#pragma once

#include "interface.h"

namespace p2boost::sdk {

	// VEngineCvar007 owns all engine console commands and variables.
	class ICvar final : public Interface {
	public:
		static constexpr const char* version{ "VEngineCvar007" };
		static constexpr std::size_t register_command_slot{ 9 };
		static constexpr std::size_t unregister_command_slot{ 10 };
		static constexpr std::size_t find_command_slot{ 13 };

		void RegisterCommand( void* command ) {
			using function_t = void( __thiscall* )( void*, void* );
			invoke< register_command_slot, function_t >( command );
		}

		void UnregisterCommand( void* command ) {
			using function_t = void( __thiscall* )( void*, void* );
			invoke< unregister_command_slot, function_t >( command );
		}

		void* FindCommand( const char* name ) {
			using function_t = void*(__thiscall*)( void*, const char* );
			return invoke< find_command_slot, function_t >( name );
		}
	};

}
