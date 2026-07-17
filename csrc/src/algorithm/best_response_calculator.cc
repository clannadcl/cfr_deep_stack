#include "algorithm/best_response_calculator.h"

#include <algorithm>
#include <cstddef>
#include <stdexcept>

#include "game/poker/game_basic.h"
#include "game/poker/node_state.h"

namespace fisher::algorithm {
namespace {

constexpr float kEvMassEpsilon = 1e-10f;

void ValidatePlayer(int player) {
  if (player < 0 || player >= game::poker::GameBasic::kNumPlayers) {
    throw std::invalid_argument("Best response player must be 0 or 1");
  }
}

std::vector<std::vector<float>> AllocateNodeCfvWorkspace(
    const PokerCfrSolver& solver) {
  std::vector<std::vector<float>> cfv(
      static_cast<std::size_t>(solver.Tree().NumNodes()));
  for (const game::poker::PokerTreeNode& node : solver.Tree().Nodes()) {
    cfv[static_cast<std::size_t>(node.node_id)].assign(
        static_cast<std::size_t>(solver.Storage().NumHands(node.node_id)),
        0.0f);
  }
  return cfv;
}

void ResetPolicy(const PokerCfrSolver& solver, BestResponsePolicy* policy) {
  policy->action_by_node_hand.assign(
      static_cast<std::size_t>(solver.Tree().NumNodes()), {});
  for (const game::poker::PokerTreeNode& node : solver.Tree().Nodes()) {
    policy->action_by_node_hand[static_cast<std::size_t>(node.node_id)]
        .assign(static_cast<std::size_t>(solver.Storage().NumHands(node.node_id)),
                -1);
  }
}

}  // namespace

BestResponseCalculator::BestResponseCalculator(PokerCfrSolver* solver)
    : solver_(solver) {
  if (solver_ == nullptr) {
    throw std::invalid_argument("BestResponseCalculator solver cannot be null");
  }
}

ExploitabilityResult BestResponseCalculator::Compute(float average_epsilon) {
  if (average_epsilon <= 0.0f) {
    throw std::invalid_argument("Average strategy epsilon must be positive");
  }

  solver_->FinalizeAverageStrategy(average_epsilon);

  ExploitabilityResult result;
  for (int player = 0; player < game::poker::GameBasic::kNumPlayers;
       ++player) {
    result.current_ev[static_cast<std::size_t>(player)] =
        solver_->NodeEv(/*node_id=*/0, player).range_ev;
  }

  ResetPolicy(*solver_, &result.policy);
  for (int player = 0; player < game::poker::GameBasic::kNumPlayers;
       ++player) {
    const std::vector<float> root_cfv =
        ComputeBestResponseCfv(player, &result.policy);
    result.best_response_ev[static_cast<std::size_t>(player)] =
        ComputeRootEv(player, root_cfv);
  }

  result.exploitability =
      0.5f *
      ((result.best_response_ev[0] - result.current_ev[0]) +
       (result.best_response_ev[1] - result.current_ev[1]));
  return result;
}

std::vector<float> BestResponseCalculator::ComputeBestResponseCfv(
    int hero_player, BestResponsePolicy* policy) const {
  ValidatePlayer(hero_player);
  if (policy == nullptr) {
    throw std::invalid_argument("Best response policy cannot be null");
  }

  std::vector<std::vector<float>> cfv = AllocateNodeCfvWorkspace(*solver_);
  const game::poker::PokerTree& tree = solver_->Tree();
  const CfrStorage& storage = solver_->Storage();

  for (const int node_id : solver_->ReverseNodeIds()) {
    const game::poker::PokerTreeNode& node = tree.Node(node_id);
    std::vector<float>& node_cfv = cfv[static_cast<std::size_t>(node_id)];

    if (node.node_state->IsTerminal()) {
      for (int hand = 0; hand < storage.NumHands(node_id); ++hand) {
        node_cfv[static_cast<std::size_t>(hand)] =
            storage.CfvAt(node_id, hero_player, hand);
      }
      continue;
    }

    if (node.node_state->ActorPlayer() ==
        game::poker::NodeState::kChancePlayer) {
      std::fill(node_cfv.begin(), node_cfv.end(), 0.0f);
      for (int child_index = 0; child_index < node.num_children;
           ++child_index) {
        const int child_id = tree.ChildNodeIdAt(node_id, child_index);
        const IsoTransition& transition = solver_->ChanceTransition(child_id);
        const std::vector<float>& child_cfv =
            cfv[static_cast<std::size_t>(child_id)];
        for (const IsoTransitionEdge& edge : transition.edges) {
          node_cfv[static_cast<std::size_t>(edge.parent_iso)] +=
              child_cfv[static_cast<std::size_t>(edge.child_iso)] *
              edge.weight * transition.chance_prob;
        }
      }
      continue;
    }

    const int actor = node.node_state->ActorPlayer();
    const NodeCfrLayout& layout = storage.Layout(node_id);
    if (actor == hero_player) {
      for (int hand = 0; hand < layout.num_hands; ++hand) {
        int best_action = 0;
        float best_value =
            cfv[static_cast<std::size_t>(tree.ChildNodeIdAt(node_id, 0))]
               [static_cast<std::size_t>(hand)];
        for (int action = 1; action < layout.num_actions; ++action) {
          const int child_id = tree.ChildNodeIdAt(node_id, action);
          const float child_value =
              cfv[static_cast<std::size_t>(child_id)]
                 [static_cast<std::size_t>(hand)];
          if (child_value > best_value) {
            best_value = child_value;
            best_action = action;
          }
        }
        node_cfv[static_cast<std::size_t>(hand)] = best_value;
        policy->action_by_node_hand[static_cast<std::size_t>(node_id)]
                                   [static_cast<std::size_t>(hand)] =
            best_action;
      }
    } else {
      std::fill(node_cfv.begin(), node_cfv.end(), 0.0f);
      for (int action = 0; action < layout.num_actions; ++action) {
        const int child_id = tree.ChildNodeIdAt(node_id, action);
        const std::vector<float>& child_cfv =
            cfv[static_cast<std::size_t>(child_id)];
        for (int hand = 0; hand < layout.num_hands; ++hand) {
          node_cfv[static_cast<std::size_t>(hand)] +=
              child_cfv[static_cast<std::size_t>(hand)];
        }
      }
    }
  }

  return cfv[static_cast<std::size_t>(tree.Root().node_id)];
}

float BestResponseCalculator::ComputeRootEv(
    int player, const std::vector<float>& root_cfv) const {
  ValidatePlayer(player);
  const PokerCfrSolver::NodeEvDetail root_ev =
      solver_->NodeEv(/*node_id=*/0, player);
  if (root_cfv.size() != root_ev.valid_mass.size()) {
    throw std::invalid_argument("Best response root CFV size mismatch");
  }

  double numerator = 0.0;
  double denominator = 0.0;
  for (int hand = 0; hand < solver_->Storage().NumHands(/*node_id=*/0);
       ++hand) {
    const std::size_t hand_index = static_cast<std::size_t>(hand);
    const double own_reach = solver_->Storage().ReachAt(/*node_id=*/0, player,
                                                        hand);
    numerator += own_reach * static_cast<double>(root_cfv[hand_index]);
    denominator +=
        own_reach * static_cast<double>(root_ev.valid_mass[hand_index]);
  }

  if (denominator <= static_cast<double>(kEvMassEpsilon)) {
    return 0.0f;
  }
  return static_cast<float>(numerator / denominator);
}

}  // namespace fisher::algorithm
