#!/usr/bin/env python3
"""Tests for tools/prs.py.

    py -3 tools/tests/test_prs.py            synthetic round-trip tests
    py -3 tools/tests/test_prs.py --disc     + byte-exact recompression of every disc/SONICADV/*.PRS
"""
import glob
import os
import random
import sys
import time

TOOLS = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, TOOLS)
import prs  # noqa: E402


def synthetic():
    rng = random.Random(1234)
    cases = [b'', b'a', b'ab', b'abc', b'\0' * 1000, b'ab' * 700, bytes(range(256)) * 50]
    for _ in range(30):
        n = rng.randrange(0, 40000)
        alphabet = rng.randrange(1, 256)
        buf = bytearray()
        while len(buf) < n:
            if buf and rng.random() < 0.4:
                # copy from somewhere in the past (exercise long offsets/lengths)
                off = rng.randrange(1, min(len(buf), 9000) + 1)
                ln = rng.randrange(1, 300)
                for _ in range(ln):
                    buf.append(buf[-off])
            else:
                buf.append(rng.randrange(alphabet))
        cases.append(bytes(buf[:n]))
    for c in cases:
        enc = prs.compress(c)
        dec, used = prs.decompress_ex(enc)
        assert dec == c, 'round-trip failed (len %d)' % len(c)
        assert used == len(enc), 'trailing bytes in compressed stream'
    print('synthetic round-trip: %d cases OK' % len(cases))


def _check_file(path):
    with open(path, 'rb') as f:
        src = f.read()
    out, used = prs.decompress_ex(src)
    return path, used == len(src) and prs.compress(out) == src


def disc():
    from multiprocessing import Pool
    root = os.path.dirname(TOOLS)
    files = sorted(glob.glob(os.path.join(root, 'disc', 'SONICADV', '*.PRS')),
                   key=lambda f: -os.path.getsize(f))
    exact = 0
    t0 = time.time()
    with Pool(max(1, (os.cpu_count() or 2) - 1)) as pool:
        for path, ok in pool.imap_unordered(_check_file, files):
            if ok:
                exact += 1
            else:
                print('DIFF', os.path.basename(path))
    print('disc: %d / %d PRS files recompress byte-exact (%.0fs)' % (exact, len(files), time.time() - t0))
    return exact == len(files)


if __name__ == '__main__':
    synthetic()
    ok = True
    if '--disc' in sys.argv:
        ok = disc()
    sys.exit(0 if ok else 1)
