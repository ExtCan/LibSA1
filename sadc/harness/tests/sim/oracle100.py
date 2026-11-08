#!/usr/bin/env python3
"""G-SA1 1.0.0 test-time prototype oracles (round R0, lane p-refs; docs/research/gsa1_100_plan.md 6.2 and 4.8,
gsa1_100_shards.md 12.1 E2 / E7).

Three host behaviours have no strict reference, since the original game never does them: Amy and Big opening an Egg
Capsule by walking into it (plan 4.8, `SA1_COPT_CAPSULE_TOUCH`), a hunt of N shards (N != 3) and a hunter other than
Knuckles. For each, this script makes a PRIVATE copy of 1ST_READ.BIN at test time from the owner's disc image
(SADC/baserom/1ST_READ.BIN) in a scratch folder with only numbers changed (the words listed below), runs the stock
harness on it (`sa1h trace --root PRIVATE`), and writes the numbers a libsa1 world must reproduce: press, find, clear and
pose steps, the poses, tones, the radar's draw places. The copy, the traces and the private disc folder are deleted at the
end (--keep keeps them); the JSON holds numbers only. Nothing of it is ever committed: the outputs live under build/.

    py -3.12 harness/tests/sim/oracle100.py touch                # Amy and Big at a capsule, the touch zone of plan 4.8
    py -3.12 harness/tests/sim/oracle100.py hunt-n 1 2 5 12       # the game's hunt with N shards (E2)
    py -3.12 harness/tests/sim/oracle100.py hunters 0 2 5 6 7 3   # another character hunts (E7)
    options: --exe EXE (default harness/build/sa1h.exe), --out DIR (default build/traces/oracle100), --keep

Scratch: build/scratch/oracle100/<command>/ on the SADC drive (a private root of 1ST_READ.BIN and the symbol file; for
the hunts also a private disc folder: the files the run opens, copied, and the private SET file made from numbers).
"""
import argparse
import csv
import json
import math
import os
import re
import shutil
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.environ.get("SADC_ROOT") or os.path.dirname(os.path.dirname(os.path.dirname(HERE)))   # as the harness
BASE = 0x8C010000
DISC = os.path.join(ROOT, "disc", "SONICADV")

# the capsule's press zone (plan 4.8, READ Ghidra 8C062930 / 8C062FB4; prep100 plan100/set/notes.txt): r^2, low, span
W930 = {0x8C062AC0: 144.0, 0x8C062AC4: 27.0, 0x8C062AC8: 29.6}
WFB4 = {0x8C0630CC: 144.0, 0x8C0630D0: 27.0, 0x8C0630D4: 57.0}
TOUCH = {5: (23.0, -10.0), 7: (29.0, -10.0)}     # Amy r 23, Big r 29, low -10, the top kept at 56.6 and 57 (plan 4.8)
# the hunt (prep100/emerald/notes.txt; design100/shards e2.py, e7.py)
ROW0 = 0x8C15C100                                   # D_8C15C100 row 0 {s8 a, b, c, d; u16 stage}
COUNT = [0x8C0C9486, 0x8C0C96CA, 0x8C0C9762, 0x8C0C9B12, 0x8C0C9B20, 0x8C0C9BA8, 0x8C0C9C12, 0x8C0C9CD4, 0x8C0C9EB0,
         0x8C0C9F18]                               # the 3s of the shard count
LIT_E8 = [0x8C0C9128, 0x8C0C9864, 0x8C0C9A9C, 0x8C0C9C50, 0x8C0C9DE8, 0x8C0C9FC8]   # pointers to B_8C7A26E8
LIT_2710 = [0x8C0C9C5C]                             # the pointer to record 1, B_8C7A2710
REC = 0x8C053520                                    # padding the records move to for N > 3
CHR = [0x8C04E568, 0x8C0C907C, 0x8C0C96B0, 0x8C0C9B02, 0x8C0C9BEE, 0x8C0C9CB2, 0x8C0C9EF4, 0x8C05B808, 0x8C05B8DC]
RADAR = "8C0C99E4"                                  # the radar task, made by --create-task for a hunter other than Knuckles
EME_P = 7                                           # O EME P's index in Emerald Coast's object list (c1_records.json)
LETTER = {0: "S", 2: "M", 3: "K", 5: "A", 6: "E", 7: "B"}
SDK_LIST = ["8C0509DA PadOff ?", "8C0940DA SE ?", "8C04E386 NextState ?", "8C04EFE0 clear ?", "8C0C9C9A collect ?",
            "8C0C84FC tone ?", "8C107BA0 voice ?"]


class Work:
    def __init__(self, a, name):
        self.exe = os.path.abspath(a.exe)
        self.dir = os.path.join(ROOT, "build", "scratch", "oracle100", name)
        self.root = os.path.join(self.dir, "root")
        self.keep = a.keep
        os.makedirs(os.path.join(self.root, "baserom"), exist_ok=True)
        os.makedirs(os.path.join(self.root, "build"), exist_ok=True)
        shutil.copyfile(os.path.join(ROOT, "build", "1ST_READ.symbols.txt"),
                        os.path.join(self.root, "build", "1ST_READ.symbols.txt"))
        self.lst = os.path.join(self.dir, "sdk.list")
        open(self.lst, "w").write("\n".join(SDK_LIST) + "\n")
        self.disc = DISC

    def rom(self, edit=None):
        """the private 1ST_READ.BIN: the owner's copy with edit(bytearray) applied"""
        d = bytearray(open(os.path.join(ROOT, "baserom", "1ST_READ.BIN"), "rb").read())
        if edit:
            edit(d)
        open(os.path.join(self.root, "baserom", "1ST_READ.BIN"), "wb").write(d)

    def private_disc(self, level, act, chr_, sets):
        """a disc folder holding the files a run of this stage and character opens (found by a -v run), plus the private
        SET files {name: bytes}"""
        dd = os.path.join(self.dir, "disc")
        os.makedirs(dd, exist_ok=True)
        r = subprocess.run([self.exe, "--root", ROOT, "trace", "-v", "--level", str(level), "--act", str(act), "--chr",
                            str(chr_), "--input", "2 0 0", "--print", "0", "--out", os.path.join(self.dir, "probe")],
                           capture_output=True, text=True, timeout=600)
        names = set(re.findall(r'gdFsOpen\("([^"]+)"\) -> \d+ bytes', r.stdout))
        names.add("S_MOT.PRS")
        for n in sorted(names):
            src = os.path.join(DISC, n)
            if os.path.isfile(src) and not os.path.exists(os.path.join(dd, n)):
                shutil.copyfile(src, os.path.join(dd, n))
        for n, b in sets.items():
            open(os.path.join(dd, n), "wb").write(b)
        self.disc = dd
        return len(names)

    def run(self, tag, level, act, chr_, inp, extra=(), objects=True):
        out = os.path.join(self.dir, tag)
        a = [self.exe, "--root", self.root, "trace", "--level", str(level), "--act", str(act), "--chr", str(chr_),
             "--disc", self.disc, "--input", inp, "--print", "0", "--out", out, "--log-sdk", out + ".sdk", "--sdk-list",
             self.lst, "--log-2d", out + ".2d"] + ([] if objects else ["--no-objects"]) + list(extra)
        r = subprocess.run(a, capture_output=True, text=True, timeout=900)
        rows = list(csv.DictReader(open(out + ".csv", newline=""))) if os.path.exists(out + ".csv") else []
        sdk = open(out + ".sdk", errors="replace").read().splitlines() if os.path.exists(out + ".sdk") else []
        d2 = open(out + ".2d", errors="replace").read().splitlines() if os.path.exists(out + ".2d") else []
        txt = r.stdout + r.stderr
        faults = [l[:120] for l in txt.splitlines() if re.search(r"fault|FAULT|fatal|illegal|unmapped|exception", l)][:3]
        return {"exit": r.returncode, "steps": len(rows), "faults": faults}, rows, sdk, d2

    def done(self):
        if not self.keep:
            shutil.rmtree(self.dir, ignore_errors=True)


def first(sdk, pat):
    for l in sdk:
        if re.search(pat, l):
            return int(l.split()[1])
    return None


def all_steps(sdk, pat):
    return [int(l.split()[1]) for l in sdk if re.search(pat, l)]


def poses(rows, frm, n=12):
    out, last = [], None
    for i, x in enumerate(rows):
        if i < frm:
            continue
        k = (int(x["mode"]), int(x["action"]))
        if k != last:
            out.append([i, k[0], k[1]])
            last = k
    return out[:n]


def setfile(recs):
    """a SET file from numbers: count, then 0x20-byte records {u16 index, rot x, y, z; pos[3]; scl[3]}"""
    b = bytearray(0x20 + 0x20 * len(recs))
    struct.pack_into("<I", b, 0, len(recs))
    for i, (idx, rx, ry, rz, p, s) in enumerate(recs):
        struct.pack_into("<HHHH3f3f", b, 0x20 + 0x20 * i, idx, rx, ry, rz, *p, *s)
    return bytes(b)


def fl(h):
    return struct.unpack(">f", bytes.fromhex(h))[0]


def radar(d2):
    """the radar's light slots (8C0C967A) and corner icons (8C0C9294) as drawn: place -> draws (harness/tools/draw2d.py
    columns: 'S', ..., exec at 6, the sprite's x / y at 21 / 22)"""
    slots, icons = {}, {}
    for l in d2:
        p = l.split()
        if len(p) < 30 or p[0] != "S":
            continue
        if p[6] in ("8C0C967A", "8C0C9294"):
            k = "%d,%d" % (round(fl(p[21])), round(fl(p[22])))
            t = slots if p[6] == "8C0C967A" else icons
            t[k] = t.get(k, 0) + 1
    return slots, icons


def cmd_touch(a):
    w = Work(a, "touch")
    res = {"what": "Amy and Big walking into an Egg Capsule (8C062640:2:3 at (-3, 3.4, -40)), level 1 act 0, --no-objects",
           "zone": "r^2 at 8C062AC0 / 8C0630CC, low at 8C062AC4 / 8C0630D0, span 56.6 - low at 8C062AC8; 8C0630D4 kept 57",
           "runs": []}
    walk = "30 0 0; 200 0 -127; 300 0 0"
    cap = (-3.0, 3.4, -40.0)
    for chr_, (r, low) in sorted(TOUCH.items()):
        for zone in ("stock", "touch"):
            def ed(d, r=r, low=low, zone=zone):
                for tbl in (W930, WFB4):
                    for ad, v in tbl.items():
                        assert abs(struct.unpack_from("<f", d, ad - BASE)[0] - v) < 1e-3, hex(ad)
                if zone == "touch":
                    struct.pack_into("<f", d, 0x8C062AC0 - BASE, r * r)
                    struct.pack_into("<f", d, 0x8C0630CC - BASE, r * r)
                    struct.pack_into("<f", d, 0x8C062AC4 - BASE, low)
                    struct.pack_into("<f", d, 0x8C062AC8 - BASE, 56.6 - low)
                    struct.pack_into("<f", d, 0x8C0630D0 - BASE, low)
            w.rom(ed)
            info, rows, sdk, _ = w.run("t%d_%s" % (chr_, zone), 1, 0, chr_, walk, objects=False,
                                       extra=["--create-task", "8C062640:2:3@%g,%g,%g+ocp+late" % cap])
            press = first(sdk, r"PadOff\(FF\)")
            clear = first(sdk, r"SE\(2DA")
            dmin = min((math.hypot(float(x["pos_x"]) - cap[0], float(x["pos_z"]) - cap[2]), i) for i, x in enumerate(rows))
            info.update({"chr": chr_, "zone": zone, "r": r if zone == "touch" else 12.0, "low": low if zone == "touch" else 27.0,
                         "press_step": press, "clear_step": clear, "closest": [round(dmin[0], 3), dmin[1]],
                         "poses": poses(rows, press if press is not None else 0)})
            if press is not None and clear is not None and clear + 300 < len(rows):
                x = rows[clear + 300]
                info["held_at_clear_plus_300"] = [int(x["mode"]), int(x["action"])]
            res["runs"].append(info)
            print("touch chr %d %-5s exit %d press %s clear %s poses %s" % (chr_, zone, info["exit"], press, clear,
                                                                            info["poses"][:4]), flush=True)
    w.done()
    return res


def k0_path(w, chr_, inp="30 0 0; 200 0 -127"):
    """a plain run's places (the stock ROM, no objects): the spots' positions"""
    w.rom()
    info, rows, _, _ = w.run("path%d" % chr_, 1, 0, chr_, inp, objects=False)
    return rows


def cmd_hunt_n(a):
    w = Work(a, "hunt_n")
    rows = k0_path(w, 3)

    def at(step, upy=5.0):
        r = rows[step]
        return (float(r["pos_x"]), float(r["pos_y"]) + upy, float(r["pos_z"]))
    extra_ids = [0x12, 0x13, 0x14, 0x15, 0x16, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D,
                 0x4E, 0x4F, 0x17, 0x18]
    res = {"what": "Knuckles' hunt with N shards at level 1 act 0 (E2): row 0 {2,0,2,2, 0x0100}, the count bytes 3 -> N, "
                   "the records moved to %08X, ids of shards 4.. preset there, spots on his line" % REC, "runs": []}
    for N in a.n:
        if not 1 <= N <= 24:
            print("N %d: 1..24 only" % N)
            continue
        extra = extra_ids[:max(0, N - 3)]

        def ed(d, N=N, extra=extra):
            d[ROW0 - BASE:ROW0 - BASE + 6] = bytes([2, 0, 2, 2, 0x00, 0x01])
            for ad in COUNT:
                assert d[ad - BASE] == 3, hex(ad)
                d[ad - BASE] = N
            for ad in LIT_E8:
                assert struct.unpack_from("<I", d, ad - BASE)[0] == 0x8C7A26E8
                struct.pack_into("<I", d, ad - BASE, REC)
            for ad in LIT_2710:
                assert struct.unpack_from("<I", d, ad - BASE)[0] == 0x8C7A2710
                struct.pack_into("<I", d, ad - BASE, REC + 40)
            for i, idv in enumerate(extra):
                struct.pack_into("<I", d, REC - BASE + (3 + i) * 40, idv)
        place = {0x10: 60, 0x11: 60, 0x30: 90, 0x31: 90, 0x40: 120, 0x41: 120} if N >= 3 else \
            ({0x10: 60, 0x11: 60} if N == 1 else {0x10: 60, 0x11: 60, 0x30: 90, 0x31: 90})
        st = 135
        for i in extra:
            place[i] = st
            st += 8
        recs = [(EME_P, 0, 0, i, at(s), (0, 0, 0)) for i, s in place.items()]
        if w.disc == DISC:
            w.private_disc(1, 0, 3, {})
        open(os.path.join(w.disc, "SET0100K.BIN"), "wb").write(setfile(recs))
        w.rom(ed)
        info, rr, sdk, d2 = w.run("n%d" % N, 1, 0, 3, "30 0 0; 200 0 -127; 300 0 0")
        slots, icons = radar(d2)
        finds = all_steps(sdk, r" 8C0C9C9A ")
        clear = first(sdk, r" 8C04EFE0 ")
        tones = [(int(l.split()[1]), l.split()[4][3:].split(",")[0]) for l in sdk if " SE(" in l]
        info.update({"N": N, "spots": {"%02X" % k: v for k, v in place.items()}, "find_steps": finds, "clear_step": clear,
                     "after_clear": poses(rr, clear, 6) if clear is not None else [], "tones": [t for t in tones if t[1] != "2E9"][:24],
                     "beeps_2E9": sum(1 for t in tones if t[1] == "2E9"), "radar_slots": slots, "icons": icons})
        res["runs"].append(info)
        print("hunt N %d exit %d finds %s clear %s after %s slots %d" % (N, info["exit"], finds, clear, info["after_clear"][:2],
                                                                      len(slots)), flush=True)
    w.done()
    return res


def cmd_hunters(a):
    w = Work(a, "hunters")
    res = {"what": "another character hunts at level 1 act 0 (E7): the character number 3 -> c at %s, the radar task %s "
                   "made by --create-task for c != 3, row 0 {2,0,2,2, 0x0100}, a private SET by the character's letter with "
                   "spots on the character's line" % (", ".join("%08X" % x for x in CHR), RADAR), "runs": []}
    for c in a.chr:
        if c not in LETTER:
            print("character %d: one of %s" % (c, sorted(LETTER)))
            continue
        rows = k0_path(w, c, "30 0 0; 200 0 -127; 300 0 0")

        def at(step, rows=rows):
            r = rows[step]
            return (float(r["pos_x"]), float(r["pos_y"]) + 5.0, float(r["pos_z"]))
        place = {0x10: 60, 0x11: 60, 0x30: 90, 0x31: 90, 0x40: 120, 0x41: 120}
        w.private_disc(1, 0, c, {})          # this character's files too (motions, voices)
        for L in "SMKAEB":
            p = os.path.join(w.disc, "SET0100%s.BIN" % L)
            if L not in ("S",) and os.path.exists(p):
                os.remove(p)
        open(os.path.join(w.disc, "SET0100%s.BIN" % LETTER[c]), "wb").write(setfile(
            [(EME_P, 0, 0, i, at(s), (0, 0, 0)) for i, s in place.items()]))

        def ed(d, c=c):
            d[ROW0 - BASE:ROW0 - BASE + 6] = bytes([2, 0, 2, 2, 0x00, 0x01])
            for ad in CHR:
                assert d[ad - BASE] == 3, hex(ad)
                d[ad - BASE] = c
        w.rom(ed)
        info, rr, sdk, d2 = w.run("h%d" % c, 1, 0, c, "30 0 0; 200 0 -127; 300 0 0",
                                  extra=[] if c == 3 else ["--create-task", RADAR + ":2:3"])
        slots, _ = radar(d2)
        clear = first(sdk, r" 8C04EFE0 ")
        tones = [(int(l.split()[1]), l.split()[4][3:].split(",")[0]) for l in sdk if " SE(" in l]
        info.update({"chr": c, "find_steps": all_steps(sdk, r" 8C0C9C9A "), "clear_step": clear,
                     "after_clear": poses(rr, clear, 8) if clear is not None else [],
                     "tones": [t for t in tones if t[1] != "2E9"][:24], "beeps_2E9": sum(1 for t in tones if t[1] == "2E9"),
                     "radar_slots": slots})
        res["runs"].append(info)
        print("hunter %d exit %d finds %s clear %s after %s" % (c, info["exit"], info["find_steps"], clear,
                                                              info["after_clear"][:3]), flush=True)
    w.done()
    return res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=["touch", "hunt-n", "hunters"])
    ap.add_argument("values", nargs="*", type=int)
    ap.add_argument("--exe", default=os.path.join(ROOT, "harness", "build", "sa1h.exe"))
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "traces", "oracle100"))
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    a.n = a.values or [1, 2, 5, 12]
    a.chr = a.values or [0, 2, 5, 6, 7, 3]
    res = {"touch": cmd_touch, "hunt-n": cmd_hunt_n, "hunters": cmd_hunters}[a.cmd](a)
    os.makedirs(a.out, exist_ok=True)
    p = os.path.join(a.out, a.cmd + ".json")
    json.dump(res, open(p, "w"), indent=1)
    print("wrote %s" % p)
    bad = [r for r in res["runs"] if r["exit"] != 0 or r["faults"]]
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
