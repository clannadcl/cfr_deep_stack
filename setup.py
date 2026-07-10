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


class BuildExt(build_ext):
    def build_extensions(self) -> None:
        original_compile = self.compiler._compile

        def compile_without_cxx_std_for_c(
            obj, src, ext, cc_args, extra_postargs, pp_opts
        ):
            if src.endswith(".c") and isinstance(extra_postargs, list):
                extra_postargs = [
                    arg
                    for arg in extra_postargs
                    if not arg.startswith("-std=c++")
                ]
            return original_compile(
                obj, src, ext, cc_args, extra_postargs, pp_opts
            )

        self.compiler._compile = compile_without_cxx_std_for_c
        try:
            super().build_extensions()
        finally:
            self.compiler._compile = original_compile


ext_modules = [
    Pybind11Extension(
        "fisher._core",
        [
            "csrc/src/binding/pybind.cc",
            "csrc/src/algorithm/cfr.cc",
            "csrc/src/game/kuhn.cc",
            "csrc/src/game/poker/abstracted_action.cc",
            "csrc/src/game/poker/action.cc",
            "csrc/src/game/poker/belief.cc",
            "csrc/src/game/poker/game_basic.cc",
            "csrc/src/game/poker/poker_cards_isomorphic_index.cc",
            "csrc/src/game/poker/poker_card.cc",
            "csrc/src/game/poker/poker_cards.cc",
            "csrc/src/game/poker/poker_hand.cc",
            "third_party/hand-isomorphism/src/deck.c",
            "third_party/hand-isomorphism/src/hand_index.c",
        ],
        include_dirs=["csrc/include", "third_party/hand-isomorphism/src"],
        cxx_std=17,
        extra_compile_args=["-O3"],
    ),
]

setup(
    package_dir={"": "pysrc"},
    packages=find_packages("pysrc"),
    ext_modules=ext_modules,
    cmdclass={"build_ext": BuildExt},
)
