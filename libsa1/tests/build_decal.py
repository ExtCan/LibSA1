#!/usr/bin/env python3
"""Build (and --run) tests/decal_test.c: tests/build_render_tests.py decal [options] (see there)."""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import build_render_tests  # noqa: E402

build_render_tests.main("decal")
