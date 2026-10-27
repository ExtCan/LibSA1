#!/usr/bin/env python3
"""Build and run tests/hostobj_test.c (libsa1 host objects, API 0.3: sadc docs/libsa1_interaction.md 4.2 cases 1-10 with
the addendum, section 6, cases 11-17; the ring API; the contacts with moving collision objects) against a snapshot of
build/<target>/libsa1.a taken under build/.build.lock (a build running at the same time never hands over a half-written
archive).

    py -3.12 tests/build_hostobj.py [--target x86_64-windows|x86-windows|x86_64-linux|windows|all] [--run]
                                    [--gdi FILE] [--only NAME] [--verbose] [--guard-alloc] [--sadc DIR]

Run it from LIB after `py -3.12 build.py --target <target>` (or --target all). The default target is "windows"
(x86_64-windows and x86-windows). --run runs build/hostobj/<target>/hostobj_test [GDI] with the user's GD-ROM dump
(--gdi, default $SA1_TEST_GDI; never copied; without it only the registry part runs) and, when more than one target ran,
compares their "DETERMINISM <events> <arena>" and "DETERMINISM5 ..." lines (case 10, also for round 5's lift / balloon /
damage paths: x86 and x64 must give the same event streams and the same world RAM).
"""
import argparse
import os
import platform
import shutil
import subprocess
import sys
import time

LIB = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TARGETS = {"x86_64-windows": "x86_64-windows-gnu", "x86-windows": "x86-windows-gnu", "x86_64-linux": "x86_64-linux-gnu"}


def runnable(key):
    if key.endswith("windows"):
        return platform.system() == "Windows"
    return platform.system() == "Linux" and platform.machine().lower() in ("x86_64", "amd64")


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


def build(key, sadc):
    triple = TARGETS[key]
    lib = os.path.join(LIB, "build", key, "libsa1.a")
    if not os.path.exists(lib):
        raise SystemExit("%s missing: run py -3.12 build.py --target %s first" % (lib, key))
    out = os.path.join(LIB, "build", "hostobj", key)
    os.makedirs(out, exist_ok=True)
    snap = os.path.join(out, "libsa1_snapshot.a")
    locked_copy(lib, snap)
    flags = ["-target", triple, "-std=gnu11", "-Iinclude", "-Isrc", "-Isrc/world", "-Itests",
             "-I" + os.path.join(sadc, "include"), "-Wall", "-Wextra", "-Wshadow", "-Wstrict-prototypes", "-Wundef",
             "-ffp-contract=off", "-frounding-math", "-fno-builtin", "-O2", "-g", "-DSA1_STATIC"]
    if key.startswith("x86-"):
        flags += ["-msse2", "-mfpmath=sse"]
    if not key.endswith("windows"):
        flags += ["-fPIC"]
    libs = [] if key.endswith("windows") else ["-lm", "-lpthread"]
    exe = os.path.join(out, "hostobj_test" + (".exe" if key.endswith("windows") else ""))
    cmd = [sys.executable, "-m", "ziglang", "cc", *flags, os.path.join("tests", "hostobj_test.c"), snap, "-o", exe, *libs]
    r = subprocess.run(cmd, cwd=LIB, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
    if r.stdout.strip():
        print(r.stdout.rstrip())
    if r.returncode != 0:
        raise SystemExit("FAILED: " + " ".join(cmd))
    print("[%s] built %s" % (key, exe))
    return exe


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--target", default="windows", choices=sorted(TARGETS) + ["windows", "all"])
    ap.add_argument("--sadc", default=os.path.join(os.path.dirname(LIB), "sadc"))
    ap.add_argument("--gdi", default=os.environ.get("SA1_TEST_GDI", ""))
    ap.add_argument("--run", action="store_true")
    ap.add_argument("--only", help="run only this part (api, rings, inert, jump, touch, homing, churn, determinism, contacts, "
                    "damage, hostdmg, roll, armor, e102, worlds, lift, kinds, forced, balloon, mask, flags, determinism5)")
    ap.add_argument("--runs", type=int, help="forced: the number of random release / removal runs (default 1000)")
    ap.add_argument("--verbose", action="store_true")
    ap.add_argument("--guard-alloc", action="store_true", help="every library heap block against a guard page "
                    "(tests/guard_alloc.h; also SA1_GUARD_ALLOC=1)")
    a = ap.parse_args()
    if a.target == "all":
        keys = sorted(TARGETS)
    elif a.target == "windows":
        keys = ["x86_64-windows", "x86-windows"]
    else:
        keys = [a.target]
    rc = 0
    det = {}
    for k in keys:
        exe = build(k, a.sadc)
        if not (a.run and runnable(k)):
            continue
        cmd = [exe]
        if a.only:
            cmd += ["--only", a.only]
        if a.verbose:
            cmd.append("--verbose")
        if a.runs is not None:
            cmd += ["--runs", str(a.runs)]
        if a.guard_alloc:
            cmd.append("--guard-alloc")
        if a.gdi:
            cmd.append(a.gdi)
        r = subprocess.run(cmd, cwd=LIB, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
        sys.stdout.write(r.stdout)
        print("[%s] hostobj_test exit %d" % (k, r.returncode))
        rc |= r.returncode != 0
        for line in r.stdout.splitlines():
            if line.startswith("DETERMINISM"):         # DETERMINISM (case 10) and DETERMINISM5 (round 5's paths)
                det.setdefault(k, []).extend(line.split())
    if len(det) > 1:
        vals = list(det.values())
        same = all(v == vals[0] for v in vals)
        print("determinism across targets (%s): %s %s" % (", ".join(sorted(det)), "identical" if same else "DIFFERENT",
                                                         "; ".join("%s %s" % (k, " ".join(v)) for k, v in sorted(det.items()))))
        rc |= not same
    sys.exit(1 if rc else 0)


main()
