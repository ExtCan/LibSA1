#!/usr/bin/env python3
"""List the SDK / C-runtime functions that game code calls (the boundary of
docs/libsa1_integration.md 4.2) with a signature each, for `sa1h trace
--log-sdk` (harness/src/t_sim.c) and for libsa1's SA1_DEBUG_SDKLOG, which
formats its lines with the same signatures.

    py -3.12 harness/tools/sdk_boundary.py [--out build/verify/sdk_boundary.txt]
                                           [--registry harness/build/gen_X/native_registry.c ...]

Boundary function = an entry of 1ST_READ in the SDK area (8C5FBB60-8C67C680)
or the SHC C runtime (8C10C10C-8C111194) that game code of 1ST_READ or of an
overlay calls (build/*.xrefs.json callees) or whose address it loads from its
literal pool (function pointers: njRotateXYZ stored in D_8C19AD44 ...).
Signatures ("ret:arg,..", the gen_native notation) come from the thunk and
native tables of the harness builds' native_registry.c files (the newest file
that knows a function wins; the variants of a function that several files
declare differently are merged: the longer parameter list, a float wins over
an integer at a position, a non-void return wins); "?" when no build knows
the function (t_sim.c then logs R4-R7/FR4-FR7 raw).
The register-convention helpers of the SHC runtime (8C10CA5C-8C10D008:
_divls/_divlu/_modls/_modlu, __quick_mvn) are left out: libsa1 inlines them.
Output lines: ADDR NAME SIG.
"""
import argparse
import glob
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
HARNESS = os.path.dirname(HERE)
ROOT = os.path.dirname(HARNESS)

SDK_LO, SDK_HI = 0x8C5FBB60, 0x8C67C680          # SDK code, up to the start of BSS
CRT_LO, CRT_HI = 0x8C10C10C, 0x8C111194
# SHC helpers with their own register conventions (_divls/_divlu/_modls/_modlu in R0/R1, the
# __quick_mvn struct-copy entries): libsa1 has no calls there (SA1_DIV/SA1_MOD, struct copies),
# so they are left out of the log
HELPERS_LO, HELPERS_HI = 0x8C10CA5C, 0x8C10D008


def boundary_kind(a):
    if (a & 0xFF000000) == 0x0C000000:
        a |= 0x80000000
    if SDK_LO <= a < SDK_HI:
        return "sdk"
    if CRT_LO <= a < CRT_HI:
        return "crt"
    return None


def sig_parse(sig):
    ret, _, ps = sig.partition(":")
    return ret, [p for p in ps.split(",") if p]


def is_float(t):
    return t in ("f32", "f64")


def sig_merge(a, b):
    """the rule of t_sim.c sig_merge"""
    if not b:
        return a
    if not a:
        return b
    ra, pa = sig_parse(a)
    rb, pb = sig_parse(b)
    if ra == "v" or (is_float(rb) and not is_float(ra)):
        ra = rb
    for i, t in enumerate(pb):
        if i >= len(pa):
            pa.append(t)
        elif is_float(t) and not is_float(pa[i]):
            pa[i] = t
    return ra + ":" + ",".join(pa)


ROW_THUNK = re.compile(r'\{ 0x([0-9A-F]{8})u, \(void \*\)\w+, "(\w+)", "([^"]*)" \}')
ROW_NATIVE = re.compile(r'\{ 0x([0-9A-F]{8})u, \(void \*\)\w+, \w+, "(\w+)", "([^"]*)", "[^"]*" \}')


def registry_sigs(paths):
    """address -> signature: the first file that knows an address wins (its variants merged)"""
    out = {}
    for p in paths:
        here = {}
        txt = open(p, encoding="utf-8", errors="replace").read()
        for m in list(ROW_THUNK.finditer(txt)) + list(ROW_NATIVE.finditer(txt)):
            a = int(m.group(1), 16)
            if boundary_kind(a):
                here[a] = sig_merge(here.get(a, ""), m.group(3))
        for a, s in here.items():
            out.setdefault(a, s)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "verify", "sdk_boundary.txt"))
    ap.add_argument("--registry", nargs="*",
                    help="native_registry.c files (default: those of all harness builds, newest first)")
    a = ap.parse_args()
    x1 = json.load(open(os.path.join(ROOT, "build", "1ST_READ.xrefs.json")))
    names = {int(k, 16): v["name"] for k, v in x1.items()}
    callers = {}
    for path in sorted(glob.glob(os.path.join(ROOT, "build", "*.xrefs.json"))):
        image = os.path.basename(path).split(".")[0]
        xs = x1 if image == "1ST_READ" else json.load(open(path))
        for k, v in xs.items():
            if image == "1ST_READ" and boundary_kind(int(k, 16)):
                continue                        # SDK / runtime code calling itself
            targets = list(v.get("callees", []))
            targets += [c["value"] for c in v.get("consts", []) if isinstance(c.get("value"), int)]
            for t in targets:
                t &= 0xFFFFFFFF
                if (t & 0xFF000000) == 0x0C000000:
                    t |= 0x80000000
                if boundary_kind(t) and t in names and not HELPERS_LO <= t < HELPERS_HI:
                    callers.setdefault(t, set()).add(image)
    regs = a.registry if a.registry else sorted(
        glob.glob(os.path.join(HARNESS, "build", "gen_*", "native_registry.c")), key=os.path.getmtime, reverse=True)
    sigs = registry_sigs(regs)
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    nsig = 0
    with open(a.out, "w", encoding="utf-8", newline="\n") as f:
        f.write("# SDK / C runtime functions called by game code (harness/tools/sdk_boundary.py): ADDR NAME SIG\n")
        f.write("# SIG: ret:arg,.. (v i8 u8 i16 u16 i32 u32 p f32 f64 ...); '?' = unknown (registers logged raw)\n")
        for ad in sorted(callers):
            s = sigs.get(ad, "")
            nsig += bool(s)
            f.write("%08X %s %s\n" % (ad, names.get(ad, "func_%08X" % ad), s or "?"))
    nsdk = sum(1 for ad in callers if boundary_kind(ad) == "sdk")
    print("%d boundary functions (%d SDK, %d C runtime), %d with a signature (%d registries) -> %s" % (
        len(callers), nsdk, len(callers) - nsdk, nsig, len(regs), a.out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
