#!/usr/bin/env python3
"""Compile C with SHC and compare a function against the original binary.

    py -3.12 tools/shcdiff.py <func_name|0xADDR> <file.c> [--image 1ST_READ] [-v]

Compares at the machine-word level:
  * ordinary instructions must have identical 16-bit encodings;
  * PC-relative loads (MOV.W/MOV.L/MOVA) must have the same opcode/register
    and load the same *value* (symbol address or constant); local C data
    (string literals, static arrays) is compared byte-wise with the data at
    the original pointer target;
  * branches must reach the same instruction (by index within the function),
    or the same symbol when leaving the function.
Exit status 0 = match.
"""
import argparse
import difflib
import json
import os
import re
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
sys.path.insert(0, os.path.join(ROOT, "tools"))
from sh4.isa import decode, format_insn  # noqa: E402

CFG_PATH = os.path.join(ROOT, "tools", "compiler.json")
DEFAULT_CFG = {
    "version": "5.0r28",
    "cflags": "-cpu=sh4 -division=cpu -fpu=single -endian=little -optimize=1",
    "include": ["include", "src"],
}


def compiler_cfg():
    if os.path.exists(CFG_PATH):
        cfg = dict(DEFAULT_CFG)
        cfg.update(json.load(open(CFG_PATH)))
        return cfg
    return dict(DEFAULT_CFG)


# --------------------------------------------------------------------------
# running the toolchain

def run_hitachi(tool, args, ver):
    cmd = [sys.executable, os.path.join(ROOT, "tools", "hitachi.py"), "--ver", ver, tool] + args
    r = subprocess.run(cmd, capture_output=True, text=True, cwd=ROOT)
    return r.returncode, r.stdout + r.stderr


def compile_c(c_path, out_src, cfg=None, extra=""):
    cfg = cfg or compiler_cfg()
    inc = ",".join(os.path.relpath(os.path.join(ROOT, i), ROOT) for i in cfg["include"])
    flags = cfg["cflags"].split() + extra.split()
    args = [os.path.relpath(c_path, ROOT)] + flags + [f"-include={inc}", "-code=asmcode",
                                                         f"-objectfile={os.path.relpath(out_src, ROOT)}"]
    rc, out = run_hitachi("shc", args, cfg["version"])
    if rc or not os.path.exists(out_src):
        raise RuntimeError("compile failed:\n" + out)
    return out


LIST_RE = re.compile(r"^\s*(\d+)\s+([0-9A-F]{8})?\s+([0-9A-F]*)\s+(\d+)\s(.*)$")


class Listing:
    """asmsh listing of a .src file: lines, encodings, labels, sections."""

    def __init__(self, src, ver, out_stem=None):
        # out_stem: where asmsh writes <out_stem>.obj / .lis (default: next to src). Callers that may assemble one
        # .src in parallel pass a stem of their own (splice.py: one per chunk; two chunks spliced from one C file
        # raced on a shared .obj: "CANNOT OPEN FILE").
        stem = out_stem if out_stem else os.path.splitext(src)[0]
        lis = stem + ".lis"
        obj = stem + ".obj"
        rc, out = run_hitachi("asmsh", [os.path.relpath(src, ROOT), "-cpu=sh4", "-endian=little",
                                        f"-object={os.path.relpath(obj, ROOT)}",
                                        f"-list={os.path.relpath(lis, ROOT)}"], ver)
        if rc:
            raise RuntimeError("assemble failed:\n" + out)
        self.lines = []      # dicts: addr, code, text, section, label
        self.labels = {}     # name -> (section, addr)
        self.funcs = []      # function label names in order
        section = None
        for raw in open(os.path.join(ROOT, lis), encoding="cp932", errors="replace"):
            raw = raw.rstrip("\n")
            if raw.startswith("*** CROSS REFERENCE"):
                break
            m = re.match(r"^\s*(\d+)\s(.{8})\s(.{16})\s+(\d+)\s(.*)$", raw)
            if not m:
                continue
            addr = m.group(2).strip()
            code = m.group(3).strip()
            text = m.group(5)
            body = text.split(";")[0]
            st = body.strip()
            if st.upper().startswith(".SECTION"):
                section = st.split()[1].split(",")[0]
                continue
            lab = None
            mm = re.match(r"^([A-Za-z_$?.][\w$?.]*):", st)
            if mm:
                lab = mm.group(1)
                st = st[len(mm.group(0)):].strip()
                if addr:
                    self.labels[lab] = (section, int(addr, 16))
                if "; function:" in text:
                    self.funcs.append(lab)
            self.lines.append({"addr": int(addr, 16) if addr else None, "code": code,
                               "stmt": st, "section": section, "label": lab})
        # bytes of each section from the listing (relocated fields read as 0)
        self.bytes = {}
        for l in self.lines:
            if l["addr"] is None or not l["code"]:
                continue
            sec = self.bytes.setdefault(l["section"], {})
            st = l["stmt"].upper()
            code = l["code"]
            if st.startswith(".DATA.L") or st.startswith(".DATA.W") or st.startswith(".DATA.B") \
                    or st.startswith(".SDATA") or st.startswith(".RES") or st.startswith(".DATA.F"):
                # listing shows values big-endian per item; reconstruct per item size
                if st.startswith(".DATA.L") or st.startswith(".DATA.F"):
                    size = 4
                elif st.startswith(".DATA.W"):
                    size = 2
                else:
                    size = 1
                a = l["addr"]
                for k in range(0, len(code), size * 2):
                    v = int(code[k:k + size * 2], 16)
                    for b in range(size):
                        sec[a + b] = (v >> (8 * b)) & 0xFF
                    a += size
            else:
                v = int(code, 16)
                sec[l["addr"]] = v & 0xFF
                sec[l["addr"] + 1] = v >> 8

    def data_items(self, section):
        """[(addr, size, operand_text)] for every .DATA item in a section."""
        out = []
        for l in self.lines:
            if l["section"] != section or l["addr"] is None:
                continue
            st = l["stmt"]
            mm = re.match(r"^\.DATA\.([LWB])\s+(.*)$", st, re.I)
            if not mm:
                continue
            size = {"L": 4, "W": 2, "B": 1}[mm.group(1).upper()]
            a = l["addr"]
            for op in split_operands(mm.group(2)):
                out.append((a, size, op.strip()))
                a += size
        return out

    def function_insns(self, name):
        """[(addr, word, stmt)] for function `name` (asm label without _ or with)."""
        lab = name if name in self.labels else "_" + name
        if lab not in self.labels:
            raise KeyError(f"function {name} not in compiler output")
        out = []
        on = False
        for l in self.lines:
            if l["label"] == lab:
                on = True
            elif on and l["label"] in self.funcs:
                break
            if not on or l["section"] != self.labels[lab][0]:
                continue
            st = l["stmt"]
            if not st or st.startswith("."):
                continue
            if l["code"] and len(l["code"]) == 4:
                out.append((l["addr"], int(l["code"], 16), st))
        return out


def split_operands(s):
    out, cur, q = [], "", False
    for ch in s:
        if ch == '"':
            q = not q
        if ch == "," and not q:
            out.append(cur)
            cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur)
    return out


# --------------------------------------------------------------------------
# original side

class Original:
    def __init__(self, image):
        self.image = image
        man = json.load(open(os.path.join(ROOT, "build", f"{image}.manifest.json")))
        self.man = man
        self.base = man["base"]
        self.data = open(os.path.join(ROOT, man.get("target") or f"baserom/{image}.BIN"), "rb").read()
        self.sym2addr = {}
        self.addr2sym = {}
        for line in open(os.path.join(ROOT, "build", f"{image}.symbols.txt")):
            n, v = line.split("=")
            n, v = n.strip(), int(v.strip().rstrip(";"), 16)
            self.sym2addr[n] = v
            self.addr2sym.setdefault(v, n)
        for extra in ("1ST_READ",):
            if extra != image:
                p = os.path.join(ROOT, "build", f"{extra}.symbols.txt")
                if os.path.exists(p):
                    for line in open(p):
                        n, v = line.split("=")
                        self.sym2addr.setdefault(n.strip(), int(v.strip().rstrip(";"), 16))
        self.funcs = []
        self.chunk_of = {}
        for ch in man["chunks"]:
            for a, n in ch["functions"]:
                self.funcs.append((a, n))
                self.chunk_of[a] = ch
        self.funcs.sort()

    def u16(self, a):
        return struct.unpack_from("<H", self.data, a - self.base)[0]

    def u32(self, a):
        return struct.unpack_from("<I", self.data, a - self.base)[0]

    def find(self, key):
        if key.lower().startswith("0x"):
            a = int(key, 16)
        else:
            a = self.sym2addr.get(key)
            if a is None:
                raise KeyError(f"unknown function {key}")
        return a

    def function_insns(self, addr):
        ch = self.chunk_of[addr]
        insn_addrs = []
        src = os.path.join(ROOT, ch["src"])
        nxt = None
        for a, n in ch["functions"]:
            if a > addr:
                nxt = a
                break
        hi = nxt or ch["end"]
        for line in open(src):
            m = re.search(r";\s([0-9A-F]{8})\s([0-9A-F]{4})\s*$", line)
            if m:
                a = int(m.group(1), 16)
                if addr <= a < hi:
                    insn_addrs.append(a)
        return [(a, self.u16(a)) for a in insn_addrs]


# --------------------------------------------------------------------------

def masked(ins):
    """Encoding with displacement fields cleared for pc-relative / branches."""
    w = ins.word
    if ins.pcrel or ins.mnem in ("BT", "BF", "BT/S", "BF/S"):
        return w & 0xFF00
    if ins.mnem in ("BRA", "BSR"):
        return w & 0xF000
    return w


def compare(orig, lst, func_key, c_text_cache=None, verbose=False):
    a0 = orig.find(func_key)
    oi = orig.function_insns(a0)
    name = orig.addr2sym.get(a0, func_key)
    mi = lst.function_insns(name)
    o_dec = [decode(w, a) for a, w in oi]
    m_dec = [decode(w, a) for a, w, _ in mi]
    o_idx = {a: k for k, (a, w) in enumerate(oi)}
    m_idx = {a: k for k, (a, w, s) in enumerate(mi)}
    p_items = {}
    for sec in set(l["section"] for l in lst.lines):
        for a, size, op in lst.data_items(sec):
            p_items[(sec, a)] = (size, op)
    psec = lst.labels.get("_" + name, lst.labels.get(name))[0]

    def m_value(target, size):
        it = p_items.get((psec, target))
        if it is None:
            return ("?", None)
        isz, op = it
        mask = (1 << (8 * isz)) - 1
        # general expression: [+-] term {[+-] term}, term = H'hex | decimal | symbol
        terms = re.findall(r"([+-]?)\s*(H'[0-9A-Fa-f]+|\d+|[A-Za-z_$?.][\w$?.]*)", op)
        if not terms or re.sub(r"[\s+-]|H'[0-9A-Fa-f]+|\d+|[A-Za-z_$?.][\w$?.]*", "", op):
            return ("expr", op)
        total, local = 0, None
        for sign, t in terms:
            k = -1 if sign == "-" else 1
            if t.upper().startswith("H'"):
                total += k * int(t[2:], 16)
            elif t.isdigit():
                total += k * int(t)
            else:
                is_local = t in lst.labels and (not t.startswith("_") or lst.labels[t][0] != psec)
                if is_local:
                    if local is not None or k < 0:
                        return ("expr", op)
                    local = t
                    continue
                n = t[1:] if t.startswith("_") else t
                if n not in orig.sym2addr:
                    return ("sym", op)
                total += k * orig.sym2addr[n]
        if local is not None:
            return ("local", (local, total))
        return ("num", total & mask)

    def local_matches(sym, off, oaddr):
        sec, la = lst.labels[sym]
        bts = lst.bytes.get(sec, {})
        # compare up to the next label in the same section or 256 bytes
        nxt = min([a for s, (ss, a) in lst.labels.items() if ss == sec and a > la] + [la + 256])
        n = 0
        for k in range(la, nxt):
            if k not in bts:
                break
            ob = oaddr - off + (k - la)
            if not (orig.base <= ob < orig.base + len(orig.data)):
                return False
            if orig.data[ob - orig.base] != bts[k]:
                return False
            n += 1
        return n > 0

    rows = []
    ok = len(oi) == len(mi)
    n = max(len(oi), len(mi))
    mism = 0
    for k in range(n):
        o = o_dec[k] if k < len(oi) else None
        m = m_dec[k] if k < len(mi) else None
        good = o is not None and m is not None and masked(o) == masked(m)
        note = ""
        if good and o.pcrel:
            size = o.pcrel[1] or 4
            ov = orig.u32(o.pcrel[2]) if size == 4 else orig.u16(o.pcrel[2])
            kind, mv = m_value(m.pcrel[2], size)
            if kind == "num":
                good = (mv & ((1 << (8 * size)) - 1)) == ov
                note = f"={ov:08X}" if good else f"orig={ov:08X} mine={mv:08X}"
            elif kind == "local":
                good = local_matches(mv[0], mv[1], ov)
                note = f"{mv[0]}@{ov:08X}" + ("" if good else " (data differs)")
            else:
                good = False
                note = f"orig={ov:08X} mine={kind}:{mv}"
        elif good and o.branch_target is not None:
            ot, mt = o.branch_target, m.branch_target
            if ot in o_idx and mt in m_idx:
                good = o_idx[ot] == m_idx[mt]
                if not good:
                    note = f"target #{o_idx[ot]} vs #{m_idx[mt]}"
            else:
                good = (ot not in o_idx) and (mt not in m_idx)
                if good:
                    # resolve the compiled target through the listing's labels
                    tgt_names = [n for n, (sec, la) in lst.labels.items() if sec == psec and la == mt]
                    addrs = [orig.sym2addr.get(n[1:] if n.startswith("_") else n) for n in tgt_names]
                    addrs = [x for x in addrs if x is not None]
                    good = ot in addrs
                    note = f"-> {orig.addr2sym.get(ot, hex(ot))}" + ("" if good else f" vs {tgt_names}")
        if not good:
            mism += 1
        os_ = format_insn(o, lambda t: orig.addr2sym.get(t)) if o else ""
        ms_ = mi[k][2] if m is not None else ""
        rows.append((good, oi[k][0] if o else None, os_, ms_, note))
    match = ok and mism == 0
    return match, rows, len(oi), len(mi)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("func")
    ap.add_argument("cfile")
    ap.add_argument("--image", default="1ST_READ")
    ap.add_argument("--ver")
    ap.add_argument("--flags", default="")
    ap.add_argument("-v", "--verbose", action="store_true")
    a = ap.parse_args()
    cfg = compiler_cfg()
    if a.ver:
        cfg["version"] = a.ver
    orig = Original(a.image)
    tmpd = os.path.join(ROOT, "build", "diff")
    os.makedirs(tmpd, exist_ok=True)
    stem = os.path.splitext(os.path.basename(a.cfile))[0]
    out_src = os.path.join(tmpd, stem + ".src")
    try:
        compile_c(os.path.abspath(a.cfile), out_src, cfg, a.flags)
        lst = Listing(out_src, cfg["version"])
    except RuntimeError as e:
        print(e)
        sys.exit(2)
    match, rows, no, nm = compare(orig, lst, a.func)
    for good, addr, os_, ms_, note in rows:
        if a.verbose or not good:
            flag = "  " if good else "XX"
            ad = f"{addr:08X}" if addr else "        "
            print(f"{flag} {ad} {os_:<34} | {ms_:<34} {note}")
    print(f"{a.func}: {'MATCH' if match else 'DIFF'} (orig {no} insns, mine {nm}, "
          f"{sum(1 for r in rows if not r[0])} differing)")
    sys.exit(0 if match else 1)


if __name__ == "__main__":
    main()
