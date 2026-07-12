#pragma once

#include <memory>
#include <optional>
#include <vector>

#include "game/poker/action.h"
#include "game/poker/node_state.h"
#include "game/poker/poker_card.h"

namespace fisher::game::poker {

class SubgameSetup;

struct PokerTreeEdge {
  PokerTreeEdge(Action action, std::optional<PokerCard> chance_card,
                int child_node_id);

  static PokerTreeEdge PlayerAction(Action action, int child_node_id);
  static PokerTreeEdge Chance(PokerCard card, int child_node_id);

  Action action;
  std::optional<PokerCard> chance_card;
  int child_node_id;
};

struct PokerTreeNode {
  PokerTreeNode(int node_id, std::shared_ptr<const NodeState> node_state,
                std::optional<int> parent_node_id);

  int node_id;
  std::shared_ptr<const NodeState> node_state;
  std::optional<int> parent_node_id;
  std::vector<PokerTreeEdge> child_edges;
};

class PokerTree {
 public:
  explicit PokerTree(const NodeState& root_state);
  explicit PokerTree(std::shared_ptr<const SubgameSetup> setup);

  const std::vector<PokerTreeNode>& Nodes() const;
  const PokerTreeNode& Node(int node_id) const;
  const PokerTreeNode& Root() const;
  int NumNodes() const;

  std::optional<int> FindNode(
      const std::vector<Action>& action_history) const;
  std::optional<int> FindNode(const std::vector<PokerTreeEdge>& path) const;

 private:
  void Build(const NodeState& root_state);
  int AddNode(const NodeState& state, std::optional<int> parent_node_id);
  void ExpandPlayerNode(int node_id, std::vector<int>* queue);
  void ExpandChanceNode(int node_id, std::vector<int>* queue);
  std::optional<int> FindNodeFromStrippedPath(
      const std::vector<PokerTreeEdge>& path) const;

  std::vector<PokerTreeNode> nodes_;
};

}  // namespace fisher::game::poker
