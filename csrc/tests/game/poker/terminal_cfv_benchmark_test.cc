#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

#include "game/poker/action.h"
#include "game/poker/belief.h"
#include "game/poker/game_basic.h"
#include "game/poker/hand_evaluator.h"
#include "game/poker/isomorphic_mapping.h"
#include "game/poker/node_state.h"
#include "game/poker/poker_cards.h"
#include "game/poker/subgame_setup.h"
#include "game/poker/terminal_cfv_calculator.h"
#include "game/poker/tree_abstracted_bets.h"

namespace {

using Clock = std::chrono::steady_clock;

void Expect(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

double MillisecondsSince(Clock::time_point begin, Clock::time_point end) {
  return std::chrono::duration<double, std::milli>(end - begin).count();
}

std::vector<std::vector<float>> MatrixBelief(float value) {
  return std::vector<std::vector<float>>(
      fisher::game::poker::GameBasic::kNumPlayers,
      std::vector<float>(fisher::game::poker::GameBasic::kNumHands, value));
}

std::shared_ptr<fisher::game::poker::SubgameSetup> MakeSetup(
    const fisher::game::poker::PokerCards& board) {
  using fisher::game::poker::Action;
  using fisher::game::poker::GameBasic;
  using fisher::game::poker::SubgameSetup;
  using fisher::game::poker::TreeAbstractedBets;

  return std::make_shared<SubgameSetup>(SubgameSetup::Args(
      board, /*pot=*/20.0f, /*stacks=*/std::array<float, 2>{0.0f, 0.0f},
      /*bet_total=*/std::array<float, 2>{100.0f, 100.0f},
      /*bet_current_round=*/std::array<float, 2>{100.0f, 100.0f},
      /*current_player=*/0, /*last_aggressor=*/1, /*raise_count=*/1,
      std::vector<Action>{},
      MatrixBelief(1.0f), TreeAbstractedBets(TreeAbstractedBets::Args{}),
      GameBasic(), /*bet_rounding=*/0.1f, /*min_raise_size=*/1.0f));
}

fisher::game::poker::NodeState MakeShowdownNode(
    std::shared_ptr<const fisher::game::poker::SubgameSetup> setup) {
  using fisher::game::poker::NodeState;
  using fisher::game::poker::TerminalStatus;

  return NodeState(NodeState::Args(
      setup, setup->Board(), setup->Pot(), setup->Stacks(),
      setup->BetTotal(), setup->BetCurrentRound(),
      NodeState::kTerminalPlayer, setup->LastAggressor(), setup->RaiseCount(),
      /*is_fold=*/std::array<bool, 2>{false, false},
      TerminalStatus::kShowdownTerminal, setup->RootActionHistory()));
}

std::vector<bool> AllPossibleHands() {
  return std::vector<bool>(fisher::game::poker::GameBasic::kNumHands, true);
}

std::vector<float> DenseReach(
    const fisher::game::poker::IsomorphicMapping& mapping) {
  std::vector<float> reach(static_cast<std::size_t>(mapping.NumIsoHands()),
                           1.0f);
  for (int iso = 0; iso < mapping.NumIsoHands(); ++iso) {
    reach[static_cast<std::size_t>(iso)] =
        1.0f + static_cast<float>((iso * 17) % 101) / 101.0f;
  }
  return reach;
}

struct TimingSummary {
  double mean_ms = 0.0;
  double p50_ms = 0.0;
  double p95_ms = 0.0;
};

TimingSummary Summarize(std::vector<double> timings) {
  Expect(!timings.empty(), "benchmark timings cannot be empty");
  std::sort(timings.begin(), timings.end());
  const double sum =
      std::accumulate(timings.begin(), timings.end(), 0.0);
  const std::size_t p50_index = timings.size() / 2;
  const std::size_t p95_index =
      std::min(timings.size() - 1,
               static_cast<std::size_t>(
                   std::ceil(static_cast<double>(timings.size()) * 0.95) - 1));
  return TimingSummary{sum / static_cast<double>(timings.size()),
                       timings[p50_index], timings[p95_index]};
}

void BenchmarkCase(const std::string& name,
                   const fisher::game::poker::PokerCards& board,
                   int warm_iterations) {
  using fisher::game::poker::GameBasic;
  using fisher::game::poker::IsomorphicMapping;
  using fisher::game::poker::SevenCardLookupTable;
  using fisher::game::poker::TerminalCfvCalculator;

  GameBasic game_basic;
  SevenCardLookupTable evaluator;
  TerminalCfvCalculator calculator(game_basic, evaluator);
  IsomorphicMapping mapping(game_basic, board, AllPossibleHands());
  const auto setup = MakeSetup(board);
  const auto node = MakeShowdownNode(setup);
  const std::vector<float> opponent_reach = DenseReach(mapping);

  const auto cold_begin = Clock::now();
  const std::vector<float> cold_cfv =
      calculator.Calculate(node, /*player=*/0, mapping, opponent_reach);
  const auto cold_end = Clock::now();
  Expect(cold_cfv.size() == static_cast<std::size_t>(mapping.NumIsoHands()),
         "cold CFV size mismatch");

  std::vector<double> timings;
  timings.reserve(static_cast<std::size_t>(warm_iterations));
  double checksum = 0.0;
  for (int iteration = 0; iteration < warm_iterations; ++iteration) {
    const auto begin = Clock::now();
    const std::vector<float> cfv =
        calculator.Calculate(node, /*player=*/0, mapping, opponent_reach);
    const auto end = Clock::now();
    timings.push_back(MillisecondsSince(begin, end));
    checksum += cfv.empty() ? 0.0 : static_cast<double>(cfv.front());
  }

  const TimingSummary summary = Summarize(std::move(timings));
  std::cout << "terminal_cfv_benchmark"
            << " name=" << name
            << " board=" << board.ToString()
            << " board_cards=" << board.Size()
            << " num_iso_hands=" << mapping.NumIsoHands()
            << " cold_ms=" << MillisecondsSince(cold_begin, cold_end)
            << " warm_iterations=" << warm_iterations
            << " warm_mean_ms=" << summary.mean_ms
            << " warm_p50_ms=" << summary.p50_ms
            << " warm_p95_ms=" << summary.p95_ms
            << " checksum=" << checksum << '\n';
}

}  // namespace

int main() {
  using fisher::game::poker::PokerCards;

  BenchmarkCase("river_showdown", PokerCards("Kd8d4d4s4c"),
                /*warm_iterations=*/100);
  BenchmarkCase("turn_allin", PokerCards("QsTh4dQd"),
                /*warm_iterations=*/50);
  BenchmarkCase("flop_allin", PokerCards("QsTh4d"),
                /*warm_iterations=*/20);

  return 0;
}
