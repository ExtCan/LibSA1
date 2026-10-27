#!/usr/bin/env python3
"""Generate Ghidra function seeds for a raw SH-4 binary by pointer scanning.

Every 4-byte aligned little-endian word in the image whose value points into one
of the given code ranges (and is 2-byte aligned) is treated as a candidate
function entry (function-pointer tables, task/object exec callbacks, literal
pool entries used by JSR/JMP). Each candidate is validated by straight-line
decoding with sadc/tools/sh4/isa.py: all instructions up to the first block end
(RTS/BRA/JMP/BRAF/RTE + delay slot, max --max-insns) must decode, the delay slot
must be legal, and the first word must not be 0x0000/0xFFFF/NOP padding.

Output: one line per seed, "ADDR  # refs=N" (ADDR is 8 hex digits, no 0x),
sorted by address; this is the seed-file format read by SadcSetup.java.

Usage:
  py -3.12 gen_seeds.py BIN --base 8C010000 \
      --code 8C010000-8C111194 --code 8C5FBB20-8C66C2C0 -o seeds.txt --weak-out weak.txt

Candidates whose preceding bytes decode as instructions that would fall through
into them ("fallthrough") are kept out of the main list (usually in-function JMP
targets or coincidental data values); --weak-out lists them for SadcExport.java
(weakseeds=), which only uses them where no code exists after analysis.
"""
import argparse
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))  # sadc/tools
from sh4.isa import decode as _decode, ENDS_BLOCK  # noqa: E402


class _Extra:
    """FSCA/FSRRA: implemented by the Dreamcast SH7091 and used by 1ST_READ
    (sin/cos, 1/sqrt), but not decoded by sh4/isa.py (SHC never emits them)."""
    __slots__ = ("mnem",)

    def __init__(self, mnem):
        self.mnem = mnem

    def slot_illegal(self):
        return False


def decode(w, addr):
    ins = _decode(w, addr)
    if ins is None:
        if (w & 0xF1FF) == 0xF0FD:
            return _Extra("FSCA")
        if (w & 0xF0FF) == 0xF07D:
            return _Extra("FSRRA")
    return ins


def parse_range(s):
    a, b = s.split("-")
    return int(a, 16), int(b, 16)


def valid_entry(data, base, addr, max_insns):
    off = addr - base
    if off < 0 or off + 2 > len(data):
        return False
    first = struct.unpack_from("<H", data, off)[0]
    if first in (0x0000, 0xFFFF, 0x0009):
        # 0x0009 (NOP) is SHC alignment padding far more often than an entry
        return False
    n = 0
    a = addr
    while n < max_insns:
        o = a - base
        if o + 2 > len(data):
            return False
        ins = decode(struct.unpack_from("<H", data, o)[0], a)
        if ins is None:
            return False
        n += 1
        if ins.mnem in ENDS_BLOCK:
            o += 2
            if o + 2 > len(data):
                return False
            slot = decode(struct.unpack_from("<H", data, o)[0], a + 2)
            return slot is not None and not slot.slot_illegal()
        a += 2
    return True  # long straight-line run of valid instructions


CALLS = ("JSR", "BSR", "BSRF")
PAD_WORDS = (0x0000, 0x0009, 0x0900)  # 0x0900 = big-endian NOP bytes "00 09" (linker fill)
POOL_HI = (0x8C, 0x8D, 0xA0, 0xA5, 0xFF, 0x00, 0x3F, 0xBF, 0x40, 0xC0, 0x41, 0x42, 0x43,
           0xC1, 0xC2, 0xC3, 0x3E, 0xBE, 0x44, 0xC4, 0x45, 0xC5, 0x46, 0x47, 0x48)


def plausible_boundary(data, base, t):
    """Reject targets that the preceding code would fall through into
    (in-function JMP targets, return addresses, coincidental data values)."""
    o = t - base
    if o < 4:
        return True
    p4 = decode(struct.unpack_from("<H", data, o - 4)[0], t - 4)
    if p4 is not None and p4.mnem in CALLS:
        return False  # return address: call + delay slot (often NOP) falls through
    w2 = struct.unpack_from("<H", data, o - 2)[0]
    if w2 in PAD_WORDS:
        return True
    p2 = decode(w2, t - 2)
    if p2 is None:
        return True  # data (pool / table / string) right before
    if p4 is None or p4.mnem in ENDS_BLOCK:
        return True  # data before, or TERM + delay slot
    if t % 4 == 0 and (struct.unpack_from("<I", data, o - 4)[0] >> 24) in POOL_HI:
        return True  # looks like the tail of a literal pool
    return False


# ---- SHC long branches: "mov.l @(disp,PC),Rn ... jmp @Rn" ----------------------------
# SHC emits these both for tail calls (after the epilogue) and for branches inside one big
# function that exceed BRA range. Targets of the latter are not functions and must not be
# seeded (they split the enclosing function and confuse Ghidra's analyzers).

def _w(data, base, a):
    o = a - base
    if o < 0 or o + 2 > len(data):
        return None
    return struct.unpack_from("<H", data, o)[0]


def is_pop(w):
    """Epilogue-only instructions: LDS.L @R15+,PR / MOV.L @R15+,Rn / FMOV.S @R15+,FRn /
    ADD #+imm,R15 / LDS.L @R15+,MACL."""
    return (w == 0x4F26 or w == 0x4F16 or (w & 0xF0FF) == 0x60F6 or (w & 0xF0FF) == 0xF0F9
            or ((w & 0xFF00) == 0x7F00 and 0 < (w & 0xFF) < 0x80))


def is_push(w):
    return ((w & 0xFF0F) == 0x2F06 or w == 0x4F22 or w == 0x4F12 or (w & 0xFF0F) == 0xFF0B
            or ((w & 0xFF00) == 0x7F00 and (w & 0x80)))


def uses_frame(data, base, t, n=12):
    """True if code at t touches an existing stack frame before setting one up."""
    for k in range(n):
        w = _w(data, base, t + 2 * k)
        if w is None or is_push(w):
            return False
        if (is_pop(w) or (w & 0xF0F0) == 0x50F0 or (w & 0xF0FF) == 0x60F2
                or (w & 0xFF00) == 0x1F00 or (w & 0xFFF0) == 0x85F0 or (w & 0xFFF0) == 0x84F0
                or (w & 0xF0FF) == 0xF0F6 or (w & 0xF0FF) == 0xF0F7):
            return True
        ins = decode(w, t + 2 * k)
        if ins is None or ins.mnem in ENDS_BLOCK:
            return False
    return False


def epilogue_before(data, base, j, n=12):
    """Pops within n instructions before JMP at j, or in its delay slot."""
    ws = _w(data, base, j + 2)
    if ws is not None and is_pop(ws):
        return True
    for k in range(1, n + 1):
        w = _w(data, base, j - 2 * k)
        if w is None:
            return False
        if is_pop(w):
            return True
        if w == 0x000B:  # previous RTS: different path
            return False
    return False


def long_jumps(data, base, code):
    """Yield (jmp_addr, target, intra) for constant-target JMP @Rn in code ranges."""
    for lo, hi in code:
        for a in range(lo, hi, 2):
            x = _w(data, base, a)
            if x is None or (x & 0xF0FF) != 0x402B:
                continue
            rn = (x >> 8) & 15
            t = None
            for b in range(a - 2, a - 20, -2):
                y = _w(data, base, b)
                if y is None:
                    break
                if (y >> 12) == 0xD and ((y >> 8) & 15) == rn:
                    po = (b & ~3) + (y & 0xFF) * 4 + 4 - base
                    if 0 <= po + 4 <= len(data):
                        t = struct.unpack_from("<I", data, po)[0]
                    break
                yi = decode(y, b)
                if yi is None or yi.mnem in ENDS_BLOCK or yi.mnem in CALLS:
                    break
            if t is None or not any(l <= t < h for l, h in code):
                continue
            intra = (not epilogue_before(data, base, a) and uses_frame(data, base, t)
                     and not is_push(_w(data, base, t) or 0))
            yield a, t, intra


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("binary")
    ap.add_argument("--base", default="8C010000", help="load address (hex)")
    ap.add_argument("--code", action="append", default=[],
                    help="code range START-END (hex, END exclusive); repeatable")
    ap.add_argument("--scan", action="append", default=[],
                    help="only scan pointers stored in START-END (default: whole file)")
    ap.add_argument("--max-insns", type=int, default=96)
    ap.add_argument("--min-refs", type=int, default=1)
    ap.add_argument("--keep-long-jumps", action="store_true",
                    help="do not exclude in-function JMP targets (SHC long branches)")
    ap.add_argument("--no-boundary-check", action="store_true",
                    help="accept targets that look like fallthrough from preceding code")
    ap.add_argument("-o", "--out", required=True)
    ap.add_argument("--weak-out",
                    help="also write the 'fallthrough' rejects here (applied after analysis by "
                         "SadcExport.java weakseeds=, only where no code exists yet)")
    args = ap.parse_args()

    data = open(args.binary, "rb").read()
    base = int(args.base, 16)
    code = [parse_range(r) for r in args.code] or [(base, base + len(data))]
    scan = [parse_range(r) for r in args.scan] or [(base, base + len(data))]

    refs = {}
    n = len(data) // 4
    words = struct.unpack_from("<%dI" % n, data, 0)
    for i, v in enumerate(words):
        a = base + 4 * i
        if not any(s <= a < e for s, e in scan):
            continue
        if v & 1:
            continue
        for s, e in code:
            if s <= v < e:
                refs[v] = refs.get(v, 0) + 1
                break

    intra = set()
    if not args.keep_long_jumps:
        lj = list(long_jumps(data, base, code))
        intra = {t for _, t, i in lj if i}
        print("long jumps: %d constant-target JMPs, %d distinct intra-function targets excluded"
              % (len(lj), len(intra)))

    seeds = []
    rejected = []
    for v in sorted(refs):
        if v in intra:
            rejected.append((v, "jmp-intra"))
            continue
        if refs[v] < args.min_refs:
            continue
        if not valid_entry(data, base, v, args.max_insns):
            rejected.append((v, "invalid"))
        elif not args.no_boundary_check and not plausible_boundary(data, base, v):
            rejected.append((v, "fallthrough"))
        else:
            seeds.append(v)

    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "w", newline="\n") as f:
        f.write("# generated by gen_seeds.py from %s\n" % os.path.basename(args.binary))
        for v in seeds:
            f.write("%08X  # refs=%d\n" % (v, refs[v]))
        for v, why in rejected:
            f.write("# rejected %08X  refs=%d  %s\n" % (v, refs[v], why))
    if args.weak_out:
        with open(args.weak_out, "w", newline="\n") as f:
            f.write("# weak seeds (fallthrough-ambiguous) from gen_seeds.py\n")
            for v, why in rejected:
                if why == "fallthrough":
                    f.write("%08X  # refs=%d\n" % (v, refs[v]))
    print("candidates=%d seeds=%d rejected=%d (invalid=%d fallthrough=%d jmp-intra=%d) -> %s"
          % (len(refs), len(seeds), len(rejected),
             sum(1 for _, w in rejected if w == "invalid"),
             sum(1 for _, w in rejected if w == "fallthrough"),
             sum(1 for _, w in rejected if w == "jmp-intra"), args.out))


if __name__ == "__main__":
    main()
