#!/usr/bin/env python3
"""binsyms.py - just enough COFF / ELF / PE parsing for LIB/build.py's link test and size report.

    objfile(path)   -> Obj: .defined (global symbol names), .undefined, .sections [(name, size, kind)]
                       for a COFF object (x86 / x86_64, regular or bigobj) or an ELF relocatable (32/64-bit LE)
    exports(path)   -> names a DLL (PE export table) or a shared object (.dynsym, defined, default visibility)
                       makes visible
    binsections(path) -> [(name, size, kind)] of a linked PE / ELF binary
kind is "text", "rodata", "data", "bss", "other" (allocated) or "debug" (not loaded: DWARF, .drectve, notes ...).
i386 COFF names lose their leading underscore, so every format reports C names.
"""
import struct
import sys


class Obj:
    def __init__(self):
        self.defined = set()
        self.undefined = set()
        self.weak = set()
        self.sections = []


def _kind(name, alloc, code, nobits):
    if not alloc:
        return "debug"
    n = name.lower()
    if nobits or n.startswith(".bss") or n.startswith(".tbss"):
        return "bss"
    if code or n.startswith(".text") or n.startswith("sa1g_text"):
        return "text"
    if n.startswith((".rdata", ".rodata", ".srodata")):
        return "rodata"
    if n.startswith((".data", ".tdata", ".sdata")):
        return "data"
    return "other"


def _cstr(b, off):
    end = b.find(b"\0", off)
    return b[off:end if end >= 0 else len(b)].decode("latin-1")


# ---- COFF objects -------------------------------------------------------------------------------------------
def _coff(b):
    o = Obj()
    sig1, sig2 = struct.unpack_from("<HH", b, 0)
    if sig1 == 0 and sig2 == 0xFFFF:                       # bigobj
        machine = struct.unpack_from("<H", b, 6)[0]
        nsec, symptr, nsym = struct.unpack_from("<III", b, 44)
        sec_off, symsize, secfmt = 56, 20, "<i"
    else:
        machine, nsec = struct.unpack_from("<HH", b, 0)
        symptr, nsym = struct.unpack_from("<II", b, 8)
        opt = struct.unpack_from("<H", b, 16)[0]
        sec_off, symsize, secfmt = 20 + opt, 18, "<h"
    strtab = symptr + nsym * symsize
    under = machine == 0x14C

    def name8(raw):
        if raw[:4] == b"\0\0\0\0":
            return _cstr(b, strtab + struct.unpack_from("<I", raw, 4)[0])
        return raw.rstrip(b"\0").decode("latin-1")

    for i in range(nsec):
        off = sec_off + 40 * i
        raw = b[off:off + 8]
        name = raw.rstrip(b"\0").decode("latin-1")
        if name.startswith("/"):
            try:
                name = _cstr(b, strtab + int(name[1:]))
            except ValueError:
                pass
        size = struct.unpack_from("<I", b, off + 16)[0]
        ch = struct.unpack_from("<I", b, off + 36)[0]
        alloc = not (ch & 0x02000000 or ch & 0x800 or ch & 0x200)     # discardable / LNK_REMOVE / LNK_INFO
        o.sections.append((name, size, _kind(name, alloc, ch & 0x20, ch & 0x80)))
    i = 0
    while i < nsym:
        off = symptr + i * symsize
        raw = b[off:off + 8]
        value = struct.unpack_from("<I", b, off + 8)[0]
        secno = struct.unpack_from(secfmt, b, off + 12)[0]
        sclass, naux = b[off + symsize - 2], b[off + symsize - 1]
        name = name8(raw)
        if under and name.startswith("_"):
            name = name[1:]
        if sclass == 2:                                    # EXTERNAL
            if secno == 0 and value == 0:
                o.undefined.add(name)
            elif secno != 0 or value != 0:
                o.defined.add(name)
        elif sclass == 105:                                # WEAK_EXTERNAL
            o.weak.add(name)
            o.defined.add(name)
        i += 1 + naux
    o.undefined -= o.defined
    return o


# ---- ELF ------------------------------------------------------------------------------------------------------
def _elf_hdr(b):
    cls = b[4]
    if b[5] != 1:
        raise ValueError("big-endian ELF")
    if cls == 2:
        shoff = struct.unpack_from("<Q", b, 0x28)[0]
        shentsize, shnum, shstrndx = struct.unpack_from("<HHH", b, 0x3A)
    else:
        shoff = struct.unpack_from("<I", b, 0x20)[0]
        shentsize, shnum, shstrndx = struct.unpack_from("<HHH", b, 0x2E)
    secs = []
    for i in range(shnum):
        off = shoff + i * shentsize
        if cls == 2:
            nm, typ, flags, addr, soff, size, link, info, align, entsize = struct.unpack_from("<IIQQQQIIQQ", b, off)
        else:
            nm, typ, flags, addr, soff, size, link, info, align, entsize = struct.unpack_from("<IIIIIIIIII", b, off)
        secs.append({"name": nm, "type": typ, "flags": flags, "off": soff, "size": size, "link": link, "entsize": entsize})
    if shstrndx < len(secs):
        base = secs[shstrndx]["off"]
        for s in secs:
            s["name"] = _cstr(b, base + s["name"])
    return cls, secs


def _elf_syms(b, cls, secs, typ):
    out = []
    for s in secs:
        if s["type"] != typ:
            continue
        strbase = secs[s["link"]]["off"]
        ent = s["entsize"] or (24 if cls == 2 else 16)
        for k in range(1, s["size"] // ent):
            off = s["off"] + k * ent
            if cls == 2:
                nm, info, other, shndx, value, size = struct.unpack_from("<IBBHQQ", b, off)
            else:
                nm, value, size, info, other, shndx = struct.unpack_from("<IIIBBH", b, off)
            out.append((_cstr(b, strbase + nm), info >> 4, other & 3, shndx))
    return out


def _elf_obj(b):
    o = Obj()
    cls, secs = _elf_hdr(b)
    for s in secs:
        if s["type"] == 0:
            continue
        alloc = bool(s["flags"] & 2)
        o.sections.append((s["name"], s["size"], _kind(s["name"], alloc, s["flags"] & 4, s["type"] == 8)))
    for name, bind, vis, shndx in _elf_syms(b, cls, secs, 2):
        if bind not in (1, 2) or not name:
            continue
        if shndx == 0:
            o.undefined.add(name)
        else:
            o.defined.add(name)
            if bind == 2:
                o.weak.add(name)
    o.undefined -= o.defined
    return o


def objfile(path):
    b = open(path, "rb").read()
    if b[:4] == b"\x7fELF":
        return _elf_obj(b)
    return _coff(b)


# ---- linked binaries -----------------------------------------------------------------------------------------
def _pe(b):
    pe = struct.unpack_from("<I", b, 0x3C)[0]
    if b[pe:pe + 4] != b"PE\0\0":
        raise ValueError("not a PE file")
    coff = pe + 4
    nsec = struct.unpack_from("<H", b, coff + 2)[0]
    optsize = struct.unpack_from("<H", b, coff + 16)[0]
    opt = coff + 20
    magic = struct.unpack_from("<H", b, opt)[0]
    dd = opt + (112 if magic == 0x20B else 96)
    secs = []
    for i in range(nsec):
        off = opt + optsize + 40 * i
        name = b[off:off + 8].rstrip(b"\0").decode("latin-1")
        vsize, va, rawsize, rawptr = struct.unpack_from("<IIII", b, off + 8)
        ch = struct.unpack_from("<I", b, off + 36)[0]
        secs.append((name, vsize, va, rawsize, rawptr, ch))
    return dd, secs


def _rva(secs, rva):
    for name, vsize, va, rawsize, rawptr, ch in secs:
        if va <= rva < va + max(vsize, rawsize):
            return rawptr + rva - va
    raise ValueError("rva %08X outside the sections" % rva)


def exports(path):
    b = open(path, "rb").read()
    if b[:4] == b"\x7fELF":
        cls, secs = _elf_hdr(b)
        return sorted(n for n, bind, vis, shndx in _elf_syms(b, cls, secs, 11)
                      if n and shndx != 0 and bind in (1, 2) and vis in (0, 3))
    dd, secs = _pe(b)
    rva, size = struct.unpack_from("<II", b, dd)
    if not rva:
        return []
    e = _rva(secs, rva)
    nnames, _, names = struct.unpack_from("<III", b, e + 24)
    return sorted(_cstr(b, _rva(secs, struct.unpack_from("<I", b, _rva(secs, names) + 4 * i)[0])) for i in range(nnames))


def binsections(path):
    b = open(path, "rb").read()
    out = []
    if b[:4] == b"\x7fELF":
        cls, secs = _elf_hdr(b)
        for s in secs:
            if s["type"] == 0:
                continue
            out.append((s["name"], s["size"], _kind(s["name"], bool(s["flags"] & 2), s["flags"] & 4, s["type"] == 8)))
        return out
    dd, secs = _pe(b)
    for name, vsize, va, rawsize, rawptr, ch in secs:
        alloc = not (ch & 0x02000000)
        kind = _kind(name, alloc, ch & 0x20, ch & 0x80 and not rawsize)
        if name.startswith(".debug") or name.startswith("/"):
            kind = "debug"
        out.append((name, max(vsize, rawsize) if kind != "debug" else rawsize, kind))
    return out


if __name__ == "__main__":
    for p in sys.argv[1:]:
        if p.endswith((".dll", ".so", ".exe")):
            print(p, "exports:", " ".join(exports(p))[:2000])
            print(p, "sections:", binsections(p))
        else:
            o = objfile(p)
            print(p, "defined", len(o.defined), "undefined", sorted(o.undefined)[:50], o.sections[:20])
