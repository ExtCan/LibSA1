#!/usr/bin/env python3
"""texdump_ref.py - an independent reference decoder for the PVR textures libsa1 decodes (tests/texdump.c).

Reads the character texture archives (PRS-compressed PVM files) straight from the user's .gdi (never copied;
nothing is written but the report and, with --png, reference PNGs), decodes every PVR from the format description
below (written for this test from the PowerVR / PVR file format, independently of libsa1's src/sa1_pvr.c) and
compares each texture texel by texel with libsa1's dump (texdump.exe: DIR/<char>/<NN>_<ARCHIVE>/index.txt + PNGs).
Optionally also against PNGs made by another decoder (--other DIR: DIR/<ARCHIVE>/<name>.png, e.g. the output of
SA Tools' "ArchiveTool -png <ARCHIVE>.PRS", which uses VrSharp).

  py -3.12 tests/texdump_ref.py [--gdi GDI] [--dump build/texdump] [--other DIR] [--png OUT] [--archives A,B]

PVR file layout (PVRT chunk): "PVRT", u32 size, u8 pixel format, u8 data format, u16 0, u16 width, u16 height, data.
  pixel formats: 0 ARGB1555, 1 RGB565, 2 ARGB4444, 3 YUV422, (4 bump, 5/6 palettised: need a palette, not decoded)
  data formats:  1 square twiddled, 2 twiddled + mipmaps, 3 VQ, 4 VQ + mipmaps, 5-8 palettised (4/8 bpp, +mips),
                 9 rectangle (linear), 0x0B stride (linear), 0x0D rectangle twiddled, 0x10 small VQ,
                 0x11 small VQ + mipmaps, 0x12 twiddled + mipmaps (alternative padding)
  twiddled: texel (x, y) is texel number interleave(x, y) with the bits of y on the even and those of x on the odd
            positions (Morton order); a rectangle is a row/column of squares of the smaller side, one after another
  mipmaps:  stored smallest level first, the full-size level last. 16-bit twiddled: the 1x1 level occupies as much as
            two texels (format 2: 2 bytes of padding before it) or as much as a 2x2 level (format 0x12: 6 bytes);
            VQ: one padding byte (the 1x1 level) before the 2x2 level's index byte. Chunks may end with padding.
  VQ:       a codebook of 2x2-texel entries (4 x u16, texels in twiddled order: (0,0) (0,1) (1,0) (1,1)), then one
            index byte per 2x2 block, the blocks twiddled; VQ = 256 entries; small VQ = fewer entries, by the width:
            without mipmaps <= 16: 16, 32: 32, 64: 128, else 256; with mipmaps <= 16: 16, 32: 64, else 256
"""
import argparse
import os
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
SADC_TOOLS = os.path.join(os.path.dirname(os.path.dirname(HERE)), "sadc", "tools")
sys.path.insert(0, SADC_TOOLS)
import prs  # noqa: E402  (sadc's own PRS implementation)

CHAR_ARCHIVES = {
    "sonic": ["SONIC", "SON_EFF"], "eggman": ["EGGMAN"], "tails": ["MILES"], "knuckles": ["KNUCKLES", "KNU_EFF"],
    "tikal": ["TIKAL"], "amy": ["AMY", "AMY_EFF"], "gamma": ["E102", "E102BEAM", "E102EFFECT", "E102TIME", "E102HIT"],
    "big": ["BIG", "FISHING", "BIG_KAERU"], "supersonic": ["SUPERSONIC"],
    # the common archives every character set gets last, in the stage start's order (src/sa1_loader.c
    # load_common_textures): OBJ_REGULAR (the blob shadow's texture 58 "shadow"), EFF_REGULAR (the particle textures),
    # CON_REGULAR (round 0.9.1: the HUD's digits, labels and life icons, sadc docs/research/gsa1_070_hud.md F5)
    "common": ["OBJ_REGULAR", "EFF_REGULAR", "CON_REGULAR"],
}


# ---- disc (ISO9660 in the last data track of the .gdi) -------------------------------------------------------------
class Disc:
    def __init__(self, gdi):
        base = os.path.dirname(os.path.abspath(gdi))
        lines = [l.strip() for l in open(gdi, encoding="ascii", errors="replace") if l.strip()]
        self.tracks = []
        for line in lines[1:int(lines[0]) + 1]:
            p = line.split()
            if int(p[2]) != 4:
                continue
            name = " ".join(p[4:-1]).strip('"')
            path = os.path.join(base, name)
            ss = int(p[3])
            self.tracks.append((int(p[1]), ss, path, os.path.getsize(path) // ss))
        self.files = {}
        pvd = self.read(self.tracks[-1][0] + 16, 1)
        assert pvd[1:6] == b"CD001", "no ISO9660 volume"
        self._walk(pvd[156:156 + 34], "")

    def read(self, lba, count):
        out = bytearray()
        for i in range(count):
            for t0, ss, path, n in self.tracks:
                if t0 <= lba + i < t0 + n:
                    with open(path, "rb") as f:
                        f.seek((lba + i - t0) * ss + (16 if ss == 2352 else 0))
                        out += f.read(2048)
                    break
            else:
                raise ValueError("LBA %d outside the data tracks" % (lba + i))
        return bytes(out)

    def _walk(self, rec, path):
        ext, size = struct.unpack_from("<I", rec, 2)[0], struct.unpack_from("<I", rec, 10)[0]
        raw = self.read(ext, (size + 2047) // 2048)
        pos = 0
        while pos < size:
            ln = raw[pos]
            if ln == 0:
                pos = (pos // 2048 + 1) * 2048
                continue
            r = raw[pos:pos + ln]
            pos += ln
            name = r[33:33 + r[32]]
            if name in (b"\x00", b"\x01"):
                continue
            name = name.decode("ascii").split(";")[0]
            if r[25] & 2:
                self._walk(r, path + name + "/")
            else:
                self.files[(path + name).upper()] = (struct.unpack_from("<I", r, 2)[0],
                                                     struct.unpack_from("<I", r, 10)[0])

    def get(self, name):
        ext, size = self.files[name.upper()]
        return self.read(ext, (size + 2047) // 2048)[:size]


# ---- PVM / PVR -----------------------------------------------------------------------------------------------------
def pvm_entries(d):
    """[(name, gbix, PVRT chunk bytes)] in archive order"""
    assert d[:4] == b"PVMH", "not a PVM"
    hsize = struct.unpack_from("<I", d, 4)[0]
    flags, n = struct.unpack_from("<HH", d, 8)
    es = 2 + (28 if flags & 1 else 0) + (2 if flags & 2 else 0) + (2 if flags & 4 else 0) + (4 if flags & 8 else 0)
    names = []
    for i in range(n):
        e = 12 + es * i + 2
        names.append(d[e:e + 28].split(b"\0")[0].decode("latin-1") if flags & 1 else "%03d" % i)
    out, pos, gbix = [], 8 + hsize, None
    while len(out) < n and pos + 8 <= len(d):
        tag = d[pos:pos + 4]
        if tag == b"GBIX":
            gbix = struct.unpack_from("<I", d, pos + 8)[0]
            pos += 8 + struct.unpack_from("<I", d, pos + 4)[0]
            continue
        if tag != b"PVRT":
            pos += 1
            continue
        size = struct.unpack_from("<I", d, pos + 4)[0]
        out.append((names[len(out)], gbix, d[pos:pos + 8 + size]))
        gbix = None
        pos += 8 + size
    return out


def interleave(x, y):
    r = 0
    for b in range(12):
        r |= ((y >> b) & 1) << (2 * b) | ((x >> b) & 1) << (2 * b + 1)
    return r


def texel_index(x, y, w, h):
    """twiddled texel number of (x, y) in a w x h texture (rectangles: squares of the smaller side)"""
    s = min(w, h)
    return (x // s + y // s) * s * s + interleave(x % s, y % s)


def colour(pf, v):
    if pf == 0:
        return (((v >> 10) & 31) * 255 // 31, ((v >> 5) & 31) * 255 // 31, (v & 31) * 255 // 31,
                255 if v & 0x8000 else 0)
    if pf == 1:
        return (((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31, 255)
    if pf == 2:
        return (((v >> 8) & 15) * 17, ((v >> 4) & 15) * 17, (v & 15) * 17, ((v >> 12) & 15) * 17)
    raise ValueError("pixel format %d" % pf)


def yuv_pair(w0, w1):
    y0, u, y1, v = w0 >> 8, w0 & 255, w1 >> 8, w1 & 255
    out = []
    for yy in (y0, y1):
        r = yy + 1.375 * (v - 128)
        g = yy - 0.34375 * (u - 128) - 0.6875 * (v - 128)
        b = yy + 1.71875 * (u - 128)
        out.append(tuple(max(0, min(255, int(c))) for c in (r, g, b)) + (255,))
    return out


def decode(chunk):
    """(w, h, pf, df, rgba bytes or None, note)"""
    pf, df = chunk[8], chunk[9]
    w, h = struct.unpack_from("<HH", chunk, 12)
    data = chunk[16:]
    px = bytearray(w * h * 4)

    def u16(off):
        return struct.unpack_from("<H", data, off)[0]

    def put(x, y, c):
        px[4 * (y * w + x):4 * (y * w + x) + 4] = bytes(c)

    if pf in (0, 1, 2, 3) and df in (1, 2, 0x0D, 0x12, 9, 0x0B):
        off = 0
        if df in (2, 0x12):
            off, s = (2 if df == 2 else 6), 1
            while s < w:
                off += 2 * s * s
                s *= 2
        linear = df in (9, 0x0B)
        if pf == 3:
            for y in range(h):
                for x in range(0, w, 2):
                    i0 = y * w + x if linear else texel_index(x, y, w, h)
                    i1 = y * w + x + 1 if linear else texel_index(x + 1, y, w, h)
                    a, b = yuv_pair(u16(off + 2 * i0), u16(off + 2 * i1))
                    put(x, y, a)
                    put(x + 1, y, b)
            return w, h, pf, df, bytes(px), ""
        for y in range(h):
            for x in range(w):
                i = y * w + x if linear else texel_index(x, y, w, h)
                put(x, y, colour(pf, u16(off + 2 * i)))
        return w, h, pf, df, bytes(px), ""
    if pf in (0, 1, 2) and df in (3, 4, 0x10, 0x11):
        mips = df in (4, 0x11)
        if df in (3, 4):
            entries = 256
        elif not mips:
            entries = 16 if w <= 16 else 32 if w == 32 else 128 if w == 64 else 256
        else:
            entries = 16 if w <= 16 else 64 if w == 32 else 256
        off = 8 * entries
        if mips:
            off += 1
            s = 2
            while s < w:
                off += (s // 2) * (s // 2)
                s *= 2
        bw, bh = w // 2, h // 2
        for by in range(bh):
            for bx in range(bw):
                code = data[off + texel_index(bx, by, bw, bh)]
                for t in range(4):
                    put(2 * bx + (t >> 1), 2 * by + (t & 1), colour(pf, u16(8 * code + 2 * t)))
        return w, h, pf, df, bytes(px), "codebook %d" % entries
    return w, h, pf, df, None, "not decoded (pixel format %d, data format 0x%X)" % (pf, df)


# ---- PNG -----------------------------------------------------------------------------------------------------------
def read_png(path):
    d = open(path, "rb").read()
    assert d[:8] == b"\x89PNG\r\n\x1a\n", path
    pos, idat, plte, trns = 8, b"", None, None
    w = h = bd = ct = il = 0
    while pos < len(d):
        ln, typ = struct.unpack(">I4s", d[pos:pos + 8])
        body = d[pos + 8:pos + 8 + ln]
        pos += 12 + ln
        if typ == b"IHDR":
            w, h, bd, ct, _, _, il = struct.unpack(">IIBBBBB", body)
        elif typ == b"IDAT":
            idat += body
        elif typ == b"PLTE":
            plte = body
        elif typ == b"tRNS":
            trns = body
    assert bd == 8 and il == 0, "unsupported PNG " + path
    ch = {6: 4, 2: 3, 3: 1, 0: 1, 4: 2}[ct]
    raw = zlib.decompress(idat)
    stride = w * ch
    out = bytearray(w * h * 4)
    prev = bytearray(stride)
    p = 0
    for y in range(h):
        f = raw[p]
        line = bytearray(raw[p + 1:p + 1 + stride])
        p += 1 + stride
        for i in range(stride):
            a = line[i - ch] if i >= ch else 0
            b = prev[i]
            c = prev[i - ch] if i >= ch else 0
            if f == 1:
                line[i] = (line[i] + a) & 255
            elif f == 2:
                line[i] = (line[i] + b) & 255
            elif f == 3:
                line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        prev = line
        for x in range(w):
            o = 4 * (y * w + x)
            if ch == 4:
                out[o:o + 4] = line[4 * x:4 * x + 4]
            elif ch == 3:
                out[o:o + 3] = line[3 * x:3 * x + 3]
                out[o + 3] = 255
            elif ct == 3:
                i = line[x]
                out[o:o + 3] = plte[3 * i:3 * i + 3]
                out[o + 3] = trns[i] if trns and i < len(trns) else 255
            elif ch == 2:
                out[o:o + 3] = bytes([line[2 * x]] * 3)
                out[o + 3] = line[2 * x + 1]
            else:
                out[o:o + 3] = bytes([line[x]] * 3)
                out[o + 3] = 255
    return w, h, bytes(out)


def write_png(path, w, h, rgba):
    raw = b"".join(b"\x00" + rgba[4 * w * y:4 * w * (y + 1)] for y in range(h))

    def chunk(t, b):
        return struct.pack(">I", len(b)) + t + b + struct.pack(">I", zlib.crc32(t + b) & 0xFFFFFFFF)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) +
                chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def texlist_names(disc):
    """The texture numbers of the game's texlists against the PVM entries, by name: the game's PVM list
    (1ST_READ.BIN 0x8C1C2584: per character id {char *name; NJS_TEXLIST *list}[]) names the archive and the texlist a
    PVM is loaded into; a texlist entry is {char *filename; u32 attr; u32 texaddr} and texture number i of a model is
    entry i. The common archives (CHAR_ARCHIVES["common"]) come from the stage start's list (0x8C1C2450, the same
    layout) with character id -1. Returns [(character id, archive, texlist, entries, named, matching, mismatches)]."""
    b = disc.get("1ST_READ.BIN")
    base = 0x8C010000

    def u32(a):
        a &= 0x1FFFFFFF
        a -= base & 0x1FFFFFFF
        return struct.unpack_from("<I", b, a)[0] if 0 <= a <= len(b) - 4 else None

    def cstr(a):
        a = (a & 0x1FFFFFFF) - (base & 0x1FFFFFFF)
        if not 0 <= a < len(b):
            return None
        return b[a:b.index(b"\0", a)].decode("latin-1")
    out = []
    for cid in list(range(8)) + [-1]:
        lst = u32(0x8C1C2584 + 4 * cid) if cid >= 0 else 0x8C1C2450
        i = 0
        while lst is not None:
            nm, tl = u32(lst + 8 * i), u32(lst + 8 * i + 4)
            if not nm:
                break
            arc = cstr(nm)
            i += 1
            if cid < 0 and arc not in CHAR_ARCHIVES["common"]:
                continue        # common archives libsa1 does not load (EXTRA, HINT, EME_KIRAN ...)
            if u32(tl) is None:
                out.append((cid, arc, tl, None, 0, 0, []))   # texlist in an overlay (TIKAL_PROG)
                continue
            texs, n = u32(tl), u32(tl + 4)
            ents = pvm_entries(prs.decompress(disc.get("SONICADV/%s.PRS" % arc)))
            named = match = 0
            bad = []
            for k in range(n):
                fn = u32(texs + 12 * k)
                tname = cstr(fn) if fn else None
                if not tname:
                    continue
                named += 1
                if k < len(ents) and ents[k][0] == tname:
                    match += 1
                else:
                    bad.append((k, tname, ents[k][0] if k < len(ents) else None))
            out.append((cid, arc, tl, n, named, match, bad))
    return out


def compare(a, b, tol=1):
    """texels that differ by more than tol in a channel (texels transparent in both: ignored)"""
    bad = 0
    for i in range(0, len(a), 4):
        if a[i + 3] == 0 and b[i + 3] == 0:
            continue
        if abs(a[i] - b[i]) > tol or abs(a[i + 1] - b[i + 1]) > tol or abs(a[i + 2] - b[i + 2]) > tol or \
                abs(a[i + 3] - b[i + 3]) > tol:
            bad += 1
    return bad


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--gdi", default=os.environ.get("SA1_TEST_GDI"))
    ap.add_argument("--dump", default=os.path.join(os.path.dirname(HERE), "build", "texdump"),
                    help="texdump.exe output directory (default LIB/build/texdump; '' = none)")
    ap.add_argument("--other", help="folder of PNGs of another decoder: OTHER/<ARCHIVE>/<name>.png")
    ap.add_argument("--png", help="also write the reference decode as PNGs into this directory")
    ap.add_argument("--archives", help="comma-separated archive names, any PVM of SONICADV (default: every character archive)")
    args = ap.parse_args()
    if not args.gdi:
        ap.error("no .gdi (--gdi or SA1_TEST_GDI)")
    disc = Disc(args.gdi)
    wanted = args.archives.upper().split(",") if args.archives else None
    dumped = {}     # archive -> {index: (name, png path)}
    if args.dump and os.path.isdir(args.dump):
        for root, _, files in os.walk(args.dump):
            if "index.txt" not in files:
                continue
            for line in open(os.path.join(root, "index.txt"), encoding="utf-8"):
                if line.startswith("#") or not line.strip():
                    continue
                p = line.split()    # index name gbix pf df w h file archive
                dumped.setdefault(p[8], {})[int(p[0])] = (p[1], os.path.join(root, p[7]) if p[7] != "-" else None)
    seen = set()
    totals = {"textures": 0, "ref_undecoded": 0, "lib_equal": 0, "lib_differ": 0, "lib_missing": 0,
              "other_equal": 0, "other_differ": 0}
    for arcs in ([wanted] if wanted else CHAR_ARCHIVES.values()):
        for arc in arcs:
            if arc in seen:
                continue
            seen.add(arc)
            try:
                raw = disc.get("SONICADV/%s.PRS" % arc)
            except KeyError:
                print("%-12s not on the disc" % arc)
                continue
            ents = pvm_entries(prs.decompress(raw))
            notes = []
            for i, (name, gbix, chunk) in enumerate(ents):
                totals["textures"] += 1
                w, h, pf, df, ref, note = decode(chunk)
                if ref is None:
                    totals["ref_undecoded"] += 1
                    notes.append("%d:%s %s" % (i, name, note))
                    continue
                if args.png:
                    os.makedirs(os.path.join(args.png, arc), exist_ok=True)
                    write_png(os.path.join(args.png, arc, "%02d_%s.png" % (i, name)), w, h, ref)
                lib = dumped.get(arc, {}).get(i)
                if lib is None or lib[1] is None:
                    if dumped:
                        totals["lib_missing"] += 1
                        notes.append("%d:%s missing from libsa1's dump" % (i, name))
                else:
                    lw, lh, lp = read_png(lib[1])
                    bad = compare(ref, lp) if (lw, lh) == (w, h) else w * h
                    if lib[0] != name:
                        notes.append("%d: name %s in libsa1's dump, %s in the PVM" % (i, lib[0], name))
                        bad = max(bad, 1)
                    if bad:
                        totals["lib_differ"] += 1
                        notes.append("%d:%s pf %d df 0x%X %dx%d: libsa1 differs in %d texels" % (
                            i, name, pf, df, w, h, bad))
                    else:
                        totals["lib_equal"] += 1
                if args.other:
                    op = os.path.join(args.other, arc, name + ".png")
                    if os.path.exists(op):
                        ow, oh, opx = read_png(op)
                        bad = compare(ref, opx) if (ow, oh) == (w, h) else w * h
                        if bad:
                            totals["other_differ"] += 1
                            notes.append("%d:%s pf %d df 0x%X %dx%d: the other decoder differs in %d texels %s" % (
                                i, name, pf, df, w, h, bad, note))
                        else:
                            totals["other_equal"] += 1
            print("%-12s %3d textures" % (arc, len(ents)))
            for n in notes:
                print("    " + n)
    print("totals: " + ", ".join("%s %d" % kv for kv in totals.items()))
    ok = totals["lib_differ"] == 0 and totals["ref_undecoded"] == 0 and totals["other_differ"] == 0 and \
        totals["lib_missing"] == 0
    if not wanted:
        # texture number -> PVM entry: libsa1 uses the texlist index = the PVM entry index; check it by name
        print("texlist names against the PVM entries (texture number i = PVM entry i):")
        for cid, arc, tl, n, named, match, bad in texlist_names(disc):
            who = "char %d" % cid if cid >= 0 else "common"
            if n is None:
                print("  %-6s %-12s texlist %08X outside 1ST_READ (not checked)" % (who, arc, tl))
                continue
            print("  %-6s %-12s texlist %08X: %2d entries, %2d named, %2d named like their PVM entry%s" % (
                who, arc, tl, n, named, match, "" if not bad else "; MISMATCH %r" % bad[:4]))
            ok = ok and not bad
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
