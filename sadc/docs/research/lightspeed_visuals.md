# Light Speed Dash visuals and special-move effects (round 4c research)

Status: research and design only, written 2026-10-21. No file in LIB, g-sa1, SADC/src, include, tools, harness or config
was changed. The scratch work is in `sadc/build/research/lightspeed_visuals/` (section 9).

Owner reports on G-SA1 0.4.0:

* "the light-speed charge particles are extremely large when they aren't supposed to be"
* "the charged Sonic model has no material"
* "The glowing charged model should also be transparent"

## 0. Summary

| # | Symptom | Root cause (where) | Evidence | Fix |
|---|---|---|---|---|
| 1 | The charge particles are huge | `emit_sprite` in LIB `src/sa1_capture.c:546-640` ignores `sp->sx`/`sp->sy` when `NJD_SPRITE_SCALE` (0x10) is clear (lines 563-566 leave sx = sy = 1). It also ignores the scale of the matrix, but the SDK's unscaled path (8C63F2D4) transforms every corner by XMTRX. | Measured in a libsa1 world (section 2.3). The game draws the sparkles 12.4 SA1 units wide at spawn, shrinking to 0. libsa1 emits a constant 64 units (256 Hammer units). That is 5.2x too large at spawn and 160x near the end of each sparkle's life, with a mean of 21.8x over 423 sprite records. | F1 |
| 1b | The streaks that trail the sparkles are white wedges | `emit_polygon` (`sa1_capture.c:729-826`): (a) the two SDK entry points have their meanings swapped. 8C63D66C is njDrawTriangle3D (n *triangles*; attr 0x2 = strip, 0x1\|0x2 = fan), but it is emitted as a fan and the points are cut to n. (b) Every textured polygon is emitted untextured (line 808): the texture number from `njSetTextureNum` is never recorded (LIB `src/rt/rt_texture.c:50-53, 81`: the hook is "NOT IMPLEMENTED YET"). | Asm of 8C63D66C (section 2.5). Audit: 643 ribbon records, attr 0x80000062, 31 points, n 29. | F4 |
| 2 | The charged model shows no material | The aura model (S_MOT 0x0CC02AD4, 20 node models) has `nbMat = 0`, yet its meshsets use material 0 at `mats = 0x0CC00000`. Ninja indexes `mats[id]` without checking nbMat. libsa1 only reads materials below nbMat (LIB `src/sa1_pose.c:1170`, rt snapshot `src/rt/rt_draw.c:433`), so the material is lost: attr 0, untextured, opaque. G-SA1 then draws a lit, opaque `vgui/white` shell (three nested copies, the largest 1.1025x) over Sonic. | Model dump, SDK asm 8C610A5E-8C610A7C, probe draw (section 3) | F3 |
| 3 | The charged model is not transparent | (a) The same lost material. The real one is ONE/ONE additive, `IGNORE_LIGHT`, black diffuse. (b) `sa1_capture_draw_state` (`sa1_capture.c:463`) gives Ninja motion draws (8C60E080/8C60E49A, record kind NJMOTION) the hand-written renderer's rules: vertex RGB white, the constant material on alpha only, palette-lit. The original draws them through njDrawModel 8C6108B0, which adds the constant material to all four channels. | ctl3d 0x36C (OFFSET_MATERIAL) measured in the harness RAM and in the records. Section 3. | F2 + F3 |

All three root causes are in libsa1's capture emitter, the host side of `sa1_capture.c` and `sa1_pose.c`. The G-SA1 sprite path,
the unit conversion (4 Hammer units per SA1 unit, set once in LIB: `sa1_capture.c:551`, `k = ctx->coords.scale`) and
`rt_effect.c` are **not** involved (section 2.6). A prototype of F1-F4 (host side only) was built against a locked snapshot
of `libsa1.a` and measured:

* sparkles 12.40 / 11.60 / ... units (the game's numbers);
* the aura becomes `BLEND|UNLIT|ADDITIVE`, ONE/ONE, colour (0, 26-71, 255) per the game's pulse;
* the ribbons become strips;
* over 24 move scripts of all six characters (102,538 model draws), the only model draws that change are the 10 aura draws
  (section 6.3).

The special-move audit (section 7) finds further problems in the same draw kinds:

* **Big's fishing line** draws as 6 separate dashes instead of an 11-segment line (njDrawLine3D "connected" flag).
* The strip ribbons of **Knuckles' Maximum Heat** and of the **barrier / magnetic barrier / invincibility** effect are
  emitted as fans, and the barrier's are also untextured.
* Several of **Knuckles' dig and debris sprites** have the wrong size (unscaled sprites).
* **Amy's Warrior-Feather ring** pulses in the game, but not in ours (its `sp->sx` is ignored).
* The game's own sprite module draws scaled sprites with `sp->sx` on both axes (bubbles become ellipses in ours).
* Sprite UVs use /255 where the SDK uses /256.
* The **snowboards** are also NJMOTION draws, so they inherit cause 3b.
* **2D HUD sprites** are not emitted at all (E-102's lock-on cursor and timer, Big's fishing HUD). This is an owner question.

## 1. Method and evidence base

* **Game code**: SADC C and asm.
  * Sonic's display and the aura draw: `src/1st_read/code_8C0AE0E0.c`, `code_8C0B4F00.c`.
  * The charge: `code_8C0B11C0.c`, `code_8C0A77BC.c`.
  * The sparkles: `code_8C0B5B80.c`, `code_8C0B58E0.c`.
  * The ribbon task: `code_8C0B54AC.c`, `code_8C0B4F00.c`.
  * SDK draws, from asm: `asm/1st_read/text/8C63EF74.src`, `8C63F038.src`, `8C63F2D4.src`, `8C63D414.src`, `8C63D66C.src`,
    `8C63CE24.src`, `8C60F5BC.src`.
* **Data**: the extracted images `baserom/1ST_READ.BIN` at 0x8C010000, `baserom/overlays/S_MOT.BIN` at 0x0CC00000 and
  `B_CHAOS7.BIN` at 0x0C900000 (`baserom/overlays.json`), read with `tools/research/rip_character.py`'s readers.
  Textures were decoded from `disc/SONICADV/SON_EFF.PRS`.
* **The original game's draw state**: harness RAM dumps with the screen (`build/traces/capture_harness/ec1_run_jump.step110.ram`,
  `.step250.ram`, 2026-10-20):
  * control-3D flags `B_8C88F62C = 0x36C`;
  * constant-attribute masks `and FFFFFFFF`, `or 00002000`;
  * screen distance `B_8C88F5E4 = 457.059`, aspect 1.0 / 1.0, near/far 3 / 3000.
* **libsa1 measurements**: scratch programs linked against a copy of `LIB/build/x86_64-windows/libsa1.a`
  (built 2026-10-20 14:59, newer than every source used: `sa1_capture.c` 14:17, `sa1_pose.c` 14:47), copied under
  `build/.build.lock`.
  * `lsd_probe.c`: Sonic with event flags 0x8D (Light Speed Shoes) + 0x8E (Crystal Ring), giving equipment 0x0003. He runs on
    Emerald Coast act 1's own collision (DISC), holds the action button from pair 30 to pair 190, and the probe dumps every
    capture record and the render output.
  * `lsd_audit.c`: all six characters with all their upgrades, 24 move scripts. Every capture record is classified, and the
    render output is dumped.
  * Both programs were also linked with the prototype fixes (`proto/`).

Pair = one 1/30 s tick = two 60 Hz game steps. "Units" are SA1 units; G-SA1 uses 4 Hammer units (HU) per unit
(`g-sa1/src/core/session.cpp:229`, `SA1_COORDS_PRESET_SOURCE`).

## 2. The charge particles

### 2.1 How the game draws them

* **Charge**: SonicMain mode 4 (spin dash charge) calls the jump-ball effect spawner `func_8C0B2D40` every step. With
  equipment bit 0x2 (Light Speed Shoes), with probability 0.1 per step, it also spawns three sparkles
  `func_8C0B5B80(twp, 31)` (`code_8C0A77BC.c:269-277`).
* The charge completes after 210 steps of mode 4, or 120 with equipment & 3 == 3 (Crystal Ring). It then sets
  `twp->flag |= 0x200` (`code_8C0B11C0.c:157-159`): Sonic is "charged" and the aura is drawn (section 3).
* **Sparkle** (`code_8C0B5B80.c`):
  * It is placed on a shell of radius 25..40 around the centre of collision shape 0.
  * `timer.l = n + 1 = 32`; `btimer = equipment & 1` (Crystal Ring).
  * Each sparkle also creates a companion **ribbon** task `func_8C0B5520` with 31 points.
* **Sparkle exec** `func_8C0B5994` (`code_8C0B58E0.c:46-93`): it turns towards the player's centre (AdjustAngle 0x400 / 0x700),
  moves at most 3 units per frame, and draws every frame until the timer runs out.
* **Sparkle display** `func_8C0B58E0` (`code_8C0B58E0.c:20-44`): drawn on sub step 0 only. Blending SRCALPHA/ONE (8/10).
  The matrix is `translate(pos) * rotY(camera ang.y) * rotX(camera ang.x) * njScale(s, s, s)` with `s = timer / 32`
  (line 33). Then `njDrawSprite3D(&D_8C1C6AD0[btimer], 0, 32)` (line 38), attr 0x20 = `NJD_SPRITE_ALPHA` only: **not scaled**.
* **Sprite data** (dumped from 1ST_READ, `dump_sprites.py`):

  | Sprite | p | sx, sy | texlist | tanim cell 0 |
  |---|---|---|---|---|
  | `D_8C1C6AD0[0]` | (0, 0, 0) | 0.2, 0.2 | 0x8C1C6AA4 = { `lcharge02` } | 64x64 px, centre (32, 32), UV 0..255 |
  | `D_8C1C6AD0[1]` | (0, 0, 0) | 0.2, 0.2 | 0x8C1C6AAC = { `lcharge04` } | 64x64 px, centre (32, 32), UV 0..255 |

  Index [1] is the Crystal Ring colour.
* **Textures** (SON_EFF 25-28, 32x32 ARGB1555, decoded from the disc):
  * `lcharge02`: alpha-weighted mean RGB (66, 45, 137), a violet-blue glow;
  * `lcharge04`: (72, 114, 60), green;
  * 90 % of the light lies within radius 12.7-13.2 px of 16.

### 2.2 What Ninja does with a 3D sprite (SDK asm)

`njDrawSprite3D` 8C63EF74 (`asm/1st_read/text/8C63EF74.src`) calls `func_8C63F038` when `attr & 0x10`, else `func_8C63F2D4`
(8C63EFE6-8C63EFF2).

* **Scaled path, 8C63F038**: corner offsets are `(-cx, sx - cx) * sp->sx` and `(-cy, sy - cy) * sp->sy` (8C63F09A-8C63F0D6).
  Only `sp->p` is transformed (`FTRV XMTRX,FV4`, 8C63F0E2). The offsets are scaled by `dist / z` and the aspect
  (8C63F10C-8C63F1C2) and added in screen space (y down). The extent is therefore `tanim px * sp->sx` view units,
  facing the camera, and the **matrix scale is not applied**. libsa1 does this correctly.
* **Unscaled path, 8C63F2D4**: the same offsets times `sp->sx` / `sp->sy` (8C63F332-8C63F370) are added to `sp->p` in the
  matrix's local XY plane (8C63F3C8-8C63F3E2). **Each corner** is transformed by XMTRX (`FTRV XMTRX,FV0/FV4/FV8/FV12`,
  8C63F3F2-8C63F42C) and then projected. The quad is a real 3D quad: its size is `tanim px * sp->sx * |M e_x|`.
* The game's own sprite module behaves the same way:
  * 8C09E778 -> 8C09EB08 (unscaled: corners transformed by the matrix, `code_8C09EB08.c:9-15, 124-127`);
  * 8C09E880 (scaled), with one quirk: the y scale register is loaded from `sp->sx` as well, so `sp->sy` is never read
    (`code_8C09E880.c:28-30`).
* UVs are `u * 1/256` (`0x3B800000` at 8C63F058; 0.00390625 in `code_8C09EB08.c`). libsa1 divides by 255
  (`sa1_capture.c:571-574`).

### 2.3 What libsa1 does, measured

`emit_sprite` (`sa1_capture.c:546-640`):

* `sx = sy = 1.0f` unless `attr & NJ_SPRITE_SCALE` (lines 551, 563-566);
* `rect = (-cx * sx * k, ..., (tsx - cx) * sx * k)` (lines 598-601);
* only the anchor goes through the matrix (line 596).

For the sparkle (attr 0x20) that gives 64 x 64 units whatever the matrix scale is. `lsd_probe` (`probe_8D_8E.log`) records
for every sparkle record the payload's sp.sx, the tanim size, the matrix's axis length `|m e_x|` and the emitted rect:

| Pair | Matrix scale s (= timer / 32) | Game width 64 x 0.2 x s | libsa1 width | Ratio |
|---|---|---|---|---|
| 42 | 0.9688 | 12.40 | 64.00 | 5.2x |
| 47 | 0.7812 | 10.00 | 64.00 | 6.4x |
| 60 | 0.5000 | 6.40 | 64.00 | 10.0x |
| 64 | 0.2500 | 3.20 | 64.00 | 20.0x |
| 98 | 0.0312 | 0.40 | 64.00 | 160x |
| all 423 records | | | | mean 21.77x, max 160x |

The probe's render output agrees: `sprite 0: rect -128.00 -128.00 128.00 128.00 host units = 64.00 x 64.00 SA1 units`. In G-SA1
that is 256 HU (about 6.6 times Sonic's height of 39 HU); the game's sparkle starts at 49.6 HU and shrinks to nothing.

On screen, at the camera distance the libsa1 world uses (`SA1G_VIEW_DIST` 40, LIB `src/world/sa1_world_tick.c:79`), a
sparkle at Sonic's depth starts in the game at 12.4 x 457.06 / 40 = 142 px of 640 (22 % of the width) and shrinks to 0.
Ours is 731 px, wider than the screen. With the prototype fix (F1) the probe prints `rect -24.80 ... 24.80 host units =
12.40 x 12.40 SA1 units`, then 11.60, 10.80, and so on: exactly the game's widths.

### 2.4 Colour and blending

The sparkle records have blend `A08` (SRCALPHA / ONE) and no `NJD_SPRITE_COLOR`. libsa1 resolves the texture: draw
`prim 1 src 2 arc 1 tex 28`, flags 0x255 = BLEND, DOUBLE_SIDED, UNLIT, EFFECT and ADDITIVE, blend 4/1. That is correct; only the
size is wrong.

### 2.5 The ribbons (companion strips)

`func_8C0B504A` (`code_8C0B4F00.c:121-137`): drawn on sub step 0, blending 8/10 (SRCALPHA/ONE),
`njSetTexture(&D_8C1C6A94[btimer]); njSetTextureNum(0)`. The texlists are `D_8C1C6A94[0]` = { `lcharge01` } (blue) and
`[1]` = { `lcharge03` } (green, with the Crystal Ring). The call is `func_8C63D66C(pc, pc->num - 2, 0x80000062)`. The strip
points alternate sides at +-0.4 units (`code_8C0B4F00.c:164-202`), so the band is 0.8 units wide. The UVs use u = 0x70 or 0x90
and v = 0 or 0xFF; the colour is `timer.l` = 0xFFFFFFFF.

What 8C63D66C does (`asm/1st_read/text/8C63D66C.src`):

* the stride is 1 if `attr & 2`, else 2 if `attr & 1`, else 3 (8C63D716-8C63D732);
* triangle i = (v0, v1, v1 + 1), with `v1 = 1 + i * stride` and `v0 = (attr & 1) ? 0 : i * stride` (8C63D758-8C63D7AE,
  8C63D922-8C63D93A);
* n is the number of **triangles**.

So 0x2 is a strip, 0x1|0x2 a fan, and 0 independent triangles. 0x40 selects translucent context words 0x0210000A / 0x02100002
(8C63D6C8-8C63D6EC); 0x80000000 selects the textured variants (`H'80000000` at L8C63D780). The meaning of 0x20 (it picks
submission routine 8C64FC68/8C64FAE8 over 8C650144/8C64FE0C) is **UNVERIFIED**; it is probably the per-vertex colour mode.

`func_8C63D414` (`8C63D414.src`) transforms all n **points** (8C63D480-8C63D494), copies their UVs and colours, and clips them
as **one convex polygon** (8C63D536-8C63D54C). That is njDrawPolygon3D. libsa1 has the two names swapped:

* `rt_draw.c:633-641`: "8C63D414: 3D triangles (n points; attr 0x20 a fan)", "8C63D66C: 3D polygon";
* `sa1_capture.c:735`: `fan = r->kind == SA1_CAP_POLY3D || (r->attr & NJ_DRAW_FAN)`, with `NJ_DRAW_FAN 0x20` (line 43);
* `sa1_capture.c:744-746`: `num = r->n`.

The ribbon (31 points, n = 29) is therefore emitted as a fan of 27 triangles around point 0, and its last 2 points are dropped.
It is also untextured, because `sa1_capture.c:808` turns `NJ_DRAW_TEXTURE` into `SA1_DRAW_NO_TEXTURE`. The texture number
cannot be resolved: `njSetTextureNum`'s capture hook is a no-op (`rt_texture.c:50-53`, called at line 81). The probe shows
draws 51-68: `prim 0 src 4 arc -1 tex -1 flags 0x02D5` (NO_TEXTURE), white vertices with alpha 255 -> 5. In G-SA1 these
become white additive wedges instead of thin blue (or green) streaks. With the prototype the triangle counts become the strip
counts (for example 19 instead of 17 for a 21-point strip). The texture still needs the rt change (F4).

### 2.6 What is not the cause

* **rt_effect.c**: the particle execs (8C046480 / 8C0464EE / 8C046960) drive the particle manager's dust and splash nodes. Those
  are drawn by `func_8C09E0B8` with `func_8C09E778(sp, cell, 51)` (`code_8C09E0B8.c:101-104`): scaled, `sp->sx = sp->sy = ep->scl`.
  No 8C09E778 sprite occurs in any Sonic script of the audit; the charge sparkles are task sprites. rt_effect.c's
  header states that it is bit-exact against the original.
* **G-SA1's sprite path**: `session.cpp:920-944` builds each quad as `position + R * x - U * y` from `sa1_sprite.rect`. The rect
  is already in host units (LIB multiplies by `coords.scale` = 4.0, `sa1_capture.c:551`, `sa1_core.c:354`), so nothing is
  scaled twice.
* **The capture records themselves**: the payload holds the game's sprite and tanim (sx 0.2, 64x64), and the record's XMTRX holds
  the game's njScale. All the information is there; only the emitter drops it.

## 3. The charged (light speed) aura

### 3.1 How the game draws it

Sonic's display `func_8C0AE17A` (`code_8C0AE0E0.c:88-162`):

* `func_8C056926()` saves the draw flags;
* `func_8C05694A(0, 0x80000)` ORs `IGNORE_SPECULAR` into the constant attribute (line 104);
* the matrix is translated to the collision centre and rotated, and the body is drawn with the motion player;
* then, if `twp->flag & 0x200`, the aura is drawn: `func_8C0B4F00(twp, mwp, pwp)` (lines 144-145).

`func_8C0B4F00` (`code_8C0B4F00.c:70-115`, NON_MATCHING only in register order) draws on sub step 0. The action is the aura
object `S_MOT 0x0CC02AD4` with Sonic's current motion and frame, drawn three times with `func_8C60E49A` (Ninja's action
draw). The pulse is `f = (c < 64 ? c : 128 - c) / 64` with `c = B_8C752BEC & 0x7F` (`code_8C0B4F00.c:89-93`), a triangle
between 0 and 1 with a period of 128 frames:

| Pass | Matrix | Constant material (a, r, g, b) |
|---|---|---|
| 1 | as Sonic's body | (0, 0, 0.28 - 0.18f, 1.0) |
| 2 | njScale 1.05 | (0, 0.05, 0.13 - 0.18f, 1.0) |
| 3 | another njScale 1.05 (1.1025) | (0, 0.05, 0.08 - 0.18f, 0.3) |

The constant material is reset to 0 afterwards.

The control-3D flags during the draw are **0x36C** = CNK_CONSTANT_ATTR | MODEL_CLIP | DEPTH_QUEUE | **OFFSET_MATERIAL (0x20)** |
ENABLE_ALPHA | CONSTANT_ATTR. The same value is measured in the original game (harness RAM above) and in every aura capture
record (`probe_8D_8E.log` line 121: `NJMOTION model 0x0CC0290C ctl3d 0x36C and FFFFFFFF or 00082000 cmat a 0.000 r 0.000
g 0.122 b 1.000`). So the constant material is **added** to the material's colour.

### 3.2 The aura model (dumped, `dump_models.py`, `dump_aura_raw.py`)

* 73 nodes; 20 node models (207 vertices, 290 triangles), the same skeleton as Sonic's main model.
* Every node model has `mats = 0x0CC00000` and **nbMat = 0**, and one strip meshset with material id 0, normals, and no UVs or
  vertex colours.
* The material at 0x0CC00000 (the start of the S_MOT image) is:
  * diffuse 0xFF000000 (black, alpha 255), specular 0xFFFFFFFF, exponent 6, texture id 0;
  * attr **0x26182400** = SRC **ONE** / DST **ONE**, IGNORE_SPECULAR, USE_ALPHA, **IGNORE_LIGHT**, bilinear;
  * **no USE_TEXTURE**.

  In words: an untextured, unlit, additive model whose colour comes from the constant material.
* A scan of all 550 node models of the asset table in 1ST_READ, S_MOT and B_CHAOS7 (`scan_matrange.py`) finds material
  ids >= nbMat **only** in the aura's 20 models and in Eggman's model 0x8C57A694, which is not a G-SA1 character.

### 3.3 Ninja does not bound the material id

8C60E49A's node draws reach `njDrawModel`: its callers are SDK functions 8C60D8DC, 8C60DD56, 8C60DDE4 and 8C60DE96
(`build/1ST_READ.xrefs.json`). `njDrawModel` 8C611028 calls the basic model draw `func_8C6108B0`
(`asm/1st_read/text/8C60F5BC.src`, 8C611030). Its meshset loop at 8C610A5E-8C610A7C runs:
`MOV.W @R10,R4; AND #0x3FFF; R14 = model->mats + id * 20; attr = mat->attrflags`. It reads `nbMeshset` (+0x14, 8C6109FA)
but never `nbMat` (+0x16): no `MOV.W @(22,Rn)` exists in the SDK draw code 8C60B518-8C612000 (grep). The material at
0x0CC00000 is therefore what the Dreamcast draws with.

### 3.4 What libsa1 and G-SA1 make of it

* `rt_cap_model` (`rt_draw.c:421-470`) snapshots `nmat = model->nbMat` materials (line 433), which is 0 for the aura.
* `sa1_pose_emit_model_ex` (`sa1_pose.c:1054, 1170`) only uses `mat_id < nmat`. The material is lost: attr 0, diffuse white.
* `sa1_capture_draw_state` (`sa1_capture.c:463`) sets `hand` for NJMOTION records. `ds_color` (`sa1_pose.c:869-885`) then
  writes white RGB and only the alpha offset, and `sa1_draw_classify` adds PALETTE_LIT.

  This contradicts the header's own description (`sa1_capture.h:219-220`: "Ninja's draws (the Light Speed aura's tint) take
  all four channels"). The `NJMOTION` kind exists only because `rt_draw.c:825-849` reuses the game's motion player
  8C040784 and 8C037098 to walk the nodes. The original draws them with Ninja.

Probe output once charged (`probe_8D_8E.log` line 135):

```
draw 49 prim 0 src 0 arc -1 tex -1 n 870 flags 0x0020 blend 1/0 mat 0x00000000 diffuse 255,255,255,255 col0 255,255,255,255
```

That is 870 triangles (3 x 290), opaque (ONE/ZERO), palette-lit, untextured and white.

G-SA1 renders it as follows:

* `build_batches` puts it in pass 0 (opaque) because it has no `SA1_DRAW_BLEND`.
* `T.Material` (`g-sa1/addon/lua/gsa1/cl_textures.lua:373-407`) creates `VertexLitGeneric` with `$basetexture "vgui/white"`
  (line 385) and `$halflambert`.
* `vgui/white` exists (`hl2_textures_dir.vpk: materials/vgui/white.vtf`, checked with `vpk_find.py`), so this is not the
  error texture.

The result is three nested, solid, map-lit white Sonic shapes; the outer one is 10 % larger than Sonic. This is the
untextured "no material" look the owner saw, and it is opaque.

### 3.5 The faithful result (prototype, measured)

With F2 and F3 the same draw becomes (`probe_proto.log` line 135; `lsd_audit_proto` "D|" lines for pairs 75-82):

```
draw 49 prim 0 src 1 arc -1 tex -1 n 870 flags 0x0211 blend 1/1 mat 0x26182400 diffuse 0,0,0,255 col0 0,31,255,255
```

That is BLEND, UNLIT and ADDITIVE with ONE/ONE. The pass-1 colour (0, 26..71, 255) follows the pulse: 0.28 - 0.18f, for
example 0,56,255 then 0,54,255 and so on. Passes 2 and 3 work out to (13, 0..33, 255) and (13, 0..20, 77) (channels clamped at
0; the exact clamping is **UNVERIFIED**, see section 8).

G-SA1 then uses pass 1 and `UnlitGeneric` with `$additive`/`$vertexalpha` and `render.OverrideBlend(ONE, ONE)`
(`cl_render.lua:108`). The result is a translucent, pulsing blue glow added on top of Sonic, which is the faithful version of
"transparent". With the Crystal Ring the aura is still blue; only the sparkles and ribbons turn green.

## 4. Proposed fixes

All the fixes are in libsa1 except F6. F1, F2 and F3's host half need no world, rt or pclower change; F3's rt half, F4 and F5
touch `src/rt` (the lowered runtime: rebuild and replay check).

### F1. Sprite size and UVs (LIB `src/sa1_capture.c`, `emit_sprite`)

* Always read `sx = sp->sx` and `sy = sp->sy` (payload +0x0C / +0x10).
* With `NJ_SPRITE_SCALE`:
  * keep the rect independent of the matrix (8C63F038);
  * for a game-sprite record (`SA1_CAPF_GAME_SPRITE`), set `sy = sx` (8C09E880 quirk).
* Without it, multiply by the matrix's axis lengths: `sx *= |m e_x|`, `sy *= |m e_y|` (columns 0 and 1 of the
  model -> SA1 world matrix `m`; the view matrix in slot 0 is rigid).
* UVs: divide by 256, not 255.
* Exact patch: `build/research/lightspeed_visuals/proto.diff`, hunk 2.
* Optional refinement for unscaled sprites. Project the local x and y axes of the record's XMTRX (`r->m` rows 0 and 1, view
  space with x right and y down) into the game's view plane. That gives the in-plane rotation (add it to `sa1_sprite.angle`)
  and the mirror (swap v0/v1 when `cross(Xv, Yv) < 0`), so sprites the game rotates with `njRotateZ` keep their spin (Amy's
  feather ring `code_8C0DF760.c:66`) and Knuckles' dirt clods keep their flip. Sprites the game tumbles in 3D (Knuckles'
  debris `lbl_8C0CAD98`, Amy's `func_8C0DF100`) stay billboards facing the host camera: owner question 2.
* Documentation: `libsa1.h`'s `sa1_sprite` comment, "rect: the quad ... host units". Add "(the game's size: tanim px x
  sp->sx, x the matrix scale for unscaled sprites)".

### F2. Ninja motion draws are Ninja draws (LIB `src/sa1_capture.c:463`)

Use `ds->hand = (kind == SA1_CAP_RENDER || kind == SA1_CAP_RENDER2)`. NJMOTION records then use the material colour plus or
replaced by the constant material on all four channels, `IGNORE_LIGHT` gives UNLIT, and they are not PALETTE_LIT. Fix the
comment in `rt_draw.c:710-718`, which describes them as recorded "as Ninja draws". This also changes the snowboards (Sonic
`func_8C0B2F46`, Tails `func_8C0BF92C`, the only other 8C60E080 callers in character code): they become material-coloured and
lit by the host instead of palette-lit white. This is more faithful, but not exercised (risk 1).

### F3. Material ids beyond nbMat (LIB `src/sa1_pose.c:1170` and `src/rt/rt_draw.c:421-470`)

* **Host**: in `sa1_pose_emit_model_ex`, when `mat_id >= nmat` and `model->mats != 0`, read `mats + 20 * mat_id` through the
  reader. The prototype reads live, which is correct for the aura's static material (`proto.diff` hunk 5).
* **rt**: in `rt_cap_model`, snapshot `max(nbMat, max meshset id + 1)` materials (bounded by `RT_CAP_MSNAP_MAXMATS`) and walk
  the meshsets' `type_matId` (cheap: 20 entries for the aura). Effects that edit a material just before the draw then stay
  correct for such models too. Keep the host fallback for records without a snapshot.

### F4. 3D triangles, 3D polygons and their textures (LIB `src/sa1_capture.c` `emit_polygon`, `src/rt/rt_draw.c`, `src/rt/rt_texture.c`, `src/sa1_capture.h`)

1. Swap the meanings to match the asm (fix the comments in `rt_draw.c:633-641` and the kind comments in `sa1_capture.h:62-63`):
   * **`SA1_CAP_TRI3D` = 8C63D414 = njDrawPolygon3D**: one convex polygon of n points, emitted as a fan
     (0, i, i + 1) whatever the attr is.
   * **`SA1_CAP_POLY3D` = 8C63D66C = njDrawTriangle3D**: n triangles, with `stride = (attr & 2) ? 1 : (attr & 1) ? 2 : 3`,
     `v0 = (attr & 1) ? 0 : i * stride`, `v1 = 1 + i * stride`, `v2 = v1 + 1`.

   The prototype (`proto.diff` hunks 3-4) covers strip, fan and independent triangles; the rare 0x1-only stride-2 case must be
   added.
2. **Texture number**:
   * define the hook `RT_TEX_CAPTURE(n)` as `rt_cap_texnum(n)` in the libsa1 profile (`rt_texture.c:52-53`; it stays empty in
     the harness profile);
   * `rt_draw.c` keeps the number in a static (pcdata, per world) and copies it into a new record field `texnum` in `rt_cap_new`;
   * bump `RT_CAP_VERSION` and `SA1_CAP_VERSION` to 5 and the record size from 0xD0 to 0xD4, in both `rt_draw.c:171-218` and the
     mirror `sa1_capture.h:92-128`, together with the size checks;
   * `njSetTextureNum`'s game-visible stores are unchanged, so replays stay bit-exact.
3. **`emit_polygon`**: when `attr & 0x80000000` and the payload has UVs, resolve `sa1_capture_resolve(c, r->texlist, r->texnum,
   &arc)`, set the draw's archive and texture, take each corner's UV from the payload's `NJS_TEX` (s16 u, v; / 256 is
   **UNVERIFIED** for the polygon path, but the model and sprite paths use 1/256), set `SA1_TRI_HAS_UV`, and drop
   `SA1_DRAW_NO_TEXTURE`. Classify the alpha with `cap_tex_alpha`, as sprites do.

### F5. 3D lines (LIB `src/sa1_capture.c:686` and the segment loop)

njDrawLine3D 8C63CE24 (`asm/1st_read/text/8C63CE24.src` 8C63CEF2-8C63CF4C, 8C63CF82-8C63CF96):

* `stride = (attr & 0x0F) ? 1 : 2`;
* first point `(attr & 1) ? 0 : i * stride`, second point `i * stride + 1`;
* n lines.

libsa1 treats 0x20 as "connected" and otherwise pairs. Big's fishing line (`code_8C0EC180`: `njDrawLine3D(&p3c, 11, 0x42)`,
12 points) is meant to be an 11-segment polyline, but is emitted as 6 separate segments. E-102's targeting lines (0x40, n 1)
are unaffected.

### F6. G-SA1 (`g-sa1/src/core/session.cpp:888`)

`white = !opt.vertex_color && !(flags & PALETTE_LIT)` forces untextured draws white when "Vertex colours" is off. Their only
colour is the vertex colour (the aura, the tail-attack quads, the ribbons until F4, the lines), so the white rule should
require `rb.archive >= 0`. Nothing else is needed in G-SA1: after F1-F3 the draws arrive with the right flags, blend factors
and colours, and `T.Material`, `build_batches` and the pass 1 `OverrideBlend` already handle untextured additive draws. The
Options tab needs no new setting (the owner wants faithful visuals).

## 5. Tests

1. **LIB `tests/capture_test.c`, a new part 10 "special-move visuals"** (run with `py -3.12 tests/build_capture.py --run` and
   `SA1_TEST_GDI`). Sonic on DISC EC1 with event flags 0x8D and 0x8E, written as G-SA1's `set_event_flags` does, holds the
   action button for 75 pairs.
   * Every SPR3D record with texlist 0x8C1C6AA4 or 0x8C1C6AAC and attr 0x20 emits a sprite whose width
     `(rect[2] - rect[0]) / scale` equals `64 * 0.2 * |m e_x|` within 1e-4. Today that is 64 for every one.
   * Once `game_flags & 0x200`: one draw with `source MODEL`, `flags & (BLEND|ADDITIVE|UNLIT)`, blend ONE/ONE, material
     0x26182400 and 870 triangles per pair. Pass 1's corner colour is (0, round(255 x (0.28 - 0.18f)), 255, 255), with f from
     `B_8C752BEC` as read in the arena.
   * No Sonic draw with `archive < 0`, without `SA1_DRAW_BLEND` and not from a polygon.
   * The ribbon (POLY3D, attr 0x80000062): triangles == n of the record, archive 1, texture `lcharge03` (Crystal Ring) or
     `lcharge01`, UVs (0x70/256, 0) and so on (after F4).
2. **Line and polygon rules**:
   * Big after a cast: the fishing line record (attr 0x42, n 11) gives 11 connected segments; segment i's end equals segment
     i + 1's start.
   * Tails' tail attack (njDrawPolygon3D 0x60, 4 points) gives 2 triangles, as today.
3. **Regression across characters**: turn `build/research/lightspeed_visuals/lsd_audit.c` into a LIB test. It runs 24 scripts
   for the 6 characters with all upgrades and dumps every model draw. Store a reference dump; after the fixes only NJMOTION
   draws may change (today that is the 10 aura draws, `draws_orig.txt` vs `draws_proto.txt`).
4. **Replays**: `py -3.12 build.py --test` (all 10 reference traces strictly bit-exact) after the rt changes of F3, F4 and F5,
   plus `tests/capture_test.c` part 1 (capture adds recording only).
5. **G-SA1**: a core test for `prepare_view`. Untextured blended batches keep their vertex RGB with `vertex_color = false`, and
   textured ones turn white as before.
6. **In game** (owner check after installing):
   * Light Speed Shoes on, hold the action button for about 4 s: small blue sparkles (green with the Crystal Ring) fly in and
     shrink, trailing thin streaks.
   * When charged, Sonic glows a translucent, pulsing blue.
   * Big's fishing line is continuous.

## 6. Prototype

### 6.1 What was built

`proto/sa1_capture.c` and `proto/sa1_pose.c` are copies of LIB's files with F1, F2, F3 (host) and F4 (topology) applied by
`patch_proto.py` (the diff is `proto.diff`, 92 lines). They are linked ahead of a copy of the archive with those two members
removed (`libsa1_proto.a`) by `build_proto.py`. LIB's sources and archive were not touched.

### 6.2 Charge probe, before and after

| | Before (`probe_8D_8E.log`) | After (`probe_proto.log`) |
|---|---|---|
| Sparkle width at pair 42 | 64.00 units | 12.40 units |
| Sparkle width at pair 43 | 64.00 units | 11.60 units |
| Aura draw | `src 0 flags 0x0020 blend 1/0 mat 0 col 255,255,255,255` | `src 1 flags 0x0211 blend 1/1 mat 0x26182400 col 0,31,255,255` |
| Ribbon triangles (21-point strip) | 17 (fan, truncated) | 19 (strip) |

### 6.3 Regression

`lsd_audit` with and without the prototype dumps every triangle draw whose source is not a 3D polygon (6 characters,
24 scripts, 102,538 draws). `diff` shows exactly 10 changed lines, `Dsonic|charge`: the aura in pairs 75-84. Sprite widths
change only in Sonic's charge script; every other script's sprite widths are identical (`r_orig.txt` / `r_proto.txt`).

## 7. Audit: every character's special moves and upgrades

Draw kinds, and how each one is handled before the fixes:

* **Hand-written renderer** (8C037098 / 8C03700C / 8C036F12 -> 8C036BC0): handled, including the constant-material alpha.
* **Ninja model draws** (njDrawModel / njDrawObject): handled.
* **NJMOTION**: F2.
* **Sprites**: scaled ones handled; unscaled ones need F1.
* **njDrawTriangle3D**: F4.
* **njDrawPolygon3D**: handled for the attr 0x60 the characters use.
* **Lines**: F5.
* **Env maps**: libsa1 flags `SA1_DRAW_ENV_MAP` and G-SA1 sphere-maps them (`session.cpp`, `prepare_view`). Not visually
  verified.
* **2D sprites**: not emitted.

Sources:

* **"measured"**: seen in the `lsd_audit` run (`audit_summary.txt`). The scripts are in `lsd_audit.c` `k_chars`; the run is on
  DISC EC1 with no objects.
* **"static"**: from the draw-site scan of every character-code function that reaches a draw entry point (`draw_sites.py` ->
  `draw_sites.txt`, 98 functions) and the decompiled C. Not exercised.

### Sonic (equipment 0x7: Light Speed Shoes 0x2, Crystal Ring 0x1, Ancient Light 0x4)

| Effect | Game draw | Status |
|---|---|---|
| Light Speed charge sparkles | `func_8C0B58E0`, njDrawSprite3D attr 0x20 (unscaled), 64 px x 0.2, njScale(timer / 32) | **broken: size 5-160x** (measured), F1 |
| Charge sparkle streaks | `func_8C0B504A`, njDrawTriangle3D 0x80000062 strip, `lcharge01`/`03` | **broken: fan and untextured** (measured, 643 records), F4 |
| Charged aura | `func_8C0B4F00`, 3 x 8C60E49A, material ONE/ONE unlit, offset material | **broken: opaque white** (measured), F2 + F3 |
| Spin dash ball shell | `func_8C0B4436`/`4600`/`4724` -> 8C036F12 `Sonic_Jumpball_Effect`, ONE/ONE with alpha offset | ok (measured: flags 0x371, additive) |
| Spin dash / jump dash trail | `func_8C0B4A48` -> 8C036F12 `Sonic_Effect_Trail` and heap copies with `s_dash*`, SRCALPHA/ONE | ok (measured) |
| Crystal Ring (worn), Light Speed Shoes | hand renderer, env map | ok (measured: env map flag) |
| Bubbles (underwater) | `func_8C0B3F2C`, 8C09E778 attr 49 (scaled), `sp.sy = size * aspect` | minor: the game draws `sx x sx` (8C09E880 quirk), ours `sx x sy`, F1 |
| Light Speed Dash (mode 6) / Attack (mode 44, Ancient Light) | the aura stops (flag 0x200 cleared, `code_8C0B11C0.c:124-137`); then the dash and ball effects | not exercised: needs rings or enemies |

### Tails (Jet Anklet 0x8, Rhythm Badge 0x10)

| Effect | Game draw | Status |
|---|---|---|
| Flight, tails spin, afterimages | hand renderer, constant material alpha -0.667 (`func_8C0BA820`) | ok (measured) |
| Tail attack trail | `func_8C0BFE00` / `func_8C0C023E`, njDrawPolygon3D(4 points, 0x60), ONE/ONE, vertex colours | ok (measured: 2464 records, fan of 4 = quad) |
| Jet Anklet (worn), Rhythm Badge (env map) | hand renderer | ok (measured) |
| Snowboard | `func_8C0BF92C`, 8C60E080 (NJMOTION) | hand semantics, changes with F2 (static) |

### Knuckles (Fighting Gloves 0x20, Shovel Claw 0x40)

| Effect | Game draw | Status |
|---|---|---|
| Punch trail / hit | `func_8C0CA094` (D_8C3534BC, attr 0x32, scaled), `func_8C0CA232` (D_8C353510, 0x32) | ok (measured) |
| Punch effect model | `func_8C0CB628` -> 8C036F12 `Knuckles_Punch_Effect`, SRCALPHA/ONE, cmat alpha | ok (measured) |
| Maximum Heat model | `func_8C0CB7A0` / `8C0CB870` -> 8C036F12 `Knuckles_Maximum_Heat_Effect` | handled kind (static) |
| Maximum Heat ribbon | `func_8C0CBA40`, njDrawTriangle3D(16 tris, 0x62) strip, red | **broken: fan** (static), F4 |
| Orbiting sparkles | `func_8C0CB380`, D_8C353530 attr 50 (scaled) | ok (static) |
| Spark (no caller found, `code_8C0CA040.c:16-18`) | `func_8C0CA454`, D_8C3534F0 0x2A unscaled, 16 px x 0.1 x njScale(4 - c) | would be 2.5-3.3x too large (static), F1 |
| Dig: drifting dirt | `func_8C0CA680`, D_8C3534F0 0x32 (scaled) | ok (static) |
| Dig: dirt clods | `func_8C0CAAF8`, D_8C353564 attr 32 unscaled, njScale(sx, sy, sx), size 1..2, +-1 flip | **wrong size**: game 3..10 units stretched, ours always 4 (static), F1 |
| Debris | `lbl_8C0CAD98`, D_8C353584 0x20 unscaled, 0.25 x 4 px, tumbling | **4x too large**, billboarded (static), F1 / owner question 2 |
| Gloves / Shovel Claw (worn) | hand renderer, env map | ok (measured) |
| Glide, climb | body variants; climbing dust from the particle manager (scaled) | ok (measured body; dust fixed in 0.4.0) |
| Emerald radar | `func_8C0CAC28` njDrawSprite2D, `func_8C0C967A` lines | 2D HUD not emitted |

### Amy (Long Hammer 0x80, Warrior Feather 0x100)

| Effect | Game draw | Status |
|---|---|---|
| Hammer shockwave | `func_8C0DFCBC` -> 8C036F12 `Amy_Shockwave_Effect`, SRCALPHA/ONE | ok (measured) |
| Hammer hit spark | `func_8C0DF540`, D_8C4BF078 0x32 (scaled) | ok (measured: 256 records) |
| Expanding quad ring | `func_8C0DF9F8`, njDrawPolygon3D(4, 0x60) x 12 | ok (static; same rule as Tails) |
| Fading sprite | `func_8C0DF100`, D_8C4BF078 0x22 unscaled, rotated ang.y/ang.z | size ok (4 units), tumble lost (static) |
| Feather ring | `func_8C0DF760`, D_8C4BF0C0 cell 0 attr 0x28 unscaled (`sp->sx` pulses 1 +- 0.3, fades to 0.1) + cell 1 0x32 | **pulse and fade lost** (ours keeps 2 units) and spin lost (static), F1 |
| Long Hammer, Warrior Feather (worn) | hand renderer, env map | ok (measured) |

### E-102 (Laser Blaster 0x200, Jet Booster 0x400)

| Effect | Game draw | Status |
|---|---|---|
| Targeting lines | `func_8C0D553A`, njDrawLine3D(0x40) pairs | ok (measured: 790) |
| Lock-on marker and count | `func_8C0D5BE0` / `func_8C0D5E80`, D_8C423260 0x22 unscaled, 8 px, corner-anchored | ok size (static); orientation **UNVERIFIED** |
| Laser shots | `func_8C0D3C9A` (D_8C422DA8 0x30 scaled), `func_8C0D4320` (D_8C422DC8 48 scaled) | ok (static; needs targets) |
| Blaster ring | `func_8C0D43DA` -> 8C036F12 `E_102_Gamma_Blaster_Effect`, ctl3d set to CONSTANT_MATERIAL (`code_8C0D4250.c:131`) | hand renderer takes cmat alpha only: **UNVERIFIED** for mode 0x10 |
| Hover jet effect (Jet Booster) | `func_8C0CFACE` -> 8C036F12 `E_102_Gamma_Hover_Jet_Effect`, ONE/ONE | ok (measured) |
| Lock-on cursor, timer | `func_8C0D4B00` / `4B70` / `4D08` / `50AC` / `60C0`, njDrawSprite2D | 2D HUD not emitted |

### Big (Life Belt 0x1000, Power Rod 0x800, lures)

| Effect | Game draw | Status |
|---|---|---|
| Fishing line | `func_8C0EC180`, njDrawLine3D(11, 0x42) connected | **broken: 6 dashes** (measured record: 12 points, n 11), F5 |
| Lure (gold lure env map), cast circle and cursor | 8C036F12 hand renderer | ok (measured) |
| Fishing sprites | `func_8C0EB2AC`, FISHING tex 57/58 attr 0x20/0x21 unscaled, sx 1 | ok size (measured: matrix scale 1.0) |
| Fishing HUD | `func_8C0E6F04` ... `func_8C0EBAB2`, njDrawSprite2D | 2D HUD not emitted |
| Life Belt, Power Rod | hand renderer | ok (measured) |

### Shared effects and upgrades still to come

* **Barrier / magnetic barrier / invincibility** (`func_8C043E70`, item power-ups, not character code) uses
  njDrawTriangle3D(2, 0x80000062): a textured strip quad. Today it is broken (a fan, untextured, static); F4 fixes it.
* **Super Sonic** (round 6, B_CHAOS7 functions `func_0C915340` ... `func_0C91E1D2` in `draw_sites.txt`): njDrawSprite3D sites
  with unknown attrs (asm only), ctl3d edits (8C056986), and the aura models 0x0C90C620 / 0x0C90CFA4 / 0x0C90D928. Those are
  textured SRCALPHA/ONE, DOUBLE_SIDE, IGNORE_LIGHT, with `nbMat` 1, so they do not need F3. Re-run `lsd_audit` once Super Sonic
  ticks.

## 8. Unverified and open points

* **The exact Ninja colour arithmetic** of OFFSET_MATERIAL with IGNORE_LIGHT in 8C6108B0 (clamping and rounding). The
  prototype uses libsa1's existing `ds_channel` rule (`sa1_pose.c:858-866`). A harness driver that records the TA FIFO
  (`sh4_set_io` / `sh4_evlog`, SQ events) while the original 8C0B4F00 runs would settle it, together with the on-screen sprite
  quad. Not done here.
* **The meaning of attr 0x20** in njDrawTriangle3D (it picks 8C64FC68/8C64FAE8 over 8C650144/8C64FE0C).
* **The polygon UV scale** (assumed 1/256).
* **Whether the hand-written renderer** applies CONSTANT_MATERIAL (0x10) to RGB (E-102's blaster ring).
* **Not exercised dynamically**: Maximum Heat, digging (it needs `D_8C19E390` and ground attribute 0x100, `code_8C0C80D4.c:21-29`),
  E-102's laser shots (they need targets), Amy's feather ring, the Light Speed Dash and Attack (they need rings or enemies),
  and the snowboards.
* **8C0B5690 and 8C0B5760** (other users of the ribbon task) have no caller in the scanned images.

## 9. Scratch files (`sadc/build/research/lightspeed_visuals/`)

`dump_models.py`, `dump_aura_raw.py`, `dump_sprites.py`, `scan_matrange.py` (data dumps); `draw_sites.py` -> `draw_sites.txt`;
`lsd_probe.c` -> `probe_8D_8E.log` / `probe_proto.log`; `lsd_audit.c` -> `audit.log`, `audit_proto.log`, `audit_summary.txt`
(`analyze_audit.py`), `draws_orig.txt` / `draws_proto.txt`, `r_orig.txt` / `r_proto.txt`; `build_probe.py` /
`build_proto.py` (zig, x86_64-windows, LIB snapshot under the build lock); `patch_proto.py`, `proto/`, `proto.diff`;
`vpk_find.py` (read-only GMod VPK listing).

## Review (adversarial)

Round 4c adversarial pass, 2026-10-21. Read-only. Nothing in LIB, g-sa1, SADC/src, include, tools, harness or config was
changed. The reviewer's scratch is in `sadc/build/research/lightspeed_review/`:

* `dump_snowboard.py` dumps the materials of Sonic's snowboard (S_SBMOT, linked at 0x0CB08000);
* `dump_texlists.py` dumps the texlist sizes of the textured 3D-polygon draws.

The investigator's `dump_aura_raw.py` and `scan_matrange.py` were re-run and gave the same output. LIB was read as of its
2026-10-20 timestamps (`sa1_capture.c` 14:17, `sa1_pose.c` 14:47, `rt_draw.c` 14:32). Round 4b had not touched these files
yet when this review was written, but it is editing capture and pose concurrently (R.3 G2).

### R.1 Verified (independently re-derived)

* **Sprite dispatch** (8C63EF74). `attr & 0x10` goes to 8C63F038, otherwise to 8C63F2D4 (8C63EFE4-8C63EFF2).
* **Unscaled sprite path** (8C63F2D4):
  * UV = s16 x 1/256 (`L8C63F3B8` = 0x3B800000, 8C63F2F4-8C63F31A);
  * H/V flips at 8C63F31C-8C63F330;
  * corner offsets `-cx*sp->sx`, `(sx-cx)*sp->sx`, `-cy*sp->sy` and `(sy-cy)*sp->sy` (8C63F332-8C63F370), added to `sp->p` in
    the local plane (8C63F3C8-8C63F3E2);
  * `FTRV XMTRX` on all four corners (8C63F3F2, 8C63F404, 8C63F41A, 8C63F42C).

  The quad's size is therefore `tanim px x sp->sx x |M e_x|`. The scaled path's UV scale is also 1/256 (8C63F058).
* **libsa1 sprite emitter** (`emit_sprite`, LIB `sa1_capture.c`): sx = sy = 1 unless attr has 0x10 (lines 551 and 563-566);
  rect at lines 598-601; UV /255 at lines 571-574; only the anchor goes through the matrix (line 596).
* **Sparkle draw and the measured ratios.** The sparkle matrix and draw call match `code_8C0B58E0.c:20-44`.
  `probe_8D_8E.log:168` reports 423 records, mean 21.77x and max 160x. The prototype's rects in `probe_proto.log`
  (24.80 / 23.20 host units) match.
* **8C09E880 sx-only quirk.** The FR4 and FR5 loads both use `MOV #12,R0` (8C09E904-8C09E90C).
* **njDrawTriangle3D (8C63D66C):**
  * stride is 1 if `attr & 2`, else 2 if `attr & 1`, else 3 (8C63D716-8C63D732);
  * `v0 = (attr & 1) ? 0 : i*stride` (8C63D758-8C63D7AE);
  * `v1 = 1 + i*stride` and `v2 = v1 + 1` (8C63D7C4-8C63D7F6, 8C63D922-8C63D93A);
  * n counts triangles.
* **njDrawPolygon3D (8C63D414).** It transforms n points (8C63D480-8C63D494) and clips them once as one polygon
  (8C64F840, called at 8C63D542).
* **libsa1 swaps the two.** `rt_draw.c:633-641`, `sa1_capture.c:735`, `744-746` and `808`; `rt_texture.c:50-53` and `81`.
* **njDrawLine3D (8C63CE24):**
  * `stride = (attr & 0xF) ? 1 : 2` (8C63CEF2-8C63CEFC, 8C63CF4C);
  * first point `(attr & 1) ? 0 : i*stride` (8C63CF82-8C63CF96).

  libsa1 keys on 0x20 instead (`sa1_capture.c:686`, `715`), so Big's 0x42 line becomes 6 pairs. F5 is correct.
* **Material ids beyond nbMat.** `scan_matrange.py` re-run: of 550 models, 21 have a material id >= nbMat: the 20 aura node
  models and Eggman's 0x8C57A694. The aura material at 0x0CC00000 is diffuse FF000000, attr 26182400.
* **libsa1 bounds the material id and uses hand rules:**
  * `rt_draw.c:433` (nmat = nbMat) and `sa1_pose.c:1054`, `1170`;
  * `sa1_capture.c:463` (`hand` set for NJMOTION);
  * `ds_color` (`sa1_pose.c:869-885`): white RGB when hand;
  * `sa1_draw_classify` adds PALETTE_LIT for hand (`sa1_pose.c:906-907`).
* **Regression dump.** `draws_orig.txt` and `draws_proto.txt` both have 102,538 lines. Exactly 10 differ, all
  `D|sonic|charge` draw 49 at ticks 75-84. In `r_orig.txt` vs `r_proto.txt`, only Sonic's charge line changes:
  sprite-width-sum 73728.0 -> 7065.6, tris 130069 -> 131319.
* **G-SA1:**
  * `session.cpp:888` (the white rule) and `920-944` (sprite quads);
  * `cl_textures.lua:373-407` (`vgui/white` base) and `cl_render.lua:108` (OverrideBlend in pass 1);
  * `gsa1_vertexcolor` defaults to 1 (`sh_config.lua:128`) and has its Q-menu checkbox (`cl_menu.lua:184`). F6 therefore
    only matters when the owner turns that option off.
* **Game code:**
  * aura call site `code_8C0AE0E0.c:144-145`;
  * charge completion in `func_8C0B14E0` (`code_8C0B11C0.c`);
  * pulse and pass colours `code_8C0B4F00.c:89-113`;
  * ribbon draw `code_8C0B4F00.c:128-136`.
* **Texlist sizes.** All the texlists the textured 3D-polygon draws select have exactly one texture (`dump_texlists.py`):
  D_8C1C6A94 { lcharge01 }, D_8C1C6A9C { lcharge03 }, and the barrier's D_8C19B630 (its name pointer is 0 in the image).

### R.2 Refuted or corrected

1. **8C60E49A does not draw through njDrawModel or 8C6108B0** (sections 3.3 and 3, F2 wording). That chain holds only for
   8C60E080: walker 8C60DDE4 calls `njDrawModel` (literal `L8C60E004`, call at 8C60DE6E-8C60DE72), which runs 8C611028 and
   then 8C6108B0.

   8C60E49A, the call the aura uses, walks the tree with its own walker `func_8C60E31C` (BSR at 8C60E5CA). That walker's
   node draw is `func_8C621574` (literal `L8C60E540`, call at 8C60E3AA), which runs:
   * begin 8C605DAC;
   * `func_8C617C34` (per-model light transform), which tail-calls `func_8C6215BA` and then **`func_8C6209A8`**;
   * end 8C605E0A.

   The xref list cited in 3.3 (the callers of njDrawModel) does not contain 8C60E31C. **The conclusion survives.**
   8C6209A8 also indexes `mats + (id & 0x3FFF) * 20` with no nbMat read (8C620A30-8C620A62; there is no `@(22,Rn)` in
   `8C620428.src`, `8C617C34.src`, `8C620F2C.src` or `8C60E31C.src`).

   8C6209A8 also settles most of section 8's colour question for the aura:
   * the diffuse bytes are scaled by 1/255 (`L8C620B78` = 0x3B808080) into float A, R, G, B;
   * ctl3d 0x10 replaces them with B_8C88F668 (8C620B32-8C620B5C);
   * ctl3d 0x20 **adds B_8C88F668 to all four channels** (8C620B8C-8C620BC2).

   Pass 1 is therefore (1.0, 0, 0.28 - 0.18f, 1.0) before packing, as the prototype assumes. The final clamping is still
   **UNVERIFIED**.
2. **ctl3d bit 0x200 is not a no-op for textured materials** (3.1 labels it "CNK_CONSTANT_ATTR"). 8C6209A8 tests it at
   8C620A94-8C620A9A. For a material with USE_TEXTURE (0x00200000) and ctl3d & 0x200, it sets RGB = 1.0 and takes only the
   diffuse alpha (8C620A9C-8C620AB8). Its name is probably NJD_CONTROL_3D_CONSTANT_TEXTURE_MATERIAL (**UNVERIFIED**). SA1
   runs with 0x36C, so the bit is set.

   F2's "material colour ... on all four channels" is therefore wrong for **textured** NJMOTION materials. libsa1's non-hand
   `ds_color` (`sa1_pose.c:878-884`) uses the diffuse RGB. 8C6108B0, the 8C60E080 path, takes only the alpha from the
   diffuse in its textured branch (8C610AAA-8C610ABC, into D_8C6735A8); where its RGB comes from is **UNVERIFIED**.

   Measured snowboard (S_SBMOT object 0x0CB37454): one material, diffuse FFFFFFFF, attr 94218400 (textured, lit, id < nbMat).
   For the snowboard only the lighting model changes, not the colour.
3. **attr 0x20 is "fill", not a vertex-colour mode** (2.5 and section 8). Without 0x20, 8C63D414 and 8C63D66C submit through
   8C64FE0C (untextured) or 8C650144 (textured). Both draw every edge of the clipped polygon as a screen-space line quad:
   * the perpendicular is normalised with FSQRT and scaled by the constant 0.525 (0x3F066666, `L8C65013C`), the same
     constant njDrawLine3D uses (`L8C63CF8C`);
   * the closing edge (0, n-1) comes first, then (i-1, i);

   so the result is an outline. With 0x20 set, 8C64FAE8 and 8C64FC68 submit the clipped polygon as **one TA strip in the
   order 0, n-1, 1, n-2, ...** (8C64FB14-8C64FC3E).

   libsa1's `NJ_DRAW_FAN 0x20` (`sa1_capture.c:43`, "a fan around point 0; lines: a connected strip") is wrong for both uses.
   8C63CE24 itself only tests 0x0F, 0x1, 0x40 and 0x80000000. Every polygon site in character code passes 0x20 (0x60, 0x62,
   0x80000062; `draw_sites.txt`), so there is no visible effect today. F4's "a fan whatever the attr is" is still wrong.
4. **The polygon UV scale is verified** (section 8, F4.3). 8C64FC68 multiplies the NJS_TEX s16 v, then u, by `L8C64FDF4` =
   0x3B800000 (8C64FCB6-8C64FCD0). Drop the UNVERIFIED mark.
5. **njDrawPolygon3D fill is not a fan.** The SDK's zig-zag order gives (0, n-1, 1), (n-1, 1, n-2), ...: for a quad the
   diagonal is 1-3, while libsa1 and F4 use diagonal 0-2.

   Tails' tail quad is non-planar: point 0 is the old tip, 1 the new tip, 2 this frame's centre, 3 the last frame's centre
   (`code_8C0BFE00.c:98-105`). Its colours are outer on 0 and 1, inner on 2 and 3 (lines 86-97). So the surface and the
   Gouraud interpolation differ. The "ok (measured)" in section 7 checked counts only.
6. Minor corrections:
   * **3.2:** the aura skeleton is not "the same" as Sonic's main model. `dump_aura_raw.py` prints 17 of 73 nodes with a
     different pos, ang or flags from 0x8C1CE76C; only the node count matches.
   * **Section 4 header and 5.4:** F5 does not touch `src/rt`; it is host-only (`emit_lines`).
   * **3.5:** the diff covers ticks 75-84, not 75-82.
   * **F4.1:** `sa1_capture.h:62-63` carry no kind comments; the wrong comments are in `rt_draw.c:633-641`.

### R.3 Gaps

* **G1: the game widths are modelled, not measured.** `lsd_probe.c:64-70` computes the "game width" with the same formula F1
  implements, so the prototype matches it by construction. The asm reading of 8C63F2D4 is sound (R.1), but only a harness
  TA/store-queue log of one sparkle's four vertices would measure the original. Section 8 already lists this; keep it
  marked as such.
* **G2: concurrency with round 4b.** 4b is editing capture and pose, the same files F1-F5 patch. Every line number in this
  doc and in `proto.diff` will drift, and capture_test "part 10" may collide with a part that 4b adds. Re-base the hunks and
  re-run the `lsd_audit` regression on the post-4b archive before landing anything.
* **G3: the texture-number design is incomplete.**
  * In the original, the sprite and model draws select textures themselves (8C63EF74 -> 8C606A88(texid); 8C6209A8 and
    8C6108B0 -> 8C606A88). The rt versions do not go through `njSetTextureNum` (`rt_draw.c:621-625`), so a "current
    texnum" static goes stale after them.
  * Recording n and resolving it against `r->texlist` at the draw also ignores a `njSetTexture` that comes between the two
    calls.
* **G4: the rt half of F3 adds risk for no gain.** It sizes the snapshot from data (max id + 1, up to
  `RT_CAP_MSNAP_MAXMATS` x 20 bytes) and reads it **inside the world**, where an out-of-range read faults and kills the
  world. The host fallback (a bounded reader) already fixes the aura, and no model with id >= nbMat has its material
  edited before a draw.
* **G5: F2 cannot tell its two sources apart.** 8C60E080 records (8C6108B0) and 8C60E49A records (8C6209A8) are both NJMOTION
  (`rt_draw.c:825-849`), and R.2.1 and R.2.2 show that their colour pipelines differ. The textured-material issue of R.2.2
  also affects the existing NJMODEL and NJOBJECT records. That problem predates this work and is out of scope, but should be
  recorded.
* **G6: aura pass 1 may z-fight.** Pass 1 (scale 1.0) lies on Sonic's body with a different, coarser mesh. In G-SA1, an
  additive translucent pass drawn over the opaque body is likely to show depth artefacts. The Dreamcast's depth compare for
  the translucent list is **UNVERIFIED**. The owner's in-game check (5.6) should look for this.
* **G7: zero-size sparkles.** Each sparkle's last draw has njScale(0); `probe_8D_8E.log` has 3 such records, excluded from the
  mean. F1 should emit nothing for a zero extent, and the part 10 assertion must skip these records.
* **G8: missing tests.**
  * Unscaled sprite with NJ_SPRITE_ANGLE under a non-uniform matrix: the result is a parallelogram that rect + angle cannot
    express.
  * Outline (non-fill) polygons and the zig-zag fill order.
  * The snowboard: only a visual check is planned, but numeric checks are possible (no PALETTE_LIT; RGB 255 because the
    diffuse is white).
* **G9: model UVs.** The SDK's model draw also uses 1/256 (8C6108B0, `L8C610B44` = 0x3B800000). libsa1's model paths divide
  by 255 (`sa1_pose.c:462-463` evaluator, `1210-1211` emitter). F4.3's "the model and sprite paths use 1/256" describes the
  SDK, not libsa1. The hand renderer's (8C037098) UV scale is **UNVERIFIED**, so do not change body UVs without checking it.
* **G10: scan scope.** `scan_matrange.py` covered the 1ST_READ, S_MOT and B_CHAOS7 assets only. S_SBMOT was spot-checked here
  (id < nbMat). M_SBMOT and the other overlays were not scanned.

### R.4 Required changes to the work plan

* **F1:** keep as proposed. Add:
  * emit nothing when an unscaled sprite's extent is 0 (G7);
  * cite functions, not line numbers, and re-base on 4b (G2).

  Treat UV /256 for models (G9) as a separate, later item with its own regression.
* **F2:** correct the stated rationale to the two chains of R.2.1. Then either:
  * (a) have the rt's 8C60E49A wrapper set a new record flag bit (the `flags` word exists, so the layout does not change)
    and give only those records 8C6209A8 semantics; or
  * (b) make the non-hand `ds_color` follow 8C6209A8 for every Ninja record. A textured material with `ctl3d & 0x200`
    gets RGB 255 and the diffuse alpha; otherwise it gets the diffuse colour. CONSTANT_MATERIAL then replaces all four
    channels, and OFFSET_MATERIAL adds to all four. This needs the record's ctl3d bit in `sa1_draw_state`.

  Add a part 10 assertion for the snowboard: no PALETTE_LIT and RGB 255.
* **F3:** land the host half only, as proto hunk 5. Drop the rt snapshot extension, or defer it until a model needs it. If it
  is kept, take it only when id >= nbMat and bound it to the arena (G4).
* **F4:**
  * Rename `NJ_DRAW_FAN 0x20` to `NJ_DRAW_FILL 0x20`, and add `NJ_DRAW_FAN 0x1` and `NJ_DRAW_CONNECTED 0x2`.
  * Without FILL, emit the edges (the polygon's closed loop, or each triangle's 3 edges) as `SA1_PRIM_LINES`, or skip them
    and count them.
  * Fill njDrawPolygon3D in the SDK's zig-zag order (R.2.5). Implement the 0x1-only stride-2 case: proto hunk 3 sends it
    down the independent-triangle path, but it should be (0, 1+2i, 2+2i).
  * Polygon UV /256 is verified (R.2.4).
  * Texture, in two phases:
    1. Host-only, with no record change: when the draw's texlist has exactly one texture (every observed case, R.1),
       resolve texture 0, the only valid number.
    2. Only if a multi-texture case appears: an rt hook that records `&texlist->textures[n]` (the NJS_TEXNAME guest
       address) at `njSetTextureNum` and is invalidated by the rt sprite and model entries (G3). The record bump to
       0xD4 / version 5 then goes through pclower (pcdata comes from `sa1g_pcdata.c`) and `build.py --test`.
* **F5:** correct as proposed, and host-only (R.2.6).
* **F6:** correct. It is low priority, because it only applies with `gsa1_vertexcolor` 0.
* **Tests:**
  * keep part 10 but re-number it against 4b;
  * skip zero-size sparkles;
  * add the outline and zig-zag polygon cases and the snowboard flags;
  * the strict 10-trace replay check is needed only for an rt change (F2(a), or F4 phase 2).
