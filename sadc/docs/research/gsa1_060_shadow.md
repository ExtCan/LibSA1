# G-SA1 0.6.0 diagnosis: "Characters have no shadows"

Topic `shadow` of the 0.6.0 diagnosis (owner test of 2026-10-21 on gm_bigcity, `gsa1_rounds_plan.md` 14.1c item 2).
Read-only diagnosis: nothing in SADC/src, SADC/include, LIB or g-sa1 was changed. Experiments are in
`SADC/build/research/shadow/` (section 9). Guesses are marked UNVERIFIED.

## 0. Answer

* **The game's shadow is game logic, and libsa1 already runs it.** Every character main calls the character shadow
  `8C0925A0` once per 60 Hz step. It finds the ground under the character in the land collision, stores that ground in
  `playerwk.shadow`, and draws the blob `Character_Shadow_Blob` (0x8C1ACA78) there with `8C09247E`. E-102 uses
  `8C09222C` instead. The blob is a flat 10 x 10 quad textured with OBJ_REGULAR texture 58, `shadow`. It is tilted to
  the slope, turned with the character, shrunk with the height above the ground, and drawn 0.3 units above the ground.
  In libsa1 worlds this code runs against the host's collision (GRID from the GMod map), and the display capture
  records the draw on sub step 0 of every pair. This is measured for all six characters (section 2).
* **The draw is captured, then removed, so G-SA1 never sees it.** libsa1 removes it from the render output on purpose:
  `src/sa1_character.c:1052`, `o.exclude_model = sa1_capture_shadow_model(ch->cap)`, with the comment "hosts draw their
  own shadows". G-SA1 has never drawn a shadow of its own. `g-sa1/README.md:1069` lists "a blob shadow" under "Not done
  yet". Both the character entity and the hidden player call `DrawShadow(false)`
  (`cl_render.lua:29`, `sv_gsa1.lua:54`).
* **Turning the exclusion off is not enough.** Two more gaps make the captured draw unusable as it is:
  1. **The texture setup is a stub.** `8C099500` sets the texlist before each blob. In libsa1 it is a boundary stub
     (`gen/policy/stubs.tsv:152`, "ret 1"), and the stage-start variable it reads, `B_8C79F0F0`, is 0 in libsa1 worlds.
     So the blob is drawn with the character's own texlist. Texture id 58 is past the end of every character texlist
     (22 to 53 textures).
  2. **OBJ_REGULAR is not loaded.** libsa1 loads only EFF_REGULAR from the common archives (`src/sa1_loader.c:371-389`).

  Measured: with the exclusion simply removed, every character gets an **untextured light-grey square** at 80 % opacity
  (flags BLEND|NO_TEXTURE, colour 178/178/178/204).
* **Fix (faithful, section 4):** libsa1 makes the texture setup the game's own and loads OBJ_REGULAR. It then emits the
  blob as a normal draw marked with a new flag `SA1_DRAW_SHADOW`, instead of leaving it out. G-SA1 draws that batch with
  its own Q menu toggle, separate from "Effects".
* **Render-target alpha (0.6.0 on the owner's DXVK):** the blob's shape is entirely in the texture's alpha. Its RGB is
  black everywhere. Through the normal translucent path it would show as a dark square on the owner's machine. Two ways
  out:
  * Draw the blob in G-SA1 as a multiply of the alpha. This is exact for a black texture under SA/ISA blending, and it
    needs no alpha in the render target.
  * Land the alpha fix of topic `alpha` first.
* **No projected-texture or engine shadow.** The game's own blob, captured and drawn where the game put it, is the
  faithful answer. GMod's projected textures, render-to-texture or blob shadows would replace game logic, or look and
  behave differently (section 4.4).

## 1. How SA1 draws the player characters' shadows (Dreamcast, US 1.005)

### 1.1 Code

| Address | Function (SADC source) | What it does |
|---|---|---|
| `8C0925A0` | character shadow (`src/1st_read/code_8C0925A0.c:26`) | It gathers the land objects within 200 of `twp->pos` with `8C0522C0` (line 57), then probes each one from 1 unit above the character. It runs the lower and upper probe `8C02D2E0` on solid objects, and `8C02CA00` on water (`slAttribute & 0x400002`). It keeps the highest ground at or below the probe point in `shadow->y_bottom`, `Attr_bottom`, `angx` and `angz`. It keeps the lowest surface above in `y_top` and `Attr_top`; that is also the player's water probe and splash trigger (`8C07B346` / `8C07B3DA`). Moving land is lifted by its `forcewk.pos_spd.y`. Then it calls `8C09247E` (line 105). |
| `8C09247E` | blob from the shadow work (`code_8C092124.c:131`) | It returns on sub step `B_8C754EC8 != 0`, and when no ground was found (`y_bottom <= pos.y - 20000`). Otherwise it computes `s = scl / (0.03 * |pos.y - y_bottom + 1| + 1)`. Then it calls `8C099500` (texture setup) and `njPushMatrix`, `njTranslate(pos.x, y_bottom + 0.3, pos.z)`, `njRotateZ(angz)`, `njRotateX(angx)`, `njRotateY(-ang.y)`, `njScale(s, s, 0.8 s)`, `njDrawObject(Character_Shadow_Blob)`. |
| `8C09222C` | probe and blob (`code_8C092124.c:68`) | It probes from 4 above the position (`8C091C20`, gather radius 10), then draws at `y + 0.3` with `r / (0.03 * |pos.y - y| + 1)`, tilted and turned by `-ang.y`. It returns the ground height. **E-102** uses it (`code_8C0CBE00.c:745`, `w->ground_y = func_8C09222C(twp, 1.5f)`; also his display, `code_8C0CFACE.c:381`). Other users: the animals (`code_8C017360.c`, r 0.35), Amy's bird (`code_8C0DE8C0.c`, r 0.4) and E-100 Zero (`code_8C1032AC.c`, r 1.8). |
| `8C092124`, `8C0922EE`, `8C0923C0` | blob variants (same file) | Dropped and scattered rings (`code_8C0869E0.c:202`, at `y + 0.2`) and event work (`code_8C04BC70.c:177`). |
| `8C091C20` | ground under a point (`code_8C091C20.c:43`) | NON_MATCHING (register allocation only). It returns the highest ground at or below y, or -1000000. |
| `8C02D060` / `8C02D2E0` | ground probes against one land object (`code_8C02D060.c`, hand-written SH-4, described in C) | It walks the object's polygons sorted by z, and keeps a polygon only if `|centre - p|^2 < rr + 900` (`D_8C02D960 = 900.0f`, lines 184/217/392/422). Then it tests whether the point lies inside the triangle seen from above, and computes the plane height at that point. |
| `8C099500` | blob texture setup (Ghidra `build/ghidra/1ST_READ/decomp/8C099500.c`, 0x26 bytes) | `if (B_8C79F0F0) { njSetTexture(B_8C79F0F0); return 1; } return 0;` (`njSetTexture` = `8C608CCC`). |
| `8C0990DE` | stage texlists (Ghidra) | Called by the stage start `8C04E490` with mode 0, or mode 2 for levels 0x24 and 0x25. It stores the texlist of entry 0 of `PTR_8C1C24A8[mode]` into `B_8C79F0F0`. For mode 0 that is `{ "OBJ_REGULAR", 0x8C19F530 }` (96 textures, list 0x8C1C2450). In every ordinary stage the blob is therefore drawn with OBJ_REGULAR. |

Per character:

| Character | Exec call (60 Hz) | Paused-only draw (display, `if (func_8C04FE40())` = game mode 0x10) | Size factor |
|---|---|---|---|
| Sonic | `code_8C0A77BC.c:967` SonicMain -> `8C0925A0` | `code_8C0AE0E0.c:153` | `scl = p.rad * 0.2` = 0.8 |
| Tails | `code_8C0B6148.c:616` | `code_8C0BA820.c:269` | 0.7 |
| Knuckles | `code_8C0C0640.c:646` | `code_8C0C46AC.c:191` | 0.8 |
| Amy | `code_8C0D6B40.c:503` | `code_8C0DA106.c:197` | 0.7 |
| E-102 | `code_8C0CBE00.c:745` -> `8C09222C(twp, 1.5)` | `code_8C0CFACE.c:381` | r = 1.5 (fixed) |
| Big | `code_8C0DFFA0.c:583` | `code_8C0E386E.c:120` | 1.9 |
| (Tikal) | `src/overlays/tikal_prog/code_0CB00000.c:211` | - | 0.8 |
| (Super Sonic) | Sonic's code | Sonic's | 0.8 |

`scl` is set by PInitialize, `code_8C06C300.c:80`: `pwp->shadow.scl = pwp->p.rad * 0.2f`. The `rad` values come from the
physics table at 0x8C1BDFE4 (read from 1ST_READ.BIN: Sonic 4.0, Tails 3.5, Knuckles 4.0, Amy 3.5, E-102 4.0, Big 9.5).
The shadow work is `playerwk.shadow` at pwp+0x1DC, size 0x24 (`include/sa1/player.h:89-101`).

### 1.2 The blob (1ST_READ data, read from `sadc/baserom/1ST_READ.BIN`, script `build/research/shadow/blob.py`)

* `Character_Shadow_Blob` 0x8C1ACA78: evalflags 0x17, model 0x8C1ACA50. The model has 4 points (+-5, 0, +-5), all with
  normal (0, 1, 0), and r 7.07. It has one meshset of type 0 (triangles), indices 0 1 3 / 0 3 2, which faces +Y when
  seen from above. The UVs run from 7 to 247 of 256.
* Its one material: diffuse **0xCCB2B2B2** (alpha 204), attribute **0x94318400**. That is source factor SA, destination
  factor ISA, USE_TEXTURE, USE_ALPHA, CLAMP_U, CLAMP_V, lit, texture id **0x3A = 58**.
* OBJ_REGULAR texture 58 (read from the owner's disc with LIB's independent decoder `tests/texdump_ref.py`, script
  `build/research/shadow/shadow_tex.py`) is named **`shadow`**. It is 16 x 16, ARGB4444, twiddled with mipmaps. **The RGB
  is (0, 0, 0) in every texel.** The alpha is a disc: 255 in the core, 221 and 17 at the rim, 0 outside (7 distinct
  values). Ninja's material rule makes the result black with alpha `tex.a x 204/255`: a soft disc at 80 % darkness.
  UNVERIFIED on hardware; this is the colour rule libsa1 already applies to `njDrawObject` draws.
* The shape on the ground is 10s along the character's facing and 8s across, since the local x axis follows `-ang.y`.
  At rest (`s = scl / 1.03`) the sizes are:

  | Character | SA1 units | Hammer units (x4) |
  |---|---|---|
  | Sonic, Knuckles | 7.77 x 6.21 | 31 x 25 |
  | Tails, Amy | 6.8 x 5.4 | 27 x 22 |
  | Big | 18.4 x 14.8 | 74 x 59 |
  | E-102 | 15 x 12 | 60 x 48 |

### 1.3 Behaviour that follows from the code (all game logic)

* **The shadow shrinks with height:** `s / (0.03 h + 1)`. At 18.7 units above the ground Sonic's blob is 0.65 of its
  size at rest.
* **The shadow tilts with the ground under the centre.** The quad is planar, so it does not bend over edges or steps.
* **The shadow disappears when the ground is out of the probe's reach.** That happens beyond about `sqrt(R^2 + 30^2)`
  from a ground polygon's bounding-sphere centre (R = the polygon's radius), because of the `+ 900` in `8C02D060` /
  `8C02D2E0`. In the measured scene the gather radius of 200 (10 for E-102) and the 20000 test never limited it first.
  On the Dreamcast the
  stage's polygon sizes set this reach. In libsa1 the GRID pieces of the GMod map set it: edges up to 128 SA1 units, so
  roughly 30-80 SA1 units, or 120-320 Hammer units, above the floor.
* **E-102's blob is never dropped.** When nothing is found he still draws the blob at y = -1000000 with a scale of
  about 0. libsa1's clip radius (`SA1_MESH_CLIP_RADIUS` 16384) already leaves that draw out.
* **One frame in two:** only sub step 0 of each 30 Hz pair draws, so the blob shares the step of the drawn body.

## 2. What libsa1 0.2.0 / G-SA1 0.6.0 do with it (reproduced)

Test: `SADC/build/research/shadow/shadow_probe.c`, built by `build_shadow.py` against a locked snapshot of
`LIB/build/x86_64-windows/libsa1.a` (2026-10-21 10:04). It uses NATIVE coordinates and GRID static collision through
`sa1_collision_set_static`: a floor of 100-unit quads at y = 0, a 20-degree ramp onto a plateau, and a block with its
top at 40. The six ticking characters each run 6 starts. After every tick the test reads:

* the capture records whose model is the blob's,
* `playerwk.shadow` from the arena,
* `sa1_state_ex`,
* the capture's emit statistics.

Output: `probe_run2.txt` (summaries) and `probe_run3.txt` (naive un-exclude).

| Measurement | Result |
|---|---|
| Blob draw captured | Sonic, Tails, Knuckles, Amy, Big: 34 of 35 pairs on the floor, and the same on the ramp and the block; E-102 every pair. The first pair after creation has none, although its shadow work has the ground (cause UNVERIFIED; one 33 ms frame at spawn). |
| Record | kind 10 (`njDrawObject` node), sub step 0, **texlist = the character's own**: Sonic 0x8C1C6C94 (28 textures), Tails 0x8C257C54 (24), Knuckles 0x8C3537D8 (22), Amy 0x8C4BF3AC (53), E-102 0x8C423454 (35), Big 0x8C525FB0 (36). Not OBJ_REGULAR 0x8C19F530; `B_8C79F0F0` = 0 in the world. |
| Placement and size, standing (centre = translation of the draw's world matrix) | Centre (pos.x, y_bottom + 0.300, pos.z). Scale x/z: Sonic and Knuckles 0.7767 / 0.6214, Tails 0.6638, Amy 0.6645, Big 1.8448 / 1.4758, E-102 1.4999 / 1.1999. These equal the game's formula (`0.8 / 1.03` and so on; Tails and Amy stand 0.8 above this floor, see 8). |
| Ramp (20 degrees) | `angz` 0x0E38 and the quad's normal tilted **20.00 degrees** for all six. |
| Jump (Sonic) | Scale 0.7767 -> 0.7429 -> 0.6852 -> 0.6380 -> 0.5470 -> 0.5013 at heights 0 -> 18.7, and back to 0.7767 on landing. |
| Reach (dropped from 150 / 400 above the floor) | No ground found at 104.9 / 91.1 above; found from 72.2 / 68.0 above (Sonic / Amy). Consistent with `sqrt(70.7^2 + 30^2)` = 76.8 for these triangles (circumradius 70.7). E-102 at 143.6 above: draw at y = -999999.6, scale 0. |
| Render output | `sa1_capture_stats.excluded` = 1 every pair: the blob is never in `sa1_character_render`, so G-SA1 never gets it. |
| Naive un-exclude (`naive_unexclude()`: same options as `render_capture` with `exclude_model = 0`) | One more draw per character: 2 triangles, archive -1, texture -1, material 0x9431A400, flags 0x0081 (BLEND, NO_TEXTURE), blend 4/5, vertex colour 178 178 178 204. An **untextured grey square**. |

Also checked: G-SA1 has no other shadow path. `grep -i shadow` in g-sa1 finds only `DrawShadow(false)` on the render
entity and the player, `DrawShadow(true)` on leaving, and the README backlog line.

## 3. Causes

* **C1. libsa1 removes the blob on purpose.** `libsa1/src/sa1_character.c:1052` (`render_capture`) sets
  `o.exclude_model = sa1_capture_shadow_model(ch->cap)`, and `sa1_capture.c:1078` drops every draw of that model. The
  design comment is at `sa1_character.c:959` and `sa1_capture.h:83-86`.
* **C2. G-SA1 never implemented the "host draws its own shadow" side.** See `README.md:1069`.
* **C3. The blob's texture setup is a boundary stub.** `libsa1/gen/policy/stubs.tsv:152` makes `8C099500` "ret 1",
  which never calls `njSetTexture`, and the world boot never sets `B_8C79F0F0` (the stage-start texture setup `8C0990DE`
  does not run in libsa1). The record therefore carries whatever texlist the display left: the character's own, where
  id 58 is past the end.
* **C4. The blob's archive is not loaded.** OBJ_REGULAR is not in `load_common_textures`, `src/sa1_loader.c:377`,
  whose `want[]` lists only EFF_REGULAR.
* **C5 (G-SA1 0.6.0 on the owner's machine).** The render-target alpha probe failed: "BGRA8888 read back 96 where 96 was
  painted, 255 where 0" (log, 09:47:25). A texture whose shape is only in its alpha would be drawn as a square. This is
  the same root as topic `alpha` (Knuckles' particles).

## 4. Design: the game's own shadow, captured and drawn in GMod

### 4.1 libsa1 (game logic stays the game's; only the host boundary changes)

**L1. The texture setup becomes the game's.**

* SADC decompiles `8C099500` (0x26 bytes). Its `8C099538` twin (`B_8C79F0F4`, stub line 153) can come along. Expected C:

  ```c
  Sint32 func_8C099500(void) { if (B_8C79F0F0) { njSetTexture(B_8C79F0F0); return 1; } return 0; }
  ```

  It goes in a new `src/1st_read/code_8C099500.c` or in the chunk file of 8C098EE0. Check it with
  `py -3.12 tools/shcdiff.py func_8C099500 ...` (MATCH expected).
* LIB removes `stubs.tsv:152`, so the routed C runs (`njSetTexture` is already `rt_texture.c:57`).
* The world boot (`src/world/sa1_world_boot.c`) stores what `8C0990DE(0)` stores at a stage start:
  `B_8C79F0F0 = (entry 0 of SA1_ADDR_PVM_COMMON).list` = 0x8C19F530. It reads that from the image, the same list
  `sa1_loader.c` reads. It does so for levels other than 0x24 and 0x25, which are mode 2: EFF_REGULAR first.

Effect: the record's texlist is OBJ_REGULAR. Like the original, `D_8C673480` then stays OBJ_REGULAR after the blob.
Gate: the 10 strict replays (`py -3.12 build.py --test`) must stay bit-exact. The harness references ran the real
`8C099500` with the real `B_8C79F0F0` (UNVERIFIED that no compared byte depends on `D_8C673480`; the replays will
tell).

**L2. Load OBJ_REGULAR as a common archive.** Add "OBJ_REGULAR" to `want[]` in `load_common_textures`
(`sa1_loader.c:377`), with `texlist_addr` 0x8C19F530. `sa1_capture_resolve` then maps texlist 0x8C19F530 / id 58 to that
archive, texture 58, `shadow`.

Cost: 96 more textures per character set (PRS 155,785 bytes; textures 16x16 to 64x64). G-SA1's `T.Prepare` lists every
texture of every archive for background painting. Either keep that, or make G-SA1 paint only what batches ask for
(G3). Cheaper alternative, UNVERIFIED feasibility in `load_archive`: register the archive with all 96 names but decode
only texture `shadow`, leaving the others 0 x 0.

**L3. Mark the blob instead of removing it.**

* New draw flag in `include/libsa1.h` (render draft):

  ```c
  #define SA1_DRAW_SHADOW 0x0800u   /* the game's blob shadow (Character_Shadow_Blob: 8C09247E, 8C09222C, 8C092124 ...):
                                       a flat quad the game put 0.3 units above the ground it found under the character */
  ```

* In `sa1_capture_emit_opts`, replace `exclude_model` by `shadow_model` (mark, do not drop), or add the latter.
  `sa1_capture_emit_into` (`sa1_capture.c:1078`) then sets `ei.extra_flags |= SA1_DRAW_SHADOW` for that model and does
  not set `SA1_DRAW_EFFECT` on it. The texture resolves through `ei.resolve`, as the effect archives already do.
* `render_capture` (`sa1_character.c:1052`) passes `shadow_model`.
* The draw arrives with archive OBJ_REGULAR, texture 58, BLEND (SA/ISA), the diffuse alpha 204 in its colours, and is
  interpolated between pairs like every other drawn model: the same model is drawn every pair, so the transform is
  interpolated, not only the vertices.
* Rings dropped on damage (round 5) draw the same model with `njDrawModel`, so their shadows come along.
* The clip radius stays, which leaves out E-102's parked draw.

**L4. Hidden pairs keep the blob.** When the game's camera does not see the character, `render_capture` switches to the
CPU pose (`sa1_character.c`, `sa1_capture_hidden`). The blob does not ask the visibility test, so it is still in the
capture. Append it to the CPU-pose output: the body stays drawn, and so should its shadow.

**L5. Documentation.**

* `sa1_capture.h:83-86` and the comment at `sa1_character.c:959`: drawn and flagged, no longer "hosts draw their own".
* `libsa1.h`: the `SA1_DRAW_SHADOW` comment.
* `sadc/docs/libsa1_integration.md` 7.2: the render output now carries the shadow.

### 4.2 G-SA1

**G1. Batches** (`src/core/session.cpp`, `session.hpp:68`).

* Add `SA1_DRAW_SHADOW` to `kBatchKeyFlags`, so shadow draws get their own batch and key. They are blended, so pass 1,
  in the game's order (after the body, as in the exec).
* Bind `DRAW_SHADOW` next to the other `DRAW_*` constants (`gsa1_module.cpp:2386-2396`).

**G2. Drawing** (`addon/lua/gsa1/cl_render.lua`, `DrawCharacter`).

* A shadow batch is drawn when `gsa1_shadow` is 1, whatever `gsa1_effects` says. Today every batch from an archive
  other than 0 is gated by `DRAW_EFFECT`, and the blob must not be.
* Optionally it is also drawn in first person (owner question Q2). Today `first_person` skips every batch.
* The 0.3-unit lift is 1.2 Hammer units. On flat ground that clears depth fighting at the distances characters are seen
  (UNVERIFIED in-game on DXVK). If the owner sees flicker at a distance, add `$decal 1` to the shadow material (Source's
  decal polygon offset, UNVERIFIED for mesh draws) or `render.DepthRange(0, 0.9999)` around the batch.

**G3. The material: draw the blob as a multiply of its alpha (recommended; independent of C5)** (`cl_textures.lua`).

* For a SA/ISA draw whose texture RGB is black in every texel (texture 58 is; check it once from the run-length data
  `gsa1.Texture` already returns), the game's result is `dst x (1 - a x tex.a)` with a = 204/255.
* Draw exactly that:
  * Paint a mask render target with RGB = the texel's alpha. A new address-mode bit, e.g. `WRAP_MASK = 8`, in
    `T.Get`/`paint_some`: `setcolor(ca, ca, ca, 255)`.
  * Use the material UnlitGeneric with `$translucent 1` (no depth writes), `$vertexcolor 1` and `$nofog 1`.
  * Draw with `render.OverrideBlend(true, BLEND_ZERO, BLEND_ONE_MINUS_SRC_COLOR, BLENDFUNC_ADD)`.
  * The module writes the vertex colour of SHADOW batches as `(A, A, A, 255)` from the draw's alpha. Alternatively Lua
    calls `render.SetColorModulation(a, a, a)` with `$vertexcolor 0`, and the batch table carries the diffuse alpha.
* This path needs no alpha in the render target, which the owner's 0.6.0 setup lost.
* The ordinary path stays correct: a translucent material with `$vertexalpha` and SA/ISA. It applies when the texture's
  RGB is not uniform, and on machines where the alpha probe passes.
* Add the shadow state to the pre-warm (`cl_warm.lua`, `cl_warm_census.lua`) so its material is made with the
  character's other materials.
* UNVERIFIED: the mask values pass through the same colour path as the character textures; the core (alpha 255) is
  exact, and the rim values (221, 17) may shift slightly with Source's colour handling.

**G4. Option.**

* `gsa1_shadow` (default 1) in `sh_config.lua`.
* Q menu > Options > G-SA1 > Rendering: "Character shadows (the game's blob)" in `cl_menu.lua`, with a help line
  ("drawn where the game finds the ground under the character; it shrinks with height and disappears high up, as in the
  game").
* If the owner wants Q2 or Q3, their checkboxes go next to it.

**G5. Log and status.** The session log's character line gains `shadow draws N` (from the batch flags), so a missing
shadow shows up in the owner's next log.

**G6. README.** Remove "a blob shadow" from "Not done yet" and add the option and a checklist item.

### 4.3 Known differences from the Dreamcast that stay (host geometry, not logic)

* **The blob lies on the collision surface, not on the drawn one.** On brushes and displacements the two are the same.
* **On stairs,** G-SA1's collision turns runs of steps into ramps (`bsp.cpp`), so the blob follows the ramp: it floats
  over the lower treads and is cut by the next riser.
* **On static props** the physics hull can differ from the drawn mesh by a few units.
* **The reach depends on the size of the GRID pieces** (1.3). A host-side option could conform the quad to the drawn
  ground: split it 4 x 4 and trace each vertex down with `util.TraceLine` to the visible surface plus 1.2 HU, about 25
  traces per character per frame. That is rendering only, and default off; owner question Q3.

### 4.4 Alternatives considered (not recommended)

| Alternative | Why not |
|---|---|
| GMod / Source render-to-texture shadow (G64's trick: give the entity a sphere model, `DrawShadow(true)`; `research/g64.md:418`) | It is the engine's shadow, not the game's. It is projected along the map's `shadow_control` direction (usually slanted), blurred, and round or silhouette-shaped, with no shrink by height and no slope tilt. It costs one render-to-texture shadow per character. Our IMesh draws do not cast it (`gmod.md` 4.7). It would double up with the game's blob. Possible later as an extra "GMod-style shadow" option, never instead of the blob. |
| `ProjectedTexture` | A dynamic light: it can only add light, so a black texture projects nothing. It is expensive (a shadow depth map per light, few lights at once). |
| `util.DecalEx` and decals | Permanent and accumulating; nothing to update per frame. |
| A host-made blob (G-SA1 traces down and draws `render.DrawQuadEasy`) | It replaces the game's ground search, slope, size by height, reach and orientation with GMod logic, against the owner's rule. Acceptable only for puppets that have no world (future multiplayer relay), which could instead relay the state's `y_bottom`, `angx`, `angz` and scale. |

## 5. Tests (fail before, pass after)

**T1. LIB, new `tests/shadow_test.c` + `tests/build_shadow.py`.**

* Scene: the scene of `build/research/shadow/shadow_probe.c` (copy it): NATIVE coordinates, a GRID floor of 100-unit
  quads, a 20-degree ramp, a 40-unit block. Starts off vertices and diagonals (13.7, 7.3).
* Characters: Sonic, Tails, Knuckles, Amy, E-102, Big.
* Assertions:

  | Id | Check | Before |
  |---|---|---|
  | S1 | After 12 pairs standing, `sa1_character_render(alpha 1)` holds exactly 1 draw with `SA1_DRAW_SHADOW`: TRIANGLES with count 2, source MODEL, an archive named OBJ_REGULAR (`sa1_texture_archive`), texture 58 named `shadow` (`sa1_texture_get`), BLEND without EFFECT and without NO_TEXTURE, blend 4/5, corner alpha 204. | FAILS: 0 shadow draws, `excluded` = 1. |
  | S2 | Corners are coplanar. The centre is (pos.x, y_bottom + 0.3, pos.z) of sub step 0. Extents are 10s by 8s, with s as measured in section 2 (+-1e-3): 0.7767, 0.6638, 0.7767, 0.6645, 1.4999, 1.8448. | |
  | S3 | On the ramp, the normal is tilted 20.0 +- 0.1 degrees for all six. | |
  | S4 | During Sonic's jump, s goes down monotonically while rising, then back to 0.7767. | |
  | S5 | Dropped from 150: no shadow draw above 105, shadow at or below 72. E-102's parked draw never appears. | |
  | S6 | Zero screen, as in `capture_test` part 9: hidden pairs still carry the shadow draw. | |
  | S7 | `alpha = 0.5` during a jump: the shadow's centre height and scale lie between those of the two pairs. | |

**T2. LIB strict replays.** `py -3.12 build.py --test` (10/10 bit-exact) with 8C099500 routed and `B_8C79F0F0` set.
`tests/capture_test.c` parts 2/3 count the blob draws "aside" (`capture_test.c:627-639`). Update them to expect the
flag, not the absence.

**T3. SADC.** `tools/shcdiff.py func_8C099500` is a MATCH. The full build still says `1ST_READ.ok`.

**T4. G-SA1 core and binding.**

* `tests/core_test.cpp`: a character on a flat floor gives a pass-1 batch whose flags contain `SA1_DRAW_SHADOW`, with
  its own key and 2 triangles. With the mask path the emitted colours of that batch are (204, 204, 204, 255).
* `tests/binding_test.cpp`: `DRAW_SHADOW` is exported.

  Before: no such batch.

**T5. G-SA1 Lua** (`tests/lua_mock.lua`, `frame_test.lua`, `warm_test.lua`).

* `DrawCharacter` draws the shadow batch with `gsa1_shadow` 1 and `gsa1_effects` 0, and does not with `gsa1_shadow` 0.
* The shadow material uses the mask path when the texture's RGB is all black.
* The pre-warm census contains the shadow state.

**T6. Owner checklist (in game).** Each of the six characters has a dark soft oval under the feet:

* not a square, not grey;
* longer along the facing;
* shrinking while jumping, tilted on ramps;
* gone high above the ground;
* toggled by the new option, independent of "Effects".

## 6. Ownership and scheduling

Wave B currently owns the files involved, so all of this goes to the next fix round after wave B (call it "0.6 fixes").

| Work | Files | Suggested owner | After |
|---|---|---|---|
| S: decompile 8C099500 (+8C099538) | SADC `src/1st_read/` (new file), `config/` symbols if needed | a decompilation agent of the fix round, e.g. `060-sadc` | `l0-land` (it owns the SADC landing in wave B) |
| L1-L5 | LIB `gen/policy/stubs.tsv` (line 152), `src/world/sa1_world_boot.c` (p), `src/sa1_loader.c` (p), `src/sa1_capture.{c,h}`, `src/sa1_character.c` (p), `include/libsa1.h` (p), `tests/shadow_test.c` + `build_shadow.py` (new), `tests/capture_test.c` (p) | `060-lib-render`, which can be the same agent as topic `alpha`'s LIB part if there is one | `l0-land` (stubs.tsv), `5-lib-core` (`sa1_world_boot.c` / `sa1_character.c` patches), S |
| G1-G6 | g-sa1 `src/core/session.{hpp,cpp}` (p), `src/module/gsa1_module.cpp` (p), `addon/lua/gsa1/cl_render.lua`, `cl_textures.lua`, `cl_warm.lua`, `cl_warm_census.lua`, `sh_config.lua` (p), `cl_menu.lua` (p), `cl_log.lua` (p), `README.md` (p), tests | `060-gsa1-render`, the same agent as topic `alpha`'s G-SA1 part, since both touch `cl_textures.lua`'s painting | `g0-seam` (it owns `session.cpp`, `sh_config.lua`, `cl_menu.lua` in wave B), L3's API |
| Verification | T1-T6 | the fix round's independent verifier | all of the above |

## 7. Owner questions (defaults in brackets)

* **Q1.** The shadow vanishes when the character is high above the ground, as in the game. Keep that? [yes: game logic]
* **Q2.** Show the shadow in first person (the body is hidden there)? [no: like the body; an option if wanted]
* **Q3.** On stairs and uneven ground, should the shadow hug the drawn ground (a GMod-side option), or stay the game's
  flat oval? [the game's flat oval; the option off]

## 8. UNVERIFIED and side observations

* The first pair after a character is created has no blob draw, although its shadow work has the ground. Cause not
  checked; it costs one frame.
* The 80 % darkness is Ninja's material rule (texture alpha x diffuse alpha); not checked on hardware.
* The depth behaviour of a 1.2 HU lift under DXVK, and `$decal` on mesh draws, are not checked in game.
* The mask path's colour handling in Source (sRGB, fog) is not checked in game.
* Routing `8C099500` keeps the strict replays exact: expected, not yet run.
* Side observation for the collision owner, not this topic: on this GRID floor Tails and Amy stand (`flag 1`, mode 1)
  0.78-0.91 units above y = 0. Sonic, Knuckles, Big and E-102 stand at 0.00. On the ramp all are at about 0. Their
  blob is still placed on the ground; only s is about 2 % smaller. Reproduce with `shadow_probe.exe`, lines
  `tails/flat` and `amy/flat` of `probe_run4.txt`.

## 9. Reproduction (scratch: `SADC/build/research/shadow/`)

* `blob.py`: the blob object, model, material, UVs and the stage-start texlist table, from `sadc/baserom/1ST_READ.BIN`.
* `shadow_tex.py GDI [OUTDIR]`: OBJ_REGULAR texture 58's name, format and alpha profile, through LIB's
  `tests/texdump_ref.py`. Any PNG goes to OUTDIR (outside every repository); nothing ripped is kept in the repos.
* `shadow_probe.c` + `build_shadow.py [--run] [--nosnap]`: the measurements of section 2. `SA1_TEST_GDI` must point to
  the .gdi. It uses a locked snapshot copy of `LIB/build/x86_64-windows/libsa1.a` (`libsa1_snapshot.a`). Set
  `SHADOW_ONE=1` to run Sonic on the floor only.
* Outputs: `probe_run1.txt` (starts on exact vertices and diagonals: the probe misses there, a scene artifact),
  `probe_run2.txt` (summaries), `probe_run3.txt` (naive un-exclude), `probe_run4.txt` (with state flags).

## Review (adversarial)

Reviewer: independent adversarial review, 2026-10-21. Nothing in SADC/src, SADC/include, LIB or g-sa1 was changed.
Method: `shadow_probe.c` re-run (copy) against the same locked snapshot `libsa1_snapshot.a` (the 10:04 LIB build; no file
in LIB `src/`, `src/world/`, `include/` changed after it), plus a new probe `review_probe.c` (scratch
`SADC/build/research/shadowreview/`, section R.6). Addresses checked against the asm (`asm/1st_read/text/8C098EE0.src`),
Ghidra (`8C099500.c`, `8C0990DE.c`, `8C04E490.c`), the SADC C, LIB, g-sa1 and the installed 0.6.0 addon.

### R.1 Verified

* **C1** `sa1_character.c:1052` and `sa1_capture.c:1078-1081`; `excluded` = 1 every pair. Reproduced exactly
  (`review_run1.txt`): 34/35 pairs on the floor (11/12 ramp, 31/32 block), E-102 every pair, high/vhigh 16/7 (Sonic),
  5/0 (Amy), 22/20 (Big); naive un-exclude: archive -1, material 9431A400, flags 0081, blend 4/5, colour 178 178 178 204.
* **C2** `README.md:1069`, `cl_render.lua:29`, `sv_gsa1.lua:54`; the installed addon
  (`garrysmod/addons/gsa1/lua/gsa1/`) has the same lines. `cl_render.lua:121` gates every EFFECT batch by
  `gsa1_effects`; `:119` skips all batches in first person.
* **C3** The asm of 8C099500 (`8C098EE0.src:838-859`): R14 = &B_8C79F0F0, `TST`/`BT` to `return 0`, else
  `JSR njSetTexture` with R4 = @R14 and `return 1` - the proposed C. `stubs.tsv:152` is `ret 1`. B_8C79F0F0 is referenced
  only inside the 8C098EE0 chunk and by nothing in LIB. `8C04E490` calls `8C0990DE(2)` for levels 0x24/0x25, else
  `8C0990DE(0)`. Table 0x8C1C24A8: [0] = [1] = 0x8C1C2450 (`OBJ_REGULAR` 0x8C19F530, 96), [2] = 0x8C1C2490
  (`EFF_REGULAR` 0x8C1C3068, 138); 0x8C1C2450 is `SA1_ADDR_PVM_COMMON` (`sa1_internal.h:257`).
* **C4** `sa1_loader.c:375-388`: `want[]` = { EFF_REGULAR }. Archives measured (R.6 A): Sonic SONIC/SON_EFF/EFF_REGULAR,
  Tails MILES/EFF_REGULAR, Knuckles KNUCKLES/KNU_EFF/EFF_REGULAR, Amy AMY/AMY_EFF/EFF_REGULAR, E-102 5 own + EFF_REGULAR,
  Big BIG/FISHING/BIG_KAERU/EFF_REGULAR; the texlists the doc lists (28/24/22/53/35/36 textures) are the archive-0 ones.
* **C5** Owner log line 8, verbatim.
* **Data** Blob from 1ST_READ.BIN: 4 points +-5, normals +Y, indices 0 1 3 / 0 3 2, UVs 7..247, diffuse CCB2B2B2,
  attribute 94318400, texture 0x3A. Texture 58 of OBJ_REGULAR (`shadow_tex.py` on the owner's GDI): `shadow`, 16x16,
  pixel format 2 (ARGB4444), data format 2, one RGB value (0,0,0), alphas 0/17/221/255.
* **Code** Every per-character call line of the doc's table, `code_8C06C300.c:80`, `D_8C02D960` at 184/217/392/422,
  the 8C09247E formula (`code_8C092124.c:131-151`), the paused-only display draw (`code_8C0AE0E0.c:152-154` is in
  SonicDisplay 8C0AE17A).
* **New: winding.** The blob's triangles face up in the same convention as the body (winding normal . vertex normal > 0;
  body: 998 of 1001 triangles agree): G-SA1's back-face culling keeps it. The SOURCE preset has det +1, so this holds in
  G-SA1's coordinates too.
* **New: order.** The blob record comes after the body (Sonic record 28 of 29; Big 36 of 39, followed by two FISHING
  renderer draws that set their own texlist 0x8C54CB4C). Reason: SonicMain calls its display 8C0AE17A itself at
  `code_8C0A77BC.c:962`, before 8C0925A0 at `:967`. G1's "after the body" is right.

### R.2 Refuted or corrected

1. **L3 "does not set SA1_DRAW_EFFECT on it" cannot be done in `sa1_capture.c` alone.** `sa1_pose.c:1248-1249` adds
   EFFECT to every draw whose archive is > 0, whatever `extra_flags` says. Measured (R.6 C2, EFF_REGULAR standing in for
   the unloaded OBJ_REGULAR): `sa1_pose_emit_model_ex` with `extra_flags` 0 still gives flags 0x0041 (BLEND|EFFECT).
   OBJ_REGULAR will be archive >= 2 for every character, so S1 ("BLEND without EFFECT") fails with the files L3 lists.
   Dropping EFFECT is also the wrong choice: `write_mesh_src` (`sa1_character.c:757-790`) puts every non-EFFECT
   triangle into the v0.1/v0.2 `mesh_out`, and `sa1_pose.c:1233` stores `tex = -1` for archive > 0. Legacy hosts would
   get an untextured quad under the character. `capture_test` part 5 would fail as well (`:1513-1518`, CHECK `:1662`:
   every triangle IGNORE_SPECULAR; the blob's 9431A400 has no 0x80000).
2. **T2 names the wrong tests.** `capture_test.c:627-639` (part 2) reads the raw records; L3 does not affect it. The
   check that breaks whatever L3 does is part 7, "Sonic's materials" (`census` `:2000-2037`, CHECK `:2647-2648`:
   `draws_char == c.draws`, `c.palette == c.draws`, `c.ignspec_tris == c.tris`, `c.effect == 0`). The blob is
   SOURCE_MODEL, not palette-lit and has no IGNORE_SPECULAR.
3. **The first pair (section 8): the shadow work is read after sub step 1.** A wrapper around the world's step
   (`w->step`) shows that after sub step 0 of pair 1, `y_bottom = -1000000` with 18 land objects gathered. After sub
   step 1 it is 0. So 8C09247E returns on its own "no ground" test in the only step that draws. The capture is not at
   fault. Why the first probe misses: UNVERIFIED (one frame per spawn).
4. **1.1 water.** 8C0925A0 runs 8C02D2E0 on every gathered entry, water included (`code_8C0925A0.c:62-84`: the
   if/else-if only adds 8C02CA00 for water and the distance cull for task objects). A water surface can therefore become
   `y_bottom`, and the shadow then lies on the water. This is game logic and does not change the fix.
5. **Causes vs. blockers.** "No shadows" has exactly two causes: C1 and C2. C3 and C4 make the un-excluded draw unusable
   (blockers of the fix). C5 predicts the look after the fix on the owner's machine. Today neither C3, C4 nor C5 is
   visible.
6. **Reach.** The bound is `h^2 < rr + 900 - d^2` (d = horizontal distance from the polygon centre). For the probe start
   (53.7, 27.3), d = 23 from the circumcentre (50, 50), which gives a limit of 73.3 for the probe point. That is pos.y
   <= 72.3, and the shadow was found at 72.17. This matches better than the 76.8 the doc gives. Near polygon rims the
   reach drops to 30 units (120 HU). On G-SA1's pieces (at most about 90 wide, `sa1_coladapt.c:763`) the shadow
   therefore pops in and out with horizontal movement between about 30 and 70 units above the ground. Sonic's ordinary
   jump peaks at 18.7, so it never pops in a normal jump; it can pop after springs and drops. This is game logic: keep it.

### R.3 Gaps

1. **L2 archive order.** `load_common_textures` walks the common list in order. OBJ_REGULAR is entry 0 and EFF_REGULAR
   entry 1, so OBJ_REGULAR lands **before** EFF_REGULAR, as the game loads them. EFF_REGULAR's index then moves by one:
   Sonic 2->3, Tails 1->2, Knuckles/Amy 2->3, E-102 5->6, Big 3->4. Tests find archives by name (`archive_named`), and
   G-SA1's census has no EFF_REGULAR states. E-102 reaches 7 of `SA1_MAX_ARCHIVES` 8. Write this down. Add
   OBJ_REGULAR to `tests/texdump_ref.py:43` (`"common"`). It matches by name, so without that the texel check silently
   skips the archive. Reject the "decode only `shadow`" variant: G-SA1 would make 0 x 0 render targets for the others.
2. **G-SA1 cost.** `T.Prepare` (`cl_textures.lua:141-148`) queues every texture of every archive. Each one costs one
   `GetRenderTargetEx` and Lua painting, keyed per character kind. OBJ_REGULAR would add 96 per character (576 for six).
   Required: T.Prepare skips OBJ_REGULAR (or every common archive), and the `shadow` render target is made at first use.
   `T.Get(..., urgent)` from the batch already does that.
3. **G3 mask path.**
   a. `session.cpp:1110`: `white = !opt.vertex_color && !(PALETTE_LIT) && archive >= 0` sets RGB to 255 when
      `gsa1_vertexcolor` is 0. That would turn the mask colour 204 into 255 (a 100 % black core). SHADOW batches must
      be exempt. T4: with `vertex_color` false the shadow colours stay (204, 204, 204, 255).
   b. Tone mapping: Source's UnlitGeneric scales its output by the HDR tone-map scale on HDR maps (UNVERIFIED for the
      owner's map and DXVK). A multiplicative mask would then darken or lighten with exposure. Draw the shadow batch
      under `render.SetToneMappingScaleLinear(Vector(1, 1, 1))` and restore the previous value afterwards
      (`render.GetToneMappingScaleLinear()`), or measure it in game.
   c. `cl_warm_census.lua` is generated (`tools/gen_warm_census.py`, its header says not to edit it by hand). Regenerate
      it from core_test's census instead of editing it.
   d. G-SA1 builds against `build/deps/include/libsa1.h`. Refresh that copy for `SA1_DRAW_SHADOW`.
4. **Interpolation of a reappearing blob.** `render_interp` (`sa1_character.c:1649-1656`) moves vertices without a
   previous match by `root_shift` (`:1632-1637`), and that shift includes the character's vertical motion. In the first
   pair a blob appears (falling back into reach, a spawn), it is drawn up to one pair's fall above or below the ground for
   one frame. Fix: do not root-shift unmatched SHADOW draws, or fade them in. Extend S7 with this case.
5. **Test robustness.** S2 and S5 compare against values read after the pair (sub step 1), but the blob is drawn at
   sub step 0. Measured: on the ramp the centre is `y_bottom + 0.364` (Sonic slides), and at `sonic/high` pair 24
   (state y 72.17) the shadow work has ground but there is no blob. S2 must read the sub-step-0 shadow work (a step
   wrapper, as `review_probe.c` does) or test standing still only. S5 needs a margin (no draw above 105, a draw at 65
   or below).
6. **L1 boot path.** Store B_8C79F0F0 only in boots that also emulate the stage start's other setup (the live boot), not
   in transplants that keep a RAM image (`SA1G_LAND_NONE`). UNVERIFIED which path the strict replays use; T2 shows it.
7. **Comments.** `libsa1.h:561` ("archives 1.. the effect sets") and `:639` (EFFECT = "another archive than 0")
   need updating with L5. OBJ_REGULAR is not an effect set.
8. **Another blob user.** 8C0877A0 (`code_8C0876F4.c:99-113`, y + 0.4, through 8C036F12) draws Character_Shadow_Blob
   too, so its draws get marked as well. The doc's list of other users is not complete. Harmless.
9. **References.** Wave B's `l0-land` edits `stubs.tsv`. Name the row by its address (8C099500), not by line 152.
10. **L1 side effect.** After the blob, `D_8C673480` stays OBJ_REGULAR, as on the Dreamcast. In this scene the only
    later draws (Big's two FISHING draws) set their own texlist. Add a T1 assertion that every non-blob record keeps the
    texlist it has before the fix.

### R.4 Required changes

1. **L3:** keep SA1_DRAW_EFFECT on the blob (change neither `sa1_capture.c:1115-1116` nor `sa1_pose.c:1248-1249`) and add
   SA1_DRAW_SHADOW through `extra_flags`. S1 then expects BLEND|EFFECT|SHADOW, not NO_TEXTURE. `mesh_out` and part 5
   stay unchanged. (The alternative, no EFFECT, also needs `sa1_pose.c`, `write_mesh_src` filtering SHADOW, and part 5
   updates; it is larger and gains nothing.)
2. **G2:** in `cl_render.lua:121`, SHADOW batches are gated only by `gsa1_shadow`, before the `effects` test; all
   others stay as today. T5 keeps its two cases.
3. **T2:** update capture_test part 7 (the census / CHECK `:2647`) to leave SHADOW draws out, and drop the "parts 2/3"
   item.
4. **L2:** state the archive order and index shift, update `texdump_ref.py:43`, and add the G-SA1 `T.Prepare`
   exclusion (R.3.2).
5. **G3:** the vertex-colour exemption, the tone-map guard, census regeneration and the deps header (R.3.3).
6. **S7:** add the unmatched-blob case and its fix (R.3.4). **S2/S5:** use sub-step-0 values and margins (R.3.5).
7. **Section 8:** replace "although its shadow work has the ground" with the R.2.3 measurement.

Owner rules: respected. Placement, size, tilt and reach stay the game's code (8C0925A0, 8C09247E, 8C09222C). The only
new game C is 8C099500, decompiled. Storing B_8C79F0F0 in the world boot emulates the stage start the same way
`sa1_world_boot.c` already does (the screen and clip setup, `:189-223`). Textures are read from the disc at run time.
The option gets a Q menu control.
Ownership: as in section 6, plus `tests/texdump_ref.py` (060-lib-render) and `cl_textures.lua` T.Prepare / the vertex
colour rule in `session.cpp` (060-gsa1-render, after g0-seam).

### R.5 Verdict

The diagnosis of the symptom is right (C1 + C2) and so is the direction (capture the game's own blob and draw it). The
fix as written would fail its own S1 and break capture_test part 7. Make the EFFECT decision of R.4.1, fix the test
list, and add the G-SA1 details of R.3.2-3 before it is scheduled.

### R.6 Reproduction (scratch `SADC/build/research/shadowreview/`)

* `shadow_probe.c` (copy) -> `review_run1.txt`.
* `review_probe.c` -> `review_run2.txt`: A the archives, B the step wrapper (first pair), C the fix emulated (blob
  record's texlist set to EFF_REGULAR's; C1 `sa1_capture_emit_into` un-excluded, C2 `sa1_pose_emit_model_ex` with
  `extra_flags` 0), D the record order and texlists, E the winding.
* Build: `py -3.12 build_shadow.py --nosnap --src review_probe.c`, then run `review_probe.exe <gdi>`
  (`libsa1_snapshot.a` is a copy of the 10:04 build).
