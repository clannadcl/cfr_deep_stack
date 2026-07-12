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

bool EdgeMatchesPathStep(const PokerTreeEdge& edge,
                         const PokerTreeEdge& path_step) {
  if (edge.action != path_step.action ||
      edge.chance_card.has_value() != path_step.chance_card.has_value()) {
    return false;
  }
  if (!edge.chance_card.has_value()) {
    return true;
  }
  return edge.chance_card->Value() == path_step.chance_card->Value();
}

}  // namespace

PokerTreeEdge::PokerTreeEdge(Action action,
                             std::optional<PokerCard> chance_card,
                             int child_node_id)
    : action(action),
      chance_card(chance_card),
      child_node_id(child_node_id) {}

PokerTreeEdge PokerTreeEdge::PlayerAction(Action action, int child_node_id) {
  if (action.IsChance()) {
    throw std::invalid_argument(
        "Player poker tree edge cannot use chance action");
  }
  return PokerTreeEdge(action, std::nullopt, child_node_id);
}

PokerTreeEdge PokerTreeEdge::Chance(PokerCard card, int child_node_id) {
  return PokerTreeEdge(Action::Chance(), card, child_node_id);
}

PokerTreeNode::PokerTreeNode(int node_id,
                             std::shared_ptr<const NodeState> node_state,
                             std::optional<int> parent_node_id)
    : node_id(node_id),
      node_state(std::move(node_state)),
      parent_node_id(parent_node_id) {}

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
    const PokerTreeNode& node = Node(current_node_id);
    std::optional<int> matched_node_id;
    for (const PokerTreeEdge& edge : node.child_edges) {
      if (edge.action != action) {
        continue;
      }
      if (matched_node_id.has_value()) {
        return std::nullopt;
      }
      matched_node_id = edge.child_node_id;
    }
    if (!matched_node_id.has_value()) {
      return std::nullopt;
    }
    current_node_id = matched_node_id.value();
  }
  return current_node_id;
}

std::optional<int> PokerTree::FindNode(
    const std::vector<PokerTreeEdge>& path) const {
  std::vector<Action> path_actions;
  path_actions.reserve(path.size());
  for (const PokerTreeEdge& edge : path) {
    path_actions.push_back(edge.action);
  }

  const std::vector<Action>& root_history =
      Root().node_state->ActionHistory();
  const std::size_t start_index =
      IsPrefix(root_history, path_actions) ? root_history.size() : 0;
  std::vector<PokerTreeEdge> stripped_path;
  stripped_path.reserve(path.size() - start_index);
  for (std::size_t index = start_index; index < path.size(); ++index) {
    stripped_path.push_back(path[index]);
  }
  return FindNodeFromStrippedPath(stripped_path);
}

void PokerTree::Build(const NodeState& root_state) {
  nodes_.clear();
  std::vector<int> queue;
  queue.push_back(AddNode(root_state, std::nullopt));

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

int PokerTree::AddNode(const NodeState& state,
                       std::optional<int> parent_node_id) {
  const int node_id = static_cast<int>(nodes_.size());
  nodes_.emplace_back(node_id, std::make_shared<NodeState>(state),
                      parent_node_id);
  return node_id;
}

void PokerTree::ExpandPlayerNode(int node_id, std::vector<int>* queue) {
  const std::shared_ptr<const NodeState> node_state =
      Node(node_id).node_state;
  for (const Action& action : node_state->ValidActions()) {
    if (action.IsChance()) {
      throw std::runtime_error("Player node cannot expand chance action");
    }
    const NodeState child_state = node_state->CommitAction(action);
    const int child_node_id = AddNode(child_state, node_id);
    nodes_[static_cast<std::size_t>(node_id)].child_edges.push_back(
        PokerTreeEdge::PlayerAction(action, child_node_id));
    queue->push_back(child_node_id);
  }
}

void PokerTree::ExpandChanceNode(int node_id, std::vector<int>* queue) {
  const std::shared_ptr<const NodeState> node_state =
      Node(node_id).node_state;
  const std::vector<PokerCard>& deck =
      node_state->Setup()->BasicGame().Deck();
  for (PokerCard card : deck) {
    if (node_state->Board().Contains(card)) {
      continue;
    }
    const NodeState child_state = node_state->CommitChanceAction(card);
    const int child_node_id = AddNode(child_state, node_id);
    nodes_[static_cast<std::size_t>(node_id)].child_edges.push_back(
        PokerTreeEdge::Chance(card, child_node_id));
    queue->push_back(child_node_id);
  }
}

std::optional<int> PokerTree::FindNodeFromStrippedPath(
    const std::vector<PokerTreeEdge>& path) const {
  int current_node_id = 0;
  for (const PokerTreeEdge& path_step : path) {
    const PokerTreeNode& node = Node(current_node_id);
    std::optional<int> matched_node_id;
    for (const PokerTreeEdge& edge : node.child_edges) {
      if (!EdgeMatchesPathStep(edge, path_step)) {
        continue;
      }
      if (matched_node_id.has_value()) {
        return std::nullopt;
      }
      matched_node_id = edge.child_node_id;
    }
    if (!matched_node_id.has_value()) {
      return std::nullopt;
    }
    current_node_id = matched_node_id.value();
  }
  return current_node_id;
}

}  // namespace fisher::game::poker
