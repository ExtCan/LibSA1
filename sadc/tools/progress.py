#!/usr/bin/env python3
"""Report decompilation progress.

    py -3.12 tools/progress.py [--image 1ST_READ] [--json] [--tiers]

A function counts as decompiled when a C definition for it exists in
src/<image>/ (ASM_STUB declarations do not count). Sizes come from the
splitter's xref index. --tiers also reports the libsa1 tiers from
build/tier_functions.json (matched / NON_MATCHING / todo) and writes
build/tier_status.json.
"""
import argparse
import json
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
sys.path.insert(0, ROOT)
from configure import c_functions  # noqa: E402

STUB_RE = re.compile(r"ASM_STUB\s*\(.*?\b([A-Za-z_]\w*)\s*\(")
NM_RE = re.compile(r"(?ms)^#ifdef NON_MATCHING\b(.*?)^#(?:else|endif)\b")


def defined_functions(path, text):
    tmp = path + ".progress.tmp"
    open(tmp, "w", encoding="utf-8").write(text)
    try:
        return c_functions(tmp)
    finally:
        os.remove(tmp)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--image", default="1ST_READ")
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--tiers", action="store_true")
    a = ap.parse_args()
    xr = json.load(open(os.path.join(ROOT, "build", f"{a.image}.xrefs.json")))
    name_to_addr = {v["name"]: int(k, 16) for k, v in xr.items()}
    done = set()
    nonmatching = set()
    src_dir = os.path.join(ROOT, "src", a.image.lower())
    for dp, dn, fn in os.walk(src_dir):
        for f in fn:
            if not f.endswith(".c"):
                continue
            p = os.path.join(dp, f)
            text = open(p, encoding="utf-8", errors="replace").read()
            stubs = set(STUB_RE.findall(text))
            # functions defined only inside #ifdef NON_MATCHING do not count
            live = NM_RE.sub("", text)
            for n in defined_functions(p, live):
                if n in name_to_addr and n not in stubs:
                    done.add(name_to_addr[n])
            nm = "\n".join(NM_RE.findall(text))
            if nm.strip():
                for n in defined_functions(p, nm):
                    if n in name_to_addr:
                        nonmatching.add(name_to_addr[n])
    regions = {"game": (0x8C010000, 0x8C200000), "library": (0x8C5F0000, 0x8C680000)}
    rep = {}
    for rname, (lo, hi) in regions.items():
        fs = [(int(k, 16), v["size"]) for k, v in xr.items() if lo <= int(k, 16) < hi]
        tot_n, tot_b = len(fs), sum(s for _, s in fs)
        d_n = sum(1 for f, s in fs if f in done)
        d_b = sum(s for f, s in fs if f in done)
        rep[rname] = {"functions": tot_n, "done": d_n, "bytes": tot_b, "done_bytes": d_b}
    if a.tiers:
        tiers = json.load(open(os.path.join(ROOT, "build", "tier_functions.json")))
        status = {}
        for tier in ("P0", "P1", "P2"):
            fs = tiers.get(tier, [])
            st = {"matched": [], "nonmatching": [], "todo": []}
            for addr, name, size in fs:
                cur = xr.get(f"{addr:08X}", {}).get("name", name)
                k = "matched" if addr in done else "nonmatching" if addr in nonmatching else "todo"
                st[k].append([f"{addr:08X}", cur, size])
            status[tier] = st
            tb = sum(f[2] for f in fs)
            mb = sum(f[2] for f in st["matched"])
            print(f"{tier}: {len(fs):5} functions | matched {len(st['matched']):5} "
                  f"({100.0 * mb / max(1, tb):.1f}% bytes) | nonmatching {len(st['nonmatching']):4} | "
                  f"todo {len(st['todo']):5} ({sum(f[2] for f in st['todo'])} bytes)")
        json.dump(status, open(os.path.join(ROOT, "build", "tier_status.json"), "w"), indent=0)
    if a.json:
        print(json.dumps(rep, indent=1))
        return
    for rname, r in rep.items():
        pct = 100.0 * r["done_bytes"] / max(1, r["bytes"])
        print(f"{a.image} {rname:8}: {r['done']:5}/{r['functions']:5} functions, "
              f"{r['done_bytes']:8}/{r['bytes']:8} bytes ({pct:.2f}%)")


if __name__ == "__main__":
    main()