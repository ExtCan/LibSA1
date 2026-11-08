#!/usr/bin/env python3
"""gen_emote_events.py - the catalogue of cutscene animations (round 100-EV, G-SA1 1.0.0-beta1; sadc
docs/research/emotes_ev.md 3.2 with its review RX.10, gsa1_100_master.md R1 r1-ev).

Reads the developer's own SADC/baserom (overlays.json, the unpacked EVxxxx.BIN images, 1ST_READ.BIN) and the evidence table
tools/emote_events.tsv (numbers, hashes and names only: the cutscenes' own pace and loop bit per motion, the floor measures,
the cast per cutscene; made once from the research tables with --evidence-from) and writes src/sa1_emote_events.c: for the six
characters with an emote wheel (kinds 0, 2, 3, 5, 6, 7) every character animation of the 207 cutscene overlays that is not a
copy of a gameplay row or extra (334), in order of cutscene number, then address, and the cutscene files they need (number,
file size, SHA-1, image size, cast). No game bytes: addresses, counts, sizes, SHA-1 and generated names only. No SA Tools label
(review RX.10: they stay in the research notes).

A binding is an aligned pair {object, motion} (an NJS_ACTION) in an EV image whose object is the character's gameplay body
(Tails: also his flight body 8C26769C) and whose motion is valid: the header inside the image, nbFrame 1..100000, type 3 or 7
(position + angle [+ scale], 16-byte keys), inp_fn's low nibble = the channels, every per-node channel pointer 0 or inside the
image and aligned, its key count <= nbFrame + 1, its keys inside the image, the per-node table (the body's node count) inside
the image. Entries are distinct by key content (the research's motion hash: SHA-1 of nbFrame, type, inp_fn and every node's
keys); an entry's home is the first cutscene that holds it, its action and motion the lowest such binding there; `order` its
place among the character's entries of that home by address, from 1; the name "ev%04x_%u". The generator stops when a binding
fails a check that the research passed, or when a count differs from the measured one (emotes_ev.md 2.2).

Copies of gameplay (round r1-fix-ev, the verifier r1-verify-ev's finding): the generator measures them itself - the motion of
every row and extra the wheel offers (sadc docs/research/emote_actions_<name>.tsv, wheel "yes": its image and motion address)
hashed with the same content hash and the body's node count - and stops when the evidence's gameplay_dup column disagrees.
The research (plan100/anim bindings.py) had missed Knuckles' six: his extras' TSV rows leave `nodes` empty, so it never hashed
them; they are the K_MOT motions of k_item_c0..2 and k_item_fg0..2 that EV016B / EV016A bind (Knuckles 35, not 41).

The lift (round r1-fix-ev, the verifier's second finding): the research's lift comes from rigid model points, but the body the
game draws is welded and wears the display's own models, so its lowest point lies higher (EV0003 #2 floated 0.18-0.25 above
the stand with the rigid 1.87; LIB's CPU pose evaluator with welds gives 1.8656, so it cannot stand in for the draw). Every
entry the research lifts (lift > 0) takes its lift from tools/emote_lift.tsv instead: the drawn body measured in a LIB world
(tests/emote_test.c --measure-lift: the settled stand's lowest drawn point minus the animation's at frame 0); SA1_EMOTE_F_LIFTED
follows the new number (> 0.25). Which entries lift stays the research's rule (the rigid number's cap of 4 included); the
amount is the draw's (Big's EV00D0 #1, #7, #8: rigid 3.73, drawn 4.36). The generator stops when an entry it lifts has no
measured line or a number outside 0..8.

    py -3.12 tools/gen_emote_events.py [--sadc DIR] [--out FILE] [--check]
    py -3.12 tools/gen_emote_events.py --evidence-from SADC/build/research/plan100/anim     (writes tools/emote_events.tsv)

--check regenerates in memory and exits 1 when the file differs (or the evidence does not join).
"""
import argparse
import collections
import csv
import hashlib
import json
import os
import re
import struct
import sys

LIB = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_SADC = os.path.normpath(os.path.join(LIB, "..", "sadc"))
DEFAULT_OUT = os.path.join(LIB, "src", "sa1_emote_events.c")
EVIDENCE = os.path.join(LIB, "tools", "emote_events.tsv")
LIFTS = os.path.join(LIB, "tools", "emote_lift.tsv")      # r1-fix-ev: the drawn lift (tests/emote_test.c --measure-lift)

# kind, name, body roots (object index 0 = main body; 1 = another body of the same tree: Tails' flight model), measured count
CHARS = [
    (0, "sonic", [0x8C1CE76C], 80),
    (2, "tails", [0x8C25F968, 0x8C26769C], 65),
    (3, "knuckles", [0x8C35B094], 35),     # r1-fix-ev: 41 less his six copies of k_item_c0..2 / k_item_fg0..2
    (5, "amy", [0x8C4C7C3C], 88),
    (6, "gamma", [0x8C42CC34], 31),
    (7, "big", [0x8C52F298], 35),
]
# SA1_CHAR_* of the research's cast names (event copies count as the character: emotes_ev.md 3.2 `cast`; the E-10x robots
# shaped like E-102 are other robots and are not E-102)
CAST_BIT = {"sonic": 0, "eggman": 1, "eggman_eventcopy": 1, "tails": 2, "tails_flight": 2, "tails_eventcopy": 2, "knuckles": 3,
            "tikal": 4, "amy": 5, "gamma": 6, "big": 7}
CAST_SKIP = {"gamma_eventcopy", "-"}
# SA1_EMOTE_F_* (include/libsa1.h)
F_OTHER_OBJECT, F_EVENT, F_ROOT_MOVES, F_LIFTED, F_RESTS_ON = 0x10, 0x100, 0x200, 0x400, 0x800
# the deny list: entries the play-all test (emote_test T3) found faulting (none so far)
DENY = set()
EV_LOAD = 0x0CB80000
MAX_IMAGE = 1 << 20


def phys(a):
    return a & 0x1FFFFFFF


class Image:
    def __init__(self, data, load):
        self.d = data
        self.b = phys(load)

    def inside(self, a, n=4):
        p = phys(a)
        return a != 0 and (a & 3) == 0 and self.b <= p and p + n <= self.b + len(self.d)

    def u32(self, a):
        return struct.unpack_from("<I", self.d, phys(a) - self.b)[0]

    def raw(self, a, n):
        o = phys(a) - self.b
        return self.d[o:o + n]


def tree_nodes(img, root):
    """the node count and shape of an NJS_OBJECT tree (child +0x2C, sibling +0x30; depth first, child before sibling)"""
    shape = bytearray()

    def walk(a, depth):
        while a:
            if not img.inside(a, 0x34) or depth > 64 or len(shape) > 512:
                raise SystemExit("tree at %08X leaves 1ST_READ" % root)
            ch, sib = struct.unpack_from("<II", img.d, phys(a) - img.b + 0x2C)
            shape.append((1 if ch else 0) | (2 if sib else 0))
            if ch:
                walk(ch, depth + 1)
            a = sib
    walk(root, 0)
    return len(shape), hashlib.sha1(bytes(shape)).hexdigest()[:12]


def check_motion(img, mo, nn):
    """the bindings' checks; returns (nbFrame, type, inp_fn, nf, md, channels[(node, ch, ptr, count)], key bytes) or None"""
    if not img.inside(mo, 12):
        return None
    md, nbf = img.u32(mo), img.u32(mo + 4)
    typ, inp = struct.unpack_from("<HH", img.d, phys(mo) - img.b + 8)
    nf = inp & 0xF
    if not img.inside(md, 4) or not (1 <= nbf <= 100000) or nf not in (1, 2, 3, 4, 5, 6) or bin(typ).count("1") != nf or typ & ~0x3FF:
        return None
    if not img.inside(md, 8 * nf * nn):
        return None
    chans, kb = [], 0
    for i in range(nn):
        e = struct.unpack_from("<%dI" % (2 * nf), img.d, phys(md) - img.b + 8 * nf * i)
        for k in range(nf):
            p, n = e[k], e[nf + k]
            if p and n:
                if not img.inside(p, 4) or n > nbf + 1 or not img.inside(p, 16 * n):
                    return None
                chans.append((i, k, p, n))
                kb += 16 * n
    return nbf, typ, inp, nf, md, chans, kb


def mot_hash(img, nbf, typ, inp, chans):
    """the research's motion hash (plan100/anim/tools/bindings.py mot_hash): content, not address"""
    h = hashlib.sha1(struct.pack("<IHH", nbf, typ, inp))
    for i, k, p, n in chans:
        h.update(struct.pack("<II", i, k))
        h.update(img.raw(p, 16 * n))
    return h.hexdigest()[:16]


def read_tsv(path):
    rows = collections.defaultdict(list)
    hdr = {}
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\r\n")
            if not line or line.startswith("#"):
                continue
            cells = line.split("\t")
            if cells[0] == "@header":
                hdr[cells[1]] = cells[2:]
                continue
            sec = cells[0]
            rows[sec].append(dict(zip(hdr[sec], cells[1:])))
    return rows


def scan(sadc):
    ovs = {o["name"]: o for o in json.load(open(os.path.join(sadc, "baserom", "overlays.json"), encoding="utf-8"))}
    first = Image(open(os.path.join(sadc, "baserom", "1ST_READ.BIN"), "rb").read(), 0x8C010000)
    bodies = {}         # phys -> (kind, object index, nodes, shape)
    for kind, name, roots, _ in CHARS:
        shapes = [tree_nodes(first, r) for r in roots]
        for oi, r in enumerate(roots):
            if shapes[oi] != shapes[0]:
                raise SystemExit("%s: body %08X has another tree than the main body" % (name, r))
            bodies[phys(r)] = (kind, oi, shapes[oi][0], shapes[oi][1])
    evs = sorted(n for n in ovs if re.match(r"^EV[0-9A-F]{4}$", n))
    files = {}
    binds = []          # dicts
    for name in evs:
        o = ovs[name]
        if int(o["load_addr"], 16) != EV_LOAD:
            raise SystemExit("%s loads at %s" % (name, o["load_addr"]))
        data = open(os.path.join(sadc, o["bin_path"]), "rb").read()
        if len(data) != o["size"] or len(data) > MAX_IMAGE:
            raise SystemExit("%s: image size %d, overlays.json %d" % (name, len(data), o["size"]))
        img = Image(data, EV_LOAD)
        files[name] = {"event": int(name[2:], 16), "prs_size": o["prs_size"], "sha1": o["sha1_prs"], "image_size": o["size"]}
        seen = set()
        for off in range(0, len(data) - 8, 4):
            ob, mo = struct.unpack_from("<II", data, off)
            if phys(ob) not in bodies or not img.inside(mo) or (phys(ob), mo) in seen:
                continue
            kind, oi, nn, _ = bodies[phys(ob)]
            r = check_motion(img, mo, nn)
            if r is None:
                continue
            seen.add((phys(ob), mo))
            nbf, typ, inp, nf, md, chans, kb = r
            binds.append({"overlay": name, "event": int(name[2:], 16), "kind": kind, "obj": oi, "nodes": nn,
                          "action": 0x0C000000 | phys(EV_LOAD + off), "motion": 0x0C000000 | phys(mo), "frames": nbf, "type": typ,
                          "inp": inp, "nf": nf, "hash": mot_hash(img, nbf, typ, inp, chans),
                          "bytes": 12 + 8 * nf * nn + kb, "md": md})
    return files, binds


def gameplay_copies(sadc):
    """per kind the content hashes of the motions of every row and extra its wheel offers (emote_actions_<name>.tsv, wheel
    "yes"), hashed as the bindings are, with the body's node count: an EV motion with one of them is a copy of gameplay"""
    ovs = {o["name"]: o for o in json.load(open(os.path.join(sadc, "baserom", "overlays.json"), encoding="utf-8"))}
    images = {"1ST_READ": Image(open(os.path.join(sadc, "baserom", "1ST_READ.BIN"), "rb").read(), 0x8C010000)}
    out = {}
    for kind, name, roots, _ in CHARS:
        nn = tree_nodes(images["1ST_READ"], roots[0])[0]
        hs = set()
        path = os.path.join(sadc, "docs", "research", "emote_actions_%s.tsv" % name)
        with open(path, encoding="utf-8") as f:
            for r in csv.DictReader((l for l in f if not l.startswith("#")), delimiter="\t"):
                if r.get("wheel") != "yes" or r.get("motion_addr") in (None, "", "-") or not r.get("image"):
                    continue
                im = r["image"].replace(".PRS", "")
                if im not in images:
                    o = ovs.get(im)
                    images[im] = Image(open(os.path.join(sadc, o["bin_path"]), "rb").read(), int(o["load_addr"], 16)) if o else None
                img = images[im]
                if img is None:
                    continue
                c = check_motion(img, int(r["motion_addr"], 16), nn)    # Image compares physical addresses
                if c is None:
                    continue
                nbf, typ, inp, nf, md, chans, kb = c
                hs.add(mot_hash(img, nbf, typ, inp, chans))
        out[kind] = hs
    return out


def build(sadc, evidence, lifts=LIFTS):
    files, binds = scan(sadc)
    ev = read_tsv(evidence)
    copies = gameplay_copies(sadc)
    drawn = {(int(r["kind"]), r["name"]): float(r["lift"]) for r in read_tsv(lifts)["drawn"]}
    mot = {(int(r["kind"]), int(r["obj"]), r["hash"]): r for r in ev["motion"]}
    cast = {r["overlay"]: r["cast"] for r in ev["cast"]}
    per = collections.OrderedDict()     # (kind, obj, hash) -> bindings in scan order (overlay, then address)
    for b in binds:
        per.setdefault((b["kind"], b["obj"], b["hash"]), []).append(b)
    lists = []
    used_files = set()
    for kind, name, roots, want in CHARS:
        ents = []
        for key, bl in per.items():
            if key[0] != kind:
                continue
            m = mot.get(key)
            if m is None:
                raise SystemExit("%s: motion %s (%s %08X) is not in %s: run --evidence-from" % (
                    name, key[2], bl[0]["overlay"], bl[0]["motion"], evidence))
            dup = key[2] in copies[kind]
            if dup != (m["gameplay_dup"] == "yes"):
                raise SystemExit("%s: motion %s (%s %08X): the evidence's gameplay_dup is %s, the wheel's rows and extras say %s"
                                 % (name, key[2], bl[0]["overlay"], bl[0]["motion"], m["gameplay_dup"], "yes" if dup else "no"))
            if dup:
                continue
            home = bl[0]
            if any(b["type"] not in (3, 7) for b in bl):
                raise SystemExit("%s %s: type %d (16-byte keys only)" % (name, key[2], home["type"]))
            if any(b["frames"] != home["frames"] or b["bytes"] != home["bytes"] for b in bl):
                raise SystemExit("%s %s: the same keys with other frames / sizes" % (name, key[2]))
            events = sorted(set(b["event"] for b in bl))
            lift = float(m["lift"])
            fl = F_EVENT
            if key[1] != 0:
                fl |= F_OTHER_OBJECT
            if float(m["travel"]) > 2.0 or float(m["rise"]) > 2.0:
                fl |= F_ROOT_MOVES
            if lift > 0.25:
                fl |= F_LIFTED
            if m["low0"] != "" and float(m["low0"]) > 0.5:
                fl |= F_RESTS_ON
            ents.append({"kind": kind, "obj": key[1], "event": home["event"], "overlay": home["overlay"],
                         "action": home["action"], "motion": home["motion"], "frames": home["frames"], "nodes": home["nodes"],
                         "type": home["type"], "inp": home["inp"], "bytes": home["bytes"], "pace": float(m["pace"]),
                         "loop": m["play"] == "loop", "lift": lift, "flags": fl, "events": events, "hash": key[2]})
        ents.sort(key=lambda e: (e["event"], e["action"]))
        order = collections.Counter()
        for e in ents:
            order[e["event"]] += 1
            e["order"] = order[e["event"]]
            e["name"] = "ev%04x_%u" % (e["event"], e["order"])
        for e in ents:
            if e["lift"] > 0.0:     # r1-fix-ev: the drawn body's lift (the docstring)
                d = drawn.get((kind, e["name"]))
                if d is None:
                    raise SystemExit("%s %s: no drawn lift in %s (run emote_test --measure-lift)" % (name, e["name"], lifts))
                if not (0.0 <= d <= 8.0):
                    raise SystemExit("%s %s: drawn lift %g (0..8: run emote_test --measure-lift again)" % (name, e["name"], d))
                e["lift"] = d   # the research chose which entries lift (its cap of 4 on the rigid number); the amount is the draw's
                e["flags"] = (e["flags"] & ~F_LIFTED) | (F_LIFTED if e["lift"] > 0.25 else 0)
        ents = [e for e in ents if e["name"] not in DENY]
        if len(ents) != want and not DENY:
            raise SystemExit("%s: %d entries, the research measured %d (emotes_ev.md 2.2)" % (name, len(ents), want))
        for e in ents:
            used_files.update(e["events"])
        lists.append((kind, name, ents))
    frows = []
    for nm in sorted(files):
        f = files[nm]
        if f["event"] not in used_files:
            continue
        bits = 0
        for part in cast.get(nm, "-").split(","):
            part = part.strip()
            if not part or part in CAST_SKIP:
                continue
            if part not in CAST_BIT:
                raise SystemExit("%s: cast %r" % (nm, part))
            bits |= 1 << CAST_BIT[part]
        f = dict(f)
        f["cast"] = bits
        frows.append(f)
    return lists, frows


def c_float(v):
    s = "%.6g" % v
    if "." not in s and "e" not in s:
        s += ".0"
    return s + "f"


def render(lists, frows):
    out = []
    w = out.append
    fidx = {f["event"]: i for i, f in enumerate(frows)}
    w("/*")
    w(" * sa1_emote_events.c - GENERATED by tools/gen_emote_events.py from the developer's SADC/baserom and tools/emote_events.tsv:")
    w(" * numbers, names and SHA-1 only (no game data, no SA Tools label: sadc docs/research/emotes_ev.md 3.2, review RX.10). Do not")
    w(" * edit: run \"py -3.12 tools/gen_emote_events.py\" (--check compares).")
    w(" *")
    w(" * The cutscene animations of the emote catalogue (round 100-EV, SA1_API_EMOTE_EVENT): per kind the entries in order of")
    w(" * cutscene number, then address (sa1ev_entry in src/sa1_emote_ev.h), then the cutscene files they are read from (sa1ev_file).")
    w(" * `action` / `motion` are addresses in the home cutscene's image (loaded at 0x0CB80000); `bytes` the copy's size (header,")
    w(" * per-node table, keys); `pace` the cutscene's own speed (frames per 1/30 s); `lift` SA1 units (SA1_EMOTE_F_LIFTED; the drawn")
    w(" * body's, tools/emote_lift.tsv).")
    w(" */")
    w('#include "sa1_emote_ev.h"')
    w("")
    w("const sa1ev_file sa1ev_files[%d] = {" % len(frows))
    for i, f in enumerate(frows):
        w("    /* %3d */ { 0x%04Xu, %7du, %7du, 0x%03Xu, \"%s\" }," % (i, f["event"], f["prs_size"], f["image_size"], f["cast"], f["sha1"]))
    w("};")
    w("const uint32_t sa1ev_num_files = %du;" % len(frows))
    w("")
    evs = []
    eoff = {}
    for kind, name, ents in lists:
        for e in ents:
            key = tuple(e["events"])
            if key not in eoff:
                eoff[key] = len(evs)
                evs.extend(key)
    w("static const uint16_t k_events[%d] = {" % len(evs))
    for i in range(0, len(evs), 16):
        w("    " + " ".join("0x%04X," % x for x in evs[i:i + 16]))
    w("};")
    w("")
    for kind, name, ents in lists:
        w("static const sa1ev_entry k_%s[%d] = {" % (name, len(ents)))
        for i, e in enumerate(ents):
            w("    /* %3d */ { 0x%04Xu, %2du, %du, %3du, %3du, %2du, %du, 0x%04Xu, 0x%08Xu, 0x%08Xu, %6du, %-11s %-11s 0x%03Xu, %du, %2du, &k_events[%d], \"%s\" }," % (
                i, e["event"], e["order"], e["obj"], fidx[e["event"]], e["frames"], e["nodes"], e["type"], e["inp"], e["action"],
                e["motion"], e["bytes"], c_float(e["pace"]) + ",", c_float(e["lift"]) + ",", e["flags"], 1 if e["loop"] else 0,
                len(e["events"]), eoff[tuple(e["events"])], e["name"]))
        w("};")
        w("")
    w("static const sa1ev_list k_lists[%d] = {" % len(lists))
    for kind, name, ents in lists:
        w("    { %d, %du, k_%s }," % (kind, len(ents), name))
    w("};")
    w("")
    w("const sa1ev_list *sa1ev_list_of(int32_t kind)")
    w("{")
    w("    uint32_t i;")
    w("    for (i = 0; i < (uint32_t)(sizeof k_lists / sizeof k_lists[0]); i++) {")
    w("        if (k_lists[i].kind == kind) {")
    w("            return &k_lists[i];")
    w("        }")
    w("    }")
    w("    return NULL;")
    w("}")
    return "\n".join(out) + "\n"


def evidence_from(d):
    """tools/emote_events.tsv from the research tables of sadc build/research/plan100/anim (emotes_ev.md 14): per distinct
    motion of the six characters the cutscenes' most used speed and loop bit over the tracked EV_SetAction calls of its
    bindings, the root's travel and rise, the first frame's lowest point against the stand (floor.py), the lift; per
    cutscene its cast. Numbers, hashes and names only."""
    def rd(fn):
        return list(csv.DictReader(open(os.path.join(d, fn), encoding="utf-8"), delimiter="\t"))
    names = {name: (kind, roots) for kind, name, roots, _ in CHARS}
    copies = gameplay_copies(DEFAULT_SADC)     # r1-fix-ev: the research's gameplay_dup missed Knuckles' six (the docstring)
    calls = collections.defaultdict(list)
    for r in rd("ev_calls.tsv"):
        if r["call"] != "SetAction":
            continue
        m = re.match(r"_D_([0-9A-F]{8})", r["action"] or "")
        if m:
            calls[(r["overlay"], m.group(1))].append(r)
    binds = collections.defaultdict(list)
    for r in rd("ev_bindings.tsv"):
        binds[(r["char"], r["obj"], r["hash"])].append(r)
    poses = {(r["char"], r["hash"]): r for r in rd("ev_poses.tsv")}
    floor = {(r["char"], r["hash"]): r for r in rd("ev_floor.tsv")}
    rows = []
    for r in rd("ev_motions.tsv"):
        if r["char"] not in names:
            continue
        kind, roots = names[r["char"]]
        oi = 0 if r["obj"] == "main" else [phys(x) for x in roots].index(int(r["obj"][4:], 16) & 0x1FFFFFFF)
        cs = [c for b in binds[(r["char"], r["obj"], r["hash"])] for c in calls.get((b["overlay"], b["action"]), [])]
        sp = collections.Counter("%.6g" % float(c["speed"]) for c in cs if c["speed"] not in ("", None))
        md = collections.Counter(c["mode"] for c in cs if c["mode"] not in ("", None))
        pace = min(sp.items(), key=lambda kv: (-kv[1], float(kv[0])))[0] if sp else "1"
        loops = md.get("1", 0)
        play = "loop" if loops > sum(md.values()) - loops else "once"
        p = poses.get((r["char"], r["hash"]))
        f = floor.get((r["char"], r["hash"])) if r["obj"] == "main" else None
        travel = float(p["root_travel_xz"]) if p else 0.0
        rise = float(p["root_rise"]) if p else 0.0
        low0 = float(f["first_frame_low_minus_stand"]) if f else None
        lift = max(0.0, -low0) if low0 is not None else 0.0
        if travel > 2.0 or rise > 2.0 or lift > 4.0:
            lift = 0.0
        rows.append((kind, oi, r["hash"], pace, play, "%.2f" % lift, "%.2f" % travel, "%.2f" % rise,
                     "" if low0 is None else "%.2f" % low0, "yes" if r["gameplay_dup"] or r["hash"] in copies[kind] else "no", r["char"],
                     r["first_overlay"], r["first_motion"]))
    cast = []
    for line in open(os.path.join(d, "cast_by_overlay.txt"), encoding="utf-8"):
        if line.startswith("#") or ":" not in line:
            continue
        ov, rest = line.split(":", 1)
        parts = [p.strip().rsplit(" ", 1)[0] for p in rest.split(",") if p.strip()]
        cast.append((ov.strip(), ",".join(parts) if parts else "-"))
    with open(EVIDENCE, "w", encoding="utf-8", newline="\n") as f:
        f.write("# emote_events.tsv - the evidence of tools/gen_emote_events.py (round 100-EV; sadc docs/research/emotes_ev.md 2, 3.2, 14).\n")
        f.write("# Made by --evidence-from from sadc build/research/plan100/anim (census.py, bindings.py, scan_calls.py, poses.py, floor.py).\n")
        f.write("# Numbers, hashes and names only. motion: kind, object (0 main body, 1 Tails' flight body), the research's motion hash\n")
        f.write("# (content), pace (frames per 1/30 s, the most used speed of the tracked EV_SetAction calls; 1 without a call), play,\n")
        f.write("# lift (SA1 units; 0 when the root travels or rises more than 2 or the lift is over 4), root travel, rise, the first\n")
        f.write("# frame's lowest point minus the stand's, gameplay_dup (a copy of a gameplay row or extra), then for a reader: character,\n")
        f.write("# home cutscene, motion address. cast: the characters with animations bound in a cutscene (bindings.py, event copies).\n")
        f.write("@header\tmotion\tkind\tobj\thash\tpace\tplay\tlift\ttravel\trise\tlow0\tgameplay_dup\tchar\thome\tmotion_addr\n")
        for r in sorted(rows, key=lambda x: (x[0], x[1], x[11], x[12])):
            f.write("motion\t" + "\t".join(str(x) for x in r) + "\n")
        f.write("@header\tcast\toverlay\tcast\n")
        for ov, c in cast:
            f.write("cast\t%s\t%s\n" % (ov, c))
    print("gen_emote_events: wrote %s (%d motions, %d cutscenes)" % (EVIDENCE, len(rows), len(cast)))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--sadc", default=DEFAULT_SADC)
    ap.add_argument("--evidence", default=EVIDENCE)
    ap.add_argument("--out", default=DEFAULT_OUT)
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--evidence-from", metavar="DIR")
    a = ap.parse_args()
    if a.evidence_from:
        evidence_from(a.evidence_from)
        return
    lists, frows = build(a.sadc, a.evidence)
    text = render(lists, frows)
    sizes = ", ".join("%s %d" % (name, len(e)) for _, name, e in lists)
    total = sum(len(e) for _, _, e in lists)
    if a.check:
        try:
            cur = open(a.out, encoding="utf-8", newline="").read()
        except OSError:
            cur = None
        if cur != text:
            print("gen_emote_events: %s differs from SADC/baserom + %s (run without --check)" % (a.out, a.evidence))
            sys.exit(1)
        print("gen_emote_events: %s equals SADC/baserom + the evidence (%d entries: %s; %d cutscene files)" % (a.out, total, sizes, len(frows)))
        return
    with open(a.out, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print("gen_emote_events: wrote %s (%d entries: %s; %d cutscene files)" % (a.out, total, sizes, len(frows)))


if __name__ == "__main__":
    main()
