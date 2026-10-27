#!/usr/bin/env python3
"""Generate libsa1/src/rt/rt_fsca_q.c: the FSCA quarter-wave table of the harness.

    py -3.12 harness/tools/dump_fsca.py             # (re)write libsa1/src/rt/rt_fsca_q.c
    py -3.12 harness/tools/dump_fsca.py --check     # fresh dump == the checked-in file? (exit 1 if not)
    py -3.12 harness/tools/dump_fsca.py --out F.c   # write somewhere else

The SH-4 FSCA instruction reads a hardware table; the interpreter models it
with fsca_build() (harness/src/sh4_fsca_ref.c): sin(i * 2pi/65536) computed
with the host C library's sin() in round-to-nearest, rounded to float, exact
0 and 1 at the quadrant points, the other quadrants by symmetry.  The
reference traces were recorded with that table, and its values depend on the
C library, so the harness and libsa1 both use this dump instead of computing
it (docs/libsa1_integration.md 8.2).  The output is generated mathematics,
not game data.

How: sh4_fsca_ref.c is compiled stand-alone (-DSH4_FSCA_REF_MAIN) for the
harness's main target (x86-windows-gnu, the target that recorded the
references) with the harness's own compiler flags (harness/build.py), and run.
The harness itself checks the loaded table against its own fsca_build() in
"sa1h selftest" (check "FSCA table").
"""
import argparse
import hashlib
import os
import re
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
HARNESS = os.path.dirname(HERE)
sys.path.insert(0, HARNESS)
import build as hbuild      # noqa: E402  (harness/build.py: flags, paths)

N = 0x4001
TARGET = "win32"


def dump():
    outdir = os.path.join(HARNESS, "build", "fsca_dump")
    os.makedirs(outdir, exist_ok=True)
    exe = os.path.join(outdir, "dump_fsca.exe")
    src = os.path.join(hbuild.SRC, "sh4_fsca_ref.c")
    cmd = hbuild.ZIG + hbuild.harness_cflags(TARGET) + ["-DSH4_FSCA_REF_MAIN", "-o", exe, src]
    rc, out = hbuild.run(cmd)
    if rc:
        sys.exit("dump_fsca: compile failed:\n" + out)
    r = subprocess.run([exe], capture_output=True, text=True)
    if r.returncode:
        sys.exit("dump_fsca: %s failed: %s" % (exe, r.stderr))
    words = [int(x, 16) for x in r.stdout.split()]
    if len(words) != N:
        sys.exit("dump_fsca: expected %d words, got %d" % (N, len(words)))
    # sanity: exact 0 and 1 at the ends, monotonic quarter wave, all below 1.0
    assert words[0] == 0 and words[-1] == 0x3F800000, "quadrant points"
    assert all(words[i] <= words[i + 1] for i in range(N - 1)), "not monotonic"
    return words


def sha1(words):
    return hashlib.sha1(struct.pack("<%dI" % len(words), *words)).hexdigest()


def render(words):
    h = sha1(words)
    lines = [
        "/*",
        " * rt_fsca_q.c - FSCA quarter-wave table.  GENERATED, do not edit:",
        " *     py -3.12 SADC/harness/tools/dump_fsca.py",
        " *",
        " * sh4_fsca_q[i] = float bits of sin(i * 2pi/65536), i = 0 .. 0x4000 (16,385",
        " * entries), exactly as the SH-4 interpreter harness computes its FSCA model",
        " * (fsca_build() in SADC/harness/src/sh4_fsca_ref.c: host sin() in",
        " * round-to-nearest, rounded to float, exact 0 and 1 at the quadrant points).",
        " * The other quadrants follow by symmetry (sh4_fsca() in",
        " * SADC/include/pc/sh4_fpmodel.h).  The reference traces were recorded with",
        " * this table (docs/libsa1_integration.md 8.2, decision D6).  Generated",
        " * mathematics, not game data.",
        " *",
        " * SHA-1 of the 16,385 entries as little-endian 32-bit words (65,540 bytes):",
        " *     %s" % h,
        " * Checks: dump_fsca.py --check; \"sa1h selftest\" (FSCA table == fsca_build()).",
        " */",
        "#include \"pc/sh4_fpmodel.h\"",
        "",
        "const uint32_t sh4_fsca_q[0x4001] = {",
    ]
    for i in range(0, N, 8):
        chunk = words[i:i + 8]
        lines.append("    " + " ".join("0x%08Xu," % w for w in chunk) + "  /* %04X */" % i)
    lines.append("};")
    return "\n".join(lines) + "\n", h


def parse_file(path):
    text = open(path, encoding="utf-8").read()
    body = text[text.index("sh4_fsca_q[0x4001] = {"):]
    body = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
    words = [int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{8})u", body)]
    m = re.search(r"\(65,540 bytes\):\s*\*\s*([0-9a-f]{40})", text)
    return words, (m.group(1) if m else None)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--out", default=hbuild.FSCA_TABLE)
    ap.add_argument("--check", action="store_true", help="compare the existing file with a fresh dump")
    a = ap.parse_args()
    words = dump()
    text, h = render(words)
    if a.check:
        if not os.path.exists(a.out):
            print("dump_fsca: %s does not exist" % a.out)
            return 1
        old, old_h = parse_file(a.out)
        bad = [i for i in range(min(len(old), N)) if old[i] != words[i]]
        if len(old) != N or bad or old_h != h or sha1(old) != h:
            print("dump_fsca: %s DIFFERS from the harness's fsca_build() (%d entries, %d differ, sha %s vs %s)"
                  % (a.out, len(old), len(bad), old_h, h))
            return 1
        print("dump_fsca: %s ok (16,385 entries, sha1 %s)" % (a.out, h))
        return 0
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    with open(a.out, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print("dump_fsca: wrote %s (16,385 entries, sha1 %s)" % (a.out, h))
    return 0


if __name__ == "__main__":
    sys.exit(main())
