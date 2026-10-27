#!/usr/bin/env python3
"""Build (and optionally run) the world tests of libsa1 against build/<target>/libsa1.a.

    py -3.12 src/world/build_world_tests.py [--target x86_64-windows|x86-windows|x86_64-linux|all] [--run]
                                            [--sadc DIR] [--gdi FILE] [--out DIR] [--strict]

Run it from LIB after `py -3.12 build.py --target <target>` (the archive must hold the current world core and
the whole lowered game). Builds, with the flags build.py uses for spike_test (the world core's flags without
-Wmissing-prototypes, -DSA1_STATIC) plus -I<sadc>/harness/src for sa1_trace.h:

    build/<target>/transplant_test   M1a: RAM transplant (sadc docs/libsa1_integration.md 9.1 V4)
    build/<target>/world_boot_test   M0 / M1b: libsa1's own boot and player creation, live mode, character API

--run (runnable targets only) runs both on the five inputs below; --strict drops --stage-tolerant (the two
residuals of the STG01 water task 0C909FDA then fail the run, see the test headers):

    <sadc>/build/traces/ref/ec1_stand.bin, ec1_run_jump.bin           reference traces v2 (shared, committed in
                                                                       sadc's ignored build/)
    <sadc>/build/traces/m1_ec1_stand.step{0,1}.ram                     RAM images of a private pure harness build
    <sadc>/build/traces/m1_ec1_run_jump.step{0,1}.ram                  (generated locally, never committed):
        py -3.12 harness/build.py --files none --tag <tag>
        harness/build/sa1h_<tag>.exe trace --level 1 --act 0 --no-objects --input @harness/tests/sim/ec1_stand.txt
            --dump-ram-at 0,1 --out build/traces/m1_ec1_stand --check build/traces/ref/ec1_stand.bin
        (the same with ec1_run_jump)
    the user's GD-ROM dump (--gdi, default $SA1_TEST_GDI)
"""
import argparse
import os
import platform
import subprocess
import sys

LIB = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TARGETS = {"x86_64-windows": "x86_64-windows-gnu", "x86-windows": "x86-windows-gnu", "x86_64-linux": "x86_64-linux-gnu"}
TESTS = ["transplant_test", "world_boot_test"]


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
    exe = ".exe" if key.endswith("windows") else ""
    for t in TESTS:
        cmd = [sys.executable, "-m", "ziglang", "cc", *flags, os.path.join("tests", t + ".c"), lib, "-o",
               os.path.join(out, t + exe), *libs]
        r = subprocess.run(cmd, cwd=LIB, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
        if r.stdout.strip():
            print(r.stdout.rstrip())
        if r.returncode != 0:
            raise SystemExit("FAILED: " + " ".join(cmd))
        print("[%s] built %s" % (key, os.path.join(out, t + exe)))


def run(key, sadc, gdi, out_dir, strict):
    exe = ".exe" if key.endswith("windows") else ""
    tol = [] if strict else ["--stage-tolerant"]
    rc = 0
    od = []
    if out_dir:
        od = ["--out", os.path.join(out_dir, key)]
        os.makedirs(os.path.join(out_dir, key), exist_ok=True)
    runs = [[os.path.join(LIB, "build", key, "transplant_test" + exe)] + od + tol + ["--from", str(f), "--sadc", sadc]
            for f in (0, 1)]
    runs.append([os.path.join(LIB, "build", key, "world_boot_test" + exe)] + od + tol + [sadc, gdi])
    for cmd in runs:
        r = subprocess.run(cmd, cwd=LIB)
        print("[%s] %s exit %d" % (key, " ".join(os.path.basename(c) if i == 0 else c for i, c in enumerate(cmd)),
                                   r.returncode))
        rc |= r.returncode != 0
    return rc


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--target", default="x86_64-windows", choices=sorted(TARGETS) + ["all"])
    ap.add_argument("--sadc", default=os.path.join(os.path.dirname(LIB), "sadc"))
    ap.add_argument("--gdi", default=os.environ.get("SA1_TEST_GDI", ""))
    ap.add_argument("--out", help="write the world's traces under DIR/<target>/")
    ap.add_argument("--run", action="store_true")
    ap.add_argument("--strict", action="store_true", help="fail on the STG01 water task's residuals too")
    a = ap.parse_args()
    keys = sorted(TARGETS) if a.target == "all" else [a.target]
    rc = 0
    for k in keys:
        build(k, a.sadc)
        if a.run and runnable(k):
            if not a.gdi:
                raise SystemExit("--run needs --gdi or SA1_TEST_GDI")
            rc |= run(k, a.sadc, a.gdi, a.out, a.strict)
    sys.exit(rc)


main()
