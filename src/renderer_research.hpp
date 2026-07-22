#pragma once

#ifdef P2BOOST_RESEARCH_DRAW_TRACE

#include "metrics.hpp"

#include <d3d9.h>

#include <cstdint>

namespace p2boost::renderer_research {

struct PipelineState {
    std::uint32_t vertex_shader = 0;
    std::uint32_t pixel_shader = 0;
    std::uint32_t textures[4]{};
};

struct PipelineObservation {
    PipelineState state{};
    bool first_vertex_shader = false;
    bool first_pixel_shader = false;
    bool first_pipeline = false;
    std::uint8_t first_texture_mask = 0;
};

void ResetWorkerState() noexcept;
void CountQueuedCommand(RenderWorkerMetrics& metrics,
                        std::uint8_t* node,
                        std::uint8_t* material_module) noexcept;
void CaptureDynamicLight(RenderWorkerMetrics& metrics, const void* command) noexcept;

void SetTexture(DWORD stage, IDirect3DBaseTexture9* texture) noexcept;
void SetVertexShader(IDirect3DVertexShader9* shader) noexcept;
void SetPixelShader(IDirect3DPixelShader9* shader) noexcept;
[[nodiscard]] PipelineObservation ObservePipeline() noexcept;
void RecordDraw(RenderWorkerMetrics& metrics,
                long long begin_qpc,
                unsigned int method,
                unsigned int primitive_type,
                unsigned int primitive_count,
                unsigned int vertex_count,
                const PipelineObservation& pipeline) noexcept;

} // namespace p2boost::renderer_research

#endif
