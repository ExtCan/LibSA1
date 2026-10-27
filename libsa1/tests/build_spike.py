#!/usr/bin/env python3
"""Build and run the GUEST-ARENA spike / world tests (sadc docs/libsa1_integration.md sections 6, 11).

    py -3.12 tests/build_spike.py [--sadc DIR] [--tag spike] [--no-run] [--skip-pclower] [--targets ...]
                                  [--pclower-args "..."]

Steps (outputs in libsa1/build/spike/ and libsa1/build/<target>/; the shared harness exes are untouched):

  1. sadc/tools/pclower.py on the three spike TUs, profiles ga32 + ga64 -> build/spike/pclower/
  2. a private interpreter-only harness build (harness/build.py --files none --tag <tag>), for the
     interpreter objects (harness/build/obj_tag_<tag>/)
  3. spike_ref.exe (x86, LARGE_ADDRESS_AWARE): tests/spike_test.c -DSPIKE_REF -DSPIKE_GA32 + the
     interpreter objects + the lowered TUs of profile ga32. Runs the ORIGINAL functions in the
     interpreter (reference RAM dumps build/spike/ref_*), then the same sequence through the ga32
     lowered code
  4. LIB/build.py --lowered build/spike/pclower/ga64 for x86_64-windows, x86-windows, x86_64-linux:
     libsa1 with the world core and the lowered code, and spike_test (the world tests)
  5. runs spike_ref, then spike_test on the Windows targets (SA1_TEST_GDI=<game.gdi> is passed through:
     the world image is then composed from the disc)
"""
import argparse
import glob
import os
import shlex
import struct
import subprocess
import sys

TESTS = os.path.dirname(os.path.abspath(__file__))
LIB = os.path.dirname(TESTS)
ZIG = [sys.executable, "-m", "ziglang", "cc"]
SPIKE_TUS = ["src/1st_read/code_8C098444.c", "src/1st_read/code_8C098998.c", "src/1st_read/code_8C098B0C.c"]
GAME_CFLAGS = ["-O2", "-g", "-std=gnu11", "-ffp-contract=off", "-fno-strict-aliasing", "-fwrapv",
               "-frounding-math", "-fno-builtin", "-fno-math-errno", "-fsigned-char",
               "-Wall", "-Wno-unused-function", "-Wno-unused-parameter",
               "-Werror=incompatible-pointer-types", "-Werror=int-conversion", "-Werror=implicit-function-declaration"]
LIB_CFLAGS = ["-O2", "-g", "-std=gnu11", "-Wall", "-Wextra", "-Wno-unused-function"]
PCD_NONE = ("/* build_spike.py: the lowered code has no R4 pcdata */\n#include <stdint.h>\n"
            "uint32_t sa1g_pcd_size(void);\nvoid sa1g_pcd_init(void);\n"
            "uint32_t sa1g_pcd_size(void)\n{\n    return 0u;\n}\n\nvoid sa1g_pcd_init(void)\n{\n}\n")


def run(cmd, cwd=None, check=True, show=True, env=None):
    r = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, encoding="utf-8", errors="replace", env=env)
    out = (r.stdout + r.stderr).strip()
    if show and out:
        print(out)
    if check and r.returncode:
        print("FAILED (%d): %s" % (r.returncode, " ".join(cmd)))
        sys.exit(1)
    return r.returncode, out


def cc(src, obj, flags):
    run(ZIG + flags + ["-c", src, "-o", obj])
    return obj


def set_laa(exe):
    b = bytearray(open(exe, "rb").read())
    e = struct.unpack_from("<I", b, 0x3C)[0]
    off = e + 4 + 18
    struct.pack_into("<H", b, off, struct.unpack_from("<H", b, off)[0] | 0x0020)
    open(exe, "wb").write(b)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sadc", default=os.path.join(os.path.dirname(LIB), "sadc"))
    ap.add_argument("--tag", default="spike")
    ap.add_argument("--no-run", action="store_true")
    ap.add_argument("--skip-pclower", action="store_true")
    ap.add_argument("--targets", nargs="+", default=["x86_64-windows", "x86-windows", "x86_64-linux"])
    ap.add_argument("--pclower-args", default="", help="more pclower options, e.g. \"--allow-unpinned\"")
    a = ap.parse_args()
    sadc = os.path.abspath(a.sadc)
    out = os.path.join(LIB, "build", "spike")
    pcl = os.path.join(out, "pclower")
    os.makedirs(os.path.join(out, "obj32"), exist_ok=True)
    rom = os.path.join(sadc, "baserom", "1ST_READ.BIN")
    inc = os.path.join(sadc, "include")

    # 1. lowering (into libsa1/build/spike/pclower: other agents own sadc/build/pclower)
    if not a.skip_pclower:
        print("== pclower (ga32 + ga64)")
        run([sys.executable, os.path.join(sadc, "tools", "pclower.py"), "--stub-unresolved", "--out", pcl,
             "--allow-conflict", "CreateElementalTask"] + shlex.split(a.pclower_args)
            + [os.path.join(sadc, f) for f in SPIKE_TUS], cwd=sadc)

    # 2. private interpreter-only harness build
    print("== harness (interpreter only, tag %s)" % a.tag)
    run([sys.executable, os.path.join(sadc, "harness", "build.py"), "--files", "none", "--tag", a.tag], show=False)
    hobj = os.path.join(sadc, "harness", "build", "obj_tag_" + a.tag)
    hsrc = os.path.join(sadc, "harness", "src")

    # 3. reference (x86): interpreter + ga32 lowered code
    print("== spike_ref (x86: interpreter reference + ga32 lowered pass)")
    t32 = ["-target", "x86-windows-gnu", "-msse2", "-mfpmath=sse"]
    # every object of the private build but the command-line front end (main.c, t_*.c)
    objs = sorted(o for o in glob.glob(os.path.join(hobj, "*.o"))
                  if os.path.basename(o) != "main.o" and not os.path.basename(o).startswith("t_"))
    objs.append(cc(os.path.join(TESTS, "spike_test.c"), os.path.join(out, "obj32", "spike_ref.o"),
                   t32 + LIB_CFLAGS + ["-DSPIKE_REF", "-DSPIKE_GA32", "-DSA1_GA32", "-I", hsrc, "-I", inc]))
    g32 = os.path.join(pcl, "ga32")
    srcs = sorted(glob.glob(os.path.join(g32, "lowered", "*.c")))
    srcs += [os.path.join(g32, f) for f in ("sa1g_fntab.c", "sa1g_stubs.c", "sa1g_pcdata.c")
             if os.path.exists(os.path.join(g32, f))]
    if not os.path.exists(os.path.join(g32, "sa1g_pcdata.c")):
        p = os.path.join(out, "obj32", "sa1g_pcdata_none.c")
        open(p, "w", encoding="utf-8", newline="\n").write(PCD_NONE)
        srcs.append(p)
    for src in srcs:
        objs.append(cc(src, os.path.join(out, "obj32", os.path.basename(src)[:-2] + ".o"),
                       t32 + GAME_CFLAGS + ["-DSA1_GA32", "-I", inc, "-I", g32]))
    ref_exe = os.path.join(out, "spike_ref.exe")
    run(ZIG + ["-target", "x86-windows-gnu", "-o", ref_exe] + objs)
    set_laa(ref_exe)

    # 4. libsa1 (world core + ga64 lowered code) and the world tests, per target
    for t in a.targets:
        print("== libsa1 %s with the lowered spike code" % t)
        run([sys.executable, os.path.join(LIB, "build.py"), "--target", t, "--game", sadc,
             "--lowered", os.path.join(pcl, "ga64")], cwd=LIB)

    if a.no_run:
        return 0
    prefix = os.path.join(out, "ref")
    print("== run spike_ref")
    rc, _ = run([ref_exe, rom, prefix], check=False)
    results = ["reference %s" % ("ok" if rc == 0 else "FAILED (%d)" % rc)]
    fail = rc != 0
    for t in a.targets:
        if not t.endswith("windows") or sys.platform != "win32":
            results.append("%s compiled (not run here)" % t)
            continue
        print("== run spike_test (%s)" % t)
        r, _ = run([os.path.join(LIB, "build", t, "spike_test.exe"), rom, prefix], check=False)
        results.append("%s %s" % (t, "PASS" if r == 0 else "FAILED (%d)" % r))
        fail |= r != 0
    print("spike: " + ", ".join(results))
    return 1 if fail else 0


if __name__ == "__main__":
    sys.exit(main())
