#pragma once

#ifdef P2BOOST_RESEARCH_DRAW_TRACE

#include "metrics.h"

#include <d3d9.h>

#include <cstdint>

namespace p2boost::renderer_research {

	struct PipelineState {
		std::uint32_t m_vertex_shader{ };
		std::uint32_t m_pixel_shader{ };
		std::uint32_t m_textures[ 4 ]{ };
		std::uint32_t m_render_targets[ 4 ]{ };
		std::uint32_t m_depth_stencil_surface{ };
		std::uint32_t m_render_state_hash{ };
		std::uint32_t m_texture_stage_state_hash{ };
		std::uint32_t m_sampler_state_hash{ };
		std::uint32_t m_vertex_declaration{ };
		std::uint32_t m_fvf{ };
		std::uint32_t m_vertex_buffers[ 4 ]{ };
		std::uint32_t m_vertex_offsets[ 4 ]{ };
		std::uint32_t m_vertex_strides[ 4 ]{ };
		std::uint32_t m_vertex_frequencies[ 4 ]{ };
		std::uint32_t m_index_buffer{ };
	};

	struct PipelineObservation {
		PipelineState m_state{ };
		bool		  m_first_vertex_shader{ };
		bool		  m_first_pixel_shader{ };
		bool		  m_first_pipeline{ };
		bool		  m_first_state_pipeline{ };
		bool		  m_first_draw_submission{ };
		std::uint32_t m_draw_submission_key{ };
		std::uint32_t m_draw_arguments[ 6 ]{ };
		std::uint8_t  m_first_texture_mask{ };
		std::uint8_t  m_first_vertex_buffer_mask{ };
		bool		  m_first_index_buffer{ };
	};

	void		 reset_worker_state( ) noexcept;
	unsigned int queued_command_key(
		void* object, void* queued_functor_execute, void* queued_functor_leaf, void* queued_functor_one_argument ) noexcept;
	void count_queued_command( RenderWorkerMetrics& metrics,
		std::uint8_t*								node,
		void*										queued_functor_execute,
		void*										queued_functor_leaf,
		void*										queued_functor_one_argument ) noexcept;
	void record_queued_command( RenderWorkerMetrics& metrics, unsigned int key, Qpc::tick_t elapsed ) noexcept;
	void capture_dynamic_light( RenderWorkerMetrics& metrics, const void* command ) noexcept;

	void							  set_texture( DWORD stage, IDirect3DBaseTexture9* texture ) noexcept;
	void							  set_render_target( DWORD index, IDirect3DSurface9* target ) noexcept;
	void							  set_depth_stencil_surface( IDirect3DSurface9* surface ) noexcept;
	void							  set_render_state( D3DRENDERSTATETYPE state, DWORD value ) noexcept;
	void							  set_texture_stage_state( DWORD stage, D3DTEXTURESTAGESTATETYPE state, DWORD value ) noexcept;
	void							  set_sampler_state( DWORD sampler, D3DSAMPLERSTATETYPE state, DWORD value ) noexcept;
	void							  set_vertex_declaration( IDirect3DVertexDeclaration9* declaration ) noexcept;
	void							  set_fvf( DWORD fvf ) noexcept;
	void							  set_stream_source( UINT stream, IDirect3DVertexBuffer9* buffer, UINT offset, UINT stride ) noexcept;
	void							  set_stream_source_frequency( UINT stream, UINT setting ) noexcept;
	void							  set_indices( IDirect3DIndexBuffer9* buffer ) noexcept;
	void							  set_vertex_shader( IDirect3DVertexShader9* shader ) noexcept;
	void							  set_pixel_shader( IDirect3DPixelShader9* shader ) noexcept;
	[[nodiscard]] PipelineObservation observe_pipeline( IDirect3DDevice9* device ) noexcept;
	void							  classify_draw_submission( PipelineObservation& pipeline,
									 unsigned int									 method,
									 unsigned int									 argument_0,
									 unsigned int									 argument_1,
									 unsigned int									 argument_2,
									 unsigned int									 argument_3,
									 unsigned int									 argument_4,
									 unsigned int									 argument_5 ) noexcept;
	[[nodiscard]] unsigned int		  begin_driver_call( ) noexcept;
	void record_driver_call( RenderWorkerMetrics& metrics, unsigned int method, unsigned int sequence, Qpc::tick_t begin_qpc ) noexcept;
	void record_draw( RenderWorkerMetrics& metrics,
		IDirect3DDevice9*				   device,
		Qpc::tick_t						   begin_qpc,
		unsigned int					   sequence,
		unsigned int					   method,
		unsigned int					   primitive_type,
		unsigned int					   primitive_count,
		unsigned int					   vertex_count,
		const PipelineObservation&		   pipeline ) noexcept;

}

#endif
