#include "game/poker/action.h"

#include <sstream>
#include <stdexcept>
#include <string>

namespace fisher::game::poker {

Action Action::Fold() { return Action(ActionType::kFold, 0); }

Action Action::Check() { return Action(ActionType::kCheck, 0); }

Action Action::Call() { return Action(ActionType::kCall, 0); }

Action Action::Bet(float amount) {
  if (amount < 0.0f) {
    throw std::invalid_argument("Action amount cannot be negative");
  }
  return Action(ActionType::kBet, MoneyToMilliBb(amount));
}

Action Action::Chance() { return Action(ActionType::kChance, 0); }

ActionType Action::Type() const { return type_; }

float Action::Amount() const {
  return MilliBbToMoney(amount_in_int_);
}

MoneyMilliBb Action::AmountInInt() const { return amount_in_int_; }

std::string Action::TypeString() const {
  switch (type_) {
    case ActionType::kFold:
      return "f";
    case ActionType::kCheck:
      return "x";
    case ActionType::kCall:
      return "c";
    case ActionType::kBet:
      return "b";
    case ActionType::kChance:
      return "C";
  }
  throw std::invalid_argument("Invalid action type");
}

std::string Action::ToString() const {
  if (!IsBet()) {
    return TypeString();
  }

  std::ostringstream output;
  output << TypeString() << Amount();
  return output.str();
}

bool Action::IsFold() const { return type_ == ActionType::kFold; }

bool Action::IsCheck() const { return type_ == ActionType::kCheck; }

bool Action::IsCall() const { return type_ == ActionType::kCall; }

bool Action::IsBet() const { return type_ == ActionType::kBet; }

bool Action::IsChance() const { return type_ == ActionType::kChance; }

bool Action::operator==(const Action& other) const {
  return type_ == other.type_ && amount_in_int_ == other.amount_in_int_;
}

bool Action::operator!=(const Action& other) const { return !(*this == other); }

bool Action::operator<(const Action& other) const {
  if (type_ != other.type_) {
    return static_cast<int>(type_) < static_cast<int>(other.type_);
  }
  return amount_in_int_ < other.amount_in_int_;
}

Action::Action(ActionType type, MoneyMilliBb amount_in_int)
    : type_(type), amount_in_int_(amount_in_int) {}

}  // namespace fisher::game::poker
