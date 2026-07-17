#include <array>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <vector>

#include "algorithm/best_response_calculator.h"
#include "algorithm/poker_cfr_solver.h"
#include "game/poker/action.h"
#include "game/poker/game_basic.h"
#include "game/poker/poker_cards.h"
#include "game/poker/poker_hand.h"
#include "game/poker/subgame_setup.h"
#include "game/poker/tree_abstracted_bets.h"

namespace {

void Expect(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void ExpectNear(float actual, float expected, const char* message) {
  if (std::fabs(actual - expected) > 1e-4f) {
    throw std::runtime_error(message);
  }
}

void ExpectFinite(float value, const char* message) {
  if (!std::isfinite(value)) {
    throw std::runtime_error(message);
  }
}

template <typename Fn>
void ExpectInvalidArgument(Fn fn, const char* message) {
  try {
    fn();
  } catch (const std::invalid_argument&) {
    return;
  }
  throw std::runtime_error(message);
}

std::vector<std::vector<float>> HeadsUpBelief(const char* player0_hand,
                                              const char* player1_hand) {
  fisher::game::poker::GameBasic game;
  std::vector<std::vector<float>> belief(
      fisher::game::poker::GameBasic::kNumPlayers,
      std::vector<float>(fisher::game::poker::GameBasic::kNumHands, 0.0f));
  belief[0][static_cast<std::size_t>(
      game.HandIndex(fisher::game::poker::PokerHand(player0_hand)))] = 1.0f;
  belief[1][static_cast<std::size_t>(
      game.HandIndex(fisher::game::poker::PokerHand(player1_hand)))] = 1.0f;
  return belief;
}

fisher::game::poker::TreeAbstractedBets AllInOnlyBets() {
  return fisher::game::poker::TreeAbstractedBets(
      fisher::game::poker::AbstractedBetStringConfig{{"allin"}},
      fisher::game::poker::AbstractedDonkBetStringConfig{"allin"});
}

std::shared_ptr<fisher::game::poker::SubgameSetup> MakeSetup(
    fisher::game::poker::PokerCards board,
    std::vector<std::vector<float>> belief) {
  using fisher::game::poker::Action;
  using fisher::game::poker::GameBasic;
  using fisher::game::poker::SubgameSetup;

  return std::make_shared<SubgameSetup>(SubgameSetup::Args(
      board, /*pot=*/10.0f, /*stacks=*/std::array<float, 2>{10.0f, 10.0f},
      /*bet_total=*/std::array<float, 2>{0.0f, 0.0f},
      /*bet_current_round=*/std::array<float, 2>{0.0f, 0.0f},
      /*current_player=*/0, /*last_aggressor=*/0, /*raise_count=*/0,
      std::vector<Action>{}, std::move(belief), AllInOnlyBets(), GameBasic(),
      /*bet_rounding=*/0.1f, /*min_raise_size=*/1.0f));
}

void RunIterations(fisher::algorithm::PokerCfrSolver* solver,
                   int iterations) {
  for (int iteration = 0; iteration < iterations; ++iteration) {
    solver->RunIteration();
  }
}

}  // namespace

int main() {
  using fisher::algorithm::BestResponseCalculator;
  using fisher::algorithm::ExploitabilityResult;
  using fisher::algorithm::PokerCfrSolver;
  using fisher::game::poker::PokerCards;

  auto setup = MakeSetup(PokerCards("AhKhQh2c3d"),
                         HeadsUpBelief("7c8d", "JhTh"));
  PokerCfrSolver solver{PokerCfrSolver::Args(setup, /*num_threads=*/1)};

  const ExploitabilityResult initial_result =
      BestResponseCalculator(&solver).Compute();
  ExpectFinite(initial_result.exploitability,
               "initial exploitability should be finite");
  Expect(initial_result.exploitability >= -1e-4f,
         "initial exploitability should be non-negative");
  Expect(initial_result.best_response_ev[0] >= initial_result.current_ev[0] -
                                             1e-4f,
         "player 0 BR EV should dominate current EV");
  Expect(initial_result.best_response_ev[1] >= initial_result.current_ev[1] -
                                             1e-4f,
         "player 1 BR EV should dominate current EV");
  ExpectNear(initial_result.current_ev[0], solver.NodeEv(0, 0).range_ev,
             "player 0 current EV should match root node EV");
  ExpectNear(initial_result.current_ev[1], solver.NodeEv(0, 1).range_ev,
             "player 1 current EV should match root node EV");
  Expect(initial_result.policy.action_by_node_hand.size() ==
             static_cast<std::size_t>(solver.Tree().NumNodes()),
         "BR policy should contain every node");

  RunIterations(&solver, 50);
  const ExploitabilityResult after_iterations =
      BestResponseCalculator(&solver).Compute();
  ExpectFinite(after_iterations.exploitability,
               "post-solve exploitability should be finite");
  Expect(after_iterations.exploitability >= -1e-4f,
         "post-solve exploitability should be non-negative");

  bool has_best_response_action = false;
  for (const std::vector<int>& node_policy :
       after_iterations.policy.action_by_node_hand) {
    for (int action : node_policy) {
      has_best_response_action = has_best_response_action || action >= 0;
    }
  }
  Expect(has_best_response_action,
         "BR policy should record at least one hero action");

  const std::vector<float> regret_before_resume = solver.Storage().RegretData();
  solver.RunIteration();
  bool regret_changed = false;
  const std::vector<float>& regret_after_resume = solver.Storage().RegretData();
  Expect(regret_before_resume.size() == regret_after_resume.size(),
         "resume should keep regret storage shape");
  for (std::size_t index = 0; index < regret_before_resume.size(); ++index) {
    regret_changed =
        regret_changed ||
        std::fabs(regret_after_resume[index] - regret_before_resume[index]) >
            1e-6f;
  }
  Expect(regret_changed,
         "solver should resume CFR updates after exploitability calculation");

  {
    PokerCfrSolver early_stop_solver{
        PokerCfrSolver::Args(setup, /*num_threads=*/1,
                             /*max_iterations=*/20,
                             /*exploitability_check_interval=*/5,
                             /*target_exploitability=*/1000.0f)};
    const PokerCfrSolver::SolveResult solve_result =
        early_stop_solver.Solve();
    Expect(solve_result.iterations == 5,
           "solver should stop at the first successful exploitability check");
    Expect(solve_result.converged,
           "solver should report convergence when target is met");
    Expect(solve_result.target_exploitability == 1000.0f,
           "solver should preserve explicit exploitability target");
    ExpectFinite(solve_result.exploitability,
                 "solve exploitability should be finite");

    const std::vector<float> regret_before_extra_iteration =
        early_stop_solver.Storage().RegretData();
    early_stop_solver.RunIteration();
    bool extra_iteration_changed_regret = false;
    const std::vector<float>& regret_after_extra_iteration =
        early_stop_solver.Storage().RegretData();
    for (std::size_t index = 0; index < regret_before_extra_iteration.size();
         ++index) {
      extra_iteration_changed_regret =
          extra_iteration_changed_regret ||
          std::fabs(regret_after_extra_iteration[index] -
                    regret_before_extra_iteration[index]) > 1e-6f;
    }
    Expect(extra_iteration_changed_regret,
           "solver should resume after Solve exploitability checks");
  }

  {
    PokerCfrSolver max_iteration_solver{
        PokerCfrSolver::Args(setup, /*num_threads=*/1,
                             /*max_iterations=*/10,
                             /*exploitability_check_interval=*/5,
                             /*target_exploitability=*/0.0f)};
    const PokerCfrSolver::SolveResult solve_result =
        max_iteration_solver.Solve();
    Expect(solve_result.iterations == 10,
           "solver should run to max iterations when target is not met");
    Expect(!solve_result.converged,
           "solver should not report convergence when target is not met");
  }

  ExpectInvalidArgument(
      [&] {
        PokerCfrSolver bad_solver{PokerCfrSolver::Args(
            setup, /*num_threads=*/1, /*max_iterations=*/0,
            /*exploitability_check_interval=*/5)};
        (void)bad_solver;
      },
      "non-positive max iterations should be invalid");
  ExpectInvalidArgument(
      [&] {
        PokerCfrSolver bad_solver{PokerCfrSolver::Args(
            setup, /*num_threads=*/1, /*max_iterations=*/10,
            /*exploitability_check_interval=*/0)};
        (void)bad_solver;
      },
      "non-positive exploitability check interval should be invalid");

  return 0;
}
