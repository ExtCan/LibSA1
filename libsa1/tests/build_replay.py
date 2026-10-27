#!/usr/bin/env python3
"""Build and run tests/replay_test.c (M1c / M1 / M1.1 / Phase 7 through the public API; sadc
docs/libsa1_integration.md 9.1 V5-V7, 11 "First milestone", Phase 7) against build/<target>/libsa1.a.

    py -3.12 tests/build_replay.py [--target x86_64-windows|x86-windows|x86_64-linux|all] [--run]
                                   [--sadc DIR] [--gdi FILE] [--out DIR] [--tolerant] [--no-stage-tasks] [--pure]
                                   [--trace NAME]...

Run it from LIB after `py -3.12 build.py --target <target>` (the archive must hold the whole lowered game). The test
is compiled with the flags of the world tests (src/world/build_world_tests.py: the world core's flags,
-DSA1_STATIC, -I<sadc>/harness/src for sa1_trace.h).

--run (runnable targets only) runs build/<target>/replay_test with --out <out>/<target> (default
build/replay/<target>), then for every reference trace
    py -3.12 <sadc>/harness/tools/sa1trace.py diff <sadc>/build/traces/ref/NAME.bin <out>/<target>/NAME.bin --ulps 0
(V6, and NAME.reseed.bin for V5, NAME.disc.bin for DISC mode). With --target all, the outputs of x86_64-windows and
x86-windows must be byte-identical (V7); x86_64-linux is built (and run on a Linux host).

Strict is the default and the gate for all ten reference traces: replay_test fails unless every record of V6, V5 and
DISC is identical in every decoded field and raw blob (DISC = FAITHFUL, no known issue), and every sa1trace.py diff
must report "identical" (exit 0). The worlds have the stage's own tasks (SA1_CREATE_STAGE_TASKS: the stage overlay of
each trace's stage from the disc, its level tasks as far as the stage's code is decompiled; sadc
docs/libsa1_integration.md 6.2). --tolerant / --no-stage-tasks are for information only.

HOSTOBJ (round 5, sadc gsa1_rounds_plan.md 9.4): references recorded by the harness's host-object oracle (`sa1h trace
--hostobj FILE`: libsa1's own host-object layer on the original game code, sadc harness/src/ho_oracle.c) carry a sidecar
NAME.obj.bin (sadc harness/src/sa1_trace_ext.h): the host calls before each step and what each step gave. replay_test's
replays feed those calls through the public API (tests/world_replay.c) and write the world's own sidecar next to each
output trace; sa1trace.py diff compares both (strict, like the trace). replay_test takes at most 16 traces per run
(MAX_JOBS): the list is run in chunks. Round 0.9.2: a sidecar may be version 2 (`trace --create-task .../SX,SY,SZ+ocp+late`:
the tasks' scale and OBJ_CONDITION, and each step's condition word, which the replay compares too); version 1 is read as
before. RINGS: the ring references of gsa1_090_rings.md 5.2, replayed like HOSTOBJ. ITEMBOX (round 0.9.4): the ground
item box of gsa1_094_plan.md 6.4, replayed like HOSTOBJ (a task flag SA1_TOBJ_T_OCPW gives the initial condition word).
The chunks also keep at most 4 character kinds per stage (the DISC context's image limit).

Inputs: the reference traces v2 <sadc>/build/traces/ref/*.bin, the landtable dump of each trace's stage
<sadc>/build/traces/land/L<level>A<act> (.tri/.col, harness --dump-land v2: L01A0 for the ec1_* scripts, L04A0
tails_sh1, L05A2 knux_rm3, L03A1 amy_tp2, L10A0 gamma_fe1, L01A2 big_ec3; made with a pure harness build:
    harness/build/sa1h_<tag>.exe trace <the script's "# args:"> --input @harness/tests/sim/NAME.txt
        --dump-land build/traces/land/L<level>A<act>)
and the user's GD-ROM dump (--gdi, default $SA1_TEST_GDI; never copied).
"""
import argparse
import filecmp
import glob
import os
import platform
import struct
import subprocess
import sys

LIB = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TARGETS = {"x86_64-windows": "x86_64-windows-gnu", "x86-windows": "x86-windows-gnu", "x86_64-linux": "x86_64-linux-gnu"}
M1 = ["ec1_stand", "ec1_run_jump"]
M11 = ["ec1_turns", "ec1_jump_hold", "ec1_spindash"]
P7 = ["tails_sh1", "knux_rm3", "amy_tp2", "gamma_fe1", "big_ec3"]     # Phase 7: the other 1ST_READ characters
# round 5: the host-object oracle's references (with NAME.obj.bin; sadc harness/tests/sim/<name>.txt, ho/<name>.ho)
HOSTOBJ = ["ho_jump_ec1", "ho_touch_ec1", "ho_damage_ec1", "ho_homing_ec1", "ho_e102_fe1", "ho_hold_ec1", "balloon_ec1",
           "ho_hurt_tails_sh1", "ho_hurt_knux_rm3", "ho_hurt_amy_tp2", "ho_hurt_gamma_fe1", "ho_hurt_big_ec3", "hold_ec1",
           "ho_tails_sh1",
           # round 0.9.1 (sadc docs/research/gsa1_070_bird.md F2 / F3, gsa1_070_refs.md 3): Amy's bird at Emerald Coast act 1
           # (`--create-task 8C0DECDA:2:3`), made in the replay through sa1_character_set_companion (tests/world_replay.c)
           "amy_bird_ec1",
           # round 0.9.2 (sadc docs/research/gsa1_090_plan.md 6.5, gsa1_090_faults.md F1 / F2): a host hit before the
           # character's first step (F1: held in mode 0, applied at step 1), the air item box with each of its 9 items
           # (`--create-task 8C05D9F6:15:3@-9,4,4/N,0,0+late`, sidecar version 2: scl.x = the item), and the bomb item
           # with host ENEMY proxies (two HITs through the game's enemy check, sa1_world_hostobj.c ho_bomb)
           "ho_dmg_step0_ec1", "airbox_ec1", "airbox_1_ec1", "airbox_2_ec1", "airbox_3_ec1", "airbox_4_ec1",
           "airbox_5_ec1", "airbox_6_ec1", "airbox_7_ec1", "airbox_8_ec1", "airbox_bomb_ec1"]
# round 0.9.2 (sadc docs/research/gsa1_090_rings.md 5.2 with its Review R.2.4 / R.4.3, gsa1_090_plan.md 6.5): rings made the
# SET spawner's way after LoadPlayerTask: four RINGs on Sonic's running line (E12, sidecar version 1), and O_GRING groups with
# an OBJ_CONDITION (`--create-task 8C087B80:2:3@...+ocp+late`, sidecar version 2: the condition word of every step is
# compared, the group's scUserFlag bits of 8C087EAC / 8C091C00): a line (7, 10, 0) and a circle (7, 20, 1), each partly taken
RINGS = ["ring_line_ec1", "gring_line_ec1", "gring_circle_ec1"]
# round 0.9.1 (sadc docs/research/gsa1_070_refs.md 5.4): a real stage's drowning (Big in Emerald Coast act 3's lake, 71 deep:
# the stage's own water, which W1 leaves alone); with SA1_REPLAY_EVENTS=1 the replay prints its SA1_EVENT_DEATH (cause
# DROWN, step 2115) and RESTART (step 2355)
DROWN = ["big_drown_ec3"]
# round 0.9.1 (the HUD, sadc docs/research/gsa1_070_refs.md 4 / 4.2; tests/hud_test.c part 12 compares their 2D draws):
# one action stage per character with the game clock running (the sidecar task 8C08A5AE) and ring calls (the sidecar's
# host calls), replayed like HOSTOBJ (the HUD pass stays off: replays never run it). hud_knux_rm3 is left out: libsa1 leaves
# it at step 398 and does not die at step 425 as the reference does, with or without any HUD (a Red Mountain act 3 world
# gap, reported by 091-lib-hud; tests/hud_test.c part 12 prints it as known)
HUD = ["hud_sonic_ec1", "hud_tails_sh1", "hud_amy_tp2", "hud_big_ec3", "hud_gamma_fe1"]
# round 6a / 0.9.3a (sadc gsa1_rounds_plan.md 11.1 / 11.2, docs/research/super_general_design.md 8.1.3): the harness's Super
# free-roam references (`trace ... --no-objects --super-free`: Sonic on the Perfect Chaos stage, level 19 act 0, the trigger at
# step 2, the sea plane of the stage's landtable L19A0, no drain). The worlds are Sonic worlds with the stage's own tasks
# (B_CHAOS7's level task stand-in: the sea plane and the trigger), strict like the others. super_pc_play (the boss fight with
# B_CHAOS7 routed) is not in the list: the boss tasks are not decompiled (908 of 909 records differ from step 1)
SUPER = ["super_free_run", "super_free_long"]
# round 0.9.4 (sadc docs/research/gsa1_094_plan.md 6.4 with its Review R.6 / R.7, harness README "Round 0.9.4"): the ground
# item box objexec_O_ITEMBOX 8C05D10C (C since 094-land) made the SET spawner's way (`--create-task 8C05D10C:2:3@...
# /N,0,0+ocp+late`, sidecar version 2; itembox_shell_ec1 with `+ocp=8005`, task flag SA1_TOBJ_T_OCPW: already broken, the
# shell at once). The condition word of every step is compared (0x8011 -> 0x8015 at the break, E-102: 0x0010 at his fire,
# then 0x0014). Each character's break: Sonic on the lid with each of the 9 items, his roll, Tails' jump ball, Knuckles'
# punch, Amy on the lid, E-102's lock-on, Big's body, Super Sonic's body; Tails' tail attack and Amy's hammer on their
# own stages (Speed Highway act 1, Twinkle Park act 2: the camera sees them, the box breaks) and Amy's hammer in Emerald
# Coast (no contact). itembox_tailatk_ec1 (Tails' tail attack in Emerald Coast, no contact) is a harness reference only:
# the game camera's view is NaN there for every character but Sonic and E-102, and Tails' tail-trail tips
# (func_8C0BEDA0: pwp->free.f[0..3] = njCalcPoint of the NaN matrix) come out as the NaN 0x7FFFFFFF in the interpreter
# and 0x7FBFFFFF in libsa1 from step 61 (V6 169 / V5 30 records differ in those 4 words only; sa1trace.py's 0-ulp diff
# calls them equal): a NaN-encoding gap of a degenerate camera, not of the box; the harness's GUEST-ARENA build (pclower's
# lowering, the original SDK interpreted) reproduces the reference, so libsa1's clean-room runtime makes the other NaN
ITEMBOX = ["itembox_ec1", "itembox_1_ec1", "itembox_2_ec1", "itembox_3_ec1", "itembox_4_ec1", "itembox_5_ec1",
           "itembox_6_ec1", "itembox_7_ec1", "itembox_8_ec1", "itembox_roll_ec1", "itembox_shell_ec1", "itembox_gamma_ec1",
           "itembox_tails_ec1", "itembox_knux_ec1", "itembox_amy_ec1", "itembox_big_ec1", "itembox_hammer_ec1",
           "itembox_tailatk_sh1", "itembox_hammer_tp2", "itembox_super_free"]
REPLAY_MAX_JOBS = 16    # replay_test.c MAX_JOBS
REPLAY_DISC_IMAGES = 4  # src/world/sa1_api_world.c SA1W_DISC_IMAGES: character kinds per stage in one DISC context


def runnable(key):
    if key.endswith("windows"):
        return platform.system() == "Windows"
    return platform.system() == "Linux" and platform.machine().lower() in ("x86_64", "amd64")


def build(key, sadc):
    triple = TARGETS[key]
    out = os.path.join("build", key)
    lib = os.path.join(out, "libsa1.a")
    if not os.path.exists(os.path.join(LIB, lib)):
        raise SystemExit("%s missing: run py -3.12 build.py --target %s first" % (lib, key))
    flags = ["-target", triple, "-std=gnu11", "-Iinclude", "-Isrc", "-Isrc/world", "-I" + os.path.join(sadc, "include"),
             "-I" + os.path.join(sadc, "harness", "src"), "-Wall", "-Wextra", "-Wshadow", "-Wstrict-prototypes",
             "-Wundef", "-ffp-contract=off", "-frounding-math", "-fno-builtin", "-O2", "-g", "-DSA1_STATIC"]
    if key.startswith("x86-"):
        flags += ["-msse2", "-mfpmath=sse"]
    if not key.endswith("windows"):
        flags += ["-fPIC"]
    libs = [] if key.endswith("windows") else ["-lm", "-lpthread"]
    exe = os.path.join(out, "replay_test" + (".exe" if key.endswith("windows") else ""))
    cmd = [sys.executable, "-m", "ziglang", "cc", *flags, os.path.join("tests", "replay_test.c"), lib, "-o", exe, *libs]
    r = subprocess.run(cmd, cwd=LIB, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
    if r.stdout.strip():
        print(r.stdout.rstrip())
    if r.returncode != 0:
        raise SystemExit("FAILED: " + " ".join(cmd))
    print("[%s] built %s" % (key, exe))
    return exe


def ref_stage_kind(sadc, name):
    """(level, act), character of a reference trace (sadc harness/src/sa1_trace.h header), or None"""
    try:
        with open(os.path.join(sadc, "build", "traces", "ref", name + ".bin"), "rb") as f:
            h = f.read(32)
    except OSError:
        return None
    if len(h) < 32 or h[:8] != b"SA1TRC01":
        return None
    level, act, chr_ = struct.unpack_from("<hhb", h, 24)
    return (level, act), chr_


def replay_chunks(sadc, traces):
    """the traces in order, cut into replay_test runs of at most REPLAY_MAX_JOBS traces with at most
    REPLAY_DISC_IMAGES character kinds per stage"""
    chunks, cur, kinds = [], [], {}
    for t in traces:
        sk = ref_stage_kind(sadc, t)
        full = len(cur) >= REPLAY_MAX_JOBS
        if sk is not None and sk[1] not in kinds.get(sk[0], ()) and len(kinds.get(sk[0], ())) >= REPLAY_DISC_IMAGES:
            full = True
        if cur and full:
            chunks.append(cur)
            cur, kinds = [], {}
        cur.append(t)
        if sk is not None:
            kinds.setdefault(sk[0], set()).add(sk[1])
    if cur:
        chunks.append(cur)
    return chunks


def run(key, exe, sadc, gdi, out_dir, extra, traces):
    od = os.path.join(out_dir, key)
    os.makedirs(od, exist_ok=True)
    for f in glob.glob(os.path.join(od, "*.bin")):
        os.remove(f)
    rc = False
    # replay_test keeps at most REPLAY_MAX_JOBS traces, and its DISC context of one stage holds at most
    # REPLAY_DISC_IMAGES character kinds: one run per chunk (round 0.9.4: ITEMBOX has six kinds in Emerald Coast act 1)
    for chunk in replay_chunks(sadc, traces):
        cmd = [os.path.join(LIB, exe), "--out", od] + extra + sum((["--trace", t] for t in chunk), []) + [sadc, gdi]
        r = subprocess.run(cmd, cwd=LIB)
        print("[%s] replay_test exit %d (%s)" % (key, r.returncode, " ".join(chunk)))
        rc |= r.returncode != 0
    strict = "--tolerant" not in extra
    tool = os.path.join(sadc, "harness", "tools", "sa1trace.py")
    for name in traces:
        ref = os.path.join(sadc, "build", "traces", "ref", name + ".bin")
        for suffix, what in (("", "V6"), (".reseed", "V5"), (".disc", "DISC V6")):
            mine = os.path.join(od, name + suffix + ".bin")
            if not os.path.exists(mine):
                continue
            d = subprocess.run([sys.executable, tool, "diff", ref, mine, "--ulps", "0"], cwd=sadc,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
            print("[%s] sa1trace.py diff %s %s --ulps 0 (%s): exit %d" % (key, os.path.relpath(ref, sadc), mine, what,
                                                                          d.returncode))
            if strict and d.returncode != 0:
                rc = True
            if d.returncode != 0:
                # without --all the tool names only the first diverging fields: show every one
                d = subprocess.run([sys.executable, tool, "diff", ref, mine, "--ulps", "0", "--all"], cwd=sadc,
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
            lines = d.stdout.rstrip().splitlines()
            for line in lines[:14]:
                print("    " + line)
            if len(lines) > 14:
                print("    ... (%d more lines)" % (len(lines) - 14))
    return rc


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--target", default="x86_64-windows", choices=sorted(TARGETS) + ["all"])
    ap.add_argument("--sadc", default=os.path.join(os.path.dirname(LIB), "sadc"))
    ap.add_argument("--gdi", default=os.environ.get("SA1_TEST_GDI", ""))
    ap.add_argument("--out", default=os.path.join(LIB, "build", "replay"))
    ap.add_argument("--run", action="store_true")
    ap.add_argument("--strict", action="store_true", help="the default (kept for old command lines)")
    ap.add_argument("--tolerant", action="store_true", help="information: accept the residuals of stage tasks the "
                    "world does not have (a constant land_count offset, rand_state draws ahead) and known issues")
    ap.add_argument("--no-stage-tasks", action="store_true", help="information: worlds without the stage's own "
                    "tasks (implies nothing about strictness: combine with --tolerant to see the residuals classified)")
    ap.add_argument("--pure", action="store_true", help="also the information run with only the pad injected")
    ap.add_argument("--trace", action="append", default=[], help="reference trace name (default: M1, M1.1, Phase 7, "
                    "HOSTOBJ, DROWN, HUD, RINGS, SUPER, ITEMBOX)")
    a = ap.parse_args()
    keys = sorted(TARGETS) if a.target == "all" else [a.target]
    traces = a.trace or (M1 + M11 + P7 + HOSTOBJ + DROWN + HUD + RINGS + SUPER + ITEMBOX)
    extra = (["--tolerant"] if a.tolerant else []) + (["--no-stage-tasks"] if a.no_stage_tasks else []) + \
        (["--pure"] if a.pure else [])
    rc = 0
    ran = []
    for k in keys:
        exe = build(k, a.sadc)
        if a.run and runnable(k):
            if not a.gdi:
                raise SystemExit("--run needs --gdi or SA1_TEST_GDI")
            rc |= run(k, exe, a.sadc, a.gdi, a.out, extra, traces)
            ran.append(k)
    if "x86_64-windows" in ran and "x86-windows" in ran:
        a64, a32 = os.path.join(a.out, "x86_64-windows"), os.path.join(a.out, "x86-windows")
        names = sorted(set(os.listdir(a64)) | set(os.listdir(a32)))
        same = [n for n in names if os.path.exists(os.path.join(a64, n)) and os.path.exists(os.path.join(a32, n))
                and filecmp.cmp(os.path.join(a64, n), os.path.join(a32, n), shallow=False)]
        bad = [n for n in names if n not in same]
        print("V7: %d of %d output files byte-identical between x86_64-windows and x86-windows%s" % (
            len(same), len(names), ("; DIFFER: " + " ".join(bad)) if bad else ""))
        rc |= bool(bad) or not names
    sys.exit(1 if rc else 0)


main()
