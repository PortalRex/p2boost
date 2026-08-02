#include "console.h"
#include "console_commands.h"
#include "flat_textures.h"
#include "frame_limiter.h"
#include "frame_metrics.h"
#include "profiler_overlay.h"
#include "projected_shadow_cache.h"
#include "runtime_verification.h"
#include "sdk.h"

#include <Windows.h>
#include <shellapi.h>

#include <cstring>
#include <string>

namespace p2boost {
	namespace {

		// convert paths for the Unicode process launcher.
		std::wstring widen( const std::string& input ) {
			if( input.empty( ) )
				return { };

			const int	 size = MultiByteToWideChar( CP_UTF8, 0, input.data( ), static_cast< int >( input.size( ) ), nullptr, 0 );
			std::wstring output( static_cast< size_t >( size ), L'\0' );
			MultiByteToWideChar( CP_UTF8, 0, input.data( ), static_cast< int >( input.size( ) ), output.data( ), size );
			return output;
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

			// register the complete public command surface as one transaction.
			if( !m_commands.connect( interface_factory ) ||
				!m_commands.add( "p2boost_status", "Print p2boost runtime state.", &CommandThunk ) ||
				!m_commands.add( "p2boost_enable", "Enable or disable verified fixes.", &CommandThunk ) ||
				!m_commands.add( "p2boost_flat_textures", "Enable or disable alpha-safe flat textures.", &CommandThunk ) ||
				!m_commands.add( "p2boost_trace", "Enable or disable frame tracing.", &CommandThunk ) ||
				!m_commands.add( "p2boost_overlay", "Enable or disable the frame profiler.", &CommandThunk ) ||
				!m_commands.add( "p2boost_dump_metrics", "Write frame metrics to CSV.", &CommandThunk ) ||
				!m_commands.add( "p2boost_driver_profile", "Configure the NVIDIA DXVK presentation profile.", &CommandThunk ) ) {
				console_print( "[p2boost] failed to register console commands\n" );
				m_instance = nullptr;
				return false;
			}

			// fingerprint the exact binaries before resolving any private code.
			m_verification = verify_runtime( );
			m_overlay.connect( interface_factory );

			// each feature validates its own signatures and remains disabled on failure.
			if( m_verification.m_supported && !m_frame_limiter.install( &m_metrics ) )
				m_verification.m_errors.emplace_back( "frame-limiter signature mismatch" );
			if( m_verification.m_supported && !m_shadow_cache.install( &m_metrics ) )
				m_verification.m_errors.emplace_back( "projected-shadow signature mismatch" );
			if( m_verification.m_supported && !m_flat_textures.install( ) )
				m_verification.m_errors.emplace_back( "optional flat-texture hooks unavailable" );

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
			return true;
		}

		void Unload( ) override {
			// unregister commands first so teardown cannot race a new callback.
			m_commands.disconnect( );
			m_flat_textures.remove( );
			m_overlay.set_enabled( false, m_metrics );
			m_overlay.disconnect( );
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
			m_flat_textures.set_rendering_active( false );
			m_shadow_cache.invalidate( );
		}

		void ServerActivate( sdk::edict_t*, int, int client_max ) override {
			m_metrics.set_coop( client_max > 1 );
		}

		void GameFrame( bool simulating ) override {
			// mirror only state needed by diagnostics and texture upload filtering.
			m_metrics.set_simulating( simulating );
			m_flat_textures.set_rendering_active( simulating );

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
			m_flat_textures.set_rendering_active( false );
			m_flat_textures.release_level_textures( );
			m_shadow_cache.invalidate( );
		}

		// remaining callbacks intentionally preserve vanilla behavior.
		void ClientFullyConnect( sdk::edict_t* ) override {
		}

		void ClientActive( sdk::edict_t* ) override {
		}

		void ClientDisconnect( sdk::edict_t* ) override {
		}

		void ClientPutInServer( sdk::edict_t*, const char* ) override {
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
			if( _stricmp( command, "p2boost_driver_profile" ) == 0 )
				return DriverProfileCommand( args );

			return sdk::Continue;
		}

		sdk::PluginResult NetworkIDValidated( const char*, const char* ) override {
			return sdk::Continue;
		}

		void OnQueryCvarValueFinished( int, sdk::edict_t*, sdk::QueryStatus, const char*, const char* ) override {
		}

		void OnEdictAllocated( sdk::edict_t* ) override {
		}

		void OnEdictFreed( const sdk::edict_t* ) override {
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
			console_print( "[p2boost] build=23973718 supported=%d dxvk_vulkan=%d enabled=%d "
						   "limiter=%d shadow_cache=%d shadow_hits=%u tracing=%d overlay=%d overlay_available=%d "
						   "flat_available=%d flat=%d flat_textures=%zu flat_ready=%zu flat_binds=%llu "
						   "flat_uploads=%llu/%llu/%llu sar=%d samples=%zu root=%s\n",
						   m_verification.m_supported,
						   m_verification.m_dxvk_vulkan,
						   m_enabled,
						   m_frame_limiter.installed( ),
						   m_shadow_cache.installed( ),
						   m_shadow_cache.hit_count( ),
						   m_metrics.tracing( ),
						   m_overlay.enabled( ),
						   m_overlay.available( ),
						   m_flat_textures.available( ),
						   m_flat_textures.enabled( ),
						   m_flat_textures.texture_count( ),
						   m_flat_textures.ready_texture_count( ),
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
				console_print( "[p2boost] flat textures %s; RGB is solid and source alpha is preserved\n", enabled ? "enabled" : "disabled" );

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

		// delegate reversible NVIDIA profile changes to the signed helper process.
		sdk::PluginResult DriverProfileCommand( const sdk::CCommand& args ) const {
			if( args.ArgC( ) != 2 || ( _stricmp( args.Arg( 1 ), "status" ) != 0 && _stricmp( args.Arg( 1 ), "apply" ) != 0 && _stricmp( args.Arg( 1 ), "restore" ) != 0 ) ) {
				console_print( "usage: p2boost_driver_profile status|apply|restore\n" );
				return sdk::Stop;
			}

			const bool success = run_driver_profile_tool( args.Arg( 1 ) );
			console_print( "[p2boost] NVIDIA profile %s %s%s\n",
						   args.Arg( 1 ),
						   success ? "succeeded" : "failed",
						   success && _stricmp( args.Arg( 1 ), "apply" ) == 0 ? "; restart Portal 2 before testing alt-tab" : "" );

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

		bool run_driver_profile_tool( const char* action ) const {
			// keep NVAPI and elevation outside the game process.
			const std::wstring directory	   = widen( m_verification.m_game_root + "\\portal2" );
			const std::wstring executable	   = directory + L"\\p2boost_driver.exe";
			const std::wstring parameters	   = widen( action );
			const bool		   changes_profile = _stricmp( action, "status" ) != 0;

			SHELLEXECUTEINFOW launch{ };
			launch.cbSize		= sizeof( launch );
			launch.fMask		= SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
			launch.lpVerb		= changes_profile ? L"runas" : L"open";
			launch.lpFile		= executable.c_str( );
			launch.lpParameters = parameters.c_str( );
			launch.lpDirectory	= directory.c_str( );
			launch.nShow		= SW_HIDE;
			if( !ShellExecuteExW( &launch ) || !launch.hProcess )
				return false;

			// bound the wait so a broken helper cannot hang the engine indefinitely.
			const DWORD wait	  = WaitForSingleObject( launch.hProcess, 15000 );
			DWORD		exit_code = 1;
			if( wait == WAIT_OBJECT_0 )
				GetExitCodeProcess( launch.hProcess, &exit_code );
			CloseHandle( launch.hProcess );
			return wait == WAIT_OBJECT_0 && exit_code == 0;
		}

		static void __cdecl CommandThunk( const sdk::CCommand& command ) {
			// Source invokes a C callback, so forward through the live instance.
			if( m_instance )
				m_instance->ClientCommand( nullptr, command );
		}

		static Plugin* m_instance;

		VerificationResult	   m_verification;
		FrameMetrics		   m_metrics;
		FrameLimiter		   m_frame_limiter;
		ProjectedShadowCache   m_shadow_cache;
		FlatTextures		   m_flat_textures;
		ProfilerOverlay		   m_overlay;
		ConsoleCommandRegistry m_commands;
		bool				   m_enabled{ };
		bool				   m_last_focused{ };
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
