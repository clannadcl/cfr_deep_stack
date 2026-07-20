#include <array>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "game/poker/action.h"
#include "game/poker/game_basic.h"
#include "game/poker/hand_equity_calculator.h"
#include "game/poker/hand_evaluator.h"
#include "game/poker/isomorphic_mapping.h"
#include "game/poker/node_state.h"
#include "game/poker/poker_cards.h"
#include "game/poker/poker_hand.h"
#include "game/poker/subgame_setup.h"
#include "game/poker/tree_abstracted_bets.h"

namespace {

void Expect(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void ExpectNear(float actual, float expected, float tolerance,
                const char* message) {
  if (std::fabs(actual - expected) > tolerance) {
    throw std::runtime_error(message);
  }
}

std::vector<std::vector<float>> MatrixBelief(float value) {
  return std::vector<std::vector<float>>(
      fisher::game::poker::GameBasic::kNumPlayers,
      std::vector<float>(fisher::game::poker::GameBasic::kNumHands, value));
}

std::shared_ptr<fisher::game::poker::SubgameSetup> MakeSetup(
    const fisher::game::poker::PokerCards& board,
    fisher::game::poker::GameBasic game_basic =
        fisher::game::poker::GameBasic()) {
  using fisher::game::poker::Action;
  using fisher::game::poker::SubgameSetup;
  using fisher::game::poker::TreeAbstractedBets;

  return std::make_shared<SubgameSetup>(SubgameSetup::Args(
      board, /*pot=*/10.0f, /*stacks=*/{100.0f, 100.0f},
      /*bet_total=*/{0.0f, 0.0f}, /*bet_current_round=*/{0.0f, 0.0f},
      /*current_player=*/0, /*last_aggressor=*/-1, /*raise_count=*/0,
      std::vector<Action>{}, MatrixBelief(1.0f),
      TreeAbstractedBets(TreeAbstractedBets::Args{}), std::move(game_basic),
      /*bet_rounding=*/0.1f, /*min_raise_size=*/1.0f));
}

std::vector<bool> PossibleHands(
    const fisher::game::poker::GameBasic& game_basic,
    const fisher::game::poker::PokerCards& board,
    const std::vector<std::string>& hands) {
  const fisher::game::poker::IsomorphicMapping full_mapping(
      game_basic, board,
      std::vector<bool>(fisher::game::poker::GameBasic::kNumHands, true));
  std::vector<bool> possible(fisher::game::poker::GameBasic::kNumHands, false);
  std::vector<bool> possible_iso(
      static_cast<std::size_t>(full_mapping.NumIsoHands()), false);
  for (const std::string& hand : hands) {
    const int iso = full_mapping.RawToIso(
        game_basic.HandIndex(fisher::game::poker::PokerHand(hand)));
    if (iso < 0) {
      throw std::runtime_error("possible hand collides with board");
    }
    possible_iso[static_cast<std::size_t>(iso)] = true;
  }
  for (int raw = 0; raw < fisher::game::poker::GameBasic::kNumHands; ++raw) {
    const int iso = full_mapping.RawToIso(raw);
    if (iso >= 0 && possible_iso[static_cast<std::size_t>(iso)]) {
      possible[static_cast<std::size_t>(raw)] = true;
    }
  }
  return possible;
}

int IsoIndex(const fisher::game::poker::GameBasic& game_basic,
             const fisher::game::poker::IsomorphicMapping& mapping,
             const char* hand) {
  return mapping.RawToIso(
      game_basic.HandIndex(fisher::game::poker::PokerHand(hand)));
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
  using fisher::game::poker::HandEquityCalculator;
  using fisher::game::poker::HandEquityResult;
  using fisher::game::poker::IsomorphicMapping;
  using fisher::game::poker::NodeState;
  using fisher::game::poker::PokerCards;
  using fisher::game::poker::SevenCardLookupTable;

  GameBasic game_basic;
  SevenCardLookupTable evaluator;
  HandEquityCalculator calculator(game_basic, evaluator);

  {
    const PokerCards board("AhKdQs7c2h");
    const NodeState node = MakeSetup(board)->GetRootNodeState();
    IsomorphicMapping mapping(
        game_basic, board,
        PossibleHands(game_basic, board, {"Ac3c", "Kc3d", "Ad4d", "7d7s"}));
    std::vector<float> player_reach(
        static_cast<std::size_t>(mapping.NumIsoHands()), 0.0f);
    std::vector<float> opponent_reach(
        static_cast<std::size_t>(mapping.NumIsoHands()), 0.0f);
    AddReachFor(game_basic, mapping, "Ac3c", 1.0f, &player_reach);
    AddReachFor(game_basic, mapping, "Kc3d", 1.0f, &opponent_reach);
    AddReachFor(game_basic, mapping, "Ad4d", 1.0f, &opponent_reach);
    AddReachFor(game_basic, mapping, "7d7s", 1.0f, &opponent_reach);

    const HandEquityResult result = calculator.Calculate(
        node, /*player=*/0, mapping, player_reach.data(),
        opponent_reach.data());
    const int hero_iso = IsoIndex(game_basic, mapping, "Ac3c");
    ExpectNear(result.equity[static_cast<std::size_t>(hero_iso)], 0.5f, 1e-5f,
               "river weighted win/chop/lose equity mismatch");
    ExpectNear(result.range_equity, 0.5f, 1e-5f,
               "river weighted range equity mismatch");
  }

  {
    const PokerCards board("AsKsQsJsTs");
    const NodeState node = MakeSetup(board)->GetRootNodeState();
    IsomorphicMapping mapping(
        game_basic, board,
        PossibleHands(game_basic, board, {"2c2d", "3c3d", "4c4d"}));
    std::vector<float> player_reach(
        static_cast<std::size_t>(mapping.NumIsoHands()), 0.0f);
    std::vector<float> opponent_reach(
        static_cast<std::size_t>(mapping.NumIsoHands()), 0.0f);
    AddReachFor(game_basic, mapping, "2c2d", 1.0f, &player_reach);
    AddReachFor(game_basic, mapping, "3c3d", 2.0f, &opponent_reach);

    const HandEquityResult result = calculator.Calculate(
        node, /*player=*/0, mapping, player_reach.data(),
        opponent_reach.data());
    ExpectNear(result.equity[static_cast<std::size_t>(
                   IsoIndex(game_basic, mapping, "2c2d"))],
               0.5f, 1e-5f, "river board-chop equity mismatch");
    ExpectNear(result.range_equity, 0.5f, 1e-5f,
               "river board-chop range equity mismatch");
  }

  {
    const PokerCards board("2c7d9hKs");
    const NodeState node = MakeSetup(board)->GetRootNodeState();
    IsomorphicMapping mapping(
        game_basic, board,
        PossibleHands(game_basic, board, {"AsAd", "KcQd"}));
    std::vector<float> player_reach(
        static_cast<std::size_t>(mapping.NumIsoHands()), 0.0f);
    std::vector<float> opponent_reach(
        static_cast<std::size_t>(mapping.NumIsoHands()), 0.0f);
    AddReachFor(game_basic, mapping, "AsAd", 1.0f, &player_reach);
    AddReachFor(game_basic, mapping, "KcQd", 1.0f, &opponent_reach);

    const HandEquityResult result = calculator.Calculate(
        node, /*player=*/0, mapping, player_reach.data(),
        opponent_reach.data());
    ExpectNear(result.equity[static_cast<std::size_t>(
                   IsoIndex(game_basic, mapping, "AsAd"))],
               39.0f / 44.0f, 1e-5f,
               "turn exact 5-out equity mismatch");
    ExpectNear(result.range_equity, 39.0f / 44.0f, 1e-5f,
               "turn exact 5-out range equity mismatch");
  }

  {
    const PokerCards board("KsQh4dKc");
    const NodeState node = MakeSetup(board)->GetRootNodeState();
    IsomorphicMapping mapping(
        game_basic, board,
        PossibleHands(game_basic, board, {"KhKd", "AcAd"}));
    std::vector<float> player_reach(
        static_cast<std::size_t>(mapping.NumIsoHands()), 0.0f);
    std::vector<float> opponent_reach(
        static_cast<std::size_t>(mapping.NumIsoHands()), 0.0f);
    AddReachFor(game_basic, mapping, "KhKd", 0.826000034809f, &player_reach);
    AddReachFor(game_basic, mapping, "AcAd", 1.0f, &opponent_reach);

    const int kk_iso = IsoIndex(game_basic, mapping, "KhKd");
    Expect(mapping.RawHandCount(kk_iso) == 1,
           "KK bucket should contain only the unblocked combo");
    const HandEquityResult result = calculator.Calculate(
        node, /*player=*/0, mapping, player_reach.data(),
        opponent_reach.data());
    ExpectNear(result.equity[static_cast<std::size_t>(kk_iso)], 1.0f, 1e-5f,
               "blocked KK turn equity should not be diluted");
    ExpectNear(result.range_equity, 1.0f, 1e-5f,
               "blocked KK range equity should not be diluted");
  }

  return 0;
}
