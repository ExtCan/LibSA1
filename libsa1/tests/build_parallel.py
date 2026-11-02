#!/usr/bin/env python3
"""Build and run tests/parallel_test.c (round 0.9.8, 098-lib-threads: the parallel-tick contract SA1_API_PARALLEL and the code map
race, sadc docs/research/gsa1_098_plan.md 6.4, gsa1_096_plan.md 6.3) against a SNAPSHOT of build/<target>/libsa1.a and its headers.

    py -3.12 tests/build_parallel.py [--target x86_64-windows|x86-windows|x86_64-linux|windows|all] [--run] [--race] [--replay]
                                     [--gdi GDI] [--sadc DIR] [--debug] [--no-snapshot] [--base DIR] [--lib-root DIR] [--out DIR]
                                     [--runs N] [--timeout S] [-- ARGS]

--run           parallel_test with GDI (default $SA1_TEST_GDI) and ARGS; when more than one target ran, their DETERMINISM lines must
                be equal (x86 = x64).
--race          the race, one process per run: --runs x 2 contexts and --runs x 8 contexts with N worlds in all (99 on x86_64-windows,
                32 on x86-windows: review R.10), the tight loop 5 x 10 s and --runs x 2 s; a process that dies is a crash: its exit code
                is reported. --runs defaults to 50.
--replay        parallel_replay (parallel_test.c with -DSA1PT_REPLAY): the strict replays of tests/build_replay.py's lists in chunks while
                20 other worlds tick on other threads; every output diffed against its reference with sa1trace.py --ulps 0.
--base DIR      use DIR/<target>/libsa1.a with DIR/include, DIR/src instead of a snapshot (r098/base/lib030: the round-start archive; only
                --race works there).
--lib-root DIR  the LIB tree to snapshot (a scratch variant); --out DIR: where the programs go (default build/parallel_test/<target>).
"""
import argparse
import concurrent.futures
import glob
import os
import platform
import re
import shutil
import struct
import subprocess
import sys
import time

LIB = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TRIPLES = {"x86_64-windows": "x86_64-windows-gnu", "x86-windows": "x86-windows-gnu", "x86_64-linux": "x86_64-linux-gnu"}


class LibLock:
    """build.py serialises its builds with an exclusive lock on byte 0 of build/.build.lock."""

    def __init__(self, root, timeout=3600.0):
        self.root = root
        self.timeout = timeout
        self.f = None

    def __enter__(self):
        path = os.path.join(self.root, "build", ".build.lock")
        os.makedirs(os.path.dirname(path), exist_ok=True)
        self.f = open(path, "a+")
        t0 = time.time()
        said = False
        while True:
            try:
                if os.name == "nt":
                    import msvcrt
                    self.f.seek(0)
                    msvcrt.locking(self.f.fileno(), msvcrt.LK_NBLCK, 1)
                else:
                    import fcntl
                    fcntl.flock(self.f.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
                return self
            except OSError:
                if time.time() - t0 > self.timeout:
                    raise TimeoutError("libsa1's build lock is held")
                if not said:
                    print("waiting for libsa1's build lock (a libsa1 build is running) ...", flush=True)
                    said = True
                time.sleep(0.25)

    def __exit__(self, *exc):
        if self.f:
            try:
                if os.name == "nt":
                    import msvcrt
                    self.f.seek(0)
                    msvcrt.locking(self.f.fileno(), msvcrt.LK_UNLCK, 1)
                else:
                    import fcntl
                    fcntl.flock(self.f.fileno(), fcntl.LOCK_UN)
            finally:
                self.f.close()


def runnable(key):
    if key.endswith("windows"):
        return platform.system() == "Windows"
    return platform.system() == "Linux" and platform.machine().lower() in ("x86_64", "amd64")


def snapshot(target, snap, debug, lib_root):
    if os.path.isdir(snap):
        shutil.rmtree(snap)
    for d in ("include", "src", os.path.join("src", "world"), os.path.join("src", "rt")):
        os.makedirs(os.path.join(snap, d), exist_ok=True)
    with LibLock(lib_root):
        lib = os.path.join(lib_root, "build", target + ("-debug" if debug else ""), "libsa1.a")
        if not os.path.exists(lib):
            raise SystemExit("no %s: run build.py --target %s%s first" % (lib, target, " --debug" if debug else ""))
        shutil.copy2(lib, os.path.join(snap, "libsa1.a"))
        for pat, d in (("include/*.h", "include"), ("src/*.h", "src"), ("src/world/*.h", os.path.join("src", "world")),
                       ("src/rt/*.h", os.path.join("src", "rt"))):
            for h in glob.glob(os.path.join(lib_root, pat)):
                shutil.copy2(h, os.path.join(snap, d, os.path.basename(h)))
    a = os.path.join(snap, "libsa1.a")
    print("[%s] snapshot: %s libsa1.a %d bytes (LIB build of %s)" % (
        target, "debug" if debug else "optimised", os.path.getsize(a),
        time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(os.path.getmtime(a)))), flush=True)


def build(name, target, out, inc_root, lib, sadc, defines=(), sadc_inc=None):
    src = os.path.join(os.path.dirname(os.path.abspath(__file__)), "parallel_test.c")
    flags = ["-target", TRIPLES[target], "-std=gnu11",
             "-I" + os.path.join(inc_root, "include"), "-I" + os.path.join(inc_root, "src"),
             "-I" + os.path.join(inc_root, "src", "world"), "-I" + (sadc_inc or os.path.join(sadc, "include")),
             "-I" + os.path.join(sadc, "harness", "src"), "-I" + os.path.join(LIB, "tests"), "-I" + os.path.dirname(src),
             "-Wall", "-Wextra", "-Wno-unused-function", "-Wno-unused-parameter",
             "-ffp-contract=off", "-frounding-math", "-fno-builtin", "-O2", "-g", "-DSA1_STATIC"] + list(defines)
    if target.startswith("x86-"):
        flags += ["-msse2", "-mfpmath=sse"]
    if "linux" in target:
        flags += ["-fPIC"]
    exe = os.path.join(out, name + (".exe" if "windows" in target else ""))
    cmd = [sys.executable, "-m", "ziglang", "cc", *flags, src, lib, "-o", exe]
    if "linux" in target:
        cmd += ["-lm", "-lpthread"]
    r = subprocess.run(cmd, cwd=LIB, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
    if r.stdout.strip():
        print(r.stdout.rstrip())
    if r.returncode != 0:
        raise SystemExit("FAILED: " + " ".join(cmd))
    print("[%s] built %s" % (target, exe), flush=True)
    return exe


def run_one(k, exe, args, timeout):
    print("==== [%s] %s %s" % (k, os.path.basename(exe), " ".join(args[:-1])), flush=True)
    t0 = time.time()
    try:
        r = subprocess.run([exe] + args, cwd=LIB, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace",
                           timeout=timeout)
        out, rc = r.stdout, r.returncode
    except subprocess.TimeoutExpired as e:
        out = (e.stdout or b"").decode("utf-8", "replace") if isinstance(e.stdout, bytes) else (e.stdout or "")
        out += "\nTIME LIMIT: ran longer than %d s\n" % timeout
        rc = 124
    sys.stdout.write(out)
    print("==== [%s] exit %d (0x%08X) (%.0f s)" % (k, rc, rc & 0xFFFFFFFF, time.time() - t0), flush=True)
    return rc, out


def race(k, exe, gdi, runs, timeout):
    """one process per run; a crash is the process's exit code. Returns the number of bad cases."""
    n = 99 if k == "x86_64-windows" else 32
    bad = 0

    def one(args):
        r = subprocess.run([exe] + args + [gdi], cwd=LIB, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace",
                           timeout=timeout)
        return r.returncode & 0xFFFFFFFF, r.stdout

    for contexts in (2, 8):
        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
            futs = [pool.submit(one, ["--only", "race", "--contexts", str(contexts), "--worlds", str(n)]) for _ in range(runs)]
            res = [f.result() for f in futs]
        crashes = sum(1 for c, _ in res if c != 0)
        hashes = set(m.group(1) for _, o in res for m in re.finditer(r"hash=([0-9a-f]+)", o))
        print("[%s] RACE %d contexts, %d worlds: %d crash(es) in %d runs; exit codes %s; hashes %s" % (
            k, contexts, n, crashes, runs, sorted(set("0x%08X" % c for c, _ in res)), sorted(hashes)), flush=True)
        bad += crashes != 0 or len(hashes) > 1
    res = []
    for i in range(5):
        c, o = one(["--only", "tight", "--seconds", "10"])
        res.append(c)
        line = [l for l in o.splitlines() if l.startswith("TIGHT")]
        print("[%s] TIGHT run %d: exit 0x%08X %s" % (k, i, c, line[0] if line else "(died before its line)"), flush=True)
    print("[%s] TIGHT: %d fault(s) in 5 runs of 10 s" % (k, sum(1 for c in res if c)), flush=True)
    bad += any(res)
    res = []
    for i in range(runs):
        c, o = one(["--only", "tight", "--seconds", "2"])
        res.append(c)
    print("[%s] PREPARE: sa1_character_prepare on a worker while a world ticks: %d fault(s) in %d runs of 2 s" % (
        k, sum(1 for c in res if c), runs), flush=True)
    bad += any(res)
    return bad


def replay(k, exe, sadc, gdi, out_dir, jobs):
    """the strict replays with 20 worlds ticking beside them: every output against its reference"""
    text = open(os.path.join(LIB, "tests", "build_replay.py"), encoding="utf-8").read()
    names = []
    for m in re.finditer(r"^[A-Z][A-Z0-9_]* = \[([^\]]*)\]", text, re.M):
        names += [n for n in re.findall(r'"([A-Za-z0-9_]+)"', m.group(1)) if n not in names]
    # FOLLOW and FOLLOWC do not go to replay_test (tests/follower_replay.c runs them): tests/build_replay.py's rule
    follow = re.search(r'^FOLLOW = \[([^\]]*)\]', text, re.M)
    follow = set(re.findall(r'"([A-Za-z0-9_]+)"', follow.group(1))) if follow else set()
    names = [n for n in names if n not in follow and os.path.isfile(os.path.join(sadc, "build", "traces", "ref", n + ".bin"))]
    od = os.path.join(out_dir, k)
    if os.path.isdir(od):
        shutil.rmtree(od)
    os.makedirs(od)
    # tests/build_replay.py's chunks: at most 16 traces (MAX_JOBS), at most 4 character kinds per stage (its live-mode part needs the first
    # traces' Sonic, so another cut of the list fails there whatever the threads do)
    chunks, cur, kinds = [], [], {}
    for t in names:
        with open(os.path.join(sadc, "build", "traces", "ref", t + ".bin"), "rb") as f:
            h = f.read(32)
        sk = None
        if len(h) >= 32 and h[:8] == b"SA1TRC01":
            level, act, chr_ = struct.unpack_from("<hhb", h, 24)
            sk = ((level, act), chr_)
        full = len(cur) >= 16
        if sk is not None and sk[1] not in kinds.get(sk[0], ()) and len(kinds.get(sk[0], ())) >= 4:
            full = True
        if cur and full:
            chunks.append(cur)
            cur, kinds = [], {}
        cur.append(t)
        if sk is not None:
            kinds.setdefault(sk[0], set()).add(sk[1])
    if cur:
        chunks.append(cur)
    env = dict(os.environ, SA1_TEST_GDI=gdi)
    rc = 0
    bad_chunks = []

    def one(chunk):
        cmd = [exe, "--out", od] + sum((["--trace", t] for t in chunk), []) + [sadc, gdi]
        r = subprocess.run(cmd, cwd=LIB, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace", env=env)
        return r.returncode, r.stdout

    tool = os.path.join(sadc, "harness", "tools", "sa1trace.py")
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, jobs)) as pool:
        for chunk, (code, out) in zip(chunks, pool.map(one, chunks)):
            load = [l for l in out.splitlines() if l.startswith("LOAD")]
            print("[%s] parallel_replay exit %d (%s); %s" % (k, code, " ".join(chunk), load[0] if load else "no LOAD line"), flush=True)
            if code != 0:
                rc = 1
                bad_chunks.append(chunk)
                sys.stdout.write("\n".join(out.splitlines()[-30:]) + "\n")
        diffs = []
        for name in names:
            ref = os.path.join(sadc, "build", "traces", "ref", name + ".bin")
            for suffix, what in (("", "V6"), (".reseed", "V5"), (".disc", "DISC V6")):
                mine = os.path.join(od, name + suffix + ".bin")
                if os.path.exists(mine):
                    diffs.append((name, what, pool.submit(subprocess.run, [sys.executable, tool, "diff", ref, mine, "--ulps", "0"],
                                                          cwd=sadc, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                                                          errors="replace")))
        ndiff = 0
        for name, what, fu in diffs:
            d = fu.result()
            if d.returncode != 0:
                ndiff += 1
                rc = 1
                print("[%s] DIFFERENT %s %s: %s" % (k, name, what, d.stdout[-400:]))
    print("[%s] REPLAY under load: %d traces, %d outputs, %d different, %d failed chunk(s)" % (k, len(names), len(diffs), ndiff, len(bad_chunks)),
          flush=True)
    return rc


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--target", default="windows", choices=sorted(TRIPLES) + ["windows", "all"])
    ap.add_argument("--sadc", default=os.path.join(os.path.dirname(LIB), "sadc"))
    ap.add_argument("--gdi", default=os.environ.get("SA1_TEST_GDI", ""))
    ap.add_argument("--run", action="store_true")
    ap.add_argument("--race", action="store_true")
    ap.add_argument("--replay", action="store_true")
    ap.add_argument("--debug", action="store_true", help="snapshot build/<target>-debug (build.py --debug)")
    ap.add_argument("--no-snapshot", action="store_true")
    ap.add_argument("--base", help="a directory with <target>/libsa1.a, include, src instead of a snapshot")
    ap.add_argument("--lib-root", default=LIB, help="the LIB tree to snapshot (a scratch variant)")
    ap.add_argument("--out", help="the output directory (default build/parallel_test/<target>)")
    ap.add_argument("--sadc-inc", help="the include directory with sa1/guest.h, when it is not <sadc>/include (a scratch variant)")
    ap.add_argument("--runs", type=int, default=50)
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--timeout", type=int, default=1800)
    argv = sys.argv[1:]
    rest = argv[argv.index("--") + 1:] if "--" in argv else []
    a = ap.parse_args(argv[:argv.index("--")] if "--" in argv else argv)
    if a.target == "all":
        keys = sorted(TRIPLES)
    elif a.target == "windows":
        keys = ["x86_64-windows", "x86-windows"]
    else:
        keys = [a.target]
    rc = 0
    det = {}
    for k in keys:
        out = os.path.join(a.out, k) if a.out else os.path.join(a.lib_root, "build", "parallel_test", k)
        os.makedirs(out, exist_ok=True)
        if a.base:
            inc_root, lib = a.base, os.path.join(a.base, k, "libsa1.a")
        else:
            inc_root = os.path.join(out, "snap")
            lib = os.path.join(inc_root, "libsa1.a")
            if not a.no_snapshot:
                snapshot(k, inc_root, a.debug, a.lib_root)
        exe = build("parallel_test", k, out, inc_root, lib, a.sadc, (), a.sadc_inc)
        if not runnable(k):
            continue
        if (a.run or a.race or a.replay) and not a.gdi:
            raise SystemExit("--run, --race and --replay need --gdi or SA1_TEST_GDI")
        if a.run:
            code, text = run_one(k, exe, rest + [a.gdi], a.timeout)
            rc |= code != 0
            for line in text.splitlines():
                if line.startswith("DETERMINISM"):
                    det.setdefault(k, []).append(line.strip())
        if a.race:
            rc |= race(k, exe, a.gdi, a.runs, a.timeout) != 0
        if a.replay:
            ex2 = build("parallel_replay", k, out, inc_root, lib, a.sadc, ["-DSA1PT_REPLAY"], a.sadc_inc)
            rc |= replay(k, ex2, a.sadc, a.gdi, os.path.join(out, "replay"), a.jobs) != 0
    if len(det) > 1:
        vals = [tuple(v) for v in det.values()]
        same = len(set(vals)) == 1
        print("determinism: %s (%s)" % ("identical" if same else "DIFFERENT",
                                         "; ".join("%s: %s" % (k, " ".join(v)) for k, v in sorted(det.items()))))
        rc |= not same
    return 1 if rc else 0


if __name__ == "__main__":
    sys.exit(main())
