#pragma once

#include "shrewd/isa.hpp"

#include <cstdint>
#include <limits>

namespace shrewd {

constexpr Value wrap_add(Value a, Value b) noexcept {
    return static_cast<Value>(static_cast<std::uint64_t>(a) +
                              static_cast<std::uint64_t>(b));
}

constexpr Value wrap_sub(Value a, Value b) noexcept {
    return static_cast<Value>(static_cast<std::uint64_t>(a) -
                              static_cast<std::uint64_t>(b));
}

constexpr Value wrap_mul(Value a, Value b) noexcept {
    return static_cast<Value>(static_cast<std::uint64_t>(a) *
                              static_cast<std::uint64_t>(b));
}

inline constexpr Value kMinValue = std::numeric_limits<Value>::min();

constexpr Value safe_div(Value a, Value b) noexcept {
    if (b == 0)
        return 0;
    if (a == kMinValue && b == -1)
        return kMinValue;
    return a / b;
}

constexpr Value safe_mod(Value a, Value b) noexcept {
    if (b == 0)
        return 0;
    if (a == kMinValue && b == -1)
        return 0;
    return a % b;
}

} // namespace shrewd
