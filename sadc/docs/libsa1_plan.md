# libsa1 + G-SA1: plan

Synthesis of the research in `docs/research/` (g64, libsm64, ecosystem, gmod, sa1_code_map,
sa1_assets, critique). Goal: decompile Sonic Adventure's engine, physics and playable characters,
build them as a standalone, host-agnostic C library (**libsa1**, the analogue of libsm64), and ship a
Garry's Mod host (**G-SA1**, the analogue of G64) that loads everything else from the user's own GDI.

## 1. Architecture

```
 sadc/ (this repo)                      libsa1/ (standalone library)          hosts
 ----------------------                 ---------------------------          ------------------------
 matching decomp of the       ----->    decompiled engine/physics/    ---->  G-SA1: GMod binary module
 original SH-4 binaries                 character C, compiled for PC          (gmcl_gsa1_win64/linux64)
 (source of truth, verified             + PC shims (Ninja math, task          + Lua addon
 byte-identical by the build)           tree, host collision, mesh out)       Others later: Unity, Godot,
                                        + runtime asset ripper                Blender, standalone viewer
                                        flat C API, no host dependencies
```

* **One source, two targets.** Every decompiled file compiles (a) with SHC for the matching build and
  (b) with GCC/Clang/MSVC for libsa1 under `TARGET_PC`. PC differences live in headers and shims, not
  in forked copies of the game code.
* **No game data in any binary.** Models, motions, welds, collision shapes, physics tables, textures
  and sounds are ripped at runtime from the user's disc (as ckosmic's libsm64 fork does). Matched C
  that defines initialised game tables reads them from the ripped image under `TARGET_PC`.
* **SDK code is reimplemented, not decompiled into libsa1**: Ninja math/matrix/motion helpers,
  the SH-4 asm model renderer, sound driver, libc/libm wrappers (clean-room, from behaviour).

## 2. What the research established

| Topic | Finding |
|---|---|
| G64 / libsm64 design | Host owns collision (feeds triangles); library ticks a fixed step and returns a CPU-skinned triangle list plus state; owner-authoritative networking with a pose-only path for remote players; per-instance globals swapped in before each call |
| Where the code is | Player common `8C06C0C0-8C07DEC0` (205 fns, 70 KB); characters `8C0A77A0-8C0EEAA0` in order Sonic, Tails, Knuckles, E-102, Amy, Big, Eggman; Tikal in `TIKAL_PROG`; Super Sonic runs Sonic's code |
| Character skeleton | Loader `func_8C04F3C0` -> `CreateElementalTask(7,1,main)`; main = mode machine + `switch(twp->mode)`; shared pipeline turn -> `PGetAcceleration 8C06D254` -> `PGetSpeed 8C072174` -> `PSetPosition 8C072B44` (collision core `8C0730D8`) -> `PSetMotion 8C079476` -> `PJoinVertexes 8C07A55C` |
| Engine deps | Task system + heap `8C098440` (CreateElementalTask `8C09852E`), angle utils `8C055D20`, node matrices `8C057BC0`, motion eval `8C03FDC0`, land collision `8C027360` fed by the landtable list builder `8C0522C0`, CCL `8C02E820`, input `8C0509A0`, 68 Ninja functions |
| Timing | Fixed 60 Hz logic steps (`func_8C04E95A` loops `func_8C04E714`), catch-up steps when frames are slow |
| Sizes | Sonic-only milestone ~574 functions / 174 KB of SH-4; all characters ~1,082 / 398 KB; biggest pieces are the per-character mode machines (10-19 KB each) |
| Assets | Proven: `tools/research/rip_character.py` rips all 9 characters (models, posed actions, every PVR format on the disc) from the GDI in under a second each, our own code only |
| Physics table | 8 x 0x84-byte `player_parameter` entries at `8C1BDFE4` (Sonic, Eggman, Tails, Knuckles, Tikal, Amy, Gamma, Big); SA Tools' labels were wrong (fixed in `config/symbols.1st_read.txt`) |
| FP fidelity | FSCA (119) / FSRRA (70) used via Ninja trig and inline in collision/motion; SHC truncates literals; RZ rounding. Bit-exactness is not guaranteed on x86: target "matched where possible, tolerance-checked elsewhere" |
| GMod | 64-bit Windows on all branches now; `IMesh:DrawSkinned` exists but SA1's averaging welds need the CPU triangle path; G64's architecture (module + Lua, BSP parse, cache) is the model, but its path-traversal hole must not be repeated |

## 3. libsa1 API sketch (host-neutral)

```
sa1_init(const sa1_host *host)                     host callbacks: log, alloc, file reader
sa1_load_disc(const char *gdi_path) -> status      validates 1ST_READ SHA-1 + per-file hashes, rips assets
sa1_collision_set_static(tris, n, attrs)           world triangles (+ surface flags: water, no-climb, hurt)
sa1_collision_object_{add,move,remove}(...)        moving geometry (props, platforms)
sa1_character_create(kind, pos, yaw) -> id         SONIC, TAILS, KNUCKLES, AMY, BIG, GAMMA, TIKAL, (SUPER)
sa1_character_tick(id, const sa1_input *in, float dt, sa1_state *out, sa1_mesh *mesh)
                                                   runs whole 60 Hz steps from an accumulator
sa1_character_pose(id, const sa1_pose *net, sa1_mesh *mesh)   remote/puppet characters
sa1_character_destroy(id)
sa1_texture_atlas(kind, rgba_out, w, h)            decoded from the user's PVM files
sa1_poll_sound_events(...)                         SE ids/voices + positions (audio optional)
```

Conventions: configurable unit scale (default 4.0 Hammer units per SA1 unit) and axis map; all
outputs in host space; no globals visible to the host (per-instance contexts, per-instance RNG).

## 4. Work plan

The "how" of phases 0, 3 and 5 was replaced by `docs/libsa1_integration.md` (GUEST-ARENA, its own phases 0-9);
the last column gives the state of each phase on 2026-10-20 (details: that document's "Status 2026-10-20").

| Phase | Content | Size (SH-4) | Exit test | Status 2026-10-20 |
|---|---|---|---|---|
| 0. Foundations | `TARGET_PC` conventions (portable `Sint32`, no pointers in packed game structs, typed field access), PC CMake build of `src/`, x64 compile check in CI, physics label fix (done) | - | every matched file compiles for x64 | **done, differently**: the game keeps its DC layout in a per-world arena; `tools/pclower.py` lowers all 507 TUs (src/1st_read, TIKAL_PROG, LIB/src/rt) for x86_64-windows, x86-windows and x86_64-linux with 0 errors; one flag set `tools/pc_cflags.txt` |
| 1. Engine core | task system + heap, angle/matrix utils, node matrices, motion evaluation, part attach | ~150 fns | unit tests vs. original via harness | **done**: decompiled (P0); the 15 reference scripts are identical with all of it routed in the harness; runs in libsa1 worlds |
| 2. Player common | 4 TUs `8C06C0C0-8C07DEC0` | 205 fns / 70 KB | per-function differential tests | **done**: decompiled, routed traces identical |
| 3. Collision | land collision + geometry helpers; host-geometry adapter replacing the landtable gather; measure step-up limit and buffer capacities | 56 fns / 25 KB | Sonic stands/runs on synthetic geometry | **mostly done**: land collision and the landtable manager decompiled and running; the adapter's FAITHFUL (dump) and DISC (the stage's landtable) modes are exact (M1). Open: GRID synthesis of host triangles, the step-up measurement, the synthetic geometry suite (integration Phase 6) |
| 4. Sonic | core TU + effects TU (homing, spin dash, light-speed dash in the mode machine) | 117 fns / 58 KB | trace replay vs. original | **done (M1)**: libsa1 replays the 5 Emerald Coast 1 traces through the public API bit-identical except `land_count` / `rand_state`, the residual of the STG01 ocean task 0C909FDA (overlay code without C, being decompiled) |
| 5. libsa1 runtime | C ripper (GDI/ISO9660/PRS/PVM/PVR, Ninja model + NJS_MOTION + welds), Ninja math reimplementation (FSCA-faithful sin/cos), mesh emitter, API, standalone viewer | new code | Sonic runs in the viewer on a test level | **mostly done**: the ripper and CPU mesh emitter (all 9 characters), the SDK runtime `LIB/src/rt` (bit-exact against the SDK in the harness), the world core, API 0.2 draft (`sa1_character_tick`, `_create_ex`, `_state_ex`, `sa1_poll_events`). Open: capture of the game's display pass for the mesh, the standalone viewer |
| 6. G-SA1 | GMod module (gmod-module-base), Lua addon: BSP world -> collision, rendering via Mesh(), camera, input, networking (owner-authoritative + puppets), SE playback | new code | Sonic in gm_construct, multiplayer | not started (needs GRID collision first) |
| 7. Other characters | Tails (flight), Knuckles (glide/climb/dig), Amy (hammer), Big (fishing optional), Gamma (lock-on/beam), Tikal; CCL + common objects (rings, springs, dash panels) | ~500 fns / 220 KB | per character | **started**: decompiled (P1, TIKAL_PROG and most of the release-scope queue; what is still missing is a policy stub in `LIB/gen/policy/stubs.tsv`); Tails, Knuckles, Amy, E-102 and Big worlds boot and tick in libsa1; replays against their reference traces in progress: tails_sh1 (414 records), amy_tp2 (381) and gamma_fe1 (363) replay identical in V5 and V6; knux_rm3 differs from step 141 and big_ec3 from step 0 beyond the stage tasks' land_count / rand_state residuals; Tikal's create path and Super Sonic open |

Phases 1-4 are decompilation and run as agent batches like the earlier ones, but in **call-graph
order** (callees before callers) instead of address order, tracked by call-graph coverage.

## 5. Validation ladder

1. **Matching** (SHC, byte-identical build) is the strongest proof and stays the default target.
2. **Differential tests** for anything non-matching: run the original function from the user's
   `1ST_READ.BIN` in an SH-4 interpreter (full FPU semantics) and the PC-compiled C on the same inputs;
   compare outputs and touched memory with tolerances. A 32-bit Linux harness can also map the DC RAM
   image at `0x8C000000` and run decompiled C in place.
3. **Scenario traces**: record per-step player state from the original (interpreter or emulator) and
   replay the inputs into libsa1.
4. **Host integration tests** in GMod.

## 6. Risks (ranked)

1. Decompilation throughput for the mode machines (10-19 KB switch functions). Mitigation: allow
   functionally verified NON_MATCHING C for libsa1; optionally an SH-4 interpreter as scaffolding that
   runs not-yet-decompiled functions from the user's own binary.
2. Feel of SA1 collision on Source maps (stairs/step-up, huge brush faces, buffer limits). Early test.
3. One source that is both byte-matching and 64-bit clean. Define conventions before mass work.
4. Distribution of a compiled decomp library (same position as libsm64/G64). Decide release model.
5. FP fidelity (FSCA/FSRRA/FMAC). Tolerance-based acceptance.

## 7. Decisions (project owner, 2026-10-06)

* **Release model: prebuilt DLL** (libsa1 + GMod module), containing decompiled game code only;
  Ninja/Kamui/Shinobi/sound-driver/CRI/SHC-runtime functionality is reimplemented, never decompiled
  into the library.
* **Scope: all characters before the first GMod release** (Sonic, Tails, Knuckles, Amy, Big, E-102,
  Tikal; Super Sonic via Sonic). Decompilation covers the P0+P1 set (~1,082 functions); the libsa1
  runtime and harness are built in parallel.
* **Functionally verified NON_MATCHING C is accepted** in libsa1 where matching stalls; it must pass
  differential tests against the original first. Matching remains the default goal.
* **SH-4 interpreter harness: yes**, as test oracle and as development scaffolding (running
  not-yet-decompiled functions from the user's own `1ST_READ.BIN`); the shipped library is pure
  decompiled C plus reimplemented SDK.

Defaults chosen unless told otherwise: scale 4.0 HU/unit (ConVar), CPU triangle rendering,
in-memory assets only (no `data/` caches), owner-authoritative networking, 60 Hz accumulator on
`SysTime`, US 1.005 GDI + extracted folder (CHD later), module-owned disc path (never from Lua).

## 8. Integration decisions (project owner, 2026-10-20)

* **Data model: GUEST-ARENA** (docs/libsa1_integration.md): per-world 16 MB Dreamcast-layout arena filled from the
  user's disc, 32-bit guest pointers in memory, build-time lowering (`tools/pclower.py`); the native-struct design is
  the fallback.
* **Motion player 8C03FDC0-8C041E3E** ships as decompiled C (game code).
* **DLL content**: code, DC addresses, symbol names, code map, generated math tables; never game bytes.
* **First-release scope** adds Big's fishing, basic objects (jump panels, ring scatter, item box) and the E-100 Zero
  chaser; Eggman stays out.
