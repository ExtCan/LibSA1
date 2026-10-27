#!/usr/bin/env python3
"""Generate config/overlays/<NAME>.yaml split configs from baserom/overlays.json.

    py -3.12 tools/gen_overlay_configs.py

Code regions found by extract_overlays.py are per object file; regions
separated only by linker fill (zero bytes / NOP padding, up to 0x40 bytes)
are merged so each overlay has few contiguous code ranges.
"""
import json
import os

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
PAD = {0x0000, 0x0009, 0x0900}


def main():
    ov = json.load(open(os.path.join(ROOT, "baserom", "overlays.json")))
    out_dir = os.path.join(ROOT, "config", "overlays")
    os.makedirs(out_dir, exist_ok=True)
    names = []
    for e in ov:
        if not e.get("has_code"):
            continue
        name = e["name"]
        data = open(os.path.join(ROOT, e["bin_path"]), "rb").read()
        base = int(e["load_addr"], 16)
        regs = sorted((int(a, 16), int(b, 16)) for a, b in e["code_regions"])
        merged = []
        for a, b in regs:
            if merged:
                pa, pb = merged[-1]
                gap = data[pb:a]
                words = [int.from_bytes(gap[i:i + 2], "little") for i in range(0, len(gap) - 1, 2)]
                if a - pb <= 0x40 and all(w in PAD for w in words):
                    merged[-1] = (pa, max(pb, b))
                    continue
            merged.append((a, b))
        # code ranges are 4-aligned at both ends where possible
        ranges = []
        for a, b in merged:
            a &= ~3
            b = min((b + 3) & ~3, len(data))
            ranges.append((base + a, base + b))
        sym = f"config/symbols.{name.lower()}.txt"
        if not os.path.exists(os.path.join(ROOT, sym)):
            ref = os.path.join(ROOT, "reference", "apply", f"symbols.{name}.txt")
            with open(os.path.join(ROOT, sym), "w", newline="\n") as f:
                f.write(f"// Names for {name}.BIN. Seeded from reference/apply.\n")
                if os.path.exists(ref):
                    for line in open(ref, encoding="utf-8"):
                        if "=" in line.split("//")[0]:
                            f.write(line)
        lines = [
            f"# {name} overlay ({e['prs_path']}), load address {e['load_addr']}",
            f"name: {name}",
            f"target: {e['bin_path']}",
            f"sha1: {e['sha1_bin']}",
            f"base: 0x{base:08X}",
            "code_ranges:",
        ]
        lines += [f"  - [0x{a:08X}, 0x{b:08X}]" for a, b in ranges]
        lines += [
            f"seeds: [0x{base + 0x20:08X}]" if name.startswith("EV") else "seeds: []",
            f"symbol_files: [{sym}]",
            "link_with: [1ST_READ]",
            f"asm_dir: asm/overlays/{name.lower()}",
            "data_chunk: 0x8000",
            f"prs: {e['prs_path']}",
            f"prs_sha1: {e['sha1_prs']}",
        ]
        with open(os.path.join(out_dir, f"{name.lower()}.yaml"), "w", newline="\n") as f:
            f.write("\n".join(lines) + "\n")
        names.append(name)
    print(f"wrote {len(names)} overlay configs")


if __name__ == "__main__":
    main()
