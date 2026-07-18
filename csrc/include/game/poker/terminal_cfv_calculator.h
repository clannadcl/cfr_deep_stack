#pragma once

#include <mutex>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "game/poker/game_basic.h"
#include "game/poker/hand_evaluator.h"
#include "game/poker/isomorphic_mapping.h"
#include "game/poker/node_state.h"
#include "game/poker/terminal_win_prob_matrix.h"

namespace fisher::game::poker {

class TerminalCfvCalculator {
 public:
  struct Profile {
    int64_t fold_calls = 0;
    int64_t runout_batch_calls = 0;
    int64_t river_matrix_batch_calls = 0;
    int64_t river_scan_batch_calls = 0;
    int64_t river_scan_items = 0;
    int64_t runout_batch_items = 0;

    double fold_ms = 0.0;
    double runout_cache_ms = 0.0;
    double runout_matrix_ms = 0.0;
    double runout_multiply_ms = 0.0;
    double runout_valid_mass_ms = 0.0;
    double runout_combine_ms = 0.0;
    double river_scan_cache_ms = 0.0;
    double river_scan_stats_ms = 0.0;
    double river_scan_group_stats_ms = 0.0;
    double river_scan_combine_ms = 0.0;
    double river_scan_accumulate_ms = 0.0;
  };

  TerminalCfvCalculator(const GameBasic& game_basic,
                        const SevenCardLookupTable& evaluator);
  ~TerminalCfvCalculator();

  std::vector<float> Calculate(const NodeState& node, int player,
                               const IsomorphicMapping& mapping,
                               const std::vector<float>& opponent_reach);
  void CalculateInto(const NodeState& node, int player,
                     const IsomorphicMapping& mapping,
                     const float* opponent_reach, float* out_cfv);
  struct BatchItem {
    const NodeState* node = nullptr;
    int player = -1;
    const IsomorphicMapping* mapping = nullptr;
    const float* opponent_reach = nullptr;
    float* out_cfv = nullptr;
  };

  void CalculateRunoutShowdownBatch(const std::vector<BatchItem>& items);
  void CalculateRiverShowdownBatch(const std::vector<BatchItem>& items);
  void CalculateRiverShowdownScanBatch(const std::vector<BatchItem>& items);
  void SetProfilingEnabled(bool enabled);
  bool ProfilingEnabled() const;
  void ResetProfile();
  Profile ProfileSnapshot() const;

 private:
  struct RawHandsCache;
  struct RunoutShowdownCache;
  struct RiverShowdownCache;

  void CalculateFold(const NodeState& node, int player,
                     const IsomorphicMapping& mapping,
                     const float* opponent_reach, float* out_cfv);
  void CalculateRiverShowdown(const NodeState& node, int player,
                              const IsomorphicMapping& mapping,
                              const float* opponent_reach, float* out_cfv);
  void CalculateRiverShowdownWithCache(const NodeState& node, int player,
                                       int num_iso_hands,
                                       const RiverShowdownCache& cache,
                                       const float* opponent_reach,
                                       float* out_cfv);
  void CalculateRunoutShowdown(const NodeState& node, int player,
                               const IsomorphicMapping& mapping,
                               const float* opponent_reach, float* out_cfv);

  const RawHandsCache& RawHandsFor(const IsomorphicMapping& mapping);
  const TerminalWinProbMatrix& MatrixFor(const IsomorphicMapping& mapping);
  const RunoutShowdownCache& RunoutCacheFor(
      const IsomorphicMapping& mapping);
  const RiverShowdownCache& RiverCacheFor(const IsomorphicMapping& mapping);
  void AddProfile(const Profile& profile);

  const GameBasic& game_basic_;
  const SevenCardLookupTable& evaluator_;
  std::unordered_map<std::string, std::unique_ptr<RawHandsCache>>
      raw_hands_cache_;
  std::unordered_map<std::string, std::unique_ptr<TerminalWinProbMatrix>>
      matrix_cache_;
  std::unordered_map<std::string, std::unique_ptr<RunoutShowdownCache>>
      runout_cache_;
  std::unordered_map<std::string, std::unique_ptr<RiverShowdownCache>>
      river_cache_;
  std::mutex cache_mutex_;
  mutable std::mutex profile_mutex_;
  Profile profile_;
  bool profiling_enabled_ = false;
};

}  // namespace fisher::game::poker
