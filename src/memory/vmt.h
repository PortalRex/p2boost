#pragma once

#include <cstddef>

namespace p2boost {

	// return the live VMT of an engine or COM object with no typed wrapper.
	template < typename object_t >
	void** vmt_of( object_t* object ) noexcept {
		return object ? *reinterpret_cast< void*** >( object ) : nullptr;
	}

	// keep slot arithmetic at the boundary where a hook actually needs it.
	template < typename object_t >
	void** vmt_slot( object_t* object, std::size_t index ) noexcept {
		void** methods = vmt_of( object );
		return methods ? methods + index : nullptr;
	}

	// the current target of one slot of an object's live VMT, typed by the
	// caller, which passes the object itself as the first argument.
	template < typename function_t >
	function_t vfunc( const void* object, std::ptrdiff_t slot ) noexcept {
		return reinterpret_cast< function_t >( ( *static_cast< void* const* const* >( object ) )[ slot ] );
	}

}
