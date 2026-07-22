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

struct WorkerGpuTrace {
    bool active = false;
    unsigned int hardware_sync_count = 0;
    long long hardware_sync_qpc = 0;
    long long max_hardware_sync_qpc = 0;
    unsigned long long hardware_sync_cycles = 0;
    int sync_index_before = -1;
    unsigned int sync_pending_before = 0;
    unsigned int sync_pending_after = 0;
    unsigned int present_count = 0;
    long long present_qpc = 0;
    long long max_present_qpc = 0;
#ifdef P2BOOST_RESEARCH_DRAW_TRACE
    unsigned int draw_count = 0;
    long long draw_qpc = 0;
    long long max_draw_qpc = 0;
    unsigned int max_draw_method = 0;
    unsigned int max_draw_primitive_type = 0;
    unsigned int max_draw_primitive_count = 0;
    unsigned int max_draw_vertex_count = 0;
    void* max_draw_stack[16]{};
    unsigned short max_draw_stack_depth = 0;
    unsigned int command_keys[16]{};
    unsigned int command_counts[16]{};
#endif
};
thread_local WorkerGpuTrace worker_gpu_trace;

#ifdef P2BOOST_RESEARCH_DRAW_TRACE
void RecordDraw(long long begin_qpc, unsigned int method, unsigned int primitive_type,
                unsigned int primitive_count, unsigned int vertex_count) {
    if (!worker_gpu_trace.active) return;
    LARGE_INTEGER end{};
    QueryPerformanceCounter(&end);
    const long long elapsed = end.QuadPart - begin_qpc;
    ++worker_gpu_trace.draw_count;
    worker_gpu_trace.draw_qpc += elapsed;
    if (elapsed <= worker_gpu_trace.max_draw_qpc) return;
    worker_gpu_trace.max_draw_qpc = elapsed;
    worker_gpu_trace.max_draw_method = method;
    worker_gpu_trace.max_draw_primitive_type = primitive_type;
    worker_gpu_trace.max_draw_primitive_count = primitive_count;
    worker_gpu_trace.max_draw_vertex_count = vertex_count;
    static const long long capture_threshold = [] {
        LARGE_INTEGER frequency{};
        QueryPerformanceFrequency(&frequency);
        return frequency.QuadPart / 500; // 2 ms
    }();
    if (elapsed < capture_threshold) {
        worker_gpu_trace.max_draw_stack_depth = 0;
        return;
    }
    worker_gpu_trace.max_draw_stack_depth = RtlCaptureStackBackTrace(
        1, 16,
        worker_gpu_trace.max_draw_stack, nullptr);
}
#endif
}

RendererTrace::~RendererTrace() { Remove(); }

bool RendererTrace::Install(Metrics* metrics) {
    if (instance_ || !metrics) return false;
    auto* module = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(L"shaderapidx9.dll"));
    if (!module) return false;
    auto* material_module = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(L"materialsystem.dll"));
    if (!material_module) return false;
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
    removing_.store(false, std::memory_order_release);
    instance_ = this;
    auto install_slot = [](VtableSlotHook& hook, void* object, size_t index, void* replacement) {
        if (!object) return false;
        auto** vtable = *reinterpret_cast<void***>(object);
        return vtable && hook.Install(vtable + index, replacement);
    };
    void* shader_device = *reinterpret_cast<void**>(module + 0xda594);
    void* resource_callbacks = *reinterpret_cast<void**>(module + 0xda5a4);
    void* frame_sync = module + 0xd5bb0;
    void* shader_api = *reinterpret_cast<void**>(material_module + 0x120720);
    void* present_device = *reinterpret_cast<void**>(material_module + 0x120728);
    if (!shader_api || !present_device) {
        Remove();
        return false;
    }
    auto** shader_api_vtable = *reinterpret_cast<void***>(shader_api);
    auto** present_vtable = *reinterpret_cast<void***>(present_device);
    if (!shader_api_vtable || shader_api_vtable[136] != module + 0x14a50 ||
        !present_vtable || present_vtable[10] != module + 0x24940) {
        Remove();
        return false;
    }
#ifdef P2BOOST_RESEARCH_DRAW_TRACE
    void* d3d_device = *reinterpret_cast<void**>(module + 0xda5d8);
    material_module_ = material_module;
    const bool draw_hooks = d3d_device &&
        install_slot(draw_primitive_, d3d_device, 81,
                     reinterpret_cast<void*>(&DrawPrimitiveHook)) &&
        install_slot(draw_indexed_primitive_, d3d_device, 82,
                     reinterpret_cast<void*>(&DrawIndexedPrimitiveHook)) &&
        install_slot(draw_primitive_up_, d3d_device, 83,
                     reinterpret_cast<void*>(&DrawPrimitiveUpHook)) &&
        install_slot(draw_indexed_primitive_up_, d3d_device, 84,
                     reinterpret_cast<void*>(&DrawIndexedPrimitiveUpHook));
#else
    const bool draw_hooks = true;
#endif
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
    std::vector<std::uint8_t> render_worker_pointer{0xb8, 0, 0, 0, 0};
    const auto render_worker_original = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(material_module + 0xf3f0));
    render_worker_original_ = reinterpret_cast<RenderWorkerFn>(
        static_cast<std::uintptr_t>(render_worker_original));
    std::memcpy(render_worker_pointer.data() + 1, &render_worker_original,
                sizeof(render_worker_original));
    std::vector<std::uint8_t> hardware_sync_entry{0xa1, 0, 0, 0, 0};
    const auto frame_sync_cvar = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(module + 0xd6efc));
    std::memcpy(hardware_sync_entry.data() + 1, &frame_sync_cvar, sizeof(frame_sync_cvar));
    if (!needs_reset_.Install(module + 0x247f7, expected_call(0x247f7), reinterpret_cast<void*>(&ReacquireHook)) ||
        !other_app_.Install(module + 0x2484b, expected_call(0x2484b), reinterpret_cast<void*>(&ReacquireHook)) ||
        !lighting_restore_.Install(engine_module + 0xf8bb4, lighting_call,
                                   reinterpret_cast<void*>(&StaticLightingRestoreHook)) ||
        !lighting_cache_restore_.Install(engine_module + 0xf3adc, lighting_cache_call,
                                         reinterpret_cast<void*>(&StaticLightingCacheRestoreHook)) ||
        !install_slot(shader_device_, shader_device, 253, reinterpret_cast<void*>(&ShaderDeviceHook)) ||
        !install_slot(frame_sync_, frame_sync, 3, reinterpret_cast<void*>(&FrameSyncHook)) ||
        !install_slot(resource_callbacks_, resource_callbacks, 15,
                      reinterpret_cast<void*>(&ResourceCallbacksHook)) ||
        !hardware_sync_.Install(module + 0x14a50, hardware_sync_entry,
                                reinterpret_cast<void*>(&HardwareSyncHook)) ||
        !present_.Install(present_vtable + 10, reinterpret_cast<void*>(&PresentHook)) ||
        !draw_hooks ||
        !render_worker_.Install(material_module + 0x12c84,
                                render_worker_pointer, 1,
                                reinterpret_cast<void*>(&RenderWorkerHook))) {
        Remove();
        return false;
    }
    return true;
}

void RendererTrace::Remove() {
    removing_.store(true, std::memory_order_release);
    render_worker_.Remove();
    // The material render queue runs on a worker thread. Its call target can
    // already have been fetched when the immediate is restored, so require a
    // short zero-callback grace period before this DLL can be unloaded.
    unsigned int quiet_milliseconds = 0;
    while (quiet_milliseconds < 50) {
        if (active_render_workers_.load(std::memory_order_acquire) == 0) {
            ++quiet_milliseconds;
        } else {
            quiet_milliseconds = 0;
        }
        Sleep(1);
    }
#ifdef P2BOOST_RESEARCH_DRAW_TRACE
    draw_indexed_primitive_up_.Remove();
    draw_primitive_up_.Remove();
    draw_indexed_primitive_.Remove();
    draw_primitive_.Remove();
    material_module_ = nullptr;
#endif
    present_.Remove();
    hardware_sync_.Remove();
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
    render_worker_original_ = nullptr;
    reacquire_depth_.store(0);
    active_render_workers_.store(0, std::memory_order_release);
    material_system_ = nullptr;
    material_lock_ = nullptr;
    material_unlock_ = nullptr;
    fix_enabled_.store(false);
    lighting_skips_.store(0);
    if (instance_ == this) instance_ = nullptr;
}

void __fastcall RendererTrace::RenderWorkerHook(void* self, void*, void* queued_context) {
    auto* instance = instance_;
    if (!instance) return;
    instance->active_render_workers_.fetch_add(1, std::memory_order_acq_rel);
    struct ActiveWorkerGuard {
        RendererTrace* instance;
        ~ActiveWorkerGuard() {
            instance->active_render_workers_.fetch_sub(1, std::memory_order_acq_rel);
        }
    } guard{instance};
    auto previous = instance->render_worker_original_;
    if (!previous) return;
    const bool tracing = !instance->removing_.load(std::memory_order_acquire) &&
        instance->metrics_ && instance->metrics_->Tracing() && queued_context;
    if (!tracing) {
        previous(self, queued_context);
        return;
    }

    auto* context = static_cast<std::uint8_t*>(queued_context);
    worker_gpu_trace = {};
    unsigned int queued_calls = 0;
    for (auto* node = *reinterpret_cast<std::uint8_t**>(context + 0x1f0);
         node && queued_calls < 1000000; node = *reinterpret_cast<std::uint8_t**>(node)) {
        ++queued_calls;
#ifdef P2BOOST_RESEARCH_DRAW_TRACE
        auto* object = node + sizeof(void*);
        auto** vtable = *reinterpret_cast<void***>(object);
        auto* execute = vtable ? reinterpret_cast<std::uint8_t*>(vtable[3]) : nullptr;
        auto* key = execute;
        if (execute == instance->material_module_ + 0x13e90) {
            key = *reinterpret_cast<std::uint8_t**>(object + 0x10);
            if (key == instance->material_module_ + 0xcfc0) {
                auto* target = *reinterpret_cast<std::uint8_t**>(object + 0x14);
                if (target) {
                    auto* leaf = *reinterpret_cast<std::uint8_t**>(target + 0x10);
                    if (leaf) key = leaf;
                }
            }
        }
        const auto key32 = static_cast<unsigned int>(reinterpret_cast<std::uintptr_t>(key));
        size_t slot = 0;
        while (slot < 16 && worker_gpu_trace.command_counts[slot] != 0 &&
               worker_gpu_trace.command_keys[slot] != key32) ++slot;
        if (slot < 16) {
            worker_gpu_trace.command_keys[slot] = key32;
            ++worker_gpu_trace.command_counts[slot];
        }
#endif
    }
    const auto byte_delta = [context](size_t current, size_t start) {
        const int value = *reinterpret_cast<int*>(context + current) -
                          *reinterpret_cast<int*>(context + start);
        return value > 0 ? static_cast<unsigned int>(value) : 0u;
    };
    const unsigned int parameter_bytes = byte_delta(0x1f8, 0x204);
    const unsigned int vertex_bytes = byte_delta(0x264, 0x270);
    const unsigned int index_bytes = byte_delta(0x28c, 0x298);
    LARGE_INTEGER begin{}, end{};
    unsigned long long begin_cycles = 0;
    unsigned long long end_cycles = 0;
    QueryPerformanceCounter(&begin);
    QueryThreadCycleTime(GetCurrentThread(), &begin_cycles);
    worker_gpu_trace.active = true;
    previous(self, queued_context);
    worker_gpu_trace.active = false;
    QueryThreadCycleTime(GetCurrentThread(), &end_cycles);
    QueryPerformanceCounter(&end);
    instance->metrics_->RecordRenderWorker(
        begin.QuadPart, end.QuadPart, end_cycles - begin_cycles, queued_calls,
        parameter_bytes, vertex_bytes, index_bytes,
        worker_gpu_trace.hardware_sync_count, worker_gpu_trace.hardware_sync_qpc,
        worker_gpu_trace.max_hardware_sync_qpc, worker_gpu_trace.hardware_sync_cycles,
        worker_gpu_trace.sync_index_before, worker_gpu_trace.sync_pending_before,
        worker_gpu_trace.sync_pending_after, worker_gpu_trace.present_count,
        worker_gpu_trace.present_qpc, worker_gpu_trace.max_present_qpc
#ifdef P2BOOST_RESEARCH_DRAW_TRACE
        , worker_gpu_trace.draw_count, worker_gpu_trace.draw_qpc,
        worker_gpu_trace.max_draw_qpc, worker_gpu_trace.max_draw_method,
        worker_gpu_trace.max_draw_primitive_type,
        worker_gpu_trace.max_draw_primitive_count,
        worker_gpu_trace.max_draw_vertex_count,
        worker_gpu_trace.max_draw_stack, worker_gpu_trace.max_draw_stack_depth,
        worker_gpu_trace.command_keys, worker_gpu_trace.command_counts
#endif
        );
}

void __fastcall RendererTrace::HardwareSyncHook(void* self, void*) {
    auto* instance = instance_;
    if (!instance) return;
    auto previous = reinterpret_cast<void(__thiscall*)(void*)>(instance->hardware_sync_.Previous());
    if (!previous) return;
    if (!worker_gpu_trace.active || !instance->metrics_ || !instance->metrics_->Tracing()) {
        previous(self);
        return;
    }
    auto* bytes = static_cast<std::uint8_t*>(self);
    const int index_before = *reinterpret_cast<int*>(bytes - 0x18c);
    const unsigned int pending_before =
        static_cast<unsigned int>(*(bytes - 0x190) != 0) |
        (static_cast<unsigned int>(*(bytes - 0x18f) != 0) << 1);
    LARGE_INTEGER begin{}, end{};
    unsigned long long begin_cycles = 0;
    unsigned long long end_cycles = 0;
    QueryPerformanceCounter(&begin);
    QueryThreadCycleTime(GetCurrentThread(), &begin_cycles);
    previous(self);
    QueryThreadCycleTime(GetCurrentThread(), &end_cycles);
    QueryPerformanceCounter(&end);
    const long long elapsed = end.QuadPart - begin.QuadPart;
    ++worker_gpu_trace.hardware_sync_count;
    worker_gpu_trace.hardware_sync_qpc += elapsed;
    worker_gpu_trace.hardware_sync_cycles += end_cycles - begin_cycles;
    if (elapsed > worker_gpu_trace.max_hardware_sync_qpc) {
        worker_gpu_trace.max_hardware_sync_qpc = elapsed;
        worker_gpu_trace.sync_index_before = index_before;
        worker_gpu_trace.sync_pending_before = pending_before;
        worker_gpu_trace.sync_pending_after =
            static_cast<unsigned int>(*(bytes - 0x190) != 0) |
            (static_cast<unsigned int>(*(bytes - 0x18f) != 0) << 1);
    }
}

void __fastcall RendererTrace::PresentHook(void* self, void*) {
    auto* instance = instance_;
    if (!instance) return;
    auto previous = reinterpret_cast<void(__thiscall*)(void*)>(instance->present_.Previous());
    if (!previous) return;
    if (!worker_gpu_trace.active || !instance->metrics_ || !instance->metrics_->Tracing()) {
        previous(self);
        return;
    }
    LARGE_INTEGER begin{}, end{};
    QueryPerformanceCounter(&begin);
    previous(self);
    QueryPerformanceCounter(&end);
    const long long elapsed = end.QuadPart - begin.QuadPart;
    ++worker_gpu_trace.present_count;
    worker_gpu_trace.present_qpc += elapsed;
    if (elapsed > worker_gpu_trace.max_present_qpc)
        worker_gpu_trace.max_present_qpc = elapsed;
}

#ifdef P2BOOST_RESEARCH_DRAW_TRACE
HRESULT __stdcall RendererTrace::DrawPrimitiveHook(IDirect3DDevice9* self,
                                                    D3DPRIMITIVETYPE primitive_type,
                                                    UINT start_vertex, UINT primitive_count) {
    auto* instance = instance_;
    auto previous = instance ? reinterpret_cast<decltype(&DrawPrimitiveHook)>(
        instance->draw_primitive_.Previous()) : nullptr;
    if (!previous) return D3DERR_INVALIDCALL;
    if (!worker_gpu_trace.active || !instance->metrics_ || !instance->metrics_->Tracing())
        return previous(self, primitive_type, start_vertex, primitive_count);
    LARGE_INTEGER begin{};
    QueryPerformanceCounter(&begin);
    const HRESULT result = previous(self, primitive_type, start_vertex, primitive_count);
    RecordDraw(begin.QuadPart, 81, primitive_type, primitive_count, 0);
    return result;
}

HRESULT __stdcall RendererTrace::DrawIndexedPrimitiveHook(
    IDirect3DDevice9* self, D3DPRIMITIVETYPE primitive_type, INT base_vertex_index,
    UINT min_vertex_index, UINT vertex_count, UINT start_index, UINT primitive_count) {
    auto* instance = instance_;
    auto previous = instance ? reinterpret_cast<decltype(&DrawIndexedPrimitiveHook)>(
        instance->draw_indexed_primitive_.Previous()) : nullptr;
    if (!previous) return D3DERR_INVALIDCALL;
    if (!worker_gpu_trace.active || !instance->metrics_ || !instance->metrics_->Tracing())
        return previous(self, primitive_type, base_vertex_index, min_vertex_index,
                        vertex_count, start_index, primitive_count);
    LARGE_INTEGER begin{};
    QueryPerformanceCounter(&begin);
    const HRESULT result = previous(self, primitive_type, base_vertex_index, min_vertex_index,
                                    vertex_count, start_index, primitive_count);
    RecordDraw(begin.QuadPart, 82, primitive_type, primitive_count, vertex_count);
    return result;
}

HRESULT __stdcall RendererTrace::DrawPrimitiveUpHook(IDirect3DDevice9* self,
                                                      D3DPRIMITIVETYPE primitive_type,
                                                      UINT primitive_count,
                                                      const void* vertex_data,
                                                      UINT vertex_stride) {
    auto* instance = instance_;
    auto previous = instance ? reinterpret_cast<decltype(&DrawPrimitiveUpHook)>(
        instance->draw_primitive_up_.Previous()) : nullptr;
    if (!previous) return D3DERR_INVALIDCALL;
    if (!worker_gpu_trace.active || !instance->metrics_ || !instance->metrics_->Tracing())
        return previous(self, primitive_type, primitive_count, vertex_data, vertex_stride);
    LARGE_INTEGER begin{};
    QueryPerformanceCounter(&begin);
    const HRESULT result = previous(self, primitive_type, primitive_count,
                                    vertex_data, vertex_stride);
    RecordDraw(begin.QuadPart, 83, primitive_type, primitive_count, 0);
    return result;
}

HRESULT __stdcall RendererTrace::DrawIndexedPrimitiveUpHook(
    IDirect3DDevice9* self, D3DPRIMITIVETYPE primitive_type, UINT min_vertex_index,
    UINT vertex_count, UINT primitive_count, const void* index_data, D3DFORMAT index_format,
    const void* vertex_data, UINT vertex_stride) {
    auto* instance = instance_;
    auto previous = instance ? reinterpret_cast<decltype(&DrawIndexedPrimitiveUpHook)>(
        instance->draw_indexed_primitive_up_.Previous()) : nullptr;
    if (!previous) return D3DERR_INVALIDCALL;
    if (!worker_gpu_trace.active || !instance->metrics_ || !instance->metrics_->Tracing())
        return previous(self, primitive_type, min_vertex_index, vertex_count, primitive_count,
                        index_data, index_format, vertex_data, vertex_stride);
    LARGE_INTEGER begin{};
    QueryPerformanceCounter(&begin);
    const HRESULT result = previous(self, primitive_type, min_vertex_index, vertex_count,
                                    primitive_count, index_data, index_format, vertex_data,
                                    vertex_stride);
    RecordDraw(begin.QuadPart, 84, primitive_type, primitive_count, vertex_count);
    return result;
}
#endif

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
