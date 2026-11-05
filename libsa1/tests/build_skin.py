#!/usr/bin/env python3
"""Build and run tests/skin_test.c (round 0.9.9f, a worn skin: Metal Sonic over Sonic's world, SA1_API_SKIN; sadc docs/research/metal099e.md section 7) against a snapshot of
build/<target>/libsa1.a and its headers - the same options and rules as tests/build_board.py (whose code it uses):

    py -3.12 tests/build_skin.py [--target x86_64-windows|x86-windows|x86_64-linux|windows|all] [--run] [--gdi GDI] [--debug] [--no-snapshot] [--timeout S]

The "DETERMINISM" lines of the two Windows targets must be equal.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import build_board  # noqa: E402

if __name__ == "__main__":
    sys.exit(build_board.main("skin_test"))
