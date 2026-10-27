"""SH-4 instruction decoder producing Hitachi asmsh / SHC-style syntax.

Every 16-bit word decodes to at most one instruction. Encodings that are not
valid SH-4 instructions decode to None (callers emit them as .DATA.W).

FPU instructions are always printed in their single-precision / SZ=0 form
(FRn, FMOV.S); the encodings are identical for the DRn / XDn forms, so
reassembly is byte-exact regardless of the FPSCR mode at runtime. The
exceptions are instructions that only exist with DR/FV operands
(FCNVSD, FCNVDS, FIPR, FTRV).

Operand kinds (tuples) in Insn.ops:
  ("reg", "R4")             register / control register / FR register
  ("imm", value)            immediate (signed for MOV/ADD/CMP/EQ, else 0..255)
  ("mem", text)             memory operand with fixed text
  ("pcrel", size, target)   PC-relative MOV.W (2) / MOV.L (4) / MOVA (0);
                            target is the absolute address referenced
  ("branch", target)        BRA/BSR/BT/BF[/S] destination address
"""
import struct

R = [f"R{i}" for i in range(16)]
FR = [f"FR{i}" for i in range(16)]

BRANCH_UNCOND = {"BRA", "BSR"}
BRANCH_COND = {"BT", "BF", "BT/S", "BF/S"}
DELAYED = {"BRA", "BSR", "BT/S", "BF/S", "JMP", "JSR", "RTS", "RTE", "BRAF", "BSRF"}
# no fallthrough past the delay slot
ENDS_BLOCK = {"BRA", "JMP", "RTS", "RTE", "BRAF"}
# valid on the Dreamcast CPU but unknown to asmsh: printed as .DATA.W
RAW_DATA_MNEMONICS = {"FSCA", "FSRRA"}
# instructions that may not be placed in a delay slot
SLOT_ILLEGAL = DELAYED | {"BT", "BF", "TRAPA", "LDC", "LDC.L"}


class Insn:
    __slots__ = ("word", "mnem", "ops", "addr")

    def __init__(self, word, mnem, ops, addr):
        self.word, self.mnem, self.ops, self.addr = word, mnem, ops, addr

    @property
    def delayed(self):
        return self.mnem in DELAYED

    @property
    def pcrel(self):
        for o in self.ops:
            if o[0] == "pcrel":
                return o
        return None

    @property
    def branch_target(self):
        for o in self.ops:
            if o[0] == "branch":
                return o[1]
        return None

    def slot_illegal(self):
        if self.mnem in SLOT_ILLEGAL:
            return True
        # PC-relative addressing is illegal in a delay slot too
        return self.pcrel is not None

    def __repr__(self):
        return f"<{self.addr:08X} {format_insn(self)}>"


def _s8(v):
    return v - 0x100 if v & 0x80 else v


def _s12(v):
    return v - 0x1000 if v & 0x800 else v


_FIXED = {
    0x0008: "CLRT", 0x0009: "NOP", 0x000B: "RTS", 0x0018: "SETT", 0x0019: "DIV0U",
    0x001B: "SLEEP", 0x0028: "CLRMAC", 0x002B: "RTE", 0x0038: "LDTLB",
    0x0048: "CLRS", 0x0058: "SETS", 0xF3FD: "FSCHG", 0xFBFD: "FRCHG",
}

# 0000 nnnn xxxx xxxx; kind 0 = CR,Rn  1 = Rn  2 = @Rn  3 = R0,@Rn
_G0_N = {
    0x02: ("STC", "SR", 0), 0x12: ("STC", "GBR", 0), 0x22: ("STC", "VBR", 0),
    0x32: ("STC", "SSR", 0), 0x42: ("STC", "SPC", 0), 0x3A: ("STC", "SGR", 0),
    0xFA: ("STC", "DBR", 0),
    0x0A: ("STS", "MACH", 0), 0x1A: ("STS", "MACL", 0), 0x2A: ("STS", "PR", 0),
    0x5A: ("STS", "FPUL", 0), 0x6A: ("STS", "FPSCR", 0),
    0x29: ("MOVT", None, 1),
    0x03: ("BSRF", None, 1), 0x23: ("BRAF", None, 1),
    0x83: ("PREF", None, 2), 0x93: ("OCBI", None, 2), 0xA3: ("OCBP", None, 2),
    0xB3: ("OCBWB", None, 2), 0xC3: ("MOVCA.L", None, 3),
}

_G2 = {0x0: "MOV.B", 0x1: "MOV.W", 0x2: "MOV.L", 0x4: "MOV.B", 0x5: "MOV.W",
       0x6: "MOV.L", 0x7: "DIV0S", 0x8: "TST", 0x9: "AND", 0xA: "XOR", 0xB: "OR",
       0xC: "CMP/STR", 0xD: "XTRCT", 0xE: "MULU.W", 0xF: "MULS.W"}
_G3 = {0x0: "CMP/EQ", 0x2: "CMP/HS", 0x3: "CMP/GE", 0x4: "DIV1", 0x5: "DMULU.L",
       0x6: "CMP/HI", 0x7: "CMP/GT", 0x8: "SUB", 0xA: "SUBC", 0xB: "SUBV",
       0xC: "ADD", 0xD: "DMULS.L", 0xE: "ADDC", 0xF: "ADDV"}
_G6 = {0x0: "MOV.B", 0x1: "MOV.W", 0x2: "MOV.L", 0x3: "MOV", 0x4: "MOV.B",
       0x5: "MOV.W", 0x6: "MOV.L", 0x7: "NOT", 0x8: "SWAP.B", 0x9: "SWAP.W",
       0xA: "NEGC", 0xB: "NEG", 0xC: "EXTU.B", 0xD: "EXTU.W", 0xE: "EXTS.B",
       0xF: "EXTS.W"}

_G4_N = {
    0x00: "SHLL", 0x10: "DT", 0x20: "SHAL", 0x01: "SHLR", 0x11: "CMP/PZ",
    0x21: "SHAR", 0x04: "ROTL", 0x24: "ROTCL", 0x05: "ROTR", 0x15: "CMP/PL",
    0x25: "ROTCR", 0x08: "SHLL2", 0x18: "SHLL8", 0x28: "SHLL16", 0x09: "SHLR2",
    0x19: "SHLR8", 0x29: "SHLR16",
}
_G4_AT = {0x0B: "JSR", 0x1B: "TAS.B", 0x2B: "JMP"}
_G4_LDC = {0x0E: "SR", 0x1E: "GBR", 0x2E: "VBR", 0x3E: "SSR", 0x4E: "SPC", 0xFA: "DBR"}
_G4_LDCL = {0x07: "SR", 0x17: "GBR", 0x27: "VBR", 0x37: "SSR", 0x47: "SPC", 0xF6: "DBR"}
_G4_STCL = {0x03: "SR", 0x13: "GBR", 0x23: "VBR", 0x33: "SSR", 0x43: "SPC",
            0x32: "SGR", 0xF2: "DBR"}
_G4_LDS = {0x0A: "MACH", 0x1A: "MACL", 0x2A: "PR", 0x5A: "FPUL", 0x6A: "FPSCR"}
_G4_LDSL = {0x06: "MACH", 0x16: "MACL", 0x26: "PR", 0x56: "FPUL", 0x66: "FPSCR"}
_G4_STSL = {0x02: "MACH", 0x12: "MACL", 0x22: "PR", 0x52: "FPUL", 0x62: "FPSCR"}

_GC = {0x8: "TST", 0x9: "AND", 0xA: "XOR", 0xB: "OR"}
_GC_B = {0xC: "TST.B", 0xD: "AND.B", 0xE: "XOR.B", 0xF: "OR.B"}

_GF = {0x0: "FADD", 0x1: "FSUB", 0x2: "FMUL", 0x3: "FDIV", 0x4: "FCMP/EQ",
       0x5: "FCMP/GT", 0xC: "FMOV"}

_MOVSZ = ["MOV.B", "MOV.W", "MOV.L"]


def _reg(x):
    return ("reg", x)


def _mem(t):
    return ("mem", t)


def decode(w, addr):
    """Decode one 16-bit word located at `addr`. Returns Insn or None."""
    if w in _FIXED:
        return Insn(w, _FIXED[w], (), addr)
    top = w >> 12
    n = (w >> 8) & 0xF
    m = (w >> 4) & 0xF
    lo = w & 0xF
    lo8 = w & 0xFF

    def I(mn, *ops):
        return Insn(w, mn, ops, addr)

    if top == 0x0:
        if lo8 in _G0_N:
            mn, cr, kind = _G0_N[lo8]
            if kind == 0:
                return I(mn, _reg(cr), _reg(R[n]))
            if kind == 1:
                return I(mn, _reg(R[n]))
            if kind == 2:
                return I(mn, _mem(f"@{R[n]}"))
            return I(mn, _reg("R0"), _mem(f"@{R[n]}"))
        if lo == 0x2 and (m & 0x8):
            return I("STC", _reg(f"R{m & 7}_BANK"), _reg(R[n]))
        if lo in (0x4, 0x5, 0x6):
            return I(_MOVSZ[lo - 4], _reg(R[m]), _mem(f"@(R0,{R[n]})"))
        if lo == 0x7:
            return I("MUL.L", _reg(R[m]), _reg(R[n]))
        if lo in (0xC, 0xD, 0xE):
            return I(_MOVSZ[lo - 0xC], _mem(f"@(R0,{R[m]})"), _reg(R[n]))
        if lo == 0xF:
            return I("MAC.L", _mem(f"@{R[m]}+"), _mem(f"@{R[n]}+"))
        return None

    if top == 0x1:
        return I("MOV.L", _reg(R[m]), _mem(f"@({lo * 4},{R[n]})"))

    if top == 0x2:
        if lo not in _G2:
            return None
        mn = _G2[lo]
        if lo <= 2:
            return I(mn, _reg(R[m]), _mem(f"@{R[n]}"))
        if lo <= 6:
            return I(mn, _reg(R[m]), _mem(f"@-{R[n]}"))
        return I(mn, _reg(R[m]), _reg(R[n]))

    if top == 0x3:
        if lo not in _G3:
            return None
        return I(_G3[lo], _reg(R[m]), _reg(R[n]))

    if top == 0x4:
        if lo8 in _G4_N:
            return I(_G4_N[lo8], _reg(R[n]))
        if lo8 in _G4_AT:
            return I(_G4_AT[lo8], _mem(f"@{R[n]}"))
        if lo8 in _G4_LDC:
            return I("LDC", _reg(R[n]), _reg(_G4_LDC[lo8]))
        if lo8 in _G4_LDCL:
            return I("LDC.L", _mem(f"@{R[n]}+"), _reg(_G4_LDCL[lo8]))
        if lo8 in _G4_STCL:
            return I("STC.L", _reg(_G4_STCL[lo8]), _mem(f"@-{R[n]}"))
        if lo8 in _G4_LDS:
            return I("LDS", _reg(R[n]), _reg(_G4_LDS[lo8]))
        if lo8 in _G4_LDSL:
            return I("LDS.L", _mem(f"@{R[n]}+"), _reg(_G4_LDSL[lo8]))
        if lo8 in _G4_STSL:
            return I("STS.L", _reg(_G4_STSL[lo8]), _mem(f"@-{R[n]}"))
        if lo == 0xE and (m & 0x8):
            return I("LDC", _reg(R[n]), _reg(f"R{m & 7}_BANK"))
        if lo == 0x7 and (m & 0x8):
            return I("LDC.L", _mem(f"@{R[n]}+"), _reg(f"R{m & 7}_BANK"))
        if lo == 0x3 and (m & 0x8):
            return I("STC.L", _reg(f"R{m & 7}_BANK"), _mem(f"@-{R[n]}"))
        if lo == 0xC:
            return I("SHAD", _reg(R[m]), _reg(R[n]))
        if lo == 0xD:
            return I("SHLD", _reg(R[m]), _reg(R[n]))
        if lo == 0xF:
            return I("MAC.W", _mem(f"@{R[m]}+"), _mem(f"@{R[n]}+"))
        return None

    if top == 0x5:
        return I("MOV.L", _mem(f"@({lo * 4},{R[m]})"), _reg(R[n]))

    if top == 0x6:
        mn = _G6[lo]
        if lo <= 2:
            return I(mn, _mem(f"@{R[m]}"), _reg(R[n]))
        if 4 <= lo <= 6:
            return I(mn, _mem(f"@{R[m]}+"), _reg(R[n]))
        return I(mn, _reg(R[m]), _reg(R[n]))

    if top == 0x7:
        return I("ADD", ("imm", _s8(lo8)), _reg(R[n]))

    if top == 0x8:
        if n == 0x0:
            return I("MOV.B", _reg("R0"), _mem(f"@({lo},{R[m]})"))
        if n == 0x1:
            return I("MOV.W", _reg("R0"), _mem(f"@({lo * 2},{R[m]})"))
        if n == 0x4:
            return I("MOV.B", _mem(f"@({lo},{R[m]})"), _reg("R0"))
        if n == 0x5:
            return I("MOV.W", _mem(f"@({lo * 2},{R[m]})"), _reg("R0"))
        if n == 0x8:
            return I("CMP/EQ", ("imm", _s8(lo8)), _reg("R0"))
        if n in (0x9, 0xB, 0xD, 0xF):
            mn = {0x9: "BT", 0xB: "BF", 0xD: "BT/S", 0xF: "BF/S"}[n]
            return I(mn, ("branch", addr + 4 + _s8(lo8) * 2))
        return None

    if top == 0x9:
        return I("MOV.W", ("pcrel", 2, addr + 4 + lo8 * 2), _reg(R[n]))

    if top in (0xA, 0xB):
        return I("BRA" if top == 0xA else "BSR",
                 ("branch", addr + 4 + _s12(w & 0xFFF) * 2))

    if top == 0xC:
        d = lo8
        if n == 0x0:
            return I("MOV.B", _reg("R0"), _mem(f"@({d},GBR)"))
        if n == 0x1:
            return I("MOV.W", _reg("R0"), _mem(f"@({d * 2},GBR)"))
        if n == 0x2:
            return I("MOV.L", _reg("R0"), _mem(f"@({d * 4},GBR)"))
        if n == 0x3:
            return I("TRAPA", ("imm", d))
        if n == 0x4:
            return I("MOV.B", _mem(f"@({d},GBR)"), _reg("R0"))
        if n == 0x5:
            return I("MOV.W", _mem(f"@({d * 2},GBR)"), _reg("R0"))
        if n == 0x6:
            return I("MOV.L", _mem(f"@({d * 4},GBR)"), _reg("R0"))
        if n == 0x7:
            return I("MOVA", ("pcrel", 0, ((addr + 4) & ~3) + d * 4), _reg("R0"))
        if n in _GC:
            return I(_GC[n], ("imm", d), _reg("R0"))
        return I(_GC_B[n], ("imm", d), _mem("@(R0,GBR)"))

    if top == 0xD:
        return I("MOV.L", ("pcrel", 4, ((addr + 4) & ~3) + lo8 * 4), _reg(R[n]))

    if top == 0xE:
        return I("MOV", ("imm", _s8(lo8)), _reg(R[n]))

    # top == 0xF: FPU
    if lo in _GF:
        return I(_GF[lo], _reg(FR[m]), _reg(FR[n]))
    if lo == 0x6:
        return I("FMOV.S", _mem(f"@(R0,{R[m]})"), _reg(FR[n]))
    if lo == 0x7:
        return I("FMOV.S", _reg(FR[m]), _mem(f"@(R0,{R[n]})"))
    if lo == 0x8:
        return I("FMOV.S", _mem(f"@{R[m]}"), _reg(FR[n]))
    if lo == 0x9:
        return I("FMOV.S", _mem(f"@{R[m]}+"), _reg(FR[n]))
    if lo == 0xA:
        return I("FMOV.S", _reg(FR[m]), _mem(f"@{R[n]}"))
    if lo == 0xB:
        return I("FMOV.S", _reg(FR[m]), _mem(f"@-{R[n]}"))
    if lo == 0xE:
        return I("FMAC", _reg("FR0"), _reg(FR[m]), _reg(FR[n]))
    if lo == 0xD:
        if m == 0x0:
            return I("FSTS", _reg("FPUL"), _reg(FR[n]))
        if m == 0x1:
            return I("FLDS", _reg(FR[n]), _reg("FPUL"))
        if m == 0x2:
            return I("FLOAT", _reg("FPUL"), _reg(FR[n]))
        if m == 0x3:
            return I("FTRC", _reg(FR[n]), _reg("FPUL"))
        if m == 0x4:
            return I("FNEG", _reg(FR[n]))
        if m == 0x5:
            return I("FABS", _reg(FR[n]))
        if m == 0x6:
            return I("FSQRT", _reg(FR[n]))
        if m == 0x8:
            return I("FLDI0", _reg(FR[n]))
        if m == 0x9:
            return I("FLDI1", _reg(FR[n]))
        if m == 0xA and not (n & 1):
            return I("FCNVSD", _reg("FPUL"), _reg(f"DR{n}"))
        if m == 0xB and not (n & 1):
            return I("FCNVDS", _reg(f"DR{n}"), _reg("FPUL"))
        if m == 0xE:
            return I("FIPR", _reg(f"FV{(n & 3) * 4}"), _reg(f"FV{(n >> 2) * 4}"))
        if m == 0xF and (n & 3) == 1:
            return I("FTRV", _reg("XMTRX"), _reg(f"FV{(n >> 2) * 4}"))
        # Not accepted by asmsh -cpu=sh4 (SH-4A mnemonics) but executed by the
        # Dreamcast CPU; emitted as .DATA.W (see RAW_DATA_MNEMONICS).
        if m == 0xF and not (n & 1):
            return I("FSCA", _reg("FPUL"), _reg(f"DR{n}"))
        if m == 0x7:
            return I("FSRRA", _reg(FR[n]))
        return None
    return None


def format_insn(insn, label_for=None):
    """Format an instruction in SHC output style ("MNEMONIC    OPERANDS").

    label_for(addr) -> str|None supplies symbolic names for PC-relative and
    branch targets. Without one, PC-relative loads use the explicit
    @(disp,PC) form and branches use $+n / $-n expressions.
    """
    if insn.mnem in RAW_DATA_MNEMONICS:
        return f"{'.DATA.W':<11} H'{insn.word:04X}"
    parts = []
    for o in insn.ops:
        k = o[0]
        if k == "reg" or k == "mem":
            parts.append(o[1])
        elif k == "imm":
            parts.append(f"#{o[1]}")
        elif k == "pcrel":
            tgt = o[2]
            name = label_for(tgt) if label_for else None
            if name:
                parts.append(name)
            else:
                base = (insn.addr + 4) & ~3 if o[1] in (0, 4) else insn.addr + 4
                parts.append(f"@({tgt - base},PC)")
        elif k == "branch":
            tgt = o[1]
            name = label_for(tgt) if label_for else None
            if name:
                parts.append(name)
            else:
                rel = tgt - insn.addr
                parts.append(f"$+{rel}" if rel >= 0 else f"$-{-rel}")
    if parts:
        return f"{insn.mnem:<11} {','.join(parts)}"
    return insn.mnem


def read_u16(data, off):
    return struct.unpack_from("<H", data, off)[0]


def read_u32(data, off):
    return struct.unpack_from("<I", data, off)[0]
