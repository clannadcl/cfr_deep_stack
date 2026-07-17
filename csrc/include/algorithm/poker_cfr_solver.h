#pragma once

#include <array>
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
  struct HeroPassProfile {
    double initialize_root_reach_ms = 0.0;
    double forward_reach_ms = 0.0;
    double terminal_cfv_ms = 0.0;
    double backward_update_ms = 0.0;
    double total_ms = 0.0;
  };

  struct NodeEvDetail {
    int node_id = -1;
    int player = -1;
    std::vector<float> cfv;
    std::vector<float> valid_mass;
    std::vector<float> hand_ev;
    float range_ev = 0.0f;
    float range_mass = 0.0f;
  };

  struct Args {
    explicit Args(std::shared_ptr<game::poker::SubgameSetup> setup,
                  int num_threads = 0);

    std::shared_ptr<game::poker::SubgameSetup> setup;
    int num_threads = 0;
  };

  explicit PokerCfrSolver(const Args& args);

  void RunIteration();
  void RunHeroPass(int hero_player);
  HeroPassProfile RunHeroPassProfiled(int hero_player);
  void FinalizeAverageStrategy(float average_epsilon = 1e-12f);

  std::vector<float> CurrentStrategyData() const;
  std::vector<float> AverageStrategyData(float epsilon = 1e-12f) const;
  float AverageStrategyAt(int node_id, int action_index, int hand_index,
                          float epsilon = 1e-12f) const;
  NodeEvDetail NodeEv(int node_id, int player,
                      float mass_epsilon = 1e-15f) const;

  const game::poker::PokerTree& Tree() const;
  int NumThreads() const;
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
  void PropagateAveragePlayerReach(const game::poker::PokerTreeNode& node,
                                   float average_epsilon);
  void PropagateChanceReach(const game::poker::PokerTreeNode& node);
  void ComputeTerminalCfvs();
  void BackwardAndUpdate(int hero_player);
  void BackwardChanceNode(const game::poker::PokerTreeNode& node);
  void BackwardPlayerNode(const game::poker::PokerTreeNode& node,
                          int hero_player);
  void BackwardAveragePlayerNode(const game::poker::PokerTreeNode& node,
                                 float average_epsilon);
  void UpdateStrategyFromRegret(int node_id);
  std::vector<float> ValidMassVector(int node_id, int player) const;
  std::vector<float> ReachVector(int node_id, int player) const;
  void WriteCfvVector(int node_id, int player, const std::vector<float>& cfv);

  std::shared_ptr<game::poker::SubgameSetup> setup_;
  game::poker::PokerTree tree_;
  game::poker::IsomorphicMappingTable mapping_table_;
  CfrStorage storage_;
  game::poker::SevenCardLookupTable evaluator_;
  game::poker::TerminalCfvCalculator terminal_cfv_calculator_;
  std::vector<const game::poker::IsomorphicMapping*> node_mappings_;
  std::vector<std::vector<int>> node_ids_by_depth_;
  std::vector<int> terminal_node_ids_;
  std::vector<int> reverse_node_ids_;
  std::unordered_map<int, IsoTransition> chance_transitions_by_child_;
  int num_threads_ = 1;
  bool average_finalized_ = false;
};

}  // namespace fisher::algorithm
