#include "console_commands.h"

namespace p2boost {
	con_command_base_t::con_command_base_t( const char* command_name, const char* command_help, int command_flags )
	    : m_name{ command_name }, m_help{ command_help }, m_flags{ command_flags } {
	}

	con_command_t::con_command_t( const char* command_name, const char* command_help, callback_fn_t command_callback )
	    : con_command_base_t{ command_name, command_help, 0 }, m_callback{ command_callback }, m_callback_flags{ 0x02 } {
	}

	bool ConsoleCommandRegistry::connect( sdk::ICvar* cvar ) {
		if( m_cvar )
			return true;

		m_cvar = cvar;
		if( !m_cvar )
			return false;

		// borrow the engine's command VMT from a known built-in command.
		void* reference = m_cvar->FindCommand( "listdemo" );
		if( !reference ) {
			m_cvar = nullptr;
			return false;
		}
		m_command_vmt = static_cast< con_command_base_t* >( reference )->m_vmt;
		return m_command_vmt != nullptr;
	}

	bool ConsoleCommandRegistry::add( const char* name, const char* help, callback_fn_t callback ) {
		// Source expects the object layout declared in console_commands.h.
		if( !m_cvar || !m_command_vmt || !callback )
			return false;

		auto command   = std::make_unique< con_command_t >( name, help, callback );
		command->m_vmt = m_command_vmt;
		m_cvar->RegisterCommand( command.get( ) );
		command->m_registered = true;
		m_commands.push_back( std::move( command ) );
		return true;
	}

	bool ConsoleCommandRegistry::remove_flags( const char* name, int flags ) {
		if( !m_cvar || !name || !flags )
			return false;

		// engine cvars share ConCommandBase's leading layout. remember the exact
		// value so plugin unload restores every flag, including flags owned by
		// another plugin loaded before p2boost.
		auto* command = static_cast< con_command_base_t* >( m_cvar->FindCommand( name ) );
		if( !command )
			return false;

		for( const restored_flags_t& restored : m_restored_flags ) {
			if( restored.m_command == command )
				return true;
		}

		m_restored_flags.push_back( { command, command->m_flags } );
		command->m_flags &= ~flags;
		return true;
	}

	void ConsoleCommandRegistry::disconnect( ) {
		// restore engine-owned cvars before releasing the ICvar interface.
		for( const restored_flags_t& restored : m_restored_flags ) {
			if( restored.m_command )
				restored.m_command->m_flags = restored.m_original;
		}
		m_restored_flags.clear( );

		// unregister before releasing storage the engine may still reference.
		if( m_cvar ) {
			for( auto& command : m_commands ) {
				if( command->m_registered )
					m_cvar->UnregisterCommand( command.get( ) );
				command->m_registered = false;
			}
		}

		m_commands.clear( );
		m_command_vmt = nullptr;
		m_cvar        = nullptr;
	}

}
