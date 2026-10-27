#!/usr/bin/env python3
"""Reader for the 2D draw logs of `sa1h trace --log-2d FILE` (harness/src/t_sim.c, docs/research/gsa1_070_refs.md).

One line per njDrawSprite2D (8C63EEB4, kind S) / njDrawPolygon2D (8C63EBC8, kind P) / njDrawTexture (8C63ED08, kind T:
the menu screens' quads, round 0.9.2) / njDrawPolygon (8C63EC58, kind G) call, written at the call before the original
runs; floats are bit patterns (8 hex digits).  The columns are named in the file's header lines.  `sa1h menu --log-2d`
(round 0.9.2, docs/research/gsa1_090_charselect.md CS2 / RC6) writes the same format for the ADVERTISE menu, phase M,
step = the menu frame; its references are build/traces/ref/charsel_*.d2d.txt (scripts harness/tests/sim/menu/).

    py -3.12 harness/tools/draw2d.py info  FILE                     # draws per kind / phase / caller (PR), steps covered
    py -3.12 harness/tools/draw2d.py dump  FILE [--from S] [--to S] [--pr ADDR] [--kind S|P|T|G]  # decoded, one per line
    py -3.12 harness/tools/draw2d.py diff  A B [--ignore-z] [--ignore COL,..] [--lib] [--max N]
                                                                     # draw by draw (exit 1 when they differ)
    py -3.12 harness/tools/draw2d.py check [--exe EXE] [--only N,..]  # every reference with a sidecar NAME.d2d.txt:
                                                                     # re-run its script with --log-2d and --check
    py -3.12 harness/tools/draw2d.py menu-record [--exe EXE] [--only N,..]   # (re-)record the menu references

`check` is the 2D counterpart of harness/tests/sim/regress.py: for each build/traces/ref/NAME.d2d.txt it runs
harness/tests/sim/NAME.txt with the script's "# args:" line, --check build/traces/ref/NAME.bin (the trace and its
sidecars) and --log-2d build/traces/run/NAME.d2d.txt, and compares the two logs line by line (header comments aside).
A reference whose script is harness/tests/sim/menu/NAME.txt is a menu reference: `sa1h menu` with the script's
"# args:", --log-2d and --log, and both its 2D log and its menu log (build/traces/ref/NAME.menu.txt: state, sound,
2D-helper lines) must be identical.

The sprite's p.z is not set by every caller (Big's gauge 8C0E72DA leaves stack garbage there; the C has PC_ONLY(p.z =
0)), so --ignore-z skips it in a diff against a libsa1 log. --lib skips every column a libsa1 log written in this
format cannot reproduce (docs/research/gsa1_070_refs.md 4.2): obj and the point-list pointers (stack addresses), pr
(the guest caller), frame (B_8C752BE8: the harness never runs the frame advance 8C08A5BE), sub (B_8C754EC8 after the
pair), p.z, and ang unless the sprite's attr has NJD_SPRITE_ANGLE (1).
"""
import argparse
import glob
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
HARNESS = os.path.dirname(HERE)
ROOT = os.path.dirname(HARNESS)

COMMON = ["kind", "phase", "step", "seq", "frame", "sub", "exec", "pr", "obj", "n", "pri", "attr", "texlist",
          "cmat_a", "cmat_r", "cmat_g", "cmat_b", "blend", "ctl3d", "attr_and", "attr_or"]
SPRITE = ["p.x", "p.y", "p.z", "sx", "sy", "ang", "tlist", "tanim", "ta.sx", "ta.sy", "ta.cx", "ta.cy", "u1", "v1",
          "u2", "v2", "texid", "ta.attr", "texname", "name"]
POLY = ["p", "col", "tex", "num"]
TEXTURE = ["gbix", "texname"]     # kind T: then n vertices x,y,z,u,v,argb; kind G: n vertices x,y,z,argb
FLOATS = {"pri", "cmat_a", "cmat_r", "cmat_g", "cmat_b", "p.x", "p.y", "p.z", "sx", "sy"}


def f32(h):
    return struct.unpack("<f", struct.pack("<I", int(h, 16)))[0]


def parse_line(line):
    t = line.split()
    d = dict(zip(COMMON, t[:len(COMMON)]))
    rest = t[len(COMMON):]
    if d["kind"] == "S":
        d.update(zip(SPRITE, rest))
    elif d["kind"] == "T":
        d.update(zip(TEXTURE, rest[:2]))
        d["verts"] = rest[2:]
    elif d["kind"] == "G":
        d["verts"] = rest
    else:
        d.update(zip(POLY, rest[:4]))
        d["verts"] = rest[4:]
    d["_raw"] = t
    return d


def kind_cols(kind):
    return COMMON + {"S": SPRITE, "T": TEXTURE, "G": []}.get(kind, POLY)


def read(path):
    head, rows = [], []
    with open(path, encoding="utf-8") as f:
        for line in f:
            if line.startswith("#"):
                head.append(line.rstrip("\n"))
            elif line.strip():
                rows.append(parse_line(line))
    return head, rows


def fmt(d):
    out = []
    for k in COMMON[1:]:
        v = d.get(k)
        if k in FLOATS and v not in (None, "-"):
            out.append("%s=%g" % (k, f32(v)))
        elif k in ("exec", "pr", "obj", "texlist") and v:
            out.append("%s=%s" % (k, v))
        elif k in ("step", "seq", "n", "sub", "frame", "attr", "blend"):
            out.append("%s=%s" % (k, v))
    if d["kind"] == "S":
        for k in SPRITE:
            v = d.get(k)
            if v is None:
                continue
            out.append("%s=%s" % (k, "%g" % f32(v) if k in FLOATS and v != "-" else v))
    elif d["kind"] == "T":
        out += ["%s=%s" % (k, d.get(k)) for k in TEXTURE]
        for v in d.get("verts", []):
            x, y, z, u, vv, c = v.split(",")
            out.append("(%g,%g z %g uv %g,%g %s)" % (f32(x), f32(y), f32(z), f32(u), f32(vv), c))
    elif d["kind"] == "G":
        for v in d.get("verts", []):
            x, y, z, c = v.split(",")
            out.append("(%g,%g z %g %s)" % (f32(x), f32(y), f32(z), c))
    else:
        out += ["%s=%s" % (k, d.get(k)) for k in POLY]
        for v in d.get("verts", []):
            x, y, c, uv = v.split(",")
            out.append("(%g,%g %s %s)" % (f32(x), f32(y), c, uv))
    return "%s %s" % (d["kind"], " ".join(out))


def cmd_info(a):
    head, rows = read(a.file)
    print("\n".join(head[:1]))
    if not rows:
        print("no draws")
        return 0
    steps = sorted(set(int(r["step"]) for r in rows))
    print("%d draws, steps %d..%d (%d steps with draws)" % (len(rows), steps[0], steps[-1], len(steps)))
    by = {}
    for r in rows:
        k = (r["kind"], r["phase"], r["pr"])
        by.setdefault(k, [0, int(r["step"]), int(r["step"])])
        e = by[k]
        e[0] += 1
        e[2] = int(r["step"])
    print("kind phase caller(PR)  draws  first..last step")
    for k in sorted(by, key=lambda k: (k[1], k[0], k[2])):
        e = by[k]
        print("  %s    %s    %s  %6d  %d..%d" % (k[0], k[1], k[2], e[0], e[1], e[2]))
    return 0


def cmd_dump(a):
    head, rows = read(a.file)
    for r in rows:
        s = int(r["step"])
        if a.from_ is not None and s < a.from_ or a.to is not None and s > a.to:
            continue
        if a.pr and r["pr"].upper() != a.pr.upper():
            continue
        if a.kind and r["kind"] != a.kind:
            continue
        print(fmt(r))
    return 0


LIB_IGNORE = {"obj", "pr", "frame", "sub", "p.z", "p", "col", "tex"}


def key_cols(r, ignore_z, ignore=()):
    t = list(r["_raw"])
    cols = kind_cols(r["kind"])
    skip = set(ignore) | ({"p.z"} if ignore_z else set())
    if r["kind"] == "S" and "ang!" in skip and not int(r.get("attr", "0"), 16) & 1:
        skip.add("ang")
    for j, c in enumerate(cols):
        if c in skip and j < len(t):
            t[j] = "*"
    return t


def cmd_diff(a):
    ha, ra = read(a.a)
    hb, rb = read(a.b)
    nd = 0
    for i in range(max(len(ra), len(rb))):
        x = ra[i] if i < len(ra) else None
        y = rb[i] if i < len(rb) else None
        if x is None or y is None:
            print("draw %d: only in %s" % (i, a.a if y is None else a.b))
            nd += 1
        else:
            ign = [c for c in (a.ignore or "").split(",") if c] + (sorted(LIB_IGNORE) + ["ang!"] if a.lib else [])
            kx, ky = key_cols(x, a.ignore_z, ign), key_cols(y, a.ignore_z, ign)
            if kx != ky:
                cols = kind_cols(x["kind"])
                diffs = ["%s %s/%s" % (cols[j] if j < len(cols) else "v%d" % (j - len(cols)),
                                       kx[j] if j < len(kx) else "-", ky[j] if j < len(ky) else "-")
                         for j in range(max(len(kx), len(ky)))
                         if (kx[j] if j < len(kx) else None) != (ky[j] if j < len(ky) else None)]
                print("draw %d (step %s %s seq %s): %s" % (i, x["step"], x["phase"], x["seq"], ", ".join(diffs[:8])))
                nd += 1
        if nd >= a.max:
            print("... (stopped after %d)" % nd)
            break
    print("%d draws vs %d: %s" % (len(ra), len(rb), "identical" if not nd else "%d differ" % nd))
    return 1 if nd else 0


def cmd_check(a):
    exe = os.path.abspath(a.exe)
    refdir = os.path.join(ROOT, "build", "traces", "ref")
    outdir = os.path.join(ROOT, "build", "traces", "run")
    os.makedirs(outdir, exist_ok=True)
    only = set(a.only.split(",")) if a.only else None
    fails = total = 0
    for side in sorted(glob.glob(os.path.join(refdir, "*.d2d.txt"))):
        name = os.path.basename(side)[:-len(".d2d.txt")]
        if only and name not in only:
            continue
        script = os.path.join(HARNESS, "tests", "sim", name + ".txt")
        menu = os.path.join(HARNESS, "tests", "sim", "menu", name + ".txt")
        if not os.path.exists(script) and os.path.exists(menu):
            total += 1
            ok, msg = check_menu(exe, refdir, outdir, name, menu)
            print("%-20s %-8s %s" % (name, "ok" if ok else "DIFFERS", msg))
            fails += 0 if ok else 1
            continue
        if not os.path.exists(script):
            print("%-20s no script %s" % (name, script))
            fails += 1
            continue
        total += 1
        first = open(script).readline()
        extra = first[len("# args:"):].split() if first.startswith("# args:") else "--level 1 --act 0 --no-objects".split()
        out = os.path.join(outdir, name)
        cmd = [exe, "trace", "--input", "@" + script, "--print", "0"] + extra + \
            ["--out", out, "--check", os.path.join(refdir, name + ".bin"), "--log-2d", out + ".d2d.txt"]
        r = subprocess.run(cmd, capture_output=True, text=True, cwd=ROOT)
        _, ra = read(side)
        try:
            _, rb = read(out + ".d2d.txt")
        except OSError:
            rb = None
        same = rb is not None and [x["_raw"] for x in ra] == [y["_raw"] for y in rb]
        ok = r.returncode == 0 and same
        print("%-20s %-8s trace %s, 2D log %s (%d draws)" % (name, "ok" if ok else "DIFFERS",
              "identical" if r.returncode == 0 else "DIFFERS", "identical" if same else "DIFFERS", len(ra)))
        fails += 0 if ok else 1
    print("%d/%d identical" % (total - fails, total))
    return 1 if fails else 0


def menu_lines(path):
    """a menu log's lines without its header comments"""
    with open(path, encoding="utf-8") as f:
        return [l.rstrip("\r\n") for l in f if not l.startswith("#")]


def menu_cmd(exe, menu_script, out):
    """the `sa1h menu` command of a menu reference script (its first line "# args: ...")"""
    first = open(menu_script).readline()
    extra = first[len("# args:"):].split() if first.startswith("# args:") else []
    return [exe, "menu", "--input", "@" + menu_script] + extra + ["--log", out + ".menu.txt", "--log-2d",
                                                                 out + ".d2d.txt"]


def check_menu(exe, refdir, outdir, name, menu_script):
    """a menu reference: re-run it, compare the 2D log and the menu log with build/traces/ref/NAME.{d2d,menu}.txt"""
    out = os.path.join(outdir, name)
    r = subprocess.run(menu_cmd(exe, menu_script, out), capture_output=True, text=True, cwd=ROOT)
    _, ra = read(os.path.join(refdir, name + ".d2d.txt"))
    try:
        _, rb = read(out + ".d2d.txt")
    except OSError:
        rb = None
    same = rb is not None and [x["_raw"] for x in ra] == [y["_raw"] for y in rb]
    ref_menu = os.path.join(refdir, name + ".menu.txt")
    try:
        same_menu = menu_lines(ref_menu) == menu_lines(out + ".menu.txt")
    except OSError:
        same_menu = False
    ok = r.returncode == 0 and same and same_menu
    return ok, "menu run %s, 2D log %s (%d draws), menu log %s" % (
        "ok" if r.returncode == 0 else "FAILED", "identical" if same else "DIFFERS", len(ra),
        "identical" if same_menu else "DIFFERS")


def cmd_menu_record(a):
    """record (or re-record) the menu references: harness/tests/sim/menu/NAME.txt -> build/traces/ref/NAME.{d2d,menu}.txt
    (+ NAME.cov.txt: the coverage from the ADVERTISE load)"""
    exe = os.path.abspath(a.exe)
    refdir = os.path.join(ROOT, "build", "traces", "ref")
    only = set(a.only.split(",")) if a.only else None
    fails = 0
    for script in sorted(glob.glob(os.path.join(HARNESS, "tests", "sim", "menu", "*.txt"))):
        name = os.path.splitext(os.path.basename(script))[0]
        if only and name not in only:
            continue
        out = os.path.join(refdir, name)
        cmd = menu_cmd(exe, script, out) + ["--coverage", out + ".cov.txt", "-1"]
        r = subprocess.run(cmd, capture_output=True, text=True, cwd=ROOT)
        last = [l for l in r.stdout.splitlines() if l.startswith("menu:")][-3:]
        print("%-20s %s  %s" % (name, "recorded" if r.returncode == 0 else "FAILED", " | ".join(last)))
        fails += r.returncode != 0
    return 1 if fails else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("info")
    p.add_argument("file")
    p = sub.add_parser("dump")
    p.add_argument("file")
    p.add_argument("--from", dest="from_", type=int)
    p.add_argument("--to", type=int)
    p.add_argument("--pr")
    p.add_argument("--kind", choices=["S", "P", "T", "G"])
    p = sub.add_parser("diff")
    p.add_argument("a")
    p.add_argument("b")
    p.add_argument("--ignore-z", action="store_true")
    p.add_argument("--ignore", help="comma-separated column names to skip (header names: pri, cmat_a, p.x, ...)")
    p.add_argument("--lib", action="store_true", help="skip the columns a libsa1 log cannot reproduce (see above)")
    p.add_argument("--max", type=int, default=20)
    p = sub.add_parser("check")
    p.add_argument("--exe", default=os.path.join(HARNESS, "build", "sa1h.exe"))
    p.add_argument("--only")
    p = sub.add_parser("menu-record", help="record the menu references (harness/tests/sim/menu/*.txt)")
    p.add_argument("--exe", default=os.path.join(HARNESS, "build", "sa1h.exe"))
    p.add_argument("--only")
    a = ap.parse_args()
    return {"info": cmd_info, "dump": cmd_dump, "diff": cmd_diff, "check": cmd_check,
            "menu-record": cmd_menu_record}[a.cmd](a)


if __name__ == "__main__":
    sys.exit(main())
