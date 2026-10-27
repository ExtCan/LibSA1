# Header migration: from local struct copies to the shared headers

The decompilation agents wrote about 170 files in `src/1st_read` before the shared headers had
the types they needed, so many files define their own copy, view or padded overlay of a shared
struct and declare engine functions and globals locally. The header steward pass (2026-10-06)
merged the well-evidenced fields and types into `include/` (`build/decomp_reports/L1L2_type_notes.md`,
the local definitions and the asm). This file says, for each src file, what to switch to. A later
pass migrates the files one by one and re-verifies each with `tools/shcdiff.py`. The rules are in
`docs/portability.md` section 2.

State after the pass: `py -3.12 tools/pc_check.py --layout` runs 422 layout checks with 0
mismatches; x86 `_Static_assert`s pass, and the x86-64 parse of the headers is clean. The full
recompile (`build/src/1st_read/*.src` deleted, then `configure.py` and `ninja build/1ST_READ.ok`)
still prints `1ST_READ.BIN: OK`. `pc_check` on all files reports no new diagnostic.

## 1. Why some shared types are called `sa1_<name>`

SHC 5.1 rejects a second definition of a struct tag or typedef, even an identical one
("2115 (E) Multiple tag declarations", "2114 (E) Multiple variable declarations"). A header can
also not tell whether the file that includes it defines a local copy afterwards. So while any src
file still defines `TOUCHINFO`, `forcewk`, `MOTION_CTX`, and so on, the shared definition cannot
use that name in a header that the file includes. It also cannot go in a header that `sa1/sa1.h`
does not include, because then `pc_check --layout` (which compiles `#include "sa1/sa1.h"`) would
not check it.

The pass resolves this as follows:

* The core header defines the type as `sa1_<name>` (tag and typedef). It is asserted with
  `SA1_ASSERT_*`, so it is in the `--layout` table and in every translation unit.
* `include/sa1/pending.h` (not included by `sa1/sa1.h`) contains `typedef sa1_<name> <name>;`
  for each such name. It also declares the globals and functions whose types are these names.
  A function is declared in only one place: a core header or `pending.h`, never both.
* Function-pointer typedefs (`MOTION_FUNC`, `ROTATE_FUNC`) follow the same scheme. The externs
  typed with them (`B_8C88FE24`, `D_8C19AD44`, ...) are already in the core headers, because a
  function-pointer type is compatible with the local typedef of the same signature.
* Pointer fields that point to such types stay as they are (`task.fwp`, `task.awp`, `task.ocp`
  `void *`, `eventwk.list40/buf44/list4C` `void *`, `playerwk.pfp` `struct pl_face *`).
  Changing them would make the existing local copies incompatible. `pending.h` gives typed
  accessors: `FWP(tp)`, `AWP(tp)`, `PFP(pwp)`, and `EWP_FACE(ewp)` (DC layout only).

## 2. Migrating one file

1. Delete the file's local copies of the types listed for it below and rename the fields as
   listed. Remove every local copy of every name that `pending.h` defines, or SHC reports 2114/2115.
2. If any of its types maps to a `sa1/pending.h` name, add `#include "sa1/pending.h"`. It includes
   `sa1/sa1.h`. Files that include only some of `common.h`/`ninja.h`/`task.h`/`sa1/...` may need
   more includes. Watch for new name clashes: their other local names must not collide with header
   names. The per-file list flags the known clashes.
3. Delete the header workaround (`#define func_X func_X_hdr` around the include), if any, and the
   local declarations that the headers now provide. Entries marked `*` have a different local
   signature. Adopt the header one and fix the call sites, for example `njPushMatrix(0)` becomes
   `njPushMatrix(NULL)`. The ASM_STUB definitions stay: they are needed for BSR/BRA, and their
   prototype must equal the header one. Exception: pad masks that a file declares `Sint32`
   (`D_8C19E38C`, `D_8C19E390`) because its match needs signed operands. Use the header
   (`Uint32`) declaration and put `(Sint32)` casts on both operands at the uses (section 4,
   item 4).
4. Remove the casts made redundant by type corrections in this pass (section 3), or keep them;
   both compile to the same code.
5. Verify: `py -3.12 tools/shcdiff.py <func> src/1st_read/<file>` for every matched function of
   the file; then the full build (delete `build/src/1st_read/*.src`, `py -3.12 configure.py && py
   -3.12 -m ninja build/1ST_READ.ok` -> `1ST_READ.BIN: OK`; the cc rule has no header
   dependencies); then `py -3.12 tools/pc_check.py src/1st_read/<file>`.
6. When no src file defines a pending name any more, move it into its core header. Rename
   `sa1_<name>` to `<name>` in the definition and the asserts, delete its `pending.h` line, and move
   the declarations that use it. For pointer fields (`fwp`, `pfp`, `list40`, ...), give the field
   its real type at the same time. When `pending.h` is empty, fold it into `sa1/sa1.h`.

Pending names (core definition, header, and the local variants to rename):

| Name | Core type (header) | Local variants (files) |
|---|---|---|
| `forcewk` | `sa1_forcewk` (task.h) | forcewk (8C056A04, 8C074CF0, 8C076782, 8C098444, 8C0988E4, 8C0AFB10), sonic_forcewk (8C0A966C) |
| `anywk` | `sa1_anywk` (task.h) | 8C098444, 8C0988E4 |
| `OBJ_CONDITION` | `sa1_OBJ_CONDITION` (task.h) | 8C098444 |
| `NJS_MKEY_F/A/P`, `NJS_MDATA1..4` | `sa1_NJS_*` (sa1/ninja_types.h) | 8C03FDC0, 8C03FF90, 8C040200, 8C040596, 8C0406A0, 8C040A60, 8C040CE0, 8C041000, 8C04124E, 8C0414CC, 8C041666, 8C057BC0, 8C079C20, 8C07A49C, 8C07B080; MTN_MDATA3 (8C079476) |
| `MEMBLK` | `sa1_MEMBLK` (sa1/engine.h) | 8C098998 (end/last), 8C098B0C (nextphys/prevphys) |
| `CHILDTASKINFO` | `sa1_CHILDTASKINFO` (sa1/engine.h) | 8C098B0C |
| `facewk` | `sa1_facewk` (sa1/engine.h) | 8C04B7BA, 8C04BC70, 8C07B080 |
| `EV_ACTION_LIST`, `EV_PATH`, `EV_PATH_LIST`, `EV_PATH_POINT` | `sa1_EV_*` (sa1/engine.h) | 8C04B440, 8C04B7BA |
| `PATHTAG`, `PATHTBL`, `PATHINFO` | `sa1_PATH*` (sa1/engine.h) | 8C06F530, 8C078308, 8C07BE68, 8C08966E; PATH_TAG/PATH_TBL (8C089AA0) |
| `OBJECT_DRAW_FUNC` | `sa1_OBJECT_DRAW_FUNC` (sa1/engine.h) | 8C057460 |
| `NUMDISP` | `sa1_NUMDISP` (sa1/engine.h) | 8C07BAC0, UNK_NUMDISP (8C08ABC0); NUMDISP in 8C057780 is a different struct, NUMLAYOUT |
| `MOTION_CTX`, `MOTION_FUNC`, `ROTATE_FUNC` | `sa1_MOTION_CTX`, `sa1_MOTION_FUNC`, `sa1_ROTATE_FUNC` (sa1/motion.h) | MOTION_CTX (10 files), MOTION_EVAL (8C03FF90, 8C040596) |
| `TOUCHINFO`, `TOUCHPOLY` | `sa1_TOUCHINFO`, `sa1_TOUCHPOLY` (sa1/collision.h) | TOUCHINFO (9 files), TOUCHHIT+TOUCHINFO (8C076782), COLHIT (8C028100), CL_HITPOLY (8C02770E) |
| `colpoly`, `colobj`, `colbucket` | `sa1_colpoly`, `sa1_colobj`, `sa1_colbucket` (sa1/collision.h) | colpoly/COLPOLY, NEARPOLY, FDPOLY, LCPOLY, LANDPOLY; colobj, NEARLIST, FDOBJ, LCOBJ, LANDPOLYLIST |
| `xssunit`, `zxsdwstr`, `xyyzzxsdwstr` | `sa1_*` (sa1/collision.h) | 8C029400, 8C02AD22, 8C02BC60, 8C02C100, 8C02CA00, 8C02D060, 8C02D410, 8C07BAC0; UNK_8C016C60_unit/info |
| `MESHBUF`, `COLHITCTX`, `LANDRAY`, `LINECOLLI`, `DEATHZONE` | `sa1_*` (sa1/collision.h) | MESHBUF (5 files), STRIPBUF (8C028620), CL_STRIPBUF/CL_HITLIST (8C02770E), COLHITCTX (8C028100), LANDRAY (8C02B1A0), LINECOLLI (8C02A680), DEATHZONE (8C07BAC0) |
| `PL_TARGET` | `sa1_PL_TARGET` (sa1/player.h) | 8C06CFCC, 8C0AEFA0, 8C0AF5A0; SONIC_TARGET (8C0B16BC) |
| `PL_FACE`, `PL_FACE_STEP` | `sa1_PL_FACE`, `sa1_PL_FACE_STEP` (sa1/player.h) | struct pl_face (8C07B080, 8C07B2B8), FACEWK (8C0B2338) |
| `PL_SPDUPWK`, `DROWNWK`, `PL_LOOKTIMEWK`, `SPDLIMITWK`, `lookwk` | `sa1_*` (sa1/player.h) | 8C07D060/8C07D2A2/8C07D3A0; 8C06C6A0/8C07BAC0/8C07BE68; 8C07C3C0; 8C0B2E30/8C0B2EA0 + SONIC_8C0B2EA0_AWP (8C0B2D40); 8C07AEF8/8C0B2890 + LOOKWK (8C0B2338) |
| `BUBBLE`, `BUBBLE_PARAM` | `sa1_BUBBLE`, `sa1_BUBBLE_PARAM` (sa1/player.h) | BUBBLE (8C0B37F2, 8C0B3B1A), PTCL_8C0B33A0; BUBBLEPARAM, BUBBLE_PARAM, EMITPRM_8C0B3480 |

New shared types with their final names (nothing to wait for; use them directly): `NJS_SPHERE`
(sa1/ninja_types.h); `NODE_WELD` (sa1/motion.h); `NODE_MTN_EVAL`, `NUMLAYOUT`, eventwk
`look_mode`/`look_ang`/`look_pos`/`look_tp` (sa1/engine.h); `PL_AIWK`, `SONIC_SUPERWK`
(sa1/player.h); the `TWF_*` taskwk.flag bits (task.h); `NJD_EVAL_*`/`NJD_MTYPE_*` (ninja.h).

## 3. Corrections in this pass that make src workarounds redundant

Type corrections (all verified by the full build; no existing offset or size changed):

* `taskwk.id`, `taskwk.btimer`: `Sint8` -> `Uint8` (every read is MOV.B + EXTU.B). The
  `(Uint8)twp->id` / `(Uint8)twp->btimer` casts can go.
* `playerwk.equipment`, `playerwk.item`: `Uint16` -> `Sint16` (MOV.W + TST with no EXTU.W in
  8C06F6C0, 8C071D40, 8C0A77BC, 8C0AEFA0, 8C0AFB10, 8C0B11C0, 8C0B3280, 8C07D3A0, which matches
  SADX). `(Sint16)pwp->equipment`, `(Sint16)pwp->item` and `*(Sint16 *)&pwp->item` can go.
* `eventwk.lock`, `eventwk.mode`: `Sint8` -> `Uint8` (EXTU.B before the compares in 8C06CE40,
  8C06EAC0, 8C06EFCE, 8C0AFE72, 8C04BD9E). The `(Uint8)ewp->lock` casts can go.
* `PL_JOIN_VERTEX.inpmode`: `Uint8` -> `Sint8` (switch without EXTU.B in 8C07A5AC, 8C04C10E).
* `GetStageNumber` returns `Uint16` (callers EXTU.W, and the older files already declare it
  `Uint16`). The `(Uint16)GetStageNumber()` casts can go.

Prototype corrections (the `#define X X_hdr` workarounds can go). The self-referencing alias macros (`#define GetStageNumber GetStageNumber`, `CreateElementalTask`, `PSetMotion`, `CCL_Init`, ...) were removed, as the engine.h header comment asks once a symbol is renamed:
`func_8C029B00(csts *, NJS_OBJECT *, Uint32)`, `Sint32 func_8C028BFE`,
`func_8C0583A0(NJS_MATRIX_PTR base, NJS_ACTION *, Float frame, Sint32 node, NJS_MATRIX_PTR out)`,
`func_8C058434(Sint32, NJS_MATRIX_PTR)`, `Sint32 func_8C057B40` (+ `njPushUnitMatrix`),
`func_8C06C5CA(Sint32 pno)`, `Sint32 func_8C0730D8`,
`Sint32 func_8C07CEF2(Uint8, Sint32, NJS_POINT3 *, Angle3 *)`,
`func_8C63FE54(NJS_SPHERE *, NJS_SPHERE *)` (+ `njCollisionCheckSS`),
`Float func_8C63FF84(NJS_POINT3 *, NJS_LINE *, NJS_POINT3 *)`,
`func_8C64016C(NJS_PLANE *, NJS_PLANE *, NJS_LINE *)`, `Sint32 njPushMatrix`,
`Float njRandom(Float n)` (a fraction helper, not a random generator), and `memset` with the C
library prototype on PC (this removes the x86-64 header warning).

New in the headers: 52 types (315 fields), 4 eventwk fields (the look-at block that was pad01), and about 360 declarations (216 functions and 145 globals, counting sa1/pending.h) that src files had repeated (player
common 8C06C1D8-8C07DC2A, land collision, engine/eventwk, motion evaluator globals, Sonic helpers,
camera `D_8C18B7EC` = `camera_twp`, unit matrix `D_8C67C640`, Ninja matrix stack, position history
`B_8C78C688/690`, `B_8C792690`, ...). They have the signatures of the defining files.

## 4. Unresolved conflicts (not merged; decide, then migrate)

1. **task size.** The SA1 DC task is 0x30 bytes. The pool init 8C09844A MAllocs 0x30000 for
   0x1000 tasks and links them with ADD #48, and DestroyTask touches only 0x00-0x2C (matched
   `code_8C098444.c`). `task.thp` (0x30) is a SADX field. It is kept because existing sizes are
   frozen, and the conflict is documented in task.h. Removing it changes `sizeof(task)` on every
   target. `code_8C098444.c` uses its own `TASK_SIZE`.
2. **playerwk.path_point** (0x14) is written with MOV.W (8C07C0E4, 8C07C120), so it is a `Sint16`
   followed by 2 unused bytes. The header keeps `Sint32` because field sizes are frozen.
   `code_8C07BE68.c` and `code_8C07C120.c` write `*(Sint16 *)&pwp->path_point`.
3. **CreateElementalTask(Uint16 im, ...)**: `code_8C098444.c` defines it with `Uint16` (MOV.W/EXTU.W
   of the parameter, needed to match). `sa1/engine.h` keeps `Sint32` because `code_8C098B0C.c`
   ASM_STUBs it with `Sint32` and includes engine.h. Change all three at once.
4. **Signedness of the pad masks.** `PDS_PERIPHERAL.on/press` and `D_8C19E38C/390/3A0`:
   8C06CEBE, 8C0B14E0, 8C0B1C56 and code_8C084BA0.c match only with `(Sint32)` casts on both
   operands, while 8C06CEFE, 8C0B12E0 and 8C0B1352 match with the `Uint32` header types. They stay
   `Uint32`, with casts where needed (note in sa1/input.h). `D_8C19E3A4` is `Sint32` (only that
   spelling matches `func_8C06C32C`).
5. **Arguments that the definitions do not declare.** `func_8C06CFCC` and `func_8C06CF60` are
   defined as `(taskwk *)`, but callers pass mwp in R5 (8C06F386, 8C0B1C78, 8C0B16F4, 8C0B17DE).
   `func_8C06CEBE` is defined as `(taskwk *)`, but the Sonic code passes pwp. `func_8C075F8C` takes
   4 arguments, but 8C0777E0 pushes a 5th (attr, unused, like func_8C075A40). Add the unused
   parameters to the definitions, then declare them in sa1/player.h.
6. **pno types that disagree:** `func_8C07CE40` (definition `Uint8`, 3 callers `Sint32`),
   `func_8C07C9D2`, `func_8C07C580` and `func_8C07C676` (Uint8 / Sint8 / Sint32),
   `func_8C07BA0A` (3rd parameter Sint8 or Sint16), `func_8C0B27EE` (Sint8 or Uint8,
   `NJS_OBJECT **` or `void **`), `func_8C0AF5A0` (returns Sint32 or taskwk *), `func_8C0AFE72` (1 or
   3 parameters), `func_8C01AD2C` (`void (*)(void)`/Sint32 or `void (*)(void *)`/Sint8). None of
   these is declared in a header yet; check each callee (EXTU.B in the callee means `Uint8`).
7. **func_8C057586:** defined `(from, to, Float dist, out)`, and the float must come third for
   matching; code_8C0876F4.c declares and calls it as `(a, b, c, Float t)`. **func_8C027360:**
   defined with `Float *` (post-increment codegen), while callers use `NJS_POINT3 *`.
   **func_8C10D0A8:** `(Float y, Float x)` in code_8C02D410.c, `(Float x)` in code_8C02D060.c
   (hand-written module). None of them is declared.
8. **VectorAngle** stays `(void *, void *, void *)` because code_8C055D20.c defines it so. The real
   arguments are `const NJS_VECTOR *`. **func_8C64133E** stays `(void *handler, Uint32 arg)`;
   the handler is a `void (*)(void)` (code_8C06C0C0.c casts). **func_8C02CA00** stays
   `(void *, void *)` until code_8C07BAC0.c uses the shared `zxsdwstr`.
9. **Globals declared in more than one spelling:** `B_8C88F62C`/`B_8C788C40`
   (Sint32 scalars in code_8C056860.c, Uint32 and `Uint32[1]` in code_8C05697C.c; each spelling is
   required for its match). `B_8C78B451` is an array in code_8C0A77A0.c/code_8C014480.c and a scalar
   in code_8C08A198.c/code_8C08A71E.c (the array form matches). `D_8C112470` is `char[]` in one
   file and `const char[]` in another. None of them is in a header.
10. **colliwk.mytask (0x10)** is read as the owner task (8C06C880, 8C079234, 8C05E6E8). SonicMain
    copies `cwp->mytask->twp->ang.x/z` when `twp->flag & 0x20` (8C0A77F4), which SADX does with the
    collided task. Verify before renaming.
11. **eventwk 0x28 and 0x5C.** 0x28 is a union (`Angle ang[3]` / `NJS_POINT3`). It stays `ang[3]`
    because src uses `ewp->ang`, and `EWP_POS(ewp)` gives the point view. The face block 0x5C-0x6B
    must be a nested struct for matching (ADD #92), but src uses the flat names on PC. When
    code_8C04B7BA.c, code_8C04BC70.c and code_8C07B080.c migrate, replace the flat fields with
    `facewk fc;` (the member name must differ from the old `face` field) and use `&ewp->fc`
    everywhere. `pad6C` (an NJS_POINT3 model offset) keeps its name because code_8C04BC70.c uses it.
12. **The RANGE-poly pool `B_8C6BE27C`:** 8C028E60 initialises 0x2000 entries of 0x40 bytes, but
    the symbol `B_8C6C398C` starts at +0x5710. Check the symbol split before giving the array a size.
13. **`B_8C88FE40`/`B_8C88FE5C`** are probably one `MOTION_CTX[2]` (FE5C = FE40 + 0x1C). They
    stay two externs, as in most files.
14. **Agent naming conflicts resolved from the asm:** PL_FACE fields are signed (`MOV.B` without
    EXTU.B at 8C07B190/8C07B19A/8C07B1F8), so code_8C07B2B8.c's `Uint8 step` is wrong. The BUBBLE
    fields 0x20/0x24/0x28 are aspect, size and maxsize (draw 8C0B3F2C sets sprite sx = +0x24 and
    sy = +0x20 * +0x24), not alpha, scl_spd and scl. BUBBLE_PARAM 0x10/0x14 are dist/spd along the
    weld vector (8C0B3480). DROWNWK.pno is `Uint8` (EXTU.B at 8C07BE94). The look byte at
    eventwk 0x04 is only tested for zero (8C0B2928), so it is kept `Sint8` as in the matched
    code_8C07C298.c. MOTION_CTX.idx is `Uint32` (8 files), against `Sint32` in the 2 MOTION_EVAL files.

## 5. Candidates that were not merged (views disagree)

* Animal work (0x6C, `task.mwp` of the animals): code_8C01593C.c, code_8C015EE0.c,
  code_8C01622A.c, code_8C0168A8.c, code_8C016C60.c, code_8C017360.c, code_8C017828.c,
  code_8C017A5C.c and code_8C015820.c agree on spd 0x08, acc 0x14, exec 0x60 and param 0x68. They
  disagree at 0x38-0x44 and 0x64 (`Sint32` against a function pointer).
* AI task creation parameters (`UNK_8C082D60_arg`, `UNK_8C084260_arg`, `UNK_8C084AE0_arg`,
  `UNK_8C083F7C_t`): 0x04/0x08 are tasks or other objects, and 0x10 is a TaskFuncPtr or a Sint32.
* Single-file types: SA1_EFFECT*/SA1_SE_AWP/SA1_TIMER_AWP (code_8C07B500.c), SPLINE_TAG and
  SPLINE_INFO (code_8C089AA0.c), save/backup/VMS types (code_8C010400.c-code_8C0114BC.c,
  code_8C0886F4.c, code_8C088C74.c), UNK_8C6AEF84_t (three different views),
  UNK_8C082D60_work's 0x14 callback, and SA1_TMU (hardware registers, code_8C06C0C0.c).

## 6. Per-file list

Generated from the local definitions and declarations as of this pass. Local types are listed with
their shared replacement. "Header workaround" lists what the corrected prototypes make
unnecessary. "Local declarations" lists the functions and globals that a header now declares; `*`
marks a local signature that differs from the header one.
### code_8C0130F8.c

Includes: `common.h`, `ninja.h`.

* Local types -> shared:
  * `PDS_PERIPHERAL` -> `PDS_PERIPHERAL` (sa1/input.h)
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/input.h: `B_8C754C08`, `B_8C754DA8`; sa1/ninja_funcs.h: `njCalcPoint`*, `njCalcVector`*, `njPopMatrix`*, `njPushMatrix`*, `njRotateY`*, `njRotateZ`*, `njUnitMatrix`*.

### code_8C013860.c

Includes: `common.h`, `ninja.h`.

* Local types -> shared:
  * `NJS_POINT2COL` -> `NJS_POINT2COL` (sa1/ninja_types.h): col is NJS_COLOR * (code_8C013860.c has Uint32 *)
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/ninja_funcs.h: `njPrintC`*.

### code_8C013F20.c

Includes: `common.h`, `ninja.h`.

* Local types -> shared:
  * `NJS_POINT2COL` -> `NJS_POINT2COL` (sa1/ninja_types.h): col is NJS_COLOR * (code_8C013860.c has Uint32 *)

### code_8C0141E0.c

Includes: `common.h`.

* Local types -> shared:
  * `PDS_PERIPHERAL` -> `PDS_PERIPHERAL` (sa1/input.h)
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/input.h: `B_8C754DA8`.

### code_8C014480.c

Includes: `common.h`, `ninja.h`, `task.h`.

* Local types -> shared:
  * `UNK_ARGB` -> `NJS_ARGB` (sa1/ninja_types.h)
  * `UNK_SPRITE` -> `NJS_SPRITE` (sa1/ninja_types.h): tlist/tanim typed (were void *)
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/engine.h: `B_8C754EC8`, `CreateElementalTask`, `FreeTask`, `GetPlayerNumber`, `GetStageNumber`, `tcrf_LevelSelectVar`; sa1/ninja_funcs.h: `func_8C6088BC`*, `func_8C608C82`*, `njColorBlendingMode`*, `njDrawSprite2D`*, `njSetConstantMaterial`*, `njSetTexture`*.

### code_8C014A78.c

Includes: `common.h`, `ninja.h`, `task.h`, `1st_read/code_8C014480.c`.

* Local types -> shared:
  * `NJS_ARGB` -> `NJS_ARGB` (sa1/ninja_types.h)
  * `NJS_SPRITE` -> `NJS_SPRITE` (sa1/ninja_types.h): tanim is NJS_TEXANIM *
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/engine.h: `B_8C754EC8`, `CreateElementalTask`, `GetPlayerNumber`, `GetStageNumber`; sa1/ninja_funcs.h: `func_8C6088BC`*, `func_8C608C82`*, `njColorBlendingMode`*, `njDrawSprite2D`*, `njSetConstantMaterial`, `njSetTexture`*, `njSin`.

### code_8C015220.c

Includes: `common.h`, `ninja.h`.

* Local types -> shared:
  * `NJS_ARGB` -> `NJS_ARGB` (sa1/ninja_types.h)
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/ninja_funcs.h: `njSetConstantMaterial`.

### code_8C01593C.c

Includes: `common.h`, `ninja.h`, `task.h`.

* Local types -> shared:
  * `UNK_cwk_head` -> `colliwk` (sa1/ccl.h): prefix view
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/ccl.h: `CCL_Init`*, `func_8C02F63C`; sa1/engine.h: `B_8C754EC8`, `DestroyTask`, `FreeTask`, `GetStageNumber`, `MFree`, `func_8C0919CC`, `func_8C09413A`, `rand`; sa1/player.h: `B_8C78C608`.

### code_8C015EE0.c

Includes: `common.h`, `task.h`.

* Local types -> shared:
  * `UNK_8C015EE0_colli` -> `colliwk` (sa1/ccl.h): view
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/engine.h: `atan2f`, `func_8C0940DA`.

### code_8C016C60.c

Includes: `common.h`, `ninja.h`, `task.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `UNK_8C016C60_info` -> `xyyzzxsdwstr` (sa1/pending.h): unit -> hit
  * `UNK_8C016C60_unit` -> `xssunit` (sa1/pending.h)
  * `UNK_8C754EF4_t` -> `_OBJ_LANDCOLL` (sa1/collision.h): attr -> slAttribute, obj -> pObject, tp -> pTask
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/collision.h: `B_8C754EF0`, `B_8C754EF4`*, `func_8C0522C0`; sa1/engine.h: `rand`; sa1/ninja_funcs.h: `njCos`; sa1/pending.h: `func_8C029400`*.
* Note: Its func_8C029400 prototype (UNK_8C016C60_info *, void *, Sint32) becomes the pending.h one (xyyzzxsdwstr *, NJS_OBJECT *, Uint32).

### code_8C017360.c

Includes: `common.h`, `ninja.h`, `task.h`.

* Local types -> shared:
  * `UNK_animal_mwk` -> `motionwk` (task.h): spd
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/ccl.h: `func_8C02F63C`; sa1/engine.h: `AdjustAngle`, `func_8C036F12`, `func_8C038D00`, `func_8C040942`; sa1/ninja_funcs.h: `njPopMatrix`*, `njPushMatrix`*, `njRotateXYZ`*, `njRotateY`*, `njScale`*, `njSetTexture`*, `njTranslate`*; sa1/player.h: `B_8C78C608`.

### code_8C027360.c

Includes: `common.h`, `ninja.h`, `sa1/ninja_funcs.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `MESHBUF` -> `MESHBUF` (sa1/pending.h)
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/collision.h: `tcrf_CollisionDisplayVar`.
* Note: func_8C027360 is defined with Float * parameters here; callers (code_8C02770E.c, code_8C029400.c, code_8C02C100.c) use NJS_POINT3 * and code_8C02B620.c Float *: not declared in a header yet (pick Float *, callers pass &p.x).

### code_8C02770E.c

Includes: `common.h`, `ninja.h`, `sa1/sa1.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `CL_HITLIST` -> `COLHITCTX` (sa1/pending.h): count -> nbHit, tested -> nbCheck, unk0C -> range2, len -> spd, step -> halfr
  * `CL_HITPOLY` -> `TOUCHINFO` (sa1/pending.h): onpoly2 -> pshbk, height/height2 -> dist/dist2
  * `CL_STRIPBUF` -> `MESHBUF` (sa1/pending.h): nbVertex -> n, v -> buf
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/ninja_funcs.h: `D_8C67C640`.

### code_8C028100.c

Includes: `common.h`, `ninja.h`, `sa1/sa1.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `COLHIT` -> `TOUCHINFO` (sa1/pending.h): unk50/unk54 -> dist/dist2
  * `COLHITCTX` -> `COLHITCTX` (sa1/pending.h)
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/collision.h: `D_8C112460`; sa1/pending.h: `B_8C6BACF0`*.

### code_8C028480.c

Includes: `common.h`, `ninja.h`, `task.h`, `sa1/collision.h`.

* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/collision.h: `func_8C02B620`; sa1/pending.h: `B_8C6BB27C`*, `B_8C6BE27C`*, `func_8C02CE80`*, `func_8C02CECA`*.
* Note: func_8C02CE80/func_8C02CECA take colpoly */colobj * in sa1/pending.h (void *pool here); B_8C6BE27C/B_8C6BB27C typed.

### code_8C028620.c

Includes: `common.h`, `ninja.h`, `sa1/ninja_funcs.h`, `sa1/shc_intrinsics.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `COLPOLY` -> `colpoly` (sa1/pending.h)
  * `STRIPBUF` -> `MESHBUF` (sa1/pending.h): v -> buf
  * `colpoly` -> `colpoly` (sa1/pending.h): unk08.. -> center/r/v/attr
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/collision.h: `B_8C73EDEC`, `B_8C73EDF0`, `tcrf_CollisionDisplayVar`; sa1/engine.h: `func_8C02CF68`; sa1/pending.h: `B_8C6BB270`*, `B_8C6BB274`, `func_8C028B00`*, `func_8C02CFC0`*.
* Note: func_8C028B00 is (NJS_OBJECT *, Uint32, colobj *) in sa1/pending.h (void *grp here). B_8C6BE27C/B_8C6BB27C are typed colpoly[]/colobj[] there.

### code_8C028BDC.c

Includes: `common.h`, `ninja.h`, `task.h`, `sa1/ninja_funcs.h`, `sa1/collision.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `NEARLIST` -> `colobj` (sa1/pending.h): head/tail -> polys/last, unk04 -> prev (void *)
  * `NEARPOLY` -> `colpoly` (sa1/pending.h): rr -> r
* Header workaround to drop: #define func_8C028BFE func_8C028BFE_void (the header now returns Sint32).
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/collision.h: `B_8C73E27C`, `func_8C028BFE`, `tcrf_CollisionDisplayVar`; sa1/pending.h: `B_8C73E280`*, `D_8C02D548`*.

### code_8C028E60.c

Includes: `common.h`, `ninja.h`, `sa1/sa1.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `colobj` -> `colobj` (sa1/pending.h)
  * `colpoly` -> `colpoly` (sa1/pending.h): unk08.. -> center/r/v/attr
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/collision.h: `B_8C73E27C`, `B_8C73E400`, `B_8C73EDC4`, `B_8C73EDEC`, `B_8C73EDF0`, `B_8C73EDF4`, `D_8C196044`, `tcrf_CollisionDisplayVar`; sa1/ninja_funcs.h: `D_8C67C640`; sa1/pending.h: `B_8C6BB27C`, `B_8C6BE27C`, `D_8C02D548`, `func_8C028B00`, `func_8C02CE80`, `func_8C02CECA`, `func_8C02CEF4`, `func_8C02CF48`, `func_8C02CFA4`, `func_8C02D00E`.

### code_8C029400.c

Includes: `common.h`, `ninja.h`, `sa1/sa1.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `FDOBJ` -> `colobj` (sa1/pending.h): poly/head/tail -> polys/last, attr is Uint32
  * `FDPOLY` -> `colpoly` (sa1/pending.h): rr -> r
  * `xssunit` -> `xssunit` (sa1/pending.h)
  * `xyyzzxsdwstr` -> `xyyzzxsdwstr` (sa1/pending.h)
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/pending.h: `D_8C02D548`*.

### code_8C029B00.c

Includes: `common.h`, `ninja.h`, `task.h`, `sa1/ninja_funcs.h`, `sa1/shc_intrinsics.h`, `sa1/engine.h`, `sa1/collision.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `COLPOLY` -> `colpoly` (sa1/pending.h)
  * `NEARLIST` -> `colobj` (sa1/pending.h): head/tail -> polys/last, unk04 -> prev (void *)
  * `TOUCHINFO` -> `TOUCHINFO` (sa1/pending.h)
  * `TOUCHPOLY` -> `TOUCHPOLY` (sa1/pending.h)
  * `colpoly` -> `colpoly` (sa1/pending.h): unk08.. -> center/r/v/attr
* Header workaround to drop: #define func_8C029B00 func_8C029B00_hdr and the local prototype (the header now has (csts *, NJS_OBJECT *, Uint32)).
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/collision.h: `B_8C73E400`, `B_8C73EDC8`, `B_8C73EDF8`, `tcrf_CollisionDisplayVar`; sa1/engine.h: `D_8C196034`; sa1/ninja_funcs.h: `D_8C67C640`; sa1/pending.h: `B_8C6BACF0`, `B_8C73E404`, `D_8C02D548`*.

### code_8C02A680.c

Includes: `sa1/sa1.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `LCOBJ` -> `colobj` (sa1/pending.h): unk04 -> prev
  * `LCPOLY` -> `colpoly` (sa1/pending.h): radius -> r, vtx -> v, unk04 -> prev
  * `LINECOLLI` -> `LINECOLLI` (sa1/pending.h)
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/ninja_funcs.h: `D_8C67C640`; sa1/pending.h: `D_8C02D548`*.

### code_8C02AD22.c

Includes: `common.h`, `ninja.h`, `sa1/collision.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `xssunit` -> `xssunit` (sa1/pending.h)
  * `zxsdwstr` -> `zxsdwstr` (sa1/pending.h)
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/pending.h: `func_8C02BC60`.

### code_8C02B0B4.c

Includes: `common.h`, `ninja.h`, `sa1/ninja_types.h`.

* Header workaround to drop: include sa1/ninja_funcs.h (func_8C63FF84 returns Float there).
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/engine.h: `func_8C057B18`; sa1/ninja_funcs.h: `func_8C63FF84`, `njDistanceP2P`.

### code_8C02B1A0.c

Includes: `common.h`, `ninja.h`, `task.h`, `sa1/ninja_funcs.h`, `sa1/collision.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `LANDPOLY` -> `colpoly` (sa1/pending.h): rr -> r
  * `LANDPOLYLIST` -> `colobj` (sa1/pending.h): head -> polys
  * `LANDRAY` -> `LANDRAY` (sa1/pending.h)
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/collision.h: `func_8C066660`, `func_8C066732`; sa1/pending.h: `D_8C02D548`*.

### code_8C02B620.c

Includes: `common.h`, `ninja.h`, `task.h`, `sa1/ninja_funcs.h`, `sa1/shc_intrinsics.h`, `sa1/engine.h`, `sa1/collision.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `MESHBUF` -> `MESHBUF` (sa1/pending.h)
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/pending.h: `func_8C0273A2`, `func_8C0274B8`, `func_8C0275E0`.

### code_8C02BC60.c

Includes: `common.h`, `ninja.h`, `sa1/sa1.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `MESHBUF` -> `MESHBUF` (sa1/pending.h)
  * `xssunit` -> `xssunit` (sa1/pending.h)
  * `zxsdwstr` -> `zxsdwstr` (sa1/pending.h)
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/collision.h: `D_8C112460`; sa1/pending.h: `func_8C0273A2`, `func_8C0274B8`, `func_8C0275E0`.

### code_8C02C100.c

Includes: `common.h`, `ninja.h`, `sa1/sa1.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `MESHBUF` -> `MESHBUF` (sa1/pending.h)
  * `xssunit` -> `xssunit` (sa1/pending.h)
  * `xyyzzxsdwstr` -> `xyyzzxsdwstr` (sa1/pending.h)
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/collision.h: `D_8C112460`; sa1/pending.h: `func_8C0273A2`, `func_8C0274B8`, `func_8C0275E0`.

### code_8C02CA00.c

Includes: `common.h`, `ninja.h`, `sa1/ninja_funcs.h`, `sa1/engine.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `MESHBUF` -> `MESHBUF` (sa1/pending.h)
  * `colobj` -> `colobj` (sa1/pending.h)
  * `xssunit` -> `xssunit` (sa1/pending.h)
  * `zxsdwstr` -> `zxsdwstr` (sa1/pending.h)
* Header workaround to drop: include sa1/collision.h once func_8C02CA00 is typed there (see code_8C07BAC0.c).
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/pending.h: `D_8C02D548`, `func_8C0273A2`, `func_8C0274B8`, `func_8C0275E0`.

### code_8C02CE80.c

Includes: `common.h`, `ninja.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `colpoly` -> `colpoly` (sa1/pending.h): unk08.. -> center/r/v/attr
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/pending.h: `D_8C02D540`.

### code_8C02CEA0.c

Includes: `common.h`, `ninja.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `colobj` -> `colobj` (sa1/pending.h)
  * `colpoly` -> `colpoly` (sa1/pending.h): unk08.. -> center/r/v/attr
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/pending.h: `D_8C02D540`, `D_8C02D544`, `D_8C02D548`.

### code_8C02CEF4.c

Includes: `common.h`, `ninja.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `colobj` -> `colobj` (sa1/pending.h)
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/pending.h: `D_8C02D544`, `D_8C02D548`.

### code_8C02CF20.c

Includes: `common.h`, `ninja.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `colobj` -> `colobj` (sa1/pending.h)
  * `colpoly` -> `colpoly` (sa1/pending.h): unk08.. -> center/r/v/attr
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/pending.h: `D_8C02D540`, `D_8C02D544`, `D_8C02D548`.

### code_8C02CFA4.c

Includes: `common.h`, `ninja.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `colbucket` -> `colbucket` (sa1/pending.h)
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/pending.h: `D_8C02D560`.

### code_8C02CFC0.c

Includes: `common.h`, `ninja.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `colbucket` -> `colbucket` (sa1/pending.h)
  * `colobj` -> `colobj` (sa1/pending.h)
  * `colpoly` -> `colpoly` (sa1/pending.h): unk08.. -> center/r/v/attr
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/pending.h: `D_8C02D540`, `D_8C02D560`.

### code_8C02D060.c

Includes: `common.h`, `ninja.h`, `sa1/sa1.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `FDOBJ` -> `colobj` (sa1/pending.h): poly/head/tail -> polys/last, attr is Uint32
  * `FDPOLY` -> `colpoly` (sa1/pending.h): rr -> r
  * `xssunit` -> `xssunit` (sa1/pending.h)
  * `zxsdwstr` -> `zxsdwstr` (sa1/pending.h)
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/collision.h: `D_8C02D54C`, `D_8C02D964`; sa1/pending.h: `D_8C02D548`*.
* Note: func_8C10D0A8 is declared (Float x) here and (Float y, Float x) in code_8C02D410.c: check the core 8C10E6A0 before declaring it in sa1/engine.h. func_8C02D278 takes its vector in FR4-FR6, not by pointer (docs/fixlist.md), so it is not declared in a header either.

### code_8C02D410.c

Includes: `common.h`, `ninja.h`, `sa1/sa1.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `xssunit` -> `xssunit` (sa1/pending.h)
  * `zxsdwstr` -> `zxsdwstr` (sa1/pending.h)
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/collision.h: `D_8C02D54C`, `D_8C02D964`.
* Note: See code_8C02D060.c (func_8C10D0A8).

### code_8C03FDC0.c

Includes: `common.h`, `ninja.h`, `sa1/shc_intrinsics.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `MOTION_CTX` -> `MOTION_CTX` (sa1/pending.h)
  * `NJS_MKEY_F` -> `NJS_MKEY_F` (sa1/pending.h)
  * `NJS_MKEY_P` -> `NJS_MKEY_P` (sa1/pending.h)
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/motion.h: `B_8C749100`, `B_8C749102`, `B_8C749104`, `B_8C749106`, `B_8C88FE24`, `B_8C88FE28`, `B_8C88FE2C`, `B_8C88FE30`, `B_8C88FE34`, `D_8C19ACE0`, `D_8C19ACF4`; sa1/pending.h: `B_8C88FE40`, `B_8C88FE5C`.

### code_8C03FF90.c

Includes: `sa1/sa1.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `MOTION_EVAL` -> `MOTION_CTX` (sa1/pending.h): idx becomes Uint32 (was Sint32): re-verify
  * `NJS_MKEY_A` -> `NJS_MKEY_A` (sa1/pending.h)
  * `NJS_MKEY_F` -> `NJS_MKEY_F` (sa1/pending.h)
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/pending.h: `B_8C88FE40`*.

### code_8C040200.c

Includes: `common.h`, `ninja.h`, `sa1/shc_intrinsics.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `MOTION_CTX` -> `MOTION_CTX` (sa1/pending.h)
  * `NJS_MKEY_P` -> `NJS_MKEY_P` (sa1/pending.h)
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/pending.h: `B_8C88FE5C`.

### code_8C0403C0.c

Includes: `common.h`, `ninja.h`, `sa1/ninja_funcs.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `MOTION_CTX` -> `MOTION_CTX` (sa1/pending.h)
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/engine.h: `func_8C037098`; sa1/motion.h: `B_8C749104`, `B_8C89002C`; sa1/pending.h: `B_8C88FE5C`.

### code_8C040596.c

Includes: `sa1/sa1.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `MOTION_EVAL` -> `MOTION_CTX` (sa1/pending.h): idx becomes Uint32 (was Sint32): re-verify
  * `NJS_MDATA1` -> `NJS_MDATA1` (sa1/pending.h)
  * `NJS_MDATA2` -> `NJS_MDATA2` (sa1/pending.h): p[] is void * (SDK): cast to the key type
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/motion.h: `B_8C88FE2C`, `B_8C88FE30`, `B_8C88FE34`; sa1/pending.h: `B_8C88FE40`*.

### code_8C0406A0.c

Includes: `sa1/sa1.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `MOTION_CTX` -> `MOTION_CTX` (sa1/pending.h)
  * `NJS_MDATA1` -> `NJS_MDATA1` (sa1/pending.h)
  * `NJS_MDATA2` -> `NJS_MDATA2` (sa1/pending.h): p[] is void * (SDK): cast to the key type
  * `NJS_MDATA3` -> `NJS_MDATA3` (sa1/pending.h)
  * `NJS_MDATA4` -> `NJS_MDATA4` (sa1/pending.h)
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/engine.h: `func_8C037098`; sa1/motion.h: `B_8C88FE24`, `B_8C88FE2C`, `B_8C88FE30`, `B_8C88FE34`, `B_8C890074`; sa1/pending.h: `B_8C88FE40`, `B_8C88FE5C`.

### code_8C040A60.c

Includes: `sa1/sa1.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `MOTION_CTX` -> `MOTION_CTX` (sa1/pending.h)
  * `NJS_MKEY_F` -> `NJS_MKEY_F` (sa1/pending.h)
  * `NJS_MKEY_P` -> `NJS_MKEY_P` (sa1/pending.h)
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/motion.h: `B_8C88FE80`, `B_8C88FE88`, `B_8C88FE98`, `B_8C88FEA0`, `B_8C88FEA8`, `B_8C88FF28`, `B_8C88FF2C`, `B_8C88FF30`, `B_8C88FF34`, `B_8C88FF38`, `B_8C89002A`, `D_8C19AD20`, `D_8C19AD34`, `D_8C19AD44`; sa1/pending.h: `B_8C88FEB8`, `B_8C88FEF0`.

### code_8C040CE0.c

Includes: `sa1/sa1.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `MOTION_CTX` -> `MOTION_CTX` (sa1/pending.h)
  * `NJS_MKEY_A` -> `NJS_MKEY_A` (sa1/pending.h)
  * `NJS_MKEY_F` -> `NJS_MKEY_F` (sa1/pending.h)
  * `NJS_MKEY_P` -> `NJS_MKEY_P` (sa1/pending.h)
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/motion.h: `B_8C88FF38`, `B_8C88FF40`, `B_8C88FF42`, `B_8C88FF44`, `B_8C88FF4C`, `B_8C88FF7C`, `B_8C88FFAC`; sa1/pending.h: `B_8C88FEB8`, `B_8C88FEF0`.

### code_8C041000.c

Includes: `sa1/sa1.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `MOTION_CTX` -> `MOTION_CTX` (sa1/pending.h)
  * `NJS_MKEY_P` -> `NJS_MKEY_P` (sa1/pending.h)
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/motion.h: `B_8C88FF30`, `B_8C88FF38`, `B_8C88FF46`, `B_8C88FF48`, `B_8C88FFFC`, `B_8C89000C`; sa1/pending.h: `B_8C88FEF0`.

### code_8C04124E.c

Includes: `sa1/sa1.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `MOTION_CTX` -> `MOTION_CTX` (sa1/pending.h)
  * `NJS_MKEY_P` -> `NJS_MKEY_P` (sa1/pending.h)
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/motion.h: `B_8C88FF30`, `B_8C88FF38`, `B_8C88FF48`, `B_8C88FF4C`, `B_8C88FFFC`, `B_8C89000C`, `B_8C89002C`; sa1/pending.h: `B_8C88FEF0`.

### code_8C0414CC.c

Includes: `sa1/sa1.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `MOTION_CTX` -> `MOTION_CTX` (sa1/pending.h)
  * `NJS_MDATA1` -> `NJS_MDATA1` (sa1/pending.h)
  * `NJS_MDATA2` -> `NJS_MDATA2` (sa1/pending.h): p[] is void * (SDK): cast to the key type
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/motion.h: `B_8C88FE98`, `B_8C88FEA0`, `B_8C88FEA8`, `B_8C88FF38`, `B_8C88FF7C`, `B_8C88FFAC`; sa1/pending.h: `B_8C88FEB8`.

### code_8C041666.c

Includes: `sa1/sa1.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `MOTION_CTX` -> `MOTION_CTX` (sa1/pending.h)
  * `NJS_MDATA1` -> `NJS_MDATA1` (sa1/pending.h)
  * `NJS_MDATA2` -> `NJS_MDATA2` (sa1/pending.h): p[] is void * (SDK): cast to the key type
  * `NJS_MDATA3` -> `NJS_MDATA3` (sa1/pending.h)
  * `NJS_MDATA4` -> `NJS_MDATA4` (sa1/pending.h)
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/motion.h: `B_8C88FE98`, `B_8C88FEA0`, `B_8C88FEA8`, `B_8C88FF38`, `B_8C88FF3C`, `B_8C88FF40`, `B_8C88FF42`, `B_8C88FF44`, `B_8C88FF4C`, `B_8C88FF7C`, `B_8C88FFAC`, `B_8C88FFFC`, `B_8C89000C`, `D_8C19AD44`; sa1/pending.h: `B_8C88FEB8`, `B_8C88FEF0`.

### code_8C043A20.c

Includes: `common.h`, `ninja.h`, `task.h`.

* Local types -> shared:
  * `NJS_ARGB` -> `NJS_ARGB` (sa1/ninja_types.h)
  * `NJS_POINT3COL` -> `NJS_POINT3COL` (sa1/ninja_types.h)
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/ninja_funcs.h: `njDrawLine3D`*, `njSetConstantMaterial`.

### code_8C0486A0.c

Includes: `common.h`, `task.h`.

* Local types -> shared:
  * `UNK_ccl` -> `CCL_INFO` (sa1/ccl.h): flag4 -> flag
  * `UNK_cwk` -> `colliwk` (sa1/ccl.h): view
  * `UNK_pwk` -> `playerwk` (sa1/player.h): view; see the file note
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/engine.h: `CreateElementalTask`, `rand`; sa1/player.h: `B_8C78C608`.
* Note: UNK_pwk here is NOT a playerwk (Float at 0x008, Uint16 at 0x04E, Float at 0x1A0): keep it local.

### code_8C04B440.c

Includes: `sa1/sa1.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `EV_ACTION_LIST` -> `EV_ACTION_LIST` (sa1/pending.h): next is typed (was void *)
  * `ev_action_list` -> `EV_ACTION_LIST` (sa1/pending.h): struct tag
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/engine.h: `func_8C038CEC`, `func_8C041C92`, `func_8C04B48C`, `func_8C04B82E`, `func_8C04BF00`.

### code_8C04B7BA.c

Includes: `common.h`, `ninja.h`, `sa1/sa1.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `EV_PATH` -> `EV_PATH` (sa1/pending.h)
  * `EV_PATH_LIST` -> `EV_PATH_LIST` (sa1/pending.h): next is typed (was void *)
  * `EV_PATH_POINT` -> `EV_PATH_POINT` (sa1/pending.h)
  * `facewk` -> `facewk` (sa1/pending.h): SHC: EWP_FACE(ewp) replaces the local FACEWK(); PC keeps the flat eventwk fields
* Header workaround to drop: #define njRandom njRandom_header_proto (the header now has Float njRandom(Float n); keep the njFraction alias local if wanted).
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/engine.h: `D_8C112AFC`, `D_8C112B08`; sa1/ninja_funcs.h: `njRandom`.

### code_8C04BC70.c

Includes: `common.h`, `ninja.h`, `task.h`, `sa1/ninja_types.h`, `sa1/ninja_funcs.h`, `sa1/engine.h`, `sa1/ccl.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `facewk` -> `facewk` (sa1/pending.h): SHC: EWP_FACE(ewp) replaces the local FACEWK(); PC keeps the flat eventwk fields
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/ccl.h: `D_8C19C1C4`; sa1/engine.h: `D_8C19C32C`, `func_8C04BD9E`, `func_8C0922EE`.
* Note: pad6C stays the eventwk field name (NJS_POINT3 model offset).

### code_8C04BF00.c

Includes: `common.h`, `ninja.h`, `sa1/ninja_types.h`, `sa1/ninja_funcs.h`, `sa1/player.h`.

* Header workaround to drop: include sa1/engine.h (func_8C0583A0 fixed).
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/engine.h: `func_8C0583A0`, `func_8C058434`; sa1/motion.h: `B_8C7492B0`, `B_8C7492F0`, `B_8C749330`, `B_8C749370`; sa1/ninja_funcs.h: `D_8C67C640`.

### code_8C056860.c

Includes: `common.h`, `ninja.h`, `sa1/sa1.h`.

* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/engine.h: `B_8C788C38`, `B_8C788C3C`, `B_8C88F660`, `B_8C88F664`.
* Note: B_8C88F62C/B_8C788C40 stay local (Sint32 scalars here, Uint32 / Uint32[1] in code_8C05697C.c; each spelling is needed for its match).

### code_8C05697C.c

Includes: `common.h`, `ninja.h`.

* Note: B_8C88F62C/B_8C788C40 stay local (see code_8C056860.c).

### code_8C056A04.c

Includes: `common.h`, `ninja.h`, `task.h`, `sa1/ninja_funcs.h`, `sa1/engine.h`, `sa1/collision.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `forcewk` -> `forcewk` (sa1/pending.h)
* Header workaround to drop: #define func_8C029B00 func_8C029B00_hdr and the local prototype.
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/collision.h: `B_8C788C44`, `func_8C029B00`; sa1/ninja_funcs.h: `D_8C67C640`.

### code_8C056EC4.c

Includes: `common.h`, `ninja.h`, `sa1/ninja_types.h`, `stdarg.h`, `string.h`.

* Header workaround to drop: include sa1/engine.h (memset is the C library one on PC now).

### code_8C0572D8.c

Includes: `common.h`, `ninja.h`, `sa1/ninja_types.h`, `sa1/ninja_funcs.h`.

* Local types -> shared:
  * `NODE_WELD` -> `NODE_WELD` (sa1/motion.h): include sa1/motion.h
* Header workaround to drop: include sa1/engine.h and sa1/motion.h (func_8C0583A0/func_8C058434 fixed, NODE_WELD).
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/engine.h: `func_8C0583A0`, `func_8C058434`; sa1/ninja_funcs.h: `B_8C88F5F8`, `D_8C67C640`.

### code_8C057460.c

Includes: `common.h`, `ninja.h`, `sa1/sa1.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `NJS_SPHERE_LOCAL` -> `NJS_SPHERE` (sa1/ninja_types.h)
  * `OBJECT_DRAW_FUNC` -> `OBJECT_DRAW_FUNC` (sa1/pending.h)
* Header workaround to drop: #define func_8C63FE54 func_8C63FE54_header_proto (header: Sint32 func_8C63FE54(NJS_SPHERE *, NJS_SPHERE *)).
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/engine.h: `func_8C03700C`; sa1/ninja_funcs.h: `func_8C63FE54`*.

### code_8C057780.c

Includes: `common.h`, `ninja.h`, `sa1/ninja_types.h`, `sa1/ninja_funcs.h`.

* Local types -> shared:
  * `NUMDISP` -> `NUMDISP / NUMLAYOUT`: see the file note
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/engine.h: `D_8C19EF64`, `D_8C1BF19C`.
* Note: NUMDISP here (0x18, sprite digit layout of func_8C057780) is NUMLAYOUT in sa1/engine.h; NUMDISP is now the 0x1C request of func_8C08EE98.

### code_8C057B40.c

Includes: `common.h`, `ninja.h`, `sa1/ninja_funcs.h`.

* Header workaround to drop: include sa1/engine.h (func_8C057B40 now returns Sint32 there).
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/ninja_funcs.h: `B_8C88F5F8`, `B_8C88F698`, `B_8C88F69C`.

### code_8C057BC0.c

Includes: `common.h`, `ninja.h`, `sa1/ninja_funcs.h`, `sa1/engine.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `MOTION_MTX_CTX` -> `NODE_MTN_EVAL` (sa1/engine.h)
  * `NJS_MDATA2` -> `NJS_MDATA2` (sa1/pending.h): p[] is void * (SDK): cast to the key type
  * `NJS_MDATA3` -> `NJS_MDATA3` (sa1/pending.h)
  * `NJS_MKEY_A` -> `NJS_MKEY_A` (sa1/pending.h)
  * `NJS_MKEY_F` -> `NJS_MKEY_F` (sa1/pending.h)
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/engine.h: `B_8C78B448`, `B_8C78B44C`.

### code_8C058100.c

Includes: `common.h`, `ninja.h`, `task.h`, `sa1/ninja_types.h`, `sa1/ninja_funcs.h`, `sa1/motion.h`, `sa1/ccl.h`, `sa1/player.h`.

* Local types -> shared:
  * `NODE_MTN_EVAL` -> `NODE_MTN_EVAL` (sa1/engine.h): include sa1/engine.h (its func_8C0583A0/func_8C058434 prototypes are now right)
* Header workaround to drop: include sa1/engine.h (func_8C0583A0/func_8C058434 fixed; NODE_MTN_EVAL is there).
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/engine.h: `B_8C789448`, `B_8C78B448`, `B_8C78B44C`, `D_8C19F0A0`.

### code_8C06C300.c

Includes: `common.h`, `ninja.h`, `sa1/sa1.h`.

* Header workaround to drop: #define func_8C06C5CA func_8C06C5CA_header_proto (header: void func_8C06C5CA(Sint32 pno)).
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/input.h: `B_8C754BF8`, `D_8C19E3A4`; sa1/player.h: `func_8C06C5CA`, `func_8C07D67C`, `func_8C07D6C0`, `func_8C0B3A2E`.

### code_8C06C6A0.c

Includes: `sa1/sa1.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `DROWNWK` -> `DROWNWK` (sa1/pending.h): pno is Uint8
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/player.h: `B_8C78C688`, `B_8C78C690`, `B_8C792690`, `D_8C1BDFE0`.
* Note: DROWNWK.pno was Sint8 here; the shared type has Uint8 (EXTU.B in 8C07BE6E): re-verify.

### code_8C06CCA0.c

Includes: `sa1/sa1.h`.

* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/ccl.h: `func_8C0344DC`; sa1/player.h: `func_8C05E6E8`.
* Note: Defines func_8C06CEBE/func_8C06CF60 with (taskwk *) and func_8C06CFCC is defined (taskwk *) in code_8C06CFCC.c, but callers pass mwp (or pwp) in R5: add the unused second parameter to the definitions, then the prototypes can go into sa1/player.h.

### code_8C06CFCC.c

Includes: `sa1/sa1.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `PL_TARGET` -> `PL_TARGET` (sa1/pending.h)
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/ccl.h: `func_8C0344DC`; sa1/pending.h: `B_8C73EE38`, `B_8C742288`.
* Note: See code_8C06CCA0.c: func_8C06CFCC needs an unused motionwk * parameter (callers 8C06F386, 8C0B1C78).

### code_8C06F530.c

Includes: `common.h`, `ninja.h`, `sa1/sa1.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `PATHINFO` -> `PATHINFO` (sa1/pending.h)
  * `PATHTAG` -> `PATHTAG` (sa1/pending.h): drop the incomplete struct pathtag
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/pending.h: `func_8C0897F8`.

### code_8C072B44.c

Includes: `sa1/sa1.h`.

* Header workaround to drop: #define func_8C0730D8 func_8C0730D8_void_decl (header: Sint32).
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/ccl.h: `func_8C0344DC`; sa1/player.h: `func_8C06C880`, `func_8C0730D8`.

### code_8C0730D8.c

Includes: `sa1/sa1.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `TOUCHINFO` -> `TOUCHINFO` (sa1/pending.h)
* Header workaround to drop: #define func_8C029B00 ..._hdr, #define func_8C0730D8 ..._hdr and both local prototypes (header: Sint32 func_8C0730D8).
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/collision.h: `D_8C1BE84C`, `func_8C029B00`; sa1/pending.h: `B_8C6BACF0`; sa1/player.h: `D_8C1BDFE0`, `func_8C0742D4`, `func_8C0747D2`, `func_8C074CF0`, `func_8C074EE4`, `func_8C07506E`, `func_8C0751C0`, `func_8C075214`, `func_8C075416`, `func_8C075860`, `func_8C075A40`, `func_8C0777E0`, `func_8C079000`.

### code_8C0742D4.c

Includes: `sa1/sa1.h`.

* Header workaround to drop: #define func_8C029B00 func_8C029B00_hdr and the local prototype.
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/collision.h: `D_8C1BE84C`, `D_8C1BE850`, `D_8C1BE854`, `Debug_Sphere`, `func_8C029B00`, `tcrf_CollisionDisplayVar`; sa1/engine.h: `func_8C043A20`; sa1/player.h: `func_8C074CF0`, `func_8C079000`.

### code_8C074CF0.c

Includes: `sa1/sa1.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `forcewk` -> `forcewk` (sa1/pending.h)

### code_8C0751C0.c

Includes: `sa1/sa1.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `TOUCHINFO` -> `TOUCHINFO` (sa1/pending.h)
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/pending.h: `B_8C6BACF0`.

### code_8C075686.c

Includes: `sa1/sa1.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `TOUCHINFO` -> `TOUCHINFO` (sa1/pending.h)
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/pending.h: `B_8C6BACF0`.

### code_8C075860.c

Includes: `sa1/sa1.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `TOUCHINFO` -> `TOUCHINFO` (sa1/pending.h)
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/pending.h: `B_8C6BACF0`.

### code_8C075A40.c

Includes: `sa1/sa1.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `TOUCHINFO` -> `TOUCHINFO` (sa1/pending.h)
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/pending.h: `B_8C6BACF0`.

### code_8C075F8C.c

Includes: `sa1/sa1.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `TOUCHINFO` -> `TOUCHINFO` (sa1/pending.h)
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/pending.h: `B_8C6BACF0`.
* Note: func_8C075F8C is called with a 5th stack argument (attr, unused; 8C077D48): add it like func_8C075A40, then drop LAND_8C075F8C in code_8C0777E0.c.

### code_8C076782.c

Includes: `sa1/sa1.h`, `1st_read/code_8C076640.c`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `TOUCHHIT` -> `TOUCHINFO` (sa1/pending.h): the game copies only the first 0x50 bytes (__quick_mvn 80): keep a 0x50 copy (memcpy of offsetof(TOUCHINFO, dist)) and re-verify
  * `TOUCHINFO` -> `TOUCHINFO` (sa1/pending.h)
  * `forcewk` -> `forcewk` (sa1/pending.h)
* Header workaround to drop: #define func_8C029B00 func_8C029B00_hdr and the local prototype.
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/ccl.h: `func_8C0344DC`; sa1/collision.h: `func_8C029B00`; sa1/pending.h: `B_8C6BACF0`.

### code_8C0771C4.c

Includes: `sa1/sa1.h`.

* Header workaround to drop: #define func_8C029B00 func_8C029B00_hdr and the local prototype.
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/ccl.h: `func_8C0344DC`; sa1/collision.h: `func_8C029B00`; sa1/player.h: `func_8C07506E`.

### code_8C0777E0.c

Includes: `sa1/sa1.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `TOUCHINFO` -> `TOUCHINFO` (sa1/pending.h)
* Header workaround to drop: #define func_8C029B00 func_8C029B00_hdr and the local prototype.
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/collision.h: `func_8C029B00`; sa1/pending.h: `B_8C6BACF0`; sa1/player.h: `func_8C074AFC`, `func_8C074CF0`, `func_8C074EE4`, `func_8C07506E`, `func_8C075214`, `func_8C075686`, `func_8C075860`.
* Note: Drop LAND_8C075F8C once func_8C075F8C takes 5 arguments.

### code_8C078308.c

Includes: `common.h`, `ninja.h`, `sa1/sa1.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `PATHINFO` -> `PATHINFO` (sa1/pending.h)
  * `PATHTAG` -> `PATHTAG` (sa1/pending.h): drop the incomplete struct pathtag
  * `pathtag` -> `PATHTAG` (sa1/pending.h): prefix view
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/pending.h: `func_8C0897F8`.

### code_8C078856.c

Includes: `sa1/sa1.h`.

* Header workaround to drop: #define func_8C029B00 func_8C029B00_hdr and the local prototype.
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/ccl.h: `func_8C0344DC`; sa1/collision.h: `func_8C029B00`; sa1/player.h: `func_8C07506E`.

### code_8C079476.c

Includes: `sa1/sa1.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `MTN_MDATA3` -> `NJS_MDATA3` (sa1/pending.h): size-only view

### code_8C079C20.c

Includes: `sa1/sa1.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `NJS_MDATA2` -> `NJS_MDATA2` (sa1/pending.h): p[] is void * (SDK): cast to the key type
  * `NJS_MDATA3` -> `NJS_MDATA3` (sa1/pending.h)
  * `NJS_MKEY_A` -> `NJS_MKEY_A` (sa1/pending.h)
  * `NJS_MKEY_F` -> `NJS_MKEY_F` (sa1/pending.h)
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/engine.h: `func_8C055D70`, `func_8C0567B0`.

### code_8C07A49C.c

Includes: `sa1/sa1.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `NJS_MDATA2` -> `NJS_MDATA2` (sa1/pending.h): p[] is void * (SDK): cast to the key type
  * `NJS_MDATA3` -> `NJS_MDATA3` (sa1/pending.h)

### code_8C07A55C.c

Includes: `common.h`, `ninja.h`, `task.h`, `sa1/ninja_types.h`, `sa1/ninja_funcs.h`, `sa1/motion.h`, `sa1/ccl.h`, `sa1/player.h`.

* Header workaround to drop: local func_8C0583A0/func_8C058434 prototypes are the header ones now.
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/engine.h: `MAlloc`, `MFree`, `func_8C056712`, `func_8C0583A0`, `func_8C058434`; sa1/ninja_funcs.h: `D_8C67C640`.

### code_8C07AEF8.c

Includes: `common.h`, `ninja.h`, `sa1/engine.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `lookwk` -> `lookwk` (sa1/pending.h)

### code_8C07B080.c

Includes: `common.h`, `ninja.h`, `task.h`, `sa1/engine.h`, `sa1/player.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `NJS_MDATA2` -> `NJS_MDATA2` (sa1/pending.h): p[] is void * (SDK): cast to the key type
  * `NJS_MKEY_P` -> `NJS_MKEY_P` (sa1/pending.h)
  * `PL_FACE` -> `PL_FACE` (sa1/pending.h)
  * `PL_FACE_STEP` -> `PL_FACE_STEP` (sa1/pending.h)
  * `facewk` -> `facewk` (sa1/pending.h): SHC: EWP_FACE(ewp) replaces the local FACEWK(); PC keeps the flat eventwk fields
  * `pl_face` -> `PL_FACE` (sa1/pending.h): struct pl_face completion: use PFP(pwp)
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/engine.h: `func_8C04BCC8`, `func_8C055EFE`.
* Note: Also delete its local NJS_MKEY_P/NJS_MDATA2 (NJS_MDATA2.p is void * in the SDK type: cast to NJS_MKEY_P *).

### code_8C07B2B8.c

Includes: `common.h`, `ninja.h`, `task.h`, `sa1/sa1.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `PL_FACE` -> `PL_FACE` (sa1/pending.h)
  * `pl_face` -> `PL_FACE` (sa1/pending.h): struct pl_face completion: use PFP(pwp)
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/engine.h: `D_8C19C32C`.
* Note: Its struct pl_face prefix has Uint8 step and const char *seq; the asm (8C07B19A, 8C07B1F8: MOV.B without EXTU.B) makes them Sint8 and PL_FACE_STEP *: re-verify func_8C07B2B8/func_8C07B2DC.

### code_8C07BAC0.c

Includes: `common.h`, `ninja.h`, `task.h`, `sa1/sa1.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `DEATHZONE` -> `DEATHZONE` (sa1/pending.h)
  * `DROWNWK` -> `DROWNWK` (sa1/pending.h): pno is Uint8
  * `NUMDISP` -> `NUMDISP / NUMLAYOUT`: see the file note
  * `xssunit` -> `xssunit` (sa1/pending.h)
  * `zxsdwstr` -> `zxsdwstr` (sa1/pending.h)
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/collision.h: `tcrf_CollisionDisplayVar`; sa1/engine.h: `D_8C19B468`, `func_8C098E98`; sa1/pending.h: `DeathZoneTable`, `func_8C08EE98`; sa1/player.h: `SetInputP`.
* Note: NUMDISP here is the shared NUMDISP (func_8C08EE98 request, sa1/pending.h). func_8C02CA00 can only be typed in the header once this file uses the shared zxsdwstr.

### code_8C07BE68.c

Includes: `sa1/sa1.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `DROWNWK` -> `DROWNWK` (sa1/pending.h): pno is Uint8
  * `PATHTAG` -> `PATHTAG` (sa1/pending.h): drop the incomplete struct pathtag
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/engine.h: `D_8C19B468`; sa1/pending.h: `func_8C089A40`, `func_8C089AA0`; sa1/player.h: `func_8C107C80`.

### code_8C07C298.c

Includes: `common.h`, `ninja.h`, `task.h`, `sa1/sa1.h`.

* Local types -> shared:
  * `EVWK_LOOK` -> `eventwk` (sa1/engine.h): look_mode/look_ang/look_pos/look_tp are eventwk fields now (no pending name needed)

### code_8C07C3C0.c

Includes: `common.h`, `ninja.h`, `task.h`, `sa1/sa1.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `PL_LOOKTIMEWK` -> `PL_LOOKTIMEWK` (sa1/pending.h)

### code_8C07CE40.c

Includes: `sa1/sa1.h`.

* Header workaround to drop: #define func_8C07CEF2 func_8C07CEF2_header_decl (header: Sint32 func_8C07CEF2(Uint8, Sint32, NJS_POINT3 *, Angle3 *)).
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/player.h: `B_8C78C688`, `B_8C78C690`, `B_8C792690`.

### code_8C07D060.c

Includes: `common.h`, `task.h`, `sa1/sa1.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `PL_SPDUPWK` -> `PL_SPDUPWK` (sa1/pending.h): has the Uint16 timer at 0x02
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/player.h: `B_8C798798`, `B_8C7987B8`.

### code_8C07D2A2.c

Includes: `common.h`, `task.h`, `sa1/sa1.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `PL_SPDUPWK` -> `PL_SPDUPWK` (sa1/pending.h): has the Uint16 timer at 0x02
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/player.h: `func_8C107C80`.

### code_8C07D3A0.c

Includes: `common.h`, `task.h`, `sa1/sa1.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `PL_SPDUPWK` -> `PL_SPDUPWK` (sa1/pending.h): has the Uint16 timer at 0x02
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/player.h: `func_8C07D2DA`, `func_8C107C80`.
* Note: PL_SPDUPWK here lacks the timer at 0x02 (layout otherwise identical). (Sint16)pwp->item casts and *(Sint16 *)&pwp->item are redundant now that item is Sint16.

### code_8C0817E0.c

Includes: `common.h`, `ninja.h`, `task.h`.

* Local types -> shared:
  * `UNK_SPRITE` -> `NJS_SPRITE` (sa1/ninja_types.h): tlist/tanim typed (were void *)
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/engine.h: `B_8C754EC8`, `GetStageNumber`; sa1/ninja_funcs.h: `njColorBlendingMode`*, `njDrawSprite2D`*, `njSetTexture`*.

### code_8C081D00.c

Includes: `common.h`, `ninja.h`, `task.h`.

* Local types -> shared:
  * `UNK_ccl` -> `CCL_INFO` (sa1/ccl.h): flag4 -> flag
  * `UNK_cwk` -> `colliwk` (sa1/ccl.h): view
  * `UNK_mwk` -> `motionwk` (task.h): unk0 -> work, spd
  * `UNK_per` -> `PDS_PERIPHERAL` (sa1/input.h): prefix view
  * `UNK_pwk` -> `playerwk` (sa1/player.h): view; see the file note
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/engine.h: `func_8C049A4E`; sa1/input.h: `D_8C19E390`, `func_8C0509B8`, `func_8C0509DA`; sa1/player.h: `B_8C78C608`, `B_8C78C628`*, `SetInputP`, `func_8C07CEF2`*, `func_8C07CFA0`, `func_8C07CFDE`*.
* Note: UNK_pwk (spd at 0x38) is playerwk; func_8C07CFDE then returns playerwk *. func_8C07CEF2 is declared with the old (Sint32, Sint32, NJS_POINT3 *, Sint32) signature: switch to the header (pass NULL as ang).

### code_8C082160.c

Includes: `common.h`, `ninja.h`, `task.h`.

* Local types -> shared:
  * `UNK_8C082160_cs` -> `playerwk` (sa1/player.h): unk38 -> spd.x, unkC0/unkC4 -> p.jog_speed/p.run_speed
  * `UNK_8C082160_pwp` -> `motionwk` (task.h): cs -> work.pwp, spd
  * `UNK_8C754C3C_t` -> `PDS_PERIPHERAL` (sa1/input.h): prefix view (B_8C754C3C = &B_8C754C08[1])
  * `UNK_8C754DD4_t` -> `PL_ANALOG` (sa1/input.h): stroke
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/input.h: `D_8C19E38C`, `D_8C19E390`; sa1/ninja_funcs.h: `njCalcPoint`*, `njUnitVector`*; sa1/player.h: `B_8C78C608`, `B_8C78C628`*; sa1/shc_intrinsics.h: `_builtin_fsca`, `_builtin_sqrtf`.

### code_8C08273C.c

Includes: `common.h`, `ninja.h`, `task.h`.

* Local types -> shared:
  * `colliwk` -> `colliwk` (sa1/ccl.h): prefix view
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/engine.h: `GetPlayerNumber`, `GetStageNumber`, `func_8C049A4E`, `func_8C09068E`.

### code_8C082D60.c

Includes: `common.h`, `task.h`.

* Local types -> shared:
  * `UNK_8C082D60_work` -> `PL_AIWK` (sa1/player.h): view
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/ccl.h: `CCL_Init`*; sa1/engine.h: `B_8C754EC8`, `CAlloc`, `CreateElementalTask`, `DestroyTask`, `FreeTask`*.

### code_8C082FD4.c

Includes: `common.h`, `ninja.h`, `task.h`.

* Local types -> shared:
  * `UNK_8C082FD4_4C` -> `motionwk` (task.h): wk -> work.pwp, pos -> spd
  * `UNK_8C082FD4_50` -> `playerwk` (sa1/player.h): flag (0x04) -> equipment, unk38 -> spd.x
  * `UNK_8C082FD4_AWP` -> `PL_AIWK` (sa1/player.h): tp1/twp1/pw1/wk1 -> tp/twp/mwp/pwp, tp2.. -> tp2/twp2/mwp2/pwp2, spd -> stroke
  * `UNK_8C754DD4` -> `PL_ANALOG` (sa1/input.h): spd -> stroke
  * `UNK_CWP` -> `colliwk` (sa1/ccl.h): prefix view (flag is Uint16 in the header)
  * `UNK_PER` -> `PDS_PERIPHERAL` (sa1/input.h): prefix view
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/engine.h: `FreeTask`, `func_8C049A4E`, `func_8C098E98`; sa1/input.h: `D_8C19E38C`*, `D_8C19E390`*, `func_8C0509DA`*.

### code_8C083E5C.c

Includes: `common.h`, `ninja.h`, `task.h`.

* Local types -> shared:
  * `UNK_8C083EF8_t` -> `PL_AIWK` (sa1/player.h): view
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/engine.h: `CreateElementalTask`, `FreeTask`, `GetStageNumber`, `func_8C098E98`; sa1/player.h: `B_8C78C608`, `func_8C07CB54`*, `func_8C07CEF2`*, `func_8C07CFA0`*.
* Note: func_8C07CEF2/func_8C07C580/func_8C07C676/func_8C07CFA0 local prototypes differ from sa1/player.h (Sint32/Sint8 pno, old SetPlayerPosition): switch to the header ones.

### code_8C084200.c

Includes: `common.h`.

* Local types -> shared:
  * `UNK_POINT3` -> `NJS_POINT3` (ninja.h)
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/ninja_funcs.h: `njSetTexture`*.

### code_8C084260.c

Includes: `common.h`, `task.h`.

* Local types -> shared:
  * `UNK_8C084260_work` -> `PL_AIWK` (sa1/player.h): view
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/engine.h: `CAlloc`, `CreateElementalTask`.

### code_8C084324.c

Includes: `common.h`, `ninja.h`, `task.h`.

* Local types -> shared:
  * `UNK_CWK` -> `colliwk` (sa1/ccl.h): prefix view
  * `UNK_MWK` -> `motionwk` (task.h): pwp -> work.pwp, v -> spd
  * `UNK_PERIPHERAL` -> `PDS_PERIPHERAL` (sa1/input.h): prefix view
  * `UNK_PWK` -> `playerwk` (sa1/player.h): spd at 0x38
  * `UNK_STICK` -> `PL_ANALOG` (sa1/input.h): mag -> stroke
  * `UNK_WORK` -> `PL_AIWK` (sa1/player.h): tp44.. -> tp/twp/mwp/pwp, tp54.. -> tp2.., ang24 -> ang, mag30 -> stroke, pos90 -> pos
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/engine.h: `FreeTask`, `func_8C049A4E`, `func_8C098E98`; sa1/input.h: `D_8C19E38C`, `D_8C19E390`, `func_8C0509DA`*; sa1/player.h: `SetInputP`*.

### code_8C084AE0.c

Includes: `common.h`, `task.h`.

* Local types -> shared:
  * `UNK_8C084AE0_work` -> `PL_AIWK` (sa1/player.h): view
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/engine.h: `CAlloc`, `CreateElementalTask`.

### code_8C084BA0.c

Includes: `common.h`, `ninja.h`, `task.h`.

* Local types -> shared:
  * `UNK_AIWK` -> `PL_AIWK` (sa1/player.h): idx -> btimer, unk6 -> count, ang24 -> ang, unk28 -> dist, ang2C -> ang2, dist[] -> range[]
  * `UNK_CWP` -> `colliwk` (sa1/ccl.h): prefix view (flag is Uint16 in the header)
  * `UNK_INPUT` -> `PL_ANALOG` (sa1/input.h): angle -> ang
  * `UNK_MWP` -> `motionwk` (task.h): pwp -> work.pwp
  * `UNK_PER` -> `PDS_PERIPHERAL` (sa1/input.h): prefix view
  * `UNK_PWP` -> `playerwk` (sa1/player.h): flag (0x04) -> equipment, spd, unk7C -> free.ptr
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/engine.h: `FreeTask`, `func_8C049A4E`, `func_8C098E98`; sa1/input.h: `D_8C19E38C`*, `D_8C19E390`*, `func_8C0509DA`*; sa1/player.h: `SetInputP`*.

### code_8C085532.c

Includes: `common.h`, `ninja.h`, `task.h`.

* Local types -> shared:
  * `UNK_8C085532_awp` -> `PL_AIWK` (sa1/player.h): unk58/unk5C/unk60 -> twp2/mwp2/pwp2
  * `UNK_8C085532_scl` -> `playerwk` (sa1/player.h): unk38 -> spd.x
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/ccl.h: `CCL_Init`*; sa1/engine.h: `CreateChildTask`.

### code_8C085660.c

Includes: `common.h`, `ninja.h`, `task.h`.

* Local types -> shared:
  * `UNK_8C085660_awp` -> `PL_AIWK` (sa1/player.h): unk0C -> flag, unk48 -> twp, unk54/unk58/unk60 -> tp2/twp2/pwp2
  * `UNK_8C085660_cwp` -> `colliwk` (sa1/ccl.h): hit -> hit_cwp
  * `UNK_8C085660_hit` -> `colliwk` (sa1/ccl.h): tp -> mytask
  * `UNK_8C085660_scl` -> `playerwk` (sa1/player.h): unk110 -> p.height
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/ccl.h: `func_8C02F63C`; sa1/engine.h: `FreeTask`.

### code_8C0869E0.c

Includes: `common.h`, `ninja.h`, `task.h`.

* Local types -> shared:
  * `NJS_SPRITE` -> `NJS_SPRITE` (sa1/ninja_types.h): tanim is NJS_TEXANIM *
  * `UNK_pwk` -> `playerwk` (sa1/player.h): view; see the file note
  * `colliwk` -> `colliwk` (sa1/ccl.h): prefix view
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/ccl.h: `CCL_Init`*, `func_8C02F63C`; sa1/engine.h: `B_8C754EC8`, `CreateElementalTask`, `D_8C18B7EC`, `D_8C19B468`, `FreeTask`, `GetPlayerNumber`, `func_8C02CF68`, `func_8C03700C`*, `func_8C038D00`, `func_8C043A20`, `func_8C04FE40`, `func_8C056926`, `func_8C056938`, `func_8C0919CC`, `func_8C091C20`, `func_8C0940DA`, `rand`; sa1/ninja_funcs.h: `njColorBlendingMode`*, `njDrawSprite3D`*, `njPopMatrix`*, `njPushMatrix`*, `njRotateX`*, `njRotateY`*, `njScale`*, `njSetTexture`*, `njTranslate`*; sa1/player.h: `func_8C07CFA0`*, `func_8C07CFDE`*.
* Note: UNK_pwk.equipment at 0x06 is playerwk.item (Sint16 now, as here).

### code_8C0870EA.c

Includes: `common.h`, `ninja.h`, `task.h`.

* Local types -> shared:
  * `NJS_SPRITE` -> `NJS_SPRITE` (sa1/ninja_types.h): tanim is NJS_TEXANIM *
  * `UNK_pwk` -> `playerwk` (sa1/player.h): view; see the file note
  * `colliwk` -> `colliwk` (sa1/ccl.h): prefix view
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/ccl.h: `CCL_Init`*, `func_8C02F63C`; sa1/engine.h: `DestroyTask`, `atan2f`, `func_8C08A71E`, `func_8C091C20`*, `func_8C0940DA`, `rand`; sa1/ninja_funcs.h: `D_8C67C640`, `njCalcPoint`*, `njCalcVector`*, `njCos`, `njPopMatrix`*, `njPushMatrix`*, `njRotateX`*, `njRotateZ`*, `njSin`; sa1/player.h: `func_8C07CFDE`*.
* Note: UNK_pwk.equipment at 0x06 is playerwk.item (Sint16 now; the local copy is Uint16: re-verify the & 0x4000 test).

### code_8C0876F4.c

Includes: `common.h`, `ninja.h`, `task.h`.

* Local types -> shared:
  * `UNK_8C07CFDE_t` -> `playerwk` (sa1/player.h): spd at 0x38
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/ccl.h: `func_8C02F63C`; sa1/engine.h: `CreateElementalTask`, `func_8C036F12`*, `func_8C03700C`*, `func_8C04FE40`, `func_8C056926`, `func_8C056938`, `rand`; sa1/ninja_funcs.h: `D_8C67C640`, `njCalcPoint`*, `njCalcVector`*, `njDistanceP2P`*, `njPopMatrix`*, `njPushMatrix`*, `njRotateX`*, `njRotateY`*, `njRotateZ`*, `njScale`*, `njScalor`*, `njTranslate`*; sa1/player.h: `func_8C07CFA0`*, `func_8C07CFDE`*; sa1/shc_intrinsics.h: `_builtin_fabsf`.
* Note: func_8C057586 is declared (a, b, c, Float t) here but defined (from, to, Float dist, out) in code_8C057460.c (the float must come third for matching): not declared in a header until this call is rewritten.

### code_8C08966E.c

Includes: `common.h`, `ninja.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `PATHINFO` -> `PATHINFO` (sa1/pending.h)
  * `PATHTAG` -> `PATHTAG` (sa1/pending.h): drop the incomplete struct pathtag
  * `PATHTBL` -> `PATHTBL` (sa1/pending.h)
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/engine.h: `SubAngle`, `func_8C056784`; sa1/ninja_funcs.h: `njCos`, `njSin`, `njUnitVector`*.

### code_8C089AA0.c

Includes: `common.h`, `ninja.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `PATH_TAG` -> `PATHTAG` (sa1/pending.h): flag -> slattr, tbl -> tblhead
  * `PATH_TBL` -> `PATHTBL` (sa1/pending.h): len -> length, pos -> xpos/ypos/zpos
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/engine.h: `atan2f`, `func_8C056784`, `func_8C10D058`; sa1/ninja_funcs.h: `njDistanceP2P`*, `njOuterProduct`*, `njUnitVector`*.

### code_8C089F20.c

Includes: `common.h`, `ninja.h`, `task.h`.

* Local types -> shared:
  * `UNK_SPRITE` -> `NJS_SPRITE` (sa1/ninja_types.h): tlist/tanim typed (were void *)
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/engine.h: `CreateElementalTask`, `FreeTask`, `func_8C0940DA`; sa1/ninja_funcs.h: `njDrawSprite2D`*.

### code_8C08A198.c

Includes: `common.h`, `ninja.h`, `task.h`.

* Local types -> shared:
  * `UNK_SPRITE` -> `NJS_SPRITE` (sa1/ninja_types.h): tlist/tanim typed (were void *)
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/engine.h: `CreateElementalTask`, `FreeTask`, `GetPlayerNumber`, `GetStageNumber`, `func_8C043A20`, `func_8C04FE40`; sa1/ninja_funcs.h: `njColorBlendingMode`*, `njDrawSprite2D`*, `njSin`; sa1/player.h: `B_8C78C608`.

### code_8C08A71E.c

Includes: `common.h`.

* Local types -> shared:
  * `UNK_POINT3` -> `NJS_POINT3` (ninja.h)
  * `UNK_SPRITE` -> `NJS_SPRITE` (sa1/ninja_types.h): tlist/tanim typed (were void *)
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/engine.h: `B_8C752BE8`, `GetPlayerNumber`, `GetStageNumber`, `func_8C043A20`; sa1/ninja_funcs.h: `njColorBlendingMode`*, `njDrawSprite2D`*, `njSetTexture`*, `njSin`; sa1/shc_intrinsics.h: `_builtin_fabsf`.

### code_8C08ABC0.c

Includes: `common.h`, `ninja.h`, `task.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `NJS_ARGB` -> `NJS_ARGB` (sa1/ninja_types.h)
  * `UNK_NUMDISP` -> `NUMDISP` (sa1/pending.h)
  * `UNK_SPRITE` -> `NJS_SPRITE` (sa1/ninja_types.h): tlist/tanim typed (were void *)
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/engine.h: `B_8C754EC8`, `GetPlayerNumber`, `tcrf_LevelSelectVar`; sa1/ninja_funcs.h: `njColorBlendingMode`*, `njDrawSprite2D`*, `njSetConstantMaterial`.
* Note: UNK_NUMDISP -> NUMDISP; add #include "sa1/pending.h" to get func_8C08EE98 (the PC compile reports it as an implicit declaration today).

### code_8C08B284.c

Includes: `common.h`, `ninja.h`, `task.h`.

* Local types -> shared:
  * `NJS_ARGB` -> `NJS_ARGB` (sa1/ninja_types.h)
  * `NJS_SPRITE` -> `NJS_SPRITE` (sa1/ninja_types.h): tanim is NJS_TEXANIM *
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/engine.h: `GetPlayerNumber`, `GetStageNumber`, `func_8C0940DA`; sa1/ninja_funcs.h: `njColorBlendingMode`*, `njDrawSprite2D`*, `njSetConstantMaterial`.

### code_8C098440.c

Includes: `common.h`.

* Note: func_8C098440 is declared (task *tp) in sa1/engine.h (it is stored as a TaskFuncPtr); the definition here is (void): add the unused parameter.

### code_8C098444.c

Includes: `common.h`, `task.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `OBJ_CONDITION` -> `OBJ_CONDITION` (sa1/pending.h)
  * `anywk` -> `anywk` (sa1/pending.h)
  * `forcewk` -> `forcewk` (sa1/pending.h)
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/engine.h: `B_8C79F044`, `B_8C79F048`, `B_8C79F04C`, `CAlloc`, `D_8C1C1D40`, `DestroyTask`, `MAlloc`, `MFree`, `func_8C098440`, `func_8C098E60`.
* Note: Also: CreateElementalTask is defined with Uint16 im (the original), but sa1/engine.h keeps Sint32 because code_8C098B0C.c ASM_STUBs it with Sint32: change both together (unresolved, see above). TASK_SIZE stays (task is 0x30 on DC).

### code_8C0988E4.c

Includes: `common.h`, `task.h`, `sa1/engine.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `anywk` -> `anywk` (sa1/pending.h)
  * `forcewk` -> `forcewk` (sa1/pending.h)

### code_8C098998.c

Includes: `common.h`, `task.h`, `sa1/engine.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `MEMBLK` -> `MEMBLK` (sa1/pending.h): code_8C098998.c: end/last -> nextphys/prevphys
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/ccl.h: `func_8C0344A0`; sa1/engine.h: `B_8C79F040`, `B_8C7EE8E0`, `MFree`, `func_8C04B468`; sa1/pending.h: `B_8C79F03C`.

### code_8C098B0C.c

Includes: `common.h`, `task.h`, `sa1/ninja_types.h`, `sa1/ninja_funcs.h`, `sa1/engine.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `CHILDTASKINFO` -> `CHILDTASKINFO` (sa1/pending.h)
  * `MEMBLK` -> `MEMBLK` (sa1/pending.h): code_8C098998.c: end/last -> nextphys/prevphys
  * `memblk` -> `MEMBLK` (sa1/pending.h): struct tag
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/engine.h: `func_8C098E98`; sa1/ninja_funcs.h: `D_8C67C640`; sa1/pending.h: `B_8C79F03C`.

### code_8C0A1120.c

Includes: `common.h`.

* Local types -> shared:
  * `UNK_POINT3` -> `NJS_POINT3` (ninja.h)
  * `UNK_SPRITE` -> `NJS_SPRITE` (sa1/ninja_types.h): tlist/tanim typed (were void *)
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/engine.h: `GetStageNumber`, `tcrf_LevelSelectVar`; sa1/ninja_funcs.h: `njColorBlendingMode`*, `njDrawSprite2D`*, `njSetConstantMaterial`*.

### code_8C0A77A0.c

Includes: `common.h`.

* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/engine.h: `tcrf_LevelSelectVar`.
* Note: B_8C78B451 must stay an array (extern Uint8 B_8C78B451[]); code_8C08A198.c/code_8C08A71E.c declare it scalar: not in a header until both spellings are checked.

### code_8C0A966C.c

Includes: `common.h`, `ninja.h`, `sa1/sa1.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `sonic_forcewk` -> `forcewk` (sa1/pending.h): call_back typed (was void *)
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/engine.h: `D_8C18B7EC`, `func_8C0945A0`; sa1/input.h: `D_8C19E38C`, `D_8C19E390`, `D_8C19E3A0`; sa1/ninja_funcs.h: `D_8C67C640`; sa1/player.h: `func_8C05E6E8`, `func_8C05E788`, `func_8C0A9660`, `func_8C0AE3D6`, `func_8C0B1D60`.

### code_8C0AE0E0.c

Includes: `common.h`, `ninja.h`, `sa1/sa1.h`.

* Header workaround to drop: #define func_8C06C5CA func_8C06C5CA_header_proto.
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/engine.h: `func_8C038CEC`, `func_8C038D00`, `func_8C04B57C`, `func_8C04FE40`, `func_8C056926`, `func_8C056938`, `func_8C05694A`, `func_8C0945D0`, `func_8C098FFC`; sa1/player.h: `func_8C06C5CA`, `func_8C09247E`, `func_8C0B2170`, `func_8C0B4F00`, `texlist_SONIC`.

### code_8C0AEFA0.c

Includes: `sa1/sa1.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `PL_TARGET` -> `PL_TARGET` (sa1/pending.h)
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/engine.h: `D_8C18B7EC`; sa1/pending.h: `B_8C73EE38`, `B_8C742288`; sa1/player.h: `func_8C07D18E`, `func_8C0B2DB2`, `func_8C0B2DD8`.

### code_8C0AF5A0.c

Includes: `sa1/sa1.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `PL_TARGET` -> `PL_TARGET` (sa1/pending.h)
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/pending.h: `B_8C73EE38`, `B_8C742288`; sa1/player.h: `func_8C0714B6`, `func_8C07D18E`.

### code_8C0AFB10.c

Includes: `common.h`, `ninja.h`, `sa1/sa1.h`, `1st_read/code_8C0AF5A0.c`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `forcewk` -> `forcewk` (sa1/pending.h)
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/player.h: `func_8C05E788`, `func_8C06C880`, `func_8C07B2B8`, `func_8C07B320`, `func_8C07C298`, `func_8C0B1D60`, `func_8C109FDE`.

### code_8C0B07E4.c

Includes: `sa1/sa1.h`.

* Local types -> shared:
  * `SONIC_SUPER_AWP` -> `SONIC_SUPERWK` (sa1/player.h): prefix view
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/player.h: `func_8C07B320`, `func_8C0B1D60`, `func_8C0B32B6`, `func_8C0B60B8`.

### code_8C0B16BC.c

Includes: `common.h`, `ninja.h`, `sa1/sa1.h`, `1st_read/code_8C0B11C0.c`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `SONIC_TARGET` -> `PL_TARGET` (sa1/pending.h)
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/pending.h: `B_8C73EE38`*, `B_8C742288`*; sa1/player.h: `func_8C06C880`, `func_8C06CB40`, `func_8C06D118`, `func_8C07DC2A`, `func_8C0A9660`.

### code_8C0B2338.c

Includes: `sa1/sa1.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `FACEWK` -> `PL_FACE` (sa1/pending.h): index is Sint8 step, frame -> rate, step -> rate_spd, script -> seq, obj/def/face_obj -> parent/sibling/object
  * `LOOKWK` -> `lookwk` (sa1/pending.h)
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/ninja_funcs.h: `D_8C67C640`; sa1/player.h: `func_8C0B28EE`.

### code_8C0B2890.c

Includes: `common.h`, `ninja.h`, `sa1/sa1.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `evlookwk` -> `eventwk` (sa1/engine.h): look -> look_mode, look_y/look_z -> look_ang.y/.z, look_pos, look_tp
  * `lookwk` -> `lookwk` (sa1/pending.h)
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/engine.h: `D_8C18B7EC`; sa1/ninja_funcs.h: `D_8C67C640`; sa1/pending.h: `func_8C07AEF8`, `func_8C07AF8C`; sa1/player.h: `func_8C0B48B4`.

### code_8C0B2D40.c

Includes: `sa1/sa1.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `SONIC_8C0B2EA0_AWP` -> `SPDLIMITWK` (sa1/pending.h)
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/player.h: `func_8C0B3FA6`.

### code_8C0B2E30.c

Includes: `common.h`, `ninja.h`, `sa1/sa1.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `SPDLIMITWK` -> `SPDLIMITWK` (sa1/pending.h)

### code_8C0B2EA0.c

Includes: `common.h`, `sa1/sa1.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `SPDLIMITWK` -> `SPDLIMITWK` (sa1/pending.h)
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/engine.h: `func_8C038D00`, `func_8C08A71E`; sa1/player.h: `D_8C1C6318`, `D_8C1C6380`, `D_8C1C63A0`.

### code_8C0B3240.c

Includes: `common.h`, `task.h`, `sa1/player.h`, `sa1/engine.h`.

* Local types -> shared:
  * `PARAMWK_8C0B32B6` -> `SONIC_SUPERWK` (sa1/player.h)

### code_8C0B33A0.c

Includes: `common.h`, `task.h`, `sa1/ninja_types.h`, `sa1/ninja_funcs.h`, `sa1/engine.h`, `sa1/ccl.h`, `sa1/player.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `EMITPRM_8C0B3480` -> `BUBBLE_PARAM` (sa1/pending.h): f1C.. -> size_max..
  * `PTCL_8C0B33A0` -> `BUBBLE` (sa1/pending.h): f00 -> surface, w1C/w1E -> ang/angspd, f20/f24/f28 -> aspect/size/maxsize
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/ninja_funcs.h: `D_8C67C640`.

### code_8C0B37F2.c

Includes: `common.h`, `task.h`, `sa1/player.h`, `sa1/engine.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `BUBBLE` -> `BUBBLE` (sa1/pending.h)
  * `BUBBLEPARAM` -> `BUBBLE_PARAM` (sa1/pending.h): ofs_scl/vec_scl -> dist/spd, type -> attach, scl_max/min -> size_max/min, alpha_rand -> aspect_rand, f28/f2C -> damp/buoyancy, f34 -> rate, f3C.. -> grow/jitter_rate/jitter
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/engine.h: `func_8C042DC4`; sa1/pending.h: `D_8C1C63A4`*.

### code_8C0B3B1A.c

Includes: `sa1/sa1.h`.

* Local types -> shared (then `#include "sa1/pending.h"`):
  * `BUBBLE` -> `BUBBLE` (sa1/pending.h)
  * `BUBBLE_PARAM` -> `BUBBLE_PARAM` (sa1/pending.h): key -> chr, vec_scl/scl -> dist/spd, type -> attach, f1C/f20/f24 -> size_max/size_min/aspect_rand, f30 -> spd_rand, f38 -> ang_rand
  * `bubble` -> `BUBBLE` (sa1/pending.h): struct tag
* Local declarations the headers now provide (delete; `*` = the local signature differs from the header, adopt the header one): sa1/engine.h: `func_8C042DC4`; sa1/player.h: `D_8C1C3068`, `D_8C1C67DC`, `func_8C0B4040`, `func_8C0B41E0`, `func_8C0B4332`.


## 7. Files whose only change is deleting local declarations

These files have no local copies of shared types. Their local prototypes and externs are now in headers; `*` marks a local signature that differs from the header.

| File | Declarations now in headers |
|---|---|
| code_8C010340.c | sa1/ninja_funcs.h: `B_8C88F5F8` |
| code_8C010400.c | sa1/engine.h: `D_8C18B7EC`, `D_8C19B468`; sa1/player.h: `func_8C07CFA0`* |
| code_8C010B00.c | sa1/engine.h: `B_8C754EC8`, `memset`*; sa1/input.h: `B_8C754DA8`* |
| code_8C011EA6.c | sa1/engine.h: `GetStageNumber`; sa1/ninja_funcs.h: `njCalcVector`*, `njPopMatrix`*, `njPushMatrix`*, `njRotateY`*, `njRotateZ`*, `njUnitMatrix`* |
| code_8C012520.c | sa1/engine.h: `GetStageNumber`; sa1/ninja_funcs.h: `func_8C640A80`*, `njPrintC`*; sa1/player.h: `B_8C78C608` |
| code_8C015720.c | sa1/engine.h: `GetStageNumber`*, `rand` |
| code_8C015820.c | sa1/engine.h: `CAlloc`, `CreateElementalTask`, `DestroyTask`, `rand` |
| code_8C01622A.c | sa1/engine.h: `atan2f`, `rand`; sa1/ninja_funcs.h: `njCalcVector`*, `njCos`, `njRotateY`*, `njSin`, `njUnitMatrix`*, `njUnitVector`*; sa1/player.h: `B_8C78C608` |
| code_8C0168A8.c | sa1/engine.h: `atan2f`; sa1/ninja_funcs.h: `njCos`, `njSin`, `njUnitVector`* |
| code_8C017A5C.c | sa1/engine.h: `CAlloc`, `rand` |
| code_8C040840.c | sa1/engine.h: `func_8C037098`; sa1/motion.h: `B_8C88FE24`, `B_8C88FE28`, `B_8C890074` |
| code_8C04196C.c | sa1/engine.h: `func_8C03700C`; sa1/motion.h: `B_8C88FE58`, `B_8C88FE80`, `B_8C88FE88`, `B_8C88FF38`, `B_8C88FF3C`, `B_8C88FF40`, `B_8C88FF42`, `B_8C88FF44`, `B_8C88FF46`, `B_8C88FF48`, `B_8C88FFFC`, `B_8C89000C`, `B_8C89002C`, `B_8C890074` |
| code_8C052A00.c | sa1/engine.h: `B_8C754BDC`*, `B_8C754BE8`, `B_8C754BEC`, `atan2f`, `func_8C10D058`; sa1/ninja_funcs.h: `njCos`, `njSin` |
| code_8C055D20.c | sa1/engine.h: `D_8C18B7EC` |
| code_8C055ECE.c | sa1/engine.h: `func_8C055EFE`, `func_8C05646C` |
| code_8C057A20.c | sa1/engine.h: `func_8C043A40` |
| code_8C06C0C0.c | sa1/engine.h: `B_8C78C5E0`, `B_8C78C600`, `B_8C78C604`, `D_8C03F760`; sa1/player.h: `func_8C06C1D8` |
| code_8C06C1D8.c | sa1/engine.h: `B_8C78C604`, `func_8C0108EA` |
| code_8C06C240.c | sa1/engine.h: `B_8C78C5E0`, `tcrf_SprTaskDisplayMode` |
| code_8C06CA30.c | sa1/ccl.h: `func_8C0344DC`; sa1/player.h: `D_8C1BDFE0` |
| code_8C06D254.c | sa1/player.h: `func_8C06E4E0`, `func_8C06EAC0`, `func_8C06F530`, `func_8C0718C0`, `func_8C071A20`, `func_8C071EC0`, `func_8C072000`, `func_8C0720A0` |
| code_8C06DCA2.c | sa1/engine.h: `D_8C18B7EC`; sa1/player.h: `func_8C06F530`, `func_8C0718C0`, `func_8C072000`, `func_8C0720A0` |
| code_8C06E4E0.c | sa1/player.h: `func_8C071EC0`, `func_8C072000`, `func_8C0720A0` |
| code_8C06E904.c | sa1/player.h: `func_8C06EFCE`, `func_8C071EC0` |
| code_8C06EAC0.c | sa1/player.h: `func_8C072000` |
| code_8C06EFCE.c | sa1/ccl.h: `func_8C0344DC`; sa1/player.h: `func_8C072000` |
| code_8C06F6C0.c | sa1/player.h: `func_8C071EC0` |
| code_8C06F840.c | sa1/player.h: `func_8C071EC0` |
| code_8C06FB8C.c | sa1/engine.h: `D_8C18B7EC`; sa1/player.h: `func_8C072000`, `func_8C0720A0` |
| code_8C06FFA0.c | sa1/player.h: `func_8C072000` |
| code_8C0700C2.c | sa1/engine.h: `D_8C18B7EC`; sa1/player.h: `func_8C072000` |
| code_8C07061C.c | sa1/player.h: `func_8C06EFCE`, `func_8C072000`, `func_8C0720A0` |
| code_8C07095E.c | sa1/engine.h: `D_8C18B7EC` |
| code_8C070EDA.c | sa1/player.h: `func_8C071EC0` |
| code_8C0714B6.c | sa1/engine.h: `D_8C18B7EC`; sa1/player.h: `func_8C06F530`, `func_8C0718C0`, `func_8C071EC0`, `func_8C072000`, `func_8C0720A0` |
| code_8C071B26.c | sa1/player.h: `func_8C0723E0`, `func_8C072510` |
| code_8C072174.c | sa1/player.h: `func_8C0723E0`, `func_8C072510`, `func_8C079000` |
| code_8C07266E.c | sa1/ccl.h: `func_8C0344DC`; sa1/player.h: `func_8C06C880`, `func_8C0723E0`, `func_8C07256A` |
| code_8C0747D2.c | sa1/player.h: `D_8C1BDFE0`, `func_8C0763A0`, `func_8C076640` |
| code_8C078E40.c | sa1/player.h: `func_8C078F1E`, `func_8C079000`, `func_8C079234` |
| code_8C07B500.c | sa1/engine.h: `D_8C19B468`, `func_8C094330` |
| code_8C07C120.c | sa1/pending.h: `func_8C089A40`* |
| code_8C07C9FA.c | sa1/player.h: `B_8C78C688`, `B_8C78C690` |
| code_8C07D560.c | sa1/player.h: `B_8C798690` |
| code_8C07D6C0.c | sa1/player.h: `func_8C0714B6` |
| code_8C081AC0.c | sa1/engine.h: `CreateElementalTask`, `FreeTask`, `GetStageNumber`; sa1/input.h: `func_8C0509B8`*, `func_8C0509DA`*; sa1/player.h: `SetInputP`*, `func_8C07CFA0`* |
| code_8C082A60.c | sa1/engine.h: `GetPlayerNumber`, `GetStageNumber`; sa1/ninja_funcs.h: `func_8C6088BC`*, `func_8C608C82`*, `func_8C63FAE8`, `njSetTexture`* |
| code_8C08593C.c | sa1/shc_intrinsics.h: `_builtin_fipr`, `_builtin_sqrtf` |
| code_8C08594C.c | sa1/shc_intrinsics.h: `_builtin_fipr`, `_builtin_sqrtf` |
| code_8C085960.c | sa1/shc_intrinsics.h: `_builtin_fipr` |
| code_8C085970.c | sa1/shc_intrinsics.h: `_builtin_fipr` |
| code_8C085980.c | sa1/shc_intrinsics.h: `_builtin_fipr`, `_builtin_sqrtf` |
| code_8C0859A0.c | sa1/shc_intrinsics.h: `_builtin_fipr`, `_builtin_sqrtf` |
| code_8C0859C0.c | sa1/shc_intrinsics.h: `_builtin_fipr`, `_builtin_sqrtf` |
| code_8C0859E4.c | sa1/shc_intrinsics.h: `_builtin_fipr`, `_builtin_sqrtf` |
| code_8C085A00.c | sa1/shc_intrinsics.h: `_builtin_fipr`, `_builtin_sqrtf` |
| code_8C085A1C.c | sa1/shc_intrinsics.h: `_builtin_fipr` |
| code_8C0867B0.c | sa1/shc_intrinsics.h: `_builtin_ftrv` |
| code_8C086F5E.c | sa1/engine.h: `CreateElementalTask`, `GetPlayerNumber`, `func_8C0940DA`, `rand`; sa1/player.h: `B_8C78C608` |
| code_8C087B80.c | sa1/engine.h: `CreateChildTask`, `DestroyTask`, `FreeTask`, `func_8C036F12`, `func_8C042DC4`, `func_8C0919CC`, `func_8C098E60`, `func_8C098E98`, `func_8C10D148`; sa1/ninja_funcs.h: `D_8C67C640`, `njAddVector`*, `njCalcVector`*, `njCos`, `njPopMatrix`*, `njPrintC`*, `njPushMatrix`*, `njRotateX`*, `njRotateY`*, `njRotateZ`*, `njSin`, `njTranslate`*, `njTranslateV`* |
| code_8C089184.c | sa1/engine.h: `CreateElementalTask`, `GetStageNumber`; sa1/ninja_funcs.h: `njDistanceP2P`* |
| code_8C08A4BA.c | sa1/engine.h: `B_8C752BE8`, `B_8C754EC4`, `GetPlayerNumber`, `func_8C0940DA` |
| code_8C09878C.c | sa1/engine.h: `B_8C79F04C`, `D_8C1C1D40` |
| code_8C0A77BC.c | sa1/engine.h: `D_8C19B468`, `func_8C04B9E0`, `func_8C094330`, `func_8C0945A0`, `func_8C098FAE`; sa1/player.h: `D_8C1C5F08`, `D_8C1C7D70`, `D_8C1C8CA4`, `D_8C1CAD20`, `SonicModeMachine`, `func_8C05E7D4`, `func_8C06C6F8`, `func_8C06C880`, `func_8C06D136`, `func_8C0700C2`, `func_8C0714B6`, `func_8C071656`, `func_8C071B26`, `func_8C071D40`, `func_8C071DD8`, `func_8C078856`, `func_8C0792DC`, `func_8C07B2B8`, `func_8C07B320`, `func_8C07D7B4`, `func_8C0925A0`, `func_8C0AE17A`, `func_8C0AE3D6`, `func_8C0AF6C4`, `func_8C0B1DE0`, `func_8C0B1FD8`, `func_8C0B2022`, `func_8C0B2120`, `func_8C0B2338`, `func_8C0B2D40`, `func_8C0B5B80` |
| code_8C0A9660.c | sa1/player.h: `func_8C0AE3D6` |
| code_8C0AE760.c | sa1/engine.h: `func_8C08A71E` |
| code_8C0AFF1A.c | sa1/player.h: `func_8C07B320`, `func_8C0B32B6`, `func_8C0B60B8` |
| code_8C0B0E94.c | sa1/player.h: `func_8C0A9660`, `func_8C0B1D60` |
| code_8C0B11C0.c | sa1/engine.h: `func_8C094330`, `func_8C0945A0`; sa1/player.h: `func_8C06C880`, `func_8C0A9660`, `func_8C0B1D60`, `func_8C0B2EA0` |
| code_8C0B1C56.c | sa1/ninja_funcs.h: `D_8C67C640`; sa1/player.h: `func_8C06C880`, `func_8C0A9660`, `func_8C0B1D60` |
| code_8C0B2170.c | sa1/engine.h: `func_8C055ECE`; sa1/player.h: `D_8C1C5EE8`, `D_8C1C5EF8`, `D_8C1C7504`, `D_8C1C8454` |
| code_8C0B4040.c | sa1/engine.h: `func_8C056926`, `func_8C056938`, `func_8C05694A`; sa1/ninja_funcs.h: `D_8C67C640`; sa1/player.h: `D_8C1C694C`, `D_8C1C6954`, `Sonic_Jumpball_Effect` |
| code_8C0B4600.c | sa1/engine.h: `func_8C056926`, `func_8C056938`, `func_8C05694A`; sa1/player.h: `Sonic_Jumpball_Effect` |
| code_8C0B48E0.c | sa1/ninja_funcs.h: `D_8C67C640` |
| code_8C0B4F00.c | sa1/ninja_funcs.h: `D_8C67C640` |
| code_8C0B58E0.c | sa1/engine.h: `D_8C18B7EC`; sa1/ninja_funcs.h: `D_8C67C640`; sa1/player.h: `D_8C1C6AD0` |
| code_8C0B5D60.c | sa1/player.h: `D_8C25658C`, `Sonic_Ice`, `Sonic_Ice_Particles` |
| code_8C0B6072.c | sa1/player.h: `D_8C25658C`, `Sonic_Ice` |
