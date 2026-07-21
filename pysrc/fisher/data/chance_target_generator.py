from __future__ import annotations

import argparse
import time
from pathlib import Path
from typing import Any

import numpy as np

from fisher import raw_to_iso_indices, solve_poker

NUM_HANDS = 1326
NUM_PLAYERS = 2
MAX_BOARD_CARDS = 5
ROUND_ID_BY_BOARD_SIZE = {3: 1, 4: 2}
RANKS = "23456789TJQKA"
SUITS = "cdhs"


def _card_id_to_string(card: int) -> str:
    if card < 0 or card >= 52:
        raise ValueError("card id must be in [0, 51]")
    return RANKS[card // 4] + SUITS[card % 4]


def _board_to_string(board: np.ndarray) -> str:
    return "".join(_card_id_to_string(int(card)) for card in board)


def _board_and_size(
    data: np.lib.npyio.NpzFile, sample_index: int
) -> tuple[np.ndarray, int]:
    board = np.asarray(data["board"][sample_index], dtype=np.uint8)
    if "board_size" in data.files:
        board_size = int(data["board_size"][sample_index])
        board = board[:board_size]
    else:
        board_size = int(board.shape[0])
    if board_size not in ROUND_ID_BY_BOARD_SIZE:
        raise ValueError("chance target board must be flop or turn")
    if len(set(int(card) for card in board)) != board_size:
        raise ValueError("board contains duplicate cards")
    return board, board_size


def _round_id(
    data: np.lib.npyio.NpzFile, sample_index: int, board_size: int
) -> int:
    inferred = ROUND_ID_BY_BOARD_SIZE[board_size]
    if "round" not in data.files:
        return inferred
    value = data["round"][sample_index]
    if isinstance(value, np.bytes_):
        value = value.decode("utf-8")
    if isinstance(value, str):
        by_name = {"flop": 1, "turn": 2}
        if value not in by_name:
            raise ValueError("round must be flop or turn")
        parsed = by_name[value]
    else:
        parsed = int(value)
    if parsed != inferred:
        raise ValueError("round does not match board size")
    return parsed


def _abstracted_bets_config() -> dict[str, Any]:
    bets = [["50%", "100%"]]
    donk_bets = ["50%", "100%"]
    return {
        "flop_bets": bets,
        "turn_bets": bets,
        "river_bets": bets,
        "flop_donk_bets": donk_bets,
        "turn_donk_bets": donk_bets,
        "river_donk_bets": donk_bets,
    }


def _build_spot_config(
    board: np.ndarray,
    pot: float,
    stacks: np.ndarray,
    rake_ratio: float,
    rake_cap: float,
    reach: np.ndarray,
) -> dict[str, Any]:
    if reach.shape != (NUM_PLAYERS, NUM_HANDS):
        raise ValueError("reach must have shape [2, 1326]")
    return {
        "board_cards": _board_to_string(board),
        "common_pot": float(pot),
        "stacks": [float(stacks[0]), float(stacks[1])],
        "bet_total": [0.0, 0.0],
        "bet_current_round": [0.0, 0.0],
        "current_player": -1,
        "previous_street_aggressor": -1,
        "raise_count": 0,
        "beliefs": reach.astype(np.float32, copy=False).tolist(),
        "min_bet_increment": 0.1,
        "rake": {
            "enabled": True,
            "percentage": float(rake_ratio),
            "cap": float(rake_cap),
        },
        "abstracted_bets": _abstracted_bets_config(),
    }


def _expand_iso_cfv_to_raw(board: np.ndarray, iso_cfv: np.ndarray) -> np.ndarray:
    raw_to_iso = np.asarray(
        raw_to_iso_indices([int(card) for card in board]), dtype=np.int32
    )
    if raw_to_iso.shape != (NUM_HANDS,):
        raise RuntimeError("raw_to_iso_indices returned an unexpected shape")
    max_iso = int(raw_to_iso.max()) if np.any(raw_to_iso >= 0) else -1
    if iso_cfv.shape != (max_iso + 1,):
        raise RuntimeError(
            "root CFV shape does not match the full board isomorphic mapping; "
            "chance target generation currently expects dense legal ranges"
        )
    raw_cfv = np.zeros(NUM_HANDS, dtype=np.float32)
    valid = raw_to_iso >= 0
    raw_cfv[valid] = iso_cfv[raw_to_iso[valid]]
    return raw_cfv


def _solve_one_sample(
    *,
    board: np.ndarray,
    pot: float,
    stacks: np.ndarray,
    rake_ratio: float,
    rake_cap: float,
    reach: np.ndarray,
    max_iterations: int,
    exploitability_check_interval: int,
    target_exploitability_ratio: float,
    num_threads: int,
) -> tuple[np.ndarray, float, int, bool, float]:
    spot_config = _build_spot_config(
        board=board,
        pot=pot,
        stacks=stacks,
        rake_ratio=rake_ratio,
        rake_cap=rake_cap,
        reach=reach,
    )
    solver_config = {
        "max_iterations": int(max_iterations),
        "exploitability_check_interval": int(exploitability_check_interval),
        "target_exploitability": float(pot) * float(target_exploitability_ratio),
        "num_threads": int(num_threads),
    }

    begin = time.perf_counter()
    session = solve_poker(spot_config, solver_config)
    solve_ms = (time.perf_counter() - begin) * 1000.0
    metadata = session.metadata()

    target_cfv = np.zeros((NUM_PLAYERS, NUM_HANDS), dtype=np.float32)
    for player in range(NUM_PLAYERS):
        iso_cfv = np.asarray(session.node_cfv(0, player)["cfv"], dtype=np.float32)
        target_cfv[player] = _expand_iso_cfv_to_raw(board, iso_cfv)

    return (
        target_cfv,
        float(metadata["exploitability"]),
        int(metadata["iterations"]),
        bool(metadata["converged"]),
        float(solve_ms),
    )


class _OutputBuffer:
    def __init__(self, output_dir: Path, num_sample_per_file: int) -> None:
        self.output_dir = output_dir
        self.num_sample_per_file = num_sample_per_file
        self.file_index = 0
        self.clear()

    def clear(self) -> None:
        self.pot: list[float] = []
        self.stacks: list[np.ndarray] = []
        self.rake_ratio: list[float] = []
        self.rake_cap: list[float] = []
        self.board: list[np.ndarray] = []
        self.board_size: list[int] = []
        self.round_id: list[int] = []
        self.reach: list[np.ndarray] = []
        self.target_cfv: list[np.ndarray] = []
        self.exploitability: list[float] = []
        self.iterations: list[int] = []
        self.converged: list[bool] = []
        self.solve_ms: list[float] = []

    def __len__(self) -> int:
        return len(self.pot)

    def append(
        self,
        *,
        pot: float,
        stacks: np.ndarray,
        rake_ratio: float,
        rake_cap: float,
        board: np.ndarray,
        board_size: int,
        round_id: int,
        reach: np.ndarray,
        target_cfv: np.ndarray,
        exploitability: float,
        iterations: int,
        converged: bool,
        solve_ms: float,
    ) -> None:
        padded_board = np.full(MAX_BOARD_CARDS, 255, dtype=np.uint8)
        padded_board[:board_size] = board
        self.pot.append(float(pot))
        self.stacks.append(stacks.astype(np.float32, copy=False))
        self.rake_ratio.append(float(rake_ratio))
        self.rake_cap.append(float(rake_cap))
        self.board.append(padded_board)
        self.board_size.append(board_size)
        self.round_id.append(round_id)
        self.reach.append(reach.astype(np.float32, copy=False))
        self.target_cfv.append(target_cfv.astype(np.float32, copy=False))
        self.exploitability.append(float(exploitability))
        self.iterations.append(int(iterations))
        self.converged.append(bool(converged))
        self.solve_ms.append(float(solve_ms))

    def should_flush(self) -> bool:
        return len(self) >= self.num_sample_per_file

    def flush(self) -> Path | None:
        if len(self) == 0:
            return None
        self.output_dir.mkdir(parents=True, exist_ok=True)
        output_path = self.output_dir / f"chance_targets_{self.file_index:06d}.npz"
        np.savez_compressed(
            output_path,
            pot=np.asarray(self.pot, dtype=np.float32),
            stacks=np.asarray(self.stacks, dtype=np.float32),
            rake_ratio=np.asarray(self.rake_ratio, dtype=np.float32),
            rake_cap=np.asarray(self.rake_cap, dtype=np.float32),
            board=np.asarray(self.board, dtype=np.uint8),
            board_size=np.asarray(self.board_size, dtype=np.uint8),
            round=np.asarray(self.round_id, dtype=np.uint8),
            reach=np.asarray(self.reach, dtype=np.float32),
            target_cfv=np.asarray(self.target_cfv, dtype=np.float32),
            exploitability=np.asarray(self.exploitability, dtype=np.float32),
            iterations=np.asarray(self.iterations, dtype=np.int32),
            converged=np.asarray(self.converged, dtype=np.bool_),
            solve_ms=np.asarray(self.solve_ms, dtype=np.float32),
        )
        self.file_index += 1
        self.clear()
        return output_path


def generate_chance_targets(
    *,
    input_dir: Path,
    output_dir: Path,
    num_sample_per_file: int,
    max_iterations: int,
    exploitability_check_interval: int,
    target_exploitability_ratio: float,
    num_threads: int,
    max_samples: int | None = None,
) -> list[Path]:
    if num_sample_per_file <= 0:
        raise ValueError("num_sample_per_file must be positive")
    if max_iterations <= 0:
        raise ValueError("max_iterations must be positive")
    if exploitability_check_interval <= 0:
        raise ValueError("exploitability_check_interval must be positive")
    if target_exploitability_ratio < 0.0:
        raise ValueError("target_exploitability_ratio cannot be negative")

    input_files = sorted(input_dir.glob("*.npz"))
    if not input_files:
        raise ValueError("input_dir contains no npz files")

    output_files: list[Path] = []
    output = _OutputBuffer(output_dir, num_sample_per_file)
    generated = 0

    for input_file in input_files:
        with np.load(input_file) as data:
            count = int(data["pot"].shape[0])
            for sample_index in range(count):
                if max_samples is not None and generated >= max_samples:
                    break
                board, board_size = _board_and_size(data, sample_index)
                round_id = _round_id(data, sample_index, board_size)
                reach = np.asarray(data["reach"][sample_index], dtype=np.float32)
                target_cfv, exploitability, iterations, converged, solve_ms = (
                    _solve_one_sample(
                        board=board,
                        pot=float(data["pot"][sample_index]),
                        stacks=np.asarray(
                            data["stacks"][sample_index], dtype=np.float32
                        ),
                        rake_ratio=float(data["rake_ratio"][sample_index]),
                        rake_cap=float(data["rake_cap"][sample_index]),
                        reach=reach,
                        max_iterations=max_iterations,
                        exploitability_check_interval=exploitability_check_interval,
                        target_exploitability_ratio=target_exploitability_ratio,
                        num_threads=num_threads,
                    )
                )
                output.append(
                    pot=float(data["pot"][sample_index]),
                    stacks=np.asarray(data["stacks"][sample_index], dtype=np.float32),
                    rake_ratio=float(data["rake_ratio"][sample_index]),
                    rake_cap=float(data["rake_cap"][sample_index]),
                    board=board,
                    board_size=board_size,
                    round_id=round_id,
                    reach=reach,
                    target_cfv=target_cfv,
                    exploitability=exploitability,
                    iterations=iterations,
                    converged=converged,
                    solve_ms=solve_ms,
                )
                generated += 1
                if output.should_flush():
                    flushed = output.flush()
                    if flushed is not None:
                        output_files.append(flushed)
            if max_samples is not None and generated >= max_samples:
                break

    flushed = output.flush()
    if flushed is not None:
        output_files.append(flushed)
    if generated == 0:
        raise ValueError("no samples were generated")
    return output_files


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Solve chance-root postflop spots and write root CFV targets."
    )
    parser.add_argument("--input-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--num-sample-per-file", type=int, required=True)
    parser.add_argument("--max-iterations", type=int, default=500)
    parser.add_argument("--exploitability-check-interval", type=int, default=50)
    parser.add_argument("--target-exploitability-ratio", type=float, default=0.001)
    parser.add_argument("--num-threads", type=int, default=0)
    parser.add_argument("--max-samples", type=int, default=None)
    return parser.parse_args()


def main() -> None:
    args = _parse_args()
    begin = time.perf_counter()
    output_files = generate_chance_targets(
        input_dir=args.input_dir,
        output_dir=args.output_dir,
        num_sample_per_file=args.num_sample_per_file,
        max_iterations=args.max_iterations,
        exploitability_check_interval=args.exploitability_check_interval,
        target_exploitability_ratio=args.target_exploitability_ratio,
        num_threads=args.num_threads,
        max_samples=args.max_samples,
    )
    elapsed = time.perf_counter() - begin
    total_bytes = sum(path.stat().st_size for path in output_files)
    print(
        "generated "
        f"num_files={len(output_files)} "
        f"total_bytes={total_bytes} "
        f"elapsed_sec={elapsed:.3f}"
    )


if __name__ == "__main__":
    main()
