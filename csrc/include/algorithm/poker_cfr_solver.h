#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
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

IsoTransition BuildIsoTransition(int parent_node_id, int child_node_id,
                                 const game::poker::IsomorphicMapping &parent,
                                 const game::poker::IsomorphicMapping &child);

class PokerCfrSolver {
public:
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
                  int num_threads = 0, int max_iterations = 500,
                  int exploitability_check_interval = 50,
                  float target_exploitability = -1.0f, bool use_dcfr = true,
                  float positive_regret_discount_exponent = 1.5f,
                  float negative_regret_discount_exponent = 0.5f,
                  float average_strategy_discount_exponent = 2.0f);

    std::shared_ptr<game::poker::SubgameSetup> setup;
    int num_threads = 0;
    int max_iterations = 500;
    int exploitability_check_interval = 50;
    float target_exploitability = -1.0f;
    bool use_dcfr = true;
    float positive_regret_discount_exponent = 1.5f;
    float negative_regret_discount_exponent = 0.5f;
    float average_strategy_discount_exponent = 2.0f;
  };

  struct SolveResult {
    struct Checkpoint {
      int iteration = 0;
      float exploitability = 0.0f;
      std::array<float, 2> current_ev = {0.0f, 0.0f};
      std::array<float, 2> best_response_ev = {0.0f, 0.0f};
      bool converged = false;
    };

    int iterations = 0;
    bool converged = false;
    float target_exploitability = 0.0f;
    float exploitability = 0.0f;
    std::array<float, 2> current_ev = {0.0f, 0.0f};
    std::array<float, 2> best_response_ev = {0.0f, 0.0f};
    std::vector<Checkpoint> checkpoints;
  };

  explicit PokerCfrSolver(const Args &args);
  ~PokerCfrSolver();

  void RunIteration();
  void RunHeroPass(int hero_player);
  SolveResult Solve(float average_epsilon = 1e-12f);
  void FinalizeAverageStrategy(float average_epsilon = 1e-12f);

  std::vector<float> CurrentStrategyData() const;
  std::vector<float> AverageStrategyData(float epsilon = 1e-12f) const;
  float AverageStrategyAt(int node_id, int action_index, int hand_index,
                          float epsilon = 1e-12f) const;
  NodeEvDetail NodeEv(int node_id, int player) const;

  const game::poker::PokerTree &Tree() const;
  int NumThreads() const;
  CfrStorage &Storage();
  const CfrStorage &Storage() const;
  const game::poker::IsomorphicMapping &MappingForNode(int node_id) const;
  const std::vector<int> &TerminalNodeIds() const;
  const std::vector<int> &ReverseNodeIds() const;
  const IsoTransition &ChanceTransition(int child_node_id) const;

private:
  class ThreadPool;
  struct NodeChildCache {
    int first_child_id = -1;
    int num_children = 0;
  };

  struct TerminalWorkItem {
    int node_id = -1;
    const game::poker::NodeState *node_state = nullptr;
    const game::poker::IsomorphicMapping *mapping = nullptr;
  };
  using TerminalBatchItem = game::poker::TerminalCfvCalculator::BatchItem;
  using TerminalBatchItems = std::vector<TerminalBatchItem>;

  void BuildNodeCaches();
  std::uint64_t
  EstimateNodeTraversalWork(const game::poker::PokerTreeNode &node) const;
  bool ShouldParallelizeLevel(std::size_t depth) const;
  std::array<std::vector<int>, 2>
  BuildActiveIsoHands(const game::poker::IsomorphicMapping &mapping) const;
  const std::vector<int> &ActiveIsoHands(int node_id, int player) const;
  IsoTransition BuildChanceTransition(int parent_node_id,
                                      int child_node_id) const;
  void RefreshDcfrDiscounts();
  void InitializeRootReach();
  void ForwardReachAndAccumulateAverage(int hero_player);
  void PropagatePlayerReach(const game::poker::PokerTreeNode &node,
                            int hero_player);
  void PropagateAveragePlayerReach(const game::poker::PokerTreeNode &node,
                                   float average_epsilon);
  void PropagateChanceReach(const game::poker::PokerTreeNode &node);
  void ComputeTerminalCfvs(int player);
  void BackwardAndUpdate(int hero_player);
  void BackwardChanceNode(const game::poker::PokerTreeNode &node, int player);
  void BackwardPlayerNode(const game::poker::PokerTreeNode &node,
                          int hero_player);
  void ApplyRegretDiscount(int node_id);
  void ApplyAverageStrategyDiscount(int node_id);
  void BackwardAveragePlayerNode(const game::poker::PokerTreeNode &node,
                                 float average_epsilon);
  void UpdateStrategyFromRegret(int node_id);
  std::vector<float> ValidMassVector(int node_id, int player) const;
  std::vector<float> ReachVector(int node_id, int player) const;
  void WriteCfvVector(int node_id, int player, const std::vector<float> &cfv);

  std::shared_ptr<game::poker::SubgameSetup> setup_;
  game::poker::PokerTree tree_;
  game::poker::IsomorphicMappingTable mapping_table_;
  CfrStorage storage_;
  game::poker::SevenCardLookupTable evaluator_;
  game::poker::TerminalCfvCalculator terminal_cfv_calculator_;
  std::vector<const game::poker::IsomorphicMapping *> node_mappings_;
  std::vector<NodeChildCache> node_child_caches_;
  std::vector<std::vector<int>> node_ids_by_depth_;
  std::vector<std::uint64_t> node_level_work_;
  std::vector<int> terminal_node_ids_;
  std::vector<TerminalWorkItem> fold_terminal_items_;
  std::array<std::vector<TerminalBatchItem>, 2> fold_terminal_batch_items_;
  std::array<std::vector<TerminalBatchItems>, 2> river_terminal_batch_items_;
  std::array<std::vector<TerminalBatchItems>, 2> runout_terminal_batch_items_;
  std::vector<int> reverse_node_ids_;
  std::vector<IsoTransition> chance_transitions_by_child_id_;
  std::unordered_map<std::string, std::array<std::vector<int>, 2>>
      active_iso_hands_by_key_;
  std::vector<std::array<const std::vector<int> *, 2>>
      active_iso_hands_by_node_player_;
  int num_threads_ = 1;
  std::unique_ptr<ThreadPool> thread_pool_;
  int max_iterations_ = 500;
  int exploitability_check_interval_ = 50;
  float target_exploitability_ = 0.0f;
  bool use_dcfr_ = true;
  float positive_regret_discount_exponent_ = 1.5f;
  float negative_regret_discount_exponent_ = 0.5f;
  float average_strategy_discount_exponent_ = 2.0f;
  int hero_pass_count_ = 0;
  float current_positive_regret_discount_ = 1.0f;
  float current_negative_regret_discount_ = 1.0f;
  float current_average_strategy_discount_ = 1.0f;
  bool average_finalized_ = false;
};

} // namespace fisher::algorithm
