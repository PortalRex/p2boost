#include "renderer_trace.hpp"

#include "metrics.hpp"

#include <Windows.h>
#include <cstdint>
#include <cstring>
#include <vector>

namespace p2boost {
RendererTrace* RendererTrace::instance_ = nullptr;

RendererTrace::~RendererTrace() { Remove(); }

bool RendererTrace::Install(Metrics* metrics) {
    if (instance_ || !metrics) return false;
    auto* module = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(L"shaderapidx9.dll"));
    if (!module) return false;
    original_ = reinterpret_cast<ReacquireFn>(module + 0x24450);
    auto expected_call = [&](std::size_t call_rva) {
        std::vector<std::uint8_t> bytes(5);
        bytes[0] = 0xe8;
        const auto displacement = static_cast<std::int32_t>(
            reinterpret_cast<std::intptr_t>(original_) -
            (reinterpret_cast<std::intptr_t>(module + call_rva) + 5));
        std::memcpy(bytes.data() + 1, &displacement, sizeof(displacement));
        return bytes;
    };
    metrics_ = metrics;
    instance_ = this;
    auto install_slot = [](VtableSlotHook& hook, void* object, size_t index, void* replacement) {
        if (!object) return false;
        auto** vtable = *reinterpret_cast<void***>(object);
        return vtable && hook.Install(vtable + index, replacement);
    };
    void* shader_device = *reinterpret_cast<void**>(module + 0xda594);
    void* resource_callbacks = *reinterpret_cast<void**>(module + 0xda5a4);
    void* frame_sync = module + 0xd5bb0;
    if (!needs_reset_.Install(module + 0x247f7, expected_call(0x247f7), reinterpret_cast<void*>(&ReacquireHook)) ||
        !other_app_.Install(module + 0x2484b, expected_call(0x2484b), reinterpret_cast<void*>(&ReacquireHook)) ||
        !install_slot(shader_device_, shader_device, 253, reinterpret_cast<void*>(&ShaderDeviceHook)) ||
        !install_slot(frame_sync_, frame_sync, 3, reinterpret_cast<void*>(&FrameSyncHook)) ||
        !install_slot(resource_callbacks_, resource_callbacks, 15,
                      reinterpret_cast<void*>(&ResourceCallbacksHook))) {
        Remove();
        return false;
    }
    return true;
}

void RendererTrace::Remove() {
    resource_callbacks_.Remove();
    frame_sync_.Remove();
    shader_device_.Remove();
    other_app_.Remove();
    needs_reset_.Remove();
    metrics_ = nullptr;
    original_ = nullptr;
    reacquire_depth_ = 0;
    if (instance_ == this) instance_ = nullptr;
}

void __fastcall RendererTrace::ReacquireHook(void* self, void*, unsigned char release,
                                              unsigned char restore, const char* reason) {
    RendererTrace* instance = instance_;
    if (!instance || !instance->original_) return;
    if (!instance->metrics_ || !instance->metrics_->Tracing()) {
        instance->original_(self, release, restore, reason);
        return;
    }
    const double begin = NowMilliseconds();
    ++instance->reacquire_depth_;
    instance->original_(self, release, restore, reason);
    --instance->reacquire_depth_;
    instance->metrics_->RecordReacquire(NowMilliseconds() - begin, reason);
}

double RendererTrace::NowMilliseconds() noexcept {
    static LARGE_INTEGER frequency = [] { LARGE_INTEGER value{}; QueryPerformanceFrequency(&value); return value; }();
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    return static_cast<double>(now.QuadPart) * 1000.0 / static_cast<double>(frequency.QuadPart);
}

void __fastcall RendererTrace::ShaderDeviceHook(void* self, void*) {
    auto* instance = instance_;
    if (!instance) return;
    auto previous = reinterpret_cast<NoArgsFn>(instance->shader_device_.Previous());
    const bool timed = instance->reacquire_depth_ != 0 && instance->metrics_ && instance->metrics_->Tracing();
    const double begin = timed ? NowMilliseconds() : 0.0;
    previous(self);
    if (timed) instance->metrics_->RecordReacquireStage(0, NowMilliseconds() - begin);
}

void __fastcall RendererTrace::FrameSyncHook(void* self, void*) {
    auto* instance = instance_;
    if (!instance) return;
    auto previous = reinterpret_cast<NoArgsFn>(instance->frame_sync_.Previous());
    const bool timed = instance->reacquire_depth_ != 0 && instance->metrics_ && instance->metrics_->Tracing();
    const double begin = timed ? NowMilliseconds() : 0.0;
    previous(self);
    if (timed) instance->metrics_->RecordReacquireStage(1, NowMilliseconds() - begin);
}

void __fastcall RendererTrace::ResourceCallbacksHook(void* self, void*, void* callback, int context) {
    auto* instance = instance_;
    if (!instance) return;
    auto previous = reinterpret_cast<ResourceCallbacksFn>(instance->resource_callbacks_.Previous());
    const bool timed = instance->reacquire_depth_ != 0 && instance->metrics_ && instance->metrics_->Tracing();
    const double begin = timed ? NowMilliseconds() : 0.0;
    previous(self, callback, context);
    if (timed) instance->metrics_->RecordReacquireStage(2, NowMilliseconds() - begin);
}
} // namespace p2boost
