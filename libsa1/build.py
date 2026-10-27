#!/usr/bin/env python3
"""Build libsa1 with zig (`py -3.12 -m ziglang cc`, the ziglang Python package).

    py -3.12 build.py                        # all targets, the whole lowered game (pclower runs when needed)

Usage:
    py -3.12 build.py [--target x86_64-windows|x86-windows|x86_64-linux|all]
                      [--debug] [--werror] [--clean] [-j N] [--game DIR]
                      [--no-game | --lowered DIR | --lower TU ... [--with-rt]]
                      [--relower] [--strict-stubs] [--allow-unpinned] [--allow-conflict NAME]... [--pclower-args "..."]
                      [--test] [--rip GDI OUTDIR] [--no-link-test]

Game code (sadc docs/libsa1_integration.md 2.3, 4, 10.5):
    default        sadc/tools/pclower.py --all --profile ga64 --stub-unresolved into build/pclower/ (all 530 TUs:
                   src/1st_read, every overlay image of src/overlays (STG01, STG05, TIKAL_PROG: each with its own code
                   map, sadc docs/libsa1_integration.md 2.5) and the runtime src/rt). The run is cached: it is repeated
                   only when an input changes (pclower and gen_native, pc_cflags.txt, the symbol files, every file of
                   sadc include/, src/1st_read/, src/overlays/, config/overlays/ and src/rt/, the zig version);
                   --relower forces it. pclower's R10 pin is enforced: build.py refuses a zig other than
                   PINNED_ZIG / PINNED_CLANG of tools/pclower.py (--allow-unpinned passes through, at your risk).
    --lowered DIR  link a pclower profile directory made elsewhere (DIR/lowered/*.c, sa1g_fntab.c, ...; e.g. the
                   phase 1 spike of tests/build_spike.py)
    --lower TU...  lower only these sadc TUs into build/pclower-subset/ ("spike" = the phase 1 TUs); --with-rt
                   adds src/rt
    --no-game      the world core without game code (empty code map: ticking reports SA1_ERR_NOT_IMPLEMENTED)

    Boundary stubs: gen/gen_stubs.py turns gen/policy/stubs.tsv (one policy + evidence per function pclower
    stubs) into build/gen/sa1g_stubs_policy.c, linked instead of pclower's all-fatal sa1g_stubs.c, plus the
    runtime glue the lowered code needs (njFraction, UNK_PcWrite). A stub without a TSV line is fatal and
    reported (--strict-stubs: an error). The lowered code, the code map (sa1g_fntab.c), the R4 pcdata placement
    (sa1g_pcdata.c), the layout asserts (sa1g_layout_asserts.c) and the stubs are compiled with the one PC flag
    set (sadc tools/pc_cflags.txt) into the game-text section (src/world/sa1g_text.h), with the SHC intrinsics
    inlined from src/rt/rt_intrin.h. --debug does not change the game flags.

Tests (--test, runnable targets only: x86_64-windows / x86-windows on Windows, x86_64-linux on Linux):
    link_test, link_test_shared, intrin_test   always (unless --no-link-test)
    unit_test, spike_test                      every build (spike_test composes the world image from the disc when
                                               SA1_TEST_GDI is set and compares with build/spike/ref when present)
    world_boot_test, transplant_test           M0 / M1a / M1b / live / character API (sadc docs/libsa1_integration.md
                                               11): built by src/world/build_world_tests.py, run here with
                                               --stage-tolerant; need the whole game (no --no-game / --lower /
                                               --debug), SA1_TEST_GDI=<the user's .gdi>, sadc's reference traces
                                               build/traces/ref/ec1_stand.bin + ec1_run_jump.bin, and for
                                               transplant_test the local RAM images build/traces/m1_<trace>.step{0,1}.ram
    replay test                                tests/build_replay.py --run --trace <every reference trace it knows that
                                               has its landtable dump> (the ten: M1, M1.1 and the Phase 7 characters;
                                               V5, V6, DISC, sa1trace.py diffs; with --target all also V7), strict:
                                               every record bit-exact, the gate. Needs SA1_TEST_GDI, the references
                                               (ec1_stand and ec1_run_jump at least) and the landtable dumps
                                               build/traces/land/L<level>A<act> (.tri/.col)
    A test whose inputs are missing is skipped with a message (the build still succeeds); a failing one fails.

Outputs per target in build/<target>/ (build/<target>-debug with --debug):
    libsa1.a                    static library (define SA1_STATIC when using it)
    sa1.dll + libsa1.dll.a      (Windows) or libsa1.so (Linux): exports only the sa1_* API
    unit_test, rip_test, rip_test_shared, spike_test   tests (tests/)
    link_test, link_test_shared                        gen/linktest/link_test.c: the whole static archive
                                linked (--whole-archive) and the shared library: no unresolved symbol
    size_report.txt             sizes per component (game code, runtime, code map, stubs, world, asset layer)
build/link_report.txt: the link test of every target built (exports, CRT imports of the game objects, sizes).

Incremental: an object is rebuilt when its command line or any file in its depfile changed.
"""
import argparse
import concurrent.futures
import glob
import hashlib
import json
import os
import platform
import re
import shlex
import shutil
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(ROOT, "gen"))
import binsyms  # noqa: E402  object / binary symbol tables (link test, size report)
import gen_stubs  # noqa: E402  stubs.tsv -> C

TARGETS = {
    "x86_64-windows": "x86_64-windows-gnu",
    "x86-windows": "x86-windows-gnu",
    "x86_64-linux": "x86_64-linux-gnu",
}
TOOLS = ["unit_test", "rip_test"]
SPIKE_TUS = ["src/1st_read/code_8C098444.c", "src/1st_read/code_8C098998.c", "src/1st_read/code_8C098B0C.c"]
SPIKE_CONFLICTS = ["CreateElementalTask"]
# warnings for lowered game code: every pointer/integer mix-up is an error (a site pclower missed)
GAME_WARN = ["-Wno-unused-function", "-Wno-unused-variable", "-Wno-unused-but-set-variable",
             "-Werror=incompatible-pointer-types", "-Werror=int-conversion", "-Werror=implicit-function-declaration",
             "-Werror=int-to-pointer-cast", "-Werror=pointer-to-int-cast",
             # artefacts of the lowering: parenthesised macro expansions, C89 calls pclower made explicit (escapes)
             "-Wno-parentheses-equality", "-Wno-deprecated-non-prototype"]
GAME_FORCE = ["-DSA1G_TEXT_PRAGMA", "-include", "src/world/sa1g_text.h"]
GAME_INTRIN = ["-include", "src/rt/rt_intrin.h"]
WORLD_WARN = ["-Wall", "-Wextra", "-Wshadow", "-Wstrict-prototypes", "-Wmissing-prototypes", "-Wundef"]
# the world core holds the FP intrinsics of the game (sa1_world_intrin.c): no contraction, no RN folding
WORLD_FP = ["-ffp-contract=off", "-frounding-math", "-fno-builtin"]
# generated mathematics of the runtime that the world core needs whatever is linked (FSCA table)
RT_ALWAYS = ["src/rt/rt_fsca_q.c"]
# what game / runtime objects may import from the C library (design 8.1 post-link check)
CRT_ALLOWED = {"memcpy", "memmove", "memset", "__chkstk", "___chkstk_ms", "__chkstk_ms", "_alloca", "__alloca",
               "_chkstk", "__stack_chk_fail", "__stack_chk_guard"}
# inputs of a full lowering (relative to sadc), besides src/rt of libsa1
LOWER_INPUT_DIRS = ["include", "src/1st_read", "src/overlays", "config/overlays"]
LOWER_INPUT_FILES = ["tools/pclower.py", "harness/tools/gen_native.py", "tools/pc_cflags.txt", "config/asset_symbols.txt",
                     "build/libsa1_units.json"]


def zig(*args):
    return [sys.executable, "-m", "ziglang"] + list(args)


def run(cmd, cwd=ROOT):
    r = subprocess.run(cmd, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                       encoding="utf-8", errors="replace")
    return r.returncode, r.stdout


def runnable(key):
    host = platform.system()
    if key.endswith("windows"):
        return host == "Windows"
    return host == "Linux" and platform.machine().lower() in ("x86_64", "amd64")


def rel(p):
    return os.path.relpath(p, ROOT).replace("\\", "/")


def sha1_file(path, h):
    with open(path, "rb") as f:
        h.update(f.read())


def pc_cflags(sadc, key):
    """sadc/tools/pc_cflags.txt for one target ([all], [x86], [x86_64] sections)."""
    path = os.path.join(sadc, "tools", "pc_cflags.txt")
    arch = "x86" if key.startswith("x86-") else "x86_64"
    flags, active = [], True
    if not os.path.exists(path):
        raise SystemExit("missing %s (design 8.1: the one PC flag set)" % path)
    for line in open(path, encoding="utf-8"):
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        if line.startswith("[") and line.endswith("]"):
            secs = line[1:-1].split()
            active = "all" in secs or arch in secs
            continue
        if active:
            flags += line.split()
    return flags


# ---- toolchain pin (pclower R10) -------------------------------------------------------------------------------
def toolchain(sadc, allow_unpinned):
    """the zig / clang in use against the pin of tools/pclower.py; returns (zig version, clang version)"""
    rc, zv = run(zig("version"))
    rc2, cv = run(zig("cc", "--version"))
    zv = zv.strip()
    cv = cv.strip().splitlines()[0].strip() if cv.strip() else "?"
    cv = re.sub(r"\s*\(.*$", "", cv)
    src = open(os.path.join(sadc, "tools", "pclower.py"), encoding="utf-8").read()
    pz = re.search(r'^PINNED_ZIG = "([^"]*)"', src, re.M)
    pc = re.search(r'^PINNED_CLANG = "([^"]*)"', src, re.M)
    pin = (pz.group(1) if pz else "?", pc.group(1) if pc else "?")
    if rc or rc2 or (zv, cv) != pin:
        msg = "toolchain zig %s / %s is not the one pclower pins (zig %s / %s, sadc tools/pclower.py R10)" % (
            zv, cv, pin[0], pin[1])
        if not allow_unpinned:
            raise SystemExit(msg + ": install that zig (pip install ziglang==%s) or run pclower --revalidate "
                             "--write-pin; --allow-unpinned builds anyway" % pin[0])
        print("WARNING: " + msg)
    return zv, cv


# ---- game code ---------------------------------------------------------------------------------------------------
class Game:
    """The lowered game code to link."""

    def __init__(self, pdir, report, info):
        self.dir = pdir                                    # pclower profile directory (ga64)
        self.report = report                               # pclower report.json (None: use pdir/sa1g_stubs.c)
        self.info = info
        self.lowered = sorted(glob.glob(os.path.join(pdir, "lowered", "*.c")))
        self.support = [os.path.join(pdir, f) for f in ("sa1g_fntab.c", "sa1g_pcdata.c", "sa1g_layout_asserts.c")
                        if os.path.exists(os.path.join(pdir, f))]
        self.stubs = None                                  # generated policy stubs
        self.glue = None                                   # generated runtime glue
        self.stub_summary = None
        self.full = False                                  # every TU of pclower --all (set by main)
        if report is None and os.path.exists(os.path.join(pdir, "sa1g_stubs.c")):
            self.support.append(os.path.join(pdir, "sa1g_stubs.c"))

    def sources(self):
        return self.lowered + self.support + [s for s in (self.stubs,) if s]


def lowering_hash(sadc, extra):
    """sha1 over every input of a full pclower run (names and contents) and the options"""
    h = hashlib.sha1()
    files = [os.path.join(sadc, f) for f in LOWER_INPUT_FILES]
    files += sorted(glob.glob(os.path.join(sadc, "build", "*.symbols.txt")))
    for d in LOWER_INPUT_DIRS:
        for dp, dn, fn in os.walk(os.path.join(sadc, d)):
            dn.sort()
            files += [os.path.join(dp, f) for f in sorted(fn)]
    # src/rt is lowered with the game; rt_intrin.h is not a pclower input (force-included at compile time only)
    files += sorted(f for f in glob.glob(os.path.join(ROOT, "src", "rt", "*")) if not f.endswith("rt_intrin.h"))
    for f in files:
        if os.path.isfile(f):
            h.update(os.path.relpath(f, os.path.dirname(ROOT)).replace("\\", "/").encode() + b"\0")
            sha1_file(f, h)
    h.update(json.dumps(extra, sort_keys=True).encode())
    return h.hexdigest()


def lower_all(args, sadc, zv):
    """the whole game (pclower --all) into build/pclower, cached by lowering_hash"""
    out = os.path.join(ROOT, "build", "pclower")
    opts = ["--all", "--profile", "ga64", "--stub-unresolved"] + sum((["--allow-conflict", c] for c in args.allow_conflict), [])
    opts += (["--allow-unpinned"] if args.allow_unpinned else []) + shlex.split(args.pclower_args or "")
    key = lowering_hash(sadc, {"opts": opts, "zig": zv})
    stamp = os.path.join(out, "inputs.sha1")
    have = (os.path.exists(stamp) and open(stamp).read().strip() == key and os.path.exists(os.path.join(out, "report.json"))
            and glob.glob(os.path.join(out, "ga64", "lowered", "*.c")))
    if have and not args.relower:
        print("== pclower: up to date (%s, inputs %s)" % (rel(out), key[:12]))
    else:
        if os.path.exists(stamp):
            os.remove(stamp)
        cmd = [sys.executable, os.path.join(sadc, "tools", "pclower.py"), "-j", str(max(1, min(args.jobs, 16))),
               "--out", out] + opts
        print("== pclower --all (ga64) -> %s" % rel(out))
        # pclower rewrites every output; files whose text did not change keep their time stamp, so only the
        # TUs that really changed are recompiled
        before = {}
        for f in glob.glob(os.path.join(out, "ga64", "*.c")) + glob.glob(os.path.join(out, "ga64", "lowered", "*.c")):
            before[f] = (hashlib.sha1(open(f, "rb").read()).hexdigest(), os.stat(f))
        rc, text = run(cmd, cwd=sadc)
        kept = 0
        for f, (h, st) in before.items():
            if os.path.exists(f) and hashlib.sha1(open(f, "rb").read()).hexdigest() == h:
                os.utime(f, ns=(st.st_atime_ns, st.st_mtime_ns))
                kept += 1
        print("== pclower: %d of %d previous outputs unchanged" % (kept, len(before)))
        print("\n".join(l for l in text.rstrip().splitlines() if l.strip())[-3000:])
        if rc != 0:
            raise SystemExit("pclower failed (%d): see %s" % (rc, os.path.join(out, "report.json")))
        with open(stamp, "w") as f:
            f.write(key + "\n")
    rep = json.load(open(os.path.join(out, "report.json"), encoding="utf-8"))
    n = len(glob.glob(os.path.join(out, "ga64", "lowered", "*.c")))
    info = "%d TUs lowered by pclower --all (ga64), %d code-map entries, %d stubs, inputs %s" % (
        n, rep.get("code_map_entries", 0), len(rep.get("stubs", [])), key[:12])
    return Game(os.path.join(out, "ga64"), os.path.join(out, "report.json"), info)


def lower_subset(args, sadc):
    """--lower: pclower (profile ga64) on some TUs into build/pclower-subset"""
    tus = []
    for t in args.lower:
        tus += SPIKE_TUS if t == "spike" else [t]
    conflicts = list(args.allow_conflict)
    if "spike" in args.lower:
        conflicts += SPIKE_CONFLICTS
    out = os.path.join(ROOT, "build", "pclower-subset")
    files = [os.path.join(sadc, t) if not os.path.isabs(t) else t for t in tus]
    if args.with_rt:
        files += [p for p in sorted(glob.glob(os.path.join(ROOT, "src", "rt", "*.c")))
                  if os.path.basename(p) != "rt_fsca_q.c"]
    cmd = [sys.executable, os.path.join(sadc, "tools", "pclower.py"), "--profile", "ga64", "--stub-unresolved",
           "-j", str(min(args.jobs, 8)), "--out", out] + sum((["--allow-conflict", c] for c in conflicts), [])
    if args.with_rt:
        cmd += ["--inc", os.path.join(ROOT, "src", "rt")]
    if args.allow_unpinned:
        cmd.append("--allow-unpinned")
    cmd += shlex.split(args.pclower_args or "") + files
    print("== pclower: %d TU(s)%s -> %s" % (len(files), " (+ rt)" if args.with_rt else "", rel(out)))
    rc, text = run(cmd, cwd=sadc)
    print(text.rstrip())
    if rc != 0:
        raise SystemExit("pclower failed (%d)" % rc)
    return Game(os.path.join(out, "ga64"), os.path.join(out, "report.json"),
                "%d TU(s) lowered by build.py%s" % (len(tus), " + rt" if args.with_rt else ", no rt"))


def gen_game_files(game, args):
    """policy stubs + runtime glue + game info (target independent, build/gen)"""
    gdir = os.path.join(ROOT, "build", "gen")
    os.makedirs(gdir, exist_ok=True)
    if game.report:
        tag = hashlib.sha1(os.path.abspath(game.report).encode()).hexdigest()[:8]
        game.stubs = os.path.join(gdir, "sa1g_stubs_policy_%s.c" % tag)
        game.glue = os.path.join(gdir, "sa1g_rtglue_%s.c" % tag)
        summ = os.path.join(gdir, "stubs_summary_%s.json" % tag)
        full = game.full
        s = gen_stubs.generate(game.report, os.path.join(ROOT, "gen", "policy", "stubs.tsv"), game.stubs, summ,
                               strict=args.strict_stubs and full, quiet=not full, glue_out=game.glue,
                               lenient=not full)
        if not full:
            # a subset of the game: most TSV lines are not stubs here, and decompiled functions outside the
            # subset are fatal stubs; only the counts matter
            print("gen_stubs: %d stubs (subset lowering): %s; %d without a TSV line (fatal)" % (
                s["stubs"], ", ".join("%s %d" % kv for kv in sorted(s["policies"].items())), len(s["missing"])))
            for e in s["errors"]:
                print("gen_stubs: error: " + e)
        if s["errors"]:
            raise SystemExit("gen_stubs failed: %d error(s) in gen/policy/stubs.tsv" % len(s["errors"]))
        game.stub_summary = s
    info = os.path.join(gdir, "sa1g_game_info.c")
    text = ("/* Generated by build.py. Do not edit. */\n"
            "int sa1g_game_linked(void);\nconst char *sa1g_game_info(void);\n"
            "int sa1g_game_linked(void)\n{\n    return 1;\n}\n\n"
            "const char *sa1g_game_info(void)\n{\n    return \"%s\";\n}\n" % game.info.replace("\\", "/").replace('"', "'"))
    if not os.path.exists(info) or open(info, encoding="utf-8").read() != text:
        with open(info, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
    game.info_c = info


# ---- incremental compilation -------------------------------------------------------------------------------------
def parse_depfile(path):
    try:
        text = open(path, encoding="utf-8", errors="replace").read()
    except OSError:
        return None
    i = text.find(": ")
    if i < 0:
        i = text.find(":\n")
    if i < 0:
        return None
    body = text[i + 1:].replace("\\\r\n", " ").replace("\\\n", " ")
    deps, cur, j = [], [], 0
    while j < len(body):
        ch = body[j]
        if ch == "\\" and j + 1 < len(body) and body[j + 1] == " ":
            cur.append(" ")
            j += 2
            continue
        if ch in " \t\r\n":
            if cur:
                deps.append("".join(cur))
                cur = []
        else:
            cur.append(ch)
        j += 1
    if cur:
        deps.append("".join(cur))
    return deps


def cmd_key(cmd):
    return hashlib.sha1("\0".join(cmd).encode("utf-8")).hexdigest()


def stale(obj, cmd):
    """True when obj must be rebuilt: no object, another command, or a dependency newer than it"""
    o = os.path.join(ROOT, obj)
    if not os.path.exists(o) or not os.path.exists(o + ".key") or open(o + ".key").read().strip() != cmd_key(cmd):
        return True
    deps = parse_depfile(o + ".d")
    if deps is None:
        return True
    t = os.path.getmtime(o)
    for d in deps:
        p = d if os.path.isabs(d) else os.path.join(ROOT, d)
        if not os.path.exists(p) or os.path.getmtime(p) > t:
            return True
    return False


def compile_all(jobs, n):
    """jobs: (group, obj, cmd); compiles the stale ones in parallel; returns (ok, rebuilt count)"""
    todo = [j for j in jobs if stale(j[1], j[2])]
    ok = True

    def one(j):
        group, obj, cmd = j
        o = os.path.join(ROOT, obj)
        if os.path.exists(o + ".key"):
            os.remove(o + ".key")
        rc, text = run(cmd + ["-MD", "-MF", obj + ".d"])
        if rc == 0:
            with open(o + ".key", "w") as f:
                f.write(cmd_key(cmd) + "\n")
        return rc, text

    with concurrent.futures.ThreadPoolExecutor(max_workers=n) as ex:
        for (group, obj, cmd), (rc, text) in zip(todo, ex.map(one, todo)):
            if text.strip():
                print(text.rstrip())
            if rc != 0:
                print("FAILED:", " ".join(cmd))
                ok = False
    return ok, len(todo)


def newer(out, inputs):
    o = os.path.join(ROOT, out)
    if not os.path.exists(o):
        return True
    t = os.path.getmtime(o)
    return any(os.path.getmtime(os.path.join(ROOT, i)) > t for i in inputs if os.path.exists(os.path.join(ROOT, i)))


def rsp(path, items):
    """a response file (zig cc / zig ar @file): the object lists exceed the Windows command-line limit.
    Returns (@path, changed): a different object list (another game source set) forces the link."""
    text = "".join('"%s"\n' % i.replace("\\", "/") for i in items)
    p = os.path.join(ROOT, path)
    changed = not os.path.exists(p) or open(p, encoding="utf-8").read() != text
    if changed:
        with open(p, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
    return "@" + path, changed


# ---- one target --------------------------------------------------------------------------------------------------
def build_target(key, args, sadc, game):
    triple = TARGETS[key]
    out = os.path.join("build", key + ("-debug" if args.debug else "")).replace("\\", "/")
    windows = key.endswith("windows")
    x86 = key.startswith("x86-")
    inc_sadc = os.path.join(sadc, "include")
    sources = sorted(rel(p) for p in glob.glob(os.path.join(ROOT, "src", "*.c")))
    world = sorted(rel(p) for p in glob.glob(os.path.join(ROOT, "src", "world", "*.c")))
    world += [s for s in RT_ALWAYS if os.path.exists(os.path.join(ROOT, s))]
    if game is not None:
        world = [s for s in world if not s.endswith("sa1_world_nogame.c")]
    opt = ["-O0", "-g", "-fsanitize=undefined", "-fsanitize-trap=undefined"] if args.debug else ["-O2", "-g"]
    cflags = ["-target", triple, "-std=c99", "-Iinclude", "-Isrc",
              "-Wall", "-Wextra", "-Wpedantic", "-Wshadow", "-Wstrict-prototypes", "-Wmissing-prototypes",
              "-Wvla", "-Wcast-qual", "-Wpointer-arith", "-Wundef"] + opt
    wflags = (["-target", triple, "-std=gnu11", "-Iinclude", "-Isrc", "-Isrc/world", "-I" + inc_sadc] + WORLD_WARN
              + WORLD_FP + opt)
    if x86:
        wflags += ["-msse2", "-mfpmath=sse"]
    if args.werror:
        cflags.append("-Werror")
        wflags.append("-Werror")
    if not windows:
        cflags += ["-fPIC", "-fvisibility=hidden"]
        wflags += ["-fPIC", "-fvisibility=hidden"]

    jobs = []
    for variant, define in (("static", "-DSA1_STATIC"), ("shared", "-DSA1_BUILD_SHARED")):
        od = out + "/obj/" + variant
        os.makedirs(os.path.join(ROOT, od), exist_ok=True)
        for s in sources:
            o = od + "/" + os.path.splitext(os.path.basename(s))[0] + ".o"
            jobs.append(("host:" + variant, o, zig("cc", *cflags, define, "-c", s, "-o", o)))
        for s in world:
            o = od + "/world_" + os.path.splitext(os.path.basename(s))[0] + ".o"
            jobs.append(("world:" + variant, o, zig("cc", *wflags, define, "-c", s, "-o", o)))
    if game is not None:
        # one object directory per game source set: switching between the full game and a subset (the spike)
        # does not recompile the other set
        od = out + "/obj/game_" + hashlib.sha1(os.path.normcase(os.path.abspath(game.dir)).encode()).hexdigest()[:8]
        os.makedirs(os.path.join(ROOT, od), exist_ok=True)
        # the certified game flags whatever --debug says (UBSan would change nothing the traces check, and
        # zig's -O0 default would add a sanitizer runtime the world never links)
        base = ["-target", triple] + pc_cflags(sadc, key) + ["-std=gnu11", "-g", "-I" + inc_sadc, "-I" + game.dir,
                                                               "-Isrc/rt", "-Iinclude"] + GAME_WARN
        if not windows:
            base += ["-fPIC", "-fvisibility=hidden"]
        gflags = base + GAME_FORCE + GAME_INTRIN
        for s in game.sources():
            b = os.path.splitext(os.path.basename(s))[0]
            grp = ("rt" if b.startswith("rt_") else "game") if s in game.lowered else {
                "sa1g_fntab": "codemap", "sa1g_pcdata": "pcdata", "sa1g_layout_asserts": "layout"}.get(b, "stubs")
            jobs.append((grp, od + "/" + b + ".o", zig("cc", *gflags, "-c", s, "-o", od + "/" + b + ".o")))
        if game.glue:
            jobs.append(("stubs", od + "/sa1g_rtglue.o", zig("cc", *(base + GAME_FORCE), "-c", game.glue,
                                                             "-o", od + "/sa1g_rtglue.o")))
        if not any(os.path.basename(s) == "sa1g_pcdata.c" for s in game.sources()):
            # lowered code from a pclower without R4 pcdata: no C-defined data to place
            pcd = os.path.join(ROOT, "build", "gen", "sa1g_pcdata_none.c")
            text = ("/* Generated by build.py: the lowered code has no R4 pcdata. */\n#include <stdint.h>\n"
                    "uint32_t sa1g_pcd_size(void);\nvoid sa1g_pcd_init(void);\n"
                    "uint32_t sa1g_pcd_size(void)\n{\n    return 0u;\n}\n\nvoid sa1g_pcd_init(void)\n{\n}\n")
            if not os.path.exists(pcd) or open(pcd).read() != text:
                with open(pcd, "w", encoding="utf-8", newline="\n") as f:
                    f.write(text)
            jobs.append(("pcdata", od + "/sa1g_pcdata_none.o", zig("cc", "-target", triple, "-std=gnu11", "-O2", "-c",
                                                                  pcd, "-o", od + "/sa1g_pcdata_none.o")
                         + ([] if windows else ["-fPIC"])))
        jobs.append(("info", od + "/sa1g_game_info.o", zig("cc", "-target", triple, "-std=gnu11", "-O2", "-c",
                                                          game.info_c, "-o", od + "/sa1g_game_info.o")
                     + ([] if windows else ["-fPIC", "-fvisibility=hidden"])))
    # stale objects of sources that are gone (a TU removed from the game) must not be archived
    keep = set(os.path.normcase(os.path.join(ROOT, o)) for _, o, _ in jobs)
    for d in set(os.path.dirname(o) for _, o, _ in jobs):
        for o in glob.glob(os.path.join(ROOT, d, "*.o")):
            if os.path.normcase(o) not in keep:
                os.remove(o)
    ok, nbuilt = compile_all(jobs, args.jobs)
    if not ok:
        return None
    print("[%s] %d object(s) compiled, %d up to date" % (key, nbuilt, len(jobs) - nbuilt))

    game_objs = [o for g, o, _ in jobs if not g.startswith(("host", "world"))]
    static_objs = [o for g, o, _ in jobs if g.endswith(":static")] + game_objs
    shared_objs = [o for g, o, _ in jobs if g.endswith(":shared")] + game_objs
    sys_libs = [] if windows else ["-lm", "-lpthread"]
    exe = ".exe" if windows else ""
    lib = out + "/libsa1.a"
    shared = out + ("/sa1.dll" if windows else "/libsa1.so")
    implib = out + "/libsa1.dll.a"
    steps = []
    srsp, schanged = rsp(out + "/static.rsp", static_objs)
    if schanged or newer(lib, static_objs):
        if os.path.exists(os.path.join(ROOT, lib)):
            os.remove(os.path.join(ROOT, lib))
        steps.append((lib, zig("ar", "rcs", lib, srsp)))
    drsp, dchanged = rsp(out + "/shared.rsp", shared_objs)
    if dchanged or newer(shared, shared_objs):
        if windows:
            steps.append((shared, zig("cc", "-target", triple, "-shared", "-o", shared, drsp,
                                      "-Wl,--out-implib," + implib)))
        else:
            steps.append((shared, zig("cc", "-target", triple, "-shared", "-o", shared, drsp,
                                      "-Wl,--no-undefined", *sys_libs)))
    tool_flags = [f for f in cflags if f not in ("-Wmissing-prototypes", "-fvisibility=hidden")]
    tflags = [f for f in wflags if f not in ("-Wmissing-prototypes", "-fvisibility=hidden")]
    for tool in TOOLS:
        steps.append((out + "/" + tool + exe, zig("cc", *tool_flags, "-DSA1_STATIC", "tests/%s.c" % tool, lib,
                                                    "-o", out + "/" + tool + exe, *sys_libs)))
    # rip_test once more against the shared library (exercises the exported API)
    if windows:
        steps.append((out + "/rip_test_shared" + exe, zig("cc", *tool_flags, "tests/rip_test.c", implib,
                                                          "-o", out + "/rip_test_shared" + exe)))
    else:
        steps.append((out + "/rip_test_shared", zig("cc", *tool_flags, "tests/rip_test.c", "-L" + out, "-lsa1", "-lm",
                                                     "-Wl,-rpath,$ORIGIN", "-o", out + "/rip_test_shared")))
    # the world tests (static library: internal API)
    steps.append((out + "/spike_test" + exe, zig("cc", *tflags, "-DSA1_STATIC", "tests/spike_test.c", lib,
                                                  "-o", out + "/spike_test" + exe, *sys_libs)))
    # link test: every object of the archive linked (no unresolved symbol anywhere), and the shared library
    if not args.no_link_test:
        lt = "gen/linktest/link_test.c"
        full = ["-DSA1_LINK_TEST_FULL"] if game is not None and game.full else []
        steps.append((out + "/link_test" + exe, zig("cc", *tflags, "-DSA1_STATIC", "-DSA1_LINK_TEST_STATIC", *full, lt,
                                                     "-Wl,--whole-archive", lib, "-Wl,--no-whole-archive",
                                                     "-o", out + "/link_test" + exe, *sys_libs)))
        if windows:
            steps.append((out + "/link_test_shared" + exe, zig("cc", *tool_flags, lt, implib,
                                                                "-o", out + "/link_test_shared" + exe)))
        else:
            steps.append((out + "/link_test_shared", zig("cc", *tool_flags, lt, "-L" + out, "-lsa1",
                                                         "-Wl,-rpath,$ORIGIN", "-o", out + "/link_test_shared")))
    if game is not None and not args.no_link_test:
        # rt_intrin.h's inline intrinsics against the world core's out-of-line ones, bit for bit
        ib = ["-target", triple] + pc_cflags(sadc, key) + ["-std=gnu11", "-I" + inc_sadc, "-Isrc/rt", "-Iinclude"]
        steps.append((out + "/intrin_test" + exe, zig("cc", *ib, *GAME_INTRIN, "gen/linktest/intrin_test.c", lib,
                                                       "-o", out + "/intrin_test" + exe, *sys_libs)))
    failed = []
    for what, cmd in steps:
        rc, text = run(cmd)
        if text.strip():
            print(text.rstrip())
        if rc != 0:
            print("FAILED:", " ".join(cmd))
            failed.append(what)
            if what in (lib, shared):
                return None
    print("[%s] built %s%s" % (key, ", ".join(sorted(f for f in os.listdir(os.path.join(ROOT, out))
                                                     if not f.endswith((".pdb", ".lib", ".rsp", ".txt")) and
                                                     os.path.isfile(os.path.join(ROOT, out, f)))),
                               "" if game is None else " (game: %s)" % game.info))
    return {"key": key, "out": out, "jobs": jobs, "lib": lib, "shared": shared, "windows": windows, "failed": failed}


# ---- link test and size report -----------------------------------------------------------------------------------
GROUP_NAMES = [("game", "lowered game code (src/1st_read, src/overlays/*)"),
               ("rt", "lowered runtime (src/rt)"), ("codemap", "code map (sa1g_fntab.c)"),
               ("stubs", "boundary stubs + runtime glue (gen/policy/stubs.tsv)"), ("pcdata", "R4 pcdata placement"),
               ("layout", "layout asserts"), ("info", "game info"), ("world", "world core (src/world + FSCA table)"),
               ("host", "asset layer + API (src/*.c)")]
COMPILER_RT = re.compile(r"^_*(u?div|u?mod|mul|fix|float|ash|lsh|cmp|ucmp|neg|pop|clz|ctz|bswap|extend|trunc|"
                         r"udivmod|divmod|powi|addsf|subsf|muldf|divdf)\w*$")


def link_report(res, game):
    """exports, unresolved symbols of the game objects, sizes; returns (ok, text)"""
    key, out, jobs = res["key"], res["out"], res["jobs"]
    lines, ok = ["== %s" % key], True
    objs = {}
    for g, o, _ in jobs:
        if g.endswith(":shared"):
            continue
        objs[o] = (g.split(":")[0], binsyms.objfile(os.path.join(ROOT, o)))
    defined = set().union(*(s.defined for _, s in objs.values())) if objs else set()
    # 1. game / runtime objects import only memcpy / memmove / memset (+ compiler helpers) from the C library
    gimp = set()
    for o, (g, s) in objs.items():
        if g not in ("host", "world"):
            gimp |= s.undefined - defined
    bad = sorted(n for n in gimp if n not in CRT_ALLOWED and not COMPILER_RT.match(n))
    lines.append("game/rt imports from outside libsa1: %s" % (" ".join(sorted(gimp)) or "none"))
    if bad:
        ok = False
        lines.append("ERROR: game/rt code imports C library functions (design 8.1 allows memcpy/memmove/memset): "
                     + " ".join(bad))
    himp = set()
    for o, (g, s) in objs.items():
        if g in ("host", "world"):
            himp |= s.undefined - defined
    lines.append("world/asset layer imports: %s" % " ".join(sorted(himp)))
    # 2. the shared library exports only the sa1_* API
    shared = os.path.join(ROOT, res["shared"])
    if os.path.exists(shared):
        ex = binsyms.exports(shared)
        notapi = [n for n in ex if not n.startswith("sa1_")]
        lines.append("%s exports %d symbols%s" % (os.path.basename(shared), len(ex),
                                                    ("; NOT sa1_*: " + " ".join(notapi)) if notapi else ", all sa1_*"))
        if notapi:
            ok = False
    # 3. link test executables
    for f in res["failed"]:
        lines.append("ERROR: link failed: %s" % f)
        ok = False
    # 4. sizes
    sizes = {}
    for o, (g, s) in objs.items():
        d = sizes.setdefault(g, {"objects": 0, "text": 0, "rodata": 0, "data": 0, "bss": 0, "other": 0, "debug": 0})
        d["objects"] += 1
        for name, size, kind in s.sections:
            d[kind] += size
    lines.append("")
    lines.append("%-62s %5s %10s %10s %9s %9s %9s" % ("component (static objects, bytes)", "objs", "text", "rodata",
                                                       "data", "bss", "other"))
    tot = dict.fromkeys(("objects", "text", "rodata", "data", "bss", "other"), 0)
    for g, title in GROUP_NAMES:
        if g in sizes:
            d = sizes[g]
            lines.append("%-62s %5d %10d %10d %9d %9d %9d" % (title, d["objects"], d["text"], d["rodata"], d["data"],
                                                              d["bss"], d["other"]))
            for k in tot:
                tot[k] += d[k]
    lines.append("%-62s %5d %10d %10d %9d %9d %9d" % ("total", tot["objects"], tot["text"], tot["rodata"], tot["data"],
                                                      tot["bss"], tot["other"]))
    lines.append("")
    for f in (res["lib"], res["shared"]) + ((out + "/libsa1.dll.a",) if res["windows"] else ()):
        p = os.path.join(ROOT, f)
        if os.path.exists(p):
            lines.append("%-40s %12d bytes on disk" % (f, os.path.getsize(p)))
    if os.path.exists(shared):
        secs = binsyms.binsections(shared)
        load = sum(s for _, s, k in secs if k != "debug")
        dbg = sum(s for _, s, k in secs if k == "debug")
        lines.append("%s: %d bytes loaded (%s), %d bytes of debug info" % (
            os.path.basename(shared), load, ", ".join("%s %d" % (n, s) for n, s, k in secs if k != "debug"), dbg))
    if game is not None and game.stub_summary:
        s = game.stub_summary
        lines.append("stubs: %d (%s)%s; runtime glue: %s" % (
            s["stubs"], ", ".join("%s %d" % kv for kv in sorted(s["policies"].items())),
            ("; WITHOUT a policy line (fatal): " + " ".join(s["missing"])) if s["missing"] else "",
            " ".join(s.get("runtime_glue", [])) or "none"))
    text = "\n".join(lines) + "\n"
    with open(os.path.join(ROOT, out, "size_report.txt"), "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    return ok, text


def write_cmake_bundle(game, sadc):
    """build/gen/sa1g_game.cmake: the game sources and flags for CMakeLists.txt (-DSA1_GAME_CMAKE=<file>)"""
    def q(p):
        return '"%s"' % os.path.abspath(p).replace("\\", "/")

    def flags(sec):
        out, active = [], True
        for line in open(os.path.join(sadc, "tools", "pc_cflags.txt"), encoding="utf-8"):
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            if line.startswith("["):
                active = sec in line[1:-1].split()
                continue
            if active:
                out += line.split()
        return " ".join(out)
    srcs = [s for s in game.sources()]
    text = ("# Generated by build.py. Do not edit. The lowered game code of libsa1 for CMakeLists.txt\n"
            "# (-DSA1_WORLD=ON -DSA1_GAME_CMAKE=<this file>; clang only: the flag set is certified for zig's clang).\n"
            "set(SA1G_GAME_INFO \"%s\")\n" % game.info.replace('"', "'") +
            "set(SA1G_GAME_SOURCES\n    %s)\n" % "\n    ".join(q(s) for s in srcs) +
            "set(SA1G_GAME_GLUE %s)\n" % (q(game.glue) if game.glue else '""') +
            "set(SA1G_GAME_INFO_C %s)\n" % q(game.info_c) +
            "set(SA1G_GAME_INCLUDES %s %s %s %s)\n" % (q(os.path.join(sadc, "include")), q(game.dir),
                                                      q(os.path.join(ROOT, "src", "rt")), q(os.path.join(ROOT, "include"))) +
            "set(SA1G_GAME_FLAGS_ALL \"%s\")\n" % flags("all") +
            "set(SA1G_GAME_FLAGS_X86 \"%s\")\n" % flags("x86") +
            "set(SA1G_GAME_FLAGS_X86_64 \"%s\")\n" % flags("x86_64") +
            "set(SA1G_GAME_WARN \"%s\")\n" % " ".join(GAME_WARN) +
            "set(SA1G_TEXT_H %s)\nset(SA1G_RT_INTRIN_H %s)\n" % (q(os.path.join(ROOT, "src", "world", "sa1g_text.h")),
                                                                  q(os.path.join(ROOT, "src", "rt", "rt_intrin.h"))))
    p = os.path.join(ROOT, "build", "gen", "sa1g_game.cmake")
    gen_stubs.write_if_changed(p, text)
    return p


# ---- world and replay tests (--test) ----------------------------------------------------------------------------
M1_TRACES = ["ec1_stand", "ec1_run_jump"]                     # M0, M1a, M1b and M1 (tests/build_replay.py M1)
M11_TRACES = ["ec1_turns", "ec1_jump_hold", "ec1_spindash"]   # M1.1


def run_test(cmd):
    """a test program or test script with its output in order after build.py's (stdout may be a pipe)"""
    sys.stdout.flush()
    return subprocess.run(cmd, cwd=ROOT)


def world_tests(args, sadc, results, game):
    """--test: world_boot_test + transplant_test (built by src/world/build_world_tests.py) and the M1 replay test
    (tests/build_replay.py), on the runnable targets just built. They need the whole lowered game in
    build/<target>/libsa1.a, the user's disc (SA1_TEST_GDI; never copied) and sadc's local reference data; whatever
    is missing is skipped with a message. Returns 1 when a test that ran failed."""
    keys = [r["key"] for r in results if runnable(r["key"])]
    if not keys:
        return 0
    gdi = os.environ.get("SA1_TEST_GDI", "")
    why = None
    if args.debug:
        why = "--debug builds build/<target>-debug, the world test scripts link build/<target>/libsa1.a"
    elif game is None or not game.full:
        why = "they need the whole lowered game (this build: %s)" % ("--no-game" if game is None else game.info)
    elif not gdi:
        why = "SA1_TEST_GDI is not set (the path of your own .gdi; it is read in place, never copied)"
    elif not os.path.isfile(gdi):
        why = "SA1_TEST_GDI=%s is not a file" % gdi
    if why:
        print("== world_boot_test, transplant_test, replay test: skipped: " + why)
        return 0
    traces = os.path.join(sadc, "build", "traces")

    def ref(name):
        return os.path.join(traces, "ref", name + ".bin")

    summary, rc = [], 0

    def note(k, what, code):
        summary.append("[%s] %s %s" % (k, what, "ok" if code == 0 else "FAILED (%d)" % code))
        return 1 if code else 0

    # world_boot_test (M0, M1b, live mode, character API) and transplant_test (M1a)
    miss_ref = [ref(n) for n in M1_TRACES if not os.path.isfile(ref(n))]
    if miss_ref:
        print("== world_boot_test, transplant_test: skipped: reference trace(s) missing: %s (sadc docs/"
              "harness_simulator.md)" % ", ".join(miss_ref))
        summary.append("world_boot_test, transplant_test skipped (references missing)")
    else:
        for k in keys:
            ext = ".exe" if k.endswith("windows") else ""
            print("== [%s] src/world/build_world_tests.py: world_boot_test, transplant_test" % k)
            x = run_test([sys.executable, os.path.join("src", "world", "build_world_tests.py"), "--target", k,
                          "--sadc", sadc])
            if x.returncode != 0:
                rc |= note(k, "world test build", x.returncode)
                continue
            exe = os.path.join(ROOT, "build", k, "world_boot_test" + ext)
            x = run_test([exe, "--stage-tolerant", sadc, gdi])
            print("[%s] world_boot_test exit %d" % (k, x.returncode))
            rc |= note(k, "world_boot_test (M0, M1b, live, character API)", x.returncode)
            for n in (0, 1):
                rams = [os.path.join(traces, "m1_%s.step%d.ram" % (t, n)) for t in M1_TRACES]
                miss = [r for r in rams if not os.path.isfile(r)]
                if miss:
                    print("[%s] transplant_test --from %d: skipped: RAM image(s) missing: %s (generated locally with "
                          "a private pure harness build and --dump-ram-at 0,1, see src/world/build_world_tests.py)"
                          % (k, n, ", ".join(miss)))
                    summary.append("[%s] transplant_test --from %d skipped (RAM images missing)" % (k, n))
                    continue
                x = run_test([os.path.join(ROOT, "build", k, "transplant_test" + ext), "--stage-tolerant",
                              "--from", str(n), "--sadc", sadc])
                print("[%s] transplant_test --from %d exit %d" % (k, n, x.returncode))
                rc |= note(k, "transplant_test --from %d (M1a)" % n, x.returncode)

    # the replay through the public API: tests/build_replay.py builds replay_test, runs it (strict: every record of
    # V6, V5 and DISC bit-exact, the worlds with the stage's own tasks) and diffs every output against its reference
    # with sa1trace.py; with all three targets built it also checks V7. Every reference trace the script knows whose
    # reference and landtable dump (build/traces/land/L<level>A<act>, named by the trace header) exist is the gate.
    land = os.path.join(traces, "land", "L01A0")
    miss_land = [land + e for e in (".tri", ".col") if not os.path.isfile(land + e)]
    if miss_ref or miss_land:
        print("== replay test: skipped: missing %s" % ", ".join(miss_ref + miss_land))
        summary.append("replay test skipped (inputs missing)")
    else:
        have, lack = [], []
        for n in replay_script_traces():
            if not os.path.isfile(ref(n)):
                lack.append("%s (no reference)" % n)
                continue
            lp = trace_land(ref(n), traces)
            if lp is None or not all(os.path.isfile(lp + e) for e in (".tri", ".col")):
                lack.append("%s (no landtable dump %s)" % (n, lp))
                continue
            have.append(n)
        if lack:
            print("== replay test: not run for %s" % ", ".join(lack))
        built = set(r["key"] for r in results)
        runs = ["all"] if built == set(TARGETS) else keys
        sel = sum((["--trace", n] for n in have), [])
        for k in runs:
            print("== tests/build_replay.py --target %s --run %s" % (k, " ".join(sel)))
            x = run_test([sys.executable, os.path.join("tests", "build_replay.py"), "--target", k, "--run",
                          "--sadc", sadc, "--gdi", gdi] + sel)
            rc |= note(k, "replay test, strict (%d traces%s)" % (len(have), ", V7" if k == "all" else ""),
                       x.returncode)
    print("== --test: " + "; ".join(summary))
    return rc


def trace_land(ref_path, traces):
    """the landtable dump prefix of a reference trace (L<level>A<act> of its header), or None"""
    try:
        with open(ref_path, "rb") as f:
            h = f.read(64)
    except OSError:
        return None
    if len(h) < 28 or h[:8] != b"SA1TRC01":
        return None
    level, act = struct.unpack_from("<hh", h, 24)     # sa1_trace_header: magic, version, sizes, count, level, act
    return os.path.join(traces, "land", "L%02dA%d" % (level, act))


def replay_script_traces():
    """the reference traces tests/build_replay.py knows (its list constants such as M1 = [...], P7 = [...]); read
    from its text because the script runs on import"""
    try:
        text = open(os.path.join(ROOT, "tests", "build_replay.py"), encoding="utf-8").read()
    except OSError:
        return []
    names = []
    for m in re.finditer(r"^[A-Z][A-Z0-9_]* = \[([^\]]*)\]", text, re.M):
        names += [n for n in re.findall(r'"([A-Za-z0-9_]+)"', m.group(1)) if n not in names]
    return names


# ---- main --------------------------------------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--target", default="all", choices=sorted(TARGETS) + ["all"])
    ap.add_argument("--debug", action="store_true", help="-O0 + UBSan traps, output in build/<target>-debug")
    ap.add_argument("--werror", action="store_true")
    ap.add_argument("--clean", action="store_true", help="remove the target directories first (not the lowering)")
    ap.add_argument("-j", "--jobs", type=int, default=os.cpu_count() or 4)
    ap.add_argument("--game", default=os.path.join(os.path.dirname(ROOT), "sadc"), help="sadc root")
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--no-game", action="store_true", help="world core without game code")
    g.add_argument("--lowered", help="pre-lowered game code (a pclower profile directory)")
    g.add_argument("--lower", nargs="+", metavar="TU", help="lower only these sadc TUs ('spike' = phase 1 TUs)")
    ap.add_argument("--with-rt", action="store_true", help="with --lower: lower src/rt too")
    ap.add_argument("--relower", action="store_true", help="run pclower --all even when its inputs did not change")
    ap.add_argument("--strict-stubs", action="store_true", help="a pclower stub without a line in stubs.tsv is an error")
    ap.add_argument("--allow-unpinned", action="store_true", help="accept a zig other than pclower's pin (R10)")
    ap.add_argument("--allow-conflict", action="append", default=[], help="pclower --allow-conflict")
    ap.add_argument("--pclower-args", default="", help="more pclower options")
    ap.add_argument("--no-link-test", action="store_true", help="skip link_test / link_test_shared and the report")
    ap.add_argument("--test", action="store_true", help="run unit_test and spike_test on runnable targets, and with "
                    "SA1_TEST_GDI and sadc's reference data also world_boot_test, transplant_test and the M1 replay test")
    ap.add_argument("--replay-gate-all", action="store_true", help="with --test: kept for old command lines (every "
                    "replay trace of tests/build_replay.py is a strict gate now)")
    ap.add_argument("--rip", nargs=2, metavar=("GDI", "OUTDIR"), help="run rip_test on runnable targets")
    args = ap.parse_args()

    sadc = os.path.abspath(args.game)
    if not os.path.exists(os.path.join(sadc, "include", "sa1", "guest.h")):
        print("sadc not found at %s (--game): sa1/guest.h is needed for the world core" % sadc)
        return 1
    zv, cv = toolchain(sadc, args.allow_unpinned)
    print("== zig %s, %s (pinned by sadc tools/pclower.py)" % (zv, cv))
    game = None
    if args.lowered:
        d = os.path.abspath(args.lowered)
        if not glob.glob(os.path.join(d, "lowered", "*.c")) or not os.path.exists(os.path.join(d, "sa1g_fntab.c")):
            print("--lowered %s: no lowered/*.c or sa1g_fntab.c" % d)
            return 1
        rep = os.path.join(os.path.dirname(d), "report.json")
        n = len(glob.glob(os.path.join(d, "lowered", "*.c")))
        game = Game(d, rep if os.path.exists(rep) else None, "%d lowered TU(s) from %s" % (n, d))
    elif args.lower:
        game = lower_subset(args, sadc)
    elif not args.no_game:
        game = lower_all(args, sadc, zv)
    if game is not None:
        # the whole game (pclower --all, here or e.g. in sadc/build/pclower/ga64): stricter checks apply
        want = set(os.path.basename(f) for f in glob.glob(os.path.join(sadc, "src", "1st_read", "*.c")))
        for d in glob.glob(os.path.join(sadc, "src", "overlays", "*")):
            # pclower names an overlay TU's lowered file <dir>__<file>.c (images, sadc docs 2.5)
            want |= set("%s__%s" % (os.path.basename(d), os.path.basename(f)) for f in glob.glob(os.path.join(d, "*.c")))
        want |= set(os.path.basename(f) for f in glob.glob(os.path.join(ROOT, "src", "rt", "*.c"))) - {"rt_fsca_q.c"}
        game.full = want <= set(os.path.basename(f) for f in game.lowered)
        gen_game_files(game, args)
        if game.full:
            write_cmake_bundle(game, sadc)      # the CMake bundle always describes the whole game

    keys = sorted(TARGETS) if args.target == "all" else [args.target]
    if args.clean:
        for k in keys:
            shutil.rmtree(os.path.join(ROOT, "build", k + ("-debug" if args.debug else "")), ignore_errors=True)
    results, failed = [], []
    for k in keys:
        r = build_target(k, args, sadc, game)
        if r is None:
            failed.append(k)
        else:
            results.append(r)
    report_ok = True
    if not args.no_link_test and results:
        texts = []
        for r in results:
            ok, text = link_report(r, game)
            report_ok &= ok
            texts.append(text)
        with open(os.path.join(ROOT, "build", "link_report%s.txt" % ("-debug" if args.debug else "")), "w",
                  encoding="utf-8", newline="\n") as f:
            f.write("\n".join(texts))
        print("".join(texts).rstrip())
    if failed:
        print("build failed for:", ", ".join(failed))
        return 1
    rc = 0 if report_ok and not any(r["failed"] for r in results) else 1
    if rc:
        print("link test FAILED (build/link_report.txt)")
    rom = os.path.join(sadc, "baserom", "1ST_READ.BIN")
    ref = os.path.join(ROOT, "build", "spike", "ref")
    for r in results:
        k = r["key"]
        if not runnable(k):
            continue
        ext = ".exe" if k.endswith("windows") else ""
        k = k + ("-debug" if args.debug else "")
        if not args.no_link_test:
            for t in ("link_test", "link_test_shared", "intrin_test"):
                p = os.path.join(ROOT, "build", k, t + ext)
                if os.path.exists(p):
                    x = subprocess.run([p], cwd=os.path.dirname(p), stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                       text=True, errors="replace")
                    print("[%s] %s exit %d: %s" % (k, t, x.returncode, " | ".join(x.stdout.strip().splitlines()[-4:])))
                    rc |= x.returncode
        if args.test:
            x = run_test([os.path.join(ROOT, "build", k, "unit_test" + ext)])
            print("[%s] unit_test exit %d" % (k, x.returncode))
            rc |= x.returncode
            x = run_test([os.path.join(ROOT, "build", k, "spike_test" + ext), rom, ref])
            print("[%s] spike_test exit %d" % (k, x.returncode))
            rc |= x.returncode
        if args.rip:
            x = run_test([os.path.join(ROOT, "build", k, "rip_test" + ext), args.rip[0], os.path.join(args.rip[1], k)])
            print("[%s] rip_test exit %d" % (k, x.returncode))
            rc |= x.returncode
    if args.test:
        rc |= world_tests(args, sadc, results, game)
    return 1 if rc else 0


def _locked(fn):
    """Several builds (e.g. concurrent agents or test scripts) share build/: serialise them with an exclusive lock
    on build/.build.lock; a second build waits until the first one is done."""
    os.makedirs(os.path.join(ROOT, "build"), exist_ok=True)
    lockf = open(os.path.join(ROOT, "build", ".build.lock"), "a+")
    if os.name == "nt":
        import msvcrt
        import time
        while True:
            try:
                lockf.seek(0)
                msvcrt.locking(lockf.fileno(), msvcrt.LK_NBLCK, 1)
                break
            except OSError:
                time.sleep(1.0)
    else:
        import fcntl
        fcntl.flock(lockf.fileno(), fcntl.LOCK_EX)
    try:
        return fn()
    finally:
        try:
            if os.name == "nt":
                import msvcrt
                lockf.seek(0)
                msvcrt.locking(lockf.fileno(), msvcrt.LK_UNLCK, 1)
        finally:
            lockf.close()


if __name__ == "__main__":
    sys.exit(_locked(main))
