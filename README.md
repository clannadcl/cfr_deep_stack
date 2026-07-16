# fisher

`fisher` is a C++ poker solver workbench.  The current codebase contains a
Kuhn CFR pybind benchmark and the early Hold'em subgame solver stack:
cards/ranges, public tree construction, isomorphic hand-space mapping, terminal
CFV calculation, and a first vanilla CFR traversal.

The long-term direction is a CFR-average + DeepStack-style postflop solver.  The
current implementation is deliberately modular: Python stays as a thin
configuration/test layer, while poker game logic and solver hot paths live in
C++.

Development conventions are documented in `CONTRIBUTING.md`.

## Layout

- `csrc/include`: C++ public headers.
- `csrc/src/algorithm`: Kuhn CFR, CFR storage, data propagation, and poker CFR
  traversal.
- `csrc/src/game/kuhn.cc`: compact Kuhn benchmark game.
- `csrc/src/game/poker`: Hold'em poker primitives and subgame infrastructure.
- `csrc/src/binding`: pybind glue for the current Python API.
- `csrc/tests`: C++ unit and regression tests.
- `pysrc/fisher`: Python package that re-exports the pybind API.
- `pysrc/tests`: pytest smoke/convergence checks.
- `third_party/hand-isomorphism`: Kevin Waugh hand indexer used for suit
  isomorphism.
- `third_party/open_spiel_subset`: copied OpenSpiel reference code, not the
  active solver runtime.

## Poker Module Design

The Hold'em solver is split into small layers:

- `PokerCard`, `PokerCards`, `PokerHand`: card, card-set, and two-card hand
  value types.  Cards use ids `0..51` in `2c,2d,2h,2s,...,As` order.
- `GameBasic`: static game rules and indexes: deck, 1326 hand catalog, hand
  index lookup, board/hand validation, rake config, and isomorphic indexers.
- `IsomorphicMapping`: per-board mapping between 1326 raw hands and compact
  suit-isomorphic hand buckets.  Reach is stored as bucket total mass; strategy,
  regret, and CFV are stored as per-hand values shared inside each bucket.
- `SubgameSetup`: root state plus whole-subgame rules: board, pot, stacks,
  invested bets, action history, root belief, action abstraction, and numeric
  betting rules.
- `NodeState`: one public game state.  It owns board/pot/stack/bet state,
  terminal status, terminal payoff, legal concrete actions, and state
  transitions.
- `PokerTree`: BFS-expanded public tree.  Node ids are stable and children are
  stored by `children_offset + num_children`, matching valid action order.
- `TerminalCfvCalculator`: terminal leaf value calculator for fold, river
  showdown/all-in, and flop/turn all-in.  River showdown uses a per-board cache;
  flop/turn all-in uses `TerminalWinProbMatrix`.
- `CfrStorage`: flat storage for strategy, regret, CFV, reach, and accumulated
  average-strategy sums.
- `PokerCfrSolver`: first vanilla CFR traversal over a `PokerTree`.

## CFR Traversal

The first poker CFR implementation is vanilla CFR with a two-player sweep:

1. `RunIteration()` runs `RunHeroPass(0)` and then `RunHeroPass(1)`.
2. Each pass initializes root reach from unnormalized root belief.
3. Reach is propagated forward:
   - Player node: actor reach is split by `strategy[action, hand]`; non-actor
     reach is unchanged/shared.
   - Chance node: both players use a precomputed sparse iso transition.  Reach
     is not multiplied by chance probability.
4. Average strategy is accumulated only for the current hero at hero-owned
   player nodes:
   `sum_strategy[action, hand] += reach[hero, hand] * strategy[action, hand]`.
5. Terminal node CFVs are calculated for both players.
6. CFV is propagated backward in reverse node order:
   - Actor CFV is strategy-weighted over child actions.
   - Non-actor CFV is a plain sum over child CFVs because actor reach was
     already split during the forward pass.
   - Chance CFV uses the sparse transition in reverse and multiplies
     `chance_prob = 1 / (52 - board_size - 4)`.
7. Only the current hero's actor nodes update regret and immediately refresh
   strategy by regret matching.

Average strategy export applies epsilon smoothing only at read time:

```text
avg[action, hand] =
  (sum_strategy[action, hand] + epsilon) /
  sum_actions(sum_strategy[action, hand] + epsilon)
```

The default epsilon is `1e-12`.

## Performance Notes

- OpenBLAS is used for terminal matrix-vector kernels when available.  The
  OpenBLAS backend is forced to one thread in code so future solver-level
  parallelism does not oversubscribe the CPU.
- `TerminalWinProbMatrix` caches flop/turn all-in equity deltas by board in the
  terminal calculator.
- River terminal evaluation caches each board's sorted hand-strength groups.
- Chance transitions are precomputed once as sparse `(parent_iso, child_iso,
  weight)` edges so each iteration avoids scanning all 1326 raw hands.

## Python API

The current Python API still exposes the compact Kuhn benchmark:

```python
from fisher import (
    build_game,
    solve,
    policy_value,
    nash_exploitability,
)

game_config = {"game": "kuhn_poker"}
result = solve(game_config, {"iterations": 1000, "cfr_plus": False})

strategy = result["strategy"]
values = policy_value(game_config, strategy)
exploitability = nash_exploitability(game_config, strategy)
```

The Hold'em CFR stack is currently C++-only and tested through C++ unit tests.

## Build

Install Python/dev dependencies:

```bash
uv sync --group dev
```

Build C++ core and tests:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DFISHER_BUILD_PYBIND=OFF
cmake --build build
ctest --test-dir build --output-on-failure
```

Build release core:

```bash
cmake -S . -B build-release \
  -DCMAKE_BUILD_TYPE=Release \
  -DFISHER_BUILD_PYBIND=OFF
cmake --build build-release
```

Build without OpenBLAS:

```bash
cmake -S . -B build-no-openblas \
  -DCMAKE_BUILD_TYPE=Release \
  -DFISHER_BUILD_PYBIND=OFF \
  -DFISHER_USE_OPENBLAS=OFF
cmake --build build-no-openblas
```

Build the pybind extension with the active uv environment:

```bash
PYBIND11_CMAKE_DIR="$(uv run python -m pybind11 --cmakedir)"
cmake -S . -B build-pybind \
  -DCMAKE_BUILD_TYPE=Release \
  -DPython_EXECUTABLE="$PWD/.venv/bin/python" \
  -DCMAKE_PREFIX_PATH="$PYBIND11_CMAKE_DIR"
cmake --build build-pybind
```

The pybind build writes:

```text
pysrc/fisher/_core.*.so
```

That file is a build artifact and is ignored by git.

## CMake Options

- `-DFISHER_BUILD_PYBIND=ON|OFF`: build the Python extension. Defaults to `ON`.
- `-DFISHER_BUILD_TESTS=ON|OFF`: build C++ tests. Defaults to `ON`.
- `-DFISHER_USE_OPENBLAS=ON|OFF`: use OpenBLAS CBLAS for hot matrix-vector
  kernels. Defaults to `ON` and falls back to the built-in loop if OpenBLAS is
  unavailable.
- `-DCMAKE_BUILD_TYPE=Release|Debug`: choose optimization/debug mode.
- `-DPython_EXECUTABLE=/path/to/python`: select the Python ABI for `_core`.
- `-DCMAKE_PREFIX_PATH=/path/to/pybind11/cmake`: help CMake find pybind11.

For OpenBLAS:

```bash
# macOS
brew install openblas

# Ubuntu/Debian
sudo apt install libopenblas-dev
```

On this machine, the extension build prefers Homebrew LLVM:

```text
/opt/homebrew/opt/llvm/bin/clang++
```

## Tests

Run all C++ tests:

```bash
ctest --test-dir build --output-on-failure
```

Run one C++ test directly:

```bash
cmake --build build --target poker_cfr_solver_test
./build/poker_cfr_solver_test
```

Run Python tests:

```bash
UV_CACHE_DIR=.uv-cache uv run pytest
```

Current notable C++ coverage:

- card/hand parsing and validation
- hand isomorphism and per-board iso mapping
- action abstraction and concrete action resolving
- subgame setup, node transitions, and poker tree topology
- terminal payoff/CFV and flop/turn terminal win probability matrices
- CFR storage layout, chance transition cache, and poker CFR regression tests
