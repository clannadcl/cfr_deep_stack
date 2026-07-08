"""Evaluation utilities for small tabular poker games."""

from __future__ import annotations

import itertools
from typing import Dict, Mapping

import numpy as np

from cfr_deep_stack.games.kuhn_poker import CHANCE_PLAYER

Policy = Mapping[str, Mapping[int, float]]


def expected_returns(state, policy: Policy) -> np.ndarray:
    if state.is_terminal():
        return np.array(state.returns(), dtype=np.float64)

    current_player = state.current_player()
    if current_player == CHANCE_PLAYER:
        return sum(
            probability * expected_returns(state.child(action), policy)
            for action, probability in state.chance_outcomes()
        )

    info_state = state.information_state_string(current_player)
    action_probs = policy.get(info_state)
    if action_probs is None:
        legal_actions = state.legal_actions(current_player)
        probability = 1.0 / len(legal_actions)
        action_probs = {action: probability for action in legal_actions}

    return sum(
        probability * expected_returns(state.child(action), policy)
        for action, probability in action_probs.items()
    )


def policy_value(game, policy: Policy) -> np.ndarray:
    return expected_returns(game.new_initial_state(), policy)


def exploitability(game, policy: Policy) -> float:
    """Computes exploitability by exhaustive BR search.

    This is meant for tiny validation games. It enumerates deterministic best
    responses over each player's reachable information states.
    """
    on_policy = policy_value(game, policy)
    improvements = []
    for player in range(game.num_players()):
        info_states = sorted(_collect_info_states(game.new_initial_state(), player))
        best_value = -float("inf")
        for actions in itertools.product((0, 1), repeat=len(info_states)):
            response: Dict[str, Dict[int, float]] = {
                info_state: {action: 1.0}
                for info_state, action in zip(info_states, actions)
            }
            candidate = _merge_policy_for_player(policy, response, player, game)
            best_value = max(best_value, policy_value(game, candidate)[player])
        improvements.append(best_value - on_policy[player])
    return float(sum(improvements) / game.num_players())


def _collect_info_states(state, player: int) -> set[str]:
    if state.is_terminal():
        return set()
    if state.current_player() == CHANCE_PLAYER:
        states = set()
        for action, _ in state.chance_outcomes():
            states.update(_collect_info_states(state.child(action), player))
        return states
    current_player = state.current_player()
    states = set()
    if current_player == player:
        states.add(state.information_state_string(player))
    for action in state.legal_actions(current_player):
        states.update(_collect_info_states(state.child(action), player))
    return states


def _merge_policy_for_player(
    base_policy: Policy, response: Policy, player: int, game
) -> Dict[str, Dict[int, float]]:
    merged = {key: dict(value) for key, value in base_policy.items()}
    _install_response_states(game.new_initial_state(), response, player, merged)
    return merged


def _install_response_states(state, response: Policy, player: int, merged) -> None:
    if state.is_terminal():
        return
    current_player = state.current_player()
    if current_player == CHANCE_PLAYER:
        for action, _ in state.chance_outcomes():
            _install_response_states(state.child(action), response, player, merged)
        return
    info_state = state.information_state_string(current_player)
    if current_player == player and info_state in response:
        merged[info_state] = dict(response[info_state])
    for action in state.legal_actions(current_player):
        _install_response_states(state.child(action), response, player, merged)
