#include <array>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "game/poker/action.h"
#include "game/poker/poker_cards.h"
#include "game/poker/poker_cards_isomorphic_index.h"
#include "game/poker/poker_hand.h"
#include "game/poker/subgame_setup.h"
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

std::vector<std::vector<float>> MatrixBelief(float value) {
  return std::vector<std::vector<float>>(
      fisher::game::poker::GameBasic::kNumPlayers,
      std::vector<float>(fisher::game::poker::GameBasic::kNumHands, value));
}

fisher::game::poker::SubgameSetup::Args MakeArgs(
    fisher::game::poker::SubgameSetup::RootBeliefInput root_belief) {
  using fisher::game::poker::Action;
  using fisher::game::poker::GameBasic;
  using fisher::game::poker::PokerCards;
  using fisher::game::poker::SubgameSetup;
  using fisher::game::poker::TreeAbstractedBets;

  return SubgameSetup::Args(
      PokerCards("AsKdQh"), 12.5f, std::array<float, 2>{90.0f, 88.0f},
      std::array<float, 2>{2.0f, 4.0f},
      std::array<float, 2>{2.0f, 4.0f}, /*current_player=*/1,
      /*last_aggressor=*/0, /*raise_count=*/1,
      std::vector<Action>{Action::Check(), Action::Bet(4.0f)},
      std::move(root_belief), TreeAbstractedBets(TreeAbstractedBets::Args{}),
      GameBasic(), /*bet_rounding=*/0.1f, /*min_raise_size=*/1.0f);
}

}  // namespace

int main() {
  using fisher::game::poker::Action;
  using fisher::game::poker::GameBasic;
  using fisher::game::poker::PokerCards;
  using fisher::game::poker::PokerHand;
  using fisher::game::poker::PokerRound;
  using fisher::game::poker::RakeConfig;
  using fisher::game::poker::SubgameSetup;
  using fisher::game::poker::TreeAbstractedBets;

  GameBasic game;

  std::vector<std::vector<float>> matrix_belief = MatrixBelief(2.0f);
  const int blocked_hand = game.HandIndex(PokerHand("As2c"));
  const int unblocked_hand = game.HandIndex(PokerHand("AcKh"));
  SubgameSetup setup(MakeArgs(matrix_belief));

  Expect(setup.Board().ToString() == "AsKdQh", "board mismatch");
  Expect(setup.Street() == PokerRound::kFlop, "street mismatch");
  Expect(setup.Pot() == 12.5f, "pot mismatch");
  Expect(setup.Stacks()[0] == 90.0f, "player 0 stack mismatch");
  Expect(setup.Stacks()[1] == 88.0f, "player 1 stack mismatch");
  Expect(setup.BetTotal()[0] == 2.0f, "player 0 total bet mismatch");
  Expect(setup.BetTotal()[1] == 4.0f, "player 1 total bet mismatch");
  Expect(setup.BetCurrentRound()[0] == 2.0f, "player 0 bet mismatch");
  Expect(setup.BetCurrentRound()[1] == 4.0f, "player 1 bet mismatch");
  Expect(setup.CurrentPlayer() == 1, "current player mismatch");
  Expect(setup.LastAggressor() == 0, "last aggressor mismatch");
  Expect(setup.RaiseCount() == 1, "raise count mismatch");
  Expect(setup.RootActionHistory().size() == 2, "action history mismatch");
  Expect(setup.BetRounding() == 0.1f, "bet rounding mismatch");
  Expect(setup.MinRaiseSize() == 1.0f, "min raise size mismatch");
  Expect(setup.RootBelief().Belief()[0][blocked_hand] == 0.0f,
         "blocked hand should be zeroed for player 0");
  Expect(setup.RootBelief().Belief()[1][blocked_hand] == 0.0f,
         "blocked hand should be zeroed for player 1");
  Expect(setup.RootBelief().Belief()[0][unblocked_hand] == 2.0f,
         "unblocked hand should remain unnormalized");
  Expect(setup.AbstractedBets().BetToAllInThreshold() == 75.0f,
         "abstracted bets should be preserved");
  Expect(!setup.BasicGame().Rake().enabled, "default rake should be disabled");

  SubgameSetup turn_setup(SubgameSetup::Args(
      PokerCards("AsKdQh2c"), 10.0f, std::array<float, 2>{50.0f, 50.0f},
      std::array<float, 2>{0.0f, 0.0f},
      std::array<float, 2>{0.0f, 0.0f}, 0, -1, 0, {},
      MatrixBelief(1.0f), TreeAbstractedBets(TreeAbstractedBets::Args{}),
      GameBasic(), 0.1f, 1.0f));
  Expect(turn_setup.Street() == PokerRound::kTurn, "turn street mismatch");

  SubgameSetup river_setup(SubgameSetup::Args(
      PokerCards("AsKdQh2c3d"), 10.0f, std::array<float, 2>{50.0f, 50.0f},
      std::array<float, 2>{0.0f, 0.0f},
      std::array<float, 2>{0.0f, 0.0f}, 0, -1, 0, {},
      MatrixBelief(1.0f), TreeAbstractedBets(TreeAbstractedBets::Args{}),
      GameBasic(), 0.1f, 1.0f));
  Expect(river_setup.Street() == PokerRound::kRiver, "river street mismatch");

  SubgameSetup range_setup(MakeArgs(
      std::vector<std::string>{"AcKh:3,As2c:5", "AcKh:7,As2c:11"}));
  Expect(range_setup.RootBelief().Belief()[0][blocked_hand] == 0.0f,
         "range blocked hand should be zeroed");
  Expect(range_setup.RootBelief().Belief()[0][unblocked_hand] == 3.0f,
         "range unblocked hand should stay unnormalized");

  auto custom_args = MakeArgs(MatrixBelief(1.0f));
  TreeAbstractedBets::Args abstracted_args;
  abstracted_args.bet_to_allin_threshold = 55.0f;
  custom_args.abstracted_bets = TreeAbstractedBets(abstracted_args);
  custom_args.game_basic = GameBasic(RakeConfig{/*enabled=*/true,
                                                /*percentage=*/0.05,
                                                /*cap=*/0.6});
  SubgameSetup custom_setup(custom_args);
  Expect(custom_setup.AbstractedBets().BetToAllInThreshold() == 55.0f,
         "custom abstracted bets should be preserved");
  Expect(custom_setup.BasicGame().Rake().enabled,
         "custom game basic should preserve rake");
  Expect(custom_setup.BasicGame().Rake().percentage == 0.05,
         "custom rake percentage mismatch");

  ExpectInvalidArgument(
      [] {
        auto args = MakeArgs(MatrixBelief(1.0f));
        args.board = PokerCards("As");
        SubgameSetup invalid(args);
      },
      "one-card board should be invalid");
  ExpectInvalidArgument(
      [] {
        auto args = MakeArgs(MatrixBelief(1.0f));
        args.board = PokerCards("AsKd");
        SubgameSetup invalid(args);
      },
      "two-card board should be invalid");
  ExpectInvalidArgument(
      [] {
        auto args = MakeArgs(MatrixBelief(1.0f));
        args.board = PokerCards("AsKdQh2c3d4s");
        SubgameSetup invalid(args);
      },
      "six-card board should be invalid");
  ExpectInvalidArgument(
      [] {
        auto args = MakeArgs(MatrixBelief(1.0f));
        args.pot = -1.0f;
        SubgameSetup invalid(args);
      },
      "negative pot should be invalid");
  ExpectInvalidArgument(
      [] {
        auto args = MakeArgs(MatrixBelief(1.0f));
        args.stacks[0] = -1.0f;
        SubgameSetup invalid(args);
      },
      "negative stack should be invalid");
  ExpectInvalidArgument(
      [] {
        auto args = MakeArgs(MatrixBelief(1.0f));
        args.bet_current_round[1] = -1.0f;
        SubgameSetup invalid(args);
      },
      "negative current-round bet should be invalid");
  ExpectInvalidArgument(
      [] {
        auto args = MakeArgs(MatrixBelief(1.0f));
        args.bet_total[1] = -1.0f;
        SubgameSetup invalid(args);
      },
      "negative total bet should be invalid");
  ExpectInvalidArgument(
      [] {
        auto args = MakeArgs(MatrixBelief(1.0f));
        args.bet_total[1] = 3.0f;
        SubgameSetup invalid(args);
      },
      "total bet below current-round bet should be invalid");
  ExpectInvalidArgument(
      [] {
        auto args = MakeArgs(MatrixBelief(1.0f));
        args.current_player = 2;
        SubgameSetup invalid(args);
      },
      "invalid current player should be invalid");
  ExpectInvalidArgument(
      [] {
        auto args = MakeArgs(MatrixBelief(1.0f));
        args.last_aggressor = 2;
        SubgameSetup invalid(args);
      },
      "invalid last aggressor should be invalid");
  ExpectInvalidArgument(
      [] {
        auto args = MakeArgs(MatrixBelief(1.0f));
        args.raise_count = -1;
        SubgameSetup invalid(args);
      },
      "negative raise count should be invalid");
  ExpectInvalidArgument(
      [] {
        auto args = MakeArgs(std::vector<std::vector<float>>{
            std::vector<float>(GameBasic::kNumHands, 1.0f)});
        SubgameSetup invalid(args);
      },
      "one-player belief should be invalid");
  ExpectInvalidArgument(
      [] {
        auto args = MakeArgs(std::vector<std::vector<float>>{
            std::vector<float>(GameBasic::kNumHands, 1.0f),
            std::vector<float>(GameBasic::kNumHands - 1, 1.0f)});
        SubgameSetup invalid(args);
      },
      "wrong hand count should be invalid");
  ExpectInvalidArgument(
      [] {
        auto args = MakeArgs(MatrixBelief(1.0f));
        args.bet_rounding = 0.0f;
        SubgameSetup invalid(args);
      },
      "non-positive bet rounding should be invalid");
  ExpectInvalidArgument(
      [] {
        auto args = MakeArgs(MatrixBelief(1.0f));
        args.min_raise_size = 0.0f;
        SubgameSetup invalid(args);
      },
      "non-positive min raise size should be invalid");

  return 0;
}
