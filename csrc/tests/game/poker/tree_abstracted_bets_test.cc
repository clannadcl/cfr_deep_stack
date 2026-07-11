#include <stdexcept>
#include <vector>

#include "game/poker/abstracted_action.h"
#include "game/poker/poker_cards_isomorphic_index.h"
#include "game/poker/tree_abstracted_bets.h"

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
  using fisher::game::poker::AbstractedBetConfig;
  using fisher::game::poker::AbstractedBetStringConfig;
  using fisher::game::poker::AbstractedDonkBetStringConfig;
  using fisher::game::poker::PokerRound;
  using fisher::game::poker::TreeAbstractedBets;

  TreeAbstractedBets default_bets(AbstractedBetStringConfig{
      {"33%", "50%", "75%", "allin"}, {"100%", "allin"}});

  const std::vector<AbstractedAction>& preflop_open =
      default_bets.GetBets(PokerRound::kPreflop, 0);
  Expect(preflop_open.size() == 4, "preflop open bet count mismatch");
  Expect(preflop_open[0].ToString() == "percent:33",
         "preflop percent bet mismatch");
  Expect(preflop_open[3].ToString() == "allin", "preflop allin mismatch");

  const std::vector<AbstractedAction>& flop_raise =
      default_bets.GetBets(PokerRound::kFlop, 1);
  Expect(flop_raise.size() == 2, "flop raise bet count mismatch");
  Expect(flop_raise[0].ToString() == "percent:100",
         "flop raise percent mismatch");

  const std::vector<AbstractedAction>& river_late_raise =
      default_bets.GetBets(PokerRound::kRiver, 99);
  Expect(river_late_raise[0].ToString() == "percent:100",
         "late raise should use last config");
  Expect(default_bets.GetDonkBets(PokerRound::kTurn)[0].ToString() ==
             "percent:33",
         "default donk should use first raise config");

  TreeAbstractedBets street_bets(AbstractedBetStringConfig{{"33%"}});
  street_bets.SetStreetBets(PokerRound::kPreflop,
                            AbstractedBetStringConfig{{"2.5bb"}});
  street_bets.SetStreetBets(PokerRound::kTurn,
                            AbstractedBetStringConfig{{"50%"}});
  street_bets.SetDonkBets(PokerRound::kPreflop,
                          AbstractedDonkBetStringConfig{"1.5bb"});
  street_bets.SetDonkBets(PokerRound::kFlop,
                          AbstractedDonkBetStringConfig{"25%", "allin"});
  Expect(street_bets.GetBets(PokerRound::kPreflop, 0)[0].ToString() ==
             "bb:2.5",
         "preflop bb config mismatch");
  Expect(street_bets.GetBets(PokerRound::kTurn, 0)[0].ToString() ==
             "percent:50",
         "turn config mismatch");
  Expect(street_bets.GetDonkBets(PokerRound::kPreflop)[0].ToString() ==
             "bb:1.5",
         "preflop donk config mismatch");
  Expect(street_bets.GetDonkBets(PokerRound::kFlop)[1].ToString() == "allin",
         "flop donk allin mismatch");

  TreeAbstractedBets direct_bets(AbstractedBetConfig{
      {AbstractedAction::BetPercent(33.0f), AbstractedAction::AllIn()}});
  Expect(direct_bets.GetBets(PokerRound::kFlop, 0)[0].ToString() ==
             "percent:33",
         "direct config mismatch");

  TreeAbstractedBets default_donk_bets(
      AbstractedBetStringConfig{{"33%"}, {"100%"}},
      AbstractedDonkBetStringConfig{"2.5bb", "allin"});
  Expect(default_donk_bets.GetDonkBets(PokerRound::kRiver)[0].ToString() ==
             "bb:2.5",
         "default explicit donk mismatch");

  ExpectInvalidArgument(
      [] { TreeAbstractedBets invalid(AbstractedBetStringConfig{}); },
      "empty config should be invalid");
  ExpectInvalidArgument(
      [] { TreeAbstractedBets invalid(AbstractedBetStringConfig{{}}); },
      "empty raise config should be invalid");
  ExpectInvalidArgument(
      [] { TreeAbstractedBets invalid(AbstractedBetStringConfig{{"33"}}); },
      "bare number should be invalid");
  ExpectInvalidArgument(
      [] { TreeAbstractedBets invalid(AbstractedBetStringConfig{{"fold"}}); },
      "fold should be invalid");
  ExpectInvalidArgument(
      [] { TreeAbstractedBets invalid(AbstractedBetStringConfig{{"call"}}); },
      "call should be invalid");
  ExpectInvalidArgument(
      [] { TreeAbstractedBets invalid(AbstractedBetStringConfig{{"-1%"}}); },
      "negative percent should be invalid");
  ExpectInvalidArgument(
      [&] { default_bets.GetBets(PokerRound::kFlop, -1); },
      "negative raise count should be invalid");
  ExpectInvalidArgument(
      [&] {
        default_bets.SetStreetBets(
            PokerRound::kFlop,
            AbstractedBetConfig{{AbstractedAction::Check()}});
      },
      "direct check action should be invalid");
  ExpectInvalidArgument(
      [&] {
        default_bets.SetDonkBets(PokerRound::kFlop,
                                 AbstractedDonkBetStringConfig{});
      },
      "empty donk config should be invalid");
  ExpectInvalidArgument(
      [&] {
        default_bets.SetDonkBets(PokerRound::kFlop,
                                 AbstractedDonkBetStringConfig{"check"});
      },
      "check donk should be invalid");

  return 0;
}
