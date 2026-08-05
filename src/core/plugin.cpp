#include "console.h"
#include "console_commands.h"
#include "demo_provenance.h"
#include "display_adjustments.h"
#include "flat_textures.h"
#include "frame_limiter.h"
#include "frame_metrics.h"
#include "nvidia_profile.h"
#include "profiler_overlay.h"
#include "projected_shadow_cache.h"
#include "runtime_verification.h"
#include "sdk.h"

#include <Windows.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace p2boost {
	namespace {

		struct window_search_t {
			DWORD m_process{ };
			HWND  m_window{ };
		};

		// remember the first visible top-level window owned by Portal 2.
		BOOL CALLBACK find_process_window( HWND window, LPARAM parameter ) {
			auto* search = reinterpret_cast< window_search_t* >( parameter );
			DWORD process{ };
			GetWindowThreadProcessId( window, &process );
			if( process != search->m_process || !IsWindowVisible( window ) || GetWindow( window, GW_OWNER ) )
				return TRUE;

			search->m_window = window;
			return FALSE;
		}

		// resolve the window at the time it is needed instead of caching it.
		HWND process_window( ) noexcept {
			window_search_t search{ GetCurrentProcessId( ) };
			EnumWindows( &find_process_window, reinterpret_cast< LPARAM >( &search ) );
			return search.m_window;
		}

		// explain the one-time change, then terminate before Vulkan starts.
		void restart_after_profile_update( ) noexcept {
			const HWND window = process_window( );
			MessageBoxW( window,
						 L"p2boost applied the NVIDIA settings required for reliable Vulkan alt-tabbing.\n\n"
						 L"Portal 2 must restart and will now close.",
						 L"p2boost - restart required",
						 MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND );

			// Source ignores WM_CLOSE while its startup command buffer is active.
			// no session exists yet, so an immediate exit cannot lose game state.
			ExitProcess( 0 );
		}

		// compare the active window owner without caching a stale HWND.
		bool process_is_foreground( ) noexcept {
			DWORD foreground_process{ };
			GetWindowThreadProcessId( GetForegroundWindow( ), &foreground_process );
			return foreground_process == GetCurrentProcessId( );
		}

	}

	class Plugin final : public sdk::IServerPluginCallbacks {
	public:
		bool Load( sdk::CreateInterfaceFn interface_factory, sdk::CreateInterfaceFn ) override {
			// publish the instance before engine-owned command callbacks can fire.
			m_instance = this;

			// configure the NVIDIA application profile before installing engine hooks.
			m_verification = verify_runtime( );
			if( m_verification.m_dxvk_vulkan ) {
				m_nvidia_profile = configure_nvidia_profile( );
				if( m_nvidia_profile.m_state == nvidia_profile_state_t::failed )
					m_nvidia_profile = configure_nvidia_profile_elevated( );

				if( m_nvidia_profile.m_state == nvidia_profile_state_t::applied ) {
					m_instance = nullptr;
					restart_after_profile_update( );
					return false;
				}
			}

			// register the complete public command surface as one transaction.
			if( !m_commands.connect( interface_factory ) ||
				!m_commands.add( "p2boost_status", "Print p2boost runtime state.", &CommandThunk ) ||
				!m_commands.add( "p2boost_enable", "Enable or disable verified fixes.", &CommandThunk ) ||
				!m_commands.add( "p2boost_flat_textures", "Enable or disable alpha-safe flat textures.", &CommandThunk ) ||
				!m_commands.add( "p2boost_trace", "Enable or disable frame tracing.", &CommandThunk ) ||
				!m_commands.add( "p2boost_overlay", "Enable or disable the frame profiler.", &CommandThunk ) ||
				!m_commands.add( "p2boost_dump_metrics", "Write frame metrics to CSV.", &CommandThunk ) ) {
				console_print( "[p2boost] failed to register console commands\n" );
				m_instance = nullptr;
				return false;
			}

			// Portal 2 shipped these public SDK rendering controls with FCVAR_CHEAT.
			// removing only that flag makes the original engine path usable while
			// preserving archive, client, bounds, and replication behavior.
			const bool viewport_cvars_ready = !m_verification.m_supported ||
											  ( m_commands.remove_flags( "mat_viewportscale", cvar_flag_cheat ) &&
												m_commands.remove_flags( "mat_viewportupscale", cvar_flag_cheat ) );
			if( !viewport_cvars_ready )
				m_verification.m_errors.emplace_back( "viewport cvars unavailable" );

			m_overlay.connect( interface_factory );
			m_engine_client = interface_factory ? static_cast< sdk::EngineClient* >( interface_factory( "VEngineClient015", nullptr ) ) : nullptr;
			if( m_verification.m_supported && !m_demo_provenance.connect( m_engine_client ) )
				m_verification.m_errors.emplace_back( "demo command-buffer xref unavailable" );

			// each feature validates its own signatures and remains disabled on failure.
			if( m_verification.m_supported && !m_frame_limiter.install( &m_metrics ) )
				m_verification.m_errors.emplace_back( "frame-limiter signature mismatch" );
			if( m_verification.m_supported && !m_shadow_cache.install( &m_metrics ) )
				m_verification.m_errors.emplace_back( "projected-shadow signature mismatch" );
			if( m_verification.m_supported && !m_flat_textures.install( ) )
				m_verification.m_errors.emplace_back( "optional flat-texture hooks unavailable" );
			if( m_verification.m_supported && !m_display_adjustments.install( m_commands ) )
				m_verification.m_errors.emplace_back( "display-adjustment hooks unavailable" );

			// enable performance fixes only when the complete supported set is ready.
			m_enabled = can_enable( );
			apply_enabled_state( );
			m_last_focused = process_is_foreground( );
			m_metrics.set_focused( m_last_focused );

			console_print( "[p2boost] loaded v%s; backend=%s fixes=%s\n",
						   P2BOOST_VERSION,
						   m_verification.m_dxvk_vulkan ? "DXVK/Vulkan" : "unsupported",
						   m_enabled ? "enabled" : "disabled" );
			for( const auto& error : m_verification.m_errors )
				console_print( "[p2boost] %s\n", error.c_str( ) );
			if( m_nvidia_profile.m_state == nvidia_profile_state_t::unavailable || m_nvidia_profile.m_state == nvidia_profile_state_t::failed )
				console_print( "[p2boost] NVIDIA profile setup %s at %s (%d); no driver settings were changed\n",
							   nvidia_profile_state_name( m_nvidia_profile.m_state ),
							   m_nvidia_profile.m_operation,
							   m_nvidia_profile.m_status );
			return true;
		}

		void Unload( ) override {
			// unregister commands first so teardown cannot race a new callback.
			m_commands.disconnect( );
			m_display_adjustments.remove( );
			m_flat_textures.remove( );
			m_overlay.set_enabled( false, m_metrics );
			m_overlay.disconnect( );
			m_demo_provenance.disconnect( );
			m_engine_client = nullptr;
			m_shadow_cache.remove( );
			m_frame_limiter.remove( );
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

		void LevelInit( const char* ) override {
			m_demo_provenance.reset( );
			m_shadow_cache.invalidate( );
		}

		void ServerActivate( sdk::edict_t*, int, int client_max ) override {
			// The activated server owns a new receiver and entity lifetime.
			m_shadow_cache.invalidate( );
			m_metrics.set_coop( client_max > 1 );
		}

		void GameFrame( bool simulating ) override {
			// Projected-light receiver membership can change without changing the
			// private shadow bytes. Rebuild once after every simulation update, then
			// allow exact reuse across the additional high-refresh render frames.
			if( simulating )
				m_shadow_cache.invalidate( );

			// mirror only the state needed by diagnostics.
			m_metrics.set_simulating( simulating );
			m_demo_provenance.update( P2BOOST_VERSION, m_enabled, m_flat_textures.enabled( ) );

			const bool focused = process_is_foreground( );
			m_metrics.set_focused( focused );
			if( focused != m_last_focused ) {
				// DXVK can recreate swapchain-backed resources after a display
				// transition. never carry a shadow cache across that boundary.
				m_shadow_cache.invalidate( );
				m_last_focused = focused;
			}

			m_overlay.draw( m_metrics, m_enabled, GetModuleHandleW( L"sar.dll" ) != nullptr );
		}

		void LevelShutdown( ) override {
			// release map-owned state before Source begins destroying its resources.
			m_metrics.set_simulating( false );
			m_metrics.set_coop( false );
			m_flat_textures.release_level_textures( );
			m_shadow_cache.invalidate( );
		}

		void ClientFullyConnect( sdk::edict_t* ) override {
			// A completed sign-on can replace the entire client entity set.
			m_shadow_cache.invalidate( );
		}

		void ClientActive( sdk::edict_t* ) override {
			// The active transition makes newly received renderables visible.
			m_shadow_cache.invalidate( );
		}

		void ClientDisconnect( sdk::edict_t* ) override {
			// Never retain receivers owned by a departing co-op player.
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
				return StatusCommand( );
			if( _stricmp( command, "p2boost_enable" ) == 0 )
				return EnableCommand( args );
			if( _stricmp( command, "p2boost_flat_textures" ) == 0 )
				return FlatTexturesCommand( args );
			if( _stricmp( command, "p2boost_trace" ) == 0 )
				return TraceCommand( args );
			if( _stricmp( command, "p2boost_overlay" ) == 0 )
				return OverlayCommand( args );
			if( _stricmp( command, "p2boost_dump_metrics" ) == 0 )
				return DumpMetricsCommand( );

			return sdk::Continue;
		}

		sdk::PluginResult NetworkIDValidated( const char*, const char* ) override {
			return sdk::Continue;
		}

		void OnQueryCvarValueFinished( int, sdk::edict_t*, sdk::QueryStatus, const char*, const char* ) override {
		}

		void OnEdictAllocated( sdk::edict_t* ) override {
			// Handles may be reused, so allocation begins a new cache lifetime.
			m_shadow_cache.invalidate( );
		}

		void OnEdictFreed( const sdk::edict_t* ) override {
			// Receiver lists must not retain a pointer to a destroyed renderable.
			m_shadow_cache.invalidate( );
		}

	private:
		// parse the exact 0/1 syntax shared by toggle commands.
		static bool ParseToggle( const sdk::CCommand& args, bool& enabled ) noexcept {
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

		// print one machine-readable snapshot for support and automated tests.
		sdk::PluginResult StatusCommand( ) const {
			console_print( "[p2boost] build=23973718 supported=%d dxvk_vulkan=%d nvidia_profile=%s nvidia_detail=%s:%d enabled=%d "
						   "limiter=%d shadow_cache=%d shadow_hits=%u shadow_culled=%u tracing=%d overlay=%d overlay_available=%d "
						   "display_available=%d gamma_passes=%llu viewport_passes=%llu demo_marker=%d "
						   "flat_available=%d flat=%d flat_textures=%zu flat_ready=%zu flat_normal=%d flat_binds=%llu "
						   "flat_uploads=%llu/%llu/%llu sar=%d samples=%zu root=%s\n",
						   m_verification.m_supported,
						   m_verification.m_dxvk_vulkan,
						   nvidia_profile_state_name( m_nvidia_profile.m_state ),
						   m_nvidia_profile.m_operation,
						   m_nvidia_profile.m_status,
						   m_enabled,
						   m_frame_limiter.installed( ),
						   m_shadow_cache.installed( ),
						   m_shadow_cache.hit_count( ),
						   m_shadow_cache.culled_count( ),
						   m_metrics.tracing( ),
						   m_overlay.enabled( ),
						   m_overlay.available( ),
						   m_display_adjustments.available( ),
						   static_cast< unsigned long long >( m_display_adjustments.gamma_passes( ) ),
						   static_cast< unsigned long long >( m_display_adjustments.viewport_passes( ) ),
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
						   GetModuleHandleW( L"sar.dll" ) != nullptr,
						   m_metrics.count( ),
						   m_verification.m_game_root.c_str( ) );

			return sdk::Stop;
		}

		// toggle only the benchmark-proven performance fixes.
		sdk::PluginResult EnableCommand( const sdk::CCommand& args ) {
			bool enabled{ };
			if( !ParseToggle( args, enabled ) ) {
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
		sdk::PluginResult FlatTexturesCommand( const sdk::CCommand& args ) {
			bool enabled{ };
			if( !ParseToggle( args, enabled ) ) {
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
		sdk::PluginResult TraceCommand( const sdk::CCommand& args ) {
			bool enabled{ };
			if( !ParseToggle( args, enabled ) )
				console_print( "usage: p2boost_trace 0|1\n" );
			else
				m_metrics.set_tracing( enabled );

			return sdk::Stop;
		}

		// show or hide the lightweight in-game profiler.
		sdk::PluginResult OverlayCommand( const sdk::CCommand& args ) {
			bool enabled{ };
			if( !ParseToggle( args, enabled ) )
				console_print( "usage: p2boost_overlay 0|1\n" );
			else
				m_overlay.set_enabled( enabled, m_metrics );

			if( !m_overlay.available( ) )
				console_print( "[p2boost] profiler unavailable: VDebugOverlay004 is missing\n" );

			return sdk::Stop;
		}

		// write the current ring buffer beside the game installation.
		sdk::PluginResult DumpMetricsCommand( ) const {
			const std::string path = m_verification.m_game_root + "\\p2boost_metrics.csv";
			console_print( "[p2boost] metrics %s: %s\n", m_metrics.dump( path ) ? "written" : "write failed", path.c_str( ) );
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
		}

		static void __cdecl CommandThunk( const sdk::CCommand& command ) {
			// Source invokes a C callback, so forward through the live instance.
			if( m_instance )
				m_instance->ClientCommand( nullptr, command );
		}

		static Plugin* m_instance;

		verification_result_t	m_verification;
		nvidia_profile_result_t m_nvidia_profile;
		FrameMetrics			m_metrics;
		FrameLimiter			m_frame_limiter;
		ProjectedShadowCache	m_shadow_cache;
		DisplayAdjustments		m_display_adjustments;
		DemoProvenance			m_demo_provenance;
		FlatTextures			m_flat_textures;
		ProfilerOverlay			m_overlay;
		ConsoleCommandRegistry	m_commands;
		sdk::EngineClient*		m_engine_client{ };
		bool					m_enabled{ };
		bool					m_last_focused{ };
	};

	Plugin* Plugin::m_instance{ };
	Plugin	g_plugin;

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
