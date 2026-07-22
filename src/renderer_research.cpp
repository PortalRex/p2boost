#include "renderer_research.hpp"

#ifdef P2BOOST_RESEARCH_DRAW_TRACE

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstring>

namespace p2boost::renderer_research {
namespace {

constexpr std::size_t PointerTableSize = 8192;
constexpr std::size_t PipelineTableSize = 16384;
static_assert((PointerTableSize & (PointerTableSize - 1)) == 0);
static_assert((PipelineTableSize & (PipelineTableSize - 1)) == 0);

thread_local std::uint32_t dynamic_light_table[PointerTableSize]{};
thread_local std::uint32_t dynamic_surface_table[PointerTableSize]{};
thread_local PipelineState pipeline_state;

alignas(64) std::atomic_uint32_t vertex_shader_keys[PipelineTableSize]{};
alignas(64) std::atomic_uint32_t pixel_shader_keys[PipelineTableSize]{};
alignas(64) std::atomic_uint32_t pipeline_keys[PipelineTableSize]{};
alignas(64) std::atomic_uint32_t texture_keys[PipelineTableSize]{};

bool InsertThreadKey(std::uint32_t value, std::uint32_t* table) noexcept {
    if (!value) {
        return false;
    }
    std::size_t slot = (value * 2654435761u) & (PointerTableSize - 1);
    for (std::size_t probe = 0; probe < PointerTableSize; ++probe) {
        if (table[slot] == value) {
            return false;
        }
        if (table[slot] == 0) {
            table[slot] = value;
            return true;
        }
        slot = (slot + 1) & (PointerTableSize - 1);
    }
    return false;
}

bool InsertProcessKey(std::uint32_t key, std::atomic_uint32_t* table) noexcept {
    if (!key) {
        return false;
    }
    std::size_t slot = (key * 2654435761u) & (PipelineTableSize - 1);
    for (std::size_t probe = 0; probe < PipelineTableSize; ++probe) {
        auto current = table[slot].load(std::memory_order_relaxed);
        if (current == key) {
            return false;
        }
        if (current == 0 &&
            table[slot].compare_exchange_strong(
                current, key, std::memory_order_relaxed, std::memory_order_relaxed)) {
            return true;
        }
        slot = (slot + 1) & (PipelineTableSize - 1);
    }
    return false;
}

void DescribeTexture0(RenderWorkerMetrics& metrics, const PipelineObservation& pipeline) noexcept {
    auto* texture = reinterpret_cast<IDirect3DBaseTexture9*>(
        static_cast<std::uintptr_t>(pipeline.state.textures[0]));
    if (!texture) {
        return;
    }

    const auto type = texture->GetType();
    metrics.max_draw_texture0_type = static_cast<std::uint32_t>(type);
    metrics.max_draw_texture0_levels = texture->GetLevelCount();

    D3DSURFACE_DESC description{};
    HRESULT result = D3DERR_INVALIDCALL;
    if (type == D3DRTYPE_TEXTURE) {
        result = static_cast<IDirect3DTexture9*>(texture)->GetLevelDesc(0, &description);
    } else if (type == D3DRTYPE_CUBETEXTURE) {
        result = static_cast<IDirect3DCubeTexture9*>(texture)->GetLevelDesc(0, &description);
    }

    if (SUCCEEDED(result)) {
        metrics.max_draw_texture0_format = static_cast<std::uint32_t>(description.Format);
        metrics.max_draw_texture0_width = description.Width;
        metrics.max_draw_texture0_height = description.Height;
        metrics.max_draw_texture0_usage = description.Usage;
        metrics.max_draw_texture0_pool = static_cast<std::uint32_t>(description.Pool);

        // Hashing is intentionally research-only. LockRect can itself block on
        // a managed texture, so its cost must never enter a shipping build.
        if ((pipeline.first_texture_mask & 1) != 0 && type == D3DRTYPE_TEXTURE &&
            description.Format == D3DFMT_DXT5) {
            D3DLOCKED_RECT locked{};
            auto* texture2d = static_cast<IDirect3DTexture9*>(texture);
            if (SUCCEEDED(texture2d->LockRect(0, &locked, nullptr, D3DLOCK_READONLY))) {
                std::uint64_t hash = 1469598103934665603ull;
                const auto rows = (description.Height + 3u) / 4u;
                const auto row_bytes = ((description.Width + 3u) / 4u) * 16u;
                const auto* row = static_cast<const std::uint8_t*>(locked.pBits);
                for (UINT y = 0; y < rows; ++y) {
                    for (UINT x = 0; x < row_bytes; ++x) {
                        hash ^= row[x];
                        hash *= 1099511628211ull;
                    }
                    row += locked.Pitch;
                }
                metrics.max_draw_texture0_hash = hash;
                texture2d->UnlockRect(0);
            }
        }
        return;
    }

    if (type == D3DRTYPE_VOLUMETEXTURE) {
        D3DVOLUME_DESC volume{};
        if (SUCCEEDED(static_cast<IDirect3DVolumeTexture9*>(texture)->GetLevelDesc(0, &volume))) {
            metrics.max_draw_texture0_format = static_cast<std::uint32_t>(volume.Format);
            metrics.max_draw_texture0_width = volume.Width;
            metrics.max_draw_texture0_height = volume.Height;
            metrics.max_draw_texture0_usage = volume.Usage;
            metrics.max_draw_texture0_pool = static_cast<std::uint32_t>(volume.Pool);
        }
    }
}

} // namespace

void ResetWorkerState() noexcept {
    std::memset(dynamic_light_table, 0, sizeof(dynamic_light_table));
    std::memset(dynamic_surface_table, 0, sizeof(dynamic_surface_table));
}

void CountQueuedCommand(RenderWorkerMetrics& metrics,
                        std::uint8_t* node,
                        std::uint8_t* material_module) noexcept {
    auto* object = node + sizeof(void*);
    auto** vtable = *reinterpret_cast<void***>(object);
    auto* execute = vtable ? reinterpret_cast<std::uint8_t*>(vtable[3]) : nullptr;
    auto* key = execute;

    // Unwrap queued functors so the histogram identifies their real target.
    if (execute == material_module + 0x13e90) {
        key = *reinterpret_cast<std::uint8_t**>(object + 0x10);
        if (key == material_module + 0xcfc0) {
            auto* target = *reinterpret_cast<std::uint8_t**>(object + 0x14);
            if (target) {
                auto* leaf = *reinterpret_cast<std::uint8_t**>(target + 0x10);
                if (leaf) {
                    key = leaf;
                }
            }
        }
    }

    const auto key32 = static_cast<unsigned int>(reinterpret_cast<std::uintptr_t>(key));
    std::size_t slot = 0;
    while (slot < metrics.command_counts.size() && metrics.command_counts[slot] != 0 &&
           metrics.command_keys[slot] != key32) {
        ++slot;
    }
    if (slot < metrics.command_counts.size()) {
        metrics.command_keys[slot] = key32;
        ++metrics.command_counts[slot];
    }
}

void CaptureDynamicLight(RenderWorkerMetrics& metrics, const void* command) noexcept {
    const auto* bytes = static_cast<const std::uint8_t*>(command);
    const auto light = *reinterpret_cast<const std::uint32_t*>(bytes + 0x14);
    const auto surface = *reinterpret_cast<const std::uint32_t*>(bytes + 0x18);
    if (InsertThreadKey(light, dynamic_light_table)) {
        ++metrics.dynamic_light_unique_lights;
    }
    if (InsertThreadKey(surface, dynamic_surface_table)) {
        ++metrics.dynamic_light_unique_surfaces;
    }
    if (metrics.dynamic_light_count != 0) {
        return;
    }

    metrics.dynamic_light_first_light = light;
    metrics.dynamic_light_first_surface = surface;
    metrics.dynamic_light_first_flags = *reinterpret_cast<const std::uint32_t*>(bytes + 0x4c);
    metrics.dynamic_light_first_bool_a = *(bytes + 0x50);
    metrics.dynamic_light_first_bool_b = *(bytes + 0x51);
    if (light) {
        std::memcpy(metrics.dynamic_light_words.data(),
                    reinterpret_cast<const void*>(static_cast<std::uintptr_t>(light)),
                    sizeof(metrics.dynamic_light_words));
    }
}

void SetTexture(DWORD stage, IDirect3DBaseTexture9* texture) noexcept {
    if (stage < 4) {
        pipeline_state.textures[stage] =
            static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(texture));
    }
}

void SetVertexShader(IDirect3DVertexShader9* shader) noexcept {
    pipeline_state.vertex_shader =
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(shader));
}

void SetPixelShader(IDirect3DPixelShader9* shader) noexcept {
    pipeline_state.pixel_shader =
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(shader));
}

PipelineObservation ObservePipeline() noexcept {
    PipelineObservation observation{};
    observation.state = pipeline_state;
    observation.first_vertex_shader =
        InsertProcessKey(observation.state.vertex_shader, vertex_shader_keys);
    observation.first_pixel_shader =
        InsertProcessKey(observation.state.pixel_shader, pixel_shader_keys);

    auto pair_key = observation.state.vertex_shader * 0x9e3779b9u;
    pair_key ^= observation.state.pixel_shader + 0x85ebca6bu + (pair_key << 6) + (pair_key >> 2);
    if (!pair_key && (observation.state.vertex_shader || observation.state.pixel_shader)) {
        pair_key = 1;
    }
    observation.first_pipeline = InsertProcessKey(pair_key, pipeline_keys);
    for (std::size_t stage = 0; stage < 4; ++stage) {
        if (InsertProcessKey(observation.state.textures[stage], texture_keys)) {
            observation.first_texture_mask |= static_cast<std::uint8_t>(1u << stage);
        }
    }
    return observation;
}

void RecordDraw(RenderWorkerMetrics& metrics,
                long long begin_qpc,
                unsigned int method,
                unsigned int primitive_type,
                unsigned int primitive_count,
                unsigned int vertex_count,
                const PipelineObservation& pipeline) noexcept {
    LARGE_INTEGER end{};
    QueryPerformanceCounter(&end);
    const long long elapsed = end.QuadPart - begin_qpc;
    ++metrics.draw_count;
    metrics.draw_qpc += elapsed;
    if (elapsed <= metrics.max_draw_qpc) {
        return;
    }

    metrics.max_draw_qpc = elapsed;
    metrics.max_draw_method = method;
    metrics.max_draw_primitive_type = primitive_type;
    metrics.max_draw_primitive_count = primitive_count;
    metrics.max_draw_vertex_count = vertex_count;
    metrics.max_draw_vertex_shader = pipeline.state.vertex_shader;
    metrics.max_draw_pixel_shader = pipeline.state.pixel_shader;
    std::copy_n(pipeline.state.textures,
                metrics.max_draw_textures.size(),
                metrics.max_draw_textures.begin());
    metrics.max_draw_first_vertex_shader = pipeline.first_vertex_shader;
    metrics.max_draw_first_pixel_shader = pipeline.first_pixel_shader;
    metrics.max_draw_first_pipeline = pipeline.first_pipeline;
    metrics.max_draw_first_texture_mask = pipeline.first_texture_mask;
    DescribeTexture0(metrics, pipeline);

    static const long long capture_threshold = [] {
        LARGE_INTEGER frequency{};
        QueryPerformanceFrequency(&frequency);
        return frequency.QuadPart / 500; // 2 ms
    }();
    if (elapsed < capture_threshold) {
        metrics.max_draw_stack_depth = 0;
        return;
    }

    void* stack[16]{};
    metrics.max_draw_stack_depth = RtlCaptureStackBackTrace(1, 16, stack, nullptr);
    for (unsigned short index = 0; index < metrics.max_draw_stack_depth; ++index) {
        metrics.max_draw_stack[index] = reinterpret_cast<std::uintptr_t>(stack[index]);
    }
}

} // namespace p2boost::renderer_research

#endif
