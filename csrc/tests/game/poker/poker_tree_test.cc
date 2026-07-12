#include <array>
#include <memory>
#include <optional>
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

const fisher::game::poker::PokerTreeEdge& FindChildEdge(
    const fisher::game::poker::PokerTreeNode& node,
    const fisher::game::poker::Action& action) {
  for (const fisher::game::poker::PokerTreeEdge& edge : node.child_edges) {
    if (edge.action == action) {
      return edge;
    }
  }
  throw std::runtime_error("child edge not found");
}

bool SameCard(fisher::game::poker::PokerCard left,
              fisher::game::poker::PokerCard right) {
  return left.Value() == right.Value();
}

const fisher::game::poker::PokerTreeEdge& FindChanceEdge(
    const fisher::game::poker::PokerTreeNode& node,
    fisher::game::poker::PokerCard card) {
  for (const fisher::game::poker::PokerTreeEdge& edge : node.child_edges) {
    if (edge.chance_card.has_value() &&
        SameCard(edge.chance_card.value(), card)) {
      return edge;
    }
  }
  throw std::runtime_error("chance edge not found");
}

std::vector<fisher::game::poker::PokerTreeEdge> Path(
    std::initializer_list<fisher::game::poker::PokerTreeEdge> edges) {
  return std::vector<fisher::game::poker::PokerTreeEdge>(edges);
}

}  // namespace

int main() {
  using fisher::game::poker::Action;
  using fisher::game::poker::NodeState;
  using fisher::game::poker::PokerCard;
  using fisher::game::poker::PokerCards;
  using fisher::game::poker::PokerTree;
  using fisher::game::poker::PokerTreeEdge;
  using fisher::game::poker::TerminalStatus;

  auto river_setup =
      MakeSetup(PokerCards("AsKdQh2c3d"), 10.0f, {1.0f, 1.0f},
                {0.0f, 0.0f}, {0.0f, 0.0f}, /*current_player=*/0,
                /*last_aggressor=*/0, /*raise_count=*/0);
  PokerTree river_tree(river_setup);

  Expect(river_tree.NumNodes() == 9, "river all-in tree node count mismatch");
  Expect(river_tree.Root().node_id == 0, "root id mismatch");
  Expect(!river_tree.Root().parent_node_id.has_value(),
         "root parent should be empty");
  Expect(river_tree.Root().node_state->Board().ToString() == "AsKdQh2c3d",
         "root state board mismatch");
  Expect(river_tree.Root().child_edges.size() == 2,
         "root child count mismatch");
  Expect(river_tree.Root().child_edges[0].action == Action::Check(),
         "root first child should be check");
  Expect(river_tree.Root().child_edges[1].action == Action::Bet(1.0f),
         "root second child should be allin bet");
  Expect(!river_tree.Root().child_edges[0].chance_card.has_value(),
         "player edge should not have chance card");

  const int check_node_id = river_tree.Root().child_edges[0].child_node_id;
  const int bet_node_id = river_tree.Root().child_edges[1].child_node_id;
  Expect(check_node_id == 1, "check child id should be 1");
  Expect(bet_node_id == 2, "bet child id should be 2");
  Expect(river_tree.Node(check_node_id).parent_node_id == 0,
         "check parent mismatch");
  Expect(river_tree.Node(bet_node_id).parent_node_id == 0,
         "bet parent mismatch");

  const fisher::game::poker::PokerTreeNode& check_node =
      river_tree.Node(check_node_id);
  Expect(check_node.child_edges.size() == 2, "check node child count");
  Expect(check_node.child_edges[0].child_node_id == 3,
         "BFS child id after check-check mismatch");
  Expect(check_node.child_edges[1].child_node_id == 4,
         "BFS child id after check-bet mismatch");
  Expect(check_node.child_edges[0].action == Action::Check(),
         "check node first action mismatch");
  Expect(check_node.child_edges[1].action == Action::Bet(1.0f),
         "check node second action mismatch");

  const fisher::game::poker::PokerTreeNode& bet_node =
      river_tree.Node(bet_node_id);
  Expect(bet_node.child_edges.size() == 2, "bet node child count");
  Expect(bet_node.child_edges[0].child_node_id == 5,
         "BFS fold child id mismatch");
  Expect(bet_node.child_edges[1].child_node_id == 6,
         "BFS call child id mismatch");
  Expect(bet_node.child_edges[0].action == Action::Fold(),
         "bet node first action should be fold");
  Expect(bet_node.child_edges[1].action == Action::Call(),
         "bet node second action should be call");

  const int check_check_id = check_node.child_edges[0].child_node_id;
  Expect(river_tree.Node(check_check_id).node_state->Status() ==
             TerminalStatus::kShowdownTerminal,
         "river check-check should be showdown terminal");
  Expect(river_tree.Node(check_check_id).child_edges.empty(),
         "terminal node should not have children");

  const int fold_id = bet_node.child_edges[0].child_node_id;
  const int call_id = bet_node.child_edges[1].child_node_id;
  Expect(river_tree.Node(fold_id).node_state->Status() ==
             TerminalStatus::kFoldTerminal,
         "fold child terminal status mismatch");
  Expect(river_tree.Node(call_id).node_state->Status() ==
             TerminalStatus::kShowdownTerminal,
         "call child terminal status mismatch");

  const fisher::game::poker::PokerTreeNode& check_bet_node =
      river_tree.Node(check_node.child_edges[1].child_node_id);
  Expect(check_bet_node.child_edges.size() == 2,
         "check-bet node should have fold/call children");
  Expect(check_bet_node.child_edges[0].action == Action::Fold(),
         "check-bet first child should be fold");
  Expect(check_bet_node.child_edges[1].action == Action::Call(),
         "check-bet second child should be call");
  Expect(river_tree.Node(check_bet_node.child_edges[0].child_node_id)
             .node_state->Status() == TerminalStatus::kFoldTerminal,
         "check-bet fold should be terminal");
  Expect(river_tree.Node(check_bet_node.child_edges[1].child_node_id)
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
  ExpectInvalidArgument([&] { river_tree.Node(-1); },
                        "negative node id should be invalid");
  ExpectInvalidArgument([&] { river_tree.Node(999); },
                        "large node id should be invalid");

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
  Expect(chance_tree.Root().child_edges.size() == 49,
         "flop chance should create 49 children");
  Expect(chance_tree.Root().child_edges[0].action == Action::Chance(),
         "chance child action mismatch");
  Expect(chance_tree.Root().child_edges[0].chance_card.has_value(),
         "chance child must carry card");
  Expect(SameCard(chance_tree.Root().child_edges[0].chance_card.value(),
                  PokerCard("2c")),
         "first chance card should follow deck order");
  Expect(chance_tree.Root().child_edges[0].child_node_id == 1,
         "first chance child id mismatch");
  Expect(chance_tree.Node(1).parent_node_id == 0,
         "chance child parent mismatch");
  Expect(chance_tree.Node(1).node_state->Board().ToString() == "AsKdQh2c",
         "chance child board mismatch");
  Expect(chance_tree.Node(1).node_state->Pot() == 10.0f,
         "chance child settled pot mismatch");
  Expect(chance_tree.Node(1).node_state->ActorPlayer() == 0,
         "chance child actor mismatch");
  for (const PokerTreeEdge& edge : chance_tree.Root().child_edges) {
    Expect(!SameCard(edge.chance_card.value(), PokerCard("As")),
           "chance should not include board card As");
    Expect(!SameCard(edge.chance_card.value(), PokerCard("Kd")),
           "chance should not include board card Kd");
    Expect(!SameCard(edge.chance_card.value(), PokerCard("Qh")),
           "chance should not include board card Qh");
  }

  Expect(!chance_tree.FindNode(
                    std::vector<Action>{Action::Check(), Action::Check(),
                                        Action::Chance()})
              .has_value(),
         "action-only chance lookup should be ambiguous");
  Expect(chance_tree
             .FindNode(Path({PokerTreeEdge::PlayerAction(Action::Check(), 0),
                             PokerTreeEdge::PlayerAction(Action::Check(), 0),
                             PokerTreeEdge::Chance(PokerCard("2c"), 0)}))
             .value() == 1,
         "exact chance path should find 2c child");
  Expect(chance_tree
             .FindNode(Path({PokerTreeEdge::Chance(PokerCard("2d"), 0)}))
             .value() ==
             FindChanceEdge(chance_tree.Root(), PokerCard("2d")).child_node_id,
         "local exact chance path should find 2d child");
  Expect(!chance_tree
              .FindNode(Path({PokerTreeEdge::Chance(PokerCard("As"), 0)}))
              .has_value(),
         "exact chance path with board card should not be found");
  Expect(!chance_tree
              .FindNode(Path({PokerTreeEdge::PlayerAction(Action::Bet(9.0f),
                                                          0)}))
              .has_value(),
         "nonexistent exact path should not be found");

  const PokerTreeEdge& found_check_edge =
      FindChildEdge(river_tree.Root(), Action::Check());
  Expect(found_check_edge.child_node_id == check_node_id,
         "FindChildEdge helper sanity check failed");

  return 0;
}
