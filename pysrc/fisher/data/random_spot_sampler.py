from __future__ import annotations

import argparse
import time
from pathlib import Path

import numpy as np

from fisher import raw_to_iso_indices

NUM_CARDS = 52
NUM_HANDS = 1326
NUM_PLAYERS = 2
TURN_ROUND_ID = 2


def _canonicalize_reach_by_isomorphism(
    board: np.ndarray, reach: np.ndarray
) -> None:
    raw_to_iso = np.asarray(
        raw_to_iso_indices([int(card) for card in board]), dtype=np.int32
    )
    if raw_to_iso.shape != (NUM_HANDS,):
        raise RuntimeError("raw_to_iso_indices returned an unexpected shape")

    valid = raw_to_iso >= 0
    reach[:, ~valid] = 0.0
    valid_iso = raw_to_iso[valid]
    num_iso = int(valid_iso.max()) + 1 if valid_iso.size else 0
    if num_iso <= 0:
        raise RuntimeError("board produced no legal iso hands")

    counts = np.bincount(valid_iso, minlength=num_iso).astype(np.float32)
    if np.any(counts == 0.0):
        raise RuntimeError("raw_to_iso_indices returned non-contiguous iso ids")

    for player in range(NUM_PLAYERS):
        sums = np.bincount(
            valid_iso, weights=reach[player, valid], minlength=num_iso
        ).astype(np.float32)
        means = sums / counts
        reach[player, valid] = means[valid_iso]


def generate_random_turn_spots(
    num_samples: int,
    num_sample_per_file: int,
    output_dir: Path,
    seed: int,
) -> list[Path]:
    if num_samples <= 0:
        raise ValueError("num_samples must be positive")
    if num_sample_per_file <= 0:
        raise ValueError("num_sample_per_file must be positive")

    output_dir.mkdir(parents=True, exist_ok=True)
    rng = np.random.default_rng(seed)
    output_files: list[Path] = []
    remaining = num_samples
    file_index = 0

    while remaining > 0:
        count = min(remaining, num_sample_per_file)
        pot = np.ones(count, dtype=np.float32)
        spr = rng.uniform(0.05, 250.0, size=count).astype(np.float32)
        stacks = np.repeat(spr[:, None], NUM_PLAYERS, axis=1)
        rake_ratio = rng.uniform(0.0, 0.30, size=count).astype(np.float32)
        rake_cap = rng.uniform(0.0, 30.0, size=count).astype(np.float32)
        board = np.empty((count, 4), dtype=np.uint8)
        round_id = np.full(count, TURN_ROUND_ID, dtype=np.uint8)
        reach = rng.random((count, NUM_PLAYERS, NUM_HANDS), dtype=np.float32)

        for sample_index in range(count):
            board[sample_index] = rng.choice(
                NUM_CARDS, size=4, replace=False
            ).astype(np.uint8)
            _canonicalize_reach_by_isomorphism(
                board[sample_index], reach[sample_index]
            )

        output_path = output_dir / f"random_turn_spots_{file_index:06d}.npz"
        np.savez_compressed(
            output_path,
            pot=pot,
            stacks=stacks,
            rake_ratio=rake_ratio,
            rake_cap=rake_cap,
            board=board,
            round=round_id,
            reach=reach,
        )
        output_files.append(output_path)
        remaining -= count
        file_index += 1

    return output_files


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate random turn spots for DeepStack value training."
    )
    parser.add_argument("--num-samples", type=int, required=True)
    parser.add_argument("--num-sample-per-file", type=int, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--seed", type=int, default=0)
    return parser.parse_args()


def main() -> None:
    args = _parse_args()
    begin = time.perf_counter()
    output_files = generate_random_turn_spots(
        num_samples=args.num_samples,
        num_sample_per_file=args.num_sample_per_file,
        output_dir=args.output_dir,
        seed=args.seed,
    )
    elapsed = time.perf_counter() - begin
    total_bytes = sum(path.stat().st_size for path in output_files)
    print(
        "generated "
        f"num_samples={args.num_samples} "
        f"num_files={len(output_files)} "
        f"total_bytes={total_bytes} "
        f"bytes_per_sample={total_bytes / args.num_samples:.2f} "
        f"elapsed_sec={elapsed:.3f}"
    )


if __name__ == "__main__":
    main()
