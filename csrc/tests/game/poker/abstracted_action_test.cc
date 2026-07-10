#include <algorithm>
#include <stdexcept>
#include <vector>

#include "game/poker/abstracted_action.h"

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
  using fisher::game::poker::AbstractedAction;

  const AbstractedAction fold = AbstractedAction::Fold();
  Expect(fold.ToString() == "fold", "fold string mismatch");
  Expect(fold.Value() == "fold", "fold value mismatch");

  const AbstractedAction check = AbstractedAction::Check();
  Expect(check.ToString() == "check", "check string mismatch");

  const AbstractedAction call = AbstractedAction::Call();
  Expect(call.ToString() == "call", "call string mismatch");

  const AbstractedAction percent = AbstractedAction::BetPercent(33.0f);
  Expect(percent.ToString() == "percent:33", "percent bet string mismatch");

  const AbstractedAction big_blind = AbstractedAction::BetBigBlind(35.5f);
  Expect(big_blind.ToString() == "bb:35.5", "bb bet string mismatch");

  const AbstractedAction allin = AbstractedAction::AllIn();
  Expect(allin.ToString() == "allin", "allin string mismatch");

  Expect(AbstractedAction::BetPercent(33.0f) ==
             AbstractedAction::BetPercent(33.0f),
         "equal abstractions mismatch");
  Expect(AbstractedAction::BetPercent(33.0f) !=
             AbstractedAction::BetPercent(66.0f),
         "different abstractions mismatch");

  std::vector<AbstractedAction> actions = {
      AbstractedAction::Fold(), AbstractedAction::AllIn(),
      AbstractedAction::BetPercent(33.0f), AbstractedAction::Call()};
  std::sort(actions.begin(), actions.end());
  Expect(actions.front().ToString() == "allin", "sorted first mismatch");
  Expect(actions.back().ToString() == "percent:33", "sorted last mismatch");

  ExpectInvalidArgument([] { AbstractedAction::BetPercent(-1.0f); },
                        "negative percent bet should be invalid");
  ExpectInvalidArgument([] { AbstractedAction::BetBigBlind(-1.0f); },
                        "negative bb bet should be invalid");

  return 0;
}
