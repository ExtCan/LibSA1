# G-SA1 1.0: the architecture of game objects (v1_arch)

Design, 2026-10-21. Read-only research (workflow `gsa1-design-v1`, topic `arch`); nothing here is implemented. Companion
docs of the same workflow: `v1_objects.md` (the object catalogue), `v1_enemies.md` (badniks, bosses), `v1_npcs.md`,
`v1_triggers.md` (cameras, splines, trigger volumes), `v1_machinima.md`, `v1_roadmap.md`. Builds on
`docs/libsa1_interaction.md` (round 5 host objects, read in full; its addendum 6 wins over its sections 2-4),
`docs/libsa1_integration.md` (GUEST-ARENA worlds), `docs/research/gsa1_rounds_plan.md`, `g-sa1/README.md`.

Roots: `SADC` = `<project>\sadc`, `LIB` = `...\libsa1`, `GSA1` = `...\g-sa1`, `GMOD` =
`<GarrysMod>\garrysmod` (read only). Addresses are 1ST_READ US 1.005 unless an overlay is
named. Units: SA1 units (4 HU each in G-SA1's Source preset); "step" = one 60 Hz logic step, "pair" = the two steps of a
30 Hz frame. Scratch and tools: `SADC/build/research/v1_arch/` (`status.py` per-function decomp status with
`tools/progress.py`'s rules, `closure.py` / `union2.py` call-graph closures of todo code, `objlist.py` stage object lists
and SET files, `ramstat.py` task / condition / heap census of RAM dumps, `ec1obj.*` the harness run of 1.2).

---

## 0. Summary

* **SA1 already has the mechanism G-SA1 needs.** Its SET manager (8C090EC0-8C091C20) keeps up to 1,024 object records
  per act in an `OBJ_CONDITION` array (`B_8C79A3E0`, 16 bytes each); every step (task list 7, first in the step) it
  spawns the task of each record that player 0 comes near (400 units by default, a per-type distance otherwise), each
  object destroys itself when it is out of range again (8C0919CC), and the condition remembers across respawns whether
  the object is still there (bit 15: a collected ring, a broken item box, a killed enemy) plus per-object bits
  (`scUserFlag`: which rings of a ring group are taken). Measured on Emerald Coast act 1 (788 records): 42-82 SET objects
  alive at a time, about 0.7 KB of game heap each, +45 % SH-4 instructions per step (1.2).
* **(a) Where the logic runs - decision: the game's own tasks in every character world that sees the object
  ("mirrored SET objects"), GMod's server as the authority over the shared state.** libsa1 gets a *SET layer* that
  writes host records into each world's own condition array and runs the game's own SET manager on it. In each world
  the object meets that world's character as player 0: exactly SA1's single-player situation, so the decompiled object
  code runs unchanged and can be checked against the original game in the harness. The shared part of an object's state
  is the persistent part of its `OBJ_CONDITION` (present / gone, user bits); GMod's server (pure Lua) arbitrates it, the
  clients simulate. Objects that move by themselves (badniks, carriers, walking NPCs) are *actors*: simulated in exactly
  one world (the owner's) and mirrored into the others as round-5 host-object proxies plus a streamed puppet. Bosses and
  stage-overlay objects need *arena worlds* or *stage packs* (another overlay image in the 0x0C900000 slot): later.
  Rejected: one shared "object world" (breaks the player-0 assumptions of the object code; needs an order-forwarding
  layer that is not game code) and host-side logic (owner rule) - 2.1.
* **(b) Interaction never crosses the host.** Rings (CCL id 7), springs (id 4), item boxes, jump panels, enemies (id 3)
  meet the character in the game's own CCL pass, solid objects through DynamicCOL, and the effects are the game's player
  API orders (`8C07C720` spring request = SetInputP + SetVelocityP, ring add `8C08A198`, item grants `8C07D43E` ...),
  all inside the character's world, with zero latency. Only state crosses: the world reports a condition change (a
  ring's bit 15 cleared), the client tells the server, the server sets the entity's state, every other client forces
  that bit into its worlds (the task is freed silently). A ring collected by one character disappears for all within
  one round trip (3.2).
* **(c) Spawn UI**: a top-level **G-SA1 creation tab** built like GMod's own Entities tab (`SpawnmenuContentPanel` + a
  populate hook: `GMOD/gamemodes/sandbox/gamemode/spawnmenu/creationmenu/content/contenttypes/entities.lua`; G64 ships
  the same pattern as its "G64" tab, `GMOD/lua/autorun/client/g64_spawnmenu.lua:501`), with the tree Collectables /
  World objects / Triggers / Badniks / Bosses / NPCs. One scripted entity class per object type, generated from one
  shared catalogue; spawning through `gm_spawnsent` gives GMod's own undo, cleanup, limits and spawn hooks
  (`sandbox/gamemode/commands.lua:777-892`); properties are NetworkVars with `Edit` tables (the Edit Properties window)
  that the duplicator, saves and dupes keep (`GMOD/lua/includes/modules/duplicator.lua:253-254, 323-324`); tools for
  placement, ring lines / circles, links, splines and triggers; icons rendered from the owner's disc at run time (4).
* **(d) Work**: libsa1 - the SET layer API and its world side, the SET manager task in the world, a generated object
  list, render attribution (draws per record, world space, rigid-model instancing), the host camera in the world, an
  ATTACKER host-object kind for remote hits on actors (5). SADC - the SET manager (26 functions to do, about 2.9 KB)
  and the 1.0 object closure (about 29 KB more, an upper bound; rings, ring groups and the air item box already have C)
  (5.5). Harness - `--set FILE` and a `.set.bin` sidecar, so every object type gets an oracle in a real stage (7).
  G-SA1 - feature `objects`: catalogue, entities, tab, tools, server authority, instanced rendering (6).
* **Phasing** (8): S1 SET core with rings (verifiable at once) -> S2 decompilation wave of the common objects -> S3 the
  G-SA1 tab (first owner-facing release: rings, ring lines, item boxes, springs, dash panels, dash rings, jump panels,
  rockets, checkpoints, switches, spikes) -> S4 actors, badniks, multiplayer sync -> S5 arena worlds (bosses) and stage
  packs. Triggers / splines (`v1_triggers.md`) and NPCs (`v1_npcs.md`) plug into the same entity base and tab.

---

## 1. Facts this design rests on (measured)

### 1.1 SA1's SET manager

| What | Where | Notes |
|---|---|---|
| Object list of a stage (OBJ_ITEMTABLE) | `D_8C1C143C[level * 8 + act]` -> {count, entries} | every pointer of levels 0-42 points into an overlay slot (scratch dump: 0x0C9xxxxx / 0x0CAxxxxx / 0x0CBxxxxx); Emerald Coast: `0x0CAA8F1C` in STG01, 84 types |
| SET data of a stage | `D_8C1C0EDC[level * 8 + act]` -> the loaded SET file | read by 8C091016 |
| Current list / SET data | `B_8C79A3CC` / `B_8C79A3D0`, `B_8C79A3D4`, `B_8C79A3D8` (= data + 0x20) | 8C091016 (stage start) |
| Condition array | `B_8C79A3E0`, 1,024 x 0x10 (`sa1_OBJ_CONDITION`, `SADC/include/task.h:150-157`), count `B_8C79A3DC` (Sint16) | 8C0916EE fills one per SET entry (at most 0x3FF + 1): `ssCondition = 0x8000`, `pObjEditEntry` = the entry; 8C0916AE clears |
| Manager task | exec `8C090FA0` (0x76 B; mode 0: 8C091016 init, mode 1: 8C0915A2 every step, mode 2: 8C0915D2 act change), on **task list 7** | measured: the only list-7 task of EC1 (`ramstat.py`, step 60); the executor runs list 7 first ("level 8", interaction 1.8), so objects spawn before anything else runs in the step |
| Spawner | 8C091148 (8C091380 in event mode / with player 1 in mode 2) | for every condition with bit 15 and without bit 0: in range of player 0 (or the camera without one) by the entry's distance, or within 40 units of player 1 (8C0910C0: 1600 = 40^2) -> `CreateElementalTask(entry.im, entry.level, entry.exec)`, `scCount++`, bit 0, `tp->ocp = cond`, taskwk pos / ang / scl from the SET entry (or from the moved block, bit 1), `twp->counter = entry.arg`, `unionStatus.f = distance + 100` |
| Range-out | 8C0919CC (28 B, **81 callers**) -> 8C0919E8 | the object's own exec asks it every step: out of range of the camera, player 0 and player 1 (squared distance `ocp->unionStatus.f`; without ocp 168,100 = 410^2) -> `exec = DestroyTask`, returns 1. Never with `ssCondition & 8` or distance 0 |
| Gone for good | 8C091BA4 (C, **23 callers**: rings, springs, item boxes, balloons, emblems, enemies) | `ssCondition &= ~0x8001`, `exec = DestroyTask`; 8C091B8A (E-102's firing) clears the bits only (`code_8C091B8A.c`) |
| Object bits | 8C091C00 / 8C091C10 (C) | `scUserFlag`: O_GRING's per-ring bits (`code_8C091C10.c`) |
| Act change | 8C09174E, 8C0917E0 (compaction: bit 0x4000, fixes `pTask->ocp`), 8C091890 (reset, keeps bit 0x10) | |

`OBJ_ITEMENTRY` (0x14, from 8C091148): +0 `im` (the task's works: 1 motionwk, 2 taskwk, 4 forcewk, 8 anywk), +1 task
list, +2 flags (1: own squared distance at +4; 2: always alive: distance -100, spawned at once, never ranges out; 4:
spawned at the first pass whatever the distance, by range afterwards), +4 squared distance (default 160,000 = 400^2),
+8 word copied into `twp->counter`, +0xC exec, +0x10 name.

**SET file** (disc `SONICADV/SETllaaC.BIN`, little-endian; `objlist.py`): 0x20-byte header (count at +0), then 0x20-byte
records: +0 type (Uint16 index into the stage's list, bit 15 masked), +2 / +4 / +6 rotation x / y / z (Sint16 BAMS ->
`twp->ang`), +8 position (3 floats), +0x14 three parameters (-> `twp->scl`).

**Condition bits**: 0x8000 present (cleared = gone until the act restarts), 0x0001 task alive (DestroyTask 8C098752
clears it), 0x0002 moved (`unionStatus.ptr` -> a 0x24-byte {ang, pos, scl} block the spawner uses and MFrees), 0x0008
never ranges out, 0x0010 kept by the reset, 0x4000 compaction mark; `scCount` spawn count; `scUserFlag` the object's
bits; `unionStatus.f` the squared range-out distance.

**Decomp status** (`status.py`, `tools/progress.py`'s rules): 8C090EC0-8C091C20 = 31 functions, 5 matched (8C091B5E,
8C091B8A, 8C091BA4, 8C091C00, 8C091C10: 96 B), 26 todo. The xrefs index gives 8C090FA0 1,806 bytes (it spans the TU);
Ghidra measures 0x76, so the manager's todo is about **2.9 KB**, not the 4.6 KB of the raw closure. libsa1 stubs
8C0919CC as `ret 0` (`LIB/gen/policy/stubs.tsv:139`): objects never range out in libsa1 today.

### 1.2 A real stage, measured (harness, objects on)

`sa1h trace --level 1 --act 0 --chr 0 --input @harness/tests/sim/obj_ec1_sonic.txt --dump-ram-at 0,60,200,400,600,end`
(a copy of `harness/build/sa1h.exe` in the scratch directory), and the same script with `--no-objects`:

| | with objects | `--no-objects` |
|---|---|---|
| SH-4 instructions per step (694 steps) | 599,484 | 413,269 |

| RAM dump | tasks | records (bit 15) | live SET tasks (bit 0) | game heap free (of 589,824 B) |
|---|---|---|---|---|
| step 0 (stage start) | 14 | 0 (not filled yet) | 0 | 391,424 |
| step 60 | 108 | 788 (788) | 53 | |
| step 200 | 143 | 788 (785) | 82 | 335,280 |
| step 400 | 119 | 788 (785) | 51 | |
| step 600 | 134 | 788 (785) | 42 | 358,704 (largest block 327,792) |

So the game's own windowing keeps a dense layout (788 records) at 42-82 live objects; three records went "gone"
(collected / killed). The heap cost is about 56 KB for 82 objects and their children (about 0.7 KB each).

**The disc** (156 SET files, 43,091 records; the largest has 1,003 records, `SET1202E.BIN`, just under the 1,024 of
the array). In the 61 SET files of the action stages (levels 1-10, 12; 28,430 records), **10,316 (36 %) are types whose
exec is in 1ST_READ**, 18,114 stage-overlay types. The most frequent 1ST_READ types: RING 3,376; ring groups (O GRING /
O_GRING / O GROUP RING) 907; C CUBE 947, WALL 572, C CYLINDER 543, C SPHERE 246 (invisible collision volumes); O AXPNL
(dash panel) 451; O ITEMBOX 441; O ItemBoxAir 255; SPRINGB 249; E SARU (Kiki) 253; OTTOTTO 199; SPINA A/B/C 233.

### 1.3 Where object code lives

Overlay load slots (`config/overlays/*.yaml`): **0x0C900000** holds 31 images (STG00-STG12, ADV00 / 02 / 03, the B_*
bosses, MINICART, SBOARD, SHOOTING, AL_MAIN; up to 2.36 MB each); 0x0CB80000 211 (events EVxxxx, Chao gardens);
0x0C920000 ADV0100 / ADV0130; 0x0CB00000 TIKAL_PROG. A world holds one image per slot; a Sonic world keeps B_CHAOS7 at
0x0C900000 for Super Sonic (integration 2.5).

The object lists live in the stage overlays, but their execs split. Emerald Coast act 1's list (84 types): types 0-31,
77, 82, 83 are 1ST_READ code, 32-81 STG01 code (palms, the pier, dolphins, seagulls, rocks ...). The 1ST_READ part is the
common object set of every stage (`objlist.py STG01 0CAA8F1C disc/SONICADV/SET0100S.BIN`):

| Type (EC1 list) | Exec | im / list / flags / distance | Exec status | Still to decompile, closure incl. shared helpers (`union2.py`) |
|---|---|---|---|---|
| 0 RING | 8C0869E8 | 2 / 3 / - / 400 | **matched** | 0 |
| 21 O_GRING (ring group) | 8C087B80 | 2 / 3 | **NON_MATCHING C** | 0 |
| 82 O ItemBoxAir | 8C05D9F6 | 15 / 3 | **NON_MATCHING C** | 0 |
| 13 O ITEMBOX | 8C05D10C | 2 / 3 | todo | with rings and ring lines: 4,118 B, 26 functions |
| 1 SPRING, 2 SPRINGB (+ O_SPRING_H 8C067E40) | 8C065800, 8C065EE4 | 2 / 2 | todo | 4,814 B, 22 |
| 3 O AXPNL (dash panel) | 8C059B48 | 3 / 3 | todo | 2,126 B, 13 |
| 11 CMN_DRING (dash ring) | 8C05A360 | 14 / 3 | todo | 2,560 B, 18 |
| 18 O JPanel (jump panel) | 8C05EB76 | 2 / 2 / 1 / 2,000 | todo | 842 B, 10 |
| 14-17 Rocket H / HS / V / VS | 8C063100 ... 8C063480 | 14 / 2 | todo | 9,988 B, 54 |
| 19 O Save Point (checkpoint) | 8C064F40 | 15 / 3 | todo | 4,258 B, 19 |
| 9 O SWITCH | 8C067480 | 6 / 3 | todo | 3,146 B, 23 |
| 6 O TOGE (spikes) | 8C0682A0 | 2 / 3 | todo | 1,040 B, 5 |
| 20 WALL, 22-24 C SPHERE / CYLINDER / CUBE | 8C02DF86, 8C02DE20 ... | 2 / 2-3 | todo | 1,744 B, 10 |
| 12 O BALOON (Amy's goal) | 8C059E20 | 2 / 3 | todo | 24,026 B, 145 (the stage-clear chain) |
| O_LevelItem (upgrades) | 8C0698F6 | | todo | 16,772 B, 102 |
| 29 E SARU, 30 E SAITO, 31 E AMEMB, ... (12 badnik execs in 1ST_READ) | 8C0FBE04 ... | 2 / 3 / 1 / 600 | todo | 67,966 B, 319 |

The closures stop at functions that already have C (lowered) and leave out the SDK / C runtime (8C10C10C-8C111194) and
the game's asm model renderer (8C036BC0-8C03FDC0), which libsa1 serves with its draw capture (`LIB/src/rt/rt_draw.c`).
They are upper bounds: static call graphs include debug-edit paths (`8C042DC4`, the debug prints). **The 1.0 core set
(SET manager + rings / groups / lines + item boxes + springs + dash panel + dash ring + jump panel + rockets + checkpoint
+ switch + spikes + collision volumes) unites to 33,678 B in 172 functions**, about 32 KB after the 8C090FA0 size
correction. Already C on the way: the ring scatter 8C086F5E / 8C0870EA (NON_MATCHING), ring add 8C08A198, score
8C08A2A2, the item grants 8C07D3A0 / 8C07D43E / 8C07D3DC / 8C07D4A0 (matched), the power-up displays 8C044060 / 8C043C9E
(NON_MATCHING) whose helpers 8C043C00-8C04430A (9 functions, 1,366 B) are fatal stubs today (`stubs.tsv:82-90`).

### 1.4 How an object reaches the character (inside one world)

* **CCL** (interaction 1.1): rings register id 7 (`CCL_Init(tp, D_8C1BF0BC, 7, 1, 1)`, `twp->id = 11` = a Light Speed
  Dash target; `code_8C0869E0.c:108-123`); a touch sets mode 2, `func_8C08A198(1)` (+1 ring, the 1-up per 100) and SE 7
  (`:129-141`). Springs register id 4 with colliwk flag 0x800, a homing target (`asm/1st_read/text/8C065800.src`:
  `CCL_Init(tp, D_8C1BA884, 4, 1, 1)`). Enemies id 3, the air item box id 2.
* **DynamicCOL** for solid objects that the character stands on (rockets, switches: `DynamicCOL_Add` 8C0528AE in their
  closures).
* **Orders** through the player API: 8C065A98 -> 8C07C720 (SetInputP(pno, 3) + SetVelocityP: 5 units per step along the
  spring's up) or 8C07C768 (SPRINGB: smode 4, no-control time), item grants, lives `B_8C752BE0`, score.
* **Player-0 assumptions** (why the object must sit in the character's own world): the spawner and the range-out test
  player 0, player 1 and the camera; the ring adds to the one ring counter and credits only player 0's touches when
  the main character is Tails (`GetPlayerNumber() == 2`, `code_8C0869E0.c:132-140`); 53 player-0 references are
  hard-coded in 27 decompiled files (integration 6.8); enemies chase `playertwp[0]`.
* **Visibility**: the ring's display tests its sphere with 8C038D00 against the view in XMTRX before it draws (the
  extern in `code_8C0869E0.c`). libsa1's live worlds load a *virtual* view 40 units behind player 0 along the camera yaw
  (`LIB/src/world/sa1_world_tick.c` `sa1g_camera_view`), not GMod's view: objects GMod shows can be culled by the game.

### 1.5 libsa1 and G-SA1 today

* A world commits its whole 16 MB arena (`LIB/src/world/sa1_world.c:891`) plus the extension segment 0x18 (bump
  allocated, reset by every rebuild); a step costs 0.010-0.037 ms (integration "Measured facts"); a world is created in
  2-5 ms (round 4b2). One world per ticking character; followers get their own worlds (plan 14.1a).
* Round 5's host objects are in LIB (`sa1_hostobj.c`, `sa1_world_hostobj.c`, API 0.3 section of `libsa1.h`): registry
  1,024, window 64 per world, proxies as real game tasks at the synthetic exec 0x7F000100, events, kind mask, the held
  state. Their oracle runs libsa1's own layer on the original game (`harness/README.md`, `--hostobj`); G-SA1's side
  (5-gsa1) is still queued.
* Render: `sa1_character_render` returns host-space triangles of the character's world, interpolated relative to the
  character's root; no attribution of draws to tasks, no model ids (`libsa1.h:551-750`).
* Camera: `sa1_input` carries the stick, the buttons and `camera_yaw` only; the camera adapter's position is whatever the
  world holds (`sa1g_camera_refresh`), not the host camera's.
* G-SA1: the module is client-only (`gmcl_gsa1`); "the server side is pure Lua and needs neither the module nor a disc"
  (`addon/lua/autorun/gsa1_autorun.lua`); multiplayer is not done (`sh_net.lua` header); no scripted entities yet; new
  parts live behind the feature seam (`GSA1/README.md` "Features").
* **G64's precedent** (installed, read only): a "G64" creation tab with a custom content type and an icon generator
  that draws icons from the ROM's textures at run time (`g64_spawnmenu.lua:501-560`); coins are SENTs whose collection
  is host-side Lua, detected on the client, removed locally and reported to the server (`lua/entities/g64_coin.lua`).
  G-SA1 keeps the UI pattern and the client-detected, server-confirmed flow, and replaces the host-side logic with the
  game's own.

---

## 2. Where object logic runs (a)

### 2.1 The three options

| Criterion | A: mirrored SET objects in every character world | B: one object world with the characters mirrored into it | C: host-side (Lua / C++) |
|---|---|---|---|
| Owner rule (game logic is the game's code) | yes: the object runs unchanged with its own player 0 | partly: the object code runs, but every effect on a player must be marshalled to the real world by new code | no |
| Verifiable against the original | yes: the same records through the original SET manager in the harness (7.1) | no: no original situation has puppet players receiving objects' orders | no |
| Interaction with the character | native, same step (CCL, DynamicCOL, orders) | delayed by the forwarding; direct `playerwk` writes of objects cannot be forwarded generically | approximated |
| Player-0 code paths (1.4) | hold (one character per world) | break: up to 8 slots, but range, credit, AI and orders serve player 0 | n/a |
| Shared state | host forces the persistent condition bits (3.2) | natural (one copy) | natural |
| Objects with own movement | diverge per world -> actors (2.2) | natural | natural |
| CPU | each object once per world that sees it (1 + followers per client); 42-82 live per world in a dense stage | once | once |
| Memory | inside each world's existing heap and condition array | +16 MB per object world | small |
| Multiplayer | every client simulates near its own character; the server merges state | one simulating machine: a client (the server has no module) | any realm |
| Determinism / replay | per world, from the SET stream (2.4) | needs all characters' streams | n/a |

**Decision: A**, with B's one good property (one copy) given to the objects that need it: the actors, which run in one
owner world. C stays for what is not game logic: the editor and puppet drawing, the network state, GMod-physics side
effects (3.5).

### 2.2 Simulation classes

Every catalogue type carries one class (`v1_objects.md` assigns them; the table gives the rule and examples).

| Class | Examples (exec) | Runs in | Shared state (server authority) | Others see |
|---|---|---|---|---|
| STATIC | springs 8C065800 / 8C065EE4, dash panel 8C059B48, dash ring 8C05A360, jump panel 8C05EB76, spikes 8C0682A0, collision volumes WALL / C_* | every world in range | none | the object in their own world |
| CONSUMABLE | ring 8C0869E8, ring group 8C087B80 (per-ring bits), ring lines 8C061C40 / 8C062020, item box 8C05D10C, air item box 8C05D9F6 | every world in range | present / gone, `scUserFlag`; respawn time | gone in every world |
| PERSONAL | checkpoint 8C064F40, hint orbs 8C060460 / 8C060D40, emblems, the goal (capsule; location in `v1_objects.md`) | every world in range | none shared; per player (client) | their own state |
| TOGGLE | switch 8C067480 and what it controls | every world in range | on / off per switch | forced into their worlds |
| ACTOR | badniks (E_SARU 8C0FBE04 ...), carriers (rockets 8C063100... while ridden), walking NPCs | the owner's world only | owner, pose stream, gone | puppet + host-object proxy (ENEMY / TARGET) |
| ARENA | bosses (B_* overlays), stage-overlay objects (STGnn types) | an arena world / a stage-pack world | session | puppet stream |
| HOST | trigger volumes, camera switches, splines, NPC dialogue UI (`v1_triggers.md`, `v1_npcs.md`) | host (Lua) calling libsa1 per character | server | n/a |

A rocket is STATIC while nobody rides it and becomes an ACTOR for the duration of a ride in multiplayer (the first
rider owns it; 3.4); in single player every class but ARENA runs in the one world anyway.

### 2.3 Budget

* **CPU**: objects added 186K SH-4 instructions per step (45 %) to Emerald Coast act 1 in the interpreter (1.2). libsa1's
  native step is 0.010-0.037 ms; by that ratio objects add on the order of 10-20 microseconds per world and step
  (UNVERIFIED estimate: to be measured with the S1 test). The display pass and capture cost more per drawn object; the
  dominant cost is G-SA1's Lua mesh building, which instancing removes (5.3).
* **Memory**: no world per object or per type (16 MB committed and 2-5 ms each). Live objects use the world's game heap
  (589,824 B; 391 KB free at EC1's stage start, 335 KB at its peak with 82 objects; a libsa1 world without stage tasks
  starts with more free) and its 4,096-task pool; each record in a world's window costs a 0x20-byte SET entry in segment
  0x18 and a 0x10-byte condition. The window stops adding when the heap's largest free block is below 4 KB (the round-5
  rule, `sa1_world_hostobj.c` `ho_heap_walk`).
* **Hundreds of rings**: the game spawns only what is near (400 units = 1,600 HU); a host layout of thousands of records
  costs registry memory and one window sort per pair. CCL list capacities per step (id 7: 256, id 4: 512, id 3: 256;
  interaction 1.1) and the 1,023-condition array cap the window.

### 2.4 Determinism and replay

A world's object state is a function of its inputs: the pad, the camera, the round-5 host-object stream, the **SET
stream** (records added / removed / updated, forced state, owner changes, per pair) and the virtual stage number
(5.2). Rules as for host objects (interaction 2.8): the window order is (squared distance, record id), the lowest free
condition slot, ascending ids; no host time, no host `rand()`; each world has its own game `rand()` (objects draw from
it). Traces get a sidecar `NAME.set.bin` (7.1); the same stream is the "world track" a machinima recording needs
(`v1_machinima.md`).

### 2.5 Multiplayer and authority

The server (Lua, no module, no disc) owns every G-SA1 object entity and its shared state; each client registers the
entities it knows with its module and simulates them in its own worlds.

| Message / variable | Direction | Content | Server rule |
|---|---|---|---|
| `gsa1_obj_ev` (reliable) | client -> server | entindex, event (GONE, USERBITS, TOGGLE, PASSED), condition bits, `scUserFlag` | sender is an active character; distance from `ply:GetPos()` to the object within the type's reach + a ping allowance; a legal transition; rate limit; `hook.Run("GSA1ObjectEvent", ply, ent, event)` may veto |
| entity NetworkVars `State`, `UserBits`, `RespawnAt` | server -> all | the shared state | set by the server only |
| `gsa1_actor` (unreliable, 20 Hz) | owner -> server -> PVS | entindex, position, yaw, action, frame, flags | displacement clamp per type |
| `gsa1_actor_hit` (reliable) | client -> server -> owner | entindex, attack kind, level | 3.2-style validation of `libsa1_interaction.md` |
| NW2Entity `gsa1_owner` | server -> all | the actor's simulating player | the nearest active character within the actor's range, with hysteresis |

* **Latency**: the collecting client acts at once in its own world (no round trip, like G64's coins); the others see it
  within the round trip plus a pair. Two characters touching the same ring within that time both get it (no rollback;
  owner question Q2).
* **Dedicated servers** have no module and no disc: all simulation is on clients; an actor with no character near it is
  dormant at its SET position, as the game itself does (out of range = no task).
* G-SA1's multiplayer (remote characters as puppets, `sh_net.lua`) is not built yet; objects' sync is designed to run
  in single player through the same server path (a listen server with one player), so it is exercised from day one.

### 2.6 Several worlds on one client (followers)

* Follower worlds mirror SET records too (their window centred on the follower, slot 1, as for host objects, interaction
  6.5); the game's own spawner also spawns within 40 units of player 1.
* **Ring credit**: a ring the follower touches is counted in the follower world's counter (SA1's rule: Tails' touches
  count for Sonic unless Tails is the main character, 1.4); G-SA1 forwards the count to the leader
  (`sa1_character_add_rings(leader, n)`, option, default on) and the ring's GONE goes to the server as usual.
* **Render and sound dedupe**: a record is drawn from one world only (priority: the local character's world, then the
  followers'; switch with hysteresis), its sound events are played from that world only (events carry the record, 5.3).

### 2.7 Overlay-bound objects and bosses

The 0x0C900000 slot holds one image per world (1.3), so:

* **Stage packs** (1.x): one STGnn chosen per map session is composed into every local world instead of nothing (or
  B_CHAOS7: Super Sonic's aura is unavailable meanwhile); that stage's list types become available as G-SA1 types
  (e.g. the trampoline, whose model `STG02_Trampoline` 0x0C9C8C98 is stage data). Stage objects without C stay disabled.
* **Arena worlds** (bosses, `v1_enemies.md`): the fighting characters' worlds are rebuilt with the boss image; the boss is
  an ACTOR of one of them.
* Relocating overlays to other addresses is not an option: GUEST-ARENA has no relocation, and overlay data holds
  absolute pointers (integration 1.3).

---

## 3. Collection, hits, springs and shared state (b)

### 3.1 In the world

| Family | Meets the character through | Effect (game code) | Host involvement |
|---|---|---|---|
| Rings, ring groups / lines | CCL id 7 (also the Light Speed Dash list, the magnetic barrier's pull) | +1 ring (8C08A198), SE 7, sparkle, then 8C091BA4 (gone) | the GONE event |
| Item boxes (ground, air) | CCL (air: id 2, a homing target) | the item: barrier / magnetic barrier / invincibility grants (8C07D43E / 8C07D3DC / 8C07D4A0), the power-up display tasks (8C07D3A0), rings, 1-up (the full content list: `v1_objects.md`) | GONE |
| Springs, dash panels, dash rings, jump panels, rockets | CCL id 4 (springs targetable) / DynamicCOL | orders: 8C07C720, 8C07C768, speed and mode changes | none |
| Checkpoint, switch | CCL / DynamicCOL | the restart point; the switch state | PASSED (per player) / TOGGLE (shared) |
| Badniks | CCL id 3 both ways | damage both ways, 8C0489E4's bounce, score, animal | actor rules (3.4) |

The round-5 host-object proxies and the SET objects live side by side in a world: proxies never pair with game objects
(attr 0xE0000, interaction 2.3), so a GMod NPC proxy and an SA1 spring do not interact; both interact with the character.

### 3.2 The shared state is the persistent part of OBJ_CONDITION

1. In world W the game clears bit 15 of record R's condition (8C091BA4: a ring was collected) or changes its
   `scUserFlag` (a ring of a group was taken).
2. libsa1's post-step scan sees it and queues `SA1_SET_EVENT_GONE` / `USERBITS` {record, the task's exec, step, position}.
3. The client sends `gsa1_obj_ev` with the entity index; the server validates (2.5) and sets the entity's `State` /
   `UserBits` (and `RespawnAt` if the object respawns).
4. Every client applies the NetworkVar change: `sa1_set_state(R, present = 0, user_bits)`; libsa1 writes it into the
   condition of R in every world at the next pair start: bit 15 cleared, and a live task is freed silently (`FreeTask`:
   DestroyTask runs its dest, no sound, no score). The collecting world already holds that state, so nothing happens
   there.
5. Respawn / reset: the server sets `State` present again; libsa1 sets bit 15 once bit 0 is clear; the game's spawner
   creates the task again when the character is in range: the game's own mechanism, toggled by the host.

Per-world bits (0x0001 alive, 0x0002 moved, `scCount`, `unionStatus`) are never shared. PERSONAL objects report PASSED
for the local HUD and respawn logic only. TOGGLE objects mirror their switch state (the switch-flag storage is in
`v1_objects.md`; its address is UNVERIFIED here).

### 3.3 Respawn, reset, saves

* Per-entity property "Respawn after (s)" (0 = never, SA1's rule) and a server default `gsa1_sv_obj_respawn`; a Q-menu
  admin button "Reset G-SA1 objects" and a context-menu "Reset this object" (owner question Q1).
* Saves and dupes keep the layout (the NetworkVars). The runtime state is reset on load by default (Q4).
* A character's death respawns the character only; objects keep the server's state (SA1 restarts the act instead).

### 3.4 Actors

* **Ownership**: the server sets `gsa1_owner` to the nearest active character within the type's range (the list
  distance: Kiki 600 units = 2,400 HU), with 20 % hysteresis; none = dormant.
* **The owner's world** runs the record as a normal SET object. Hand-off uses the game's own "moved" mechanism: the new
  owner's world spawns the record with condition bit 1 and a `{ang, pos, scl}` block at the last streamed pose (the
  spawner reads it, 1.1); the actor's internal state restarts, as when the game re-spawns an object that went out of
  range.
* **The other worlds** do not spawn the record (`SA1_SETF_ACTOR` without ownership); G-SA1 registers a round-5 host object
  (ENEMY for a badnik: touch harm, homing and lock-on target; TARGET for a carrier) at the streamed pose, and draws the
  puppet from the stream (5.3).
* **Hits by others**: a HIT on that proxy goes to the server (`gsa1_actor_hit`), then to the owner, whose module injects
  it with a new host-object kind **ATTACKER** (5.4): a list-1 (projectile) shape at the actor's shape 0 centre with the
  hit's attack level for one step. The game's own pass out-levels the actor and its own damage check (8C0489E4) kills
  it: explosion, animal, score in the owner's world; as for E-102's shots the attacker's owner is not player 0, so
  nobody in the owner's world bounces (interaction 1.6). The death's GONE goes to everybody.
* **GMod damage** (weapons, explosions) on a badnik entity: the server forwards it as an ATTACKER hit (level from the
  damage, option; owner question Q8, `v1_enemies.md`).

### 3.5 Plain GMod players, NPCs and props (host-side, optional)

Plain players (no character), NPCs and props are not in any world. Optional GMod-side imitations, clearly not game
logic, server-side: springs / dash panels / jump-type launchers push what touches them with the type's speed converted
(5 units per step = 1,200 HU/s along the spring's up, interaction 6.2.1); rings and items are not collectable by plain
players by default (owner question Q3).

---

## 4. The G-SA1 tab and the entities (c)

### 4.1 The creation tab

* `spawnmenu.AddCreationTab("G-SA1", builder, icon, order, tooltip)` (`GMOD/lua/includes/modules/spawnmenu.lua:182`); the
  builder makes a `SpawnmenuContentPanel` with search (`ctrl:EnableSearch("gsa1", "PopulateGSA1")`) and a populate hook
  that fills a tree, exactly as sandbox's Entities tab (`entities.lua:22-38`) and G64's tab.
* Tree: **Collectables** (Ring, Ring line, Ring circle, Item box: one entry per content, Air item box), **World objects**
  (Spring, Wall spring (SPRINGB), Dash panel, Dash ring, Jump panel, Rockets, Checkpoint, Switch, Spikes, Goal, invisible
  collision volumes for builders), **Triggers** (camera, spline, board on / off, event: `v1_triggers.md`), **Badniks**,
  **Bosses** (listed, greyed with "needs an arena" until S5), **NPCs** (`v1_npcs.md`). Category nodes come from the
  catalogue (4.2); types whose code is not in the loaded module are shown disabled with the reason.
* Icons: a content type `gsa1_object` (as G64's `g64_entity`) whose icon is rendered by the module from the owner's
  disc the first time the tab opens (the object's model and OBJ_REGULAR textures, `sa1_model_mesh`) into
  `garrysmod/data/gsa1/icons/<type>.png` on the owner's machine only (owner question Q6); a generic icon until then.
* Click = `RunConsoleCommand("gm_spawnsent", class)`: `Spawn_SENT` checks `CanPlayerSpawnSENT`, calls the
  `PlayerSpawnSENT` hook (the sandbox limit), the entity's `SpawnFunction`, then makes the undo entry "Undone <name>" and
  the cleanup entry (`commands.lua:777-892`). The right-click menu of the icon gets "Spawn with the tool gun" (creator
  tool) like every entity icon, plus "Place with the G-SA1 placer".
* The same classes are `Spawnable` with `Category = "G-SA1"`, so they also appear under Entities > G-SA1 (where the
  owner asked for the board trigger boxes, plan 14.1c).

### 4.2 Entity model

* **One shared catalogue** (`feat/sh_objects.lua`): per type the class name, category, display name, the libsa1 type id
  (from `sa1_object_info`), the simulation class, the reach for the server's validation, the NetworkVars with their
  `Edit` tables and conversions, the GMod hull, defaults. A base class `gsa1_obj_base` and one generated class per type
  (`scripted_ents.Register` in a loop: undo text, spawnlists, dupes and `gm_spawnsent` see a real class per type;
  whether classes registered from a feature file show in the Entities tab like files in `lua/entities` is UNVERIFIED).
* **Server**: static entities (`MOVETYPE_NONE`), `SpawnFunction` aligns to the hit surface (springs and dash panels take
  the surface normal / the player's yaw), state machine (present / gone / respawn), validation of `gsa1_obj_ev`, owner
  assignment for actors, limits.
* **Physics / selection**: a hull from the catalogue so the physgun and the tool gun can pick the entity; solid for GMod
  players only where the object is solid in SA1 (springs, item boxes, rockets), non-solid otherwise (rings; the exact
  collision group that keeps it pickable but walk-through is UNVERIFIED and must be tested in game). Moving an entity
  (physgun drop, `SetPos`) updates its record; libsa1 frees a live task and the spawner re-creates it at the new place.
* **Transmission**: `UpdateTransmitState` -> `TRANSMIT_ALWAYS` (objects are static and cheap; the game spawns them by
  distance, not by visibility, so a client must know objects behind walls). Ring lines and circles are one entity each
  (the game's own O_GRING / line types), which keeps the edict count down (GMod's edict limit: UNVERIFIED for the current
  x64 branch).
* **Exclusions**: G-SA1 object entities carry `ENT.GSA1Object = true`; the moving-collision scan (`cl_collision.lua`) and
  round 5's host-object scanner (`cl_interact.lua`, 5-gsa1) skip them, because each world has the object's own collision
  and CCL already.
* **Drawing**: the entity draws nothing itself; the client's object manager draws (a) the instanced draws of the worlds
  (5.3) for records that are live in a world, (b) a puppet (the model at the record's pose, a simple idle turn for rings
  at the game's rate, 0x16C BAMS per step) for records that are not, up to a distance option; ACTOR puppets follow the
  stream. One render hook batches everything (the existing `gsa1_render` pattern), no per-entity `Draw` / `Think`.

### 4.3 Properties

* NetworkVars with `KeyName` and `Edit = {type, order, min, max, values}` (as `GMOD/lua/entities/sent_ball.lua:21-22`)
  and `ENT.Editable = true` give the right-click "Edit Properties" window (`lua/autorun/properties/editentity.lua:13`).
  Friendly controls per type map to the three SET parameters and the rotation (a spring's speed in HU/s, an item box's
  content as a combo, a jump panel's number, a ring line's count / spacing / shape), plus an "Advanced: raw SET
  parameters" group; conversions (units, BAMS) live in the catalogue, the SA1-space values in the record.
* Limits: 32 NetworkVars per type and 4 strings; longer data (NPC pages, spline points) goes into entity modifiers
  (`duplicator.StoreEntityModifier`) with its own net message.
* Context-menu properties (`properties.Add`): "Reset this object", "Respawn now", "Link to..." (switch targets, jump panel
  sequences), "Show its range" (debug), "Characters..." (which kinds the record exists for, SA1's per-character
  layouts; owner question Q9).

### 4.4 Tools (Tools tab, category "G-SA1")

| Tool | Does |
|---|---|
| `gsa1_place` | places any catalogue type aligned to the surface, with a ghost preview, grid / angle snapping, and drag-placement of ring lines and circles (one entity) |
| `gsa1_link` | links a switch to its targets and orders jump panels (the numbered homing chain) |
| `gsa1_spline`, `gsa1_trigger` | `v1_triggers.md` (point-by-point splines with separate ids, trigger volumes) |
| `gsa1_import` (1.x) | spawns the common objects of an SA1 stage's layout (the owner's disc: `SETllaaC.BIN`) at an offset; stage types are skipped unless a stage pack is loaded |

Each tool action is one undo block (`undo.Create` with "Undone G-SA1 ring line (8)") and goes into the cleanup list.

### 4.5 Saves, dupes, undo, cleanup, limits

* Saves and dupes: NetworkVars are stored as `DT` and restored (`duplicator.lua:253-254, 323-324, 827-828`); entity
  modifiers for long data; runtime state reset on paste / load (Q4). AdvDupe2's behaviour: UNVERIFIED.
* Undo: `gm_spawnsent` and the tools (above). Cleanup: `cleanup.Register("gsa1_objects")` (Utilities > Cleanup).
* Limits: `sbox_maxgsa1_objects` (`ply:CheckLimit`), admin-only categories (bosses), `gsa1_sv_obj` master switch.
* Map persistence: GMod's "Make Persistent" property works on scripted entities when the server allows it.

### 4.6 Settings (Q menu > Options > G-SA1 > "Objects"; the owner's rule: every option has a control)

| ConVar | Default | Realm |
|---|---|---|
| `gsa1_obj` | 1 | client: objects in the worlds |
| `gsa1_obj_window` | 512 (16-1023) | client: records per world |
| `gsa1_obj_puppets`, `gsa1_obj_puppet_dist` | 1, 8,000 HU | client: draw objects outside the worlds |
| `gsa1_obj_follower_credit` | 1 | client: follower rings count for the leader |
| `gsa1_obj_debug` | 0 | client: ranges, records, owners, live / puppet |
| `gsa1_sv_obj` | 1 | server: G-SA1 objects on |
| `gsa1_sv_obj_respawn` | 0 s | server: default respawn time |
| `gsa1_sv_obj_plain_players` | 0 | server: 3.5 imitations for plain players |
| `gsa1_sv_obj_reach_slack` | 64 HU | server: validation allowance |
| `sbox_maxgsa1_objects` | 1,000 | server limit |

---

## 5. What libsa1 needs (d)

### 5.1 API draft (API 0.4, a new section of `libsa1.h`)

```c
/* ---- SET objects (API 0.4 draft; sadc docs/research/v1_arch.md) ------------------------------------------------ */
#define SA1_API_SET 1

/* the catalogue: the object types this build can run (a generated table: exec, list-entry values, model, class) */
#define SA1_OBJCLASS_STATIC     1u
#define SA1_OBJCLASS_CONSUMABLE 2u
#define SA1_OBJCLASS_PERSONAL   3u
#define SA1_OBJCLASS_TOGGLE     4u
#define SA1_OBJCLASS_ACTOR      5u
#define SA1_OBJCLASS_ARENA      6u
typedef struct sa1_object_info {
    uint32_t    struct_size;
    uint32_t    type;               /* libsa1 type id, stable across versions */
    const char *name;               /* the game's list name ("RING", "O ITEMBOX") */
    uint32_t    exec;               /* DC address of the exec */
    uint8_t     im, level;          /* the list entry: task works, task list */
    uint16_t    list_flags;         /* the list entry: 1 own distance, 2 always alive, 4 first pass */
    float       range;              /* host units (the entry's distance) */
    uint32_t    cls;                /* SA1_OBJCLASS_* (2.2) */
    uint32_t    model;              /* guest address of the NJS_OBJECT for icons and puppets (0 = none) */
    uint32_t    rotation_order;     /* how its code applies ang (for the host rotation conversion) */
    uint32_t    available;          /* 1: its code is in this build and its image is in the worlds */
} sa1_object_info;
SA1_API uint32_t   sa1_object_count(const sa1_context *ctx);
SA1_API sa1_status sa1_object_info_get(const sa1_context *ctx, uint32_t type, sa1_object_info *out);

typedef struct sa1_set_record {
    uint32_t struct_size;
    uint32_t type;                  /* sa1_object_info.type */
    uint32_t flags;                 /* SA1_SETF_* */
    float    position[3];           /* host space */
    float    rotation[9];           /* host rotation matrix; converted to BAMS in the type's order inside the world */
    float    params[3];             /* the SET entry's parameters as the game reads them (twp->scl), SA1 semantics */
    uint32_t kinds;                 /* character kinds the record exists for (bit per kind; 0 = all) */
    uint64_t user;                  /* host tag (G-SA1: the entity index), returned in events */
} sa1_set_record;
#define SA1_SETF_ACTOR     0x1u     /* spawned only in the world that owns it (sa1_set_own) */
#define SA1_SETF_ALWAYS    0x2u     /* condition flag 0x0008: never ranges out */
#define SA1_SETF_RAW_BAMS  0x4u     /* rotation[0..2] are BAMS x, y, z as in a SET file (layout import) */

SA1_API sa1_status sa1_set_add(sa1_context *ctx, const sa1_set_record *r, uint32_t *out_id);
SA1_API sa1_status sa1_set_update(sa1_context *ctx, uint32_t id, const sa1_set_record *r);  /* a live task respawns */
SA1_API sa1_status sa1_set_remove(sa1_context *ctx, uint32_t id);                          /* silent */
/* the shared state (3.2): applied in every world at its next pair start */
SA1_API sa1_status sa1_set_state(sa1_context *ctx, uint32_t id, uint32_t present, uint32_t user_bits);
/* actors (3.4): the world that simulates the record (0 = dormant) and where it continues */
SA1_API sa1_status sa1_set_own(sa1_context *ctx, uint32_t id, sa1_character_id owner, const float position[3],
                               const float rotation[9]);
SA1_API sa1_status sa1_set_hit(sa1_context *ctx, uint32_t id, uint32_t attack_level);       /* ATTACKER (5.4) */
typedef struct sa1_set_pose {
    uint32_t struct_size;
    uint32_t live;                  /* 1: a task of the record runs in the owner's world */
    float    position[3], rotation[9];
    uint32_t action;                /* the motion's action / frame when the task has a motion (puppets) */
    float    frame;
    uint32_t flags;
} sa1_set_pose;
SA1_API sa1_status sa1_set_pose_get(sa1_context *ctx, uint32_t id, sa1_set_pose *out);

#define SA1_SETLIST_LIBSA1 0u       /* the generated list (production) */
#define SA1_SETLIST_STAGE  1u       /* the world's stage list (DISC worlds, references) */
typedef struct sa1_set_config {
    uint32_t struct_size;
    uint32_t window;                /* records per world, 1..1023 (default 512) */
    float    margin;                /* host units beyond each type's range (default 100 SA1 units) */
    uint32_t list;                  /* SA1_SETLIST_* */
    uint32_t stage;                 /* the level / act GetStageNumber reports (default 0x0100) */
} sa1_set_config;
SA1_API sa1_status sa1_set_configure(sa1_context *ctx, const sa1_set_config *cfg);

#define SA1_SET_EVENT_SPAWNED   1u  /* the record's task appeared in this world: draw it from the world */
#define SA1_SET_EVENT_DESPAWNED 2u  /* it went (range-out, removal, forced): puppet again */
#define SA1_SET_EVENT_GONE      3u  /* the game cleared condition bit 15 (collected, broken, killed) */
#define SA1_SET_EVENT_USERBITS  4u  /* scUserFlag changed (a ring of a group) */
#define SA1_SET_EVENT_TOGGLE    5u  /* a type's state word changed (switch) */
#define SA1_SET_EVENT_PASSED    6u  /* PERSONAL types: the character activated it (checkpoint) */
typedef struct sa1_set_event {
    sa1_character_id character;     /* the world */
    uint32_t type, record;
    uint64_t user;
    uint32_t condition;             /* ssCondition after the step */
    uint32_t user_bits;
    uint32_t exec;                  /* the task's exec when it changed */
    uint32_t step;
    float    position[3];           /* host space */
} sa1_set_event;
SA1_API sa1_status sa1_poll_set_events(sa1_context *ctx, sa1_set_event *out, uint32_t capacity, uint32_t *count);

/* models for icons and puppets: an NJS_OBJECT tree of the loaded images, untransformed (sa1_mesh rules) */
SA1_API sa1_status sa1_model_mesh(sa1_context *ctx, uint32_t model, sa1_mesh *out);

/* a world's object draws (5.3) */
typedef struct sa1_instance {
    uint32_t record;                /* 0 = an unattributed world draw (particles, scattered rings, animals) */
    uint32_t model;                 /* guest address of the model: the host caches its mesh (sa1_model_mesh) */
    float    matrix[12];            /* host space 3 x 4, interpolated at alpha in world space */
    uint32_t draw;                  /* index into the render's draws (material, texture, flags) */
} sa1_instance;
SA1_API sa1_status sa1_world_render(sa1_context *ctx, sa1_character_id world, float alpha, uint32_t flags,
                                    const sa1_render **draws, const sa1_instance **instances, uint32_t *num_instances);

/* the host camera in the world (5.2, 5.3) */
typedef struct sa1_view {
    uint32_t struct_size;
    float    position[3], forward[3], up[3];   /* host space */
    float    fov_y, aspect;
} sa1_view;
SA1_API sa1_status sa1_character_set_view(sa1_context *ctx, sa1_character_id id, const sa1_view *view);

/* round 5's host objects: one more kind (5.4) */
#define SA1_HOSTOBJ_ATTACKER 7u
```

### 5.2 World side

* **Attach**: when the context has SET records (or `sa1_set_configure` enables the layer), the world creates the manager
  task as the stage start does, `CreateElementalTask(2, 7, 0x8C090FA0)` (list 7: first in every step, 1.1), and does
  its mode 0 itself, as the boot already does the landtable manager's first exec (integration "Measured facts"):
  `B_8C79A3CC` = the generated list in segment 0x18 ({count, entries} of `OBJ_ITEMENTRY` with each type's exec, im, list,
  flags, distance and name), `B_8C79A3DC` = 0, the task in mode 1. In STAGE list mode (DISC worlds, references) mode 0 is
  the game's own: the stage's list from `D_8C1C143C`, the SET data from a SET image libsa1 loads from the disc into
  segment 0x18 behind `D_8C1C0EDC`.
* **Window** (pre hook at the pair start, before the executor: round 5's `hostobj_pre` place, interaction 6.4 row 5):
  the records of the world's character kind within (type range + margin) of player 0 (follower worlds: slot 1), nearest
  first by (squared distance, id), at most `window`. An entering record takes the lowest free condition (`ssCondition` 0
  and no live task) and a 0x20-byte SET entry in segment 0x18 (type = its list index, rotation in BAMS for the type's
  order, position and parameters in SA1 space, converted in the world's FP mode at the latch); `B_8C79A3DC` = the
  high-water mark. A leaving record keeps its slot until its task is gone (bit 0 clear); slots never move while a task
  points at them (`tp->ocp`).
* **Forced state** (same hook): present 0 -> bit 15 cleared and, with bit 0, `FreeTask(pTask)` (its dest runs at level
  order in this step, no sound, no score); present 1 -> bit 15 set once bit 0 is clear; `user_bits` -> `scUserFlag`;
  an owner change -> the owner's world spawns the record with bit 1 and a CAlloc'd 0x24-byte moved block at the given
  pose, the other worlds clear bit 15 for it (freeing a live task) and keep it clear.
* **Post hook** (after the CCL pass): per slot, bit 15, bit 0, `scUserFlag` and the type's state words against the last
  step -> events; owned actors' live pose (taskwk pos / ang; action and frame when the task has a motion).
* **Range-out** 8C0919CC becomes the game's C (the `ret 0` stub goes), so the camera adapter's position must be real:
  8C0919E8 keeps everything near the camera alive. `sa1_character_set_view` writes the host camera into the camera
  adapter's taskwk and the view matrix (below).
* **Virtual stage**: `GetStageNumber()` reports the configured level / act (default 0x0100, as today); object code that
  branches on the stage (the animals of a level, `D_8C1623D0`; per-level tables) follows it.
* The segment-0x18 blocks are re-created after every rebuild (apply_ram and transplants reset the segment, interaction
  6.4 row 2), like round 5's host-object block; the arena part (conditions, tasks) is ordinary world RAM.

### 5.3 Render

* **Attribution**: the capture tags each draw with the task whose display (or exec) runs (the executor's current task)
  and its top-level ancestor: a task whose `ocp` lies in the condition array belongs to its record; descendants of
  player 0's task to the character; the rest to the world (particles, scattered rings, explosions, animals).
  `sa1_character_render` keeps returning the character's draws only, so G-SA1's character path is unchanged.
* **World space**: object draws are interpolated per record in world space (the n-th draw of a model in a record with
  the previous pair's), never in the character's root frame (a turn of the character would swing them around it).
* **Instancing**: a model draw whose points were not rewritten since the image was loaded (rigid: rings, springs,
  panels, boxes) comes out as an instance {record, model, matrix, draw}; the host builds one mesh per model with
  `sa1_model_mesh` and caches it (an `IMesh` in G-SA1), so a hundred rings cost a hundred matrices instead of a hundred
  Lua vertex streams. Sprites, effects and rewritten models keep the triangle streams.
* **Capture filter**: a world that is not the render world of a record skips capturing it; its display still runs
  (displays carry logic in SA1, integration 7.2).
* **The view**: the steps and the display pass see the host camera (`sa1_character_set_view`), or the game's own test
  8C038D00 culls objects GMod shows (1.4). The screen distance follows the host FOV; the test stays the game's
  (`rt_draw.c` section 6). For the character, `visibility_fidelity.md` settled the virtual view; whether a host view
  changes anything the character references record is UNVERIFIED and must be replayed before the switch.
* **Textures**: common objects use OBJ_REGULAR (rings, items: `sa1_assets.md`), which every character world loads;
  object draws need context-wide archive ids for the common archives (draws name archives per character kind today).

### 5.4 The ATTACKER host-object kind (actors)

`SA1_HOSTOBJ_ATTACKER` (7): a CCL id 1 task like E-102's shot (interaction 1.1), one sphere at the target record's
shape 0 centre, attack level from the request, applies to ids 2-5 (flag 0x40 | 0x80), alive for one step, then freed.
`sa1_set_hit` queues it in the owner's world. The out-levelling, the death and the score are the game's (the pass,
8C0489E4, the enemy's death code); nothing bounces in the owner's world, because the attacker's owner is not player 0
(interaction 1.6). Test: `set_test.c` case 8.

### 5.5 Decompilation (SADC; Wave A style: staged, re-verified, then landed)

| Batch | Functions | To do (closure, upper bound) | Phase |
|---|---|---|---|
| SET-0 | the SET manager 8C090EC0-8C091C20, 26 todo: 8C090FA0, 8C091016, 8C0915A2, 8C0915D2, 8C091148, 8C091380, 8C0910C0, 8C09111A, 8C0916AE, 8C0916EE, 8C09174E, 8C0917E0, 8C091890, 8C091912, 8C0919CC, 8C0919E8, 8C091A92, 8C090EC0, 8C090F16 and small ones | about 2.9 KB | S1; then `stubs.tsv:139` goes |
| OBJ-1 | ring lines 8C061C40 / 8C062020, item box 8C05D10C with its closure, the power-up display helpers 8C043C00-8C04430A | 4,118 B, 26 functions, + 1,366 B helpers | S2 |
| OBJ-2 | springs 8C065800 / 8C065EE4 / 8C067E40 (8C065A98), dash panel 8C059B48, dash ring 8C05A360, jump panel 8C05EB76, spikes 8C0682A0, collision volumes 8C02DE20-8C02DF86 | about 13 KB, about 78 | S2 |
| OBJ-3 | rockets 8C063100-8C063480, checkpoint 8C064F40, switch 8C067480 | about 17 KB, about 96 | S2 |
| BAD-n | the 12 1ST_READ badnik execs (first wave picked in `v1_enemies.md`) | 67,966 B, 319 (all twelve) | S4 |

SET-0 and OBJ-1..3 together: 33,678 B in 172 functions by the raw index (about 32 KB with 8C090FA0's real size); shared
helpers make the per-batch rows add up to more. Every type needs its harness reference (7.1) before its C is trusted;
functionally verified NON_MATCHING C is acceptable (owner rule).

### 5.6 Limits

| Limit | Value | Source |
|---|---|---|
| Records per context | 16,384 (proposal) | registry |
| Records per world | 1,023 + 1 | the condition array (8C0916EE caps at 0x3FF) |
| Live objects per world | the game heap (589,824 B; ~0.7 KB per object measured) and the task pool (4,096) | 1.2 |
| CCL lists per step | id 7: 256 (rings), id 4: 512, id 3: 256, id 2: 128 | interaction 1.1 |
| Events | 256 per world and pair, overflow counted | as host objects |

---

## 6. What G-SA1 needs

Feature `objects` behind the seam (`GSA1/README.md` "Features"); the files its round owns:

| File | Content |
|---|---|
| `src/module/bind_objects.cpp` | `SetAdd / SetUpdate / SetRemove / SetState / SetOwn / SetHit / SetEvents / SetConfig / SetPose`, `ObjectInfo` (catalogue), `ModelMesh`, `WorldInstances(world, alpha)`, `SetView`; flat arguments and returns on the hot path |
| `src/core/objects.{hpp,cpp}` | record <-> entity map, the per-model mesh cache, instance batches per texture and material, the render world of each record |
| `addon/lua/gsa1/feat/sh_objects.lua` | the catalogue (types, categories, NetworkVars, conversions, reach, class), class generation, ConVars (4.6), net strings |
| `feat/cl_objects.lua` | registration from entities (`NetworkEntityCreated`, `EntityRemoved`, NetworkVar notifications), events to the server, forced state, actor streams, puppets and instanced drawing, icons, debug drawing |
| `feat/sv_objects.lua` | validation, state, respawn timers, reset, ownership, the actor-hit relay, limits, cleanup, hooks `GSA1ObjectEvent` and `GSA1CanSpawnObject` |
| `feat/cl_objects_tab.lua` | the creation tab (4.1) |
| `addon/lua/weapons/gmod_tool/stools/gsa1_place.lua`, `gsa1_link.lua` | the tools (4.4) |
| patches | `cl_collision.lua` and round 5's scanner skip `GSA1Object`; `cl_character.lua` passes the view (`SetView`) |
| tests | `feat_objects_test.cpp` (registry, window churn against the module, instancing), `feat_objects_binding_test.cpp`, `feat_objects_test.lua` (catalogue validity, state machine, `RestoreNetworkVars` round trip, server validation) |

---

## 7. Verification

### 7.1 The oracle: the original SET manager with the same records

* **Harness `--set FILE`**: the gdFs HLE serves FILE in place of the stage's `SETllaaC.BIN` (it already serves empty SET
  files for `--no-objects`, `harness/src/t_sim.c:394-406`); record types are the stage list's indices. The original game
  then spawns exactly the test layout through its own SET manager in its own stage.
* **Sidecar `NAME.set.bin`** (`sa1_trace_ext.h`, plan 2.6): per step the condition array (count; the 16-byte entries,
  `pTask` as a handle), the taskwk / motionwk of every live SET task by condition index, the SET events. `sa1trace.py`
  learns `set` as it learned `p1` and `obj`.
* **References** (Emerald Coast act 1, Sonic, `--no-objects --set`):
  * `set_ring_ec1`: 30 rings and a ring group (type 21) collected along a run, a host-damage scatter, re-collection;
  * `set_items_ec1`: item boxes of every content (type 13) and an air item box (82);
  * `set_springs_ec1`: SPRING (1), SPRINGB (2), dash panel (3), dash ring (11), jump panels (18), rockets (14-17),
    spikes (6), a checkpoint (19), a switch (9), collision volumes (20, 22-24);
  * `set_ec1_common`: the real `SET0100S.BIN` filtered to its 1ST_READ types (289 of 788 records) under
    `obj_ec1_sonic`'s script: the manager and the windowing at a real stage's density.
  The existing `spring_ec1` (`--create-task` of a real spring) and `obj_ec1_kill` (a real Rhinotank kill) stay.
* **libsa1 replays them strictly** (`tests/build_replay.py`, a new list SET; DISC mode composes STG01, so the stage list
  is in the world; the layer in STAGE list mode loads the same SET file): player state and the sidecar bit-exact,
  x86 = x64, as libsa1 replays the character and host-object references today.
* **Generated list = stage lists**: `LIB/gen/gen_objlist.py` builds the libsa1 list from the stage lists of the disc and
  reports where stages disagree for the same exec (im, list, flags, distance: e.g. O JPanel's own distance of 2,000
  units in EC1); production takes one named reference entry per type; `set_ring_ec1` also runs in LIBSA1 list mode with
  identical results.

### 7.2 libsa1 tests (`tests/set_test.c`, `tests/build_set.py --run`, `SA1_TEST_GDI`)

1. Window churn: 5,000 ring records on a grid, Sonic running through: at most `window` conditions; live tasks follow
   the game's 400-unit range; heap and task pool back to their start values at the end.
2. GONE: a collected ring raises GONE once; `sa1_set_state(present 0)` in a second world frees its live task silently
   (no SE event, rings unchanged there).
3. Respawn: present again -> the task returns when in range.
4. Ring group bits: two rings of a group taken in world 1, forced into world 2: the group spawns without them.
5. Two worlds, the same records: each world's results equal a run of that world alone.
6. Determinism: cases 1-5 on x86 and x64, twice in a row: identical events and world RAM (segment 0x18 compared
   separately).
7. Followers: a follower world's ring touch counts there; the host's credit reaches the leader.
8. Actor and ATTACKER: a Kiki owned by world 1, an ENEMY proxy of it in world 2; a HIT in world 2 -> `sa1_set_hit` ->
   the Kiki dies by its own code in world 1 (GONE).
9. Render: draws attributed to records, rigid models as instances, world-space interpolation (the character turning
   does not move a ring's matrix).
10. Capacity: without 4 KB of heap headroom the window stops adding (CAPACITY event), no fault.

### 7.3 G-SA1

The tests of 6, then the owner's in-game checklist: spawn every type from the tab, undo, dupe and paste, save and load,
edit properties; Sonic collects a ring line (the HUD counts), breaks item boxes of every content, is launched by
springs, dash panels, dash rings and jump panels in order, rides a rocket, touches a checkpoint, presses a switch; a
Tails follower collects rings for Sonic; with two players a ring taken by one disappears for the other; respawn and
reset work.

---

## 8. Phasing and effort

| Phase | Content | Needs | Exit | Effort |
|---|---|---|---|---|
| S0 (queued rounds) | round 5 G-SA1 (host objects in GMod), the 2D HUD (ring / lives counter), `sa1_character_set_view` | - | rounds plan | in the current queue |
| S1 SET core | SADC SET-0; harness `--set`, `.set.bin`, `set_ring_ec1`; LIB SET layer (API, world side, events, forced state), generated list and its tool, render attribution, instancing, `sa1_model_mesh` | S0's view | `set_ring_ec1` strict; `set_test.c` 1-7, 9, 10 | one round: a decomp agent, a harness agent, 2-3 LIB agents, a verifier |
| S2 common objects | SADC OBJ-1..3 (about 29 KB, about 145 functions) staged; references `set_items_ec1`, `set_springs_ec1`, `set_ec1_common` | S1 | the references strict in libsa1 | one decomp wave (6-8 batches of 20-25 functions) and a replay agent |
| S3 G-SA1 tab | feature `objects`: catalogue, entities, tab, icons, puppets, instanced drawing, properties, `gsa1_place` / `gsa1_link`, server authority, respawn and reset, saves / dupes / undo / cleanup, the Options page | S1 (S2 for the types) | 7.3 in single player | one round: 2-3 G-SA1 agents and a verifier; **the first owner release with game objects** |
| S4 actors, badniks, multiplayer | ATTACKER, `sa1_set_own` and the pose stream, ownership and relay in Lua; BAD batches (first wave per `v1_enemies.md`); object sync with two clients | S3; G-SA1's remote-character puppets | `set_test.c` 8; badnik references; a two-client test | 2-3 decomp waves for the 12 badnik execs (68 KB) and one LIB / G-SA1 round |
| S5 arena worlds, stage packs | world rebuilds with a B_* or STGnn image; bosses (`v1_enemies.md`); stage types | S4 | per boss / per stage | per boss / per stage |

Triggers, cameras and splines (`v1_triggers.md`) and NPCs (`v1_npcs.md`) share the base entity, the catalogue and the
tab (classes HOST and ACTOR) and can run alongside S3 / S4.

---

## 9. Owner questions (each with the default this design uses)

* **Q1** Do collected rings and item boxes come back? Default: never (SA1), with a per-object "respawn after" property,
  a server default (`gsa1_sv_obj_respawn` 0) and a "Reset G-SA1 objects" button.
* **Q2** Two characters touching the same ring at the same moment may both get it (no rollback). Default: accepted.
* **Q3** Plain GMod players (no character): launched by springs and dash panels (a GMod-side imitation, not the game's
  code)? Can they collect rings? Default: off for both (`gsa1_sv_obj_plain_players` 0); one switch turns the launchers on.
* **Q4** Should a save remember what was collected? Default: no (a save is a layout).
* **Q5** Stage-specific objects (e.g. trampolines, which are stage data): a "stage pack" per map, one stage at a time,
  with Super Sonic's transformation unavailable while a pack is loaded (the same memory slot)? Default: 1.0 has the
  common objects only; packs in 1.x.
* **Q6** Icons drawn from your disc the first time the tab opens, kept in `garrysmod/data/gsa1/icons` on your machine
  only? Default: yes.
* **Q7** The tab is "G-SA1" at the top of the Q menu, and the same objects also appear under Entities > G-SA1. Default:
  both.
* **Q8** Do GMod weapons kill badniks (with their own death: explosion, animal)? Default: yes (also in `v1_enemies.md`).
* **Q9** SA1 has separate layouts per character (Knuckles' hint monitors, Big's fishing spots). Default: a placed object
  exists for every character, with a "Characters..." filter.

---

## 10. Risks and UNVERIFIED points

| Risk | Mitigation |
|---|---|
| The worlds cull with a virtual view: objects GMod shows may not be drawn, and visibility-gated object logic changes | the host camera into the worlds (5.2, 5.3); puppets for records that are not drawn; replay the character references with a host view before switching |
| Decompilation volume (core about 32 KB, badniks 68 KB; rockets and the switch have large closures; Amy's goal balloon pulls the stage-clear chain, 24 KB) | staged waves; leave types whose closure is out of proportion (the goal balloon) out of 1.0 |
| Lua drawing cost with many objects | instancing is part of S1 (5.3); measure 500 rings on the owner's machine |
| Edicts and join traffic with one entity per object | ring lines and circles as one entity; measure a join with 2,000 objects |
| Multiplayer puppets are not built | object sync runs through the server path in single player from S3; two-client tests wait for the puppets (S4) |
| STATIC objects animate only in the activating world (a spring's compression on other clients) | accepted; a visual "poke" event is possible later |
| Stage-dependent object behaviour (`GetStageNumber()`: animals, level tables) | the virtual stage (5.2); per-type notes in `v1_objects.md` |
| Overlay slot conflicts (B_CHAOS7, stage packs, bosses) | S5; one image per slot per world |
| Capacities (1,023 conditions, CCL lists, the heap) | windows bounded by the heap headroom rule; CAPACITY events |
| Generated list vs stage lists (per-stage differences in im / flags / distance) | `gen_objlist.py`'s report; one named reference entry per type |

UNVERIFIED: the native CPU cost of objects (estimated from the interpreter's ratio); the collision group that keeps a
ring entity pickable but walk-through; whether classes registered from a feature file appear in the Entities tab like
files in `lua/entities`; AdvDupe2's handling of NetworkVars; the switch-flag storage; the goal capsule's code location
(`v1_objects.md`); GMod's edict limit on the x64 branch; whether a host view changes anything the character references
record.

---

## Appendix A: addresses

| What | Address |
|---|---|
| Object list table / SET data table | `D_8C1C143C` / `D_8C1C0EDC` (`[level * 8 + act]`) |
| Current list / SET data / count / conditions | `B_8C79A3CC` / `B_8C79A3D0`-`B_8C79A3D8` / `B_8C79A3DC` / `B_8C79A3E0` (1,024 x 0x10) |
| Level / act bytes of the manager | `B_8C79E3D0` / `B_8C79E3D1` |
| First-pass and act-change flags | `D_8C1C0ED8`; `D_8C1C0ED0` / `D_8C1C0ED4` |
| Manager task exec / init / step / act change | 8C090FA0 / 8C091016 / 8C0915A2 / 8C0915D2 |
| Spawner / range test / point test | 8C091148, 8C091380 / 8C0910C0 / 8C09111A |
| Range-out / its body / camera-only test | 8C0919CC / 8C0919E8 / 8C091912 |
| Gone / gone without destroy / user flag set / get | 8C091BA4 / 8C091B8A / 8C091C00 / 8C091C10 |
| Clear / fill / compaction / reset | 8C0916AE / 8C0916EE / 8C09174E, 8C0917E0 / 8C091890 |
| Ring exec / ring add / scatter | 8C0869E8 / 8C08A198 / 8C086F5E, 8C0870EA |
| Spring request / SPRINGB request | 8C065A98 -> 8C07C720 / 8C07C768 |
| Item grants | 8C07D3A0, 8C07D43E, 8C07D3DC, 8C07D4A0 |
| Visibility test | 8C038D00 |
| Emerald Coast act 1 list | STG01 `0x0CAA8F1C` (84 types) |

## Appendix B: reproducing the measurements

```
cd SADC/build/research/v1_arch
py -3.12 status.py 1ST_READ 8C090EC0 8C091C20 8C059A80 8C06C0C0          # per-function status, range sums
py -3.12 objlist.py STG01 0CAA8F1C ../../../disc/SONICADV/SET0100S.BIN   # EC1's list against its SET file
py -3.12 union2.py                                                         # closures of the 1.0 sets
SADC_ROOT=<SADC> ./sa1h_v1arch.exe trace --level 1 --act 0 --chr 0 \
    --input @<SADC>/harness/tests/sim/obj_ec1_sonic.txt --out ec1obj --dump-ram-at 0,60,200,400,600,end
                                                                           # and --no-objects for the comparison
py -3.12 ramstat.py ec1obj.step0.ram ec1obj.step200.ram ec1obj.step600.ram  # tasks, conditions, heap
```

---

## Review (adversarial)

Reviewer pass, 2026-10-21, read-only (nothing in SADC/src, LIB or g-sa1 changed). Method: every load-bearing claim
checked against the asm (`asm/1st_read/text/8C090FA0.src`, the whole SET manager), the decompiled C, Ghidra
(`build/ghidra/1ST_READ/functions.json`, `decomp/`), the xrefs index, LIB / G-SA1 sources, the GMod Lua files and the
owner's disc; the harness runs of 1.2 were repeated and extended. Scratch: `SADC/build/research/v1_arch_review/`
(`thru.py`, `thru2.py` closures that walk through C; `mkset.py`, `mkset2.py` filtered EC1 layouts; `files.txt` the files
the EC1 run opens; `discf/` a 16 MB copy of exactly those disc files, used with the harness `--disc` option, local only,
safe to delete; `m_*.csv/.ram`, `rev_ec1obj.*` the runs).

### R.1 Verified

* **SET manager range and status**: 8C090EC0-8C091C20 = 31 xrefs entries, 5 matched (8C091B5E, 8C091B8A, 8C091BA4,
  8C091C00, 8C091C10; 96 B), 26 todo (re-run of `status.py`).
* **The manager task**: the stage start creates it with `CreateElementalTask(2, 8, 8C090FA0)` (8C04D334-8C04D33A in
  `8C04C680.src`, 8C04E58C-8C04E590 in `8C04D920.src`); level 8 is linked into list 7 (`code_8C098444.c:218-232`), and
  8C0988A4 runs list 7 first (`code_8C09878C.c:108-117`). Re-run census of the EC1 dumps: 8C090FA0 is the only list-7
  task at steps 60, 200 and 600.
* **Condition array and fill**: `B_8C79A3E0` 1,024 x 0x10, count `B_8C79A3DC` (MOV.W). 8C0916EE caps the total at
  0x3FF and, as the doc says ("0x3FF + 1"), fills one entry more than it counts (loop from count to R7 inclusive, then
  count = R7), so 1,023 records are usable.
* **List entry and spawner** (decoded constants): default squared distance 160,000 (0x481C4000); flag 1 own distance
  at +4; flag 2 = -100.0 then `unionStatus.f = -100 + 100 = 0`, which 8C0919E8 treats as "never range out"; flag 4 =
  spawn while `D_8C1C0ED8 == 0` (first pass); `unionStatus.f` = squared distance + 100.0; player-1 radius 1,600.0;
  no-ocp range-out 168,100.0 (410^2). Copies: pos +8, ang from the three Sint16 at +2/+4/+6, scl from +0x14,
  `twp->counter` = entry +8 (only when the task has a taskwk), `tp->ocp`, scCount++, bit 0. Moved block = 0x24 bytes
  {ang +0, pos +12, scl +24}, MFree'd by the spawner, bit 1 cleared.
* **Range-out**: 8C0919CC is 28 B with 81 callers; 8C0919E8 returns 0 for condition bit 8 or distance 0, else tests
  camera_twp, player 0 and (40 units) player 1, then `exec = DestroyTask`. LIB `gen/policy/stubs.tsv:139` is `ret 0`
  (its note: 269 range-outs in the obj_* scripts).
* **Gone / user bits**: 8C091BA4 (matched, 23 callers) clears 0x8001 and sets `exec = DestroyTask`; 8C091B8A clears
  the bits only; O_GRING keeps its per-ring bits in scUserFlag through 8C091C00 / 8C091C10.
* **Disc**: 156 SET files, 43,091 records, largest `SET1202E.BIN` with 1,003 (32,128 B = 32 + 32 x 1,003); every file
  is exactly 32 + 32n bytes. (The 36 % action-stage share was not re-measured; `v1_objects.md` gets 38.7 % of all
  resolved placements with another denominator.)
* **EC1 list**: STG01 `0x0CAA8F1C`, 84 types, 1ST_READ types 0-31, 77, 82, 83; 289 1ST_READ records in
  `SET0100S.BIN`; every im / list / flags / distance in the 1.3 table matches `objlist.py`.
* **1.2 measurement**: re-run gives exactly 599,484 vs 413,269 instructions per step; tasks 14 / 108 / 143 / 134,
  records 788 (785 present after step 200), live 53 / 82 / 42, heap free 391,424 / 335,280 / 358,704.
* **Overlay slot**: 31 images load at 0x0C900000 (the list in 1.3 omits ADV01OBJ but the count is right).
* **Ring and spring facts**: ring CCL id 7, `twp->id = 11`, 0x16C BAMS turn per step, the GetPlayerNumber() == 2
  credit rule, the display's `8C038D00(pos, 20.0)` test (`code_8C0869E0.c:108-141, 197`); spring
  `CCL_Init(tp, D_8C1BA884, 4, 1)` with colliwk flag 0x800 (Ghidra `8C065800.c`).
* **LIB / G-SA1**: 16 MB commit (`sa1_world.c:891`), the virtual view (`sa1_world_tick.c:83-120`), host objects
  (registry 1,024, exec 0x7F000100, 4,096-B heap headroom, kinds 1-6, so 7 is free for ATTACKER); the G-SA1 module is
  client-only, `sh_net.lua` says multiplayer is not done, `feat/` holds the seam, no `cl_interact.lua` yet.
* **GMod**: `spawnmenu.lua:182` (AddCreationTab), `entities.lua:22-38`, `g64_spawnmenu.lua:501`, `commands.lua:777`
  (Spawn_SENT) and `:892` (gm_spawnsent), G64's coin (client detects, removes locally, `net.SendToServer`).
* **One UNVERIFIED point resolved**: `scripted_ents.Register` puts every class with `Spawnable = true` into the
  `SpawnableEntities` list wherever it is registered (`scripted_ents.lua:53-139`), so generated classes do appear under
  Entities > G-SA1 (they must be registered in both realms at load time). The Entities tab builds icons through
  `ScriptedEntityType` / `IconOverride` (`entities.lua:7-14`): set `ScriptedEntityType = "gsa1_object"` so both tabs
  share the disc-rendered icons.
* **Harness hooks**: `--no-objects` serves empty SET files (`harness/src/t_sim.c:394-406`); `--disc DIR` exists
  (used for R.4).

### R.2 Refuted or corrected

1. **Which spawner runs (1.1, Appendix A).** 8C0915A2 (mode 1, asm lines 851-878) calls **8C091380 whenever
   `playertwp[1]` is NULL**, or `D_8C19B468 == 1`, or `playertwp[1]->counter.b[1]` (twp +9) == 2; it calls 8C091148
   only when a player 1 exists. So every single-character G-SA1 world runs 8C091380: a player-0-only test (8C09111A),
   no 40-unit player-1 test. 8C091380 centres on `camera_twp` (`D_8C18B7EC`) instead of player 0 when there is no
   player 0, when `D_8C19B468 == 1`, in level 42 (stage & 0xFF00 == 0x2A00), and when the main character is Big
   (GetPlayerNumber() == 7) with the fishing flag `B_8C7A40E8 & 0x40`. 8C091148 (player 0 at range, player 1 within
   40 units) is the follower-world path. Both are in SET-0, so the batch is unchanged; the text is wrong.
2. **The attach call (5.2) would never run the manager.** `CreateElementalTask(2, 7, 0x8C090FA0)` is wrong:
   func_8C0985EC links level 8 into list 7 and **links level 7 into no list** (level 7 is for CreateChildTask). The
   game passes level 8.
3. **Item boxes do not use bit 15.** The ground item box (type 13; 441 + 450 placements) never calls 8C091BA4 (not
   among its 23 callers; ItemBoxAir is). On spawn it sets 0x0010 (8C091B5E). If 0x0004 is set (8C091B68) it starts
   in the broken-shell mode 3. When broken, 8C05D4CE sets 0x0004 (8C091B54), grants the item once and switches to the
   shell with CCL id 4 (`D_8C1B9B28`). Bit 15 stays set, so the shell respawns (Ghidra `8C05D10C.c:128-152`,
   `8C05D4CE.c`). O_Save_Point and CMN_KUSA (plus 8C06546E) use 0x0004 the same way. **The bit list of 1.1 omits
   0x0004.** The GONE event (bit-15 diff) therefore never fires for ground item boxes, the server never hears of them,
   and other worlds keep them intact.
4. **Ground item box CCL**: it registers id 2 (`D_8C1B9AC0`) like the air box, then id 4 as a shell. Minor.
5. **DestroyTask is 8C09865C** (FreeTask 8C098786); 8C098752 is an instruction inside DestroyTask.
6. **SET-0 is 27 functions, about 3.1 KB.** Ghidra lists 32 functions in the range. 8C0915D2 (mode 2, 128 B of code,
   220 B with its pool) has no xrefs entry because it lies inside 8C090FA0's 1,806-byte span (8C090FA0-8C0916AE), and
   the doc's 2.9 KB leaves it out: 4,564 - 1,806 + 118 + 220 = 3,096 B. 8C090F94 / 8C090F98 (4 B each) are padding
   pseudo-functions. The batch tooling works per xrefs entry, so the 8C090FA0 entry must be split into its seven
   functions first.
7. **The closures are not upper bounds.** `closure.py` follows calls and code-pool function pointers only. It misses
   function pointers in data tables. The ground item box's contents are a data table `D_8C1B9BA0` of {icon, function}
   pairs: 8C05D764, 8C05D884, 8C05D7A6, 8C05D7D6, 8C05D806, 8C05D900, 8C05D854, 8C05D930, 8C05D964, plus 8C05E582
   below them. That is **10 todo functions / 508 B missing from OBJ-1**; without them a box breaks but grants
   nothing. Walking through C as well (`thru.py`) adds 8C05DC60 (304 B, todo): the air item box's display. It is a
   `void` stub in LIB (`stubs.tsv`), so the air box that "already has C" is invisible in libsa1 today.
8. **"Ring lines 8C061C40 / 8C062020"** are O_LINERING / O_LINERING_V, ring lines gated by a switch id (8C067D84;
   `v1_objects.md` 238-240). Plain lines and circles are O_GRING (`scl.z` 0 = line, 1 = circle), which already has C.
   **One O_GRING record holds at most 8 rings**: bit `1 << i` in a Sint8 scUserFlag, `scl.x + 1` rings
   (`code_8C087B80.c:56-90`). A ring-line entity with more rings must be several records.
9. **S0 "sa1_character_set_view ... in the current queue"**: `gsa1_rounds_plan.md` has no such item. It exists only as
   S1d. And 1.5 "the camera adapter's position is whatever the world holds" is too soft. Live worlds copy
   `w->camera.pos` into camera_twp before every step (`sa1_world_tick.c:52-65`), and nothing ever assigns it (only
   `ang[1]`: `sa1_api_world.c:545`, `sa1_world_api.c:193`), so **camera_twp->pos is (0, 0, 0) in every live world**.
10. **0.7 KB per object** is the EC1 average, dominated by STG01 overlay tasks (func_0C908FA6 x25 and func_0C902D08
    x27 in the census). For the 1.0 core set it measures about 0.34 KB per live object (R.4).
11. **Q1 "never (SA1)" misstates SA1.** The game's rule is 8C091890, called by the per-act object init 8C058E48 from
    the stage init 8C04E660. It sets every condition **without 0x0010** back to 0x8000 (present, scUserFlag 0). On a
    (re)start, rings, ring groups and enemies come back. Item boxes, air item boxes and checkpoints set 0x0010 and
    keep their state. Whether the death restart re-runs 8C04E660 is UNVERIFIED. The static graph (8C04E660 <-
    8C04CA40, 8C04DA20) suggests it does; check it with a harness death trace.
12. **Trampolines**: v1_arch is right and `v1_objects.md` ("no trampoline in the final game") is wrong. STG02's
    **O POLINE** (list type 47, exec 0C9098E6, cluster 0C909880-0C909F40, about 1.7 KB todo, 8 functions) draws
    `STG02_Trampoline` 0x0C9C8C98 (from the pool of 0C909E20), and Windy Valley places it twice. Name it in 2.7.
### R.3 Gaps (missing objects, dependencies, risks)

* **G1. The per-act object-system init is missing.** 8C058E48 (todo, 120 B) runs at stage start. It does the following:
  * creates a level-3 task 8C058F62 / 8C058F20 with a `CAlloc(1, 0x34)` block, held in `B_8C78C254` / `B_8C78C258`;
  * resets the conditions (8C091890);
  * initialises the jump-panel list (8C05E680), the rocket state (8C0635C0) and the capsule pointer (8C062634);
  * runs 8C05B1DA;
  * clears the 32 switch flags (8C067DF4);
  * sets up the item popup (8C05E0CE).

  That is 10 functions and about 0.44 KB (8C091890 included), all todo. It is not in SET-0 or in the 5.2 attach, and LIB's boot does not run
  it (`v1_objects.md` line 137 says the same). Without it, jump panels, rockets, switch-gated objects and the capsule
  read uninitialised globals.
* **G2. The hand-made mode 0 must do everything the game's mode 0 does.** Besides `B_8C79A3CC` and the count, it must
  clear `B_8C79A3D0` and `B_8C79E3D0` / `D1`, set `D_8C1C0ED0 = 1`, and clear `D_8C1C0ED4` and `D_8C1C0ED8`. Flag-4
  types spawn on the first pass only while `D_8C1C0ED8 == 0`. A simpler approach that keeps more of the game's own
  code: point the world's `D_8C1C143C[level * 8 + act]` and `D_8C1C0EDC[...]` (1ST_READ data, so per world) at the
  generated list and at a SET block in segment 0x18 with header count 0, then let 8C091016 run. Mode 2 is then safe
  as well. Otherwise an act change (8C0918E6 -> `D_8C1C0ED4`; today a fatal stub) reloads the stage table's list,
  which is the STG01 address 0x0CAA8F1C for stage 0x0100, and appends that act's SET file.
* **G3. The shared state must include 0x0004, and 0x0010 for the reset rule.** Add an event for 0x0004 becoming set
  (item box broken, checkpoint passed). A forced state on a **live** task must also FreeTask it, so the spawner
  re-creates it in the forced state. Otherwise:
  * a live O_GRING with forced user bits still has the taken rings as child tasks, and they can be collected twice;
  * a live item box with forced 0x0004 stays intact.

  Test case 4 checks only the spawn.
* **G4. TOGGLE is keyed by switch id, not by entity.** Each world keeps 32 switch flags in `B_8C78C5B4[32]` (cleared by
  8C067DF4). O_LINERING and rockets HS / VS read them through 8C067D84. Merge by id after each pair, as in
  `v1_objects.md` 537-539.
* **G5. The window is larger than the CCL list.** The default window of 512 is above the id-7 list size of 256.
  Rings, ring-group children and up to 20 scattered rings share that list, so in a ring-dense layout some rings
  cannot be collected and nothing reports it. Budget the window per CCL id (about 200 live rings), and make test 1
  assert that every live ring registered.
* **G6. Follower worlds.** 8C091148 centres on player 0, which is the proxy leader (full range), and adds only 40 units
  around the follower. Range-out tests player 1 only while `counter.b[1] == 0`. The window must mirror this: the leader
  proxy at range + margin, and the follower at 40 + margin. "Centred on the follower" (2.6) does not.
* **G7. Order of S1c and S1d.** Once the `ret 0` stub goes, a camera_twp at (0, 0, 0) (R.2-9) keeps every object
  within its range of the SA1 origin alive, and Big's fishing (8C091380) spawns objects around the origin. Either
  S1c writes player 0's position (or the virtual view's eye) into `camera.pos` as an interim, or the stub stays until
  S1d.
* **G8. Virtual-stage hazards.**
  * O_ITEMBOX calls overlay code at 0x0C907998 when GetStageNumber() == 0x0302 (Twinkle Park act 2). This is the
    only overlay reference in the whole core closure, so 0x0302 must never be configurable without STG03.
  * Super Sonic worlds boot at level 19 (`super_sonic.md`), so "0x0100 in every world" conflicts. The stage number
    has to be per world, and objects must be checked at level 19.
* **G9. Stage-end capsule.** The owner asked for it. `v1_objects.md` names O_RELEASE 8C062640 (cluster
  8C062620-8C063100, 26 functions, 2,142 B, 1ST_READ), but OBJ-1..3 and the S3 release list leave it out. Its clear
  flow (timer stop 8C08A5B6, pad lock 8C0509DA(0), clear request) also needs a G-SA1 policy for what happens after a
  clear.
* **G10. set_ec1_common cannot replay strictly after S2.** 19 of its 289 records have no C before S4: E SARU x9,
  E SAITO x2, OTTOTTO x7 and O IRONB x1. Use the 270-record filter (types 4, 25, 29-31 removed) for S2 and the
  289-record one for S4. OTTOTTO itself (199 action-stage placements, 298 B) has no class.
* **G11. Checkpoints.** A PASSED event alone does not respawn anyone at the checkpoint. G-SA1's restart
  (`SA1_EVENT_RESTART`, `gsa1_070_drown.md`) should use the restart point that the checkpoint stored in its world.
  S3 does not include this.
* **G12. HOST class vs `v1_triggers.md`.**
  * Camera triggers are CAM entries run by the game's camera code (8C019360-8C025960, 0 % decompiled, 92 functions /
    26.8 KB needed).
  * Splines are the game's path code, which already has C.

  HOST should mean "the host places data only", and the camera decompilation is a dependency that the phasing table
  does not list.
* **G13. Badnik assets.** The EC1 run loads badnik textures from per-stage files (`E_SARU.PRS`, `E_SAI.PRS`,
  `E_AMENBO.PRS`, `E_BOMB.PRS`, `TOGEBALL_TOGEBALL.PRS`), not from OBJ_REGULAR. S4 needs per-type asset loading into
  each world.
* **G14. The actor hand-off must not copy live scl.** 8C091A92 is the game's only writer of the moved block (it has no
  1ST_READ caller), and it copies the live `twp->scl`. Many objects reuse scl as work after mode 0: the ring
  overwrites all three at init. The hand-off must write the SET parameters into `block.scl` and take only pos / ang
  from the stream.
* **G15. Host-side imitations.** The puppet ring turn (0x16C BAMS per step), the 3.5 launchers and the "respawn after"
  timer are host imitations of game behaviour. Under the owner rule they are cosmetics or options, and should be
  labelled that way in the Options page.
* **G16. Single-world items.** The bomb item (8C05D930 -> 8C05E582) and invincibility affect only enemies that are
  live in the collecting world. With actors owned elsewhere, these effects need the ATTACKER relay too.

### R.4 Measurements added (harness, EC1 act 1, Sonic, `obj_ec1_sonic.txt`, 694 steps, `--disc discf` with filtered `SET0100S.BIN`)

| Layout | Records | SH-4 instructions / step | Added |
|---|---|---|---|
| none | 0 | 413,269 | - |
| WALL + C_* (types 20, 22-24) | 112 | 422,656 | +9,387 (+2.3 %) |
| rings + O_GRING (0, 21) | 104 | 434,452 | +21,183 (+5.1 %) |
| **1.0 core** (1ST_READ minus 4, 25, 29-31) | 270 | 449,737 | +36,468 (**+8.8 %**) |
| core + O IRONB / + OTTOTTO | 271 / 277 | 449,813 / 450,808 | +76 / +1,071 over core |
| core + E SARU (Kiki x9) | 279 | 496,297 | +46,560 over core |
| core + E SAITO (x2) | 272 | 507,670 | +57,933 over core |
| all 1ST_READ types | 289 | 582,397 | +169,128 (+40.9 %) |
| full layout (+499 STG01 records) | 788 | 599,484 | +186,215 (+45.1 %) |

Heap at step 200: none 374,928 B free; core 370,464 (13 live SET tasks, about 0.34 KB each with children); all
1ST_READ 366,400 (17 live).

* **The 1.0 set is cheap**: about 9 % of the step.
* **Eleven badnik records cost about 70 % of the full layout's object cost**, and the 499 overlay records only about
  9 %. A Rhinotank (E SAITO) costs roughly 29K instructions per step while it is in range. S4 needs a per-world actor
  budget.

### R.5 Required changes (to this design and its work items)

1. 5.2: create the manager with `CreateElementalTask(2, 8, 0x8C090FA0)`, or better, the G2 table approach (the game's
   own 8C091016 and mode 2). Fix 1.1 and Appendix A on 8C091380 vs 8C091148 (R.2-1).
2. Make 0x0004 part of the shared state, and add its event. A forced state on a live task always means FreeTask and
   respawn. Q1's SA1 default is the 8C091890 rule (R.2-11): offer it as the "SA1 rules" option (on the owner's
   RESTART: everything without 0x0010 comes back).
3. Add to SET-0: 8C0915D2 (split 8C090FA0's xrefs entry first) and the object-system init 8C058E48 with its other
   callees (9 new functions, about 0.35 KB; 8C091890 is already in SET-0), and run that init in the attach (G1).
   Re-size SET-0 to about 36 functions / 3.45 KB.
4. Add to OBJ-1: the nine item-content functions plus 8C05E582 (R.2-7) and the air box display 8C05DC60. Move
   O_LINERING to OBJ-3 or 1.1 (switch-gated), and cap ring entities at 8 rings per O_GRING record (R.2-8). Add
   O_RELEASE (the capsule, G9) to OBJ-3. Re-run every closure with data-table pointers included: scan the tables an
   object's code references for code addresses.
5. Window: budget per CCL id (G5), follower rule (G6), and camera position before the stub goes (G7).
6. Key TOGGLE by switch id, via `B_8C78C5B4[32]` (G4).
7. Make the virtual stage per world. Forbid 0x0302 without STG03, and give Super Sonic worlds level 19 (G8).
8. References: use 270 records for `set_ec1_common` in S2 (G10). Add a death or restart trace to settle R.2-11.
9. Replace the S0 claim with "set_view = S1d, new work". Reword HOST and add the camera decompilation as a trigger
   dependency (G12). Add checkpoint restart points (G11), per-type assets for S4 (G13) and SET parameters in the
   hand-off block (G14).
10. Replace the 0.7 KB / 45 % figures with R.4's breakdown in 1.2, 2.3 and 5.6.

### R.6 Effort realism

* **S1** (one round) is plausible, but tight once G1-G7 are added.
* **S2**: about 145 + 10 + 1 + 26 (O_RELEASE) = 182 functions, roughly 32 KB. That is 8-9 batches of 20-25
  functions, not 6-8.
* **S4**: 319 functions by this doc's closure (`v1_enemies.md`: 58,424 B in 6 TUs). That is 13-16 batches, so "2-3
  waves" holds.
* **S3**: one G-SA1 round is realistic for the code, but most of 7.3 can only be checked in game by the owner. Plan
  for a fix round after the owner's test.

**Verdict:** the architecture holds up: mirrored SET objects run by the game's own manager in each character's world,
the server as authority over the persistent condition bits, actors with one owner, arena worlds later. The measured
facts reproduce exactly. Several load-bearing details are wrong or missing:

* the task level (8, not 7);
* which spawner runs in single-character worlds;
* bit 0x0004 (ground item boxes and checkpoints never clear bit 15);
* the per-act object init 8C058E48;
* the zero camera position in live worlds;
* closures that miss data-table functions (item contents);
* a reference that cannot replay strictly in S2;
* the omitted stage-end capsule.

All of them can be fixed inside this architecture. None of them forces another choice of design A.
