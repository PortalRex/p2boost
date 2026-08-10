#pragma once

#include "interface.h"
#include "module.h"
#include "sdk.h"

#include <type_traits>

namespace p2boost::sdk {

	// one non-owning CreateInterface entry point with typed, versioned lookup.
	class InterfaceFactory {
	public:
		InterfaceFactory( ) noexcept = default;

		explicit InterfaceFactory( create_interface_fn_t factory ) noexcept
		    : m_factory{ factory } {
		}

		explicit InterfaceFactory( const Module& module ) noexcept
		    : m_factory{ module.export_function< create_interface_fn_t >( "CreateInterface" ) } {
		}

		explicit operator bool( ) const noexcept {
			return m_factory != nullptr;
		}

		template < typename interface_t >
		interface_t* get( ) const noexcept {
			static_assert( std::is_base_of_v< Interface, interface_t >, "Source wrappers must derive from Interface" );
			if( !m_factory )
				return nullptr;

			auto* result = static_cast< interface_t* >( m_factory( interface_t::version, nullptr ) );
			return result && result->valid( ) ? result : nullptr;
		}

	private:
		create_interface_fn_t m_factory{ };
	};

}
