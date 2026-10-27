# G-SA1 0.3: falling through ramps, and the freezes

Research for the owner's report on G-SA1 0.3 (2026-10-20): "2. Going up a ramp too fast makes you fall through it.
3. The game is randomly freezing up (I believe it may be related to collision in a new map)". The owner also said:
"I think the collision should be generated in the same way G64 is". Item 1 (the ball animation) is not covered here.

**Status 2026-10-21.** The fixes of section 6 were made in round 4b (section 7: libsa1's GRID cutting, object split,
descending-z order, the 256-entry NEAR list, the matrix stack; G-SA1's one-way thin platforms, thin walls, buried
terrain, G64's sources, the map job; 48 -> 0 ramp falls on gm_freespace_13). Round 4b2 (sections 8 and 9) found the root
cause of mechanism B - the game's TOUCH test prefers a polygon touched from behind, so a slab's underside within the
sphere's reach wins over its top - and fixed it in libsa1's GRID (undersides within reach left out: ramp_diag `slab`
10 -> 0 of 264, `under` 3 -> 0), moved the GRID synthesis and the world images off the thread that creates characters
(`sa1_collision_prepare`, `sa1_character_prepare`: creation 2-3 ms instead of 125-253 ms after a map load and 56-59 ms
for each kind's first world) and gave tools the NEAR list (`sa1_debug_near_polys`).

Everything below was measured offline with two new diagnostic programs. Existing LIB and g-sa1 sources were not
changed:

* `LIB/tests/ramp_diag.c` with `LIB/tests/build_ramp_diag.py` uses synthetic scenes and the public API. It takes a
  snapshot of `build/x86_64-windows/libsa1.a` and its headers under the libsa1 build lock.
* `g-sa1/tests/diag_maps.cpp` with `g-sa1/tests/build_diag_maps.py` uses real maps. It follows the module's path
  (`Bsp::build_world` with the default options, then libsa1 in SOURCE coordinates as `gsa1::Session` drives it). It
  snapshots `bsp.*`, `util.*` and libsa1 the same way.
* `LIB/tests/stage_geometry.py` computes statistics of SA1's own landtables
  (`sadc/build/traces/land/*.obj`).

The "new map" is **gm_freespace_13**. The boundary_warp addon wrote `data/boundary_warp/gm_freespace_13_v20.json` at
12:43 today, and G64 also holds a cache for it. Its BSP is inside `garrysmod/cache/workshop/115510325.gma` and was
extracted to the scratchpad for testing. Other maps tested: gm_construct, gm_flatgrass (game folder), and from the
owner's workshop/cache `.gma` files gm_genesis, gm_bigcity_improved_rp, gm_goldencity_v2_day, gm_flatgrass_destroyable,
gm_driversheaven_tdm, gm_sonic_zone_one, gm_sonic_abilities_1 and awm_bigcity. From addons/skategm_maps:
sgm_skate3_industrial and sgm_skate3_portcarverton. Ten of them appear in the owner's data/boundary_warp, so the
owner has played them.

Units: Hammer units (HU) on maps and SA1 units in the game. 1 SA1 unit = 4 HU. Speeds are in SA1 units per 60 Hz step.
Sonic's land-collision sphere has radius 5 (height 10), Tails 4.5, Knuckles 5.7, Gamma 10.5, Big 8.5 (physics table
8C1BDFE4). Sonic's top running speed is about 4.4, a spin dash reaches about 7-10, and the speed limit (`lim_h_spd`)
is 16.

---------------------------------------------------------------------------------------------------------------------

## 1. Summary

### Item 2, falling through ramps: two mechanisms, both caused by how map geometry reaches SA1's collision

The game's ramp physics is not the cause. Plain synthetic ramps never let anyone through. That holds for 10-70 degrees,
for ramps made of 2 huge triangles or of 32-unit tiles, for every GRID cell / max_edge / CUT setting, for STAIRS
flags, and at any speed up to 16. Counts: `ramps` 308 runs, `threshold` 660, `params` 90, `smooth` 60, `stairs` 80;
0 fell through.

**A. NEAR-poly overflow from needle triangles (the main cause, gm_freespace_13 and the city maps).**

* GMod ramps and roads are long, narrow faces: planks, rails, beams, kerbs and road strips. gm_freespace_13's big ramp
  is a 12 HU plank, 5312 x 236 HU, on beams.
* GRID splits every triangle at the midpoint of its longest edge until all edges are at most 128 SA1 units
  (`sa1_coladapt.c` `ca_grid_split`). On a long thin triangle this produces **needles**: pieces 364 HU long and
  0.5-16 HU wide, each with a bounding sphere of about 60 SA1 units.
* The game's NEAR query 8C028BFE takes every polygon whose centre is within sqrt(1.3 (r^2 + R^2)) of the probe. With
  such large R, every needle on every layer of the ramp (top, underside, rails, beams) passes.
* At the failing frames the query would have collected 196-211 polygons. The list holds 96.
* The plank's own top drops out, TOUCH is 0, and the character falls. This happens at every speed, even standing on
  the middle part of the ramp.
* The same mechanism makes characters fall through long flat road and pavement strips on gm_bigcity_improved_rp,
  gm_goldencity_v2_day and gm_driversheaven_tdm, right after spawning.

**B. Entry dip plus thin plank underside (speed dependent).**

* At high speed (spin dash or boost, about 9 units per step or more), the first step from flat ground onto a slope
  leaves the character 5-10 units inside the slope. This is the game's own landing logic.
* A solid slope pushes the character back out on the next step, so this is only a one-frame dip.
* A thin plank is different. Its underside lies within a sphere radius of the dip, and single-sided polygons are
  touched from behind within the radius (`fabsf(p.y) <= radius` in 8C029B00).
* The underside therefore wins and pushes the sphere out **below** the plank. The character then runs on under the
  ramp.
* Reproduced on synthetic plank ramps (`slab`, `under`, `case`: Knuckles at 30 and 45 degrees, Sonic spin dash at 45
  degrees). Seen as a dip on gm_construct (ramp #3, a 16 HU plank) and as the remaining foot-of-slope falls on
  gm_freespace_13 once A is fixed.

**Emulated fixes**, using diag_maps options that change the triangles before `set_static`:

| map (ramp runs / falls) | as 0.3 | well-shaped split `--tess 256` | no thin-slab undersides `--drop-under 24` | both |
|---|---|---|---|---|
| gm_freespace_13 (240) | **48** | 4 | 3 | **0** |
| gm_bigcity_improved_rp (200) | 19 | - | 19 | **0** |
| gm_goldencity_v2_day (200) | 14 | - | 14 | **0** |
| gm_driversheaven_tdm (200) | 28 | - | 29 | 10 (see 3.6) |
| random play, gm_freespace_13 (120 runs x 15 s) | 13 runs fell, NEAR full in 19 | | | 3 runs fell, NEAR full in 0 |

Variants that do **not** help:

* Smaller GRID `max_edge`: 64 gives 38 falls. 32 exceeds GRID's 4M-fragment limit, and boot fails.
* Thicker thin floors (`thin_floor_min` 24 / 48): 41 / 36 falls.
* Dropping slivers under 2 HU: 48 falls.
* No hidden-face removal: **80** falls.

### Item 3, the freezes: not reproduced in libsa1 or the G-SA1 core; one measured stall source and a list of suspects

No hang and no slow tick was found:

* Coverage: 13 maps, more than 1.5 million `sa1_character_tick` calls, several thousand character creations (every ramp run and random
  run creates one), and every
  playable kind with random input, spin dashes, jumps and water. A watchdog thread was set at 20-60 s.
* Longest tick 8.6 ms, longest `sa1_character_render` 1.7 ms, mean tick 0.03-0.08 ms.
* Map conversion takes at most 133 ms (684 ms on a cold file cache). The first character with the GRID synthesis
  takes at most 365 ms. Later creations take 5-35 ms.

Candidates outside that path, in order of evidence:

1. **Audio clip loads run on the Lua thread.** The mixer reads the whole ADX file from the disc image when a jingle,
   music or voice clip starts (`sa1_audio_mix.c` `start_clip` -> `sa1a_read_adx`), called from `Session::take_events`
   -> `Audio::event`.
   * Measured cold: 27-430 ms per music file (AMY.ADX 429 ms, BIG.ADX 340 ms, E102.ADX 277 ms). Warm: up to 13 ms.
   * The drowning countdown TIMER.ADX (jingle 0x60) started 38-68 times per 60 underwater runs.
   * This matches "random" short freezes at events.
2. **The first spawn on a workshop map.** `cl_module.lua` `GSA1.LoadWorld` calls `file.Read` on the whole BSP into a
   Lua string for `WorldLoadData`.
   * gm_freespace_13 is 115 MB.
   * rp_nycity (1.1 GB) took 30 s to read cold from the owner's D: drive (0.4 s warm).
   * This matches "related to collision in a new map", but it is a one-time stall, not a random one.
3. A GPU readback every 2 s: `cl_textures.lua`'s device-reset canary calls `render.CapturePixels`. This would cause a
   periodic hitch, not a freeze.
4. Respawn loops after falls. Each costs about 35 ms in libsa1 plus Lua entity and texture work. Fixing item 2
   removes most of them.
5. Latent: the Ninja matrix stack leaks one slot per TOUCH-buffer overflow, and nothing resets it in a libsa1 world.
   It reached 126 of 128 in a pathological scene and stayed at 1 on every real map.

### "Generate collision the way G64 does"

G64 builds its world from the engine's vphysics collision mesh (convex hulls), plus static props and displacements.
G64's own cache for gm_freespace_13 (`data/g64/cache/gm_freespace_13_cache.dat`, parsed) holds the same 5312 HU
ramp triangles, with up- and down-facing hull faces 0.8-14 HU apart. That is exactly what breaks SA1.

SM64's floor search picks the highest floor below Mario, so it does not care. SA1's sphere/NEAR collision does. The
closest emulation, the brush faces without hidden-face removal, made it worse: 80 falls instead of 48.

**Worth taking from G64:**

* its data sources for what G-SA1 does not have at all: static props and brush entities. sgm_skate3_portcarverton
  has 6 world brushes / 50 triangles, and its ramps are props, so characters fall through them;
* its per-map disk cache;
* its background parsing.

**Not worth taking:** its raw triangles. See section 4.

---------------------------------------------------------------------------------------------------------------------

## 2. How SA1's land collision meets a slope (code)

These are the parts that matter here. All of them run unchanged in libsa1.

* **8C073160** (land collision of PSetPosition 8C072B44, code_8C0730D8.c):
  * The sphere (radius height/2) is placed at pos + up x (height/2 - pos_error x k). With pos_error 2 and k up to 1.8,
    the probe sits up to 3.6 units lower than the body centre.
  * It is swept by mwp->spd against every gathered land object (8C029B00).
  * On a landing the new position is pshbk + anaspdh, projected onto the plane through the touched point with normal
    `tnorm` (the player's up vector, not the polygon normal).
  * A landing on a new object is refused when the pushback-plus-slide vector is longer than the character's height or
    points into the polygon (`ip < -0.2 || dist > height` -> flags 0 -> wall handling 8C0747D2).
  * Both refusals and the snap along the old up vector leave the character inside a steeper slope after the first
    step at speed (the "dip": 6-10 units at speed 14.5, synthetic `case` traces).
* **8C028BFE** (NEAR, code_8C028BDC.c):
  * Takes the polygons of the gathered objects whose bounding sphere passes
    `|c - pos|^2 <= 1.3 x ((|spd| + radius)^2 + R^2)`.
  * Stops at **96** (`B_8C73E280[96]`).
  * Objects are walked in cache order. Whatever comes after the 96th is never tested.
* **8C029B00** (TOUCH, code_8C029B00.c):
  * For each NEAR polygon, sweeps the centre in steps of radius/2.
  * A touch is `|height above the plane| <= radius` and inside the triangle. For single-sided polygons this includes
    **behind** the polygon, and the pushback then goes to the front side.
  * Within one object, onpoly/pshbk are averaged over all touches. 16 touches per object at most; the overflow leaves
    one `njPushMatrix` unpopped (as on the DC).
* **8C074EE4** turns the player's up vector toward the floor by at most 22.5-45 degrees per step, depending on speed.

What SA1's own stages look like (`tests/stage_geometry.py`, collidable triangles of six landtables):

| stage | longest edge p50 / p99 | bounding radius p50 / p99 | needles (width < 5% of length) | floors with a down face below within 3 / 6 units |
|---|---|---|---|---|
| Emerald Coast 1 | 41 / 165 | 27 / 107 | 1.8% | 52 / 192 of 4536 |
| Emerald Coast 3 | 53 / 198 | 31 / 106 | 0.7% | 1 / 11 of 3849 |
| Twinkle Park 2 | 77 / 193 | 50 / 124 | 6.6% | 28 / 133 of 1701 |
| Speed Highway 1 | 34 / 377 | 21 / 224 | 1.0% | 59 / 194 of 8213 |
| Red Mountain 3 | 66 / 186 | 41 / 118 | 0.1% | 0 / 0 of 2006 |
| Final Egg 1 | 66 / 172 | 42 / 104 | 1.1% | 10 / 20 of 1939 |

* SA1 stages do have sharp floor/slope kinks: p99 35-65 degrees; 25-252 shared edges per stage where a flat floor
  meets a slope of 20 degrees or more (up to 60 degrees).
* So steep entries alone are fine, as the synthetic tests confirm.
* What SA1 geometry almost never has: floors with a down-facing polygon within one sphere radius below them, and
  stacks of parallel needles.
* The freespace ramp is both. Its plank top has its underside 3 SA1 units below, and the GRID pieces near the
  character are needles with radius 60.

---------------------------------------------------------------------------------------------------------------------

## 3. Measurements, item 2 (ramps)

### 3.1 Synthetic ramps (`ramp_diag --only ramps|threshold|params|smooth|stairs|entry`)

* Scene: flat floor, a ramp 300 units high, a plateau; 1200 wide.
* Approaches:
  * walk, jog;
  * run from 600 units;
  * top running speed (4.4);
  * spin dash (7.1);
  * ground speed set to 6 / 10 / 16 at x = -150, as a dash panel would.
* Characters: Sonic, Tails, Knuckles.
* Fall test: the feet more than 3 units below the surface for 6 steps in a row while over it.
* Result: **no fall-through anywhere**, for every angle, mesh, GRID setting, rounded foot and STAIRS-flagged slope. The
  deepest dip below the surface was 0.7 units.
* Ramps steeper than about 50 degrees are simply not climbable at running speed, and the character stops or slides
  back.

### 3.2 Plank ramps (`--only slab|under|case`)

The slope is a slab T units thick with its underside, ends and sides, leading up to a platform T thick (G-SA1 thickens
only floors thinner than 3 units).

Results (`slab`, 264 runs over 20/30/45 degrees, T 0.5-12):

* Sonic:
  * falls at the **top** edge onto a platform 0.5-1 unit thick (20 degrees, boost16);
  * at the foot, dips 5-9 units deep and comes back for T 0.5-6 (30 degrees boost16, 45 degrees boost10, spin dash).
* Knuckles (radius 5.7):
  * falls **at the foot** with T 8 (30 degrees, boost16);
  * falls at the top with T 0.5-1 (20 degrees, boost16);
  * at the foot, dips 5.5-10 deep and comes back at 45 degrees for T 0.5-6 (boost10 / boost16).
* Thick slabs (T 12) and plain surfaces never dip.
* A "fall" is the feet more than 3 units below the surface for 6 steps in a row.
* `under` keeps the slope and changes what lies under it:
  * a solid wedge, or a surface with the floor continuing under it: never dips;
  * a slab with the floor stopping at the foot: Knuckles falls (30 degrees boost16, 45 degrees boost10);
  * a slab 3: Sonic falls (45 degrees spin dash).
  * **The underside is what turns the dip into a fall.**
* Trace (`RAMP_CASE="3 30 8 0 0 0 7 -1"`, Knuckles, 30 degrees, plank 8, speed 14.5):
  * step 277: on the floor at x 0.5;
  * step 278: x 14.7, y -1.8. The ground is lost, n2 is the slope normal, and he is inside the plank.
  * step 279: n2 = (0.5, -0.87), the **underside's** normal; mode 9 (falling).
  * from step 280: running on under the ramp (depth 73 at step 286).
* `needle` (freespace's plank rebuilt in SA1 units: 1328 x 59, 24.5 degrees, 3 thick) as long triangles: the NEAR
  list is **full (96)** on every run. With the same faces cut into 32-unit tiles it reaches 62.

### 3.3 gm_freespace_13 (`diag_maps --ramps 30`)

**Load:**

* Bsp::build_world 41 ms: 3178 brushes, 8478 hidden and 4516 clipped faces, 542 thin floors thickened, 61
  displacements.
* 60,992 triangles become GRID 586,559 in 12,748 objects; first character 265 ms.

**Ramps:** 30 found, 240 runs, **48 fell through.**

* Every character and approach on the four big stunt ramps: #0-#3, 2488 / 1884 HU high, 24.5 degrees.
* Most runs on #6 / #7 (968 high, 17.5 degrees). Some of those fall on the flat run-up next to the ramp, before
  the foot, which is the same NEAR overflow.
* A few on #10 and #12.

**Where:**

* At 540-920 HU up the slope, with the NEAR list full (96) and TOUCH 0.
* The crossed polygon is the plank top, triangle 25548: (-2048 -7688 -13360) (3264 -7688 -15776) (3264 -7452 -15776),
  a 5312 x 236 HU triangle.

**Geometry around it (`--dump "2853 -7609 -15597 120"`):**

* the plank top (normal 0.41 0 0.91);
* its underside 13.2 HU lower (normal -0.41 0 -0.91), which `thin_floor_min` = 12 does not thicken: 13.2 x cos 24.5 =
  12.0;
* side strips 8 HU wide;
* slanted beam faces (normal -0.31 +-0.67 -0.67) under it;
* more undersides (25544-25547, 25556-7, 26230-1).

**The NEAR list at the failing frame:**

* 96 entries, nearly all needles: edges 364.7 / 364.7 / 0.5-16 HU, bounding radius 241-245 HU, 64-300 HU away.
* Uncapped census (`near_census`, which walks `D_8C02D548` the way 8C028BFE does): **196** qualifying polygons from 23
  land objects, 100 of them after the 96th (211 / 115 at boost10).

**Repro:**

```
diag_maps --gdi GAME.gdi --trace "3397.3 -7609.3 -15800.0 -180.0 top 0 2400" gm_freespace_13.bsp
```

* Sonic running up from 2400 HU: falls at frame 303, 544 HU up, speed 2.9.
* A character created just above the slope at (2900, -7609, -15602) drops straight through as it lands, without
  moving (`walk` / `jog` traces: frame 17, NEAR 96, TOUCH 0).

**Variants** (`diag_maps --ramps 30 --random 0 VARIANT`):

| variant | falls / 240 |
|---|---|
| as 0.3 | 48 |
| `--max-edge 64` (2.08 M GRID triangles, first character 746 ms) | 38 |
| `--max-edge 32` | boot fails: GRID "more than 4194304 triangles after cutting" |
| `--drop-sliver 2` (1704 slivers dropped) | 48 |
| `--thin 24` / `--thin 48` | 41 / 36 |
| `--no-hidden` (like hull data) | 80 |
| `--drop-under 24` / `--drop-under 48` (3403 / 4183 undersides dropped) | 3 / 2 |
| `--tess 256` (cut every face into well-shaped pieces of at most 256 HU, 794k triangles) | 4 |
| `--tess 128` (2.7 M triangles, first character 1135 ms) | 5 |
| `--tess 256 --drop-under 24` | **0** |

The falls that remain after `--tess` are all foot-of-slope dips at boost16 or spin dash, onto a plank with an 11
degree lip (mechanism B). `--drop-under` removes them.

### 3.4 Other maps (`diag_maps --ramps 25 --random 40 --secs 8`, all default options)

| map | BSP | build_world | tris -> GRID tris / objects | 1st char | ramp runs / falls | random: longest tick, runs that fell |
|---|---|---|---|---|---|---|
| gm_construct | 37 MB | 24 ms | 36,226 -> 214,042 / 5,351 | 111 ms | 200 / 1 (dip, ramp #3) | 5.0 ms, 0/40 |
| gm_flatgrass | 47 MB | 3 ms (684 cold) | 9,387 -> 288,943 / 8,094 | 151 ms | no ramps | 0.9 ms, 0/40 |
| gm_freespace_13 | 115 MB | 41 ms | 60,992 -> 586,559 / 12,748 | 265 ms | 240 / 48 | 3.8 ms, 57/400 (400 runs x 20 s) |
| gm_genesis | 169 MB | 86 ms | 63,434 -> 856,187 / 13,806 | 348 ms | 200 / 1 | 1.0 ms, 0/40 |
| gm_bigcity_improved_rp | 105 MB | 106 ms | 59,560 -> 425,973 / 7,079 | 194 ms | 200 / 19 | 0.9 ms, 2/40 |
| gm_goldencity_v2_day | 150 MB | 89 ms | 70,770 -> 422,686 / 7,103 | 190 ms | 200 / 14 | 1.1 ms, 0/40 |
| gm_driversheaven_tdm | 155 MB | 59 ms | 142,998 -> 474,884 / 4,743 | 203 ms | 200 / 28 | 1.1 ms, 2/40 |
| gm_flatgrass_destroyable | 21 MB | 5 ms | 8,688 -> 302,848 / 7,953 | 150 ms | no ramps | 1.7 ms, 0/40 |
| gm_sonic_zone_one | 14 MB | 2 ms | 1,481 -> 53,937 / 1,237 | 55 ms | 56 / 0 | 8.6 ms, 1/40 |
| gm_sonic_abilities_1 | 11 MB | 1 ms | 1,154 -> 82,908 / 2,465 | 65 ms | 200 / 0 | 0.9 ms, 0/40 |
| awm_bigcity | 481 MB | 133 ms | 86,886 -> 777,407 / 11,653 | 365 ms | 56 / 0 | 2.3 ms, 10/40 |
| sgm_skate3_industrial | 297 MB | 83 ms (open 419) | 1,009 -> 330,432 / 4,539 | 159 ms | no ramps: the ramps are props | 2.1 ms, 0/40 |
| sgm_skate3_portcarverton | 170 MB | 40 ms | 50 (6 brushes) | 172 ms | the whole map is props | - |

The falls on the city maps happen at frame 11, during the 2 HU drop onto flat road or pavement right after creation.
Every one shows NEAR full and the crossed triangle is a long strip:

* gm_bigcity_improved_rp: 6784 x 64 HU (8096 -2176) to (8160 -8960);
* gm_goldencity_v2_day: 3008 x 256 HU;
* gm_driversheaven_tdm: 5772 x 844 HU.

This is mechanism A on flat ground. With `--tess 256 --drop-under 24`: bigcity 19 -> 0, goldencity 14 -> 0, random
falls 2 -> 0. driversheaven is in 3.6.

### 3.5 Things ruled out or minor

* **Character size at a wall.** On gm_construct, Knuckles created 5 HU from a wall (y -880) fell through the floor
  of slivers there. A test artefact: the GMod player hull keeps 16 HU, and `wallspawn` (2-8 units, both floor kinds)
  never falls. The ramp finder now checks wall clearance.
* **The window's polygon budget.** `SA1_EVENT_CAPACITY` id 2 fired 5x in 120 freespace runs, and 16x with `--tess 256`
  on every face. Tessellate only long faces; GRID's max_edge already bounds well-shaped ones.
* **Matrix stack.** Depth 1 throughout on all maps. See 5.2 (5).

### 3.6 Open: gm_driversheaven_tdm (-3500 .. -3960, -8050 .. -8212, z -124)

With both fixes, 10 of 200 runs still cross the floor near a ramp crest there. Two features meet at that spot:

* a 12 HU slab (top -124, underside -136) with a 15 degree plank (underside 3.9 HU below);
* a **displacement whose triangles face down** (normal 0 -0.02 -1, tri 54622 onward), 0-4 HU above the floor top.

A down-facing displacement 4 units above a walkable top is an underside within reach (mechanism B). Before treating it
as map data, check the displacement winding against the engine's (`BspWorldOptions::disp_transpose`, or a G64 or
PHYSCOLLIDE comparison).

Repro:

```
diag_maps --gdi GAME.gdi --tess 256 --drop-under 24 --trace "-3498.0 -6464.0 -584.0 -90.0 top 0 2400" gm_driversheaven_tdm.bsp
```

(ramp #13)

---------------------------------------------------------------------------------------------------------------------

## 4. G64 comparison (owner: "the collision should be generated in the same way G64 is")

**G64's pipeline** (docs/research/g64.md 4.1, 4.4):

* The server reads `Entity(0):GetPhysicsObject():GetMeshConvexes()`, the world's vphysics convex hulls, and streams
  them to clients.
* Clients bin the triangles into 1000-unit cells.
* Displacements are parsed with luabsp, and static props from their `.phy` models through temporary client props.
* Everything goes into a per-map cache (`data/g64/cache/<map>_cache.dat`).
* At runtime only Mario's cell is loaded.

**What G64's cache holds for the same ramp:**

* `gm_freespace_13_cache.dat`: 238,779 world triangles + 218,078 displacement triangles, against G-SA1's 60,992.
* At the failing point (parsed with a scratch script; the format is in g64.md 4.1): eight triangles of the plank's hulls, each the full 5312 HU length: up faces
  at 13.7 and -0.8 HU, down faces at 0.8 and 7.2 HU.
* So: the same needles, more of them, and undersides within one SA1 sphere radius. Hulls of touching brushes keep
  their touching faces; vbsp does no CSG between them.

**Why SM64 tolerates it and SA1 does not:**

* SM64 classifies surfaces by normal and asks "highest floor below Mario". A down face 1 HU under a floor changes
  nothing.
* SA1 tests every polygon near a sphere: undersides within the radius push it out the wrong side, and stacked layers
  fill the 96-entry NEAR list.
* Feeding SA1 hull data is the `--no-hidden` variant: 80 falls instead of 48. G-SA1 0.1, which sent all brush faces,
  fell through stacked floors for the same reason.

**What to take from G64:**

* static props: the `sprp` game lump plus `.phy` models, or in Lua `ents.CreateClientProp` + `PhysicsInit` +
  `GetMeshConvexes`, as G64 does;
* brush entities;
* the per-map cache, written once, so a second visit costs nothing;
* parsing off the frame: coroutines in G64, or a worker thread in the module.

Prop and entity hulls need the same treatment as brushes before they reach libsa1: hidden faces between touching
hulls removed, no undersides within reach, well-shaped triangles.

---------------------------------------------------------------------------------------------------------------------

## 5. Measurements, item 3 (freezes)

### 5.1 libsa1 and the G-SA1 core (diag_maps random play, ramp_diag stress)

**Random play** (`diag_maps --random N --secs S`):

* Setup: random open floors; every kind in turn (Sonic, Tails, Knuckles, Amy, Gamma, Big); random stick, jump,
  action, spin dash and action 2 every 0.5 s.
* Every tick and every render timed. The watchdog thread aborts with the state when a tick takes longer than 20-60 s.
* `--water` spawns above water instead.
* Results:
  * freespace 400 runs x 20 s (480k ticks): longest tick 3.8 ms, 0 ticks > 16 ms.
  * all maps together (ramp runs included): more than 1.5 M ticks, longest 8.6 ms (gm_sonic_zone_one, Tails mode
    15).
  * `sa1_character_render` every frame: mean 0.01 ms, longest 1.7 ms.
  * no faults, no `SA1_EVENT_UNIMPLEMENTED`, no character stuck (stick held 3 s, moved less than 8 units).
  * 500 create/destroy cycles: 5-36 ms each; private memory 54 -> 94 MB. A slow growth of about 80 KB per cycle,
    worth a look but not a freeze.

**Load** (`load:` and `libsa1:` lines):

* build_world 1-133 ms. The hidden-face removal is 15-27 ms of that.
* GRID synthesis with the first character: 55-365 ms. Later characters: 5-7 ms.

**Synthetic stress** (`ramp_diag --only stress`; dense 8/4/2-unit floors, bumpy terrain, 20 coplanar copies, a
1000-sliver fan, 40 stacked floors, a 40000-unit floor with max_edge 220):

* every step under 3.1 ms;
* GRID synthesis 5.7-94 ms.

Two failures found there, neither a freeze:

* The sliver fan's centre lets Sonic drop straight through: NEAR 96, TOUCH 0.
* A cell whose object holds more polygons than the window budget (7680) can never enter the window, and the
  character falls through it.

### 5.2 Outside the measured path (the module, Lua, the GPU)

1. **Audio, synchronous disc reads on the Lua thread.**
   * Chain: `Session::tick` -> `take_events` -> `Audio::event` -> `sa1_audio_mixer_event` -> `start_clip` ->
     `sa1a_read_adx`, which reads the whole ADX (music files up to `MAX_ADX_SIZE` 32 MB) from the GDI's track03.bin.
   * `ramp_diag --only audio` (sa1_audio_stream_open, the same read), first run: 27-430 ms per music file. Second run:
     110 files in 356 ms, slowest 12.8 ms. Voices: 0.1 ms warm.
   * Events in play:
     * TIMER.ADX (jingle 0x60, the drowning countdown) 38-68 times per 60 underwater runs;
     * the death and restart jingles;
     * invincibility / speed-up when items exist;
     * every voice line.
   * First use of each file after the OS cache dropped it means a stall of a tenth to half a second. This happens at
     game events, so it looks random.
2. **The first spawn on a new workshop map.** `GSA1.LoadWorld` -> `gsa1.WorldLoad` fails with not_found ->
   `file.Read("maps/<map>.bsp", "GAME")` (the whole BSP as a Lua string) -> `gsa1.WorldLoadData`, then the GRID
   synthesis at the first character. Reading from `.gma` files (Python):
   * gm_freespace_13 115 MB: 0.05 s warm;
   * rp_nycity 1,089 MB: 30.3 s cold, 0.42 s warm.

   Parsing itself is at most about 0.5 s. This explains a freeze when spawning on a new map; it is not random.
3. **The texture canary** (`cl_textures.lua` PostRender "gsa1_texture_check"): every 2 s `render.PushRenderTarget` +
   `render.CapturePixels` + `ReadPixel`, a GPU-CPU sync. A hitch every 2 s, not a freeze.
4. **Respawn churn.** Each fall below the map or death -> RESTART -> `GSA1.Respawn` -> a new world (35 ms) +
   `ents.CreateClientside` + texture preparation. On freespace's ramps that is every few seconds. Item 2's fix removes
   most of it.
5. **Latent matrix-stack leak.**
   * 8C029B00 leaves an `njPushMatrix` unpopped when 16 polygons of one object are touched (as the DC does).
   * libsa1 runs `njInitMatrix` only at boot (`sa1_world_boot.c`). The DC re-inits only on game-mode changes
     (8C0544E2).
   * Measured: depth 126 of 128 after 1200 steps on 20 coplanar floors (`stress`). Once full, `njPushMatrix` is
     refused while pops still run, which corrupts the next transforms.
   * Not seen on any real map (depth 1), but `--no-hidden` / hull-like data or coplanar duplicates would reach it.

---------------------------------------------------------------------------------------------------------------------

## 6. Recommended fixes (file / function), in order

(Status: 1-6 and 7-11 were done in round 4b, section 7; mechanism B's last part in round 4b2, section 8.)

### Ramps and floors

1. **LIB `src/sa1_coladapt.c` `ca_grid_split` / `ca_grid_place`: well-shaped splitting instead of halving the
   longest edge.**
   * Cut each triangle along its longest direction into strips no longer than its width, or `max_edge`, whichever is
     larger.
   * Then cut across into cells and fan-triangulate the convex pieces, the way `ca_clip_plane` already cuts for
     SA1_GRID_CUT (`diag_maps tess_tri` is a reference implementation).
   * Do the same for the pieces of SA1_GRID_CUT.
   * Target: no piece with width / length < about 0.25 unless the input already is one; bounding radius at most about
     max_edge / 2.
   * Optionally merge coplanar neighbours of the same object first: the CSG fragments of one brush face.
   * Emulation: freespace 48 -> 4, city maps -> 0.
   * Add the `needle` scene (ramp_diag) and a freespace-like plank to `tests/grid_test.c`.
2. **g-sa1 `src/core/bsp.cpp` `Bsp::build_world`: one-way thin platforms.**
   * Do not emit the down-facing faces (n.z < -0.5) of a solid brush whose walkable top (n.z >= 0.5, sloped planks
     included) is at most about 24-32 HU above them. This replaces "thicken to 12 HU" for those brushes.
   * Generalise it to any down face with a walkable visible face within that distance above it, whatever the brush:
     `--drop-under` did it on the triangle soup with a column test.
   * Effect: freespace 48 -> 3 (with 1: 0); the dips at plank feet stop being falls.
   * Cost: a character can jump up through such a platform from below, a common game convention. Ceilings of rooms
     under 16-24 HU floors become one-way too; restrict to brushes thinner than the sphere's reach (24 HU) if that
     matters.
3. **LIB pclower manifest + `code_8C028BDC.c` (8C028BFE): NEAR list 96 -> 256.**
   * Both the buffer `B_8C73E280` and the `== 96` test.
   * The uncapped census at the failures was 196-211, and EC1 already reaches 98-129 (docs/libsa1_integration.md
     5.7).
   * Keep DC limits for exactness traces, as 5.7 says.
   * Also report overflows: `src/world/sa1_coladapt_world.c` `cw_step`, or after the step, reads B_8C73E27C == cap and
     emits a new `SA1_CAPACITY_NEAR_POLYGONS`. G-SA1 then logs it with the position, which gives a map-side report of
     where geometry is too dense.
4. **LIB `src/world/sa1_coladapt_world.c` `cw_window`: never leave out the object under the character.**
   * Today an object whose polygons alone exceed the budget is skipped (`npoly + np > budget`).
   * Take the nearest object even when it exceeds the budget, as long as it is under 8192. Or let the GRID builder
     split objects above about 2000 polygons into sub-cells.
5. **g-sa1 `bsp.cpp`, displacements:** verify the winding of displacements whose face normal points down (3.6).
6. **Static props and brush entities** (G64's sources, section 4): a new pass in the module or the addon, with hulls
   cleaned as in 1-2.

### Freezes

7. **g-sa1 `src/core/audio.cpp` / LIB `sa1_audio_mix.c` `start_clip`: load clips off the Lua thread.**
   * Option: a loader thread that reads and parses the ADX and hands the clip to the mixer on the next update.
   * Option: preload the jingles G-SA1 can trigger at `audio_open` (TIMER, INVNCIBL, SPEEDUP, ONEUP, the death and
     clear jingles: a few MB).
   * Option: keep an LRU of decoded clips.
   * Voices are small but still cross the disc image; preload the selected character's voice set.
8. **g-sa1 `addon/lua/gsa1/cl_module.lua` `GSA1.LoadWorld` + module `WorldLoadData`: no whole-file read on the Lua
   thread.**
   * Read only the lumps `build_world` needs. The module already has `ByteSource`; a Lua `file.Open` + seek reader
     can feed it chunk by chunk, and the pak lump, usually most of the file, is never touched.
   * Run the parse on a worker thread and poll.
   * Cache the triangles per map (keyed by the BSP's size and CRC) under the config directory, as G64 does.
   * Show "loading map collision" instead of freezing.
9. **g-sa1 `cl_textures.lua` canary:** check only after `OnScreenSizeChanged` / `render.GetRenderTarget` changes or
   every 30 s, not every 2 s.
10. **LIB `src/world/sa1_world_tick.c` `sa1g_step_game`:** at the start of each step, check Ninja's matrix stack depth
    (B_8C88F69C) against its value after boot. Restore it and count the leak in the world stats when it grew.
    Exactness traces never trigger it.
11. **g-sa1 addon:** a frame-time logger. When a Think / Draw frame takes longer than 100 ms, print which part was slow:
    Tick, Render, Batches, EmitBatch, AudioUpdate, PadUpdate, texture paint, canary, LoadWorld. That way the next
    owner test names the freeze.

---------------------------------------------------------------------------------------------------------------------

## 7. Round 4b (2026-10-20 / 21): what the fixes of section 6 did and what they found

Implemented in libsa1 and G-SA1 0.5.0 (independently verified), in the order of section 6:

**libsa1 (GRID, `src/sa1_coladapt.c`, `src/world/sa1_coladapt_world.c`, `code_8C028BDC.c` SA1_GA):**

* **Well-shaped cutting (fix 1).** `ca_grid_poly` replaced the longest-edge halving: a convex planar polygon is cut on a
  grid in its own plane, laid along its longest edge (columns at most max_edge / sqrt 2 wide, slabs as long as a cell
  diagonal of max_edge allows); consecutive triangles of one planar convex face (one surface word, shared edges) are
  joined back into the face first (`ca_grid_merge`). gm_freespace_13's 1328 x 59 plank face: about 30 pieces instead of
  about 500 needles; the whole map 360,639 GRID triangles instead of 586,559.
* **Objects split (fix 4).** A cell with more than 1024 land triangles of one COL word becomes several objects (Z order of
  the centroids); the window ranks candidates by the distance to each object's box (header word `SA1CA_GH_BOXES`) and the
  nearest object, and those within 40 units, may use the whole polygon pool.
* **The descending-z sort (found while doing fix 4).** The cache build 8C02CFC0 pushes each polygon onto the head of its
  16-unit z bucket and chains the buckets in ascending z; the NEAR query 8C028BFE walks a land object's list from the head
  or back from the tail and **stops at the first polygon past its z range**. Polygons stored out of z order inside a
  bucket therefore made it stop early, and near an object's high-z end (where it walks back from the tail) it could find
  nothing at all: a 256-unit cell of 6- or 4.5-unit tiles split into parts let the character fall through. Every land
  object's triangles are now stored in descending centroid z (input order with `SA1_GRID_DC_LIMITS`), so the pushes leave
  each bucket in ascending order.
* **NEAR list 256 (fix 3)**, PC-only: a pcdata block `SA1_NEARCAP` ("NCAP" "8BFE": cap, full, peak, pool[256]) in the
  lowered 8C028BFE, enabled by host worlds (GRID or no static collision; 96 with `SA1_GRID_DC_LIMITS`); full lists are
  reported as `SA1_CAPACITY_NEAR_POLYGONS`. Reference worlds keep the game's `B_8C73E280[96]`.
* **Matrix stack (fix 10):** restored at each step of a host world (`SA1_CAPACITY_MATRIX_STACK`).

**G-SA1 (`src/core/bsp.cpp`, `mapjob.*`):** one-way thin platforms (the underside of a solid whose walkable top is at most
24 HU above it, also across solids and displacements, plus down faces at most 32 HU above a walkable surface), static
props and brush entities as G64 takes them (section 4), a per-map cache, the map job off the frame, and two further
findings:

* **Thin walls.** SA1 touches a single-sided polygon from behind within the sphere's radius (8C029B00, section 2): a
  character that steps into one side of a wall deeper than the wall is thick touches the other side too, and the game
  averages a land object's push-backs, so the two cancel and the character walks through. `map_test`: 4- and 8-HU walls
  let 15 of 15 and 3 of 15 runs and spin dashes through. Two opposite steep faces of a solid at most 16 HU apart now
  become one double-sided face halfway between them (it pushes the sphere out to the side its centre is on): 0 through.
* **Buried terrain.** gm_freespace_13 has displacement terrain 8 HU under its brush floors (displacements are not cut by
  brushes), and characters bobbed into such floors: displacement triangles buried inside a solid brush are now left out
  like hidden faces (868 on gm_freespace_13).

**Results (G-SA1 README "What 0.5.0 changes", the verifier's runs):** gm_freespace_13's 30 ramps 48 of 240 runs fell
in 0.3, **0** now; gm_bigcity_improved_rp 19 -> 0, gm_goldencity_v2_day 14 -> 0, gm_driversheaven_tdm 28 -> 0,
gm_construct 1 -> 0 (200 runs each); random play on gm_freespace_13 (400 x 20 s) 57 -> 3 runs crossing a floor,
gm_driversheaven_tdm (400 x 8 s) 14 -> 5.

**What the verifier found still open (round 4b2, sections 8 and 9):** the first `sa1_character_create` after the map's
collision was applied ran the GRID synthesis on the game thread (125 ms gm_construct, 144 gm_flatgrass, 202
gm_freespace_13, 208 gm_bigcity_improved_rp, 253 gm_driversheaven_tdm) and later creations still reached 56-59 ms;
ramp_diag `slab` let 10 of 264 runs fall (5 in the diagnosis) and `under` 3; the raised NEAR list could not be read by
tools (G-SA1 README, libsa1 API requests 11 and 12).

---------------------------------------------------------------------------------------------------------------------

## 8. Mechanism B, root cause and fix (2026-10-21)

### 8.1 The rule that turns a dip into a fall

Three properties of the game's land collision, all visible in the decompiled C (section 2):

1. **8C029B00 touches a single-sided polygon from behind** when the sphere's centre (swept from its position along its
   speed in steps of radius / 2) comes within the radius of the polygon's plane on either side and projects inside it.
2. **Per land object it selects the touch with the lowest height after the move** (`dist2 = rel.y + spd.y` in the
   polygon's frame: `min > dist2`). A touch from behind always has a negative height, a touch from the front a positive
   one: **a polygon touched from behind always wins**. The object's `normal2` and angles are the selected polygon's;
   `onpoly` and `pshbk` (the sphere pushed out to the radius on the polygon's front side) are averaged over all touches.
3. **8C073160 then lands on, or bumps into, each object with that normal2.** A downward normal2 is a ceiling / wall
   (8C0747D2, the averaged push-back applied: the sphere is moved down), or the landing projects the character onto the
   underside's plane; the character ends in mode 9 / 12 (falling) under the slab and runs on under it.

So the underside of any slab thinner than a sphere's reach is dangerous: whenever the centre comes within the radius of
it from above - the probe sits up to 3.6 units low (8C074CF0), an entry at speed dips 5-10 units into a slope, a crest
lifts the sphere off and drops it onto a thin platform - the underside is touched from behind and wins over the top.

### 8.2 Frame by frame (round 4b GRID, `tests/build_ramp_diag.py --touch-hook`)

The diagnostic build links its own copy of the lowered 8C029B00 whose returns call the test (`sa1diag_touch`): every
TOUCH call of a step is logged with the sphere it was given, the object's NEAR entries and the touches. Outputs:
`LIB/build/ramp_diag/round4b2_case_*.txt`.

**Knuckles, 45 degrees, slab 4, dash panel 10** (`RAMP_CASE="3 45 4 0 0 0 6 0"`; fell at the foot):

* Step 280: on the floor; the TOUCH calls of the land collision (radius 5.70) and of the second probe (radius 5.10) see
  the floor only.
* Step 281, the second probe's call on the object that holds the slope's top piece (normal (-0.71, 0.71)), its
  underside piece (normal (0.71, -0.71)) and a floor piece: sphere (-7.08, 2.11), speed (8.81, -0.08):

  | touch | normal | height | after the move | push-back |
  |---|---|---|---|---|
  | **0 (selected)** | underside (0.71, -0.71) | **-10.50 (from behind)** | **-4.21** | (3.95, -8.92) |
  | 1 | top (-0.71, 0.71) | +6.50 | +0.21 | (-6.09, 1.12) |
  | 2 | floor (0, 1) | +2.09 | +2.01 | (-7.08, 5.13) |

  The end of the sweep lies 0.21 above the top and 4.21 behind the underside (T 4 + 0.21), within Knuckles' radius:
  the underside wins, normal2 = (0.71, -0.71), and the averaged push-back moves the sphere by (+4.01, **-3.00**), to y
  -0.89: below the floor.
* Step 282: mode 9 (falling) at y -10.3; the slab's buried foot cap (normal (-0.71, -0.71)) is touched from behind and
  selected next, pushing down again; from step 283 on he falls under the floor (73 units at step 290).

**Sonic, 20 degrees, slab 0.5, dash panel 16** (`RAMP_CASE="0 20 0.5 0 0 0 7 0"`; fell at the crest):

* Steps 361-363, running up the slope: the land collision's calls (radius 5.00) select, every step, the slope's
  underside touched from behind (height -3.50 -> -3.42 against the top's +3.00 -> +2.92), push-back (+1.11, -3.05);
  only the second probe's call (radius 5.10), which selects the top, keeps him on the slope.
* Step 364, the crest: the platform's bottom (normal (0, -1), 0.5 under its top) is touched from behind (height -1.18
  -> **-3.66**) and selected: normal2 (0, -1), push-back (+0.56, -1.99); the feet end 6.0 below the surface (FELL at 100%).

### 8.3 Compared with the cutting before round 4b

`ramp_diag --legacy-split` halves every input triangle along its longest edge down to 128 units before
`sa1_collision_set_static_ex` and sets `SA1_GRID_DC_LIMITS` (input order, the 96-entry list): the GRID of the diagnosis.
`--keep-undersides` keeps the undersides (the round-4b behaviour, see 8.4).

| `slab` (264 runs) / `under` falls | undersides kept | undersides within reach left out (8.4) |
|---|---|---|
| cutting before round 4b (`--legacy-split`) | **6** (the diagnosis: 5) / 5 | **0 / 0** |
| round-4b cutting | **10** / 3 | **0 / 0** |

The mechanism is the same with both cuttings (`round4b2_case_knuckles30_T8_boost16_legacy.txt`: the underside, height
-12.23 -> -4.93, wins over the top, -3.07, push-back (+4.12, -7.13)). Which runs fall depends on which pieces share a land
object (the selection and the averaging are per object) and on the order 8C073160 visits the objects: the round-4b
cutting (fewer, well-shaped pieces, descending z order, objects ranked by their boxes) only moved the near-ties - the
falls moved from Knuckles 30 degrees slab 8 to Knuckles 45 degrees slabs 3 / 4 / 6 and Sonic 30 degrees slab 6.

### 8.4 The fix: GRID leaves out the undersides within reach

`src/sa1_coladapt.c` step c2 (`ca_grid_undersides`), after the cutting and before the objects are made: a land piece
that is **single-sided, faces down** (unit normal y < -0.1) and lies, at every one of 7 sample points (the centroid, the
corners and the edge midpoints, each moved 2% towards the centroid), **at most `under_reach` behind an upward-facing land
piece** (normal y >= 0.3, measured along that piece's normal, the point's projection inside it within 0.05) is left out.
Default 12 SA1 units (48 HU: thicker than Knuckles' radius 5.7 plus a 6-unit dip), `sa1_collision_grid.under_reach`
0.5 .. 64; `SA1_GRID_KEEP_UNDERSIDES` keeps them (`sa1_collision_info.grid_undersides` counts them). The covering pieces
are indexed by 128-unit x / z cells (an open-addressing table built in two counting passes, no sort); it costs 6-15 ms per
40,000-80,000 GRID triangles. Without the underside, a sphere that went into the slab touches only the top, from behind,
and is pushed out above it - the same as on a solid wedge, which never dipped.

Results (`round4b2_*` outputs; ramp_diag in its default GRID):

* `slab` **0 of 264** (no run dips more than 0.02 below the surface any more; Sonic's 45-degree dash-panel runs, which
  stopped in round 4b, go up), `under` **0** (no dips), `needle` no dips (NEAR up to 16 / 23 instead of 20 / 30); with the
  old cutting (`--legacy-split`) 0 of 264 too, and the needle plank no longer fills the 96-entry list (75 at most).
* The other sections unchanged or better: `ramps` 0 of 308, `threshold` never, `params`, `smooth`, `entry`, `stress`,
  `wallspawn` identical; `stairs` slab 3 at 40 / 45 degrees now enters at full dash-panel speed.
* `grid_test`: 0 failures and **0 known issues** (round 4b: 4 - the thin roofs under 2 units and the roof of 40 stacked
  slabs, both the same mechanism); its new section `underside` runs the round-4b falls (none falls; 2 of 8 with
  `SA1_GRID_KEEP_UNDERSIDES`); EC1 GRID keeps every reference mode / action sequence and the two bit-identical scripts
  (376 of its 14,520 triangles are undersides within reach).
* Real maps (`g-sa1/tests/diag_maps`, the real pipeline): gm_freespace_13's 30 ramps 0 of 240, gm_construct 0 of 200,
  random play gm_construct 0 of 200 runs, gm_flatgrass 0 of 200; gm_freespace_13 random play (400 x 20 s, seeds 1 / 2 / 3)
  3 / 4 / 2 runs crossing a floor - **the same counts with the round-4b libsa1** (`round4b2_random_freespace_*`): those are
  dips of 2-3 units on G-SA1 geometry (e.g. Knuckles running onto a 1-degree displacement edge, TOUCH 0 for one step) that
  recover, not mechanism B. G-SA1's one-way cleaning had already removed gm_freespace_13's thin undersides; GRID still
  leaves out 31,000 more pieces there (344,318 -> 312,995 GRID triangles).
* The cost: such slabs are **one-way platforms** - `grid_test`: Sonic jumps up through a 2-unit platform from below and
  lands on it; with `SA1_GRID_KEEP_UNDERSIDES` its underside stops him; a 20-unit slab keeps its underside.

---------------------------------------------------------------------------------------------------------------------

## 9. The game thread at creation (G-SA1 request 11) and the NEAR list for tools (request 12)

### 9.1 Where the time went

* The first creation after `sa1_collision_set_static`: the GRID synthesis (100-260 ms), on the creating thread.
* The 56-59 ms "respawns" of the verifier were `diag_maps`' random play cycling through the six kinds: **the first world of
  each kind composes that kind's world image** (`sa1g_image_compose`: 1ST_READ.BIN, 6.7 MB, and the kind's overlays read
  from the disc image, SHA-1 checked, PRS-unpacked) on the creating thread. A respawn of a kind already composed took
  5-6 ms: zeroing the fresh 16 MB arena (`sa1g_world_apply_image`), copying the 7-9 MB image in, pcdata, the boot (0.1 ms)
  and the collision adapter's attach (0.01 ms).

### 9.2 The preparation API (libsa1.h "Preparation on host threads", `SA1_API_PREPARE`)

* `sa1_collision_prepare` / `_prepare_tris` (any thread): the GRID synthesis (or the FAITHFUL landtable) into an
  immutable, reference-counted `sa1_collision_prepared` with a collision segment of its own (64 MB reserved, committed as
  used, sealed). The work runs on a **worker context** (`sa1_worker_ctx_create`: the host's allocator and file callbacks,
  no log, its own error text and file list) and never enters a world, so it shares nothing mutable with the thread that
  ticks; the token counter is atomic. `sa1_collision_set_prepared` installs it in O(1); every world created afterwards maps
  the prepared segment as its segments 0x08-0x0B (`sa1g_world.shared`) and holds a reference until it is destroyed.
* `sa1_character_prepare` (any thread) / `SA1_LOAD_WORLD_IMAGES`: the world images composed ahead with a worker context;
  the image table is filled under the world core's table lock, composing runs outside it.
* A new world's arena is no longer zeroed (`sa1g_world.fresh`: freshly committed pages are zero).

### 9.3 Measured (`LIB/tests/prepare_test.c`, x86_64 and x86)

| | before | now |
|---|---|---|
| prepared GRID synthesis vs the first world's | - | byte-identical (but the header's generation / token words); Sonic / Knuckles 400 pairs bit-identical; EC1 FAITHFUL prepared bit-identical |
| 8 preparations on 4 threads while the main thread ticks | - | all byte-identical, 5 repetitions |
| GRID synthesis of a 260,000-triangle map | 211 ms on the creating thread | 183 ms on a worker; `sa1_collision_set_prepared` 0.005-0.015 ms |
| `sa1_character_create_ex`, 60 creations of the six kinds | 6.3 / 7.3 / 8.1 ms (p50 / p90 / max, images composed) | **2.2 / 2.7 / 3.4 ms** (another run 1.9 / 2.3 / 2.6; x86 1.9 / 2.2 / 2.5) |
| the first creation of each kind | 30-60 ms (image) | 1.7-2.7 ms (`SA1_LOAD_WORLD_IMAGES`: `sa1_load_disc` 246 ms instead of about 60) |
| after creation | | `sa1_character_render(1.0)` 0.1 ms, the first pair (the land cache build) 1.2 / 1.5 / 2.6 ms, `sa1_character_destroy` 0.5 ms |

The ten reference traces stay strictly bit-exact (V5, V6, DISC, x86 and x64, V7 30 of 30).

### 9.4 The NEAR list for tools

`sa1_debug_near_polys(ctx, id, &info, polys, cap, &n)` after a tick: `info.list` is the guest address of the list the last
query filled (the raised pool `block + 0x14` in host worlds, `B_8C73E280` otherwise), `info.count_address` 0x8C73E27C,
`info.block` the `NCAP` pcdata block, `capacity` 256 / 96, `full`, `peak`, `peak_life`; `polys` the entries in host space
(corners, bounding sphere, attribute, the land object each came from, its guest address). `prepare_test` checks every
entry against the world's memory (up to 241 entries in the raised list; the game's list full 77 times on a 1-unit tiled
floor with `SA1_GRID_DC_LIMITS`). `tests/build_ramp_diag.py --touch-hook` goes further for one-off diagnoses: every TOUCH
call with its touches (8.2).

---------------------------------------------------------------------------------------------------------------------
## 10. Reproducing

```
# libsa1 (LIB): synthetic ramps, planks, stress, audio
py -3.12 tests/build_ramp_diag.py --run --gdi GAME.gdi -- --only ramps
build/ramp_diag/ramp_diag.exe --only slab --only under --only needle --only stress GAME.gdi
set RAMP_CASE=3 30 8 0 0 0 7 -1   (kind deg slab tile fillet_n fillet_len approach floor_under) & ramp_diag --only case GAME.gdi
set RAMP_GDI=GAME.gdi & ramp_diag --only audio GAME.gdi
py -3.12 tests/stage_geometry.py

# G-SA1: real maps (extract workshop maps from .gma first; never write into the game folder)
py -3.12 tests/build_diag_maps.py
build/diag_maps/diag_maps.exe --gdi GAME.gdi [--ramps 30] [--random 40 --secs 8] [--water] MAP.bsp...
   emulated fixes: --tess 256  --drop-under 24  --max-edge E --cell C --cut --drop-sliver W --thin HU --no-hidden
   one run:        --trace "foot_x foot_y foot_z uphill_yaw top|boost10|boost16|spindash|jog|walk kind runup"
   geometry:       --dump "x y z radius"
```

Outputs of this round are in `LIB/build/ramp_diag/*.txt` and `g-sa1/build/diag_maps/*.txt`: map_*, freespace_*,
construct_*.

Round 4b2 (sections 8 and 9):

```
# libsa1 (LIB)
py -3.12 tests/build_ramp_diag.py --touch-hook        # + every TOUCH call of a fall in section "case"
set RAMP_CASE=3 45 4 0 0 0 6 0 & build/ramp_diag/ramp_diag.exe --only case [--keep-undersides] GAME.gdi
build/ramp_diag/ramp_diag.exe --only slab --only under --only needle [--keep-undersides] [--legacy-split] GAME.gdi
py -3.12 tests/build_grid.py --run [--only underside]
py -3.12 tests/build_prepare.py --run [--target all] [--only api|same|faithful|threads|timing|near]

# G-SA1: the real pipeline, as before (random play: --ramps 0 --random 400 --secs 20 --seed 1|2|3)
build/diag_maps/diag_maps.exe --gdi GAME.gdi --ramps 30 --random 400 --secs 20 gm_freespace_13.bsp
```

Outputs: `LIB/build/ramp_diag/round4b2_*.txt` (the cases of 8.2 and 8.3 with the round-4b library, the sections before /
after, the real-map runs before / after).
