# G-SA1 roadmap: from 0.9 to 1.0, the 1.x series, and a sketch of 2.0 (`v1_roadmap`)

Status: roadmap, written 2026-10-21 (planner step of workflow `gsa1-design-v1`). Read-only: nothing in `SADC/src`, LIB or
g-sa1 was changed. Scratch and measurements: `SADC/build/research/roadmap/` (`rstat.py`, `closure.py`, their outputs;
Appendix A).

Inputs, all read in full with their reviews (where a review and its document disagree, this plan follows the review):

* `v1_arch.md` (architecture of game objects), `v1_objects.md` (collectables and world objects), `v1_enemies.md`
  (badniks and bosses), `v1_triggers.md` (triggers, cameras, splines), `v1_npcs.md` (NPCs with text),
  `v1_machinima.md` (2.0);
* `gsa1_rounds_plan.md` (the queued rounds 5, 6a, 6b, the jukebox, and its owner sections 14.1a-14.1g);
* the 0.7.0 diagnoses `gsa1_070_{drown,bird,tails,translucency,hud}.md` and `gsa1_060_board.md`, each with its review;
* `HANDOFF.md`, the owner's 0.7.0 report and roadmap (rounds plan 14.1f), and the repositories as they stand today.

Abbreviations: SADC = `<project>\sadc`, LIB = `...\libsa1`, G-SA1 = `...\g-sa1`. "Round" = one workflow
round as used for 0.5-0.8 (a few decompilation / LIB / G-SA1 agents, then an independent verifier, then the release step
of rounds plan 2.5). "Batch" = one decompilation agent's unit of 20-25 functions. Bytes are 1ST_READ clipped sizes
(a function cut at the next function's start, pools included) unless stated otherwise; "asm" = no C yet.

---------------------------------------------------------------------------------------------------------------------

## 0. Summary

**Where things stand (measured today).** G-SA1 0.8.0 is installed. Round 5's G-SA1 half runs now and becomes 0.9.0
(interaction, pushing props, the INTERACT button, a minimal ring counter). LIB still reports API 0.2 with the host-object
section in. 1ST_READ game code has 2,343 functions / 414,574 B left in assembly. Everything this roadmap needs from it
adds up to about **203 KB** (1,096 functions) in the order below. On top of that come about 270 KB of boss overlays
(Perfect Chaos included) and about 80 KB of stage and adventure-field overlays (more for the unmeasured gimmicks).

**The release train** (one shippable round per line unless noted; each ends with an independent verifier and the
release step; section numbers point at the details):

| Release | Content | Decompilation (1ST_READ + overlays, still asm today) | Rounds | Section |
|---|---|---|---|---|
| 0.9.0 | round 5 in G-SA1 (running): damage and rings, attacks on NPCs, homing / lock-on, holdable and pushed props, balloons, INTERACT | none | running | 4.1 |
| 0.9.1 | the owner's 0.7 report: drowning drowns, jukebox steps aside for jingles, Amy's bird, Tails' ball, see-through afterimages, lives | none | 1 | 4.2 |
| 0.9.2 | the game's HUD | 6 fns / 1,964 B | 1 | 4.3 |
| 0.9.3 | Super Sonic as a character and as a transformation | none (landed) | 1 | 4.4 |
| 0.9.4 | board mode, with Board On / Board Off boxes | none required | 1 | 4.5 |
| 0.9.5 | followers | none (landed) | 1-2 | 4.6 |
| (LIB only) | S1 SET core, S2 common objects (no install) | 158 fns / 21,128 B | 2 | 5.1, 5.2 |
| **1.0.0** | **Game objects: the G-SA1 tab, collectables and world objects** | (above) | 1 + a fix round | 5.3 |
| (1.0.x) | multiplayer: other players see your character (only if the owner wants it, question B2) | none | 1 | 6.1 |
| 1.1 | triggers, splines and rides; Amy's balloon, air bubbles | 57 fns / 7,008 B | 1-2 | 6.2 |
| 1.2 | the game's own cinematic cameras, loops, rockets, dash rings, ring lines | 204 fns / 44,384 B | 2 | 6.3 |
| 1.3 | badniks (the 12 1ST_READ types) | 303 fns / 64,582 B | 2-3 | 6.4 |
| 1.4 | NPCs with your own text in the game's window | 115 fns / 10,358 B + ADV00 44 fns / 5,604 B | 2-3 | 6.5 |
| 1.5 | bosses without arenas: the rival fights, E-100 Zero | 42 fns / 8,938 B | 1 | 6.6 |
| 1.6-1.7 | arena bosses (E-101 Beta and Egg Hornet first, then the Chaos fights and the rest) | 88 fns / 19,252 B shared + about 243 KB of ten boss overlays | 1 + about 1 per boss | 6.7 |
| 1.8 | stage packs (the trampoline, stage badniks, E-103 / E-104), more objects, NPC phase 2 | 102 fns / 19,944 B + about 72 KB of stage / ADV overlays (more for bumpers, cannons, updrafts) | several | 6.8 |
| 1.9 | Perfect Chaos | B_CHAOS7 166 fns / 27,764 B + shared | 1-2 | 6.9 |
| 2.0 | the machinima creator (faces first) | 21 fns / 5,274 B | 10-12 | 7 |

**How it runs.** Four lanes work at once with clear ownership (section 3):

* **Decompilation lane, from now:** staged in `SADC/build/staging` (Wave A rule, rounds plan 2.3), so it never disturbs LIB.
  Order: the 1.0 objects first, then the camera subset, then the badniks, then NPCs.
* **Harness lane:** one owner of `SADC/harness/**` at a time. Next come the HUD oracle, then `--set` with its `.set.bin`
  sidecar and the 25-30 object references.
* **LIB lane:** serial, because every round patches `sa1_world_boot.c`, `sa1_world_tick.c` and `sa1_world.h`.
* **G-SA1 lane:** parallel through the feature seam.

The critical path to 1.0 is the LIB lane: the queue 0.9.1-0.9.5, then S1, S2 and S3. The 1.0 decompilation (about 8
batches) and its oracle finish well before S1 starts, if they start now.

**Owner questions.** Three genuinely block a design choice (section 10.1). Each has a recommended default, and the plan
proceeds on it:

* **B1** how trampolines (and the other stage gimmicks) reach a map;
* **B2** whether multiplayer matters in 1.x;
* **B3** whether bosses may fight in an "arena pocket".

Everything else has a default (section 10.2).

---------------------------------------------------------------------------------------------------------------------

## 1. State today (measured 2026-10-21, afternoon)

### 1.1 Versions and running work

| Item | State | Source |
|---|---|---|
| G-SA1 installed | 0.8.0 (backup of 0.7.0 kept) | HANDOFF "What is installed" |
| G-SA1 source | 0.8.0, `REQUIRED_API` 6; `feat/` holds `sh_interact.lua` (round 5, in progress), jukebox and surfaces | `sh_config.lua:4-5`, `addon/lua/gsa1/feat/` |
| Running | `gsa1_round5_gsa1.js` (5-gsa1 + fx-leftovers + verifier -> 0.9.0) | `sadc/tools/workflows/gsa1_round5_gsa1.js` |
| LIB | `SA1_VERSION_MINOR 2`; sections RENDER, CHARACTER_EX, COLLISION_EX, PREPARE, WORLD_DRAFT, HOSTOBJ (`SA1_API_HOSTOBJ`, line 1348); audio header 3 | `libsa1.h:68-73`, `libsa1_audio.h:61` |
| Not in LIB yet | SUPER, FOLLOWER, SCREEN (HUD), BOARD, companion (bird), SET, PATH, CAMERA, TEXT, FACE, journal | grep of `libsa1/include` |
| Playable kinds | Sonic, Tails, Knuckles, Amy, E-102, Big. **Tikal, Super Sonic and Eggman cannot tick yet** | `sh_config.lua:9-17`, `libsa1.h:535` |
| Multiplayer | design hook only: the state packet exists, there are no remote-player puppets (`gsa1.CreatePuppet` / `Pose` exist in the module but are unused) | `sh_net.lua:1-10`, `g-sa1/README.md:41, 1099` |
| Already decompiled for the queue | B_CHAOS7 Super Sonic tasks (24 M + 1 NM), 8C046A9C; follower F0; 8C0586A0 chunk incl. the board's 8C05872A (fix060); the bird's task 8C0DECDA / 8C0DE964 (M) | `rstat.py` |

### 1.2 Decompilation state of everything the roadmap touches (`rstat.py`, nested-#if-aware NON_MATCHING rules)

`tools/progress.py` reports 1,223 / 4,119 functions (220,168 / 973,430 B) for 1ST_READ. Its NON_MATCHING regex stops at
the first nested `#else` (`v1_triggers` R2.1, `v1_enemies` header), so `rstat.py` uses the reviews' `status2.py` rules.

| Range | Image / addresses | Functions | Bytes | M | NM | asm (fns / B) |
|---|---|---|---|---|---|---|
| 1ST_READ game code | 8C010000-8C10C000 | 3,953 | 920,358 | 1,217 | 393 | **2,343 / 414,574** |
| SET manager | 8C090EC0-8C091C20 | 31 | 2,972 | 5 | 0 | 26 / 2,876 |
| object helpers (incl. 8C058E48) | 8C0586A0-8C059A80 | 33 | 4,560 | 7 | 0 | 26 / 2,686 |
| common objects | 8C059A80-8C06AF00 | 422 | 58,982 | 22 | 3 | 397 / 55,756 |
| shield / invincibility displays | 8C043C00-8C0445A0 | 14 | 1,978 | 1 | 2 | 11 / 1,572 |
| rings | 8C0869E0-8C088120 | 21 | 5,414 | 11 | 10 | **0** |
| path-work objects (loops) | 8C06AF00-8C06C0C0 | 16 | 4,446 | 1 | 0 | 15 / 4,436 |
| camera | 8C019360-8C025960 | 185 | 46,538 | 0 | 0 | 185 / 46,538 |
| path library / event work | 8C089180-8C089F20 / 8C04B440-8C04C100 | 17 / 18 | 3,294 / 2,952 | 17 / 16 | 0 / 2 | **0 / 0** |
| badniks (12 execs, 6 TUs) | 8C0F2BA0-8C102E80 | 264 | 58,424 | 0 | 0 | 264 / 58,424 |
| enemy library / sprite effects | 8C047040-8C048B00 / 8C0445A0-8C047040 | 39 / 54 | 6,398 / 8,418 | 5 / 6 | 1 / 0 | 33 / 5,068; 48 / 7,516 |
| animals | 8C015720-8C017B20 | 46 | 8,670 | 37 | 9 | **0** |
| E-100 Zero / rival AI | 8C102E80-8C107800 / 8C082D60-8C0869E0 | 52 / 58 | 17,716 / 11,646 | 24 / 17 | 3 / 34 | 25 / 5,398; 7 / 1,572 |
| Chaos code + boss title / gauge; motion weld | 8C0EEAA0-8C0F2BA0; 8C018120-8C019360 | 83; 5 | 14,674; 4,578 | 0 | 0 | 83 / 14,674; 5 / 4,578 |
| MSG engine / talk + hint API / talk manager / thread lists | 8C0548E0.. / 8C05F000.. / 8C10A120.. / 8C099A00.. | 40 / 31 / 8 / 23 | 4,568 / 2,848 / 814 / 852 | 0 | 0 | all asm |
| face code / player event setters | 8C07AEF8-8C07B300 / 8C07C120-8C07C400 | 7 / 15 | 974 / 654 | all | 0 | **0** |
| event library / Eggman | 8C0491C0-8C04B440 / 8C0ED880-8C0EEAA0 | 106 / 11 | 7,702 / 4,468 | 1 / 0 | 0 | 105 / 7,696; 11 / 4,468 |
| ADV00 / ADV02 / ADV03 people | ADV00 0C90F2C0.. / ADV02 0C911D60.. / ADV03 0C902F00.. | 44 / 36 / 45 | 5,604 / 4,834 / 5,610 | 0 | 0 | all asm |
| STG02 O POLINE (trampoline) / STG02 whole | 0C909880-0C909F40 / image | 8 / 207 | 1,588 / 46,948 | 0 | 0 | all asm |
| B_E101 / B_EGM1 / B_CHAOS0 / B_CHAOS7 | images | 51 / 137 / 75 / 191 | 9,054 / 16,016 / 19,126 / 33,604 | 0 / 0 / 0 / 24 | 0 / 0 / 0 / 1 | B_CHAOS7 166 / 27,764 |

Two conclusions:

* **Everything 2.0's first phases need is already C** (faces, event work, paths, look-at).
* **The 1.x content is the long pole.** The objects, camera, badniks and NPC engine are 0-5 % C.

### 1.3 Data on the owner's disc that the roadmap reads (`SONICADV/`, measured today)

| Data | Files | Bytes | Used by | Check |
|---|---|---|---|---|
| SET layouts `SETllaaC.BIN` | 156 | 1,383,904 | object catalogue, `--set` oracle, layout import | = 156 x 0x20 + 43,091 x 0x20 (header + records) |
| camera volumes `CAM*.BIN` | 101 | 319,680 | the game's camera (1.2), synthetic CAM references | = 101 x 0x40 + 4,894 x 0x40 |
| `OBJ_REGULAR.PRS` | 1 | 155,785 | common object textures (already loaded by LIB) | SHA-1 in `sa1_manifest.c` |
| `CON_REGULAR.PRS` | 1 | 27,268 | the HUD | loaded from 0.9.2 |
| badnik archives `E_*.PRS` | 12 | 411,227 | 1.3 (per-world texture loading) | |
| boss overlays `B_*.PRS` | 14 | 3,735,663 | 1.6-1.9 | |
| `ADV0*.PRS` | 6 | 4,644,291 | 1.4 NPC worlds (ADV00 first) | |
| `STG02.PRS` | 1 | 733,811 | the trampoline (1.8, question B1) | |
| talk files `*_MES_*.BIN` | 99 | 1,169,620 | 1.4 "import a game line" (by reference) | |
| event overlays `EV0*.PRS` | 207 | 11,634,447 | 2.0 event motions (404 distinct, 4.6 MB of keys) | |
| event sound banks `E_*.MLT` | 51 | 6,701,280 | 2.0 audio | |
| `EVENT_ADX_US.AFS` | 1 | 65,200,128 | NPC voices, 2.0 audio (2,044 clips) | |

Nothing from these files goes into the DLL, the addon, saves, dupes or project files. Every client reads its own disc
(owner rule; the reviews' "by reference" rules in 2.D16-D18).

---------------------------------------------------------------------------------------------------------------------

## 2. Decisions this plan takes (reconciling the six documents)

The six documents were written in parallel and overlap. Where they conflict, this plan decides as follows. Every item
cites the review that forces it.

**Objects**

* **D1. One object architecture.** `v1_arch` design A wins: the game's own SET manager runs in every character world on
  host records, and the GMod server owns the shared condition bits. `v1_objects` section 7 (direct tasks, a LIB window,
  `ocp` stand-ins with flag 8) is dropped (`v1_objects` R.3.1). `v1_objects` stays the per-object catalogue (its
  sections 2-6 and 7.8).
* **D2. SET attach the game's way.**
  * Point the world's `D_8C1C143C[level*8+act]` and `D_8C1C0EDC[...]` at the generated list and at a SET block in
    segment 0x18, then let the game's own 8C091016 run (mode 0 and mode 2 stay game code).
  * The manager task is level 8 (`v1_arch` R.2-2, G2).
  * Single-character worlds run 8C091380, follower worlds 8C091148 (R.2-1).
* **D3. The shared state.** It is bit 15 and bit **0x0004** (ground item boxes, checkpoints, grass), plus `scUserFlag`.
  0x0010 marks what the game's reset keeps. A forced state on a live task always means FreeTask and a respawn
  (`v1_arch` R.2-3, G3).
* **D4. Switches are keyed by switch id**, through `B_8C78C5B4[32]`. Snapshot before the executor, because the switch
  consumes bit 1; merge bit 0 by difference (`v1_objects` R.2.6, `v1_arch` G4).
* **D5. The window is budgeted per CCL id** (about 200 live rings for id 7's 256 entries). The follower rule of
  `v1_arch` G6 applies once followers exist.
* **D6. The camera position comes before the range-out.**
  * The `ret 0` stub of 8C0919CC goes only after `camera_twp->pos` is real.
  * Either S1 adds `sa1_character_set_view`, or S1 writes player 0's position as an interim (`v1_arch` R.2-9, G7).
* **D7. The virtual stage is per world.**
  * Level 1 by default; Super Sonic worlds keep level 19.
  * 0x0302 is never offered without STG03: O_ITEMBOX calls STG03 code there (`v1_arch` G8).
* **D8. The item-icon popup TU is part of S1.** It is a hard prerequisite of item boxes and of 8C058E48: without it the
  icon queue overflows into the jump-panel list and the bomb never ends (`v1_objects` R.2.2-3).
* **D9. Stage clear is decompiled, not re-written.**
  * 8C04EFE0 (554 B) gets C. The results task 8C08ECB8 and Sonic's capsule task 8C04F20A stay boundaries.
  * The camera hooks 8C0213E0 / 8C021284 / 8C01AD2C stay stubs until 1.2.
  * A GOAL event notifies G-SA1, which then gives control back (`v1_objects` R.3.5, R.4.8).
* **D10. Object references are made with real OBJ_CONDITIONs** through the harness `--set` path. There are 25-30 of
  them. `set_ec1_common` uses the 270-record filter in 1.0 and the 289-record one in 1.3 (`v1_objects` R.2.10, R.4.5;
  `v1_arch` G10).

**Entities and APIs**

* **D11. One entity model.** A small base `gsa1_base` (the `GSA1Object` tag, editing visibility, undo / cleanup, seam
  registration) has three kinds of children:
  * catalogue-generated `gsa1_obj_<type>` classes for SET objects (`v1_arch` 4.2);
  * file-based `gsa1_trigger` with preset subclasses (`v1_triggers` 3.1);
  * one `gsa1_spline_node` entity per point (not entity modifiers; `v1_triggers` R5.8).

  The board round (0.9.4) builds its Board On / Board Off boxes as the first two `gsa1_trigger` presets, so 1.1 extends
  them without breaking anyone's saves.
* **D12. LIB API sections are named by feature macro** (`SA1_API_SCREEN`, `_SUPER`, `_BOARD`, `_FOLLOWER`, `_SET`,
  `_PATH`, `_CAMERA`, `_TEXT`, `_FACE`, ...). The "API 0.4 draft" labels in five documents are not binding.
  `SA1_VERSION_MINOR` is bumped in landing order by the release step (rounds plan 2.5).
* **D13. Journal-ready inputs.** Every new LIB call that injects state into a world (SET state, rides, camera volumes,
  faces, companions) is queued and applied at the pair start, as host objects are. 2.0's journal (M0) then records them
  at the latch without a retrofit (`v1_machinima` 4.2).
* **D14. The extension segment gets a pool allocator with a free list.** It comes in the first round that frees: 1.1's
  path API. It also serves the CAM tables (1.2) and the event-motion cache (2.0) (`v1_triggers` R2.10, R5.3;
  `v1_machinima` G4).
* **D15. One capture attribution mechanism.** A hook at the executor's display call tags draws with the task, in S1.
  The bird's interim world-space rule (0.9.1, `gsa1_070_bird` R.4 F5) moves onto it in S1.
* **D16. The HUD round's 2D record carries texture identity** (texlist and number) and a reserved memory-texture
  handle / serial field. 1.4's message window then extends it instead of adding a second record kind (`v1_npcs` R3.7).
* **D17. Start lives have one implementation.** 0.9.1 provides `SA1_CREATE_START_LIVES` (drown E2); the HUD round adds
  only the start time and the clock (`gsa1_070_hud` F4).
* **D18. Game text and icons are stored by reference.**
  * Imported NPC lines, game subtitles and game audio are references that each client resolves from its own disc.
  * Spawn-menu icons are rendered at run time and kept in memory (`v1_npcs` R3.8 over `v1_arch` Q6's disk cache; a
    non-blocking question, 10.2).

**Order and slots**

* **D19. Rockets, the dash ring and line rings come after the camera (1.2).** The rockets' closure reaches the camera
  function 8C0211AC (`closure.py`). Amy's balloon and air bubbles need no camera and come in 1.1.
* **D20. The Knuckles radar comes with the emerald shards (1.8),** not with 1.0. The shard pulls Knuckles' hunt code,
  so `gsa1_070_hud` H5's default moves.
* **D21. Super Sonic and the overlay slot.**
  * The owner chose a transformation that re-creates Sonic in place as kind 8 (rounds plan 14.1a, 11.3). A normal
    Sonic world then no longer needs B_CHAOS7 at 0x0C900000.
  * A stage pack (1.8) can therefore occupy that slot in every world except kind 8. Super Sonic sees stage-pack objects
    as puppets only.
  * UNVERIFIED: no Super path of SonicMain runs without equipment 0x8000. The harness image guard checks it.
* **D22. Rival fights need no boss gauge and no HUD.** They need the same-world player 1 from 6b and host-supplied
  positions (`v1_enemies` R.2.7).
* **D23. Event pacing is 2.0's first research item** (`v1_machinima` R.2-2, G2). The display pass is never enabled.

---------------------------------------------------------------------------------------------------------------------

## 3. How the work is organised

### 3.1 Lanes

| Lane | Owns | Runs |
|---|---|---|
| Decompilation | `SADC/build/staging/**`. Split / symbol fixes (`config/`) only through a landing agent | from now, in roadmap order (8) |
| Landing | `SADC/src/**` landing, `LIB/gen/policy/stubs.tsv` (rounds plan 2.3, L0 pattern). LIB relowers on every SADC change | once per LIB round, at its start |
| Harness / oracle | `SADC/harness/**`, `build/traces/ref`, sidecar formats (`sa1_trace_ext.h`) | one owner at a time: hud-oracle, then `--set` objects, paths, CAM, badniks, NPC, events |
| LIB | one round at a time. Every round patches `sa1_world_boot.c`, `sa1_world_tick.c`, `sa1_world.h`, `libsa1.h` (own section) and `tests/build_replay.py` (own list) | serial (3.2) |
| G-SA1 | the feature's own files (`bind_<f>.cpp`, `core/<f>.*`, `feat/{sh,cl,sv}_<f>.lua`, tests), plus named small patches on `gsa1_module.cpp`, `session.cpp`, `cl_menu.lua`, `cl_character.lua`, `sh_config.lua` | parallel; a release only at a quiescent tree (rounds plan 2.5) |
| Verify / release | read-only verifier, then the orchestrator's release step; install only while GMod is closed | per round |

### 3.2 LIB order (shared world files)

0.9.1 fix070, then 0.9.2 HUD, then 0.9.3 6a, then 0.9.4 board, then 0.9.5 6b, then S1, then S2, then 1.1 path, then
1.2 camera, then 1.3 actors, then 1.4 NPC worlds and the coroutine shim, then 1.5 F2 promotion, then 1.6 arena worlds.

Two pairs may swap if one is late:

* 1.3 and 1.4 (both live mostly in their own LIB files; their patches of the shared world files are small);
* the board (0.9.4) and 6b (0.9.5): the board's review C8 asks for "board after 6a, before 6b-lib-proxy".

### 3.3 Gates (unchanged, rounds plan 2.1 / 13)

* the SADC full build (every check OK);
* LIB `build.py --test` (the 10 strict references plus every list a round adds: HOSTOBJ, SUPER, FOLLOW, BOARD, SET, PATH,
  CAM, ...), x86 = x64 once per round;
* harness `regress.py --route`;
* g-sa1 `build.py --test` and `lua_check`;
* each round's own tests.

A deviation from game logic is allowed only behind a host option that the references never use.

### 3.4 Effort

Throughput so far:

* P0 + P1, about 1,000 functions with verification, took about two weeks;
* fix060 decompiled 18 functions inside one round;
* Wave A staged 45.

So **1 batch is about 20-25 functions**, and a decompilation-heavy release costs its batches plus one landing and replay
round. The table in section 0 counts rounds. The batch counts are in section 8.

---------------------------------------------------------------------------------------------------------------------

## 4. The queue before 1.0 (0.9.x) and what each item unlocks

### 4.1 0.9.0: round 5 in G-SA1 (running)

* **Goal:** rounds plan 9.3 plus owner additions A-C: pushing props, the INTERACT button for doors and buttons, and a
  minimal ring counter isolated in `feat/cl_hud.lua` (which the HUD round replaces).
* **LIB:** done (host objects API section, HOLDABLE, BALLOON, 14 HOSTOBJ references strict).
* **Owner gets:** NPCs hurt you and you lose rings; your attacks hurt NPCs; E-102 locks on and Sonic homes onto NPCs and
  vehicles; you can pick up, throw and push props; balloons pop and launch; doors and buttons open; PvP is on; every
  target category has its own switch.
* **Unlocks for 1.x:**
  * the host-object registry and proxy machinery (ENEMY / HAZARD for badnik mirrors, kind 7 ATTACKER, TALKABLE);
  * the client-detects / server-validates pattern that objects reuse;
  * HOLDABLE, which carries the liftable crates of 1.0.
* **Patch S3 must make:** round 5's scanner `cl_interact.lua` must skip `GSA1Object` entities (`v1_arch` 4.2). Round 5 is
  already running, so S3 makes that small patch.

### 4.2 0.9.1: the 0.7 report fixes ("fix070")

**Decompilation:** none.

**LIB**

* Drowning:
  * W1, deep host water: the water object is inserted inside the game's 128-entry copy and counted against the pool
    budgets (drown review R4.1);
  * E1, `SA1_EVENT_DEATH` for drowning (smode 39) and death zones (8C07BD48), with the RESTART-id-2 fallback and a
    TIME_UP cause;
  * E2, `SA1_CREATE_START_LIVES` (clamped 0..127);
  * A1, the mixer reports the game's music state (audio header 4);
  * a separate "drowning" option, not `SA1_COPT_NO_DEATH` (drown review R4.8).
* Amy's bird: F1 `sa1_character_set_companion(SA1_COMPANION_BIRD)` with the gate-aware boot of bird review R.4 (a)-(c),
  F3, and F5 (the companion interpolated in world space; interim rule).
* Tails: F-T1, screw interpolation for models interpolated in the drawn root's frame (`gsa1_070_tails` 6.1).
* Translucency T4 (minor fidelity).

**Harness:** the bird reference (`--no-objects --create-task`, HOSTOBJ list).

**G-SA1**

* Drowning:
  * G1 death handling and lives, in the same release as E1 (drown review R4.3);
  * G2 session-log lines;
  * J1, the jukebox steps aside for the game's jingles and holds until the respawn (R4.4).
* Bird F4: the companion feature through `GSA1.AddCharacterOption`, with the checkbox "Amy's bird"; not applied to
  followers.
* Translucency: T1 (`$alpha` for lit translucent batches), T2 tests, T3 startup check; T7 "Afterimages" switch
  (optional).

**SADC:** T5 comments only.

**Verification**

* W1 test with 128 or more objects in range: the breath timer counts at 300 and 476 deep;
* the DEATH -> RESTART -> respawn timing (respawn at t + 5.0 s);
* the jukebox hold sequence;
* `companion_test`: 1 / 0 / 1 / 0 birds, and the turn case within 0.25 units;
* the Tails ball copy: 0 of 49 pairs stick out;
* translucency T2;
* the 10 references and HOSTOBJ strict.

**Owner gets**

* the character drowns: countdown music, the game's drown motion, then a respawn, and a life is lost;
* the jukebox gets out of the way of the countdown and jingles;
* Amy's Birdie follows her (Q menu switch);
* no tails poke out of Tails' ball;
* afterimages are see-through as on the Dreamcast.

**Unlocks**

* The death, lives and respawn path, used by:
  * checkpoint respawns, 1-up boxes and the capsule's lives display (1.0);
  * the Kill trigger action (1.1);
  * badnik hits (1.3).
* The music state, used by the speed-shoes / invincibility music (85 / 55) and the capsule jingle (1.0).
* The companion API pattern, used by 2.0 actors.

### 4.3 0.9.2: the HUD (`gsa1_070_hud.md` F1-F9 with its review)

**Decompilation:** Big's HUD 8C0EB160, 8C0E6FE0, 8C0E70E4, 8C0E74FC, 8C0E746C, and the number draw 8C08EE98: **6 fns /
1,964 B** (`closure.py` wave H).

**LIB**

* F2: `njDrawSprite2D` keeps the priority and the executing task, and a real `njDrawPolygon2D` records POLY2D, with the
  texture identity and memory-texture fields of D16;
* F3: the HUD pass at the game's call site (before 8C08A5BE);
* F4: the clock, E-102's time limit, start time (start lives from 0.9.1);
* F5: CON_REGULAR;
* F6: `sa1_character_screen` (`SA1_API_SCREEN`).

H1, the drowning countdown digits, comes from the game's 8C07BDA0 through 8C08EE98 (drown review R4.5).

**Harness:** F7, the oracle of the HUD functions' 2D draws.

**G-SA1:** F8, feature `hud`. It replaces 0.9.0's minimal counter, has a "HUD" page, and anchors to the corners at
3440 x 1440.

**Verification:** `hud_test` cases 1-11, `capture_test` "2d", the F7 oracle, `big_ec3` strict, the 10 references.

**Owner gets:** the game's own HUD:

* rings, lives and time;
* E-102's countdown;
* Big's weight / rings / lives, reel and line tension;
* the drowning digits over the head.

**Unlocks**

* 1.0: the lives counter, the item-box icon popup and the checkpoint time digits (all are 2D task draws that arrive
  through `SA1_HUD_CHARACTER`);
* the 1.4 message window (POLY2D);
* the 1.6 boss title and life gauge;
* 2.0 subtitles in the game's look.

### 4.4 0.9.3: Super Sonic (rounds plan 11, owner answer 14.1a: both)

* **Decompilation:** none (landed: B_CHAOS7 24 M + 1 NM, 8C046A9C).
* **LIB:** `6a-lib-super`: kind 8, the level-task stand-in, DISC level 19, archives, start rings 50, drain flags, the
  revert fix and the water floor.
* **G-SA1:** `6a-gsa1`, plus the transformation from normal Sonic (50+ rings and a button; position, facing, speed and
  rings carry over through a re-create in place).
* **Verification:** `super_test`, the SUPER list strict (x86 / x64), lsd_audit with Super Sonic.
* **Owner gets:** Super Sonic in the character list, and turning super during play.
* **Unlocks:** Perfect Chaos (1.9); the per-world level-19 rule (D7); the slot rule for stage packs (D21).

### 4.5 0.9.4: board mode (`gsa1_060_board.md` with review R.4)

* **Decompilation:** F1 is landed (8C0586A0 chunk). Optional C4: the STG08 start push 0C90E810.
* **LIB:**
  * F2, the 0C900AE0 stub -> void;
  * F3 `sa1_character_board` (`SA1_API_BOARD`);
  * F4 board PVRs;
  * F5 DISC level 38 replay;
  * C5 a secondary stage bank per mixer;
  * C7 re-entry.
* **G-SA1:** F7 / C3, feature `board`: the Q-menu switch and key binder. **The Board On and Board Off boxes are the first
  `gsa1_trigger` presets (D11)**, shown under Entities > G-SA1 and later in the G-SA1 tab.
* **Verification:** `board_test` (a)-(f), `board_sh_*` strict, the Lua seam test, the owner test.
* **Owner gets:** Sonic and Tails on the board, from the menu or by running into a Board On box.
* **Unlocks:** the trigger entity family that 1.1 extends.

### 4.6 0.9.5: followers (rounds plan 12, owner answer Q1: a separate world per follower)

* **Decompilation:** none (F0 landed).
* **LIB:** same-world F2 (test configuration), proxy-leader worlds (C), per-kind bring-up.
* **G-SA1:** the Followers page.
* **Verification:** FOLLOW references strict for players 0 and 1; bring-up gates per kind.
* **Owner gets:** Tails (and the kinds that pass) follows you, flies to keep up and reappears when lost.
* **Unlocks:**
  * 1.0: the SET window's follower rule (D5) and follower ring credit;
  * 1.5: the rival fights (F2 promoted to a supported mode);
  * 2.0: multi-actor scenes.

### 4.7 Prerequisite matrix (which queued item each 1.x feature needs)

| 1.x feature | round 5 | 0.9.1 | 0.9.2 HUD | 0.9.3 Super | 0.9.4 board | 0.9.5 followers | MP round |
|---|---|---|---|---|---|---|---|
| 1.0 objects | registry pattern, scanner exclusion, HOLDABLE crates | death / lives / respawn, music state | lives, item icons, checkpoint time | virtual stage 19 | - | window rule, ring credit (if landed first) | shared objects seen by others |
| 1.1 triggers / splines | - | Kill action | text in the Event action (game look) | - | **the trigger base and Board actions** | - | shared triggers |
| 1.2 cameras | - | - | - | - | - | followers keep the adapter camera | - |
| 1.3 badniks | ENEMY / HAZARD / ATTACKER | death | score | - | - | owner = nearest local world | **hand-off between players** |
| 1.4 NPCs | TALKABLE | - | **POLY2D, the 2D layer** | - | - | - | "show text to others" |
| 1.5 rivals, Zero | ATTACKER | death | - | - | - | **F2 same world** | - |
| 1.6+ arena bosses | ATTACKER | death | **title, gauge** | Perfect Chaos | - | - | co-op later |
| 2.0 machinima | journaled host objects | - | subtitles | no faces on Super Sonic (R.3 G12) | - | multi-actor | face / replay sync |

### 4.8 What runs in parallel from now

* **Decompilation lane** (staging; can start while 0.9.0 still runs):
  1. 1.0 waves S1, then S2a-d (158 functions, about 8 batches, section 5). First a split / symbol step:
     * split the 8C090FA0 xrefs entry into its seven functions (`v1_arch` R.2-6);
     * add symbols for the Ghidra-only first-wave functions 8C05E59A, 8C05E612, 8C062B5C, 8C0656E4, 8C0676AC, 8C0683C0,
       8C068560 and 8C0915D2 (`v1_objects` R.2.4).
  2. Then the camera subset (T3a, about 5 batches; FPU-dense).
  3. Then the badniks (E1, 13-16 batches).
  4. Then the NPC engine (N0 split fixes, N1, ADV00 image setup; about 7 batches).
* **Harness lane**, after the HUD oracle:
  * `--set FILE` (gdFs serves the file in place of `SETllaaC.BIN`) and the `.set.bin` sidecar (the condition array,
    live SET tasks' taskwk / motionwk, SET events);
  * then the 1.0 references: rings and groups; item boxes x 9 contents, ground and air; SPRING / SPRINGB / O_SPRING_H with
    parameters; dash panel; a jump chain; the five switch kinds; spikes hurt; checkpoint touch and restore after a death;
    capsule open; a death or restart trace that settles the 8C091890 reset rule (`v1_arch` R.2-11);
    `set_ec1_common` with 270 records.
* **Docs:** `sa1_code_map` corrections listed by the reviews (8C08ECB8 = the results tally, 8C0F1C40 = the boss title,
  8C0F2228 = the gauge, `B_8C754EC8` = the sub-step).

---------------------------------------------------------------------------------------------------------------------

## 5. 1.0: Game objects (collectables and world objects in the G-SA1 tab)

### 5.1 S1: the SET core (LIB round; LIB minor bump, no install)

**Decompilation landed:** wave S1, **44 fns / 4,424 B**: the SET manager 8C090EC0-8C091C20 including 8C0915D2; the
object-system init 8C058E48 and its resets 8C091890, 8C05E680, 8C0635C0, 8C062634, 8C05B1DA, 8C067DF4; the item-icon
popup TU 8C05E0CE-8C05E59A (D8); and 8C04FE4A. Their `stubs.tsv` lines go, starting with 8C0919CC `ret 0` and
8C0918E6 `fatal`.

**LIB (`SA1_API_SET`, `v1_arch` 5 with R.5)**

* The catalogue: `LIB/gen/gen_objlist.py` builds it from the disc's 33 object lists (im / level / flags / distance), with
  a parameter schema from the game's edit labels (`v1_objects` Appendix B).
* `sa1_set_add / update / remove / state / configure / poll_set_events`.
* Events: SPAWNED, DESPAWNED, GONE (bit 15), CONSUMED (0x0004), USERBITS, TOGGLE, PASSED; GOAL / CHECKPOINT / ITEM as
  notifications.
* Attach the game's way (D2), and run 8C058E48 at attach.
* Forced state (D3); window per CCL id (D5); the follower rule (D5); per-world virtual stage (D7).
* `sa1_character_set_view`, with `camera_twp->pos` real before the stub goes (D6).
* Render: capture attribution at the executor's display hook (D15); world-space interpolation for attributed draws;
  instances for rigid models; `sa1_model_mesh`; `sa1_world_render`.

**Verification**

* `set_test.c` cases 1-7, 9, 10 (churn with 5,000 rings, GONE, respawn, group bits, two worlds, determinism, followers,
  render, capacity);
* `set_ring_ec1` strict in STAGE and LIBSA1 list modes (rings, O_GRING and the air box already have C);
* the 10 references and every list strict.

**Prerequisites:** round 5 (patterns), 0.9.1 (respawn), 0.9.2 (2D output), and 0.9.5 if it landed first.

### 5.2 S2: the common objects (landing and replay round; LIB minor bump, no install)

**Decompilation landed (1.0 total: 158 fns / 21,128 B with S1)**

| Wave | Content | fns / B |
|---|---|---|
| S2a | O_ITEMBOX 8C05D10C and its nine content handlers (data table `D_8C1B9BA0`: 8C05D764 ... 8C05D964, 8C05E582, 8C05E5C2, 8C05E59A); O_ItemBoxAir's display 8C05DC60; the shield / invincibility displays 8C043C00-8C0444C4 (11, fatal stubs today) | 33 / 4,024 |
| S2b | SPRING 8C065800, SPRINGB 8C065EE4 and the launch 8C065A98; O_SPRING_H 8C067E40; dash panel 8C059B48 / 8C059A80; O_JPanel 8C05EB76 and the landing effect 8C05EFBA | 24 / 5,168 |
| S2c | O_Save_Point 8C064F40 (+ 8C0656E4, sparkle 8C045BB6, store 8C064EF6, restore 8C064EB2); O_SWITCH 8C067480 (+ 8C0676AC) and the switch-flag set / read / reset; O_TOGE 8C0682A0 (+ 8C0683C0, 8C068560); O_SAMPLE_SW | 42 / 5,514 |
| S2d | O_RELEASE 8C062640 (+ 8C062B5C) and the stage clear 8C04EFE0 (D9); the voice and jingle helpers stay event stubs | 15 / 1,998 |

Today's `ret 0` / `void` / `freetask` lines for 8C05E5C2, 8C05DC60, 8C05EFBA, 8C064EF6 and 8C04EFE0 go. The boundaries of
D9 stay. The debug edit paths (8C042DC4 `ret 0`, the print wrapper 8C034C80) and the STG03 call of O_ITEMBOX get explicit
stub entries (`v1_objects` R.3.3).

**LIB**

* the switch merge by id (D4);
* the jump-panel list, at most 10 per world;
* one capsule per world;
* CHECKPOINT from 8C064EF6, and the restore path wired to the host respawn (`v1_arch` G11);
* GOAL at 8C04EFE0's end;
* ITEM from the popup list;
* single-world effects of the bomb and invincibility documented: actors come in 1.3 (`v1_arch` G16).

**Verification**

* the 25-30 references of 4.8 replay strictly in LIB (STAGE list mode), x86 = x64;
* `set_ec1_common` (270 records) strict;
* `harness diff` per function;
* `regress.py --route` on all references.

### 5.3 S3: the G-SA1 tab, release 1.0.0

**G-SA1, feature `objects` (`v1_arch` 4 and 6)**

* The creation tab "G-SA1" (`spawnmenu.AddCreationTab`). Its tree: Collectables / World objects / Goals / Hazards, plus
  Triggers (the board presets), with Badniks / Bosses / NPCs greyed as "coming".
* Catalogue-generated classes (D11), also under Entities > G-SA1.
* Icons rendered from the owner's disc, in memory (D18).
* Properties with the game's own labels and host units:
  * item content as a list of 9;
  * spring "Add Y speed" and "No-control steps";
  * dash panel "Speed" (default 14 = 3,360 HU/s) and "Disable time" (60 steps);
  * ring line / circle count, spacing and shape;
  * switch kind / id / timer;
  * jump panel number.
* Tools:
  * `gsa1_place`: ghost preview, surface alignment, ring lines and circles made of O_GRING groups of at most 8 rings
    (`v1_arch` R.2-8; the circle makes `scl.x` rings);
  * `gsa1_link`: switch ids and the jump-panel order, warning at the 10-panel limit.
* Server authority: `gsa1_obj_ev` validation; the `State` / `UserBits` / `RespawnAt` NetworkVars; respawn timers and
  "Reset G-SA1 objects".
* Saves, dupes, undo, cleanup and limits.
* Instanced drawing and puppets.
* The page Options > G-SA1 > Objects (`v1_arch` 4.6).
* Small patches: the round-5 scanner and `cl_collision.lua` skip `GSA1Object`; the character passes its view (`SetView`);
  follower ring credit.
* Goals: on GOAL, a "Clear" banner and the jingle; after the game's clear pose (about 5 s) G-SA1 gives control back and
  resets the clock (non-blocking question 10.2-O2).

**Verification**

* `feat_objects_test.cpp`, `feat_objects_binding_test.cpp`, `feat_objects_test.lua` (catalogue validity, state machine,
  `RestoreNetworkVars` round trip, server validation);
* single-player through the server path;
* the owner checklist (`v1_arch` 7.3);
* measure 500 rings on the owner's machine (Lua drawing cost, `v1_arch` risk table).

**Plan a 1.0.1 fix round after the owner's test.** Most of 7.3 can only be checked in game (`v1_arch` R.6).

**What the owner gets in 1.0:**

| In the G-SA1 tab | What it does (the game's own code) |
|---|---|
| Ring, ring line, ring circle | collected with the game's sparkle and sound; 100 rings give a life |
| Item box (ground) and air item box, 9 contents | speed shoes, invincibility (with its music), 5 / 10 / random rings, barrier, magnetic barrier, 1-up, bomb; the icon pops up on the HUD |
| Spring, air spring, wide ("triple") spring | the game's launch speeds; the air spring's no-control time |
| Dash panel | the game's boost: 14 units per step for 60 steps unless changed |
| Numbered jump panels | Sonic, Tails, Knuckles and Amy fly from panel to panel; E-102 and Big ignore them, as in the game |
| Checkpoint | lights up; on death you come back there with 0 rings, as in the game |
| Switch | toggle / push / timed / on-only; drives other objects with the same id |
| Spikes | hurt as in the game |
| Liftable crates | pick up and throw (round 5's carry) |
| Egg capsule (goal) | press the button: the capsule bursts, animals fly out, the clear pose, jingle and banner |

Lives and shields have no objects of their own in SA1: they come from item boxes and 100 rings (`v1_objects` 4.2). Not
in 1.0: the trampoline (question B1, 1.8), rockets / dash rings / ring lines (1.2), Amy's balloon and air bubbles (1.1),
emblems / upgrades / iron balls / shards (1.8).

---------------------------------------------------------------------------------------------------------------------

## 6. The 1.x series

### 6.1 Multiplayer round (only if the owner wants it, question B2; default: right after 1.0.1)

* **G-SA1:**
  * the server relays `ply.GSA1State` with `net.SendPVS` (unreliable, about 20 Hz);
  * other clients run `gsa1.CreatePuppet(kind)` / `gsa1.Pose(...)` per packet and draw the puppet like the local
    character, interpolated (`sh_net.lua` header);
  * objects' state already goes through the server from 1.0.
* **LIB:** none known (the puppet path exists in the module).
* **Verification:** a two-client test (a ring taken by one player disappears for the other; PvP hits; puppets). It needs
  a second GMod client. UNVERIFIED whether the owner can run two instances.
* **Owner gets:** friends see your character; PvP and shared objects become real.
* **Unlocks:** the badnik owner hand-off between players (1.3); "show NPC text to others" (1.4).

### 6.2 1.1: triggers, splines and rides (`v1_triggers` T1, T2 with R5)

**Decompilation:** wave O3, **57 fns / 7,008 B**: Amy's balloon O_BALOON 8C059E20 (whose clear goes through the stage
clear decompiled in S2d), air bubbles 8C0686C0 / 8C068750, and the invisible collision volumes 8C02DE20-8C02E820 (for
builders). No decompilation for T1 / T2: every path function is C.

**LIB, `SA1_API_PATH`**

* `sa1_path_set` / `sa1_character_ride` / ride events;
* the six ride modes, greyed per character: order 2 is not handled by Knuckles, Amy and Big; order 29 not by E-102, Amy
  and Big; Tikal has only Steer, Ride and Drive; Super Sonic UNVERIFIED;
* the Drive re-aim;
* the extension-segment pool allocator and deferred frees (D14).

**Harness:** path references (orders 1 / 2 / 26 / 29 / 31, the event path, lock 3, for each character), replayed
strictly in LIB.

**G-SA1, feature `triggers`**

* T1a: the `gsa1_trigger` family on the base of 0.9.4, with shapes box / sphere / gate (swept tests, `pos + 4` up).
* Actions:
  * Board On / Off;
  * Event: text, sound / jingle / music from the disc, rings, lives, checkpoint, teleport, map input, enable / disable,
    chaining;
  * Kill;
  * Spline;
  * "View through a camera": a placed Camera-tool camera through `SetViewEntity`, which is GMod's own camera, not a
    stand-in for SA1's (`v1_triggers` R3.9).
* The Trigger tool.
* T2b: `gsa1_spline_node`; the Spline tool with ids, insert / select / move / delete, close loop, reverse, Test ride;
  dupe-paste remapping of spline ids and trigger names.
* The page Options > G-SA1 > Triggers.
* Event text until 1.4: drawn by the HUD layer in a plain font; the game's window comes with 1.4.

**Verification:** path references strict; LuaJIT tests (shapes, swept gates, enter / leave edges, chaining, the dupe
remap twice, the sampler's lengths and up-vector round trip); the owner checklist.

**Owner gets:**

* volumes that switch to placed cameras;
* point-by-point splines under separate ids that never mix;
* characters riding a spline the game's ways (steer, rail run, roll, hang, shimmy, cutscene ride, drive);
* event triggers;
* Amy's goal balloon;
* breathable air bubbles under water.

### 6.3 1.2: the game's cinematic cameras, loops, rockets (`v1_triggers` T3, T4 with R2-R5; `v1_objects` 9.2)

**Decompilation: 204 fns / 44,384 B.** The T3a and T4 counts reproduce the triggers review exactly.

| Wave | Content | fns / B |
|---|---|---|
| T3a | the camera subset of the triggers review: 90 functions of the closure plus 8C01E92A and 8C0213E0, the six camera functions the characters reach (8C021700, 8C0246EA, 8C0239C2, 8C0EAD6A, 8C01A7A4, 8C019368). 8C04FE4A came with S1; 8C0522C0 is already NM | 98 / 27,704 |
| T4 | path-work objects 8C06AF00-8C06C0C0 + 8C01B35E / 8C01B36C | 17 / 4,464 |
| O2 | rockets H / HS / V / VS and the helper TU 8C066320-8C067480; dash ring CMN_DRING; line rings O_LINERING / _V | 89 / 12,216 |

**LIB, `SA1_API_CAMERA`**

* T3b: 7 clean-room Ninja functions. Of the 9 the triggers doc lists, 8C63EBC8 arrives with the HUD round, and
  8C60957E is the camera-motion player (out of scope until 2.0).
* T3c: the game camera in the viewer's world only.
  * The CAM table is installed by patching `8C1958B4[level][act]` before 8C01957E; the HOST row by patching
    `8C1958A8`.
  * Stable entry indices; at most 255 entries.
  * `sa1g_camera_refresh` / `sa1g_camera_view` are off under `SA1_CAMERA_GAME`; L / R and the free-camera masks are
    gated by `FREEKEYS`.
  * `sa1_character_camera` with interpolation.
* T4: loops, hang bars and path cameras in worlds.

**Harness:**

* synthetic CAM references (FIXED, LINE, ASHLAND_I, COLLI walls, SURVEY) are mandatory;
* a death-zone and a Light Speed Attack reference with the game camera on;
* "camera native" replay against the 24 LIB-replayable references (10 strict + 14 HOSTOBJ).

**G-SA1**

* Camera trigger presets: Fixed, Watch, Pivot, Point, Area follow, Path, Camera wall, Advanced (all 76 modes).
* `CalcView` keeps the per-frame chase camera for NORMAL / HOST and uses the game camera only for trigger / event
  cameras and blends (R3.5).
* The stick-hold option.
* The spline "Loop" option.
* Rockets, the dash ring and ring lines in the tab.
* The capsule's clear camera becomes real.

**Verification:** the references above; the camera state bit-exact against `px_cam_raw`; the owner checklist.

**Owner gets:**

* the Dreamcast's own camera work in GMod: fixed and watching shots, pivots, smooth game blends in and out, invisible
  camera walls;
* loops that work like the game's;
* rockets you ride;
* Tails' / Knuckles' dash rings;
* switch-armed ring lines.

**Coupling:** objects range out against the real camera from now on, as in the game (a far fixed camera can despawn
objects near the character: faithful, owner-visible).

### 6.4 1.3: badniks (`v1_enemies` with R.4; `v1_arch` S4)

**Decompilation:** wave E1, **303 fns / 64,582 B** (13-16 batches): the 12 execs (58,424 B) plus the enemy library, the
effects, Kiki's draw helper and the projectiles. The emerald-shard path (EMERALD) stays out. Order:

1. Rhinotank (the `obj_ec1_kill` oracle exists);
2. Kiki (253 placements);
3. Spinner A / B / C;
4. Unidus A / B / C;
5. Leon, Egg Keeper, Boa-Boa, Sweep.

**LIB**

* Actors: `sa1_set_own`, the pose stream, the owner world. A hand-off respawns at the record's own position, not at the
  streamed pose (`v1_enemies` R.3.5).
* `SA1_HOSTOBJ_ATTACKER` (7) with attr 0x200, no bounce (R.3.3).
* **Host-object shape extensions:** per-shape attack / defence levels, capsules, live on / off, CCL id 2 (Spinners,
  Boa-Boa), or a "mirror this CCL table" proxy kind (R.3.1). The current descriptor has one level pair and 4 shapes.
* Per-world texture loading by texlist (E_*.PRS, the animals' archives).
* Animals per virtual stage.
* The bomb / invincibility relay through ATTACKER.
* A per-world actor budget (a Rhinotank costs about 29K SH-4 instructions per step, `v1_arch` R.4).
* Bank 4 is already loaded (R.2.9).

**Harness:** one reference per badnik in its home stage via `--set` with the `.set.bin` sidecar (badnik taskwk pos /
flag / mode, R.3.6): Kiki EC1, Spinners SH / SD, Leon / Boa-Boa WV, Egg Keeper FE, Unidus SH / RM, Sweep EC2.
`set_ec1_common` with 289 records.

**G-SA1**

* the Badniks category: Kiki, Rhinotank, Boa-Boa, Sweep, Leon, Egg Keeper, Unidus, Fire Unidus, Spinner, Patrol Spinner,
  Electric Spinner;
* properties: Kiki's projectile, ranges in HU, the Spinner patrol-box gizmo, "no collision";
* touch damage to plain players and NPCs (option, off);
* score and animals (toggles);
* the weapon-damage threshold;
* remote kills score +10 in both worlds: the HUD shows one, G-SA1 adds nothing (R.2.6).

**Verification:** per-badnik references strict (player and badnik); `set_test` case 8 (ATTACKER kill); actor hand-off
tests; with the MP round, a two-client test.

**Owner gets:** the 11 1ST_READ badniks behaving exactly as in SA1 (chase, attack, projectiles, explode, release an
animal, +10 score), killable by every character's attacks and by GMod weapons.

### 6.5 1.4: NPCs with your own text (`v1_npcs` with R4)

**Decompilation**

* N0, split fixes (S): symbols for 8C05F1C4 and 8C05F240 and the correct 8C05F196 extent; 8C0550FC; ADV00 0C90FFD2,
  0C910034, 0C910060.
* N1, **115 fns / 10,358 B**: the MSG engine, the talk / hint API and runners, the talk manager, the thread lists, the
  voice stop 8C107BB0 / 8C107BC2, and about 1.2 KB of hint-orb functions the talk API reaches.
* The first ADV image: an ADV00 code map and lowering like STG01, and the people code, **44 fns / 5,604 B** (+142 B
  unindexed).

In all, about 15.5-16 KB (R2.2 counts 15.5 KB of covered bytes).

**LIB**

* W4, the coroutine shim (effort L): fibers replace 8C099A50 / 8C099B2C / 8C099C0A; the stack window is rebased on every
  switch; recovery happens outside fibers; 8C099A00 is in the boot (R3.3). It is NPC-only (R2.6).
* W6, `sa1_text_*` in a service world: a generated Unicode-to-SJIS table; pagination capped at 2 lines and 500 px, with
  3-line game pages tested.
* W7, NPC worlds:
  * ADV00 composed; no talk-manager pick in NPC worlds;
  * the viewer stand-in specified (R3.5);
  * deferred voice semantics (R3.2) and rt bodies for 8C608914 / 8C608922;
  * the talk-table builder.
* W8, TALKABLE with per-type talk info.
* W9, POLY2D texture identity and memory textures (extends D16).

**Harness:** W11 (a) text buffers in 5 languages including the 3-line pages, (b) a talk session, (c) O PEOPLE motions.

**G-SA1, feature `npc`**

* 26 Station Square types in the tab;
* the NPC text tool: editor, preview in the game's font, import of a game line by reference;
* the window in the HUD layer, with a fallback window for clients without the disc;
* talking: characters with B / X / Y by the game's rules, plain players with E;
* voices from the disc;
* Options > G-SA1 > NPCs.

**Verification:** the W11 references; shim tests (nested threads, faults in a fiber, x86 / x64); encode / paginate over
10,000 sentences; the in-game checklist (front / side / back, radius edge, stairs, 21:9, a client without the disc).

**Owner gets:** Station Square's townspeople, placeable anywhere, saying your own lines in the game's message window and
font, with the game's fades and button waits.

### 6.6 1.5: bosses without an arena (`v1_enemies` tier R with R.2.7-8)

**Decompilation**

* R, **17 fns / 3,540 B**: the rival dispatcher and AI asm (8C083818, 8C0839EC, 8C083A08, 8C083A1E, 8C083AA0 / A6), the
  fight rings 8C083AAC with 8C02D980, and the request / end path (8C09072C, 8C090800, 8C0909A4, 8C095742, 8C0956BE).
  Host code may replace the request / end path.
* Z, **25 fns / 5,398 B**: E-100 Zero's remaining asm (the beam catch 8C105920, the draws, the event variants).

**LIB:** F2 same-world player 1 becomes a supported mode (from 0.9.5); the fight position is supplied by the host (data,
default: at the spawn point); Zero is created through 8C107744 in Amy worlds only (the game's guard; question 10.2-E6).

**G-SA1:** the Bosses category: rival fights (Knuckles / Sonic / E-102 AI against the player; the six pairings, the rest
marked "unofficial"), E-100 Zero.

**Verification:** a rival-fight harness reference per AI kind in a recreated adventure-field position; the Zero
references.

**Owner gets:** fights against the game's own AI Knuckles, Sonic and E-102 anywhere; Zero hunting Amy.

### 6.7 1.6-1.7: arena bosses (`v1_enemies` tier A with R.3.2)

**Infrastructure round (1.6)**

* Arena worlds: the B_* image at 0x0C900000, the level and sky tasks (`D_8C19DE30` / `D_8C19E0E0`).
* DISC collision raised from levels 0-11 to 15-25, with a level -> B_* map and the landtable from `D_8C19E9FC` (R.2.10).
* Per-world collision (or an own context) and the arena's landtable as render output.
* Per-world audio banks (R.3.9).
* Boss HP from `B_8C7A50E8` / `B_8C7A50F4` (UNVERIFIED meaning) for the HUD gauge.
* The GOAL return to the map.
* G-SA1's "arena pocket" (question B3): an anchor in the map, no map collision inside, a boss camera.

**Decompilation**

* Shared 1ST_READ, **88 fns / 19,252 B**: the Chaos code, the boss title 8C0F1C40 / gauge 8C0F2228, the motion weld
  8C018120-8C019360.
* Per boss: the overlay plus its share of 1ST_READ (R.3.2):

| Order | Boss | Overlay (B) |
|---|---|---|
| 1 | E-101 Beta | 9,054 (about 14.8 KB with 1ST_READ) |
| 2 | Egg Hornet | 16,016 |
| 3 | Chaos 0 | 19,126 |
| 4 | Chaos 2 | 24,648 |
| 5 | E-101 mkII | 25,188 |
| 6 | Chaos 4 | 25,422 |
| 7 | Zero | 28,946 |
| 8 | Egg Walker | 27,892 |
| 9 | Chaos 6 | 27,112 |
| 10 | Egg Viper | 39,348 (fix the harness fault at frame 1 first) |

* All 11 arena bosses (Perfect Chaos included) are about 303 KB with their 1ST_READ shares, about 1,450 functions
  (60-70 batches): the largest decompilation of the series.

**Verification:** the boss-boot references (`v1_enemies` 3.1) extended with scripted fights; one boss reference per
release.

**Owner gets:** the game's bosses fought in their own arenas, appearing at a chosen spot in the map, one fighter at a
time, others watching.

### 6.8 1.8: stage packs, more objects, NPC phase 2

**Stage packs (question B1 default):** one STGnn per map session, composed into every local world except kind 8 (D21).

| Pack | Content | Bytes (asm) |
|---|---|---|
| Windy Valley | trampoline O POLINE (STG02 0x0C9098E6, model 0x0C9C8C98) | 8 fns / 1,588 |
| | E-103 Delta | 10,448 |
| | updrafts | |
| Twinkle Park | Buyon | 3,828 |
| Speed Highway | Cop Speeder (+ a path from a G-SA1 spline) | 11,102 |
| Sky Deck | aircraft; cannons | 5,996 + cannons |
| Ice Cap | Ice Ball | 8,468 |
| Final Egg | Beat + gachapon | about 8.1 KB + COLLI GACHA 1,122 |
| Red Mountain | E-104 Epsilon | 10,520 |
| Casinopolis | bumpers / flippers | |

Each pack needs its overlay's object cluster (STG02 is 0 % C; overlays are 0-9 % C).

**Objects:**

* O4, **81 fns / 14,558 B**: iron balls, upgrades (O_LevelItem: it reaches 8C051760, a fatal stub today), emblems
  (counted per session), grass, dynamite;
* O5, **21 fns / 5,386 B**: Knuckles' emerald shards with the hunt code and the radar (D20).

**NPC phase 2:**

* ADV02 / ADV03 people, 4,834 / 5,610 B (re-split ADV03 first);
* walkers as actors, which reconciles `v1_npcs` R3.11 with `v1_arch`;
* the yes / no choice window 8C055460.

### 6.9 1.9: Perfect Chaos

* After 0.9.3 and 1.2 (it needs `CAM1900S`).
* B_CHAOS7 has 166 fns / 27,764 B left, plus about 10.6 KB of 1ST_READ.
* The arena's solid collision spans 21,600 x 39,248 HU (vertex bounds, `v1_enemies` R.1), beyond Source's 32,768.
  It needs a reduced scale or a render-only placement.
* Tikal and the path work are needed too.

---------------------------------------------------------------------------------------------------------------------

## 7. 2.0 sketch: the machinima creator (`v1_machinima` with R.4)

| Phase | Goal | Decompilation | LIB | G-SA1 | Verification gate |
|---|---|---|---|---|---|
| M1 faces | EVENT faces, expressions, hotkeys, look-at, talk from audio | none (face code, event work, look setters all C; optional wrappers 36 B) | `sa1_character_face` / `sa1_face_info_get` / `sa1_character_look`; validation (Big 8 keys, letters A-Z / a-e / 0); the interrupt semantics of R.3 G1 (journaled); face state in `sa1_state_ex` | Faces page: Expressive / Game / Off, presets, 10 hotkey slots, look at camera / entity; pre-warm only the unlit EV-head states (R.2-6) | a face-script harness reference replayed strictly |
| M0 journal | record and replay one actor exactly; ghost fallback | none | the journal at the latch (D13: every 1.x input is already queued there); collision and logic fingerprints widened to rt, `sa1_coladapt_world.c`, `sa1_world_hostobj.c`, stage stand-ins and image SHA-1s (G3); the collision-ready rule (G5) | minimal record / play, save / load | J1-J4, J6 |
| M2 event motions | any of the 404 cutscene motions on a live actor | wrappers 144 B (optional) | **pacing first** (G2: EC4 = 1 for a performing world, or speed scaling); the EV overlay loader and a per-world motion cache on the pool allocator (D14) | catalogue browser, "perform" | EV0003 oracle with EC4 = 1, plus an Amy or E-102 event (ungated displays) |
| M3 Director | timeline, takes, layered actors, camera keys, project file v1 | none | snapshots optional | Director window (2 rounds) | file round trip, fuzzing |
| M4 audio | game voices, music, SE, event SE; own MP3 / WAV / OGG / ADX | none | event banks, memory streams | timeline audio from the jukebox engine, with flush and sample-accurate start (G9) | 51 event banks decode; drift over 10 min |
| M5 subtitles | subtitle lane, game lines by reference | none | none | the HUD layer or 1.4's message window | lines never stored in files (validator test) |
| M6 choreography, puppets | keyed walk / glide / animate / face; Eggman and Tikal actors | Eggman 4,468 B + 8C04F8FA 576 B and the event wrappers: **21 fns / 5,274 B**; Tikal needs her create path | ticking Eggman / Tikal | key editor | strict replays of choreographed takes |
| M7 polish | seek snapshots, prop restore, render mode, sharing | none | world snapshot / restore | `MOVR`, `PACK` | seek < 0.5 s after 10 min |

**Effort:** about 10-12 rounds (review R.4-11).

**Prerequisites from 1.x:**

* the journal-ready inputs (D13);
* the SET stream (1.0) and host objects (round 5) as journal entries;
* the game camera (1.2) for event cameras;
* splines and rides (1.1) for camera paths and walk-to;
* the HUD layer (0.9.2) and the message window (1.4) for subtitles;
* the jukebox engine (0.7.0) for audio;
* faces in multiplayer through the `sh_net.lua` stream after the MP round (R.3 G7).

**Note:** M1 needs nothing from 1.x and could ship earlier as a preview (question 10.2-M0).

---------------------------------------------------------------------------------------------------------------------

## 8. Decompilation budget (one table; `closure.py`, de-duplicated in roadmap order)

Each wave counts only code no earlier wave claimed. Sizes are clipped 1ST_READ bytes (pools included; the reviews'
Ghidra-boundary instruction counts are 10-15 % lower). Function pointers in data tables are added as roots by hand (the
item-box contents, `v1_arch` R.2-7).

| Release | Wave | Functions | Bytes | Batches | Notes |
|---|---|---|---|---|---|
| 0.9.2 | H Big's HUD + 8C08EE98 | 6 | 1,964 | 1 (inside the HUD round) | |
| 1.0 | S1 SET manager, 8C058E48 + resets, popup TU | 44 | 4,424 | 2 | split 8C090FA0's xrefs entry first |
| 1.0 | S2a item boxes, contents, shields, bomb | 33 | 4,024 | 1-2 | the 11 shield callees are fatal stubs today |
| 1.0 | S2b springs, wide spring, dash panel, jump panels | 24 | 5,168 | 1 | |
| 1.0 | S2c checkpoint, switch, spikes, crate SW | 42 | 5,514 | 2 | |
| 1.0 | S2d capsule + stage clear | 15 | 1,998 | 1 | camera hooks stay stubs |
| **1.0 total** | | **158** | **21,128** | **about 8** | the reviews' figures: 155 fns / about 17.7 KB of instructions (objects) + SET-0 |
| 1.1 | O3 balloon, bubbles, collision volumes | 57 | 7,008 | 2-3 | |
| 1.2 | T3a camera subset | 98 | 27,704 | 4-5 | = triggers review R4 minus 8C04FE4A (S1) |
| 1.2 | T4 path-work objects | 17 | 4,464 | 1 | |
| 1.2 | O2 rockets, dash ring, line rings | 89 | 12,216 | 4 | |
| 1.3 | E1 badniks | 303 | 64,582 | 13-16 | `v1_enemies` R.1: 64,976 B incl. 8C0919E8 / 8C09111A (S1 here) |
| 1.4 | N1 MSG / talk / threads / voice stop | 115 | 10,358 | 5 | + ADV00 people 44 fns / 5,604 B (overlay) |
| 1.5 | R rival fights | 17 | 3,540 | 1 | |
| 1.5 | Z E-100 Zero | 25 | 5,398 | 1 | |
| 1.6+ | B Chaos code, title / gauge, motion weld | 88 | 19,252 | 4 | + about 243 KB of ten boss overlays (about 1,200 functions); Perfect Chaos's 27.8 KB is 1.9 |
| 1.8 | O4 iron balls, upgrades, emblem, grass, dynamite | 81 | 14,558 | 4 | |
| 1.8 | O5 shards, hunt, radar | 21 | 5,386 | 1 | |
| 1.8 | stage packs | - | - | - | about 61 KB of overlay code for the listed pack objects (6.8; bumpers, cannons and updrafts unmeasured) + ADV02 / ADV03 people 10.4 KB |
| 2.0 | M Eggman, 8C04F8FA, event wrappers | 21 | 5,274 | 1 | |
| **All 1ST_READ above** | | **1,096** | **202,832** | | 49 % of the 414,574 B of 1ST_READ game code still in asm |

---------------------------------------------------------------------------------------------------------------------

## 9. Verification plan (oracles to record, by release)

| Release | New oracle / references | Replayed by |
|---|---|---|
| 0.9.1 | bird reference; W1 / E1 / G1 / J1 tests | LIB HOSTOBJ list; g-sa1 tests |
| 0.9.2 | `hud_draws` (2D draw arguments of the HUD functions in many states) | harness + `hud_test` |
| 0.9.3-0.9.5 | SUPER, BOARD (`board_sh_*`), FOLLOW references (HX made most of them) | LIB lists |
| 1.0 | `--set` + `.set.bin`; 25-30 object references with real conditions; a death / restart trace (the 8C091890 rule); `set_ec1_common` (270) | LIB SET list, STAGE and LIBSA1 list modes |
| 1.1 | path / ride references per character | LIB PATH list |
| 1.2 | synthetic CAM references; death-zone and LSA with the game camera; camera native on 24 references | LIB CAM list |
| 1.3 | per-badnik `--set` references with badnik state; `set_ec1_common` (289) | LIB SET list |
| 1.4 | text buffers (5 languages, 3-line pages), talk session, O PEOPLE motions | LIB NPC list |
| 1.5 | rival fights per AI kind; Zero | LIB lists (F2) |
| 1.6+ | boss boots + scripted fights per boss; the Egg Viper harness fault fixed first | per-boss lists |
| 2.0 | face script; EV0003 with EC4 = 1 plus an Amy / E-102 event; journal tests J1-J6 | LIB, journal suite |

Every release keeps the 10 strict references and all earlier lists green, x86 = x64.

---------------------------------------------------------------------------------------------------------------------

## 10. Owner questions

### 10.1 Questions that block a design choice (the plan proceeds on the recommended default)

**B1. Trampolines and other stage gimmicks.**

The trampoline is not common code. It is Windy Valley's own object (`O POLINE`, STG02), and so are bumpers, cannons and
updrafts. A game world holds one stage's code at a time.

Options:

* (a) **Stage packs.** Pick one stage per map (for example "Windy Valley"). Its gimmicks then become placeable:
  trampoline, updrafts, E-103. Super Sonic's world cannot hold a pack; he sees those objects as statues.
* (b) Additionally research a way to run single stage objects anywhere by moving their data. It is more work, and it is
  unproven.
* (c) A G-SA1 "trampoline" made from the air spring's code with its own look. That is not the game's code and needs your
  OK.

**Recommended default:** (a), with (b) as a research spike in 1.x.

**Blocks:** the trampoline, every stage-pack item (1.8), the overlay-slot rule (D21).

**B2. Multiplayer in 1.x.**

Today other players cannot see your character at all (there are no puppets), so PvP, shared rings and shared badniks
cannot be seen or tested with a friend.

Options:

* (a) Single player first, with a multiplayer round right after 1.0. Objects already go through the server, so they work
  in multiplayer once puppets exist.
* (b) Multiplayer before 1.0.
* (c) Multiplayer not needed.

**Recommended default:** (a).

**Blocks:** the extra round (6.1), the badnik hand-off between players (1.3), "show NPC text to others" (1.4), and the
two-client tests (they need a second GMod client).

**B3. Bosses in an "arena pocket".**

Most bosses need their own arena (the Chaos 0 plaza with its lamp posts, Chaos 4's pond, the Egg Carrier deck), so they
cannot fight on an arbitrary GMod map.

Options:

* (a) **Arena pocket.** The boss's own arena appears at a spot you choose in the map, and the fight happens inside it
  (one fighter at a time; others watch).
* (b) Only bosses that work on any map: the rival fights, Zero, and E-103 / E-104 with their stage pack.

**Recommended default:** (a).

**Blocks:** the arena-world infrastructure (1.6) and every arena boss.

### 10.2 Questions with a sensible default (each becomes a Q-menu option where that makes sense)

| Id | Topic | Question | Default |
|---|---|---|---|
| S0 | schedule | Finish Super Sonic, the board and followers before 1.0 (their code is already decompiled), or move 1.0 ahead of followers? | keep the queue first; 1.0's decompilation runs in parallel |
| S1 | schedule | Tikal cannot be played yet (her create path is missing). When should she come? | with 1.4 or 1.5 (she is needed for 2.0's actors and Perfect Chaos) |
| O1 | objects | SA1 limits some objects to some characters (dash rings: Tails / Knuckles; Amy's balloon; jump panels not E-102 / Big; checkpoints player 0). Add an "all characters" override that is not game code? | game rules; override off, labelled "not in SA1" |
| O2 | objects | What happens at a goal? | the game's clear pose, voice and jingle, a "Clear" banner; control comes back after the pose; no results screen |
| O3 | objects | Do collected rings / broken boxes come back? | never by default; per-object "respawn after (s)"; a server default; "Reset G-SA1 objects"; optional "SA1 restart rule" (rings, groups and enemies return when you restart, boxes stay broken) |
| O4 | objects | Multiplayer: is a ring shared (first taker) or per player? Two players touching it at the same moment may both get it | shared; no rollback |
| O5 | objects | On death, respawn at the last checkpoint with 0 rings (the game), or GMod's spawn? | the game's rule |
| O6 | objects | Should plain GMod players (no character) be launched by springs / dash panels (a GMod imitation) and collect rings? | off; one switch for the launchers |
| O7 | objects | Should a save remember what was collected? | no (a save is a layout) |
| O8 | objects | Spawn-menu icons: drawn from your disc each session and kept in memory, or cached as PNGs in `data/gsa1/icons`? | memory only |
| O9 | objects | SA1 has per-character layouts. Should a placed object exist for every character? | yes, with a "Characters..." filter |
| O10 | objects | 1.0 scope: rockets, dash rings and ring lines in 1.0 or after the camera (1.2)? | 1.2 |
| O11 | objects | Jump panels: the game's limit of 10 per chain and world? | accept; the tool warns |
| O12 | objects | Emblems: count per session, or save them? | per session |
| E1 | enemies | Badniks against plain GMod players / NPCs: touch damage only (off by default), or also hunting (a 16 MB world per hunted entity)? | touch damage, off; hunting later |
| E2 | enemies | Killed badniks stay gone (SA1) or respawn? | gone; per-entity respawn time |
| E3 | enemies | Score (+10 with the chain) and animals as in the game? | yes, both toggleable |
| E4 | enemies | How much GMod weapon damage is one SA1 hit? | a badnik dies at 10+; a boss takes one hit per 50, with the game's invulnerability |
| E5 | enemies | Rival fights: any character against any AI rival? Where does a fight take place on a GMod map? | yes, marked "unofficial"; at the spot you spawn it |
| E6 | enemies | E-100 Zero only chases Amy in the game (its code refuses other characters). Bypass it? | Amy only |
| E7 | enemies | Perfect Chaos last, at a reduced arena scale? | yes |
| E8 | enemies | One fighter per boss in 1.x? | yes; co-op later |
| T1 | triggers | Between trigger cameras: G-SA1's chase camera with mouse orbit, or the game's follow camera? | chase camera |
| T2 | triggers | Looking around inside a trigger camera | as the game (the camera keys take over and blend back; locked cameras refuse) |
| T3 | triggers | The stick across camera cuts | keep the old direction while held |
| T4 | triggers | Who a trigger affects | the entering player; plain players use Event triggers, not camera triggers |
| T5 | triggers | Default scripted ride | Ride (cutscene-exact, ground snap option) |
| T6 | triggers | Spline shape | smooth curves |
| T7 | triggers | Show triggers and splines | only while editing |
| T8 | triggers | Per-map trigger files in `data/gsa1/maps` | off |
| T9 | triggers | Before 1.2: camera switching | GMod's own Camera-tool cameras through a trigger action; no stand-in for SA1's camera |
| N1 | NPCs | Who sees an NPC's text? | only the talker; an option for everyone nearby |
| N2 | NPCs | Plain players talk with E; frozen while talking? | not frozen |
| N3 | NPCs | A "Talk" prompt (the game has none)? | none |
| N4 | NPCs | Voices | only the game's clips for 1.4; your own files with 2.0's audio |
| N5 | NPCs | Accented text uses the European font automatically? | yes |
| N6 | NPCs | Early NPCs with a placeholder body? | no, wait for the townspeople |
| N7 | NPCs | Cast members (Sonic and others) as NPCs | with 2.0's cutscene motions |
| N8 | NPCs | Spin attacks on townspeople | they only flinch (game) |
| N9 | NPCs | Imported game lines are stored as references, so a friend's G-SA1 reads them from their own disc | yes |
| M0 | machinima | Faces could ship right after 1.0 as a preview (no decompilation needed) | keep your order (2.0), unless you ask |
| M1 | machinima | Expressive EVENT faces during normal play | on, per character |
| M2 | machinima | Expression hotkeys | none bound; 10 slots; controller combo off |
| M3 | machinima | A hotkey plays once or holds | play once for talk / reactions, hold for moods |
| M4 | machinima | Recording | one character at a time, layered takes |
| M5 | machinima | Move GMod props back on playback | off at first |
| M6 | machinima | Your own audio in the shared file | referenced, not packed |
| M7 | machinima | Subtitle look | the game's window once 1.4 exists |
| M8 | machinima | Eggman / Tikal actors | later (M6) |
| M9 | machinima | Talking faces for your own clips | from loudness |
| M10 | machinima | Japanese voices / subtitles | English, Japanese selectable |
| M11 | machinima | A shared replay plays exactly only with the same G-SA1 and map versions; otherwise from stored positions ("ghost") | accept |

The HUD round's own questions (H1-H5), the board's (1-5) and the follower / Super Sonic defaults of rounds plan 14.2
stand as written there, except H5 (the radar moves to 1.8, D20).

---------------------------------------------------------------------------------------------------------------------

## 11. Risks and UNVERIFIED points

* **Decompilation volume.** The roadmap's 1ST_READ items are about 203 KB, half of what is left; the boss overlays add
  about 270 KB. NON_MATCHING C must be proven against references made with real conditions (D10). Badniks and bosses
  dominate (65 KB and about 300 KB).
* **The LIB lane is serial.** Seven rounds patch the same world files before 1.0. A failing verifier there delays
  everything behind it. Swap candidates are named in 3.2.
* **Visibility couplings.**
  * Objects, badniks and the game camera all depend on the world's view (8C038D00, 8C0919E8, 8C048160). The host view
    (D6) changes what the worlds cull.
  * Whether a host view changes any character reference record is UNVERIFIED: replay before switching (`v1_arch` 5.3).
* **Capacities.** 1,023 conditions per world, CCL lists (id 7: 256), the 589,824-B game heap, 10 jump panels, one capsule
  and 32 switch ids per world: the sandbox inherits the game's limits.
* **Multiplayer is unbuilt.** Without the MP round, shared objects and badnik actors are proven in single player only
  (question B2).
* **The extension segment never frees today.** D14 must land before 1.1's rides, or long sessions leak (36 KB per
  1,000-point spline and ride).
* **Overlay exclusivity.**
  * Stage packs, arena worlds and Super Sonic all want 0x0C900000 (D21; UNVERIFIED that kind 0 never reaches B_CHAOS7
    code without equipment 0x8000).
  * Audio banks are per client, not per world (`v1_enemies` R.3.9).
* **Event pacing (2.0)** is unresolved between EC4 = 1 and scaling (D23).
* **Owner rules.**
  * Host imitations (puppet ring turn, launchers for plain players, respawn timers, rival positions) are labelled as
    options.
  * Nothing from the disc is written into saves, dupes, icons on disk or project files (D18).
* **Tikal** cannot tick yet. The drowning, the ride table and 2.0 actors list her (question S1).
* UNVERIFIED carried from the documents:
  * the native CPU cost of objects;
  * the collision group that makes rings pickable but walk-through;
  * GMod's edict limit on x64;
  * AdvDupe2 and NetworkVars;
  * the boss HP addresses;
  * Sweep on dry ground;
  * the Cop Speeder's path source;
  * E-103 / E-104 independence from their stages;
  * whether the owner can run a second GMod client for the multiplayer tests.

---------------------------------------------------------------------------------------------------------------------

## 12. Owner-facing summary (plain words)

**Next (0.9.x): finishing what you already asked for.**

* First, the fixes from your 0.7 test:
  * drowning really drowns, and you lose a life;
  * the jukebox steps aside for the countdown;
  * Amy's bird, with a switch;
  * Tails' ball and the see-through afterimages fixed.
* Then the game's own HUD.
* Then Super Sonic (as a character and as a transformation).
* Then the board, with Board On / Off boxes.
* Then followers.

Meanwhile, in the background, the game code for 1.0 is already being translated.

**1.0: game objects.** A new "G-SA1" tab in the Q menu, holding the game's real objects, running the game's own code:

* rings, ring lines and circles;
* item boxes with all 9 contents (speed shoes, invincibility, rings, shields, 1-ups, the bomb);
* springs, dash panels, numbered jump panels;
* checkpoints, switches, spikes, crates;
* the Egg capsule goal.

You can edit each one's settings with the game's own names. Undo, saves and dupes work.

**After 1.0:**

* 1.1: triggers and splines you draw point by point, characters riding them, events.
* 1.2: the game's real cinematic cameras, plus loops and rockets.
* 1.3: badniks (Kiki, Rhinotank, Spinners, Unidus and the rest).
* 1.4: townspeople that say your own lines in the game's window.
* 1.5 and on: bosses. First the rival fights and Zero. Then the arena bosses, which appear in their own arena at a spot
  you pick. Stage packs (the trampoline lives in Windy Valley's), and Perfect Chaos last.

**2.0: the machinima creator.** Faces first (the game's expressive cutscene faces, expression hotkeys), then recording
and replay, cutscene animations, sound and music tracks (the game's and your own files), subtitles and keyframes, all
saved in one file that a friend who owns the game can open.

**Three questions for you** (the plan goes ahead with the recommendation if you don't answer):

1. **Trampolines live in Windy Valley's code.** OK to get them with a "Windy Valley pack" you pick per map? [yes]
2. **Do you play with friends on one server?** If yes, a round that lets others see your character comes right after
   1.0. [single player first, multiplayer right after 1.0]
3. **Bosses in their own arena,** appearing at a spot in your map? [yes]

---------------------------------------------------------------------------------------------------------------------

## Appendix A. Reproducing the measurements (`SADC/build/research/roadmap/`)

| File | What |
|---|---|
| `rstat.py` -> `rstat_out.txt`, `rstat_out.json` | per-range decomp status of every range in section 1.2 (the reviews' nested-#if-aware NON_MATCHING rules; clipped sizes) |
| `closure.py` -> `closure_out.txt` (`-v`: per function), `closure_out.json` | the de-duplicated closures of section 8. It walks callees and code-pool constants from each wave's roots; walks through the stub-policy functions a wave must replace (FORCE); stops at LIB-lowered functions, other stubs and the BOUNDARY list; adds the Ghidra-only functions of the reviews with their sizes. The camera wave reads `build/research/triggersreview/union.txt` |

```
cd SADC
py -3.12 build/research/roadmap/rstat.py
py -3.12 build/research/roadmap/closure.py -v
ls disc/SONICADV/SET*.BIN | wc -l     # and the other counts of 1.3 (cat ... | wc -c)
```

The closures depend on today's `LIB/build/pclower/functions.json` and `LIB/gen/policy/stubs.tsv`. Re-run them when a
landing changes either.
