#!/usr/bin/env python3
"""Build (and run) the display-capture / render-output tests in a private output directory.

    py -3.12 tests/build_capture.py [--target T] [--build] [--run] [--part LIST] [--texdump] [--harness DIR] [--gdi GDI]
                                    [--sadc DIR]

    --build     run build.py --target T first (it takes libsa1's build lock itself)
    --run       run capture_test (tests/capture_test.c: parts 1-9, "lsd" (round 4c's special-move visuals), "alpha" (round
                0.9.1's vertex alpha) and "powerup" (round 0.9.2a FX3: the item box power-up models)) with the disc and
                sadc's reference data
    --part LIST capture_test --part LIST: only these parts (comma-separated: 1-9, lsd, alpha, powerup; e.g. "lsd" or
                "1,7,lsd")
    --texdump   run texdump (every texture as PNG + renders into build/texdump) and tests/texdump_ref.py (the
                independent decoder: every texture texel for texel, and the texlist names)

Outputs in build/capture/<target>/: capture_test(.exe), texdump(.exe), built against a snapshot of
build/<target>/libsa1.a taken under build/.build.lock (so a build running at the same time never hands over a
half-written archive). The disc: --gdi or $SA1_TEST_GDI (never copied).
"""
import argparse
import os
import shutil
import subprocess
import sys
import time

TESTS = os.path.dirname(os.path.abspath(__file__))
LIB = os.path.dirname(TESTS)
TRIPLES = {"x86_64-windows": "x86_64-windows-gnu", "x86-windows": "x86-windows-gnu", "x86_64-linux": "x86_64-linux-gnu"}
WORLD_FP = ["-ffp-contract=off", "-frounding-math", "-fno-builtin"]


def locked_copy(src, dst):
    lockp = os.path.join(LIB, "build", ".build.lock")
    os.makedirs(os.path.dirname(lockp), exist_ok=True)
    f = open(lockp, "a+")
    try:
        if os.name == "nt":
            import msvcrt
            while True:
                try:
                    f.seek(0)
                    msvcrt.locking(f.fileno(), msvcrt.LK_NBLCK, 1)
                    break
                except OSError:
                    time.sleep(1.0)
        else:
            import fcntl
            fcntl.flock(f.fileno(), fcntl.LOCK_EX)
        shutil.copyfile(src, dst)
    finally:
        try:
            if os.name == "nt":
                import msvcrt
                f.seek(0)
                msvcrt.locking(f.fileno(), msvcrt.LK_UNLCK, 1)
        finally:
            f.close()


def run(cmd, cwd=LIB):
    print("$ " + " ".join(cmd))
    r = subprocess.run(cmd, cwd=cwd)
    return r.returncode


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--target", default="x86_64-windows", choices=sorted(TRIPLES))
    ap.add_argument("--build", action="store_true")
    ap.add_argument("--run", action="store_true")
    ap.add_argument("--texdump", action="store_true")
    ap.add_argument("--harness", help="capture_test --harness DIR (parts 3 and 4)")
    ap.add_argument("--part", help="capture_test --part LIST (1-9, lsd, alpha, powerup; comma-separated)")
    ap.add_argument("--gdi", default=os.environ.get("SA1_TEST_GDI"))
    ap.add_argument("--sadc", default=os.path.join(os.path.dirname(LIB), "sadc"))
    args = ap.parse_args()
    if args.build and run([sys.executable, "build.py", "--target", args.target]) != 0:
        print("build.py failed")
        return 1
    lib = os.path.join(LIB, "build", args.target, "libsa1.a")
    if not os.path.exists(lib):
        print("no %s: run build.py --target %s first (or --build)" % (lib, args.target))
        return 1
    out = os.path.join(LIB, "build", "capture", args.target)
    os.makedirs(out, exist_ok=True)
    snap = os.path.join(out, "libsa1_snapshot.a")
    locked_copy(lib, snap)
    exe = ".exe" if args.target.endswith("windows") else ""
    zig = [sys.executable, "-m", "ziglang", "cc", "-target", TRIPLES[args.target]]
    inc_sadc = os.path.join(args.sadc, "include")
    extra = ["-msse2", "-mfpmath=sse"] if args.target.startswith("x86-") else []
    rc = run(zig + ["-std=gnu11", "-Iinclude", "-Isrc", "-Isrc/world", "-I" + inc_sadc,
                    "-I" + os.path.join(args.sadc, "harness", "src"), "-Wall", "-Wextra", "-O2", "-g"] + WORLD_FP + extra +
             ["-DSA1_STATIC", "tests/capture_test.c", snap, "-o", os.path.join(out, "capture_test" + exe)])
    rc |= run(zig + ["-std=gnu11", "-Iinclude", "-Itests", "-Wall", "-Wextra", "-O2", "-g", "-DSA1_STATIC",
                     "tests/texdump.c", snap, "-o", os.path.join(out, "texdump" + exe)])
    if rc:
        print("compile failed")
        return 1
    if (args.run or args.texdump) and not args.gdi:
        print("no disc: --gdi or SA1_TEST_GDI")
        return 1
    if args.run:
        cmd = [os.path.join(out, "capture_test" + exe)]
        if args.harness:
            cmd += ["--harness", args.harness]
        if args.part:
            cmd += ["--part", args.part]
        rc |= run(cmd + [args.sadc, args.gdi])
    if args.texdump:
        rc |= run([os.path.join(out, "texdump" + exe), "--gdi", args.gdi, "--render"])
        rc |= run([sys.executable, os.path.join("tests", "texdump_ref.py"), "--gdi", args.gdi, "--dump",
                   os.path.join("build", "texdump")])
    return 1 if rc else 0


if __name__ == "__main__":
    sys.exit(main())
