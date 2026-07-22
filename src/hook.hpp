#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace p2boost {

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

class PointerImmediatePatch {
public:
    PointerImmediatePatch() = default;
    ~PointerImmediatePatch();
    PointerImmediatePatch(const PointerImmediatePatch&) = delete;
    PointerImmediatePatch& operator=(const PointerImmediatePatch&) = delete;
    bool Install(void* address, const std::vector<std::uint8_t>& expected,
                 size_t pointer_offset, void* replacement);
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

} // namespace p2boost
