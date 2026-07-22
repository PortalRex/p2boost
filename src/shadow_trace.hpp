#pragma once

#include "hook.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace p2boost {
class Metrics;

class ShadowTrace {
public:
    ~ShadowTrace();
    bool Install(Metrics* metrics);
    void Remove();
    void Invalidate() noexcept;
    void SetEnabled(bool enabled) noexcept;
    bool Installed() const noexcept { return update_projected_.Installed(); }
    unsigned int Skips() const noexcept { return skips_.load(std::memory_order_relaxed); }

private:
    struct StateHash {
        std::uint16_t handle = 0;
        std::array<std::uint8_t, 0x114> bytes{};
        int flashlight_models = 0;
    };
    using UpdateProjectedFn = void(__thiscall*)(void*, unsigned int, bool);
    static void __fastcall UpdateProjectedHook(void* self, void*, unsigned int handle, bool force);
    static ShadowTrace* instance_;
    VtableSlotHook update_projected_;
    Metrics* metrics_ = nullptr;
    std::vector<StateHash> states_;
    int** flashlight_models_parent_ = nullptr;
    using RemoveDirtyFn = void(__thiscall*)(void*, unsigned int);
    RemoveDirtyFn remove_dirty_ = nullptr;
    std::atomic<bool> enabled_{false};
    std::atomic<unsigned int> skips_{0};
};
} // namespace p2boost
