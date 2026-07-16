#include <array>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "game/poker/action.h"
#include "game/poker/game_basic.h"
#include "game/poker/hand_evaluator.h"
#include "game/poker/isomorphic_mapping.h"
#include "game/poker/node_state.h"
#include "game/poker/poker_cards.h"
#include "game/poker/poker_hand.h"
#include "game/poker/subgame_setup.h"
#include "game/poker/terminal_cfv_calculator.h"
#include "game/poker/tree_abstracted_bets.h"

namespace {

void Expect(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void ExpectNear(float actual, float expected, const char* message) {
  if (std::fabs(actual - expected) > 1e-5f) {
    throw std::runtime_error(message);
  }
}

void ExpectNear(float actual, float expected, float tolerance,
                const char* message) {
  if (std::fabs(actual - expected) > tolerance) {
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

std::shared_ptr<fisher::game::poker::SubgameSetup> MakeSetup(
    const fisher::game::poker::PokerCards& board, float pot,
    std::array<float, 2> stacks, std::array<float, 2> bet_total,
    std::array<float, 2> bet_current_round,
    fisher::game::poker::GameBasic game_basic =
        fisher::game::poker::GameBasic()) {
  using fisher::game::poker::Action;
  using fisher::game::poker::SubgameSetup;
  using fisher::game::poker::TreeAbstractedBets;

  return std::make_shared<SubgameSetup>(SubgameSetup::Args(
      board, pot, stacks, bet_total, bet_current_round, /*current_player=*/0,
      /*last_aggressor=*/-1, /*raise_count=*/0, std::vector<Action>{},
      MatrixBelief(1.0f), TreeAbstractedBets(TreeAbstractedBets::Args{}),
      std::move(game_basic), /*bet_rounding=*/0.1f,
      /*min_raise_size=*/1.0f));
}

fisher::game::poker::NodeState MakeTerminalNode(
    std::shared_ptr<const fisher::game::poker::SubgameSetup> setup,
    fisher::game::poker::TerminalStatus status,
    std::array<bool, 2> is_fold) {
  using fisher::game::poker::NodeState;

  return NodeState(NodeState::Args(
      setup, setup->Board(), setup->Pot(), setup->Stacks(),
      setup->BetTotal(), setup->BetCurrentRound(),
      NodeState::kTerminalPlayer, setup->LastAggressor(), setup->RaiseCount(),
      is_fold, status, setup->RootActionHistory()));
}

std::vector<bool> PossibleHands(
    const fisher::game::poker::GameBasic& game_basic,
    const std::vector<std::string>& hands) {
  std::vector<bool> possible(fisher::game::poker::GameBasic::kNumHands, false);
  for (const std::string& hand : hands) {
    possible[static_cast<std::size_t>(
        game_basic.HandIndex(fisher::game::poker::PokerHand(hand)))] = true;
  }
  return possible;
}

int IsoIndex(const fisher::game::poker::GameBasic& game_basic,
             const fisher::game::poker::IsomorphicMapping& mapping,
             const char* hand) {
  return mapping.RawToIso(
      game_basic.HandIndex(fisher::game::poker::PokerHand(hand)));
}

std::vector<float> ReachFor(
    const fisher::game::poker::GameBasic& game_basic,
    const fisher::game::poker::IsomorphicMapping& mapping,
    const char* hand, float reach) {
  std::vector<float> values(static_cast<std::size_t>(mapping.NumIsoHands()),
                            0.0f);
  values[static_cast<std::size_t>(IsoIndex(game_basic, mapping, hand))] = reach;
  return values;
}

void AddReachFor(const fisher::game::poker::GameBasic& game_basic,
                 const fisher::game::poker::IsomorphicMapping& mapping,
                 const char* hand, float reach,
                 std::vector<float>* values) {
  (*values)[static_cast<std::size_t>(IsoIndex(game_basic, mapping, hand))] +=
      reach;
}

}  // namespace

int main() {
  using fisher::game::poker::GameBasic;
  using fisher::game::poker::IsomorphicMapping;
  using fisher::game::poker::NodeState;
  using fisher::game::poker::PokerCards;
  using fisher::game::poker::RakeConfig;
  using fisher::game::poker::SevenCardLookupTable;
  using fisher::game::poker::TerminalCfvCalculator;
  using fisher::game::poker::TerminalStatus;

  GameBasic game_basic;
  SevenCardLookupTable evaluator;
  TerminalCfvCalculator calculator(game_basic, evaluator);

  {
    const PokerCards board("AsKdQh");
    auto setup = MakeSetup(board, 10.0f, {95.0f, 100.0f}, {5.0f, 0.0f},
                           {5.0f, 0.0f});
    const NodeState node =
        MakeTerminalNode(setup, TerminalStatus::kFoldTerminal, {false, true});
    IsomorphicMapping mapping(
        game_basic, board,
        PossibleHands(game_basic, {"AcAh", "2c2d", "3c3d"}));
    const std::vector<float> opponent_reach =
        ReachFor(game_basic, mapping, "2c2d", 1.0f);

    const std::vector<float> winner_cfv =
        calculator.Calculate(node, /*player=*/0, mapping, opponent_reach);
    ExpectNear(winner_cfv[static_cast<std::size_t>(
                   IsoIndex(game_basic, mapping, "AcAh"))],
               5.0f, "fold winner CFV mismatch");
    ExpectNear(winner_cfv[static_cast<std::size_t>(
                   IsoIndex(game_basic, mapping, "2c2d"))],
               0.0f, "fold blocker CFV should be zero");

    const std::vector<float> folded_cfv =
        calculator.Calculate(node, /*player=*/1, mapping, opponent_reach);
    ExpectNear(folded_cfv[static_cast<std::size_t>(
                   IsoIndex(game_basic, mapping, "AcAh"))],
               -5.0f, "folded player CFV mismatch");
  }

  {
    const PokerCards board("2c3d4h");
    auto setup = MakeSetup(board, 10.0f, {95.0f, 100.0f}, {5.0f, 0.0f},
                           {5.0f, 0.0f});
    const NodeState node =
        MakeTerminalNode(setup, TerminalStatus::kFoldTerminal, {false, true});
    IsomorphicMapping mapping(
        game_basic, board,
        PossibleHands(game_basic, {"AcAh", "AcKd", "QcQd"}));
    std::vector<float> opponent_reach(
        static_cast<std::size_t>(mapping.NumIsoHands()), 0.0f);
    AddReachFor(game_basic, mapping, "AcKd", 7.0f, &opponent_reach);
    AddReachFor(game_basic, mapping, "QcQd", 3.0f, &opponent_reach);

    const std::vector<float> winner_cfv =
        calculator.Calculate(node, /*player=*/0, mapping, opponent_reach);
    ExpectNear(winner_cfv[static_cast<std::size_t>(
                   IsoIndex(game_basic, mapping, "AcAh"))],
               15.0f, "fold CFV should exclude card-blocked reach");
  }

  {
    const PokerCards board("2c7dJhQsAc");
    auto setup = MakeSetup(board, 20.0f, {100.0f, 100.0f}, {0.0f, 0.0f},
                           {0.0f, 0.0f});
    const NodeState node = MakeTerminalNode(
        setup, TerminalStatus::kShowdownTerminal, {false, false});
    const std::vector<std::string> hands = {
        "KdKh", "KsKc", "TdTh", "9c9d", "8s8h", "3c3d"};
    IsomorphicMapping mapping(game_basic, board,
                              PossibleHands(game_basic, hands));
    std::vector<float> opponent_reach(
        static_cast<std::size_t>(mapping.NumIsoHands()), 0.0f);
    AddReachFor(game_basic, mapping, "KdKh", 0.421f, &opponent_reach);
    AddReachFor(game_basic, mapping, "KsKc", 0.792f, &opponent_reach);
    AddReachFor(game_basic, mapping, "TdTh", 0.163f, &opponent_reach);
    AddReachFor(game_basic, mapping, "9c9d", 0.534f, &opponent_reach);
    AddReachFor(game_basic, mapping, "8s8h", 0.905f, &opponent_reach);
    AddReachFor(game_basic, mapping, "3c3d", 0.276f, &opponent_reach);

    const std::vector<float> cfv =
        calculator.Calculate(node, /*player=*/0, mapping, opponent_reach);
    ExpectNear(cfv[static_cast<std::size_t>(
                   IsoIndex(game_basic, mapping, "KdKh"))],
               18.77999878f, 1e-5f,
               "cross-repo river KdKh CFV mismatch");
    ExpectNear(cfv[static_cast<std::size_t>(
                   IsoIndex(game_basic, mapping, "KsKc"))],
               18.77999878f, 1e-5f,
               "cross-repo river KsKc CFV mismatch");
    ExpectNear(cfv[static_cast<std::size_t>(
                   IsoIndex(game_basic, mapping, "TdTh"))],
               5.01999807f, 1e-5f,
               "cross-repo river TdTh CFV mismatch");
    ExpectNear(cfv[static_cast<std::size_t>(
                   IsoIndex(game_basic, mapping, "9c9d"))],
               -1.94999993f, 1e-5f,
               "cross-repo river 9c9d CFV mismatch");
    ExpectNear(cfv[static_cast<std::size_t>(
                   IsoIndex(game_basic, mapping, "8s8h"))],
               -16.34000206f, 1e-5f,
               "cross-repo river 8s8h CFV mismatch");
    ExpectNear(cfv[static_cast<std::size_t>(
                   IsoIndex(game_basic, mapping, "3c3d"))],
               -28.14999962f, 1e-5f,
               "cross-repo river 3c3d CFV mismatch");
  }

  {
    const PokerCards board("2d7hJcQdAs");
    auto setup = MakeSetup(board, 20.0f, {100.0f, 100.0f}, {0.0f, 0.0f},
                           {0.0f, 0.0f});
    const NodeState node = MakeTerminalNode(
        setup, TerminalStatus::kShowdownTerminal, {false, false});
    const std::vector<std::string> hands = {
        "KdKh", "KsKc", "TdTh", "9c9d", "8s8h", "3c3d"};
    IsomorphicMapping mapping(game_basic, board,
                              PossibleHands(game_basic, hands));
    std::vector<float> opponent_reach(
        static_cast<std::size_t>(mapping.NumIsoHands()), 0.0f);
    AddReachFor(game_basic, mapping, "KdKh", 0.669f, &opponent_reach);
    AddReachFor(game_basic, mapping, "KsKc", 0.288f, &opponent_reach);
    AddReachFor(game_basic, mapping, "TdTh", 0.907f, &opponent_reach);
    AddReachFor(game_basic, mapping, "9c9d", 0.526f, &opponent_reach);
    AddReachFor(game_basic, mapping, "8s8h", 0.145f, &opponent_reach);
    AddReachFor(game_basic, mapping, "3c3d", 0.764f, &opponent_reach);

    const std::vector<float> cfv =
        calculator.Calculate(node, /*player=*/0, mapping, opponent_reach);
    ExpectNear(cfv[static_cast<std::size_t>(
                   IsoIndex(game_basic, mapping, "KdKh"))],
               23.42000580f, 1e-5f,
               "cross-repo second river KdKh CFV mismatch");
    ExpectNear(cfv[static_cast<std::size_t>(
                   IsoIndex(game_basic, mapping, "KsKc"))],
               23.42000580f, 1e-5f,
               "cross-repo second river KsKc CFV mismatch");
    ExpectNear(cfv[static_cast<std::size_t>(
                   IsoIndex(game_basic, mapping, "TdTh"))],
               4.78000212f, 1e-5f,
               "cross-repo second river TdTh CFV mismatch");
    ExpectNear(cfv[static_cast<std::size_t>(
                   IsoIndex(game_basic, mapping, "9c9d"))],
               -9.55000019f, 1e-5f,
               "cross-repo second river 9c9d CFV mismatch");
    ExpectNear(cfv[static_cast<std::size_t>(
                   IsoIndex(game_basic, mapping, "8s8h"))],
               -16.26000023f, 1e-5f,
               "cross-repo second river 8s8h CFV mismatch");
    ExpectNear(cfv[static_cast<std::size_t>(
                   IsoIndex(game_basic, mapping, "3c3d"))],
               -25.35000229f, 1e-5f,
               "cross-repo second river 3c3d CFV mismatch");
  }

  {
    const PokerCards board("2c7dJhQs");
    auto setup = MakeSetup(board, 20.0f, {0.0f, 0.0f}, {100.0f, 100.0f},
                           {100.0f, 100.0f});
    const NodeState node = MakeTerminalNode(
        setup, TerminalStatus::kShowdownTerminal, {false, false});
    const std::vector<std::string> hands = {
        "KdKh", "KsKc", "TdTh", "9c9d", "8s8h", "3c3d"};
    IsomorphicMapping mapping(game_basic, board,
                              PossibleHands(game_basic, hands));
    std::vector<float> opponent_reach(
        static_cast<std::size_t>(mapping.NumIsoHands()), 0.0f);
    for (const std::string& hand : hands) {
      AddReachFor(game_basic, mapping, hand.c_str(), 1.0f, &opponent_reach);
    }

    const std::vector<float> cfv =
        calculator.Calculate(node, /*player=*/0, mapping, opponent_reach);
    ExpectNear(cfv[static_cast<std::size_t>(
                   IsoIndex(game_basic, mapping, "KdKh"))],
               400.00003815f, 1e-4f,
               "cross-repo turn all-in KdKh CFV mismatch");
    ExpectNear(cfv[static_cast<std::size_t>(
                   IsoIndex(game_basic, mapping, "KsKc"))],
               400.00003815f, 1e-4f,
               "cross-repo turn all-in KsKc CFV mismatch");
    ExpectNear(cfv[static_cast<std::size_t>(
                   IsoIndex(game_basic, mapping, "TdTh"))],
               100.0f, 1e-4f,
               "cross-repo turn all-in TdTh CFV mismatch");
    ExpectNear(cfv[static_cast<std::size_t>(
                   IsoIndex(game_basic, mapping, "9c9d"))],
               -99.99999045f, 1e-4f,
               "cross-repo turn all-in 9c9d CFV mismatch");
    ExpectNear(cfv[static_cast<std::size_t>(
                   IsoIndex(game_basic, mapping, "8s8h"))],
               -300.0f, 1e-4f,
               "cross-repo turn all-in 8s8h CFV mismatch");
    ExpectNear(cfv[static_cast<std::size_t>(
                   IsoIndex(game_basic, mapping, "3c3d"))],
               -500.00003815f, 1e-4f,
               "cross-repo turn all-in 3c3d CFV mismatch");
  }

  {
    const PokerCards board("AsKsQsJsTs");
    auto setup = MakeSetup(board, 20.0f, {100.0f, 100.0f}, {0.0f, 0.0f},
                           {0.0f, 0.0f});
    const NodeState node = MakeTerminalNode(
        setup, TerminalStatus::kShowdownTerminal, {false, false});
    IsomorphicMapping mapping(
        game_basic, board,
        PossibleHands(game_basic, {"2c2d", "3c3d", "4c4d"}));
    const std::vector<float> opponent_reach =
        ReachFor(game_basic, mapping, "3c3d", 2.0f);

    const std::vector<float> cfv =
        calculator.Calculate(node, /*player=*/0, mapping, opponent_reach);
    ExpectNear(cfv[static_cast<std::size_t>(
                   IsoIndex(game_basic, mapping, "2c2d"))],
               0.0f, "unraked river tie CFV mismatch");
  }

  {
    const PokerCards board("AsKsQsJsTs");
    GameBasic raked_game(RakeConfig{/*enabled=*/true,
                                    /*percentage=*/0.05,
                                    /*cap=*/100.0});
    TerminalCfvCalculator raked_calculator(raked_game, evaluator);
    auto setup = MakeSetup(board, 20.0f, {100.0f, 100.0f}, {0.0f, 0.0f},
                           {0.0f, 0.0f}, raked_game);
    const NodeState node = MakeTerminalNode(
        setup, TerminalStatus::kShowdownTerminal, {false, false});
    IsomorphicMapping mapping(
        raked_game, board,
        PossibleHands(raked_game, {"2c2d", "3c3d", "4c4d"}));
    const std::vector<float> opponent_reach =
        ReachFor(raked_game, mapping, "3c3d", 2.0f);

    const std::vector<float> cfv =
        raked_calculator.Calculate(node, /*player=*/0, mapping,
                                   opponent_reach);
    ExpectNear(cfv[static_cast<std::size_t>(
                   IsoIndex(raked_game, mapping, "2c2d"))],
               -1.0f, "raked river tie CFV mismatch");
    ExpectNear(cfv[static_cast<std::size_t>(
                   IsoIndex(raked_game, mapping, "3c3d"))],
               0.0f, "same hand collision should remove tie mass");
  }

  {
    const PokerCards board("AcAdAhAs2c");
    GameBasic raked_game(RakeConfig{/*enabled=*/true,
                                    /*percentage=*/0.05,
                                    /*cap=*/100.0});
    TerminalCfvCalculator raked_calculator(raked_game, evaluator);
    auto setup = MakeSetup(board, 20.0f, {100.0f, 100.0f}, {0.0f, 0.0f},
                           {0.0f, 0.0f}, raked_game);
    const NodeState node = MakeTerminalNode(
        setup, TerminalStatus::kShowdownTerminal, {false, false});
    IsomorphicMapping mapping(
        raked_game, board,
        PossibleHands(raked_game, {"KcKd", "QcQd"}));
    const std::vector<float> opponent_reach =
        ReachFor(raked_game, mapping, "QcQd", 1.0f);

    const std::vector<float> cfv =
        raked_calculator.Calculate(node, /*player=*/0, mapping,
                                   opponent_reach);
    ExpectNear(cfv[static_cast<std::size_t>(
                   IsoIndex(raked_game, mapping, "KcKd"))],
               9.0f, "raked river always-win CFV mismatch");
  }

  {
    const PokerCards board("AcAdAhAs2c");
    auto setup = MakeSetup(board, 20.0f, {100.0f, 100.0f}, {0.0f, 0.0f},
                           {0.0f, 0.0f});
    const NodeState node = MakeTerminalNode(
        setup, TerminalStatus::kShowdownTerminal, {false, false});
    IsomorphicMapping mapping(
        game_basic, board,
        PossibleHands(game_basic, {"KcKd", "QcQd"}));
    const std::vector<float> opponent_reach =
        ReachFor(game_basic, mapping, "QcQd", 1.0f);

    const std::vector<float> cfv =
        calculator.Calculate(node, /*player=*/0, mapping, opponent_reach);
    ExpectNear(cfv[static_cast<std::size_t>(
                   IsoIndex(game_basic, mapping, "KcKd"))],
               10.0f, "river win CFV mismatch");
    ExpectNear(cfv[static_cast<std::size_t>(
                   IsoIndex(game_basic, mapping, "QcQd"))],
               0.0f, "river same hand blocker CFV mismatch");
  }

  {
    const PokerCards board("AcAdAh");
    auto setup = MakeSetup(board, 2.0f, {100.0f, 100.0f}, {0.0f, 0.0f},
                           {0.0f, 0.0f});
    const NodeState node = MakeTerminalNode(
        setup, TerminalStatus::kShowdownTerminal, {false, false});
    IsomorphicMapping mapping(
        game_basic, board,
        PossibleHands(game_basic, {"KcKd", "QcQd"}));
    const std::vector<float> opponent_reach =
        ReachFor(game_basic, mapping, "QcQd", 1.0f);

    const std::vector<float> cfv =
        calculator.Calculate(node, /*player=*/0, mapping, opponent_reach);
    ExpectNear(cfv[static_cast<std::size_t>(
                   IsoIndex(game_basic, mapping, "KcKd"))],
               986.0f / 990.0f, "flop all-in CFV mismatch");
    ExpectNear(cfv[static_cast<std::size_t>(
                   IsoIndex(game_basic, mapping, "QcQd"))],
               0.0f, "flop same hand blocker CFV mismatch");
  }

  {
    const PokerCards board("AcAdAhAs");
    auto setup = MakeSetup(board, 2.0f, {100.0f, 100.0f}, {0.0f, 0.0f},
                           {0.0f, 0.0f});
    const NodeState node = MakeTerminalNode(
        setup, TerminalStatus::kShowdownTerminal, {false, false});
    IsomorphicMapping mapping(
        game_basic, board,
        PossibleHands(game_basic, {"KcKd", "QcQd"}));
    const std::vector<float> opponent_reach =
        ReachFor(game_basic, mapping, "QcQd", 1.0f);

    const std::vector<float> cfv =
        calculator.Calculate(node, /*player=*/0, mapping, opponent_reach);
    ExpectNear(cfv[static_cast<std::size_t>(
                   IsoIndex(game_basic, mapping, "KcKd"))],
               42.0f / 44.0f, "turn all-in CFV mismatch");
    ExpectNear(cfv[static_cast<std::size_t>(
                   IsoIndex(game_basic, mapping, "QcQd"))],
               0.0f, "turn same hand blocker CFV mismatch");
  }

  {
    const PokerCards board("AcAdAhAs");
    auto setup = MakeSetup(board, 2.0f, {100.0f, 100.0f}, {0.0f, 0.0f},
                           {0.0f, 0.0f});
    const NodeState non_terminal = setup->GetRootNodeState();
    IsomorphicMapping mapping(
        game_basic, board,
        PossibleHands(game_basic, {"KcKd", "QcQd"}));
    std::vector<float> opponent_reach(
        static_cast<std::size_t>(mapping.NumIsoHands()), 0.0f);
    ExpectInvalidArgument(
        [&] {
          calculator.Calculate(non_terminal, /*player=*/0, mapping,
                               opponent_reach);
        },
        "non-terminal CFV calculation should be invalid");
    ExpectInvalidArgument(
        [&] {
          calculator.Calculate(
              MakeTerminalNode(setup, TerminalStatus::kShowdownTerminal,
                               {false, false}),
              /*player=*/2, mapping, opponent_reach);
        },
        "bad player should be invalid");
    ExpectInvalidArgument(
        [&] {
          calculator.Calculate(
              MakeTerminalNode(setup, TerminalStatus::kShowdownTerminal,
                               {false, false}),
              /*player=*/0, mapping, std::vector<float>{});
        },
        "bad reach size should be invalid");
    opponent_reach[0] = -1.0f;
    ExpectInvalidArgument(
        [&] {
          calculator.Calculate(
              MakeTerminalNode(setup, TerminalStatus::kShowdownTerminal,
                               {false, false}),
              /*player=*/0, mapping, opponent_reach);
        },
        "negative reach should be invalid");
  }

  return 0;
}
