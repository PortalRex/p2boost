#pragma once

#include "hook.hpp"

namespace p2boost {
class Metrics;

class RendererTrace {
public:
    ~RendererTrace();
    bool Install(Metrics* metrics);
    void Remove();
    bool Installed() const noexcept { return needs_reset_.Installed() && other_app_.Installed(); }

private:
    static void __fastcall ReacquireHook(void* self, void*, unsigned char release, unsigned char restore,
                                         const char* reason);
    using ReacquireFn = void(__thiscall*)(void*, unsigned char, unsigned char, const char*);
    using NoArgsFn = void(__thiscall*)(void*);
    using ResourceCallbacksFn = void(__thiscall*)(void*, void*, int);
    static void __fastcall ShaderDeviceHook(void* self, void*);
    static void __fastcall FrameSyncHook(void* self, void*);
    static void __fastcall ResourceCallbacksHook(void* self, void*, void* callback, int context);
    static double NowMilliseconds() noexcept;
    static RendererTrace* instance_;
    CallSitePatch needs_reset_;
    CallSitePatch other_app_;
    VtableSlotHook shader_device_;
    VtableSlotHook frame_sync_;
    VtableSlotHook resource_callbacks_;
    ReacquireFn original_ = nullptr;
    Metrics* metrics_ = nullptr;
    unsigned int reacquire_depth_ = 0;
};
} // namespace p2boost
