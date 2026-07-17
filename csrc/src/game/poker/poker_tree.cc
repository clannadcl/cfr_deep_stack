#include "game/poker/poker_tree.h"

#include <stdexcept>
#include <utility>

#include "game/poker/subgame_setup.h"

namespace fisher::game::poker {
namespace {

bool IsPrefix(const std::vector<Action>& prefix,
              const std::vector<Action>& values) {
  if (prefix.size() > values.size()) {
    return false;
  }
  for (std::size_t index = 0; index < prefix.size(); ++index) {
    if (prefix[index] != values[index]) {
      return false;
    }
  }
  return true;
}

}  // namespace

PokerTreeNode::PokerTreeNode(int node_id,
                             std::shared_ptr<const NodeState> node_state,
                             int parent_node_id, int depth)
    : node_id(node_id),
      node_state(std::move(node_state)),
      parent_node_id(parent_node_id),
      depth(depth) {}

PokerTree::PokerTree(const NodeState& root_state) { Build(root_state); }

PokerTree::PokerTree(std::shared_ptr<const SubgameSetup> setup) {
  if (setup == nullptr) {
    throw std::invalid_argument("PokerTree setup cannot be null");
  }
  Build(setup->GetRootNodeState());
}

const std::vector<PokerTreeNode>& PokerTree::Nodes() const { return nodes_; }

const PokerTreeNode& PokerTree::Node(int node_id) const {
  if (node_id < 0 || node_id >= static_cast<int>(nodes_.size())) {
    throw std::invalid_argument("Poker tree node id is out of range");
  }
  return nodes_[static_cast<std::size_t>(node_id)];
}

const PokerTreeNode& PokerTree::Root() const { return Node(0); }

int PokerTree::NumNodes() const { return static_cast<int>(nodes_.size()); }

bool PokerTree::HasChildren(int node_id) const {
  return Node(node_id).num_children > 0;
}

int PokerTree::ChildNodeIdAt(int node_id, int child_index) const {
  const PokerTreeNode& node = Node(node_id);
  if (child_index < 0 || child_index >= node.num_children) {
    throw std::invalid_argument("Poker tree child index is out of range");
  }
  return node.children_offset + child_index;
}

std::optional<int> PokerTree::FindNode(
    const std::vector<Action>& action_history) const {
  const std::vector<Action>& root_history =
      Root().node_state->ActionHistory();
  const std::size_t start_index =
      IsPrefix(root_history, action_history) ? root_history.size() : 0;
  int current_node_id = 0;
  for (std::size_t action_index = start_index;
       action_index < action_history.size(); ++action_index) {
    const Action& action = action_history[action_index];
    const std::optional<int> child_node_id =
        FindChild(current_node_id, action);
    if (!child_node_id.has_value()) {
      return std::nullopt;
    }
    current_node_id = child_node_id.value();
  }
  return current_node_id;
}

std::optional<int> PokerTree::FindChild(int node_id,
                                        const Action& action) const {
  const PokerTreeNode& node = Node(node_id);
  if (!HasChildren(node_id) ||
      node.node_state->ActorPlayer() == NodeState::kChancePlayer ||
      action.IsChance()) {
    return std::nullopt;
  }

  const std::vector<Action>& valid_actions = node.node_state->ValidActions();
  if (static_cast<int>(valid_actions.size()) != node.num_children) {
    throw std::runtime_error(
        "Player node valid action count does not match child count");
  }
  for (std::size_t index = 0; index < valid_actions.size(); ++index) {
    if (valid_actions[index] == action) {
      return ChildNodeIdAt(node_id, static_cast<int>(index));
    }
  }
  return std::nullopt;
}

std::optional<int> PokerTree::FindChanceChild(int node_id,
                                              PokerCard card) const {
  const PokerTreeNode& node = Node(node_id);
  if (!HasChildren(node_id) ||
      node.node_state->ActorPlayer() != NodeState::kChancePlayer) {
    return std::nullopt;
  }

  int chance_index = 0;
  const std::vector<PokerCard>& deck =
      node.node_state->Setup()->BasicGame().Deck();
  for (PokerCard deck_card : deck) {
    if (node.node_state->Board().Contains(deck_card)) {
      continue;
    }
    if (deck_card.Value() == card.Value()) {
      if (chance_index >= node.num_children) {
        throw std::runtime_error(
            "Chance child count does not match available deck cards");
      }
      return ChildNodeIdAt(node_id, chance_index);
    }
    ++chance_index;
  }
  return std::nullopt;
}

void PokerTree::Build(const NodeState& root_state) {
  nodes_.clear();
  std::vector<int> queue;
  queue.push_back(AddNode(root_state, -1));

  for (std::size_t queue_index = 0; queue_index < queue.size();
       ++queue_index) {
    PokerTreeNode* node = &nodes_[static_cast<std::size_t>(
        queue[queue_index])];
    if (node->node_state->IsTerminal()) {
      continue;
    }
    if (node->node_state->ActorPlayer() == NodeState::kChancePlayer) {
      ExpandChanceNode(node->node_id, &queue);
    } else {
      ExpandPlayerNode(node->node_id, &queue);
    }
  }
}

int PokerTree::AddNode(const NodeState& state, int parent_node_id) {
  const int node_id = static_cast<int>(nodes_.size());
  const int depth = parent_node_id < 0 ? 0 : Node(parent_node_id).depth + 1;
  nodes_.emplace_back(node_id, std::make_shared<NodeState>(state),
                      parent_node_id, depth);
  return node_id;
}

void PokerTree::ExpandPlayerNode(int node_id, std::vector<int>* queue) {
  const std::shared_ptr<const NodeState> node_state =
      Node(node_id).node_state;
  const int children_offset = static_cast<int>(nodes_.size());
  int num_children = 0;
  for (const Action& action : node_state->ValidActions()) {
    if (action.IsChance()) {
      throw std::runtime_error("Player node cannot expand chance action");
    }
    const NodeState child_state = node_state->CommitAction(action);
    const int child_node_id = AddNode(child_state, node_id);
    queue->push_back(child_node_id);
    ++num_children;
  }
  SetChildrenRange(node_id, children_offset, num_children);
}

void PokerTree::ExpandChanceNode(int node_id, std::vector<int>* queue) {
  const std::shared_ptr<const NodeState> node_state =
      Node(node_id).node_state;
  const std::vector<PokerCard>& deck =
      node_state->Setup()->BasicGame().Deck();
  const int children_offset = static_cast<int>(nodes_.size());
  int num_children = 0;
  for (PokerCard card : deck) {
    if (node_state->Board().Contains(card)) {
      continue;
    }
    const NodeState child_state = node_state->CommitChanceAction(card);
    const int child_node_id = AddNode(child_state, node_id);
    queue->push_back(child_node_id);
    ++num_children;
  }
  SetChildrenRange(node_id, children_offset, num_children);
}

void PokerTree::SetChildrenRange(int node_id, int children_offset,
                                 int num_children) {
  if (num_children < 0) {
    throw std::invalid_argument("Poker tree child count cannot be negative");
  }
  PokerTreeNode& node = nodes_[static_cast<std::size_t>(node_id)];
  node.children_offset = num_children == 0 ? -1 : children_offset;
  node.num_children = num_children;
}

}  // namespace fisher::game::poker
