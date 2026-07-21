#include <array>
#include <memory>
#include <stdexcept>
#include <vector>

#include "game/poker/action.h"
#include "game/poker/node_state.h"
#include "game/poker/poker_card.h"
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
    int last_aggressor, int raise_count,
    std::vector<fisher::game::poker::Action> root_history = {}) {
  using fisher::game::poker::GameBasic;
  using fisher::game::poker::SubgameSetup;

  return std::make_shared<SubgameSetup>(SubgameSetup::Args(
      board, pot, stacks, bet_total, bet_current_round, current_player,
      last_aggressor, raise_count, root_history, MatrixBelief(1.0f),
      AllInOnlyBets(), GameBasic(), /*bet_rounding=*/0.1f,
      /*min_raise_size=*/1.0f));
}

bool SameCard(fisher::game::poker::PokerCard left,
              fisher::game::poker::PokerCard right) {
  return left.Value() == right.Value();
}

}  // namespace

int main() {
  using fisher::game::poker::Action;
  using fisher::game::poker::NodeState;
  using fisher::game::poker::PokerCard;
  using fisher::game::poker::PokerCards;
  using fisher::game::poker::PokerTree;
  using fisher::game::poker::TerminalStatus;

  auto river_setup =
      MakeSetup(PokerCards("AsKdQh2c3d"), 10.0f, {1.0f, 1.0f},
                {0.0f, 0.0f}, {0.0f, 0.0f}, /*current_player=*/0,
                /*last_aggressor=*/0, /*raise_count=*/0);
  PokerTree river_tree(river_setup);

  Expect(river_tree.NumNodes() == 9, "river all-in tree node count mismatch");
  Expect(river_tree.Root().node_id == 0, "root id mismatch");
  Expect(river_tree.Root().parent_node_id == -1,
         "root parent should be empty");
  Expect(river_tree.Root().depth == 0, "root depth mismatch");
  Expect(river_tree.Root().node_state->Board().ToString() == "AsKdQh2c3d",
         "root state board mismatch");
  Expect(river_tree.Root().children_offset == 1,
         "root children offset mismatch");
  Expect(river_tree.Root().num_children == 2, "root child count mismatch");
  Expect(river_tree.HasChildren(0), "root should have children");
  Expect(river_tree.ChildNodeIdAt(0, 0) == 1,
         "root first child id mismatch");
  Expect(river_tree.ChildNodeIdAt(0, 1) == 2,
         "root second child id mismatch");

  const int check_node_id =
      river_tree.FindChild(0, Action::Check()).value();
  const int bet_node_id =
      river_tree.FindChild(0, Action::Bet(1.0f)).value();
  Expect(check_node_id == 1, "check child id should be 1");
  Expect(bet_node_id == 2, "bet child id should be 2");
  Expect(river_tree.Node(check_node_id).parent_node_id == 0,
         "check parent mismatch");
  Expect(river_tree.Node(bet_node_id).parent_node_id == 0,
         "bet parent mismatch");
  Expect(river_tree.Node(check_node_id).depth == 1,
         "check depth mismatch");
  Expect(river_tree.Node(bet_node_id).depth == 1, "bet depth mismatch");

  const auto& check_node = river_tree.Node(check_node_id);
  Expect(check_node.children_offset == 3, "check node offset mismatch");
  Expect(check_node.num_children == 2, "check node child count");
  Expect(river_tree.ChildNodeIdAt(check_node_id, 0) == 3,
         "BFS child id after check-check mismatch");
  Expect(river_tree.ChildNodeIdAt(check_node_id, 1) == 4,
         "BFS child id after check-bet mismatch");
  Expect(river_tree.FindChild(check_node_id, Action::Check()).value() == 3,
         "check node first action mismatch");
  Expect(river_tree.FindChild(check_node_id, Action::Bet(1.0f)).value() == 4,
         "check node second action mismatch");

  const auto& bet_node = river_tree.Node(bet_node_id);
  Expect(bet_node.children_offset == 5, "bet node offset mismatch");
  Expect(bet_node.num_children == 2, "bet node child count");
  Expect(river_tree.FindChild(bet_node_id, Action::Fold()).value() == 5,
         "bet node first action should be fold");
  Expect(river_tree.FindChild(bet_node_id, Action::Call()).value() == 6,
         "bet node second action should be call");

  const int check_check_id =
      river_tree.FindChild(check_node_id, Action::Check()).value();
  Expect(river_tree.Node(check_check_id).node_state->Status() ==
             TerminalStatus::kShowdownTerminal,
         "river check-check should be showdown terminal");
  Expect(river_tree.Node(check_check_id).depth == 2,
         "river check-check depth mismatch");
  Expect(!river_tree.HasChildren(check_check_id),
         "terminal node should not have children");
  Expect(river_tree.Node(check_check_id).children_offset == -1,
         "terminal node offset mismatch");
  Expect(river_tree.Node(check_check_id).num_children == 0,
         "terminal node child count mismatch");

  const int fold_id = river_tree.FindChild(bet_node_id, Action::Fold()).value();
  const int call_id = river_tree.FindChild(bet_node_id, Action::Call()).value();
  Expect(river_tree.Node(fold_id).node_state->Status() ==
             TerminalStatus::kFoldTerminal,
         "fold child terminal status mismatch");
  Expect(river_tree.Node(call_id).node_state->Status() ==
             TerminalStatus::kShowdownTerminal,
         "call child terminal status mismatch");
  Expect(river_tree.Node(fold_id).depth == 2, "fold depth mismatch");
  Expect(river_tree.Node(call_id).depth == 2, "call depth mismatch");

  const int check_bet_id =
      river_tree.FindChild(check_node_id, Action::Bet(1.0f)).value();
  const auto& check_bet_node = river_tree.Node(check_bet_id);
  Expect(check_bet_node.num_children == 2,
         "check-bet node should have fold/call children");
  Expect(river_tree.FindChild(check_bet_id, Action::Fold()).has_value(),
         "check-bet first child should be fold");
  Expect(river_tree.FindChild(check_bet_id, Action::Call()).has_value(),
         "check-bet second child should be call");
  Expect(river_tree.Node(river_tree.FindChild(check_bet_id, Action::Fold())
                             .value())
             .node_state->Status() == TerminalStatus::kFoldTerminal,
         "check-bet fold should be terminal");
  Expect(river_tree.Node(river_tree.FindChild(check_bet_id, Action::Call())
                             .value())
             .node_state->Status() == TerminalStatus::kShowdownTerminal,
         "check-bet call should be showdown");

  Expect(river_tree.FindNode(std::vector<Action>{}).value() == 0,
         "empty local path should find root");
  Expect(river_tree.FindNode(std::vector<Action>{Action::Check()}).value() ==
             check_node_id,
         "local check path should find check node");
  Expect(river_tree
             .FindNode(std::vector<Action>{Action::Bet(1.0f), Action::Call()})
             .value() == call_id,
         "local bet-call path should find call node");
  Expect(!river_tree.FindNode(std::vector<Action>{Action::Call()}).has_value(),
         "invalid local path should not be found");
  Expect(!river_tree.FindChild(0, Action::Call()).has_value(),
         "invalid root child action should not be found");
  ExpectInvalidArgument([&] { river_tree.Node(-1); },
                        "negative node id should be invalid");
  ExpectInvalidArgument([&] { river_tree.Node(999); },
                        "large node id should be invalid");
  ExpectInvalidArgument([&] { river_tree.ChildNodeIdAt(0, -1); },
                        "negative child index should be invalid");
  ExpectInvalidArgument([&] { river_tree.ChildNodeIdAt(0, 2); },
                        "large child index should be invalid");

  auto prefix_setup = MakeSetup(
      PokerCards("AsKdQh2c3d"), 10.0f, {1.0f, 1.0f}, {2.5f, 2.5f},
      {0.0f, 0.0f}, 0, 0, 0,
      std::vector<Action>{Action::Bet(2.5f), Action::Call()});
  PokerTree prefix_tree(prefix_setup);
  Expect(prefix_tree
             .FindNode(std::vector<Action>{Action::Bet(2.5f), Action::Call(),
                                           Action::Check()})
             .value() == 1,
         "global path with root prefix should find check node");
  Expect(prefix_tree.FindNode(std::vector<Action>{Action::Check()}).value() ==
             1,
         "local path without root prefix should find check node");

  auto chance_setup =
      MakeSetup(PokerCards("AsKdQh"), 10.0f, {0.0f, 0.0f}, {0.0f, 0.0f},
                {0.0f, 0.0f}, 0, -1, 0);
  const NodeState chance_root =
      chance_setup->GetRootNodeState()
          .CommitAction(Action::Check())
          .CommitAction(Action::Check());
  PokerTree chance_tree(chance_root);
  Expect(chance_tree.Root().node_state->ActorPlayer() ==
             NodeState::kChancePlayer,
         "chance tree root should be chance node");
  Expect(chance_tree.Root().depth == 0, "chance root depth mismatch");
  Expect(chance_tree.Root().children_offset == 1,
         "chance root offset mismatch");
  Expect(chance_tree.Root().num_children == 49,
         "flop chance should create 49 children");
  Expect(chance_tree.ChildNodeIdAt(0, 0) == 1,
         "first chance child id mismatch");
  Expect(chance_tree.FindChanceChild(0, PokerCard("2c")).value() == 1,
         "first chance card should follow deck order");
  Expect(chance_tree.FindChanceChild(0, PokerCard("2d")).value() ==
             chance_tree.ChildNodeIdAt(0, 1),
         "second chance card should follow deck order");
  Expect(chance_tree.Node(1).parent_node_id == 0,
         "chance child parent mismatch");
  Expect(chance_tree.Node(1).depth == 1, "chance child depth mismatch");
  Expect(chance_tree.Node(1).node_state->Board().ToString() == "AsKdQh2c",
         "chance child board mismatch");
  Expect(chance_tree.Node(1).node_state->Pot() == 10.0f,
         "chance child settled pot mismatch");
  Expect(chance_tree.Node(1).node_state->ActorPlayer() == 0,
         "chance child actor mismatch");
  Expect(!chance_tree.FindChanceChild(0, PokerCard("As")).has_value(),
         "chance should not include board card As");
  Expect(!chance_tree.FindChanceChild(0, PokerCard("Kd")).has_value(),
         "chance should not include board card Kd");
  Expect(!chance_tree.FindChanceChild(0, PokerCard("Qh")).has_value(),
         "chance should not include board card Qh");

  Expect(!chance_tree.FindNode(
                    std::vector<Action>{Action::Check(), Action::Check(),
                                        Action::Chance()})
              .has_value(),
         "action-only chance lookup should be unsupported");
  Expect(!chance_tree.FindChild(0, Action::Chance()).has_value(),
         "FindChild should not resolve chance action");
  Expect(!chance_tree.FindChanceChild(1, PokerCard("2d")).has_value(),
         "non-chance node should not resolve chance child");
  Expect(SameCard(PokerCard("2c"), PokerCard("2c")),
         "SameCard helper sanity check failed");

  auto direct_flop_chance_setup =
      MakeSetup(PokerCards("AsKdQh"), 10.0f, {0.0f, 0.0f}, {0.0f, 0.0f},
                {0.0f, 0.0f}, NodeState::kChancePlayer, -1, 0);
  PokerTree direct_flop_chance_tree(direct_flop_chance_setup);
  Expect(direct_flop_chance_tree.Root().node_state->ActorPlayer() ==
             NodeState::kChancePlayer,
         "direct flop chance root should be chance node");
  Expect(direct_flop_chance_tree.Root().num_children == 49,
         "direct flop chance should create 49 children");
  Expect(direct_flop_chance_tree.FindChanceChild(0, PokerCard("2c")).value() ==
             1,
         "direct flop chance first child mismatch");
  Expect(direct_flop_chance_tree.Node(1).node_state->Board().ToString() ==
             "AsKdQh2c",
         "direct flop chance child board mismatch");

  auto direct_turn_chance_setup =
      MakeSetup(PokerCards("AsKdQh2c"), 10.0f, {0.0f, 0.0f},
                {0.0f, 0.0f}, {0.0f, 0.0f}, NodeState::kChancePlayer, -1, 0);
  PokerTree direct_turn_chance_tree(direct_turn_chance_setup);
  Expect(direct_turn_chance_tree.Root().node_state->ActorPlayer() ==
             NodeState::kChancePlayer,
         "direct turn chance root should be chance node");
  Expect(direct_turn_chance_tree.Root().num_children == 48,
         "direct turn chance should create 48 children");
  Expect(direct_turn_chance_tree.FindChanceChild(0, PokerCard("2d")).value() ==
             1,
         "direct turn chance first child mismatch");
  Expect(direct_turn_chance_tree.Node(1).node_state->Board().ToString() ==
             "AsKdQh2c2d",
         "direct turn chance child board mismatch");

  return 0;
}
