#include "game/poker/abstracted_action.h"

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

}  // namespace

AbstractedAction AbstractedAction::Fold() {
  return AbstractedAction(AbstractedActionType::kFold, 0.0f, "fold");
}

AbstractedAction AbstractedAction::Check() {
  return AbstractedAction(AbstractedActionType::kCheck, 0.0f, "check");
}

AbstractedAction AbstractedAction::Call() {
  return AbstractedAction(AbstractedActionType::kCall, 0.0f, "call");
}

AbstractedAction AbstractedAction::BetPercent(float percent) {
  return AbstractedAction(AbstractedActionType::kBetPercent, percent,
                          AmountValue("percent:", percent));
}

AbstractedAction AbstractedAction::BetBigBlind(float big_blind) {
  return AbstractedAction(AbstractedActionType::kBetBigBlind, big_blind,
                          AmountValue("bb:", big_blind));
}

AbstractedAction AbstractedAction::AllIn() {
  return AbstractedAction(AbstractedActionType::kAllIn, 0.0f, "allin");
}

AbstractedActionType AbstractedAction::Type() const { return type_; }

float AbstractedAction::Amount() const { return amount_; }

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
                                   std::string value)
    : type_(type), amount_(amount), value_(std::move(value)) {}

}  // namespace fisher::game::poker
