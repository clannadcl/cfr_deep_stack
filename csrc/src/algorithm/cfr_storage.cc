#include "algorithm/cfr_storage.h"

#include <stdexcept>
#include <string>

#include "game/poker/game_basic.h"

namespace fisher::algorithm {
namespace {

bool IsPlayerNode(const game::poker::PokerTreeNode& node) {
  return !node.node_state->IsTerminal() &&
         node.node_state->ActorPlayer() != game::poker::NodeState::kChancePlayer;
}

}  // namespace

CfrStorage::CfrStorage(const game::poker::PokerTree& tree) {
  layouts_.resize(static_cast<std::size_t>(tree.NumNodes()));
  for (const game::poker::PokerTreeNode& node : tree.Nodes()) {
    NodeCfrLayout& layout = layouts_[static_cast<std::size_t>(node.node_id)];
    layout.num_hands = node.node_state->NumHands();
    if (layout.num_hands <= 0) {
      throw std::invalid_argument("CFR storage node num_hands must be positive");
    }
    layout.cfv_offset = static_cast<int>(cfv_.size());
    cfv_.resize(cfv_.size() + static_cast<std::size_t>(layout.num_hands),
                0.0f);
    InitializeReachLayout(tree, node);

    if (!IsPlayerNode(node)) {
      continue;
    }

    if (node.num_children <= 0) {
      throw std::invalid_argument(
          "Player node must have at least one child in CFR storage");
    }
    if (node.node_state->ValidActions().size() !=
        static_cast<std::size_t>(node.num_children)) {
      throw std::invalid_argument(
          "Player node action count must match child count");
    }

    layout.num_actions = node.num_children;
    const int action_hand_size = layout.num_actions * layout.num_hands;
    layout.strategy_offset = static_cast<int>(strategy_.size());
    strategy_.resize(strategy_.size() +
                         static_cast<std::size_t>(action_hand_size),
                     0.0f);
    layout.regret_offset = static_cast<int>(regret_.size());
    regret_.resize(regret_.size() + static_cast<std::size_t>(action_hand_size),
                   0.0f);
  }
}

int CfrStorage::NumNodes() const {
  return static_cast<int>(layouts_.size());
}

int CfrStorage::NumHands(int node_id) const {
  return Layout(node_id).num_hands;
}

int CfrStorage::NumActions(int node_id) const {
  return Layout(node_id).num_actions;
}

const NodeCfrLayout& CfrStorage::Layout(int node_id) const {
  ValidateNodeId(node_id);
  return layouts_[static_cast<std::size_t>(node_id)];
}

float& CfrStorage::StrategyAt(int node_id, int action_index,
                              int hand_index) {
  const NodeCfrLayout& layout = Layout(node_id);
  return strategy_[static_cast<std::size_t>(
      ActionHandIndex(layout, action_index, hand_index, "strategy"))];
}

const float& CfrStorage::StrategyAt(int node_id, int action_index,
                                    int hand_index) const {
  const NodeCfrLayout& layout = Layout(node_id);
  return strategy_[static_cast<std::size_t>(
      ActionHandIndex(layout, action_index, hand_index, "strategy"))];
}

float& CfrStorage::RegretAt(int node_id, int action_index, int hand_index) {
  const NodeCfrLayout& layout = Layout(node_id);
  return regret_[static_cast<std::size_t>(
      ActionHandIndex(layout, action_index, hand_index, "regret"))];
}

const float& CfrStorage::RegretAt(int node_id, int action_index,
                                  int hand_index) const {
  const NodeCfrLayout& layout = Layout(node_id);
  return regret_[static_cast<std::size_t>(
      ActionHandIndex(layout, action_index, hand_index, "regret"))];
}

float& CfrStorage::CfvAt(int node_id, int hand_index) {
  const NodeCfrLayout& layout = Layout(node_id);
  return cfv_[static_cast<std::size_t>(HandIndex(layout, hand_index))];
}

const float& CfrStorage::CfvAt(int node_id, int hand_index) const {
  const NodeCfrLayout& layout = Layout(node_id);
  return cfv_[static_cast<std::size_t>(HandIndex(layout, hand_index))];
}

float& CfrStorage::ReachAt(int node_id, int player, int hand_index) {
  const NodeCfrLayout& layout = Layout(node_id);
  return reach_[static_cast<std::size_t>(
      PlayerHandIndex(layout, player, hand_index))];
}

const float& CfrStorage::ReachAt(int node_id, int player,
                                 int hand_index) const {
  const NodeCfrLayout& layout = Layout(node_id);
  return reach_[static_cast<std::size_t>(
      PlayerHandIndex(layout, player, hand_index))];
}

std::vector<float>& CfrStorage::StrategyData() { return strategy_; }

const std::vector<float>& CfrStorage::StrategyData() const {
  return strategy_;
}

std::vector<float>& CfrStorage::RegretData() { return regret_; }

const std::vector<float>& CfrStorage::RegretData() const { return regret_; }

std::vector<float>& CfrStorage::CfvData() { return cfv_; }

const std::vector<float>& CfrStorage::CfvData() const { return cfv_; }

std::vector<float>& CfrStorage::ReachData() { return reach_; }

const std::vector<float>& CfrStorage::ReachData() const { return reach_; }

int CfrStorage::AllocateReachBlock(int num_hands) {
  if (num_hands <= 0) {
    throw std::invalid_argument("CFR reach num_hands must be positive");
  }
  const int offset = static_cast<int>(reach_.size());
  reach_.resize(reach_.size() + static_cast<std::size_t>(num_hands), 0.0f);
  return offset;
}

void CfrStorage::InitializeReachLayout(
    const game::poker::PokerTree& tree,
    const game::poker::PokerTreeNode& node) {
  NodeCfrLayout& layout = layouts_[static_cast<std::size_t>(node.node_id)];
  if (node.parent_node_id < 0) {
    for (int player = 0; player < game::poker::GameBasic::kNumPlayers;
         ++player) {
      layout.reach_offset[static_cast<std::size_t>(player)] =
          AllocateReachBlock(layout.num_hands);
    }
    return;
  }

  const game::poker::PokerTreeNode& parent = tree.Node(node.parent_node_id);
  const NodeCfrLayout& parent_layout =
      layouts_[static_cast<std::size_t>(parent.node_id)];
  const int parent_actor = parent.node_state->ActorPlayer();
  const bool parent_is_player =
      parent_actor >= 0 && parent_actor < game::poker::GameBasic::kNumPlayers;
  const bool can_share_with_parent =
      parent_layout.num_hands == layout.num_hands;

  for (int player = 0; player < game::poker::GameBasic::kNumPlayers;
       ++player) {
    const std::size_t player_index = static_cast<std::size_t>(player);
    if (parent_is_player && can_share_with_parent && player != parent_actor) {
      layout.reach_offset[player_index] =
          parent_layout.reach_offset[player_index];
    } else {
      layout.reach_offset[player_index] = AllocateReachBlock(layout.num_hands);
    }
  }
}

void CfrStorage::ValidateNodeId(int node_id) const {
  if (node_id < 0 || node_id >= static_cast<int>(layouts_.size())) {
    throw std::invalid_argument("CFR storage node id is out of range");
  }
}

void CfrStorage::ValidatePlayer(int player) const {
  if (player < 0 || player >= game::poker::GameBasic::kNumPlayers) {
    throw std::invalid_argument("CFR storage player is out of range");
  }
}

int CfrStorage::ActionHandIndex(const NodeCfrLayout& layout, int action_index,
                                int hand_index,
                                const char* storage_name) const {
  if (layout.num_actions <= 0) {
    throw std::invalid_argument(
        std::string("CFR ") + storage_name +
        " storage is only available for player nodes");
  }
  if (action_index < 0 || action_index >= layout.num_actions) {
    throw std::invalid_argument(
        std::string("CFR ") + storage_name + " action index is out of range");
  }
  if (hand_index < 0 || hand_index >= layout.num_hands) {
    throw std::invalid_argument(
        std::string("CFR ") + storage_name + " hand index is out of range");
  }

  const int offset =
      storage_name == std::string("strategy") ? layout.strategy_offset
                                               : layout.regret_offset;
  if (offset < 0) {
    throw std::invalid_argument(
        std::string("CFR ") + storage_name + " storage offset is invalid");
  }
  return offset + action_index * layout.num_hands + hand_index;
}

int CfrStorage::HandIndex(const NodeCfrLayout& layout, int hand_index) const {
  if (hand_index < 0 || hand_index >= layout.num_hands) {
    throw std::invalid_argument("CFR cfv hand index is out of range");
  }
  if (layout.cfv_offset < 0) {
    throw std::invalid_argument("CFR cfv storage offset is invalid");
  }
  return layout.cfv_offset + hand_index;
}

int CfrStorage::PlayerHandIndex(const NodeCfrLayout& layout, int player,
                                int hand_index) const {
  ValidatePlayer(player);
  if (hand_index < 0 || hand_index >= layout.num_hands) {
    throw std::invalid_argument("CFR reach hand index is out of range");
  }
  const int offset =
      layout.reach_offset[static_cast<std::size_t>(player)];
  if (offset < 0) {
    throw std::invalid_argument("CFR reach storage offset is invalid");
  }
  return offset + hand_index;
}

}  // namespace fisher::algorithm
