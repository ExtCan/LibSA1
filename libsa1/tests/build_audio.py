#!/usr/bin/env python3
"""Build and run tests/audio_test.c (the audio layer: include/libsa1_audio.h, src/sa1_audio*.c) against
build/<target>/libsa1.a.

    py -3.12 tests/build_audio.py [--target x86_64-windows|x86-windows|x86_64-linux|windows|all] [--run]
                                  [--gdi FILE] [--out DIR] [-- ARGS]

The audio sources are compiled here and linked before the archive, so the test always uses the current
sa1_audio*.c even when the archive is older (run `py -3.12 build.py --target <target>` for the rest of the
library).  --run (runnable targets) runs build/audio_test_bin/<target>/audio_test GDI OUT (objects and program
outside build/<target>, which a library build's --clean removes) with the user's GD-ROM dump (--gdi,
default $SA1_TEST_GDI; never copied); it writes WAV files for listening into OUT (default build/audio_test,
local only: they hold game audio). ARGS go to audio_test (e.g. --only stage_rules,repeats or --skip whoosh_world;
the groups are listed at audio_test.c's want()). The link takes libsa1's build lock (build/.build.lock, as build.py and
tests/build_super.py do), so a library build running at the same time never hands the test a half-written archive.
"windows" = x86_64-windows and x86-windows.
"""
import argparse
import glob
import os
import platform
import subprocess
import sys
import time

LIB = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TARGETS = {"x86_64-windows": "x86_64-windows-gnu", "x86-windows": "x86-windows-gnu", "x86_64-linux": "x86_64-linux-gnu"}
WARN = ["-Wall", "-Wextra", "-Wpedantic", "-Wshadow", "-Wstrict-prototypes", "-Wmissing-prototypes", "-Wvla",
        "-Wcast-qual", "-Wpointer-arith", "-Wundef"]


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


def runnable(key):
    if key.endswith("windows"):
        return platform.system() == "Windows"
    return platform.system() == "Linux" and platform.machine().lower() in ("x86_64", "amd64")


def zig(*args):
    return [sys.executable, "-m", "ziglang", "cc", *args]


def run(cmd):
    r = subprocess.run(cmd, cwd=LIB, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
    if r.stdout.strip():
        print(r.stdout.rstrip())
    if r.returncode != 0:
        raise SystemExit("FAILED: " + " ".join(cmd))


def build(key, werror):
    triple = TARGETS[key]
    out = os.path.join("build", key)
    lib = os.path.join(out, "libsa1.a")
    # objects and the test program live outside build/<target>: a library build's --clean (another agent's, while this
    # one waits for the lock or runs the test) removes build/<target> (round 0.9.4a)
    obj = os.path.join("build", "audio_test_bin", key)
    os.makedirs(os.path.join(LIB, obj), exist_ok=True)
    flags = ["-target", triple, "-std=c99", "-Iinclude", "-Isrc", "-O2", "-g", "-DSA1_STATIC"] + WARN
    if werror:
        flags.append("-Werror")
    if key.startswith("x86-"):
        flags += ["-msse2", "-mfpmath=sse"]
    if not key.endswith("windows"):
        flags += ["-fPIC"]
    objs = []
    for src in sorted(glob.glob(os.path.join(LIB, "src", "sa1_audio*.c"))):
        o = os.path.join(obj, os.path.splitext(os.path.basename(src))[0] + ".o")
        run(zig(*flags, "-c", os.path.relpath(src, LIB), "-o", o))
        objs.append(o)
    exe = os.path.join(obj, "audio_test" + (".exe" if key.endswith("windows") else ""))
    libs = [] if key.endswith("windows") else ["-lm", "-lpthread"]
    tflags = [f for f in flags if f != "-std=c99"] + ["-std=gnu11", "-Wno-missing-prototypes"]
    with LibLock():     # a library build running now (maybe --clean) is done when the lock is ours
        if not os.path.exists(os.path.join(LIB, lib)):
            raise SystemExit("%s missing: run py -3.12 build.py --target %s first" % (lib, key))
        run(zig(*tflags, os.path.join("tests", "audio_test.c"), *objs, lib, "-o", exe, *libs))
    print("[%s] built %s" % (key, exe))
    return exe


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--target", default="x86_64-windows", choices=sorted(TARGETS) + ["windows", "all"])
    ap.add_argument("--gdi", default=os.environ.get("SA1_TEST_GDI", ""))
    ap.add_argument("--out", default=os.path.join("build", "audio_test"))
    ap.add_argument("--run", action="store_true")
    ap.add_argument("--werror", action="store_true")
    argv = sys.argv[1:]
    rest = argv[argv.index("--") + 1:] if "--" in argv else []
    a = ap.parse_args(argv[:argv.index("--")] if "--" in argv else argv)
    if a.target == "all":
        keys = sorted(TARGETS)
    elif a.target == "windows":
        keys = ["x86_64-windows", "x86-windows"]
    else:
        keys = [a.target]
    rc = 0
    for k in keys:
        exe = build(k, a.werror)
        if a.run and runnable(k):
            if not a.gdi:
                raise SystemExit("--run needs --gdi or SA1_TEST_GDI")
            r = subprocess.run([os.path.join(LIB, exe), a.gdi, a.out] + rest, cwd=LIB)
            print("[%s] audio_test exit %d" % (k, r.returncode))
            rc |= r.returncode != 0
    sys.exit(1 if rc else 0)


main()
