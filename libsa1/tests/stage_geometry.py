#!/usr/bin/env python3
"""How SA1's own stages are built where G-SA1 has trouble (ramps, thin floors, long triangles): statistics of the
collidable triangles of the landtable dumps the harness wrote (sadc/build/traces/land/*.obj, one object per COL entry,
world space, the game's winding). Diagnostics for sadc/docs/research/gsa1_ramp_freeze.md; reads only.

    py -3.12 tests/stage_geometry.py [--land DIR]

Per stage:
  shape     longest edge, bounding radius about the centroid (what 8C028BFE's NEAR test uses), needles (width / length
            below 0.05);
  slopes    walkable triangles (|n.y| >= 0.5) by slope; at every edge two walkable triangles share, the angle between
            their normals (the kink a character crosses), in particular flat (< 5 deg) next to steep (>= 20 deg);
  undersides  walkable triangles with a down-facing triangle below them within 3 / 6 / 12 units (the underside of a
            thin floor that the land collision reaches from above).
"""
import argparse
import math
import os
import re
from collections import defaultdict


def load_obj(path):
    tris = []
    verts = []
    flags = 0
    rx = re.compile(r"^o col\d+_([0-9A-Fa-f]+)")
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            if line.startswith("v "):
                verts.append(tuple(float(x) for x in line.split()[1:4]))
            elif line.startswith("f "):
                i = [int(x.split("/")[0]) - 1 for x in line.split()[1:4]]
                tris.append((verts[i[0]], verts[i[1]], verts[i[2]], flags))
            elif line.startswith("o "):
                m = rx.match(line)
                flags = int(m.group(1), 16) if m else 0
    return tris


def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def norm(a):
    return math.sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2])


def pct(xs, p):
    if not xs:
        return float("nan")
    xs = sorted(xs)
    return xs[min(len(xs) - 1, int(p / 100.0 * len(xs)))]


def analyse(name, tris):
    info = []
    for (a, b, c, fl) in tris:
        n = cross(sub(b, a), sub(c, a))
        l = norm(n)
        if l <= 0:
            continue
        n = (n[0] / l, n[1] / l, n[2] / l)
        e = [norm(sub(b, a)), norm(sub(c, b)), norm(sub(a, c))]
        cen = ((a[0] + b[0] + c[0]) / 3, (a[1] + b[1] + c[1]) / 3, (a[2] + b[2] + c[2]) / 3)
        r = max(norm(sub(a, cen)), norm(sub(b, cen)), norm(sub(c, cen)))
        width = l / max(e)          # the altitude onto the longest edge
        info.append(dict(v=(a, b, c), n=n, e=e, r=r, w=width, cen=cen, fl=fl, area=0.5 * l))
    walk = [t for t in info if abs(t["n"][1]) >= 0.5]
    up = sum(1 for t in walk if t["n"][1] > 0)
    print("== %s: %d collidable triangles, %d walkable (|n.y| >= 0.5; %d of them face +y)" % (name, len(info), len(walk), up))
    longest = [max(t["e"]) for t in info]
    radius = [t["r"] for t in info]
    needles = sum(1 for t in info if t["w"] / max(t["e"]) < 0.05)
    print("  shape: longest edge p50 %.1f p90 %.1f p99 %.1f max %.1f; bounding radius p50 %.1f p90 %.1f p99 %.1f max %.1f; "
          "needles (width < 5%% of length) %d (%.1f%%)" % (pct(longest, 50), pct(longest, 90), pct(longest, 99), max(longest),
                                                           pct(radius, 50), pct(radius, 90), pct(radius, 99), max(radius),
                                                           needles, 100.0 * needles / max(1, len(info))))
    # slopes of walkable triangles facing up (the majority orientation)
    sign = 1 if up >= len(walk) - up else -1
    floors = [t for t in walk if t["n"][1] * sign > 0]
    bins = [(0, 5), (5, 15), (15, 25), (25, 35), (35, 45), (45, 60)]
    area_by = defaultdict(float)
    for t in floors:
        s = math.degrees(math.acos(min(1.0, abs(t["n"][1]))))
        for lo, hi in bins:
            if lo <= s < hi:
                area_by[(lo, hi)] += t["area"]
    tot = sum(area_by.values()) or 1.0
    print("  floor area by slope: " + ", ".join("%d-%d deg %.1f%%" % (lo, hi, 100.0 * area_by[(lo, hi)] / tot) for lo, hi in bins))
    steep = [t for t in floors if math.degrees(math.acos(min(1.0, abs(t["n"][1])))) >= 20.0]
    if steep:
        se = [max(t["e"]) for t in steep]
        print("  floors of 20 deg or more: %d, longest edge p50 %.1f p90 %.1f max %.1f" % (len(steep), pct(se, 50),
                                                                                         pct(se, 90), max(se)))
    # kinks at shared edges between floors
    key = lambda p: (round(p[0], 1), round(p[1], 1), round(p[2], 1))
    edges = defaultdict(list)
    for i, t in enumerate(floors):
        v = t["v"]
        for k in range(3):
            a, b = key(v[k]), key(v[(k + 1) % 3])
            edges[(a, b) if a < b else (b, a)].append(i)
    kinks, flat_steep = [], []
    for e, ts in edges.items():
        if len(ts) != 2:
            continue
        n1, n2 = floors[ts[0]]["n"], floors[ts[1]]["n"]
        d = max(-1.0, min(1.0, n1[0] * n2[0] + n1[1] * n2[1] + n1[2] * n2[2]))
        k = math.degrees(math.acos(d))
        kinks.append(k)
        s1 = math.degrees(math.acos(min(1.0, abs(n1[1]))))
        s2 = math.degrees(math.acos(min(1.0, abs(n2[1]))))
        if min(s1, s2) < 5.0 and max(s1, s2) >= 20.0:
            flat_steep.append(max(s1, s2))
    if kinks:
        print("  kinks between neighbouring floors (%d shared edges): p50 %.1f p90 %.1f p99 %.1f max %.1f deg; above 10 deg "
              "%.1f%%, above 20 deg %.1f%%" % (len(kinks), pct(kinks, 50), pct(kinks, 90), pct(kinks, 99), max(kinks),
                                                100.0 * sum(1 for k in kinks if k > 10) / len(kinks),
                                                100.0 * sum(1 for k in kinks if k > 20) / len(kinks)))
        print("  a flat floor (< 5 deg) right next to a slope of 20 deg or more: %d edges%s" % (
            len(flat_steep), (" (slopes p50 %.0f, max %.0f deg)" % (pct(flat_steep, 50), max(flat_steep))) if flat_steep else ""))
    # undersides within reach
    cell = 16.0
    grid = defaultdict(list)
    downs = [t for t in info if t["n"][1] * sign < -0.5]
    for i, t in enumerate(downs):
        xs = [p[0] for p in t["v"]]
        zs = [p[2] for p in t["v"]]
        for gx in range(int(math.floor(min(xs) / cell)), int(math.floor(max(xs) / cell)) + 1):
            for gz in range(int(math.floor(min(zs) / cell)), int(math.floor(max(zs) / cell)) + 1):
                grid[(gx, gz)].append(i)

    def inside_xz(t, x, z):
        (a, b, c) = t["v"]
        d = (b[0] - a[0]) * (c[2] - a[2]) - (b[2] - a[2]) * (c[0] - a[0])
        if abs(d) < 1e-9:
            return None
        l1 = ((b[0] - x) * (c[2] - z) - (b[2] - z) * (c[0] - x)) / d
        l2 = ((c[0] - x) * (a[2] - z) - (c[2] - z) * (a[0] - x)) / d
        l3 = 1 - l1 - l2
        if l1 < 0 or l2 < 0 or l3 < 0:
            return None
        return l1 * a[1] + l2 * b[1] + l3 * c[1]

    within = {3: 0, 6: 0, 12: 0}
    for t in floors:
        x, y, z = t["cen"]
        best = None
        for i in grid.get((int(math.floor(x / cell)), int(math.floor(z / cell))), []):
            yy = inside_xz(downs[i], x, z)
            if yy is not None and yy < y - 0.01:
                best = y - yy if best is None else min(best, y - yy)
        if best is not None:
            for k in within:
                within[k] += best <= k
    print("  floors with a down-facing triangle below them within 3 / 6 / 12 units: %d / %d / %d (of %d floors)" % (
        within[3], within[6], within[12], len(floors)))


def main():
    ap = argparse.ArgumentParser()
    lib = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    ap.add_argument("--land", default=os.path.join(os.path.dirname(lib), "sadc", "build", "traces", "land"))
    a = ap.parse_args()
    names = {"L01A0": "Emerald Coast 1", "L01A2": "Emerald Coast 3", "L03A1": "Twinkle Park 2", "L04A0": "Speed Highway 1",
             "L05A2": "Red Mountain 3", "L10A0": "Final Egg 1"}
    for fn in sorted(os.listdir(a.land)):
        if fn.endswith(".obj"):
            stem = fn[:-4]
            analyse("%s (%s)" % (stem, names.get(stem, "?")), load_obj(os.path.join(a.land, fn)))


main()
