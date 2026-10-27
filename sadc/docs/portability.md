# Portability rules: one C source for SHC (matching) and the PC (libsa1)

Every decompiled file must (a) compile with SHC 5.1r03 to byte-identical code and (b) compile
as correct 32- and 64-bit PC code under `-DTARGET_PC` for libsa1 (`docs/libsa1_plan.md`).
PC differences live in headers and macros, never in forked copies of game code. These rules
come from `docs/research/critique.md` 4.3, 5.1, 5.3, 5.7, `libsm64.md` 10.4/10.11/10.12 and
`ecosystem.md` 3.6/3.7, and are enforced by `tools/pc_check.py`.

Headers: `include/common.h` (types, helpers), `include/ninja.h`, `include/task.h`,
`include/sa1/*.h` (shared engine/player types and prototypes; `sa1/sa1.h` includes all).

## 0. Principles

1. **SHC first.** Every helper below expands, under SHC, to exactly the expression the matched
   code needs. A PC fix must never change the SH-4 output: check with `tools/shcdiff.py` and the
   full build.
2. **Native layout on PC.** On 64-bit hosts structs are native (pointers are 8 bytes). Game data
   is never used in place: the runtime ripper deserialises it into these structs (libsm64 10.4
   option (a), ecosystem 3.7 option C). Hence all code must access memory through typed fields.
3. **`SA1_PTR32`** is 1 on SHC and on 32-bit x86. Only then do structs have the DC layout, and only
   then are layout assertions active. x86-32 is therefore the cheap "DC layout" test target.
4. Build the PC side with `-DTARGET_PC -DNON_MATCHING`: `#ifdef NON_MATCHING` bodies are the PC
   implementation of functions that do not match yet.

## 1. Types

| Rule | Why |
|---|---|
| Use `Sint8/Uint8/Sint16/Uint16/Sint32/Uint32/Float`. Never write `long` (or `short`/`int` where the width matters). | `Sint32` is `signed long` under SHC (kept: it matches); on PC it is `int32_t`. `long` is 64-bit on LP64 (linux64). |
| `Double` is `float` on PC. Do not use `double` or unsuffixed float literals in game code. | SHC `-fpu=single` evaluates `double` in single precision; PC would compute in double and round once. |
| `char` signedness: use `Sint8`/`Uint8`; PC builds use `-fsigned-char`. | SHC `char` is signed; ARM hosts default to unsigned. |
| No bit-fields and no `enum`-typed members in structs shared with data. | Allocation order and size are compiler-specific. |
| `Bool` is `Sint32`. `Int`/`Uint` are `int`. | |
| An integer that must hold a pointer is `SA1_UPTR` (e.g. `NJS_TEXNAME.texaddr`). | `Uint32` on SHC (same code), `uintptr_t` on PC. |

## 2. Struct layout and field access

* **Typed field access only** for any struct that contains (or is reached through) pointers:
  `pwp->mj.plactptr`, never `*(PL_ACTION **)((char *)pwp + 0x140)`. Matching rarely needs raw
  offsets; when SHC output needs an address expression, write it with `&p->field`.
* **`sizeof()` instead of DC literals** in allocations, copies and strides:
  `CAlloc(1, sizeof(playerwk))` (PInitialize's `CAlloc(1, 0x200)`), `n * sizeof(CCL_INFO)` (CCL_Init's
  `n * 0x34`). On SHC the value is identical (asserted), so the code is identical.
* **Assert layouts** next to every shared struct: `SA1_ASSERT_SIZE(type, size)` and
  `SA1_ASSERT_OFFSET(type, member, off)` (member may be `mj.spdp`). SHC checks sizes itself;
  offsets are checked by every x86-32 compile (`_Static_assert`) and by
  `py -3.12 tools/pc_check.py --layout` (SHC computes an offset table that is compared with the
  assertions; 422 checks pass today). SHC cannot fold `offsetof` into a constant expression,
  so the SHC form of `SA1_ASSERT_OFFSET` is a no-op typedef (an `extern` would shift SHC's
  internal label numbers).
* **Use the shared types from `include/sa1/`** (`playerwk`, `motionwk`, `colliwk`, `CCL_INFO`,
  `csts`, `PL_ACTION`, ...). Local "overlay" structs that reach a DC offset through padding
  (`Uint8 pad[0x7C - 0x3C]`) are tolerated in SHC-only work, but on 64-bit they read the wrong
  field of the real (native) struct. Replace them with the shared type before a file is used by
  libsa1. `pc_check` reports them (`padded-overlay`, `dc-layout-struct`).
* **Shared types whose name is still claimed by local copies** (`TOUCHINFO`, `forcewk`,
  `MOTION_CTX`, `xssunit`, `colobj`, `PL_FACE`, ...; list in `docs/header_migration.md`) are
  defined in the core headers as `sa1_<name>` (asserted like every shared struct, so they are in
  the `--layout` table) and get their canonical name from `include/sa1/pending.h`, which
  `sa1/sa1.h` does not include: SHC rejects a second definition of a struct tag or typedef
  (errors 2114/2115) even when it is identical. A file switches to `pending.h` only after it has
  deleted all of its local copies; globals and prototypes whose types are such names are declared
  there too (one declaration per function: never both in a core header and in `pending.h`).
  When the last local copy of a name is gone, the name moves into its core header.
* A macro may be defined both in a header and in a src file only if the two replacement lists are
  identical *including the trailing comment*: SHC 5.1 compares the text after the name, so
  `#define K 0x04` and `#define K 0x04 /* c */` are "2006 (E) Multiple #define's" (two
  definitions that both end in a comment are accepted). `EWP`, `EWP_POS` and the `NJD_*` flags in
  the headers follow the spelling of the existing src definitions.
* Never assume adjacency of separate globals or `sym+off` aliases. If tooling forces a separate
  symbol for `&B_8C78C608[1]` (e.g. `B_8C78C60C`), declare it under `TARGET_PC` as
  `#define B_8C78C60C (&B_8C78C608[1])`.

## 3. Pointers never live in integer or float fields

* A slot that holds a pointer must be read and written through a pointer member of a union:
  `TaskWkUnion` (`taskwk.counter/timer/value`, `motionwk.work`) has `.ptr`, `.tp`, `.pwp`;
  `playerwk.free` has `.ptr[]`; `playerwk.work` is a `TaskWkUnion`. `.l` keeps only 32 bits on x64.
  Known cases: PInitialize stores the `playerwk*` in `motionwk.work` (use `mwp->work.pwp`, macro
  `PWP(mwp)`); E-102 keeps a pointer in `pwp->free.ptr[0]`; objects keep tasks in `twp->value.ptr`.
* No pointer-to-integer casts (`(Sint32)ptr`, `(Uint32)ptr`), no address masking or mirror
  arithmetic (`0x0Cxxxxxx` vs `0x8Cxxxxxx`), no absolute DC addresses outside the startup and
  hardware code that libsa1 never compiles.
* Unknown 32-bit slots: when a field might hold a pointer, say so in the header
  (`CCL_INFO.unk30`) and resolve it before PC use.

## 4. Game data: extern only

* Game data in `8C111194-8C5FBB60` (physics table, action/weld lists, CCL_INFO tables, models,
  motions, texlists, start positions, voice tables) is **declared `extern`, never defined in C**.
  The matching build takes it from the asm data chunks; the PC build gets it from the runtime
  ripper (the user's own GDI). Short string literals and immediates in code are unavoidable and
  allowed (`pc_check` lists file-scope strings as `data-string`).
* Tables that the game **mutates** must be per instance on PC: `PJoinVertexes` writes `org` and
  the depth bytes into the shared weld table, and welds rewrite model vertex arrays (critique 4.2).
* Data tables holding **code pointers** (dispatch tables, object lists) are declared with typed
  function-pointer elements; the PC loader maps DC addresses to PC functions with a remap table.
* The physics table is `Sonic_Physics_Data[8]` indexed by character id (`sa1/player.h`); the
  per-character labels in `build/1ST_READ.symbols.txt` are stale until the next
  `configure.py --split`.

## 5. Floating point

### 5.1 Literals: the window rule

SHC converts decimal literals with `-round=zero` (truncation); PC compilers round to nearest.
`0.1f` is `0x3DCCCCCC` on SHC and `0x3DCCCCCD` on PC. **Write every float literal as a decimal
in the window `[B + ulp/16, B + 7*ulp/16]` above the wanted bits `B`**: truncation and rounding
then both give `B`. Examples (all checked with SHC and with round-to-nearest):

| bits | SHC-only spelling | portable spelling |
|---|---|---|
| `0x3DCCCCCC` | `0.1f` | `0.099999996f` |
| `0x3E4CCCCC` | `0.2f` | `0.19999999f` |
| `0x3FD55554` | `1.6666666f` | `1.66666654f` |
| `0x44888888` | `1092.2667f` | `1092.26663f` |
| `0x4622F986` | `10430.381f` | `10430.381f` (already in the window) |

`pc_check` prints the replacement for every literal whose bits differ
(`float-literal-rounding`). The rewrite does not change SHC output. Always suffix `f`.

### 5.2 Rounding mode, precision, contraction

* SH-4 runs with FPSCR `0x00040001` (round to zero, denormals flushed). The libsa1 runtime sets
  MXCSR to round-toward-zero with FTZ/DAZ for the duration of a tick and restores the host's exact
  MXCSR afterwards; never call back into the host while it is set. Integer-to-float conversions
  (`FLOAT`, `cvtsi2ss`) then also truncate (`pc_check` lists them as `-Wimplicit-int-float-conversion`).
* Compile with SSE2 scalar math (`-msse2 -mfpmath=sse` on x86-32, `/arch:SSE2`), never x87; no
  `-ffast-math`, `-march=native` or reassociation; `-ffp-contract=off` (MSVC `/fp:strict`).
  Prefer `-frounding-math` (GCC) so constant folding does not assume round-to-nearest.
* **Mandatory:** wherever the original asm has `FMAC`, write `SA1_FMAC(a, b, c)` (= `a * b + c` for
  SHC, `fmaf` on PC). SH-4 FMAC rounds once (verified by the harness), and libsa1 builds with
  `-ffp-contract=off`, so an unmarked `a * b + c` would round twice on PC and a contracted
  `a * b - c * d` (original FMUL/FMUL/FSUB) would round once: both drift (seen: floor normal of
  `func_8C027360` diverged by 1 ulp at reference-trace step 95). Count the FMACs in funcinfo's asm
  and make the number of `SA1_FMAC` uses in the function agree.
* SHC folds constant expressions at compile time; a folded constant in matched code may need to
  be written as its folded value if PC folding differs.

### 5.3 FSCA, FSRRA, FIPR, FTRV and libm

* Trig goes through `njSin/njCos/njTan` (FSCA, 16-bit BAMS) and normalisation through
  `njUnitVector` (FSRRA); inline uses go through the SHC intrinsics declared in
  `sa1/shc_intrinsics.h` (`_builtin_fsca`, `_builtin_fsrra`, `_builtin_fipr`, `_builtin_ftrv`,
  `_builtin_sqrtf`, `_builtin_fabsf`). On PC they are runtime functions (tolerance-checked
  until the hardware tables are characterised; FTRV uses the runtime's emulated XMTRX).
* Math library calls use the game's own wrappers (`func_8C10D0F8` atan2f-like, `8C10D008..8C10D198`),
  reimplemented in the runtime, never the host CRT (MSVC CRT and glibc differ).

## 6. Integer semantics

| Hazard | SH-4 / SHC | PC rule |
|---|---|---|
| Signed overflow (BAMS angles, timers) | wraps | compile with `-fwrapv` |
| Type punning through unions/casts | works | `-fno-strict-aliasing` |
| Integer division by zero | `__divls/__divlu/__modls/__modlu` return 0 (errno `B_8C7AC108 = 0x44E`), no trap | `SA1_DIV`, `SA1_MOD`, `SA1_UDIV`, `SA1_UMOD` for every non-constant divisor |
| `INT_MIN / -1` | DIV1 loop, no trap (wraps; confirm in harness) | same macros |
| float -> int out of range | `FTRC` saturates to `0x7FFFFFFF`/`0x80000000`, NaN -> `0x80000000` | `SA1_FTOI(f)` (then narrow) where the value can leave the range, always for BAMS angle conversions; C leaves it undefined, `cvttss2si` gives `0x80000000` |
| Narrowing (`(Sint16)ang`) | truncates | implementation-defined but modulo on GCC/Clang/MSVC: allowed |
| Shifts of negative values | arithmetic | implementation-defined/UB in C; GCC/Clang with `-fwrapv` behave like SH-4 |

The helper macros are plain C on SHC (`SA1_DIV(a, b)` is `((a) / (b))`), so writing them
never changes the matched code.

## 7. Uninitialised reads

Matched code sometimes reads a variable that is not set on every path (the SH-4 then uses
whatever the register held). On PC that is undefined behaviour and the compiler may do anything.
Rules: keep `-Wuninitialized -Wsometimes-uninitialized -Wconditional-uninitialized` clean or
annotated; where the original really reads garbage, initialise on PC only with
`PC_ONLY(x = 0;)` and note the original behaviour; run ASan/UBSan/MSan builds in the harness.

## 8. Calls, prototypes, SDK

* Every function and function pointer has a full prototype (no `()` declarations, no K&R).
  Unprototyped calls promote `float` to `double` on PC (harmless on SHC `-fpu=single`).
  No implicit declarations (an error in C99; SHC accepts them silently).
* **SDK functions are called by their SDK names** from `sa1/ninja_funcs.h` (`njSin`,
  `njPushMatrix`, `pdGetPeripheral`, ...). On SHC each name is `#define`d to its address symbol
  (`njSin` -> `func_8C63A970`), so the object is unchanged; on PC the names are the libsa1 runtime
  reimplementation, and address names are `#define`d back to them so older code links too. SDK
  code is never decompiled into libsa1.
* Engine and player functions are declared once in `sa1/engine.h`, `sa1/player.h`, ... under
  their address symbols with SADX-analogy aliases (`PInitialize` = `func_8C06C350`). New files
  include the header instead of writing local `extern` prototypes, so a wrong prototype becomes a
  compile error. Older files that define their own `colliwk`, `NJS_ARGB`, `PDS_PERIPHERAL`, ...
  do not include `sa1/` headers; migrate them file by file.
* `ASM_STUB(proto)` is only a prototype under `TARGET_PC`.

## 9. Globals and per-instance state (policy, implementation later)

* Access game globals only through their declared names (`B_8C754EC8`, or aliases such as
  `playertwp`, `Gravity`), never through computed addresses, so that libsa1 can later redirect
  them into a per-instance context with `#define` renames (libsm64's GlobalState pattern) without
  touching game code. No `static` locals for game state in new code.
* Native multi-instance support comes from the 8 player slots (`B_8C78C608/628/648/668`). State
  that must become per instance: `rand` (`func_8C10D334`), gravity, input records, camera,
  weld tables and the models they rewrite.
* Tick contract: one libsa1 tick = one logic step with `B_8C754EC4 = 1`, `B_8C754EC8 = 0`
  (critique 4.1).

## 10. PC compiler flags

| GCC / Clang | MSVC | Purpose |
|---|---|---|
| `-DTARGET_PC -DNON_MATCHING` | `/DTARGET_PC /DNON_MATCHING` | PC profile, C bodies of non-matching functions |
| `-std=gnu11` | `/std:c11` | `_Static_assert` |
| `-fwrapv` | (wraps by default) | BAMS arithmetic |
| `-fno-strict-aliasing` | (default) | type punning |
| `-ffp-contract=off`, no `-ffast-math` | `/fp:strict` | no FMA contraction, no reassociation |
| `-msse2 -mfpmath=sse` (x86-32) | `/arch:SSE2` | no x87 extended precision |
| `-fsigned-char` | `/J` must not be used | signed `char` |
| `-frounding-math` (GCC) | | no folding that assumes round-to-nearest |
| warnings: `-Wall -Wextra -Wpointer-to-int-cast -Wint-to-pointer-cast -Wshorten-64-to-32 -Wdouble-promotion -Wimplicit-float-conversion -Wstrict-prototypes -Wuninitialized` | `/W4` | see `tools/pc_check.py` |

## 11. Checking: `tools/pc_check.py`

```
py -3.12 tools/pc_check.py                 # all src/**/*.c; x86_64-windows, x86-windows, x86_64-linux
py -3.12 tools/pc_check.py src/1st_read/code_8C0869E0.c --json build/pc_check/report.json
py -3.12 tools/pc_check.py --layout        # SA1_ASSERT_* vs SHC and x86
```

It compiles with `zig cc` (clang 21) and reports compiler diagnostics, AST findings
(`int-div`, `float-to-int`, `double-arith`), source findings (`float-literal-rounding` with the
replacement literal, `double-literal`, `long`, `data-table`, `raw-offset`, `abs-address`) and
layout findings (`dc-layout-struct`: a local struct whose offset comments hold on 32-bit but not
on x86_64; `padded-overlay`; `offset-comment`). zig 0.16's `cc -fsyntax-only` fails on Windows,
so objects go to `build/pc_check/`; a per-run define defeats zig's cache, which otherwise
suppresses diagnostics.

**Status of `src/1st_read` (93 files, 2026-10-06):** 2 files fail to compile on PC
(`code_8C0869E0.c` calls `_builtin_fabsf` without a declaration; `code_8C08ABC0.c` calls the
undeclared `func_8C08EE98`), 103 float literals in 22 files need the window spelling,
87 float-to-int conversions in 28 files (SA1_FTOI candidates), 4 integer divisions by a
runtime divisor in 3 files, 1 double-precision expression (`code_8C0869E0.c:251`,
`sc[i] * 0.05`), 2 pointer-to-int casts (startup code `code_8C010180.c`), 18 uses of
possibly uninitialised variables in 8 files, 14 unprototyped declarations in 3 files, 2
missing returns, 1 returned stack address (`code_8C010320.c`, startup), 9 absolute addresses
(startup/VMU code), 28 padded overlay structs in 18 files and 13 structs whose layout moves on
x86_64. No `-Wshorten-64-to-32` and no `long`. The headers in `include/` compile without
warnings on all three targets.

## 12. Checklist for a decompiled file

1. Include `common.h` and the `sa1/` headers you need (plus `sa1/pending.h` for the names listed
   in `docs/header_migration.md`); no local copies of shared structs.
2. Typed fields, `sizeof`, no raw offsets, no pointers in integers (unions with pointer members).
3. Data `extern` only; per-instance copies for data the code writes.
4. Float literals in the window, `f` suffix, no `double`; `SA1_FMAC` where SHC emitted FMAC.
5. `SA1_DIV/SA1_MOD` for runtime divisors, `SA1_FTOI` for float-to-int that can leave the range.
6. Full prototypes; SDK by name; no implicit declarations.
7. `py -3.12 tools/shcdiff.py <func> <file> -v` (still matches) and
   `py -3.12 tools/pc_check.py <file>` (clean or each finding justified).