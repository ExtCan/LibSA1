#!/usr/bin/env python3
"""Build and run tests/follower_test.c (round 0.9.8, 098-lib-follow: the follower API, include/libsa1.h "followers" SA1_API_FOLLOWER; sadc
docs/research/gsa1_098_plan.md 6.2, gsa1_096_plan.md 6.1 tasks 9, 10) against a snapshot of build/<target>/libsa1.a and its headers - the same
options and rules as tests/build_board.py (whose code it uses):

    py -3.12 tests/build_follower.py [--target x86_64-windows|x86-windows|x86_64-linux|windows|all] [--run] [--gdi GDI]
                                     [--debug] [--no-snapshot] [--timeout S] [-- --long] [-- --only CASE]

Until src/sa1_follower.c's gate SA1FO_BUILT is 1 the optimised archives answer "followers are not built yet": run the -O0 build
(py -3.12 build.py --debug --target x86_64-windows; here --debug). The 99-world case runs on x86_64-windows, 32 worlds on x86-windows (review
R.10). The "DETERMINISM" lines of the two Windows targets must be equal.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import build_board  # noqa: E402

if __name__ == "__main__":
    sys.exit(build_board.main("follower_test"))
