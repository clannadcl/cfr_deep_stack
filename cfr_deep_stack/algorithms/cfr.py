"""Tabular counterfactual regret minimization.

The implementation follows OpenSpiel's CFR shape: current strategies are
derived through regret matching and the average policy is the convergent output.
It is deliberately compact while the project is still using small poker games
to validate the CFR-average pipeline.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Dict, Mapping, MutableMapping, Sequence

import numpy as np

from cfr_deep_stack.games.kuhn_poker import CHANCE_PLAYER


@dataclass
class InfoStateNode:
    legal_actions: Sequence[int]
    cumulative_regret: MutableMapping[int, float] = field(default_factory=dict)
    cumulative_policy: MutableMapping[int, float] = field(default_factory=dict)


def regret_matching(
    cumulative_regret: Mapping[int, float], legal_actions: Sequence[int]
) -> Dict[int, float]:
    positive_regrets = {
        action: max(float(cumulative_regret.get(action, 0.0)), 0.0)
        for action in legal_actions
    }
    normalizer = sum(positive_regrets.values())
    if normalizer <= 0.0:
        probability = 1.0 / len(legal_actions)
        return {action: probability for action in legal_actions}
    return {
        action: positive_regrets[action] / normalizer for action in legal_actions
    }


class CFRSolver:
    """Alternating-update tabular CFR solver."""

    def __init__(self, game, regret_matching_plus: bool = False):
        self.game = game
        self.num_players = game.num_players()
        self.regret_matching_plus = regret_matching_plus
        self.info_state_nodes: Dict[str, InfoStateNode] = {}
        self.iterations = 0

    def evaluate_and_update_policy(self) -> np.ndarray:
        """Runs one full CFR iteration and returns root values by player."""
        values = np.zeros(self.num_players, dtype=np.float64)
        for player in range(self.num_players):
            reach = np.ones(self.num_players, dtype=np.float64)
            values[player] = self._cfr(self.game.new_initial_state(), player, reach)
        self.iterations += 1
        return values

    def average_policy(self) -> Dict[str, Dict[int, float]]:
        return {
            info_state: self._average_strategy(node)
            for info_state, node in self.info_state_nodes.items()
        }

    def current_policy(self) -> Dict[str, Dict[int, float]]:
        return {
            info_state: regret_matching(node.cumulative_regret, node.legal_actions)
            for info_state, node in self.info_state_nodes.items()
        }

    def action_probabilities(self, state, player: int | None = None) -> Dict[int, float]:
        acting_player = state.current_player() if player is None else player
        info_state = state.information_state_string(acting_player)
        node = self._node_for_state(state, acting_player)
        average = self._average_strategy(node)
        if info_state not in self.info_state_nodes:
            return average
        return average

    def _cfr(self, state, updating_player: int, reach: np.ndarray) -> float:
        if state.is_terminal():
            return float(state.returns()[updating_player])

        current_player = state.current_player()
        if current_player == CHANCE_PLAYER:
            return sum(
                probability * self._cfr(state.child(action), updating_player, reach)
                for action, probability in state.chance_outcomes()
            )

        node = self._node_for_state(state, current_player)
        strategy = regret_matching(node.cumulative_regret, node.legal_actions)

        action_values = {}
        node_value = 0.0
        for action in node.legal_actions:
            next_reach = reach.copy()
            next_reach[current_player] *= strategy[action]
            action_value = self._cfr(state.child(action), updating_player, next_reach)
            action_values[action] = action_value
            node_value += strategy[action] * action_value

        if current_player == updating_player:
            opponent_reach = np.prod(np.delete(reach, updating_player))
            player_reach = reach[updating_player]
            for action in node.legal_actions:
                regret = opponent_reach * (action_values[action] - node_value)
                updated = node.cumulative_regret.get(action, 0.0) + regret
                if self.regret_matching_plus:
                    updated = max(updated, 0.0)
                node.cumulative_regret[action] = updated
                node.cumulative_policy[action] = (
                    node.cumulative_policy.get(action, 0.0)
                    + player_reach * strategy[action]
                )

        return node_value

    def _node_for_state(self, state, player: int) -> InfoStateNode:
        info_state = state.information_state_string(player)
        node = self.info_state_nodes.get(info_state)
        if node is None:
            node = InfoStateNode(tuple(int(action) for action in state.legal_actions(player)))
            self.info_state_nodes[info_state] = node
        return node

    @staticmethod
    def _average_strategy(node: InfoStateNode) -> Dict[int, float]:
        normalizer = sum(node.cumulative_policy.values())
        if normalizer <= 0.0:
            probability = 1.0 / len(node.legal_actions)
            return {action: probability for action in node.legal_actions}
        return {
            action: node.cumulative_policy.get(action, 0.0) / normalizer
            for action in node.legal_actions
        }
