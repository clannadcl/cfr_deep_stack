#include <array>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <vector>

#include "algorithm/poker_cfr_solver.h"
#include "game/poker/action.h"
#include "game/poker/belief.h"
#include "game/poker/game_basic.h"
#include "game/poker/isomorphic_mapping.h"
#include "game/poker/poker_cards.h"
#include "game/poker/poker_hand.h"
#include "game/poker/poker_tree.h"
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

void ExpectVectorNear(const std::vector<float>& actual,
                      const std::vector<float>& expected,
                      const char* message) {
  if (actual.size() != expected.size()) {
    throw std::runtime_error(message);
  }
  for (std::size_t index = 0; index < actual.size(); ++index) {
    if (std::fabs(actual[index] - expected[index]) > 1e-4f) {
      throw std::runtime_error(message);
    }
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

std::vector<std::vector<float>> MatrixBelief(float value) {
  return std::vector<std::vector<float>>(
      fisher::game::poker::GameBasic::kNumPlayers,
      std::vector<float>(fisher::game::poker::GameBasic::kNumHands, value));
}

std::vector<std::vector<float>> BucketClosedHeadsUpBelief(
    const fisher::game::poker::PokerCards& board, const char* player0_hand,
    const char* player1_hand) {
  fisher::game::poker::GameBasic game;
  fisher::game::poker::IsomorphicMapping mapping(
      game, board,
      std::vector<bool>(fisher::game::poker::GameBasic::kNumHands, true));
  const int player0_iso = mapping.RawToIso(game.HandIndex(
      fisher::game::poker::PokerHand(player0_hand)));
  const int player1_iso = mapping.RawToIso(game.HandIndex(
      fisher::game::poker::PokerHand(player1_hand)));
  if (player0_iso < 0 || player1_iso < 0) {
    throw std::runtime_error("bucket-closed test hand collides with board");
  }

  std::vector<std::vector<float>> belief(
      fisher::game::poker::GameBasic::kNumPlayers,
      std::vector<float>(fisher::game::poker::GameBasic::kNumHands, 0.0f));
  for (int raw = 0; raw < fisher::game::poker::GameBasic::kNumHands; ++raw) {
    const int iso = mapping.RawToIso(raw);
    if (iso == player0_iso) {
      belief[0][static_cast<std::size_t>(raw)] = 1.0f;
    }
    if (iso == player1_iso) {
      belief[1][static_cast<std::size_t>(raw)] = 1.0f;
    }
  }
  return belief;
}

fisher::game::poker::TreeAbstractedBets AllInOnlyBets() {
  return fisher::game::poker::TreeAbstractedBets(
      fisher::game::poker::AbstractedBetStringConfig{{"allin"}},
      fisher::game::poker::AbstractedDonkBetStringConfig{"allin"});
}

std::shared_ptr<fisher::game::poker::SubgameSetup> MakeSetup(
    fisher::game::poker::PokerCards board, float pot,
    std::array<float, 2> stacks, std::array<float, 2> bet_total,
    std::array<float, 2> bet_current_round, int current_player,
    int last_aggressor, int raise_count,
    std::vector<std::vector<float>> belief = MatrixBelief(1.0f)) {
  using fisher::game::poker::GameBasic;
  using fisher::game::poker::SubgameSetup;

  return std::make_shared<SubgameSetup>(SubgameSetup::Args(
      board, pot, stacks, bet_total, bet_current_round, current_player,
      last_aggressor, raise_count, std::vector<fisher::game::poker::Action>{},
      std::move(belief), AllInOnlyBets(), GameBasic(),
      /*bet_rounding=*/0.1f, /*min_raise_size=*/1.0f));
}

std::vector<float> ApplyReachTransition(
    const fisher::algorithm::IsoTransition& transition,
    const std::vector<float>& parent_reach, int child_num_hands) {
  std::vector<float> child_reach(static_cast<std::size_t>(child_num_hands),
                                 0.0f);
  for (const fisher::algorithm::IsoTransitionEdge& edge : transition.edges) {
    child_reach[static_cast<std::size_t>(edge.child_iso)] +=
        parent_reach[static_cast<std::size_t>(edge.parent_iso)] * edge.weight;
  }
  return child_reach;
}

float SumStrategyForHand(const fisher::algorithm::CfrStorage& storage,
                         int node_id, int hand) {
  float sum = 0.0f;
  for (int action = 0; action < storage.NumActions(node_id); ++action) {
    sum += storage.StrategyAt(node_id, action, hand);
  }
  return sum;
}

float SumAverageForHand(const fisher::algorithm::CfrStorage& storage,
                        const std::vector<float>& average, int node_id,
                        int hand) {
  const fisher::algorithm::NodeCfrLayout& layout = storage.Layout(node_id);
  float sum = 0.0f;
  for (int action = 0; action < layout.num_actions; ++action) {
    sum += average[static_cast<std::size_t>(
        layout.sum_strategy_offset + action * layout.num_hands + hand)];
  }
  return sum;
}

int FindActionIndex(const fisher::game::poker::PokerTree& tree, int node_id,
                    const fisher::game::poker::Action& action) {
  const std::vector<fisher::game::poker::Action>& actions =
      tree.Node(node_id).node_state->ValidActions();
  for (std::size_t index = 0; index < actions.size(); ++index) {
    if (actions[index] == action) {
      return static_cast<int>(index);
    }
  }
  throw std::runtime_error("action not found");
}

int FindBetActionIndex(const fisher::game::poker::PokerTree& tree,
                       int node_id) {
  const std::vector<fisher::game::poker::Action>& actions =
      tree.Node(node_id).node_state->ValidActions();
  for (std::size_t index = 0; index < actions.size(); ++index) {
    if (actions[index].IsBet()) {
      return static_cast<int>(index);
    }
  }
  throw std::runtime_error("bet action not found");
}

float AverageActionProbability(const fisher::algorithm::CfrStorage& storage,
                               const std::vector<float>& average, int node_id,
                               int action_index, int hand_index) {
  const fisher::algorithm::NodeCfrLayout& layout = storage.Layout(node_id);
  return average[static_cast<std::size_t>(
      layout.sum_strategy_offset + action_index * layout.num_hands +
      hand_index)];
}

int IsoHandIndex(const fisher::game::poker::SubgameSetup& setup,
                 const fisher::game::poker::PokerCards& board,
                 const char* hand) {
  fisher::game::poker::IsomorphicMappingTable table(setup.BasicGame(),
                                                    setup.RootBelief());
  const fisher::game::poker::IsomorphicMapping& mapping = table.Get(board);
  const int raw_index =
      setup.BasicGame().HandIndex(fisher::game::poker::PokerHand(hand));
  const int iso_index = mapping.RawToIso(raw_index);
  if (iso_index < 0) {
    throw std::runtime_error("hand is invalid in iso mapping");
  }
  return iso_index;
}

void RunIterations(fisher::algorithm::PokerCfrSolver* solver,
                   int iterations) {
  for (int iteration = 0; iteration < iterations; ++iteration) {
    solver->RunIteration();
  }
}

}  // namespace

int main() {
  using fisher::algorithm::BuildIsoTransition;
  using fisher::algorithm::PokerCfrSolver;
  using fisher::game::poker::Action;
  using fisher::game::poker::GameBasic;
  using fisher::game::poker::IsomorphicMappingTable;
  using fisher::game::poker::PokerBelief;
  using fisher::game::poker::PokerCards;

  GameBasic game;
  PokerBelief full_belief(MatrixBelief(1.0f));
  IsomorphicMappingTable table(game, full_belief);
  const auto& flop = table.Get(PokerCards("AsKdQh"));
  const auto& turn = table.Get(PokerCards("AsKdQh2c"));
  const auto transition = BuildIsoTransition(/*parent_node_id=*/7,
                                             /*child_node_id=*/11, flop, turn);
  Expect(transition.parent_node_id == 7, "transition parent id mismatch");
  Expect(transition.child_node_id == 11, "transition child id mismatch");
  ExpectNear(transition.chance_prob, 1.0f / 45.0f,
             "flop transition chance probability mismatch");
  Expect(!transition.edges.empty(), "transition should contain edges");

  std::vector<float> parent_reach(static_cast<std::size_t>(flop.NumIsoHands()),
                                  0.0f);
  for (int iso = 0; iso < flop.NumIsoHands(); ++iso) {
    parent_reach[static_cast<std::size_t>(iso)] =
        static_cast<float>(flop.RawHandCount(iso));
  }
  const std::vector<float> child_reach =
      ApplyReachTransition(transition, parent_reach, turn.NumIsoHands());
  for (int iso = 0; iso < turn.NumIsoHands(); ++iso) {
    ExpectNear(child_reach[static_cast<std::size_t>(iso)],
               static_cast<float>(turn.RawHandCount(iso)),
               "transition reach should match child raw bucket counts");
  }
  ExpectInvalidArgument(
      [&] {
        BuildIsoTransition(/*parent_node_id=*/0, /*child_node_id=*/1, flop,
                           table.Get(PokerCards("AsKdQh2c3d")));
      },
      "non-adjacent transition should be invalid");
  ExpectInvalidArgument(
      [&] {
        BuildIsoTransition(/*parent_node_id=*/0, /*child_node_id=*/1, flop,
                           table.Get(PokerCards("AsKdJh2c")));
      },
      "non-prefix transition should be invalid");

  auto river_setup =
      MakeSetup(PokerCards("AsKdQh2c3d"), 10.0f, {10.0f, 10.0f},
                {0.0f, 0.0f}, {0.0f, 0.0f}, /*current_player=*/0,
                /*last_aggressor=*/0, /*raise_count=*/0);
  PokerCfrSolver solver{PokerCfrSolver::Args(river_setup)};
  const auto& tree = solver.Tree();
  auto& storage = solver.Storage();
  Expect(!solver.TerminalNodeIds().empty(),
         "solver should cache terminal nodes");
  Expect(solver.ReverseNodeIds().front() == tree.NumNodes() - 1,
         "reverse node order should start from last BFS node");
  Expect(storage.NumHands(0) ==
             static_cast<int>(
                 table.Get(PokerCards("AsKdQh2c3d")).NumIsoHands()),
         "solver storage should use iso hand count");
  ExpectNear(SumStrategyForHand(storage, 0, 0), 1.0f,
             "initial root strategy should be normalized");

  solver.RunIteration();

  for (const auto& node : tree.Nodes()) {
    if (storage.NumActions(node.node_id) == 0) {
      continue;
    }
    ExpectNear(SumStrategyForHand(storage, node.node_id, 0), 1.0f,
               "strategy should remain normalized after iteration");
  }

  const int check_node_id = tree.FindChild(0, Action::Check()).value();
  Expect(storage.SumStrategyAt(0, 0, 0) > 0.0f,
         "player 0 root should accumulate average strategy");
  Expect(storage.SumStrategyAt(check_node_id, 0, 0) > 0.0f,
         "player 1 node should accumulate average strategy during sweep");
  const std::vector<float> average = solver.AverageStrategyData();
  ExpectNear(SumAverageForHand(storage, average, 0, 0), 1.0f,
             "average root strategy should normalize");
  ExpectNear(SumAverageForHand(storage, average, check_node_id, 0), 1.0f,
             "average player 1 node strategy should normalize");

  solver.RunIteration();
  bool has_non_zero_regret = false;
  for (float regret : storage.RegretData()) {
    has_non_zero_regret = has_non_zero_regret || std::fabs(regret) > 1e-6f;
  }
  Expect(has_non_zero_regret,
         "second iteration should store at least one non-zero regret");

  ExpectInvalidArgument(
      [&] {
        PokerCfrSolver bad_solver(PokerCfrSolver::Args(nullptr));
        (void)bad_solver;
      },
      "null setup should be invalid");
  ExpectInvalidArgument(
      [&] {
        PokerCfrSolver bad_solver(PokerCfrSolver::Args(
            river_setup, /*num_threads=*/1, /*max_iterations=*/500,
            /*exploitability_check_interval=*/50,
            /*target_exploitability=*/-1.0f, /*use_dcfr=*/true,
            /*positive_regret_discount_exponent=*/-1.0f));
        (void)bad_solver;
      },
      "negative DCFR positive exponent should be invalid");
  ExpectInvalidArgument(
      [&] {
        PokerCfrSolver bad_solver(PokerCfrSolver::Args(
            river_setup, /*num_threads=*/1, /*max_iterations=*/500,
            /*exploitability_check_interval=*/50,
            /*target_exploitability=*/-1.0f, /*use_dcfr=*/true,
            /*positive_regret_discount_exponent=*/1.5f,
            /*negative_regret_discount_exponent=*/0.5f,
            /*average_strategy_discount_exponent=*/-1.0f));
        (void)bad_solver;
      },
      "negative DCFR average strategy exponent should be invalid");

  {
    auto threaded_setup =
        MakeSetup(PokerCards("AsKdQh2c3d"), 10.0f, {10.0f, 10.0f},
                  {0.0f, 0.0f}, {0.0f, 0.0f}, /*current_player=*/0,
                  /*last_aggressor=*/0, /*raise_count=*/0);
    PokerCfrSolver single_thread_solver{
        PokerCfrSolver::Args(threaded_setup, /*num_threads=*/1)};
    PokerCfrSolver two_thread_solver{
        PokerCfrSolver::Args(threaded_setup, /*num_threads=*/2)};
    Expect(single_thread_solver.NumThreads() == 1,
           "single-thread solver thread count mismatch");
    Expect(two_thread_solver.NumThreads() == 2,
           "two-thread solver thread count mismatch");

    single_thread_solver.RunIteration();
    two_thread_solver.RunIteration();

    ExpectVectorNear(two_thread_solver.Storage().StrategyData(),
                     single_thread_solver.Storage().StrategyData(),
                     "threaded strategy data mismatch");
    ExpectVectorNear(two_thread_solver.Storage().RegretData(),
                     single_thread_solver.Storage().RegretData(),
                     "threaded regret data mismatch");
    ExpectVectorNear(two_thread_solver.Storage().CfvData(),
                     single_thread_solver.Storage().CfvData(),
                     "threaded cfv data mismatch");
    ExpectVectorNear(two_thread_solver.Storage().SumStrategyData(),
                     single_thread_solver.Storage().SumStrategyData(),
                     "threaded sum strategy data mismatch");
  }

  {
    auto hero_pass_setup =
        MakeSetup(PokerCards("AsKdQh2c3d"), 10.0f, {10.0f, 10.0f},
                  {0.0f, 0.0f}, {0.0f, 0.0f}, /*current_player=*/0,
                  /*last_aggressor=*/0, /*raise_count=*/0);
    PokerCfrSolver hero_pass_solver{PokerCfrSolver::Args(
        hero_pass_setup, /*num_threads=*/1, /*max_iterations=*/500,
        /*exploitability_check_interval=*/50, /*target_exploitability=*/-1.0f,
        /*use_dcfr=*/false)};
    hero_pass_solver.RunHeroPass(0);

    bool updated_player0_node = false;
    const auto& hero_pass_tree = hero_pass_solver.Tree();
    const auto& hero_pass_storage = hero_pass_solver.Storage();
    for (const auto& node : hero_pass_tree.Nodes()) {
      if (node.node_state->IsTerminal() ||
          node.node_state->ActorPlayer() ==
              fisher::game::poker::NodeState::kChancePlayer) {
        continue;
      }
      const fisher::algorithm::NodeCfrLayout& layout =
          hero_pass_storage.Layout(node.node_id);
      const float* regrets = hero_pass_storage.RegretBlock(node.node_id);
      bool has_node_regret = false;
      for (int index = 0; index < layout.num_actions * layout.num_hands;
           ++index) {
        has_node_regret =
            has_node_regret || std::fabs(regrets[index]) > 1e-6f;
      }
      if (node.node_state->ActorPlayer() == 0) {
        updated_player0_node = updated_player0_node || has_node_regret;
      } else {
        Expect(!has_node_regret,
               "hero 0 pass should not update player 1 regrets");
      }
    }
    Expect(updated_player0_node,
           "hero 0 pass should update at least one player 0 node regret");
  }

  {
    auto vanilla_setup =
        MakeSetup(PokerCards("AsKdQh2c3d"), 10.0f, {10.0f, 10.0f},
                  {0.0f, 0.0f}, {0.0f, 0.0f}, /*current_player=*/0,
                  /*last_aggressor=*/0, /*raise_count=*/0);
    auto dcfr_setup =
        MakeSetup(PokerCards("AsKdQh2c3d"), 10.0f, {10.0f, 10.0f},
                  {0.0f, 0.0f}, {0.0f, 0.0f}, /*current_player=*/0,
                  /*last_aggressor=*/0, /*raise_count=*/0);
    PokerCfrSolver vanilla_solver{PokerCfrSolver::Args(
        vanilla_setup, /*num_threads=*/1, /*max_iterations=*/500,
        /*exploitability_check_interval=*/50,
        /*target_exploitability=*/-1.0f, /*use_dcfr=*/false)};
    PokerCfrSolver dcfr_solver{PokerCfrSolver::Args(
        dcfr_setup, /*num_threads=*/1, /*max_iterations=*/500,
        /*exploitability_check_interval=*/50,
        /*target_exploitability=*/-1.0f, /*use_dcfr=*/true)};

    vanilla_solver.RunHeroPass(0);
    dcfr_solver.RunHeroPass(0);

    bool compared_non_zero_regret = false;
    const std::vector<float>& vanilla_regret =
        vanilla_solver.Storage().RegretData();
    const std::vector<float>& dcfr_regret = dcfr_solver.Storage().RegretData();
    Expect(vanilla_regret.size() == dcfr_regret.size(),
           "DCFR regret storage shape mismatch");
    for (std::size_t index = 0; index < vanilla_regret.size(); ++index) {
      if (std::fabs(vanilla_regret[index]) <= 1e-6f) {
        ExpectNear(dcfr_regret[index], 0.0f,
                   "zero vanilla regret should stay zero under DCFR");
        continue;
      }
      compared_non_zero_regret = true;
      ExpectNear(dcfr_regret[index], 0.0f,
                 "first DCFR pass should use t=0 and clear stored regret");
    }
    Expect(compared_non_zero_regret,
           "DCFR regression should compare at least one non-zero regret");
    ExpectVectorNear(dcfr_solver.Storage().StrategyData(),
                     vanilla_solver.Storage().StrategyData(),
                     "first DCFR pass should use undiscounted regret matching");
  }

  {
    auto cfr_plus_like_setup =
        MakeSetup(PokerCards("AsKdQh2c3d"), 10.0f, {10.0f, 10.0f},
                  {0.0f, 0.0f}, {0.0f, 0.0f}, /*current_player=*/0,
                  /*last_aggressor=*/0, /*raise_count=*/0);
    PokerCfrSolver cfr_plus_like_solver{PokerCfrSolver::Args(
        cfr_plus_like_setup, /*num_threads=*/1, /*max_iterations=*/500,
        /*exploitability_check_interval=*/50,
        /*target_exploitability=*/-1.0f, /*use_dcfr=*/true,
        /*positive_regret_discount_exponent=*/1.5f,
        /*negative_regret_discount_exponent=*/-5.0f)};

    cfr_plus_like_solver.RunHeroPass(0);
    for (float regret : cfr_plus_like_solver.Storage().RegretData()) {
      Expect(regret >= -1e-6f,
             "beta <= -5 should clear negative cumulative regrets");
    }
  }

  {
    auto no_average_discount_setup =
        MakeSetup(PokerCards("AsKdQh2c3d"), 10.0f, {10.0f, 10.0f},
                  {0.0f, 0.0f}, {0.0f, 0.0f}, /*current_player=*/0,
                  /*last_aggressor=*/0, /*raise_count=*/0);
    auto average_discount_setup =
        MakeSetup(PokerCards("AsKdQh2c3d"), 10.0f, {10.0f, 10.0f},
                  {0.0f, 0.0f}, {0.0f, 0.0f}, /*current_player=*/0,
                  /*last_aggressor=*/0, /*raise_count=*/0);
    PokerCfrSolver no_average_discount_solver{PokerCfrSolver::Args(
        no_average_discount_setup, /*num_threads=*/1,
        /*max_iterations=*/500, /*exploitability_check_interval=*/50,
        /*target_exploitability=*/-1.0f, /*use_dcfr=*/true,
        /*positive_regret_discount_exponent=*/1.5f,
        /*negative_regret_discount_exponent=*/0.5f,
        /*average_strategy_discount_exponent=*/0.0f)};
    PokerCfrSolver average_discount_solver{PokerCfrSolver::Args(
        average_discount_setup, /*num_threads=*/1, /*max_iterations=*/500,
        /*exploitability_check_interval=*/50,
        /*target_exploitability=*/-1.0f, /*use_dcfr=*/true,
        /*positive_regret_discount_exponent=*/1.5f,
        /*negative_regret_discount_exponent=*/0.5f,
        /*average_strategy_discount_exponent=*/2.0f)};

    no_average_discount_solver.RunIteration();
    average_discount_solver.RunIteration();
    no_average_discount_solver.RunIteration();
    average_discount_solver.RunIteration();

    const float no_discount_sum =
        no_average_discount_solver.Storage().SumStrategyAt(0, 0, 0) +
        no_average_discount_solver.Storage().SumStrategyAt(0, 1, 0);
    const float discounted_sum =
        average_discount_solver.Storage().SumStrategyAt(0, 0, 0) +
        average_discount_solver.Storage().SumStrategyAt(0, 1, 0);
    Expect(discounted_sum < no_discount_sum,
           "gamma should discount cumulative average strategy before adding");
    ExpectVectorNear(average_discount_solver.Storage().StrategyData(),
                     no_average_discount_solver.Storage().StrategyData(),
                     "gamma should not change current regret-matched strategy");
  }

  {
    auto air_vs_nuts_setup =
        MakeSetup(PokerCards("AhKhQh2c3d"), 10.0f, {10.0f, 10.0f},
                  {0.0f, 0.0f}, {0.0f, 0.0f}, /*current_player=*/0,
                  /*last_aggressor=*/0, /*raise_count=*/0,
                  BucketClosedHeadsUpBelief(PokerCards("AhKhQh2c3d"),
                                            "7c8d", "JhTh"));
    PokerCfrSolver air_vs_nuts_solver{
        PokerCfrSolver::Args(air_vs_nuts_setup)};
    RunIterations(&air_vs_nuts_solver, 200);
    ExpectInvalidArgument(
        [&] { (void)air_vs_nuts_solver.NodeEv(0, 0); },
        "node EV should require average strategy finalize");

    const auto& solver_tree = air_vs_nuts_solver.Tree();
    const auto& solver_storage = air_vs_nuts_solver.Storage();
    const std::vector<float> avg =
        air_vs_nuts_solver.AverageStrategyData();
    const int air_hand =
        IsoHandIndex(*air_vs_nuts_setup, PokerCards("AhKhQh2c3d"), "7c8d");
    const int nuts_hand =
        IsoHandIndex(*air_vs_nuts_setup, PokerCards("AhKhQh2c3d"), "JhTh");
    const int root_bet_index = FindBetActionIndex(solver_tree, 0);
    const int bet_node_id = solver_tree.ChildNodeIdAt(0, root_bet_index);
    const int call_index =
        FindActionIndex(solver_tree, bet_node_id, Action::Call());

    Expect(AverageActionProbability(solver_storage, avg, 0, root_bet_index,
                                    air_hand) < 0.01f,
           "river pure air should rarely shove into pure nuts");
    Expect(AverageActionProbability(solver_storage, avg, bet_node_id,
                                    call_index, nuts_hand) > 0.99f,
           "river pure nuts should call air shove");

    const std::vector<float> current_strategy_before_finalize =
        air_vs_nuts_solver.Storage().StrategyData();
    air_vs_nuts_solver.FinalizeAverageStrategy();
    ExpectVectorNear(air_vs_nuts_solver.Storage().StrategyData(),
                     current_strategy_before_finalize,
                     "finalize should not overwrite current strategy");

    const PokerCfrSolver::NodeEvDetail player0_ev =
        air_vs_nuts_solver.NodeEv(0, 0);
    const PokerCfrSolver::NodeEvDetail player1_ev =
        air_vs_nuts_solver.NodeEv(0, 1);
    Expect(player0_ev.node_id == 0 && player0_ev.player == 0,
           "player 0 node EV identity mismatch");
    Expect(player1_ev.node_id == 0 && player1_ev.player == 1,
           "player 1 node EV identity mismatch");
    Expect(player0_ev.cfv.size() == player0_ev.valid_mass.size(),
           "node EV cfv and valid mass size mismatch");
    Expect(player0_ev.cfv.size() == player0_ev.hand_ev.size(),
           "node EV cfv and hand EV size mismatch");
    Expect(player0_ev.valid_mass[static_cast<std::size_t>(air_hand)] > 0.0f,
           "player 0 active hand should have valid mass");
    Expect(player1_ev.valid_mass[static_cast<std::size_t>(nuts_hand)] > 0.0f,
           "player 1 active hand should have valid mass");
    ExpectNear(player0_ev.hand_ev[static_cast<std::size_t>(air_hand)],
               player0_ev.cfv[static_cast<std::size_t>(air_hand)] /
                   player0_ev.valid_mass[static_cast<std::size_t>(air_hand)],
               "player 0 hand EV should equal cfv divided by valid mass");
    ExpectNear(player1_ev.hand_ev[static_cast<std::size_t>(nuts_hand)],
               player1_ev.cfv[static_cast<std::size_t>(nuts_hand)] /
                   player1_ev.valid_mass[static_cast<std::size_t>(nuts_hand)],
               "player 1 hand EV should equal cfv divided by valid mass");
    ExpectNear(player0_ev.range_ev,
               player0_ev.hand_ev[static_cast<std::size_t>(air_hand)],
               "single-hand player 0 range EV should match hand EV");
    ExpectNear(player1_ev.range_ev,
               player1_ev.hand_ev[static_cast<std::size_t>(nuts_hand)],
               "single-hand player 1 range EV should match hand EV");
    ExpectFinite(player0_ev.range_ev, "player 0 range EV should be finite");
    ExpectFinite(player1_ev.range_ev, "player 1 range EV should be finite");
    Expect(player0_ev.range_mass > 0.0f, "player 0 range mass should be positive");
    Expect(player1_ev.range_mass > 0.0f, "player 1 range mass should be positive");

    const PokerCfrSolver::NodeCfvDetail player0_cfv =
        air_vs_nuts_solver.NodeCfv(0, 0);
    const PokerCfrSolver::NodeCfvDetail player1_cfv =
        air_vs_nuts_solver.NodeCfv(0, 1);
    Expect(player0_cfv.node_id == 0 && player0_cfv.player == 0,
           "player 0 node CFV identity mismatch");
    Expect(player1_cfv.node_id == 0 && player1_cfv.player == 1,
           "player 1 node CFV identity mismatch");
    ExpectVectorNear(player0_cfv.cfv, player0_ev.cfv,
                     "node CFV should expose storage CFV without EV scaling");
    ExpectVectorNear(player1_cfv.cfv, player1_ev.cfv,
                     "node CFV should expose player 1 storage CFV");

    const PokerCfrSolver::NodeEquityDetail player0_equity =
        air_vs_nuts_solver.NodeEquity(0, 0);
    const PokerCfrSolver::NodeEquityDetail player1_equity =
        air_vs_nuts_solver.NodeEquity(0, 1);
    Expect(player0_equity.node_id == 0 && player0_equity.player == 0,
           "player 0 node equity identity mismatch");
    Expect(player1_equity.node_id == 0 && player1_equity.player == 1,
           "player 1 node equity identity mismatch");
    Expect(player0_equity.equity.size() ==
               static_cast<std::size_t>(solver_storage.NumHands(0)),
           "player 0 node equity size mismatch");
    Expect(player1_equity.equity.size() ==
               static_cast<std::size_t>(solver_storage.NumHands(0)),
           "player 1 node equity size mismatch");
    for (float equity : player0_equity.equity) {
      ExpectFinite(equity, "player 0 node equity should be finite");
      Expect(equity >= 0.0f && equity <= 1.0f,
             "player 0 node equity should be a probability");
    }
    for (float equity : player1_equity.equity) {
      ExpectFinite(equity, "player 1 node equity should be finite");
      Expect(equity >= 0.0f && equity <= 1.0f,
             "player 1 node equity should be a probability");
    }
    ExpectNear(player0_equity.range_equity,
               player0_equity.equity[static_cast<std::size_t>(air_hand)],
               "single-hand player 0 range equity should match hand equity");
    ExpectNear(player1_equity.range_equity,
               player1_equity.equity[static_cast<std::size_t>(nuts_hand)],
               "single-hand player 1 range equity should match hand equity");
  }

  {
    auto turn_setup =
        MakeSetup(PokerCards("AhKhQh2c"), 10.0f, {10.0f, 10.0f},
                  {0.0f, 0.0f}, {0.0f, 0.0f}, /*current_player=*/0,
                  /*last_aggressor=*/0, /*raise_count=*/0,
                  BucketClosedHeadsUpBelief(PokerCards("AhKhQh2c"), "7c8d",
                                            "JhTh"));
    PokerCfrSolver turn_solver{PokerCfrSolver::Args(turn_setup)};
    RunIterations(&turn_solver, 200);

    const auto& solver_tree = turn_solver.Tree();
    const auto& solver_storage = turn_solver.Storage();
    const std::vector<float> avg = turn_solver.AverageStrategyData();
    const int air_hand =
        IsoHandIndex(*turn_setup, PokerCards("AhKhQh2c"), "7c8d");
    const int nuts_hand =
        IsoHandIndex(*turn_setup, PokerCards("AhKhQh2c"), "JhTh");
    const int root_bet_index = FindBetActionIndex(solver_tree, 0);
    const int bet_node_id = solver_tree.ChildNodeIdAt(0, root_bet_index);
    const int call_index =
        FindActionIndex(solver_tree, bet_node_id, Action::Call());

    Expect(AverageActionProbability(solver_storage, avg, 0, root_bet_index,
                                    air_hand) < 0.01f,
           "turn pure air should rarely shove into made royal flush");
    Expect(AverageActionProbability(solver_storage, avg, bet_node_id,
                                    call_index, nuts_hand) > 0.99f,
           "turn made royal flush should call air shove");
  }

  return 0;
}
