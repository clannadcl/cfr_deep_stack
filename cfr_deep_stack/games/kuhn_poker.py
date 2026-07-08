"""Pure-Python Kuhn poker with a small OpenSpiel-like API.

This file is based on the game structure in OpenSpiel's Python Kuhn poker
example, but avoids the `pyspiel` extension so the repository has a runnable
baseline before the C++ OpenSpiel subset is ported.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from enum import IntEnum
from typing import List, Sequence, Tuple


class Action(IntEnum):
    PASS = 0
    BET = 1


CHANCE_PLAYER = -1
TERMINAL_PLAYER = -2
_DECK = (0, 1, 2)


class KuhnPokerGame:
    """Two-player Kuhn poker."""

    def num_players(self) -> int:
        return 2

    def new_initial_state(self) -> "KuhnPokerState":
        return KuhnPokerState()


@dataclass
class KuhnPokerState:
    cards: List[int] = field(default_factory=list)
    bets: List[int] = field(default_factory=list)
    pot: List[float] = field(default_factory=lambda: [1.0, 1.0])
    game_over: bool = False
    next_player: int = 0

    def clone(self) -> "KuhnPokerState":
        return KuhnPokerState(
            cards=list(self.cards),
            bets=list(self.bets),
            pot=list(self.pot),
            game_over=self.game_over,
            next_player=self.next_player,
        )

    def child(self, action: int) -> "KuhnPokerState":
        state = self.clone()
        state.apply_action(action)
        return state

    def current_player(self) -> int:
        if self.game_over:
            return TERMINAL_PLAYER
        if len(self.cards) < 2:
            return CHANCE_PLAYER
        return self.next_player

    def is_terminal(self) -> bool:
        return self.game_over

    def is_chance_node(self) -> bool:
        return self.current_player() == CHANCE_PLAYER

    def chance_outcomes(self) -> Sequence[Tuple[int, float]]:
        outcomes = [card for card in _DECK if card not in self.cards]
        probability = 1.0 / len(outcomes)
        return [(card, probability) for card in outcomes]

    def legal_actions(self, player: int | None = None) -> Sequence[int]:
        del player
        if self.is_terminal() or self.is_chance_node():
            return []
        return [Action.PASS, Action.BET]

    def apply_action(self, action: int) -> None:
        if self.is_chance_node():
            self.cards.append(int(action))
            return

        action = int(action)
        self.bets.append(action)
        if action == Action.BET:
            self.pot[self.next_player] += 1.0

        self.next_player = 1 - self.next_player
        if (
            min(self.pot) == 2
            or (len(self.bets) == 2 and action == Action.PASS)
            or len(self.bets) == 3
        ):
            self.game_over = True

    def information_state_string(self, player: int) -> str:
        if len(self.cards) <= player:
            return f"p{player}:?"
        history = "".join("pb"[action] for action in self.bets)
        return f"{self.cards[player]}{history}"

    def returns(self) -> List[float]:
        if not self.game_over:
            return [0.0, 0.0]

        winnings = float(min(self.pot))
        if self.pot[0] > self.pot[1]:
            return [winnings, -winnings]
        if self.pot[0] < self.pot[1]:
            return [-winnings, winnings]
        if self.cards[0] > self.cards[1]:
            return [winnings, -winnings]
        return [-winnings, winnings]

    def __str__(self) -> str:
        cards = "".join(str(card) for card in self.cards)
        bets = "".join("pb"[action] for action in self.bets)
        return cards + bets
