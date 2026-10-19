#include "console.h"
#include "console_commands.h"
#include "auto_update.h"
#include "demo_provenance.h"
#include "display_adjustments.h"
#include "remote_view.h"
#include "flashlight_buckets.h"
#include "flat_textures.h"
#include "frame_limiter.h"
#include "frame_metrics.h"
#include "game_window.h"
#include "nvidia_profile.h"
#include "occlusion_skip.h"
#include "queued_frames.h"
#include "vgui_screen_cache.h"
#include "vgui_screen_shadow_skip.h"
#include "shadow_depth_reuse.h"
#include "profiler_overlay.h"
#include "projected_shadow_cache.h"
#include "portal2_context.h"
#include "runtime_verification.h"
#include "sdk.h"
#include "xinput_throttle.h"

#include <Windows.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace p2boost {
	namespace {

		// every performance change, controlled through p2boost_fix. each one also
		// follows the global p2boost_enable switch.
		enum class fix_t {
			frame_limiter,
			shadow_cache,
			multicore,
			flashlight_flush,
			xinput_throttle,
			occlusion_skip,
			vgui_shadow_skip,
			shadow_depth_reuse,
			shadow_cull,
			partner_view_fps,
			shadow_depth_fps,
			vgui_screen_fps,
			frame_queue_depth,
			count
		};

		struct fix_info_t {
			const char* m_name;
			int         m_default;
			int         m_maximum;
			const char* m_help;
		};

		constexpr fix_info_t fix_table[]{
		    { "frame_limiter", 1, 1, "carry fps_max deadline overshoot instead of dropping it" },
		    { "shadow_cache", 1, 1, "skip unchanged projected-light receiver rebuilds" },
		    { "multicore", 1, 1, "keep mat_queue_mode 2 (multicore rendering)" },
		    { "flashlight_flush", 1, 1, "flush flashlight surface buckets in O(used)" },
		    { "xinput_throttle", 1, 1, "re-probe empty controller slots once per second" },
		    { "occlusion_skip", 1, 1, "skip the occlusion pass on maps without occluders" },
		    { "vgui_shadow_skip", 1, 1, "keep in-world VGUI screens out of shadow-depth maps" },
		    { "shadow_depth_reuse", 1, 1, "reuse depth maps another view rendered this frame" },
		    { "shadow_cull", 1, 1, "skip depth maps whose light volume misses the view" },
		    { "partner_view_fps", 120, 1000, "co-op partner's view (+remote_view, or the split-screen player not in control) renders per second (0 = every frame)" },
		    { "shadow_depth_fps", 120, 1000, "also reuse shadow-depth maps across frames, refreshed this often (0 = off)" },
		    { "vgui_screen_fps", 0, 1000, "experimental: in-world VGUI screens repaint this often from a cached texture (0 = off)" },
		    { "frame_queue_depth", 2, 2, "frames the render thread may run behind the game (1 = the engine's one)" },
		};

		constexpr std::size_t fix_count{ static_cast< std::size_t >( fix_t::count ) };
		static_assert( std::size( fix_table ) == fix_count, "every fix needs a table entry" );

		std::array< int, fix_count > default_fixes( ) noexcept {
			std::array< int, fix_count > values{ };
			for( std::size_t index = 0; index < fix_count; ++index )
				values[ index ] = fix_table[ index ].m_default;
			return values;
		}

	}

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
			if( m_verification.m_supported && !m_flashlight_buckets.install( m_modules ) )
				m_verification.m_errors.emplace_back( "flashlight-bucket signature mismatch" );
			if( m_verification.m_supported && !m_xinput_throttle.install( m_modules ) )
				m_verification.m_errors.emplace_back( "inputsystem XInput signature mismatch" );
			if( m_verification.m_supported && !m_occlusion_skip.install( m_modules ) )
				m_verification.m_errors.emplace_back( "occlusion-pass signature mismatch" );
			if( m_verification.m_supported && !m_vgui_screen_shadow_skip.install( m_modules ) )
				m_verification.m_errors.emplace_back( "vgui-screen shadow-skip signature mismatch" );
			if( m_verification.m_supported && !m_shadow_depth_reuse.install( m_modules ) )
				m_verification.m_errors.emplace_back( "optional shadow-depth reuse unavailable" );
			if( m_verification.m_supported && !m_queued_frames.install( m_modules, m_interfaces ) )
				m_verification.m_errors.emplace_back( "optional frame-queue ring unavailable" );
			if( m_verification.m_supported && !m_vgui_screen_cache.install( m_modules, m_interfaces, m_vgui_screen_shadow_skip.draw_model_slot( ), m_verification.m_game_root ) )
				m_verification.m_errors.emplace_back( "optional vgui-screen cache unavailable" );
			if( m_verification.m_supported && !m_flat_textures.install( m_modules, m_interfaces, m_device ) )
				m_verification.m_errors.emplace_back( "optional flat-texture hooks unavailable" );
			if( m_verification.m_supported && !m_display_adjustments.install( m_commands, m_device, &m_metrics ) )
				m_verification.m_errors.emplace_back( "display-adjustment hooks unavailable" );
			if( m_verification.m_supported && !m_display_adjustments.install_viewport_commit_patch( m_modules ) )
				m_verification.m_errors.emplace_back( "viewport-commit signature mismatch" );
			if( m_verification.m_supported &&
			    !m_remote_view.install( m_modules, m_interfaces, m_commands, m_device,
			                            m_display_adjustments.available( ) ? &m_display_adjustments : nullptr ) )
				m_verification.m_errors.emplace_back( "optional remote-view hooks unavailable" );

			// a device Reset leaves every depth texture's contents undefined.
			if( m_shadow_depth_reuse.installed( ) && m_display_adjustments.available( ) )
				m_display_adjustments.add_reset_listener( []( void* context ) { static_cast< ShadowDepthReuse* >( context )->invalidate( ); },
				                                          &m_shadow_depth_reuse );

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
			// the forced queue mode belongs to the plugin; hand it back first.
			restore_render_threading( );

			// remove feature callbacks before releasing their shared engine bindings.
			// remote view and depth reuse register reset listeners on the display hooks.
			m_display_adjustments.remove_reset_listener( &m_shadow_depth_reuse );
			m_remote_view.remove( );
			m_display_adjustments.remove( );
			m_flat_textures.remove( );
			m_overlay.set_mode( profiler_mode_t::off, m_metrics );
			m_overlay.disconnect( );
			m_demo_provenance.disconnect( );
			m_shadow_cache.remove( );
			m_flashlight_buckets.remove( );
			m_xinput_throttle.remove( );
			m_occlusion_skip.remove( );
			m_vgui_screen_cache.remove( );
			m_queued_frames.remove( );
			m_vgui_screen_shadow_skip.remove( );
			m_shadow_depth_reuse.remove( );
			m_frame_limiter.remove( );
			m_commands.disconnect( );
			m_interfaces.disconnect( );
			m_device          = nullptr;
			m_instance        = nullptr;
			m_queue_mode      = nullptr;
			m_last_queue_mode = -1;
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
		// threaded rendering) on modern CPUs, and Portal Stories: Mel's maps
		// additionally force 0 through a logic_auto entity on every spawn,
		// which runs after LevelInit. forcing 2 moves draw submission onto the
		// render worker. the command is executed through the engine's own
		// client-command path so the change callback always runs; the per-frame
		// check re-asserts the mode after any map or device reset overrides it.
		void enforce_render_threading( ) const noexcept {
			if( !m_enabled || !fix( fix_t::multicore ) )
				return;

			if( !m_queue_mode )
				m_queue_mode = m_commands.find_variable( "mat_queue_mode" );
			if( !m_queue_mode || m_queue_mode->value_integer( ) >= 2 )
				return;

			if( sdk::IVEngineClient* engine_client = m_interfaces.engine_client( ) ) {
				const int before = m_queue_mode->value_integer( );
				// remember the game's own setting once so disabling restores it.
				if( !m_queue_mode_forced ) {
					m_original_queue_mode = before;
					m_queue_mode_forced   = true;
				}
				engine_client->ExecuteClientCmd( "mat_queue_mode 2" );
				const int after = m_queue_mode->value_integer( );
				if( after != 2 )
					console_print( "[p2boost] mat_queue_mode %d -> 2 failed\n", before );
				else if( before != 2 && before != m_last_queue_mode )
					console_print( "[p2boost] mat_queue_mode %d -> 2 (multicore rendering enabled)\n", before );
				m_last_queue_mode = after;
			}
		}

		// hand mat_queue_mode back to the value the game had before forcing.
		void restore_render_threading( ) const noexcept {
			if( !m_queue_mode_forced )
				return;

			if( sdk::IVEngineClient* engine_client = m_interfaces.engine_client( ) ) {
				char command[ 32 ];
				std::snprintf( command, sizeof( command ), "mat_queue_mode %d", m_original_queue_mode );
				engine_client->ExecuteClientCmd( command );
				m_queue_mode_forced = false;
				m_last_queue_mode   = -1;
				console_print( "[p2boost] mat_queue_mode restored to %d\n", m_original_queue_mode );
			}
		}

		void LevelInit( const char* ) override {
			m_demo_provenance.reset( );
			m_shadow_cache.invalidate_all( );
			m_shadow_depth_reuse.invalidate( );
			m_vgui_screen_cache.invalidate( );
			m_remote_view.invalidate( );
			enforce_render_threading( );
		}

		void ServerActivate( sdk::edict_t*, int, int client_max ) override {
			// the activated server owns a new receiver and entity lifetime.
			m_shadow_cache.invalidate_all( );
			m_metrics.set_coop( client_max > 1 );
		}

		void GameFrame( bool simulating ) override {
			// mirror only the state needed by diagnostics.
			m_metrics.set_simulating( simulating );
			m_demo_provenance.update( P2BOOST_VERSION, m_enabled, m_flat_textures.enabled( ) );

			// PSM's logic_auto re-forces mat_queue_mode 0 after map spawn;
			// re-assert multicore every frame so the override never sticks.
			enforce_render_threading( );

			const bool focused = game_is_foreground( );
			m_metrics.set_focused( focused );
			if( focused != m_last_focused ) {
				// DXVK can recreate swapchain-backed resources after a display
				// transition. never carry a shadow cache across that boundary,
				// and re-assert the render thread mode the engine may reset.
				m_shadow_cache.invalidate_all( );
				m_shadow_depth_reuse.invalidate( );
				m_vgui_screen_cache.invalidate( );
				m_remote_view.invalidate( );
				enforce_render_threading( );
				m_last_focused = focused;
			}
		}

		void LevelShutdown( ) override {
			// release map-owned state before Source begins destroying its resources.
			m_metrics.set_simulating( false );
			m_metrics.set_coop( false );
			m_flat_textures.discard_pending_uploads( );
			m_shadow_cache.invalidate_all( );
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
			if( _stricmp( command, "p2boost_fix" ) == 0 )
				return fix_command( args );
			if( _stricmp( command, "p2boost_overlay" ) == 0 )
				return overlay_command( args );
			if( _stricmp( command, "p2boost_trace" ) == 0 )
				return trace_command( args );
			if( _stricmp( command, "p2boost_update" ) == 0 )
				return update_command( args );
			if( _stricmp( command, "p2boost_flat_textures" ) == 0 )
				return flat_textures_command( args );
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
			    { "p2boost_status", "Print p2boost's state and the counters of every fix." },
			    { "p2boost_enable", "Global switch for every performance fix: p2boost_enable 0|1." },
			    { "p2boost_fix", "List fixes, or read or set one: p2boost_fix [name] [value]." },
			    { "p2boost_overlay", "Frame-time overlay: 0 off, 1 full profiler, 2 FPS counter." },
			    { "p2boost_trace", "Record frame times; p2boost_trace 0 writes p2boost_metrics.csv." },
			    { "p2boost_update", "Update p2boost to the latest release. Optional 'force' reinstalls." },
			    { "p2boost_flat_textures", "Optional visual mode with solid-color surfaces: 0|1." },
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

		// print a readable snapshot for support and benchmark logs.
		sdk::PluginResult status_command( ) const {
			const std::string game_label = m_verification.m_game.empty( ) ? std::string{ } : std::string{ " (" } + m_verification.m_game + ")";
			console_print_color( m_verification.m_supported ? console_color::green : console_color::red,
			                     "[p2boost] p2boost %s: %s%s\n", P2BOOST_VERSION,
			                     m_verification.m_supported ? "supported build" : "UNSUPPORTED BUILD", game_label.c_str( ) );
			console_print( "[p2boost]   backend   %s, dxvk %s (%s)\n",
			               m_verification.m_dxvk_vulkan ? "DXVK/Vulkan" : "NOT ACTIVE",
			               m_verification.m_dxvk_shipped    ? "shipped"
			               : m_verification.m_dxvk_approved ? "approved replacement"
			                                                : "unrecognized",
			               m_verification.m_dxvk_sha256.c_str( ) );
			console_print( "[p2boost]   nvidia    %s (%s:%d)\n", nvidia_profile_state_name( m_nvidia_profile.m_state ),
			               m_nvidia_profile.m_operation, m_nvidia_profile.m_status );
			console_print_color( m_enabled ? console_color::green : console_color::yellow,
			                     "[p2boost]   fixes     %s (p2boost_enable)\n", m_enabled ? "enabled" : "disabled" );
			for( std::size_t index = 0; index < fix_count; ++index )
				print_fix( static_cast< fix_t >( index ), true );
			console_print( "[p2boost]   display   gamma passes %llu, viewport passes %llu, r_intensity %g%s\n",
			               static_cast< unsigned long long >( m_display_adjustments.gamma_passes( ) ),
			               static_cast< unsigned long long >( m_display_adjustments.viewport_passes( ) ),
			               m_display_adjustments.intensity( ),
			               m_display_adjustments.available( ) ? "" : " (unavailable)" );
			console_print( "[p2boost]   flat      %s (%zu of %zu textures ready)\n",
			               m_flat_textures.available( ) ? ( m_flat_textures.enabled( ) ? "on" : "off" ) : "unavailable",
			               m_flat_textures.ready_texture_count( ),
			               m_flat_textures.texture_count( ) );
			console_print( "[p2boost]   overlay   %s, trace %s, demo marker %s\n",
			               m_overlay.available( ) ? "available" : "unavailable",
			               m_metrics.tracing( ) ? "on" : "off",
			               m_demo_provenance.available( ) ? "ready" : "unavailable" );
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

		// record frame times; stopping writes them beside the game executable.
		sdk::PluginResult trace_command( const sdk::CCommand& args ) {
			bool enabled{ };
			if( !parse_toggle( args, enabled ) ) {
				console_print( "usage: p2boost_trace 0|1\n" );
				return sdk::Stop;
			}

			m_metrics.set_tracing( enabled );
			if( enabled ) {
				console_print( "[p2boost] tracing frames; p2boost_trace 0 writes p2boost_metrics.csv\n" );
				return sdk::Stop;
			}

			const std::string path = m_verification.m_game_root + "\\p2boost_metrics.csv";
			console_print( "[p2boost] metrics %s: %s\n", m_metrics.dump( path ) ? "written" : "write failed", path.c_str( ) );
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

		// diagnostic for shadow_depth_fps: which lights the depth cache holds.
		void print_depth_views( ) const {
			for( const auto& view : m_shadow_depth_reuse.views( ) ) {
				if( view.m_frame < 0 )
					continue;
				console_print( "[p2boost]     light origin %.1f %.1f %.1f angles %.1f %.1f %.1f fov %.1f frame %d\n",
				               view.m_origin[ 0 ], view.m_origin[ 1 ], view.m_origin[ 2 ],
				               view.m_angles[ 0 ], view.m_angles[ 1 ], view.m_angles[ 2 ],
				               view.m_fov, view.m_frame );
			}
		}

		// parse one bounded integer.
		static bool parse_bounded( const char* text, int minimum, int maximum, int& value ) noexcept {
			char*      end{ };
			const long parsed = std::strtol( text, &end, 10 );
			if( end == text || *end != '\0' || parsed < minimum || parsed > maximum )
				return false;

			value = static_cast< int >( parsed );
			return true;
		}

		int fix( fix_t which ) const noexcept {
			return m_fixes[ static_cast< std::size_t >( which ) ];
		}

		// a fix whose hooks could not be installed on this build stays off.
		bool fix_available( fix_t which ) const noexcept {
			switch( which ) {
			case fix_t::frame_limiter:
				return m_frame_limiter.installed( );
			case fix_t::shadow_cache:
			case fix_t::shadow_cull:
				return m_shadow_cache.installed( );
			case fix_t::multicore:
				return true;
			case fix_t::flashlight_flush:
				return m_flashlight_buckets.installed( );
			case fix_t::xinput_throttle:
				return m_xinput_throttle.installed( );
			case fix_t::occlusion_skip:
				return m_occlusion_skip.installed( );
			case fix_t::vgui_shadow_skip:
				return m_vgui_screen_shadow_skip.installed( );
			case fix_t::shadow_depth_reuse:
			case fix_t::shadow_depth_fps:
				return m_shadow_depth_reuse.installed( );
			case fix_t::partner_view_fps:
				return m_remote_view.reuse_available( );
			case fix_t::vgui_screen_fps:
				return m_vgui_screen_cache.installed( );
			case fix_t::frame_queue_depth:
				return m_queued_frames.installed( );
			default:
				return false;
			}
		}

		// one line per fix: value, default, availability, and with counters
		// what it has done so far.
		void print_fix( fix_t which, bool counters ) const {
			const fix_info_t& info = fix_table[ static_cast< std::size_t >( which ) ];
			char              detail[ 240 ]{ };
			if( counters ) {
				switch( which ) {
				case fix_t::shadow_cache:
					std::snprintf( detail, sizeof( detail ), "%u hits", m_shadow_cache.hit_count( ) );
					break;
				case fix_t::shadow_cull:
					std::snprintf( detail, sizeof( detail ), "%u culled", m_shadow_cache.culled_count( ) );
					break;
				case fix_t::multicore:
					if( const con_var_t* queue = m_commands.find_variable( "mat_queue_mode" ) )
						std::snprintf( detail, sizeof( detail ), "mat_queue_mode %d", queue->value_integer( ) );
					break;
				case fix_t::xinput_throttle:
					std::snprintf( detail, sizeof( detail ), "%llu probes skipped", static_cast< unsigned long long >( m_xinput_throttle.skipped_count( ) ) );
					break;
				case fix_t::occlusion_skip:
					std::snprintf( detail, sizeof( detail ), "%llu passes skipped", static_cast< unsigned long long >( m_occlusion_skip.skipped_count( ) ) );
					break;
				case fix_t::vgui_shadow_skip:
					std::snprintf( detail, sizeof( detail ), "%llu draws skipped", static_cast< unsigned long long >( m_vgui_screen_shadow_skip.skipped_count( ) ) );
					break;
				case fix_t::shadow_depth_reuse:
					std::snprintf( detail, sizeof( detail ), "%llu rendered, %llu reused",
					               static_cast< unsigned long long >( m_shadow_depth_reuse.rendered_count( ) ),
					               static_cast< unsigned long long >( m_shadow_depth_reuse.reused_count( ) ) );
					break;
				case fix_t::shadow_depth_fps: {
					int cached = 0;
					for( const auto& view : m_shadow_depth_reuse.views( ) )
						cached += view.m_frame >= 0;
					std::snprintf( detail, sizeof( detail ), "%d lights cached", cached );
					break;
				}
				case fix_t::partner_view_fps:
					std::snprintf( detail, sizeof( detail ), "%llu rendered, %llu reused",
					               static_cast< unsigned long long >( m_remote_view.rendered_count( ) ),
					               static_cast< unsigned long long >( m_remote_view.reused_count( ) ) );
					break;
				case fix_t::vgui_screen_fps:
					std::snprintf( detail, sizeof( detail ), "%llu painted, %llu reused",
					               static_cast< unsigned long long >( m_vgui_screen_cache.painted_count( ) ),
					               static_cast< unsigned long long >( m_vgui_screen_cache.reused_count( ) ) );
					break;
				case fix_t::frame_queue_depth:
					std::snprintf( detail, sizeof( detail ), "%llu ring frames, %llu drains",
					               static_cast< unsigned long long >( m_queued_frames.ring_frames( ) ),
					               static_cast< unsigned long long >( m_queued_frames.drained_count( ) ) );
					break;
				default:
					break;
				}
			}

			console_print( "[p2boost]   %-18s %4d (default %d)%s  %s%s%s\n", info.m_name, fix( which ), info.m_default,
			               fix_available( which ) ? "" : " unavailable", detail, detail[ 0 ] ? " - " : "", info.m_help );
		}

		// list every fix, or read or set one by name.
		sdk::PluginResult fix_command( const sdk::CCommand& args ) {
			if( args.ArgC( ) == 1 ) {
				for( std::size_t index = 0; index < fix_count; ++index )
					print_fix( static_cast< fix_t >( index ), false );
				return sdk::Stop;
			}

			std::size_t index = 0;
			while( index < fix_count && _stricmp( fix_table[ index ].m_name, args.Arg( 1 ) ) != 0 )
				++index;
			if( index == fix_count || args.ArgC( ) > 3 ) {
				console_print( "usage: p2boost_fix [name] [value]; run p2boost_fix for the list\n" );
				return sdk::Stop;
			}

			const fix_t which = static_cast< fix_t >( index );
			if( args.ArgC( ) == 3 ) {
				int value{ };
				if( !parse_bounded( args.Arg( 2 ), 0, fix_table[ index ].m_maximum, value ) ) {
					console_print( "usage: p2boost_fix %s <0..%d>\n", fix_table[ index ].m_name, fix_table[ index ].m_maximum );
					return sdk::Stop;
				}
				if( value > 0 && !fix_available( which ) ) {
					console_print( "[p2boost] %s is unavailable on this build\n", fix_table[ index ].m_name );
					return sdk::Stop;
				}

				m_fixes[ index ] = value;
				apply_enabled_state( );
			}

			print_fix( which, true );
			if( which == fix_t::shadow_depth_fps && args.ArgC( ) == 2 )
				print_depth_views( );
			return sdk::Stop;
		}

		bool can_enable( ) const noexcept {
			// partial activation would make A/B results impossible to interpret.
			return m_verification.m_supported && m_verification.m_dxvk_vulkan && m_frame_limiter.installed( ) &&
			       m_shadow_cache.installed( );
		}

		void apply_enabled_state( ) noexcept {
			// p2boost_enable is the global A/B switch; p2boost_fix controls each change.
			auto on = [ this ]( fix_t which ) {
				return m_enabled && fix( which ) != 0;
			};

			m_frame_limiter.set_enabled( on( fix_t::frame_limiter ) );
			m_shadow_cache.set_enabled( on( fix_t::shadow_cache ) );
			m_shadow_cache.set_exact_cull( on( fix_t::shadow_cull ) );
			if( m_remote_view.reuse_available( ) )
				m_remote_view.set_max_fps( m_enabled ? fix( fix_t::partner_view_fps ) : 0 );
			if( on( fix_t::multicore ) )
				enforce_render_threading( );
			else
				restore_render_threading( );
			m_flashlight_buckets.set_enabled( on( fix_t::flashlight_flush ) );
			m_xinput_throttle.set_enabled( on( fix_t::xinput_throttle ) );
			m_occlusion_skip.set_enabled( on( fix_t::occlusion_skip ) );
			m_vgui_screen_shadow_skip.set_enabled( on( fix_t::vgui_shadow_skip ) );
			m_shadow_depth_reuse.set_enabled( on( fix_t::shadow_depth_reuse ) );
			m_shadow_depth_reuse.set_max_fps( on( fix_t::shadow_depth_reuse ) ? fix( fix_t::shadow_depth_fps ) : 0 );
			m_vgui_screen_cache.set_max_fps( m_enabled ? fix( fix_t::vgui_screen_fps ) : 0 );
			m_queued_frames.set_depth( m_enabled ? fix( fix_t::frame_queue_depth ) : 1 );
			m_shadow_cache.set_stable_depth_order( on( fix_t::shadow_depth_reuse ) && m_shadow_depth_reuse.installed( ) );
			m_overlay.set_fixes_enabled( m_enabled );
		}

		static void __cdecl command_thunk( const sdk::CCommand& command ) {
			// Source invokes a C callback, so forward through the live instance.
			if( m_instance )
				m_instance->ClientCommand( nullptr, command );
		}

		static Plugin* m_instance;

		verification_result_t        m_verification;
		portal2_modules_t            m_modules;
		Portal2Interfaces            m_interfaces;
		nvidia_profile_result_t      m_nvidia_profile;
		FrameMetrics                 m_metrics;
		FrameLimiter                 m_frame_limiter;
		ProjectedShadowCache         m_shadow_cache;
		FlashlightBuckets            m_flashlight_buckets;
		XInputThrottle               m_xinput_throttle;
		OcclusionSkip                m_occlusion_skip;
		VguiScreenShadowSkip         m_vgui_screen_shadow_skip;
		VguiScreenCache              m_vgui_screen_cache;
		QueuedFrames                 m_queued_frames;
		ShadowDepthReuse             m_shadow_depth_reuse;
		DisplayAdjustments           m_display_adjustments;
		RemoteView                   m_remote_view;
		AutoUpdate                   m_auto_update;
		DemoProvenance               m_demo_provenance;
		FlatTextures                 m_flat_textures;
		ProfilerOverlay              m_overlay;
		ConsoleCommandRegistry       m_commands;
		IDirect3DDevice9*            m_device{ };
		bool                         m_enabled{ };
		bool                         m_last_focused{ };
		std::array< int, fix_count > m_fixes{ default_fixes( ) };
		mutable con_var_t*           m_queue_mode{ };
		mutable int                  m_last_queue_mode{ -1 };
		mutable int                  m_original_queue_mode{ -1 };
		mutable bool                 m_queue_mode_forced{ };
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
