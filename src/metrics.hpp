#pragma once

#include <Windows.h>
#include <array>
#include <cstddef>
#include <string>

namespace p2boost {

class Metrics {
public:
    static constexpr size_t Capacity = 1u << 16;
    Metrics();
    void SetTracing(bool tracing) noexcept { tracing_ = tracing; }
    bool Tracing() const noexcept { return tracing_; }
    void SetCoop(bool coop) noexcept { coop_ = coop; }
    void Frame(bool simulating) noexcept;
    void RecordReacquire(double milliseconds, const char* reason, long long begin_qpc,
                         long long end_qpc) noexcept;
    void RecordReacquireStage(size_t stage, double milliseconds) noexcept;
    void RecordPortalRender(double milliseconds) noexcept;
    void RecordShadowUpdate(double milliseconds, bool same_projection, bool same_full,
                            bool skipped) noexcept;
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
    bool tracing_ = false;
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
    int pending_reacquire_reason_ = 0;
};

} // namespace p2boost
