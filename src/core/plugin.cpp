#include "console.h"
#include "console_commands.h"
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

		std::wstring widen( const std::string& input ) {
			if( input.empty( ) )
				return { };

			const int size = MultiByteToWideChar( CP_UTF8, 0, input.data( ), static_cast< int >( input.size( ) ), nullptr, 0 );
			std::wstring output( static_cast< size_t >( size ), L'\0' );
			MultiByteToWideChar( CP_UTF8, 0, input.data( ), static_cast< int >( input.size( ) ), output.data( ), size );
			return output;
		}

		bool process_is_foreground( ) noexcept {
			DWORD foreground_process{ };
			GetWindowThreadProcessId( GetForegroundWindow( ), &foreground_process );
			return foreground_process == GetCurrentProcessId( );
		}

	}

	class Plugin final : public sdk::IServerPluginCallbacks {
	public:
		bool Load( sdk::CreateInterfaceFn interface_factory, sdk::CreateInterfaceFn ) override {
			m_instance = this;

			if( !m_commands.connect( interface_factory ) ||
				!m_commands.add( "p2boost_status", "Print p2boost runtime state.", &CommandThunk ) ||
				!m_commands.add( "p2boost_enable", "Enable or disable verified fixes.", &CommandThunk ) ||
				!m_commands.add( "p2boost_trace", "Enable or disable frame tracing.", &CommandThunk ) ||
				!m_commands.add( "p2boost_overlay", "Enable or disable the frame profiler.", &CommandThunk ) ||
				!m_commands.add( "p2boost_dump_metrics", "Write frame metrics to CSV.", &CommandThunk ) ||
				!m_commands.add( "p2boost_driver_profile", "Configure the NVIDIA DXVK presentation profile.", &CommandThunk ) ) {
				console_print( "[p2boost] failed to register console commands\n" );
				m_instance = nullptr;
				return false;
			}

			m_verification = verify_runtime( );
			m_overlay.connect( interface_factory );

			if( m_verification.m_supported && !m_frame_limiter.install( &m_metrics ) )
				m_verification.m_errors.emplace_back( "frame-limiter signature mismatch" );
			if( m_verification.m_supported && !m_shadow_cache.install( &m_metrics ) )
				m_verification.m_errors.emplace_back( "projected-shadow signature mismatch" );

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
			m_commands.disconnect( );
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
			m_shadow_cache.invalidate( );
		}

		void ServerActivate( sdk::edict_t*, int, int client_max ) override {
			m_metrics.set_coop( client_max > 1 );
		}

		void GameFrame( bool simulating ) override {
			m_metrics.set_simulating( simulating );

			const bool focused = process_is_foreground( );
			m_metrics.set_focused( focused );
			if( focused != m_last_focused ) {
				// DXVK can still recreate swapchain-backed resources after a display
				// transition. Never carry a projected-shadow cache across that boundary.
				m_shadow_cache.invalidate( );
				m_last_focused = focused;
			}

			m_overlay.draw( m_metrics, m_enabled, GetModuleHandleW( L"sar.dll" ) != nullptr );
		}

		void LevelShutdown( ) override {
			m_metrics.set_simulating( false );
			m_metrics.set_coop( false );
			m_shadow_cache.invalidate( );
		}

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
			const char* command = args.Arg( 0 );
			if( _stricmp( command, "p2boost_status" ) == 0 ) {
				console_print( "[p2boost] build=23973718 supported=%d dxvk_vulkan=%d enabled=%d "
							   "limiter=%d shadow_cache=%d shadow_hits=%u tracing=%d overlay=%d overlay_available=%d "
							   "sar=%d samples=%zu root=%s\n",
					m_verification.m_supported,
					m_verification.m_dxvk_vulkan,
					m_enabled,
					m_frame_limiter.installed( ),
					m_shadow_cache.installed( ),
					m_shadow_cache.hit_count( ),
					m_metrics.tracing( ),
					m_overlay.enabled( ),
					m_overlay.available( ),
					GetModuleHandleW( L"sar.dll" ) != nullptr,
					m_metrics.count( ),
					m_verification.m_game_root.c_str( ) );
				return sdk::Stop;
			}

			if( _stricmp( command, "p2boost_enable" ) == 0 ) {
				if( args.ArgC( ) != 2 )
					console_print( "usage: p2boost_enable 0|1\n" );
				else if( std::strcmp( args.Arg( 1 ), "0" ) == 0 )
					m_enabled = false;
				else if( std::strcmp( args.Arg( 1 ), "1" ) == 0 && can_enable( ) )
					m_enabled = true;
				else
					console_print( "[p2boost] cannot enable: unsupported build/backend or invalid value\n" );

				apply_enabled_state( );
				return sdk::Stop;
			}

			if( _stricmp( command, "p2boost_trace" ) == 0 ) {
				if( args.ArgC( ) == 2 && ( std::strcmp( args.Arg( 1 ), "0" ) == 0 || std::strcmp( args.Arg( 1 ), "1" ) == 0 ) )
					m_metrics.set_tracing( std::strcmp( args.Arg( 1 ), "1" ) == 0 );
				else
					console_print( "usage: p2boost_trace 0|1\n" );
				return sdk::Stop;
			}

			if( _stricmp( command, "p2boost_overlay" ) == 0 ) {
				if( args.ArgC( ) == 2 && ( std::strcmp( args.Arg( 1 ), "0" ) == 0 || std::strcmp( args.Arg( 1 ), "1" ) == 0 ) )
					m_overlay.set_enabled( std::strcmp( args.Arg( 1 ), "1" ) == 0, m_metrics );
				else
					console_print( "usage: p2boost_overlay 0|1\n" );

				if( !m_overlay.available( ) )
					console_print( "[p2boost] profiler unavailable: VDebugOverlay004 is missing\n" );
				return sdk::Stop;
			}

			if( _stricmp( command, "p2boost_dump_metrics" ) == 0 ) {
				const std::string path = m_verification.m_game_root + "\\p2boost_metrics.csv";
				console_print( "[p2boost] metrics %s: %s\n", m_metrics.dump( path ) ? "written" : "write failed", path.c_str( ) );
				return sdk::Stop;
			}

			if( _stricmp( command, "p2boost_driver_profile" ) == 0 ) {
				if( args.ArgC( ) != 2 || ( _stricmp( args.Arg( 1 ), "status" ) != 0 && _stricmp( args.Arg( 1 ), "apply" ) != 0 &&
											 _stricmp( args.Arg( 1 ), "restore" ) != 0 ) ) {
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
		bool can_enable( ) const noexcept {
			return m_verification.m_supported && m_verification.m_dxvk_vulkan && m_frame_limiter.installed( ) &&
				   m_shadow_cache.installed( );
		}

		void apply_enabled_state( ) noexcept {
			m_frame_limiter.set_enabled( m_enabled );
			m_shadow_cache.set_enabled( m_enabled );
		}

		bool run_driver_profile_tool( const char* action ) const {
			const std::wstring directory = widen( m_verification.m_game_root + "\\portal2" );
			const std::wstring executable = directory + L"\\p2boost_driver.exe";
			const std::wstring parameters = widen( action );
			const bool changes_profile = _stricmp( action, "status" ) != 0;

			SHELLEXECUTEINFOW launch{ };
			launch.cbSize = sizeof( launch );
			launch.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
			launch.lpVerb = changes_profile ? L"runas" : L"open";
			launch.lpFile = executable.c_str( );
			launch.lpParameters = parameters.c_str( );
			launch.lpDirectory = directory.c_str( );
			launch.nShow = SW_HIDE;
			if( !ShellExecuteExW( &launch ) || !launch.hProcess )
				return false;

			const DWORD wait = WaitForSingleObject( launch.hProcess, 15000 );
			DWORD exit_code = 1;
			if( wait == WAIT_OBJECT_0 )
				GetExitCodeProcess( launch.hProcess, &exit_code );
			CloseHandle( launch.hProcess );
			return wait == WAIT_OBJECT_0 && exit_code == 0;
		}

		static void __cdecl CommandThunk( const sdk::CCommand& command ) {
			if( m_instance )
				m_instance->ClientCommand( nullptr, command );
		}

		static Plugin* m_instance;

		VerificationResult m_verification;
		FrameMetrics m_metrics;
		FrameLimiter m_frame_limiter;
		ProjectedShadowCache m_shadow_cache;
		ProfilerOverlay m_overlay;
		ConsoleCommandRegistry m_commands;
		bool m_enabled{ };
		bool m_last_focused{ };
	};

	Plugin* Plugin::m_instance{ };
	Plugin g_plugin;

}

extern "C" __declspec( dllexport ) void* CreateInterface( const char* name, int* return_code ) {
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
