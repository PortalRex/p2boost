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

}
