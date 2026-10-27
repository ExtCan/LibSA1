#!/usr/bin/env python3
"""Build and run tests/emote_test.c (round 0.9.4a, 094a-lib-emote: emotes through the cutscenes' event action queue,
include/libsa1.h "emotes" SA1_API_EMOTE; sadc docs/research/faces_wheel_plan.md 4.5 / 5.3 / 6.3 with its Review) against
a SNAPSHOT of build/<target>/libsa1.a and its headers (as tests/build_super.py).

    py -3.12 tests/build_emote.py [--target x86_64-windows|x86-windows|x86_64-linux|windows|all] [--run] [--gdi GDI]
                                  [--sadc DIR] [--tsv DIR] [--no-snapshot] [--no-guard] [--timeout S] [-- ARGS]

First tools/gen_emote_names.py --check (src/sa1_emote_names.c equals the research TSVs). The archive and the headers it was
built with (include/, src/*.h, src/world/*.h) are copied under libsa1's build lock (build/.build.lock) into
build/emote_test/<target>/snap/. E13 (an emote with a face sequence) is compiled in (-DEMOTE_TEST_FACES=1) only when the
snapshot's archive DEFINES sa1_character_face_sequence (its armap; review R.14: SA1_API_FACE is in the header before the
faces lane lands), else emote_test reports "E13 not run: faces not landed". --run runs emote_test with GDI (default
$SA1_TEST_GDI) and ARGS under a time limit (--timeout, default 1800 s: a hang - 8C04BF00 spinning on jvmode != 1, review
R.8 - fails the run), then E14 (memcheck) and E21 (hold_memcheck, round 0.9.4b's SA1_EMOTE_HOLD) again with
SA1_GUARD_ALLOC=1 (every libsa1 heap block against a guard page; --no-guard skips it). When more than one target ran, their "DETERMINISM" lines must be equal (x86 = x64).
The default target "windows" is x86_64-windows and x86-windows.
"""
import argparse
import glob
import os
import platform
import shutil
import struct
import subprocess
import sys
import time

LIB = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TRIPLES = {"x86_64-windows": "x86_64-windows-gnu", "x86-windows": "x86-windows-gnu", "x86_64-linux": "x86_64-linux-gnu"}
FACE_FN = b"sa1_character_face_sequence"


class LibLock:
    """build.py serialises its builds with an exclusive lock on byte 0 of build/.build.lock."""

    def __init__(self, timeout=3600.0):
        self.timeout = timeout
        self.f = None

    def __enter__(self):
        path = os.path.join(LIB, "build", ".build.lock")
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
                time.sleep(2.0)

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


def archive_defines(path, name):
    """1 when the archive's symbol table (the GNU / COFF armap "/" or "/SYM64/") lists `name` (a defined symbol)"""
    with open(path, "rb") as f:
        data = f.read()
    if not data.startswith(b"!<arch>\n"):
        return 0
    pos = 8
    while pos + 60 <= len(data):
        hdr = data[pos:pos + 60]
        ident = hdr[:16].rstrip()
        size = int(hdr[48:58].strip() or b"0")
        body = data[pos + 60:pos + 60 + size]
        if ident in (b"/", b"/SYM64/"):
            w = 8 if ident == b"/SYM64/" else 4
            n = struct.unpack(">Q" if w == 8 else ">I", body[:w])[0]
            names = body[w + w * n:].split(b"\0")
            if name in names or b"_" + name in names:      # i386 COFF: C names carry a leading underscore
                return 1
        elif not ident.startswith(b"/"):
            break                   # past the symbol tables
        pos += 60 + size + (size & 1)
    return 0


def snapshot(target, snap):
    if os.path.isdir(snap):
        shutil.rmtree(snap)
    for d in ("include", "src", os.path.join("src", "world")):
        os.makedirs(os.path.join(snap, d), exist_ok=True)
    with LibLock():
        lib = os.path.join(LIB, "build", target, "libsa1.a")
        if not os.path.exists(lib):
            raise SystemExit("no %s: run build.py --target %s first" % (lib, target))
        shutil.copy2(lib, os.path.join(snap, "libsa1.a"))
        for pat, d in (("include/*.h", "include"), ("src/*.h", "src"), ("src/world/*.h", os.path.join("src", "world"))):
            for h in glob.glob(os.path.join(LIB, pat)):
                shutil.copy2(h, os.path.join(snap, d, os.path.basename(h)))
    a = os.path.join(snap, "libsa1.a")
    print("[%s] snapshot: libsa1.a %d bytes (LIB build of %s)" % (
        target, os.path.getsize(a), time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(os.path.getmtime(a)))))


def build(target, out, snap, sadc):
    lib = os.path.join(snap, "libsa1.a")
    if not os.path.exists(lib):
        raise SystemExit("no library at %s: run without --no-snapshot" % lib)
    faces = archive_defines(lib, FACE_FN)
    print("[%s] faces lane in the archive: %s" % (target, "yes (E13 compiled in)" if faces else
                                                  "no (E13 reports: not run: faces not landed)"))
    flags = ["-target", TRIPLES[target], "-std=gnu11",
             "-I" + os.path.join(snap, "include"), "-I" + os.path.join(snap, "src"),
             "-I" + os.path.join(snap, "src", "world"), "-I" + os.path.join(sadc, "include"),
             "-I" + os.path.join(LIB, "tests"), "-Wall", "-Wextra", "-Wno-unused-function",
             "-ffp-contract=off", "-frounding-math", "-fno-builtin", "-O2", "-g", "-DSA1_STATIC",
             "-DEMOTE_TEST_FACES=%d" % faces]
    if target.startswith("x86-"):
        flags += ["-msse2", "-mfpmath=sse"]
    if "linux" in target:
        flags += ["-fPIC"]
    exe = os.path.join(out, "emote_test" + (".exe" if "windows" in target else ""))
    cmd = [sys.executable, "-m", "ziglang", "cc", *flags, os.path.join(LIB, "tests", "emote_test.c"), lib, "-o", exe]
    if "linux" in target:
        cmd += ["-lm", "-lpthread"]
    r = subprocess.run(cmd, cwd=LIB, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
    if r.stdout.strip():
        print(r.stdout.rstrip())
    if r.returncode != 0:
        raise SystemExit("FAILED: " + " ".join(cmd))
    print("[%s] built %s" % (target, exe))
    return exe


def run_one(k, exe, args, env, timeout, label):
    print("==== [%s] emote_test%s" % (k, label), flush=True)
    t0 = time.time()
    try:
        r = subprocess.run([exe] + args, cwd=LIB, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                           errors="replace", env=env, timeout=timeout)
        out, rc = r.stdout, r.returncode
    except subprocess.TimeoutExpired as e:
        out = (e.stdout or b"").decode("utf-8", "replace") if isinstance(e.stdout, bytes) else (e.stdout or "")
        out += "\nTIME LIMIT: emote_test ran longer than %d s (a hang: review R.8)\n" % timeout
        rc = 124
    sys.stdout.write(out)
    print("==== [%s] emote_test%s exit %d (%.0f s)" % (k, label, rc, time.time() - t0), flush=True)
    return rc, out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--target", default="windows", choices=sorted(TRIPLES) + ["windows", "all"])
    ap.add_argument("--sadc", default=os.path.join(os.path.dirname(LIB), "sadc"))
    ap.add_argument("--tsv", default=None, help="the research TSVs (default <sadc>/docs/research)")
    ap.add_argument("--gdi", default=os.environ.get("SA1_TEST_GDI", ""))
    ap.add_argument("--run", action="store_true")
    ap.add_argument("--no-snapshot", action="store_true")
    ap.add_argument("--no-guard", action="store_true")
    ap.add_argument("--timeout", type=int, default=1800)
    argv = sys.argv[1:]
    rest = argv[argv.index("--") + 1:] if "--" in argv else []
    a = ap.parse_args(argv[:argv.index("--")] if "--" in argv else argv)
    tsv = a.tsv or os.path.join(a.sadc, "docs", "research")
    r = subprocess.run([sys.executable, os.path.join(LIB, "tools", "gen_emote_names.py"), tsv, "--check"], cwd=LIB)
    if r.returncode != 0:
        raise SystemExit("gen_emote_names.py --check failed")
    if a.target == "all":
        keys = sorted(TRIPLES)
    elif a.target == "windows":
        keys = ["x86_64-windows", "x86-windows"]
    else:
        keys = [a.target]
    rc = 0
    det = {}
    for k in keys:
        out = os.path.join(LIB, "build", "emote_test", k)
        os.makedirs(out, exist_ok=True)
        snap = os.path.join(out, "snap")
        if not a.no_snapshot:
            snapshot(k, snap)
        exe = build(k, out, snap, a.sadc)
        if a.run and runnable(k):
            if not a.gdi:
                raise SystemExit("--run needs --gdi or SA1_TEST_GDI")
            env = dict(os.environ)
            env.pop("SA1_GUARD_ALLOC", None)
            args = ["--tsv", tsv] + rest + [a.gdi]
            code, text = run_one(k, exe, args, env, a.timeout, "")
            rc |= code != 0
            for line in text.splitlines():
                if line.startswith("DETERMINISM"):
                    det.setdefault(k, []).append(line.strip())
            if not a.no_guard and "--only" not in rest:
                env["SA1_GUARD_ALLOC"] = "1"
                code, _ = run_one(k, exe, ["--tsv", tsv, "--only", "memcheck,hold_memcheck", a.gdi], env, a.timeout,
                                  " (memcheck + hold_memcheck, SA1_GUARD_ALLOC=1)")
                rc |= code != 0
    if len(det) > 1:
        vals = [tuple(v) for v in det.values()]
        same = len(set(vals)) == 1
        print("determinism: %s (%s)" % ("identical" if same else "DIFFERENT",
                                         "; ".join("%s: %s" % (k, " ".join(v)) for k, v in sorted(det.items()))))
        rc |= not same
    sys.exit(1 if rc else 0)


if __name__ == "__main__":
    main()
