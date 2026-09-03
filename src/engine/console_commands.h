#pragma once

#include "interfaces.h"
#include "module.h"
#include "sdk.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace p2boost {

	// Portal 2's ConCommand ABI predates the public Source SDK 2013 ICvar layout.
	// these fields are validated against the shipped build and SAR's public
	// Portal 2 implementation. the engine-owned VMT is copied from an
	// existing command before registration.
	struct con_command_base_t {
		con_command_base_t( const char* name, const char* help, int flags );

		void*               m_vmt{ };
		con_command_base_t* m_next{ };
		bool                m_registered{ };
		std::uint8_t        m_padding[ 3 ]{ };
		const char*         m_name{ };
		const char*         m_help{ };
		int                 m_flags{ };
	};

	struct con_command_t final : con_command_base_t {
		using callback_fn_t            = void( __cdecl* )( const sdk::CCommand& );
		using completion_callback_fn_t = int( __cdecl* )( const char*, char ( * )[ 64 ] );

		con_command_t( const char* name, const char* help, callback_fn_t callback );

		callback_fn_t            m_callback{ };
		completion_callback_fn_t m_completion_callback{ };
		std::uint8_t             m_callback_flags{ };
		std::uint8_t             m_callback_padding[ 3 ]{ };
	};

	// read-only view of an engine-owned ConVar. Portal 2 places a second VMT
	// pointer between ConCommandBase and the public value fields.
	struct con_var_t final : con_command_base_t {
		void*       m_variable_vmt;
		con_var_t*  m_parent;
		const char* m_default;
		char*       m_string;
		int         m_string_length;
		float       m_float;
		int         m_integer;

		float value_float( ) const noexcept {
			return m_parent ? m_parent->m_float : m_float;
		}

		int value_integer( ) const noexcept {
			return m_parent ? m_parent->m_integer : m_integer;
		}
	};

	static_assert( sizeof( con_command_base_t ) == 24, "Portal 2 ConCommandBase ABI changed" );
	static_assert( sizeof( con_command_t ) == 36, "Portal 2 ConCommand ABI changed" );
	static_assert( sizeof( con_var_t ) == 52, "Portal 2 ConVar ABI changed" );

	// Source's public flag value is stable across the Portal 2 branch.
	constexpr int cvar_flag_cheat{ 1 << 14 };

	class ConsoleCommandRegistry {
	public:
		using callback_fn_t = con_command_t::callback_fn_t;
		bool       connect( sdk::ICvar* cvar );
		bool       add( const char* name, const char* help, callback_fn_t callback );
		bool       remove_flags( const char* name, int flags );
		con_var_t* find_variable( const char* name ) const {
			return m_cvar && name ? static_cast< con_var_t* >( m_cvar->FindCommand( name ) ) : nullptr;
		}
		void disconnect( );
		~ConsoleCommandRegistry( ) {
			disconnect( );
		}

	private:
		struct restored_flags_t {
			con_command_base_t* m_command{ };
			int                 m_original{ };
		};

		sdk::ICvar*                                     m_cvar        = nullptr;
		void*                                           m_command_vmt = nullptr;
		std::vector< std::unique_ptr< con_command_t > > m_commands;
		std::vector< restored_flags_t >                 m_restored_flags;
	};

}
