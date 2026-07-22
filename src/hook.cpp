#include "hook.hpp"

#include <Windows.h>
#include <cstring>

namespace p2boost {
namespace {
bool WriteSlot(void** slot, void* expected, void* value) {
    DWORD old_protect = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old_protect))
        return false;
    const bool matches = *slot == expected;
    if (matches) {
        *slot = value;
        FlushInstructionCache(GetCurrentProcess(), slot, sizeof(void*));
    }
    DWORD ignored = 0;
    VirtualProtect(slot, sizeof(void*), old_protect, &ignored);
    return matches;
}
} // namespace

VtableSlotHook::~VtableSlotHook() {
    Remove();
}

bool VtableSlotHook::Install(void** slot, void* replacement) {
    if (installed_ || !slot || !replacement)
        return false;
    slot_ = slot;
    previous_ = *slot;
    replacement_ = replacement;
    installed_ = WriteSlot(slot_, previous_, replacement_);
    if (!installed_) {
        slot_ = nullptr;
        previous_ = nullptr;
        replacement_ = nullptr;
    }
    return installed_;
}

bool VtableSlotHook::Remove() {
    if (!installed_)
        return true;
    const bool removed = WriteSlot(slot_, replacement_, previous_);
    installed_ = false;
    slot_ = nullptr;
    previous_ = nullptr;
    replacement_ = nullptr;
    return removed;
}

CallSitePatch::~CallSitePatch() {
    Remove();
}

bool CallSitePatch::Install(void* address,
                            const std::vector<std::uint8_t>& expected,
                            void* replacement) {
    if (installed_ || !address || !replacement || expected.size() < 5)
        return false;
    address_ = static_cast<std::uint8_t*>(address);
    if (std::memcmp(address_, expected.data(), expected.size()) != 0) {
        address_ = nullptr;
        return false;
    }
    original_ = expected;
    patched_.assign(expected.size(), 0x90);
    patched_[0] = 0xE8;
    const auto displacement =
        static_cast<std::int32_t>(reinterpret_cast<std::intptr_t>(replacement) -
                                  (reinterpret_cast<std::intptr_t>(address_) + 5));
    std::memcpy(patched_.data() + 1, &displacement, sizeof(displacement));
    DWORD old_protect = 0;
    if (!VirtualProtect(address_, patched_.size(), PAGE_EXECUTE_READWRITE, &old_protect))
        return false;
    std::memcpy(address_, patched_.data(), patched_.size());
    FlushInstructionCache(GetCurrentProcess(), address_, patched_.size());
    DWORD ignored = 0;
    VirtualProtect(address_, patched_.size(), old_protect, &ignored);
    installed_ = true;
    return true;
}

bool CallSitePatch::Remove() {
    if (!installed_)
        return true;
    if (std::memcmp(address_, patched_.data(), patched_.size()) != 0) {
        installed_ = false;
        return false;
    }
    DWORD old_protect = 0;
    if (!VirtualProtect(address_, original_.size(), PAGE_EXECUTE_READWRITE, &old_protect))
        return false;
    std::memcpy(address_, original_.data(), original_.size());
    FlushInstructionCache(GetCurrentProcess(), address_, original_.size());
    DWORD ignored = 0;
    VirtualProtect(address_, original_.size(), old_protect, &ignored);
    installed_ = false;
    return true;
}

PointerImmediatePatch::~PointerImmediatePatch() {
    Remove();
}

bool PointerImmediatePatch::Install(void* address,
                                    const std::vector<std::uint8_t>& expected,
                                    size_t pointer_offset,
                                    void* replacement) {
    if (installed_ || !address || !replacement ||
        pointer_offset + sizeof(std::uint32_t) > expected.size())
        return false;
    const auto replacement_value = reinterpret_cast<std::uintptr_t>(replacement);
    if (replacement_value > UINT32_MAX)
        return false;
    address_ = static_cast<std::uint8_t*>(address);
    if (std::memcmp(address_, expected.data(), expected.size()) != 0) {
        address_ = nullptr;
        return false;
    }
    original_ = expected;
    patched_ = expected;
    std::uint32_t previous_value = 0;
    std::memcpy(&previous_value, original_.data() + pointer_offset, sizeof(previous_value));
    previous_ = reinterpret_cast<void*>(static_cast<std::uintptr_t>(previous_value));
    const auto replacement32 = static_cast<std::uint32_t>(replacement_value);
    std::memcpy(patched_.data() + pointer_offset, &replacement32, sizeof(replacement32));
    DWORD old_protect = 0;
    if (!VirtualProtect(address_, patched_.size(), PAGE_EXECUTE_READWRITE, &old_protect)) {
        address_ = nullptr;
        previous_ = nullptr;
        return false;
    }
    std::memcpy(address_, patched_.data(), patched_.size());
    FlushInstructionCache(GetCurrentProcess(), address_, patched_.size());
    DWORD ignored = 0;
    VirtualProtect(address_, patched_.size(), old_protect, &ignored);
    installed_ = true;
    return true;
}

bool PointerImmediatePatch::Remove() {
    if (!installed_)
        return true;
    if (std::memcmp(address_, patched_.data(), patched_.size()) != 0) {
        installed_ = false;
        return false;
    }
    DWORD old_protect = 0;
    if (!VirtualProtect(address_, original_.size(), PAGE_EXECUTE_READWRITE, &old_protect))
        return false;
    std::memcpy(address_, original_.data(), original_.size());
    FlushInstructionCache(GetCurrentProcess(), address_, original_.size());
    DWORD ignored = 0;
    VirtualProtect(address_, original_.size(), old_protect, &ignored);
    installed_ = false;
    address_ = nullptr;
    previous_ = nullptr;
    return true;
}

InlineDetour::~InlineDetour() {
    Remove();
}

bool InlineDetour::Install(void* address,
                           const std::vector<std::uint8_t>& expected,
                           void* replacement) {
    if (installed_ || !address || !replacement || expected.size() != 5)
        return false;
    address_ = static_cast<std::uint8_t*>(address);
    if (std::memcmp(address_, expected.data(), expected.size()) != 0) {
        address_ = nullptr;
        return false;
    }
    trampoline_ = static_cast<std::uint8_t*>(VirtualAlloc(
        nullptr, expected.size() + 5, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!trampoline_) {
        address_ = nullptr;
        return false;
    }
    original_ = expected;
    std::memcpy(trampoline_, original_.data(), original_.size());
    trampoline_[original_.size()] = 0xe9;
    const auto return_displacement = static_cast<std::int32_t>(
        reinterpret_cast<std::intptr_t>(address_ + original_.size()) -
        reinterpret_cast<std::intptr_t>(trampoline_ + original_.size() + 5));
    std::memcpy(
        trampoline_ + original_.size() + 1, &return_displacement, sizeof(return_displacement));
    patched_.assign(original_.size(), 0x90);
    patched_[0] = 0xe9;
    const auto replacement_displacement =
        static_cast<std::int32_t>(reinterpret_cast<std::intptr_t>(replacement) -
                                  reinterpret_cast<std::intptr_t>(address_ + 5));
    std::memcpy(patched_.data() + 1, &replacement_displacement, sizeof(replacement_displacement));
    DWORD old_protect = 0;
    if (!VirtualProtect(address_, patched_.size(), PAGE_EXECUTE_READWRITE, &old_protect)) {
        VirtualFree(trampoline_, 0, MEM_RELEASE);
        trampoline_ = nullptr;
        address_ = nullptr;
        return false;
    }
    std::memcpy(address_, patched_.data(), patched_.size());
    FlushInstructionCache(GetCurrentProcess(), address_, patched_.size());
    FlushInstructionCache(GetCurrentProcess(), trampoline_, original_.size() + 5);
    DWORD ignored = 0;
    VirtualProtect(address_, patched_.size(), old_protect, &ignored);
    installed_ = true;
    return true;
}

bool InlineDetour::Remove() {
    if (!installed_)
        return true;
    if (std::memcmp(address_, patched_.data(), patched_.size()) != 0) {
        // A later hook owns the entry point and may still chain through our
        // trampoline. Preserve it instead of creating a dangling target.
        installed_ = false;
        return false;
    }
    DWORD old_protect = 0;
    if (!VirtualProtect(address_, original_.size(), PAGE_EXECUTE_READWRITE, &old_protect))
        return false;
    std::memcpy(address_, original_.data(), original_.size());
    FlushInstructionCache(GetCurrentProcess(), address_, original_.size());
    DWORD ignored = 0;
    VirtualProtect(address_, original_.size(), old_protect, &ignored);
    VirtualFree(trampoline_, 0, MEM_RELEASE);
    installed_ = false;
    address_ = nullptr;
    trampoline_ = nullptr;
    return true;
}

} // namespace p2boost
