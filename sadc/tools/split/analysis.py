"""Recursive-descent code analysis for SHC-compiled SH-4 binaries.

Classifies every halfword of the configured code ranges as instruction,
literal-pool / jump-table data, padding, or unknown, and discovers function
entry points. The result drives the asm emitter (emit.py).

Discovery order:
  1. strong seeds: entry points, symbol-file functions, BSR targets,
     JSR/JMP targets loaded from literal pools;
  2. pointer seeds: aligned 32-bit words anywhere in the image that point
     into a code range; accepted only if they start in a not-yet-covered gap;
  3. gap seeds: the first non-padding halfword of each remaining gap
     (functions reachable only through pointers we could not see).
A tentative traversal that hits an invalid encoding, runs into classified
data, or overlaps another function's body is rejected.
"""
import bisect
import struct

from sh4.isa import decode, read_u16, read_u32

NOP = 0x0009
# alignment fill seen in SHC output: NOP, byte-swapped NOP (asmsh .ALIGN fill), zero
PAD_WORDS = (0x0009, 0x0900, 0x0000)


class Image:
    def __init__(self, data, base, bss_end=None):
        self.data = data
        self.base = base
        self.end = base + len(data)
        self.bss_end = bss_end or self.end

    def has(self, addr, size=1):
        return self.base <= addr and addr + size <= self.end

    def u16(self, addr):
        return read_u16(self.data, addr - self.base)

    def u32(self, addr):
        return read_u32(self.data, addr - self.base)

    def s16(self, addr):
        v = self.u16(addr)
        return v - 0x10000 if v & 0x8000 else v


class JumpTable:
    __slots__ = ("addr", "base", "esize", "count", "targets")

    def __init__(self, addr, base, esize, count, targets):
        self.addr, self.base, self.esize = addr, base, esize
        self.count, self.targets = count, targets


class Rejected(Exception):
    pass


class CodeAnalysis:
    def __init__(self, img, code_ranges, log=None):
        self.img = img
        self.code_ranges = sorted(code_ranges)
        self.insn = {}        # addr -> Insn (instruction starts)
        self.owner = {}       # addr -> function entry owning the instruction
        self.data = {}        # addr -> 'L' | 'W' (pool entries)
        self.jtab = {}        # table addr -> JumpTable
        self.jt_entry = {}    # entry addr -> JumpTable
        self.funcs = {}       # entry -> {'end': max insn addr + 2, 'src': reason}
        self.code_labels = set()   # branch / pool targets needing labels
        self.calls = {}       # entry -> set(callee entries)
        self.ptr_refs = {}    # addr of pointer word -> target (for symbolization)
        self.log = log or (lambda *a: None)
        self.rejected = {}
        self.known_entries = set()
        self.stop_entries = set()
        self.split_points = set()
        self.bsr_targets = set()
        self.long_branches = {}
        self.strong_seen = set()
        self.ptr_targets = set()
        self.data_ptrs = set()

    # ------------------------------------------------------------------
    def in_code(self, addr):
        for lo, hi in self.code_ranges:
            if lo <= addr < hi:
                return True
        return False

    def range_of(self, addr):
        for lo, hi in self.code_ranges:
            if lo <= addr < hi:
                return lo, hi
        return None

    def covered(self, addr):
        return addr in self.insn or addr in self.data or addr in self.jt_entry or \
            (addr - 2) in self.data and self.data[addr - 2] == "L"

    # ------------------------------------------------------------------
    def _jumptable(self, braf_addr, body):
        """Recognise SHC's  MOVA tbl,R0 ; MOV.W @(R0,Rx),R0 ; BRAF R0  idiom."""
        movw = body.get(braf_addr - 2)
        mova = body.get(braf_addr - 4)
        if not movw or not mova or mova.mnem != "MOVA":
            return None
        if movw.mnem not in ("MOV.W", "MOV.B") or not movw.ops[0][1].startswith("@(R0,"):
            return None
        esize = 2 if movw.mnem == "MOV.W" else 1
        tbl = mova.pcrel[2]
        base = braf_addr + 4
        # bound: look back for CMP/HS Rk,R0 or CMP/HI with MOV #n,Rk
        count = None
        a = braf_addr - 4
        for _ in range(12):
            a -= 2
            i = body.get(a)
            if i is None:
                break
            if i.mnem in ("CMP/HS", "CMP/HI", "CMP/GT", "CMP/GE") and i.ops[1][1] == "R0":
                kreg = i.ops[0][1]
                b = a
                for _ in range(8):
                    b -= 2
                    j = body.get(b)
                    if j is None:
                        break
                    if j.mnem == "MOV" and j.ops[0][0] == "imm" and j.ops[1][1] == kreg:
                        count = j.ops[0][1] + (1 if i.mnem in ("CMP/HI", "CMP/GT") else 0)
                        break
                break
        targets = []
        lo_tgt = None
        k = 0
        limit = count if count else 512
        while k < limit:
            ea = tbl + k * esize
            if lo_tgt is not None and ea >= lo_tgt and not count:
                break
            if not self.img.has(ea, esize):
                break
            if esize == 2:
                off = self.img.s16(ea)
            else:
                off = self.img.data[ea - self.img.base]
                off = off - 0x100 if off & 0x80 else off
            t = base + off
            if t & 1 or not self.in_code(t):
                if count:
                    raise Rejected(f"bad jumptable entry at {ea:08X}")
                break
            targets.append(t)
            lo_tgt = t if lo_tgt is None or t < lo_tgt else lo_tgt
            k += 1
        if not targets:
            return None
        return JumpTable(tbl, base, esize, len(targets), targets)

    # ------------------------------------------------------------------
    @staticmethod
    def _pool_load_for(body, addr, reg):
        """Literal-pool address loaded into `reg` before the jump at addr."""
        b = addr - 2
        for _ in range(16):
            j = body.get(b)
            if j is None:
                return None
            if j.ops and j.ops[-1][0] == "reg" and j.ops[-1][1] == reg:
                if j.mnem == "MOV.L" and j.pcrel:
                    return j.pcrel[2]
                return None
            if j.delayed or j.mnem in ("BT", "BF"):
                return None
            b -= 2
        return None

    @staticmethod
    def _pr_restored(body, addr, labels):
        """True if PR is restored between the last block start and the jump."""
        nxt = body.get(addr + 2)
        if nxt is not None and nxt.mnem == "LDS.L" and nxt.ops[1][1] == "PR":
            return True
        b = addr - 2
        for _ in range(24):
            j = body.get(b)
            if j is None:
                return False
            if j.mnem == "LDS.L" and j.ops[1][1] == "PR":
                return True
            if j.delayed or b in labels:
                return False
            b -= 2
        return False

    # ------------------------------------------------------------------
    def _traverse(self, entry):
        """Walk one function. Returns (body, data, jtabs, calls, ptr_loads)
        without committing anything; raises Rejected."""
        img = self.img
        body = {}
        data = {}
        jtabs = []
        calls = set()
        tail_ptrs = set()
        labels = set()
        work = [entry]
        seen = set()
        jmp_pools = set()
        jsr_targets = set()
        long_br = {}
        saved_pr = [False]

        def take(addr, in_slot=False):
            if addr in body:
                return body[addr]
            if not img.has(addr, 2) or not self.in_code(addr):
                raise Rejected(f"leaves code range at {addr:08X}")
            if addr in self.data or addr in self.jt_entry or addr in data or \
                    (addr - 2) in self.data and self.data[addr - 2] == "L" or \
                    (addr - 2) in data and data[addr - 2] == "L":
                raise Rejected(f"runs into data at {addr:08X}")
            own = self.owner.get(addr)
            if own is not None and own != entry:
                raise Rejected(f"overlaps function {own:08X} at {addr:08X}")
            ins = decode(img.u16(addr), addr)
            if ins is None:
                raise Rejected(f"invalid opcode {img.u16(addr):04X} at {addr:08X}")
            if in_slot and ins.slot_illegal():
                raise Rejected(f"illegal delay slot at {addr:08X}")
            body[addr] = ins
            if ins.mnem == "STS.L" and ins.ops[0][1] == "PR":
                saved_pr[0] = True
            pc = ins.pcrel
            if pc:
                size, tgt = pc[1], pc[2]
                if not img.has(tgt, 2):
                    raise Rejected(f"pc-relative target outside image at {addr:08X}")
                if tgt in body or (tgt + 2) in body and size == 4:
                    raise Rejected(f"pool overlaps code at {tgt:08X}")
                labels.add(tgt)
                if size == 4:
                    data[tgt] = "L"
                elif size == 2:
                    data.setdefault(tgt, "W")
                else:  # MOVA: referenced object, size unknown; at least a word
                    data.setdefault(tgt, "L" if tgt % 4 == 0 else "W")
            return ins

        while work:
            addr = work.pop()
            if addr in seen:
                continue
            while True:
                if addr in seen:
                    break
                if addr != entry and (addr in self.funcs or addr in self.stop_entries):
                    # fell / branched into another function: tail jump
                    # (SHC cross-jumps a wrapper into the function after it)
                    calls.add(addr)
                    break
                seen.add(addr)
                ins = take(addr)
                m = ins.mnem
                if ins.delayed:
                    take(addr + 2, in_slot=True)
                    seen.add(addr + 2)
                    if m in ("BRA",):
                        t = ins.branch_target
                        labels.add(t)
                        # a BRA to a known entry, or to before our own entry,
                        # is a tail call
                        # (only to a plausible function start: otherwise this
                        # "function" is a fragment of a bigger one and gets rejected)
                        if (t in self.funcs or t in self.stop_entries or
                                (t < entry and self.clean_start(t))) and t != entry:
                            calls.add(t)
                        else:
                            work.append(t)
                        break
                    if m in ("BT/S", "BF/S"):
                        t = ins.branch_target
                        labels.add(t)
                        work.append(t)
                        addr += 4
                        continue
                    if m == "BSR":
                        calls.add(ins.branch_target)
                        self.bsr_targets.add(ins.branch_target)
                        labels.add(ins.branch_target)
                        addr += 4
                        continue
                    if m in ("JSR", "BSRF"):
                        if m == "JSR":
                            pa = self._pool_load_for(body, addr, ins.ops[0][1][1:])
                            if pa is not None and img.has(pa, 4):
                                t = img.u32(pa)
                                if self.in_code(t) and not t & 1:
                                    jsr_targets.add(t)
                        addr += 4
                        continue
                    if m == "BRAF":
                        jt = self._jumptable(addr, body)
                        if jt:
                            jtabs.append(jt)
                            for t in jt.targets:
                                labels.add(t)
                                work.append(t)
                            labels.add(jt.addr)
                            labels.add(jt.base)
                        break
                    if m == "JMP":
                        # MOV.L Lx,Rn ; JMP @Rn is either a tail call or, while
                        # the frame is live (PR saved, not yet restored), SHC's
                        # long branch inside one function.
                        pa = self._pool_load_for(body, addr, ins.ops[0][1][1:])
                        if pa is not None and img.has(pa, 4):
                            tgt = img.u32(pa)
                            if self.in_code(tgt) and not tgt & 1:
                                jmp_pools.add(pa)
                                # a long branch stays inside one function: after
                                # its entry and within 32 KB of it (the largest
                                # SHC function here is ~19 KB)
                                if tgt not in self.known_entries and tgt not in self.funcs \
                                        and entry <= tgt < entry + 0x8000 \
                                        and saved_pr[0] and not self._pr_restored(body, addr, labels):
                                    long_br[addr] = tgt
                                    labels.add(tgt)
                                    work.append(tgt)
                                else:
                                    tail_ptrs.add(tgt)
                        break
                    break  # RTS / RTE
                if m in ("BT", "BF"):
                    t = ins.branch_target
                    labels.add(t)
                    work.append(t)
                    addr += 2
                    continue
                if m in ("SLEEP",):
                    addr += 2
                    continue
                addr += 2
        # literal-pool code pointers loaded for JSR/JMP become call seeds
        ptr_loads = {}
        for a, ins in body.items():
            pc = ins.pcrel
            if pc and pc[1] == 4 and img.has(pc[2], 4):
                ptr_loads[pc[2]] = img.u32(pc[2])
        for jt in jtabs:
            for k in range(jt.count):
                ea = jt.addr + k * jt.esize
                if ea in body:
                    raise Rejected(f"jump table overlaps code at {ea:08X}")
        for a in data:
            if a in body or (data[a] == "L" and a + 2 in body):
                raise Rejected(f"pool entry overlaps code at {a:08X}")
        self._last_extra = (jmp_pools, jsr_targets, long_br)
        return body, data, jtabs, calls | tail_ptrs, ptr_loads, labels

    def clean_start(self, a):
        """Could a function begin at a? (weak evidence only): the previous
        bytes must be padding / data, or the delay slot of a block end."""
        img = self.img
        if not img.has(a - 4, 4):
            return True
        if (a - 2) in self.data or (a - 4) in self.data or (a - 2) in self.jt_entry:
            return True
        if img.u16(a - 2) in PAD_WORDS:
            return True
        prev = decode(img.u16(a - 4), a - 4)
        if prev is not None and prev.mnem in ("RTS", "BRA", "JMP", "RTE", "BRAF"):
            return True
        return False

    def add_function(self, entry, src, tentative=False):
        if entry in self.funcs:
            return True
        if tentative and not src.startswith("gap") and not self.clean_start(entry):
            self.rejected[entry] = (src, "not at a clean function boundary")
            return False
        if entry in self.insn and self.owner.get(entry) != entry:
            # entry lies inside an already-analysed function body
            if tentative:
                return False
        try:
            body, data, jtabs, calls, ptr_loads, labels = self._traverse(entry)
        except Rejected as e:
            self.rejected[entry] = (src, str(e))
            return False
        jmp_pools, jsr_targets, long_br = self._last_extra
        self.funcs[entry] = {"src": src, "end": max(body) + 2}
        self.long_branches.update(long_br)
        self.strong_seen |= jsr_targets
        for pa, val in ptr_loads.items():
            if pa not in jmp_pools and self.in_code(val) and not val & 1:
                self.ptr_targets.add(val)
        for t in jsr_targets:
            self._pending.append((t, f"jsr from {entry:08X}"))
        for a, ins in body.items():
            self.insn[a] = ins
            self.owner[a] = entry
        for a, k in data.items():
            if self.data.get(a) != "L":
                self.data[a] = k
        for jt in jtabs:
            self.jtab[jt.addr] = jt
            for k in range(jt.count):
                self.jt_entry[jt.addr + k * jt.esize] = jt
        self.code_labels |= labels
        self.calls[entry] = calls
        for pa, val in ptr_loads.items():
            self.ptr_refs[pa] = val
        self._pending.extend((c, f"call from {entry:08X}") for c in calls)
        for val in ptr_loads.values():
            if self.in_code(val) and not val & 1:
                self._pending_ptr.append((val, f"pool ptr in {entry:08X}"))
        return True

    # ------------------------------------------------------------------
    def _reset(self):
        self.insn, self.owner, self.data = {}, {}, {}
        self.jtab, self.jt_entry, self.funcs = {}, {}, {}
        self.code_labels, self.calls, self.ptr_refs = set(), {}, {}
        self.rejected = {}
        self.long_branches = {}
        self.strong_seen = set()
        self.ptr_targets = set()
        self.split_points = set()
        self.bsr_targets = set()

    def run(self, seeds, data_ptr_ranges=(), max_iter=5):
        """Discover functions; repeat with the previous pass's evidence so
        tail calls and long branches are told apart consistently."""
        known = set(a for a, _ in seeds)
        stop = set(known)
        for it in range(max_iter):
            self._reset()
            self.known_entries = known
            self.stop_entries = stop
            self._run_once(seeds, data_ptr_ranges)
            new = set(a for a, _ in seeds) | self.strong_seen | self.ptr_targets | self.data_ptrs
            new |= {f for f, v in self.funcs.items() if not v["src"].startswith("gap")}
            new |= self.split_points
            new -= set(self.long_branches.values())
            # entries that end other functions' traversal: strong evidence only
            new_stop = set(a for a, _ in seeds) | self.strong_seen | self.bsr_targets | \
                self.split_points | set(self.funcs)
            new_stop -= set(self.long_branches.values())
            self.log(f"pass {it}: {len(self.funcs)} functions, {len(self.long_branches)} long branches, "
                     f"{len(self.split_points)} split points")
            new_stop |= stop   # monotonic: guarantees convergence
            if new == known and new_stop == stop:
                break
            known, stop = new, new_stop

    def _run_once(self, seeds, data_ptr_ranges=()):
        self._pending = list(seeds)
        self._pending_ptr = []
        self._drain()
        # pointer seeds from data words outside code
        self.data_ptrs = set()
        for lo, hi in data_ptr_ranges:
            for a in range(lo + (-lo) % 4, hi - 3, 4):
                v = self.img.u32(a)
                if self.in_code(v) and not v & 1:
                    self.data_ptrs.add(v)
                    self._pending_ptr.append((v, f"data ptr at {a:08X}"))
        self._drain_ptrs()
        # gap seeds until fixed point
        for _ in range(50):
            added = 0
            for g0, g1 in self.gaps():
                s = g0
                while s < g1 and self.img.u16(s) in PAD_WORDS:
                    s += 2
                if s >= g1 or s in self.rejected:
                    continue
                if self.add_function(s, "gap", tentative=True):
                    added += 1
                    self._drain()
                    self._drain_ptrs()
            if not added:
                break

    def _drain(self):
        while self._pending:
            a, src = self._pending.pop()
            if a in self.funcs or not self.in_code(a) or a & 1:
                continue
            if a in self.insn and self.owner.get(a) != a:
                # call into the middle of an analysed body: becomes an entry
                # (and splits that body) on the next pass
                self.rejected.setdefault(a, (src, "inside other function"))
                self.split_points.add(a)
                continue
            self.add_function(a, src)

    def _drain_ptrs(self):
        self._drain()
        while self._pending_ptr:
            a, src = self._pending_ptr.pop()
            if a in self.funcs:
                continue
            if a in self.insn and self.owner.get(a) != a and self.clean_start(a):
                # a code pointer to a clean block start inside another body:
                # that body absorbed a function through a BRA tail call
                self.split_points.add(a)
                continue
            if a in self.insn or self.covered(a):
                continue
            if self.add_function(a, src, tentative=True):
                self._drain()

    # ------------------------------------------------------------------
    def gaps(self):
        out = []
        for lo, hi in self.code_ranges:
            a = lo
            while a < hi:
                if self.covered(a):
                    a += 2
                    continue
                b = a
                while b < hi and not self.covered(b):
                    b += 2
                out.append((a, b))
                a = b
        return out

    def classify(self):
        """Per-halfword class map for every code range: 'I', 'L', 'l', 'W', 'J', 'P', '?'.
        'L' = start of 4-byte pool entry, 'l' = its second half."""
        cls = {}
        for lo, hi in self.code_ranges:
            for a in range(lo, hi, 2):
                if a in self.insn:
                    cls[a] = "I"
                elif self.data.get(a) == "L":
                    cls[a] = "L"
                    cls[a + 2] = "l"
                elif a in cls:
                    continue
                elif a in self.data:
                    cls[a] = "W"
                elif a in self.jt_entry:
                    cls[a] = "J"
                else:
                    w = self.img.u16(a)
                    cls[a] = "P" if w in PAD_WORDS else "?"
        return cls
