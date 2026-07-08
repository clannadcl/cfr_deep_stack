# cfr_deep_stack

This repository is a focused poker-solver workbench seeded from OpenSpiel.

The first runnable baseline is intentionally small:

- tabular CFR / CFR+ style average-policy training
- a pure-Python Kuhn poker game with an OpenSpiel-like state API
- brute-force exploitability checks for small poker games
- vendored OpenSpiel CFR and poker source references under `third_party/`

The next intended step is to add Leduc poker, then connect a DeepStack-style
public-state value network and depth-limited continual re-solving loop.

## Layout

- `cfr_deep_stack/algorithms`: local solver code used by tests.
- `cfr_deep_stack/games`: local poker game implementations.
- `third_party/open_spiel_subset`: copied OpenSpiel source files relevant to
  CFR, MCCFR, Deep CFR, Kuhn, Leduc, and Universal Poker.
- `tests`: smoke and convergence tests for the local baseline.

## Quick Start

```bash
uv sync --group dev
uv run pytest
```

To install the optional neural-network stack later:

```bash
uv sync --group dev --extra deep
```

The local baseline does not require `pyspiel`. The copied OpenSpiel files in
`third_party/open_spiel_subset` still depend on OpenSpiel's C++/pybind runtime
and are kept as upstream reference code for later porting.
