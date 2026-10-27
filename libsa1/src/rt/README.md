# libsa1 runtime (`libsa1/src/rt`)

The clean-room reimplementation of everything the decompiled game code calls that is not game code: the
Ninja / Kamui / Shinobi SDK and the SHC C runtime (design: `SADC/docs/libsa1_integration.md` section 4, the
82 boundary functions of 4.2, the intrinsics of 4.3, the edge semantics of 4.5, the FP rules of 8).
The same C runs in two places:

| Profile | Where | Built by | XMTRX (`SA1_XMTRX`) | Memory |
|---|---|---|---|---|
| harness | inside the 32-bit SH-4 interpreter harness, next to the original SDK code (design profile `ga32`, `sa1h_rt`) | `SADC/harness/build.py --pc --rt` (`TARGET_PC_HARNESS`, set by `harness/src/harness_pre.h`) | the interpreter's XF bank `SH4.xf` (`sa1h_xmtrx`, `harness/src/sh4_intrin.c`) | guest address == host address |
| libsa1 | the library | `libsa1/build.py --lower <TUs> --with-rt` (lowered with the game TUs; blocked by pclower today, see below) | `sa1_rt_xmtrx`, pointed at the ticking world's `XMTRX[16]` by the world core (design 6.1) | the world's arena (`pclower`, design 2) |

## Rules

1. **Clean room.** Implement from behaviour. You may read the original SDK code
   (`py -3.12 tools/funcinfo.py <addr|name>` in `SADC/`) to learn the exact operation order, the FPU
   instructions used, rounding, register side effects and edge cases, and you must reproduce them bit-exactly,
   but write your own C; never transcribe SDK machine code.
2. **No game bytes.** No table, constant block or data copied from `1ST_READ.BIN` or any disc file into a
   source file. Data the SDK keeps in its own data/BSS (libm coefficients `D_8C1603A4`, the unit matrix
   `D_8C67C640`, the matrix stack `B_8C7EC8E0` ...) is used **in place** through `extern` declarations of its
   symbol, i.e. read from the user's image at run time. Only generated mathematics may be checked in
   (`rt_fsca_q.c`, produced by `SADC/harness/tools/dump_fsca.py`).
3. **Game dialect.** Include `"rt.h"` first. Use the game's types (`Sint32`, `Float`, `Angle`, `NJS_MATRIX`,
   ... from `SADC/include/common.h`, `ninja.h`, `sa1/ninja_types.h`). `rt.h` includes `sa1/ninja_funcs.h`, so
   a definition whose prototype differs from what the game calls does not compile. Name each function like its
   symbol in `SADC/build/1ST_READ.symbols.txt` (`njSin`, `njPushMatrix`) or `func_XXXXXXXX` when the SDK name
   is unknown: the harness registers it at that address from the name. Internal helpers that game code never
   calls are `static` or `func_XXXXXXXX` of the helper's own address (so they can be tested alone).
4. **DC layout and real state.** Structures keep their Dreamcast layout. State the SDK keeps in memory lives
   at the SDK's own addresses (declare `extern Sint32 B_8C88F69C;` etc. in the C file or in `ninja_funcs.h`),
   because game code reads it directly (design 2.9); no private shadow copies. Pointers stored in memory are
   32-bit guest values (`pclower` rewrites accesses for 64-bit hosts later).
5. **Exact operation order.** One C operation per SH-4 FPU instruction, in the original order, single
   precision. FIPR, FTRV, FSCA, FSRRA, FMAC, FSQRT, FTRC go through the `RT_*` helpers of `rt.h` (the
   interpreter's models in `SADC/include/pc/sh4_fpmodel.h`; never host libm, never another formula). FADD /
   FSUB / FMUL / FDIV are plain C operators, or `RT_FADD` ... where a NaN can come out (x86 NaN `FFC00000`
   vs SH-4 `7FBFFFFF`). FABS / FNEG are `RT_FABS` / `RT_FNEG` (bit operations). Float literals as decimal
   (the harness and `pclower` rewrite them to their round-toward-zero value, as SHC did) or hex floats.
   Integer division by a value that can be 0: `SA1_DIV` ... (`common.h`). No `-ffast-math` assumptions: the
   flags are `SADC/tools/pc_cflags.txt` (no contraction, `-frounding-math`).
6. **Registers.** XMTRX is `SA1_XMTRX[16]` (`rt_xmtrx_load` / `rt_xmtrx_store` copy it bit-exactly; float k is
   float k of the `NJS_MATRIX`, `RT_FTRV` computes `v * M`). Values the SDK leaves in other registers (njSin also
   leaves cos in FR1) are not part of the C interface: if an original caller depends on one, say so in the
   file and handle it (e.g. XMTRX contents after a call are part of the interface and must match).
7. **Edge semantics stay** (design 4.5): NaN from njUnitVector(0), the singular njInvertMatrix fill, push/pop
   limits, push(NULL), skipped zero angles, FTRC-like floor/ceil saturation.
8. **Portable C.** Builds for x86 and x86_64 (Windows, Linux); no host calls inside functions that run during a
   tick (no allocation, no I/O, no CRT maths).

## Helpers (`rt.h`)

| Helper | SH-4 | Model |
|---|---|---|
| `SA1_XMTRX` | XMTRX (XF0-XF15) | harness: `SH4.xf.f`; libsa1: world matrix |
| `rt_xmtrx_load(m)` / `rt_xmtrx_store(m)` | FMOV pair loads/stores of XMTRX | 16 floats, bit-exact |
| `RT_FIPR(va, vb)` | FIPR | 4 products summed in double, rounded once |
| `RT_FTRV(v, out)`, `RT_FTRV_M(m, v, out)` | FTRV | the same per row; `out` may alias `v` |
| `RT_FSCA(ang, &s, &c)` | FSCA | quarter-wave table `rt_fsca_q.c`, only `ang & 0xFFFF` counts |
| `RT_FSRRA(x)` | FSRRA | `1/sqrt` in double; -0 -> -inf, < 0 -> qNaN, +0/denormal -> +inf |
| `RT_FSQRT(x)` | FSQRT | `sqrtss`, SH-4 NaN |
| `RT_FMAC(a, b, c)` | FMAC | exact product + one rounding (equal to fused under RZ) |
| `RT_FTRC(x)` | FTRC | truncation, saturating, NaN -> `0x80000000` |
| `RT_FLOAT(i)` | FLOAT | int -> float in the current mode (RZ) |
| `RT_FADD` `RT_FSUB` `RT_FMUL` `RT_FDIV` | FADD ... FDIV | the C operator, NaN canonicalised |
| `RT_FABS` `RT_FNEG` `RT_F2U` `RT_U2F` | FABS FNEG, bits | bit operations |

Everything runs under the game's FP mode (MXCSR round toward zero + FTZ + DAZ): the harness keeps it all the
time, libsa1 sets it per tick.

## Verification (from `SADC/`)

```
py -3.12 harness/build.py --non-matching --pc --rt --tag T   # all decompiled code + the runtime natively
py -3.12 harness/build.py --files none --rt --tag T          # only the runtime natively (fast)
harness/build/sa1h_T.exe list                                # njSin ... registered at 8C63A9B8 ...
harness/build/sa1h_T.exe diff njSin 2000 1 --ulps 0          # original SDK code vs this C: PASS required
harness/build/sa1h_T.exe diff njSin 2000 1 --warm --ulps 0   # the same on the booted baseline
harness/build/sa1h_T.exe call --route njTan 0x4000           # one call through the routed native code
py -3.12 harness/tests/sim/regress.py --route --exe harness/build/sa1h_T.exe   # every script identical
py -3.12 harness/tools/dump_fsca.py --check                  # rt_fsca_q.c == the harness's fsca_build()
```

`diff` runs the original with `sh4_invoke` and the native function through its adapter on the same RAM and
registers and compares the return value and every RAM word; `PASS` means every conclusive iteration was
bit-identical (`--ulps 0`). In an `--rt` build the decompiled game code calls the runtime directly (no thunk
to the original any more) and `--route` hooks every runtime function at its SDK address, so the 10 sample
traces exercise it from both native and interpreted callers. `build/verify/coverage/<script>.txt` lists which
SDK functions each script executes (a function no trace executes needs its own `diff` argument generator,
design 4.2 P3b-u). Every `*.c` here except `rt_fsca_q.c` (always compiled into the harness, the interpreter
uses it) is picked up by `--rt` automatically.

## libsa1 world core (coordination, 2026-10-20)

What the world core (`libsa1/src/world`, `libsa1/README.md` "World core") provides to the libsa1 profile:

* `float *sa1_rt_xmtrx` and `const RT_PAD *sa1_rt_pad`, defined in `sa1_world.c`: on world entry they point
  at the world's `XMTRX[16]` and its `RT_PAD[4]` (mirrored there as `sa1g_pad`, 20 bytes, layout
  static-asserted: change both together), NULL outside worlds; `sa1_rt_pad` is also NULL in replay mode.
* The rt section of `sa1/guest.h` without DC addresses (`sa1g_fabsf`, `sa1g_sqrtf`, `sa1g_fsrra`,
  `sa1g_fipr`, `sa1g_ftrv`, `sa1g_fsca`, `sa1g_set_cr` / `get_cr` / `set_imask` / `get_imask`,
  `sa1g_set_gbr` / `get_gbr`) in `sa1_world_intrin.c`, on `pc/sh4_fpmodel.h`: rt must not define them.
* `rt_fsca_q.c` is compiled into the world core as it is (pure data).
* `sa1g_event_push(type, id, bank, pos, arg)` (`sa1_world.h`; `SA1_EVENT_*` in `include/libsa1.h`) for the
  sound / death boundary of `rt_game.c` (7.4): no allocation, callable inside a tick.
* Linking: `libsa1/build.py --lower <TUs> --with-rt` lowers `src/rt/*.c` (all but `rt_fsca_q.c`) with the
  game TUs (`pclower --inc src/rt`). pclower rejected rt on 2026-10-20 02:30: R8 "cannot parse type
  'sh4fp_v2df' / 'sh4fp_v4sf'" (`pc/sh4_fpmodel.h` 139 / 168) and R4 "data sa1_rt_pad has no DC address
  and no lowered TU defines it" (world-provided data, like `sa1_rt_xmtrx`).

## Files

| File | Contents | Status |
|---|---|---|
| `rt.h` | profile switch, `SA1_XMTRX`, FP helpers | - |
| `rt_fsca_q.c` | FSCA quarter-wave table `sh4_fsca_q[0x4001]`, generated by `SADC/harness/tools/dump_fsca.py`, SHA-1 `d974ab42bd74113cfcf296b5cd74e85c428cda3b` | equals the harness's `fsca_build()` (`sa1h selftest`, check "FSCA table": all 65,536 angles, sine and cosine; also on the 64-bit harness) |
| `rt_trig.c` | njSin 8C63A9B8, njCos 8C63A970, njTan 8C63A9F4, njSqrt 8C63A9C4 | verified: `diff` PASS, 0 mismatches, `--ulps 0` (2,000 iterations seed 1 cold and `--warm`, 4,000 seed 99); all sample traces identical (the 10 scripts plus the 5 `obj_*` scripts present on 2026-10-20) with everything routed (`--non-matching --pc --rt`) and with only the runtime routed (`--files none --rt`). njSin/njCos/njSqrt run in all 10 original scripts, njTan in 2 |
| `rt_vector.c` | njAddVector 8C638EAC, njSubVector 8C63A7E4, njInnerProduct 8C639090, njOuterProduct 8C639B50, njScalor 8C63A75C, njScalor2 8C63A748, njUnitVector 8C63A94C, njDistanceP2P 8C640128, njDistanceP2L 8C640080 (+ signed FSRRA func_8C63A9DC), func_8C63FEAC, func_8C63FF84, func_8C64016C (+ func_8C650614, func_8C6505F0), njCollisionCheckSS func_8C63FE54 | all 16 verified: `diff --ulps 0` with 0 mismatches (2,000 seed 1 cold and `--warm`, 4,000 seed 99; the four APPROX are NaN return values only), all 15 traces identical with everything routed; details in the `rt_vector.c` section below |
| `rt_misc.c` | SHC C runtime: rand 8C10D334, srand 8C10D39C, memset 8C10D31C, strlen 8C10D3F0, strcpy 8C10D3DC, block copy 8C604500; libsa1-only `sa1_tmu`; how SA1_DIV and struct assignment cover __divls ... / __quick_mvn | verified (diff PASS, traces); details in the section below |
| `rt_input.c` | pdGetPeripheral 8C601874 from the host input (`sa1_rt_pad`) / the harness's pad HLE | verified (cold diff, HLE pad sequence, traces) |
| `rt_texture.c` | njSetTexture 8C608CCC, njSetTextureNum 8C608D14, texlist load 8C608C82 / release 8C6088BC (success, no texture memory) | njSetTexture / njSetTextureNum verified; load / release by design (empty list exact) |
| `rt_draw.c` | draw-state stores (njColorBlendingMode, 8C605DAC / 8C605E0A, 8C60EDE0, njSetConstantAttr / Material, 8C63FAE8 / 8C63FB18, njPrintColor, vsync 8C604546), no-op draws and prints with the draw-capture hook, 8C64133E | state stores verified; draws no-op by design (characterised) |
| `rt_libm.c` | SHC libm: acosf 8C10D008, asinf 8C10D058, atan2 8C10D0A8, atan2f 8C10D0F8, ceilf 8C10D148, floorf 8C10D198, modff 8C10EB50, "njRandom" 8C63A980 and the cores 8C10E590 / E5AC / E6A0 / E6F8 / E758 / E7B8 / EF84 / F164 / FB94 / FBB8 | verified, all 18 bit-exact (bulk check 2,048,576 inputs each, 0 mismatches; `diff --ulps 0` 0 mismatches cold and `--warm`; 15/15 traces routed); details in the section below |
| `rt_matrix.c` | XMTRX and the 128-slot matrix stack: njPushMatrix 8C639C70, njPopMatrix 8C639B98, njInitMatrix `func_8C638FE4`, njUnitMatrix, njSetMatrix, njGetMatrix, njMultiMatrix, njTranslate / njTranslateV, njScale / njScaleV, njRotateX / Y / Z / XYZ / ZXY, njRotate, njInvertMatrix + determinant `func_8C64F3EC`, njCalcPoint, njCalcVector, njProjectScreen | verified (diff PASS, XMTRX-comparing driver, routed traces); details in the section below |

<!-- rt_misc / rt_input / rt_texture / rt_draw section (Phase 3a) -->
## C runtime, input, texture and draw state (`rt_misc.c`, `rt_input.c`, `rt_texture.c`, `rt_draw.c`)

Verified 2026-10-20 in private builds `--files none --rt --tag rt_miscq` (runtime only) and
`--non-matching --pc --rt --tag rt_misc` (everything). "diff" = `sa1h diff <name> 2000 1 --ulps 0`, cold and
`--warm` (identical numbers in both unless noted). "targeted" = a scratch driver linked against the private
harness objects (not committed) that runs the original and the native function on hand-built inputs random
`diff` cannot produce (valid texlists and texture-memory records, Kamui context / material pointers, NULL,
overlapping buffers) and compares R0, all 16 MB of RAM (guest stack excepted), XMTRX and the hardware event log.
"neg" = the same file with one deliberate bug, runtime-only build, routed traces: how many of the 15 sample
traces see it. Inconclusive iterations are the harness's usual ones: a NULL (or 0x0-0xFFFF) pointer that the
original reads as the zero boot-ROM page and the native code faults on, a random 32-bit count that runs past
RAM / the 4M-instruction budget on both sides, or a fault on both sides.

| Function | DC address | Name in the harness / libsa1 | Status |
|---|---|---|---|
| rand | 8C10D334 | `func_8C10D334` / `sa1g_rand` | verified: diff PASS 2000/2000; neg (wrong shift) 4/15 traces |
| srand | 8C10D39C | `func_8C10D39C`, `sa1g_srand` | verified: diff PASS 2000/2000 (no game caller; world boot 6.2) |
| memset | 8C10D31C | `func_8C10D31C` / `sa1g_memset` | verified: diff PASS (1362 exact, 638 inconclusive: NULL destination / huge count); n = 0 and an odd destination PASS; neg 15/15 traces |
| strlen | 8C10D3F0 | `func_8C10D3F0` / `sa1g_strlen` | verified: diff PASS (1807, 193 inconclusive: NULL); odd pointer PASS. The traces call it but do not depend on it (neg 0/15) |
| strcpy-like | 8C10D3DC | `func_8C10D3DC`, `sa1g_strcpy` | verified: diff PASS (1636, 364 inconclusive: NULL); fixed overlapping / aligned / unaligned pairs PASS (dst = src + 4, src = dst + 4, odd pairs); dst = src + 1 smears forever on both sides. Not traced (P3b-u) |
| block copy | 8C604500 | `func_8C604500` | verified: diff PASS (1196, 804 inconclusive: huge counts / NULL); dst = src +- 4, odd addresses, n = 1 PASS; neg 6/15 traces |
| __divls / __divlu / __modls / __modlu | 8C10CA5C ... 8C10CC80 | none (SHC ABI: R1 / R0) | covered by SA1_DIV / SA1_UDIV / SA1_MOD / SA1_UMOD (`common.h`; pclower R6 = guest.h `sa1g_div_s32` ...): `sa1h rtlib` 0 mismatches against C semantics with divisor 0 -> 0, INT_MIN / -1 -> INT_MIN, x % -1 -> 0. The errno word B_8C7AC108 (= 0x44E on divisor 0) is not written: only the C library reads it |
| __quick_mvn | 8C10CD38, 8C10CD44, 8C10CDDC | none (SHC ABI: R0 count, R1 dst, R2 src) | covered by C struct assignment; a C-ABI `func_8C10CDDC` must not exist (its routed adapter would break every interpreted struct copy). Copy order not observable (rt_misc.c) |
| emulated TMU 1 | - | `sa1_tmu`, `sa1_tmu_tcnt1` (libsa1 profile only) | not verifiable in the harness (it has its own copy in `harness/src/pc_runtime.c`); pclower places `sa1_tmu` in pcdata |
| pdGetPeripheral | 8C601874 | `pdGetPeripheral` | verified: cold diff PASS 2000/2000 (no HLE: &B_8C88E9A4[port] untouched, ports 23 / 24 / 0xFFFFFFFF PASS); targeted pad sequence behind the simulator's HLE: 3,000 calls on ports A-D, every word of every returned record equal to the HLE's record, every record at B_8C88E9A4[port]; `--warm` diff FAILs by design (the warm baseline's HLE returns its own record address); routed traces 15/15; neg (press without old) 10/15 traces |
| njSetTexture | 8C608CCC | `njSetTexture` | verified: diff PASS 2000/2000; targeted 250/250 |
| njSetTextureNum | 8C608D14 | `njSetTextureNum` | verified by the targeted test only: 1000/1000 exact (8-texture lists, matching / other keys, TCW over and under 0x100000, texaddr 0, context NULL or set) and 250/250 in the libsa1 situation (texaddr 0, no context). Random diff: FAULT (D_8C673480 is ROM data 0, so every iteration dereferences NULL). Not traced |
| texlist load | 8C608C82 | `func_8C608C82` | by design (success, no texture memory). The error-record reset and the empty list (-1, flag) are exact: targeted 200/200 with an empty list and a garbage error record. The original also loads each texture (fails on unknown names, writes textures[i].texaddr, D_8C673D18, B_8C88F6A0, B_8C88F7A8-B_8C88F7B0), which libsa1 does not: diff FAIL (257 exact, 1496 inconclusive, 247 mismatch). Traced in 2 scripts (gamma) |
| texlist release | 8C6088BC | `func_8C6088BC` | by design (success). Empty list exact (targeted 200/200); otherwise the original frees each texture's memory record and may set the error record: diff FAIL (531, 1465 inconclusive, 4 mismatch). Not traced |
| njColorBlendingMode | 8C605628 | `njColorBlendingMode` | verified: targeted 998/1000 exact, 2 inconclusive (Kamui faults on both sides); diff 1287 exact, 711 inconclusive, 2 mismatches, both with target not 0 / 1, where the runtime makes exactly the original's two Kamui calls: the original Kamui 8C654E60 indexes a 4-entry stack table with context word +0x54 and, for a (random) value above 3, reads its caller's saved PR, which a native caller does not have (traced instruction by instruction). With +0x54 in range no mismatch |
| save draw state | 8C605DAC | `func_8C605DAC` | verified: diff PASS (1010, 990 inconclusive: random small pointers); targeted 1000/1000 (NULL / set / overlapping context and material, both B_8C88F62C & 0x4000 states) |
| restore draw state | 8C605E0A | `func_8C605E0A` | verified: diff PASS (1014, 986 inconclusive); targeted 1000/1000 (material overlapping the save area included). Limit: a context whose texture words +0x90..+0x9C overlap B_8C890104 gives 27/1000 differences (clang merges the four stores; distinct C objects cannot overlap, and the SDK's layout never does) |
| control 3D | 8C60EDE0 | `func_8C60EDE0` | verified: diff PASS 2000/2000 |
| njSetConstantAttr | 8C60EDE6 | `njSetConstantAttr` | verified: diff PASS 2000/2000 |
| njSetConstantMaterial | 8C60EDF0 | `njSetConstantMaterial` | verified: diff PASS (1784, 216 inconclusive: NULL) |
| render-state switches | 8C63FAE8, 8C63FB18 | `func_8C63FAE8`, `func_8C63FB18` | verified: diff PASS (1105 / 1108, rest inconclusive); targeted 998/1000 each, 2 inconclusive (Kamui faults on both sides) |
| vsync step | 8C604546 | `func_8C604546` | verified: diff PASS 2000/2000 |
| register vblank handler | 8C64133E | `func_8C64133E` | no-op returning 0 by design (libsa1 has no interrupts). diff PASS 2000/2000 only because both baselines have the SDK table pointer D_8C67C004 = 0 (the original's table writes land in the NULL page) |
| njDrawSprite2D, njDrawSprite3D | 8C63EEB4, 8C63EF74 | same | no-op by design, plus the original's njSetTexture(sp->tlist). Characterised (targeted, 100 sprites each): the only differing words are renderer state: D_8C67348C / D_8C673490, B_8C890104-B_8C89012C, the context / material words when those pointers are set, TA / store-queue events and renderer buffers; XMTRX and D_8C673480 never differ. diff FAILs by design |
| njDrawLine3D, triangles, polygon | 8C63CE24, 8C63D414, 8C63D66C | same | no-op by design; characterised as above (B_8C890104-B_8C89012C, context / material, events, renderer buffers) |
| njDrawPolygon2D (round 0.9.1) | 8C63EBC8 | `func_8C63EBC8` | no-op by design (a "void" boundary stub before; the original saves / restores the draw state around its Kamui draw, Ghidra 8C63EBC8.c: renderer state only), plus the 2D capture record (POLY2D); njDrawSprite2D's record keeps pri, the running exec and the texlist before the draw (round 0.9.1, the HUD) |
| njPrintColor | 8C640766 | `njPrintColor` | verified: diff PASS 2000/2000 |
| njPrintC, print number | 8C640922, 8C640A80 | same | no-op by design; the original also sets D_8C67348C = -2 and the print state B_8C89059C (characterised) |

Routed sample traces (`regress.py --route`): 15/15 identical with everything routed (`--non-matching --pc --rt`) and
with only the runtime routed (`--files none --rt`). The original scripts execute rand (14 of 15), memset, strlen,
8C604500, pdGetPeripheral, njSetTexture, njColorBlendingMode, 8C605DAC / 8C605E0A, njSetConstantMaterial,
8C63FAE8 / 8C63FB18 and njDrawSprite2D (15), njDrawSprite3D (6), njDrawLine3D and 8C608C82 (2), 8C63D414 (1);
never srand, strcpy, njSetTextureNum, the release, 8C60EDE0, njSetConstantAttr, 8C63D66C, the prints, 8C64133E
and 8C604546. Draw state the game never reads back (njSetTexture, njSetConstantAttr) does not show in the traces
(neg 0/15): diff and the targeted test are the proof for those.

Hooks and interfaces for later phases:
* **Draw capture (design 7.2, NOT IMPLEMENTED):** every draw entry point calls `RT_DRAW_CAPTURE(kind, object,
  n, attr)` first and njSetTextureNum calls `RT_TEX_CAPTURE(n)`; both expand to nothing until the world core
  defines them. The Kamui calls (`RT_KM_STATE` / `RT_KM_CONTEXT`) run the original Kamui code in the harness and
  are empty in libsa1.
* **Pad input:** `sa1_rt_pad` is host data (pclower `RT_DATA`): the world core points it at the ticking world's
  `sa1g_pad pad[4]` (`libsa1/src/world`, the `RT_PAD` layout of `rt_input.c`) on tick entry, NULL in replay mode
  (= nothing connected).
* The Kamui context / material pointers B_8C88FCB4 / B_8C88FCD4 stay 0 in libsa1 (no Kamui init); a NULL pointer
  reads as zeros and drops stores, like the harness's NULL page. Since 2026-10-20 the reference simulator runs
  njInitView (its screen set-up, header flag 8), which selects context 0 (B_8C88FCB4 = &B_8C88F834[0]); only the
  SDK's own draws read the context, no game code does, and the traces do not depend on it (with the runtime routed
  into the harness the 15 reference scripts stay identical).
* **The sphere visibility test 8C038D00** (`rt_draw.c` section 6, both profiles; hand-written SH-4 in the game, no
  C in sadc): the original's test exactly - 0 on sub step 1, FTRV of the centre with XMTRX, the near / far window,
  the projection with 1 / (z + r) against the clip rectangle in the original's operation order, and its two quirks
  (the y scale is aspect y times the screen centre x; the last test uses the x scale and the x maximum). Verified:
  `sa1h diff func_8C038D00 --ulps 0` PASS (2,000 cold, 2,000 warm, 20,000 seed 99) and `regress.py --route` with
  `--non-matching --pc --rt` 15/15 identical against the references with the screen (Tails, Knuckles, Big and the
  object scripts ask it about 140,000 times, with both answers). In the libsa1 profile the capture counts the
  answers of the pair (RT_CAP_HDR vis_*: the host shows a hidden character all the same, `sa1_capture.h`).
* `pclower --profile all --inc libsa1/src/rt --compile` lowers the four files without errors (ga32 x86-windows,
  ga64 x86_64-windows and x86_64-linux compile checks pass); `sa1_tmu` becomes an R4 pcdata export, `sa1_rt_pad`
  runtime (host) data, 8C10D39C / 8C10D3DC code-map aliases of `sa1g_srand` / `sa1g_strcpy`.
<!-- end of rt_misc / rt_input / rt_texture / rt_draw section -->

<!-- rt_libm section (Phase 3a) -->
## SHC libm (`rt_libm.c`)

The C library maths of the game (code region 8C10C10C-8C111194), design 4.2 / 4.5. Every constant the library
loads (its NaN word D_8C1601F4, +-0, +-inf, +-1, +-0.5, pi, pi/2, pi/4, the atan breakpoints and atan values
D_8C16038C-D_8C1603A0 and the polynomial D_8C1603A4[6]) is read in place through its symbol (pclower: `SA1_GV`
arena reads of the user's image); the only literals are bit masks and 2^31 / 2^32. errno is the SDK word
B_8C7AC108 (0x44C inf / 0x44D NaN result, set by the wrappers only); njRandom's integer part goes to B_8C89033C.
The behaviour (operation order, FTRC saturation, NaN words, sign handling) is summarised in the file header.

| Function | DC address | Name | Status |
|---|---|---|---|
| acosf | 8C10D008 | `func_8C10D008` | verified |
| asinf | 8C10D058 | `func_8C10D058` | verified |
| atan2 (y, x), second wrapper | 8C10D0A8 | `func_8C10D0A8` | verified |
| atan2f | 8C10D0F8 | `atan2f` (pclower R7: `sa1g_atan2f`) | verified |
| ceilf | 8C10D148 | `func_8C10D148` | verified (FTRC saturation: ceilf(3e9) = 2147483520) |
| floorf | 8C10D198 | `func_8C10D198` | verified (floorf(-3e9) = -2^31) |
| modff | 8C10EB50 | `func_8C10EB50` | verified |
| "njRandom" (modff fraction) | 8C63A980 | `njRandom` | verified (njRandom(-2.75) = -0.75, B_8C89033C = -2) |
| acos core = pi/2 - asin | 8C10E590 | `func_8C10E590` | verified |
| asin core (FSQRT + atan core + ldexp) | 8C10E5AC | `func_8C10E5AC` | verified |
| atan2 core (FDIV, +-pi when x has the sign bit) | 8C10E6A0 | `func_8C10E6A0` | verified |
| ceil core | 8C10E6F8 | `func_8C10E6F8` | verified |
| floor core | 8C10E758 | `func_8C10E758` | verified |
| ldexp core (exponent field) | 8C10E7B8 | `func_8C10E7B8` | verified |
| atanf core (eighths via 8C10FBB8, polynomial) | 8C10EF84 | `func_8C10EF84` | verified |
| modf core | 8C10F164 | `func_8C10F164` | verified |
| atan polynomial (5 FMAC Horner steps over D_8C1603A4) | 8C10FB94 | `func_8C10FB94` | verified |
| remainder / quotient core | 8C10FBB8 | `func_8C10FBB8` | verified |

Verification (2026-10-20, private builds `--files none --rt --tag rt_libmq` and `--non-matching --pc --rt --tag
rt_libm`, both with only `rt_libm.c` and `rt_trig.c` of this directory):

* **Bulk check** (scratch driver linked against the private harness objects, not committed): per function
  1,048,576 inputs from per-function generators (domain values, the 2^31 / 2^32 truncation edges, quotients
  near 2^23-2^33, integral and half-integral values, +-0, denormals, +-1, +-inf, signalling / quiet NaNs, ldexp
  exponents up to +-2^31) plus a 1,048,576-point sweep over all 2^32 bit patterns of the first argument; the
  original in the interpreter vs the native function, comparing FR0 bits, errno and the *ip / *q / B_8C89033C
  word. **0 mismatches for all 18** (36.9 M calls). The seven one-argument cores (8C10E590, E5AC, E6F8, E758,
  EF84, F164, FB94) also on a 67,108,864-point sweep each: 0 mismatches. A copy with the Horner FMAC split into
  FMUL + FADD fails in 9 of the 18 functions, so the check sees last-bit differences.
* **NaN payloads**: the recorded vectors (8.2 M, every NaN result included) replayed on an x86_64 build of the
  file in the libsa1 profile (`sa1_rt_xmtrx`, constants from the image, MXCSR RZ + FTZ + DAZ): 0 mismatches, so
  the NaN words are exact too (D_8C1601F4 = 0x7F800001 where the original returns it, 0x7FBFFFFF after
  arithmetic). In the 32-bit build a NaN *returned* through the i386 ABI passes the x87 stack, which sets the
  quiet bit (0x7FC00001 / 0x7FFFFFFF); see the file header. The NaN stays a NaN for every game caller.
* **`diff <f> 2000 1 --ulps 0`**, cold and `--warm` (identical numbers): 0 mismatches for all 18. PASS: atan2f,
  8C10D0A8, ceilf, floorf, njRandom, 8C10E6F8, E758, E7B8, EF84, FB94 (2000/2000), modff 8C10EB50 (1807 exact,
  193 inconclusive) and 8C10F164 (1797 / 203): the inconclusive iterations pass a NULL-page pointer for *ip,
  which the original writes to the boot-ROM page and the native code faults on. APPROX = NaN vs NaN only (with
  `--ulps 0` nothing else can be approx), from the i386 return above: acosf (119 exact, 1881 NaN: random
  arguments +-1000 are mostly outside [-1, 1]), asinf (118 / 1882), 8C10E590 (121 / 1879), 8C10E5AC (136 / 1864),
  8C10E6A0 (1999 / 1 NaN, 0/0), 8C10FBB8 (1783 exact, 26 NaN for y = 0, 191 inconclusive: NULL *q).
* **Traces**: `regress.py --route` 15/15 identical with everything routed (`--non-matching --pc --rt`) and with
  only the runtime routed (`--files none --rt`). Negative test (runtime-only build, atan2's x >= 0 result
  scaled by 1.0001): 13/15 traces differ. Coverage: acosf, asinf, both atan2 wrappers and the cores they use
  (E590, E5AC, E6A0, E7B8, EF84, FB94, FBB8) run in all 15 scripts, floorf / 8C10E758 in 11, ceilf / 8C10E6F8
  in 5 (`obj_*`), njRandom / modff / 8C10F164 in 1 (`obj_ec1_sonic`).
* Builds: no warnings with `-Wall -Wextra -Wpedantic` for x86-windows, x86_64-windows, x86_64-linux and
  x86-linux in the libsa1 profile; no library calls (the only calls are to the file's own `rt_asin` /
  `rt_atan`); `pclower --profile all --inc libsa1/src/rt --compile` lowers it with 0 errors (ga32 and ga64
  compile checks pass, 18 code-map entries).

Interface notes: all 18 are plain SHC-ABI C functions (FR4 / FR5 / R4 in, FR0 out, *ip / *q through memory).
The originals leave R0 at 4 or at the errno code; R0 is scratch under the SHC ABI (the caller of a float
function takes FR0), so the adapters' R0 is not part of the interface. The library's own callers of
the cores (8C10CE9C, 8C10E878 -> ldexp; 8C10FCBC -> 8C10FBB8, all still interpreted) read FR0 and memory only;
8C10FCBC's unreachable default case reads FR5, which the original 8C10FBB8 does not change and the adapter does
not either.
<!-- end of rt_libm section -->

## `rt_vector.c`: vector maths, distances, intersections (2026-10-20)

njAddVector 8C638EAC, njSubVector 8C63A7E4, njInnerProduct 8C639090, njOuterProduct 8C639B50, njScalor
8C63A75C, njScalor2 8C63A748, njUnitVector 8C63A94C, njDistanceP2P 8C640128, njDistanceP2L 8C640080 with the
signed FSRRA `func_8C63A9DC`, the line/plane helpers `func_8C63FEAC` (line x plane -> point),
`func_8C63FF84` (point-line distance and foot point), `func_8C64016C` (plane x plane -> NJS_LINE) with the
parallel tests `func_8C6505F0` / `func_8C650614`, and njCollisionCheckSS (`func_8C63FE54`, the name is a
`#define` in `ninja_funcs.h`).

How it is written: one C operation per FPU instruction in the original order. Every FMAC is an `RT_FMAC`
(counts equal the original's: njOuterProduct 3, njDistanceP2P 2, njDistanceP2L 7, 8C63FEAC 7, 8C63FF84 11,
8C6505F0 / 8C650614 / 8C63FE54 2 each, the others 0). Every FIPR is an `RT_FIPR` with w = +0 (FLDI0).
FSRRA and FSQRT go through the models. Results that are stored or returned and can be NaN are canonicalised
(`RT_FADD` ...). Inputs are read in the original order relative to the stores, so aliased arguments behave
like the original. Nothing touches XMTRX. The edge cases are kept: njUnitVector(0) stores and returns NaN,
and the intersection helpers divide by zero unchecked.

**Registers.** Only the C return value is part of the interface. A scan of all 1,932 call sites (891 in 1ST_READ, 1,041 in the
overlays, all SHC code) for a caller-saved register read after the call before any write found two reads.
Both are an uninitialised local on a zero-length path:
- func_8C055E40 reads FR4 = +0 left by njDistanceP2P. `code_8C055D20.c` already uses `PC_ONLY(r = 0.0f)`.
- func_8C0ED0BA, not decompiled yet, reads FR6 = v->z left by njScalor. Both of its tests are then false,
  and its C must take that branch.

An interpreted caller of a routed native function sees its own stale register there instead. This affects
the harness only. 8C63FEAC's FR0 is not read by any of its 6 callers.

**Verification.** The private build was `harness/build.py --non-matching --pc --rt --tag rt_vector --exclude rt_libm.c
rt_matrix.c rt_misc.c rt_input.c rt_texture.c rt_draw.c`, so the runtime was `rt_trig.c` + `rt_vector.c`
only, and the other files, still in progress, could not mix in. `diff --ulps 0` results are given as exact / approx / inconclusive,
with 0 mismatches everywhere. The figures are the same for 2,000 iterations of seed 1 cold and `--warm`.
4,000 iterations of seed 99 also gave 0 mismatches: the same twelve PASS and the same four APPROX, NaN returns only.
The combined build with every `src/rt/*.c` present at about 03:05 (`--tag rt_vector_all`) and the runtime-only build
(`--files none --rt`, game interpreted) also kept all 15 traces identical with `--route`.

| Function | diff (seed 1) | Traces: runs in / 1% error makes differ | Status |
|---|---|---|---|
| njAddVector | PASS 1,630 / 0 / 370 | 15 / 14 | verified |
| njSubVector | PASS 1,618 / 0 / 382 | 15 / 1 | verified |
| njInnerProduct | PASS 1,621 / 0 / 379 | 15 / 15 | verified |
| njOuterProduct | PASS 1,460 / 0 / 540 | 15 / 4 | verified |
| njScalor | PASS 1,809 / 0 / 191 | 15 / 11 | verified |
| njScalor2 | PASS 1,813 / 0 / 187 | 2 / 0 | verified (diff) |
| njUnitVector | APPROX 599 / 1,194 / 207 (RAM-only: PASS 1,800 / 0 / 200) | 15 / 14 | verified |
| njDistanceP2P | PASS 1,629 / 0 / 371 | 15 / 2 | verified |
| njDistanceP2L | APPROX 725 / 877 / 398 (RAM-only: PASS 1,610 / 0 / 390) | 15 / 15 (foot point), 0 (return value) | verified |
| func_8C63A9DC | PASS 2,000 / 0 / 0 | 15, called only by njDistanceP2L (the native one inlines it) / 0 | verified (diff) |
| func_8C63FEAC | PASS 1,612 / 0 / 388 | 3 / 0 | verified (diff) |
| func_8C63FF84 | APPROX 539 / 1,091 / 370 (RAM-only: PASS 1,611 / 0 / 389) | 3 / 2 | verified |
| func_8C64016C | APPROX 741 / 857 / 402 (RAM-only: PASS 1,625 / 0 / 375) | 6 / 0 (line point) | verified (diff) |
| func_8C650614 | PASS 1,637 / 0 / 363 | 6 / 6 (result inverted) | verified |
| func_8C6505F0 | PASS 1,615 / 0 / 385 | 3 / 0 (result inverted) | verified (diff) |
| func_8C63FE54 | PASS 1,598 / 0 / 402 | none (P3b-u) | verified (diff) |

- **APPROX** comes only from a NaN *return value*. The 32-bit adapter returns it through x87 ST(0), which
  quiets 7FBFFFFF to 7FFFFFFF: `call --route njUnitVector 0x8CF00000` on a zero vector gives FR0 7FFFFFFF,
  and the original gives 7FBFFFFF. The random inputs often hold zero or denormal (DAZ) vectors. "RAM-only"
  is the same function behind a void wrapper registered at the same address, in a throw-away diagnostic
  build, so `diff` compares memory only: every word is exact. x86_64 returns in XMM0.
- **Inconclusive** iterations are all "original ok, native access violation" at the NULL page. The argument
  generator passes NULL for about 10% of pointer arguments: the original reads the zero boot ROM and the
  native code faults, and `diff` treats this as inconclusive by design. NULL `cp` / `c` / `l` is a legal
  input and is compared.
- **Paths** were counted in a diagnostic build (2,000 diff inputs each, NULL-page iterations not counted):
  - func_8C64016C: parallel with l 1,344, parallel with l NULL 149, l NULL 11, p = (x, y, 0) 18,
    p = (x, 0, z) 16, p = (0, y, z) 88.
  - func_8C63FEAC: parallel with c 1,101, parallel with c NULL 118, intersection 331, c NULL 36.
  - njUnitVector: zero or denormal vector 1,198, other 603.
  - njDistanceP2L: cp != NULL 467 + 968 with v = 0, cp NULL 51 + 125 with v = 0.
  - func_8C63FF84: cp != NULL 478 + 960 with v = 0, cp NULL 50 + 124 with v = 0.
  - njCollisionCheckSS: hit 134, miss 1,512.
- **Traces.** All 15 scripts were identical with everything routed (`regress.py --route`). For the negative
  tests, each function was made 1% wrong (or its parallel test inverted) in a copy built with only the
  runtime native (game interpreted, `--route`). "Runs in" is from `build/verify/coverage`. A function whose
  error does not reach the player trace is proven by `diff` alone.

<!-- rt_matrix section (Phase 3a) -->
## Matrix state and matrix functions (`rt_matrix.c`)

Verified 2026-10-20 in private builds `--files none --rt --tag rt_matrix_q` (runtime only) and
`--non-matching --pc --rt --tag rt_matrix` (everything). "diff" = `sa1h diff <name> 2000 1 --ulps 0`, cold and
`--warm` (identical numbers in both unless noted); its inconclusive iterations are a NULL pointer argument (one
in ten) or a random small stack pointer in the BSS words, on which both sides fault. `diff` cannot see XMTRX (it
compares R0 / FR0 and RAM, and every iteration starts from the same CPU template) and its random words are
rarely full-precision floats, so the main proof is "xm": a scratch driver (not committed) linked against the
private harness objects. Per iteration it sets a random XMTRX and FR bank, random matrices / vectors in a
scratch window with frequent aliasing (md = ms + k, pd = ps, pd or ps inside m, v inside m, push(m) with m = the
current or the next slot), random SDK state (stack pointer, depth and limit -2..8, the njCalcVector flag, the
screen words, the unit matrix itself) and draws floats from three regimes: full-mantissa values from 2^-12 to
2^14, small integers (exactly singular matrices, exact zeros) and specials (+-0, +-inf, NaNs, denormals,
+-FLT_MAX). It runs the original with `sh4_invoke` and the runtime through its adapter and compares XMTRX (16
words), R0 / FR0 and every RAM page either side may have written, bit for bit, plus a periodic check that no
other page changed. 20,000 iterations per function (seeds 11-14 x 5,000), a third of them with m = NULL (the
XMTRX path).

| Function | DC address | diff | xm (exact / 20,000) | Traces |
|---|---|---|---|---|
| njPushMatrix | 8C639C70 | PASS 1567 exact, 433 inconclusive | 20,000 (at the limit, push(NULL), m = slot) | 15/15 |
| njPopMatrix | 8C639B98 | see below | 20,000 (n = 0, 0xFFFFFFFF, depth, depth - 1) | 15/15 |
| njInitMatrix | 8C638FE4 `func_8C638FE4` | PASS 1808, 192 inconclusive | 20,000 (m overlapping the unit matrix from m >= D_8C67C640, and the stack words) | game init only, not in the logic steps |
| njUnitMatrix | 8C63A8E0 | PASS 2000 | 20,000 (m overlapping the unit matrix) | 15/15 |
| njSetMatrix | 8C63A770 | PASS 1807, 193 inconclusive | 20,000 (overlapping md / ms) | 15/15 |
| njGetMatrix | 8C638F94 | PASS 1799, 201 inconclusive | 20,000 | 15/15 |
| njMultiMatrix | 8C63997C | PASS 1795, 205 inconclusive | 18,754 + 1,246 FIPR NaN encoding | 15/15 |
| njTranslate | 8C63A878 | PASS 2000 | 19,199 + 801 FIPR NaN encoding | 15/15 |
| njTranslateV | 8C63A804 | PASS 1783, 217 inconclusive | 19,221 + 779 FIPR NaN encoding | 15/15 |
| njScale | 8C63A69C | PASS 2000 | 20,000 | 15/15 |
| njScaleV | 8C63A5EC | PASS 1799, 201 inconclusive | 20,000 | 15/15 |
| njRotateX / Y / Z | 8C639EC8 / 8C639F5C / 8C63A1CC | PASS 2000 each | 20,000 each (inf / NaN in the XMTRX rows the memory path borrows) | 15/15 |
| njRotateXYZ | 8C639CF4 | PASS 2000 | 20,000 (zero angles, angles with zero low 16 bits) | 15/15 |
| njRotateZXY | 8C639FF8 | PASS 2000 | 20,000 | not traced (P3b-u) |
| njRotate | 8C63A268 | PASS 1796, 204 inconclusive | 20,000 (unit, axis-aligned, +-0, zero and NaN axes; v inside m) | not traced (P3b-u) |
| determinant | 8C64F3EC `func_8C64F3EC` | PASS 2000 | 18,723 + 1,277 FR0 NaN encoding | 15/15 |
| njInvertMatrix | 8C6390B0 | PASS 2000 | 20,000 (exactly singular: 0x7F7FC99E fill, R0 0) | 15/15 |
| njCalcPoint | 8C638ECC | PASS 1633, 367 inconclusive | 19,343 + 657 FIPR NaN encoding | 15/15 |
| njCalcVector | 8C638F28 | PASS 1574, 426 inconclusive | 19,644 + 356 FIPR NaN encoding (flag on and off) | 15/15 |
| njProjectScreen | 8C639BDC | PASS 1634, 366 inconclusive | 20,000 | 7/15 |

* **njPopMatrix diff**: cold 98 exact, 1,899 inconclusive, 3 fault differences; `--warm` 942 / 1,057 / 1 (`--warm`
  seeds 2-7: 1 to 6 each). In every fault difference the random 32-bit count and stack pointer put the slot outside main
  RAM (store-queue area 0xE0xxxxxx, VRAM mirror 0xA4xxxxxx): the original reads it without a hardware event,
  native code can only reach RAM at its guest address and faults. With in-range counts the diff is clean:
  `--warm --args 1`, `--args 2`, `--args 0xFFFFFFFF` PASS (1079 / 1066 / 1055 exact, 0 mismatches).
* **NaN encodings ("known x87 delta")**: in those xm iterations all 16 XMTRX words, R0 and every non-NaN word
  are identical; only a NaN's encoding differs, always the same way. The runtime gives the SH-4 default NaN
  0x7FBFFFFF. The interpreter's FIPR stores 0x7FFFFFFF: `harness/src/sh4_cpu.c` calls the out-of-line
  `sh4_fipr` (SH4_FPMODEL_EXTERN), whose float result comes back through x87 ST(0) on 32-bit x86, and loading
  0x7FBFFFFF (an x86 signalling-NaN pattern) into ST(0) sets the quiet bit. The same happens to the determinant's
  NaN FR0 in the 32-bit adapter (0x7FFFFFFF where the interpreted code keeps 0x7FBFFFFF). diff and the trace
  comparison treat NaN vs NaN as one class. Fix belongs to the harness: give the interpreter the static inline
  models (or return through memory) so FIPR stores 0x7FBFFFFF; the runtime then matches bit for bit.
* **Routed traces** (`regress.py --route`): 15/15 identical with everything routed (`--non-matching --pc --rt`)
  and with only the runtime routed (`--files none --rt`). Coverage (`build/verify/coverage`): every function above
  runs in all 15 scripts except njProjectScreen (7), njInitMatrix (game init), njRotateZXY and njRotate (never).
  Negative tests, runtime-only build: a 1-ulp error in njCalcVector's x makes 15/15 traces differ; a 1-ulp error
  in the rotation helper (njRotateX / Y / Z / XYZ) makes 15/15 differ.
* **Registers**: XMTRX after the call is reproduced for every function (FTRV paths, the memory paths of
  njRotateX / Y / Z / XYZ / ZXY that borrow and restore XMTRX rows, push / pop / set / unit / init). njPushMatrix
  and njPopMatrix return 1 / 0 like their R0 (only push's is ever read: adv00 0C900724; a scan of the JSR sites
  in 1ST_READ and the overlays finds no read of R0 after njPopMatrix). FR0-FR11 contents are not reproduced; the
  SDK callers of these functions are SHC-compiled C and rely on the ABI only.
* **Limit**: njInitMatrix with m overlapping the unit matrix from below (m < D_8C67C640 < m + 16) differs (xm
  with that case: 19 of 500): on the SH-4 the second half reads D_8C67C640 after the first half was written over
  it; in C that overlap needs pointer arithmetic outside an object, and clang loads the unit matrix first. The
  game calls it once, njInitMatrix(B_8C7EC8E0, 128, 0).
* **libsa1 profile**: compiles without warnings (`-Wall -Wextra -Wpedantic`, the `tools/pc_cflags.txt` flags) for
  x86_64-windows, x86-windows and x86_64-linux, no library calls, no FMA; `pclower --profile all --inc
  libsa1/src/rt --compile` lowers it with 0 errors (ga32 / ga64 compile checks ok). njPopMatrix computes the slot
  as pointer - (Sint32)(n << 6) bytes, which equals the SH-4's 32-bit wrap on 64-bit hosts too.
<!-- end of rt_matrix section -->

## Particle execs (`rt_effect.c`, 2026-10-20; removed 2026-10-21)

Removed on 2026-10-21: sadc decompiled the three execs (`src/1st_read/code_8C046480.c` and `code_8C046960.c`, all
MATCH; harness `diff --ulps 0` 0 mismatches and the routed references 15/15 identical), pclower lowers them with the
game, and this file is gone. The only behavioural difference is the NaN payload: the decompiled C uses plain
operators, `rt_effect.c` canonicalised NaN results like the SH-4 (`RT_FADD` / `RT_FMUL`), which matters only with
NaN inputs (libsa1_integration.md: NaNs are one class in the traces). The record of the hand-written version:

The particle manager 8C09DF64 (sadc `code_8C09DE8C.c`) runs every particle's exec each step and recycles the nodes
whose exec cleared itself (8C09DE34). libsa1's step runs the manager since the particle system was decompiled
(`src/world/sa1_world_tick.c`), so the execs of the particles the decompiled spawners create must run as well; three
of them have no C in sadc (chunks 8C046480 and 8C046960, SHC-compiled leaf functions). They are game code: written
from the asm (one C operation per FPU instruction in its order, FMAC as `RT_FMAC`, the literal-pool constants as exact
hex floats, the cell test on the low half of the descriptor's cell count as `EXTS.W` / `MOV.W` read it), they call
the decompiled release 8C09DE34 like the original's tail call. When sadc decompiles them this file goes.

Verified 2026-10-20 in a private runtime-only build (`harness/build.py --files none --rt --tag integ5rt`):

* **diff** (`sa1h diff <f> 2000 1 --ulps 0`, cold and `--warm`, and 20,000 iterations seed 99): func_8C046480 exact
  409 / 3,965, func_8C0464EE exact 250 / 2,405, func_8C046960 exact 546 / 5,251, 0 mismatches (the inconclusive
  iterations are random pointers that fault on both sides);
* **routed reference runs** (`regress.py --route`): 15/15 identical; the particle pool (B_8C7493D4, 512 x 0x4C
  bytes) of a routed run equals the pure interpreter's byte for byte at steps 150, 200, 300 and the end of
  obj_tp2_amy (13 live dust particles at step 150) and ec1_spindash (20 live splash particles at step 150);
* **libsa1**: the world's pool equals the reference RAM (`replay_test --dump-ram`, the harness's `--dump-ram-at`) at
  ec1_spindash steps 150 and 200, the ten reference traces replay strictly with the manager running.
<!-- end of rt_effect section -->
