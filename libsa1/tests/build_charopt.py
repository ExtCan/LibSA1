#!/usr/bin/env python3
"""Build and run the round 0.9.1 world-layer tests against a SNAPSHOT of build/<target>/libsa1.a:

    tests/drown_test.c      deep host water (W1), the deaths of drowning / death zones / the RESTART fallback / E-102's
                            time-up (E1), the life counter (E2)          (sadc docs/research/gsa1_070_drown.md)
    tests/companion_test.c  Amy's bird through sa1_character_set_companion (F1)   (sadc docs/research/gsa1_070_bird.md)
    tests/tailsnap_test.c   the game option SA1_GOPT_TAILS_SNAP_TAILS        (sadc gsa1_rounds_plan.md 14.1h)

    py -3.12 tests/build_charopt.py [--target x86_64-windows|x86-windows|x86_64-linux] [--run] [--gdi GDI] [--sadc DIR]
                                    [--test drown|companion|tailsnap]... [--no-snapshot] [-- ARGS]

The archive and the headers it was built with (include/, src/*.h, src/world/*.h) are copied under libsa1's build lock
(build/.build.lock, the lock build.py takes) into build/charopt_test/<target>/snap/, so a build running at the same time
never hands the tests a half-written archive. --run runs each test with GDI (default $SA1_TEST_GDI; the disc is never
copied) and ARGS (e.g. --verbose, or --only NAME for drown_test), and exits non-zero when one fails.
"""
import argparse
import glob
import os
import shutil
import subprocess
import sys
import time

LIB = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TRIPLES = {"x86_64-windows": "x86_64-windows-gnu", "x86-windows": "x86-windows-gnu", "x86_64-linux": "x86_64-linux-gnu"}
TESTS = ["drown", "companion", "tailsnap"]


class LibLock:
    """build.py serialises its builds with an exclusive lock on byte 0 of build/.build.lock."""

    def __init__(self, timeout=3600.0):
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
                if os.name == "nt":
                    import msvcrt
                    self.f.seek(0)
                    msvcrt.locking(self.f.fileno(), msvcrt.LK_NBLCK, 1)
                else:
                    import fcntl
                    fcntl.flock(self.f.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
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
                if os.name == "nt":
                    import msvcrt
                    self.f.seek(0)
                    msvcrt.locking(self.f.fileno(), msvcrt.LK_UNLCK, 1)
                else:
                    import fcntl
                    fcntl.flock(self.f.fileno(), fcntl.LOCK_UN)
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


def build(target, out, snap, sadc, name):
    lib = os.path.join(snap, "libsa1.a")
    if not os.path.exists(lib):
        raise SystemExit("no library at %s: run without --no-snapshot" % lib)
    flags = ["-target", TRIPLES[target], "-std=gnu11",
             "-I" + os.path.join(snap, "include"), "-I" + os.path.join(snap, "src"),
             "-I" + os.path.join(snap, "src", "world"), "-I" + os.path.join(sadc, "include"),
             "-I" + os.path.join(sadc, "harness", "src"), "-Wall", "-Wextra", "-Wno-unused-function",
             "-ffp-contract=off", "-frounding-math", "-fno-builtin", "-O2", "-g", "-DSA1_STATIC"]
    if target.startswith("x86-"):
        flags += ["-msse2", "-mfpmath=sse"]
    exe = os.path.join(out, name + "_test" + (".exe" if "windows" in target else ""))
    cmd = [sys.executable, "-m", "ziglang", "cc", *flags, os.path.join(LIB, "tests", name + "_test.c"), lib, "-o", exe]
    if "linux" in target:
        cmd += ["-lm", "-lpthread"]
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
    ap.add_argument("--test", action="append", choices=TESTS, default=[])
    ap.add_argument("--no-snapshot", action="store_true")
    argv = sys.argv[1:]
    rest = argv[argv.index("--") + 1:] if "--" in argv else []
    a = ap.parse_args(argv[:argv.index("--")] if "--" in argv else argv)
    names = a.test or TESTS
    out = os.path.join(LIB, "build", "charopt_test", a.target)
    os.makedirs(out, exist_ok=True)
    snap = os.path.join(out, "snap")
    if not a.no_snapshot:
        snapshot(a.target, snap)
    exes = [(n, build(a.target, out, snap, a.sadc, n)) for n in names]
    rc = 0
    if a.run:
        if not a.gdi:
            raise SystemExit("--run needs --gdi or SA1_TEST_GDI")
        for n, exe in exes:
            print("==== %s_test" % n, flush=True)
            r = subprocess.run([exe] + rest + [a.gdi], cwd=LIB)
            print("==== %s_test exit %d" % (n, r.returncode), flush=True)
            rc |= r.returncode != 0
    sys.exit(1 if rc else 0)


if __name__ == "__main__":
    main()
