#pragma once

#include "build_23973718.h"
#include "math_types.h"

#include <cstdint>
#include <cstring>

namespace p2boost {

	// typed read-only view over the small CViewSetup subset used by shadow culling.
	class client_view_t {
	public:
		explicit client_view_t( const void* view ) noexcept
		    : m_bytes{ static_cast< const std::uint8_t* >( view ) } {
		}

		explicit operator bool( ) const noexcept {
			return m_bytes != nullptr;
		}

		vec3_t origin( ) const noexcept {
			return read< vec3_t >( portal2::view::origin );
		}

		vec3_t angles( ) const noexcept {
			return read< vec3_t >( portal2::view::angles );
		}

		float near_z( ) const noexcept {
			return read< float >( portal2::view::near_z );
		}

		float far_z( ) const noexcept {
			return read< float >( portal2::view::far_z );
		}

		float fov( ) const noexcept {
			return read< float >( portal2::view::fov );
		}

		float aspect_ratio( ) const noexcept {
			return read< float >( portal2::view::aspect_ratio );
		}

	private:
		template < typename value_t >
		value_t read( std::ptrdiff_t offset ) const noexcept {
			value_t value{ };
			if( m_bytes )
				std::memcpy( &value, m_bytes + offset, sizeof( value ) );
			return value;
		}

		const std::uint8_t* m_bytes{ };
	};

}
