#!/usr/bin/env python3
"""Build and run tests/camera_test.c (round 0.9.5, 095-lib-board: the host camera, sa1_input.host_cam_pos / host_cam_target
and SA1_INPUT_HOST_CAMERA; sadc docs/research/board095_cam.md 6.2 C1-C9, gsa1_095_plan.md 6.2 task 7) against a snapshot of
build/<target>/libsa1.a and its headers - the same options and rules as tests/build_board.py (whose code it uses):

    py -3.12 tests/build_camera.py [--target x86_64-windows|x86-windows|x86_64-linux|windows|all] [--run] [--gdi GDI]
                                   [--debug] [--no-snapshot] [--timeout S] [-- ARGS]
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import build_board  # noqa: E402

if __name__ == "__main__":
    sys.exit(build_board.main("camera_test"))
