#include "console.h"
#include "console_commands.h"
#include "auto_update.h"
#include "demo_provenance.h"
#include "display_adjustments.h"
#include "flat_textures.h"
#include "frame_limiter.h"
#include "frame_metrics.h"
#include "game_window.h"
#include "nvidia_profile.h"
#include "profiler_overlay.h"
#include "projected_shadow_cache.h"
#include "portal2_context.h"
#include "runtime_verification.h"
#include "sdk.h"

#include <Windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace p2boost {
	class Plugin final : public sdk::IServerPluginCallbacks {
	public:
		bool Load( sdk::create_interface_fn_t interface_factory, sdk::create_interface_fn_t ) override {
			// publish the instance before engine-owned command callbacks can fire.
			m_instance = this;

			// configure the NVIDIA application profile before installing engine hooks.
			m_modules.refresh( );
			m_verification = verify_runtime( m_modules );
			m_device       = find_dxvk_device( m_modules );
			if( m_verification.m_dxvk_vulkan ) {
				m_nvidia_profile = configure_nvidia_profile( m_device );
				if( m_nvidia_profile.m_state == nvidia_profile_state_t::failed )
					m_nvidia_profile = configure_nvidia_profile_elevated( );

				if( m_nvidia_profile.m_state == nvidia_profile_state_t::applied ) {
					m_instance = nullptr;
					restart_after_nvidia_profile_update( );
					return false;
				}
			}

			// acquire every versioned Source interface exactly once for this load.
			m_interfaces.connect( interface_factory, m_modules );

			// register the public command surface as one reversible transaction.
			if( !register_commands( ) ) {
				console_print( "[p2boost] failed to register console commands\n" );
				m_commands.disconnect( );
				m_interfaces.disconnect( );
				m_instance = nullptr;
				return false;
			}

			// Portal 2 resolves mat_queue_mode to 0 on modern CPUs; force the
			// multicore render path now and again at every level load.
			enforce_render_threading( );

			// Portal 2 shipped these public SDK rendering controls with FCVAR_CHEAT.
			// removing only that flag makes the original engine path usable while
			// preserving archive, client, bounds, and replication behavior.
			const bool cvars_ready = !m_verification.m_supported ||
			                         ( m_commands.remove_flags( "mat_viewportscale", cvar_flag_cheat ) &&
			                           m_commands.remove_flags( "mat_viewportupscale", cvar_flag_cheat ) &&
			                           m_commands.remove_flags( "r_drawparticles", cvar_flag_cheat ) );
			if( !cvars_ready )
				m_verification.m_errors.emplace_back( "cvars unavailable" );

			if( m_verification.m_supported && !m_overlay.connect( m_modules, m_interfaces, &m_metrics ) )
				m_verification.m_errors.emplace_back( "profiler client-paint interface unavailable" );
			if( m_verification.m_supported && !m_demo_provenance.connect( m_modules, m_interfaces.engine_client( ) ) )
				m_verification.m_errors.emplace_back( "demo command-buffer xref unavailable" );

			// each feature validates its own signatures and remains disabled on failure.
			if( m_verification.m_supported && !m_frame_limiter.install( m_modules, &m_metrics ) )
				m_verification.m_errors.emplace_back( "frame-limiter signature mismatch" );
			if( m_verification.m_supported && !m_shadow_cache.install( m_modules, m_interfaces, &m_metrics ) )
				m_verification.m_errors.emplace_back( "projected-shadow signature mismatch" );
			if( m_verification.m_supported && !m_flat_textures.install( m_modules, m_interfaces, m_device ) )
				m_verification.m_errors.emplace_back( "optional flat-texture hooks unavailable" );
			if( m_verification.m_supported && !m_display_adjustments.install( m_commands, m_device, &m_metrics ) )
				m_verification.m_errors.emplace_back( "display-adjustment hooks unavailable" );
			if( m_verification.m_supported && !m_display_adjustments.install_viewport_commit_patch( m_modules ) )
				m_verification.m_errors.emplace_back( "viewport-commit signature mismatch" );

			// enable performance fixes only when the complete supported set is ready.
			m_enabled = can_enable( );
			apply_enabled_state( );
			m_last_focused = game_is_foreground( );
			m_metrics.set_focused( m_last_focused );

			const std::string load_label = m_verification.m_game.empty( ) ? std::string{ } : std::string{ " (" } + m_verification.m_game + ")";
			console_print_color( console_color::green, "[p2boost] p2boost v%s loaded%s\n", P2BOOST_VERSION, load_label.c_str( ) );
			if( !m_verification.m_supported ) {
				console_print_color( console_color::red, "[p2boost] ERROR: the running game build is not supported. no hooks were installed.\n" );
				for( const auto& error : m_verification.m_errors )
					console_print( "[p2boost]   - %s\n", error.c_str( ) );
			} else if( !m_verification.m_errors.empty( ) ) {
				console_print_color( console_color::yellow, "[p2boost] loaded with partial features:\n" );
				for( const auto& error : m_verification.m_errors )
					console_print( "[p2boost]   - %s\n", error.c_str( ) );
			}

			console_print( "[p2boost] features: frame_limiter=%s shadow_cache=%s flat_textures=%s display=%s viewport_patch=%s overlay=%s\n",
			               m_frame_limiter.installed( ) ? "OK" : "OFF",
			               m_shadow_cache.installed( ) ? "OK" : "OFF",
			               m_flat_textures.available( ) ? "OK" : "OFF",
			               m_display_adjustments.available( ) ? "OK" : "OFF",
			               m_display_adjustments.viewport_commit_patched( ) ? "OK" : "OFF",
			               m_overlay.available( ) ? "OK" : "OFF" );
			console_print( "[p2boost] backend=%s fixes=%s\n",
			               m_verification.m_dxvk_vulkan ? "DXVK/Vulkan" : "unsupported",
			               m_enabled ? "enabled" : "disabled" );
			if( m_verification.m_dxvk_vulkan && !m_verification.m_dxvk_shipped )
				console_print( "[p2boost] dxvk_d3d9.dll is %s (%s); performance measurements use this exact binary\n",
				               m_verification.m_dxvk_approved ? "an approved replacement build" : "an unrecognized build",
				               m_verification.m_dxvk_sha256.c_str( ) );
			if( m_nvidia_profile.m_state == nvidia_profile_state_t::unavailable || m_nvidia_profile.m_state == nvidia_profile_state_t::failed )
				console_print( "[p2boost] NVIDIA profile setup %s at %s (%d); no driver settings were changed\n",
				               nvidia_profile_state_name( m_nvidia_profile.m_state ),
				               m_nvidia_profile.m_operation,
				               m_nvidia_profile.m_status );
			return true;
		}

		void Unload( ) override {
			// remove feature callbacks before releasing their shared engine bindings.
			m_display_adjustments.remove( );
			m_flat_textures.remove( );
			m_overlay.set_mode( profiler_mode_t::off, m_metrics );
			m_overlay.disconnect( );
			m_demo_provenance.disconnect( );
			m_shadow_cache.remove( );
			m_frame_limiter.remove( );
			m_commands.disconnect( );
			m_interfaces.disconnect( );
			m_device   = nullptr;
			m_instance = nullptr;
			console_print( "[p2boost] unloaded\n" );
		}

		void Pause( ) override {
			m_enabled = false;
			apply_enabled_state( );
		}

		void UnPause( ) override {
			m_enabled = can_enable( );
			apply_enabled_state( );
		}

		const char* GetPluginDescription( ) override {
			return "p2boost DXVK/Vulkan performance patch v" P2BOOST_VERSION;
		}

		// Portal 2's mat_queue_mode auto-detection resolves to 0 (single
		// threaded rendering) on modern CPUs, serialising the whole render
		// path on the main thread. forcing 2 moves draw submission onto the
		// render worker. the engine re-evaluates the mode at map load and
		// device reset, so this runs at every LevelInit and Reset. the
		// command is executed through the engine's own client-command path so
		// the change callback always runs without depending on a recovered
		// ConVar vtable slot.
		void enforce_render_threading( ) const noexcept {
			if( !m_force_multicore )
				return;

			const con_var_t* queue_mode = m_commands.find_variable( "mat_queue_mode" );
			if( !queue_mode || queue_mode->value_integer( ) >= 2 )
				return;

			if( sdk::IVEngineClient* engine_client = m_interfaces.engine_client( ) ) {
				const int before = queue_mode->value_integer( );
				engine_client->ExecuteClientCmd( "mat_queue_mode 2" );
				const int after = m_commands.find_variable( "mat_queue_mode" )->value_integer( );
				if( after != 2 )
					console_print( "[p2boost] mat_queue_mode %d -> 2 failed\n", before );
				else if( before != 2 )
					console_print( "[p2boost] mat_queue_mode %d -> 2 (multicore rendering enabled)\n", before );
			}
		}

		void LevelInit( const char* ) override {
			m_demo_provenance.reset( );
			m_shadow_cache.invalidate( );
			enforce_render_threading( );
		}

		void ServerActivate( sdk::edict_t*, int, int client_max ) override {
			// the activated server owns a new receiver and entity lifetime.
			m_shadow_cache.invalidate( );
			m_metrics.set_coop( client_max > 1 );
		}

		void GameFrame( bool simulating ) override {
			// mirror only the state needed by diagnostics.
			m_metrics.set_simulating( simulating );
			m_demo_provenance.update( P2BOOST_VERSION, m_enabled, m_flat_textures.enabled( ) );

			const bool focused = game_is_foreground( );
			m_metrics.set_focused( focused );
			if( focused != m_last_focused ) {
				// DXVK can recreate swapchain-backed resources after a display
				// transition. never carry a shadow cache across that boundary,
				// and re-assert the render thread mode the engine may reset.
				m_shadow_cache.invalidate( );
				enforce_render_threading( );
				m_last_focused = focused;
			}
		}

		void LevelShutdown( ) override {
			// release map-owned state before Source begins destroying its resources.
			m_metrics.set_simulating( false );
			m_metrics.set_coop( false );
			m_flat_textures.discard_pending_uploads( );
			m_shadow_cache.invalidate( );
		}

		void ClientFullyConnect( sdk::edict_t* ) override {
			// A completed sign-on can replace the entire client entity set.
			m_shadow_cache.invalidate( );
		}

		void ClientActive( sdk::edict_t* ) override {
			// the active transition makes newly received renderables visible.
			m_shadow_cache.invalidate( );
		}

		void ClientDisconnect( sdk::edict_t* ) override {
			// never retain receivers owned by a departing co-op player.
			m_shadow_cache.invalidate( );
		}

		void ClientPutInServer( sdk::edict_t*, const char* ) override {
			// A joining player introduces a new renderable hierarchy.
			m_shadow_cache.invalidate( );
		}

		void SetCommandClient( int ) override {
		}

		void ClientSettingsChanged( sdk::edict_t* ) override {
		}

		sdk::PluginResult ClientConnect( bool*, sdk::edict_t*, const char*, const char*, char*, int ) override {
			return sdk::Continue;
		}

		sdk::PluginResult ClientCommand( sdk::edict_t*, const sdk::CCommand& args ) override {
			// dispatch owned commands and leave every other command untouched.
			const char* command = args.Arg( 0 );

			if( _stricmp( command, "p2boost_status" ) == 0 )
				return status_command( );
			if( _stricmp( command, "p2boost_enable" ) == 0 )
				return enable_command( args );
			if( _stricmp( command, "p2boost_flat_textures" ) == 0 )
				return flat_textures_command( args );
			if( _stricmp( command, "p2boost_trace" ) == 0 )
				return trace_command( args );
			if( _stricmp( command, "p2boost_overlay" ) == 0 )
				return overlay_command( args );
			if( _stricmp( command, "p2boost_dump_metrics" ) == 0 )
				return dump_metrics_command( );
			if( _stricmp( command, "p2boost_update" ) == 0 )
				return update_command( args );
			if( _stricmp( command, "p2boost_multicore" ) == 0 )
				return multicore_command( args );
			if( _stricmp( command, "r_intensity" ) == 0 )
				return intensity_command( args );

			return sdk::Continue;
		}

		sdk::PluginResult NetworkIDValidated( const char*, const char* ) override {
			return sdk::Continue;
		}

		void OnQueryCvarValueFinished( int, sdk::edict_t*, sdk::QueryStatus, const char*, const char* ) override {
		}

		void OnEdictAllocated( sdk::edict_t* ) override {
			// handles may be reused, so allocation begins a new cache lifetime.
			m_shadow_cache.invalidate( );
		}

		void OnEdictFreed( const sdk::edict_t* ) override {
			// receiver lists must not retain a pointer to a destroyed renderable.
			m_shadow_cache.invalidate( );
		}

	private:
		struct command_descriptor_t {
			const char* m_name;
			const char* m_help;
		};

		bool register_commands( ) {
			static constexpr command_descriptor_t descriptors[]{
			    { "p2boost_status", "Print p2boost runtime state." },
			    { "p2boost_enable", "Enable or disable verified fixes." },
			    { "p2boost_flat_textures", "Enable or disable alpha-safe flat textures." },
			    { "p2boost_trace", "Enable or disable frame tracing." },
			    { "p2boost_overlay", "Select the full or FPS-only frame profiler." },
			    { "p2boost_dump_metrics", "Write frame metrics to CSV." },
			    { "p2boost_update", "Update p2boost to the latest release. Optional 'force' reinstalls." },
			    { "p2boost_multicore", "Force mat_queue_mode 2 (multicore rendering). Re-enforced on every level load." },
			    { "r_intensity", "Scale final-frame RGB brightness (0.0 to 2.0, 1.0 neutral)." },
			};

			if( !m_commands.connect( m_interfaces.cvar( ) ) )
				return false;

			for( const command_descriptor_t& descriptor : descriptors ) {
				if( !m_commands.add( descriptor.m_name, descriptor.m_help, &command_thunk ) )
					return false;
			}
			return true;
		}

		// parse the exact 0/1 syntax shared by toggle commands.
		static bool parse_toggle( const sdk::CCommand& args, bool& enabled ) noexcept {
			if( args.ArgC( ) != 2 )
				return false;

			if( std::strcmp( args.Arg( 1 ), "0" ) == 0 ) {
				enabled = false;
				return true;
			}

			if( std::strcmp( args.Arg( 1 ), "1" ) == 0 ) {
				enabled = true;
				return true;
			}

			return false;
		}

		// accept only the documented profiler modes instead of coercing input.
		static bool parse_profiler_mode( const sdk::CCommand& args, profiler_mode_t& mode ) noexcept {
			if( args.ArgC( ) != 2 )
				return false;

			if( std::strcmp( args.Arg( 1 ), "0" ) == 0 )
				mode = profiler_mode_t::off;
			else if( std::strcmp( args.Arg( 1 ), "1" ) == 0 )
				mode = profiler_mode_t::full;
			else if( std::strcmp( args.Arg( 1 ), "2" ) == 0 )
				mode = profiler_mode_t::fps;
			else
				return false;

			return true;
		}

		// print one machine-readable snapshot for support and automated tests.
		sdk::PluginResult status_command( ) const {
			const con_var_t* queue_mode = m_commands.find_variable( "mat_queue_mode" );
			const int        queue      = queue_mode ? queue_mode->value_integer( ) : -999;
			console_print( "[p2boost] supported=%d dxvk_vulkan=%d dxvk_shipped=%d dxvk_hash=%s nvidia_profile=%s nvidia_detail=%s:%d enabled=%d "
			               "limiter=%d shadow_cache=%d shadow_hits=%u shadow_culled=%u tracing=%d overlay=%d overlay_available=%d "
			               "display_available=%d viewport_commit=%d gamma_passes=%llu viewport_passes=%llu intensity=%g demo_marker=%d "
			               "flat_available=%d flat=%d flat_textures=%zu flat_ready=%zu flat_normal=%d flat_binds=%llu "
			               "flat_uploads=%llu/%llu/%llu sar=%d samples=%zu mat_queue_mode=%d multicore=%d root=%s\n",
			               m_verification.m_supported,
			               m_verification.m_dxvk_vulkan,
			               m_verification.m_dxvk_shipped,
			               m_verification.m_dxvk_sha256.c_str( ),
			               nvidia_profile_state_name( m_nvidia_profile.m_state ),
			               m_nvidia_profile.m_operation,
			               m_nvidia_profile.m_status,
			               m_enabled,
			               m_frame_limiter.installed( ),
			               m_shadow_cache.installed( ),
			               m_shadow_cache.hit_count( ),
			               m_shadow_cache.culled_count( ),
			               m_metrics.tracing( ),
			               m_overlay.mode( ),
			               m_overlay.available( ),
			               m_display_adjustments.available( ),
			               m_display_adjustments.viewport_commit_patched( ),
			               static_cast< unsigned long long >( m_display_adjustments.gamma_passes( ) ),
			               static_cast< unsigned long long >( m_display_adjustments.viewport_passes( ) ),
			               m_display_adjustments.intensity( ),
			               m_demo_provenance.available( ),
			               m_flat_textures.available( ),
			               m_flat_textures.enabled( ),
			               m_flat_textures.texture_count( ),
			               m_flat_textures.ready_texture_count( ),
			               m_flat_textures.normal_ready( ),
			               static_cast< unsigned long long >( m_flat_textures.substitution_count( ) ),
			               static_cast< unsigned long long >( m_flat_textures.texture_upload_count( ) ),
			               static_cast< unsigned long long >( m_flat_textures.surface_upload_count( ) ),
			               static_cast< unsigned long long >( m_flat_textures.device_upload_count( ) ),
			               static_cast< bool >( Module{ L"sar.dll" } ),
			               m_metrics.count( ),
			               queue,
			               m_force_multicore ? 1 : 0,
			               m_verification.m_game_root.c_str( ) );

			const std::string game_label = m_verification.m_game.empty( ) ? std::string{ } : std::string{ " (" } + m_verification.m_game + ")";
			console_print_color( m_verification.m_supported ? console_color::green : console_color::red,
			                     "[p2boost] status: %s%s\n",
			                     m_verification.m_supported ? "supported build" : "UNSUPPORTED BUILD",
			                     game_label.c_str( ) );
			console_print( "[p2boost]   version       %s\n", P2BOOST_VERSION );
			console_print( "[p2boost]   backend       %s (dxvk %s)\n",
			               m_verification.m_dxvk_vulkan ? "DXVK/Vulkan" : "NOT ACTIVE",
			               m_verification.m_dxvk_shipped    ? "shipped"
			               : m_verification.m_dxvk_approved ? "approved replacement"
			                                                : "unrecognized" );
			console_print( "[p2boost]   render thread %s (mat_queue_mode=%d)\n",
			               queue >= 2 ? "multicore" : ( m_force_multicore ? "forcing" : "single" ),
			               queue );
			console_print_color( m_enabled ? console_color::green : console_color::yellow,
			                     "[p2boost]   fixes         %s\n", m_enabled ? "enabled" : "disabled" );
			console_print( "[p2boost]   frame limiter %s\n",
			               m_frame_limiter.installed( ) ? "OK" : "OFF" );
			console_print( "[p2boost]   shadow cache  %s (%u hits, %u culled)\n",
			               m_shadow_cache.installed( ) ? "OK" : "OFF",
			               m_shadow_cache.hit_count( ),
			               m_shadow_cache.culled_count( ) );
			console_print( "[p2boost]   flat textures %s (%zu of %zu ready%s)\n",
			               m_flat_textures.available( ) ? ( m_flat_textures.enabled( ) ? "on" : "off" ) : "OFF",
			               m_flat_textures.ready_texture_count( ),
			               m_flat_textures.texture_count( ),
			               m_flat_textures.normal_ready( ) ? ", normal ready" : "" );
			console_print( "[p2boost]   display       %s (gamma=%llu viewport=%llu intensity=%g)\n",
			               m_display_adjustments.available( ) ? "OK" : "OFF",
			               static_cast< unsigned long long >( m_display_adjustments.gamma_passes( ) ),
			               static_cast< unsigned long long >( m_display_adjustments.viewport_passes( ) ),
			               m_display_adjustments.intensity( ) );
			console_print( "[p2boost]   viewport patch %s\n",
			               m_display_adjustments.viewport_commit_patched( ) ? "OK" : "OFF" );
			console_print( "[p2boost]   profiler      %s\n", m_overlay.available( ) ? "OK" : "OFF" );
			if( !m_verification.m_errors.empty( ) ) {
				console_print( "[p2boost]   issues:\n" );
				for( const auto& error : m_verification.m_errors )
					console_print( "[p2boost]     - %s\n", error.c_str( ) );
			}

			return sdk::Stop;
		}

		// toggle only the benchmark-proven performance fixes.
		sdk::PluginResult enable_command( const sdk::CCommand& args ) {
			bool enabled{ };
			if( !parse_toggle( args, enabled ) ) {
				console_print( "usage: p2boost_enable 0|1\n" );
				return sdk::Stop;
			}

			if( enabled && !can_enable( ) ) {
				console_print( "[p2boost] cannot enable: unsupported build or backend\n" );
				return sdk::Stop;
			}

			m_enabled = enabled;
			apply_enabled_state( );
			return sdk::Stop;
		}

		// rebuild optional flat companions when the visual mode changes.
		sdk::PluginResult flat_textures_command( const sdk::CCommand& args ) {
			bool enabled{ };
			if( !parse_toggle( args, enabled ) ) {
				console_print( "usage: p2boost_flat_textures 0|1\n" );
				return sdk::Stop;
			}

			if( !m_flat_textures.set_enabled( enabled ) )
				console_print( "[p2boost] flat textures unavailable on this renderer/build\n" );
			else
				console_print( "[p2boost] flat textures %s; color maps are solid, normals are neutral, and UI/dynamic inputs are preserved\n",
				               enabled ? "enabled" : "disabled" );

			return sdk::Stop;
		}

		// enable detailed CSV sampling without changing frame behavior.
		sdk::PluginResult trace_command( const sdk::CCommand& args ) {
			bool enabled{ };
			if( !parse_toggle( args, enabled ) )
				console_print( "usage: p2boost_trace 0|1\n" );
			else
				m_metrics.set_tracing( enabled );

			return sdk::Stop;
		}

		// select the full profiler, compact FPS counter, or hidden mode.
		sdk::PluginResult overlay_command( const sdk::CCommand& args ) {
			profiler_mode_t mode{ };
			if( !parse_profiler_mode( args, mode ) )
				console_print( "usage: p2boost_overlay 0|1|2\n" );
			else
				m_overlay.set_mode( mode, m_metrics );

			if( !m_overlay.available( ) )
				console_print( "[p2boost] profiler unavailable: local client paint interfaces are missing\n" );

			return sdk::Stop;
		}

		// write the current ring buffer beside the game installation.
		sdk::PluginResult dump_metrics_command( ) const {
			const std::string path = m_verification.m_game_root + "\\p2boost_metrics.csv";
			console_print( "[p2boost] metrics %s: %s\n", m_metrics.dump( path ) ? "written" : "write failed", path.c_str( ) );
			return sdk::Stop;
		}

		// download and install the latest p2boost release in the background.
		sdk::PluginResult update_command( const sdk::CCommand& args ) {
			bool force = false;
			if( args.ArgC( ) == 2 ) {
				if( _stricmp( args.Arg( 1 ), "force" ) != 0 ) {
					console_print( "usage: p2boost_update [force]\n" );
					return sdk::Stop;
				}
				force = true;
			} else if( args.ArgC( ) != 1 ) {
				console_print( "usage: p2boost_update [force]\n" );
				return sdk::Stop;
			}

			if( m_auto_update.busy( ) ) {
				console_print( "[p2boost] an update is already in progress\n" );
				return sdk::Stop;
			}

			m_auto_update.update( force );
			return sdk::Stop;
		}

		// set or print the Quake-style final-frame RGB brightness multiplier.
		sdk::PluginResult intensity_command( const sdk::CCommand& args ) {
			if( args.ArgC( ) == 1 ) {
				console_print( "[p2boost] r_intensity = %g (0.0 to 2.0, 1.0 neutral)\n", m_display_adjustments.intensity( ) );
				return sdk::Stop;
			}

			if( args.ArgC( ) != 2 ) {
				console_print( "usage: r_intensity <0.0..2.0>\n" );
				return sdk::Stop;
			}

			char*       end{ };
			const float value = std::strtof( args.Arg( 1 ), &end );
			if( end == args.Arg( 1 ) || *end != '\0' || value != value || value < p2boost::min_intensity || value > p2boost::max_intensity ) {
				console_print( "usage: r_intensity <0.0..2.0>\n" );
				return sdk::Stop;
			}

			m_display_adjustments.set_intensity( value );
			console_print( "[p2boost] r_intensity = %g\n", m_display_adjustments.intensity( ) );
			return sdk::Stop;
		}

		// force multicore rendering (mat_queue_mode 2) on and off.
		sdk::PluginResult multicore_command( const sdk::CCommand& args ) {
			bool enabled{ };
			if( !parse_toggle( args, enabled ) ) {
				console_print( "usage: p2boost_multicore 0|1\n" );
				return sdk::Stop;
			}

			m_force_multicore = enabled;
			if( enabled )
				enforce_render_threading( );
			console_print( "[p2boost] multicore rendering %s\n", enabled ? "enabled" : "disabled" );
			return sdk::Stop;
		}

		bool can_enable( ) const noexcept {
			// partial activation would make A/B results impossible to interpret.
			return m_verification.m_supported && m_verification.m_dxvk_vulkan && m_frame_limiter.installed( ) &&
			       m_shadow_cache.installed( );
		}

		void apply_enabled_state( ) noexcept {
			// both performance fixes share one reversible master switch.
			m_frame_limiter.set_enabled( m_enabled );
			m_shadow_cache.set_enabled( m_enabled );
			m_overlay.set_fixes_enabled( m_enabled );
		}

		static void __cdecl command_thunk( const sdk::CCommand& command ) {
			// Source invokes a C callback, so forward through the live instance.
			if( m_instance )
				m_instance->ClientCommand( nullptr, command );
		}

		static Plugin* m_instance;

		verification_result_t   m_verification;
		portal2_modules_t       m_modules;
		Portal2Interfaces       m_interfaces;
		nvidia_profile_result_t m_nvidia_profile;
		FrameMetrics            m_metrics;
		FrameLimiter            m_frame_limiter;
		ProjectedShadowCache    m_shadow_cache;
		DisplayAdjustments      m_display_adjustments;
		AutoUpdate              m_auto_update;
		DemoProvenance          m_demo_provenance;
		FlatTextures            m_flat_textures;
		ProfilerOverlay         m_overlay;
		ConsoleCommandRegistry  m_commands;
		IDirect3DDevice9*       m_device{ };
		bool                    m_enabled{ };
		bool                    m_last_focused{ };
		bool                    m_force_multicore{ true };
	};

	Plugin* Plugin::m_instance{ };
	Plugin  g_plugin;

}

extern "C" __declspec( dllexport ) void* CreateInterface( const char* name, int* return_code ) {
	// expose only the callback interface requested by Portal 2's plugin loader.
	if( name && std::strcmp( name, "ISERVERPLUGINCALLBACKS003" ) == 0 ) {
		if( return_code )
			*return_code = 0;
		return &p2boost::g_plugin;
	}

	if( return_code )
		*return_code = 1;
	return nullptr;
}

BOOL APIENTRY DllMain( HMODULE, DWORD, LPVOID ) {
	return TRUE;
}
