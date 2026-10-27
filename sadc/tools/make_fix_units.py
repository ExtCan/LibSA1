#!/usr/bin/env python3
"""Turn route_matrix results into work units for tools/workflows/verify_fix.js.

    py -3.12 tools/make_fix_units.py [--matrix build/verify/route_matrix_nm_pc.json]
        [--manifest harness/build/gen_win32_nm_pc/native_manifest.json] [--out build/verify/fix_units.json]

One unit per source file: the functions of that file that break at least one trace when routed alone, with the
evidence (script -> first difference). Functions the matrix shows as "ok" everywhere are left out.
"""
import argparse
import json
import os

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--matrix", default=os.path.join(ROOT, "build", "verify", "route_matrix_nm_pc.json"))
    ap.add_argument("--manifest", default=os.path.join(ROOT, "harness", "build", "gen_win32_nm_pc", "native_manifest.json"))
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "verify", "fix_units.json"))
    ap.add_argument("--fmac", default=os.path.join(ROOT, "build", "verify", "fmac_audit.json"),
                    help="tools/fmac_audit.py result: functions whose FMAC count differs are added ('-': none)")
    a = ap.parse_args()
    mx = json.load(open(a.matrix))
    man = json.load(open(a.manifest))
    owner = {}
    for f, e in man["files"].items():
        for d in e.get("defines", []):
            owner[d["name"]] = f
    units = {}
    for key, per in mx.items():
        addr, name = key.split(" ", 1)
        bad = {s: v for s, v in sorted(per.items()) if v != "ok"}
        if not bad:
            continue
        f = owner.get(name)
        if not f:
            print("no source file for", key)
            continue
        u = units.setdefault(f, {"file": f, "functions": [], "evidence": [], "scripts": set()})
        u["functions"].append(name)
        lines = ["  %s (%s):" % (name, addr)]
        for s, v in bad.items():
            lines.append("    %-14s %s" % (s, v))
            u["scripts"].add(s)
        ok = [s for s, v in per.items() if v == "ok"]
        if ok:
            lines.append("    exact on: " + ", ".join(sorted(ok)))
        u["evidence"].append("\n".join(lines))
    if a.fmac != "-" and os.path.exists(a.fmac):
        fm = json.load(open(a.fmac))
        for name, v in sorted(fm["mismatch"].items()):
            f = v["file"]
            u = units.setdefault(f, {"file": f, "functions": [], "evidence": [], "scripts": set()})
            line = ("  %s: FMAC audit: the original has %d FMAC instruction(s), the libsa1 build of the C fuses %d "
                    "multiply-add(s) (inlined callees count for the caller on both sides)" % (name, v["sh4"], v["pc"]))
            if name not in u["functions"]:
                u["functions"].append(name)
            u["evidence"].append(line)
    out = []
    for f in sorted(units):
        u = units[f]
        out.append({"file": f, "functions": u["functions"], "scripts": sorted(u["scripts"]),
                    "evidence": "\n".join(u["evidence"])})
    json.dump({"units": out}, open(a.out, "w"), indent=1)
    print("%d files, %d functions -> %s" % (len(out), sum(len(u["functions"]) for u in out), a.out))


if __name__ == "__main__":
    main()