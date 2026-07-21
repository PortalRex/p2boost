#include "frame_pacer.hpp"

#include <cstdint>
#include <cstring>
#include <intrin.h>

namespace p2boost {

FramePacer* FramePacer::instance_ = nullptr;

FramePacer::~FramePacer() { Remove(); }

bool FramePacer::Install() {
    if (instance_ || import_hook_.Installed()) return false;
    HMODULE engine = GetModuleHandleW(L"engine.dll");
    HMODULE tier0 = GetModuleHandleW(L"tier0.dll");
    if (!engine || !tier0) return false;
    const auto exported_sleep = reinterpret_cast<ThreadSleepFn>(GetProcAddress(tier0, "ThreadSleep"));
    if (!exported_sleep) return false;
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
    timer_ = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                    TIMER_MODIFY_STATE | SYNCHRONIZE);
    if (!timer_) timer_ = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    if (!timer_) return false;
    // FUN_101f9e30 + 0xae calls ThreadSleep(1) through this import. Hooking the
    // slot preserves executable bytes and the previous target; the return
    // address limits high-resolution waiting to the proven limiter callsite.
    auto* callsite = reinterpret_cast<std::uint8_t*>(engine) + 0x1f9ede;
    auto** import_slot = reinterpret_cast<void**>(reinterpret_cast<std::uint8_t*>(engine) + 0x3445f8);
    const auto relocated_iat = reinterpret_cast<std::uint32_t>(import_slot);
    if (callsite[0] != 0xff || callsite[1] != 0x15 ||
        std::memcmp(callsite + 2, &relocated_iat, sizeof(relocated_iat)) != 0 || !*import_slot) {
        CloseHandle(timer_);
        timer_ = nullptr;
        return false;
    }
    original_sleep_ = reinterpret_cast<ThreadSleepFn>(*import_slot);
    limiter_return_address_ = callsite + 6;
    instance_ = this;
    if (!import_hook_.Install(import_slot, reinterpret_cast<void*>(&SleepHook))) {
        instance_ = nullptr;
        limiter_return_address_ = nullptr;
        CloseHandle(timer_);
        timer_ = nullptr;
        return false;
    }
    return true;
}

void FramePacer::Remove() {
    import_hook_.Remove();
    enabled_.store(false, std::memory_order_relaxed);
    if (timer_) CloseHandle(timer_);
    timer_ = nullptr;
    limiter_return_address_ = nullptr;
    if (instance_ == this) instance_ = nullptr;
}

void __cdecl FramePacer::SleepHook(unsigned int milliseconds) {
    FramePacer* instance = instance_;
    if (!instance) return;
    if (_ReturnAddress() == instance->limiter_return_address_) instance->Sleep(milliseconds);
    else instance->original_sleep_(milliseconds);
}

void FramePacer::Sleep(unsigned int milliseconds) {
    if (!Enabled() || milliseconds == 0 || !timer_) {
        original_sleep_(milliseconds);
        return;
    }
    LARGE_INTEGER due{};
    due.QuadPart = -static_cast<LONGLONG>(milliseconds) * 10'000LL;
    if (!SetWaitableTimerEx(timer_, &due, 0, nullptr, nullptr, nullptr, 0) ||
        WaitForSingleObject(timer_, milliseconds + 50) != WAIT_OBJECT_0) {
        original_sleep_(milliseconds);
    }
}

} // namespace p2boost
