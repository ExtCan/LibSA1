#!/usr/bin/env python3
"""pclower - lower decompiled SA1 C to the GUEST-ARENA dialect (docs/libsa1_integration.md 2.3).

Per translation unit:

  1. preprocess: zig cc -E with the -D/-I/-std flags of tools/pc_cflags.txt plus -DSA1_GA, for x86
     (ILP32): the AST then has every struct in its DC layout and the SA1_ASSERT_* checks are real
     _Static_asserts, which stay valid for the lowered (struct-wrapped 32-bit pointer) layout on
     every target (R9);
  2. decimal float literals -> round-toward-zero hex floats (gen_native.rewrite_floats, R6);
  3. clang JSON AST of the preprocessed text (gen_native.ast_of);
  4. byte-offset edits (rules below); text that came from host system headers is replaced by its
     #include line, so the lowered file is target-independent (ga32 and ga64 share it);
  5. build/pclower/<profile>/lowered/<file>.c with #line markers back to the original sources.

Cross-TU outputs per profile (build/pclower/<profile>/): sa1g_fntab.c (hashed code map, section 3),
sa1g_stubs.c (fatal stubs, --stub-unresolved), sa1g_pcdata.c (R4 pcdata units), sa1g_layout_asserts.c
(R9, every record of include/), sa1g_symtab.h (data symbols with a class column), profile.json; and
build/pclower/report.json (rule counts, errors, escapes, allow lists), build/pclower/functions.json
(defined / referenced functions with ABI signatures, read by harness/tools/gen_ga.py).

Rules (names as in the design):
  R1  pointers in memory positions (fields, globals, array elements, pointees, escaped locals and
      parameters, block-scope records) -> sa1_gp; function pointer types -> sa1_gfn everywhere
      (typedefs lowered in place, declarators returning function pointers rewritten); value
      positions keep native pointers with lowered pointees; sizeof(T *) -> 4
  R2  loads SA1_LD, stores sa1_st (slot evaluated once), memory-to-memory copies and NULL stores
      as raw guest words, ++/--/+=/-= on memory pointers as raw guest arithmetic, &slot;
      initializer lists of automatic aggregates put {sa1_encode(p)} into pointer members
  R2b address-taken pointer locals AND parameters become sa1_gp storage
  R3  globals -> (*(T *)SA1_GV(addr)); extern declarations become typedefs of the lowered type;
      C definitions of symbols that have a DC address (static const char D_8C1113D8[] = ...) are
      the arena bytes (the matching build emits exactly them), so they are lowered like externs
  R4  C-defined data without a DC address (file-scope and static-local variables, string literals
      used as pointers) -> per-TU pcdata units, placed by sa1g_pcdata.c into the extension segment
      0x18 (ga64) or TU-static storage (ga32) and initialised by sa1g_pcd_init() at world creation
      (pointer members encoded then); non-static ones are exported to other TUs (sa1g_pcdv_NAME)
  R5  function designators -> DC code address constants, indirect calls through sa1_fn(g, sig),
      comparisons (==, !=, <, <=, >, >=) / NULL / truth values on sa1_gfn
  R6  float literals (RZ), non-constant / and % (also /= %=, side effects kept single) -> SHC
      semantics, float -> int -> FTRC, __builtin_fmaf -> SA1G_FMAC; R6c: sizeof / pointer
      differences keep the ILP32 width of the AST (size_t / ptrdiff_t are 64-bit on x86_64)
  R6b raw integer <-> pointer casts keep the raw guest word (SA1_UPTR = uint32_t)
  R7  rand/srand/memset/memcpy/strlen/strcpy/atan2f -> sa1g_* (declarations, definitions and uses);
      runtime functions and data without a DC address (SHC intrinsics, njFraction, sa1_tmu_tcnt1,
      sa1_rt_xmtrx, xm ...) map to the rt symbols of sa1/guest.h (RT_FUNCS / RT_DATA)
  R8  fail-closed: every problem is an error (report.json "errors"), cross-TU prototype
      conflicts included; R8_ALLOWED_CONFLICTS lists the conflicts still open in the sources
  R9  _Static_asserts kept, plus sa1g_layout_asserts.c (sizeof/offsetof of every record of
      include/, ILP32 values measured by clang) and per-TU asserts for records defined in .c files
  R10 zig/clang version and the AST format fingerprint are pinned; see --revalidate

    py -3.12 tools/pclower.py [--profile ga32|ga64|all] [--out DIR] [-j N] [--compile]
        [--stub-unresolved | --extern-unresolved] [--allow-conflict NAME]... FILE.c... [@RESPONSE_FILE]
    py -3.12 tools/pclower.py --all [-j 16] [--compile]       # src/1st_read + every src/overlays/* (+ libsa1 rt)
    py -3.12 tools/pclower.py --selftest                      # rule tests, ga32 + ga64 run, 3 targets
    py -3.12 tools/pclower.py --revalidate [--write-pin]      # R10 procedure for a new zig

--compile compiles every output for x86-windows (ga32), x86_64-windows and x86_64-linux (ga64).

Images (design 2.5): 1ST_READ (with libsa1's runtime) is the base image; each src/overlays/<dir> is the image of
one overlay file (config/overlays/<dir>.yaml: code range, PRS file and SHA-1). Overlays that share a load slot
(STGxx, B_CHAOS7, MINICART, SHOOTING at 0x0C900000; TIKAL_PROG over S_SBMOT) have overlapping DC code
addresses, so every overlay image gets
  * its own symbols first (build/<IMAGE>.symbols.txt, then the merged table: objexec_O_POLE is a different
    address in ADV00, B_CHAOS0, MINICART and STG03),
  * per-image C names: a function an image TU defines, or references at an address of the image's code range
    without defining it (an image stub), is renamed <IMAGE>__<name> by #define lines at the top of the lowered
    file (two overlays may both define func_0C900000), and the lowered file is <dir>__<file>.c,
  * its own code map in sa1g_fntab.c (sa1g_fnimages[]: name, PRS SHA-1, code range, lookup). The world lists
    the images it holds (exactly that PRS) in sa1g_fn_active[] while it runs; sa1g_fn_lookup / sa1_fn look a
    DC code address up in those images first (by the image's code range), then in the base map. A base TU's
    direct call into an overlay slot (1ST_READ calls 0C907998 of STG03) stays a base stub; the stub generator
    of the host dispatches it to the running world's image (sa1g_fn_image_lookup) when that defines it.
--extern-unresolved (harness/build.py --ga) leaves functions no lowered TU defines external: the
host provides them (the harness: thunks into the interpreter).
"""
import argparse
import bisect
import collections
import hashlib
import json
import os
import re
import shlex
import subprocess
import sys
import zlib

TOOLS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS)
sys.path.insert(0, os.path.join(ROOT, "harness", "tools"))
import gen_native as gn  # noqa: E402  ast_of, rewrite_floats, load_symbols, addr_of

PARSE_TARGET = "x86-windows-gnu"     # ILP32: the AST sees the DC layout
LIB_RT = os.path.join(os.path.dirname(ROOT), "libsa1", "src", "rt")
PROFILES = ("ga32", "ga64")

# R10: the toolchain the lowering was validated with. The AST JSON format (node kinds, keys, type
# spellings) changes between clang versions; a different zig fails the run until --revalidate passes
# and --write-pin records the new values here.
PINNED_ZIG = "0.16.0"
PINNED_CLANG = "clang version 21.1.0"
PINNED_AST_FINGERPRINT = "26fae1b844d7b276"

PROFILE_CFLAGS = {"ga32": ["-target", "x86-windows-gnu", "-msse2", "-mfpmath=sse", "-DSA1_GA32"],
                  "ga64": ["-target", "x86_64-windows-gnu"]}
# --compile: one ga32 and two ga64 targets
COMPILE_TARGETS = [("ga32", "x86-windows", ["-target", "x86-windows-gnu", "-msse2", "-mfpmath=sse", "-DSA1_GA32"]),
                   ("ga64", "x86_64-windows", ["-target", "x86_64-windows-gnu"]),
                   ("ga64", "x86_64-linux", ["-target", "x86_64-linux-gnu", "-fPIC"])]
# every pointer/integer mix-up is an error: a site pclower missed is a type error on sa1_gp / sa1_gfn
COMPILE_CHECK_FLAGS = ["-O1", "-std=gnu11", "-ffp-contract=off", "-fno-strict-aliasing", "-fwrapv", "-frounding-math",
                       "-fno-builtin", "-fsigned-char", "-Wno-unused-function", "-Wno-unused-variable",
                       "-Wno-unused-but-set-variable", "-Werror=incompatible-pointer-types",
                       "-Werror=incompatible-function-pointer-types", "-Werror=int-conversion",
                       "-Werror=implicit-function-declaration", "-Werror=pointer-integer-compare",
                       "-Werror=int-to-pointer-cast", "-Werror=pointer-to-int-cast"]

# R7: game functions whose names clash with the host C library -> name in sa1/guest.h, DC address
R7_RENAMES = {"rand": "sa1g_rand", "srand": "sa1g_srand", "memset": "sa1g_memset", "memcpy": "sa1g_memcpy",
              "strlen": "sa1g_strlen", "strcpy": "sa1g_strcpy", "atan2f": "sa1g_atan2f"}
# fixed ABI classes of the sa1/guest.h names (a game TU's own declaration may be inactive under TARGET_PC)
R7_SIGS = {"sa1g_rand": "i32:", "sa1g_srand": "v:u32", "sa1g_memset": "p:p,i32,u32", "sa1g_memcpy": "p:p,p,u32",
           "sa1g_strlen": "u32:p", "sa1g_strcpy": "p:p,p", "sa1g_atan2f": "f32:f32,f32"}
R7_ADDR = {"sa1g_rand": 0x8C10D334, "sa1g_srand": 0x8C10D39C, "sa1g_memset": 0x8C10D31C, "sa1g_memcpy": None,
           "sa1g_strlen": 0x8C10D3F0, "sa1g_strcpy": 0x8C10D3DC, "sa1g_atan2f": 0x8C10D0F8}

# R7: runtime functions without a DC address. SHC intrinsics are renamed to their sa1/guest.h names;
# the others keep their names. libsa1's rt provides all of them, the harness its own versions.
RT_FUNCS = {"_builtin_fabsf": "sa1g_fabsf", "_builtin_sqrtf": "sa1g_sqrtf", "_builtin_fsrra": "sa1g_fsrra",
            "_builtin_fipr": "sa1g_fipr", "_builtin_ftrv": "sa1g_ftrv", "_builtin_fsca": "sa1g_fsca",
            "_builtin_set_cr": "sa1g_set_cr", "_builtin_get_cr": "sa1g_get_cr",
            "_builtin_set_imask": "sa1g_set_imask", "_builtin_get_imask": "sa1g_get_imask",
            "_builtin_set_gbr": "sa1g_set_gbr", "_builtin_get_gbr": "sa1g_get_gbr",
            "njFraction": "njFraction", "sa1_tmu_tcnt1": "sa1_tmu_tcnt1", "UNK_PcWrite": "UNK_PcWrite",
            # game-detected fatal state (PC_ONLY in CCL_Init, src/1st_read/code_8C02F27C.c): libsa1's world
            # core (LIB/src/world/sa1_world.c) and the harness (harness/src/pc_runtime.c) define it
            "sa1g_fatal": "sa1g_fatal"}
RT_FUNC_NAMES = set(RT_FUNCS.values())
# R7: runtime data without a DC address that lives in HOST memory (native types, never lowered):
# name -> replacement expression (None: the name itself). PC-only data defined in C (sa1_tmu in
# libsa1/src/rt/rt_misc.c) is not here: it is R4 pcdata exported across TUs.
RT_DATA = {"sa1_rt_xmtrx": None,                                  # the ticking world's XMTRX (rt.h)
           "sa1_rt_pad": None,                                    # the ticking world's RT_PAD[4] (rt_input.c)
           "sh4_fsca_q": None,                                    # FSCA quarter-wave table (rt_fsca_q.c)
           "xm": "(*(float (*)[16])sa1_rt_xmtrx)"}                # code_8C0103C4.c: "stands for the XF bank"

# R8: cross-TU prototype conflicts that are still open in the sources (being fixed by the canonical
# prototypes work); every other conflict fails the run. Keep this list as short as the sources allow.
R8_ALLOWED_CONFLICTS = [
    # empty after the canonical-prototype pass (build/verify/canonical_protos.json, 2026-10-20); since then:
    # E-102 effect system (code_8C0500A0.c, 2026-10-20) declares the 4th parameter of 8C050190 as the exec it
    # receives (sa1_gfn), its caller code_8C0D6960.c as Sint32: one 32-bit word in the same register on every
    # target (a 4-byte struct is passed like an int)
    "func_8C050190",
    # STG05 act setup (code_0C9122EE.c) passes -1 as the owner of the looping SE 8C0941FC (SA1_UPTR, u32), the
    # 1ST_READ callers a taskwk * (p); 8C0941FC is a boundary stub whose position read accepts arena pointers only
    # (LIB/gen/gen_stubs.py sa1g_stub_pos), so the raw -1 is never dereferenced
    "func_8C0941FC",
]

# Header-level decisions the design puts into the headers under SA1_GA. Until the headers implement
# them, pclower applies them to the preprocessed text and reports them ("header_changes_needed").
DROP_FIELDS = {("task", "thp")}                                  # D12: task is 0x30 under SA1_GA
ASSERT_PATCHES = [(re.compile(r"__builtin_offsetof\(task, thp\)"), None),
                  (re.compile(r"sizeof\(task\) == \(0x34\)(.*)\"0x34\""), "sizeof(task) == (0x30)\\1\"0x30\"")]
TYPEDEF_PATCHES = {"SA1_UPTR": "uint32_t"}                       # R6b: SA1_UPTR is Uint32 under SA1_GA
HEADER_CHANGES = [
    # done: include/task.h (D12) and include/common.h (SA1_UPTR, SA1_FMAC) implement SA1_GA themselves;
    # the patches above are no-ops on those headers
]
# limits the design accepts for now (reported in report.json "escapes")
KNOWN_LIMITS = [
    "Pointer equality compares decoded host pointers in ga64, so P0/P1/P2 aliases of one address "
    "compare equal (the SH-4 compares raw words; ga32 compares raw words)",
    "Variadic calls pass native pointers (fine for game-internal callees; printf-style rt functions "
    "must accept host pointers)",
    "Block-scope compound literals and other stack objects whose address is stored into guest memory "
    "rely on the host-stack window (segment 0x1D) that the world registers at tick entry",
]

# guest segments (design 2.1)
SEG_PCDATA = 0x18
PCDATA_GUEST = 0x98000000


# ------------------------------------------------------------------------------------------------
# types: a small parser/printer for clang's type spellings


class TypeParseError(Exception):
    pass


class Base(object):
    __slots__ = ("name", "quals")

    def __init__(self, name, quals=()):
        self.name = name
        self.quals = tuple(quals)


class Ptr(object):
    __slots__ = ("to", "quals")

    def __init__(self, to, quals=()):
        self.to = to
        self.quals = tuple(quals)


class Arr(object):
    __slots__ = ("of", "n")

    def __init__(self, of, n):
        self.of = of
        self.n = n


class Fn(object):
    __slots__ = ("ret", "params", "variadic", "noproto")

    def __init__(self, ret, params, variadic=False, noproto=False):
        self.ret = ret
        self.params = list(params)
        self.variadic = variadic
        self.noproto = noproto


QUALS = ("const", "volatile", "restrict", "__restrict")
ANON_RE = re.compile(r"\((?:unnamed|anonymous) (?:struct|union|enum) at .*?:\d+:\d+\)")
TOK_RE = re.compile(r"\s*(\.\.\.|[A-Za-z_]\w*|0[xX][0-9A-Fa-f]+|\d+|[*()\[\],])")


class _TP(object):
    def __init__(self, toks, src):
        self.t = toks
        self.i = 0
        self.src = src

    def peek(self, k=0):
        return self.t[self.i + k] if self.i + k < len(self.t) else None

    def take(self):
        v = self.peek()
        self.i += 1
        return v

    def expect(self, x):
        if self.take() != x:
            raise TypeParseError(self.src)

    def typename(self):
        words = []
        while self.peek() is not None and re.match(r"[A-Za-z_]", self.peek()):
            words.append(self.take())
        if not words:
            raise TypeParseError(self.src)
        quals = [w for w in words if w in QUALS]
        name = " ".join(w for w in words if w not in QUALS)
        return self.declarator()(Base(name, quals))

    def declarator(self):
        ptrs = []
        while self.peek() == "*":
            self.take()
            q = []
            while self.peek() in QUALS:
                q.append(self.take())
            ptrs.append(q)
        inner = None
        if self.peek() == "(" and self.peek(1) in ("*", "(", "["):
            self.take()
            inner = self.declarator()
            self.expect(")")
        sufs = []
        while self.peek() in ("[", "("):
            if self.take() == "[":
                n = None
                if self.peek() != "]":
                    n = self.take()
                self.expect("]")
                sufs.append(("a", n))
            else:
                params, variadic, noproto = [], False, False
                if self.peek() == ")":
                    noproto = True
                else:
                    while True:
                        if self.peek() == "...":
                            self.take()
                            variadic = True
                        else:
                            params.append(self.typename())
                        if self.peek() == ",":
                            self.take()
                            continue
                        break
                self.expect(")")
                if len(params) == 1 and isinstance(params[0], Base) and params[0].name == "void" \
                        and not params[0].quals:
                    params = []
                sufs.append(("f", params, variadic, noproto))

        def apply(t):
            for q in ptrs:
                t = Ptr(t, q)
            for s in reversed(sufs):
                t = Arr(t, s[1]) if s[0] == "a" else Fn(t, s[1], s[2], s[3])
            return inner(t) if inner else t
        return apply


def decl(t, name=""):
    """C declaration of `name` with type t (abstract declarator when name is empty)"""
    if isinstance(t, Base):
        s = " ".join(list(t.quals) + [t.name])
        return s + (" " + name if name else "")
    if isinstance(t, Ptr):
        inner = "*" + " ".join(t.quals) + (" " if t.quals and name else "") + name
        if isinstance(t.to, (Arr, Fn)):
            inner = "(" + inner + ")"
        return decl(t.to, inner)
    if isinstance(t, Arr):
        return decl(t.of, name + "[" + (t.n or "") + "]")
    if isinstance(t, Fn):
        ps = ", ".join(decl(p) for p in t.params)
        if t.variadic:
            ps = ps + ", ..." if ps else "..."
        elif not ps and not t.noproto:
            ps = "void"
        return decl(t.ret, name + "(" + ps + ")")
    raise TypeError(t)


INT_NAMES = {"char", "signed char", "unsigned char", "short", "short int", "signed short", "unsigned short",
             "unsigned short int", "int", "signed int", "signed", "unsigned int", "unsigned", "long", "long int",
             "signed long", "unsigned long", "unsigned long int", "long long", "long long int",
             "unsigned long long", "unsigned long long int", "_Bool"}
FLOAT_NAMES = {"float", "double", "long double"}


class TypeEnv(object):
    def __init__(self):
        self.typedef_src = {}       # name -> clang spelling of the underlying type
        self.cache = {}
        self.anon = {}
        self.self_lowered = set()   # typedef names whose declaration pclower rewrote (fnptr typedefs)
        self.fnptr_typedefs = set()
        self.rec_classes = {}       # record spelling (struct X / typedef name) -> {"sa1_gp", "sa1_gfn"}

    def parse(self, s):
        t = self.cache.get(s)
        if t is None and ("vector_size" in s or "ext_vector_type" in s):
            # SIMD types (sh4_fpmodel.h's sqrtss helpers): opaque values, never pointers
            t = Base("__sa1_vector")
            self.cache[s] = t
        if t is None:
            s2 = ANON_RE.sub(lambda m: self.anon.setdefault(m.group(0), "__sa1_anon_%d" % len(self.anon)), s)
            toks, pos = [], 0
            while pos < len(s2):
                m = TOK_RE.match(s2, pos)
                if not m:
                    if not s2[pos:].strip():
                        break
                    raise TypeParseError(s)
                toks.append(m.group(1))
                pos = m.end()
            p = _TP(toks, s)
            t = p.typename()
            if p.i != len(toks):
                raise TypeParseError(s)
            self.cache[s] = t
        return t

    def resolve(self, t):
        n = 0
        while isinstance(t, Base) and t.name in self.typedef_src and n < 64:
            u = self.parse(self.typedef_src[t.name])
            if t.quals:
                if isinstance(u, Base):
                    u = Base(u.name, tuple(u.quals) + t.quals)
                elif isinstance(u, Ptr):
                    u = Ptr(u.to, tuple(u.quals) + t.quals)
            t = u
            n += 1
        return t

    def kind(self, t):
        r = self.resolve(t)
        if isinstance(r, Ptr):
            return "fnptr" if isinstance(self.resolve(r.to), Fn) else "dataptr"
        if isinstance(r, Arr):
            return "arr"
        if isinstance(r, Fn):
            return "fn"
        nm = r.name
        if nm in ("sa1_gp", "sa1_gfn"):
            return nm
        if nm in FLOAT_NAMES:
            return "float"
        if nm == "void":
            return "void"
        if nm in INT_NAMES or nm.startswith("enum "):
            return "int"
        return "record"

    def int_info(self, t):
        """(unsigned, width) of an integer type"""
        r = self.resolve(t)
        nm = r.name if isinstance(r, Base) else ""
        uns = nm.startswith("unsigned") or nm == "_Bool"
        width = 64 if "long long" in nm else 32
        return uns, width

    def same(self, a, b):
        return decl(a) == decl(b)

    def mem_class(self, t, depth=0):
        """which guest pointer kinds a lowered memory type holds (symtab class column)"""
        if depth > 24:
            return ""
        if isinstance(t, Base):
            if t.name == "sa1_gp":
                return "sa1_gp"
            if t.name == "sa1_gfn" or t.name in self.fnptr_typedefs:
                return "sa1_gfn"
            if t.name in self.rec_classes:
                return " ".join(sorted(self.rec_classes[t.name]))
            try:
                r = self.resolve(t)
            except TypeParseError:
                return ""
            return self.mem_class(r, depth + 1) if r is not t else ""
        if isinstance(t, Arr):
            return self.mem_class(t.of, depth + 1)
        return ""

    def lower_mem(self, t):
        """type of an object stored in guest memory"""
        if isinstance(t, Base):
            if t.name in self.self_lowered:
                return t
            r = self.resolve(t)
            if r is t:
                return t
            lr = self.lower_mem(r)
            return t if self.same(lr, r) else lr
        if isinstance(t, Ptr):
            return Base("sa1_gfn" if self.kind(t) == "fnptr" else "sa1_gp")
        if isinstance(t, Arr):
            lo = self.lower_mem(t.of)
            return t if self.same(lo, t.of) else Arr(lo, t.n)
        return t

    def lower_val(self, t):
        """type of a native value (local, parameter, return value, cast)"""
        if isinstance(t, Base):
            if t.name in self.self_lowered:
                return t
            r = self.resolve(t)
            if r is t:
                return t
            lr = self.lower_val(r)
            return t if self.same(lr, r) else lr
        if isinstance(t, Ptr):
            if self.kind(t) == "fnptr":
                return Base("sa1_gfn")
            lo = self.lower_mem(t.to)
            return t if self.same(lo, t.to) else Ptr(lo, t.quals)
        if isinstance(t, Arr):
            lo = self.lower_mem(t.of)
            return t if self.same(lo, t.of) else Arr(lo, t.n)
        if isinstance(t, Fn):
            return Fn(self.lower_val(t.ret), [self.lower_val(p) for p in t.params], t.variadic, t.noproto)
        return t

    def canon(self, t, mem=False):
        """lowered, typedef-free, qualifier-free spelling (cross-TU prototype comparison)"""
        if isinstance(t, Base):
            if t.name in self.fnptr_typedefs:
                return "sa1_gfn"
            r = self.resolve(t)
            if r is not t:
                return self.canon(r, mem)
            return t.name
        if isinstance(t, Ptr):
            if self.kind(t) == "fnptr":
                return "sa1_gfn"
            return "sa1_gp" if mem else self.canon(t.to, True) + "*"
        if isinstance(t, Arr):
            return "%s[%s]" % (self.canon(t.of, True), t.n or "")
        if isinstance(t, Fn):
            ps = [self.canon(p) for p in t.params] + (["..."] if t.variadic else [])
            return "%s(%s)" % (self.canon(t.ret), ",".join(ps))
        raise TypeError(t)

    def abi(self, t):
        """ABI class of a value type: v i8 u8 i16 u16 i32 u32 i64 f32 f64 p gfn s"""
        k = self.kind(t)
        if k in ("dataptr", "arr"):
            return "p"
        if k in ("fnptr", "sa1_gfn"):
            return "gfn"
        if k == "void":
            return "v"
        if k == "float":
            return "f64" if self.resolve(t).name != "float" else "f32"
        if k == "int":
            r = self.resolve(t)
            nm = r.name
            u = "u" if nm.startswith("unsigned") or nm == "_Bool" else "i"
            if "char" in nm or nm == "_Bool":
                return u + "8"
            if "short" in nm:
                return u + "16"
            if "long long" in nm:
                return u + "64"
            return u + "32"
        return "s"

    def sig(self, fn):
        return "%s:%s%s" % (self.abi(fn.ret), ",".join(self.abi(p) for p in fn.params),
                            ",..." if fn.variadic else "")


def sig_id(sig):
    return zlib.crc32(sig.encode()) | 1     # never 0 (0 = unchecked)


def has_anon(s):
    return "__sa1_anon_" in s


# ------------------------------------------------------------------------------------------------
# preprocessing


MARK_RE = re.compile(r'^#[ \t]+(\d+)[ \t]+"((?:[^"\\]|\\.)*)"((?:[ \t]+\d+)*)[ \t]*\r?$', re.M)


def pp_flags(extra_inc=()):
    pcf = os.path.join(TOOLS, "pc_cflags.txt")
    keep = []
    if os.path.exists(pcf):
        toks = shlex.split(open(pcf, encoding="utf-8").read(), comments=True)
        i = 0
        while i < len(toks):
            t = toks[i]
            if t in ("-include", "-I", "-D", "-U") and i + 1 < len(toks):
                keep += [t, toks[i + 1]]
                i += 2
                continue
            if t.startswith(("-D", "-U", "-I", "-std=")) or t in ("-fsigned-char", "-funsigned-char"):
                keep.append(t)
            i += 1
        src = "tools/pc_cflags.txt"
    else:
        # harness/build.py --pc --non-matching: -DNON_MATCHING -DTARGET_PC -I include -I src
        # -I src/1st_read (its -include harness_pre.h is harness-only and not used for libsa1)
        src = "harness/build.py --pc --non-matching (tools/pc_cflags.txt does not exist)"
    for d in ("-DTARGET_PC", "-DNON_MATCHING", "-DSA1_GA"):
        if d not in keep:
            keep.append(d)
    keep += ["-I", os.path.join(ROOT, "include"), "-I", os.path.join(ROOT, "src")]
    for d in extra_inc:
        keep += ["-I", d]
    return keep, src


def unescape_c(s):
    return re.sub(r"\\(.)", r"\1", s)


def header_name(path):
    parts = re.split(r"[\\/]+", path)
    for i in range(len(parts) - 2, -1, -1):
        if parts[i] == "include" or parts[i].endswith("-any") or parts[i].endswith("-gnu"):
            return "/".join(parts[i + 1:])
    return parts[-1]


def fn_addr(name, syms):
    if name in R7_ADDR:
        return R7_ADDR[name]
    a = gn.addr_of(name, syms)
    if a is None:
        # overlay code named after its load address (B_CHAOS7_exec_0C900480); 1ST_READ fillers
        # (tu_gap_8C0BBDD6) are not functions
        m = re.match(r"^(?!tu_gap)[A-Za-z]\w*_(0C[0-9A-Fa-f]{6})$", name)
        if m:
            a = int(m.group(1), 16)
    return a


# ------------------------------------------------------------------------------------------------
# images (design 2.5; see the docstring)
BASE_IMAGE = "1ST_READ"
OVERLAY_SRC = os.path.join(ROOT, "src", "overlays")
_IMAGE_INFO = {}
_IMAGE_SYMS = {}


def image_of(path):
    """image of a TU: the upper-cased overlay directory (STG01, TIKAL_PROG) or BASE_IMAGE"""
    p = os.path.normcase(os.path.abspath(path))
    o = os.path.normcase(os.path.abspath(OVERLAY_SRC)) + os.sep
    if p.startswith(o):
        return p[len(o):].split(os.sep)[0].upper()
    return BASE_IMAGE


def unit_base(path):
    """name of a TU's lowered file: overlay TUs carry their directory (two overlays may both have code_0C900000.c)"""
    img = image_of(path)
    b = os.path.basename(path)
    return b if img == BASE_IMAGE else "%s__%s" % (img.lower(), b)


def image_name(img, name):
    """the C name of a function of an overlay image"""
    return name if img == BASE_IMAGE else "%s__%s" % (img, name)


def image_info(img):
    """config/overlays/<dir>.yaml: {"name", "lo", "hi" (code range, addr & 0x1FFFFFFF), "prs", "prs_sha1"}"""
    if img not in _IMAGE_INFO:
        path = os.path.join(ROOT, "config", "overlays", img.lower() + ".yaml")
        info = {"name": img, "lo": 0, "hi": 0, "prs": "", "prs_sha1": "", "error": None}
        try:
            text = open(path, encoding="utf-8").read()
        except OSError as ex:
            info["error"] = "no overlay config %s (%s)" % (os.path.relpath(path, ROOT), ex)
            text = ""
        rs = [(int(a, 16) & 0x1FFFFFFF, int(b, 16) & 0x1FFFFFFF) for a, b in re.findall(
            r"^\s*-\s*\[\s*0x([0-9A-Fa-f]+)\s*,\s*0x([0-9A-Fa-f]+)\s*\]", text, re.M)]
        m = re.search(r"^name:\s*(\S+)", text, re.M)
        if m and m.group(1).upper() != img:
            info["error"] = "%s names the overlay %s" % (os.path.relpath(path, ROOT), m.group(1))
        m = re.search(r"^prs:\s*(\S+)", text, re.M)
        info["prs"] = os.path.basename(m.group(1)).upper() if m else ""
        m = re.search(r"^prs_sha1:\s*([0-9a-fA-F]{40})", text, re.M)
        info["prs_sha1"] = m.group(1).lower() if m else ""
        if rs:
            info["lo"], info["hi"] = min(a for a, b in rs), max(b for a, b in rs)
        elif text:
            info["error"] = "%s has no code_ranges" % os.path.relpath(path, ROOT)
        if text and not info["prs_sha1"]:
            info["error"] = "%s has no prs_sha1" % os.path.relpath(path, ROOT)
        _IMAGE_INFO[img] = info
    return _IMAGE_INFO[img]


def image_syms(img, base_syms):
    """symbol table of a TU of image img: the overlay's own build/<IMAGE>.symbols.txt first"""
    if img == BASE_IMAGE:
        return base_syms
    if img not in _IMAGE_SYMS:
        own = {}
        path = os.path.join(ROOT, "build", "%s.symbols.txt" % img)
        if os.path.exists(path):
            with open(path) as f:
                for line in f:
                    m = re.match(r"\s*([A-Za-z0-9_$.]+)\s*=\s*0x([0-9A-Fa-f]+)", line)
                    if m:
                        own.setdefault(m.group(1), int(m.group(2), 16))
        _IMAGE_SYMS[img] = collections.ChainMap(own, base_syms)
    return _IMAGE_SYMS[img]


def in_image(img, addr):
    """addr lies in the code range of overlay image img"""
    if img == BASE_IMAGE or addr is None:
        return False
    info = image_info(img)
    return info["lo"] <= (addr & 0x1FFFFFFF) < info["hi"]


def zig_versions():
    v = subprocess.run([sys.executable, "-m", "ziglang", "version"], capture_output=True, text=True).stdout.strip()
    c = subprocess.run(gn.zig_cc() + ["--version"], capture_output=True, text=True).stdout.strip().splitlines()
    return v, (c[0] if c else "")


def walk(n, parent=None):
    yield n, parent
    for c in n.get("inner", []) or []:
        if isinstance(c, dict) and c.get("kind"):
            yield from walk(c, n)


# ------------------------------------------------------------------------------------------------
# one translation unit


class Global(object):
    """an extern data symbol: kind "arena" (R3, a DC address) or "pcdx" (R4 pcdata of another TU)"""

    def __init__(self, name, addr, ltype, tdname, kind="arena"):
        self.name = name
        self.addr = addr
        self.ltype = ltype
        self.tdname = tdname
        self.kind = kind


class PcdObj(object):
    """an R4 pcdata object of one TU: kind "var" (file scope), "static" (static local), "string" """

    def __init__(self, k, kind, name, t, lt, node=None, init=None, export=False, text=None, size=None):
        self.k = k
        self.kind = kind
        self.name = name
        self.t = t
        self.lt = lt
        self.node = node
        self.init = init
        self.export = export
        self.text = text            # string literal source text
        self.size = size            # string literal size (bytes incl. NUL)


def sanitize(base):
    return re.sub(r"\W", "_", os.path.splitext(base)[0])


class Unit(object):
    def __init__(self, path, ctx):
        self.path = os.path.abspath(path)
        self.image = image_of(path)
        self.base = unit_base(path)
        self.uname = sanitize(self.base)
        self.ctx = ctx
        self.errors = []
        self.counts = collections.Counter()
        self.edits = []
        self.global_refs = collections.Counter()
        self.globals = {}           # VarDecl id -> Global (arena and cross-TU pcdata references)
        self.gtypedefs = {}         # (name, lowered decl) -> typedef name
        self.rtdata = {}            # VarDecl id -> name (R7 runtime data in host memory)
        self.rtdata_refs = collections.Counter()
        self.pcd = []               # R4 pcdata objects of this TU (index = sa1g_pcdo[] slot)
        self.pcd_by_id = {}         # VarDecl id -> pcdata index
        self.pcd_str = {}           # string literal text -> pcdata index
        self.pcdx_names = set()     # cross-TU pcdata variables this TU references
        self.dc_defined = set()     # C definitions of DC-address symbols, lowered as arena globals (R3)
        self.escaped = set()        # R2b locals (VarDecl ids)
        self.esc_params = {}        # R2b parameters: ParmVarDecl id -> name
        self.escapes = []           # sites the static rules cannot make exact (report.json "escapes")
        self.fn_defined = {}        # name -> {"static", "canon", "sig"}
        self.fn_declared = {}       # name -> ABI signature of the lowered declaration (non-static)
        self.fn_canon = {}          # name -> canonical lowered spelling
        self.fn_sig = {}            # name -> ABI signature of its declaration
        self.fn_noproto = set()     # declared only without a prototype
        self.fn_called = set()
        self.fn_addr_taken = set()
        self.rt_called = set()      # R7 runtime functions referenced
        self.statics = set()
        self.implicit_fns = set()
        self.rw_text = {}
        self.env = TypeEnv()
        self.patches = []
        self.skipped_bodies = []
        self.records = {}           # RecordDecl id -> node
        self.typedef_record = {}    # typedef name -> RecordDecl id
        self.local_records = []     # (C spelling, [fields]) of file-scope records defined in the .c file
        self.layout_asserts = ""    # per-TU R9 asserts (records of the .c file)
        self.tail = []              # text appended after the lowered TU (pcdata unit, asserts)
        self.tmpn = 0
        self.sigs = {}              # signature id -> ABI signature of the indirect calls
        self.implicit_decls = set()  # undeclared functions called (C89 implicit int f())
        self.r7_defined = set()     # R7 names this TU defines (renamed to sa1g_*)
        self.markers, self.mark_starts, self.nl = [], [], []

    @property
    def syms(self):
        """the symbols this TU resolves names with (its image's own first)"""
        return image_syms(self.image, self.ctx.syms)

    # -------------------------------------------------------------- diagnostics
    def where(self, n):
        if n is None:
            return self.base
        r = self.rng(n)
        off = r[0] if r else (n.get("loc") or {}).get("offset")
        if off is None:
            return self.base
        i = bisect.bisect_right(self.mark_starts, off) - 1
        if i < 0:
            return self.base
        st, en, ln, fesc, f, fl = self.markers[i]
        line = ln + bisect.bisect_left(self.nl, off) - bisect.bisect_left(self.nl, en) - 1
        if f.startswith("<"):
            return "%s:%d" % (f, line)
        return "%s:%d" % (os.path.relpath(f, ROOT).replace("\\", "/"), line)

    def err(self, rule, msg, n=None):
        self.errors.append({"rule": rule, "msg": msg, "where": self.where(n)})

    def escape(self, kind, msg, n=None):
        self.escapes.append({"kind": kind, "msg": msg[:200], "where": self.where(n)})

    # -------------------------------------------------------------- pipeline
    def run(self):
        if not self.preprocess():
            return
        self.scan_markers()
        self.collect()
        self.lower_toplevel()
        self.lower_bodies()
        self.emit_pcdata()
        self.local_layout()
        self.lowered = self.apply_edits()
        self.ast = None             # the JSON AST of one TU is ~30 MB of Python objects
        self.rw_text = {}
        self.records = {}

    def preprocess(self):
        flags, self.flag_source = pp_flags(self.ctx.extra_inc)
        cmd = gn.zig_cc() + ["-target", PARSE_TARGET, "-E"] + flags + ["-I", os.path.dirname(self.path), self.path]
        r = subprocess.run(cmd, capture_output=True)
        if r.returncode:
            self.err("R8", "preprocessing failed: " + r.stderr.decode("latin-1")[-1500:])
            return False
        raw = r.stdout.decode("latin-1").replace("\r\n", "\n")
        self.text, changes = gn.rewrite_floats(raw)
        hexf = re.compile(r"\(-?0x[0-9a-fA-F.]+p[-+]?\d+f\)")
        self.counts["R6_float_literals"] = len(hexf.findall(self.text)) - len(hexf.findall(raw))
        self.counts["R6_float_literals_double_or_inexact"] = len(changes)
        self.counts["R6_float_literals_rounding_differs"] = sum(1 for c in changes if c["differs"])
        work = os.path.join(self.ctx.out, "work")
        os.makedirs(work, exist_ok=True)
        self.pp_path = os.path.join(work, self.uname + ".pp.c")
        with open(self.pp_path, "w", encoding="latin-1", newline="") as f:
            f.write(self.text)
        ast, stderr = parse_ast(self.pp_path)
        if ast is None:
            self.err("R8", "does not compile (x86, preprocessed): " + stderr[-1500:])
            return False
        self.ast = ast
        self.nl = [m.start() for m in re.finditer("\n", self.text)]
        return True

    def scan_markers(self):
        text = self.text
        self.markers = []
        for m in MARK_RE.finditer(text):
            f = unescape_c(m.group(2))
            flags = [int(x) for x in m.group(3).split()]
            self.markers.append((m.start(), m.end(), int(m.group(1)), m.group(2), f, flags))
        self.mark_starts = [m[0] for m in self.markers]
        roots = [os.path.normcase(os.path.abspath(ROOT))] + \
            [os.path.normcase(os.path.abspath(d)) for d in self.ctx.extra_inc]
        sysf = {}

        def is_system(f):
            if f not in sysf:
                nf = os.path.normcase(os.path.abspath(f))
                sysf[f] = f.startswith("<") or not any(nf.startswith(r) for r in roots)
            return sysf[f]
        self.removed = []
        i, n = 0, len(self.markers)
        while i < n:
            st, en, ln, fesc, f, fl = self.markers[i]
            if is_system(f):
                j = i
                while j < n and is_system(self.markers[j][4]):
                    j += 1
                end = self.markers[j][0] if j < n else len(text)
                repl = "#include <%s>\n" % header_name(f) if (1 in fl and not f.startswith("<")) else ""
                self.removed.append((st, end, repl))
                i = j
            else:
                i += 1
        self.removed_starts = [r[0] for r in self.removed]
        for st, end, repl in self.removed:
            self.edits.append((st, end, repl, "system"))

    def in_removed(self, off):
        i = bisect.bisect_right(self.removed_starts, off) - 1
        return i >= 0 and self.removed[i][0] <= off < self.removed[i][1]

    def rng(self, n):
        r = n.get("range") or {}
        b, e = r.get("begin") or {}, r.get("end") or {}
        if "offset" not in b or "offset" not in e:
            return None
        return b["offset"], e["offset"] + e.get("tokLen", 0)

    def is_project(self, n):
        r = self.rng(n)
        off = r[0] if r else (n.get("loc") or {}).get("offset")
        return off is not None and not self.in_removed(off)

    def in_this_file(self, n):
        w = self.where(n)
        return w.split(":")[0] == os.path.relpath(self.path, ROOT).replace("\\", "/")

    def ty(self, n):
        return self.env.parse(n["type"]["qualType"])

    def kind_of(self, n):
        t = (n.get("type") or {}).get("qualType")
        if t is None:
            return None
        try:
            return self.env.kind(self.env.parse(t))
        except TypeParseError:
            self.err("R8", "cannot parse type %r" % t, n)
            return None

    def keep_nl(self, b, e, s):
        """s replaces text[b:e]: keep the line count so the #line mapping stays exact"""
        k = self.text.count("\n", b, e) - s.count("\n")
        if k > 0:
            return s + "\n" * k
        if k < 0:
            return s.replace("\n", " ")
        return s

    def tmp(self, stem):
        self.tmpn += 1
        return "sa1g_%s%d" % (stem, self.tmpn)

    # -------------------------------------------------------------- collection
    def is_def(self, n):
        return n.get("storageClass") != "extern" or bool(n.get("init"))

    def collect(self):
        env = self.env
        explicit, refs = set(), set()
        for n, parent in walk(self.ast):
            k = n["kind"]
            if k == "TypedefDecl":
                env.typedef_src[n["name"]] = n["type"]["qualType"]
                for c, p in walk(n):
                    d = c.get("ownedTagDecl") or (c.get("decl") if c.get("kind") == "RecordType" else None)
                    if d and d.get("kind") == "RecordDecl":
                        self.typedef_record.setdefault(n["name"], d["id"])
                        break
            elif k == "RecordDecl" and n.get("completeDefinition"):
                self.records[n["id"]] = n
            elif k == "FunctionDecl" and n.get("isImplicit") and not n.get("name", "").startswith("__builtin") \
                    and (n.get("isUsed") or n.get("isReferenced")):
                self.implicit_fns.add(n["name"])
            elif k == "FunctionDecl" and not n.get("isImplicit"):
                explicit.add(n.get("name"))
            elif k == "DeclRefExpr" and (n.get("referencedDecl") or {}).get("kind") == "FunctionDecl":
                refs.add(n["referencedDecl"]["name"])
        self.implicit_fns |= set(x for x in refs if not x.startswith("__builtin"))
        self.implicit_fns -= explicit
        self.record_classes()
        # function pointer typedefs are lowered in place (typedef sa1_gfn Name)
        for name, src in list(env.typedef_src.items()):
            try:
                if env.kind(env.parse(src)) == "fnptr":
                    env.fnptr_typedefs.add(name)
            except TypeParseError:
                pass
        for n, parent in walk(self.ast):
            if n["kind"] == "TypedefDecl" and n["name"] in env.fnptr_typedefs:
                if self.is_project(n):
                    env.self_lowered.add(n["name"])
                else:
                    env.fnptr_typedefs.discard(n["name"])
        # file-scope data, by name (R3 / R4 / R7)
        top = [n for n in self.ast.get("inner", []) if n.get("kind") == "VarDecl" and self.is_project(n)]
        by_name = collections.OrderedDict()
        for n in top:
            by_name.setdefault(n["name"], []).append(n)
        self.file_pcd = {}          # name -> pcdata index of a file-scope definition
        for name, decls in by_name.items():
            self.classify_data(name, decls, True)
        # local declarations, escapes, functions
        top_ids = set(id(x) for x in self.ast.get("inner", []))
        for n, parent in walk(self.ast):
            k = n["kind"]
            if k == "VarDecl" and self.is_project(n) and id(n) not in top_ids:
                sc = n.get("storageClass")
                if sc == "extern" and not n.get("init"):
                    self.classify_data(n["name"], [n], False)
                elif sc == "static":
                    if gn.addr_of(n["name"], self.syms, True) is not None:
                        self.err("R3", "static local %s is named like a DC symbol" % n["name"], n)
                        continue
                    self.new_pcd("static", n["name"], n, n, export=False)
            elif k == "UnaryOperator" and n.get("opcode") == "&" and self.is_project(n):
                c = self.strip(n["inner"][0], ())
                if c["kind"] == "DeclRefExpr":
                    ref = c.get("referencedDecl") or {}
                    isptr = self.kind_of(c) == "dataptr"
                    if ref.get("kind") == "VarDecl" and isptr:
                        self.escaped.add(ref["id"])
                    elif ref.get("kind") == "ParmVarDecl" and isptr:
                        self.esc_params[ref["id"]] = ref["name"]
            elif k == "CompoundLiteralExpr" and self.is_project(n) and parent is not None \
                    and self.file_scope_expr(n):
                self.err("R4", "file-scope compound literal (needs its own pcdata object; not supported)", n)
            elif k == "FunctionDecl" and self.is_project(n) and not n.get("isImplicit") \
                    and not n.get("name", "").startswith("__builtin"):
                name = n["name"]
                if n.get("storageClass") == "static":
                    self.statics.add(name)
                try:
                    ft = env.resolve(self.ty(n))
                except TypeParseError:
                    self.err("R8", "cannot parse the type of %s" % name, n)
                    continue
                if not isinstance(ft, Fn):
                    continue
                if ft.noproto:
                    self.fn_noproto.add(name)
                if n.get("storageClass") != "static" and not ft.noproto and name not in R7_RENAMES \
                        and name not in RT_FUNCS:
                    # unprototyped declarations (f()) say nothing about the parameters; the R7 / rt names
                    # are called through their sa1/guest.h declarations, whatever a TU declares
                    self.fn_declared[name] = env.sig(env.lower_val(ft))
                    self.fn_canon[name] = env.canon(ft)
                    self.fn_sig[name] = env.sig(env.lower_val(ft))
                if any(c.get("kind") == "CompoundStmt" for c in n.get("inner", [])):
                    if name in R7_RENAMES:
                        self.r7_defined.add(name)
                        name = R7_RENAMES[name]
                    self.fn_defined[name] = {"static": n.get("storageClass") == "static",
                                             "canon": env.canon(ft), "sig": env.sig(env.lower_val(ft)),
                                             "variadic": ft.variadic}
        for name in self.statics:
            if name in self.fn_defined:
                self.fn_defined[name]["static"] = True
            self.fn_declared.pop(name, None)
        for i in list(self.escaped):
            if i in self.globals or i in self.pcd_by_id or i in self.rtdata:
                self.escaped.discard(i)
        self.counts["R2b_escaped_locals"] = len(self.escaped)
        self.counts["R2b_escaped_params"] = len(self.esc_params)

    def record_classes(self):
        """records that hold guest pointers once lowered (for the symtab class column)"""
        env = self.env
        spell = collections.defaultdict(list)
        for rid, r in self.records.items():
            if r.get("name"):
                spell[rid].append("%s %s" % (r.get("tagUsed", "struct"), r["name"]))
        for td, rid in self.typedef_record.items():
            spell[rid].append(td)
        for i in range(8):
            changed = False
            for rid, r in self.records.items():
                cls = set()
                for f in r.get("inner", []):
                    if f.get("kind") != "FieldDecl":
                        continue
                    try:
                        t = env.parse(f["type"]["qualType"])
                        k = env.kind(t)
                    except TypeParseError:
                        continue
                    if k == "dataptr":
                        cls.add("sa1_gp")
                    elif k == "fnptr":
                        cls.add("sa1_gfn")
                    else:
                        c = env.mem_class(t)
                        cls |= set(c.split())
                for sp in spell.get(rid, []):
                    if cls and env.rec_classes.get(sp) != cls:
                        env.rec_classes[sp] = cls
                        changed = True
            if not changed:
                break

    def file_scope_expr(self, n):
        """n lies in the initializer of a file-scope declaration (not inside a function body)"""
        r = self.rng(n)
        if r is None:
            return False
        for top in self.ast.get("inner", []):
            tr = self.rng(top)
            if tr and tr[0] <= r[0] < tr[1]:
                return top.get("kind") == "VarDecl"
        return False

    def classify_data(self, name, decls, file_scope):
        """R3 arena global / R7 runtime data / R4 pcdata (definition here) / cross-TU pcdata reference"""
        addr = gn.addr_of(name, self.syms, True)
        defs = [n for n in decls if self.is_def(n)]
        if addr is not None:
            for n in decls:
                self.add_global(n, addr)
            if defs:
                self.dc_defined.add(name)
            return
        if name in RT_DATA:
            for n in decls:
                self.rtdata[n["id"]] = name
            return
        if file_scope and defs:
            d = [n for n in defs if n.get("init")] or defs
            k = self.new_pcd("var", name, d[-1], d[-1], export=d[-1].get("storageClass") != "static")
            self.file_pcd[name] = k
            for n in decls:
                self.pcd_by_id[n["id"]] = k
            return
        if not file_scope and name in getattr(self, "file_pcd", {}):
            for n in decls:
                self.pcd_by_id[n["id"]] = self.file_pcd[name]
            return
        for n in decls:
            self.add_global(n, None)

    def new_pcd(self, kind, name, n, init_node, export=False):
        try:
            t = self.ty(n)
            lt = self.env.lower_mem(t)
        except TypeParseError:
            self.err("R8", "cannot parse the type of %s" % name, n)
            return -1
        if has_anon(decl(lt)):
            self.err("R4", "pcdata object %s has an anonymous type" % name, n)
        k = len(self.pcd)
        init = None
        if init_node is not None and init_node.get("init"):
            ini = [c for c in init_node.get("inner", []) if c.get("kind") and "valueCategory" in c]
            init = ini[0] if ini else None
        self.pcd.append(PcdObj(k, kind, name, t, lt, n, init, export))
        self.pcd_by_id[n["id"]] = k
        self.counts["R4_pcdata_" + ("statics" if kind == "static" else "vars")] += 1
        if export:
            self.counts["R4_pcdata_exported"] += 1
        return k

    def pcd_string(self, lit):
        txt = self.text[slice(*self.rng(lit))]
        k = self.pcd_str.get(txt)
        if k is None:
            m = re.match(r".*\[(\d+)\]$", lit["type"]["qualType"])
            size = int(m.group(1)) if m else 0
            k = len(self.pcd)
            self.pcd.append(PcdObj(k, "string", None, None, None, lit, text=txt, size=size))
            self.pcd_str[txt] = k
            self.counts["R4_pcdata_strings"] += 1
        self.counts["R4_string_refs"] += 1
        return "((char *)sa1_decode(sa1g_pcdo[%d]))" % k

    def add_global(self, n, addr):
        name = n["name"]
        try:
            lt = self.env.lower_mem(self.ty(n))
        except TypeParseError:
            self.err("R8", "cannot parse the type of %s" % name, n)
            return
        key = (name, decl(lt))
        td = self.gtypedefs.get(key)
        if td is None:
            k = sum(1 for kk in self.gtypedefs if kk[0] == name)
            td = ("sa1gt_%s" if addr is not None else "sa1gx_%s") % name + ("_%d" % k if k else "")
            self.gtypedefs[key] = td
        self.globals[n["id"]] = Global(name, addr, lt, td, "arena" if addr is not None else "pcdx")
        if addr is None:
            self.pcdx_names.add(name)

    # -------------------------------------------------------------- declarations
    def lower_toplevel(self):
        top = [n for n in self.ast.get("inner", []) if n.get("kind")]
        groups = collections.OrderedDict()
        for n in top:
            if not self.is_project(n):
                continue
            k = n["kind"]
            if k == "VarDecl":
                groups.setdefault(self.rng(n)[0], []).append(n)
            elif k == "TypedefDecl":
                self.lower_typedef(n)
            elif k == "StaticAssertDecl":
                self.lower_static_assert(n)
            elif k == "FunctionDecl":
                self.lower_function_decl(n)
        for n, parent in walk(self.ast):
            if n["kind"] == "RecordDecl" and n.get("completeDefinition") and self.is_project(n):
                if parent is not None and parent.get("kind") == "DeclStmt":
                    continue            # block scope: lowered with its DeclStmt (rw_declstmt)
                for e in self.record_field_edits(n):
                    self.edits.append(e)
                if parent is self.ast and (self.in_this_file(n) or getattr(self, "all_records", False)):
                    self.note_local_record(n)
        for b, g in groups.items():
            e = max(self.rng(v)[1] for v in g)
            s = self.data_group(g, b, e, True)
            if s is not None and s is not LOCAL:
                self.edits.append((b, e, s, "R3/R4"))

    def data_group(self, g, b, e, file_scope):
        """replacement text of a declaration group of data (None: keep it)"""
        cls = set()
        for v in g:
            if v["id"] in self.globals:
                cls.add("global")
            elif v["id"] in self.rtdata:
                cls.add("rt")
            elif v["id"] in self.pcd_by_id:
                cls.add("pcd")
            else:
                cls.add("local")
        if len(cls) != 1:
            self.err("R3", "declaration mixes data classes %s" % sorted(cls), g[0])
            return None
        c = cls.pop()
        if c == "global":
            self.counts["R3_extern_decls_replaced"] += len(g)
            return self.keep_nl(b, e, "; ".join(self.global_typedef(v) for v in g))
        if c == "rt":
            self.counts["R7_rt_data_decls"] += len(g)
            return None
        if c == "pcd":
            self.counts["R4_pcdata_decls_moved"] += len(g)
            return self.keep_nl(b, e, "typedef int sa1g_pcdrm_%d" % b if file_scope else "")
        return LOCAL

    def global_typedef(self, v):
        g = self.globals[v["id"]]
        s = "typedef " + decl(g.ltype, g.tdname)
        if g.kind == "pcdx":
            s += "; extern uint32_t sa1g_pcdv_%s" % g.name
        return s

    def lower_typedef(self, n):
        name = n["name"]
        b, e = self.rng(n)
        if name in TYPEDEF_PATCHES:
            self.edits.append((b, e, "typedef %s %s" % (TYPEDEF_PATCHES[name], name), "R6b"))
            self.patches.append("typedef %s -> %s" % (name, TYPEDEF_PATCHES[name]))
            return
        if name in self.env.self_lowered:
            self.edits.append((b, e, self.keep_nl(b, e, "typedef sa1_gfn %s" % name), "R1"))
            self.counts["R1_fnptr_typedefs"] += 1
            return
        try:
            t = self.env.parse(n["type"]["qualType"])
        except TypeParseError:
            return
        if isinstance(t, Arr) and not self.env.same(self.env.lower_mem(t), t):
            self.edits.append((b, e, self.keep_nl(b, e, "typedef " + decl(self.env.lower_mem(t), name)), "R1"))
            self.env.self_lowered.add(name)
            self.counts["R1_array_typedefs"] += 1

    def lower_static_assert(self, n):
        b, e = self.rng(n)
        s = self.text[b:e]
        self.counts["R9_static_asserts"] += 1
        for rx, repl in ASSERT_PATCHES:
            if rx.search(s):
                if repl is None:
                    self.edits.append((b, e, self.keep_nl(b, e, "typedef int sa1g_d12_dropped_%d" % b), "D12"))
                    self.patches.append("dropped _Static_assert " + rx.pattern)
                else:
                    self.edits.append((b, e, rx.sub(repl, s), "D12"))
                    self.patches.append("rewrote _Static_assert " + rx.pattern)
                self.counts["R9_static_asserts_patched"] += 1
                return

    def record_field_edits(self, n, block=False):
        """R1 edits of one record definition (pointer fields -> sa1_gp / sa1_gfn; D12 drops)"""
        env = self.env
        rname = n.get("name", "")
        fields = [c for c in n.get("inner", []) if c.get("kind") == "FieldDecl"]
        groups = collections.OrderedDict()
        for f in fields:
            r = self.rng(f)
            if r:
                groups.setdefault(r[0], []).append(f)
        has_ptr = False
        edits = []
        for b, g in groups.items():
            e = max(self.rng(f)[1] for f in g)
            out, changed = [], False
            for f in g:
                try:
                    t = self.ty(f)
                except TypeParseError:
                    self.err("R8", "cannot parse field type %s.%s" % (rname, f.get("name")), f)
                    continue
                if (rname, f.get("name")) in DROP_FIELDS:
                    changed = True
                    self.patches.append("dropped field %s.%s (D12)" % (rname, f["name"]))
                    continue
                lt = env.lower_mem(t)
                if not env.same(lt, t):
                    changed = True
                    has_ptr = True
                    k = env.kind(t)
                    self.counts["R1_fnptr_fields" if k == "fnptr" else "R1_dataptr_fields"] += 1
                    if f.get("isBitfield"):
                        self.err("R1", "pointer bit-field %s.%s" % (rname, f.get("name")), f)
                    if has_anon(decl(lt)):
                        self.err("R1", "anonymous type in field %s.%s" % (rname, f.get("name")), f)
                elif env.kind(t) == "fnptr":
                    # declared with a function pointer typedef: lowered by the typedef itself
                    has_ptr = True
                    self.counts["R1_fnptr_fields_by_typedef"] += 1
                out.append(decl(lt, f.get("name", "")))
            if not changed:
                continue
            if not out:
                # every declarator of the group dropped: remove up to and including the ';'
                semi = self.text.index(";", e)
                edits.append((b, semi + 1, self.keep_nl(b, semi + 1, ""), "D12"))
            else:
                edits.append((b, e, self.keep_nl(b, e, "; ".join(out)), "R1"))
        if has_ptr:
            self.counts["R1_records_with_pointers"] += 1
            if block:
                self.counts["R1_block_scope_records"] += 1
        return edits

    def note_local_record(self, n):
        """file-scope record defined in the .c file itself: per-TU layout asserts (R9)"""
        tag = n.get("tagUsed", "struct")
        if n.get("name"):
            sp = "%s %s" % (tag, n["name"])
        else:
            tds = [t for t, rid in self.typedef_record.items() if rid == n["id"]]
            if not tds:
                return
            sp = tds[0]
        fields = [f["name"] for f in n.get("inner", []) if f.get("kind") == "FieldDecl" and f.get("name")
                  and not f.get("isBitfield") and (n.get("name", ""), f["name"]) not in DROP_FIELDS]
        self.local_records.append((sp, fields))

    def fn_declarator(self, ret, name, params, variadic):
        ps = ", ".join(params)
        if variadic:
            ps = ps + ", ..." if ps else "..."
        elif not ps:
            ps = "void"
        return decl(ret, "%s(%s)" % (name, ps))

    def lower_function_decl(self, n):
        env = self.env
        name = n["name"]
        used = n.get("isUsed") or n.get("isReferenced") or \
            any(c.get("kind") == "CompoundStmt" for c in n.get("inner", []))
        try:
            ft = self.ty(n)
        except TypeParseError:
            return
        if not isinstance(ft, Fn):
            return
        b, e = self.rng(n)
        nameoff = (n.get("loc") or {}).get("offset")
        params = [p for p in n.get("inner", []) if p.get("kind") == "ParmVarDecl"]
        body = [c for c in n.get("inner", []) if c.get("kind") == "CompoundStmt"]
        lr = env.lower_val(ft.ret)
        ret_changed = not env.same(lr, ft.ret)
        if ret_changed and isinstance(ft.ret, Ptr) and isinstance(ft.ret.to, (Fn, Arr)):
            # T (*f(params))(...) / T (*f(params))[N]: the whole declarator is regenerated
            if nameoff is None:
                self.err("R1", "%s returns a function / array pointer (no location)" % name, n)
                return
            ps = []
            for p in params:
                t = self.ty(p)
                pn = p.get("name", "")
                if p["id"] in self.esc_params:
                    pn += "__sa1in"
                ps.append(decl(env.lower_val(t), pn))
            if ft.noproto and not params:
                ps = None
            prefix = self.text[b:nameoff]
            m = re.match(r"\s*((?:(?:static|extern|inline|__inline__|__inline)\s+)*)", prefix)
            end = self.rng(body[0])[0] if body else e
            text = m.group(1) + self.fn_declarator(lr, name, ps if ps is not None else [], ft.variadic)
            if ps is None:
                text = text.replace(name + "(void)", name + "()")
            self.edits.append((b, end, self.keep_nl(b, end, text + (" " if body else "")), "R1"))
            self.counts["R1_fnptr_return_declarators"] += 1
            if has_anon(text):
                self.err("R1", "anonymous type in the declarator of %s" % name, n)
            return
        if name in R7_RENAMES and body and nameoff is not None and \
                self.text.startswith(name, nameoff):
            # R7: the definition of a game C-library function gets its sa1/guest.h name
            self.edits.append((nameoff, nameoff + len(name), R7_RENAMES[name], "R7"))
            self.counts["R7_renamed_definitions"] += 1
        if ret_changed and nameoff is not None:
            prefix = self.text[b:nameoff]
            m = re.match(r"\s*((?:(?:static|extern|inline|__inline__|__inline)\s+)*)", prefix)
            self.edits.append((b, nameoff, self.keep_nl(b, nameoff, m.group(1) + decl(lr) + " "), "R1"))
            self.counts["R1_return_types"] += 1
        elif ret_changed and used:
            self.err("R1", "cannot rewrite the return type of %s" % name, n)
        for p in params:
            r = self.rng(p)
            if r is None:
                continue
            t = self.ty(p)
            lt = env.lower_val(t)
            esc = p["id"] in self.esc_params
            if not env.same(lt, t) or esc:
                if has_anon(decl(lt)):
                    self.err("R1", "anonymous type in a parameter of %s" % name, p)
                pn = p.get("name", "") + ("__sa1in" if esc and body else "")
                self.edits.append((r[0], r[1], self.keep_nl(r[0], r[1], decl(lt, pn)), "R1"))
                self.counts["R1_params"] += 1

    # -------------------------------------------------------------- function bodies
    def lower_bodies(self):
        for n in self.ast.get("inner", []):
            if n.get("kind") != "FunctionDecl" or not self.is_project(n):
                continue
            body = [c for c in n.get("inner", []) if c.get("kind") == "CompoundStmt"]
            if not body:
                continue
            if self.where(n).startswith("include/common.h"):
                # the PC helper layer of common.h (sa1_divls, sa1_ftrc, ...) is already host code
                self.skipped_bodies.append(n["name"])
                continue
            b, e = self.rng(body[0])
            t = self.rw(body[0], "stmt")
            if t is None:
                continue
            # R2b: address-taken pointer parameters live in sa1_gp storage
            pre = []
            for p in n.get("inner", []):
                if p.get("kind") == "ParmVarDecl" and p["id"] in self.esc_params:
                    pre.append("sa1_gp %s = { sa1_encode(%s__sa1in) };" % (p["name"], p["name"]))
                    self.counts["R2b_param_slots"] += 1
            if pre:
                t = t[:1] + " " + " ".join(pre) + t[1:]
            if t != self.text[b:e]:
                self.edits.append((b, e, t, "body"))

    def strip(self, n, casts=("NoOp", "BitCast", "LValueToRValue", "NullToPointer")):
        while True:
            k = n.get("kind")
            if k == "ParenExpr":
                n = n["inner"][0]
            elif k == "ImplicitCastExpr" and n.get("castKind") in casts:
                n = n["inner"][0]
            else:
                return n

    def is_mem(self, n):
        k = n.get("kind")
        if k == "ParenExpr":
            return self.is_mem(n["inner"][0])
        if k == "DeclRefExpr":
            ref = n.get("referencedDecl") or {}
            i = ref.get("id")
            if ref.get("kind") == "ParmVarDecl":
                return i in self.esc_params
            return ref.get("kind") == "VarDecl" and (i in self.globals or i in self.escaped or i in self.pcd_by_id)
        if k in ("MemberExpr", "ArraySubscriptExpr", "CompoundLiteralExpr"):
            return True
        if k == "UnaryOperator" and n.get("opcode") == "*":
            return True
        return False

    def is_null_const(self, n):
        while True:
            k = n.get("kind")
            if k == "ParenExpr":
                n = n["inner"][0]
            elif k == "ImplicitCastExpr":
                if n.get("castKind") == "NullToPointer":
                    return True
                if n.get("castKind") not in ("BitCast", "NoOp", "IntegralCast"):
                    return False
                n = n["inner"][0]
            elif k == "CStyleCastExpr":
                return n.get("castKind") == "NullToPointer"
            elif k == "IntegerLiteral":
                return n.get("value") == "0"
            else:
                return False

    def is_const(self, n):
        n = self.strip(n, ("IntegralCast", "NoOp"))
        k = n.get("kind")
        if k in ("IntegerLiteral", "CharacterLiteral", "UnaryExprOrTypeTraitExpr", "ConstantExpr"):
            return True
        if k == "UnaryOperator" and n.get("opcode") in ("-", "~", "+"):
            return self.is_const(n["inner"][0])
        if k == "BinaryOperator" and n.get("opcode") not in ("=", ","):
            return self.is_const(n["inner"][0]) and self.is_const(n["inner"][1])
        if k == "DeclRefExpr" and (n.get("referencedDecl") or {}).get("kind") == "EnumConstantDecl":
            return True
        return False

    def side_effect_free(self, n):
        for c, p in walk(n):
            k = c["kind"]
            if k in ("CallExpr", "CompoundAssignOperator", "StmtExpr"):
                return False
            if k == "UnaryOperator" and c.get("opcode") in ("++", "--"):
                return False
            if k == "BinaryOperator" and c.get("opcode") == "=":
                return False
        return True

    def stack_object(self, n):
        """n (an address expression) points at the host stack: &local, &param, local array, compound literal"""
        n = self.strip(n, ("NoOp", "BitCast", "ArrayToPointerDecay"))
        if n.get("kind") == "UnaryOperator" and n.get("opcode") == "&":
            n = self.strip(n["inner"][0], ())
            while True:
                k = n.get("kind")
                if k == "MemberExpr":
                    if n.get("isArrow"):
                        return False
                    n = self.strip(n["inner"][0], ())
                elif k == "ArraySubscriptExpr":
                    base = self.strip(n["inner"][0], ())
                    if base.get("kind") == "ImplicitCastExpr" and base.get("castKind") == "ArrayToPointerDecay":
                        n = self.strip(base["inner"][0], ())
                    else:
                        return False
                else:
                    break
        if n.get("kind") == "CompoundLiteralExpr":
            return True
        if n.get("kind") == "DeclRefExpr":
            ref = n.get("referencedDecl") or {}
            i = ref.get("id")
            if ref.get("kind") == "ParmVarDecl":
                return True
            if ref.get("kind") == "VarDecl" and i not in self.globals and i not in self.pcd_by_id                     and i not in self.rtdata:
                return True
        return False

    def fn_ref(self, n):
        n = self.strip(n, ("FunctionToPointerDecay", "BuiltinFnToFnPtr"))
        if n.get("kind") == "DeclRefExpr" and (n.get("referencedDecl") or {}).get("kind") == "FunctionDecl":
            return n["referencedDecl"]["name"]
        return None

    def fn_const(self, name, n):
        addr = fn_addr(name, self.syms)
        if addr is None:
            if name in self.statics:
                self.err("R5", "address of static function %s taken (no DC address)" % name, n)
            else:
                self.err("R5", "address of %s taken but it has no DC address" % name, n)
            addr = 0
        self.fn_addr_taken.add(name)
        self.counts["R5_fn_designators"] += 1
        return "SA1_GFN_C(0x%08Xu /* %s */)" % (addr, name)

    def roles(self, n, inner, role):
        k = n.get("kind")
        R = ["value"] * len(inner)
        isexpr = [isinstance(c, dict) and ("valueCategory" in c) for c in inner]
        if k == "CompoundStmt":
            R = ["stmt"] * len(inner)
        elif k == "CallExpr":
            if R:
                R[0] = "callee"
        elif k in ("IfStmt", "WhileStmt", "DoStmt", "SwitchStmt"):
            ci = isexpr.index(True) if True in isexpr else None
            if k == "DoStmt" and True in isexpr:
                ci = len(isexpr) - 1 - isexpr[::-1].index(True)
            R = [("cond" if k != "SwitchStmt" else "value") if i == ci else "stmt" for i in range(len(inner))]
        elif k == "ForStmt":
            R = (["stmt", "value", "cond", "stmt", "stmt"] + ["stmt"] * 8)[:len(inner)]
        elif k == "ConditionalOperator":
            if R:
                R[0] = "cond"
        elif k == "BinaryOperator" and n.get("opcode") in ("&&", "||"):
            R = ["cond"] * len(inner)
        elif k == "BinaryOperator" and n.get("opcode") == ",":
            R = ["stmt", role]
        elif k == "UnaryOperator" and n.get("opcode") == "!":
            R = ["cond"]
        elif k in ("LabelStmt", "DefaultStmt"):
            R = ["stmt"] * len(inner)
        elif k == "CaseStmt":
            R = ["value"] * len(inner)
            if inner:
                R[-1] = "stmt"
        elif k == "ParenExpr":
            R = [role] * len(inner)
        elif k == "CStyleCastExpr" and (n.get("type") or {}).get("qualType") == "void":
            R = ["stmt"] * len(inner)
        return R

    def rw(self, n, role="value"):
        r = self.rng(n)
        if r is None:
            return None
        b, e = r
        if n["kind"] == "DeclStmt":
            t = self.rw_declstmt(n)
            self.rw_text[n["id"]] = t
            return t
        inner = n.get("inner") or []
        roles = self.roles(n, inner, role)
        texts = {}
        for c, cr in zip(inner, roles):
            if not isinstance(c, dict) or not c.get("kind"):
                continue
            rr = self.rng(c)
            if rr is None or rr[0] < b or rr[1] > e:
                continue
            ct = self.rw(c, cr)
            if ct is None:
                continue
            if cr == "cond" and self.kind_of(c) in ("fnptr", "sa1_gfn"):
                ct = "SA1_GFN_NZ(%s)" % ct
                self.counts["R5_fnptr_truth"] += 1
            texts[c["id"]] = ct
        kids = [c for c in inner if isinstance(c, dict) and c.get("kind")]
        t = self.transform(n, b, e, kids, texts, role)
        self.rw_text[n["id"]] = t
        return t

    def splice(self, b, e, kids, texts, override=None):
        parts = []
        for c in kids:
            if c["id"] in texts or (override and c["id"] in override):
                r = self.rng(c)
                if r is None:
                    continue
                cb, ce = r
                t = (override or {}).get(c["id"], texts.get(c["id"]))
                parts.append((cb, ce, t))
        parts.sort(key=lambda x: (x[0], -x[1]))
        out, pos = [], b
        for cb, ce, t in parts:
            if cb < pos:
                if t != self.text[cb:ce]:
                    self.err("R8", "overlapping rewrites inside one node (%s)" % self.text[b:e][:60])
                continue
            out.append(self.text[pos:cb])
            out.append(t)
            pos = ce
        out.append(self.text[pos:e])
        return "".join(out)

    def tx(self, c, texts):
        return texts.get(c["id"], self.text[slice(*self.rng(c))])

    def used(self, role):
        return role != "stmt"

    def ld(self, n, t):
        lt = self.env.lower_val(self.ty(n))
        if isinstance(lt, Ptr) and isinstance(lt.to, Base) and not has_anon(lt.to.name):
            return "SA1_LD(%s, %s)" % (decl(lt.to), t)
        s = decl(lt)
        if has_anon(s):
            self.err("R2", "load of a pointer to an anonymous type", n)
        return "SA1_LDP(%s, %s)" % (s, t)

    def elem_size(self, ptype, n):
        r = self.env.resolve(ptype)
        if not isinstance(r, Ptr):
            self.err("R2", "pointer arithmetic on a non-pointer", n)
            return "1"
        if self.env.kind(r.to) == "void":
            return "1"
        s = decl(self.env.lower_mem(r.to))
        if has_anon(s):
            self.err("R2", "pointer arithmetic on a pointer to an anonymous type", n)
        return "(ptrdiff_t)sizeof(%s)" % s

    def transform(self, n, b, e, kids, texts, role):
        k = n["kind"]
        if k == "DeclRefExpr":
            return self.t_declref(n, b, e)
        if k == "ImplicitCastExpr":
            return self.t_implicit(n, b, e, kids, texts, role)
        if k == "CStyleCastExpr":
            return self.t_cstyle(n, b, e, kids, texts, role)
        if k == "BinaryOperator":
            return self.t_binop(n, b, e, kids, texts, role)
        if k == "CompoundAssignOperator":
            return self.t_compound(n, b, e, kids, texts, role)
        if k == "UnaryOperator":
            return self.t_unop(n, b, e, kids, texts, role)
        if k == "CallExpr":
            return self.t_call(n, b, e, kids, texts, role)
        if k == "UnaryExprOrTypeTraitExpr":
            return self.t_sizeof(n, b, e, kids, texts)
        if k == "OffsetOfExpr":
            self.counts["R6c_ilp32_widths"] += 1
            return "((unsigned int)%s)" % self.splice(b, e, kids, texts)
        if k == "InitListExpr":
            return self.t_initlist(n, b, e, kids, texts)
        return self.splice(b, e, kids, texts)

    def t_declref(self, n, b, e):
        ref = n.get("referencedDecl") or {}
        i = ref.get("id")
        if ref.get("kind") == "VarDecl":
            if i in self.globals:
                g = self.globals[i]
                self.global_refs[g.name] += 1
                if g.kind == "pcdx":
                    self.counts["R4_pcdata_cross_tu_refs"] += 1
                    return "(*(%s *)sa1_decode(sa1g_pcdv_%s))" % (g.tdname, g.name)
                self.counts["R3_global_refs"] += 1
                return "(*(%s *)SA1_GV(0x%08Xu))" % (g.tdname, g.addr)
            if i in self.pcd_by_id:
                o = self.pcd[self.pcd_by_id[i]]
                self.counts["R4_pcdata_refs"] += 1
                return "(*(%s)sa1_decode(sa1g_pcdo[%d]))" % (decl(Ptr(o.lt)), o.k)
            if i in self.rtdata:
                name = self.rtdata[i]
                self.rtdata_refs[name] += 1
                self.counts["R7_rt_data_refs"] += 1
                return RT_DATA.get(name) or self.text[b:e]
        if ref.get("kind") == "FunctionDecl":
            name = ref.get("name")
            if name == "__builtin_fmaf":
                self.counts["R6_fmac"] += 1
                return "SA1G_FMAC"
            if name in R7_RENAMES:
                self.counts["R7_renames"] += 1
                return R7_RENAMES[name]
            if name in RT_FUNCS and name not in self.fn_defined:
                self.counts["R7_rt_function_refs"] += 1
                return RT_FUNCS[name]
        return self.text[b:e]

    def t_implicit(self, n, b, e, kids, texts, role):
        ck = n.get("castKind")
        if ck == "ArrayToPointerDecay" and kids and self.strip(kids[0], ()).get("kind") == "StringLiteral":
            return self.keep_nl(b, e, self.pcd_string(self.strip(kids[0], ())))
        t = self.splice(b, e, kids, texts)
        if not kids or ck == "BuiltinFnToFnPtr":
            return t
        c = kids[0]
        tk = self.kind_of(n)
        if ck == "LValueToRValue":
            if self.is_mem(c):
                if tk == "dataptr":
                    self.counts["R2_loads"] += 1
                    return self.ld(n, t)
                if tk == "fnptr":
                    self.counts["R5_fnptr_loads"] += 1
            return t
        if ck == "FunctionToPointerDecay":
            name = self.fn_ref(c)
            if name is None or role == "callee":
                return t
            return self.fn_const(name, n)
        if ck in ("BuiltinFnToFnPtr", "ArrayToPointerDecay"):
            return t
        if tk == "fnptr":
            if ck == "NullToPointer" or self.is_null_const(c):
                self.counts["R5_fnptr_null"] += 1
                return "SA1_GFN_NULL"
            if self.kind_of(c) == "fnptr":
                self.check_fn_compat(n, c)
                return t
            if ck == "IntegralToPointer":
                self.counts["R6b_int_to_fnptr"] += 1
                return "SA1_GFN_C(%s)" % t
            self.err("R5", "conversion to a function pointer from %s" % c["type"]["qualType"], n)
            return t
        if tk == "dataptr" and self.kind_of(c) == "fnptr":
            self.err("R5", "conversion of a function pointer to a data pointer", n)
            return t
        if ck == "FloatingToIntegral":
            self.counts["R6_ftrc"] += 1
            return "sa1g_ftrc(%s)" % t
        if ck == "PointerToIntegral":
            return self.ptr2int(c, t, "")
        if ck == "IntegralToPointer":
            return self.int2ptr(n, t)
        return t

    def check_fn_compat(self, n, c):
        try:
            a = self.env.resolve(self.ty(n))
            b = self.env.resolve(self.ty(c))
            sa = self.env.canon(self.env.resolve(a.to))
            sb = self.env.canon(self.env.resolve(b.to))
        except (TypeParseError, AttributeError):
            return
        if sa != sb:
            self.err("R8", "function-pointer signature mismatch: %s vs %s" % (sb, sa), n)

    def ptr2int(self, c, rest, prefix):
        s = self.strip(c, ("NoOp", "BitCast"))
        if s.get("kind") == "ImplicitCastExpr" and s.get("castKind") == "LValueToRValue" \
                and self.is_mem(s["inner"][0]) and self.kind_of(s) == "dataptr":
            self.counts["R6b_ptr_to_int_raw"] += 1
            return prefix + "(%s).v" % self.rw_text[s["inner"][0]["id"]]
        if self.kind_of(c) == "fnptr":
            self.counts["R6b_fnptr_to_int"] += 1
            return prefix + "(%s).v" % rest
        self.counts["R6b_ptr_to_int_encode"] += 1
        return prefix + "sa1_encode(%s)" % rest

    def int2ptr(self, n, rest):
        lt = self.env.lower_val(self.ty(n))
        self.counts["R6b_int_to_ptr_decode"] += 1
        self.escape("int_to_pointer_decode", "integer used as a pointer (decoded through the segment table): %s"
                    % rest[:80], n)
        return "((%s)sa1_decode((uint32_t)(%s)))" % (decl(lt), rest)

    def t_cstyle(self, n, b, e, kids, texts, role):
        ck = n.get("castKind")
        if not kids:
            return self.text[b:e]
        c = kids[0]
        cb = self.rng(c)[0]
        rest = self.splice(cb, e, kids, texts)
        prefix = self.text[b:cb]
        try:
            t0 = self.ty(n)
            lt = self.env.lower_val(t0)
        except TypeParseError:
            self.err("R8", "cannot parse cast type %r" % n["type"]["qualType"], n)
            return prefix + rest
        tk = self.env.kind(t0)
        if not self.env.same(lt, t0) and tk != "fnptr":
            prefix = "(%s)" % decl(lt)
            self.counts["R1_casts"] += 1
        if tk == "fnptr":
            if ck == "NullToPointer" or self.is_null_const(c):
                self.counts["R5_fnptr_null"] += 1
                return "SA1_GFN_NULL"
            if self.kind_of(c) == "fnptr":
                self.check_fn_compat(n, c)
                self.counts["R5_fnptr_casts"] += 1
                return "(%s)" % rest
            if ck == "IntegralToPointer":
                self.counts["R6b_int_to_fnptr"] += 1
                return "SA1_GFN_C(%s)" % rest
            self.err("R5", "cast of %s to a function pointer" % c["type"]["qualType"], n)
            return prefix + rest
        if tk == "dataptr" and self.kind_of(c) == "fnptr":
            # the DC code address as a data pointer: decodes into the arena copy of the code, and
            # encodes back to the same word if it is stored (8C06C0C0 -> 8C64133E(handler))
            self.counts["R6b_fnptr_to_dataptr"] += 1
            return "((%s)sa1_decode((%s).v))" % (decl(lt), rest)
        if ck == "FloatingToIntegral":
            self.counts["R6_ftrc"] += 1
            return prefix + "sa1g_ftrc(%s)" % rest
        if ck == "PointerToIntegral":
            return self.ptr2int(c, rest, prefix)
        if ck == "IntegralToPointer":
            return self.int2ptr(n, rest)
        return prefix + rest

    def note_stack_store(self, R, n, what):
        if self.stack_object(R):
            self.counts["R2_stack_addr_stores"] += 1
            self.escape("stack_address_stored", "%s (host stack: needs the segment 0x1D window in ga64)" % what, n)

    def store(self, n, b, e, L, R, tL, tR, role):
        rs = self.strip(R, ("NoOp", "BitCast"))
        while rs.get("kind") == "CStyleCastExpr" and rs.get("castKind") in ("BitCast", "NoOp"):
            rs = self.strip([c for c in rs["inner"] if c.get("kind")][0], ("NoOp", "BitCast"))
        raw = True
        if self.is_null_const(R):
            s = "sa1_st_raw(&(%s), 0u)" % tL
            self.counts["R2_null_stores"] += 1
        elif rs.get("kind") == "ImplicitCastExpr" and rs.get("castKind") == "LValueToRValue" \
                and self.is_mem(rs["inner"][0]) and self.kind_of(rs) == "dataptr":
            s = "sa1_st_raw(&(%s), (%s).v)" % (tL, self.rw_text[rs["inner"][0]["id"]])
            self.counts["R2_raw_copies"] += 1
        elif rs.get("kind") == "CStyleCastExpr" and rs.get("castKind") == "IntegralToPointer":
            op = [c for c in rs["inner"] if c.get("kind")][0]
            s = "sa1_st_raw(&(%s), (uint32_t)(%s))" % (tL, self.rw_text[op["id"]])
            self.counts["R6b_int_to_ptr_raw_stores"] += 1
        else:
            self.note_stack_store(R, n, self.text[b:e][:80])
            s = "sa1_st(&(%s), %s)" % (tL, tR)
            self.counts["R2_stores"] += 1
            raw = False
        if self.used(role):
            lt = decl(self.env.lower_val(self.ty(n)))
            s = "((%s)%s)" % (lt, ("sa1_decode(%s)" % s) if raw else s)
        return self.keep_nl(b, e, s)

    def t_binop(self, n, b, e, kids, texts, role):
        op = n.get("opcode")
        if len(kids) != 2:
            return self.splice(b, e, kids, texts)
        L, R = kids
        tL, tR = self.tx(L, texts), self.tx(R, texts)
        if op == "=":
            lk = self.kind_of(L)
            if self.is_mem(L) and lk == "dataptr":
                return self.store(n, b, e, L, R, tL, tR, role)
            if self.is_mem(L) and lk == "fnptr":
                self.counts["R5_fnptr_stores"] += 1
            return self.splice(b, e, kids, texts)
        kl, kr = self.kind_of(L), self.kind_of(R)
        if op in ("==", "!=") and "fnptr" in (kl, kr):
            self.counts["R5_fnptr_compares"] += 1
            return self.keep_nl(b, e, "SA1_GFN_%s(%s, %s)" % ("EQ" if op == "==" else "NE", tL, tR))
        if op in ("<", ">", "<=", ">=") and "fnptr" in (kl, kr):
            # the SH-4 compares the code addresses as unsigned words
            self.counts["R5_fnptr_relational"] += 1
            return self.keep_nl(b, e, "((%s).v %s (%s).v)" % (tL, op, tR))
        if op in ("-", "+") and "fnptr" in (kl, kr):
            self.err("R5", "arithmetic %s on a function pointer" % op, n)
        if op in ("/", "%") and self.kind_of(n) == "int" and not self.is_const(R):
            uns, width = self.env.int_info(self.ty(n))
            if width != 32:
                self.err("R6", "64-bit division", n)
            fn = "sa1g_%s_%s32" % ("div" if op == "/" else "mod", "u" if uns else "s")
            self.counts["R6_div_mod"] += 1
            return self.keep_nl(b, e, "%s(%s, %s)" % (fn, tL, tR))
        if op == "-" and kl == "dataptr" and kr == "dataptr":
            self.counts["R6c_ilp32_widths"] += 1
            return "((int)(%s))" % self.splice(b, e, kids, texts)
        return self.splice(b, e, kids, texts)

    def t_compound(self, n, b, e, kids, texts, role):
        op = n.get("opcode")
        L, R = kids
        tL, tR = self.tx(L, texts), self.tx(R, texts)
        lk = self.kind_of(L)
        if self.is_mem(L) and lk == "dataptr":
            if op not in ("+=", "-="):
                self.err("R2", "operator %s on a pointer in memory" % op, n)
                return self.splice(b, e, kids, texts)
            esz = self.elem_size(self.ty(L), n)
            s = "sa1_gp_add(&(%s), %s(ptrdiff_t)(%s) * %s)" % (tL, "" if op == "+=" else "-", tR, esz)
            if self.used(role):
                s = "((%s)%s)" % (decl(self.env.lower_val(self.ty(L))), s)
            self.counts["R2_rmw"] += 1
            return self.keep_nl(b, e, s)
        if lk == "fnptr":
            self.err("R5", "operator %s on a function pointer" % op, n)
        if op in ("/=", "%=") and lk == "int" and not self.is_const(R):
            ct = self.env.parse((n.get("computeResultType") or n.get("computeLHSType") or n["type"])["qualType"])
            uns, width = self.env.int_info(ct)
            if width != 32:
                self.err("R6", "64-bit division", n)
            fn = "sa1g_%s_%s32" % ("div" if op == "/=" else "mod", "u" if uns else "s")
            self.counts["R6_div_mod"] += 1
            if self.side_effect_free(L):
                return self.keep_nl(b, e, "(%s = %s(%s, %s))" % (tL, fn, tL, tR))
            # the lvalue has side effects (a[i++] /= d): evaluate its address once
            if self.strip(L, ()).get("kind") == "MemberExpr" and self.is_bitfield(L):
                self.err("R6", "%s on a bit-field with side effects" % op, n)
                return self.splice(b, e, kids, texts)
            p = self.tmp("p")
            self.counts["R6_div_mod_side_effects"] += 1
            return self.keep_nl(b, e, "({ __typeof__(%s) *%s = &(%s); *%s = (__typeof__(%s))%s(*%s, %s); })"
                                % (tL, p, tL, p, tL, fn, p, tR))
        return self.splice(b, e, kids, texts)

    def is_bitfield(self, L):
        L = self.strip(L, ())
        if L.get("kind") != "MemberExpr":
            return False
        fid = L.get("referencedMemberDecl")
        for rec in self.records.values():
            for f in rec.get("inner", []):
                if f.get("id") == fid:
                    return bool(f.get("isBitfield"))
        return False

    def t_unop(self, n, b, e, kids, texts, role):
        op = n.get("opcode")
        if not kids:
            return self.text[b:e]
        c = kids[0]
        tc = self.tx(c, texts)
        ck = self.kind_of(c)
        if op in ("++", "--") and self.is_mem(c) and ck == "dataptr":
            esz = self.elem_size(self.ty(c), n)
            fn = "sa1_gp_postadd" if n.get("isPostfix") else "sa1_gp_add"
            s = "%s(&(%s), %s%s)" % (fn, tc, "" if op == "++" else "-", esz)
            if self.used(role):
                s = "((%s)%s)" % (decl(self.env.lower_val(self.ty(c))), s)
            self.counts["R2_rmw"] += 1
            return self.keep_nl(b, e, s)
        if op in ("++", "--") and ck == "fnptr":
            self.err("R5", "%s on a function pointer" % op, n)
        if op == "&":
            name = self.fn_ref(c)
            if name is not None:
                return self.fn_const(name, n)
            if self.is_mem(c) and ck in ("dataptr", "fnptr"):
                self.counts["R2_addr_of_slot"] += 1
        return self.splice(b, e, kids, texts)

    def t_call(self, n, b, e, kids, texts, role):
        callee = kids[0]
        name = self.fn_ref(callee)
        if name is not None:
            if name in self.implicit_fns:
                # C89 implicit declaration (SHC accepts it): made explicit as int f() in the lowered text;
                # an SHC intrinsic maps to its sa1/guest.h declaration. Reported as a source issue.
                if name not in RT_FUNCS:
                    self.implicit_decls.add(name)
                self.counts["R8_implicit_declarations"] += 1
                if name not in [w.get("name") for w in self.escapes if w["kind"] == "implicit_declaration"]:
                    self.escapes.append({"kind": "implicit_declaration", "name": name, "where": self.where(n),
                                         "msg": "call of the undeclared function %s (source issue)" % name})
            if not name.startswith("__builtin") and name not in self.statics:
                if name in R7_RENAMES:
                    self.fn_called.add(R7_RENAMES[name])
                elif name in self.fn_defined:
                    self.fn_called.add(name)
                elif name in RT_FUNCS:
                    self.rt_called.add(RT_FUNCS[name])
                else:
                    self.fn_called.add(name)
            return self.splice(b, e, kids, texts)
        x = callee
        if x.get("kind") == "ImplicitCastExpr" and x.get("castKind") == "FunctionToPointerDecay":
            y = self.strip(x["inner"][0], ())
            if y.get("kind") == "UnaryOperator" and y.get("opcode") == "*":
                x = y["inner"][0]
        try:
            ft = self.env.resolve(self.ty(x))
            fn = self.env.resolve(ft.to) if isinstance(ft, Ptr) else None
        except TypeParseError:
            fn = None
        if not isinstance(fn, Fn):
            self.err("R5", "indirect call through %r" % (x.get("type") or {}).get("qualType"), n)
            return self.splice(b, e, kids, texts)
        nt = decl(Ptr(self.env.lower_val(fn)))
        if has_anon(nt):
            self.err("R5", "indirect call with an anonymous parameter type", n)
        sig = self.env.sig(self.env.lower_val(fn))
        sid = sig_id(sig)
        self.sigs[sid] = sig
        xt = self.rw_text.get(x["id"], self.text[slice(*self.rng(x))])
        new = "((%s)sa1_fn(%s, 0x%08Xu /* %s */))" % (nt, xt, sid, sig)
        self.counts["R5_indirect_calls"] += 1
        if self.is_mem(self.strip(x, ("LValueToRValue",))):
            self.counts["R5_indirect_calls_through_memory"] += 1
        return self.splice(b, e, kids, texts, {callee["id"]: new})

    def t_sizeof(self, n, b, e, kids, texts):
        at = (n.get("argType") or {}).get("qualType")
        try:
            t = self.env.parse(at) if at else (self.ty(kids[0]) if kids else None)
        except TypeParseError:
            t = None
        k = self.env.kind(t) if t is not None else None
        if n.get("name") in ("sizeof", "__alignof", "_Alignof", "alignof") and k in ("dataptr", "fnptr"):
            self.counts["R1_sizeof_ptr"] += 1
            return "((unsigned int)4u)"
        self.counts["R6c_ilp32_widths"] += 1
        return "((unsigned int)%s)" % self.splice(b, e, kids, texts)

    def t_initlist(self, n, b, e, kids, texts):
        """automatic aggregates (locals, compound literals, the pcdata initialisers): pointer members
        get the guest word of the value"""
        over = {}
        braced = self.text[b:b + 1] == "{"
        for c in kids:
            k = self.kind_of(c)
            if k != "dataptr":
                continue
            if not braced:
                self.err("R1", "brace-elided initializer list with a pointer member", c)
                continue
            if self.is_null_const(c):
                over[c["id"]] = "{0u}"
                self.counts["R2_null_inits"] += 1
                continue
            s = self.strip(c, ("NoOp", "BitCast"))
            if s.get("kind") == "ImplicitCastExpr" and s.get("castKind") == "LValueToRValue" \
                    and self.is_mem(s["inner"][0]):
                over[c["id"]] = "{(%s).v}" % self.rw_text[s["inner"][0]["id"]]
                self.counts["R2_raw_inits"] += 1
            else:
                self.note_stack_store(c, c, "initializer " + self.text[slice(*self.rng(c))][:60])
                over[c["id"]] = "{sa1_encode(%s)}" % self.tx(c, texts)
                self.counts["R2_pointer_inits"] += 1
        return self.splice(b, e, kids, texts, over)

    def rw_declstmt(self, n):
        b, e = self.rng(n)
        kids = [c for c in n.get("inner", []) if c.get("kind")]
        groups = collections.OrderedDict()
        rec_edits = []
        for c in kids:
            if c["kind"] == "VarDecl":
                groups.setdefault(self.rng(c)[0], []).append(c)
            elif c["kind"] == "RecordDecl" and c.get("completeDefinition"):
                rec_edits += self.record_field_edits(c, True)
                for c2, p2 in walk(c):
                    if c2 is not c and c2.get("kind") == "RecordDecl" and c2.get("completeDefinition"):
                        rec_edits += self.record_field_edits(c2, True)
        parts = []
        used = set()
        for gb, g in groups.items():
            ge = max(self.rng(v)[1] for v in g)
            inside = [x for x in rec_edits if gb <= x[0] and x[1] <= ge]
            used |= set(id(x) for x in inside)
            s = self.vardecl_group(g, gb, ge, inside)
            if s is not None:
                parts.append((gb, ge, s))
        for x in rec_edits:
            if id(x) not in used:
                parts.append((x[0], x[1], x[2]))
        parts.sort(key=lambda x: (x[0], x[1]))
        out, pos = [], b
        for gb, ge, s in parts:
            if gb < pos:
                self.err("R8", "overlapping rewrites in a declaration", n)
                continue
            out.append(self.text[pos:gb])
            out.append(s)
            pos = ge
        out.append(self.text[pos:e])
        return "".join(out)

    def vardecl_group(self, g, gb, ge, rec_edits=()):
        env = self.env
        cls = self.data_group(g, gb, ge, False)
        if cls is not LOCAL:
            if rec_edits:
                self.err("R1", "record definition inside a moved declaration", g[0])
            return cls
        items, changed = [], False
        for v in g:
            t = self.ty(v)
            esc = v["id"] in self.escaped
            lt = env.lower_mem(t) if esc else env.lower_val(t)
            init = [c for c in v.get("inner", []) if c.get("kind") and "valueCategory" in c] if v.get("init") else []
            it = self.rw(init[0], "value") if init else None
            if not env.same(lt, t):
                changed = True
                self.counts["R2b_escaped_decls" if esc else "R1_locals"] += 1
                if has_anon(decl(lt)):
                    self.err("R1", "anonymous type in local %s" % v["name"], v)
            if it is not None and esc and env.kind(t) == "dataptr":
                s = self.strip(init[0], ("NoOp", "BitCast"))
                if self.is_null_const(init[0]):
                    it = "{0u}"
                elif s.get("kind") == "ImplicitCastExpr" and s.get("castKind") == "LValueToRValue" \
                        and self.is_mem(s["inner"][0]):
                    it = "{(%s).v}" % self.rw_text[s["inner"][0]["id"]]
                else:
                    self.note_stack_store(init[0], v, "escaped local %s" % v["name"])
                    it = "{sa1_encode(%s)}" % it
                changed = True
            items.append((v, lt, init[0] if init else None, it))
        if not changed:
            # keep the original spelling, only splice the rewritten initialisers (and block records)
            parts = [(x[0], x[1], x[2]) for x in rec_edits]
            for v, lt, ini, it in items:
                if ini is not None and it is not None:
                    ib, ie = self.rng(ini)
                    parts.append((ib, ie, it))
            parts.sort()
            out, pos = [], gb
            for ib, ie, it in parts:
                if ib < pos:
                    continue
                out.append(self.text[pos:ib])
                out.append(it)
                pos = ie
            out.append(self.text[pos:ge])
            return "".join(out)
        if rec_edits:
            self.err("R1", "record definition in a declaration whose declarators change", g[0])
        decls = []
        for v, lt, ini, it in items:
            sc = v.get("storageClass")
            s = ("%s " % sc if sc == "register" else "") + decl(lt, v["name"])
            if it is not None:
                s += " = " + it
            decls.append(s)
        return self.keep_nl(gb, ge, "; ".join(decls))

    # -------------------------------------------------------------- R4 pcdata unit
    def emit_pcdata(self):
        """the TU's pcdata block, its initialiser and the unit descriptor (sa1g_pcdata.c places it)"""
        if not self.pcd:
            return
        inits = []
        for o in self.pcd:
            if o.kind != "string" and o.init is not None:
                it = self.rw(o.init, "value")
                if self.env.kind(o.t) == "dataptr":
                    # a pointer object: its guest word (as an escaped local's initialiser)
                    s = self.strip(o.init, ("NoOp", "BitCast"))
                    if self.is_null_const(o.init):
                        it = "{0u}"
                    elif s.get("kind") == "ImplicitCastExpr" and s.get("castKind") == "LValueToRValue" \
                            and self.is_mem(s["inner"][0]):
                        it = "{(%s).v}" % self.rw_text[s["inner"][0]["id"]]
                    else:
                        it = "{sa1_encode(%s)}" % it
                inits.append((o, it))
        # rw of the initialisers may have added string objects: lay out after it
        members = []
        for o in self.pcd:
            if o.kind == "string":
                members.append("    char o%d[%d];" % (o.k, max(1, o.size)))
            else:
                members.append("    %s; /* %s */" % (decl(o.lt, "o%d" % o.k), o.name))
        L = ["", "/* ---- R4 pcdata of this TU (pclower; docs/libsa1_integration.md 2.3) ---- */"]
        for o in self.pcd:
            if o.export:
                L.append("uint32_t sa1g_pcdv_%s; /* guest address of %s, for the other TUs */" % (o.name, o.name))
        L += ["struct sa1g_pcdblk {"] + members + ["};", "static void sa1g_pcdinit(uint32_t sa1g_gb)", "{"]
        for o in self.pcd:
            L.append("    sa1g_pcdo[%d] = sa1g_gb + (uint32_t)__builtin_offsetof(struct sa1g_pcdblk, o%d);" % (o.k, o.k))
        for o in self.pcd:
            if o.export:
                L.append("    sa1g_pcdv_%s = sa1g_pcdo[%d];" % (o.name, o.k))
        L.append("    __builtin_memset(sa1_decode(sa1g_gb), 0, sizeof(struct sa1g_pcdblk));")
        for o in self.pcd:
            if o.kind == "string":
                L.append("    __builtin_memcpy(sa1_decode(sa1g_pcdo[%d]), %s, %d);" % (o.k, o.text.replace("\n", " "),
                                                                                       o.size))
        for o, it in inits:
            t = self.tmp("t")
            L.append("    { %s = %s; __builtin_memcpy(sa1_decode(sa1g_pcdo[%d]), &%s, sizeof(%s)); }"
                     % (decl(o.lt, t), it.replace("\n", " "), o.k, t, t))
        L.append("}")
        L.append("SA1G_PCD_UNIT(%s, struct sa1g_pcdblk, sa1g_pcdinit)" % self.uname)
        self.tail.append("\n".join(L))
        self.counts["R4_pcdata_objects"] = len(self.pcd)

    # -------------------------------------------------------------- R9 per-TU layout asserts
    def local_layout(self):
        if not self.local_records:
            return
        text = apply_edit_list(self.text, [x for x in self.edits if x[3] == "D12"])
        res = layout_probe(text, self.local_records, os.path.join(self.ctx.out, "work", self.uname + ".lp.c"))
        if res is None:
            self.err("R9", "layout probe of the records of this file failed")
            return
        self.tail.append(layout_assert_text(res, "records of " + self.base))
        self.counts["R9_local_layout_asserts"] += len(res)

    # -------------------------------------------------------------- output
    def apply_edits(self):
        body = apply_edit_list(self.text, self.edits, self)
        body = MARK_RE.sub(lambda m: '#line %s "%s"' % (m.group(1), m.group(2)), body)
        hdr = ("/* Generated by tools/pclower.py from %s (GUEST-ARENA dialect, docs/libsa1_integration.md 2.3).\n"
               " * Do not edit. The text is the same for every profile; sa1/guest.h selects ga32/ga64. */\n"
               '#include "sa1/guest.h"\n') % os.path.relpath(self.path, ROOT).replace("\\", "/")
        if self.pcd:
            hdr += "static uint32_t sa1g_pcdo[%d]; /* R4: guest addresses of this TU's pcdata objects */\n" % len(self.pcd)
        for name in sorted(self.implicit_decls):
            hdr += "int %s(); /* R8: called without a declaration in the source (C89 implicit int) */\n" % name
        tail = "\n".join(self.tail)
        return hdr + body + ("\n" + tail + "\n" if tail else "")

    def slim(self):
        """drop what only the per-TU work needs (process pool results are pickled)"""
        for k in ("text", "nl", "markers", "mark_starts", "removed", "removed_starts", "edits", "rw_text",
                  "records", "ast", "typedef_record", "file_pcd"):
            if hasattr(self, k):
                setattr(self, k, None)
        for o in self.pcd:
            o.node = o.init = None
        self.env.cache = {}
        self.ctx = None

    def summary(self):
        return {"source": os.path.relpath(self.path, ROOT).replace("\\", "/"),
                "counts": dict(sorted(self.counts.items())),
                "globals": dict(sorted(self.global_refs.items())),
                "global_symbols": len(self.global_refs),
                "defines": sorted(n for n, i in self.fn_defined.items() if not i["static"]),
                "statics": sorted(n for n, i in self.fn_defined.items() if i["static"]),
                "calls": sorted(self.fn_called),
                "rt_calls": sorted(self.rt_called),
                "rt_data": dict(sorted(self.rtdata_refs.items())),
                "address_taken": sorted(self.fn_addr_taken),
                "pcdata": [{"k": o.k, "kind": o.kind, "name": o.name, "export": o.export} for o in self.pcd],
                "pcdata_imports": sorted(self.pcdx_names),
                "dc_data_defined_in_c": sorted(self.dc_defined),
                "layout_patches": sorted(set(self.patches)),
                "skipped_bodies": self.skipped_bodies,
                "preprocess_flags_from": getattr(self, "flag_source", None),
                "escapes": self.escapes,
                "errors": self.errors}


LOCAL = object()     # data_group(): a plain local declaration


def apply_edit_list(text, edits, unit=None):
    edits = sorted(edits, key=lambda x: (x[0], x[1]))
    out, pos = [], 0
    for b, e, t, why in edits:
        if b < pos:
            if unit is not None:
                unit.err("R8", "overlapping edits at offset %d (%s)" % (b, why))
            continue
        out.append(text[pos:b])
        out.append(t)
        pos = e
    out.append(text[pos:])
    return "".join(out)


def parse_ast(path, extra=()):
    gn.native_cflags = lambda non_matching, extra_inc=(): ["-target", PARSE_TARGET]
    if not extra:
        return gn.ast_of(path, False)
    cmd = gn.zig_cc() + ["-target", PARSE_TARGET, "-fsyntax-only", "-w", "-Wno-error=implicit-function-declaration",
                         "-Wno-error=implicit-int", "-Xclang", "-ast-dump=json"] + \
        list(extra) + [path]
    r = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace")
    errs = [l for l in r.stderr.splitlines() if "error:" in l and "error: FileNotFound" not in l]
    if errs or not r.stdout.strip():
        return None, r.stderr
    # -ast-dump-filter prints one JSON object per matching declaration
    objs, dec, s, i = [], json.JSONDecoder(), r.stdout, 0
    while True:
        while i < len(s) and s[i] in " \r\n\t":
            i += 1
        if i >= len(s):
            break
        try:
            o, j = dec.raw_decode(s, i)
        except ValueError:
            break
        objs.append(o)
        i = j
    return objs, r.stderr


def layout_probe(text, records, path):
    """ILP32 sizeof / offsetof of records, measured by clang on the unlowered text:
    records = [(C spelling, [field, ...])] -> [(spelling, field or None, value)]"""
    items = []
    lines = [text, "\n/* pclower layout probe */\n"]
    for sp, fields in records:
        items.append((sp, None))
        lines.append("extern char sa1g_lp_%d[sizeof(%s)];\n" % (len(items) - 1, sp))
        for f in fields:
            items.append((sp, f))
            lines.append("extern char sa1g_lp_%d[__builtin_offsetof(%s, %s) + 1];\n" % (len(items) - 1, sp, f))
    with open(path, "w", encoding="latin-1", newline="") as fh:
        fh.write("".join(lines))
    objs, err = parse_ast(path, ["-Xclang", "-ast-dump-filter=sa1g_lp_"])
    if objs is None:
        return None
    vals = {}
    for o in objs:
        m = re.match(r"sa1g_lp_(\d+)$", o.get("name", ""))
        t = re.match(r"char\s*\[(\d+)\]", (o.get("type") or {}).get("qualType", ""))
        if m and t:
            vals[int(m.group(1))] = int(t.group(1))
    out = []
    for i, (sp, f) in enumerate(items):
        if i not in vals:
            return None
        out.append((sp, f, vals[i] - (1 if f else 0)))
    return out


def layout_assert_text(res, what):
    L = ["/* R9: lowered layout == DC (ILP32) layout, %s (values measured by pclower) */" % what]
    for sp, f, v in res:
        if f is None:
            L.append('_Static_assert(sizeof(%s) == %du, "pclower R9: sizeof(%s)");' % (sp, v, sp))
        else:
            L.append('_Static_assert(__builtin_offsetof(%s, %s) == %du, "pclower R9: %s.%s");' % (sp, f, v, sp, f))
    return "\n".join(L)


# ------------------------------------------------------------------------------------------------
# cross-TU outputs


class Ctx(object):
    def __init__(self, out, extra_inc=()):
        self.out = out
        self.syms = gn.load_symbols()
        self.sigs = {}
        self.extra_inc = [os.path.abspath(d) for d in extra_inc]


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="latin-1", newline="\n") as f:
        f.write(text)


GEN = "/* Generated by tools/pclower.py. Do not edit. */"
GUEST_H_FUNCS = set(R7_RENAMES.values()) | {v for v in RT_FUNCS.values() if v.startswith("sa1g_")}


def mix32(x):
    x &= 0xFFFFFFFF
    x ^= x >> 16
    x = (x * 0x85EBCA6B) & 0xFFFFFFFF
    x ^= x >> 13
    x = (x * 0xC2B2AE35) & 0xFFFFFFFF
    x ^= x >> 16
    return x


def build_phash(keys):
    """hash-and-displace perfect hash: bucket = top bits of mix(k), slot = mix(k ^ d[bucket] * phi)"""
    n = max(1, len(keys))
    mbits = max(1, (n + n // 4 - 1).bit_length())
    while True:
        m = 1 << mbits
        bbits = max(0, (max(1, n // 4) - 1).bit_length())
        nb = 1 << bbits
        buckets = [[] for _ in range(nb)]
        for k in keys:
            buckets[(mix32(k) >> (32 - bbits)) if bbits else 0].append(k)
        disp = [0] * nb
        slots = [None] * m
        ok = True
        for b in sorted(range(nb), key=lambda i: -len(buckets[i])):
            ks = buckets[b]
            if not ks:
                continue
            for d in range(65536):
                ss = [mix32(k ^ ((d * 0x9E3779B9) & 0xFFFFFFFF)) & (m - 1) for k in ks]
                if len(set(ss)) == len(ss) and all(slots[s] is None for s in ss):
                    for k, s in zip(ks, ss):
                        slots[s] = k
                    disp[b] = d
                    break
            else:
                ok = False
                break
        if ok:
            return mbits, bbits, disp, slots
        mbits += 1


def gen_fntab_base(entries):
    """the code map of one image, as the harness wants it (gen_ga.py): entries = (addr, name, sig, kind);
    kind def | stub | extern | rt"""
    ents = sorted(entries, key=lambda x: x[0] & 0x1FFFFFFF)
    keys = [a & 0x1FFFFFFF for a, n, s, k in ents]
    mbits, bbits, disp, slots = build_phash(keys)
    index = {k: i for i, k in enumerate(keys)}
    lines = [GEN, "/* Code map (docs/libsa1_integration.md section 3): DC code address -> native function, a",
             " * hash-and-displace perfect hash keyed by addr & 0x1FFFFFFF (P0/P1/P2 aliases of one address",
             " * hit the same entry). Compile with -DSA1G_FN_HOST when the host defines sa1_fn itself (the",
             " * harness routes indirect calls through its interpreter). */",
             '#include "sa1/guest.h"', "", "typedef void (*sa1g_anyfn)(void);", ""]
    for a, name, sig, kind in ents:
        if name not in GUEST_H_FUNCS:
            lines.append("extern void %s(void);" % name)
    lines += ["", "static const sa1g_fnent sa1g_fntab[%d] = {" % max(1, len(ents))]
    for a, name, sig, kind in ents:
        lines.append('    { 0x%08Xu, 0x%08Xu, 0x%08Xu, (sa1g_anyfn)%s, "%s" }, /* %s %s */' % (
            a & 0x1FFFFFFF, a, sig_id(sig), name, name, kind, sig))
    if not ents:
        lines.append('    { 0, 0, 0, 0, "" },')
    lines += ["};", "const size_t sa1g_fntab_count = %d;" % len(ents), ""]
    lines.append("static const uint16_t sa1g_fnhash_d[%d] = {" % len(disp))
    for i in range(0, len(disp), 16):
        lines.append("    " + ", ".join("%du" % d for d in disp[i:i + 16]) + ",")
    lines += ["};", "static const uint16_t sa1g_fnhash_s[%d] = { /* slot -> entry + 1 (0: empty) */" % len(slots)]
    vals = [(index[k] + 1) if k is not None else 0 for k in slots]
    for i in range(0, len(vals), 16):
        lines.append("    " + ", ".join("%du" % v for v in vals[i:i + 16]) + ",")
    lines += ["};", "",
              "static uint32_t sa1g_fnmix(uint32_t x)", "{",
              "    x ^= x >> 16;", "    x *= 0x85EBCA6Bu;", "    x ^= x >> 13;", "    x *= 0xC2B2AE35u;",
              "    x ^= x >> 16;", "    return x;", "}", "",
              "const sa1g_fnent *sa1g_fn_lookup(uint32_t g)", "{",
              "    uint32_t key = g & 0x1FFFFFFFu;",
              "    uint32_t b = %s;" % ("sa1g_fnmix(key) >> %d" % (32 - bbits) if bbits else "0u"),
              "    uint32_t s = sa1g_fnmix(key ^ (sa1g_fnhash_d[b] * 0x9E3779B9u)) & 0x%Xu;" % ((1 << mbits) - 1),
              "    uint32_t i = sa1g_fnhash_s[s];",
              "    return (i != 0u && sa1g_fntab[i - 1u].key == key) ? &sa1g_fntab[i - 1u] : (const sa1g_fnent *)0;",
              "}", "",
              "#if !defined(SA1G_FN_HOST)",
              "void *sa1_fn(sa1_gfn g, uint32_t sig)", "{",
              "    const sa1g_fnent *e = sa1g_fn_lookup(g.v);",
              "    if (e == (const sa1g_fnent *)0 || (sig != 0u && e->sig != sig)) {",
              "        sa1g_fault_fn(g.v, sig);", "    }",
              "    return (void *)e->fn;", "}", "#endif", "",
              "/* native function -> DC code address (traces, debug output): a hash built on first use */",
              "#define SA1G_FNREV_SIZE %du" % (1 << max(1, (2 * max(1, len(ents)) - 1).bit_length())),
              "uint32_t sa1_fn_addr(const void *native)", "{",
              "    static uint32_t rev[SA1G_FNREV_SIZE];",
              "    static int built;",
              "    uint32_t h;",
              "    size_t i;",
              "    if (!built) {",
              "        for (i = 0; i < sa1g_fntab_count; i++) {",
              "            h = sa1g_fnmix((uint32_t)((uintptr_t)sa1g_fntab[i].fn >> 2)) & (SA1G_FNREV_SIZE - 1u);",
              "            while (rev[h] != 0u) {",
              "                h = (h + 1u) & (SA1G_FNREV_SIZE - 1u);",
              "            }",
              "            rev[h] = (uint32_t)i + 1u;",
              "        }",
              "        built = 1;",
              "    }",
              "    h = sa1g_fnmix((uint32_t)((uintptr_t)native >> 2)) & (SA1G_FNREV_SIZE - 1u);",
              "    while (rev[h] != 0u) {",
              "        if ((const void *)sa1g_fntab[rev[h] - 1u].fn == native) {",
              "            return sa1g_fntab[rev[h] - 1u].addr;",
              "        }",
              "        h = (h + 1u) & (SA1G_FNREV_SIZE - 1u);",
              "    }",
              "    return 0u;", "}", "",
              "const char *sa1_fn_name(uint32_t g)", "{",
              "    const sa1g_fnent *e = sa1g_fn_lookup(g);",
              "    return e ? e->name : (const char *)0;", "}"]
    return "\n".join(lines) + "\n"


def _fntab_table(ents, tab, hd, hs, lookup, static_lookup):
    """C of one hashed table: tab[] (the entries), hd[] / hs[] (displacements / slots) and its lookup function"""
    keys = [a & 0x1FFFFFFF for a, n, s, k in ents]
    mbits, bbits, disp, slots = build_phash(keys)
    index = {k: i for i, k in enumerate(keys)}
    lines = ["static const sa1g_fnent %s[%d] = {" % (tab, max(1, len(ents)))]
    for a, name, sig, kind in ents:
        lines.append('    { 0x%08Xu, 0x%08Xu, 0x%08Xu, (sa1g_anyfn)%s, "%s" }, /* %s %s */' % (
            a & 0x1FFFFFFF, a, sig_id(sig), name, name, kind, sig))
    if not ents:
        lines.append('    { 0, 0, 0, 0, "" },')
    lines += ["};", "static const uint16_t %s[%d] = {" % (hd, len(disp))]
    for i in range(0, len(disp), 16):
        lines.append("    " + ", ".join("%du" % d for d in disp[i:i + 16]) + ",")
    lines += ["};", "static const uint16_t %s[%d] = { /* slot -> entry + 1 (0: empty) */" % (hs, len(slots))]
    vals = [(index[k] + 1) if k is not None else 0 for k in slots]
    for i in range(0, len(vals), 16):
        lines.append("    " + ", ".join("%du" % v for v in vals[i:i + 16]) + ",")
    lines += ["};", "",
              "%sconst sa1g_fnent *%s(uint32_t g)" % ("static " if static_lookup else "", lookup), "{",
              "    uint32_t key = g & 0x1FFFFFFFu;",
              "    uint32_t b = %s;" % ("sa1g_fnmix(key) >> %d" % (32 - bbits) if bbits else "0u"),
              "    uint32_t s = sa1g_fnmix(key ^ (%s[b] * 0x9E3779B9u)) & 0x%Xu;" % (hd, (1 << mbits) - 1),
              "    uint32_t i = %s[s];" % hs,
              "    return (i != 0u && %s[i - 1u].key == key) ? &%s[i - 1u] : (const sa1g_fnent *)0;" % (tab, tab),
              "}", ""]
    return lines


def gen_fntab(entries, images=None):
    """entries: list of (addr, name, sig, kind) of the base image; kind def | stub | extern | rt.
    images: None (one code map, as the harness builds it) or the overlay images, a list of (name, info, entries)
    (design 2.5; info = image_info()): every image gets its own table, sa1g_fnimages[] lists them, and the
    lookups try the running world's images (sa1g_fn_active, set by the world core) before the base map"""
    if images is None:
        return gen_fntab_base(entries)
    ents = sorted(entries, key=lambda x: x[0] & 0x1FFFFFFF)
    imgs = [(n, i, sorted(e, key=lambda x: x[0] & 0x1FFFFFFF)) for n, i, e in images]
    total = len(ents) + sum(len(e) for n, i, e in imgs)
    lines = [GEN, "/* Code map (docs/libsa1_integration.md section 3, images 2.5): DC code address -> native function.",
             " * The base image (1ST_READ and libsa1's runtime) and every overlay image have their own hash-and-",
             " * displace perfect hash keyed by addr & 0x1FFFFFFF (P0/P1/P2 aliases of one address hit the same entry);",
             " * overlays that share a load slot have overlapping addresses. The world core lists the images its arena",
             " * holds in sa1g_fn_active[] (NULL-terminated) while it runs: a lookup tries those images (by their code",
             " * range) first, then the base map. Compile with -DSA1G_FN_HOST when the host defines sa1_fn itself. */",
             '#include "sa1/guest.h"', "", "typedef void (*sa1g_anyfn)(void);", ""]
    for a, name, sig, kind in ents + [e for n, i, es in imgs for e in es]:
        if name not in GUEST_H_FUNCS:
            lines.append("extern void %s(void);" % name)
    lines += ["", "static uint32_t sa1g_fnmix(uint32_t x)", "{",
              "    x ^= x >> 16;", "    x *= 0x85EBCA6Bu;", "    x ^= x >> 13;", "    x *= 0xC2B2AE35u;",
              "    x ^= x >> 16;", "    return x;", "}", "",
              "/* ---- base image: 1ST_READ (and the lowered runtime) */"]
    lines += _fntab_table(ents, "sa1g_fntab", "sa1g_fnhash_d", "sa1g_fnhash_s", "sa1g_fn_lookup_base", True)
    lines += ["const size_t sa1g_fntab_count = %d;" % len(ents), ""]
    for name, info, es in imgs:
        lines.append("/* ---- image %s: %s (SHA-1 %s), code %08X-%08X, %d entries */" % (
            name, info["prs"], info["prs_sha1"], info["lo"], info["hi"], len(es)))
        lines += _fntab_table(es, "sa1g_fntab_%s" % name, "sa1g_fnhash_d_%s" % name, "sa1g_fnhash_s_%s" % name,
                              "sa1g_fn_lookup_%s" % name, True)
    lines += ["/* ---- the overlay images (keep in step with LIB/src/world/sa1_world.h) */",
              "typedef struct sa1g_fnimage {",
              "    const char *name;           /* the overlay: STG01 (the file STG01.PRS) */",
              "    const char *prs;            /* its file on the disc */",
              "    const char *prs_sha1;       /* SHA-1 of that file: the bytes this C was decompiled from */",
              "    uint32_t lo, hi;            /* code range (addr & 0x1FFFFFFF) */",
              "    const sa1g_fnent *(*lookup)(uint32_t g);",
              "    const sa1g_fnent *table;",
              "    size_t count;",
              "} sa1g_fnimage;", "",
              "const sa1g_fnimage sa1g_fnimages[%d] = {" % max(1, len(imgs))]
    for name, info, es in imgs:
        lines.append('    { "%s", "%s", "%s", 0x%08Xu, 0x%08Xu, sa1g_fn_lookup_%s, sa1g_fntab_%s, %du },' % (
            name, info["prs"], info["prs_sha1"], info["lo"], info["hi"], name, name, len(es)))
    if not imgs:
        lines.append('    { "", "", "", 0u, 0u, (const sa1g_fnent *(*)(uint32_t))0, (const sa1g_fnent *)0, 0u },')
    lines += ["};", "const size_t sa1g_fnimage_count = %d;" % len(imgs), "",
              "/* the images of the running world, NULL-terminated (the world core, like sa1g_seg) */",
              "extern const sa1g_fnimage *sa1g_fn_active[];", "",
              "/* a function of one of the running world's images (NULL: none at this address) */",
              "const sa1g_fnent *sa1g_fn_image_lookup(uint32_t g);",
              "const sa1g_fnent *sa1g_fn_image_lookup(uint32_t g)", "{",
              "    uint32_t key = g & 0x1FFFFFFFu;", "    size_t i;",
              "    for (i = 0; sa1g_fn_active[i] != (const sa1g_fnimage *)0; i++) {",
              "        const sa1g_fnimage *m = sa1g_fn_active[i];",
              "        if (key >= m->lo && key < m->hi) {",
              "            const sa1g_fnent *e = m->lookup(g);",
              "            if (e != (const sa1g_fnent *)0) {", "                return e;", "            }",
              "        }", "    }", "    return (const sa1g_fnent *)0;", "}", "",
              "const sa1g_fnent *sa1g_fn_lookup(uint32_t g)", "{",
              "    const sa1g_fnent *e = sa1g_fn_image_lookup(g);",
              "    return e != (const sa1g_fnent *)0 ? e : sa1g_fn_lookup_base(g);", "}", "",
              "#if !defined(SA1G_FN_HOST)",
              "void *sa1_fn(sa1_gfn g, uint32_t sig)", "{",
              "    const sa1g_fnent *e = sa1g_fn_lookup(g.v);",
              "    if (e == (const sa1g_fnent *)0 || (sig != 0u && e->sig != sig)) {",
              "        sa1g_fault_fn(g.v, sig);", "    }",
              "    return (void *)e->fn;", "}", "#endif", "",
              "/* native function -> DC code address (traces, debug output): a hash over every table, built on first use */",
              "static const struct { const sa1g_fnent *t; size_t n; } sa1g_fnall[%d] = {" % (1 + len(imgs)),
              "    { sa1g_fntab, %du }," % len(ents)]
    for name, info, es in imgs:
        lines.append("    { sa1g_fntab_%s, %du }," % (name, len(es)))
    lines += ["};",
              "#define SA1G_FNREV_SIZE %du" % (1 << max(1, (2 * max(1, total) - 1).bit_length())),
              "uint32_t sa1_fn_addr(const void *native)", "{",
              "    static uint32_t rev[SA1G_FNREV_SIZE];",
              "    static int built;",
              "    uint32_t h;",
              "    size_t i, j;",
              "    if (!built) {",
              "        for (j = 0; j < sizeof sa1g_fnall / sizeof sa1g_fnall[0]; j++) {",
              "            for (i = 0; i < sa1g_fnall[j].n; i++) {",
              "                h = sa1g_fnmix((uint32_t)((uintptr_t)sa1g_fnall[j].t[i].fn >> 2)) & (SA1G_FNREV_SIZE - 1u);",
              "                while (rev[h] != 0u) {",
              "                    h = (h + 1u) & (SA1G_FNREV_SIZE - 1u);",
              "                }",
              "                rev[h] = (uint32_t)((j << 16) | i) + 1u;",
              "            }",
              "        }",
              "        built = 1;",
              "    }",
              "    h = sa1g_fnmix((uint32_t)((uintptr_t)native >> 2)) & (SA1G_FNREV_SIZE - 1u);",
              "    while (rev[h] != 0u) {",
              "        uint32_t v = rev[h] - 1u;",
              "        const sa1g_fnent *e = &sa1g_fnall[v >> 16].t[v & 0xFFFFu];",
              "        if ((const void *)e->fn == native) {",
              "            return e->addr;",
              "        }",
              "        h = (h + 1u) & (SA1G_FNREV_SIZE - 1u);",
              "    }",
              "    return 0u;", "}", "",
              "const char *sa1_fn_name(uint32_t g)", "{",
              "    const sa1g_fnent *e = sa1g_fn_lookup(g);",
              "    return e ? e->name : (const char *)0;", "}"]
    return "\n".join(lines) + "\n"


def gen_stubs(stubs):
    lines = [GEN, "/* Fatal boundary stubs (docs/libsa1_integration.md 10.5, default policy): functions the lowered",
             " * code calls or takes the address of that no lowered TU defines. Each one is a world fault naming",
             " * its DC address. Declared without parameters: they never return. */",
             '#include "sa1/guest.h"', ""]
    for a, name, sig in sorted(stubs, key=lambda x: x[1]):
        lines.append("void %s(void); /* %s */" % (name, sig))
        lines.append("void %s(void)" % name)
        lines.append("{")
        lines.append('    sa1g_unimpl(0x%08Xu, "%s");' % (a, name))
        lines.append("}")
        lines.append("")
    return "\n".join(lines) + "\n"


def gen_pcdata(units):
    us = [u for u in units if getattr(u, "pcd", None) and hasattr(u, "lowered")]
    lines = [GEN, "/* R4 pcdata (docs/libsa1_integration.md 2.3): every lowered TU with C-defined data (file-scope",
             " * and static-local variables without a DC address, string literals used as pointers) has one unit.",
             " * sa1g_pcd_init() places the units one after another in the extension segment 0x18 (ga64:",
             " * guest 0x98000000 + offset, the world maps sa1g_pcd_size() bytes there and enters before calling",
             " * it) or in each unit's static storage (ga32), and runs their initialisers: plain data copied,",
             " * pointer members encoded then. */",
             '#include "sa1/guest.h"', ""]
    for u in us:
        lines.append("extern const sa1g_pcd_unit sa1g_pcdunit_%s;" % u.uname)
    lines += ["", "static const sa1g_pcd_unit *const sa1g_pcd_units[] = {"]
    for u in us:
        lines.append("    &sa1g_pcdunit_%s," % u.uname)
    lines += ["    (const sa1g_pcd_unit *)0", "};", "",
              "uint32_t sa1g_pcd_size(void)", "{",
              "    uint32_t off = 0u;", "    size_t i;",
              "    for (i = 0; sa1g_pcd_units[i] != (const sa1g_pcd_unit *)0; i++) {",
              "        const sa1g_pcd_unit *u = sa1g_pcd_units[i];",
              "        off = (off + u->align - 1u) & ~(u->align - 1u);",
              "        off += u->size;", "    }", "    return off;", "}", "",
              "void sa1g_pcd_init(void)", "{",
              "    uint32_t off = 0u;", "    size_t i;",
              "    for (i = 0; sa1g_pcd_units[i] != (const sa1g_pcd_unit *)0; i++) {",
              "        const sa1g_pcd_unit *u = sa1g_pcd_units[i];",
              "        off = (off + u->align - 1u) & ~(u->align - 1u);",
              "        u->init(u->storage != (void *)0 ? (uint32_t)(uintptr_t)u->storage : SA1G_PCDATA_GUEST + off);",
              "        off += u->size;", "    }", "}"]
    return "\n".join(lines) + "\n"


# runtime-owned state inside the arena (design 2.9): SDK image data/BSS and C-library state
RUNTIME_RANGES = [(0x8C5FBB08, 0x8C5FBB0C), (0x8C1601F4, 0x8C1603A8), (0x8C5FBB60, 0x8C67C680),
                  (0x8C7EC8E0, 0x8C7EE8E0), (0x8C88F5F8, 0x8C890400)]


def sym_class(addr, lt_decl):
    if any(lo <= addr < hi for lo, hi in RUNTIME_RANGES):
        return "runtime"
    a = addr & 0x1FFFFFFF
    if 0x0C900000 <= a < 0x0D000000:
        return "overlay"
    if 0x0C67C680 <= a < 0x0C900000:
        return "bss"
    if "sa1_gfn" in lt_decl:
        return "code_table"
    if "sa1_gp" in lt_decl:
        return "pointer_data"
    return "pod"


def gen_symtab(units):
    rows = {}
    for u in units:
        for g in u.globals.values():
            if g.name in u.global_refs and g.kind == "arena":
                rows.setdefault((g.name, g.addr), set()).add((decl(g.ltype), u.base, u.env.mem_class(g.ltype)))
    lines = [GEN, "/* Data symbols referenced by the lowered code: name, DC address, class, lowered type, file",
             " * (docs/libsa1_integration.md 2.4; debugging, --dump-world and the write census, never the data path).",
             " * class: bss / pod / pointer_data (holds sa1_gp) / code_table (holds sa1_gfn) / overlay / runtime",
             " * (state the SDK or C library owns, design 2.9). One row per declared view. */",
             "#ifndef SA1G_SYMTAB_H", "#define SA1G_SYMTAB_H", "#include <stdint.h>", "",
             "enum sa1g_symclass { SA1G_SYM_BSS, SA1G_SYM_POD, SA1G_SYM_POINTER_DATA, SA1G_SYM_CODE_TABLE,",
             "                     SA1G_SYM_OVERLAY, SA1G_SYM_RUNTIME };", "",
             "typedef struct sa1g_sym {", "    const char *name;", "    uint32_t addr;", "    int cls;",
             "    const char *type;", "    const char *file;", "} sa1g_sym;", "", "static const sa1g_sym sa1g_symtab[] = {"]
    n = 0
    hist = collections.Counter()
    for (name, addr), views in sorted(rows.items(), key=lambda x: x[0][1]):
        for t, f, mc in sorted(views):
            c = sym_class(addr, t + " " + mc)
            hist[c] += 1
            lines.append('    { "%s", 0x%08Xu, SA1G_SYM_%s, "%s", "%s" },' % (name, addr, c.upper(),
                                                                          t.replace('"', "'"), f))
            n += 1
    if not n:
        lines.append('    { "", 0u, 0, "", "" },')
    lines += ["};", "#define SA1G_SYMTAB_COUNT %d" % n, "", "#endif"]
    return "\n".join(lines) + "\n", dict(hist)


LAYOUT_SRC = '''/* pclower: every project header, for sa1g_layout_asserts.c (R9) */
#include "common.h"
#include "ninja.h"
#include "task.h"
#include "sa1/sa1.h"
#include "sa1/pending.h"
'''


def gen_layout(ctx):
    src = os.path.join(ctx.out, "work", "sa1g_layout_asserts.src.c")
    write(src, LAYOUT_SRC)
    u = Unit(src, ctx)
    u.all_records = True
    u.run()
    if not hasattr(u, "lowered"):
        return u, None
    return u, u.lowered.replace("tools/pclower.py from build", "tools/pclower.py (R9 layout asserts) from build")


def compile_check(out, outputs, jobs):
    """outputs: profile -> list of C files; every file for each target of its profile"""
    import concurrent.futures
    res = {}
    for prof, tname, flags in COMPILE_TARGETS:
        objd = os.path.join(out, "work", "obj_" + tname)
        os.makedirs(objd, exist_ok=True)
        srcs = outputs.get(prof, [])

        def one(src):
            r = subprocess.run(gn.zig_cc() + flags + COMPILE_CHECK_FLAGS +
                               ["-I", os.path.join(ROOT, "include"), "-I", os.path.dirname(src), "-c", src, "-o",
                                os.path.join(objd, os.path.basename(src)[:-2] + ".o")],
                               capture_output=True, text=True, encoding="utf-8", errors="replace")
            lines = r.stderr.splitlines()
            return (os.path.basename(src), r.returncode, [l for l in lines if " error: " in l][:6],
                    [l for l in lines if " warning: " in l][:6])
        with concurrent.futures.ThreadPoolExecutor(jobs) as ex:
            rows = list(ex.map(one, srcs))
        res[tname] = {"profile": prof, "files": len(rows), "ok": sum(1 for r in rows if r[1] == 0),
                      "failed": {r[0]: r[2] for r in rows if r[1] != 0},
                      "warnings": {r[0]: r[3] for r in rows if r[3]}}
        print("pclower: %s (%s) compile check: %d/%d ok" % (tname, prof, res[tname]["ok"], len(rows)))
    return res


# ------------------------------------------------------------------------------------------------
# R10: toolchain pin and AST format fingerprint

FINGERPRINT_SRC = r'''
typedef struct fp_node { struct fp_node *next; int v; unsigned char b : 3; void (*fn)(struct fp_node *); } fp_node;
typedef void (*fp_fn)(fp_node *);
typedef union fp_u { fp_node *p; int i; } fp_u;
extern fp_node D_8C000100;
extern fp_node *B_8C000200[4];
void func_8C000300(fp_node *n);
static int fp_s = 1;
static const char *fp_names[] = { "a", "bc" };
_Static_assert(sizeof(fp_node) == 16, "fp");
void (*fp_pick(int i))(fp_node *) { return i ? func_8C000300 : 0; }
int fp_f(fp_node *p, int d, int *a, float x, ...)
{
    static int cnt = 2;
    struct loc { fp_node *q; int k; } l = { p, 1 };
    fp_node *arr[2] = { p, 0 };
    int i = 0;
    unsigned u = (unsigned)x;
    fp_fn f = p->fn;
    l.q = &D_8C000100;
    a[i++] /= d;
    B_8C000200[1] = p->next;
    p->next++;
    p->v += sizeof(fp_node *) + __builtin_offsetof(fp_node, v);
    if (f < fp_pick(1) && f != 0 && !f) cnt++;
    f(p);
    (*p->fn)(p);
    p = (fp_node *)(unsigned)p->v;
    return l.q->v + arr[0]->v + fp_names[0][0] + (int)(p - arr[1]) + (int)u + fp_s + cnt + (x > 1.5f ? 1 : 0);
}
'''


def ast_fingerprint(out):
    """node kinds, their keys and the type spellings pclower depends on, for a fixed probe file"""
    p = os.path.join(out, "work", "sa1g_fingerprint.c")
    write(p, FINGERPRINT_SRC)
    ast, err = parse_ast(p)
    if ast is None:
        return "error: " + err[-300:]
    shape = set()
    for n, parent in walk(ast):
        if not n.get("loc") or n.get("isImplicit"):
            pass
        keys = sorted(k for k in n.keys() if k not in ("id", "loc", "range", "inner", "name", "value",
                                                       "mangledName", "previousDecl", "parentDeclContextId"))
        t = (n.get("type") or {}).get("qualType", "")
        if "fp_" in json.dumps(n.get("loc", {})) or True:
            shape.add("%s|%s|%s" % (n["kind"], ",".join(keys),
                                    n.get("castKind", "") + n.get("opcode", "") + n.get("storageClass", "")))
        if t and n["kind"] in ("FunctionDecl", "VarDecl", "FieldDecl", "TypedefDecl", "CStyleCastExpr",
                               "ImplicitCastExpr", "ParmVarDecl"):
            shape.add("T|" + t)
    shape = sorted(s for s in shape if "__" not in s.split("|")[-1] or s.startswith("T|"))
    return hashlib.sha1("\n".join(shape).encode()).hexdigest()[:16]


# ------------------------------------------------------------------------------------------------
# driver

def p0p1_functions():
    p = os.path.join(ROOT, "build", "libsa1_units.json")
    if not os.path.exists(p):
        return None
    fns = set()
    for u in json.load(open(p)):
        fns.update(u.get("functions", []))
    return fns


SCOPE_KEYS = [("loads of pointers held in memory", ["R2_loads"]),
              ("stores of pointers held in memory", ["R2_stores", "R2_null_stores", "R2_raw_copies",
                                                     "R6b_int_to_ptr_raw_stores"]),
              ("read-modify-writes", ["R2_rmw"]),
              ("&slot", ["R2_addr_of_slot"]),
              ("function-pointer loads", ["R5_fnptr_loads"]),
              ("function-pointer stores", ["R5_fnptr_stores"]),
              ("indirect calls", ["R5_indirect_calls"]),
              ("indirect calls through memory", ["R5_indirect_calls_through_memory"]),
              ("function-address sites", ["R5_fn_designators"]),
              ("global references", ["R3_global_refs"]),
              ("sizeof(T *) sites", ["R1_sizeof_ptr"]),
              ("stack addresses stored into memory", ["R2_stack_addr_stores"])]


def scope_of(units):
    tot = collections.Counter()
    syms, fnaddr = set(), set()
    for u in units:
        tot.update(u.counts)
        syms |= set(g for g in u.global_refs)
        fnaddr |= u.fn_addr_taken
    d = collections.OrderedDict()
    d["translation units"] = len(units)
    for label, keys in SCOPE_KEYS:
        d[label] = sum(tot[k] for k in keys)
    d["global symbols"] = len(syms)
    d["functions whose address is taken"] = len(fnaddr)
    return d


def main():
    # @FILE reads more arguments from FILE, one per line (a response file: 500+ absolute TU paths exceed the 32 KB
    # command line of Windows)
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0], fromfile_prefix_chars="@")
    ap.add_argument("files", nargs="*")
    ap.add_argument("--all", action="store_true", help="every TU of src/1st_read, of every src/overlays/* image "
                    "and of libsa1/src/rt (the runtime is lowered with the game)")
    ap.add_argument("--no-rt", action="store_true", help="with --all: without libsa1/src/rt")
    ap.add_argument("--profile", default="all", choices=list(PROFILES) + ["all"])
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "pclower"))
    ap.add_argument("--inc", action="append", default=[],
                    help="extra project include directory (libsa1/src/rt when lowering the runtime)")
    ap.add_argument("--allow-conflict", action="append", default=[],
                    help="accept a cross-TU prototype conflict for NAME (reported, R8)")
    ap.add_argument("--allow-all-conflicts", action="store_true", help="survey mode: report conflicts only")
    ap.add_argument("--stub-unresolved", action="store_true",
                    help="emit fatal stubs for referenced functions no lowered TU defines (else R8 error)")
    ap.add_argument("--extern-unresolved", action="store_true",
                    help="leave referenced functions / exported pcdata no lowered TU defines to the host")
    ap.add_argument("--allow-unpinned", action="store_true", help="R10: accept another toolchain")
    ap.add_argument("--no-layout", action="store_true", help="skip sa1g_layout_asserts.c")
    ap.add_argument("--drop-failing", action="store_true",
                    help="harness builds: leave out TUs with lowering errors (listed in report.json dropped_units)")
    ap.add_argument("-j", type=int, default=1, help="TUs lowered in parallel (threads; zig runs overlap)")
    ap.add_argument("--compile", action="store_true",
                    help="compile every output for x86-windows (ga32), x86_64-windows and x86_64-linux (ga64)")
    ap.add_argument("--selftest", action="store_true", help="rule tests (see selftest())")
    ap.add_argument("--revalidate", action="store_true", help="R10 procedure for a new toolchain")
    ap.add_argument("--write-pin", action="store_true", help="with --revalidate: record the toolchain in this file")
    a = ap.parse_args()
    if a.selftest:
        return selftest(a)
    if a.revalidate:
        return revalidate(a)
    files = list(a.files)
    if a.all:
        dirs = [os.path.join(ROOT, "src", "1st_read")]
        if os.path.isdir(OVERLAY_SRC):
            dirs += sorted(os.path.join(OVERLAY_SRC, d) for d in os.listdir(OVERLAY_SRC)
                           if os.path.isdir(os.path.join(OVERLAY_SRC, d)))
        for dd in dirs:
            files += sorted(os.path.join(dd, f) for f in os.listdir(dd) if f.endswith(".c"))
        # libsa1's runtime is lowered with the game (design 4.1); the FSCA table is host data (RT_DATA)
        if os.path.isdir(LIB_RT) and not a.no_rt:
            files += sorted(os.path.join(LIB_RT, f) for f in os.listdir(LIB_RT)
                            if f.endswith(".c") and f != "rt_fsca_q.c")
            if LIB_RT not in a.inc:
                a.inc.append(LIB_RT)
    if not files:
        ap.error("no input files (give FILE.c... or --all)")
    return run_lowering(a, files)


_CTX = None


def _init_worker(out, extra_inc):
    global _CTX
    _CTX = Ctx(out, extra_inc)


def _lower_one(path):
    return lower_one(path, _CTX)


def lower_one(path, ctx):
    u = Unit(path, ctx)
    try:
        u.run()
    except Exception as ex:     # a pclower bug must not hide the other TUs' results
        import traceback
        u.err("R8", "pclower internal error: %s: %s | %s" % (type(ex).__name__, ex,
                                                              traceback.format_exc().splitlines()[-3:]))
    u.slim()
    return u


def run_lowering(a, files, quiet=False):
    out = os.path.abspath(a.out)
    ctx = Ctx(out, a.inc)
    report = {"tool": "tools/pclower.py (phase 2)", "errors": [], "units": {}}
    zv, cv = zig_versions()
    fp = ast_fingerprint(out)
    report["toolchain"] = {"zig": zv, "clang": cv, "ast_fingerprint": fp, "pinned_zig": PINNED_ZIG,
                           "pinned_clang": PINNED_CLANG, "pinned_ast_fingerprint": PINNED_AST_FINGERPRINT,
                           "parse_target": PARSE_TARGET}
    if (zv, cv, fp) != (PINNED_ZIG, PINNED_CLANG, PINNED_AST_FINGERPRINT) and not a.allow_unpinned:
        report["errors"].append({"rule": "R10", "msg": "toolchain zig %s / %s / AST %s is not the pinned %s / %s / %s: "
                                 "run tools/pclower.py --revalidate" % (zv, cv, fp, PINNED_ZIG, PINNED_CLANG,
                                                                        PINNED_AST_FINGERPRINT), "where": ""})

    if a.j > 1 and len(files) > 1:
        # processes, not threads: the lowering is Python work (JSON of ~30 MB per TU) under the GIL
        import concurrent.futures
        with concurrent.futures.ProcessPoolExecutor(min(a.j, len(files)), initializer=_init_worker,
                                                    initargs=(out, ctx.extra_inc)) as ex:
            units = list(ex.map(_lower_one, files))
    else:
        units = [lower_one(f, ctx) for f in files]
    for u in units:
        ctx.sigs.update(u.sigs)
    for u in units:
        report["units"][u.base] = u.summary()
    if getattr(a, "drop_failing", False):
        # harness builds: a TU that cannot be lowered is left to the interpreter (reported)
        report["dropped_units"] = {u.base: u.errors for u in units if u.errors}
        units = [u for u in units if not u.errors]

    # ---- per-image C names (design 2.5): the functions an overlay image defines, and the ones its TUs reference
    # at an address of the image's code range without defining them (image stubs), are <IMAGE>__<name>
    img_defs = collections.defaultdict(set)
    for img in sorted(set(u.image for u in units) - {BASE_IMAGE}):
        info = image_info(img)
        if info["error"]:
            report["errors"].append({"rule": "R8", "msg": "image %s: %s" % (img, info["error"]), "where": ""})
    for u in units:
        if u.image != BASE_IMAGE:
            img_defs[u.image] |= set(n for n, i in u.fn_defined.items() if not i["static"])

    def cname(img, name):
        """the C symbol of the function name as a TU of image img sees it"""
        if img == BASE_IMAGE or name in GUEST_H_FUNCS or name in RT_FUNC_NAMES:
            return name
        if name in img_defs[img] or in_image(img, fn_addr(name, image_syms(img, ctx.syms))):
            return image_name(img, name)
        return name
    for u in units:
        u.renames = {}
        if u.image != BASE_IMAGE:
            for name in set(u.fn_declared) | set(n for n, i in u.fn_defined.items() if not i["static"]) \
                    | u.fn_called | u.fn_addr_taken:
                c = cname(u.image, name)
                if c != name:
                    u.renames[name] = c

    # ---- cross-TU checks (R8)
    defined = {}                # C name -> (unit, info, image, name in the source)
    for u in units:
        for name, info in u.fn_defined.items():
            if info["static"]:
                continue
            cn = u.renames.get(name, name)
            if cn in defined and defined[cn][0] != u.base:
                report["errors"].append({"rule": "R8", "msg": "%s defined in %s and %s" % (
                    cn, defined[cn][0], u.base), "where": u.base})
            defined[cn] = (u.base, info, u.image, name)
    per = collections.defaultdict(dict)
    for u in units:
        for name, canon in u.fn_declared.items():
            per[u.renames.get(name, name)][u.base] = canon
    conflicts = {n: p for n, p in per.items() if len(set(p.values())) > 1}
    report["prototype_conflicts"] = {n: {s: sorted(b for b, x in p.items() if x == s) for s in sorted(set(p.values()))}
                                     for n, p in sorted(conflicts.items())}
    spell = collections.defaultdict(dict)
    for u in units:
        for name, c in u.fn_canon.items():
            spell[u.renames.get(name, name)][u.base] = c
    report["prototype_spelling_differences_same_abi"] = len(
        [n for n, p in spell.items() if len(set(p.values())) > 1 and n not in conflicts])
    allowed = set(R8_ALLOWED_CONFLICTS) | set(a.allow_conflict)
    report["allowed_conflicts"] = sorted(allowed & set(conflicts))
    report["allow_list_unused"] = sorted(set(R8_ALLOWED_CONFLICTS) - set(conflicts))
    for name in sorted(conflicts):
        if name not in allowed and not a.allow_all_conflicts:
            report["errors"].append({"rule": "R8", "msg": "conflicting prototypes of %s: %s" % (
                name, report["prototype_conflicts"][name]), "where": ""})
    referenced = {}             # C name -> (image of the symbol, name in the source)
    for u in sorted(units, key=lambda x: (x.image != BASE_IMAGE, x.image, x.base)):
        for name in sorted(u.fn_called | u.fn_addr_taken):
            cn = u.renames.get(name, name)
            referenced.setdefault(cn, (u.image if cn != name else BASE_IMAGE, name))
    entries, stubs, externs, runtime = [], [], [], {}
    stub_images = {}
    for cn, (base, info, img, name) in sorted(defined.items()):
        addr = fn_addr(name, image_syms(img, ctx.syms))
        if addr is None:
            if cn not in RT_FUNC_NAMES:
                report.setdefault("warnings", []).append("%s (%s) has no DC address: not in the code map" % (cn, base))
            continue
        if img != BASE_IMAGE and not in_image(img, addr):
            report["errors"].append({"rule": "R8", "msg": "%s is defined by image %s at %08X, outside its code range"
                                     % (cn, img, addr), "where": base})
        entries.append((addr, cn, info["sig"], "def", img))
    orig_of = {v: k for k, v in R7_RENAMES.items()}

    def sig_of(name, img=BASE_IMAGE):
        """ABI signature of a referenced function: the TUs of its image first"""
        if name in R7_SIGS:
            return R7_SIGS[name]
        orig = orig_of.get(name, name)
        us = [u for u in units if u.image == img] + [u for u in units if u.image != img]
        return next((u.fn_sig[orig] for u in us if orig in u.fn_sig),
                    next((u.fn_sig[name] for u in us if name in u.fn_sig), "?"))
    for cn in sorted(set(referenced) - set(defined)):
        img, name = referenced[cn]
        addr = fn_addr(name, image_syms(img, ctx.syms))
        if cn in GUEST_H_FUNCS:
            runtime[cn] = sig_of(cn)
            if addr is not None:
                entries.append((addr, cn, sig_of(cn), "rt", BASE_IMAGE))
            continue
        if addr is None:
            report["errors"].append({"rule": "R8", "msg": "unresolved function %s (no DC address, not a runtime "
                                     "function)" % cn, "where": ""})
            continue
        sig = sig_of(name, img)
        if img != BASE_IMAGE:
            stub_images[cn] = img
        if a.stub_unresolved:
            stubs.append((addr, cn, sig))
            entries.append((addr, cn, sig, "stub", img))
        elif a.extern_unresolved:
            externs.append((addr, cn, sig))
            entries.append((addr, cn, sig, "extern", img))
        else:
            report["errors"].append({"rule": "R8", "msg": "unresolved external function %s" % cn, "where": ""})
    for u in units:
        for name in u.rt_called:
            if name not in defined:
                runtime[name] = sig_of(next((k for k, v in RT_FUNCS.items() if v == name), name))
    # duplicate code-map keys within one image (an R7 name and the address name of one function): keep one
    best = {}
    rank = {"def": 0, "extern": 1, "rt": 2, "stub": 3}
    for e in entries:
        k = (e[4], e[0] & 0x1FFFFFFF)
        if k not in best or (rank[e[3]], e[1].startswith("sa1g_")) < (rank[best[k][3]], best[k][1].startswith("sa1g_")):
            best[k] = e
    report["code_map_aliases"] = sorted("%08X %s" % (e[0], e[1]) for e in entries
                                        if best[(e[4], e[0] & 0x1FFFFFFF)] is not e)
    entries = sorted(best.values(), key=lambda x: (x[4] != BASE_IMAGE, x[4], x[0] & 0x1FFFFFFF))
    base_entries = [e[:4] for e in entries if e[4] == BASE_IMAGE]
    images = []
    for img in sorted((set(e[4] for e in entries) | set(u.image for u in units)) - {BASE_IMAGE}):
        images.append((img, image_info(img), [e[:4] for e in entries if e[4] == img]))
    report["images"] = {img: {"prs": info["prs"], "prs_sha1": info["prs_sha1"],
                              "code_range": ["%08X" % info["lo"], "%08X" % info["hi"]],
                              "units": sorted(u.base for u in units if u.image == img),
                              "code_map_entries": len(es),
                              "defined": sorted(n for ad, n, sg, k in es if k == "def"),
                              "stubs": sorted(n for ad, n, sg, k in es if k == "stub")}
                        for img, info, es in images}
    report["stub_images"] = stub_images
    # R4 cross-TU pcdata
    exports = {}
    for u in units:
        for o in u.pcd:
            if o.export:
                if o.name in exports:
                    report["errors"].append({"rule": "R8", "msg": "pcdata %s defined in %s and %s" % (
                        o.name, exports[o.name], u.base), "where": u.base})
                exports[o.name] = u.base
    host_data = sorted(set().union(*[u.pcdx_names for u in units]) - set(exports)) if units else []
    if host_data and not a.extern_unresolved:
        for n in host_data:
            report["errors"].append({"rule": "R4", "msg": "data %s has no DC address and no lowered TU defines "
                                     "it" % n, "where": ", ".join(u.base for u in units if n in u.pcdx_names)})
    report["runtime_functions"] = runtime
    report["runtime_data"] = sorted(set().union(*[set(u.rtdata_refs) for u in units])) if units else []
    report["pcdata_exports"] = exports
    report["host_provided_pcdata"] = host_data if a.extern_unresolved else []
    report["code_map_entries"] = len(entries)
    report["stubs"] = ["%s %08X %s" % (n, ad, s) for ad, n, s in sorted(stubs, key=lambda x: x[1])]
    report["host_provided_functions"] = ["%s %08X %s" % (n, ad, s) for ad, n, s in sorted(externs, key=lambda x: x[1])]
    report["call_signatures"] = {"0x%08X" % k: v for k, v in sorted(ctx.sigs.items())}

    # ---- totals, scope, escapes
    tot = collections.Counter()
    gsyms = set()
    for u in units:
        tot.update(u.counts)
        gsyms |= set(g for g in u.global_refs)
        report["errors"] += [dict(e, unit=u.base) for e in u.errors]
    tot["R3_global_symbols"] = len(gsyms)
    report["totals"] = dict(sorted(tot.items()))
    pp = p0p1_functions()
    report["scope"] = {"all": scope_of(units)}
    if pp:
        sub = [u for u in units if set(u.fn_defined) & pp]
        report["scope"]["p0p1"] = scope_of(sub)
    esc = collections.Counter()
    report["escapes"] = []
    for u in units:
        for e in u.escapes:
            esc[e["kind"]] += 1
            report["escapes"].append(dict(e, unit=u.base))
    report["escape_counts"] = dict(esc)
    report["header_changes_needed"] = HEADER_CHANGES
    report["known_limits"] = KNOWN_LIMITS
    report["profiles"] = {}

    # ---- outputs
    lay_u, lay_text = (None, None) if a.no_layout else gen_layout(ctx)
    if lay_u is not None:
        report["layout"] = {"records": len(lay_u.local_records), "asserts": lay_u.counts["R9_local_layout_asserts"],
                            "errors": lay_u.errors}
        report["errors"] += [dict(e, unit="sa1g_layout_asserts.c") for e in lay_u.errors]
    profiles = PROFILES if a.profile == "all" else (a.profile,)
    outputs = {}
    for prof in profiles:
        pd = os.path.join(out, prof)
        ld = os.path.join(pd, "lowered")
        if os.path.isdir(ld):
            for f in os.listdir(ld):
                if f.endswith(".c"):
                    os.unlink(os.path.join(ld, f))
        files_out = []
        for u in units:
            if hasattr(u, "lowered"):
                text = u.lowered
                if u.renames:
                    # per-image C names (design 2.5), after sa1/guest.h: they rename definitions, declarations and calls
                    head, _, rest = text.partition('#include "sa1/guest.h"\n')
                    text = (head + '#include "sa1/guest.h"\n'
                            + "/* image %s (design 2.5): the C names of its functions */\n" % u.image
                            + "".join("#define %s %s\n" % kv for kv in sorted(u.renames.items())) + rest)
                write(os.path.join(ld, u.base), text)
                files_out.append(os.path.join(ld, u.base))
        write(os.path.join(pd, "sa1g_fntab.c"), gen_fntab(base_entries, images))
        write(os.path.join(pd, "sa1g_stubs.c"), gen_stubs(stubs))
        write(os.path.join(pd, "sa1g_pcdata.c"), gen_pcdata(units))
        st, hist = gen_symtab(units)
        write(os.path.join(pd, "sa1g_symtab.h"), st)
        report["symbol_classes"] = hist
        files_out += [os.path.join(pd, f) for f in ("sa1g_fntab.c", "sa1g_stubs.c", "sa1g_pcdata.c")]
        lp = os.path.join(pd, "sa1g_layout_asserts.c")
        if lay_text:
            write(lp, lay_text)
            files_out.append(lp)
        elif os.path.exists(lp):
            os.unlink(lp)
        prof_info = {"profile": prof, "cflags": PROFILE_CFLAGS[prof],
                     "include": [os.path.join(ROOT, "include"), pd],
                     "lowered": sorted(u.base for u in units if hasattr(u, "lowered"))}
        write(os.path.join(pd, "profile.json"), json.dumps(prof_info, indent=1) + "\n")
        report["profiles"][prof] = pd
        outputs[prof] = files_out
    # manifest for harness/tools/gen_ga.py
    fj = {"units": {}, "code_map": [{"addr": "%08X" % ad, "name": n, "sig": s, "kind": k, "image": im}
                                    for ad, n, s, k, im in entries],
          "images": report["images"],
          "runtime_functions": runtime, "host_provided_pcdata": report["host_provided_pcdata"],
          "r7": R7_RENAMES, "rt_funcs": RT_FUNCS}
    for u in units:
        if not hasattr(u, "lowered"):
            continue
        ext = {}
        usyms = image_syms(u.image, ctx.syms)
        for name in sorted((u.fn_called | u.fn_addr_taken) - set(u.fn_defined)):
            orig = orig_of.get(name, name)
            ext[u.renames.get(name, name)] = {"addr": "%08X" % (fn_addr(name, usyms) or 0),
                                              "sig": u.fn_sig.get(orig, u.fn_sig.get(name, "?")),
                                              "called": name in u.fn_called}
        fj["units"][u.base] = {
            "source": os.path.relpath(u.path, ROOT).replace("\\", "/"),
            "image": u.image,
            "defines": [{"name": u.renames.get(n, n), "addr": "%08X" % (fn_addr(n, usyms) or 0), "sig": i["sig"],
                         "variadic": i.get("variadic", False)}
                        for n, i in sorted(u.fn_defined.items()) if not i["static"]],
            "externs": ext, "rt_calls": sorted(u.rt_called), "rt_data": sorted(u.rtdata_refs),
            "pcdata": bool(u.pcd), "pcdata_imports": sorted(u.pcdx_names),
            "globals": {g.name: "%08X" % g.addr for g in u.globals.values() if g.kind == "arena"
                        and g.name in u.global_refs}}
    write(os.path.join(out, "functions.json"), json.dumps(fj, indent=1) + "\n")
    if a.compile:
        report["compile"] = compile_check(out, outputs, max(1, a.j))
        for t, r in report["compile"].items():
            for f, errs in sorted(r["failed"].items()):
                report["errors"].append({"rule": "R8", "msg": "%s does not compile for %s: %s" % (
                    f, t, " | ".join(errs)[:600]), "where": f})
    write(os.path.join(out, "report.json"), json.dumps(report, indent=1) + "\n")
    if not quiet:
        print("pclower: %d TU(s), %d code-map entries (%d stubs, %d host), %d error(s) -> %s" % (
            len(units), len(entries), len(stubs), len(externs), len(report["errors"]), os.path.join(out, "report.json")))
        for e in report["errors"][:200]:
            print("  error %s %s: %s" % (e["rule"], e.get("where", ""), e["msg"][:300]))
    return 1 if report["errors"] else 0


# ------------------------------------------------------------------------------------------------
# --selftest: one TU per rule family, lowered, then run in a fake world (ga64 on x86_64-windows, ga32 on
# x86-windows with DC RAM at 0x8C000000) and compiled for x86_64-linux

SELFTEST_TU1 = r'''/* pclower selftest TU 1 (not game code): one function per rule */
typedef int Sint32;
typedef unsigned int Uint32;
typedef float Float;

typedef struct st_node {
    struct st_node *next;
    Sint32 v;
    void (*fn)(struct st_node *);
} st_node;
typedef void (*st_fn)(st_node *);

extern st_node D_8C8F0000;          /* R3: arena global */
extern st_node *B_8C8F0100[4];      /* R3: arena array of pointers */
extern Sint32 st_shared;            /* R4: pcdata exported by TU 2 */

void func_8CF10000(st_node *n);
void func_8CF10020(st_node *n);

static st_node st_local = { &st_local, 7, func_8CF10000 };     /* R4: pointers in a static initialiser */
static const char *st_names[] = { "ab", "cde" };              /* R4: string literals in memory */
st_node *st_exported = &D_8C8F0000;                             /* R4: exported, arena pointer */

void func_8CF10000(st_node *n)
{
    n->v += 1;
}

void func_8CF10020(st_node *n)
{
    n->v += 100;
}

Sint32 func_8CF10040(void)
{
    static Sint32 cnt = 5;          /* R4: static local */
    return ++cnt;
}

static void st_take(st_node **pp)
{
    *pp = (*pp)->next;
}

Sint32 func_8CF10060(st_node *p)
{
    st_take(&p);                    /* R2b: address of a pointer parameter */
    return p->v;
}

Sint32 func_8CF10080(st_node *a, st_node *b)
{
    st_node *arr[2] = { a, b };     /* R2: pointers in an automatic initialiser list */
    struct { st_node *q; Sint32 k; } l = { b, 3 };
    return arr[0]->v + arr[1]->v * 10 + l.q->v * 100 + l.k;
}

Sint32 func_8CF100A0(st_node *p)
{
    struct loc { st_node *q; Sint32 k; } l;     /* R1: block-scope record with a pointer field */
    l.q = p;
    l.k = 2;
    return l.q->v * l.k;
}

void (*func_8CF100C0(Sint32 i))(st_node *)     /* R1: declarator returning a function pointer */
{
    return i ? func_8CF10000 : func_8CF10020;
}

Sint32 func_8CF100E0(st_node *p, Sint32 i)
{
    func_8CF100C0(i)(p);            /* R5: indirect call through the code map */
    return p->v;
}

Sint32 func_8CF10100(Sint32 *a, Sint32 d)
{
    Sint32 i = 0;
    a[i++] /= d;                    /* R6: /= %= on lvalues with side effects */
    a[i++] %= d;
    a[i] /= d - d;
    return i;
}

Sint32 func_8CF10120(st_node *p)
{
    st_fn f = p->fn;
    return (f < func_8CF10020) + 2 * (f >= func_8CF10000) + 4 * (f == st_local.fn);   /* R5 relational */
}

Sint32 func_8CF10140(st_node *g)
{
    st_node local;                  /* stack object stored into guest memory: segment 0x1D */
    local.v = 42;
    local.next = 0;
    g->next = &local;
    return g->next->v;
}

Sint32 func_8CF10160(void)
{
    const char *s = "xyz";          /* R4: string literal used as a pointer */
    return s[0] + st_names[1][2] + st_local.v + st_local.next->v + st_exported->v + st_shared;
}

Sint32 func_8CF10180(void)
{
    st_local.fn(&st_local);         /* R5: call through a function pointer held in pcdata */
    return st_local.v;
}

Sint32 func_8CF101A0(Uint32 w)
{
    st_node *p = (st_node *)w;      /* R6b: integer -> pointer decodes through the segment table */
    return p->v;
}

Sint32 func_8CF101C0(void)
{
    B_8C8F0100[1] = B_8C8F0100[0];  /* R2: raw guest-word copy */
    B_8C8F0100[2] = 0;
    return B_8C8F0100[1] == &D_8C8F0000;
}

Sint32 func_8CF101E0(Float f)
{
    return (Sint32)f;               /* R6: FTRC saturation */
}

st_node *(*func_8CF10200(void))[4]  /* R1: declarator returning a pointer to an array of pointers */
{
    return &B_8C8F0100;
}

Sint32 func_8CF10220(void)
{
    return (*func_8CF10200())[0]->v;
}
'''

SELFTEST_TU2 = r'''/* pclower selftest TU 2: an exported pcdata variable */
typedef int Sint32;
Sint32 st_shared = 9;

Sint32 func_8CF20000(void)
{
    return st_shared;
}
'''

SELFTEST_DRIVER = r'''/* pclower selftest driver (plain C, not lowered): a fake world for the lowered selftest TUs */
#include "sa1/guest.h"
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <windows.h>
#endif

typedef struct gnode { uint32_t next; int32_t v; uint32_t fn; } gnode;

int32_t func_8CF10040(void);
int32_t func_8CF10060(gnode *p);
int32_t func_8CF10080(gnode *a, gnode *b);
int32_t func_8CF100A0(gnode *p);
int32_t func_8CF100E0(gnode *p, int32_t i);
int32_t func_8CF10100(int32_t *a, int32_t d);
int32_t func_8CF10120(gnode *p);
int32_t func_8CF10140(gnode *g);
int32_t func_8CF10160(void);
int32_t func_8CF10180(void);
int32_t func_8CF101A0(uint32_t w);
int32_t func_8CF101C0(void);
int32_t func_8CF101E0(float f);
int32_t func_8CF10220(void);
int32_t func_8CF20000(void);
void func_8CF10000(gnode *n);
extern uint32_t sa1g_pcdv_st_exported, sa1g_pcdv_st_shared;

float *sa1_rt_xmtrx;
static int fails;
#define CHECK(c, what) do { if (!(c)) { printf("  FAIL %s (line %d)\n", what, __LINE__); fails++; } \
                            else { printf("  ok   %s\n", what); } } while (0)

void sa1g_fault_encode(const void *host) { printf("  FAULT encode %p\n", host); exit(3); }
void sa1g_fault_fn(uint32_t g, uint32_t sig) { printf("  FAULT code map %08X sig %08X\n", g, sig); exit(3); }
void sa1g_unimpl(uint32_t g, const char *name) { printf("  FAULT unimplemented %s %08X\n", name, g); exit(3); }

const struct sa1g_fnimage *sa1g_fn_active[1];      /* the code map's overlay images: none is running */

#if !defined(SA1_GA32)
uintptr_t sa1g_seg[32];
uint8_t *sa1g_ram_base;
static uintptr_t span[32];
uint32_t sa1g_encode_slow(const void *p)
{
    static const uint32_t order[] = { 0x18u, 0x1Du, 0x08u, 0x09u, 0x0Au, 0x0Bu };
    size_t i;
    for (i = 0; i < sizeof order / sizeof order[0]; i++) {
        uint32_t g = sa1g_encode_segment(p, order[i], sa1g_seg[order[i]], span[order[i]]);
        if (g) {
            return g;
        }
    }
    sa1g_fault_encode(p);
}
#endif

static uint8_t *G(uint32_t g) { return (uint8_t *)sa1_decode(g); }

int main(void)
{
    gnode *a, *b, *n;
    int32_t arr[3];
    uint8_t *col = NULL;
    setvbuf(stdout, NULL, _IONBF, 0);
#if defined(SA1_GA32)
    uint8_t *ram = (uint8_t *)VirtualAlloc((void *)0x8C000000u, 0x1000000u, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (ram != (uint8_t *)0x8C000000u) {
        printf("  cannot map DC RAM at 0x8C000000 (LARGE_ADDRESS_AWARE?)\n");
        return 2;
    }
    printf("pclower selftest, profile ga32\n");
#else
    uint8_t *ram = (uint8_t *)calloc(1, 0x1000000u);
    uint8_t *ext = (uint8_t *)calloc(1, 0x100000u);
    char marker = 0;
    col = (uint8_t *)calloc(1, 0x10000u);
    sa1g_ram_base = ram;
    sa1g_seg[0x0C] = (uintptr_t)ram;
    sa1g_seg[0x18] = (uintptr_t)ext;
    span[0x18] = 0x100000u;
    sa1g_seg[0x08] = (uintptr_t)col;
    span[0x08] = 0x10000u;
    sa1g_seg[0x1D] = ((uintptr_t)&marker - 0x800000u) & ~(uintptr_t)0xFFFF;   /* stack window */
    span[0x1D] = 0x1000000u;
    printf("pclower selftest, profile ga64 (pcdata needs %u bytes)\n", (unsigned)sa1g_pcd_size());
    CHECK(sa1g_pcd_size() > 0 && sa1g_pcd_size() < 0x100000u, "sa1g_pcd_size");
#endif
    sa1g_pcd_init();
    n = (gnode *)G(0x8C8F0000u);            /* D_8C8F0000 */
    n->v = 11;
    ((uint32_t *)G(0x8C8F0100u))[0] = 0x8C8F0000u;      /* B_8C8F0100[0] */
    a = (gnode *)G(0x8C8F1000u);
    b = (gnode *)G(0x8C8F1010u);
    a->v = 1;
    b->v = 5;
    a->next = 0x8C8F1010u;

    CHECK(func_8CF10040() == 6 && func_8CF10040() == 7, "R4 static local");
    CHECK(func_8CF10060(a) == 5, "R2b address of a pointer parameter");
    CHECK(func_8CF10080(a, b) == 1 + 50 + 500 + 3, "R2 pointers in initialiser lists");
    CHECK(func_8CF100A0(b) == 10, "R1 block-scope record");
    CHECK(func_8CF100E0(a, 1) == 2 && func_8CF100E0(a, 0) == 102, "R1 declarator returning a function pointer, R5 code map");
    arr[0] = 50; arr[1] = 20; arr[2] = 9;
    CHECK(func_8CF10100(arr, 7) == 2 && arr[0] == 7 && arr[1] == 6 && arr[2] == 0, "R6 compound division with side effects");
    a->fn = 0x8CF10000u;
    CHECK(func_8CF10120(a) == 1 + 2 + 4, "R5 relational comparison of function pointers");
    CHECK(func_8CF10140(a) == 42, "stack address stored into memory");
#if defined(SA1_GA32)
    CHECK(a->next != 0 && (a->next >> 24) != 0x9Du, "stack word is the host address (ga32)");
#else
    CHECK((a->next >> 24) == 0x9Du, "stack word in segment 0x1D (ga64)");
    CHECK((sa1g_pcdv_st_exported >> 24) == 0x98u && (sa1g_pcdv_st_shared >> 24) == 0x98u, "pcdata in segment 0x18");
    CHECK(sa1_encode(col + 0x20) == 0x88000020u && sa1_decode(0x88000020u) == (void *)(col + 0x20),
          "collision segment encode/decode");
    CHECK(sa1_encode(ext + 4) == 0x98000004u, "extension segment encode");
#endif
    CHECK(*(uint32_t *)G(sa1g_pcdv_st_exported) == 0x8C8F0000u, "R4 exported pointer initialised to an arena address");
    CHECK(func_8CF10160() == 'x' + 'e' + 7 + 7 + 11 + 9, "R4 strings, pcdata, cross-TU pcdata");
    CHECK(func_8CF10180() == 8, "R5 call through a function pointer in pcdata");
    CHECK(func_8CF20000() == 9, "R4 exported variable");
#if defined(SA1_GA32)
    ((gnode *)G(0x8C8F2000u))->v = 77;
    CHECK(func_8CF101A0(0x8C8F2000u) == 77, "R6b integer -> pointer (P1; the harness maps the P0/P2 mirrors too)");
#else
    ((gnode *)(col + 0x10))->v = 77;
    CHECK(func_8CF101A0(0x88000010u) == 77, "R6b integer -> pointer in the collision segment");
    ((gnode *)G(0x8C8F2000u))->v = 78;
    CHECK(func_8CF101A0(0x0C8F2000u) == 78 && func_8CF101A0(0xAC8F2000u) == 78, "P0/P2 aliases decode to the arena");
#endif
    CHECK(func_8CF101C0() == 1 && ((uint32_t *)G(0x8C8F0100u))[1] == 0x8C8F0000u
          && ((uint32_t *)G(0x8C8F0100u))[2] == 0u, "R2 raw copy and NULL store");
    CHECK(func_8CF101E0(3.0e9f) == 0x7FFFFFFF && func_8CF101E0(-3.0e9f) == (int32_t)0x80000000u
          && func_8CF101E0(-2.9f) == -2, "R6 FTRC");
    CHECK(func_8CF10220() == 11, "R1 declarator returning a pointer to an array of pointers");
    CHECK(sa1_fn_addr((const void *)func_8CF10000) == 0x8CF10000u, "code map reverse lookup");
    CHECK(sa1_fn_name(0x0CF10020u) != NULL && strcmp(sa1_fn_name(0x0CF10020u), "func_8CF10020") == 0,
          "code map P0 alias");
    CHECK(sa1g_fn_lookup(0x8CF10010u) == NULL, "code map miss");
    printf("pclower selftest: %d failure(s)\n", fails);
    return fails ? 1 : 0;
}
'''


def set_laa(exe):
    import struct
    b = bytearray(open(exe, "rb").read())
    e = struct.unpack_from("<I", b, 0x3C)[0]
    off = e + 4 + 18
    struct.pack_into("<H", b, off, struct.unpack_from("<H", b, off)[0] | 0x0020)
    open(exe, "wb").write(b)


def selftest(a):
    out = os.path.join(os.path.abspath(a.out), "selftest")
    src = os.path.join(out, "src")
    t1, t2, drv = (os.path.join(src, "pclower_st1.c"), os.path.join(src, "pclower_st2.c"),
                   os.path.join(src, "pclower_st_driver.c"))
    write(t1, SELFTEST_TU1)
    write(t2, SELFTEST_TU2)
    write(drv, SELFTEST_DRIVER)
    ns = argparse.Namespace(out=out, inc=[], allow_conflict=[], allow_all_conflicts=False, stub_unresolved=False,
                            extern_unresolved=False, allow_unpinned=True, no_layout=True, j=2, compile=False,
                            profile="all")
    rc = run_lowering(ns, [t1, t2], quiet=True)
    rep = json.load(open(os.path.join(out, "report.json")))
    errs = [e for e in rep["errors"] if e["rule"] != "R10"]
    for e in errs:
        print("  pclower error %s %s: %s" % (e["rule"], e.get("where", ""), e["msg"][:300]))
    print("selftest: lowering %s; rule counts: %s" % ("ok" if not errs else "FAILED", ", ".join(
        "%s %d" % (k, v) for k, v in sorted(rep["totals"].items()) if not k.startswith("R9") and v)))
    ok = not errs
    flags = COMPILE_CHECK_FLAGS + ["-I", os.path.join(ROOT, "include")]
    for prof, tname, tflags in COMPILE_TARGETS:
        cok = True
        pd = os.path.join(out, prof)
        srcs = [os.path.join(pd, "lowered", "pclower_st1.c"), os.path.join(pd, "lowered", "pclower_st2.c"),
                os.path.join(pd, "sa1g_fntab.c"), os.path.join(pd, "sa1g_pcdata.c")]
        objd = os.path.join(out, "obj_" + tname)
        os.makedirs(objd, exist_ok=True)
        objs = []
        for s in srcs + [drv]:
            o = os.path.join(objd, os.path.basename(s)[:-2] + ".o")
            fl = flags if s != drv else ["-O1", "-std=gnu11", "-I", os.path.join(ROOT, "include")]
            r = subprocess.run(gn.zig_cc() + tflags + fl + ["-c", s, "-o", o], capture_output=True, text=True,
                               encoding="utf-8", errors="replace")
            if r.returncode:
                print("selftest: %s does not compile for %s:\n%s" % (os.path.basename(s), tname, r.stderr[-1500:]))
                cok = False
            objs.append(o)
        ok = ok and cok
        if not cok or tname.endswith("linux"):
            print("selftest: %s compile %s" % (tname, "ok (no run on this host)" if cok else "FAILED"))
            continue
        exe = os.path.join(out, "pclower_selftest_%s.exe" % tname)
        r = subprocess.run(gn.zig_cc() + tflags[:2] + ["-o", exe] + objs, capture_output=True, text=True)
        if r.returncode:
            print("selftest: link for %s failed:\n%s" % (tname, r.stderr[-1500:]))
            ok = False
            continue
        if prof == "ga32":
            set_laa(exe)
        r = subprocess.run([exe], capture_output=True, text=True)
        print(r.stdout.rstrip())
        if r.returncode:
            ok = False
    print("pclower selftest: %s" % ("PASS" if ok else "FAILED"))
    return 0 if ok else 1


def revalidate(a):
    """R10: what has to hold before a new zig/clang is pinned"""
    out = os.path.abspath(a.out)
    zv, cv = zig_versions()
    fp = ast_fingerprint(out)
    print("toolchain: zig %s, %s, AST fingerprint %s" % (zv, cv, fp))
    print("pinned:    zig %s, %s, AST fingerprint %s" % (PINNED_ZIG, PINNED_CLANG, PINNED_AST_FINGERPRINT))
    ok = selftest(a) == 0
    print("revalidate: step 1 (selftest) %s" % ("PASS" if ok else "FAILED"))
    sample = ["src/1st_read/code_8C098444.c", "src/1st_read/code_8C098998.c", "src/1st_read/code_8C098B0C.c",
              "src/1st_read/code_8C011EA6.c", "src/1st_read/code_8C0DE8C0.c", "src/1st_read/code_8C02CEF4.c",
              "src/1st_read/code_8C0A77BC.c", "src/1st_read/code_8C06C350.c"]
    sample = [os.path.join(ROOT, s) for s in sample if os.path.exists(os.path.join(ROOT, s))]
    ns = argparse.Namespace(out=os.path.join(out, "revalidate"), inc=[], allow_conflict=[], allow_all_conflicts=True,
                            stub_unresolved=True, extern_unresolved=False, allow_unpinned=True, no_layout=False,
                            j=8, compile=True, profile="all")
    rc = run_lowering(ns, sample, quiet=True)
    rep = json.load(open(os.path.join(out, "revalidate", "report.json")))
    errs = [e for e in rep["errors"] if e["rule"] != "R10"]
    for e in errs[:40]:
        print("  error %s %s: %s" % (e["rule"], e.get("where", ""), e["msg"][:300]))
    print("revalidate: step 2 (%d sample TUs + layout asserts, 3 targets) %s" % (len(sample), "PASS" if not errs else "FAILED"))
    ok = ok and not errs
    print("revalidate: step 3 (manual, the V1 gate of docs/libsa1_integration.md 2.3):\n"
          "    py -3.12 harness/build.py --non-matching --pc --ga --tag reval\n"
          "    py -3.12 harness/tests/sim/regress.py --route --exe harness/build/sa1h_reval.exe   (all identical)\n"
          "    py -3.12 harness/tools/route_matrix.py --exe harness/build/sa1h_reval.exe          (no new failures)\n"
          "    py -3.12 tools/pclower.py --all -j 16 --compile --stub-unresolved                  (0 errors)")
    if ok and a.write_pin:
        p = os.path.abspath(__file__)
        s = open(p, encoding="utf-8").read()
        s = re.sub(r'^PINNED_ZIG = ".*"$', 'PINNED_ZIG = "%s"' % zv, s, flags=re.M)
        s = re.sub(r'^PINNED_CLANG = ".*"$', 'PINNED_CLANG = "%s"' % cv, s, flags=re.M)
        s = re.sub(r'^PINNED_AST_FINGERPRINT = ".*"$', 'PINNED_AST_FINGERPRINT = "%s"' % fp, s, flags=re.M)
        with open(p, "w", encoding="utf-8", newline="\n") as f:
            f.write(s)
        print("revalidate: pin written to tools/pclower.py (run step 3 before committing it)")
    elif a.write_pin:
        print("revalidate: not pinned (a step failed)")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
