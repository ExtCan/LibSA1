#!/usr/bin/env python3
"""Splice SHC-compiled functions into a generated asm chunk.

    splice.py <IMAGE> <chunk.src> <out.src> <compiled.src>...

For every function defined in the compiled .src files (SHC -code=asmcode
output) that lives in this chunk, the function is verified against the
original binary (tools/shcdiff.compare) and its instruction lines in the
chunk are replaced with the compiler's instructions. PC-relative and branch
operands are re-pointed at the chunk's original labels, so literal pools that
SHC shares between functions keep their original layout even while the rest
of the translation unit is still assembly. Any mismatch fails the build.
"""
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import shcdiff  # noqa: E402
from sh4.isa import decode  # noqa: E402

INSN_RE = re.compile(r";\s([0-9A-F]{8})\s([0-9A-F]{4})\s*$")


def operands(stmt):
    parts = stmt.split(None, 1)
    mn = parts[0]
    ops = shcdiff.split_operands(parts[1]) if len(parts) > 1 else []
    return mn, [o.strip() for o in ops]


STUB_RE = re.compile(r"ASM_STUB\s*\(.*?\b([A-Za-z_]\w*)\s*\(")


def asm_stubs(compiled_src):
    """Names declared with ASM_STUB(...) in the C file behind a compiled .src."""
    rel = os.path.relpath(compiled_src, os.path.join(ROOT, "build"))
    c_path = os.path.join(ROOT, os.path.splitext(rel)[0] + ".c")
    names = set()
    if os.path.exists(c_path):
        for line in open(c_path, encoding="utf-8", errors="replace"):
            m = STUB_RE.search(line)
            if m:
                names.add(m.group(1))
    return names


def main():
    image, chunk_src, out_src = sys.argv[1], sys.argv[2], sys.argv[3]
    compiled = sys.argv[4:]
    orig = shcdiff.Original(image)
    cfg = shcdiff.compiler_cfg()
    chunk_rel = os.path.relpath(chunk_src, ROOT).replace("\\", "/")
    chunk = next(c for c in orig.man["chunks"] if c["src"] == chunk_rel)
    funcs = chunk["functions"]
    lines = open(chunk_src).read().split("\n")
    # index instruction lines by address
    at = {}
    for i, l in enumerate(lines):
        m = INSN_RE.search(l)
        if m:
            at[int(m.group(1), 16)] = i
    replaced = 0
    errors = []
    # one C file can feed several chunks (src/1st_read/code_8C0585A6.c: 8C0584A0 and 8C058640), whose splices ninja
    # runs in parallel: each assembles the compiled .src into an .obj / .lis named after its own chunk
    chunk_tag = os.path.splitext(os.path.basename(chunk_rel))[0]
    for csrc in compiled:
        lst = shcdiff.Listing(csrc, cfg["version"],
                              out_stem=os.path.splitext(csrc)[0] + "__" + chunk_tag)
        stubs = asm_stubs(csrc)
        for lab in lst.funcs:
            name = lab[1:] if lab.startswith("_") else lab
            if name in stubs:
                continue
            addr = orig.sym2addr.get(name)
            if addr is None or not any(a == addr for a, n in funcs):
                continue
            match, rows, no, nm = shcdiff.compare(orig, lst, name)
            if not match:
                bad = "\n".join(f"   {r[1] and format(r[1], '08X')} {r[2]:<32} | {r[3]:<32} {r[4]}"
                                for r in rows if not r[0])
                errors.append(f"{name}: does not match ({no} vs {nm} insns)\n{bad}")
                continue
            mi = lst.function_insns(name)
            oi = orig.function_insns(addr)
            for (oa, ow), (ma, mw, stmt) in zip(oi, mi):
                li = at[oa]
                old = lines[li]
                ostmt = old.split(";")[0].strip()
                omn, oops = operands(ostmt)
                mmn, mops = operands(stmt.split(";")[0].strip())
                ins = decode(mw, ma)
                if ins.pcrel or ins.branch_target is not None:
                    # first operand is the label / target: keep the original's
                    mops[0] = oops[0]
                new = f"{mmn:<11} {','.join(mops)}" if mops else mmn
                lines[li] = f"          {new:<40}; {oa:08X} {ow:04X} C"
            replaced += 1
    if errors:
        sys.exit("splice failed for " + chunk_rel + ":\n" + "\n".join(errors))
    os.makedirs(os.path.dirname(os.path.abspath(out_src)), exist_ok=True)
    with open(out_src, "w", newline="\n") as f:
        f.write("\n".join(lines))


if __name__ == "__main__":
    main()
