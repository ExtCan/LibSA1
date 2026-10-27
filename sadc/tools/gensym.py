#!/usr/bin/env python3
"""Emit an asmsh source defining another image's symbols as absolute values.

    gensym.py build/1ST_READ.symbols.txt [needed.txt] out.src

Overlays are linked separately from 1ST_READ but call into it, so every
1ST_READ symbol they import is provided as  _name: .EQU H'8Cxxxxxx  with
.EXPORT (the same role sa2dc's genfsy.py plays). If a list of needed symbols
is given, only those are emitted.
"""
import sys


def main():
    syms = []
    for line in open(sys.argv[1]):
        n, v = line.split("=")
        syms.append((n.strip(), int(v.strip().rstrip(";"), 16)))
    known = {n for n, _ in syms}
    need = None
    out = sys.argv[-1]
    if len(sys.argv) > 3:
        need = set(l.strip() for l in open(sys.argv[2]) if l.strip())
    col = " " * 10
    with open(out, "w", newline="\n") as f:
        if need is not None:
            # an auto-generated name (func_/D_/B_/lbl_ + address) still resolves
            # after the symbol was renamed in the other image
            import re
            for n in sorted(need - known):
                m = re.fullmatch(r"(?:func|D|B|lbl)_([0-9A-F]{8})", n)
                if m:
                    syms.append((n, int(m.group(1), 16)))
        for n, v in syms:
            if need is not None and n not in need:
                continue
            f.write(f"{col}.EXPORT     _{n}\n")
            f.write(f"_{n}: .EQU H'{v:08X}\n")
        f.write(f"{col}.END\n")


if __name__ == "__main__":
    main()
