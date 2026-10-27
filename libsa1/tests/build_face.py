#!/usr/bin/env python3
"""Build and run tests/face_test.c (round 0.9.4a: the game's event faces, include/libsa1.h "faces", SA1_API_FACE; sadc
docs/research/faces_wheel_plan.md 4.2-4.4, 5.3 and 6.2 with its Review) against a SNAPSHOT of build/<target>/libsa1.a and its
headers.

    py -3.12 tests/build_face.py [--target x86_64-windows|x86-windows|x86_64-linux|windows|all] [--run] [--gdi GDI]
                                 [--sadc DIR] [--no-snapshot] [--no-guard] [-- ARGS]

The archive and the headers it was built with (include/, src/*.h, src/world/*.h) are copied under libsa1's build lock
(build/.build.lock, the lock build.py takes) into build/face_test/<target>/snap/, so a build running at the same time never
hands the test a half-written archive. --run runs face_test with GDI (default $SA1_TEST_GDI; the disc is never copied) and
ARGS (e.g. --verbose, or --only NAME[,NAME...] to select cases by name), then - unless --no-guard or ARGS select cases -
runs the case rebuild_memory (F24) again with SA1_GUARD_ALLOC=1 (libsa1's heap blocks on the page-guard allocator,
tests/guard_alloc.h). It exits non-zero when a run fails. When more than one target ran, their "FACE_TIMELINE <hash>" lines
(a hash of a fixed face scenario: every face field, rand and position per pair) must be equal (x86 = x64).
The default target "windows" is x86_64-windows and x86-windows.
"""
import argparse
import glob
import os
import platform
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


def runnable(key):
    if key.endswith("windows"):
        return platform.system() == "Windows"
    return platform.system() == "Linux" and platform.machine().lower() in ("x86_64", "amd64")


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
    print("[%s] snapshot: libsa1.a %d bytes (LIB build of %s)" % (
        target, os.path.getsize(a), time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(os.path.getmtime(a)))))


def build(target, out, snap, sadc):
    lib = os.path.join(snap, "libsa1.a")
    if not os.path.exists(lib):
        raise SystemExit("no library at %s: run without --no-snapshot" % lib)
    flags = ["-target", TRIPLES[target], "-std=gnu11",
             "-I" + os.path.join(snap, "include"), "-I" + os.path.join(snap, "src"),
             "-I" + os.path.join(snap, "src", "world"), "-I" + os.path.join(sadc, "include"),
             "-I" + os.path.join(LIB, "tests"), "-Wall", "-Wextra", "-Wno-unused-function",
             "-ffp-contract=off", "-frounding-math", "-fno-builtin", "-O2", "-g", "-DSA1_STATIC"]
    if target.startswith("x86-"):
        flags += ["-msse2", "-mfpmath=sse"]
    if "linux" in target:
        flags += ["-fPIC"]
    exe = os.path.join(out, "face_test" + (".exe" if "windows" in target else ""))
    cmd = [sys.executable, "-m", "ziglang", "cc", *flags, os.path.join(LIB, "tests", "face_test.c"), lib, "-o", exe]
    if "linux" in target:
        cmd += ["-lm", "-lpthread"]
    r = subprocess.run(cmd, cwd=LIB, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
    if r.stdout.strip():
        print(r.stdout.rstrip())
    if r.returncode != 0:
        raise SystemExit("FAILED: " + " ".join(cmd))
    print("[%s] built %s" % (target, exe))
    return exe


def run(k, exe, args, gdi, env=None, label="face_test"):
    print("==== [%s] %s %s" % (k, label, " ".join(args)), flush=True)
    r = subprocess.run([exe] + args + [gdi], cwd=LIB, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                       errors="replace", env=env)
    sys.stdout.write(r.stdout)
    print("==== [%s] %s exit %d" % (k, label, r.returncode), flush=True)
    return r


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--target", default="windows", choices=sorted(TRIPLES) + ["windows", "all"])
    ap.add_argument("--sadc", default=os.path.join(os.path.dirname(LIB), "sadc"))
    ap.add_argument("--gdi", default=os.environ.get("SA1_TEST_GDI", ""))
    ap.add_argument("--run", action="store_true")
    ap.add_argument("--no-snapshot", action="store_true")
    ap.add_argument("--no-guard", action="store_true", help="skip the second run of rebuild_memory under SA1_GUARD_ALLOC=1")
    argv = sys.argv[1:]
    rest = argv[argv.index("--") + 1:] if "--" in argv else []
    a = ap.parse_args(argv[:argv.index("--")] if "--" in argv else argv)
    if a.target == "all":
        keys = sorted(TRIPLES)
    elif a.target == "windows":
        keys = ["x86_64-windows", "x86-windows"]
    else:
        keys = [a.target]
    rc = 0
    tl = {}
    for k in keys:
        out = os.path.join(LIB, "build", "face_test", k)
        os.makedirs(out, exist_ok=True)
        snap = os.path.join(out, "snap")
        if not a.no_snapshot:
            snapshot(k, snap)
        exe = build(k, out, snap, a.sadc)
        if a.run and runnable(k):
            if not a.gdi:
                raise SystemExit("--run needs --gdi or SA1_TEST_GDI")
            env = dict(os.environ)
            env.pop("SA1_GUARD_ALLOC", None)
            r = run(k, exe, rest, a.gdi, env)
            rc |= r.returncode != 0
            for line in r.stdout.splitlines():
                if line.startswith("FACE_TIMELINE "):
                    tl[k] = line.strip()
            if not a.no_guard and "--only" not in rest:
                env["SA1_GUARD_ALLOC"] = "1"
                r = run(k, exe, ["--only", "rebuild_memory"], a.gdi, env, "face_test (SA1_GUARD_ALLOC=1)")
                rc |= r.returncode != 0
    if len(tl) > 1:
        same = len(set(v.split()[1] for v in tl.values())) == 1
        print("face timeline: %s (%s)" % ("identical" if same else "DIFFERENT",
                                           "; ".join("%s: %s" % (k, v) for k, v in sorted(tl.items()))))
        rc |= not same
    sys.exit(1 if rc else 0)


if __name__ == "__main__":
    main()
