# Sonic Adventure (Dreamcast) decompilation

A work-in-progress **matching decompilation** of *Sonic Adventure* for the Sega
Dreamcast, US release v1.005 (MK-51000), in the spirit of the Super Mario 64
decompilation: the build reproduces the original executable byte for byte,
and functions are progressively rewritten as C that compiles, with the
original Hitachi SHC compiler, to exactly the original machine code.

| Image | Load address | Status |
|-------|--------------|--------|
| `1ST_READ.BIN` (main executable, 6.7 MB) | `0x8C010000` | builds byte-identical; 6,493 functions identified |
| 248 code overlays (`SONICADV/*.PRS`: stages, adventure fields, bosses, Chao, minigames, 207 cutscenes) | `0x0C900000`, `0x0C920000`, `0x0CB80000`, `0x8C900000`, ... | every `.BIN` and its re-compressed `.PRS` build byte-identical |

In total the build verifies 497 outputs covering 24,252 functions (5.15 MB of code).

Run `py -3.12 tools/progress.py` for current decompilation progress.

## How it works

```
disc image ──extract_gdi.py──> disc/ ──> baserom/1ST_READ.BIN, baserom/overlays/*.BIN
                                              │
                    config/<image>.yaml ──> tools/split/split.py
                                              │  recursive-descent analysis of SH-4 code,
                                              │  literal pools, jump tables, pointers
                                              v
                         asm/<image>/text/*.src   (code, cut where no PC-relative
                         asm/<image>/data/*.src    reference crosses: ~1 source file each)
                                              │
 src/<image>/*.c ──shc──> .src ──splice.py──> │  decompiled functions replace their asm
                                              v
                         asmsh ──> .obj ──> lnk ──> .abs ──> rof2bin ──> build/<image>.BIN
                                                                         (SHA-1 checked)
```

* The build uses the **original Hitachi SH toolchain** (`shc`, `asmsh`, `lnk`)
  from Sega's Katana SDK era, natively on Windows. The game was compiled with
  **SHC 5.1 Release 03** (see `docs/compiler.md` for how this was established).
* Overlays are re-compressed with a reimplementation of SEGA's PRS encoder that
  reproduces all 849 retail `.PRS` files exactly (`tools/prs.py`).
* SHC shares one literal pool between all functions of a translation unit.
  `tools/splice.py` therefore splices compiled functions into the original
  chunk and re-points their PC-relative operands at the original pool, after
  verifying the function matches (`tools/shcdiff.py`). Any non-matching
  function fails the build.
* Assembly is **generated** from your own disc and is not committed.

## Setup

Requirements: Windows, Python 3.12 (`py -3.12`), `pip install pyyaml ninja`.

1. **Game data.** Extract your own disc dump (GDI format):
   ```
   py -3.12 tools/extract_gdi.py "path/to/Sonic Adventure.gdi" disc
   copy disc\1ST_READ.BIN baserom\1ST_READ.BIN
   py -3.12 tools/extract_overlays.py
   ```
   `baserom/1ST_READ.BIN` must have SHA-1 `e74c849806db103aa87f84e41a382873bc96b6df`.
2. **Compiler.** Place the Hitachi SH toolchain (SHC 5.x: `shc.exe`,
   `asmsh.exe`, `lnk.exe`, `rof2bin.exe` and their message files) in
   `tools/shc/<version>/bin/`. The decomp.me compiler mirror provides
   these packages (`shc-v5.0r10` ... `shc-v5.1r13`). The version used is set in
   `tools/compiler.json`.
3. **Optional analysis tools.** Ghidra 12 + JDK 21 in `tools/` enable
   `tools/ghidra/run_ghidra.ps1`, whose pseudo-C drafts are shown by
   `tools/funcinfo.py`.

## Building

```
py -3.12 configure.py --split     # analyse baserom, generate asm/ and build.ninja
py -3.12 -m ninja                 # assemble, compile, splice, link, verify
```

A successful build ends with `1ST_READ.BIN: OK (<sha1>)`.

## Decompiling a function

```
py -3.12 tools/funcinfo.py func_8C04A168          # asm, pool constants, xrefs, Ghidra draft
# write C into src/1st_read/<file>.c
py -3.12 tools/shcdiff.py func_8C04A168 src/1st_read/<file>.c -v   # MATCH / DIFF
py -3.12 configure.py && py -3.12 -m ninja         # full build re-verifies everything
```

Conventions:

* Functions are named `func_<ADDR>`, data `D_<ADDR>`, BSS `B_<ADDR>` until
  they are given real names in `config/symbols.<image>.txt`
  (`name = 0x8C0xxxxx;`), after which `configure.py --split` regenerates the
  asm with the new names.
* A C file may hold any functions; the build finds them by name. Callees
  that the original reaches with `BSR`/`BRA` must be defined in the same file
  (SHC only emits short branches within a translation unit); declare
  still-assembly neighbours with `ASM_STUB(prototype)` from `include/common.h`.
* Data stays in the assembly data chunks; C refers to it with `extern`.
  String literals and static constants written in C are compared byte-wise
  with the original data they replace.
* Work that does not match yet goes under `#ifdef NON_MATCHING`.

## Repository layout

| Path | Contents |
|------|----------|
| `config/` | split configs and symbol names per image |
| `src/` | decompiled C, per image |
| `include/` | shared headers |
| `tools/sh4/isa.py` | SH-4 decoder (validated against `asmsh` on all 65,536 encodings) |
| `tools/split/` | analysis, asm emission, cross-references |
| `tools/shcdiff.py`, `tools/splice.py` | match checking and C/asm splicing |
| `tools/prs.py`, `tools/extract_overlays.py` | SEGA PRS (de)compression, overlay extraction |
| `tools/ghidra/` | headless Ghidra export of functions and pseudo-C |
| `reference/` | symbol and data-label research (SA Tools, SADX, SA2 DC); local only, not in the public repository |
| `docs/` | notes on the compiler, file formats and engine |

## Credits

* SA Tools (X-Hax) data-label configurations for the Dreamcast release.
* Exant64's [sa2dc](https://github.com/Exant64/sa2dc), the matching
  decompilation of *Sonic Adventure 2* (Dreamcast), which established the
  Hitachi-toolchain approach this project follows.
* decomp.me for preserving the SHC compiler builds.
