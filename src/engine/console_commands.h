#pragma once

#include "interfaces.h"
#include "sdk.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace p2boost {

	// Portal 2's ConCommand ABI predates the public Source SDK 2013 ICvar layout.
	// these fields are validated against the shipped build and SAR's public
	// Portal 2 implementation. the engine-owned VMT is copied from an
	// existing command before registration.
	struct ConsoleCommandBase {
		ConsoleCommandBase( const char* name, const char* help, int flags );
		virtual void DestroyStub( ) {
		}
		virtual bool IsCommandStub( ) const {
			return false;
		}

		ConsoleCommandBase* m_next;
		bool				m_registered;
		std::uint8_t		m_padding[ 3 ];
		const char*			m_name;
		const char*			m_help;
		int					m_flags;
	};

	struct ConsoleCommand final : ConsoleCommandBase {
		using Callback			 = void( __cdecl* )( const sdk::CCommand& );
		using CompletionCallback = int( __cdecl* )( const char*, char ( * )[ 64 ] );

		ConsoleCommand( const char* name, const char* help, Callback callback );

		Callback		   m_callback;
		CompletionCallback m_completion_callback;
		std::uint8_t	   m_callback_flags;
		std::uint8_t	   m_callback_padding[ 3 ];
	};

	// read-only view of an engine-owned ConVar. Portal 2 places a second VMT
	// pointer between ConCommandBase and the public value fields.
	struct ConsoleVariable final : ConsoleCommandBase {
		void*			 m_variable_vmt;
		ConsoleVariable* m_parent;
		const char*		 m_default;
		char*			 m_string;
		int				 m_string_length;
		float			 m_float;
		int				 m_integer;

		float value_float( ) const noexcept {
			return m_parent ? m_parent->m_float : m_float;
		}

		int value_integer( ) const noexcept {
			return m_parent ? m_parent->m_integer : m_integer;
		}
	};

	static_assert( sizeof( ConsoleCommandBase ) == 24, "Portal 2 ConCommandBase ABI changed" );
	static_assert( sizeof( ConsoleCommand ) == 36, "Portal 2 ConCommand ABI changed" );

	// Source's public flag value is stable across the Portal 2 branch.
	constexpr int cvar_flag_cheat{ 1 << 14 };

	class ConsoleCommandRegistry {
	public:
		using Callback = ConsoleCommand::Callback;
		bool			 connect( sdk::CreateInterfaceFn engine_factory );
		bool			 add( const char* name, const char* help, Callback callback );
		bool			 remove_flags( const char* name, int flags );
		ConsoleVariable* find_variable( const char* name ) const {
			return m_cvar && name ? static_cast< ConsoleVariable* >( m_cvar->FindCommand( name ) ) : nullptr;
		}
		void disconnect( );
		~ConsoleCommandRegistry( ) {
			disconnect( );
		}

	private:
		struct restored_flags_t {
			ConsoleCommandBase* m_command{ };
			int					m_original{ };
		};

		sdk::Cvar*										 m_cvar		   = nullptr;
		void*											 m_command_vmt = nullptr;
		std::vector< std::unique_ptr< ConsoleCommand > > m_commands;
		std::vector< restored_flags_t >					 m_restored_flags;
	};

}
