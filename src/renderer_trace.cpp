#include "renderer_trace.hpp"

#include "metrics.hpp"

#include <Windows.h>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <vector>

namespace p2boost {
RendererTrace* RendererTrace::instance_ = nullptr;

namespace {
bool HasBytes(const std::uint8_t* address, const std::initializer_list<std::uint8_t>& bytes) {
    return address && std::memcmp(address, bytes.begin(), bytes.size()) == 0;
}
}

RendererTrace::~RendererTrace() { Remove(); }

bool RendererTrace::Install(Metrics* metrics) {
    if (instance_ || !metrics) return false;
    auto* module = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(L"shaderapidx9.dll"));
    if (!module) return false;
    auto* material_module = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(L"materialsystem.dll"));
    if (material_module) {
        using CreateInterfaceFn = void* (__cdecl*)(const char*, int*);
        auto factory = reinterpret_cast<CreateInterfaceFn>(GetProcAddress(
            reinterpret_cast<HMODULE>(material_module), "CreateInterface"));
        void* material_system = factory ? factory("VMaterialSystem080", nullptr) : nullptr;
        if (material_system) {
            auto** vtable = *reinterpret_cast<void***>(material_system);
            material_system_ = material_system;
            const auto material_lock_rva = reinterpret_cast<std::uint8_t*>(vtable[116]) - material_module;
            const auto material_unlock_rva = reinterpret_cast<std::uint8_t*>(vtable[117]) - material_module;
            if (material_lock_rva == 0xeab0 && material_unlock_rva == 0xeba0 &&
                HasBytes(material_module + 0xeab0, {0x56, 0x8b, 0xf1, 0x57}) &&
                HasBytes(material_module + 0xeba0, {0x55, 0x8b, 0xec, 0x56, 0x57, 0x8b, 0x7d, 0x08, 0x8b, 0xf1})) {
                material_lock_ = reinterpret_cast<MaterialLockFn>(vtable[116]);
                material_unlock_ = reinterpret_cast<MaterialUnlockFn>(vtable[117]);
            }
        }
    }
    original_ = reinterpret_cast<ReacquireFn>(module + 0x24450);
    auto* engine_module = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(L"engine.dll"));
    if (!engine_module) return false;
    lighting_original_ = reinterpret_cast<StaticLightingRestoreFn>(engine_module + 0x1aeae0);
    lighting_cache_original_ = reinterpret_cast<StaticLightingRestoreFn>(engine_module + 0x1ae7f0);
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
    std::vector<std::uint8_t> lighting_call(5);
    lighting_call[0] = 0xe8;
    const auto lighting_displacement = static_cast<std::int32_t>(
        reinterpret_cast<std::intptr_t>(lighting_original_) -
        (reinterpret_cast<std::intptr_t>(engine_module + 0xf8bb4) + 5));
    std::memcpy(lighting_call.data() + 1, &lighting_displacement, sizeof(lighting_displacement));
    std::vector<std::uint8_t> lighting_cache_call(5);
    lighting_cache_call[0] = 0xe8;
    const auto lighting_cache_displacement = static_cast<std::int32_t>(
        reinterpret_cast<std::intptr_t>(lighting_cache_original_) -
        (reinterpret_cast<std::intptr_t>(engine_module + 0xf3adc) + 5));
    std::memcpy(lighting_cache_call.data() + 1, &lighting_cache_displacement,
                sizeof(lighting_cache_displacement));
    if (!needs_reset_.Install(module + 0x247f7, expected_call(0x247f7), reinterpret_cast<void*>(&ReacquireHook)) ||
        !other_app_.Install(module + 0x2484b, expected_call(0x2484b), reinterpret_cast<void*>(&ReacquireHook)) ||
        !lighting_restore_.Install(engine_module + 0xf8bb4, lighting_call,
                                   reinterpret_cast<void*>(&StaticLightingRestoreHook)) ||
        !lighting_cache_restore_.Install(engine_module + 0xf3adc, lighting_cache_call,
                                         reinterpret_cast<void*>(&StaticLightingCacheRestoreHook)) ||
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
    lighting_cache_restore_.Remove();
    lighting_restore_.Remove();
    other_app_.Remove();
    needs_reset_.Remove();
    metrics_ = nullptr;
    original_ = nullptr;
    lighting_original_ = nullptr;
    lighting_cache_original_ = nullptr;
    reacquire_depth_.store(0);
    material_system_ = nullptr;
    material_lock_ = nullptr;
    material_unlock_ = nullptr;
    fix_enabled_.store(false);
    lighting_skips_.store(0);
    if (instance_ == this) instance_ = nullptr;
}

void __fastcall RendererTrace::ReacquireHook(void* self, void*, unsigned char release,
                                              unsigned char restore, const char* reason) {
    RendererTrace* instance = instance_;
    if (!instance || !instance->original_) return;
    void* material_lock = nullptr;
    if (instance->fix_enabled_.load() && instance->material_system_ && instance->material_lock_ &&
        instance->material_unlock_) {
        material_lock = instance->material_lock_(instance->material_system_);
    }
    const bool tracing = instance->metrics_ && instance->metrics_->Tracing();
    LARGE_INTEGER frequency{}, begin_qpc{}, end_qpc{};
    if (tracing) {
        QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&begin_qpc);
    }
    instance->reacquire_depth_.fetch_add(1);
    if (!tracing) {
        instance->original_(self, release, restore, reason);
        instance->reacquire_depth_.fetch_sub(1);
        if (material_lock) instance->material_unlock_(instance->material_system_, material_lock);
        return;
    }
    instance->original_(self, release, restore, reason);
    QueryPerformanceCounter(&end_qpc);
    instance->reacquire_depth_.fetch_sub(1);
    instance->metrics_->RecordReacquire(
        static_cast<double>(end_qpc.QuadPart - begin_qpc.QuadPart) * 1000.0 /
            static_cast<double>(frequency.QuadPart),
        reason, begin_qpc.QuadPart, end_qpc.QuadPart);
    if (material_lock) instance->material_unlock_(instance->material_system_, material_lock);
}

void __cdecl RendererTrace::StaticLightingRestoreHook() {
    auto* instance = instance_;
    if (!instance || !instance->lighting_original_) return;
    if (instance->fix_enabled_.load() && instance->reacquire_depth_.load() != 0) {
        instance->lighting_skips_.fetch_add(1);
        return;
    }
    instance->lighting_original_();
}

void __cdecl RendererTrace::StaticLightingCacheRestoreHook() {
    auto* instance = instance_;
    if (!instance || !instance->lighting_cache_original_) return;
    if (instance->fix_enabled_.load() && instance->reacquire_depth_.load() != 0) {
        instance->lighting_skips_.fetch_add(1);
        return;
    }
    instance->lighting_cache_original_();
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
    const bool timed = instance->reacquire_depth_.load() != 0 && instance->metrics_ && instance->metrics_->Tracing();
    const double begin = timed ? NowMilliseconds() : 0.0;
    previous(self);
    if (timed) instance->metrics_->RecordReacquireStage(0, NowMilliseconds() - begin);
}

void __fastcall RendererTrace::FrameSyncHook(void* self, void*) {
    auto* instance = instance_;
    if (!instance) return;
    auto previous = reinterpret_cast<NoArgsFn>(instance->frame_sync_.Previous());
    const bool timed = instance->reacquire_depth_.load() != 0 && instance->metrics_ && instance->metrics_->Tracing();
    const double begin = timed ? NowMilliseconds() : 0.0;
    previous(self);
    if (timed) instance->metrics_->RecordReacquireStage(1, NowMilliseconds() - begin);
}

void __fastcall RendererTrace::ResourceCallbacksHook(void* self, void*, void* callback, int context) {
    auto* instance = instance_;
    if (!instance) return;
    auto previous = reinterpret_cast<ResourceCallbacksFn>(instance->resource_callbacks_.Previous());
    const bool timed = instance->reacquire_depth_.load() != 0 && instance->metrics_ && instance->metrics_->Tracing();
    const double begin = timed ? NowMilliseconds() : 0.0;
    previous(self, callback, context);
    if (timed) instance->metrics_->RecordReacquireStage(2, NowMilliseconds() - begin);
}
} // namespace p2boost
