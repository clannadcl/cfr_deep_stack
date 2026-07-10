#include <algorithm>
#include <stdexcept>
#include <vector>

#include "game/poker/action.h"

namespace {

void Expect(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

template <typename Fn>
void ExpectInvalidArgument(Fn fn, const char* message) {
  try {
    fn();
  } catch (const std::invalid_argument&) {
    return;
  }
  throw std::runtime_error(message);
}

}  // namespace

int main() {
  using fisher::game::poker::Action;
  using fisher::game::poker::ActionType;

  const Action fold = Action::Fold();
  Expect(fold.Type() == ActionType::kFold, "fold type mismatch");
  Expect(fold.IsFold(), "fold predicate mismatch");
  Expect(fold.TypeString() == "f", "fold type string mismatch");
  Expect(fold.ToString() == "f", "fold string mismatch");
  Expect(fold.AmountInInt() == 0, "fold amount mismatch");

  const Action check = Action::Check();
  Expect(check.IsCheck(), "check predicate mismatch");
  Expect(check.TypeString() == "x", "check type string mismatch");
  Expect(check.ToString() == "x", "check string mismatch");

  const Action call = Action::Call();
  Expect(call.IsCall(), "call predicate mismatch");
  Expect(call.TypeString() == "c", "call type string mismatch");
  Expect(call.ToString() == "c", "call string mismatch");

  const Action bet = Action::Bet(1.2345f);
  Expect(bet.IsBet(), "bet predicate mismatch");
  Expect(bet.TypeString() == "b", "bet type string mismatch");
  Expect(bet.AmountInInt() == 1235, "bet amount int should be rounded");
  Expect(bet.Amount() == 1.235f, "bet amount mismatch");
  Expect(Action::Bet(2.5f).ToString() == "b2.5", "bet string mismatch");

  const Action chance = Action::Chance();
  Expect(chance.IsChance(), "chance predicate mismatch");
  Expect(chance.TypeString() == "C", "chance type string mismatch");
  Expect(chance.ToString() == "C", "chance string mismatch");

  Expect(Action::Bet(1.0004f) == Action::Bet(1.00049f),
         "rounded equal bets should compare equal");
  Expect(Action::Bet(1.0004f) != Action::Bet(1.001f),
         "different rounded bets should not compare equal");

  std::vector<Action> actions = {
      Action::Chance(), Action::Bet(2.0f), Action::Fold(), Action::Call(),
      Action::Bet(1.0f), Action::Check()};
  std::sort(actions.begin(), actions.end());
  Expect(actions[0].IsFold(), "sorted fold mismatch");
  Expect(actions[1].IsCheck(), "sorted check mismatch");
  Expect(actions[2].IsCall(), "sorted call mismatch");
  Expect(actions[3] == Action::Bet(1.0f), "sorted small bet mismatch");
  Expect(actions[4] == Action::Bet(2.0f), "sorted large bet mismatch");
  Expect(actions[5].IsChance(), "sorted chance mismatch");

  ExpectInvalidArgument([] { Action::Bet(-0.001f); },
                        "negative bet should be invalid");

  return 0;
}
