#!/usr/bin/env python3
"""Read, print and compare reference-simulator traces (harness/src/sa1_trace.h),
versions 1 and 2, and the other outputs of `sa1h trace`.

    py -3.12 harness/tools/sa1trace.py info  TRACE.bin
    py -3.12 harness/tools/sa1trace.py dump  TRACE.bin [--from N] [--to N] [--fields pos,pspd,mode]
    py -3.12 harness/tools/sa1trace.py diff  REF.bin OTHER.bin [--ulps N] [--abs X] [--fields ...] [--all]
                                             [--raw [--heap-by-structure]] [--no-phys]
    py -3.12 harness/tools/sa1trace.py compat V1.bin V2.bin    # the v1 part of a v2 trace equals a v1 trace
    py -3.12 harness/tools/sa1trace.py script TRACE.bin        # input script that reproduces the trace
    py -3.12 harness/tools/sa1trace.py land  PREFIX [--col N]  # --dump-land output (.tri / .col)
    py -3.12 harness/tools/sa1trace.py landcheck PREFIX RAM    # the collision cache of a RAM image vs the dump
    py -3.12 harness/tools/sa1trace.py world A.txt B.txt       # first difference of two --dump-world files
    py -3.12 harness/tools/sa1trace.py p1    TRACE.bin|TRACE.p1.bin [--from N] [--to N] [--fields ...]
                                             # the player-1 / follower-AI sidecar (trace --pno1)
    py -3.12 harness/tools/sa1trace.py obj   TRACE.bin|TRACE.obj.bin [--from N] [--to N] [--all]
                                             # the host-object sidecar (trace --hostobj / --create-task)

Sidecars (harness/src/sa1_trace_ext.h): NAME.p1.bin next to NAME.bin holds player 1 and the follower
AI per step.  info reports it, dump --p1 and p1 print it, and diff compares the sidecars too when both
traces have one (or when given two sidecars): the AI fields and player 1's record with the same rules
as the main record (fields named p1.<field>).  A trace without a sidecar is compared as before.
NAME.obj.bin (round 5, trace --hostobj / --create-task) holds the host calls made before each step and
what the step gave (host-object events, the HURT / DEATH / CAPACITY world events, the held state, the
proxies); diff compares it field by field (floats with --ulps / --abs, the proxies' guest addresses left
out), and a reference that has one fails a diff against a trace that has none.  Its version 2 (round
0.9.2: --create-task .../SX,SY,SZ+ocp) adds each task's scale and OBJ_CONDITION slot after the version-1
header; version-1 files read as before (their tasks have no scale and no condition). Round 0.9.4:
+ocp=HEX sets another initial ssCondition (task flag 8, the word in the task's reserved[0]; 0x8001 without it).

`diff` is what a libsa1 test does: write its own per-step state in the same
format (or load this module and compare dicts), then compare field by field.
Floats are compared in ulps (default 0 = bit-exact) or with an absolute
tolerance; angles and integers exactly.  The decoded fields are compared by
default (v1 and, when both traces are v2, the v2 scalars: px_calls, px_rand,
px_frame_counter, px_rings, px_gravity, frame_counter, rings, gravity).
--raw also compares the raw blobs (twp/mwp/pwp/csts, the camera taskwk) and
XMTRX word by word; pointer words (known pointer fields, and union words that
hold a guest RAM address in both traces) are compared by physical address
(addr & 0x1FFFFFFF; --no-phys: exactly).  --heap-by-structure (v2 traces)
maps a pointer to "twp+0x20", "pwp+0x38", ... when it points into one of the
record's own blobs and to "heap" when it points anywhere else in the game heap,
so two runs whose heap blocks sit at different addresses compare equal when
the pointers mean the same thing.  Exit status 1 when a field diverges.
"""
import argparse
import os
import struct
import subprocess
import sys

HDR = struct.Struct("<8sIIII hh bBH IIII 3f 3I 64s")
assert HDR.size == 136
MAGIC = b"SA1TRC01"

# (name, struct code, count)
V1_FIELDS = [
    ("step", "I", 1), ("frame", "I", 1), ("sub", "I", 1), ("icount", "I", 1),
    ("stick_x", "h", 1), ("stick_y", "h", 1), ("buttons", "I", 1),
    ("pad_on", "I", 1), ("pad_press", "I", 1), ("analog_ang", "I", 1), ("analog_stroke", "f", 1),
    ("mode", "b", 1), ("smode", "b", 1), ("id", "b", 1), ("btimer", "b", 1),
    ("twp_flag", "h", 1), ("wtimer", "H", 1),
    ("ang", "I", 3), ("pos", "f", 3), ("mspd", "f", 3), ("macc", "f", 3), ("ang_aim", "I", 3),
    ("pspd", "f", 3), ("pacc", "f", 3), ("floor_normal", "f", 3), ("dotp", "f", 1),
    ("attr", "I", 1), ("last_attr", "I", 1),
    ("pwp_flag", "h", 1), ("jumptimer", "h", 1), ("nocontimer", "h", 1), ("waittimer", "h", 1),
    ("reqaction", "H", 1), ("action", "H", 1), ("lastaction", "H", 1), ("nextaction", "H", 1),
    ("nframe", "f", 1), ("equipment", "H", 1), ("item", "H", 1), ("land_count", "I", 1),
    ("rand_state", "I", 1), ("reserved", "I", 3),
    ("twp_raw", "64s", 1), ("mwp_raw", "64s", 1), ("pwp_raw", "512s", 1), ("csts_raw", "132s", 1),
]
V2_FIELDS = [
    ("px_exec", "I", 1), ("px_calls", "I", 1), ("px_rand", "I", 1), ("px_frame_counter", "I", 1),
    ("px_rings", "h", 1), ("px_pad", "H", 1), ("px_gravity", "f", 3), ("px_xmtrx", "I", 16),
    ("px_cam_twp", "I", 1), ("px_cam_raw", "64s", 1),
    ("frame_counter", "I", 1), ("rings", "h", 1), ("pad2", "H", 1), ("gravity", "f", 3),
    ("ptp_addr", "I", 1), ("twp_addr", "I", 1), ("mwp_addr", "I", 1), ("pwp_addr", "I", 1), ("csts_addr", "I", 1),
    ("reserved2", "I", 3),
]


def _struct(fields):
    return struct.Struct("<" + "".join(("%d%s" % (n, c)) if n > 1 else c for _, c, n in fields))


REC1 = _struct(V1_FIELDS)
REC2 = _struct(V1_FIELDS + V2_FIELDS)
assert REC1.size == 972, REC1.size
assert REC2.size == 1188, REC2.size
REC = REC2                       # compatibility: the newest record layout
REC_FIELDS = V1_FIELDS + V2_FIELDS
LAYOUT = {1: (REC1, V1_FIELDS), 2: (REC2, V1_FIELDS + V2_FIELDS)}

RAW = {"twp_raw", "mwp_raw", "pwp_raw", "csts_raw", "reserved", "px_cam_raw", "px_xmtrx", "reserved2", "px_pad",
       "pad2"}
ADDRS = {"px_exec", "px_cam_twp", "ptp_addr", "twp_addr", "mwp_addr", "pwp_addr", "csts_addr"}
NOT_STATE = {"step", "frame", "sub", "icount"}
ANGLES = {"ang", "ang_aim", "analog_ang"}
HEX = {"attr", "last_attr", "buttons", "pad_on", "pad_press", "rand_state", "px_rand", "px_exec", "px_cam_twp",
       "ptp_addr", "twp_addr", "mwp_addr", "pwp_addr", "csts_addr"}

# pointer words of the raw blobs (DC layouts: include/task.h, include/sa1/player.h, include/sa1/motion.h)
TWP_PTRS = {0x38: "cwp", 0x3C: "ewp"}
PTR_WORDS = {
    "twp_raw": TWP_PTRS,
    "px_cam_raw": TWP_PTRS,
    "mwp_raw": {0x00: "work.pwp"},
    "pwp_raw": {0x064: "cstsp", 0x068: "htp", 0x06C: "ttp", 0x070: "pfp", 0x074: "lclop", 0x078: "island_list",
                0x138: "mj.spdp", 0x13C: "mj.workp", 0x140: "mj.plactptr", 0x144: "mj.pljvptr",
                0x148: "mj.actwkptr"},
    "csts_raw": {},
}
# unions that hold a pointer for some tasks / characters: compared as pointers when both values are guest RAM addresses
UNION_WORDS = {
    "twp_raw": {0x08: "counter", 0x0C: "timer", 0x10: "value"},
    "px_cam_raw": {0x08: "counter", 0x0C: "timer", 0x10: "value"},
    "pwp_raw": {0x000: "work", 0x07C: "free.ptr[0]"},
}
# the game heap (B_8C7EE8E0, 0x90000 bytes + the end header; code_8C098998.c), physical
HEAP_LO, HEAP_HI = 0x0C7EE8E0, 0x0C7EE8E0 + 0x90010
# blobs of a v2 record: (label, address field, size)
STRUCTS = [("twp", "twp_addr", 0x40), ("mwp", "mwp_addr", 0x40), ("pwp", "pwp_addr", 0x200),
           ("csts", "csts_addr", 0x84), ("ptp", "ptp_addr", 0x30), ("cam", "px_cam_twp", 0x40)]


def version_of(path):
    with open(path, "rb") as f:
        h = HDR.unpack(f.read(HDR.size))
    if h[0] != MAGIC:
        raise SystemExit("%s: not an SA1 trace" % path)
    return h[1]


FLAG_DISP, FLAG_NO_OBJECTS, FLAG_ROUTED, FLAG_SCREEN = 1, 2, 4, 8


def flags_of(path):
    """the header flags of a trace (sa1_trace.h: 1 display pass, 2 no objects, 4 routed, 8 screen)"""
    with open(path, "rb") as f:
        h = HDR.unpack(f.read(HDR.size))
    if h[0] != MAGIC:
        raise SystemExit("%s: not an SA1 trace" % path)
    return h[9]


def load(path, raw_bytes=False):
    """-> (header dict, list of record dicts); records of a v1 trace lack the v2 keys.
    raw_bytes: also keep each record's bytes as rec['_bytes']"""
    data = open(path, "rb").read()
    h = HDR.unpack_from(data, 0)
    if h[0] != MAGIC:
        raise SystemExit("%s: not an SA1 trace" % path)
    hdr = dict(zip(["magic", "version", "header_size", "record_size", "count", "level", "act", "chr",
                    "steps_per_frame", "flags", "rom_crc32", "stage_crc32", "stage_size", "landtable"], h[:14]))
    hdr["start_pos"] = h[14:17]
    hdr["start_ang"] = h[17:20]
    hdr["input_name"] = h[20].split(b"\0")[0].decode("latin-1")
    if hdr["version"] not in LAYOUT:
        raise SystemExit("%s: trace version %d, this tool knows 1 and 2" % (path, hdr["version"]))
    st, fields = LAYOUT[hdr["version"]]
    if hdr["record_size"] != st.size:
        raise SystemExit("%s: version %d with record size %d, expected %d" % (path, hdr["version"],
                                                                              hdr["record_size"], st.size))
    recs = []
    off = hdr["header_size"]
    for i in range(hdr["count"]):
        v = st.unpack_from(data, off + i * st.size)
        r, k = {}, 0
        for name, c, n in fields:
            r[name] = v[k] if n == 1 else tuple(v[k:k + n])
            k += n
        if raw_bytes:
            r["_bytes"] = data[off + i * st.size: off + (i + 1) * st.size]
        recs.append(r)
    return hdr, recs


def f2u(x):
    return struct.unpack("<I", struct.pack("<f", x))[0]


def ulps(a, b):
    ia, ib = f2u(a), f2u(b)
    ia = ia if ia < 0x80000000 else 0x80000000 - ia
    ib = ib if ib < 0x80000000 else 0x80000000 - ib
    return abs(ia - ib)


def is_guest_ptr(v):
    """main RAM in any mirror (P0/P1/P2), like the harness's sh4_in_ram"""
    return (v & 0x1C000000) == 0x0C000000 and v < 0xC0000000


def canon_ptr(v, rec, phys=True, heap_struct=False):
    """comparison key of a guest pointer value found in record rec"""
    if v == 0:
        return 0
    p = v & 0x1FFFFFFF if phys else v
    if heap_struct and is_guest_ptr(v):
        q = v & 0x1FFFFFFF
        for label, field, size in STRUCTS:
            base = rec.get(field, 0)
            if base and is_guest_ptr(base) and (base & 0x1FFFFFFF) <= q < (base & 0x1FFFFFFF) + size:
                return "%s+0x%X" % (label, q - (base & 0x1FFFFFFF))
        if HEAP_LO <= q < HEAP_HI:
            return "heap"
    return p


def fmt(name, v):
    if isinstance(v, tuple):
        return "(" + " ".join(fmt(name, x) for x in v) + ")"
    if isinstance(v, float):
        return "%.6f" % v
    if isinstance(v, bytes):
        return v.hex()
    if name in ANGLES:
        return "%04X" % (v & 0xFFFF)
    if name in HEX:
        return "%X" % v
    return str(v)


# ---------------------------------------------------------------- sidecars (harness/src/sa1_trace_ext.h)

EXT_HDR = struct.Struct("<8sIIIII hh bBH 3I")
assert EXT_HDR.size == 48
EXT_MAGIC_P1 = b"SA1XP101"
EXT_MAGIC_OBJ = b"SA1XOB01"     # host objects (round 5): sa1_trace_obj_header / sa1_trace_obj_rec
P1_HEAD_FIELDS = [
    ("step", "I", 1), ("flags", "I", 1), ("ai_tp", "I", 1), ("ai_twp", "I", 1),
    ("ai_mode", "b", 1), ("ai_smode", "b", 1), ("ai_id", "b", 1), ("ai_btimer", "b", 1),
    ("ai_flag", "h", 1), ("ai_wtimer", "H", 1), ("ai_counter", "I", 1), ("ai_timer", "I", 1),
    ("ai_twp_raw", "64s", 1), ("ai_calls", "I", 1), ("ai_rand", "I", 1), ("ai_xmtrx", "I", 16),
    ("ai_cam_twp", "I", 1), ("ai_cam_raw", "64s", 1), ("ai_on", "I", 1), ("ai_press", "I", 1),
    ("ai_analog_ang", "I", 1), ("ai_analog_stroke", "f", 1), ("first_pno", "I", 1), ("first_rand", "I", 1),
    ("p1_reserved", "I", 3),
]
P1_HEAD = _struct(P1_HEAD_FIELDS)
assert P1_HEAD.size == 272, P1_HEAD.size
P1_REC_SIZE = P1_HEAD.size + REC2.size          # 1460
P1F_PLAYER1, P1F_AI_TASK, P1F_AI_ENTERED, P1F_P1_ENTERED = 1, 2, 4, 8
P1_RAW = {"ai_twp_raw", "ai_cam_raw", "ai_xmtrx", "p1_reserved"}
P1_ADDRS = {"ai_tp", "ai_twp", "ai_cam_twp"}
P1_NOT_STATE = {"step"}
HEX |= {"flags", "ai_on", "ai_press", "ai_rand", "first_rand", "ai_tp", "ai_twp", "ai_cam_twp"}
ANGLES |= {"ai_analog_ang"}


# ---- the host-object sidecar NAME.obj.bin (harness/src/sa1_trace_ext.h sa1_trace_obj_*) ----
OBJ_SHAPE = "II3f3ff"
OBJ_OP = struct.Struct("<IIIIiiII3ffff" + OBJ_SHAPE * 4 + "2i3fI")
assert OBJ_OP.size == 224, OBJ_OP.size
OBJ_EV = struct.Struct("<9I2I3f3ff")
assert OBJ_EV.size == 72
OBJ_ST = struct.Struct("<IIHHHH3fI")
assert OBJ_ST.size == 32
OBJ_HEAD = struct.Struct("<4I4I2I3f3ffI")
assert OBJ_HEAD.size == 72
OBJ_MAX_OPS, OBJ_MAX_EVENTS, OBJ_MAX_OBJS = 4, 8, 6
OBJ_REC_SIZE = OBJ_HEAD.size + OBJ_MAX_OPS * OBJ_OP.size + OBJ_MAX_EVENTS * OBJ_EV.size + OBJ_MAX_OBJS * OBJ_ST.size
assert OBJ_REC_SIZE == 1736
OBJ_TASK = struct.Struct("<4I3fI")
OBJ_HDR_TAIL = struct.Struct("<f9fI")
OBJ_HDR_SIZE = 280
# version 2 (round 0.9.2: --create-task /SX,SY,SZ and +ocp): sa1_trace_obj_task_ext per task after the v1 header
OBJ_TASK_EXT = struct.Struct("<I3fIf2I")
assert OBJ_TASK_EXT.size == 32
OBJ_HDR_V2_SIZE = OBJ_HDR_SIZE + 4 * OBJ_TASK_EXT.size + 16
assert OBJ_HDR_V2_SIZE == 424
OBJ_T_SCL, OBJ_T_OCP, OBJ_T_LATE = 1, 2, 4
OBJ_T_OCPW = 8                                  # round 0.9.4: +ocp=HEX, the initial ssCondition in reserved[0]
OBJ_REC_EXT = struct.Struct("<4I4I")             # sa1_trace_obj_rec_ext after each version-2 record
OBJ_REC_V2_SIZE = OBJ_REC_SIZE + OBJ_REC_EXT.size
assert OBJ_REC_V2_SIZE == 1768
OBJ_OPS = {1: "add", 2: "update", 3: "move", 4: "remove", 5: "release", 6: "damage", 7: "option", 8: "rings",
           9: "mask", 10: "config"}
OBJ_EVENTS = {1: "HIT", 2: "LOCKED", 3: "TARGETED", 4: "UNLOCKED", 5: "TOUCH", 6: "LIFTED", 7: "RELEASED",
              0x100 | 14: "HURT", 0x100 | 6: "DEATH", 0x100 | 11: "CAPACITY"}
OBJ_ATTACKS = ["OTHER", "SPIN", "SPIN_DASH", "HOMING", "LIGHT_SPEED_ATTACK", "TAIL", "TAILS_FLIGHT", "PUNCH",
               "MAX_HEAT", "DIG", "HAMMER", "LASER", "LASER_BLAST", "LURE", "BIG_ATTACK", "SPARKLE", "INVINCIBLE",
               "SUPER"]
OBJ_KINDS = {1: "ENEMY", 2: "TARGET", 3: "BREAKABLE", 4: "HAZARD", 5: "HOLDABLE", 6: "BALLOON"}
OBJ_RELEASE = {1: "THROW", 2: "THROW_AIMED", 3: "PUT_DOWN", 4: "DROP", 5: "FORCED"}


def is_obj(path):
    return os.path.exists(path) and ext_magic(path) == EXT_MAGIC_OBJ


def load_obj(path):
    """the host-object sidecar -> (header dict, records); each record a dict with 'ops', 'events', 'objs' lists
    (only the used entries) and the scalar fields"""
    data = open(path, "rb").read()
    h = EXT_HDR.unpack_from(data, 0)
    if h[0] != EXT_MAGIC_OBJ:
        raise SystemExit("%s: not a host-object sidecar (magic %r)" % (path, h[0]))
    hdr = dict(zip(["magic", "version", "header_size", "record_size", "count", "flags", "level", "act", "chr", "pno"],
                   h[:10]))
    if hdr["record_size"] < OBJ_REC_SIZE or hdr["header_size"] < OBJ_HDR_SIZE:
        raise SystemExit("%s: record / header size %d / %d, this tool needs %d / %d" % (
            path, hdr["record_size"], hdr["header_size"], OBJ_REC_SIZE, OBJ_HDR_SIZE))
    t = OBJ_HDR_TAIL.unpack_from(data, EXT_HDR.size)
    hdr["coords_scale"], hdr["coords_axis"], hdr["ntasks"] = t[0], t[1:10], t[10]
    hdr["tasks"] = []
    v2 = hdr["version"] >= 2 and hdr["header_size"] >= OBJ_HDR_V2_SIZE
    for k in range(min(hdr["ntasks"], 4)):
        v = OBJ_TASK.unpack_from(data, EXT_HDR.size + OBJ_HDR_TAIL.size + k * OBJ_TASK.size)
        t = {"exec": v[0], "im": v[1], "list": v[2], "has_pos": v[3], "pos": v[4:7], "angy": v[7]}
        # version 2: the task's scale and OBJ_CONDITION (version 1: none, as written then)
        x = OBJ_TASK_EXT.unpack_from(data, OBJ_HDR_SIZE + k * OBJ_TASK_EXT.size) if v2 else (0, 0.0, 0.0, 0.0, 0, 0.0, 0, 0)
        t.update({"ext_flags": x[0], "scl": x[1:4], "ocp_slot": x[4], "ocp_range": x[5],
                  "ocp_init": (x[6] & 0xFFFF) if x[0] & OBJ_T_OCPW else 0x8001})
        hdr["tasks"].append(t)
    hdr["script"] = data[EXT_HDR.size + OBJ_HDR_TAIL.size + 4 * OBJ_TASK.size:][:48].split(b"\0")[0].decode(
        "latin-1")
    recs = []
    for i in range(hdr["count"]):
        off = hdr["header_size"] + i * hdr["record_size"]
        v = OBJ_HEAD.unpack_from(data, off)
        r = {"step": v[0], "nops": v[1], "nevents": v[2], "nobjs": v[3], "proxies": v[4], "created": v[5],
             "destroyed": v[6], "refused": v[7], "held_object": v[8], "held_phase": v[9], "held_pos": v[10:13],
             "held_hand": v[13:16], "held_yaw": v[16], "events_lost": v[17]}
        off += OBJ_HEAD.size
        r["ops"] = []
        for k in range(OBJ_MAX_OPS):
            o = OBJ_OP.unpack_from(data, off + k * OBJ_OP.size)
            if k < r["nops"]:
                shapes = [o[16 + 9 * j:16 + 9 * j + 9] for j in range(4)]
                r["ops"].append({"op": o[0], "handle": o[1], "kind": o[2], "flags": o[3], "armor": o[4],
                                 "touch": o[5], "cooldown": o[6], "nshapes": o[7], "pos": o[8:11], "yaw": o[11],
                                 "launch_speed": o[12], "grab_reach": o[13], "shapes": shapes[:o[7]],
                                 "arg": o[52:54], "src": o[54:57]})
        off += OBJ_MAX_OPS * OBJ_OP.size
        r["events"] = []
        for k in range(OBJ_MAX_EVENTS):
            e = OBJ_EV.unpack_from(data, off + k * OBJ_EV.size)
            if k < r["nevents"]:
                r["events"].append(dict(zip(["type", "handle", "attack", "level", "damage", "shape", "flags",
                                             "reason", "target", "arg", "bank"], e[:11]),
                                        pos=e[11:14], dir=e[14:17], speed=e[17]))
        off += OBJ_MAX_EVENTS * OBJ_EV.size
        r["objs"] = []
        for k in range(OBJ_MAX_OBJS):
            st = OBJ_ST.unpack_from(data, off + k * OBJ_ST.size)
            if k < r["nobjs"]:
                r["objs"].append({"handle": st[0], "twp": st[1], "tflag": st[2], "cflag": st[3], "eflag": st[4],
                                  "mode": st[5], "pos": st[6:9], "angy": st[9]})
        off += OBJ_MAX_OBJS * OBJ_ST.size
        # version 2: the +ocp tasks' OBJ_CONDITION word 0 after the step (scCount | scUserFlag << 8 | ssCondition << 16)
        if v2 and hdr["record_size"] >= OBJ_REC_V2_SIZE:
            r["ocp"] = OBJ_REC_EXT.unpack_from(data, off)[:4]
        recs.append(r)
    return hdr, recs


def obj_event_text(e):
    name = OBJ_EVENTS.get(e["type"], "type %X" % e["type"])
    if e["type"] == (0x100 | 6):
        return "%s obj %d cause %s" % (name, e["handle"], {0: "OTHER", 1: "HURT", 2: "HOST", 3: "DROWN", 4: "ZONE"}.get(
            e["arg"], e["arg"]))
    if e["type"] == (0x100 | 11):
        return "%s id %d objects refused %d" % (name, e["handle"], e["arg"])
    if e["type"] & 0x100:
        hf = e["arg"] >> 16
        names = [n for b, n in ((1, "BARRIER"), (2, "ABSORBED"), (4, "FATAL"), (8, "HOST"), (0x10, "SURFACE"),
                                (0x20, "IGNORED")) if hf & b]
        return "%s obj %d rings lost %d %s, rings left %d" % (
            name, e["handle"], e["arg"] & 0xFFFF, "|".join(names) or "-", struct.unpack("<h", struct.pack(
                "<H", e["bank"] & 0xFFFF))[0])
    t = "%s obj %d" % (name, e["handle"])
    if e["type"] == 1:
        t += " %s level %d damage %d shape %d flags %X" % (
            OBJ_ATTACKS[e["attack"]] if e["attack"] < len(OBJ_ATTACKS) else e["attack"], e["level"], e["damage"],
            e["shape"], e["flags"])
    if e["type"] == 7:
        t += " %s target %d" % (OBJ_RELEASE.get(e["reason"], e["reason"]), e["target"])
    if e["type"] in (1, 7):
        t += " pos (%.4g %.4g %.4g) speed %.4g" % (e["pos"] + (e["speed"],))
    return t


def obj_op_text(o):
    name = OBJ_OPS.get(o["op"], "op %d" % o["op"])
    if o["op"] in (1, 2):
        return "%s %d %s flags %X pos (%g %g %g) %d shape(s)" % (name, o["handle"], OBJ_KINDS.get(o["kind"], o["kind"]),
                                                               o["flags"], *o["pos"], o["nshapes"])
    if o["op"] == 3:
        return "move %d (%g %g %g) yaw %g" % (o["handle"], *o["pos"], o["yaw"])
    return "%s %d args %d %d" % (name, o["handle"], o["arg"][0], o["arg"][1])


def obj_info(path):
    h, recs = load_obj(path)
    ev = {}
    for r in recs:
        for e in r["events"]:
            ev[OBJ_EVENTS.get(e["type"], e["type"])] = ev.get(OBJ_EVENTS.get(e["type"], e["type"]), 0) + 1
    print("host-object sidecar %s: version %d, %d records, script %s, %d host call(s), %d task(s)%s; events: %s; "
          "most proxies %d" % (os.path.basename(path), h["version"], h["count"], h["script"] or "-",
                               sum(r["nops"] for r in recs), h["ntasks"],
                               "".join(" (%08X im %d list %d%s%s%s)" % (
                                   t["exec"], t["im"], t["list"],
                                   " scl (%g %g %g)" % tuple(t["scl"]) if t["ext_flags"] & OBJ_T_SCL else "",
                                   " ocp %d%s" % (t["ocp_slot"], "=%04X" % t["ocp_init"]
                                                  if t["ext_flags"] & OBJ_T_OCPW else "")
                                   if t["ext_flags"] & OBJ_T_OCP else "",
                                   " late" if t["ext_flags"] & OBJ_T_LATE else "")
                                   for t in h["tasks"]),
                               ", ".join("%s %d" % kv for kv in ev.items()) or "none",
                               max([r["proxies"] for r in recs] or [0])))


def cmd_obj(a):
    path = a.trace if is_obj(a.trace) else sidecar_path(a.trace, "obj")
    if not os.path.exists(path):
        print("%s: no host-object sidecar (trace --hostobj / --create-task writes it)" % path)
        return 1
    h, recs = load_obj(path)
    for r in recs:
        if r["step"] < a.start or (a.to is not None and r["step"] > a.to):
            continue
        for o in r["ops"]:
            print("%5d call  %s" % (r["step"], obj_op_text(o)))
        for e in r["events"]:
            print("%5d event %s" % (r["step"], obj_event_text(e)))
        if getattr(a, "all", False):
            for s in r["objs"]:
                print("%5d proxy %d twp %08X mode %d flag %04X colli %04X enemy %04X pos (%.4f %.4f %.4f) ang %04X" % (
                    r["step"], s["handle"], s["twp"], s["mode"], s["tflag"], s["cflag"], s["eflag"], *s["pos"],
                    s["angy"]))
            if r["held_object"]:
                print("%5d held  %d phase %d pos (%.4f %.4f %.4f) hand (%.4f %.4f %.4f) yaw %.4f" % (
                    r["step"], r["held_object"], r["held_phase"], *r["held_pos"], *r["held_hand"], r["held_yaw"]))
            for k, w in enumerate(r.get("ocp", ())):
                if k < len(h["tasks"]) and h["tasks"][k]["ext_flags"] & OBJ_T_OCP:
                    print("%5d ocp   task %d slot %d: scCount %d scUserFlag %02X ssCondition %04X" % (
                        r["step"], 0x80 + k, h["tasks"][k]["ocp_slot"], w & 0xFF, (w >> 8) & 0xFF, w >> 16))
        elif "ocp" in r and (not recs or r is recs[0] or r["ocp"] != recs[r["step"] - 1].get("ocp")):
            # without --all: the OBJ_CONDITION words when they change (version 2)
            for k, w in enumerate(r["ocp"]):
                if k < len(h["tasks"]) and h["tasks"][k]["ext_flags"] & OBJ_T_OCP:
                    print("%5d ocp   task %d slot %d: scCount %d scUserFlag %02X ssCondition %04X" % (
                        r["step"], 0x80 + k, h["tasks"][k]["ocp_slot"], w & 0xFF, (w >> 8) & 0xFF, w >> 16))
    return 0


def _obj_flat(r):
    """(name, value) pairs of a record for diff: everything but the proxies' guest addresses"""
    out = [(k, r[k]) for k in ("nops", "nevents", "nobjs", "proxies", "created", "destroyed", "refused", "held_object",
                               "held_phase", "held_pos", "held_hand", "held_yaw", "events_lost")]
    for k, o in enumerate(r["ops"]):
        out += [("op%d.%s" % (k, n), v) for n, v in o.items()]
    for k, e in enumerate(r["events"]):
        out += [("ev%d.%s" % (k, n), v) for n, v in e.items()]
    for k, s in enumerate(r["objs"]):
        out += [("obj%d.%s" % (k, n), v) for n, v in s.items() if n != "twp"]
    if "ocp" in r:          # version 2: the tasks' OBJ_CONDITION words
        out += [("ocp%d" % k, v) for k, v in enumerate(r["ocp"])]
    return out


def diff_obj(ref, other, a):
    """compare two host-object sidecars; 0 when identical within tolerance"""
    h1, r1 = load_obj(ref)
    h2, r2 = load_obj(other)
    first = {}
    if h1["tasks"] != h2["tasks"]:
        first["tasks"] = (0, str(h1["tasks"]), str(h2["tasks"]))
    n = min(len(r1), len(r2))
    for i in range(n):
        x, y = dict(_obj_flat(r1[i])), dict(_obj_flat(r2[i]))
        for f in sorted(set(x) | set(y)):
            va, vb = x.get(f, "-"), y.get(f, "-")
            ta = va if isinstance(va, (tuple, list)) else (va,)
            tb = vb if isinstance(vb, (tuple, list)) else (vb,)
            bad = len(ta) != len(tb)
            for p, q in zip(ta, tb):
                if isinstance(p, float) and isinstance(q, float):
                    if p != p and q != q:
                        continue
                    if (a.abs is not None and abs(p - q) > a.abs) or (a.abs is None and ulps(p, q) > a.ulps):
                        bad = True
                elif p != q:
                    bad = True
            if bad:
                key = f.split(".", 1)[-1] if "." in f else f
                if key not in first:
                    first[key] = (i, "%s=%s" % (f, va), "%s=%s" % (f, vb))
        if first and not a.all and i > min(v[0] for v in first.values()) + 60:
            break
    if len(r1) != len(r2):
        print("note: host-object sidecars: %d vs %d records" % (len(r1), len(r2)))
    if not first:
        print("host-object sidecar identical within tolerance over %d steps" % n)
        return 0
    print("host-object sidecar: diverging fields (first step, reference, other):")
    for f, (i, va, vb) in sorted(first.items(), key=lambda kv: kv[1][0]):
        print("  %-16s step %5d  %s  vs  %s" % (f, i, va, vb))
    return 1


def sidecar_path(trace, kind="p1"):
    """NAME.bin -> NAME.<kind>.bin"""
    base = trace[:-4] if trace.endswith(".bin") else trace
    return base + "." + kind + ".bin"


def ext_magic(path):
    with open(path, "rb") as f:
        return f.read(8)


def is_p1(path):
    return os.path.exists(path) and ext_magic(path) == EXT_MAGIC_P1


def load_p1(path):
    """the player-1 sidecar -> (header dict, records); each record: the AI fields and rec['p1'], player 1
    as a version-2 trace record dict"""
    data = open(path, "rb").read()
    h = EXT_HDR.unpack_from(data, 0)
    if h[0] != EXT_MAGIC_P1:
        raise SystemExit("%s: not a player-1 sidecar (magic %r)" % (path, h[0]))
    hdr = dict(zip(["magic", "version", "header_size", "record_size", "count", "flags", "level", "act", "chr", "pno"],
                   h[:10]))
    if hdr["record_size"] < P1_REC_SIZE:
        raise SystemExit("%s: record size %d, this tool needs %d" % (path, hdr["record_size"], P1_REC_SIZE))
    recs = []
    for i in range(hdr["count"]):
        off = hdr["header_size"] + i * hdr["record_size"]
        v = P1_HEAD.unpack_from(data, off)
        r, k = {}, 0
        for name, c, n in P1_HEAD_FIELDS:
            r[name] = v[k] if n == 1 else tuple(v[k:k + n])
            k += n
        v = REC2.unpack_from(data, off + P1_HEAD.size)
        p, k = {}, 0
        for name, c, n in V1_FIELDS + V2_FIELDS:
            p[name] = v[k] if n == 1 else tuple(v[k:k + n])
            k += n
        r["p1"] = p
        recs.append(r)
    return hdr, recs


def p1_get(r, f):
    """field f of a sidecar record: 'ai_mode', or 'p1.pos' for player 1's record"""
    if f.startswith("p1."):
        return r["p1"].get(f[3:], "-")
    return r.get(f, "-")


def cmd_p1(a):
    path = a.trace if is_p1(a.trace) else sidecar_path(a.trace)
    if not os.path.exists(path):
        print("%s: no player-1 sidecar (trace --pno1 writes it)" % path)
        return 1
    h, recs = load_p1(path)
    fields = a.fields.split(",") if a.fields else ["flags", "ai_mode", "ai_smode", "ai_counter", "ai_on", "ai_press",
                                                   "ai_analog_stroke", "first_pno", "p1.mode", "p1.action", "p1.pos"]
    for r in recs:
        if r["step"] < a.start or (a.to is not None and r["step"] > a.to):
            continue
        print("%5d " % r["step"] + " ".join("%s=%s" % (f, fmt(f.split(".")[-1], p1_get(r, f))) for f in fields))
    return 0


def p1_info(path):
    h, recs = load_p1(path)
    ai = [r for r in recs if r["flags"] & P1F_AI_TASK]
    modes = {}
    for r in ai:
        modes[r["ai_mode"]] = modes.get(r["ai_mode"], 0) + 1
    resp = [r["step"] for i, r in enumerate(recs) if i and r["ai_mode"] == 2 and recs[i - 1]["ai_mode"] != 2
            and r["flags"] & P1F_AI_TASK]
    print("player-1 sidecar %s: version %d, %d records, AI task in %d steps (modes %s), player 1 in %d, "
          "respawn requests at steps %s, highest off-screen counter %d" % (
              os.path.basename(path), h["version"], h["count"], len(ai),
              ", ".join("%d x%d" % kv for kv in sorted(modes.items())) or "-",
              sum(1 for r in recs if r["flags"] & P1F_PLAYER1), resp or "none",
              max([r["ai_counter"] for i, r in enumerate(recs) if i and r["flags"] & P1F_AI_TASK
                   and recs[i - 1]["ai_mode"] != 0] or [0])))


def diff_p1(ref, other, a):
    """compare two player-1 sidecars; returns 0 when identical within tolerance"""
    h1, r1 = load_p1(ref)
    h2, r2 = load_p1(other)
    head = [n for n, _, _ in P1_HEAD_FIELDS if n not in P1_RAW and n not in P1_NOT_STATE and n not in P1_ADDRS]
    sub = ["p1." + n for n in decoded_fields({"version": 2}, {"version": 2})]
    names = a.fields.split(",") if a.fields else head + sub
    raws = (["ai_twp_raw", "ai_cam_raw", "ai_xmtrx"] + ["p1." + x for x in
                                                         ("twp_raw", "mwp_raw", "pwp_raw", "csts_raw", "px_cam_raw",
                                                          "px_xmtrx")]) if a.raw else []
    phys = not a.no_phys
    first = {}
    n = min(len(r1), len(r2))
    for i in range(n):
        x, y = r1[i], r2[i]
        for f in names:
            va, vb = p1_get(x, f), p1_get(y, f)
            base = f.split(".")[-1]
            ta = va if isinstance(va, tuple) else (va,)
            tb = vb if isinstance(vb, tuple) else (vb,)
            bad = False
            for p, q in zip(ta, tb):
                if isinstance(p, float):
                    if p != p and q != q:
                        continue
                    if (a.abs is not None and abs(p - q) > a.abs) or (a.abs is None and ulps(p, q) > a.ulps):
                        bad = True
                elif base in ANGLES:
                    bad |= bool((p - q) & 0xFFFF)
                elif p != q:
                    bad = True
            if bad and f not in first:
                first[f] = (i, fmt(base, va), fmt(base, vb))
        for f in raws:
            if f in first:
                continue
            if f.startswith("p1."):
                d = raw_word_diffs(x["p1"], y["p1"], f[3:], phys, a.heap_by_structure)
            elif f == "ai_xmtrx":
                d = [(4 * k, "XF%d" % k, "%08X" % p, "%08X" % q) for k, (p, q) in enumerate(zip(x[f], y[f])) if p != q]
            else:
                k = "twp_raw" if f == "ai_twp_raw" else "px_cam_raw"      # both taskwk layouts
                d = raw_word_diffs({k: x[f]}, {k: y[f]}, k, phys, False)
            if d:
                off, nm, p, q = d[0]
                first[f] = (i, "+0x%03X%s %s" % (off, (" " + nm) if nm else "", p), q)
        if first and not a.all and i > min(v[0] for v in first.values()) + 60:
            break
    if len(r1) != len(r2):
        print("note: player-1 sidecars: %d vs %d records" % (len(r1), len(r2)))
    if not first:
        print("player-1 sidecar identical within tolerance over %d steps" % n)
        return 0
    print("player-1 sidecar: diverging fields (first step, reference, other):")
    for f, (i, va, vb) in sorted(first.items(), key=lambda kv: kv[1][0]):
        print("  %-20s step %5d  %s  vs  %s" % (f, i, va, vb))
    return 1


def cmd_info(a):
    if is_p1(a.trace):
        p1_info(a.trace)
        return 0
    if is_obj(a.trace):
        obj_info(a.trace)
        return 0
    h, recs = load(a.trace)
    for k, v in h.items():
        if k == "magic":
            continue
        print("%-16s %s" % (k, ("%08X" % v) if k in ("rom_crc32", "stage_crc32", "landtable") else v))
    if recs:
        last = recs[-1]
        print("last step %d: mode %d action %d pos %s" % (last["step"], last["mode"], last["action"],
                                                          fmt("pos", last["pos"])))
        if h["version"] >= 2:
            miss = sum(1 for r in recs if r["px_calls"] == 0)
            execs = sorted({r["px_exec"] for r in recs if r["px_calls"]})
            print("player exec entries: %s; steps without one: %d" % (
                ", ".join("%08X" % e for e in execs) or "none", miss))
    if is_p1(sidecar_path(a.trace)):
        p1_info(sidecar_path(a.trace))
    if is_obj(sidecar_path(a.trace, "obj")):
        obj_info(sidecar_path(a.trace, "obj"))


def cmd_dump(a):
    if getattr(a, "p1", False) or is_p1(a.trace):
        return cmd_p1(a)
    if getattr(a, "obj", False) or is_obj(a.trace):
        return cmd_obj(a)
    h, recs = load(a.trace)
    fields = a.fields.split(",") if a.fields else ["stick_x", "stick_y", "buttons", "mode", "action", "pos",
                                                   "pspd", "ang", "attr"]
    for r in recs:
        if r["step"] < a.start or (a.to is not None and r["step"] > a.to):
            continue
        print("%5d " % r["step"] + " ".join("%s=%s" % (f, fmt(f, r.get(f, "-"))) for f in fields))


def decoded_fields(h1, h2):
    """the fields diff compares by default for these two headers"""
    both2 = h1["version"] >= 2 and h2["version"] >= 2
    names = [n for n, _, _ in (V1_FIELDS + (V2_FIELDS if both2 else []))
             if n not in RAW and n not in NOT_STATE and n not in ADDRS]
    return names


def raw_word_diffs(x, y, f, phys, heap_struct):
    """differences of blob f between records x and y: list of (offset, name, ref value, other value)"""
    out = []
    if f == "px_xmtrx":
        for k, (p, q) in enumerate(zip(x[f], y[f])):
            if p != q:
                out.append((4 * k, "XF%d" % k, "%08X" % p, "%08X" % q))
        return out
    bx, by = x[f], y[f]
    ptrs, unions = PTR_WORDS.get(f, {}), UNION_WORDS.get(f, {})
    for off in range(0, len(bx), 4):
        p, q = struct.unpack_from("<I", bx, off)[0], struct.unpack_from("<I", by, off)[0]
        if p == q:
            continue
        if off in ptrs or (off in unions and is_guest_ptr(p) and is_guest_ptr(q)):
            if canon_ptr(p, x, phys, heap_struct) == canon_ptr(q, y, phys, heap_struct):
                continue
            name = ptrs.get(off) or unions.get(off)
        else:
            name = unions.get(off, "")
        out.append((off, name, "%08X" % p, "%08X" % q))
    return out


def cmd_diff(a):
    if is_p1(a.ref) or is_p1(a.other):
        return diff_p1(a.ref, a.other, a)
    if is_obj(a.ref) or is_obj(a.other):
        return diff_obj(a.ref, a.other, a)
    rc = cmd_diff_main(a)
    s1, s2 = sidecar_path(a.ref), sidecar_path(a.other)
    if is_p1(s1) and is_p1(s2) and not a.fields:
        rc |= diff_p1(s1, s2, a)
    elif is_p1(s1) != is_p1(s2):
        print("note: only %s has a player-1 sidecar (not compared)" % (s1 if is_p1(s1) else s2))
    o1, o2 = sidecar_path(a.ref, "obj"), sidecar_path(a.other, "obj")
    if is_obj(o1) and is_obj(o2) and not a.fields:
        rc |= diff_obj(o1, o2, a)
    elif is_obj(o1) and not is_obj(o2):
        print("the reference has a host-object sidecar, %s has none" % a.other)
        rc |= 1
    elif is_obj(o2) and not is_obj(o1):
        print("note: only %s has a host-object sidecar (not compared)" % o2)
    return rc


def cmd_diff_main(a):
    h1, r1 = load(a.ref)
    h2, r2 = load(a.other)
    for k in ("level", "act", "chr"):
        if h1[k] != h2[k]:
            print("warning: %s differs (%s vs %s)" % (k, h1[k], h2[k]))
    if h1["stage_crc32"] != h2["stage_crc32"]:
        print("warning: different stage image (CRC %08X vs %08X)" % (h1["stage_crc32"], h2["stage_crc32"]))
    if h1["version"] != h2["version"]:
        print("note: version %d vs %d: the fields both have are compared" % (h1["version"], h2["version"]))
    names = a.fields.split(",") if a.fields else decoded_fields(h1, h2)
    raws = []
    if a.raw:
        raws = ["twp_raw", "mwp_raw", "pwp_raw", "csts_raw"]
        if h1["version"] >= 2 and h2["version"] >= 2:
            raws += ["px_cam_raw", "px_xmtrx"]
            if not a.fields:
                names += [n for n, _, _ in V2_FIELDS if n in ADDRS]     # blob addresses, as pointers
    phys = not a.no_phys
    if a.heap_by_structure and (h1["version"] < 2 or h2["version"] < 2):
        print("note: --heap-by-structure needs the blob addresses of v2 traces; only heap ranges are used")
    first, worst = {}, {}
    n = min(len(r1), len(r2))
    for i in range(n):
        x, y = r1[i], r2[i]
        for f in names:
            if f not in x or f not in y:
                continue
            va, vb = x[f], y[f]
            ta = va if isinstance(va, tuple) else (va,)
            tb = vb if isinstance(vb, tuple) else (vb,)
            bad, err = False, 0.0
            for p, q in zip(ta, tb):
                if isinstance(p, float):
                    if p != p and q != q:
                        continue
                    e = abs(p - q)
                    u = ulps(p, q)
                    err = max(err, e)
                    if (a.abs is not None and e > a.abs) or (a.abs is None and u > a.ulps):
                        bad = True
                elif f in ANGLES:
                    if (p - q) & 0xFFFF:
                        bad = True
                elif isinstance(p, bytes):
                    if p != q:
                        bad = True
                elif f in ADDRS:
                    if canon_ptr(p, x, phys, a.heap_by_structure) != canon_ptr(q, y, phys, a.heap_by_structure):
                        bad = True
                elif p != q:
                    bad = True
            worst[f] = max(worst.get(f, 0.0), err)
            if bad and f not in first:
                first[f] = (i, fmt(f, va), fmt(f, vb))
        for f in raws:
            if f in first:
                continue
            d = raw_word_diffs(x, y, f, phys, a.heap_by_structure)
            if d:
                off, nm, p, q = d[0]
                first[f] = (i, "+0x%03X%s %s" % (off, (" " + nm) if nm else "", p), q + (
                    "  (%d words differ)" % len(d) if len(d) > 1 else ""))
        if first and not a.all and i > min(v[0] for v in first.values()) + 60:
            break
    if len(r1) != len(r2):
        print("note: %d vs %d records" % (len(r1), len(r2)))
    if not first:
        print("identical within tolerance over %d steps (%s%s)" % (
            n, ("abs %g" % a.abs) if a.abs is not None else ("%d ulps" % a.ulps),
            ", raw blobs%s" % (" by structure" if a.heap_by_structure else (" (pointers physical)" if phys else ""))
            if a.raw else ""))
        return 0
    print("diverging fields (first step, reference, other):")
    for f, (i, va, vb) in sorted(first.items(), key=lambda kv: kv[1][0]):
        extra = "   (max abs err %g)" % worst[f] if worst.get(f) else ""
        print("  %-16s step %5d  %s  vs  %s%s" % (f, i, va, vb, extra))
    return 1


def compat(v1path, v2path):
    """the v1 part (offsets 0..971 of every record, icount aside) of trace v2path equals trace v1path;
    returns (ok, message)"""
    h1, r1 = load(v1path, raw_bytes=True)
    h2, r2 = load(v2path, raw_bytes=True)
    for k in ("level", "act", "chr", "steps_per_frame", "flags", "rom_crc32", "stage_crc32", "landtable",
              "start_pos", "start_ang", "count"):
        x, y = h1[k], h2[k]
        if k == "flags":
            # bit 2 = routed: a routed run may match a pure reference; bit 3 = screen (2026-10-20): the v1
            # recordings predate it, a script the screen does not change still matches its v1 file
            x, y = x & ~12, y & ~12
        if x != y:
            return False, "header %s differs: %s vs %s" % (k, h1[k], h2[k])
    dec = [n for n, _, _ in V1_FIELDS if n not in RAW and n != "icount"]
    raw_same = True
    for i, (x, y) in enumerate(zip(r1, r2)):
        bx, by = x["_bytes"][:REC1.size], y["_bytes"][:REC1.size]
        bx, by = bx[:12] + bx[16:], by[:12] + by[16:]
        if bx == by:
            continue
        for f in dec:
            if x[f] != y[f] and not (isinstance(x[f], float) and x[f] != x[f] and y[f] != y[f]):
                return False, "step %d: decoded field %s differs: %s vs %s" % (i, f, fmt(f, x[f]), fmt(f, y[f]))
        raw_same = False
    return True, "v1 decoded fields byte-identical over %d records, raw blobs %s" % (
        len(r1), "identical too" if raw_same else "differ")


def cmd_compat(a):
    ok, msg = compat(a.v1, a.v2)
    print(("ok: " if ok else "DIFFERS: ") + msg)
    return 0 if ok else 1


def cmd_script(a):
    h, recs = load(a.trace)
    segs = []
    for r in recs:
        key = (r["stick_x"], r["stick_y"], r["buttons"])
        if segs and segs[-1][1] == key:
            segs[-1][0] += 1
        else:
            segs.append([1, key])
    names = {0x4: "A", 0x2: "B", 0x1: "C", 0x8: "START", 0x400: "X", 0x200: "Y", 0x100: "Z", 0x800: "D",
             0x10: "UP", 0x20: "DOWN", 0x40: "LEFT", 0x80: "RIGHT", 0x10000: "L", 0x20000: "R"}
    print("# input of %s (level %d act %d chr %d)" % (a.trace, h["level"], h["act"], h["chr"]))
    for n, (x, y, b) in segs:
        bs = "+".join(v for k, v in sorted(names.items()) if b & k)
        print("%d %d %d%s" % (n, x, y, (" " + bs) if bs else ""))


# ---------------------------------------------------------------- landtable dump (--dump-land)

LAND_HDR = struct.Struct("<8I")
LAND_TRI = struct.Struct("<9f II 9f III HHHH 3H H")
LAND_COL_HDR = struct.Struct("<8I")
LAND_COL = struct.Struct("<II 3f f ff I I I 3f 3I 3f I 3f f 3f II I III")
assert LAND_TRI.size == 108 and LAND_COL.size == 136


def load_land(prefix):
    """-> (header dict, triangles, cols); v1 .tri files give triangles with v/flags/col only"""
    data = open(prefix + ".tri", "rb").read()
    magic, ver = struct.unpack_from("<II", data, 0)
    if magic != 0x444E414C:
        raise SystemExit("%s.tri: not a LAND file" % prefix)
    tris, cols = [], []
    if ver == 1:
        _, _, ntri, ncol = struct.unpack_from("<4I", data, 0)
        hdr = {"version": 1, "ntri": ntri, "ncol": ncol}
        for i in range(ntri):
            v = struct.unpack_from("<9fII", data, 16 + 44 * i)
            tris.append({"v": v[:9], "flags": v[9], "col": v[10]})
        return hdr, tris, cols
    h = LAND_HDR.unpack_from(data, 0)
    hdr = dict(zip(["magic", "version", "ntri", "ncol", "tri_size", "col_size", "landtable", "nnodes"], h))
    for i in range(hdr["ntri"]):
        v = LAND_TRI.unpack_from(data, LAND_HDR.size + hdr["tri_size"] * i)
        tris.append({"v": v[0:9], "flags": v[9], "col": v[10], "lv": v[11:20], "attrflags": v[20], "emit": v[21],
                     "node_obj": v[22], "node": v[23], "mset": v[24], "mesh": v[25], "tri": v[26],
                     "idx": v[27:30], "type": v[30]})
    if os.path.exists(prefix + ".col"):
        cd = open(prefix + ".col", "rb").read()
        ch = LAND_COL_HDR.unpack_from(cd, 0)
        ncol, csz = ch[2], ch[3]
        names = ["index", "flags", "center", "radius", "ywidth", "zwidth", "blockbit", "object", "evalflags", "pos",
                 "ang", "scl", "model", "model_center", "model_r", "world_center", "tri_first", "tri_count", "nodes",
                 "npoints", "nmeshsets", "nmats"]
        counts = [1, 1, 3, 1, 1, 1, 1, 1, 1, 3, 3, 3, 1, 3, 1, 3, 1, 1, 1, 1, 1, 1]
        for i in range(ncol):
            v = LAND_COL.unpack_from(cd, LAND_COL_HDR.size + csz * i)
            c, k = {}, 0
            for nm, n in zip(names, counts):
                c[nm] = v[k] if n == 1 else tuple(v[k:k + n])
                k += n
            cols.append(c)
    return hdr, tris, cols


def cmd_land(a):
    hdr, tris, cols = load_land(a.prefix)
    print(" ".join("%s=%s" % (k, ("%08X" % v) if k in ("magic", "landtable") else v) for k, v in hdr.items()))
    if hdr["version"] >= 2:
        coll = [c for c in cols if c["flags"] & 0x400003]
        print("%d COLs (%d collidable, %d triangles of them), %d with child nodes" % (
            len(cols), len(coll), sum(c["tri_count"] for c in coll), sum(1 for c in cols if c["nodes"] > 1)))
        types = {}
        for t in tris:
            types[t["type"]] = types.get(t["type"], 0) + 1
        print("triangles by meshset type: " + ", ".join("%04X: %d" % kv for kv in sorted(types.items())))
    if a.col is not None and cols:
        c = cols[a.col]
        for k, v in c.items():
            print("  %-13s %s" % (k, ("%08X" % v) if k in ("flags", "object", "model", "evalflags", "blockbit")
                                  else (tuple("%04X" % (x & 0xFFFF) for x in v) if k == "ang" else v)))
        for t in tris[c["tri_first"]:c["tri_first"] + min(c["tri_count"], 8)]:
            print("    emit %3d node %d mset %d mesh %d tri %d idx %s attr %08X  v %s" % (
                t["emit"], t["node"], t["mset"], t["mesh"], t["tri"], t["idx"], t["attrflags"],
                " ".join("%.4f" % x for x in t["v"])))


def cmd_landcheck(a):
    """the collision cache of a RAM image (--dump-ram-at) against a --dump-land v2 output: for every
    landtable object in the 192-entry colobj buffer B_8C6BB27C, its COLPOLY list (vertex bits and
    attr, as sets: the pool is sorted by z) and colobj.center against the dump"""
    ram = open(a.ram, "rb").read()

    def r32(x):
        return struct.unpack_from("<I", ram, x & 0xFFFFFF)[0]
    hdr, tris, cols = load_land(a.prefix)
    if hdr["version"] < 2:
        raise SystemExit("%s: needs a version-2 dump" % a.prefix)
    byobj = {c["object"] & 0x1FFFFFFF: c for c in cols}

    def bits(fs):
        return struct.unpack("<9I", struct.pack("<9f", *fs))
    nobj = nok = npoly = dyn = 0
    bad = []
    for k in range(192):
        co = 0x8C6BB27C + 0x40 * k
        obj = r32(co + 8)
        if not obj:
            continue
        c = byobj.get(obj & 0x1FFFFFFF)
        if not c:
            dyn += 1                    # dynamic (moving) collision object
            continue
        nobj += 1
        pool, p = [], r32(co + 0x10)
        while p and len(pool) < 0x2000:
            pool.append((struct.unpack_from("<9I", ram, (p + 0x18) & 0xFFFFFF), r32(p + 0x3C)))
            p = r32(p)
        npoly += len(pool)
        mine = sorted((bits(t["v"]), t["attrflags"]) for t in tris[c["tri_first"]:c["tri_first"] + c["tri_count"]])
        cen = struct.unpack_from("<3I", ram, (co + 0x30) & 0xFFFFFF)
        if sorted(pool) == mine and cen == struct.unpack("<3I", struct.pack("<3f", *c["world_center"])):
            nok += 1
        else:
            bad.append(c["index"])
    print("%d landtable objects in the collision cache (%d polygons): %d identical to the dump%s; %d dynamic" % (
        nobj, npoly, nok, (", COL %s differ" % bad[:10]) if bad else "", dyn))
    return 1 if bad else 0


# ---------------------------------------------------------------- world dump (--dump-world)

def world_states(path):
    """yields (tag, state dict name -> bytearray) after each block of a --dump-world file"""
    state, tag = {}, None
    with open(path) as f:
        for line in f:
            if line.startswith("#"):
                continue
            line = line.rstrip("\n")
            if line.startswith("@"):
                if tag is not None:
                    yield tag, state
                tag = line
                continue
            sym, hx = line.split(" ")
            name, off = sym.rsplit("+", 1)
            off = int(off, 16)
            b = bytes.fromhex(hx)
            cur = state.setdefault(name, bytearray())
            if len(cur) < off + len(b):
                cur.extend(b"\0" * (off + len(b) - len(cur)))
            cur[off:off + len(b)] = b
    if tag is not None:
        yield tag, state


def cmd_world(a):
    ga, gb = world_states(a.a), world_states(a.b)
    n = 0
    for (ta, sa), (tb, sb) in zip(ga, gb):
        n += 1
        if ta != tb:
            print("block %d: %s vs %s" % (n, ta, tb))
            return 1
        names = sorted(set(sa) | set(sb))
        bad = []
        for nm in names:
            x, y = sa.get(nm, b""), sb.get(nm, b"")
            if x != y:
                k = next((i for i in range(max(len(x), len(y))) if x[i:i + 1] != y[i:i + 1]), 0)
                bad.append("%s+%X: %s vs %s" % (nm, k, x[k:k + 8].hex(), y[k:k + 8].hex()))
        if bad:
            print("first difference at %s (%d symbols):" % (ta, len(bad)))
            for b in bad[:20]:
                print("  " + b)
            return 1
    print("identical over %d blocks" % n)
    return 0


# ---------------------------------------------------------------- helpers for regress.py / route_matrix.py

_exe_versions = {}
_exe_screen = {}


def exe_trace_version(exe):
    """trace version an sa1h build writes (2 if its usage mentions --trace-v1, else 1)"""
    exe = os.path.abspath(exe)
    if exe not in _exe_versions:
        r = subprocess.run([exe, "trace", "--help"], capture_output=True, text=True)
        _exe_versions[exe] = 2 if "--trace-v1" in (r.stdout + r.stderr) else 1
    return _exe_versions[exe]


def exe_screen_mode(exe):
    """how an sa1h build sets up the screen (njInitSystem's view part, header flag 8):
    2 by default with --no-screen to opt out (since 2026-10-20), 1 only with --screen (the experiment
    builds of 2026-10-20), 0 never (older builds)"""
    exe = os.path.abspath(exe)
    if exe not in _exe_screen:
        r = subprocess.run([exe, "trace", "--help"], capture_output=True, text=True)
        u = r.stdout + r.stderr
        _exe_screen[exe] = 2 if "--no-screen" in u else 1 if "--screen" in u else 0
    return _exe_screen[exe]


def screen_args(exe, want):
    """trace options that make build exe run with (want true) or without the screen; None: it cannot"""
    mode = exe_screen_mode(exe)
    if want:
        return [] if mode == 2 else ["--screen"] if mode == 1 else None
    return ["--no-screen"] if mode == 2 else []


def reference_for(exe, refdir, name):
    """the reference trace a build can --check against: <name>.bin, or for a build that
    only knows version 1, <name>.v1.bin when <name>.bin is version 2 (None: none usable)"""
    ref = os.path.join(refdir, name + ".bin")
    if exe_trace_version(exe) >= 2:
        return ref if os.path.exists(ref) else None
    if os.path.exists(ref) and version_of(ref) == 1:
        return ref
    old = os.path.join(refdir, name + ".v1.bin")
    return old if os.path.exists(old) else None


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("info"); p.add_argument("trace"); p.set_defaults(fn=cmd_info)
    p = sub.add_parser("dump"); p.add_argument("trace"); p.add_argument("--from", dest="start", type=int, default=0)
    p.add_argument("--to", type=int); p.add_argument("--fields")
    p.add_argument("--p1", action="store_true", help="the player-1 sidecar NAME.p1.bin (fields: ai_*, p1.<field>)")
    p.add_argument("--obj", action="store_true", help="the host-object sidecar NAME.obj.bin (calls and events)")
    p.add_argument("--all", action="store_true", help="--obj: also the proxies and the held state of every step")
    p.set_defaults(fn=cmd_dump)
    p = sub.add_parser("obj"); p.add_argument("trace"); p.add_argument("--from", dest="start", type=int, default=0)
    p.add_argument("--to", type=int); p.add_argument("--all", action="store_true", help="the proxies and the held state")
    p.set_defaults(fn=cmd_obj)
    p = sub.add_parser("p1"); p.add_argument("trace"); p.add_argument("--from", dest="start", type=int, default=0)
    p.add_argument("--to", type=int); p.add_argument("--fields"); p.set_defaults(fn=cmd_p1)
    p = sub.add_parser("diff"); p.add_argument("ref"); p.add_argument("other")
    p.add_argument("--ulps", type=int, default=0); p.add_argument("--abs", type=float)
    p.add_argument("--fields"); p.add_argument("--all", action="store_true")
    p.add_argument("--raw", action="store_true", help="also compare the raw blobs and XMTRX word by word")
    p.add_argument("--heap-by-structure", action="store_true",
                   help="pointers into the record's own blobs as blob+offset, other heap pointers as 'heap'")
    p.add_argument("--no-phys", action="store_true", help="compare pointer words exactly, not by physical address")
    p.set_defaults(fn=cmd_diff)
    p = sub.add_parser("compat"); p.add_argument("v1"); p.add_argument("v2"); p.set_defaults(fn=cmd_compat)
    p = sub.add_parser("script"); p.add_argument("trace"); p.set_defaults(fn=cmd_script)
    p = sub.add_parser("land"); p.add_argument("prefix"); p.add_argument("--col", type=int)
    p.set_defaults(fn=cmd_land)
    p = sub.add_parser("landcheck"); p.add_argument("prefix"); p.add_argument("ram"); p.set_defaults(fn=cmd_landcheck)
    p = sub.add_parser("world"); p.add_argument("a"); p.add_argument("b"); p.set_defaults(fn=cmd_world)
    a = ap.parse_args()
    return a.fn(a) or 0


if __name__ == "__main__":
    sys.exit(main())
