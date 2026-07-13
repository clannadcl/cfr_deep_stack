#include "algorithm/cfr_storage.h"

#include <stdexcept>
#include <string>

namespace fisher::algorithm {
namespace {

bool IsPlayerNode(const game::poker::PokerTreeNode& node) {
  return !node.node_state->IsTerminal() &&
         node.node_state->ActorPlayer() != game::poker::NodeState::kChancePlayer;
}

}  // namespace

CfrStorage::CfrStorage(const game::poker::PokerTree& tree, int num_hands)
    : num_hands_(num_hands) {
  if (num_hands <= 0) {
    throw std::invalid_argument("CFR storage num_hands must be positive");
  }

  layouts_.resize(static_cast<std::size_t>(tree.NumNodes()));
  for (const game::poker::PokerTreeNode& node : tree.Nodes()) {
    NodeCfrLayout& layout = layouts_[static_cast<std::size_t>(node.node_id)];
    layout.num_hands = num_hands_;
    layout.cfv_offset = static_cast<int>(cfv_.size());
    cfv_.resize(cfv_.size() + static_cast<std::size_t>(num_hands_), 0.0f);

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
    const int action_hand_size = layout.num_actions * num_hands_;
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

int CfrStorage::NumHands() const { return num_hands_; }

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

std::vector<float>& CfrStorage::StrategyData() { return strategy_; }

const std::vector<float>& CfrStorage::StrategyData() const {
  return strategy_;
}

std::vector<float>& CfrStorage::RegretData() { return regret_; }

const std::vector<float>& CfrStorage::RegretData() const { return regret_; }

std::vector<float>& CfrStorage::CfvData() { return cfv_; }

const std::vector<float>& CfrStorage::CfvData() const { return cfv_; }

void CfrStorage::ValidateNodeId(int node_id) const {
  if (node_id < 0 || node_id >= static_cast<int>(layouts_.size())) {
    throw std::invalid_argument("CFR storage node id is out of range");
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

}  // namespace fisher::algorithm
