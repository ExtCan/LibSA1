"""Emit Hitachi asmsh (.src) files from a CodeAnalysis.

Output conventions (mirroring SHC's own assembly output):
  _func_8C0xxxxx / known names   function entries (exported, C-visible)
  _D_8Cxxxxxx                    labelled data (exported)
  _B_8Cxxxxxx                    BSS objects (exported)
  _lbl_8Cxxxxxx                  code addresses referenced as data (exported)
  L8Cxxxxxx                      file-local branch / literal-pool labels
"""
import os
import struct

from sh4.isa import format_insn

COL = " " * 10


def is_printable(b):
    return 0x20 <= b < 0x7F and b != 0x22


class Labeler:
    """Global label table for one image."""

    def __init__(self, img, ca, names, data_ranges, extern_names=None):
        self.img, self.ca = img, ca
        self.names = dict(names)          # addr -> symbol (without leading _)
        self.extern = extern_names or {}  # addr -> symbol in other images
        self.glob = {}                    # addr -> exported asm symbol
        self.local = set(ca.code_labels)  # addr -> L%08X
        self.data_ranges = data_ranges
        for f in ca.funcs:
            self.glob[f] = "_" + self.names.get(f, f"func_{f:08X}")
        for a, n in self.names.items():
            if a not in self.glob:
                self.glob[a] = "_" + n

    def in_image(self, v):
        return self.img.base <= v < self.img.bss_end

    def pointer_label(self, v):
        """Return a symbol for pointer value v, creating a label if needed,
        or None if v must stay a raw number."""
        if v in self.glob:
            return self.glob[v]
        if v in self.extern:
            return "_" + self.extern[v]
        if not self.in_image(v):
            return None
        ca = self.ca
        if ca.in_code(v):
            if v & 1:
                return None
            if v in ca.insn or v in ca.data or v in ca.jt_entry:
                name = "_" + self.names.get(v, f"lbl_{v:08X}")
            elif (v - 2) in ca.data and ca.data[v - 2] == "L":
                name = "_" + self.names.get(v, f"lbl_{v:08X}")
            else:
                name = "_" + self.names.get(v, f"D_{v:08X}")
        elif v < self.img.end:
            name = "_" + self.names.get(v, f"D_{v:08X}")
        else:
            name = "_" + self.names.get(v, f"B_{v:08X}")
        self.glob[v] = name
        return name

    def label_for_insn(self, tgt):
        if tgt in self.glob:
            return self.glob[tgt]
        return f"L{tgt:08X}"


class Emitter:
    def __init__(self, img, ca, labeler):
        self.img, self.ca, self.lab = img, ca, labeler
        self.cls = ca.classify()

    # -- pointer pass: find every word that will be symbolised ---------
    def collect_pointers(self, code_chunks, data_chunks):
        self.ptr_words = {}
        img = self.img
        for lo, hi in code_chunks:
            for a in range(lo, hi, 2):
                c = self.cls.get(a)
                if c == "L" or (c == "?" and a % 4 == 0 and self.cls.get(a + 2) == "?"):
                    v = img.u32(a)
                    s = self.lab.pointer_label(v)
                    if s:
                        self.ptr_words[a] = v
        for lo, hi in data_chunks:
            for a in range(lo + (-lo) % 4, min(hi, img.end) - 3, 4):
                v = img.u32(a)
                s = self.lab.pointer_label(v)
                if s:
                    self.ptr_words[a] = v

    def label_lines(self, a, exported, local_ok=True):
        out = []
        if a in self.lab.glob:
            out.append(f"{self.lab.glob[a]}:")
            exported.add(self.lab.glob[a])
        if local_ok and a in self.lab.local and a not in self.lab.glob:
            out.append(f"L{a:08X}:")
        return out

    def has_label(self, a):
        return a in self.lab.glob or (a in self.lab.local and self.ca.in_code(a))

    def sym_expr(self, v):
        return self.lab.pointer_label(v)

    # -- data emission ------------------------------------------------
    def emit_data(self, lo, hi, lines, exported, imported, defined):
        """Emit bytes [lo,hi) as data directives with labels and pointers."""
        img = self.img
        a = lo
        pend = []   # pending (kind, value) for line grouping
        pend_kind = None

        def flush():
            nonlocal pend, pend_kind
            if pend:
                lines.append(f"{COL}{pend_kind:<11} {','.join(pend)}")
            pend, pend_kind = [], None

        def put(kind, text):
            nonlocal pend_kind
            # asmsh rejects statements longer than ~255 characters
            if pend_kind != kind or len(pend) >= 8 or \
                    sum(len(x) + 1 for x in pend) + len(text) > 200:
                flush()
                pend_kind = kind
            pend.append(text)

        while a < hi:
            if self.has_label(a) and a != lo or (a == lo and self.has_label(a)):
                flush()
                lines.extend(self.label_lines(a, exported))
            if a >= img.end:
                # BSS
                b = a + 1
                while b < hi and not self.has_label(b):
                    b += 1
                flush()
                lines.append(f"{COL}{'.RES.B':<11} {b - a}")
                a = b
                continue
            nlab = lambda n: any(self.has_label(a + k) for k in range(1, n))
            if a in self.ptr_words and a + 4 <= hi and not nlab(4):
                s = self.sym_expr(self.ptr_words[a])
                if s not in defined:
                    imported.add(s)
                put(".DATA.L", s)
                a += 4
                continue
            # ASCII string run (C string, NUL terminated)
            if a % 4 == 0 and a not in self.ptr_words:
                b = a
                while b < hi and b < img.end and is_printable(img.data[b - img.base]) and \
                        (b == a or not self.has_label(b)) and b not in self.ptr_words:
                    b += 1
                if b - a >= 4 and b < hi and img.data[b - img.base] == 0 and \
                        not self.has_label(b):
                    flush()
                    txt = img.data[a - img.base:b - img.base].decode("ascii")
                    lines.append(f'{COL}{".SDATA":<11} "{txt}"')
                    a = b
                    continue
            if a % 4 == 0 and a + 4 <= hi and not nlab(4) and \
                    not any((a + k) in self.ptr_words for k in (1, 2, 3)):
                put(".DATA.L", f"H'{img.u32(a):08X}")
                a += 4
            elif a % 2 == 0 and a + 2 <= hi and not nlab(2):
                put(".DATA.W", f"H'{img.u16(a):04X}")
                a += 2
            else:
                put(".DATA.B", f"H'{img.data[a - img.base]:02X}")
                a += 1
        flush()

    # -- code emission ------------------------------------------------
    def emit_code(self, lo, hi, lines, exported, imported, defined):
        img, ca, cls = self.img, self.ca, self.cls
        a = lo
        while a < hi:
            c = cls.get(a, "?")
            if c in ("?", "P"):
                b = a + 2
                while b < hi and cls.get(b, "?") in ("?", "P") and b not in ca.funcs:
                    b += 2
                self.emit_data(a, b, lines, exported, imported, defined)
                a = b
                continue
            lines.extend(self.label_lines(a, exported))
            if c == "I":
                ins = ca.insn[a]
                if a in ca.funcs:
                    pass
                txt = format_insn(ins, self.lab.label_for_insn)
                for o in ins.ops:
                    if o[0] == "branch" and o[1] in self.lab.glob and o[1] not in defined:
                        imported.add(self.lab.glob[o[1]])
                lines.append(f"{COL}{txt:<40}; {a:08X} {ins.word:04X}")
                a += 2
            elif c == "L":
                if self.has_label(a + 2):
                    lines.append(f"{COL}{'.DATA.W':<11} H'{img.u16(a):04X}")
                    lines.extend(self.label_lines(a + 2, exported))
                    lines.append(f"{COL}{'.DATA.W':<11} H'{img.u16(a + 2):04X}")
                elif a in self.ptr_words:
                    s = self.sym_expr(self.ptr_words[a])
                    if s not in defined:
                        imported.add(s)
                    lines.append(f"{COL}{'.DATA.L':<11} {s}")
                else:
                    lines.append(f"{COL}{'.DATA.L':<11} H'{img.u32(a):08X}")
                a += 4
            elif c == "W":
                lines.append(f"{COL}{'.DATA.W':<11} H'{img.u16(a):04X}")
                a += 2
            elif c == "J":
                jt = ca.jt_entry[a]
                if jt.esize == 2:
                    k = (a - jt.addr) // 2
                    t = jt.targets[k]
                    lines.append(f"{COL}{'.DATA.W':<11} {self.lab.label_for_insn(t)}-{self.lab.label_for_insn(jt.base)}")
                    a += 2
                else:
                    lines.append(f"{COL}{'.DATA.W':<11} H'{img.u16(a):04X}")
                    a += 2
            elif c == "l":
                # second half of a pool entry that started before this chunk
                lines.append(f"{COL}{'.DATA.W':<11} H'{img.u16(a):04X}")
                a += 2
            else:
                raise AssertionError(c)


def cut_points(ca, lo, hi):
    """4-aligned addresses in (lo,hi) where the code can be split into separate
    objects: no PC-relative load, branch, BSR or jump table spans the cut."""
    span_end = {}   # start -> furthest end
    def span(x, y):
        s, e = min(x, y), max(x, y)
        if e - s <= 0:
            return
        if span_end.get(s, -1) < e:
            span_end[s] = e
    for a, ins in ca.insn.items():
        if not (lo <= a < hi):
            continue
        pc = ins.pcrel
        if pc:
            span(a, pc[2] + (4 if pc[1] != 2 else 2))
        t = ins.branch_target
        if t is not None:
            span(a, t + 2)
        if ins.delayed:
            span(a, a + 4)
    for ja, t in ca.long_branches.items():
        if lo <= ja < hi:
            span(ja, t + 2)
    for ta, jt in ca.jtab.items():
        if lo <= ta < hi:
            span(jt.base - 4, ta + jt.count * jt.esize)
            for t in jt.targets:
                span(jt.base, t + 2)
    # sweep
    covered_until = lo
    starts = sorted(span_end)
    i = 0
    cuts = []
    cand = sorted(set(f for f in ca.funcs if lo < f < hi and f % 4 == 0))
    for c in cand:
        while i < len(starts) and starts[i] < c:
            covered_until = max(covered_until, span_end[starts[i]])
            i += 1
        if covered_until <= c:
            cuts.append(c)
    return cuts
