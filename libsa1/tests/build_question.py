#!/usr/bin/env python3
"""Build and run tests/question_test.c (round 0.9.9, 099-lib-playable: the question mark, SA1_API_QUESTION; sadc docs/research/gsa1_099_plan.md 5.10, 6.3 task B, playable099.md 7.3)
against a snapshot of build/<target>/libsa1.a and its headers - the same options and rules as tests/build_board.py (whose code it uses):

    py -3.12 tests/build_question.py [--target x86_64-windows|x86-windows|x86_64-linux|windows|all] [--run] [--gdi GDI]
                                     [--debug] [--no-snapshot] [--timeout S]

Until src/sa1_character.c's gate SA1Q_BUILT is 1 the optimised archives answer "the question mark is not built yet": run the -O0 build (py -3.12 build.py --debug --target x86_64-windows; here
--debug). build.py --test does not run it (gsa1_099_plan.md 2.4 C7): each lane and the verifier run it by name. The "DETERMINISM" lines of the two Windows targets must be equal.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import build_board  # noqa: E402

if __name__ == "__main__":
    sys.exit(build_board.main("question_test"))
