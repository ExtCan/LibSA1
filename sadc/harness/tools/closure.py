#!/usr/bin/env python3
"""Closure of the character select's code against the tree (round 0.9.8, docs/research/charsel098.md 7, review RC5): every
function the game executed in the reference runs (build/traces/ref/charsel_*.cov.txt) and every direct callee of the landed C has
C (src/1st_read, src/overlays/advertise), a libsa1 runtime definition (LIB/src/rt) or a line in LIB/gen/policy/stubs.tsv; an executed
function that no C names is counted as BELOW (the inner functions of the SDK and the CRT, under an rt or a policy).

    py -3.12 harness/tools/closure.py [--out FILE] [COV ...]

Exit 1 with the list when something has none of the three. C the PC never sees (#if !defined(TARGET_PC) ... #endif, as
8C098FAE in code_8C099080.c) counts as no C, and its callees are not asked about. Reads build/*.xrefs.json and build/*.symbols.txt:
run after a SADC build.
"""
import argparse
import glob
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SADC = os.path.dirname(os.path.dirname(HERE))
LIB = os.path.join(os.path.dirname(SADC), "libsa1")
# the C of the character select: every file of the overlay, and these of 1ST_READ
FIRST_READ_FILES = ["code_8C036F60.c", "code_8C038CEC.c", "code_8C043998.c", "code_8C04A120.c", "code_8C04F692.c", "code_8C051760.c",
                    "code_8C055C44.c", "code_8C07DEC0.c", "code_8C099080.c", "code_8C099A00.c", "code_8C0A5840.c", "code_8C0A68D8.c"]
SDK = (0x8C5FBB60, 0x8C66C2A8)


def read_pc(path):
    """the file's text without comments and without what a PC build does not see"""
    s = open(path, encoding="utf-8", errors="replace").read()
    s = re.sub(r"/\*.*?\*/", "", s, flags=re.S)
    s = re.sub(r"//[^\n]*", "", s)
    out, stack = [], []
    for line in s.split("\n"):
        t = line.strip()
        if t.startswith("#if"):
            pc_only = t in ("#if defined(TARGET_PC)", "#ifdef TARGET_PC")
            not_pc = ("!defined(TARGET_PC)" in t and "||" not in t) or t == "#ifndef TARGET_PC"
            stack.append("else hidden" if pc_only else not_pc)
        elif t.startswith("#else") or t.startswith("#elif"):
            if stack:
                stack[-1] = stack[-1] == "else hidden"
        elif t.startswith("#endif"):
            if stack:
                stack.pop()
        elif not any(x is True for x in stack):
            out.append(line)
    return "\n".join(out)


def symbols():
    syms = {}
    for image in ("1ST_READ", "ADVERTISE"):
        for line in open(os.path.join(SADC, "build", image + ".symbols.txt")):
            m = re.match(r"(\S+) = 0x([0-9A-Fa-f]+);", line)
            if m:
                syms[m.group(1)] = int(m.group(2), 16)
    return syms


def defined(paths, syms):
    d = {}
    for fn in paths:
        for m in re.finditer(r"^[A-Za-z_][^\n;=()#]*?\b([A-Za-z_]\w*)\s*\([^;{}]*\)\s*\{", read_pc(fn), re.M):
            a = address(m.group(1), syms)
            if a is not None:
                d[a] = os.path.basename(fn)
    return d


def address(name, syms):
    m = re.match(r"func_(8C[0-9A-F]{6})$", name)
    if m:
        return int(m.group(1), 16)
    return syms.get(name)


def load_funcs(image):
    x = json.load(open(os.path.join(SADC, "build", image + ".xrefs.json")))
    r = {}
    for k, v in x.items():
        try:
            a = int(k, 16)
        except ValueError:
            continue
        if isinstance(v, dict) and "size" in v:
            r[a] = v["size"]
    return r


def ranges(path):
    out = []
    for line in open(path):
        q = line.split()
        if len(q) >= 2 and not line.startswith("#"):
            try:
                out.append((int(q[0], 16), int(q[1], 16)))
            except ValueError:
                pass
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out")
    ap.add_argument("cov", nargs="*")
    a = ap.parse_args()
    covs = a.cov or sorted(glob.glob(os.path.join(SADC, "build", "traces", "ref", "charsel_*.cov.txt")))
    if len(covs) < 4:
        print("closure: FAIL: expected the 4 charsel_*.cov.txt references, found %d" % len(covs))
        return 1
    syms = symbols()
    adv_files = sorted(glob.glob(os.path.join(SADC, "src", "overlays", "advertise", "*.c")))
    fr_files = [os.path.join(SADC, "src", "1st_read", f) for f in FIRST_READ_FILES]
    src = defined(glob.glob(os.path.join(SADC, "src", "1st_read", "*.c")) + adv_files, syms)
    rt = defined(glob.glob(os.path.join(LIB, "src", "rt", "*.c")), syms)
    for fn in glob.glob(os.path.join(LIB, "src", "rt", "*.c")):     # #define RT_RAND func_8C10D334 (rt_misc.c)
        for m in re.finditer(r"^#define\s+RT_\w+\s+func_(8C[0-9A-F]{6})\s*$", open(fn, encoding="utf-8", errors="replace").read(), re.M):
            rt[int(m.group(1), 16)] = os.path.basename(fn)
    policy = {}
    for line in open(os.path.join(LIB, "gen", "policy", "stubs.tsv"), encoding="utf-8"):
        if line.startswith("#") or not line.strip():
            continue
        q = line.rstrip("\n").split("\t")
        if len(q) >= 4:
            try:
                policy[int(q[0], 16)] = q[3].split()[0]
            except ValueError:
                pass

    def what(addr):
        if addr in src:
            return "C"
        if addr in rt:
            return "RT"
        if addr in policy:
            return "POLICY " + policy[addr]
        return "NONE"

    rows = {}
    rs = [r for c in covs for r in ranges(c)]
    for image in ("ADVERTISE", "1ST_READ"):
        for addr, size in load_funcs(image).items():
            if image == "1ST_READ" and addr >= 0x8C900000:
                continue
            if any(addr < hi and lo < addr + max(2, size) for lo, hi in rs):
                rows[addr] = ["executed", what(addr)]
    for fn in adv_files + fr_files:
        s = read_pc(fn)
        for m in re.finditer(r"\b([A-Za-z_]\w*)\s*\(", s):
            addr = address(m.group(1), syms)
            if addr is not None and addr not in rows:
                rows[addr] = ["callee", what(addr)]
    # An executed function with none of the three that no C names is below a line libsa1 replaces (the SDK's and the CRT's inner
    # functions under an rt or a policy, the MLT loader under 8C0939B0, 8C0997BC under 8C099750): libsa1 never reaches it.
    named = set()
    for fn in glob.glob(os.path.join(SADC, "src", "1st_read", "*.c")) + adv_files:
        for m in re.finditer(r"\b([A-Za-z_]\w*)\s*[(,;)]", read_pc(fn)):
            addr = address(m.group(1), syms)
            if addr is not None:
                named.add(addr)
    for k, v in rows.items():
        if v[1] == "NONE" and k not in named:
            v[1] = "BELOW"
    bad = sorted(k for k, v in rows.items() if v[1] == "NONE")
    tally = {}
    for k, v in rows.items():
        kind = v[1].split()[0]
        key = (v[0], kind)
        tally[key] = tally.get(key, 0) + 1
    if a.out:
        with open(a.out, "w") as f:
            for k in sorted(rows):
                region = "SDK" if SDK[0] <= k < SDK[1] else ("ADVERTISE" if k >= 0x8C900000 and k < SDK[1] + 0x300000 else "1ST_READ")
                f.write("%08X %-9s %-8s %s\n" % (k, region, rows[k][0], rows[k][1]))
    for key in sorted(tally):
        print("closure: %-8s %-6s %4d" % (key[0], key[1], tally[key]))
    for k in bad:
        print("closure: NONE %08X (%s)" % (k, rows[k][0]))
    print("closure: %s: %d functions, %d with nothing" % ("FAIL" if bad else "ok", len(rows), len(bad)))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
