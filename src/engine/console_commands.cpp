#include "console_commands.h"

#include <Windows.h>

namespace p2boost {
	namespace {
		// ICvar007 slots recovered from Valve's public Source interface layout.
		constexpr std::size_t register_command_slot = 9;
		constexpr std::size_t unregister_command_slot = 10;
		constexpr std::size_t find_command_slot = 13;

		template < typename T >
		T vfunc( void* object, std::size_t slot ) {
			// keep raw vtable access in one small, typed helper.
			return reinterpret_cast< T >( ( *reinterpret_cast< void*** >( object ) )[ slot ] );
		}
	}

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
			m_cvar = engine_factory( "VEngineCvar007", nullptr );
		if( !m_cvar ) {
			// vstdlib exposes the same factory when the engine omits ICvar.
			HMODULE vstdlib = GetModuleHandleW( L"vstdlib.dll" );
			if( !vstdlib )
				return false;
			using GetFactoryFn = sdk::CreateInterfaceFn( __cdecl* )( );
			auto get_factory = reinterpret_cast< GetFactoryFn >( GetProcAddress( vstdlib, "VStdLib_GetICVarFactory" ) );
			if( !get_factory )
				return false;
			sdk::CreateInterfaceFn factory = get_factory( );
			if( factory )
				m_cvar = factory( "VEngineCvar007", nullptr );
		}
		if( !m_cvar )
			return false;

		// borrow the engine's command vtable from a known built-in command.
		using FindCommandBaseFn = void*(__thiscall*)( void*, const char* );
		auto find = vfunc< FindCommandBaseFn >( m_cvar, find_command_slot );
		void* reference = find( m_cvar, "listdemo" );
		if( !reference ) {
			m_cvar = nullptr;
			return false;
		}
		m_command_vtable = *reinterpret_cast< void** >( reference );
		return m_command_vtable != nullptr;
	}

	bool ConsoleCommandRegistry::add( const char* name, const char* help, Callback callback ) {
		// Source expects the object layout declared in console_commands.h.
		if( !m_cvar || !m_command_vtable || !callback )
			return false;
		auto command = std::make_unique< ConsoleCommand >( name, help, callback );
		*reinterpret_cast< void** >( command.get( ) ) = m_command_vtable;
		using RegisterFn = void( __thiscall* )( void*, ConsoleCommandBase* );
		vfunc< RegisterFn >( m_cvar, register_command_slot )( m_cvar, command.get( ) );
		command->m_registered = true;
		m_commands.push_back( std::move( command ) );
		return true;
	}

	void ConsoleCommandRegistry::disconnect( ) {
		// unregister before releasing storage the engine may still reference.
		if( m_cvar ) {
			using UnregisterFn = void( __thiscall* )( void*, ConsoleCommandBase* );
			auto unregister = vfunc< UnregisterFn >( m_cvar, unregister_command_slot );
			for( auto& command : m_commands ) {
				if( command->m_registered )
					unregister( m_cvar, command.get( ) );
				command->m_registered = false;
			}
		}
		m_commands.clear( );
		m_command_vtable = nullptr;
		m_cvar = nullptr;
	}

}
