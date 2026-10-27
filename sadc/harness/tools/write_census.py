#!/usr/bin/env python3
"""Write census (docs/libsa1_integration.md 2.8): what the game writes in guest
RAM over the sample traces, mapped to symbols - to size per-world memory and
copy-on-write for libsa1 worlds.

    py -3.12 harness/tools/write_census.py [--exe harness/build/sa1h.exe] [--scripts harness/tests/sim/*.txt]
                                           [--out build/verify/write_census] [-j N] [--reuse]

Runs `sa1h trace --census` (pure: no routing, native writes would not be seen)
for every script, then merges build/verify/census/<script>.txt:
  * per region (1ST_READ code/data, BSS, game heap, guest stack, overlays, ...):
    4 KB blocks the interpreter wrote and bytes that changed, setup (game init ..
    stage start) and steps (the logic frames), per script and over all;
  * per symbol (build/1ST_READ.symbols.txt, sizes and classes of
    build/verify/world_syms.txt when present): bytes changed in setup / steps and
    which scripts change it.
Writes <out>.txt (summary and the symbols changed during the steps) and
<out>.json (everything).
"""
import argparse
import bisect
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

REGIONS = [  # P1 addresses [lo, hi)
    (0x8C000000, 0x8C00C000, "low RAM (BIOS area, HLE records)"),
    (0x8C00C000, 0x8C010000, "guest stack"),
    (0x8C010000, 0x8C5FBB60, "1ST_READ game code + data"),
    (0x8C5FBB60, 0x8C67C680, "1ST_READ SDK code + data"),
    (0x8C67C680, 0x8C7EE8E0, "BSS (game + SDK)"),
    (0x8C7EE8E0, 0x8C87E8F0, "game heap (B_8C7EE8E0)"),
    (0x8C87E8F0, 0x8C900000, "BSS (SDK, after the heap)"),
    (0x8C900000, 0x8D000000, "overlay area (stage, motions, textures ...)"),
]


def script_args(path):
    first = open(path).readline()
    if first.startswith("# args:"):
        return first[len("# args:"):].split()
    return ["--level", "1", "--act", "0", "--no-objects"]


def run_one(job):
    exe, script, cens, trace = job
    cmd = [exe, "trace", "--input", "@" + script, "--print", "0"] + script_args(script) + \
        ["--census", cens, "--out", trace]
    r = subprocess.run(cmd, capture_output=True, text=True)
    return script, r.returncode, (r.stdout + r.stderr)[-400:]


def parse(path):
    blocks = {"setup": set(), "steps": set()}
    ranges = {"setup": [], "steps": []}
    for line in open(path):
        p = line.split()
        if not p or p[0].startswith("#"):
            continue
        if p[0] == "B":
            lo, n = int(p[2], 16), int(p[3])
            blocks[p[1]].update(range((lo & 0xFFFFFF) >> 12, ((lo & 0xFFFFFF) >> 12) + n))
        elif p[0] == "W":
            ranges[p[1]].append((int(p[2], 16), int(p[3], 16)))
    return blocks, ranges


def region_of(a):
    for lo, hi, name in REGIONS:
        if lo <= a < hi:
            return name
    return "?"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", default=os.path.join(HARNESS, "build", "sa1h.exe"))
    ap.add_argument("--scripts", nargs="*")
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "verify", "write_census"))
    ap.add_argument("-j", type=int, default=max(1, min(8, (os.cpu_count() or 4) - 2)))
    ap.add_argument("--reuse", action="store_true", help="use existing build/verify/census/*.txt, do not run")
    a = ap.parse_args()
    exe = os.path.abspath(a.exe)
    scripts = a.scripts or sorted(glob.glob(os.path.join(HARNESS, "tests", "sim", "*.txt")))
    cdir = os.path.join(ROOT, "build", "verify", "census")
    tdir = os.path.join(ROOT, "build", "traces", "census")
    os.makedirs(cdir, exist_ok=True)
    os.makedirs(tdir, exist_ok=True)
    names = [os.path.splitext(os.path.basename(s))[0] for s in scripts]
    if not a.reuse:
        jobs = [(exe, s, os.path.join(cdir, n + ".txt"), os.path.join(tdir, n)) for s, n in zip(scripts, names)]
        with concurrent.futures.ThreadPoolExecutor(a.j) as ex:
            for script, rc, tail in ex.map(run_one, jobs):
                if rc:
                    print("%s: trace failed:\n%s" % (script, tail))
                    return 1
    # symbols
    syms = []
    for line in open(os.path.join(ROOT, "build", "1ST_READ.symbols.txt")):
        m = re.match(r"\s*([A-Za-z0-9_$.]+)\s*=\s*0x([0-9A-Fa-f]+)", line)
        if m:
            syms.append((int(m.group(2), 16), m.group(1)))
    syms.sort()
    info = {}
    wpath = os.path.join(ROOT, "build", "verify", "world_syms.txt")
    if os.path.exists(wpath):
        for line in open(wpath):
            p = line.split()
            if len(p) == 5 and not p[0].startswith("#"):
                info[p[4]] = {"size": int(p[1], 16), "class": p[2], "ref": int(p[3])}
    addrs = [s[0] for s in syms]

    def sym_of(a):
        k = bisect.bisect_right(addrs, a) - 1
        if k < 0 or a >= 0x8C900000:
            return None, 0
        return syms[k][1], syms[k][0]

    per_script = {}
    per_sym = {}
    allblocks = {"setup": set(), "steps": set()}
    for n in names:
        blocks, ranges = parse(os.path.join(cdir, n + ".txt"))
        reg = {}
        for ph in ("setup", "steps"):
            allblocks[ph] |= blocks[ph]
            for b in blocks[ph]:
                r = region_of(0x8C000000 + (b << 12))
                reg.setdefault(r, {}).setdefault(ph + "_blocks", 0)
                reg[r][ph + "_blocks"] += 1
            for lo, hi in ranges[ph]:
                a2 = lo
                while a2 < hi:
                    r = region_of(a2)
                    rhi = min(hi, next(h for l2, h, nm in REGIONS if nm == r) if r != "?" else hi)
                    reg.setdefault(r, {}).setdefault(ph + "_bytes", 0)
                    reg[r][ph + "_bytes"] += rhi - a2
                    a2 = rhi
                # symbols
                a2 = lo
                while a2 < hi:
                    nm, sa = sym_of(a2)
                    if nm is None:
                        break
                    k = bisect.bisect_right(addrs, a2)
                    send = addrs[k] if k < len(addrs) else 0x8C900000
                    e = min(hi, send)
                    d = per_sym.setdefault(nm, {"addr": sa, "setup": 0, "steps": 0, "scripts_setup": set(),
                                                "scripts_steps": set()})
                    d.setdefault(ph + "_bytes_by_script", {})
                    d[ph + "_bytes_by_script"][n] = d[ph + "_bytes_by_script"].get(n, 0) + (e - a2)
                    d["scripts_" + ph].add(n)
                    a2 = e
        per_script[n] = {"setup_blocks": len(blocks["setup"]), "steps_blocks": len(blocks["steps"]),
                         "union_blocks": len(blocks["setup"] | blocks["steps"]),
                         "steps_only_blocks": len(blocks["steps"] - blocks["setup"]), "regions": reg}
    for nm, d in per_sym.items():
        for ph in ("setup", "steps"):
            by = d.get(ph + "_bytes_by_script", {})
            d[ph] = max(by.values()) if by else 0
            d["scripts_" + ph] = sorted(d["scripts_" + ph])
        d.update(info.get(nm, {}))
    out = {"scripts": per_script,
           "all": {"setup_blocks": len(allblocks["setup"]), "steps_blocks": len(allblocks["steps"]),
                   "union_blocks": len(allblocks["setup"] | allblocks["steps"])},
           "symbols": per_sym}
    with open(a.out + ".json", "w") as f:
        json.dump(out, f, indent=1, sort_keys=True)
    with open(a.out + ".txt", "w", encoding="utf-8", newline="\n") as f:
        f.write("# write census over %d scripts (harness/tools/write_census.py, sa1h trace --census, pure runs)\n"
                % len(names))
        f.write("# 4 KB blocks written by the interpreter: setup = game init .. stage start, steps = the logic frames\n\n")
        f.write("%-16s %8s %8s %8s %10s  (4 KB blocks; MB = union x 4 KB)\n" % (
            "script", "setup", "steps", "union", "steps-only"))
        for n in names:
            s = per_script[n]
            f.write("%-16s %8d %8d %8d %10d  %.2f MB\n" % (n, s["setup_blocks"], s["steps_blocks"], s["union_blocks"],
                                                          s["steps_only_blocks"], s["union_blocks"] * 4096 / 1048576))
        al = out["all"]
        f.write("%-16s %8d %8d %8d %10s  %.2f MB\n\n" % ("all scripts", al["setup_blocks"], al["steps_blocks"],
                                                        al["union_blocks"], "", al["union_blocks"] * 4096 / 1048576))
        f.write("per region (blocks / changed bytes), maximum over the scripts:\n")
        regs = sorted({r for s in per_script.values() for r in s["regions"]})
        for r in regs:
            vals = {k: max(s["regions"].get(r, {}).get(k, 0) for s in per_script.values())
                    for k in ("setup_blocks", "setup_bytes", "steps_blocks", "steps_bytes")}
            f.write("  %-44s setup %5d / %9d   steps %5d / %8d\n" % (r, vals["setup_blocks"], vals["setup_bytes"],
                                                                     vals["steps_blocks"], vals["steps_bytes"]))
        stepsyms = sorted((d["addr"], nm, d) for nm, d in per_sym.items() if d["steps"])
        f.write("\nsymbols changed during the logic steps (%d; bytes = maximum over the scripts; class from "
                "world_syms.txt):\n" % len(stepsyms))
        for ad, nm, d in stepsyms:
            f.write("  %08X %-24s %-2s %7d B  setup %7d B  %d scripts%s\n" % (
                ad, nm, d.get("class", "?"), d["steps"], d["setup"], len(d["scripts_steps"]),
                "" if len(d["scripts_steps"]) == len(names) else ": " + ",".join(d["scripts_steps"])))
    print("census: %d scripts, %d / %d / %d blocks (setup / steps / union, all scripts), %d symbols changed in steps "
          "-> %s.txt/.json" % (len(names), al["setup_blocks"], al["steps_blocks"], al["union_blocks"],
                               sum(1 for d in per_sym.values() if d["steps"]), a.out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
