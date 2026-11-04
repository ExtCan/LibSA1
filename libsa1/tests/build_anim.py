#!/usr/bin/env python3
"""Build and run tests/anim_test.c (round 0.9.9a, 099a-anim: a class check of what the character render draws, every catalogue entry of every kind, entering and leaving; sadc
build/research/r099a/anim/diagnosis.md) against a snapshot of build/<target>/libsa1.a and its headers - the same options and rules as tests/build_board.py (whose code it uses):

    py -3.12 tests/build_anim.py [--target x86_64-windows|x86-windows|x86_64-linux|windows|all] [--run] [--gdi GDI] [--debug] [--no-snapshot] [--timeout S] [-- ARGS]

build.py --test does not run it: each lane and the verifier run it by name. The "DETERMINISM" lines of the two Windows targets must be equal.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import build_board  # noqa: E402

if __name__ == "__main__":
    sys.exit(build_board.main("anim_test"))
