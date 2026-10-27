#!/usr/bin/env python3
"""Build and run the render tests of the 0.6.0 fixes against a SNAPSHOT of build/<target>/libsa1.a:

    tests/pairs_test.c  a tick that runs several pairs renders bit for bit what one-pair ticks render (sadc
                        docs/research/gsa1_060_pops.md F1, Review R.3.1 / R.5.2)
    tests/decal_test.c  eyes, irises, pupils and decals stay in front of the faces they lie on at every alpha (anchored
                        interpolation, sadc docs/research/gsa1_060_render.md 3.6 and Review R3.4 / R3.5 / R4)
    tests/blob_test.c   the game's blob shadow in the render output (SA1_DRAW_SHADOW; sadc docs/research/
                        gsa1_060_shadow.md L3-L5 and Review R.3.4 / R.3.5 / R.4)
    tests/ball_test.c   round 0.9.1: Tails' spin ball and curled body (screw of rigid groups' roots), whole bodies of
                        all six characters between the pairs, Amy's bird interpolated in world space (sadc docs/research/
                        gsa1_070_tails.md F-T1 with Review R.4.1 / R.4.2, gsa1_070_bird.md Review R.4 F5)
    tests/visuals_test.c  round 0.9.4: game OBJECT draws (rings, ring groups, dug rings) matched by their place between
                        the pairs, never slid from another ring's place; the character's own draws unchanged (sadc
                        docs/research/gsa1_094_plan.md 6.2, gsa1_091b_visuals.md 4.2 with its Review)

    py -3.12 tests/build_render_tests.py [pairs|decal|blob|ball|visuals|all] [--target T] [--run] [--gdi GDI] [--sadc DIR]
                                         [--no-snapshot] [--before DIR] [-- ARGS]

The archive and the headers it was built with (include/, src/*.h, src/world/*.h) are copied under libsa1's build lock
(build/.build.lock, the lock build.py takes) into build/render_tests/<target>/snap/, so a build running at the same time
never hands a test a half-written archive. --before DIR builds against DIR instead (DIR/libsa1.a with DIR/include,
DIR/src, DIR/src/world: a copy of an older library, to see a test fail before the fixes; tests that need a newer
header say so at compile time). --run runs each test with GDI (default $SA1_TEST_GDI; the disc is never copied) and
exits with the first failing test's code. Shortcuts: tests/build_pairs.py, build_decal.py, build_blob.py,
build_ball.py.
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
TESTS = ("pairs", "decal", "blob", "ball", "visuals")


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


def build(name, target, out, snap, sadc):
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


def main(default=None):
    ap = argparse.ArgumentParser()
    if default is None:
        ap.add_argument("test", nargs="?", default="all", choices=TESTS + ("all",))
    ap.add_argument("--target", default="x86_64-windows", choices=sorted(TRIPLES))
    ap.add_argument("--sadc", default=os.path.join(os.path.dirname(LIB), "sadc"))
    ap.add_argument("--gdi", default=os.environ.get("SA1_TEST_GDI", ""))
    ap.add_argument("--run", action="store_true")
    ap.add_argument("--no-snapshot", action="store_true")
    ap.add_argument("--before", default="", help="build against DIR/libsa1.a + DIR/include, DIR/src, DIR/src/world")
    argv = sys.argv[1:]
    rest = argv[argv.index("--") + 1:] if "--" in argv else []
    a = ap.parse_args(argv[:argv.index("--")] if "--" in argv else argv)
    which = default if default is not None else a.test
    names = TESTS if which == "all" else (which,)
    out = os.path.join(LIB, "build", "render_tests", a.target + ("-before" if a.before else ""))
    os.makedirs(out, exist_ok=True)
    if a.before:
        snap = os.path.abspath(a.before)
    else:
        snap = os.path.join(out, "snap")
        if not a.no_snapshot:
            snapshot(a.target, snap)
    exes = [(n, build(n, a.target, out, snap, a.sadc)) for n in names]
    rc = 0
    if a.run:
        if not a.gdi:
            raise SystemExit("--run needs --gdi or SA1_TEST_GDI")
        for n, exe in exes:
            print("==== %s_test" % n, flush=True)
            r = subprocess.run([exe] + rest + [a.gdi], cwd=LIB)
            print("==== %s_test exit %d" % (n, r.returncode), flush=True)
            if r.returncode != 0 and rc == 0:
                rc = r.returncode
    sys.exit(rc)


if __name__ == "__main__":
    main()
