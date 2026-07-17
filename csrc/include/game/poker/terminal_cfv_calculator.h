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
  TerminalCfvCalculator(const GameBasic& game_basic,
                        const SevenCardLookupTable& evaluator);
  ~TerminalCfvCalculator();

  std::vector<float> Calculate(const NodeState& node, int player,
                               const IsomorphicMapping& mapping,
                               const std::vector<float>& opponent_reach);

 private:
  struct RiverShowdownCache;

  void ValidateInput(const NodeState& node, int player,
                     const IsomorphicMapping& mapping,
                     const std::vector<float>& opponent_reach) const;
  std::vector<float> CalculateFold(const NodeState& node, int player,
                                   const IsomorphicMapping& mapping,
                                   const std::vector<float>& opponent_reach)
      const;
  std::vector<float> CalculateRiverShowdown(
      const NodeState& node, int player, const IsomorphicMapping& mapping,
      const std::vector<float>& opponent_reach);
  std::vector<float> CalculateRunoutShowdown(
      const NodeState& node, int player, const IsomorphicMapping& mapping,
      const std::vector<float>& opponent_reach);

  const TerminalWinProbMatrix& MatrixFor(const IsomorphicMapping& mapping);
  const RiverShowdownCache& RiverCacheFor(const IsomorphicMapping& mapping);

  const GameBasic& game_basic_;
  const SevenCardLookupTable& evaluator_;
  std::unordered_map<std::string, std::unique_ptr<TerminalWinProbMatrix>>
      matrix_cache_;
  std::unordered_map<std::string, std::unique_ptr<RiverShowdownCache>>
      river_cache_;
  std::mutex cache_mutex_;
};

}  // namespace fisher::game::poker
