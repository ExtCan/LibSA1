#!/usr/bin/env python3
"""check.py built.bin <sha1> stamp -- compare, report first difference."""
import hashlib
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))


def main():
    path, want, stamp = sys.argv[1], sys.argv[2], sys.argv[3]
    data = open(path, "rb").read()
    got = hashlib.sha1(data).hexdigest()
    if got == want:
        open(stamp, "w").write(got + "\n")
        print(f"{os.path.basename(path)}: OK ({got})")
        return
    name = os.path.basename(path)
    ref = os.path.join(ROOT, "baserom", name)
    msg = f"{name}: MISMATCH sha1 {got} != {want}"
    if os.path.exists(ref):
        r = open(ref, "rb").read()
        n = min(len(r), len(data))
        diff = next((i for i in range(n) if r[i] != data[i]), None)
        msg += f"; size {len(data):#x} vs {len(r):#x}"
        if diff is not None:
            msg += f"; first diff at file offset {diff:#x}"
    if os.path.exists(stamp):
        os.remove(stamp)
    sys.exit(msg)


if __name__ == "__main__":
    main()
