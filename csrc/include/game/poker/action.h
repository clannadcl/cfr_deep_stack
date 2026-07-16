#pragma once

#include <string>

#include "game/poker/money.h"

namespace fisher::game::poker {

enum class ActionType {
  kFold = 0,
  kCheck = 1,
  kCall = 2,
  kBet = 3,
  kChance = 4,
};

class Action {
 public:
  static Action Fold();
  static Action Check();
  static Action Call();
  static Action Bet(float amount);
  static Action Chance();

  ActionType Type() const;
  float Amount() const;
  MoneyMilliBb AmountInInt() const;
  std::string TypeString() const;
  std::string ToString() const;

  bool IsFold() const;
  bool IsCheck() const;
  bool IsCall() const;
  bool IsBet() const;
  bool IsChance() const;

  bool operator==(const Action& other) const;
  bool operator!=(const Action& other) const;
  bool operator<(const Action& other) const;

 private:
  Action(ActionType type, MoneyMilliBb amount_in_int);

  ActionType type_;
  MoneyMilliBb amount_in_int_;
};

}  // namespace fisher::game::poker
