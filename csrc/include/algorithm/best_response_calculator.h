#pragma once

#include <array>
#include <vector>

#include "algorithm/poker_cfr_solver.h"

namespace fisher::algorithm {

struct BestResponsePolicy {
  std::vector<std::vector<int>> action_by_node_hand;
};

struct ExploitabilityResult {
  std::array<float, 2> current_ev = {0.0f, 0.0f};
  std::array<float, 2> best_response_ev = {0.0f, 0.0f};
  float exploitability = 0.0f;
  BestResponsePolicy policy;
};

class BestResponseCalculator {
 public:
  explicit BestResponseCalculator(PokerCfrSolver* solver);

  ExploitabilityResult Compute(float average_epsilon = 1e-12f);

 private:
  std::vector<float> ComputeBestResponseCfv(int hero_player,
                                            BestResponsePolicy* policy) const;
  float ComputeRootEv(int player,
                      const std::vector<float>& root_cfv) const;

  PokerCfrSolver* solver_;
};

}  // namespace fisher::algorithm
