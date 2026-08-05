#include "projected_shadow_cache.h"

#include "address.h"
#include "interfaces.h"
#include "frame_metrics.h"
#include "pattern.h"
#include "qpc.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

namespace p2boost {
	namespace {

		struct vector_t {
			float m_x{ };
			float m_y{ };
			float m_z{ };

			vector_t operator+( const vector_t& other ) const noexcept {
				return { m_x + other.m_x, m_y + other.m_y, m_z + other.m_z };
			}

			vector_t operator-( const vector_t& other ) const noexcept {
				return { m_x - other.m_x, m_y - other.m_y, m_z - other.m_z };
			}

			vector_t operator*( float scale ) const noexcept {
				return { m_x * scale, m_y * scale, m_z * scale };
			}
		};

		struct matrix_t {
			float m_value[ 4 ][ 4 ]{ };
		};

		using frustum_t = std::array< vector_t, 8 >;

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

		float dot( const vector_t& left, const vector_t& right ) noexcept {
			return left.m_x * right.m_x + left.m_y * right.m_y + left.m_z * right.m_z;
		}

		vector_t cross( const vector_t& left, const vector_t& right ) noexcept {
			return {
				left.m_y * right.m_z - left.m_z * right.m_y,
				left.m_z * right.m_x - left.m_x * right.m_z,
				left.m_x * right.m_y - left.m_y * right.m_x,
			};
		}

		bool finite( const vector_t& value ) noexcept {
			return std::isfinite( value.m_x ) && std::isfinite( value.m_y ) && std::isfinite( value.m_z );
		}

		// Invert the normalized-volume matrix with pivoted Gauss-Jordan elimination.
		// A singular or non-finite matrix is retained by the caller, never culled.
		bool inverse( const matrix_t& input, matrix_t& output ) noexcept {
			float work[ 4 ][ 8 ]{ };
			for( size_t row = 0; row < 4; ++row ) {
				for( size_t column = 0; column < 4; ++column )
					work[ row ][ column ] = input.m_value[ row ][ column ];
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
					output.m_value[ row ][ column ] = work[ row ][ column + 4 ];
					if( !std::isfinite( output.m_value[ row ][ column ] ) )
						return false;
				}
			}
			return true;
		}

		bool transform_volume_corner( const matrix_t& volume_to_world, float x, float y, float z, vector_t& output ) noexcept {
			const float transformed_x = volume_to_world.m_value[ 0 ][ 0 ] * x + volume_to_world.m_value[ 0 ][ 1 ] * y +
										volume_to_world.m_value[ 0 ][ 2 ] * z + volume_to_world.m_value[ 0 ][ 3 ];
			const float transformed_y = volume_to_world.m_value[ 1 ][ 0 ] * x + volume_to_world.m_value[ 1 ][ 1 ] * y +
										volume_to_world.m_value[ 1 ][ 2 ] * z + volume_to_world.m_value[ 1 ][ 3 ];
			const float transformed_z = volume_to_world.m_value[ 2 ][ 0 ] * x + volume_to_world.m_value[ 2 ][ 1 ] * y +
										volume_to_world.m_value[ 2 ][ 2 ] * z + volume_to_world.m_value[ 2 ][ 3 ];
			const float transformed_w = volume_to_world.m_value[ 3 ][ 0 ] * x + volume_to_world.m_value[ 3 ][ 1 ] * y +
										volume_to_world.m_value[ 3 ][ 2 ] * z + volume_to_world.m_value[ 3 ][ 3 ];
			if( !std::isfinite( transformed_w ) || std::fabs( transformed_w ) < axis_epsilon )
				return false;

			output = { transformed_x / transformed_w, transformed_y / transformed_w, transformed_z / transformed_w };
			return finite( output );
		}

		bool projected_frustum( const matrix_t& world_to_volume, frustum_t& output ) noexcept {
			matrix_t volume_to_world{ };
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

		vector_t read_vector( const std::uint8_t* base, std::ptrdiff_t offset ) noexcept {
			vector_t value{ };
			std::memcpy( &value, base + offset, sizeof( value ) );
			return value;
		}

		float read_float( const std::uint8_t* base, std::ptrdiff_t offset ) noexcept {
			float value{ };
			std::memcpy( &value, base + offset, sizeof( value ) );
			return value;
		}

		// Build the exact finite camera frustum used by Portal 2's perspective
		// selection. The Source QAngle basis includes roll for remote views.
		bool camera_frustum( const void* opaque_view, frustum_t& output ) noexcept {
			if( !opaque_view )
				return false;

			const auto*	   view	  = static_cast< const std::uint8_t* >( opaque_view );
			const vector_t origin = read_vector( view, portal2::view::origin );
			const vector_t angles = read_vector( view, portal2::view::angles );
			const float	   near_z = read_float( view, portal2::view::near_z );
			const float	   far_z  = read_float( view, portal2::view::far_z );
			const float	   fov	  = read_float( view, portal2::view::fov );
			const float	   aspect = read_float( view, portal2::view::aspect_ratio );
			if( !finite( origin ) || !finite( angles ) || !std::isfinite( near_z ) || !std::isfinite( far_z ) ||
				!std::isfinite( fov ) || !std::isfinite( aspect ) || near_z <= 0.0f || far_z <= near_z ||
				fov <= 0.0f || fov >= 179.0f || aspect <= 0.0f )
				return false;

			const float pitch		 = angles.m_x * pi / 180.0f;
			const float yaw			 = angles.m_y * pi / 180.0f;
			const float roll		 = angles.m_z * pi / 180.0f;
			const float sine_pitch	 = std::sin( pitch );
			const float cosine_pitch = std::cos( pitch );
			const float sine_yaw	 = std::sin( yaw );
			const float cosine_yaw	 = std::cos( yaw );
			const float sine_roll	 = std::sin( roll );
			const float cosine_roll	 = std::cos( roll );

			const vector_t forward{ cosine_pitch * cosine_yaw, cosine_pitch * sine_yaw, -sine_pitch };
			const vector_t right{
				-sine_roll * sine_pitch * cosine_yaw + cosine_roll * sine_yaw,
				-sine_roll * sine_pitch * sine_yaw - cosine_roll * cosine_yaw,
				-sine_roll * cosine_pitch,
			};
			const vector_t up{
				cosine_roll * sine_pitch * cosine_yaw + sine_roll * sine_yaw,
				cosine_roll * sine_pitch * sine_yaw - sine_roll * cosine_yaw,
				cosine_roll * cosine_pitch,
			};

			const float tangent_x = std::tan( fov * pi / 360.0f );
			const float tangent_y = tangent_x / aspect;
			if( !std::isfinite( tangent_x ) || !std::isfinite( tangent_y ) )
				return false;

			for( size_t z_index = 0; z_index < 2; ++z_index ) {
				const float	   distance = z_index == 0 ? near_z : far_z;
				const vector_t center	= origin + forward * distance;
				for( size_t y_index = 0; y_index < 2; ++y_index ) {
					const float y_scale = y_index == 0 ? -distance * tangent_y : distance * tangent_y;
					for( size_t x_index = 0; x_index < 2; ++x_index ) {
						const float x_scale							  = x_index == 0 ? -distance * tangent_x : distance * tangent_x;
						output[ z_index * 4 + y_index * 2 + x_index ] = center + right * x_scale + up * y_scale;
					}
				}
			}
			return true;
		}

		bool separated_on_axis( const frustum_t& left, const frustum_t& right, vector_t axis ) noexcept {
			const float length_squared = dot( axis, axis );
			if( !std::isfinite( length_squared ) || length_squared < axis_epsilon )
				return false;

			axis			= axis * ( 1.0f / std::sqrt( length_squared ) );
			float left_min	= std::numeric_limits< float >::max( );
			float left_max	= std::numeric_limits< float >::lowest( );
			float right_min = std::numeric_limits< float >::max( );
			float right_max = std::numeric_limits< float >::lowest( );
			for( const auto& corner : left ) {
				const float projection = dot( corner, axis );
				left_min			   = std::min( left_min, projection );
				left_max			   = std::max( left_max, projection );
			}
			for( const auto& corner : right ) {
				const float projection = dot( corner, axis );
				right_min			   = std::min( right_min, projection );
				right_max			   = std::max( right_max, projection );
			}

			return left_max + separation_margin < right_min || right_max + separation_margin < left_min;
		}

		// SAT is exact for convex polyhedra when testing both face normals and
		// every cross-product of their edges. Degenerate axes are retained.
		bool frusta_intersect( const frustum_t& left, const frustum_t& right ) noexcept {
			for( const auto& face : frustum_faces ) {
				const vector_t normal = cross( left[ face[ 1 ] ] - left[ face[ 0 ] ], left[ face[ 2 ] ] - left[ face[ 0 ] ] );
				if( separated_on_axis( left, right, normal ) )
					return false;
			}
			for( const auto& face : frustum_faces ) {
				const vector_t normal = cross( right[ face[ 1 ] ] - right[ face[ 0 ] ], right[ face[ 2 ] ] - right[ face[ 0 ] ] );
				if( separated_on_axis( left, right, normal ) )
					return false;
			}

			for( const auto& left_edge : frustum_edges ) {
				const vector_t left_direction = left[ left_edge[ 1 ] ] - left[ left_edge[ 0 ] ];
				for( const auto& right_edge : frustum_edges ) {
					const vector_t right_direction = right[ right_edge[ 1 ] ] - right[ right_edge[ 0 ] ];
					if( separated_on_axis( left, right, cross( left_direction, right_direction ) ) )
						return false;
				}
			}
			return true;
		}

	}

	ProjectedShadowCache* ProjectedShadowCache::m_instance{ };

	ProjectedShadowCache::~ProjectedShadowCache( ) {
		remove( );
	}

	bool ProjectedShadowCache::install( FrameMetrics* metrics ) {
		if( m_instance || !metrics )
			return false;

		const Address client{ GetModuleHandleW( L"client.dll" ) };
		if( !client )
			return false;

		// this xref loads Portal 2's private CClientShadowMgr singleton.
		const Address manager_xref = pattern::find_unique( client, portal2::signature::shadow_manager );
		const Address manager	   = manager_xref.add( portal2::xref::shadow_manager_object ).at< Address >( );
		auto**		  vmt		   = manager.to< void** >( );
		if( !manager || !vmt )
			return false;

		// method 24 is CClientShadowMgr::UpdateProjectedTexture. its second direct
		// call removes the handle from both dirty trees and must never be skipped.
		const Address update_projected{ vmt[ portal2::vmt::update_projected ] };
		const Address remove_dirty_call = update_projected.add( portal2::shadow::remove_dirty_call );
		if( remove_dirty_call.at< std::uint8_t >( ) != 0xe8 )
			return false;
		m_remove_dirty = remove_dirty_call.rel32( 1 ).as< RemoveDirtyFn >( );

		// The private ConVar value controls model receiver projection even when
		// the shadow entry is unchanged, so it remains part of the cache key.
		const Address models_xref		= pattern::find_unique( client, portal2::signature::flashlight_models );
		m_flashlight_models_cvar_parent = models_xref.add( portal2::xref::flashlight_models_global ).at< Address >( ).as< int** >( );

		// Hook only the verified call from ComputeShadowDepthTextures. Calling the
		// engine selector first preserves every stock visibility, split-screen,
		// priority, and maximum-shadow rule before applying exact frustum culling.
		const Address active_call_xref = pattern::find_unique( client, portal2::signature::active_shadow_depth_call );
		const Address active_call	   = active_call_xref.add( portal2::xref::active_shadow_depth_call );
		const Address active_target	   = active_call.rel32( 1 );
		const Address expected_target  = pattern::find_unique( client, portal2::signature::active_shadow_depth_list );
		if( active_call.at< std::uint8_t >( ) != 0xe8 || active_target != expected_target )
			return false;

		const Address shadow_manager_xref = pattern::find_unique( client, portal2::signature::active_shadow_manager );
		m_shadow_manager_parent			  = shadow_manager_xref.add( portal2::xref::active_shadow_manager_global )
									  .at< Address >( )
									  .as< sdk::ShadowManager** >( );
		if( !m_remove_dirty || !m_flashlight_models_cvar_parent || !*m_flashlight_models_cvar_parent ||
			!m_shadow_manager_parent || !*m_shadow_manager_parent )
			return false;

		m_original_build_active_depth = active_target.as< BuildActiveDepthFn >( );
		std::vector< std::uint8_t > active_call_bytes( 5 );
		std::memcpy( active_call_bytes.data( ), active_call.as< const void* >( ), active_call_bytes.size( ) );

		m_metrics = metrics;
		m_states.reserve( 16 );
		m_instance = this;

		if( !m_update_projected.install( vmt + portal2::vmt::update_projected, reinterpret_cast< void* >( &UpdateProjected ) ) ) {
			m_instance = nullptr;
			m_metrics  = nullptr;
			return false;
		}
		if( !m_build_active_depth.install( active_call.as< void* >( ), active_call_bytes, reinterpret_cast< void* >( &BuildActiveDepthList ) ) ) {
			m_update_projected.remove( );
			m_instance					  = nullptr;
			m_metrics					  = nullptr;
			m_original_build_active_depth = nullptr;
			m_shadow_manager_parent		  = nullptr;
			return false;
		}

		return true;
	}

	void ProjectedShadowCache::remove( ) {
		m_build_active_depth.remove( );
		m_update_projected.remove( );
		invalidate( );
		m_metrics						= nullptr;
		m_flashlight_models_cvar_parent = nullptr;
		m_remove_dirty					= nullptr;
		m_original_build_active_depth	= nullptr;
		m_shadow_manager_parent			= nullptr;
		m_enabled.store( false, std::memory_order_relaxed );
		m_hits.store( 0, std::memory_order_relaxed );
		m_depth_culls.store( 0, std::memory_order_relaxed );

		if( m_instance == this )
			m_instance = nullptr;
	}

	int __fastcall ProjectedShadowCache::BuildActiveDepthList(
		void* self, void*, const void* view, int maximum, std::uint16_t* output, int* high_priority_count ) {
		ProjectedShadowCache* cache = m_instance;
		if( !cache || !cache->m_original_build_active_depth )
			return 0;

		// Preserve Portal 2's complete stock selection before removing only
		// projected volumes proven disjoint from this exact rendered view.
		const int selected = cache->m_original_build_active_depth( self, view, maximum, output, high_priority_count );
		if( !cache->m_enabled.load( std::memory_order_relaxed ) || selected <= 0 || !output || !high_priority_count )
			return selected;

		frustum_t camera{ };
		if( !camera_frustum( view, camera ) )
			return selected;

		auto*				entries		   = *reinterpret_cast< std::uint8_t** >( static_cast< std::uint8_t* >( self ) + portal2::shadow::entries );
		sdk::ShadowManager* shadow_manager = cache->m_shadow_manager_parent ? *cache->m_shadow_manager_parent : nullptr;
		if( !entries || !shadow_manager )
			return selected;

		int retained			   = 0;
		int retained_high_priority = *high_priority_count;
		for( int index = 0; index < selected; ++index ) {
			const std::uint16_t handle = output[ index ];
			const auto*			entry  = entries + static_cast< size_t >( handle ) * portal2::shadow::entry_size;

			matrix_t world_to_shadow{ };
			std::memcpy( &world_to_shadow, entry + portal2::shadow::world_to_shadow, sizeof( world_to_shadow ) );
			frustum_t projected{ };
			if( projected_frustum( world_to_shadow, projected ) && !frusta_intersect( camera, projected ) ) {
				// Removed high-priority lights must also release the cache slot that
				// Portal 2 reserved for them before this exact visibility test.
				unsigned short engine_handle{ };
				std::memcpy( &engine_handle, entry + portal2::shadow::engine_handle, sizeof( engine_handle ) );
				const auto* state = static_cast< const std::uint8_t* >( shadow_manager->GetFlashlightState( engine_handle ) );
				if( state && state[ portal2::shadow::high_priority ] != 0 && retained_high_priority > 0 )
					--retained_high_priority;

				cache->m_depth_culls.fetch_add( 1, std::memory_order_relaxed );
				continue;
			}

			output[ retained++ ] = handle;
		}

		// Portal 2 reserves depth-cache slots for high-priority lights. Recounting
		// after compaction prevents a culled light from consuming that capacity.
		*high_priority_count = retained_high_priority;
		return retained;
	}

	void ProjectedShadowCache::invalidate( ) noexcept {
		// A generation prevents a rebuild finishing during invalidation from
		// publishing receiver state that belongs to the previous scene lifetime.
		const std::lock_guard lock{ m_state_mutex };
		m_states.clear( );
		++m_generation;
	}

	void ProjectedShadowCache::set_enabled( bool enabled ) noexcept {
		// cached state must not cross a patched/unpatched A/B transition.
		if( m_enabled.exchange( enabled, std::memory_order_relaxed ) != enabled )
			invalidate( );
	}

	void __fastcall ProjectedShadowCache::UpdateProjected( void* self, void*, unsigned int handle, bool force ) {
		// recover the chain target before inspecting any reversed object fields.
		ProjectedShadowCache* cache = m_instance;
		if( !cache )
			return;

		auto original = reinterpret_cast< UpdateProjectedFn >( cache->m_update_projected.previous( ) );
		if( !original )
			return;

		// 0xffff is Source's invalid projected-texture handle.
		if( ( handle & 0xffffu ) == 0xffffu ) {
			original( self, handle, force );
			return;
		}

		// resolve the shadow record from the verified manager layout.
		auto* entries = *reinterpret_cast< std::uint8_t** >( static_cast< std::uint8_t* >( self ) + portal2::shadow::entries );
		if( !entries ) {
			original( self, handle, force );
			return;
		}

		const auto	index = static_cast< std::uint16_t >( handle & 0xffffu );
		const auto* entry = entries + static_cast< size_t >( index ) * portal2::shadow::entry_size;

		// Read the reversed `r_flashlightmodels` ConVar value used by this path.
		int* models_owner = *cache->m_flashlight_models_cvar_parent;
		int	 models_value = 0;
		if( models_owner ) {
			auto* model_bytes = reinterpret_cast< std::uint8_t* >( models_owner );
			models_value	  = *reinterpret_cast< int* >( model_bytes + portal2::shadow::flashlight_models );
		}
		const bool		collecting = cache->m_metrics && cache->m_metrics->collecting( );
		const Stopwatch timer{ collecting };
		std::uint64_t	generation{ };

		{
			// Invalidation and a cache hit must be one transaction. Otherwise an
			// entity-lifecycle callback could clear the cache after comparison but
			// before the engine receiver rebuild is skipped.
			const std::lock_guard lock{ cache->m_state_mutex };
			generation = cache->m_generation;

			auto prior = cache->m_states.end( );
			for( auto state = cache->m_states.begin( ); state != cache->m_states.end( ); ++state ) {
				if( state->m_handle == index ) {
					prior = state;
					break;
				}
			}

			const bool known_state = prior != cache->m_states.end( );
			const bool same_models = known_state && prior->m_flashlight_models_value == models_value;
			const bool same_entry  = known_state && std::memcmp( prior->m_entry.data( ), entry, prior->m_entry.size( ) ) == 0;

			// BSP/client-leaf receiver membership is not stored in ClientShadow_t.
			// Limiting reuse to one simulation generation keeps that hidden state
			// stable while still removing duplicate rebuilds at high render rates.
			if( cache->m_enabled.load( std::memory_order_relaxed ) && same_models && same_entry ) {
				cache->m_remove_dirty( self, handle );
				cache->m_hits.fetch_add( 1, std::memory_order_relaxed );
				if( collecting )
					cache->m_metrics->record_shadow_update( timer.elapsed_ms( ), true );
				return;
			}
		}

		// The first update in each generation always follows the vanilla path.
		original( self, handle, force );
		if( collecting )
			cache->m_metrics->record_shadow_update( timer.elapsed_ms( ), false );

		// The original call may run engine callbacks. Re-resolve the entry and
		// reject the result if any callback advanced the scene generation.
		const std::lock_guard lock{ cache->m_state_mutex };
		if( cache->m_generation != generation )
			return;

		entries = *reinterpret_cast< std::uint8_t** >( static_cast< std::uint8_t* >( self ) + portal2::shadow::entries );
		if( !entries )
			return;

		entry	   = entries + static_cast< size_t >( index ) * portal2::shadow::entry_size;
		auto prior = cache->m_states.end( );
		for( auto state = cache->m_states.begin( ); state != cache->m_states.end( ); ++state ) {
			if( state->m_handle == index ) {
				prior = state;
				break;
			}
		}

		if( prior == cache->m_states.end( ) ) {
			cache->m_states.push_back( { } );
			prior			= cache->m_states.end( ) - 1;
			prior->m_handle = index;
		}

		std::memcpy( prior->m_entry.data( ), entry, prior->m_entry.size( ) );
		prior->m_flashlight_models_value = models_value;
	}

}
