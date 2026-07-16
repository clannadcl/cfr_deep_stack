#include <array>
#include <memory>
#include <stdexcept>
#include <vector>

#include "algorithm/cfr_storage.h"
#include "game/poker/action.h"
#include "game/poker/isomorphic_mapping.h"
#include "game/poker/node_state.h"
#include "game/poker/poker_cards.h"
#include "game/poker/poker_tree.h"
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

fisher::game::poker::TreeAbstractedBets AllInOnlyBets() {
  return fisher::game::poker::TreeAbstractedBets(
      fisher::game::poker::AbstractedBetStringConfig{{"allin"}},
      fisher::game::poker::AbstractedDonkBetStringConfig{"allin"});
}

std::shared_ptr<fisher::game::poker::SubgameSetup> MakeSetup(
    fisher::game::poker::PokerCards board, float pot,
    std::array<float, 2> stacks, std::array<float, 2> bet_total,
    std::array<float, 2> bet_current_round, int current_player,
    int last_aggressor, int raise_count) {
  using fisher::game::poker::GameBasic;
  using fisher::game::poker::SubgameSetup;

  return std::make_shared<SubgameSetup>(SubgameSetup::Args(
      board, pot, stacks, bet_total, bet_current_round, current_player,
      last_aggressor, raise_count, std::vector<fisher::game::poker::Action>{},
      MatrixBelief(1.0f), AllInOnlyBets(), GameBasic(),
      /*bet_rounding=*/0.1f, /*min_raise_size=*/1.0f));
}

}  // namespace

int main() {
  using fisher::algorithm::CfrStorage;
  using fisher::game::poker::Action;
  using fisher::game::poker::IsomorphicMappingTable;
  using fisher::game::poker::NodeState;
  using fisher::game::poker::PokerCards;
  using fisher::game::poker::PokerTree;

  auto river_setup =
      MakeSetup(PokerCards("AsKdQh2c3d"), 10.0f, {1.0f, 1.0f},
                {0.0f, 0.0f}, {0.0f, 0.0f}, /*current_player=*/0,
                /*last_aggressor=*/0, /*raise_count=*/0);
  PokerTree river_tree(river_setup);
  IsomorphicMappingTable river_mapping_table(river_setup->BasicGame(),
                                             river_setup->RootBelief());
  CfrStorage storage(river_tree, &river_mapping_table);
  const int num_hands =
      river_mapping_table.Get(river_tree.Root().node_state->Board())
          .NumIsoHands();

  Expect(storage.NumNodes() == river_tree.NumNodes(), "node count mismatch");
  Expect(storage.NumHands(0) == num_hands, "num hands mismatch");
  std::size_t expected_cfv_size = 0;
  for (const auto& node : river_tree.Nodes()) {
    expected_cfv_size +=
        static_cast<std::size_t>(
            fisher::game::poker::GameBasic::kNumPlayers *
            river_mapping_table.Get(node.node_state->Board()).NumIsoHands());
  }
  Expect(storage.CfvData().size() == expected_cfv_size,
         "cfv data size mismatch");

  const auto& root_layout = storage.Layout(0);
  Expect(root_layout.strategy_offset == 0, "root strategy offset mismatch");
  Expect(root_layout.regret_offset == 0, "root regret offset mismatch");
  Expect(root_layout.cfv_offset == 0, "root cfv offset mismatch");
  Expect(root_layout.sum_strategy_offset == 0,
         "root sum strategy offset mismatch");
  Expect(root_layout.num_actions == 2, "root action count mismatch");
  Expect(root_layout.num_hands == num_hands,
         "root layout hand count mismatch");
  Expect(root_layout.reach_offset[0] >= 0, "root player 0 reach missing");
  Expect(root_layout.reach_offset[1] >= 0, "root player 1 reach missing");
  Expect(root_layout.reach_offset[0] != root_layout.reach_offset[1],
         "root players should use different reach blocks");
  Expect(storage.StrategyData().size() >= static_cast<std::size_t>(2 * num_hands),
         "strategy data should contain root actions");
  Expect(storage.RegretData().size() >= static_cast<std::size_t>(2 * num_hands),
         "regret data should contain root actions");
  Expect(storage.SumStrategyData().size() >=
             static_cast<std::size_t>(2 * num_hands),
         "sum strategy data should contain root actions");
  Expect(storage.StrategyAt(0, 0, 0) == 0.5f,
         "strategy should initialize uniform");
  Expect(storage.StrategyAt(0, 1, 0) == 0.5f,
         "strategy should initialize uniform across actions");

  storage.StrategyAt(0, 1, num_hands - 1) = 0.75f;
  storage.RegretAt(0, 0, 1) = -1.25f;
  storage.SumStrategyAt(0, 1, 2) = 2.25f;
  storage.CfvAt(0, 0, num_hands - 1) = 3.5f;
  storage.CfvAt(0, 1, num_hands - 1) = -3.5f;
  const CfrStorage& const_storage = storage;
  Expect(const_storage.StrategyAt(0, 1, num_hands - 1) == 0.75f,
         "strategy at mismatch");
  Expect(const_storage.RegretAt(0, 0, 1) == -1.25f,
         "regret at mismatch");
  Expect(const_storage.SumStrategyAt(0, 1, 2) == 2.25f,
         "sum strategy at mismatch");
  Expect(const_storage.CfvAt(0, 0, num_hands - 1) == 3.5f,
         "player 0 cfv at mismatch");
  Expect(const_storage.CfvAt(0, 1, num_hands - 1) == -3.5f,
         "player 1 cfv at mismatch");
  storage.ReachAt(0, 0, 0) = 0.5f;
  storage.ReachAt(0, 1, num_hands - 1) = 0.25f;
  Expect(const_storage.ReachAt(0, 0, 0) == 0.5f,
         "root player 0 reach mismatch");
  Expect(const_storage.ReachAt(0, 1, num_hands - 1) == 0.25f,
         "root player 1 reach mismatch");

  const int check_node_id =
      river_tree.FindChild(0, Action::Check()).value();
  Expect(storage.NumActions(check_node_id) == 2,
         "check node action count mismatch");
  Expect(storage.NumHands(check_node_id) == num_hands,
         "check node hand count mismatch");
  Expect(storage.Layout(check_node_id).strategy_offset == 2 * num_hands,
         "check node strategy offset should follow root block");
  Expect(storage.Layout(check_node_id).reach_offset[0] !=
             root_layout.reach_offset[0],
         "player 0 action should allocate new player 0 reach");
  Expect(storage.Layout(check_node_id).reach_offset[1] ==
             root_layout.reach_offset[1],
         "player 0 action should share player 1 reach");
  storage.ReachAt(check_node_id, 1, num_hands - 1) = 0.8f;
  Expect(storage.ReachAt(0, 1, num_hands - 1) == 0.8f,
         "shared opponent reach should update root block");

  const int check_check_id =
      river_tree.FindChild(check_node_id, Action::Check()).value();
  Expect(river_tree.Node(check_check_id).node_state->IsTerminal(),
         "check-check should be terminal");
  Expect(storage.Layout(check_check_id).num_actions == 0,
         "terminal node should not have strategy actions");
  Expect(storage.Layout(check_check_id).reach_offset[0] ==
             storage.Layout(check_node_id).reach_offset[0],
         "player 1 action should share player 0 reach");
  Expect(storage.Layout(check_check_id).reach_offset[1] !=
             storage.Layout(check_node_id).reach_offset[1],
         "player 1 action should allocate new player 1 reach");
  storage.ReachAt(check_check_id, 0, 0) = 0.9f;
  Expect(storage.ReachAt(check_node_id, 0, 0) == 0.9f,
         "shared player 0 reach should update parent block");
  storage.CfvAt(check_check_id, num_hands - 1) = 9.0f;
  Expect(storage.CfvAt(check_check_id, num_hands - 1) == 9.0f,
         "terminal cfv access mismatch");
  ExpectInvalidArgument(
      [&] { storage.StrategyAt(check_check_id, 0, 0); },
      "terminal strategy access should be invalid");
  ExpectInvalidArgument(
      [&] { storage.RegretAt(check_check_id, 0, 0); },
      "terminal regret access should be invalid");
  ExpectInvalidArgument(
      [&] { storage.SumStrategyAt(check_check_id, 0, 0); },
      "terminal sum strategy access should be invalid");

  auto chance_setup =
      MakeSetup(PokerCards("AsKdQh"), 10.0f, {0.0f, 0.0f}, {0.0f, 0.0f},
                {0.0f, 0.0f}, 0, -1, 0);
  const NodeState chance_root =
      chance_setup->GetRootNodeState()
          .CommitAction(Action::Check())
          .CommitAction(Action::Check());
  PokerTree chance_tree(chance_root);
  IsomorphicMappingTable chance_mapping_table(chance_setup->BasicGame(),
                                              chance_setup->RootBelief());
  CfrStorage chance_storage(chance_tree, &chance_mapping_table);
  const int chance_num_hands =
      chance_mapping_table.Get(chance_tree.Root().node_state->Board())
          .NumIsoHands();
  Expect(chance_tree.Root().node_state->ActorPlayer() ==
             NodeState::kChancePlayer,
         "chance root actor mismatch");
  Expect(chance_storage.Layout(0).num_actions == 0,
         "chance node should not have strategy actions");
  Expect(chance_storage.Layout(0).reach_offset[0] >= 0,
         "chance root player 0 reach missing");
  Expect(chance_storage.Layout(0).reach_offset[1] >= 0,
         "chance root player 1 reach missing");
  chance_storage.CfvAt(0, 1, chance_num_hands - 1) = 4.0f;
  Expect(chance_storage.CfvAt(0, 1, chance_num_hands - 1) == 4.0f,
         "chance cfv access mismatch");
  ExpectInvalidArgument(
      [&] { chance_storage.StrategyAt(0, 0, 0); },
      "chance strategy access should be invalid");

  const int turn_child_id = chance_tree.ChildNodeIdAt(0, 0);
  Expect(chance_tree.Node(turn_child_id).parent_node_id == 0,
         "turn child parent mismatch");
  Expect(chance_storage.Layout(turn_child_id).reach_offset[0] !=
             chance_storage.Layout(0).reach_offset[0],
         "chance child should allocate new player 0 reach");
  Expect(chance_storage.Layout(turn_child_id).reach_offset[1] !=
             chance_storage.Layout(0).reach_offset[1],
         "chance child should allocate new player 1 reach");
  chance_storage.ReachAt(0, 0, 0) = 0.3f;
  Expect(chance_storage.ReachAt(turn_child_id, 0, 0) == 0.0f,
         "chance child should not share player 0 reach with parent");
  chance_storage.ReachAt(turn_child_id, 1, 0) = 0.7f;
  Expect(chance_storage.ReachAt(0, 1, 0) == 0.0f,
         "chance child should not share player 1 reach with parent");

  ExpectInvalidArgument([&] { storage.Layout(-1); },
                        "negative node id should be invalid");
  ExpectInvalidArgument([&] { storage.Layout(999); },
                        "large node id should be invalid");
  ExpectInvalidArgument([&] { storage.StrategyAt(0, -1, 0); },
                        "negative action index should be invalid");
  ExpectInvalidArgument([&] { storage.StrategyAt(0, 2, 0); },
                        "large action index should be invalid");
  ExpectInvalidArgument([&] { storage.StrategyAt(0, 0, -1); },
                        "negative hand index should be invalid");
  ExpectInvalidArgument([&] { storage.StrategyAt(0, 0, num_hands); },
                        "large hand index should be invalid");
  ExpectInvalidArgument([&] { storage.CfvAt(0, num_hands); },
                        "large cfv hand index should be invalid");
  ExpectInvalidArgument([&] { storage.CfvAt(0, 2, 0); },
                        "large cfv player should be invalid");
  ExpectInvalidArgument([&] { storage.ReachAt(0, -1, 0); },
                        "negative reach player should be invalid");
  ExpectInvalidArgument([&] { storage.ReachAt(0, 2, 0); },
                        "large reach player should be invalid");
  ExpectInvalidArgument([&] { storage.ReachAt(0, 0, -1); },
                        "negative reach hand index should be invalid");
  ExpectInvalidArgument([&] { storage.ReachAt(0, 0, num_hands); },
                        "large reach hand index should be invalid");

  return 0;
}
