#!/usr/bin/env python3
"""gen_emote_names.py - the emote catalogue's table of names and numbers (round 0.9.4a, 094a-lib-emote; sadc
docs/research/faces_wheel_plan.md 4.5 / 5.3 / 6.3 task 1 with its Review R.2 and R.9, emote_wheel_lib.md 3).

Reads the seven research TSVs (emote_actions_{sonic,tails,knuckles,amy,gamma,big,supersonic}.tsv: names, labels, classes,
numbers and addresses only - no game data) and writes src/sa1_emote_names.c: for every (kind, form) the OFFERED entries
of the emote catalogue, in list order (the table rows in row order, then the validated extra motions in TSV order):

    source      SA1_EMOTE_ROW / SA1_EMOTE_EXTRA
    index       the action row, or the extra's index among the kind's offered extras
    name        the internal name (the developers' motion file name from the SA Tools labels)
    label       the SA Tools label
    class       SA1_EMOTE_CLASS_*
    flags       SA1_EMOTE_F_* (NEEDS_PROP from `needs`, HOLD from `play`, SUPER_ONLY 130-141, BOARD, OTHER_OBJECT from
                the drawn object, EXTRA)
    dup_of      the list position of the first entry with the same DRAWN object and motion (kind 8: the substituted
                Super body / ball; the General form: GT's objects) - never the name: names are not unique (review R.9:
                Knuckles' k_panch0 = rows 39 and 54, two motions)
    frames      extras only: NJS_MOTION.nbFrame (review R.2: no extra motion is left to read without a world); rows take
                their frames and play modes from the loaded character set (sa1_action_info_get)
    motion      extras only: the DC address of the motion (the body object is the kind's main object, sa1_manifest_char)

    py -3.12 tools/gen_emote_names.py [TSV_DIR] [--out FILE] [--check]

TSV_DIR defaults to ../sadc/docs/research next to LIB. --check regenerates in memory and exits 1 when the file differs.
"""
import argparse
import os
import sys

LIB = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_TSV = os.path.normpath(os.path.join(LIB, "..", "sadc", "docs", "research"))
DEFAULT_OUT = os.path.join(LIB, "src", "sa1_emote_names.c")

# SA1_EMOTE_CLASS_* (include/libsa1.h, faces_wheel_plan.md 5.3)
CLASSES = {"stand": 0, "idle": 1, "pose": 2, "move": 3, "air": 4, "brake": 5, "ball": 6, "attack": 7, "carry": 8,
           "setpiece": 9, "hurt": 10, "fish": 11, "swim": 12, "board": 13, "super": 14}
F_NEEDS_PROP, F_HOLD, F_SUPER_ONLY, F_BOARD, F_OTHER_OBJECT, F_EXTRA = 0x1, 0x2, 0x4, 0x8, 0x10, 0x20

# kind -> (TSV, the main body's SA Tools object label)
CHARS = [
    ("sonic", 0, "Sonic"),
    ("tails", 2, "Tails"),
    ("knuckles", 3, "Knuckles"),
    ("amy", 5, "Amy"),
    ("gamma", 6, "E_102_Gamma"),
    ("big", 7, "Big"),
]
SUPER_BODY = "B_CHAOS7_Super_Sonic"
SUPER_BALL = "B_CHAOS7_Super_Sonic_Jumpball"
SPIN_BALL_MOTION = "8C232E80"       # Sonic_Spin_Jumpball (action 141's motion): GT's curled-body rows draw it


def read_tsv(path):
    rows = []
    hdr = None
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\r\n")
            if not line or line.startswith("#"):
                continue
            cells = line.split("\t")
            if hdr is None:
                hdr = cells
                continue
            cells += [""] * (len(hdr) - len(cells))
            rows.append(dict(zip(hdr, cells)))
    if hdr is None:
        raise SystemExit("%s: no header" % path)
    return rows


def c_str(s):
    if any(ord(ch) < 32 or ord(ch) > 126 for ch in s):
        raise SystemExit("non-ASCII name %r" % s)
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def needs_prop(needs):
    n = needs.lower()
    return ("held object" in n or "stage object" in n or "fishing tasks" in n or n.startswith("water"))


def row_flags(r, drawn_obj, main_obj, super_only):
    f = 0
    if needs_prop(r.get("needs", "")):
        f |= F_NEEDS_PROP
    if r.get("play") == "hold":
        f |= F_HOLD
    if super_only:
        f |= F_SUPER_ONLY
    if r.get("class") == "board":
        f |= F_BOARD
    if drawn_obj != main_obj:
        f |= F_OTHER_OBJECT
    return f


def assign_dups(entries):
    """dup_of = the list position of the first entry with the same drawn (object, motion)"""
    first = {}
    for i, e in enumerate(entries):
        key = (e["obj"], e["motion"].upper())
        if key in first:
            e["dup"] = first[key]
        else:
            e["dup"] = -1
            first[key] = i


def build(tsv_dir):
    lists = []          # (symbol, kind, form, entries)
    sonic_rows = None
    for name, kind, main in CHARS:
        rows = read_tsv(os.path.join(tsv_dir, "emote_actions_%s.tsv" % name))
        if name == "sonic":
            sonic_rows = rows
        entries = []
        for r in rows:
            if r["action"] == "-" or r["wheel"] != "yes":
                continue
            row = int(r["action"])
            cls = r["class"]
            if cls not in CLASSES:
                raise SystemExit("%s row %d: class %r" % (name, row, cls))
            entries.append({"source": 0, "index": row, "name": r["internal_name"], "label": r["label"],
                            "cls": CLASSES[cls], "flags": row_flags(r, r["object"], main, False),
                            "frames": 0, "motion": r["motion_addr"], "maddr": 0, "obj": r["object"]})
        nx = 0
        for r in rows:
            if r["action"] != "-" or r["wheel"] != "yes":
                continue
            cls = r["class"]
            if not cls.startswith("extra:") or cls[6:] not in CLASSES:
                raise SystemExit("%s extra %s: class %r" % (name, r["internal_name"], cls))
            f = F_EXTRA
            if needs_prop(r.get("needs", "")) or cls[6:] in ("carry", "setpiece", "fish", "swim"):
                f |= F_NEEDS_PROP
            if r.get("play") == "hold":
                f |= F_HOLD
            entries.append({"source": 1, "index": nx, "name": r["internal_name"], "label": r["label"],
                            "cls": CLASSES[cls[6:]], "flags": f, "frames": int(r["frames"]),
                            "motion": r["motion_addr"], "maddr": int(r["motion_addr"], 16), "obj": main})
            nx += 1
        assign_dups(entries)
        lists.append(("k_%s" % name, kind, 0, entries))

    # Super Sonic: Sonic's table as the two Super Sonics play it (emote_actions_supersonic.tsv kind8 / general columns)
    srows = read_tsv(os.path.join(tsv_dir, "emote_actions_supersonic.tsv"))
    by_row = {int(r["action"]): r for r in sonic_rows if r["action"] != "-"}
    for col, sym, kind, form in (("general", "k_sonic_super", 0, 1), ("kind8", "k_supersonic", 8, 0)):
        entries = []
        for s in srows:
            row = int(s["action"])
            if s[col].startswith("no"):
                continue
            r = by_row[row]
            obj, motion = r["object"], r["motion_addr"]
            if obj == "Sonic":
                obj = SUPER_BODY
            elif obj == "Sonic_Jumpball":
                obj = SUPER_BALL
            elif obj == "Sonic_Curled":
                if col != "general":
                    raise SystemExit("kind 8 offers a curled-body row %d" % row)
                obj, motion = SUPER_BALL, SPIN_BALL_MOTION      # GT: {Super ball, Sonic_Spin_Jumpball}
            cls = s["class"]
            if cls not in CLASSES:
                raise SystemExit("supersonic row %d: class %r" % (row, cls))
            entries.append({"source": 0, "index": row, "name": s["internal_name"], "label": s["label"],
                            "cls": CLASSES[cls], "flags": row_flags(r, obj, SUPER_BODY, 130 <= row <= 141),
                            "frames": 0, "motion": motion, "maddr": 0, "obj": obj})
        assign_dups(entries)
        lists.append((sym, kind, form, entries))
    return lists


def render(lists, tsv_dir):
    out = []
    w = out.append
    w("/*")
    w(" * sa1_emote_names.c - GENERATED by tools/gen_emote_names.py from sadc docs/research/emote_actions_*.tsv: names and")
    w(" * numbers only (no game data). Do not edit: run \"py -3.12 tools/gen_emote_names.py\" (--check compares).")
    w(" *")
    w(" * The emote catalogue (round 0.9.4a, sadc docs/research/faces_wheel_plan.md 4.5 / 5.3 with its Review R.2, R.9): per")
    w(" * character kind and form the offered entries in list order - the action rows in row order, then the validated extra")
    w(" * motions. Fields: sa1em_entry in src/sa1_emote.h. dup_of compares the DRAWN object and the motion (kind 8: the")
    w(" * substituted Super body / ball; the General form: GT's objects), never the internal name (names are not unique).")
    w(" * Rows' frames and play modes come from the loaded character set (sa1_action_info_get); extras carry theirs here.")
    w(" */")
    w('#include "sa1_emote.h"')
    w("")
    for sym, kind, form, entries in lists:
        w("static const sa1em_entry %s[%d] = {" % (sym, len(entries)))
        for i, e in enumerate(entries):
            w("    /* %3d */ { %du, %2du, 0x%02Xu, %3du, %3d, %3du, 0x%08Xu, %s, %s }," % (
                i, e["source"], e["cls"], e["flags"], e["index"], e["dup"], e["frames"], e["maddr"],
                c_str(e["name"]), c_str(e["label"])))
        w("};")
        w("")
    w("static const sa1em_list k_lists[%d] = {" % len(lists))
    for sym, kind, form, entries in lists:
        w("    { %d, %du, %du, %s }," % (kind, form, len(entries), sym))
    w("};")
    w("")
    w("const sa1em_list *sa1em_list_of(int32_t kind, uint32_t form)")
    w("{")
    w("    uint32_t i;")
    w("    if (kind == 8) {")
    w("        form = 0u;                  /* kind 8 is always Super: one list */")
    w("    }")
    w("    for (i = 0; i < (uint32_t)(sizeof k_lists / sizeof k_lists[0]); i++) {")
    w("        if (k_lists[i].kind == kind && k_lists[i].form == form) {")
    w("            return &k_lists[i];")
    w("        }")
    w("    }")
    w("    return NULL;")
    w("}")
    return "\n".join(out) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("tsv_dir", nargs="?", default=DEFAULT_TSV)
    ap.add_argument("--out", default=DEFAULT_OUT)
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    lists = build(a.tsv_dir)
    text = render(lists, a.tsv_dir)
    sizes = ", ".join("%s %d" % (sym[2:], len(e)) for sym, _, _, e in lists)
    if a.check:
        try:
            cur = open(a.out, encoding="utf-8", newline="").read()
        except OSError:
            cur = None
        if cur != text:
            print("gen_emote_names: %s differs from the TSVs in %s (run without --check)" % (a.out, a.tsv_dir))
            sys.exit(1)
        print("gen_emote_names: %s equals the TSVs (%s)" % (a.out, sizes))
        return
    with open(a.out, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print("gen_emote_names: wrote %s (%s)" % (a.out, sizes))


if __name__ == "__main__":
    main()
