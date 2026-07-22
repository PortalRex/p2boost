#include "build_fingerprint.h"
#include "console.h"
#include "console_commands.h"
#include "frame_pacer.h"
#include "metrics.h"
#include "portal_trace.h"
#include "renderer_trace.h"
#include "sdk.h"
#include "shadow_trace.h"

#include <Windows.h>
#include <cstring>
#include <string>

namespace p2boost {

	class Plugin final : public sdk::IServerPluginCallbacks {
	public:
		bool Load( sdk::CreateInterfaceFn interface_factory, sdk::CreateInterfaceFn ) override {
			// publish the singleton before command thunks can be invoked.
			m_instance = this;

			// commands are the plugin's only public control surface.
			if( !m_commands.connect( interface_factory ) ||
				!m_commands.add( "p2boost_status", "Print p2boost runtime state.", &CommandThunk ) ||
				!m_commands.add( "p2boost_enable", "Enable or disable verified fixes.", &CommandThunk ) ||
				!m_commands.add( "p2boost_trace", "Enable or disable diagnostic frame tracing.", &CommandThunk ) ||
				!m_commands.add( "p2boost_dump_metrics", "Write diagnostic timings to CSV.", &CommandThunk ) ) {
				console_print( "[p2boost] failed to register console commands\n" );
				m_instance = nullptr;
				return false;
			}
			m_verification = verify_runtime( );

			// each feature remains unavailable when its exact signature fails.
			if( m_verification.m_supported && m_verification.m_native_d3d9 && !m_renderer_trace.install( &m_metrics ) )
				m_verification.m_errors.emplace_back( "renderer trace callsite signature mismatch" );
			if( m_verification.m_supported && m_verification.m_native_d3d9 &&
				!m_renderer_trace.focus_sync_available( ) )
				m_verification.m_errors.emplace_back( "material-system focus synchronization signature mismatch" );
			if( m_verification.m_supported && m_verification.m_native_d3d9 &&
				!m_renderer_trace.static_lighting_fix_available( ) )
				m_verification.m_errors.emplace_back( "static-lighting restore callsite signature mismatch" );
			if( m_verification.m_supported && m_verification.m_native_d3d9 && !m_portal_trace.install( &m_metrics ) )
				m_verification.m_errors.emplace_back( "portal trace callsite signature mismatch" );
			if( m_verification.m_supported && m_verification.m_native_d3d9 && !m_shadow_trace.install( &m_metrics ) )
				m_verification.m_errors.emplace_back( "projected-shadow trace vtable mismatch" );
			if( m_verification.m_supported && m_verification.m_native_d3d9 && !m_frame_pacer.install( ) ) {
				m_verification.m_errors.emplace_back( "frame-pacer callsite signature mismatch" );
			}
			// one state transition updates every reversible optimization.
			m_enabled = can_enable( );
			apply_enabled_state( );
			console_print( "[p2boost] loaded v%s; frame_pacer=%s\n",
				P2BOOST_VERSION,
				m_frame_pacer.installed( ) ? ( m_enabled ? "enabled" : "disabled" ) : "unavailable" );
			for( const auto& error : m_verification.m_errors )
				console_print( "[p2boost] %s\n", error.c_str( ) );
			return true;
		}
		void Unload( ) override {
			// stop external entry points before removing their backing hooks.
			m_commands.disconnect( );
			m_shadow_trace.remove( );
			m_portal_trace.remove( );
			m_renderer_trace.remove( );
			m_frame_pacer.remove( );
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
			return "p2boost native-D3D9 performance patch v" P2BOOST_VERSION;
		}
		void LevelInit( const char* ) override {
			m_shadow_trace.invalidate( );
		}
		void ServerActivate( sdk::edict_t*, int, int client_max ) override {
			m_metrics.set_coop( client_max > 1 );
		}
		void GameFrame( bool simulating ) override {
			// focus transitions invalidate cached shadow inputs owned by D3D9.
			DWORD foreground_process = 0;
			GetWindowThreadProcessId( GetForegroundWindow( ), &foreground_process );
			const bool focused = foreground_process == GetCurrentProcessId( );
			if( focused != m_last_focused ) {
				m_shadow_trace.invalidate( );
				m_last_focused = focused;
			}
			m_metrics.frame( simulating );
		}
		void LevelShutdown( ) override {
			m_metrics.set_coop( false );
			m_shadow_trace.invalidate( );
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
			// dispatch only commands registered by this plugin instance.
			const char* command = args.Arg( 0 );
			if( _stricmp( command, "p2boost_status" ) == 0 ) {
				const bool sar_loaded = GetModuleHandleW( L"sar.dll" ) != nullptr;
				console_print( "[p2boost] build=23973718 supported=%d native_d3d9=%d enabled=%d "
							   "pacer_installed=%d tracing=%d sar=%d samples=%zu focus_sync=%d lighting_fix=%d "
							   "lighting_skips=%u shadow_cache=%d shadow_skips=%u root=%s\n",
					m_verification.m_supported,
					m_verification.m_native_d3d9,
					m_enabled,
					m_frame_pacer.installed( ),
					m_metrics.tracing( ),
					sar_loaded,
					m_metrics.count( ),
					m_renderer_trace.focus_sync_available( ),
					m_renderer_trace.static_lighting_fix_available( ),
					m_renderer_trace.static_lighting_skips( ),
					m_shadow_trace.installed( ),
					m_shadow_trace.skips( ),
					m_verification.m_game_root.c_str( ) );
				return sdk::Stop;
			}
			if( _stricmp( command, "p2boost_enable" ) == 0 ) {
				// invalid values report an error without changing prior state.
				if( args.ArgC( ) != 2 )
					console_print( "usage: p2boost_enable 0|1\n" );
				else if( strcmp( args.Arg( 1 ), "0" ) == 0 )
					m_enabled = false;
				else if( strcmp( args.Arg( 1 ), "1" ) == 0 && can_enable( ) )
					m_enabled = true;
				else
					console_print( "[p2boost] cannot enable: unsupported build/backend or invalid value\n" );
				apply_enabled_state( );
				return sdk::Stop;
			}
			if( _stricmp( command, "p2boost_trace" ) == 0 ) {
				if( args.ArgC( ) == 2 && ( strcmp( args.Arg( 1 ), "0" ) == 0 || strcmp( args.Arg( 1 ), "1" ) == 0 ) )
					m_metrics.set_tracing( strcmp( args.Arg( 1 ), "1" ) == 0 );
				else
					console_print( "usage: p2boost_trace 0|1\n" );
				return sdk::Stop;
			}
			if( _stricmp( command, "p2boost_dump_metrics" ) == 0 ) {
				// keep captures beside the game for predictable script discovery.
				const std::string path = m_verification.m_game_root + "\\p2boost_metrics.csv";
				console_print( "[p2boost] metrics %s: %s\n",
					m_metrics.dump( path ) ? "written" : "write failed",
					path.c_str( ) );
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
		// one predicate keeps every enable path identical.
		bool can_enable( ) const noexcept {
			return m_verification.m_supported && m_verification.m_native_d3d9 && m_frame_pacer.installed( ) &&
				   m_renderer_trace.focus_sync_available( ) && m_renderer_trace.static_lighting_fix_available( );
		}

		// apply the reversible fixes without touching user cvars.
		void apply_enabled_state( ) noexcept {
			m_frame_pacer.set_enabled( m_enabled );
			m_renderer_trace.set_fix_enabled( m_enabled );
			m_shadow_trace.set_enabled( m_enabled );
		}

		// all commands share the engine's one-argument callback ABI.
		static void __cdecl CommandThunk( const sdk::CCommand& command ) {
			if( m_instance )
				m_instance->ClientCommand( nullptr, command );
		}
		static Plugin*		   m_instance;
		VerificationResult	   m_verification;
		Metrics				   m_metrics;
		FramePacer			   m_frame_pacer;
		PortalTrace			   m_portal_trace;
		RendererTrace		   m_renderer_trace;
		ShadowTrace			   m_shadow_trace;
		ConsoleCommandRegistry m_commands;
		bool				   m_enabled	  = false;
		bool				   m_last_focused = false;
	};

	Plugin* Plugin::m_instance = nullptr;

	Plugin g_plugin;
}

extern "C" __declspec( dllexport ) void* CreateInterface( const char* name, int* return_code ) {
	// Source locates plugins through this versioned factory export.
	if( name && strcmp( name, "ISERVERPLUGINCALLBACKS003" ) == 0 ) {
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
