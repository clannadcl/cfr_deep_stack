#pragma once

#include <array>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "game/kuhn.h"

namespace fisher::algorithm {

using Strategy = std::map<std::string, std::map<int, double>>;

struct InfoStateNode {
  std::vector<int> legal_actions;
  std::unordered_map<int, double> cumulative_regret;
  std::unordered_map<int, double> cumulative_policy;
};

class KuhnCfrSolver {
 public:
  explicit KuhnCfrSolver(bool cfr_plus = false);

  void Run(const game::KuhnPokerGame& game, int iterations);
  Strategy AveragePolicy() const;
  int Iterations() const;

 private:
  double Cfr(const game::KuhnState& state, int updating_player,
             const std::array<double, 2>& reach);
  InfoStateNode& NodeForState(const game::KuhnState& state, int player);

  bool cfr_plus_;
  int iterations_ = 0;
  std::unordered_map<std::string, InfoStateNode> info_state_nodes_;
};

std::array<double, 2> PolicyValue(const game::KuhnPokerGame& game,
                                  const Strategy& strategy);

double NashExploitability(const game::KuhnPokerGame& game,
                          const Strategy& strategy);

}  // namespace fisher::algorithm
