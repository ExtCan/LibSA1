#!/usr/bin/env python3
"""Build decompilation work units (JSON) for workflow agents.

    py -3.12 tools/make_work_units.py --image 1ST_READ [--max-funcs 8] [--max-bytes 1600]
        [--lo 0x8C010000 --hi 0x8C111194] [--exclude-big 4096] [--limit N] [--order size|addr|callers]
        > build/work_units.json

A unit is a run of consecutive not-yet-decompiled functions from one code
chunk, written to its own C file src/<image>/code_<first>.c. Functions
larger than --exclude-big bytes are left out (too large for one session).
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


def done_functions(src_dir):
    done = set()
    for dp, dn, fn in os.walk(src_dir):
        for f in fn:
            if f.endswith(".c"):
                p = os.path.join(dp, f)
                text = open(p, encoding="utf-8", errors="replace").read()
                stubs = set(STUB_RE.findall(text))
                # NON_MATCHING attempts count as claimed (someone tried)
                for n in c_functions(p):
                    if n not in stubs:
                        done.add(n)
    return done


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--image", default="1ST_READ")
    ap.add_argument("--max-funcs", type=int, default=8)
    ap.add_argument("--max-bytes", type=int, default=1600)
    ap.add_argument("--exclude-big", type=int, default=4096)
    ap.add_argument("--lo", type=lambda x: int(x, 16), default=0)
    ap.add_argument("--hi", type=lambda x: int(x, 16), default=0xFFFFFFFF)
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--order", default="addr", choices=["addr", "size", "callers"])
    a = ap.parse_args()
    man = json.load(open(os.path.join(ROOT, "build", f"{a.image}.manifest.json")))
    xr = json.load(open(os.path.join(ROOT, "build", f"{a.image}.xrefs.json")))
    img_dir = (man.get("asm_dir") or f"asm/{a.image.lower()}").replace("asm/", "", 1)
    src_dir = os.path.join(ROOT, "src", img_dir)
    done = done_functions(src_dir)
    units = []
    for ch in man["chunks"]:
        if ch["kind"] != "code" or not (a.lo <= ch["start"] < a.hi):
            continue
        cur, cur_b = [], 0
        def flush():
            nonlocal cur, cur_b
            if cur:
                units.append({"image": a.image, "src": ch["src"], "cfile": f"code_{cur[0][0]:08X}.c",
                              "srcdir": f"src/{img_dir}", "functions": [n for _, n, _ in cur],
                              "bytes": cur_b,
                              "callers": sum(len(xr[f"{x:08X}"]["callers"]) for x, _, _ in cur)})
            cur, cur_b = [], 0
        for addr, name in ch["functions"]:
            size = xr[f"{addr:08X}"]["size"]
            if name in done or size > a.exclude_big:
                flush()
                continue
            if cur and (len(cur) >= a.max_funcs or cur_b + size > a.max_bytes):
                flush()
            cur.append((addr, name, size))
            cur_b += size
        flush()
    if a.order == "size":
        units.sort(key=lambda u: u["bytes"])
    elif a.order == "callers":
        units.sort(key=lambda u: -u["callers"])
    if a.limit:
        units = units[:a.limit]
    json.dump(units, sys.stdout, indent=0)
    print(f"{len(units)} units, {sum(len(u['functions']) for u in units)} functions, "
          f"{sum(u['bytes'] for u in units)} bytes", file=sys.stderr)


if __name__ == "__main__":
    main()
