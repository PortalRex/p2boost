#pragma once

#include "hook.hpp"

namespace p2boost {
class Metrics;

class PortalTrace {
public:
    ~PortalTrace();
    bool Install(Metrics* metrics);
    void Remove();
    bool Installed() const noexcept { return primary_view_.Installed() && secondary_view_.Installed(); }

private:
    using PortalRenderFn = void(__thiscall*)(void*, void*, unsigned int);
    static void __fastcall PortalRenderHook(void* self, void*, void* view, unsigned int clear_flags);
    static PortalTrace* instance_;
    CallSitePatch primary_view_;
    CallSitePatch secondary_view_;
    PortalRenderFn original_ = nullptr;
    Metrics* metrics_ = nullptr;
};
} // namespace p2boost
