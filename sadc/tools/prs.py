#!/usr/bin/env python3
"""SEGA PRS compression (decompressor + byte-exact compressor) for Sonic Adventure (DC).

Usage:
    prs.py d <in.prs> <out.bin>     decompress
    prs.py c <in.bin> <out.prs>     compress (reproduces SEGA's original encoder)
    prs.py t <in.prs> [...]         self-test: decompress, recompress, compare to original

Format reference (bitstream layout): fuzziqer's PRS utility (prs.cpp, archived at
https://github.com/essen/prsutil), also ported by FraGag (https://github.com/FraGag/prs.net,
used by SA Tools) and Sewer56 (https://github.com/Sewer56/prs-rs).  This file is an
independent Python implementation; the compressor's match-selection policy was
reverse-engineered from the retail Sonic Adventure (US 1.005) .PRS files and is not
the same as fuzziqer's (which is plain greedy).

Bitstream
---------
A control byte supplies flag bits LSB first.  A new control byte is read from the
input stream *lazily*, i.e. only at the moment a 9th bit is needed, so control bytes
are interleaved with the payload bytes of the commands they describe.  Commands:

    1                 literal: copy next input byte to the output
    0 0 b1 b0  XX     short copy: size = (b1<<1|b0) + 2   (2..5),
                      offset = XX - 256                  (-256..-1)
    0 1  LO HI        long copy:  w = LO | HI<<8 ; w == 0 terminates the stream
                      offset = (w >> 3) - 0x2000         (-8192..-1)
                      size   = (w & 7) + 2               (3..9)  if (w & 7) != 0
                      size   = NN + 1 (next byte NN)     (1..256) if (w & 7) == 0

Copies may overlap the output being produced (offset > -size => run-length).
The stream ends with bits "0 1" followed by bytes 00 00.  Unused high bits of the
last control byte are 0.

SEGA encoder policy (reproduces the retail SONICADV/*.PRS files byte-for-byte):
  * window: offsets 1..0x1FFF back, match length 2..256 (capped at end of data),
    overlapping matches allowed.
  * match finder returns the longest match; ties -> nearest (smallest offset).
    Length-2 matches are searched in the whole window even though only offsets
    <= 256 are encodable (as short copies).
  * one-step lazy matching: if a match (len L) exists at p, look at p+1 (len L2).
    If L2 > L + 1, emit a literal at p and then emit the p+1 match unconditionally
    (no further lazy check at p+1).  Otherwise emit the match at p.
  * a "match" of length 2 at offset > 256 is emitted as a literal.
  * encoding choice: short copy if size <= 5 and offset >= -256, else long copy
    (inline size if size <= 9, extended size byte otherwise).
"""

import sys

__all__ = ["decompress", "decompress_ex", "compress", "PRSError"]

WINDOW = 0x1FFF
MAXLEN = 256


class PRSError(Exception):
    pass


# ---------------------------------------------------------------------------
# Decompression
# ---------------------------------------------------------------------------

def decompress_ex(src, trace=None):
    """Decompress PRS data.

    Returns (output_bytes, number_of_input_bytes_consumed).
    If ``trace`` is a list, (kind, out_pos, offset, size) tuples are appended,
    kind in 'L' (literal), 'S' (short copy), 'G' (long copy, inline size),
    'X' (long copy, extended size).
    """
    src = bytes(src)
    n = len(src)
    out = bytearray()
    sp = 0
    cmd = 0
    nbits = 0
    try:
        while True:
            # --- flag bit 1
            if nbits == 0:
                cmd = src[sp]; sp += 1; nbits = 8
            b = cmd & 1; cmd >>= 1; nbits -= 1
            if b:
                if trace is not None:
                    trace.append(('L', len(out), 0, 1))
                out.append(src[sp]); sp += 1
                continue
            # --- flag bit 2
            if nbits == 0:
                cmd = src[sp]; sp += 1; nbits = 8
            b = cmd & 1; cmd >>= 1; nbits -= 1
            if b:
                w = src[sp] | (src[sp + 1] << 8)
                sp += 2
                if w == 0:
                    break
                off = (w >> 3) - 0x2000
                size = w & 7
                if size == 0:
                    size = src[sp] + 1; sp += 1
                    kind = 'X'
                else:
                    size += 2
                    kind = 'G'
            else:
                size = 0
                for _ in range(2):
                    if nbits == 0:
                        cmd = src[sp]; sp += 1; nbits = 8
                    size = (size << 1) | (cmd & 1); cmd >>= 1; nbits -= 1
                size += 2
                off = src[sp] - 256; sp += 1
                kind = 'S'
            start = len(out) + off
            if start < 0:
                raise PRSError("copy before start of output (pos %d, offset %d)" % (len(out), off))
            if trace is not None:
                trace.append((kind, len(out), off, size))
            if -off >= size:
                out += out[start:start + size]
            else:
                # overlapping copy: the pattern of period -off repeats
                period = out[start:]
                reps = size // len(period) + 1
                out += (period * reps)[:size]
    except IndexError:
        raise PRSError("truncated PRS stream (input exhausted at 0x%X of 0x%X)" % (sp, n))
    return bytes(out), sp


def decompress(src):
    """Decompress PRS data, returning the decompressed bytes."""
    return decompress_ex(src)[0]


# ---------------------------------------------------------------------------
# Compression (SEGA-exact)
# ---------------------------------------------------------------------------

def _find_commands(data):
    """Return a list of (pos, offset, size) commands; offset == 0 means literal."""
    n = len(data)
    head3 = {}          # 3-byte key -> most recent position
    head2 = {}          # 2-byte key -> most recent position
    prev = [-1] * n     # 3-byte hash chain (previous position with the same key)
    state = [0]         # next position to insert into the dictionaries

    def insert_upto(p):
        q = state[0]
        lim3 = n - 2
        lim2 = n - 1
        while q < p:
            if q < lim3:
                k = data[q] | (data[q + 1] << 8) | (data[q + 2] << 16)
                prev[q] = head3.get(k, -1)
                head3[k] = q
            if q < lim2:
                head2[data[q] | (data[q + 1] << 8)] = q
            q += 1
        state[0] = q

    def matchlen(q, p, lim):
        # length of the common prefix of data[q:] and data[p:], at most lim
        if data[q:q + lim] == data[p:p + lim]:
            return lim
        lo, hi = 0, lim
        while hi - lo > 1:
            mid = (lo + hi) >> 1
            if data[q:q + mid] == data[p:p + mid]:
                lo = mid
            else:
                hi = mid
        return lo

    def find(p):
        insert_upto(p)
        lim = n - p
        if lim > MAXLEN:
            lim = MAXLEN
        if lim < 2:
            return 0, 0
        best = 0
        boff = 0
        if lim >= 3:
            q = head3.get(data[p] | (data[p + 1] << 8) | (data[p + 2] << 16), -1)
            minq = p - WINDOW
            while q >= minq and q >= 0:
                # quick reject: a candidate must beat the current best
                if best == 0 or data[q + best] == data[p + best]:
                    l = matchlen(q, p, lim)
                    if l > best:
                        best = l
                        boff = q - p
                        if l >= lim:
                            break
                q = prev[q]
        if best == 0:
            q = head2.get(data[p] | (data[p + 1] << 8), -1)
            if q >= 0 and p - q <= WINDOW:
                best = 2
                boff = q - p
        return best, boff

    cmds = []
    p = 0
    cached = None       # (pos, len, off) from the lazy look-ahead
    commit = False      # previous literal was emitted by the lazy rule
    while p < n:
        if cached is not None and cached[0] == p:
            l, o = cached[1], cached[2]
        else:
            l, o = find(p)
        cached = None
        if l >= 2:
            if not commit and p + 1 < n:
                l2, o2 = find(p + 1)
                cached = (p + 1, l2, o2)
                if l2 > l + 1:
                    cmds.append((p, 0, 1))
                    p += 1
                    commit = True
                    continue
            commit = False
            if l == 2 and o < -256:
                cmds.append((p, 0, 1))      # far 2-byte match is not encodable
                p += 1
                continue
            cmds.append((p, o, l))
            p += l
        else:
            commit = False
            cmds.append((p, 0, 1))
            p += 1
    return cmds


def _encode(data, cmds):
    out = bytearray(b'\0')
    ctrl = 0            # position of the current control byte
    nbits = 0           # bits used in it

    def bit(b):
        nonlocal ctrl, nbits
        if nbits == 8:      # allocate the next control byte only when needed
            ctrl = len(out)
            out.append(0)
            nbits = 0
        if b:
            out[ctrl] |= 1 << nbits
        nbits += 1

    for p, o, l in cmds:
        if o == 0:
            bit(1)
            out.append(data[p])
        elif o >= -256 and l <= 5:
            s = l - 2
            bit(0); bit(0); bit(s >> 1); bit(s & 1)
            out.append(o & 0xFF)
        else:
            bit(0); bit(1)
            o13 = (o & 0x1FFF) << 3
            if l <= 9:
                w = o13 | (l - 2)
                out.append(w & 0xFF); out.append(w >> 8)
            else:
                out.append(o13 & 0xFF); out.append(o13 >> 8); out.append(l - 1)
    bit(0); bit(1)
    out.append(0); out.append(0)
    return bytes(out)


def compress(data):
    """Compress bytes with SEGA's PRS encoder policy (byte-exact vs. retail files)."""
    data = bytes(data)
    return _encode(data, _find_commands(data))


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def _main(argv):
    if len(argv) == 3 and argv[0] == 'd':
        with open(argv[1], 'rb') as f:
            src = f.read()
        out, used = decompress_ex(src)
        if used != len(src):
            sys.stderr.write("warning: %d trailing bytes after end of PRS stream\n" % (len(src) - used))
        with open(argv[2], 'wb') as f:
            f.write(out)
        return 0
    if len(argv) == 3 and argv[0] == 'c':
        with open(argv[1], 'rb') as f:
            src = f.read()
        with open(argv[2], 'wb') as f:
            f.write(compress(src))
        return 0
    if len(argv) >= 2 and argv[0] == 't':
        bad = 0
        for path in argv[1:]:
            with open(path, 'rb') as f:
                src = f.read()
            out = decompress(src)
            rec = compress(out)
            ok_rt = decompress(rec) == out
            exact = rec == src
            if not exact:
                bad += 1
            print("%-40s prs=%8d bin=%8d recompressed=%8d %s%s" % (
                path, len(src), len(out), len(rec), "EXACT" if exact else "DIFF",
                "" if ok_rt else " ROUNDTRIP-FAIL"))
        return 1 if bad else 0
    sys.stderr.write("usage: prs.py d in.prs out.bin | prs.py c in.bin out.prs | prs.py t file.prs...\n")
    return 2


if __name__ == '__main__':
    sys.exit(_main(sys.argv[1:]))
