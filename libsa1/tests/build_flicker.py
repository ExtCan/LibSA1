#!/usr/bin/env python3
"""Build and run tests/flicker_test.c (the render root is the drawn step's root, and libsa1 reports the frames it moved
with the root: sadc docs/research/gsa1_vertical_flicker.md fix B and review R.4) against a SNAPSHOT of
build/<target>/libsa1.a.

    py -3.12 tests/build_flicker.py [--target x86_64-windows|x86-windows] [--run] [--gdi FILE] [--sadc DIR]
                                    [--no-snapshot] [-- ARGS for flicker_test]

The archive and the headers it was built with (include/, src/*.h, src/world/*.h) are copied under libsa1's own build
lock (build/.build.lock, the lock build.py takes) into build/flicker/<target>/snap/, so a libsa1 build running at the
same time never hands this test a half-written archive or headers that do not match it. Nothing outside
build/flicker/ is written. The test is compiled with the world tests' flags (tests/build_ramp_diag.py). Built against
a libsa1 from before fix B (no SA1W_PSTATE_DRAWN / SA1_RENDER_MOVED_WITH_ROOT in the snapshot's headers) it only
measures the parts it can (the "before" numbers). --run runs flicker_test [ARGS] GDI (default $SA1_TEST_GDI; the disc
is never copied); the exit code is the test's.
"""
import argparse
import glob
import os
import shutil
import subprocess
import sys
import time

LIB = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TRIPLES = {"x86_64-windows": "x86_64-windows-gnu", "x86-windows": "x86-windows-gnu"}


class LibLock:
    """build.py serialises its builds with an exclusive lock on byte 0 of build/.build.lock."""

    def __init__(self, timeout=1800.0):
        self.timeout = timeout
        self.f = None

    def __enter__(self):
        path = os.path.join(LIB, "build", ".build.lock")
        os.makedirs(os.path.dirname(path), exist_ok=True)
        self.f = open(path, "a+")
        t0 = time.time()
        said = False
        while True:
            try:
                import msvcrt
                self.f.seek(0)
                msvcrt.locking(self.f.fileno(), msvcrt.LK_NBLCK, 1)
                return self
            except OSError:
                if time.time() - t0 > self.timeout:
                    raise TimeoutError("libsa1's build lock is held")
                if not said:
                    print("waiting for libsa1's build lock (a libsa1 build is running) ...", flush=True)
                    said = True
                time.sleep(2.0)

    def __exit__(self, *exc):
        if self.f:
            try:
                import msvcrt
                self.f.seek(0)
                msvcrt.locking(self.f.fileno(), msvcrt.LK_UNLCK, 1)
            finally:
                self.f.close()


def snapshot(target, snap):
    if os.path.isdir(snap):
        shutil.rmtree(snap)
    for d in ("include", "src", os.path.join("src", "world")):
        os.makedirs(os.path.join(snap, d), exist_ok=True)
    with LibLock():
        lib = os.path.join(LIB, "build", target, "libsa1.a")
        if not os.path.exists(lib):
            raise SystemExit("no %s: run build.py --target %s first" % (lib, target))
        shutil.copy2(lib, os.path.join(snap, "libsa1.a"))
        for pat, d in (("include/*.h", "include"), ("src/*.h", "src"), ("src/world/*.h", os.path.join("src", "world"))):
            for h in glob.glob(os.path.join(LIB, pat)):
                shutil.copy2(h, os.path.join(snap, d, os.path.basename(h)))
    a = os.path.join(snap, "libsa1.a")
    print("snapshot: libsa1.a %d bytes (LIB build of %s)" % (
        os.path.getsize(a), time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(os.path.getmtime(a)))))


def build(target, out, snap, sadc):
    lib = os.path.join(snap, "libsa1.a")
    if not os.path.exists(lib):
        raise SystemExit("no snapshot: run without --no-snapshot")
    flags = ["-target", TRIPLES[target], "-std=gnu11",
             "-I" + os.path.join(snap, "include"), "-I" + os.path.join(snap, "src"),
             "-I" + os.path.join(snap, "src", "world"), "-I" + os.path.join(sadc, "include"),
             "-I" + os.path.join(sadc, "harness", "src"), "-Wall", "-Wextra", "-Wno-unused-function",
             "-ffp-contract=off", "-frounding-math", "-fno-builtin", "-O2", "-g", "-DSA1_STATIC"]
    if target.startswith("x86-"):
        flags += ["-msse2", "-mfpmath=sse"]
    exe = os.path.join(out, "flicker_test.exe")
    cmd = [sys.executable, "-m", "ziglang", "cc", *flags, os.path.join(LIB, "tests", "flicker_test.c"), lib, "-o", exe]
    r = subprocess.run(cmd, cwd=LIB, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
    if r.stdout.strip():
        print(r.stdout.rstrip())
    if r.returncode != 0:
        raise SystemExit("FAILED: " + " ".join(cmd))
    print("built %s" % exe)
    return exe


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--target", default="x86_64-windows", choices=sorted(TRIPLES))
    ap.add_argument("--sadc", default=os.path.join(os.path.dirname(LIB), "sadc"))
    ap.add_argument("--gdi", default=os.environ.get("SA1_TEST_GDI", ""))
    ap.add_argument("--run", action="store_true")
    ap.add_argument("--no-snapshot", action="store_true")
    ap.add_argument("rest", nargs=argparse.REMAINDER)
    a = ap.parse_args()
    out = os.path.join(LIB, "build", "flicker", a.target)
    snap = os.path.join(out, "snap")
    os.makedirs(out, exist_ok=True)
    if not a.no_snapshot:
        snapshot(a.target, snap)
    exe = build(a.target, out, snap, a.sadc)
    if a.run:
        if not a.gdi:
            raise SystemExit("--run needs --gdi or SA1_TEST_GDI")
        rest = [x for x in a.rest if x != "--"]
        r = subprocess.run([exe] + rest + [a.gdi], cwd=LIB)
        sys.exit(r.returncode)


main()
