#include "renderer_research.h"

#ifdef P2BOOST_RESEARCH_DRAW_TRACE

#include "qpc.h"

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstring>

namespace p2boost::renderer_research {
	namespace {

		constexpr std::size_t pointer_table_size  = 8192;
		constexpr std::size_t pipeline_table_size = 16384;
		static_assert( ( pointer_table_size & ( pointer_table_size - 1 ) ) == 0 );
		static_assert( ( pipeline_table_size & ( pipeline_table_size - 1 ) ) == 0 );

		thread_local std::uint32_t g_dynamic_light_table[ pointer_table_size ]{ };
		thread_local std::uint32_t g_dynamic_surface_table[ pointer_table_size ]{ };
		thread_local PipelineState g_pipeline_state;

		alignas( 64 ) std::atomic_uint32_t g_vertex_shader_keys[ pipeline_table_size ]{ };
		alignas( 64 ) std::atomic_uint32_t g_pixel_shader_keys[ pipeline_table_size ]{ };
		alignas( 64 ) std::atomic_uint32_t g_pipeline_keys[ pipeline_table_size ]{ };
		alignas( 64 ) std::atomic_uint32_t g_texture_keys[ pipeline_table_size ]{ };

		bool insert_thread_key( std::uint32_t value, std::uint32_t* table ) noexcept {
			// multiplicative hashing spreads aligned pointer values across the table.
			if( !value ) {
				return false;
			}
			std::size_t slot = ( value * 2654435761u ) & ( pointer_table_size - 1 );
			for( std::size_t probe = 0; probe < pointer_table_size; ++probe ) {
				// linear probing keeps this hook allocation-free.
				if( table[ slot ] == value ) {
					return false;
				}
				if( table[ slot ] == 0 ) {
					table[ slot ] = value;
					return true;
				}
				slot = ( slot + 1 ) & ( pointer_table_size - 1 );
			}
			return false;
		}

		bool insert_process_key( std::uint32_t key, std::atomic_uint32_t* table ) noexcept {
			// relaxed CAS is sufficient because entries only transition from zero.
			if( !key ) {
				return false;
			}
			std::size_t slot = ( key * 2654435761u ) & ( pipeline_table_size - 1 );
			for( std::size_t probe = 0; probe < pipeline_table_size; ++probe ) {
				auto current = table[ slot ].load( std::memory_order_relaxed );
				if( current == key ) {
					return false;
				}
				if( current == 0 && table[ slot ].compare_exchange_strong( current,
										key,
										std::memory_order_relaxed,
										std::memory_order_relaxed ) ) {
					return true;
				}
				slot = ( slot + 1 ) & ( pipeline_table_size - 1 );
			}
			return false;
		}

		void describe_texture_0( RenderWorkerMetrics& metrics, const PipelineObservation& pipeline ) noexcept {
			// inspect only the texture attached to the slowest draw in this worker.
			auto* texture = reinterpret_cast< IDirect3DBaseTexture9* >(
				static_cast< std::uintptr_t >( pipeline.m_state.m_textures[ 0 ] ) );
			if( !texture ) {
				return;
			}

			const auto type					   = texture->GetType( );
			metrics.m_max_draw_texture0_type   = static_cast< std::uint32_t >( type );
			metrics.m_max_draw_texture0_levels = texture->GetLevelCount( );

			D3DSURFACE_DESC description{ };
			HRESULT			result = D3DERR_INVALIDCALL;
			if( type == D3DRTYPE_TEXTURE ) {
				result = static_cast< IDirect3DTexture9* >( texture )->GetLevelDesc( 0, &description );
			} else if( type == D3DRTYPE_CUBETEXTURE ) {
				result = static_cast< IDirect3DCubeTexture9* >( texture )->GetLevelDesc( 0, &description );
			}

			if( SUCCEEDED( result ) ) {
				metrics.m_max_draw_texture0_format = static_cast< std::uint32_t >( description.Format );
				metrics.m_max_draw_texture0_width  = description.Width;
				metrics.m_max_draw_texture0_height = description.Height;
				metrics.m_max_draw_texture0_usage  = description.Usage;
				metrics.m_max_draw_texture0_pool   = static_cast< std::uint32_t >( description.Pool );

				// Hashing is intentionally research-only. LockRect can itself block on
				// a managed texture, so its cost must never enter a shipping build.
				if( ( pipeline.m_first_texture_mask & 1 ) != 0 && type == D3DRTYPE_TEXTURE &&
					description.Format == D3DFMT_DXT5 ) {
					D3DLOCKED_RECT locked{ };
					auto*		   texture2d = static_cast< IDirect3DTexture9* >( texture );
					if( SUCCEEDED( texture2d->LockRect( 0, &locked, nullptr, D3DLOCK_READONLY ) ) ) {
						std::uint64_t hash		= 1469598103934665603ull;
						const auto	  rows		= ( description.Height + 3u ) / 4u;
						const auto	  row_bytes = ( ( description.Width + 3u ) / 4u ) * 16u;
						const auto*	  row		= static_cast< const std::uint8_t* >( locked.pBits );
						for( UINT y = 0; y < rows; ++y ) {
							for( UINT x = 0; x < row_bytes; ++x ) {
								hash ^= row[ x ];
								hash *= 1099511628211ull;
							}
							row += locked.Pitch;
						}
						metrics.m_max_draw_texture0_hash = hash;
						texture2d->UnlockRect( 0 );
					}
				}
				return;
			}

			if( type == D3DRTYPE_VOLUMETEXTURE ) {
				D3DVOLUME_DESC volume{ };
				if( SUCCEEDED( static_cast< IDirect3DVolumeTexture9* >( texture )->GetLevelDesc( 0, &volume ) ) ) {
					metrics.m_max_draw_texture0_format = static_cast< std::uint32_t >( volume.Format );
					metrics.m_max_draw_texture0_width  = volume.Width;
					metrics.m_max_draw_texture0_height = volume.Height;
					metrics.m_max_draw_texture0_usage  = volume.Usage;
					metrics.m_max_draw_texture0_pool   = static_cast< std::uint32_t >( volume.Pool );
				}
			}
		}

	}

	void reset_worker_state( ) noexcept {
		// uniqueness is scoped to a single queued render-worker execution.
		std::memset( g_dynamic_light_table, 0, sizeof( g_dynamic_light_table ) );
		std::memset( g_dynamic_surface_table, 0, sizeof( g_dynamic_surface_table ) );
	}

	void count_queued_command( RenderWorkerMetrics& metrics,
		std::uint8_t*								node,
		void*										queued_functor_execute,
		void*										queued_functor_leaf ) noexcept {
		// queued nodes store the command object immediately after their link.
		auto* object = node + sizeof( void* );
		// the command vtable exposes its execute function in slot three.
		auto** vtable  = *reinterpret_cast< void*** >( object );
		auto*  execute = vtable ? reinterpret_cast< std::uint8_t* >( vtable[ 3 ] ) : nullptr;
		auto*  key	   = execute;

		// Unwrap queued functors so the histogram identifies their real target.
		if( execute == queued_functor_execute ) {
			key = *reinterpret_cast< std::uint8_t** >( object + 0x10 );
			if( key == queued_functor_leaf ) {
				auto* target = *reinterpret_cast< std::uint8_t** >( object + 0x14 );
				if( target ) {
					auto* leaf = *reinterpret_cast< std::uint8_t** >( target + 0x10 );
					if( leaf ) {
						key = leaf;
					}
				}
			}
		}

		// retain a compact per-worker histogram without heap allocation.
		const auto	key32 = static_cast< unsigned int >( reinterpret_cast< std::uintptr_t >( key ) );
		std::size_t slot  = 0;
		while( slot < metrics.m_command_counts.size( ) && metrics.m_command_counts[ slot ] != 0 &&
			   metrics.m_command_keys[ slot ] != key32 ) {
			++slot;
		}
		if( slot < metrics.m_command_counts.size( ) ) {
			metrics.m_command_keys[ slot ] = key32;
			++metrics.m_command_counts[ slot ];
		}
	}

	void capture_dynamic_light( RenderWorkerMetrics& metrics, const void* command ) noexcept {
		// recovered command fields identify the light and affected surface.
		const auto* bytes	= static_cast< const std::uint8_t* >( command );
		const auto	light	= *reinterpret_cast< const std::uint32_t* >( bytes + 0x14 );
		const auto	surface = *reinterpret_cast< const std::uint32_t* >( bytes + 0x18 );
		if( insert_thread_key( light, g_dynamic_light_table ) ) {
			++metrics.m_dynamic_light_unique_lights;
		}
		if( insert_thread_key( surface, g_dynamic_surface_table ) ) {
			++metrics.m_dynamic_light_unique_surfaces;
		}
		// preserve only one representative payload for offline inspection.
		if( metrics.m_dynamic_light_count != 0 ) {
			return;
		}

		metrics.m_dynamic_light_first_light	  = light;
		metrics.m_dynamic_light_first_surface = surface;
		metrics.m_dynamic_light_first_flags	  = *reinterpret_cast< const std::uint32_t* >( bytes + 0x4c );
		metrics.m_dynamic_light_first_bool_a  = *( bytes + 0x50 );
		metrics.m_dynamic_light_first_bool_b  = *( bytes + 0x51 );
		if( light ) {
			std::memcpy( metrics.m_dynamic_light_words.data( ),
				reinterpret_cast< const void* >( static_cast< std::uintptr_t >( light ) ),
				sizeof( metrics.m_dynamic_light_words ) );
		}
	}

	void set_texture( DWORD stage, IDirect3DBaseTexture9* texture ) noexcept {
		if( stage < 4 ) {
			g_pipeline_state.m_textures[ stage ] =
				static_cast< std::uint32_t >( reinterpret_cast< std::uintptr_t >( texture ) );
		}
	}

	void set_vertex_shader( IDirect3DVertexShader9* shader ) noexcept {
		g_pipeline_state.m_vertex_shader = static_cast< std::uint32_t >( reinterpret_cast< std::uintptr_t >( shader ) );
	}

	void set_pixel_shader( IDirect3DPixelShader9* shader ) noexcept {
		g_pipeline_state.m_pixel_shader = static_cast< std::uint32_t >( reinterpret_cast< std::uintptr_t >( shader ) );
	}

	PipelineObservation observe_pipeline( ) noexcept {
		// classify first-seen resources without retaining COM references.
		PipelineObservation observation{ };
		observation.m_state = g_pipeline_state;
		observation.m_first_vertex_shader =
			insert_process_key( observation.m_state.m_vertex_shader, g_vertex_shader_keys );
		observation.m_first_pixel_shader =
			insert_process_key( observation.m_state.m_pixel_shader, g_pixel_shader_keys );

		// combine both shader identities into one stable pipeline key.
		auto pair_key = observation.m_state.m_vertex_shader * 0x9e3779b9u;
		pair_key ^= observation.m_state.m_pixel_shader + 0x85ebca6bu + ( pair_key << 6 ) + ( pair_key >> 2 );
		if( !pair_key && ( observation.m_state.m_vertex_shader || observation.m_state.m_pixel_shader ) ) {
			pair_key = 1;
		}
		observation.m_first_pipeline = insert_process_key( pair_key, g_pipeline_keys );
		for( std::size_t stage = 0; stage < 4; ++stage ) {
			if( insert_process_key( observation.m_state.m_textures[ stage ], g_texture_keys ) ) {
				observation.m_first_texture_mask |= static_cast< std::uint8_t >( 1u << stage );
			}
		}
		return observation;
	}

	void record_draw( RenderWorkerMetrics& metrics,
		Qpc::tick_t						   begin_qpc,
		unsigned int					   method,
		unsigned int					   primitive_type,
		unsigned int					   primitive_count,
		unsigned int					   vertex_count,
		const PipelineObservation&		   pipeline ) noexcept {
		// retain detailed state only when this becomes the slowest draw.
		const Qpc::tick_t elapsed = Qpc::now( ) - begin_qpc;
		++metrics.m_draw_count;
		metrics.m_draw_qpc += elapsed;
		if( elapsed <= metrics.m_max_draw_qpc ) {
			return;
		}

		metrics.m_max_draw_qpc			   = elapsed;
		metrics.m_max_draw_method		   = method;
		metrics.m_max_draw_primitive_type  = primitive_type;
		metrics.m_max_draw_primitive_count = primitive_count;
		metrics.m_max_draw_vertex_count	   = vertex_count;
		metrics.m_max_draw_vertex_shader   = pipeline.m_state.m_vertex_shader;
		metrics.m_max_draw_pixel_shader	   = pipeline.m_state.m_pixel_shader;
		std::copy_n( pipeline.m_state.m_textures,
			metrics.m_max_draw_textures.size( ),
			metrics.m_max_draw_textures.begin( ) );
		metrics.m_max_draw_first_vertex_shader = pipeline.m_first_vertex_shader;
		metrics.m_max_draw_first_pixel_shader  = pipeline.m_first_pixel_shader;
		metrics.m_max_draw_first_pipeline	   = pipeline.m_first_pipeline;
		metrics.m_max_draw_first_texture_mask  = pipeline.m_first_texture_mask;
		describe_texture_0( metrics, pipeline );

		// stack capture is limited to material stalls above two milliseconds.
		static const Qpc::tick_t capture_threshold = Qpc::ticks( 2.0 );
		if( elapsed < capture_threshold ) {
			metrics.m_max_draw_stack_depth = 0;
			return;
		}

		void* stack[ 16 ]{ };
		metrics.m_max_draw_stack_depth = RtlCaptureStackBackTrace( 1, 16, stack, nullptr );
		for( unsigned short index = 0; index < metrics.m_max_draw_stack_depth; ++index ) {
			metrics.m_max_draw_stack[ index ] = reinterpret_cast< std::uintptr_t >( stack[ index ] );
		}
	}

}

#endif
