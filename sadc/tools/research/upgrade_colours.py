#!/usr/bin/env python3
"""The name colour of every upgrade on G-SA1's CHOOSE YOUR UPGRADE, and of every character's name, from the owner's own disc
(sadc docs/research/gsa1_100_select.md 4.6.1 and 7.2 C1; the owner's answers 18, 22 and 29, sadc docs/research/gsa1_rounds_plan.md
14.1bl and 14.1bn).

    py -3.12 tools/research/upgrade_colours.py [--gdi GDI | --disc DIR] [--check <g-sa1>/addon/lua/gsa1/feat/sh_upgrades.lua]

Reads the disc at run time (the .gdi through rip_character.GdiFS, or an extracted folder holding 1ST_READ.BIN and SONICADV/), checks
1ST_READ.BIN's SHA-1 (US v1.005) and prints numbers only: colours as hex means and shares of area. It writes nothing and holds no game
byte: the upgrade parts are addresses carried here as numbers. Only the disc readers come from the project's tools (rip_character.py:
the ISO, NJS_MODEL, PVM and PVR readers; prs.py through it); no research notes are imported.

THE METHOD (select 4.6.1, made from plan100b/select-b/upgcolour.py; one tree per character, RY.3):
  1. the upgrade's own part is what the game's own code brings in for it (a swapped model, a swapped subtree, an extra draw, the lure
     models of D_8C565FC4), against the plain part it replaces (nothing for an extra draw, the plain lure for the lures);
  2. a point's colour is the texel alone (the characters' renderer takes no material colour); each triangle 45 points weighted by its
     area; an environment-mapped material (or one with no UVs) by its whole texture on an 8 x 8 grid; an untextured one its diffuse;
     texels under alpha 128 skipped where the material uses alpha;
  3. 15 bins: 12 hues of 30 degrees (saturation >= 0.25, value >= 0.20), black (value < 0.20), grey, white (value >= 0.75);
  4. what the upgrade adds: per bin its area minus the plain part's;
  5. its main colour: the three neighbouring hues to which it adds the most area, among the bins whose mean colour has saturation
     >= 0.25 and value >= 0.35; the mean of their colours, each weighted by the area it adds. None of its own (under 1 % of its area,
     or no model): the character's colour (false in the Lua);
  6. a character's colour: the fill of its pulsing name on Character Select (M_CHNAM.PRS m_<name>: the heaviest bin of its texels at
     alpha >= 128 and brightest channel >= 77).
THE OWNER'S CHOICES, carried as fixed values (not measures; their measure is printed beside them): the Light Speed Shoes F4FF00 (answer
29, the owner's mockup), the Jet Anklet C0C0C0 (answer 29), the Shovel Claw and the Laser Blaster C0C0C0 (answer 22); Ancient Light has
no model: false (answer 18's rule, answer 22's reading: the character's colour).
--check compares the 17 entries of U.COLORS and the seven of U.SLOT_COLOR in the Lua with these and exits 1 on any difference.
"""
import argparse
import colorsys
import hashlib
import math
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.dont_write_bytecode = True
import rip_character as rc  # noqa: E402  (sadc/tools/research: the disc readers; it imports sadc/tools/prs.py)
import prs  # noqa: E402

FIRST_READ_SHA1 = "e74c849806db103aa87f84e41a382873bc96b6df"
BASE = 0x8C010000
SADC = os.path.abspath(os.path.join(HERE, "..", ".."))

# 1. the parts, one tree each (the character's main tree; RY.3): (upgrade, texture archive, [(field, plain target, upgrade target)])
# field "model": the object's model is swapped; "sibling" / "child": a subtree is swapped in (its models, siblings and children)
SWAPS = [
    ("light_speed_shoes", "SONIC", [("sibling", 0x8C1C7938, 0x8C1E0550), ("sibling", 0x8C1C8888, 0x8C1E0EB0)]),
    ("crystal_ring", "SONIC", [("sibling", 0x8C1CACEC, 0x8C1E12AC)]),
    ("jet_anklet", "MILES", [("sibling", 0x8C259D40, 0x8C290484), ("sibling", 0x8C25ABB0, 0x8C290E54)]),
    ("rhythm_badge", "MILES", [("model", 0x8C2592A0, 0x8C28FA8C)]),
    ("shovel_claw", "KNUCKLES", [("model", 0x8C3567E8, 0x8C38E160), ("model", 0x8C3578B4, 0x8C38ED04)]),
    ("fighting_gloves", "KNUCKLES", [("model", 0x8C356DB0, 0x8C38F760), ("model", 0x8C3569B4, 0x8C38FFE8), ("model", 0x8C3567E8, 0x8C38FE1C),
                                     ("model", 0x8C356394, 0x8C38F9C8), ("model", 0x8C357EB0, 0x8C390418), ("model", 0x8C357A80, 0x8C390CBC),
                                     ("model", 0x8C3578B4, 0x8C390AF0), ("model", 0x8C357444, 0x8C390680)]),
    ("long_hammer", "AMY", [("sibling", 0x8C4C38B8, 0x8C4CBA84)]),
    ("laser_blaster", "E102", [("sibling", 0x8C42E2C4, 0x8C42EF4C), ("sibling", 0x8C42F780, 0x8C430408)]),
    ("life_belt", "BIG", [("sibling", 0x8C526E8C, 0x8C53232C)]),
    ("power_rod", "BIG", [("model", 0x8C52ACF4, 0x8C532FD8), ("child", 0x8C52A5E8, 0x8C5328CC)]),
]
# the extra draws (8C036F12 on a node's matrix): the Warrior Feather, the Jet Booster (its flame, drawn only while hovering, not counted)
ADDONS = [("warrior_feather", "AMY", 0x8C4CC1D0), ("jet_booster", "E102", 0x8C4309C4)]
LURE_TABLE = 0x8C565FC4          # D_8C565FC4[0..4]: the plain lure, then lures 1-4 (texlist_FISHING: FISHING.PRS by index)
LURES = ["lure_1", "lure_2", "lure_3", "lure_4"]
NO_MODEL = ["ancient_light"]
ORDER = ["light_speed_shoes", "crystal_ring", "ancient_light", "jet_anklet", "rhythm_badge", "shovel_claw", "fighting_gloves", "warrior_feather",
         "long_hammer", "jet_booster", "laser_blaster", "life_belt", "power_rod"] + LURES
# the owner's choices (rounds plan 14.1bn): fixed values, not measures
OWNER = {"light_speed_shoes": ("F4FF00", "the owner's answer 29: the mockup's yellow"), "jet_anklet": ("C0C0C0", "the owner's answer 29: silver"),
         "shovel_claw": ("C0C0C0", "the owner's answer 22: silver"), "laser_blaster": ("C0C0C0", "the owner's answer 22: silver"),
         "ancient_light": (False, "no model: the character's colour (answer 18; answer 22's reading)")}
# the characters' names on Character Select, by the screen's slot (G-SA1 S.SLOT_ENTRY)
NAMES = [("m_sonic", 0), ("m_tails", 1), ("m_knuckles", 2), ("m_amy", 3), ("m_big", 4), ("m_e102", 5), ("m_supersonic", 6)]
BIN_NAMES = ["red", "orange", "yellow", "lime", "green", "spring", "cyan", "azure", "blue", "violet", "magenta", "rose", "black", "grey", "white"]
BARY = [(i / 8.0, j / 8.0) for i in range(9) for j in range(9 - i)]       # 45 points


class Disc:
    """the owner's disc: a .gdi (read through rip_character.GdiFS) or an extracted folder"""

    def __init__(self, gdi=None, folder=None):
        self.fs = rc.GdiFS(gdi) if gdi else None
        self.folder = folder

    def read(self, path):
        if self.fs is not None:
            return self.fs.open(path)
        with open(os.path.join(self.folder, *path.split("/")), "rb") as f:
            return f.read()


def default_source():
    gdi = os.environ.get("SA1_TEST_GDI")
    if gdi and os.path.isfile(gdi):
        return gdi, None
    folder = os.path.join(SADC, "disc")
    if os.path.isfile(os.path.join(folder, "1ST_READ.BIN")):
        return None, folder
    return None, None


class Measure:
    def __init__(self, disc):
        self.disc = disc
        first = disc.read("1ST_READ.BIN")
        sha = hashlib.sha1(first).hexdigest()
        if sha != FIRST_READ_SHA1:
            raise SystemExit("1ST_READ.BIN SHA-1 %s: not Sonic Adventure US v1.005, whose addresses this tool carries" % sha)
        self.mem = rc.Mem()
        self.mem.add("1ST_READ.BIN", BASE, first)
        self.archives = {}

    def textures(self, name):
        if name not in self.archives:
            pvm = rc.parse_pvm(prs.decompress(self.disc.read("SONICADV/%s.PRS" % name)))
            self.archives[name] = [rc.decode_pvr(chunk)[:3] for _nm, _gbix, chunk in pvm]
        return self.archives[name]

    def chain(self, addr):
        """the models of the subtree at addr: node, children, then siblings (Ninja order)"""
        out = []

        def visit(a):
            while a:
                v = self.mem.unpack("<II3f3i3fII", a)
                if v[1]:
                    out.append(v[1])
                if v[11]:
                    visit(v[11])
                a = v[12]
        visit(addr)
        return out

    def part(self, field, addr):
        return [addr] if field == "model" else self.chain(addr)

    def sample(self, arch, models):
        """per bin [area, sum r, sum g, sum b], and the whole area"""
        H = [[0.0, 0.0, 0.0, 0.0] for _ in range(15)]
        total = 0.0
        T = self.textures(arch)
        for m in models:
            md = rc.read_model(self.mem, m)
            P = md["points"]
            for ms in md["meshsets"]:
                mat = md["mats"][ms["matid"]] if ms["matid"] < len(md["mats"]) else dict(texid=0, flags=0, diffuse=0xFFFFFFFF)
                fl, d = mat["flags"], mat["diffuse"]
                textured, env, alpha = bool(fl & (1 << 21)), bool(fl & (1 << 22)), bool(fl & (1 << 20))
                t = T[mat["texid"]] if textured and mat["texid"] < len(T) else None
                for (a, ka), (b, kb), (c, kc) in rc.triangles(ms):
                    A, B, C = P[a], P[b], P[c]
                    ux = [B[i] - A[i] for i in range(3)]
                    vx = [C[i] - A[i] for i in range(3)]
                    cr = (ux[1] * vx[2] - ux[2] * vx[1], ux[2] * vx[0] - ux[0] * vx[2], ux[0] * vx[1] - ux[1] * vx[0])
                    area = 0.5 * math.sqrt(sum(q * q for q in cr))
                    if area <= 0:
                        continue
                    total += area
                    if t is None:
                        pts = [((d >> 16) & 255, (d >> 8) & 255, d & 255, 255)] * len(BARY)
                    elif env or ms["uvs"] is None:
                        pts = [texel(t, (i + 0.5) / 8.0, (j + 0.5) / 8.0, fl) for i in range(8) for j in range(8)]
                    else:
                        UV = [(ms["uvs"][k][0] / 255.0, ms["uvs"][k][1] / 255.0) for k in (ka, kb, kc)]
                        pts = []
                        for s1, s2 in BARY:
                            s0 = 1.0 - s1 - s2
                            pts.append(texel(t, s0 * UV[0][0] + s1 * UV[1][0] + s2 * UV[2][0], s0 * UV[0][1] + s1 * UV[1][1] + s2 * UV[2][1], fl))
                    keep = [p for p in pts if not (alpha and p[3] < 128)]
                    if not keep:
                        continue
                    w = area / len(pts)
                    for r, g, bb, _a in keep:
                        h = H[bin_of(r, g, bb)]
                        h[0] += w
                        h[1] += w * r
                        h[2] += w * g
                        h[3] += w * bb
        return H, total

    def colour(self, arch, up, plain):
        """(hex or None, the window's bins, the share of the upgrade's area the window adds)"""
        Hu, Au = self.sample(arch, list(dict.fromkeys(up)))
        Hp, _Ap = self.sample(arch, list(dict.fromkeys(plain))) if plain else ([[0.0] * 4 for _ in range(15)], 0.0)
        add = [Hu[k][0] - Hp[k][0] for k in range(15)]

        def chroma_ok(k):
            if k >= 12 or Hu[k][0] <= 0:
                return False
            _h, s, v = colorsys.rgb_to_hsv(*(Hu[k][i] / Hu[k][0] / 255.0 for i in (1, 2, 3)))
            return s >= 0.25 and v >= 0.35
        best, bw = None, 0.0
        for k0 in range(12):
            ks = [(k0 + j) % 12 for j in range(3)]
            w = sum(max(add[k], 0.0) for k in ks if chroma_ok(k))
            if w > bw:
                best, bw = ks, w
        if best is None or Au <= 0 or bw / Au < 0.01:
            return None, "", (bw / Au if Au else 0.0)
        ws = [(max(add[k], 0.0), k) for k in best if chroma_ok(k) and add[k] > 0]
        tw = sum(w for w, _k in ws)
        hx = "%02X%02X%02X" % tuple(int(round(sum(w * Hu[k][i] / Hu[k][0] for w, k in ws) / tw)) for i in (1, 2, 3))
        return hx, "+".join(BIN_NAMES[k] for _w, k in ws), bw / Au

    def name_fill(self, pvm, name):
        for nm, _gbix, chunk in pvm:
            if nm.lower() != name:
                continue
            w, h, px = rc.decode_pvr(chunk)[:3]
            H = [[0.0, 0.0, 0.0, 0.0] for _ in range(15)]
            for i in range(w * h):
                r, g, b, a = px[4 * i:4 * i + 4]
                if a < 128 or max(r, g, b) < 77:
                    continue
                e = H[bin_of(r, g, b)]
                e[0] += 1
                e[1] += r
                e[2] += g
                e[3] += b
            top = max(range(15), key=lambda k: H[k][0])
            return "%02X%02X%02X" % tuple(int(round(H[top][i] / H[top][0])) for i in (1, 2, 3)) if H[top][0] else None
        return None


def texel(t, u, v, flags):
    w, h, px = t
    u = min(max(u, 0.0), 0.999999) if flags & 0x10000 else u % 1.0
    v = min(max(v, 0.0), 0.999999) if flags & 0x8000 else v % 1.0
    i = 4 * (int(v * h) * w + int(u * w))
    return px[i], px[i + 1], px[i + 2], px[i + 3]


def bin_of(r, g, b):
    hh, s, v = colorsys.rgb_to_hsv(r / 255.0, g / 255.0, b / 255.0)
    if v < 0.20:
        return 12
    if s < 0.25:
        return 14 if v >= 0.75 else 13
    return int(((hh * 360.0 + 15.0) % 360.0) // 30.0)


def measure_all(m):
    """{upgrade: (measured hex or None, window, share)}"""
    out = {}
    for name, arch, swaps in SWAPS:
        up, plain = [], []
        for field, p, u in swaps:
            up += m.part(field, u)
            plain += m.part(field, p)
        out[name] = m.colour(arch, up, plain)
    for name, arch, addr in ADDONS:
        out[name] = m.colour(arch, m.chain(addr), [])
    table = [m.mem.u32(LURE_TABLE + 4 * i) for i in range(5)]
    plain = m.chain(table[0])
    for i, name in enumerate(LURES):
        out[name] = m.colour("FISHING", m.chain(table[i + 1]), plain)
    for name in NO_MODEL:
        out[name] = (None, "no model", 0.0)
    return out


def parse_lua(path, table):
    """{key: "RRGGBB" | False} of the Lua table `table` = { ... } (string or false values); keys may be names or [numbers]"""
    src = re.sub(r"--[^\n]*", "", open(path, encoding="utf-8").read())
    mt = re.search(r"(?m)^\s*" + re.escape(table) + r"\s*=\s*\{", src)
    if not mt:
        raise SystemExit("%s: no %s table" % (path, table))
    depth, i = 1, mt.end()
    while i < len(src) and depth:
        depth += {"{": 1, "}": -1}.get(src[i], 0)
        i += 1
    body = src[mt.end():i - 1]
    out = {}
    for k, v in re.findall(r"(\[\s*\d+\s*\]|[A-Za-z_][A-Za-z0-9_]*)\s*=\s*(\"[0-9A-Fa-f]{6}\"|false)", body):
        key = int(k.strip("[] ")) if k.startswith("[") else k
        out[key] = False if v == "false" else v.strip('"').upper()
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--gdi", help="the owner's .gdi (default: $SA1_TEST_GDI, else the extracted sadc/disc)")
    ap.add_argument("--disc", help="an extracted disc folder (1ST_READ.BIN, SONICADV/)")
    ap.add_argument("--check", metavar="SH_UPGRADES_LUA", help="compare U.COLORS and U.SLOT_COLOR of G-SA1's feat/sh_upgrades.lua; exit 1 on a difference")
    a = ap.parse_args(argv)
    gdi, folder = a.gdi, a.disc
    if not gdi and not folder:
        gdi, folder = default_source()
    if not gdi and not folder:
        raise SystemExit("no disc: give --gdi or --disc (or set SA1_TEST_GDI)")
    m = Measure(Disc(gdi=gdi, folder=folder))
    print("upgrade_colours: the disc %s (1ST_READ.BIN SHA-1 %s)" % ("image" if gdi else "folder", FIRST_READ_SHA1[:12]))
    meas = measure_all(m)
    want = {}
    print("the upgrades' names (measured: select 4.6.1's method; the owner's choices fixed):")
    for name in ORDER:
        hx, window, share = meas[name]
        if name in OWNER:
            v, why = OWNER[name]
            want[name] = v
            print("  %-18s %-7s THE OWNER'S CHOICE (%s); measured %s%s" % (name, v if v else "false", why, hx or "none of its own",
                                                                       (" (%s, %.1f %% of its area)" % (window, 100 * share)) if hx else ""))
        else:
            want[name] = hx if hx else False
            print("  %-18s %-7s measured: %s" % (name, hx or "false", ("%s, %.1f %% of its area" % (window, 100 * share)) if hx else "none of its own (%s)" % window))
    pvm = rc.parse_pvm(prs.decompress(m.disc.read("SONICADV/M_CHNAM.PRS")))
    slots = {}
    print("the characters' names (M_CHNAM.PRS, by Character Select's slot):")
    for nm, slot in NAMES:
        slots[slot] = m.name_fill(pvm, nm)
        print("  slot %d %-13s %s" % (slot, nm, slots[slot]))
    if not a.check:
        return 0
    got = parse_lua(a.check, "U.COLORS")
    got_slots = parse_lua(a.check, "U.SLOT_COLOR")
    bad = []
    for name in ORDER:
        if got.get(name, "missing") != want[name]:
            bad.append("%s: the Lua %s, the disc %s" % (name, str(got.get(name, "missing")).replace("False", "false"), str(want[name]).replace("False", "false")))
    for k in got:
        if k not in want:
            bad.append("%s: in the Lua, not an upgrade" % k)
    for slot, hx in slots.items():
        if got_slots.get(slot) != hx:
            bad.append("slot %d: the Lua %s, the disc %s" % (slot, got_slots.get(slot), hx))
    print("--check %s: %d of %d upgrade colours and %d of %d slot colours equal" % (a.check, len(ORDER) - sum(1 for b in bad if not b.startswith("slot")), len(ORDER),
                                                                             len(slots) - sum(1 for b in bad if b.startswith("slot")), len(slots)))
    for b in bad:
        print("  DIFFERS " + b)
    print("C1 %s" % ("ok" if not bad else "FAILED"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
