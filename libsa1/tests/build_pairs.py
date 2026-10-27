#!/usr/bin/env python3
"""Build (and --run) tests/pairs_test.c: tests/build_render_tests.py pairs [options] (see there)."""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import build_render_tests  # noqa: E402

build_render_tests.main("pairs")
