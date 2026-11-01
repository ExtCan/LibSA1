# SA1 DC code map: engine, physics and characters (for a libsm64-style "libsa1")

Target: Sonic Adventure (Dreamcast, US 1.005), `baserom/1ST_READ.BIN` at `0x8C010000` plus the
PRS overlays. Goal: decide exactly which code to decompile so that the characters, their physics
and the engine pieces they depend on can be compiled for PC as a library (G64 / libsm64 style),
with all assets ripped at runtime from the user's own GDI.

Machine-readable companion: `docs/research/sa1_code_map.csv` (one row per module, columns
`start,end,subsystem,module_guess,evidence,confidence,functions_count`; `module_guess` starts with
a priority tag `[P0]`/`[P1]`/`[P2]`/`[S]`/`[X]` and the binary name, see section 9).

Everything below was derived from this repository's own analysis outputs:
`build/1ST_READ.xrefs.json` (callers, callees, pool constants, strings), `build/1ST_READ.manifest.json`
(code chunks), `build/1ST_READ.tus.json` (recovered TU starts), `build/ghidra/1ST_READ/decomp/*.c`,
`reference/*.csv`, `config/symbols.1st_read.txt`, `reference/strings_1st_read.txt`, the overlay
`build/<OVL>.xrefs.json`, and direct reads of `baserom/1ST_READ.BIN` and `disc/SONICADV/*`.
Function addresses are the `func_XXXXXXXX` names of the current split. "nc" = number of callers.
SADX names are used only as *analogies* (structure/callers/constants match), never as proof.

Confidence: **high** = direct evidence (strings, SA Tools labels, call from a known function,
unambiguous asm); **medium** = strong structural match; **low** = educated guess.

---------------------------------------------------------------------------------------------------

## 1. Big picture

| Range | What |
|---|---|
| `8C010000-8C111194` | game code (`P` sections, link order = source order). 4103 functions, ~950 KB code |
| `8C111194-8C5FBB60` | game const + init data, same object order. Characters' models/motions/tables live here |
| `8C5FBB60-8C66C2A8` | SDK library code (Shinobi, Ninja, Kamui, sd/ADX/mwSnd) |
| BSS `8C67C680-` | globals (player arrays, input, collision buffers, ...) |

The game code is ordered like the original source tree and is *grouped by subsystem*:

```
8C010000  startup, VMU save, lantern lighting, animals
8C019360  CAMERA (~185 funcs)
8C027360  LAND COLLISION, geometry, debug colli objects
8C02E820  CCL object collision
8C036BC0  model renderer (SH4 asm) + MOTION evaluation (Ninja-style)
8C042E20  game over, barriers, sprite effects, enemy common, EVENT system
8C04C680  game mode / stage manager / player loading
8C0509A0  INPUT, video, LANDTABLE manager, main loop
8C055D20  angle/vector utils + MSetPosition, matrix capture, object helpers
8C059A80  common stage objects (springs, dash panels, item boxes, ...), path-work objects
8C06C0C0  ======== PLAYER COMMON (physics, motion, welds, player API) 8C06C0C0-8C07DEC0 ========
8C07DEC0  debug texture viewer, race AI, Tails AI, vs-boss AI, RING
8C088120  progress flags, PATH library, counters + HUD, title, story, SET manager
8C091C20  SHADOWS, SOUND, TASK SYSTEM + heap, texture lists, file loaders, SPR particles
8C0A0220  debug menus/editors, emblem
8C0A77A0  ======== CHARACTERS: Sonic, Tails, Knuckles, E-102, Amy, Big (+fishing, Froggy), Eggman ========
8C0EEAA0  Chaos effects, ENEMIES, E-100 Zero, BGM/voice, vibration, VMU/movie, libc/libm
```

Player-related code is therefore **contiguous**: player common `8C06C0C0-8C07DEC0` (205 functions,
70 KB) and characters `8C0A77A0-8C0EEAA0` (~550 functions). The data region mirrors this order:
the character models/action lists sit in the same order as their code (section 7).

---------------------------------------------------------------------------------------------------

## 2. Character architecture (common to all characters)

Every character is one SHC translation unit (one `.c` file) for its "core" plus usually a second TU
for effects. All of them follow the same skeleton (verified in the Ghidra pseudo-C of each main):

* **Loader**: `func_8C04F3C0` (game-mode TU) creates the player task with
  `CreateElementalTask(7, 1, <main>)` = `func_8C09852E(7,1,main)` for slot 0, picking the main by
  `CurrentCharacter` (`B_8C7493C8`): 0 Sonic, 2 Tails, 3 Knuckles, 5 Amy, 6 E-102, 7 Big
  (1 Eggman, 4 Tikal are created elsewhere). For `CurrentLevel` (`B_8C7493BA`) 0x24/0x25 (Sky Chase)
  it uses `0x0C9015C0` in the SHOOTING overlay instead. It writes `twp->counter.b[1]` (+9) = character
  id, `+8` = player number, stores `B_8C78C608[0]=tp->twp`, `B_8C78C628[0]=tp->mwp`, then calls
  `func_8C04F8FA` (initial position from `Sonic_Start_Array` etc.). Slot 1 gets the Tails AI
  (`func_8C081AC0`) or a race opponent (`func_8C07F560`).
* **Main exec** `XxxMain(task *tp)`: `twp = tp+0x20`, `mwp = tp+0x24`, `pwp = *(playerwk**)mwp`.
  1. calls the character's **mode machine** (input-driven state transitions; SADX analogue
     `Sonic_Act1` / `*CheckInput` family) unless frozen;
  2. `switch (twp->mode)` (byte at `twp+0`): case 0 = init, then one case per action/mode that runs
     the shared physics pipeline (section 3) and mode-specific code;
  3. motion update `PSetMotion(&pwp->mj)` (`func_8C079476`), weld update `PJoinVertexes`
     (`func_8C07A55C`), CCL entry (`func_8C02F63C`), shadow/aux updates.
* **case 0 init** (all mains): `PInitialize(pno, tp)` = `func_8C06C350`; `pwp+0x140 =` the
  character's action list (SA Tools `XxxActionList`); `CCL_Init(tp, <colli info>, n, id)` =
  `func_8C02F4B0`; weld list into `pwp+0x144`; children via `CreateElementalTask` (face/eyes,
  jiggle, ...); `tp->dest (+0x18)` and `tp->disp (+0x14)` set.
* **Forced-action handler** ("NAct"): switch on `twp->smode` (`twp+1`) for commands posted by
  objects through the player API (`SetInputP` = `func_8C07C65E` sets `twp->flag|=0x1000` and
  `twp->smode`). Sonic `func_8C0AFF1A` / `func_8C0B07E4`, Knuckles `func_8C0C5BCA` / `func_8C0C61F4`.
* **Display** draws the body with the motion system (`func_8C040942`), the fast model renderer
  (`func_8C038CEC`, `func_8C038D00`), attached parts (`func_8C04B57C`), shadow (`func_8C09247E`),
  texture list (`njSetTexture` `8C608CCC`).
* **Destructor** calls the player free helper `func_8C06C5CA` and texture release `func_8C098FFC`.
* **Stand/idle animation selector** (slope vs. gravity via `njInnerProduct`, idle timer
  `pwp+0xE > 1800` -> idle action + voice `func_8C07DCE0`): Sonic `func_8C0AE3D6`,
  Tails `func_8C0BAFC0`, Knuckles `func_8C0C4F48`, E-102 `func_8C0D1E38`, Amy `func_8C0DA6E8`,
  Big `func_8C0E3C20`.
* Per-character rotation helper wrapping `func_8C07C298`: Sonic `8C0AFE72`, Tails `8C0BBD3C`,
  Knuckles `8C0C5B3A`, E-102 `8C0D06F4`, Amy `8C0DB67E`, Big `8C0E43CA`, Eggman `8C0EE8B6`.

### 2.1 Per-character table (all in 1ST_READ unless noted)

| Char | Code range (TU starts) | Main exec | Mode machine | Display / Destruct | Modes (switch size) | Data (SA Tools labels) |
|---|---|---|---|---|---|---|
| Sonic | core `8C0A77A0-8C0B33A0` (TU 8C0A77A0), effects `8C0B33A0-8C0B6120` | `8C0A77BC` (0x1E1C) | `8C0A966C` (0x4A48, largest function in the game) | `8C0AE17A` / `8C0AE0E0` | 0..0x53 (84) | `SonicActionList` 8C1C55A0, CCL info 8C1C5E80, `SonicWeldList` 8C1C5FA4, `texlist_SONIC`, `texlist_SON_EFF` |
| Tails | core `8C0B6120-8C0BFE00`, effects `8C0BFE00-8C0C0640` | `8C0B6148` | `8C0B70F4` (0x3644) | `8C0BA820` / `8C0BA738` | ~62 cases | `TailsActionList` 8C256D6C, CCL 8C2575AC, `TailsWeldList`, `texlist_MILES` |
| Knuckles | core `8C0C0640-8C0C8F20`, emerald hunt `8C0C8F20-8C0CA040` (same TU), effects `8C0CA040-8C0CBE00` | `8C0C0640` | `8C0C193A` (0x2BD0) | `8C0C46AC` / `8C0C4690` | 0..0x35 (54) | `KnucklesActionList` 8C3528CC, CCL 8C352FBC, `KnucklesWeldList`, `texlist_KNUCKLES`, `texlist_KNU_EFF` |
| E-102 | core `8C0CBE00-8C0D3C00` (TUs 8C0CBE00, 8C0D2600, 8C0D26E0, 8C0D32A0), weapons/HUD `8C0D3C00-8C0D6B40` | `8C0CBE00` | `8C0CD0A8` (0x29C6); aim/input `8C0D317E` | `8C0CFACE` / `8C0CFA6E` | 0..0x37 (56) | `GammaActionList` 8C4225D4, CCL 8C422B80, `texlist_E102*` |
| Amy | core `8C0D6B40-8C0DEEA0`, effects `8C0DEEA0-8C0DFFA0` | `8C0D6B40` | `8C0D7960` (0x27A6) | `8C0DA122` / `8C0DA106` | 0..0x31 (50) | `AmyActionList` 8C4BB1D0, CCL 8C4BB7F0, `texlist_AMY(_EFF)`, Birdie models |
| Big | core `8C0DFFA0-8C0E6620`, fishing `8C0E6620-8C0EC6E0`, Froggy `8C0EC6E0-8C0ED880` | `8C0DFFA0` | `8C0E0E70` (0x29FE) | `8C0E3A40` / `8C0E386E` | ~52 cases | `BigActionList` 8C525564, CCL 8C525BB4, `texlist_BIG`, `texlist_FISHING`, `texlist_BIG_KAERU` |
| Eggman | `8C0ED880-8C0EEAA0` | `8C0ED880` | `8C0EDD46` | `8C0EE43C` / `8C0EE420` | ~18 cases | `EggmanActionList` 8C57008C. Created only by EV* cutscene overlays |
| Tikal | overlay **TIKAL_PROG.PRS** (load `0x0CB00000`, 17 funcs, 0x13F4 bytes) | `0x0CB00000` | (inside main) | destruct `0x0CB00780` | small | model/weld/motions in the same overlay (SA Tools `TIKAL_PROG.ini`) |
| Super Sonic | Sonic's 1ST_READ code; the `SUPERSONIC` texture name exists only in **B_CHAOS7** | - | - | - | - | see open questions |
| Tornado (Sky Chase) | overlay **SHOOTING** `0x0C9015C0` (uses `PInitialize`) | `0x0C9015C0` | | destruct `0x0C9019A8` | | not needed |

Children / helpers per character (high/medium confidence):

* **Sonic**: face/eye child `8C0B2338` (refs `sonic_face_s_head`, `Sonic_EV_Head`) and `8C0B2376`;
  quill jiggle `8C0B27EE/8C0B2890/8C0B28EE`; foot morph `8C0B1DE0, 8C0B1FD8, 8C0B2022, 8C0B2120`
  (uses `Sonic_*_Foot_Morph_*` models); upgrade welds `8C0B2170` (Light Speed Shoes, Crystal Ring
  models); **homing attack**: target search `8C0AF5A0` (walks the CCL "attackable" list
  `B_8C73EE38`/`B_8C742288`, double-buffered, also used by the camera) and steering `8C0AF280`;
  **light speed dash** `8C0AF90A` (walks the ring list `B_8C741280`/`B_8C7446D0`, calls PGetSpeed
  `8C072174`); light speed attack `8C0AF6C4`; board task init `8C0B2EE6` (exec `8C0B2F46`, display `lbl_8C0B3068`; created by Sand Hill's SBOARD and Ice Cap's STG08).
  Spin-dash/jump/roll are modes inside `8C0A966C` and the 84-case main (not separate functions).
  Effects TU: jump-ball `8C0B4436/8C0B4600/8C0B4724` (`Sonic_Jumpball_Effect`), spin trail
  `8C0B48E0..8C0B4B20` (`Sonic_Effect_Trail`), other effect tasks `8C0B504A..8C0B5B80` (role not pinned),
  ice `8C0B5D60..8C0B60B8` (`Sonic_Ice`, `Sonic_Ice_Particles`).
* **Tails**: flight uses physics `8C06F6C0` (called only by Tails, from `8C0BD8B2`); tail morph
  `8C0BDC80/8C0BDD48` (uses the `Tails_*_Tail_Morph_*` models via `8C056000/8C05646C`); face child `8C0BF76A`; child `8C0BF402` (refs `Tails`,
  `Tails_Flight`, `miles_head_z`); board task init / exec / display `8C0BF7CE/8C0BF804/8C0BF92C`; tail-attack /
  flight trail polygons `8C0BFE00`, `8C0C023E`.
* **Knuckles**: glide physics `8C06F9A2`/`8C06FB04` and climbing collision `8C076782` (called only
  by Knuckles); climb position update `8C0C7D4C` and `8C0C7FB0` (land collision + PSetPosition);
  dig `8C0C8330` and dig-up spawns `8C0C8534` (rings `8C087A9E`, animals `8C015798`, item box
  `8C05D9C0`); punch / Maximum Heat `8C0C7786` (KNU_EFF tasks, vibration); upgrade welds `8C0C4C2E`
  (fighting gloves, shovel claws); face `8C0C8E8E`; emerald hunt (radar, shard placement
  `8C0C905A` called by stage init, `8C0C9BE2`/`8C0C9C9A` called by enemies holding shards).
* **E-102**: beam/laser + targeting `8C0D3C00-8C0D4B00` (`texlist_E102BEAM`, `Laser_Blast`,
  `Targeting_Beam`), lock-on/target registry `8C0D4B00-8C0D5D90` (`8C0D4996`, `8C0D5D80`,
  `8C0D5D90`, `8C0D5DF6` are also called by item boxes/switches/AI), time HUD `8C0D5E80`
  (`texlist_E102TIME`), hit effect `8C0D6960` (`texlist_E102HIT`); extra list `8C04FEA0-8C0509A0`
  only called from E-102.
* **Amy**: hammer (`Amy_Long_Hammer`), Birdie (`Amy_s_Birdie`, functions from `8C0DE540`), face
  `8C0DDA00`, shockwave effect TU `8C0DEEA0-8C0DFFA0` (`Amy_Shockwave_Effect`); the main also calls
  E-100 Zero helpers `8C107700/8C107744/8C10778E`.
* **Big**: fishing `8C0E6620-8C0EC6E0` (rod/line/lure, `Big_Cast_Circle/Cursor`, fishing HUD; 93
  funcs, 23 KB), Froggy `objexec_S_KAERU` `8C0EC6E0`; face `8C0E59C0`.

---------------------------------------------------------------------------------------------------

## 3. Shared player physics ("player common", `8C06C0C0-8C07DEC0`, 4 TUs)

All 7 character modules call into this block (more than 40 functions here are called by every
character). Tikal's overlay main calls the same pipeline. The common per-mode pipeline (Sonic
case 1, also Tikal):

```
8C071700 (turn from analog) -> 8C06D254 PGetAcceleration -> 8C072174 PGetSpeed
  -> 8C072B44 PSetPosition (+8C0730D8 collision core) -> 8C079096 (reset angle/position) -> anim select
```

### 3.1 Key structures / globals (observed)

* `task`: `+0x14 disp`, `+0x18 dest`, `+0x20 twp`, `+0x24 mwp` (matches `docs/matching_tips.md`).
* `taskwk`: `+0 mode`, `+1 smode`, `+2 id`, `+4 flag (u16)`, `+8 player no`, `+9 character id`,
  `+0x14/0x18/0x1C ang x/y/z (int BAMS)`, `+0x20 pos`, `+0x38 cwp (colli work)`, `+0x3C` aux work
  (look-at/head + attached action, allocated by `8C04B440` from PInitialize).
* `playerwk`: allocated in `PInitialize` with `CAlloc(1, 0x200)`; its pointer is stored at `mwp+0`.
  `+0x0E` idle timer, `+0x50` vector compared with gravity, `+0x5C` flags, `+0x64` = second
  `CAlloc(1,0x84)` block, **`+0x9C` = 0x84-byte copy of the physics parameters**, `+0x120` =
  motion work `mtnjvwk` (`+0x124` requested action, `+0x126` current action), `+0x140` action list
  (`PL_ACTION*`), `+0x144` weld list.
* Player arrays (8 slots): `playertwp B_8C78C608`, `playermwp B_8C78C628`, `playertp B_8C78C648`,
  `playerpwp B_8C78C668`; position history index `B_8C78C688`, history buffer `B_8C78C690`
  (`GetPlayerPosition`/`SetPlayerPosition`).
* Gravity vector `B_8C754BDC` (NJS_VECTOR), user-gravity angles `B_8C754BE8/8C754BEC`
  (set by `8C052A8A/8C052AA8`).
* Input: analog angle/magnitude per player `B_8C754DCC/8C754DD0` (stride 8), written by
  `func_8C05117E` (atan2f, camera-relative) and read by `GetAnalog` `8C06CD22`; button state arrays
  `B_8C754C08/8C754C10/8C754C18` with key-assignment masks in `D_8C19E38C/8C19E390/8C19E3A0`;
  `per[]` at `B_8C754DA8`.
* **Physics table**: `D_8C1BDFE4`, **8 entries x 0x84 bytes** indexed by character id
  (0 Sonic, 1 Eggman, 2 Tails, 3 Knuckles, 4 Tikal, 5 Amy, 6 E-102, 7 Big), ending at `8C1BE404`.
  `PInitialize` computes `id*0x84` and copies 0x84 bytes. **The SA Tools labels for every entry
  except Sonic are wrong** (they assume a 0x108 stride: `Eggman_Physics_Data 8C1BE0EC` is really
  Tails, `Tikal_Physics_Data 8C1BE404` is a different table). Verified by dumping the floats
  (Big's max speeds 5/8, Amy's 0.05 field, etc.).
* `D_8C1BE404` = per-level pointer table indexed `[level][act]` (death zones): used by
  `8C07BAC0` (player death-zone handler, with `8C02CA00` ground query) and `8C0C88E6`.
* Other player tables next to physics: `D_8C1BE7A0` (voice table, `8C07DCE0`), `D_8C1BE84C`
  (collision shape params read by `8C0730D8`), `D_8C1BE858/894` (`8C07B694`), `D_8C1BE8D0`.

### 3.2 Player core A (TU `8C06C300`, 30 funcs, 12.7 KB; plus TU `8C06C0C0`)

| Addr | Size | nc | Role (confidence) |
|---|---|---|---|
| 8C06C0C0..8C06C240 | | | player work pool via syMalloc wrapper `8C64133E`; SPR task debug display (TU 8C06C0C0) |
| 8C06C300 | 2C | 1 | clear player arrays (SADX `ClearPlayerArrays` analogue) (high) |
| 8C06C350 | 27A | 8 | **PInitialize(pno, tp)** (high) |
| 8C06C5CA | 96 | 8 | player free/destruct helper (high) |
| 8C06C6A0/8C06C6F8 | | 7 | player sub-task creation (uses `8C07BE6E`) (medium) |
| 8C06C880 | 1B0 | 53 | ridden/held object kind check from `cwp` (returns 0x200/0x400/0x800) (medium) |
| 8C06CA30/8C06CA4E/8C06CB40 | | 6-7 | jump/surface checks (medium) |
| 8C06CD22 | D4 | 57 | **GetAnalog** (angle, magnitude) (high) |
| 8C06CE40 | 7E | 10 | analog vs. facing check (medium) |
| 8C06CEBE/8C06CEFE | 40 | 13/7 | button-pressed checks (jump/action) (medium) |
| 8C06CF60/8C06CFCC | | 15/14 | analog direction checks (medium) |
| 8C06D118/8C06D136 | | 6 | CCL-hit-list checks (medium) |
| **8C06D254** | A4E | 9 | **PGetAcceleration** (+ helpers 8C06DCA2, 8C06E4E0, 8C06EAC0, 8C06EFCE) (medium) |
| 8C06E904 | 188 | 7 | air acceleration (PGetAccelerationAir-like) (low) |
| 8C06F25C / 8C06F386 / 8C06F530 | | 7/6/3 | brake/inertia variants (low) |

### 3.3 Player core B (TU `8C06F6C0`, 48 funcs, 27.6 KB)

| Addr | Size | nc | Role |
|---|---|---|---|
| 8C06F6C0 | 15E | 1 | **flight acceleration** (Tails only) (high) |
| 8C06F9A2 / 8C06FB04 | 162 / 88 | 1 | **glide acceleration / speed** (Knuckles only) (high) |
| 8C06FB8C | 3E4 | 1 | Sonic-only physics variant (low) |
| 8C0700C2, 8C07061C, 8C070814, 8C07095E | | 2 | Sonic+Tails only: the board's accelerations (`8C0700C2` steers relative to the camera `D_8C18B7EC`; board095_lib.md 1.1) |
| 8C070EDA, 8C0711D2, 8C0714B6, 8C071656 | | 3-8 | gravity / inertia / friction family (use gravity `8C754BDC`) (medium) |
| 8C071700 | 17C | 15 | rotation from input (PGetRotation-like) (medium) |
| 8C0718C0, 8C071A20, 8C071B26 | | | slope helpers (medium) |
| 8C071D40 | 98 | 7 | user-gravity related (low) |
| **8C072174** | 222 | 10 | **PGetSpeed** (high-medium) |
| 8C07256A | 26 | 39 | **PClearSpeed(mwp, pwp)** (medium) |
| 8C0725C0, 8C07266E, 8C072806, 8C0728A0 | | 7/7/1/1 | speed variants (push/pull, Knuckles-only) (low) |
| **8C072B44** | 594 | 8 | **PSetPosition** (returns a status code used by the mains) (high-medium) |
| 8C0730D8 | 11FC | 1 | PSetPosition core: calls land collision `8C028BFE`/`8C029B00`, step/wall/floor helpers `8C0742D4..8C0763A0`, `8C0777E0` (high) |
| 8C0742D4 | 4FE | 1 | collision debug draw (`tcrf_CollisionDisplayVar`) |
| 8C075214 | 202 | 2 | reads the physics table (medium) |

### 3.4 Player core C (TU `8C076640`, 119 funcs, 29.5 KB)

| Addr | nc | Role |
|---|---|---|
| 8C076782 (A42) | 1 | **Knuckles climbing collision** (high) |
| 8C0777E0 (B28) | 1 | floor/wall collision step (called by 8C0730D8) (medium) |
| 8C078308, 8C0784F8, 8C078710 | 7/4/6 | movement along paths (call path lib `8C0897F8`) (medium) |
| **8C078E40** | 38 | **PConvertVector_G2P** (rotate by -ang) (high) |
| **8C078EB0** | 72 | **PConvertVector_P2G** (rotate by ang: push-unit-matrix 8C057B40 + njRotate + njCalcVector) (high) |
| 8C079096 | 7 | reset angle/position from ground normal and gravity (medium) |
| **8C079476** | 12 | **PSetMotion(mtnjvwk*)** (15-mode switch, builds NJS_MOTION_LINK via MAlloc) (high) |
| 8C079C20 / 8C07A49C | 1 / 1 | motion link interpolation / free (medium) |
| **8C07A55C** | 8 | **PJoinVertexes** (weld buffers `8C798698..8C798758`, njInvertMatrix, node matrices 8C0583A0/8C058434) (high) |
| 8C07AEF8, 8C07AF8C, 8C07B0F6, 8C07B106 | 5-6 | eyes/face/head helpers (medium) |
| 8C07B320 | 14 | spawns crash/impact effect task 8C044680 (medium) |
| 8C07B346, 8C07B3DA, 8C07B500, 8C07B694 | | splash / ripple / water effects (medium) |
| 8C07BAC0 | 1 | death-zone handler (table 8C1BE404) (medium) |
| 8C07BBC2, 8C07BC0E, 8C07BE6E | | player sub-tasks, upgrade effect (low) |
| **8C07BFFA..8C07D6E0** | | **player API "...P(pno, ...)"**: `SetInputP 8C07C65E`, `RunAlongPathP 8C07C0A0`, `RunWithSeeingPathP 8C07C0E4`, `HangDownFromPathP 8C07C120`, look-at `8C07C2E2/8C07C318`, cancel look `8C07C376`, velocity-like `8C07C580`, `SetRotationP 8C07C9D2`, rotation wrapper `8C07C298`, acceleration-like `8C07CB54`, `SetPositionP 8C07CB6A`, `GetPlayerPosition 8C07CE82`, `SetPlayerPosition 8C07CEF2`, `GetPlayerTaskPointer 8C07CFA0`, `GetPlayerWorkPtr 8C07CFDE`, nearest-other-player `8C07D18E`, barrier/invincibility givers `8C07D3DC/8C07D43E/8C07D4A0` (high-medium) |
| 8C07D7B4 (476) | 7 | angle/posture update used by every character (low) |
| 8C07DC2A, 8C07DCE0 | 4 / 6 | voice selection (`D_8C1BE7A0`, rand) (medium) |

---------------------------------------------------------------------------------------------------

## 4. Collision, paths, water, input

* **Land collision** `8C027360-8C02CE80` (1 TU, 36 funcs, 22 KB): SA1's own player-shape vs.
  polygon collision. Builds strip/near/object/range/touch polygon buffers (overflow warnings in
  the strings), `8C028BFE` (NEAR-poly list, 10 callers), `8C029B00` (0xB50, 12 callers, the main
  sphere/ray vs. polygon routine), `8C02770E`, `8C028EC2`, `8C029400`, `8C02A680`; vertical
  ground/shadow queries `8C02B620/8C02BC60/8C02C100/8C02CA00`. Its input list comes from the
  **landtable manager** `8C0519C0-8C052FA0` (`8C0522C0` = ListGroundForCollision-like, 19 callers;
  count/pointer `B_8C754EF0/8C754EF4`, entries at `B_8C757EF4`). **This is the replacement point for
  GMod world geometry** (libsm64 equivalent: static surface load): feed BSP triangles into the
  polygon buffers instead of LandTable COL entries.
* Geometry helpers `8C02CE80-8C02D980` (20 tiny functions: line/plane/triangle).
* `MSetPosition` (objects vs land) `8C0569E0/8C0569F0 -> 8C056A04`.
* **CCL** object collision `8C02E820-8C034940` (57 funcs, 23 KB): `CCL_Init 8C02F4B0`
  ("CANT GET COLLI_WORK!!!!", 79 callers), per-frame entry `8C02F63C` (83), shape-pair tests
  `8C02F8A0..8C033122`, list builder `8C03339E` fills per-kind lists `B_8C73EE38/8C742288`,
  `B_8C741280/8C7446D0`, master `8C033F00`, push-out `8C03431C` ("PUSHOUT"), `8C0344DC`
  (side1/2/3), debug display `8C02E820..8C02F33E`.
* **Paths/splines**: path library `8C089180-8C089F20` (`8C0897F8` status on path, 7 callers;
  `8C089AA0` path setup used by `RunAlongPathP`); path-work objects (loops, rails)
  `8C06AF00-8C06C0C0` drive the player through the player API. Loops in SA1 are scripted paths.
* **Water**: no separate module found; splash/ripple helpers live in player core C
  (`8C07B346..8C07B694`), shadow-on-water `8C092BE0`, `objexec_NO_WATER`. Underwater physics is
  inside the character code (open question).
* **Input** `8C0509A0-8C051760` (11 funcs): per-player pad state + analog conversion
  `8C05117E` (camera-relative angle, atan2f) + PadReadOn/Off-like `8C0509A0/8C0509A8`.
  Vibration `8C109E20-8C10A120` (`8C109EAC`, `8C109FDE` -> pdVib).

---------------------------------------------------------------------------------------------------

## 5. Engine pieces the characters depend on

| Piece | Where | Notes |
|---|---|---|
| **Task system + game heap** | `8C098440-8C098EE0` (1 TU, 32 funcs, 2.4 KB) | `CreateElementalTask 8C09852E` (213 callers), `DestroyTask 8C09865C` (82), `FreeTask 8C098786` (6 bytes, 189 callers), `CreateChildTask 8C098CA0` (36), list exec `8C098800..8C098900`, `MAlloc 8C098A80` (56), `CAlloc 8C098B0C` (49), `MFree 8C098B42` (70), heap init `8C098A10`. Same layout as `task.c` in Exant64/sa2dc (FreeTask is 6 bytes in both) |
| **Angle/vector utils + render state** | `8C055D20-8C057460` (1 chunk, 43 funcs) | `AdjustAngle 8C055D20` (66 callers), `SubAngle 8C055D4E`, `DiffAngle 8C055D5A`, `VectorAngle 8C055DAC`, `Save/Load/On/OffConstantAttr 8C056926/38/4A/5C`, `Save/Load/On/OffControl3D 8C056972..8C056990`, MSetPosition, matrix helpers 8C056DC0.., number formatting 8C056F94 |
| push-unit-matrix / misc draw | `8C057460-8C057BC0` | `8C057B40` (18 callers in physics) |
| node-matrix capture | `8C057BC0-8C0584A0` | `8C0583A0` (15), `8C058434` (12) used by welds (CalcMMMatrix/GetMMMatrix-like) |
| **Motion / animation** | `8C03FDC0-8C041E40` (85 funcs, 8 KB) + PSetMotion/PJoinVertexes in player core C + part attach `8C04B440-8C04BF00` | Ninja-style motion evaluation with callbacks calling njTranslate/njScale/njRotateXYZ (`8C0400A0..8C0401A8`), `8C040942` (35 callers) = draw action at frame. Data: `NJS_ACTION`/`NJS_MOTION` from the action lists |
| **Model drawing** | `8C036BC0-8C03FDC0` | wrapper `8C036F12` (80 callers) and a hand-written SH-4 basic-model renderer (`8C03700C`, `8C037098`, `8C038CEC`, `8C038D00`; FTRV/FIPR; chunk `8C038E20` is mostly a zeroed work area). Port: replace with a mesh emitter, do not decompile the asm |
| Camera | `8C019360-8C025960` (~185 funcs, 49 KB) | Characters call only ~12 camera functions (`8C01AD2C` x5, `8C01AE60` x8, `8C01ABDC`, `8C01B158`, `8C01FB98`, `8C01FD6C`, `8C021700`, ...): stub them; GMod owns the camera. Input conversion needs the camera yaw |
| Shadows | `8C091C20-8C092FA0` | `GetShadowPos 8C091C20` (20 callers), blob shadow draw `8C092124..8C09247E` (`Character_Shadow_Blob`), character shadow `8C0925A0`, XYZ/water variants `8C09282E/8C092BE0` |
| Sound triggers | `8C092FA0-8C095860` | `8C0940DA` play SE id (148 callers; 65 from the core), positional `8C09413A`, `8C094330`, `8C0943E0`, stop `8C0945A0/8C0945D0`; banks `*.MLT`, driver `masaru.drv`. Port: callback with SE id + position |
| BGM / voice | `8C107800-8C107E40` | ADXT based; `8C107BA0`, `8C107C80` called from the core |
| Game state / globals | `8C04C680-8C04FEA0` | `GetStageNumber 8C04F7E0` ((level<<8)|act, 133 callers), `GetPlayerNumber 8C04F698`, game state `8C04FE40` (B_8C7493B4), `8C04E386`, start position `8C04F8FA`; `CurrentLevel B_8C7493BA`, `CurrentAct B_8C7493BC`, `CurrentCharacter B_8C7493C8`, level-select `tcrf_LevelSelectVar 8C760970` |
| Rings / score / lives / time + HUD | `8C089F20-8C08F1A0` | counters around `B_8C752BDD..8C752BF8` (`8C08A198` add, `8C08A71E` get, `8C08AB2E/42/60`), in-play HUD `8C08A4BA` (rings, lives) / `8C08A724` (timer), called by the frame steps `8C04E95A` / `8C04EA4A` / `8C04EB7E`, clock `8C08A5CC`, number draw `8C08EE98`; `8C08ECB8` is the stage-clear results task, not a HUD master (`gsa1_070_hud.md` 1.5) |
| Effects | barriers `8C043C00-8C0445A0`; sprite particles `8C0445A0-8C047040`; SPR task `8C09DDA0-8C09E760` | optional |
| Texture lists | `8C098EE0-8C099A00` | `8C098FAE/8C098FFC` (load/release per character), `8C099500` set texlist, `8C099750` PVM loader ('%s.PRS') |
| File loading / PRS | `8C09CC80-8C09DDA0`; PRS decompressor `8C04C5E0` | stage overlay table, SET/CAM files, character motion files (section 7) |
| Events / cutscenes | `8C0491C0-8C04B440` | only `8C049A4E` ("event running") is called from the core (x9) |
| SET object manager | `8C090EC0-8C091C20` | `8C0919CC` (11 calls from the core) |
| libc/libm/runtime | `8C10C10C-8C111194` | `__divls/__divlu/__modls/__modlu`, `__quick_mvn`, **`rand` = `8C10D334`** (LCG *0x41C64E6D+0x3039, 149 callers; not memset), `memset 8C10D31C`, `strlen 8C10D3F0`, `sprintf 8C10D360`, float libm wrappers `8C10D008/8C10D058/8C10D0A8/8C10D0F8 (atan2f)/8C10D148/8C10D198` |

### 5.1 SDK (Ninja) functions used by the core (P0+P1): 68 functions

Identified by asm (FSCA/FTRV/FIPR/FSRRA patterns) and size/order agreement with sa2dc's Ninja names
(e.g. njInvertMatrix is 0x7FE bytes in both games). The port must reimplement these. Math must be
faithful: njSin/njCos use the FSCA hardware table with 16-bit BAMS angles, njUnitVector uses FSRRA.

`njAddVector 8C638EAC, njCalcPoint 8C638ECC, njCalcVector 8C638F28, njGetMatrix 8C638F94,
njInnerProduct 8C639090, njInvertMatrix 8C6390B0, njMultiMatrix 8C63997C, njOuterProduct 8C639B50,
njPopMatrix 8C639B98, njProjectScreen 8C639BDC, njPushMatrix 8C639C70, njRotateXYZ/ZXY pair
8C639CF4 & 8C639FF8, njRotateX 8C639EC8, njRotateY 8C639F5C, njRotateZ 8C63A1CC, njRotate(axis)
8C63A268, njScaleV 8C63A5EC, njScale 8C63A69C, njScalor2 8C63A748, njScalor 8C63A75C,
njSetMatrix 8C63A770, njSubVector 8C63A7E4, njTranslateV 8C63A804, njTranslate 8C63A878,
njUnitMatrix 8C63A8E0, njUnitVector 8C63A94C, njSin 8C63A970, njRandom-like 8C63A980, njCos 8C63A9B8,
njSqrt(signed) 8C63A9C4, njTan 8C63A9F4, njDrawLine3D 8C63CE24, polygon draw 8C63D414/8C63D66C,
njDrawSprite2D 8C63EEB4, njDrawSprite3D 8C63EF74, render-state 8C63FAE8/8C63FB18, distance/collision
helpers 8C63FE54/8C63FEAC/8C63FF84/8C64016C, njDistanceP2L 8C640080, njDistanceP2P 8C640128,
njPrintColor 8C640766, njPrint* 8C640922/8C640A80, njColorBlendingMode 8C605628, draw begin/end
8C605DAC/8C605E0A, njSetTexture 8C608CCC, njSetTextureNum 8C608D14, texture load/release
8C608C82/8C6088BC, model draw variants 8C60E080/8C60E49A, njSetConstantAttr-like 8C60EDE0/8C60EDE6,
njSetConstantMaterial 8C60EDF0, njDrawModel 8C611028, njDrawObject 8C61103C, alt model draw
8C6215D6, misc 8C601874, memcpy-like 8C604500, 8C604546, 8C60D1F8, syMalloc wrapper 8C64133E.`

---------------------------------------------------------------------------------------------------

## 6. Host interface: what the core calls outside itself

Computed from the call graph of all P0+P1 modules into modules marked S/X/P2 (counts = number of
distinct callees, "(n)" = callers inside the core). This is the shim surface a libsa1 needs:

* camera (12): `8C01AD2C(5) 8C01AE60(8) 8C01ABDC(3) 8C01B158(2) 8C01FB98(2) 8C01FD6C(2) 8C021700(2) 8C019368 8C01A7A4 8C0213E0 8C023360 8C0239C2`
* fast renderer (4): `8C03700C 8C037098 8C038CEC 8C038D00`
* game state (9): `8C04F7E0(33) 8C04FE40(7) 8C04F698(6) 8C04F8FA(3) 8C04E386 8C04EF22 8C04EF52 8C04EFE0 8C04C960`
* landtable (3): `8C0522C0(12) 8C052518 8C052E30` -> replace with host geometry
* sound (7): `8C0940DA(65) 8C09413A(19) 8C0945A0(13) 8C094330(9) 8C0945D0(6) 8C0941FC 8C0943E0`; BGM/voice (4): `8C107ADA 8C107B26 8C107BA0 8C107C80`
* counters/HUD (6): `8C08A71E(6) 8C08A298 8C08AB2E 8C08AB42 8C08AB60 8C08EE98`
* textures (4): `8C098FAE 8C098FFC 8C099500 8C099750`; SET manager (2): `8C0919CC(11) 8C091B8A`
* events (1): `8C049A4E(9)`; vibration (2): `8C109EAC 8C109FDE`
* optional gameplay (P2): objects `8C05E6E8/8C05E788/8C05E7D4/8C067414/...`, ring scatter `8C086F5E(6)`,
  barriers `8C043C9E/8C044060/8C044284`, particles `8C0465E0...`, SPR `8C09DDFC...`, Big fishing,
  Knuckles emerald hunt, E-102 list, Tails race hooks `8C0818CE..8C0818E4`, E-100 helpers
* libc/libm (14): division helpers, `rand`, `atan2f` (67 calls), acos/asin-like, `__quick_mvn`.

---------------------------------------------------------------------------------------------------

## 7. 1ST_READ vs. overlays vs. disc files (asset ripping)

| Item | Location |
|---|---|
| Code of Sonic, Tails, Knuckles, E-102, Amy, Big, Eggman + player physics + engine | 1ST_READ |
| Their models, welds, morph targets, upgrades, effects, action lists, CCL infos, base motions | 1ST_READ data, in code order: Sonic `8C1C24BC/8C1C55A0-8C256D6C`, Tails `8C256D6C-8C3528CC`, Knuckles `8C3528CC-8C4225D4`, E-102 `8C4225D4-8C4BB1D0`, Amy `8C4BB1D0-8C525564`, Big (+fishing, Froggy) `8C525564-8C57008C`, Eggman `8C57008C-~8C594968`; physics `8C1BDFE4` (8 x 0x84); start positions `8C19C4F8..8C19DD78`; shadow blob `8C1ACA78` |
| Extra motions (current character only) | `S_MOT.PRS`, `K_MOT.PRS`, `E_MOT.PRS`, `A_MOT.PRS`, `B_MOT.PRS` (no Tails file). Decompressed images are linked at **`0x0CC00000`** (1,059-2,145 internal pointers each) and point back into 1ST_READ models. Loader strings in `func_8C09D980`. Action-list entries point there: Sonic 43, Knuckles 29, E-102 29, Amy 60, Big 67 entries in `0x0CCxxxxx` |
| Stage-dependent motions | Sonic has action entries into `0x0C9/0x0CA/0x0CB` (loaded overlays), e.g. `S_SBMOT.PRS` (base `0x0CBxxxxx`), `M_SBMOT.PRS` (`0x0CA/0x0CB`) for board riding; `SBOARD.PRS` overlay (`0x0C900000`, 104 funcs) is the Sand Hill stage (its level task puts the player on the board; the board task itself is 1ST_READ `8C0B2EE6` / `8C0BF7CE`) |
| Textures | PRS-compressed PVM archives: `SONIC.PRS`, `SON_EFF.PRS`, `MILES.PRS`, `KNUCKLES.PRS`, `KNU_EFF.PRS`, `E102.PRS`, `E102BEAM/EFFECT/HIT/TIME.PRS`, `AMY.PRS`, `AMY_EFF.PRS`, `BIG.PRS`, `BIG_KAERU.PRS`, `EGGMAN.PRS`, `TIKAL.PRS`, `SUPERSONIC.PRS` (names in the 1ST_READ string table at 8C15A1A0.., FISHING texlist too) |
| Tikal | `TIKAL_PROG.PRS` overlay at `0x0CB00000`: code (17 funcs) + model, weld list, motions |
| Super Sonic | texture name only in the `B_CHAOS7` overlay; code path = Sonic |
| Sounds | `*.MLT` banks (122 files; character voices `v_*.mlt`), ADX voices (`SONIC.ADX` etc.) |
| Landtables / paths / SET layouts | stage overlays `STGxx`, `ADVxx` + `SET*.BIN`/`CAM*.BIN` (not needed for a character library) |

Overlays not needed for libsa1: all STG/ADV/boss/Chao/minigame/EV overlays (they only *use* the
player API). Useful generic overlay code: TIKAL_PROG (SBOARD is the Sand Hill stage; the board code is in 1ST_READ).

---------------------------------------------------------------------------------------------------

## 8. Biggest functions in the core (decompilation risk)

`8C0A966C:4A48 8C0B70F4:3644 8C0C193A:2BD0 8C0E0E70:29FE 8C0CD0A8:29C6 8C0D7960:27A6 8C0A77BC:1E1C
8C0C0640:12FA 8C0CBE00:129C 8C0730D8:11FC 8C0B6148:FA0 8C0DFFA0:ED0 8C0D6B40:DAC 8C029B00:B50
8C0777E0:B28 8C06D254:A4E 8C076782:A42 8C02770E:9BE 8C07A55C:99C 8C0CFACE:946` (address:size).
The six mode machines are each 10-19 KB of SH-4 code with dense switches. A functional
(non-matching) decompilation is enough for the port; matching them is the hardest part of the job.

---------------------------------------------------------------------------------------------------

## 9. Size estimate and priority list ("libsa1")

Tags used in the CSV: **P0** decompile (core), **P1** decompile (needed support), **P2** optional
gameplay extras, **S** stub/shim or reimplement behind a host API, **X** not needed.
Sizes are sums of function code sizes from `build/1ST_READ.xrefs.json` (literal pools excluded).

| Tier | Functions | Code bytes | Content |
|---|---|---|---|
| P0 | 814 | 326,690 (0x4FC22) | player common, 6 character cores, land collision + geometry, angle utils + MSetPosition, motion eval, part attach, node matrices, task system/heap |
| P1 | 268 | 71,856 | CCL, input conversion, path lib, shadows, draw wrapper, weld variant, debug line/material, character effect TUs (Sonic, Tails, Knuckles, E-102 weapons, Amy) |
| P2 | 885 | 179,530 | stage objects (springs, dash panels, item boxes, rings...), path-work objects, AIs, barrier/particle effects, Big fishing, Froggy, emerald hunt, Eggman, lantern lighting |
| S | 1,629 | 399,204 | camera, renderer asm, events, game mode, landtable, HUD, sound, textures, loaders, libc, **Ninja (696 funcs)** |
| X | 2,877 | 424,304 | everything else |
| overlay TIKAL_PROG | 17 | 0x13F4 | Tikal |

**Minimum "Sonic only" milestone**: P0 engine/physics part (457 funcs, 116.7 KB) + Sonic core
(68, 47.1 KB) + Sonic effects (49, 10.5 KB) = **574 functions, about 174 KB of SH-4 code**, plus
about 40 Ninja math functions to reimplement and the shims of section 6. Adding P1 engine support
(CCL, input, paths, shadows): about 680 functions / 210 KB. All characters (P0+P1):
**1,082 functions / about 398 KB** (about 17% of 1ST_READ's 6,473 functions).

### 9.1 Proposed source files (one per recovered TU / chunk group)

Priority order; each line = range, TU starts from `build/1ST_READ.tus.json`, chunk count.

1. `engine/task.c` 8C098440-8C098EE0 (TU 8C098440, 2 chunks): task system + heap.
2. `engine/angle_util.c` 8C055D20-8C057460 (1 chunk), `engine/matrix_util.c` 8C057460-8C057BC0
   (TUs 8C057460, 8C057AE0), `engine/nodematrix.c` 8C057BC0-8C0584A0 (TU 8C057BC0).
3. Ninja math reimplementation (section 5.1), with an FSCA-exact sin/cos table.
4. `player/pl_alloc.c` 8C06C0C0 (TU), `player/pl_core_a.c` 8C06C300-8C06F6C0 (TU, 8 chunks),
   `player/pl_core_b.c` 8C06F6C0-8C076640 (TU, 7 chunks), `player/pl_core_c.c` 8C076640-8C07DEC0
   (TU, 16 chunks).
5. `collision/landcolli.c` 8C027360-8C02CE80 (TU, 17 chunks), `collision/geom.c` 8C02CE80-8C02D980
   (TUs 8C02CE80, 8C02CFC0); a host-geometry adapter replacing `8C0519C0-8C052FA0`.
6. `motion/motion.c` 8C03FDC0-8C041E40 (TU, 2 chunks), `motion/partattach.c` 8C04B440 (TU),
   `motion/weld2.c` 8C04BF00 (TU).
7. Test character: the Tikal overlay (17 funcs, uses only the shared pipeline).
8. `chr/sonic.c` 8C0A77A0-8C0B33A0 (TU, 11 chunks) + `chr/sonic_eff.c` 8C0B33A0 (TU, 6 chunks).
9. `collision/ccl.c` 8C02E820-8C034940 (TU, 3 chunks); `input/pad.c` 8C0509A0 (TU, 2 chunks);
   `path/path.c` 8C089180 (TU, 3 chunks); `render/shadow.c` 8C091C20 (TU, 2 chunks).
10. `chr/miles.c` 8C0B6120 (+ `miles_eff.c` 8C0BFE00), `chr/knuckles.c` 8C0C0640-8C0CA040 (one TU,
    includes the emerald hunt) + `knu_eff.c` 8C0CA040, `chr/e102.c` 8C0CBE00 (TUs 8C0CBE00,
    8C0D2600, 8C0D26E0, 8C0D32A0) + weapons (TUs 8C0D3C00 .. 8C0D6960), `chr/amy.c` 8C0D6B40 +
    `amy_eff.c` (TUs 8C0DEEA0, 8C0DF100, 8C0DFE60), `chr/big.c` 8C0DFFA0 (+ fishing TUs 8C0E6620,
    8C0EBFA0, Froggy 8C0EC6E0).
11. P2 extras as wanted (rings `8C0869E0`, springs/dash panels/item boxes `8C059A80-8C06AF00`,
    barriers, particles, Eggman `8C0ED880`).

---------------------------------------------------------------------------------------------------

## 10. Caveats and open questions

1. SADX-style names in sections 3-5 marked medium/low are structural guesses; the first
   decompilation pass should confirm `PGetRotation`, `PGetAccelerationAir`, the gravity/inertia
   family, `8C079096` and `8C07D7B4`.
2. SA Tools `DC_SA1/1st_read.ini` physics labels for Eggman..Big are wrong (0x108 stride instead
   of 0x84). The real layout is `8C1BDFE4 + id*0x84`; `8C1BE404` is a death-zone table, not Tikal
   physics. `reference/dc_sa1_data_labels.csv` and `config/symbols.1st_read.txt` inherit the error.
3. Super Sonic: no separate main was found; check Sonic's main/mode machine for the super flag and
   what B_CHAOS7 sets (physics override, texture swap to `SUPERSONIC`).
4. Underwater/water-surface physics location (inside character code or player core C) is not pinned.
5. Logic rate: whether SA1 DC runs player logic once per displayed frame or uses a frame-skip
   counter needs checking in the main loop `8C053B60` and the task executor before porting timing.
6. Renderer: how welds and morph targets (`Sonic_*_Foot_Morph_*`, Tails tail morphs) reach the asm
   renderer (`8C036F60-8C03FDC0`) should be traced before writing the mesh emitter.
7. `build/1ST_READ.tus.json` has no recorded generator and is incomplete (198 starts); some modules
   here have no TU start (e.g. `8C055D20`). The boundaries in this map are chunk-aligned.
8. Library region boundaries (Shinobi/Ninja/Kamui/sd) are approximate except the Ninja functions
   listed in 5.1.
9. Unknown-role modules (low confidence in the CSV): `8C010400`, `8C014480`, `8C025960`,
   `8C095860` (0x256C-byte function with no callers), `8C107E40`, `8C10A120`, `8C099A00`.
10. Camera: characters call about 12 camera functions (springs, homing, boards); decide per call
    whether to stub or forward to the GMod camera.

### Licences of sources consulted

* This repository's reference CSVs (derived from SA Tools, X-Hax/sadx-mod-loader, Exant64/sa2dc,
  tbg-decomp, crazy-taxi-decomp, SARecomp, doldecomp/sadx; see `reference/README.md`).
* Exant64/sa2dc @ ba180d99 (cloned to the scratchpad for this report): **no LICENSE file** in the
  repository; used only for function names/sizes, no code copied.
* X-Hax/sadx-mod-loader (scratchpad clone): no LICENSE file at the repository root; names only.
* SA Tools: has a `Licenses` folder; only its data-label configuration was used (via the CSVs).
