#!/usr/bin/env python3
"""talk_ss.py - test-time reference 2 of harness/tests/sim/talkref (G-SA1 1.0.0, prep lane p-refs-T;
sadc docs/research/gsa1_100_npc.md 9, 15.2 items 1-4, tests T4 and T6; gsa1_100_master.md R0 "Later prep", R5b).

Real talks on the ORIGINAL game in Station Square, on the stock SH-4 harness (`sa1h trace`, the owner's disc): level 26
act 0, Sonic standing at his start (270, 4, 277), and one townsperson made as the SET spawner 8C091380 makes an object:
`--create-task 0C90F35E:2:3@X,Y,Z,0,ANGY,0/0,0,MSG+ocp+late` (objexec_O_PEOPLE, IM 2 and list 3 from ADV00's own object
list, MEASURED at 0CA5E130; ang.x the type, ang.y its facing, scl.z its message number in SS_MES_S_E.BIN). The game's own
code does the rest: the person registers with the talk manager 8C10A146 (its radius: the motion set's word +0x58 plus
20.0), the manager 8C10A28E picks it on a B, X or Y press, turns both ports off (8C0509DA), stops Sonic (8C07256A),
turns his head (8C07C318), the runner opens the window and types the pages, A turns them, and the end turns the ports
back on (8C0509B8).

Per case it records, from the run's own logs: the person's node in the talk manager's list (radius, flags, place, angle:
read from a RAM image of step 30 that is deleted at once), Sonic's p.rad (playerwk +0x10C), the step of the pick
(D_8C5FBAC4 != 0), the ports per step (D_8C19E3A9, D_8C19E3AA), the window's alphas per step (B_8C78C518 +0x1F the band,
+0x1B the text), the steps of the window's open, each page's text (8C054F2E), its close, the head turn, the speed cleared,
and the band's draws (njDrawPolygon2D from 8C054AF0) with their alpha. The cases settle the talk radius (a man: 23 + p.rad 4
= 27 units, against v1_npcs.md 1.3's 7 + 20 + 4 = 31; a fat man: 24 + 4 = 28 against 29), the +-80 degrees facing test,
the 10-unit height test, the buttons (B, X and Y talk, A does not), a walk up to a person, and one whole talk.

What it writes (`--out`, default build/traces/talkref/talk_ss.json; only under the project's build/ folder, which
repos/sync_repos.py never enters): numbers and names only, no text of the game. The traces, logs and RAM images live in
`--work` (default build/scratch/talkref/ss) and are deleted at the end unless --keep.

    py -3.12 harness/tests/sim/talkref/talk_ss.py                  # every case
    py -3.12 harness/tests/sim/talkref/talk_ss.py --cases talk edge_in edge_out
    options: --exe EXE (default harness/build/sa1h.exe), --out FILE, --work DIR, --keep
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
            sys.exit("talk_ss: cannot find the sadc folder (set SADC_ROOT)")
        d = up


ROOT = find_root()
LEVEL, ACT, CHR = 26, 0, 0
START = (270.0, 4.0, 277.0)       # Sonic's start at level 26 act 0 (MEASURED: trace step 0; checked on every run)
PEOPLE = "0C90F35E:2:3"           # objexec_O_PEOPLE, IM 2, list 3 (MEASURED: ADV00's object list entry at 0CA5E130)
MSG = 0                           # message 0 of SS_MES_S_E.BIN (Sonic's Station Square talk file)
PRESS = 60                        # the step of the talk button in the standing cases
FACING = 0x4000                   # the person's ang.y whose facing test (8C10A28E) is centred on Sonic at +Z of it
SYMS = ["8C5FBAC0 8 v 1 TALK_MANAGER", "8C19E3A8 4 v 1 PORTS", "8C78C508 10 v 1 MSG_STATE", "8C78C518 3C v 1 MSGC",
        "8C1BA1E0 8 v 1 MSG_FLAGS", "8C754C08 20 v 1 PER0", "8C754EC8 4 v 1 SUB_STEP"]
SDK = ["8C0509DA PortsOff ?", "8C0509B8 PortsOn ?", "8C07256A ClearSpeed ?", "8C055794 WindowOpen ?",
       "8C054F2E PageText ?", "8C055C22 WindowClose ?", "8C07C318 LookOn ?", "8C07C376 LookOff ?"]

# name: (type, distance in front, facing offset (BAMS, added to FACING), height offset, button, what it checks)
CASES = {
    "talk":       (0, 20.0, 0, 0.0, "B", "one whole talk: B, then A for every page"),
    "edge_in":    (0, 26.9, 0, 0.0, "B", "a man: 23 + p.rad 4 = 27; just inside"),
    "edge_out":   (0, 27.1, 0, 0.0, "B", "just outside 27"),
    "v1_edge":    (0, 30.9, 0, 0.0, "B", "inside v1_npcs.md 1.3's 7 + 20 + 4 = 31, outside 27"),
    "fat_in":     (18, 27.9, 0, 0.0, "B", "a fat man (motion set 4, +0x58 = 4): 24 + 4 = 28; just inside"),
    "fat_out":    (18, 28.1, 0, 0.0, "B", "just outside 28 (v1_npcs.md's 5 + 20 + 4 = 29 would talk)"),
    "behind":     (0, 20.0, 0x8000, 0.0, "B", "the person faces away (the facing test 0x8000 off)"),
    "side_in":    (0, 20.0, 14500, 0.0, "B", "the facing test 14500 off (< 14563, the +-80 degrees)"),
    "side_out":   (0, 20.0, 14600, 0.0, "B", "the facing test 14600 off (> 14563)"),
    "above_in":   (0, 20.0, 0, 9.9, "B", "9.9 above (flag 2: |d.y| < 10)"),
    "above_out":  (0, 20.0, 0, 10.1, "B", "10.1 above"),
    "below_in":   (0, 20.0, 0, -9.9, "B", "9.9 below"),
    "below_out":  (0, 20.0, 0, -10.1, "B", "10.1 below"),
    "button_a":   (0, 20.0, 0, 0.0, "A", "A does not talk (D_8C19E3A0 = 0x602: B, X, Y)"),
    "button_x":   (0, 20.0, 0, 0.0, "X", "X talks"),
    "button_y":   (0, 20.0, 0, 0.0, "Y", "Y talks"),
    "walk_up":    (0, None, 0, 0.0, "B", "Sonic walks up to the person and presses B at the first step inside 27"),
    "walk_early": (0, None, 0, 0.0, "B", "the same walk, B at the last step outside 28.5"),
}


def bams_atan2(x, z):
    """8C085A50(x, z): atan2(x, z) in BAMS (MEASURED with sa1h call: (1,0) 0x4000, (0,1) 0, (-1,0) -0x4000, (0,-1) -0x8000)"""
    return int(round(math.atan2(x, z) * 32768.0 / math.pi)) & 0xFFFF


def short(v):
    v &= 0xFFFF
    return v - 0x10000 if v >= 0x8000 else v


class Work:
    def __init__(self, a):
        self.exe = os.path.abspath(a.exe)
        self.dir = os.path.abspath(a.work)
        os.makedirs(self.dir, exist_ok=True)
        self.syms = os.path.join(self.dir, "syms.txt")
        self.sdk = os.path.join(self.dir, "sdk.txt")
        open(self.syms, "w").write("# talk_ss.py: address size class referenced name\n" + "\n".join(SYMS) + "\n")
        open(self.sdk, "w").write("\n".join(SDK) + "\n")
        self.keep = a.keep

    def run(self, tag, inp, person=None, ramat=30):
        out = os.path.join(self.dir, tag)
        args = [self.exe, "--root", ROOT, "trace", "--level", str(LEVEL), "--act", str(ACT), "--chr", str(CHR),
                "--input", inp, "--print", "0", "--out", out, "--dump-world", out + ".world.txt", "--world-syms",
                self.syms, "--log-sdk", out + ".sdk.txt", "--sdk-list", self.sdk, "--log-2d", out + ".d2d.txt"]
        if ramat is not None:
            args += ["--dump-ram-at", str(ramat)]
        if person:
            (x, y, z), angy, typ = person
            args += ["--create-task", "%s@%.4f,%.4f,%.4f,%X,%X,0/0,0,%d+ocp+late" % (PEOPLE, x, y, z, typ, angy & 0xFFFF,
                                                                                     MSG)]
        r = subprocess.run(args, capture_output=True, text=True, timeout=1800)
        txt = r.stdout + r.stderr
        if r.returncode or not os.path.exists(out + ".csv"):
            sys.exit("talk_ss: the harness run %s failed (exit %d):\n%s" % (tag, r.returncode, txt[-3000:]))
        rows = list(csv.DictReader(open(out + ".csv", newline="")))
        node = prad = None
        ram = "%s.step%d.ram" % (out, ramat) if ramat is not None else None
        if ram and os.path.exists(ram):
            node, prad = read_ram(open(ram, "rb").read(), person)
            os.remove(ram)              # a RAM image is game bytes: read at once, never kept
        world = parse_world(out + ".world.txt")
        sdk = [l.split() for l in open(out + ".sdk.txt", errors="replace") if l[:1] in "SFD"]
        band = []
        for l in open(out + ".d2d.txt", errors="replace"):
            f = l.split()
            if len(f) > 8 and f[0] == "P" and f[7] == "8C054AF0":
                band.append((int(f[2]), int(f[-4].split(",")[2], 16) >> 24))
        return {"rows": rows, "node": node, "p_rad": prad, "world": world, "sdk": sdk, "band": band,
                "faults": [l[:160] for l in txt.splitlines() if re.search(r"fault|FAULT|illegal|unmapped", l)][:3]}

    def done(self):
        if not self.keep:
            shutil.rmtree(self.dir, ignore_errors=True)


def read_ram(r, person):
    """the person's node of the talk manager's list D_8C5FBAC0 (READ v1_npcs.md 1.3: +4 next, +8 key, +0xC pos *,
    +0x10 ang *, +0x14 flag *, +0x18 radius, +0x1C flags) and player 0's p.rad (B_8C78C668[0] + 0x10C)"""
    u32 = lambda a: struct.unpack_from("<I", r, a & 0xFFFFFF)[0]
    f32 = lambda a: struct.unpack_from("<f", r, a & 0xFFFFFF)[0]
    pw = u32(0x8C78C668)
    prad = f32(pw + 0x10C) if pw else None
    best, h, n = None, u32(0x8C5FBAC0), 0
    while h and n < 512:
        pos, ang = u32(h + 0x0C), u32(h + 0x10)
        p = [f32(pos), f32(pos + 4), f32(pos + 8)] if pos else None
        rec = {"radius": f32(h + 0x18), "flags": u32(h + 0x1C), "pos": p,
               "ang": (u32(ang) & 0xFFFF) if ang else None}
        if person and p and all(abs(p[i] - person[0][i]) < 0.01 for i in range(3)):
            best = rec
        h, n = u32(h + 4), n + 1
    return best, prad


def parse_world(path):
    """--dump-world: the bytes of each symbol after every step -> {step: {name: bytes}} (the changes applied)"""
    cur, out, step = {}, {}, None
    for l in open(path, errors="replace"):
        l = l.strip()
        if not l or l.startswith("#"):
            continue
        if l.startswith("@"):
            if step is not None:
                out[step] = {k: bytes(v) for k, v in cur.items()}
            step = -1 if l == "@start" else int(l.split()[1])
            continue
        name_off, hexs = l.split()
        name, off = name_off.rsplit("+", 1)
        off = int(off, 16)
        b = bytes.fromhex(hexs)
        buf = cur.setdefault(name, bytearray(off + len(b)))
        if len(buf) < off + len(b):
            buf.extend(b"\0" * (off + len(b) - len(buf)))
        buf[off:off + len(b)] = b
    if step is not None:
        out[step] = {k: bytes(v) for k, v in cur.items()}
    return out


def state(world, step):
    w = world.get(step, {})
    tm = w.get("TALK_MANAGER", b"\0" * 8).ljust(8, b"\0")
    po = w.get("PORTS", b"\0" * 4).ljust(4, b"\0")
    m = w.get("MSGC", b"\0" * 0x3C).ljust(0x3C, b"\0")
    return {"active": struct.unpack_from("<I", tm, 4)[0], "ports": [po[1], po[2]], "band": m[0x1F], "text": m[0x1B]}


def summarise(name, spec, res, press_step, extra):
    typ, d, off, dy, button, what = spec
    world, rows = res["world"], res["rows"]
    steps = sorted(k for k in world if k >= 0)
    pick = next((s for s in steps if state(world, s)["active"]), None)
    calls = {}
    for f in res["sdk"]:
        calls.setdefault(f[4].split("(")[0], []).append(int(f[1]))
    series, last = [], None
    for s in steps:
        st = state(world, s)
        key = (st["active"] != 0, st["ports"][0], st["ports"][1], st["band"], st["text"])
        if key != last:
            series.append([s, int(key[0]), key[1], key[2], key[3], key[4]])
            last = key
    out = {"what": what, "type": typ, "button": button, "press_step": press_step, "talk": pick is not None,
           "pick_step": pick, "node": res["node"], "p_rad": res["p_rad"], "faults": res["faults"],
           "calls": {k: v for k, v in sorted(calls.items())},
           "changes": series, "changes_columns": ["step", "picked", "port0", "port1", "band_alpha", "text_alpha"],
           "band_draws": res["band"][:400]}
    out.update(extra)
    if press_step is not None and press_step < len(rows):
        r0 = rows[press_step - 1] if press_step else rows[0]
        r1 = rows[press_step]
        out["sonic_before_press"] = [float(r0["pos_x"]), float(r0["pos_y"]), float(r0["pos_z"])]
        out["sonic_at_press"] = [float(r1["pos_x"]), float(r1["pos_y"]), float(r1["pos_z"])]
        out["speed_before_press"] = [float(r0["pspd_x"]), float(r0["pspd_y"]), float(r0["pspd_z"])]
        if pick is not None and pick < len(rows):
            r2 = rows[pick]
            out["speed_after_pick"] = [float(r2["pspd_x"]), float(r2["pspd_y"]), float(r2["pspd_z"])]
            out["motion_speed_after_pick"] = [float(r2["mspd_x"]), float(r2["mspd_y"]), float(r2["mspd_z"])]
    return out


def phases(world, calls, presses):
    """the whole talk's steps: open, each page's wait and turn, the close (the window's numbers of npc 15.2 item 4)"""
    steps = sorted(k for k in world if k >= 0)
    st = {s: state(world, s) for s in steps}
    opn = calls.get("WindowOpen", [None])[0]
    pages = calls.get("PageText", [])
    full = [s for s in steps if st[s]["text"] == 0xFF]
    out = {"window_open": opn, "page_text": pages, "presses": presses}
    if opn is not None:
        b128 = next((s for s in steps if s >= opn and st[s]["band"] == 128), None)
        t255 = next((s for s in steps if s >= opn and st[s]["text"] == 0xFF), None)
        out["open"] = {"band_128": b128, "text_255": t255, "steps": (t255 - opn + 1) if t255 else None,
                       "band": [st[s]["band"] for s in range(opn, (t255 or opn) + 1)],
                       "text": [st[s]["text"] for s in range(opn, (t255 or opn) + 1)]}
    turns = []
    for p in presses[:-1]:
        nxt = next((x for x in pages if x > p), None)
        back = next((s for s in steps if s > p and st[s]["text"] == 0xFF and (nxt is None or s >= nxt)), None)
        dip = next((s for s in steps if s >= p and st[s]["text"] != 0xFF), None)
        turns.append({"press": p, "text_falls_from": dip, "next_page_text": nxt, "text_255_again": back,
                      "steps": (back - dip + 1) if back and dip else None,
                      "text": [st[s]["text"] for s in range(dip, back + 1)] if back and dip else None})
    out["turns"] = turns
    if presses:
        p = presses[-1]
        dip = next((s for s in steps if s >= p and st[s]["text"] != 0xFF), None)
        b0 = next((s for s in steps if dip and s > dip and st[s]["band"] == 0), None)
        out["close"] = {"press": p, "text_falls_from": dip, "band_0": b0,
                        "window_close": next((x for x in calls.get("WindowClose", []) if x > p), None),
                        "ports_on": next((x for x in calls.get("PortsOn", []) if x > p), None),
                        "look_off": next((x for x in calls.get("LookOff", []) if x > p), None),
                        "picked_cleared": next((s for s in steps if s > p and not st[s]["active"]), None),
                        "band": [st[s]["band"] for s in range(dip, (b0 or dip) + 3)] if dip else None,
                        "text": [st[s]["text"] for s in range(dip, (b0 or dip) + 3)] if dip else None}

    def ranges(key):
        r, on = [], None
        for s in steps:
            if st[s][key] and on is None:
                on = s
            elif not st[s][key] and on is not None:
                r.append([on, s - 1])
                on = None
        if on is not None:
            r.append([on, steps[-1]])
        return r
    out["band_nonzero"] = ranges("band")      # the band's alpha is not 0 (MSG_Disp draws it, on sub-step 0)
    out["text_nonzero"] = ranges("text")      # the text's alpha is not 0: a switch (8C054A00's constant vertex colours)
    return out


def person_at(d, off, dy):
    """the person d units from Sonic's start along -Z, its facing test centred on him plus off"""
    return (START[0], START[1] + dy, START[2] - d), (FACING + off) & 0xFFFF


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--exe", default=os.path.join(ROOT, "harness", "build", "sa1h.exe"))
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "traces", "talkref", "talk_ss.json"))
    ap.add_argument("--work", default=os.path.join(ROOT, "build", "scratch", "talkref", "ss"))
    ap.add_argument("--cases", nargs="*", default=list(CASES))
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    out = os.path.abspath(a.out)
    build = os.path.join(ROOT, "build") + os.sep
    if not out.startswith(build) or not os.path.abspath(a.work).startswith(build):
        sys.exit("talk_ss: --out and --work must lie under %s (a folder repos/sync_repos.py never enters)" % build)
    bad = [c for c in a.cases if c not in CASES]
    if bad:
        sys.exit("talk_ss: unknown cases %s (known: %s)" % (bad, " ".join(CASES)))
    t0 = time.time()
    w = Work(a)
    doc = {"what": "real talks on the original game in Station Square (talk_ss.py, sa1h trace); numbers only",
           "version": 1, "level": LEVEL, "act": ACT, "chr": CHR, "start": list(START), "people_task": PEOPLE,
           "message": MSG, "facing": FACING, "cases": {}}
    walk = None
    for name in a.cases:
        spec = CASES[name]
        typ, d, off, dy, button, what = spec
        extra = {}
        if name in ("walk_up", "walk_early"):
            if walk is None:
                # the walk without a person: Sonic's place after every step (stick forward from step 30)
                cal = w.run("walk_cal", "30 0 0; 170 0 -127", None, ramat=None)
                walk = [(float(r["pos_x"]), float(r["pos_y"]), float(r["pos_z"])) for r in cal["rows"]]
            P = walk[160]
            lim = 27.0 if name == "walk_up" else 28.5
            hd = [math.hypot(q[0] - P[0], q[2] - P[2]) for q in walk]
            # the manager runs before the player (list 0 against list 1): it sees the place of the step before
            k = next(i for i in range(31, 160) if hd[i - 1] < lim) if name == "walk_up" else \
                max(i for i in range(31, 160) if hd[i - 1] >= lim)
            dx, dz = walk[k - 1][0] - P[0], walk[k - 1][2] - P[2]
            angy = (bams_atan2(dx, dz) + 0x4000) & 0xFFFF
            person = ((P[0], P[1], P[2]), angy, typ)
            inp = "30 0 0; %d 0 -127; 1 0 -127 B; %d 0 -127" % (k - 30, 200 - k - 1)
            extra = {"person": [P[0], P[1], P[2]], "person_ang_y": angy,
                     "horizontal_distance_before_press": hd[k - 1], "horizontal_distance_two_before": hd[k - 2]}
            press = k
        else:
            pos, angy = person_at(d, off, dy)
            person = (pos, angy, typ)
            press = PRESS
            dxz = (START[0] - pos[0], START[2] - pos[2])
            a_ = bams_atan2(dxz[0], dxz[1])
            extra = {"person": list(pos), "person_ang_y": angy, "horizontal_distance": math.hypot(*dxz),
                     "dy": START[1] - pos[1], "facing_test": abs(short(a_ - angy + 0x4000))}
            if name == "talk":
                # first a run with an A every 100 steps to count the pages, then the reference with one A per page
                probe = w.run("talk_probe", "%d 0 0; 1 0 0 B; " % PRESS + "; ".join(["99 0 0; 1 0 0 A"] * 12) +
                              "; 100 0 0", person, ramat=None)
                npages = len([f for f in probe["sdk"] if "PageText(" in f[4]])
                presses = [PRESS + 100 * (i + 1) for i in range(max(npages, 1))]
                inp = "%d 0 0; 1 0 0 B; " % PRESS + "; ".join(["99 0 0; 1 0 0 A"] * len(presses)) + "; 150 0 0"
                extra["pages"] = npages
            else:
                inp = "%d 0 0; 1 0 0 %s; 60 0 0" % (PRESS, button)
        res = w.run(name, inp, person)
        if res["rows"] and [round(float(res["rows"][0][k]), 3) for k in ("pos_x", "pos_y", "pos_z")] != list(START):
            sys.exit("talk_ss: Sonic does not start at %s in this build" % (START,))
        rec = summarise(name, spec, res, press, extra)
        if name == "talk":
            rec["phases"] = phases(res["world"], rec["calls"], presses)
            if rec["pick_step"] is not None and rec["phases"].get("window_open") is not None:
                rec["phases"]["open_after_pick"] = rec["phases"]["window_open"] - rec["pick_step"]
        rec["input"] = inp
        doc["cases"][name] = rec
        print("talk_ss: %-10s talk %-5s pick %-5s node radius %s p.rad %s" %
              (name, rec["talk"], rec["pick_step"], rec["node"] and rec["node"]["radius"], rec["p_rad"]))
    exp = {"talk": True, "edge_in": True, "edge_out": False, "v1_edge": False, "fat_in": True, "fat_out": False,
           "behind": False, "side_in": True, "side_out": False, "above_in": True, "above_out": False, "below_in": True,
           "below_out": False, "button_a": False, "button_x": True, "button_y": True, "walk_up": True,
           "walk_early": False}
    doc["expected_by_npc_15_2"] = {k: exp[k] for k in doc["cases"]}
    doc["agrees"] = all(doc["cases"][k]["talk"] == exp[k] for k in doc["cases"])
    doc["seconds"] = round(time.time() - t0, 1)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        json.dump(doc, f, indent=1)
        f.write("\n")
    w.done()
    print("talk_ss: %d cases, every one as gsa1_100_npc.md 15.2 reads the code: %s, %.1f s -> %s" %
          (len(doc["cases"]), doc["agrees"], doc["seconds"], out))
    return 0 if doc["agrees"] else 1


if __name__ == "__main__":
    sys.exit(main())