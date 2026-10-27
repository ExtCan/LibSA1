#!/usr/bin/env python3
"""Route each native decompiled function ALONE into every sample trace and
record whether the trace still reproduces the original (per-function
isolation; complements route_bisect.py, which follows one divergence at a time).

    py -3.12 harness/tools/route_matrix.py [--exe harness/build/sa1h_nm_pc.exe] [-j 14]
        [--scripts harness/tests/sim/*.txt] [--only F,..] [--out build/verify/route_matrix.json]

For every (function, script): `trace --route-only F --check build/traces/ref/<script>.bin`.
Result per pair: "ok", or "step N field" of the first difference, or "fault: ...".
References come from tests/sim/regress.py (pure original; record them first).  They are
version-2 traces (harness/src/sa1_trace.h); a build that only writes version 1 is checked
against <script>.v1.bin (sa1trace.reference_for).  Each run uses the reference's screen mode
(header flag 8: with the screen since 2026-10-20; regress.py does the same).
Prints the functions that break at least one trace, earliest step first.
"""
import argparse
import concurrent.futures
import glob
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
HARNESS = os.path.dirname(HERE)
ROOT = os.path.dirname(HARNESS)
sys.path.insert(0, HERE)
from route_bisect import natives  # noqa: E402
import sa1trace  # noqa: E402


def script_args(path):
    first = open(path).readline()
    if first.startswith("# args:"):
        return first[len("# args:"):].split()
    return ["--level", "1", "--act", "0", "--no-objects"]


REFDIR = os.path.join(ROOT, "build", "traces", "ref")


def one(job):
    exe, fn, script, idx = job
    name = os.path.splitext(os.path.basename(script))[0]
    ref = sa1trace.reference_for(exe, REFDIR, name) or os.path.join(REFDIR, name + ".bin")
    out = os.path.join(ROOT, "build", "traces", "matrix", "w%d" % idx)
    sargs = []
    if os.path.exists(ref) and sa1trace.version_of(ref) >= 2:
        sargs = sa1trace.screen_args(exe, bool(sa1trace.flags_of(ref) & sa1trace.FLAG_SCREEN))
        if sargs is None:
            return fn, name, "error: the reference has the screen (flag 8), this build cannot set it up"
    cmd = [exe, "trace", "--input", "@" + script, "--print", "0"] + script_args(script) + sargs + \
        ["--route-only", fn, "--out", out, "--check", ref]
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=300)
    except subprocess.TimeoutExpired:
        return fn, name, "hang (300 s)"
    if r.returncode == 0:
        return fn, name, "ok"
    txt = r.stdout + r.stderr
    for l in txt.splitlines():
        m = re.match(r"\s+step (\d+): (.*)", l)
        if m:
            return fn, name, "step %s %s" % (m.group(1), m.group(2)[:120])
    for l in txt.splitlines():
        if "fault" in l:
            return fn, name, "fault: " + l.strip()[:160]
    return fn, name, "error: " + " | ".join(txt.strip().splitlines()[-2:])[:200]


def main():
    global REFDIR
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", default=os.path.join(HARNESS, "build", "sa1h_nm_pc.exe"))
    ap.add_argument("-j", type=int, default=max(1, (os.cpu_count() or 4) - 2))
    ap.add_argument("--scripts", nargs="*")
    ap.add_argument("--only", help="comma-separated function names/addresses")
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "verify", "route_matrix.json"))
    ap.add_argument("--refdir", default=REFDIR, help="reference traces (default build/traces/ref)")
    ap.add_argument("--coverage", default=os.path.join(ROOT, "build", "verify", "coverage"),
                    help="directory of <script>.txt files from 'trace --coverage' (pure runs): only pairs where "
                         "the function runs are tested ('-' to test all pairs)")
    a = ap.parse_args()
    REFDIR = os.path.abspath(a.refdir)
    exe = os.path.abspath(a.exe)
    print("references: %s (this build writes trace version %d)" % (REFDIR, sa1trace.exe_trace_version(exe)))
    scripts = a.scripts or sorted(glob.glob(os.path.join(HARNESS, "tests", "sim", "*.txt")))
    fns = natives(exe)
    if a.only:
        want = set(a.only.split(","))
        fns = [f for f in fns if f[0] in want or f[1] in want]
    os.makedirs(os.path.join(ROOT, "build", "traces", "matrix"), exist_ok=True)
    os.makedirs(os.path.dirname(a.out), exist_ok=True)
    cov = {}
    if a.coverage != "-":
        for s in scripts:
            n = os.path.splitext(os.path.basename(s))[0]
            cp = os.path.join(a.coverage, n + ".txt")
            if not os.path.exists(cp):
                sys.exit("no coverage file %s (run trace --coverage, or pass --coverage -)" % cp)
            cov[n] = {l.split()[0] for l in open(cp) if l.strip()}
    jobs = []
    skipped = 0
    for addr, name in fns:
        for s in scripts:
            n = os.path.splitext(os.path.basename(s))[0]
            if cov and addr not in cov[n]:
                skipped += 1
                continue
            jobs.append((exe, addr, s))
    print("%d (function, script) pairs to run, %d skipped (function not executed by the script)" % (len(jobs), skipped))
    names = dict(fns)
    done = 0
    res = {}
    with concurrent.futures.ThreadPoolExecutor(a.j) as ex:
        for fn, sname, verdict in ex.map(one, [(exe, fn, s, i) for i, (exe, fn, s) in enumerate(jobs)]):
            res.setdefault("%s %s" % (fn, names[fn]), {})[sname] = verdict
            done += 1
            if done % 200 == 0:
                print("%d/%d" % (done, len(jobs)), flush=True)
    json.dump(res, open(a.out, "w"), indent=1, sort_keys=True)
    bad = []
    for k, per in res.items():
        steps = [int(v.split()[1]) for v in per.values() if v.startswith("step ")]
        other = [v for v in per.values() if v != "ok" and not v.startswith("step ")]
        if steps or other:
            bad.append((min(steps) if steps else -1, k, per))
    bad.sort()
    print("%d functions x %d scripts (%d pairs run): %d functions break at least one trace"
          % (len(fns), len(scripts), len(jobs), len(bad)))
    for first, k, per in bad:
        brief = ", ".join("%s:%s" % (s, v.split(":")[0] if v.startswith("fault") else " ".join(v.split()[:3]))
                          for s, v in sorted(per.items()) if v != "ok")
        print("  %-36s %s" % (k, brief[:220]))
    return 0


if __name__ == "__main__":
    sys.exit(main())