#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace p2boost {

// Replaces one vtable entry without owning the whole table. Removal succeeds
// only while our replacement still owns the slot, which preserves later hooks
// and allows either SAR/plugin load order.
class VtableSlotHook {
public:
    VtableSlotHook() = default;
    ~VtableSlotHook();
    VtableSlotHook(const VtableSlotHook&) = delete;
    VtableSlotHook& operator=(const VtableSlotHook&) = delete;
    bool Install(void** slot, void* replacement);
    bool Remove();
    void* Previous() const noexcept { return previous_; }
    bool Installed() const noexcept { return installed_; }

private:
    void** slot_ = nullptr;
    void* previous_ = nullptr;
    void* replacement_ = nullptr;
    bool installed_ = false;
};

// Rewrites a validated x86 call site to CALL rel32 and restores the exact
// displaced bytes. The expected byte sequence is the build signature.
class CallSitePatch {
public:
    CallSitePatch() = default;
    ~CallSitePatch();
    CallSitePatch(const CallSitePatch&) = delete;
    CallSitePatch& operator=(const CallSitePatch&) = delete;
    bool Install(void* address, const std::vector<std::uint8_t>& expected, void* replacement);
    bool Remove();
    bool Installed() const noexcept { return installed_; }

private:
    std::uint8_t* address_ = nullptr;
    std::vector<std::uint8_t> original_;
    std::vector<std::uint8_t> patched_;
    bool installed_ = false;
};

// Replaces a 32-bit pointer embedded in a validated instruction. This is used
// for queued functor targets where a code detour would be needlessly invasive.
class PointerImmediatePatch {
public:
    PointerImmediatePatch() = default;
    ~PointerImmediatePatch();
    PointerImmediatePatch(const PointerImmediatePatch&) = delete;
    PointerImmediatePatch& operator=(const PointerImmediatePatch&) = delete;
    bool Install(void* address,
                 const std::vector<std::uint8_t>& expected,
                 size_t pointer_offset,
                 void* replacement);
    bool Remove();
    void* Previous() const noexcept { return previous_; }
    bool Installed() const noexcept { return installed_; }

private:
    std::uint8_t* address_ = nullptr;
    std::vector<std::uint8_t> original_;
    std::vector<std::uint8_t> patched_;
    void* previous_ = nullptr;
    bool installed_ = false;
};

// Five-byte x86 detour for a target whose first complete instruction span has
// been validated by the caller. The trampoline owns the displaced bytes.
class InlineDetour {
public:
    InlineDetour() = default;
    ~InlineDetour();
    InlineDetour(const InlineDetour&) = delete;
    InlineDetour& operator=(const InlineDetour&) = delete;
    bool Install(void* address, const std::vector<std::uint8_t>& expected, void* replacement);
    bool Remove();
    void* Previous() const noexcept { return trampoline_; }
    bool Installed() const noexcept { return installed_; }

private:
    std::uint8_t* address_ = nullptr;
    std::uint8_t* trampoline_ = nullptr;
    std::vector<std::uint8_t> original_;
    std::vector<std::uint8_t> patched_;
    bool installed_ = false;
};

} // namespace p2boost
