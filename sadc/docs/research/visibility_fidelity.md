# Visibility test fidelity: the reference simulator has no screen

Question (2026-10-20): libsa1's draw capture makes the game's sphere visibility test `8C038D00` answer 1,
where every reference run of the harness answers 0. With the answer 1, Tails' display `8C0BA820` and
Knuckles' display `8C0C46AC` run logic that the references never ran, so `tails_sh1` / `knux_rm3` differ
from step 74 / 90. Which behaviour does the real game have, and are the references faithful?

**Short answer.** The references are not faithful. On a Dreamcast the test passes for the player
character on every first logic step of a frame (sub step 0), because the camera keeps the player on screen.
In the simulator it can never pass. The cause is the HLE stub list of `harness/src/t_sim.c`, which turns
`8C604702` into a no-op labelled "vsync wait (SDK)". That function is really Ninja's `njInitSystem(mode,
frame, count)`. Without it the screen size, the aspect and the 2D clip rectangle stay at 0, and so does the
projection distance that the game derives from the screen width.

A new harness option, `--screen`, restores the view part of `njInitSystem`:

* 10 of the 15 references stay identical record for record: all 5 Sonic EC1 scripts, `amy_tp2`, `big_ec3`,
  `gamma_fe1`, `obj_fe1_gamma` and `obj_tp2_amy`.
* 5 change: `tails_sh1`, `knux_rm3`, `obj_ec1_sonic`, `obj_ec3_big` and `obj_rm3_knux`. In `obj_rm3_knux`,
  Knuckles' position diverges from step 96, by up to 1,624 units.

**Recommendation.** Re-record the references with `--screen` (the candidates are in `build/traces/ref_disp/`).
Do not use `--disp` for this. Change libsa1 so that it answers the test like the original:

* 0 on sub step 1;
* on sub step 0, "visible" for the player's display, whether or not a capture window is open;
* with the screen state set up as `--screen` sets it.

Details are in section 7.

## 1. The test, `8C038D00` (hand-written SH-4, 160 bytes)

Its arguments arrive in FR4-FR7 (x, y, z, r: a sphere in world space) and the result comes back in R0. The
callers are the displays of Sonic `8C0AE17A`, Tails `8C0BA820`, Knuckles `8C0C46AC` and Big `8C0E3A40`,
plus 43 other callers (`tools/funcinfo.py func_8C038D00`), among them object displays such as Kiki's
`8C0FC160`. The displays of Amy (`8C0DA122`)
and E-102 (`8C0CFACE`) do not call it: in `amy_tp2` / `gamma_fe1` it is called 0 times. Read from the asm:

```c
Sint32 func_8C038D00(Float x, Float y, Float z, Float r)
{
    Float vx, vy, vz, iz, kx, ky;
    if (B_8C754EC8 != 0)                    /* sub step 1 (the 2nd logic step of a frame): never visible */
        return 0;
    (vx, vy, vz) = XMTRX * (x, y, z, 1);    /* FTRV: the current matrix = the camera view */
    if (!(vz > B_8C88F610 - r)) return 0;   /* near (B_8C88F610 = -near of njClipZ 8C64036C) */
    if (vz > B_8C88F614 + r) return 0;      /* far */
    iz = 1.0f / (vz + r);
    kx = B_8C88F5E4 * B_8C88F618 * iz;      /* distance * aspect x */
    ky = B_8C88F5F0 * B_8C88F61C * iz;      /* sic: screen centre x * aspect y, not the distance */
    if (!((vx + r) * kx + B_8C88F5F0 > B_8C88F600)) return 0;   /* cx, clip x min */
    if ((vx - r) * kx + B_8C88F5F0 > B_8C88F608) return 0;      /* clip x max */
    if (!((vy + r) * ky + B_8C88F5F4 > B_8C88F604)) return 0;   /* cy, clip y min */
    if ((vy - r) * kx + B_8C88F5F4 > B_8C88F608) return 0;      /* sic: kx and the clip x MAXIMUM (F60C is loaded, unused) */
    return 1;
}
```

The two "sic" lines are quirks of the original, which libsa1 must keep if it re-implements the test
(Ghidra's draft shows the same operands). The test reads five pieces of state:

| State | Meaning | Set by |
|---|---|---|
| `B_8C754EC8` | logic step index within the display frame (0, 1) | the frame loop `8C04E95A` (0, 1; after the loop it is 2) |
| XMTRX | the current Ninja matrix = the camera's view matrix when a display runs | the camera task (previous step) |
| `B_8C88F5E4` dist, `F5E8` w, `F5EC` h, `F5F0` cx, `F5F4` cy | Ninja screen | `njInitSystem` `8C604702` (w, h), `njInitView` `8C604BA0` (dist 500, cx = w/2, cy = h/2), the game's `8C0544E2`: `8C640528` (njSetScreen: dist, cx, cy from `B_8C7889FC` = 640 x 480, centre 320, 240), `8C640500(0x31C7)` (njSetPerspective: dist = w / (2 tan 35 deg) = 457.06); the camera later changes the angle (dist 456.4, 454.6 in SH1) |
| `B_8C88F600..F60C` | 2D clip rectangle x0, y0, x1, y1 | `njInitView`: (0, 0, w-1, h-1) |
| `B_8C88F610` / `F614` | near / far | `njInitView` (1, 60000), then `njClipZ(-3, -3000)` in `8C0544E2`; stages set far (2500 on Speed Highway) |
| `B_8C88F618` / `F61C` | aspect x / y | `njInitView`: 1, 1 |

### Why the simulator always answers 0

`8C604702` is called once, by the game's video init `8C010180` (game init `8C053AC0` -> `8C0546C0`), as
`njInitSystem(mode, 0, 1)`. The mode is 1 with the VGA cable that the harness BIOS reports, and 0x38 / 0x3A
otherwise; all three give 640 x 480. The function:

1. stores the mode words `B_8C88F620/24/28`;
2. derives the screen size from the mode bits (floats `B_8C88F5E8/EC`, integers `B_8C88F644/48`);
3. sets up the video (`8C6045B4`);
4. busy-waits on Holly (`0xA05F6900` bit 3, inline, at 8C60487C), which is why it cannot run in the
   interpreter;
5. initialises Kamui (`8C65262C`, `8C6526BC`, `8C653386`) and the interrupt handlers (`8C64133E`,
   `8C64162A`, `8C64167E`);
6. calls `njInitView` `8C604BA0`.

The simulator's `sim_stubs[]` makes the whole function a no-op. RAM after the stage start
(`tails_sh1`, step 30):

| | dist | w x h | cx, cy | clip rect | near / far | aspect |
|---|---|---|---|---|---|---|
| simulator (references) | **0** | **0 x 0** | 320, 240 | **0, 0, 0, 0** | 3 / 2500 | **0, 0** |
| `--screen` (= hardware) | 457.06 | 640 x 480 | 320, 240 | 0, 0, 639, 479 | 3 / 2500 | 1, 1 |

With kx = 0, the x maximum check compares cx = 320 with the clip maximum 0, so every call returns 0. The
player is right in front of the camera all the same. At step 2 of `tails_sh1`, Tails' view-space centre is
(0.0003, 7.06, 37.98), the middle of the screen.

The same zero screen also makes `njProjectScreen` `8C639BDC` map every point to the screen centre
(320, 240). Game logic that asks "is this point on screen" therefore always gets yes: `8C0480C6`, for
example, accepts 0 <= x <= 640 and 0 <= y <= 400.

## 2. When the displays run on the Dreamcast

* **From the mains, on every logic step.** TailsMain `8C0B6148` calls `func_8C0BA820(tp)` near its end
  (`code_8C0B6148.c:612`, before `CCL_Entry`). The other mains do the same: Sonic at `code_8C0A77BC.c:962`,
  Knuckles at `code_8C0C0640.c:642`. The frame loop `8C04E95A` runs two logic steps per display frame (sub
  0, 1), so a display is called twice per frame.
* **The test gates the drawing and the logic in it.** It returns 0 on sub 1, so a display's visible path
  runs at most once per pair, on sub 0, and only when the sphere is inside the view. The hand-written model
  renderers `8C03700C` / `8C037098` have the same `B_8C754EC8` gate and their own frustum test `8C03718C`.
* **`8C0988C4` (DisplayAllTasks) is not part of a normal frame.** It calls every task's `disp`. Its only
  callers are `8C04EB7E` (the pause menu frame) and `8C04ECD0` (another frozen state of `8C04CA40` /
  `8C04DA20`). Both set `B_8C754EC8 = 0` first. During play the game never runs it.
* **The harness's `--disp` is not hardware behaviour.** It runs `8C0988C4` after every frame, with
  `B_8C754EC8` left at 2, so the test still answers 0. Displays that are not gated by the test run a second
  time per frame. Amy's display advances her hammer morph (`pwp+0x98`), so with `--disp`, `amy_tp2` differs
  from step 204 and `obj_tp2_amy` from step 156. The statement in `harness_simulator.md` that runs with and
  without the display pass are identical holds only for the Sonic EC1 scripts it was checked on.

## 3. The harness options added for the experiment (`harness/src/t_sim.c`; the default is unchanged)

| Option | |
|---|---|
| `--screen` | `8C604702` gets an HLE instead of the no-op: the mode words, the screen size from the mode bits as the original computes it, then the original `njInitView` `8C604BA0` in the interpreter. The hardware part (Kamui, PVR, interrupts) stays out. The hand-written renderers `8C03700C` / `8C037098` return at once, as their frustum test made them do before: now that the test passes they would reach their TA output, which reads renderer state the simulator does not set up and faults at `8C037CCC` (like the land strip renderer `8C037108`, which was already stubbed). They only draw. Trace header flag bit 3 (8) marks such a run. |
| `--log-vis FILE` | one line per call of `8C038D00`: phase, step, sub, caller (PR), x y z r, the view-space centre XMTRX * (x, y, z, 1), the result, plus the screen words whenever they change |
| `--vis-force 0\|1\|sub0` | answer the test instead of running it: always 0, always 1 (libsa1's capture window today), 1 on sub step 0 only |
| `--vis-only PR,..` | run the test only for these call sites (return addresses), answer 0 for the others: used to find which display causes a difference |

`regress.py` with the patched build (default options): 15/15 identical. All runs below used the private
pure build `harness/build/sa1h_fid.exe` (`build.py --files none --tag fid`) and were compared against
`build/traces/ref/*.bin` with `--check`, so every byte except `icount` was compared.

## 4. Results over the 15 scripts

First difference against the current reference (`=` means identical):

| Script | A `--screen` | B `--screen --vis-force 0` | C `--vis-force sub0` | D `--vis-force 1` | E `--disp` | F `--screen --disp` vs A |
|---|---|---|---|---|---|---|
| ec1_stand, ec1_run_jump, ec1_jump_hold, ec1_spindash, ec1_turns | = | = | = | = | = | = |
| amy_tp2 | = | = | = | = | 204 | 204 |
| gamma_fe1 | = | = | = | = | = | = |
| big_ec3 | = | = | = | = | = | = |
| **tails_sh1** | **74** | = | 74 (= A) | 74 (vs A: 75) | = | = |
| **knux_rm3** | **90** | = | 90 (= A) | 90 (vs A: 91) | = | = |
| **obj_ec1_sonic** | **55** | 198 | 55 | 52 | = | 666 |
| **obj_ec3_big** | **57** | = | 57 | 54 | = | = |
| obj_fe1_gamma | = | = | = | = | = | = |
| **obj_rm3_knux** | **12** | = | 12 | 11 | = | = |
| obj_tp2_amy | = | = | 65 | 54 | 156 | 156 |

The columns isolate one mechanism each:

* **B: the visibility test is the cause.** With the screen set up but the test forced to 0, everything is
  identical except `obj_ec1_sonic` from step 198. That one is the projection: at step 198 an object
  projects a point with `njProjectScreen`. With the real screen it takes a branch that draws from
  `rand()`; with the zero screen it does not.
* **C: for the character-only scripts, "visible on sub 0" is exact.** The `--vis-force sub0` traces of
  `tails_sh1` and `knux_rm3` are **byte-identical to the `--screen` traces**. So for the player, "visible on
  sub 0" is exactly what the frustum test answers. In the `--screen` runs the player is visible on every
  sub-0 step from step 1 on. Step 0 is special: the camera has not run yet (section 8). The smallest
  margin to the clip rectangle is 210 px (tails_sh1), 279 px (knux_rm3), 223 px (obj_rm3_knux), 274 px
  (big_ec3) and 279 px (ec1_turns). For objects it is not exact: `obj_tp2_amy` is identical with `--screen`, but
  differs from step 65 when every object is forced visible.
* **D: "always visible", libsa1's capture today, is not hardware behaviour.** Running the display logic on
  sub 1 too advances Tails' tail morph, flight-tail spin and `btimer`, and Knuckles' blink counter, twice
  per pair. Against the faithful traces, D differs one step later than against the references (75 / 91),
  and Tails reaches action 38 at step 189 instead of 190.
* **E / F: `--disp` neither fixes nor helps anything.** It changes only what the game does not do (section 2).

The XMTRX at the display call is the matrix at the player-exec entry (`px_xmtrx`, trace v2). Recomputed
offline, Tails' and Knuckles' view centres from `px_xmtrx` match the logged ones on all 414 / 319 calls,
within 1.3e-5 (float rounding of the recomputation).

Calls of the test per run (`--screen`; calls / visible, of which on sub 0):

| | calls | visible | sub-0 calls | sub-0 visible |
|---|---|---|---|---|
| tails_sh1 | 414 | 206 | 207 | 206 |
| knux_rm3 | 320 | 159 | 160 | 159 |
| big_ec3 | 392 | 195 | 196 | 195 |
| ec1_stand / run_jump / jump_hold / spindash / turns | 180 / 272 / 322 / 242 / 480 | 90 / 136 / 161 / 121 / 240 | the same | all |
| obj_ec1_sonic | 16,606 | 4,418 | 8,290 | 4,418 |
| obj_ec3_big | 18,873 | 3,148 | 9,684 | 3,148 |
| obj_rm3_knux | 103,275 | 22,515 | 51,612 | 22,515 |
| obj_tp2_amy | 19,477 | 2,145 | 9,729 | 2,145 |
| obj_fe1_gamma | 1,303 | 0 | 652 | 0 |
| amy_tp2, gamma_fe1 | 0 | | | |

Cost: the draw paths now run, so `tails_sh1` takes 371,061 instructions per step instead of 277,639
(+34 %), and `obj_rm3_knux` 622,197 instead of 392,493 (+59 %).

## 5. What the visible path changes

All of the following is in the `--screen` traces and not in the references. The first divergence of each
script was traced to one call site with `--vis-only`.

* **Tails, `8C0BA820`** (`code_8C0BA820.c`), on every visible sub-0 step:
  * writes the world centre of collision shape 0 (`CWP(twp)->info[0].center`): the point `p.height / 2`
    above `twp->pos` along the player's up axis;
  * advances the tail morph `pwp->free.f[6]` (+-0.01 per pair), the tail angles (`D_8C258974` /
    `D_8C2582E8`) and the flight-tail spin (`8C0BDB72`, `free.f[7]`);
  * counts `twp->btimer` while `twp->flag & 0x100` (the jump ball);
  * switches `reqaction` 37 -> 38 with `mtnmode` 2 once `free.f[7] >= 0.4`.

  In `tails_sh1`:
  * `pwp+0x94` (f[6]) differs from step 74 and `btimer` from step 120;
  * the flight goes 37 -> 38 (step 190) -> 39 (step 233) instead of 37 -> 40 (step 301), with nframe,
    action, last/next action following;
  * `rand_state` differs from step 192;
  * position and speed stay identical (no objects).
* **Knuckles, `8C0C46AC`**: the same collision-shape-0 centre, the blink counter `pwp->free.w[4]`
  (`pwp+0x84`, from step 90 in `knux_rm3`) and the action-51 tilt `free.w[3]`. RAM at steps 40, 41 and 120
  of `knux_rm3` shows the effect on the shape: **in the reference, Knuckles' collision shape 0 stays at its
  table value (0, 1, 0), near the world origin, for the whole run**. With `--screen` it follows him, for
  example (-3147.8, 763.1, -1358.8) while he stands at (-3148.1, 757.4, -1358.9). With objects this decides
  what he touches. In `obj_rm3_knux`, Knuckles' display alone (`--vis-only 8C0C46E0`) reproduces the
  `--screen` trace up to step 683. The effects:
  * `land_count` 16 -> 17 at step 12;
  * his angle and position from step 96 (up to 1,624 units apart later);
  * mode, action and `rand_state` later on.
* **Objects.** Kiki's (E_SARU) display `8C0FC160` (call site `8C0FC196`) draws from `rand()` when visible.
  This is the first difference of `obj_ec1_sonic` (step 55) and `obj_ec3_big` (step 57); `--vis-only
  8C0FC196` alone reproduces them up to steps 543 / 123. In `obj_ec3_big` the RNG change reaches Big:
  mode / action from step 391, rings from 409, position from 490.
* **Projection.** With a real screen, `njProjectScreen` gives real screen positions instead of (320, 240).
  In `obj_ec1_sonic` this sends an object into a branch that draws from `rand()`, at step 198 (column B).
* **Sonic `8C0AE17A`, Big `8C0E3A40`**: visible on every sub-0 step from step 1 on, but nothing recorded
  changes.

At setup, `--screen` changes these globals:
* the screen words `B_8C88F5E4..F648`;
* `njInitView`'s tables `B_8C88F834`, `FB34`, `FCB4..FCF4`, `B_8C8E9FA0`, `B_8C8EA0E0`, `B_8C8EA3xx`,
  `B_8C8A2Dxx..B_8C8A3298` and `B_8C8FFE00/14`;
* values the game derives from the screen: `B_8C6AEE28+0x80` (1.0 -> 1.13) and `B_8C749114..11C`.

None of them changes a trace except through the test and the projection (column B).

## 6. Verdict on the references

The 15 references in `build/traces/ref/` were recorded with a view that cannot exist on a Dreamcast. On
hardware the screen is 640 x 480 and the player is on screen, so the visibility test passes on sub 0. The
references are therefore:

* **unfaithful for Tails and Knuckles.** Display logic is missing (collision-shape centre, Tails' flight
  actions 38 / 39, counters). With objects, Knuckles' body collision sits at the world origin, and
  `obj_rm3_knux` diverges in position from step 96;
* **unfaithful in the RNG for object scripts** with visible objects whose displays draw from `rand()`
  (`obj_ec1_sonic`, `obj_ec3_big`), and for objects that test projected screen positions;
* **faithful for Sonic, Amy, E-102 and Big without objects, and for `obj_fe1_gamma` and `obj_tp2_amy`.**
  Their `--screen` recordings are identical record for record; only header flag bit 3 differs.

## 7. Recommendation

1. **Re-record the references with `--screen`, not with `--disp`.** The candidates are
   `build/traces/ref_disp/<script>.bin` / `.csv`. They were recorded twice (with and without `--log-vis`)
   with identical results; the pure build passes `regress.py` 15/15 on the old references. Steps for the
   harness owner:
   * Make `--screen` the default of `trace` (an opt-out `--no-screen` keeps the old behaviour). Today
     `regress.py` cannot append an option to scripts with a `# args:` line, because their line replaces
     `--args`.
   * Correct the `8C604702` entry in `sim_stubs[]` and in the HLE table of `docs/harness_simulator.md`. Add
     header flag bit 3 to `sa1_trace.h`. Narrow the "with and without `--disp`: identical" statement
     (section 2).
   * `regress.py --update`, or copy the candidates. Keep the old files, for example as
     `<name>.noscreen.bin`. The `*.v1.bin` of the 5 changed scripts no longer matches the new v2 references
     (`--v1-compat` reports them).
   * Re-take derived data of the changed scripts: `build/verify/coverage/*.txt` (the draw paths now run),
     the route matrix and the write census.
2. **libsa1: the visibility test.** `rt_draw.c` answers 1 on every sub step while a capture window is open
   and 0 otherwise. Neither is the Dreamcast. It should:
   * keep the original's first test in every mode: `B_8C754EC8 != 0` -> 0;
   * on sub 0, answer **visible for the player's display**, with or without a capture window. This is
     exactly what the frustum test answers for the player in all 10 character scripts (column C ==
     column A, byte for byte). The capture window then only records: `capture_test.c` part 1 should find
     **all six** characters unchanged, against the new references. The displays draw once per pair, on
     sub 0, as the hand-written renderer already does.
   * for objects (worlds with SET objects) or a host camera, run the original test (section 1, including
     its two quirks) against the world's screen state and the view matrix in XMTRX. In replays that is
     the record's `px_xmtrx`, which equals XMTRX at the display call. In a live world it is a view matrix
     from the host camera. With no view, "visible on sub 0" is the fallback.
3. **libsa1: the screen state.** `sa1_world_boot.c` (`sa1g_boot_runtime`) copies the reference's zero
   screen on purpose ("0 with the SDK's zero B_8C88F5E8, as in the reference"). It should set what
   `njInitSystem` + `njInitView` + `8C0544E2` set:
   * w x h = 640 x 480 (`B_8C88F5E8/EC`, `B_8C88F644/48`);
   * aspect 1, 1 (`F618/F61C`);
   * clip rectangle (0, 0, 639, 479) (`F600..F60C`);
   * dist = 640 / (2 tan 35 deg) = 457.06 (njSetPerspective `0x31C7`);
   * near / far from the game's `njClipZ` calls.

   `njProjectScreen` (rt_matrix.c) and the screen tests built on it (`8C0480C6`, ...) then behave like the
   new references; column B shows that this matters (obj_ec1_sonic, step 198).
4. **Tests to adjust together with the references:**
   * libsa1's replays of `tails_sh1` / `knux_rm3` / the obj scripts need items 2 and 3;
   * `capture_test.c` (part 1 expects Tails and Knuckles to differ with the window open);
   * the M1 / route-matrix expectations of the changed scripts.

   Keep the old references, and `--vis-force 0` with the old screen, until libsa1 follows. The current
   behaviour (old references + "0 outside capture") stays reproducible.

## 8. Limits of `--screen`

* Only the view part of `njInitSystem` runs: the mode words, the screen size and `njInitView`. Kamui, the
  PVR, the interrupt handlers and the vsync flags (`B_8C88F7D0..D8`) stay out, as before. The mode is the
  one the game passes: 1 with the harness's VGA report. NTSC / PAL (0x38 / 0x3A) give the same
  640 x 480 screen.
* The hand-written renderers `8C03700C` / `8C037098` (and the land strip renderer `8C037108`, as before)
  return at once. The texture-key cache, the material words and the TA output they would update are
  render state that only the renderers read (libsa1 `rt_draw.c`, section 3), and the references never
  reached that code either. The SDK draws (`njDrawModel`, ...) run with the real screen and write to the
  store queues (no fault).
* The interpreter's FTRV, FSCA and FSRRA are approximations (`harness/README.md`). The player is at least
  210 px inside the clip rectangle, so his answer cannot flip. An object right at the screen edge could
  flip in rare cases.
* The in-game state machine `8C04CA40` still does not run: no fade-in, no pause. In particular the pause
  display pass `8C04EB7E` never runs; it calls the displays with sub 0 and runs their logic while paused.
* The camera runs after the player in the task order. At step 0 XMTRX is still diag(1, -1, -1, 1), so
  the answer depends on where the stage puts the player: Sonic at (-9, 16, 4), near the origin, is
  "visible"; Tails, Knuckles and Big are not. Forcing step 0 visible (column C) changes nothing recorded.

## 9. Files and reproduction

* `harness/src/t_sim.c`: `--screen`, `--log-vis`, `--vis-force`, `--vis-only` (section "--screen,
  --log-vis, --vis-force, --vis-only", `hle_nj_init_system`, `vis_hook`). Without these options, `regress.py`
  is 15/15 identical.
* `build/traces/ref_disp/<script>.bin / .csv`: the candidate references (`--screen`, header flags | 8).
* `build/traces/ref_disp/run_screen/`: the same with `--log-vis` (`<script>.vis`: every call of the test,
  with the view-space centre and the result).
* `build/traces/ref_disp/run_screen_vis0/` (B), `run_vis_sub0/` (C), `run_vis_1/` (D), `run_disp/` (E),
  `run_screen_disp/` (F): the experiment runs of section 4.

```
py -3.12 harness/build.py --files none --tag fid
harness/build/sa1h_fid.exe trace --input @harness/tests/sim/tails_sh1.txt --level 4 --act 0 --chr 2 \
    --no-objects --print 0 --screen --log-vis build/traces/ref_disp/tails_sh1.vis \
    --out build/traces/ref_disp/tails_sh1 --check build/traces/ref/tails_sh1.bin
#   sim: --screen: njInitSystem(1, 0, 1) -> screen 640 x 480, njInitView 8C604BA0 ran
#   sim: visibility test 8C038D00: 414 calls, 206 visible; on sub step 0: 207 calls, 206 visible
#   check: first difference at step 74:   step 74: pwp_raw+0x94: 00 (reference) vs 0A (now)
py -3.12 harness/tools/sa1trace.py diff build/traces/ref/tails_sh1.bin build/traces/ref_disp/tails_sh1.bin --all --raw
# attribution: only Knuckles' display may pass the test
harness/build/sa1h_fid.exe trace --input @harness/tests/sim/obj_rm3_knux.txt --level 5 --act 2 --chr 3 \
    --print 0 --screen --vis-only 8C0C46E0 --check build/traces/ref_disp/obj_rm3_knux.bin
```