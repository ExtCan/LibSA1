#!/usr/bin/env python3
"""talk_pages.py - test-time reference 1 of harness/tests/sim/talkref (G-SA1 1.0.0, prep lane p-refs-T;
sadc docs/research/gsa1_100_npc.md 9, 15.2 item 4, test T1; gsa1_100_master.md R0 "Later prep", R5b).

Every page of every talk file on the owner's disc (`disc/SONICADV/*_MES_*.BIN`: Station Square, Mystic Ruins, the Past,
the five languages J E F S G), typeset by the ORIGINAL code of the owner's `baserom/1ST_READ.BIN` on the harness's SH-4
interpreter: per page the game's 8C055794 (NH_MSG_Open 70, 384, 500, 52, texture 100, buffer B_8C87E900: the first-page
path of the talk runner's 8C05F70A) and 8C054F2E (the text routine), with B_8C754BFC (the language: the font table row)
set to the file's language. R5b's `text_test` (LIB) renders the same page bytes with `sa1_text_render` and compares its
512 x 64 buffer with this one byte for byte, through the buffer's SHA-1.

What it writes (`--out`, default build/traces/talkref/pages.json; only under the project's build/ folder, which
repos/sync_repos.py never enters): numbers, names and SHA-1 only. Per page: the file, its language row, entry, group and
page index, the page's offset and length in the file and the SHA-1 of its bytes (the input), the first byte (the game's
alignment: 0x07 block centring, 0x09 each line centred, else none), its line breaks; the result: the fault status, the
instruction count, the cursor the text routine left, the SHA-1 of the 0x10000-byte buffer, its set pixels, rows and
columns used, the pixel values, and whether anything was written in the 0x4000 bytes past the buffer (a third line does:
sadc docs/research/v1_npcs.md review R2.4). No glyph, no picture and no byte of text is written to any file: each buffer
travels from the interpreter to this script through a pipe and is hashed in memory.

The interpreter is the harness's own (harness/src), linked at test time with `talkref_drv.c` (this folder) in place of
main.c, in a private build folder (`--work`, default build/scratch/talkref/drv). It needs Python 3.12 with ziglang and
the project's generated build/1ST_READ.symbols.txt and build/1ST_READ.xrefs.json (configure.py --split), as the harness.

    py -3.12 harness/tests/sim/talkref/talk_pages.py                 # every page, every language
    py -3.12 harness/tests/sim/talkref/talk_pages.py --files SS_MES_S_E.BIN SS_MES_S_J.BIN
    options: --disc DIR (default disc/SONICADV), --out FILE, --work DIR, --budget N (instructions per page, 5,000,000),
             --recheck N (typeset the first N pages again and compare: the run is deterministic; default 64)
"""
import argparse
import array
import glob
import hashlib
import importlib.util
import json
import os
import struct
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))


def find_root():
    """the sadc folder: SADC_ROOT, else the first parent of this folder holding configure.py and harness/"""
    env = os.environ.get("SADC_ROOT")
    if env:
        return os.path.abspath(env)
    d = HERE
    while True:
        if os.path.isfile(os.path.join(d, "configure.py")) and os.path.isdir(os.path.join(d, "harness", "src")):
            return d
        up = os.path.dirname(d)
        if up == d:
            sys.exit("talk_pages: cannot find the sadc folder (set SADC_ROOT)")
        d = up


ROOT = find_root()
HARNESS = os.path.join(ROOT, "harness")
DRIVER = os.path.join(HERE, "talkref_drv.c")
CORE = ["sh4_cpu.c", "sh4_mem.c", "sh4_call.c", "sh4_sym.c", "sh4_intrin.c", "sh4_fsca_ref.c", "hle.c", "pc_runtime.c"]
GEN = ["overlays.c", "functions.c", "native_abs.c", "native_thunks.c", "native_registry.c"]

LANG_ROW = {"J": 0, "E": 1, "F": 2, "S": 3, "G": 4}   # B_8C754BFC, the font table 8C15497C's rows (READ v1_npcs.md 1.4, 1.7)
MSGC = 0x8C78C518            # B_8C78C518: the talk runner's window structure (READ asm 8C05F902 -> 8C05F70A)
BUF = 0x10000                # 512 x 64 x 2 bytes (NH_MSG_Open's texture size, READ asm 8C055B80)
OVER = 0x4000                # the bytes past the buffer a third line can reach (v1_npcs.md review R2.4)
PAYLOAD = 8 + 0x3C + 4 + BUF + OVER


def load_address(name):
    """where the game's loaders put a talk file: Station Square 0CB46000, Mystic Ruins and the Past 0CB4A000 (READ
    v1_npcs.md 1.4: 0C90F2C0, 0C911D60, 0C902F00); each file's table pointer is checked against it"""
    return 0x0CB46000 if name.upper().startswith("SS_") else 0x0CB4A000


def walk(b, base):
    """(entry, group, page, pointer) of every page of a talk file, in the game's format (READ v1_npcs.md 1.4:
    +0 count, +4 table of {s16 *condition; char **messages}; the groups one after another, each a NULL-ended list;
    as many groups as the condition has -2 opcodes, plus one), and the file's oddities"""
    notes = []
    n, tbl = struct.unpack_from("<iI", b, 0)
    out = []
    if not (base <= tbl < base + len(b)):
        notes.append("table pointer %08X outside the file" % tbl)
        return out, notes, n
    for i in range(n):
        off = tbl - base + 8 * i
        if off + 8 > len(b):
            notes.append("entry %d: past the end of the file" % i)
            break
        cp, mp = struct.unpack_from("<II", b, off)
        ng = 1
        if cp:
            ops, o = [], cp - base
            while 0 <= o and o + 2 <= len(b) and len(ops) <= 400:
                v = struct.unpack_from("<h", b, o)[0]
                ops.append(v)
                o += 2
                if v == -1:
                    break
            ng = ops.count(-2) + 1
        if not mp:
            continue
        o = mp - base
        ended = False
        for g in range(ng):
            k = 0
            while True:
                if o < 0 or o + 4 > len(b):
                    notes.append("entry %d group %d: the page list runs to the end of the file (the next word read as 0, "
                                 "as a clean load buffer holds; UNVERIFIED in the game)" % (i, g))
                    ended = True
                    break
                p = struct.unpack_from("<I", b, o)[0]
                o += 4
                if p == 0:
                    break
                if not (base <= p < base + len(b)):
                    notes.append("entry %d group %d page %d: pointer %08X outside the file (not typeset)" % (i, g, k, p))
                else:
                    out.append((i, g, k, p))
                k += 1
            if ended:
                break
    return out, notes, n


def load_hb():
    spec = importlib.util.spec_from_file_location("sa1h_harness_build", os.path.join(HARNESS, "build.py"))
    hb = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(hb)
    return hb


def build_driver(work):
    """the harness's interpreter core + talkref_drv.c -> WORK/talkref_drv.exe (32-bit Windows, as the harness)"""
    hb = load_hb()
    gen, objd = os.path.join(work, "gen"), os.path.join(work, "obj")
    os.makedirs(gen, exist_ok=True)
    os.makedirs(objd, exist_ok=True)
    for cmd in ([sys.executable, os.path.join(HARNESS, "tools", "gen_optab.py"), os.path.join(gen, "sh4_optab.h")],
                [sys.executable, os.path.join(HARNESS, "tools", "gen_native.py"), "--out", gen]):
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode:
            sys.exit("talk_pages: %s failed:\n%s" % (os.path.basename(cmd[1]), (r.stdout + r.stderr)[-2000:]))
    flags = hb.harness_cflags("win32", [gen])
    jobs = [(os.path.join(hb.SRC, s), os.path.join(objd, s[:-2] + ".o"), flags, True) for s in CORE]
    jobs.append((hb.FSCA_TABLE, os.path.join(objd, "rt_fsca_q.o"), flags, True))
    jobs += [(os.path.join(gen, s), os.path.join(objd, s[:-2] + ".o"), flags) for s in GEN]
    jobs.append((DRIVER, os.path.join(objd, "talkref_drv.o"), flags, True))
    for j in jobs:
        src, rc, out = hb.compile_one(j)
        if rc:
            sys.exit("talk_pages: compile failed: %s\n%s" % (src, out[-3000:]))
    exe = os.path.join(work, "talkref_drv.exe")
    r = subprocess.run(hb.ZIG + ["-target", hb.TARGETS["win32"], "-g", "-o", exe] + [j[1] for j in jobs],
                       capture_output=True, text=True)
    if r.returncode:
        sys.exit("talk_pages: link failed:\n%s" % (r.stdout + r.stderr)[-3000:])
    hb.set_laa(exe)
    return exe


class Driver:
    def __init__(self, exe):
        self.p = subprocess.Popen([exe], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    def cmd(self, line):
        self.p.stdin.write((line + "\n").encode("utf-8"))
        self.p.stdin.flush()
        h = self.p.stdout.read(8)
        if len(h) != 8:
            raise RuntimeError("talkref_drv stopped: " + self.p.stderr.read().decode("utf-8", "replace")[-2000:])
        st, n = struct.unpack("<iI", h)
        data = self.p.stdout.read(n) if n else b""
        if len(data) != n:
            raise RuntimeError("talkref_drv: short answer")
        return st, data

    def close(self):
        try:
            self.p.stdin.write(b"Q\n")
            self.p.stdin.close()
        except OSError:
            pass
        self.p.wait(timeout=60)


def measure(data):
    """the numbers of one answer (nothing of the picture itself)"""
    ic = struct.unpack_from("<Q", data, 0)[0]
    m = data[8:8 + 0x3C]
    buf = data[8 + 0x3C + 4: 8 + 0x3C + 4 + BUF]
    over = data[8 + 0x3C + 4 + BUF:]
    px = array.array("H")
    px.frombytes(buf)
    set_px = len(px) - px.count(0)
    rows = []
    x0, x1 = 1 << 30, -1
    for y in range(64):
        rb = buf[y * 1024:(y + 1) * 1024]
        t = rb.rstrip(b"\0")
        if t:
            rows.append(y)
            lead = len(rb) - len(rb.lstrip(b"\0"))
            x0 = min(x0, lead // 2)
            x1 = max(x1, (len(t) - 1) // 2)
    over_n = len(over) - over.count(0)
    r = {"icount": ic, "cursor": list(struct.unpack_from("<hh", m, 0x0E)),
         "buffer_sha1": hashlib.sha1(buf).hexdigest(), "set_pixels": set_px,
         "rows": [rows[0], rows[-1]] if rows else None, "cols": [x0, x1] if rows else None,
         "values": sorted("%04X" % v for v in set(px) if v), "past_buffer_bytes": over_n}
    if over_n:
        r["past_buffer_sha1"] = hashlib.sha1(over).hexdigest()
    return r, m


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--disc", default=os.path.join(ROOT, "disc", "SONICADV"))
    ap.add_argument("--rom", default=os.path.join(ROOT, "baserom", "1ST_READ.BIN"))
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "traces", "talkref", "pages.json"))
    ap.add_argument("--work", default=os.path.join(ROOT, "build", "scratch", "talkref", "drv"))
    ap.add_argument("--files", nargs="*", help="talk file names (default: every *_MES_*.BIN of the disc folder)")
    ap.add_argument("--budget", type=int, default=5000000)
    ap.add_argument("--recheck", type=int, default=64)
    a = ap.parse_args()
    out = os.path.abspath(a.out)
    build = os.path.join(ROOT, "build") + os.sep
    if not out.startswith(build):
        sys.exit("talk_pages: --out must lie under %s (a folder repos/sync_repos.py never enters)" % build)
    t0 = time.time()
    exe = build_driver(os.path.abspath(a.work))
    names = a.files or sorted(os.path.basename(f) for f in glob.glob(os.path.join(a.disc, "*_MES_*.BIN")))
    if not names:
        sys.exit("talk_pages: no talk files in %s" % a.disc)
    rom = open(a.rom, "rb").read()
    drv = Driver(exe)
    st, _ = drv.cmd("R " + os.path.abspath(a.rom))
    if st != len(rom):
        sys.exit("talk_pages: the driver could not load %s" % a.rom)
    files, pages, msgc0, sample = {}, [], None, []
    for name in names:
        path = os.path.join(a.disc, name)
        b = open(path, "rb").read()
        stem = os.path.splitext(name)[0].upper()
        row = LANG_ROW.get(stem[-1])
        base = load_address(name)
        lst, notes, count = walk(b, base) if row is not None else ([], ["not a talk file name"], 0)
        files[name] = {"bytes": len(b), "sha1": hashlib.sha1(b).hexdigest(), "row": row, "load": "%08X" % base,
                       "entries": count, "pages": len(lst), "notes": notes}
        if not lst:
            continue
        st, _ = drv.cmd("F %08X %s" % (base, os.path.abspath(path)))
        if st != len(b):
            sys.exit("talk_pages: the driver could not load %s" % path)
        for (e, g, k, p) in lst:
            o = p - base
            end = b.find(b"\0", o)
            end = len(b) if end < 0 else end
            s = b[o:end]
            first = s[0] if s else 0
            st, data = drv.cmd("T %d %08X %08X %d" % (row, MSGC, p, a.budget))
            res, m = measure(data)
            if msgc0 is None:
                msgc0 = m
            rec = {"file": name, "row": row, "entry": e, "group": g, "page": k, "ptr": "%08X" % p, "off": o,
                   "len": len(s), "input_sha1": hashlib.sha1(s).hexdigest(), "first": first,
                   "align": first if first in (7, 9) else 0, "breaks": s.count(b"\n"), "status": st}
            inner = sum(1 for x in s[1:] if x < 0x20 and x != 10)
            if inner:
                rec["inner_controls"] = inner      # control bytes inside the page besides the line breaks (a second 0x07)
            rec.update(res)
            if m[0:0x0E] != msgc0[0:0x0E]:
                rec["window_fields"] = m[0:0x0E].hex()
            pages.append(rec)
            if len(sample) < a.recheck:
                sample.append((rec, base, path))
    # the run is deterministic: typeset the first pages again, each from a fresh load of its file
    recheck_bad = []
    for rec, base, path in sample:
        drv.cmd("F %08X %s" % (base, os.path.abspath(path)))
        st, data = drv.cmd("T %d %08X %s %d" % (rec["row"], MSGC, rec["ptr"], a.budget))
        res, _ = measure(data)
        if res["buffer_sha1"] != rec["buffer_sha1"] or st != rec["status"]:
            recheck_bad.append("%s %d/%d/%d" % (rec["file"], rec["entry"], rec["group"], rec["page"]))
    drv.close()
    by_row = {}
    for r in pages:
        k = "JEFSG"[r["row"]]
        by_row[k] = by_row.get(k, 0) + 1
    summary = {"files": len(files), "pages": len(pages), "pages_by_language": by_row,
               "faults": sum(1 for r in pages if r["status"]), "past_buffer": sum(1 for r in pages if r["past_buffer_bytes"]),
               "empty": sum(1 for r in pages if not r["set_pixels"]),
               "third_line": sum(1 for r in pages if r["rows"] and r["rows"][1] > 51),
               "beyond_500px": sum(1 for r in pages if r["cols"] and r["cols"][1] >= 500),
               "inner_controls": sum(1 for r in pages if r.get("inner_controls")),
               "not_07": sum(1 for r in pages if r["first"] != 7),
               "widest_col": max((r["cols"][1] for r in pages if r["cols"]), default=None),
               "lowest_row": max((r["rows"][1] for r in pages if r["rows"]), default=None),
               "distinct_buffers": len({r["buffer_sha1"] for r in pages}),
               "recheck": {"pages": len(sample), "different": recheck_bad}, "seconds": round(time.time() - t0, 1)}
    doc = {"what": "talk pages typeset by the original 8C055794 + 8C054F2E on the SH-4 harness (talk_pages.py); "
                   "numbers, names and SHA-1 only",
           "version": 1, "rom_sha1": hashlib.sha1(rom).hexdigest(), "msgc": "%08X" % MSGC,
           "window": {"kind": msgc0[0] if msgc0 else None,
                      "x_y_w_h": list(struct.unpack_from("<hhhh", msgc0, 2)) if msgc0 else None,
                      "tex_w_h": list(struct.unpack_from("<HH", msgc0, 0x0A)) if msgc0 else None,
                      "spacing": list(struct.unpack_from("<hh", msgc0, 0x12)) if msgc0 else None,
                      "glyph_colour": "%04X" % struct.unpack_from("<H", msgc0, 0x16)[0] if msgc0 else None,
                      "buffer": "%08X" % struct.unpack_from("<I", msgc0, 0x20)[0] if msgc0 else None},
           "summary": summary, "files": files, "pages": pages}
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        json.dump(doc, f, indent=None, separators=(",", ":"))
        f.write("\n")
    print("talk_pages: %d files, %d pages %s, %d faults, %d past the buffer, %d distinct buffers, recheck %d/%d same, "
          "%.1f s -> %s" % (len(files), len(pages), by_row, summary["faults"], summary["past_buffer"],
                            summary["distinct_buffers"], len(sample) - len(recheck_bad), len(sample), summary["seconds"],
                            out))
    return 1 if summary["faults"] or recheck_bad else 0


if __name__ == "__main__":
    sys.exit(main())