#pragma once

#include <Windows.h>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>

namespace p2boost {

// One execution of the material system's queued render worker. The hook fills
// this record locally, then Metrics copies it under a short lock. A named
// record keeps the renderer/metrics boundary reviewable as research evolves.
struct RenderWorkerMetrics {
    long long begin_qpc = 0;
    long long end_qpc = 0;
    unsigned long long cycles = 0;
    unsigned int queued_calls = 0;
    unsigned int parameter_bytes = 0;
    unsigned int vertex_bytes = 0;
    unsigned int index_bytes = 0;
    unsigned int hardware_sync_count = 0;
    long long hardware_sync_qpc = 0;
    long long max_hardware_sync_qpc = 0;
    unsigned long long hardware_sync_cycles = 0;
    int sync_index_before = -1;
    unsigned int sync_pending_before = 0;
    unsigned int sync_pending_after = 0;
    unsigned int present_count = 0;
    long long present_qpc = 0;
    long long max_present_qpc = 0;
#ifdef P2BOOST_RESEARCH_DRAW_TRACE
    unsigned int draw_count = 0;
    long long draw_qpc = 0;
    long long max_draw_qpc = 0;
    unsigned int max_draw_method = 0;
    unsigned int max_draw_primitive_type = 0;
    unsigned int max_draw_primitive_count = 0;
    unsigned int max_draw_vertex_count = 0;
    std::uint32_t max_draw_vertex_shader = 0;
    std::uint32_t max_draw_pixel_shader = 0;
    std::array<std::uint32_t, 4> max_draw_textures{};
    std::uint8_t max_draw_first_vertex_shader = 0;
    std::uint8_t max_draw_first_pixel_shader = 0;
    std::uint8_t max_draw_first_pipeline = 0;
    std::uint8_t max_draw_first_texture_mask = 0;
    std::uint32_t max_draw_texture0_type = 0;
    std::uint32_t max_draw_texture0_format = 0;
    std::uint32_t max_draw_texture0_width = 0;
    std::uint32_t max_draw_texture0_height = 0;
    std::uint32_t max_draw_texture0_usage = 0;
    std::uint32_t max_draw_texture0_pool = 0;
    std::uint32_t max_draw_texture0_levels = 0;
    std::uint64_t max_draw_texture0_hash = 0;
    std::array<std::uintptr_t, 16> max_draw_stack{};
    unsigned short max_draw_stack_depth = 0;
    std::array<unsigned int, 16> command_keys{};
    std::array<unsigned int, 16> command_counts{};
    unsigned int dynamic_light_count = 0;
    long long dynamic_light_qpc = 0;
    unsigned int dynamic_light_unique_lights = 0;
    unsigned int dynamic_light_unique_surfaces = 0;
    std::uint32_t dynamic_light_first_light = 0;
    std::uint32_t dynamic_light_first_surface = 0;
    std::uint32_t dynamic_light_first_flags = 0;
    std::uint8_t dynamic_light_first_bool_a = 0;
    std::uint8_t dynamic_light_first_bool_b = 0;
    std::array<std::uint32_t, 11> dynamic_light_words{};
#endif
    unsigned int thread_id = 0;
};

class Metrics {
public:
    static constexpr size_t Capacity = 1u << 16;
    Metrics();
    void SetTracing(bool tracing) noexcept { tracing_.store(tracing, std::memory_order_relaxed); }
    bool Tracing() const noexcept { return tracing_.load(std::memory_order_relaxed); }
    void SetCoop(bool coop) noexcept { coop_ = coop; }
    void Frame(bool simulating) noexcept;
    void RecordReacquire(double milliseconds,
                         const char* reason,
                         long long begin_qpc,
                         long long end_qpc) noexcept;
    void RecordReacquireStage(size_t stage, double milliseconds) noexcept;
    void RecordPortalRender(double milliseconds) noexcept;
    void RecordShadowUpdate(double milliseconds,
                            bool same_projection,
                            bool same_full,
                            bool skipped) noexcept;
    void RecordRenderWorker(RenderWorkerMetrics sample) noexcept;
    bool Dump(const std::string& path) const;
    size_t Count() const noexcept { return count_; }

private:
    struct Sample {
        long long qpc;
        double delta_ms;
        long long reacquire_begin_qpc;
        long long reacquire_end_qpc;
        double reacquire_ms;
        std::array<double, 3> reacquire_stages_ms;
        double portal_render_ms;
        unsigned int portal_render_count;
        double shadow_update_ms;
        unsigned int shadow_update_count;
        unsigned int shadow_same_projection_count;
        unsigned int shadow_same_full_count;
        unsigned int shadow_skipped_count;
        long long render_worker_begin_qpc;
        long long render_worker_end_qpc;
        double render_worker_ms;
        unsigned long long render_worker_cycles;
        unsigned int render_worker_count;
        unsigned long long render_worker_queued_calls;
        unsigned long long render_worker_parameter_bytes;
        unsigned long long render_worker_vertex_bytes;
        unsigned long long render_worker_index_bytes;
        int reacquire_reason;
        bool simulating;
        bool focused;
        bool coop;
    };
    LARGE_INTEGER frequency_{};
    long long last_qpc_ = 0;
    std::array<Sample, Capacity> samples_{};
    size_t cursor_ = 0;
    size_t count_ = 0;
    std::atomic<bool> tracing_{false};
    bool coop_ = false;
    double pending_reacquire_ms_ = 0.0;
    long long pending_reacquire_begin_qpc_ = 0;
    long long pending_reacquire_end_qpc_ = 0;
    std::array<double, 3> pending_reacquire_stages_ms_{};
    double pending_portal_render_ms_ = 0.0;
    unsigned int pending_portal_render_count_ = 0;
    double pending_shadow_update_ms_ = 0.0;
    unsigned int pending_shadow_update_count_ = 0;
    unsigned int pending_shadow_same_projection_count_ = 0;
    unsigned int pending_shadow_same_full_count_ = 0;
    unsigned int pending_shadow_skipped_count_ = 0;
    mutable std::mutex render_worker_mutex_;
    std::array<RenderWorkerMetrics, Capacity> render_worker_samples_{};
    size_t render_worker_cursor_ = 0;
    size_t render_worker_count_ = 0;
    long long pending_render_worker_begin_qpc_ = 0;
    long long pending_render_worker_end_qpc_ = 0;
    long long pending_render_worker_qpc_ = 0;
    unsigned long long pending_render_worker_cycles_ = 0;
    unsigned int pending_render_worker_count_ = 0;
    unsigned long long pending_render_worker_queued_calls_ = 0;
    unsigned long long pending_render_worker_parameter_bytes_ = 0;
    unsigned long long pending_render_worker_vertex_bytes_ = 0;
    unsigned long long pending_render_worker_index_bytes_ = 0;
    int pending_reacquire_reason_ = 0;
};

} // namespace p2boost
