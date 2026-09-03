#pragma once

#include "game_profile.h"
#include "math_types.h"

#include <cstdint>
#include <cstring>

namespace p2boost {

	// typed read-only view over the small CViewSetup subset used by shadow
	// culling. the profile supplies the recovered field offsets; production
	// code uses the active game, tests pass the base profile explicitly.
	class client_view_t {
	public:
		client_view_t( const void* view, const game::profile_t* profile = game::active( ) ) noexcept
		    : m_bytes{ static_cast< const std::uint8_t* >( view ) }, m_profile{ profile } {
		}

		explicit operator bool( ) const noexcept {
			return m_bytes != nullptr && m_profile != nullptr;
		}

		vec3_t origin( ) const noexcept {
			return read< vec3_t >( m_profile->view_origin );
		}

		vec3_t angles( ) const noexcept {
			return read< vec3_t >( m_profile->view_angles );
		}

		float near_z( ) const noexcept {
			return read< float >( m_profile->view_near_z );
		}

		float far_z( ) const noexcept {
			return read< float >( m_profile->view_far_z );
		}

		float fov( ) const noexcept {
			return read< float >( m_profile->view_fov );
		}

		float aspect_ratio( ) const noexcept {
			return read< float >( m_profile->view_aspect_ratio );
		}

	private:
		template < typename value_t >
		value_t read( std::ptrdiff_t offset ) const noexcept {
			value_t value{ };
			if( m_bytes && offset >= 0 )
				std::memcpy( &value, m_bytes + offset, sizeof( value ) );
			return value;
		}

		const std::uint8_t*    m_bytes{ };
		const game::profile_t* m_profile{ };
	};

}