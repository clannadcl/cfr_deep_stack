import math

from fisher import (
    build_game,
    nash_exploitability,
    policy_value,
    solve,
    solve_poker,
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


def test_poker_node_equity_smoke():
    session = solve_poker(
        {
            "board_cards": "AhKhQh2c3d",
            "common_pot": 10.0,
            "stacks": [10.0, 10.0],
            "current_player": 0,
            "previous_street_aggressor": 0,
            "ranges": ["7c8d:1", "JhTh:1"],
            "abstracted_bets": {
                "bets": [["allin"]],
                "donk_bets": ["allin"],
            },
        },
        {"max_iterations": 1, "exploitability_check_interval": 1},
    )

    equity = session.node_equity(0, 0)

    assert equity["node_id"] == 0
    assert equity["player"] == 0
    assert equity["board"] == "AhKhQh2c3d"
    assert len(equity["equity"]) > 0
    assert 0.0 <= equity["range_equity"] <= 1.0
    assert all(0.0 <= value <= 1.0 for value in equity["equity"])
