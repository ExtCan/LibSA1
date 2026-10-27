#!/usr/bin/env python3
"""Make interrupted / work-in-progress C files safe for the build.

    py -3.12 tools/guard_nonmatching.py [--image 1ST_READ] [files...]

For every C file (default: all in the image's src dir):
  * if it does not compile, it is moved to wip/<imgdir>/ (nothing is deleted);
  * every function that compiles but does not match is wrapped as
        #ifdef NON_MATCHING <definition> #else ASM_STUB(<prototype>) #endif
    so the build keeps the original assembly for it while same-file callers
    still see a definition.
Re-verifies each edited file; prints a summary.
"""
import argparse
import os
import re
import shutil
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import shcdiff  # noqa: E402
from splice import asm_stubs  # noqa: E402


def find_definition(text, name):
    """(start, end, prototype) of the definition of `name`, or None."""
    for m in re.finditer(r"(?m)^[^\n;{}#]*?\b" + re.escape(name) + r"\s*\(", text):
        i = m.end() - 1
        depth = 0
        while i < len(text):
            ch = text[i]
            if ch == "(":
                depth += 1
            elif ch == ")":
                depth -= 1
                if depth == 0:
                    break
            i += 1
        close = i
        j = close + 1
        while j < len(text) and text[j] in " \t\r\n":
            j += 1
        if j >= len(text) or text[j] != "{":
            continue
        # balanced braces, skipping comments and literals
        depth, k = 0, j
        while k < len(text):
            if text.startswith("/*", k):
                k = text.index("*/", k) + 2
                continue
            ch = text[k]
            if ch in "\"'":
                q = ch
                k += 1
                while k < len(text) and text[k] != q:
                    k += 2 if text[k] == "\\" else 1
            elif ch == "{":
                depth += 1
            elif ch == "}":
                depth -= 1
                if depth == 0:
                    proto = " ".join(text[m.start():close + 1].split())
                    return m.start(), k + 1, proto
            k += 1
    return None


def check(path, orig, cfg):
    out_src = os.path.join(ROOT, "build", "diff", os.path.splitext(os.path.basename(path))[0] + ".src")
    os.makedirs(os.path.dirname(out_src), exist_ok=True)
    shcdiff.compile_c(path, out_src, cfg)
    lst = shcdiff.Listing(out_src, cfg["version"])
    stubs = asm_stubs_for(path)
    bad = []
    for lab in lst.funcs:
        name = lab[1:] if lab.startswith("_") else lab
        if name in stubs or name not in orig.sym2addr:
            continue
        a = orig.sym2addr[name]
        if a not in orig.chunk_of:
            continue
        match, rows, no, nm = shcdiff.compare(orig, lst, name)
        if not match:
            bad.append(name)
    return bad


STUB_RE = re.compile(r"ASM_STUB\s*\(.*?\b([A-Za-z_]\w*)\s*\(")


def asm_stubs_for(path):
    return set(STUB_RE.findall(open(path, encoding="utf-8", errors="replace").read()))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--image", default="1ST_READ")
    ap.add_argument("files", nargs="*")
    a = ap.parse_args()
    orig = shcdiff.Original(a.image)
    cfg = shcdiff.compiler_cfg()
    imgdir = (orig.man.get("asm_dir") or f"asm/{a.image.lower()}").replace("asm/", "", 1)
    src_dir = os.path.join(ROOT, "src", imgdir)
    files = a.files or sorted(os.path.join(src_dir, f) for f in os.listdir(src_dir) if f.endswith(".c"))
    moved, guarded = [], {}
    for path in files:
        path = os.path.abspath(path)
        try:
            bad = check(path, orig, cfg)
        except RuntimeError:
            dst = os.path.join(ROOT, "wip", imgdir)
            os.makedirs(dst, exist_ok=True)
            shutil.move(path, os.path.join(dst, os.path.basename(path)))
            moved.append(os.path.basename(path))
            continue
        if not bad:
            continue
        text = open(path, encoding="utf-8").read()
        done = []
        for name in bad:
            d = find_definition(text, name)
            if d is None:
                continue
            s, e, proto = d
            block = ("#ifdef NON_MATCHING\n" + text[s:e] + "\n#else\nASM_STUB(" + proto + ")\n#endif")
            text = text[:s] + block + text[e:]
            done.append(name)
        open(path, "w", encoding="utf-8", newline="\n").write(text)
        try:
            still = check(path, orig, cfg)
        except RuntimeError:
            still = ["<compile error>"]
        if still:
            dst = os.path.join(ROOT, "wip", imgdir)
            os.makedirs(dst, exist_ok=True)
            shutil.move(path, os.path.join(dst, os.path.basename(path)))
            moved.append(os.path.basename(path) + f" (after guarding: {still})")
        else:
            guarded[os.path.basename(path)] = done
    for f, names in guarded.items():
        print(f"guarded {f}: {', '.join(names)}")
    for f in moved:
        print(f"moved to wip/: {f}")
    print(f"{len(guarded)} files guarded, {len(moved)} moved")


if __name__ == "__main__":
    main()
