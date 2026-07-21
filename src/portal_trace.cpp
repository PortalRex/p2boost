#include "portal_trace.hpp"

#include "metrics.hpp"

#include <Windows.h>
#include <cstdint>
#include <cstring>
#include <vector>

namespace p2boost {
PortalTrace* PortalTrace::instance_ = nullptr;

PortalTrace::~PortalTrace() { Remove(); }

bool PortalTrace::Install(Metrics* metrics) {
    if (instance_ || !metrics) return false;
    auto* module = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(L"client.dll"));
    if (!module) return false;
    original_ = reinterpret_cast<PortalRenderFn>(module + 0x1ee590);
    auto expected_call = [&](size_t rva) {
        std::vector<std::uint8_t> bytes(5, 0);
        bytes[0] = 0xe8;
        const auto displacement = static_cast<std::int32_t>(
            reinterpret_cast<std::intptr_t>(original_) -
            (reinterpret_cast<std::intptr_t>(module + rva) + 5));
        std::memcpy(bytes.data() + 1, &displacement, sizeof(displacement));
        return bytes;
    };
    metrics_ = metrics;
    instance_ = this;
    if (!primary_view_.Install(module + 0x285c91, expected_call(0x285c91),
                               reinterpret_cast<void*>(&PortalRenderHook)) ||
        !secondary_view_.Install(module + 0x2c57db, expected_call(0x2c57db),
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
    if (instance_ == this) instance_ = nullptr;
}

void __fastcall PortalTrace::PortalRenderHook(void* self, void*, void* view, unsigned int clear_flags) {
    auto* instance = instance_;
    if (!instance || !instance->original_) return;
    if (!instance->metrics_ || !instance->metrics_->Tracing()) {
        instance->original_(self, view, clear_flags);
        return;
    }
    LARGE_INTEGER frequency{}, begin{}, end{};
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&begin);
    instance->original_(self, view, clear_flags);
    QueryPerformanceCounter(&end);
    instance->metrics_->RecordPortalRender(
        static_cast<double>(end.QuadPart - begin.QuadPart) * 1000.0 /
        static_cast<double>(frequency.QuadPart));
}
} // namespace p2boost
