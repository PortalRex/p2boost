#include "build_fingerprint.hpp"
#include "hook.hpp"
#include "metrics.hpp"
#include "frame_pacer.hpp"

#include <Windows.h>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <memory>
#include <cstring>
#include <vector>
#include <string>

namespace {
volatile int hook_marker = 0;
__declspec(noinline) void Original() { hook_marker = 1; }
__declspec(noinline) void Replacement() { hook_marker = 2; }

bool TestHookChain() {
    void* table[1] = {reinterpret_cast<void*>(&Original)};
    p2boost::VtableSlotHook hook;
    if (!hook.Install(&table[0], reinterpret_cast<void*>(&Replacement))) return false;
    if (table[0] != reinterpret_cast<void*>(&Replacement) || hook.Previous() != reinterpret_cast<void*>(&Original)) return false;
    table[0] = reinterpret_cast<void*>(&Original); // Simulate a later owner replacing our slot.
    if (hook.Remove()) return false; // Must not overwrite the later owner.
    return table[0] == reinterpret_cast<void*>(&Original);
}

bool TestCallSitePatch() {
    auto* memory = static_cast<unsigned char*>(VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!memory) return false;
    const std::vector<std::uint8_t> expected{0xff, 0x15, 1, 2, 3, 4};
    std::memcpy(memory, expected.data(), expected.size());
    p2boost::CallSitePatch patch;
    const bool installed = patch.Install(memory, expected, reinterpret_cast<void*>(&Replacement));
    const bool encoded = installed && memory[0] == 0xe8 && memory[5] == 0x90;
    const bool removed = patch.Remove() && std::memcmp(memory, expected.data(), expected.size()) == 0;
    VirtualFree(memory, 0, MEM_RELEASE);
    return installed && encoded && removed;
}

bool TestPointerImmediatePatch() {
    auto* memory = static_cast<unsigned char*>(VirtualAlloc(
        nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!memory) return false;
    std::vector<std::uint8_t> expected{0xb8, 0, 0, 0, 0};
    const auto original = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&Original));
    std::memcpy(expected.data() + 1, &original, sizeof(original));
    std::memcpy(memory, expected.data(), expected.size());
    p2boost::PointerImmediatePatch patch;
    const bool installed = patch.Install(memory, expected, 1, reinterpret_cast<void*>(&Replacement));
    std::uint32_t encoded = 0;
    std::memcpy(&encoded, memory + 1, sizeof(encoded));
    const bool points_to_replacement = installed && encoded ==
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&Replacement)) &&
        patch.Previous() == reinterpret_cast<void*>(&Original);
    const bool removed = patch.Remove() && std::memcmp(memory, expected.data(), expected.size()) == 0;
    VirtualFree(memory, 0, MEM_RELEASE);
    return installed && points_to_replacement && removed;
}

bool TestKnownHash() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    const std::string first = p2boost::Sha256File(path);
    const std::string second = p2boost::Sha256File(path);
    return first.size() == 64 && first == second;
}

bool TestMetrics() {
    auto metrics = std::make_unique<p2boost::Metrics>();
    metrics->SetTracing(true);
    metrics->Frame(true);
    metrics->Frame(false);
    const std::string path = "p2boost-test-metrics.csv";
    if (!metrics->Dump(path)) return false;
    std::ifstream input(path);
    std::string line;
    int lines = 0;
    while (std::getline(input, line)) ++lines;
    input.close();
    DeleteFileA(path.c_str());
    DeleteFileA("p2boost-test-metrics_render_workers.csv");
    return lines == 3;
}

bool TestFramePhaseCarry() {
    constexpr float epsilon = 0.000001f;
    return std::abs(p2boost::CarryFrameOvershoot(0.0021f, 0.0020f, true) - 0.0001f) < epsilon &&
           p2boost::CarryFrameOvershoot(0.0026f, 0.0020f, true) == 0.0f &&
           p2boost::CarryFrameOvershoot(0.0021f, 0.0020f, false) == 0.0f &&
           p2boost::CarryFrameOvershoot(0.0019f, 0.0020f, true) == 0.0f;
}

} // namespace

int main() {
    const bool hook = TestHookChain();
    const bool callsite = TestCallSitePatch();
    const bool pointer = TestPointerImmediatePatch();
    const bool hash = TestKnownHash();
    const bool metrics = TestMetrics();
    const bool phase = TestFramePhaseCarry();
    std::printf("hook_chain=%s callsite=%s pointer=%s hash=%s metrics=%s phase=%s\n",
                hook ? "ok" : "FAIL", callsite ? "ok" : "FAIL", pointer ? "ok" : "FAIL",
                hash ? "ok" : "FAIL", metrics ? "ok" : "FAIL", phase ? "ok" : "FAIL");
    return hook && callsite && pointer && hash && metrics && phase ? 0 : 1;
}
