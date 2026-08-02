#pragma once

#include "sdk.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace p2boost {

	// Portal 2's ConCommand ABI predates the public Source SDK 2013 ICvar layout.
	// These fields and slots are validated against the shipped build and SAR's
	// public Portal 2 implementation. The engine-owned vtable is copied from an
	// existing command before registration.
	struct ConsoleCommandBase {
		ConsoleCommandBase( const char* name, const char* help, int flags );
		virtual void DestroyStub( ) {
		}
		virtual bool IsCommandStub( ) const {
			return false;
		}

		ConsoleCommandBase* m_next;
		bool m_registered;
		std::uint8_t m_padding[ 3 ];
		const char* m_name;
		const char* m_help;
		int m_flags;
	};

	struct ConsoleCommand final : ConsoleCommandBase {
		using Callback = void( __cdecl* )( const sdk::CCommand& );
		using CompletionCallback = int( __cdecl* )( const char*, char ( * )[ 64 ] );

		ConsoleCommand( const char* name, const char* help, Callback callback );

		Callback m_callback;
		CompletionCallback m_completion_callback;
		std::uint8_t m_callback_flags;
		std::uint8_t m_callback_padding[ 3 ];
	};

	static_assert( sizeof( ConsoleCommandBase ) == 24, "Portal 2 ConCommandBase ABI changed" );
	static_assert( sizeof( ConsoleCommand ) == 36, "Portal 2 ConCommand ABI changed" );

	class ConsoleCommandRegistry {
	public:
		using Callback = ConsoleCommand::Callback;
		bool connect( sdk::CreateInterfaceFn engine_factory );
		bool add( const char* name, const char* help, Callback callback );
		void disconnect( );
		~ConsoleCommandRegistry( ) {
			disconnect( );
		}

	private:
		void* m_cvar = nullptr;
		void* m_command_vtable = nullptr;
		std::vector< std::unique_ptr< ConsoleCommand > > m_commands;
	};

}
