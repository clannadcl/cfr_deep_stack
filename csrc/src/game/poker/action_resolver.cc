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
    case AbstractedActionType::kAllIn:
      return own_bet + state.Stacks()[static_cast<std::size_t>(actor)];
    case AbstractedActionType::kFold:
    case AbstractedActionType::kCheck:
    case AbstractedActionType::kCall:
      break;
  }
  return -1.0f;
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
  std::sort(actions.begin(), actions.end());
  actions.erase(std::unique(actions.begin(), actions.end()), actions.end());
  return actions;
}

}  // namespace fisher::game::poker
