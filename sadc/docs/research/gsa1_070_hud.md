# G-SA1 0.7 report: "Add in the HUD" (diagnosis and design)

Status: read-only diagnosis, 2026-10-21. Nothing under SADC/src, include, tools, harness, config, LIB or g-sa1 was
changed. Experiments: `SADC/build/research/hud/` (a probe linked against a snapshot of LIB's `libsa1.a`, taken under
LIB's build lock; see its `README.txt`).

Abbreviations: SADC = `<project>\sadc`, LIB = `...\libsa1`, G-SA1 = `...\g-sa1`.

Owner request (0.7 test): "Add in the HUD". The earlier answer to plan question Q3 (`gsa1_rounds_plan.md` 14.1a) asked
for the characters' 2D displays (E-102's lock-on cursor and timer, Big's reel and line tension, Knuckles' radar) as an
overlay. The new request is for the whole HUD.

---------------------------------------------------------------------------------------------------------------------

## 0. Summary

* **The game's HUD is a small set of 2D sprite draws.** The game draws it with `njDrawSprite2D` (8C63EEB4) on a
  640 x 480 screen, plus one `njDrawPolygon2D` (8C63EBC8) for Big's tension bar. The textures come from **CON_REGULAR**
  (the common HUD archive) and from each character's own archives (E102TIME, FISHING, KNU_EFF).
* **The in-play HUD has two parts**, called once per frame by the frame wrapper after the two logic steps:
  * `8C08A4BA` draws the rings and lives (draw `8C08A2C2`).
  * `8C08A724` draws the timer (draw `8C08A7E0`).
  * Both have C in SADC (NON_MATCHING wrappers around matching draws).
  * The SA Tools / code-map label "8C08ECB8 HUD master" is wrong: 8C08ECB8 is the stage-clear results tally (section 1.5).
* **Today libsa1 worlds draw no HUD at all.** Measured: 0 `SPRITE2D` records in 120 pairs for each of the six
  characters (probe part A). Four separate gaps cause it:
  1. libsa1's pair loop replaces the frame wrapper and never calls 8C08A4BA / 8C08A724.
  2. The game clock is stopped, so the timer would read 00:00:00 (E-102: 03:00:00).
  3. CON_REGULAR is not loaded, so the HUD textures do not resolve (`archive -1`).
  4. The capture records the 2D draws that do happen, but drops them at emission. Big's reel and gauge frame are
     recorded, but `sa1_render` has 0 sprites (part B). `njDrawPolygon2D` and Big's own HUD task are void stubs.
* **Proof of concept (probe part C).** Running the game's own HUD calls inside the pair, after sub step 1 and before
  the frame advance 8C08A5BE (where the game's 8C04E95A runs them), gives exactly the game's HUD:
  * 15 sprite records per pair: TIME label and ring icon, 3 ring digits, life icon, 2 life digits, 8 timer digits.
  * They sit in the same snapshot as the character's model draws.
  * Drawn with the disc's CON_REGULAR textures, they show the real SA1 HUD (section 2, images).
  * Part D: with the same inputs, the HUD pass changes no game state. Only 26 bytes of display state differ.
* **E-102 needs care.** Once the clock runs, E-102's 3-minute limit kills him at 00:00:00, as in the game. Measured in
  part E: SA1_EVENT_DEATH, mode 49, action 69. The design therefore adds an "E-102 time limit" option (default off).
* **Big has no standard HUD.** `8C08A4BA` and `8C08A724` skip player 7. His own HUD (WEIGHT, rings, lives) is drawn by
  his fishing HUD task, whose exec 8C0EB160 is a void stub in LIB. Six functions need decompiling (section 4.3).
* **The running fix round does not deliver the HUD.** It does deliver four of its prerequisites (section 3).
* **Design (section 4).** libsa1 gets:
  * a HUD pass (the game's own calls, split into parts with the game's own visibility flags);
  * game-clock and E-102 time-limit options;
  * start lives and time, so a respawn keeps the counters;
  * CON_REGULAR as a common archive;
  * 2D capture with priority and source;
  * a new output `sa1_character_screen`: 2D draws on the 640 x 480 virtual screen.

  G-SA1 gets a `hud` feature: a HUDPaint layer drawn through the mesh builder, sorted by the game's priority, scaled
  by H / 480. On wide screens each HUD element is anchored continuously to the edge it belongs to (3440 x 1440 shown
  in section 2). A Q menu page "HUD" has every option.

---------------------------------------------------------------------------------------------------------------------

## 1. The game's HUD code (inventory)

### 1.1 Who calls the HUD, and when

The main loop runs one frame per 30 Hz frame:

* `8C053940` calls the per-frame function `8C053B60`, which dispatches by game mode `B_8C760970`. Action play is case 4
  or 9: the state machine `8C04CA40`, which calls the frame step.
* The frame step's tail is `8C0439AE`, then `8C08A5BE` (`B_8C752BE8 += B_8C754EC4`, the frame advance).
  Ghidra: `build/ghidra/1ST_READ/decomp/8C053B60.c:500-501`.

The frame steps (`asm/1st_read/text/8C04D920.src`):

| Frame step | Used for | Logic | HUD calls after the logic |
|---|---|---|---|
| `8C04E95A` | action stages | 2 steps of `8C04E714` | `8C08A4BA()` (rings, lives), then `8C08A724()` (timer) |
| `8C04EA4A` | adventure fields | the same loop | `8C08A4BA()` only |
| `8C04EB7E` | pause | the display pass 8C0988C4 | `8C08A4BA()`, the pause menu, `8C08A724()`, `8C09E0B8()` |

In every case the HUD is drawn **after** both logic steps and **before** the frame advance.

**The step tail `8C04E714` sets the HUD's visibility counter.**

* `B_8C752BDD` = 0 normally, and -1 while an event runs (`8C049A4E`).
* In action stages it ticks the game clock `8C08A5CC`. `8C04C960() != 1` means the level type is not "adventure
  field".
* libsa1 reproduces this tail in `LIB/src/world/sa1_world_tick.c:139-156`.

**`8C04C960` gives the level type.**

* It returns 1 for adventure fields (levels 26-34) and for levels above 38.
* It returns 2 otherwise, which is the action-stage style. G-SA1 worlds run level 1, Emerald Coast, so they get 2.

### 1.2 The common HUD: rings, time, lives

All of these functions are in `SADC/src/1st_read`.

| Element | Function (file:line) | Decomp status | Draws (CON_REGULAR cell, size) | Screen position (640 x 480) | Data read |
|---|---|---|---|---|---|
| Wrapper | `func_8C08A4BA` (`code_8C08A4BA.c:25-36`) | C, NON_MATCHING | none itself | - | `B_8C752BDD >= 0`, not Twinkle Circuit (`8C0A1218`: level 35), `GetPlayerNumber() != 7` (**not Big**). It then copies BDD into BDE and BDF, or writes -1 in adventure fields (`8C04C960() == 1`) |
| TIME label and ring icon | `func_8C08A2C2` (`code_8C08A198.c:129-189`) | C, NON_MATCHING | cell 11 `hyoji_head` 64x64 | (16, 32) | `B_8C752BDF >= 0` (the ring flag) |
| Adventure-field ring icon | the same function | | `D_8C1BF5B4`: cell 11, v 68..255 only (the ring, without TIME), 64x47 | (16, 49) | levels 26-34 |
| Ring count | the same function, through `func_8C089F20` (`code_8C089F20.c:44-91`, matching) | | cells 0-9 `snum_*` 16x16, 3 digits, `NJD_SPRITE_COLOR` | (46, 52), step 16 | `B_8C752BE4` (Sint16). At 0 rings it blinks red: material (1, 1, c, c) with c = 1 - sin^2, and the phase `B_8C7997AC += 0x400` |
| Life icon | the same function | | cell 12 + GetPlayerNumber(): `hyoji_zanki_s` (Sonic) 12, `_t` 14, `_k` 15, `_a` 17, `_e` 18, `_b` 19, 32x32 | (16, 400) | `B_8C752BDE >= 0` |
| Lives | the same function | | 2 digits | (49, 416) | `B_8C752BE0` (Sint8) |
| Timer | `func_8C08A724` (`code_8C08A71E.c:55-68`), draw `func_8C08A7E0` (`:74-144`, matching) | C | mm:ss:ff, 8 cells (digits plus `snum_co` 10), 16x16 | (64, 33), step 16 | `B_8C752BE1/2/3` (min / s / frames; frames shown as `fr * 1.6667`). At 100+ minutes it shows "99:59:99" |
| E-102's timer colour | inside `8C08A7E0` | | | | E-102 (player 6) under 30 s: blue pulses with \|sin\|. Under 15 s: yellow to red (g = \|sin\|, b = 0) |

Further timer and score rules:

* **Score mode instead of the timer.** When `8C0A11DE()` is true and `B_8C78B451 == 1`, the score `B_8C752BF0` is shown
  instead of the timer, through `8C0A1120` and the label `8C0A11BE`.
  * `8C0A11DE()` is true on Sand Hill (level 38), and on Ice Cap act 3 in game mode 9.
  * Both functions have C (`code_8C0A1120.c`).
  * This matters for the board round.
* **No timer** on Sky Chase (36, 37) or in Twinkle Circuit (35).

The game clock is `func_8C08A5CC` (`code_8C08A4BA.c:82-128`):

* It counts up to 99:59:59 and stops there.
* For E-102 it counts down, and plays SE 0x30B every 2 s (every 1 s in the last 5 s).
* `func_8C08A580` (`:58-67`) resets it, and gives E-102 3:00.
* It runs only while `D_8C1BF198 = 1` (`8C08A5AE` on, `8C08A5B6` off).

Pop-up HUD in adventure fields:

* `8C08A198` (rings) creates task `8C08A178`, which forces `B_8C752BDF = 0` for 180 frames.
* `8C08A120` (lives) creates task `8C08A084`, which does the same for `BDE`.
* Both happen only when `8C04C960() == 1`.

Draw state: every HUD draw uses `njColorBlendingMode(0, 8)` / `(1, 6)` (SRCALPHA / INVSRCALPHA, i.e. normal alpha) and
attribute 0x20 (`NJD_SPRITE_ALPHA`) or 0x22 (`+ NJD_SPRITE_COLOR`). The colour comes from `func_8C043A20(a, r, g, b)`
(the `njSetConstantMaterial` wrapper). Priorities run from -1.5 (digits, front) to -2.001 (labels).

Texlists:

* `D_8C1BF1A4` = { `0x8C1C3070`, 24 } is the first 24 entries of CON_REGULAR's NJS_TEXNAME array.
* CON_REGULAR is `0x8C1C3310` = { `0x8C1C3070`, 56 }.
* `D_8C1BF19C` = { `0x8C1C3190`, 11 } is entries 24-34 (the big digits).
* Texture resolution in libsa1 works by NJS_TEXNAME address (`sa1_capture_resolve`, `LIB/src/sa1_capture.c:203-227`),
  so these shared sub-texlists resolve once CON_REGULAR is a loaded archive.

CON_REGULAR (56 textures; names from the disc's PVM):

| Cells | Contents |
|---|---|
| 0-9 | `snum_0..9` (digits) |
| 10 | `snum_co` (colon) |
| 11 | `hyoji_head` (TIME label and ring icon) |
| 12-19 | `hyoji_zanki_s/s/t/k/s/a/e/zankib` (life icons) |
| 20-23 | `level_s/a/b/c` (rank letters) |
| 24-34 | `digit0..9`, `colon` (results screen) |
| 35-45 | pause menu items |
| 46-55 | a third digit set |

Formats are twiddled, rectangle and mipmapped. All of them are supported by `LIB/src/sa1_pvr.c:227-230`. The archive
is 27,268 bytes of PRS and 318 KB as RGBA.

### 1.3 The characters' own displays

| Character | Display | Function (status) | Primitive / texture | Position | Runs in a libsa1 world today? |
|---|---|---|---|---|---|
| Sonic, Tails, Knuckles, Amy | the common HUD only | - | - | - | no (no HUD pass) |
| **E-102** | countdown timer (colour) | `8C08A7E0` (C) | CON_REGULAR | (64, 33) | no; the clock is stopped (03:00:00 forever) |
| E-102 | "+N" time bonus counter | exec `func_8C0D4E40`, display `func_8C0D4D08` / `func_8C0D50AC` (C: `code_8C0D4E40.c`, `code_8C0D4B00.c`); it pays the bonus through `8C0D4C30` -> `8C08AB42` | `njDrawSprite2D`, E102TIME cells 0-9 and 11 (`e102_getsecbg`) | pops at (540, 120), then flies off by (-14, -3) per frame (`code_8C0D4E40.c:90-91, 128-129`). **Fixed, not projected** | only when a lock-on kill pays time: round 5 H4 (5-lib-game, running). Recorded, then dropped at emission |
| E-102 | last-5-seconds digits | exec `func_8C0D6198` (created by E102Main `code_8C0CBE00.c:316-321` when the level is 25 or below), display `func_8C0D60C0` (C) | E102TIME digit at (320, 415.2) x1.98-2.2, `e102lastsec` (13) at (320, 380) x2.2 | centre bottom | measured in part E, recorded with the clock running; dropped at emission |
| E-102 | time-up death | `func_8C0D23E4` (C): `B_8C7A2EAA` (set at E-102's init) and `B_8C7A2EC1 = 8C08AB60()` (time zero) -> mode 49, `8C07BBC2` | - | - | yes, once the clock runs (part E) |
| E-102 | **lock-on cursor and target counters** | `8C0D5BE0` / `8C0D5E80` (`njDrawSprite3D`, E102TIME `e102_lockon0` and digits), reticle lines `8C0D5600` / `8C0D5A00` (`njDrawLine3D`) | **3D** sprites and lines in the world | world | **not a HUD job**: they are 3D effects. libsa1 already emits 3D sprites and lines (round 4c). They appear when E-102 can lock on to something: round 5 H4 (running) |
| **Big** | his HUD: WEIGHT (best fish), rings, lives | task exec **`8C0EB160` (void stub, `LIB/gen/policy/stubs.tsv:158`)** -> `8C0E6FE0` / display `8C0E70E4` (asm), numbers `8C0E74FC` (asm) through the HUD number draw **`8C08EE98` (void stub, stubs.tsv:137, asm)** | FISHING 61 `bighyouji` (WEIGHT and ring) at the TIME / RINGS spot, 62 `hyouji_g`, CON_REGULAR 19 `hyoji_zankib` at (16, 416), plus digits | top left / bottom left | **no: Big has no HUD at all.** Part C measured 0 sprites even with the HUD pass, because 8C08A4BA skips player 7 |
| Big | reel (turns while reeling) | `func_8C0E727A` (C, `code_8C0E727A.c:29-48`) | FISHING 58 `castgauge1` 64x32, attr 0x21 (ANGLE) | (592, 394) | **recorded** (part B: SPRITE2D, resolves to archive 1 index 58), **dropped** at emission |
| Big | line-tension gauge | `func_8C0E72DA` (C, `:50-90`): frame FISHING 57 `castgauge0` 128x256 at (576, 328) pri -1.819. Bar `njDrawPolygon2D` 4 points x 574..606, y 368 up by 135 x tension, green to red, pri -1.82 | sprite and **2D polygon** | right side | frame recorded and dropped; bar not recorded: **8C63EBC8 is a void stub** (stubs.tsv:240) |
| Big | line length readout | `8C0E746C` (asm; void stub stubs.tsv:156) through `8C08EE98` | digits | by the gauge (`LINE ... m` printed in `castgauge0`) | no |
| **Knuckles** | emerald radar (3 shards, colour / blink by distance) and the found-shard icons | task `8C0C99E4` (created for Knuckles' levels in `D_8C15C100`), exec `8C0C967A`, `8C0C94AC`, `8C0C9420`, `8C0C9294` (all asm); the 2D sprite `func_8C0CAC28` (C, `code_8C0CA780.c:130-143`) | KNU_EFF 11-20 `emerader0..8`, `emerader_n` | bottom (UNVERIFIED exact) | no: needs the emerald hunt's shards (`B_8C7A26E8`, 3 x 40 bytes) from the stage start's `8C0C905A`. G-SA1 maps have none. It belongs with the 1.0 "collectables" (emerald shards) |
| Knuckles | hint display | Tikal's hint orbs and message windows (HINT archive, the message / font system) | text windows | - | no; not a HUD element; out of scope (UNVERIFIED which functions) |
| **Tails** | race progress bar (Tails vs Sonic / Eggman) | `func_8C0817E0` (C, `code_8C0817E0.c:32-65`), called by the race opponents `8C07FA54` / `8C07FDFE` | 2D, `D_8C1BE9C0` | (184, 424) | no race opponent in G-SA1: not applicable (a later follower or race feature) |
| **Amy** | none of her own | the stubs reachable from AmyMain at 8C104DBC-8C106B3E are E-100 Zero's displays (`8C102E80-8C107800`) | - | - | - |
| Super Sonic (6a) | the common HUD (rings draining) | `8C08A4BA` / `8C08A724` work on level 19 (player 0, `8C04C960() == 2`) | | | comes with the HUD pass |
| Board (Sand Hill, level 38) | score instead of time | `8C0A11BE` / `8C0A1120` (C) when `B_8C78B451 == 1` | | | for the board round (`B_8C78B451`'s meaning UNVERIFIED) |

### 1.4 Data the HUD reads

All of it is the game's own state. Round 5's LIB ring API (5-lib-core, in LIB) already exposes and changes most of
it: `sa1_state_ex.rings` / `lives` / `score` (`LIB/include/libsa1.h:846-849`), `sa1_character_add_rings` (`:1507`) and
`SA1_CREATE_START_RINGS` (`:783`).

| Address | Holds |
|---|---|
| `B_8C752BE4` (Sint16) | rings |
| `B_8C752BE0` (Sint8) | lives |
| `B_8C752BE1/2/3` | time (min, s, frames) |
| `B_8C752BF0` / `B_8C752BF4` | score |
| `B_8C752BDD/BDE/BDF` | HUD visibility: all / lives / rings |
| `B_8C7997AC` | ring blink phase |
| `D_8C1BF198` | clock on / off |
| `B_8C7A2EAA` / `B_8C7A2EC1` | E-102 time limit armed / time is zero |
| `B_8C7A40D8` | Big's best weight |

The HUD therefore needs no host data. Rings that host objects add (the 1.0 collectables) appear by themselves.

### 1.5 Not part of the in-play HUD

* **`8C08ECB8` is the stage-clear results task, not the "HUD master".**
  * It is created by `8C04EFE0` / `8C04F20A` with `8C09852E(2, 1, 8C08ECB8)` (`build/ghidra/1ST_READ/decomp/8C04EFE0.c`).
  * It dispatches by stage:
    * `8C08CA40` (level 0);
    * `8C08E380` (Sky Chase);
    * `8C08E93A` (Twinkle Circuit);
    * `8C08DC32` (Sand Hill / board trial);
    * `8C08C100` (bosses in trial mode);
    * `8C08D1C0` (the action-stage tally, which loads `SCORE_BACK` / `SCORE_ACT` / `SCORE_RESULT`).
  * The code map row `0x8C089F20-0x8C08F1A0` ("func_8C08ECB8 HUD master") needs correcting (fix F7).
* Also not part of the in-play HUD: pause menu, game over (`8C042E20-8C043A20`), time display / continue / retry
  screens (`8C035000-8C036BC0`), Sky Chase and Twinkle Circuit HUDs, front-end 2D screens.
* `njDrawTexture` (`8C63ED08`, a void stub) is used by the E-102 effect `8C050540`: a 3D effect projected to the
  screen, not a HUD element (left for an effects round).

---------------------------------------------------------------------------------------------------------------------

## 2. What runs in libsa1 worlds today (reproduced)

The probe is `SADC/build/research/hud/hud_probe.c`. It is linked against LIB's `build/x86_64-windows/libsa1.a` as of
2026-10-21 14:08, copied with its headers under the build lock. It uses Emerald Coast with its stage tasks, as
`capture_test` does. Its outputs are `out_a.txt` .. `out_e.txt`.

### A. Baseline: no HUD at all

Setup: 120 pairs per character, idle, with a jump every 40 pairs.

| Character | `SPRITE2D` records | 2D records dropped | `sa1_render` sprites | Time | Rings | Lives |
|---|---|---|---|---|---|---|
| Sonic | 0 | - | 0 | 00:00:00 | 0 | 4 |
| Tails | 0 | - | 0 | 00:00:00 | 0 | 4 |
| Knuckles | 0 | - | 0 | 00:00:00 | 0 | 4 |
| Amy | 0 | - | 0 | 00:00:00 | 0 | 4 |
| E-102 | 0 | - | 0 | **03:00:00 (stopped)** | 0 | 4 |
| Big | 0 | - | 0 | 00:00:00 | 0 | 4 |

The clock is stopped because live worlds boot without `SA1G_BOOT_CLOCK` (`LIB/src/world/sa1_world_boot.c:457`,
`sa1_world.h:671`).

### B. Big fishing

Setup: Emerald Coast act 3, input "20n30a40n60a90n".

* Pairs with the lure out record 2 `SPRITE2D` each:
  * the reel: cell 1 at (592, 394), 64x32, centre (20, 16), FISHING 58, attr 0x21, blend 0x0101;
  * the gauge frame: cell 0 at (576, 328), 128x256, centre (64, 128), FISHING 57.
* Both resolve (archive 1, Big's FISHING).
* `sa1_render` on those pairs: 75 draws, **0 sprites**. The emitter only takes the 3D kinds
  (`LIB/src/sa1_capture.c:1071-1072`).
* The tension bar has no record at all (8C63EBC8 void).

### C. The HUD pass

Setup: 200 pairs, a step wrapper that runs the 8C04E95A tail after sub step 1 and before 8C08A5BE, with the clock on.

* **Sonic:** 15 `SPRITE2D` records next to 31 model draws in the same snapshot, so the frame-advance ordering works:

  | What | Cell(s) | Position | Colour |
  |---|---|---|---|
  | label | 11 | (16, 32) | white |
  | ring digits | 0 0 0 | (46/62/78, 52) | blinking (1, 1, 0.5, 0.5) |
  | life icon | 12 | (16, 400) | white |
  | lives | 0 4 | (49/65, 416) | white |
  | timer "00:06:64" | 0 0 10 0 6 10 6 4 | (64 + 16k, 33) | white |

  Every record: `tlist 8C1BF1A4`, **archive -1** (CON_REGULAR not loaded).
* **E-102:** the same layout with life icon 18. The timer counts down (02:53:21 after 200 pairs).
* **Big:** **0 sprites** (8C08A4BA and 8C08A724 skip player 7; his own HUD task is a void stub).
* `sa1_render` is unchanged: Sonic 48 draws, 0 sprites, the same as part A.

Drawn with the disc's textures (`compose.py`):

* `hud_gamma_640.png`: the real SA1 HUD. "TIME 02:53:34", ring icon "000" in blinking red, E-102's life icon "04".
* `hud_sonic_3440.png`: 3440 x 1440 with the continuous edge anchoring of 4.2.3. Scale 3; the elements sit in the
  top-left and bottom-left corners.
* `hud_*_3440_box.png`: the centred 4:3 alternative, offset 760.

### D. Does the HUD pass change the game?

Setup: 300 pairs with jumps, actions and stick, with and without the pass, both with the clock on. Guest RAM
8C010000-8D000000 compared (4,080 pages).

* Sonic and E-102: positions **bitwise identical**, rings identical.
* Exactly 4 runs, 26 bytes, differ, all display state:

  | Address | What |
  |---|---|
  | `8C1BF4E2-E7` | `D_8C1BF4E0`, the HUD's static sprite position |
  | `8C673480` | `D_8C673480`, the current texlist |
  | `8C7997AD-AE` | `B_8C7997AC`, the ring blink phase |
  | `8C88F66A-77` | `B_8C88F668`, the constant material |

* The game itself has these values every frame. With the pass, a world's draw state matches the original more closely.
  The pass must stay off in replays: the references were recorded without it, and the trace data never includes these
  words.

### E. E-102's time limit with the clock running

Setup: time set to 00:02:00 at pair 30.

* The time reaches 00:00:00 at pair 90.
* **SA1_EVENT_DEATH** fires in the same pair: mode 49, action 69, then mode 51.
* Two more `SPRITE2D` records come from the last-seconds task 8C0D60C0:
  * digit 0 at (320, 415.2), scale 1.98, E102TIME archive 3 index 0;
  * `e102lastsec` cell 13 at (320, 380), scale 2.2.
* These resolve (E102TIME is E-102's archive 3).
* The timer digits turn red (material r 1, g 0, b 0).

**Conclusion.** The game's HUD code works in libsa1 worlds as it is. What is missing is the call site, the clock, the
archive, the emission and the host layer. Big additionally needs decompilation.

---------------------------------------------------------------------------------------------------------------------

## 3. What the running fix round covers (sadc/tools/workflows/gsa1_round_fix060.js)

**The HUD itself is not in that round.** Its agents are fx-decomp, fx-lib-render, fx-lib-shadow, 5-lib-game, 5-oracle,
fx-gsa1-weapon-log, fx-gsa1-surfaces and fx-gsa1-render. None calls the HUD, loads CON_REGULAR, emits 2D draws or adds
a HUD layer. Rounds plan section 6.1 explicitly left "2D HUD sprites (owner question Q3)" out of 4c. Plan 9.3 (5-gsa1,
after 0.8.0) only plans a G-SA1-drawn ring counter (`feat/cl_hud.lua`).

Prerequisites that the running round **does** deliver:

1. **The alpha fix** (fx-gsa1-render, `gsa1_060_alpha.md` fixes 1-5: colour-only clears of the texture render targets).
   CON_REGULAR's HUD textures have alpha-0 texels. For example, `hyoji_head` at (40, 40) is (0, 0, 0, 0), measured on
   the decoded texture. On 0.6.0 and 0.7.0 they would draw as opaque black boxes. The HUD must ship after this fix.
2. **The camera / weapon HUD fix** (fx-gsa1-weapon-log, `gsa1_060_weapon.md` C3). The Camera SWEP hides `CHudGMod`,
   and with it every HUDPaint hook, including the HUD layer. The weapon fix holsters to empty hands, which removes that.
3. **E-102 lock-on onto host objects** (5-lib-game, H4, with the time bonus through the dest hook 8C0D32C0). Only this
   creates the 3D lock-on markers and the "+N" counter task in G-SA1 worlds.
4. **The common-archive pattern for OBJ_REGULAR** (fx-lib-shadow: loader order, `texdump_ref.py`, G-SA1's
   `T.NO_PREPARE`). CON_REGULAR follows the same pattern.

Also relevant: fx-lib-render's multi-pair capture (`gsa1_060_pops.md` F1 with refresh). The HUD's snapshot is the last
pair's, the same rule as the body's.

**What remains for a new HUD round:** sections 4 and 5, all of them.

---------------------------------------------------------------------------------------------------------------------

## 4. Design

### 4.1 libsa1

#### 4.1.1 The HUD pass (game code, the game's call site)

* **Hook.** Add a new world hook `sa1g_world.hud_fn`, called in `sa1g_world_pair` (`LIB/src/world/sa1_world_tick.c`)
  after the two steps and **before** `SA1G_FN_FRAME_ADVANCE` (`:459`). That is where 8C04E95A / 8C04EA4A call it.
  * It must come before the advance because `rt_cap_new` restarts the record buffer when `B_8C752BE8` changes
    (`LIB/src/rt/rt_draw.c:244-249`). Called after the advance, as `display_fn` is (`:462`), the HUD records would wipe
    the pair's model records.
  * The hook stays NULL in replays and in every test that compares references (part D: the pass writes 26 bytes of
    display state).
* **Installer.** `sa1_character.c` installs the hook when the character's HUD option is on. The character owns the
  capture, so the hook can note record ranges.
* **What the hook runs.** The tail of the game's frame step, split into parts with the game's own visibility flags:

  ```
  save e = B_8C752BDE, f = B_8C752BDF
  RINGS part: B_8C752BDE = -1 (B_8C752BDF = f)  -> 8C08A4BA()   // draws the label / ring icon and the ring count only
  LIVES part: B_8C752BDE = e, B_8C752BDF = -1   -> 8C08A4BA()   // draws the life icon and lives only; it then writes
                                                                 // BDE / BDF exactly as the game's single call does
  TIME part (only when 8C04C960() != 1)         -> 8C08A724()   // the timer (or the Sand Hill score)
  ```

  * A disabled part is simply skipped.
  * The ring blink phase advances only inside the RINGS call, once per frame as in the game.
  * The host notes each part's record range: the capture header's `count` before and after each call.
* **Tagging.** Records made during the executor (task displays: E-102's counters, Big's HUD, the radar) form the part
  CHARACTER. They are tagged by the running exec `D_8C1C1D40` (set by the executor `8C09878C`).
* **Big.** Big's HUD needs no special case: once 8C0EB160 and the functions it calls are real C, it draws during the
  executor like any task.

#### 4.1.2 Clock, E-102's limit, counters across respawns

All of these are new character options (`sa1_character_set_option`), applied inside the world at the next pair:

| Option | Values | Mechanism |
|---|---|---|
| `SA1_COPT_CLOCK` | 0 stopped (today's default), 1 running | the game's `8C08A5AE` / `8C08A5B6` |
| `SA1_COPT_E102_TIME_LIMIT` | 1 the game's rule (death at 00:00:00, measured in part E) | as the game |
| | 0 (G-SA1's default) | `B_8C7A2EAA = 0`, as the boss stages' code 8C082D60 / 8C084AE0 / 8C084BA0 does, **and** E-102's clock held stopped. The timer then shows 03:00:00 plus any bonus, and the last-seconds task never shows. Owner question H2 |

New creation-time values, so a respawn (G-SA1 re-creates the world: `cl_character.lua:244-256`) keeps the counters:

* `SA1_CREATE_START_LIVES` + `start_lives`: `B_8C752BE0`, clamped to 0..127.
* `SA1_CREATE_START_TIME` + `start_time[3]`: through `8C08AB20` after `8C08A580`.

New `sa1_state_ex` tail fields: `time_min`, `time_sec`, `time_frames`, `clock_running`.

#### 4.1.3 Capture: 2D priority, source and polygons

* **Sprite payload.** `rt_draw.c njDrawSprite2D` (`:619-624`) drops `pri` (`:621`). Its `SPRITE2D` payload grows from
  0x34 to 0x3C:
  * +0x34 `float pri`;
  * +0x38 `u32 exec` = `D_8C1C1D40` at the draw (0 outside the executor).

  The record layout (0xD0) and version 4 stay the same; only this kind's payload grows (the precedent is round 4c).
  `sa1_capture_payload(c, r, 0x34)` callers keep working.
* **Polygon kind.** A new kind `SA1_CAP_POLY2D = 13` comes from a real rt `njDrawPolygon2D` (`8C63EBC8`), replacing the
  void stub at `LIB/gen/policy/stubs.tsv:240`. Its payload:
  * u32 points, u32 has-colour, u32 has-uv;
  * the points (x, y floats), the NJS_COLORs, the NJS_TEX;
  * pri and exec.

  Its attribute bits are `0x20` (translucent) and `0x80000000` (textured, `pc->tex`); see `code_8C0E727A.c:84-89`.
* **Ordering (the DC's).** All HUD draws are translucent (attr 0x20). Ninja passes `pri` as the 2D depth: -1.5 is in
  front of -2.0.
  * The emitter sorts the 2D draws stably: opaque first, then translucent from the most negative `pri` (back) to the
    least (front).
  * This matters for Big's gauge: the frame is at -1.819 and the bar at -1.82, so the bar goes behind the frame's
  border. The frame's window is alpha 0 (measured), so the bar shows through it.
  * Whether SA1's PowerVR translucent list is auto-sorted or presorted is UNVERIFIED. Sorting by `pri` is the faithful
    choice under auto-sort.

#### 4.1.4 CON_REGULAR

* `LIB/src/sa1_loader.c:384` becomes `want[] = { "OBJ_REGULAR", "EFF_REGULAR", "CON_REGULAR" }`, the third entry of
  the stage start's common list `0x8C1C2450`. It adds 56 textures (318 KB RGBA) per character set; it could be shared
  across sets (UNVERIFIED whether the loader shares them).
* Hosts find the archive by name, as with OBJ_REGULAR.
* `tests/texdump_ref.py` "common" grows by CON_REGULAR.

#### 4.1.5 The output: `sa1_character_screen`

```c
#define SA1_API_SCREEN 1
#define SA1_HUD_RINGS     0x01u   /* the TIME label / ring icon and the ring count (8C08A4BA, rings part) */
#define SA1_HUD_LIVES     0x02u   /* the life icon and lives (8C08A4BA, lives part) */
#define SA1_HUD_TIME      0x04u   /* the timer (8C08A724; the score on Sand Hill) */
#define SA1_HUD_CHARACTER 0x08u   /* 2D draws of the world's tasks: E-102's counters, Big's HUD and fishing gauges,
                                     Knuckles' radar (once it runs) */
typedef struct sa1_screen_sprite {
    float    rect[4];      /* x0, y0, x1, y1 on the game's 640 x 480 screen (+y down), around `pivot`, before `angle`:
                              -cx*sx .. (w-cx)*sx (NJS_TEXANIM size and centre times the sprite's scale) */
    float    pivot[2];     /* sp->p.x, sp->p.y */
    float    angle;        /* radians, clockwise (NJD_SPRITE_ANGLE: Big's reel) */
    float    uv[4];        /* u0, v0, u1, v1 = NJS_TEXANIM / 256 (flips applied) */
    uint8_t  color[4];     /* RGBA: the constant material with NJD_SPRITE_COLOR, else white */
    float    priority;     /* njDrawSprite2D pri */
    uint32_t part;         /* SA1_HUD_* */
    uint32_t source;       /* guest exec address of the drawing task (0: the HUD pass) - for host anchoring rules */
} sa1_screen_sprite;
typedef struct sa1_screen_vertex { float x, y, u, v; uint8_t color[4]; } sa1_screen_vertex;   /* 2D polygons */
typedef struct sa1_screen_draw {
    uint32_t prim;         /* SA1_PRIM_SPRITES or SA1_PRIM_TRIANGLES (2D polygons as a triangle list) */
    uint32_t first, count;
    int32_t  archive, texture;  /* -1: untextured */
    uint32_t flags;        /* SA1_DRAW_BLEND / ADDITIVE / ALPHA_TEST / NO_TEXTURE, as sa1_draw */
    uint8_t  blend_src, blend_dst, pad[2];
    float    priority;
    uint32_t part;
} sa1_screen_draw;
typedef struct sa1_screen {
    uint32_t struct_size, generation;   /* generation: changes with every captured pair */
    float    width, height;             /* 640, 480 */
    uint32_t num_draws;  const sa1_screen_draw   *draws;    /* in drawing order (4.1.3) */
    uint32_t num_sprites; const sa1_screen_sprite *sprites;
    uint32_t num_vertices; const sa1_screen_vertex *vertices;
} sa1_screen;
SA1_API sa1_status sa1_character_screen(sa1_context *ctx, sa1_character_id id, const sa1_screen **out);
#define SA1_COPT_HUD_PARTS  ...   /* mask of SA1_HUD_*; 0 (default) = no HUD pass, no 2D output */
```

* **Built from the snapshot.** Emission reuses `emit_sprite`'s resolution, colour and blend rules
  (`LIB/src/sa1_capture.c:585-712`, `ninja_blend` :554), in screen space.
* **Hidden pairs.** The output is taken from the snapshot also when the snapshot is HIDDEN (the character is not seen
  by the game's camera): the HUD does not depend on visibility.
* **Not interpolated.** The game draws the HUD once per 30 Hz frame. The "+N" counter moves in 14-px steps, as on the
  console.
* **No change to `sa1_render`.** It keeps leaving 2D records out.

### 4.2 G-SA1: the `hud` feature

#### 4.2.1 Files

All of these are new, through the feature seam: `src/module/bind_hud.cpp`, `addon/lua/gsa1/feat/sh_hud.lua`,
`feat/cl_hud.lua`, `tests/hud_test.lua`, `tests/feat_hud_test.cpp`, `tests/feat_hud_binding_test.cpp`.

* `gsa1.Screen()` returns flat arrays (no tables on the hot path), like `gsa1.Batches`.
* Only the local player's own character draws a HUD. Followers (6b) and other players' characters never do.

#### 4.2.2 The layer (HUDPaint)

* **One pass per frame** over `draws` in their order, inside the 2D context HUDPaint already provides. Each draw is
  `render.SetMaterial(material)` then `mesh.Begin(MATERIAL_QUADS / MATERIAL_TRIANGLES)` with screen coordinates. The
  mesh path keeps per-vertex colour (the tension bar), rotation (the reel) and the game's UVs, which `surface.*`
  cannot all do.
* **Materials.** `T.Material` with the draw's blend: unlit, vertex colour and alpha, no depth.
* **Textures.** Through `T.Get` on first use. CON_REGULAR joins `T.NO_PREPARE` (`cl_textures.lua:83`), so a spawn
  does not paint its 56 textures.
* **Filter.** Bilinear by default; an option offers sharp. The Dreamcast's filter for these sprites is UNVERIFIED.
* **When the layer is hidden:**
  * no character, or `gsa1_hud 0`;
  * the player looks through a view entity (spawned cameras, `gsa1_rounds_plan.md` 14.1e);
  * `cl_drawhud 0`.
* **GMod's own HUD.** Option, on by default: hide `CHudHealth` / `CHudBattery` while the SA1 HUD shows. The rings are
  the health (round 5 decision 1). This joins the `hidden_hud` table (`cl_character.lua:503-506`).

#### 4.2.3 Layout, the owner plays at 3440 x 1440

* **Scale.** `s = (H / 480) * gsa1_hud_scale` (default 1.0, giving 3.0 at 1440 p).
* **Elements.** An element is the records of one part, or of one `source` among the CHARACTER records.
* **Anchoring.** For an element with bounding-box centre `xc` (in 640 virtual px):

  ```
  extra = W - 640 * s              (1520 px at 3440 x 1440)
  t     = clamp((xc - 160) / 320, 0, 1)     ("edges", default)   or   t = 0.5   ("4:3 box")
  X     = x * s + extra * t + safe * (1 - 2 t) * W        Y = y * s + safe_y
  ```

  * Left-quarter elements stay at the left edge, centred ones in the middle, right-quarter ones at the right edge.
  * Moving elements such as E-102's "+N" counter, which flies from (540, 120) towards the timer, move continuously.
  * Measured in `compose.py`: offsets 0 for rings, lives and time; 760 for all of them in box mode.
* **Safe area.** `gsa1_hud_safe` (%, default 0): the game's own 16-px inset is already 2.5 % of 640.
* **Narrower than 4:3** (extra < 0, e.g. 5:4): scale by `W / 640` instead.

#### 4.2.4 Q menu > Options > G-SA1 > HUD (owner rule: every option gets a control)

| ConVar | Default | Control |
|---|---|---|
| `gsa1_hud` | 1 | checkbox "Show the Sonic Adventure HUD" |
| `gsa1_hud_rings` / `_lives` / `_time` / `_character` | 1 | four checkboxes (parts mask) |
| `gsa1_hud_scale` | 1.0 | slider 0.5-3 |
| `gsa1_hud_safe` | 0 | slider 0-10 % |
| `gsa1_hud_layout` | edges | combo "Corners of the screen" / "Centred 4:3 box" |
| `gsa1_hud_filter` | smooth | combo smooth / sharp |
| `gsa1_hud_hide_gmod` | 1 | checkbox "Hide Garry's Mod's health and suit display" |
| `gsa1_clock` | 1 | checkbox "Game clock runs" |
| `gsa1_e102_time_limit` | 0 | checkbox "E-102's 3-minute time limit (dies at 0:00)" |
| `gsa1_hud_keep_counters` | 1 | checkbox "Keep lives and time after a respawn" (a death costs a life; at 0, back to 4: owner question H3) |

The session log gets a line: HUD on, parts, scale, layout, screen size and the draw count of the first frame.

### 4.3 SADC decompilation

| Need | Functions | Notes |
|---|---|---|
| Big's HUD (required) | `8C0EB160` (exec, 0x24), `8C0E6FE0` (0x104), `8C0E70E4` (display, 0x196), `8C0E74FC` (weight, ring and life numbers, 0x1A4), `8C0E746C` (line length, 0x90): all in chunk `asm/1st_read/text/8C0E6620.src` / `8C0EB160.src`. The HUD number draw `8C08EE98` (0x230, chunk `8C08E1E0.src`; the Ghidra output is unreadable FPU-pair code, so the task needs time) | All are void stubs or asm today. Their stubs.tsv lines (137, 156, 158) go away. big_ec3 runs 8C0EB160 every frame ("904 calls"), so the strict replay of big_ec3 is the regression gate |
| 2D polygon | none: `njDrawPolygon2D` 8C63EBC8 is SDK, reimplemented in LIB rt (4.1.3) | |
| Knuckles' radar (deferred to the 1.0 collectables) | `8C0C99E4`, `8C0C99E0`, `8C0C967A`, `8C0C9626`, `8C0C94AC`, `8C0C9420`, `8C0C9294` (chunks `8C0C9420.src`, `8C0C9AE0.src`), and the shard placement `8C0C905A` | With emerald shards as host objects, the radar's 2D draws arrive through the same path |
| none (already C) | `8C08A4BA`, `8C08A2C2`, `8C08A724`, `8C08A7E0`, `8C089F20`, `8C08A5CC`, `8C08A580`, `8C0A11BE`, `8C0A1120`, `8C0D4E40`, `8C0D4D08`, `8C0D50AC`, `8C0D60C0`, `8C0D6198`, `8C0E727A`, `8C0E72DA`, `8C0CAC28`, `8C0817E0` | The NON_MATCHING ones (8C08A4BA, 8C08A2C2, 8C08A724, 8C0A1120 / 11BE) never ran in any test: they are only reached from the frame wrapper. They need the oracle of 5.2 F6 |

---------------------------------------------------------------------------------------------------------------------

## 5. Fixes, tests and owners

Every fix belongs to a new **HUD round**. It is scheduled after the running fix round (0.8.0), alongside or right after
5-gsa1 (0.9.0), with per-agent waits:

* hud-decomp waits for fx-decomp (the only SADC/src editor now);
* hud-lib waits for fx-lib-render and fx-lib-shadow (they own `sa1_capture.c`, `sa1_character.c` and `sa1_loader.c`
  at the moment);
* hud-oracle waits for 5-oracle (it owns SADC/harness);
* hud-gsa1 waits for fx-gsa1-render and fx-gsa1-weapon-log (prerequisites 1 and 2 of section 3).

**Coordination with plan 9.3:** 5-gsa1 must **not** write its own G-SA1-drawn ring counter in `feat/cl_hud.lua`. That
file belongs to hud-gsa1, and the game's own HUD replaces it. 5-gsa1 keeps the rest of its scope.

| # | Fix | Files | Test: fails before, passes after | Owner |
|---|---|---|---|---|
| F1 | Decompile Big's HUD: 8C0EB160, 8C0E6FE0, 8C0E70E4, 8C0E74FC, 8C0E746C and the number draw 8C08EE98. Remove their void lines from stubs.tsv | `SADC/src/1st_read/` (the chunk files of 8C0E6620, 8C0EB160 and 8C08E1E0), `LIB/gen/policy/stubs.tsv` | `harness diff <fn> --warm` for each. SADC `configure.py --split` + ninja all OK. LIB `build.py --relower --strict-stubs --test` (the big_ec3 strict replay; today the exec is a void stub, so a wrong real exec breaks the task pool and the replay). `hud_test` case 5 (below) shows Big's WEIGHT / rings / lives: today 0 sprites (part C) | `hud-decomp` (SADC decompilation agent) |
| F2 | rt: `njDrawSprite2D` keeps `pri` and the running exec in the SPRITE2D payload (0x3C); a real `njDrawPolygon2D` 8C63EBC8 recording `SA1_CAP_POLY2D` (13). Drop the 8C63EBC8 stub | `LIB/src/rt/rt_draw.c`, `LIB/src/sa1_capture.h` (kinds, payload doc), `LIB/gen/policy/stubs.tsv:240` | `capture_test` new part "2d": Big's cast (part B setting) gives reel + frame + a POLY2D of 4 points with colours 0xFF00FFFF-based. Today no POLY2D record exists and pri is lost. The 10 strict references are unchanged (draw-only) | `hud-lib` |
| F3 | The HUD pass hook (`hud_fn` before 8C08A5BE) with the part split of 4.1.1; record ranges per part; off in replays | `LIB/src/world/sa1_world_tick.c`, `sa1_world.h`, `LIB/src/sa1_character.c` (small block) | New `LIB/tests/hud_test.c` + `build_hud.py`. Case 1: Sonic with all parts gives 15 sprites per pair at the positions of 2C (today 0, part A). Case 2: parts masked (rings off gives 11 sprites; time off gives 7). Case 3: RAM neutrality as in part D, where only the 4 display runs may differ and positions stay bitwise equal. Case 4: the hook is never installed in `build.py --test` replays (strict replays unchanged) | `hud-lib` |
| F4 | Options and counters: `SA1_COPT_CLOCK`, `SA1_COPT_E102_TIME_LIMIT`, `SA1_CREATE_START_LIVES` / `START_TIME`, `sa1_state_ex` time fields | `LIB/include/libsa1.h` (new "screen / HUD (API 0.4)" section), `LIB/src/sa1_character.c`, `LIB/src/world/sa1_world_boot.c` | `hud_test` case 6: E-102 with the clock and limit on dies at 00:00:00 (part E: today it needs the probe's own clock call). With limit 0: no death in 400 pairs, timer at 03:00:00. Case 7: `start_lives` 2 / `start_time` 1:23:45 appear in `sa1_state_ex` and in the HUD digits | `hud-lib` |
| F5 | Load CON_REGULAR as a common archive | `LIB/src/sa1_loader.c:384`, `LIB/tests/texdump_ref.py` (common list), `texdump.c` expectations | `hud_test` case 8: every HUD-pass sprite resolves to (CON_REGULAR, cell); `D_8C1BF19C` cells resolve to 24 + cell. Today: archive -1 (part C). `texdump_ref.py` PASS on 56 more textures | `hud-lib` |
| F6 | Output API `sa1_character_screen` (4.1.5): sorting, colour, blend, parts, sources; HIDDEN pairs included | `LIB/include/libsa1.h`, `LIB/src/sa1_capture.c` (a screen emitter next to `emit_sprite`), `LIB/src/sa1_character.c` | `hud_test` case 9: draws ordered by pri (Big's bar before the gauge frame). Case 10: colours (zero-ring blink alpha 1, red 1, g = b in 0..1), the E-102 timer red under 15 s. Case 11: `sa1_render` unchanged (draw count equal with the HUD on and off) | `hud-lib` |
| F7 | Oracle: the HUD functions' 2D draws against the original. In the harness, the interpreted 8C08A4BA / 8C08A724 / 8C0E70E4 / 8C08EE98 log `njDrawSprite2D` arguments; LIB logs the native ones. States: rings 0/7/123/999, lives 0/4/127, times 0:00:00 / 0:00:29 (E-102) / 2:59:59 / 100:00:00, players 0/2/3/5/6/7, levels 1/26/35/38. Also correct the code map row (1.5) | `SADC/harness/**` (after 5-oracle), `SADC/docs/research/sa1_code_map.csv` (row 0x8C089F20) | A new `harness/tests/hud_draws` passes. Today: no comparison exists, and the NON_MATCHING HUD C has never run in a test | `hud-oracle` |
| F8 | G-SA1 feature `hud` (4.2): binding, layer, layout, materials, Q menu page, GMod HUD hiding, counters across respawns, session log line, README checklist | `g-sa1/src/module/bind_hud.cpp`, `addon/lua/gsa1/feat/sh_hud.lua`, `feat/cl_hud.lua`, `cl_textures.lua` (patch: `T.NO_PREPARE.CON_REGULAR`), `cl_character.lua` (patch: `hidden_hud`, respawn counters), tests | `tests/hud_test.lua` (GMod's LuaJIT with the mock). It checks the layout maths: 640 x 480 is the identity; 3440 x 1440 edges gives offsets 0 / 1520 / 760 for left / right / centre elements; box gives 760; the scale and safe-area options. It also checks: hidden with no character, while a view entity is not the player and with `cl_drawhud 0`; parts mask passed to `SA1_COPT_HUD_PARTS`. `feat_hud_binding_test.cpp` (shapes, argument checks). Today: no `feat/cl_hud.lua`, so the test fails to load. In game: the README checklist (rings, lives, time at top left / bottom left on 3440 x 1440; E-102's countdown; Big's WEIGHT and the reel / gauge while fishing) | `hud-gsa1` |
| F9 | Independent verification and release | - | All of the above, from clean | `hud-verify`, then the orchestrator's release step |

---------------------------------------------------------------------------------------------------------------------

## 6. Owner questions (each has a default; the HUD round proceeds on the defaults)

* **H1. Style.** The action-stage HUD (always shown, with the timer) or the adventure-field style (no timer; rings and
  lives appear for 3 s when they change)? Default: action stage. G-SA1 worlds run level 1. The adventure style needs
  the world's level type to be 1, which also stops the clock (game rule).
* **H2. E-102's 3-minute limit.** On means the game's rule: he dies at 0:00, and lock-on kills add time once round 5
  lands. Off means no death and his timer held at 3:00. Default: off.
* **H3. Lives across respawns.** Default: a death costs a life; at 0 the counter goes back to 4 (no game-over screen).
  The time and rings follow the game: the time keeps running (UNVERIFIED: the console restores a checkpoint's time),
  and the rings go to 0.
* **H4. Wide screens.** HUD in the screen's corners (default) or in a centred 4:3 box?
* **H5. Knuckles' radar.** It needs emerald shards in the world. Should it come with the 1.0 collectables (default)?

---------------------------------------------------------------------------------------------------------------------

## 7. UNVERIFIED points

* Whether SA1's translucent 2D list is auto-sorted by depth or drawn in submission order. The design sorts by `pri`.
* The Dreamcast's texture filter for HUD sprites. The design defaults to bilinear and offers sharp.
* `8C605480`, a Kamui draw-state call that is a void stub: it is called with 0 before HUD draws and 1 after. Its
  meaning (fog / z-compare?) does not matter on the host.
* `B_8C78B451`, which selects score versus timer on Sand Hill.
* The cell-to-texture mapping of Big's HUD:
  * cell 4 = FISHING 61 `bighyouji` and cell 5 = 62 `hyouji_g`: read from `D_8C566044` / `D_8C56606C` and Ghidra's
    8C0E6FE0;
  * the life icon is CON_REGULAR 19 `hyoji_zankib` through `D_8C5660F8` / `D_8C566100`.

  These are to be confirmed by F1's oracle.
* The radar's exact layout and draw calls, since 8C0C9420 / 8C0C967A / 8C0C94AC are asm only.
* That the HUD pass's cost is negligible in game. Offline, 200 pairs x 3 characters including disc loading ran in under
  1 s; it has not been measured in GMod.
* Whether a checkpoint restores the time on the console (H3).

---------------------------------------------------------------------------------------------------------------------

## 8. Reproduction

```
cd SADC/build/research/hud
py -3.12 lockcopy.py                                   # snapshot LIB's libsa1.a + headers under build/.build.lock
py -3.12 -m ziglang cc -target x86_64-windows-gnu -std=gnu11 -Isnap/include -Isnap/src -Isnap/src/world -I../../../include ^
    -Wall -O2 -g -ffp-contract=off -frounding-math -fno-builtin -DSA1_STATIC hud_probe.c snap/libsa1.a -o hud_probe.exe
hud_probe.exe "<GDI>" ABCDE                            # parts A-E of section 2 (each ~0.2-1 s)
py -3.12 ../../../../libsa1/tests/texdump_ref.py --gdi "<GDI>" --dump "" --archives CON_REGULAR,E102TIME,FISHING,KNU_EFF --png tex
py -3.12 compose.py out_c.txt                          # hud_<char>_640.png, _3440.png, _3440_box.png
```

The decoded textures and images stay in that folder. They are game bytes from the owner's disc, for research only,
and are never shipped.

---------------------------------------------------------------------------------------------------------------------

## Review (adversarial)

Independent review, 2026-10-21, read-only: nothing under SADC/src, include, tools, harness, config, LIB or g-sa1 was
changed. Scratch: `SADC/build/research/hud/review/` (see its `README.txt`). `snap/` there is LIB's `libsa1.a` as
rebuilt at 14:31, after fx-lib-shadow's OBJ_REGULAR loading landed. It was copied under LIB's build lock. Every
reproduction used the owner's GDI.

### R.1 What was re-run

* **The doc's probe.**
  * `hud_probe.exe`, unchanged, parts A-E: byte-identical to `out_a.txt` .. `out_e.txt` (line ends aside).
  * `hud_probe.c` rebuilt against the 14:31 LIB: identical again. Nothing the running round has landed so far changes
    any HUD-relevant behaviour.
* **A new probe, `review/hud_review.c`** (output `out_review.txt`):

  | Part | What it does |
  |---|---|
  | S | The F3 split exactly as the 4.1.1 pseudo-code. Masks 7 / 6 / 5 / 3 / 4, Sonic and Big, 300 pairs with part D's inputs. Guest RAM compared with the game's single call |
  | G | The same with the guard fix of R.5.1 |
  | V | The HUD pass run after the frame advance instead of before it |
  | K | E-102 with the clock on and no HUD pass |
  | L | K plus `B_8C7A2EAA = 0` written once |
  | Z | E-102 with the clock on and the time 00:00:00 from the first pair (a respawn that carried a zero countdown) |
  | T | Big's HUD tables and `D_8C1BF5B4`, read from guest memory |

* **`review/arcs.c`:** the texture archives of every character in the current LIB (`out_arcs.txt`).
* **CON_REGULAR from the disc**, read with `texdump_ref.py`'s own reader:
  * 27,268 bytes of PRS, 56 textures;
  * data formats 1, 2 and 9 only, all three in `sa1_pvr.c`'s supported list (`:227`);
  * 318,464 bytes as RGBA;
  * `hyoji_head` (40, 40) = (0, 0, 0, 0); 3,250 of its 4,096 texels have alpha 0.

### R.2 Verified

**The causes.**

1. **No call site.**
   * `sa1g_world_pair` (`sa1_world_tick.c:433-475`) runs the two steps, then the advance (`:459`, only when
     `w->step == sa1g_step_game && !w->replay`), then `display_fn` (`:462`). Nothing calls 8C08A4BA or 8C08A724.
   * In the game, Ghidra `8C04E95A.c` ends with an unconditional `FUN_8c08a4ba(); FUN_8c08a724();`. `8C053B60.c:500-501`
     then calls 8C0439AE and 8C08A5BE.
   * Part A reproduced: 0 SPRITE2D for all six characters.
2. **The clock is stopped.**
   * Nothing in LIB passes `SA1G_BOOT_CLOCK` (`sa1_world.h:671`; grep of src, tests and tools), so every world boots
     through 8C08A5B6.
   * Part K shows that the clock alone kills E-102, with no HUD pass: SA1_EVENT_DEATH at pair 90, mode 49 action 69,
     then mode 51.
3. **CON_REGULAR is not loaded.**
   * `sa1_loader.c:384` is still `{ "OBJ_REGULAR", "EFF_REGULAR" }`. `texdump_ref.py:332` still skips CON_REGULAR.
   * Textures resolve by NJS_TEXNAME address (`sa1_capture.c:203-227`). Part T read the sub-texlists from guest memory:
     `D_8C1BF1A4` = { 8C1C3070, 24 } and `D_8C1BF19C` = { 8C1C3190, 11 }. Both resolve once CON_REGULAR is an archive.
4. **2D draws are not emitted.**
   * `rt_draw.c:619-624` drops `pri` at `:621`.
   * The emitter filters kinds at `sa1_capture.c:1070-1072`.
   * 8C63EBC8 is void (`stubs.tsv:240`).
   * `sa1_capture_payload` (`sa1_capture.c:365-370`) refuses a record only when `pay_len < n`. Readers that ask for 0x34
     bytes therefore keep working with a 0x3C payload.
5. **Big.**
   * Both HUD functions test `GetPlayerNumber() != 7`: `code_8C08A4BA.c:26` and `code_8C08A71E.c:58`.
   * 8C0EB160 is void (`stubs.tsv:158`). Ghidra shows what it does: its first call sets `tp->disp = 8C0E70E4`, and every
     later call runs 8C0E6FE0.
   * 8C08EE98 is void (`:137`). It is 2D only: its single draw call is 8C63EEB4, with `D_8C1BF4E0`. Decompiling it
     cannot add draws to `sa1_render`.
6. **The code-map error.** 8C08ECB8 dispatches by `GetStageNumber` to 8C08CA40 / 8C08E380 / 8C08E93A / 8C08DC32 /
   8C08C100 / 8C08D1C0. It is created at four sites in `8C04EFE0.c` and at `8C04F20A.c:57`.

**The ordering argument (4.1.1), now measured (part V, Sonic):**

| HUD pass | Sprites in the snapshot | Model records | `sa1_render` draws |
|---|---|---|---|
| before the advance | 15 | 49 | 49 (the same as with no HUD) |
| after the advance | 15 | 0 | 0 |

**Parts C, D and E reproduced**, and the counts of F3 case 2 hold. Sonic, sprites per pair: all parts 15, rings off 11,
lives off 12, time off 7, time only 8. For Sonic the doc's split leaves guest RAM bit-identical to the game's single
call (0 bytes).

**Points of section 7 now settled (part T):**

* **Big's cells.** `D_8C566044[4]` = FISHING 61 (64 x 64, centre (32, 32)); `[5]` = FISHING 62 (16 x 16, centre (8, 8)).
* **Big's life icon** is CON_REGULAR 19 `hyoji_zankib`: `D_8C5660F8` = { 8C1C3070, 24 } (CON_REGULAR's first 24 names)
  and `D_8C566100[0]` texid 19. **Big's HUD therefore needs F5 too.**
* **The adventure-field ring icon `D_8C1BF5B4`:** at (16, 49), CON_REGULAR, 64 x 47, uv (0, 68)-(255, 255), texid 11.
* **"Whether the loader shares CON_REGULAR" (4.1.4): it does not.**
  * `load_common_textures` (`sa1_loader.c:382-395`) runs once per character set (`:476-477`).
  * `load_archive` decodes into that set's own arena (`:182-249`).
  * Every loaded character carries its own copy. OBJ_REGULAR and EFF_REGULAR are already duplicated this way, 9 times.

**"Covered by the running round: No" holds.**

* No agent of `gsa1_round_fix060.js` mentions the HUD, 2D emission or CON_REGULAR.
* A grep of LIB and g-sa1 finds no `sa1_character_screen`, `SA1_CAP_POLY2D`, `hud_fn`, `SA1_COPT_CLOCK` or
  `feat/*hud*`.

Status of the four prerequisites:

* **1-3 are in the round's task text.** 1 is fx-gsa1-render's task 1. 2 is fx-gsa1-weapon-log's "camera HUD". 3 is
  5-lib-game's H4: `sa1_world_hostobj.c:773-781` already pays the bonus through the game's 8C0D5064.
* **4 has already landed.** `sa1_loader.c` (12:22) loads OBJ_REGULAR, and `cl_textures.lua:83` has
  `T.NO_PREPARE = { OBJ_REGULAR = true }`.

### R.3 Refuted or corrected

1. **The F3 split is not RAM-neutral when the wrapper's guard is false.**
   * **The mechanism.** The pseudo-code forces BDE = -1 or BDF = -1 before each 8C08A4BA call. 8C08A4BA writes the flags
     back only when its guard holds (`code_8C08A4BA.c:26-33`): BDD >= 0, `8C0A1218() == 0` and
     `GetPlayerNumber() != 7`. When the guard is false, the forced value stays.
   * **Measured (part S, Big, 300 pairs).**
     * BDF ends every frame at -1; the game leaves 0. Guest RAM differs at 8C752BDF.
     * With LIVES off, BDE stays at -1.
   * **Where else it happens.** For every character while an event runs (BDD = -1, `sa1_world_tick.c:141-150`), and in
     Twinkle Circuit.
   * **What it breaks.** The doc's "it then writes BDE / BDF exactly as the game's single call does" is false in these
     cases, and F3 case 3 would fail for Big.
   * **The fix is R.5.1**, measured in part G: 0 differing bytes against the game's single call for Sonic and Big, with
     the same sprite counts.
   * **One more reason the forced values must not leak:** 8C08A724 also tests BDF >= 0 (`code_8C08A71E.c:58`).
2. **The attribute bits of njDrawPolygon2D (4.1.3) are wrong.**
   * **The doc** says 0x20 is "translucent" and cites `code_8C0E727A.c:84-89` for 0x80000000. Those lines pass only
     0x20.
   * **Ghidra `8C63EBC8.c`:**
     * 0x80000000 selects the textured routines;
     * 0x40 is passed on as the translucency flag;
     * 0x20 selects fill rather than outline.

     LIB's notes on the 3D siblings say the same (`rt_draw.c:639-650`).
   * **So Big's tension bar (attr 0x20) is an opaque, filled gouraud quad.** "All HUD draws are translucent" is wrong for
     it.
   * **The drawn result does not change.** Opaque draws go first, so the bar still ends up behind the gauge frame. But
     F2 must record `attr`, and F6 must honour fill / outline, translucency and texturing.
3. **Big's HUD draws from his task's exec, not its display.**
   * **The draw path.** The in-play draws come from 8C0EB160 -> 8C0E6FE0, on sub step 0 only (8C0E700A-8C0E700C).
   * **8C0E70E4 is the task's `disp`.** Only the display pass 8C0988C4 (pause) runs it, and LIB never runs that pass:
     nothing sets `w->display`. It needs a symbol only because 8C0EB160 stores its address.
   * **Correction for F7:** the oracle should list 8C0EB160 and 8C0E6FE0, not 8C0E70E4.
   * **Two details for hud-decomp:**
     * 8C0E6FE0 itself rewrites BDE = BDF = BDD every sub step 0 (-1 in adventure fields; 8C0E70BC-8C0E70D4).
     * It draws WEIGHT and the ring when **BDF <= 0** (`CMP/PL`, then `BT` to skip, 8C0E7040-8C0E7042). The test looks
       inverted but is the game's, and it must stay.
4. **"fx-lib-render's multi-pair capture also fixes which pair's snapshot is shown" (section 3): no.**
   * `rt_cap_new` (`rt_draw.c:245-250`) already restarts the buffer at every new frame-counter value. The snapshot
     therefore always holds the last pair, with or without `gsa1_060_pops.md` F1.
   * F1 changes the render's interpolation end points, and the HUD is not interpolated.
   * The claim is harmless but is not a prerequisite.
5. **E-102's "limit off" (4.1.2, F4): a one-time `B_8C7A2EAA = 0` is undone by the game.**
   * **Who writes the flag.**
     * It is set to 1 when a time bonus finishes (8C0D4C30, `code_8C0D4B00.c:86`) and by the bonus task's destructor
       (8C0D51FC).
     * It is cleared while the bonus counts (`code_8C0D4E40.c:87`).
   * **Why that matters now.** 5-lib-game pays lock-on kills through 8C0D5064 (`sa1_world_hostobj.c:115, 773-781`). The
     first kill therefore re-arms the limit.
   * **What prevents the death is the doc's other half, the clock held stopped.** The hold must be enforced every pair,
     because game code can restart the clock: 8C06132A (common stage objects, chunk 8C060D40; which object is
     UNVERIFIED) calls 8C08A5B6 and later 8C08A5AE.
   * **Measured (part L):** with the clock running and only `B_8C7A2EAA = 0`, there is no death. But the timer sits at
     00:00:00 and the last-seconds sprites are drawn forever (2 SPRITE2D at pair 179).
   * **A side effect to document.** While the clock is held, 8C08A5CC does not count down the score-chain window
     `B_8C7997A0` (`code_8C08A4BA.c:123-125`; 8C08A1F0 resets it on landing). Today's stopped clock does the same for
     every character.
6. **Smaller corrections.**
   * **Summary:** 8C08A2C2 is itself NON_MATCHING (`code_8C08A198.c:128-129`). Only 8C08A7E0 and 8C089F20 are
     matching.
   * **1.2, E-102's timer colour:** under 30 s the blue channel pulses, so the digits go between white and yellow, not
     "blue pulses".
   * **1.2, SE 0x30B:** it plays only while 0 < t < 10 s. It sounds every 2 s above 5 s and every 1 s from 5 s down
     (`code_8C08A4BA.c:99-110`).
   * **1.3, the tension bar's colour** is a vertical gradient, not "green to red". With c = tension x 255, the base is
     (r c, g 255, b 255 - c) and the top is (r c, g 0, b 255 - c). It is cyan to blue when the line is slack and yellow
     to red at full tension.
   * **1.5 / F7:** the wrong "HUD master" label is also in `sa1_code_map.md:302`, not only in the .csv row.
   * **Moved citations.** fx-gsa1-weapon-log edited `cl_character.lua` at 14:41. `hidden_hud` is now at `:506`, the
     debug HUDPaint at `:536`, `GSA1.Respawn` at `:244` and `GSA1.Recreate` at `:261`.

### R.4 Gaps

1. **Archive capacity.**
   * E-102 already has 7 archives (`out_arcs.txt`): E102, E102BEAM, E102EFFECT, E102TIME, E102HIT, OBJ_REGULAR and
     EFF_REGULAR. `SA1_MAX_ARCHIVES` is 8 (`sa1_internal.h:41`).
   * CON_REGULAR makes it 8 of 8. `load_archive` refuses a ninth archive with a warning and marks the load partial
     (`sa1_loader.c:186-190`).
   * Any later common archive then fails for E-102, for example the 1.0 objects' stage archives, EXTRA, HINT or
     EME_KIRAN. F5 does not mention this.
2. **Respawn rules.**
   * **A death loop (measured, part Z).** E-102 starting at 00:00:00 with the limit on dies at pair 29 (SA1_EVENT_DEATH,
     mode 49). With "keep counters" on, a time-up death would re-create him at 00:00:00, and he would then die again
     every second.
     * The respawn must use the game's restart time instead: 03:00:00 from 8C08A580, or the checkpoint's time.
     * A lead for H3: 8C064EF6 (common objects, chunk 8C064E80) calls 8C08AB20 with `B_8C78C590` / `B_8C78C598` and
       `GetStageNumber`. It is probably the checkpoint restore (UNVERIFIED).
   * **Re-creation without a death.** `GSA1.Recreate` (an upgrade change, `cl_character.lua:261`; also 6a's Super Sonic
     revert) re-creates the world without a death. The counters must carry over unchanged there, with no life lost. The
     doc names only respawns.
   * **F4 case 6 does not test the hold.** It runs 400 pairs from 03:00:00, about 13 s against a 180 s limit, so "no
     death" proves nothing. Only "the timer stays at 03:00:00" checks the hold.
3. **The clock function is not verified.**
   * F4 turns on the running branches of 8C08A5CC. They are NON_MATCHING (`code_8C08A4BA.c:81-82`) and never ran in a
     reference, because the references keep `D_8C1BF198 = 0`.
   * F7 must cover them: the count-up stop at 99:59:59, E-102's countdown, the SE 0x30B timing through `B_8C7997B0`,
     and the `B_8C7997A0` decrement.
4. **The "TIME" label belongs to the RINGS part.**
   * `hyoji_head` (cell 11) holds both the TIME label and the ring icon, and the RINGS call draws it.
   * With TIME off, a lone "TIME" label stays on screen. With RINGS off and TIME on, the digits have no label.
   * The game has its own ring-only cell for this, `D_8C1BF5B4` (part T): the same texture with v 68..255, at (16, 49).
     That is exactly the lower part of the sprite drawn at (16, 32).
   * F6 / F8 should draw the ring icon with that geometry when TIME is off. This is display only.
5. **Plan dependencies that assume 5-gsa1's ring HUD.**
   * 6a-gsa1 waits for "5-gsa1 (the ring HUD)": plan 11.3 and the table at `:259`.
   * 6a's owner checklist has "the ring drain on the HUD" (11.4).
   * Plan 9.3's "Interaction" page lists "HUD".
   * `gsa1_060_weapon.md` C3 names "round 5's ring HUD (cl_hud.lua)".

   When 5-gsa1 drops `cl_hud.lua`, these must point at hud-gsa1, or 6a's ring-drain check waits for the HUD round.
6. **The harness capture.** F2 changes only the native rt path. In the harness profile, `RT_CAP_SPRITE` is
   `RT_DRAW_CAPTURE(kind, sp, n, attr)`, without `pri` (`rt_draw.c:610-614`). F7's logger must take `njDrawSprite2D`'s
   four arguments itself.
7. **`empty_windows` changes meaning.**
   * With the HUD pass on, a pair in which the character drew nothing is no longer empty: it holds the HUD records.
   * `sa1_render` is unaffected, because the hidden test uses the visibility counts (`sa1_character.c:1071`).
   * But any log or test that reads `sa1_capture_stats.empty_windows` changes meaning. F6 should say so.

### R.5 Required changes

1. **F3 (hud-lib): decide the wrapper's guard first.**
   * Evaluate it with the game's own functions:
     `guard = BDD >= 0 && 8C0A1218() == 0 && GetPlayerNumber() (8C04F698) != 7`.
   * Guard false: make no 8C08A4BA call. The game's call would do nothing either.
   * Guard true, with RINGS and LIVES both off: make one call with BDE = BDF = -1. It draws nothing and writes the flags
     as the game does.
   * Guard true otherwise: the doc's split.
   * Part G measured this: 0 differing bytes against the game's single call for Sonic and Big. Sonic's counts are
     unchanged (15 / 11 / 12 / 7 / 8).
   * F3 case 3 must also run for Big and with a forced event (BDD = -1). The 4.1.1 pseudo-code fails both (1 byte at
     8C752BDF).
2. **F4 (hud-lib): hold the clock every pair, and fix the respawn rules.**
   * Implement "E-102 limit off" as a hold enforced every pair: `D_8C1BF198 = 0` written before each pair of an E-102
     world with the option off. A one-time write is not enough. `B_8C7A2EAA = 0` is optional.
   * Rewrite case 6:
     * start from `START_TIME` 00:02:00: with the limit off, no death; with it on, death within about 3 s;
     * run a lock-on kill (5-lib-game H4) and a game-code call of 8C08A5AE in the middle of the run.
   * New case: E-102 with the limit on and "keep counters" on. After a time-up death the re-created world starts at
     03:00:00 with 3 lives (no death loop, part Z).
   * New case: `GSA1.Recreate`-style re-creation keeps the counters unchanged.
3. **F2 / F6 (hud-lib): keep and honour the polygon attribute bits.**
   * POLY2D records keep `attr`. Document the bits from 8C63EBC8: 0x20 fill, 0x40 translucent, 0x80000000 textured.
   * F6 emits a filled polygon as triangles and an outline as lines (or skips outlines and counts them). It takes the
     blend from bit 0x40.
   * Correct 4.1.3's "0x20 (translucent)" and "All HUD draws are translucent".
4. **F5 (hud-lib): make room for the archive.**
   * Raise `SA1_MAX_ARCHIVES` (for example to 12), or share the common archives across the context.
   * `hud_test` case 8 asserts that no character of the 9 logs "more than N texture archives", and that all of E-102's
     own archives still load.
5. **F7 (hud-oracle): widen the oracle.**
   * Add the running branches of 8C08A5CC.
   * Name 8C0EB160 and 8C0E6FE0 instead of 8C0E70E4.
   * Also correct `sa1_code_map.md:302`.
6. **F1 (hud-decomp): two notes.**
   * In LIB worlds 8C0E70E4 is only a stored `disp` pointer. A stub symbol would satisfy `--strict-stubs`, but
     decompiling it is fine.
   * Keep 8C0E6FE0's BDF <= 0 test.
7. **F8 and the plan (hud-gsa1 and the orchestrator).**
   * Use the ring-only geometry when TIME is off (R.4.4).
   * Carry the counters across `GSA1.Recreate`.
   * Retarget plan 11.3, the table at `:259`, 11.4, 9.3 and the note in the weapon doc (R.4.5).
8. **Section 3: two edits.**
   * Drop the multi-pair item (R.3.4).
   * Mark prerequisite 4 as already landed.

**Owner rules: no conflict found.**

* Game bytes stay run time only. CON_REGULAR comes from the disc, and `compose.py`'s PNGs stay in the research folder.
* The HUD is the game's own functions and draw state.
* Every option has a control under Q menu > Options > G-SA1 > HUD.
* The two host-side additions are an option (E-102's limit off) and a display-only crop (R.4.4).

**Verdict.**

* The causes hold, and "not covered by the running round" holds.
* The design is sound once these four changes are made:
  * F3's guard (measured);
  * F4's per-pair hold and respawn rules (the death loop is measured);
  * F2's attribute bits;
  * F5's archive capacity.
