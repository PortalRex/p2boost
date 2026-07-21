#pragma once

#include "hook.hpp"

#include <Windows.h>
#include <atomic>

namespace p2boost {

class FramePacer {
public:
    FramePacer() = default;
    ~FramePacer();
    bool Install();
    void Remove();
    void SetEnabled(bool enabled) noexcept { enabled_.store(enabled, std::memory_order_relaxed); }
    bool Enabled() const noexcept { return enabled_.load(std::memory_order_relaxed); }
    bool Installed() const noexcept { return import_hook_.Installed(); }

private:
    using ThreadSleepFn = void(__cdecl*)(unsigned int);
    static void __cdecl SleepHook(unsigned int milliseconds);
    void Sleep(unsigned int milliseconds);
    static FramePacer* instance_;
    VtableSlotHook import_hook_;
    void* limiter_return_address_ = nullptr;
    HANDLE timer_ = nullptr;
    ThreadSleepFn original_sleep_ = nullptr;
    std::atomic<bool> enabled_{false};
};

} // namespace p2boost
