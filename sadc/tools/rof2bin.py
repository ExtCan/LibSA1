#!/usr/bin/env python3
"""Convert a linked SYSROF absolute file (.abs) to a flat binary.

Usage: rof2bin.py in.abs out.bin BASE END   (BASE/END hex, END exclusive)

Wraps SEGA's rof2bin.exe from the Katana tool set, which writes <stem>.bin
next to the input; output is then trimmed to [BASE, END) so BSS is dropped.
"""
import os
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))


def main():
    src, dst, base, end = sys.argv[1], sys.argv[2], int(sys.argv[3], 16), int(sys.argv[4], 16)
    d = os.path.dirname(os.path.abspath(src))
    stem = os.path.splitext(os.path.basename(src))[0]
    r = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "hitachi.py"), "--cwd", d,
                        "rof2bin", f"-s={base:08X}", f"-e={end:08X}", "-q", os.path.basename(src)])
    if r.returncode:
        sys.exit(r.returncode)
    produced = None
    for cand in (stem + ".bin", stem + ".BIN", stem.upper() + ".BIN"):
        p = os.path.join(d, cand)
        if os.path.exists(p):
            produced = p
            break
    if produced is None:
        sys.exit("rof2bin produced no output")
    data = open(produced, "rb").read()
    if os.path.abspath(produced) != os.path.abspath(dst):
        os.remove(produced)
    open(dst, "wb").write(data[: end - base])


if __name__ == "__main__":
    main()
