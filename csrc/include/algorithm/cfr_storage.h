#pragma once

#include <array>
#include <vector>

#include "game/poker/poker_tree.h"

namespace fisher::algorithm {

struct NodeCfrLayout {
  int strategy_offset = -1;
  int regret_offset = -1;
  int cfv_offset = -1;
  std::array<int, 2> reach_offset = {-1, -1};
  int num_actions = 0;
  int num_hands = 0;
};

class CfrStorage {
 public:
  explicit CfrStorage(const game::poker::PokerTree& tree);

  int NumNodes() const;
  int NumHands(int node_id) const;
  int NumActions(int node_id) const;
  const NodeCfrLayout& Layout(int node_id) const;

  float& StrategyAt(int node_id, int action_index, int hand_index);
  const float& StrategyAt(int node_id, int action_index,
                          int hand_index) const;
  float& RegretAt(int node_id, int action_index, int hand_index);
  const float& RegretAt(int node_id, int action_index, int hand_index) const;
  float& CfvAt(int node_id, int hand_index);
  const float& CfvAt(int node_id, int hand_index) const;
  float& ReachAt(int node_id, int player, int hand_index);
  const float& ReachAt(int node_id, int player, int hand_index) const;

  std::vector<float>& StrategyData();
  const std::vector<float>& StrategyData() const;
  std::vector<float>& RegretData();
  const std::vector<float>& RegretData() const;
  std::vector<float>& CfvData();
  const std::vector<float>& CfvData() const;
  std::vector<float>& ReachData();
  const std::vector<float>& ReachData() const;

 private:
  int AllocateReachBlock(int num_hands);
  void InitializeReachLayout(const game::poker::PokerTree& tree,
                             const game::poker::PokerTreeNode& node);
  void ValidateNodeId(int node_id) const;
  void ValidatePlayer(int player) const;
  int ActionHandIndex(const NodeCfrLayout& layout, int action_index,
                      int hand_index, const char* storage_name) const;
  int HandIndex(const NodeCfrLayout& layout, int hand_index) const;
  int PlayerHandIndex(const NodeCfrLayout& layout, int player,
                      int hand_index) const;

  std::vector<NodeCfrLayout> layouts_;
  std::vector<float> strategy_;
  std::vector<float> regret_;
  std::vector<float> cfv_;
  std::vector<float> reach_;
};

}  // namespace fisher::algorithm
