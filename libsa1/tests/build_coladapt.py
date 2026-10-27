#!/usr/bin/env python3
"""Build and run the collision adapter tests (tests/coladapt_test.c) in a private output directory.

    py -3.12 tests/build_coladapt.py [--targets T ...] [--game] [--skip-pclower] [--no-run]
                                     [--gdi GDI] [--dump PREFIX] [--sadc DIR]

Outputs in libsa1/build/coladapt/ only (the shared build/<target> directories of build.py are not
touched, so other work on libsa1 can build at the same time):

    build/coladapt/<target>/coladapt_test(.exe)        libsa1 + world core, no game code
    build/coladapt/<target>-game/coladapt_test(.exe)   --game: + the lowered collision code (pclower ga64
                                                      of COLLISION_TUS and src/rt into build/coladapt/pclower)

The test needs the landtable dump v2 (default ../sadc/build/traces/land/L01A0, see coladapt_test.c) and,
for the DISC comparisons and the game-code runs, the user's disc (--gdi or SA1_TEST_GDI).
Compiler flags: those of build.py (the lowered code: sadc/tools/pc_cflags.txt, design 8.1).
"""
import argparse
import glob
import os
import shlex
import subprocess
import sys

TESTS = os.path.dirname(os.path.abspath(__file__))
LIB = os.path.dirname(TESTS)


class B:
    """The flag sets of libsa1/build.py (kept here so that this script does not depend on build.py's
    module state while build.py changes)."""
    TARGETS = {"x86_64-windows": "x86_64-windows-gnu", "x86-windows": "x86-windows-gnu",
               "x86_64-linux": "x86_64-linux-gnu"}
    GAME_WARN = ["-Wno-unused-function", "-Wno-unused-variable", "-Wno-unused-but-set-variable",
                 "-Werror=incompatible-pointer-types", "-Werror=int-conversion",
                 "-Werror=implicit-function-declaration", "-Werror=int-to-pointer-cast", "-Werror=pointer-to-int-cast"]
    WORLD_WARN = ["-Wall", "-Wextra", "-Wshadow", "-Wstrict-prototypes", "-Wmissing-prototypes", "-Wundef"]
    WORLD_FP = ["-ffp-contract=off", "-frounding-math", "-fno-builtin"]

    @staticmethod
    def zig(*args):
        return [sys.executable, "-m", "ziglang"] + list(args)

    @staticmethod
    def runnable(key):
        import platform
        if key.endswith("windows"):
            return platform.system() == "Windows"
        return platform.system() == "Linux" and platform.machine().lower() in ("x86_64", "amd64")

    @staticmethod
    def pc_cflags(sadc, key):
        """sadc/tools/pc_cflags.txt for one target ([all], [x86], [x86_64] sections)"""
        arch = "x86" if key.startswith("x86-") else "x86_64"
        flags, active = [], True
        for line in open(os.path.join(sadc, "tools", "pc_cflags.txt"), encoding="utf-8"):
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            if line.startswith("[") and line.endswith("]"):
                secs = line[1:-1].split()
                active = "all" in secs or arch in secs
                continue
            if active:
                flags += line.split()
        return flags

# the land collision code the test runs: init / update 8C028E60 / 8C028EC2, polygon build 8C028B00 /
# 8C0287A0 / 8C028666, list helpers 8C02CE80-8C02D00E, gathers 8C052518 / 8C052E30 (+ the runtime)
COLLISION_TUS = ["code_8C028E60.c", "code_8C028620.c", "code_8C02CE80.c", "code_8C02CEA0.c", "code_8C02CEF4.c",
                 "code_8C02CF20.c", "code_8C02CF68.c", "code_8C02CF70.c", "code_8C02CFA4.c", "code_8C02CFC0.c",
                 "code_8C052518.c", "code_8C052E30.c"]


def run(cmd, cwd=LIB, show=True):
    r = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, encoding="utf-8", errors="replace")
    out = (r.stdout + r.stderr).strip()
    if show and out:
        print(out)
    return r.returncode, out


def compile_all(jobs):
    import concurrent.futures
    ok = True
    with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as ex:
        for (src, cmd), (rc, out) in zip(jobs, ex.map(lambda j: run(j[1], show=False), jobs)):
            if out:
                print(out)
            if rc != 0:
                print("FAILED:", src)
                ok = False
    return ok


def lower(sadc, out, extra):
    files = [os.path.join(sadc, "src", "1st_read", t) for t in COLLISION_TUS]
    files += [p for p in sorted(glob.glob(os.path.join(LIB, "src", "rt", "*.c"))) if not p.endswith("rt_fsca_q.c")]
    cmd = [sys.executable, os.path.join(sadc, "tools", "pclower.py"), "--profile", "ga64", "--stub-unresolved",
           "-j", "8", "--out", out, "--inc", os.path.join(LIB, "src", "rt")] + shlex.split(extra) + files
    print("== pclower ga64: %d TUs -> %s" % (len(files), os.path.relpath(out, LIB)))
    rc, _ = run(cmd, cwd=sadc)
    if rc != 0:
        raise SystemExit("pclower failed (%d)" % rc)
    return os.path.join(out, "ga64")


def build(key, sadc, game_dir, outdir):
    triple = B.TARGETS[key]
    windows = key.endswith("windows")
    x86 = key.startswith("x86-")
    od = os.path.join(outdir, "obj")
    os.makedirs(od, exist_ok=True)
    inc_sadc = os.path.join(sadc, "include")
    cflags = ["-target", triple, "-std=c99", "-Iinclude", "-Isrc", "-Wall", "-Wextra", "-Wpedantic", "-Wshadow",
              "-Wstrict-prototypes", "-Wmissing-prototypes", "-Wvla", "-Wcast-qual", "-Wpointer-arith", "-Wundef",
              "-O2", "-g", "-DSA1_STATIC"]
    wflags = (["-target", triple, "-std=gnu11", "-Iinclude", "-Isrc", "-Isrc/world", "-I" + inc_sadc] + B.WORLD_WARN
              + B.WORLD_FP + ["-O2", "-g", "-DSA1_STATIC"])
    if x86:
        wflags += ["-msse2", "-mfpmath=sse"]
    if not windows:
        cflags += ["-fPIC"]
        wflags += ["-fPIC"]
    jobs = []
    for s in sorted(glob.glob(os.path.join(LIB, "src", "*.c"))):
        o = os.path.join(od, "lib_" + os.path.basename(s)[:-2] + ".o")
        jobs.append((s, B.zig("cc", *cflags, "-c", s, "-o", o)))
    world = sorted(glob.glob(os.path.join(LIB, "src", "world", "*.c"))) + [os.path.join(LIB, "src", "rt", "rt_fsca_q.c")]
    if game_dir:
        world = [s for s in world if not s.endswith("sa1_world_nogame.c")]
    for s in world:
        o = os.path.join(od, "world_" + os.path.basename(s)[:-2] + ".o")
        jobs.append((s, B.zig("cc", *wflags, "-c", s, "-o", o)))
    if game_dir:
        gflags = (["-target", triple] + B.pc_cflags(sadc, key) + ["-std=gnu11", "-g", "-I" + inc_sadc, "-I" + game_dir,
                  "-Isrc/rt", "-DSA1G_TEXT_PRAGMA", "-include", "src/world/sa1g_text.h"] + B.GAME_WARN)
        if not windows:
            gflags += ["-fPIC"]
        srcs = sorted(glob.glob(os.path.join(game_dir, "lowered", "*.c")))
        srcs += [os.path.join(game_dir, f) for f in ("sa1g_fntab.c", "sa1g_stubs.c", "sa1g_pcdata.c")
                 if os.path.exists(os.path.join(game_dir, f))]
        info = os.path.join(od, "sa1g_game_info.c")
        with open(info, "w", encoding="utf-8", newline="\n") as f:
            f.write("/* Generated by tests/build_coladapt.py. */\nint sa1g_game_linked(void);\n"
                    "const char *sa1g_game_info(void);\nint sa1g_game_linked(void)\n{\n    return 1;\n}\n\n"
                    "const char *sa1g_game_info(void)\n{\n    return \"lowered land collision code (%d TUs + rt)\";\n}\n"
                    % len(COLLISION_TUS))
        srcs.append(info)
        if not os.path.exists(os.path.join(game_dir, "sa1g_pcdata.c")):
            pcd = os.path.join(od, "sa1g_pcdata_none.c")
            with open(pcd, "w", encoding="utf-8", newline="\n") as f:
                f.write("#include <stdint.h>\nuint32_t sa1g_pcd_size(void);\nvoid sa1g_pcd_init(void);\n"
                        "uint32_t sa1g_pcd_size(void)\n{\n    return 0u;\n}\n\nvoid sa1g_pcd_init(void)\n{\n}\n")
            srcs.append(pcd)
        for s in srcs:
            o = os.path.join(od, "game_" + os.path.basename(s)[:-2] + ".o")
            jobs.append((s, B.zig("cc", *gflags, "-c", s, "-o", o)))
    tflags = [f for f in wflags if f not in ("-Wmissing-prototypes",)]
    t = os.path.join(LIB, "tests", "coladapt_test.c")
    jobs.append((t, B.zig("cc", *tflags, "-c", t, "-o", os.path.join(od, "coladapt_test.o"))))
    print("== %s: %d objects -> %s" % (key, len(jobs), os.path.relpath(outdir, LIB)))
    if not compile_all(jobs):
        return None
    exe = os.path.join(outdir, "coladapt_test" + (".exe" if windows else ""))
    objs = [j[1][j[1].index("-o") + 1] for j in jobs]
    libs = [] if windows else ["-lm", "-lpthread"]
    rc, _ = run(B.zig("cc", "-target", triple, "-o", exe, *objs, *libs))
    return exe if rc == 0 else None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--targets", nargs="+", default=["x86_64-windows", "x86-windows", "x86_64-linux"],
                    choices=sorted(B.TARGETS))
    ap.add_argument("--game", action="store_true", help="also link the lowered land collision code")
    ap.add_argument("--skip-pclower", action="store_true", help="reuse build/coladapt/pclower")
    ap.add_argument("--pclower-args", default="")
    ap.add_argument("--no-run", action="store_true")
    ap.add_argument("--gdi", default=os.environ.get("SA1_TEST_GDI"))
    ap.add_argument("--dump", default=None, help="landtable dump prefix (default ../sadc/build/traces/land/L01A0)")
    ap.add_argument("--sadc", default=os.path.join(os.path.dirname(LIB), "sadc"))
    a = ap.parse_args()
    sadc = os.path.abspath(a.sadc)
    root = os.path.join(LIB, "build", "coladapt")
    game_dir = None
    if a.game:
        pcl = os.path.join(root, "pclower")
        game_dir = os.path.join(pcl, "ga64") if a.skip_pclower else lower(sadc, pcl, a.pclower_args)
    dump = a.dump or os.path.join(sadc, "build", "traces", "land", "L01A0")
    fail = False
    results = []
    for key in a.targets:
        outdir = os.path.join(root, key + ("-game" if a.game else ""))
        exe = build(key, sadc, game_dir, outdir)
        if exe is None:
            results.append("%s: build FAILED" % key)
            fail = True
            continue
        if a.no_run or not B.runnable(key):
            results.append("%s: built" % key)
            continue
        print("== run %s" % os.path.relpath(exe, LIB))
        r = subprocess.run([exe, a.gdi or "-", dump], cwd=LIB)
        results.append("%s: %s" % (key, "PASS" if r.returncode == 0 else "FAIL (%d)" % r.returncode))
        fail |= r.returncode != 0
    print("coladapt: " + ", ".join(results))
    return 1 if fail else 0


if __name__ == "__main__":
    sys.exit(main())
