#!/usr/bin/env python3
"""Build and run tests/ramp_diag.c (diagnostics for G-SA1 0.3: falling through ramps, tick cost / freezes) against a
SNAPSHOT of build/x86_64-windows/libsa1.a.

    py -3.12 tests/build_ramp_diag.py [--run] [--gdi FILE] [--sadc DIR] [--no-snapshot] [-- ARGS for ramp_diag]

The archive and the headers it was built with (include/, src/*.h, src/world/*.h) are copied under libsa1's own build
lock (build/.build.lock, the lock build.py and g-sa1/build.py use) into build/ramp_diag/snap/, so a libsa1 build that
runs at the same time never hands this test a half-written archive or headers that do not match it. Nothing outside
build/ramp_diag/ is written. The test is compiled with the world tests' flags (as tests/build_grid.py).
--run runs build/ramp_diag/ramp_diag.exe [ARGS] GDI (default $SA1_TEST_GDI; the disc is never copied).
--touch-hook (diagnostics): the lowered TOUCH 8C029B00 (build/pclower/ga64/lowered/code_8C029B00.c, snapshotted with the
archive) is compiled into the test with the game flags, its returns calling the test's sa1diag_touch(): the section
"case" then prints every TOUCH call of the steps around a fall (the sphere, the object's NEAR-poly entries, the touches,
which was selected and the push-back). The archive's own copy of 8C029B00 is not linked (the test defines it first).
"""
import argparse
import glob
import os
import shutil
import subprocess
import sys
import time

LIB = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(LIB, "build", "ramp_diag")
SNAP = os.path.join(OUT, "snap")


class LibLock:
    """build.py serialises its builds with an exclusive lock on byte 0 of build/.build.lock."""

    def __init__(self, timeout=1800.0):
        self.timeout = timeout
        self.f = None

    def __enter__(self):
        path = os.path.join(LIB, "build", ".build.lock")
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


def snapshot():
    if os.path.isdir(SNAP):
        shutil.rmtree(SNAP)
    for d in ("include", "src", os.path.join("src", "world")):
        os.makedirs(os.path.join(SNAP, d), exist_ok=True)
    with LibLock():
        shutil.copy2(os.path.join(LIB, "build", "x86_64-windows", "libsa1.a"), os.path.join(SNAP, "libsa1.a"))
        for pat, d in (("include/*.h", "include"), ("src/*.h", "src"), ("src/world/*.h", os.path.join("src", "world"))):
            for h in glob.glob(os.path.join(LIB, pat)):
                shutil.copy2(h, os.path.join(SNAP, d, os.path.basename(h)))
        # --touch-hook: the lowered TOUCH and the force-included headers of the game objects
        os.makedirs(os.path.join(SNAP, "game"), exist_ok=True)
        for src in (os.path.join(LIB, "build", "pclower", "ga64", "lowered", "code_8C029B00.c"),
                    os.path.join(LIB, "src", "rt", "rt_intrin.h"), os.path.join(LIB, "src", "rt", "rt.h")):
            if os.path.exists(src):
                shutil.copy2(src, os.path.join(SNAP, "game", os.path.basename(src)))
    a = os.path.join(SNAP, "libsa1.a")
    print("snapshot: libsa1.a %d bytes (LIB build of %s)" % (
        os.path.getsize(a), time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(os.path.getmtime(a)))))


def pc_cflags(sadc):
    """sadc/tools/pc_cflags.txt for x86_64 (the [all] and [x86_64] sections), as LIB build.py reads it."""
    out, on = [], True
    for line in open(os.path.join(sadc, "tools", "pc_cflags.txt"), encoding="utf-8"):
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        if line.startswith("["):
            on = "all" in line or "x86_64" in line.strip("[]").split()
            continue
        if on:
            out += line.split()
    return out


def touch_hook(sadc):
    """the snapshotted lowered 8C029B00 with sa1diag_touch() before each return of func_8C029B00, compiled with the
    game flags (sadc tools/pc_cflags.txt, the game-text section, the inlined SHC intrinsics)"""
    src = os.path.join(SNAP, "game", "code_8C029B00.c")
    if not os.path.exists(src):
        raise SystemExit("--touch-hook: no lowered code_8C029B00.c in the snapshot (build LIB first)")
    text = open(src, encoding="utf-8").read()
    head = text.find("Sint32 func_8C029B00(csts *cp, NJS_OBJECT *object, Uint32 attr)\n{")
    if head < 0:
        raise SystemExit("--touch-hook: func_8C029B00 not found in the lowered file")
    body = text[head:]
    end = body.find("\n}\n") + 3
    fn = body[:end]
    n0, n1 = fn.count("return 0;"), fn.count("return 1;")
    fn = fn.replace("return 0;", "{ sa1diag_touch(cp, object, 0); return 0; }")
    fn = fn.replace("return 1;", "{ sa1diag_touch(cp, object, 1); return 1; }")
    if n0 < 2 or n1 != 1:
        raise SystemExit("--touch-hook: unexpected returns in func_8C029B00 (%d / %d)" % (n0, n1))
    text = text[:head] + "void sa1diag_touch(const void *cp, const void *object, int ret);\n" + fn + body[end:]
    hooked = os.path.join(OUT, "hook_8C029B00.c")
    with open(hooked, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    obj = os.path.join(OUT, "hook_8C029B00.o")
    flags = ["-target", "x86_64-windows-gnu"] + pc_cflags(sadc) + [
        "-std=gnu11", "-g", "-I" + os.path.join(sadc, "include"), "-I" + os.path.join(SNAP, "game"),
        "-I" + os.path.join(SNAP, "include"), "-Wno-unused-function", "-Wno-unused-variable",
        "-Wno-unused-but-set-variable", "-DSA1G_TEXT_PRAGMA", "-include", os.path.join(SNAP, "src", "world", "sa1g_text.h"),
        "-include", os.path.join(SNAP, "game", "rt_intrin.h")]
    cmd = [sys.executable, "-m", "ziglang", "cc", *flags, "-c", hooked, "-o", obj]
    r = subprocess.run(cmd, cwd=LIB, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
    if r.stdout.strip():
        print(r.stdout.rstrip())
    if r.returncode != 0:
        raise SystemExit("FAILED: " + " ".join(cmd))
    return obj


def build(sadc, hook=False):
    lib = os.path.join(SNAP, "libsa1.a")
    if not os.path.exists(lib):
        raise SystemExit("no snapshot: run without --no-snapshot")
    flags = ["-target", "x86_64-windows-gnu", "-std=gnu11",
             "-I" + os.path.join(SNAP, "include"), "-I" + os.path.join(SNAP, "src"),
             "-I" + os.path.join(SNAP, "src", "world"), "-I" + os.path.join(sadc, "include"),
             "-I" + os.path.join(sadc, "harness", "src"), "-Wall", "-Wextra", "-Wno-unused-function",
             "-ffp-contract=off", "-frounding-math", "-fno-builtin", "-O2", "-g", "-DSA1_STATIC"]
    exe = os.path.join(OUT, "ramp_diag.exe")
    extra = []
    if hook:
        flags.append("-DRAMP_TOUCH_HOOK")
        extra = [touch_hook(sadc)]
    cmd = [sys.executable, "-m", "ziglang", "cc", *flags, os.path.join(LIB, "tests", "ramp_diag.c"), *extra, lib, "-o", exe]
    r = subprocess.run(cmd, cwd=LIB, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
    if r.stdout.strip():
        print(r.stdout.rstrip())
    if r.returncode != 0:
        raise SystemExit("FAILED: " + " ".join(cmd))
    print("built %s" % exe)
    return exe


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sadc", default=os.path.join(os.path.dirname(LIB), "sadc"))
    ap.add_argument("--gdi", default=os.environ.get("SA1_TEST_GDI", ""))
    ap.add_argument("--run", action="store_true")
    ap.add_argument("--no-snapshot", action="store_true")
    ap.add_argument("--touch-hook", action="store_true", help="log every TOUCH call (diagnostics, section case)")
    ap.add_argument("rest", nargs=argparse.REMAINDER)
    a = ap.parse_args()
    os.makedirs(OUT, exist_ok=True)
    if not a.no_snapshot:
        snapshot()
    exe = build(a.sadc, a.touch_hook)
    if a.run:
        if not a.gdi:
            raise SystemExit("--run needs --gdi or SA1_TEST_GDI")
        rest = [x for x in a.rest if x != "--"]
        r = subprocess.run([exe] + rest + [a.gdi], cwd=LIB)
        sys.exit(r.returncode)


main()
