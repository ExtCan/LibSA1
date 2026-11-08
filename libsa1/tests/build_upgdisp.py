#!/usr/bin/env python3
"""Build and run tests/upgdisp_test.c (round 1.0.0-beta1, lane r1-s1: the game option SA1_GOPT_UPGRADE_MODELS, the display set of the upgrades;
sadc docs/research/gsa1_100_select.md 7.1 U1-U6) against a snapshot of build/<target>/libsa1.a and its headers, with the code of tests/build_board.py:

    py -3.12 tests/build_upgdisp.py [--target x86_64-windows|x86-windows|x86_64-linux|windows|all] [--run] [--gdi GDI] [--debug] [--no-snapshot] [--timeout S]

The "DETERMINISM" lines of the targets run must be equal (x86 = x64).
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import build_board  # noqa: E402

if __name__ == "__main__":
    sys.exit(build_board.main("upgdisp_test"))