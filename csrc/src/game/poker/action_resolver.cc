#include "game/poker/action_resolver.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "game/poker/abstracted_action.h"
#include "game/poker/node_state.h"
#include "game/poker/subgame_setup.h"

namespace fisher::game::poker {
namespace {

float RoundTo(float value, float unit) {
  if (unit <= 0.0f) {
    return value;
  }
  return std::round(value / unit) * unit;
}

void AddUniqueAction(const Action& action, std::vector<Action>* actions) {
  if (std::find(actions->begin(), actions->end(), action) == actions->end()) {
    actions->push_back(action);
  }
}

bool NeedsCall(const NodeState& state) {
  const int actor = state.ActorPlayer();
  const int opponent = 1 - actor;
  return state.BetCurrentRound()[static_cast<std::size_t>(opponent)] >
         state.BetCurrentRound()[static_cast<std::size_t>(actor)];
}

bool ShouldUseDonkBets(const NodeState& state) {
  return state.NumRaisesCurrentRound() == 0 && state.ActorPlayer() == 0 &&
         state.LastAggressor() != state.ActorPlayer();
}

const std::vector<AbstractedAction>& AbstractedBetsForState(
    const NodeState& state) {
  if (ShouldUseDonkBets(state)) {
    return state.Setup()->AbstractedBets().GetDonkBets(state.Street());
  }
  return state.Setup()->AbstractedBets().GetBets(
      state.Street(), state.NumRaisesCurrentRound());
}

bool ShouldAddAllInBySpr(const NodeState& state) {
  if (state.Pot() <= 0.0f) {
    return false;
  }
  const float effective_stack = std::min(
      state.Stacks()[0] + state.BetCurrentRound()[0],
      state.Stacks()[1] + state.BetCurrentRound()[1]);
  const float spr = effective_stack / state.Pot();
  return spr < state.Setup()->AbstractedBets().AddAllInThreshold() / 100.0f;
}

int RemainingStreets(PokerRound round) {
  switch (round) {
    case PokerRound::kPreflop:
      return 4;
    case PokerRound::kFlop:
      return 3;
    case PokerRound::kTurn:
      return 2;
    case PokerRound::kRiver:
      return 1;
  }
  return 1;
}

float PotAfterCall(const NodeState& state) {
  const int actor = state.ActorPlayer();
  const int opponent = 1 - actor;
  const float own_bet =
      state.BetCurrentRound()[static_cast<std::size_t>(actor)];
  const float opponent_bet =
      state.BetCurrentRound()[static_cast<std::size_t>(opponent)];
  const float call_amount = std::max(0.0f, opponent_bet - own_bet);
  return state.Pot() + state.BetCurrentRound()[0] +
         state.BetCurrentRound()[1] + call_amount;
}

float ResolveGeometricTarget(const AbstractedAction& abstracted_action,
                             const NodeState& state) {
  const int actor = state.ActorPlayer();
  const int opponent = 1 - actor;
  const float own_bet =
      state.BetCurrentRound()[static_cast<std::size_t>(actor)];
  const float opponent_bet =
      state.BetCurrentRound()[static_cast<std::size_t>(opponent)];
  const float allin_target =
      own_bet + state.Stacks()[static_cast<std::size_t>(actor)];
  const float baseline_target = NeedsCall(state) ? opponent_bet : own_bet;
  const float pot_after_call = PotAfterCall(state);
  if (pot_after_call <= 0.0f || allin_target <= baseline_target) {
    return -1.0f;
  }

  int num_streets = static_cast<int>(std::round(abstracted_action.Amount()));
  if (num_streets <= 0) {
    num_streets = RemainingStreets(state.Street());
  }
  if (NeedsCall(state)) {
    num_streets = std::max(1, num_streets - state.NumRaisesCurrentRound() + 1);
  }

  const float remaining_after_call = allin_target - baseline_target;
  const float spr_after_call = remaining_after_call / pot_after_call;
  const float geometric_ratio =
      (std::pow(2.0f * spr_after_call + 1.0f,
                1.0f / static_cast<float>(num_streets)) -
       1.0f) /
      2.0f;
  float bet_increment = pot_after_call * geometric_ratio;
  if (std::isfinite(abstracted_action.MaxPercent())) {
    bet_increment =
        std::min(bet_increment,
                 pot_after_call * abstracted_action.MaxPercent() / 100.0f);
  }
  return baseline_target + bet_increment;
}

float ResolveTargetBet(const AbstractedAction& abstracted_action,
                       const NodeState& state) {
  const int actor = state.ActorPlayer();
  const int opponent = 1 - actor;
  const float own_bet =
      state.BetCurrentRound()[static_cast<std::size_t>(actor)];
  const float opponent_bet =
      state.BetCurrentRound()[static_cast<std::size_t>(opponent)];

  switch (abstracted_action.Type()) {
    case AbstractedActionType::kBetBigBlind:
      return abstracted_action.Amount();
    case AbstractedActionType::kBetPercent: {
      const float ratio = abstracted_action.Amount() / 100.0f;
      if (!NeedsCall(state)) {
        return state.Pot() * ratio;
      }
      const float call_amount = opponent_bet - own_bet;
      const float pot_after_call =
          state.Pot() + state.BetCurrentRound()[0] +
          state.BetCurrentRound()[1] + call_amount;
      return opponent_bet + pot_after_call * ratio;
    }
    case AbstractedActionType::kBetPreviousBetMultiplier:
      if (!NeedsCall(state)) {
        return -1.0f;
      }
      return opponent_bet * abstracted_action.Amount();
    case AbstractedActionType::kBetGeometric:
      return ResolveGeometricTarget(abstracted_action, state);
    case AbstractedActionType::kAllIn:
      return own_bet + state.Stacks()[static_cast<std::size_t>(actor)];
    case AbstractedActionType::kFold:
    case AbstractedActionType::kCheck:
    case AbstractedActionType::kCall:
      break;
  }
  return -1.0f;
}

void MergeCloseBetActions(const NodeState& state, std::vector<Action>* actions) {
  const float threshold = state.Setup()->AbstractedBets().MergingThreshold();
  if (threshold <= 0.0f) {
    return;
  }

  std::vector<Action> non_bet_actions;
  std::vector<Action> bet_actions;
  for (const Action& action : *actions) {
    if (action.IsBet()) {
      bet_actions.push_back(action);
    } else {
      non_bet_actions.push_back(action);
    }
  }
  if (bet_actions.size() <= 1) {
    return;
  }

  std::sort(bet_actions.begin(), bet_actions.end());
  const int actor = state.ActorPlayer();
  const int opponent = 1 - actor;
  const float own_bet =
      state.BetCurrentRound()[static_cast<std::size_t>(actor)];
  const float opponent_bet =
      state.BetCurrentRound()[static_cast<std::size_t>(opponent)];
  const float baseline_target = NeedsCall(state) ? opponent_bet : own_bet;
  const float pot_after_call = PotAfterCall(state);

  std::vector<Action> kept_bets;
  for (auto it = bet_actions.rbegin(); it != bet_actions.rend(); ++it) {
    if (kept_bets.empty()) {
      kept_bets.push_back(*it);
      continue;
    }
    const float current_increment =
        std::max(0.0f, it->Amount() - baseline_target);
    const float kept_increment =
        std::max(0.0f, kept_bets.back().Amount() - baseline_target);
    const float current_pot_after_bet =
        pot_after_call + 2.0f * current_increment;
    const float kept_pot_after_bet =
        pot_after_call + 2.0f * kept_increment;
    if (current_pot_after_bet <= 0.0f ||
        kept_pot_after_bet / current_pot_after_bet >
            1.0f + threshold) {
      kept_bets.push_back(*it);
    }
  }

  actions->clear();
  actions->insert(actions->end(), non_bet_actions.begin(),
                  non_bet_actions.end());
  actions->insert(actions->end(), kept_bets.begin(), kept_bets.end());
}

void AddBetActions(const NodeState& state, std::vector<Action>* actions) {
  const int actor = state.ActorPlayer();
  const int opponent = 1 - actor;
  const float own_bet =
      state.BetCurrentRound()[static_cast<std::size_t>(actor)];
  const float opponent_bet =
      state.BetCurrentRound()[static_cast<std::size_t>(opponent)];
  const float allin_target =
      own_bet + state.Stacks()[static_cast<std::size_t>(actor)];
  const float min_non_allin_target =
      NeedsCall(state) ? opponent_bet + state.Setup()->MinRaiseSize()
                       : state.Setup()->MinRaiseSize();
  const float must_exceed_target = NeedsCall(state) ? opponent_bet : own_bet;

  if (state.Stacks()[static_cast<std::size_t>(actor)] <= 0.0f ||
      state.Stacks()[static_cast<std::size_t>(opponent)] <= 0.0f) {
    return;
  }

  for (const AbstractedAction& abstracted_action :
       AbstractedBetsForState(state)) {
    float target = ResolveTargetBet(abstracted_action, state);
    if (target < 0.0f) {
      continue;
    }
    target = RoundTo(target, state.Setup()->BetRounding());
    if (target >= allin_target *
                      state.Setup()->AbstractedBets().BetToAllInThreshold() /
                      100.0f) {
      target = allin_target;
    }
    if (target >= allin_target) {
      if (allin_target > must_exceed_target) {
        AddUniqueAction(Action::Bet(allin_target), actions);
      }
      continue;
    }
    if (target < min_non_allin_target || target <= must_exceed_target) {
      continue;
    }
    AddUniqueAction(Action::Bet(target), actions);
  }

  if (ShouldAddAllInBySpr(state) && allin_target > must_exceed_target) {
    AddUniqueAction(Action::Bet(allin_target), actions);
  }
}

}  // namespace

std::vector<Action> ActionResolver::Resolve(const NodeState& state) {
  std::vector<Action> actions;
  if (state.IsTerminal()) {
    return actions;
  }
  if (state.ActorPlayer() == NodeState::kChancePlayer) {
    actions.push_back(Action::Chance());
    return actions;
  }

  const int actor = state.ActorPlayer();
  if (NeedsCall(state)) {
    if (state.Stacks()[static_cast<std::size_t>(actor)] > 0.0f) {
      actions.push_back(Action::Fold());
      actions.push_back(Action::Call());
    }
  } else {
    actions.push_back(Action::Check());
  }

  AddBetActions(state, &actions);
  MergeCloseBetActions(state, &actions);
  std::sort(actions.begin(), actions.end());
  actions.erase(std::unique(actions.begin(), actions.end()), actions.end());
  return actions;
}

}  // namespace fisher::game::poker
