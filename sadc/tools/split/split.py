#!/usr/bin/env python3
"""Split a Dreamcast SH-4 binary into Hitachi asmsh sources.

Usage: py -3.12 tools/split/split.py config/<name>.yaml

Reads the target binary named in the config, analyses code ranges, and writes
  asm/<name>/text/*.src   code chunks (cut where no PC-relative ref crosses)
  asm/<name>/data/*.src   data chunks
  asm/<name>/bss.src      BSS labels
  build/<name>.sub        linker subcommand file (order = address order)
  build/<name>.manifest.json  chunk/function inventory for configure.py
"""
import hashlib
import json
import os
import sys

import yaml

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from split.analysis import Image, CodeAnalysis  # noqa: E402
from split.emit import Labeler, Emitter, cut_points, COL  # noqa: E402
from split.xrefs import build_xrefs  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))


def load_symbols(paths):
    names = {}
    for p in paths:
        p = os.path.join(ROOT, p)
        if not os.path.exists(p):
            continue
        for line in open(p, encoding="utf-8"):
            line = line.split("//")[0].strip()
            if not line or "=" not in line:
                continue
            name, val = line.split("=", 1)
            names[int(val.strip().rstrip(";"), 16)] = name.strip()
    return names


def main():
    cfg = yaml.safe_load(open(sys.argv[1]))
    name = cfg["name"]
    data = open(os.path.join(ROOT, cfg["target"]), "rb").read()
    sha1 = hashlib.sha1(data).hexdigest()
    if cfg.get("sha1") and cfg["sha1"] != sha1:
        sys.exit(f"{cfg['target']}: sha1 mismatch ({sha1})")
    base = cfg["base"]
    img = Image(data, base, cfg.get("bss_end", base + len(data)))
    code_ranges = [tuple(r) for r in cfg["code_ranges"]]
    code_ranges = [(lo, min(hi, img.end)) for lo, hi in code_ranges]
    names = load_symbols(cfg.get("symbol_files", []))
    extern = {}
    for other in cfg.get("link_with", []):
        p = os.path.join(ROOT, "build", f"{other}.symbols.txt")
        if not os.path.exists(p):
            sys.exit(f"{name}: link_with {other}: split it first ({p} missing)")
        for line in open(p):
            n, v = line.split("=")
            extern[int(v.strip().rstrip(";"), 16)] = n.strip()
    ca = CodeAnalysis(img, code_ranges, log=lambda m: print(f"{name}: {m}"))
    seeds = [(s, "entry") for s in cfg.get("seeds", [])]
    seeds += [(a, "symbol") for a, n in names.items() if ca.in_code(a) and not a & 1]
    data_ranges = []
    prev = base
    for lo, hi in code_ranges:
        if lo > prev:
            data_ranges.append((prev, lo))
        prev = hi
    if prev < img.end:
        data_ranges.append((prev, img.end))
    ca.run(seeds, data_ptr_ranges=data_ranges)
    print(f"{name}: {len(ca.funcs)} functions, {len(ca.jtab)} jump tables")

    # effective code ends: cover every instruction and literal referenced
    regions = []   # (lo, hi, kind)
    prev = base
    for lo, hi in code_ranges:
        top = lo
        for a, ins in ca.insn.items():
            if lo <= a < hi:
                top = max(top, a + 2)
                pc = ins.pcrel
                if pc:   # literal pools may lie past a heuristic range end
                    top = max(top, pc[2] + (2 if pc[1] == 2 else 4))
        for a, k in ca.data.items():
            if lo <= a < hi:
                top = max(top, a + (4 if k == "L" else 2))
        for ta, jt in ca.jtab.items():
            if lo <= ta < hi:
                top = max(top, ta + jt.count * jt.esize)
        # absorb trailing padding up to 4-byte alignment
        top = (top + 3) & ~3
        if lo > prev:
            regions.append((prev, lo, "data"))
        regions.append((lo, top, "code"))
        prev = top
    if prev < img.end:
        regions.append((prev, img.end, "data"))
    # code that branches / loads across a "data" gap means the gap belongs to
    # the same object: merge the code regions (and the gap) into one
    code = [[lo, hi] for lo, hi, k in regions if k == "code"]
    refs = []
    for a, ins in ca.insn.items():
        t = ins.branch_target
        if t is not None:
            refs.append((a, t))
        if ins.pcrel:
            refs.append((a, ins.pcrel[2]))
    for ja, t in ca.long_branches.items():
        refs.append((ja, t))
    for ta, jt in ca.jtab.items():
        refs.extend((ta, t) for t in jt.targets)

    def region_index(x):
        for i, (lo, hi) in enumerate(code):
            if lo <= x < hi:
                return i
        return None

    changed = True
    while changed:
        changed = False
        for a, t in refs:
            i, j = region_index(a), region_index(t)
            if i is None or i == j:
                continue
            if j is None and not (base <= t < img.end):
                continue
            lo = min(code[i][0], t if j is None else code[j][0])
            hi = max(code[i][1], (t + 4) & ~3 if j is None else code[j][1])
            code = [c for c in code if c[1] <= lo or c[0] >= hi] + [[lo, hi]]
            # absorb anything now overlapping
            code.sort()
            merged = []
            for c in code:
                if merged and c[0] < merged[-1][1]:
                    merged[-1][1] = max(merged[-1][1], c[1])
                else:
                    merged.append(c)
            code = merged
            changed = True
            break
    regions = []
    prev = base
    for lo, hi in code:
        if lo > prev:
            regions.append((prev, lo, "data"))
        regions.append((lo, hi, "code"))
        prev = hi
    if prev < img.end:
        regions.append((prev, img.end, "data"))
    bss_lo = img.end
    # code regions may have grown to cover their literal pools: labels and
    # classification must use the grown ranges
    ca.code_ranges = sorted((lo, hi) for lo, hi, kind in regions if kind == "code")

    lab = Labeler(img, ca, names, data_ranges, extern)
    em = Emitter(img, ca, lab)

    chunks = []   # (lo, hi, kind, section)
    ncode = ndata = 0
    for lo, hi, kind in regions:
        if kind == "code":
            ncode += 1
            sec = "P" if ncode == 1 else f"P{ncode}"
            cuts = [lo] + cut_points(ca, lo, hi) + [hi]
            for a, b in zip(cuts, cuts[1:]):
                chunks.append((a, b, "code", sec))
        else:
            ndata += 1
            sec = "D" if ndata == 1 else f"D{ndata}"
            step = cfg.get("data_chunk", 0x8000)
            a = lo
            while a < hi:
                b = min(hi, (a + step) & ~3) if hi - a > step else hi
                chunks.append((a, b, "data", sec))
                a = b
    bss_hi = img.bss_end
    if bss_hi > bss_lo:
        chunks.append((bss_lo, bss_hi, "bss", "B"))

    em.collect_pointers([(a, b) for a, b, k, s in chunks if k == "code"],
                        [(a, b) for a, b, k, s in chunks if k != "code"])

    asm_dir = os.path.join(ROOT, cfg.get("asm_dir", f"asm/{name.lower()}"))
    for sub in ("text", "data"):
        os.makedirs(os.path.join(asm_dir, sub), exist_ok=True)
    manifest = {"name": name, "base": base, "end": img.end, "sha1": sha1, "chunks": [],
                "sections": [], "target": cfg["target"], "asm_dir": cfg.get("asm_dir"),
                "prs": cfg.get("prs"), "prs_sha1": cfg.get("prs_sha1")}
    glob_sorted = sorted(lab.glob.items())
    import bisect
    gaddrs = [a for a, _ in glob_sorted]
    all_defined = set(lab.glob[a] for a in lab.glob if base <= a < img.bss_end)
    ext_needed = set()
    for lo, hi, kind, sec in chunks:
        lines = []
        exported, imported = set(), set()
        i0, i1 = bisect.bisect_left(gaddrs, lo), bisect.bisect_left(gaddrs, hi)
        defined = {glob_sorted[i][1] for i in range(i0, i1)}
        if kind == "code":
            em.emit_code(lo, hi, lines, exported, imported, defined)
            rel = f"text/{lo:08X}.src"
            attr = "CODE"
        elif kind == "data":
            em.emit_data(lo, hi, lines, exported, imported, defined)
            rel = f"data/{lo:08X}.src"
            attr = "DATA"
        else:
            em.emit_data(lo, hi, lines, exported, imported, defined)
            rel = "bss.src"
            attr = "DATA"
        imported -= defined
        ext_needed |= {x for x in imported if x not in all_defined}
        hdr = [f"; {name} {lo:08X}-{hi:08X} ({kind}) -- generated by tools/split/split.py"]
        for s in sorted(imported):
            hdr.append(f"{COL}{'.IMPORT':<11} {s}")
        for s in sorted(exported):
            hdr.append(f"{COL}{'.EXPORT':<11} {s}")
        hdr.append(f"{COL}{'.SECTION':<11} {sec},{attr},ALIGN=4")
        path = os.path.join(asm_dir, rel)
        with open(path, "w", newline="\n") as f:
            f.write("\n".join(hdr + lines + [f"{COL}.END", ""]))
        funcs = sorted(f for f in ca.funcs if lo <= f < hi)
        manifest["chunks"].append({
            "src": os.path.relpath(path, ROOT).replace("\\", "/"),
            "start": lo, "end": hi, "kind": kind, "section": sec,
            "functions": [[f, lab.glob[f][1:]] for f in funcs],
        })
    with open(os.path.join(ROOT, "build", f"{name}.imports.txt"), "w") as f:
        for x in sorted(ext_needed):
            f.write(x[1:] + "\n")
    secs = []
    for lo, hi, kind, sec in chunks:
        if sec not in secs:
            secs.append(sec)
    manifest["sections"] = secs
    manifest["link_with"] = cfg.get("link_with", [])
    os.makedirs(os.path.join(ROOT, "build"), exist_ok=True)
    with open(os.path.join(ROOT, "build", f"{name}.manifest.json"), "w") as f:
        json.dump(manifest, f, indent=1)
    # symbol table of every exported label (for other images and tools)
    with open(os.path.join(ROOT, "build", f"{name}.symbols.txt"), "w") as f:
        for a, s in glob_sorted:
            f.write(f"{s[1:]} = 0x{a:08X};\n")
    xr = build_xrefs(img, ca, lab)
    with open(os.path.join(ROOT, "build", f"{name}.xrefs.json"), "w") as f:
        json.dump({f"{a:08X}": v for a, v in xr.items()}, f)
    print(f"{name}: wrote {len(chunks)} chunks ({sum(1 for c in chunks if c[2]=='code')} code)")


if __name__ == "__main__":
    main()
