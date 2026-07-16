#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace fisher::game::poker {

using MoneyMilliBb = std::int64_t;

inline MoneyMilliBb MoneyToMilliBb(float amount) {
  if (!std::isfinite(amount)) {
    throw std::invalid_argument("Money amount must be finite");
  }
  return static_cast<MoneyMilliBb>(std::llround(amount * 1000.0f));
}

inline float MilliBbToMoney(MoneyMilliBb amount) {
  return static_cast<float>(amount) / 1000.0f;
}

inline std::array<MoneyMilliBb, 2> MoneyArrayToMilliBb(
    std::array<float, 2> amounts) {
  return {MoneyToMilliBb(amounts[0]), MoneyToMilliBb(amounts[1])};
}

inline std::array<float, 2> MilliBbArrayToMoney(
    std::array<MoneyMilliBb, 2> amounts) {
  return {MilliBbToMoney(amounts[0]), MilliBbToMoney(amounts[1])};
}

}  // namespace fisher::game::poker
