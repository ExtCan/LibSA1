#!/usr/bin/env python3
"""Collect decompilation-batch results from a workflow journal.

    py -3.12 tools/collect_batch.py <workflow transcript dir or journal.jsonl> [--tag A1]

Writes build/decomp_reports/<tag>/{summary.txt,names.csv,nonmatching.csv,notes.md}
and appends name suggestions to reference/name_suggestions.csv (deduplicated).
Prints a short summary.
"""
import argparse
import csv
import json
import os

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("path")
    ap.add_argument("--tag", default="batch")
    a = ap.parse_args()
    jp = a.path if a.path.endswith(".jsonl") else os.path.join(a.path, "journal.jsonl")
    results = []
    for line in open(jp, encoding="utf-8"):
        j = json.loads(line)
        if j.get("type") != "result":
            continue
        r = j.get("result")
        if isinstance(r, str):
            try:
                r = json.loads(r)
            except ValueError:
                continue
        if isinstance(r, dict) and "matched" in r:
            results.append(r)
    # resumed runs append a fresh result line per re-run unit: keep the last per file
    last = {}
    for r in results:
        last[os.path.basename(r.get("file", "")) or id(r)] = r
    results = list(last.values())
    out = os.path.join(ROOT, "build", "decomp_reports", a.tag)
    os.makedirs(out, exist_ok=True)
    n_m = sum(len(r["matched"]) for r in results)
    n_nm = sum(len(r.get("nonmatching", [])) for r in results)
    n_sk = sum(len(r.get("skipped", [])) for r in results)
    with open(os.path.join(out, "nonmatching.csv"), "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["file", "function", "kind", "reason"])
        for r in results:
            fn = os.path.basename(r.get("file", ""))
            for x in r.get("nonmatching", []):
                w.writerow([fn, x["name"], "nonmatching", x["reason"]])
            for x in r.get("skipped", []):
                w.writerow([fn, x["name"], "skipped", x["reason"]])
    names = []
    for r in results:
        for s in r.get("suggested_names", []):
            names.append((s["func"], s["name"], s["evidence"], os.path.basename(r.get("file", ""))))
    with open(os.path.join(out, "names.csv"), "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["func", "name", "evidence", "file"])
        w.writerows(names)
    ref = os.path.join(ROOT, "reference", "name_suggestions.csv")
    seen = set()
    if os.path.exists(ref):
        with open(ref, encoding="utf-8") as f:
            for row in csv.DictReader(f):
                seen.add((row["func"], row["name"]))
    new_rows = []
    for n in names:
        if (n[0], n[1]) not in seen:
            seen.add((n[0], n[1]))
            new_rows.append(n)
    exists = os.path.exists(ref)
    with open(ref, "a", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        if not exists:
            w.writerow(["func", "name", "evidence", "file", "batch"])
        for n in new_rows:
            w.writerow(list(n) + [a.tag])
    with open(os.path.join(out, "notes.md"), "w", encoding="utf-8") as f:
        for r in results:
            f.write(f"## {os.path.basename(r.get('file', ''))}\n\n")
            f.write(f"matched {len(r['matched'])}; nonmatching {[x['name'] for x in r.get('nonmatching', [])]}\n\n")
            if r.get("type_notes"):
                f.write("**types:** " + r["type_notes"].strip() + "\n\n")
            if r.get("process_notes"):
                f.write("**process:** " + r["process_notes"].strip() + "\n\n")
    summ = (f"{a.tag}: {len(results)} unit results, {n_m} matched, {n_nm} nonmatching, "
            f"{n_sk} skipped, {len(names)} name suggestions ({len(new_rows)} new)")
    open(os.path.join(out, "summary.txt"), "w").write(summ + "\n")
    print(summ)


if __name__ == "__main__":
    main()
