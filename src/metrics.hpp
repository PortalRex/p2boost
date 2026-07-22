#pragma once

#include <Windows.h>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>

namespace p2boost {

class Metrics {
public:
    static constexpr size_t Capacity = 1u << 16;
    Metrics();
    void SetTracing(bool tracing) noexcept { tracing_.store(tracing, std::memory_order_relaxed); }
    bool Tracing() const noexcept { return tracing_.load(std::memory_order_relaxed); }
    void SetCoop(bool coop) noexcept { coop_ = coop; }
    void Frame(bool simulating) noexcept;
    void RecordReacquire(double milliseconds, const char* reason, long long begin_qpc,
                         long long end_qpc) noexcept;
    void RecordReacquireStage(size_t stage, double milliseconds) noexcept;
    void RecordPortalRender(double milliseconds) noexcept;
    void RecordShadowUpdate(double milliseconds, bool same_projection, bool same_full,
                            bool skipped) noexcept;
    void RecordRenderWorker(long long begin_qpc, long long end_qpc,
                            unsigned long long cycles, unsigned int queued_calls,
                            unsigned int parameter_bytes, unsigned int vertex_bytes,
                            unsigned int index_bytes, unsigned int hardware_sync_count,
                            long long hardware_sync_qpc, long long max_hardware_sync_qpc,
                            unsigned long long hardware_sync_cycles, int sync_index_before,
                            unsigned int sync_pending_before, unsigned int sync_pending_after,
                            unsigned int present_count, long long present_qpc,
                            long long max_present_qpc
#ifdef P2BOOST_RESEARCH_DRAW_TRACE
                            , unsigned int draw_count, long long draw_qpc,
                            long long max_draw_qpc, unsigned int max_draw_method,
                            unsigned int max_draw_primitive_type,
                            unsigned int max_draw_primitive_count,
                            unsigned int max_draw_vertex_count,
                            void* const* max_draw_stack, unsigned short max_draw_stack_depth
                            , const unsigned int* command_keys,
                            const unsigned int* command_counts,
                            unsigned int dynamic_light_calls,
                            unsigned int dynamic_light_unique_pairs,
                            unsigned int dynamic_light_unique_tuples
#endif
                            ) noexcept;
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
    struct RenderWorkerSample {
        long long begin_qpc;
        long long end_qpc;
        unsigned long long cycles;
        unsigned int queued_calls;
        unsigned int parameter_bytes;
        unsigned int vertex_bytes;
        unsigned int index_bytes;
        unsigned int hardware_sync_count;
        long long hardware_sync_qpc;
        long long max_hardware_sync_qpc;
        unsigned long long hardware_sync_cycles;
        int sync_index_before;
        unsigned int sync_pending_before;
        unsigned int sync_pending_after;
        unsigned int present_count;
        long long present_qpc;
        long long max_present_qpc;
#ifdef P2BOOST_RESEARCH_DRAW_TRACE
        unsigned int draw_count;
        long long draw_qpc;
        long long max_draw_qpc;
        unsigned int max_draw_method;
        unsigned int max_draw_primitive_type;
        unsigned int max_draw_primitive_count;
        unsigned int max_draw_vertex_count;
        std::array<std::uintptr_t, 16> max_draw_stack;
        unsigned short max_draw_stack_depth;
        std::array<unsigned int, 16> command_keys;
        std::array<unsigned int, 16> command_counts;
        unsigned int dynamic_light_calls;
        unsigned int dynamic_light_unique_pairs;
        unsigned int dynamic_light_unique_tuples;
#endif
        unsigned int thread_id;
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
    std::array<RenderWorkerSample, Capacity> render_worker_samples_{};
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
