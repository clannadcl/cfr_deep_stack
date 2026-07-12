#pragma once

#include <string>

namespace fisher::game::poker {

enum class AbstractedActionType {
  kFold = 0,
  kCheck = 1,
  kCall = 2,
  kBetPercent = 3,
  kBetBigBlind = 4,
  kAllIn = 5,
};

class AbstractedAction {
 public:
  static AbstractedAction Fold();
  static AbstractedAction Check();
  static AbstractedAction Call();
  static AbstractedAction BetPercent(float percent);
  static AbstractedAction BetBigBlind(float big_blind);
  static AbstractedAction AllIn();

  AbstractedActionType Type() const;
  float Amount() const;
  const std::string& Value() const;
  std::string ToString() const;

  bool operator==(const AbstractedAction& other) const;
  bool operator!=(const AbstractedAction& other) const;
  bool operator<(const AbstractedAction& other) const;

 private:
  AbstractedAction(AbstractedActionType type, float amount,
                   std::string value);

  AbstractedActionType type_;
  float amount_;
  std::string value_;
};

}  // namespace fisher::game::poker
