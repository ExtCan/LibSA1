#!/usr/bin/env python3
"""Region / symbol diff of two 16 MB DC RAM images (sadc docs/libsa1_integration.md 9.1 V3).

    py -3.12 src/world/world_ram_diff.py REF.ram WORLD.ram [--sadc DIR] [--top N]

REF.ram: a reference image (harness `trace ... --dump-ram-at 0`, e.g. sadc/build/traces/m1_ec1_stand.step0.ram);
WORLD.ram: a world arena (tests/world_boot_test --dump DIR writes DIR/boot_ec1_stand.ram right after boot).
Prints the differing bytes per region (low RAM, 1ST_READ data, BSS, heap, overlay slots) and, for 1ST_READ and
BSS, per symbol of sadc/build/1ST_READ.symbols.txt (the nearest symbol at or below the byte), marking the symbols
the decompiled C references (src/1st_read/*.c): only those can influence the lowered game code.
"""
import argparse
import bisect
import glob
import os
import re

HEAP_LO, HEAP_HI = 0x7EE8E0, 0x87E8F0


def region(o):
    if o < 0x10000:
        return "low RAM (BIOS / HLE / stack)"
    if o < 0x67C680:
        return "1ST_READ"
    if HEAP_LO <= o < HEAP_HI:
        return "heap"
    if o < 0x900000:
        return "BSS"
    return "overlay slots"


def main():
    lib = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    ap = argparse.ArgumentParser()
    ap.add_argument("ref")
    ap.add_argument("world")
    ap.add_argument("--sadc", default=os.path.join(os.path.dirname(lib), "sadc"))
    ap.add_argument("--top", type=int, default=60)
    a = ap.parse_args()
    ref = open(a.ref, "rb").read()
    mine = open(a.world, "rb").read()
    if len(ref) != 0x1000000 or len(mine) != 0x1000000:
        raise SystemExit("both images must be 16 MB")
    syms = []
    for line in open(os.path.join(a.sadc, "build", "1ST_READ.symbols.txt"), encoding="utf-8", errors="replace"):
        m = re.match(r"\s*(\S+)\s*=\s*0x([0-9A-Fa-f]+)", line)
        if m:
            syms.append((int(m.group(2), 16) & 0xFFFFFF, m.group(1)))
    syms.sort()
    keys = [s[0] for s in syms]
    used = set()
    for f in glob.glob(os.path.join(a.sadc, "src", "1st_read", "*.c")):
        used.update(re.findall(r"\b([BD]_8C[0-9A-F]{6})\b", open(f, encoding="utf-8", errors="replace").read()))
    per_region, per_sym = {}, {}
    for o in range(0x1000000):
        if ref[o] == mine[o]:
            continue
        r = region(o)
        per_region[r] = per_region.get(r, 0) + 1
        if r in ("1ST_READ", "BSS"):
            i = bisect.bisect_right(keys, o) - 1
            name = syms[i][1] if i >= 0 else "?"
            per_sym[(r, name)] = per_sym.get((r, name), 0) + 1
    print("differing bytes per region:")
    for r, n in sorted(per_region.items(), key=lambda kv: -kv[1]):
        print("  %-30s %9d" % (r, n))
    referenced = [(k, n) for k, n in per_sym.items() if k[1] in used]
    print("symbols referenced by the decompiled C that differ (%d of %d differing symbols):" % (len(referenced),
                                                                                              len(per_sym)))
    for (r, name), n in sorted(referenced, key=lambda kv: -kv[1])[:a.top]:
        print("  %-9s %-24s %7d" % (r, name, n))


main()
