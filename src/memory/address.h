#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace p2boost {

	// stores one native process address without losing its type.
	class Address {
	protected:
		std::uintptr_t m_addr;

	public:
		// default ctor.
		__forceinline Address( ) noexcept
		    : m_addr{ } {
		}

		// native address ctors.
		__forceinline Address( std::uintptr_t address ) noexcept
		    : m_addr{ address } {
		}
		__forceinline Address( const void* address ) noexcept
		    : m_addr{ reinterpret_cast< std::uintptr_t >( address ) } {
		}

		template < typename t, std::enable_if_t< std::is_pointer_v< t >, int > = 0 >
		__forceinline Address( t address ) noexcept
		    : m_addr{ reinterpret_cast< std::uintptr_t >( address ) } {
		}

		// native conversions.
		__forceinline operator bool( ) const noexcept {
			return m_addr != 0;
		}

		__forceinline operator std::uintptr_t( ) const noexcept {
			return m_addr;
		}

		// compare native addresses.
		__forceinline bool operator==( Address address ) const noexcept {
			return m_addr == address.m_addr;
		}

		__forceinline bool operator!=( Address address ) const noexcept {
			return m_addr != address.m_addr;
		}

		// cast the stored address.
		template < typename t = Address >
		__forceinline t as( ) const noexcept {
			if constexpr( std::is_same_v< t, Address > )
				return Address{ m_addr };
			else if constexpr( std::is_pointer_v< t > )
				return m_addr ? reinterpret_cast< t >( m_addr ) : nullptr;
			else
				return static_cast< t >( m_addr );
		}

		// add a byte offset.
		__forceinline Address add( std::ptrdiff_t offset ) const noexcept {
			return m_addr ? Address{ m_addr + static_cast< std::uintptr_t >( offset ) } : Address{ };
		}

		// subtract a byte offset.
		__forceinline Address sub( std::ptrdiff_t offset ) const noexcept {
			return m_addr ? Address{ m_addr - static_cast< std::uintptr_t >( offset ) } : Address{ };
		}

		// read a trivial value at a byte offset.
		template < typename t = Address >
		__forceinline t at( std::ptrdiff_t offset = 0 ) const noexcept {
			static_assert( std::is_trivially_copyable_v< t >, "Address::at requires a trivial type" );

			t out{ };
			if( m_addr )
				std::memcpy( &out, add( offset ).as< const void* >( ), sizeof( out ) );

			return out;
		}

		// dereference the stored address.
		template < typename t = Address >
		__forceinline t to( ) const noexcept {
			return at< t >( );
		}

		// follow a signed x86 rel32 operand.
		__forceinline Address rel32( std::ptrdiff_t offset = 0 ) const noexcept {
			if( !m_addr )
				return { };

			Address displacement{ add( offset ) };
			return displacement.add( sizeof( std::int32_t ) ).add( displacement.at< std::int32_t >( ) );
		}
	};

}
