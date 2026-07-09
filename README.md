# fisher

Small C++/pybind benchmark for CFR on Kuhn poker.

The C++ backend owns the game tree, CFR solver, policy-value evaluation, and
Nash exploitability calculation. Python is only a thin configuration and pytest
benchmark layer.

Development conventions are documented in `CONTRIBUTING.md`.

## Layout

- `csrc/include`: C++ public headers.
- `csrc/src/game`: game implementations, currently Kuhn poker.
- `csrc/src/algorithm`: CFR implementation and exploitability utilities.
- `csrc/src/binding`: pybind glue.
- `csrc/tests`: C++ unit tests.
- `pysrc/fisher`: Python package that re-exports the pybind
  API.
- `pysrc/tests`: pytest benchmark/convergence checks.
- `third_party/open_spiel_subset`: copied OpenSpiel reference code.

## API

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

## Run

```bash
uv sync --group dev
uv run pytest
```

## C++ Build

The CMake build defines these targets:

- `fisher_core`: static library for game and CFR algorithm code.
- `kuhn_cfr_test`: C++ convergence smoke test.
- `_core`: optional pybind11 extension emitted to `pysrc/fisher`.

Build only the C++ core and unittest:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DFISHER_BUILD_PYBIND=OFF
cmake --build build
ctest --test-dir build --output-on-failure
```

To build the pybind extension with the active uv environment:

```bash
PYBIND11_CMAKE_DIR="$(uv run python -m pybind11 --cmakedir)"
cmake -S . -B build-pybind \
  -DCMAKE_BUILD_TYPE=Release \
  -DPython_EXECUTABLE="$PWD/.venv/bin/python" \
  -DCMAKE_PREFIX_PATH="$PYBIND11_CMAKE_DIR"
cmake --build build-pybind
```

Useful options:

- `-DFISHER_BUILD_PYBIND=ON|OFF`: build the Python extension. Defaults to `ON`.
- `-DFISHER_BUILD_TESTS=ON|OFF`: build C++ tests. Defaults to `ON`.
- `-DCMAKE_BUILD_TYPE=Release|Debug`: choose optimization/debug mode.
- `-DPython_EXECUTABLE=/path/to/python`: select the Python ABI for `_core`.
- `-DCMAKE_PREFIX_PATH=/path/to/pybind11/cmake`: help CMake find pybind11.

The pybind build writes:

```text
pysrc/fisher/_core.*.so
```

That file is a build artifact and is ignored by git.

On this machine, the extension build prefers Homebrew LLVM:

```text
/opt/homebrew/opt/llvm/bin/clang++
```
