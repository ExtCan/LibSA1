#!/usr/bin/env python3
"""Build and run tests/camreq_test.c (round 0.9.8, 098-lib-lookcam: the game's camera requests, SA1_API_CAMERA; sadc
docs/research/fish098.md 6.1, gsa1_098_plan.md 6.3) against a snapshot of build/<target>/libsa1.a and its headers - the same options and
rules as tests/build_board.py (whose code it uses), with sadc's harness/src on the include path (T1 replays the FISHCAM references through
tests/replay_test.c, included whole) and T1's outputs in build/camreq_test/<target>/replay:

    py -3.12 tests/build_camreq.py [--target x86_64-windows|x86-windows|x86_64-linux|windows|all] [--run] [--gdi GDI]
                                   [--sadc DIR] [--debug] [--no-snapshot] [--timeout S] [-- --only T1,T2,...]
"""
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import build_board  # noqa: E402

LIB = build_board.LIB


def build(name, target, out, snap, sadc):
    lib = os.path.join(snap, "libsa1.a")
    if not os.path.exists(lib):
        raise SystemExit("no library at %s: run without --no-snapshot" % lib)
    flags = ["-target", build_board.TRIPLES[target], "-std=gnu11",
             "-I" + os.path.join(snap, "include"), "-I" + os.path.join(snap, "src"),
             "-I" + os.path.join(snap, "src", "world"), "-I" + os.path.join(sadc, "include"),
             "-I" + os.path.join(sadc, "harness", "src"), "-I" + os.path.join(LIB, "tests"),
             "-Wall", "-Wextra", "-Wno-unused-function", "-Wno-unused-parameter", "-Wno-unused-variable",
             "-ffp-contract=off", "-frounding-math", "-fno-builtin", "-O2", "-g", "-DSA1_STATIC"]
    if target.startswith("x86-"):
        flags += ["-msse2", "-mfpmath=sse"]
    if "linux" in target:
        flags += ["-fPIC"]
    exe = os.path.join(out, name + (".exe" if "windows" in target else ""))
    cmd = [sys.executable, "-m", "ziglang", "cc", *flags, os.path.join(LIB, "tests", name + ".c"), lib, "-o", exe]
    if "linux" in target:
        cmd += ["-lm", "-lpthread"]
    r = subprocess.run(cmd, cwd=LIB, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
    if r.stdout.strip():
        print(r.stdout.rstrip())
    if r.returncode != 0:
        raise SystemExit("FAILED: " + " ".join(cmd))
    print("[%s] built %s" % (target, exe))
    return exe


def run_one(name, k, exe, args, env, timeout):
    out = os.path.join(LIB, "build", name, k, "replay")
    os.makedirs(out, exist_ok=True)
    sadc = SADC
    return ORIG_RUN(name, k, exe, ["--sadc", sadc, "--out", out] + args, env, timeout)


ORIG_RUN = build_board.run_one
SADC = os.path.join(os.path.dirname(LIB), "sadc")

if __name__ == "__main__":
    argv = sys.argv[1:]
    if "--sadc" in argv:
        SADC = argv[argv.index("--sadc") + 1]
    build_board.build = build
    build_board.run_one = run_one
    sys.exit(build_board.main("camreq_test"))
