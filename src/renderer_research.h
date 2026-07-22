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
	};

	struct PipelineObservation {
		PipelineState m_state{ };
		bool		  m_first_vertex_shader{ };
		bool		  m_first_pixel_shader{ };
		bool		  m_first_pipeline{ };
		std::uint8_t  m_first_texture_mask{ };
	};

	void reset_worker_state( ) noexcept;
	void count_queued_command(
		RenderWorkerMetrics& metrics, std::uint8_t* node, void* queued_functor_execute, void* queued_functor_leaf ) noexcept;
	void capture_dynamic_light( RenderWorkerMetrics& metrics, const void* command ) noexcept;

	void							  set_texture( DWORD stage, IDirect3DBaseTexture9* texture ) noexcept;
	void							  set_vertex_shader( IDirect3DVertexShader9* shader ) noexcept;
	void							  set_pixel_shader( IDirect3DPixelShader9* shader ) noexcept;
	[[nodiscard]] PipelineObservation observe_pipeline( ) noexcept;
	void							  record_draw( RenderWorkerMetrics& metrics,
									 Qpc::tick_t						begin_qpc,
									 unsigned int						method,
									 unsigned int						primitive_type,
									 unsigned int						primitive_count,
									 unsigned int						vertex_count,
									 const PipelineObservation&			pipeline ) noexcept;

}

#endif
