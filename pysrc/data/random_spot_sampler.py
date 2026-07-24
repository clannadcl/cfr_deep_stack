from __future__ import annotations

import argparse
import gzip
import time
from pathlib import Path

import numpy as np

from fisher import raw_to_iso_indices

NUM_CARDS = 52
NUM_HANDS = 1326
NUM_PLAYERS = 2
TURN_ROUND_ID = 2
DEFAULT_SPR_MIN = 0.05
DEFAULT_SPR_MAX = 200.0
MAX_RANGE_POOL_SAMPLE_ATTEMPTS = 1000


def _load_range_pool(range_pool_path: Path | None) -> np.ndarray | None:
    if range_pool_path is None:
        return None

    if range_pool_path.suffix == ".gz":
        with gzip.open(range_pool_path, "rb") as file:
            range_pool = np.load(file, allow_pickle=False)
    else:
        range_pool = np.load(range_pool_path, allow_pickle=False)

    if range_pool.ndim != 3 or range_pool.shape[1:] != (NUM_PLAYERS, NUM_HANDS):
        raise ValueError("range pool must have shape [num_ranges, 2, 1326]")
    if range_pool.shape[0] <= 0:
        raise ValueError("range pool must contain at least one range")
    if not np.all(np.isfinite(range_pool)):
        raise ValueError("range pool contains non-finite values")
    if np.any(range_pool < 0.0):
        raise ValueError("range pool contains negative weights")
    return np.asarray(range_pool, dtype=np.float32)


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


def _apply_board_blockers(board: np.ndarray, reach: np.ndarray) -> None:
    raw_to_iso = np.asarray(
        raw_to_iso_indices([int(card) for card in board]), dtype=np.int32
    )
    if raw_to_iso.shape != (NUM_HANDS,):
        raise RuntimeError("raw_to_iso_indices returned an unexpected shape")
    reach[:, raw_to_iso < 0] = 0.0


def generate_random_turn_spots(
    num_samples: int,
    num_sample_per_file: int,
    output_dir: Path,
    seed: int,
    range_pool_path: Path | None = None,
) -> list[Path]:
    if num_samples <= 0:
        raise ValueError("num_samples must be positive")
    if num_sample_per_file <= 0:
        raise ValueError("num_sample_per_file must be positive")

    output_dir.mkdir(parents=True, exist_ok=True)
    rng = np.random.default_rng(seed)
    range_pool = _load_range_pool(range_pool_path)
    output_files: list[Path] = []
    remaining = num_samples
    file_index = 0

    while remaining > 0:
        count = min(remaining, num_sample_per_file)
        pot = np.ones(count, dtype=np.float32)
        spr = rng.uniform(DEFAULT_SPR_MIN, DEFAULT_SPR_MAX, size=count).astype(
            np.float32
        )
        stacks = np.repeat(spr[:, None], NUM_PLAYERS, axis=1)
        rake_ratio = rng.uniform(0.0, 0.30, size=count).astype(np.float32)
        rake_cap = rng.uniform(0.0, 30.0, size=count).astype(np.float32)
        board = np.empty((count, 4), dtype=np.uint8)
        round_id = np.full(count, TURN_ROUND_ID, dtype=np.uint8)
        reach = np.empty((count, NUM_PLAYERS, NUM_HANDS), dtype=np.float32)

        for sample_index in range(count):
            for _ in range(MAX_RANGE_POOL_SAMPLE_ATTEMPTS):
                board[sample_index] = rng.choice(
                    NUM_CARDS, size=4, replace=False
                ).astype(np.uint8)
                if range_pool is None:
                    reach[sample_index] = rng.random(
                        (NUM_PLAYERS, NUM_HANDS), dtype=np.float32
                    )
                    _canonicalize_reach_by_isomorphism(
                        board[sample_index], reach[sample_index]
                    )
                    reach[sample_index] /= float(NUM_HANDS)
                else:
                    range_index = int(rng.integers(0, range_pool.shape[0]))
                    reach[sample_index] = range_pool[range_index]
                    _canonicalize_reach_by_isomorphism(
                        board[sample_index], reach[sample_index]
                    )
                if np.all(reach[sample_index].sum(axis=1) > 0.0):
                    break
            else:
                raise RuntimeError(
                    "failed to sample a non-empty turn range after board blockers"
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
    parser.add_argument(
        "--range-pool-path",
        type=Path,
        default=None,
        help="Optional .npy or .npy.gz dense range pool with shape [N, 2, 1326].",
    )
    return parser.parse_args()


def main() -> None:
    args = _parse_args()
    begin = time.perf_counter()
    output_files = generate_random_turn_spots(
        num_samples=args.num_samples,
        num_sample_per_file=args.num_sample_per_file,
        output_dir=args.output_dir,
        seed=args.seed,
        range_pool_path=args.range_pool_path,
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
