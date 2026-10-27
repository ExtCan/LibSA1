# Compiler identification: Sonic Adventure (DC, US 1.005, MK-51000) `1ST_READ.BIN`

Scope: game code at `0x8C010000`-`~0x8C10B9A0` of `disc/1ST_READ.BIN` (load address `0x8C010000`).
Test harness and all C probe files: `tools/tests/compiler_probe/` (run `py -3.12 run_all.py`).

## Result

| Item | Value |
|---|---|
| Compiler | **Hitachi SH C Compiler Ver. 5.1 Release 03** (`tools/shc/5.1r03/bin/shc.exe`; PE build date of shc/shcgen/shcpep 1999-02-08) |
| Flags | `-cpu=sh4 -endian=little -fpu=single -division=cpu -optimize=1 -speed -macsave=0 -extra=a=1800 -round=zero -code=machinecode` |
| Defaults included above | `-round=zero` and `-division=cpu` are SHC defaults; `-pic=0` (default) also applies |
| Object output | `-code=machinecode` (shc writes the SYSROF object directly; NOT `-code=asmcode` + asmsh) |
| Linker | Hitachi `lnk`; every object's P section is `ALIGN=32`; the linker fills the gap between objects with `00` bytes |
| Confidence | High. A 160-byte TU tail (code + pool + alignment fill + linker gap) is byte-identical only with 5.1r03; codegen restricts the version to 5.1r03/r04/r08, and the alignment-fill byte order excludes r04/r08 |

Flags that are **not** used (contrary to the SA2 decomp, which used SHC 5.1r13 with
`-round=nearest -aggressive=2 -inline -loop -sjis -string=const`):
`-round=nearest` (literals are truncated), `-aggressive=2` (FDIV by a constant survives), `-macsave=1`, `-size`/`-nospeed`, `-pic=1`, `-fpu=double`.

Flags with no observable effect on the code bytes (cannot be determined; harmless to add or leave out):
`-loop`, `-inline` (both implied/no-ops once `-speed` is given: a 40-file random-program fuzz showed zero differences),
`-aggressive=0/1`, `-string=const|data` (data placement only), `-sjis|-euc`, `-comment=nonest`, `-debug`.

## Version discrimination

Build dates (PE TimeDateStamp of shc.exe): 5.0r26 1998-06-25, 5.0r28 1998-07-14, 5.0r31 1998-09-25,
5.0r32 1998-10-20, 5.1r01 1998-11-05, **5.1r03 1999-02-08**, 5.1r04 1999-04-15, 5.1r08 1999-07-15,
5.1r11 2000-02-08, 5.1r13 2000-09-04. 5.0r10 has no `-cpu=sh4` (sh1..sh3e only) and was not tested further.
The Katana libraries linked into the game are dated Feb-Aug 1999 (`Shinobi Ver 1.41 Build Mar 03 1999`,
`Ninja Ver 01000081 Build Mar 04 1999`, `syCache Ver 1.52 Build Jul 22 1999`, `ntUtl Ver 2.00 Build Aug 02 1999`),
consistent with a Feb-1999 compiler.

Output of `tools/tests/compiler_probe/run_all.py` (function mode = number of differing normalised
instructions, `OK` = identical; byte mode = number of differing halfwords, `MATCH` = byte identical):

```
test / function                    5.0r26  5.0r28  5.0r31  5.0r32  5.1r01  5.1r03  5.1r04  5.1r08  5.1r11  5.1r13
t01:isMode2       8C01B35E         4       4       OK      OK      OK      OK      OK      OK      OK      OK
t01:isMode3       8C01B36C         4       4       OK      OK      OK      OK      OK      OK      OK      OK
t01:getVar        8C01B37A         OK      OK      OK      OK      OK      OK      OK      OK      OK      OK
t01:clrVar        8C01B380         OK      OK      OK      OK      OK      OK      OK      OK      OK      OK
t02:SetSub        8C01B3E0         4       4       OK      OK      OK      OK      OK      OK      OK      OK
t02:GetSub        8C01B406         OK      OK      OK      OK      OK      OK      OK      OK      OK      OK
t02:GetB6         8C01B40E         OK      OK      OK      OK      OK      OK      OK      OK      OK      OK
t02:SetB6         8C01B416         4       4       OK      OK      OK      OK      OK      OK      OK      OK
t02:Check         8C01B436         22      22      27      27      4       OK      OK      OK      OK      OK
t03:F             8C01B500         10      10      3       3       3       OK      OK      OK      OK      OK
t03:G             8C01B554         3       3       3       3       3       OK      OK      OK      OK      OK
t04:IsStage1      8C0673E0         10      10      10      10      10      OK      OK      OK      8       8
t04:GetDir        8C067414         9       9       OK      OK      OK      OK      OK      OK      OK      OK
t04:Dummy         8C067456         OK      OK      OK      OK      OK      OK      OK      OK      OK      OK
t05:Find          8C011042         OK      OK      OK      OK      OK      OK      OK      OK      OK      OK
t05:InRange       8C01B818         4       4       OK      OK      OK      OK      OK      OK      OK      OK
t05:SetB7         8C01B636         OK      OK      OK      OK      OK      OK      OK      OK      OK      OK
t06:CallSeq       8C036F12         4       4       OK      OK      OK      OK      OK      OK      OK      OK
t06:Start         8C01B640         6       6       OK      OK      OK      OK      OK      OK      OK      OK
t07:SetColor      8C02E820         17      17      13      13      OK      OK      OK      OK      OK      OK
t07:Disp1         8C02E886         12      12      5       5       5       5       5       5       5       5    (near miss)
t07:Disp2         8C02E8DA         21      21      27      27      OK      OK      OK      OK      OK      OK
t08:IsOddSub      8C01B5E0         (near miss; jump-table words decoded as code in function mode)
t08:A             8C034D0A         6       6       2       2       2       2       2       2       2       2    (near miss)
t08:Voice4        8C015798         28      28      20      20      20      20      20      20      20      20   (near miss)

byte-exact (shc -code=machinecode + lnk at the original address)
t04 SA1 flags   8C0673E0-8C067480  56      56      56      56      56      MATCH   3       3       58      58
t04 without -speed                 56      56      56      56      56      56      58      58      58      58
t04 without -extra=a=1800          62      62      62      62      62      16      16      16      62      62
t07 SetColor only 8C02E820-886     45      45      46      46      MATCH   MATCH   MATCH   MATCH   MATCH   MATCH
t07 pad+pool      8C02E936-970     MATCH   MATCH   MATCH   MATCH   MATCH   MATCH   5       5       5       5

discriminators
d01 ushort>>8 signed-shift seq     plain   plain   plain   plain   plain   NOT     NOT     NOT     plain   plain
d02 a/8.0f                         FDIV    FDIV    FDIV    FDIV    FDIV    FDIV    FDIV    FDIV    FDIV    FDIV
d02 a/8.0f -aggressive=2           FMUL    FMUL    FMUL    FMUL    FMUL    FMUL    FMUL    FMUL    FMUL    FMUL
d03 .ALIGN fill (machinecode)      0009    0009    0009    0009    0009    0009    0900    0900    0900    0900
d03 inter-object gap (lnk)         0000    0000    0000    0000    0000    0000    0000    0000    0000    0000
```
(d03 shows the two bytes in memory order: `00 09` vs `09 00`.)

Discriminating evidence, version by version:

* **5.0r26 / 5.0r28** (identical to each other in every test): `if (x == 2) return 1; return 0;` compiles to
  `BF/RTS/MOV #1` instead of `MOVT R0` (t01); pool loads scheduled differently (t02 SetSub/SetB6);
  spill of a parameter uses `ADD #-4,R15; MOV.L R4,@R15` instead of the pre-decrement push in the JSR delay slot (t06 CallSeq).
* **5.0r31 / 5.0r32** (identical to each other in every test): `Check` (8C01B436) keeps the float distance on the
  stack instead of FR15; the cross-jumped switch in `SetColor` (8C02E820) is not produced.
* **5.1r01**: emits an if-chain for a sparse `switch` in case-value order (0,1,2,3,4) where the original keeps
  source order (1,0,2,3,4) (t02 Check); different register choice in inline 12-byte struct copies (t03).
* **5.1r03 / 5.1r04 / 5.1r08**: identical C code generation in all real-function tests. Unique to this group:
  `(int)(unsigned short)x >> 8` is compiled as a *signed* shift (`EXTU.W; CMP/PZ; BF; BRA; SHLR8; NOT; SHLR8; NOT`)
  although the operand is known non-negative (d01). 1ST_READ.BIN contains 54 `EXTU.x Rn` + `CMP/PZ Rn`
  sequences of this kind (e.g. 8C01494A, 8C02685C, 8C0673E8); t04 IsStage1 reproduces one exactly.
* **5.1r11 / 5.1r13**: drop that signed-shift sequence (t04 IsStage1 differs); additionally differ from r08 in
  FP instruction scheduling (fuzzing).
* **5.1r03 vs 5.1r04/5.1r08**: with `-code=machinecode`, 5.0r26 .. 5.1r03 write `.ALIGN` padding inside the
  P section as the big-endian NOP `00 09` even for a little-endian target; 5.1r04 and later write `09 00`
  (d03). Every one of the 3909 alignment-padding runs in the game code (`0x8C010000`-`0x8C10C000`) is `00 09 ...`
  and ends on a 32-byte boundary. t04 (`0x8C0673E0`-`0x8C067480`) is byte identical with 5.1r03 and differs from
  r04/r08 exactly in its 3 padding halfwords. No assembler (`asmsh` of any version, all tested) produces `00 09`.
  Codegen differences r03 vs r04 also exist but are rare and context dependent (FP register allocation when two
  FMAC chains feed one FCMP, e.g. `if (a*a + c <= b*k + (d + (float)i))`; r04 inserts an extra FMOV copy).
  Random-program fuzzing (650 files, 3 flag sets) found **no** codegen difference between 5.1r04 and 5.1r08.

## Flag evidence

| Flag | Evidence in 1ST_READ.BIN |
|---|---|
| `-cpu=sh4 -endian=little` | SH-4 opcodes (FTRC/FIPR/MOVCA etc.), little-endian image |
| `-fpu=single` | no FCNVSD/FCNVDS and no FPSCR save/restore in compiled code (only 6 FSCHG, in asm). Without it any `double` (e.g. an unsuffixed `0.5`) produces FPSCR.PR switching |
| `-round=zero` (default) | literals are truncated: `0x3DCCCCCC` (0.1) x142, `0x3DCCCCCD` x0; `0x3F199999` (0.6) x33 vs `0x3F19999A` x0; `0x3E999999` (0.3) x66 vs x0. t07 differs by one halfword with `-round=nearest` |
| `-optimize=1` | everything; `-optimize=0` adds redundant RTS/NOP etc. |
| `-speed` | 12-byte struct assignments are expanded inline as `MOV.L` load/store pairs (t03 F/G, e.g. 8C01B53A, 8C01B586); without `-speed` SHC calls the runtime block-move routine (R1=dst, R2=src, R0=size); the game has only 9 such calls (0x8C10CDDC/0x8C10CD38, sizes 48-96 bytes, e.g. 8C02A380 `MOV #88,R0`), i.e. only large blocks. t04 byte match fails without `-speed`. `-size`/`-nospeed` behave like no `-speed` |
| `-macsave=0` | 336 MUL.L + 347 MULU.W + 99 MULS.W in game code but only 3 `STS.L MACL,@-R15` (macsave=1 would save MACL in every function using MUL) |
| `-extra=a=1800` | hidden option, bitmask. Bit `0x800`: P section `ALIGN=32` + `.ALIGN 32` before every literal pool (and after a mid-function pool). Bit `0x1000`: 16-bit constants are built with `MOV #imm,Rn; SHLL8 Rn; [ADD #imm,Rn]` instead of `MOV.W @(disp,PC)` pool loads -- game code has 1176 MOV/SHLL8 pairs and only 249 `MOV.W @(disp,PC)` (220 of them in the non-standard region 8C027360-8C02D980). Bit `0x1` (changes pool-load scheduling) and bit `0x400` (inserts NOP after `MOV Rm,R0`) are NOT set: matches fail with them. `-extra=a=800` gives identical code on the tested functions but would use MOV.W constants |
| `-division=cpu` (default) | integer division calls `__divls` (0x8C10CA5C) / `__modls` (0x8C10CBB8) / `__divlu` (0x8C10CC80) with dividend in R1, divisor in R0 (the DIV1-loop CPU routines). `-division=peripheral` would call `__divlsp` |
| no `-aggressive=2` | `FDIV FR4,FR2` by the pool constant 8.0f at 0x8C0640D0; with `-aggressive=2` SHC turns every division by a constant into FMUL by the reciprocal (d02) |
| `-code=machinecode` | `00 09` alignment fill (see above) |

## Layout and padding conventions

* Within a translation unit (one `.c` file = one object) functions are packed with 2-byte alignment;
  there is no padding between functions. Empty functions are `RTS; NOP` (e.g. 8C067456).
* SHC keeps one literal pool for the whole file and dumps it after the last function, or earlier
  (after an RTS/BRA/JMP, possibly in the middle of a function) when the 1020-byte MOV.L range would be exceeded.
  With `-extra=a=1800` every pool is preceded by `.ALIGN 32`, and a pool dumped in the middle of the file is
  also followed by `.ALIGN 32`, so the next code starts on a 32-byte boundary. Hence a 32-byte boundary after
  a pool is **not** necessarily a file boundary.
  Exception: the pool flushed right after a `BRAF` jump-table dispatch (switch) is only 4-byte aligned
  (`.RES.W` slot, no 32-byte alignment), immediately followed by the `.DATA.W` offset table
  (e.g. 8C01B5F8 BRAF, 8C01B5FC pool entry, 8C01B600 table).
* `.ALIGN` padding bytes are `00 09 00 09 ...` (big-endian NOP; quirk of the SHC 5.0r26-5.1r03 `-code=machinecode` object writer).
* Pools hold `.DATA.L` entries in first-use order; float constants are loaded with `MOVA` + `FMOV.S @R0`.
* Every object's P section is `ALIGN=32`; lnk fills the gap between the end of one object (end of its last
  pool, 4-byte aligned) and the next object with `00` bytes. **File boundaries are therefore visible as runs of
  2..28 zero bytes after a pool, ending on a 32-byte boundary**: 196 such regions in 0x8C010000-0x8C111000
  (a lower bound: a file whose last pool ends exactly on a 32-byte boundary leaves no gap). rof2bin
  (`SYSROF-BINARY TRANSLATOR Ver. 2.51 (C) SEGA`, `-v=filldata`) does not change these gaps; lnk writes them.
* 184 of the 196 zero-delimited regions contain the `00 09` 32-byte pool padding (compiled with the flags above).
  The 12 regions without it are assembly or differently-built modules and must be handled separately:
  `8C010000-8C010180` (startup), `8C010320-8C010400`, `8C027360-8C02CE80`, `8C02CE80-8C02D980`,
  `8C02E720-8C02E820`, `8C036F60-8C03FDC0` (contains hand-written asm, e.g. 8C037618 uses R8/R9 unsaved),
  `8C053320-8C053940`, `8C057AE0-8C057BC0`, `8C099A00-8C099DA0`, `8C09E760-8C09F080`,
  `8C10C0E0-8C111000` (SHC runtime library: `__divls` 8C10CA5C, `__modls` 8C10CBB8, `__divlu` 8C10CC80,
  block-move helpers 8C10CD38/8C10CDDC; plus other library objects).
  and `8C10B9A0-8C10C0E0`, which lacks 32-byte pool alignment and uses MOV.W constants but shows the
  5.1r03-r08 signed-shift idiom (8C10BCBC, 8C10BCDC, 8C10BD3A): same compiler family without `-extra=a=1800`
  (probably a separately built library object).

## Library code (0x8C5F8000+)

* Ninja core routines are hand-written assembly (e.g. 0x8C639C70 / 0x8C639B98, probably njPushMatrix / njPopMatrix, use
  FSCHG pair moves and MOVCA.L); no compiler can be identified for them.
* Some library objects contain 32-byte-aligned pools with `00 09` fill, i.e. C compiled by an SHC <= 5.1r03 with
  `-code=machinecode` and `-extra` bit 0x800: regions 8C5FF5A2-8C5FF9A0, 8C6411EA-8C643480,
  8C650B10-8C65A1E0, 8C66126C-8C6669C0 (526 `00 09` runs in the library area, 511 ending on 32-byte boundaries).
* The rest of the library uses MOV.W pool constants (2929 `MOV.W @(disp,PC)` vs 4 MOV/SHLL8 pairs), i.e. it was
  built with different flags. Library objects are prebuilt SDK binaries and should be kept as binary/asm.

## Notes for the decomp build

* Compile C with `tools/shc/5.1r03/bin/shc.exe` and the flags above, **directly to an object**
  (`-code=machinecode`). Going through `-code=asmcode` + asmsh produces identical instructions but `09 00`
  alignment fill; if an asm path is needed (e.g. SA2-style `.INCLUDE` of non-matching functions), the padding
  must be fixed up afterwards (e.g. post-link: rewrite the `.ALIGN` gaps listed in the map/listing to `00 09`).
* Link with lnk (P sections `ALIGN=32`, zero gap fill). Object order follows the zero-gap boundaries.
* Environment quirks: SHC needs `SHC_LIB=<bin dir>` and `SHC_TMP=<dir>`; `SHC_TMP` must be short (use the 8.3
  name) or SHC aborts with `3321 (F) Illegal environment variable`. Source/object names containing `-` or more
  than one `.` fail (`3305 (F) Invalid command parameter` / asmsh `CANNOT OPEN FILE`); run with a clean cwd.
* Language: SHC 5.x is C89 (no `//` comments in 5.0, no `long long`, auto aggregate initializers must be constant).
* Float literals: decimal-to-float conversion truncates (`-round=zero`) through an inexact intermediate, so write
  the shortest decimal slightly *above* the wanted value (e.g. `0x3C23D708` -> `0.0099999986f`,
  `0x38FF5C28` -> `0.000121765135f`; `0.0999999940395355224609375f` gives `0x3DCCCCCB`, use `0.1f`).
  5.1r01 rejects long literals with `2406 (E) Out of float`.
* Matching idioms seen so far: `if (c) return 1; return 0;` (not `return c;`) gives `MOVT; RTS; NOP`;
  calls to functions defined earlier in the same file become `BSR`, tail calls to them `BRA`;
  struct/array element addresses of global arrays (e.g. `playertwp[0]`) get CSE'd into stack slots across calls;
  a stack slot holding a *local* variable is allocated with `ADD #-n,R15` in the prologue, a spilled *parameter*
  is pushed with `MOV.L Rn,@-R15`.

## Test cases (tools/tests/compiler_probe/)

| File | Original address(es) | Status with 5.1r03 + flags |
|---|---|---|
| `t01_gamemode_getters.c` | isMode2 8C01B35E, isMode3 8C01B36C, getVar 8C01B37A, clrVar 8C01B380 | match |
| `t02_gamestat.c` | SetSub 8C01B3E0, GetSub 8C01B406, GetB6 8C01B40E, SetB6 8C01B416, Check 8C01B436 | match (Check needs >= 5.1r03) |
| `t03_vec_copy.c` | F 8C01B500, G 8C01B554 | match (needs -speed, >= 5.1r03) |
| `t04_stage_tu_bytematch.c` | IsStage1 8C0673E0, GetDir 8C067414, Dummy 8C067456; TU tail 8C0673E0-8C067480 | **byte-exact match, 5.1r03 only** |
| `t05_misc_leaf.c` | Find 8C011042, InRange 8C01B818, SetB7 8C01B636 | match |
| `t06_calls.c` | CallSeq 8C036F12, Start 8C01B640 | match |
| `t07_setcolor_tu.c` | SetColor 8C02E820, Disp2 8C02E8DA (match); Disp1 8C02E886 (near miss); pool 8C02E940 | SetColor byte-exact; pool + 00 09 padding byte-exact |
| `t08_near_misses.c` | IsOddSub 8C01B5E0 (switch jump table), A 8C034D0A, Voice4 8C015798 | near misses (source-level) |
| `d01_ushort_shift.c` | discriminator (signed-shift idiom) | 5.1r03/r04/r08 only |
| `d02_fdiv_const.c` | discriminator (-aggressive=2) | FDIV kept => no -aggressive=2 |
| `d03_pad_a.c`, `d03_pad_b.c` | two-object link: .ALIGN fill and inter-object gap | 00 09 fill (<= 5.1r03), 00 gap |

Harness: `shcprobe.py` (function mode `py -3.12 shcprobe.py FILE.c -v`; byte mode
`py -3.12 shcprobe.py FILE.c --exact [--range S-E]`), `run_all.py` (everything, all versions, ~30 s).
Externals are named `name_XXXXXXXX` so the harness can resolve them to their addresses.
