#pragma once

#include <cstddef>
#include <cstdint>

namespace p2boost::sdk {

	// minimal public Source ABI needed by an IServerPluginCallbacks003 plugin.
	using CreateInterfaceFn = void* (*)( const char*, int* );
	struct edict_t;

	enum PluginResult {
		Continue = 0,
		Override,
		Stop,
	};

	enum QueryStatus {
		ValueIntact = 0,
		CvarNotFound,
		NotACvar,
		CvarProtected,
	};

	class CCommand {
	public:
		int ArgC( ) const noexcept {
			return m_argc;
		}
		const char* Arg( int index ) const noexcept {
			return index >= 0 && index < m_argc && m_argv[ index ] ? m_argv[ index ] : "";
		}

	private:
		// layout mirrors the engine-owned CCommand passed to callbacks.
		int			m_argc;
		int			m_argv0_size;
		char		m_args_buffer[ 512 ];
		char		m_argv_buffer[ 512 ];
		const char* m_argv[ 64 ];
	};

	class IServerPluginCallbacks {
	public:
		// declaration order is ABI-significant; do not reorder these methods.
		virtual bool		 Load( CreateInterfaceFn interface_factory, CreateInterfaceFn game_server_factory ) = 0;
		virtual void		 Unload( )																			= 0;
		virtual void		 Pause( )																			= 0;
		virtual void		 UnPause( )																			= 0;
		virtual const char*	 GetPluginDescription( )															= 0;
		virtual void		 LevelInit( const char* map_name )													= 0;
		virtual void		 ServerActivate( edict_t* edict_list, int edict_count, int client_max )				= 0;
		virtual void		 GameFrame( bool simulating )														= 0;
		virtual void		 LevelShutdown( )																	= 0;
		virtual void		 ClientFullyConnect( edict_t* edict )												= 0;
		virtual void		 ClientActive( edict_t* entity )													= 0;
		virtual void		 ClientDisconnect( edict_t* entity )												= 0;
		virtual void		 ClientPutInServer( edict_t* entity, const char* player_name )						= 0;
		virtual void		 SetCommandClient( int index )														= 0;
		virtual void		 ClientSettingsChanged( edict_t* edict )											= 0;
		virtual PluginResult ClientConnect(
			bool* allow_connect, edict_t* entity, const char* name, const char* address, char* reject, int max_reject_length ) = 0;
		virtual PluginResult ClientCommand( edict_t* entity, const CCommand& args )											   = 0;
		virtual PluginResult NetworkIDValidated( const char* user_name, const char* network_id )							   = 0;
		virtual void		 OnQueryCvarValueFinished(
					int cookie, edict_t* player, QueryStatus status, const char* cvar_name, const char* cvar_value ) = 0;
		virtual void OnEdictAllocated( edict_t* edict )																 = 0;
		virtual void OnEdictFreed( const edict_t* edict )															 = 0;
	};

}
