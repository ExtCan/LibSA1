# gsa1_090_bigfish: Big's fishing camera (and HUD) and head look-at - 0.9.0 report

Topic of the 0.9.0 diagnosis (plan 14.1i). Read-only diagnosis: nothing in SADC/src, SADC/include, SADC/tools, LIB or
g-sa1 was changed. Experiments: `SADC/build/research/bigfish/` (probe `look_probe.c` linked against a snapshot of
`LIB/build/x86_64-windows/libsa1.a` taken under LIB's build lock by `lockcopy.py` on 2026-10-25 09:34, i.e. with
0.9.1's landed HUD C already in it; outputs `out_*.txt`; scripts `xcallers.py`, `headkeys.py`).

Owner items (verbatim):

* "The camera doesn't follow Big's fishing line when he casts it into the water when it should, it also doesn't have
  Big's fishing HUD."
* "The characters don't turn their heads to look at things like they do in Sonic Adventure."

## 0. Summary

1. **Fishing camera.** The game drives three cameras while Big fishes: the FISHING camera while he aims and the lure flies
   (behind Big, looking at the cast cursor), the **lure camera** `8C0EAF28` while the lure is in the water (it follows the
   lure and keeps about 15 units behind it, just above the water), and a catch shot. Big's code asks for them through
   the camera's push / pop functions (`8C01AD2C`, `8C01AF06`, `8C01AE60`, `8C01B158`, `8C01ABDC`). In libsa1 these are
   void stubs (`LIB/gen/policy/stubs.tsv:59-63`). `8C0239C2` (FISHING) is "address-only" (`:69`) and `8C0EAD6A` (catch
   target) is a void stub (`:154`). G-SA1's chase camera knows nothing about any of them. The data the cameras need is
   computed in every libsa1 world today: the lure, `B_8C6B6E68` (lure position and camera target) and the camera work at
   `8C6B6EEC`. Running the game's own camera functions on that data inside a world gives the game's lure camera
   (measured, section 2.4).
2. **Fishing HUD: covered by round 0.9.1** (section 2.6). The only gap is a reference with the lure in the water.
3. **Head look-at.** The game's head tracking runs in libsa1 and is drawn. Sonic, Tails, Knuckles and Amy turn their
   heads into a turn (up to 45 degrees, measured in the render). What never happens in G-SA1 is looking **at things**. In
   SA1 that is started only by game objects that G-SA1 maps do not have: freed animals (5 s), NPCs while they talk
   (adventure fields), Tikal's hint orbs, hint monitors, character upgrades and a few stage set pieces. The fix is a
   libsa1 look API driven by G-SA1 with GMod targets (NPCs, players, items, the camera). There are exceptions that
   match the game: E-102 has no head tracking at all (his look task is dead code), Big's look turns only his eyes, and
   Sonic's idle motion keys the head.

**What 0.9.1 covers** (`SADC/tools/workflows/gsa1_round_091.js`): Big's fishing HUD in full. 091-land lands `8C0EB160`,
`8C0E6FE0` / `8C0E70E4`, `8C0E746C` / `8C0E74FC` and `8C08EE98` and removes their stub lines (already gone from
`stubs.tsv` at the time of writing). 091-lib-hud F2-F6 adds `njDrawPolygon2D` 8C63EBC8 as POLY2D (the tension bar) and
`sa1_character_screen`. 091-gsa1-hud F8 draws "Big's reel / tension / weight". The verifier checks "Big's WEIGHT and
the reel / gauge while fishing". **0.9.1 covers neither the fishing camera nor head look-at**: no 0.9.1 agent touches
a camera hook, CalcView or the look code.

---------------------------------------------------------------------------------------------------------------------

## 1. Causes

| # | Owner item | Cause | Evidence |
|---|---|---|---|
| C1 | camera does not follow the line | libsa1 drops every camera request of Big's code (void stubs `stubs.tsv:59-63`, `8C0239C2` address-only `:69`, `8C0EAD6A` void `:154`), has no camera output API, and G-SA1's CalcView (`g-sa1/addon/lua/gsa1/cl_character.lua:470-499`) is the mouse chase camera only | 2.1-2.4 |
| C2 | no fishing HUD | Big's HUD task `8C0EB160` and its draws were void stubs, `njDrawPolygon2D` is a void stub, and 2D draws were dropped at emission (gsa1_070_hud.md 1.3) | being fixed by 0.9.1 (2.6) |
| C3 | heads don't look at things | Only game objects set the event look (`ewp->look`): animals `8C015820`, O_TIKAL, O_HINT, O_KN_HINT, O_LevelItem, adventure-field O_PEOPLE, four stage / boss objects. None of them exist on a GMod map, and libsa1 has no API for a host target | 3.1, 3.3 (H) |
| C4 | (the parts that are the game) | E-102's look task `8C0D1916` is never created anywhere (no pointer to it in 1ST_READ or any overlay). Big's look "head" node `8C529520` is a leaf without model or children (only his eyes move). Sonic's idle action 1 (and 2, 5, 74) keys the head node, which overrides the look | 3.1, 3.3 (A, D, F, G) |

## 2. Big's fishing camera

### 2.1 What the game does (action stages; `func_8C04C960() != 1`)

| When | Code (file:line) | Camera call | Level / adjust | Function and what it computes |
|---|---|---|---|---|
| Big pulls the rod (mode 32 -> 33 while the action button is held) | BigModeMachine `code_8C0E0E70.c:557` (also `:733` for the power-rod cast) -> `func_8C0E6586` `code_8C0E6500.c:52` | `8C01AD2C(8C0239C2, 3, 2)` (adventure field: `8C01ABDC(8, 5)`, mode 8 KNUCKLES2 with TIME) | EVENT (2), adjust 3 SLOW, kind 2 | **FISHING `8C0239C2`** (0x108 B, asm). Ghidra reading: target = `B_8C6B6E68.tgt`, the cast cursor placed by `8C0EAB68` (`code_8C0EAA80.c:83`: the cursor task's horizontal offset from Big, at least 30 units, at the cursor height). The camera is 50 units from Big on the line from the target through Big, raised by (0, 20, 0) before and after normalising. Then `8C01B684` (push out of CAM COLI volumes; returns at once when the level has no CAM table), `8C01BD3C` (blend in from the previous camera over 30 steps, counter `8C18B86C`) and `8C01E32A` (camera against the land). **This is the placement of `8C0EAA80` plus camera collision (UNVERIFIED until a harness diff of the decompiled function)** |
| Lure flies (lure modes 2, 3) | `func_8C0E943C` `code_8C0E91E4.c:255` | (FISHING stays) | | the camera keeps looking at the cursor, i.e. where the lure will land |
| Lure lands in water (mode 4 -> 5) | `code_8C0E91E4.c:418-419` | `8C01AE60()` then `8C01AF06(8C0EAF28, 0, 2)` | pop EVENT; push **COLI (4)**, adjust 0 | **lure camera `8C0EAF28`** (C, `code_8C0EAEC0.c`). Target = midpoint of `B_8C6B6E68.pos` (the lure, written each step by `8C0EABEE`, `code_8C0EAA80.c:102`) and `.tgt` (a point 10 units past the lure, away from Big, or towards a hooked fish). The camera keeps its direction from the target at 2 x their distance (= 20 units), moves at most 20 units per step, never goes below the target. When it is more than 5 units under the water level `D_8C565F6C`, a radius-4 sweep (`8C0EAEC0` -> `8C0569E0`) from the camera task's position limits it |
| Lure reeled in completely with L + R (no fish), mode 5 | `:434-440` | `8C01B158()`, `8C01AD2C(8C0239C2, 0, 2)` + `8C0EAE0A` (target 30 units from Big towards the lure, Big's height) | pop COLI; FISHING again, cut | |
| Reel in with a fish within 50 units | `func_8C0E91E4` `:177`, `func_8C0E9CC6` `:512-513` | `8C01AD2C(8C0EAA80, 0, 2)` + `8C0EAD6A` | EVENT, cut | **catch shot**: `8C0EAD6A` (0xA0 B, asm) sets `.tgt` = Big + (100, 0, 0) turned by Z, X and Y(0x8000 - ang.y). `PConvertVector_P2G` turns by -ang.y (`code_8C078E40.c:59`), so this is 100 units **behind** Big. `8C0EAA80` then places the camera 50 units **in front of** Big, 20 up, looking at him (reading, UNVERIFIED). BigModeMachine case 37 calls `8C0EAD6A` every step while he shows the fish (`code_8C0E0E70.c:686-688`) |
| Rod put away / catch shown | `func_8C0E65A8` `code_8C0E6500.c:61`; case 37 | `8C01B158` or `8C01AE60` | pop | back to the normal camera |
| Hot Shelter (stage 12) | `code_8C0E91E4.c:186, 196, 436` | `8C0EAA80` + `8C0EAD6A` instead of FISHING | | not relevant to G-SA1 worlds |

The camera stack and levels are described in `docs/research/v1_triggers.md` 1.2-1.5. FISHING is modes 26-27 of the
76-mode table (`v1_triggers.md:147`).

### 2.2 Decompilation status

| Function | Size | Status | In libsa1 |
|---|---|---|---|
| `8C01AD2C` push EVENT by function | 0x100 | asm (camera range 0 % decompiled) | void stub `stubs.tsv:60` |
| `8C01ABDC` push EVENT by mode | 0x150 | asm | void `:59` |
| `8C01AE60` pop EVENT | 0xA6 | asm | void `:61` |
| `8C01AF06` push COLI by function | 0xFA | asm | void `:62` |
| `8C01B158` pop COLI | 0xA2 | asm | void `:63` |
| `8C0239C2` FISHING mode | 0x108 | asm | address-only `:69` |
| `8C01B684`, `8C01BD3C`, `8C01E32A` (FISHING's callees) | 0xFE, 0x62, 0x1DE | asm | not linked |
| `8C0EAD6A` catch target | 0xA0 | asm (in no src file) | void `:154` |
| `8C0EAA80`, `8C0EAB68`, `8C0EABEE` | 0xE8, 0x86, 0x17C | C (`code_8C0EAA80.c:61, 83, 102`) | lowered (def) |
| `8C0EAE0A`, `8C0EAEC0`, `8C0EAF28` | 0x70, 0x68, 0x1E8 | C | lowered |
| `8C0E6586`, `8C0E65A8`, `8C0E91E4`, `8C0E9CC6` | | C | lowered |
| `8C0E943C` lure task | | NON_MATCHING C | lowered |

The camera work pointer `D_8C112048` is a static pointer in 1ST_READ data. It holds `0x8C6B6EEC` (read from
`baserom/1ST_READ.BIN` and in a live world, out_e.txt), so it is valid in every world arena.

### 2.3 libsa1 and G-SA1 today

* The camera hooks are void stubs ("the player code does not read back what these set"). The world's camera adapter
  taskwk `D_8C18B7EC` gets only the host **yaw** (`sa1_world_api.c:193`, `sa1_world_tick.c:52-64`). Its position is never
  set from the host camera.
* Everything else runs. The lure task, `B_8C7A40E8`, `B_8C6B6E68` and the line simulation are lowered C.
* G-SA1: CalcView is the mouse chase camera (`cl_character.lua:470-499`; `gsa1_cam_distance` 150 HU and `gsa1_cam_height`
  20 HU, `sh_config.lua:123-124`). The tick gets `camera_yaw = LocalPlayer():EyeAngles().y` (`cl_character.lua:373`).
* So the owner sees the mouse camera behind Big the whole time. The lure lands up to 170 units (680 HU) away, which is
  more than four chase-camera distances.

### 2.4 Reproduction (probe part E, out_e.txt)

Flat test level: a 600 x 600 HU island at z 0, water at z -20 HU (SA1 y -5) everywhere else, sea bed at -600 HU. Big at
the origin, camera yaw 0. Inputs: 10 pairs idle, then the action button held for 40 pairs (stick forward from pair 25),
released, 150 pairs idle, L + R for 20 pairs, 40 pairs idle.
The step hook does what the game camera would do on the camera work `8C6B6EEC`. While Big is in modes 33/35/36 (FISHING
pushed) it runs `8C0EAA80` (FISHING's placement). While the lure is in mode 5/6 with `B_8C7A40E8 & 0x40` (lure camera
pushed) it runs the game's `8C0EAF28`. The camera work starts at a chase position, 50 behind and 20 up.

| Pair | Big mode | Lure mode | `B_8C7A40E8` | Camera (SA1 units) | Notes |
|---|---|---|---|---|---|
| 23 | 33 (aim) | 1 | 0000 | (-50, 20, 0) -> target (30, 20, 0) | FISHING target = cursor (`8C0EAB68`), 30 units out |
| 40 | 33 | 1 | 0000 | (-48.6, 30.8, 4.2) -> (112.5, -5.0, -9.6) | the cursor moved out over the water with the stick |
| 50-80 | 35 (cast) | 2, 3 | 0000 | (-49.3, 27.3, 4.5) -> (169.2, -5, -15.6) | lure in flight, camera still |
| 81 | 35 | 4 | 0000 | | lure at the water (y -5.6) |
| 82 | 35 | **5** | **0040** | (-9.8, 21.5, 0.9) | lure camera starts, 20 units per step towards the lure |
| 90-190 | 35 | 5 | 0040 | (162.4..165.6, -3.4..-4.9, -14.4..-14.7) -> midpoint (182.0, -6.5..-16.2, -16.1) | **\|camera - midpoint\| = 20.0** every logged pair, \|camera - lure\| 15.1-16.5, camera 0.1-1.6 units (0.4-6.4 HU) above the water. The lure sinks from 1.1 to 12.6 units under the surface |
| 200 | 35 | 7 | 4000 | (-41.4, 47.7, 3.7) -> (29.9, 0, -2.6) | L + R: back to FISHING (a cut, 214.9 units in one step) |
| 209 | 1 | 1 | 4000 | | rod away (the FISHING request is popped in the game) |

No world fault. These steps reproduce the game's camera behaviour from libsa1 data alone. `8C0EAF28` is the game's own C
running unchanged in a world. Big's 2D HUD records (the landed 0.9.1 C) are 9 per pair, and 13 per pair while the lure
is out (reel, gauge frame, line length digits) for all 118 pairs in mode 5.

Side finding: the lure camera looks at a lure **under** the water surface (1 to 13 units deep in this run), from 0.4 to
6.4 HU above the surface. With the 0.9.0 water report ("can't see the characters through the top of water"), the lure
and line seen from this camera will be hidden too. The water topic's fix applies to the lure draws, which come from
Big's render. UNVERIFIED in game.

### 2.5 Design: the game's camera requests in libsa1, followed by G-SA1

**libsa1 (new `src/world/sa1_world_camreq.c`, API `sa1_character_camera`).**

1. Replace the void policies of `8C01AD2C`, `8C01ABDC`, `8C01AE60`, `8C01AF06`, `8C01B158` (and `8C01B040`, push COLI by
   mode, 0x118, which only the free camera calls) with LIB stand-ins. They keep a request per level (EVENT 2, COLI 4) in the
   world struct, **not in the arena**: function (DC address) or mode index, adjust, conversion kind, push step. A pop
   clears the level. The top request is COLI if present, else EVENT.
   This is the request record the game keeps in its control work `*(8C11204C)` (+8 level, +9 adjust, +0xC function;
   `v1_triggers.md` 1.2). The full camera core (T3a of v1_triggers) can replace the stand-ins later.
2. Live worlds only (`w->replay == 0`): after each step's executor, if the top request is a **known** function, run it
   on the camera work:
   * `8C0EAF28` (lure camera);
   * `8C0EAA80` (catch);
   * `8C0239C2`. Until F1 lands, run `8C0EAA80` in its place: the same placement without camera collision (UNVERIFIED
     equivalence, 2.1).

   The game camera runs once per 60 Hz step. Whether it runs before or after the player is UNVERIFIED. The camera
   task is created at level 0, so it probably runs before the player. Running after the executor uses this step's
   lure, which is one step earlier than the game. Keep it consistent and document it.
3. Seeding: at each push, copy the host camera into the camera work (`+0x0C` position, `+0x18` target) in SA1 space,
   and every step into the adapter taskwk's position (`camera_twp->pos`, today never set). The host camera comes from
   two new `sa1_input` fields, `camera_position[3]` and `camera_target[3]` (host space), read when struct_size covers
   them. This matches the game, where the camera work holds the previous camera when a new mode starts.
   `8C0EAF28` then flies from the player's view to the lure at 20 units per step. Its under-water sweep starts from the
   real camera.
4. Output: `sa1_status sa1_character_camera(sa1_context*, sa1_character_id, float alpha, sa1_camera_state *out)`. It is
   a subset of the v1_triggers 4.4 draft and keeps that name so T3 extends it:
   * `struct_size`;
   * `flags`: ACTIVE, KNOWN (LIB evaluated it), CUT (adjust 0 push this pair);
   * `level`, `function` (DC address), `mode`, `adjust`;
   * `position[3]`, `target[3]`: host space, interpolated between the last two pairs at alpha, like `sa1_render`.

   Unknown requests are reported ACTIVE without KNOWN, so the host keeps its own camera. Examples: Sonic's death
   camera `8C021700`, LSA, Tails' `8C0213E0`, the board cameras.
5. Replays: no arena write in replay mode. The stand-ins write no game memory, so the strict references are unchanged.

**G-SA1 (feature "gamecam", own files through the G0 seam).**

* `src/module/bind_gamecam.cpp`: `gsa1.Camera(id, alpha)` returns `{flags, level, fn, adjust, px, py, pz, tx, ty, tz}`
  or nil. `src/core/session.{hpp,cpp}` (patch): the tick passes the view origin and a target (origin + forward) into
  the new input fields.
* `addon/lua/gsa1/feat/cl_gamecam.lua`, behind a one-line seam in `cl_character.lua`'s CalcView
  (`GSA1.ViewOverride(view)`, after the view-entity check). While a KNOWN request is active and the option allows its
  source:
  * origin = the request position, hull-traced from the target to the position (the existing 5 HU hull,
    `MASK_SOLID_BRUSHONLY`); angles = (target - origin):Angle().
  * Blend in and out over `gsa1_gamecam_blend` seconds (default 0.3; lerp the origin, slerp the angles). A request with
    CUT cuts as the game does.
  * The mouse does not turn the view while it is active (as the game). Option "orbit with the mouse" (yaw / pitch
    offsets around the target).
  * A second seam at `cl_character.lua:373` (`GSA1.TickYaw()`): while the game camera is shown, the tick gets **its**
    yaw. The stick then aims the cursor and moves the lure relative to the screen, as `8C05117E` does with whatever camera
    is active.
* Q menu, Options > G-SA1 > Camera: "Big's fishing camera" = Off / Lure only / As the game (aim, lure, catch; default),
  blend time, mouse orbit. README checklist line.

### 2.6 Big's fishing HUD: covered by 0.9.1

| HUD part | Game function | 0.9.1 step |
|---|---|---|
| WEIGHT (best fish), ring count, lives with Big's icon | exec `8C0EB160` -> `8C0E6FE0` / display `8C0E70E4`, numbers `8C0E74FC` -> `8C08EE98` | 091-land (`code_8C0EB160.c`, `code_8C0E6FE0.c`, `code_8C0E746C.c`, `code_8C08EE98.c`; stub lines removed), 091-lib-hud F2-F6, 091-gsa1-hud F8 |
| reel (rotating) | `8C0E727A` (C) | recorded already; emitted by F6 / F8 |
| line tension gauge frame + bar | `8C0E72DA` (C) + `njDrawPolygon2D` 8C63EBC8 | F2 (POLY2D, `stubs.tsv:236` goes), F6, F8 |
| line length digits | `8C0E746C` | 091-land, F8 |
| Froggy-catch banner | `8C0E6F04` via `8C0EB184` / `8C0EB244`, created by the Froggy object `8C0ED398` | **not covered**; needs Froggy (role UNVERIFIED), not in G-SA1 |

Gap: the only Big reference, `hud_big_ec3`, never lands the lure in water (`gsa1_070_refs.md:161, 315`). The in-water
state therefore has no reference: line length counting, the tension bar at a value. The probe's flat-island cast lands
it at pair 82 and holds lure mode 5 for 118 pairs (13 SPRITE2D records per pair). It is proposed as a LIB test case
(F4).

## 3. Head look-at

### 3.1 The game's head tracking

**Look tasks** (`CreateElementalTask(2, 2, exec)` from each main's mode 0; look work `lookwk` 0x88 B in `tp->mwp`):

| Character | Creator (call site) | Exec | Head / eyeL / eyeR objects (measured in a world, out_a.txt) | Limits (degrees, out_a.txt) |
|---|---|---|---|---|
| Sonic | `8C0B27EE` `code_8C0B2338.c:231`, called `code_8C0A77BC.c:191` with `D_8C1C5F08` | `8C0B28EE` `code_8C0B2890.c:155` (NON_MATCHING C) | 8C1CDDE8 / 8C1CDA20 / 8C1CDCB0 | head y +-45, z -33.8..+5.6; eyes y -38..+2.8 / -2.8..+38, z -18.3..+11.2 |
| Tails | `8C0BE070` (`code_8C0BDC80.c:207`), called `code_8C0B6148.c:153` | `8C0BE1CE` `code_8C0BE160.c:101` | 8C25F898 / 8C25D164 / 8C25D2E0 (eyes moved by translation, `8C0BEA0C`) | head y +-33.8, z -33.8..+5.6 |
| Knuckles | `code_8C0C8534.c:304` | `8C0C8A80` | 8C35AE8C / 8C35AAC4 / 8C35AD54 | head y +-45 |
| Amy | `code_8C0DD490.c:193` | `8C0DDA00` | 8C4C796C / 8C4C74F0 / 8C4C77BC | head y +-45, eyes +-11.2 |
| Big | `8C0E5D20` `code_8C0E5D20.c:83`, called `code_8C0DFFA0.c:184` with `D_8C525C1C` | `8C0E5DF0` | **8C529520 = a leaf node without model or children** / 8C52739C / 8C5274E8 | head y +-33.8 |
| E-102 | **none** | `8C0D1916` exists (`code_8C0D1916.c:148`) but **nothing creates it**: no pointer to it in 1ST_READ or any of the 257 overlays (`xcallers.py`, a binary scan) | - | - |
| Tikal | her own copy of Sonic's in TIKAL_PROG (`src/overlays/tikal_prog/code_0CB00F60.c`) | | | |

**Modes** (Sonic `code_8C0B2890.c:155-299`; the others are copies):

* 1: the head leads the turn: `head_tgt_y = -(ang.y - ang_aim.y)` (`:210`).
* 2: look at the camera taskwk `D_8C18B7EC` while pad bit 0x100 is held (`:179`). This is Shinobi `PDD_DGT_TZ`, the Z
  button, which a standard Dreamcast pad does not have.
* 3: event look from `taskwk.ewp` (`eventwk` +4 look, +0xC angles, +0x14 point, +0x20 task):
  * 1: angles;
  * 2: a point;
  * 3: a task plus offset. Sonic turns the offset by the task's angles; Tails and Big do not. Sonic, Knuckles and Amy
    test `look_tp` and `look_tp->twp` for NULL. **Tails and Big read `look_tp->twp` without testing `look_tp`**
    (`code_8C0BE160.c:201`, `code_8C0E5D20.c:216`).

The head turns 0x1000 BAMS (5.6 degrees) per step and the eyes 0x800 (`func_8C07AEF8` / `func_8C07AF8C`,
`code_8C07AEF8.c:53, 78`). Both write the head / eye `NJS_OBJECT.ang` in place. Sonic's also copies the head angles
into the Light Speed aura's head (`:297-298`).

**Setters** (C):

* look at a task + offset `8C07C2E2` (`code_8C07C298.c:57`);
* a point `8C07C318` (`:74`);
* angles `8C07C34C` (`:90`);
* off `8C07C376` (`:108`);
* timed versions `8C07C3F2` / `8C07C44E` / `8C07C49E` (`code_8C07C3C0.c:53...`): a timer task stops the look after n
  steps.

**Who sets the event look in play** (every caller of the setters in all images; `xcallers.py` on `build/*.xrefs.json`
plus a pointer scan):

| Caller | What | Rule |
|---|---|---|
| `8C015820` (`code_8C015820.c:35`) | **freed animal** (badnik destroyed, capsule) | look at the animal task for 300 steps (5 s) |
| O_TIKAL `8C0607A6` / `8C0608CE` | Tikal's hint orb | look at the orb when the player is within 225 (distance or squared distance: UNVERIFIED), stop when done |
| O_HINT `8C061198`, `8C06125A`, `8C06132A` | hint monitor | look at its point while reading |
| O_KN_HINT `8C061A90` / `8C061B80` | Knuckles' hint | look at the task, stop after a timer |
| O_LevelItem (`8C0698F6` -> `8C06A7E0`) | character upgrade | look at the item while within 40 units |
| ADV00 O_PEOPLE `0C9107A0` / `0C91064E` (+ `0C91103E` / `0C911018`), ADV02 x2, ADV03 x2, ADV0100, ADV0130 | adventure-field NPCs | look at the NPC's position + its height when it starts talking (state 3/4), stop when the message ends (reading of the asm, UNVERIFIED) |
| B_ROBO `0C902740`, STG05 `0C908FE0` (near O_Erupt), STG06 `0C90CF0E` (near CANNON_S), STG08 `0C90DFE0` (near O_Kuguri) | boss / stage set pieces | point / angle / timed point |

The event library clears the look (`8C049E90`, `8C04A3B6`). It was not searched exhaustively for other setters used
by cutscenes. The game never looks at rings, enemies or items other than upgrades.

**Decompilation**: everything above that runs in a character world is C and lowered: look tasks, `8C07AEF8` /
`8C07AF8C`, the setters, the timed looks, `8C056860` and `8C04B440` (event work). The object callers are asm (stage
objects, overlays). Nothing has to be decompiled for a host-driven look.

### 3.2 What libsa1 worlds need for host targets

* The look runs today (3.3). What is missing is a way to set `ewp->look` from the host. Today only game objects do,
  and G-SA1 has none of them.
* Use the **point** look (2) refreshed every step for host targets, not the task look (3) on a proxy. Probe H: a removed
  proxy's task block (8C7EE950) was reused by the next proxy at the same address. A `look_tp` kept over a removal
  retargets silently whenever the slot is reused before the look task runs. Tails' and Big's tasks would also fault on
  `look_tp == NULL`.
* A look the game set itself (an animal's 300 steps) must win over the host's.
* E-102: nothing to drive (no look task in the game).

### 3.3 Reproduction in libsa1 (look_probe parts A-H)

* **A** (out_a.txt): one look task per character in a world. Sonic, Tails, Knuckles, Amy and Big run in mode 1. E-102
  has none. `ewp` is allocated (8C81EE90).
* **B** (out_b.txt): Sonic runs, then turns right. At the first turn pair `ang.y` is 16.5 and `ang_aim.y` 83.6, so
  `head_tgt` is -67, clamped to **-45**. The head then follows: -44.5, -25, -17.4 ... 0 after about 10 pairs.
* **C** (out_c.txt): `ewp->look = 2` poked between pairs, exactly what `8C07C318` writes. Point left / right: head
  +-45 (Sonic, Knuckles, Amy), +-33.8 (Tails, Big). Up: z -33.8. Down: +5.6. Behind: clamped. The eyes add up to
  38 degrees. Look off: back to mode 1.
* **D / F** (out_df.txt, out_f.txt): render comparison through `sa1_character_render` (what G-SA1 draws). Worlds with no
  look, look left and look right; F also pins the eyes so only the head turns.

  | Character | Idle action | Head only (F): triangles moved, max | With eyes (D) |
  |---|---|---|---|
  | Sonic | 1 | **0** (head keyed by action 1) | 24 (eyes), 3.5 HU |
  | Tails | 2 | 405 of 1237, 8.4 HU | 405 |
  | Knuckles | 1 | 482 of 981, 10.4 HU | |
  | Amy | 1 | 483 of 934, 8.4 HU | |
  | Big | 0 | **0** (dummy head node) | 8 (eyes), 2.3 HU |
  | E-102 | | (no look task) | 0 |

* **headkeys.py**: the actions whose motion keys the head node:

  | Character | Actions keying the head |
  |---|---|
  | Sonic | 1, 2, 5, 74 |
  | Tails | 5 |
  | Knuckles | 5, 24, 84 |
  | Amy | 9 |
  | Big | none |

  Every head node has `NJD_EVAL_UNIT_ANG` set, yet the look shows in unkeyed actions. So the game's motion player uses
  `obj->ang` for unkeyed nodes and the keys otherwise. This was measured in libsa1, which runs that game code lowered;
  it is UNVERIFIED on a console.
* **G** (out_g.txt): mode 1 in the render while running and turning (world N has every look limit pinned to 0):
  * Sonic (actions 10 / 11): 490 of 1003 triangles move, up to 12.7 HU, one pair after `head_y` changes (the pair draws
    its first step).
  * Knuckles: 482 triangles, up to 11.8 HU.
  * Big: 0.

  **So G-SA1 already shows the head leading every turn for Sonic, Tails, Knuckles and Amy.**
* **H** (out_h.txt): a TARGET host object 30 units left and 10 up, `ewp->look = 3` on its proxy task. Head +45 left;
  after a move to the right, -45. After `sa1_hostobj_remove` the freed task block has twp 0, and Sonic's NULL test
  clears the look. The next proxy is allocated at the same address 8C7EE950. No fault.

### 3.4 Design

**libsa1 (`src/world/sa1_world_look.c`, new).**

```c
#define SA1_LOOK_OFF     0u  /* 8C07C376 (only a host-owned look is cleared) */
#define SA1_LOOK_POINT   1u  /* host-space point; 8C07C318 each step, lerped over the pair */
#define SA1_LOOK_HOSTOBJ 2u  /* host object id + offset: the proxy's latched position each step -> 8C07C318;
                                cleared when the object leaves the world's window or is removed */
#define SA1_LOOK_ANGLES  3u  /* yaw / pitch relative to the body: 8C07C34C */
#define SA1_LOOK_CAMERA  4u  /* the input's camera_position each step (the game's Z-button mode 2) */
#define SA1_LOOKF_OVERRIDE_GAME 0x1u  /* also replace a look the game set (default: the game's wins) */
typedef struct sa1_look { uint32_t struct_size, kind, flags, hostobj; float point[3]; float yaw, pitch; } sa1_look;
SA1_API sa1_status sa1_character_look(sa1_context *, sa1_character_id, const sa1_look *);
/* E-102: SA1_ERR_UNSUPPORTED (no look task in the game). */
```

* Latched and applied at the start of the next step, after the pad update, like `hostobj_pre`.
* LIB records which looks it set (host-owned) and leaves `ewp->look` alone while the game owns it.
* `sa1_state_ex` tail: `look_mode`, `head_angle[2]`, `eye_angle[2]` (BAMS, from the lookwk). The UI and network
  remote copies read them.
* Optional for remote puppets (LAN topic): `sa1_pose` gains `SA1_POSE_LOOK` plus head / eye angles, applied to the look
  nodes. Their indices follow from the creators' object arrays: Sonic node 11, Tails 4, Knuckles 10, Amy 10, Big 60
  (headkeys.py).

**G-SA1 (feature "look": `src/module/bind_look.cpp`, `feat/sh_look.lua`, `feat/cl_look.lua`).**

The target is chosen once per pair on the client, for the local character. Candidates:

* friendly / neutral NPCs and other players within 30 units (120 HU), in a front cone of +-100 degrees, with line of
  sight; the NPC-talk analogue;
* items: GMod pickups, weapons and spawned entities within 40 units (160 HU); the O_LevelItem rule;
* a defeated object's last position for 5 s after a KILLED hit; the freed-animal rule;
* the camera while a bind is held; the game's Z button.

The point looked at is `EyePos()` for NPCs and players, else the OBB centre. Hysteresis: 0.5 s before switching, at
least 1 s on a target. Hostile NPCs are not looked at by default (the game never looks at badniks).

Q menu, Options > G-SA1 > Head look:

* Off / Game only / Game + GMod things (default);
* a checkbox per category;
* the camera-look key binder.

The network state carries `look_mode` and the head / eye angles.

**Optional, game-faithful**: spawn the game's animal (`8C015720` -> `8C015820`, lowered) on host-object KILLED
removals. The game's own 5-s look then follows, but the animal's draws must reach the capture
(`libsa1_interaction.md` open question 5).

## 4. Fixes

| # | Fix | Files | Test (fails before / passes after) | Owner |
|---|---|---|---|---|
| F1 | Decompile FISHING `8C0239C2` (0x108) and the catch target `8C0EAD6A` (0xA0); optionally `8C01BD3C` (0x62). LIB policies for FISHING's other callees: `8C01B684` "ret 0" (no CAM table, which the game also tests first), `8C01E32A` void (the host traces) | `SADC/src/1st_read/` (the chunk files of 8C023360 and 8C0EAA80), `LIB/gen/policy/stubs.tsv:69, :154` | `harness diff 8C0239C2 --warm`, `harness diff 8C0EAD6A --warm`; SADC `configure.py --split` + ninja OK; LIB `build.py --relower --strict-stubs --test` strict (big_ec3) | SADC decomp agent "bigfish-decomp" |
| F2 | libsa1 camera requests: stand-ins for the six push / pop hooks, evaluation of `8C0EAF28` / `8C0EAA80` / `8C0239C2` in live worlds, seeding from new `sa1_input.camera_position / camera_target`, `camera_twp->pos` set, `sa1_character_camera` | `LIB/src/world/sa1_world_camreq.c` (new), `sa1_world_tick.c` (p), `sa1_world.h` (p), `sa1_world_api.c` / `sa1_api_world.c` (p), `include/libsa1.h` (p), `gen/policy/stubs.tsv:59-63` (p), `tests/camreq_test.c` + `tests/build_camreq.py` (new) | `camreq_test` (does not build today: no API). Cases on the 2.4 level: (1) Big mode 33 gives ACTIVE+KNOWN, level 2, `fn 8C0239C2`, adjust 3, target = the cursor, camera 50 units from Big; (2) lure mode 5 gives level 4, `fn 8C0EAF28`, and within 10 pairs \|camera - midpoint\| = 20.0 +- 0.1, every step's move <= 20 units, camera above water - 5; (3) L + R gives FISHING again with CUT; (4) rod away gives inactive; (5) Sonic's LSA request ACTIVE without KNOWN; (6) `build.py --test` strict unchanged; x86 = x64 | libsa1 agent "lib-camreq" |
| F3 | G-SA1 feature "gamecam": override view, blend, yaw hand-off, Q-menu page | `g-sa1/src/module/bind_gamecam.cpp`, `src/core/session.{hpp,cpp}` (p: input camera fields), `addon/lua/gsa1/feat/cl_gamecam.lua` (new), `cl_character.lua` (p: one-line seams at CalcView `:470` and the tick yaw `:373`), README (p) | `tests/feat_gamecam_test.lua` (mock): blend at 0 / 0.15 / 0.3 s, a view entity wins, the tick yaw is the request's while active, Off option ignores it, hull trace from target; `feat_gamecam_binding_test.cpp`; in game: cast into water, the view follows the lure, L + R returns | G-SA1 agent "gsa1-gamecam" |
| F4 | HUD with the lure in the water (0.9.1 follow-up) | `LIB/tests/hud_test.c` (p) or `camreq_test.c` | the 2.4 cast: 13 SPRITE2D (+ the POLY2D bar after F2 of 0.9.1) per pair in lure mode 5, line length digits changing; optional harness reference with a cast into the EC3 lake (`gsa1_070_refs.md` 8) | libsa1 HUD agent (after 0.9.1) |
| F5 | libsa1 look API (3.4) | `LIB/include/libsa1.h` (p), `src/world/sa1_world_look.c` (new), `sa1_world_tick.c` (p: apply at step start), `sa1_world.h` (p), `sa1_api_world.c` (p), `src/sa1_pose.c` (p, optional `SA1_POSE_LOOK`), `tests/look_test.c` + `build_look.py` (new) | `look_test` (no API today). Cases: (1) POINT left / right / up / down per character gives the 3.3 C angles; (2) render: Tails / Knuckles / Amy >= 400 triangles move, Sonic >= 490 in a run action and 0 in action 1, Big eyes only (8), E-102 UNSUPPORTED; (3) HOSTOBJ removed: look cleared in that step, not retargeted to the reused task (H); (4) an animal from `8C015820` keeps the look 300 steps over a host POINT; (5) no command gives a bit-identical arena; strict replays; x86 = x64 | libsa1 agent "lib-look" |
| F6 | G-SA1 feature "look": target selection, Q-menu page, network angles | `g-sa1/src/module/bind_look.cpp`, `feat/sh_look.lua`, `feat/cl_look.lua` (new), `cl_character.lua` (p: state fields), README (p) | `tests/feat_look_test.lua`: range / cone / LOS / hysteresis / 5-s defeat look / hostile excluded / camera key; binding test | G-SA1 agent "gsa1-look" |
| F7 | (optional) the game's animal on KILLED removals | `LIB/src/world/sa1_world_hostobj.c` (p) | hostobj_test: KILLED gives an animal task and the look task in mode 3 on it for 300 steps | libsa1 host-object agent |

## 5. Owner questions (with defaults)

1. Big's fishing camera: "as the game" (behind Big while aiming, following the lure in the water, the catch shot) or
   "lure only"? Default: as the game.
2. While the game's fishing camera is shown, is the mouse locked (as the game: the stick moves the lure relative to that
   view) or does it orbit around the lure? Default: locked, orbit as an option.
3. What should characters look at? Default: friendly / neutral NPCs and players within 120 HU, items within 160 HU,
   what you defeated for 5 s, the camera on a key (unbound). Hostile NPCs too? Default: no (the game never looks at
   enemies).
4. E-102 never turns his head in SA1 (his look code exists but is never started). Enable it in G-SA1 as an option?
   Default: off (as the game).
5. Big's look moves only his eyes, because the game hangs his look on an empty node. Keep (default), or a PC-only head
   turn?

## 6. Open / UNVERIFIED

* `8C0239C2` = `8C0EAA80`'s placement plus `8C01B684` / `8C01BD3C` / `8C01E32A`: a reading of the Ghidra output. To be
  confirmed by F1's harness diff.
* `8C0EAD6A`'s target behind Big, so the catch camera faces Big: a reading.
* Adjust 3 SLOW's constants (v1_triggers 1.5).
* The camera task's order relative to the player.
* The O_PEOPLE trigger (talk start / message end) and the O_TIKAL distance: readings of asm / Ghidra.
* Event-library looks in cutscenes not searched exhaustively.
* Sonic's head not turning in idle actions 1 / 2 / 5 / 74, and Big's head never turning: measured in libsa1 (game
  code), not on a console.
* The lure under the water surface vs the 0.9.0 water rendering issue: not seen in game.

## 7. Experiments (`SADC/build/research/bigfish/`)

Build and run:

```
py -3.12 lockcopy.py
py -3.12 -m ziglang cc -target x86_64-windows-gnu -std=gnu11 -Isnap/include -Isnap/src -Isnap/src/world -I../../../include
  -Wall -O2 -g -ffp-contract=off -frounding-math -fno-builtin -DSA1_STATIC look_probe.c snap/libsa1.a -o look_probe.exe
look_probe.exe <GDI> ABCDEFGH
```

| Part | Output |
|---|---|
| A | out_a.txt |
| B | out_b.txt |
| C | out_c.txt |
| D, F | out_df.txt, out_f.txt |
| E | out_e.txt |
| G | out_g.txt |
| H | out_h.txt |

* `xcallers.py <ADDR...>`: callers / pool loads of an address in every image's xrefs.
* `headkeys.py`: which actions key the head nodes (reads `baserom/1ST_READ.BIN`).
* `part_f.c`, `part_g.c`, `part_h.c`, `patch_*.py`: the probe's later parts, spliced into `look_probe.c`.

## Review (adversarial)

Adversarial pass, 2026-10-25. It is read-only like the rest of the doc and uses the same snapshot (`snap/libsa1.a`, 09:34).
New files are in `SADC/build/research/bigfish/review/`:

* `rerun_{a..h}.txt`: `look_probe.exe` run again;
* `review_probe.c` / `.exe`: parts L, F, T and A, built with the section 7 command and `../snap` paths. Output is in
  `out_review.txt` (L, F, T) and `out_review_a.txt` (A);
* `ptrscan.py`, `poolscan.py`, `poolscan2.py`, `camtab.py`, `nodes.py` and `masks.py`: reads of `baserom/`.

### R1. Verified

* **Reproduction.** Parts A, B, C, E, F, G and H give output identical to `out_*.txt`. Part D matches the D half of
  `out_df.txt`. Every number in 2.4 holds: |camera - midpoint| is 20.0 from p90 to p190, |camera - lure| is 15.1-16.5, the
  camera's y is -3.4 to -4.9 over water at -5.0, and the largest step is 214.89 (the L + R cut). There are 9 / 13 2D records
  per pair and no fault.
* **C1, LIB and G-SA1 side.** The stub lines are as cited: `stubs.tsv:59-63` (void), `:69` (address-only) and `:154`
  (void). In `cl_character.lua`, `:373` gives the tick yaw = `EyeAngles().y` and `:470-499` is CalcView. The installed
  0.9.0 file is identical to the repo file (SHA-256 B196394C...), and the addon has no other CalcView hook.
  `D_8C112048` = 8C6B6EEC in `baserom/1ST_READ.BIN`.
* **G-SA1 worlds request FISHING.** G-SA1 never passes a level, so worlds boot CurrentLevel 1 (`sa1_api_world.c:336`).
  `func_8C04C960` returns 2 there (`code_8C04C960.c`), so `8C0E6586` calls `8C01AD2C(8C0239C2, 3, 2)`
  (`code_8C0E6500.c:54-57`), not the adventure-field `8C01ABDC(8, 5)`.
* **The game's camera calls are as in 2.1.** Checked: `code_8C0E0E70.c:557, 733`, `code_8C0E91E4.c:166-201, 408-419,
  432-457, 509-516` and `code_8C0E6500.c:61-68`. There is one more site, `code_8C0E0E70.c:851-853` (case 45, the power-rod
  copy of case 37). The camera functions read as 2.1 says. Where the source comments disagree, the doc is right: `8C0EAB68`
  makes the offset at least 30 (`code_8C0EAA80.c:92-96`), and `8C0EAF28` sweeps when the camera is more than 5 units below
  the water (`code_8C0EAEC0.c:90`).
* **Ghidra readings, still UNVERIFIED.**
  * `8C0239C2` places the camera like `8C0EAA80`: it copies `.tgt`, normalises `v = Big - tgt + (0, 20, 0)`, scales by
    50 (0x42480000) and adds 20 to y. It then calls `8C01B684`, `8C01BD3C` and `8C01E32A` (the last two on a copy of
    `camera_twp->pos`) and clears +0x2C.
  * `8C0EAD6A` turns (100, 0, 0) by Z, X and Y(0x8000 - ang.y). P2G turns by Z, X and Y(-ang.y) (`code_8C078E40.c:57-59`).
    So the target is 100 units behind Big.
* **E-102's look task is dead code.** I scanned the bytes of `1ST_READ.BIN` and the 257 overlay files for 8C0D1916 and its
  0xAC / 0x0C mirrors: 0 hits. As a control, the scan does find 8C0B28EE, 8C0BE1CE and 8C0E5DF0 in 1ST_READ's literal
  pools. `xcallers.py 8C0D1916` returns nothing.
* **The look setters and their callers.** `xcallers.py` on the seven setters gives the 3.1 table exactly. In the C, only the
  setters write `ewp->look` (grep). Tails and Big read `look_tp->twp` without a NULL test (`code_8C0BE160.c:201`,
  `code_8C0E5D20.c:216`). Sonic tests both pointers (`code_8C0B2890.c:253-265`).
* **Big's head node.** 8C529520 has evalflags 0x1E (UNIT_ANG | UNIT_SCL | HIDE | BREAK), model 0 and child 0.
* **The look in the render.** Parts F and G give the same numbers again. G-SA1 renders at alpha < 1, and that path
  interpolates each model's transform between the captured pairs (`sa1_character.c:2203-2218`). So the look still shows
  when interpolated, although the probe measured only alpha 1.
* **0.9.1's scope.** `gsa1_round_091.js` has no camera hook, no CalcView change and no look work. 091-gsa1-char does patch
  `cl_character.lua`, but for death, lives, the jukebox and options. These HUD lines are gone from `stubs.tsv`: 8C0EB160,
  8C0E6FE0, 8C0E70E4, 8C0E746C, 8C0E74FC, 8C08EE98 and 8C04C960. Their C is in `src/1st_read` (09:20).

### R2. Refuted or corrected

1. **Head and eye turn speed (3.1).** 0x1000 BAMS is **22.5 degrees** per step, not 5.6. The eyes' 0x800 is 11.25 degrees
   (0x10000 BAMS = 360 degrees). Measured with `review_probe T`: in the first turn pair, Sonic's head_y goes 0 -> -22.50 ->
   -45.00, one step each.
2. **"camera_twp->pos is never set" (2.3, 2.5 item 3).** It is written on every live step, with zeros.
   `sa1g_camera_refresh` copies `w->camera.pos` (`sa1_world_tick.c:63`). `sa1_world_image.c:512` zeroes that field and
   nothing assigns it; only `camera.ang[1]` is set (`sa1_world_api.c:193`). Measured with `review_probe L`: camera_twp
   8C81E900 has pos (0, 0, 0) on every logged pair. The doc misses these consequences:
   * **Big's fishing line is drawn faint, which the owner sees.** Each of the 12 line vertices gets alpha
     `255 - |pt - camera_twp->pos|^2 / 4`, clamped to 0x40..0xE0 (`code_8C0EC212.c:167-192`, `code_8C0EC180.c`).
     Measured line-vertex alpha in `sa1_character_render`, lure in the water: 64..104 with the island at the host origin,
     and **64 on every vertex** with the island 8000 HU away. So on real maps the line is always 25 % opaque. In the game,
     the lure end of the line is 15-20 units from the lure camera, which gives alpha 0x9B-0xC7.
   * Big's own write to `camera_twp->pos` (`code_8C0E6500.c:41-45`) is overwritten on the next step.
   * `8C0EAF28` starts its sweep from the SA1 origin (`cpos`, `code_8C0EAEC0.c:61, 91`). `8C01BD3C` and `8C01E32A`
     also start from the SA1 origin.
3. **8C01BD3C's 30-step blend (2.1, F1).** Its counter 8C18B86C has no writer. In 1ST_READ, the only literal in
   8C18B820..8C18B86C is at 8C01BEE8, which is 8C01BD3C's own pool (`poolscan2.py`). `xcallers.py` lists only 8C01BD3C,
   and the counter's initial value is 0. The function decrements the counter only when it is nonzero, so the blend never
   runs. This is UNVERIFIED only for GBR-relative stores. A `void` policy is exact, so decompiling 8C01BD3C gains
   nothing.
4. **The reason given for 8C01B684 "ret 0" (F1).** G-SA1 worlds are level 1 act 0, which has a CAM table: the pointer is
   0x0CB50000 (`8C1958B4 + 0x20`, `camtab.py`). So the real function would not return early. It would dereference
   `*8C6B7060`, which is NULL in libsa1 (BSS; no camera task, no CAM file). The policy is still right in effect: GMod maps
   have no CAM volumes, and 8C0239C2 ignores the result, so `void` would also work. But it is a host decision, not the
   game returning early.
5. **F2 test (1) "camera 50 units from Big".** The camera is 50 units from Big + (0, 20, 0), not from Big. Measured:
   |camera - Big| = 53.9 at p23 and 57.7 at p40, while |camera - (Big + (0, 20, 0))| = 50.0. A test written as stated
   fails on correct code.
6. **The stand-in model (2.5 item 1).** The game's push and pop functions (Ghidra) work like this:
   * `8C01AD2C` does nothing when the current level is 3 or more. `8C01AF06` does nothing at level 5.
   * When a push changes the level, it saves the state it covers in that level's slot: level, entry, flags, mode,
     function, and the adjust to return with. That adjust is the CAM entry's own, or **0x15 when no entry is active**.
   * `8C01AE60` and `8C01B158` act only when the level is 2 or 4. They restore the saved slot, so the camera returns to
     the previous one with **adjust 0x15**, a timed blend (`8C02535E`), not a cut.
   * Every push and pop also calls `CreateElementalTask(8, 6, <thunk to FreeTask 8C098786>)`, a level-6 task that frees
     itself. Its purpose is UNVERIFIED.

   For Big's sequences, the top request comes out the same. But the stand-ins re-implement existing game logic (R4.2).
7. **"WEIGHT (best fish)" (2.6).** The HUD prints `B_8C7A40D8` (`code_8C0E6FE0.c:84, 138`). `8C0EBA80` **adds every
   catch** to it, capped at 99,990 (0x18696). The best single fish is `B_8C7A40DC`, which the score screen uses
   (`code_8C08ABC0.c:285`). This is a Ghidra reading (UNVERIFIED); the C comment "best fish weight" makes the same
   mistake.
8. **"0.9.1 covers Big's fishing HUD in full."** It does not:
   * WEIGHT changes only through the catch-result task, and no 0.9.1 agent lands it. `8C0EBD22` (still
     `stubs.tsv:156`, void, "display only") creates exec 8C0EBBDE. That task runs 120 steps and draws a 2D sprite with
     8C0EBAB2 at (604, 40), scale 2. Its destructor 8C0EBBA0 calls 8C0EBA80.
   * There is no fish to catch (R3.1), so WEIGHT stays 0.
   * The doc quotes the verifier as checking "Big's WEIGHT and the reel / gauge while fishing". That sentence is in
     neither the workflow nor the README. The verifier compares the 2D draws with the hud_* references
     (`gsa1_round_091.js:126`), and `hud_big_ec3` never has the lure in the water.

   What 0.9.1 does cover for Big: the HUD task and its draws (091-land), POLY2D and `sa1_character_screen`
   (091-lib-hud), and the G-SA1 HUD layer (091-gsa1-hud F8).
9. **8C021700 (2.5 item 4).** It is the death camera of every character (`8C07BC0E`, `code_8C07BAC0.c:173`). It is also
   Sonic's Light Speed Attack camera (`code_8C0B11C0.c:133`).
10. **Probe H's "retarget hazard"** is inferred, not measured. After the removal, Sonic cleared the look. The proxy that
    reused the task at 8C7EE950 was **not** looked at (ev.look 0). Keep it as an UNVERIFIED hazard.

### R3. Gaps

1. **A host world has no fish (measured, `review_probe F`).** The lure stays in the water for 1,218 pairs. `B_8C7A40CC`
   is 0 throughout, and `B_8C7A40E8` never goes beyond 0x40. The fish are stage objects: `objexec_S_GENE` in STG01, 03,
   08 and 12, and Froggy, `objexec_S_KAERU` 8C0ED398 (`xcallers.py 8C7A40CC`). So in G-SA1 the catch shot, the catch
   result, WEIGHT, a line break and fish tension never happen. F2's "catch" and F3's "As the game (aim, lure, catch)"
   cannot be tested in game. Tell the owner that fishing in GMod is cast, watch and reel in, until fish can be spawned.
2. **F2 item 3 affects every reader of `camera_twp->pos` in live worlds.** The readers:
   * `8C0EC60C`: the line alpha (the wanted fix);
   * the Z-button eye / head looks (mode 2 of `8C0B28EE` and `8C0BE558`);
   * `8C049160`: a follower respawns above the camera (round 6b);
   * `8C1032AC`: E-100 Zero;
   * `8C052518`: the dynamic collision list, only when there is no player;
   * `8C0520C8` and `8C0869E0`: display lists;
   * `8C055E40`: Sky Chase only.

   F2 needs this list and a test that live play of the six characters is unchanged except where intended.
3. **F3's transitions are host-made.** Its 0.3 s blend replaces the game's adjusts. In the game, FISHING comes in with
   adjust 3 SLOW (`8C025010`). The lure camera and L + R cut (adjust 0). A pop returns with adjust 0x15 (`8C02535E`).
   The owner must approve the host blend, or LIB runs the decompiled adjusts on the camera work: `8C024D8E`, `8C025010`
   and `8C02535E` (about 1 KB) plus `8C025482` / `8C025524`. Only the switch between the chase camera and the game
   camera has no equivalent in the game.
4. **F3's hull trace "from the target" is wrong for FISHING and the catch shot.** Those targets are up to 170 units
   beyond Big, or 100 units behind him, so the trace crosses Big and whatever lies behind him. For the `8C0239C2` and
   `8C0EAA80` placements, trace from Big + (0, 20, 0). Trace from the target only for `8C0EAF28`. Another option is to
   mirror `8C01E32A`: trace from the previous camera to the new one.
5. **F3 in game depends on the render topic.** The lure camera sits 0.4-6.4 HU above the water, looking at a lure 1-13
   units below it. In 0.9.0, the refraction view clips the draws (`gsa1_090_render.md` item 2). F3's in-game test must
   wait for that fix. It is also UNVERIFIED how Source renders when the eye is within a few HU of water.
6. **Hand the yaw back.** When the game camera ends, F3 must set the player's eye yaw to the game camera's yaw, or the
   view and the stick frame snap. Add this to the test.
7. **Seams by line number.** 091-gsa1-char patches `cl_character.lua`, so `:373` and `:470` will move. Anchor the seams
   on `tick_character`'s `local yaw =` and on the `"gsa1_view"` hook instead.
8. **Input names.** F2's `camera_position` / `camera_target` duplicate v1_triggers 4.4's `host_cam_pos` /
   `host_cam_target` (`v1_triggers.md:557`). Use one pair of names.
9. **F4 must reel.** The line length changes only while the reel button is held: `D_8C19E390` = 0x0004, the A / jump
   button (`8C0E80A6`). The 2.4 inputs never reel.
10. **F7, and F5 test (4), stop the world (measured, `review_probe A`).** I spawned `8C015720` 20 units from Sonic. Sonic
    looks at the animal (ev.look 3, look_tp 8C7EEA10). At pair 46 the tick fails: "func_8C017C82 has no implementation
    in this build (fatal stub)". The animal code (`code_8C015EE0.c:67-71, 89`) calls 8C017C82, 8C017B20 and 8C026D40,
    all three fatal stubs (`stubs.tsv:54, 55, 70`).
11. **Sonic looks with his eyes only while standing.** Actions 1, 2 and 5 key the head (headkeys.py), so looking at an NPC
    moves 24 triangles by at most 3.5 HU (part D). The game does the same, but this is the most common look case. Add an
    owner question.
12. **How LIB knows which look is the host's.** It must compare look, pos and tp, not only `ewp->look`. Otherwise a game
    look 2 written over a host look 2 is taken for the host's.

### R4. Required changes

1. Correct the points in R2:
   * the turn speeds (22.5 and 11.25 degrees per step);
   * `camera_twp->pos`: written as zero, the faint line, the overwritten write, the sweep origin;
   * 8C01BD3C: a `void` policy and no 30-step blend;
   * the reason for 8C01B684's policy;
   * F2 test (1): 50 units from Big + (0, 20, 0), ± 0.01;
   * WEIGHT is the total caught weight;
   * 0.9.1's coverage: not 8C0EBD22;
   * 8C021700 (R2.9) and probe H (R2.10).
2. **The owner rule applies to F2.** Choose one of:
   * (a) F1 also decompiles the six push and pop functions (8C01ABDC, 8C01AD2C, 8C01AE60, 8C01AF06, 8C01B040,
     8C01B158; about 1,400 bytes). LIB runs them in live worlds on the game's control work `*(8C11204C)`, and
     `sa1_character_camera` reads +7, +8, +9 and +0xC from it. Their task creation must be checked, and the strict
     replays are the gate.
   * (b) The owner approves the stand-ins.

   Do the same for F3's blends. Land F1 before F2 instead of running 8C0EAA80 in FISHING's place.
3. F2 needs:
   * the list of readers and a live-play test;
   * a line-alpha case: with the camera 15 units from the lure, the lure-end vertices have alpha >= 0x9B; with the
     camera 2,000 units away, 0x40.
4. F3 needs:
   * the trace origin for each camera;
   * the yaw hand-back;
   * seams anchored by name;
   * the render-topic dependency for the in-game test.
5. F4: hold the A / jump button while the lure is in mode 5.
6. Before F7: decompile or add policies for 8C017C82, 8C017B20 and 8C026D40 (an SADC decomp agent plus the libsa1
   host-object agent). F5 test (4) must use a task whose exec is harmless.
7. Add two owner questions:
   * there are no fish in GMod (no catch, WEIGHT stays 0) until fish or Froggy can be spawned;
   * Sonic standing: eyes only (as the game) or a PC option to turn the head.
8. Use one set of input field names with v1_triggers.

### R5. Verdict

* **C1 holds** and matches what the owner sees. The same zero camera position also makes the line faint.
* **C2 holds** for the drawn HUD, but not "in full": 8C0EBD22 is still a stub, and there are no fish.
* **C3 holds:** no look target exists on a GMod map.
* **C4 holds,** with the turn speed corrected.
* **Fixes:**
  * F1 is smaller than proposed (8C01BD3C is dead code), and it should also take the push and pop functions.
  * F2's direction is right: the game's own 8C0EAF28 reproduces the lure camera from libsa1 data. But its stand-ins and
    F3's blends need the owner's approval or decompilation, F2's test (1) is wrong, and the camera position change
    reaches beyond Big.
  * F3 needs the trace and yaw fixes, and the water render fix before it shows anything.
  * F7 crashes as written.
