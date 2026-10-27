#!/usr/bin/env python3
"""Build (and run) tests/hud_test.c: the HUD and the 2D screen output (round 0.9.1; sadc docs/research/gsa1_070_hud.md
F2-F6 and F7's libsa1 half, with its Review R.5).

    py -3.12 tests/build_hud.py [--target T] [--build] [--run] [--part LIST] [--gdi GDI] [--sadc DIR] [--compare-only]

    --build         run build.py --target T first (it takes libsa1's build lock itself)
    --run           run hud_test (parts 1-12; --part LIST: only these, comma-separated) with the disc and sadc's
                    references, then compare the 2D logs of part 12 with the references' NAME.d2d.txt sidecars
    --compare-only  only the comparison of the logs already in build/hud/<target>/

Outputs in build/hud/<target>/: hud_test(.exe), built against a snapshot of build/<target>/libsa1.a taken under
build/.build.lock; NAME.lib.d2d.txt (the libsa1 replay's 2D draws, the harness's format: harness/src/t_sim.c --log-2d).

The comparison (sadc docs/research/gsa1_070_refs.md 4.2): the reference's lines and the libsa1 log's, in order, with
harness/tools/draw2d.py's --lib rules (obj and the point-list pointers, pr, frame, sub, p.z and, without
NJD_SPRITE_ANGLE, ang are not compared). Draws of tasks libsa1 worlds do not run are taken out of the reference first,
and only when the libsa1 log has no draw of that exec at all (NOT_IN_LIB, each with its reason); the remaining lines
are renumbered (seq) per phase and step. Every other column must be equal.
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
REFS = ["hud_sonic_ec1", "hud_tails_sh1", "hud_knux_rm3", "hud_amy_tp2", "hud_big_ec3", "hud_gamma_fe1", "big_drown_ec3"]
# the 2D draws of tasks libsa1 worlds do not run (exec as the logs print it)
NOT_IN_LIB = {
    "0C90024C": "Emerald Coast act 1's lens flare (the STG01 level task 0C90024C: libsa1's Emerald Coast stand-in, "
                "src/world/sa1_world_stage.c, makes the ocean and footprints only)",
    "8C0197BA": "the game camera's underwater tint (njDrawPolygon2D from the camera task 8C0197BA: libsa1 worlds have the "
                "camera adapter, not the game's camera)",
    "8C07FC70": "Tails' race bar (the race opponent task 8C07FC70: libsa1 worlds have no race, sa1_world_boot.c)",
    "8C0C967A": "Knuckles' emerald radar (task 8C0C99E4 / exec 8C0C967A, made by the emerald hunt's stage start "
                "8C0C905A: not in libsa1 worlds; with the 1.0 collectables)",
}


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
    return subprocess.run(cmd, cwd=cwd).returncode


def normalise(draw2d, r, k, same_task):
    """The renderer state a draw does not use, and the state draws libsa1 worlds do not make leave behind:
    texlist  D_8C673480 BEFORE the draw (each 2D draw sets its own: njSetTexture(sp->tlist)); compared only right after a
             2D draw of the same task in the same phase and step (same_task): otherwise the draw sees what other draws
             left (the stage's land and object displays, the lens flare: stage-overlay texlists 0C9xxxxx / 0CAxxxxx in
             the references), which libsa1 worlds do not draw
    cmat_*   the constant material: used by a sprite only with NJD_SPRITE_COLOR (attr 0x02); a polygon has its own colours
    blend    used only by a translucent draw (sprite attr 0x20, polygon attr 0x40), as the effective pair: a source mode
             NJD_COLOR_BLENDING_BOTHINVALPHA / BOTHSRCALPHA (0 / 1) gives both factors, so the destination mode left by
             earlier draws (the SDK's model draws set it from their materials on the Dreamcast, libsa1's capture-only
             model draws do not) does not count (src/sa1_screen.c s_blend)"""
    k = list(k)
    cols = draw2d.COMMON + (draw2d.SPRITE if r["kind"] == "S" else draw2d.POLY)
    attr = int(r.get("attr", "0"), 16)
    def put(name, v):
        j = cols.index(name)
        if j < len(k):
            k[j] = v
    if not same_task:
        put("texlist", "*")
    if r["kind"] != "S" or not attr & 0x02:
        for c in ("cmat_a", "cmat_r", "cmat_g", "cmat_b"):
            put(c, "*")
    trans = attr & (0x20 if r["kind"] == "S" else 0x40)
    if not trans:
        put("blend", "*")
    else:
        b = int(r["blend"], 16)
        put("blend", "%02X" % (b & 0xFF) if (b & 0xFF) in (0, 1) else "%04X" % b)
    return k


def compare(sadc, out):
    sys.path.insert(0, os.path.join(sadc, "harness", "tools"))
    import draw2d  # noqa: E402  (sadc's reader of the --log-2d format)
    ignore = sorted(draw2d.LIB_IGNORE) + ["ang!"]
    bad = 0
    for name in REFS:
        ref = os.path.join(sadc, "build", "traces", "ref", name + ".d2d.txt")
        mine = os.path.join(out, name + ".lib.d2d.txt")
        if not os.path.exists(ref) or not os.path.exists(mine):
            print("FAIL %s: missing %s" % (name, ref if not os.path.exists(ref) else mine))
            bad += 1
            continue
        _, rr = draw2d.read(ref)
        _, lr = draw2d.read(mine)
        lib_execs = set(r["exec"] for r in lr)
        ref_execs = {}
        for r in rr:
            ref_execs[r["exec"]] = ref_execs.get(r["exec"], 0) + 1
        drop = {e for e in NOT_IN_LIB if e in ref_execs and e not in lib_execs}
        unexplained = sorted(e for e in ref_execs if e not in lib_execs and e not in drop)
        kept = [r for r in rr if r["exec"] not in drop]
        # renumber seq per (phase, step) after the drop
        seq, last = 0, None
        for r in kept:
            key = (r["phase"], r["step"])
            seq = 0 if key != last else seq + 1
            last = key
            r["seq"] = str(seq)
            r["_raw"] = list(r["_raw"])
            r["_raw"][draw2d.COMMON.index("seq")] = str(seq)
        ndiff, first, cols = 0, [], {}
        for i in range(max(len(kept), len(lr))):
            x = kept[i] if i < len(kept) else None
            y = lr[i] if i < len(lr) else None
            if x is None or y is None:
                ndiff += 1
                if len(first) < 6:
                    first.append("draw %d: only in %s" % (i, "the reference" if y is None else "libsa1"))
                continue
            px = kept[i - 1] if i > 0 else None
            same = px is not None and (px["phase"], px["step"], px["exec"]) == (x["phase"], x["step"], x["exec"])
            kx = normalise(draw2d, x, draw2d.key_cols(x, False, ignore), same)
            ky = normalise(draw2d, y, draw2d.key_cols(y, False, ignore), same)
            if kx != ky:
                names = draw2d.COMMON + (draw2d.SPRITE if x["kind"] == "S" else draw2d.POLY)
                d = [(names[j] if j < len(names) else "v%d" % (j - len(names)), kx[j] if j < len(kx) else "-",
                      ky[j] if j < len(ky) else "-") for j in range(max(len(kx), len(ky)))
                     if (kx[j] if j < len(kx) else None) != (ky[j] if j < len(ky) else None)]
                for c, _, _ in d:
                    cols[c] = cols.get(c, 0) + 1
                ndiff += 1
                if len(first) < 6:
                    first.append("draw %d (step %s %s seq %s exec %s): %s" % (i, x["step"], x["phase"], x["seq"], x["exec"],
                                 ", ".join("%s %s/%s" % t for t in d[:6])))
        ok = ndiff == 0 and not unexplained
        print("%s %s: reference %d draws (%s), libsa1 %d draws; %d compared, %d differ%s" % (
            "ok  " if ok else "FAIL", name, len(rr), ", ".join("%s %d" % (e, n) for e, n in sorted(ref_execs.items())),
            len(lr), len(kept), ndiff, (" (columns: %s)" % ", ".join("%s %d" % c for c in sorted(cols.items()))) if cols else ""))
        for e in sorted(drop):
            print("      taken out of the reference: exec %s, %d draws: %s" % (e, ref_execs[e], NOT_IN_LIB[e]))
        for e in unexplained:
            print("      MISSING in libsa1: exec %s (%d draws in the reference)" % (e, ref_execs[e]))
        for line in first:
            print("      " + line)
        bad += not ok
    print("2D comparison: %s (%d of %d references differ)" % ("PASS" if not bad else "FAIL", bad, len(REFS)))
    return bad


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--target", default="x86_64-windows", choices=sorted(TRIPLES))
    ap.add_argument("--build", action="store_true")
    ap.add_argument("--run", action="store_true")
    ap.add_argument("--compare-only", action="store_true")
    ap.add_argument("--part", help="hud_test --part LIST (1-12, comma-separated)")
    ap.add_argument("--gdi", default=os.environ.get("SA1_TEST_GDI"))
    ap.add_argument("--sadc", default=os.path.join(os.path.dirname(LIB), "sadc"))
    args = ap.parse_args()
    out = os.path.join(LIB, "build", "hud", args.target)
    os.makedirs(out, exist_ok=True)
    if args.compare_only:
        return 1 if compare(args.sadc, out) else 0
    if args.build and run([sys.executable, "build.py", "--target", args.target]) != 0:
        print("build.py failed")
        return 1
    lib = os.path.join(LIB, "build", args.target, "libsa1.a")
    if not os.path.exists(lib):
        print("no %s: run build.py --target %s first (or --build)" % (lib, args.target))
        return 1
    snap = os.path.join(out, "libsa1_snapshot.a")
    locked_copy(lib, snap)
    exe = ".exe" if args.target.endswith("windows") else ""
    zig = [sys.executable, "-m", "ziglang", "cc", "-target", TRIPLES[args.target]]
    extra = ["-msse2", "-mfpmath=sse"] if args.target.startswith("x86-") else []
    libs = [] if args.target.endswith("windows") else ["-lm", "-lpthread"]
    rc = run(zig + ["-std=gnu11", "-Iinclude", "-Isrc", "-Isrc/world", "-I" + os.path.join(args.sadc, "include"),
                    "-I" + os.path.join(args.sadc, "harness", "src"), "-Wall", "-Wextra", "-O2", "-g"] + WORLD_FP + extra +
             ["-DSA1_STATIC", "tests/hud_test.c", snap, "-o", os.path.join(out, "hud_test" + exe)] + libs)
    if rc:
        print("compile failed")
        return 1
    if not args.run:
        return 0
    if not args.gdi:
        print("no disc: --gdi or SA1_TEST_GDI")
        return 1
    cmd = [os.path.join(out, "hud_test" + exe), "--out", out]
    if args.part:
        cmd += ["--part", args.part]
    rc = run(cmd + [args.sadc, args.gdi])
    if not args.part or "12" in args.part.split(","):
        rc |= compare(args.sadc, out)
    return 1 if rc else 0


if __name__ == "__main__":
    sys.exit(main())
