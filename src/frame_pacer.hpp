#pragma once

#include "hook.hpp"

#include <atomic>

namespace p2boost {

void __cdecl FrameCompleteHook();
void __cdecl FrameCompleteDispatch(void* host_state, void* caller_frame);
float CarryFrameOvershoot(float elapsed, float target, bool enabled) noexcept;

class FramePacer {
public:
    FramePacer() = default;
    ~FramePacer();
    bool Install();
    void Remove();
    void SetEnabled(bool enabled) noexcept { enabled_.store(enabled, std::memory_order_relaxed); }
    bool Enabled() const noexcept { return enabled_.load(std::memory_order_relaxed); }
    bool Installed() const noexcept { return frame_complete_.Installed(); }

private:
    friend void __cdecl FrameCompleteDispatch(void*, void*);
    static FramePacer* instance_;
    CallSitePatch frame_complete_;
    std::atomic<bool> enabled_{false};
};

} // namespace p2boost
