#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

#include "algorithm/cfr_storage.h"
#include "game/poker/hand_evaluator.h"
#include "game/poker/isomorphic_mapping.h"
#include "game/poker/poker_tree.h"
#include "game/poker/subgame_setup.h"
#include "game/poker/terminal_cfv_calculator.h"

namespace fisher::algorithm {

struct IsoTransitionEdge {
  int parent_iso = -1;
  int child_iso = -1;
  float weight = 0.0f;
};

struct IsoTransition {
  int parent_node_id = -1;
  int child_node_id = -1;
  float chance_prob = 0.0f;
  std::vector<IsoTransitionEdge> edges;
};

IsoTransition BuildIsoTransition(
    int parent_node_id, int child_node_id,
    const game::poker::IsomorphicMapping& parent,
    const game::poker::IsomorphicMapping& child);

class PokerCfrSolver {
 public:
  struct Args {
    explicit Args(std::shared_ptr<game::poker::SubgameSetup> setup);

    std::shared_ptr<game::poker::SubgameSetup> setup;
  };

  explicit PokerCfrSolver(const Args& args);

  void RunIteration();
  void RunHeroPass(int hero_player);

  std::vector<float> CurrentStrategyData() const;
  std::vector<float> AverageStrategyData(float epsilon = 1e-12f) const;

  const game::poker::PokerTree& Tree() const;
  CfrStorage& Storage();
  const CfrStorage& Storage() const;
  const std::vector<int>& TerminalNodeIds() const;
  const std::vector<int>& ReverseNodeIds() const;
  const IsoTransition& ChanceTransition(int child_node_id) const;

 private:
  void BuildNodeCaches();
  IsoTransition BuildChanceTransition(int parent_node_id,
                                      int child_node_id) const;
  void InitializeRootReach();
  void ForwardReachAndAccumulateAverage(int hero_player);
  void PropagatePlayerReach(const game::poker::PokerTreeNode& node,
                            int hero_player);
  void PropagateChanceReach(const game::poker::PokerTreeNode& node);
  void ComputeTerminalCfvs();
  void BackwardAndUpdate(int hero_player);
  void BackwardChanceNode(const game::poker::PokerTreeNode& node);
  void BackwardPlayerNode(const game::poker::PokerTreeNode& node,
                          int hero_player);
  void UpdateStrategyFromRegret(int node_id);
  std::vector<float> ReachVector(int node_id, int player) const;
  void WriteCfvVector(int node_id, int player, const std::vector<float>& cfv);

  std::shared_ptr<game::poker::SubgameSetup> setup_;
  game::poker::PokerTree tree_;
  game::poker::IsomorphicMappingTable mapping_table_;
  CfrStorage storage_;
  game::poker::SevenCardLookupTable evaluator_;
  game::poker::TerminalCfvCalculator terminal_cfv_calculator_;
  std::vector<const game::poker::IsomorphicMapping*> node_mappings_;
  std::vector<int> terminal_node_ids_;
  std::vector<int> reverse_node_ids_;
  std::unordered_map<int, IsoTransition> chance_transitions_by_child_;
};

}  // namespace fisher::algorithm
