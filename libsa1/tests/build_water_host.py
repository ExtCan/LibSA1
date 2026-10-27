#!/usr/bin/env python3
"""Build and run tests/water_host_test.c (round 0.9.2, sadc docs/research/gsa1_090_water.md WTR-A / WTR-W1V with its
Review R4 a-c, R3.3-R3.5; gsa1_090_plan.md 6.3): the host's water at the character (sa1_input.host_flags) and the deep-water
rule W1 of round 0.9.1 (its land test, a moving ceiling, its cost) against a SNAPSHOT of build/<target>/libsa1.a.

    py -3.12 tests/build_water_host.py [--target x86_64-windows|x86-windows|x86_64-linux|all] [--run] [--gdi GDI]
                                       [--sadc DIR] [--snap DIR] [--out DIR] [--no-snapshot] [-- ARGS]

Built like tests/build_grid.py (the world tests' flags, the world core's include paths); the archive and the headers it was
built with (include/, src/*.h, src/world/*.h) are copied under libsa1's build lock (build/.build.lock, the lock build.py
takes) into build/water_host/<target>/snap/ (or --snap DIR), so a build running at the same time never hands the test a
half-written archive. --run runs the test with GDI (default $SA1_TEST_GDI; the disc is never copied) and ARGS, for example
--only NAME, --verbose, --hash (the no-flag fingerprints: one line per scene, compared between two builds), or the opt-in
owner-map checks --map NAME SOUP WATER POINTS (see the test's header), and exits non-zero when a check fails.
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


def build(target, out, snap, sadc):
    lib = os.path.join(snap, "libsa1.a")
    if not os.path.exists(lib):
        raise SystemExit("no library at %s: run without --no-snapshot" % lib)
    flags = ["-target", TRIPLES[target], "-std=gnu11",
             "-I" + os.path.join(snap, "include"), "-I" + os.path.join(snap, "src"),
             "-I" + os.path.join(snap, "src", "world"), "-I" + os.path.join(sadc, "include"),
             "-I" + os.path.join(sadc, "harness", "src"), "-Wall", "-Wextra", "-Wshadow", "-Wno-unused-function",
             "-ffp-contract=off", "-frounding-math", "-fno-builtin", "-O2", "-g", "-DSA1_STATIC"]
    if target.startswith("x86-"):
        flags += ["-msse2", "-mfpmath=sse"]
    exe = os.path.join(out, "water_host_test" + (".exe" if "windows" in target else ""))
    cmd = [sys.executable, "-m", "ziglang", "cc", *flags, os.path.join(LIB, "tests", "water_host_test.c"), lib, "-o", exe]
    if "linux" in target:
        cmd += ["-lm", "-lpthread"]
    r = subprocess.run(cmd, cwd=LIB, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
    if r.stdout.strip():
        print(r.stdout.rstrip())
    if r.returncode != 0:
        raise SystemExit("FAILED: " + " ".join(cmd))
    print("built %s" % exe)
    return exe


def runnable(target):
    import platform
    if target.endswith("windows"):
        return platform.system() == "Windows"
    return platform.system() == "Linux" and platform.machine().lower() in ("x86_64", "amd64")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--target", default="x86_64-windows", choices=sorted(TRIPLES) + ["all"])
    ap.add_argument("--sadc", default=os.path.join(os.path.dirname(LIB), "sadc"))
    ap.add_argument("--gdi", default=os.environ.get("SA1_TEST_GDI", ""))
    ap.add_argument("--run", action="store_true")
    ap.add_argument("--snap", default="", help="snapshot directory (default build/water_host/<target>/snap)")
    ap.add_argument("--out", default="", help="where the test is built (default build/water_host/<target>)")
    ap.add_argument("--no-snapshot", action="store_true", help="build against the snapshot that is there")
    argv = sys.argv[1:]
    rest = argv[argv.index("--") + 1:] if "--" in argv else []
    a = ap.parse_args(argv[:argv.index("--")] if "--" in argv else argv)
    targets = sorted(TRIPLES) if a.target == "all" else [a.target]
    rc = 0
    for t in targets:
        out = os.path.abspath(a.out) if a.out else os.path.join(LIB, "build", "water_host", t)
        os.makedirs(out, exist_ok=True)
        snap = os.path.abspath(a.snap) if a.snap else os.path.join(out, "snap")
        if not a.no_snapshot:
            snapshot(t, snap)
        exe = build(t, out, snap, a.sadc)
        if a.run and runnable(t):
            if not a.gdi:
                raise SystemExit("--run needs --gdi or SA1_TEST_GDI")
            print("==== water_host_test [%s]" % t, flush=True)
            r = subprocess.run([exe] + rest + [a.gdi], cwd=LIB)
            print("==== water_host_test [%s] exit %d" % (t, r.returncode), flush=True)
            rc |= r.returncode != 0
    sys.exit(1 if rc else 0)


if __name__ == "__main__":
    main()
