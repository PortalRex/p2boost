#include "renderer_research.h"

#ifdef P2BOOST_RESEARCH_DRAW_TRACE

#include "qpc.h"

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <iterator>
#include <vector>

namespace p2boost::renderer_research {
	namespace {

		constexpr std::size_t pointer_table_size		= 8192;
		constexpr std::size_t pipeline_table_size		= 16384;
		constexpr std::size_t render_state_count		= 256;
		constexpr std::size_t texture_stage_count		= 8;
		constexpr std::size_t texture_stage_state_count = 33;
		constexpr std::size_t sampler_count				= 16;
		constexpr std::size_t sampler_state_count		= 14;
		static_assert( ( pointer_table_size & ( pointer_table_size - 1 ) ) == 0 );
		static_assert( ( pipeline_table_size & ( pipeline_table_size - 1 ) ) == 0 );

		thread_local std::uint32_t g_dynamic_light_table[ pointer_table_size ]{ };
		thread_local std::uint32_t g_dynamic_surface_table[ pointer_table_size ]{ };
		thread_local std::uint32_t g_render_states[ render_state_count ]{ };
		thread_local std::uint32_t g_texture_stage_states[ texture_stage_count ][ texture_stage_state_count ]{ };
		thread_local std::uint32_t g_sampler_states[ sampler_count ][ sampler_state_count ]{ };
		thread_local PipelineState g_pipeline_state;
		thread_local unsigned int  g_driver_call_sequence{ };
		thread_local bool		   g_device_state_initialized{ };

		alignas( 64 ) std::atomic_uint32_t g_vertex_shader_keys[ pipeline_table_size ]{ };
		alignas( 64 ) std::atomic_uint32_t g_pixel_shader_keys[ pipeline_table_size ]{ };
		alignas( 64 ) std::atomic_uint32_t g_pipeline_keys[ pipeline_table_size ]{ };
		alignas( 64 ) std::atomic_uint32_t g_state_pipeline_keys[ pipeline_table_size ]{ };
		alignas( 64 ) std::atomic_uint32_t g_texture_keys[ pipeline_table_size ]{ };
		alignas( 64 ) std::atomic_uint32_t g_geometry_keys[ pipeline_table_size ]{ };
		alignas( 64 ) std::atomic_uint32_t g_draw_submission_keys[ pipeline_table_size ]{ };

		std::uint64_t hash_bytes( const std::uint8_t* bytes, std::size_t size ) noexcept {
			// FNV-1a gives stable research identities without storing bytecode.
			std::uint64_t hash = 14695981039346656037ull;
			for( std::size_t index = 0; index < size; ++index ) {
				hash ^= bytes[ index ];
				hash *= 1099511628211ull;
			}
			return hash;
		}

		template < typename Shader >
		std::uint64_t shader_hash( Shader* shader ) noexcept {
			if( !shader )
				return 0;
			UINT size = 0;
			if( FAILED( shader->GetFunction( nullptr, &size ) ) || size == 0 )
				return 0;
			std::vector< std::uint8_t > bytecode( size );
			if( FAILED( shader->GetFunction( bytecode.data( ), &size ) ) )
				return 0;
			return hash_bytes( bytecode.data( ), size );
		}

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
				if( current == 0 &&
					table[ slot ].compare_exchange_strong( current, key, std::memory_order_relaxed, std::memory_order_relaxed ) ) {
					return true;
				}
				slot = ( slot + 1 ) & ( pipeline_table_size - 1 );
			}
			return false;
		}

		std::uint32_t mix_word( std::uint32_t hash, std::uint32_t value ) noexcept {
			// FNV-1a over words is sufficient for classifying exact state reuse.
			for( unsigned int byte = 0; byte < sizeof( value ); ++byte ) {
				hash ^= value & 0xffu;
				hash *= 16777619u;
				value >>= 8;
			}
			return hash;
		}

		void initialize_device_state( IDirect3DDevice9* device ) noexcept {
			if( g_device_state_initialized || !device )
				return;

			// The plugin loads after D3D9 establishes its default state. Seed
			// the mirror once from the authoritative device, then let the
			// setter hooks maintain it without per-draw Get calls.
			for( std::size_t index = 0; index < render_state_count; ++index ) {
				DWORD value = 0;
				if( SUCCEEDED( device->GetRenderState( static_cast< D3DRENDERSTATETYPE >( index ), &value ) ) )
					g_render_states[ index ] = value;
			}

			for( DWORD stage = 0; stage < texture_stage_count; ++stage ) {
				for( DWORD state = 0; state < texture_stage_state_count; ++state ) {
					DWORD value = 0;
					if( SUCCEEDED( device->GetTextureStageState( stage, static_cast< D3DTEXTURESTAGESTATETYPE >( state ), &value ) ) ) {
						g_texture_stage_states[ stage ][ state ] = value;
					}
				}
			}

			for( DWORD sampler = 0; sampler < sampler_count; ++sampler ) {
				for( DWORD state = 0; state < sampler_state_count; ++state ) {
					DWORD value = 0;
					if( SUCCEEDED( device->GetSamplerState( sampler, static_cast< D3DSAMPLERSTATETYPE >( state ), &value ) ) )
						g_sampler_states[ sampler ][ state ] = value;
				}
			}

			for( DWORD index = 0; index < std::size( g_pipeline_state.m_render_targets ); ++index ) {
				IDirect3DSurface9* target{ };
				if( SUCCEEDED( device->GetRenderTarget( index, &target ) ) && target ) {
					g_pipeline_state.m_render_targets[ index ] =
						static_cast< std::uint32_t >( reinterpret_cast< std::uintptr_t >( target ) );
					target->Release( );
				}
			}

			IDirect3DSurface9* depth_stencil{ };
			if( SUCCEEDED( device->GetDepthStencilSurface( &depth_stencil ) ) && depth_stencil ) {
				g_pipeline_state.m_depth_stencil_surface =
					static_cast< std::uint32_t >( reinterpret_cast< std::uintptr_t >( depth_stencil ) );
				depth_stencil->Release( );
			}

			IDirect3DVertexDeclaration9* declaration{ };
			if( SUCCEEDED( device->GetVertexDeclaration( &declaration ) ) && declaration ) {
				g_pipeline_state.m_vertex_declaration = static_cast< std::uint32_t >( reinterpret_cast< std::uintptr_t >( declaration ) );
				declaration->Release( );
			}

			DWORD fvf = 0;
			if( SUCCEEDED( device->GetFVF( &fvf ) ) )
				g_pipeline_state.m_fvf = fvf;

			for( UINT stream = 0; stream < std::size( g_pipeline_state.m_vertex_buffers ); ++stream ) {
				IDirect3DVertexBuffer9* buffer{ };
				UINT					offset = 0;
				UINT					stride = 0;
				if( SUCCEEDED( device->GetStreamSource( stream, &buffer, &offset, &stride ) ) && buffer ) {
					g_pipeline_state.m_vertex_buffers[ stream ] =
						static_cast< std::uint32_t >( reinterpret_cast< std::uintptr_t >( buffer ) );
					buffer->Release( );
				}
			}

			IDirect3DIndexBuffer9* index_buffer{ };
			if( SUCCEEDED( device->GetIndices( &index_buffer ) ) && index_buffer ) {
				g_pipeline_state.m_index_buffer = static_cast< std::uint32_t >( reinterpret_cast< std::uintptr_t >( index_buffer ) );
				index_buffer->Release( );
			}

			g_device_state_initialized = true;
		}

		std::uint8_t* resolve_command_key(
			void* command, void* queued_functor_execute, void* queued_functor_leaf, void* queued_functor_one_argument ) noexcept {
			auto*  object  = static_cast< std::uint8_t* >( command );
			auto** vtable  = object ? *reinterpret_cast< void*** >( object ) : nullptr;
			auto*  execute = vtable ? reinterpret_cast< std::uint8_t* >( vtable[ 3 ] ) : nullptr;
			auto*  key	   = execute;

			// A one-argument functor stores its concrete callback directly at +0x10.
			if( execute == queued_functor_one_argument ) {
				key = *reinterpret_cast< std::uint8_t** >( object + 0x10 );
			}
			// The generic functor adds another object layer around the same layout.
			else if( execute == queued_functor_execute ) {
				key = *reinterpret_cast< std::uint8_t** >( object + 0x10 );
				if( key == queued_functor_leaf || key == queued_functor_one_argument ) {
					auto* target = *reinterpret_cast< std::uint8_t** >( object + 0x14 );
					if( target ) {
						auto* leaf = *reinterpret_cast< std::uint8_t** >( target + 0x10 );
						if( leaf )
							key = leaf;
					}
				}
			}
			return key;
		}

		void describe_texture_0( RenderWorkerMetrics& metrics ) noexcept {
			// inspect only the texture attached to the slowest draw in this worker.
			auto* texture = reinterpret_cast< IDirect3DBaseTexture9* >( static_cast< std::uintptr_t >( metrics.m_max_draw_textures[ 0 ] ) );
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
		g_driver_call_sequence = 0;
	}

	void count_queued_command( RenderWorkerMetrics& metrics,
		std::uint8_t*								node,
		void*										queued_functor_execute,
		void*										queued_functor_leaf,
		void*										queued_functor_one_argument ) noexcept {
		// Queued nodes store the command object immediately after their link.
		auto*	   object = node + sizeof( void* );
		const auto key32  = queued_command_key( object, queued_functor_execute, queued_functor_leaf, queued_functor_one_argument );

		// retain a compact per-worker histogram without heap allocation.
		std::size_t slot = 0;
		while(
			slot < metrics.m_command_counts.size( ) && metrics.m_command_counts[ slot ] != 0 && metrics.m_command_keys[ slot ] != key32 ) {
			++slot;
		}
		if( slot < metrics.m_command_counts.size( ) ) {
			metrics.m_command_keys[ slot ] = key32;
			++metrics.m_command_counts[ slot ];
		}
	}

	unsigned int queued_command_key(
		void* object, void* queued_functor_execute, void* queued_functor_leaf, void* queued_functor_one_argument ) noexcept {
		// Resolve before Execute: a command remains alive, but Execute may mutate
		// the wrapper fields used to locate a queued functor's concrete callback.
		auto* key = resolve_command_key( object, queued_functor_execute, queued_functor_leaf, queued_functor_one_argument );
		return static_cast< unsigned int >( reinterpret_cast< std::uintptr_t >( key ) );
	}

	void record_queued_command( RenderWorkerMetrics& metrics, unsigned int key, Qpc::tick_t elapsed ) noexcept {
		++metrics.m_command_execute_count;
		metrics.m_command_execute_qpc += elapsed;
		for( std::size_t slot = 0; slot < metrics.m_command_keys.size( ); ++slot ) {
			if( metrics.m_command_counts[ slot ] != 0 && metrics.m_command_keys[ slot ] == key ) {
				metrics.m_command_qpc[ slot ] += elapsed;
				break;
			}
		}
		if( elapsed > metrics.m_slow_command_qpc ) {
			metrics.m_slow_command_qpc = elapsed;
			metrics.m_slow_command_key = key;
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
			g_pipeline_state.m_textures[ stage ] = static_cast< std::uint32_t >( reinterpret_cast< std::uintptr_t >( texture ) );
		}
	}

	void set_render_target( DWORD index, IDirect3DSurface9* target ) noexcept {
		if( index < std::size( g_pipeline_state.m_render_targets ) ) {
			g_pipeline_state.m_render_targets[ index ] = static_cast< std::uint32_t >( reinterpret_cast< std::uintptr_t >( target ) );
		}
	}

	void set_depth_stencil_surface( IDirect3DSurface9* surface ) noexcept {
		g_pipeline_state.m_depth_stencil_surface = static_cast< std::uint32_t >( reinterpret_cast< std::uintptr_t >( surface ) );
	}

	void set_render_state( D3DRENDERSTATETYPE state, DWORD value ) noexcept {
		const auto index = static_cast< std::size_t >( state );
		if( index < render_state_count )
			g_render_states[ index ] = value;
	}

	void set_texture_stage_state( DWORD stage, D3DTEXTURESTAGESTATETYPE state, DWORD value ) noexcept {
		const auto state_index = static_cast< std::size_t >( state );
		if( stage < texture_stage_count && state_index < texture_stage_state_count )
			g_texture_stage_states[ stage ][ state_index ] = value;
	}

	void set_sampler_state( DWORD sampler, D3DSAMPLERSTATETYPE state, DWORD value ) noexcept {
		const auto state_index = static_cast< std::size_t >( state );
		if( sampler < sampler_count && state_index < sampler_state_count )
			g_sampler_states[ sampler ][ state_index ] = value;
	}

	void set_vertex_declaration( IDirect3DVertexDeclaration9* declaration ) noexcept {
		g_pipeline_state.m_vertex_declaration = static_cast< std::uint32_t >( reinterpret_cast< std::uintptr_t >( declaration ) );
		g_pipeline_state.m_fvf				  = 0;
	}

	void set_fvf( DWORD fvf ) noexcept {
		g_pipeline_state.m_vertex_declaration = 0;
		g_pipeline_state.m_fvf				  = fvf;
	}

	void set_stream_source( UINT stream, IDirect3DVertexBuffer9* buffer ) noexcept {
		if( stream < std::size( g_pipeline_state.m_vertex_buffers ) ) {
			g_pipeline_state.m_vertex_buffers[ stream ] = static_cast< std::uint32_t >( reinterpret_cast< std::uintptr_t >( buffer ) );
		}
	}

	void set_indices( IDirect3DIndexBuffer9* buffer ) noexcept {
		g_pipeline_state.m_index_buffer = static_cast< std::uint32_t >( reinterpret_cast< std::uintptr_t >( buffer ) );
	}

	void set_vertex_shader( IDirect3DVertexShader9* shader ) noexcept {
		g_pipeline_state.m_vertex_shader = static_cast< std::uint32_t >( reinterpret_cast< std::uintptr_t >( shader ) );
	}

	void set_pixel_shader( IDirect3DPixelShader9* shader ) noexcept {
		g_pipeline_state.m_pixel_shader = static_cast< std::uint32_t >( reinterpret_cast< std::uintptr_t >( shader ) );
	}

	PipelineObservation observe_pipeline( IDirect3DDevice9* device ) noexcept {
		// classify first-seen resources without retaining COM references.
		initialize_device_state( device );
		PipelineObservation observation{ };
		observation.m_state = g_pipeline_state;

		// D3D9 drivers may specialize the same shader pair for blend, depth,
		// stencil, fog, and vertex-layout state. Include every observed render
		// state so a slow first combination is not mislabeled as a repeat.
		auto state_hash = 2166136261u;
		for( const auto value : g_render_states )
			state_hash = mix_word( state_hash, value );
		observation.m_state.m_render_state_hash = state_hash;

		auto texture_stage_state_hash = 2166136261u;
		for( const auto& stage : g_texture_stage_states ) {
			for( const auto value : stage )
				texture_stage_state_hash = mix_word( texture_stage_state_hash, value );
		}
		observation.m_state.m_texture_stage_state_hash = texture_stage_state_hash;

		auto sampler_state_hash = 2166136261u;
		for( const auto& sampler : g_sampler_states ) {
			for( const auto value : sampler )
				sampler_state_hash = mix_word( sampler_state_hash, value );
		}
		observation.m_state.m_sampler_state_hash = sampler_state_hash;
		observation.m_first_vertex_shader		 = insert_process_key( observation.m_state.m_vertex_shader, g_vertex_shader_keys );
		observation.m_first_pixel_shader		 = insert_process_key( observation.m_state.m_pixel_shader, g_pixel_shader_keys );

		// combine both shader identities into one stable pipeline key.
		auto pair_key = observation.m_state.m_vertex_shader * 0x9e3779b9u;
		pair_key ^= observation.m_state.m_pixel_shader + 0x85ebca6bu + ( pair_key << 6 ) + ( pair_key >> 2 );
		if( !pair_key && ( observation.m_state.m_vertex_shader || observation.m_state.m_pixel_shader ) ) {
			pair_key = 1;
		}
		observation.m_first_pipeline = insert_process_key( pair_key, g_pipeline_keys );

		auto state_pipeline_key = mix_word( state_hash, observation.m_state.m_vertex_shader );
		state_pipeline_key		= mix_word( state_pipeline_key, observation.m_state.m_pixel_shader );
		state_pipeline_key		= mix_word( state_pipeline_key, observation.m_state.m_vertex_declaration );
		state_pipeline_key		= mix_word( state_pipeline_key, observation.m_state.m_fvf );
		if( !state_pipeline_key )
			state_pipeline_key = 1;
		observation.m_first_state_pipeline = insert_process_key( state_pipeline_key, g_state_pipeline_keys );

		for( std::size_t stage = 0; stage < 4; ++stage ) {
			if( insert_process_key( observation.m_state.m_textures[ stage ], g_texture_keys ) ) {
				observation.m_first_texture_mask |= static_cast< std::uint8_t >( 1u << stage );
			}
		}
		for( std::size_t stream = 0; stream < std::size( observation.m_state.m_vertex_buffers ); ++stream ) {
			if( insert_process_key( observation.m_state.m_vertex_buffers[ stream ], g_geometry_keys ) )
				observation.m_first_vertex_buffer_mask |= static_cast< std::uint8_t >( 1u << stream );
		}
		observation.m_first_index_buffer = insert_process_key( observation.m_state.m_index_buffer, g_geometry_keys );

		// Track the complete driver-visible draw state as one key. This catches
		// lazy specialization that only occurs for an exact resource combination.
		auto draw_submission_key = state_pipeline_key;
		draw_submission_key		 = mix_word( draw_submission_key, texture_stage_state_hash );
		draw_submission_key		 = mix_word( draw_submission_key, sampler_state_hash );
		for( const auto texture : observation.m_state.m_textures )
			draw_submission_key = mix_word( draw_submission_key, texture );
		for( const auto render_target : observation.m_state.m_render_targets )
			draw_submission_key = mix_word( draw_submission_key, render_target );
		draw_submission_key = mix_word( draw_submission_key, observation.m_state.m_depth_stencil_surface );
		for( const auto vertex_buffer : observation.m_state.m_vertex_buffers )
			draw_submission_key = mix_word( draw_submission_key, vertex_buffer );
		draw_submission_key = mix_word( draw_submission_key, observation.m_state.m_index_buffer );
		if( !draw_submission_key )
			draw_submission_key = 1;
		observation.m_first_draw_submission = insert_process_key( draw_submission_key, g_draw_submission_keys );
		return observation;
	}

	unsigned int begin_driver_call( ) noexcept {
		// Sequence numbers preserve the call order after the slowest events are
		// selected. Zero remains the sentinel for an unused CSV entry.
		return ++g_driver_call_sequence;
	}

	void record_driver_call( RenderWorkerMetrics& metrics, unsigned int method, unsigned int sequence, Qpc::tick_t begin_qpc ) noexcept {
		const Qpc::tick_t elapsed = Qpc::now( ) - begin_qpc;
		++metrics.m_driver_call_count;

		// Keep only the twelve slowest calls, ordered by duration. Their original
		// sequence values reconstruct chronology without logging every draw.
		size_t slot = 0;
		while( slot < metrics.m_driver_event_qpc.size( ) && elapsed <= metrics.m_driver_event_qpc[ slot ] )
			++slot;
		if( slot == metrics.m_driver_event_qpc.size( ) )
			return;
		for( size_t move = metrics.m_driver_event_qpc.size( ) - 1; move > slot; --move ) {
			metrics.m_driver_event_methods[ move ]	 = metrics.m_driver_event_methods[ move - 1 ];
			metrics.m_driver_event_sequences[ move ] = metrics.m_driver_event_sequences[ move - 1 ];
			metrics.m_driver_event_qpc[ move ]		 = metrics.m_driver_event_qpc[ move - 1 ];
		}
		metrics.m_driver_event_methods[ slot ]	 = method;
		metrics.m_driver_event_sequences[ slot ] = sequence;
		metrics.m_driver_event_qpc[ slot ]		 = elapsed;
	}

	void record_draw( RenderWorkerMetrics& metrics,
		IDirect3DDevice9*				   device,
		Qpc::tick_t						   begin_qpc,
		unsigned int					   sequence,
		unsigned int					   method,
		unsigned int					   primitive_type,
		unsigned int					   primitive_count,
		unsigned int					   vertex_count,
		const PipelineObservation&		   pipeline ) noexcept {
		// retain detailed state only when this becomes the slowest draw.
		const Qpc::tick_t elapsed = Qpc::now( ) - begin_qpc;
		record_driver_call( metrics, method, sequence, begin_qpc );
		++metrics.m_draw_count;
		metrics.m_draw_qpc += elapsed;

		// Separate lazy first-use work from ordinary steady-state draws. A
		// resource can be new even when its shader pair was already observed.
		const bool first_pipeline = pipeline.m_first_vertex_shader || pipeline.m_first_pixel_shader || pipeline.m_first_pipeline;
		const bool first_texture  = pipeline.m_first_texture_mask != 0;
		if( first_pipeline ) {
			++metrics.m_first_pipeline_draw_count;
			metrics.m_first_pipeline_draw_qpc += elapsed;
		}
		if( pipeline.m_first_state_pipeline ) {
			++metrics.m_first_state_pipeline_draw_count;
			metrics.m_first_state_pipeline_draw_qpc += elapsed;
		}
		if( pipeline.m_first_vertex_buffer_mask || pipeline.m_first_index_buffer ) {
			++metrics.m_first_geometry_draw_count;
			metrics.m_first_geometry_draw_qpc += elapsed;
		}
		if( pipeline.m_first_draw_submission ) {
			++metrics.m_first_draw_submission_count;
			metrics.m_first_draw_submission_qpc += elapsed;
		}
		if( first_pipeline || first_texture ) {
			++metrics.m_first_resource_draw_count;
			metrics.m_first_resource_draw_qpc += elapsed;
		}
		for( auto mask = pipeline.m_first_texture_mask; mask != 0; mask >>= 1 )
			metrics.m_first_texture_count += mask & 1u;
		for( auto mask = pipeline.m_first_vertex_buffer_mask; mask != 0; mask >>= 1 )
			metrics.m_first_vertex_buffer_count += mask & 1u;
		if( pipeline.m_first_index_buffer )
			++metrics.m_first_index_buffer_count;
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
		std::copy_n( pipeline.m_state.m_textures, metrics.m_max_draw_textures.size( ), metrics.m_max_draw_textures.begin( ) );

		// Some ShaderAPI paths restore state without passing through the public
		// setters. Query only a newly slowest draw so the trace records the
		// driver's authoritative objects without burdening every submission.
		if( device ) {
			IDirect3DVertexShader9* vertex_shader{ };
			if( SUCCEEDED( device->GetVertexShader( &vertex_shader ) ) && vertex_shader ) {
				metrics.m_max_draw_vertex_shader	  = static_cast< std::uint32_t >( reinterpret_cast< std::uintptr_t >( vertex_shader ) );
				metrics.m_max_draw_vertex_shader_hash = shader_hash( vertex_shader );
				vertex_shader->Release( );
			}

			IDirect3DPixelShader9* pixel_shader{ };
			if( SUCCEEDED( device->GetPixelShader( &pixel_shader ) ) && pixel_shader ) {
				metrics.m_max_draw_pixel_shader		 = static_cast< std::uint32_t >( reinterpret_cast< std::uintptr_t >( pixel_shader ) );
				metrics.m_max_draw_pixel_shader_hash = shader_hash( pixel_shader );
				pixel_shader->Release( );
			}

			for( DWORD stage = 0; stage < metrics.m_max_draw_textures.size( ); ++stage ) {
				IDirect3DBaseTexture9* texture{ };
				if( SUCCEEDED( device->GetTexture( stage, &texture ) ) && texture ) {
					metrics.m_max_draw_textures[ stage ] = static_cast< std::uint32_t >( reinterpret_cast< std::uintptr_t >( texture ) );
					texture->Release( );
				}
			}
		}
		metrics.m_max_draw_first_vertex_shader		= pipeline.m_first_vertex_shader;
		metrics.m_max_draw_first_pixel_shader		= pipeline.m_first_pixel_shader;
		metrics.m_max_draw_first_pipeline			= pipeline.m_first_pipeline;
		metrics.m_max_draw_first_state_pipeline		= pipeline.m_first_state_pipeline;
		metrics.m_max_draw_first_submission			= pipeline.m_first_draw_submission;
		metrics.m_max_draw_first_texture_mask		= pipeline.m_first_texture_mask;
		metrics.m_max_draw_first_vertex_buffer_mask = pipeline.m_first_vertex_buffer_mask;
		metrics.m_max_draw_first_index_buffer		= pipeline.m_first_index_buffer;
		metrics.m_max_draw_render_state_hash		= pipeline.m_state.m_render_state_hash;
		metrics.m_max_draw_vertex_declaration		= pipeline.m_state.m_vertex_declaration;
		metrics.m_max_draw_fvf						= pipeline.m_state.m_fvf;
		std::copy_n( pipeline.m_state.m_vertex_buffers,
			metrics.m_max_draw_vertex_buffers.size( ),
			metrics.m_max_draw_vertex_buffers.begin( ) );
		metrics.m_max_draw_index_buffer = pipeline.m_state.m_index_buffer;
		describe_texture_0( metrics );

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
