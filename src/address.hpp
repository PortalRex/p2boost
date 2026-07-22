#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace p2boost {

// A typed view of an address in Portal 2's 32-bit process. Keeping address
// arithmetic here makes hook code read in terms of RVAs instead of casts.
class Address {
public:
    constexpr Address() noexcept = default;
    explicit constexpr Address(std::uintptr_t value) noexcept : value_(value) {}
    explicit Address(const void* value) noexcept
        : value_(reinterpret_cast<std::uintptr_t>(value)) {}
    template <typename T, std::enable_if_t<std::is_pointer_v<T>, int> = 0>
    explicit Address(T value) noexcept : value_(reinterpret_cast<std::uintptr_t>(value)) {}

    [[nodiscard]] constexpr bool Valid() const noexcept { return value_ != 0; }
    [[nodiscard]] constexpr std::uintptr_t Value() const noexcept { return value_; }

    template <typename T> [[nodiscard]] T As() const noexcept {
        static_assert(std::is_pointer_v<T>, "Address::As requires a pointer type");
        return reinterpret_cast<T>(value_);
    }

    [[nodiscard]] constexpr Address Add(std::ptrdiff_t offset) const noexcept {
        return Valid() ? Address(value_ + static_cast<std::uintptr_t>(offset)) : Address{};
    }

    [[nodiscard]] constexpr Address Subtract(std::ptrdiff_t offset) const noexcept {
        return Valid() ? Address(value_ - static_cast<std::uintptr_t>(offset)) : Address{};
    }

    template <typename T> [[nodiscard]] T Read(std::ptrdiff_t offset = 0) const noexcept {
        static_assert(std::is_trivially_copyable_v<T>, "Address::Read requires a trivial type");
        T result{};
        if (Valid()) {
            std::memcpy(&result, Add(offset).As<const void*>(), sizeof(result));
        }
        return result;
    }

    // Resolves the signed displacement used by x86 CALL/JMP rel32 operands.
    [[nodiscard]] Address Relative32(std::ptrdiff_t displacement_offset = 0) const noexcept {
        if (!Valid()) {
            return {};
        }
        const Address displacement = Add(displacement_offset);
        return displacement.Add(sizeof(std::int32_t)).Add(displacement.Read<std::int32_t>());
    }

private:
    std::uintptr_t value_ = 0;
};

} // namespace p2boost
