#!/usr/bin/env python3
"""gen_stubs.py - the boundary stub policy (gen/policy/stubs.tsv) as C (sadc docs/libsa1_integration.md 10.5).

    py -3.12 gen/gen_stubs.py --report build/pclower/report.json --out build/gen/sa1g_stubs_policy.c
                              [--tsv gen/policy/stubs.tsv] [--summary FILE.json] [--strict] [--quiet]

pclower (--stub-unresolved) lists in report.json "stubs" every function the lowered code calls or takes the
address of that no lowered translation unit defines ("name ADDR sig"). This script writes one C definition per
stub, with the policy of its line in the TSV (format: the header of gen/policy/stubs.tsv):

    fatal          sa1g_unimpl(addr, name): the world records the DC address and longjmps to the tick
                   (SA1_ERR_NOT_IMPLEMENTED, SA1_EVENT_UNIMPLEMENTED). Default for a stub without a line.
    void           no-op                         ret V       constant result
    null           NULL result                   address-only  no-op returning 0 (only the address is used)
    freetask       FreeTask(tp) (task exec)      event K ... sa1g_event_push(SA1_EVENT_K, id, bank, pos, arg)

Overlay slots (sadc docs/libsa1_integration.md 2.5): a stub of the base image at a DC address in the overlay area
(0x0C900000-0x0CFFFFFF: 1ST_READ code calls 0C907998 of STG03 directly) first asks the running world's overlay images
(pclower's sa1g_fn_image_lookup): when the image the world holds defines that address natively (with the same ABI
signature; another one is a code-map fault), the call goes there; otherwise the policy applies. Stubs of an overlay
image (report.json "stub_images": STG05__func_0C9131AA ...) are reached through that image's code map only.

The output replaces pclower's all-fatal sa1g_stubs.c; it is compiled like the lowered code (game flags, game-text
section). Checks (exit status 1): TSV syntax, a policy that does not fit the stub's signature, duplicate lines;
with --strict also stubs without a line. Stale lines (functions that are no longer stubs: now decompiled and
lowered) are reported and ignored: a defined function is never stubbed.
"""
import argparse
import json
import os
import re
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
LIB = os.path.dirname(HERE)
EVENTS = {"SOUND": 1, "SOUND_STOP": 2, "VOICE": 3, "JINGLE": 4, "RUMBLE": 5, "DEATH": 6, "RESTART": 7, "RESPAWN": 8,
          "MISSING_ACTION": 9, "UNIMPLEMENTED": 10, "CAPACITY": 11, "TIME_UP": 12, "FAULT": 13}


def load_events(header=os.path.join(LIB, "include", "libsa1.h")):
    """SA1_EVENT_* numbers of the public header (the generated C must agree with it)"""
    ev = dict(EVENTS)
    if os.path.exists(header):
        for m in re.finditer(r"^#define\s+SA1_EVENT_([A-Z_]+)\s+(\d+)", open(header, encoding="utf-8").read(), re.M):
            ev[m.group(1)] = int(m.group(2))
    return ev


# runtime functions without a DC address that the world core provides (src/world/sa1_world_intrin.c); the
# intrinsics among them are also inlined into lowered code by src/rt/rt_intrin.h
WORLD_RUNTIME = {"sa1g_fabsf", "sa1g_sqrtf", "sa1g_fsrra", "sa1g_fipr", "sa1g_ftrv", "sa1g_fsca", "sa1g_set_cr",
                 "sa1g_get_cr", "sa1g_set_imask", "sa1g_get_imask", "sa1g_set_gbr", "sa1g_get_gbr",
                 "sa1g_fatal"}     # sa1g_fatal: src/world/sa1_world.c (PC_ONLY in CCL_Init, sadc code_8C02F27C.c)
CTYPE = {"v": "void", "i8": "int8_t", "u8": "uint8_t", "i16": "int16_t", "u16": "uint16_t", "i32": "int32_t",
         "u32": "uint32_t", "p": "void *", "f32": "float", "f64": "double", "gfn": "sa1_gfn"}
INTS = ("i8", "u8", "i16", "u16", "i32", "u32")
POLICIES = ("fatal", "void", "ret", "null", "address-only", "freetask", "event")


class Sig:
    def __init__(self, text):
        self.text = text
        self.ok = False
        self.ret, self.args = "?", []
        m = re.match(r"^([a-z0-9?]+):(.*)$", text or "")
        if m:
            self.ret = m.group(1)
            self.args = [a for a in m.group(2).split(",") if a] if m.group(2) else []
            self.ok = self.ret in CTYPE and all(a in CTYPE and a != "v" for a in self.args)

    def proto(self, name):
        if not self.ok:
            return "void %s(void)" % name
        params = ", ".join("%s%sa%d" % (CTYPE[a], "" if CTYPE[a].endswith("*") else " ", i)
                           for i, a in enumerate(self.args)) or "void"
        rt = CTYPE[self.ret]
        return "%s%s%s(%s)" % (rt, "" if rt.endswith("*") else " ", name, params)


def parse_value(tok, sig, what, allow_float=False):
    """aN (an integer / pointer argument) or a C integer literal -> C expression (uint32_t)"""
    m = re.match(r"^a(\d+)$", tok)
    if m:
        i = int(m.group(1))
        if i >= len(sig.args):
            raise ValueError("%s: argument a%d does not exist (sig %s)" % (what, i, sig.text))
        t = sig.args[i]
        if t in INTS:
            return "(uint32_t)a%d" % i
        if t == "gfn":
            return "a%d.v" % i
        raise ValueError("%s: argument a%d is %s" % (what, i, t))
    if re.match(r"^-?(0x[0-9A-Fa-f]+|\d+)$", tok):
        return "(uint32_t)(%s)" % tok
    raise ValueError("%s: bad value %r" % (what, tok))


def parse_pos(tok, sig):
    """-> C statements filling float pos[3]"""
    m = re.match(r"^a(\d+),a(\d+),a(\d+)$", tok)
    if m:
        idx = [int(x) for x in m.groups()]
        for i in idx:
            if i >= len(sig.args) or sig.args[i] != "f32":
                raise ValueError("pos: a%d is not a float argument (sig %s)" % (i, sig.text))
        return "    pos[0] = a%d;\n    pos[1] = a%d;\n    pos[2] = a%d;\n" % tuple(idx)
    m = re.match(r"^a(\d+)@(twp|vec)$", tok)
    if m:
        i = int(m.group(1))
        if i >= len(sig.args) or sig.args[i] != "p":
            raise ValueError("pos: a%d is not a pointer argument (sig %s)" % (i, sig.text))
        off = 0x20 if m.group(2) == "twp" else 0
        return "    sa1g_stub_pos(pos, a%d, %d);\n" % (i, off)
    raise ValueError("pos: bad value %r" % tok)


def ret_literal(sig, val, what):
    """C return statement for a constant result"""
    r = sig.ret
    if r == "v":
        raise ValueError("%s: void function cannot return a value" % what)
    if r in ("f32", "f64"):
        try:
            float(val.rstrip("fF"))
        except ValueError:
            raise ValueError("%s: bad float %r" % (what, val))
        v = val.rstrip("fF")
        if "." not in v and "e" not in v.lower():
            v += ".0"
        return "    return %s%s;\n" % (v, "f" if r == "f32" else "")
    if not re.match(r"^-?(0x[0-9A-Fa-f]+|\d+)$", val):
        raise ValueError("%s: bad integer %r" % (what, val))
    if r == "p":
        if int(val, 0) != 0:
            raise ValueError("%s: a pointer result can only be 0 (use null)" % what)
        return "    return (void *)0;\n"
    if r == "gfn":
        return "    return SA1_GFN_C(%s);\n" % val
    return "    return (%s)(%s);\n" % (CTYPE[r], val)


def zero_return(sig):
    if not sig.ok or sig.ret == "v":
        return ""
    if sig.ret == "p":
        return "    return (void *)0;\n"
    if sig.ret == "gfn":
        return "    return SA1_GFN_NULL;\n"
    if sig.ret == "f32":
        return "    return 0.0f;\n"
    if sig.ret == "f64":
        return "    return 0.0;\n"
    return "    return 0;\n"


def unused(sig, used):
    return "".join("    (void)a%d;\n" % i for i in range(len(sig.args) if sig.ok else 0) if i not in used)


def body(name, addr, sig, policy, events=EVENTS):
    """C body of one stub; raises ValueError when the policy does not fit"""
    toks = policy.split()
    kind = toks[0]
    a = int(addr, 16)
    if kind == "fatal":
        if len(toks) != 1:
            raise ValueError("fatal takes no arguments")
        if sig.ok and slot_dispatch(addr):
            return sig.proto(name), unused(sig, set()) + "    sa1g_unimpl(0x%08Xu, \"%s\");\n" % (a, name)
        return "void %s(void)" % name, "    sa1g_unimpl(0x%08Xu, \"%s\");\n" % (a, name)
    if not sig.ok:
        raise ValueError("policy %s needs a known signature (pclower reports %r)" % (kind, sig.text))
    proto = sig.proto(name)
    if kind == "void":
        if len(toks) != 1 or sig.ret != "v":
            raise ValueError("void: only for functions returning void (sig %s), no arguments" % sig.text)
        return proto, unused(sig, set())
    if kind == "address-only":
        if len(toks) != 1:
            raise ValueError("address-only takes no arguments")
        return proto, unused(sig, set()) + zero_return(sig)
    if kind == "null":
        if len(toks) != 1 or sig.ret != "p":
            raise ValueError("null: only for functions returning a pointer (sig %s)" % sig.text)
        return proto, unused(sig, set()) + "    return (void *)0;\n"
    if kind == "ret":
        if len(toks) != 2:
            raise ValueError("ret takes one value")
        return proto, unused(sig, set()) + ret_literal(sig, toks[1], "ret")
    if kind == "freetask":
        if len(toks) != 1 or sig.text != "v:p":
            raise ValueError("freetask: only for task execs (sig v:p, not %s)" % sig.text)
        return proto, "    FreeTask(a0);\n"
    if kind == "event":
        if len(toks) < 2 or toks[1] not in events:
            raise ValueError("event: unknown kind (one of %s)" % " ".join(sorted(events)))
        opts = {}
        for t in toks[2:]:
            k, _, v = t.partition("=")
            if k not in ("id", "bank", "pos", "arg", "ret") or not v or k in opts:
                raise ValueError("event: bad option %r" % t)
            opts[k] = v
        used = set(int(x) for x in re.findall(r"a(\d+)", " ".join(opts.get(k, "") for k in ("id", "bank", "pos", "arg"))))
        idv = parse_value(opts.get("id", "0"), sig, "id")
        bank = parse_value(opts.get("bank", "0"), sig, "bank")
        arg = parse_value(opts["arg"], sig, "arg") if "arg" in opts else "0x%08Xu" % a
        b = "    float pos[3] = { 0.0f, 0.0f, 0.0f };\n" + unused(sig, used)
        if "pos" in opts:
            b += parse_pos(opts["pos"], sig)
        b += "    sa1g_event_push(%du /* SA1_EVENT_%s */, %s, %s, pos, %s);\n" % (events[toks[1]], toks[1], idv, bank, arg)
        if "ret" in opts:
            b += ret_literal(sig, opts["ret"], "event ret")
        else:
            b += zero_return(sig)
        return proto, b
    raise ValueError("unknown policy %r (one of %s)" % (kind, ", ".join(POLICIES)))


OVERLAY_AREA = (0x0C900000, 0x0D000000)     # DC RAM above 1ST_READ and its BSS: the overlay load slots


def slot_dispatch(addr, image=None):
    """a base-image stub at an overlay-slot address: dispatched to the running world's image first"""
    a = int(addr, 16) & 0x1FFFFFFF
    return image is None and OVERLAY_AREA[0] <= a < OVERLAY_AREA[1]


def dispatch_prologue(name, addr, sig):
    """C that forwards the call to the running world's overlay image when it defines this address"""
    a = int(addr, 16)
    sid = zlib.crc32(sig.text.encode()) | 1
    rt = CTYPE[sig.ret]
    fnt = "%s (*)(%s)" % (rt, ", ".join(CTYPE[x] for x in sig.args) or "void")
    call = "((%s)sa1g_e->fn)(%s)" % (fnt, ", ".join("a%d" % i for i in range(len(sig.args))))
    return ("    const sa1g_fnent *sa1g_e = sa1g_fn_image_lookup(0x%08Xu);\n"
            "    if (sa1g_e != NULL) {\n"
            "        if (sa1g_e->sig != 0x%08Xu) {\n"
            "            sa1g_fault_fn(0x%08Xu, 0x%08Xu);\n"
            "        }\n"
            "%s"
            "    }\n" % (a, sid, a, sid, ("        %s;\n        return;\n" % call) if sig.ret == "v"
                                         else "        return %s;\n" % call))


def read_tsv(path):
    rows, errors = {}, []
    with open(path, encoding="utf-8") as f:
        for n, line in enumerate(f, 1):
            line = line.rstrip("\n").rstrip("\r")
            if not line.strip() or line.lstrip().startswith("#"):
                continue
            cols = line.split("\t")
            if len(cols) != 5:
                errors.append("%s:%d: %d columns (addr name sig policy evidence)" % (path, n, len(cols)))
                continue
            addr, name, sig, policy, ev = (c.strip() for c in cols)
            if not re.match(r"^[0-9A-F]{8}$", addr):
                errors.append("%s:%d: bad address %r" % (path, n, addr))
            if not re.match(r"^[A-Za-z_][A-Za-z0-9_]*$", name):
                errors.append("%s:%d: bad name %r" % (path, n, name))
            if not policy:
                errors.append("%s:%d: empty policy" % (path, n))
            if policy.split()[0] != "fatal" and not ev:
                errors.append("%s:%d: %s: a non-fatal policy needs evidence" % (path, n, name))
            if name in rows:
                errors.append("%s:%d: %s listed twice" % (path, n, name))
            rows[name] = {"line": n, "addr": addr, "sig": sig, "policy": policy, "evidence": ev}
    return rows, errors


def comment(s):
    return s.replace("*/", "* /")


def glue_text(rep, defined, report):
    """C for the runtime functions without a DC address that no lowered TU and not the world core define"""
    need = sorted(n for n in rep.get("runtime_functions", {}) if n not in WORLD_RUNTIME)
    parts = ["/* Generated by LIB/gen/gen_stubs.py from %s. Do not edit.\n"
             " * Runtime functions without a DC address that the lowered code calls and that neither the lowered\n"
             " * runtime (src/rt) nor the world core define (pclower report.json \"runtime_functions\"). Compiled\n"
             " * with the game flags into the game-text section, without sa1/guest.h (whose prototypes some of these\n"
             " * names would contradict). */\n#include <stdint.h>\n\n"
             "__attribute__((noreturn)) void sa1g_unimpl(uint32_t g, const char *name);\n\n" % os.path.basename(report)]
    for n in need:
        if n == "njFraction" and "njRandom" in defined:
            parts.append("/* code_8C04B7BA.c (TARGET_PC): the fractional part of n. On the DC that call is njRandom\n"
                         " * 8C63A980 (modff fraction, integer part to B_8C89033C; include/sa1/ninja_funcs.h), which the\n"
                         " * lowered runtime (rt_libm.c) implements bit-exactly: call it, so libsa1 does what the\n"
                         " * reference traces did (harness/src/pc_runtime.c's n - (int)n differs for |n| >= 2^31 and -0). */\n"
                         "float njRandom(float n);\nfloat njFraction(float n);\n"
                         "float njFraction(float n)\n{\n    return njRandom(n);\n}\n\n")
        elif n == "UNK_PcWrite":
            parts.append("/* code_8C012520.c: writes a disc file to the development PC (debug disc listing, never on a\n"
                         " * character path): fatal */\n"
                         "int32_t UNK_PcWrite(char *name, void *buf, int32_t size);\n"
                         "int32_t UNK_PcWrite(char *name, void *buf, int32_t size)\n{\n"
                         "    (void)name;\n    (void)buf;\n    (void)size;\n"
                         "    sa1g_unimpl(0u, \"UNK_PcWrite\");\n}\n\n")
        else:
            parts.append("/* runtime function without a definition in this build: fatal */\n"
                         "void %s(void);\nvoid %s(void)\n{\n    sa1g_unimpl(0u, \"%s\");\n}\n\n" % (n, n, n))
    return "".join(parts), need


def write_if_changed(path, text):
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    old = open(path, encoding="utf-8").read() if os.path.exists(path) else None
    if old != text:        # keep the timestamp when nothing changed (incremental builds)
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)


def generate(report, tsv, out, summary_path=None, strict=False, quiet=False, glue_out=None, lenient=False):
    """lenient (subset lowerings): a policy that does not fit the subset's signature falls back to fatal"""
    rep = json.load(open(report, encoding="utf-8"))
    events = load_events()
    fj = os.path.join(os.path.dirname(os.path.abspath(report)), "functions.json")
    defined = set()
    if os.path.exists(fj):
        defined = set(e["name"] for e in json.load(open(fj, encoding="utf-8")).get("code_map", []) if e.get("kind") == "def")
    stubs = []
    for s in rep.get("stubs", []):
        name, addr, sig = s.split(" ", 2)
        stubs.append((name, addr, sig))
    stub_images = rep.get("stub_images", {})
    rows, errors = read_tsv(tsv) if os.path.exists(tsv) else ({}, ["%s does not exist" % tsv])
    names = set(n for n, _, _ in stubs)
    missing = sorted(n for n in names if n not in rows)
    stale = sorted(n for n in rows if n not in names)
    sigdiff, counts = [], {}
    dispatched = 0
    parts = ["/* Generated by LIB/gen/gen_stubs.py from gen/policy/stubs.tsv and %s. Do not edit.\n"
             " * Boundary stubs of the lowered game code (sadc docs/libsa1_integration.md 10.5): one definition per\n"
             " * function pclower could not resolve, with its policy. Compiled with the game flags into the game-text\n"
             " * section (a fault in here is a world fault). */\n" % os.path.basename(report),
             "#include <stddef.h>\n#include <stdint.h>\n\n#include \"sa1/guest.h\"\n\n",
             "/* the world core (src/world/sa1_world.h) and the lowered task system (FreeTask 8C098786) */\n"
             "void sa1g_event_push(uint32_t type, uint32_t id, uint32_t bank, const float pos[3], uint32_t arg);\n"
             "void FreeTask(void *tp);\n\n"
             "/* the running world's overlay images (pclower's sa1g_fntab.c, design 2.5) */\n"
             "const sa1g_fnent *sa1g_fn_image_lookup(uint32_t g);\n\n"
             "/* a position (SA1 space) read from a host pointer into the arena: taskwk pos at +0x20, NJS_POINT3 at +0;\n"
             " * anything else (NULL, a raw -1 \"no owner\" that a caller passes as an integer) gives (0, 0, 0) */\n"
             "static void sa1g_stub_pos(float pos[3], const void *p, size_t off)\n{\n"
             "    if (p != NULL && (uintptr_t)p - (uintptr_t)sa1g_ram_base <= 0x1000000u - off - 3 * sizeof(float)) {\n"
             "        __builtin_memcpy(pos, (const char *)p + off, 3 * sizeof(float));\n    }\n}\n\n"]
    for name, addr, sigtext in sorted(stubs, key=lambda t: (t[1], t[0])):
        row = rows.get(name)
        policy = row["policy"] if row else "fatal"
        sig = Sig(sigtext)
        if row and row["sig"] != sigtext:
            sigdiff.append("%s: tsv %s, pclower %s" % (name, row["sig"], sigtext))
        if row and row["addr"] != addr:
            errors.append("%s:%d: %s: address %s, pclower %s" % (tsv, row["line"], name, row["addr"], addr))
        try:
            proto, b = body(name, addr, sig, policy, events)
        except ValueError as ex:
            if not lenient:
                errors.append("%s:%d: %s (%s): %s" % (tsv, row["line"] if row else 0, name, policy, ex))
                continue
            sigdiff.append("%s: policy %r does not fit %s here (%s): fatal" % (name, policy, sigtext, ex))
            policy = "fatal"
            proto, b = body(name, addr, sig, policy, events)
        counts[policy.split()[0]] = counts.get(policy.split()[0], 0) + 1
        why = comment(row["evidence"])[:300] if row else "no line in stubs.tsv: default policy"
        img = stub_images.get(name)
        disp = sig.ok and slot_dispatch(addr, img)
        parts.append("/* %s %s %s: %s%s%s\n * %s */\n" % (addr, name, sigtext, policy,
                                                       " (overlay image %s)" % img if img else "",
                                                       " (+ the running world's overlay image)" if disp else "", why))
        if disp:
            dispatched += 1
            b = dispatch_prologue(name, addr, sig) + b
        parts.append("%s;\n%s\n{\n%s}\n\n" % (proto, proto, b))
    gtext, glued = glue_text(rep, defined, report) if glue_out else ("", [])
    summary = {"report": os.path.abspath(report), "tsv": os.path.abspath(tsv), "stubs": len(stubs), "policies": counts,
               "image_stubs": len([n for n, a, sg in stubs if n in stub_images]), "slot_dispatch": dispatched,
               "missing": missing, "stale": stale, "signature_changed": sigdiff, "runtime_glue": glued, "errors": errors}
    if strict and missing:
        errors.append("--strict: %d stub(s) without a line in %s: %s" % (len(missing), tsv, " ".join(missing)))
    if not errors:
        write_if_changed(out, "".join(parts))
        if glue_out:
            write_if_changed(glue_out, gtext)
    if summary_path:
        os.makedirs(os.path.dirname(os.path.abspath(summary_path)), exist_ok=True)
        with open(summary_path, "w", encoding="utf-8", newline="\n") as f:
            json.dump(summary, f, indent=1)
    if not quiet:
        print("gen_stubs: %d stubs: %s%s" % (len(stubs), ", ".join("%s %d" % kv for kv in sorted(counts.items())),
                                               ("; runtime glue: " + " ".join(glued)) if glued else ""))
        if missing:
            print("gen_stubs: WARNING %d stub(s) have no line in %s (default fatal): %s" % (
                len(missing), os.path.relpath(tsv, LIB), " ".join(missing[:30]) + (" ..." if len(missing) > 30 else "")))
        if stale:
            print("gen_stubs: %d line(s) for functions that are no longer stubs (decompiled now; ignored): %s" % (
                len(stale), " ".join(stale[:30]) + (" ..." if len(stale) > 30 else "")))
        for s in sigdiff:
            print("gen_stubs: signature changed: " + s)
        for e in errors:
            print("gen_stubs: error: " + e)
    return summary


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--report", required=True, help="pclower report.json (its \"stubs\")")
    ap.add_argument("--tsv", default=os.path.join(HERE, "policy", "stubs.tsv"))
    ap.add_argument("--out", required=True, help="C file to write")
    ap.add_argument("--summary", help="JSON summary (counts, missing, stale lines)")
    ap.add_argument("--glue", help="also write the runtime glue (njFraction, UNK_PcWrite ...) to this C file")
    ap.add_argument("--strict", action="store_true", help="a stub without a line is an error")
    ap.add_argument("--quiet", action="store_true")
    a = ap.parse_args()
    s = generate(a.report, a.tsv, a.out, a.summary, a.strict, a.quiet, a.glue)
    return 1 if s["errors"] else 0


if __name__ == "__main__":
    sys.exit(main())
