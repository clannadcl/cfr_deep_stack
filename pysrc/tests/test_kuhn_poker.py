import math

from fisher import (
    build_game,
    nash_exploitability,
    policy_value,
    solve,
)


def test_kuhn_poker_cfr_1000_iter_has_low_nash_exploitability():
    game_config = {"game": "kuhn_poker"}
    solver_config = {"iterations": 1000, "cfr_plus": False}

    game = build_game(game_config)
    result = solve(game_config, solver_config)

    assert game["game"] == "kuhn_poker"
    assert game["num_players"] == 2
    assert result["game"] == "kuhn_poker"
    assert result["iterations"] == 1000
    assert len(result["strategy"]) == 12

    values = policy_value(game_config, result["strategy"])
    assert math.isclose(values[0], -1 / 18, abs_tol=0.01)
    assert math.isclose(values[1], 1 / 18, abs_tol=0.01)

    exploitability = nash_exploitability(game_config, result["strategy"])
    assert exploitability < 0.01
