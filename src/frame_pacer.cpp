#include "frame_pacer.hpp"

#include <Windows.h>
#include <cmath>
#include <cstdint>
#include <vector>

namespace p2boost {

FramePacer* FramePacer::instance_ = nullptr;

FramePacer::~FramePacer() { Remove(); }

bool FramePacer::Install() {
    if (instance_ || frame_complete_.Installed()) return false;
    auto* engine = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(L"engine.dll"));
    if (!engine) return false;
    // CEngine::Frame's limiter accumulator is normally reset with:
    //   xorps xmm0,xmm0; movss [esi+18h],xmm0
    // The replacement preserves only ordinary deadline-poll overshoot.
    const std::vector<std::uint8_t> expected = {
        0x0f, 0x57, 0xc0, 0xf3, 0x0f, 0x11, 0x46, 0x18
    };
    instance_ = this;
    if (!frame_complete_.Install(engine + 0x1fa043, expected,
                                 reinterpret_cast<void*>(&FrameCompleteHook))) {
        instance_ = nullptr;
        return false;
    }
    return true;
}

void FramePacer::Remove() {
    frame_complete_.Remove();
    enabled_.store(false, std::memory_order_relaxed);
    if (instance_ == this) instance_ = nullptr;
}

void __declspec(naked) __cdecl FrameCompleteHook() {
    __asm {
        push ebp
        push esi
        call FrameCompleteDispatch
        add esp, 8
        ret
    }
}

void __cdecl FrameCompleteDispatch(void* host_state, void* caller_frame) {
    auto* instance = FramePacer::instance_;
    auto* accumulated = reinterpret_cast<float*>(static_cast<std::uint8_t*>(host_state) + 0x18);
    const float target = *reinterpret_cast<const float*>(
        static_cast<const std::uint8_t*>(caller_frame) - 8);
    *accumulated = CarryFrameOvershoot(*accumulated, target, instance && instance->Enabled());
}

float CarryFrameOvershoot(float elapsed, float target, bool enabled) noexcept {
    // Do not carry debt from renderer work or a real hitch. A <=10% carry is
    // enough to correct deadline-poll drift without creating a short catch-up
    // frame when fps_max is close to the machine's throughput ceiling.
    if (enabled && std::isfinite(target) && std::isfinite(elapsed) && target > 0.0f &&
        elapsed >= target && elapsed < target * 1.10f) {
        return elapsed - target;
    }
    return 0.0f;
}

} // namespace p2boost
