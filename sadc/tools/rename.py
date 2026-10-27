#!/usr/bin/env python3
"""Rename symbols consistently across config and sources.

    py -3.12 tools/rename.py OLD NEW [OLD NEW ...] [--image 1st_read]
    py -3.12 tools/rename.py --file renames.txt [--image 1st_read]   (lines: OLD NEW)

Updates config/symbols.<image>.txt (adding or replacing the entry for the
symbol's address) and every whole-word occurrence in src/ and include/.
Re-run `configure.py --split` afterwards so the assembly uses the new names.
"""
import argparse
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
IDENT = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("pairs", nargs="*")
    ap.add_argument("--file")
    ap.add_argument("--image", default="1st_read")
    a = ap.parse_args()
    pairs = list(zip(a.pairs[::2], a.pairs[1::2]))
    if a.file:
        for line in open(a.file, encoding="utf-8"):
            p = line.split("#")[0].split()
            if len(p) >= 2:
                pairs.append((p[0], p[1]))
    if not pairs:
        sys.exit("nothing to rename")
    img = a.image.lower()
    built = os.path.join(ROOT, "build", f"{img.upper() if img == '1st_read' else img.upper()}.symbols.txt")
    addr_of = {}
    if os.path.exists(built):
        for line in open(built):
            n, v = line.split("=")
            addr_of[n.strip()] = int(v.strip().rstrip(";"), 16)
    sym_path = os.path.join(ROOT, "config", f"symbols.{img}.txt")
    lines = open(sym_path, encoding="utf-8").read().split("\n") if os.path.exists(sym_path) else []
    names_in_cfg = {}
    for i, l in enumerate(lines):
        s = l.split("//")[0]
        if "=" in s:
            names_in_cfg[s.split("=")[0].strip()] = i
    existing = set(addr_of) | set(names_in_cfg)
    for old, new in pairs:
        if not IDENT.match(new):
            sys.exit(f"bad identifier {new}")
        if new in existing and new != old:
            sys.exit(f"{new} already exists")
        if old in names_in_cfg:
            i = names_in_cfg[old]
            lines[i] = re.sub(r"^\s*" + re.escape(old) + r"\b", new, lines[i])
        elif old in addr_of:
            lines.append(f"{new} = 0x{addr_of[old]:08X}; // renamed from {old}")
        else:
            sys.exit(f"unknown symbol {old}")
        existing.add(new)
    with open(sym_path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines).rstrip("\n") + "\n")
    pat = {old: re.compile(r"\b" + re.escape(old) + r"\b") for old, _ in pairs}
    changed = 0
    for top in ("src", "include"):
        for dp, dn, fn in os.walk(os.path.join(ROOT, top)):
            for f in fn:
                if not f.endswith((".c", ".h")):
                    continue
                p = os.path.join(dp, f)
                s = open(p, encoding="utf-8").read()
                t = s
                for old, new in pairs:
                    t = pat[old].sub(new, t)
                if t != s:
                    open(p, "w", encoding="utf-8", newline="\n").write(t)
                    changed += 1
    print(f"renamed {len(pairs)} symbols; {changed} source files updated; run configure.py --split")


if __name__ == "__main__":
    main()
