from __future__ import annotations

import argparse
import math
import time
from pathlib import Path
from typing import Any

import numpy as np

from data.random_spot_sampler import (
    MAX_RANGE_POOL_SAMPLE_ATTEMPTS,
    NUM_CARDS,
    NUM_HANDS,
    NUM_PLAYERS,
    _canonicalize_reach_by_isomorphism,
    _load_range_pool,
)
from fisher import create_poker_session

RANKS = "23456789TJQKA"
SUITS = "cdhs"
TURN_ROUND_ID = 2
DEFAULT_SPR_MIN = 0.05
DEFAULT_SPR_MAX = 200.0
NORMALIZE_EPSILON = 1e-15

TURN_DONK_POOL = (33.0, 50.0)
TURN_BET_POOL = (33.0, 50.0, 75.0, 100.0, 150.0)
TURN_RAISE_POOL = (50.0, 100.0, 150.0)


def _card_id_to_string(card: int) -> str:
    if card < 0 or card >= NUM_CARDS:
        raise ValueError("card id must be in [0, 51]")
    return RANKS[card // 4] + SUITS[card % 4]


def _board_to_string(board: np.ndarray) -> str:
    return "".join(_card_id_to_string(int(card)) for card in board)


def _format_percent(value: float) -> str:
    return f"{value:.3f}".rstrip("0").rstrip(".") + "%"


def _maybe_jitter_percent(value: float, rng: np.random.Generator) -> float:
    if bool(rng.integers(0, 2)):
        value *= float(rng.uniform(0.8, 1.2))
    return value


def _sample_percent_actions(
    pool: tuple[float, ...],
    min_count: int,
    max_count: int,
    rng: np.random.Generator,
) -> list[str]:
    count = int(rng.integers(min_count, max_count + 1))
    if count > len(pool):
        raise ValueError("sample count cannot exceed action pool size")
    values = rng.choice(np.asarray(pool, dtype=np.float32), size=count, replace=False)
    jittered = sorted(_maybe_jitter_percent(float(value), rng) for value in values)
    return [_format_percent(value) for value in jittered]


def sample_abstracted_bets(rng: np.random.Generator) -> dict[str, Any]:
    turn_donk_bets = _sample_percent_actions(TURN_DONK_POOL, 1, 1, rng)
    turn_open_bets = _sample_percent_actions(TURN_BET_POOL, 2, 3, rng)
    turn_raise_bets = _sample_percent_actions(TURN_RAISE_POOL, 1, 2, rng)
    return {
        "turn_donk_bets": turn_donk_bets,
        "turn_bets": [turn_open_bets, turn_raise_bets],
        "river_donk_bets": ["100%", "allin"],
        "river_bets": [["100%", "allin"]],
    }


def sample_iteration_chunks(
    total_iterations: int, num_checkpoints: int, rng: np.random.Generator
) -> list[int]:
    if total_iterations <= 0:
        raise ValueError("total_iterations must be positive")
    if num_checkpoints <= 0:
        raise ValueError("num_checkpoints must be positive")
    if num_checkpoints > total_iterations:
        raise ValueError("num_checkpoints cannot exceed total_iterations")
    if num_checkpoints == 1:
        return [total_iterations]

    cuts = np.sort(
        rng.choice(
            np.arange(1, total_iterations, dtype=np.int32),
            size=num_checkpoints - 1,
            replace=False,
        )
    )
    points = np.concatenate(
        [np.asarray([0], dtype=np.int32), cuts, np.asarray([total_iterations])]
    )
    chunks = np.diff(points).astype(np.int32)
    return [int(chunk) for chunk in chunks]


def _sample_root_inputs(
    range_pool: np.ndarray,
    rng: np.random.Generator,
) -> tuple[np.ndarray, np.ndarray, np.ndarray, float, float, int]:
    for _ in range(MAX_RANGE_POOL_SAMPLE_ATTEMPTS):
        board = rng.choice(NUM_CARDS, size=4, replace=False).astype(np.uint8)
        range_index = int(rng.integers(0, range_pool.shape[0]))
        reach = np.asarray(range_pool[range_index], dtype=np.float32).copy()
        _canonicalize_reach_by_isomorphism(board, reach)
        if np.all(reach.sum(axis=1) > 0.0):
            break
    else:
        raise RuntimeError("failed to sample a non-empty turn range")

    spr = float(rng.uniform(DEFAULT_SPR_MIN, DEFAULT_SPR_MAX))
    stacks = np.asarray([spr, spr], dtype=np.float32)
    rake_ratio = float(rng.uniform(0.0, 0.30))
    rake_cap = float(rng.uniform(0.0, 30.0))
    last_aggressor = int(rng.integers(0, NUM_PLAYERS))
    return board, reach, stacks, rake_ratio, rake_cap, last_aggressor


def _normalize_reach_for_export(reach: np.ndarray) -> np.ndarray:
    if reach.shape != (NUM_PLAYERS, NUM_HANDS):
        raise ValueError("sampled turn chance reach must have shape [2, 1326]")
    normalized = reach.astype(np.float32, copy=True)
    for player in range(NUM_PLAYERS):
        player_sum = float(normalized[player].sum())
        if player_sum <= NORMALIZE_EPSILON:
            raise ValueError("sampled turn chance reach has zero player mass")
        normalized[player] /= player_sum
    return normalized


def _build_turn_root_spot(
    *,
    board: np.ndarray,
    pot: float,
    stacks: np.ndarray,
    rake_ratio: float,
    rake_cap: float,
    last_aggressor: int,
    reach: np.ndarray,
    abstracted_bets: dict[str, Any],
) -> dict[str, Any]:
    return {
        "board_cards": _board_to_string(board),
        "common_pot": float(pot),
        "stacks": [float(stacks[0]), float(stacks[1])],
        "bet_total": [0.0, 0.0],
        "bet_current_round": [0.0, 0.0],
        "current_player": 0,
        "previous_street_aggressor": int(last_aggressor),
        "raise_count": 0,
        "beliefs": reach.astype(np.float32, copy=False).tolist(),
        "min_bet_increment": 0.1,
        "rake": {
            "enabled": True,
            "percentage": float(rake_ratio),
            "cap": float(rake_cap),
        },
        "abstracted_bets": abstracted_bets,
    }


class _OutputBuffer:
    def __init__(self, output_dir: Path, num_sample_per_file: int) -> None:
        self.output_dir = output_dir
        self.num_sample_per_file = num_sample_per_file
        self.file_index = 0
        self.clear()

    def clear(self) -> None:
        self.board: list[np.ndarray] = []
        self.round_id: list[int] = []
        self.stacks: list[np.ndarray] = []
        self.pot: list[float] = []
        self.rake_ratio: list[float] = []
        self.rake_cap: list[float] = []
        self.last_aggressor: list[int] = []
        self.exploitability: list[float] = []
        self.reach: list[np.ndarray] = []

    def __len__(self) -> int:
        return len(self.pot)

    def append(self, sample: dict[str, Any], exploitability: float) -> None:
        board = np.asarray(sample["board"], dtype=np.uint8)
        if board.shape != (4,):
            raise ValueError("sampled turn chance board must contain 4 cards")
        reach = np.asarray(sample["reach"], dtype=np.float32)
        reach = _normalize_reach_for_export(reach)

        self.board.append(board)
        self.round_id.append(int(sample["round"]))
        self.stacks.append(np.asarray(sample["stacks"], dtype=np.float32))
        self.pot.append(float(sample["pot"]))
        self.rake_ratio.append(float(sample["rake_ratio"]))
        self.rake_cap.append(float(sample["rake_cap"]))
        self.last_aggressor.append(int(sample["last_aggressor"]))
        self.exploitability.append(float(exploitability))
        self.reach.append(reach)

    def should_flush(self) -> bool:
        return len(self) >= self.num_sample_per_file

    def flush(self) -> Path | None:
        if len(self) == 0:
            return None
        self.output_dir.mkdir(parents=True, exist_ok=True)
        output_path = self.output_dir / f"turn_chance_nodes_{self.file_index:06d}.npz"
        np.savez_compressed(
            output_path,
            board=np.asarray(self.board, dtype=np.uint8),
            round=np.asarray(self.round_id, dtype=np.uint8),
            stacks=np.asarray(self.stacks, dtype=np.float32),
            pot=np.asarray(self.pot, dtype=np.float32),
            rake_ratio=np.asarray(self.rake_ratio, dtype=np.float32),
            rake_cap=np.asarray(self.rake_cap, dtype=np.float32),
            last_aggressor=np.asarray(self.last_aggressor, dtype=np.int8),
            exploitability=np.asarray(self.exploitability, dtype=np.float32),
            reach=np.asarray(self.reach, dtype=np.float32),
        )
        self.file_index += 1
        self.clear()
        return output_path


def generate_turn_chance_node_samples(
    *,
    num_roots: int,
    num_sample_per_file: int,
    output_dir: Path,
    range_pool_path: Path,
    seed: int,
    total_iterations: int = 400,
    num_checkpoints: int = 20,
    sample_fraction: float = 0.15,
    target_exploitability_ratio: float = 0.0015,
    num_threads: int = 0,
) -> list[Path]:
    if num_roots <= 0:
        raise ValueError("num_roots must be positive")
    if num_sample_per_file <= 0:
        raise ValueError("num_sample_per_file must be positive")
    if not math.isfinite(sample_fraction) or sample_fraction <= 0.0:
        raise ValueError("sample_fraction must be positive")

    range_pool = _load_range_pool(range_pool_path)
    if range_pool is None:
        raise ValueError("range_pool_path is required")

    rng = np.random.default_rng(seed)
    output = _OutputBuffer(output_dir, num_sample_per_file)
    output_files: list[Path] = []

    for root_index in range(num_roots):
        root_begin = time.perf_counter()
        board, reach, stacks, rake_ratio, rake_cap, last_aggressor = (
            _sample_root_inputs(range_pool, rng)
        )
        abstracted_bets = sample_abstracted_bets(rng)
        pot = 1.0
        spot_config = _build_turn_root_spot(
            board=board,
            pot=pot,
            stacks=stacks,
            rake_ratio=rake_ratio,
            rake_cap=rake_cap,
            last_aggressor=last_aggressor,
            reach=reach,
            abstracted_bets=abstracted_bets,
        )
        solver_config = {
            "max_iterations": int(total_iterations),
            "exploitability_check_interval": 1,
            "target_exploitability": float(pot) * float(target_exploitability_ratio),
            "num_threads": int(num_threads),
        }
        session = create_poker_session(spot_config, solver_config)
        metadata = session.metadata()
        print(
            "started "
            f"root={root_index} "
            f"board={_board_to_string(board)} "
            f"num_nodes={metadata['num_nodes']} "
            f"spr={float(stacks[0]):.3f} "
            f"last_aggressor={last_aggressor}",
            flush=True,
        )
        chunks = sample_iteration_chunks(total_iterations, num_checkpoints, rng)

        last_metadata: dict[str, Any] | None = None
        for chunk in chunks:
            session.run_iterations(int(chunk))
            metadata = session.compute_exploitability()
            last_metadata = metadata
            samples = session.sample_turn_chance_nodes(
                sample_fraction=float(sample_fraction),
                seed=int(rng.integers(0, np.iinfo(np.int64).max)),
            )
            for sample in samples:
                output.append(sample, float(metadata["exploitability"]))
                if output.should_flush():
                    flushed = output.flush()
                    if flushed is not None:
                        output_files.append(flushed)
            if bool(metadata["converged"]):
                break

        if last_metadata is None:
            raise RuntimeError("turn chance sampler did not run any checkpoint")
        print(
            "sampled "
            f"root={root_index} "
            f"iterations={last_metadata['iterations']} "
            f"exploitability={last_metadata['exploitability']:.6g} "
            f"converged={last_metadata['converged']} "
            f"num_nodes={last_metadata['num_nodes']} "
            f"elapsed_sec={time.perf_counter() - root_begin:.3f}",
            flush=True,
        )

    flushed = output.flush()
    if flushed is not None:
        output_files.append(flushed)
    return output_files


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Sample turn chance nodes from ongoing CFR solves."
    )
    parser.add_argument("--num-roots", type=int, required=True)
    parser.add_argument("--num-sample-per-file", type=int, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--range-pool-path", type=Path, required=True)
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--total-iterations", type=int, default=400)
    parser.add_argument("--num-checkpoints", type=int, default=20)
    parser.add_argument("--sample-fraction", type=float, default=0.15)
    parser.add_argument("--target-exploitability-ratio", type=float, default=0.0015)
    parser.add_argument("--num-threads", type=int, default=0)
    return parser.parse_args()


def main() -> None:
    args = _parse_args()
    begin = time.perf_counter()
    output_files = generate_turn_chance_node_samples(
        num_roots=args.num_roots,
        num_sample_per_file=args.num_sample_per_file,
        output_dir=args.output_dir,
        range_pool_path=args.range_pool_path,
        seed=args.seed,
        total_iterations=args.total_iterations,
        num_checkpoints=args.num_checkpoints,
        sample_fraction=args.sample_fraction,
        target_exploitability_ratio=args.target_exploitability_ratio,
        num_threads=args.num_threads,
    )
    elapsed = time.perf_counter() - begin
    total_bytes = sum(path.stat().st_size for path in output_files)
    print(
        "generated "
        f"num_roots={args.num_roots} "
        f"num_files={len(output_files)} "
        f"total_bytes={total_bytes} "
        f"elapsed_sec={elapsed:.3f}"
    )


if __name__ == "__main__":
    main()
