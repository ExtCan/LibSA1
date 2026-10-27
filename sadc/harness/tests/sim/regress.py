#!/usr/bin/env python3
"""Reference-simulator regression: run every input script of this folder
(default stage Emerald Coast act 1, no objects) and compare with the stored
reference trace.

    py -3.12 harness/tests/sim/regress.py                 # check (creates missing references)
    py -3.12 harness/tests/sim/regress.py --update        # re-record all references
    py -3.12 harness/tests/sim/regress.py --route         # natively routed decompiled code vs the references
    py -3.12 harness/tests/sim/regress.py --v1-compat     # also: the v1 part of each new trace equals <name>.v1.bin
    py -3.12 harness/tests/sim/regress.py --exe harness/build/sa1h.exe --args "--level 1 --act 0" --only ec1_stand
    py -3.12 harness/tests/sim/regress.py --extra "--fast -v"         # options added to EVERY script's command
    py -3.12 harness/tests/sim/regress.py --ref-suffix .noscreen      # the old references (before 2026-10-20)
    py -3.12 harness/tests/sim/regress.py --refdir DIR --outdir DIR2   # other reference / output folders

References live in build/traces/ref/ (derived from the user's game files:
never committed).  A script may override the stage with a first-line comment
"# args: --level 2 --act 0 ..."; that line replaces --args, while --extra is
appended to every script's command, "# args:" line or not.

The screen (header flag 8, docs/research/visibility_fidelity.md): since
2026-10-20 the references are recorded with the screen set up (njInitSystem's
view part; trace's default, --no-screen opts out).  The references before
that have the zero screen; they are kept as <name>.noscreen.bin (and .csv),
and --update moves a version-2 reference without flag 8 there before it
records the new one.  A check runs the build in the reference's screen mode:
with the screen for a reference with flag 8, with --no-screen for one without
(an --extra --screen / --no-screen overrides that); a build that cannot do
what a reference needs is reported, not run.

Trace versions (harness/src/sa1_trace.h): references are version 2.  When
--update replaces a version-1 reference, the old file is kept as
<name>.v1.bin (and .v1.csv).  A build whose trace command only knows version 1
is checked against <name>.v1.bin.
"""
import argparse
import glob
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
HARNESS = os.path.dirname(os.path.dirname(HERE))
ROOT = os.path.dirname(HARNESS)
sys.path.insert(0, os.path.join(HARNESS, "tools"))
import sa1trace  # noqa: E402


def keep_as(refdir, name, suffix):
    """move <name>.bin / .csv to <name><suffix>.bin / .csv unless that file exists"""
    ref = os.path.join(refdir, name + ".bin")
    old = os.path.join(refdir, name + suffix + ".bin")
    if not os.path.exists(ref) or os.path.exists(old):
        return False
    os.replace(ref, old)
    if os.path.exists(os.path.join(refdir, name + ".csv")):
        os.replace(os.path.join(refdir, name + ".csv"), os.path.join(refdir, name + suffix + ".csv"))
    return True


def keep_v1(refdir, name):
    """before a reference is re-recorded: keep a version-1 <name>.bin as <name>.v1.bin"""
    ref = os.path.join(refdir, name + ".bin")
    if os.path.exists(ref) and sa1trace.version_of(ref) == 1:
        return keep_as(refdir, name, ".v1")
    return False


def keep_noscreen(refdir, name):
    """before a reference is re-recorded with the screen: keep a version-2 <name>.bin without
    header flag 8 (the zero screen of the references before 2026-10-20) as <name>.noscreen.bin"""
    ref = os.path.join(refdir, name + ".bin")
    if (os.path.exists(ref) and sa1trace.version_of(ref) >= 2
            and not sa1trace.flags_of(ref) & sa1trace.FLAG_SCREEN):
        return keep_as(refdir, name, ".noscreen")
    return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", default=os.path.join(HARNESS, "build", "sa1h.exe"))
    ap.add_argument("--args", default="--level 1 --act 0 --no-objects",
                    help="stage options of the scripts without a '# args:' line")
    ap.add_argument("--extra", default="",
                    help="options appended to every script's trace command, '# args:' line or not")
    ap.add_argument("--update", action="store_true")
    ap.add_argument("--route", action="store_true")
    ap.add_argument("--v1-compat", action="store_true",
                    help="also compare the v1 part of every new trace with build/traces/ref/<name>.v1.bin "
                         "(sa1trace.py compat: the v1 decoded fields must be byte-identical)")
    ap.add_argument("--only", help="comma-separated script names")
    ap.add_argument("--outdir", default=os.path.join(ROOT, "build", "traces", "run"))
    ap.add_argument("--refdir", default=os.path.join(ROOT, "build", "traces", "ref"))
    ap.add_argument("--ref-suffix", default="",
                    help="the references are <name><SUFFIX>.bin (.noscreen: the zero-screen references); "
                         "--update records under that name and keeps no older file")
    a = ap.parse_args()
    a.exe = os.path.abspath(a.exe)
    refdir = os.path.abspath(a.refdir)
    os.makedirs(refdir, exist_ok=True)
    os.makedirs(a.outdir, exist_ok=True)
    ver = sa1trace.exe_trace_version(a.exe)
    if a.update and ver < 2:
        print("%s writes version-1 traces; the references are version 2: rebuild it first" % a.exe)
        return 1
    extra_all = a.extra.split()
    screen_forced = "--screen" in extra_all or "--no-screen" in extra_all
    if a.update and not screen_forced and sa1trace.exe_screen_mode(a.exe) != 2:
        print("%s does not set up the screen by default; the references have it (flag 8): rebuild it first, "
              "or pass --extra --screen (or --extra --no-screen with --ref-suffix .noscreen)" % a.exe)
        return 1
    only = set(a.only.split(",")) if a.only else None
    fails = total = 0
    for script in sorted(glob.glob(os.path.join(HERE, "*.txt"))):
        name = os.path.splitext(os.path.basename(script))[0]
        if only and name not in only:
            continue
        total += 1
        extra = a.args.split()
        first = open(script).readline()
        if first.startswith("# args:"):
            extra = first[len("# args:"):].split()
        ref = os.path.join(refdir, name + a.ref_suffix)
        cmd = [a.exe, "trace", "--input", "@" + script, "--print", "0"] + extra + extra_all
        if a.route:
            cmd.append("--route")
        out = os.path.join(a.outdir, name)
        notes = []
        if a.update or not os.path.exists(ref + ".bin"):
            if a.route:
                print("%-20s no reference (record one without --route first)" % name)
                fails += 1
                continue
            if not a.ref_suffix:
                if keep_v1(refdir, name):
                    notes.append("v1 kept as %s.v1.bin" % name)
                elif "--no-screen" not in extra_all and keep_noscreen(refdir, name):
                    notes.append("zero-screen reference kept as %s.noscreen.bin" % name)
            r = subprocess.run(cmd + ["--out", ref], capture_output=True, text=True)
            status = "recorded" if r.returncode == 0 else "FAILED to record"
            out = ref
        else:
            chk = ref + ".bin" if a.ref_suffix else sa1trace.reference_for(a.exe, refdir, name)
            if not chk or not os.path.exists(chk):
                print("%-20s no reference this build can read (it writes version %d)" % (name, ver))
                fails += 1
                continue
            if chk != ref + ".bin":
                notes.append("vs " + os.path.basename(chk))
            if not screen_forced and sa1trace.version_of(chk) >= 2:
                want = bool(sa1trace.flags_of(chk) & sa1trace.FLAG_SCREEN)
                sargs = sa1trace.screen_args(a.exe, want)
                if sargs is None:
                    print("%-20s the reference has the screen (flag 8), this build cannot set it up" % name)
                    fails += 1
                    continue
                if not want:
                    notes.append("zero screen")
                cmd += sargs
            r = subprocess.run(cmd + ["--out", out, "--check", chk], capture_output=True, text=True)
            status = "ok" if r.returncode == 0 else "DIFFERS"
        rc = r.returncode
        if a.v1_compat and rc == 0:
            old = os.path.join(refdir, name + ".v1.bin")
            if not os.path.exists(old):
                notes.append("no %s.v1.bin" % name)
            elif sa1trace.version_of(out + ".bin") < 2:
                notes.append("v1 output")
            else:
                ok, msg = sa1trace.compat(old, out + ".bin")
                notes.append(("v1 compat ok" if ok else "V1 COMPAT FAILS: " + msg))
                if not ok:
                    rc = 1
                    status = "DIFFERS"
        last = [l for l in (r.stdout + r.stderr).splitlines() if l.startswith(("check:", "sim:", "  step"))]
        print("%-20s %-10s %s" % (name, status, " | ".join(notes + last[-3:])))
        if rc:
            fails += 1
    print("%d/%d %s" % (total - fails, total, "recorded/identical" if not fails else "ok, %d failed" % fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
