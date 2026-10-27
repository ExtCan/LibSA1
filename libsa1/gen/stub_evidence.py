#!/usr/bin/env python3
"""stub_evidence.py - the measured part of the evidence column of gen/policy/stubs.tsv.

    py -3.12 gen/stub_evidence.py --exe <sadc>/harness/build/sa1h_<tag>.exe [--report build/pclower/report.json]
                                  [--logs DIR] [--update-tsv] [--add-missing]

Build the interpreter first, privately (never rebuild the shared harness exes):
    cd <sadc> && py -3.12 harness/build.py --files none --tag <tag>

1. "ref:"   runs every reference script of <sadc>/harness/tests/sim (15 today) with
            "trace ... --log-sdk FILE --sdk-list <the stubs>" (the original functions run, hooked at their DC
            address; --check against build/traces/ref proves the hooked run reproduces the reference) and counts the
            calls made during logic steps (phase S), the scripts, the return values;
2. "reach:" static reach from the character mains and the libsa1 step functions over <sadc>/build/1ST_READ.xrefs.json
            (direct calls and literal-pool code addresses; SDK / C runtime bodies are not followed: rt reimplements
            them; the particle manager 8C09DF64 is a step function since 2026-10-20; TIKAL_PROG's TUs are roots
            through pclower's functions.json);
3. "the original draws rand": the original reaches rand (8C10D334) through direct calls.

Without --update-tsv it prints the measured text per stub. --update-tsv rewrites, in every TSV line, the part of the
evidence from "; ref:" on (the hand-written reason before it stays); --add-missing appends a fatal line for stubs
without one (the reason must then be written by hand).
"""
import argparse
import collections
import concurrent.futures
import glob
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
LIB = os.path.dirname(HERE)
ROOTS = {"SonicMain": 0x8C0A77BC, "TailsMain": 0x8C0B6148, "KnucklesMain": 0x8C0C0640, "E102Main": 0x8C0CBE00,
         "AmyMain": 0x8C0D6B40, "BigMain": 0x8C0DFFA0, "pad 8C050BE4": 0x8C050BE4, "executor 8C0988A4": 0x8C0988A4,
         "CCL 8C0342E0": 0x8C0342E0, "land update 8C028EC2": 0x8C028EC2, "land init 8C028E60": 0x8C028E60,
         "particles 8C09DF64": 0x8C09DF64, "display 8C0988C4": 0x8C0988C4, "heap 8C098A10": 0x8C098A10,
         "tasks 8C09844A": 0x8C09844A, "slots 8C06C300": 0x8C06C300}
RAND = 0x8C10D334
LOGRE = re.compile(r"^(\S) (-?\d+) (\d+) ([0-9A-F]{8}) (\S+?)\((.*)\) -> (\S+)$")


def run_logs(exe, sadc, stubs, logdir, jobs):
    os.makedirs(logdir, exist_ok=True)
    lst = os.path.join(logdir, "stubs.lst")
    with open(lst, "w") as f:
        for name, addr, sig in stubs:
            f.write("%s %s %s\n" % (addr, name, sig))

    def one(script):
        name = os.path.splitext(os.path.basename(script))[0]
        extra = "--level 1 --act 0 --no-objects".split()
        first = open(script).readline()
        if first.startswith("# args:"):
            extra = first[len("# args:"):].split()
        cmd = [exe, "trace", "--input", "@" + script, "--print", "0"] + extra + [
            "--out", os.path.join(logdir, name), "--log-sdk", os.path.join(logdir, name + ".sdk"), "--sdk-list", lst]
        ref = os.path.join(sadc, "build", "traces", "ref", name + ".bin")
        if os.path.exists(ref):
            cmd += ["--check", ref]
        r = subprocess.run(cmd, cwd=sadc, capture_output=True, text=True, errors="replace")
        return name, r.returncode
    scripts = sorted(glob.glob(os.path.join(sadc, "harness", "tests", "sim", "*.txt")))
    with concurrent.futures.ThreadPoolExecutor(jobs) as ex:
        for name, rc in ex.map(one, scripts):
            print("  %-16s %s" % (name, "identical to the reference" if rc == 0 else "exit %d (NOT identical)" % rc))
    return len(scripts)


def summarise(logdir):
    st = collections.defaultdict(lambda: {"S": 0, "F": 0, "D": 0, "scripts": set(), "rets": collections.Counter()})
    for f in sorted(glob.glob(os.path.join(logdir, "*.sdk"))):
        name = os.path.basename(f)[:-4]
        for line in open(f, errors="replace"):
            m = LOGRE.match(line.strip())
            if not m:
                continue
            ph, addr, ret = m.group(1), m.group(4), m.group(7)
            e = st[addr]
            e[ph] += 1
            if ph == "S":
                e["scripts"].add(name)
                e["rets"][ret] += 1
    return st


def reach_scan(sadc, stubs, fj):
    x = json.load(open(os.path.join(sadc, "build", "1ST_READ.xrefs.json")))
    keys = {int(a, 16) & 0x1FFFFFFF for _, a, _ in stubs}
    roots = list(ROOTS.items())
    for u, d in fj.get("units", {}).items():
        if u.split("__")[-1].startswith("code_0CB"):      # tikal_prog__code_0CB00000.c (per-image TU names)
            roots += [("Tikal", int(e["addr"], 16)) for e in d["externs"].values()]
    reach = collections.defaultdict(set)
    for rname, ra in roots:
        seen, stack = set(), [ra]
        while stack:
            a = stack.pop()
            k = a & 0x1FFFFFFF
            if k in seen:
                continue
            seen.add(k)
            if k in keys:
                reach[k].add(rname)
                continue
            full = a | 0x80000000
            if full >= 0x8C5F0000 or 0x8C10C10C <= full < 0x8C111194:
                continue
            e = x.get("%08X" % full)
            if e is None:
                continue
            stack += e.get("callees", [])
            stack += [c["value"] for c in e.get("consts", []) if "%08X" % (c.get("value", 0) | 0x80000000) in x
                      or (c.get("value", 0) & 0x1FFFFFFF) in keys]

    def rand_of(a0):
        seen, stack = set(), [a0]
        while stack:
            a = stack.pop()
            if a in seen:
                continue
            seen.add(a)
            e = x.get("%08X" % a)
            for c in (e or {}).get("callees", []):
                if c == RAND:
                    return True
                stack.append(c)
        return False
    return reach, rand_of


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exe", help="a private pure harness exe (harness/build.py --files none --tag T)")
    ap.add_argument("--game", default=os.path.join(os.path.dirname(LIB), "sadc"))
    ap.add_argument("--report", default=os.path.join(LIB, "build", "pclower", "report.json"))
    ap.add_argument("--logs", default=os.path.join(LIB, "build", "stub_evidence"))
    ap.add_argument("--tsv", default=os.path.join(HERE, "policy", "stubs.tsv"))
    ap.add_argument("--no-run", action="store_true", help="reuse the logs in --logs")
    ap.add_argument("--update-tsv", action="store_true")
    ap.add_argument("--add-missing", action="store_true")
    ap.add_argument("-j", type=int, default=8)
    a = ap.parse_args()
    sadc = os.path.abspath(a.game)
    rep = json.load(open(a.report, encoding="utf-8"))
    fjp = os.path.join(os.path.dirname(os.path.abspath(a.report)), "functions.json")
    fj = json.load(open(fjp, encoding="utf-8")) if os.path.exists(fjp) else {}
    stubs = [tuple(s.split(" ", 2)) for s in rep["stubs"]]
    nscripts = len(glob.glob(os.path.join(sadc, "harness", "tests", "sim", "*.txt")))
    if not a.no_run:
        if not a.exe:
            ap.error("--exe is required (or --no-run with existing logs)")
        print("== reference scripts with the stubs hooked")
        run_logs(os.path.abspath(a.exe), sadc, stubs, a.logs, a.j)
    st = summarise(a.logs)
    reach, rand_of = reach_scan(sadc, stubs, fj)
    measured = {}
    for name, addr, sig in stubs:
        e = st.get(addr)
        parts = []
        if e and (e["S"] or e["F"]):
            rets = e["rets"]
            rs = ""
            if rets and list(rets) != ["-"]:
                rs = ", returns " + "/".join("%s x%d" % kv for kv in rets.most_common(8))
            parts.append("ref: %d calls in steps in %d/%d scripts%s%s" % (e["S"], len(e["scripts"]), nscripts, rs,
                                                                         (", %d in frame code" % e["F"]) if e["F"] else ""))
        else:
            parts.append("ref: 0 calls in the %d scripts" % nscripts)
        rr = sorted(reach.get(int(addr, 16) & 0x1FFFFFFF, []))
        parts.append("reach: %s" % (", ".join(rr) if rr else "none from the mains / step"))
        if addr.startswith("8C") and rand_of(int(addr, 16)):
            parts.append("the original draws rand")
        measured[name] = "; ".join(parts)
    if not a.update_tsv:
        for name, addr, sig in sorted(stubs, key=lambda t: t[1]):
            print("%s %-26s %s" % (addr, name, measured[name]))
        return 0
    lines = open(a.tsv, encoding="utf-8").read().split("\n")
    seen = set()
    for i, line in enumerate(lines):
        if not line.strip() or line.startswith("#"):
            continue
        cols = line.split("\t")
        if len(cols) == 5 and cols[1] in measured:
            reason = cols[4].split("; ref:")[0] if "ref:" in cols[4] else cols[4]
            reason = "" if reason.startswith("ref:") else reason
            cols[4] = (reason + "; " if reason else "") + measured[cols[1]]
            lines[i] = "\t".join(cols)
            seen.add(cols[1])
    if a.add_missing:
        for name, addr, sig in sorted(stubs, key=lambda t: t[1]):
            if name not in seen:
                lines.insert(len(lines) - 1 if lines and lines[-1] == "" else len(lines),
                             "%s\t%s\t%s\tfatal\tTODO reason; %s" % (addr, name, sig, measured[name]))
    with open(a.tsv, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))
    print("updated %s (%d lines measured)" % (a.tsv, len(seen)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
