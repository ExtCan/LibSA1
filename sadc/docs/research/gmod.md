# Garry's Mod technical requirements for an SA1 native-module character port

Research note, October 2026. Scope: everything on the Garry's Mod (GMod) side that a
"G64-style" port of the decompiled Sonic Adventure (DC, US v1.005) engine, physics and
playable characters needs: binary modules, world geometry, rendering, player integration,
networking, audio, units. It ends with a concrete module + addon architecture.

Companion note: `docs/research/g64.md` (a deep read of G64 itself). This file repeats only the
G64 facts that matter for GMod-side design decisions.

All third-party code below is described in prose only. No code was copied.

---

## 0. Summary (decisions in one screen)

1. **Ship a client module `gmcl_sa1_<platform>.dll`** in `garrysmod/lua/bin/`, plus a Lua-only
   Workshop addon. Binary modules cannot be loaded from addons or the Workshop. The module is a
   thin C++ Lua binding (Facepunch `gmod-module-base`, development branch) over a pure-C
   `libsa1` built from the decomp. Link everything statically into that one DLL (no extra DLLs in
   `bin/`). Build `win64` first, then `win32` and `linux64`. Since the **September 2026 update**,
   64-bit Windows is available on the public branch through Steam launch options, so `BRANCH`
   no longer tells you the bitness. Use `jit.arch` and `util.IsBinaryModuleInstalled` instead.
2. **Assets are read at runtime from the user's own disc**: the `.gdi` plus its tracks, or an
   extracted folder. Check `1ST_READ.BIN` against SHA-1 `e74c849806db103aa87f84e41a382873bc96b6df`
   (US v1.005, the same file the decomp builds against). The only things allowed on disk are
   derived caches in the user's own `garrysmod/data/sa1/`. Ripped data is never sent over the
   network: every client rips from its own disc.
3. **Simulation is client-authoritative, as in G64.** The owning client runs the SA1 player code
   at a fixed 60 Hz in its own Think loop and sends a compact state packet (about 40 bytes) at
   30 Hz. The server moves the real player entity there and relays the packet to other clients.
   Other clients evaluate animation only ("puppet" mode). The server needs no module and no disc.
4. **World collision is built in C++ from the map's BSP**, read through GMod's filesystem
   interface so mounted/Workshop maps work. It uses brush solids (including playerclip),
   displacements and static-prop `.phy` data, stored in one BVH. Collision is cached per map
   in `data/sa1/cache/`. Dynamic entities (props, brush movers, NPCs) are pushed from Lua each
   tick. Server streaming of world physics convexes (the G64 method) is kept only as a fallback.
5. **Rendering uses Lua hardware skinning.** It calls `Mesh(mat, 2)` with `mesh.BoneData` and
   `IMesh:DrawSkinned(bones)`, which takes at most 52 matrices. These were added on 2026-02-25
   and shipped in the April 2026 update. SA1 meshes are rigid per node and carry welds, so each
   vertex has one or two bones. Sonic has 73 nodes, but only 28 of them carry meshes. The static
   mesh is built once; each frame only the bone matrices change. The fallback is CPU skinning
   into a dynamic mesh. Textures are decoded from PVR, written as PNG to `data/`, and loaded with
   `Material("data/...png", "vertexlitgeneric mips smooth")`. Lighting comes from
   `render.ComputeLighting`, `render.SetModelLighting` and `render.RenderFlashlights`.
6. **Audio**: sound effects and voices (from MLT banks, AICA ADPCM) are decoded in the module and
   registered with `sound.Generate`, which takes 16-bit PCM in a string. They are then played
   with `Entity:EmitSound`, which gives Source 3D, DSP and volume. Music (ADX, stereo 44.1 kHz) is
   decoded to WAV in `data/` and played with `sound.PlayFile`, which also handles looping.
7. **Scale**: 1 SA1 unit = **4.0 HU** by default, set by a replicated convar. Sonic's collision
   cylinder (radius 4, height 10) becomes radius 16 HU and height 40 HU. The radius equals the
   GMod player hull half-width, and Sonic ends up about 1.0 m tall next to a 1.83 m (72 HU)
   human. Axis map with no mirroring: `src = S * (sa1.x, -sa1.z, sa1.y)`.

---

## 1. Sources read

| Project | Commit | What it is | License |
|---|---|---|---|
| ckosmic/g64 | 6de9c9e (2023-03) | G64 Lua addon (Workshop part) | **no LICENSE file** (all rights reserved). Its bundled `g64_luabsp.lua` comes from h3xcat (GPL-3.0) |
| ckosmic/libsm64-gmod | 198dc63 (2022-09) | `gmcl_g64_win64.dll` source (MSVC v143) | no LICENSE file |
| ckosmic/libsm64 (branch `gmod`) | b7bfef1 | libsm64 fork used by G64 (adds anim-only "fake" Mario, textures, audio) | no LICENSE file in fork |
| ckosmic/G64Installer | 75f722e (2024-12) | C# (Avalonia) installer | no LICENSE file |
| ckosmic/g64_autoupdater | b41d77e | `gmcl_g64_updater_win64.dll` (libcurl, jsoncpp, libzip) | no LICENSE file |
| libsm64/libsm64 | fd11813 (2026-02) | upstream libsm64 | CC0-1.0 |
| Retro64Mod/libsm64-retro64 | 7afa8ed | fork with audio, used by G64 | no LICENSE file |
| Facepunch/gmod-module-base | master 4fdafe7, development e0156e5 | official module headers + CMake helper | no LICENSE file |
| danielga/garrysmod_common | f77a18d (2026-06) | premake framework, Source SDK minimal, internal Lua interfaces | BSD-3-Clause |
| meetric1/gwater2 | fb4fba3 (2026-09) | large modern client module (FleX water), C++ IMesh rendering, C++ BSP parsing | GPL-3.0 |
| h3xcat/gmod-luabsp | 632a181 | Lua BSP parser | GPL-3.0 |
| Derpius/BSPParser (now "source-parsers", TASBox) | 0bce23e (2025-12) | C++ parsers: BSP, MDL/VVD/VTX, PHY, VTF, VPK, VDF | MIT |
| Derpius/GMFS | HEAD | tiny wrapper to reach GMod's `IBaseFileSystem` from a module | no LICENSE file |
| ThreadPool (Progsch/Zeman), vendored in gwater2 | | header-only thread pool | zlib |

Repos are cloned (depth 1) in the session scratchpad under `scratchpad/repos`.

Wiki pages were fetched raw from `https://wiki.facepunch.com/gmod/<Page>?format=text`, October
2026. They include: Creating Binary Modules (CMake/Premake), require, util.IsBinaryModuleInstalled,
BRANCH, Global Variables, Dev Branch, Mesh, mesh.Begin, mesh.BoneData, IMesh:DrawSkinned,
IMesh:BuildFromTriangles, Structures/MeshVertex, util.GetModelMeshes, PhysObj:GetMesh(Convexes),
Entity:GetBrushSurfaces, SurfaceInfo*, Material, Material Parameters, CreateMaterial,
GetRenderTarget, render.* (Capture, CapturePixels, ReadPixel, PushRenderTarget,
SuppressEngineLighting, ComputeLighting, SetModelLighting, SetLocalModelLights,
ResetModelLighting, SetLightingOrigin, RenderFlashlights, SetColorModulation), ENTITY:GetRenderMesh,
ENTITY:RenderOverride, GM:SetupMove/Move/FinishMove/StartCommand/CreateMove/CalcView,
Game Movement, Prediction, Entity Driving, net.Start, Net Library Usage, util.Compress,
input.GetAnalogValue, Enums/ANALOG/JOYSTICK, GM:PlayerButtonDown, GM:EntityTakeDamage,
sound.Generate, sound.PlayFile, sound.PlayURL, CreateSound, file.Write, game.MountGMA,
Workshop Addon Creation, Entity:NetworkVar, Entity:SetPredictable, ClientsideModel, and others.

GMod release notes used: April 2026 update (mesh skinning, module fixes) and September 2026
update (64-bit Windows on the public branch), both at `gmod.facepunch.com/news/...`.

---

## 1A. Requirements for the SA1 port in brief

These are the GMod-side requirements the SA1 port has to meet. Sections 2 to 8 give the details.

### Module, packaging and safety
- Build a client module named `gmcl_sa1_<suffix>.dll` and install it in `garrysmod/lua/bin`. The
  Workshop addon contains Lua only.
- The suffix is `win64`, `win32`, `linux64`, `linux` or `osx64`. The file extension is `.dll` on
  every platform.
- Detect which module to load from `jit.arch`, not `BRANCH`. Check
  `util.IsBinaryModuleInstalled("sa1")` before calling `require`.
- Make the module a single self-contained DLL with a static CRT. Do not write to `bin/` and do
  not ship an auto-updater that runs downloaded code.
- The Lua binding uses only `ILuaBase` (gmod-module-base, development branch), so it does not
  depend on the Source SDK ABI. If you use the CMake helper, override its hard-coded `gmsv_`
  prefix.
- No `ILuaBase` call may come from a worker thread. Results go to the main thread through queues
  that a Think hook polls.
- Assume the Lua state is torn down on map change: create all state in `gmod13_open` and free it
  in `gmod13_close`.
- An `ArgError` or `ThrowError` longjmps past C++ destructors. Validate arguments first.
- Make disc parsing defensive. Check the SHA-1 of `1ST_READ.BIN`
  (`e74c849806db103aa87f84e41a382873bc96b6df`) before using any address tables.
- Never send ripped data over the network. Every client rips from its own disc. Caches stay in
  the user's `garrysmod/data/sa1/`.

### World collision
- The collision provider must offer:
  - static triangles with attributes in a BVH;
  - dynamic triangle objects with per-tick transforms and velocity, so the player can ride them;
  - cylinder objects for NPCs and players;
  - a water-height query and a ceiling query;
  - "standing on object id" output.
- Build static geometry in C++ from the BSP through GMod's `IBaseFileSystem` (`VBaseFileSystem011`).
  Use player-solid brushes (contents-based, including playerclip), displacements that respect
  DISPINFO flags, and static props with solid type 6 using their `.phy` collision. Support
  LZMA-compressed lumps.
- Cache the result in `data/sa1/cache/`. Fall back to the client world surfaces or to the server
  stream of `GetMeshConvexes`.
- Feed dynamic entities from Lua with one batched binary call per tick. Respect solidity and
  collision groups.
- Triggers are handled on the server through the real player entity. Teleports and pushes become
  module commands.

### Rendering
- Hardware skinning:
  - `Mesh(mat, 2)` plus `mesh.BoneData`, built once per character, split by material.
  - Each frame, `IMesh:DrawSkinned` with at most 52 node-world matrices. Sonic needs 28 to 36
    bones.
  - Store vertices in node-local space, pre-scaled by S. Bone matrices hold rotation and
    translation only.
  - Fall back to CPU skinning into a dynamic mesh when `DrawSkinned` is missing.
- Textures: PVR is decoded to PNG in `data/sa1/tex/`, loaded with
  `Material(..., "vertexlitgeneric mips smooth")`, and the resulting texture is put into
  `CreateMaterial` materials with the flags mapped from the SA1 material (alpha test, translucent,
  additive, no-cull, unlit).
- Lighting: an ambient cube from 6 × `render.ComputeLighting`, plus `SetLocalModelLights` and
  `RenderFlashlights`.
- Shadows: a procedural blob shadow, as SA1 does.
- One client-side render entity per visible character, with its own render bounds. Hide the
  player through `PrePlayerDraw`.

### Player integration
- The player entity is the authority for position. Movement is driven through
  `SetupMove`/`Move`/`FinishMove` overrides on the server, from the client's reported state. Do
  not use the drive system. If this conflicts with prediction, fall back to G64's drive plus chase
  observer.
- Set the hull to (radius × S, height × S) and the view offset to eyes_height × S on both realms.
- Input is read on the client into SA1's input struct (camera-relative stick, A/B/X/Y, L/R). The
  module runs a 60 Hz accumulator with interpolation, and pauses when `FrameTime() == 0`.
- Camera: a CalcView chase camera with hull collision. First person comes free with
  distance 0.
- Damage: block it with `EntityTakeDamage` and forward it to the owner's SA1 hurt logic. There is
  no fall or drown damage.
- Attacks are requested by the client and validated by the server.
- State sync uses unreliable 30 Hz packets of about 40 bytes. Events use reliable messages.
  Clients without the module or disc fall back to the stock player model.

### Audio
- Sound effects and voices: decode MLT (AICA ADPCM) and resample to 22.05 or 44.1 kHz mono. Then
  call `sound.Generate` and play with `EmitSound` or `CreateSound`.
- Music: decode ADX to WAV in `data/` and play it with `sound.PlayFile`. Loop points are handled
  through `noblock` and `SetTime`.
- No module-owned audio device.

### Units
- S = 4.0 HU per SA1 unit (convar). Axis map: `src = S * (x, -z, y)`. Speeds and gravity are
  converted × 60 × S per second. Sonic's top speed is about 3840 HU/s.

## 2. Binary modules

### 2.1 Naming, location, loading

| File in `garrysmod/lua/bin/` | Realm | `require` name | Platform |
|---|---|---|---|
| `gmcl_sa1_win32.dll` / `gmsv_sa1_win32.dll` | client / server (and menu state, which uses `gmsv_`) | `sa1` | Windows 32-bit |
| `gmcl_sa1_win64.dll` / `gmsv_sa1_win64.dll` | client / server | `sa1` | Windows 64-bit |
| `gmcl_sa1_linux.dll` (also `linux32` accepted on x86-64-branch 32-bit Linux) | client | `sa1` | Linux 32-bit (actually a `.so`) |
| `gmcl_sa1_linux64.dll` | client | `sa1` | Linux 64-bit (`.so`) |
| `gmcl_sa1_osx.dll` / `gmcl_sa1_osx64.dll` | client | `sa1` | macOS (`.dylib`, renamed); 32-bit macOS is obsolete |

- The extension is always `.dll`, even on Linux and macOS.
- `require(name)` first tries a binary module with that name, then a Lua module. If a binary
  module is missing, the error counts toward `sv_kickerrornum` even inside `pcall` (wiki bug 2498).
  Always call `util.IsBinaryModuleInstalled("sa1")` first; it was added 2023-01-25.
- How `util.IsBinaryModuleInstalled` works (in `lua/includes/extensions/util.lua`):
  - It picks the suffix from `system.IsWindows()`, `system.IsLinux()` and whether
    `jit.arch == "x86"`.
  - It checks `file.Exists("lua/bin/gm{cl|sv}_<name>_<suffix>.dll", "MOD")`. The prefix is
    `gmcl` in the client state and `gmsv` in the server and menu states.
  - On 32-bit Linux running LuaJIT other than 2.0.4, it also accepts the suffix `linux32`.
- Binary modules **cannot be installed as part of an addon**. They are only detected in
  `garrysmod/lua/bin/`, as a safety measure. The Workshop packer (gmad) rejects `.dll`, `.exe`,
  `.htm`, `.html`, `.css` and `.js` files, and the allowed folders are fixed (`lua`, `materials`,
  `models`, `sound`, `particles`, `resource/fonts`, `data_static`, ...).
- Lua sent by a server can `require` a client module that the user installed locally. This is
  how G64 works in multiplayer, and it means a server only needs the Lua addon.
- `_MODULES` lists the loaded module names.

### 2.2 32-bit vs 64-bit, branches, platforms (state in October 2026)

- **September 2026 update (16 Sep 2026)**: 64-bit builds of the Windows client, dedicated server
  and Hammer are on the public branch. The 64-bit client is chosen from Steam launch options, and
  the 32-bit builds remain. Before this, the `x86-64` beta branch was required; the G64 README
  still says so, and its Lua refuses to run on `jit.arch == "x86"`.
- `BRANCH` can be `"unknown"` (main), `"dev"`, `"prerelease"` or `"x86-64"`. It is only defined
  on the server for local servers. **Do not infer bitness from `BRANCH`.** gwater2 still does
  this, which is now wrong for main-branch 64-bit users. Use `jit.arch` (`"x86"` or `"x64"`)
  together with `system.Is*`.
- Linux: the native client on the main branch is 32-bit, and 64-bit Linux comes through the
  x86-64 branch. Many Linux players run the Windows build under Proton, so the Windows DLL of
  matching bitness is what loads for them.
- macOS: `_osx64` exists in garrysmod_common's naming and is untested here. Low priority.
- LuaJIT differs between builds: the main branch reports `jit.versionnum` 20004. This does not
  matter when you only use `ILuaBase`, which is a C++ vtable interface.
- Target order for SA1: `win64`, then `win32` (32-bit users still exist), then `linux64`, then
  `linux`. A portable C core makes the extra targets cheap.

### 2.3 Build tooling

- **gmod-module-base, development branch** (header-only; recommended):
  - `GMOD_MODULE_OPEN()` and `GMOD_MODULE_CLOSE()` export `gmod13_open(lua_State*)` and
    `gmod13_close(lua_State*)`.
  - `LUA_FUNCTION(name)` produces a `lua_CFunction` that fetches `L->luabase` and calls
    `LUA->SetState(L)` before running your body. That keeps it correct when Lua calls it from a
    coroutine.
  - `lua_State` is opaque. The header pads it before the `ILuaBase* luabase` member: 70 bytes on
    32-bit builds, 114 bytes on 64-bit builds.
  - The CMake helper `set_gmod_suffix_prefix()` always sets the prefix to `gmsv_`. **A client
    module must override PREFIX to `gmcl_`.**
  - The master branch has the older macros (`GMMODULE`, `LUA` = `state->luabase`). G64 uses that
    style and keeps a global `ILuaBase*`; avoid that pattern.
- **garrysmod_common (premake5)**:
  - Set `PROJECT_GENERATOR_VERSION = 3` to get x86_64 platforms.
  - It names targets `gmcl_` or `gmsv_` plus the per-OS/arch suffix, and the extension is `.dll`
    everywhere.
  - It builds with a static CRT on Windows and Linux, C++17 and SSE2.
  - `abi_compatible = true` limits Windows builds to VS2015–VS2022 release builds and sets
    `_GLIBCXX_USE_CXX11_ABI=0` on Linux. This is needed when you call into Source SDK classes.
  - `IncludeSDK*` adds Source SDK headers (sourcesdk-minimal) for `IMaterialSystem`,
    `IVEngineClient`, the file system and so on.
  - `IncludeLuaShared()` adds `ILuaShared` and `ILuaInterface`. These are internal interfaces with
    `AddThreadedCall`, `CreateConVar` and others.
  - `--autoinstall` copies the result into `garrysmod/lua/bin`.
- G64 builds a plain MSVC v143 solution. It links the import libraries of `sm64.dll` and
  `SDL2.dll`, and those DLLs have to sit next to `gmod.exe` in `bin/win64`.
- **Recommendation**: build with CMake against gmod-module-base (development). `libsa1` is a
  static library in C99 (the decomp) with the port layer on top. Do **not** depend on Source SDK
  classes in v1, which removes the ABI and interface-version risk. Keep an optional SDK feature
  set (C++ meshes, procedural textures) behind a build flag, using garrysmod_common.

### 2.4 Lua C API surface (ILuaBase, development headers)

- **Stack and tables**: `Top`, `Push(i)`, `Pop(n)`, `GetTable(i)`, `GetField(i, name)`,
  `SetField(i, name)`, `CreateTable()`, `SetTable(i)`, `RawGet`/`RawSet`, `Next(i)`,
  `Insert`/`Remove`, `ObjLen(i)`.
- **Values**:
  - Readers: `GetString(i, &len)` (length-aware, binary-safe), `GetNumber`, `GetBool`,
    `GetCFunction`, `GetVector`/`GetAngle` (3 floats each).
  - Pushers: `PushNil`, `PushString(ptr, len)` (binary-safe), `PushNumber`, `PushBool`,
    `PushCFunction`, `PushCClosure(f, nUpvalues)`, `PushVector`, `PushAngle`.
  - Checkers: `CheckString`, `CheckNumber`, `CheckType(i, type)`, `IsType`, `GetType`,
    `GetTypeName`.
- **Calls**: `Call(nArgs, nResults)` and `PCall(nArgs, nResults, errFunc)`.
  `PushSpecial(SPECIAL_GLOB / SPECIAL_ENV / SPECIAL_REG)` gives you `_G`, the environment or the
  registry.
- **References**: `ReferenceCreate()` pops the top value and returns an integer handle;
  `ReferencePush(r)` and `ReferenceFree(r)` use it. G64 keeps per-Mario result tables in the
  registry and refills them in place every tick to reduce garbage.
- **User types**:
  - `CreateMetaTable(name)` returns a type id and pushes the metatable; set `__index`, `__gc` and
    `__tostring` on it.
  - `PushUserType(ptr, type)` wraps a pointer; `PushMetaTable(type)` followed by `SetMetaTable(-2)`
    attaches the methods.
  - `GetUserType<T>(i, type)` returns null on a type mismatch or an invalidated object.
    `SetUserType(i, nullptr)` invalidates an object.
  - `PushUserType_Value<T>` embeds a trivially destructible POD in the userdata (alignment ≤ 8).
  - gwater2 uses this pattern for its `FlexSolver` and `FlexRenderer` objects, with `__gc`
    deleting the C++ object.
- **Built-in GMod type ids** (Types.h, development branch): Nil 0, Bool 1, LightUserData 2,
  Number 3, String 4, Table 5, Function 6, UserData 7, Thread 8, Entity 9, Vector 10, Angle 11,
  PhysObj 12, Save 13, Restore 14, DamageInfo 15, EffectData 16, MoveData 17, RecipientFilter 18,
  UserCmd 19, ScriptedVehicle 20, Material 21, Panel 22, Particle 23, ParticleEmitter 24,
  Texture 25, UserMsg 26, ConVar 27, IMesh 28, Matrix 29, Sound 30, PixelVisHandle 31, DLight 32,
  Video 33, File 34, Locomotion 35, Path 36, NavArea 37, SoundHandle 38, NavLadder 39,
  ParticleSystem 40, ProjectedTexture 41, PhysCollide 42, SurfaceInfo 43.
  - A module can read engine userdata by type. gwater2 reads a `VMatrix` with
    `GetUserType<VMatrix>(i, Type::Matrix)`, and `GetUserType<ConVar>` the same way.
- **Errors**: `ThrowError`, `ArgError` and the `Check*` functions longjmp out of the function, so
  **C++ destructors in that frame do not run**.
  - Validate every argument before allocating anything.
  - Catch C++ exceptions inside each binding and turn them into a Lua error only after cleanup.
  - Never throw a Lua error from a callback that the C core invoked deep inside the simulation.
- **Bulk data**: there is no FFI in GMod Lua. Move large data as binary strings
  (`PushString(ptr, len)`), never as tables of `Vector`s. G64's per-vertex `Vector` tables are
  its main garbage-collection cost.

### 2.5 Threading

- Each Lua realm runs on a single thread. Call `ILuaBase` only on the main thread, during a call
  from Lua (a binding, a hook or Think).
- Worker threads are fine for:
  - disc reads, PRS, PVR, ADX and MLT decoding;
  - BVH builds;
  - audio mixing.
- Hand results back in one of two ways:
  - a lock-protected queue drained by a Lua `Think` hook through a `sa1.Poll()` call
    (recommended, no internal headers needed);
  - `ILuaInterface::AddThreadedCall(ILuaThreadedCall*)` from garrysmod_common's
    `LuaInterface.h`:
    - `Init()` is called on the main thread when the call is queued, and `Run(ILuaBase*)` runs
      later on the main thread.
    - The object is not deleted for you.
    - The April 2026 update added a virtual destructor to `ILuaThreadedCall`, which is an ABI
      change. Avoid this interface unless it is needed.
- Make Source material system calls (only on the optional SDK path) on the main thread inside
  render hooks. GMod normally renders in queued mode. gwater2 creates static meshes from worker
  threads; do not copy that.
- Module-owned audio threads are safe as long as they never touch Lua. G64's libsm64 fork runs its
  audio this way.

### 2.6 Lifetime and crash safety

- `gmod13_open` runs on `require`. `gmod13_close` runs when that Lua state shuts down (disconnect,
  map change, quit).
  - Create all state in a context during open and destroy it during close.
  - Invalidate all userdata (`SetUserType(i, nullptr)`) and free the registry references.
  - Whether the DLL itself is unloaded between maps is unverified, so never rely on static state
    surviving.
- Any fault inside the module crashes GMod; there is no sandbox. So:
  - bounds-check every disc structure, because bad dumps exist;
  - verify the SHA-1 of `1ST_READ.BIN` (`e74c8498...b6df`) before using any hard-coded addresses;
  - refuse other regions or revisions until per-version tables exist;
  - cap allocation sizes read from files.
- Do a version handshake:
  - the module exports `sa1.API_VERSION` and `sa1.BUILD`;
  - Lua refuses to run on a mismatch and prints the expected version;
  - this follows G64's `REQUIRED_MODULE` / `GetPackageVersion` idea.
- If the module is missing or the disc fails validation, the addon must degrade gracefully: no
  errors, and a chat or HUD hint. gwater2 shows `ErrorNoHalt` plus a chat line.

### 2.7 Distribution, installation, updates (G64 and gwater2 case studies)

- **G64**:
  - What ships:
    - a Workshop addon with Lua, VGUI icons and an `sm64.pcf` particle file;
    - a GitHub release zip extracted over the GMod root. It puts `gmcl_g64_win64.dll` in
      `garrysmod/lua/bin/`, and `sm64.dll` plus `bz2`, `jsoncpp`, `libcurl`, `zip` and `zlib1`
      DLLs next to `gmod.exe` in `bin/win64/`.
  - Installer (C#), install:
    - finds Steam from `HKLM\SOFTWARE\Wow6432Node\Valve\Steam\InstallPath`;
    - reads `steamapps/libraryfolders.vdf` to find the library that holds
      `appmanifest_4000.acf`, then `steamapps/common/<installdir>`;
    - validates that `hl2.exe` or `gmod.exe` is present;
    - downloads `releases[0].assets[0]` from the GitHub releases API and unzips it in place.
  - Installer, uninstall: deletes a fixed list of files.
  - Auto-updater: a second module, `gmcl_g64_updater_win64.dll`, runs when the world initialises
    if `g64_auto_update` is set.
    - It compares the `g64_lib_version` convar against the latest GitHub tag and downloads the
      zip with libcurl.
    - It extracts the zip over the game directory and skips files that are locked.
    - The main module could also launch a PowerShell update script when it closes.
  - In multiplayer, an outdated client refuses to spawn Mario.
- **gwater2**:
  - Installation is a `.bat` that uses PowerShell `Invoke-WebRequest` and `Expand-Archive`.
  - Uninstalling deletes the DLLs it added: module DLLs in `lua/bin`, FleX and NVIDIA DLLs in the
    game root, and shader `.vcs` files.
  - In Lua: if `util.IsBinaryModuleInstalled` is false, it shows a localized error and exits.
    Otherwise it calls `pcall(require, ...)`.
- **Recommendation for SA1**:
  1. Workshop: Lua plus original UI assets only. No Sega data, no binaries.
  2. GitHub Releases: a zip containing only `garrysmod/lua/bin/gmcl_sa1_{win64,win32,linux64,linux}.dll`
     and a `SHA256SUMS` file. Each DLL is self-contained: static CRT, zlib-free, SDL-free. Nothing
     goes into `bin/`, so uninstalling means deleting those files.
  3. Optional installer that reuses the Steam library discovery above. **No silent
     auto-updater.** Downloading and running code automatically is a supply-chain risk. Instead,
     Lua can call the GitHub releases API with `HTTP()` and show an "update available" link.
  4. Disc path:
     - stored in a client convar `sa1_disc_path` (archived);
     - picked with a native file dialog on Windows (G64 uses `GetOpenFileName`), with a
       text-entry fallback on Linux;
     - accepts a `.gdi` (tracks referenced relative to it) or a folder that holds
       `1ST_READ.BIN` and `SONICADV/`;
     - CDI and CHD support can come later.

---

## 3. World geometry for SA1 collision

### 3.1 What the SA1 player code needs from the host

The decompiled player code expects SA1-style collision. A port layer must supply the following.
Its exact shape is defined by the people porting the collision code.

- **Static triangles**: vertices, a unit normal, and attributes. SA1-like attribute flags are
  solid, water, hurt or damage, slippery or no-grip if the code uses them, a footstep/surface
  class, and "do not stand".
  - Queried every frame by bounding sphere or box around the player.
  - Built once per map and indexed with a BVH inside the module.
- **Dynamic objects**:
  - an id, a local triangle set and attributes;
  - a world transform plus linear and angular velocity, updated each tick, so the character can
    ride platforms and inherit their motion;
  - an "enabled" flag.
- **Simple actors**: cylinders (center, radius, height) for NPCs and players. Used for bumping,
  homing-attack targets and attack hit tests.
- **Water**: a surface height and an "in water" flag at a position.
- **Outputs to GMod**:
  - the object id the character stands on;
  - which object it pushed;
  - hit events against actors.

### 3.2 Where geometry comes from in GMod

**a) World brushes**
- **Server physics**: `game.GetWorld():GetPhysicsObject():GetMeshConvexes()`.
  - Returns a list of convexes; each one is a list of `{pos=Vector}` with three entries per
    triangle.
  - It is the real player-collision geometry: it includes clip brushes and excludes non-solid
    brushes.
  - It has **no displacements** (G64 notes this explicitly) and no materials.
  - On the client the world `PhysObj` is normally NULL, which is why G64 streams this data from
    the server.
- **Client visual faces**: `game.GetWorld():GetBrushSurfaces()` returns `SurfaceInfo` objects.
  - `GetVertices()` gives a polygon, usually a counter-clockwise quad. `GetMaterial()`,
    `IsWater()`, `IsNoDraw()` and `IsSky()` are also available.
  - It is fast and needs no server, but it has **holes**: no clip brushes, and nodraw faces are
    absent. gwater2 uses it only as a fallback ("collision WILL have holes").
- **Raw BSP on the client**:
  - In Lua: `file.Open("maps/"..game.GetMap()..".bsp", "rb", "GAME")` plus luabsp. luabsp is
    GPL-3 and slow on big maps.
  - In C++: open the same path through GMod's file system interface. GMFS does this by getting
    `filesystem_stdio.dll` (`filesystem_stdio_client.so` with `RTLD_NOLOAD` on Linux), calling
    `CreateInterface("VBaseFileSystem011")`, then using `Open`, `Size`, `Read` and `FileExists`
    with path id `GAME`. That path id also finds maps inside mounted GMA or Workshop content.
    Parse with source-parsers `bspparser` (MIT), which has face triangulation, displacement
    triangulation, static-prop iteration and texture accessors. gwater2 does exactly this for
    its map collider.
  - **Best fidelity for player collision**: rebuild brush solids from `dbrush`/`dbrushside`.
    - Keep brushes whose contents are SOLID, PLAYERCLIP, WINDOW, GRATE or MOVEABLE. Skip water,
      triggers and non-solid brushes.
    - Clip each brush by its side planes to get polygons. luabsp shows this technique for clip
      brushes.
    - Take the attributes from the texinfo surface flags (SKY, NODRAW, ...) and the material's
      surfaceprop.
    - This includes `func_detail`, which is compiled into world brushes.
  - Some Workshop maps are repacked with LZMA-compressed lumps (`bspzip -repack`). The parser
    must handle the "LZMA" lump header, or fall back.

**b) Displacements**
- Read them from `LUMP_DISPINFO` and `LUMP_DISP_VERTS` (power 2–4 grids over a 4-sided face).
  `LUMP_PHYSDISP` holds the physics version in another format.
- Respect the DISPINFO flags for no-physics and no-hull-collision.
- G64 triangulates them in Lua with luabsp and buckets them into their own tiles.

**c) Static props**
- They live in the game lump `sprp`: a dictionary of model names, a leaf list, then entries with
  origin, angles, prop-type index, solid type (0 none, 2 bounding box, 6 VPhysics), flags, and
  in newer versions a uniform scale.
- source-parsers supports sprp versions 4, 5, 6 and 7 ("Multiplayer2013"). **Verify** which
  sprp versions GMod maps use; v10 and v11 are common in Source 2013 multiplayer branches.
- For collision, use the model's `.phy` through C++ `phyparser` and the file system, transformed
  by origin and angles. G64 instead does this in Lua:
  - it creates a client prop with `ents.CreateClientProp(model)`;
  - reads `GetPhysicsObject():GetMeshConvexes()`;
  - removes the prop;
  - rotates the vertices with its own yaw/pitch/roll matrix;
  - keeps only Solid == 6 props.
- `util.GetModelMeshes(model, lod, bodygroupMask, skin)` returns visual triangles and bone bind
  poses. Use it as a fallback for props without a `.phy`.

**d) Entities, every tick**
- **Physics props**: use `PhysObj:GetMeshConvexes()`. On the client, a `PhysObj` exists only if
  something created one there. Two workarounds:
  - G64 temporarily calls `PhysicsInit(SOLID_VPHYSICS)` and then `PhysicsDestroy` on the client
    entity;
  - gwater2 creates a temporary `ents.CreateClientProp(model)`.
  For ragdolls, read each physics bone with `GetPhysicsObjectNum(i):GetMesh()`.
- **Brush entities** (func_door, func_door_rotating, func_movelinear, func_tracktrain,
  func_rotating, func_brush, func_wall, func_breakable, func_physbox, func_platrot,
  func_pushable, ...):
  - call `Entity:GetBrushSurfaces()` on the client and fan-triangulate the faces;
  - move the object by `GetPos()` and `GetAngles()` each tick.
  - G64 keeps an allow-list of exactly this kind.
- **Disable collision** while any of these holds: the collision group is `COLLISION_GROUP_WORLD`,
  `not IsSolid()`, the `FSOLID_NOT_SOLID` flag is set, or the object is being carried. G64
  disables an object by moving it to z = -32768.
- **NPCs and players**: build cylinders from `WorldSpaceAABB()` and update them each tick.
- **Lifecycle**:
  - queue entities from `OnEntityCreated` and process a few per tick (G64 does 8);
  - drop them on `EntityRemoved`;
  - rebuild everything on `PreCleanupMap`/`PostCleanupMap`.
- Batch the per-tick transform updates into **one** module call: a binary string of
  (id, pos, ang, enabled) records. Do not call the module once per entity.

**e) Triggers**
- `trigger_*` brush entities exist **only on the server**; they are not transmitted. Handle them
  through the real player entity on the server:
  - If the player is moved through the movement pipeline (section 5), the engine's normal
    trigger touching should fire `trigger_hurt`, `trigger_multiple` and `trigger_once`. Verify
    whether `SetPos`-only movement fires them. G64 rebuilt `trigger_teleport` as its own entity
    and mirrored its Enable/Disable inputs through `AcceptInput`, which suggests it does not.
  - `trigger_teleport`: the server teleports the player and sends a teleport command to the
    owning client's module.
  - `trigger_push`: the server sends a velocity-impulse command.

**f) Water**
- Static water can be found three ways:
  - water faces through `SurfaceInfo:IsWater()` on the client;
  - BSP brushes with `CONTENTS_WATER`;
  - `func_water_analog` entities (moving water).
- Runtime checks:
  - `util.PointContents(pos)` with `CONTENTS_WATER`;
  - `Player:WaterLevel()` (0 to 3);
  - G64 traces `MASK_WATER` 10000 HU up and 10000 HU down from the character and uses
    `FractionLeftSolid` to find the surface height.
- SA1 needs a surface height. Compute it in the module from the water polygons and refresh it
  from traces when water entities move.

**g) Surface classes**
- Use the trace `MatType` (`MAT_*`) or the material's surfaceprop. Map it to SA1 footstep, sound
  and particle classes, the way G64 maps `MAT_*` to SM64 terrain types.

### 3.3 G64's approach and trade-offs

- **How G64 gets the world**:
  - The server walks the world physics convexes and sends 32 of them per tick per requesting
    player, as int16 coordinates. That loses precision but fits the ±16384 world.
  - The client sorts triangles into XY tiles: 1000 HU for brushes, 500 HU for displacements.
    Displacements and static props are parsed on the client with luabsp.
  - Only the tile under Mario is loaded into libsm64. Crossing a tile edge reloads the whole
    static set, which causes hitches and seams.
  - Results are cached in `data/g64/cache/<map>_cache.dat` (a versioned header plus float
    triangles). The module reads it back by building a Windows path from `_getcwd()`.
- **Trade-offs**:

| Source | Accuracy | Cost | Notes |
|---|---|---|---|
| Server physics convexes, streamed | high, but no displacements | slow first load (MBs over net), needs server Lua | G64 default; good fallback |
| Client `GetBrushSurfaces` | holes (no clip/nodraw) | fast | gwater2 fallback |
| Lua BSP (luabsp) | high | slow in Lua; GPL-3 | G64 for displacements and static props |
| **C++ BSP via IBaseFileSystem** | highest (solids, clips, disps, props, materials) | fast; parser upkeep | **recommended** |

---

## 4. Rendering an animated, skinned SA1 character

### 4.1 Facts about the SA1 assets (measured from SA Tools splits in `AdventureRip/figure/*`)

| Model file | Nodes | Nodes with mesh | Vertices | Bind-pose height |
|---|---|---|---|---|
| sonic/s_kihon10_sonic | 73 | 28 | 635 | ~10.1 units (Y -5.33..4.75) |
| miles/m_kihon3_miles | 68 | 28 | 649 | ~8.7 |
| knuckles/knuck_a | 73 | 28 | 624 | ~11.0 |
| amy/amy_a | 78 | 33 | 656 | ~9.2 |
| e102/e102_a | 77 | 34 | 680 | ~21.3 |
| big/b_kihon0_bic | 73 | 36 | 729 | ~21.0 (not in a standing pose) |

- The models are Ninja BASIC models (`NJS_MODEL`): each node has its own rigid mesh in that
  node's local space.
- Characters use **welds**. For example, Sonic's `WeldData.ini` has 34 entries that copy
  vertices of joint meshes from neighbouring nodes. Every vertex therefore follows 1 node, or 2
  when an averaging weld is used. That fits **hardware skinning with 1–2 weights**.
- Bone palette per draw is the nodes that carry meshes or are weld sources: 28 to 36. That is
  under the 52-matrix limit, so no split is needed. Verify per character, including extra
  objects such as hammers, rods and Gamma's parts.
- Materials (`NJS_MATERIAL`) have diffuse and specular ARGB, an exponent, a texture id, and
  attribute flags. The flags cover the blend source and destination, double-sided, environment
  map, ignore light, flat shading, UV clamp and flip, and filtering.
- Textures are PVR images inside PVM archives, PRS-compressed (for example `SONICADV/SONIC.PRS`).
  The formats are ARGB1555, RGB565 and ARGB4444 (possibly also YUV422, palettized or VQ), in
  twiddled layout. Decode them to RGBA8 in the module.

### 4.2 Options for drawing

**A. Lua hardware skinning (recommended)**
- API status: `Mesh(material, boneWeights)` with `boneWeights = 2` creates a skinnable `IMesh`.
  `mesh.BoneData(slot 0|1, matrixId 0..52, weight)` is set per vertex during `mesh.Begin`. Both
  `mesh.BoneData` and `IMesh:DrawSkinned` were added 2026-02-25 and shipped in the April 2026
  update.
- `IMesh:DrawSkinned(bones, multiply=false)` draws with a list of up to 52 `VMatrix`.
  `multiply=true` also applies the current model matrix, at a performance cost.
- Related engine changes:
  - April 2026 raised the maximum RenderData size to 16 MB because many skinned meshes on x86-64
    could crash.
  - September 2026 fixed `IMesh:BuildFromTriangles` reading bone weights. It reads at most 2,
    while `util.GetModelMeshes` can return 3.
- **Build once per character and per material group:**
  - Store vertex positions **in node-local space, pre-multiplied by S**. That matches SA1's data,
    so bone matrices need no inverse bind pose.
  - Store the normal, UV and color.
  - Set `BoneData(0, nodeIndex, 1)` per vertex, or two weights for averaged welds.
  - Make separate `IMesh` objects for opaque, alpha-test, translucent and additive material
    groups.
- **Each frame:**
  - the module computes the node world transforms (SA1 motion evaluation plus code-driven
    overrides), interpolated between the last two 60 Hz states;
  - it converts them to Source space as rotation + translation in HU, with no scale, which keeps
    normals unit length;
  - it writes them into a **preallocated** Lua table of `Matrix` objects. It either writes the
    `VMatrix` storage in place through `GetUserType<VMatrix>(i, Type::Matrix)` (verify that the
    userdata pointer is writable storage; gwater2 reads it this way), or returns a binary string
    that Lua unpacks into the matrices with `Matrix:SetUnpacked`.
  - Lua then calls `render.SetMaterial(mat)` and `imesh:DrawSkinned(bones)` for each group.
- Limit: 65535 vertices per mesh. SA1 characters are about 700 vertices.

**B. CPU skinning into a dynamic mesh (fallback when `IMesh.DrawSkinned` is nil)**
- The module skins the vertices and returns positions, normals and UVs as a binary string. Lua
  then emits them with `mesh.Begin(MATERIAL_TRIANGLES, n)`, `mesh.Position`, `mesh.Normal`,
  `mesh.TexCoord`, `mesh.Color` and `mesh.AdvanceVertex` inside a render hook. The dynamic mesh
  draws immediately on `mesh.End()`.
- G64 does a variant of this:
  - every Think it destroys and recreates a `Mesh()`;
  - it fills the mesh from Lua tables of per-vertex `Vector`s;
  - it interpolates each vertex with `LerpVector` between two snapshots taken at 33 Hz.
- It is correct but garbage-heavy. Rebuilding a mesh with the static API is roughly 4 C calls per
  vertex.
- The wiki warns: do not create or modify an `IMesh` inside `ENT:GetRenderMesh`.

**C. C++ rendering through IMaterialSystem (gwater2 style)**
- `materials->GetRenderContext()`, then `CreateStaticMesh` or the dynamic mesh, `CMeshBuilder`,
  and `Draw()`, called from a Lua render hook after `render.SetMaterial`.
- Fastest, but it needs the Source SDK, ABI discipline and interface-version maintenance. Keep it
  as an optional optimisation only.

**D. Generated Source model, mounted at runtime (phase 2 option)**
- The module writes `.mdl`, `.vvd`, `.dx90.vtx`, `.phy`, `.vtf` and `.vmt` files built from the
  user's disc. `file.Write` allows all these extensions plus `.gma`.
- It packs them into `data/sa1/sa1_chars.gma`, then calls `game.MountGMA("data/sa1/sa1_chars.gma")`.
  The path is relative to `garrysmod/`, and error models are reloaded.
- That gives `models/sa1/sonic.mdl` with full engine features: model lighting, render-to-texture
  and flashlight shadows, mirrors, a usable player model, and hitboxes.
- Animate it with `Entity:AddCallback("BuildBonePositions", ...)` plus `Entity:SetBoneMatrix`,
  driven by the module's node matrices. No sequences are needed.
- Costs:
  - writers for the MDL v48/49, VVD and VTX formats;
  - the server still does not have the model (it has no disc), so these are client-only visuals.
- Real shadows and player-model integration are the reasons to do it later.

### 4.3 Textures at runtime

| Method | How | Pros / cons |
|---|---|---|
| **PNG in data/ (recommended)** | Module: PVR → RGBA → PNG (e.g. public-domain stb_image_write). Lua: `file.Write("sa1/tex/<pvm>_<n>.png", bytes)`, then `Material("data/sa1/tex/<pvm>_<n>.png", "vertexlitgeneric mips smooth")`. Take its texture with `:GetTexture("$basetexture")` and put it on your own `CreateMaterial` materials with `IMaterial:SetTexture("$basetexture", tex)`. | File-backed, so it survives device resets. Mipmaps with `mips` (also forces power-of-two). `smooth` is needed, or you get point sampling. Omit `noclamp` to clamp, include it to tile. The default generated material has `$vertexcolor 1` and `$vertexalpha 1` unless you pass `alphatest`. |
| VTF in data/ | Module writes VTF 7.2 BGRA8888 with CLAMPS/CLAMPT flags. Point a material's `$basetexture` at a `../data/...` path. | Allows exact flags and mips. `.vtf` and `.vmt` are writable, but referencing `../data/` from a material is community lore: **verify**. |
| Render target | `GetRenderTargetEx(name, w, h, RT_SIZE_OFFSCREEN, MATERIAL_RT_DEPTH_NONE, flags, 0, IMAGE_FORMAT_RGBA8888)`, then paint pixels. G64 receives per-pixel `{r,g,b,a}` Lua tables and draws one `surface.DrawRect` per pixel. | Slow. Content is lost on device reset; G64 polls one pixel every second with `render.CapturePixels`/`render.ReadPixel` and repaints if it is black. Render targets are never garbage collected. Avoid. |
| C++ procedural texture | `IMaterialSystem::CreateProceduralTexture` with an `ITextureRegenerator` | Robust (regenerates itself). Needs the SDK. Optional. |

**SA1 material flags → Source material**

| SA1 (Ninja) | Source |
|---|---|
| opaque texture | `VertexLitGeneric`, `$model 1` |
| 1-bit alpha (ARGB1555, punch-through) | `$alphatest 1` (+ `$alphatestreference 0.5`) |
| alpha blend (SRC_ALPHA / INV_SRC_ALPHA) | `$translucent 1`. Draw in `DrawTranslucent` (`RENDERGROUP_BOTH`) as its own submesh |
| additive (destination ONE) | `$additive 1` |
| double-sided | `$nocull 1` |
| ignore light | `UnlitGeneric` (+ `$model 1`) |
| env map (USE_ENV, sphere mapping) | No sphere maps in Source. Approximate with `$envmap env_cubemap` and `$envmaptint`, or compute UVs on the CPU for those few submeshes (method B) |
| material diffuse color | `$color2` per material, or vertex color with `$vertexcolor 1` |
| UV clamp | texture clamp flags |
| UV flip (mirror wrap) | Not supported. Bake a mirrored copy of the texture and remap UVs at build time |

### 4.4 Lighting

- When an `IMesh` is drawn from `ENT:Draw`, `PostPlayerDraw` or a 3D hook, model lighting is not
  set up for you. `render.SuppressEngineLighting` "does not affect IMeshes". The `DrawSkinned` wiki
  example sets the lighting by hand. Recommended per character per frame:
  1. Six calls to `render.ComputeLighting(center, dir)` for ±X, ±Y and ±Z. Feed the results to
     `render.SetModelLighting(BOX_FRONT/BACK/LEFT/RIGHT/TOP/BOTTOM, r, g, b)`.
  2. `render.SetLocalModelLights({ ... })` with up to 4 point, spot or directional lights (for
     example nearby `DynamicLight`s), or `render.SetLocalModelLights()` to clear them.
  3. Draw the meshes.
  4. `render.RenderFlashlights(function() <draw again> end)` adds the projected flashlight passes
     (added 2023-06-28).
  5. `render.SetColorModulation(r, g, b)` for tints such as the damage flash or invincibility
     effects.
- G64 uses `ENT:GetRenderMesh()` plus `DrawModel()`, so the engine lights it like a model. It
  draws three passes with `render.MaterialOverride`:
  - a white `VertexLitGeneric` pass that produces only the lighting;
  - an `UnlitGeneric` vertex-color pass multiplied in with
    `render.OverrideBlend(true, BLEND_DST_COLOR, BLEND_ZERO, ...)`;
  - a textured `$decal $translucent` pass.
  `GetRenderMesh` takes no bone matrices, so this is not usable with `DrawSkinned`.

### 4.5 Where the character is drawn

- **G64**:
  - Mario is a scripted `anim` entity (`g64_mario`, `RENDERGROUP_BOTH`, render bounds of
    ±160/scale HU). Its model is set to a sphere prop purely to get a round shadow.
  - The owner player gets `SetNoDraw(true)` and model scale 0.8, and loses their weapons.
  - The player "drives" Mario with `drive.PlayerStartDriving(ply, mario, "G64_DRIVE")`, an empty
    drive class, plus `OBS_MODE_CHASE`.
  - The server also spawns an invisible `g64_physbox` (SOLID_BBOX physics) that follows Mario so
    that physics props bump into him.
- **Recommended for SA1**: keep the **player entity as the character**, so damage, NPC targeting,
  triggers and other addons all see the right position. Drawing:
  - hide the default player model with `GM:PrePlayerDraw` returning true;
  - draw through **one client-only render entity per visible SA1 character**
    (`ents.CreateClientside("sa1_render")`). It follows the player, has its own
    `SetRenderBounds`, and implements `Draw` and `DrawTranslucent`;
  - this avoids player render bounds and culling oddities. The September 2026 update made
    bone manipulations stop overriding `SetRenderBounds`, but players remain awkward.
  - For the local player in third person, the render entity draws whenever the view is not first
    person. The player model is still suppressed with `PrePlayerDraw`, which runs only when the
    player would be drawn.
- **Phase 2**: with method D, set the generated model on a `ClientsideModel` instead.

### 4.6 First-person view and camera

- SA1 has no first-person mode. Offer a camera distance with 0 meaning first person, as G64 does.
  At 0, hide the character (or only its head nodes) and put the view at `eyes_height * S`.
  That is Sonic 7 × 4 = 28 HU, which is also `Player:SetViewOffset`.

### 4.7 Shadows

- SA1 draws simple blob shadows under characters. Reproduce that:
  - trace down from the character's center against solid world and props;
  - draw a quad aligned to the hit normal (`render.DrawQuadEasy`) with a procedurally generated
    radial alpha texture;
  - scale and fade it with height above the ground.
  This is authentic and cheap.
- Engine render-to-texture shadows and flashlight shadows come only from models. `IMesh` draws do
  not cast them. G64's sphere-model trick produces a round engine shadow. Method D would give
  real silhouette shadows.

---

## 5. Player integration

### 5.1 Authority models

1. **G64 model (client-authoritative, separate entity)**:
   - The owning client simulates on its own 30 Hz SysTime-based timer, sends position, animation
     and flags every tick, and the server calls `SetPos` on both the player and Mario.
   - Other clients run an anim-only "fake" Mario.
   - Proven, but the player entity is teleported constantly and the drive/observer tricks are
     needed.
2. **Recommended v1 (client-authoritative, player is the character)**:
   - The owning client runs `libsa1` at a fixed 60 Hz.
   - It sends a state packet (5.7) at 30 Hz as an **unreliable** net message, and reliable
     messages for discrete events.
   - On the server, the player's movement is replaced with the newest reported state:
     - `SetupMove` puts the reported origin and velocity into `CMoveData`;
     - `Move` returns true, which skips the engine movement;
     - `FinishMove` writes `mv:GetOrigin()` to the player and returns true.
     Moving through the movement pipeline (instead of `SetPos`) keeps the engine's normal
     post-move logic: ground entity, trigger touching, water level.
   - On the owning client, the same hooks must produce the same result or Source prediction
     will fight it. Use the client's own simulated state for the origin. The mismatch is only
     latency, and you can reduce prediction errors by tolerating small deltas or by using
     option 3.
   - If this turns out to be too jittery, fall back to G64's drive plus chase-observer approach.
   - **Note**: in singleplayer, `SetupMove`, `Move` and `FinishMove` do **not** run on the client,
     because prediction is off. Only the server realm runs them. So the client simulation must be
     driven by its own Think or timer loop, and the move hooks only apply positions.
3. **Advanced, predicted (optional)**:
   - Put the authoritative character state into the `CUserCmd` stream. In `CreateMove` and
     `StartCommand`, write position into `SetForwardMove`, `SetSideMove` and `SetUpMove`, and the
     facing yaw in BAMS into `SetMouseX`.
   - Both realms' `Move` then take the origin from the command, so prediction matches exactly.
   - Unverified: whether these floats travel at full precision without clamping (Source writes
     usercmd forwardmove as a float; the default movement clamps it later, but `Move` returns
     before that).
   - Abuses fields other addons may read. Experimental.
4. **Server-authoritative**:
   - Needs `gmsv_sa1` and a disc on the server.
   - Needs a deterministic simulation (the same compiler flags, no FMA contraction, SA1's own
     sin/cos tables).
   - Needs a module snapshot and restore per command for prediction replays.
   - Not for v1.

### 5.2 Movement details

- Order of hooks: `CreateMove` (client only, easy to override), then `StartCommand` (shared, the
  first step of prediction), then `SetupMove`, then `Move`, then `FinishMove`.
  - Changes to the `CUserCmd` inside `SetupMove` are ignored.
  - In `Move`, change only the move data; it can run several times during prediction.
- Player state while active:
  - **Hull**: `Player:SetHull(Vector(-r,-r,0), Vector(r,r,h))` and `SetHullDuck`. Set it in
    **both realms**; it is not replicated. Sonic at S=4: r = 16, h = 40.
  - **View**: `SetViewOffset(Vector(0,0,eyes*S))` and `SetViewOffsetDucked`.
  - Hide weapons and the HUD. G64 suppresses `CHudHealth`, `CHudBattery`, `CHudCrosshair` and
    others through `HUDShouldDraw`.
- Turn off Source side effects:
  - fall damage: `GM:GetFallDamage` returns 0;
  - drowning: block `DMG_DROWN`, because SA1 has its own air timer;
  - footstep sounds: `PlayerFootstep` returns true.
- `sv_maxvelocity` (default 3500) limits engine-integrated velocities. SA1's top horizontal speed
  at S=4 is 3840 HU/s. Write origins directly and keep `mv:SetVelocity` informational, clamped or
  not; verify that velocity clamping does not interfere.
- Vehicles: G64 parents Mario to the seat, plays a sitting action, and maps gamepad input to `IN_*`
  inside vehicles. For SA1, pause the simulation, pose the character sitting, and resume at the
  exit point.

### 5.3 Camera

- Use `GM:CalcView(ply, origin, angles, fov, znear, zfar)` and return
  `{origin, angles, fov, drawviewer = true}`. Add to the values rather than replacing them where
  possible, for compatibility with other addons. Also handle `CalcVehicleView`.
- Make a chase camera that orbits with the mouse (`ply:EyeAngles()`) and collides through
  `util.TraceHull` (G64 uses a 4 HU hull, about like `drive_base`'s third-person helper). The
  mouse wheel changes the distance through a convar; 0 means first person.
- SA1 input is **camera-relative**: pass the camera yaw to the module each tick. G64 passes the
  aim vector as camLook X/Z.
- SA1's level-specific camera scripts are out of scope.

### 5.4 Input

- **Keyboard and mouse**: poll `input.IsButtonDown(KEY_*)` with configurable key convars on the
  client, as G64 does; G64 normalises WASD into a stick. Alternatively, read `cmd:KeyDown(IN_*)`
  and the forward/side move in `CreateMove`.
- **Gamepad through Source**: requires the user's `joystick 1` setup.
  - `input.GetAnalogValue(ANALOG_JOY_X/Y/R/U)` returns -32768..32767 (since 2021-01-27).
  - Buttons come from `input.IsButtonDown`: 114 = A, 115 = B, 116 = X, 117 = Y, 118/119 =
    shoulders, 146–149 = D-pad, 154/155 = triggers as buttons.
  - The wiki says analog triggers return 0 (unverified).
- **Gamepad through the module** (G64 uses SDL2 GameController): needs an extra library. Make it
  optional: XInput on Windows is header-only and needs no DLL, or link SDL statically. Right
  stick goes to view angles in `InputMouseApply` with `cmd:SetViewAngles`, as in G64.
- Suppress input while the spawn menu (`g_SpawnMenu:IsVisible()`), chat (`StartChat`/`FinishChat`)
  or the game UI (`gui.IsGameUIVisible()`) is open.
- SA1 mapping to the module input struct:
  - stick angle and magnitude, relative to the camera;
  - A = jump;
  - B/X = action (spin dash, Knuckles punch or dig, Amy hammer, Gamma lock-on, Big's rod);
  - L/R = SA1 camera rotation (optional, since the mouse orbits);
  - Start is not used.

### 5.5 Damage and health

- On the server, `GM:EntityTakeDamage(target, dmg)` **returns true to block** damage for SA1
  players. It forwards the amount, type and source position to the owner with a reliable net
  message. The module then runs the SA1 hurt logic: rings scatter, knockback, invincibility
  timer. G64 does this with damage divided by 10.
- Death: when the character is hit with 0 rings, or an SA1 death occurs (drowning, crushed), the
  client reports it and the server calls `ply:Kill()`. Respawn re-enters SA1 mode.
- Rings are the health. Show them on an SA1-style HUD; the HUD sprites can come from the disc.
  Optionally mirror them to `ply:SetHealth`. G64 sets health to Mario's 0–8 power meter with
  `SetMaxHealth(8)`.
- Pickups: in `HUDItemPickedUp`, a health kit or vial adds rings; G64 heals Mario this way.

### 5.6 Interacting with props and NPCs

- **Attacks**: during attack states, the client tests the module's attack volumes against GMod
  entities. The states are the spin ball, homing attack, spin dash, Knuckles punch, Amy hammer
  and Gamma shots. The tests use `util.TraceHull` or `ents.FindInSphere`.
  - The client sends a reliable `SA1_Attack` message with entity, direction, hit position and
    attack class.
  - The server validates distance and rate, then applies a `DamageInfo` (attacker = player) and a
    physics force (`phys:ApplyForceOffset`).
  - G64 sends `G64_DAMAGEENTITY` and the server applies random damage in a range, force × 15000,
    and drops coins on a kill.
- **Homing targets**: Lua sends the module a candidate list of NPCs, players and props with health
  inside the homing radius. The module runs SA1's target choice and rebound.
- **Riding and pushing**: the module reports the object id it stands on, which comes from the
  dynamic objects (3.1), and the object it pushes. Pushing becomes a server-validated
  `ApplyForceCenter`.
- **Physics props bumping into the character**: the player entity's hull already collides with
  them. G64 needed a separate physbox only because Mario was not the player.

### 5.7 Multiplayer networking

- **Every client needs the module and its own disc to see SA1 models.**
  - Network a capability flag (`ply:SetNW2Bool("SA1Active", true)` plus a character id).
  - Clients without the module or disc see the player's normal model (don't hide it), or a
    neutral stand-in. Never stream assets.
- **State packet** (owner → server → others), unreliable, 30 Hz:

| Field | Size |
|---|---|
| position | 3 × float32, or 3 × int24 at 1/8 HU |
| velocity | 3 × int16 |
| rotation x/y/z | BAMS u16 × 3 (SA1 tilts to slopes) |
| character id + action/mode | u8 + u16 |
| animation | anim id A u16, frame A f16/u16, anim id B u16, frame B, blend weight u8, playback speed f16 |
| flags | u32 (ball/spin, invincible, super, shield, holding, hurt) |
| rings | u16 |

  - That is about 40–48 bytes per packet, or about 1.5 KB/s upstream per player. The server
    relays to the PVS (`net.SendPVS(pos)` or `RecipientFilter:AddPVS`). G64 instead used a
    custom view-cone filter. The interval should match SA1's own motion blending.
- **Events**, reliable: damage, attacks, character change, teleport, ring scatter, sound-effect
  triggers (sound ids only).
- **Limits**:
  - a net message holds at most 65,533 bytes;
  - the internal buffer is about 256 KB, and overflowing the reliable channel kicks the client;
  - names must be pooled first with `util.AddNetworkString`;
  - net messages are not reliable to a client until its `InitPostEntity`;
  - unreliable data is dropped first when a packet is full.
- **Remote rendering**: buffer about 100 ms and interpolate between snapshots. The module runs in
  "puppet" mode: motion evaluation from (anim A, frame A, anim B, frame B, blend) with no physics.
  This is the counterpart of the libsm64 fork's `sm64_mario_anim_tick` with a fake Mario. Hide the
  character if no update arrives for more than 1.5 s, as G64 does.
- **Server sanity checks**: clamp the per-packet displacement to (`lim_h_spd` or `lim_v_spd` ×
  60 × S × dt) plus a margin, and rate-limit attacks.
- NetworkVars, if a networked helper entity is ever used, allow 32 slots per type and 4 string
  slots (511 characters each), and transmit by PVS. Packets are more flexible.

---

## 6. Audio

### 6.1 What is on the user's disc (surveyed in `sadc/disc/SONICADV`)

- **ADX** (CRI): 109 `*.ADX` files of music (for example `SONIC.ADX`, which is 44100 Hz stereo
  according to its header), plus `EVENT_ADX.AFS` (73 MB) and `EVENT_ADX_US.AFS` (65 MB). The AFS
  files are archives of cutscene voice ADX files.
- **MLT**: 122 `*.MLT` files. These are Sega "MultiUnit" sound banks for the AICA chip, played by
  `MASARU.DRV`. The likely player and voice banks are `P_SONICTAILS.MLT` and `V_*.MLT`/`V_*_E.MLT`.
  Samples are AICA ADPCM or PCM. The table from SA1 sound-effect ids to bank/program/note, plus
  volume, pan and pitch, lives in `1ST_READ.BIN`; it needs decomp research.
- SFD (video) files are not needed.

### 6.2 Playback paths in GMod

1. **`sound.Generate(identifier, samplerate, duration, data[, loopStart])`** (client):
   - The sample rate must be 11025, 22050 or 44100.
   - Data can be a function, a table of floats in -1..1, or a **string of 16-bit samples**. The
     wiki says "unsigned"; verify whether it means signed PCM16.
   - `loopStart` was added 2024-06-04. Inputs have been validated since November 2025.
   - **Identifiers cannot be overwritten**, so register once per session under stable names.
   - Play it with `surface.PlaySound(id)` (not spatialized since September 2026), with
     `Entity:EmitSound(id, level, pitch, volume, channel)` (assumed to accept generated names;
     verify), or with `CreateSound(ent, id)` for loops and pitch changes.
   - This goes through Source's sound system: 3D, DSP, volume and pause are handled.
   - Probably mono. **Best for sound effects and voices.** 1 s of mono 44.1 kHz is about 88 KB.
2. **`file.Write("sa1/bgm/<name>.wav", bytes)` plus `sound.PlayFile("data/sa1/bgm/<name>.wav", "noplay noblock", cb)`**:
   - Uses the BASS-based `IGModAudioChannel`: Play, Pause, SetVolume, SetPlaybackRate,
     EnableLooping, SetTime and GetTime (`SetTime` needs `noblock`), and SetPos with `3d`.
   - Handles stereo and long files. **Best for music.**
   - ADX loop points: write an intro file and a loop file, or poll `GetTime` and `SetTime`.
   - Known bugs: looping `.wav` in 3D mode fails (issue 1752), and unicode file names fail.
   - `file.Write` also allows `.ogg` and `.mp3`, but there is no encoder in-module; WAV is fine
     and is written only when needed.
3. **Module-owned audio device** (G64 and libsm64: WASAPI, PulseAudio, ALSA or SDL on its own
   pthread): low latency and seamless loops, but it ignores GMod's volume, mute-on-focus-loss,
   pause and 3D unless you rebuild those. Last resort.
- `sound.PlayURL` is not relevant. Never send decoded audio over the network: each client
  decodes its own disc.

### 6.3 Decoding in the module (write clean-room implementations)

- **ADX**:
  - The header has the 0x8000 signature, an offset to the "(c)CRI" string, encoding type 3,
    block size 18, 4 bits per sample, channel count, sample rate, total samples, a high-pass
    cutoff, and loop data in version 3/4 headers.
  - Each channel frame is 18 bytes: a 2-byte big-endian scale followed by 32 4-bit samples.
  - The two-tap predictor coefficients are derived from the cutoff frequency and the sample rate.
  - About 150 lines of code. vgmstream implements it; check its license before reading it.
- **AFS**: a header table of offsets and sizes.
- **AICA ADPCM**: Yamaha 4-bit ADPCM with step adaptation. Resample to 22050 or 44100 for
  `sound.Generate`.
- **MLT**: parse the program and sample banks to find sample data and loop and pitch information.
  Some SA1 sound effects are layered or pitch-swept by the driver; first version: play the base
  sample with SA1's per-sound-effect pitch and volume.

---

## 7. Units and coordinates

### 7.1 Source

- Z is up and the system is right-handed. `Angle(pitch, yaw, roll)`; yaw turns counter-clockwise
  about +Z, from +X toward +Y.
- 1 HU ≈ 1 inch. The standing player hull is 32 × 32 × 72 (36 crouched), with the view at
  64 HU.
- Default gravity is 600 HU/s². The world spans ±16384 HU. The default `sv_maxvelocity` is 3500.

### 7.2 SA1 (Ninja)

- Y is up and the system is right-handed. Angles are BAMS (0x10000 = 360°). The player logic
  steps per frame at 60 Hz. sa1-rs assumes this and it matches SADX; **verify** against the
  decomp's frame-increment variable.
- **Physics parameter table**: at `0x8C1BDFE4` (file offset `0x1ADFE4` in `1ST_READ.BIN`;
  symbol `Sonic_Physics_Data` in `config/symbols.1st_read.txt`). There are 8 entries of
  **0x84 bytes** each: an int `jump2_timer` followed by 32 floats, with `rad`, `height`,
  `weight`, `eyes_height` and `center_height` last. Values read from the user's disc:

| # | Character (assumed SADX order) | rad | height | eyes_height | center_height | jmp_y_spd | run_speed | weight |
|---|---|---|---|---|---|---|---|---|
| 0 | Sonic | 4.0 | 10.0 | 7.0 | 5.4 | 1.66 | 1.39 | 0.08 |
| 1 | Eggman | 8.5 | 18.0 | 7.0 | 5.3 | 1.66 | 1.39 | 0.08 |
| 2 | Tails | 3.5 | 9.0 | 6.0 | 4.5 | 1.66 | 1.39 | 0.08 |
| 3 | Knuckles | 4.0 | 11.4 | 9.0 | 5.7 | 1.66 | 1.39 | 0.08 |
| 4 | Tikal | 4.0 | 10.0 | 7.0 | 5.0 | 1.66 | 1.39 | 0.08 |
| 5 | Amy | 3.5 | 10.0 | 7.0 | 5.0 | 1.30 | 1.39 | 0.04 |
| 6 | Gamma | 4.0 | 21.0 | 20.0 | 14.0 | 2.00 | 2.00 | 0.10 |
| 7 | Big | 9.5 | 17.0 | 15.0 | 8.0 | 2.00 | 1.39 | 0.135 |

  Sonic also has: lim_h_spd 16, lim_v_spd 16, max_x_spd 3, rush_speed 2.3, crash 3.7, dash 5.09,
  jmp_addit 0.076, run_accel 0.05, air_accel 0.031.

  **Two problems in the existing data:**
  - SA Tools `GameConfig/DC_SA1/1st_read.ini` gives the physics entries a **0x108 stride**, so
    every label after Sonic points at the wrong record. "Eggman" is really Tails, "Tails" is
    Tikal, "Knuckles" is Gamma, and "Tikal", "Amy", "Gamma" and "Big" point past the table or
    into other data. As a result, `AdventureRip/figure/{eggman,miles,knuckles,tikal,amy,e102,big}/Physics.ini`
    are mislabeled or garbage; only `sonic/Physics.ini` is right.
  - The `Height=` value in `sadxlvl.ini` (Sonic 5.4, Tails 4.5, Knuckles 5.7, Amy 5) is
    **center_height**, the model origin above the feet, **not** the character's stature. Its
    Gamma=8 and Big=14 appear swapped relative to entries 6 and 7. Bind-pose bounds back
    entry 6 = Gamma: the e102 model origin sits about 13.8 above its lowest vertex.
- **Stature**: Sonic's collision height is 10 and his bind-pose model is about 10.1 tall.
  Canonically Sonic is about 100 cm, which gives **1 SA1 unit ≈ 10 cm ≈ 3.94 HU**.

### 7.3 Proposed scale: S = 4.0 HU per SA1 unit

Use a replicated, archived convar `sa1_scale`, range 1–8.

| Character | Hull radius × height (HU) | Eye (HU) |
|---|---|---|
| Sonic | 16 × 40 | 28 |
| Tails | 14 × 36 | 24 |
| Knuckles | 16 × 45.6 | 36 |
| Amy | 14 × 40 | 28 |
| Gamma | 16 × 84 | 80 |
| Big | 38 × 68 | 60 |

- Sonic's radius of 16 equals the GMod player half-width, so he fits anywhere a player fits.
  Gamma (84 HU) is taller than a 72 HU player but still under the usual 96–128 HU doorways.
- Speeds at 60 Hz × S=4, per second:
  - run 1.39 → 334 HU/s; rush 2.3 → 552; dash 5.09 → 1222;
  - lim_h_spd 16 → **3840 HU/s**, above `sv_maxvelocity` 3500 (see 5.2);
  - jump start 1.66 → 398 HU/s.
  - If `weight` is the per-frame gravity (verify in the decomp): 0.08 → 1152 HU/s², and the
    basic jump apex is about 17 units ≈ 69 HU (a GMod player jumps about 33–56 HU).
- **Axis mapping (proper rotation, no mirroring)**:
  - `src = S · (x, −z, y)` and `sa1 = (src.x, src.z, −src.y) / S`.
  - The matrix rows are `[1,0,0], [0,0,-1], [0,1,0]`, with determinant +1. Winding and
    handedness are preserved.
  - G64 mirrors instead (`(−x, z, y)`) and compensates by reversing triangle order. Avoid that,
    because SA1 code computes cross products and yaw directions.
  - A positive SA1 rotation about +Y maps to a positive Source yaw about +Z. **Verify** SA1's
    facing convention (which axis is "forward" at ang.y = 0) in the decomp before mapping
    character yaw.
  - Normals use the same rotation.
  - Source culls by its own front-face rule. During bring-up, use `$nocull` until the winding
    is confirmed, then flip index order once at mesh build if needed.
- Coordinate range: ±16384 HU is ±4096 SA1 units at S=4, which is comfortable for float32 SA1
  code. Check for any fixed-size grids in the SA1 collision gatherer.

---

## 8. Proposed architecture

### 8.1 Components

1. **`libsa1`**: pure C99, no GMod or Source dependency, testable stand-alone. It is built from
   the decomp plus a port layer.
   - `sa1_disc`:
     - GDI parser. Track 3 starts at LBA 45000 and has 2352-byte raw sectors (Mode 1 user data at
       +16, 2048 bytes); ISO9660 lies in the high-density area.
     - Also accepts an extracted folder.
     - Readers for PRS, PVM/PVR, AFS, ADX and MLT.
     - SHA-1 check of `1ST_READ.BIN`.
     - The US v1.005 address table (models, motions, weld tables, physics table `0x8C1BDFE4`,
       sound-effect tables, sin/cos tables if the code uses them).
   - `sa1_assets`: turns NJS_OBJECT trees, motions, welds, materials and textures into internal
     runtime structures. Nothing is written to disk except caches requested by the host.
   - `sa1_world`: the collision provider. A static triangle BVH, dynamic objects with transforms
     and velocities, actor cylinders, a water query, and attribute flags. Includes a C++-free BSP
     ingest that takes a byte buffer provided by the host, with brushes, displacements and
     static props (`.phy` buffers are also supplied by the host).
   - `sa1_player`: the decompiled player and character code at a fixed 60 Hz.
     - Input: stick angle and magnitude relative to camera yaw, buttons, triggers.
     - Output: position, velocity, rotation, action/mode, flags, rings, animation state.
     - Events: sound-effect ids, effects, attack volumes, damage taken or dealt, death.
     - Commands: teleport, impulse, hurt, set rings, set character.
   - `sa1_anim`: motion evaluation and blending into node matrices; a puppet mode driven by a
     networked animation state.
   - Public header `sa1.h`: an opaque context and handles, plain C structs, no callbacks into
     the host during a step except collision queries, which are served from data the host has
     already uploaded.
2. **`gmcl_sa1`**: C++17, gmod-module-base development branch. Thin bindings, all on the main
   thread. A small worker pool handles disc loading, decoding and BVH building, with results
   delivered through `sa1.Poll()`. One self-contained DLL per platform.
3. **Lua addon** (Workshop, no binaries, no Sega data):
   - `lua/autorun/sa1_init.lua` (shared): net string pooling on the server, convars, version
     check, `util.IsBinaryModuleInstalled("sa1")` then `require("sa1")` on the client.
   - `lua/sa1/cl_disc.lua`: disc selection UI, async load, texture PNG and material creation,
     `sound.Generate` registration, music WAV cache.
   - `lua/sa1/cl_world.lua`: map ingest (BSP bytes through the module's file system access, or
     the fallbacks), entity tracking, the per-tick batched dynamic-object upload, water.
   - `lua/sa1/cl_player.lua`: local character, 60 Hz accumulator in `Think` with interpolation
     alpha, input gathering, CalcView, events to the server.
   - `lua/sa1/cl_render.lua`: the `sa1_render` client-side entity, skinned meshes, lighting,
     blob shadow, HUD.
   - `lua/sa1/sv_player.lua`: movement override (SetupMove/Move/FinishMove), hull and view
     setup, damage forwarding, attack validation, trigger handling, state relay to the PVS.
   - `lua/sa1/sh_net.lua`: packet encode and decode shared by both realms.
   - Spawn-menu tab (character select, settings) and an `sa1_*` console command set.
4. **Server**: Lua only. It needs neither module nor disc.

### 8.2 Lua API of the module (proposed; `sa1` global table, methods on userdata)

- `sa1.API_VERSION` (number) and `sa1.BUILD` (string).
- `sa1.OpenDiscDialog()` → path or nil (Windows only).
- `sa1.LoadDisc(path)` → job id. Progress and the result arrive through `sa1.Poll()` as
  `{type="disc", ok, err, version}`.
- `sa1.Poll()` → array of completed job and event records, drained each Think.
- `sa1.GetCharacterList()` → `{ {id, name}, ... }`.
- `sa1.GetTextures(charId)` → `{ {name, pngBytes, flags}, ... }`. Lua writes the PNGs to `data/`
  and builds materials.
- `sa1.GetMeshData(charId)` → per material group: `{ material = {flags, diffuse, texIndex},
  vertexBytes, vertexCount, boneCount }`.
  - Vertex record: pos 3f in HU in node-local space, normal 3f, uv 2f, bone 2 × u8,
    weight 2 × f.
  - Lua builds `Mesh(mat, 2)` once from this.
- `sa1.GetSounds(set)` → `{ {id, rate, pcm16Bytes, loopStart}, ... }` for `sound.Generate`.
- `sa1.DecodeMusic(name)` → WAV bytes (or a job).
- World:
  - `sa1.WorldBeginMap(mapName, crc)`.
  - `sa1.WorldAddBSP(bytes)`, or `sa1.WorldLoadBSPFromGameFS(path)` on the optional SDK/GMFS
    build.
  - `sa1.WorldAddStaticMesh(bytes, attr)`.
  - `sa1.WorldBuild()` → job.
  - `sa1.WorldSaveCache(path)` and `sa1.WorldLoadCache(bytes)`.
- Dynamic world:
  - `sa1.ObjCreate(trianglesBytes, attr)` → objId.
  - `sa1.ObjUpdateBatch(bytes)`, with records (objId, pos, ang, linVel, angVel, enabled).
  - `sa1.ObjDelete(id)`.
  - `sa1.ActorsUpdateBatch(bytes)` for cylinders.
- `sa1.CreateCharacter(charId, pos, yaw, puppet)` → `SA1Char` userdata with `__gc`. Methods:
  - `Step(dtSeconds, inputTable)`: runs 0..n fixed 60 Hz substeps and returns the number of
    steps.
  - `GetState(outTable)`: fills a reused table with pos, vel, ang, action, flags, rings, anim and
    events.
  - `GetBones(matrixTable, alpha)`: writes the interpolated node matrices into the reused
    `Matrix` objects.
  - `ApplyPuppetState(packetBytes)`: for remote characters.
  - `SetPos(pos)`, `Teleport(pos, yaw)`, `Hurt(amount, srcPos)`, `Impulse(vel)`, `SetRings(n)`,
    `SetWaterLevel(z)`.
  - `EncodeNetState()` → bytes and `Destroy()`.
- `sa1.SetScale(S)`.
- All vector inputs and outputs are in Source space and HU. The module owns the axis and scale
  conversion.

### 8.3 Per-frame flow (local player, client)

1. `Think`:
   - `sa1.Poll()`;
   - gather input;
   - `ObjUpdateBatch` and `ActorsUpdateBatch` for entities near the character;
   - `char:Step(RealFrameTime(), input)`, skipped when `FrameTime() == 0`, scaled by
     `host_timescale` (G64 handles timescale too);
   - read state;
   - play the sound-effect events with `EmitSound`;
   - send the state packet every second step (30 Hz) and events at once.
2. `CalcView`: chase camera from the interpolated position.
3. `sa1_render:Draw()` and `DrawTranslucent()`:
   - `GetBones` with the interpolation alpha;
   - lighting setup;
   - `DrawSkinned` for each material group;
   - `RenderFlashlights`;
   - blob shadow in an opaque hook.
4. Server: `SetupMove`, `Move` and `FinishMove` place the player. `EntityTakeDamage` blocks and
   forwards damage. Packets are relayed to the PVS.

### 8.4 Phases

1. **P0**: module skeleton, disc load and SHA-1 check, PVR → PNG, static bind-pose Sonic drawn in
   GMod with `DrawSkinned` (bones = bind pose).
2. **P1**: motion playback with welds; material flags; lighting; puppet mode on a test entity.
3. **P2**: Sonic's physics on BSP collision in singleplayer; camera; input; dynamic props.
4. **P3**: multiplayer relay; damage and attacks; triggers and water.
5. **P4**: other characters; sound effects and voices (MLT) and music (ADX); HUD.
6. **P5**: optional generated-MDL path (shadows, player model); optional SDK fast paths; Linux.

---

## 9. Open questions

1. Is `IMesh:DrawSkinned` available on the 32-bit main-branch build? It shipped in the April 2026
   update; it is assumed to be on all branches. Is the 52-bone limit per call enough once
   held-item nodes are added (Amy's hammer, Big's rod, Gamma's parts)?
2. Does `GetUserType<VMatrix>(i, Type::Matrix)` return writable storage of the Lua `Matrix`, so
   bones can be written in place? Is `Matrix:SetUnpacked` fast enough otherwise?
3. Do materials accept `$basetexture "../data/..."` for VTF files written into `data/`? If so,
   that is an alternative to PNG.
4. `sound.Generate` with string data: signed or unsigned 16-bit? Mono only? Does
   `Entity:EmitSound` accept generated identifiers, and does it spatialize them?
5. Does `ply:SetPos` on the server fire trigger touches, or must movement go through the move
   hooks? (G64 rebuilt `trigger_teleport` itself.) With client-authoritative movement through
   the move hooks, how large are prediction errors in practice, and is the `CUserCmd`-carried
   state trick viable (are forward/side/up move floats sent at full precision and unclamped)?
6. Is the GMod `sprp` (static prop lump) version covered by source-parsers (v4–v7 "MP2013"), or
   are v10/v11 needed? How common are LZMA-compressed lumps in Workshop maps?
7. Is the world `PhysObj` ever valid on the client in current builds (the search answer says
   no)? If so, the server streaming fallback could be dropped.
8. Is a binary module's DLL unloaded on map change, or only its Lua state? (Design assumes
   neither and re-initialises.)
9. SA1 logic tick rate on DC: is it 60 Hz for all player code, or does some code use a frame
   increment (30 Hz modes)? Is `weight` the per-frame gravity? Which axis is "forward" at yaw 0?
   (These are needed for the scale, speed and axis tables above.)
10. Physics table identification: is the order of entries 0–7 Sonic, Eggman, Tails, Knuckles,
    Tikal, Amy, Gamma, Big? Fix the SA Tools `DC_SA1/1st_read.ini` stride (0x108 → 0x84) and
    regenerate the AdventureRip `Physics.ini` files.
11. MLT format and the sound-effect id → bank/program table in `1ST_READ.BIN` (which banks hold
    the player sound effects, `P_SONICTAILS.MLT`?), and how much of the MASARU driver behaviour
    (pitch sweeps, layers) needs emulating.
12. Environment-mapped SA1 materials on characters: which submeshes use USE_ENV? Is a cubemap
    approximation acceptable, or are CPU-computed sphere-map UVs needed?
13. Legal posture: the module ships decompiled game logic (as libsm64 ships SM64 decomp code)
    while all data comes from the user's disc. The project owner should decide the release
    model (source-only vs binaries).
