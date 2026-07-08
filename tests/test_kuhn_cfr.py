import numpy as np

from cfr_deep_stack.algorithms.cfr import CFRSolver
from cfr_deep_stack.algorithms.evaluation import exploitability, policy_value
from cfr_deep_stack.games.kuhn_poker import KuhnPokerGame


def test_kuhn_poker_cfr_average_policy_converges_to_known_value():
    game = KuhnPokerGame()
    solver = CFRSolver(game)

    for _ in range(1000):
        solver.evaluate_and_update_policy()

    average_policy = solver.average_policy()
    values = policy_value(game, average_policy)

    np.testing.assert_allclose(values, [-1 / 18, 1 / 18], atol=0.01)


def test_kuhn_poker_cfr_average_policy_reduces_exploitability():
    game = KuhnPokerGame()
    solver = CFRSolver(game)

    for _ in range(1500):
        solver.evaluate_and_update_policy()

    assert exploitability(game, solver.average_policy()) < 0.03


def test_cfr_plus_runs_and_creates_policy_for_all_infosets():
    game = KuhnPokerGame()
    solver = CFRSolver(game, regret_matching_plus=True)

    for _ in range(20):
        solver.evaluate_and_update_policy()

    average_policy = solver.average_policy()
    assert len(average_policy) == 12
    for action_probs in average_policy.values():
        assert abs(sum(action_probs.values()) - 1.0) < 1e-12
