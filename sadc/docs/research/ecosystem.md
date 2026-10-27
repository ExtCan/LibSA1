# Ecosystem survey for an SA1 "libsm64-style" Garry's Mod port

Date: 2026-10-06. Scope: projects around the planned port of Sonic Adventure (Dreamcast, US v1.005)
engine, physics and playable characters to a PC library driven by Garry's Mod, in the style of
G64/libsm64. The port must take the user's own GDI and rip assets from it at run time.

Companion documents in this folder (read them for depth; this file does not repeat them):

* `g64.md`: deep read of G64 (Lua addon, binary module, ckosmic's libsm64 fork, installer).
* `libsm64.md`: deep read of libsm64 and a proposed "libsa1" API.

Method. Every repository below marked "read" was cloned with `git clone --depth 1` into
`<scratch>\repos\eco\`
(metadata-only clones are in `...\repos\eco\meta\`; G64 and upstream libsm64 are in `...\repos\`).
The commit and date shown are the clone's HEAD. "No license file" means none was found in the
repository; treat such code as all rights reserved and use it as reference only. Nothing from
third-party code is copied here. Identifiers and API names are quoted where needed.

Steam Workshop pages returned HTTP 429 to the fetcher, so Workshop facts come from search
snippets and are marked as such.

---

## 0. Key takeaways

1. **The libsm64 contract carried over to every host with almost no changes.** It has a C ABI of
   plain structs; the host feeds triangle collision; the library returns a skinned world-space
   triangle list each tick plus one texture made at init; the tick is fixed with host-side
   interpolation; audio is pulled as PCM. Bindings exist for C#, Java (JNA and JNI), Python
   ctypes, GDExtension, Lua C modules and Rust. An SA1 library should keep this contract (section 1.8).
2. **Collision import is always the host's job**, and it is where every port spends its effort.
   The patterns are: bulk static triangles once, plus per-object surface objects with a
   transform; streaming collision near the player (Minecraft, ClassiCube, GTA SA); or
   re-implementing the game's floor, ceiling and wall queries on the host's raycasts (Roblox).
3. **Multiplayer in every libsm64 port is owner-authoritative.** Each client simulates only its
   own character and sends a pose: position, animation id, animation rate, rotation, flags. The
   others re-pose a "fake" character from that pose without running physics (G64's
   `sm64_mario_anim_tick`, ClassiCube's plugin messages, the Roblox RemoteEvent). An SA1 library
   needs a pose-only path from the first version: action/motion id, frame and root transform
   become a mesh.
4. **No Garry's Mod addon runs real SA physics.** The Sonic content on the Workshop is
   playermodels and ragdolls (many removed for content-guideline violations, according to search
   snippets), SWEPs that change the Source walk and jump speeds, and Pill Pack morphs. G64 is the
   only GMod addon that runs a decompiled engine.
5. **The closest blueprint is `darkpbiMods/sonic-adventure-2-rust` (MIT, 2026-10-05).** It is an
   SA2B (GameCube) Sonic controller made of translated original routines. The host implements a
   `World` trait (collision, targets, rings, events), the simulation is fixed at 60 Hz, and an
   importer checks the user's `main.dol` SHA-256 and extracts the numeric data and a CPU pose
   bundle. One weakness to avoid: the imported data is compiled into the build, so its binaries
   cannot be redistributed. We must load the data at run time.
6. **SARecomp (the SA1 DC PAL v1.003 static recompilation) maps almost 1:1 onto our US v1.005
   build.** A function-gap alignment of its 4,815 PAL function entries against our 6,376 US
   function starts gives:
   * the same addresses for `0x8C010000..0x8C055794`;
   * `+0xC0` from about `0x8C055F40` to `0x8C66BF54`.

   Every SARecomp-named routine checked lands on a US function start. That includes the player
   mode dispatchers, the collision world producer, the movement/contact owner and the animation
   hierarchy (section 3.2). Its docs are therefore a free map of the SA1 hot paths.
7. **Bit-faithful physics on PC needs FPU care.**
   * Katana runs with FPSCR `0x00040001`: round toward zero, denormals flushed.
   * SHC truncates float literals (`-round=zero`; see `docs/compiler.md`).
   * 1ST_READ contains 3,134 FMAC, 184 FIPR and 152 FTRV instructions. There are also 119 FSCA
     and 70 FSRRA encodings, emitted as `.DATA.W` because asmsh cannot spell them.

   The PC build must:
   * emit literals bit-exactly;
   * run the tick with MXCSR set to round-to-zero and FTZ/DAZ, restoring the mode afterwards;
   * decide on fused FMAC;
   * use a table for FSCA (the Flycast-captured table, as dream-recomp does).
8. **Run-time ripping is easy because the data is uncompressed and fixed.**
   * Character models, motions, action lists, weld lists and physics are plain structures in
     `1ST_READ.BIN`, which is loaded at `0x8C010000`.
   * Textures are PRS-compressed PVM archives (`SONIC.PRS` decompresses to a 28-texture PVM).
   * Player sound effects and voices are Manatee MLT banks (`P_SONICTAILS.MLT`,
     `V_SONICTAILS(_E).MLT`).
   * Music is ADX, and cutscene voices are in `EVENT_ADX(_US).AFS`.

   Every format has open documentation and at least one permissively licensed decoder (section 4).
9. **Data bug to fix in our reference set.**
   * The SA Tools `DC_SA1/1st_read.ini` places the physics entries `0x108` apart. The real
     `player_parameter` table has 8 contiguous entries of `0x84` bytes, starting at `0x8C1BDFE4`.
   * Only Sonic's row is correct. AdventureRip's `figure/big/Physics.ini` is garbage.
   * `reference/dc_sa1_data_labels.csv` inherited the wrong rows.

   Correct addresses are in section 2.4.1.
10. **Licensing.**
    * libsm64 is CC0.
    * Most hosts, binding layers and SA tool libraries are MIT, BSD or CC0.
    * Several key Dreamcast pieces are GPL: prs-rs, manatools and SA3D.Modeling are GPL-3.0;
      Flycast, dream-recomp and SRB2 are GPL-2.0.
    * The SA mod-loader headers carry an "educational purposes, commercial use prohibited"
      disclaimer and no license.
    * SARecomp, G64, sa2dc and dc-manatee have no license file.

    Write our own decoders: the formats are small. Use these projects as documentation only
    (section 5).

---
## 1. (a) libsm64 and the hosts that consume it

### 1.1 The core contract (summary; details in `libsm64.md`)

`libsm64/libsm64` (CC0-1.0, read @fd11813, 2026-02-13, about 960 stars).

* **Assets.** The library takes the ROM bytes at init. It builds Mario's animation table and the
  texture atlas from fixed ROM offsets, and, with audio, the sound and sequence banks.
  * The atlas is `SM64_TEXTURE_WIDTH` x `SM64_TEXTURE_HEIGHT`, which is 704 x 64 RGBA.
  * Mario's **geometry is not ripped.** `import-mario-geo.py` downloads `actors/mario/geo.inc.c`
    and `model.inc.c` from n64decomp/sm64 at build time and compiles them in. The shipped DLL
    therefore contains Nintendo vertex data.

  Our port should go further and rip the meshes too, so that the binary holds no SEGA data.
* **Simulation.** The tick is fixed at 30 Hz. `sm64_mario_tick(id, inputs, outState, outBuffers)`
  returns the state and up to `SM64_GEO_MAX_TRIANGLES` (1024) triangles in world space. Each
  triangle carries position, normal, colour and uv, already skinned on the CPU.
* **Collision.** The host passes triangles in the game's units:
  * `SM64Surface {type, force, terrain, int32 v[3][3]}`. Older versions used int16, which forced
    clamping in Blender and coordinate extensions in Retro64.
  * `sm64_static_surfaces_load` loads the static world.
  * `sm64_surface_object_create`/`_move`/`_delete` handle moving platforms.
  * Query helpers for floor, ceiling, wall, water and gas exist.
* **Audio** is optional. `sm64_audio_init(rom)` sets it up and
  `sm64_audio_tick(queued, desired, int16 *buf)` produces 32 kHz stereo PCM pulled by the host.
* **Gameplay hooks:** set action, position, velocity, health, caps and water level; take damage;
  attack.

### 1.2 Forks that matter

| Fork | License | HEAD | What it adds |
|---|---|---|---|
| `ckosmic/libsm64` branch `gmod` (used by G64) | no license file (upstream CC0) | b7bfef1, 2022-09-03 | Audio from sm64-port played by an **in-library SDL audio thread**, plus `sm64_set_volume`/`_reverb` and `sm64_get_sound_arg`. **Pose-only remote players**: `sm64_mario_create(..., fake)`, `sm64_mario_get_anim_info`, `sm64_mario_anim_tick(id, stateFlags, SM64AnimInfo*, buffers, rot[3])`. Cylinder colliders `sm64_object_create({pos, height, radius})`. `sm64_set_mario_floor_override(terrain, floorType, floorForce)`. Lives, version query. |
| `Retro64Mod/libsm64-retro64` | no license file | 7afa8ed, 2025-02-24 | The first audio port; coordinates beyond +-32768; power-up, teleport, damage and water helpers. G64's README credits it for sound and multiplayer. |
| `MeltyPlayer/libsm64-ext` | CC0-1.0 | 685da11, 2025-09-16 | Exports the raycast and collision helpers for the C# bindings. |
| `headshot2017/libsm64-linux-audio-fix` | (not cloned) | - | Fixes a Linux crash in ckosmic's audio fork. |

### 1.3 Hosts

| Host / project | URL | License, HEAD | Collision import | Rendering | Audio | Multiplayer |
|---|---|---|---|---|---|---|
| **Garry's Mod: G64** | github.com/ckosmic/g64, /libsm64-gmod, /G64Installer | no license files; g64 6de9c9e 2023-03-06; module 198dc63 2022-09-03 | Map BSP parsed **in Lua** (bundled `g64_luabsp.lua`, from h3xcat/gmod-luabsp). The vertices are cached to `garrysmod/data` in X/Y chunks and reloaded by the module's `LoadMapCache`. Displacements are kept separately. Props (`g64_physbox`) become surface objects with transforms; NPCs and players become cylinder objects. Water level comes from a per-tick `util.TraceLine` with `MASK_WATER`. A tool gun (`g64surfacechanger`) overrides surface type. | Lua `IMesh` rebuilt with `mesh.Begin(MATERIAL_TRIANGLES)` from vertex tables that the module fills, plus a separate wing mesh. The vertex-table round trip through Lua is the bottleneck (see `g64.md` section 5). | SDL thread inside the fork; Source's sound system is bypassed. | Client-authoritative. Each tick the owner sends `G64_TRANSMITMOVE`: position int16 x3, animID int16, animAccel int32, rotation int16 x3, health 4 bits, flags u32 (about 23 bytes). The server relays `G64_TICKREMOTEMARIO`, and receivers re-pose a fake Mario with `MarioAnimTick`. About 26 net strings in total (colours, caps, damage, held objects, coins). |
| Unity (official demo) | github.com/libsm64/libsm64-unity | no license file; 21f6737 2022-04-15 | `MeshCollider` of objects carrying `SM64StaticTerrain` -> triangles. X is mirrored (right-handed to left-handed) and the winding is swapped (v1<->v2); scale factor 100. `SM64DynamicTerrain` -> surface objects moved each `FixedUpdate`. | Double-buffered position/normal arrays, lerped in `Update` between `FixedUpdate` ticks; a `Mesh` with a fixed index list of `3*1024`. | none | none (a multiplayer demo was shown on Twitter only) |
| Godot 4 | github.com/Brawmario/libsm64-godot | MIT; 7d7be1e 2026-04-03 | Nodes in groups `libsm64_static_surfaces` and `libsm64_surface_objects`. Faces come from `MeshInstance3D.get_faces()`, from `BoxShape3D` (unit-box face table x size) and from `ConcavePolygonShape3D`. A per-node `LibSM64SurfacePropertiesComponent` sets surface type and terrain. | `ArrayMesh` rebuilt per tick. A C++ `LibSM64MarioInterpolator` lerps vertices and state between ticks, combined with Godot's physics interpolation fraction. | `AudioStreamGenerator` at 32 kHz; `audio_tick` is called on a 30 Hz accumulator, and pushing stops once about 12k frames are buffered. | none built in |
| Blender | github.com/libsm64/libsm64-blender | no license file; 03dcdd9 2026-01-21 | All scene meshes -> surfaces with Fast64 collision types. Z-up is converted to Y-up, and coordinates are **clamped to the int16 range**. | Updates a Blender mesh datablock from a modal timer. | none | none |
| Minecraft (Fabric 1.21.4): mario64-in-minecraft | github.com/Zckyy/mario64-in-minecraft | MIT; 681c685 2026-10-01 | Rebuilt when Mario enters a new block, and every 1/3 s: exposed faces of block collision boxes in a 17x14x17 box. Stairs become 45-degree ramps, short boxes become step-ups, lava becomes a burning floor. Every column always gets the first floor below it, because **"no floor" acts as a wall in SM64**. About 1 ms per rebuild. Uses a **floating origin**, because world coordinates overflow SM64's integers. | Drawn as the local player entity in two passes: vertex colour, then texture alpha nudged along the normals. This lets shader packs treat Mario as a mob. | Dedicated thread -> Java Sound 32 kHz; all audio calls are queued onto it. | Single-player only: the server judges moves against Steve's hitbox. **Users build `sm64.dll` themselves**; even the compiled decomp is not distributed. |
| Minecraft: Retro64 | github.com/Retro64Mod/Retro64Mod | MIT; b1fab2c 2026-09-27 | Blocks near Mario (JNI). | entity render | A separate executable handles audio (README). | yes (README video; not source-read) |
| ClassiCube: Classic64 | github.com/headshot2017/Classic64 | no license file; 0f3795f 2026-09-13 | One surface object per nearby block, created and deleted as Mario moves, plus two static floor triangles. | ClassiCube plugin mesh | SDL2 | CPE **plugin messages** (64-byte payload, channel 64): opcodes for tick, colours, cap and "has plugin". An `MCGalaxy` server plugin relays inputs, colours and cap changes. |
| GTA San Andreas | github.com/headshot2017/sm64-san-andreas | no license file; 1c0ef9a 2025-05-05 | Each tick, scans entities near Mario and turns their `CColModel` triangles, and boxes as 12 triangles, into surface objects (Y/Z swapped). | Direct3D 9 (`d3d9_funcs.cpp`) | yes | n/a |
| Rocket League: Supersonic Mario | github.com/Serialbocks/SupersonicMarioPlugin | MIT; e2810d8 2024-09-11 | Arena geometry | Own **D3D11 injection** to draw the mesh (BakkesMod plugin). | Python extraction of ROM banks | Own server on ports 7777/7778 TCP/UDP. FPS must be a multiple of 30. |
| Roblox | github.com/MaximumADHD/sm64-roblox | no license file (README asks for credit); 79d5372 2024-08-04 | **No libsm64.** Mario's movement is hand-ported to Luau. `find_floor`/`find_ceil`/`find_wall_collisions` are re-implemented on Roblox raycasts: floor is a ray down, ceiling a ray up, walls are 4 cardinal rays of the wall radius. An option truncates positions to int16. | Roblox character, with animations retargeted to R15 (no ROM assets) | Roblox sounds | RemoteEvent `LazyNetwork` with per-message validators |
| Unreal Engine 5 | github.com/LunaRyuko/SMUE5 | no license file; c1678c0 2022-12-20 | Not implemented (the README says so) | `ProceduralMeshComponent`; right-handed Y-up -> left-handed Z-up | none | none |
| Others (README or awesome-list only) | GZDoom (headshot2017/gzdoom-sm64, GPL-3.0), OpenLara (BSD-2), DDNet, AugmentedMario (ARKit/Metal), Venus (Unity machinima), Sonic Generations "SM64 Generations" (closed source) | | | | | |
| Bindings | nickmass/libsm64-rust (MIT), MeltyPlayer/libsm64-sharp (GPL-3.0; bundles a Quad64 level loader), lammmab/libsm64-odin, headshot2017/libsm64-gm8 | | | | | |

Index: `SuperSonicHub1/awesome-libsm64` (CC0, fb74383 2023-12-22).
### 1.4 Collision import: the patterns

1. **Static bulk plus dynamic objects.** Unity, Godot, Blender and G64 work this way. The static
   world is uploaded once as a triangle soup. Moving and physics objects become surface objects
   with a transform updated every tick.
2. **Streaming near the player.** Minecraft, ClassiCube and GTA SA work this way. Only the
   collision inside a box around the character is converted, and it is rebuilt when the
   character crosses a cell. This is cheap and avoids importing huge worlds.
3. **Query re-implementation.** Roblox works this way. The game's collision queries are
   re-implemented on the host's raycasts and the game's own collision system is skipped. This is
   lossy: the wall test becomes 4 rays.
4. **Units, handedness and winding.** Every host does a scale factor, an axis flip and a winding
   swap. Bugs here show up as walls that act as floors.
5. **Engine quirks that leak.** SM64 treats "no floor" as a wall (Minecraft adds a fallback
   floor) and has 16-bit coordinate limits (Blender clamps; Minecraft uses a floating origin;
   Retro64 extends the limit). G64's map cache solves "too many triangles in Lua".

**SA1 consequences.** SA1's landtable collision is per-object (`COL` entries with
`SurfaceFlags`) with float vertices, so there is no int16 limit. Its collision routines select
candidate objects near the player each frame. In SARecomp this is the "collision world" group at
US `0x8C028EC2`, then the "eligibility selection" at `0x8C052518`, then the movement owner at
`0x8C0730D8` (section 3.2). Those are the natural seams.

The SA1 library should accept host geometry as "COL-like" objects. Each object is a triangle mesh,
a transform, surface flags (solid, water, slippery, no-footprints, ...) and an optional moving or
rotating transform. Mapping those onto the original candidate and broadphase lists keeps the
original contact code intact. This is pattern 1 for static brushes and props, plus the game's own
per-frame candidate culling, so pattern 2 comes for free.

### 1.5 Rendering

* All hosts consume the per-tick, world-space, CPU-skinned triangle list and the one-time
  texture. Nobody uses a skeleton. This made porting trivial but cost two things:
  * remote players need a separate pose-only call;
  * a host cannot blend its own animations.
* Interpolation from 30 Hz to the display rate happens in the host (Unity, Godot, Minecraft) or
  in Lua (G64).
* Rocket League and GTA SA draw through injected D3D. GMod does not need this, because `IMesh` is
  available.

**SA1 differences.**

* Characters use many textures (Sonic's PVM has 28; Amy's 53; Super Sonic's 73), so a single
  704x64-style atlas is not given. Either output per-material batches (texture id, blend and flag
  bits from `NJS_MATERIAL.attr_texId`) or build an atlas at load time and remap uvs.
* SA1 also welds vertices between body parts after posing (`PL_JOIN_VERTEX`, `PJoinVertexes`), so
  the CPU skinning path is the natural output, as in libsm64.
* Offer a second output mode as well: bones (an `NJS_OBJECT` hierarchy with per-node matrices)
  plus a static bind mesh. This would allow GPU skinning in a C++ module or a GMod
  `ClientsideModel` later.

### 1.6 Audio

The pattern is the same everywhere: the library renders PCM and the host pulls it, or the library
owns an output thread (the G64 fork uses SDL). For GMod, an in-module output (SDL or miniaudio)
was the practical choice, because GMod Lua cannot stream raw PCM into the Source sound system.
Expect the same for SA1. The SA1 audio sources are listed in section 4.8. SFX are
Manatee MIDI-driven banks, not raw samples.

### 1.7 Multiplayer

The pattern is owner-authoritative: replicate a compact pose and re-pose remote characters.

* G64 sends about 23 bytes per tick through the server.
* ClassiCube uses 64-byte plugin messages relayed by an MCGalaxy plugin.
* Roblox uses a validated RemoteEvent.
* Supersonic Mario runs a custom server.
* `kellsnc/sadx-multiplayer` uses ENet in SADX (section 2.2). It sends input and state packets:
  location, mode/smode, flag, anim, rings, score, lives and more.

None of them simulates remote characters authoritatively.

**For SA1:** per-tick `{pos, ang (3 x BAMS int16 or int32), action/motion id, frame (float),
flags, mode}` is enough for posing. Sound triggers can ride along as events.

### 1.8 What was reusable across hosts, and what to keep for "libsa1"

* One small C header of POD structs, no callbacks except debug print and play-sound, and no
  engine types. That is why bindings appeared in seven languages.
* User-supplied asset blob in, plus a hash check before init.
  * Godot checks the ROM SHA-256 before `global_init`, because a bad ROM crashes the library.
  * Blender checks the SHA-1.
  * G64 does **not** validate (see `g64.md` section 2.2).
  * SARecomp verifies the GDI tracks by size and SHA-256 (section 3.1).

  For our US 1.005 disc:
  * `1ST_READ.BIN` md5 is `060cad2ceefc07c7429085f30a356046` (`reference/README.md`);
  * `track03.bin` is 1,185,760,800 bytes at LBA 45000;
  * the GDI has 3 tracks (`track01.bin` 34,254,528 bytes, `track02.raw` 2,147,376 bytes at LBA 14714).
* A fixed tick plus host interpolation helpers.
* An optional audio pull API with a fixed sample rate.
* Owner-authoritative networking with a pose-only API.
* Separate binary distribution. Workshop cannot carry DLLs, so G64 ships an installer and an
  auto-updater. GMod module names are `gmcl_<name>_win64.dll`, `_win32.dll`, `_linux.dll` and
  `_linux64.dll`. G64 supports only 64-bit Windows.

---

## 2. (b) Sonic character ports and physics recreations

### 2.1 Sonic in Garry's Mod today

None of these is on GitHub; the GitHub search API returned no relevant repositories. The Steam
facts come from search snippets.

* **Playermodels, ragdolls and NPC packs** made from ripped game models, for example "Sonic Redux
  Pack", "GMod SonicPack" and "SA1 styled Classic Sonic". Snippets say many Sonic items were
  removed from the Workshop for content-guideline violations. That is a direct argument for
  run-time ripping from the user's disc.
* **SWEPs.** Examples: "Sonic SWEP" (id 352460642), "Improved Sonic SWEP" (2921224410, Jan 2023)
  and "Sonic SWEP(FIXED)" (264373973). They toggle the Source player's run speed and jump height
  ("Hyperspeed" and "Godspeed" modes) and add a spin-dash sound. They use the Source movement
  controller, so there is no momentum or slope physics.
* **Pill Pack morphs.** "Sonic R Pill Pack" (328646709) needs Parakeet's Pill Pack. It morphs the
  player into characters with reproduced animations for races.
* **Vehicles and maps:** the SADX Tornado, and Station Square / Speed Highway maps.

Conclusion: nothing to reuse technically. The niche, real SA physics in GMod, is empty.
### 2.2 Projects derived from original code

| Project | URL | License, HEAD | What it is | What to learn or reuse |
|---|---|---|---|---|
| **sonic-adventure-2-rust** | github.com/darkpbiMods/sonic-adventure-2-rust | MIT; d87dfff 2026-10-05 | Reusable Sonic controller in Rust, translated from SA2B GameCube `main.dol` routines (SHA-256 `a5333530...`); source addresses are given in comments. The `runtime` crate contains hosts for ground motion, air motion, bounce, animation clock and animated vertices. The `playable` crate's `simulation::World` trait asks the host for: optional source-unit terrain, a `move_player` step, `ground_normal`, homing targets (position, radius, kind, flags), rings, `attack_path_clear`, spawn and respawn, events and vibration. A transactional `PlatformMemory` is passed to "platform callbacks". The tick is fixed at 60 Hz, with a `Clock` that keeps input edges across variable frame rates. `tools/import_assets.py` checks the game hash and extracts the numeric controller data and an optional CPU pose bundle from `plcommtn.prs`, `sonicmtn.prs` and `sonicmdl.prs`. | **The best design reference for our API.** Copy the ideas: a host `World` interface; separating the pose sampler from rendering; events for audio; tests against original-execution fixtures. Do not compile imported data into the binary (they do; `playable` embeds it at build time). PROVENANCE.md is a good model for our own provenance statement. |
| **SARecomp** | github.com/sonicfreak1337/SARecomp | no license file; d5288fe 2026-09-24 (development halted) | Native PC port of SA1 DC **PAL v1.003** by static recompilation (KatanaRecomp). See section 3.1. | Address map of hot paths; GDI install and verify flow; graphics and audio replacement strategy. |
| **sadx-multiplayer** | github.com/kellsnc/sadx-multiplayer | no license file; 0b6525a 2025-07-16 | SADX PC mod (on the SADX Mod Loader) adding split-screen and ENet netplay. There are about 100 `patches/patch_*.cpp` for objects, bosses and the camera that assumed player 0. | SA1 code reads `playertwp[0]`, `playerpwp[0]` and similar in many places. The SADX arrays hold 8 slots: `playertp`, `playertwp`, `playermwp`, `playerpwp`. Multiple characters per library instance therefore need per-player indexing in every decompiled routine we take. Its packet list is a good minimal replication set. |
| **SADX-cutscene-decompilation** | github.com/X-Hax/SADX-cutscene-decompilation | no license file; 9ee24f5 2024-04-21 | Hand-written C++ for all 206 SADX event scripts, using the `EV_*` API (`EV_GetPlayer`, `EV_SetPos`, `EV_Wait`, `EV_PadOff`, ...). | Names and patterns only; cutscenes are out of scope for us. |
| **SonicAdventure64** | github.com/cuckydev/SonicAdventure64 | no license file (SM64 decomp base); faa4542 2021-05-20 | SM64 decomp with Mario replaced by "Sonic" (`src/game/sonic*.c`, about 2.5k lines). Its `PhysicsParam` struct has the same 33 fields and **the same values as SA1 Sonic's `player_parameter`**. The logic is re-implemented with quaternions on SM64 surfaces (walk, spindash, roll, homing, water). | Shows that SA values without the original routines do not reproduce the feel. A cautionary example: decompile the real routines. |
| **Adventure.Physics.Swap.ReloadedII** | github.com/Sewer56/Adventure.Physics.Swap.ReloadedII | no license file; 0081a1d 2025-10-30 | A Reloaded-II mod that swaps and edits the physics struct in SADX (table at `0x9154E8`), SA2B (`0x17391E8`) and Heroes (`0x8BE550`). | `AdventurePhysics.cs` gives community descriptions of each field, for example `jump2_timer` = "hang time" and `pos_error` = "floor grip". Pair them with the original field names from section 2.4. |
| **SA mod loaders** | github.com/X-Hax/sadx-mod-loader, /sa2-mod-loader | No license; README disclaimer: "informational and educational purposes only. Commercial usage is expressly prohibited"; sadx eb4cdcc 2026-04-08, sa2 611d686 2026-08-08 | Headers with **symbol-based** names for SADX PC (`SADXStructsNew.h`, `SADXEnumsNew.h`, `SADXFunctionsNew.h`, `SADXVariablesNew.h`), plus older community names. | The best naming source for SA1 player code (section 2.4). Already mined into `reference/sadx_pc_*.csv`. |
| SA2 mods (Shaddatic, Sora-yx) | github.com/shaddatic/sa2b-render-fix (no license file, 4110597 2026-10-05); Sora-yx/SA2-Sonic-New-Tricks (no license file) | | SA2 PC rendering and gameplay mods. | Shaddatic's SAMT headers are credited by sa2dc for `playerwk` member names. |

### 2.3 Clean-room recreations (no original code; "inspired by" physics)

| Project | URL | License, HEAD | Notes |
|---|---|---|---|
| SonKE (Sonic Kinematic Engine) | github.com/GlaireDaggers/com.glairedaggers.sonke | MIT; fbefabb 2021-05-22 | A Unity package of "Adventure-inspired" kinematic physics. It does not use `player_parameter` names or values. |
| HedgePhysics | github.com/dudemancart456/HedgePhysics | BSD-3; 4068951 2017-07-15 | Unity 3D Sonic engine (LakeFeperd and Damizean). |
| BadnikFramework | github.com/Gnidel/BadnikFramework | MIT-style `LICENCE.md`; ce54cf8 2026-10-01 | Godot 4 C#, boost and Adventure styles. It includes a `SonicAssets` folder of fan assets (the repository is 1.2 GB). |
| MoonCast | github.com/Moonlight-Team/MoonCast | MIT; 46d9aa5 2026-03-30 | Godot 4, 2D and 3D, physics state as a `Resource`. |
| ricky-daniel13/SonicAdventure | github.com/ricky-daniel13/SonicAdventure | no license file; b38d43c 2023-11-02 | Godot state machine, WIP. |
| SRB2 | github.com/STJr/SRB2 | GPL-2.0; 639b58c 2025-07-29 | A Doom Legacy-derived engine with Sonic momentum on sectors and slopes. SA-style Lua addons ("Adventure Sonic" v1.9 on mb.srb2.org: homing attack, light-speed dash) are clean-room. Useful for **feel and game-design references** only. |
| sm64-roblox | (section 1.3) | | Decomp-derived (Mario), but it shows how to port fixed-tick physics onto a host's raycasts. |

None of these has a "playerwk" or "player_parameter" reimplementation. The only public
reimplementations of SA physics that use the original layout are SonicAdventure64 (values only)
and the SA2 Rust controller (translated routines).
### 2.4 Documentation of SA physics, the player state machine and action lists

#### 2.4.1 `player_parameter` (physics) - DC table and a correction

The original field names, from the SADX `SADXStructsNew.h` and the SA Tools `PlayerParameter`
class, are 33 fields totalling `0x84` bytes, in this order:

`jump2_timer` (s32), then 32 floats: `pos_error`, `lim_h_spd`, `lim_v_spd`, `max_x_spd`,
`max_psh_spd`, `jmp_y_spd`, `nocon_speed`, `slide_speed`, `jog_speed`, `run_speed`, `rush_speed`,
`crash_speed`, `dash_speed`, `jmp_addit`, `run_accel`, `air_accel`, `slow_down`, `run_break`,
`air_break`, `air_resist_air`, `air_resist`, `air_resist_y`, `air_resist_z`, `grd_frict`,
`grd_frict_z`, `lim_frict`, `rat_bound`, `rad`, `height`, `weight`, `eyes_height`,
`center_height`.

We read the table directly from `disc/1ST_READ.BIN`. It is **8 contiguous `0x84` entries in
player-number order**:

| PL | Character | Address (US 1.005) | Check values (rad, height, weight, jmp_y_spd) |
|---|---|---|---|
| 0 | Sonic | `0x8C1BDFE4` | 4.0, 10.0, 0.08, 1.66 |
| 1 | Eggman | `0x8C1BE068` | 8.5, 18.0, 0.08, 1.66 |
| 2 | Tails | `0x8C1BE0EC` | 3.5, 9.0, 0.08, 1.66 |
| 3 | Knuckles | `0x8C1BE170` | 4.0, 11.4, 0.08, 1.66 |
| 4 | Tikal | `0x8C1BE1F4` | 4.0, 10.0, 0.08, 1.66 |
| 5 | Amy | `0x8C1BE278` | 3.5, 10.0, 0.04, 1.30 |
| 6 | Gamma (E-102) | `0x8C1BE2FC` | 4.0, 21.0, 0.10, 2.00 |
| 7 | Big | `0x8C1BE380` | 9.5, 17.0, 0.135, 2.00 (lim_h_spd 5.0) |

The table ends at `0x8C1BE404`; zeros follow.

Two functions in our build reference the table base: `func_8C06C350` (634 bytes, 8 callers) and
`func_8C075214` (514 bytes). These are likely the player-init copies into `playerwk.p`.

The SA Tools `GameConfig/DC_SA1/1st_read.ini` gives Eggman..Big at `1AE0EC + 0x108*k`. That is
wrong for every entry except Sonic. AdventureRip's `figure/big/Physics.ini` is garbage, and
`reference/dc_sa1_data_labels.csv` rows `Eggman_Physics_Data..Big_Physics_Data` are wrong. This
was **not fixed here**: the task allows writing this report only.

#### 2.4.2 Player work structures (SADX symbol-based names, `SADXStructsNew.h`)

* `taskwk`: mode, smode, id, btimer, flag, wtimer, counter, timer, value, `Angle3 ang`,
  `NJS_POINT3 pos`, `scl`, `colliwk *cwp`, `eventwk *ewp`.
* `motionwk` / `motionwk2`: spd, acc, ang_aim, ang_spd, rad, height, weight.
* `playerwk` contains, in order:
  * work union, equipment, item;
  * timers: jump, nocon, breath, wait;
  * flag, island_num, path_point, hpos, dotp;
  * vectors `eff`, `acc`, `spd`, `wall_normal`, `floor_normal`;
  * `attr`, `last_attr`;
  * `csts *cstsp` (collision status), held-task and target-task pointers, `PL_FACE *pfp`,
    `lclop`, `island_list`;
  * a 32-byte `free` union;
  * an embedded `player_parameter p`;
  * `mtnjvwk mj`;
  * hand, foot and user positions and vectors;
  * `shadowwk shadow`.
* `mtnjvwk`: mtnmode, jvmode, reqaction, action, lastaction, nextaction, acttimer, flag, nframe,
  start_frame, `spdp`, `workp`, `PL_ACTION *plactptr`, `PL_JOIN_VERTEX *pljvptr`,
  `NJS_ACTION *actwkptr`.
* **`PL_ACTION`** (action list entry, 16 bytes on DC): `NJS_ACTION *actptr; Uint8 objnum;
  Sint8 mtnmode; Uint16 next; Float frame; Float racio`.
* **`PL_JOIN_VERTEX`** (weld): objptr, srcobj, dstobj, numVertex, inpmode, srcdepth, dstdepth,
  `NJS_POINT3 *org`, `Uint16 *pnum`.
* `csts`: radius, pos, spd, tnorm, find_count, selected_nmb, yt, yb, angx, angz, normal,
  normal2, onpoly, pshbk, anaspdh, anaspdv.
* `shadowwk`: angx, angz, Attr_top/bottom, y_top/bottom, scl.

These are SADX PC (32-bit x86) layouts. The SA1 DC layout must be confirmed from our code, because
DX added fields in places.

Field offsets used by the SA2 DC decomp (`Exant64/sa2dc src/player.h`) differ, as expected for
SA2.

#### 2.4.3 State machines (`SADXEnumsNew.h`)

* Per-character mode enums: `MD_SONIC` runs 0x00..0x57 (88 values, including the Super Sonic
  modes `MD_SUPER_*` and `MD_SONIC_POSE/EVENT/DOZA/KILL/EDIT`). There are also `MD_MILES`,
  `MD_KNUCK`, `MD_AMY`, `MD_BIG` and `MD_E102`.
* **`PL_OP`** (0x00..0x32): the orders that objects send to a player:
  * `SPRING`, `CANON`, `PARABOLIC`, `TRAMPOLINE*`, `ASCEND`;
  * `PLACEON` and `PLACEWITH{SPIN,RUN,PUSH,HUNG,FALL,CART,KIME,ROCKET*,RAFT}`;
  * `LETITGO`, `SPINDASH`, `SEEPATH`, `EVENTMOTION`;
  * `HANGDOWNFROMPATH`, `HOLDONICICLE`, `LADDERINGPATH`, `HOLDONPILLAR`;
  * `CAPTURED`, `CRASH`, `BREATH`, `HOLDTASK`, `ICED`, `SNOWBOARDING`, `PINBALL`, `SUPER`,
    `NORMAL`, `FLY`, `PROP`, `KILLED`.

  **This is the natural API for GMod entities** (springs, dash panels, rails) to drive the
  character.

#### 2.4.4 Core routines (SADX PC names and addresses, from `SADXFunctionsNew.h`)

| Routine (SADX PC address) | Role |
|---|---|
| `PGetAcceleration` (0x44C270) | ground acceleration |
| `PGetAccelerationAir` (0x44B9C0) | air acceleration |
| `PGetAccelerationFly` / `PGetAccelerationGlik` | Tails flying, Knuckles gliding |
| `PGetAcceleration1D` | 1D paths |
| `PGetRotation` (0x44BB60) | rotation |
| `PSetPosition` (0x44CDF0) | position integration and collision |
| `PSetPosition1D` | 1D-path position |
| `PResetAngle` | reset angles |
| `PConvertVector_P2G` / `_G2P` | player<->global vector conversion |
| `PJoinVertexes` (0x43FA90) | applies welds |
| `PSetMotion` (0x44A800, takes `mtnjvwk*`) | sets the motion |
| `PSetCrashEffect` | crash effect |
| `MSetPosition`, `MSetPositionIgnoreAttribute`, `MSetPositionWIgnoreAttribute` (0x43A030 / 0x43A060 / 0x439620) | move a point with radius against geometry collision, with attribute masks |
| `GetShadowPos` (0x49E920) | ground height and angle below a point |
| `SonicTheHedgehog` (0x49A9B0) | Sonic's task exec |

DC counterparts are found through SARecomp (section 3.2) and `reference/sadx_pc_functions.csv`.

#### 2.4.5 Action lists and welds (DC, from the SA Tools ini; verified count x 16 = size)

| Character | Action list | Weld list |
|---|---|---|
| Sonic | `0x8C1C55A0`, 142 entries (0x8E0 bytes) | `0x8C1C5FA4` |
| Tails | `0x8C256D6C`, 132 entries | `0x8C2576EC` |
| Knuckles | `0x8C3528CC`, 111 entries | `0x8C353114` |
| Gamma | `0x8C4225D4` | `0x8C422BE8` |
| Amy | `0x8C4BB1D0` | `0x8C4BB8B4` |
| Big | `0x8C525564` | `0x8C525C90` |
| Eggman | `0x8C57008C` | `0x8C5702BC` |

Sonic's texlist is `texlist_SONIC` at `0x8C1C6C94` (PVM "SONIC", i.e. `SONICADV/SONIC.PRS`).

---
## 3. (c) Static recompilation and decompilation of Dreamcast and SA-family games

### 3.1 SARecomp and KatanaRecomp

* URL: github.com/sonicfreak1337/SARecomp; no license file; d5288fe 2026-09-24. The README says
  development is halted. `AGENTS.md` says development continues in a private repository.
* **KatanaRecomp (github.com/sonicfreak1337/KatanaRecomp) returns 404**: it is private. The
  public tree cannot be built. It needs the "pinned Katana runtime and retained compiled game
  partitions" (1,157 AOT partitions), which are not published.
* **Architecture** (from `docs/` and `src/`):
  * SH-4 code is AOT-translated to C++ partitions that operate on **16 MiB of guest RAM plus a
    register file**, so game data and pointers stay 32-bit guest addresses.
  * Hot paths are replaced by hand-written "native owners". Each is "authenticated", byte-checked
    against the retail code, then verified against the recompiled original with full CPU and RAM
    comparisons. Owners are switched with `SARECOMP_NATIVE_*` environment variables.
  * Original FPU semantics are kept: docs on FSCA/FTRV rounding "epochs", FIPR/FTRC flag
    behaviour, and FSRRA.
* **Graphics.** Ninja and Kamui entry points are replaced at function boundaries:
  * Examples include `sa_nj_draw_polygon`, `sa_nj_draw_texture`, `sa_nj_draw_sprite_2d/3d`, the
    glyph text routines, `sa_nj_texture_list_bind`, `sa_kamui_present_drain`,
    `sa_native_scene_begin` and `sa_native_frame_turnover`.
  * These feed a native renderer: D3D11, and Vulkan 1.3 with dynamic rendering.
  * It implements PVR-style **"Type-2" order-independent transparency** with a secondary
    accumulation arena (about 512 MiB), fog, flat/smooth shading and the PVR blend rules.
  * A presentation thread can repeat frames; widescreen is Hor+ with matching culling.
* **Audio.** The `sd`/MIDI sound API is replaced by a native "SoundBank" worker that renders MLT
  banks; there are notes on MIDI note-on, DSP tails and a "qsound reverb" program. ADX and movies
  go through FFmpeg.
* **Timing.**
  * The original game runs "scene-dependent cadence": a 50 Hz video clock on PAL, with a
    *slot count plus logical delta* contract of 1 or 2 units per update.
  * "Recompiled timing" targets 60 updates/s.
  * Interpolation was tried and withdrawn.
  * The gameplay wrapper is PAL `8C04E95A`, the same address in US.
* **Install flow** (`src/setup/`):
  * The user selects the `.gdi`.
  * Setup checks each track's number, LBA, type, sector size, byte size and SHA-256
    (`supported_disc.hpp`).
  * It reads ISO9660 from track 3 with 2352-byte raw sectors at LBA 45000 (Mode 1: data at +16,
    mode byte checked).
  * It extracts files into a private content directory.
  * `src/sonic-native-content-index.inc` holds about 2k entries of
    `{name, path, sha256, size, lba, sectors}` and **no payload bytes**.
  * There are also texture, sound and event-ADX catalogs with hashes.

  **This is exactly the pattern our ripper needs.** Ship a hash manifest for US 1.005, and read
  files by LBA on demand rather than unpacking 1.2 GB.
* **Reuse.** Its addresses (via section 3.2), its docs on hot paths, and its policy of no
  payload bytes in the repository. Its code is not reusable (no license; depends on private
  KatanaRecomp).

### 3.2 PAL v1.003 -> US v1.005 address mapping (new result)

SARecomp ships `src/sonic-adventure-pal-v1003-function-entries.inc` (4,815 PAL function starts).
Matching unique 5-gap signatures against our `build/1ST_READ.symbols.txt` (6,376 `func_` starts)
gave 1,603 anchors, which form exactly three runs:

| PAL range | US = PAL + |
|---|---|
| `0x8C010000 - 0x8C055794` | `+0x000` |
| `0x8C055F40 - 0x8C10B952` | `+0x0C0` |
| `0x8C10CDDC - 0x8C66BF54` | `+0x0C0` |

The 0xC0-byte difference is inserted between PAL `0x8C055794` and `0x8C055F40`. The script that
reproduces this is
`<scratch>\work\palmap.py`.

Named SARecomp routines mapped to US. Every mapped address is a function start in our build;
sizes come from `build/1ST_READ.xrefs.json`.

| SARecomp description | PAL | US 1.005 | US size |
|---|---|---|---|
| Player operation root A ("56-state dispatch") | `8C0CBD40` | `func_8C0CBE00` | 4764 B; called by `8C04F3C0`, `8C083F7C` |
| Player operation root B ("55-state dispatch") | `8C0CCFE8` | `func_8C0CD0A8` | 10694 B |
| Player display operation | `8C0CFA0E` | `func_8C0CFACE` | 2374 B |
| Common movement/contact owner (candidate selection, TOUCH query, contact response, position/orientation finalisation) | `8C073018` | `func_8C0730D8` | 4604 B |
| Collision-world transaction (landtable candidate list) | `8C028EC2` | `func_8C028EC2` | 1242 B |
| Collision eligibility selection | `8C052518` | `func_8C052518` | 902 B |
| Animation hierarchy (motion keys -> node matrices, one subtree) | `8C057B00` | `func_8C057BC0` | 1310 B |
| Model draw owners | `8C037098`, `8C037108` | same | 104 B, 116 B |
| njDrawPolygon | `8C63CD64` | `8C63CE24` | |
| Scene begin / frame turnover / interrupt dispatch | `8C641E40` / `8C604FF0` / `8C645ACE` | `8C641F00` / `8C6050B0` / `8C645B8E` | |
| Gameplay update wrapper | `8C04E95A` | `func_8C04E95A` | 240 B |

Other SARecomp docs that name more owners: `docs/native-*.md`, and the JSON owner lists in
`tools/*-owners.json` (player, movement-contact, object-contact, camera, land-render, actor).
Which character root A and root B belong to is not yet confirmed (see open questions).

### 3.3 Other Dreamcast static recompilers

| Project | URL | License, HEAD | Approach | Notes for us |
|---|---|---|---|---|
| dcrecomp | github.com/sp00nznet/dcrecomp | MIT; 90ee221 2026-08-26 | Python tools (`sh4_disasm.py`, `static_recompile.py`, `extract_gdi.py`). Emits C per function against a `sh4_cpu` struct, with emulated memory, MMU and store queues. Low-level PVR2 TA FIFO parser with an OpenGL 3.3 renderer (basic); AICA stubbed. Used by `sp00nznet/crazytaxi`. | FSCA is emitted as `sinf/cosf` (not bit-exact). |
| DreamcastRecompiled | github.com/Duw0ng/DreamcastRecompiled | MIT; ba7df34 2026-09-20 | C++ emission from an IR, run as a **hybrid**: PVR/TA software rendering plus D3D11, ARM7/AICA emulation, Maple, GD-ROM and CDI parsing, BIOS HLE. | General Dreamcast runtime ideas only. |
| dream-recomp | github.com/ez2torta/dream-recomp | GPL-2.0 (borrows from Flycast); 51a148c 2026-09-21 | One C++ function per guest function, with registers as locals. Hardware is emulated (16 MB RAM, VRAM in both 64- and 32-bit views, store queues; PVR display lists -> Vulkan; ARM7 runs the game's sound driver); BIOS and GD-ROM are HLE. A dev interpreter covers untranslated code. Crazy Taxi is playable. | Best-documented FP semantics (`docs/`, `runtime/include/dream/runtime/sh4/ops.h`):<br>- FPSCR `0x00040001` (round to zero, DN) maps to host `fesetround` and FTZ/DAZ.<br>- FMAC is treated as fused (`fmaf`).<br>- FIPR/FTRV accumulate in double and round once.<br>- **FSCA uses Flycast's hardware-captured 32,768-entry sine table**; computing `sin` live gave `0x3F7FFFFF` for sin 90 degrees, where hardware gives exactly 1.0.<br>The memory model doc covers areas, mirrors and the VRAM interleave. |
| KatanaRecomp | (private) | - | Powers SARecomp. | - |

### 3.4 Non-Dreamcast comparators (patterns for 32-bit guests on 64-bit hosts)

* **XenonRecomp** (github.com/hedge-dev/XenonRecomp, MIT, ddd128b 2025-08-04).
  * Every recompiled function receives the CPU context **and a base pointer**. Guest pointers stay
    32-bit offsets into one host allocation; loads and stores byte-swap.
  * Function overrides are done by weak-linking the generated function and aliasing an
    implementation.
  * "Mid-asm hooks" are declared in TOML.
  * **UnleashedRecomp** (GPL-3.0, cf829a9 2026-06-29) built on it, a Sonic Unleashed PC port,
    with the same "user supplies game files, installer verifies and extracts" model.
* **N64Recomp** (github.com/Mr-Wiseguy/N64Recomp, MIT, ffb39cd 2026-05-27) uses the same idea:
  an RDRAM base plus 32-bit addresses, and function replacement for mods.
### 3.5 Decompilations in the SA family

| Project | URL | License, HEAD | State | Learn or reuse |
|---|---|---|---|---|
| sa2dc | github.com/Exant64/sa2dc | no license file; ba180d9 2026-02-21 | SA2 DC matching decomp (SHC **5.1r13**, Katana r11). Mostly `INLINE_ASM(...)` per function: `player.c` 201, `sonic.c` 197, `c_colli.c` about 125. A few C files: `motiontable.c`, part of `prs.c`. | Same engine lineage: `CCL_*` collision-list API, task system, `playerwk`, motion tables. Its symbol names are in `reference/sa2dc_symbols.csv`. Very little player code is in C yet. |
| doldecomp/sadx | github.com/doldecomp/sadx | no license file; 4c2b833 2024-08-01 | GameCube SADX: builds DOLs, disassembles RELs. About 6 C files (SDK runtime). | Symbol maps for the GC builds. Already mined (`reference/other_versions_symbols.csv`). |
| roeming/SA2B | github.com/roeming/SA2B | CC0-1.0; 9334b0c 2026-10-02 | GameCube SA2B decomp, WIP. | Not read in depth. |
| FraGag sadx_decomp | github.com/FraGag/sadx_decomp | no license file (Sonic Retro disclaimer); ca40cd3 2013-12-01; archived | Only `CHRMODELS.DLL` data as generated C (SA Tools output). | It embeds SEGA model data in C. **The model we must not follow for distribution.** |
| FraGag sadtools | github.com/FraGag/sadtools | WTFPL; d0cee7c 2011-03-02 | Old SA DC toolset. | Historical. |
| SADX-cutscene-decompilation | (section 2.2) | | | |

### 3.6 SH-4 FPU usage in SA1 (scan of our asm)

| Item | Count / detail |
|---|---|
| `FMAC` | 3134 |
| `FIPR` | 184 |
| `FTRV` | 152 |
| `FRCHG` | 150 |
| `FSCHG` | 1276 |
| `FSQRT` | 38 |
| `FDIV` | 509 |
| `FSCA` (`0xFn FD`, n even), as raw `.DATA.W` | **119** halfwords in about 30 code units of 1ST_READ, in game code `8C044A20..8C09EB08` and Ninja `8C639..8C64F` units, plus 31 overlay files |
| `FSRRA` (`0xFn7D`), as raw `.DATA.W` | **70** halfwords |

FSCA and FSRRA appear only as raw `.DATA.W` because SHC 5.1's asmsh cannot encode them. A few
could be data in text, so treat the counts as upper bounds.

Consequences for a native PC build of the decompiled C:

1. SHC truncates decimal float literals (`-round=zero`, `docs/compiler.md`). MSVC and Clang round
   to nearest, so 0.1 becomes `0x3DCCCCCD`, not `0x3DCCCCCC`. Literals in decompiled C must be
   written bit-exactly, for example C99 hex floats or a `F32(0x3DCCCCCC)` macro, or the physics
   constants will differ in the last bit.
2. Runtime rounding is toward zero with denormals flushed (Katana FPSCR `0x00040001`). Set MXCSR
   RC=toward-zero and FTZ/DAZ for the duration of `tick()` and restore it afterwards, because
   GMod/Source expects nearest. Compile with strict FP: no contraction, no x87, single precision.
3. Where SHC emitted FMAC, decide whether to use `fmaf` (dream-recomp's choice) or a separate
   multiply and add. Hardware fidelity of FMAC, FIPR and FTRV is an open question.
4. FSCA should be a 64K table built from the captured coefficients. SA1 Ninja angles are 16-bit
   BAMS in the FPUL low word. FSRRA is an approximation, so decide on a tolerance.

### 3.7 32-bit pointer data on a 64-bit host: options

| Option | Who does it | Pros | Cons |
|---|---|---|---|
| A. **Guest RAM image** (load 1ST_READ at `0x8C010000` into a 16 MiB array; data pointers stay `u32` guest addresses; decompiled C reads through typed accessors) | SARecomp, dream-recomp, XenonRecomp, N64Recomp, SA2-Rust's `PlatformMemory` for callbacks | No relocation; exact layouts (`sizeof` matches DC); rip = `memcpy` of 1ST_READ; trivially correct for every table. | Decompiled C must use accessor or handle types instead of native pointers, which makes it less readable and diverges from the matching-decomp source. Host-created objects (GMod props) must be allocated inside the guest heap. |
| B. **Native structs plus a loader** (walk `NJS_OBJECT`, `NJS_MODEL`, `NJS_MESHSET`, `NJS_MATERIAL`, `NJS_MOTION`, `NJS_ACTION`, `PL_ACTION`, `PL_JOIN_VERTEX`, `player_parameter` and texlists from 1ST_READ; deep-copy into host structs with 64-bit pointers) | libsm64 (animations from ROM to native structs), SA Tools (C# object model) | Decompiled C stays idiomatic and compiles on both SHC (matching) and PC from **one source** with ordinary pointer types. | A loader per type. Any code doing offset arithmetic on structs, or storing pointers in `int`, needs care. Layouts differ (pointer size), so `free` unions and `work.ptr` must be wide enough. |
| C. Hybrid | - | Use B for the character and engine library, and A only in a **verification harness**: run original SH-4 code in an interpreter, as SA2-Rust and SARecomp test against original execution, and compare state per tick. | Two code paths to maintain. |

**Recommendation:** C. The decomp's C is already being written for matching. Keep it
pointer-clean so that the same sources build on x64 with real pointers. Load assets with option-B
loaders. Use an SH-4 interpreter (or Flycast in a headless harness) only for differential tests.

---

## 4. (d) Dreamcast asset formats: documentation and decoders

### 4.1 Disc access

* Our GDI has 3 tracks:
  * `track01.bin`: LBA 0, data, 2352-byte sectors;
  * `track02.raw`: LBA 14714, audio;
  * `track03.bin`: LBA 45000, data, 2352-byte sectors, 1,185,760,800 bytes, holding the whole
    high-density area.
* The ISO9660 filesystem is on track 3. Sector `s` maps to file offset `(s - 45000) * 2352 + 16`
  for its 2048 user bytes (Mode 1). The volume descriptor is at LBA 45016; the IP.BIN bootstrap
  is the first 0x8000 bytes of LBA 45000.
* `1ST_READ.BIN` is scrambled on CD-ROM releases but **not on GD-ROM**. Ours loads as-is; the md5
  matches SA Tools.
* Existing readers: our own `tools/extract_gdi.py`; dcrecomp's `extract_gdi.py` (MIT); SARecomp's
  ISO reader (no license); KallistiOS (BSD "KOS License") and Flycast (GPL-2.0) GD-ROM code.
* The ripper should read directory records and fetch files by LBA lazily. A web or wasm build
  would need random-access file reads anyway.

### 4.2 PRS (SEGA LZ77)

* Bitstream: control bits LSB-first, read lazily and interleaved with data. Three commands:
  * literal (`1`);
  * short copy (`00` + 2 bits, size 2..5, offset -256..-1 from one byte);
  * long copy (`01` + 16-bit word: 13-bit offset -8192..-1 and a 3-bit size; size 0 means "next
    byte + 1"). The word `0x0000` terminates the stream.
* Our `tools/prs.py` is the project's own decoder, plus a **byte-exact SEGA encoder**. Its
  docstring documents the format and the retail encoder policy.
* Decoders:
  * Sewer56/prs-rs (GPL-3.0, db0deb1 2026-09-15; fastest; C exports);
  * HybridEidolon/rust-ages-prs (Apache-2.0/MIT, e62923b 2020);
  * FraGag/prs.net (used by SA Tools);
  * puyotools `PrsCompression.cs` (MIT);
  * essen/prsutil (fuzziqer's original).
* **Recommendation:** port our own `prs.py` decoder to C, about 60 lines. No license issue.
### 4.3 PVM / PVR textures

* **PVM** (`PVMH`): u32 chunk size; u16 flags; u16 count; then per-entry records (id, then name,
  format, dims and GBIX as the flags select). Each texture follows as an optional `GBIX`
  (global index) chunk and a `PVRT` chunk.
* **PVRT** header: u32 length; u8 pixel format; u8 data format; u16 pad; u16 width; u16 height;
  then data.
* Pixel formats: 0 ARGB1555, 1 RGB565, 2 ARGB4444, 3 YUV422, 4 bump, 5 RGB555, 6 ARGB8888/YUV420.
* Data formats: 1 square twiddled, 2 + mipmaps, 3 VQ, 4 VQ + mipmaps, 5/6 4-bpp palettised (+- mips),
  7/8 8-bpp palettised (+- mips), 9 rectangle, 0xB stride, 0xD rectangular twiddled, 0x10 small VQ,
  0x11 small VQ + mipmaps, 0x12 alternative twiddled mipmaps (SA2).
* Twiddling is Morton (bit-interleaved x/y) order. Rectangular twiddled textures are tiled as
  squares of the smaller side.
* **VQ:** a codebook of 2x2-pixel 16-bit entries (256 entries, 2048 bytes) followed by one index
  byte per 2x2 block, in twiddled order. **Small VQ** codebook sizes depend on width:
  * without mipmaps: <=16 -> 16 entries, <=32 -> 32, <=64 -> 128, else 256;
  * with mipmaps: <=16 -> 16, <=32 -> 64, else 256.
* Mipmaps are stored smallest-first, with format-specific padding before the 1x1 level.
* **SA1 US disc census.** Our scan script is
  `<scratch>\work\pvrstats.py`.
  * There are 588 PRS-compressed PVM archives holding 16,725 textures, plus 441 standalone `.PVR`
    files.
  * Formats used, by count:

    | Pixel format | Data format | Count |
    |---|---|---|
    | RGB565 | twiddled + mips | 5573 |
    | RGB565 | VQ + mips | 5164 |
    | ARGB4444 | rectangle | 1315 |
    | RGB565 | twiddled | 1109 |
    | ARGB4444 | twiddled + mips | 907 |
    | ARGB4444 | twiddled | 585 |
    | ARGB1555 | rectangle | 429 |
    | RGB565 | small VQ + mips | 408 |
    | ARGB1555 | VQ | 384 |
    | ARGB1555 | twiddled | 336 |
    | ARGB4444 | VQ + mips | 335 |
    | Others | small VQ variants and RGB565 rectangle | - |

  * **No palettised, YUV, bump or stride textures.** The decoder needs 3 pixel formats x {twiddled,
    twiddled + mips, VQ, VQ + mips, small VQ, small VQ + mips, rectangle}.
  * The most common sizes are 64^2, 32^2 and 128^2.
  * Character PVMs (all PRS -> `PVMH`): SONIC 28 textures, MILES 24, KNUCKLES 22, AMY 53, E102 35,
    BIG 36, SUPERSONIC 73, SON_EFF 29.
* Decoders and docs:
  * nickworonekin/puyotools (MIT, 62118d2 2026-03-06): `PuyoTools.Core/Textures/Pvr/*` covers
    every codec, PVM/AFS archives and PRS.
  * SA Tools `Libraries/TextureLib`.
  * tvspelsfreak/texconv (encoder, twiddle and VQ; no license file).
  * ianmicheal/PVRVQ-PORT-KATANA (Sega/Imagination pvrtool port; no license file).
  * KallistiOS `pvr.h` docs (BSD).
  * Flycast `TexCache` (GPL-2.0).
  * PyPVR (a Dreamcast-Talk tool).

### 4.4 Ninja "basic" models (SA1 DC; SA Tools `Libraries/SAModel`)

* `NJS_OBJECT` (0x34): `evalflags` u32; `NJS_MODEL*`; `pos[3]` f32; `ang[3]` s32 BAMS
  (0x10000 = 360 degrees); `scl[3]` f32; `child*`; `sibling*`.
  * Evalflags bits: 1 no translate, 2 no rotate, 4 no scale, 8 no draw, 0x10 no children,
    0x20 ZYX rotation order, 0x40 no animate, 0x80 no morph.
* `NJS_MODEL` (basic, 0x28): `points*`; `normals*`; `nbPoint`; `NJS_MESHSET*`;
  `NJS_MATERIAL*`; u16 `nbMeshset`; u16 `nbMat`; `center[3]`; `r`.
* `NJS_MESHSET` is **0x18 on SA1 DC**. SADX PC adds a 4-byte buffer pointer, making it 0x1C;
  SA Tools' `Size(DX)`.
  * Fields: u16 `type_matId`, where the top 2 bits are the polytype (0 triangles, 1 quads,
    2 N-poly, 3 strips) and the low 14 bits the material; u16 `nbMesh`; `s16* meshes`; `attrs*`;
    `normals*`; `vertcolor*`; `vertuv*`.
  * For strips, each strip starts with an s16 count. **The sign bit is a winding flip**, and
    per-strip-vertex normals, colours and uvs follow the index order.
  * UVs are s16 / 255. SA Tools divides by 255.0, or by 256 for some variants, and by 1023 for
    "UVH".
* `NJS_MATERIAL` (0x14): diffuse ARGB; specular ARGB; `exponent`; `attr_texId`.
  * Low 14 bits are the texture id.
  * The other bits are `NJD_FLAG_*`: ignore light, use alpha, double side, flat, env, use texture,
    clamp and flip U/V, filter, and super-sample/d-adjust bits 8..11.
  * The source alpha is in bits 29..31 and the destination alpha in bits 26..28.
* Docs and decoders:
  * SA Tools `SAModel` (`NJS_OBJECT.cs`, `BasicAttach.cs`, `NJS_MESHSET.cs`, `NJS_MATERIAL.cs`,
    `Enums.cs`).
  * **X-Hax/SA3D.Modeling** (GPL-3.0, 5fe1873 2026-08-05; a clean, documented rewrite).
  * cmargroff/njaPatcher (GPL-3.0; NJ/NJA chunk models).
  * Radfordhound's "Sega NN" gist (the later NN format, a descendant of Ninja chunk).

  SA1 DC characters are **basic** models, not chunk models.

### 4.5 Landtables and geometry collision (SA1)

* `LandTable` (SA1 / SADX, 0x24):
  * s16 `COLCount`; s16 `AnimCount`; s16 attributes; s16 flags; f32 far clip;
  * `COL*` at +0x0C; `GeoAnimData*` at +0x10; texture-name `char*` at +0x14; `NJS_TEXLIST*` at
    +0x18; two more words.
* `COL` entry (0x24): bounding sphere center (3 f32) and radius; two unknown words; `NJS_OBJECT*`;
  a word; **`SurfaceFlags`** (SA1 bitfield: solid, water, no-friction, no-acceleration,
  increased acceleration, diggable, no-climb, hurt, footprints, visible, ...).
* `reference/dc_sa1_data_labels.csv` lists 85 landtables with their COL and anim lists. SA Tools'
  `LandTable.cs`/`COL.cs` read them.
* For GMod we import host brushes and props *as* COL entries (section 1.4). The ripper needs
  landtables only if the port wants to load SA1 stages into GMod as a bonus. That is useful for
  testing physics on original geometry, as SA2-Rust tests on its "source terrain".

### 4.6 Motions, actions, action lists and welds

* `NJS_ACTION` = { `NJS_OBJECT* object`; `NJS_MOTION* motion` }.
* `NJS_MOTION` (0xC): `mdata*`; s32 `nbFrame`; u16 `type` (flags: 1 pos, 2 rot, 4 scale,
  8 vector, 0x10 vertex, 0x20 normal, 0x40 target, 0x80 roll, 0x100 angle, 0x200 colour,
  0x400 intensity, 0x800 spot, 0x1000 point, 0x2000 quaternion, ...); u16 `inp_fn`, which holds
  the element count in the low bits and the interpolation (linear, spline, user) in the high bits.
* `mdata` is an array with one entry per node, in depth-first `NJS_OBJECT` order. Each entry has
  one pointer per enabled element type, then one key count per type. Pos/rot gives 16 bytes, and
  vertex morphs add more.
* Key types:
  * float vector keys: `{u32 frame; f32 x, y, z}`;
  * angle keys: `{u32 frame; s32 BAMS x 3}`. Some SA2 data uses s16 "short rotation"; SA Tools
    exposes `ShortRot`.
  * Sonic's morphs are vertex/normal keys.
* SA Tools `Animation.cs` reads all of these.
* Evaluation: SARecomp's native animation hierarchy note (US `func_8C057BC0`) records some
  quirks:
  * the "visit budget" counter;
  * the channel count is the mask bit length, not the popcount;
  * two- and three-channel authoring.
* **Action lists** (`PL_ACTION[]`, 16 bytes each) map an action number to an `NJS_ACTION`, the
  part count, the motion mode (loop, once, ...), the next action, a frame speed and a blend ratio.
* **Weld lists** (`PL_JOIN_VERTEX[]`) copy posed vertices from source to destination objects
  after posing.
* Both are covered by section 2.4.5 and can be read directly from 1ST_READ.
### 4.7 ADX and AFS

* **ADX** (CRI): big-endian header with `0x8000` magic, a copyright offset and `"(c)CRI"`; the
  encoding type (3 = fixed coefficients); block size 18; 4-bit ADPCM, 32 samples per 18-byte
  frame per channel; prediction coefficients derived from a high-pass cutoff; optional loop
  points.
  * SA1 uses ADX for music: 109 `.ADX` files, for example `SONIC.ADX`, `THESONIC.ADX` and the
    `ADV*` jingles.
  * Decoders: vgmstream (ISC-style, 7dc938f 2026-09-28; `src/meta/adx.c`,
    `src/coding/adx_decoder.c`; it also handles AICA/Yamaha ADPCM) and Thealexbarney/VGAudio (MIT,
    C#; ADX encode and decode).
* **AFS**: `"AFS\0"`; u32 count; count x `{u32 offset, u32 size}`; then optionally
  `{offset, size}` of a name table (0x30-byte records: 32-byte name plus date fields and size).
  Files are 0x800-aligned.
  * `EVENT_ADX.AFS` has 0x7FC (2044) entries; `EVENT_ADX_US.AFS` holds the English voices.
  * Decoders: puyotools `AfsArchive` (MIT).
* The library's character sounds do not need either. Music is optional ("play `SONIC.ADX` while
  possessed"); about 100 lines of C would decode it.

### 4.8 MLT sound banks and the Manatee driver

* **MLT** (`SMLT`, version 1.01): u32 unit count, padding, then 0x20-byte unit records:
  * a 4CC (`SMSB` MIDI sequence bank, `SMPB` program/instrument bank, `SMDB` drum bank, `SOSB`
    one-shot bank, `SFPB` DSP program, `SFOB` DSP output/mixer, `SFPW` DSP work, `SPSR` PCM stream
    ring);
  * the bank number;
  * the AICA RAM address;
  * the reserved size;
  * the file offset (`0xFFFFFFFF` = reserve only);
  * the file size.
* SA1 has 123 MLT files. The ones the port needs:
  * player SFX: `P_SONICTAILS.MLT`, `P_KNUCKLES`, `P_AMY`, `P_E102`, `P_BIG`;
  * voices: `V_SONICTAILS(_E).MLT` and so on;
  * stage banks such as `EMERALDCOAST.MLT`;
  * event banks `E_00xx.MLT`.

  `P_SONICTAILS.MLT` has 21 units. SMPB/SMSB banks 0..2 are reserve-only; SMPB bank 3 sits at
  file offset 0x2C0, size 0x22040.
* **SFX are MIDI-driven.** The game asks the `sd` library to play a sequence or note in a bank.
  Faithful playback needs a Manatee-like voice engine: ADSR, pitch, AICA ADPCM and PCM samples,
  pan and DSP. Alternatively, pre-render each SE with a small player.
* Docs and decoders:
  * dakrk/manatools (GPL-3.0, d61b1e3 2025-08-17): MLT, MPB/MDB, OSB, FOB and MSB tools, with
    010-Editor `.hexpat` specs in `docs/`.
  * **iamsh4/dc-manatee** (no license file, 150e85c 2026-09-25): a complete logical C
    reconstruction of the Manatee ARM7 driver (181 functions: voices, sequencer, host mailbox,
    FX), an annotated Ghidra project and an explainer site (iamsh4.github.io/dc-manatee). Its
    reference is Manatee 1.1 build 0x3F from Soul Calibur's `CINIT.DAT`. SA1 ships `MANATEE.DRV`
    (disc root) and `SONICADV/MASARU.DRV`, which may be a different build.
  * itloziusz/BankSmith and mlt-soundbank-explorer (Python; not cloned).
  * SARecomp implements a native SoundBank/MIDI engine (no license).
  * vgmstream decodes Yamaha AICA ADPCM.
* **Recommendation:** use dc-manatee's documentation (not its code; no license) and the manatools
  hexpats to write a small MIT-licensed SE renderer covering one-shot and sequence playback. As a
  fallback, play each SE's first sample.

### 4.9 Format / decoder / license matrix

| Format | Best documentation | Decoder to consult | License of that decoder |
|---|---|---|---|
| GDI / ISO9660 | Our `tools/extract_gdi.py`; SARecomp `src/setup/install_engine.cpp` | dcrecomp `tools/extract_gdi.py` | MIT |
| PRS | Our `tools/prs.py` docstring | prs.py (ours); rust-ages-prs | own; Apache-2.0/MIT |
| PVM/PVR | puyotools `Textures/Pvr` | puyotools | MIT |
| Ninja basic model / landtable / motion | SA Tools `Libraries/SAModel` | SA Tools (C#) | no license file (`Licenses/` holds third-party only) |
| Same, cleaner | X-Hax/SA3D.Modeling | - | GPL-3.0 |
| ADX | vgmstream `adx_decoder.c`; VGAudio | VGAudio | MIT |
| AFS | puyotools `AfsReader` | puyotools | MIT |
| MLT/MPB/MSB/OSB | manatools `docs/*.hexpat`; dc-manatee `docs/notes` | manatools | GPL-3.0 (read only) |
| Manatee playback | dc-manatee | - | no license file (read only) |

---

## 5. Licensing guidance for the port

* **Distributable binary (GMod module plus our library).** Link or adapt only CC0, MIT, BSD,
  zlib, Apache-2.0 or ISC code: libsm64 ideas (CC0), puyotools (MIT), VGAudio (MIT), dcrecomp (MIT),
  KallistiOS (BSD), SDL (zlib), Facepunch `gmod-module-base` (header-only; no license file, but
  universally used), danielga `garrysmod_common`.
* **GPL** (prs-rs, manatools, SA3D.Modeling, libsm64-sharp, gzdoom-sm64: GPL-3.0; Flycast,
  dream-recomp, SRB2: GPL-2.0). Read them, but do not copy, unless the whole module goes GPL.
  GPL-3.0 and GMod's closed engine are compatible in practice (it is a plugin), but GPL-2.0-only
  code cannot be combined with GPL-3.0 code.
* **No license or "educational" disclaimers** (SARecomp, G64 and its fork, SA mod-loader headers,
  SA Tools, sa2dc, dc-manatee, sadx-multiplayer, SonicAdventure64). Use them as references only:
  names, offsets, behaviour.

  Our own decompiled SA1 C is SEGA-derived code in any case, the same legal position as libsm64.
  The binary must contain **no SEGA data**: no models, motions, tables or textures. Contrast
  libsm64, which compiles Mario's geometry in, and SA2-Rust, which compiles imported tables in.
* All projects that do this well state "no game data in the repository or releases". Users supply
  the disc, and the software checks hashes. We should do the same, with a manifest of SHA-256
  values for the US 1.005 files, as SARecomp does for PAL.

---
## 6. Requirements derived for the SA1 port

1. **Input.** Accept a `.gdi` path or an extracted directory. Verify:
   * the track layout (3 tracks; track 3 at LBA 45000, 2352-byte sectors, 1,185,760,800 bytes);
   * the SHA-256 of `track03.bin`, or of each needed file;
   * `1ST_READ.BIN` md5 `060cad2ceefc07c7429085f30a356046`.

   Refuse other builds clearly (initially US 1.005 only; PAL 1.003 is mappable later via
   section 3.2).
2. **Lazy ISO9660 reader** over 2352-byte sectors. Fetch only `1ST_READ.BIN`, the character
   `*.PRS` PVMs, `P_*.MLT`, `V_*.MLT`, and optionally ADX and AFS.
3. **Rip at run time into native structs** (option B in section 3.7):
   * the 8 `player_parameter` entries at `0x8C1BDFE4 + 0x84*pl`, using the corrected addresses;
   * per-character `NJS_OBJECT` trees, action lists (`PL_ACTION[]`, for example Sonic
     `0x8C1C55A0` x 142), weld lists and `NJS_TEXLIST`s;
   * all motions those reference;
   * PVM -> RGBA8 textures, decoding 3 pixel formats x 7 data formats.

   Nothing from the disc is compiled into the binary.
4. **The decompiled engine subset** builds from the same C as the matching decomp. Requirements:
   pointer-clean code; bit-exact float literals; MXCSR round-to-zero and FTZ/DAZ during `tick`;
   an FSCA table; a defined FMAC policy.
5. **Tick at the original logical rate** (60 Hz units; the original's logical delta of 1 or 2 is
   open). Provide host interpolation helpers. Expose `tick(dt)` with an accumulator, as SA2-Rust's
   `Clock` does.
6. **Collision API.**
   * Static host triangles grouped as COL-like objects with SA1 `SurfaceFlags`.
   * Dynamic objects with a transform per tick.
   * Water as a surface flag or a water-level query callback.
   * Use the original candidate-selection and contact code:
     * collision world `func_8C028EC2`;
     * eligibility `func_8C052518`;
     * movement owner `func_8C0730D8`, including the `MSetPosition`/`GetShadowPos` equivalents.

   Units: scale GMod HU to SA units. Sonic's physics height is 10, which fits a GMod player of
   about 72 HU at roughly 6-7 HU per unit; this is tunable. Convert Z-up and the handedness.
7. **Object interaction API** modelled on `PL_OP` (springs, dash panels, cannons, launch). Also
   damage and knockback, ring count, invincibility, and homing targets (position, radius,
   attackable; as SA2-Rust's `World::target*`).
8. **Output.** Per tick: state (position, `Angle3`, speed, mode/smode, action, motion frame,
   flags, floor attributes); a CPU-skinned, welded, world-space triangle list grouped by
   material (texture id, blend flags, double-sided, alpha); and events (sounds, effects).
   Optionally, node matrices for GPU skinning.
9. **Pose-only API for remote players.** `{action/motion id, frame, root pos/ang, flags}` becomes
   the same mesh output, with no physics, so that G64-style replication works.
10. **Multi-instance.** Several characters per process, with no hidden `playertwp[0]`
    assumptions (the sadx-multiplayer lesson).
11. **Audio.** An optional in-module output thread (SDL or miniaudio). An SE renderer for MLT
    `P_*`/`V_*` banks, starting with a simple sampler and moving to a Manatee-like sequencer.
    An optional ADX music decoder.
12. **Packaging** in G64 style: a Lua addon on the Workshop, and the binary module via an
    installer (`gmcl_<name>_win64.dll`, plus `_win32` for the main branch).

## 7. Open questions

1. Which characters do SARecomp's two player roots belong to? US `func_8C0CBE00` (4764 B) and
   `func_8C0CD0A8` (10694 B) are described as "56-state" and "55-state" dispatchers. They are
   called from `8C04F3C0` and `8C083F7C`. Is root A Sonic's exec and root B its mode processor,
   or are they two characters?
2. Does SA1 DC run player physics once per 60 Hz logical frame, or with a logical delta of 2 at
   30 fps, in the stages we care about? SARecomp says the original has "one-unit and two-unit
   consumers". Which player routines read the delta?
3. FMAC, FIPR, FTRV and FSRRA fidelity: is real SH-4 FMAC fused? Are FIPR and FTRV
   reduced-precision on hardware? (Flycast and dream-recomp use double accumulation.) Do we need
   bit-exactness, or only "feels identical"?
4. The SA1 DC layouts of `playerwk`, `taskwk`, `motionwk` and `csts` against SADX PC: which
   fields did DX add or move?
5. Does SA1 DC's `MASARU.DRV` or `MANATEE.DRV` match dc-manatee's reference build (Manatee 1.1
   build 0x3F)? If not, how different is the SE sequence format?
6. How should GMod brushes and displacements be grouped into COL-like objects (per BSP leaf? per
   model?) so that the original eligibility selection stays efficient?
7. `reference/dc_sa1_data_labels.csv` (and the SA Tools DC ini) carries wrong physics addresses
   for PL 1..7 (section 2.4.1). Someone with write access to the reference set should regenerate
   it.
8. The unit scale between GMod HU and SA units, and whether to scale the character or the world.
   G64 exposes `SetScaleFactor`.

## 8. Sources (all accessed 2026-10-06)

libsm64 ecosystem:

| Repository | License | Commit | Read |
|---|---|---|---|
| github.com/libsm64/libsm64 | CC0 | fd11813 | read |
| github.com/ckosmic/g64 | none | 6de9c9e | read |
| github.com/ckosmic/libsm64 (gmod) | none | b7bfef1 | read |
| github.com/ckosmic/libsm64 (audio) | none | 3973a84 | |
| github.com/ckosmic/libsm64-gmod | none | 198dc63 | read |
| github.com/ckosmic/G64Installer | none | 75f722e | |
| github.com/libsm64/libsm64-unity | none | 21f6737 | read |
| github.com/Brawmario/libsm64-godot | MIT | 7d7be1e | read |
| github.com/libsm64/libsm64-blender | none | 03dcdd9 | read |
| github.com/Zckyy/mario64-in-minecraft | MIT | 681c685 | read |
| github.com/Retro64Mod/Retro64Mod | MIT | b1fab2c | |
| github.com/Retro64Mod/libsm64-retro64 | none | 7afa8ed | |
| github.com/headshot2017/Classic64 | none | 0f3795f | read |
| github.com/headshot2017/sm64-san-andreas | none | 1c0ef9a | read |
| github.com/headshot2017/ddnet-sm64 | DDNet license | 3cea8ff | |
| github.com/headshot2017/gzdoom-sm64 | GPL-3.0 | cc6847f | |
| github.com/headshot2017/OpenLara | BSD-2 | b6dfa3d | |
| github.com/Serialbocks/SupersonicMarioPlugin | MIT | e2810d8 | read |
| github.com/MaximumADHD/sm64-roblox | none | 79d5372 | read |
| github.com/LunaRyuko/SMUE5 | none | c1678c0 | |
| github.com/nickmass/libsm64-rust | MIT | f46ed0a | |
| github.com/MeltyPlayer/libsm64-ext | CC0 | 685da11 | |
| github.com/MeltyPlayer/libsm64-sharp | GPL-3.0 | 1cbfd9f | |
| github.com/warrenm/AugmentedMario | none | 0fef308 | |
| github.com/Llennpie/Venus | none | 3f731ef | |
| github.com/SuperSonicHub1/awesome-libsm64 | CC0 | fb74383 | |

Sonic / SA:

| Repository | License | Commit | Read |
|---|---|---|---|
| github.com/darkpbiMods/sonic-adventure-2-rust | MIT | d87dfff | read |
| github.com/sonicfreak1337/SARecomp | none | d5288fe | read |
| github.com/kellsnc/sadx-multiplayer | none | 0b6525a | read |
| github.com/X-Hax/sadx-mod-loader | disclaimer | eb4cdcc | read |
| github.com/X-Hax/sa2-mod-loader | disclaimer | 611d686 | |
| github.com/X-Hax/sa_tools | none | 5d52a39 | read |
| github.com/X-Hax/SADX-cutscene-decompilation | none | 9ee24f5 | |
| github.com/cuckydev/SonicAdventure64 | none | faa4542 | read |
| github.com/Sewer56/Adventure.Physics.Swap.ReloadedII | none | 0081a1d | read |
| github.com/GlaireDaggers/com.glairedaggers.sonke | MIT | fbefabb | |
| github.com/dudemancart456/HedgePhysics | BSD-3 | 4068951 | |
| github.com/Gnidel/BadnikFramework | MIT-style | ce54cf8 | |
| github.com/Moonlight-Team/MoonCast | MIT | 46d9aa5 | |
| github.com/ricky-daniel13/SonicAdventure | none | b38d43c | |
| github.com/STJr/SRB2 | GPL-2.0 | 639b58c | |
| github.com/Sora-yx/SA2-Sonic-New-Tricks | none | eac7981 | |
| github.com/shaddatic/sa2b-render-fix | none | 4110597 | |
| github.com/X-Hax/SA-Mod-Manager | MIT | 5bc4f40 | |

SRB2 Adventure Sonic: mb.srb2.org/addons/adventure-sonic-v1-9.3589/. Steam Workshop items 352460642,
2921224410, 264373973 and 328646709 were seen through search snippets only.

Recompilation and decompilation:

| Repository | License | Commit | Read |
|---|---|---|---|
| github.com/sp00nznet/dcrecomp | MIT | 90ee221 | read |
| github.com/sp00nznet/crazytaxi | MIT | 2d1484a | |
| github.com/Duw0ng/DreamcastRecompiled | MIT | ba7df34 | read |
| github.com/ez2torta/dream-recomp | GPL-2.0 | 51a148c | read |
| github.com/Exant64/sa2dc | none | ba180d9 | read |
| github.com/doldecomp/sadx | none | 4c2b833 | read |
| github.com/roeming/SA2B | CC0 | 9334b0c | |
| github.com/FraGag/sadx_decomp | none | ca40cd3 | read |
| github.com/FraGag/sadtools | WTFPL | d0cee7c | |
| github.com/hedge-dev/XenonRecomp | MIT | ddd128b | README read |
| github.com/hedge-dev/UnleashedRecomp | GPL-3.0 | cf829a9 | |
| github.com/Mr-Wiseguy/N64Recomp | MIT | ffb39cd | |
| github.com/flyinghead/flycast | GPL-2.0 | 59ed35a | |
| github.com/KallistiOS/KallistiOS | KOS/BSD | d458073 | |

KatanaRecomp (github.com/sonicfreak1337/KatanaRecomp) and recompcore/AdventureRecomp returned 404.

Formats:

| Repository | License | Commit | Read |
|---|---|---|---|
| github.com/nickworonekin/puyotools | MIT | 62118d2 | read |
| github.com/Sewer56/prs-rs | GPL-3.0 | db0deb1 | |
| github.com/HybridEidolon/rust-ages-prs | Apache-2.0 | e62923b | |
| github.com/tvspelsfreak/texconv | none | dd40d25 | |
| github.com/ianmicheal/PVRVQ-PORT-KATANA | none | 05e8c0a | |
| github.com/X-Hax/SA3D.Modeling | GPL-3.0 | 5fe1873 | |
| github.com/cmargroff/njaPatcher | GPL-3.0 | e962983 | |
| github.com/dakrk/manatools | GPL-3.0 | d61b1e3 | read |
| github.com/iamsh4/dc-manatee | none | 150e85c | read |
| github.com/vgmstream/vgmstream | ISC-style | 7dc938f | |
| github.com/Thealexbarney/VGAudio | MIT | 9d8f6ea | |

Also: gist.github.com/Radfordhound/5650ef12209da2709a6bf9b705bdd79c (Sega NN format) and
iamsh4.github.io/dc-manatee.

GMod module scaffolding (cloned by a sibling task): github.com/Facepunch/gmod-module-base (no
license file) and github.com/danielga/garrysmod_common.