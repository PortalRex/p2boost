#pragma once

#include "address.hpp"

#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace p2boost::x86 {

// Encodes a five-byte near CALL after the caller has verified that the target
// build and call site are the expected ones. An empty result means rel32 cannot
// represent the target and must be treated as a failed signature check.
inline std::vector<std::uint8_t> RelativeCall(Address instruction, Address target) {
    if (!instruction.Valid() || !target.Valid()) {
        return {};
    }
    const auto displacement = static_cast<std::int64_t>(target.Value()) -
                              static_cast<std::int64_t>(instruction.Add(5).Value());
    if (displacement < (std::numeric_limits<std::int32_t>::min)() ||
        displacement > (std::numeric_limits<std::int32_t>::max)()) {
        return {};
    }

    std::vector<std::uint8_t> bytes(5);
    bytes[0] = 0xe8;
    const auto relative = static_cast<std::int32_t>(displacement);
    std::memcpy(bytes.data() + 1, &relative, sizeof(relative));
    return bytes;
}

} // namespace p2boost::x86
