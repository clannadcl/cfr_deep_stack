import numpy as np

from fisher import raw_to_iso_indices
from fisher.data.random_spot_sampler import generate_random_turn_spots


def _raw_hand_cards():
    hands = []
    for first in range(52):
        for second in range(first + 1, 52):
            hands.append((first, second))
    assert len(hands) == 1326
    return hands


def _assert_reach_is_canonical(board, reach):
    raw_to_iso = np.asarray(raw_to_iso_indices([int(card) for card in board]))
    hands = _raw_hand_cards()
    blocked = np.asarray(
        [first in board or second in board for first, second in hands],
        dtype=bool,
    )
    assert np.all(raw_to_iso[blocked] == -1)
    assert np.all(reach[:, blocked] == 0.0)

    valid_iso = raw_to_iso[raw_to_iso >= 0]
    assert valid_iso.min() == 0
    assert valid_iso.max() + 1 == len(np.unique(valid_iso))

    for player in range(2):
        values = reach[player]
        for iso in np.unique(valid_iso):
            indices = raw_to_iso == iso
            assert np.all(values[indices] == values[indices][0])


def test_random_turn_spot_sampler_writes_dense_npz_files(tmp_path):
    output_files = generate_random_turn_spots(
        num_samples=25,
        num_sample_per_file=10,
        output_dir=tmp_path,
        seed=7,
    )

    assert [path.name for path in output_files] == [
        "random_turn_spots_000000.npz",
        "random_turn_spots_000001.npz",
        "random_turn_spots_000002.npz",
    ]

    total_samples = 0
    for path in output_files:
        with np.load(path) as data:
            count = data["pot"].shape[0]
            total_samples += count
            assert data["pot"].dtype == np.float32
            assert data["stacks"].dtype == np.float32
            assert data["rake_ratio"].dtype == np.float32
            assert data["rake_cap"].dtype == np.float32
            assert data["board"].dtype == np.uint8
            assert data["round"].dtype == np.uint8
            assert data["reach"].dtype == np.float32
            assert data["stacks"].shape == (count, 2)
            assert data["board"].shape == (count, 4)
            assert data["reach"].shape == (count, 2, 1326)
            assert np.all(data["pot"] == 1.0)
            assert np.all(data["stacks"] >= 0.05)
            assert np.all(data["stacks"] <= 250.0)
            assert np.all(data["stacks"][:, 0] == data["stacks"][:, 1])
            assert np.all(data["rake_ratio"] >= 0.0)
            assert np.all(data["rake_ratio"] <= 0.30)
            assert np.all(data["rake_cap"] >= 0.0)
            assert np.all(data["rake_cap"] <= 30.0)
            assert np.all(data["round"] == 2)
            for board, reach in zip(data["board"], data["reach"]):
                assert len(set(int(card) for card in board)) == 4
                _assert_reach_is_canonical(board, reach)

    assert total_samples == 25


def test_random_turn_spot_sampler_is_seed_reproducible(tmp_path):
    first_dir = tmp_path / "first"
    second_dir = tmp_path / "second"
    first_files = generate_random_turn_spots(
        num_samples=12,
        num_sample_per_file=5,
        output_dir=first_dir,
        seed=42,
    )
    second_files = generate_random_turn_spots(
        num_samples=12,
        num_sample_per_file=5,
        output_dir=second_dir,
        seed=42,
    )

    for first, second in zip(first_files, second_files):
        with np.load(first) as first_data, np.load(second) as second_data:
            assert set(first_data.files) == set(second_data.files)
            for key in first_data.files:
                np.testing.assert_array_equal(first_data[key], second_data[key])
