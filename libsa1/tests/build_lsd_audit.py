#!/usr/bin/env python3
"""Build and run tests/lsd_audit.c, the round 4c regression of the capture emitter (sadc docs/research/
lightspeed_visuals.md 5.3 / 6.3; plan gsa1_rounds_plan.md 6.1): 27 move scripts of the six characters with all their
upgrades, every model draw and every 3D sprite record of every pair dumped, compared with a reference dump made by
the library BEFORE a change.

    py -3.12 tests/build_lsd_audit.py --ref [--lib ARCHIVE | --pristine DIR] [--target T] [--gdi GDI] [--sadc DIR]
    py -3.12 tests/build_lsd_audit.py --run [--target T] [--gdi GDI] [--sadc DIR]

--ref       build lsd_audit, run it and keep its dump as the reference build/lsd_audit/ref/audit.txt (local; never
            committed: it is made from the user's disc). The library is a snapshot of build/<target>/libsa1.a taken
            under build/.build.lock, or
              --lib ARCHIVE    a given archive (e.g. a copy of libsa1.a saved before the change), or
              --pristine DIR   the current archive with its sa1_capture / sa1_pose members replaced by DIR's
                               sa1_capture.c / sa1_pose.c (+ sa1_capture.h): the emitter as it was, the rest as it is
--run       build lsd_audit against the current archive, run it into build/lsd_audit/out/audit.txt and compare it with
            the reference. Round 4c's rule (exit 1 when it is broken):
              D lines (model draws): identical, except the Light Speed aura's (Sonic, "charge", material 0x26182400 in
                                     the new dump); the number of those is printed (10 expected: pairs 75-84);
              P lines (3D sprites):  identical for scaled sprites (attr 0x10), except a zero-size one that is no longer
                                     emitted; unscaled ones may change (their size is the game's now);
              no line may be missing or new among D / P; no "unmapped" or "X" line.
            Q / R lines (polygons, lines, summaries) are compared for information only.
Round 0.9.2a (FX3, the item box power-up models drawn by 8C621574): lsd_audit.c gained Sonic's scripts "barrier",
"invincible" and "magnetic", and the reference was re-taken with the FX3 library for those documented new draws only:
the new reference without these three scripts' lines (and with the E line's script count 24) is line for line the
previous one, kept as build/lsd_audit/ref/audit_pre092a.txt.
The disc: --gdi or $SA1_TEST_GDI (never copied).
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import time

TESTS = os.path.dirname(os.path.abspath(__file__))
LIB = os.path.dirname(TESTS)
TRIPLES = {"x86_64-windows": "x86_64-windows-gnu", "x86-windows": "x86-windows-gnu", "x86_64-linux": "x86_64-linux-gnu"}
WORLD_FP = ["-ffp-contract=off", "-frounding-math", "-fno-builtin"]
AURA_MAT = "mat 0x26182400"


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


def build(args, out, lib_path, pristine):
    exe = os.path.join(out, "lsd_audit" + (".exe" if args.target.endswith("windows") else ""))
    snap = os.path.join(out, "libsa1_snapshot.a")
    if lib_path:
        shutil.copyfile(lib_path, snap)
    else:
        lib = os.path.join(LIB, "build", args.target, "libsa1.a")
        if not os.path.exists(lib):
            print("no %s: run build.py --target %s first" % (lib, args.target))
            return None
        locked_copy(lib, snap)
    zig = [sys.executable, "-m", "ziglang"]
    triple = TRIPLES[args.target]
    extra = ["-msse2", "-mfpmath=sse"] if args.target.startswith("x86-") else []
    srcs = [os.path.join("tests", "lsd_audit.c")]
    inc = []
    if pristine:
        # the archive without its emitter members, the given sources compiled ahead of it (their header first)
        members = subprocess.run(zig + ["ar", "t", snap], capture_output=True, text=True).stdout.split()
        drop = [m for m in members if os.path.splitext(os.path.basename(m))[0] in ("sa1_capture", "sa1_pose")]
        if len(drop) != 2:
            print("archive members of sa1_capture / sa1_pose not found: %s" % drop)
            return None
        if run(zig + ["ar", "d", snap] + drop) != 0:
            return None
        inc = ["-I" + os.path.abspath(pristine)]
        for name in ("sa1_capture.c", "sa1_pose.c"):
            obj = os.path.join(out, "pristine_" + name[:-2] + ".o")
            if run(zig + ["cc", "-target", triple, "-std=c99"] + inc + ["-Iinclude", "-Isrc", "-O2", "-g"] + extra +
                   ["-c", os.path.join(os.path.abspath(pristine), name), "-o", obj]) != 0:
                return None
            srcs.append(obj)
    cmd = zig + ["cc", "-target", triple, "-std=gnu11"] + inc + ["-Iinclude", "-Isrc", "-Isrc/world",
                                                                 "-I" + os.path.join(args.sadc, "include"),
                                                                 "-I" + os.path.join(args.sadc, "harness", "src"),
                                                                 "-Wall", "-Wextra", "-O2", "-g"] + WORLD_FP + extra + \
        ["-DSA1_STATIC"] + srcs + [snap, "-o", exe]
    if not args.target.endswith("windows"):
        cmd += ["-lm", "-lpthread"]
    if run(cmd) != 0:
        print("compile failed")
        return None
    return exe


def parse(path):
    lines = {}
    other = []
    with open(path, encoding="utf-8", errors="replace") as f:
        for ln in f:
            ln = ln.rstrip("\n")
            parts = ln.split("|")
            if parts[0] in ("D", "P") and len(parts) >= 6:
                lines["|".join(parts[:5])] = ln
            else:
                other.append(ln)
    return lines, other


def sprite_size(ln):
    m = re.search(r"\|w (\S+) h (\S+)$", ln)
    return (float(m.group(1)), float(m.group(2))) if m else None


def compare(ref_path, new_path):
    ref, ref_other = parse(ref_path)
    new, new_other = parse(new_path)
    fails = []
    aura = sprite_changed = sprite_dropped = 0
    missing = sorted(set(ref) - set(new))
    added = sorted(set(new) - set(ref))
    for k in missing[:10]:
        fails.append("missing in the new dump: " + ref[k])
    for k in added[:10]:
        fails.append("new in the new dump: " + new[k])
    if len(missing) > 10 or len(added) > 10:
        fails.append("... %d missing, %d new lines in all" % (len(missing), len(added)))
    for k in sorted(set(ref) & set(new)):
        a, b = ref[k], new[k]
        if "unmapped" in b:
            fails.append("unmapped sprites: " + b)
            continue
        if a == b:
            continue
        kind, who, script = k.split("|")[:3]
        if kind == "D":
            if who.lower() == "sonic" and script == "charge" and AURA_MAT in b:
                aura += 1
                continue
            fails.append("model draw changed:\n      before " + a + "\n      after  " + b)
            continue
        attr = int(re.search(r"attr 0x([0-9A-Fa-f]+)", b).group(1), 16)
        if not attr & 0x10:
            sprite_changed += 1
            continue
        sa = sprite_size(a)
        if b.endswith("|-") and sa is not None and (sa[0] == 0.0 or sa[1] == 0.0):
            sprite_dropped += 1
            continue
        fails.append("scaled sprite changed:\n      before " + a + "\n      after  " + b)
    for ln in new_other:
        if ln.startswith("X|"):
            fails.append("error line: " + ln)
    info_changed = len(set(ref_other) ^ set(new_other))
    print("lsd_audit: %d D/P lines; %d aura draws changed (Sonic charge, material 0x26182400), %d unscaled sprite "
          "records changed, %d zero-size scaled sprites dropped; %d Q/R/E lines differ (information)" %
          (len(new), aura, sprite_changed, sprite_dropped, info_changed))
    for ln in sorted(set(ref_other) ^ set(new_other)):
        if ln.startswith("R|"):
            print("  (information) %s %s" % ("before" if ln in ref_other else "after ", ln))
    if fails:
        for f in fails[:40]:
            print("  FAIL  " + f)
        print("FAIL: %d difference(s) outside round 4c's rule" % len(fails))
        return 1
    print("PASS: only the allowed differences")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--target", default="x86_64-windows", choices=sorted(TRIPLES))
    ap.add_argument("--ref", action="store_true")
    ap.add_argument("--run", action="store_true")
    ap.add_argument("--lib", help="--ref: the archive to use")
    ap.add_argument("--pristine", help="--ref: a directory with the emitter sources before the change")
    ap.add_argument("--gdi", default=os.environ.get("SA1_TEST_GDI"))
    ap.add_argument("--sadc", default=os.path.join(os.path.dirname(LIB), "sadc"))
    args = ap.parse_args()
    if args.ref == args.run:
        print("give --ref or --run")
        return 2
    if not args.gdi:
        print("no disc: --gdi or SA1_TEST_GDI")
        return 1
    base = os.path.join(LIB, "build", "lsd_audit")
    out = os.path.join(base, "ref" if args.ref else "out")
    os.makedirs(out, exist_ok=True)
    exe = build(args, out, args.lib if args.ref else None, args.pristine if args.ref else None)
    if exe is None:
        return 1
    dump = os.path.join(out, "audit.txt")
    if run([exe, args.gdi, "--out", dump]) != 0:
        print("lsd_audit failed")
        return 1
    if args.ref:
        print("reference: %s" % dump)
        return 0
    ref = os.path.join(base, "ref", "audit.txt")
    if not os.path.exists(ref):
        print("no reference %s: make it with --ref from the library before the change" % ref)
        return 1
    return compare(ref, dump)


if __name__ == "__main__":
    sys.exit(main())
