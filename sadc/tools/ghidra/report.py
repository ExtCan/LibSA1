#!/usr/bin/env python3
"""Summarize a SadcExport output directory (functions.json + summary.json).

Usage: py -3.12 report.py [OUTDIR]   (default: sadc/build/ghidra/1ST_READ)
"""
import json
import os
import sys
from collections import Counter

HERE = os.path.dirname(os.path.abspath(__file__))
out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "..", "build", "ghidra", "1ST_READ")
funcs = json.load(open(os.path.join(out, "functions.json"), encoding="utf-8"))
summ = json.load(open(os.path.join(out, "summary.json"), encoding="utf-8"))

print("program:", summ["program"], " functions:", summ["function_count"],
      " after auto-analysis:", summ.get("functions_after_analysis"))
print("%-10s %-23s %5s %6s %9s %9s %9s %9s  found_by" % (
    "block", "range", "perm", "funcs", "fn_bytes", "insn_b", "orphan_b", "undef_b"))
for b in summ["blocks"]:
    print("%-10s %s-%s %5s %6d %9d %9d %9d %9d  %s" % (
        b["name"], b["start"], b["end"], b["perms"], b["functions"], b["function_bytes"],
        b["instruction_bytes"], b.get("orphan_instruction_bytes", 0), b["undefined_bytes"],
        json.dumps(b["found_by"])))
print("decomp:", json.dumps(summ["decomp"]), " thunks:", summ.get("thunks"),
      " noreturn:", len(summ.get("noreturn", [])))
for k in ("pool_items", "weakseed_functions", "orphan_functions", "gapfill_functions",
          "tailcall_overrides"):
    print("  %s: %s" % (k, summ.get(k)))
print("timing:", json.dumps(summ["timing"]))

align = Counter()
for f in funcs:
    e = int(f["entry"], 16)
    align[32 if e % 32 == 0 else 16 if e % 16 == 0 else 4 if e % 4 == 0 else 2] += 1
print("entry alignment (largest power of two dividing entry, capped at 32):", dict(align))
bad = [f for f in funcs if f["decomp"] not in ("ok", "skipped", "skipped_uninitialized")]
print("decomp failures: %d" % len(bad))
for f in bad[:20]:
    print("  %s %s %s" % (f["entry"], f["name"], f["decomp"][:100]))
slow = sorted((f for f in funcs if "decomp_ms" in f), key=lambda f: -f["decomp_ms"])[:10]
print("slowest decompiles:", ", ".join("%s=%dms" % (f["entry"], f["decomp_ms"]) for f in slow))
