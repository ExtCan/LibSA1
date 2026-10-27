#!/usr/bin/env python3
"""Proof of concept: rip a playable character straight from the user's own GD-ROM dump.

Sonic Adventure (Dreamcast, US v1.005 / MK-51000).  Everything is read from the
user's .gdi at run time; nothing from the game is bundled.  The only project code
used is tools/extract_gdi.py (track parsing) and tools/prs.py (PRS decompression);
the ISO9660 lookup, Ninja model/motion parsing, PVM/PVR texture decoding and the
PNG/OBJ writers are implemented here from the format descriptions in
docs/research/sa1_assets.md.

Usage:
    py -3.12 tools/research/rip_character.py <game.gdi> <outdir> [options]

Options:
    --char NAME       sonic (default), tails, knuckles, amy, big, gamma, eggman,
                      tikal, supersonic
    --action N        pose the model with entry N of the character's action list
    --frame F         frame of that action (default 0)
    --all-textures    decode every texture of the character PVM (default: only the
                      ones the model uses)
    --preview         also write a software-rendered preview PNG (front view)
    --list-actions    print the action list (motion pointers, frames, key types)

Output: <char>.obj + <char>.mtl + tex_<NN>_<name>.png in <outdir>.
"""
import argparse
import hashlib
import math
import os
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))          # sadc/tools
import extract_gdi  # noqa: E402
import prs          # noqa: E402

# ---------------------------------------------------------------------------
# Version check.  All addresses below are for US v1.005 only.
# ---------------------------------------------------------------------------
FIRST_READ_SHA1 = "e74c849806db103aa87f84e41a382873bc96b6df"
FIRST_READ_BASE = 0x8C010000

# Overlays that hold character data outside 1ST_READ.BIN: file -> load address
OVERLAYS = {
    "S_MOT.PRS": 0x0CC00000, "K_MOT.PRS": 0x0CC00000, "A_MOT.PRS": 0x0CC00000,
    "B_MOT.PRS": 0x0CC00000, "E_MOT.PRS": 0x0CC00000,
    "S_SBMOT.PRS": 0x0CB08000, "M_SBMOT.PRS": 0x0CADC000,
    "TIKAL_PROG.PRS": 0x0CB00000, "B_CHAOS7.PRS": 0x0C900000,
}

# Character table (US v1.005).  model = root NJS_OBJECT, pvm = texture archive,
# actions = PL_ACTION table (16 bytes per entry), weld = weld table (24 bytes per entry),
# image = file the model lives in, motfile = extra motion overlay loaded at 0x0CC00000.
CHARS = {
    "sonic":    dict(model=0x8C1CE76C, pvm="SONIC", actions=(0x8C1C55A0, 142), weld=0x8C1C5FA4,
                     image="1ST_READ.BIN", motfile="S_MOT.PRS"),
    "tails":    dict(model=0x8C25F968, pvm="MILES", actions=(0x8C256D6C, 132), weld=0x8C2576EC,
                     image="1ST_READ.BIN", motfile="S_MOT.PRS"),
    "knuckles": dict(model=0x8C35B094, pvm="KNUCKLES", actions=(0x8C3528CC, 111), weld=0x8C353114,
                     image="1ST_READ.BIN", motfile="K_MOT.PRS"),
    "amy":      dict(model=0x8C4C7C3C, pvm="AMY", actions=(0x8C4BB1D0, 98), weld=0x8C4BB8B4,
                     image="1ST_READ.BIN", motfile="A_MOT.PRS"),
    "big":      dict(model=0x8C52F298, pvm="BIG", actions=(0x8C525564, 101), weld=0x8C525C90,
                     image="1ST_READ.BIN", motfile="B_MOT.PRS"),
    "gamma":    dict(model=0x8C42CC34, pvm="E102", actions=(0x8C4225D4, 87), weld=0x8C422BE8,
                     image="1ST_READ.BIN", motfile="E_MOT.PRS"),
    "eggman":   dict(model=0x8C57A6BC, pvm="EGGMAN", actions=(0x8C57008C, 24), weld=0x8C5702BC,
                     image="1ST_READ.BIN", motfile=None),
    "tikal":    dict(model=0x0CB09818, pvm="TIKAL", actions=None, weld=0x0CB015A8,
                     image="TIKAL_PROG.PRS", motfile=None),
    "supersonic": dict(model=0x0C909D2C, pvm="SUPERSONIC", actions=None, weld=None,
                       image="B_CHAOS7.PRS", motfile=None),
}


# ---------------------------------------------------------------------------
# GD-ROM / ISO9660 access (single files, no full extraction)
# ---------------------------------------------------------------------------
class GdiFS:
    """Reads files from the ISO9660 filesystem in the high-density area of a .gdi."""

    def __init__(self, gdi_path):
        self.tracks = extract_gdi.parse_gdi(gdi_path)
        self.data_tracks = [t for t in self.tracks if t.ttype == 4]
        if not self.data_tracks:
            raise ValueError("no data tracks in %s" % gdi_path)
        hd = self.data_tracks[-1]
        pvd = self.read(hd.lba + 16)
        if pvd[1:6] != b"CD001":
            raise ValueError("no ISO9660 volume descriptor in the high-density area")
        self.root = pvd[156:156 + 34]

    def read(self, lba, count=1):
        out = bytearray()
        for i in range(count):
            for t in self.data_tracks:
                if t.lba <= lba + i < t.lba + t.nsectors:
                    out += t.read_sector(lba + i)
                    break
            else:
                raise ValueError("LBA %d is not in a data track" % (lba + i))
        return bytes(out)

    def _entries(self, rec):
        extent, size = struct.unpack_from("<I", rec, 2)[0], struct.unpack_from("<I", rec, 10)[0]
        raw = self.read(extent, (size + 2047) // 2048)
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
            yield name.decode("ascii").split(";")[0].upper(), r

    def open(self, path):
        rec = self.root
        parts = [p for p in path.replace("\\", "/").upper().split("/") if p]
        for part in parts:
            for name, r in self._entries(rec):
                if name == part:
                    rec = r
                    break
            else:
                raise FileNotFoundError(path)
        ext, sz = struct.unpack_from("<I", rec, 2)[0], struct.unpack_from("<I", rec, 10)[0]
        return self.read(ext, (sz + 2047) // 2048)[:sz]


# ---------------------------------------------------------------------------
# Memory images (1ST_READ.BIN + overlays), addressed by SH-4 pointers
# ---------------------------------------------------------------------------
class Mem:
    """A set of images mapped at their load addresses.  Pointers are compared by
    physical address (low 29 bits), so 0x8Cxxxxxx and 0x0Cxxxxxx alias."""

    def __init__(self):
        self.images = []   # (name, phys_base, data)

    def add(self, name, base, data):
        self.images.append((name, base & 0x1FFFFFFF, data))

    def find(self, addr, n=1):
        p = addr & 0x1FFFFFFF
        for name, base, data in self.images:
            if base <= p and p + n <= base + len(data):
                return data, p - base, name
        return None

    def valid(self, addr, n=1):
        return addr != 0 and self.find(addr, n) is not None

    def bytes(self, addr, n):
        f = self.find(addr, n)
        if f is None:
            raise ValueError("address 0x%08X (+%d) is not mapped" % (addr, n))
        data, off, _ = f
        return data[off:off + n]

    def unpack(self, fmt, addr):
        return struct.unpack(fmt, self.bytes(addr, struct.calcsize(fmt)))

    def u32(self, addr):
        return self.unpack("<I", addr)[0]

# ---------------------------------------------------------------------------
# Ninja basic models (NJS_OBJECT / NJS_MODEL / NJS_MESHSET / NJS_MATERIAL)
# ---------------------------------------------------------------------------
EVAL_UNIT_POS, EVAL_UNIT_ANG, EVAL_UNIT_SCL, EVAL_HIDE, EVAL_BREAK, EVAL_ZXY_ANG = 1, 2, 4, 8, 0x10, 0x20


class NjObject:
    __slots__ = ("addr", "flags", "model", "pos", "ang", "scl", "child", "sibling", "index")


def read_object_tree(mem, root):
    """Returns the nodes in Ninja traversal order (node, children, then siblings),
    which is also the order of the per-node entries of an NJS_MOTION."""
    nodes = []

    def visit(addr):
        while addr:
            o = NjObject()
            v = mem.unpack("<II3f3i3fII", addr)
            o.addr, o.flags, o.model = addr, v[0], v[1]
            o.pos, o.ang, o.scl = list(v[2:5]), list(v[5:8]), list(v[8:11])
            o.child, o.sibling = v[11], v[12]
            o.index = len(nodes)
            nodes.append(o)
            if o.child:
                visit(o.child)
            addr = o.sibling
    visit(root)
    return nodes


def read_model(mem, addr):
    """NJS_MODEL (basic, 0x28 bytes) -> dict with points, normals, meshsets, materials."""
    pts_p, nrm_p, npts, ms_p, mat_p, nms, nmat = mem.unpack("<IIiIIHH", addr)
    cx, cy, cz, r = mem.unpack("<4f", addr + 0x18)
    raw = mem.bytes(pts_p, 12 * npts)
    points = [struct.unpack_from("<3f", raw, 12 * i) for i in range(npts)]
    normals = None
    if nrm_p:
        raw = mem.bytes(nrm_p, 12 * npts)
        normals = [struct.unpack_from("<3f", raw, 12 * i) for i in range(npts)]
    mats = []
    for i in range(nmat):
        diff, spec, expo, attr_tex, attrflags = mem.unpack("<IIfII", mat_p + 20 * i)
        mats.append(dict(diffuse=diff, specular=spec, exponent=expo,
                         texid=attr_tex & ~0xC0000000, flags=attrflags))
    meshsets = []
    for i in range(nms):
        a = ms_p + 0x18 * i
        type_mat, nmesh, meshes_p, attrs_p, mnorm_p, vcol_p, uv_p = mem.unpack("<HHIIIII", a)
        ptype, matid = type_mat >> 14, type_mat & 0x3FFF
        polys, q = [], meshes_p
        for _ in range(nmesh):
            if ptype == 0:
                polys.append(list(mem.unpack("<3H", q)))
                q += 6
            elif ptype == 1:
                polys.append(list(mem.unpack("<4H", q)))
                q += 8
            else:                                   # 2 = N-gon, 3 = strip: count header
                hdr = mem.unpack("<H", q)[0]
                q += 2
                n = hdr & 0x7FFF
                idx = list(mem.unpack("<%dH" % n, q))
                q += 2 * n
                polys.append((idx, bool(hdr & 0x8000)))
        nidx = sum(len(p) if ptype < 2 else len(p[0]) for p in polys)
        uvs = None
        if uv_p:
            raw = mem.bytes(uv_p, 4 * nidx)
            uvs = [struct.unpack_from("<2h", raw, 4 * k) for k in range(nidx)]
        vcols = None
        if vcol_p:
            vcols = list(mem.unpack("<%dI" % nidx, vcol_p))
        meshsets.append(dict(ptype=ptype, matid=matid, polys=polys, uvs=uvs, vcols=vcols))
    return dict(points=points, normals=normals, mats=mats, meshsets=meshsets,
                center=(cx, cy, cz), radius=r)


def triangles(meshset):
    """Yields triangles as three (vertex_index, corner_index) pairs; the corner index
    counts poly-vertices through the meshset and indexes its UV / colour arrays."""
    k = 0
    pt = meshset["ptype"]
    for poly in meshset["polys"]:
        if pt == 0:
            yield (poly[0], k), (poly[1], k + 1), (poly[2], k + 2)
            k += 3
        elif pt == 1:                               # quads are stored in strip order
            c = [(poly[j], k + j) for j in range(4)]
            yield c[0], c[1], c[2]
            yield c[2], c[1], c[3]
            k += 4
        else:
            idx, rev = poly
            c = [(idx[j], k + j) for j in range(len(idx))]
            for j in range(len(c) - 2):
                a, b, d = c[j], c[j + 1], c[j + 2]
                if (j & 1) ^ rev:
                    a, b = b, a
                if a[0] != b[0] and b[0] != d[0] and a[0] != d[0]:
                    yield a, b, d
            k += len(idx)


# ---------------------------------------------------------------------------
# Matrices (column vectors, 3x4 stored as 12 floats row-major)
# ---------------------------------------------------------------------------
BAMS = 2.0 * math.pi / 65536.0
IDENT = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0]


def mmul(a, b):
    r = [0.0] * 12
    for i in range(3):
        for j in range(4):
            s = a[4 * i] * b[j] + a[4 * i + 1] * b[4 + j] + a[4 * i + 2] * b[8 + j]
            if j == 3:
                s += a[4 * i + 3]
            r[4 * i + j] = s
    return r


def rot(axis, ang):
    c, s = math.cos(ang * BAMS), math.sin(ang * BAMS)
    if axis == 0:
        return [1, 0, 0, 0, 0, c, -s, 0, 0, s, c, 0]
    if axis == 1:
        return [c, 0, s, 0, 0, 1, 0, 0, -s, 0, c, 0]
    return [c, -s, 0, 0, s, c, 0, 0, 0, 0, 1, 0]


def local_matrix(flags, pos, ang, scl):
    """Ninja object transform: T * R * S.  Default rotation order applies X first,
    then Y, then Z (M = T*Rz*Ry*Rx); NJD_EVAL_ZXY_ANG applies Z, X, then Y (T*Ry*Rx*Rz)."""
    m = [1, 0, 0, pos[0], 0, 1, 0, pos[1], 0, 0, 1, pos[2]] if not flags & EVAL_UNIT_POS else list(IDENT)
    if not flags & EVAL_UNIT_ANG:
        order = (1, 0, 2) if flags & EVAL_ZXY_ANG else (2, 1, 0)
        for ax in order:
            if ang[ax]:
                m = mmul(m, rot(ax, ang[ax]))
    if not flags & EVAL_UNIT_SCL:
        m = mmul(m, [scl[0], 0, 0, 0, 0, scl[1], 0, 0, 0, 0, scl[2], 0])
    return m


def xform(m, v, w=1.0):
    return (m[0] * v[0] + m[1] * v[1] + m[2] * v[2] + m[3] * w,
            m[4] * v[0] + m[5] * v[1] + m[6] * v[2] + m[7] * w,
            m[8] * v[0] + m[9] * v[1] + m[10] * v[2] + m[11] * w)

# ---------------------------------------------------------------------------
# Motions (NJS_ACTION / NJS_MOTION / NJS_MKEY_F / NJS_MKEY_A)
# ---------------------------------------------------------------------------
MTYPE_NAMES = {0x1: "POS", 0x2: "ANG", 0x4: "SCL", 0x8: "VEC", 0x10: "VERT", 0x20: "NORM",
               0x40: "TARGET", 0x80: "ROLL", 0x100: "ANGLE", 0x200: "RGB",
               0x400: "INTENSITY", 0x800: "SPOT", 0x1000: "POINT", 0x2000: "QUAT"}


def read_motion(mem, addr, nnodes):
    """NJS_MOTION -> dict(frames, type, inp_fn, nodes=[{type_bit: [(frame, values)]}])."""
    mdata, nframes, mtype, inp_fn = mem.unpack("<IIHH", addr)
    nfac = inp_fn & 0xF
    bits = [b for b in sorted(MTYPE_NAMES) if mtype & b]
    nodes = []
    for i in range(nnodes):
        ent = mdata + i * 8 * nfac
        ptrs = mem.unpack("<%dI" % nfac, ent)
        counts = mem.unpack("<%dI" % nfac, ent + 4 * nfac)
        keys = {}
        for b, p, n in zip(bits, ptrs, counts):
            if not p or not n:
                continue
            if b in (0x1, 0x4):
                keys[b] = [(k[0], k[1:]) for k in (mem.unpack("<I3f", p + 16 * j) for j in range(n))]
            elif b == 0x2:
                keys[b] = [(k[0], k[1:]) for k in (mem.unpack("<I3i", p + 16 * j) for j in range(n))]
            else:
                keys[b] = (p, n)                     # shape / other data: not decoded here
        nodes.append(keys)
    return dict(frames=nframes, type=mtype, inp_fn=inp_fn, nodes=nodes)


def sample(keys, frame):
    """Linear interpolation between the keys around `frame` (no spline support)."""
    if frame <= keys[0][0]:
        return keys[0][1]
    for (f0, v0), (f1, v1) in zip(keys, keys[1:]):
        if f0 <= frame <= f1:
            t = (frame - f0) / float(f1 - f0) if f1 != f0 else 0.0
            return tuple(a + (b - a) * t for a, b in zip(v0, v1))
    return keys[-1][1]


# ---------------------------------------------------------------------------
# PVM archives and PVR textures
# ---------------------------------------------------------------------------
PIXEL_FORMATS = {0: "ARGB1555", 1: "RGB565", 2: "ARGB4444"}
DATA_FORMATS = {1: "TWIDDLED", 2: "TWIDDLED_MM", 3: "VQ", 4: "VQ_MM", 9: "RECTANGLE",
                0x10: "SMALL_VQ", 0x11: "SMALL_VQ_MM"}


def parse_pvm(data):
    """Returns [(name, gbix, pvrt_chunk_bytes)] in archive order (= texlist order)."""
    if data[:4] != b"PVMH":
        raise ValueError("not a PVM archive")
    hsize = struct.unpack_from("<I", data, 4)[0]
    flags, count = struct.unpack_from("<HH", data, 8)
    esize = 2 + (28 if flags & 1 else 0) + (2 if flags & 2 else 0) + (2 if flags & 4 else 0) + (4 if flags & 8 else 0)
    entries = []
    for i in range(count):
        p = 0xC + esize * i + 2
        name = data[p:p + 28].split(b"\0")[0].decode("ascii", "replace") if flags & 1 else "%03d" % i
        p += (28 if flags & 1 else 0) + (2 if flags & 2 else 0) + (2 if flags & 4 else 0)
        gbix = struct.unpack_from("<I", data, p)[0] if flags & 8 else None
        entries.append([name, gbix])
    pos, out = 8 + hsize, []
    for name, gbix in entries:
        while data[pos:pos + 4] not in (b"PVRT", b"GBIX"):
            pos += 1
            if pos + 4 > len(data):
                raise ValueError("PVM truncated")
        if data[pos:pos + 4] == b"GBIX":
            pos += 8 + struct.unpack_from("<I", data, pos + 4)[0]
        size = struct.unpack_from("<I", data, pos + 4)[0]
        out.append((name, gbix, data[pos:pos + 8 + size]))
        pos += 8 + size
    return out


def _untwiddle_table(n):
    t = []
    for i in range(n):
        v, b = 0, 0
        while (1 << b) <= i:
            v |= ((i >> b) & 1) << (2 * b)
            b += 1
        t.append(v)
    return t


def _color(pf, v):
    if pf == 0:      # ARGB1555
        return (((v >> 10) & 31) * 255 // 31, ((v >> 5) & 31) * 255 // 31, (v & 31) * 255 // 31,
                255 if v & 0x8000 else 0)
    if pf == 1:      # RGB565
        return (((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31, 255)
    if pf == 2:      # ARGB4444
        return (((v >> 8) & 15) * 17, ((v >> 4) & 15) * 17, (v & 15) * 17, ((v >> 12) & 15) * 17)
    raise ValueError("unsupported PVR pixel format %d" % pf)


def decode_pvr(chunk):
    """Decodes the top mip level of one PVRT chunk -> (width, height, RGBA bytes)."""
    if chunk[:4] != b"PVRT":
        raise ValueError("not a PVRT chunk")
    size = struct.unpack_from("<I", chunk, 4)[0]
    pf, df = chunk[8], chunk[9]
    w, h = struct.unpack_from("<HH", chunk, 12)
    body = chunk[16:8 + size]
    if pf not in PIXEL_FORMATS or df not in DATA_FORMATS:
        raise ValueError("unsupported PVR format %d/%d" % (pf, df))
    out = bytearray(w * h * 4)

    def put(x, y, rgba):
        o = 4 * (y * w + x)
        out[o:o + 4] = bytes(rgba)

    if df in (1, 2, 9):                              # 16-bit texels
        off = 0
        if df == 2:                                  # mip chain, smallest level first
            chain = sum((1 << (2 * k)) * 2 for k in range(w.bit_length() - 1))
            off = 6 + chain if len(body) >= 6 + chain + w * h * 2 else 2 + chain
        texels = struct.unpack_from("<%dH" % (w * h), body, off)
        if df == 9:
            for y in range(h):
                for x in range(w):
                    put(x, y, _color(pf, texels[y * w + x]))
        else:
            s = min(w, h)
            tw = _untwiddle_table(s)
            for y in range(h):
                for x in range(w):
                    blk = (x // s) + (y // s)
                    put(x, y, _color(pf, texels[blk * s * s + (tw[x % s] << 1 | tw[y % s])]))
    else:                                            # VQ: 2x2 texel codebook + index map
        mip = df in (4, 0x11)
        nidx = (w // 2) * (h // 2)
        chain = (1 + sum(1 << (2 * k) for k in range((w // 2).bit_length() - 1))) if mip else 0
        if df in (3, 4):
            ncodes = 256
        else:                                        # small VQ: codebook size from the data size
            est = (len(body) - chain - nidx) // 8
            ncodes = max(c for c in (16, 32, 64, 128, 256) if c <= max(est, 16))
        book = struct.unpack_from("<%dH" % (4 * ncodes), body, 0)
        idx = body[8 * ncodes + chain: 8 * ncodes + chain + nidx]
        tw = _untwiddle_table(max(w, h) // 2)
        for by in range(h // 2):
            for bx in range(w // 2):
                code = idx[tw[bx] << 1 | tw[by]]
                if code >= ncodes:                   # defensive: out-of-range index
                    code = min(code - (256 - ncodes), ncodes - 1) if code >= 256 - ncodes else 0
                for t in range(4):
                    put(2 * bx + (t >> 1), 2 * by + (t & 1), _color(pf, book[4 * code + t]))
    return w, h, bytes(out)


def write_png(path, w, h, rgba):
    raw = b"".join(b"\0" + rgba[y * w * 4:(y + 1) * w * 4] for y in range(h))

    def chunk(tag, payload):
        c = tag + payload
        return struct.pack(">I", len(payload)) + c + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))

# ---------------------------------------------------------------------------
# Building the mesh
# ---------------------------------------------------------------------------
def build_mesh(mem, root, motion=None, frame=0.0):
    """World-space triangles of the whole object tree.  Returns
    (verts, normals, uvs, faces, node_count) with faces = [(texid, flags, [(v, n, uv) x3])]."""
    nodes = read_object_tree(mem, root)
    nodes_by_addr = {o.addr: o for o in nodes}
    verts, norms, uvs, faces = [], [], [], []

    def emit(m, mdl):
        nm = [m[0], m[1], m[2], 0, m[4], m[5], m[6], 0, m[8], m[9], m[10], 0]
        vbase, nbase = len(verts), len(norms)
        verts.extend(xform(m, p) for p in mdl["points"])
        if mdl["normals"]:
            for n in mdl["normals"]:
                x, y, z = xform(nm, n, 0.0)
                ln = math.sqrt(x * x + y * y + z * z) or 1.0
                norms.append((x / ln, y / ln, z / ln))
        for ms in mdl["meshsets"]:
            mat = mdl["mats"][ms["matid"]] if ms["matid"] < len(mdl["mats"]) else dict(texid=0, flags=0)
            textured = bool(mat["flags"] & (1 << 21))
            for tri in triangles(ms):
                corner = []
                for vi, k in tri:
                    uvi = None
                    if ms["uvs"] is not None:
                        u, v = ms["uvs"][k]
                        uvi = len(uvs)
                        uvs.append((u / 255.0, v / 255.0))
                    corner.append((vbase + vi, nbase + vi if mdl["normals"] else None, uvi))
                faces.append((mat["texid"] if textured else None, mat["flags"], corner))

    def walk(addr, parent):
        while addr:
            o = nodes_by_addr[addr]
            pos, ang, scl, flags = o.pos, o.ang, o.scl, o.flags
            if motion is not None and o.index < len(motion["nodes"]):
                k = motion["nodes"][o.index]
                if 0x1 in k:
                    pos, flags = sample(k[0x1], frame), flags & ~EVAL_UNIT_POS
                if 0x2 in k:
                    ang, flags = sample(k[0x2], frame), flags & ~EVAL_UNIT_ANG
                if 0x4 in k:
                    scl, flags = sample(k[0x4], frame), flags & ~EVAL_UNIT_SCL
            m = mmul(parent, local_matrix(flags, pos, ang, scl))
            if o.model and not flags & EVAL_HIDE:
                emit(m, read_model(mem, o.model))
            if o.child and not flags & EVAL_BREAK:
                walk(o.child, m)
            addr = o.sibling

    walk(root, IDENT)
    return verts, norms, uvs, faces, len(nodes)


def write_obj(path, name, verts, norms, uvs, faces, texnames):
    mtl = os.path.splitext(path)[0] + ".mtl"
    with open(mtl, "w") as f:
        for tid in sorted({t for t, _, _ in faces if t is not None}):
            f.write("newmtl tex%02d\nKd 1 1 1\nmap_Kd %s\n\n" % (tid, texnames.get(tid, "missing.png")))
        f.write("newmtl untextured\nKd 0.8 0.8 0.8\n")
    with open(path, "w") as f:
        f.write("# %s ripped from the user's GD-ROM by rip_character.py\nmtllib %s\no %s\n"
                % (name, os.path.basename(mtl), name))
        for v in verts:
            f.write("v %.5f %.5f %.5f\n" % v)
        for n in norms:
            f.write("vn %.5f %.5f %.5f\n" % n)
        for u, v in uvs:
            f.write("vt %.5f %.5f\n" % (u, 1.0 - v))
        cur = -2
        for tid, _, corner in sorted(faces, key=lambda fc: -1 if fc[0] is None else fc[0]):
            if tid != cur:
                f.write("usemtl %s\n" % ("untextured" if tid is None else "tex%02d" % tid))
                cur = tid
            f.write("f " + " ".join("%d/%s/%s" % (v + 1, "" if t is None else t + 1, "" if n is None else n + 1)
                                    for v, n, t in corner) + "\n")


def render_preview(path, verts, norms, uvs, faces, textures, size=512, yaw=0.0):
    """Tiny z-buffered software rasteriser (orthographic, +Y up, camera on +Z,
    model turned by `yaw` degrees about Y).  Only meant as a visual check."""
    cy_, sy_ = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
    V = [(x * cy_ + z * sy_, y, -x * sy_ + z * cy_) for x, y, z in verts]
    Nn = [(x * cy_ + z * sy_, y, -x * sy_ + z * cy_) for x, y, z in norms]
    xs = [v[0] for v in V]
    ys = [v[1] for v in V]
    cx, cy = (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2
    scale = 0.9 * size / max(max(xs) - min(xs), max(ys) - min(ys), 1e-6)
    W = H = size
    img = bytearray(b"\x40\x40\x48\xff" * (W * H))
    zb = [-1e30] * (W * H)
    light = (0.3, 0.5, 0.8)
    for tid, _, corner in faces:
        P = [V[v] for v, _, _ in corner]
        S = [((p[0] - cx) * scale + W / 2, H / 2 - (p[1] - cy) * scale, p[2]) for p in P]
        T = [uvs[t] if t is not None else (0, 0) for _, _, t in corner]
        N = [Nn[n] if n is not None else (0, 0, 1) for _, n, _ in corner]
        tex = textures.get(tid)
        x0, x1 = max(int(min(s[0] for s in S)), 0), min(int(max(s[0] for s in S)) + 1, W - 1)
        y0, y1 = max(int(min(s[1] for s in S)), 0), min(int(max(s[1] for s in S)) + 1, H - 1)
        d = (S[1][0] - S[0][0]) * (S[2][1] - S[0][1]) - (S[2][0] - S[0][0]) * (S[1][1] - S[0][1])
        if abs(d) < 1e-9:
            continue
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                px, py = x + 0.5, y + 0.5
                a = ((S[1][0] - px) * (S[2][1] - py) - (S[2][0] - px) * (S[1][1] - py)) / d
                b = ((S[2][0] - px) * (S[0][1] - py) - (S[0][0] - px) * (S[2][1] - py)) / d
                c = 1 - a - b
                if a < 0 or b < 0 or c < 0:
                    continue
                z = a * S[0][2] + b * S[1][2] + c * S[2][2]
                o = y * W + x
                if z <= zb[o]:
                    continue
                zb[o] = z
                nx = a * N[0][0] + b * N[1][0] + c * N[2][0]
                ny = a * N[0][1] + b * N[1][1] + c * N[2][1]
                nz = a * N[0][2] + b * N[1][2] + c * N[2][2]
                ln = math.sqrt(nx * nx + ny * ny + nz * nz) or 1
                sh = 0.35 + 0.65 * abs((nx * light[0] + ny * light[1] + nz * light[2]) / ln)
                if tex:
                    tw, th, px_ = tex
                    u = a * T[0][0] + b * T[1][0] + c * T[2][0]
                    v = a * T[0][1] + b * T[1][1] + c * T[2][1]
                    tx, ty = int(u * tw) % tw, int(v * th) % th
                    q = 4 * (ty * tw + tx)
                    col = px_[q:q + 3]
                else:
                    col = b"\xc8\xc8\xc8"
                img[4 * o:4 * o + 4] = bytes((min(255, int(col[0] * sh)), min(255, int(col[1] * sh)),
                                              min(255, int(col[2] * sh)), 255))
    write_png(path, W, H, bytes(img))

# ---------------------------------------------------------------------------
def load_memory(fs, info, need_motfile):
    mem = Mem()
    first = fs.open("1ST_READ.BIN")
    sha = hashlib.sha1(first).hexdigest()
    if sha != FIRST_READ_SHA1:
        raise SystemExit("1ST_READ.BIN SHA-1 %s: this is not Sonic Adventure US v1.005; "
                         "the address tables in this script only fit that version." % sha)
    mem.add("1ST_READ.BIN", FIRST_READ_BASE, first)
    if info["image"] != "1ST_READ.BIN":
        mem.add(info["image"], OVERLAYS[info["image"]], prs.decompress(fs.open("SONICADV/" + info["image"])))
    if need_motfile and info.get("motfile"):
        mem.add(info["motfile"], OVERLAYS[info["motfile"]], prs.decompress(fs.open("SONICADV/" + info["motfile"])))
    return mem


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("gdi")
    ap.add_argument("outdir")
    ap.add_argument("--char", default="sonic", choices=sorted(CHARS))
    ap.add_argument("--action", type=int)
    ap.add_argument("--frame", type=float, default=0.0)
    ap.add_argument("--all-textures", action="store_true")
    ap.add_argument("--preview", action="store_true")
    ap.add_argument("--yaw", type=float, default=0.0, help="preview rotation about Y in degrees")
    ap.add_argument("--list-actions", action="store_true")
    a = ap.parse_args(argv)
    info = CHARS[a.char]
    os.makedirs(a.outdir, exist_ok=True)

    fs = GdiFS(a.gdi)
    mem = load_memory(fs, info, need_motfile=a.action is not None or a.list_actions)
    nodes = read_object_tree(mem, info["model"])
    print("%s: root NJS_OBJECT 0x%08X, %d nodes" % (a.char, info["model"], len(nodes)))

    if a.list_actions:
        if not info["actions"]:
            print("no action list for %s" % a.char)
        else:
            base, count = info["actions"]
            for i in range(count):
                # PL_ACTION: actptr, objnum, mtnmode, next, frame (blend rate), racio (playback speed)
                act, objnum, mode, nxt, blend, speed = mem.unpack("<IBbHff", base + 16 * i)
                where = "not loaded (other overlay)"
                if mem.valid(act, 8):
                    obj, mot = mem.unpack("<II", act)
                    if mem.valid(mot, 12):
                        md, nf, mt, inp = mem.unpack("<IIHH", mot)
                        where = "%s frames=%d type=%s inp_fn=0x%X" % (
                            mem.find(mot)[2], nf, "|".join(n for b, n in sorted(MTYPE_NAMES.items()) if mt & b), inp)
                print("%3d act=0x%08X nodes=%d mode=%d next=%d blend=%.3f speed=%.3f  %s"
                      % (i, act, objnum, mode, nxt, blend, speed, where))

    motion = None
    if a.action is not None:
        base, count = info["actions"]
        act = mem.u32(base + 16 * a.action)
        if not mem.valid(act, 8):
            raise SystemExit("action %d (0x%08X) lives in an overlay that is not loaded" % (a.action, act))
        obj, mot = mem.unpack("<II", act)
        motion = read_motion(mem, mot, len(read_object_tree(mem, obj)))
        print("posing with action %d (NJS_ACTION 0x%08X, object 0x%08X, motion 0x%08X, %d frames) at frame %g"
              % (a.action, act, obj, mot, motion["frames"], a.frame))
        if obj != info["model"]:
            print("note: action uses object 0x%08X; rendering that object" % obj)
            info = dict(info, model=obj)

    verts, norms, uvs, faces, nnodes = build_mesh(mem, info["model"], motion, a.frame)
    print("geometry: %d vertices, %d triangles, %d UVs" % (len(verts), len(faces), len(uvs)))

    pvm = parse_pvm(prs.decompress(fs.open("SONICADV/%s.PRS" % info["pvm"])))
    used = sorted({t for t, _, _ in faces if t is not None})
    print("texture archive %s.PRS: %d textures; model uses ids %s" % (info["pvm"], len(pvm), used))
    texnames, textures = {}, {}
    for tid, (name, gbix, chunk) in enumerate(pvm):
        if not a.all_textures and tid not in used:
            continue
        pf, df = chunk[8], chunk[9]
        try:
            w, h, rgba = decode_pvr(chunk)
        except ValueError as e:
            print("  tex %2d %-16s %s" % (tid, name, e))
            continue
        fn = "tex_%02d_%s.png" % (tid, name)
        write_png(os.path.join(a.outdir, fn), w, h, rgba)
        texnames[tid], textures[tid] = fn, (w, h, rgba)
        print("  tex %2d %-16s gbix=%-6s %dx%d %s %s -> %s"
              % (tid, name, gbix, w, h, PIXEL_FORMATS[pf], DATA_FORMATS[df], fn))

    obj_path = os.path.join(a.outdir, a.char + ".obj")
    write_obj(obj_path, a.char, verts, norms, uvs, faces, texnames)
    print("wrote", obj_path)
    if a.preview:
        pv = os.path.join(a.outdir, a.char + "_preview.png")
        render_preview(pv, verts, norms, uvs, faces, textures, yaw=a.yaw)
        print("wrote", pv)


if __name__ == "__main__":
    main()