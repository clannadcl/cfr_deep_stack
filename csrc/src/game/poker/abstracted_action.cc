#include "game/poker/abstracted_action.h"

#include <cmath>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace fisher::game::poker {
namespace {

std::string FloatToString(float value) {
  std::ostringstream output;
  output << value;
  return output.str();
}

std::string AmountValue(const std::string& prefix, float amount) {
  if (amount < 0.0f) {
    throw std::invalid_argument("Action abstraction amount cannot be negative");
  }
  return prefix + FloatToString(amount);
}

std::string GeometricValue(float num_streets, float max_percent) {
  if (num_streets < 0.0f) {
    throw std::invalid_argument(
        "Geometric action abstraction streets cannot be negative");
  }
  if (max_percent < 0.0f) {
    throw std::invalid_argument(
        "Geometric action abstraction max percent cannot be negative");
  }
  std::string value = "geo:" + FloatToString(num_streets);
  if (std::isfinite(max_percent)) {
    value += ":" + FloatToString(max_percent);
  }
  return value;
}

}  // namespace

AbstractedAction AbstractedAction::Fold() {
  return AbstractedAction(AbstractedActionType::kFold, 0.0f, 0.0f, "fold");
}

AbstractedAction AbstractedAction::Check() {
  return AbstractedAction(AbstractedActionType::kCheck, 0.0f, 0.0f, "check");
}

AbstractedAction AbstractedAction::Call() {
  return AbstractedAction(AbstractedActionType::kCall, 0.0f, 0.0f, "call");
}

AbstractedAction AbstractedAction::BetPercent(float percent) {
  return AbstractedAction(AbstractedActionType::kBetPercent, percent,
                          0.0f,
                          AmountValue("percent:", percent));
}

AbstractedAction AbstractedAction::BetBigBlind(float big_blind) {
  return AbstractedAction(AbstractedActionType::kBetBigBlind, big_blind,
                          0.0f,
                          AmountValue("bb:", big_blind));
}

AbstractedAction AbstractedAction::BetPreviousBetMultiplier(float multiplier) {
  if (multiplier <= 1.0f) {
    throw std::invalid_argument(
        "Previous-bet multiplier must be greater than 1");
  }
  return AbstractedAction(AbstractedActionType::kBetPreviousBetMultiplier,
                          multiplier, 0.0f, AmountValue("x:", multiplier));
}

AbstractedAction AbstractedAction::BetGeometric(float num_streets) {
  return BetGeometric(num_streets, INFINITY);
}

AbstractedAction AbstractedAction::BetGeometric(float num_streets,
                                                float max_percent) {
  return AbstractedAction(AbstractedActionType::kBetGeometric, num_streets,
                          max_percent,
                          GeometricValue(num_streets, max_percent));
}

AbstractedAction AbstractedAction::AllIn() {
  return AbstractedAction(AbstractedActionType::kAllIn, 0.0f, 0.0f, "allin");
}

AbstractedActionType AbstractedAction::Type() const { return type_; }

float AbstractedAction::Amount() const { return amount_; }

float AbstractedAction::MaxPercent() const { return max_percent_; }

const std::string& AbstractedAction::Value() const { return value_; }

std::string AbstractedAction::ToString() const { return value_; }

bool AbstractedAction::operator==(const AbstractedAction& other) const {
  return value_ == other.value_;
}

bool AbstractedAction::operator!=(const AbstractedAction& other) const {
  return !(*this == other);
}

bool AbstractedAction::operator<(const AbstractedAction& other) const {
  return value_ < other.value_;
}

AbstractedAction::AbstractedAction(AbstractedActionType type, float amount,
                                   float max_percent,
                                   std::string value)
    : type_(type),
      amount_(amount),
      max_percent_(max_percent),
      value_(std::move(value)) {}

}  // namespace fisher::game::poker
