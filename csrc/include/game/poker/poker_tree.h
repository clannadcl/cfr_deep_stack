#pragma once

#include <memory>
#include <optional>
#include <vector>

#include "game/poker/action.h"
#include "game/poker/node_state.h"
#include "game/poker/poker_card.h"

namespace fisher::game::poker {

class SubgameSetup;

struct PokerTreeNode {
  PokerTreeNode(int node_id, std::shared_ptr<const NodeState> node_state,
                int parent_node_id, int depth);

  int node_id;
  std::shared_ptr<const NodeState> node_state;
  int parent_node_id;
  int depth;
  int children_offset = -1;
  int num_children = 0;
};

class PokerTree {
 public:
  explicit PokerTree(const NodeState& root_state);
  explicit PokerTree(std::shared_ptr<const SubgameSetup> setup);

  const std::vector<PokerTreeNode>& Nodes() const;
  const PokerTreeNode& Node(int node_id) const;
  const PokerTreeNode& Root() const;
  int NumNodes() const;
  bool HasChildren(int node_id) const;
  int ChildNodeIdAt(int node_id, int child_index) const;

  std::optional<int> FindNode(
      const std::vector<Action>& action_history) const;
  std::optional<int> FindChild(int node_id, const Action& action) const;
  std::optional<int> FindChanceChild(int node_id, PokerCard card) const;

 private:
  void Build(const NodeState& root_state);
  int AddNode(const NodeState& state, int parent_node_id);
  void ExpandPlayerNode(int node_id, std::vector<int>* queue);
  void ExpandChanceNode(int node_id, std::vector<int>* queue);
  void SetChildrenRange(int node_id, int children_offset, int num_children);

  std::vector<PokerTreeNode> nodes_;
};

}  // namespace fisher::game::poker
