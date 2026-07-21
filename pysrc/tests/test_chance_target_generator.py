import numpy as np

from fisher import raw_to_iso_indices
from fisher.data import chance_target_generator
from fisher.data.random_spot_sampler import generate_random_turn_spots


RANKS = "23456789TJQKA"
SUITS = "cdhs"


def _card_string_to_ids(cards):
    output = []
    for index in range(0, len(cards), 2):
        rank = RANKS.index(cards[index])
        suit = SUITS.index(cards[index + 1])
        output.append(rank * 4 + suit)
    return output


def test_chance_target_generator_writes_raw_cfv_targets(tmp_path, monkeypatch):
    input_dir = tmp_path / "input"
    output_dir = tmp_path / "output"
    generate_random_turn_spots(
        num_samples=2,
        num_sample_per_file=2,
        output_dir=input_dir,
        seed=11,
    )

    class FakeSession:
        def __init__(self, board_cards):
            self.board_cards = board_cards

        def metadata(self):
            return {
                "exploitability": 0.0004,
                "iterations": 3,
                "converged": True,
            }

        def node_cfv(self, node_id, player):
            assert node_id == 0
            board_ids = _card_string_to_ids(self.board_cards)
            raw_to_iso = np.asarray(raw_to_iso_indices(board_ids), dtype=np.int32)
            num_iso = int(raw_to_iso.max()) + 1
            cfv = [
                float(player + 1) + float(index) / 1000.0
                for index in range(num_iso)
            ]
            return {"node_id": node_id, "player": player, "cfv": cfv}

    def fake_solve_poker(spot_config, solver_config):
        assert spot_config["current_player"] == -1
        assert spot_config["bet_current_round"] == [0.0, 0.0]
        assert spot_config["abstracted_bets"]["river_bets"] == [
            ["50%", "100%"]
        ]
        assert solver_config["max_iterations"] == 3
        assert solver_config["exploitability_check_interval"] == 1
        assert solver_config["target_exploitability"] == 0.001
        return FakeSession(spot_config["board_cards"])

    monkeypatch.setattr(chance_target_generator, "solve_poker", fake_solve_poker)

    output_files = chance_target_generator.generate_chance_targets(
        input_dir=input_dir,
        output_dir=output_dir,
        num_sample_per_file=1,
        max_iterations=3,
        exploitability_check_interval=1,
        target_exploitability_ratio=0.001,
        num_threads=1,
    )

    assert [path.name for path in output_files] == [
        "chance_targets_000000.npz",
        "chance_targets_000001.npz",
    ]
    with np.load(output_files[0]) as data:
        assert data["pot"].shape == (1,)
        assert data["stacks"].shape == (1, 2)
        assert data["board"].shape == (1, 5)
        assert data["board_size"].tolist() == [4]
        assert data["round"].tolist() == [2]
        assert data["reach"].shape == (1, 2, 1326)
        assert data["target_cfv"].shape == (1, 2, 1326)
        np.testing.assert_allclose(data["exploitability"], [0.0004])
        assert data["iterations"].tolist() == [3]
        assert data["converged"].tolist() == [True]
        assert data["solve_ms"][0] >= 0.0

        board = data["board"][0, : data["board_size"][0]]
        raw_to_iso = np.asarray(
            raw_to_iso_indices([int(card) for card in board]), dtype=np.int32
        )
        blocked = raw_to_iso < 0
        assert np.all(data["target_cfv"][0, :, blocked] == 0.0)
        first_valid_raw = int(np.flatnonzero(~blocked)[0])
        first_iso = raw_to_iso[first_valid_raw]
        assert data["target_cfv"][0, 0, first_valid_raw] == np.float32(
            1.0 + first_iso / 1000.0
        )
        assert data["target_cfv"][0, 1, first_valid_raw] == np.float32(
            2.0 + first_iso / 1000.0
        )
