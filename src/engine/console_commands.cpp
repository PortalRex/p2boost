#include "console_commands.h"

#include <Windows.h>

namespace p2boost {
	ConsoleCommandBase::ConsoleCommandBase( const char* command_name, const char* command_help, int command_flags )
		: m_next( nullptr ), m_registered( false ), m_padding{ }, m_name( command_name ), m_help( command_help ), m_flags( command_flags ) {
	}

	ConsoleCommand::ConsoleCommand( const char* command_name, const char* command_help, Callback command_callback )
		: ConsoleCommandBase( command_name, command_help, 0 ), m_callback( command_callback ), m_completion_callback( nullptr ),
		  m_callback_flags( 0x02 ), m_callback_padding{ } {
	}

	bool ConsoleCommandRegistry::connect( sdk::CreateInterfaceFn engine_factory ) {
		if( m_cvar )
			return true;

		// the plugin factory is preferred because it follows the engine load path.
		if( engine_factory )
			m_cvar = static_cast< sdk::Cvar* >( engine_factory( "VEngineCvar007", nullptr ) );

		if( !m_cvar ) {
			// vstdlib exposes the same factory when the engine omits ICvar.
			HMODULE vstdlib{ GetModuleHandleW( L"vstdlib.dll" ) };
			if( !vstdlib )
				return false;

			using GetFactoryFn = sdk::CreateInterfaceFn( __cdecl* )( );
			auto get_factory   = reinterpret_cast< GetFactoryFn >( GetProcAddress( vstdlib, "VStdLib_GetICVarFactory" ) );
			if( !get_factory )
				return false;

			sdk::CreateInterfaceFn factory = get_factory( );
			if( factory )
				m_cvar = static_cast< sdk::Cvar* >( factory( "VEngineCvar007", nullptr ) );
		}

		if( !m_cvar )
			return false;

		// borrow the engine's command VMT from a known built-in command.
		void* reference = m_cvar->FindCommand( "listdemo" );
		if( !reference ) {
			m_cvar = nullptr;
			return false;
		}
		m_command_vmt = *reinterpret_cast< void** >( reference );
		return m_command_vmt != nullptr;
	}

	bool ConsoleCommandRegistry::add( const char* name, const char* help, Callback callback ) {
		// Source expects the object layout declared in console_commands.h.
		if( !m_cvar || !m_command_vmt || !callback )
			return false;

		auto command								  = std::make_unique< ConsoleCommand >( name, help, callback );
		*reinterpret_cast< void** >( command.get( ) ) = m_command_vmt;
		m_cvar->RegisterCommand( command.get( ) );
		command->m_registered = true;
		m_commands.push_back( std::move( command ) );
		return true;
	}

	void ConsoleCommandRegistry::disconnect( ) {
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
		m_cvar		  = nullptr;
	}

}
