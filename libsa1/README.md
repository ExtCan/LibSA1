# libsa1

A standalone, host-agnostic C library for the playable characters of **Sonic Adventure**
(Dreamcast, US v1.005), in the spirit of [libsm64](https://github.com/libsm64/libsm64): the
library ships **no game data** and rips everything it needs at run time from the user's own
GD-ROM dump. A host (first: the Garry's Mod module G-SA1; later Unity, Godot, Blender, a
standalone viewer) feeds it collision and input and gets posed, textured triangle meshes back.

libsa1 is independent of any host: plain C99 at the API, no dependencies beyond the C library, no
globals visible to the host (everything lives in an `sa1_context`), a flat versioned C API in
`include/libsa1.h`. The characters' logic is the game's own: the decompiled C of `../sadc`, lowered by
`sadc/tools/pclower.py` and run in a Dreamcast-layout arena per character (design:
`sadc/docs/libsa1_integration.md`).

## Status (0.3.0, 2026-11-01)

0.3.0 (round 0.9.5, `SA1_API_BOARD`): Sonic's and Tails' sand / snow board by the game's own code (`sa1_character_board`, `sa1_board_state`, `SA1_EVENT_BOARD`, `SA1_DRAW_BOARD`, `SA1_SURFACE_BOARD_SNOW` / `_SAND`, `SA1_PREVIEW_BOARD`) and the host's camera position for the game's code that looks at the camera (`SA1_INPUT_HOST_CAMERA`); the header's section on the board names every host deviation. The strict replays stay bit-exact (67 traces, V7).

### 0.2.0 (2026-10-20)

**All ten reference traces replay strictly bit-exact.** The five Sonic Emerald Coast act 1 traces (M1, M1.1),
tails_sh1, knux_rm3, amy_tp2, gamma_fe1 and big_ec3 replay through the public API identical to the reference
simulator in every decoded field and every raw player blob, in V6 (free run) and V5 (reseeded), in FAITHFUL mode
(the stage's landtable dump synthesized into the collision segment) and in DISC mode (the stage's own landtable),
which write identical records; on x86_64-windows and x86-windows with byte-identical outputs (V7). This is the gate
of `build.py --test` (strict is the default of `tests/build_replay.py`). What closed the last residuals:

* **Overlay images** (sadc `docs/libsa1_integration.md` 2.5, "Status"): `sadc/tools/pclower.py --all` lowers every
  `sadc/src/overlays/*` image (STG01, STG05, TIKAL_PROG) with its own code map (overlays that share a load slot
  have overlapping DC addresses) and per-image C names; a world activates the map of each overlay file it holds
  (the very PRS, SHA-1 compared) while it runs.
* **The stage's own tasks** (`SA1_CREATE_STAGE_TASKS`, `src/world/sa1_world_stage.c`): the world image holds the
  stage overlay in every collision mode and the boot creates the stage start's level tasks as far as the stage's
  code has C: Red Mountain's level task 0C911E0A runs as it is (its act setup creates the kill collision
  0C913028, DynamicCOL 0x1001); Emerald Coast's level task 0C900000 (asm only) has a stand-in that creates the
  decompiled ocean (the water plane DynamicCOL 0x88000002 that follows the player) and footprints (rand() after
  the player) at the same point and in the same list order as the original.
* **The particle system of the stage start** (8C09DDDA / 8C09E2DA, decompiled: the boot calls them, the level's
  lists only with the stage overlay in the image): the decompiled spawners 8C0465E0 / 8C046A08 find their effect
  lists and node pool and draw rand() as in the game (big_ec3 steps 179 and 193, the splash of ec1_spindash).
* **The particle manager in the step** (2026-10-20, integration of the particle-system decompilation): 8C09DF64 runs
  after CCL as in the original step 8C04E714 (`src/world/sa1_world_tick.c`): every particle's exec, the nodes of
  ended particles back to the pool, released lists freed, and on sub step 0 the particles drawn with the game's
  sprite draw 8C09E778 (recorded by the capture). The three particle execs (8C046480, 8C0464EE, 8C046960:
  dust and water splash) are the game's own C since 2026-10-21 (sadc `src/1st_read/code_8C046480.c` and
  `code_8C046960.c`, all MATCH, lowered with the game); the hand-written `src/rt/rt_effect.c` they replace is gone. The ten traces stay strictly bit-exact
  with it, and the pool equals the reference RAM's byte for byte (ec1_spindash steps 150 and 200: 20 live splash
  particles, then all 512 nodes free again in the same free-list order); 100,000 steps of a spin dash / jump loop at
  the shore spawn 23,180 particles without the pool running out (`tests/capture_test.c` part 8). Before, the step
  skipped the manager and a world stopped spawning effects after 512 particles.
* **The screen and the visibility test** (2026-10-20, sadc `docs/research/visibility_fidelity.md`): the reference
  simulator used to stub Ninja's njInitSystem 8C604702, so its screen stayed 0 and the game's sphere visibility test
  8C038D00 never passed; on a Dreamcast the player is on screen and the visible paths of Tails' and Knuckles'
  displays run logic (the collision shape's centre, Tails' tail morph and flight actions, counters). The references
  are re-recorded with the screen (trace header flag 8; the old ones are `sadc/build/traces/ref/<name>.noscreen.bin`).
  The boot sets the screen as njInitSystem / njInitView / 8C0544E2 do (`src/world/sa1_world_boot.c`: 640 x 480,
  aspect 1, clip rectangle (0, 0, 639, 479), distance 457.06 from njSetPerspective(0x31C7), near / far 3 / 3000),
  and 8C038D00 is the original's test in the runtime (`src/rt/rt_draw.c` section 6: 0 on sub step 1, FTRV with
  XMTRX, its two quirks; `sa1h diff --ulps 0` exact on 24,000 inputs, and with the runtime routed into the harness
  the 15 reference scripts stay identical, about 140,000 real calls). Its answer is game logic: a capture window no
  longer changes it (it used to answer "visible" inside one, 0 outside). Live worlds load the camera adapter's view
  into XMTRX before each step (`src/world/sa1_world_tick.c` sa1g_camera_view: 40 units behind the player along the
  host camera yaw), where the game's camera task leaves its view, so the displays see the player on screen as on the
  Dreamcast. Replays inject the record's XMTRX; a reference without flag 8 is replayed with the zero screen again
  (`tests/world_replay.c` wr_screen_of). Hosts draw the character whether the game's camera sees it or not: a pair
  whose visibility answers were all "not visible" is hidden (`sa1_capture_hidden`, capture version 4) and the render
  output is then the CPU pose of the game's action and frame (`tests/capture_test.c` part 9).
* The decompiled 1ST_READ fidelity gaps (particle spawners and allocator, E-102 effects, power-ups ...) are lowered
  (their stub lines are gone), Big's lure reset 8C0E91A0 has a TARGET_PC body (the lure reaches mode 1 as in the
  reference), the SDK truncation 8C63A994 (the ocean's grid snap) is in `src/rt/rt_libm.c` (bit-exact against the
  SDK in the harness), and DISC mode knows STG03, STG05 and STG10.

**Preparation on host threads, GRID undersides, the NEAR-poly query** (2026-10-21, G-SA1 requests 11 and 12; sadc
`docs/research/gsa1_ramp_freeze.md` sections 7-9):

* `sa1_collision_prepare` / `_prepare_tris` run the GRID synthesis (or the FAITHFUL landtable) on any thread, into an
  immutable, reference-counted `sa1_collision_prepared` with a collision segment of its own;
  `sa1_collision_set_prepared` installs it in O(1) (0.005-0.015 ms) and every world created afterwards maps that segment
  by reference (`sa1g_world.shared` = the prepared segment, a reference until the world is destroyed). The synthesis is
  byte-identical to the one `sa1_collision_set_static`'s first world makes (but the GRID header's generation / token
  words), runs bit-identically (Sonic / Knuckles 400 pairs, EC1 FAITHFUL from the start table), and 8 preparations on
  4 threads while the main thread ticks are byte-identical (`tests/prepare_test.c`). A preparation uses a worker context
  (`sa1_worker_ctx_create`: the host's allocator and file callbacks, no log, its own error text) and never enters a world.
* `sa1_character_prepare` (any thread) / `SA1_LOAD_WORLD_IMAGES` (in `sa1_load_disc`) compose the world images ahead:
  the 30-60 ms "respawns" G-SA1's verifier saw were the first world of each kind composing its image (1ST_READ.BIN and
  the overlays read, SHA-1 checked, PRS-unpacked) on the creating thread. The image table is filled under the world
  core's table lock (`sa1g_table_lock`), composing runs outside it.
* A new world's arena is no longer zeroed by `sa1g_world_apply_image` (`sa1g_world.fresh`: just committed, already zero):
  creating a character on prepared geometry and images now costs **p50 2.2 / p90 2.7 / max 3.4 ms** (60 creations of the
  six kinds on a 260,000-triangle map; 6.3 / 7.3 / 8.1 ms with the zeroing, 211 ms for the first creation after
  `sa1_collision_set_static`), the first pair 1.2-2.6 ms (the land cache build), `sa1_character_render` 0.1 ms,
  `sa1_character_destroy` 0.5 ms. The ten reference traces stay strictly bit-exact.
* GRID leaves out the **undersides within reach** (step c2 of `src/sa1_coladapt.c`, `sa1_collision_grid.under_reach`,
  default 12 units; `SA1_GRID_KEEP_UNDERSIDES` keeps them): a single-sided land triangle facing down that lies, at 7
  sample points, at most that far behind an upward-facing land triangle. The game's TOUCH test 8C029B00 touches a
  single-sided polygon from behind and selects, per land object, the touch with the lowest height after the move, which
  a touch from behind always is: a plank's underside, a thin platform's bottom or a slope's buried end cap then pushed
  the character out below the slab (ramp_diag `slab` 10 of 264 runs fell in round 4b, `under` 3; now 0 and 0, no dip
  deeper than 0.02 anywhere; `grid_test` thin roofs and the 40 stacked slabs are no longer known issues). Such slabs are
  one-way platforms (jump up through them from below), as G-SA1's own one-way thin platforms.
* `sa1_debug_near_polys`: the NEAR-poly list of a character's world after a tick (the raised 256-entry pool in the
  world's pcdata or the game's `B_8C73E280[96]`), its guest addresses (list, count, the `NCAP` pcdata block), the
  counters, and the entries in host space with the land object each came from.

| Area | API | Status |
|---|---|---|
| Context, host callbacks (log, alloc/free, file reader), versioning | `sa1_init`, `sa1_shutdown`, `sa1_host_defaults`, `sa1_version*`, `sa1_last_error`, `sa1_status_string` | done (0.2.0; hosts built against 0.1 are accepted) |
| Coordinate system (scale + axis matrix, winding flag) | `sa1_coords_preset`, `sa1_set_coords`, `sa1_get_coords` | done |
| Disc: .gdi, 2352/2048-byte sectors, ISO9660 single-file lookup, SHA-1 of every file, PRS | `sa1_load_disc`, `sa1_unload_disc`, `sa1_disc_file*` | done |
| Assets: Ninja object trees / basic models, NJS_MOTION + PL_ACTION tables (all overlays), weld lists, texlists + PVM/PVR, player_parameter | `sa1_character_info_get`, `sa1_action_info_get`, `sa1_physics_get` | done |
| Textures: RGBA8 per texture, per-archive atlas | `sa1_texture_archive`, `sa1_texture_get`, `sa1_texture_atlas` | done |
| Characters: create / destroy / pose (CPU mesh evaluation incl. welds), indexed + triangle-soup output | `sa1_character_create`, `_destroy`, `_pose`, `_geometry` | done |
| Game logic: one world per ticking character (design D2) running the decompiled game code; pairs from an accumulator, the camera yaw, state and mesh output, events | `sa1_character_tick`, `sa1_character_create_ex`, `sa1_character_state`, `sa1_character_state_ex`, `sa1_poll_events` (API 0.2 draft) | **the ten reference traces bit-exact** (strict, above). Sonic, Tails, Knuckles, Amy, E-102 and Big boot and tick; `SA1_CREATE_STAGE_TASKS` adds the stage's own tasks (for exactness against the game: a host level usually wants none). Tikal, Super Sonic and Eggman report `SA1_ERR_UNSUPPORTED`. The mesh comes from the game's own draws (display-pass capture, Phase 6) |
| Static collision from the host | `sa1_collision_set_static[_ex]`, `sa1_collision_info_get` | FAITHFUL (synthesized once per geometry generation into the context's collision segment, sealed) and DISC (the stage's own landtable; STG01 to STG10) feed the worlds; GRID (plain host triangles) is synthesized by the collision adapter and served through the window landtable (`src/world/sa1_coladapt_world.c`, `tests/grid_test.c`) |
| Preparation on host threads | `sa1_collision_prepare[_tris]`, `sa1_collision_prepared_retain` / `_release` / `_info_get`, `sa1_collision_set_prepared`, `sa1_character_prepare`, `SA1_LOAD_WORLD_IMAGES` | done (2026-10-21): GRID / FAITHFUL synthesis on any thread into a shared, reference-counted segment; world images ahead; `tests/prepare_test.c` |
| Diagnostics | `sa1_debug_near_polys` | the NEAR-poly list of a world after a tick (G-SA1 request 12) |
| Moving collision objects | `sa1_collision_object_*` | proxy tasks with DynamicCOL entries, refreshed each step by the collision adapter (`src/world/sa1_coladapt_world.c`, design 5.5; `tests/grid_test.c`) |
| Sound events | `sa1_poll_sound_events` | queue exists, always empty: the worlds' sound, voice, jingle and rumble triggers arrive through `sa1_poll_events` |
| World core (GUEST-ARENA: arena, guest segments, faults, world images, overlay code maps, boot, stage tasks, player creation, tick contract, replay, events) | internal `sa1g_*` (`src/world/sa1_world.h`); API glue `src/sa1_api_world.h` + `src/world/sa1_api_world.c` | done for the ten traces |

How a ticking character works: `sa1_character_create` makes the posing instance; its first
`sa1_character_tick` (or `sa1_character_create_ex` with `SA1_CREATE_WORLD`) builds its world from the .gdi
of the last `sa1_load_disc` (the pristine image of the character kind is composed once per context) and
boots it like the game's stage start: Emerald Coast act 1 by default (`SA1_CREATE_STAGE` for another), the
given start (bit for bit with `SA1_CREATE_SA1_SPACE` / `SA1_CREATE_ANGLES`, or the game's start table with
`SA1_CREATE_START_TABLE`), the context's static collision, the particle system of the stage start, and with
`SA1_CREATE_STAGE_TASKS` the stage's own tasks and death zones (the image then holds the stage overlay STGnn,
whose SHA-1 must be known). A tick runs whole 1/30 s pairs (two 60 Hz steps, B_8C754EC8 = 0 then 1, at most 8
pairs per call). `sa1_input.camera_yaw` drives the camera adapter so that a forward stick moves the character along
the camera yaw (the game's dead-zone bias of about 6 degrees is kept); `sa1_state.yaw` uses the `sa1_pose.yaw`
convention (the game's ang.y turns clockwise seen from +Y: yaw = -ang.y). The state is read from the game's own
player blobs (`sa1_state_ex`: mode, flags, speeds, timers, actions, attributes, upgrades, attach points). Builds
without the world core (CMake with `SA1_WORLD=OFF`, which defines `SA1_NO_WORLD`; MSVC) report
`SA1_ERR_NOT_IMPLEMENTED` from the tick, as in 0.1; so does a `build.py --no-game` build.

### Replay result (M1, M1.1, Phase 7)

`tests/replay_test.c` (`py -3.12 tests/build_replay.py --target all --run`, or `build.py --test`) takes the public
path per stage: `sa1_load_disc`, the stage's `--dump-land` v2 landtable (`L01A0`, `L01A2`, `L03A1`, `L04A0`,
`L05A2`, `L10A0`) through `sa1_collision_set_static_ex` in FAITHFUL mode, `sa1_character_create_ex(<the trace's
character>, SA1_CREATE_WORLD | SA1_CREATE_STAGE_TASKS)` at the trace header's start and stage, then single 60 Hz
replay steps with each record's sub step, its pad, and at player-exec entry its rand, XMTRX and camera blob (trace
v2); DISC mode the same in a second context with `sa1_collision_set_static_ex(DISC, the stage)`.

| Trace (records) | Stage, character | V6 / V5 / DISC (strict: decoded fields + raw blobs) | The stage's own tasks in the world |
|---|---|---|---|
| ec1_stand (180), ec1_run_jump (271), ec1_turns (480), ec1_jump_hold (322), ec1_spindash (242) | Emerald Coast act 1, Sonic | identical (every sa1trace.py diff with `--ulps 0`: identical) | level task 0C900000 (stand-in): the ocean 0C909F20 (water plane: land_count + 1, ec1_spindash's water physics from step 145) and the footprints 0C9061DA (rand() after the player) |
| big_ec3 (392) | Emerald Coast act 3, Big | identical | the footprints (Big's print set); the particle spawner 8C046A08 (steps 179, 193); Big's lure in mode 1 from step 1 |
| knux_rm3 (319) | Red Mountain act 3, Knuckles | identical | level task 0C911E0A (lowered STG05 C): the kill collision 0C913028 (DynamicCOL 0x1001: land_count + 1 from step 1, pwp->lclop from step 141) |
| tails_sh1 (414), amy_tp2 (381), gamma_fe1 (363) | Speed Highway 1 Tails, Twinkle Park 2 Amy, Final Egg 1 E-102 | identical | none with C (their level tasks have no C; none of them acts on the player) |

Also verified by the same run: at steps 30 and 150 of ec1_run_jump the land collision cache (17 / 19 landtable
objects with 791 / 984 polygons, plus the water plane) and the step's gather lists equal the reference RAM
(`--dump-ram-at`) by structure (M1 exit 3); the public `sa1_character_state_ex` equals the record at every step; a
second create + replay in one process is byte-identical; the output files of x86_64-windows and x86-windows are
byte-identical (V7, 30 of 30; x86_64-linux is built, not run here); the host MXCSR is unchanged after every step
and the context allocator is never called inside a step or tick; no fatal stub is reached; about 0.010-0.037 ms
per 60 Hz step on x64. `--tolerant --no-stage-tasks` (information) shows what the stage tasks contribute: the
constant land_count offset, rand_state draws ahead and ec1_spindash's water contact from step 144.

The earlier rungs (sadc `docs/libsa1_integration.md` 9.1, 11) pass as well: M0 (`world_boot_test`: libsa1's
own boot with an empty landtable, ec1_stand steps 0-16 while the reference is airborne), M1a
(`transplant_test`: the reference RAM at the start of step 0 or 1 as the arena, every task without native
code frozen) and M1b (`world_boot_test`: libsa1's boot with the DISC landtable). These boots have no stage
tasks, so they run with `--stage-tolerant` (the ocean and footprint residuals accepted).

### Tests: what each one proves

| Test | What it proves | Needs | Run by |
|---|---|---|---|
| `link_test`, `link_test_shared` | the whole archive links (`--whole-archive`) with no unresolved symbol; the code map resolves the 21 required entries (the six mains, the step functions, rt and stub entries); the shared library works through its exports | - | every `build.py` |
| `intrin_test` | `src/rt/rt_intrin.h` (the SHC intrinsics inlined into the lowered code) is bit-identical to the world core's out-of-line ones (2,262,144 comparisons) | - | every `build.py` |
| `build/link_report.txt` | the shared library exports only `sa1_*` (75 symbols on 2026-10-21, the audio API and the preparation API included); the game / runtime objects import nothing from the C library but the stack probe (design 8.1); sizes per component | - | every `build.py` |
| `unit_test [gdi [runs]]` | SHA-1, PRS, path rules, .gdi parser, PVR / PVM decoders, synthetic ISO9660 discs, coordinate / collision API, argument checks, allocation balance, fuzzing of every parser; with a GDI also hash rejection and thousands of corrupted copies of the real images | GDI optional | `build.py --test` |
| `rip_test <gdi> <outdir>` | the asset layer against the Python reference ripper (see "Asset-layer verification"), and `sa1_character_tick` = `SA1_OK` with game code, `SA1_ERR_NOT_IMPLEMENTED` without | GDI | `build.py --rip GDI OUTDIR` |
| `spike_test` | the world core: the phase 1 call sequence (heap and task init, `CreateElementalTask(7, 1, 8C0A77BC)` and 19 more calls) byte-identical to the SH-4 interpreter over the whole arena (with `build/spike/ref`), two interleaved worlds, traps, segments, guards, the stack window, encode, the code map, a fatal stub (8C011D46), `sa1g_fatal`, dead worlds, the tick contract (pairs, presses, accumulator, events), MXCSR after 100,000 pairs and after faults, internal character worlds (a Knuckles world from the disc ticks a pair) | `sadc/baserom/1ST_READ.BIN`; GDI and the interpreter reference optional | `build.py --test`; `tests/build_spike.py` makes the reference |
| `world_boot_test` | M0 (free fall, ec1_stand steps 0-16, empty landtable), M1b (libsa1's boot + DISC collision on ec1_stand and ec1_run_jump), the start table, a second boot byte-identical, 60 live pairs, the internal character API, `sa1w_world_run`'s checks (both paths refuse a world that cannot run before touching its accumulator) | GDI, the reference traces | `build.py --test` (or `src/world/build_world_tests.py --run`) |
| `transplant_test` | M1a: the reference RAM at the start of step 0 and of step 1 as the arena of a 64-bit world, replayed | the reference traces, `m1_<trace>.step{0,1}.ram` | `build.py --test` (or `src/world/build_world_tests.py --run`) |
| `replay_test` | the ten reference traces through the public API with the stage's own tasks (above), strict: V5, V6, DISC = FAITHFUL, every record bit-exact, no known issue, `sa1_character_state_ex` = record, two runs identical, MXCSR, no allocation in steps, the exit 3 caches, the live tick; every `sa1trace.py diff --ulps 0` of the outputs must say identical; V7 with `--target all`. `--tolerant` / `--no-stage-tasks`: information | GDI, the reference traces, the landtable dumps `L01A0`, `L01A2`, `L03A1`, `L04A0`, `L05A2`, `L10A0` | `build.py --test` (every trace whose reference and dump exist is the gate); or `tests/build_replay.py --run` |
| `capture_test` | the display capture and the render output (parts 1-9, header of `tests/capture_test.c`; `--part LIST` runs some): capture adds recording only (all ten references replayed with the window open are identical to the runs without it; the displays that ask the visibility test are visible on every sub step 0 from step 2 on), the captured mesh against the CPU pose and the harness's RAM, events, live ticks, textures, materials / eyes / afterimages / effects / interpolation, (part 8) the particle manager in the step over 100,000 steps (the pool never runs out, particles drawn as EFF_REGULAR sprites at their nodes) and Big's fishing displays (lure, target group, line), and (part 9) hidden displays: with the world's own view every pair is visible, with the zero screen every pair is hidden and the mesh is still the character (the CPU pose); (part `alpha`, round 0.9.1) the character renderer's vertex alpha as the game computes it (8C037872: single precision rounding toward zero, FTRC; Amy's hammer ghosts 76 / 51) | GDI, the reference traces and dumps (parts 1-5, 9); parts 3-4 the `--harness DIR` of a pure interpreter run | `tests/build_capture.py --run [--harness DIR]` |
| `ball_test` | round 0.9.1's render interpolation (header of `tests/ball_test.c`): Tails' curled body stays inside the spin ball and on its line between the pairs (the roots of rigid groups move along the screw; across a fading material the shorter turn), the whole bodies of all six characters between the pairs (spread, feet), Amy's bird interpolated in world space through sharp turns | GDI | `tests/build_ball.py --run` (`tests/build_render_tests.py all --run` with `pairs`, `decal`, `blob`) |
| `hud_test` | round 0.9.1's HUD and 2D screen output (header of `tests/hud_test.c`): the game's own HUD pass at the game's call site (Sonic: 15 sprites per pair at the game's positions, CON_REGULAR cells; the parts masks; the split leaves guest RAM as the game's single calls for Sonic, Big and a running event; without a pass only the HUD's display words differ and `sa1_render` is unchanged), replays never run it, Big's own HUD task, E-102's clock (the game's time-up, `SA1_COPT_E102_NO_TIME_LIMIT` held through game code and a time bonus, 00:00:00 respawns at 03:00:00), counters across a re-creation, Big's tension bar (`njDrawPolygon2D`) behind the gauge frame, the zero-ring blink and E-102's red timer; part 12 replays the `hud_*` and `big_drown_ec3` references with the pass and compares every 2D draw with the references' `NAME.d2d.txt` (the drowning countdown digits included) | GDI, the references with their `.d2d.txt` sidecars and landtable dumps | `tests/build_hud.py --run` |
| `coladapt_test` | the collision adapter: API validation, the synthesis layout field by field, FAITHFUL and DISC polygon / colobj records bit-identical, the lowered collision code on both landtables | GDI, the `L01A0` dump | `tests/build_coladapt.py [--game]` |
| `grid_test` | GRID collision through the public API (header of `tests/grid_test.c`): flat, ramps, stairs, walls, water, moving and rotating objects, kill planes, surfaces, capacity, stacked floors and thin roofs, the needle plank, the freespace-like plank, **underside** (the slab ramps that fell in round 4b: none falls; `SA1_GRID_KEEP_UNDERSIDES` reported; the one-way platform as the cost), the PC-only limits, GRID on EC1 against the five reference scripts | GDI, the reference traces and `L01A0` | `tests/build_grid.py --run [--target all]` |
| `prepare_test` | preparation on host threads (header of `tests/prepare_test.c`): API checks and reference counts, prepared GRID = the old path's synthesis byte for byte and run for run, EC1 FAITHFUL prepared, 8 preparations on 4 threads while the main thread ticks, `sa1_character_prepare` on a thread racing world creation, creation timing (p50 / p90 / max), `sa1_debug_near_polys` | GDI (`faithful`: `L01A0`) | `tests/build_prepare.py --run [--target all]` |
| `ramp_diag` (diagnostics, prints) | ramps, slabs, needles, stress, audio cost (header of `tests/ramp_diag.c`); `--keep-undersides`, `--legacy-split` (the GRID before round 4b), and with `tests/build_ramp_diag.py --touch-hook` every TOUCH call of a fall (section `case`) | GDI | `tests/build_ramp_diag.py --run -- --only slab ...` |

Run them all:

```
export SA1_TEST_GDI="<your game.gdi>"         # PowerShell: $env:SA1_TEST_GDI = "..."; never copied
py -3.12 build.py --target all --test
```

`--test` skips (with a message) whatever it lacks: the world and replay tests need the whole lowered game
(not `--no-game`, `--lower`, `--debug`), `SA1_TEST_GDI`, and sadc's local reference data in its ignored
`build/`: the reference traces `build/traces/ref/*.bin` (`sadc/docs/harness_simulator.md`), the landtable
dump `build/traces/land/L01A0.tri/.col` (`sa1h trace ... --dump-land`) and, for `transplant_test`, the RAM
images `build/traces/m1_<trace>.step{0,1}.ram` (a private pure harness build with `--dump-ram-at 0,1`, see
`src/world/build_world_tests.py`). `world_boot_test` and `transplant_test` run with `--stage-tolerant` (their boots
have no stage tasks: the ocean and footprint residuals accepted); the replay test is strict.

Supported disc: Sonic Adventure (US) v1.005, `1ST_READ.BIN` SHA-1
`e74c849806db103aa87f84e41a382873bc96b6df`. Other builds move every address and need their own
manifest (`src/sa1_manifest.c`).
## Building

Reference build: zig through Python (`pip install ziglang==0.16.0`: the version `sadc/tools/pclower.py`
pins, R10; `build.py` refuses another one unless `--allow-unpinned`), three targets. It needs the sadc
tree next to libsa1 (`--game DIR`, default `../sadc`).

```
py -3.12 build.py                       # x86_64-windows, x86-windows, x86_64-linux with the whole game (release)
py -3.12 build.py --target x86_64-windows --test      # + unit, world and replay tests (see "Tests")
py -3.12 build.py --target x86_64-windows --rip "<game.gdi>" out/
py -3.12 build.py --debug               # -O0 + UBSan (trap mode) for the asset layer / world into build/<target>-debug
py -3.12 build.py --werror --clean
```

What one `py -3.12 build.py` does (about 4 minutes cold on 20 cores: pclower 2, compiling 3 targets 1.5; 10 s
when nothing changed; several builds wait for each other on `build/.build.lock`):

1. **Lowering.** `sadc/tools/pclower.py --all --profile ga64 --stub-unresolved` into `build/pclower/`: all
   554 TUs (`src/1st_read`, every overlay image of `src/overlays` (STG01, STG05, TIKAL_PROG) and this library's
   runtime `src/rt`), the code map `sa1g_fntab.c` (1,946 entries in the base map and the STG01 / STG05 /
   TIKAL_PROG maps), the R4 pcdata placement `sa1g_pcdata.c`, the layout asserts
   `sa1g_layout_asserts.c`, `report.json`. Cached: it runs again only when an input changes (pclower and
   gen_native, `pc_cflags.txt`, the symbol files, every file of sadc `include/`, `src/1st_read/`,
   `src/overlays/`, `config/overlays/`, `src/rt/`, the zig version; `build/pclower/inputs.sha1`); `--relower`
   forces it. The output is reproducible (a second run is identical but for its own path).
2. **Boundary stubs** (design 10.5): `gen/gen_stubs.py` turns `gen/policy/stubs.tsv` (one policy and its
   evidence for each of the 217 functions pclower stubs: fatal 107, void 64, event 20, ret 11, freetask 9,
   address-only 5, null 1; 3 of them in the STG05 image) into `build/gen/sa1g_stubs_policy_*.c`, which replaces pclower's
   all-fatal
   `sa1g_stubs.c`, plus `sa1g_rtglue_*.c` (`njFraction` = rt's njRandom 8C63A980, `UNK_PcWrite` fatal). Fatal =
   the DC address is recorded and the tick returns `SA1_ERR_NOT_IMPLEMENTED` (`SA1_EVENT_UNIMPLEMENTED`
   queued); events go to the world's event queue. A stub without a line is fatal and reported
   (`--strict-stubs`: an error). See `gen/README.md`.
3. **Compilation** per target, incremental (command line + depfile): the asset layer `src/*.c` and the world
   core `src/world/*.c` + `src/rt/rt_fsca_q.c` (static and shared variants), and once the game objects: the
   lowered TUs, code map, pcdata, layout asserts and stubs with the one PC flag set (`sadc/tools/pc_cflags.txt`,
   design 8.1, whatever `--debug` says) into the game-text section (`-include src/world/sa1g_text.h`) and with
   the SHC intrinsics inlined (`-include src/rt/rt_intrin.h`: `_builtin_fipr/ftrv/fsrra/sqrtf/fabsf/fsca` on
   `pc/sh4_fpmodel.h` and the world's XMTRX).
4. **Link + link test**: `libsa1.a`, the shared library (Linux with `--no-undefined`), the tests, `link_test`,
   `link_test_shared` and `intrin_test` (see "Tests"); `build/link_report.txt` and
   `build/<target>/size_report.txt`.
5. **Tests** (`--test`, runnable targets): `unit_test`, `spike_test`, then `world_boot_test` and
   `transplant_test` (built by `src/world/build_world_tests.py`) and the replay test (`tests/build_replay.py
   --run`; `--target all` adds the V7 comparison) when their inputs exist.

Outputs in `build/<target>/`: `libsa1.a` (static; define `SA1_STATIC` when including the header; game
symbols are hidden from the shared libraries, not from the archive), `sa1.dll` + `libsa1.dll.a` (Windows)
or `libsa1.so` (Linux), `unit_test`, `rip_test`, `rip_test_shared`, `spike_test`, `link_test`,
`link_test_shared`, `intrin_test`; with `--test` also `world_boot_test`, `transplant_test`, `replay_test`
(outputs in `build/replay/<target>/`).

Sizes (2026-10-20, x86_64-windows, text unless noted): lowered game code 763 KB (522 objects), lowered runtime
34 KB, code map 98 KB of tables (1,897 entries in 4 maps), stubs + runtime glue 7 KB, world core 50 KB + 69 KB of
tables (the 66 KB FSCA table), asset layer + API 103 KB; `sa1.dll` 1.44 MB on disk, exporting 39 `sa1_*` symbols (75 since the audio and preparation APIs).

Other game-code choices:

```
py -3.12 build.py --no-game                          # world core without game code (ticking -> NOT_IMPLEMENTED)
py -3.12 build.py --lowered DIR                      # a pclower profile directory made elsewhere (DIR/lowered/*.c ...)
py -3.12 build.py --lower spike [--with-rt]          # only some sadc TUs, into build/pclower-subset
py -3.12 tests/build_spike.py --tag <yours>          # phase 1 spike: interpreter reference + all targets + tests
SA1_TEST_GDI="<game.gdi>" build/x86_64-windows/spike_test - build/spike/ref   # world image from the disc
```

`build/<target>` is shared by everyone working on libsa1: `--no-game`, `--lowered` and `--lower` replace the
game code in `build/<target>/libsa1.a` until the next full build.

Hosts that build with their own compiler can use `CMakeLists.txt` (project version 0.2.0; tested with MSVC
2026 /W4 and with zig's clang through CMake + Ninja):

```
cmake -S . -B build/cmake-x64 -A x64 && cmake --build build/cmake-x64 --config Release
```

Without options this is the asset layer and the API without the world core (`SA1_WORLD=OFF` defines
`SA1_NO_WORLD`; MSVC defines it by itself): posing works, ticking reports `SA1_ERR_NOT_IMPLEMENTED`.
`-DSA1_WORLD=ON -DSA1_SADC_DIR=<sadc>` adds the world core (GCC/Clang only). With
`-DSA1_GAME_CMAKE=<libsa1>/build/gen/sa1g_game.cmake` (written by `build.py`) it also links the lowered game
code, compiled with the same flags (Clang only; tested with zig's clang through CMake + Ninja: exports only
`sa1_*`, unit_test passes). The certified binaries are `build.py`'s.

## Using it

```c
#include "libsa1.h"

sa1_context *ctx;
sa1_host host;
sa1_host_defaults(&host);
host.log = my_log;                                   /* optional */
sa1_init(&host, &ctx);                               /* default coords: Source, 4 HU per unit */

sa1_status st = sa1_load_disc(ctx, "C:/games/sa1/Sonic Adventure.gdi", NULL);
if (SA1_FAILED(st)) { puts(sa1_last_error(ctx)); }

sa1_character_info info = { sizeof info };
sa1_character_info_get(ctx, SA1_CHAR_SONIC, &info);

sa1_character_id sonic;
float pos[3] = { 0, 0, 64 };
sa1_character_create(ctx, SA1_CHAR_SONIC, pos, 0.0f, &sonic);

/* caller-owned triangle soup, libsm64 style */
sa1_mesh mesh = { sizeof mesh };
mesh.max_triangles = info.max_triangles;
mesh.position = malloc(9 * sizeof(float) * info.max_triangles);   /* NULL streams are skipped */
mesh.uv       = malloc(6 * sizeof(float) * info.max_triangles);
mesh.texture  = malloc(sizeof(int16_t) * info.max_triangles);
sa1_pose pose = { sizeof pose };
pose.action = 12; pose.frame = 8.0f;                 /* Sonic's run cycle */
pose.position[2] = 64; pose.yaw = 1.57f;
sa1_character_pose(ctx, sonic, &pose, &mesh);        /* puppet / remote path */

const sa1_geometry *g;                               /* or the indexed form, library-owned */
sa1_character_geometry(ctx, sonic, &g);

sa1_texture_info tex = { sizeof tex };
if (mesh.texture[0] >= 0)                            /* -1 = untextured */
    sa1_texture_get(ctx, SA1_CHAR_SONIC, 0, (uint32_t)mesh.texture[0], &tex);   /* RGBA8 */

/* a ticking character in the game's own logic, on the stage's own collision (DISC; GRID synthesis
   from host triangles is not done yet, such a world has no land) */
sa1_collision_static col = { sizeof col };
col.mode = SA1_COLLISION_MODE_DISC;
col.disc_level = 1; col.disc_act = 0;                /* Emerald Coast act 1 */
sa1_collision_set_static_ex(ctx, &col);
sa1_character_desc d = { sizeof d };
d.kind = SA1_CHAR_SONIC;
d.flags = SA1_CREATE_WORLD | SA1_CREATE_START_TABLE; /* build + boot now, the game's start position */
sa1_character_id me;
st = sa1_character_create_ex(ctx, &d, &me);
sa1_input in = { sizeof in };
in.stick_y = 1.0f;                                   /* forward ... */
in.camera_yaw = 0.0f;                                /* ... along the camera yaw (sa1_pose.yaw convention) */
sa1_state s = { sizeof s };
st = sa1_character_tick(ctx, me, &in, frame_seconds, &s, &mesh);   /* whole 1/30 s pairs */
sa1_event ev[32];
uint32_t nev;
sa1_poll_events(ctx, ev, 32, &nev);                  /* sounds, voices, death, unimplemented calls ... */
sa1_shutdown(ctx);
```

Preparation on a host worker thread (`SA1_API_PREPARE`; the context's allocator and file callbacks must be thread-safe,
the defaults are):

```c
/* worker thread: the map's GRID synthesis (0.1-0.4 s for a big map) and the world images */
sa1_collision_prepared *prep = NULL;
sa1_status st = sa1_collision_prepare_tris(ctx, tris, ntris, NULL, &prep);   /* host space, GRID defaults */
if (SA1_FAILED(st)) {                                  /* the object holds the failure */
    sa1_collision_prepared_info pi = { sizeof pi };
    sa1_collision_prepared_info_get(prep, &pi);        /* pi.message */
}
sa1_character_prepare(ctx, 0);                         /* every kind that ticks (or SA1_LOAD_WORLD_IMAGES at load) */

/* the thread that ticks, when the worker is done: O(1); creations then cost about 2-3 ms */
sa1_collision_set_prepared(ctx, prep);                 /* the context takes its own reference */
sa1_collision_prepared_release(prep);                  /* worlds keep theirs until they are destroyed */
```

## Render output (API 0.2 render draft, `SA1_API_RENDER`)

`sa1_character_render(ctx, id, alpha, &render)` gives what the game drew in the last pair as draws in the game's
order (`include/libsa1.h` documents every rule): triangles of the character's renderer and of effect models with their
texture named by archive and index (effect textures resolve through the draw's texlist into the character's effect
archives: SON_EFF, KNU_EFF, E102BEAM, ...), the material word, blend factors, flags (`SA1_DRAW_BLEND`, `ALPHA_TEST`,
`USE_ALPHA` drawn opaque, `PALETTE_LIT`, `UNLIT`, `ENV_MAP`, `DOUBLE_SIDED`, `AFTERIMAGE`, `EFFECT`), sprites
(njDrawSprite3D and the game's 3D sprite draw 8C09E778, now recorded) and line segments (njDrawLine3D); with alpha
0..1 (or -1: the library's accumulator) the vertices are interpolated between the last two pairs (round 0.9.1,
`src/sa1_character.c` above `ANCHOR_MAX`: the root of a rigid group moves along the screw between its two transforms, so
Tails' curled body stays inside the spin ball; a draw matched across a fading material turns the shorter way; a
companion such as Amy's bird is interpolated in world space; the character renderer's vertex alpha is the game's own
arithmetic, truncated: Amy's hammer ghosts 76 / 51).
`sa1_character_mesh_lerp` writes the same into an `sa1_mesh` (new optional streams `archive`, `draw_flags`, `order`,
`blend`). Findings behind it (2026-10-20): the character renderer 8C037098 lights from the stage's light palette and
uses only the material alpha (the material diffuse is not a vertex colour: Sonic's irises have a black diffuse); many
character materials set USE_ALPHA with nothing translucent and rely on the Dreamcast's per-pixel sorting (Sonic's
irises are drawn before the eye whites they sit 0.05 units in front of); Tails' display draws the whole body three
times under fading constant materials for the tails' afterimages (the body copies are left out). Emitted effects: Sonic's
spin dash ball / trail (SON_EFF models, additive), Knuckles' punch sprite, E-102's targeting lines, Tails' tail trail
polygons, Amy's sprites; since 2026-10-20 the particles of the particle manager 8C09DF64 (dust, water splashes:
sprites whose textures resolve into the common archive EFF_REGULAR, see "What the loader does") and Big's fishing
displays (the lure 8C0E7720 / 8C0E78C0 and the cast cursor's target circle and cursor 8C0E66C4 with FISHING textures,
the fishing line 8C0EC180 as line segments; the line's points stay at the origin of its buffer until the rope
simulation 8C0EBFA8 / 8C0EC0A8 is decompiled, its HUD gauges are 2D and not captured). Not emitted: the other display
stubs of `gen/policy/stubs.tsv`. Tests: `tests/capture_test.c` parts 6, 7 and 8, `tests/texdump.c` (textures and
software renders as PNG into `build/texdump/`), `py -3.12 tests/build_capture.py --run --texdump`. The draws are the
game's own, and the game draws a character only when its sphere visibility test says it is on screen (8C038D00, game
logic): when every answer of a pair is "not visible" (the world's view does not see it; the camera adapter's view
of live worlds always does) the render output is the CPU pose of the game's action and frame instead, so hosts always
have the character (`tests/capture_test.c` part 9).

## Host objects (API 0.3 draft, `SA1_API_HOSTOBJ`): the game's rules a host meets

`sa1_hostobj_*` mirrors host objects (NPCs, props, balloons, hazards) into every ticking world as real game tasks with
the game's own collision (CCL) shapes, so the decompiled player code attacks, homes onto, locks onto, lifts and gets hurt
by them natively (design: sadc `docs/libsa1_interaction.md` 2-6 and its section 7, "Corrections"; every rule in
`include/libsa1.h`). Tests: `tests/hostobj_test.c` (`py -3.12 tests/build_hostobj.py --run`), and the 14 HOSTOBJ
references of `tests/build_replay.py` (recorded by sadc's harness running this layer on the original code), replayed
strictly by `build.py --test`. The game rules behind the results, as the code has them (round 5, 2026-10-21):

* **Who an object's touch hurts.** The body levels of the characters (attack / defence, SonicMain
  sadc `src/1st_read/code_8C0A77BC.c:143-173` and the other mains): the ball (jump, roll, the homing attack) 1/1; **the spin dash is the
  ball too: Sonic's modes 4 (charge) and 5 (roll), 1/1**; only the light speed dash (Sonic's mode 6, 1/3), the Light
  Speed Attack (mode 44, 2/3), invincibility and Super Sonic (3/3) have defence 3. A touch hurts when its level is
  above the defence (the game's 8C02FA20) and the character is not invulnerable: touch 1 is stopped by any defence
  (the ball included), **touch 2 (HAZARD) hurts a spin-dashing or jumping Sonic**, and touch 3 is stopped only by
  defence 3 (those four). `SA1_ATTACK_SPIN_DASH` reports Sonic's modes 4, 5 and 6.
* **Holding.** A HOLDABLE is lifted by the game's own action button and hand path; throws carry the game's speeds
  (fixed 517 HU/s at 21.8 degrees, aimed 720 HU/s at the nearest target; Big 1018 / 960 HU/s), a put-down has speed 0,
  and **a hurt while carrying drops the object without speed** (the drop asks the object's class after clearing the
  carry flag and while the hurt flag is set, so the game's P2G(1, 1, 0) is not written: `SA1_RELEASE_DROP`, speed 0;
  a host's forced release gives speed 0 as well). Held objects are in no CCL list; removal, `sa1_hostobj_release`, a
  kind change or a world mask run the holder's own drop function first.
* **Damage to the character** (`sa1_character_damage`, the options NO_DEATH / KEEP_RINGS / NO_DAMAGE): the game's hurt
  path decides (120 steps of invulnerability, barrier, ring scatter, death at 0 rings); `SA1_EVENT_HURT` reports it and
  `SA1_EVENT_DEATH` (arg `SA1_DEATH_OTHER` / `HURT` / `HOST`) follows a fatal hit in the same step.
* **E-102.** LOCKED / TARGETED / UNLOCKED come from the game's markers; a hit by its shot releases the marker at once,
  and a `SA1_HOSTOBJ_REMOVE_KILLED` removal of a counted target pays the game's time bonus (8C0D5064) once.
* **Balloons** launch the character through the game's spring request (5 SA1 units per step, 1200 HU/s up), or keep
  the ball with `SA1_HOSTOBJ_LAUNCH_BOUNCE`; a projectile only pops them. The homing attack does not take a target that
  is above the character when it starts (the game's 8C0AF280).

## Screen / HUD (API 0.4 draft, `SA1_API_SCREEN`, round 0.9.1)

`sa1_character_screen` hands out the game's own 2D draws of a character's world on the Dreamcast's 640 x 480 screen
(rules in `include/libsa1.h`, design sadc `docs/research/gsa1_070_hud.md` with its Review): the HUD made by the game's
own functions (`8C08A4BA` rings / lives, `8C08A724` the time) where its frame steps call them, after a pair's two logic
steps and before the frame advance (`src/world/sa1_world_hud.c`, split into the parts of `SA1_COPT_HUD_PARTS` with the
game's own visibility flags after the wrapper's own test), and the 2D draws of the world's tasks (Big's HUD, his reel,
gauge and tension bar, E-102's counters, the drowning countdown digits). The capture keeps each 2D draw's priority, the
running exec and the polygon draws (`src/rt/rt_draw.c`: `njDrawSprite2D`, `njDrawPolygon2D` 8C63EBC8);
`src/sa1_screen.c` emits them in the Dreamcast's depth order (opaque, then blended, each from the most negative priority
up) with textures from CON_REGULAR (a common archive of every character set since round 0.9.1; `SA1_MAX_ARCHIVES` 12)
and the characters' own archives. The game clock is a host choice (`SA1_COPT_CLOCK`; worlds boot with it stopped, as the
references), E-102's time limit can be held (`SA1_COPT_E102_NO_TIME_LIMIT`), and `sa1_character_set_time` /
`sa1_character_set_lives` carry the counters into a new world (`sa1_state_ex.time_*`). Not in libsa1 worlds: the
game camera's underwater tint, Tails' race bar, Knuckles' emerald radar, the lens flare. Tests: `tests/hud_test.c`
(`py -3.12 tests/build_hud.py --run`), the HUD references in `tests/build_replay.py` (strict, the pass off).

## Conventions

**Coordinates.** SA1 space is right-handed, +Y up; character models face -X in model space and
the game turns them by 180 degrees about +Y, so a character with yaw 0 faces SA1 world +X.
Host space is `host = scale * (axis * sa1)`.

* Default `SA1_COORDS_PRESET_SOURCE`: scale **4.0 Hammer units per SA1 unit**, axis rows
  `[1,0,0] [0,0,-1] [0,1,0]`, i.e. `host = 4 * (x, -z, y)`: SA1 +X -> Source +X (forward),
  SA1 +Y -> Source +Z (up), SA1 +Z -> Source -Y. det = +1 (no mirroring), so handedness and
  winding are preserved and `pose.yaw` is an ordinary Source yaw (counter-clockwise about +Z).
  A posed character at yaw 0 therefore maps model space to `4 * (-x, z, y)`.
* `SA1_COORDS_PRESET_NATIVE`: scale 1, identity (raw SA1 units, as stored on the disc).
* Any orthonormal axis matrix and scale can be set; collision input is converted back with the
  inverse. Triangles are counter-clockwise seen from the front (right-handed); set
  `SA1_COORDS_FLIP_WINDING` for hosts with clockwise front faces.
* UVs are texture-relative (1.0 = one texture width), origin top-left, v downwards; character
  materials clamp or tile per texel (`SA1_MAT_CLAMP_U/V`), so the atlas does not remap UVs
  (`sa1_texture_atlas` returns the rects for hosts that want it).

**Angles of ticking characters.** The game's `taskwk.ang.y` (BAMS, 0x10000 = one turn) moves the character
along (cos a, 0, sin a): a clockwise turn seen from +Y. Host yaws (`sa1_pose.yaw`, `sa1_state.yaw`,
`sa1_character_desc.yaw`, `sa1_input.camera_yaw`) are counter-clockwise, so yaw = -ang.y. The camera adapter
gets ang.y = camera_yaw - 0x4000 (BAMS): the game's analog conversion 8C05117E subtracts the camera ang.y from
the stick angle (forward = -0x4000), so a forward stick turns the character to the camera yaw.
`sa1_state_ex` also gives the raw game values (SA1 space, units per 60 Hz step).

**Struct versioning.** Every struct passed across the API starts with `struct_size`; the
library accepts older (smaller) structs and fills outputs up to the caller's size.

**Ownership.** Textures and indexed geometry are owned by the context; `sa1_mesh` buffers are
owned by the caller. A context is single-threaded; separate contexts are independent (one world ticks at
a time per process: a process lock).
## What the loader does

1. Validates the `.gdi` path (UNC `\\server\share`, `//server`, `\\?\`, `\\.\`, `\??\`, DOS
   device names and alternate data streams are rejected), parses the `.gdi`, and accepts track
   files only as **bare file names in the `.gdi`'s directory**. Audio tracks are never opened.
2. Reads 2352-byte (Mode 1 at +16, Mode 2 Form 1 at +24) or 2048-byte sectors; the ISO9660
   filesystem is taken from the last data track (the high-density area); single files are looked
   up by path, nothing is extracted.
3. Hashes every file it reads (SHA-1). `1ST_READ.BIN` must match; overlays and texture archives
   must match the manifest unless `SA1_LOAD_ACCEPT_MODIFIED` is set.
4. PRS-decompresses overlays and archives (output capped at 16 MB) and maps `1ST_READ.BIN` at
   `0x8C010000` and each overlay at its load address. Several overlays share an address
   (`*_MOT` at `0x0CC00000`; `STGxx`, `MINICART`, `B_CHAOS7` at `0x0C900000`), so pointers are
   resolved per character in an ordered *view* of images, and actions that live in a
   shared-address overlay are resolved in exactly that overlay. A pointer into an overlay that
   is not loaded marks the action "not loaded" and is never followed. Cross-image pointers
   (S_MOT motions on 1ST_READ objects, B_CHAOS7 objects with S_MOT motions) work.
5. Deserializes into native structures (no raw-memory casts; every pointer bounds-checked
   against its image; counts capped; vertex indices checked; floats must be finite; tree
   traversal is iterative with depth/node caps and cycle detection).

Files read per character (default flags `SA1_LOAD_BOARDS | SA1_LOAD_SUPER`):

| Character | Images | Texture archives (from the game's PVM list `0x8C1C2584`) | Actions available (default / `SA1_LOAD_ALL`) |
|---|---|---|---|
| Sonic | 1ST_READ, S_MOT, S_SBMOT, B_CHAOS7 | SONIC, SON_EFF | 119 / 142 of 142 |
| Eggman | 1ST_READ | EGGMAN | 24 / 24 of 24 |
| Tails | 1ST_READ, M_SBMOT | MILES | 131 / 132 of 132 |
| Knuckles | 1ST_READ, K_MOT | KNUCKLES, KNU_EFF | 110 / 111 of 111 |
| Tikal | 1ST_READ, TIKAL_PROG | TIKAL | no action table |
| Amy | 1ST_READ, A_MOT | AMY, AMY_EFF | 97 / 98 of 98 |
| Gamma | 1ST_READ, E_MOT | E102, E102BEAM, E102EFFECT, E102TIME, E102HIT | 86 / 87 of 87 |
| Big | 1ST_READ, B_MOT | BIG, FISHING, BIG_KAERU | 93 / 94 of 101 (7 empty) |
| Super Sonic | 1ST_READ, B_CHAOS7, S_MOT | SUPERSONIC | 96 of 142 (Sonic's table) |

Round 0.9.5 (the board): with `SA1_LOAD_BOARDS`, Sonic also gets the archives **MIW_B001** and **SB_BOARD1**, Tails
**MTX_BOARD0**, after every other archive (no index of an existing archive changes): the three standalone board textures
`SONICADV/MIW_B001.PVR` (10,952 bytes, SHA-1 `7eb21e78fa9d54ef00029043be91285bdde1de32`), `SB_BOARD1.PVR` (10,956,
`d851ade3e5d8b610790a25aca1e8960aeae9bde6`) and `MTX_BOARD0.PVR` (10,956, `f41d0bf31bc1af46cf683bc55343161d519fee7a`),
one 64 x 64 texture each, named by the texlists the board tasks set (`0x8C1C6380`, `0x0CB36F94`, `0x0CB025A0`).

Every character also gets the common archive **EFF_REGULAR** last (from the stage start's common PVM list
`0x8C1C2450`, 138 textures): the particle lists' texlists (dust, water splash, sparks: drawn by the particle manager
8C09DF64 with the game's sprite draw 8C09E778) are slices of its texname array, so the display capture resolves their
sprites into it (`sa1_capture_resolve`).

`SA1_LOAD_ALL` adds MINICART (one cart action per character) and Sonic's stage overlays
STG02/04/06/07/08/09. The worlds of ticking characters compose their own arena images from the same disc
(see "World core").

**Mesh evaluation** (`sa1_pose.c`: posing, puppets, and the tick's mesh until the display-pass capture of
Phase 6). Node transform `T * Rz * Ry * Rx * S` (column vectors; `ZXY_ANG`: `T * Ry * Rx * Rz * S`), parent
times local, `UNIT_POS/ANG/SCL`, `HIDE`, `BREAK`; motion keys replace a node's pos/ang/scl where a track
exists. Linear key interpolation (every player motion is linear); a frame past the last key interpolates
towards the first key at `nbFrame` (loop closure). Strip winding: triangle *j* swaps its first two corners
when *j* is odd XOR the strip's reversed bit; quads are `(0,1,2)+(2,1,3)`; degenerate strip triangles are
dropped. Every model vertex is transformed into a per-instance world-space buffer, then the
`PL_JOIN_VERTEX` welds run in table order (type 1 average, 2 dst := src, 3 src := dst; types 4-9 give attach
points), then drawn nodes are emitted. Still provisional in this evaluator (the game's own path replaces it
when the display pass is captured): angle keys interpolated as plain integers (sa1_assets.md open question
2), attach points (weld types 4-9) at the origin of the source node, Super Sonic as Sonic's action table with
his own main / jump-ball objects, the loop-closure rule, and the C library's trig instead of the FSCA table.
A ticking character's `sa1_state_ex` attach points come from the game's own weld evaluation.

## Asset-layer verification (US v1.005 GDI, this machine)

`rip_test <gdi> <outdir> [--compare <rip_character.py output>]` writes `<char>.obj/.mtl`,
`tex_NN_<name>.png` and `atlas_<ARCHIVE>.png/.txt` for every character and checks:

* bind-pose counts against `sadc/tools/research/rip_character.py` (nodes, vertices, triangles,
  UVs, texture ids used / in the PVM): **all 9 characters identical**, vertex positions within
  6e-6 (the PoC prints 5 decimals); 9 posed cases (incl. the 74-node curled model and S_MOT,
  A_MOT, B_MOT motions) identical to the same precision;
* all 308 textures pixel-identical to the PoC's PNGs (every PVR layout used by the characters) - **superseded
  2026-10-20**: both shared a wrong mipmap layout (6 bytes of padding before the 1x1 level of twiddled 16-bit
  textures instead of 2: the full-size level of every mipmapped texture was read 2 texels off in twiddled order, the
  "noisy" textures of the first in-game test). `tests/texdump.c` + `tests/texdump_ref.py` now check every texture of
  every character archive (492) texel for texel against an independent decoder written from the PVR format, which
  agrees with SA Tools' ArchiveTool on all 782 character, effect and common textures, and the texlist names against
  the PVM entries; `tests/capture_test.c` part 6 checks every mipmapped texture against its own half-size level;
* physics entries (max_x_spd, rush_speed, rad, height, weight) for all 8 table entries;
* weld effect in the bind pose: Sonic's 52 welded vertices move by 0.047 units on average,
  matching the independent measurement in `sa1_assets.md` 5.5;
* the Source axis map, the 180-degree world turn and yaw/position transforms;
* posing every available action at every frame (20,774 poses with `--load-all`): no failures,
  about 0.015-0.02 ms per pose (x64 release, welds on).

Timings (x64 release): `sa1_load_disc` of all 9 characters (28 files, 531 motions, 21 texture
archives, about 12 MB of native assets) 50-60 ms with a warm file cache, 170 ms on the first run; 300 ms
with `SA1_LOAD_ALL` (35 files). The Python PoC takes 180-370 ms per character for the bind pose alone.

`unit_test` is described under "Tests"; run it in the `--debug` (UBSan) build too. Tested on x86_64-windows,
x86-windows (zig) and x64 MSVC; x86_64-linux is compiled and linked (not run: no Linux host here).

## World core (GUEST-ARENA, `src/world`)

Design: `sadc/docs/libsa1_integration.md` (sections 2, 5, 6, 7.4, 8, 9; its "Status 2026-10-20" section
lists what is built). A **world** runs the decompiled game code, lowered by `sadc/tools/pclower.py` (profile
`ga64`), in a 16 MB "DC RAM" arena laid out like the Dreamcast's. Pointers in memory are 32-bit guest words;
`sa1/guest.h` decodes them through a per-process segment table that the world installs while it runs.

| Segment | Guest | What |
|---|---|---|
| 0x00 | 0 | NULL stays NULL; NULL + offset hits the never-mapped low 64 KB |
| 0x08-0x0B | 0x88000000 | static collision segment, 64 MB reserved, shared by the worlds of a context, bump-allocated, read-only once sealed (the FAITHFUL landtable) |
| 0x0C | 0x0C/0x8C/0xAC000000 | the arena (16 MB, 64 KB no-access guards) |
| 0x13 | 0x73.../0xF3... | trap for negated arena pointers (`COLOBJ_HEAD_LINK`), own reservation and diagnostic |
| 0x18 | 0x98000000 | per-world extension: R4 pcdata first (`sa1g_pcd_size()`), then world objects (action placeholders, the window landtable ...), committed on demand |
| 0x1D | 0x9D000000 | host-stack window `[entry frame - 16 MB, entry frame)`, open only while the world runs |
| others | | no-access trap reservations: one per segment on 64-bit hosts (the fault names the segment), one shared on 32-bit |

* **Images** (`sa1_world_image.c`): `sa1g_image_compose` reads 1ST_READ.BIN (SHA-1 must be US v1.005)
  and the kind's overlays from the disc through the disc layer (`sa1_disc.c`, `sa1_prs.c`, manifest hashes),
  one occupant per shared slot: Sonic / Super Sonic = 1ST_READ + S_MOT + S_SBMOT + B_CHAOS7, Tails + M_SBMOT,
  Knuckles + K_MOT, Amy + A_MOT, E-102 + E_MOT, Big + B_MOT, Tikal + TIKAL_PROG; DISC collision uses the
  kind's image with the stage overlay (STG01 for Emerald Coast) in place of B_CHAOS7. The patch list (2.7)
  replaces every `PL_ACTION.actptr` whose NJS_ACTION is NULL, outside the image, or in a slot held by another
  file with a 1-frame placeholder action on the root object in segment 0x18 (Sonic 23, Big 8,
  Tails/Knuckles/Amy/E-102 1, Tikal 0; each patch is logged). `sa1g_world_apply_image` zeroes the arena,
  copies the spans, places pcdata, applies the patches and runs `sa1g_pcd_init` in the world. No game byte
  is in any source file. Every span records the SHA-1 of the file it came from.
* **Overlay code maps** (design 2.5): pclower's `sa1g_fntab.c` has a code map per overlay image
  (`sa1g_fnimages[]`: STG01, STG05, TIKAL_PROG, with the PRS file and SHA-1 their C was decompiled from and their
  code range), because overlays that share a load slot have overlapping DC addresses. `sa1g_world_apply_image`
  lists the images whose file the world holds (name and SHA-1 equal) in `w->fn_images`; while the world runs,
  `sa1g_fn_active` holds them and every code-map lookup (`sa1_fn`, `sa1g_fn_lookup`, `sa1_fn_name`) tries them by
  code range before the base map. A base stub at an overlay-slot address dispatches to them too
  (`gen/README.md`). Transplanted RAM images have only the base map.
* **Boot and player creation** (`sa1_world_boot.c`, design 6.2 / 6.3): runtime (njInitMatrix, screen,
  srand), heap 8C098A10 and tasks 8C09844A, collision init 8C028E60 and the CCL counters, the tick-contract
  globals of 6.5, the camera adapter task (whose taskwk is camera_twp), the landtable manager's set-up with
  an EMPTY, DISC or adapter landtable, LoadPlayerTask's work for player 0 (the six mains), the death-zone
  task with DISC collision, the particle system (8C09DDDA, the effect lists through 8C09E2A0); the land cache is
  built at the end of step 0, as at the game's stage start.
  The replay executor walks the task lists like 8C0988A4 and injects a trace record's player-entry state;
  `sa1g_world_transplant` takes a reference RAM image instead (M1a).
* **Creation cost** (2026-10-21): a world is a fresh 16 MB arena (committed, zero) plus the kind's image copied in
  (`sa1g_world_apply_image` no longer zeroes a fresh arena: `sa1g_world.fresh`), the boot and the collision adapter's
  attach: about 2 ms when the kind's image is composed (`sa1_character_prepare`) and the static geometry is prepared
  (`sa1_collision_set_prepared`: the world maps the prepared segment, `sa1g_world.prepared` holds a reference).
* **Tick contract** (`sa1_world_tick.c`): `sa1g_world_pair` runs two 60 Hz steps with `B_8C754EC4 = 2` and
  `B_8C754EC8 = 0, 1`, delivers host presses made between pairs to sub 0, ORs the press bits of
  `per[0]`/`per[1]` across the pair and writes them back (8C04E95A), then the optional display pass;
  `sa1g_world_tick` is the 1/30 s accumulator (at most 8 pairs per call, the backlog is dropped and counted);
  `sa1g_world_step` is the single replay step with the record's sub. The default step: camera adapter, pad
  update 8C050BE4, executor 8C0988A4, CCL 8C0342E0, the particle manager 8C09DF64, collision adapter refresh, land
  update 8C028EC2 and the original tail, all through the code map; without the game code `sa1g_game_ready()` is false and ticking
  returns `SA1_ERR_NOT_IMPLEMENTED`, and a world that was not booted refuses the step (`SA1_ERR_NOT_LOADED`).
  Every run sets MXCSR to 0xFFC0 (round toward zero, FTZ, DAZ) and restores the host's exact value
  afterwards, also after a fault; it points rt's `sa1_rt_xmtrx` / `sa1_rt_pad` at the world's XMTRX and pad
  latch and holds a process lock (one world runs at a time).
* **Faults** (`sa1_world.c`): software faults from the lowered code (encode of a host address in no
  segment, code-map miss or signature mismatch, fatal stub, `sa1g_fatal`) and access violations become a
  world error state with a diagnostic (guest address, segment, DC function, host pc). Windows: a vectored
  exception handler; Linux: `SIGSEGV` (`sigaction`, chained to the previous handler). Both act only while a
  world call is armed on the world's thread, when the faulting pc is in the **game-text section** (lowered
  code, `sa1_world_intrin.c`, functions marked `SA1G_GAME_TEXT`), in a trap range, or (win64) a caller
  within 4 frames is game text, and only for world addresses; everything else goes to the host. Recovery:
  the CRT `_setjmp(buf, NULL)` + `longjmp` through a trampoline (LLVM's `__builtin_setjmp` is broken on
  x86_64-windows-gnu). The world is then dead until its image is re-applied (the API: until the character
  is recreated). On win64 the diagnostic names the DC function (`RtlVirtualUnwind` + the code map).
* **Events**: a 256-entry queue per world (`sa1g_event_push` from inside a tick, no allocation;
  `sa1g_world_poll_events` afterwards) of the public `sa1_event`; faults queue `SA1_EVENT_FAULT` /
  `SA1_EVENT_UNIMPLEMENTED`; `sa1_poll_events` converts positions to host space.
* **API** (`sa1_api_world.c` behind `src/sa1_api_world.h`): the per-context images (one per kind, DISC images
  per stage), the collision segment and the FAITHFUL synthesis (`src/sa1_coladapt.c`, design 5.3), world
  creation for `sa1_character_create_ex` / the first tick, live ticks, the state blobs.
  `sa1_world_api.c` holds the older internal entry points (`sa1g_wctx`, `sa1g_character_world_*`) that
  `spike_test` exercises.
* **Intrinsics** (`sa1_world_intrin.c`): the rt section of `sa1/guest.h` without DC addresses
  (`sa1g_fabsf`, `sqrtf`, `fsrra`, `fipr`, `ftrv`, `fsca` on `pc/sh4_fpmodel.h`; SR and GBR per world).

Open (design phases 6-9): the capture of the display pass (the mesh of a ticking character is the CPU
evaluator today), GRID synthesis of host triangles, moving collision objects, kill planes and the
DeathZoneTable patch for host geometry, copy-on-write images, the reference traces of Knuckles and Big (Phase 7: Tails, Amy and E-102 already
replay identical), Tikal's create path and Super Sonic, and
the STG01 ocean task (strict M1).

## Layout

```
include/libsa1.h              public API
src/sa1_core.c                context, memory, logging, coordinates, sound queue, status strings
src/sa1_hostio.c              default stdio file callbacks (64-bit offsets), clock
src/sa1_sha1.c                SHA-1
src/sa1_prs.c                 PRS decompressor
src/sa1_disc.c                path rules, .gdi parser, sector reader, ISO9660 lookup
src/sa1_manifest.c            US v1.005 manifest: file hashes, overlay addresses, character tables
src/sa1_loader.c              sa1_load_disc: images, views, actions, welds, physics, textures
src/sa1_ninja.c               NJS_OBJECT/NJS_MODEL/NJS_MOTION/PL_JOIN_VERTEX deserializers
src/sa1_pvr.c                 PVM parser, PVR decoder
src/sa1_pose.c                mesh evaluator (matrices, motion sampling, welds, emission)
src/sa1_character.c           instances, queries, the character tick / state API
src/sa1_texture.c             texture queries, atlas packer
src/sa1_collision.c           collision input storage
src/sa1_coladapt.c/.h         collision adapter: FAITHFUL landtable synthesis, DISC landtables (design 5)
src/sa1_api_world.h           glue between the API and the world core (opaque types only)
src/world/sa1_world.h         world core API (internal): segments, faults, images, boot, tick, events
src/world/sa1_world.c         arena, segment table, extension / collision / stack segments, faults (VEH, SIGSEGV)
src/world/sa1_world_image.c   world images from the disc, patch list, apply
src/world/sa1_world_boot.c    boot, player creation, replay executor, RAM transplant
src/world/sa1_world_tick.c    tick contract (pairs, pad latch, accumulator), default step, events
src/world/sa1_world_intrin.c  SH-4 intrinsics / SR / GBR for lowered code (game text)
src/world/sa1_api_world.c     the API side of the worlds (images, collision segment, create, tick, state)
src/world/sa1_world_api.c     internal character entry points over worlds (spike_test)
src/world/sa1_world_nogame.c  empty code map for builds without game code
src/world/sa1g_text.h         the game-text section (fault-handler policy)
src/world/build_world_tests.py   builds / runs world_boot_test and transplant_test
src/world/world_ram_diff.py   region diff of a world arena against a reference RAM image
src/rt/                       SDK / SHC runtime in the game dialect (see src/rt/README.md)
src/rt/rt_intrin.h            SHC intrinsics inlined into the lowered code (force-included by build.py)
gen/                          build-time generators: boundary stub policy, link test, size report (gen/README.md)
tests/rip_test.c              ripper / comparison / timing CLI (public API only)
tests/unit_test.c             unit and robustness tests (internal API)
tests/spike_test.c            world core tests (and, with -DSPIKE_REF, the interpreter reference)
tests/world_replay.c          trace loading / injection / comparison shared by the world tests
tests/world_boot_test.c       M0, M1b, live mode, internal character API
tests/transplant_test.c       M1a (RAM transplant)
tests/replay_test.c           M1 / M1.1 through the public API
tests/coladapt_test.c         collision adapter
tests/grid_test.c             GRID collision (synthetic suite, EC1)
tests/prepare_test.c          preparation on host threads, sa1_debug_near_polys
tests/ramp_diag.c             ramp / slab / stress diagnostics (prints; --touch-hook build: every TOUCH call of a fall)
tests/build_spike.py, build_replay.py, build_coladapt.py, build_grid.py, build_prepare.py, build_ramp_diag.py
                              build / run scripts of those tests
build.py, CMakeLists.txt      builds
```

## Provenance

* **libsa1 contains no SEGA data.** No models, motions, textures, tables or code from the game
  are in this repository or in its binaries. `src/sa1_manifest.c` holds only metadata needed to
  recognise and navigate the user's disc: file names, sizes, SHA-1 hashes, overlay load
  addresses and the SH-4 addresses of a few tables. The library also holds, by the owner's decision
  D13, the code map with DC addresses and symbol names and generated mathematics (the FSCA table).
  Everything else is read from the user's GDI at run time and kept in memory; nothing is written to disk
  by the library.
* The asset layer (GDI/ISO9660 reader, PRS decompressor, PVM/PVR decoder, Ninja deserializers,
  mesh evaluator, SHA-1) is new code written for this project from format descriptions:
  `sadc/docs/research/sa1_assets.md`, `sadc/tools/prs.py` and
  `sadc/tools/research/rip_character.py` (this project's own reference implementations). No
  third-party source code was copied.
* Game logic is the matching / functionally verified decompilation in `../sadc/src`, compiled for PC from
  the same sources through `pclower`; SDK functionality (Ninja math/motion/drawing, Kamui, Shinobi, sound
  driver, CRI, SHC runtime) is **reimplemented from behaviour** in `src/rt`, never decompiled into libsa1.
* Licence: GNU General Public License version 3 (GPL-3.0), the project owner's decision of 2026-10-27, for
  libsa1 and the decompilation alike (the LibSA1 repository's LICENSE and NOTICE).
