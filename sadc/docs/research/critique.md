# Research-phase critique: contradictions, wrong claims, gaps and risks (G-SA1)

Date: 2026-10-06. Role: completeness critic for the six research reports in this folder:
`g64.md`, `libsm64.md`, `ecosystem.md`, `gmod.md`, `sa1_code_map.md` (+ `.csv`), `sa1_assets.md` (+ `.csv`).

Method. I read all six reports in full, then spot-checked their load-bearing claims against:

* our binary and analysis outputs: `baserom/1ST_READ.BIN`, `asm/1st_read/**`, `asm/overlays/**`,
  `build/1ST_READ.xrefs.json`, `build/ghidra/1ST_READ/decomp/*.c`, `include/*.h`, `src/1st_read/*.c`,
  `docs/compiler.md`, and the user's GDI (`track03.bin`, ISO9660 read directly);
* the cloned sources in the session scratchpad `repos/` (ckosmic `libsm64-gmod`, `libsm64-ckosmic`,
  upstream `libsm64`, `g64`, `Facepunch_gmod-module-base-dev`, `eco/X-Hax_sa_tools`,
  `eco/sonicfreak1337_SARecomp`), and the cached GMod wiki/news pages in `scratchpad/wiki`,
  `april-2026-update.txt` and `sept2026.txt`.

No third-party code is reproduced here. Licences are as recorded in the source reports (G64 repos:
no licence file; libsm64: CC0; gmod-module-base: no licence file; SA Tools: no licence file; SARecomp:
no licence file). Statements marked **(verified)** were checked in this pass; **(inferred)** are my
reading of asm or Ghidra output and still need confirmation by decompilation.

---

## 1. Verdict in one screen

1. The reports are thorough and mostly consistent on the architecture: a C library built from the
   decomp, a separate C++ GMod client module, a Lua-only Workshop addon, runtime ripping from the user's
   disc, a host-supplied collision index, a fixed tick, and owner-authoritative networking with puppets.
2. **Three factual errors matter for implementation** (all verified, section 3):
   * `sa1_assets.md` says the physics block is copied to `playerwk+0x64`. The asm copies it to
     **`playerwk+0x9C`**. `+0x64` holds a pointer to a second, separately `CAlloc(1,0x84)`'d block.
     `sa1_code_map.md` is right.
   * `libsm64.md` says 1ST_READ "never" uses `fsca`/`fsrra`. It does: **119 FSCA and 70 FSRRA**
     encodings (1ST_READ text 64 + 51, overlays 55 + 19), emitted as raw `.DATA.W`. They sit in
     `njSin`/`njCos`/`njRotate*`/`njUnitVector`, and FSRRA also appears inline in collision geometry
     (`8C02D060`) and motion evaluation (`8C03FDC0`, `8C040A60`). `ecosystem.md` is right.
   * `g64.md` and `gmod.md` say G64's `(-x, z, y)` axis map "mirrors" or "flips handedness". Its
     determinant is +1, so it is a proper rotation. G64 reverses triangle order for a winding-convention
     reason, not handedness. `sa1_assets.md` is right.
3. **One open question shared by four reports is now answered (verified in asm).** SA1 runs gameplay
   as **fixed 60 Hz logic steps**. The frame wrapper `func_8C04E95A` calls the per-step update
   `func_8C04E714` N times per displayed frame. N comes from `B_8C754EC4`, the step index is
   `B_8C754EC8`, and extra catch-up steps are added. See section 4.1.
4. **Welds mutate shared data (verified in Ghidra output).** `PJoinVertexes` (`func_8C07A55C`) MAllocs
   the weld "org" buffers and **stores their pointers back into the shared `PL_JOIN_VERTEX` table** in
   1ST_READ data. It also writes the depth bytes there and rewrites model vertex arrays every frame.
   Multiple instances of one character therefore need per-instance copies of the model and the weld
   table. Nobody flagged this.
5. **The module's Lua API is an attack surface.** No report covered this. Any server's clientside Lua
   can call every function the module exports. G64 itself has a path traversal in `LoadMapCache`
   (verified) and reads any path given to `GlobalInit`. The SA1 design as proposed (`sa1.LoadDisc(path)`,
   PNG/WAV caches in `data/`, Lua-writable map caches parsed in C++) repeats those patterns.
6. **Legal and distribution analysis is thin everywhere.** "No game data in the binary" covers assets
   only. The DLL is still compiled from decompiled SEGA code. Matched C also compiles game *data
   tables* in as initialisers, and SDK code (Ninja/Kamui/Shinobi, CRI, Hitachi runtime) has its own
   owners.
7. **C portability hazards are under-specified.** Examples: `Sint32` is `signed long` (64-bit on
   linux64); `TaskWkUnion` contains `void*`; pwp is stored inside a float vector; `-fpu=single` makes
   `double` single on DC but not on PC (matched C already contains `sc[i] * 0.05`); integer division by
   zero traps on x86 but not on SH-4.
8. **Scale is unresolved.** Five different recommendations appear: 3.5-4.5, 4-5, 4.0, 5-7 and 6-7 HU
   per SA1 unit, with two different Sonic heights (9.7 and 10.1).
9. **Collision fidelity against Source maps is the least-evidenced assumption.** The land collision
   consumes Ninja *strips* and uses five fixed-capacity polygon buffers (verified strings). Its
   step-up behaviour on Source stairs is unknown.
10. **Riskiest assumption overall: decompilation throughput.** Running Sonic needs about 574
    functions (about 174 KB of SH-4) in PC-compilable C, and about 1,082 for all characters. Only
    about 21 functions are decompiled today. The six mode machines are 10-19 KB each. Section 6.

---

## 2. Contradictions between reports

| # | Topic | Report A says | Report B says | Resolution (evidence) | Action |
|---|---|---|---|---|---|
| C1 | Physics copy offset in `playerwk` | sa1_assets 7.1: copied to **+0x64** | sa1_code_map 3.1: copied to **+0x9C**; +0x64 is a second `CAlloc(1,0x84)` | **code_map right.** `func_8C06C350`: dest = pwp + `EXTU.B(-100)` = 0x9C, size `EXTU.B(-124)` = 0x84, copied by `func_8C10CDDC`; then `CAlloc(1,0x84)` result stored at pwp+0x64 (`MOV #100`) | Fix sa1_assets 7.1 |
| C2 | FSCA/FSRRA in 1ST_READ | libsm64 0 and 10.11: never used, trig is table/software, "tables can be ripped" | ecosystem 3.6: 119 FSCA, 70 FSRRA as raw `.DATA.W` | **ecosystem right.** Grep of `asm/1st_read/text`: FSCA `H'F[even]FD` 64, FSRRA `H'Fn7D` 51; overlays 55 and 19 | Fix libsm64 10.11 and remove "rip trig tables" |
| C3 | G64 axis map | g64 3: "re-wound because the X negation flips handedness"; gmod 7.3: "G64 mirrors instead" | sa1_assets 4.1: `(-x, z, y)` "is a proper rotation, no mirroring" | **assets right.** det [[-1,0,0],[0,0,1],[0,1,0]] = +1. G64 `main.cpp` lines 427-429 use `(-x, z, y)` *and* swap v1/v3. The swap reflects Source's triangle winding, not handedness | For SA1, determine winding per geometry source and per the SA1 collision code (section 5.8) |
| C4 | Scale (HU per SA1 unit) | g64 8.4: 4-5; libsm64 10.11: 3.5-4.5 ("Sonic 36-40 HU tall"); gmod 7.3: **4.0** | ecosystem 6: 6-7; sa1_assets 11.8: 5-7 | Not a fact question. Sonic height: gmod 10.1 (bind pose) vs assets 9.7 (action 0, frame 0). Physics `height` = 10 | One decision (section 7, D1) |
| C5 | Welds vs GPU skinning | gmod 4.2: node-local vertices, no inverse bind, 1-2 bone weights, `DrawSkinned` | libsm64 10.7 and assets 11.6: welds make meshes non-rigid, triangle soup is the safe default | **Both partly right.** Type-2 welds (dst := src) are exact as a *re-bind* of the dst vertex to the src node with the src local coordinates (1 bone). Type-1 "average" welds (Big 4, Eggman 10) blend two *different* local positions, which a node-local single-position vertex cannot express. They need model-space bind positions with inverse-bind matrices, or the CPU path | Section 7, D3 |
| C6 | Writing decoded assets to disk | libsm64 10.3: nothing ripped is ever written by the library; g64 8.5/10.3 asks whether a cache is acceptable | gmod 0/4.3/6.2: PNG in `data/sa1/tex`, WAV in `data/sa1/bgm` (recommended) | Policy conflict. New fact: `data/` is readable by any server's clientside Lua (section 5.2) | Section 7, D4 |
| C7 | Possession model | g64 8.6: copy G64's SENT + `drive` + `OBS_MODE_CHASE` + physbox | gmod 5.1: the player entity *is* the character, via SetupMove/Move/FinishMove; "do not use the drive system" | gmod's design is untested in GMod (its own open question 5). G64's is proven | Prototype both early; keep G64's as fallback |
| C8 | Audio output | g64 8.6: decode and mix in-module (miniaudio/WASAPI); ecosystem 1.6: in-module output was "the practical choice because GMod Lua cannot stream raw PCM" | gmod 6.2: no module-owned device; `sound.Generate` + `EmitSound` for SFX, WAV + `sound.PlayFile` for music | Wiki (cached `sound.Generate.txt`): accepts a **string of 16-bit samples**, loopStart since 2024-06-04. So SFX do not need a device. ecosystem's rationale is outdated for SFX | Prefer gmod's path for SFX. Music path depends on D4 |
| C9 | PVR formats to support | g64 8.2 and libsm64 10.1/11: include palettised (PVP) | ecosystem 4.3 and assets 6.3: disc census finds only 3 pixel formats x 7 layouts, no palettes, YUV, bump or stride | **Census right** (two independent scans agree) | Drop palettised from v1 scope |
| C10 | Disc validation granularity | ecosystem 6.1: check 3-track layout, track03 size 1,185,760,800, SHA-256 of track03 or files | gmod, assets, libsm64: SHA-1 of `1ST_READ.BIN` | Track-level checks reject valid dumps in other containers (Redump cue/bin, CHD) with identical files | Validate per file (section 5.10) |
| C11 | ckosmic fork audio thread | ecosystem 1.2/1.3: "in-library **SDL** audio thread" | g64 3.3: pthread pushing to WASAPI | **g64 right.** `libsm64-ckosmic/src/libsm64.c`: `pthread_create(audio_thread)`, backends tried WASAPI, then Pulse/ALSA, with SDL as a fallback | Minor wording fix |
| C12 | `find_water_level` constant | libsm64 2.7: -10000 | g64 6.3: -100000 | Both right for different forks (upstream returns -10000; ckosmic returns -100000) | None |
| C13 | G64 default scale | libsm64 5.7: `sm64 = gmod * 2.0` | g64 4.3: 2.5 | Both exist: the C default is 2.0, but the Lua ConVar `g64_scale_factor` defaults to "2.5" and overrides it | None |
| C14 | Data after the physics table | ecosystem 2.4.1: "ends at 0x8C1BE404; zeros follow" | code_map 3.1: `D_8C1BE404` is a per-level death-zone pointer table | Bytes at file 0x1AE404: one zero word, then 0x0CAB0F58-style pointers. **code_map right** | Minor |
| C15 | Who owns "player root A/B" | ecosystem 7 Q1: unknown | code_map 2.1: E-102 main `8C0CBE00` and mode machine `8C0CD0A8` | Answered by code_map | Close ecosystem Q1 |
| C16 | 8th caller of PInitialize | libsm64 10.14: probably the per-character mains | code_map lists 7 characters | `func_8C107020` loads PVM 'EGGROB' (E-100 Zero), has no static callers and is referenced from EV overlays ev0013/0061/0063/0064/006f/0071. It is a cutscene actor, not a playable character | Note in code map |
| C17 | evalflag 0x20 | ecosystem 4.4: "ZYX rotation order" | libsm64/assets: `NJD_EVAL_ZXY_ANG` | SA Tools `Enums.cs` uses both the friendly name "RotateZYX" and the Ninja name `NJD_EVAL_ZXY_ANG`. Never set on character trees (assets) | Use the Ninja name |
| C18 | Mode counts | ecosystem 2.4.3: `MD_SONIC` 0x58 (88) | code_map 2.1: Sonic switch 0..0x53 (84) | SADX enum vs DC switch; not a contradiction, but use DC numbers | None |
| C19 | UV divisor | libsm64 10.7: Sint16/256, "verify" | assets 4.1: 255 = 1.0, verified by rendering | assets right | None |
| C20 | MLT file count | ecosystem: 123 | gmod/code_map: 122 | Trivial | None |

---

## 3. Spot checks against source and binary

### 3.1 Verified correct

* `1ST_READ.BIN` SHA-1 `e74c849806db103aa87f84e41a382873bc96b6df`, 6,735,488 bytes, ISO9660 extent
  **LBA 545711** (read from `track03.bin`: PVD at LBA 45016, `CD001`).
* GDI: 3 tracks `1 0 4 2352 track01.bin 0`, `2 14714 0 2352 track02.raw 0`, `3 45000 4 2352 track03.bin 0`;
  track03 is 1,185,760,800 bytes (504,150 raw sectors).
* Physics table: 8 x 0x84 bytes from `0x8C1BDFE4` (file 0x1ADFE4). Dumped values match the table in
  ecosystem/gmod/assets (Sonic rad 4 / height 10 / weight 0.08 / jmp_y 1.66; Big lim_h 5, rad 9.5).
  `center_height` 5.4 is stored as 5.3999996, a truncated literal.
* `func_8C06C350` (PInitialize): copies 0x84 bytes to pwp+0x9C, stores pwp at `*(mwp+0)`, then
  `func_8C04B440(twp)`, `CAlloc(1,0x84)` at pwp+0x64. mtnjvwk is at pwp+0x120 (spdp at +0x138 =
  pwp+0x38, workp at +0x13C = pwp; action list at +0x140; weld list at +0x144). It stores
  `rad * 0.2` at pwp+0x1FC using literal `0x3E4CCCCC`: 0.2 **truncated** (round-to-nearest would give
  `0x3E4CCCCD`).
* Function sizes: `8C0A966C` is 19,016 bytes (0x4A48, largest); 4,103 game functions below `8C111194`
  out of 6,473.
* FMAC 3,134; FIPR 184; FTRV 152 in 1ST_READ text (agrees with ecosystem and libsm64).
* Facepunch `gmod-module-base` (development): `lua_State` pad 48+22 = 70 (Win32/Linux32) and
  92+22 = 114 (Win64); `LUA_FUNCTION` calls `LUA->SetState(L)`; CMake `set_gmod_suffix_prefix` hardcodes
  `gmsv_`.
* GMod wiki (cached): `IMesh:DrawSkinned` and `mesh.BoneData` added 2026-02-25 (up to 52 matrices,
  2 weight slots); April 2026 notes list both; September 2026 notes ship 64-bit Windows client and server
  on every branch via Steam launch options. `sound.Generate` accepts a string of 16-bit samples.
* G64: ConVar `g64_scale_factor` default "2.5" (FCVAR_CHEAT, FCVAR_REPLICATED); C default 2.0;
  axis map `(-x*S, z*S, y*S)` with v3/v2/v1 order.
* SARecomp "release slots 2, logical delta 2" in its docs is consistent with section 4.1.

### 3.2 Wrong (see C1, C2, C3, C11, C14)

### 3.3 Plausible but not proven by the cited evidence

* **"Katana runs with FPSCR 0x00040001" (ecosystem).** The only explicit load of that constant in
  1ST_READ is inside the SDK routine `func_8C668E60`. It saves FPSCR, loads `0x00040001`, and restores
  FPSCR later. 0x00040001 is also the SH-4 power-on reset value (DN=1, RM=round-to-zero), so the claim
  is very likely true, but it rests on the reset default. An interrupt handler at `8C5FBE30` masks and
  ORs FPSCR. To confirm: check every `LDS Rn,FPSCR` site (12 TUs) for anything that sets RM=nearest
  during gameplay.
* **"Every vertex follows 1 or 2 nodes, which fits hardware skinning" (gmod).** True for type-2 welds
  only if they are encoded as a re-bind (C5). Not true as written for node-local storage with two
  weights.
* **"Tikal as smoke-test character" (code_map).** TIKAL_PROG is 17 functions driven by cutscenes. It
  exercises the shared pipeline, but probably not input-driven paths. Sonic case 1 (walk) may be a
  better first runtime target once task, input and land collision exist.
* **"SA1 land collision can be fed GMod triangles at 8C0522C0" (code_map 4).** The seam exists, but
  the consumer reads Ninja strips into fixed buffers (section 5.8). That is an adapter problem, not a
  simple pointer swap.
* **GMod facts from fetched pages** (singleplayer move hooks do not run clientside; `require` failures
  count toward `sv_kickerrornum`; `util.IsBinaryModuleInstalled` behaviour). These are plausible and
  sourced from the wiki, but untested in-game.

---

## 4. New findings from the spot checks

### 4.1 Logic rate: fixed 60 Hz steps (resolves g64 Q5, libsm64 Q2, ecosystem Q2, gmod Q9, code_map Q5)

`func_8C04E95A` (the gameplay wrapper; the same address in PAL per SARecomp) does the following
**(verified asm, semantics inferred)**:

* It loops on `func_8C04E714`, which is the per-step update. Its callees include the task executor
  `8C0988A4`, collision world `8C028EC2` and CCL `8C0342E0`.
* The loop index is `B_8C754EC8` (zeroed first). The count is `B_8C754EC4` plus extras. Extras come
  from a counter `D_8C19DE2C` that adds one step every 5th frame (every 3rd when the count is 2), only
  while `B_8C754C04 == 1` (plausibly a 50 Hz/PAL flag), and from a time check: `func_8C06C1B2` reads
  the SH-4 TMU counter at `0xFFD80018` and adds a step when the frame ran long.
* Between steps it calls `func_8C0517F6` (pad refresh) and ORs the "pressed" bits of `per[0]` and
  `per[1]` (`B_8C754DA8` + 0x10) across steps, writing the union back after the loop.

Consequences for the port:

* The decompiled logic is a **pure fixed-step 60 Hz simulation**, so an accumulator is the faithful
  model. One port tick should equal one `func_8C04E714` restricted to the library's own task tree.
* `B_8C754EC8` (the step index within a display frame) is read by about 90 TUs in 1ST_READ text,
  including Sonic's face and effects (`8C0B2338`, `8C0B33A0`, `8C0B4F00`, `8C0B58E0`), Knuckles' effects
  and E-102 weapons, and by hundreds of overlay files. Display-rate work is gated on step 0. **Set
  `B_8C754EC4 = 1` and `B_8C754EC8 = 0` on every port tick.** That reproduces DC 60 fps semantics
  regardless of the GMod frame rate. `B_8C754EC4` is also read by `8C051760` (input), `8C089F20`
  (counters/time), `8C0BAFC0` (Tails stand selector) and `8C04D920`.
* Input edges: when GMod runs several ticks in one frame, deliver a button-press edge to exactly one
  tick. The original re-reads the pad between steps.

### 4.2 Welds write into shared tables and models

From `build/ghidra/1ST_READ/decomp/8C07A55C.c` **(verified pseudo-C, details to confirm by
decompiling)**:

* On first use (`mtnjvwk.jvmode` at pwp+0x122 == 0), for each `PL_JOIN_VERTEX` of type 1-3 it
  `MAlloc(numVertex * 0x30)`s an "org" buffer and stores the pointer at entry+0x10 **inside the weld
  table in 1ST_READ data**. It copies the source and destination points and normals into the buffer,
  and writes node-depth bytes at +0x0E/+0x0F of the same table. For types 4-9 it sets org=NULL and
  writes a depth byte.
* Every tick it evaluates node matrices itself (`8C0583A0` into the buffer at `8C67C640`, then
  `8C058434`, `njSetMatrix` `8C63A770`, `njInvertMatrix` `8C6390B0`). It does not depend on the
  previous frame's draw. It rewrites model point and normal arrays and computes hand, foot and user
  positions.

Consequences:

* Every character instance (local players, puppets, AI) needs its **own copy of the deserialised
  NJS_MODEL vertex/normal arrays touched by welds and its own copy of the weld table**. On DC only one
  instance of a character ever existed, so the shared mutation was harmless.
* Puppets must also run `PJoinVertexes`. Hand and foot positions (types 4-9) feed held items and
  effects, so welds are gameplay-relevant, not just cosmetic. This answers libsm64 Q4 (welds run in
  exec).
* A GPU-skinned renderer that bypasses the game's weld code must reproduce type 1/2 behaviour itself
  (C5).

### 4.3 Concrete 64-bit and portability hazards already in our headers and C

* `include/common.h`: `typedef signed long Sint32;` (also `Uint32`, `Bool`, and `Angle` through
  `Sint32`). On **linux64 (LP64)** `long` is 64-bit, which changes every struct layout and the wrap
  behaviour of BAMS arithmetic. Win64 (LLP64) is unaffected. The PC profile must map these to
  `int32_t`/`uint32_t`.
* `include/task.h`: `TaskWkUnion { Sint8 b[4]; Sint16 w[2]; Sint32 l; Float f; void *ptr; }`. On x64
  the union becomes 8 bytes, which shifts every `taskwk` field after `counter` (+0x08). Any code that
  stores a pointer in `counter`/`timer`/`value` and reads it back as `l` truncates.
* PInitialize stores the `playerwk*` at `mwp+0`, inside `motionwk.work` (an `NJS_POINT3` of floats),
  as `*(playerwk**)mwp`. On x64 the pointer overlaps `work.x` and `work.y`.
* `playerwk` is allocated with a literal `CAlloc(1, 0x200)`. On x64 it holds more pointers (mtnjvwk
  spdp/workp/plactptr/pljvptr/actwkptr, `csts*` and so on), so the size must become `sizeof(playerwk)`.
* `PL_JOIN_VERTEX.org` is filled at run time (4.2), so the native struct needs a real pointer field.
* `-fpu=single` (`docs/compiler.md`) makes SHC evaluate `double` in single precision.
  `src/1st_read/code_8C0869E0.c:251` already contains `_builtin_fabsf(sc[i] * 0.05)` with the comment
  "double constant". On PC that multiply runs in double and rounds once at the end, which gives a
  different result. MSVC has no switch to make `double` single precision. Rule for the PC profile:
  no `double` and no unsuffixed float literals in decompiled code; literals go through a bit-exact
  macro; SHC builtins (`_builtin_fabsf` and the rest) get PC shims.
* `-division=cpu`: SH-4 integer division (DIV0S/DIV1 sequences and `__divls`) **does not trap** on
  zero, but x86 `idiv` raises #DE and would **crash GMod**. The PC build needs checked division
  helpers.

### 4.4 Land collision consumes strips through fixed-capacity buffers (verified strings)

`8C0273A2`, `8C0274B8`, `8C0275E0` and `8C028666` warn "STRIP buf is FULL". `8C028BFE` warns
"NEAR-poly is FULL". `8C028EC2` (collision world) prints "colbuf:", "obj:", "polMAX:", "r-p:" and warns
"OBJECT buf is FULL" and "RANGE-poly buf is FULL". `8C029B00` warns "TOUCH-poly buf is FULL".

So the original pipeline expects Ninja basic-model strips grouped into COL objects, and it silently
drops polygons past the buffer capacities. GMod displacements (up to 512 triangles per power-4 face),
dense props and BSP fans can exceed capacities sized for SA1 stages. Section 5.8.

### 4.5 G64 shows the module attack surface (verified)

* `LoadMapCache(filename, version)` builds the path as `cwd + "\garrysmod\data\" + filename` with no
  sanitising, so `..\` sequences reach any file. The result is parsed and pushed into Lua tables:
  version check, int16 bounds, float vertices.
* `GlobalInit(path)` reads any path it is given and passes it to `sm64_global_init`, which copies
  8 MiB regardless of size (g64.md 2.2).
* Both are callable by clientside Lua sent by **any** server the user joins. The ROM path ConVar is a
  plain client ConVar.
---

## 5. Topics nobody covered, or covered too thinly

### 5.1 Legal and distribution constraints

Only one-line mentions exist (gmod Q13; ecosystem 5; libsm64 8; g64 7.9). What is missing:

* **Code is not data.** The rule "no SEGA data in the binary" protects assets. The GMod DLL is still
  a compiled translation of SEGA's program, which is the same position libsm64 and G64 occupy for
  Nintendo's code. That position rests on rights-holder tolerance, not on a licence. The project
  owner must choose a release model, ranked by exposure:
  1. Source-only: users build the library themselves. mario64-in-minecraft makes users build
     `sm64.dll` (ecosystem 1.3).
  2. A prebuilt DLL containing decompiled game code only, with SDK parts reimplemented.
  3. A prebuilt DLL that also contains decompiled SDK code. Avoid this.
* **Matched C compiles game data tables in.** To be byte-identical, the matching build defines
  initialised data (per-mode tables, voice table `D_8C1BE7A0`, collision-shape params `D_8C1BE84C`,
  CCL infos, start positions) as C initialisers. The PC profile must replace every initialised table in
  the data range `8C111194-8C5FBB60` with a runtime rip by address (`extern` + loader) behind
  `TARGET_PC`. Add a build check that fails if any non-trivial initialised array from game data ends up
  in the PC binary. Immediate literals in code are unavoidable.
* **SDK and middleware owners differ.** Ninja/Kamui/Shinobi/`sd` (SEGA Katana SDK), CRI ADX/ADXT/MW
  (CRI Middleware, still a commercial product), and the SHC runtime (`__divls`, `__quick_mvn`, the libm
  wrappers; Hitachi/Renesas). Reimplement from documentation rather than decompiling and shipping them
  (libsm64 Q1 raises this; nobody decided).
* **The FSCA table.** dream-recomp's bit-exact FSCA uses Flycast's hardware-captured table, which is
  GPL-2. Shipping it in a non-GPL module is not possible. Either capture our own from hardware,
  characterise the hardware algorithm, or accept a computed table with a tolerance.
* **Trademarks and the Workshop.** Addon names, icons and descriptions using SEGA marks invite
  takedowns. ecosystem 2.1 cites search snippets about removed Sonic items (unverified). Use neutral
  naming and original art.
* **Test fixtures are SEGA data.** Golden traces, RAM dumps and decoded assets used for validation
  (section 5.9) must be generated locally from the user's disc and git-ignored, never committed.
  Public CI cannot run disc-dependent tests.
* **Disk caches enlarge the exposure** (C6 and 5.2): decoded PNG/WAV in `data/` can be harvested by any
  server.
* Our own licence: glue, tools and loaders can carry an explicit licence (G64 had none). The decompiled
  game code cannot be meaningfully licensed by us. State that in a PROVENANCE file, as
  sonic-adventure-2-rust does.

### 5.2 Threat model: server-supplied Lua and untrusted inputs

GMod runs every server's clientside Lua in the same client state as the addon. That Lua can call every
`sa1.*` function, can `file.Read`/`file.Write` under `data/`, and can change unprotected client ConVars.

Requirements nobody listed:

* **Path handling.** No filesystem path from Lua may reach `fopen`.
  * The disc path is chosen through the module's native dialog and stored in module-owned config
    outside `data/`, not in a Lua-settable ConVar. gmod.md proposes `sa1.LoadDisc(path)` and an archived
    `sa1_disc_path` ConVar; change that.
  * Reject UNC, `\\?\` and device paths. On Windows a server-supplied `\\host\share\x.gdi` would make
    the client authenticate to that host over SMB and leak an NTLM hash.
  * Track names inside the `.gdi` must be bare filenames resolved in the `.gdi`'s own directory.
* **No raw bytes to Lua.** Never expose arbitrary-offset reads, raw disc files or decoded assets
  beyond what rendering and audio strictly need. Anything handed to Lua (PNG bytes, PCM strings)
  can be harvested by any server; keeping textures native (the SDK `ITextureRegenerator` path) avoids
  this at the cost of SDK coupling.
* **Untrusted parsers.** Treat as hostile input:
  * server-supplied BSP maps, including LZMA lumps;
  * MDL/PHY files;
  * map caches in `data/`, which any server's Lua can write;
  * GDI, ISO9660, PRS and PVR data;
  * batched binary strings from Lua (`ObjUpdateBatch`).
  Bounds-check everything, cap allocations, and fuzz the parsers (libFuzzer/AFL) before release. Keep
  the BVH cache outside `data/` (module-owned directory) or authenticate it.
* **Crash containment.** A fault crashes GMod. On Windows, wrap `tick` in SEH to disable the character
  on an access violation rather than kill the game (optional, debatable), and run ASan/UBSan builds in
  testing.

### 5.3 Floating point and C semantics (beyond literals and rounding mode)

The reports cover literal truncation, RZ/FTZ/DAZ, FMAC policy and an FSCA table. Additionally:

* **FSRRA matters for collision and motion, not just Ninja.** It appears inline in `8C02D060`
  (collision geometry, P0), `8C03FDC0` and `8C040A60` (motion evaluation, P0), and in
  `njUnitVector 8C63A94C`. Its hardware approximation is undocumented.
* **FSCA is reached only through Ninja in player code.** No inline FSCA or FSRRA appears in player
  common `8C06C0C0-8C07DEC0` or the character range `8C0A77A0-8C0EEAA0`. The land-collision range has
  one FSRRA site (`8C02D060`). A single `njSin`/`njCos`/`njRotate*` implementation therefore controls
  the trig fidelity of the player code.
* Taken together, **bit-exact physics is not currently achievable**: FSCA and FSRRA hardware tables
  are unknown, and FIPR/FTRV/FMAC precision is unknown. Define the acceptance target now: "bit-exact
  where possible, tolerance elsewhere" with numeric thresholds, and make the validation harness
  tolerance-aware.
* **win32 target.** MSVC and GCC must use SSE2 scalar math (`/arch:SSE2`, `-mfpmath=sse`). x87's
  80-bit intermediates break single-precision fidelity. libsm64 and ecosystem mention win32 builds but
  not this.
* **MXCSR hygiene.** Save and restore the *exact* MXCSR around every tick. Do not call back into
  Lua/engine while RZ/FTZ/DAZ is set.
* **libm.** Use reimplementations of the game's own wrappers (`atan2f 8C10D0F8` and the acos/asin-like
  wrappers at `8C10D008..8C10D198`), not the CRT. MSVC CRT and glibc differ, so win64 and linux64
  clients would otherwise diverge.
* **Float-to-int.** SH-4 `FTRC` saturates out-of-range values; x86 `cvttss2si` returns 0x80000000.
  Wrap conversions used for BAMS angles in a helper.
* **Undefined behaviour in decompiled C.** Signed wrap (BAMS arithmetic) needs `-fwrapv` or unsigned
  helpers. Type punning needs `-fno-strict-aliasing`. Uninitialised reads that happen to work on SHC
  need warnings and sanitizers. Disable FP contraction (`-ffp-contract=off`, `/fp:strict`); never
  `-ffast-math` or `-march=native`.

### 5.4 Timing: 60 Hz steps against GMod frames and ticks

Section 4.1 settles the simulation side. Points missing from gmod.md 8.3:

* Drive the accumulator from `SysTime()`/`RealFrameTime()`, not `CurTime()`, which is server-synced
  and jumps. Cap catch-up at 4-5 steps per frame; SA1 itself adds bounded extra steps.
* `host_timescale`: G64 divides its tick interval by it. Decide whether the SA1 tick follows it, and
  whether slow-motion keeps 60 Hz steps.
* Server tick (66.67 Hz by default) and state packets (30 Hz) are fine. However, the owner's client
  sees **props and brush movers `cl_interp` (default 0.1 s) in the past**, so riding and pushing
  decisions differ from the server's physics. Collide against the client's rendered positions, and
  make server validation tolerant of that lag.

### 5.5 Multiplayer determinism and instance isolation

* With owner authority, cross-client determinism is not required (all reports agree). Same-binary
  determinism is still valuable for tests and replays; keep the FP flags in 5.3.
* `rand` (`8C10D334`, LCG, 149 callers) is global state. Make it per instance; puppets must not
  consume it.
* GMod servers have up to 128 players, so a client may show far more than 8 characters. The native
  8-slot arrays (`8C78C608/628/648/668`) cannot host every puppet. The puppet path needs its own
  lightweight instance that still provides the `playerwk`/mtnjvwk fields `PJoinVertexes` reads (4.2).
* Player-0 assumptions: sadx-multiplayer needed about 100 patches (ecosystem 2.2). The loader itself
  writes slot 0 (code_map 2). Every decompiled P0 function needs an explicit audit of `playertwp[0]`
  and `playerpwp[0]` style accesses.

### 5.6 Camera

Covered only as "GMod owns the camera; stub about 12 functions". Missing:

* `func_8C05117E` (analog conversion, `atan2f`) needs camera state. Its exact inputs, and what
  `8C01AD2C`/`8C01AE60` (called 5 and 8 times from the core) return, decide whether a yaw-only shim is
  faithful.
* SA1's high-speed feel depends on its default follow camera and on how input direction is held when
  the camera swings. A mouse-orbit chase camera (G64) makes high-speed steering much harder. Evaluate
  porting only the non-scripted default camera mode from `8C019360-8C025960`.
* The homing target lists (`B_8C73EE38`/`8C742288`) are shared with the camera (code_map 2.1); check
  for camera-dependent target choice.

### 5.7 32-bit data and code on 64-bit hosts

ecosystem 3.7 recommends option C: native structs for the library, guest image only in a harness.
Add the concrete hazards in 4.3, plus:

* Find data tables in P0 that hold **code pointers**: object or mode dispatch tables, and CCL info
  callbacks if any. They need an address-to-function remap like ckosmic's 13-entry geo callback table.
* Decide a rule for decompiled code that uses raw offsets (matching often encourages `*(T*)(p+0x9C)`
  forms). The PC profile requires typed field access everywhere. That adds a constraint to the
  matching work and can slow it down.

### 5.8 Collision fidelity against Source geometry

* **Input form.** The land collision reads strips into STRIP buffers (4.4). Either synthesise
  Ninja basic models (strip or triangle meshsets) per spatial chunk so the original gather runs
  (libsm64 10.6 option 1), or rewrite the gather. Size chunks so a query can never overflow
  NEAR/RANGE/TOUCH capacities. Read the capacities from `8C028EC2`, `8C028BFE` and `8C029B00`, and
  consider enlarging them in the PC build (a deliberate behaviour change).
* **Stairs and step-up.** Source stairs are typically 8-18 HU risers that the Source player steps up
  automatically. At S=4 that is 2-4.5 SA1 units, against Sonic's radius 4 and centre height 5.4. SA1's
  maximum step-up is unknown. If it is lower, characters are blocked by ordinary stairs. Mitigations:
  * use playerclip ramps where maps have them;
  * synthesise ramps over detected stair patterns (the Minecraft libsm64 port turns stairs into
    45-degree ramps);
  * choose S using the measured step limit.
  Measure this early: it is a go/no-go for "feel".
* **Internal and buried faces.** Rebuilding brush solids from `dbrush`/`dbrushside` (gmod.md's "best
  fidelity" route) and physics convexes both produce faces inside solid space and coplanar seams. Drop
  faces not adjacent to empty space and merge coplanar overlaps before indexing.
* **Winding and normals.** Determine whether SA1's collision tests use stored normals or vertex order
  (C3). Normalise each GMod source (convexes, brush sides, displacements, `.phy`) to that convention.
* **Very large polygons.** BSP faces span thousands of units. SA1 polygons are small and grouped
  under bounding spheres. Tessellate or chunk large faces if any code assumes small polygons.
* **Attributes.** Map NoClimb conservatively (otherwise Knuckles can climb glass and props), map water
  from CONTENTS_WATER brush faces as Water-flag polygons, and map Hurt from `trigger_hurt` volumes if
  wanted.
* **Character size.** At S=4, Big's radius 9.5 becomes 38 HU (76 HU wide against the 32 HU player hull)
  and Gamma is 84 HU tall. They will not fit many Source doors and vents. Accept that, or allow a
  per-character visual scale while keeping one physics scale.

### 5.9 Validating a PC build against the original

ecosystem 3.7 mentions an interpreter harness in one line. A concrete ladder:

1. **Matching is the strongest proof.** A matched function compiled for PC differs from the original
   only through compiler, UB and FP-environment differences. Prefer matching for P0 physics and
   collision functions where feasible, and track "matched / non-matching / missing" coverage over the
   P0 call graph.
2. **Function-level differential tests** for non-matching functions:
   * run the original routine from the user's `1ST_READ.BIN` in an SH-4 interpreter with full FPU
     semantics (FMAC, FIPR, FTRV, FSCA, FSRRA, RZ, DN);
   * run the PC-compiled C on the same inputs;
   * compare outputs and touched memory, with tolerances (5.3).
   Fast path: a **32-bit Linux (i386) harness can `mmap` the DC RAM image at 0x8C000000 (and alias the
   0x0C000000 mirror)**. Decompiled C built with `-m32` and DC layouts then runs in place on real game
   data with identical pointer values, so memory diffs are trivial. This is not possible on 32-bit
   Windows by default (addresses above 2 GB).
3. **Scenario traces.** Record per-step `taskwk`/`motionwk`/`playerwk` state plus pad input from a
   reference run, either an emulator with input recording or our interpreter running the whole game.
   Replay the inputs into libsa1 with SA1's *own* stage collision, which needs a landtable deserialiser
   in the test harness, and compare per step. No attract-mode demo input files were found on the disc
   by name.
4. **GMod integration tests.** Scripted input on fixed maps, frame-time budgets and memory budgets.

GPL emulators are acceptable as test-only tools. Everything derived from the game stays local (5.1).

### 5.10 Dump formats and versions

* Validate at the file level: `1ST_READ.BIN` SHA-1 plus per-file hashes for each PRS/MLT used. That is
  container-independent (C10).
* Containers:
  * TOSEC `.gdi` with 2352- and 2048-byte sectors;
  * Redump cue/bin, whose track splitting and pregap layout differ, so derive positions from the TOC
    rather than assuming file offset 0 = LBA 45000 (verify against a real Redump set);
  * **CHD** (libchdr, BSD-3), now very common;
  * DiscJuggler `.cdi`: usually self-boot rebuilds with a scrambled or patched `1ST_READ`; expect
    rejection.
  gmod and assets defer CDI/CHD; decide v1 scope explicitly.
* Versions: only US 1.005 is accepted. Users with JP, US 1.004, INT or PAL builds get nothing.
  Supporting them needs per-version data manifests (SA Tools configs, assets 2). The compiled logic
  stays US 1.005, so mixing it with another build's data is a fidelity question.

### 5.11 Smaller gaps

* Kill planes: SA1 uses per-level death-zone tables (`8C07BAC0` + `D_8C1BE404`). GMod needs a
  map-bounds kill plane plus `trigger_hurt` handling.
* Spawning: does SA1 init need a floor under the spawn point (libsm64 returns -1)? What happens when
  the host BVH is not ready yet?
* Water physics location is unknown (code_map Q4). Super Sonic's code path is unknown (assets Q6).
* Upgrades (light-speed shoes, crystal ring and so on) are event flags tested in PInitialize
  (`func_8C09068E`), so they become per-instance settings applied *before* init.
* Performance budgets for tick, BVH query, puppets and Lua marshalling were not estimated.

---

## 6. Riskiest assumptions (ranked), with mitigations and early tests

| Rank | Assumption | Why risky | Mitigation / early kill test |
|---|---|---|---|
| R1 | Enough of the P0 call graph can be decompiled into working PC C in reasonable time | Sonic-only needs about 574 functions / 174 KB; all characters about 1,082 / 398 KB; 21 done; six mode machines of 10-19 KB; matching the big switches is hard (code_map 8) | Track call-graph coverage, not match %. Allow NON_MATCHING C for PC. **Consider an SH-4 interpreter as scaffolding:** run not-yet-decompiled functions from the *user's own* `1ST_READ.BIN` behind HLE seams (Ninja, landtable gather, sound, camera). The port runs early, ships no SEGA code for those parts, and the same interpreter is the test oracle (5.9). This complements the user's goal of decompiling everything; it does not replace it |
| R2 | SA1 land collision on Source geometry feels right without a rewrite | Strip-based input, fixed buffers, unknown step-up, internal faces, scale (5.8) | Early test: Sonic on gm_construct stairs, ramps and displacements with the synthesised-COL adapter; measure the step limit and buffer peaks |
| R3 | One C source can be both byte-matching on SHC and pointer-clean on x64 | Offset idioms, unions with pointers, pointer-in-float fields, initialised data tables, `double` semantics (4.3, 5.1) | Define the `TARGET_PC` conventions before mass decompilation. Add a CI job compiling every decompiled file for x64 with `-Wpointer-to-int-cast -Wshorten-64-to-32` and a "no game tables" check |
| R4 | Distributing a compiled decomp DLL is tolerated | No licence; trademark exposure; SDK/CRI code (5.1) | Decide the release model first (source-only or prebuilt). Reimplement SDK pieces |
| R5 | "Faithful physics" is reachable on x86 | Unknown FSCA/FSRRA/FMAC/FIPR hardware behaviour (5.3) | Define tolerance-based acceptance; build the harness first |
| R6 | Client-authoritative movement through SetupMove/Move/FinishMove works without prediction jitter | Untested (gmod Q5) | Prototype in week 1 with a dummy mover at 3,840 HU/s; fall back to G64's drive + chase |
| R7 | `DrawSkinned` covers rendering | New (April 2026) API; 32-bit availability unknown; type-1 welds (C5) | Default to CPU triangle soup; add GPU skinning as an optimisation for type-2-only characters |
| R8 | Shims stay small | sadx-multiplayer evidence of pervasive player-0 assumptions; the camera dependency (5.6) | Audit per function; keep a shim ledger with call counts |
| R9 | Users have US 1.005 in a supported container | Many dumps are Redump/CHD or other regions (5.10) | File-level validation; CHD in v1 if cheap; clear messaging |

---

## 7. Decisions to make before implementation

| ID | Decision | Recommendation from this critique |
|---|---|---|
| D1 | Scale S (HU per SA1 unit) | One replicated ConVar. Start at **4.0**: Sonic is about 40 HU tall and child-sized, radius 16 equals the player half-width, run is 334 HU/s. Re-decide after the stair/step test (5.8). Do not use different physics scales per character |
| D2 | Axis map | Any proper rotation works (gmod's `(x, -z, y)` or assets' `(-x, z, y)`). Pick one after reading the display code's yaw convention (SADX uses `0x8000 - ang.y`; assets says models face -X). Normalise collision winding per source separately |
| D3 | Rendering path | CPU-evaluated triangle soup by default, which keeps the game's weld code authoritative. Optional GPU path: re-bind type-2 weld vertices to the source node; type-1 needs model-space bind with inverse-bind matrices, or CPU |
| D4 | Disk caches of decoded assets | Default to in-memory only. If PNG/WAV caching in `data/` is kept for robustness (it survives device resets), document that any server can read it, and offer a ConVar to disable it |
| D5 | Possession model | Prototype gmod's player-is-character; keep G64's SENT + drive + chase as fallback |
| D6 | Audio | SFX and voices through `sound.Generate` + `EmitSound` (3D, volume, pause). Music through WAV + `sound.PlayFile`, or an in-module device if D4 forbids WAV caches |
| D7 | Validation target | "Matched where possible; tolerance-checked elsewhere" with numbers; interpreter harness before the first non-matching P0 function lands |
| D8 | Release model | Source-only vs prebuilt (5.1); SDK reimplemented in both cases |
| D9 | Dump and version scope for v1 | US 1.005; GDI + extracted folder; CHD if libchdr integration is cheap; file-level hashes |
| D10 | Tick mapping | One port tick = one `func_8C04E714`-equivalent step for the library's task tree, with `B_8C754EC4 = 1` and `B_8C754EC8 = 0`; accumulator on `SysTime`; at most 4-5 steps per frame |

---

## 8. Corrections to apply to the research docs and reference data

1. `sa1_assets.md` 7.1: the physics copy goes to **pwp+0x9C**; pwp+0x64 is a pointer to a separate
   0x84-byte block.
2. `libsm64.md` 0 and 10.11: FSCA and FSRRA are used (119/70 encodings including overlays). Delete
   "tables can be ripped instead of embedded".
3. `g64.md` 3 and `gmod.md` 7.3: G64's map is a proper rotation; the triangle reversal is a winding
   convention.
4. `g64.md` 8.2: "other characters' blocks are listed in the same ini" points at the wrong SA Tools
   labels. Use `0x8C1BDFE4 + 0x84*id`.
5. `g64.md` 8.2 and `libsm64.md` 10.1/10.3/11: drop palettised PVR from scope.
6. `ecosystem.md` 1.2/1.3/1.6: ckosmic audio is a pthread with WASAPI first (SDL is a fallback). GMod
   can create sounds from PCM strings via `sound.Generate`.
7. `ecosystem.md` 2.4.1: pointer data (the death-zone table) follows `0x8C1BE404`, not zeros.
8. `ecosystem.md` 7 Q1: root A/B are E-102's main and mode machine.
9. `libsm64.md` 10.14: `func_8C107020` is E-100 Zero ('EGGROB', EV overlays).
10. `ecosystem.md` 6.1: replace track-level size and SHA-256 requirements with file-level hashes.
11. `gmod.md` 4.2: node-local vertex storage is incompatible with 2-weight blending; see C5.
12. All reports: replace "60 Hz nominal, verify" with the section 4.1 result.
13. Still pending (all reports agree; nobody may edit them in this phase): fix the 7 wrong physics
    labels in `config/symbols.1st_read.txt` and `reference/dc_sa1_data_labels.csv` (0x84 stride from
    `0x8C1BDFE4`). Also relabel `D_8C1BE404` as the death-zone table.

---

## 9. Open questions

1. What is SA1's maximum step-up height, and how does it behave on 90-degree ledges? This decides
   stair handling and possibly S.
2. What do `func_8C05117E` and the 12 core-called camera functions read and return? Is a yaw-only shim
   faithful, or should the default follow camera be ported?
3. FSCA/FSRRA: can hardware tables be obtained under a licence we can ship? Is FMAC fused on SH-4?
   What numeric tolerance defines "faithful"?
4. Release model: source-only, or prebuilt DLL? Who signs off on the legal posture?
5. Are decoded-asset caches in `data/` acceptable, given server-side harvesting?
6. What are the capacities of the STRIP, NEAR, OBJECT, RANGE and TOUCH buffers? Should the PC build
   enlarge them?
7. Which side of each weld pair is written (assets says destination), and which gameplay code reads
   the type 4-9 hand/foot/user positions?
8. Which data tables in P0 contain code pointers, and do they need a remap table?
9. What is `B_8C754C04` (PAL/50 Hz flag?), and what is the exact time threshold behind the extra step
   in `func_8C04E95A`?
10. How many player-0 assumptions does P0 contain, and how many affect a library with puppets and more
    than 8 characters?
11. Does `IMesh:DrawSkinned` exist on the 32-bit main-branch client?
12. Does SetupMove/Move/FinishMove-driven client authority hold up under prediction at Sonic speeds?
13. Which dump containers and versions are in v1 scope? How common is CHD among likely users?
14. Does the 0xC0-byte PAL/US difference at `0x8C055794..0x8C055F40` (angle utils and MSetPosition
    region) change behaviour relevant to any planned use of SARecomp's PAL documentation?
15. Is there any recorded-input demo data in 1ST_READ or the overlays that could serve as free
    end-to-end test vectors?

---

## Appendix: evidence pointers

* PInitialize: `asm/1st_read/text/8C06C300.src` (`func_8C06C350`), physics source `_Sonic_Physics_Data`,
  copy helper `func_8C10CDDC`, CAlloc `func_8C098B0C`.
* Tick wrapper: `asm/1st_read/text/8C04D920.src` (`func_8C04E95A`, `func_8C04E714`); timer helper
  `func_8C06C1B2` in `8C06C0C0.src` (literal `0xFFD80018`).
* Weld code: `build/ghidra/1ST_READ/decomp/8C07A55C.c`.
* FSCA/FSRRA: `grep -E "H'F[02468ACE]FD|H'F[0-9A-F]7D"` over `asm/1st_read/text` and `asm/overlays`.
* FPSCR constant: `asm/1st_read/text/8C668B38.src` (`func_8C668E60`, literal `H'00040001`).
* Collision buffer strings: `build/1ST_READ.xrefs.json` entries `8C0273A2`, `8C028BFE`, `8C028EC2`,
  `8C029B00`.
* E-100 Zero init: `asm/1st_read/text/8C107020.src` (string 'EGGROB').
* Portability: `include/common.h` (Sint32 = long), `include/task.h` (TaskWkUnion),
  `src/1st_read/code_8C0869E0.c:251`, `docs/compiler.md` lines 11, 110-111, 171.
* G64: `repos/libsm64-gmod/g64/main.cpp` (`LoadMapCache` lines 185-215, `GlobalInit` 307-322,
  axis map 427-429); `repos/g64/lua/autorun/g64_convars.lua` line 8; `repos/libsm64-ckosmic/src/libsm64.c`
  lines 132-163; `surface_collision.c` `find_water_level` in both forks.
* GMod: `scratchpad/wiki/IMesh_DrawSkinned.txt`, `mesh.BoneData.txt`, `sound.Generate.txt`,
  `april-2026-update.txt`, `sept2026.txt`; `repos/Facepunch_gmod-module-base-dev/include/GarrysMod/Lua/Interface.h`,
  `include/CMakeLists.txt`.