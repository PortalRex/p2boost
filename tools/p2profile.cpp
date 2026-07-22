#include <Windows.h>
#include <TlHelp32.h>
#include <DbgHelp.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct Handle {
    HANDLE value = nullptr;
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    explicit operator bool() const { return value && value != INVALID_HANDLE_VALUE; }
};

struct Module {
    std::uintptr_t begin;
    std::uintptr_t end;
    std::string name;
};

DWORD FindMainThread(DWORD process_id) {
    Handle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0)};
    if (!snapshot) return 0;
    THREADENTRY32 entry{sizeof(entry)};
    FILETIME earliest{0xffffffffu, 0xffffffffu};
    DWORD result = 0;
    if (!Thread32First(snapshot.value, &entry)) return 0;
    do {
        if (entry.th32OwnerProcessID != process_id) continue;
        Handle thread{OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ThreadID)};
        FILETIME created{}, exited{}, kernel{}, user{};
        if (!thread || !GetThreadTimes(thread.value, &created, &exited, &kernel, &user)) continue;
        if (CompareFileTime(&created, &earliest) < 0) {
            earliest = created;
            result = entry.th32ThreadID;
        }
    } while (Thread32Next(snapshot.value, &entry));
    return result;
}

std::vector<Module> ReadModules(DWORD process_id) {
    std::vector<Module> modules;
    Handle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, process_id)};
    if (!snapshot) return modules;
    MODULEENTRY32 entry{sizeof(entry)};
    if (!Module32First(snapshot.value, &entry)) return modules;
    do {
        const auto begin = reinterpret_cast<std::uintptr_t>(entry.modBaseAddr);
        modules.push_back({begin, begin + entry.modBaseSize, entry.szModule});
    } while (Module32Next(snapshot.value, &entry));
    std::sort(modules.begin(), modules.end(), [](const Module& a, const Module& b) {
        return a.begin < b.begin;
    });
    return modules;
}

const Module* FindModule(const std::vector<Module>& modules, std::uintptr_t address) {
    const auto found = std::upper_bound(modules.begin(), modules.end(), address,
        [](std::uintptr_t value, const Module& module) { return value < module.begin; });
    if (found == modules.begin()) return nullptr;
    const Module& module = *std::prev(found);
    return address < module.end ? &module : nullptr;
}

void PrintUsage() {
    std::fputs("usage: p2profile <pid> <duration-seconds> <output.csv> [--stack]\n"
               "       default EIP sampling is low overhead; --stack is intrusive research mode\n", stderr);
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 4 && argc != 5) {
        PrintUsage();
        return 2;
    }
    const bool stack_mode = argc == 5 && std::strcmp(argv[4], "--stack") == 0;
    if (argc == 5 && !stack_mode) {
        PrintUsage();
        return 2;
    }
    const DWORD process_id = std::strtoul(argv[1], nullptr, 10);
    const double duration_seconds = std::strtod(argv[2], nullptr);
    if (!process_id || !(duration_seconds > 0.0)) {
        PrintUsage();
        return 2;
    }
    const DWORD thread_id = FindMainThread(process_id);
    Handle process{OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, process_id)};
    Handle thread{OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT |
                             THREAD_QUERY_LIMITED_INFORMATION, FALSE, thread_id)};
    const auto modules = ReadModules(process_id);
    std::FILE* output = nullptr;
    if (!process || !thread || modules.empty() || fopen_s(&output, argv[3], "w") != 0 || !output) {
        std::fprintf(stderr, "p2profile: attach/output failed (pid=%lu tid=%lu error=%lu)\n",
                     process_id, thread_id, GetLastError());
        return 1;
    }
    SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME);
    if (!SymInitialize(process.value, nullptr, TRUE)) {
        std::fprintf(stderr, "p2profile: SymInitialize failed (error=%lu)\n", GetLastError());
        std::fclose(output);
        return 1;
    }

    LARGE_INTEGER frequency{}, begin{}, now{};
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&begin);
    std::fputs("qpc,sample_id,depth,thread_id,module,rva,eip,suspend_us\n", output);
    std::uint64_t samples = 0;
    for (;;) {
        QueryPerformanceCounter(&now);
        const double elapsed = static_cast<double>(now.QuadPart - begin.QuadPart) /
                               static_cast<double>(frequency.QuadPart);
        if (elapsed >= duration_seconds) break;

        LARGE_INTEGER suspend_begin{}, suspend_end{};
        QueryPerformanceCounter(&suspend_begin);
        if (SuspendThread(thread.value) != static_cast<DWORD>(-1)) {
            CONTEXT context{};
            context.ContextFlags = CONTEXT_CONTROL;
            const BOOL captured = GetThreadContext(thread.value, &context);
            if (captured) {
                STACKFRAME64 frame{};
                frame.AddrPC.Offset = context.Eip;
                frame.AddrPC.Mode = AddrModeFlat;
                frame.AddrStack.Offset = context.Esp;
                frame.AddrStack.Mode = AddrModeFlat;
                frame.AddrFrame.Offset = context.Ebp;
                frame.AddrFrame.Mode = AddrModeFlat;
                struct CapturedFrame { std::uintptr_t address; unsigned int depth; };
                std::vector<CapturedFrame> frames;
                const unsigned int depth_limit = stack_mode ? 24u : 1u;
                for (unsigned int depth = 0; depth < depth_limit && frame.AddrPC.Offset; ++depth) {
                    const auto address = static_cast<std::uintptr_t>(frame.AddrPC.Offset);
                    frames.push_back({address, depth});
                    if (depth + 1 >= depth_limit) break;
                    if (!StackWalk64(IMAGE_FILE_MACHINE_I386, process.value, thread.value, &frame,
                                     &context, nullptr, SymFunctionTableAccess64,
                                     SymGetModuleBase64, nullptr)) break;
                }
                QueryPerformanceCounter(&suspend_end);
                ResumeThread(thread.value);
                const double suspend_us = static_cast<double>(suspend_end.QuadPart - suspend_begin.QuadPart) *
                                          1000000.0 / static_cast<double>(frequency.QuadPart);
                for (const auto& captured_frame : frames) {
                    const Module* module = FindModule(modules, captured_frame.address);
                    std::fprintf(output, "%lld,%llu,%u,%lu,%s,0x%08llx,0x%08llx,%.3f\n",
                                 now.QuadPart, static_cast<unsigned long long>(samples),
                                 captured_frame.depth, thread_id,
                                 module ? module->name.c_str() : "<unknown>",
                                 static_cast<unsigned long long>(module ? captured_frame.address - module->begin : 0),
                                 static_cast<unsigned long long>(captured_frame.address), suspend_us);
                }
                ++samples;
            } else {
                ResumeThread(thread.value);
            }
        }
        Sleep(1);
    }
    SymCleanup(process.value);
    std::fclose(output);
    std::fprintf(stdout, "p2profile: %llu samples from pid=%lu tid=%lu\n",
                 static_cast<unsigned long long>(samples), process_id, thread_id);
    return samples ? 0 : 1;
}
