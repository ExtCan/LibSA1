#!/usr/bin/env python3
"""Build and run tests/memcheck_test.c (libsa1's GRID synthesis under the page-guard allocator tests/guard_alloc.c: every
library heap block against a no-access page; both preparation paths over synthetic scenes and the GMod map dumps, every
GRID parameter set; the guard allocator's own self-test) against build/<target>/libsa1.a.

    py -3.12 tests/build_memcheck.py [--target x86_64-windows|x86-windows|x86_64-linux|windows|all] [--run]
                                     [--gdi FILE] [--maps DIR] [--only selftest|scenes|maps]... [--quick]
                                     [--threads N] [--sadc DIR] [--verbose]

Run it from LIB after `py -3.12 build.py --target <target>`. The default target is "windows" (x86_64-windows and
x86-windows). --run (runnable targets) runs build/<target>/memcheck_test [GDI] with the user's GD-ROM dump (--gdi, default
$SA1_TEST_GDI; never copied; without it path B, set_static + create, is skipped). --maps (default $SA1_MEMCHECK_MAPS) is a
directory of tris_*.bin map dumps (raw sa1_collision_tri arrays in Source units); map-derived data is never part of the
repository, so without one the map part is skipped.

The guard allocator in the other tests: set SA1_GUARD_ALLOC=1 (or pass --guard-alloc to grid_test, prepare_test,
ramp_diag, capture_test, world_boot_test, transplant_test, replay_test); see tests/guard_alloc.h for its options.
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
    exe = os.path.join(out, "memcheck_test" + (".exe" if key.endswith("windows") else ""))
    cmd = [sys.executable, "-m", "ziglang", "cc", *flags, os.path.join("tests", "memcheck_test.c"), lib, "-o", exe, *libs]
    r = subprocess.run(cmd, cwd=LIB, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
    if r.stdout.strip():
        print(r.stdout.rstrip())
    if r.returncode != 0:
        raise SystemExit("FAILED: " + " ".join(cmd))
    print("[%s] built %s" % (key, exe))
    return exe


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--target", default="windows", choices=sorted(TARGETS) + ["windows", "all"])
    ap.add_argument("--sadc", default=os.path.join(os.path.dirname(LIB), "sadc"))
    ap.add_argument("--gdi", default=os.environ.get("SA1_TEST_GDI", ""))
    ap.add_argument("--maps", default=os.environ.get("SA1_MEMCHECK_MAPS", ""))
    ap.add_argument("--run", action="store_true")
    ap.add_argument("--only", action="append", default=[], help="run only this part (repeatable)")
    ap.add_argument("--quick", action="store_true", help="maps: leave out the parameter sets with the most pieces")
    ap.add_argument("--threads", type=int, default=0, help="worker threads of path A (default 4 on x64, 2 on x86)")
    ap.add_argument("--verbose", action="store_true")
    a = ap.parse_args()
    if a.target == "all":
        keys = sorted(TARGETS)
    elif a.target == "windows":
        keys = ["x86_64-windows", "x86-windows"]
    else:
        keys = [a.target]
    rc = 0
    for k in keys:
        exe = build(k, a.sadc)
        if a.run and runnable(k):
            extra = []
            for o in a.only:
                extra += ["--only", o]
            if a.maps:
                extra += ["--maps", a.maps]
            if a.quick:
                extra.append("--quick")
            if a.threads:
                extra += ["--threads", str(a.threads)]
            if a.verbose:
                extra.append("--verbose")
            if a.gdi:
                extra.append(a.gdi)
            r = subprocess.run([os.path.join(LIB, exe)] + extra, cwd=LIB)
            print("[%s] memcheck_test exit %d" % (k, r.returncode))
            rc |= r.returncode != 0
    sys.exit(1 if rc else 0)


main()
