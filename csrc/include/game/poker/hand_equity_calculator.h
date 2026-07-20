#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "game/poker/game_basic.h"
#include "game/poker/hand_evaluator.h"
#include "game/poker/isomorphic_mapping.h"
#include "game/poker/node_state.h"
#include "game/poker/terminal_win_prob_matrix.h"

namespace fisher::game::poker {

struct HandEquityResult {
  std::vector<float> equity;
  float range_equity = 0.0f;
};

class HandEquityCalculator {
 public:
  HandEquityCalculator(const GameBasic& game_basic,
                       const SevenCardLookupTable& evaluator);
  ~HandEquityCalculator();

  HandEquityResult Calculate(const NodeState& node, int player,
                             const IsomorphicMapping& mapping,
                             const float* player_reach,
                             const float* opponent_reach);

 private:
  struct RawHandsCache;
  struct RiverShowdownCache;

  HandEquityResult CalculateRiver(const NodeState& node,
                                  const IsomorphicMapping& mapping,
                                  const float* player_reach,
                                  const float* opponent_reach);
  HandEquityResult CalculateRunout(const NodeState& node,
                                   const IsomorphicMapping& mapping,
                                   const float* player_reach,
                                   const float* opponent_reach);

  const RawHandsCache& RawHandsFor(const IsomorphicMapping& mapping);
  const TerminalWinProbMatrix& MatrixFor(const IsomorphicMapping& mapping);
  const RiverShowdownCache& RiverCacheFor(const IsomorphicMapping& mapping);

  const GameBasic& game_basic_;
  const SevenCardLookupTable& evaluator_;
  std::unordered_map<std::string, std::unique_ptr<RawHandsCache>>
      raw_hands_cache_;
  std::unordered_map<std::string, std::unique_ptr<TerminalWinProbMatrix>>
      matrix_cache_;
  std::unordered_map<std::string, std::unique_ptr<RiverShowdownCache>>
      river_cache_;
  std::mutex cache_mutex_;
};

}  // namespace fisher::game::poker
