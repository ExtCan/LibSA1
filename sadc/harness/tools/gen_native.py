#!/usr/bin/env python3
"""Generate the native side of the harness from decompiled C files.

For every selected src/1st_read/*.c file this script

  * parses the file with clang (zig cc -Xclang -ast-dump=json) using the
    native flags (32-bit x86, include/ + src/, the harness pre-include that
    turns ASM_STUB(...) into a plain declaration),
  * writes a copy with every decimal floating literal replaced by the hex
    float of its round-toward-zero single precision value (SHC -round=zero
    -fpu=single semantics; clang would round to nearest and treat unsuffixed
    literals as double),
  * collects the functions it defines and the external functions / data it
    references.

Then it writes, into the output directory:

  native_abs.c       absolute symbol definitions (".set _D_8C..., 0x8C...") for
                     every referenced external data symbol, from
                     build/1ST_READ.symbols.txt
  native_thunks.c    one native function per referenced external function that
                     is not compiled natively: marshals its arguments per the
                     SHC ABI and runs the original through sh4_invoke
  native_registry.c  sh4_native_fns[] (natively compiled functions with their
                     guest address and a guest-regs -> native-call adapter)
                     and sh4_thunks[]
  native_manifest.json
  native_src/*.c     the rewritten sources (compiled by build.py)

    py -3.12 harness/tools/gen_native.py --out DIR [--non-matching] [--pc] [--inc DIR]... [files...|all]

The libsa1 runtime (libsa1/src/rt/*.c, build.py --rt) goes through the same
path: its files are passed like decompiled files and --inc names their header
directory, so SDK-named functions (njSin ...) get adapters at their SDK
addresses from build/1ST_READ.symbols.txt.

Overlay images (src/overlays/<dir>/*.c, e.g. b_chaos7): overlays that share a
load slot reuse DC code addresses, so, as tools/pclower.py does, every function
an overlay file defines, or references inside its image's code range, gets the
C name <IMAGE>__<name> (a #define in the native copy): a 1ST_READ file's call of
func_0C900480 stays a thunk into the interpreter instead of linking to the
B_CHAOS7 definition.  The registry records each native function's image and,
per image, the code ranges (config/overlays/<dir>.yaml) and the CRC-32 of those
bytes in baserom/overlays/<DIR>.BIN (image_guard.h): the trace command routes an
overlay function only while that image is the one loaded (the image guard).
"""
import argparse
import fractions
import json
import os
import re
import struct
import subprocess
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
HARNESS = os.path.abspath(os.path.join(HERE, ".."))
ROOT = os.path.abspath(os.path.join(HARNESS, ".."))

LIBC_NAMES = {"memcpy", "memset", "memmove", "memcmp", "strcpy", "strncpy", "strlen", "strcmp", "strcat",
              "sprintf", "printf", "vsprintf", "sin", "cos", "tan", "sqrt", "atan2", "atan", "asin", "acos",
              "sinf", "cosf", "sqrtf", "floor", "ceil", "fabs", "pow", "exp", "log", "rand", "srand", "malloc",
              "free", "abs", "labs", "qsort", "exit", "abort"}

# functions the harness itself provides to native code (sh4_intrin.c)
NATIVE_HELPERS = {"__asm_stub_sink", "sa1_tmu_tcnt1", "njFraction",
                  "sa1g_fatal"}   # pc_runtime.c: libsa1's game-detected fatal state (CCL_Init, code_8C02F27C.c)
# data the harness provides: harness/src/pc_runtime.c (sa1_tmu), the FSCA table
# libsa1/src/rt/rt_fsca_q.c (sh4_fsca_q, include/pc/sh4_fpmodel.h) and the
# runtime's XMTRX alias of the interpreter's XF bank (sh4_intrin.c, rt.h)
NATIVE_DATA = {"sa1_tmu", "sh4_fsca_q", "sa1h_xmtrx"}
# C descriptions of hand-written routines with a private register convention whose callers are all
# hand-written assembly: no C caller shares the convention, so an ABI adapter would hand them garbage.
# They get no adapter: never routed (--route, --route-only), the original runs.  (The helpers of
# code_8C02D060.c / code_8C02D410.c have C callers and are file-local or routed with them instead.)
# The sprite module is reached since the references have the screen (2026-10-20,
# docs/research/visibility_fidelity.md): with the zero screen every sprite was rejected before
# its store-queue writes.
NO_ROUTE = {
    "func_8C09E880": "sprite module (asm caller 8C09E778): store-queue vertex cursor in and out in R2",
    "func_8C09EB08": "sprite module (asm caller 8C09E778): store-queue vertex cursor in and out in R2",
    "func_8C09EE44": "sprite module (caller 8C09EB08): store-queue pointer in R2, clip mask R4, corners R5",
}

# extra include directories (--inc, e.g. libsa1/src/rt): searched by the AST
# pass, and their declarations count as project declarations (not host headers)
EXTRA_INC = []

# --pc: parse the sources as libsa1 compiles them (-DTARGET_PC)
PC_MODE = False

# ---------------------------------------------------------------- overlay images (image guard)

OVERLAY_SRC = os.path.join(ROOT, "src", "overlays")
BASE_IMAGE = "1ST_READ"


def image_of_path(path):
    """the overlay image of a decompiled file: the upper-cased directory under src/overlays (B_CHAOS7),
    or BASE_IMAGE"""
    p = os.path.normcase(os.path.abspath(path))
    o = os.path.normcase(os.path.abspath(OVERLAY_SRC)) + os.sep
    if p.startswith(o):
        return p[len(o):].split(os.sep)[0].upper()
    return BASE_IMAGE


_GUARD_INFO = {}


def image_guard_info(img):
    """config/overlays/<img>.yaml code ranges and the CRC-32 of those bytes (in order) in its BIN:
    {"name", "ranges": [(lo, hi)], "crc32", "code_bytes", "error"}; code_bytes 0 when the BIN or the
    config is missing (the guard then never routes the image)"""
    if img in _GUARD_INFO:
        return _GUARD_INFO[img]
    info = {"name": img, "ranges": [], "crc32": 0, "code_bytes": 0, "error": None}
    path = os.path.join(ROOT, "config", "overlays", img.lower() + ".yaml")
    try:
        text = open(path, encoding="utf-8").read()
        info["ranges"] = [(int(a, 16), int(b, 16)) for a, b in re.findall(
            r"^\s*-\s*\[\s*0x([0-9A-Fa-f]+)\s*,\s*0x([0-9A-Fa-f]+)\s*\]", text, re.M)]
        mt = re.search(r"^target:\s*(\S+)", text, re.M)
        mb = re.search(r"^base:\s*0x([0-9A-Fa-f]+)", text, re.M)
        if not info["ranges"] or not mt or not mb:
            raise ValueError("no code_ranges / target / base in %s" % path)
        data = open(os.path.join(ROOT, mt.group(1)), "rb").read()
        base = int(mb.group(1), 16)
        crc, n = 0, 0
        for lo, hi in info["ranges"]:
            if lo < base or hi - base > len(data) or hi <= lo:
                raise ValueError("code range %08X-%08X outside %s" % (lo, hi, mt.group(1)))
            crc = zlib.crc32(data[lo - base:hi - base], crc)
            n += hi - lo
        info["crc32"], info["code_bytes"] = crc & 0xFFFFFFFF, n
    except (OSError, ValueError) as ex:
        info["error"] = str(ex)
        info["code_bytes"] = 0
    _GUARD_INFO[img] = info
    return info


def in_image(addr, img):
    """addr (any mirror) lies in a code range of overlay image img"""
    if addr is None or img == BASE_IMAGE:
        return False
    a = addr & 0x1FFFFFFF
    return any((lo & 0x1FFFFFFF) <= a < (hi & 0x1FFFFFFF) for lo, hi in image_guard_info(img)["ranges"])


def emit_image_guard(f, native_images):
    """the image guard tables of image_guard.h for native_registry.c; native_images: the image name of
    every sh4_native_fns[] entry, in table order (BASE_IMAGE for 1ST_READ / the runtime)"""
    imgs = sorted({i for i in native_images if i != BASE_IMAGE})
    f.write("\n/* overlay images of the native functions (image_guard.h, harness/README.md \"Overlay image guard\") */\n")
    f.write('#include "image_guard.h"\n')
    ranges, rows = [], []
    for img in imgs:
        info = image_guard_info(img)
        rows.append('    { "%s", 0x%08Xu, %du, %d, %d },%s' % (
            img, info["crc32"], info["code_bytes"], len(ranges), len(info["ranges"]),
            (" /* %s */" % info["error"].replace("*/", "* /")) if info["error"] else ""))
        ranges += info["ranges"]
    f.write("const sa1h_fn_image sa1h_fn_images[] = {\n")
    f.write("\n".join(rows) + "\n" if rows else '    { "", 0u, 0u, 0, 0 },\n')
    f.write("};\nconst int sa1h_fn_image_count = %d;\n" % len(imgs))
    f.write("const uint32_t sa1h_fn_image_ranges[][2] = {\n")
    f.write("".join("    { 0x%08Xu, 0x%08Xu },\n" % r for r in ranges) or "    { 0u, 0u },\n")
    f.write("};\n")
    idx = [imgs.index(i) if i != BASE_IMAGE else -1 for i in native_images]
    f.write("const signed char sa1h_native_fn_image[] = {")
    for k in range(0, max(1, len(idx)), 32):
        f.write("\n    " + ", ".join("%d" % v for v in (idx[k:k + 32] or [-1])) + ",")
    f.write("\n};\n")

# ---------------------------------------------------------------- types

BASIC = {
    "char": "i8", "signed char": "i8", "unsigned char": "u8",
    "short": "i16", "short int": "i16", "signed short": "i16", "signed short int": "i16",
    "unsigned short": "u16", "unsigned short int": "u16",
    "int": "i32", "signed int": "i32", "signed": "i32", "long": "i32", "long int": "i32",
    "signed long": "i32", "signed long int": "i32",
    "unsigned int": "u32", "unsigned": "u32", "unsigned long": "u32", "unsigned long int": "u32",
    "float": "f32", "double": "f64", "long double": "f64", "void": "v", "_Bool": "u8",
    "long long": "i64", "unsigned long long": "u64", "long long int": "i64",
}
CTYPE = {"i8": "signed char", "u8": "unsigned char", "i16": "short", "u16": "unsigned short",
         "i32": "int32_t", "u32": "uint32_t", "p": "void *", "f32": "float", "f64": "double", "v": "void"}


def strip_quals(t):
    t = t.strip()
    t = re.sub(r"\b(const|volatile|restrict|__restrict)\b", "", t)
    return re.sub(r"\s+", " ", t).strip()


class Types:
    def __init__(self):
        self.typedefs = {}

    def classify(self, t, depth=0):
        t = strip_quals(t)
        if "(*)" in t or t.endswith("*") or "[" in t or "(^)" in t:
            return "p"
        if t in BASIC:
            return BASIC[t]
        if t.startswith("enum "):
            return "i32"
        if t.startswith("struct ") or t.startswith("union "):
            return "s"
        if t in self.typedefs and depth < 20:
            return self.classify(self.typedefs[t], depth + 1)
        return "?"


def split_fn_type(qt):
    """'RET (A, B)' -> ('RET', ['A', 'B'], variadic, noproto)"""
    qt = qt.strip()
    # the parameter list is the last top-level parenthesised group
    depth = 0
    end = len(qt) - 1
    assert qt[end] == ")", qt
    for i in range(end, -1, -1):
        ch = qt[i]
        if ch == ")":
            depth += 1
        elif ch == "(":
            depth -= 1
            if depth == 0:
                start = i
                break
    ret = qt[:start].strip()
    inner = qt[start + 1:end].strip()
    params, depth, cur = [], 0, ""
    for ch in inner:
        if ch in "([":
            depth += 1
        elif ch in ")]":
            depth -= 1
        if ch == "," and depth == 0:
            params.append(cur.strip())
            cur = ""
        else:
            cur += ch
    if cur.strip():
        params.append(cur.strip())
    variadic = bool(params) and params[-1] == "..."
    if variadic:
        params = params[:-1]
    noproto = inner == ""          # 'f()' : no prototype
    if params == ["void"]:
        params = []
    return ret, params, variadic, noproto

# ---------------------------------------------------------------- floats


def rz_float32(fr):
    """exact Fraction -> float32 value (as Python float) rounded toward zero"""
    if fr == 0:
        return 0.0
    neg = fr < 0
    a = -fr if neg else fr
    # exponent e with 2^e <= a < 2^(e+1)
    e = a.numerator.bit_length() - a.denominator.bit_length()
    if fractions.Fraction(2) ** e > a:
        e -= 1
    elif fractions.Fraction(2) ** (e + 1) <= a:
        e += 1
    if e > 127:
        v = struct.unpack("<f", struct.pack("<I", 0x7F7FFFFF))[0]   # RZ overflow -> FLT_MAX
    else:
        q = max(e, -126) - 23                   # quantum (denormals below 2^-126)
        m = (a / (fractions.Fraction(2) ** q)).__floor__() if q >= 0 else (a * (2 ** -q)).__floor__()
        v = float(m) * (2.0 ** q)
    return -v if neg else v


def rn_float32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


PPNUM = re.compile(r"\.?[0-9](?:[eEpP][+-]|[0-9A-Za-z_.])*")


def rewrite_floats(src):
    """Replace decimal floating literals by RZ float32 hex literals.
    Returns (new_source, changes) where changes lists literals whose RZ value
    differs from clang's nearest rounding."""
    out = []
    changes = []
    i, n = 0, len(src)
    line_start = True
    while i < n:
        ch = src[i]
        if line_start:
            m = re.match(r"[ \t]*#[ \t]*(include|line|pragma|error|warning)\b[^\n]*", src[i:])
            if m:
                out.append(m.group(0))
                i += len(m.group(0))
                line_start = False
                continue
        line_start = False
        if ch == "\n":
            out.append(ch)
            i += 1
            line_start = True
            continue
        if src.startswith("/*", i):
            j = src.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append(src[i:j])
            i = j
            continue
        if src.startswith("//", i):
            j = src.find("\n", i)
            j = n if j < 0 else j
            out.append(src[i:j])
            i = j
            continue
        if ch in "\"'":
            j = i + 1
            while j < n and src[j] != ch:
                j += 2 if src[j] == "\\" else 1
            out.append(src[i:j + 1])
            i = j + 1
            continue
        if ch.isalpha() or ch == "_":
            m = re.match(r"[A-Za-z_][A-Za-z0-9_]*", src[i:])
            out.append(m.group(0))
            i += len(m.group(0))
            continue
        if ch.isdigit() or (ch == "." and i + 1 < n and src[i + 1].isdigit()):
            m = PPNUM.match(src, i)
            tok = m.group(0)
            i = m.end()
            low = tok.lower()
            if low.startswith("0x") or ("." not in tok and "e" not in low):
                out.append(tok)
                continue
            body = tok.rstrip("fFlL")
            try:
                fr = fractions.Fraction(body)
            except ValueError:
                out.append(tok)
                continue
            v = rz_float32(fr)
            rn = rn_float32(float(fr))
            if v != rn or not tok.endswith(("f", "F")):
                changes.append({"literal": tok, "rz": v.hex(), "clang": rn.hex(),
                                "differs": v != rn, "was_double": not tok.endswith(("f", "F"))})
            out.append("(" + v.hex() + "f)")
            continue
        out.append(ch)
        i += 1
    return "".join(out), changes

# ---------------------------------------------------------------- AST


def zig_cc():
    return [sys.executable, "-m", "ziglang", "cc"]


def native_cflags(non_matching, extra_inc=()):
    f = ["-target", "x86-windows-gnu", "-include", os.path.join(HARNESS, "src", "harness_pre.h"),
         "-I", os.path.join(ROOT, "include"), "-I", os.path.join(ROOT, "src")]
    for d in extra_inc:
        f += ["-I", d]
    if non_matching:
        f += ["-DNON_MATCHING"]
    if PC_MODE:
        f += ["-DTARGET_PC"]
    return f


def ast_of(path, non_matching):
    cmd = zig_cc() + native_cflags(non_matching, [os.path.dirname(path)] + EXTRA_INC) + [
        "-fsyntax-only", "-w", "-Wno-error=implicit-function-declaration", "-Wno-error=implicit-int", "-O2", "-fno-sanitize=all", "-Xclang", "-ast-dump=json", path]
    for attempt in range(4):
        r = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace")
        if "sub-compilation" not in r.stderr and "failed to check cache" not in r.stderr:
            break       # zig's cache occasionally fails transiently: retry
    # zig cc reports "error: FileNotFound" after -fsyntax-only (no depfile is
    # written); only clang's own diagnostics count
    errs = [l for l in r.stderr.splitlines() if "error:" in l and "error: FileNotFound" not in l]
    if errs or not r.stdout.strip():
        return None, r.stderr
    try:
        return json.loads(r.stdout), r.stderr
    except ValueError:
        return None, r.stderr


def scan_ast(ast, types):
    """-> dict name -> info (functions) and dict name -> info (variables)"""
    funcs, vars_ = {}, {}
    cur_file = None
    roots = tuple(os.path.normcase(os.path.abspath(d)) for d in [ROOT] + EXTRA_INC)
    for node in ast.get("inner", []):
        loc = node.get("loc", {})
        lf = loc.get("file") or loc.get("expansionLoc", {}).get("file")
        if lf:
            cur_file = lf
        k = node.get("kind")
        # declarations from the host's system headers (TARGET_PC includes
        # <math.h>/<stdint.h>): the host C library provides them
        if cur_file and k in ("FunctionDecl", "VarDecl") and \
                not os.path.normcase(os.path.abspath(cur_file)).startswith(roots):
            continue
        # implicit declarations: clang's own builtins are skipped, but a call
        # to an undeclared function (accepted by SHC's C89) is kept: it gets a
        # no-prototype thunk
        if node.get("isImplicit") and not (k == "FunctionDecl" and not node["name"].startswith("__builtin")
                                            and (node.get("isUsed") or node.get("isReferenced"))):
            continue
        if k == "TypedefDecl":
            t = node.get("type", {})
            types.typedefs[node["name"]] = t.get("desugaredQualType", t.get("qualType", ""))
            continue
        if k == "FunctionDecl":
            name = node["name"]
            f = funcs.setdefault(name, {"name": name, "defined": False, "used": False, "static": False,
                                        "type": None, "params": None})
            if any(i.get("kind") == "CompoundStmt" for i in node.get("inner", [])):
                f["defined"] = True
            if node.get("storageClass") == "static":
                f["static"] = True
            if node.get("isUsed") or node.get("isReferenced"):
                f["used"] = True
            f["type"] = node["type"]["qualType"]
            ps = [p for p in node.get("inner", []) if p.get("kind") == "ParmVarDecl"]
            if ps or f["params"] is None:
                f["params"] = [p["type"].get("desugaredQualType", p["type"]["qualType"]) for p in ps]
            continue
        if k == "VarDecl":
            name = node["name"]
            v = vars_.setdefault(name, {"name": name, "extern": False, "defined": False, "used": False,
                                        "static": False, "type": node["type"]["qualType"]})
            sc = node.get("storageClass")
            if sc == "extern" and not node.get("init"):
                v["extern"] = True
            else:
                v["defined"] = True
            if sc == "static":
                v["static"] = True
            if node.get("isUsed") or node.get("isReferenced"):
                v["used"] = True
    # calls to undeclared functions (C89 implicit declarations) only show up
    # as referencedDecl of a DeclRefExpr
    stack = list(ast.get("inner", []))
    while stack:
        n = stack.pop()
        refd = n.get("referencedDecl")
        if refd and refd.get("kind") == "FunctionDecl" and refd.get("name") not in funcs and \
                not refd.get("name", "").startswith("__builtin"):
            funcs[refd["name"]] = {"name": refd["name"], "defined": False, "used": True, "static": False,
                                   "type": refd["type"]["qualType"], "params": None, "implicit": True}
        stack.extend(n.get("inner", []))
    return funcs, vars_


def signature(types, fn):
    ret, params, variadic, noproto = split_fn_type(fn["type"])
    rc = types.classify(ret)
    pc = [types.classify(p) for p in (fn["params"] if fn["params"] is not None else params)]
    if not fn["params"] and params:
        pc = [types.classify(p) for p in params]
    sig = rc + ":" + ",".join(pc + (["..."] if variadic else []))
    ok = "?" not in pc and rc != "?" and "s" not in pc and rc != "s" and "i64" not in pc and "u64" not in pc
    return {"ret": rc, "params": pc, "variadic": variadic, "noproto": noproto, "sig": sig, "ok": ok}

# ---------------------------------------------------------------- symbols


def load_symbols():
    syms = {}
    path = os.path.join(ROOT, "build", "1ST_READ.symbols.txt")
    with open(path) as f:
        for line in f:
            m = re.match(r"\s*([A-Za-z0-9_$.]+)\s*=\s*0x([0-9A-Fa-f]+)", line)
            if m:
                syms[m.group(1)] = int(m.group(2), 16)
    # overlay symbols (prefixed with the overlay name, e.g. B_CHAOS7_Super_Sonic):
    # only valid while that overlay is loaded, like on the console
    bdir = os.path.join(ROOT, "build")
    with open(os.path.join(ROOT, "config", "asset_symbols.txt")) as f:
        for line in f:
            m = re.match(r"\s*([A-Za-z0-9_$.]+)\s*=\s*0x([0-9A-Fa-f]+)", line)
            if m:
                syms.setdefault(m.group(1), int(m.group(2), 16))
    for fn in sorted(os.listdir(bdir)):
        if fn.endswith(".symbols.txt") and fn != "1ST_READ.symbols.txt":
            with open(os.path.join(bdir, fn)) as f:
                for line in f:
                    m = re.match(r"\s*([A-Za-z0-9_$.]+)\s*=\s*0x([0-9A-Fa-f]+)", line)
                    if m:
                        syms.setdefault(m.group(1), int(m.group(2), 16))
    return syms


def addr_of(name, syms, data=False):
    if name in syms:
        return syms[name]
    m = re.match(r"^(?:func|D|B|lbl)_([0-9A-Fa-f]{8})$", name)
    if m:
        return int(m.group(1), 16)
    # data-file / overlay names that end in their address (S_MOT_object_0CC0252C);
    # data only: functions named like that are layout fillers (tu_gap_8C0BBDD6)
    # functions only in the overlay range 0x0Cxxxxxx (B_CHAOS7_exec_0C900480); 8C-suffixed function names are
    # layout fillers of the matching build
    m = re.match(r"^[A-Za-z]\w*_((?:0C|8C)[0-9A-Fa-f]{6})$", name) if data else \
        re.match(r"^[A-Za-z]\w*_(0C[0-9A-Fa-f]{6})$", name)
    if m:
        return int(m.group(1), 16)
    return None

# ---------------------------------------------------------------- emit


def guest_arg_regs(params):
    """SHC ABI placement: list of ('r', n) / ('f', n) / ('s', k) per param"""
    ni = nf = ns = 0
    out = []
    for p in params:
        if p in ("f32", "f64"):
            if nf < 8:
                out.append(("f", 4 + nf))
                nf += 1
            else:
                out.append(("s", ns))
                ns += 1
        else:
            if ni < 4:
                out.append(("r", 4 + ni))
                ni += 1
            else:
                out.append(("s", ns))
                ns += 1
    return out

def arg_conv_to_guest(cls, expr):
    if cls == "f32":
        return "sh4_af(%s)" % expr
    if cls == "f64":
        return "sh4_af((float)%s)" % expr
    if cls == "p":
        return "sh4_ai((uint32_t)(uintptr_t)%s)" % expr
    if cls in ("i8", "i16", "i32"):
        return "sh4_ai((uint32_t)(int32_t)%s)" % expr
    return "sh4_ai((uint32_t)%s)" % expr


def ret_from_guest(cls):
    if cls == "v":
        return None
    if cls == "f32":
        return "f0_"
    if cls == "f64":
        return "(double)f0_"
    if cls == "p":
        return "(void *)(uintptr_t)r0_"
    return "(%s)r0_" % CTYPE[cls]


def emit_thunk(name, guest, s, qualtype):
    lines = ["/* %s: %s */" % (name, qualtype)]
    params = s["params"]
    if not s["ok"] and not s["noproto"]:
        lines.append("void %s(void) { sh4_unsupported_call(\"%s\", 0x%08Xu); }" % (name, name, guest))
        return "\n".join(lines)
    if s["noproto"]:
        ret = CTYPE.get(s["ret"], "uint32_t") if s["ret"] in CTYPE else "uint32_t"
        lines.append("%s %s(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)" % (ret, name))
        lines.append("{")
        lines.append("    sh4_arg A_[4];")
        lines.append("    uint32_t r0_ = 0; float f0_ = 0.0f;")
        lines.append("    A_[0] = sh4_ai(a0); A_[1] = sh4_ai(a1); A_[2] = sh4_ai(a2); A_[3] = sh4_ai(a3);")
        lines.append("    sh4_invoke(0x%08Xu, A_, 4, &r0_, &f0_);" % guest)
        lines.append("    (void)f0_;")
        r = ret_from_guest(s["ret"])
        if r:
            lines.append("    return %s;" % r)
        lines.append("}")
        return "\n".join(lines)
    decl = ", ".join("%s a%d" % (CTYPE[q], i) for i, q in enumerate(params))
    if s["variadic"]:
        decl = (decl + ", ...") if decl else "..."
    if not decl:
        decl = "void"
    lines.append("%s %s(%s)" % (CTYPE[s["ret"]], name, decl))
    lines.append("{")
    if s["variadic"]:
        lines.append("    uint32_t w_[24]; int n_ = 0, i_; uint32_t r0_ = 0; float f0_ = 0.0f;")
        lines.append("    va_list ap_;")
        for i, q in enumerate(params):
            lines.append("    w_[n_++] = %s.v;" % arg_conv_to_guest(q, "a%d" % i))
        if params:
            lines.append("    va_start(ap_, a%d);" % (len(params) - 1))
            lines.append("    for (i_ = 0; i_ < 8; i_++) w_[n_++] = va_arg(ap_, uint32_t);")
            lines.append("    va_end(ap_);")
        lines.append("    sh4_invoke_varargs(0x%08Xu, w_, n_, &r0_);" % guest)
        lines.append("    f0_ = SH4.fr.f[0];")
    else:
        lines.append("    sh4_arg A_[%d];" % max(1, len(params)))
        lines.append("    uint32_t r0_ = 0; float f0_ = 0.0f;")
        for i, q in enumerate(params):
            lines.append("    A_[%d] = %s;" % (i, arg_conv_to_guest(q, "a%d" % i)))
        lines.append("    sh4_invoke(0x%08Xu, A_, %d, &r0_, &f0_);" % (guest, len(params)))
    lines.append("    (void)r0_; (void)f0_;")
    r = ret_from_guest(s["ret"])
    if r:
        lines.append("    return %s;" % r)
    lines.append("}")
    return "\n".join(lines)


def guest_to_native_arg(cls, place):
    kind, n = place
    if kind == "r":
        src = "c->r[%d]" % n
    elif kind == "f":
        src = "c->fr.u[%d]" % n
    else:
        src = "sh4_read32(c->r[15] + %du)" % (4 * n)
    if cls == "f32":
        return "sh4_u2f(%s)" % src
    if cls == "f64":
        return "(double)sh4_u2f(%s)" % src
    if cls == "p":
        return "(void *)(uintptr_t)%s" % src
    return "(%s)%s" % (CTYPE[cls], src)


def emit_adapter(name, s):
    params = s["params"]
    places = guest_arg_regs(params)
    decl = ", ".join(CTYPE[q] for q in params) or "void"
    lines = ["extern %s %s(%s);" % (CTYPE[s["ret"]], name, decl),
             "static void adapt_%s(sh4_cpu *c)" % name, "{"]
    call = "%s(%s)" % (name, ", ".join(guest_to_native_arg(q, pl) for q, pl in zip(params, places)))
    r = s["ret"]
    if r == "v":
        lines.append("    %s;" % call)
    elif r == "f32":
        lines.append("    c->fr.f[0] = %s;" % call)
    elif r == "f64":
        lines.append("    c->fr.f[0] = (float)%s;" % call)
    elif r == "p":
        lines.append("    c->r[0] = (uint32_t)(uintptr_t)%s;" % call)
    elif r in ("i8", "i16", "i32"):
        lines.append("    c->r[0] = (uint32_t)(int32_t)%s;" % call)
    else:
        lines.append("    c->r[0] = (uint32_t)%s;" % call)
    if not params:
        lines.append("    (void)c;")
    lines.append("}")
    return "\n".join(lines)


HEADER = """/* Generated by harness/tools/gen_native.py. Do not edit. */
#include "sh4.h"
#include <stdarg.h>
#include <stdint.h>
"""


def gen_overlays(out_dir):
    path = os.path.join(ROOT, "baserom", "overlays.json")
    lines = [HEADER, '#include "harness.h"', "const sh4_overlay sh4_overlays[] = {"]
    n = 0
    if os.path.exists(path):
        for ov in json.load(open(path)):
            if not ov.get("bin_path") or not ov.get("load_addr"):
                continue
            lines.append('    { "%s", "%s", 0x%08Xu },' % (ov["name"], ov["bin_path"].replace("\\", "/"),
                                                       int(ov["load_addr"], 16)))
            n += 1
    if n == 0:
        lines.append('    { "", "", 0 },')
    lines.append("};")
    lines.append("const int sh4_overlay_count = %d;" % n)
    with open(os.path.join(out_dir, "overlays.c"), "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")

def gen_functions(out_dir):
    """table of all functions of 1ST_READ (address, size, leaf flag) from
    build/1ST_READ.xrefs.json, for the leaf smoke test"""
    path = os.path.join(ROOT, "build", "1ST_READ.xrefs.json")
    lines = [HEADER, '#include "harness.h"', "const sh4_funcinfo sh4_functions[] = {"]
    n = 0
    if os.path.exists(path):
        xr = json.load(open(path))
        for k in sorted(xr, key=lambda s: int(s, 16)):
            v = xr[k]
            leaf = 0 if v.get("callees") else 1
            lines.append('    { 0x%sU, %du, %d, "%s" },' % (k, v.get("size", 0), leaf, v.get("name", "")))
            n += 1
    if n == 0:
        lines.append('    { 0, 0, 0, "" },')
    lines.append("};")
    lines.append("const int sh4_function_count = %d;" % n)
    with open(os.path.join(out_dir, "functions.c"), "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--non-matching", action="store_true")
    ap.add_argument("--exclude", action="append", default=[])
    ap.add_argument("--pc", action="store_true", help="-DTARGET_PC (the libsa1 configuration)")
    ap.add_argument("--inc", action="append", default=[],
                    help="extra include directory of the given files (libsa1/src/rt for --rt builds)")
    ap.add_argument("files", nargs="*")
    a = ap.parse_args()
    global PC_MODE
    PC_MODE = a.pc
    EXTRA_INC.extend(os.path.abspath(d) for d in a.inc)

    out = os.path.abspath(a.out)
    src_out = os.path.join(out, "native_src")
    os.makedirs(src_out, exist_ok=True)
    for old in os.listdir(src_out):
        if old.endswith(".c"):
            os.unlink(os.path.join(src_out, old))
    gen_overlays(out)
    gen_functions(out)

    files = []
    for f in a.files:
        if f == "all":
            d = os.path.join(ROOT, "src", "1st_read")
            files += sorted(os.path.join(d, x) for x in os.listdir(d) if x.endswith(".c"))
        else:
            files.append(os.path.abspath(f))
    excl = {os.path.basename(x) for x in a.exclude}
    files = [f for f in files if os.path.basename(f) not in excl]

    syms = load_symbols()
    manifest = {"non_matching": a.non_matching, "target_pc": a.pc, "files": {}}
    parsed = {}
    for path in files:
        base = os.path.basename(path)
        text = open(path, encoding="utf-8", errors="replace").read()
        new, changes = rewrite_floats(text)
        rel = os.path.relpath(path, ROOT).replace("\\", "/")
        dst = os.path.join(src_out, base)
        with open(dst, "w", newline="\n", encoding="utf-8") as f:
            f.write(new + '\n/* native copy of %s: float literals rewritten (RZ, single) */\n' % rel)
        ast, err = ast_of(dst, a.non_matching)
        entry = {"source": rel, "float_literals": changes}
        manifest["files"][base] = entry
        if ast is None:
            entry["status"] = "excluded: does not compile natively"
            entry["error"] = err[-2000:]
            os.unlink(dst)
            continue
        types = Types()
        funcs, vars_ = scan_ast(ast, types)
        parsed[base] = (path, types, funcs, vars_)

    # overlay images: per-image C names (as tools/pclower.py): a function an overlay file defines,
    # or references at an address of its image's code range, is <IMAGE>__<name>, so no other image's
    # file links to it directly (calls between images stay thunks into the interpreter, where the
    # trace command's image guard decides) and two images may define the same address
    img_of = {base: image_of_path(parsed[base][0]) for base in parsed}
    img_defs = {}
    for base in sorted(parsed):
        if img_of[base] != BASE_IMAGE:
            for name, fn in parsed[base][2].items():
                if fn["defined"] and not fn["static"]:
                    img_defs.setdefault(img_of[base], set()).add(name)
    img_renames = {}
    for base in sorted(parsed):
        img = img_of[base]
        if img == BASE_IMAGE:
            continue
        path, types, funcs, vars_ = parsed[base]
        ren = {}
        for name, fn in funcs.items():
            if fn["static"] or name.startswith("_builtin_") or name in NATIVE_HELPERS:
                continue
            if name in img_defs.get(img, ()) or in_image(addr_of(name, syms), img):
                ren[name] = "%s__%s" % (img, name)
        if ren:
            nf = {}
            for name, fn in funcs.items():
                if name in ren:
                    fn = dict(fn)
                    fn["name"] = ren[name]
                nf[ren.get(name, name)] = fn
            parsed[base] = (path, types, nf, vars_)
            img_renames[base] = ren
        manifest["files"][base]["image"] = img

    # game functions that share a name with the host C library (the game's own
    # rand/memset/strlen): renamed per file to a thunk that runs the original
    libc_renames = {}
    # resolve: iterate until no file is excluded
    while True:
        defined = {}
        dup = set()
        for base in sorted(parsed):
            path, types, funcs, vars_ = parsed[base]
            for name, fn in funcs.items():
                if fn["defined"] and not fn["static"]:
                    if name in defined and defined[name] != base:
                        dup.add(base)
                    else:
                        defined[name] = base
        bad = {}
        for base in dup:
            bad[base] = "defines a function also defined in another file"
        for base in sorted(parsed):
            if base in bad:
                continue
            path, types, funcs, vars_ = parsed[base]
            for name, fn in funcs.items():
                if fn["defined"] or not fn["used"] or name in defined or name.startswith("_builtin_"):
                    continue
                if name in NATIVE_HELPERS:
                    continue
                if name in LIBC_NAMES and addr_of(name, syms) is not None:
                    libc_renames.setdefault(base, {})[name] = name + "__game"
                elif name in LIBC_NAMES:
                    bad[base] = "references %s (clashes with the host C library)" % name
                elif addr_of(name, syms) is None:
                    bad[base] = "unresolved function %s" % name
            for name, v in vars_.items():
                if v["extern"] and not v["defined"] and v["used"] and addr_of(name, syms, True) is None \
                        and name not in NATIVE_DATA:
                    bad[base] = "unresolved data symbol %s" % name
        if not bad:
            break
        for base, why in bad.items():
            manifest["files"][base]["status"] = "excluded: " + why
            os.unlink(os.path.join(src_out, base))
            parsed.pop(base)

    thunks, natives, data = {}, {}, {}
    # per-file signatures of every referenced external function: files that
    # disagree get their own thunk (renamed with a #define in that file)
    ext_sigs = {}
    for base in sorted(parsed):
        path, types, funcs, vars_ = parsed[base]
        for name, fn in funcs.items():
            if fn["used"] and not fn["defined"] and not name.startswith("_builtin_"):
                ext_sigs.setdefault(name, {})[base] = (signature(types, fn), fn["type"])
    conflicts = {}
    renames = {}        # base -> {name: thunk_name}
    for name, per in ext_sigs.items():
        sigs = sorted({s["sig"] for s, q in per.values()})
        if len(sigs) > 1:
            conflicts[name] = {b: s["sig"] for b, (s, q) in per.items()}
            if name not in defined:
                for b, (s, q) in per.items():
                    renames.setdefault(b, {})[name] = "%s__s%d" % (name, sigs.index(s["sig"]))
    manifest["signature_conflicts"] = conflicts
    for base, ren in libc_renames.items():
        if base in parsed:
            renames.setdefault(base, {}).update(ren)
    for base, ren in img_renames.items():
        if base in parsed:
            renames.setdefault(base, {}).update(ren)
    for base, ren in renames.items():
        dst = os.path.join(src_out, base)
        text = open(dst, encoding="utf-8").read()
        hdr = "".join("#define %s %s /* conflicting extern prototypes across files */\n" % (k, v)
                      for k, v in sorted(ren.items()))
        # headers may redefine the name (e.g. "#define GetStageNumber GetStageNumber"
        # in include/sa1/engine.h), so the rename is applied again after the last
        # #include of the file
        lines = text.split("\n")
        last = max([i for i, l in enumerate(lines) if l.lstrip().startswith("#include")] or [-1])
        if last >= 0:
            again = "".join("#undef %s\n#define %s %s /* conflicting extern prototypes (after the includes) */\n"
                            % (k, k, v) for k, v in sorted(ren.items()))
            lines.insert(last + 1, again.rstrip("\n"))
            text = "\n".join(lines)
        with open(dst, "w", newline="\n", encoding="utf-8") as f:
            f.write(hdr + text)
    for base in sorted(parsed):
        path, types, funcs, vars_ = parsed[base]
        entry = manifest["files"][base]
        entry["status"] = "native"
        entry["defines"] = []
        entry["thunks"] = []
        for name, fn in sorted(funcs.items()):
            if fn["defined"] and not fn["static"]:
                s = signature(types, fn)
                natives[name] = (base, s, addr_of(name, syms))
                entry["defines"].append({"name": name, "sig": s["sig"], "guest": "%08X" % (addr_of(name, syms) or 0)})
            elif (fn["used"] and not fn["defined"] and not name.startswith("_builtin_") and name not in defined
                  and name not in NATIVE_HELPERS):
                s = signature(types, fn)
                tname = renames.get(base, {}).get(name, name)
                if tname not in thunks:
                    thunks[tname] = (s, addr_of(name, syms), fn["type"])
                entry["thunks"].append(tname)
        for name, v in sorted(vars_.items()):
            if v["extern"] and not v["defined"] and v["used"] and name not in NATIVE_DATA:
                data[name] = addr_of(name, syms, True)
            elif v["defined"] and not v["static"]:
                entry.setdefault("warnings", []).append("defines global data %s natively" % name)

    # ---- emit
    with open(os.path.join(out, "native_abs.c"), "w", newline="\n") as f:
        f.write(HEADER)
        f.write("/* absolute addresses of the game data referenced by native code */\n")
        names = sorted(data)
        for i in range(0, len(names), 64):
            f.write("__asm__(\n")
            for n in names[i:i + 64]:
                f.write('    ".globl _%s\\n.set _%s, 0x%08X\\n"\n' % (n, n, data[n]))
            f.write(");\n")
        if not names:
            f.write("typedef int native_abs_empty;\n")

    with open(os.path.join(out, "native_thunks.c"), "w", newline="\n") as f:
        f.write(HEADER)
        f.write("void sh4_unsupported_call(const char *name, uint32_t guest);\n\n")
        for name in sorted(thunks):
            s, guest, qt = thunks[name]
            f.write(emit_thunk(name, guest, s, qt) + "\n\n")

    with open(os.path.join(out, "native_registry.c"), "w", newline="\n") as f:
        f.write(HEADER)
        f.write("static inline __attribute__((unused)) float sh4_u2f(uint32_t u) { union { uint32_t u; float f; } x; x.u = u; return x.f; }\n\n")
        adapters = {}
        for name in sorted(natives):
            base, s, guest = natives[name]
            if name in NO_ROUTE:
                f.write("extern void %s(void); /* no adapter: %s */\n" % (name, NO_ROUTE[name]))
                manifest["files"][base].setdefault("no_route", []).append(name)
            elif s["ok"] and not s["variadic"] and not s["noproto"]:
                f.write(emit_adapter(name, s) + "\n\n")
                adapters[name] = True
            else:
                f.write("extern void %s(void);\n" % name)
        for name in sorted(thunks):
            f.write("extern void %s(void);\n" % name)
        f.write("\nconst sh4_native_fn sh4_native_fns[] = {\n")
        for name in sorted(natives):
            base, s, guest = natives[name]
            f.write('    { 0x%08Xu, (void *)%s, %s, "%s", "%s", "%s" },\n' % (
                guest or 0, name, ("adapt_" + name) if name in adapters else "0", name, s["sig"], base))
        if not natives:
            f.write('    { 0, 0, 0, "", "", "" },\n')
        f.write("};\nconst int sh4_native_fn_count = %d;\n\n" % len(natives))
        f.write("const sh4_thunk_info sh4_thunks[] = {\n")
        for name in sorted(thunks):
            s, guest, qt = thunks[name]
            f.write('    { 0x%08Xu, (void *)%s, "%s", "%s" },\n' % (guest, name, name, s["sig"]))
        if not thunks:
            f.write('    { 0, 0, "", "" },\n')
        f.write("};\nconst int sh4_thunk_count = %d;\n\n" % len(thunks))
        # referenced data symbols per file, with the distance to the next symbol
        addrs = sorted(set(syms.values()))
        import bisect
        f.write("const sh4_native_data sh4_native_datas[] = {\n")
        nd = 0
        for base in sorted(parsed):
            path, types, funcs, vars_ = parsed[base]
            for name, v in sorted(vars_.items()):
                if v["extern"] and not v["defined"] and v["used"] and name not in NATIVE_DATA:
                    ad = addr_of(name, syms, True)
                    k = bisect.bisect_right(addrs, ad)
                    gap = (addrs[k] - ad) if k < len(addrs) else 256
                    f.write('    { "%s", 0x%08Xu, %du, "%s" },\n' % (name, ad, min(gap, 1024), base))
                    nd += 1
        if not nd:
            f.write('    { "", 0, 0, "" },\n')
        f.write("};\nconst int sh4_native_data_count = %d;\n" % nd)
        emit_image_guard(f, [image_of_path(parsed[natives[name][0]][0]) for name in sorted(natives)])

    manifest["images"] = {img: {k: v for k, v in image_guard_info(img).items() if k != "ranges"}
                          for img in sorted({image_of_path(p[0]) for p in parsed.values()} - {BASE_IMAGE})}
    manifest["native_functions"] = len(natives)
    manifest["thunks"] = len(thunks)
    manifest["data_symbols"] = len(data)
    manifest["unsupported_thunks"] = sorted(n for n, (s, g, q) in thunks.items() if not s["ok"])
    manifest["noproto_thunks"] = sorted(n for n, (s, g, q) in thunks.items() if s["noproto"])
    if conflicts:
        print("gen_native: %d functions have conflicting extern prototypes across files (see native_manifest.json)" % len(conflicts))
    with open(os.path.join(out, "native_manifest.json"), "w", newline="\n") as f:
        json.dump(manifest, f, indent=1)
    nfiles = sum(1 for e in manifest["files"].values() if e.get("status") == "native")
    print("gen_native: %d/%d files native, %d functions, %d thunks, %d data symbols" % (
        nfiles, len(manifest["files"]), len(natives), len(thunks), len(data)))
    for base, e in sorted(manifest["files"].items()):
        if e.get("status", "").startswith("excluded"):
            print("  %s: %s" % (base, e["status"]))


if __name__ == "__main__":
    main()