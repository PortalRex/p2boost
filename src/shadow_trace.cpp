#include "shadow_trace.hpp"

#include "metrics.hpp"

#include <Windows.h>
#include <cstddef>
#include <cstring>

namespace p2boost {
ShadowTrace* ShadowTrace::instance_ = nullptr;

ShadowTrace::~ShadowTrace() { Remove(); }

bool ShadowTrace::Install(Metrics* metrics) {
    if (instance_ || !metrics) return false;
    auto* client = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(L"client.dll"));
    if (!client) return false;
    auto* shadow_manager = client + 0x9df090;
    auto** vtable = *reinterpret_cast<void***>(shadow_manager);
    if (!vtable || vtable != reinterpret_cast<void**>(client + 0x76ab2c) ||
        vtable[24] != client + 0xf81a0) {
        return false;
    }
    auto** flashlight_models_parent = reinterpret_cast<int**>(client + 0x9dea24);
    if (!*flashlight_models_parent) return false;
    flashlight_models_parent_ = flashlight_models_parent;
    remove_dirty_ = reinterpret_cast<RemoveDirtyFn>(client + 0xf5640);
    metrics_ = metrics;
    states_.reserve(16);
    instance_ = this;
    if (!update_projected_.Install(vtable + 24,
                                   reinterpret_cast<void*>(&UpdateProjectedHook))) {
        instance_ = nullptr;
        metrics_ = nullptr;
        return false;
    }
    return true;
}

void ShadowTrace::Remove() {
    update_projected_.Remove();
    Invalidate();
    metrics_ = nullptr;
    flashlight_models_parent_ = nullptr;
    remove_dirty_ = nullptr;
    enabled_.store(false, std::memory_order_relaxed);
    skips_.store(0, std::memory_order_relaxed);
    if (instance_ == this) instance_ = nullptr;
}

void ShadowTrace::Invalidate() noexcept {
    states_.clear();
}

void ShadowTrace::SetEnabled(bool enabled) noexcept {
    if (enabled_.exchange(enabled, std::memory_order_relaxed) != enabled) Invalidate();
}

void __fastcall ShadowTrace::UpdateProjectedHook(void* self, void*, unsigned int handle, bool force) {
    auto* instance = instance_;
    if (!instance) return;
    auto previous = reinterpret_cast<UpdateProjectedFn>(instance->update_projected_.Previous());
    if (!previous) return;
    const bool tracing = instance->metrics_ && instance->metrics_->Tracing();
    if ((handle & 0xffffu) == 0xffffu) {
        previous(self, handle, force);
        return;
    }

    auto* entries = *reinterpret_cast<std::uint8_t**>(
        static_cast<std::uint8_t*>(self) + 0x2c);
    if (!entries) {
        previous(self, handle, force);
        return;
    }
    const unsigned int index = handle & 0xffffu;
    const auto* shadow = entries + index * 0x114u;
    auto prior = instance->states_.end();
    for (auto item = instance->states_.begin(); item != instance->states_.end(); ++item) {
        if (item->handle == index) { prior = item; break; }
    }
    auto* flashlight_models_parent = *instance->flashlight_models_parent_;
    const int flashlight_models = flashlight_models_parent ?
        *reinterpret_cast<int*>(reinterpret_cast<std::uint8_t*>(flashlight_models_parent) + 0x30) : 0;
    const bool same_full = prior != instance->states_.end() &&
        prior->flashlight_models == flashlight_models &&
        std::memcmp(prior->bytes.data(), shadow, prior->bytes.size()) == 0;

    if (instance->enabled_.load(std::memory_order_relaxed) && same_full) {
        // Preserve UpdateProjectedTexture's dirty-list cleanup while omitting
        // only the byte-identical projection rebuild.
        instance->remove_dirty_(self, handle);
        instance->skips_.fetch_add(1, std::memory_order_relaxed);
        if (tracing) instance->metrics_->RecordShadowUpdate(0.0, true, true, true);
        return;
    }

    LARGE_INTEGER frequency{}, begin{}, end{};
    if (tracing) {
        QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&begin);
    }
    previous(self, handle, force);
    if (tracing) {
        QueryPerformanceCounter(&end);
        instance->metrics_->RecordShadowUpdate(
            static_cast<double>(end.QuadPart - begin.QuadPart) * 1000.0 /
                static_cast<double>(frequency.QuadPart),
            same_full, same_full, false);
    }
    if (prior == instance->states_.end()) {
        instance->states_.push_back({});
        prior = instance->states_.end() - 1;
        prior->handle = static_cast<std::uint16_t>(index);
    }
    std::memcpy(prior->bytes.data(), shadow, prior->bytes.size());
    prior->flashlight_models = flashlight_models;
}
} // namespace p2boost
