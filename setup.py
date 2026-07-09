from __future__ import annotations

import os
from pathlib import Path

from pybind11.setup_helpers import Pybind11Extension, build_ext
from setuptools import find_packages, setup


def _clangxx() -> str | None:
    llvm_clang = Path("/opt/homebrew/opt/llvm/bin/clang++")
    if llvm_clang.exists():
        return str(llvm_clang)
    return None


if "CXX" not in os.environ:
    clangxx = _clangxx()
    if clangxx:
        os.environ["CXX"] = clangxx


ext_modules = [
    Pybind11Extension(
        "fisher._core",
        [
            "csrc/src/binding/pybind.cc",
            "csrc/src/algorithm/cfr.cc",
            "csrc/src/game/kuhn.cc",
            "csrc/src/game/poker/poker_card.cc",
            "csrc/src/game/poker/poker_cards.cc",
        ],
        include_dirs=["csrc/include"],
        cxx_std=17,
        extra_compile_args=["-O3"],
    ),
]

setup(
    package_dir={"": "pysrc"},
    packages=find_packages("pysrc"),
    ext_modules=ext_modules,
    cmdclass={"build_ext": build_ext},
)
