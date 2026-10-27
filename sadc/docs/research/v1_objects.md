# v1 objects: collectables and world objects for G-SA1 1.0

Research and design for the owner's 1.0 roadmap item "Game objects, in a G-SA1 tab of the Q menu: collectables (rings,
lives, item boxes, shields, ...) and world objects (springs, dash panels, trampolines, numbered jump panels, stage end
capsules, ...)". Read-only round (2026-10-21): nothing in `sadc/src`, `libsa1` or `g-sa1` was changed. Triggers,
splines, badniks/bosses and NPCs are other v1 topics; this document only cross-references them.

Sources: Ghidra pseudo-C (`build/ghidra/1ST_READ/decomp`), the decompiled C in `src/1st_read`, `build/*.xrefs.json`,
`docs/research/sa1_code_map.md`, `docs/libsa1_interaction.md` (CCL, host objects, the addendum's spring/balloon
research), the user's disc (`baserom/1ST_READ.BIN`, `baserom/overlays/*.BIN`, `disc/SONICADV/SET*.BIN`) and the
game's own **edit-mode strings** (the developers' debug labels for each object's parameters, `reference/strings_1st_read.txt`).
SADX / SA Tools names are orientation only. Addresses are 1ST_READ (US 1.005) unless an overlay is named; "B" = bytes
of SH-4 code; "M" = matched C, "NM" = NON_MATCHING C, "todo" = no C. Byte counts below are **clipped** sizes (a
function's size cut at the next function start; the splitter's raw sizes overlap at pool-island pseudo-functions).

Scratch and reproducibility: `sadc/build/research/objects/`
* `status.py IMAGE SRCDIR RANGE...` - per-function decomp status (same rules as `tools/progress.py`, temp files kept
  in the scratch directory instead of `src/`); `effort.py` - per-object clipped byte totals;
* `objlists.py` - parses every stage object list in the decompressed overlays and every SET file on the disc
  (`objlists.json`, `objlists.txt`, `set_usage.json`, `stage_objs.txt`);
* `summarize.py` - callees / data / constants per object cluster (`clusters.txt`); `ccl.py ADDR:N` - dumps CCL_INFO
  tables from the user's 1ST_READ;
* `traces/` - four harness oracle runs of real objects (section 6).

---------------------------------------------------------------------------------------------------------------------

## 0. Summary

* **Where the code is.** Every object a sandbox player wants first is **SA1 common code in 1ST_READ**: the common
  stage objects `8C059A80-8C06AF00` (422 functions; 1.6 KB matched, 1.7 KB NM, **397 functions / 55.8 KB todo**),
  the rings `8C0869E0-8C088120` (21 functions, **all have C**: 1.4 KB matched, 4.0 KB NM), the object helpers
  `8C0586A0-8C059A80` (26 todo / 2.7 KB), the path-work objects (loops) `8C06AF00-8C06C0C0` (15 todo / 4.4 KB), the
  SET manager `8C090EC0-8C091C20` (26 todo / 2.9 KB) and the barrier/invincibility displays `8C043C00-8C0445A0`
  (11 todo / 1.6 KB). Their models, CCL tables and motions are 1ST_READ data (already in every libsa1 world's arena);
  their textures are `OBJ_REGULAR.PRS` (texlist 0x8C19F530, 96 textures), which libsa1 already loads for the shadows.
* **What a placed object is.** A task made by `CreateElementalTask(im, level, exec)` from the stage's object list,
  with `taskwk.pos/ang/scl` copied from a 0x20-byte SET record; `scl` (and on some objects `ang.x`) are the
  **parameters** (the game's edit mode names them: "SCL_X IS FOR SPEED", "<- ADD YSPD", "<- NO TIMER", "<- KIND",
  "<- ID", "Target Position", "-> PANEL NUMBER", "<- NUM MAX 7", ...). It talks to the character only through the
  CCL hit fields and the decompiled **player API** (SetInputP orders, SetVelocityP, the spring and dash setters, ring /
  life / item functions), all of which already have C.
* **Measured on the disc.** 156 SET files, 43,091 placements; 62 distinct 1ST_READ object types and 1,153 overlay
  types across 33 parsed object lists; 1ST_READ objects are 38.7 % of all resolved placements (rings 4,286, ring groups
  916, dash panels 527, item boxes 450 + 293 air, air springs 271, checkpoints 93, ground springs 76, ...).
  The harness oracle ran four real objects from the user's disc (dash panel, item box, ring, air spring): the measured
  launches match the decompiled player API exactly (section 6).
* **Not in SA1.** There is **no trampoline** in the final game (the only one is `TRAMPOL` of the Windy Valley
  autodemo prototype) and **no common bumper or breakable box**: bumpers/flippers are Casinopolis-only (STG09),
  breakable boxes are per-stage overlay objects (Ice Cap, Casinopolis, Sky Deck, Red Mountain, Lost World, Hot
  Shelter). All stage overlays are 0-8 % decompiled, and a libsa1 world holds **one** stage overlay at 0x0C900000, so
  stage gimmicks are a post-1.0 "stage theme" feature.
* **Recommendation (section 9).** 1.0 = rings (single, line/circle groups, the scattered rings that already work),
  item boxes and air item boxes with all 9 contents (speed shoes, invincibility, 5/10/random rings, shield, 1-up,
  bomb, magnetic shield), ground spring, air spring, wide spring, dash panel, numbered jump panels, checkpoint, switch,
  spikes, liftable crates and the Egg capsule goal. That is **~154 functions / ~18.7 KB of SH-4 to decompile**
  (about 14 % of the P0+P1 function count), a new libsa1 **object layer** (game tasks mirrored into every world, like the round-5
  host objects but running the game's own exec) and a G-SA1 spawn tab with per-object property editors. Rockets, the
  Tails dash ring, line rings, Amy's balloon, air bubbles, iron balls, upgrades, emblems, Knuckles' shards and
  Froggy follow in 1.1/1.2 (~210 functions / ~34 KB, plus Knuckles' hunt and Froggy's flow).

---------------------------------------------------------------------------------------------------------------------

## 1. How SA1 runs a placed object

### 1.1 Object lists (per level, in the stage overlay)

`8C1C143C[level * 8 + act]` (1ST_READ) points at the act's object list; the lists live in the stage overlays
(SA Tools addresses: STG01 0x0CAA8F1C, STG02 0x0C9EBB68, STG03 0x0C9C2CF8, STG04 0x0CA79654, STG05 0x0CADAA9C,
STG06 0x0CAD9944, STG07 0x0CA5B1BC, STG08 0x0CABD830, STG09 0x0CB1AC2C, STG10 0x0CAFE9A0, STG12 0x0CA890FC..,
ADV00 0x0CA5E2C8, ADV02 0x0C9001C0, ...; 0x0C900BCC is the STG00 list used as a dummy for unused acts).
List = `{u32 count; ENTRY *entries}`, entry 0x14 bytes:

| Off | Field | Use (SET manager pass 8C091148 / 8C091380) |
|---|---|---|
| 0x00 | u8 `im` | allocation bits of `CreateElementalTask` (8C09852E): 1 motionwk, 2 taskwk, 4 forcewk, 8 anywk |
| 0x01 | u8 `level` | task list (executor order: level 1 players, 3 most objects, 6 E-102's lock search) |
| 0x02 | u16 flags | 1 = use `dist`; 2 = always in range; 4 = load on the first pass regardless of distance |
| 0x04 | f32 `dist` | **squared** load distance (default without flag 1: 160000 = 400 units) |
| 0x08 | u32 | copied to `taskwk.counter` |
| 0x0C | exec | the object's main (the `objexec_*` names) |
| 0x10 | name | string ("RING", "O ITEMBOX", "SPRINGB", ...) |

The 1ST_READ common objects use the same `im/level/dist` in almost every list (measured over all 33 lists,
`objlists.json`): RING 2/3, SPRING and SPRINGB 2/2, O_AXPNL 3/3, O_ITEMBOX 2/3, O_ItemBoxAir 15/3, O_JPanel 2/2 with
dist 4,000,000 (2000 units, the whole chain stays loaded), O_Save_Point 15/3, O_SWITCH 6/3, Rockets 14/2, CMN_DRING
14/3, O_TOGE 2/3, O_RELEASE 2/3, O_BUBBLES 2/4 dist 40000. A generated table of these (allowed in the DLL: addresses
and generated tables) is all a host needs to make the same tasks.

### 1.2 SET records (`disc/SONICADV/SETllaaX.BIN`)

156 files; `ll aa` = level and act (`SS`, `EC`, `MR`, `PAST` = adventure fields, `E101`, `EGM1..3`, `ZERO`, `MCART`,
`SBOARD`, `SHT`, `GARDEN`, `AL_RACE` = the rest); `X` = character set: S Sonic, M Miles, K Knuckles, A Amy, E E-102,
B Big, L (adventure fields only; Last story, UNVERIFIED). Header 0x20 bytes (u32 count, the rest zero in all 156
files), then 0x20-byte records:

| Off | Field | Goes to |
|---|---|---|
| 0x00 | u16 type | bits 0-14 = index into the level's object list (`& 0x7FFF` in 8C091148); bit 15 is set on 2,423 of 43,091 records, meaning UNVERIFIED (the manager masks it off) |
| 0x02 | s16 rot x, y, z | `taskwk.ang` (BAMS, sign-extended) |
| 0x08 | f32 x, y, z | `taskwk.pos` |
| 0x14 | f32 sx, sy, sz | `taskwk.scl` = **the object's parameters** |

Act load (8C091016): the SET file is read to a fixed buffer (0x0CB68000 / 0CB70000 / 0CB78000 / 0CB6C000 / 0CB74000
/ 0CB7C000 by act, table 8C1C0EDC) and every record gets a 16-byte **status entry** in `B_8C79A3E0` (at most 1024 per
act, 8C0916EE). An object's `tp->ocp` points at its status entry (`OBJ_CONDITION`):

| Off | Field | Meaning |
|---|---|---|
| 0x00 | u8 | load counter |
| 0x01 | u8 | object bits (O_GRING: one bit per collected ring, 8C091C00 / 8C091C10) |
| 0x02 | u16 flags | 0x8000 listed, 1 task alive (DestroyTask 8C098752 and 8C091B8A clear 1 and 0x8000), 2 transform saved at +0xC (8C091A92), **4 consumed** (set 8C091B54, test 8C091B68: a broken item box reloads broken, a touched checkpoint reloads touched), **8 never range out**, 0x10 (8C091B5E, item boxes and checkpoints at init), 0x4000 (kept across a reload, UNVERIFIED) |
| 0x04 | task * | the live task |
| 0x08 | SET record * | |
| 0x0C | f32 / ptr | range-out distance squared (= list dist + 100) or the saved transform |

### 1.3 The life of an object task

* **Range.** Each exec starts with `if (8C0919CC(tp)) return;` - 8C0919E8 tests the squared distance of
  `twp->pos` to the camera task `D_8C18B7EC` and to player 0 against `ocp+0xC` (no ocp: 410 units), and to player 1
  within 40 units when its character id is 0 (UNVERIFIED reading of the Ghidra output); when nobody is near it sets
  `tp->exec = DestroyTask` and returns 1; ocp flag 8 disables it. libsa1 stubs 8C0919CC as "ret 0" (always in range)
  today.
* **Init (mode 0)** sets `tp->disp` / `tp->dest`, `CCL_Init(tp, table, id, 1, n)`, reads `scl`, sometimes registers
  dynamic land collision (`8C052A1E` + DynamicCOL_Add `8C0528AE`, C) so characters stand on it.
* **Every step** it reads the CCL hit fields of the last pass (`cwp->flag & 2`, `cwp->hit_cwp->mytask`,
  `cwp->my_num` = own shape index, `twp->flag & 4` = out-levelled by an attack), acts through the player API, then
  calls `CCL_Entry` (8C02F63C). CCL ids used by objects: 2 (item boxes: in the homing / Light Speed Attack target
  lists and E-102's lock-on), 4 (most objects: solid or trigger shapes), 7 (rings: `twp->id = 11` makes them light
  speed dash targets). List capacities per step: id 2 128, id 4 512, id 7 256 (libsa1_interaction.md 1.1).
* **Death.** `8C091BA4` (C): `ssCondition &= ~0x8001`, `exec = DestroyTask`; DestroyTask runs `tp->dest`.
* **Display.** Through the stage texlist `B_8C79F0F0` (8C099500, C: OBJ_REGULAR), `njPushMatrix / njTranslateV /
  njRotate* `, the fast basic-model renderers 8C036F12 (C) / 8C03700C / 8C038CEC (libsa1 rt, captured), the motion
  draw 8C040942 (NM C), sprites (njDrawSprite3D, rt). The visibility test is 8C038D00 (rt, exact).
* **Stage start** calls the common-object initialiser **8C058E48** (from 8C04E660; todo, 120 B + 9 small callees
  ~0.4 KB): allocates the object manager block, creates its level-3 task (8C058F62), and resets the per-act globals -
  the jump panel list (8C05E680), the rocket state (8C0635C0), the capsule pointer (8C062634), the dynamite list
  (8C05B1DA), the 32 switch flags (8C067DF4) and the item-icon popup (8C05E0CE). libsa1's boot does not run it
  yet.

---------------------------------------------------------------------------------------------------------------------

## 2. What objects do to a character (all C today)

| Function | What it does | Used by |
|---|---|---|
| `SetInputP` 8C07C65E (M) | event request: sets `twp->flag` bit 0x1000 and `smode = n`; the character's next-action handler turns it into a mode | everything below; orders seen: 3/8 spring, 4 air spring, 5, 6/7/8, 9/10/12, 11, 12 park (balloon, dash ring for Knuckles), 19 stage-clear pose (8C04EFE0), 20 grab rocket, 24 release, 25 wide spring, 28, 30/32 hold, 31 path, 35 launch, 36 breathe (air bubble), 0x31 = 49 dash ring flight (Tails) |
| `SetVelocityP` 8C07C676 (M) | world speed into `mwp->spd`, player-space copy into `pwp->spd` | item box top bounce (0, 2, 0) |
| 8C07C720 (M) | smode 3 + SetVelocityP (the ground spring) | SPRING |
| 8C07C768 (NM) | smode 4, player-space speed (0, s, 0), rotation, no-control time | SPRINGB |
| 8C07C8E0 (NM) | player-space speed, rotation `0x4000 - ang.y`, no-control time | dash panel |
| 8C07C952 (NM) | the same with smode 25 | SPRING_H |
| 8C07CB6A SetPositionP, 8C07C9FA rotation from a vector, 8C07256A PClearSpeed (M) | | rockets, dash ring, capsule |
| 8C07CD16 / 8C07CD96 (M) | first player inside a sphere / vertical cylinder | switch, iron balls |
| 8C07D1D0 (M) | player stands on task (pwp->ttp) | switch |
| `AddRings` 8C08A198 (M) | `B_8C752BE4 += n`; 1-up and its jingle every 100 | rings, item boxes |
| 8C08A120 (M) | lives `B_8C752BE0 += n`, SE 0x2C0 | 1-up box |
| 8C08A2A2 (M) | score | enemies |
| 8C07D3A0 / 3DC / 43E / 4A0 (M) | speed shoes (task 8C07D2DA doubles run_accel, max_x_spd, lim_frict for 600 frames, music 85) / magnetic barrier (item 2, task 8C043C9E) / barrier (item 1, task 8C044060) / invincibility (item 0x8000, task 8C044284, music 55) | item boxes |
| 8C086F5E (NM) | hurt: scatter up to 20 rings (tasks 8C0870EA, CCL id 7, collectable again), rings to 0, or death with 0 rings | the character mains (works in libsa1 now) |
| 8C109EAC | rumble; libsa1 turns it into a RUMBLE event | springs, dash panel, capsule |
| 8C0940DA | SE: 7 ring, 9 spring, 0x0A checkpoint, 0x0B item box, 0x0D capsule button, 0x2BB dash panel, 0x2C0 1-up, 0x2DA capsule open, 0x2DC dash ring; E-102 voices 0x4AC / 0x4AD | |

**Gap:** the shield and invincibility **display tasks** are C (8C043C9E NM, 8C044060 NM, 8C044284 M) but their callees
8C043C00, 8C043D66, 8C043E70, 8C043FEC, 8C044122, 8C044166, 8C044280, 8C044290, 8C04430A are **fatal stubs** in
`LIB/gen/policy/stubs.tsv` (round 5 avoids them with a "phantom barrier" without its display). Item boxes that grant
shields need them decompiled (11 functions, ~1.6 KB with 8C044480 / 8C0444C4).

---------------------------------------------------------------------------------------------------------------------

## 3. Catalogue overview

Placements = SET records on the disc that resolve to the object (all characters, all acts). Functions / bytes = the
object's cluster in code order (clipped sizes, `effort.py`).

| Family | Object (list name) | exec | Cluster | Fns | C now | Todo | Placements |
|---|---|---|---|---|---|---|---|
| Rings | RING | 8C0869E8 | 8C0869E0-8C087B80 | 14 | 9 M + 5 NM (4.2 KB) | 0 | 4,286 (109 files) |
| | O_GRING (ring group: line / circle) | 8C087B80 | 8C087B80-8C088120 | 7 | 2 M + 5 NM (1.2 KB) | 0 | 916 (34) |
| | O_LINERING / O_LINERING_V (switch-gated ring lines) | 8C061C40 / 8C062020 | 8C061C40-8C062620 | 10 | - | 2,242 B | 68 / 2 |
| | scattered rings (hurt) | 8C0870EA | in RING | | C | 0 | - |
| Items | O_ITEMBOX | 8C05D10C | 8C05D0A0-8C05D9C0 | 18 | - | 2,004 B | 450 (45) |
| | O_ItemBoxAir | 8C05D9F6 | 8C05D9C0-8C05E680 | 19 | 4 M + 2 NM (1,050 B) | 1,684 B | 293 (42) |
| | shield / invincibility displays | 8C043C9E.. | 8C043C00-8C0445A0 | 14 | 3 (406 B) | 1,572 B | - |
| | O_LevelItem (upgrades) | 8C0698F6 | 8C0698C0-8C06AF00 | 21 | - | 4,790 B | 51 (35) |
| | O_EMBLEM | 8C0A6B78 | 8C0A6918-8C0A75A0 | 18 | - | 2,908 B | 48 (33) |
| Springs | SPRING (ground) + SPRINGB (air) | 8C065800 / 8C065EE4 | 8C065800-8C066320 | 7 | - | 2,620 B | 76 / 271 |
| | O_SPRING_H (wide "triple" spring) | 8C067E40 | 8C067E40-8C0682A0 | 5 | - | 894 B | 5 |
| Boost | O_AXPNL (dash panel) | 8C059B48 | 8C059A80-8C059E20 | 3 | - | 854 B | 527 (35) |
| | CMN_DRING (dash ring / rainbow hoop, Tails & Knuckles) | 8C05A360 | 8C05A360-8C05A940 | 8 | - | 1,290 B | 53 (Tails only) |
| | O_JPanel (numbered jump panels) | 8C05EB76 | 8C05E680-8C05F000 | 16 | 4 M + 1 NM (1,194 B) | 876 B | 45 (7) |
| | Rocket_H / HS / V / VS | 8C063100 / 220 / 360 / 480 | 8C063100-8C064980 | 31 | - | 4,792 B | 21 / 0 / 7 / 3 |
| | shared helpers (rockets, dash ring, grass, bubbles, hints) | | 8C066320-8C067480 | 45 | 5 M (298 B) | 3,700 B | - |
| Goals | O_RELEASE (Egg capsule) | 8C062640 | 8C062620-8C063100 | 26 | - | 2,142 B | 35 (9) + spawned by O_KAOSE, O_CLROBJ, C_RELEASE |
| | O_BALOON (Amy's goal balloon) | 8C059E20 | 8C059E20-8C05A360 | 10 | - | 1,070 B | 3 |
| | O_EME_P (Knuckles' emerald shard) | 8C05B6C0 | 8C05B260-8C05BD00 | 11 | - | 2,230 B | 128 (5) |
| | S_KAERU (Froggy, Big's goal) | 8C0ED398 | Big's TU | 1+ | - | 838 B + | 10 (6) |
| Course | O_Save_Point (checkpoint) | 8C064F40 | 8C064E80-8C065800 | 14 | - | 2,066 B | 93 (33) |
| | O_SWITCH | 8C067480 | 8C067480-8C067E40 | 20 | - | 1,952 B | 53 (25) |
| | O_TOGE (spikes) | 8C0682A0 | 8C0682A0-8C0686C0 | 4 | - | 672 B | 62 (11) |
| | O_IRONB (swinging iron ball) / O_FeBJG (moving iron ball) | 8C05BD00 / 8C05C8B2 | 8C05BD00-8C05D0A0 | 18 | - | 4,294 B | 50 / 94 |
| | O_SAMPLE_S / M / L / SW (liftable crates) | 8C064980.. | 8C064980-8C064E80 | 12 | 9 M (684 B) | 258 B | 0 (only in Mystic Ruins' list) |
| | O_BUBBLE / O_BUBBLES (air bubbles) | 8C0686C0 / 8C068750 | 8C0686C0-8C0698C0 | 28 | - | 3,810 B | 41 / 123 |
| | DYNAMITE (switch-ID charge, E-102 stages) | 8C05B126 | 8C05AF00-8C05B260 | 5 | - | 738 B | 114 (3) |
| | CMN_KUSA (cuttable grass) | 8C05A940 | 8C05A940-8C05AF00 | 7 | - | 1,234 B | 1,024 (30) |
| Invisible | C_SPHERE / C_CYLINDER / C_CUBE / WALL / OTTOTTO / NO_WATER / BIGWJUMP | 8C02DE20.. | 8C02DE20-8C02E820 | 20 | - | 2,234 B | 6,546 + 98 |
| Hints | O_HINT, O_KN_HINT, O_TIKAL + message window 8C05F000 + orb 8C05FCA0 | 8C060D40.. | 8C05F000-8C061C40 | 84 | - | 9,544 B | 88 / 52 / 89 (NPC topic) |
| Paths | loops / rails / corkscrews (RunAlongPathP) | 8C06B102.. | 8C06AF00-8C06C0C0 | 16 | 1 M | 4,436 B | spline topic |

Stage-overlay gimmicks are in section 4.12.

---------------------------------------------------------------------------------------------------------------------

## 4. The objects

Model / data addresses are 1ST_READ data (SA Tools labels in `config/symbols.1st_read.txt`); "row 19/20/21" =
the edit-mode label printed next to `scl.x/y/z` (rows 16-18 are `ang.x/y/z`), which is how the parameter mapping
below is established. Mappings without such a label or code evidence are marked UNVERIFIED.

### 4.1 Rings

**RING** (`objexec_RING` 8C0869E8, all C: `code_8C0869E0.c`, `code_8C0870EA.c`, `code_8C0876F4.c`).
* Init: `CCL_Init(tp, D_8C1BF0BC, 7, 1, 1)` - one sphere r 5 (b05 0xB0); `twp->id = 11` (light speed dash target);
  ground height below into `scl.y` (GetShadowPos 8C091C20, NM); disp 8C086D00, dest 8C08776C.
* Collect: CCL hit -> mode 2, `AddRings(1)`, SE 7, then three expanding sparkle sprites (8C086DE6) and free. Any
  player collects unless the story character is Tails (then only player 0, the race rule); a dead player (equipment
  0x4000) collects nothing. Magnet/fly-to-player path in 8C08789A (NM; magnetic barrier, UNVERIFIED).
* Params: `scl.x == -1` staggers the sparkle timer and every 9 steps, within 400 units of the camera task, re-tests the
  ground: if it moved by 8 units or more the ring becomes a falling ring (8C0870EA). Everything else is overwritten.
  76 placements use -1, 1,919 use 0.
* Model `Ring` 0x8C1A2204, sprite `B_8C79897C` + texlist D_8C1BF014; shadow blob `Character_Shadow_Blob` 0x8C1ACA78.
* World needs: nothing new (all callees lowered); rings in a world already appear after a hurt.

**O_GRING** (8C087B80, NM C `code_8C087B80.c`): a group of `scl.x + 1` RING child tasks (row 19 "<- NUM MAX 7",
so up to 8), spacing `scl.y` (row 20 "<- DISTANCE"), pattern `scl.z` (row 21 "<-PATTERN 0:RETU 1:CIRCLE": 0 line,
1 circle). Collected rings are remembered as bits in `ocp+1` (8C091C10), so the group needs an ocp. The usual way to
lay rings; most SET values are (2-6, 15-30, 0/1).

**O_LINERING / O_LINERING_V** (8C061C40 / 8C062020, todo 858 + 1,384 B): ring lines that appear when a switch is on:
row labels "<- SWITCH ID", "-> X LENGTH", "-> Y LENGTH", "-> RING INTERVAL" (V: "-> R LENGTH" instead of X); they
spawn RING children (8C061C66 / 8C062046 call objexec_RING) and test 8C067D84(switch id). 68 / 2 placements
(Station Square, Twinkle Park, Lost World; the vertical one in the Egg Carrier and Station Square). Which `scl` / `ang` component holds which label is UNVERIFIED.

### 4.2 Item boxes ("lives, shields, etc.")

**O_ITEMBOX** (8C05D10C, cluster 8C05D0A0-8C05D9C0, todo 2,004 B / 18 functions).
* `scl.x` = item (clamped: < 0 -> 8, > 8 -> 0). The game's own names (table `D_8C1B9C8C`) and handlers
  (`D_8C1B9BA0`: {icon texture, handler}):

| scl.x | Name | Handler | Effect | Icon tex | Ground / air placements |
|---|---|---|---|---|---|
| 0 | HAYAASI | 8C05D764 | speed shoes 8C07D3A0 | 0x23 | 22 / 10 |
| 1 | MUTEKI | 8C05D884 | invincibility 8C07D4A0 | 0x48 | 12 / 8 |
| 2 | 5RING | 8C05D7A6 | AddRings(5) | 0x21 | 53 / 61 |
| 3 | 10RING | 8C05D7D6 | AddRings(10) | 0x20 | 110 / 128 |
| 4 | RANDOMRING | 8C05D806 | AddRings of `{1,5,10,15,20,40}[rand * 6 / 32768]` (D_8C154C40) | 0x22 | 103 / 27 |
| 5 | BARIA | 8C05D900 | barrier 8C07D43E | 0x47 | 34 / 14 |
| 6 | 1UP | 8C05D854 | lives + 1 (8C08A120) | 0x1F | 79 / 27 |
| 7 | EKUSUPUROSYON | 8C05D930 | bomb: sphere r 10 at the box (8C05E582) that the enemy damage check 8C0489E4 tests through 8C05E5C2 | 0x49 | 5 / 5 |
| 8 | ITEM_TH_BARIA | 8C05D964 | magnetic ("thunder") barrier 8C07D3DC | 0x46 | 23 / 10 |

  (9 ground / 2 air records are out of range and clamp to 0.) Every handler plays SE 0x0B (E-102 also voice
  0x4AC / 0x4AD) and 8C05E0F8 queues the HUD item icon (list `B_8C78C3A0`, 16 B per entry: type, y -60.0, 2.0,
  ring count; drawn by the 8C05E0CE / 8C05E14E.. group in the air-box TU, UNVERIFIED which draws it).
* CCL: `D_8C1B9AC0`, id 2 (homing and lock-on target), cylinder r 5 h 5 at +5 (attr 0x100081: damageable, E-102
  lockable but not counted) and a sphere r 5 at +10 (the lid). Break when the box is out-levelled (`twp->flag & 4`:
  a ball / attack) or when the **lid shape** is hit (`cwp->my_num == 1`); a player who lands on the lid while airborne
  gets `SetVelocityP(pno, 0, 2, 0)` (measured, 6.2). The item goes to whoever broke it under the story-character rules
  (player 0; Sonic, E-102, Big stories any; the Tails/Sonic pair rules), then the base stays: `CCL_Init(tp,
  D_8C1B9B28, 4, 1, 2)` (two flat cylinders) and ocp bit 4 so a reload shows it broken (8C05D4CE).
* 8C05D0A0 spawns a ground box with an item (called from 8C066AA6, the grass/helper TU).
* Model `Item_Box` 0x8C1AC96C (objects D_8C1AC944, D_8C1AC1A8, D_8C1ABD30, D_8C1AB9A4, D_8C1AB994); textures OBJ_REGULAR.

**O_ItemBoxAir** (8C05D9F6, `code_8C05D9C0.c`: exec NM, init/dest M; todo 1,684 B incl. its display 8C05DC60, a
"void" display stub today). Same `scl.x` items; one sphere r 10 at +5 (`D_8C1B9C08`, id 2, attr 0x100000): a floating
box broken by a homing attack, spins (+0x222 per step), scales up 0.2 per step after the hit. 8C05D9C0 spawns one
(Knuckles' dig 8C0C8534). Model `Item_Box_Air` 0x8C1B87BC.

**Lives and shields** have no objects of their own: 1-ups come from item type 6 and every 100 rings; shields from
types 5 / 8 (`playerwk.item` bits 1 / 2) and are consumed by a hit before the rings (libsa1_interaction.md 1.5).
Shield / invincibility models: `Shield_Regular` 0x8C1A0820, `Shield_Magnetic` 0x8C1A03B0, `Invincibility_Beam`
0x8C1A0938, `Invincibility_Ball` 0x8C1A128C. Their display tasks need the 11 functions of section 2.

**O_LevelItem** (8C0698F6, todo 4,790 B): the character upgrades (row "-> ITEM NUMBER", `scl.x` 0..17, clamped;
CCL D_8C1BADD0 sphere r 4; message text such as "Lure powerup!" at 8C155561; models `Character_Upgrade_Effect` /
`_Light_Effect` / `_Floor_Effect` 0x8C1B7488.. ). G-SA1's Options already toggle upgrades; a pickup is a 1.2 extra.

**O_EMBLEM** (8C0A6B78, todo 2,908 B, adventure fields only): the emblem collectable; its count lives in the save
data (VMU), so a sandbox version is "count per session" (owner question 8).

### 4.3 Springs

**SPRING** (ground, 8C065800) and **SPRINGB** (air, 8C065EE4), cluster 8C065800-8C066320 (todo 2,620 B; the launch
8C065A98 is 0x2D4).
* Both: `CCL_Init(id 4, n 1)` with colliwk flag 0x800; SPRING `D_8C1BA884` cylinder r 6 h 10, SPRINGB
  `D_8C1BA8B8` sphere r 14. Re-trigger after 10 steps (`twp->btimer`). SE 9, rumble (4, 15, 0).
* Launch 8C065A98(tp, type): vector `(0, scl.y + 5, 0)` (D_8C1550AC + row 20 "<- ADD YSPD") rotated by the
  spring's angles (Z, X, Y). Ground spring: a character 2-14 units from the spring top is moved 2 units towards it
  first (SetInputP 12, position, SetInputP 24; libsa1_interaction.md 6.2.1). Air spring: the character is put on the
  spring's axis (measured, section 6).
  * type 0 (SPRING): 8C07C720 = smode 3 + world velocity: the character uncurls (Sonic mode 9, action 16
    Sonic_Spring_Launch), so no homing until it lands. `scl.x`, `scl.z` are animation state (overwritten).
  * type 1 (SPRINGB): 8C07C768(pno, scl.y + 5, &ang, max(scl.x, 0)) = smode 4: player-space (0, s, 0), the character
    turned to the spring, **no-control time `scl.x` steps** (row 19 "<- NO TIMER"). Correction to
    libsa1_interaction.md 6.2.1, which names `scl.z`: the code reads +0x2C (`scl.x`), and SPRINGB uses `scl.z` and
    `timer` as animation state.
* Models: `Spring` 0x8C1A40E8 (ground; objects D_8C1A40C0, D_8C1A3DD4, D_8C1A3988), `Rotatable_Spring` 0x8C1A359C
  (air; D_8C1A3574, D_8C1A3298, D_8C1A2E4C). Defaults dominate: 44 / 56 records use (0, 0); the rest add 0.05-20 to
  the speed and 1-64 steps of no-control.
* 5 SA1 units per step = 300 units/s = 1200 HU/s in G-SA1's Source preset (1 SA1 unit = 4 HU).

**O_SPRING_H** (8C067E40, todo 894 B, 5 placements in Windy Valley / Twinkle Park): a long spring bar. Row 19 "<- X
SCALE": the capsule length is `20 * scl.x` (D_8C1BAC94: capsule r 6 b 20 along X at (0, 5, 5)); row 20 "<- ADD
YSPD". It launches with 8C07C952 (smode 25: speed, rotation, no-control) from base `(0, 5, 0)` (D_8C15516C) and wobbles
for 120 steps. Model objects D_8C1B3D18 / D_8C1B3AE8 next to SA Tools' `Triple_Spring` 0x8C1B3D40 (the "three-way"
spring of the roadmap; the exact speed formula is UNVERIFIED).

### 4.4 Dash panel, jump panels, dash ring, rockets

**O_AXPNL** (dash panel, 8C059B48 + display 8C059A80; todo 854 B, 3 functions).
* Edit-mode text: "SCL_X IS FOR SPEED. IF 0.0F OR LOWER, IT'S 14"; "SCL_Y IS FOR DISABLE TIME. IF 0.0F OR LOWER,
  IT'S 60".
* CCL `D_8C1B9674` id 4: capsule r 10 b 4.44 lying flat (angx 0x4000). On a hit by a **grounded** player (not again
  for the same player until the no-control time ran out): 8C07C8E0(pno, (scl.x, 0, 0), panel ang, (int)
  ceilf(scl.y)) (8C10D148 = ceilf, libsa1 `rt_libm.c`), SE 0x2BB, rumble (4, 15, 0); E-102 also 8C0D325E.
* Display: animated `NJS_ACTION` at 0x8C1A6470 (`Dash_Panel` 0x8C1A63E0, conveyor motion
  `Dash_Panel_Conveyer_Spin` 0x8C1A6464).
* 271 of 527 records use the defaults; others (14, 60), (9, 0), (0, 20), (10, 60), (12, 0) ...

**O_JPanel** (numbered jump panels, 8C05EB76; cluster 8C05E680-8C05F000: the player-side helpers 8C05E69E / 6C4 / 6E8 /
788 are M, the flight 8C05E7D4 NM, the panel itself todo 876 B).
* Rows: "-> ROTATION X/Y/Z", "-> PANEL NUMBER" (`scl.x`, 0..10 seen; 0 is the start panel). Panels register in the
  per-world list `B_8C78C4E0[10]` - **at most 10 panels, one chain per world**. `twp->id = 8`; CCL `D_8C1B9CB0` two
  cylinders (r 8.2 h 3, r 6 h 3.8; attr 0x882).
* Sonic, Tails, Knuckles and Amy react (their mode machines call 8C05E6E8 / 8C05E788, their mains 8C05E7D4 =
  mode 40 flight to the next panel); E-102 and Big do not. The landing effect task 8C05EFBA is a "freetask" stub today.
* Models `Jump_Panel` 0x8C1B2670, `Jump_Panel_Numbers` 0x8C1B2870 (`Jump_Panel_Last_Unused` 0x8C1B3264).

**CMN_DRING** (dash ring / rainbow hoop, 8C05A360; todo 1,290 B + helpers 8C06638E, 8C0663CA, 8C0666CA, 8C066880,
8C0671AE). Row 19 "Target Position": `scl` is an **absolute target point**. When a player passes the ring plane
(8C0671AE) the ring clears the player's speed and, **only if the story character is Tails** (smode 0x31, flight to
the target) **or Knuckles** (smode 12), sends it; any other character is ignored by the game's code; SE 0x2DC. Placed
only in Tails' sets (Windy Valley 3, Speed Highway 1, Sky Deck 1: 53). Model `Dash_Ring` 0x8C1AA714.

**Rockets** (`Rocket_H/HS/V/VS` 8C063100 / 8C063220 / 8C063360 / 8C063480; cluster todo 4,792 B, 31 functions, plus
the helper TU 8C066320 (3,700 B)). Row 19 "Target Position" (`scl` = where the rocket flies), row 16 "<- ID Number"
(HS / VS only: `ang.x & 15` = the switch that arms it, 8C067D84). The character grabs the handle (SetInputP 20), rides
to the target (SetPositionP, rotation from the flight vector) and is let go (SetInputP 24). Models `Rocket_Platform`
0x8C1AD56C, `Rocket_Missile` 0x8C1B0CE0, `Rocket_Platform_Hatch_Opening` 0x8C1AD610, `Horizontal_/Vertical_
Rocket_Launch` 0x8C1B11F4 / 0x8C1B16B8. 21 / 0 / 7 / 3 placements (Windy Valley, Speed Highway, Red Mountain, Sky
Deck, Ice Cap, Final Egg, Hot Shelter, Egg Viper).

### 4.5 Goals (stage end)

| Story | Goal in SA1 | Object |
|---|---|---|
| Sonic | Egg capsule, or a Chaos Emerald that leads to it | O_RELEASE (1ST_READ); O_KAOSE (overlay copies in STG02 / STG08 / STG09, 0x356-byte exec) |
| Tails | the capsule at the end of the race (Tails' snowboard reads its state through 8C062620 / 8C062704) | O_RELEASE |
| Knuckles | three emerald shards | O_EME_P (1ST_READ) + Knuckles' radar (character code) |
| Amy | the balloon | O_BALOON (1ST_READ) |
| Big | catch Froggy with the rod | S_KAERU 8C0ED398 (1ST_READ, Big's TU; fishing C exists) |
| E-102 | boss / target (stage scripts, O FROG in STG01) | none common |

**O_RELEASE** (Egg capsule, 8C062640; cluster 8C062620-8C063100, todo 2,142 B, 26 functions).
* Init: `CCL_Init(tp, D_8C1BA3BC, 4, 1, 5)` - body sphere r 18 at +7 and four spheres r 5 around the base at +-16;
  a child task 8C062900 (the button); the capsule's position in `B_8C78C558` and its task in the single pointer
  `DAT_8C1BA4F8` (one capsule per world).
* Trigger 8C062930: a player within 12 units horizontally and 27 to 56.6 units above the base (standing on the
  button). Then 8C0509DA(0xFF) (input lock, UNVERIFIED), PClearSpeed on both players, SE 0x0D, the button sinks
  0.25 per step to -3, rumble (6, 30); the capsule shatters into its own triangles (8C062D68 builds flying, fading
  polygons from the model), animals fly out (8C015720, C), and after a countdown 8C06270A: unless an event runs,
  **8C04EFE0 (stage clear)**, SE 0x2DA.
* 8C04EFE0 (todo 554 B, a "void" stub in libsa1) is game flow, branching on the story character: Tails (unless he
  lost the race), Knuckles and Big get `SetInputP(0, 19)` (the clear pose), the clear camera (8C0213E0 / 8C01AD2C),
  the results task 8C08ECB8 and voice / jingle (8C04EF22 / 8C04EF52); Sonic's path creates task 8C04F20A, which reads
  the capsule state (8C062620 / 8C062704); all stop the timer (8C08A5B6) and lock the pad (8C0509DA(0)). A sandbox
  needs its own version (section 7.6).
* Models: `Capsule` 0x8C1A7B24 (objects D_8C1A68D4, D_8C1A6EE4, D_8C1A7AFC).

**O_BALOON** (Amy's balloon, 8C059E20, todo 1,070 B; libsa1_interaction.md 6.2.1): CCL id 4 sphere r 17 at +17 and a
cylinder r 1 h 20 (string); when player 0 **is Amy** (8C07CE40(0) == 5) within 8 units of a point 22 below it:
SetInputP(0, 12), timer stop 8C08A5B6, 8C04EFE0, and it rises carrying her. Model `Balloon` 0x8C1AB8BC.

**O_EME_P** (8C05B6C0, todo 1,880 B + the sparkle TU 8C05BB20 350 B): Knuckles' shard; uses Knuckles' hunt code
(8C0C8E32, 8C0C9BE2, 8C0C9C9A, 8C0C9EE8: partly todo). 128 placements in 5 files (candidate spots; the hunt picks
three). Model `Master_Emerald_Shard` 0x8C1A7F10.

### 4.6 Checkpoint

**O_Save_Point** (8C064F40; cluster 8C064E80-8C065800, todo 2,066 B, 14 functions).
* `CCL_Init(tp, D_8C1BA808, 4, 1, 1)`: a box 12 x 4 x 0.5 at +6; two child tasks (the lamp balls, 8C065386).
* Only **player 0**: mode 2, the lamps spin (8C0655A0, sparkle effect 8C045BB6 0x38E), SE 0x0A, the respawn record
  8C064EF6 (time from 8C08AB2E, position and angle of player 0, stage number; a libsa1 "void" stub: "the host owns
  respawns"), ocp bit 4 (stays lit after a reload), the elapsed time shown for 300 steps (8C057934). Restore
  8C064EB2 (from the death sequence 8C04F8FA): position, angle, time, **rings to 0**, 8C07D5AA(0).
* Model `Checkpoint` 0x8C1B64A8 (balls D_8C1B5440, D_8C1B4B40). Nearly all placements use the defaults.

### 4.7 Switch (and what listens to it)

**O_SWITCH** (8C067480; cluster 8C067480-8C067E40, todo 1,952 B, 20 functions).
* Rows: "<- ANGLE X/Y/Z", "<- KIND:%s" (`scl.x`, 0..4: TOGGLE TYPE, PUSH TYPE, OFF AFTER A WHILE, TURN ON ONLY,
  UNDEFIND TYPE), "<- ID" (`scl.y`, 0..31), "<- TIMER" (`scl.z`, kind 2).
* It registers **dynamic land collision** (8C052A1E + DynamicCOL_Add 8C0528AE attr 0x60004000, `Switch_Collision`
  0x8C1A8EA0 via 0x8C1A8E78) so characters stand on it, and is pressed when a player stands on it (8C07D1D0); SE.
  Model `Switch` 0x8C1A98E4 (object 0x8C1A98BC), CCL D_8C1BAC60 sphere r 6.
* State: **`B_8C78C5B4[32]`**, bit 0 on, bit 1 changed; set 8C067D64(id, on), read 8C067D84(id), reset 8C067DF4.
  Readers: Rocket_HS / VS, O_LINERING / _V, DYNAMITE ("<- ID"), and stage-overlay doors and gates.

### 4.8 Hazards

**O_TOGE** (spikes, 8C0682A0; todo 672 B, 4 functions): a field of spikes, "SCL X -> X NUMBER", "SCL Z -> Z NUMBER";
CCL `D_8C1BACC8` id 4: a solid box 6.5 x 3 x 6.5 (b05 0xAF) and a hurting box 5 x 5 x 5 at +5 (**attack 2 / defence 3,
flag 0x2E**: hurts players without invincibility), both sized by the counts; `ang.x & 0xC000` picks the orientation
(UNVERIFIED). Model `Spikes` 0x8C1A8CC4.

**O_IRONB / O_FeBJG** (iron balls, todo 2,516 + 1,778 B): the swinging ball on a chain (labels "-> ROTATION Y SPEED",
"-> ARM LENGTH", "-> Y TRANS RANGE", "-> 1000 Y TRANS SPEED", flags "100 CHANE / 10 SHADOW / 1 IRONBALL", "HEAVY" /
"LITE") and the moving one ("-> ROTATION Y", "=1:Ground Move", "-> Y TRANS SPEED", "Be Sure To Land"); CCL
`D_8C1B9A58`: sphere r 8 attack 2 / defence 3 (hurts) + a small solid sphere. Which component carries which label is
UNVERIFIED. Models `Iron_Ball` 0x8C1A5308, `Iron_Ball_Ring` 0x8C1A55EC, `Iron_Ball_Joint` 0x8C1A5938.

### 4.9 Liftable crates ("breakable boxes" in the common set)

The common set has no breakable box apart from item boxes. **O_SAMPLE_S / M / L** (8C064980 / 8C064A64 / 8C064BB8, C,
684 B matched; O_SAMPLE_SW 258 B todo) are liftable crates (CCL D_8C1BA668.. cylinder r 15 h 4 for S, then M, L;
`8C0586A0` ObjectMovableInitialize, the movable step 8C05872A): in Mystic Ruins' list but never placed (0 records).
The round-5 HOLDABLE design already uses O_SAMPLE_S as its reference object (`hold_ec1` trace). Whether a thrown
crate breaks is UNVERIFIED. Stage breakables are in 4.12.

### 4.10 Water

**O_BUBBLE / O_BUBBLES** (air bubbles, todo 3,810 B, 28 functions): the emitter ("<-RANGE", "<-FREQUENCY", "<-MAX
(DISP %d)") and the breathable bubble ("COLLISION HEIGHT %f", "=<-COLLI SEARCH OFFSET"); a breath is
`SetInputP(pno, 36)` (8C068BE4). Placed in Emerald Coast 2 (Sonic, Tails), Big's Emerald Coast 3, Twinkle Park, Ice
Cap and Hot Shelter, Amy's Hot Shelter. They pair with the 0.7 drowning bug fix (the game's own air reset).

### 4.11 Others

* **CMN_KUSA** (grass, 1,234 B): cut by attacks (`Weed`, `Weed_Cut`, `Weed_Parts` 0x8C1B1B54..), helper 8C066AA6 can
  spawn an item box from it.
* **DYNAMITE** (738 B, E-102 stages only, "<- ID"): a charge that a switch or shot sets off.
* **Invisible collision** (C_SPHERE, C_CYLINDER, C_CUBE, WALL, OTTOTTO = teeter edge?, NO_WATER, BIGWJUMP; 2,234 B):
  6,644 placements of invisible solids and triggers. A sandbox gets these from GMod brushes; skip.
* **Hint monitor / Tikal orb / Knuckles hint** (O_HINT 8C060D40 "<-MessageID", O_KN_HINT "<-CAM Dist(def. 80.0)",
  "<-CAM Hight", "<-Message ID", O_TIKAL; message window TU 8C05F000, orb TU 8C05FCA0; 84 functions, 9.5 KB todo):
  the game's "object that shows a text message" - input for the NPC-with-custom-text topic.
* **Path-work objects** 8C06AF00-8C06C0C0 (loops, rails, corkscrews: 8C06B102 / 8C06B43C / 8C06B752 call
  RunAlongPathP 8C07C0A0, RunWithSeeingPathP 8C07C0E4, HangDownFromPathP 8C07C120 (C)): input for the spline topic.

### 4.12 Stage-overlay gimmicks (not common; one overlay per world)

Each lives in its stage overlay, which is 0-8 % decompiled (STG01 4.8 KB of 57 KB has C, STG05 4.8 of 74 KB, the
rest 0), and needs that overlay loaded at 0x0C900000 in the world. Exec sizes only (their clusters are larger):

| Want | Object (overlay, exec, exec size) | Placements |
|---|---|---|
| Bumpers / flippers | Bumper1 (STG09 0C902BCC, 0x36), Bumper2 (0C902C80, 0x8C), FlipperL/R (0C90277C..), SlingL/R, StoperC/L/R | 9 / 1 / 3+3 |
| Breakable boxes | O_Hako (STG08 0C901860), O HAKO / O GBOX (STG09 0C91112E / 0C91113C), O BOX (STG06 0C90BD60, 0x222), MT_TBOX (STG05 0C905E6E), O R DBOX / O RNDBOX (STG07 0C9079E0 / 0C908300), O HAKO (STG12 0C907E60) | 15, 9 / 3, 16, 38, 36 / 6, 0 |
| Cannons / launchers | CANNON_B/S/S1/S2/M/H (STG06), LAUNCHER, STOP, HANGPOLE | 18 .. 95 |
| Wind | PU WIND (STG02 0C907C1C, 0x5A8: updraft), WC WIND | 30 / 6 |
| Jump stands | O JUMP (STG01 0C9012E0, 0x158), LostJumpdai (STG07 0C903060), O ICEJMP (STG08); O TPANEL (STG07 0C905F9C, role UNVERIFIED) | 9, 4, 12; 88 |
| Other | Twinkle Park O TP RING (41), O FLOAT, O BARREL, carts; Speed Highway O RING2 (146: scenery, UNVERIFIED), O PinPin; Red Mountain O PROPELLER (0x51E), O BREAKSTEP; Final Egg UP TARGET, O Fun; Chaos Emerald O_KAOSE | |
| Trampoline | none in the final game; `TRAMPOL` exists only in SA Tools' Windy Valley *autodemo* definitions | - |

---------------------------------------------------------------------------------------------------------------------

## 5. Data on the disc

| Data | Where | Notes |
|---|---|---|
| Object code | `1ST_READ.BIN` (common objects, rings, player API); stage `.PRS` overlays (STGnn, ADVnn, MINICART, SBOARD, ...) | the DLL ships addresses and generated tables only |
| Models, CCL tables, motions | 1ST_READ data 0x8C199518-0x8C1BAC3C (section 4 addresses) | already in every world's arena; G-SA1 can also render them through libsa1's NJS decoder |
| Textures | `OBJ_REGULAR.PRS` (155,785 B, SHA-1 446e5450..., in `libsa1/src/sa1_manifest.c`), texlist 0x8C19F530, 96 textures (item icons 0x1F-0x23 / 0x46-0x49, `shadow` 58) | loaded by libsa1 for every character since the shadow round; ring sprite texlist D_8C1BF014 |
| Object lists | stage overlays (1.1) | 33 lists parsed (every overlay SA Tools locates); B_CHAOS7, B_ROBO and STG99 not parsed (no address) |
| SET files | `SONICADV/SET*.BIN` (156) | 43,091 records; 42,761 resolved |
| Sounds | SE ids via libsa1's audio layer (tone -> bank table 0x8C1C19B4) | ids in section 2 |
| Edit-mode labels | 1ST_READ strings 0x8C154A88-0x8C1556FC, 0x8C159440-0x8C15945C | the parameter names used in this document and for the Q-menu labels |

Measured parameter use (`set_usage.json`): item boxes 10 rings 238, random 130, 1-up 106, 5 rings 114, barrier 48,
speed 32, magnetic 33, invincible 20, bomb 10; jump panel numbers 0-10; springs mostly (0, 0); dash panels 271 of 527
default.

---------------------------------------------------------------------------------------------------------------------

## 6. Measurements (harness oracle, the user's disc, original SH-4 code)

`harness/build/sa1h.exe trace --level 1 --act 0 --chr 0 --no-objects --create-task ADDR:IM:LIST@X,Y,Z ...` (Sonic,
Emerald Coast act 1, his running line of the `spring_ec1` reference), outputs in `build/research/objects/traces/`;
each run 300 steps, about 2.4 s.

| Run | Object | Result |
|---|---|---|
| `dash_ec1` | O_AXPNL 8C059B48:3:3 at (-61.64, 3.405, 32.21), scl 0 | step 92: `pspd.x` 1.43 -> **14.000000**, `nocontimer` 0 -> **60** (counting down to 0 at step 152), `ang.y` -> **0x4000** (= 0x4000 - panel ang.y 0) |
| `box_ec1` | RING 8C0869E8 at (-40, 8, 19.5) + O_ITEMBOX 8C05D10C:2:3 (scl 0 = speed shoes) | ring: `rings` 0 -> 1 at step 77, Sonic's position 8.0 units from the ring (player sphere 4 + ring sphere 5); lid: step 122 `pspd` (0, **2.000000**) = SetVelocityP(0, 2, 0); afterwards Sonic accelerates to 6.2 units/step by step 285 (consistent with the speed shoes; the parameters themselves are not in the trace) |
| `ring_ec1` | two RINGs on the running line + SPRINGB 8C065EE4:2:2 | `rings` 1 at step 64, 2 at step 72; step 92: Sonic placed on the spring's axis (x/z = -61.64, 32.21 exactly), **mode 10, smode 4**, `pspd.y` 4.868 = 5.0 minus one step of gravity, no-control 0 (scl.x 0) |

These are the reference behaviours libsa1 must reproduce bit-exactly once the C exists; the harness option needs
`scl` / `ang.x/z` arguments for the non-default cases (work item W3).

---------------------------------------------------------------------------------------------------------------------

## 7. What a libsa1 world needs: the object layer (API 0.4 draft, `SA1_API_OBJECTS`)

### 7.1 Principle

Owner rule: game logic is the game's own code. A G-SA1 object is therefore a **real SA1 object task running its
decompiled exec in every character world** (one world per character, guest arena), mirrored the way round 5 mirrors
host objects - but with the game's `objexec_*` as the exec instead of the LIBSA1_HOST proxy (0x7F000100). The object
code must exist as lowered C; an unresolved exec address is a fault, so the catalogue only offers kinds whose C is
in the build.

### 7.2 Catalogue (generated)

`sa1g_object_kinds[]`, generated from the disc's object lists and the code map: name, exec, `im`, `level`, load
distance, the parameter schema (component, the game's label, default, clamp: e.g. dash panel `scl.x` "speed" 14,
`scl.y` "disable time" 60), the CCL id, the reacting characters. Public: `sa1_object_kind_count / _info`.

### 7.3 Per-world mirror

* `sa1_object_add(ctx, desc, &id)` with `{kind, position, ang (BAMS x/y/z), params[3] (scl), flags, range}`;
  `_update`, `_move`, `_remove`, `_consume` (play the game's own consumed path in a world, e.g. ring mode 2 sparkle).
* In each world, inside a **window** around that world's character and camera (enter / leave radius, default the
  list distance: 400 units, jump panels 2000; at most ~200 rings, the CCL id 7 list holds 256 per step), the layer
  makes the task exactly as the SET manager does: `CreateElementalTask(im, level, exec)`, `twp->pos/ang/scl` from
  the descriptor (converted with `sa1_coords`), `twp->counter = 0`, and a 16-byte **OBJ_CONDITION stand-in** as
  `tp->ocp` (in segment 0x18, like the host objects): `ssCondition = 0x8001 | 8` (never range out: the window replaces
  the SET manager's range), `+0xC` = range squared, `+1` = the object's persisted bits (ring group). E-102's firing
  code dereferences `ocp` (libsa1_interaction.md 6.4 row 4), item boxes test bits 4 / 0x10.
* Each pair after the CCL pass: **consumed** detection per world - the task gone or `exec == DestroyTask`, ocp bit 4,
  or a kind's terminal mode (ring 2/3, item box 3/4, checkpoint >= 2, capsule button pressed) -> event
  `SA1_OBJ_EVENT_CONSUMED {object, character, detail (item type)}`. The host decides what happens in the other worlds
  (remove, or `_consume`) and when it respawns (owner question 3). Persistent per-object state (ring-group bits,
  "broken", "lit") lives in the registry and is written into every new ocp.
* World init runs the common-object initialiser **8C058E48** once (after it has C) so the per-act globals start clean.

### 7.4 Shared game state across worlds

* **Switches.** After each pair, OR-merge every world's `B_8C78C5B4[32]` entries that have bit 1 (changed) into a
  context table and write it to all worlds before the next pair (a switch pressed by Sonic arms a rocket in Tails'
  world). Host API `sa1_switch_get / sa1_switch_set` (lets G-SA1 wire GMod buttons / Wiremod to SA1 switch ids).
* **Jump panels**: the list `B_8C78C4E0[10]` is per world; the layer fills it in panel order in every world (the
  objects do it themselves at init when all panels are in the window: jump panels have a 2000-unit range).
* **Capsule**: one per world (`DAT_8C1BA4F8`); a second capsule overrides the first (G-SA1 should allow one).
* Counters (rings, lives, score, items): SA1 has one set per game (globals such as `B_8C752BE4`); with one world per
  character they become **one set per character**, which is what a sandbox wants; `sa1_state_ex` already reports
  rings / lives / score (round 5).

### 7.5 Events the host needs

`CONSUMED` (above); `ITEM {character, type}` (from the handler table, for the HUD icon; or expose the game's own
icon list `B_8C78C3A0`); `CHECKPOINT {character, pos, ang, time}` (8C064EF6 becomes an event instead of a "void"
stub); `GOAL {character, kind}` (8C04EFE0 replaced at the boundary, 7.6); `SWITCH {id, on}`; sounds and rumble go
through the existing event / audio paths (SE ids section 2).

### 7.6 Stage clear in a sandbox

8C04EFE0 is game flow (results, next stage) and stays a boundary function. Proposed libsa1 replacement (owner question
2): stop the world's timer as the game does (8C08A5B6, C), request the character's clear pose with the game's own
order (`SetInputP(pno, 19)`, what 8C04EFE0 sends; UNVERIFIED for every character until traced), raise `GOAL`; G-SA1
plays the clear jingle and shows a banner. The capsule and Amy's balloon run unchanged up to that call.

### 7.7 Rendering

Object display runs in every world's display pass; drawing all copies would duplicate them. Proposal: the capture
tags each draw with the executing task (the display walker knows `tp`), the layer maps tasks to object ids, and
`sa1_object_render(ctx, alpha, &out)` returns per-object draw ranges from **one** designated world (the local player's
main character; the consumed sparkle and the item box break are then what that player sees). `sa1_character_render`
excludes object draws. When no character exists, either a character-less **display world** (needs a boot without
LoadPlayerTask: UNVERIFIED that the executor tolerates no player 0 for these objects - the SET range code and the ring
both read player / camera pointers) or a static fallback: `sa1_model_*` meshes of the object's NJS_OBJECT (e.g.
`Ring` 0x8C1A2204) through libsa1's existing decoder and OBJ_REGULAR.

### 7.8 Per-object needs beyond its own C

| Object | Also needs |
|---|---|
| RING, O_GRING | nothing (C exists; verify the NM functions against W3 references) |
| O_ITEMBOX, O_ItemBoxAir | the shield / invincibility display callees (section 2), the bomb pair 8C05E582 / 8C05E5C2 (today "ret 0"), ITEM event or icon list, the air box display 8C05DC60 (today "void") |
| SPRING, SPRINGB, O_SPRING_H, O_AXPNL | nothing beyond the player API (C) and the rumble event |
| O_JPanel | the landing effect 8C05EFBA (today a "freetask" stub), panel-number ordering, the 10-panel limit |
| O_Save_Point | 8C045BB6 (sparkle effect, todo 178 B), CHECKPOINT event, 8C057934 (time digits, M) |
| O_RELEASE | GOAL replacement of 8C04EFE0, 8C0509DA (M), animals 8C015720 (M) |
| O_SWITCH | switch-state sync (7.4), dynamic land collision (C, part of the landtable manager) |
| O_TOGE | nothing (hurts through CCL; damage options of round 5 apply) |
| Rockets, CMN_DRING, grass, bubbles, hints | the helper TU 8C066320-8C067480 (40 todo, 3.7 KB; 8C0663CA is a "void" display stub today) |
| All | 8C0919CC / 8C0919E8 / 8C09111A / 8C091912 (faithful range-out, 0.4 KB; optional while ocp flag 8 is set), 8C091B54 / 8C091B68 (18 B), 8C042DC4 (edit mode, "ret 0"), 8C04FE40 ("ret 0"), 8C058E48 + callees (0.4 KB) |

### 7.9 Cost and limits

Each object runs once per world; a ring is an exec, one CCL entry and a display per step. With N character worlds the
cost is N times the game's own (a few microseconds per object per step is expected for lowered C; UNVERIFIED until
measured in `world_replay`). Heap per object: taskwk 0x40 + colliwk 0x1C + CCL copy 0x34 x n + any work; the world
task pool is 4096 tasks. CCL lists per step: rings 256, objects 512, item boxes 128.

---------------------------------------------------------------------------------------------------------------------

## 8. G-SA1 side

* **"G-SA1" spawn-menu tab** (`spawnmenu.AddCreationTab`), categories Collectables / World objects / Goals /
  Hazards, one scripted entity class per kind (`gsa1_obj_<kind>`), icons rendered from the game's own models and
  OBJ_REGULAR at load time (no shipped art).
* **Properties** (owner rule: every option has a Q-menu control): the entity's `NetworkVar`s with `Edit` tables,
  labelled with the game's edit-mode names and units: item contents (dropdown of the 9 items), spring "Add Y speed"
  and "No-control steps", dash panel "Speed" (default 14 = 3,360 HU/s) and "Disable time" (60 steps = 1 s), ring
  group count / spacing / line-circle, switch kind / id / timer, jump panel number (auto-numbered by the tool),
  rocket target (a second, draggable target entity), plus a G-SA1 Options page "Objects": respawn time, window size,
  "show objects without a character", per-object "all characters" overrides where the game restricts them (owner
  question 1).
* **Tools** (tool gun): ring line / circle / arc placer (emits O_GRING groups of up to 8 and single RINGs; ring trails
  are light speed dash paths for free), jump panel chain placer (0..9, warns at the 10-panel limit), switch linker
  (click switch, click rocket / line ring: shared id).
* **Collision with GMod**: the entities are server-side with a small trigger hull for GMod players and physics; SA1
  characters interact only through their worlds. Springs and dash panels stand on GMod geometry like any prop.
* **Networking**: the server owns object entities and respawn; each client mirrors them into its local character
  worlds; a client sends "consumed" with the object id, the server checks it (exists, distance) and broadcasts the
  removal; ring / life counts stay client-side per character (owner question 4 for shared vs per-player items).

---------------------------------------------------------------------------------------------------------------------

## 9. Recommendation and effort

Effort scale (agent workflow time, UNVERIFIED estimate from P0+P1 throughput, ~1,000 functions with verification in
about two weeks): S < 1 day, M 1-3 days, L 3-7 days, XL > 1 week.

### 9.1 First wave (1.0)

| Object | Decompile | Integration | Effort |
|---|---|---|---|
| Ring, ring group (line / circle), scattered rings | 0 (verify 10 NM functions, 4.0 KB) | object layer core, ring tool | S |
| Item box (9 contents) | 18 fns, 2,004 B + shield displays 11 fns, 1,572 B + bomb 2 fns, 104 B | ITEM event / icon, shields | M |
| Air item box | 13 fns, 1,684 B | as above | S |
| Ground spring + air spring | 7 fns, 2,620 B | properties | S |
| Wide spring (SPRING_H) | 5 fns, 894 B | | S |
| Dash panel | 3 fns, 854 B | properties | S |
| Numbered jump panels | 11 fns, 876 B (+ effect 8C05EFBA) | chain tool, order | S |
| Checkpoint | 14 fns, 2,066 B + 8C045BB6 178 B | CHECKPOINT event, G-SA1 respawn | M |
| Switch | 20 fns, 1,952 B | switch sync, linker tool, sa1_switch API | M |
| Spikes | 4 fns, 672 B | | S |
| Liftable crates S/M/L | 3 fns, 258 B | HOLDABLE path exists | S |
| Egg capsule (goal) | 26 fns, 2,142 B | GOAL replacement, banner + jingle | M |
| Support | 8C058E48 + its small callees, SET range 4 fns, SET flags 2 fns, edit / state stubs: ~16 fns, ~0.8 KB | | S |

Total: about **154 functions, ~18.7 KB** of SH-4 to decompile (P0+P1 was ~1,082 functions), all in 1ST_READ, all
testable with the harness oracle and `world_replay`.

### 9.2 Later waves

* **1.1**: Rockets H / V / HS / VS (31 fns 4.8 KB + helper TU 40 fns 3.7 KB, L), dash ring for Tails / Knuckles
  (8 fns 1.3 KB, S after the helpers), line rings (10 fns 2.2 KB, S), Amy's balloon (10 fns 1.1 KB, S), air bubbles
  (28 fns 3.8 KB, M; with the drowning fix).
* **1.2**: iron balls (18 fns 4.3 KB, M), upgrades (21 fns 4.8 KB, M), emblems (18 fns 2.9 KB, M), Knuckles' shards
  (11 fns 2.2 KB + hunt code, M), Froggy (S_KAERU + Big's goal flow, M), grass, dynamite (S each). Hint monitors /
  Tikal belong to the NPC topic, loops / rails to the spline topic.
* **Post-1.0**: stage-overlay gimmicks with a per-world "stage theme" (one overlay per world): Casinopolis
  pinball (bumpers, flippers), Sky Deck cannons, Windy Valley updrafts, stage breakable boxes. Each needs its
  overlay's object cluster decompiled (overlays are 0-8 % done).

---------------------------------------------------------------------------------------------------------------------

## 10. Risks

1. **Decompilation is the critical path**; NON_MATCHING C must be verified bit-exactly against harness references
   with the object present (W3), as the 15 reference traces were.
2. **N worlds, one object**: each object runs in every character world; consumption reaches the other worlds one
   pair later; two characters can take the same ring in the same pair (both count it; the host can arbitrate only
   after the fact).
3. **Per-world singletons**: 10 jump panels, one capsule, 32 switch ids, one checkpoint record, one item-icon list
   per world - the sandbox inherits the game's limits.
4. **Character restrictions are game code**: dash rings only move Tails / Knuckles, Amy's balloon only Amy, jump
   panels not E-102 / Big, the checkpoint only player 0. Lifting them means non-original code.
5. **Shields and invincibility fault today** if granted with their display (fatal stubs); item boxes must not ship
   before those 11 functions.
6. **Stage clear** (8C04EFE0) is game flow; a faithful sandbox version needs owner decisions and new traces of the
   clear pose for each character.
7. **Duplicate or missing draws** until draws are tagged per task (7.7); objects with no character present need a
   display world (unverified boot path) or the static fallback.
8. **Overlay exclusivity** blocks stage gimmicks from mixing (a Casinopolis bumper and a Sky Deck cannon cannot share
   a world).
9. Parameter mappings marked UNVERIFIED (iron balls, line rings, spikes orientation, wide-spring speed, SET bit 15)
   need traces before the Q-menu labels are final.

---------------------------------------------------------------------------------------------------------------------

## 11. Owner questions (with the default this plan assumes)

1. Objects that SA1 restricts to certain characters (dash ring: Tails / Knuckles; Amy's balloon; jump panels not for
   E-102 / Big; checkpoint player 0 only): keep the game's rules, or add an "all characters" option that is not the
   game's code? Default: game rules, option off, labelled "not in SA1".
2. What should a goal (capsule, balloon) do in a sandbox: clear pose + jingle + banner (default), also a time / ring
   tally screen, or nothing beyond the game's object animation?
3. Respawn of collected rings / broken boxes / used springs: never (as in an act), after N seconds (default 30 s,
   slider 0 = never), or on map cleanup?
4. Multiplayer: is a ring or item box shared (first player takes it, default) or does every player have their own copy?
5. Trampolines: SA1 has none. Skip (default), or a G-SA1 "trampoline" preset on the air spring's code with its own
   model? Bumpers come with the Casinopolis stage theme later.
6. Checkpoint respawn: on death, respawn at the last checkpoint with rings reset to 0 as the game does (default), or
   GMod's own respawn?
7. Stage-specific gimmicks need a per-world "stage theme" (one overlay at a time). Acceptable for post-1.0?
8. Emblems: count per session in the HUD (default), or persist to `data/gsa1`?
9. Jump panels: accept the game's limit of 10 panels (one chain) per world? Default yes, the tool warns.

---------------------------------------------------------------------------------------------------------------------

## Appendix A. Work items

| Id | Work | Effort | Depends on |
|---|---|---|---|
| W1 | SADC: support decomp - 8C058E48 + callees, 8C0919CC / 8C0919E8 / 8C09111A / 8C091912, 8C091B54 / 8C091B68, shield / invincibility display callees (11), bomb 8C05E582 / 8C05E5C2, 8C045BB6, 8C05EFBA | M | - |
| W2 | SADC: first-wave objects (item box, air box rest, springs, wide spring, dash panel, jump panel rest, checkpoint, capsule, switch, spikes, O_SAMPLE_SW); verify RING / O_GRING / scatter NM | L | W1 for linking |
| W3 | Harness: `--create-task ...@X,Y,Z[,ANGY][;AX,AZ][;SX,SY,SZ]` and ~15 object reference traces (each item type, spring variants, dash panel, jump chain, checkpoint, capsule, switch, spikes hurt) | M | - |
| W4 | libsa1: object layer API 0.4 (catalogue, mirror, ocp stand-ins, window, events, switch sync, GOAL / CHECKPOINT boundaries, 8C058E48 at world init, draw tagging) | L | round-5 host-object infra; W1 |
| W5 | libsa1 tests: object replays strict against W3; multi-world consumption and switch-sync tests | M | W2, W3, W4 |
| W6 | G-SA1: spawn tab, entities, property editors, Options "Objects", tools (rings, jump chain, switch linker), networking | L | W4; the 0.7 HUD for rings / lives / item icon |
| W7 | HUD hookup: item icon list, rings / lives / score | S | HUD round, W4 |
| W8 | Second-wave decomp + integration (rockets + helper TU, dash ring, line rings, balloon, bubbles, iron balls, upgrades, emblems, shards, Froggy) | XL | W4 |
| W9 | Stage themes: overlay object clusters + per-world overlay selection | XL | W4, overlay decomp |

## Appendix B. Edit-mode labels (1ST_READ strings, by object)

| Object | Labels (string address) |
|---|---|
| O_AXPNL | "SCL_X IS FOR SPEED." "IF 0.0F OR LOWER," "IT'S " (14) / "SCL_Y IS FOR DISABLE TIME." (60) (8C154A88-8C154ABC) |
| CMN_DRING | "Target Position" (8C154AE4) |
| DYNAMITE | "<- ID" (8C154AF8) |
| O_IRONB | "-> ROTATION Y SPEED", "-> ARM LENGTH", "-> Y TRANS RANGE", "-> 1000 Y TRANS SPEED", "100 CHANE", "10 SHADOW", "1 IRONBALL", "HEAVY", "LITE" (8C154B44-8C154BC8) |
| O_FeBJG | "-> ROTATION Y", "=1:Ground Move", "-> Y TRANS SPEED", "Be Sure To Land" (8C154BD8-8C154C0C) |
| Item boxes | "-> ITEM NUMBER ", HAYAASI .. ITEM_TH_BARIA (8C154C58-8C154CB0) |
| O_JPanel | "-> ROTATION X/Y/Z", "-> PANEL NUMBER" (8C154CC0-8C154CF0) |
| O_TIKAL / O_HINT / O_KN_HINT | "Don't EDIT!", "<-MessageID", "<-CAM Dist(def. 80.0)", "<-CAM Hight", "<-Message ID" (8C154D5C-8C154DC8) |
| O_LINERING / _V | "<- SWITCH ID", "-> X LENGTH" / "-> R LENGTH", "-> Y LENGTH", "-> RING INTERVAL" (8C154E74-8C154EE4) |
| Rockets | "Target Position", "<- ID Number" (8C155080, 8C155090) |
| SPRING / SPRINGB | "<- ADD YSPD", "<- NO TIMER" (8C1550A0, 8C1550B8) |
| O_SWITCH | "<- ANGLE X/Y/Z", "<- KIND:%s", "<- ID", "<- TIMER"; kinds TOGGLE TYPE, PUSH TYPE, OFF AFTER A WHILE, TURN ON ONLY, UNDEFIND TYPE (8C1550C4-8C155144) |
| O_SPRING_H | "<- X SCALE", "<- ADD YSPD" (8C155154, 8C155160) |
| O_TOGE | "SCL X -> X NUMBER", "SCL Z -> Z NUMBER" (8C155178, 8C15518C) |
| O_BUBBLE(S) | "=<-COLLI SEARCH OFFSET", "COLLISION HEIGHT %f", "<-RANGE", "<-FREQUENCY", "<-MAX (DISP %d)" (8C1551D7-8C155218) |
| O_LevelItem | "-> ITEM NUMBER" (8C1553D0) |
| O_GRING | "<- NUM MAX 7", "<- DISTANCE", "<-PATTERN 0:RETU 1:CIRCLE" (8C159440-8C15945C) |

---------------------------------------------------------------------------------------------------------------------

## Review (adversarial)

Reviewer pass, 2026-10-21, read-only. Every claim below was re-measured against the user's disc, the asm / Ghidra
exports, the C in `src/`, `LIB` and `g-sa1`. Scratch: `sadc/build/research/objectsreview/`. `rstat.py` is an
independent per-range status with `tools/progress.py`'s rules and clipped sizes. `closure2.py` walks callees and
function constants from an exec, stops at functions LIB already lowers or stubs (`LIB/build/pclower/functions.json`,
`LIB/gen/policy/stubs.tsv`) and lists the todo code it reaches. `ovlstat.py` gives the overlay status.

### R.1 Verified

* **Decomp status (re-run today).** Every count matches the summary:
  * common objects 8C059A80-8C06AF00: 422 functions; matched 22 / 1,512 B, NM 3 / 1,714 B, todo 397 / 55,756 B;
  * rings 8C0869E0-8C088120: 21 functions, all C (11 M, 10 NM, 4,028 B NM);
  * object helpers: 26 todo / 2,686 B;
  * path-work objects: 15 todo / 4,436 B;
  * SET manager: 26 todo / 2,876 B;
  * 8C043C00-8C0445A0: 11 todo / 1,572 B. 8C043C9E and 8C044060 are NM, 8C044284 is M. The nine callees
    8C043C00 .. 8C04430A are `fatal` in `stubs.tsv`.
  * P0 + P1 = 814 + 268 = 1,082 functions (`build/tier_functions.json`).
* **Disc.**
  * 156 SET files, 43,091 records, 2,423 with bit 15. The 0x20-byte headers are zero after the count. File size is
    always 0x20 + n x 0x20.
  * 33 lists parsed: 62 distinct 1ST_READ execs and 1,153 (overlay, exec) pairs.
  * 1ST_READ objects are 16,553 of 42,761 resolved placements (38.7 %).
  * The im / level / dist values of section 1.1 agree with `objlists.json`. Rocket V / VS have two odd lists (6/2,
    3/3).
* **Parameters, read from the code.**
  * Dash panel: scl.x <= 0 -> 14 (0x41600000), scl.y <= 0 -> 60. Grounded players only (`twp->flag & 1`). The same
    player is ignored until the timer ends. Call 8C07C8E0(pno, &(scl.x, 0, 0), &ang, ceilf(scl.y)), SE 699 (0x2BB),
    rumble (4, 15, 0), E-102 8C0D325E.
  * Spring launch 8C065A98: speed scl.y + 5.0. SPRINGB no-control time = (int)max(scl.x, 0), read at +0x2C. The
    correction to libsa1_interaction.md (scl.z) is right.
  * Switch: KIND = (u32)scl.x clamped to 4, stored in twp+0xC. ID = scl.y clamped to 31, in twp+0xE. Labels at rows
    0x110013 / 0x110014 / 0x110015.
  * Item box clamp: < 0 -> 8, > 8 -> 0. Handler table D_8C1B9BA0 = {0x23, 8C05D764} ... {0x46, 8C05D964}. Name
    table D_8C1B9C8C = HAYAASI .. ITEM_TH_BARIA. Random-ring table {1, 5, 10, 15, 20, 40}. SE 0x0B in every handler.
    E-102 voices 0x4AC / 0x4AD.
* **CCL tables** (`ccl.py`). Sizes and attributes match section 4: D_8C1BACC8, D_8C1B9AC0, D_8C1BA884, D_8C1BA8B8,
  D_8C1B9674, D_8C1BAC60, D_8C1BA808, D_8C1B9CB0, D_8C1BA3BC. Spikes hurt with attack 2 / defence 3, flag 0x2E. The
  ccl.py convention `CCL_Init(tp, info, id, unused, nbInfo)` matches `code_8C02F27C.c:170`.
* **Condition flags.** 8C091B54 sets 4. 8C091B68 tests 4. 8C091B5E sets 0x10. 8C091B70 / 8C091B7E set and test
  0x4000. 8C091B8A / 8C091BA4 clear 0x8000 and 1. DestroyTask clears only bit 1 (`code_8C098444.c:326`).
  8C0919CC uses 168,100 = 410^2 when the task has no ocp. 8C0919E8 skips flag 8 and accepts player 1 within 1,600 =
  40^2 when its id byte is 0.
* **Character rules.**
  * The checkpoint reacts to player 0 only. It stores player 0's position and angle and sets ocp bit 4.
  * Its restore 8C064EB2 sets rings to 0.
  * Rings: a dead player 0 (equipment 0x4000) collects nothing. In a Tails story only player 0 collects.
  * The jump-panel flight 8C05E7D4 has four character callers.
* **Harness traces** (`traces/*.csv`).
  * dash_ec1, step 92: pspd.x 14.000000, nocontimer 60 (0 at step 152), ang.y 0x4000.
  * box_ec1: rings 0 -> 1 at step 77. pspd (0, 2, 0) at step 122.
  * ring_ec1: rings 1 / 2 at steps 64 / 72. Smode 4 and pspd.y 5.0 at step 91, mode 10 at step 92, on the axis
    (-61.64, 32.21).
* **LIB / G-SA1.**
  * OBJ_REGULAR (155,785 B, SHA-1 446e5450..) is loaded for every character. Texlist 0x8C19F530, 96 textures.
  * `sa1_state_ex` has rings, lives and score.
  * Host objects use OBJ_CONDITION stand-ins in segment 0x18.
  * 8C0919CC is `ret 0`, 8C05DC60 `void`, 8C04EFE0 `void`, 8C064EF6 `void`, 8C05EFBA `freetask`, 8C05E5C2 `ret 0`.
  * LIB does not run 8C058E48.
  * `GSA1.SCALE = 4.0` (`sh_config.lua:6`), so 14 units/step = 3,360 HU/s.
  * Q-menu upgrade checkboxes exist (`cl_menu.lua:151-160`).
  * The harness has `--create-task ADDR[:IM[:LIST]][@X,Y,Z[,ANGY]]` (`harness/src/t_sim.c`).

### R.2 Refuted (with the correct facts)

1. **"No trampoline in the final game" is wrong.** The final game's Windy Valley overlay has one, **`O POLINE`**:
   * STG02 list entry 47, exec 0x0C9098E6, im 2 / level 3;
   * 2 placements: SET0200S.BIN and SET0201S.BIN (Sonic, Windy Valley acts 1 and 2), scl (1, 2, 1) and (0.4, 1.2, 1);
   * model `Trampoline` at STG02 0x0C9C8C98 (`windobj_tramp`, texlist OBJ_WINDY), CCL D_0CA1317C. SA Tools
     `DC_SA1/objdefs.ini` maps O POLINE to `WindyValley.Trampoline`;
   * code 0x0C909880-0x0C909EEE: 8 functions, about 1.6 KB, all todo (STG02 is 0 % C);
   * its launch is 8C07C738 (M): event request smode 6 / 7 / 8 by type, with a speed. 1ST_READ itself never calls
     8C07C738, so these are the "6/7/8" orders of section 2.

   The summary bullet, the 4.12 row and owner question 5 must change. A trampoline is a STG02 stage-overlay object,
   not a non-original preset.
2. **The item-icon popup is a hard dependency, not optional** (7.8 says "ITEM event or icon list").
   * 8C05E0F8 (M) appends to B_8C78C394 with no bound: `entry[num++]`, 20 entries of 16 B after a 12-byte header.
   * `num` is reset only by 8C05E2EE. That runs from the popup task exec 8C05E14E, which 8C05E0CE creates from
     8C058E48. LIB runs neither.
   * So the 21st broken box in a world writes entry[20] at **0x8C78C4E0, the jump-panel list**.
   * The icon list is not "UNVERIFIED which draws it": exec 8C05E14E animates it, disp 8C05E33C -> 8C05E3A0 draws it.
3. **The bomb is 3 functions, and it also needs the popup task.**
   * 8C05E59A (40 B) is missing from the xref index. It grows the bomb sphere by 10 per step up to 500, then sets
     -1 (off). Only the popup exec 8C05E14E calls it.
   * Without that task, a bomb's 10-unit sphere stays on forever: 8C05E582 sets the radius to 10, and 8C05E0CE's
     reset to -1 never runs.
   * The bomb pair is counted twice in 9.1: once in "air item box 13 fns" and once as "+ bomb 2 fns, 104 B".
4. **The per-object function lists miss code that the splitter did not index.** Ghidra has 23 functions in the object
   and SET ranges that `build/1ST_READ.xrefs.json` lacks. None has C. The first-wave ones:

   | Function | Size | Belongs to |
   |---|---|---|
   | 8C05E59A | 40 B | bomb |
   | 8C05E612 | 42 B | air-box edit print, `fatal` today |
   | 8C062B5C | 132 B | capsule, from 8C0627B4 |
   | 8C0656E4 | 86 B | checkpoint modes 2 / 3, from its exec |
   | 8C0676AC | 126 B | switch mode 1, from its exec |
   | 8C0683C0, 8C068560 | 262 + 278 B | spikes |
   | 8C0915D2 | 128 B | SET manager act change |

   * The spikes are 6 functions and about 880 B, not 4 / 672.
   * Recounted with Ghidra's boundaries (pools excluded), the first wave is **155 todo functions and about 17.7 KB of
     instructions**. The headline "~154 / ~18.7 KB" survives only because the errors cancel out: clipped sizes include
     pools, and the bomb and 8C058E48's callees (8C05E0CE, 8C05E680, 8C067DF4, 8C062634) are counted in two rows.
   * The split config and symbols need these boundaries before C can land. `code_8C05D9C0.c:14-19` already notes this
     for 8C05E0A0 / 8C05E612.
5. **8C091912 is not range-out code.** Its only caller is O_LevelItem 8C0698F6 (1.2). Remove it from W1 and from the
   7.8 "All" row. Range-out is 8C0919CC -> 8C0919E8 -> 8C09111A.
6. **The switch claims need correcting.**
   * DYNAMITE is not a switch reader. The callers of 8C067D84 are 8C061C66, 8C062046, 8C063220, 8C063480, 8C067BC8
     and 8C067C14. DYNAMITE (8C05B126) registers in its own list B_8C78C260 (count B_8C78C25C, reset 8C05B1DA), CCL
     id 2. What its "<- ID" selects is UNVERIFIED.
   * Rocket_HS / VS also **write** switches: 8C067D64 is called from 8C063220 and 8C063480.
   * Bit 1 ("changed") is consumed: the switch's 8C0679C8 clears it. The 7.4 merge ("entries that have bit 1") must
     snapshot before the executor, or merge bit 0 by difference.
7. **Item-box out-of-range records.**
   * Ground: 9 records = 3 x (-1), which clamps to **8** (magnetic barrier), plus 5 x 9 and 1 x 10, which clamp to 0.
   * Air: **3** records (-60 -> 8; 9 and 10 -> 0), not 2.
8. **Jump panels.**
   * 8C05EC40 clamps scl.x >= 10 or < 0 to 0, then takes the floor. Panel numbers are 0..9. The single disc record
     with 10.0 is a second start panel.
   * Panel 0 uses CCL D_8C1B9D4C (one sphere r 5); the others use D_8C1B9CB0.
9. **O_GRING.** The circle pattern skips i = 0, so it makes scl.x rings, not scl.x + 1. The code has no clamp. The
   per-ring bits are one byte, so a line of more than 8 rings overflows them.
10. **Harness evidence.**
    * There are **three** runs (dash_ec1, box_ec1, ring_ec1), not four. "As the 15 reference traces were" (risk 1)
      describes traces that do not exist yet.
    * `--create-task` leaves `tp->ocp = NULL` (`t_sim.c` ~1437-1466). The item box calls 8C091B68 unconditionally in
      its init, and 8C05D4CE / 8C06546E call 8C091B54 (ocp + 2 |= 4) unguarded. So box_ec1 ran with guest reads and
      writes at address 0x00000002, a path the game never takes.
11. **Overlay percentages.** Clipped: STG01 has 4,778 B of C out of 53,522 B (8.9 %), STG05 3,204 of 65,974 B
    (4.9 %), every other STG 0 %. Say "0-9 %".
12. **"The world task pool is 4096 tasks"** has no constant in `LIB/src/world`. It is a design figure from
    libsa1_interaction.md:565: UNVERIFIED in code.
13. **8C058E48 is not an "object manager block".** It allocates the head of a shared NJS_OBJECT draw chain:
    * 8C058F74 / 8C058F9A append 0x34-byte copies; the only caller is 8C059482;
    * level-3 task 8C058F62 draws the chain (8C099538, 8C036F12), and 8C058F20 frees it;
    * then it calls the eight resets 8C091890, 8C05E680, 8C0635C0, 8C062634, 8C05B1DA, 8C067DF4 and 8C05E0CE.

    8C091890 resets the game's condition array, so it touches nothing when there is no SET data.
14. **9.1 "Liftable crates S/M/L, 3 fns, 258 B".** S / M / L are already matched. The 3 todo functions are
    O_SAMPLE_SW (8C064D04 / 8C064D50 / 8C064E02). All four types have 0 placements.

### R.3 Gaps

1. **This design conflicts with `v1_arch.md`, the architecture doc of the same workflow.** Both are labelled "API 0.4
   draft".

   | | v1_arch | this doc (section 7) |
   |---|---|---|
   | Decision | (a) "mirrored SET objects" | direct tasks, no SET manager |
   | Spawning | the game's own SET manager task, `CreateElementalTask(2, 7, 0x8C090FA0)`, on host records in each world's condition array | `CreateElementalTask` from the layer |
   | Range-out | the real 8C0919CC | stays `ret 0`; ocp stand-ins in segment 0x18 with flag 8 and a LIB window |
   | API | `SA1_API_SET` | `SA1_API_OBJECTS` |
   | Events | SPAWNED / DESPAWNED / GONE / USERBITS / TOGGLE / PASSED | CONSUMED / ITEM / CHECKPOINT / GOAL / SWITCH |
   | 1.0 scope | adds rockets, the dash ring, ring lines and collision volumes | defers them to 1.1 |

   The owner rule favours v1_arch: the SET manager is game logic, and its oracle is the original manager via
   `--set`. This doc's "consumed" rule (task gone / exec == DestroyTask / terminal mode) also misfires: a task also
   disappears on a world rebuild, and a group's child rings go with the group. The game's own signals are bit 15
   cleared and `scUserFlag`.
2. **The Super Sonic slot is not mentioned.** A Sonic world keeps B_CHAOS7 at 0x0C900000 (libsa1_integration.md 2.5).
   A "stage theme", and the trampoline above, displaces Super Sonic in that world. Sections 4.12, 9.2 and risk 8 omit
   this.
3. **Some lowering dependencies are not listed.**
   * O_SWITCH's edit branch calls 8C034C80 (62 B, a print wrapper over the C runtime's vsprintf 8C10D400 ->
     8C10DBC4 ...; about 9.8 KB of static closure). It needs a `void` stub-policy entry.
   * O_ITEMBOX's init calls STG03 0x0C907998 when `GetStageNumber() == 0x302`: a cross-image call that needs a stub.
   * 8C05E612 is `fatal` today.
4. **Render.**
   * Per-task draw attribution does not exist in LIB: nothing in `rt_draw.c` or `sa1_capture.h` records the task. It
     needs a hook at the executor's display call. It is new work, not "the display walker knows tp".
   * The game's own visibility test 8C038D00 culls against the world's camera adapter, so objects GMod shows can be
     culled (v1_arch 1.4 / 5.3). Section 7.7 does not address it.
5. **Stage clear: the owner rule prefers the game's code.** Decompile 8C04EFE0 (554 B) and its small callees:

   | Callee | Size | Status |
   |---|---|---|
   | voices 8C04EF22 / 8C04EF52 | 96 B | todo |
   | camera 8C0213E0 / 8C021284 / 8C01AD2C | 374 B | todo |
   | 8C107AAA | 28 B | todo |
   | SetInputP, 8C08A5B6, 8C0509DA, 8C0BFAEE | - | already C |

   Keep only the results task 8C08ECB8 (454 B) and Sonic's 8C04F20A (402 B) as boundaries. About 1.1 KB gives the
   game's own clear pose, camera and voice, instead of a hand-written GOAL sequence. The capsule countdown 8C06270A
   also needs 8C049A4E (12 B, todo).
6. **Item-box safety.** Invincibility (item 1) also faults: 8C044284 (M) calls the fatal 8C044280, 8C044290 and
   8C04430A. Speed shoes (8C07D2DA, M, lowered), rings and 1-up are safe today.
7. **Reference coverage.** About 15 traces cannot cover:
   * 9 items x ground / air;
   * 5 switch kinds;
   * capsule open, checkpoint touch and restore;
   * the wide spring with parameters;
   * a jump chain, spikes, ring line / circle.

   Proving about 150 functions, mostly NON_MATCHING, needs roughly 25-30 references, all made with an ocp.
8. **Effort and ordering.**
   * W4 bundles a round-5-sized mirror, render attribution, a display world and the GOAL / CHECKPOINT boundaries. L is
     optimistic; XL is more realistic, or split W4 into a layer + events part and a render-attribution part.
   * W1 needs the popup TU from W2 (item 2 above), so the order is wrong.

### R.4 Required changes

1. Replace "no trampoline" with O POLINE (R.2.1) in sections 0, 4.12 and 11 Q5. Add it to the stage-theme list, or
   decompile its 8 functions and load the STG02 data in an opt-in world (it costs Super Sonic in a Sonic world).
2. Move the item-icon popup TU into W1 as a hard prerequisite of item boxes and of 8C058E48: 8C05E0CE, 8C05E14E,
   8C05E1E0, 8C05E28E, 8C05E2EE, 8C05E33C, 8C05E3A0, 8C05E57E and 8C05E59A, 9 functions, about 1.1 KB. Alternatively
   LIB drains `B_8C78C394.num` every step; either way, state the overflow into B_8C78C4E0.
3. Add the 7 unindexed first-wave functions (R.2.4) to the split and symbols. Correct the spikes, checkpoint, switch,
   capsule and bomb rows. Remove the double counts and give unique totals.
4. Drop 8C091912 from W1 / 7.8. Fix the switch readers and writers and the bit-1 merge rule (R.2.6).
5. W3: create every reference task with an OBJ_CONDITION (ssCondition 0x8001, `+0xC` = range^2), or use v1_arch's
   `--set` path. Then re-record box_ec1. Plan about 25-30 references.
6. Reconcile section 7 / W4 with v1_arch 5.x: one API 0.4, the SET manager in the world, GONE / USERBITS as the
   consumed signal. Keep this doc as the per-object catalogue (sections 2-6, 7.8) and let the owner choose the 1.0
   scope (rockets, dash ring, line rings in 1.0 or in 1.1).
7. Note the B_CHAOS7 slot conflict in 4.12 / 9.2 / risk 8. Add the 8C034C80 and STG03 0x0C907998 stubs and the
   8C038D00 culling issue to the 7.8 table.
8. Prefer decompiling 8C04EFE0 with boundaries at 8C08ECB8 / 8C04F20A over a LIB-written GOAL sequence. Keep the
   GOAL event as the notification.
9. Correct the minor facts in R.2.7-R.2.14: item clamps, jump-panel range and start-panel CCL, the O_GRING circle
   count, three traces, overlay percentages, the task-pool source, 8C058E48's role, the O_SAMPLE row.

**Verdict.** The catalogue is sound. Its addresses, parameter mappings (edit-mode rows), CCL tables, decomp counts,
disc counts and the three harness measurements all check out, and the first-wave decompile budget is about right
(about 155 functions, about 18 KB). Three findings change the plan:

* the final game **does** have a trampoline (STG02 O POLINE);
* the item-icon popup task is a **hard** dependency: without it the icon queue overflows into the jump-panel list,
  and the bomb never ends;
* section 7 is a second, incompatible "API 0.4" next to v1_arch's SET-manager design.

The harness references must also be made with an ocp.