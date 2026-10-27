# LibSA1: libsa1 and the Sonic Adventure (Dreamcast) decompilation

This repository has two parts:

| Folder | What it is |
|---|---|
| [`libsa1/`](libsa1/README.md) | **libsa1**: Sonic Adventure's own code running as a library. It is host-agnostic C, in the spirit of libsm64. A host program (a game mod, an engine, a tool) creates characters, feeds them input and collision, ticks them at the game's 60 Hz and gets back their state, render batches and sounds. The physics, the character mode machines, the animations and the effects are the game's decompiled code, not a re-creation. The assets are read at run time from **your own disc image**. [G-SA1](https://github.com/ExtCan/G-SA1), the Garry's Mod port, is built on it. |
| [`sadc/`](sadc/README.md) | **SADC**, a matching decompilation of *Sonic Adventure* for the Sega Dreamcast, US release v1.005 (MK-51000). The build reproduces the original executable and its 248 code overlays byte for byte. Functions are rewritten as C that compiles, with the original Hitachi **SHC 5.1 Release 03** compiler, to exactly the original machine code. |

How they fit together: `sadc/src` holds the decompiled C. `sadc/tools/pclower.py` lowers that same C into a portable
dialect that runs in a guest arena on a PC. libsa1 links the lowered game code with its own runtime (a reimplemented
Ninja / Katana SDK subset), its asset loader and its public API (`libsa1/include/libsa1.h`).

Status: 1,280 of the 4,123 game functions of `1ST_READ.BIN` are decompiled, 23 % of the game code's bytes. They cover
the engine, the physics and all the playable characters, plus parts of the Perfect Chaos, Emerald Coast, Red Mountain
and Tikal overlays. libsa1 replays the reference recordings of the original game bit-exactly on x86 and x64 (see
`libsa1/README.md`).

## Bring your own disc

**No game data is in this repository**: no disc image, no executable or overlays, no textures, models, motions, sounds
or music, no generated assembly. You need your own dump of **Sonic Adventure (USA) v1.005** for the Dreamcast as a
`.gdi` with its tracks. Its `1ST_READ.BIN` must have the SHA-1 `e74c849806db103aa87f84e41a382873bc96b6df`. Everything
the build needs from the game is extracted from that dump on your machine and stays there. `.gitignore` keeps it out of
git.

## Building

Requirements: Windows (the SHC toolchain is Windows-only; libsa1 itself also builds for Linux), Python 3.12, and
`pip install pyyaml ninja ziglang==0.16.0`. libsa1 refuses another zig version unless you pass `--allow-unpinned`.

### 1. Extract your disc and split it (`sadc/`)

```
cd sadc
py -3.12 tools/extract_gdi.py "path\to\Sonic Adventure.gdi" disc     # the ISO9660 file system -> disc/
py -3.12 tools/extract_overlays.py                                   # disc -> baserom/1ST_READ.BIN, baserom/overlays/
py -3.12 configure.py --split                                        # analysis -> asm/, build/*.symbols.txt, build.ninja
```

`configure.py --split` generates the assembly, the symbol tables and the cross-references that both the matching
build and libsa1's lowering use.

### 2. The matching build (optional)

Place the Hitachi SH toolchain, SHC 5.1r03 (`shc.exe`, `asmsh.exe`, `lnk.exe`, `rof2bin.exe` and their message files),
in `sadc/tools/shc/5.1r03/bin/`. The decomp.me compiler mirror has it as `shc-v5.1r03`. Then:

```
py -3.12 -m ninja          # assemble, compile, splice, link, verify: ends with "1ST_READ.BIN: OK (<sha1>)"
```

The flags the game was compiled with are in `sadc/tools/compiler.json`:
`-cpu=sh4 -endian=little -fpu=single -division=cpu -optimize=1 -speed -macsave=0 -extra=a=1800 -round=zero`.
`sadc/docs/compiler.md` explains how they were found. To decompile a function, see "Decompiling a function" in
[`sadc/README.md`](sadc/README.md).

### 3. libsa1

```
cd libsa1
py -3.12 build.py                                     # x86_64-windows, x86-windows, x86_64-linux
py -3.12 build.py --target x86_64-windows --test      # + the tests (set SA1_TEST_GDI=<your .gdi> for the disc tests)
```

`build.py` finds the decompilation at `../sadc` (`--game DIR` to change it). It lowers the C with `sadc/tools/pclower.py`
and builds `build/<target>/libsa1.a`, the shared library (`sa1.dll` / `libsa1.so`) and the test programs. `CMakeLists.txt`
is there for hosts that build with their own compiler. [`libsa1/README.md`](libsa1/README.md) has the API, the tests and
the details.

### Tests and the interpreter harness

`unit_test` and most of libsa1's tests need nothing but your disc (`SA1_TEST_GDI`). The strict replay tests compare
libsa1 with **reference traces** recorded from the original game code. Those recordings are game memory, so they are not
distributed. `sadc/harness` is an SH-4 interpreter that runs the original code from your own `baserom/`, and it can
record the traces on your machine (`sadc/harness/README.md`). Without them, the replay tests are skipped.

## Layout

```
libsa1/            the library: include/ (public API), src/ (asset layer, world core, runtime rt/), tests/, gen/
                   (boundary stub policy), tools/, build.py, CMakeLists.txt
sadc/
  config/          split configs and symbol names for 1ST_READ and the 248 overlays
  src/             decompiled C, per image (1st_read/, overlays/)
  include/         shared headers (sa1/ game types, pc/ portability)
  tools/           splitter, SH-4 decoder, PRS codec, SHC driver, match checker and splicer, pclower, Ghidra export scripts
  harness/         the SH-4 interpreter harness (sources only)
  docs/            compiler, layout, portability, libsa1 design and research notes
  baserom/         (your files; only README.md is in git)
```

The working copies of these files also contain things that are **not** published here: generated assembly, the
`baserom/` and `disc/` files, build outputs, the downloaded toolchains (SHC, Ghidra, JDK), recorded traces, and a
`reference/` folder of third-party symbol tables and strings extracted from the game binary. The names that the
decompilation uses are in `sadc/config/`. Some notes mention documents that were left out because they analyse private
test sessions.

## License

The code in this repository (libsa1, the decompiled C, the tools and the harness) is licensed under the GNU General
Public License version 3 ([LICENSE](LICENSE)). See [NOTICE](NOTICE) for credits and third-party terms. Sonic Adventure
is SEGA's. This is a fan project, and SEGA is not affiliated with it and does not endorse it.

## Credits

* **ExtCan**, the project's owner, directed the work and made its design decisions. **Claude** (Anthropic) co-wrote it.
* SA Tools (X-Hax): the data-label configurations for the Dreamcast release that seeded many names.
* Exant64's [sa2dc](https://github.com/Exant64/sa2dc), the matching decompilation of *Sonic Adventure 2* (Dreamcast).
  It established the Hitachi-toolchain approach that SADC follows.
* [decomp.me](https://decomp.me), for preserving the SHC compiler builds.
* [libsm64](https://github.com/libsm64/libsm64) and [G64](https://github.com/ckosmic/g64), the model for libsa1 and
  G-SA1.
