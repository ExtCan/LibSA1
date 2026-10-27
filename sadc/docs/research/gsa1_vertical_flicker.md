# G-SA1 0.5.0: the body flickers up and down against the camera

Research for the owner's report on G-SA1 0.5.0 (2026-10-21): "the horizontal speed position flickering thing from
earlier happens with vertical movement too". The earlier report (0.2) was "going forward and backward every frame"
at high speed. It was fixed in 0.3/0.4 by `Session::follow_root` and is checked by core_test's "frame loop" test.

This was a read-only diagnosis. LIB, g-sa1, SADC/src, SADC/include, SADC/tools and SADC/harness were not changed.
All experiments ran on copies in `sadc/build/research/vflicker/` (section 7).

## 1. Summary

There are two causes. Both are in how the drawn body is placed relative to the root that the chase camera follows.
Neither is in the game: the game's own position does not move back and forth (section 4).

| | Cause | Where | Size (measured) | Fix |
|---|---|---|---|---|
| **A** (0.5 regression, main cause) | **The root shift is applied twice.** In round 4b, libsa1 0.5 started moving every draw it cannot match with the previous pair along with the interpolated root (`root_shift`). G-SA1 still runs its 0.4 `Session::follow_root`, which moves the same frame a second time. During such a pair the body moves at **twice** the root's speed, then snaps back by one pair of movement at the next pair. | `g-sa1/src/core/session.cpp:705, 716-750` on top of `libsa1/src/sa1_character.c:1555-1576, 1717` | Knuckles' jump: the ball and the body are drawn in alternate pairs, so up to **16 pairs in a row** (0.53 s) are not interpolated. The body runs ahead of the camera by up to **10.3 HU** (standing jump, 144 fps) or **24-25 HU** (running jump) and snaps back every pair: 16-29 reversals per jump. On gm_construct (Knuckles, 20 s): 51 reversals of up to 38 HU. Sonic's and Tails' jumps and landings: one such pair each (10-25 HU). It happens horizontally as well as vertically. | Delete `follow_root` and `remember_root` in G-SA1 (section 5.1). After the fix, the drift in those pairs is **0.00 HU** and Knuckles' jump has **0** reversals. |
| **B** (since 0.2; it shows on vertical steps) | **The camera's root is one 60 Hz step ahead of the drawn body.** libsa1 captures the body on sub step 0 of a pair, because the character renderer 8C037098 draws only there. The root (`sa1_render.position`, which the camera follows) is the state after sub step 1. Their difference is the last step's movement. When that movement is irregular (a step-up snap, stairs, a landing, a fast fall), the camera moves one pair before the body. The body then sinks and pops back by the size of the step. | `libsa1/src/sa1_character.c:1008` (`rpos` = state after the pair) with `sa1_capture.h:65` (renderer draws on sub step 0 only) | gm_construct, E-102 stepping onto a 61-HU ledge: the camera rises 61 HU in one pair while the body stays, then the body rises in the next pair. The body moves against the camera by **17.5 HU per frame** at 144 fps (42 HU at 60 fps), swinging from -26 to +51 HU. Amy on raw 16-HU stairs: 6.4 HU per frame (15.4 at 60 fps), 4-10 reversals of up to 11.5 HU depending on the frame times. Falling at 1589 HU/s, the body hangs **26.9 HU higher** against the camera than when standing (49.4 against 22.5 HU) and sinks back over two pairs at the landing. Running at 1043 HU/s, the body trails the root by 17.1 HU along the path. | libsa1: make the render root the root of the drawn step (sub step 0) (section 5.2). Emulated: 17.5 becomes **0.71** HU per frame, 6.4 becomes **0.70**, the fall offset becomes 23.0 HU (standing 22.5), and the lead along the path becomes -0.3 HU. |

Fix A alone removes the flicker the owner describes (the per-pair sawtooth). Fix B removes the remaining one-step
bobbing on steps, ledges and landings. Fix B needs a libsa1 change. Both come with regression tests (section 6).

## 2. How it was measured

`vf_test.cpp` is a scratch extension of core_test's frame loop. It uses the same `gsa1::Session` code the module
runs, with the 0.5.0 snapshot of G-SA1's core and the 0.5.0 `libsa1.a` (`g-sa1/build/deps`, sha1
`f08fe4030e6cb917d41f22fb05acafb5d11b31f7`).

For each rendered frame it calls `Session::tick(dt)` and then `Session::render(-1)`, exactly as `cl_character.lua`'s
Think does. It records:

* the drawn root `r_pos`, which the camera follows (`CalcView` places the camera at `C.drawPos` plus fixed offsets);
* the centroid of the drawn body (the character's archive-0 triangle draws);
* whether libsa1 interpolated the frame, and whether G-SA1 moved it with the root;
* libsa1's lerp statistics (`sa1_character_lerp_stats`);
* the game's state.

A step hook (`vf_hook.c`) wraps the world's step function. It is compiled against LIB's headers and linked only into
the scratch test, so LIB is unchanged. The hook records player 0's `taskwk.pos` after sub step 0 and after sub step 1
of every pair. Check: sub step 1, converted to host units, equals `sa1_state.position` to 0.000 HU in every pair.

Frame rates: 60, 144 and 240 fps, alternating 100/200 fps, and 144 fps with frame times varying by 40 percent (the
same uneven sequence core_test uses).

Worlds:

* a flat floor;
* a 3000-HU tower with a floor below it;
* a 27-degree ramp ending in the air;
* a 15-degree downhill followed by a 40-degree one;
* raw stairs: 24 steps of 16 x 24 HU, with no ramp flag;
* a wall;
* a floor of 64-HU checkerboard GRID pieces;
* **gm_construct** through `Bsp::build_world`, as the module builds it.

Scenarios:

* every character jumping while standing and while running;
* every character falling 3000 HU (running off the tower);
* Sonic's spin dash up and off the ramp, and running up the ramp;
* Sonic and Knuckles running downhill;
* Sonic and Amy on the stairs;
* Tails flying;
* Knuckles gliding off the tower, and climbing;
* E-102 hovering with the Jet Booster;
* Sonic on the GRID pieces;
* 20 s of scripted play on gm_construct for each character (running, weaving, a jump every 3 s).

Metrics. Here "body - root" is the body against the camera, because the camera is the root plus constants.

* **drift**: inside a pair that libsa1 did not interpolate, how far body - root moves from the pair's first frame.
  It should be 0.
* **reversals**: pairs whose body - root drift (more than 1 HU) is undone by the jump at the next pair, pointing more
  than 120 degrees against it. This is the back-and-forth.
* **change per frame**: the largest change of body - root between two frames of one pair, along z and along the path.
* **game z per step**: on the ground in consecutive pairs, sign reversals of the game's own z from one 60 Hz step to
  the next (sub steps 0 and 1 of every pair).

"A" is a copy of `session.cpp` with `follow_root` disabled. "A+B" also uses the drawn step's root (sub step 0, from
the hook) for both the camera and the move libsa1 gives non-interpolated draws. This emulates fix B; libsa1 itself
was not rebuilt.

## 3. The causes in detail

### 3.1 Cause A: the root shift applied twice (libsa1 0.5 plus G-SA1's `follow_root`)

**libsa1 0.5 (round 4b).** `render_build` (`libsa1/src/sa1_character.c:1636`) runs `render_interp` whenever a
previous pair exists and alpha < 1. In `render_interp` (`:1564`), every vertex without a match in the previous pair
(`PIDX_NONE`, `:1573`) is placed at `cur->pos + shift`. The shift comes from `root_shift` (`:1555`):

`(rpos[prev] - rpos[cur]) * (1 - alpha)`

That is exactly the interpolated root minus the last pair's root. Sprites and lines get the same shift when the
pairs differ (`:1717`).

When nothing at all matched, `interp = same || matched > 0` is 0 (`:1690`), so `sa1_render.interpolated = 0`
(`:1743`). The frame is nevertheless already moved with the root. The header still describes the 0.4 behaviour
(`include/libsa1.h:668`: "0: the last pair as it is").

**G-SA1 0.5.0.** `Session::render` (`g-sa1/src/core/session.cpp:705`) calls `follow_root` (`:716-750`) for every
frame with `interpolated == 0` and alpha < 1. `follow_root` adds `r_pos - root_last` to every triangle, sprite and
line. `root_last` is the alpha-1 root that `remember_root` (`:631`) fetches after every tick that ran a pair (`:613`),
so the added term is `lerp(rpos[prev], rpos[cur], alpha) - rpos[cur]`, the same term libsa1 already added.

Result: body = drawn + 2 (prev - cur)(1 - alpha). At alpha 0 the body is one pair behind the camera, and it reaches
its drawn place at alpha 1, so it moves at twice the root's speed. If the next pair is again not interpolated, the
body jumps back by one pair of movement. That is a 30 Hz sawtooth whose size is the root's movement per pair:
12 HU at the start of a jump, 28 HU at 850 HU/s.

**Why it is vertical now.** Knuckles' jump draws the ball (1 model) and the curled body (28 models) in alternate
pairs (lerp statistics `m0/1`, `m0/28`, `m0/1`, ...). This alternation is in the game's own draws; whether it is
meant as a blur effect is UNVERIFIED. Because of it, no pair matches the one before it for the whole rise and fall of
the jump: 34 of 94 pairs in a 4-s run, up to 16 in a row. Every character's jump start (standing body to ball: Sonic
28 models to 1) and landing are single such pairs. All of these are
vertical movement. Horizontal movement is affected the same way (running jumps, gm_construct play: see the path
columns of the table).

**Why core_test did not catch it.** The "frame loop" test (`g-sa1/tests/core_test.cpp:1341`) measures Sonic running
and the spin dash. In 0.5 both are fully matched inside the measured window ("0 not interpolated by libsa1, 0 moved
with the root" at every frame rate; re-run here: 11 of 11 checks pass with and without fix A). `follow_root` is never
exercised there.

Trace (Knuckles' standing jump at 144 fps, 0.5.0). Within a non-interpolated pair, the body z advances 4.92 HU per
frame while the root z advances 2.46, and the body jumps back at the new pair:

```
t 1.0347 p1 a0.042 RAW +shift | root z  12.86 dz +2.55 | body z 12.40 dz -12.53 | body-root -0.46 | m0/28
t 1.0417 p0 a0.250 RAW +shift | root z  15.32 dz +2.46 | body z 17.32 dz  +4.92 | body-root +2.00 | m0/28
t 1.0625 p0 a0.875 RAW +shift | root z  22.69 dz +2.46 | body z 32.07 dz  +4.92 | body-root +9.38 | m0/28
t 1.0694 p1 a0.083 RAW +shift | root z  25.11 dz +2.41 | body z 32.03 dz  -0.04 | body-root +6.93 | m0/1
t 1.0972 p0 a0.917 RAW +shift | root z  34.49 dz +2.34 | body z 50.79 dz  +4.69 | body-root +16.30 | m0/1
t 1.1042 p1 a0.125 RAW +shift | root z  36.76 dz +2.28 | body z 42.39 dz  -8.40 | body-root +5.63 | m0/28
```

The same run with fix A: inside every non-interpolated pair, body - root stays constant (drift 0.00 HU; it moves by
the same 2.46 HU per frame as the root). The largest change of body - root per frame anywhere in the run falls from
2.58 to 1.53 HU; with A+B it falls to 0.03 HU.

### 3.2 Cause B: the camera's root is one step ahead of the drawn body

The body is captured from the game's own display. SonicMain (and every other character's main function) ends each
step with its display, and the hand-written renderer 8C037098 draws only on sub step 0 (`libsa1/src/sa1_capture.h:5-7,
65`, `rt_draw.c` "8C037098 then draws on sub step 0 only"). The drawn body therefore stands where the character was
**after the first step** of the pair.

`render_capture` (`sa1_character.c:1008`) records `rpos[nxt] = x.position`. That is `state_of`, the state **after
the second step**. The root that libsa1 interpolates and hands out as `sa1_render.position` (`:1751`), and which
G-SA1 hands to Lua as `C.drawPos` for the camera, is therefore one 60 Hz step ahead of the body. core_test already
notes this as an INFO ("the drawn body's centre trails the reported root by 15.8 HU at 1031 HU/s and 39.6 HU at
2379 HU/s ... libsa1 request").

On the Dreamcast the camera task ran within the same step as the drawn display, so camera and body agreed
(UNVERIFIED: reasoned from the step order, not traced). `sa1_capture.h` describes the display order for SonicMain;
the measurement shows the same one-step offset for all six characters (falls: Sonic 5.54, Tails 5.57, Knuckles 4.81,
E-102 6.54, Big 6.65 HU per frame at 144 fps, and 0.69-1.14 HU with the drawn step's root).

While the per-step movement stays steady, the one-step offset is constant and invisible. When one step contains a
jump in position, the camera moves during one pair and the body during the next, so the body sinks and then pops
back against the camera. The game does this in four cases:

* a step-up (the collision lifts the character onto a ledge in one step);
* stairs (gm_construct's are ramps after `bsp.cpp`, but single steps, curbs and props are not);
* a landing (the root stops one step before the body);
* the speed of a fall (the body hangs one step's fall higher: 26.9 HU at 1589 HU/s).

gm_construct, E-102 at 4.94 s, 144 fps, fix A only. The root rises 61 HU in one pair, the body in the next:

```
t 4.9097 p0 a0.292 G | root z  2.76 dz +18.04 | body z  27.47 dz  +1.06 | body-root +24.72
t 4.9306 p0 a0.917 G | root z 56.88 dz +18.04 | body z  30.65 dz  +1.06 | body-root -26.23
t 4.9375 p1 a0.125 G | root z 64.10 dz  +7.22 | body z  41.57 dz +10.92 | body-root -22.52
t 4.9653 p0 a0.958 G | root z 64.10 dz  +0.00 | body z 111.58 dz +17.50 | body-root +47.48
```

Amy on raw stairs (fix A only; root = after sub step 1) has body - root swinging +19 -> -11 -> +21 HU within four
pairs. With the root of the drawn step (sub step 0) it stays between +18.6 and +21.7 HU, which is her own sway.

## 4. Ruled out (with measurements)

* **The game itself.** The game's own z per 60 Hz step, on the ground in consecutive pairs: **0 reversals in 5,385
  steps** on the synthetic worlds. This covers the flat floor, the 15, 27 and 40 degree slopes, the 64-HU checkerboard
  GRID pieces (0 of 244 steps) and the raw stairs. On gm_construct there was **1 reversal in 4,891 steps** (1.28 HU,
  Knuckles). Per pair, 0 alternations everywhere. The game does not oscillate on slopes or on GRID pieces, and there
  is no NEAR-poly landing snap between pieces in these runs. Single large steps (a step-up of 61-84 HU, a landing) are
  real game events, and cause B is what makes them visible.
* **libsa1's transform interpolation (sa1_pose.c / sa1_xlerp_plan).** Translation is always lerped linearly. SNAP
  only replaces the rotation and stretch (`sa1_pose.c:1577`, `t` is lerped below it). Snapped models and
  vertex-lerp fallbacks: 0 in every traced run (jump, fall, stairs, glide, gm_construct). Matched (interpolated) pairs
  keep body - root continuous across pair edges (falls: 0.02-0.12 HU per frame of slow drift, which is the one-step
  offset growing with speed).
* **Unit conversion and offsets.** host = 4 (x, -z, y). The hook's sub-step-1 position converted this way equals
  `sa1_state.position` to 0.000 HU. `CalcView` adds a constant `C.info.center` (`cl_character.lua:403`). The CPU-pose
  centre offset (`sa1_character.c` `pose_eval_current`) is used only when the game camera does not see the character
  or before the first pair: 144 frames in the whole 144-fps run, which is the first pair after each creation.
* **The camera and entity in Lua.** Camera and body come from the same `gsa1.Render` call in Think
  (`cl_character.lua:73-87`).
  * `CalcView` (`:395`) uses `C.drawPos` plus constants. It applies no smoothing of its own. GMod's stair smoothing
    belongs to the default player view, which the hook replaces (from the engine's code, not checked in game).
  * The body's vertices are absolute (no model matrix, `cl_render.lua`). `ent:SetPos` only moves a clientside `anim`
    entity without physics, used for culling, so neither GMod gravity nor ground snapping applies to it.
  * `PrepareView` uses `EyePos()`, which is the view origin while drawing.
* **The server and the player entity.** `sv_gsa1.lua:72` sets the noclipped, invisible player to the reported state
  20 times a second. Nothing on the client reads it back for the character: the view origin comes from `CalcView`, and
  no net message from the server touches `C.drawPos`. Client prediction of a noclip player with cleared movement does
  not move the view. This was reasoned from the code, not measured in GMod.
* **Frame pacing.** All five frame-time patterns show the same picture. Only the size changes with the frame time:
  the per-frame change scales with the frame time, and the drift per pair does not.

## 5. Fixes

### 5.1 Fix A (G-SA1; required, small)

`g-sa1/src/core/session.cpp`:

* `Session::render` (`:699-705`): remove the alpha-1 `root_last` bookkeeping and the `follow_root` call. Instead set:

  ```cpp
  // libsa1 0.5 moves every draw it could not match with the previous pair along with the interpolated root itself
  // (sa1_character.c root_shift); 0.4's follow_root would move such a frame a second time (research
  // gsa1_vertical_flicker.md, cause A)
  c->r_shifted = !r->interpolated && r->alpha < 1.0f;
  if (c->r_shifted) {
      c->shifted_count++;
  }
  ```

* Delete `Session::follow_root` (`:716-750`), `Session::remember_root` (`:631-639`) and its call in `tick` (`:613`).
  This also saves one `sa1_character_render(1.0)` per pair.
* Delete `kMaxRootShift` (`:17`).
* `session.hpp`: remove `root_last`, `root_gen`, `root_valid` and `r_shift` (`:123-129`; keep `r_shifted` and
  `shifted_count` for the status), and the two declarations (`:241`, `:243`).

`src/module/gsa1_module.cpp:1276` comment and `cl_character.lua` comments: `movedWithRoot` (the 10th return of
`Render`) now means "libsa1 drew the last pair moved with the root". The overlay text stays.

README: "Rendering" step 1, and a "What 0.5.1 changes" row.

libsa1 header (a comment only, `include/libsa1.h:668`): `interpolated` 0 means "no draw matched the previous pair:
every draw is the last pair's, moved along with `position`". This tells hosts not to move it again.

### 5.2 Fix B (libsa1; the render root is the drawn step's root)

1. `libsa1/src/world/sa1_world.h` (`sa1g_world`): add `float drawn_pos[3]; uint32_t drawn_ang[3]; int32_t
   drawn_valid;`. Doc: player 0's `taskwk.pos` and `ang` after sub step 0 of the last pair, the step whose display
   8C037098 draws.
2. `libsa1/src/world/sa1_world_tick.c`: in `sa1g_world_pair`, after `w->step(w, sub, w->user)` (`:412`), add
   `if (sub == 0) sa1g_note_drawn(w);`. Do the same in `sa1g_world_step` (`:454`). `sa1g_note_drawn` reads player 0
   through `sa1g_world_player` and copies `twp + 0x20` (pos) and `twp + 0x14` (ang); with no player 0 it sets
   `drawn_valid = 0`. It only reads and does no FP arithmetic, so world state and determinism do not change (the
   hook used here does exactly this; its sub-step-1 reading matches `sa1_state.position` to 0.000 HU).
3. `libsa1/src/sa1_api_world.h` (`sa1w_pstate`, `:63-85`) and `src/world/sa1_api_world.c` (`sa1w_world_pstate`,
   `:502`): carry `drawn_pos`, `drawn_ang` and `drawn_valid`.
4. `libsa1/src/sa1_character.c` `render_capture` (`:1008-1009`): when the pstate's `drawn_valid` is set, use
   `rpos[nxt] = host(drawn_pos)` and `ryaw[nxt] = bams_to_yaw(drawn_ang[1])`; otherwise use `x.position` and `x.yaw`
   as now. `root_shift`, `sa1_render.position` and `sa1_render.yaw` then all refer to the drawn step, consistently.
5. `include/libsa1.h:669`: `position` is "the root of the drawn step (the pair's first 60 Hz step, whose display the
   game shows) at alpha; `sa1_state.position` is the state after the pair, one step further". Optionally add
   `drawn_position[3]` / `drawn_yaw` to the end of `sa1_state_ex` (versioned by struct_size) for hosts and tests.

G-SA1 needs no change for fix B: `C.drawPos` (the camera) is `sa1_render.position`. Sound, the server follow and the
fall-out check keep using the state (`GetState`).

Note for the interaction round (holding props): the hand and foot attach points in `sa1_state_ex.attach` are
evaluated for sub step 1's pose (`PJoinVertexes`, `sa1_capture.h:35`), so they are also one step ahead of the drawn
hands. Use the drawn step for them too, or take the hold point from the captured hand node. UNVERIFIED; not measured
here.

## 6. Regression tests (they fail before the fix and pass after it)

**G-SA1 core_test, "frame loop" (fix A).** Changes to `g-sa1/tests/core_test.cpp` `jitter_run` / `test_jitter`
(`:980`, `:1341`):

* Add a `kind` parameter (it creates only Sonic now) and two new metrics (`vf_test.cpp`'s `analyse` has the code):
  * **drift**: inside non-interpolated pairs, the 3D change of body - root since the pair's first frame;
  * **reversals**: pairs whose body - root drift (more than 1 HU) the next pair's jump undoes (more than 120 degrees
    against it).
* Add three cases at all five frame-time patterns:
  * Knuckles' standing jump: jump held from 1.0 to 1.3 s, measured from 0.9 s;
  * Knuckles' running jump: forward from 0.5 s, jumps at 3.0-3.3 s and 4.6-4.7 s, measured from 2.9 s;
  * Sonic's running jump: the same script.
* CHECK:
  * Knuckles has at least 10 non-interpolated pairs (proof that the case is exercised; measured 34);
  * drift at most 0.5 HU;
  * Knuckles' reversals = 0.

| | 0.5.0 | with fix A |
|---|---|---|
| Knuckles' standing jump: drift | 6.2 / 10.3 / 10.8 / 10.6 / 11.5 HU (60 / 144 / 240 / 100-200 / uneven) | 0.00 |
| Knuckles' standing jump: reversals | 12 / 16 / 20 / 17 / 19 | 0 |
| Knuckles' running jump: drift | 14.4 / 24.0 / 25.1 / 23.9 / 24.3 HU | 0.00 |
| Knuckles' running jump: reversals | 28 / 29 / 29 / 29 / 29 | 0 |
| Sonic's running jump: drift | 14.6-25.5 HU | 0.00 |

The existing Sonic run and spin dash checks pass before and after (11 of 11).

**G-SA1 core_test, "frame loop" (fix B, after the libsa1 change).**

* Turn the "lead" INFO into a CHECK: |lead at top speed - lead standing| < 2 HU.

  | | 0.5.0 | fix B (emulated) |
  |---|---|---|
  | running at 1043 HU/s | 17.1 HU | -0.3 HU |
  | spin dash at 2341 HU/s | 39.8 HU | 0.2 HU |
  | core_test's own INFO | 15.8 and 39.6 HU | |

* Add two cases on synthetic worlds: Sonic falling 3000 HU (`w_tower`) and Amy on raw stairs (`w_stairs`: 24 steps of
  16 x 24 HU). CHECK: the change of body - root z per frame within a pair is under 1.5 HU at 144 fps (under 2 HU at
  60 fps).

  | | 0.5.0 and fix A | fix B (emulated) |
  |---|---|---|
  | fall, 144 fps | 5.54 HU | 0.80 HU |
  | Amy on stairs, 144 fps | 6.40 HU | 0.70 HU |
  | fall, 60 fps | 13.29 HU | 1.62 HU |
  | Amy on stairs, 60 fps | 15.36 HU | 1.67 HU |

* Optional, on gm_construct in `test_session`: E-102 with `vf_test`'s `map_gamma` script, change per frame under
  3 HU.

  | | 0.5.0 | fix B (emulated) |
  |---|---|---|
  | 144 fps | 17.5 HU | 0.71 HU |
  | 60 fps | 42.0 HU | 1.69 HU |

**LIB test (fix B).** In `tests/capture_test.c` or `tests/world_boot_test.c`: "the render root is the drawn step's".
Create Sonic 3000 HU above a GRID floor and tick pairs. In every pair:

* `sa1_character_render(1.0).position` equals the host position of the world's `drawn_pos` exactly (or
  `sa1_state_ex.drawn_position`);
* it differs from `sa1_state.position` by the last step's movement;
* the body centroid's z minus the render root's z stays within 1 HU of its standing value while the fall passes
  1500 HU/s. In 0.5.0 it is 26.9 HU higher (49.4 against 22.5 HU); with the drawn root it is 23.0 HU.

The 10 reference traces should be unaffected: they compare world state, and fix B changes only the root that the
render output records. UNVERIFIED until LIB's `build.py --test` runs with the change; `capture_test` may compare
render roots and would then need its expectation moved to the drawn step.

## 7. Measurements and reproduction

Cells: drift (HU, inside non-interpolated pairs); reversals (largest, HU); largest change of body - root per frame within a pair along z / along the path (HU). "A" = follow_root removed, "A+B" = also the drawn step's root (emulated). The remaining reversals under A+B are single events at action changes, not a back-and-forth (`REV` lines of `--trace`):

* 1.5-5.6 HU where the drawn model changes (stand <-> ball at a jump start or landing): the centroid of another model;
* one of 9.8 HU on gm_construct where Knuckles climbs over a ledge top (actions 50 -> 0 -> 9): the game's own climb-up motion moves the model against the root.

Under A alone, gm_construct Knuckles still shows up to 17 reversals (100/200 fps; drifts of up to 14 HU, undone by 2-6 HU) in running and jumping pairs (actions 11 and 19); those are cause B and disappear with A+B. E-102 walking on gm_construct at uneven frame times shows 15-16 reversals of at most 2.5 HU in every configuration, 0.5.0 included: his walk cycle, neither cause.

### 7.1 All scenarios at 144 fps

| scenario | fps | max vz / speed (HU/s) | pairs not interpolated (longest run) | 0.5.0: drift; reversals (largest); change per frame z / path | A | A+B | game z per step on the ground: reversals |
|---|---|---|---|---|---|---|---|
| sonic_jump_stand | 144 | 442 / 442 | 7 (1) | 10.3; 2 (2.6); 2.58 / 2.58 | 0.0; 2 (3.1); 2.37 / 2.37 | 0.0; 2 (3.6); 1.76 / 1.76 | 0/87 |
| sonic_jump_run | 144 | 445 / 866 | 7 (1) | 24.3; 3 (6.1); 4.15 / 6.07 | 0.0; 5 (3.8); 4.15 / 2.41 | 0.0; 5 (4.1); 2.94 / 2.04 | 0/87 |
| sonic_fall | 144 | 1589 / 1589 | 0 (0) | 0.0; 0 (0.0); 5.54 / 5.54 | 0.0; 0 (0.0); 5.54 / 5.54 | 0.0; 0 (0.0); 0.80 / 0.27 | 0/184 |
| tails_jump_stand | 144 | 471 / 471 | 4 (1) | 10.3; 1 (1.4); 2.58 / 2.58 | 0.0; 1 (1.4); 1.61 / 1.61 | 0.0; 1 (1.4); 1.66 / 1.66 | 0/87 |
| tails_jump_run | 144 | 445 / 816 | 4 (1) | 22.9; 1 (1.4); 2.85 / 5.72 | 0.0; 2 (2.3); 2.85 / 1.93 | 0.0; 2 (2.4); 1.66 / 2.02 | 0/87 |
| tails_fall | 144 | 1589 / 1589 | 0 (0) | 0.0; 0 (0.0); 5.57 / 5.57 | 0.0; 0 (0.0); 5.57 / 5.57 | 0.0; 0 (0.0); 0.69 / 0.45 | 0/184 |
| knuckles_jump_stand | 144 | 442 / 442 | 34 (16) | 10.3; 16 (10.3); 2.58 / 2.58 | 0.0; 0 (0.0); 1.53 / 1.53 | 0.0; 0 (0.0); 0.03 / 0.03 | 0/87 |
| knuckles_jump_run | 144 | 445 / 854 | 34 (16) | 24.0; 29 (23.1); 2.58 / 5.99 | 0.0; 0 (0.0); 1.54 / 0.69 | 0.0; 1 (1.9); 1.20 / 0.14 | 0/87 |
| knuckles_fall | 144 | 1593 / 1593 | 0 (0) | 0.0; 0 (0.0); 4.81 / 4.81 | 0.0; 0 (0.0); 4.81 / 4.81 | 0.0; 0 (0.0); 1.14 / 0.55 | 0/186 |
| amy_jump_stand | 144 | 289 / 289 | 0 (0) | 0.0; 0 (0.0); 1.49 / 1.49 | 0.0; 0 (0.0); 1.49 / 1.49 | 0.0; 0 (0.0); 0.16 / 0.16 | 0/58 |
| amy_jump_run | 144 | 289 / 490 | 0 (0) | 0.0; 0 (0.0); 1.11 / 0.90 | 0.0; 0 (0.0); 1.11 / 0.90 | 0.0; 0 (0.0); 0.65 / 0.30 | 0/65 |
| amy_fall | 144 | 10 / 305 | 0 (0) | 0.0; 0 (0.0); 0.51 / 0.42 | 0.0; 0 (0.0); 0.51 / 0.42 | 0.0; 0 (0.0); 0.51 / 0.22 | 0/361 |
| gamma_jump_stand | 144 | 539 / 539 | 0 (0) | 0.0; 0 (0.0); 1.98 / 1.98 | 0.0; 0 (0.0); 1.98 / 1.98 | 0.0; 0 (0.0); 0.54 / 0.54 | 0/85 |
| gamma_jump_run | 144 | 539 / 698 | 0 (0) | 0.0; 1 (1.7); 2.43 / 1.47 | 0.0; 1 (1.7); 2.43 / 1.47 | 0.0; 0 (0.0); 2.35 / 1.40 | 0/91 |
| gamma_fall | 144 | 1879 / 1879 | 0 (0) | 0.0; 0 (0.0); 6.54 / 6.54 | 0.0; 0 (0.0); 6.54 / 6.54 | 0.0; 0 (0.0); 0.86 / 0.99 | 0/208 |
| big_jump_stand | 144 | 563 / 563 | 0 (0) | 0.0; 0 (0.0); 1.66 / 1.65 | 0.0; 0 (0.0); 1.66 / 1.65 | 0.0; 0 (0.0); 0.63 / 0.63 | 0/109 |
| big_jump_run | 144 | 512 / 644 | 0 (0) | 0.0; 0 (0.0); 1.80 / 1.38 | 0.0; 0 (0.0); 1.80 / 1.38 | 0.0; 0 (0.0); 0.56 / 0.57 | 0/115 |
| big_fall | 144 | 1920 / 1920 | 0 (0) | 0.0; 0 (0.0); 6.65 / 6.65 | 0.0; 0 (0.0); 6.65 / 6.65 | 0.0; 0 (0.0); 0.73 / 0.64 | 0/228 |
| sonic_spindash_ramp | 144 | 580 / 2341 | 0 (0) | 0.0; 0 (0.0); 2.79 / 1.93 | 0.0; 0 (0.0); 2.79 / 1.93 | 0.0; 0 (0.0); 0.93 / 2.03 | 0/247 |
| sonic_run_ramp | 144 | 383 / 1017 | 0 (0) | 0.0; 0 (0.0); 1.93 / 1.23 | 0.0; 0 (0.0); 1.93 / 1.23 | 0.0; 0 (0.0); 1.15 / 2.49 | 0/361 |
| sonic_downhill | 144 | 898 / 1417 | 0 (0) | 0.0; 0 (0.0); 0.89 / 2.09 | 0.0; 0 (0.0); 0.89 / 2.09 | 0.0; 0 (0.0); 1.05 / 1.76 | 0/421 |
| knuckles_downhill | 144 | 843 / 1301 | 0 (0) | 0.0; 1 (1.7); 1.34 / 1.60 | 0.0; 1 (1.7); 1.34 / 1.60 | 0.0; 1 (1.7); 1.33 / 1.84 | 0/418 |
| sonic_stairs | 144 | 782 / 979 | 0 (0) | 0.0; 3 (1.9); 2.99 / 5.82 | 0.0; 3 (1.9); 2.99 / 5.82 | 0.0; 0 (0.0); 0.80 / 0.66 | 0/183 |
| amy_stairs | 144 | 285 / 535 | 0 (0) | 0.0; 4 (5.3); 6.40 / 7.93 | 0.0; 4 (5.3); 6.40 / 7.93 | 0.0; 1 (1.4); 0.70 / 0.80 | 0/244 |
| tails_fly | 144 | 404 / 841 | 8 (1) | 21.1; 1 (1.6); 2.58 / 5.67 | 0.0; 4 (4.0); 1.61 / 1.61 | 0.0; 2 (1.5); 1.66 / 1.66 | 0/178 |
| knuckles_glide | 144 | 367 / 858 | 13 (13) | 12.0; 11 (12.0); 2.58 / 3.00 | 0.0; 0 (0.0); 0.06 / 0.11 | 0.0; 0 (0.0); 0.06 / 0.08 | 0/0 |
| knuckles_climb | 144 | 190 / 191 | 0 (0) | 0.0; 0 (0.0); 0.22 / 0.22 | 0.0; 0 (0.0); 0.22 / 0.22 | 0.0; 0 (0.0); 0.22 / 0.22 | 0/325 |
| gamma_hover | 144 | 440 / 595 | 0 (0) | 0.0; 0 (0.0); 1.52 / 1.57 | 0.0; 0 (0.0); 1.52 / 1.57 | 0.0; 0 (0.0); 0.87 / 0.86 | 0/67 |
| sonic_pieces | 144 | 445 / 986 | 2 (1) | 27.7; 0 (0.0); 2.58 / 6.92 | 0.0; 1 (3.2); 2.38 / 0.97 | 0.0; 1 (3.3); 0.83 / 0.55 | 0/244 |
| sonic_flat_run | 144 | 19 / 1043 | 0 (0) | 0.0; 0 (0.0); 0.94 / 0.29 | 0.0; 0 (0.0); 0.94 / 0.29 | 0.0; 0 (0.0); 0.94 / 0.29 | 0/301 |
| map_sonic | 144 | 367 / 850 | 20 (2) | 23.9; 10 (9.2); 2.67 / 5.96 | 0.0; 13 (4.1); 2.67 / 2.58 | 0.0; 10 (3.9); 1.81 / 1.80 | 0/817 |
| map_tails | 144 | 367 / 770 | 12 (1) | 16.2; 6 (1.5); 2.79 / 5.40 | 0.0; 7 (2.8); 2.79 / 2.60 | 0.0; 7 (2.5); 1.48 / 1.80 | 0/772 |
| map_knuckles | 144 | 605 / 737 | 81 (16) | 46.5; 51 (38.4); 11.55 / 11.62 | 0.0; 7 (3.9); 5.82 / 5.52 | 0.0; 2 (1.7); 4.53 / 6.26 | 1/632 |
| map_amy | 144 | 289 / 400 | 0 (0) | 0.0; 0 (0.0); 1.10 / 1.08 | 0.0; 0 (0.0); 1.10 / 1.08 | 0.0; 0 (0.0); 0.62 / 0.38 | 0/831 |
| map_gamma | 144 | 440 / 657 | 0 (0) | 0.0; 3 (3.7); 17.50 / 16.97 | 0.0; 3 (3.7); 17.50 / 16.97 | 0.0; 0 (0.0); 0.71 / 2.62 | 0/908 |
| map_big | 144 | 425 / 551 | 0 (0) | 0.0; 1 (2.8); 1.91 / 3.42 | 0.0; 1 (2.8); 1.91 / 3.42 | 0.0; 0 (0.0); 0.88 / 1.79 | 0/931 |

### 7.2 Key scenarios at the other frame times

| scenario | fps | max vz / speed (HU/s) | pairs not interpolated (longest run) | 0.5.0: drift; reversals (largest); change per frame z / path | A | A+B | game z per step on the ground: reversals |
|---|---|---|---|---|---|---|---|
| sonic_jump_run | 60 | 445 / 866 | 7 (1) | 14.6; 2 (2.8); 7.34 / 14.57 | 0.0; 2 (1.5); 7.34 / 5.14 | 0.0; 2 (2.0); 4.42 / 3.99 | 0/87 |
| sonic_jump_run | 240 | 445 / 866 | 7 (1) | 25.5; 3 (5.8); 2.60 / 3.64 | 0.0; 5 (3.3); 2.60 / 1.45 | 0.0; 5 (3.9); 1.87 / 1.23 | 0/87 |
| sonic_jump_run | 100/200 | 445 / 866 | 7 (1) | 20.0; 3 (6.3); 5.21 / 8.74 | 0.0; 4 (3.8); 5.21 / 3.45 | 0.0; 5 (4.1); 3.46 / 2.89 | 0/87 |
| sonic_jump_run | 144+-40% | 445 / 866 | 7 (1) | 24.9; 3 (6.5); 5.36 / 7.60 | 0.0; 5 (3.9); 5.36 / 3.15 | 0.0; 4 (4.2); 3.76 / 2.63 | 0/87 |
| sonic_fall | 60 | 1589 / 1589 | 0 (0) | 0.0; 0 (0.0); 13.29 / 13.29 | 0.0; 0 (0.0); 13.29 / 13.29 | 0.0; 0 (0.0); 1.62 / 0.52 | 0/184 |
| sonic_fall | 240 | 1589 / 1589 | 0 (0) | 0.0; 0 (0.0); 3.32 / 3.32 | 0.0; 0 (0.0); 3.32 / 3.32 | 0.0; 0 (0.0); 0.48 / 0.16 | 0/184 |
| sonic_fall | 100/200 | 1589 / 1589 | 0 (0) | 0.0; 0 (0.0); 7.98 / 7.98 | 0.0; 0 (0.0); 7.98 / 7.98 | 0.0; 0 (0.0); 1.11 / 0.37 | 0/184 |
| sonic_fall | 144+-40% | 1589 / 1589 | 0 (0) | 0.0; 0 (0.0); 7.21 / 7.21 | 0.0; 0 (0.0); 7.21 / 7.21 | 0.0; 0 (0.0); 1.02 / 0.34 | 0/184 |
| knuckles_jump_stand | 60 | 442 / 442 | 34 (16) | 6.2; 12 (6.2); 6.18 / 6.18 | 0.0; 0 (0.0); 3.68 / 3.68 | 0.0; 0 (0.0); 0.06 / 0.06 | 0/87 |
| knuckles_jump_stand | 240 | 442 / 442 | 34 (16) | 10.8; 20 (10.8); 1.55 / 1.55 | 0.0; 0 (0.0); 0.92 / 0.92 | 0.0; 0 (0.0); 0.02 / 0.02 | 0/87 |
| knuckles_jump_stand | 100/200 | 442 / 442 | 34 (16) | 10.6; 17 (9.3); 3.71 / 3.71 | 0.0; 0 (0.0); 2.21 / 2.21 | 0.0; 0 (0.0); 0.04 / 0.04 | 0/87 |
| knuckles_jump_stand | 144+-40% | 442 / 442 | 34 (16) | 11.5; 19 (11.5); 3.58 / 3.58 | 0.0; 0 (0.0); 2.03 / 2.03 | 0.0; 0 (0.0); 0.03 / 0.03 | 0/87 |
| knuckles_jump_run | 60 | 445 / 854 | 34 (16) | 14.4; 28 (14.4); 6.18 / 14.37 | 0.0; 0 (0.0); 3.70 / 1.66 | 0.0; 0 (0.0); 2.61 / 0.29 | 0/87 |
| knuckles_jump_run | 240 | 445 / 854 | 34 (16) | 25.1; 29 (25.1); 1.55 / 3.59 | 0.0; 0 (0.0); 0.93 / 0.42 | 0.0; 1 (1.9); 0.72 / 0.08 | 0/87 |
| knuckles_jump_run | 100/200 | 445 / 854 | 34 (16) | 23.9; 29 (23.9); 3.71 / 8.62 | 0.0; 0 (0.0); 2.22 / 1.00 | 0.0; 2 (2.1); 1.72 / 0.20 | 0/87 |
| knuckles_jump_run | 144+-40% | 445 / 854 | 34 (16) | 24.3; 29 (20.8); 3.53 / 7.42 | 0.0; 0 (0.0); 1.79 / 0.92 | 0.0; 0 (0.0); 1.59 / 0.19 | 0/87 |
| amy_stairs | 60 | 285 / 535 | 0 (0) | 0.0; 0 (0.0); 15.36 / 19.02 | 0.0; 0 (0.0); 15.36 / 19.02 | 0.0; 0 (0.0); 1.67 / 1.91 | 0/244 |
| amy_stairs | 240 | 285 / 535 | 0 (0) | 0.0; 0 (0.0); 3.84 / 4.76 | 0.0; 0 (0.0); 3.84 / 4.76 | 0.0; 1 (1.7); 0.43 / 0.48 | 0/244 |
| amy_stairs | 100/200 | 285 / 535 | 0 (0) | 0.0; 10 (11.5); 9.22 / 11.40 | 0.0; 10 (11.5); 9.22 / 11.40 | 0.0; 1 (1.0); 1.00 / 1.13 | 0/244 |
| amy_stairs | 144+-40% | 285 / 535 | 0 (0) | 0.0; 4 (7.8); 8.02 / 9.91 | 0.0; 4 (7.8); 8.02 / 9.91 | 0.0; 1 (1.2); 0.94 / 1.02 | 0/244 |
| map_knuckles | 60 | 605 / 737 | 81 (16) | 27.9; 36 (23.0); 27.71 / 27.89 | 0.0; 1 (2.3); 13.97 / 13.25 | 0.0; 0 (0.0); 10.72 / 15.00 | 1/632 |
| map_knuckles | 240 | 605 / 737 | 81 (16) | 48.8; 48 (40.3); 6.93 / 6.97 | 0.0; 2 (4.1); 3.49 / 3.31 | 0.0; 1 (1.6); 2.78 / 3.76 | 1/632 |
| map_knuckles | 100/200 | 605 / 737 | 81 (16) | 50.2; 62 (30.9); 16.63 / 16.74 | 0.0; 17 (5.9); 8.38 / 7.95 | 0.0; 2 (9.4); 5.93 / 8.88 | 1/621 |
| map_knuckles | 144+-40% | 605 / 737 | 81 (16) | 44.1; 54 (35.3); 13.46 / 13.55 | 0.0; 12 (4.2); 7.77 / 7.37 | 0.0; 1 (4.0); 5.62 / 8.25 | 1/619 |
| map_gamma | 60 | 440 / 645 | 0 (0) | 0.0; 0 (0.0); 42.00 / 40.73 | 0.0; 0 (0.0); 42.00 / 40.73 | 0.0; 0 (0.0); 1.69 / 3.74 | 0/908 |
| map_gamma | 240 | 440 / 645 | 0 (0) | 0.0; 0 (0.0); 10.50 / 10.18 | 0.0; 0 (0.0); 10.50 / 10.18 | 0.0; 0 (0.0); 0.43 / 1.01 | 0/908 |
| map_gamma | 100/200 | 440 / 657 | 0 (0) | 0.0; 8 (2.7); 25.20 / 24.44 | 0.0; 8 (2.7); 25.20 / 24.44 | 0.0; 4 (1.6); 1.02 / 3.91 | 0/908 |
| map_gamma | 144+-40% | 440 / 645 | 0 (0) | 0.0; 16 (2.4); 22.99 / 22.98 | 0.0; 16 (2.4); 22.99 / 22.98 | 0.0; 15 (2.5); 0.98 / 3.46 | 0/908 |

### 7.3 Reproduction (scratch files in `sadc/build/research/vflicker/`)

| File | What |
|---|---|
| `gsa1/` | snapshot of g-sa1 0.5.0: `src/` (core and module), `tests/`, `addon/`, `deps/include`, `deps/libsa1.a` (sha1 f08fe403...) |
| `fix1/` | the 0.5.0 core with `follow_root` disabled (fix A, as one `if (true \|\| ...)`) |
| `vf_test.cpp` | the frame-loop test: worlds, scenarios, metrics, `--trace`, `--s0` (analyse against the drawn step's root and libsa1's move with it = fix B emulated) |
| `vf_hook.c`, `build_hook.py` | the step hook (sub step 0 / 1 positions), compiled against LIB's headers into `obj/vf_hook.o` |
| `build_vf.py` | `py -3.12 build_vf.py vf_test.cpp` builds `vf_test_base.exe` from the 0.5.0 core; `py -3.12 build_vf.py vf_test.cpp <absolute path>/fix1 fix1` builds `vf_test_fix1.exe` |
| `ct_core_test.cpp` | core_test 0.5.0 (`--jitter`, built the same way for base / fix1: 11 of 11 pass with both) |
| `summarize3.py` | `py -3.12 summarize3.py run_base.txt run_fix1.txt run_fix1_s0.txt 144` produces the tables above |
| `run_base.txt`, `run_fix1.txt`, `run_fix1_s0.txt`, `run_base_s0.txt` | full outputs (every scenario, every frame time) |
| `tr_kjump.txt`, `tr_mg.txt`, `tr_amy_s0.txt`, `tr_fall_fix.txt`, ... | per-frame traces quoted above |

Run with `SA1_TEST_GDI` set to the disc. `vf_test_*.exe [--only NAME] [--fps 60|144|240|100/200|144+-40%]
[--trace NAME] [--s0]`. The whole suite (36 scenarios x 5 frame-time patterns) takes about a minute per configuration.

## 8. Open and UNVERIFIED

* **In-game confirmation.** Nothing here ran inside Garry's Mod. With `gsa1_debug 1` the overlay already shows the
  symptom of cause A: "moved with the root" appears during every Knuckles jump (alternating ball and body). After
  fix A it should still say "moved with the root" (now libsa1's move), but the body must not shake.
* **Where exactly the drawn root is.** Fix B records the position at the end of sub step 0. The display draws inside
  the player's own task during that step, and CCL, the particle manager and the land update run after it. Whether any
  of them moves the player in between is UNVERIFIED. Measured agreement with the drawn body: during a 1589 HU/s fall
  the body sits within 0.5 HU of its standing offset (23.0 against 22.5), and the lead along the path at
  1043-2341 HU/s is -0.3 to 0.2 HU. Recording inside rt_draw.c when the first 8C037098 record of a pair is written
  would be exact.
* **Model switches and the climb-up.** The remaining "reversals" are single events.
  * 1.5-5.6 HU at action changes (jump start, landing, Tails starting to fly): the centroid of a different model,
    the game's own 30 Hz change, not a flicker.
  * 9.8 HU there and back over two pairs where Knuckles climbs over a ledge top on gm_construct: the game's
    climb-up motion. Whether the Dreamcast shows the same is UNVERIFIED.
* **Moving platforms (not measured).** G-SA1's movers read the client's entity positions, and GMod draws server
  entities about 100 ms in the past (interpolation). A character riding a lift may sit at a constant offset from the
  drawn lift. That is not a back-and-forth. UNVERIFIED.
* **`gsa1_interpolate 0`** (raw pairs): cause A does not apply (alpha 1). Cause B does (a one-step offset that
  changes per pair).
* **Attach points** (hands and feet) are sub step 1's (see 5.2). This matters for held props in the interaction round.

## Review (adversarial)

Reviewer's pass on 2026-10-21. The reviewer re-ran the scratch binaries and wrote three independent probes in
`sadc/build/research/vflicker_review/`:

* `probe.cpp`: the vertex-level check of cause A.
* `probe2.cpp`: falls from a 3000-unit tower: Sonic running off, Sonic and Knuckles jumping at its edge, and the
  landings below.
* `probe3.cpp`: cause B checked against a drawn node instead of the body centroid.

All three are built by `build_probe.py` / `build_probe_hook.py` against the same 0.5.0 snapshot (`vflicker/gsa1`,
`libsa1.a` sha1 f08fe403); probe3 also links `vflicker/obj/vf_hook.o`. Run with `SA1_TEST_GDI` set. Nothing in LIB,
g-sa1, SADC/src, include, tools or harness was changed.

### R.1 Verified

1. **Reproduction.** Re-running `vf_test_base.exe` and `vf_test_fix1.exe` (with and without `--s0`) gives the doc's
   numbers exactly:

   | case | 0.5.0 | A | A+B (emulated) |
   |---|---|---|---|
   | knuckles_jump_stand@144 | RAW 34 (run 16), drift 10.30, 16 reversals | 0.00, 0 | |
   | sonic_jump_run@144 | 24.29, 3 reversals | 0.00, 5 | |
   | sonic_fall@144 | change per frame 5.54, lead_z 49.4 | | 0.80, 23.0 |
   | amy_stairs@144 | 6.40 | | 0.70 |
   | map_gamma@144 | 17.50 | | 0.71 |
   | sonic_flat_run lead | 17.1 | | -0.3 |
   | spin dash lead | 39.8 | | 0.2 |

   `ct_core_test_{base,fix1}.exe --jitter` passes 11 of 11 with both builds.

2. **Cause A at the vertex level** (probe.cpp; it does not depend on the centroid metric). Take every frame with
   `interpolated == 0` and alpha < 1, and let sh = `r_pos` minus `sa1_character_render(1.0).position`. Then:

   * libsa1's own `sa1_character_render(alpha)` equals render(1.0) + sh, to 0.0000 HU;
   * the triangles G-SA1 draws (`Character::r_tpos` after `follow_root`) equal render(1.0) + **2** sh, to 0.0000 HU;
   * in interpolated frames, the drawn triangles equal libsa1's output (0.0000).

   The second shift is up to 12.37 HU for Knuckles' standing jump, 25.95 HU for his running jump, 26.40 HU for Sonic's
   running jump and 24.53 HU for Tails', at 60 fps and at 144 fps alike. Rebuilt on the fix-A core (`probe_fix1.exe`),
   the drawn triangles equal libsa1's in every frame (0.00 HU). Fix A therefore removes the error from the geometry
   actually drawn, not only from the proxy measure.

3. **Code.** `root_shift` is lerp(rpos) - rpos[cur]. `follow_root` adds `r_pos - root_last`, and `root_last` is
   render(1.0).position, which is rpos[rcur] (`render_build`'s alpha-1 branch, `sa1_character.c:1753-1755`). It is the
   same term. No case needs `follow_root`:
   * With a previous pair and alpha < 1, `interpolated == 0` means matched == 0 and !same. Then every vertex keeps
     `PIDX_NONE` (`plan_build` :1413, only matched nodes set pidx at :1449) and is shifted (:1573), and sprites and
     lines are shifted too (:1716).
   * With no previous pair (`rhave == 1`), libsa1 shifts nothing, but `r_pos == root_last`, so `follow_root`'s shift
     is 0 as well.

   Deleting `follow_root` loses nothing.

4. **It is a regression of the 0.5 round.**
   * `root_shift` is in the debug info of `sa1_character.o` in both snapshot archives: Windows, LIB build of
     10-21 04:38, and Linux, of 10-20 14:58.
   * G-SA1's `session.cpp` (10-20 15:38) is byte-identical to the snapshot.
   * The installed `gmcl_gsa1_win64.dll` has the same sha1 (8faea33d) as the 04:41 build listed in
     `g-sa1/build/BUILD_INFO.txt`.
   * The 0.5 README row shows the author's assumption: "G-SA1's own 'move with the root' path now only matters when
     nothing matched".

5. **Cause B, per drawn node** (probe3.cpp: Sonic runs off the 3000-unit tower, one pair per tick). The first captured
   node's world z minus `taskwk.pos` z after sub step 0 is **+21.60 HU in every pair**, from 130 to 1589 HU/s
   (constant to 0.01 HU). Minus `sa1_state.position` (sub step 1) it grows from +23.77 to +48.09 HU, which is exactly
   -vz/60. The drawn body is therefore exactly at sub step 0's root, not only within the 0.5 HU that the centroid gave.
   This settles open item 8 ("where exactly the drawn root is") for falls. LIB's own `capture_test.c:1483` already
   subtracts `velocity / 60` for the same reason.

6. **The module and Lua path** (read in the snapshot). Nothing from the server writes `C.drawPos`.
   * `gsa1.Render` returns `r_pos` (`gsa1_module.cpp:1292-1294`).
   * `FrameRender` stores it in `C.drawPos`, which `CalcView` follows.
   * `EmitBatch` streams the Session's `r_tpos` every frame (`session.cpp:916`).
   * The order is Think, then tick, then render.

7. **Cause A on a long ball fall: a sawtooth of about 55 HU** (probe2). Knuckles jumps at the tower's edge and
   falls 3000 units, and the ball and the body alternate through the whole fall. At 144 fps 302 frames are not
   interpolated (125 at 60 fps).
   * The second shift reaches **56.2 HU**.
   * At the start of every pair, body - root z jumps from about +47..62 (the end of the pair before) to about
     +92..106 HU, then sinks back at twice the fall speed. For example, at t 6.000 (alpha 0) it is +106.15, at 6.028
     +62.51, and at 6.035 +99.53.
   * That is a 30 Hz vertical sawtooth of about 55 HU at 1600 HU/s. It is as large as the doc's worst case, 50.2 HU
     of drift (map_knuckles at 100/200 fps), but here on a plain fall.

   Rebuilt on the fix-A core (`probe2_fix1.exe`), body - root z stays constant inside each pair: +45.27, +48.90,
   +53.78, +49.13, +54.03, +49.35. The 4-5 HU steps left are the change between ball and body. About 26 HU of each
   value is cause B's one-step lag at that speed. The "second shift" column of probe2 is |sh|; in 0.5.0 that is the
   body's distance from where libsa1 places it (R.1, item 2).

   Add this case to fix A's regression test.

### R.2 Refuted or corrected

1. **"Every character's jump start and landing is one such pair" is wrong.** Only the characters with a jump ball
   have them: Sonic, Tails and Knuckles. Amy, E-102 and Big have 0 non-interpolated pairs in all their jump, fall and
   map scenarios (table 7.1, `pairs not interpolated` = 0).
2. **"Fix A alone removes the flicker the owner describes" is UNVERIFIED and probably too strong.**
   * What fix A removes is the per-pair double speed followed by a snap back.
   * For Sonic, cause A happens only in single pairs: the jump start, the landing, and switches between the ball and
     the curled body in mid-air. In `sonic_jump_run@144` the non-interpolated pairs start at t 3.000, 3.972, 4.604,
     4.701, 4.972, 5.236 and 5.333. The body is up to 26.4 HU from its place there, horizontally as well as
     vertically.
   * After Sonic jumps at the tower's edge he stays in the ball for the whole 3000-unit fall, and all of those pairs
     are matched (probe2). The only non-interpolated pair is the change from the ball to the standing body after
     touchdown, 17.0 HU off its place, horizontally.
   * Cause B adds the rest:
     * about one step of the vertical speed at a jump start or landing (7.4 HU at 442 HU/s). Under fix A, body - root
       still changes at that pair edge by up to 7.9 HU (standing jump) and 9.5 HU (running jump); with A+B by 2.8 and
       3.5 HU;
     * step-ups;
     * the landing sink after a long fall. In probe2, Sonic running off the tower lands in the fall action 18 and
       Sonic jumping at its edge lands in the ball; both pairs are matched. Body - root drops from 49.4 to 22.6 HU
       (run-off) and from 48.4 to 22.7 HU (ball) within the first pair after touchdown at about 1600 HU/s.
   * Which cause the owner saw depends on the character and the map. Ask the owner, with `gsa1_debug 1` on:
     * "moved with the root" shown during the flicker means cause A;
     * a flicker on landings, steps and ledges with interpolated frames means cause B.
3. **"0 reversals for Knuckles after A (and A+B)" does not mean no vertical back-and-forth.** The reversal metric
   needs more than 1 HU of drift inside a pair, and after fix A that drift is 0 by construction, so the metric cannot
   see a change that happens only at pair edges.
   * In `vf_test_fix1 --s0 --trace knuckles_jump_stand`, t 1.00-1.47, Knuckles' rise still alternates every pair
     between the ball (body - root z +22.80) and the spinning curled body (+16.69, +20.30, +28.24, +26.77, +18.42,
     +17.66, +25.80). That is a step of 2.5-6.1 HU at every pair edge, at 30 Hz, with A+B.
   * On the way down only the ball is drawn (matched, constant +22.80).
   * This is the game's own display drawing two models in turn, not a root error, and A and B rightly leave it.
   * The doc should say that it remains: the owner may still call Knuckles' rise "flickery". Whether the Dreamcast
     shows the same at 30 fps is UNVERIFIED; check it with the harness `--disp` or footage from real hardware.
4. **Section 3.2's Dreamcast remark is contradicted by libsa1's own notes.** The remark is "the camera task ran within
   the same step as the drawn display, so camera and body agreed". But `sa1_world_tick.c:27-28` and `:65-68` say the
   game's camera task leaves its view in XMTRX for the **next** step's displays. The sub-step-0 display therefore
   drew with the view computed in the step before. The Dreamcast camera is a lagging, smoothed follow camera anyway,
   so this neither supports nor weakens fix B. Delete the sentence, or mark it as contradicting `sa1_world_tick.c`.
5. **Naming.** There is no "libsa1 0.5": the library reports 0.2.0 (`sa1_version_string` in every run). Write "the
   libsa1 of G-SA1's 0.5 round", also in the proposed `session.cpp` and `libsa1.h` comments.
6. **Line references.**
   * The `libsa1.h` comment is at :657-658 in the 0.5.0 header (`g-sa1/build/deps/include`) and at :667 in the live
     header, which round 4b2 edited at 05:46.
   * Round 4b2 also changed `sa1_character.c`, `sa1_api_world.h`, `sa1_world.h` and `sa1_api_world.c` (05:32-05:42)
     after the 0.5.0 build at 04:38.
   * The `sa1_character.c` lines cited in the doc match the live file, and their behaviour matches the 0.5.0 binary
     (probe). Still, anchor both fixes by function name.

### R.3 Gaps

1. **No in-game confirmation** (acknowledged in section 8). The owner's character, map and frame rate are unknown.
2. **The offline gm_construct is not the world the game uses.**
   * `vf_test` calls the 4-argument `Bsp::build_world`, which adds no static-prop convex pieces and uses default
     options.
   * The module's map job passes the static props' pieces (`mapjob.cpp:810`) and the configured options, and adds
     movers every frame (physics props, brush entities).
   * The "1 reversal in 4,891 steps" and the cause-B sizes on gm_construct therefore cover brushes and displacements
     only. Static props (curbs, rocks, benches) are typical step-up sources for cause B.
3. **"Camera = root + constants" holds only in the open.** `CalcView` hull-traces up by `cam_height` and back by
   `cam_dist` against brushes (`cl_character.lua:403-410`).
   * Under ceilings and against walls the camera is clamped and does not move linearly with the root.
   * During an interpolated step-up pair, the interpolated centre (root + 20 HU) can pass through the ledge's corner,
     and a trace that starts in solid behaves differently.
   * This was not measured and needs GMod. It is not a cause of the measured sawtooth, but it is a possible extra
     camera pop on ledges.
4. **The `--s0` emulation of fix B is partial.** It recomputes the shift only for non-interpolated frames. Unmatched
   draws inside interpolated frames (partial matches, effects) keep the sub-step-1 shift. The difference is small, but
   fix B is only verified once libsa1 is rebuilt with it.
5. **Effects drawn on both sub steps.** Effects drawn through `njDrawModel`, `njDrawObject` or the Ninja motion draws
   are recorded on both sub steps (`rt_draw.c` comment at :700-716). The sub-step-1 copies sit one step ahead of the
   body, today and after fix B. This is not part of this symptom; it is a note for the light-speed visuals round.

### R.4 Required changes to the proposed fixes

**Fix A: approved, with these changes.**

* `r_shifted = !interpolated && alpha < 1` also flags frames where libsa1 moved nothing (no previous pair: the first
  pair after creation, or after the CPU-pose fallback). That is harmless for the overlay; say so in the comment, or
  have libsa1 report the move itself.
* Strengthen the test: in the new cases, CHECK in every frame with `interpolated == 0` and alpha < 1 that the
  Session's triangles, sprites and line vertices equal those of `sa1_character_render(alpha)` (max difference under
  1e-4 HU).
  * Before the fix: up to 12.4 HU (Knuckles standing), 26.0 (Knuckles running), 26.4 (Sonic's running jump) and 24.5
    (Tails' running jump).
  * After it: 0.0000 (probe.cpp).
  * The check is deterministic, independent of the centroid metric, and catches any later double move.
* Do not CHECK Sonic's reversals = 0: 5 remain under A at 144 fps (model changes plus cause B).
* Add Knuckles jumping at the edge of the 3000-unit tower (R.1, item 7). Today the second shift reaches 56.2 HU and
  body - root z swings by about 55 HU every pair. After the fix the shift is 0.
* Coordinate with round 4b2. Its G-SA1 step owns `session.*` and is bumping G-SA1 to 0.5.1 right now. Apply fix A on
  top of 4b2's `session.cpp`, anchored by function name:
  * `Session::render`'s `root_last` block and its `follow_root` call;
  * `Session::follow_root`;
  * `Session::remember_root` and its call in `Session::tick`;
  * `kMaxRootShift`.

  Ship it in the same 0.5.1 if possible, otherwise in 0.5.2.

**Fix B: approved, with these changes.**

* Convert with `sa1_to_host_point(ctx, drawn_pos, ...)` and `bams_to_yaw` (`state_of`, `sa1_character.c:606/610`),
  not a hard-coded 4 (x, -z, y), so non-default `ctx->coords` stay correct.
* Document the transitions. The CPU-pose fallback (the hidden branch of `render_capture`) and `sa1_character_pose`
  still use the state root (sub step 1), so the first captured pair after a fallback starts one step back. That is
  acceptable.
* The G-SA1 CHECK "|lead at top speed - lead standing| < 2 HU" is not well defined: the lead is measured along the
  velocity, and a standing character has none. Use one of these instead:
  * |lead| < 2 HU at top speed and after the spin dash (emulated: -0.3 and 0.2; today 17.1 and 39.8);
  * |lead at 1043 HU/s - lead at about 500 HU/s| < 2 HU.
* LIB test: add probe3's per-node check, with or instead of the centroid-against-standing check, which depends on the
  pose. The check: node 0's z minus the render root's z stays constant within 0.05 HU during a fall from 0 to
  1589 HU/s. Today, node minus render root grows from 23.8 to 48.1 HU.
* Run LIB `build.py --test` (the 10 traces) after round 4b2's LIB edits land. Fix B only reads guest memory after sub
  step 0, so V5, V6 and DISC should not change. It does change the `sa1g_world` struct (x86 and x64), so this is
  UNVERIFIED until run.

### R.5 Verdict

**Cause A is confirmed.**
* It is exact at the vertex level and a regression of the 0.5 round.
* Fix A is correct and safe.

**Cause B is confirmed.**
* The drawn body is exactly one step behind the root, measured per node.
* It is older than 0.5 and real, but it shows as single events (jump starts, landings, steps) rather than a steady
  back-and-forth.
* Fix B is correct.

**Two things are still open.**
* That these two are what the owner saw is UNVERIFIED in game.
* Knuckles' 30 Hz alternation between ball and body remains after both fixes, and is expected to.
