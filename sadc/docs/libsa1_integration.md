# libsa1 integration design: running the decompiled game C inside libsa1

Status: design, 2026-10-19; implemented through Phase 7 (2026-10-20: the ten reference traces replay strictly;
"Status 2026-10-20" below: what is done per phase, the results, the overlay images and stage tasks as built,
measured facts, the design text corrected since). Supersedes the open "how" parts of
`docs/libsa1_plan.md` sections 1 and 4 and, for libsa1 only, principle 2 of `docs/portability.md` (owner
sign-off: D1, 2026-10-20).

Inputs: six read-only surveys (data, runtime, collision, tasks, libsa1, missing code), three competing
proposals (NATIVE-STRUCTS, GUEST-ARENA, PRAGMATIC) and three judges. This document is the winner,
GUEST-ARENA (GA), with the grafts the judges asked for and the corrections they found.

Roots used below: `SADC` = `<project>\sadc`, `LIB` = `<project>\libsa1`.
All addresses are 1ST_READ US 1.005 unless an overlay is named.

---

## Status 2026-10-20

**All ten reference traces replay strictly bit-exact.** 64-bit and 32-bit libsa1 replay the five Sonic Emerald
Coast act 1 traces (ec1_stand 180, ec1_run_jump 271, ec1_turns 480, ec1_jump_hold 322, ec1_spindash 242 records)
and the Phase 7 traces of the other characters (tails_sh1 414, knux_rm3 319, amy_tp2 381, gamma_fe1 363, big_ec3
392) through the public API identical to the reference simulator in every decoded field and every raw twp / mwp /
pwp / csts blob (pointers by physical address, heap pointers by structure), in V6 and V5, in FAITHFUL mode (each
stage's landtable dump) and in DISC mode (the stage's own landtable, now for every stage of the traces), which write
identical records; every `sa1trace.py diff --ulps 0` reports identical, and x86_64-windows and x86-windows write
byte-identical outputs. `LIB/tests/build_replay.py` is strict by default and `LIB/build.py --test` gates on all ten.
What closed the residuals of the M1 days (the land_count / rand_state of the stage tasks, knux_rm3's lclop, the
particle draws of big_ec3 and ec1_spindash, Big's lure): the overlay images and the stage tasks below, the particle
system of the stage start in the boot, and the decompilations of 2026-10-20 (STG01 ocean and footprints, STG05 level
task and act setup, the particle spawners and allocator, E-102 effects, power-ups; Big's lure reset 8C0E91A0 got a
TARGET_PC body). How to run every test: `LIB/README.md` "Tests" (in short, from LIB with `SA1_TEST_GDI` set:
`py -3.12 build.py --target all --test`).

### What is done, per phase (11)

| Phase | State | Evidence |
|---|---|---|
| 0 | done | Owner decisions D1, D2, D4-D7, D13, D14 signed off (1.4). V0: the 15 scripts (the 10 samples and the 5 `obj_*`) identical with every native function routed; route_matrix: only the register-convention helper 8C02D410 cannot be routed alone. Landtable manager 8C0519C0-8C052FA0, GetStageNumber, GetPlayerNumber and 8C09068E have C. `tools/pc_cflags.txt`, `include/pc/sh4_fpmodel.h`, the FSCA dump `LIB/src/rt/rt_fsca_q.c`. Harness: trace v2 (1,188-byte records; the v1 files are kept as `*.v1.bin`), `--dump-land` v2, `--dump-ram-at`, `--log-sdk`, `--dump-world`, `--census`; the references re-recorded as v2 |
| 1 | done | `LIB/tests/spike_test.c`: the exit sequence and 19 more calls byte-identical to the interpreter over the whole arena; two interleaved worlds |
| 2 | done | 2.3 "Phase 2 status"; since 2026-10-20 `pclower --all` lowers 530 TUs: `src/1st_read`, every overlay image of `src/overlays` (STG01, STG05, TIKAL_PROG, each with its own code map: "Overlay images" below) and `LIB/src/rt`, 0 errors on 3 targets; `--selftest` passes. V1 (the harness GA build) last ran before; `harness/build.py --ga` now stops at the Windows command-line limit (512 TU paths of `src/1st_read`, WinError 206): pclower accepts `@FILE` response files, the harness has to pass one |
| 3 | 3a done, 3b done | `LIB/src/rt/README.md`: every rt function bit-exact against the SDK (`sa1h diff --ulps 0` and the dedicated generators), `regress.py --route` with `--rt` identical on 15/15; the draws and the visibility test of the display capture are in `rt_draw.c`; the SDK truncation 8C63A994 (the ocean's grid snap) is in `rt_libm.c` (`sa1h diff --ulps 0`: exact on 20,000 inputs cold and warm) |
| 4 | done | M0, M1a, M1b below (`LIB/tests/world_boot_test.c`, `transplant_test.c`) |
| 5 | done, M1 strict | `LIB/src/sa1_coladapt.c` (FAITHFUL synthesis into the collision segment, DISC images: STG01-STG10 known), `sa1_collision_set_static_ex`; `LIB/tests/replay_test.c`, `coladapt_test.c` |
| 6 | in progress (another work stream) | API 0.2 draft: `sa1_character_create_ex` (+ `SA1_CREATE_STAGE_TASKS`), `sa1_character_state_ex`, `sa1_poll_events`, `sa1_input.camera_yaw` and the camera adapter, sound / voice / jingle / rumble / death / respawn triggers as events; the display-pass capture (`LIB/src/sa1_capture.c`, `tests/capture_test.c`), GRID synthesis, moving-object proxies and kill planes (`LIB/src/world/sa1_coladapt_world.c`, `tests/grid_test.c`) |
| 7 | done | Tails, Knuckles, Amy, E-102 and Big replay their reference traces strictly (above); each world is built with the stage's own tasks |
| 8 | started | TIKAL_PROG has C (`src/overlays/tikal_prog`, lowered with the game in its own image). Since 2026-10-21 B_CHAOS7's Super Sonic task code has C too (`src/overlays/b_chaos7`, 25 functions, lowered as image B_CHAOS7; below). Tikal's create path and Super Sonic as a character (kind 8, round 6a) are open: both `SA1_ERR_UNSUPPORTED` |
| 9 | started (another work stream) | G-SA1 in `<project>\g-sa1` |

### M1 result against the exit criteria (11), all ten traces

| Criterion | Result |
|---|---|
| 1. V5 reseeded | every record of the 10 traces identical (decoded fields and raw blobs) |
| 2. V6 free run | the same; the checkpoints of 11 hold, ec1_stand step 0 `land_count` 18 as in the reference |
| 3. raw blobs, caches | raw blobs identical (pointers by physical address; pointers to synthesized landtable objects by COL index; heap pointers by structure). Steps 30 and 150 of ec1_run_jump: the colobj / colpoly caches (17 / 19 landtable objects with 791 / 984 polygons, plus the water plane as in the reference) and the static gather lists equal the reference RAM (`--dump-ram-at`) by structure |
| 4. DISC = FAITHFUL | 0 records differ, all 10 traces |
| 5. cross-target, two runs | x86_64-windows and x86-windows write byte-identical outputs (30 of 30 files); x86_64-linux is built (not run: no Linux host); a second create + replay in one process is byte-identical |
| 6. MXCSR, allocations, stubs | the host MXCSR is unchanged after every step; 0 context allocations inside steps and ticks; no fatal stub reached |
| 7. tick time | 0.010-0.037 ms per 60 Hz step on x64 (replay mode); the target was 0.2 ms per tick |

The earlier rungs, with `--stage-tolerant` (their boots have no stage tasks: the ocean and footprint residuals
accepted):

* **M0** (`world_boot_test`): Sonic's world image from the disc, libsa1's own boot with an EMPTY landtable,
  player 0 at (-9, 16, 4), ang.y 0x6000: ec1_stand steps 0-16 (17 records, the reference airborne) equal in
  every decoded field (`land_count`, `attr`, `last_attr` excluded); steps 17-18 differ, as expected without
  geometry (the reference touches the deck at step 17).
* **M1a** (`transplant_test --from 0` and `--from 1`, ec1_stand and ec1_run_jump): identical but for the
  stage residual; from step 0 the heap pointers are compared by structure (the frozen camera and stage tasks do
  not allocate their step-0 heap blocks). A transplanted RAM image has the base code map only.
* **M1b** (`world_boot_test`): libsa1's own boot with the DISC landtable 0x0C981554 and the death-zone task:
  ec1_stand and ec1_run_jump identical but for the stage residual, heap pointers by structure; a second boot is
  byte-identical; 60 live pairs move Sonic with the stick.

### Overlay images (2.5 as built)

Overlays that share a load slot (STGxx, B_CHAOS7, MINICART, SHOOTING at 0x0C900000; TIKAL_PROG over S_SBMOT) have
overlapping DC code addresses, and generic names (`objexec_O_POLE`) mean different addresses in different overlays.
So the code map is per image:

* `tools/pclower.py --all` lowers every `src/overlays/<dir>`. A TU's image is its directory (`STG01`); its names
  resolve against `build/<IMAGE>.symbols.txt` first; its lowered file is `<dir>__<file>.c`; the functions the image
  defines, and those its TUs reference at an address of the image's code range without a definition (image stubs,
  `STG05__func_0C9131AA`), get the C name `<IMAGE>__<name>` (`#define` lines after `sa1/guest.h`).
* `sa1g_fntab.c` has the base map (1ST_READ and the runtime: 1,842 entries) and one map per image (STG01 16, STG05
  22, TIKAL_PROG 17), listed in `sa1g_fnimages[]` with the PRS file, its SHA-1 (`config/overlays/<dir>.yaml`) and
  the code range. `sa1g_fn_lookup` / `sa1_fn` / `sa1_fn_name` try the images of `sa1g_fn_active[]` (by code range)
  first, then the base map. The harness keeps its single map (`harness/tools/gen_ga.py`: `gen_fntab(entries)`).
* The world (`LIB/src/world/sa1_world_image.c`) records the SHA-1 of every span's file and, when an image is
  applied, activates the maps whose PRS it holds with exactly that SHA-1 (a modified file accepted with
  `SA1_LOAD_ACCEPT_MODIFIED` gets no native code); `sa1g_world_enter` installs them like the segment table.
* A base TU's direct call into an overlay slot (1ST_READ calls 0C907998 of STG03, 0C900080 of SBOARD ...) is a base
  stub; `LIB/gen/gen_stubs.py` makes such a stub (8 today) call the running world's image first
  (`sa1g_fn_image_lookup`, same ABI signature or a code-map fault), else its policy.

### Stage tasks (6.2 as built: `SA1_CREATE_STAGE_TASKS`)

The stage start creates, before LoadPlayerTask, the level's list-2 task (`CreateElementalTask(2, 2,
D_8C19E0E0[level])`) and the level task (`B_8C752BD4 = CreateElementalTask(2, 0, D_8C19DE30[level])`, 8C04F506),
whose first exec (step 0, list 0, before the player) creates the stage's own tasks. `LIB/src/world/sa1_world_stage.c`
does the same at boot (`SA1G_BOOT_STAGE_TASKS`; the public `SA1_CREATE_STAGE_TASKS` makes the API compose the kind's
image with the stage overlay STGnn in every collision mode and adds the stage's death zone task):

* an exec the world's image map has runs as it is: Red Mountain's level task 0C911E0A (STG05, lowered); its act
  setup 0C9122EE creates in step 0 the kill collision 0C913028 (Knuckles only; DynamicCOL 0x1001 registered by the
  level task's children in step 1), the countdown 0C911DC0, the light task and the STG05 tasks without C, which are
  image stubs (`void`: `LIB/gen/policy/stubs.tsv`);
* an exec without C with a stand-in: Emerald Coast's level task 0C900000 (asm only) is created with that address and
  the executor runs the stand-in (an override): in its first exec it creates the ocean 0C909F20 (act 1: its
  DynamicCOL 0x88000002 water plane at y -1.5 follows the player in 25-unit steps) and the footprints O_ASIATO
  (every act: rand() per print, after the player) at the original's point and in its order, sets the ocean
  manager's globals, starts the two ambient SEs, and moves to mode act + 1; the pier, sky / cloud / sea effects and
  the act controllers it would create have no C and are left out (none acts on the player of the traces);
* any other exec is not created (`w->stage_note`); the list-2 tasks of the traces' stages have no C.

The pool object of the ocean's DynamicCOL (8C7606DC), the gather order and the land counts equal the reference's;
`--no-stage-tasks --tolerant` (information) shows the old residuals.

### Wave A landed (2026-10-21, `gsa1_rounds_plan.md` section 7)

The round-6 preparation decompilations (written to `build/staging`, re-verified, then copied into `src`):

| File | Functions | SHC |
|---|---|---|
| `src/overlays/b_chaos7/code_0C900480.c` | 0C900480 (SADX Sonic_SuperAura_Load), 0C90049C (_Delete), 0C9004B6, 0C9004D4, 0C9004F2 | MATCH |
| `src/overlays/b_chaos7/code_0C91D200.c` | 0C91D200, 0C91D222 (Sonic_SuperAura_Display), 0C91D486 (Sonic_SuperAura_Main) | MATCH |
| `src/overlays/b_chaos7/code_0C91DB60.c` | 0C91DB60, DCB2, DDB6, DE82, DED2, DFE0, E016, E1D2, E26A, E5E2 (the Super clones of 8C0B41E0 ... 8C0B4E62) | MATCH |
| | 0C91E06A (clone of the NON_MATCHING 8C0B48E0) | NON_MATCHING: 142/142, 22 rows an R2/R3 rename of dead registers |
| `src/overlays/b_chaos7/code_0C914D60.c`, `code_0C917088.c` | the trigger 0C914D60; the ring drain 0C917088 (SuperSonic_SubtractRings) | MATCH |
| `src/overlays/b_chaos7/code_0C9150BA.c` | 0C9150A0, the sea plane 0C9150BA / 0C915120 / 0C91514A | MATCH |
| `src/1st_read/code_8C046480.c`, `code_8C046960.c` | the particle execs 8C046480, 8C0464EE, 8C046960; the splash spawner 8C046A9C (B_CHAOS7's aura) | MATCH |
| `src/1st_read/code_8C049160.c`, `code_8C049A5A.c`, `code_8C0585A6.c` | the follower AI's small dependencies (8C049160, 8C049A5A, 8C0585A6-8C058664: 15 functions) | MATCH |
| `src/1st_read/code_8C047040.c` | the enemy work initialiser 8C047040 (SADX EnemyInitialize; round 5's proxies) | MATCH |
| `src/1st_read/code_8C02F27C.c` | CCL_Init's endless "CANT GET COLLI_WORK" loop calls `PC_ONLY(sa1g_fatal(0x8C02F4B0, ...))` | MATCH (SHC output unchanged) |

* `config/symbols.b_chaos7.txt` labels the BSS matrix `B_0CA6C2D0` (0C91E26A's pool imports it) and carries the SADX
  names as comments; the two BSS bytes `B_0CA6C249` / `B_0CA6C310` stay unlabelled (absolute under SHC, extern on PC,
  as in STG05). `code_8C0B07E4.c` calls the aura task by its canonical name `func_0C900480` (the alias
  `B_CHAOS7_exec_0C900480` is gone); `code_8C085532.c` / `code_8C082D60.c` declare 8C047040 as returning `void *`
  (SHC output identical), so pclower's R8 sees no conflict.
* `sa1g_fatal` is an R7 runtime function (`tools/pclower.py` RT_FUNCS): libsa1's world core defines it
  (`LIB/src/world/sa1_world.c`; `LIB/gen/gen_stubs.py` WORLD_RUNTIME, so no glue), the harness in
  `harness/src/pc_runtime.c` (a hook fault; `gen_native.py` NATIVE_HELPERS, `gen_ga.py` HARNESS_RT_FUNCS).
* `pclower --all` lowers 568 TUs (images B_CHAOS7 25 entries, STG01 16, STG05 22, TIKAL_PROG 17; 1,988 code-map
  entries), 0 errors. B_CHAOS7's map is active only in a world that holds B_CHAOS7.PRS (a Sonic world without a stage
  overlay); 1ST_READ reaches it through the base stubs 0C900480 / 0C9004B6 / 0C9004D4 / 0C9004F2, which dispatch to
  the world's image first (`LIB/gen/policy/stubs.tsv` keeps them: `super_sonic.md` Review R.2.3).
* Stubs: 209 (fatal 102, void 62, event 20, ret 10, freetask 9, address-only 5, null 1); 8C047040, 8C049160,
  8C049A5A, 8C0585BA and 8C0585DC are lowered now, the stale lines of the fishing rope sim (8C0EBFA8, 8C0EC0A8,
  8C0EC60C) went too, 8C083A1E (the boss-AI creator the wrapper 8C0585CA tail-calls) is new (`fatal`); the measured
  evidence was re-run against the screen references (`LIB/gen/stub_evidence.py --update-tsv`: 23 lines changed).
* The harness routes the landed 1ST_READ C (`--ga` and `--pc`); B_CHAOS7 is not routed (the harness has no overlay
  image guard yet; its `all` file set is `src/1st_read`).

### Measured facts

* **Task lists at the stage start** (`--dump-ram-at 0,1,2` of a private pure build, task-list walk): list 0 holds
  the level task (Emerald Coast 0C900000, Red Mountain 0C911E0A) before the sky and the camera; list 1 the death
  zone task, the player and the landtable manager; list 2 the level's second task (Emerald Coast 0C90023E ->
  0C90024C, Red Mountain 0C912860); after step 0 list 3 holds the player's own tasks, then the level task's
  (Emerald Coast act 1: 0C9049EA, 0C904980, footprints, ocean, the pier 0C9089BA with 27 children). DynamicCOL:
  Emerald Coast act 1 one entry (0x88000002, the ocean) from step 0; Red Mountain act 3 one (0x1001, task 0C913028)
  from step 1.
* **The particle system** (stage start 8C04E660: `8C09DDDA(B_8C7493D4, 0x200)`, then `8C09E2DA`; asm only): a free
  list of 512 nodes of 0x4C bytes and 12 effect lists from the common table D_8C1C40C4 (13-15 with the level's table
  D_8C1C4140[level], stage overlay data, all without init functions). Without it the decompiled spawners 8C0465E0 /
  8C046A08 found no list and drew no rand() (the reference 40 draws ahead at big_ec3 steps 179 / 193 and in
  ec1_spindash's splash from step 145). The boot does both (`LIB/src/world/sa1_world_boot.c`). The particle manager
  8C09DF64 runs in the step since 2026-10-20 (it has C; `LIB/src/world/sa1_world_tick.c`), so nodes are released; its
  particle execs 8C046480 / 8C0464EE / 8C046960 are game C since 2026-10-21 (below; before, `LIB/src/rt/rt_effect.c`).
* **The M1a freeze list** (as built, `sa1g_world_transplant`): every task exec without a code-map entry is
  frozen (skipped by the world's executor): the camera 8C0197BA, the sky 8C052FA0, the STG01 stage task
  `0C900000` and the tasks it created in step 0 (a transplant has the base code map only), the sound and effect
  tasks. The landtable manager 8C0519C0 is not frozen: an executor override runs its mode-0 set-up but not its
  per-frame draw step 8C051D50 (a fatal stub). The RAM images are `build/traces/m1_<trace>.step{0,1}.ram`
  (a private pure harness build, `--dump-ram-at 0,1`), not `ec1_start.ram`.
* **The M0 window** is steps 0-16, not 0-18: the reference is airborne (`twp->flag & 3 == 0`) until step 16
  and touches the deck at step 17, two steps before the mode change of step 19.
* **Boot as built** (6.2, 6.3; `LIB/src/world/sa1_world_boot.c`): the stage start builds the land cache at the
  end of step 0, so worlds boot without the extra world update (`SA1G_BOOT_NO_WORLD_UPDATE`); the camera
  adapter is `CreateElementalTask(2, 0, NULL)` with the game's own empty exec 8C098440 (no synthetic
  0x7F00xxxx address) and its taskwk as camera_twp D_8C18B7EC, written by the world before each live step and
  from the record's camera blob at player entry in replay; the game clock is stopped (D_8C1BF198 = 0, as in
  the references), rings 0, lives 4; the landtable manager's first-exec set-up (8C051A16, the object pool,
  D_8C19E96C = 400, D_8C19E974 / 978 = 1) is done by the boot; then the stage tasks (above), player 0, the
  death-zone task 8C07BAC0 (DISC collision or stage tasks) and the particle system.
* **Host yaw and camera conventions** (`LIB/src/sa1_character.c`, `LIB/src/world/sa1_api_world.c`): the
  game's ang.y moves along (cos a, 0, sin a), clockwise seen from +Y, so the host yaw (counter-clockwise,
  `sa1_pose.yaw`) is `-ang.y`. The camera adapter gets ang.y = `camera_yaw - 0x4000` (BAMS): 8C05117E
  subtracts the camera ang.y from the stick angle (forward = -0x4000), so a forward stick moves the character
  along the host camera yaw; the game's dead-zone bias of about 6 degrees is kept. `sa1_input` 0.2 carries
  only the stick, the buttons and `camera_yaw` (6.6's camera position / smode and analog override are not in
  the API yet).
* **Stub policy** (10.5, `LIB/gen/policy/stubs.tsv`, `LIB/gen/README.md`): pclower stubs 220 functions over the
  whole game (530 TUs; 3 of them in the STG05 image): fatal 107, void 66, event 20, ret 10, freetask 9, address-only
  8; runtime glue `njFraction` (= rt's 8C63A980) and `UNK_PcWrite` (fatal). 26 lines went stale on 2026-10-20 and
  were taken out (the newly lowered fidelity gaps, Big's lure reset, the draws of the capture). The evidence column
  comes from the call log of the originals over the 15 reference scripts (`LIB/gen/stub_evidence.py`). One FIDELITY
  stub is left (D_8C046B60, no reference trace depends on it). Two cross-TU prototype conflicts of the new sources
  are on pclower's R8 allow list (8C050190: `sa1_gfn` vs `Sint32`, one 32-bit word; 8C0941FC: `SA1_UPTR` -1 vs a
  taskwk pointer, a stub that reads arena pointers only).
* **Sizes** (x86_64-windows release): lowered game code 763 KB of text (522 objects), lowered rt 34 KB, code map
  98 KB of tables (1,897 entries in 4 maps), stubs + glue 7 KB, world core 50 KB of text + 69 KB of tables (the
  66 KB FSCA table), asset layer + API 103 KB; `sa1.dll` 1.44 MB on disk (x86-windows 1.49 MB), 39 exported
  `sa1_*` symbols; the game / rt objects import only the stack probe and memset from the C library. A world
  reserves its 16 MB arena plus guards (copy mode).
* **Tick time**: 0.010-0.037 ms per 60 Hz step (x64 release, the replay of the 10 traces), about 1/10 of the
  Phase 6 target.

Design text corrected on 2026-10-20 (marked "corrected" where it stands): the M0 window and the M1a
transplant (freeze list, RAM images) in 11 "First milestone", the V3 / V4 rows of 9.1, the M1 commands, and
the answer to 12.2 question 11. 2.5 and 6.2 are implemented as "Overlay images" and "Stage tasks" above describe
(the code map of section 3 is per image; the boot creates the stage's own tasks on request and the particle system
always).

---
## 1. Overview and decisions

### 1.1 Goal

libsa1 runs SA1's engine, physics and playable characters (Sonic, Tails, Knuckles, Amy, Big, E-102, Tikal,
Super Sonic) from the decompiled C in `SADC/src/1st_read/*.c`, compiled for 64-bit Windows/Linux with
`-DTARGET_PC -DNON_MATCHING`. Every game byte is read at run time from the user's GD-ROM dump; the DLL holds
only code, metadata (names, DC addresses, sizes) and mathematics. The first host is a Garry's Mod module.
Owner rules: prebuilt DLL; the SDK (Ninja/Kamui/Shinobi/sound/CRT) is reimplemented, never decompiled into
libsa1; functionally verified NON_MATCHING C is allowed; all characters before release.

### 1.2 Chosen architecture: GUEST-ARENA

* Each libsa1 **world** owns a 16 MB "DC RAM" arena laid out exactly like the Dreamcast's: 1ST_READ.BIN
  (6,735,488 B) at 0x8C010000, BSS 0x8C67C680-0x8C900000 zeroed, the character's overlays at their
  load addresses. Game data is **used in place**; nothing is deserialised for the game code.
* All game structs keep their **DC layout on every target**. Every pointer stored in memory is a 32-bit
  **guest address**, the same value the DC stores. Native 64-bit pointers exist only in locals,
  parameters and return values.
* A build-time pass, **`pclower`** (`SADC/tools/pclower.py`), rewrites preprocessed copies of the
  decompiled translation units so every memory access to a pointer goes through decode/encode helpers,
  every global becomes an arena address, and every function pointer in memory is a DC code address.
  The checked-in sources and the SHC build are untouched.
* The SDK is reimplemented in `LIB/src/rt/` in the same game dialect and lowered like the game, with its
  state at the SDK's own BSS addresses inside the arena, so game code that reads that state directly
  (B_8C88F5F8, B_8C88F698/69C, B_8C88FE24-B_8C89007C, D_8C673480) sees the real thing.
* Floating point follows the harness interpreter's FP models (one header shared by both), under
  MXCSR round-toward-zero with FTZ/DAZ for the whole tick.
* Correctness is proven against `SADC/build/traces/ref/*.bin` with a ladder that separates C bugs, lowering
  bugs, runtime bugs and glue bugs (section 9).

This is the harness's proven 32-bit model ("guest address == host address at 0x8C000000",
`SADC/harness/README.md`) made position-independent for 64-bit hosts.

### 1.3 Why this one

| Judge | Winner | NATIVE (corr/effort/maint/fid) | ARENA | PRAGMATIC |
|---|---|---|---|---|
| 1 | ARENA | 8 / 4 / 6 / 8 | 8 / 5 / 6 / 9 | 7 / 7 / 7 / 7 |
| 2 | PRAGMATIC | 8 / 3 / 7 / 8 | 7 / 4 / 6 / 9 | 8 / 8 / 7 / 8 |
| 3 | ARENA | 8 / 4 / 7 / 8 | 8 / 5 / 8 / 10 | 7 / 7 / 5 / 7 |

GA removes whole classes of permanent work that the other two must maintain forever: the 3.03 MB / 13,134-slot
pointer relocator with its count rules and `void*` schema, per-world deep clones and a mutability census as a
correctness requirement, 64-bit layout growth (task 0x34 to 0x68, playerwk 0x200 to 600 B, colpoly pool
512 KB to 576 KB), heap and task-pool resizing, pointer identity and interior pointers (D_8C4BB6F0 =
&AmyActionList[82]), raw overlay words (~326), and the 58 inconsistent declarations (they become harmless
aliases of the same DC bytes, as on the DC). Out-of-bounds reads such as `D_8C112460[(attr & 0x60000000) >> 13]`
land on the same neighbour bytes as on the DC and stay inside the world. Its cost is concentrated in one tool,
`pclower`, whose misses are compile errors (struct-wrapped guest pointers) and whose output is checked
mechanically against the unlowered harness build.

**Plan B** (judge 2's winner, PRAGMATIC: link-level globals, a table-driven relocator, memcpy world swap)
stays documented. Trigger: the Phase 2 (pclower) exit is not met. Everything in Phases 0, 3 and 5 below is
shared by both plans.

### 1.4 Decisions

| # | Decision | Choice | Sign-off |
|---|---|---|---|
| D1 | Data model | GA: DC-layout arena per world, 32-bit guest pointers in memory, `pclower`. Portability principle 2 stays as harness hygiene and as the plan-B basis | **owner** |
| D2 | Worlds | One world per ticking character, character in slot 0 (`counter.b[0] = 0`, `twp->id = 1`). Remote players are puppets. Optional slot 1 for the Tails AI follower later | owner |
| D3 | SDK | Clean-room `LIB/src/rt/`, exported at the original SDK addresses through the code map; FP models shared with the harness (`SADC/include/pc/sh4_fpmodel.h`) | - |
| D4 | Motion player 8C03FDC0-8C041E3E | Default: ship the decompiled C (P0, route-clean); its state lives in the arena either way. It shares Ninja BSS B_8C88FE24-B_8C890074 with SDK 8C60E080/8C60E49A and was built with SHC 5.0r10, so it may be Ninja library code | **owner** |
| D5 | Tick contract | DC contract: B_8C754EC4 = 2, B_8C754EC8 alternating 0/1, pad sampled per pair with press ORed, display pass after each pair (as 8C04E95A) | owner |
| D6 | FP reference | The interpreter's FSCA/FSRRA/FIPR/FTRV/FMAC models (the reference traces were made with them) | owner |
| D7 | libm | Algorithms ported from behaviour; coefficient tables D_8C1601F4-D_8C1603A4 read from the arena (the user's 1ST_READ) | owner |
| D8 | Missing game code | Fatal-by-default stub policy (`LIB/gen/policy/stubs.tsv`); tiny game getters are decompiled, not shimmed | - |
| D9 | Collision | Landtable manager decompiled as NON_MATCHING game C; adapter only produces landtables and DynamicCOL entries; modes DISC (oracle), FAITHFUL (dump), GRID (production) | owner (GRID deviations, capacities) |
| D10 | Mesh | Capture of the game's own display pass; `sa1_pose.c` becomes the emission back end and the puppet/fallback path | - |
| D11 | Toolchain | Game and rt built with zig/clang only, one flag file `SADC/tools/pc_cflags.txt` shared with `harness/build.py --pc`; zig version pinned | - |
| D12 | task.h | 0x30 bytes under `SA1_GA` (the SADX `thp` field removed), asserted explicitly | - |
| D13 | Distribution | DLL holds lowered code, DC addresses, symbol names, the code map and the generated FSCA table; never game bytes. The arena holds the user's bytes at run time only | **owner** |
| D14 | Scope gates | Big's fishing (44 functions), objects (jump panels, ring scatter, item box, E-100 Zero), Eggman, TIKAL_PROG decompilation | **owner** |

**Owner sign-off (2026-10-20):** D1 GUEST-ARENA adopted (plan B stays the fallback). D4: ship the decompiled motion player C (treated as game code). D13: the DLL may hold DC addresses, symbol names, the code map and generated tables (FSCA), never game bytes. D14: the first release includes Big's fishing, the basic objects (jump panels, ring scatter, item box) and the E-100 Zero chaser; Eggman is out (stubbed); Tikal is in (owner scope since 2026-10-06). D2, D5, D6, D7, D9 follow the defaults in the table (the original game's behaviour and the harness's FP models).

### 1.5 Facts settled during review

* **Route state.** `docs/fixlist.md` (2026-10-19 23:24) records that after the verify-fix pass all 10 sample
  traces are bit-identical with every native function routed (`regress.py --route --exe
  harness/build/sa1h_nm_pc.exe`). `build/verify/route_matrix_nm_pc.txt` (22:25, 21 failing functions)
  predates that pass. Remaining: the register-convention helpers 8C02D278 / 8C02D286 / 8C02D2B2 /
  8C02D410 cannot be routed alone but are exact when the whole module is native; 8C0870EA has one
  untraced path that stores callee leftovers. Phase 0 re-runs `route_matrix.py` to confirm.
* **colpoly capacity.** B_8C6BE27C is `colpoly[0x2000]` x 0x40 = 0x80000 B: B_8C73E27C (the NEAR count,
  `code_8C028BDC.c`) sits exactly 0x80000 after it. The 22,288-byte "gap" in `bss.src` comes from false
  labels B_8C6C398C/398D, B_8C70039C, B_8C70198C split out of glyph words
  (`asm/1st_read/data/8C119194.src:947`, `8C131194.src:404/484/650`). So `.DATA.L _sym` is not a reliable
  pointer marker; GA never relies on it.
* **Triangle counts.** `build/traces/land/L01A0.tri` holds 27,132 triangles over all 445 COLs; the collidable
  subset (flags & 0x400003, 151 COLs) is 13,701. Both survey numbers are right.
* **Overlay slots overlap.** B_CHAOS7 shares 0x0C900000 with STG02-09, MINICART and SHOOTING; TIKAL_PROG at
  0x0CB00000 (219 KB) overlaps S_SBMOT at 0x0CB08000 (`LIB/src/sa1_manifest.c:16-31`). World images must
  pick one occupant per slot (2.5).
* **Negated list-head link.** `code_8C02CEF4.c:51` `COLOBJ_HEAD_LINK(head) ((void *)(0 - (SA1_UPTR)(head)))`
  stores 0 - 0x8C02D548 = 0x73FD2AB8; 8C02CF20 tells head from colobj by sign (CMP/PZ). Under GA this word
  must stay a raw guest integer and its segment (0x13) must trap (2.1, rule R6b).
* **FSCA reference table** is built by `harness/src/sh4_cpu.c:121` `fsca_build()` with host `sin()` in
  round-to-nearest, so it depends on the CRT. libsa1 must use a dump of that table, not an independent
  derivation (8.2).
* **Boundary.** P0/P1 code calls 82 non-game functions directly (68 SDK, 14 runtime). The five EC1 traces
  execute 53 of them; 9 more run only in other characters' traces; 20 never run in any of the 10 traces
  (`build/verify/coverage/*.txt`).

### 1.6 New and changed files

| Where | File | Role |
|---|---|---|
| SADC | `tools/pclower.py` | Lowering pass (profiles `ga32`, `ga64`); reuses the `harness/tools/gen_native.py` AST loader and `rewrite_floats` |
| SADC | `tools/pc_cflags.txt` | The one PC flag set, read by `harness/build.py` and `LIB/build.py` |
| SADC | `include/sa1/guest.h` | `sa1_gp`, `sa1_gfn`, `SA1_LD`/`SA1_ST`/`SA1_GV`, segment table externs, per profile |
| SADC | `include/pc/sh4_fpmodel.h` | fipr, ftrv, fsrra, fmac, ftrc, NaN canon, moved verbatim from `harness/src/sh4_cpu.c` |
| SADC | `src/1st_read/` new files for 8C0519C0-8C052FA0 | Landtable manager as NON_MATCHING game C |
| SADC | `build/pclower/<profile>/` (ignored) | `lowered/*.c`, `sa1g_fntab.c`, `sa1g_symtab.h`, `sa1g_layout_asserts.c`, `report.json` |
| LIB | `src/rt/*.c` | SDK/runtime reimplementation (section 4) |
| LIB | `src/rt/rt_fsca_q.c` | 16,385-entry quarter-wave FSCA table dumped from the harness, with its SHA-1 |
| LIB | `src/world/*.c` | Arena, segments, image composition, boot, tick, faults, capture, events |
| LIB | `src/sa1_coladapt.c` | Collision adapter (section 5) |
| LIB | `gen/policy/stubs.tsv` | Boundary stub policy (10.5) |
| LIB | `tests/replay_test.c`, `tests/transplant_test.c` | Trace replay and RAM transplant |

---

## 2. Game data and globals

### 2.1 Guest address space

A guest address `g` (Uint32) is the DC virtual address. Its segment is `(g >> 24) & 0x1F`, the SH-4
physical 16 MB block, so P0 0x0C..., P1 0x8C... and P2 0xAC... alias exactly as on the hardware. Each world
has `uintptr_t seg[32]`.

| seg | Guest range (canonical) | Content |
|---|---|---|
| 0x00 | 0 | NULL: `decode(0)` returns host NULL (2.2) |
| 0x08-0x0B | 0x88000000-0x8BFFFFFF | Static collision segment, shared by all worlds of a context, read-only pages (64 MB). Moved from GA's 0x10-0x17 because negated pointers land in 0x13 |
| 0x0C | 0x0C/0x8C/0xAC000000 | The world's DC RAM arena (16 MB reserved, guard pages either side) |
| 0x13 | (0x73xxxxxx, 0xF3xxxxxx) | **Trap**: `0 - (any arena address)`, i.e. COLOBJ_HEAD_LINK values. Faults with a dedicated diagnostic |
| 0x18 | 0x98000000- | Per-world extension: `pcdata` (C-defined variables), moving-object models, placeholders, kill planes, window landtable, raised-capacity pools |
| 0x1D | 0x9D000000- | Host-stack window, registered at tick entry (`base = rsp_entry - 16 MB`) |
| all others | - | A process-wide 16 MB `PROT_NONE` / `PAGE_NOACCESS` trap reservation (judge 3: a guest word such as 1.0f = 0x3F800000 must never decode to a mapped low host address) |

### 2.2 decode, encode, globals

* `decode(g)` = `g ? (T*)(seg[(g >> 24) & 31] + (g & 0xFFFFFF)) : NULL` (a cmov, no branch). NULL plus a
  small field offset hits the never-mapped first 64 KB and becomes a world fault (6.9).
* `encode(p)`: NULL -> 0; inside the arena -> `off < 0x900000 ? 0x8C000000 | off : 0x0C000000 | off`
  (the DC convention: P1 for 1ST_READ, BSS and heap, P0 for overlays); collision segment -> 0x88000000 | off;
  seg 0x18 -> 0x98000000 | off; stack window -> 0x9D000000 | off; anything else -> world fault (debug:
  assert with the host address).
* `SA1_GV(a)` = `ram_base + (a & 0xFFFFFF)`, the address of a global.
* Tick entry copies the world's `seg[]` and `ram_base` into two process globals (one tick at a time per
  process, guarded by a mutex; a TLS variant is a build option).

### 2.3 `pclower`

Pipeline per TU: `zig cc -E` with the `-D`/`-I`/`-std` flags of `tools/pc_cflags.txt` plus `-DSA1_GA` (target x86,
so the AST sees the ILP32 = DC layout and every `SA1_ASSERT_*` is a real `_Static_assert`) -> decimal float
literals rewritten to their RZ hex value -> `-Xclang -ast-dump=json` of the preprocessed file -> byte-offset edits
-> `build/pclower/<profile>/lowered/<file>.c` with `#line` markers back to the original. Text that came from host
system headers is replaced by its `#include`, so one lowered text serves every target.

| Rule | What it does |
|---|---|
| R1 types | A pointer in a memory position (field, global, array element, pointee, escaped local or parameter, block-scope record) becomes `sa1_gp` (`struct { Uint32 v; }`) or `sa1_gfn`. Function pointers are `sa1_gfn` in values too (typedefs lowered in place; `T (*f(args))(...)` declarators regenerated as `sa1_gfn f(args)`). Value positions keep native types with lowered pointees (`NJS_OBJECT **` local -> `sa1_gp *`). A missed site is a compile error |
| R2 accesses | Loads `SA1_LD(T, lv)`; stores `sa1_st(&(lv), v)` (lvalue evaluated once); memory-to-memory copies and NULL stores as raw guest words; `++`/`+=` raw guest arithmetic; `&slot` has type `sa1_gp *`; automatic initialiser lists put `{sa1_encode(p)}` (or the raw word of a memory load) into pointer members |
| R2b escapes | Locals **and parameters** whose address is taken as `T **` live in `sa1_gp` storage (a parameter is renamed `p__sa1in` and copied into `sa1_gp p` at entry) |
| R3 globals | Every DeclRefExpr to a data symbol becomes `(*(LT *)SA1_GV(0x8C78C608u))`; extern declarations become typedefs of the lowered type. Addresses from `build/*.symbols.txt` and `config/asset_symbols.txt` (plus `D_/B_` names and overlay names ending in their address). A **C definition** of such a symbol (`static const char D_8C1113D8[] = ...`, `D_8C111590`) is the arena's bytes (the matching build emits exactly them) and is lowered like an extern |
| R4 C-defined data | File-scope and static-local variables without a DC address and string literals used as pointers become **pcdata objects**: per TU a block (`struct sa1g_pcdblk`), an initialiser and a unit descriptor `SA1G_PCD_UNIT`. `sa1g_pcdata.c` places the units: ga64 consecutively in the extension segment 0x18 (guest 0x98000000+, `sa1g_pcd_size()` bytes, the world maps them and calls `sa1g_pcd_init()` after entering), ga32 in each unit's static storage. Objects are reached through `sa1_decode(sa1g_pcdo[k])`; the initialiser copies plain data and encodes pointer members at world creation. Non-static objects are exported to other TUs as `sa1g_pcdv_NAME` (e.g. `sa1_tmu`, defined by `libsa1/src/rt/rt_misc.c`, used by `code_8C06C0C0.c`). File-scope compound literals are rejected (none exist) |
| R5 function pointers | Designators become DC-address constants (`SA1_GFN_C(0x8C0B2338u)`); indirect calls `((F)sa1_fn(g, SIG))(...)`; `==`/`!=`/`<`/`<=`/`>`/`>=` compare the code addresses as the SH-4 does; truth values `SA1_GFN_NZ` |
| R6 numerics | Float literals RZ; non-constant `/` and `%` (also `/=` `%=`; a lvalue with side effects such as `a[i++] /= d` is evaluated once through a statement expression) -> SHC semantics; float -> int FTRC-saturating; `SA1_FMAC` -> the double form (8.1); R6c: `sizeof`, `offsetof` and pointer differences keep the ILP32 width |
| R6b raw casts | Integer<->pointer casts whose operand is a memory load or whose result goes straight to memory move the raw guest word; other int -> pointer casts decode (listed as escapes). `SA1_UPTR` is `uint32_t` |
| R7 hygiene / rt symbols | `rand`, `srand`, `memset`, `memcpy`, `strlen`, `strcpy`, `atan2f` -> `sa1g_*`, uses and definitions (rt's `atan2f` becomes `sa1g_atan2f`; a TU's own declarations of these names are ignored by R8) (code-map entries at 8C10D334, 8C10D39C, 8C10D31C, -, 8C10D3F0, 8C10D3DC, 8C10D0F8). Runtime pieces without a DC address map to the rt symbols of `sa1/guest.h`: the SHC intrinsics `_builtin_fabsf/sqrtf/fsrra/fipr/ftrv/fsca/set_cr/get_cr/set_imask/get_imask/set_gbr/get_gbr` -> `sa1g_*`; `njFraction`, `sa1_tmu_tcnt1`, `UNK_PcWrite` keep their names; host data `sa1_rt_xmtrx`, `sa1_rt_pad`, `sh4_fsca_q` stay native, `xm` (code_8C0103C4.c, "the XF bank") becomes `(*(float (*)[16])sa1_rt_xmtrx)` |
| R8 gates | Fail-closed: every problem is an error in `report.json` and a non-zero exit: unparsable types, overlapping rewrites, functions/data without an address that are not runtime symbols, duplicate definitions, function-pointer signature mismatches, compile failures (`--compile`) and cross-TU **prototype conflicts**, except those in `R8_ALLOWED_CONFLICTS` (the conflicts still open in the sources, see below). A call of an undeclared function (C89 implicit `int f()`, accepted by SHC) is made explicit in the lowered text and listed as an escape (`--drop-failing`, used by the harness build, leaves TUs with errors to the interpreter instead of failing) |
| R9 layout | Each TU keeps its `_Static_assert`s; `sa1g_layout_asserts.c` (every record of `include/`: `sizeof` and every field `offsetof`, values measured by clang on the unlowered ILP32 text) and per-TU asserts for the records a `.c` file defines itself. `task` is 0x30 under `SA1_GA` (D12, applied by pclower until `include/task.h` does it) |
| R10 pinning | zig version, clang version and an AST format fingerprint (a probe file covering every node kind and key pclower reads) are pinned in `tools/pclower.py`; any difference fails the run. Re-validation: `pclower.py --revalidate` (selftest, sample TUs + layout asserts on 3 targets) and then the V1 gate below; `--revalidate --write-pin` records the new toolchain |

Profiles: `ga32` (harness: identity decode/encode, `SA1_GV(a)` = `a`, pcdata and stack objects are host memory
the interpreter reaches by pass-through) and `ga64` (libsa1: segment table, encode to P1/P0 in the arena and
through `sa1g_encode_slow` elsewhere). Only `include/sa1/guest.h` differs.

Segments in `guest.h` (2.1): `sa1g_encode_segment(p, s, base, span)` is the canonical encoding of the non-arena
segments (0x08-0x0B collision -> 0x88..., 0x18 extension -> 0x98..., 0x1D host-stack window -> 0x9D...); the
world's `sa1g_encode_slow` applies it to its span table (the selftest driver in `pclower.py` is a reference).

**Code map** (`sa1g_fntab.c`): a hash-and-displace perfect hash keyed by `addr & 0x1FFFFFFF` (P0/P1/P2 aliases
hit one entry), `sa1g_fn_lookup`, `sa1_fn` (a miss or a signature mismatch is a world fault), a lazily built
reverse hash for `sa1_fn_addr`, `sa1_fn_name`. Entries: every lowered definition with a DC address, the R7/rt
names, and the unresolved callees (`--stub-unresolved`: fatal stubs in `sa1g_stubs.c`; `--extern-unresolved`:
left to the host, e.g. the harness thunks).

**Other outputs**: `sa1g_symtab.h` (name, DC address, class `bss` / `pod` / `pointer_data` / `code_table` /
`overlay` / `runtime`, lowered type, file; one row per declared view), `report.json` (counts per rule and per TU,
`scope`, `escapes`, prototype conflicts, runtime symbols, pcdata exports, compile results), `functions.json`
(definitions, externals and ABI signatures per TU, read by `harness/tools/gen_ga.py`).

#### Phase 2 status (2026-10-20)

What changed against the Phase 1 spike: R4 pcdata (segment 0x18 / ga32 static storage, cross-TU exports); R2b for parameters; pointer members of automatic initialiser lists; block-scope records; declarators returning function / array pointers; `/=` `%=` on lvalues with side effects; relational operators on function pointers; C definitions of DC symbols lowered as arena data; the rt symbol mapping of R7 (intrinsics, `xm`, `sa1_rt_xmtrx`, `sa1_rt_pad`, `sh4_fsca_q`, `njFraction` ...); calls of undeclared functions made explicit as C89 `int f()` and reported (`code_8C0869E0.c` `_builtin_fabsf`, `code_8C08ABC0.c` `func_8C08EE98`, source issues); the perfect-hash code map; `sa1g_layout_asserts.c` and per-TU layout asserts; the symtab class column; R10 fingerprint + `--revalidate`; `--compile` on three targets; lowering in worker processes; `--selftest` (every new rule, run as ga32 on x86-windows and ga64 on x86_64-windows with the segments 0x08, 0x0C, 0x18, 0x1D of a fake world, compiled for x86_64-linux); `guest.h`: `sa1g_encode_segment`, the code-map entry type, the pcdata unit API, the rt declarations.

`py -3.12 tools/pclower.py --all -j 16 --compile --stub-unresolved` (src/1st_read, src/overlays/tikal_prog and libsa1/src/rt): 507 TUs lowered, 0 error(s); compile x86-windows 511/511, x86_64-windows 511/511, x86_64-linux 511/511; 757 layout asserts in sa1g_layout_asserts.c for 91 header records; 1816 code-map entries (222 fatal stubs). Escapes: implicit_declaration 2, int_to_pointer_decode 15, stack_address_stored 20. Runtime functions no lowered TU defines (the world core or rt provide them): UNK_PcWrite, njFraction, sa1g_fabsf, sa1g_fipr, sa1g_fsca, sa1g_fsrra, sa1g_ftrv, sa1g_set_cr, sa1g_set_imask, sa1g_sqrtf.

Measured scope (`report.json` `scope`; the design figures were counted before the later decompilation batches, so the current numbers are larger where more code exists):

| Measure | Design (P0/P1, 2026-10-19) | P0/P1 TUs now | All lowered TUs now |
|---|---|---|---|
| translation units | 343 | 355 | 507 |
| loads of pointers held in memory | 3,440 | 4,183 | 5,572 |
| stores of pointers held in memory | 735 | 828 | 989 |
| read-modify-writes | 15 | 15 | 15 |
| &slot | 29 | 33 | 34 |
| function-pointer loads | 83 | 81 | 99 |
| function-pointer stores | 143 | 155 | 222 |
| indirect calls | 63 | 63 | 69 |
| indirect calls through memory | 50 | 50 | 55 |
| function-address sites | 228 | 266 | 384 |
| global references | 2,740 | 3,104 | 5,548 |
| sizeof(T *) sites | 16 | 16 | 16 |
| stack addresses stored into memory | 11 | 11 | 20 |
| global symbols | 518 | 592 | 978 |
| functions whose address is taken | - | 215 | 295 |

Rule counts over all TUs: R1_casts 1, R1_locals 38, R1_params 446, R1_sizeof_ptr 16, R2_addr_of_slot 34, R2_loads 5,572, R2_null_stores 227, R2_raw_copies 155, R2_rmw 15, R2_stack_addr_stores 20, R2_stores 606, R2b_escaped_decls 4, R2b_escaped_locals 4, R3_global_refs 5,548, R3_global_symbols 978, R4_pcdata_cross_tu_refs 6, R4_pcdata_decls_moved 2, R4_pcdata_exported 1, R4_pcdata_objects 14, R4_pcdata_refs 3, R4_pcdata_strings 12, R4_pcdata_vars 2, R4_string_refs 12, R5_fn_designators 384, R5_fnptr_compares 27, R5_fnptr_loads 99, R5_fnptr_null 42, R5_fnptr_stores 222, R5_fnptr_truth 2, R5_indirect_calls 69, R5_indirect_calls_through_memory 55, R6_div_mod 3, R6_float_literals 7,842, R6_float_literals_double_or_inexact 62, R6_float_literals_rounding_differs 46, R6_ftrc 60, R6b_fnptr_to_dataptr 1, R6b_int_to_ptr_decode 15, R6b_int_to_ptr_raw_stores 1, R6b_ptr_to_int_encode 6, R6b_ptr_to_int_raw 5, R6c_ilp32_widths 92, R7_renamed_definitions 1, R7_renames 322, R7_rt_data_decls 18, R7_rt_data_refs 45, R7_rt_function_refs 187, R8_implicit_declarations 12, R9_local_layout_asserts 3,648.

R8 allow list (`R8_ALLOWED_CONFLICTS` in `tools/pclower.py`): 14 cross-TU prototype conflicts are still open in the sources and are accepted by name; every other conflict fails the run. They are the canonical-prototype work items: CCL_Init, CreateElementalTask, func_8C01AD2C, func_8C07B2DC, func_8C07B346, func_8C07C3F2, func_8C07CE40, func_8C099500, func_8C0B27EE, func_8C0DD8DC, func_8C0DEDC0, func_8C0DEFB6, func_8C0DF060, func_8C603998.

V1 (lowering equivalence): `harness/build.py --non-matching --pc --ga` links 1,488 (1,485 with adapters) lowered functions (all of `src/1st_read` except `code_8C012520.c`, which needs the PC-only `UNK_PcWrite`); `regress.py --route --exe harness/build/sa1h_ga.exe`: identical on all 15 scripts; `route_matrix.py` on the GA build: 1,485 routable functions x 15 scripts, 3,184 (function, script) pairs run (coverage-filtered): only func_8C02D410, the known register-convention helper, breaks traces, exactly as in the unlowered build (results in build/pclower/v1/). With `--rt` (the lowered `libsa1/src/rt` routed too; `rt_input.c` stays with the interpreter's pad HLE because its libsa1 profile reads the world's pad latch) `regress.py --route` is also identical on all 15 scripts.

Still open for the world core (LIB/src/world, other owners): map segment 0x18 with `sa1g_pcd_size()` bytes and call `sa1g_pcd_init()` after entering a new world; register the host-stack window (segment 0x1D) at tick entry and the collision segment 0x08-0x0B, and let `sa1g_encode_slow` use `sa1g_encode_segment` with their spans (the selftest driver is a reference); link `sa1g_pcdata.c` and `sa1g_layout_asserts.c`; rt must provide `njFraction` and `UNK_PcWrite` (no DC address). Headers: D12 and `SA1_UPTR` (above) are still applied by pclower.

### 2.4 Globals and symbols

* The 591 data symbols referenced by P0+P1 C (311 D_, 194 B_, 86 named, in 359 files) are all arena addresses.
  There is no generated definition file and no per-symbol code.
* Inconsistent declarations stay each file's own view of the same bytes: B_8C7A40CC (Sint32 in
  `code_8C0E0E70.c:66`, task* in `code_8C0E63C0.c:20`) is no longer a truncation bug; D_8C02D548's five views
  (colobj*, NEARLIST*, FDOBJ*, LCOBJ*, LANDPOLYLIST*) alias; B_8C754BDC Float[3] vs NJS_VECTOR alias.
  Header migration (`docs/header_migration.md`) continues for hygiene, not as a libsa1 prerequisite.
* The symbol list is still generated (`sa1g_symtab.h`): name, DC address, declared types per file, class
  (BSS / POD / pointer data / code table / overlay / runtime-owned). It feeds debugger pretty-printers, the
  `--dump-world` output and the write census, never the data path.

### 2.5 World images

A pristine image is composed once per character kind per context from the views `sa1_loader.c` /
`sa1_manifest.c` already validate (SHA-1 per file). `sa1_load_disc` keeps these images instead of freeing them
(it frees them at `sa1_loader.c:538` today).

| Kind | Arena contents | Notes |
|---|---|---|
| Sonic | 1ST_READ + S_MOT (0x0CC00000) + S_SBMOT (0x0CB08000) + B_CHAOS7 (0x0C900000) | Super Sonic is the same world (6.3a) |
| Tails | 1ST_READ + M_SBMOT (0x0CADC000) | Tails' motions are in 1ST_READ |
| Knuckles | 1ST_READ + K_MOT (0x0CC00000) | |
| Amy | 1ST_READ + A_MOT (0x0CC00000) | A_MOT_action_0CC93B9C / 0CC93698 |
| E-102 | 1ST_READ + E_MOT (0x0CC00000) | |
| Big | 1ST_READ + B_MOT (0x0CC00000) | |
| Tikal | 1ST_READ + TIKAL_PROG (0x0CB00000) | No S_SBMOT (overlap) |
| Test: DISC mode | Sonic image with STG01 at 0x0C900000 instead of B_CHAOS7 | EC1 landtable 0x0C981554 in place |

### 2.6 Initialisation sequence (per world)

1. Reserve 16 MB plus guard pages; copy the pristine image (copy mode) or map it copy-on-write (Phase 6:
   Windows pagefile section with `FILE_MAP_COPY`, Linux `memfd` + `MAP_PRIVATE`).
2. 1ST_READ.BIN at offset 0x010000-0x67C680. This includes the data inside the code section: D_8C02D540-D_8C02D96C
   (D_8C02D964 = 10430.0f) and D_8C03F760. BSS 0x67C680-0x900000 (2,636,160 B) is zero.
3. Overlays per 2.5.
4. Apply the world patch list (2.7).
5. Initialise `pcdata` (R4) and the runtime state (6.2).

### 2.7 Patches instead of relocation

Game data needs no conversion. The image builder applies a short, explicit patch list; each patch is logged:

* **Actions outside the loaded views.** Every `PL_ACTION.actptr` of the world's action lists (Sonic
  8C1C55A0 x142, Tails 8C256D6C, Knuckles 8C3528CC, E-102 8C4225D4, Amy 8C4BB1D0, Big 8C525564) that points
  into an overlay slot not holding the right file (decided with the `k_*_over` override tables of
  `sa1_manifest.c`) is rewritten to a typed placeholder in seg 0x18: a 1-frame NJS_ACTION on the character's
  root object. Selecting one raises `SA1_EVENT_MISSING_ACTION`. Without this, Sonic's stage-overlay actions
  would decode into B_CHAOS7 bytes, and PSetMotion dereferences `actptr` unconditionally. Big's 7 NULL
  entries get the same placeholder.
* **DeathZoneTable** 8C1BE404[level][act] for the world's level/act -> the world's kill-plane list in seg 0x18
  (5.6). The other 20 raw stage pointers are left; they are only read for the current level.
* **D_8C1BDF24** (per-stage task list with 20 raw overlay words and code words func_8C06B102 / func_8C06B43C):
  left as is; the code words resolve to trap entries in the code map (section 3).
* Collision globals and the landtable pointer are written by world boot, not patched (6.2).

### 2.8 Per-world state and the write census

Everything in the arena is per world automatically: rand (D_8C5FBB08), Gravity, the camera pointer, the 65
character-private singletons, the 44 directly written initialised symbols (E-102 materials D_8C42616C ...,
AmyActionList, GammaWeldList ...), weld `org` buffers, vertex arrays rewritten by PJoinVertexes, and Sonic's
sibling/model-swapped nodes (D_8C1C7D70, D_8C1C8CA4, D_8C1CAD20, D_8C1C7504, D_8C1C8454). No clone set has to
be right for correctness. The harness write census (`sh4_ram_dirty` over the 10 traces, mapped back to
symbols through `sa1g_symtab.h`) is still produced in Phase 0 to size copy-on-write and per-world memory
(estimated 9-11 MB committed in copy mode, 2-4 MB with CoW, 16 MB reserved).

### 2.9 SDK-owned state that lives in the arena

| Address | State | Read by game code |
|---|---|---|
| B_8C7EC8E0 | Matrix stack buffer, 128 x 64 B (it ends exactly at the heap, B_8C7EE8E0) | through the slot pointer |
| B_8C88F5F8 | Matrix stack slot pointer | func_8C057B40, `code_8C0572D8.c`, `code_8C010340.c` |
| B_8C88F698 / B_8C88F69C | Stack limit / depth | 5 functions |
| B_8C88FE24-B_8C89007C | Motion evaluator block (handlers, MOTION_CTX, B_8C88FF38 ...) | about 30 symbols, hundreds of references |
| B_8C890074 | Motion callback (njSetMotionCallback) | E-102 8C0CFACE |
| D_8C673480 | Current texlist | njSetTexture users |
| B_8C88F62C, B_8C88F660/664/668, B_8C88FCB4 | Control-3D flags, constant attr masks, constant material, Kamui context | draw save/restore 8C605DAC/8C605E0A |
| D_8C67C640 | Unit matrix (library data in the image) | address taken in 56 functions |

XMTRX (16 floats) is not memory on the SH-4; it lives in the world struct (6.1).
---

## 3. Code pointers

* **Code map** `sa1g_fntab.c` (generated): a perfect hash keyed by `g & 0x1FFFFFFF` -> {native function,
  signature id}, plus a reverse hash native -> DC address for traces and debug output.
* **Entries:**
  * all decompiled 1ST_READ functions (1,371 in the PC manifest);
  * the runtime under the original SDK/CRT addresses, e.g. njRotateXYZ 0x8C639CF4 and njRotateZXY
    0x8C639FF8 (code stores them into D_8C19AD44 and into handler locals in 8C0406A0, 8C040840, 8C040A60,
    8C04196C);
  * overlay functions called by name: func_0C900480 / 4B6 / 4D4 / 4F2 (B_CHAOS7, Super Sonic),
    func_0C900AE0 (STG08), func_0C907998 (STG03), func_0C900080 (SBOARD), TIKAL_PROG;
  * libsa1 adapter functions at synthetic addresses 0x7F00xxxx (camera task exec, platform proxy exec and
    forcewk `call_back`, stub execs). Data segment 0x1F is a trap, so these can never be dereferenced as data;
  * trap entries for code addresses found in data with no C: func_8C06B102, func_8C06B43C (D_8C1BDF24).
* **Disc tables are used verbatim**, with no conversion: D_8C19ACE0 = 8C0405B2 / 5D2 / 612 / 6A0 / 6E0;
  D_8C19ACF4 = 8C040720 / 73C / 73C / 760 (followed by 7 raw words); D_8C19AD20 = 8C041516 / 54E / 600 / 666 /
  6CC; D_8C19AD34 = 8C041732 / 75C / 75C / 792; D_8C15C7F4 (AMY_EFFECT_EXECS) = 8C0DFAF8 / 8C0DFD3E. A disc whose
  table holds an unexpected address faults at the first call, naming that address.
* **BSS code slots** (MOTION_FUNC x6 and [2] x5, TaskFuncPtr B_8C798798/7B8, D_8C19AD44, D_8C1C1D40) start at 0
  and are written by lowered code as DC addresses.
* `sa1_fn(g, sig)`: a miss is a world fault naming the address; debug builds also compare the signature id.
  About 50 indirect calls per step.
* No game data holds a pointer into SDK code (full scan of 1ST_READ.BIN). 8C06C0C0 registers func_8C06C1D8
  through 8C64133E, which is a no-op in rt.

---

## 4. Runtime and SDK reimplementation

### 4.1 Files

All in `LIB/src/rt/`, written in the game dialect and lowered by `pclower` (profile `ga64` for libsa1, profile
`ga32` for the harness build `sa1h_rt`, where XMTRX aliases `SH4.xf`).

| File | Content |
|---|---|
| `rt_matrix.c` | XMTRX + 128-slot memory stack: push/pop/unit/set/get, rotations, translate, scale, multiply, invert, calc point/vector, project, njInitMatrix |
| `rt_vector.c` | Vector ops, distances, intersections, sphere test (FMAC sites exact) |
| `rt_trig.c`, `rt_fsca_q.c` | njSin/njCos/njTan from the dumped FSCA table |
| `rt_libm.c` | atan2f (both wrappers), asinf, acosf, floorf, ceilf, modff; cores 8C10E590 / E5AC / E6A0 / E6F8 / E758 / E7B8 / EF84 / FB94 / FBB8 / EB50 / F164 |
| `rt_misc.c` | rand/srand (state D_8C5FBB08), _divls/_divlu/_modls/_modlu, __quick_mvn / 8C10CDDC, memcpy 8C604500, strlen, strcpy, `sa1_tmu` |
| `rt_draw.c`, `rt_texture.c` | Draw-state stores and capture, texture state |
| `rt_motion.c` | 8C60E080 and 8C60E49A on top of the decompiled motion player; njSetMotionCallback |
| `rt_input.c` | pdGetPeripheral from `sa1_input` |
| `rt_game.c` | Game-side boundary: sound triggers -> events, renderer capture, `stubs.tsv` policies (4.4) |

### 4.2 The 82 boundary functions

Status: **M1** = executed by the EC1 traces, implemented and diff-verified before M1 (53 functions). **P3b-t** =
traced only by other characters' scripts (9). **P3b-u** = never traced; needs a dedicated `sa1h diff --sdk`
argument generator (20). The figures give P0/P1 call sites, with all 1ST_READ call sites in brackets.

| Addr | Name | Purpose | libsa1 equivalent | Status |
|---|---|---|---|---|
| 8C10CA5C | _divls | Signed division helper, divisor 0 -> 0 | `rt_misc.c`, SA1_DIV semantics; 10 (103) | M1 |
| 8C10CB10 | _divlu | Unsigned division | SA1_UDIV; 1 (19) | M1 |
| 8C10CBB8 | _modls | Signed modulo | SA1_MOD; 6 (57) | M1 |
| 8C10CC80 | _modlu | Unsigned modulo | SA1_UMOD; 2 (38) | M1 |
| 8C10CDDC | __quick_mvn | SHC struct-copy jump table | Byte copy of DC-layout structs; 6 (23) | M1 |
| 8C10D008 | acosf | Wrapper; core 8C10E590 = pi/2 - asin | `rt_libm.c` port; 9 (14) | M1 |
| 8C10D058 | asinf | Wrapper; core 8C10E5AC (FSQRT + atanf core) | `rt_libm.c`; 25 (31) | M1 |
| 8C10D0A8 | atan2 (y, x) | Second wrapper of core 8C10E6A0 | `rt_libm.c`; 2 (2) | M1 |
| 8C10D0F8 | atan2f | FDIV y/x, atanf core 8C10EF84, Horner 8C10FB94 (5 FMAC over D_8C1603A4), quadrant fix; errno ignored | `rt_libm.c`, bit-exact; 67 (133) | M1 |
| 8C10D148 | ceilf | Core 8C10E6F8, FTRC saturation for 2^31..2^32 | `rt_libm.c`; 3 (6) | P3b-u |
| 8C10D198 | floorf | Core 8C10E758 | `rt_libm.c`; 3 (6) | M1 |
| 8C10D334 | rand | LCG x*0x41C64E6D + 0x3039, (x>>16) & 0x7FFF | `rt_misc.c` on arena D_8C5FBB08; 37 (151) | M1 |
| 8C10D3DC | strcpy-like | String copy | `rt_misc.c`; 1 (9) | P3b-u |
| 8C10D3F0 | strlen | String length | `rt_misc.c`; 1 (11) | M1 |
| 8C601874 | pdGetPeripheral | PDS_PERIPHERAL per port | `rt_input.c` from `sa1_input` (id 1, support 0x000F06FE); bypassed in replay; 1 (3) | M1 |
| 8C604500 | (block copy) | Byte copy, used by CCL_Init | memcpy; 1 (18) | M1 |
| 8C604546 | (vsync) | Vsync flags, user callback | No-op; 1 (3) | P3b-u |
| 8C605628 | njColorBlendingMode | 12-way blend switch, kmChangeContext | Draw-state store; 32 (120) | M1 |
| 8C605DAC | (save draw state) | Save Kamui context fields | Draw-state stack; 7 (48) | M1 |
| 8C605E0A | (restore draw state) | Restore them | Draw-state stack; 7 (48) | M1 |
| 8C6088BC | (release texlist) | Release per entry | Success no-op; 3 (38) | P3b-u |
| 8C608C82 | (load texlist) | Load per entry, returns 1/-1 | Success; 2 (20) | P3b-t |
| 8C608CCC | njSetTexture | D_8C673480 = texlist; 1 (-1 for NULL) | `rt_texture.c`; 42 (167) | M1 |
| 8C608D14 | njSetTextureNum | Select a texture of the current list | Current texture number; 1 (5) | P3b-u |
| 8C60D1F8 | njSetMotionCallback | `MOV.L R4,@B_8C890074` | Store; 1 (1) | P3b-t |
| 8C60E080 | (draw motion) | Object, motion, frame; fills B_8C88FE40 | `rt_motion.c` on the decompiled player + capture; 2 (2) | P3b-u |
| 8C60E49A | (draw action/shape) | Light Speed aura | Same; 1 (2) | P3b-u |
| 8C60EDE0 | (control 3D) | Flags into B_8C88F62C | Store; 2 (6) | P3b-u |
| 8C60EDE6 | njSetConstantAttr | And/or masks B_8C88F660/664 | Store; 1 (5) | P3b-u |
| 8C60EDF0 | njSetConstantMaterial | ARGB into B_8C88F668 | Store; 11 (58) | M1 |
| 8C611028 | njDrawModel | Draw one model | Capture; 3 (8) | P3b-u |
| 8C61103C | njDrawObject | Draw a tree | Capture (tree walk with matrix ops); 6 (9) | M1 |
| 8C6215D6 | (alternative model draw) | CCL debug shapes, death zone, E-102 | Capture; 10 (20) | P3b-u |
| 8C638EAC | njAddVector | v1 += v2 | `rt_vector.c`; 35 (71) | M1 |
| 8C638ECC | njCalcPoint | (p,1) by FTRV (NULL = XMTRX) or 3 FIPR | `rt_matrix.c`; 25 (103) | M1 |
| 8C638F28 | njCalcVector | The same with w = 0 | `rt_matrix.c`; 67 (145) | M1 |
| 8C638F94 | njGetMatrix | *m = XMTRX | 3 (8) | M1 |
| 8C639090 | njInnerProduct | Dot product by FIPR | 33 (46) | M1 |
| 8C6390B0 | njInvertMatrix | Determinant 8C64F3EC + adjugate in exact op order; singular -> all 16 floats 0x7F7FC99E, return 0 | 8 (16) | M1 |
| 8C63997C | njMultiMatrix | 16 FIPR + 4 FTRV | 5 (18) | M1 |
| 8C639B50 | njOuterProduct | FMUL + 3 FMAC; returns the length | 8 (9) | M1 |
| 8C639B98 | njPopMatrix | depth - n < 1 -> 0; else XMTRX = *pointer | 145 (420) | M1 |
| 8C639BDC | njProjectScreen | FTRV + FIPR + perspective FDIV; screen from 8C640528 / 8C640500(0x31C7) | Fixed screen model per world; 2 (7) | M1 |
| 8C639C70 | njPushMatrix | At the limit -> 0; store XMTRX and advance; NULL also stores the new top | 129 (403) | M1 |
| 8C639CF4 | njRotateXYZ | Z, Y, X; zero angles skipped | 11 (32) | M1 |
| 8C639EC8 / 8C639F5C / 8C63A1CC | njRotateX / Y / Z | FSCA + 2 FTRV; a non-NULL m borrows XF4-XF11 | 104 / 116 / 107 | M1 |
| 8C639FF8 | njRotateZXY | Y, X, Z; zero angles skipped | 8 (23) | P3b-u |
| 8C63A268 | njRotate | Arbitrary axis: FSRRA/FSQRT/FMAC, 4 FSCA, 10 FTRV | 2 (3) | P3b-u |
| 8C63A5EC / 8C63A69C | njScaleV / njScale | Column FMULs | 21 / 37 | M1 |
| 8C63A748 | njScalor2 | FIPR(v,v) | 7 (17) | P3b-t |
| 8C63A75C | njScalor | FSQRT(FIPR(v,v)) | 79 (142) | M1 |
| 8C63A770 | njSetMatrix | NULL -> XMTRX = *ms | 11 (18) | M1 |
| 8C63A7E4 | njSubVector | v1 -= v2 | 27 (47) | M1 |
| 8C63A804 / 8C63A878 | njTranslateV / njTranslate | FTRV into XF12-14, or 4 FIPR | 66 / 34 | M1 |
| 8C63A8E0 | njUnitMatrix | NULL -> XMTRX = D_8C67C640 | 18 (64) | M1 |
| 8C63A94C | njUnitVector | FIPR, FSRRA, scale; a zero vector gives NaN | 68 (132) | M1 |
| 8C63A970 | njCos | FSCA cosine | `rt_trig.c`; 45 (135) | M1 |
| 8C63A980 | "njRandom" (misnamed) | Tail call 8C10EB50 = modff fraction, state B_8C89033C | `rt_libm.c`; 1 (5) | P3b-u |
| 8C63A9B8 | njSin | FSCA sine | 44 (162) | M1 |
| 8C63A9C4 | njSqrt | FSQRT; x < 0 -> -sqrt(-x) | 41 (93) | M1 |
| 8C63A9F4 | njTan | FSCA, FDIV | 1 (3) | P3b-t |
| 8C63CE24 | njDrawLine3D | Lines | No-op (optional capture); 6 (20) | P3b-t |
| 8C63D414 | (3D triangles) | | No-op; 3 (5) | P3b-t |
| 8C63D66C | (3D polygon) | | No-op; 3 (6) | P3b-u |
| 8C63EEB4 | njDrawSprite2D | | No-op; 8 (48) | M1 |
| 8C63EF74 | njDrawSprite3D | | No-op, sprite capture later; 15 (46) | P3b-t |
| 8C63FAE8 / 8C63FB18 | (render-state switch) | Context +0 = 0x100, +60 = 2 | Store; 3 / 3 | M1 |
| 8C63FE54 | njCollisionCheckSS | Sphere-sphere test, 2 FMAC | `rt_vector.c`; 1 (1) | P3b-u |
| 8C63FEAC | (line/plane helper) | 7 FMAC; calls 8C6505F0 and njDistanceP2L | 2 (4) | P3b-t |
| 8C63FF84 | (P2L variant) | 11 FMAC, 7 FMUL, FDIV | 4 (6) | P3b-t |
| 8C640080 | njDistanceP2L | 7 FMAC, FDIV, signed FSRRA 8C63A9DC | 10 (14) | M1 |
| 8C640128 | njDistanceP2P | FMUL + 2 FMAC, sqrt | 22 (52) | M1 |
| 8C64016C | (plane-plane -> NJS_LINE) | 35 FMUL / 18 FSUB / 5 FDIV in exact order | 4 (5) | M1 |
| 8C640766 / 8C640922 / 8C640A80 | njPrintColor / njPrintC / number print | Debug text | No-op (optional log ring); 1 / 17 / 2 | P3b-u |
| 8C64133E | (register vblank callback) | Into D_8C67C004 | No-op; 1 (3) | P3b-u |

Internal helpers implemented with them (never called from game code): 8C63A9DC signed FSRRA, 8C64F3EC
determinant, 8C650614, 8C6505F0.

### 4.3 Intrinsics

`_builtin_fabsf` (109 uses in 45 files), `_builtin_fipr` (40), `_builtin_sqrtf` (20), `_builtin_fsrra` (12),
`_builtin_ftrv` (4: 8C02D528, 8C0867B0; it reads XMTRX) and `_builtin_fsca` (2: 8C082160) become static inlines
on `sh4_fpmodel.h`; `_builtin_ftrv` uses the world's XMTRX. `set_cr`/`set_imask` appear only in startup code.

### 4.4 Game-side boundary implemented in libsa1

| Function(s) | libsa1 behaviour |
|---|---|
| 8C03700C, 8C037098 (hand-written renderers; return early when B_8C754EC8 != 0), wrapper 8C036F12, strip renderer 8C037108 | Capture, keeping the early return |
| 8C038D00 visibility test | Return 1 |
| 8C038CEC palette bank, 8C09E778 sprites, 8C098FAE / 8C098FFC / 8C099500 textures, 8C046A08, 8C09DE34 | No-op (void) |
| 8C09DDFC | NULL (callers check) |
| 8C046F48 (head wobble vector, visual only) | Zero vector |
| Sound: 8C0940DA, 8C09413A, 8C0941FC, 8C094330, 8C0943E0, 8C0945A0, 8C0945D0; jingles 8C107ADA, 8C107B26, 8C107C80; voice 8C107BA0; rumble 8C109EAC, 8C109FDE | Push events (7.4) |
| 8C04E386 (game-state request) and the death path | `SA1_EVENT_DEATH` / restart event |

The full policy list with evidence is in 10.5.

### 4.5 Edge semantics that must not be "fixed"

njUnitVector(0) gives NaN components; a singular njInvertMatrix fills 0x7F7FC99E and returns 0; push fails
silently at the limit and pop below 1 is ignored; push(NULL) stores XMTRX twice; zero angles are skipped in
njRotateXYZ/ZXY; floor/ceil saturate like FTRC for 2^31 <= |x| < 2^32; njFraction in `harness/src/pc_runtime.c`
differs from modff for |n| >= 2^31 and for the sign of zero, so rt ports modff.
---

## 5. Collision adapter

### 5.1 What runs unchanged

All decompiled land collision, CCL, shadow and death-zone code: world init 8C028E60, per-step cache build
8C028EC2, NEAR 8C028BFE, TOUCH 8C029B00, probes 8C029400 / 8C02A680 / 8C02AD22 / 8C02B620 / 8C02BC60 /
8C02C100 / 8C02CA00, cache probes 8C02D060 / 8C02D170 / 8C02D2E0 / 8C02D410, player cores 8C0730D8 / 8C073160 /
8C0777E0, shadows 8C091C20-8C092BE0, CCL 8C02E820-8C034940. The adapter only produces an `_OBJ_LANDTABLE`
(`ssCount` Sint16 at +0, COL* at +0xC) and DynamicCOL registrations, in DC layout, inside the world's
segments.

### 5.2 Landtable manager (Phase 0, NON_MATCHING game C)

* 8C0522C0 ListGroundForCollision (600 B): per-query gather into B_8C754EF4 / B_8C754EF0, 0x400 cap.
* 8C052518 (902 B): per-step gather; dynamic B_8C759708 -> B_8C757EF4 / D_8C19E964, static COLs -> B_8C7584F4 /
  D_8C19E968, 128 each, in list order.
* 8C052E30 (260 B): gather everything, used once after 8C028EBA sets D_8C196044.
* 8C0528AE DynamicCOL_Add (sets 0x10000000 automatically when |center|^2 >= 225 and any ang & 0xFFF0) and 8C052940
  DynamicCOL_Remove (tail-calls 8C02CE18).
* 8C052A1E / 8C052A5E (NJS_OBJECT pool, 256 x 0x34 at B_8C75D310, in-use flags B_8C760710), 8C05289E / 8C0528A6
  (D_8C19E978 = 0 / 1), 8C052F34 / 8C052F3A (set / reset D_8C19E96C, default 400.0f).
* 8C052A8A (Gravity = (0,-1,0)) is already matching.

They are verified with `sa1h diff` and routed traces like any other game function.

### 5.3 Three ways to fill it

| Mode | Source | Synthesis | Exactness | Use |
|---|---|---|---|---|
| DISC | STG01 image at 0x0C900000 (test-only Sonic image), landtable 0x0C981554 used in place | none | bit-exact by construction | Oracle for M1b and for FAITHFUL |
| FAITHFUL | `--dump-land` v2 (`.tri` + `.col`) | One NJS_OBJECT per original COL, in landtable order, with the **original** pos/ang/scl/evalflags and model center/r; model-local points; triangles as type-0 meshsets in the order 8C028EC2 emits them (strip winding flips already applied); `mats[0]` attrflags 0, `mats[1]` 0x00800000; COL center/radius/flags copied | bit-exact expected (same FIPR inputs, same polygon order) | M1 (M1c) |
| GRID | Host triangles via `sa1_collision_set_static` | 5.4 | tolerance only (the partition changes NEAR order and culls) | Production / GMod |

FAITHFUL keeps the original object transform (judge 3) because the cache build applies `T*Rz*Ry*Rx*S` whatever
the evalflags say, the probes honour evalflags (7 untransformed, 6 translate-only, otherwise njCalcPoint), and
static culls such as 8C092000 test `obj->pos` against `model->r` x max(scl).

### 5.4 GRID synthesis

* Preprocess in SA1 space (`sa1_collision.c` already converts): drop degenerate triangles; split until the
  longest edge is <= 128 units (SA1 data never exceeds 198.2; 8C029400 / 8C02A680 / 8C02D2E0 assume <= 220;
  the angle quantisation in 8C029B00 errs by edge length x 9.6e-5); merge coplanar slivers.
* Cells of 64-128 units; one object per (cell, slAttribute):
  * NJS_OBJECT (0x34): evalflags 6, pos = cell AABB centre, ang 0, scl 1, child/sibling 0;
  * NJS_MODEL (0x28): points relative to pos (<= 32,767), type-0 meshsets (<= 65,535 triangles each), `mats[0]`
    attrflags 0, `mats[1]` 0x00800000, center (0,0,0), r = max|p|;
  * COL (0x24): center = pos, radius = r + 16 (NEAR ignores speed), Model, flags.
  These live in the shared collision segment (guest 0x88000000+).
* Before each world update the adapter writes the world's **window landtable** in seg 0x18: the <= 128 COLs
  nearest the world's character (copies, 36 B each) plus <= 128 dynamic entries, <= 191 objects in total so
  8C028EC2's 192-object cache cannot overflow. The decompiled 8C052518 then runs unchanged. Nearest-first is a
  deliberate, documented deviation from list order.
* Never set 0x10000000 or 0x60000000 on static cells.

### 5.5 Moving objects

`sa1_collision_object_add/move/remove`, per world:
* an NJS_OBJECT + NJS_MODEL in seg 0x18 (evalflags 0, model in object space, |center| < 15 so the automatic
  0x10000000 is not set; BAMS angles applied Z, Y, X);
* a level-0 proxy task (exec at a 0x7F00xxxx address) that calls DynamicCOL_Add(attr | 0x1 | 0x08000000, tp,
  obj), sets `twp->flag |= 0x100` and `fwp = CAlloc(2, 0x1C)`;
* `fwp->call_back` (synthetic address) computes `pos_spd = M_new * M_old^-1 * (p - o_old) + o_new - p` and
  `ang_spd.y` = yaw delta; 8C07506E applies it (fwp[0] for pno 0);
* host transforms are written into the object before the executor runs.

### 5.6 Water, death zones, surface flags

* Water: top faces only, in objects flagged 0x2 without 0x1. 8C0925A0 finds it through 8C02CA00 (lowest surface
  above pos.y + 1 within 200 units; near-vertical triangles ignored).
* Death zones: per-world DEATHZONE list in seg 0x18 behind the patched DeathZoneTable entry (2.7). 8C07BAC0 kills
  player 1 (pno 0, our character) when a polygon is above within 30 units and none below, so kill planes are top
  faces. Stage 0x2100 also kills below y = -432.079.
* Surface map (`libsa1.h`):

| SA1_SURFACE_* | COL bits | Game effect |
|---|---|---|
| SOLID | 0x1 | Gathered (flags & 0x400003) |
| WATER | 0x2 (no 0x1) | Water physics, splash, breath timer (drowning task after 0x438 frames) |
| HEAVY_WATER (new) | 0x400000 | Weight x0.6, air_resist_y x2, run_accel x0.45 |
| SLIPPERY | 0x4 | Weight 0.1 (0x8 = 0.15 through RAW) |
| SAND (new) / BOOST (new) | 0x10 / 0x80 | Weight 0.5 / 1.5 |
| NO_CLIMB | 0x1000 | Knuckles cannot grab or climb |
| HURT | 0x10000 | Damage when grounded on it |
| SLIDE (new) | 0x20000 | Sliding floor (8C06DCA2) |
| STAIRS (new) | 0x4000 | Stair mode; flag ramps laid over Source stairs |
| NO_LAND (new) | 0x40 | Treated as a wall |
| WALL_SLIDE (new) | 0x200000 | No landing, always slides, no climb |
| DIG (new) | 0x100 | Diggable |
| DOUBLE_SIDED | material 0x00800000 | Per triangle |
| KILL | DeathZoneTable view | 5.6 |
| RAW | pass-through | Any other bits |

### 5.7 Capacities

DC limits by default:
* NEAR 96 triangles per query; TOUCH 16 per object (overflow leaks one matrix push);
* `hits[16]` / `sorted[16]` in 8C073160 / 8C0777E0 (no bound check);
* object cache 192, 191 usable (overflow is a NULL write in 8C028EC2); polygon pool 8,192 (overflow leaves empty
  lists that crash 8C02D060 / 8C02D2E0);
* per-step lists 128 / 128, per-query list 1,024, DynamicCOL 256, object pool 256, strip buffer 1,024 vertices,
  z buckets 128 x 16 units.

EC1 already reaches NEAR p99 98-129 at random triangles. The adapter counts every limit reached. Raising limits
(Phase 7, owner decision) is a pclower manifest change: the pool symbol moves to seg 0x18 with a larger count, and
`SA1_CAP()` covers stack arrays. Exactness tests always run with the DC limits.

### 5.8 Ordering and latency

Step order: host updates -> executor -> CCL 8C0342E0 -> world update (8C052518 + 8C028EC2). Queries in step N use
step N-1's cache. On world creation, `set_static` and teleports, libsa1 runs 8C028E60 plus one world update first.

---

## 6. World, tasks, players, input, camera

### 6.1 World struct

`{seg[32], arena reservation, ram_base, XMTRX[16], capture buffer, event queue, input latch (previous buttons,
pair state), camera adapter state, step accumulator, fault jmp_buf, stats and capacity counters}`. Every game
global lives in the arena. Context = {disc images, pristine image per character kind, static collision segment,
code map, worlds}.

### 6.2 Boot

1. Arena from the pristine image (2.6).
2. Runtime init: njInitMatrix(B_8C7EC8E0, 128, 0) (depth 1, slot 0 = unit); the screen/perspective state that
   8C0544E2 sets (8C640528, 8C640500(0x31C7)); srand(0xDEAD0CAB) or a host seed.
3. Heap 8C098A10 (0x90000 at B_8C7EE8E0, filled 0xFF; DC sizes because task is 0x30) and tasks 8C09844A (pool
   0x1000 x 0x30 = 0x30000, leaving about 393 KB). MFree's `for(;;)` on a double free becomes
   `PC_ONLY(sa1g_fatal())`.
4. Collision init 8C028E60.
5. Globals (6.5), camera adapter task (6.7), window landtable.
6. Player creation (6.3), then one world update.

### 6.3 Player creation

This replaces the asm-only LoadPlayerTask 8C04F3C0 and start-position function 8C04F8FA:
`func_8C06C300` (clear the 8 slots); `CreateElementalTask(7, 1, main)` with SonicMain 8C0A77BC, TailsMain 8C0B6148,
KnucklesMain 8C0C0640, E102Main 8C0CBE00, AmyMain 8C0D6B40 or BigMain 8C0DFFA0; `counter.b[0] = 0`,
`counter.b[1] = chr`; playertwp[0] (B_8C78C608) and playermwp[0] (B_8C78C628); pos/ang from the API. The first
step runs mode 0 (PInitialize 8C06C350, helper tasks, CCL_Init, PJoinVertexes jvmode 0) and does no physics,
exactly as in the game.

**6.3a Super Sonic and Tikal.** Super Sonic = SonicMain with equipment 0x8000 and GetStageNumber() returning
0x13xx (the game's own path) in a Sonic world with B_CHAOS7 loaded. The object-substitution table in
`sa1_manifest.c` (8C1CE76C -> 0C909D2C, 8C1DBB2C -> 0C90BD04) is kept for the puppet path only. Tikal needs
TIKAL_PROG decompiled (17 functions, 4,622 B) and her own create path (LoadPlayerTask never creates chr 4).

### 6.4 Tick step and contract

`sa1g_step(sub)` replaces the asm-only 8C04E714:
1. Enter the world: seg table, MXCSR (8.1), stack window, `setjmp`.
2. Pad update 8C050BE4 (calls rt pdGetPeripheral, then the decompiled analog conversion 8C05117E) in live mode;
   in replay mode the recorded values are written instead (6.6).
3. Skip 8C099A50 (event coroutines) and 8C06C182 (debug timer).
4. Executor 8C0988A4 (list 7 as level 8, then levels 0..6).
5. CCL 8C0342E0.
6. Particle manager 8C09DF64 (as built since 2026-10-20: it runs here as in 8C04E714, `LIB/src/world/sa1_world_tick.c`;
   its execs 8C046480 / 8C0464EE / 8C046960 are the game's C since 2026-10-21, `src/1st_read/code_8C046480.c` and
   `code_8C046960.c`; libsa1's hand-written `LIB/src/rt/rt_effect.c` is gone).
7. Adapter refresh, then world update 8C028EC2 (which calls 8C052518).
8. Tail: B_8C752BDD = 0; leave the world; drain events.

Frame pairing replaces 8C04E95A (D5). Steps run in pairs with B_8C754EC4 = 2 and B_8C754EC8 = 0 then 1. The pad is
sampled before sub 0 and refreshed between the steps as 8C0517F6 does, with `press` ORed across the pair. The frame
counter B_8C752BE8 grows by 2 per pair (8C08A5BE). The display pass 8C0988C4 runs after sub 1 with capture on.

Why: about 43 character functions gate on B_8C754EC8. Sonic's head morph 8C0B2376 rebuilds only when it is
non-zero, the bubbles 8C0B3B1A skip their push sphere, Tails' tail spin 8C0BDB72 reads B_8C754EC4, and 8C03700C
draws only on sub 0. EC4 = 1 / EC8 = 0 (portability.md section 9) would change behaviour.

The host `dt` accumulator consumes whole pairs (1/30 s); leftover time carries over. Replay mode uses `sub` from
each record and runs the display pass only when the trace header asks for it (the 10 references do not).

### 6.5 Globals libsa1 sets (the tick contract)

| Global | Value | Why |
|---|---|---|
| B_8C754EC4 / B_8C754EC8 | 2 / 0,1 | Steps per frame, sub-step (154 functions read EC8) |
| B_8C754BDC (+BE8/BEC) Gravity | (0,-1,0) via 8C052A8A | Never set by P0/P1; the harness boot run shows (0,0,0) without it |
| B_8C7493C8 CurrentCharacter | chr | GetPlayerNumber |
| B_8C7493BA / B_8C7493BC Level / Act | default 1 / 0 (stage 0x0100) | GetStageNumber; 33 decompiled callers branch on it |
| tcrf_LevelSelectVar 8C760970 | 4 | Game mode |
| tcrf_CollisionDisplayVar 8C752BDC | -1 | 0 means debug display on (the game sets -1 at 8C04E3F4) |
| D_8C19B468 | 0 | 1 forces Sonic mode 83 and skips CCL_Entry |
| B_8C7492AC | 0 | Event running |
| D_8C18B7EC camera_twp | camera adapter twp | 34 player functions dereference it, many without a NULL check |
| B_8C7596F4 / D_8C19E978 / D_8C19E96C / D_8C196044 | window landtable / 1 / 400.0f / 0 | Gather |
| B_8C752BE8 / B_8C752BE4 | frame counter / rings | Idle randomness (`& 127`); the death-on-hit decision |
| B_8C799920 | event flags | Upgrades read by PInitialize through 8C09068E (exposed in the API) |
| D_8C5FBB08 | rand state | Per world, seedable |

The getters GetStageNumber 8C04F7E0 (14 B), GetPlayerNumber 8C04F698 (8 B), 8C09068E (14 B), 8C08A2BC / 8C08A074
(C exists) and 8C08A71E (C exists) are game code that reads these globals, so they are decompiled or verified
rather than shimmed.

### 6.6 Input

`sa1_input` v0.2 (versioned with `struct_size`):
* held buttons as SA1 enums, mapped to Katana bits A 0x4, B 0x2, X 0x400, Y 0x200, START 0x8;
* raw stick x/y (-128..127, Y < 0 is forward; read directly by 8C06DCA2, 8C06FB8C, 8C0700C2 and the snowboard);
* camera {ang x/y/z, pos, smode/mirror};
* optional analog override {ang, stroke}.

Live mode: rt pdGetPeripheral builds the PDS_PERIPHERAL record, then the decompiled 8C050BE4 / 8C05117E produce
B_8C754C08[0] and B_8C754DCC[0]. This keeps the +-3072 dead-zone quirk (stick (0,-127) gives stroke 0.990317) and
the B_8C754E7C repeat flags. Replay mode writes `pad_on`, `pad_press`, x1/y1, `analog_ang` and `analog_stroke`
from the record and skips step 2.

### 6.7 Camera

A level-0 camera adapter task (exec at a synthetic address) whose twp is D_8C18B7EC. Each pair it copies the host
camera into ang.x/y/z (BAMS), pos and smode. ang.z >= 0x4000 inverts steering (8C06FB8C). Big's fishing writes the
camera pos (8C0E6500), and that write is reported back. In replay mode the trace v2 camera blob (0x40 B) is copied
in instead.

### 6.8 Multiple characters

One world per character, always slot 0, so the paths that only work for player 0 are the only ones used: the pad
copy covers slots 0/1 only, the land gather centres on players 0/1, homing target lists split player 0 from the
rest, B_8C754E7C has 4 entries, 53 player-0 references are hard-coded in 27 decompiled files, there is one camera
and 65 character singletons. Switching worlds is two stores plus MXCSR. There is no cross-world CCL;
character-to-character interaction is a host matter. GMod is owner-authoritative: each client ticks its own
character (one world) and remote players are puppets (7.3). Optional later: slot 1 in the same world for the Tails
AI follower (func_8C081AC0), the game's own two-player precedent.

### 6.9 Faults

World faults (decode trap, code-map miss, fatal stub, MFree double free, access violation in game or rt code) mark
the world dead, record the DC PC and address, and make the tick return `SA1_ERR_GAME_FAULT`.
* Windows: a vectored exception handler acts only when the faulting RIP is inside libsa1's game/rt text and the
  address is in a trap region, the low 64 KB or a world reservation; it redirects to a trampoline that `longjmp`s
  to the tick.
* Linux: `sigaction(SIGSEGV)` + `siglongjmp` with the same checks.
* Everything else is passed on to GMod.

---

## 7. Output

### 7.1 State

`sa1_state`: twp->pos/ang, velocity = mwp->spd x 60 x scale, pwp->mj.action/nframe, on ground = twp->flag & 3,
pwp->attr (water and surface bits), all converted to host space. `sa1_state_ex` adds mode, smode, attr, equipment,
item, and the hand/foot/user attach points (`_pos`/`_vec` computed by PJoinVertexes).

### 7.2 Mesh: capture of the display pass

* The display pass 8C0988C4, and the draws inside exec such as SonicMain's 8C0AE17A, run with the rt draw
  functions in capture mode. Each capture is {model guest address, XMTRX copy, texlist, texture number, blend mode,
  constant material/attr}. Display passes must run because they carry logic: E-102's 8C0CFACE stores B_8C7A2F48 /
  B_8C7A2F08 and installs the node callback 8C0D215C.
* Emission reuses `sa1_pose.c` as the back end. Topology, UVs, materials and texture ids come from the existing
  decoded cache keyed by the meshset's DC address (the loader already dedups by image and physical address).
  **Points and normals are read live from the arena**, because PJoinVertexes, the morph (func_8C055ECE) and face
  shape motions rewrite them. Welds, morphs, part swaps, link motions (MAlloc'd by 8C079C76) and effects therefore
  appear exactly as the game draws them.
* Check: the captured mesh hash equals `sa1h_ga --disp` with the runtime routed, frame for frame (Phase 6).

### 7.3 Puppets

`sa1_character_pose` keeps the current `sa1_pose.c` evaluator (double-precision trig, provisional loop rule) until a
light "puppet world" (motion through rt plus PJoinVertexes on a given action and frame) matches live capture.

### 7.4 Sound and events

The game's own trigger layer is replaced, not the SDK driver; the 32 SDK sound/ADX functions are never reached.
Triggers: play SE 8C0940DA (55 files), positional SE 8C09413A (15), 8C0941FC, 8C094330 (8), 8C0943E0, stop
8C0945A0 (11), stop-by-task 8C0945D0 (6), banks 8C098FAE / 8C098FFC (no-op), voice 8C107BA0 / 8C109EAC, jingles
8C107ADA / 8C107B26 / 8C107C80, rumble 8C109FDE. Each pushes `{world, type, id, bank (0/2/3/6), host position
copied at call time, task}` onto the world queue, drained by `sa1_poll_sound_events` after MXCSR is restored.

A new `sa1_poll_events` reports death/restart (8C04E386, 8C07BBC2), respawn requests (8C04F8FA), missing actions,
E-102 time-up, unimplemented calls and capacity hits.

---

## 8. FP and determinism

### 8.1 Mode and flags

* Per tick: save MXCSR, set 0xFFC0 (round toward zero, FTZ, DAZ, all exceptions masked), run, restore exactly. No
  host callback, allocation, CRT maths call or log write happens inside a tick: logs go to a ring buffer flushed
  afterwards, and game allocations use the game heap. Unit test: 10^5 ticks leave the host MXCSR unchanged.
* `SADC/tools/pc_cflags.txt`, read by `harness/build.py --pc` and `LIB/build.py`: `-std=gnu11 -O2 -fwrapv
  -fno-strict-aliasing -fsigned-char -fno-builtin -fno-math-errno -ffp-contract=off -ffp-model=strict
  -frounding-math -ftrivial-auto-var-init=zero -fvisibility=hidden`, plus `-msse2 -mfpmath=sse` on x86.
  `-ftrivial-auto-var-init=zero` mirrors the harness's zeroed stack for uninitialised locals. Moving the harness
  to these flags must keep all 10 traces identical (Phase 0 exit).
* `SA1_FMAC(a,b,c)` under `SA1_GA` and in rt is `(float)((double)a*b + c)`, i.e. `sh4_fmac`
  (`harness/src/sh4_cpu.c:194`): the product is exact in double, and nested truncation under RZ equals a single
  truncation. The current PC definition (`include/common.h:214`, `__builtin_fmaf`) becomes a libm call on baseline
  x86_64 and is not used by libsa1. `tools/fmac_audit.py` still governs where SA1_FMAC appears.
* Float literals are rewritten to their RZ bits (R6). sqrt is `sqrtss`. No host libm anywhere.
* Post-link check: game and rt objects import only `memcpy`/`memmove`/`memset` (plus the compiler's stack-probe
  helper on Windows) from the CRT; the DLL exports only `SA1_API`.
* Only zig/clang builds are certified. x86_64-windows, x86_64-linux and x86-windows must write byte-identical
  replay files. MSVC/CMake builds of game code are unsupported (no `-frounding-math`, no wrap guarantee).

### 8.2 FP models

`SADC/include/pc/sh4_fpmodel.h` holds `sh4_fipr`, `sh4_ftrv`, `sh4_fsrra`, `sh4_fmac`, `sh4_ftrc` and NaN
canonicalisation, moved verbatim from `harness/src/sh4_cpu.c` (lines 162-210 today) and included by both the
interpreter and rt.

* FIPR/FTRV: four products summed in double in index order, rounded once.
* FSRRA: `(float)(1/sqrt((double)x))` in the current mode; -0 -> -inf, negative -> qNaN 0x7FBFFFFF, +-0 or
  denormal -> +inf, +inf -> 0.
* FSCA: `LIB/src/rt/rt_fsca_q.c` is the 16,385-entry quarter-wave table **dumped from the harness's
  `fsca_build()`** (built with host `sin()` in RN), with exact 0 and +-1 at the quadrants and quadrant symmetry;
  cos(a) = sin(a + 0x4000). The harness gets a selftest that its table equals the file and is then switched to load
  the file, so neither side depends on a CRT any more.
* Known deltas: NaN payload (x86 0xFFC00000 vs SH-4 0x7FBFFFFF) for NaNs produced by plain C arithmetic, and FTZ
  tininess-after-rounding corner cases. Trace comparison treats NaNs as one class.

### 8.3 Determinism

Per-world rand (D_8C5FBB08), frame counter, sub-step counter and heap (fixed arena, first-fit, filled 0xFF); no
clock reads. Worlds do not depend on how ticks are interleaved (tested in Phase 7).
---

## 9. Verification against reference traces

### 9.1 Ladder

Each rung changes one layer, so a failure points at one cause. References are `SADC/build/traces/ref/*.bin`:
ec1_stand (180 records), ec1_turns (480), ec1_run_jump (271), ec1_spindash (242), ec1_jump_hold (322), tails_sh1,
knux_rm3, amy_tp2, gamma_fe1, big_ec3.

| Rung | What runs | Command / method | Pass |
|---|---|---|---|
| V0 C | Decompiled C natively in the interpreter (32-bit, shipping flags) | `py -3.12 harness/tests/sim/regress.py --route --exe harness/build/sa1h_nm_pc.exe`; `harness/tools/route_matrix.py` | 10/10 identical; route_matrix 0 failures apart from the 4 non-routable helpers (done per fixlist.md; re-confirm) |
| V1 lowering | `sa1h_ga.exe` (lowered, `ga32`) vs `sa1h_nm_pc.exe` | `sa1h diff` on identical random RAM for every function that passes today; `regress.py --route` with `sa1h_ga.exe` | Byte-identical RAM; traces identical |
| V2 runtime | rt in place of the SDK inside the interpreter (XMTRX = `SH4.xf`) | `sa1h diff --sdk F` (10^5 cases per matrix op, 10^6 per libm function; dedicated generators for njRotate, njRotateZXY, ceilf at 2^31..2^32, singular njInvertMatrix, njUnitVector(0)); `trace --route --route-runtime` | 0 ulps; traces identical |
| V3 world init | libsa1 world boot vs `build/traces/m1_ec1_stand.step0.ram` (corrected: the RAM at the start of step 0, `--dump-ram-at 0`) | `world_boot_test --dump DIR` writes the booted arena; `LIB/src/world/world_ram_diff.py` diffs it by region and symbol (player blobs, colobj/colpoly caches at B_8C6BB27C/B_8C6BE27C, CCL lists, runtime-owned BSS) | Differences only in documented regions (heap addresses, absent level tasks, camera) |
| V4 transplant (M1a) | `build/traces/m1_<trace>.step{0,1}.ram` loaded into a 64-bit world (corrected, see 11 M1a) | `LIB/tests/transplant_test` | Decoded fields and raw blobs including pointers identical; as measured, identical but for the STG01 ocean task's residual, heap pointers by structure from step 0 |
| V5 one-step reseed | Before record i, import record i-1's twp/mwp/pwp/csts raw blobs (a memcpy under GA), pad, sub, rand, camera and XMTRX; run one step; compare with record i | `replay_test --reseed` | Every record; reports the first failing step and field |
| V6 free run | Full replay from creation | `replay_test` writes SA1TRC files; `sa1trace.py diff REF OUT --ulps 0` | 0 differing decoded fields; raw blobs identical (pointers by physical address, heap pointers by structure) |
| V7 cross-target | win64, linux64 and win32 builds; two runs per process | Byte compare of the replay outputs | Identical; MXCSR restored; 0 host allocations in ticks |
| V8 multi-world | Two Sonic worlds plus one Knuckles world ticking interleaved | V6 per world | Identical to single-world runs |
| V9 host modes | GRID adapter, synthetic scenes, GMod | Tolerance and assertion suites | Phase 6 exit |

### 9.2 Harness additions (Phase 0)

* **Trace v2.** Per record: the camera taskwk raw blob (0x40 B) and the rand state captured **at player-exec
  entry** (hook in `harness/src/t_sim.c`; camera and level tasks draw rand before the player), XMTRX at
  player-exec entry, frame counter B_8C752BE8, rings B_8C752BE4, Gravity. v1 decoded fields must stay identical
  between v1 and v2 recordings.
* **`--dump-land` v2.** A `.col` sidecar per COL: index, flags, center, radius, object pos/ang/scl/evalflags,
  model center and r, world-space model centre as 8C028EC2 computes it (`co->center = M * model->center`),
  triangle range. The `.tri` gains model-local vertices, per-triangle material attrflags, and the builder's
  emission order.
* **`--dump-ram-at STEP`**, **`--log-sdk`** (SDK call log in the same text format as libsa1's
  `SA1_DEBUG_SDKLOG`) and **`--dump-world`** (every non-pointer global per step, from `sa1g_symtab.h`).
* **Write census** from `sh4_ram_dirty` over the 10 traces, mapped to symbols.
* `sa1trace.py`: pointer fields compared by physical address (`& 0x1FFFFFFF`); `--heap-by-structure`.

### 9.3 Debug path when a rung fails

1. Run the same script at V0/V2 in the harness. If it fails there, `harness/tools/route_bisect.py` finds the
   function.
2. If only libsa1 fails: V5 reseed gives the first step and field; diff the arena against `--dump-ram-at`; diff
   the SDK call logs to find the first diverging Ninja call.
3. Rerun with the DISC landtable to rule the adapter in or out.

---

## 10. Missing code to decompile

### 10.1 Triage of the reach scan

Starting from the 6 mains (Eggman 8C0ED880 and Tikal excluded), following direct calls and literal-pool code
constants and not descending into the SDK, the scan finds **579 missing functions (121,592 B)** and 126 SDK
functions, identical to `build/verify/reach_from_mains.json`. F = called directly by decompiled P0/P1 code;
x = executed in at least one of the 10 traces; C = a C body already exists in `src/1st_read`.

| Category | Count | Bytes | F | x | C |
|---|---|---|---|---|---|
| NEEDED-LOGIC | 58 | 17,530 | 13 | 16 | 4 |
| OBJECT-INTERACTION | 49 | 19,276 | 11 | 3 | 7 |
| RUNTIME | 75 | 12,884 | 13 | 30 | 0 |
| SOUND/VOICE | 52 | 6,972 | 15 | 17 | 0 |
| DISPLAY-ONLY | 100 | 20,624 | 22 | 28 | 5 |
| CAMERA | 38 | 9,970 | 13 | 21 | 0 |
| HUD/SCORE/EVENT/GAME-FLOW | 203 | 32,888 | 23 | 20 | 43 |
| UNKNOWN (fishing rope sim) | 4 | 1,448 | 0 | 0 | 0 |
| **Total** | **579** | **121,592** | **110** | **135** | **59** |

Status mix: 422 none/todo, 138 P2/todo, 13 P2/matched, 5 P2/nonmatching, 1 P0/todo (8C0D2086).

### 10.2 After stubbing

* Stubbing camera, display, sound and game flow at their entry points leaves **218 functions reachable
  (55,140 B)**: NL 58, OBJ 49, RT 17 (2,066 B), behind **94 boundary stubs** (16,268 B: display 47, camera 13,
  game flow 19, sound 15). 361 functions (66,452 B) become unreachable: the event system, score screens, VMU save,
  CRI AFS, printf, renderer internals and most of the camera.
* Stubbing objects too leaves 162 reachable (34,900 B) behind 87 stubs (13 of them objects, 3,234 B).
* The 17 RT functions are covered by rt (section 4): _divls 8C10CA5C, _modls 8C10CBB8, __quick_mvn 8C10CDDC,
  acosf 8C10D008, asinf 8C10D058, 8C10D0A8, atan2f 8C10D0F8, floorf 8C10D198, rand 8C10D334, and the cores
  8C10E590, 8C10E5AC, 8C10E6A0, 8C10E758, 8C10E7B8, 8C10EF84, 8C10FB94, 8C10FBB8.
* Sonic milestone closure (rough scan; roots SonicMain, 8C0988A4, 8C0988C4, 8C0342E0, 8C028E60/8C028EC2,
  8C098A10, 8C09844A): 415 functions with C, 46 boundary functions without C (8,238 B), 54 SDK and 10 runtime
  symbols to link. The 53 boundary functions the EC1 traces execute are implemented for M1; the others link to
  fatal stubs until Phase 3b.

### 10.3 NEEDED-LOGIC (58 functions)

Outside Big's fishing there are 14 functions (1,762 B):

| Addr | Size | What | State | Plan |
|---|---|---|---|---|
| 8C04F7E0 GetStageNumber | 14 | (level << 8) \| act; 33 decompiled callers | todo | Decompile (Phase 0) |
| 8C04F698 GetPlayerNumber | 8 | Returns B_8C7493C8 | todo | Decompile (Phase 0) |
| 8C0522C0 | 600 | Per-query landtable gather | asm | Decompile NON_MATCHING (Phase 0, 5.2) |
| 8C09068E | 14 | Event-flag test B_8C799920[i] (upgrades) | todo | Decompile (Phase 0) |
| 8C086F5E | 396 | Damage: scatter up to 20 rings, or kill (8C07BBC2) with 0 rings | P2 NON_MATCHING C | Verify (Phase 7) |
| 8C08A2BC | 6 | Ring count getter (B_8C752BE4) | C exists | Verify |
| 8C08A074 | 8 | Rings = 0 after a scatter | C exists | Verify |
| 8C0673E0 | 52 | IsActionStage (level < 15), TailsModeMachine | todo | Decompile |
| 8C067414 | 66 | UnitVectorSafe, fallback (0,1,0) | todo | Decompile |
| 8C066320 | 18 | Vector copy helper of 8C067414 | todo | Decompile |
| 8C0C8F20 | 224 | Knuckles Maximum Heat nearest target | P2 todo | Decompile |
| 8C0C9000 | 90 | Knuckles wall grab -> climb mode 0x18 / action 0x34 | P2 todo | Decompile (Knuckles cannot climb without it) |
| 8C0D2086 | 202 | E-102 skid acceleration | P0 todo; NON_MATCHING C in `code_8C0D2080.c` | Verify |
| 8C0EB432 | 64 | Big: target in range (action-button choice) | todo | Decompile |

Big's fishing: 44 functions (15,768 B), all P2 todo.
* Root A: lure task 8C0E6DA0 -> exec 8C0E943C (runs every frame in big_ec3) -> the 8C0E8A4A and 8C0E79D4 subtrees.
* Root B: cast task 8C0E6620 -> 8C0E6CC0 -> 8C0E6880 / 8C0E69CA / 8C0E6B64 / 8C0E9D24 (collision through
  8C0522C0 / 8C02D2E0), 8C0E6812 / 8C0E6E80 (lure upgrades, flags 0x1DE-0x1E1), 8C0EAB68.
* All 44: 8C0E6620, 8C0E6812, 8C0E6880, 8C0E69CA, 8C0E6AEE, 8C0E6B64, 8C0E6CC0, 8C0E6DA0, 8C0E6E80, 8C0E6ED4,
  8C0E79D4, 8C0E7BA8, 8C0E7C34, 8C0E7D5A, 8C0E7E00, 8C0E7F60, 8C0E80A6, 8C0E82A0, 8C0E8562, 8C0E89A0, 8C0E89EC,
  8C0E8A4A, 8C0E9072, 8C0E91E4, 8C0E93AE, 8C0E93D2, 8C0E943C, 8C0E9CC6, 8C0E9D24, 8C0EA040, 8C0EA2F0, 8C0EA48A,
  8C0EA64E, 8C0EA6C0, 8C0EA942, 8C0EAA80, 8C0EAB68, 8C0EABEE, 8C0EAE0A, 8C0EAEC0, 8C0EAF28, 8C0EB4E0, 8C0EB5B6,
  8C0EB70E.
* BigModeMachine modes 32-45 and 8C0E6446 read the lure task B_8C7A40CC (scl.y picks reqaction 60/61/62) and
  B_8C7A40E8. With fishing stubbed, casting falls back to reqaction 62.
* The rope simulation (UNKNOWN: 8C0EBFA8, 8C0EC0A8, 8C0EC212, 8C0EC4E8) must be checked for a feed into the lure
  position.

### 10.4 Decompile queue for libsa1

| Priority | Functions | Gate |
|---|---|---|
| Phase 0 | Landtable manager (5.2); GetStageNumber, GetPlayerNumber, 8C09068E | M1 |
| Phase 7 | 8C0C9000, 8C0C8F20, 8C0EB432, 8C0673E0, 8C067414, 8C066320; verify 8C086F5E, 8C08A2BC, 8C08A074, 8C0D2086 with new traces (climb entry, E-102 skid, damage with 0 rings) | All-character exit |
| Owner (D14) | Big's fishing (44) + rope (4); objects: jump panels 8C05E69E / 8C05E6C4 / 8C05E6E8 / 8C05E788 / 8C05E7D4 (1,194 B; mode 40 needs 8C05E7D4), ring scatter 8C086DE0 / 8C0870EA / 8C08774C / 8C08776C / 8C087A9E (C exists), air item box from digging (3,698 B), small animals 8C015798 / 8C015820, E-100 Zero chaser for Amy (27 functions, 12,318 B), emerald-shard dig check 8C0C9B88 | Release scope |
| Phase 8 | B_CHAOS7 Super Sonic tasks 0C900480 / 0C9004B6 / 0C9004D4 / 0C9004F2 (`src/b_chaos7`); TIKAL_PROG (17 functions, 4,622 B, `src/tikal_prog`, with its own reach scan) | Super Sonic, Tikal |
| Stubbed until scoped | SBOARD 0C900080, STG08 0C900AE0, STG03 0C907998 (Birdie in Twinkle Park); Eggman 8C0ED880 (+11 functions) | - |

### 10.5 Stub contract (`LIB/gen/policy/stubs.tsv` seed)

The default policy is `fatal`: record the DC address, `longjmp` to the tick, return `SA1_ERR_UNIMPLEMENTED`, and
report it through `sa1_poll_events`. Every non-fatal line cites its evidence.

| Function | Policy | Evidence |
|---|---|---|
| 8C038D00 | ret 1 | The display functions 8C0AE17A, 8C0BA820, 8C0C46AC, 8C0E3A40 return early otherwise |
| 8C042DC4 | ret 0 | The original is `RTS; MOV #0,R0` |
| 8C049A4E | ret 0 | Event running; also gates the positional-SE flag B_8C7A2EA8 and the Knuckles radar |
| 8C04FE40 | ret 0 | Game mode == 0x10 |
| 8C04C960 | ret 2 | Level type for the Big event camera |
| 8C08AB2E / 8C08AB60 | fixed time / ret 0 | Otherwise E-102 dies at time-up |
| 8C0818DE | ret 0 | Race flag; affects Tails' mode functions |
| 8C0919CC | ret 0 | In range |
| 8C05E6C4 / 8C05E6E8 | ret 0 | No jump panels until objects are scoped |
| 8C0C9B88, 8C107744, 8C10778E | ret 0 | Objects stubbed |
| 8C09DDFC | NULL | Callers check |
| 8C09DE3A, 8C050190 | **check first** | Results stored in D_8C4BB1C0 / D_8C4BB1CC; NULL tolerance unverified |
| 8C044680, 8C0C99E4, 8C0EB160, 8C0E6CC0 (if fishing is deferred) | exec that FreeTasks itself | Passed to CreateElementalTask |
| 8C01FB98, 8C01FD6C, 8C021700, 8C023360, 8C0239C2, 8C021432 | address only (code-map entry to a no-op) | Their consumers 8C01AD2C / 8C01ABDC / 8C01B158 / 8C01AE60 are void stubs |
| 8C01A7A4, 8C019368, 8C01AD2C, 8C01ABDC, 8C01B158, 8C01AE60, 8C0213E0 | void | Camera hooks |
| 8C04E386, the 8C07BBC2 path, 8C04F8FA | event | Death/restart, respawn |
| Sound, voice, rumble (4.4) | event | 7.4 |
| 8C099A50, 8C06C182, 8C09DF64 | not called | Skipped by the step loop |
---

## 11. Phased work plan

Phase 0 (sadc and harness), Phases 1-2 (pclower), Phase 3 (runtime) and Phase 4 (world core) can start in
parallel. M1 needs all of them, but only for the Sonic closure.

### Phase 0: decisions and sadc/harness preconditions

Work:
1. Owner decisions D1, D2, D4, D5, D6, D7, D13, D14 (1.4).
2. Re-run `harness/tools/route_matrix.py` on the 10 scripts after the 2026-10-19 verify-fix pass; fix whatever
   still fails with `tools/workflows/verify_fix.js`.
3. Decompile the landtable manager (5.2) and GetStageNumber / GetPlayerNumber / 8C09068E as game C.
4. Declare `Float func_8C10D0A8(Float y, Float x);` in `include/sa1/engine.h` and remove the 1-argument
   declaration in `code_8C02D060.c`; resolve the 9 matching + 24 NON_MATCHING prototype conflicts that R8 will
   reject.
5. `tools/pc_cflags.txt`; `include/pc/sh4_fpmodel.h`; the FSCA table dump and selftest.
6. Harness: trace v2, `--dump-land` v2, `--dump-ram-at`, `--log-sdk`, `--dump-world`, write census; re-record the
   10 references as v2.

Exit: `regress.py --route --exe harness/build/sa1h_nm_pc.exe` identical on 10/10 with the shared flag file;
route_matrix shows 0 failures (the 4 helpers excluded); `sa1h diff` PASS for the new landtable functions; v2 traces
recorded with v1 decoded fields byte-identical; SHC build still 497/497 and `1ST_READ.BIN: OK`.

### Phase 1: spike

Prototype pclower on `code_8C098444.c` (task system), `code_8C098998.c` and `code_8C098B0C.c` (heap), and run them
in a 64-bit world.

Exit: after heap init, task init and `CreateElementalTask(7, 1, 0x8C0A77BC)`, the arena bytes (heap blocks, task,
taskwk, motionwk, forcewk) equal `sa1h call` on the same RAM in the 32-bit harness.

### Phase 2: pclower and the harness GA build

Work: rules R1-R10, both profiles, `report.json`, `sa1g_fntab.c`, `sa1g_symtab.h`, the layout asserts;
`harness/build.py --non-matching --pc --ga` -> `sa1h_ga.exe`. About 2-3k lines of Python.

Exit:
* all 448 TUs lower and compile with no pclower diagnostics on x86-windows, x86_64-windows and x86_64-linux;
* `report.json` reproduces the measured scope (3,440 / 735 / 15 / 29 data-pointer sites; 83 / 143 function-pointer
  loads/stores; 228 function addresses; 2,740 global references);
* 0 unresolved escapes outside the stack window; layout asserts pass on x86_64;
* V1 passes: the lowering-equivalence diff for every function that passes today, and `regress.py --route` with
  `sa1h_ga.exe` identical on 10/10.

**Plan-B checkpoint**: if this exit cannot be reached, switch to PRAGMATIC (1.3). Phases 0, 3 and 5 carry over.

### Phase 3: runtime

Work: 3a, the 53 EC1-executed boundary functions plus the intrinsics; 3b, the other 29 (9 traced elsewhere, 20
untraced); the `sa1h_rt` harness build.

Exit 3a: V2 for the 53 (0 ulps); `trace --route --route-runtime` identical on the 5 EC1 scripts; the FSCA table
equals the harness table for all 65,536 angles. Exit 3b: V2 for all 82 with the dedicated generators;
route-runtime identical on 10/10.

### Phase 4: world core

Work: arena reservation and segments (copy mode), image composition and the patch list, boot, player creation, the
step loop and pairing, the MXCSR guard, the stack window, faults, events, `transplant_test`, `replay_test` (reseed
and free run).

Exit: **M0**, **M1a** and **M1b** (below).

### Phase 5: FAITHFUL collision and M1

Work: `sa1_coladapt.c` FAITHFUL synthesis from dump v2, the DISC loader, `sa1_collision_set_static_ex` (grouped
input with per-group transform, flags and double-sided bits), adapter counters.

Exit: **M1** (M1c below).

### Phase 6: output, API v0.2, GRID and GMod alpha

Work: display-pass capture into `sa1_pose` emission; `sa1_state_ex`; sound and event queues; `sa1_input` v0.2 and
the camera adapter; GRID mode, moving-object proxies, kill planes, the surface map; copy-on-write worlds; the
standalone viewer; the G-SA1 skeleton module.

Exit:
* the captured mesh hash equals `sa1h_ga --disp` with the runtime routed, for 60 frames of ec1_run_jump;
* GRID on L01A0 keeps the reference mode/action sequence of the 5 EC1 scripts, with positions within 1e-3 units
  (larger differences explained by the partition and logged);
* the synthetic suite passes: flat plane, 30 degree ramp, 0x4000 ramps over stairs, 90 degree wall, water box,
  moving and rotating platforms, kill plane; the step-up height is measured;
* the sound event stream equals the SE calls logged by the harness;
* Sonic runs in gm_construct; tick time measured (target under 0.2 ms).

### Phase 7: all 1ST_READ characters

Work: dump v2 for SH1, RM3, TP2, FE1 and EC3; the Phase 7 decompile queue (10.4); a write census per character;
the owner decision on Big's fishing (decompile 44 functions or ship the reqaction-62 fallback); new traces for the
climb entry, the E-102 skid and damage with 0 rings.

Exit: V6 on all 10 references in FAITHFUL mode on win64, linux64 and win32; V8 multi-world interleaving
identical; the new traces exact; 10^6 fuzzed steps without a crash (ASan/UBSan build).

### Phase 8: Super Sonic and Tikal

Work: `src/b_chaos7` (0C900480 / 0C9004B6 / 0C9004D4 / 0C9004F2) and `src/tikal_prog` (17 functions) with their
symbol files and reach scans; Super Sonic through equipment 0x8000 and stage 0x13xx; the Tikal create path. New
reference traces: Super Sonic in a 0x13xx stage, and Tikal.

Exit: V6 on the new traces.

### Phase 9: G-SA1 host

BSP -> GRID collision, `Mesh()` rendering, camera and input mapping, owner-authoritative networking with puppets,
sound playback. Exit: every character in gm_construct in multiplayer, and the owner signs off on feel.

### First milestone

**M1: Sonic standing and running on Emerald Coast act 1 dump geometry, bit-exact to `ec1_stand.bin` (180 steps)
and `ec1_run_jump.bin` (271 steps) at the player-state level, in 64-bit libsa1.**

Prerequisites: Phases 0, 2, 3a and 4 for the Sonic closure; Sonic's world image (1ST_READ, S_MOT, S_SBMOT,
B_CHAOS7).

* **M0 (early signal).** ec1_stand steps 0-16 with an empty landtable (corrected 2026-10-20: the reference is
  airborne, `twp->flag & 3 == 0`, only until step 16; it touches the deck at step 17, two steps before the mode
  change of step 19, so steps 17-18 cannot match without geometry): decoded fields equal, excluding `land_count`
  (18 at step 0 in the reference), `attr` / `last_attr` and the shadow fields (inside the raw blobs, not
  compared). If a decoded field differs only because shadow probes feed the physics, M0 is waived in favour of
  M1a. Met: 17 of 17 records equal (`world_boot_test`).
* **M1a transplant** (corrected 2026-10-20). `transplant_test` loads the reference RAM at the start of step 0
  or step 1, `build/traces/m1_<trace>.step{0,1}.ram` (16 MB each, written by a private pure harness build with
  `trace ... --dump-ram-at 0,1`; generated locally, never checked in; the older `ec1_start.ram` is not used), as
  the arena, keeps the original landtable 0x0C981554 and the decompiled 8C052518 / 8C0522C0, and freezes (skips)
  **every task whose exec has no entry in the code map**: the camera 8C0197BA, the sky 8C052FA0, the STG01 stage
  task `0C900000` and the tasks it creates in step 0 (among them the ocean task `0C909FDA`), the sound and effect
  tasks. The landtable manager 8C0519C0 is not frozen: an executor override runs its mode-0 set-up without its
  per-frame draw step 8C051D50 (a fatal stub). Each step injects pad, analog, sub, rand, camera blob and XMTRX from
  trace v2. Exit: decoded fields and raw twp/mwp/pwp/csts blobs, pointers included, identical for both traces. As
  measured, identical but for the ocean task's residual (`land_count`, `rand_state`; "Status 2026-10-20"), with
  heap pointers compared by structure from step 0 (the frozen tasks do not allocate their step-0 blocks).
* **M1b boot.** As M1a, but the world comes from libsa1's own boot and player creation at the header's start
  (-9, 16, 4), ang.y 0x6000, with DISC-mode collision (STG01 at 0x0C900000). Exit: decoded fields identical; heap
  pointers compared by structure.
* **M1c (= M1) dump geometry.** `replay_test` goes through the public path: `sa1_collision_set_static_ex` with the
  445 COL groups of `L01A0` v2 (151 collidable, 13,701 triangles; raw flags passed through; original transforms;
  FAITHFUL), `sa1_character_create(SONIC)` at the header start, then single-step replay ticks.

Commands (each from its own repository root):

```
harness/build/sa1h.exe trace --level 1 --act 0 --no-objects --input @harness/tests/sim/ec1_run_jump.txt --dump-land build/traces/land/L01A0 --trace-v2
SA1_TEST_GDI=<game.gdi> py -3.12 build.py --target all --test          # LIB: every test, incl. the replay and V7
py -3.12 tests/build_replay.py --target x86_64-windows --run --gdi <game.gdi>   # LIB: the replay alone
build/x86_64-windows/replay_test --out out ../sadc <game.gdi>             # LIB: writes out/<trace>{,.reseed,.disc}.bin
py -3.12 ../sadc/harness/tools/sa1trace.py diff ../sadc/build/traces/ref/ec1_run_jump.bin out/ec1_run_jump.bin --ulps 0
```

(Commands corrected 2026-10-20: `replay_test [--out DIR] [--land PREFIX] [--trace NAME]... [--strict] SADC [GDI]`
reads the references and the `L01A0` dump from SADC; `build_replay.py` runs the sa1trace.py diffs itself.)

M1 exit criteria:
1. The V5 reseeded replay passes for every record of both traces (the diagnostic gate, met first).
2. V6: 0 differing decoded fields in every record (mode, smode, flags, ang, pos, mspd, macc, ang_aim, pspd, pacc,
   floor_normal, dotp, attr/last_attr, timers, mj action/nframe, equipment, item, land_count, rand_state).
   Checkpoints:
   * ec1_stand: step 0 mode 1, pos (-9, 16, 4), land_count 18; step 1 pspd_y -0.0799999982; step 19 mode 1,
     pos_y 3.99764824, attr A0100001;
   * ec1_run_jump: step 30 mode 2, action 9, analog 6C15 / 0.990317225; step 150 mode 8, action 14,
     pspd_y 1.48655057; step 195 mode 2, action 12, pos (-250.923477, 1.57215452, 129.410126); step 240 pos_y 4,
     ang_y 758D.
3. Raw blobs identical with pointers compared by physical address; arena diff at steps 30 and 150 of the colobj /
   colpoly caches against `--dump-ram-at` (pointers to synthetic objects compared by structure).
4. DISC mode and FAITHFUL mode write identical outputs.
5. The x86_64-windows, x86_64-linux and x86-windows outputs are byte-identical; two runs in one process are identical.
6. MXCSR restored after every tick; 0 host allocations inside ticks; no fatal stub hit.
7. Tick time reported (target under 0.2 ms for Sonic; not a gate).

M1.1: the same for ec1_turns (480), ec1_jump_hold (322) and ec1_spindash (242).

---

## 12. Risks and open questions

### 12.1 Risks

| # | Risk | Mitigation |
|---|---|---|
| 1 | **pclower correctness and maintenance**: static initialisers with pointers, compound literals, unions with pointer members, variadic calls, `sizeof(T*)` (16 sites), address-taken locals passed as `T **`, single evaluation in stores; drift of the clang AST JSON format | Struct-wrapped `sa1_gp` (misses are compile errors), R8 fail-closed gates, version pinning (R10), V1 lowering-equivalence diff, the Phase 1 spike, the plan-B checkpoint at the Phase 2 exit |
| 2 | **Raw-integer pointer tricks** (COLOBJ_HEAD_LINK = 0 - &D_8C02D548, the CMP/PZ sign test) silently decoding into another segment | R6b raw casts; a seg 0x13 trap with its own diagnostic; the collision segment moved to 0x08-0x0B |
| 3 | **Stack escapes**: 11 sites in 6 files (`mb.model = &model` in code_8C02B620.c:121, 8C02BC60.c:128, 8C02C100.c:127, 8C02CA00.c:122; `p3c.p/.col` in code_8C043A20.c; `actlink.motionlink = &link` in code_8C04B440.c:152) plus interprocedural ones | Host-stack window (seg 0x1D); debug encode asserts on unregistered addresses |
| 4 | **NULL + offset reads** that the DC tolerated (boot-ROM zeros) now fault | World fault with the DC PC; a `PC_ONLY` NULL guard where a trace shows one; the camera is always set |
| 5 | **Fault recovery inside GMod** (VEH redirect, signal + siglongjmp) may interact with LuaJIT or the engine | The handler acts only on libsa1 text and on world/trap addresses; fallback: recovery in debug builds only, release marks the world dead without a longjmp where unsafe |
| 6 | **Overlay slot conflicts**: actions or data words pointing into a slot held by another file decode into the wrong bytes | Patch list (2.7) with placeholders and `SA1_EVENT_MISSING_ACTION`; per-kind image table (2.5) |
| 7 | **Interpreter vs hardware**: traces are exact relative to the interpreter's FSCA/FSRRA/FIPR/FTRV, amplified where floats become BAMS (atan2f x 10430.381f, SA1_FTOI) | Shared `sh4_fpmodel.h`; adopting a hardware-true model later means re-recording every reference |
| 8 | **Compiler FP drift** (folding in RN, contraction, clang's partial `-frounding-math`) | `-ffp-model=strict`, literal rewriting, one flag file, cross-target replay in CI, re-pass V6 on every compiler upgrade |
| 9 | **Collision capacities on host geometry** (NEAR 96, `hits[16]` stack overflow, TOUCH push leak, 191 objects, 8,191 polygons) | Splitting and merging, the nearest-first window, counters, PC raises behind macros (exactness tests at DC limits) |
| 10 | **Stub values change logic** (8C038D00 must return 1, 8C08AB60 0; 8C08A71E must advance; 8C09DE3A / 8C050190 unverified) | Fatal default, evidence per line, events for every unimplemented call; untraced paths get new traces in Phase 7 |
| 11 | **Untested runtime paths**: 20 boundary functions never run in the 10 traces | Dedicated `diff --sdk` generators in Phase 3b |
| 12 | **Stage-, mode- and event-dependent behaviour**: the default 0x0100, game mode 4 and the event flags change Birdie, the Knuckles radar, E-102's HUD/timer, Super Sonic availability, the 0x902/0x903 collision core and the 0x2100 kill height | Explicit per-world settings in the API; documented defaults |
| 13 | **Per-world memory**: 16 MB reserved, about 10 MB committed in copy mode | CoW in Phase 6 (2-4 MB); fine for 1-2 worlds per client, heavy for a server with many worlds |
| 14 | **One tick at a time per process** (global segment pointers) | Process mutex; TLS variant if a host needs parallel worlds |
| 15 | **Provenance**: the motion player 8C03FDC0-8C041E3E, the asm renderers 8C036BC0-8C037108 and the vector helpers 8C08593C-8C085A1C may be library code; libm coefficients read at run time tie libsa1 to US 1.005 | Owner rulings D4, D7, D13; a rewrite with the current C as oracle if ruled SDK |
| 16 | **Debuggability**: lowered code and guest pointers in the debugger | `#line` markers; natvis and GDB pretty-printers generated from `sa1g_symtab.h` |
| 17 | **Scope outside P0/P1**: B_CHAOS7, TIKAL_PROG, SBOARD/STG03/STG08 hooks, Big's fishing (15.8 KB), objects, death on hit (8C086F5E), Knuckles climb entry (8C0C9000) | Decompile queue 10.4 with phase gates |

### 12.2 Open questions

1. D1: does the owner accept GA (DC layout everywhere in libsa1, built through `pclower`) and retire portability
   principle 2 for libsa1?
2. D4: is 8C03FDC0-8C041E3E game code (ship decompiled) or Ninja code (reimplement with the same state layout)?
   The same question applies to 8C036BC0-8C037108 and 8C08593C-8C085A1C.
3. D5: is the 30 Hz pair-atomic tick acceptable for feel in GMod, or should single 60 Hz steps (with the same
   alternating sub) be exposed?
4. D6: are the interpreter models canonical, or should a real-DC or emulator characterisation be recorded (Flycast's
   hardware FSCA table is GPL, so its licence needs checking)?
5. D14: are Big's fishing, the objects (jump panels, ring scatter, item box, Zero), Eggman and Tikal in the first
   release?
6. Which level/act, game mode and upgrade flags should a GMod world report, and which upgrades does the API expose
   (B_8C799920; Big's lure flags 0x1DE-0x1E1)?
7. Collision: keep the DC capacities or raise them (to what sizes)? Accept the nearest-first GRID gather? Map
   SLIPPERY to 0x4 (0.1) or 0x8 (0.15)? Should death zones apply to every character through the API, and what should
   Knuckles' 8C0C88E6 return when no table exists?
8. What do land flag 0x100000 and slAttribute 0x800000 mean, and what step height does the 8C0742D4 re-probe climb
   (to be measured on synthetic stairs in Phase 6)?
9. Moving platforms: how do SA1's own platform objects in the overlays carry and rotate the player through their
   forcewk `call_back`s? The adapter's call_back must match.
10. Do the users of 8C09DE3A and 8C050190 (D_8C4BB1C0 / D_8C4BB1CC) tolerate NULL?
11. Do the EC1 references start from `ec1_start.ram` (the M1a precondition)? Settled 2026-10-20: M1a uses RAM
    images regenerated with `--dump-ram-at 0,1` by the same pure harness build as the references
    (`build/traces/m1_<trace>.step{0,1}.ram`), so the question no longer matters.
12. Should the particle manager 8C09DF64 (E-102 effects, barriers) and the event dispatcher 8C099A50 be decompiled
    later, or stay skipped?
13. Is a host API for CCL shapes (rings, springs, dash panels) needed in the first release?