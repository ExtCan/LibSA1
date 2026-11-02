#!/usr/bin/env python3
"""Build and run tests/frontend_test.c (round 0.9.8, 098-lib-frontend: the game's own front end, include/libsa1.h "the game's own
front end" SA1_API_FRONTEND; sadc docs/research/gsa1_098_plan.md 6.5, charsel098.md 7) against a SNAPSHOT of build/<target>/libsa1.a
and its headers, with the code of tests/build_board.py.

    py -3.12 tests/build_frontend.py [--target x86_64-windows|x86-windows|x86_64-linux|windows|all] [--run] [--gdi GDI]
                                     [--sadc DIR] [--debug] [--no-snapshot] [--timeout S] [-- ARGS]

--debug takes build/<target>-debug/libsa1.a (build.py --debug: the -O0 build, whose gate is open before the lane's final gate). --run
runs the test with GDI (default $SA1_TEST_GDI) and ARGS (frontend_test --list: the cases; --only NAME[,NAME]; --sadc DIR: where
build/traces/ref is; default ../sadc). A reference that is missing is a FAIL. When more than one target ran, their "DETERMINISM" lines
must be equal (x86 = x64).
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import build_board  # noqa: E402

if __name__ == "__main__":
    sys.exit(build_board.main("frontend_test"))
