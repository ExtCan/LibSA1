"""Assemble every decodable SH-4 encoding with asmsh and verify byte equality."""
import os, struct, subprocess, sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from sh4.isa import decode, format_insn

SHC = os.environ.get("SHC_BIN")
work = sys.argv[1]
os.makedirs(work, exist_ok=True)
BASE = 0x10000  # code starts after a reserved pad so backward branches stay in-section
lines = ["          .SECTION    P,CODE,ALIGN=4", "          .RES.B      65536"]
expect = []
addr = BASE
valid = 0
for w in range(0x10000):
    ins = decode(w, addr)
    if ins is None:
        continue
    valid += 1
    lines.append("          " + format_insn(ins))
    lines.append("          NOP")
    expect.append((addr, w))
    addr += 4
lines.append("          .RES.B      65536")
lines.append("          .END")
src = os.path.join(work, "rt.src")
open(src, "w").write("\n".join(lines) + "\n")
env = dict(os.environ, SHC_LIB=SHC, SHC_TMP=work)
r = subprocess.run([os.path.join(SHC, "asmsh.exe"), src, "-cpu=sh4", "-endian=little",
                    "-object=" + os.path.join(work, "rt.obj"), "-list=" + os.path.join(work, "rt.lis")],
                   env=env, capture_output=True, text=True, cwd=work)
errs = [l for l in r.stdout.splitlines() if "(E)" in l or "(W)" in l]
print("valid encodings:", valid, "asm messages:", len(errs))
for e in errs[:30]:
    print("  ", e)
# Parse listing for emitted words: "  LINE ADDR  CODE  ..."
got = {}
for l in open(os.path.join(work, "rt.lis"), errors="replace"):
    p = l.split()
    if len(p) >= 3 and p[0].isdigit() and len(p[1]) == 8 and len(p[2]) == 4:
        try:
            got[int(p[1], 16)] = int(p[2], 16)
        except ValueError:
            pass
bad = [(a, w, got.get(a)) for a, w in expect if got.get(a) != w]
print("mismatches:", len(bad))
for a, w, g in bad[:40]:
    print(f"  {a:08X} want {w:04X} got {g if g is None else format(g,'04X')}  {format_insn(decode(w, a))}")
