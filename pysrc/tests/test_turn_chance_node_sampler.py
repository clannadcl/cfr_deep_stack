import gzip

import numpy as np

from data import turn_chance_node_sampler


def test_sample_iteration_chunks_are_positive_and_sum_to_total():
    rng = np.random.default_rng(17)
    chunks = turn_chance_node_sampler.sample_iteration_chunks(
        total_iterations=400,
        num_checkpoints=20,
        rng=rng,
    )

    assert len(chunks) == 20
    assert sum(chunks) == 400
    assert all(chunk > 0 for chunk in chunks)


def test_sample_abstracted_bets_uses_expected_street_shape():
    rng = np.random.default_rng(3)
    config = turn_chance_node_sampler.sample_abstracted_bets(rng)

    assert 1 <= len(config["turn_donk_bets"]) <= 1
    assert 2 <= len(config["turn_bets"][0]) <= 3
    assert 1 <= len(config["turn_bets"][1]) <= 2
    assert config["river_bets"] == [["100%", "allin"]]
    assert config["river_donk_bets"] == ["100%", "allin"]
    for actions in [config["turn_donk_bets"], *config["turn_bets"]]:
        assert all(action.endswith("%") for action in actions)


def test_turn_chance_node_sampler_writes_compact_schema(tmp_path, monkeypatch):
    range_pool = np.full((2, 2, 1326), 1.0 / 1326.0, dtype=np.float32)
    range_pool_path = tmp_path / "ranges.npy.gz"
    with gzip.open(range_pool_path, "wb") as file:
        np.save(file, range_pool)

    calls = {"run_iterations": []}

    class FakeSession:
        def metadata(self):
            return {"num_nodes": 123}

        def run_iterations(self, num_iterations):
            calls["run_iterations"].append(num_iterations)
            return {"iterations": sum(calls["run_iterations"])}

        def compute_exploitability(self):
            return {
                "iterations": sum(calls["run_iterations"]),
                "exploitability": 0.001,
                "converged": True,
                "num_nodes": 123,
            }

        def sample_turn_chance_nodes(self, sample_fraction, seed):
            assert sample_fraction == 0.5
            assert isinstance(seed, int)
            reach = np.zeros((2, 1326), dtype=np.float32)
            reach[0, 10:20] = 0.25
            reach[1, 100:140] = 0.5
            return [
                {
                    "board": [0, 5, 10, 15],
                    "round": 2,
                    "stacks": [95.0, 95.0],
                    "pot": 11.0,
                    "rake_ratio": 0.05,
                    "rake_cap": 0.6,
                    "last_aggressor": 1,
                    "reach": reach,
                }
            ]

    def fake_create_poker_session(spot_config, solver_config):
        assert spot_config["current_player"] == 0
        assert spot_config["bet_current_round"] == [0.0, 0.0]
        assert spot_config["bet_total"] == [0.0, 0.0]
        assert spot_config["common_pot"] == 1.0
        reach = np.asarray(spot_config["beliefs"], dtype=np.float32)
        assert reach.shape == (2, 1326)
        assert np.all(reach.sum(axis=1) <= 1.0)
        assert spot_config["abstracted_bets"]["river_bets"] == [
            ["100%", "allin"]
        ]
        assert solver_config["target_exploitability"] == 0.0015
        return FakeSession()

    monkeypatch.setattr(
        turn_chance_node_sampler,
        "create_poker_session",
        fake_create_poker_session,
    )

    output_files = turn_chance_node_sampler.generate_turn_chance_node_samples(
        num_roots=1,
        num_sample_per_file=10,
        output_dir=tmp_path / "out",
        range_pool_path=range_pool_path,
        seed=9,
        total_iterations=8,
        num_checkpoints=4,
        sample_fraction=0.5,
        target_exploitability_ratio=0.0015,
        num_threads=1,
    )

    assert len(calls["run_iterations"]) == 1
    assert output_files == [tmp_path / "out" / "turn_chance_nodes_000000.npz"]
    with np.load(output_files[0]) as data:
        assert set(data.files) == {
            "board",
            "round",
            "stacks",
            "pot",
            "rake_ratio",
            "rake_cap",
            "last_aggressor",
            "exploitability",
            "reach",
        }
        assert data["board"].shape == (1, 4)
        assert data["round"].tolist() == [2]
        assert data["stacks"].shape == (1, 2)
        assert data["pot"].tolist() == [11.0]
        assert data["rake_ratio"].tolist() == [np.float32(0.05)]
        assert data["rake_cap"].tolist() == [np.float32(0.6)]
        assert data["last_aggressor"].tolist() == [1]
        assert data["exploitability"].tolist() == [np.float32(0.001)]
        assert data["reach"].shape == (1, 2, 1326)
        np.testing.assert_allclose(data["reach"].sum(axis=2), [[1.0, 1.0]])
        np.testing.assert_allclose(data["reach"][0, 0, 10:20], np.full(10, 0.1))
        np.testing.assert_allclose(data["reach"][0, 1, 100:140], np.full(40, 0.025))
