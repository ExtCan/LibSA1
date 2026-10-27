#!/usr/bin/env python3
"""Extract and catalogue the Sonic Adventure (DC, US v1.005) overlays.

Steps (reproducible from a user's own disc dump):
  1. copy disc/1ST_READ.BIN            -> baserom/1ST_READ.BIN
  2. PRS-decompress every disc/SONICADV/*.PRS that is a linked module
     (contains SH-4 code, or is a data module with a known load address)
                                       -> baserom/overlays/<NAME>.BIN
  3. analyse each one: link address, size, SH-4 code estimate,
     STS.L PR,@-R15 prologue count, references into 1ST_READ
  4. write baserom/overlays.json (+ baserom/overlays_skipped.json)

Usage:
    py -3 tools/extract_overlays.py [--root <sadc dir>] [--satools <SATools/GameConfig/DC_SA1>]
                                    [--adventurerip <AdventureRip dir>] [--verify-recompress]
                                    [--only NAME ...]

PVM/PVR texture archives (magic "PVMH"/"GBIX"/"PVRT") are skipped: they are
plain data with no load address.  Load addresses come from the SA Tools
GameConfig / AdventureRip ini files ("key=") when available, verified against
the pointer values inside the file; otherwise they are inferred from pointers
(EV*.PRS also carry their entry point 0x0CB80020 in word 0).  If no ini files
are available, the table KNOWN_BASES (results for US v1.005) is used as the
candidate list.

Note on addresses: stage/event/chao overlays are *linked* at P0 addresses
(0x0C900000, 0x0CB80000, ...: pointers inside the file lack the 0x80000000
bit); P0 aliases the same physical RAM as P1 0x8C900000 etc.  SUMMARY,
ADVERTISE and ALERTWND are linked at P1 0x8C900000.  "load_addr" in the JSON
is the link address used by the file's own pointers; "load_addr_p1" is the
P1 alias.

Code estimate: SHC lays out each object's code as contiguous functions, then
00 09 filler to a 32-byte boundary, then one shared literal pool; sections are
separated by zero padding / data.  Seeds (STS.L PR,@-R15 prologues walked back
over register pushes, filler runs after a delayed branch, the EV header entry
point) are extended backwards over valid SH-4 encodings and then swept
forwards instruction by instruction, skipping literal pools that are
referenced by MOV.L/MOV.W/MOVA @(disp,PC); a sweep stops at an invalid
encoding or where data follows a function end.  code_estimate_bytes is the
union of the swept regions (instructions + their literal pools + filler).
"""

import argparse
import hashlib
import json
import os
import re
import shutil
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import prs                                   # noqa: E402
from sh4.isa import decode                   # noqa: E402

# 1ST_READ.BIN layout (approximate boundaries, P1 addresses)
FR_BASE = 0x8C010000
FR_CODE_END = 0x8C111000         # game code
FR_DATA_END = 0x8C5F8000         # game data
FR_LIB_END = 0x8C67C680          # library code+data = end of file image
FR_BSS_END = 0x8C900000          # BSS up to the overlay area

# Link addresses for US v1.005 (from SA Tools keys, verified by pointers, or
# inferred).  Only used as candidates; every value is re-verified.
KNOWN_BASES = [0x0C900000, 0x0C920000, 0x0CADC000, 0x0CB00000, 0x0CB08000,
               0x0CB80000, 0x0CC00000, 0x8C900000]

TEXTURE_MAGICS = (b'PVMH', b'GBIX', b'PVRT')

# a data-only file without an SA Tools key is still catalogued as a linked
# module when at least this many words point inside it at a candidate base
# (e.g. SHTPATH1/2, Sky Chase path data linked at 0x0CB80000)
MIN_DATA_SELF_PTRS = 64

# --------------------------------------------------------------------------
# SH-4 code heuristics
# --------------------------------------------------------------------------

_VALID = bytearray(65536)        # 1 if the halfword decodes to an SH-4 instruction
_SLOT_OK = bytearray(65536)      # 1 if legal in a delay slot


def _init_tables():
    for w in range(65536):
        ins = decode(w, 0)
        if ins is None:
            continue
        _VALID[w] = 1
        if not ins.slot_illegal():
            _SLOT_OK[w] = 1


_init_tables()

W_FILL = 0x0900                  # filler halfword (bytes 00 09) used before literal pools
PAT_PRO = bytes([0x22, 0x4F])    # STS.L PR,@-R15
PAT_EPI = bytes([0x26, 0x4F])    # LDS.L @R15+,PR
PAT_FILL = re.compile(b'(?:' + re.escape(bytes([0x00, 0x09])) + b'){2,}')


def _even_hits(data, pattern, lo=0, hi=None):
    if hi is None:
        hi = len(data)
    out = []
    i = data.find(pattern, lo, hi)
    while i >= 0:
        if not (i & 1):
            out.append(i)
        i = data.find(pattern, i + 1, hi)
    return out


def _hw(data, off):
    return data[off] | (data[off + 1] << 8)


def _window_valid(data, start, count):
    if start < 0 or start + 2 * count > len(data):
        return False
    for k in range(count):
        if not _VALID[_hw(data, start + 2 * k)]:
            return False
    return True


def _is_term(w):
    """Delayed unconditional control transfer: RTS, RTE, JMP @Rn, BRA, BRAF Rn."""
    return (w == 0x000B or w == 0x002B or (w & 0xF0FF) == 0x402B
            or (w >> 12) == 0xA or (w & 0xF0FF) == 0x0023)


def _is_push(w):
    """Prologue instructions that may precede STS.L PR,@-R15."""
    return ((w & 0xFF0F) == 0x2F06          # MOV.L Rm,@-R15
            or (w & 0xFF0F) == 0xFF0B       # FMOV.S FRm,@-R15
            or w in (0x4F12, 0x4F02, 0x4F13, 0x4F52, 0x4F62)  # STS.L MACL/MACH, STC.L GBR, STS.L FPUL/FPSCR
            or (w & 0xFF80) == 0x7F80)      # ADD #-imm,R15


def _skip_fill(data, q, n):
    while q + 2 <= n and _hw(data, q) == W_FILL:
        q += 2
    return q


def _switch_targets(data, braf, table, n):
    """SHC switch: MOVA table,R0 / MOV.W @(R0,Rx),R0 / BRAF R0; table of
    16-bit offsets relative to BRAF+4.  Returns (targets, table_end)."""
    base = braf + 4
    targets = []
    k = table
    limit = n
    while k + 2 <= n and k < limit and len(targets) < 1024:
        e = _hw(data, k)
        if e & 0x8000:
            e -= 0x10000
        t = base + e
        if (t & 1) or t <= k or t >= n:
            break
        targets.append(t)
        limit = min(limit, t)
        k += 2
    return targets, k


def _sweep(data, s, n):
    """Linear sweep from a function entry; returns end offset of the code run.

    Literal pools (targets of PC-relative loads) and switch tables found after
    an unconditional delayed branch are skipped, as is 00 09 filler; pending
    forward branch targets allow skipping other embedded data."""
    p = s
    pend = []                    # (start, end) of literal-pool entries referenced ahead
    btg = set()                  # pending forward branch targets
    last_mova = -1
    while p + 2 <= n:
        w = _hw(data, p)
        if not _VALID[w]:
            ahead = [b for b in btg if b > p]
            if ahead and min(ahead) - p <= 0x800:
                p = min(ahead)
                btg = {b for b in btg if b > p}
                continue
            break
        top = w >> 12
        if top == 0xD:                                  # MOV.L @(d,PC),Rn
            t = (p & ~3) + 4 + (w & 0xFF) * 4
            pend.append((t, t + 4))
        elif (w & 0xFF00) == 0xC700:                    # MOVA @(d,PC),R0
            t = (p & ~3) + 4 + (w & 0xFF) * 4
            pend.append((t, t + 4))
            last_mova = t
        elif top == 0x9:                                # MOV.W @(d,PC),Rn
            t = p + 4 + (w & 0xFF) * 2
            pend.append((t, t + 2))
        elif (w & 0xF900) == 0x8900:                    # BT, BF, BT/S, BF/S
            d = w & 0xFF
            t = p + 4 + (d - 256 if d & 0x80 else d) * 2
            if t > p:
                btg.add(t)
        elif top == 0xA:                                # BRA
            d = w & 0xFFF
            t = p + 4 + (d - 4096 if d & 0x800 else d) * 2
            if t > p:
                btg.add(t)
        if (w & 0xF0FF) == 0x0023 and p < last_mova < p + 0x40:   # BRAF via switch table
            tg, tend = _switch_targets(data, p, last_mova, n)
            btg.update(tg)
            pend.append((last_mova, tend))
        if not _is_term(w):
            p += 2
            continue
        if p + 4 > n or not _SLOT_OK[_hw(data, p + 2)]:
            p += 2
            break
        p += 4
        q = _skip_fill(data, p, n)
        while True:
            fut = [t for t in pend if t[0] >= p]
            if fut and min(t[0] for t in fut) <= q + 2:          # literal pool / table here
                pool_end = max(t[1] for t in fut if t[0] < q + 0x400)
                nq = _skip_fill(data, max(q, (pool_end + 3) & ~3), n)
                pend = [t for t in fut if t[0] >= nq]
                if nq == q:
                    break
                q = nq
                continue
            pend = fut
            break
        p = min(q, n)
        btg = {b for b in btg if b >= p}
        if not pend and not btg and not _window_valid(data, p, 4):
            break                                       # end of a code section
    return min(p, n)


def find_code(data, entry_hint=None):
    """Return dict: code regions (file offsets), byte count, prologue count."""
    n = len(data) & ~1
    seeds = []
    for off in _even_hits(data, PAT_PRO):
        if off < 2 or not _window_valid(data, off + 2, 6):
            continue
        prev = _hw(data, off - 2)
        before = [_hw(data, k) for k in range(max(0, off - 8), off, 2)]
        if not (any(_is_push(b) for b in before) or (_hw(data, off + 2) & 0xFF80) == 0x7F80
                or prev in (W_FILL, 0x0009, 0x0000)
                or (off >= 4 and _is_term(_hw(data, off - 4)))):
            continue
        e = off
        while e >= 2 and off - e < 24 and (_is_push(_hw(data, e - 2))
                                           or (_hw(data, e - 2) & 0xF00F) == 0x6003):  # MOV Rm,Rn
            e -= 2
        seeds.append((e, 'P'))
    for m in PAT_FILL.finditer(data):
        off = m.start()
        if off & 1 or off < 4:
            continue
        if _is_term(_hw(data, off - 4)) and _SLOT_OK[_hw(data, off - 2)]:
            seeds.append((off - 4, 'F'))
    if entry_hint is not None and 0 <= entry_hint < n and not (entry_hint & 1):
        seeds.append((entry_hint, 'H'))
    seeds.sort()

    regions = []
    for e, kind in seeds:
        if any(s <= e < t for s, t in regions):
            continue
        s = e
        while s >= 2 and _VALID[_hw(data, s - 2)] and not any(a < s <= b for a, b in regions):
            s -= 2
        t = _sweep(data, s, n)
        if t <= e:
            continue
        seg = data[s:t]
        has_ret = (any(_is_term(_hw(seg, i)) for i in range(0, len(seg) - 1, 2)))
        if not has_ret or t - s < 8:
            continue
        if kind == 'P' and PAT_EPI not in seg:
            continue                     # a real STS.L PR function restores PR
        regions.append((s, t))
        regions.sort()
    # merge overlapping / touching regions
    merged = []
    for s, t in sorted(regions):
        if merged and s <= merged[-1][1]:
            merged[-1] = (merged[-1][0], max(merged[-1][1], t))
        else:
            merged.append((s, t))
    n_pro = sum(len(_even_hits(data, PAT_PRO, s, t)) for s, t in merged)
    return {
        'regions': merged,
        'code_bytes': sum(t - s for s, t in merged),
        'n_prologues': n_pro,
        'n_prologues_raw': len(_even_hits(data, PAT_PRO)),
    }


# --------------------------------------------------------------------------
# Pointers / load address
# --------------------------------------------------------------------------

def words(data):
    return struct.unpack_from('<%dI' % (len(data) // 4), data)


def self_ptr_count(ws, base, size):
    hi = base + size
    return sum(1 for w in ws if base <= w <= hi)


def choose_base(ws, size, cands):
    best = None
    for b in cands:
        c = self_ptr_count(ws, b, size)
        if best is None or c > best[1]:
            best = (b, c)
    return best


def refs_1st_read(ws):
    r = {'code': 0, 'data': 0, 'lib': 0, 'bss': 0}
    for w in ws:
        if FR_BASE <= w < FR_CODE_END:
            r['code'] += 1
        elif FR_CODE_END <= w < FR_DATA_END:
            r['data'] += 1
        elif FR_DATA_END <= w < FR_LIB_END:
            r['lib'] += 1
        elif FR_LIB_END <= w < FR_BSS_END:
            r['bss'] += 1
    r['total'] = r['code'] + r['data'] + r['lib'] + r['bss']
    return r


# --------------------------------------------------------------------------
# SA Tools ini keys
# --------------------------------------------------------------------------

def read_ini_keys(dirs):
    keys = {}
    for d in dirs:
        if not d or not os.path.isdir(d):
            continue
        for fn in sorted(os.listdir(d)):
            if not fn.lower().endswith('.ini'):
                continue
            path = os.path.join(d, fn)
            try:
                with open(path, 'r', encoding='utf-8', errors='replace') as f:
                    head = f.read(4096)
            except OSError:
                continue
            m_df = re.search(r'^\s*datafile\s*=\s*(\S+)', head, re.M | re.I)
            m_key = re.search(r'^\s*key\s*=\s*([0-9A-Fa-f]+)', head, re.M | re.I)
            if not (m_df and m_key):
                continue
            stem = os.path.splitext(os.path.basename(m_df.group(1)))[0].upper()
            key = int(m_key.group(1), 16)
            src = os.path.basename(d.rstrip('/\\')) + '/' + fn
            keys.setdefault(stem, (key, src))
    return keys


def sha1(b):
    return hashlib.sha1(b).hexdigest()


# --------------------------------------------------------------------------

def main(argv=None):
    root_default = os.path.dirname(HERE)
    parent = os.path.dirname(root_default)
    ap = argparse.ArgumentParser(description='Extract Sonic Adventure DC overlays into baserom/')
    ap.add_argument('--root', default=root_default, help='sadc project root (contains disc/)')
    ap.add_argument('--satools', default=os.path.join(parent, 'SATools', 'GameConfig', 'DC_SA1'))
    ap.add_argument('--adventurerip', default=os.path.join(parent, 'AdventureRip'))
    ap.add_argument('--verify-recompress', action='store_true',
                    help='check that prs.compress() reproduces each original .PRS byte-for-byte (slow)')
    ap.add_argument('--only', nargs='*', help='restrict to these PRS names (without extension)')
    args = ap.parse_args(argv)

    root = os.path.abspath(args.root)
    disc = os.path.join(root, 'disc')
    sonicadv = os.path.join(disc, 'SONICADV')
    outdir = os.path.join(root, 'baserom')
    ovldir = os.path.join(outdir, 'overlays')
    os.makedirs(ovldir, exist_ok=True)

    fr_src = os.path.join(disc, '1ST_READ.BIN')
    fr_dst = os.path.join(outdir, '1ST_READ.BIN')
    shutil.copyfile(fr_src, fr_dst)
    with open(fr_dst, 'rb') as f:
        fr = f.read()
    print('1ST_READ.BIN size=0x%X sha1=%s' % (len(fr), sha1(fr)))

    ini_keys = read_ini_keys([args.satools, args.adventurerip])
    print('SA Tools ini keys found: %d' % len(ini_keys))

    names = sorted(fn for fn in os.listdir(sonicadv) if fn.upper().endswith('.PRS'))
    if args.only:
        want = {x.upper() for x in args.only}
        names = [fn for fn in names if os.path.splitext(fn)[0].upper() in want]

    prev = {}
    prev_json = os.path.join(outdir, 'overlays.json')
    if os.path.isfile(prev_json):
        try:
            with open(prev_json) as f:
                prev = {e['name']: e for e in json.load(f)}
        except (ValueError, KeyError, TypeError):
            prev = {}

    results = []
    skipped = []
    n_exact = n_checked = 0
    for fn in names:
        stem = os.path.splitext(fn)[0].upper()
        prs_path = os.path.join(sonicadv, fn)
        with open(prs_path, 'rb') as f:
            src = f.read()
        data, used = prs.decompress_ex(src)
        if data[:4] in TEXTURE_MAGICS:
            skipped.append({'name': stem, 'reason': 'texture archive %s' % data[:4].decode('latin1'),
                            'size': len(data)})
            continue
        size = len(data)
        ws = words(data)

        # link address
        key = None
        if stem in ini_keys:
            key, ini_src = ini_keys[stem]
            cands = sorted({key, key | 0x80000000, key & 0x1FFFFFFF})
            base, hits = choose_base(ws, size, cands)
            src_kind = 'satools:' + ini_src
        else:
            base, hits = choose_base(ws, size, KNOWN_BASES)
            src_kind = 'inferred-from-pointers'
        entry_hint = None
        if ws and base <= ws[0] < base + size and stem.startswith('EV'):
            entry_hint = ws[0] - base            # event overlays: word 0 = entry point
            src_kind += '+ev-header'

        code = find_code(data, entry_hint)
        has_code = code['code_bytes'] > 0
        if not has_code and key is None and hits < MIN_DATA_SELF_PTRS:
            skipped.append({'name': stem, 'reason': 'no SH-4 code and no known load address',
                            'size': size, 'self_pointers_best_base': hits})
            continue

        bin_path = os.path.join(ovldir, stem + '.BIN')
        with open(bin_path, 'wb') as f:
            f.write(data)
        selfp = [w for w in ws if base <= w <= base + size]
        refs = refs_1st_read(ws)
        code_ws = []
        for s, t in code['regions']:
            s4 = (s + 3) & ~3
            code_ws.extend(words(data[s4:t]))
        code_refs = refs_1st_read(code_ws)
        entry = {
            'name': stem,
            'prs_path': os.path.relpath(prs_path, root).replace('\\', '/'),
            'bin_path': os.path.relpath(bin_path, root).replace('\\', '/'),
            'load_addr': '0x%08X' % base,
            'load_addr_p1': '0x%08X' % (base | 0x80000000),
            'load_addr_source': src_kind,
            'satools_key': ('%X' % key) if key is not None else None,
            'size': size,
            'size_hex': '0x%X' % size,
            'end_addr': '0x%08X' % (base + size),
            'has_code': has_code,
            'code_estimate_bytes': code['code_bytes'],
            'code_regions': [['0x%X' % s, '0x%X' % t] for s, t in code['regions']],
            'n_prologues': code['n_prologues'],
            'n_prologues_raw': code['n_prologues_raw'],
            'self_pointers': len(selfp),
            'self_pointer_range': ['0x%08X' % min(selfp), '0x%08X' % max(selfp)] if selfp else None,
            'refs_1st_read': refs,
            'refs_1st_read_from_code': code_refs,
            'sha1_bin': sha1(data),
            'sha1_prs': sha1(src),
            'prs_size': len(src),
        }
        if args.verify_recompress:
            exact = prs.compress(data) == src
            entry['prs_recompress_exact'] = exact
            n_checked += 1
            n_exact += int(exact)
        elif stem in prev and prev[stem].get('sha1_prs') == entry['sha1_prs'] \
                and 'prs_recompress_exact' in prev[stem]:
            entry['prs_recompress_exact'] = prev[stem]['prs_recompress_exact']   # from an earlier run
        results.append(entry)
        print('%-12s base=%s size=0x%07X code=%s 0x%06X (%2d regions) pro=%4d self=%6d refs1st=%5d %s%s' % (
            stem, entry['load_addr'], size, 'Y' if has_code else 'n', code['code_bytes'],
            len(code['regions']), code['n_prologues'], len(selfp), refs['total'], src_kind,
            '' if not args.verify_recompress else (' prs-exact' if entry['prs_recompress_exact'] else ' PRS-DIFF')),
            flush=True)

    out_list = results
    if args.only:            # partial run: update entries in the existing catalogue
        merged = dict(prev)
        merged.update({e['name']: e for e in results})
        out_list = [merged[k] for k in sorted(merged)]
    with open(os.path.join(outdir, 'overlays.json'), 'w', newline='\n') as f:
        json.dump(out_list, f, indent=1)
        f.write('\n')
    if not args.only:
        with open(os.path.join(outdir, 'overlays_skipped.json'), 'w', newline='\n') as f:
            json.dump(skipped, f, indent=1)
            f.write('\n')
        # sha1sum-compatible checksum list (run "sha1sum -c baserom.sha1" inside baserom/)
        with open(os.path.join(outdir, 'baserom.sha1'), 'w', newline='\n') as f:
            f.write('%s  1ST_READ.BIN\n' % sha1(fr))
            for e in results:
                f.write('%s  overlays/%s.BIN\n' % (e['sha1_bin'], e['name']))
    print('overlays: %d written (%d with code, %d bytes of code est.); skipped %d PRS files' % (
        len(results), sum(1 for e in results if e['has_code']),
        sum(e['code_estimate_bytes'] for e in results), len(skipped)))
    if args.verify_recompress:
        print('PRS recompression exact: %d / %d' % (n_exact, n_checked))
    return 0


if __name__ == '__main__':
    sys.exit(main())
