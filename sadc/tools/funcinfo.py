#!/usr/bin/env python3
"""Print everything useful for decompiling one function.

    py -3.12 tools/funcinfo.py <func_name|0xADDR> [--image 1ST_READ] [--no-ghidra]

Sections: location (chunk / neighbours), original assembly, literal-pool
constants (symbols, floats, strings), callees and callers, known data labels
(reference/*.csv) for referenced addresses, and Ghidra's pseudo-C if the
Ghidra export exists in build/ghidra/<IMAGE>/decomp/.
"""
import argparse
import csv
import glob
import json
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))


def load_labels():
    labels = {}
    for p in glob.glob(os.path.join(ROOT, "reference", "*.csv")):
        if "sadx_pc" in p or "sa2dc" in p:
            continue
        try:
            with open(p, encoding="utf-8", errors="replace") as f:
                for row in csv.DictReader(f):
                    a = row.get("address") or row.get("addr")
                    n = row.get("name")
                    if not a or not n:
                        continue
                    try:
                        labels.setdefault(int(a, 16), f"{n} [{row.get('type', '')}] ({os.path.basename(p)})")
                    except ValueError:
                        pass
        except (OSError, csv.Error):
            pass
    return labels


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("func")
    ap.add_argument("--image", default="1ST_READ")
    ap.add_argument("--no-ghidra", action="store_true")
    a = ap.parse_args()
    xr = json.load(open(os.path.join(ROOT, "build", f"{a.image}.xrefs.json")))
    man = json.load(open(os.path.join(ROOT, "build", f"{a.image}.manifest.json")))
    by_name = {v["name"]: k for k, v in xr.items()}
    key = a.func[2:].upper().zfill(8) if a.func.lower().startswith("0x") else by_name.get(a.func)
    if key is None or key not in xr:
        sys.exit(f"unknown function {a.func}")
    addr = int(key, 16)
    info = xr[key]
    name = info["name"]
    names = {int(k, 16): v["name"] for k, v in xr.items()}
    symtab = {}
    for line in open(os.path.join(ROOT, "build", f"{a.image}.symbols.txt")):
        n, v = line.split("=")
        symtab[int(v.strip().rstrip(";"), 16)] = n.strip()
    chunk = next(c for c in man["chunks"] if any(f == addr for f, _ in c["functions"]))
    fl = chunk["functions"]
    idx = [f for f, _ in fl].index(addr)
    print(f"== {name} @ {addr:08X}  ({info['size']} bytes of code)")
    print(f"chunk {chunk['src']} [{chunk['start']:08X}-{chunk['end']:08X}] "
          f"function {idx + 1}/{len(fl)}: " + ", ".join(n for _, n in fl))
    # assembly
    print("\n-- assembly --")
    nxt = fl[idx + 1][0] if idx + 1 < len(fl) else chunk["end"]
    on = False
    for line in open(os.path.join(ROOT, chunk["src"])):
        line = line.rstrip("\n")
        if line.startswith(f"_{name}:"):
            on = True
        m = re.search(r";\s([0-9A-F]{8})\s", line)
        if on and m and int(m.group(1), 16) >= nxt:
            break
        if on and re.match(r"^_\w+:", line) and not line.startswith(f"_{name}:"):
            lab_addr = None
            if line[1:].split(":")[0] in by_name:
                break
        if on:
            print(line)
    # pool constants
    labels = load_labels()
    if info["consts"]:
        print("\n-- literal pool loads --")
        for c in info["consts"]:
            v = c["value"]
            desc = c.get("sym") or ""
            if "float" in c:
                desc += f" float={c['float']}"
            lab = labels.get(v)
            if lab:
                desc += f"  <{lab}>"
            if c.get("size") == 2:
                print(f"  {c['pool']:08X}: .DATA.W {v}")
            else:
                print(f"  {c['pool']:08X}: {v:08X} {desc}")
    if info["strings"]:
        print("\n-- strings --")
        for s in info["strings"]:
            print(f"  {s['addr']:08X}: {s['text']!r}")
    print("\n-- callees --")
    for c in info["callees"]:
        print(f"  {c:08X} {names.get(c, symtab.get(c, '?'))}")
    print("-- callers --")
    for c in info["callers"][:40]:
        print(f"  {c:08X} {names.get(c, '?')}")
    if len(info["callers"]) > 40:
        print(f"  ... {len(info['callers']) - 40} more")
    if not a.no_ghidra:
        gp = os.path.join(ROOT, "build", "ghidra", a.image, "decomp", f"{key}.c")
        if not os.path.exists(gp):
            gp2 = glob.glob(os.path.join(ROOT, "build", "ghidra", a.image, "decomp", f"*{key.lower()}*.c"))
            gp = gp2[0] if gp2 else gp
        if os.path.exists(gp):
            print("\n-- ghidra pseudo-C (draft, not authoritative) --")
            print(open(gp, encoding="utf-8", errors="replace").read())


if __name__ == "__main__":
    main()
