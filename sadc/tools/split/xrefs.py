"""Per-function cross references for a CodeAnalysis (written by split.py).

For every function: callees (BSR/BRA-to-function targets and code pointers
loaded from the literal pool that reach JSR/JMP), pool constants (with the
symbol they resolve to, floats decoded), and referenced strings. Callers are
the inverse of callees.
"""
import struct


def _float(v):
    return struct.unpack("<f", struct.pack("<I", v))[0]


def _cstring(img, a, limit=80):
    if not (img.base <= a < img.end):
        return None
    out = bytearray()
    o = a - img.base
    while o < len(img.data) and len(out) < limit:
        b = img.data[o]
        if b == 0:
            break
        if b < 0x20 and b not in (9, 10, 13):
            return None
        out.append(b)
        o += 1
    if len(out) < 2:
        return None
    try:
        return out.decode("ascii")
    except UnicodeDecodeError:
        try:
            return out.decode("cp932")
        except UnicodeDecodeError:
            return None


def build_xrefs(img, ca, lab):
    funcs = sorted(ca.funcs)
    by_owner = {}
    for a, f in ca.owner.items():
        by_owner.setdefault(f, []).append(a)
    out = {}
    for f in funcs:
        callees, consts, strings = set(), [], []
        for a in sorted(by_owner.get(f, [])):
            ins = ca.insn[a]
            t = ins.branch_target
            if t is not None and t in ca.funcs and t != f:
                callees.add(t)
            pc = ins.pcrel
            if pc and pc[1] == 4 and img.has(pc[2], 4):
                v = img.u32(pc[2])
                ent = {"pool": pc[2], "value": v}
                if v in ca.funcs:
                    callees.add(v)
                    ent["sym"] = lab.glob.get(v, "")[1:]
                elif v in lab.glob:
                    ent["sym"] = lab.glob[v][1:]
                    s = _cstring(img, v)
                    if s:
                        strings.append({"addr": v, "text": s})
                elif (v >> 23) in (0x7F, 0x7E, 0x80, 0x81) or 0x3A000000 <= v <= 0x4A000000 or \
                        0xBA000000 <= v <= 0xCA000000:
                    ent["float"] = round(_float(v), 6)
                consts.append(ent)
            elif pc and pc[1] == 2 and img.has(pc[2], 2):
                consts.append({"pool": pc[2], "value": img.s16(pc[2]), "size": 2})
        out[f] = {"name": lab.glob[f][1:], "callees": sorted(callees), "consts": consts,
                  "strings": strings, "size": ca.funcs[f]["end"] - f}
    callers = {}
    for f, x in out.items():
        for c in x["callees"]:
            callers.setdefault(c, set()).add(f)
    for f, x in out.items():
        x["callers"] = sorted(callers.get(f, ()))
    return out
