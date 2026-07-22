#pragma once

#include "address.h"

namespace p2boost {

	// views one verified x86 ebp stack frame.
	class Stack : public Address {
	public:
		// default ctor.
		__forceinline Stack( ) noexcept : Address{ } {
		}

		// frame ctors.
		__forceinline Stack( Address base ) noexcept : Address{ base } {
		}
		__forceinline Stack( const void* base ) noexcept : Address{ base } {
		}

		// get the saved return address.
		__forceinline Address ReturnAddress( ) const noexcept {
			return m_addr ? Address{ at< std::uintptr_t >( sizeof( std::uintptr_t ) ) } : Address{ };
		}

		// get the address that stores the return address.
		__forceinline Address AddressOfReturnAddress( ) const noexcept {
			return m_addr ? add( sizeof( std::uintptr_t ) ) : Address{ };
		}

		// walk to the caller's stack frame.
		__forceinline Stack next( ) const noexcept {
			return Stack{ Address{ at< std::uintptr_t >( ) } };
		}

		// get a local variable address.
		__forceinline Address local( size_t offset ) const noexcept {
			return sub( static_cast< std::ptrdiff_t >( offset ) );
		}

		// get an argument address by index.
		__forceinline Address arg( size_t index ) const noexcept {
			return add( static_cast< std::ptrdiff_t >( ( index + 2 ) * sizeof( std::uintptr_t ) ) );
		}
	};

}
