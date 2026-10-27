# libsm64 deep read, and the analogous design for an SA1 library ("libsa1")

Research note for the Garry's Mod port goal: compile SA1's engine, physics and playable characters
as a PC library, in the way libsm64 does for SM64 and G64 does for Garry's Mod. The port must rip
every asset from the user's own GDI at runtime. Written 2026-10-06. All statements about libsm64
come from reading the source at the commits listed below. Third-party code is described here in
prose and is not copied. Sections 10-12 contain SA1-side findings from this repository (addresses
are DC US v1.005, `1ST_READ.BIN` linked at `0x8C010000`).

---

## 0. TL;DR

* **libsm64 is mostly the SM64 decomp's Mario code compiled with GCC as a C shared library**, with
  a flat C API (`libsm64.h`). The library does five things:
  1. It replaces the object/level/camera/save systems with small stubs (`decomp/shim.h`,
     `game/object_stuff.c`).
  2. It moves Mario's file-static and global variables into a `struct GlobalState`. Each API call
     swaps in the instance it targets, which is how several Marios coexist.
  3. It replaces the collision grid with a linear scan over surfaces the host supplies.
  4. It replaces the N64 display-list/RDP pipeline with a tiny interpreter that writes
     world-space triangles into float buffers the host owns.
  5. It loads Mario's textures, animations and (optionally) audio banks from the user's ROM at
     **hard-coded offsets**.
* **Upstream compiles Mario's mesh into the DLL.** At build time, `import-mario-geo.py` downloads
  `actors/mario/model.inc.c`/`geo.inc.c` from the n64decomp repo at a pinned commit. Only the
  texture bytes are commented out, and they are ripped at runtime instead. **ckosmic's `gmod`
  fork, which G64 uses, does not do this.** It parses Mario's geo layout and F3D display lists out
  of the ROM at runtime (code inherited from Retro64Mod), so that DLL ships no Nintendo geometry.
  libsa1 must take the ckosmic approach: **nothing from the disc may be compiled into the library.**
* The host receives five things:
  * one RGBA8 texture atlas, 704x64 (11 Mario textures in 64-px slots)
  * per tick, up to 1024 triangles: world-space positions, normals, per-vertex RGB (material/light
    colour) and atlas UVs
  * a small state struct (position, velocity, yaw, action, animation, flags, health)
  * optional 32 kHz stereo s16 PCM from the decomp's own audio engine
  * optional per-sound callbacks
* Collision is a list of host triangles (`SM64Surface`), plus movable "surface objects" with a
  transform. Each query scans every loaded triangle, O(N). **G64 works around this on the Lua
  side** by chunking the map into a 2-D grid and reloading the static surface set whenever Mario
  changes chunk.
* For SA1 the same skeleton applies (flat C API, host-supplied collision, CPU-evaluated mesh
  output, assets parsed from the user's disc). The differences are fundamental, though:
  * **Rendering.** SA1 draws Ninja Basic models plus `NJS_MOTION` plus CPU vertex welding, where
    SM64 uses F3D display lists.
  * **Pointer width.** SA1 data are 32-bit-pointer little-endian structs that must be deserialised
    for x64. SM64 data are big-endian and only byte-swapped.
  * **Collision.** SA1 uses `_OBJ_LANDTABLE` COL meshes with per-entry attribute flags and
    sphere-vs-polygon tests that work in any orientation. SM64 uses floor/wall/ceiling triangles
    split by normal.y.
  * **Object model.** SA1 has a real task tree (`task`/`taskwk`/`motionwk`/`playerwk`) with up to
    8 native player slots, where SM64 has a single Mario object.
  * **Audio.** SA1 sound runs on the AICA (MLT banks, ADX streams), where SM64's audio engine is
    plain C game code.
  * **Tick rate.** SA1 runs at 60 Hz where SM64 runs at 30 Hz.
  * **Numerics.** SHC truncates float literals (`-round=zero`) and the code uses the SH-4
    `fipr`/`ftrv` vector instructions.

---

## 1. Sources read

All repositories were cloned (`--depth 1`; upstream was later unshallowed to read its history) into
`<scratch>\repos\`.

| Repo (dir) | Commit / date | License | Role |
|---|---|---|---|
| `libsm64/libsm64` (`libsm64`) | `fd11813` 2026-02-13, 204 commits, first commit 2020-10-08 (jaburns) | `LICENSE.md` = **CC0 1.0** for the repo. The repo also holds decomp-derived C and downloads Nintendo model data at build time; the license does not cover the game's data. | Upstream library |
| `ckosmic/libsm64` branch `gmod` (= HEAD) (`libsm64-ckosmic`) | `b7bfef1` 2022-09-03 | **No LICENSE file**. Bundles sm64-port `pc/gfx` (own BSD-like LICENSE.txt, Emill/MaikelChan; not compiled) | Library built for G64. Other branches: `audio`, `audio-wip`, `changes`, `master`, `vs` |
| `ckosmic/libsm64-gmod` (`libsm64-gmod`) | `198dc63` 2022-09-03 | No LICENSE file | G64 binary module `gmcl_g64_win64.dll` (MSVC; links `sm64.lib`; uses the Facepunch `gmod-module-base` submodule and SDL2 for gamepads) |
| `ckosmic/g64` (`g64`) | `6de9c9e` 2023-03-06 | No LICENSE file | Garry's Mod Lua addon |
| `Retro64Mod/libsm64-retro64` (`libsm64-retro64`) | `7afa8ed` 2025-02-24 | No LICENSE file | Minecraft-mod fork. Source of the audio port, runtime ROM geo/DL parsing, "fake" (animation-only) Marios and multiple character models |
| `Brawmario/libsm64-godot` (`libsm64-godot`) | `7d7be1e` 2026-04-03 | MIT (2022 Renato Rotenberg) | GDExtension binding: ROM SHA-256 check, interpolation, audio stream |
| `libsm64/libsm64-unity` (`libsm64-unity`) | `21f6737` 2022-04-15 | none seen | Unity binding (C# P/Invoke) |

Upstream history landmarks (`git log`):
* 2020-10-08: initial commit "hacked up sm64 decomp source"; model and animations pulled from
  sm64-port.
* 2020-10-10..12: animations, then textures, loaded from ROM; geo files fetched over HTTP.
* 2020-10-13..29: dynamic surfaces (moving platforms), `platform_displacement` import.
* 2020-10-27: "All global state coming from g_state now"; multiple-Mario API.
* 2021-08-16: 32-bit collision vertices, degenerate-triangle filtering.
* 2022-10: MeltyPlayer pulls ckosmic's audio port and the s16 to float position fix.
* 2023-01: headshot2017 adds "loads of features from ckosmic fork" (setters, sound API).
* 2023-2025: build, CI and binding housekeeping (macOS universal, Emscripten).

---

## 2. Public API (`src/libsm64.h`, upstream master)

### 2.1 Conventions
* Plain C ABI. `SM64_LIB_FN` is `__declspec(dllexport/dllimport)` on Windows and
  `visibility("default")` on GCC when `SM64_LIB_EXPORT` is defined. The whole lib is compiled
  with `-fvisibility=hidden`. The header is wrapped in `extern "C"`.
* Mario instances are `int32_t` ids (index into a growable pointer pool; freed slots are reused).
  Surface objects are `uint32_t` ids.
* All coordinates are **SM64 world units** and Y is up. Mario's hitbox is 37 radius x 160 height
  (`object_stuff.c`). The library never converts units; hosts scale. G64: `sm64 = gmod * 2.0` with
  an axis swap, see 5.7.
* Angles:
  * API-level yaw/euler values are radians (`sm64_set_mario_angle`, `outState->faceAngle`;
    converted with 3.14159 and the s16 binary angle where 0x8000 = pi).
  * Surface-object euler rotations are **degrees, negated** (`CONVERT_ANGLE(x) = s16(-(x)/180*32768)`).
* The tick is fixed at **30 Hz** (SM64 game logic rate). Hosts call `sm64_mario_tick` 30 times a
  second and interpolate for rendering (see the test program, the Godot interpolator, and G64's
  `G64_TICKRATE = 1/33`).

### 2.2 Structs
* **`SM64Surface`**: `int16 type` (SM64 `SURFACE_*`), `int16 force`, `uint16 terrain`
  (`TERRAIN_*`: grass/stone/snow/sand/spooky/water/slide), and `int32 vertices[3][3]`. Upstream
  widened the vertices from int16 to int32; the ckosmic fork too.
* **`SM64MarioInputs`**:
  * `camLookX`, `camLookZ`: horizontal direction from the camera to Mario; only the yaw is used.
  * `stickX`, `stickY` in [-1,1].
  * `buttonA`, `buttonB`, `buttonZ` (`uint8`).
  * There are no L/R/C/Start buttons, so no first-person or camera actions.
* **`SM64ObjectTransform`**: `position[3]`, `eulerRotation[3]` (degrees).
* **`SM64SurfaceObject`**: `transform`, `surfaceCount`, `SM64Surface *surfaces` (object-local
  vertices).
* **`SM64MarioState`** (output): `position[3]`, `velocity[3]`, `faceAngle` (rad),
  `forwardVelocity`, `int16 health` (0x880 at spawn; wedges = health>>8), `uint32 action`
  (`ACT_*`), `int32 animID`, `int16 animFrame`, `uint32 flags` (`MARIO_*` caps etc.),
  `particleFlags` (`PARTICLE_*`: the host spawns dust/sparkles itself), `int16 invincTimer`.
* **`SM64MarioGeometryBuffers`**: host-allocated `float *position, *normal, *color, *uv` plus
  `uint16 numTrianglesUsed` (written by the lib). Sizes:
  * position/normal/color: 9 floats per triangle
  * uv: 6 floats per triangle
  * capacity `SM64_GEO_MAX_TRIANGLES` = 1024. **The lib never bounds-checks it.**
* **`SM64WallCollisionData`**: query position, `offsetY`, `radius`, `numWalls`, `walls[4]`.
* **`SM64FloorCollisionData`**: normal and origin offset.
* **`SM64SurfaceCollisionData`**: the engine's internal surface record, exposed by the query
  functions. Fields:
  * type, force, flags, room
  * int32 lowerY/upperY and int32 vertices
  * float normal, originOffset
  * libsm64 additions: `isValid`, a pointer to `SM64SurfaceObjectTransform`, and `terrain`
* **`SM64SurfaceObjectTransform`**: position, velocity (frame delta), s16 face angles and s16
  angular velocity (frame delta). This is the "platform" Mario rides.
* Enum: `SM64_TEXTURE_WIDTH = 64*11` (704), `SM64_TEXTURE_HEIGHT = 64`,
  `SM64_GEO_MAX_TRIANGLES = 1024`.

### 2.3 Functions
| Group | Functions | Behaviour |
|---|---|---|
| Callbacks | `sm64_register_debug_print_function(fn(const char*))`, `sm64_register_play_sound_function(fn(uint32 soundBits, float *pos))` | Optional. The sound callback fires for every `play_sound` the game code makes, whether or not the internal audio engine is initialised. |
| Global | `sm64_global_init(const uint8 *rom, uint8 *outTexture)`, `sm64_global_terminate()` | init: decode textures into the caller's 704x64x4 buffer, parse animations, init memory pools. Re-init terminates first. terminate: deletes all Marios, frees the geo pool, surfaces and animations. |
| Audio | `sm64_audio_init(const uint8 *rom)`, `uint32 sm64_audio_tick(uint32 numQueuedSamples, uint32 numDesiredSamples, int16 *buf)` | See section 7 |
| Static collision | `sm64_static_surfaces_load(const SM64Surface*, uint32 n)` | Replaces the whole static set (copied and converted) |
| Mario | `int32 sm64_mario_create(float x,y,z)`, `sm64_mario_tick(id, const SM64MarioInputs*, SM64MarioState *out, SM64MarioGeometryBuffers *outBuffers)`, `sm64_mario_delete(id)` | create returns **-1 if there is no floor under the spawn point**. It spawns in `ACT_SPAWN_SPIN_AIRBORNE`. |
| Mario setters | `sm64_set_mario_action`, `_action_arg`, `_animation`, `_anim_frame`, `_state` (flags), `_position`, `_angle(x,y,z rad)`, `_faceangle(y)`, `_velocity`, `_forward_velocity`, `_invincibility`, `_water_level(int)`, `_gas_level(int)`, `_health` | Each binds that Mario's global state and writes `gMarioState` fields directly |
| Gameplay hooks | `sm64_mario_take_damage(id, damage, subtype, x,y,z)`, `sm64_mario_heal(id, healCounter)`, `sm64_mario_kill(id)` (health=0xff), `sm64_mario_interact_cap(id, capFlag, capTime, playMusic)`, `sm64_mario_extend_cap(id, time)`, `bool sm64_mario_attack(id, x,y,z, hitboxHeight)` | Re-implementations of the interaction code without real objects (`fake_interaction.c`): knockback selection from the damage source position; whether Mario's current attack/stomp would hit a target at that point. |
| Surface objects | `uint32 sm64_surface_object_create(const SM64SurfaceObject*)`, `sm64_surface_object_move(id, const SM64ObjectTransform*)`, `sm64_surface_object_delete(id)` | move recomputes every triangle and the frame velocity. delete first clears any Mario's `platform` pointer that references it. |
| Collision queries | `sm64_surface_find_wall_collision(float*x,*y,*z, offsetY, radius)`, `_wall_collisions(SM64WallCollisionData*)`, `float _find_ceil(x,y,z, SM64SurfaceCollisionData**)`, `_find_floor_height_and_data`, `_find_floor_height`, `_find_floor`, `_find_water_level`, `_find_poison_gas_level` | Thin wrappers over the engine functions, so hosts can raycast with SM64 semantics |
| Music/sound | `sm64_seq_player_play_sequence`, `sm64_play_music`, `sm64_stop_background_music`, `sm64_fadeout_background_music`, `sm64_get_current_background_music`, `sm64_play_sound(bits, pos)`, `sm64_play_sound_global(bits)`, `sm64_set_sound_volume(float)` | Drive the decomp's sequence player and sound banks |

### 2.4 Canonical host loop (`test/main.cpp`)
1. Read `baserom.us.z64` into memory.
2. Allocate the 4x704x64 texture buffer. Call `sm64_global_init(rom, tex)`, `sm64_audio_init(rom)`,
   `sm64_static_surfaces_load(level)` (the test level is Bob-omb Battlefield collision, generated
   at build time) and `sm64_mario_create(0,1000,0)`.
3. **Free the ROM.** The library keeps its own copies.
4. Each render frame:
   * build inputs: camera orbit, stick, buttons
   * run `sm64_mario_tick` at 1/30 s fixed steps
   * linearly interpolate the Mario position and all vertex positions between the last two ticks
   * upload the 4 buffers to GL
   * the shader mixes vertex colour and texel by texel alpha, then applies simple lighting from
     the normal
5. An audio thread calls `sm64_audio_tick(queued/4, 1100, buf)` every 33 ms and queues the result
   to SDL at 32 kHz stereo.

### 2.5 ckosmic `gmod` fork API (header `LIB_VERSION 3`, used by G64)
* `sm64_global_init(uint8 *rom, debugPrintFn)`:
  * does textures **no longer**; it parses audio banks and builds the runtime Mario geo layout
  * loads animations
  * picks an output backend: WASAPI, then PulseAudio, then ALSA, then null
  * starts an internal **pthread audio thread**
* `sm64_texture_load(rom, SM64TextureAtlasInfo*, uint8 *out)`: a generic MIO0-block to atlas
  ripper. The caller passes:
  * the MIO0 ROM offset
  * texture count and atlas width/height
  * a table of `{offset in the decompressed block, w, h, format RGBA16 | IA16}`

  G64 defines 5 atlases this way: Mario, coins, UI digits, health meter, particle.
* `sm64_mario_create(x,y,z, int16 rx,ry,rz, uint8 fake)`: a **fake** Mario skips the floor check
  and the spawn action. It exists for remote players in multiplayer.
* `SM64AnimInfo* sm64_mario_get_anim_info(id, int16 rot[3])` and
  `sm64_mario_anim_tick(id, stateFlags, SM64AnimInfo*, buffers, int16 rot[3])`. The anim tick
  sets the animation id/accel and flags on a fake Mario and runs only the geo/render pass. This is
  **network replication of the pose, not of the simulation.** Each client simulates only its own
  Mario and replays others from (anim id, frame/accel, rotation, flags).
* `SM64ObjectCollider {position, height, radius}` with `sm64_object_create/move/delete`:
  * cylinders that push Mario out horizontally (resolved after `bhv_mario_update`)
  * no damage or interaction
* `sm64_set_mario_floor_override(id, terrain, floorType, floorForce)`: forces the
  terrain/surface type for the floor under Mario. G64 uses it for a "surface changer" tool.
  Implemented with new `MarioState` fields `overrideTerrain`/`overrideFloorType`/
  `overrideFloorForce`.
* Extra state out: `hurtCounter`, `numLives`, `holdingObject`, `dropMethod`.
* Sound helpers: `sm64_get_sound_arg(bank, playFlags, soundID, priority, flags2)`,
  `sm64_set_volume`, `sm64_set_reverb`, `sm64_get_version`.
* No per-id existence checks in most setters (crash risk on bad ids).
* An "SM64MarioModelColors" struct is declared for recolouring. G64 instead recolours by building
  a lookup of distinct vertex colours, see 6.4.

### 2.6 Retro64 fork API (Minecraft)
* `sm64_mChar_*` naming (create, tick, delete, teleport, set_velocity, apply_damage, set_state,
  set_action(_with_arg), set_action_state, set_water/gas_level, set_angle, heal).
* `sm64_get_anim_info`/`sm64_mChar_animTick(..., int32 model, ...)`: animation-only Marios, as in
  ckosmic.
* `sm64_set_redive`, `sm64_set_ground_pound_jump`: gameplay toggles.
* `model_handler.c`: seven selectable meshes (Mario, Luigi, Steve, Alex, Neco-Arc, Vibri, Sonic),
  all rigged to Mario's skeleton and animations. Custom meshes are compiled-in `model.inc.c` files.
  **This is a precedent for driving a different character mesh with one engine's animation data.**

### 2.7 API gotchas worth not repeating
* No geometry-buffer capacity is passed in, so a mesh over 1024 triangles overruns host memory.
* There is no thread safety. Audio ticks and Mario ticks share globals: `gMarioState` sound
  sources, the request queue.
* Sound positions are `cameraToObject`, which the lib zeroes every tick. Internal audio is
  therefore effectively non-positional; real 3-D sound needs the callback.
* Water is a single per-Mario plane set by the host. `find_water_level` is hard-wired to return
  -10000.
* `sm64_mario_create` fails without a floor, so hosts must load collision before spawning.

---

## 3. How the decomp source became a library

### 3.1 Provenance
* Game C files are a **vendored, hand-edited snapshot** of the n64decomp/sm64 sources as of
  October 2020 (taken from the sm64-port tree), compiled as `VERSION_US`.
* Build-time downloads (`import-mario-geo.py`, `import-test-collision.py`) pin n64decomp/sm64
  commit `06ec56df7f951f88da05f468cdcacecba496145a`.
* The audio sources and the PC mixer came later via ckosmic/Retro64 (sm64-port lineage).
* Compile defines:
  * `-DVERSION_US`
  * `-DNO_SEGMENTED_MEMORY`: `segmented_to_virtual` becomes identity
  * `-DGBI_FLOATS`: `Mtx` and `Vtx` positions are floats
  * `-DSM64_LIB_EXPORT`
  * plus `-fPIC -fvisibility=hidden -fno-strict-aliasing -g -Wall`

### 3.2 File inventory (upstream `src/`)
| Kind | Files | Notes |
|---|---|---|
| Kept close to decomp | `decomp/game/mario.c`, `mario_actions_{airborne,automatic,cutscene,moving,object,stationary,submerged}.c`, `mario_step.c`, `mario_misc.c` (body state, geo callbacks for eyes/hands/cap/torso tilt/head rotation/wing flap), `interaction.c`, `sound_init.c`; `decomp/engine/geo_layout.c`, `graph_node.c`, `graph_node_manager.c`, `math_util.c` (with the SM64 sine/arctan tables inline) | Small edits marked "libsm64:", e.g. water/gas lookups commented out in `mario_update_geometry_inputs`, swim-state statics turned into `g_state` macros, and init of Mario from a float spawn position |
| Heavily adapted | `decomp/engine/surface_collision.c` (rewritten), `decomp/game/rendering_graph_node.c` (master-list hack, shadow stubbed, single-object root), `decomp/game/platform_displacement.c` (platform = `SM64SurfaceObjectTransform`), headers `area.h`, `camera.h`, `level_update.h`, `save_file.h`, `include/types.h` (structs trimmed/extended) | |
| New glue | `libsm64.c` (API), `load_surfaces.c`, `load_tex_data.c`, `load_anim_data.c`, `load_audio_data.c`, `gfx_adapter.c` + `gfx_macros.h` + `gfx_adapter_commands.h`, `fake_interaction.c`, `obj_pool.c`, `play_sound.c`, `debug_print.c`, `decomp/global_state.{c,h}`, `decomp/memory.c`, `decomp/shim.h`, `decomp/game/object_stuff.c`, `decomp/engine/guMtxF2L.c` | |
| Audio (decomp + port) | `decomp/audio/*` (data, effects, external, heap, load, load_sh, playback, seqplayer, synthesis, synthesis_sh, port_eu/sh, copt), `decomp/pc/mixer.c` (sm64-port's C reimplementation of the RSP audio microcode, SSE4.1/NEON paths), `decomp/pc/ultra_reimplementation.c` (libultra stubs), `decomp/pc/alBnkfNew.c`, `decomp/tools/convUtils.c` (CTL/TBL/SEQ parsing from ROM) | |
| Tools reused at runtime | `decomp/tools/libmio0.c` (MIO0 decoder), `n64graphics.c` (raw RGBA16/IA to RGBA8; includes stb_image), `utils.c` | |
| Generated at build time | `src/decomp/mario/geo.inc.c/.h`, `model.inc.c/.h`, `test/level.c/.h` | Not in git |

### 3.3 Object system: stubbed
* There is no object list, no behaviour scripts and no `spawn_object`.
* `object_stuff.c` `hack_allocate_mario` mallocs one `struct Object`, fills the fields Mario code
  reads (hitbox 37/160, `oIntangibleTimer`, identity transform, graph-node flags) and returns it.
* `bhv_mario_update` is a reduced copy of the original:
  * runs `execute_mario_action`
  * copies `MarioState` pos/vel/angles into the object
  * the particle spawning loop is commented out; particle flags are returned to the host instead
* Held/ridden/used objects stay NULL, so grabbing, carrying, Koopa shells and cannons do nothing
  real.
* `interaction.c` compiles, but no objects ever exist. `fake_interaction.c` exposes damage and
  attack checks with plain coordinates.

### 3.4 Level, area, camera, save: stubbed
* `allocate_area` mallocs a zeroed `struct Area` and a zeroed `struct Camera` per Mario.
* Only `camera->yaw` is used: input is relative to the camera, and it is set from `camLookX/Z`
  each tick.
* `shim.h` defines no-op or constant stubs for:
  * `segmented_to_virtual`
  * time stop
  * camera mode/shake
  * warps (`level_trigger_warp` returns 0)
  * save-file flags and star counts
  * star spawning, wind particles, HUD text
  * level timers, infinite-stairs/shell music
* `gCurrLevelNum` etc. live in `g_state`. Level-specific constants (`LEVEL_LLL`, `LEVEL_TTC`, ...)
  are redefined to dummy values.

### 3.5 Global state and multiple Marios
* `struct GlobalState` (`decomp/global_state.h`) holds **every mutable global and file-static
  that Mario's code touches**:
  * interaction invincibility statics
  * the floor-align matrix
  * swim statics (`sSwimStrength` initialised to 160 at create)
  * the 2 body states, the attack-anim counter, `gAreaUpdateCounter`, `gGlobalTimer`
  * special triple-jump flag, `gCurrLevelNum`, camera flags, `gController`
  * the spawn info, `gCurrentArea`, `gCurrentObject`, `gMarioObject`
  * the `MarioAnimation` struct and the `MarioState` itself
* A global pointer `g_state` points at the active instance.
* `shim.h` and per-file `#define`s rewrite each original name into a member access through
  `g_state`, e.g. `gMarioState` becomes the address of `g_state->mgMarioStateVal`. Statics in
  `interaction.c` and `mario_actions_submerged.c` get the same treatment.
* **Every API entry point first calls `global_state_bind(instance->globalState)`.** This is a
  manual context switch. It means:
  * the decomp code is unchanged apart from names
  * instances must run on one thread
  * truly shared engine state stays global by design: surfaces, the animation table, the geo
    graph, the display-list pool and the audio engine
* The Mario graph-node tree (from `process_geo_layout`) is built once and **shared** by all
  instances. Per-instance animation state lives in the instance's `Object` graph header, which is
  why the render hack drives `geo_set_animation_globals` from `gMarioObject`.

### 3.6 Memory
* `memory.c` replaces the decomp's main/display-list pools with an "alloc-only pool": a growable
  array of individual mallocs, all freed at once.
* The display-list pool is reset at the start of every geo pass.
* Instance objects, areas and the surface lists use plain malloc/free.

### 3.7 Build-time import scripts
* **`import-mario-geo.py`** downloads `actors/mario/geo.inc.c` and `model.inc.c` at the pinned
  commit, then:
  * comments out every texture array definition (the `ALIGNED8 static const u8 mario_*` lines and
    the following `#include *.rgba16.inc.c` lines), so no texture bytes are compiled in
  * prepends includes of `load_tex_data.h` and `gfx_macros.h`
  * generates `model.inc.h` with `extern` declarations of every `const` symbol
  * appends a wrapper geo layout `mario_geo_libsm64` (shadow, z-buffer, scale 0.25, mirror-Mario
    callbacks, branch to `mario_geo_load_body`) and a `mario_geo_ptr`
* **The trick:** `gfx_macros.h` redefines the F3D `gs*` macros as an `intptr_t` command stream.
  Texture symbols such as `mario_texture_metal` are names in the `enum MarioTextures` (indices into
  the atlas), so the unchanged decomp model source compiles into libsm64's own command format.
  **The vertex, light and display-list data of Mario's model end up inside the shipped DLL.**
* **`import-test-collision.py`** downloads `levels/bob/areas/1/collision.inc.c` and converts
  `COL_VERTEX`/`COL_TRI_INIT`/`COL_TRI` into an `SM64Surface[]` (terrain forced to snow), for the
  test only.

### 3.8 One `sm64_mario_tick` (upstream)
1. Bind state.
2. Edge-detect A/B/Z into `gController.buttonPressed/buttonDown`.
3. Zero `cameraToObject`.
4. Set camera yaw = `atan2s(camLookZ, camLookX)`.
5. `gController.stickX = -64*stickX`, `stickY = 64*stickY`, `stickMag` computed.
6. `apply_mario_platform_displacement()`: move Mario by the platform's last frame delta and
   rotation.
7. `bhv_mario_update()`: full SM64 action state machine, including
   `mario_update_geometry_inputs`, which does the floor/ceiling/wall queries.
8. `update_mario_platform()`: if the floor is within 4 units and belongs to a surface object, set
   `platform` to its transform.
9. `gfx_adapter_bind_output_buffers`.
10. `geo_process_root_hack_single_node(shared graph)`. This also **advances the animation frame**
    (the frame advance happens in the render pass, so geometry is always produced).
11. `gAreaUpdateCounter++`.
12. Copy out the state.

### 3.9 Missing compared with the real game
* objects and interactions
* cap objects and the cap-throw object
* shadows
* particles
* camera
* water boxes and gas
* level warps, death planes, HUD

Instant-warp/death floors simply do nothing. Hosts implement them from the outputs (G64 does
respawn, coins, caps and HUD in Lua).

---

## 4. ROM asset extraction at runtime

### 4.1 Input and validation
* The input is the whole **big-endian `.z64` US ROM** as a byte pointer. There is no size or
  format argument.
* **libsm64 itself performs no validation**: no hash, no header check. Offsets are hard-coded for
  US.
* Validation is left to hosts:
  * G64's README asks for MD5 `20b854b239203baf6c961b850a4a51a2` / SHA-1
    `9bef1128717f958171a4afac3ed78ee2bb4e86ce`, but neither the module nor the Lua actually
    checks. The module just reads the file at the path from the `g64_rompath` convar.
  * libsm64-godot rejects ROMs whose **SHA-256** is not
    `17ce077343c6133f8c9f2d6d6d9a4ab62c8cd2aa57c40aea1f490b4c8bb21d91` (in GDScript, before
    calling init).
* Recommendation for SA1: validate inside the library (or its asset loader) and return a clear
  error code.

### 4.2 Textures
* The MIO0 block at ROM `0x114750` (1132368) is decoded with `libmio0`. This is Mario's segment
  0x04 data: lights, vertices and textures.
* 11 textures are read at fixed offsets inside the decompressed block (144, 4240, 6288, 8336,
  10384, 12432, 14480, 16528, 30864, 32912, 37008):
  * metal 64x32
  * yellow button, M logo, sideburn, mustache, eyes front, eyes half-closed, eyes closed, eyes
    dead: 32x32 each
  * two wing halves: 32x64
* RGBA5551 is converted to RGBA8888: 5-bit channels scaled to 8-bit, 1-bit alpha to 0/255.
* Each texture is blitted into a **704x64 RGBA8 atlas**, one 64-px column per texture, top-left
  aligned. The rest is zero, including alpha. The atlas is written into the caller's buffer
  (180,224 bytes).
* Unused textures (eyes left/right/up/down, metal wings) get negative enum values and are not
  ripped.
* The ckosmic fork generalises this as `sm64_texture_load` with an atlas descriptor, and adds IA16
  for coin and particle textures.

### 4.3 Animations
* Mario's animation DMA table sits at ROM `0x4EC000`: a u32 count, then 8-byte
  {offset, size} pairs starting at +8.
* For each entry the loader reads, big-endian:
  * the header: flags, Y-translation divisor, start frame, loop start, loop end, bone count
  * values offset, index offset, end offset
* It copies the `u16 index[]` and `s16 values[]` arrays into native-endian malloc'd arrays, then
  builds a native `struct Animation[]`.
* `load_mario_animation` (called by `set_mario_animation`) points `targetAnim` at entry N instead
  of DMA-ing.

### 4.4 Model geometry
* **Upstream:** compiled in from the downloaded decomp model source (3.7). Only textures, anims
  and audio come from the ROM.
* **ckosmic `gmod` / Retro64:** parsed from the ROM at init.
  * `initMarioGeo(rom)` builds the same wrapper geo layout. It resolves the
    `mario_geo_load_body` segmented address `0x17002CE0` by mapping segment 0x17 to ROM base
    `0x1279B0`.
  * A recursive **geo-layout parser** handles commands 00-05, 0B-0E, 10-13, 14-1D, 20 and so on.
    It translates each command into the decomp's native `GeoLayout` macros and follows branches.
  * Geo `ASM`/switch function addresses are remapped through a **hard-coded table from N64 RAM
    addresses to library functions**, for example:
    * `0x80277D6C` to `geo_mirror_mario_backface_culling`
    * `0x802771BC` to `geo_switch_mario_eyes`
    * `0x802775CC` to `geo_mario_hand_foot_scaler`
    * `0x802B1BB0` to `geo_move_mario_part_from_parent`
    * 13 entries in total
  * Display-list addresses (segment 0x04) are parsed by a **mini F3D decoder**, which converts
    real GBI words into libsm64's command stream:
    * `G_VTX` decodes 16-byte big-endian vertices into native `Vtx`
    * `G_TRI1` divides indices by 10 (F3D's x10 encoding)
    * `G_MOVEMEM` lights become new `Lights1`
    * `G_TEXTURE`, `G_SETTILESIZE`
    * `G_SETTIMG` maps segment addresses `0x04000090`..`0x04009090` to atlas indices 0-10
    * `G_DL` is followed recursively
    * `G_ENDDL`
    * the rest become no-ops
* This is the model for libsa1: **parse the original data formats from the user's dump and
  translate code pointers through a table.**

### 4.5 Audio data
* `load_audio_banks(rom)` copies the first 8 MB of the ROM (it leaks this copy by design: the
  sample data are referenced in place) and parses:
  * CTL (instrument banks) at `0x57B720`, deserialised into a 0x20B40-byte arena with pointers
    turned into offsets
  * TBL (raw ADPCM samples) at `0x593560`
  * sequences at `0x7B0860`
  * the bank-set table at `0x7CC621`, with a one-byte patch-up
* It then calls `audio_init`, `sound_init` and `sound_reset(0)`.

### 4.6 What the host gets from init
* The texture atlas (RGBA8; G64 converts it to Lua tables and then to a render target).
* Nothing else is returned: models, animations and audio stay internal.

---

## 5. Collision model

### 5.1 Input
* `SM64Surface` is a triangle with `type`/`force`/`terrain` and int32 vertices in SM64 units.
* SM64's front side is counter-clockwise when seen from the side the normal points to:
  normal = (v2-v1)x(v3-v2).
* The host decides the surface type per triangle. G64 uses `SURFACE_DEFAULT` and lets the
  surface-changer tool override it.

### 5.2 Conversion (`load_surfaces.c`)
* Each triangle becomes an `SM64SurfaceCollisionData`:
  * For surface objects, vertices are first transformed by `mtxf_rotate_zxy_and_translate`
    (object position and s16 angles) and rounded to int32.
  * The normal and `originOffset` are computed.
  * `lowerY`/`upperY` = min/max Y -/+ 5.
  * `force` is kept only for force-type surfaces (flowing water, quicksands, horizontal wind).
  * `isValid` is 0 for degenerate triangles (|n| < 0.0001).
  * `room` is 0 and camera flags are 0.
* Static triangles live in one array. Each surface object keeps the original local triangles and a
  converted copy, re-converted on every move.

### 5.3 Classification
* Classification happens **at query time** by the normal's Y, exactly as SM64's
  `add_surface_to_cell` would:
  * floor if `normal.y > 0.01`
  * ceiling if `normal.y < -0.01`
  * wall otherwise
* Walls get `SURFACE_FLAG_X_PROJECTION` when |normal.x| > 0.707, which selects the projection
  plane for the point-in-triangle test.
* Steep slopes are floors, which is how SM64 slides Mario.

### 5.4 Queries (`surface_collision.c`, rewritten)
* All three queries iterate **every valid surface in every group** (static set plus each object).
  There is **no spatial partition**, no cell lists and no level-bounds check: the +/-0x2000 bounds
  are removed so int32 coordinates work.
* **Floor:**
  * x/z point-in-triangle by edge cross products
  * height from the plane equation
  * ignored if more than 78 units above the query point
  * returns the highest
* **Ceiling:** mirror of the floor test (78-unit buffer below), returns the lowest.
* **Walls:**
  * radius clamped to 200
  * Y must be inside lower/upper bounds
  * plane distance must be within the radius
  * projected point-in-triangle test
  * camera-boundary and vanish-cap walls skipped as in SM64
  * the position is pushed out along the normal by (radius - offset)
  * up to 4 walls are recorded
* Water and gas: constant -10000. The host supplies one plane per Mario via the setters.

### 5.5 Moving platforms
* `update_transform` stores the per-move position delta as velocity and the s16 angle delta as
  angular velocity.
* `update_mario_platform` attaches Mario when the floor is within 4 units and belongs to an
  object.
* Next tick `apply_mario_platform_displacement` moves Mario by that delta and rotates him around
  the platform pivot.
* The source comments flag a possible use-after-free if the platform is deleted. Delete clears the
  pointers, which mitigates it.

### 5.6 Limits and performance
* No hard triangle cap. Cost is O(N) per floor/ceiling/wall query, with several queries per
  Mario per tick (each `perform_*_step` does multiple), so large maps are slow.
* **G64's answer** (Lua, `lua/entities/g64_mario.lua`):
  * pre-bakes the BSP brush and displacement triangles into a 2-D chunk grid
    (`libsm64.MapVertices[x][y]`, `DispVertices`), cached on disk via `LoadMapCache`
  * calls `StaticSurfacesLoad` with only the current chunk whenever Mario crosses a chunk
  * physics props and entities become surface objects
  * player and NPC cylinders become ckosmic `ObjectCollider`s
* Coordinates are int32, so precision is 1 unit (snapped), with no practical range limit.
  Upstream removed the old s16 +/-32767 limit in 2021-2022.

### 5.7 Units and axes in practice
* G64 module (`main.cpp`):
  * `sm64 = (-gx, gz, gy) * scaleFactor`, default 2.0, settable through `SetScaleFactor`
  * triangle vertex order is reversed (v3, v2, v1) when building surfaces
  * output vertices are mapped back with the inverse swap and a -5 Z offset
* libsm64-godot: `sm64 = (gz, gy, -gx) * scale`, default 100 (Godot metres).

---

## 6. Rendering output

### 6.1 Pipeline
1. The geo layout (wrapper plus `mario_geo_load_body`) is turned into a `GraphNode` tree once
   (`process_geo_layout`, decomp code).
2. Each tick, `geo_process_root_hack_single_node`:
   * resets the display-list pool
   * builds Mario's root matrix:
     * scale x rotate_zxy_and_translate(pos, angle), or
     * the `throwMatrix`, which `mario_actions_moving` sets to the terrain-aligned matrix while
       walking on slopes
   * sets the animation globals (advancing the frame)
   * runs the decomp's normal `geo_process_node_and_siblings` traversal
3. The traversal uses the decomp's own code for:
   * animated parts: translation/rotation from the anim index/values
   * switch nodes (Mario's geo callbacks pick eyes, hands, cap and metal/vanish variants from
     `MarioBodyState`)
   * scale and translation/rotation nodes
4. Display lists are appended to master lists per layer with the current matrix.
5. `geo_process_master_list_sub`:
   * **skips the first entry** (a "libsm64 HACK": an uninitialised projection matrix entry)
   * for each remaining entry calls `gSPMatrix` (converted with `guMtxL2F`) and `gSPDisplayList`
6. Both are **real functions in `gfx_adapter.c`**, not GBI macros.
7. The shadow node is fully commented out, so no shadow geometry is produced.

### 6.2 The "GBI" interpreter (`gfx_adapter.c`)
The display lists are not F3D words. They are streams of `intptr_t` produced by `gfx_macros.h`:
`VertexData(ptr,n,v0)`, `Triangle(a,b,c,flag)`, `Light(ptr,n)`, `Texture(s,t,on)`,
`SetTextureImage(index)`, `SetTileSize(uls,ult,lrs,lrt)`, `SubDisplayList(ptr)`, `EndDisplayList`.
Everything else (pipe syncs, combine modes, geometry modes, tile loads, env colour, alpha compare)
is emitted as a no-op. `process_display_list` walks this stream recursively:
* **Vertex:** remembers the current `Vtx` array pointer. Indices are absolute in that array; the
  16-vertex cache is not emulated.
* **Triangle:**
  * transforms the 3 positions by the current matrix (w=1) into **world space**
  * transforms the 3 normals (signed bytes / 128) by the matrix (w=0) and normalises them; there
    is no inverse-transpose, as a TODO in the source notes
  * writes the current light colour to all 3 vertices
  * writes atlas UVs if texturing is on, else (1,1)
  * updates `numTrianglesUsed`
* **Light:** for `n==1` (the diffuse light of a `Lights1`) stores `col/255` as the current colour.
  Ambient is ignored.
* **Texture/SetTextureImage/SetTileSize:** store the scale, on flag, atlas index (and its
  width/height) and tile origin.

### 6.3 Vertex and UV format delivered
* Per triangle: 3 x (xyz position), 3 x (xyz unit normal), 3 x (rgb 0..1 material colour),
  3 x (uv). Non-indexed triangle soup.
* UV conversion:
  1. The N64 texcoord (s10.5) is multiplied by the `G_TEXTURE` scale (>>16).
  2. The tile origin is subtracted (`uls`/`ult` are 10.2, so x8).
  3. The result is divided by 32 to get texels, then by the texture size to get 0..1.
  4. It is remapped into the atlas: `u = u*w/64/11 + index/11`, `v = v*h/64`.
* No clamp/mirror/wrap handling. Mario's textures do not rely on wrap, which is why an atlas works.
* Untextured triangles use UV (1,1). That is the empty, alpha-0 bottom-right corner of the atlas.
  With the reference shader (`mix(vertexColor, texel.rgb, texel.a)`) such triangles show the
  vertex colour, and textured ones blend by texel alpha. This replaces the N64 combiner
  (shade x texture) with one rule.
* Output is world space. The host just draws at identity and needs no skeleton. To interpolate,
  lerp every vertex between ticks; the triangle count and order are stable while the body state
  does not change.

### 6.4 Host-side use in G64
* Builds Lua tables of positions, normals, U, V and a colour index per vertex.
  * U is multiplied by `0.6875` = 704/1024: the atlas is uploaded into a 1024-wide render target.
  * Colours are mapped to an index in a 6-entry LUT built from the first ticks' distinct
    colours. This makes recolouring (custom Mario colours) possible without lib support.
* Buffers are 3x per triangle; unused tail entries are niled when the count drops.
* Mesh rebuilt every tick in Lua (expensive), with a separate path for fake (remote) Marios.

---

## 7. Audio

* **Engine:** the decomp's full N64 audio library (sequence player, sound banks, `external.c`
  sound request queue, effects, heap) compiled for `VERSION_US`. The RSP audio microcode is
  replaced by sm64-port's C `mixer.c` (ADPCM decode, resample, envelope mixer, interleave), with
  SSE4.1/NEON fast paths. libultra calls are stubbed in `ultra_reimplementation.c`.
* **Data:** CTL/TBL/sequence/bank-set data parsed from the ROM (4.5).
* **Output (upstream):**
  * `sm64_audio_tick(numQueuedSamples, numDesiredSamples, int16 *buf)`:
    * runs `update_game_sound()` (processes `play_sound` requests)
    * renders **two** audio frames of 544 samples (when the queue is below the desired level) or
      528, stereo interleaved
    * returns the per-frame sample count, so 2 x n x 2 int16 are written
  * Buffer requirement: 544x2x2 int16.
  * **Sample rate 32 kHz** (US audio session presets). The test opens SDL at 32000 Hz, s16,
    2 channels, and calls the tick every 33 ms on its own thread.
  * Volume: `gAudioVolume`.
* **Sound triggering:**
  * Game code calls `play_sound(bits, pos)`. libsm64's version appends to the decomp's
    `sSoundRequests` queue (if audio is initialised) **and** calls the host's registered callback.
  * Hosts can therefore either use the internal synth or play their own samples by sound id.
  * The `bits` encode bank, id, priority and flags (`SOUND_ARG_LOAD`). G64 mirrors the table in
    `g64_types.lua`.
* **ckosmic fork:**
  * the lib owns output: a pthread every 33 ms runs `audio_signal_game_loop_tick` and
    `audio_tick`
  * pushes to WASAPI (Windows) or Pulse/ALSA (Linux)
  * adds reverb and track-volume setters
  * G64 therefore never touches PCM in Lua; sound plays outside the Source engine mixer
* **Godot:** pulls `sm64_audio_tick` from an `AudioStreamGenerator` at mix rate 32000.

---

## 8. Build, platforms, tests, license, limitations

* **Build:**
  * GNU make, C only (test is C/C++)
  * Python 3 needed for the import scripts, which need **network access at build time**
  * `make lib` produces `dist/libsm64.so` / `sm64.dll` / `libsm64.dylib` (universal via `lipo`)
    plus `dist/include/libsm64.h`
  * `make test`/`make run` build the SDL2+GLEW test (GL 3.3 core and GL 2.0 renderers)
  * Windows uses MSYS2 MinGW-w64 (`OS=Windows_NT`)
  * musl static option `LIBSM64_MUSL=1`
  * Emscripten via `emmake make CC=emcc`
  * CI (`.github/workflows/main.yml`): linux, windows (cross MinGW), macos, web
* **ckosmic fork build:** the same Makefile plus `-lpthread`, `-mwindows` and a static
  `-lole32 -lstdc++` link on Windows. `build.bat`/`dll2lib.bat` make an MSVC import library
  (`sm64.lib`) from the MinGW DLL with dumpbin/lib; G64's MSVC module links that. CI runs on
  windows-latest + MSYS2 + msvc-dev-cmd and uploads `dist`.
* **Test/demo:** `test/main.cpp`, `context.c` (SDL window, gamepad), `gl33core/`, `gl20/`,
  `audio.cpp`. The level is BoB collision downloaded at build time.
* **License:** the repo is CC0, but it is decomp-derived code (whose legal status is the decomp's)
  and, upstream, the shipped binary contains Mario's mesh. The ckosmic/Retro64 forks avoid
  shipping the mesh. Forks have no license files.
* **Known limitations and bugs (from source):**
  * No geometry-buffer bounds check.
  * Normals are wrong under non-uniform scale.
  * The master-list skip hack.
  * O(N) collision.
  * No water boxes.
  * No objects (grab/throw/shell/cannon/doors), no shadows, particles or camera.
  * Not thread-safe.
  * Global sine tables come from the game.
  * The audio ROM copy leaks.
  * `sm64_mario_tick` has a TODO about a platform use-after-free.
  * ckosmic setters lack id checks.
  * ckosmic `sm64_mario_get_anim_info` returns without a value on bad ids.

---

## 9. Fork and binding landscape (what each contributed)

| Project | Contribution relevant to libsa1 |
|---|---|
| upstream libsm64 | Core pattern: g_state swap, linear collision, display-list to triangle adapter, ROM ripping of anims/textures, sound callback, `sm64_audio_tick` pull model |
| Retro64Mod/libsm64-retro64 | Runtime **geo-layout + F3D parser from ROM** (no compiled-in model), audio port from sm64-port, fake/animation-only Marios for multiplayer, alternate character meshes on Mario's skeleton |
| ckosmic/libsm64 `gmod` | Generic atlas ripper descriptor, object colliders, floor overrides, internal audio thread/backends, version query. The G64 dependency. |
| ckosmic/libsm64-gmod + g64 | Binary module and Lua: chunked map collision cache, prop/entity surface objects, ROM path convar, network replication of anim state, HUD/items in Lua, SDL gamepad |
| MeltyPlayer, headshot2017 (merged upstream) | Audio upstreamed (2022-12), sound API, setters, `forwardVelocity`/`animID`/`animFrame` in the state output |
| libsm64-godot | ROM SHA-256 check before init; fixed-step interpolation helper; audio stream at 32 kHz; scale-factor project setting |

---

## 10. libsa1: the analogous decisions for Sonic Adventure (DC US 1.005)

### 10.1 Piece-by-piece mapping
| libsm64 piece | SA1 counterpart | Decision for libsa1 |
|---|---|---|
| `baserom.us.z64` byte buffer, hard-coded ROM offsets | User's GDI. Track 3 ISO9660 holds `1ST_READ.BIN` (SHA-1 `e74c849806db103aa87f84e41a382873bc96b6df`, md5 `060cad2ceefc07c7429085f30a356046`) and `SONICADV/*.PRS/.PVM/.PVR/.MLT/.ADX/.BIN` | Asset layer opens the GDI itself, reusing the logic of `tools/extract_gdi.py`, or takes a host file-read callback. Validate the 1ST_READ SHA-1 and fail cleanly. Data offsets = DC address - `0x8C010000`. |
| MIO0 + RGBA16 to RGBA8 atlas | PRS (`tools/prs.py` logic) + PVM archive (`PVMH`) of PVR textures (twiddled, VQ, mipmapped, ARGB1555/RGB565/ARGB4444, palettised). `SONIC.PRS` decompresses to 108,000 bytes, a PVM of **28 textures**. | Port the PRS decoder and a PVR decoder to C. Deliver **separate textures** (array of w,h,RGBA8 + name), not one atlas, because SA1 UVs use wrap/mirror. |
| Mario anim table at `0x4EC000` (BE, to native) | `NJS_ACTION {NJS_OBJECT*, NJS_MOTION*}` and per-character `PL_ACTION` lists in 1ST_READ, e.g. `SonicActionList` `0x8C1C55A0`, 0x8E0 bytes = 142 x 16-byte entries (`actptr, objnum, mtnmode, next, frame, racio`) | Deserialise `NJS_MOTION` (per-node `mdata` key arrays: BAMS int32 angles, float positions/scales, frame counts, interpolation flags) into native structs at init |
| Mario geo layout + F3D DLs (compiled in, or parsed from ROM) | `NJS_OBJECT` hierarchy (0x34 bytes: evalflags, model*, pos[3], int ang[3], scl[3], child*, sibling*), **Ninja Basic** `NJS_MODEL` (0x28: points*, normals*, nbPoint, meshsets*, mats*, u16 nbMeshset, u16 nbMat, center, r), `NJS_MESHSET` (**0x18 on DC SA1**: u16 type_matId, u16 nbMesh, meshes*, attrs*, normals*, vertcolor*, vertuv*; SADX PC's is 0x1C), `NJS_MATERIAL` (diffuse, specular, exponent, attr_texId, attrflags). Sonic's root `0x8C1CE76C`: **73 nodes, 635 vertices, ~1001 triangles** (all meshsets are strips). | Parse at runtime from the user's 1ST_READ; nothing compiled in (the ckosmic model, 4.4). **Do not hard-code 1024 triangles**: compute the maximum per character at load and expose it. |
| `geo_*` callbacks (eyes, hands, cap) via the remap table | Character draw functions swap sub-models and morphs: curled/jump-ball models (`Sonic_Curled` `0x8C1D630C`, `Sonic_Jumpball` `0x8C1DBB2C`), foot morphs (`Sonic_*_Foot_Morph_1..3`), worn items (light-speed shoes, crystal ring), face/head animation (`sonic_face_s_head`), plus **weld lists** (`PL_JOIN_VERTEX[]`: objptr, srcobj, dstobj, numVertex, inpmode, depths, org, pnum; `SonicWeldList` `0x8C1C5FA4`) | Decompile the character display functions and run them against a Ninja emulation layer (10.7). Pointers to drawing callbacks inside data, if any, go through a remap table like ckosmic's. |
| `bhv_mario_update` + action state machine (`mario_actions_*.c`) | Character task exec: per-mode switch on `taskwk.mode`, with shared player physics (SADX names: `PGetAcceleration`, `PGetSpeed`, `PSetPosition`, `PGetRotation`, `PResetPosition`, `PConvertVector_P2G/G2P`, `PSetMotion`) and per-character `player_parameter` | Decompile and compile as-is for PC. This is the "physics" deliverable. |
| SM64 `struct Object` hack + stubs in `shim.h` | SA1 **task system** (`task {next, last, ptp, ctp, exec, disp, dest, ocp, twp, mwp, fwp, awp, thp}`; SA1 offsets match: twp at +0x20, mwp at +0x24). Characters spawn child tasks (shadow, effects, tails, etc.). | **Port the real task manager** (small engine code) and run one private task tree per library context, rather than faking objects as libsm64 did. Stub level-specific systems (set files, events, cutscenes, menus, Chao). |
| `GlobalState` swap | SA1 natively keeps **8 player slots**: 4 parallel 8-entry arrays at `0x8C78C608` (twp), `0x8C78C628` (mwp), `0x8C78C648` (task), `0x8C78C668` (pwp), see 10.14. Plus true globals: camera, gravity, stage/act, game mode, input arrays, timers. | Use the native slots for up to 8 characters per context. Swap a `GlobalState`-style block only for the remaining globals, and for more than 8 instances or several independent worlds. |
| `SM64Surface` triangles, floor/wall/ceiling by normal.y, linear scan | `_OBJ_LANDTABLE` (`ssCount`, `fFarClipping`, `pLandEntry[]`, motion entries, texlist...) of COL entries (0x24: center/size, `NJS_OBJECT*`, block bits, **attribute flags**: Solid 0x1, Water 0x2, NoFriction 0x4, NoAccel 0x8, IncAccel 0x80, Dig 0x100, NoClimb 0x1000, Hurt 0x10000, Footprints 0x100000, Dynamic 0x8000000, UseRotation 0x10000000, Visible 0x80000000, per SADX `ColFlags`). Plus dynamic collision entries for moving objects. The player is a sphere/cylinder (`rad`, `height` in `player_parameter`) tested against polygons in any orientation; the "shadow" ray (`GetShadowPos*`) finds the floor. | Host supplies **triangles with SA1 attribute flags** (static set + transformable objects). The library builds its own **spatial index** (uniform grid or BVH) instead of a linear scan, and emulates the collision-list gather (the `ListGroundForCollision`/`CL_ColPolListUp*` stage) from that index. No floor/wall/ceiling pre-classification: SA1 physics re-orients to the floor normal (wall running, loops, slopes). |
| Platform transform delta | Dynamic COL objects (SADX `DynamicCOL_Add`/`Remove`) with a task that moves them; the player is carried by its delta | Same API shape as `sm64_surface_object_create/move/delete`. The move computes the delta for carrying. |
| `gfx_adapter` triangle soup | Ninja draw calls (`njDrawModel`, `njAction`, `dsDrawMotion`, `late_*` queued draws for alpha sorting) feeding Kamui/PowerVR | Replace the Ninja draw layer with a CPU evaluator emitting triangles (10.7) |
| SM64 audio engine + mixer | Game-side SE queue (SADX names `dsPlay_oneshot`, `dsPlay_iloop`, `dsPlay_timer`, `dsStop_*`, `dsSoundServer`), the Katana "sd" driver (`sdMultiUnitDownload` `8C643970`, `sdDrvGetInfo` `8C65AAF4`), AICA ARM7 firmware (`SONICADV/MASARU.DRV`?), MLT banks (`SMLT`), ADX music (109 files), AFS voice archives | Phase 1: host callback for SE/voice/music ids plus a ripper that converts banks to PCM. Phase 2 (optional): a native mixer. See 10.9. |
| 30 Hz fixed tick | SA1 player logic is per-frame (60 Hz nominal) | 60 Hz fixed step; host interpolates |

### 10.2 Proposed public API shape (prose, C ABI, `SA1_LIB_FN`)
**Init and teardown**
* `sa1_global_init(source, out_error)`. The source is either a GDI path or a struct of host
  callbacks (open/read/size by disc path). It:
  * validates `1ST_READ.BIN`
  * maps the data image
  * deserialises character assets on demand
* `sa1_global_terminate()`.

**Assets out**
* `sa1_character_load(charId)`: Sonic=0, Eggman=1, Tails=2, Knuckles=3, Tikal=4, Amy=5,
  Gamma=6, Big=7, matching the physics-table order (10.14). Rips models, motions, welds, physics
  and textures.
* `sa1_character_texture_count(charId)` and
  `sa1_character_texture_get(charId, index, out w, out h, out const uint8 *rgba)`.
* `sa1_character_max_triangles(charId)`.

**Collision**
* `sa1_static_surfaces_load(const SA1Surface*, count)`, where `SA1Surface` = 3 float vertices +
  `uint32 attributes` (SA1 ColFlags). The library builds the grid.
* `sa1_surface_object_create(const SA1SurfaceObject*)`, `..._move(id, transform)`,
  `..._delete(id)`.

**Players**
* `sa1_player_create(charId, x, y, z, yaw)`.
* `sa1_player_tick(id, const SA1Inputs*, SA1PlayerState *out, SA1GeometryBuffers *outBuffers)`.
  Inputs: analog stick (-1..1), camera yaw (or camera-to-player vector), and buttons
  A/B/X/Y/L/R/Start as the DC pad bits.
* `sa1_player_delete(id)`.
* Setters mirroring libsm64's: position, velocity, angle, mode/action, water level, invincibility,
  rings, equipment upgrades.

**Network replay** (as in ckosmic)
* `sa1_player_get_anim_state` and `sa1_player_anim_tick`, so remote players are replayed, not
  simulated.

**Sound**
* `sa1_register_sound_callback(fn(eventType, soundId, bank, float pos[3], volume, pitch, pan,
  loop/handle))`.
* Optional `sa1_audio_tick(...)` if a native mixer exists.
* `sa1_register_debug_print`.

**Geometry buffers**
* Like libsm64, plus `uint16 textureIndex` per triangle (or the triangles grouped by texture with
  a range table), `uint32 materialFlags` (alpha blend/additive, double-sided, env-map) and RGBA
  vertex colour.
* Pass the capacity in, and **check it**.

### 10.3 Asset ripping pipeline (runtime, from the user's dump)
1. Open the `.gdi` and locate the high-density data track (track 3).
2. Read ISO9660 to find `1ST_READ.BIN` and `SONICADV/...`. `tools/extract_gdi.py` already does
   this in Python; port it to C.
3. SHA-1 check of `1ST_READ.BIN` (the README value above). Unknown versions are refused.
   Optionally add per-file hashes for `SONIC.PRS` etc.
4. **Models, motions, actions, welds, physics:** read from the `1ST_READ.BIN` image by DC address
   (file offset = addr - `0x8C010000`). The concrete Sonic entry points are in 10.14. Other
   characters' labels are in `reference/dc_sa1_data_labels.csv` (types `basicmodel`, `animation`,
   `NJS_ACTION`, `animationlist`, `weldlist`, `physicsdata`, `texturedata`, `NJS_TEXLIST`).
   Characters that live in overlays (e.g. Tikal's weld list in `TIKAL_PROG`) need PRS-decompressing
   that overlay and rebasing its pointers at its load address (`0x0C900000`-style keys, see
   `reference/README.md`).
5. **Textures:** each character's `NJS_TEXLIST` (e.g. `texlist_SONIC` `0x8C1C6C94`) names the
   textures. The pixels are in `SONICADV/<CHAR>.PRS`:
   * PRS-decompress
   * parse the PVM (`PVMH` header: flags, count, name table)
   * decode each PVR (GBIX, PVRT header, pixel format, data format: twiddled/VQ/rectangular/
     mipmapped/palettised)
   * convert to RGBA8
6. **Sound (later):** the `*.MLT` banks for the player SE/voice (e.g. `P_SONICTAILS.MLT`,
   `V_SONICTAILS.MLT`), `*.ADX` for music, and the `*.AFS` voice archives.
7. Nothing ripped is ever written to disk by the library. Keep everything in memory, as libsm64
   does. The host may cache converted textures locally if needed, but must not redistribute them.

### 10.4 Data model: the real porting problem
* SM64 data only needed byte-swapping, because libsm64 converts animation arrays into native
  structs.
* SA1 data are already little-endian, but **every structure with pointers is 32-bit-pointer
  shaped**: `NJS_OBJECT`, `NJS_MODEL`, `NJS_MESHSET`, `NJS_MOTION`/`mdata`, `NJS_ACTION`,
  `PL_ACTION`, `PL_JOIN_VERTEX`, texlists, landtables. On an x64 host (Garry's Mod x64 is
  required, as with G64) they cannot be used in place.
* Two options:
  * **(a)** Deserialise into native structs at load time: a recursive copy with
    pointer-to-pointer translation and memoisation of shared sub-objects, since models share
    meshsets and materials. This is the libsm64 load_anim_data pattern, generalised.
  * **(b)** Keep the DC image and access everything through 32-bit offsets with accessor macros.
    That requires editing all decompiled code that dereferences these structs.
* **(a) is recommended.** The decompiled C then uses normal pointers and compiles unchanged on
  both targets, provided the PC build uses native-pointer struct definitions while the SHC build
  keeps the original layout. Use `#ifdef` per struct where a field is a pointer.
* Code that indexes data by **fixed offsets or address arithmetic** must be found and rewritten
  for PC, for example:
  * `(char*)p + 0x24`
  * tables of pointers stored in the DC data segment
  * `0x0C9xxxxx`/`0x8C9xxxxx` mirror aliasing

### 10.5 Character code and the task system (the "engine" deliverable)
* SM64's object system was cut out because libsm64 only needed Mario. SA1 characters are tasks
  with child tasks, and their exec, display and destruct functions assume the task manager
  (allocation, execution order, deletion, "rotation" of the task list each frame).
* Recommended scope:
  * the task manager
  * the player shared code (P*/M* families)
  * the motion/"mtnjvwk" handling
  * the eight character modules
  * the shadow/attribute ray code
  * the collision-list gather
  * the player-related effect tasks: either kept with their drawing redirected to the triangle
    output, or stubbed and reported to the host as flags (as libsm64 does with `particleFlags`)
* Everything level-specific is stubbed in a `shim` header with constant answers, exactly like
  libsm64's `shim.h`: set files and object lists, cutscene/event code, menus, Chao, stage
  transitions, HUD, save data (event flags such as light-speed shoes become host-settable
  equipment bits).

### 10.6 Collision design for libsa1
* **Host input:** triangles in SA1 units (Y up), each with a 32-bit SA1 attribute mask; Solid is
  required for physics. Surface objects carry a local triangle set plus a transform (position +
  Ninja BAMS rotation, or a matrix), and are re-baked or transformed on move.
* **Library:**
  * Converts the host data into the structures the decompiled collision code expects. Either:
    * synthesise one `NJS_OBJECT` / Basic-model "COL" per chunk, so the original gather and test
      code runs unchanged; or
    * re-implement the gather to output the same per-frame polygon list format.
  * The first option keeps the most original code. The library chunks the static set into a
    uniform grid itself: G64 had to do this in Lua because libsm64 does not.
  * Moving objects map to the dynamic COL list.
* Water is a polygon attribute (Water 0x2) in SA1, not a global plane, so water volumes can come
  from the host's triangles. Engine "water planes" used by some stages are stubbed or host-set.
* There are no SM64-style 78-unit floor buffers or 4-wall caps; SA1 semantics apply. **Do not
  pre-classify by normal.y.**

### 10.7 Rendering: a Ninja evaluator in place of libsm64's display-list interpreter
* For each player draw:
  * evaluate `NJS_OBJECT` matrices with the current `NJS_MOTION` frame. This includes motion
    linking/blending (`racio`, `mtnmode` in `PL_ACTION`), "late" queued draws and the evalflags
    (`NJD_EVAL_UNIT_POS/ANG/SCL`, `HIDE`, `BREAK`, `ZXY_ANG`, `SKIP`).
  * transform the Basic-model points and normals
  * then run the **weld pass**: `PL_JOIN_VERTEX` copies transformed vertices between parts so
    joints are seamless. In SA1 this is CPU code operating on vertex copies (SADX name
    `PJoinVertexes`/`ProcessVertexWelds`), so it is game code to decompile, not a GPU feature.
* Meshsets:
  * decode strips (`NJD_MESHSET_TRIMESH`: per-strip u16 count with a winding-flip bit, then
    indices), triangles, quads and N-gons into triangles
  * per-polygon-vertex UVs (`vertuv`, Sint16 / 256 on Basic models; verify the scale) and optional
    per-vertex colours
* Material:
  * diffuse ARGB to vertex colour
  * `attr_texId` and `attrflags` to texture index and blend/flags: `NJD_FLAG_USE_ALPHA`,
    `USE_ENV` (environment map: UVs depend on the camera normal, so either output a flag and let
    the host do sphere-mapping, or take the camera matrix as input), `DOUBLE_SIDE`, `IGNORE_LIGHT`,
    `FLIP/CLAMP_U/V` (wrap modes; the host needs per-texture sampler state), src/dst blend.
* Output per tick: world-space triangle soup + texture index + material flags, as in libsm64.
  Optionally offer a "skinned" output (bone matrices + static mesh). It would cut per-tick
  bandwidth, but welds make SA1 meshes non-rigid, so the soup is the safe default.
* Sub-model swaps and morphs are decided by the character display code (decompiled), not by
  host logic.

### 10.8 Animation
* libsm64 stored SM64 animations natively and let the decomp's own `geo_update_animation_frame`
  run. libsa1 should do the same:
  * deserialise `NJS_MOTION`
  * keep the game's own motion-control code (frame advance, speed-scaled playback driven by
    `mtnjvwk.nframe` and the speed pointer, action chaining via `PL_ACTION.next`)
  * re-implement the **Ninja motion interpolation** primitives (keyframe search, linear/spline,
    angle shortest-path in BAMS, linked-motion blend) used by `njAction`/`dsDrawMotionLink`
* Those primitives are SDK (Ninja) code. Re-implement them from documented behaviour, or
  decompile them if the project decides SDK code may be included (see open questions).

### 10.9 Audio options
* libsm64 could reuse SM64's audio because it is portable C game code plus a reimplemented RSP
  microcode. SA1 SE playback runs **on the AICA's ARM7** from MLT banks, so there is no C mixer to
  reuse. Options, in order of effort:
  1. **Callback only** (as `sm64_register_play_sound_function`): decompile the game-side SE
     request/queue layer and report (SE id, bank, position, volume, pitch, pan, loop, stop) to the
     host. Rip the MLT/MPB/OSB sample data to PCM WAVs in memory at init and let the host play
     them. In GMod that means `sound.PlayFile` on runtime-written files or a native output; see
     open questions.
  2. **Native mini-mixer:** parse SMLT/MPB programs (key/velocity maps, loop points, envelope
     approximations), decode AICA ADPCM/PCM8/PCM16, mix at 44.1 kHz, and expose an
     `sa1_audio_tick` pull API like libsm64's.
  3. **AICA + ARM7 emulation** running the original driver. Most faithful, but heavy, and the
     available emulator cores are GPL.
* ADX music/voice decoding is simple (CRI ADX ADPCM).

### 10.10 Global state and multiple instances
* The native player arrays give 8 concurrent characters with per-task work areas, which is better
  than SM64.
* Still audit all `B_`/BSS globals touched by player code: camera, input, gravity, frame counters,
  water level, ring count, etc.
* Move them into a `GlobalState` with macro renames, as libsm64 does, so several independent
  "worlds" can exist. Bind state on every API call. Keep a single thread.

### 10.11 Timing, units, axes, numerics
* **Tick:** 60 Hz fixed. The host interpolates as G64/Godot do for SM64.
* **Units:** SA1 units, Y up, Ninja angles as int BAMS (0x10000 = 360 degrees).
  * Sonic's `player_parameter`: `rad` 4, `height` 10, `center_height` 5.4.
  * Choose a GMod scale so Sonic is about 36-40 GMod units tall. That is roughly 3.5-4.5 GMod
    units per SA1 unit (measure the model).
  * Keep the conversion on the host side, as libsm64 does.
* **Axis mapping:** as G64 does for SM64, swap to GMod Z-up in the module and reverse the winding
  of BSP triangles when needed.
* **Float literals:** SHC 5.1r03 with `-round=zero` **truncates float literals** (see
  `docs/compiler.md`). The matching C deliberately writes decimal literals "slightly above" the
  target bits. Compiled by MSVC/Clang (round-to-nearest), some literals will come out one ULP
  higher. For faithful physics the PC build must reproduce the exact constant bits: a hex-float
  macro, or a check that compares every float literal in the PC object against the DC binary.
* **Vector FPU instructions:** 1ST_READ uses SH-4 `fipr` 184 times and `ftrv` 152 times. FIPR/FTRV
  are not IEEE-exact on hardware, so PC results will differ in the last bits. Accept this.
  `fsca`/`fsrra` do not occur; trig appears table/software based, so the tables can be ripped
  from 1ST_READ at runtime instead of embedded (libsm64 embedded SM64's sine table in source).

### 10.12 Build for two targets
* The decomp's `src/` must keep matching SHC (C89, SH-4, 32-bit). Add a PC build profile
  (CMake or Makefile; MinGW-w64 and MSVC x64) that compiles the **same** C files with:
  * a PC `common.h`: types, `ASM_STUB` mapped to real C or to unavailable functions, no
    `#pragma` SHC-isms
  * `TARGET_PC` switches for pointer-bearing struct layouts and DC hardware/library calls
    (Kamui, Shinobi file I/O, `sd`, pad, VMU)
  * a `libsa1/` glue directory (API, asset loader, Ninja evaluator, collision index, task host,
    audio)
* **Coverage problem:** with ~21 functions decompiled today, the library can only be built once
  the player/physics/collision/motion/task call graph is decompiled (it need not match first:
  `NON_MATCHING` C is fine for PC). Track coverage of that call graph separately from matching
  progress.
* Output: `sa1.dll` (C ABI) + header. As with ckosmic, an MSVC import lib if the GMod module is
  built with MSVC.

### 10.13 What is fundamentally different from libsm64 (summary)
1. **No compiled-in game data at all.** SA1 assets are parsed from the user's dump (the ckosmic
   approach, extended to every asset type).
2. **32-bit pointer data structures** must be deserialised for x64. libsm64 only byte-swapped
   arrays.
3. **Ninja Basic models + NJS_MOTION + CPU welds/morphs** replace F3D display lists. The output
   needs multiple textures with wrap modes and material flags, so an atlas is not enough.
4. **Arbitrary-orientation polygon collision** with attribute flags and a gather stage replaces
   floor/wall/ceiling triangles. The library must provide its own spatial index.
5. A **real task system** with child tasks, and 8 native player slots, replace a single
   hand-allocated object.
6. **AICA-side audio** (MLT banks, ARM7 driver) replaces portable C audio: start with callbacks
   plus sample ripping.
7. **60 Hz** and SHC numeric quirks (`-round=zero` literals, `fipr`/`ftrv`), versus 30 Hz.
8. Multiple **characters** with separate physics tables, action lists and models, versus one.

### 10.14 Concrete SA1 leads found while writing this (verify before relying on them)
* **Physics table.** `player_parameter` is **0x84 bytes** (33 fields: int `jump2_timer`, then 32
  floats `pos_error` .. `center_height`). The table holds 8 contiguous entries from `0x8C1BDFE4`
  (file `0x1ADFE4`). Entries 8 and up are zero/unrelated. By the values (rad/height/weight), the
  order is:

  | Entry | Address | Character |
  |---|---|---|
  | 0 | `0x8C1BDFE4` | Sonic |
  | 1 | `0x8C1BE068` | Eggman |
  | 2 | `0x8C1BE0EC` | Tails |
  | 3 | `0x8C1BE170` | Knuckles |
  | 4 | `0x8C1BE1F4` | Tikal |
  | 5 | `0x8C1BE278` | Amy |
  | 6 | `0x8C1BE2FC` | Gamma |
  | 7 | `0x8C1BE380` | Big |

  **The SA Tools DC labels in `reference/dc_sa1_data_labels.csv` space these 0x108 apart and are
  therefore wrong for every character except Sonic.** For example, "Eggman_Physics_Data"
  `0x8C1BE0EC` is really Tails. Fix `config/symbols.1st_read.txt`.
* **Player init.** `func_8C06C350` (634 bytes) has 8 callers: `0x8C0A77BC`, `0x8C0B6148`,
  `0x8C0C0640`, `0x8C0CBE00`, `0x8C0D6B40`, `0x8C0DFFA0`, `0x8C0ED880`, `0x8C107020`. It:
  * takes (player number, task*)
  * allocates per-player blocks of 0x200 and 0x84 through `func_8C098B0C` (calloc-like, 49
    callers)
  * stores into the 8-entry arrays at `0x8C78C608`/`628`/`648`/`668`
  * reads the character id from `twp+9` (SADX keeps the char id in `twp->counter.b[1]`)
  * tests event flags 0x8D/0x8E/0x9F via `func_8C09068E` (SADX: light shoes, crystal ring,
    ancient light, i.e. equipment bits at `pwp+4`)
  * creates a child task with `func_8C09852E` (213 callers; likely `CreateElementalTask`)
  * The 8 callers are therefore probably the per-character main/exec functions.
* **Sonic's main function.** `func_8C0A77BC` (7,708 bytes) references `SonicActionList` and
  `SonicWeldList`, so it is very likely Sonic's main/exec (SADX `SonicTheHedgehog`).
  `func_8C0AE17A` (only caller: `8C0A77BC`) references `texlist_SONIC`.
* **Sonic data.** `Sonic_Physics_Data` is referenced by `func_8C06C350` and `func_8C075214`.
  Sonic asset labels:

  | Asset | Address |
  |---|---|
  | model root | `0x8C1CE76C` |
  | `sonic_z` basic attach | `0x8C1CD804` |
  | `Sonic_Textures` | `0x8C1C24BC` |
  | `SonicActionList` | `0x8C1C55A0` |
  | `SonicWeldList` | `0x8C1C5FA4` |
  | `texlist_SONIC` | `0x8C1C6C94` |
  | first motions | `Sonic_Stand` `0x8C1FE198`, `Sonic_Walk` `0x8C207150`, `Sonic_Run` `0x8C21151C` |
* **Ninja struct sizes on DC SA1**, checked by walking Sonic's hierarchy: `NJS_OBJECT` 0x34,
  Basic `NJS_MODEL` 0x28, `NJS_MESHSET` 0x18 (no SADX buffer field).

---

## 11. Requirements for the SA1 port (checklist distilled from libsm64)

1. A flat C ABI shared library (`sa1.dll`) with one public header, opaque integer ids, and no game
   data in the binary or the repository. The GMod side is a separate binary module (as
   `gmcl_g64_win64.dll`) linking it.
2. Asset input = the user's GDI (or host file callbacks). The library validates the `1ST_READ.BIN`
   SHA-1 `e74c8498...6df` and errors out on anything else.
3. A runtime asset layer in C:
   * GDI/ISO9660 reader
   * PRS decompressor
   * PVM/PVR decoder (twiddle, VQ, palettes, mipmaps) to RGBA8
   * deserialisers for NJS_OBJECT/MODEL/MESHSET/MATERIAL, NJS_MOTION/ACTION, PL_ACTION,
     PL_JOIN_VERTEX, player_parameter (0x84), texlists and (optionally) landtables, all from DC
     addresses (file offset = addr - 0x8C010000; overlays rebased at their key)
4. Decompiled (not necessarily matching), PC-compilable C for:
   * the task manager
   * player shared physics/collision/motion code
   * the 8 character modules (or the subset in scope)
   * shadow/floor ray, collision gather, weld/morph code
5. A `shim` layer stubbing level/event/menu/save/HUD/Chao systems with constant answers.
   Event-flag equipment becomes API setters.
6. A Ninja evaluator replacing the draw path:
   * hierarchy + motion (with linking/blending) to matrices
   * Basic meshsets (strips/tris/quads/ngons) to world-space triangles with normals, UVs, vertex
     colours, texture index and material flags
   * caller-sized buffers with capacity checks
   * per-character max triangle count exposed (Sonic's base body is already about 1,001 strip
     triangles)
7. Collision API: static triangles with SA1 attribute flags; surface objects with
   create/move/delete and carry deltas; an internal spatial index; no SM64-style classification.
8. A 60 Hz fixed-step tick per player:
   * inputs: stick, camera yaw, DC buttons
   * outputs: position, velocity, angles (BAMS and radians), mode/action, motion id/frame, flags,
     rings, equipment, effect flags
   * an anim-only replay entry point for networked remote players
9. Multi-instance via the native 8 player slots, plus a GlobalState swap for remaining globals.
   Single-threaded contract documented.
10. Sound via callbacks first (SE/voice/music events with positions). PCM ripping of MLT/ADX/AFS
    to in-memory buffers. Optional native mixer later.
11. Dual build: SHC matching build untouched; a PC build of the same sources behind `TARGET_PC`
    switches. Float-literal bit-exactness check against the DC binary. Accept `fipr`/`ftrv`
    precision drift.
12. Licensing hygiene:
    * no disc-derived bytes (models, tables, textures, sine tables) committed or linked into the
      DLL
    * decide on SDK (Ninja/Shinobi/sd) code: reimplement rather than ship decompiled SDK code
      (open question)

---

## 12. Open questions

1. **SDK code.** Should Ninja motion/matrix/model primitives and Katana `sd` code be decompiled
   and shipped, or reimplemented from documentation? libsm64 shipped only game code plus
   sm64-port's clean reimplementation of the RSP audio microcode.
2. **Logic rate.** Is SA1 DC's player logic strictly per 60 Hz frame in all modes, or are there
   30 Hz-increment paths (SADX has a frame-increment variable) that the library must replicate?
3. **Scale.** Sonic's rendered height in SA1 units, to pick a default GMod scale factor (G64 uses
   2.0 for SM64). Measure from the evaluated model.
4. **Weld timing.** Exact semantics of `PL_JOIN_VERTEX.inpmode` and depths on DC, and whether
   welds run in the display function (per draw) or in exec. This affects whether `anim_tick`
   replay must also run welds.
5. **Collision representation.** Synthesise COL `NJS_OBJECT`s per grid chunk so the original
   gather code runs unchanged, or rewrite the gather? This depends on how tightly
   `CL_ColPolListUp*`-style code is bound to the landtable/blockbit layout. It needs reading of
   the SA1 collision functions (not yet located in 1ST_READ).
6. **GMod audio.** Can a GMod client module play runtime-decoded PCM through Source's mixer?
   Otherwise, own a WASAPI output as ckosmic's libsm64 does (sound then ignores GMod's volume and
   occlusion), or write temporary WAVs into `garrysmod/data` for `sound.PlayFile` (check what GMod
   allows). The G64 research note should answer this.
7. **Environment mapping.** Which character materials use `NJD_FLAG_USE_ENV` (e.g. Gamma and
   Metal parts)? Should the library compute env UVs (needs camera input) or flag them for the host
   shader?
8. **Physics labels.** Confirm the physics-table order and fix the 7 mislabelled SA Tools physics
   addresses (10.14). Check whether other SA Tools DC labels share the 0x108-vs-0x84 stride
   mistake.
9. **Other characters.** Which character assets live in overlays rather than 1ST_READ (Tikal:
   `TIKAL_PROG`; Eggman; Super Sonic: `SUPERSONIC.PRS` textures)? Is v1 scope Sonic only, or all
   six playable characters?
10. **Texture source.** Is `Sonic_Textures` (`texturedata` `0x8C1C24BC`, 0x18 bytes) the
    PVM-name/texlist binding the loader should follow, rather than hard-coding `SONIC.PRS`?
11. **Determinism.** Is bit-exact determinism between clients needed for networking? G64
    sidesteps it by simulating locally and replicating animation state, which is recommended here
    too.
