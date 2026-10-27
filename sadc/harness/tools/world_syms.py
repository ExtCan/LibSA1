#!/usr/bin/env python3
"""Symbol list for `sa1h trace --dump-world` (harness/src/t_sim.c) and the write
census: every data symbol of 1ST_READ with its size and a pointer class.

    py -3.12 harness/tools/world_syms.py [--out build/verify/world_syms.txt]

Output lines: ADDR SIZE CLASS REF NAME (hex address and size)
  SIZE   distance to the next symbol of build/1ST_READ.symbols.txt
  CLASS  v = no pointers, p = pointer (or array of pointers, function pointer),
         m = struct/union that contains pointers, u = unknown type / not declared
  REF    1 = declared by the decompiled C (src/1st_read/*.c or include/**.h) or in the data list of
         a harness build's native manifest (gen_*/native_registry.c sh4_native_datas), else 0
The class comes from the C declarations (extern ... NAME[...];) and the typedefs
of the same files, resolved recursively (a heuristic C reader, not a compiler:
several declarations of one symbol combine to the most pointer-like class);
storage declared as raw bytes that holds pointers is overridden (the game heap
B_8C7EE8E0, the NJS_OBJECT pool B_8C75D310).
`--dump-world` takes REF == 1 and CLASS v or u by default (pointer values differ
between the harness and libsa1 heaps); --world-all takes everything.  The
generated symbol table of pclower (sa1g_symtab.h, design 2.4) will replace this.
"""
import argparse
import glob
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
HARNESS = os.path.dirname(HERE)
ROOT = os.path.dirname(HARNESS)

BASE_V = {"Sint8", "Uint8", "Sint16", "Uint16", "Sint32", "Uint32", "Float", "Float32", "Double", "Angle", "Bool",
          "Int", "Uint", "Sint", "int", "char", "short", "long", "float", "double", "unsigned", "signed", "Void",
          "int8_t", "uint8_t", "int16_t", "uint16_t", "int32_t", "uint32_t", "Bool8", "Sint64", "Uint64",
          "SINT8", "UINT8", "SINT16", "UINT16", "SINT32", "UINT32", "FLOAT", "ANGLE"}
QUALS = {"const", "volatile", "struct", "union", "enum", "static", "extern", "register", "unsigned", "signed"}
RANK = {"u": 0, "v": 1, "m": 2, "p": 3}
# state the game uses only through runtime code (no C declaration): counted as declared
EXTRA = {"D_8C5FBB08": "v"}     # rand() state (rt_misc.c)
# storage the C declares as raw bytes but that holds pointer-carrying structs
OVERRIDE = {"B_8C75D310": "m"}  # NJS_OBJECT pool of the landtable manager (256 x 0x34, design 5.2)
HEAP_LO, HEAP_HI = 0x8C7EE8E0, 0x8C87E8F0   # game heap (tasks, task work): always m


def strip_comments(s):
    s = re.sub(r"/\*.*?\*/", " ", s, flags=re.S)
    return re.sub(r"//[^\n]*", " ", s)


def base_name(typepart):
    ids = [t for t in re.findall(r"[A-Za-z_]\w*", typepart) if t not in QUALS]
    if ids:
        return ids[-1]
    return "int" if re.search(r"\b(unsigned|signed)\b", typepart) else None


class Types:
    def __init__(self):
        self.ptr = set()            # pointer typedefs (incl. function pointer types)
        self.alias = {}             # typedef name -> base type name
        self.bodies = {}            # struct/union typedef name -> list of (member type part, is pointer)
        self.cache = {}

    def scan(self, text):
        # struct / union typedefs: typedef struct tag { ... } NAME, *PNAME;
        for m in re.finditer(r"typedef\s+(?:struct|union)\s*\w*\s*\{", text):
            depth, i = 1, m.end()
            while i < len(text) and depth:
                depth += {"{": 1, "}": -1}.get(text[i], 0)
                i += 1
            body = text[m.end():i - 1]
            tail = re.match(r"\s*([^;]*);", text[i:])
            if not tail:
                continue
            members = []
            for decl in re.split(r";", body):
                decl = decl.strip()
                if not decl:
                    continue
                if "{" in decl:          # nested anonymous struct/union: look at its members as well
                    inner = decl[decl.index("{") + 1:]
                    for d2 in inner.split(";"):
                        d2 = d2.strip().lstrip("}").strip()
                        if d2:
                            members.append(d2)
                    continue
                members.append(decl.lstrip("}").strip())
            for name in tail.group(1).split(","):
                name = name.strip()
                if not name:
                    continue
                n = re.sub(r"[\s*]|\[.*?\]", "", name)
                if "*" in name:
                    self.ptr.add(n)
                else:
                    self.bodies[n] = members
            tag = re.match(r"typedef\s+(?:struct|union)\s*(\w*)", m.group(0)).group(1)
            if tag and tail.group(1).split(","):
                first = re.sub(r"[\s*]|\[.*?\]", "", tail.group(1).split(",")[0])
                if first and first in self.bodies:
                    self.bodies.setdefault(tag, self.bodies[first])
        # other typedefs
        for m in re.finditer(r"typedef\s+([^;{}]*?);", text):
            d = m.group(1)
            fp = re.search(r"\(\s*\*\s*(\w+)\s*\)\s*\(", d)
            if fp:
                self.ptr.add(fp.group(1))
                continue
            mm = re.match(r"(.*?)(\**)\s*(\w+)\s*((?:\[[^\]]*\])*)\s*$", d, re.S)
            if not mm:
                continue
            tp, stars, name = mm.group(1), mm.group(2), mm.group(3)
            if re.match(r"\s*(struct|union)\b", tp) and "{" not in tp:
                b = base_name(tp)
                if stars:
                    self.ptr.add(name)
                elif b and b != name:
                    self.alias[name] = b
                continue
            if stars or "*" in tp:
                self.ptr.add(name)
            else:
                b = base_name(tp)
                if b and b != name:
                    self.alias[name] = b

    def cls(self, name, depth=0):
        if name is None:
            return "u"
        if name in BASE_V:
            return "v"
        if name in self.ptr:
            return "p"
        if name in self.cache:
            return self.cache[name]
        if depth > 30:
            return "u"
        self.cache[name] = "u"         # recursion guard
        if name in self.alias:
            c = self.cls(self.alias[name], depth + 1)
        elif name in self.bodies:
            c = "v"
            for decl in self.bodies[name]:
                if re.search(r"\(\s*\*", decl) or "*" in decl:
                    c = "m"
                    break
                sub = self.cls(base_name(re.sub(r"\[.*?\]", "", decl.rsplit(None, 1)[0] if " " in decl else decl)),
                               depth + 1)
                if sub in ("p", "m"):
                    c = "m"
                    break
                if sub == "u":
                    c = "u" if c == "v" else c
        else:
            c = "u"
        self.cache[name] = c
        return c


EXTERN = re.compile(r"\bextern\s+([^;{}()]*?)\b([A-Za-z_]\w*)\s*((?:\[[^\]]*\]\s*)*);")
EXTERN_FP = re.compile(r"\bextern\s+[^;{}]*?\(\s*\*\s*([A-Za-z_]\w*)\s*\)\s*(?:\[[^\]]*\]\s*)*\(")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "verify", "world_syms.txt"))
    a = ap.parse_args()
    syms = []
    for line in open(os.path.join(ROOT, "build", "1ST_READ.symbols.txt")):
        m = re.match(r"\s*([A-Za-z0-9_$.]+)\s*=\s*0x([0-9A-Fa-f]+)", line)
        if m:
            syms.append((int(m.group(2), 16), m.group(1)))
    syms.sort()
    funcs = {int(k, 16) for k in json.load(open(os.path.join(ROOT, "build", "1ST_READ.xrefs.json")))}
    files = sorted(glob.glob(os.path.join(ROOT, "include", "**", "*.h"), recursive=True))
    files += sorted(glob.glob(os.path.join(ROOT, "src", "1st_read", "*.c")))
    texts = [strip_comments(open(f, encoding="utf-8", errors="replace").read()) for f in files]
    T = Types()
    for t in texts:
        T.scan(t)
    decl = {}
    for t in texts:
        for m in EXTERN.finditer(t):
            tp, name, arr = m.group(1), m.group(2), m.group(3)
            if tp.strip() in ("", "C") or name in QUALS:
                continue
            c = "p" if "*" in tp else T.cls(base_name(tp))
            if RANK[c] >= RANK.get(decl.get(name, "u"), 0):
                decl[name] = c
            else:
                decl.setdefault(name, c)
        for m in EXTERN_FP.finditer(t):
            decl[m.group(1)] = "p"
    for name, c in EXTRA.items():
        decl.setdefault(name, c)
    # the data list of the native manifest (gen_native's sh4_native_datas: the data symbols the
    # natively compiled C references): the largest one among the harness builds
    manifest, mpath = set(), None
    for rp in glob.glob(os.path.join(HARNESS, "build", "gen_*", "native_registry.c")):
        txt = open(rp, encoding="utf-8", errors="replace").read()
        if "sh4_native_datas[] = {" not in txt:
            continue
        body = txt[txt.index("sh4_native_datas[] = {"):]
        names = set(re.findall(r'\{ "(\w+)", 0x[0-9A-F]{8}u, \d+u, "[^"]*" \}', body))
        if len(names) > len(manifest):
            manifest, mpath = names, rp
    if mpath:
        print("native manifest data list: %d symbols (%s)" % (len(manifest), os.path.relpath(mpath, ROOT)))
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    nexts, nxt = [0] * len(syms), 0x8C900000
    for k in range(len(syms) - 1, -1, -1):      # next higher address of any symbol
        nexts[k] = nxt
        if k and syms[k - 1][0] < syms[k][0]:
            nxt = syms[k][0]
    counts = {}
    with open(a.out, "w", encoding="utf-8", newline="\n") as f:
        f.write("# data symbols of 1ST_READ (harness/tools/world_syms.py): ADDR SIZE CLASS REF NAME\n")
        f.write("# CLASS v no pointers, p pointer, m struct with pointers, u unknown; REF 1 = declared by the C\n")
        for k, (ad, name) in enumerate(syms):
            if ad in funcs or name.startswith(("func_", "lbl_")) or not 0x8C010000 <= ad < 0x8C900000:
                continue
            nxt = min(nexts[k], 0x8C900000)
            c = OVERRIDE.get(name, decl.get(name, "u"))
            if ad < HEAP_HI and nxt > HEAP_LO:
                c = "m"
            ref = 1 if name in decl or name in manifest else 0
            counts[(c, ref)] = counts.get((c, ref), 0) + 1
            f.write("%08X %X %s %d %s\n" % (ad, nxt - ad, c, ref, name))
    print("%d data symbols -> %s; declared by the C: %s; undeclared: %d" % (
        sum(counts.values()), a.out, ", ".join("%s %d" % (c, n) for (c, r), n in sorted(counts.items()) if r),
        sum(n for (c, r), n in counts.items() if not r)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
