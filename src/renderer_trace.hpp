#pragma once

#include "hook.hpp"

#include <atomic>

#ifdef P2BOOST_RESEARCH_DRAW_TRACE
#include <d3d9.h>
#endif

namespace p2boost {
class Metrics;

class RendererTrace {
public:
    ~RendererTrace();
    bool Install(Metrics* metrics);
    void Remove();
    bool Installed() const noexcept { return needs_reset_.Installed() && other_app_.Installed(); }
    void SetFixEnabled(bool enabled) noexcept { fix_enabled_.store(enabled); }
    bool FocusSyncAvailable() const noexcept { return material_lock_ && material_unlock_; }
    bool StaticLightingFixAvailable() const noexcept {
        return lighting_restore_.Installed() && lighting_cache_restore_.Installed();
    }
    unsigned int StaticLightingSkips() const noexcept { return lighting_skips_.load(); }

private:
    static void __fastcall ReacquireHook(void* self, void*, unsigned char release, unsigned char restore,
                                         const char* reason);
    using ReacquireFn = void(__thiscall*)(void*, unsigned char, unsigned char, const char*);
    using NoArgsFn = void(__thiscall*)(void*);
    using ResourceCallbacksFn = void(__thiscall*)(void*, void*, int);
    using MaterialLockFn = void* (__thiscall*)(void*);
    using MaterialUnlockFn = void(__thiscall*)(void*, void*);
    static void __fastcall ShaderDeviceHook(void* self, void*);
    static void __fastcall FrameSyncHook(void* self, void*);
    static void __fastcall ResourceCallbacksHook(void* self, void*, void* callback, int context);
    using RenderWorkerFn = void(__thiscall*)(void*, void*);
    static void __fastcall RenderWorkerHook(void* self, void*, void* queued_context);
    static void __fastcall HardwareSyncHook(void* self, void*);
    static void __fastcall PresentHook(void* self, void*);
#ifdef P2BOOST_RESEARCH_DRAW_TRACE
    static HRESULT __stdcall DrawPrimitiveHook(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, UINT);
    static HRESULT __stdcall DrawIndexedPrimitiveHook(IDirect3DDevice9*, D3DPRIMITIVETYPE, INT,
                                                       UINT, UINT, UINT, UINT);
    static HRESULT __stdcall DrawPrimitiveUpHook(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT,
                                                  const void*, UINT);
    static HRESULT __stdcall DrawIndexedPrimitiveUpHook(IDirect3DDevice9*, D3DPRIMITIVETYPE,
                                                         UINT, UINT, UINT, const void*,
                                                         D3DFORMAT, const void*, UINT);
#endif
    static void __cdecl StaticLightingRestoreHook();
    static void __cdecl StaticLightingCacheRestoreHook();
    static double NowMilliseconds() noexcept;
    static RendererTrace* instance_;
    CallSitePatch needs_reset_;
    CallSitePatch other_app_;
    CallSitePatch lighting_restore_;
    CallSitePatch lighting_cache_restore_;
    VtableSlotHook shader_device_;
    VtableSlotHook frame_sync_;
    VtableSlotHook resource_callbacks_;
    InlineDetour hardware_sync_;
    VtableSlotHook present_;
#ifdef P2BOOST_RESEARCH_DRAW_TRACE
    VtableSlotHook draw_primitive_;
    VtableSlotHook draw_indexed_primitive_;
    VtableSlotHook draw_primitive_up_;
    VtableSlotHook draw_indexed_primitive_up_;
    std::uint8_t* material_module_ = nullptr;
#endif
    PointerImmediatePatch render_worker_;
    RenderWorkerFn render_worker_original_ = nullptr;
    ReacquireFn original_ = nullptr;
    using StaticLightingRestoreFn = void(__cdecl*)();
    StaticLightingRestoreFn lighting_original_ = nullptr;
    StaticLightingRestoreFn lighting_cache_original_ = nullptr;
    Metrics* metrics_ = nullptr;
    std::atomic_uint reacquire_depth_{0};
    std::atomic_uint active_render_workers_{0};
    std::atomic_bool removing_{false};
    void* material_system_ = nullptr;
    MaterialLockFn material_lock_ = nullptr;
    MaterialUnlockFn material_unlock_ = nullptr;
    std::atomic_bool fix_enabled_{false};
    std::atomic_uint lighting_skips_{0};
};
} // namespace p2boost
