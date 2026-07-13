#pragma once

#include <vector>

#include "game/poker/poker_tree.h"

namespace fisher::algorithm {

struct NodeCfrLayout {
  int strategy_offset = -1;
  int regret_offset = -1;
  int cfv_offset = -1;
  int num_actions = 0;
  int num_hands = 0;
};

class CfrStorage {
 public:
  CfrStorage(const game::poker::PokerTree& tree, int num_hands);

  int NumNodes() const;
  int NumHands() const;
  int NumActions(int node_id) const;
  const NodeCfrLayout& Layout(int node_id) const;

  float& StrategyAt(int node_id, int action_index, int hand_index);
  const float& StrategyAt(int node_id, int action_index,
                          int hand_index) const;
  float& RegretAt(int node_id, int action_index, int hand_index);
  const float& RegretAt(int node_id, int action_index, int hand_index) const;
  float& CfvAt(int node_id, int hand_index);
  const float& CfvAt(int node_id, int hand_index) const;

  std::vector<float>& StrategyData();
  const std::vector<float>& StrategyData() const;
  std::vector<float>& RegretData();
  const std::vector<float>& RegretData() const;
  std::vector<float>& CfvData();
  const std::vector<float>& CfvData() const;

 private:
  void ValidateNodeId(int node_id) const;
  int ActionHandIndex(const NodeCfrLayout& layout, int action_index,
                      int hand_index, const char* storage_name) const;
  int HandIndex(const NodeCfrLayout& layout, int hand_index) const;

  int num_hands_;
  std::vector<NodeCfrLayout> layouts_;
  std::vector<float> strategy_;
  std::vector<float> regret_;
  std::vector<float> cfv_;
};

}  // namespace fisher::algorithm
