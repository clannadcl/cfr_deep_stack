#include <cmath>
#include <iostream>
#include <stdexcept>

#include "algorithm/cfr.h"
#include "game/kuhn.h"

int main() {
  fisher::game::KuhnPokerGame game;
  fisher::algorithm::KuhnCfrSolver solver(/*cfr_plus=*/false);
  solver.Run(game, 1000);

  const auto strategy = solver.AveragePolicy();
  if (strategy.size() != 12) {
    throw std::runtime_error("expected 12 Kuhn poker information states");
  }

  const auto values = fisher::algorithm::PolicyValue(game, strategy);
  if (std::abs(values[0] - (-1.0 / 18.0)) > 0.01 ||
      std::abs(values[1] - (1.0 / 18.0)) > 0.01) {
    throw std::runtime_error("policy value did not converge near Kuhn Nash value");
  }

  const double exploitability =
      fisher::algorithm::NashExploitability(game, strategy);
  if (exploitability >= 0.01) {
    throw std::runtime_error("nash exploitability is too high");
  }

  std::cout << "kuhn_cfr_test passed: exploitability=" << exploitability << "\n";
  return 0;
}
