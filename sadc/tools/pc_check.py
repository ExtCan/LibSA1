#!/usr/bin/env python3
"""PC portability check for the decompiled C (docs/portability.md).

    py -3.12 tools/pc_check.py [files...] [-j N] [--targets t1,t2] [--no-ast]
                               [--json OUT] [--strict] [-v]
    py -3.12 tools/pc_check.py --layout      # verify header layouts with SHC + x86

Default mode compiles every src/**/*.c (or the files given) with zig cc
(clang) under -DTARGET_PC -DNON_MATCHING for each target (default:
x86_64-windows-gnu, x86-windows-gnu, x86_64-linux-gnu) and reports, per file:

  * compiler diagnostics (errors; warnings such as -Wpointer-to-int-cast,
    -Wint-to-pointer-cast, -Wshorten-64-to-32, -Wdouble-promotion,
    -Wimplicit-float-conversion, -Wstrict-prototypes, uninitialised use);
    diagnostics located in include/ are reported once, under the header;
  * AST checks (x86_64 only): integer '/' or '%' by a non-constant divisor
    (needs SA1_DIV/SA1_MOD: SH-4 returns 0, x86 traps), float->integer
    conversions (FTRC saturation: SA1_FTOI), arithmetic done in double
    (SHC -fpu=single does it in single precision);
  * source checks: float literals whose value differs between SHC
    (-round=zero truncation) and PC (round to nearest), with a replacement
    literal that gives the same bits on both; unsuffixed (double) floating
    literals; 'long'; file-scope initialised data tables; raw offset
    arithmetic on byte pointers; absolute DC addresses.

--layout extracts every SA1_ASSERT_SIZE/SA1_ASSERT_OFFSET from include/**/*.h,
compiles a table of sizeof/offsetof values with SHC 5.1r03 (game flags) and
compares it with the asserted values, then compiles the same table for
x86-windows-gnu where the _Static_asserts are live.

zig 0.16's "cc -fsyntax-only" fails on Windows (error: FileNotFound), so
files are compiled to objects under build/pc_check/.
"""
import argparse
import concurrent.futures
import json
import os
import re
import subprocess
import sys
from collections import Counter, defaultdict
from decimal import Decimal, getcontext
from fractions import Fraction

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
OUT = os.path.join(ROOT, "build", "pc_check")
TARGETS = ["x86_64-windows-gnu", "x86-windows-gnu", "x86_64-linux-gnu"]
ZIG = [sys.executable, "-m", "ziglang", "cc"]

BASE_FLAGS = [
    "-std=gnu11", "-DTARGET_PC", "-DNON_MATCHING",
    "-fwrapv", "-fno-strict-aliasing", "-ffp-contract=off", "-fsigned-char",
    "-I" + os.path.join(ROOT, "include"), "-I" + os.path.join(ROOT, "src"),
]
WARN_FLAGS = [
    "-Wall", "-Wextra",
    "-Wno-unused-parameter", "-Wno-unused-variable", "-Wno-unused-function",
    "-Wno-unused-but-set-variable", "-Wno-missing-field-initializers", "-Wno-sign-compare",
    "-Wno-unused-value", "-Wno-unused-label",
    "-Wpointer-to-int-cast", "-Wint-to-pointer-cast", "-Wshorten-64-to-32",
    "-Wdouble-promotion", "-Wimplicit-float-conversion",
    "-Wstrict-prototypes", "-Wuninitialized", "-Wsometimes-uninitialized",
    "-Wconditional-uninitialized", "-Wcast-function-type",
    "-ferror-limit=0", "-fno-caret-diagnostics", "-fno-color-diagnostics",
]
X86_FLAGS = ["-msse2", "-mfpmath=sse"]
RUN_ID = int.from_bytes(os.urandom(4), "little")

DIAG = re.compile(r"^(?P<file>.+?):(?P<line>\d+):(?P<col>\d+): (?P<sev>warning|error|fatal error): "
                  r"(?P<msg>.*?)(?: \[(?P<flag>-W[^\],]+)[^\]]*\])?$")


def rel(p):
    try:
        r = os.path.relpath(os.path.abspath(p), ROOT)
    except ValueError:
        return p
    return r.replace("\\", "/")


def run(cmd, timeout=600):
    r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, errors="replace", timeout=timeout)
    return r.returncode, r.stdout, r.stderr


# ---------------------------------------------------------------------------
# exact float32 conversion (SHC -round=zero vs. round-to-nearest-even)

def f32_bits(q, mode):
    """Fraction -> IEEE single bits, mode 'nearest' or 'zero' (no NaN)."""
    sign = 0
    if q < 0:
        sign, q = 1, -q
    if q == 0:
        return sign << 31
    e = q.numerator.bit_length() - q.denominator.bit_length()
    p2 = (lambda k: Fraction(2) ** k)
    if p2(e) > q:
        e -= 1
    if e < -126:
        e = -127
        m = q / p2(-149)
    else:
        m = q / p2(e - 23)
    mi = m.numerator // m.denominator
    rem = m - mi
    if mode == "nearest":
        if rem > Fraction(1, 2) or (rem == Fraction(1, 2) and (mi & 1)):
            mi += 1
    if e == -127:
        if mi >= 1 << 23:
            return (sign << 31) | (1 << 23) | (mi - (1 << 23))
        return (sign << 31) | mi
    if mi >= 1 << 24:
        mi >>= 1
        e += 1
    if e > 127:
        return (sign << 31) | (0x7F800000 if mode == "nearest" else 0x7F7FFFFF)
    return (sign << 31) | ((e + 127) << 23) | (mi - (1 << 23))


def f32_value(bits):
    sign = -1 if bits >> 31 else 1
    ex = (bits >> 23) & 0xFF
    mant = bits & 0x7FFFFF
    if ex == 0:
        return sign * Fraction(mant) * Fraction(2) ** -149
    return sign * (Fraction((1 << 23) | mant)) * Fraction(2) ** (ex - 150)


def window_literal(bits):
    """Shortest decimal giving 'bits' both truncated (SHC) and rounded (PC)."""
    lo = abs(f32_value(bits))
    hi = abs(f32_value((bits & 0x7FFFFFFF) + 1))
    ulp = hi - lo
    a, b = lo + ulp / 16, lo + ulp * 7 / 16
    getcontext().prec = 40
    mid = (a + b) / 2
    dmid = Decimal(mid.numerator) / Decimal(mid.denominator)
    for nd in range(1, 18):
        d = Decimal(format(dmid, ".%de" % (nd - 1)))
        q = Fraction(d)
        if a <= q <= b:
            s = format(d.normalize(), "f") if abs(d) >= Decimal("1e-5") else format(d, "e")
            if "." not in s and "e" not in s:
                s += ".0"
            return ("-" if bits >> 31 else "") + s + "f"
    return None


FLOAT_LIT = re.compile(
    r"(?<![\w.])((?:\d+\.\d*|\.\d+|\d+)(?:[eE][+-]?\d+)?|(?:\d+\.\d*|\.\d+)(?:[eE][+-]?\d+)?)([fFlL]?)(?![\w.])")


def strip_comments_strings(text):
    """Blank out comments and string/char literals, keep offsets/lines."""
    out = list(text)
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            for k in range(i, j):
                if out[k] != "\n":
                    out[k] = " "
            i = j
        elif text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            for k in range(i, j):
                out[k] = " "
            i = j
        elif c in "\"'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            for k in range(i + 1, min(j, n)):
                if out[k] != "\n":
                    out[k] = " "
            i = j + 1
        else:
            i += 1
    return "".join(out)


def line_of(text, pos):
    return text.count("\n", 0, pos) + 1


def source_checks(path):
    issues = []
    raw = open(path, encoding="utf-8", errors="replace").read()
    code = strip_comments_strings(raw)
    # skip preprocessor lines for literal checks
    for m in FLOAT_LIT.finditer(code):
        txt, suf = m.group(1), m.group(2)
        if not ("." in txt or "e" in txt.lower()):
            continue
        ln = line_of(code, m.start())
        line_text = code.splitlines()[ln - 1] if ln - 1 < len(code.splitlines()) else ""
        if line_text.lstrip().startswith("#"):
            continue
        q = Fraction(Decimal(txt))
        t = f32_bits(q, "zero")
        nr = f32_bits(q, "nearest")
        if suf.lower() != "f":
            issues.append((ln, "double-literal", "unsuffixed floating literal %s (double on PC; "
                           "single, truncated, on SHC -fpu=single)" % txt))
        if t != nr:
            sug = window_literal(t)
            issues.append((ln, "float-literal-rounding",
                           "%s%s: SHC 0x%08X, PC 0x%08X; use %s" % (txt, suf, t, nr, sug)))
    for m in re.finditer(r"\blong\b", code):
        issues.append((line_of(code, m.start()), "long", "'long' (64-bit on LP64 hosts): use Sint32/Uint32"))
    for m in re.finditer(r"\(\s*(?:char|Sint8|Uint8|signed char|unsigned char)\s*\*\s*\)\s*\(?\s*[\w\->\.\[\]]+\s*\)?\s*[+-]\s*(?:0x[0-9A-Fa-f]+|\d+)", code):
        issues.append((line_of(code, m.start()), "raw-offset", "byte-pointer offset arithmetic: "
                       + " ".join(m.group(0).split())))
    for m in re.finditer(r"\(\s*[\w\s]+\*\s*\)\s*0x[08]C[0-9A-Fa-f]{6}\b", code):
        issues.append((line_of(code, m.start()), "abs-address", "absolute DC address: " + " ".join(m.group(0).split())))
    # file-scope initialised aggregates (brace depth 0)
    decl = re.compile(r"(?m)^(?!\s*#)(?!\s*typedef)\s*((?:static\s+|const\s+|extern\s+)*)[A-Za-z_][\w\s\*]*?\b(\w+)\s*(\[[^\]]*\])+\s*=\s*\{")
    depth_at = []
    d = 0
    for idx, ch in enumerate(code):
        if ch == "{":
            d += 1
        elif ch == "}":
            d -= 1
        depth_at.append(d)
    for m in decl.finditer(code):
        if m.start() < len(depth_at) and (depth_at[m.start()] if m.start() else 0) == 0:
            issues.append((line_of(code, m.start()), "data-table", "initialised array '%s' defined in C "
                           "(game data must be extern; PC gets it from the ripper)" % m.group(2)))
    sdecl = re.compile(r'(?m)^(?!\s*#)\s*(?:static\s+|const\s+)*(?:char|Sint8|Uint8)\s+(\w+)\s*\[[^\]]*\]\s*=\s*"')
    for m in sdecl.finditer(code):
        if (depth_at[m.start()] if m.start() else 0) == 0:
            issues.append((line_of(code, m.start()), "data-string", "string data '%s' defined in C "
                           "(allowed for short strings; byte-compared by the matching build)" % m.group(1)))
    return issues


# ---------------------------------------------------------------------------
# AST checks (clang JSON)

def upd_loc(state, loc):
    """Follow clang's JSON location compression (file/line written only when
    they change, spelling before expansion); return the expansion position."""
    if not loc:
        return None
    if "spellingLoc" in loc or "expansionLoc" in loc:
        upd_loc(state, loc.get("spellingLoc"))
        return upd_loc(state, loc.get("expansionLoc"))
    if "file" in loc:
        state["file"] = loc["file"]
    if "line" in loc:
        state["line"] = loc["line"]
    return (state.get("file", ""), state.get("line", 0))


def ty(node):
    t = node.get("type") or {}
    return t.get("desugaredQualType") or t.get("qualType", "")


def walk_ast(node, main_file, state, out):
    upd_loc(state, node.get("loc"))
    rng = node.get("range") or {}
    pos = upd_loc(state, rng.get("begin"))
    upd_loc(state, rng.get("end"))
    if pos is None:
        pos = (state.get("file", ""), state.get("line", 0))
    in_main = os.path.normcase(os.path.abspath(pos[0] or "")) == main_file
    line = pos[1]
    kind = node.get("kind")
    if in_main and kind in ("BinaryOperator", "CompoundAssignOperator"):
        op = node.get("opcode")
        qt = ty(node)
        inner = node.get("inner") or []
        if op in ("/", "%", "/=", "%=") and len(inner) == 2:
            lt = ty(inner[0])
            if not is_float_type(qt) and not is_float_type(lt) and not is_constant(inner[1]):
                out.append((line, "int-div", "integer '%s' by a runtime divisor "
                            "(SA1_DIV/SA1_MOD/SA1_UDIV/SA1_UMOD)" % op))
        if op in ("+", "-", "*", "/", "+=", "-=", "*=", "/=") and qt == "double":
            out.append((line, "double-arith", "arithmetic in double "
                        "(SHC -fpu=single computes in single precision)"))
    if in_main and kind in ("ImplicitCastExpr", "CStyleCastExpr") and node.get("castKind") == "FloatingToIntegral":
        out.append((line, "float-to-int", "float -> integer conversion "
                    "(FTRC saturates; out-of-range is UB in C: SA1_FTOI)"))
    for ch in node.get("inner") or []:
        walk_ast(ch, main_file, state, out)


def is_float_type(qt):
    return qt.startswith("float") or qt.startswith("double") or qt.startswith("long double")


def is_constant(node):
    k = node.get("kind")
    if k in ("IntegerLiteral", "CharacterLiteral", "UnaryExprOrTypeTraitExpr"):
        return True
    if k in ("ImplicitCastExpr", "ParenExpr", "CStyleCastExpr", "ConstantExpr"):
        inn = node.get("inner") or []
        return len(inn) == 1 and is_constant(inn[0])
    if k == "UnaryOperator" and node.get("opcode") in ("-", "+", "~"):
        inn = node.get("inner") or []
        return len(inn) == 1 and is_constant(inn[0])
    if k == "BinaryOperator":
        inn = node.get("inner") or []
        return len(inn) == 2 and all(is_constant(x) for x in inn)
    if k == "DeclRefExpr" and (node.get("referencedDecl") or {}).get("kind") == "EnumConstantDecl":
        return True
    return False


def ast_checks(path):
    cmd = ZIG + ["-target", "x86_64-windows-gnu"] + BASE_FLAGS + ["-w", "-Xclang", "-ast-dump=json",
                                                                  "-fsyntax-only", path]
    rc, out, err = run(cmd)
    try:
        tree = json.loads(out)
    except ValueError:
        return [(0, "ast-failed", "could not read the AST dump")]
    res = []
    state = {}
    sys.setrecursionlimit(100000)
    walk_ast(tree, os.path.normcase(os.path.abspath(path)), state, res)
    return res


# ---------------------------------------------------------------------------
# DC-layout structs: local struct definitions whose "/* 0xNN */" offset
# comments hold on 32-bit targets but not on x86_64 (pointer-sized members
# before them). They must not be used to reinterpret shared engine structs or
# raw game data on the PC target (use include/sa1 types / ripper loaders).

REC_HDR = re.compile(r"^\s*0 \| (?:struct |union )?(.+?)\s*$")
REC_FLD = re.compile(r"^\s*(\d+)(?::[\d-]+)? \|( +)(.*?)\s*$")


def record_layouts(path, target):
    cmd = ZIG + ["-target", target] + BASE_FLAGS + ["-w", "-Xclang", "-fdump-record-layouts",
                                                    "-fsyntax-only", path]
    rc, out, err = run(cmd)
    recs = {}
    cur = None
    lines = out.splitlines()
    for i, line in enumerate(lines):
        if line.startswith("*** Dumping AST Record Layout"):
            cur = None
            continue
        if cur is None:
            m = REC_HDR.match(line)
            if m:
                cur = recs.setdefault(m.group(1), {})
            continue
        m = REC_FLD.match(line)
        if not m:
            if not line.strip():
                cur = None
            continue
        depth = (len(m.group(2)) - 1) // 2
        if depth != 1:
            continue
        decl = m.group(3)
        name = decl.split()[-1] if decl.split() else ""
        cur.setdefault(name, int(m.group(1)))
    return recs


FIELD_OFF = re.compile(r"/\*\s*(0x[0-9A-Fa-f]+)\b")


def commented_structs(raw):
    """{record name: {field: commented offset}} for structs in this file."""
    res = {}
    for m in re.finditer(r"\b(typedef\s+)?(struct|union)\s*(\w+)?\s*\{", raw):
        start = m.end()
        depth, i = 1, start
        while i < len(raw) and depth:
            if raw[i] == "{":
                depth += 1
            elif raw[i] == "}":
                depth -= 1
            i += 1
        body = raw[start:i - 1]
        tail = re.match(r"\s*(\w+)\s*[;,\[]", raw[i:])
        name = tail.group(1) if (m.group(1) and tail) else m.group(3)
        if not name:
            continue
        fields = {}
        d = 0
        for line in body.splitlines():
            if d == 0 and ";" in line:
                om = FIELD_OFF.search(line)
                code = line.split("/*")[0]
                fm = re.search(r"\(\s*\*\s*(\w+)\s*\)\s*\(|(\w+)\s*(?:\[[^\]]*\]\s*)*(?:,|;)", code)
                if om and fm:
                    fields[fm.group(1) or fm.group(2)] = int(om.group(1), 16)
            d += line.count("{") - line.count("}")
        if fields:
            res[name] = fields
    return res


def layout64_checks(path):
    raw = open(path, encoding="utf-8", errors="replace").read()
    structs = commented_structs(raw)
    if not structs:
        return []
    l32 = record_layouts(path, "x86-windows-gnu")
    l64 = record_layouts(path, "x86_64-windows-gnu")
    out = []
    for name, fields in structs.items():
        r32, r64 = l32.get(name), l64.get(name)
        if r32 is None or r64 is None:
            continue
        bad32 = [f for f, off in fields.items() if f in r32 and r32[f] != off]
        moved = [(f, off, r64[f]) for f, off in sorted(fields.items(), key=lambda x: x[1])
                 if f in r64 and f in r32 and r32[f] == off and r64[f] != off]
        line = raw.count("\n", 0, raw.find(name)) + 1 if name in raw else 0
        if bad32:
            out.append((line, "offset-comment", "%s: comment differs from the 32-bit layout for %s"
                        % (name, ", ".join(bad32[:6]))))
        if moved:
            f, off, o64 = moved[0]
            out.append((line, "dc-layout-struct", "%s: %d field(s) move on x86_64, first %s 0x%X -> 0x%X "
                        "(do not overlay shared structs/raw data with it)" % (name, len(moved), f, off, o64)))
    # partial "overlay" structs that reach a DC offset through padding
    for m in re.finditer(r"\b(typedef\s+)?(struct|union)\s*(\w+)?\s*\{([^{}]*)\}\s*(\w+)?", raw):
        body = m.group(4)
        if re.search(r"\b(?:pad|unk)\w*\s*\[\s*0x[0-9A-Fa-f]+(?:\s*-\s*0x[0-9A-Fa-f]+)?\s*\]", body):
            name = m.group(5) or m.group(3) or "?"
            out.append((raw.count("\n", 0, m.start()) + 1, "padded-overlay",
                        "%s reaches DC offsets through padding: use the shared include/sa1 type "
                        "on the PC target" % name))
    return out


# ---------------------------------------------------------------------------
# compile with zig for one target

def compile_one(path, target):
    obj = os.path.join(OUT, "obj", target, os.path.splitext(rel(path).replace("/", "_"))[0] + ".o")
    os.makedirs(os.path.dirname(obj), exist_ok=True)
    flags = BASE_FLAGS + WARN_FLAGS + (X86_FLAGS if target.startswith("x86-") else [])
    # zig caches objects and prints no diagnostics on a cache hit: make every
    # run's command line unique
    cmd = ZIG + ["-target", target] + flags + ["-DSA1_PC_CHECK_RUN=%d" % RUN_ID, "-c", "-o", obj, path]
    rc, out, err = run(cmd)
    diags = []
    for line in (out + err).splitlines():
        m = DIAG.match(line.strip())
        if not m:
            continue
        if "argument unused during compilation" in line:
            continue
        diags.append({"file": rel(m.group("file")), "line": int(m.group("line")), "sev": m.group("sev"),
                      "msg": m.group("msg"), "flag": m.group("flag") or ("error" if "error" in m.group("sev") else "")})
    return target, rc, diags


def check_file(path, targets, do_ast):
    res = {"file": rel(path), "targets": {}, "source": source_checks(path), "ast": []}
    for t in targets:
        _, rc, diags = compile_one(path, t)
        res["targets"][t] = {"rc": rc, "diags": diags}
    if do_ast:
        res["ast"] = ast_checks(path) + layout64_checks(path)
    return res


def main_check(args):
    files = args.files or sorted(
        os.path.join(dp, f) for dp, dn, fn in os.walk(os.path.join(ROOT, "src")) for f in fn if f.endswith(".c"))
    files = [os.path.abspath(f) for f in files]
    targets = args.targets.split(",") if args.targets else TARGETS
    os.makedirs(OUT, exist_ok=True)
    results = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as ex:
        futs = {ex.submit(check_file, f, targets, not args.no_ast): f for f in files}
        for fu in concurrent.futures.as_completed(futs):
            results.append(fu.result())
    results.sort(key=lambda r: r["file"])

    kinds = Counter()            # kind -> occurrences
    kind_files = defaultdict(set)
    header = defaultdict(dict)   # (file, line, msg) -> diag, for include/ diagnostics
    lines = []
    failed = []
    for r in results:
        per = []
        for t, info in r["targets"].items():
            if info["rc"] != 0:
                failed.append((r["file"], t))
            for d in info["diags"]:
                key = "%s %s" % ("error" if "error" in d["sev"] else "warning", d["flag"] or d["msg"][:40])
                tag = "[%s]" % t
                if d["file"].startswith("include/"):
                    header[(d["file"], d["line"], d["msg"])].setdefault(t, d)
                    continue
                if d["file"] != r["file"]:
                    continue
                per.append((d["line"], key, d["msg"] + " " + tag))
        for ln, kind, msg in r["source"]:
            per.append((ln, "src " + kind, msg))
        for ln, kind, msg in r["ast"]:
            per.append((ln, "ast " + kind, msg))
        # merge identical messages from several targets
        merged = {}
        for ln, kind, msg in per:
            base = re.sub(r" \[[\w-]+\]$", "", msg)
            k = (ln, kind, base)
            tg = re.findall(r"\[([\w-]+)\]$", msg)
            merged.setdefault(k, set()).update(tg)
        r["problems"] = []
        for (ln, kind, base), tg in sorted(merged.items()):
            kinds[kind] += 1
            kind_files[kind].add(r["file"])
            r["problems"].append({"line": ln, "kind": kind, "msg": base, "targets": sorted(tg)})
        if r["problems"] or args.verbose:
            lines.append("== %s (%d)" % (r["file"], len(r["problems"])))
            for p in r["problems"]:
                tg = (" [%s]" % ",".join(p["targets"])) if p["targets"] and len(p["targets"]) < len(targets) else ""
                lines.append("  %5d  %-34s %s%s" % (p["line"], p["kind"], p["msg"], tg))
    for (f, ln, msg), tg in sorted(header.items()):
        kinds["header " + (list(tg.values())[0]["flag"] or "diag")] += 1
        lines.append("== %s:%d [%s] %s" % (f, ln, ",".join(sorted(tg)), msg))

    print("\n".join(lines))
    print()
    print("pc_check: %d files, targets %s" % (len(results), ", ".join(targets)))
    if failed:
        print("compile failures (errors): %d file/target pairs, %d files" % (len(failed), len(set(f for f, _ in failed))))
    print("%-40s %8s %6s" % ("kind", "count", "files"))
    for k, n in kinds.most_common():
        print("%-40s %8d %6d" % (k, n, len(kind_files[k]) if k in kind_files else 0))
    if args.json:
        with open(args.json, "w") as f:
            json.dump({"targets": targets, "files": results, "summary": dict(kinds)}, f, indent=1)
    return 1 if (args.strict and failed) else 0


# ---------------------------------------------------------------------------
# --layout

ASSERT = re.compile(r"SA1_ASSERT_(SIZE|OFFSET)\(\s*(\w+)\s*,\s*([^,()]+?)\s*(?:,\s*([^()]+?)\s*)?\)\s*;")


def collect_asserts():
    items = []
    inc = os.path.join(ROOT, "include")
    for dp, dn, fn in os.walk(inc):
        for f in sorted(fn):
            if not f.endswith(".h"):
                continue
            p = os.path.join(dp, f)
            text = open(p, encoding="utf-8").read()
            for m in ASSERT.finditer(strip_comments_strings(text)):
                if m.group(1) == "SIZE":
                    items.append((rel(p), "size", m.group(2), None, int(m.group(3), 0)))
                else:
                    items.append((rel(p), "offset", m.group(2), m.group(3), int(m.group(4), 0)))
    return items


def main_layout(args):
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    from shcdiff import compile_c, compiler_cfg  # noqa: E402
    items = collect_asserts()
    os.makedirs(OUT, exist_ok=True)
    c_path = os.path.join(OUT, "layout.c")
    exprs = []
    for f, kind, typ, mem, val in items:
        exprs.append("sizeof(%s)" % typ if kind == "size" else "(unsigned long)&((%s *)0)->%s" % (typ, mem))
    with open(c_path, "w", newline="\n") as fh:
        fh.write("/* generated by tools/pc_check.py --layout */\n#include \"sa1/sa1.h\"\n\n")
        fh.write("const unsigned long sa1_layout_table[] = {\n")
        for e in exprs:
            fh.write("    %s,\n" % e)
        fh.write("    0\n};\n")
    out_src = os.path.join(OUT, "layout.src")
    if os.path.exists(out_src):
        os.remove(out_src)
    compile_c(c_path, out_src, compiler_cfg())
    vals = []
    in_data = False
    for line in open(out_src, encoding="cp932", errors="replace"):
        m = re.match(r"\s*\.DATA\.L\s+(.*)$", line)
        if m:
            in_data = True
            body = m.group(1)
        elif in_data and line.startswith("+"):
            body = line[1:]
        else:
            in_data = False
            continue
        vals += [int(v.strip()[2:], 16) for v in body.split(",") if v.strip().startswith("H'")]
    bad = 0
    for (f, kind, typ, mem, val), got in zip(items, vals):
        ok = got == val
        bad += not ok
        if args.verbose or not ok:
            what = "sizeof(%s)" % typ if kind == "size" else "offsetof(%s, %s)" % (typ, mem)
            print("%-4s %-28s %-48s expected 0x%X  SHC 0x%X" % ("ok" if ok else "BAD", f, what, val, got))
    if len(vals) < len(items):
        print("SHC table too short: %d values for %d checks" % (len(vals), len(items)))
        bad += 1
    print("SHC 5.1r03: %d layout checks, %d mismatches" % (len(items), bad))
    # the same file on x86 (32-bit pointers): _Static_asserts are live there
    obj = os.path.join(OUT, "layout.x86.o")
    rc, out, err = run(ZIG + ["-target", "x86-windows-gnu"] + BASE_FLAGS + ["-DSA1_PC_CHECK_RUN=%d" % RUN_ID, "-c", "-o", obj, c_path])
    msgs = [l for l in (out + err).splitlines() if "error" in l]
    print("x86-windows-gnu (_Static_assert): %s" % ("ok" if rc == 0 and not msgs else "FAILED"))
    for l in msgs[:40]:
        print("  " + l)
    h_path = os.path.join(OUT, "headers.c")
    with open(h_path, "w", newline="\n") as fh:
        fh.write("/* generated by tools/pc_check.py --layout */\n#include \"sa1/sa1.h\"\n")
    rc64, out64, err64 = run(ZIG + ["-target", "x86_64-windows-gnu"] + BASE_FLAGS + WARN_FLAGS + ["-DSA1_PC_CHECK_RUN=%d" % RUN_ID] +
                            ["-c", "-o", os.path.join(OUT, "headers.x64.o"), h_path])
    w64 = [l for l in (out64 + err64).splitlines() if ": warning:" in l or ": error:" in l]
    print("x86_64-windows-gnu (headers parse, native layout): %s%s" % (
        "ok" if rc64 == 0 else "FAILED", (", %d warnings" % len(w64)) if w64 else ""))
    for l in w64[:40]:
        print("  " + l)
    return 1 if (bad or rc or rc64) else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("files", nargs="*")
    ap.add_argument("-j", "--jobs", type=int, default=os.cpu_count() or 4)
    ap.add_argument("--targets", help="comma-separated zig targets (default: %s)" % ",".join(TARGETS))
    ap.add_argument("--no-ast", action="store_true", help="skip the clang AST checks")
    ap.add_argument("--json", help="write the full report as JSON")
    ap.add_argument("--strict", action="store_true", help="exit 1 if any file fails to compile")
    ap.add_argument("--layout", action="store_true", help="verify SA1_ASSERT_* layouts with SHC and x86")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()
    if args.layout:
        sys.exit(main_layout(args))
    sys.exit(main_check(args))


if __name__ == "__main__":
    main()