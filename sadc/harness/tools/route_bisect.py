#!/usr/bin/env python3
"""Find the decompiled function(s) whose native (routed) version makes a
reference-simulator trace diverge from the original game.

    py -3.12 harness/tools/route_bisect.py [--exe harness/build/sa1h.exe] -- <trace options>

e.g.
    py -3.12 harness/tools/route_bisect.py -- --level 1 --act 0 --no-objects \\
        --input "30 0 0; 90 0 -127; 1 0 -127 A; 60 0 -127"

Runs the pure reference once, then `trace --route-only <subset> --check ref.bin`
on halves of the routed set, keeping the half that reproduces the EARLIEST
divergence of the fully routed run (same step and field), until a single
function is left (traces go to build/traces/bisect_*).  If the divergence
needs functions from both halves, the remaining candidate set is printed.
Further culprits: pass --route-except <found> in the trace options and re-run.
"""
import argparse
import os
import re
import subprocess
import sys

HARNESS = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROOT = os.path.dirname(HARNESS)


def run(exe, args):
    r = subprocess.run([exe, "trace"] + args + ["--print", "0"], capture_output=True, text=True)
    return r.returncode, r.stdout + r.stderr


def natives(exe):
    out = subprocess.run([exe, "list"], capture_output=True, text=True).stdout
    fns = []
    for line in out.splitlines():
        if line.startswith("  ") and "(no adapter)" not in line:
            m = re.match(r"\s+([0-9A-F]{8})\s+(\S+)", line)
            if m and m.group(1) != "00000000":
                fns.append((m.group(1), m.group(2)))
        if "thunks" in line:
            break
    return fns


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", default=os.path.join(HARNESS, "build", "sa1h.exe"))
    ap.add_argument("--decoded", action="store_true",
                    help="compare only the decoded player fields (--check-decoded), not the raw blobs")
    ap.add_argument("rest", nargs=argparse.REMAINDER)
    a = ap.parse_args()
    a.exe = os.path.abspath(a.exe)
    targs = [x for x in a.rest if x != "--"]
    out = os.path.join(ROOT, "build", "traces")
    ref = os.path.join(out, "bisect_ref")
    ref_args, k = [], 0
    while k < len(targs):                       # the reference is the pure original: no routing options
        if targs[k] in ("--route-except", "--route-only"):
            k += 2
            continue
        if targs[k] not in ("--route", "--route-all"):
            ref_args.append(targs[k])
        k += 1
    rc, txt = run(a.exe, ref_args + ["--out", ref])
    if rc:
        print(txt)
        print("reference run failed")
        return 1
    fns = natives(a.exe)
    if "--route-except" in targs:
        skip = set(targs[targs.index("--route-except") + 1].split(","))
        fns = [f for f in fns if f[0] not in skip and f[1] not in skip]
    print("%d routable native functions" % len(fns))

    def signature(subset):
        rc, txt = run(a.exe, targs + ["--route-only", ",".join(f[0] for f in subset),
                                      "--out", os.path.join(out, "bisect_try"),
                                      "--check-decoded" if a.decoded else "--check", ref + ".bin"])
        if rc == 0:
            return None, []
        lines = txt.splitlines()
        m = [re.match(r"\s+step (\d+): (\w+)", l) for l in lines]
        m = [x for x in m if x]
        info = [l for l in lines if l.startswith("  step") or l.startswith("check:") or "fault" in l][:3]
        if not m:
            return ("fault",), info
        return (int(m[0].group(1)), m[0].group(2)), info

    target, info = signature(fns)
    if target is None:
        print("routing all %d functions reproduces the reference trace: nothing to bisect" % len(fns))
        return 0
    print("full routing diverges first at %s: %s" % (target, " | ".join(info)))
    print("bisecting for the function(s) that cause this earliest divergence")
    cand = fns
    while len(cand) > 1:
        half = len(cand) // 2
        lo, hi = cand[:half], cand[half:]
        s1, i1 = signature(lo)
        if s1 == target:
            cand = lo
            print("  %3d candidates (first half reproduces it)" % len(cand))
            continue
        s2, i2 = signature(hi)
        if s2 == target:
            cand = hi
            print("  %3d candidates (second half reproduces it)" % len(cand))
            continue
        print("neither half alone reproduces it (first half: %s, second half: %s);" % (s1, s2))
        print("it needs functions from both halves of this set:")
        for f in cand:
            print("  %s %s" % f)
        return 2
    sig, info = signature(cand)
    print("culprit: %s %s -> %s" % (cand[0][0], cand[0][1], " | ".join(info)))
    print("re-run with --route-except %s to look for the next divergence" % cand[0][0])
    return 0


if __name__ == "__main__":
    sys.exit(main())