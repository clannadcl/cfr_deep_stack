#pragma once

#include <string>

namespace fisher::game::poker {

enum class AbstractedActionType {
  kFold = 0,
  kCheck = 1,
  kCall = 2,
  kBetPercent = 3,
  kBetBigBlind = 4,
  kBetPreviousBetMultiplier = 5,
  kBetGeometric = 6,
  kAllIn = 7,
};

class AbstractedAction {
 public:
  static AbstractedAction Fold();
  static AbstractedAction Check();
  static AbstractedAction Call();
  static AbstractedAction BetPercent(float percent);
  static AbstractedAction BetBigBlind(float big_blind);
  static AbstractedAction BetPreviousBetMultiplier(float multiplier);
  static AbstractedAction BetGeometric(float num_streets);
  static AbstractedAction BetGeometric(float num_streets, float max_percent);
  static AbstractedAction AllIn();

  AbstractedActionType Type() const;
  float Amount() const;
  float MaxPercent() const;
  const std::string& Value() const;
  std::string ToString() const;

  bool operator==(const AbstractedAction& other) const;
  bool operator!=(const AbstractedAction& other) const;
  bool operator<(const AbstractedAction& other) const;

 private:
  AbstractedAction(AbstractedActionType type, float amount, float max_percent,
                   std::string value);

  AbstractedActionType type_;
  float amount_;
  float max_percent_;
  std::string value_;
};

}  // namespace fisher::game::poker
