#pragma once

#include <cmath>
#include <type_traits>

namespace p2boost {

	// match Source's ordinary three-float vector layout exactly.
	struct vec3_t {
		float x{ };
		float y{ };
		float z{ };

		constexpr vec3_t operator+( const vec3_t& other ) const noexcept {
			return { x + other.x, y + other.y, z + other.z };
		}

		constexpr vec3_t operator-( const vec3_t& other ) const noexcept {
			return { x - other.x, y - other.y, z - other.z };
		}

		constexpr vec3_t operator*( float scale ) const noexcept {
			return { x * scale, y * scale, z * scale };
		}

		constexpr float dot( const vec3_t& other ) const noexcept {
			return x * other.x + y * other.y + z * other.z;
		}

		constexpr vec3_t cross( const vec3_t& other ) const noexcept {
			return {
			    y * other.z - z * other.y,
			    z * other.x - x * other.z,
			    x * other.y - y * other.x,
			};
		}

		bool finite( ) const noexcept {
			return std::isfinite( x ) && std::isfinite( y ) && std::isfinite( z );
		}
	};

	// preserve the row-major 4x4 matrix copied from Portal 2's shadow entry.
	struct matrix4x4_t {
		float m_values[ 4 ][ 4 ]{ };
	};

	static_assert( sizeof( vec3_t ) == sizeof( float ) * 3 );
	static_assert( sizeof( matrix4x4_t ) == sizeof( float ) * 16 );
	static_assert( std::is_trivially_copyable_v< vec3_t > );
	static_assert( std::is_trivially_copyable_v< matrix4x4_t > );

}
