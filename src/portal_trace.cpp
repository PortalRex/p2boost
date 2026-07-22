#include "portal_trace.hpp"

#include "address.hpp"
#include "metrics.hpp"
#include "x86.hpp"

#include <Windows.h>
#include <cstdint>

namespace p2boost {
PortalTrace* PortalTrace::instance_ = nullptr;

PortalTrace::~PortalTrace() {
    Remove();
}

bool PortalTrace::Install(Metrics* metrics) {
    if (instance_ || !metrics)
        return false;
    const Address module(GetModuleHandleW(L"client.dll"));
    if (!module.Valid())
        return false;
    const Address portal_render = module.Add(0x1ee590);
    original_ = portal_render.As<PortalRenderFn>();
    const auto expected_call = [&](std::ptrdiff_t rva) {
        return x86::RelativeCall(module.Add(rva), portal_render);
    };
    metrics_ = metrics;
    instance_ = this;
    if (!primary_view_.Install(module.Add(0x285c91).As<void*>(),
                               expected_call(0x285c91),
                               reinterpret_cast<void*>(&PortalRenderHook)) ||
        !secondary_view_.Install(module.Add(0x2c57db).As<void*>(),
                                 expected_call(0x2c57db),
                                 reinterpret_cast<void*>(&PortalRenderHook))) {
        Remove();
        return false;
    }
    return true;
}

void PortalTrace::Remove() {
    secondary_view_.Remove();
    primary_view_.Remove();
    metrics_ = nullptr;
    original_ = nullptr;
    if (instance_ == this)
        instance_ = nullptr;
}

void __fastcall PortalTrace::PortalRenderHook(void* self,
                                              void*,
                                              void* view,
                                              unsigned int clear_flags) {
    auto* instance = instance_;
    if (!instance || !instance->original_)
        return;
    if (!instance->metrics_ || !instance->metrics_->Tracing()) {
        instance->original_(self, view, clear_flags);
        return;
    }
    LARGE_INTEGER frequency{}, begin{}, end{};
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&begin);
    instance->original_(self, view, clear_flags);
    QueryPerformanceCounter(&end);
    instance->metrics_->RecordPortalRender(static_cast<double>(end.QuadPart - begin.QuadPart) *
                                           1000.0 / static_cast<double>(frequency.QuadPart));
}
} // namespace p2boost
