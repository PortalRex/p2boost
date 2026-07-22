#include "metrics.hpp"

#include <fstream>
#include <iomanip>
#include <cstring>

namespace p2boost {

Metrics::Metrics() { QueryPerformanceFrequency(&frequency_); }

void Metrics::RecordReacquire(double milliseconds, const char* reason, long long begin_qpc,
                              long long end_qpc) noexcept {
    if (!tracing_) return;
    pending_reacquire_ms_ += milliseconds;
    if (pending_reacquire_begin_qpc_ == 0 || begin_qpc < pending_reacquire_begin_qpc_)
        pending_reacquire_begin_qpc_ = begin_qpc;
    if (end_qpc > pending_reacquire_end_qpc_) pending_reacquire_end_qpc_ = end_qpc;
    pending_reacquire_reason_ = reason && std::strcmp(reason, "NeedsReset") == 0 ? 1 : 2;
}

void Metrics::RecordReacquireStage(size_t stage, double milliseconds) noexcept {
    if (!tracing_ || stage >= pending_reacquire_stages_ms_.size()) return;
    pending_reacquire_stages_ms_[stage] += milliseconds;
}

void Metrics::RecordPortalRender(double milliseconds) noexcept {
    if (!tracing_) return;
    pending_portal_render_ms_ += milliseconds;
    ++pending_portal_render_count_;
}

void Metrics::RecordShadowUpdate(double milliseconds, bool same_projection, bool same_full,
                                 bool skipped) noexcept {
    if (!tracing_) return;
    pending_shadow_update_ms_ += milliseconds;
    ++pending_shadow_update_count_;
    if (same_projection) ++pending_shadow_same_projection_count_;
    if (same_full) ++pending_shadow_same_full_count_;
    if (skipped) ++pending_shadow_skipped_count_;
}

void Metrics::Frame(bool simulating) noexcept {
    if (!tracing_) return;
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    const double delta = last_qpc_ == 0 ? 0.0 :
        static_cast<double>(now.QuadPart - last_qpc_) * 1000.0 / static_cast<double>(frequency_.QuadPart);
    DWORD foreground_process = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &foreground_process);
    const bool focused = foreground_process == GetCurrentProcessId();
    samples_[cursor_] = {now.QuadPart, delta, pending_reacquire_begin_qpc_,
                         pending_reacquire_end_qpc_, pending_reacquire_ms_, pending_reacquire_stages_ms_,
                         pending_portal_render_ms_, pending_portal_render_count_,
                         pending_shadow_update_ms_, pending_shadow_update_count_,
                         pending_shadow_same_projection_count_, pending_shadow_same_full_count_,
                         pending_shadow_skipped_count_,
                         pending_reacquire_reason_, simulating, focused, coop_};
    pending_reacquire_ms_ = 0.0;
    pending_reacquire_begin_qpc_ = 0;
    pending_reacquire_end_qpc_ = 0;
    pending_reacquire_stages_ms_.fill(0.0);
    pending_portal_render_ms_ = 0.0;
    pending_portal_render_count_ = 0;
    pending_shadow_update_ms_ = 0.0;
    pending_shadow_update_count_ = 0;
    pending_shadow_same_projection_count_ = 0;
    pending_shadow_same_full_count_ = 0;
    pending_shadow_skipped_count_ = 0;
    pending_reacquire_reason_ = 0;
    cursor_ = (cursor_ + 1) % Capacity;
    if (count_ < Capacity) ++count_;
    last_qpc_ = now.QuadPart;
}

bool Metrics::Dump(const std::string& path) const {
    std::ofstream output(path, std::ios::trunc);
    if (!output) return false;
    output << "qpc,delta_ms,reacquire_begin_qpc,reacquire_end_qpc,reacquire_ms,shader_device_ms,frame_sync_ms,resource_callbacks_ms,"
              "portal_render_ms,portal_render_count,shadow_update_ms,shadow_update_count,shadow_same_projection_count,shadow_same_full_count,shadow_skipped_count,reacquire_reason,simulating,focused,coop\n"
           << std::fixed << std::setprecision(6);
    const size_t start = count_ == Capacity ? cursor_ : 0;
    for (size_t index = 0; index < count_; ++index) {
        const auto& sample = samples_[(start + index) % Capacity];
        output << sample.qpc << ',' << sample.delta_ms << ',' << sample.reacquire_begin_qpc << ','
               << sample.reacquire_end_qpc << ',' << sample.reacquire_ms << ','
               << sample.reacquire_stages_ms[0] << ',' << sample.reacquire_stages_ms[1] << ','
               << sample.reacquire_stages_ms[2] << ',' << sample.portal_render_ms << ','
               << sample.portal_render_count << ',' << sample.shadow_update_ms << ','
               << sample.shadow_update_count << ',' << sample.shadow_same_projection_count << ','
               << sample.shadow_same_full_count << ',' << sample.shadow_skipped_count << ','
               << sample.reacquire_reason << ','
               << (sample.simulating ? 1 : 0) << ','
               << (sample.focused ? 1 : 0) << ',' << (sample.coop ? 1 : 0) << '\n';
    }
    return output.good();
}

} // namespace p2boost
