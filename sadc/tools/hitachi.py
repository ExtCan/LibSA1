#!/usr/bin/env python3
"""Run a Hitachi SH toolchain program (shc / asmsh / lnk / rof2bin).

Usage: hitachi.py [--ver 5.0r28] [--cwd DIR] [--out-rename SRC DST] <tool> [args...]

Sets SHC_LIB / SHC_TMP (the tools refuse to run without them) using a
private temp dir per invocation so parallel builds do not collide, and turns
assembler/compiler error reports into a non-zero exit status.
"""
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
DEFAULT_VER = os.environ.get("SHC_VER", "5.0r28")


def main():
    args = sys.argv[1:]
    ver = DEFAULT_VER
    cwd = None
    rename = None
    quiet_ok = True
    while args and args[0].startswith("--"):
        if args[0] == "--ver":
            ver = args[1]
            args = args[2:]
        elif args[0] == "--cwd":
            cwd = args[1]
            args = args[2:]
        elif args[0] == "--out-rename":
            rename = (args[1], args[2])
            args = args[3:]
        elif args[0] == "--verbose":
            quiet_ok = False
            args = args[1:]
        else:
            sys.exit(f"unknown option {args[0]}")
    tool = args[0]
    # Hitachi tools parse '/' as an option switch: hand them Windows paths
    fixed = []
    for a in args[1:]:
        if a.startswith("-") and "=" in a:
            k, v = a.split("=", 1)
            a = k + "=" + v.replace("/", "\\")
        elif not a.startswith("-"):
            a = a.replace("/", "\\")
        fixed.append(a)
    args = [tool] + fixed
    bindir = os.path.join(ROOT, "tools", "shc", ver, "bin")
    exe = os.path.join(bindir, tool + ".exe")
    if not os.path.exists(exe):
        sys.exit(f"missing {exe}")
    tmp = tempfile.mkdtemp(prefix="shc_")
    env = dict(os.environ, SHC_LIB=bindir, SHC_TMP=tmp, SHC_INC=os.path.join(ROOT, "include"))
    try:
        r = subprocess.run([exe] + args[1:], env=env, cwd=cwd, capture_output=True)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    out = r.stdout.decode("cp932", errors="replace") + r.stderr.decode("cp932", errors="replace")
    errors = [l for l in out.splitlines()
              if "(E)" in l or "(F)" in l or "ABORT" in l or " ERROR" in l.upper() and "TOTAL ERRORS" not in l.upper()
              and "ERRORS       0" not in l]
    failed = r.returncode != 0 or any("(E)" in l or "(F)" in l or "ABORT" in l for l in out.splitlines())
    if tool == "shc" and "(E)" not in out and r.returncode == 0:
        failed = False
    if failed or not quiet_ok:
        sys.stdout.write(out)
    if failed:
        sys.exit(1)
    if rename:
        src = os.path.join(cwd or ".", rename[0])
        shutil.move(src, rename[1])


if __name__ == "__main__":
    main()
