#!/usr/bin/env python3
"""Build and run tests/look_test.c (round 0.9.8, 098-lib-lookcam: the head look, SA1_API_LOOK; sadc docs/research/gsa1_098_plan.md 6.3
part 2, look096.md 8) against a snapshot of build/<target>/libsa1.a and its headers - the same options and rules as tests/build_board.py
(whose code it uses):

    py -3.12 tests/build_look.py [--target x86_64-windows|x86-windows|x86_64-linux|windows|all] [--run] [--gdi GDI]
                                 [--debug] [--no-snapshot] [--timeout S] [-- ARGS]
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import build_board  # noqa: E402

if __name__ == "__main__":
    sys.exit(build_board.main("look_test"))
