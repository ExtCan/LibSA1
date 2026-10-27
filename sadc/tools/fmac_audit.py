#!/usr/bin/env python3
"""FMAC audit: compare, per decompiled function, the number of FMAC instructions in
the original SH-4 code with the number of fused multiply-adds the libsa1 (TARGET_PC)
build emits for the C (-mfma -ffp-contract=off: only explicit SA1_FMAC/fmaf fuse).

    py -3.12 tools/fmac_audit.py [files...] [--all] [--json build/verify/fmac_audit.json]

A function whose counts differ computes some a*b+c with a different rounding than
the original (one rounding for FMAC, two for FMUL+FADD) unless the difference is
explained by inlining. Uses the harness's native copies of the sources
(harness/build/gen_win32_nm_pc/native_src, float literals rewritten); build them
first with "py -3.12 harness/build.py --non-matching --pc".

The native tree is a copy made when the harness was last built: it does not follow
src/1st_read by itself. Before an audit the tree is compared with the sources
(native_manifest.json next to native_src, and the timestamps): a src/1st_read/*.c
file that the tree does not have (decompiled since), one newer than its native copy
(edited since), or a copy whose source is gone makes the tree STALE, and the audit
refuses to run (exit 2; a stale tree silently skips or audits old code: fix060's
18 new functions were "audited" against a tree without their files). Refresh it:

    py -3.12 harness/build.py --non-matching --pc                 # the default tree
    py -3.12 harness/build.py --non-matching --pc --tag NAME      # a private tree
    py -3.12 tools/fmac_audit.py --tag NAME ...                   # audit that one

(--native DIR names any native_src directory; --allow-stale audits a stale tree
anyway, with a warning). Files given as paths that exist (src/1st_read/code_X.c) are
compiled as they are and need no tree.
"""
import argparse
import concurrent.futures
import json
import os
import re
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
NSRC = os.path.join(ROOT, "harness", "build", "gen_win32_nm_pc", "native_src")
ASM = os.path.join(ROOT, "asm", "1st_read", "text")
FMA_RE = re.compile(r"\bv?f(n)?m(add|sub)\d*[sp]s\b")


def sh4_fmac_counts():
    xr = json.load(open(os.path.join(ROOT, "build", "1ST_READ.xrefs.json")))
    names = {v["name"] for v in xr.values()}
    counts = {}
    # only the chunk files of the current split (asm/ may hold stale files of older splits)
    man = json.load(open(os.path.join(ROOT, "build", "1ST_READ.manifest.json")))
    srcs = [os.path.join(ROOT, c["src"]) for c in man["chunks"] if c["src"].replace("\\", "/").startswith("asm/1st_read/text/")]
    for path in srcs:
        cur = None
        for line in open(path, encoding="utf-8", errors="replace"):
            m = re.match(r"^_([A-Za-z_]\w*):", line)
            if m and m.group(1) in names:
                cur = m.group(1)
                counts.setdefault(cur, 0)
                continue
            if cur and re.match(r"^\s+FMAC\b", line):
                counts[cur] += 1
    return counts


def native_tree_status(nsrc, names=None):
    """How the native tree nsrc stands against src/1st_read: {"stale": bool, "lines": [...], ...}.
    names: only these basenames (None: the whole tree)."""
    src_dir = os.path.join(ROOT, "src", "1st_read")
    gen = os.path.dirname(nsrc)
    man_path = os.path.join(gen, "native_manifest.json")
    st = {"tree": nsrc, "stale": False, "lines": [], "new": [], "changed": [], "removed": [], "config": None}
    if not os.path.isdir(nsrc) or not os.path.exists(man_path):
        st["stale"] = True
        st["lines"].append("no native tree at %s (or no %s)" % (nsrc, os.path.basename(man_path)))
        return st
    man = json.load(open(man_path, encoding="utf-8"))
    built = os.path.getmtime(man_path)
    if not (man.get("non_matching") and man.get("target_pc")):
        st["config"] = "non_matching %s, target_pc %s" % (man.get("non_matching"), man.get("target_pc"))
        st["lines"].append("the tree was not made by a --non-matching --pc build (%s)" % st["config"])
    files = man.get("files", {})
    want = set(names) if names is not None else None
    for f in sorted(x for x in os.listdir(src_dir) if x.endswith(".c")):
        if want is not None and f not in want:
            continue
        if f not in files:
            st["new"].append(f)
            continue
        copy = os.path.join(nsrc, f)
        ref = os.path.getmtime(copy) if os.path.exists(copy) else built   # excluded files have no copy
        if os.path.getmtime(os.path.join(src_dir, f)) > ref + 1.0:
            st["changed"].append(f)
    for f, e in sorted(files.items()):
        if want is not None and f not in want:
            continue
        if not os.path.exists(os.path.join(ROOT, e.get("source", "src/1st_read/" + f))):
            st["removed"].append(f)
    for key, what in (("new", "in src/1st_read but not in the tree (decompiled since the tree was made)"),
                      ("changed", "newer in src/1st_read than their native copies (edited since)"),
                      ("removed", "in the tree but no longer in src/1st_read")):
        if st[key]:
            more = " ..." if len(st[key]) > 8 else ""
            st["lines"].append("%d file(s) %s: %s%s" % (len(st[key]), what, ", ".join(st[key][:8]), more))
    st["stale"] = bool(st["new"] or st["changed"] or st["removed"] or st["config"])
    return st


def pc_fma_counts(src):
    zig = [sys.executable, "-m", "ziglang", "cc"]
    out = os.path.join(os.environ.get("TEMP", "/tmp"), "fmac_%d_%s.s" % (os.getpid(), os.path.basename(src)))
    cmd = zig + ["-target", "x86-windows-gnu", "-O2", "-msse2", "-mfpmath=sse", "-mfma", "-ffp-contract=off",
                 "-frounding-math", "-fno-builtin", "-fno-slp-vectorize", "-fno-vectorize", "-fno-strict-aliasing", "-fwrapv", "-w", "-S",
                 "-DTARGET_PC", "-DNON_MATCHING",
                 "-include", os.path.join(ROOT, "harness", "src", "harness_pre.h"),
                 "-I", os.path.join(ROOT, "include"), "-I", os.path.join(ROOT, "src"),
                 "-I", os.path.join(ROOT, "src", "1st_read"), src, "-o", out]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        return None
    counts, cur = {}, None
    for line in open(out, encoding="utf-8", errors="replace"):
        m = re.match(r"^_?([A-Za-z_]\w*):", line)
        if m and not m.group(1).startswith(("L", "LBB", "LCPI", "Ltmp", "Lfunc")):
            cur = m.group(1)
            counts.setdefault(cur, 0)
            continue
        if cur and FMA_RE.search(line):
            counts[cur] += 1
    os.remove(out)
    return counts


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("files", nargs="*")
    ap.add_argument("--json", default=None, help="write the result here (default build/verify/fmac_audit.json "
                                                 "for a full run, nothing for selected files)")
    ap.add_argument("-j", type=int, default=os.cpu_count() or 4)
    ap.add_argument("--native", help="the native_src directory to audit (default harness/build/gen_win32_nm_pc/native_src)")
    ap.add_argument("--tag", help="the native tree of a private harness build: harness/build/gen_tag_TAG/native_src")
    ap.add_argument("--allow-stale", action="store_true",
                    help="audit a native tree that is older than src/1st_read (a warning instead of refusing)")
    a = ap.parse_args()
    if a.native:
        nsrc = os.path.abspath(a.native)
    elif a.tag:
        nsrc = os.path.join(ROOT, "harness", "build", "gen_tag_" + a.tag, "native_src")
    else:
        nsrc = NSRC
    # a path to an existing file (e.g. src/1st_read/code_X.c) is compiled as is; a bare
    # name is taken from the harness's native copies, which must not be older than src/1st_read
    from_tree = [os.path.basename(f) for f in a.files if not os.path.exists(f)]
    tree = None
    if not a.files or from_tree:
        tree = native_tree_status(nsrc, from_tree or None)
        if tree["stale"]:
            print("%s: the native tree %s is STALE against src/1st_read:" %
                  ("WARNING" if a.allow_stale else "refusing", os.path.relpath(nsrc, ROOT)))
            for line in tree["lines"]:
                print("  " + line)
            print("  refresh it: py -3.12 harness/build.py --non-matching --pc%s, then run this again "
                  "(--allow-stale audits it anyway)" % (" --tag " + a.tag if a.tag else ""))
            if not a.allow_stale or not os.path.isdir(nsrc):
                return 2
    files = [os.path.abspath(f) if os.path.exists(f) else os.path.join(nsrc, os.path.basename(f)) for f in a.files] \
        or sorted(os.path.join(nsrc, f) for f in os.listdir(nsrc) if f.endswith(".c"))
    sh = sh4_fmac_counts()
    res, bad_files = {}, []
    with concurrent.futures.ThreadPoolExecutor(a.j) as ex:
        for f, pc in zip(files, ex.map(pc_fma_counts, files)):
            if pc is None:
                bad_files.append(os.path.basename(f))
                continue
            for fn, n in pc.items():
                if fn in sh:
                    res[fn] = {"file": os.path.basename(f), "sh4": sh[fn], "pc": n}
    mism = {k: v for k, v in res.items() if v["sh4"] != v["pc"]}
    if a.json is None and not a.files:
        a.json = os.path.join(ROOT, "build", "verify", "fmac_audit.json")
    if a.json:
        os.makedirs(os.path.dirname(a.json), exist_ok=True)
    if a.json:
        json.dump({"functions": res, "mismatch": mism, "not_compiled": bad_files,
                   "native_tree": None if tree is None else {"dir": os.path.relpath(nsrc, ROOT), "stale": tree["stale"],
                                                             "notes": tree["lines"]}},
                  open(a.json, "w"), indent=1)
    print("%d functions compared, %d with a different FMAC count, %d files did not compile" %
          (len(res), len(mism), len(bad_files)))
    for k, v in sorted(mism.items(), key=lambda kv: kv[1]["file"]):
        print("  %-28s %-18s SH-4 FMAC %3d   PC FMA %3d" % (k, v["file"], v["sh4"], v["pc"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())