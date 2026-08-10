#pragma once

#include "address.h"

#include <cstddef>
#include <utility>

namespace p2boost::sdk {

	// common, data-free view over a Source interface returned by CreateInterface.
	class Interface {
	public:
		bool valid( ) const noexcept {
			return this && *reinterpret_cast< void* const* const* >( this );
		}

	protected:
		// expose one engine-owned method only through a named wrapper below.
		Address method( std::size_t index ) const noexcept {
			return valid( ) ? methods( )[ index ] : Address{ };
		}

		// hooks replace a slot, while ordinary wrappers invoke its current target.
		void** method_slot( std::size_t index ) noexcept {
			return valid( ) ? methods( ) + index : nullptr;
		}

		// the explicit function type preserves references and calling conventions.
		template < std::size_t index, typename function_t, typename... args_t >
		decltype( auto ) invoke( args_t&&... arguments ) {
			auto target = method( index ).as< function_t >( );
			return target( this, std::forward< args_t >( arguments )... );
		}

	private:
		void** methods( ) const noexcept {
			return *reinterpret_cast< void*** >( const_cast< Interface* >( this ) );
		}
	};

}
