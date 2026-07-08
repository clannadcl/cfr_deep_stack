#include "algorithm/cfr.h"

#include <algorithm>
#include <stdexcept>

namespace fisher::algorithm {
namespace {

std::unordered_map<int, double> RegretMatching(const InfoStateNode& node) {
  std::unordered_map<int, double> strategy;
  double normalizer = 0.0;
  for (int action : node.legal_actions) {
    const auto it = node.cumulative_regret.find(action);
    const double regret = it == node.cumulative_regret.end() ? 0.0 : it->second;
    strategy[action] = std::max(regret, 0.0);
    normalizer += strategy[action];
  }

  if (normalizer <= 0.0) {
    const double probability = 1.0 / static_cast<double>(node.legal_actions.size());
    for (int action : node.legal_actions) strategy[action] = probability;
  } else {
    for (int action : node.legal_actions) strategy[action] /= normalizer;
  }
  return strategy;
}

std::map<int, double> ActionProbabilities(
    const Strategy& strategy, const game::KuhnState& state, int player) {
  const std::string info_state = state.InformationStateString(player);
  const auto it = strategy.find(info_state);
  if (it != strategy.end()) return it->second;

  const auto legal_actions = state.LegalActions();
  const double probability = 1.0 / static_cast<double>(legal_actions.size());
  std::map<int, double> uniform;
  for (int action : legal_actions) uniform[action] = probability;
  return uniform;
}

std::array<double, 2> PolicyValueState(const game::KuhnState& state,
                                       const Strategy& strategy) {
  if (state.IsTerminal()) return state.Returns();

  const int current_player = state.CurrentPlayer();
  std::array<double, 2> values{0.0, 0.0};

  if (current_player == game::kChancePlayer) {
    for (const auto& [action, probability] : state.ChanceOutcomes()) {
      const auto child_values = PolicyValueState(state.Child(action), strategy);
      values[0] += probability * child_values[0];
      values[1] += probability * child_values[1];
    }
    return values;
  }

  for (const auto& [action, probability] :
       ActionProbabilities(strategy, state, current_player)) {
    const auto child_values = PolicyValueState(state.Child(action), strategy);
    values[0] += probability * child_values[0];
    values[1] += probability * child_values[1];
  }
  return values;
}

void CollectInfoStates(const game::KuhnState& state, int player,
                       std::vector<std::string>* out) {
  if (state.IsTerminal()) return;

  const int current_player = state.CurrentPlayer();
  if (current_player == game::kChancePlayer) {
    for (const auto& [action, unused_probability] : state.ChanceOutcomes()) {
      CollectInfoStates(state.Child(action), player, out);
    }
    return;
  }

  if (current_player == player) {
    const std::string info_state = state.InformationStateString(player);
    if (std::find(out->begin(), out->end(), info_state) == out->end()) {
      out->push_back(info_state);
    }
  }

  for (int action : state.LegalActions()) {
    CollectInfoStates(state.Child(action), player, out);
  }
}

double BestResponseValueByEnumeration(const game::KuhnPokerGame& game,
                                      const Strategy& strategy, int player) {
  std::vector<std::string> info_states;
  CollectInfoStates(game.NewInitialState(), player, &info_states);

  double best_value = -1e100;
  const int total_responses = 1 << static_cast<int>(info_states.size());
  for (int mask = 0; mask < total_responses; ++mask) {
    Strategy candidate = strategy;
    for (int i = 0; i < static_cast<int>(info_states.size()); ++i) {
      const int action = (mask >> i) & 1;
      candidate[info_states[i]] = {{action, 1.0}};
    }
    best_value = std::max(
        best_value, PolicyValueState(game.NewInitialState(), candidate)[player]);
  }
  return best_value;
}

}  // namespace

KuhnCfrSolver::KuhnCfrSolver(bool cfr_plus) : cfr_plus_(cfr_plus) {}

void KuhnCfrSolver::Run(const game::KuhnPokerGame& game, int iterations) {
  if (iterations < 0) throw std::invalid_argument("iterations must be non-negative");
  for (int i = 0; i < iterations; ++i) {
    for (int player = 0; player < game.NumPlayers(); ++player) {
      std::array<double, 2> reach{1.0, 1.0};
      Cfr(game.NewInitialState(), player, reach);
    }
    ++iterations_;
  }
}

Strategy KuhnCfrSolver::AveragePolicy() const {
  Strategy policy;
  for (const auto& [info_state, node] : info_state_nodes_) {
    double normalizer = 0.0;
    for (const auto& [unused_action, probability_sum] : node.cumulative_policy) {
      normalizer += probability_sum;
    }

    std::map<int, double> action_probs;
    if (normalizer <= 0.0) {
      const double probability = 1.0 / static_cast<double>(node.legal_actions.size());
      for (int action : node.legal_actions) action_probs[action] = probability;
    } else {
      for (int action : node.legal_actions) {
        const auto it = node.cumulative_policy.find(action);
        action_probs[action] =
            (it == node.cumulative_policy.end() ? 0.0 : it->second) / normalizer;
      }
    }
    policy[info_state] = std::move(action_probs);
  }
  return policy;
}

int KuhnCfrSolver::Iterations() const { return iterations_; }

double KuhnCfrSolver::Cfr(const game::KuhnState& state, int updating_player,
                          const std::array<double, 2>& reach) {
  if (state.IsTerminal()) return state.Returns()[updating_player];

  const int current_player = state.CurrentPlayer();
  if (current_player == game::kChancePlayer) {
    double value = 0.0;
    for (const auto& [action, probability] : state.ChanceOutcomes()) {
      value += probability * Cfr(state.Child(action), updating_player, reach);
    }
    return value;
  }

  InfoStateNode& node = NodeForState(state, current_player);
  const auto strategy = RegretMatching(node);

  std::unordered_map<int, double> action_values;
  double node_value = 0.0;
  for (int action : node.legal_actions) {
    auto next_reach = reach;
    next_reach[current_player] *= strategy.at(action);
    const double action_value = Cfr(state.Child(action), updating_player, next_reach);
    action_values[action] = action_value;
    node_value += strategy.at(action) * action_value;
  }

  if (current_player == updating_player) {
    const double opponent_reach = reach[1 - updating_player];
    const double player_reach = reach[updating_player];
    for (int action : node.legal_actions) {
      const double regret = opponent_reach * (action_values[action] - node_value);
      double updated_regret = node.cumulative_regret[action] + regret;
      if (cfr_plus_) updated_regret = std::max(updated_regret, 0.0);
      node.cumulative_regret[action] = updated_regret;
      node.cumulative_policy[action] += player_reach * strategy.at(action);
    }
  }

  return node_value;
}

InfoStateNode& KuhnCfrSolver::NodeForState(const game::KuhnState& state,
                                           int player) {
  const std::string info_state = state.InformationStateString(player);
  auto [it, inserted] = info_state_nodes_.try_emplace(info_state);
  if (inserted) it->second.legal_actions = state.LegalActions();
  return it->second;
}

std::array<double, 2> PolicyValue(const game::KuhnPokerGame& game,
                                  const Strategy& strategy) {
  return PolicyValueState(game.NewInitialState(), strategy);
}

double NashExploitability(const game::KuhnPokerGame& game,
                          const Strategy& strategy) {
  const auto on_policy = PolicyValue(game, strategy);
  double nash_conv = 0.0;
  for (int player = 0; player < game.NumPlayers(); ++player) {
    nash_conv += BestResponseValueByEnumeration(game, strategy, player) -
                 on_policy[player];
  }
  return nash_conv / static_cast<double>(game.NumPlayers());
}

}  // namespace fisher::algorithm
