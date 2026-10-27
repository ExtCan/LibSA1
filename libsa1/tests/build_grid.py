#!/usr/bin/env python3
"""Build and run tests/grid_test.c (GRID collision, moving objects, kill planes, surfaces: the Phase 6 "synthetic
suite" of sadc docs/libsa1_integration.md 5.4-5.8, plus GRID on Emerald Coast act 1 against the reference traces)
against build/<target>/libsa1.a.

    py -3.12 tests/build_grid.py [--target x86_64-windows|x86-windows|x86_64-linux|all] [--run]
                                 [--sadc DIR] [--gdi FILE] [--only NAME]... [--no-ec1] [--verbose]

Run it from LIB after `py -3.12 build.py --target <target>` (the archive must hold the whole lowered game). The test
is compiled with the world tests' flags (as tests/build_replay.py: -DSA1_STATIC, -I<sadc>/harness/src for
sa1_trace.h, the world core's include paths). --run (runnable targets) runs build/<target>/grid_test SADC GDI with the
user's GD-ROM dump (--gdi, default $SA1_TEST_GDI; never copied). The EC1 part reads the reference traces
<sadc>/build/traces/ref/ec1_*.bin and the landtable dump <sadc>/build/traces/land/L01A0 (.tri/.col).
"""
import argparse
import os
import platform
import subprocess
import sys

LIB = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TARGETS = {"x86_64-windows": "x86_64-windows-gnu", "x86-windows": "x86-windows-gnu", "x86_64-linux": "x86_64-linux-gnu"}


def runnable(key):
    if key.endswith("windows"):
        return platform.system() == "Windows"
    return platform.system() == "Linux" and platform.machine().lower() in ("x86_64", "amd64")


def build(key, sadc):
    triple = TARGETS[key]
    out = os.path.join("build", key)
    lib = os.path.join(out, "libsa1.a")
    if not os.path.exists(os.path.join(LIB, lib)):
        raise SystemExit("%s missing: run py -3.12 build.py --target %s first" % (lib, key))
    flags = ["-target", triple, "-std=gnu11", "-Iinclude", "-Isrc", "-Isrc/world", "-I" + os.path.join(sadc, "include"),
             "-I" + os.path.join(sadc, "harness", "src"), "-Wall", "-Wextra", "-Wshadow", "-Wstrict-prototypes",
             "-Wundef", "-ffp-contract=off", "-frounding-math", "-fno-builtin", "-O2", "-g", "-DSA1_STATIC"]
    if key.startswith("x86-"):
        flags += ["-msse2", "-mfpmath=sse"]
    if not key.endswith("windows"):
        flags += ["-fPIC"]
    libs = [] if key.endswith("windows") else ["-lm", "-lpthread"]
    exe = os.path.join(out, "grid_test" + (".exe" if key.endswith("windows") else ""))
    cmd = [sys.executable, "-m", "ziglang", "cc", *flags, os.path.join("tests", "grid_test.c"), lib, "-o", exe, *libs]
    r = subprocess.run(cmd, cwd=LIB, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
    if r.stdout.strip():
        print(r.stdout.rstrip())
    if r.returncode != 0:
        raise SystemExit("FAILED: " + " ".join(cmd))
    print("[%s] built %s" % (key, exe))
    return exe


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--target", default="x86_64-windows", choices=sorted(TARGETS) + ["all"])
    ap.add_argument("--sadc", default=os.path.join(os.path.dirname(LIB), "sadc"))
    ap.add_argument("--gdi", default=os.environ.get("SA1_TEST_GDI", ""))
    ap.add_argument("--run", action="store_true")
    ap.add_argument("--only", action="append", default=[], help="run only this test (repeatable)")
    ap.add_argument("--no-ec1", action="store_true", help="skip GRID on Emerald Coast act 1")
    ap.add_argument("--verbose", action="store_true")
    a = ap.parse_args()
    keys = sorted(TARGETS) if a.target == "all" else [a.target]
    rc = 0
    for k in keys:
        exe = build(k, a.sadc)
        if a.run and runnable(k):
            if not a.gdi:
                raise SystemExit("--run needs --gdi or SA1_TEST_GDI")
            extra = []
            for o in a.only:
                extra += ["--only", o]
            if a.no_ec1:
                extra.append("--no-ec1")
            if a.verbose:
                extra.append("--verbose")
            r = subprocess.run([os.path.join(LIB, exe)] + extra + [a.sadc, a.gdi], cwd=LIB)
            print("[%s] grid_test exit %d" % (k, r.returncode))
            rc |= r.returncode != 0
    sys.exit(1 if rc else 0)


main()
