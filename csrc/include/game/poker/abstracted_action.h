#pragma once

#include <string>

namespace fisher::game::poker {

class AbstractedAction {
 public:
  static AbstractedAction Fold();
  static AbstractedAction Check();
  static AbstractedAction Call();
  static AbstractedAction BetPercent(float percent);
  static AbstractedAction BetBigBlind(float big_blind);
  static AbstractedAction AllIn();

  const std::string& Value() const;
  std::string ToString() const;

  bool operator==(const AbstractedAction& other) const;
  bool operator!=(const AbstractedAction& other) const;
  bool operator<(const AbstractedAction& other) const;

 private:
  explicit AbstractedAction(std::string value);

  std::string value_;
};

}  // namespace fisher::game::poker
