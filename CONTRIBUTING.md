# Development Guidelines

## C++ Style

Use common Google C++ Style conventions unless a file has a stronger local
pattern.

- Use C++17.
- Keep public declarations in `csrc/include`, implementations in `csrc/src`,
  and C++ tests in `csrc/tests`.
- Keep pybind code under `csrc/src/binding`; do not mix binding code into game
  or algorithm implementations.
- Use namespaces rooted at `fisher`.
- Use `PascalCase` for types and functions, `snake_case` for local variables,
  and `kConstantName` for constants.
- Prefer small value types and explicit data ownership.
- Prefer standard library containers and algorithms.
- Validate inputs at API boundaries and throw `std::invalid_argument` for
  invalid caller configuration.
- Avoid hidden global mutable state.
- Keep comments focused on non-obvious decisions, invariants, or algorithms.

## Python Style

Use PEP 8 conventions.

- Keep Python code under `pysrc`.
- Keep Python thin: configuration, pybind re-exports, and pytest benchmarks.
- Put Python tests under `pysrc/tests`.
- Use clear `snake_case` names.
- Prefer simple assertions in tests and avoid duplicating C++ game logic in
  Python.

## Build And Test

- Use `uv` for Python environment management.
- Use CMake for direct C++ builds.
- Do not commit build artifacts such as `.so`, `*.egg-info`, `build/`, or
  `dist/`.
- Do not clean local build artifacts at the end of normal work; leave ignored
  build outputs in place so later builds can reuse them.
- Before handing off C++/binding changes, run:

```bash
UV_CACHE_DIR=.uv-cache uv run pytest
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DFISHER_BUILD_PYBIND=OFF
cmake --build build
ctest --test-dir build --output-on-failure
```
