#include "algorithm/poker_cfr_solver.h"

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include "game/poker/game_basic.h"
#include "game/poker/node_state.h"

namespace fisher::algorithm {
namespace {

void ValidatePlayer(int player) {
  if (player < 0 || player >= game::poker::GameBasic::kNumPlayers) {
    throw std::invalid_argument("Poker CFR player must be 0 or 1");
  }
}

bool IsPlayerNode(const game::poker::PokerTreeNode& node) {
  return !node.node_state->IsTerminal() &&
         node.node_state->ActorPlayer() !=
             game::poker::NodeState::kChancePlayer;
}

std::shared_ptr<game::poker::SubgameSetup> ValidateSetup(
    std::shared_ptr<game::poker::SubgameSetup> setup) {
  if (setup == nullptr) {
    throw std::invalid_argument("Poker CFR setup cannot be null");
  }
  return setup;
}

int TransitionKey(int parent_iso, int child_iso, int child_num_hands) {
  return parent_iso * child_num_hands + child_iso;
}

void ValidateAdjacentBoardTransition(
    const game::poker::IsomorphicMapping& parent,
    const game::poker::IsomorphicMapping& child) {
  const int parent_cards = game::poker::BoardCardsForRound(parent.Round());
  const int child_cards = game::poker::BoardCardsForRound(child.Round());
  if (child_cards != parent_cards + 1) {
    throw std::invalid_argument("Iso transition child board must be next street");
  }
  if (parent.RawBoard().Size() >= child.RawBoard().Size()) {
    throw std::invalid_argument(
        "Iso transition child board must contain more cards");
  }
  for (std::size_t index = 0; index < parent.RawBoard().Size(); ++index) {
    if (parent.RawBoard().Cards()[index].Value() !=
        child.RawBoard().Cards()[index].Value()) {
      throw std::invalid_argument(
          "Iso transition child board must contain parent board as prefix");
    }
  }
}

}  // namespace

PokerCfrSolver::Args::Args(std::shared_ptr<game::poker::SubgameSetup> setup)
    : setup(std::move(setup)) {}

IsoTransition BuildIsoTransition(
    int parent_node_id, int child_node_id,
    const game::poker::IsomorphicMapping& parent,
    const game::poker::IsomorphicMapping& child) {
  ValidateAdjacentBoardTransition(parent, child);
  const int child_num_hands = child.NumIsoHands();
  std::unordered_map<int, int> counts;
  counts.reserve(game::poker::GameBasic::kNumHands);
  for (int raw = 0; raw < game::poker::GameBasic::kNumHands; ++raw) {
    const int parent_iso = parent.RawToIso(raw);
    const int child_iso = child.RawToIso(raw);
    if (parent_iso < 0 || child_iso < 0) {
      continue;
    }
    ++counts[TransitionKey(parent_iso, child_iso, child_num_hands)];
  }

  IsoTransition transition;
  transition.parent_node_id = parent_node_id;
  transition.child_node_id = child_node_id;
  transition.chance_prob =
      1.0f / static_cast<float>(game::poker::GameBasic::kDeckSize -
                                parent.RawBoard().Size() - 4);
  transition.edges.reserve(counts.size());
  for (const auto& entry : counts) {
    const int parent_iso = entry.first / child_num_hands;
    const int child_iso = entry.first % child_num_hands;
    transition.edges.push_back(IsoTransitionEdge{
        parent_iso,
        child_iso,
        static_cast<float>(entry.second) /
            static_cast<float>(parent.RawHandCount(parent_iso)),
    });
  }
  return transition;
}

PokerCfrSolver::PokerCfrSolver(const Args& args)
    : setup_(ValidateSetup(args.setup)),
      tree_(setup_),
      mapping_table_(setup_->BasicGame(), setup_->RootBelief()),
      storage_(tree_, &mapping_table_),
      terminal_cfv_calculator_(setup_->BasicGame(), evaluator_) {
  BuildNodeCaches();
}

void PokerCfrSolver::RunIteration() {
  RunHeroPass(0);
  RunHeroPass(1);
}

void PokerCfrSolver::RunHeroPass(int hero_player) {
  ValidatePlayer(hero_player);
  InitializeRootReach();
  ForwardReachAndAccumulateAverage(hero_player);
  ComputeTerminalCfvs();
  BackwardAndUpdate(hero_player);
}

std::vector<float> PokerCfrSolver::CurrentStrategyData() const {
  return storage_.StrategyData();
}

std::vector<float> PokerCfrSolver::AverageStrategyData(float epsilon) const {
  if (epsilon <= 0.0f) {
    throw std::invalid_argument("Average strategy epsilon must be positive");
  }

  std::vector<float> average = storage_.SumStrategyData();
  for (const game::poker::PokerTreeNode& node : tree_.Nodes()) {
    if (!IsPlayerNode(node)) {
      continue;
    }
    const NodeCfrLayout& layout = storage_.Layout(node.node_id);
    for (int hand = 0; hand < layout.num_hands; ++hand) {
      float denominator = 0.0f;
      for (int action = 0; action < layout.num_actions; ++action) {
        denominator +=
            storage_.SumStrategyAt(node.node_id, action, hand) + epsilon;
      }
      for (int action = 0; action < layout.num_actions; ++action) {
        average[static_cast<std::size_t>(
            layout.sum_strategy_offset + action * layout.num_hands + hand)] =
            (storage_.SumStrategyAt(node.node_id, action, hand) + epsilon) /
            denominator;
      }
    }
  }
  return average;
}

const game::poker::PokerTree& PokerCfrSolver::Tree() const { return tree_; }

CfrStorage& PokerCfrSolver::Storage() { return storage_; }

const CfrStorage& PokerCfrSolver::Storage() const { return storage_; }

const std::vector<int>& PokerCfrSolver::TerminalNodeIds() const {
  return terminal_node_ids_;
}

const std::vector<int>& PokerCfrSolver::ReverseNodeIds() const {
  return reverse_node_ids_;
}

const IsoTransition& PokerCfrSolver::ChanceTransition(
    int child_node_id) const {
  const auto iterator = chance_transitions_by_child_.find(child_node_id);
  if (iterator == chance_transitions_by_child_.end()) {
    throw std::invalid_argument("Chance transition child node not found");
  }
  return iterator->second;
}

void PokerCfrSolver::BuildNodeCaches() {
  node_mappings_.resize(static_cast<std::size_t>(tree_.NumNodes()), nullptr);
  terminal_node_ids_.clear();
  reverse_node_ids_.clear();
  chance_transitions_by_child_.clear();

  for (const game::poker::PokerTreeNode& node : tree_.Nodes()) {
    node_mappings_[static_cast<std::size_t>(node.node_id)] =
        &mapping_table_.Get(node.node_state->Board());
    reverse_node_ids_.push_back(node.node_id);
    if (node.node_state->IsTerminal()) {
      terminal_node_ids_.push_back(node.node_id);
    }
  }

  for (const game::poker::PokerTreeNode& node : tree_.Nodes()) {
    if (node.node_state->ActorPlayer() ==
        game::poker::NodeState::kChancePlayer) {
      for (int child_index = 0; child_index < node.num_children;
           ++child_index) {
        const int child_id = tree_.ChildNodeIdAt(node.node_id, child_index);
        chance_transitions_by_child_.emplace(
            child_id, BuildChanceTransition(node.node_id, child_id));
      }
    }
  }
  std::reverse(reverse_node_ids_.begin(), reverse_node_ids_.end());
}

IsoTransition PokerCfrSolver::BuildChanceTransition(int parent_node_id,
                                                    int child_node_id) const {
  const game::poker::IsomorphicMapping& parent =
      *node_mappings_[static_cast<std::size_t>(parent_node_id)];
  const game::poker::IsomorphicMapping& child =
      *node_mappings_[static_cast<std::size_t>(child_node_id)];
  return BuildIsoTransition(parent_node_id, child_node_id, parent, child);
}

void PokerCfrSolver::InitializeRootReach() {
  const int root_node_id = tree_.Root().node_id;
  const game::poker::IsomorphicMapping& root_mapping =
      *node_mappings_[static_cast<std::size_t>(root_node_id)];
  const std::vector<std::vector<float>>& belief = setup_->RootBelief().Belief();
  for (int player = 0; player < game::poker::GameBasic::kNumPlayers;
       ++player) {
    for (int hand = 0; hand < root_mapping.NumIsoHands(); ++hand) {
      storage_.ReachAt(root_node_id, player, hand) = 0.0f;
    }
    for (int raw = 0; raw < game::poker::GameBasic::kNumHands; ++raw) {
      const int iso = root_mapping.RawToIso(raw);
      if (iso < 0) {
        continue;
      }
      storage_.ReachAt(root_node_id, player, iso) +=
          belief[static_cast<std::size_t>(player)]
                [static_cast<std::size_t>(raw)];
    }
  }
}

void PokerCfrSolver::ForwardReachAndAccumulateAverage(int hero_player) {
  for (const game::poker::PokerTreeNode& node : tree_.Nodes()) {
    if (node.node_state->IsTerminal()) {
      continue;
    }
    if (node.node_state->ActorPlayer() ==
        game::poker::NodeState::kChancePlayer) {
      PropagateChanceReach(node);
    } else {
      PropagatePlayerReach(node, hero_player);
    }
  }
}

void PokerCfrSolver::PropagatePlayerReach(
    const game::poker::PokerTreeNode& node, int hero_player) {
  const int actor = node.node_state->ActorPlayer();
  const NodeCfrLayout& layout = storage_.Layout(node.node_id);
  for (int action = 0; action < layout.num_actions; ++action) {
    const int child_id = tree_.ChildNodeIdAt(node.node_id, action);
    for (int hand = 0; hand < layout.num_hands; ++hand) {
      storage_.ReachAt(child_id, actor, hand) =
          storage_.ReachAt(node.node_id, actor, hand) *
          storage_.StrategyAt(node.node_id, action, hand);
    }
  }

  if (actor == hero_player) {
    for (int action = 0; action < layout.num_actions; ++action) {
      for (int hand = 0; hand < layout.num_hands; ++hand) {
        storage_.SumStrategyAt(node.node_id, action, hand) +=
            storage_.ReachAt(node.node_id, actor, hand) *
            storage_.StrategyAt(node.node_id, action, hand);
      }
    }
  }
}

void PokerCfrSolver::PropagateChanceReach(
    const game::poker::PokerTreeNode& node) {
  for (int child_index = 0; child_index < node.num_children; ++child_index) {
    const int child_id = tree_.ChildNodeIdAt(node.node_id, child_index);
    const IsoTransition& transition = ChanceTransition(child_id);
    for (int player = 0; player < game::poker::GameBasic::kNumPlayers;
         ++player) {
      for (int hand = 0; hand < storage_.NumHands(child_id); ++hand) {
        storage_.ReachAt(child_id, player, hand) = 0.0f;
      }
      for (const IsoTransitionEdge& edge : transition.edges) {
        storage_.ReachAt(child_id, player, edge.child_iso) +=
            storage_.ReachAt(node.node_id, player, edge.parent_iso) *
            edge.weight;
      }
    }
  }
}

void PokerCfrSolver::ComputeTerminalCfvs() {
  for (int node_id : terminal_node_ids_) {
    const game::poker::PokerTreeNode& node = tree_.Node(node_id);
    const game::poker::IsomorphicMapping& mapping =
        *node_mappings_[static_cast<std::size_t>(node_id)];
    for (int player = 0; player < game::poker::GameBasic::kNumPlayers;
         ++player) {
      const std::vector<float> opponent_reach =
          ReachVector(node_id, 1 - player);
      const std::vector<float> cfv = terminal_cfv_calculator_.Calculate(
          *node.node_state, player, mapping, opponent_reach);
      WriteCfvVector(node_id, player, cfv);
    }
  }
}

void PokerCfrSolver::BackwardAndUpdate(int hero_player) {
  for (int node_id : reverse_node_ids_) {
    const game::poker::PokerTreeNode& node = tree_.Node(node_id);
    if (node.node_state->IsTerminal()) {
      continue;
    }
    if (node.node_state->ActorPlayer() ==
        game::poker::NodeState::kChancePlayer) {
      BackwardChanceNode(node);
    } else {
      BackwardPlayerNode(node, hero_player);
    }
  }
}

void PokerCfrSolver::BackwardChanceNode(
    const game::poker::PokerTreeNode& node) {
  for (int player = 0; player < game::poker::GameBasic::kNumPlayers;
       ++player) {
    for (int hand = 0; hand < storage_.NumHands(node.node_id); ++hand) {
      storage_.CfvAt(node.node_id, player, hand) = 0.0f;
    }
  }

  for (int child_index = 0; child_index < node.num_children; ++child_index) {
    const int child_id = tree_.ChildNodeIdAt(node.node_id, child_index);
    const IsoTransition& transition = ChanceTransition(child_id);
    for (int player = 0; player < game::poker::GameBasic::kNumPlayers;
         ++player) {
      for (const IsoTransitionEdge& edge : transition.edges) {
        storage_.CfvAt(node.node_id, player, edge.parent_iso) +=
            storage_.CfvAt(child_id, player, edge.child_iso) * edge.weight *
            transition.chance_prob;
      }
    }
  }
}

void PokerCfrSolver::BackwardPlayerNode(
    const game::poker::PokerTreeNode& node, int hero_player) {
  const int actor = node.node_state->ActorPlayer();
  const NodeCfrLayout& layout = storage_.Layout(node.node_id);

  for (int player = 0; player < game::poker::GameBasic::kNumPlayers;
       ++player) {
    for (int hand = 0; hand < layout.num_hands; ++hand) {
      double value = 0.0;
      for (int action = 0; action < layout.num_actions; ++action) {
        const int child_id = tree_.ChildNodeIdAt(node.node_id, action);
        const double child_value =
            storage_.CfvAt(child_id, player, hand);
        value += player == actor
                     ? static_cast<double>(
                           storage_.StrategyAt(node.node_id, action, hand)) *
                           child_value
                     : child_value;
      }
      storage_.CfvAt(node.node_id, player, hand) = static_cast<float>(value);
    }
  }

  if (actor != hero_player) {
    return;
  }

  for (int action = 0; action < layout.num_actions; ++action) {
    const int child_id = tree_.ChildNodeIdAt(node.node_id, action);
    for (int hand = 0; hand < layout.num_hands; ++hand) {
      storage_.RegretAt(node.node_id, action, hand) +=
          storage_.CfvAt(child_id, actor, hand) -
          storage_.CfvAt(node.node_id, actor, hand);
    }
  }
  UpdateStrategyFromRegret(node.node_id);
}

void PokerCfrSolver::UpdateStrategyFromRegret(int node_id) {
  const NodeCfrLayout& layout = storage_.Layout(node_id);
  for (int hand = 0; hand < layout.num_hands; ++hand) {
    float positive_sum = 0.0f;
    for (int action = 0; action < layout.num_actions; ++action) {
      positive_sum += std::max(0.0f, storage_.RegretAt(node_id, action, hand));
    }
    if (positive_sum <= 0.0f) {
      const float uniform = 1.0f / static_cast<float>(layout.num_actions);
      for (int action = 0; action < layout.num_actions; ++action) {
        storage_.StrategyAt(node_id, action, hand) = uniform;
      }
      continue;
    }
    for (int action = 0; action < layout.num_actions; ++action) {
      storage_.StrategyAt(node_id, action, hand) =
          std::max(0.0f, storage_.RegretAt(node_id, action, hand)) /
          positive_sum;
    }
  }
}

std::vector<float> PokerCfrSolver::ReachVector(int node_id, int player) const {
  std::vector<float> values(static_cast<std::size_t>(storage_.NumHands(node_id)),
                            0.0f);
  for (int hand = 0; hand < storage_.NumHands(node_id); ++hand) {
    values[static_cast<std::size_t>(hand)] =
        storage_.ReachAt(node_id, player, hand);
  }
  return values;
}

void PokerCfrSolver::WriteCfvVector(int node_id, int player,
                                    const std::vector<float>& cfv) {
  if (cfv.size() != static_cast<std::size_t>(storage_.NumHands(node_id))) {
    throw std::invalid_argument("Terminal CFV size does not match node layout");
  }
  for (int hand = 0; hand < storage_.NumHands(node_id); ++hand) {
    storage_.CfvAt(node_id, player, hand) =
        cfv[static_cast<std::size_t>(hand)];
  }
}

}  // namespace fisher::algorithm
