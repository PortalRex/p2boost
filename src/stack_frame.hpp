#pragma once

#include "address.hpp"

#include <cstddef>
#include <cstdint>

namespace p2boost {

// Non-owning view of a conventional x86 EBP stack frame. Callers must only
// create this after the hooked function's prologue and signature are verified.
class StackFrame {
public:
    StackFrame() noexcept = default;
    explicit StackFrame(Address base) noexcept : base_(base) {}
    explicit StackFrame(const void* base) noexcept : base_(base) {}

    [[nodiscard]] bool Valid() const noexcept { return base_.Valid(); }
    [[nodiscard]] Address Base() const noexcept { return base_; }
    [[nodiscard]] Address ReturnAddress() const noexcept {
        return Address(base_.Read<std::uintptr_t>(sizeof(std::uintptr_t)));
    }
    [[nodiscard]] Address AddressOfReturnAddress() const noexcept {
        return base_.Add(sizeof(std::uintptr_t));
    }
    [[nodiscard]] StackFrame Next() const noexcept {
        return StackFrame(Address(base_.Read<std::uintptr_t>()));
    }
    [[nodiscard]] Address LocalAddress(std::size_t byte_offset) const noexcept {
        return base_.Subtract(static_cast<std::ptrdiff_t>(byte_offset));
    }
    [[nodiscard]] Address ArgumentAddress(std::size_t index) const noexcept {
        return base_.Add(static_cast<std::ptrdiff_t>((index + 2) * sizeof(std::uintptr_t)));
    }

private:
    Address base_;
};

} // namespace p2boost
