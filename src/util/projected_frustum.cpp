#include "projected_frustum.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace p2boost::frustum {

	constexpr std::array< std::array< size_t, 2 >, 12 > frustum_edges{ {
	    { 0, 1 },
	    { 0, 2 },
	    { 1, 3 },
	    { 2, 3 },
	    { 4, 5 },
	    { 4, 6 },
	    { 5, 7 },
	    { 6, 7 },
	    { 0, 4 },
	    { 1, 5 },
	    { 2, 6 },
	    { 3, 7 },
	} };

	constexpr std::array< std::array< size_t, 3 >, 6 > frustum_faces{ {
	    { 0, 2, 3 },
	    { 4, 5, 7 },
	    { 0, 4, 6 },
	    { 1, 3, 7 },
	    { 0, 1, 5 },
	    { 2, 6, 7 },
	} };

	constexpr float pi{ 3.14159265358979323846f };
	constexpr float axis_epsilon{ 1.0e-8f };
	constexpr float separation_margin{ 0.25f };

	// invert the normalized-volume matrix with pivoted Gauss-Jordan elimination.
	// A singular or non-finite matrix is retained by the caller, never culled.
	bool inverse( const matrix4x4_t& input, matrix4x4_t& output ) noexcept {
		float work[ 4 ][ 8 ]{ };
		for( size_t row = 0; row < 4; ++row ) {
			for( size_t column = 0; column < 4; ++column )
				work[ row ][ column ] = input.m_values[ row ][ column ];
			work[ row ][ row + 4 ] = 1.0f;
		}

		for( size_t column = 0; column < 4; ++column ) {
			size_t pivot = column;
			for( size_t row = column + 1; row < 4; ++row ) {
				if( std::fabs( work[ row ][ column ] ) > std::fabs( work[ pivot ][ column ] ) )
					pivot = row;
			}

			const float divisor = work[ pivot ][ column ];
			if( !std::isfinite( divisor ) || std::fabs( divisor ) < axis_epsilon )
				return false;

			if( pivot != column ) {
				for( size_t element = 0; element < 8; ++element )
					std::swap( work[ pivot ][ element ], work[ column ][ element ] );
			}

			for( size_t element = 0; element < 8; ++element )
				work[ column ][ element ] /= divisor;

			for( size_t row = 0; row < 4; ++row ) {
				if( row == column )
					continue;

				const float factor = work[ row ][ column ];
				for( size_t element = 0; element < 8; ++element )
					work[ row ][ element ] -= factor * work[ column ][ element ];
			}
		}

		for( size_t row = 0; row < 4; ++row ) {
			for( size_t column = 0; column < 4; ++column ) {
				output.m_values[ row ][ column ] = work[ row ][ column + 4 ];
				if( !std::isfinite( output.m_values[ row ][ column ] ) )
					return false;
			}
		}
		return true;
	}

	bool transform_volume_corner( const matrix4x4_t& volume_to_world, float x, float y, float z, vec3_t& output ) noexcept {
		const float transformed_x = volume_to_world.m_values[ 0 ][ 0 ] * x + volume_to_world.m_values[ 0 ][ 1 ] * y +
		                            volume_to_world.m_values[ 0 ][ 2 ] * z + volume_to_world.m_values[ 0 ][ 3 ];
		const float transformed_y = volume_to_world.m_values[ 1 ][ 0 ] * x + volume_to_world.m_values[ 1 ][ 1 ] * y +
		                            volume_to_world.m_values[ 1 ][ 2 ] * z + volume_to_world.m_values[ 1 ][ 3 ];
		const float transformed_z = volume_to_world.m_values[ 2 ][ 0 ] * x + volume_to_world.m_values[ 2 ][ 1 ] * y +
		                            volume_to_world.m_values[ 2 ][ 2 ] * z + volume_to_world.m_values[ 2 ][ 3 ];
		const float transformed_w = volume_to_world.m_values[ 3 ][ 0 ] * x + volume_to_world.m_values[ 3 ][ 1 ] * y +
		                            volume_to_world.m_values[ 3 ][ 2 ] * z + volume_to_world.m_values[ 3 ][ 3 ];
		if( !std::isfinite( transformed_w ) || std::fabs( transformed_w ) < axis_epsilon )
			return false;

		output = { transformed_x / transformed_w, transformed_y / transformed_w, transformed_z / transformed_w };
		return output.finite( );
	}

	bool projected_frustum( const matrix4x4_t& world_to_volume, frustum_t& output ) noexcept {
		matrix4x4_t volume_to_world{ };
		if( !inverse( world_to_volume, volume_to_world ) )
			return false;

		for( size_t z = 0; z < 2; ++z ) {
			for( size_t y = 0; y < 2; ++y ) {
				for( size_t x = 0; x < 2; ++x ) {
					const size_t index = z * 4 + y * 2 + x;
					if( !transform_volume_corner(
					        volume_to_world, static_cast< float >( x ), static_cast< float >( y ), static_cast< float >( z ), output[ index ] ) )
						return false;
				}
			}
		}
		return true;
	}

	// build the exact finite camera frustum used by Portal 2's perspective
	// selection. The Source QAngle basis includes roll for remote views.
	bool camera_frustum( const camera_view_t& view, frustum_t& output ) noexcept {
		const vec3_t& origin = view.origin;
		const vec3_t& angles = view.angles;
		const float   near_z = view.near_z;
		const float   far_z  = view.far_z;
		const float   fov    = view.fov;
		const float   aspect = view.aspect_ratio;
		if( !origin.finite( ) || !angles.finite( ) || !std::isfinite( near_z ) || !std::isfinite( far_z ) ||
		    !std::isfinite( fov ) || !std::isfinite( aspect ) || near_z <= 0.0f || far_z <= near_z ||
		    fov <= 0.0f || fov >= 179.0f || aspect <= 0.0f )
			return false;

		const float pitch        = angles.x * pi / 180.0f;
		const float yaw          = angles.y * pi / 180.0f;
		const float roll         = angles.z * pi / 180.0f;
		const float sine_pitch   = std::sin( pitch );
		const float cosine_pitch = std::cos( pitch );
		const float sine_yaw     = std::sin( yaw );
		const float cosine_yaw   = std::cos( yaw );
		const float sine_roll    = std::sin( roll );
		const float cosine_roll  = std::cos( roll );

		const vec3_t forward{ cosine_pitch * cosine_yaw, cosine_pitch * sine_yaw, -sine_pitch };
		const vec3_t right{
		    -sine_roll * sine_pitch * cosine_yaw + cosine_roll * sine_yaw,
		    -sine_roll * sine_pitch * sine_yaw - cosine_roll * cosine_yaw,
		    -sine_roll * cosine_pitch,
		};
		const vec3_t up{
		    cosine_roll * sine_pitch * cosine_yaw + sine_roll * sine_yaw,
		    cosine_roll * sine_pitch * sine_yaw - sine_roll * cosine_yaw,
		    cosine_roll * cosine_pitch,
		};

		const float tangent_x = std::tan( fov * pi / 360.0f );
		const float tangent_y = tangent_x / aspect;
		if( !std::isfinite( tangent_x ) || !std::isfinite( tangent_y ) )
			return false;

		for( size_t z_index = 0; z_index < 2; ++z_index ) {
			const float  distance = z_index == 0 ? near_z : far_z;
			const vec3_t center   = origin + forward * distance;
			for( size_t y_index = 0; y_index < 2; ++y_index ) {
				const float y_scale = y_index == 0 ? -distance * tangent_y : distance * tangent_y;
				for( size_t x_index = 0; x_index < 2; ++x_index ) {
					const float x_scale                           = x_index == 0 ? -distance * tangent_x : distance * tangent_x;
					output[ z_index * 4 + y_index * 2 + x_index ] = center + right * x_scale + up * y_scale;
				}
			}
		}
		return true;
	}

	bool separated_on_axis( const frustum_t& left, const frustum_t& right, vec3_t axis ) noexcept {
		const float length_squared = axis.dot( axis );
		if( !std::isfinite( length_squared ) || length_squared < axis_epsilon )
			return false;

		axis            = axis * ( 1.0f / std::sqrt( length_squared ) );
		float left_min  = std::numeric_limits< float >::max( );
		float left_max  = std::numeric_limits< float >::lowest( );
		float right_min = std::numeric_limits< float >::max( );
		float right_max = std::numeric_limits< float >::lowest( );
		for( const auto& corner : left ) {
			const float projection = corner.dot( axis );
			left_min               = std::min( left_min, projection );
			left_max               = std::max( left_max, projection );
		}
		for( const auto& corner : right ) {
			const float projection = corner.dot( axis );
			right_min              = std::min( right_min, projection );
			right_max              = std::max( right_max, projection );
		}

		return left_max + separation_margin < right_min || right_max + separation_margin < left_min;
	}

	// SAT is exact for convex polyhedra when testing both face normals and
	// every cross-product of their edges. Degenerate axes are retained.
	bool frusta_intersect( const frustum_t& left, const frustum_t& right ) noexcept {
		for( const auto& face : frustum_faces ) {
			const vec3_t normal = ( left[ face[ 1 ] ] - left[ face[ 0 ] ] ).cross( left[ face[ 2 ] ] - left[ face[ 0 ] ] );
			if( separated_on_axis( left, right, normal ) )
				return false;
		}
		for( const auto& face : frustum_faces ) {
			const vec3_t normal = ( right[ face[ 1 ] ] - right[ face[ 0 ] ] ).cross( right[ face[ 2 ] ] - right[ face[ 0 ] ] );
			if( separated_on_axis( left, right, normal ) )
				return false;
		}

		for( const auto& left_edge : frustum_edges ) {
			const vec3_t left_direction = left[ left_edge[ 1 ] ] - left[ left_edge[ 0 ] ];
			for( const auto& right_edge : frustum_edges ) {
				const vec3_t right_direction = right[ right_edge[ 1 ] ] - right[ right_edge[ 0 ] ];
				if( separated_on_axis( left, right, left_direction.cross( right_direction ) ) )
					return false;
			}
		}
		return true;
	}

}
