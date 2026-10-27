# G-SA1 0.7.0 fixes: harness references for the bird, the HUD and drowning (prep-oracle)

Agent `prep-oracle`, 2026-10-21, for the main fix070 / HUD round (`gsa1_rounds_plan.md` 14.1h: "prep workflow (staged
decomp + references) now"). Inputs: `gsa1_070_bird.md` F2 and its Review R.4, `gsa1_070_hud.md` F7 and its Review
R.5, `gsa1_070_drown.md` (W1, E1, H1 and the Review R4.5). Scratch: `SADC/build/research/preporacle/`.

Abbreviations: SADC = `<project>\sadc`, LIB = `...\libsa1`. "Stock harness" = `harness/build/sa1h.exe`
(the interpreter build of 14:12, screen set up, which recorded every `.bin` below). Markers: **UNVERIFIED** = read
from code, not measured.

---------------------------------------------------------------------------------------------------------------------

## 1. Summary

| # | Deliverable | Where | Status |
|---|---|---|---|
| 1 | `amy_bird_ec1` (bird F2, unchanged by the review) | `harness/tests/sim/amy_bird_ec1.txt`; `build/traces/ref/amy_bird_ec1.{bin,csv,obj.bin,d2d.txt}` | recorded, `--check` identical (trace + host-object sidecar with the bird's taskwk every step) |
| 2 | HUD references, one action stage per character, game clock running | `harness/tests/sim/hud_{sonic_ec1,tails_sh1,knux_rm3,amy_tp2,big_ec3,gamma_fe1}.txt`, `harness/tests/sim/ho/hud_rings.ho`, `ho/hud_gamma_fe1.ho`; `build/traces/ref/hud_*.{bin,csv,obj.bin,d2d.txt}` | recorded, identical on re-run |
| 2a | the game's 2D draws (`njDrawSprite2D` / `njDrawPolygon2D` with priority, sprite, texanim, draw state, exec) of every frame | new trace option `--log-2d FILE` (`harness/src/t_sim.c`), reader `harness/tools/draw2d.py`; `NAME.d2d.txt` sidecars | 18 sidecars (the 6 HUD refs, the bird, the drowning ref, the 10 P7 refs), all reproduced by `draw2d.py check` (section 6) |
| 2b | code-map correction ("8C08ECB8 HUD master") | `docs/research/sa1_code_map.csv` row `0x8C089F20`, `sa1_code_map.md:302` | done |
| 3 | deep water (> ~440 below a water top) in the game's stages | section 5 | **none the game itself sees**: the LIB W1 test stays synthetic. Added instead: `big_drown_ec3`, a real-stage drowning (71 deep) with its countdown digits and the underwater tint as 2D draws |

Land dumps: every stage used already has one in `build/traces/land/` (`L01A0`, `L01A2`, `L03A1`, `L04A0`, `L05A2`,
`L10A0`), so no new dump was needed. Dumps of 47 more acts (for section 5) are in
`build/research/preporacle/land/` (research only, not references).

No file under SADC/src, include, tools, config, LIB or g-sa1 was changed. The only harness source change is the new
option in `harness/src/t_sim.c` (section 2.1), which no LIB build reads (`LIB/build.py` `LOWER_INPUT_FILES` names only
`harness/tools/gen_native.py` from the harness; LIB's tests include `harness/src/sa1_trace.h` / `sa1_trace_ext.h`,
both untouched).

---------------------------------------------------------------------------------------------------------------------

## 2. How they were recorded

### 2.1 Why a new harness option (`--log-2d`)

The stock harness already runs the HUD: `trace` calls the frame step `8C04E95A`, which ends with `8C08A4BA()` and
`8C08A724()` after its two logic steps (`gsa1_070_hud.md` 1.1). `--log-sdk` shows the calls
(`F 1 3 8C63EEB4 njDrawSprite2D(8C1BF4E0,B,fC0000000,20)`) but only their arguments: the sprite's position, scale,
texanim and the constant material are written into shared structures right before each call (`D_8C1BF4E0` is reused
for every digit; the timer's sprite is on the stack), so neither the call log nor RAM images at step boundaries can
recover them. No existing option records the contents, so `trace` got one:

* `trace ... --log-2d FILE` hooks `njDrawSprite2D` (8C63EEB4) and `njDrawPolygon2D` (8C63EBC8) like `--log-sdk` does
  (chained to any hook already there), writes one line per call at its entry, then runs the original. The run is
  unchanged: `regress.py --exe harness/build/sa1h_preporacle.exe` gave **39/39 identical** (all references of that
  time, sidecars included), and every sidecar below was written by a run whose `--check` against the stock-recorded
  `.bin` (and `.obj.bin`) passed.
* Lines are buffered per frame (a retried frame drops its own lines) and written at the end. The option is not in any
  script's `# args:` line, so builds without it (the stock `sa1h.exe` of 14:12) still record and check every script.
* Patch: `build/research/preporacle/patch/apply_d2d.py` + `d2d_block.c` (11 anchored edits, tested on a copy and
  compiled before it was applied atomically). Private build: `py -3.12 harness/build.py --files none --tag preporacle`
  -> `harness/build/sa1h_preporacle.exe` (interpreter only: the references never route native code).

### 2.2 The `NAME.d2d.txt` format (version 1)

Header lines (`#`) name the columns. One line per draw, space-separated; floats are 8-hex-digit bit patterns.

```
common:  kind phase step seq frame sub exec pr obj n pri attr texlist cmat_a cmat_r cmat_g cmat_b blend ctl3d attr_and attr_or
S        common, p.x p.y p.z sx sy ang tlist tanim, ta.sx ta.sy ta.cx ta.cy u1 v1 u2 v2 texid ta.attr, texname name
P        common, p col tex num, then n vertices x,y,argb,uv ('-' = no colour / UV list)
```

| Column | Meaning |
|---|---|
| kind | `S` sprite (`njDrawSprite2D`), `P` polygon (`njDrawPolygon2D`) |
| phase / step | `S` = inside logic step `step` (task executor, CCL ...), `F` = the frame code after step `step` (where the HUD runs; `step` is always the second step of a pair), `D` = the display pass of `--disp` (not used by the references) |
| seq | the draw's index within (phase, step) |
| frame, sub | `B_8C752BE8`, `B_8C754EC8` at the call. The harness never runs the frame advance `8C08A5BE` (it lives in the main loop `8C053B60`, not in `8C04E95A`), so `frame` is always 0; `sub` is 2 in phase F |
| exec | `D_8C1C1D40`, the exec the task executor is running (0 in phase F): the HUD round's planned SPRITE2D payload field (`gsa1_070_hud.md` 4.1.3) |
| pr | the guest caller (return address) - which function drew it |
| obj | the `NJS_SPRITE *` / `NJS_POINT2COL *` argument (stack addresses for some callers) |
| n, pri, attr | the call's arguments (`n` = texanim index for a sprite, vertex count for a polygon) |
| texlist | `D_8C673480` before the call (the draw sets it to the sprite's tlist) |
| cmat_*, blend, ctl3d, attr_and / attr_or | the draw state the game set: `B_8C88F668[4]` (constant material, ARGB float bits), the Kamui context's blend modes (src \| dst << 8, as libsa1's capture `blend`), `B_8C88F62C`, `B_8C88F660/4` |
| sprite payload | the NJS_SPRITE's 0x20 bytes (p.x .. tanim) and `tanim[n]` (sizes, centre, UVs, texid, attr): libsa1's SPRITE2D payload (`rt_cap_sprite`, 0x34 bytes) |
| texname, name | `tlist->textures + 12 * texid` (the `NJS_TEXNAME` address libsa1 resolves textures by, `sa1_capture_resolve`); its file name when the entry points at one (`-` for the CON_REGULAR / FISHING lists: their names are not in RAM) |
| P vertices | `p[i]` (x, y), `col[i]` (ARGB), `tex[i]` (u, v as two Sint16) for i < n |

Known quirks (the HUD round's comparison must skip them; `draw2d.py diff --lib` does):
* `p.z` is not set by every caller (`8C0E72DA` leaves stack garbage; its C has `PC_ONLY(sp.p.z = 0)`).
* `ang` matters only with `NJD_SPRITE_ANGLE` (attr bit 1: Big's reel, attr 0x21); Big's HUD sprite at 8C0E7060 has
  garbage there with attr 0x20.
* `obj` and the polygon pointers are stack addresses for stack sprites (the timer, Big's gauge).

### 2.3 Tools

```
py -3.12 harness/tools/draw2d.py info  build/traces/ref/hud_big_ec3.d2d.txt          # draws per kind / phase / caller
py -3.12 harness/tools/draw2d.py dump  build/traces/ref/hud_gamma_fe1.d2d.txt --from 308 --to 308
py -3.12 harness/tools/draw2d.py diff  A.d2d.txt B.d2d.txt [--lib] [--ignore COL,..]  # draw by draw
py -3.12 harness/tools/draw2d.py check --exe harness/build/sa1h_preporacle.exe       # every ref with a sidecar
```

Record a sidecar for a reference (any script; the `.bin` stays the stock one):
`sa1h_preporacle.exe trace <the script's # args> --input @harness/tests/sim/NAME.txt --check build/traces/ref/NAME.bin
--log-2d build/traces/ref/NAME.d2d.txt` (`build/research/preporacle/make_sidecars.sh NAME..` does this).

---------------------------------------------------------------------------------------------------------------------

## 3. Amy's bird: `amy_bird_ec1` (gsa1_070_bird.md F2)

* Args: `--level 1 --act 0 --chr 5 --no-objects --create-task 8C0DECDA:2:3` (the review keeps F2 unchanged: Emerald
  Coast act 1, whose stage 0x0100 fails the spawner gate `8C0DEDC0`, so the only bird is the spawner's own call
  `CreateElementalTask(2, 3, 8C0DECDA)` made after LoadPlayerTask). Amy starts at the origin (EC1 has no start
  position for her).
* Inputs (1,425 steps): stand (birth 12 above Amy), run 90, stop, jump, run, a full stick circle (turn-around), jump,
  hammer, standing hammer, jump hammer, run / reverse x4 (20-step legs), a zigzag (6 x 12 steps), run, hammer, then
  600 steps of idle (the circle `ang.x += 16` and the bob, period 64).
* Sidecar `amy_bird_ec1.obj.bin`: handle 0x80 = the bird, its taskwk pos / ang / mode / flag every step
  (`sa1trace.py obj build/traces/ref/amy_bird_ec1.bin`). Step 0: (0, 12, 0), ang.y 0x8000.
* Checks: `regress.py --only amy_bird_ec1` (stock): identical, sidecar identical; `amy_bird_ec1.d2d.txt` (the HUD with
  the clock stopped: 10,680 draws) identical on re-run.
* The gate-passing case (the game makes the bird itself) is covered by the existing `amy_tp2` / `obj_tp2_amy` / now
  `hud_amy_tp2` (Twinkle Park act 2); `gsa1_070_bird.md` E5 found the bird's taskwk identical there.

**LIB replay (next round):** list `amy_bird_ec1` in `build_replay.py`'s `HOSTOBJ` group (Review R.4). In
`world_replay.c`'s `wr_obj_start`, the sidecar task with exec `0x8C0DECDA`, im 2, list 3 and no position is made with
`sa1_character_set_companion(ctx, id, SA1_COMPANION_BIRD, 1)` **outside** any `sa1g_world_call` (R.2.7: the tick lock
is not recursive), then `wr_run.task_tp[k]` is found by a world call walking `B_8C79F04C[3]` for exec 8C0DECDA /
8C0DE964. Strict V6 / V5 / DISC, sidecar 0 differing records, V7 x86 = x64. Land dump `L01A0`. F5's interpolation
test ("turn" case) can take its sharp turns from steps 520-670 of this reference (run / reverse legs and the zigzag).

---------------------------------------------------------------------------------------------------------------------

## 4. The HUD references (gsa1_070_hud.md F7 and Review R.5)

### 4.1 Setup

| Script | Args (all `--no-objects`) | Steps | 2D draws | Character-specific draws (phase S, exec) |
|---|---|---|---|---|
| `hud_sonic_ec1` | `--level 1 --act 0` | 992 | 12,885 | EC1's lens flare (exec `0C90024C` = the STG01 level task; callers 8C04605C / 8C0460C4) |
| `hud_tails_sh1` | `--level 4 --act 0 --chr 2` | 734 | 6,606 | Tails' race progress bar `8C0817E0` (exec `8C07FC70`, the race opponent; callers 8C081862 / 8C0818A4 / 8C0818B8, at (184, 424), sub 1) |
| `hud_knux_rm3` | `--level 5 --act 2 --chr 3` | 719 | 6,462 | Knuckles' emerald radar `8C0CAC28` (exec `8C0C967A`, caller 8C0CAC8E: 3 per pair at (285 / 320 / 355, 418), scale 0.5, alpha 0.2, blend 0A01) |
| `hud_amy_tp2` | `--level 3 --act 1 --chr 5` | 711 | 5,325 | none (the game's bird draws no 2D) |
| `hud_big_ec3` | `--level 1 --act 2 --chr 7` | 766 | 3,478 | Big's HUD task (exec `8C0EB160`: 8C0E6FE0 at 8C0E7060 / 8C0E7078 / 8C0E70A2 + numbers through `8C08EE98`, caller 8C08F0E8); while the lure flies (exec `8C0E943C`): the reel 8C0E72D2, the gauge frame 8C0E731C, the tension bar **P** 8C0E7464 and the line-length digits (8 pairs) |
| `hud_gamma_fe1` | `--level 10 --act 0 --chr 6`, `ho/hud_gamma_fe1.ho` | 11,560 | 87,614 | the "+N" time bonus counter (exec `8C0D520A`, callers 8C0D51DC / 8C0D4C02, steps 308-462), the last-seconds digits (exec `8C0CBE00`, callers 8C0D6164 / 8C0D618A, steps 10802-11558) |

Common to all six:
* `--create-task 8C08A5AE:2:0`: a task whose exec is `func_8C08A5AE` (`D_8C1BF198 = 1`, the clock-on call of the
  frame state machine `8C04CA40` / `8C04DA20`, which the harness does not run). It sets the clock on every step from
  step 0, so the clock `8C08A5CC` (in the step tail) runs: count-up for five characters, E-102's countdown from 3:00.
  The task is in the `.obj.bin` sidecar (handle 0x80; libsa1's `world_replay` makes sidecar tasks after the boot).
  Without it the harness's clock stays stopped (the P7 references: 00:00:00, E-102 03:00:00).
* `--hostobj harness/tests/sim/ho/hud_rings.ho` (E-102: `hud_gamma_fe1.ho`, the same calls 400 steps later after
  `ho_e102_fe1`'s enemies): `rings` calls = `sa1_character_add_rings`, clamped by the layer to 0..999
  (`sa1_world_hostobj.c` `ho_rings_cb`) and paid through the game's `8C08A198` (a 1-up per 100). Measured rings /
  lives: 0 / 4 (the zero-ring blink, `cmat` g = b pulsing), 7, 123 / 5, 999 / 13, 999 (+1 clamped), 0, 250 / 15, 205,
  0. Lives 0 and 127, times 2:59:59 / 100:00:00 and levels 26 / 35 / 38 (F7's other states) need RAM set-up: they are
  for the function-level oracle of the HUD round (`hud-oracle`).
* Inputs: each character's P7 script (run, jump, turn, its action), then standing; Big casts four times.

E-102 (`hud_gamma_fe1`): the clock reaches 0 at step 11160 -> mode 49 action 69 (`8C0D23E4`: time-up death), mode 51
from step 11242; 400 steps after the death are kept. The timer digits go white / yellow under 30 s and red under 15 s
(`8C08A7E0`, e.g. step 11101: cmat (1, 1, 0.21, 0)); the last-seconds task draws a digit at (320, 415.2) and
`e102lastsec` (cell 13) at (320, 380) from step 10802. The lock-on chain of `ho_e102_fe1` (HIT x4, three KILLED
removals) pays the bonus through `8C0D5064`, whose counter draws at (540, 120) -> (540, 110) (steps 308-462).

Big (`hud_big_ec3`): the lure misses the water on every cast here (lure mode 3 -> 7: `8C0E93AE` finds no water under
the landing point, SE 0x31F, `code_8C0E91E4.c:325-346`), so the reel / gauge / tension bar are drawn for 1-2 pairs per
cast only (the HUD research's libsa1 probe, part B, saw the same: 4 pairs in 240). Big's HUD (WEIGHT, rings, lives)
is drawn every pair; the bar's colours are the game's gradient (`FF00FFFF` / `FF0000FF` at tension 0, `FF05FFFA` /
`FF0500FA` a pair later).

The ten P7 references (`ec1_*`, `tails_sh1`, `knux_rm3`, `amy_tp2`, `gamma_fe1`, `big_ec3`) and `amy_bird_ec1` also
have sidecars now: the HUD with the clock stopped and no ring changes (a cheap regression set for "the HUD pass
draws the same thing in every reference").

### 4.2 How the HUD round's LIB tests should replay them

1. **Strict replay (the game state):** add the six `hud_*` names to `build_replay.py` as a new group (they carry an
   `.obj.bin`: the sidecar task `8C08A5AE` im 2 list 0, and the `rings` calls), replayed exactly like the `HOSTOBJ`
   group (V6 / V5 / DISC, sidecar compare, V7). Keep the HUD pass **off** for this (part D of the HUD research: the pass
   writes 26 bytes of display state; the trace never sees them, but keep references and live HUD separate). Use the
   sidecar task, not `SA1_COPT_CLOCK`, in the strict replay (same heap layout as the reference).
2. **The 2D comparison:** run the same replay with the HUD pass on (`hud_fn` before the frame advance, all parts, the
   game's own guard of Review R.5.1) and capture SPRITE2D / POLY2D with `pri` and `exec` (F2). Write the pair's 2D
   records in this file format (`pr` 0) and compare with `draw2d.py diff ref.d2d.txt lib.d2d.txt --lib` (or the same
   rules in C):
   * harness phase **F** lines after step 2k+1 <-> the HUD pass's records of pair k, in order;
   * harness phase **S** lines of step 2k (sub 0; Tails' bar: step 2k+1, sub 1) <-> the records made inside the
     executor in that sub step, with the same `exec`;
   * compare kind, n, pri, attr, texlist, cmat x4, blend, ctl3d, attr_and / attr_or, the sprite's p.x / p.y / sx / sy /
     tlist / tanim and texanim, texname (and ang when attr & 1), a polygon's n and vertices; skip obj, pr, frame, sub,
     p.z and the point-list pointers (`--lib`).
   * Per exec, what libsa1 worlds run (filter the rest out, or make them run first): F phase (8C08A4BA / 8C08A724):
     always, once the pass exists. `8C0EB160` and the lure display: after hud-decomp F1 (8C0EB160, 8C0E6FE0, 8C08EE98
     are void stubs today). `8C0D520A` / `8C0CBE00` (E-102): already in worlds (the "+N" counter needs 5-lib-game's
     H4 bonus path, which `hud_gamma_fe1.ho` exercises). `8C07FC70` (Tails' race opponent) and `8C0C967A` (the radar):
     **UNVERIFIED** whether LIB's SH1 / RM3 replays run them. `0C90024C` (EC1's lens flare): LIB's Emerald Coast stand-in
     does not run the STG01 level task, so drop these lines.
3. **Clock and E-102 options (F4):** the reference's times are the game's own clock; a test of `SA1_COPT_CLOCK` /
   `SA1G_BOOT_CLOCK` must give the same digits as the F-phase timer lines (count-up from 00:00:00 from step 0; E-102:
   02:59:59 ... time-up death at step 11160). F4's "limit off" must instead hold the timer at 3:00 (no reference: that
   is a host option).
4. **Texture resolution (F5):** every F-phase sprite's `texname` is a CON_REGULAR entry (`0x8C1C3070 + 12 * cell`);
   Big's are FISHING (`0x8C54CAxx`) and, for his life icon, CON_REGULAR 19 (`8C1C3154`): F5's "every HUD sprite
   resolves" can be checked against these columns.

### 4.3 Code-map correction (F7, Review R.5.5)

`docs/research/sa1_code_map.csv` row `0x8C089F20-0x8C08F1A0` and `sa1_code_map.md:302` no longer call `8C08ECB8` the
"HUD master": the in-play HUD is `8C08A4BA` (rings, lives; draw `8C08A2C2`) and `8C08A724` (timer; draw `8C08A7E0`),
called by the frame steps `8C04E95A` / `8C04EA4A` / `8C04EB7E`; `8C08ECB8` is the stage-clear results task (created by
`8C04EFE0` / `8C04F20A`, dispatching by stage to 8C08CA40 / 8C08E380 / 8C08E93A / 8C08DC32 / 8C08C100 / 8C08D1C0).
The CSV still parses (126 rows x 7 columns).

---------------------------------------------------------------------------------------------------------------------

## 5. Drowning: is there deep water (> ~440) in the game? (gsa1_070_drown.md W1)

### 5.1 Method

* **Landtable water** (`build/research/preporacle/land/`, 47 acts: EC 1-3, TP 1-3, LW 1-3, IC 1-4, CP 1-4, HS 1-4,
  bosses 15-19, Station Square 26 (6 acts), Egg Carrier 29 / 32, Mystic Ruins 33 / 34; `--dump-land` with the stock
  harness, `dump_lands.sh`): `tools/water_depth.py` samples every upward face of every COL with `slAttribute &
  0x400002` (the water probe's test, `code_8C0925A0.c:61`), finds the highest solid non-water face under each sample
  and asks whether the original probe would still see the water from that floor: the landtable gather takes a COL
  when `|p - c| - 200 < xWidth` (`code_8C0522C0.c:138`). Output: `land/water_depth_all.txt`.
* **Task-registered water** (dynamic land objects, `8C0528AE`; registry `B_8C759708`, count `B_8C759704`): 13 stage
  runs **with objects** (30 steps, `--dump-ram-at end`), read by `tools/dynwater.py` (object position, model radius,
  extent, landtable floors under it). Outputs: `dyn/*.log`, printed in this section. The asm scan of `8C0528AE`
  callers with a water attribute found only STG01 (the ocean), STG12 (Hot Shelter) and STG09 (Casinopolis, attr
  0x80000003).

### 5.2 Results

| Stage | Deepest water (top -> floor) | Seen by the game's probe from that floor? | Notes |
|---|---|---|---|
| EC1 (L01A0) landtable | 1.3 | yes | beach water; the sea itself is STG01's ocean task: a 9-point object (r 20) at y -1.5 that follows the player in 25-unit steps (dynamic reach r + 230 = 250); the deepest floor under it is -13.3, so at most ~12 deep |
| EC3 (L01A2, Big) | 164-171 (COL 186, top -2, floor down to -173) | yes (COL r 1071) | the lake `big_drown_ec3` drowns in (71 deep) |
| TP2 (L03A1) | 147.6 | yes | |
| **LW2 (L07A1)** | **468.7** (COL 147, top -2197, floor -2665.8 at (7465, 1201)) | **no**: COL 147's reach is r + 200 = 394.2 from its centre (7415, -2197, 1314); the deepest floor the probe still sees is **363** (the -2560 platform, COL 67) | the original game itself treats the bottom of this pool as dry (no air timer, no water physics); COL 140 (flags 0x403, r 30, "660.7") is a solid water column (waterfall), not swimmable |
| IC2 / IC4 (L08A1 / L08A3) | 202.4 | yes | |
| MR (L33A0) | 451.5 (COL 137, flags 0xA0000403) | yes | a solid waterfall column (0x1 set, 2 of 24 faces upward at y 434-479): a player cannot be inside it |
| MR (L33A2) | 156.2 | yes | |
| HS act 1 (Amy), dynamic | 235 at most (objects at y 155, r 526; y -80, r 277; the lowest geometry under them is -80) | yes (reach r + 230) | the tank room's water levels |
| CP act 1 (Sonic), dynamic | 200 (attr 0x80000003 at y -12, r 589; floor -212) | yes | |
| all other acts | < 60 | yes | |

### 5.3 Conclusion

**The game has no place where a player can be deeper than ~440 below a water top while the game still sees the
water.** The original's water probe has exactly the limit libsa1's GRID objects show (the gather reach: 200 + the
COL's radius, 230 + r for task objects); the stages are built so that it never matters, with one exception (Lost World
act 2's pool, where the original loses the water below ~363-394). So:

* **The LIB W1 test is synthetic-only** (as `gsa1_070_drown.md` 5 W1 specifies: host water boxes 476 / 1000 / 2000
  deep in `grid_test.c` or a LIB drown test). No harness reference can show the original counting air at those depths:
  the original would not. W1 is a host-adaptation choice (owner question 5 of the drown doc: "deep GMod water follows
  the game's water rules everywhere"), not a parity fix.
* A W1 implementation must still leave every stage replay strict (the 10 P7 references and `big_drown_ec3` replay in
  DISC / stage mode, where W1's GRID window pass does not run), and its synthetic test can use the original's own
  reach rule to state the "before" value exactly (the drown review's R2 table).

### 5.4 Added: a real-stage drowning reference, `big_drown_ec3`

* Args `--level 1 --act 2 --chr 7 --no-objects`; 2,655 steps. Big walks into the EC3 lake and sinks to its floor
  (y -73, 71 under COL 186's top -2, well inside the reach). Timeline (trace + `d2d` + RAM images): air timer from
  about step 254; the countdown task `8C07BE6E` is created in step 1334 (in the task list of the RAM image before step
  1335, not before 1334), so its timer t = step - 1334; its digits (exec `8C07BE6E`, through `8C08EE98`, caller
  8C08F0E8), drawn on even steps only: **"5" first at step 1696, "4" at 1814, "3" at 1874, "2" at 1934, "1" at 1994,
  the last at 2052** - exactly the drown review's R3.4 boundaries (5 from t 361, 4 from 480, ..., none from 720);
  smode 39 / mode 53 action 98 in the record of step 2115 (t 0x30C + 1); the restart request `8C04E386(2)` at t 0x3FC,
  about step 2354 (**UNVERIFIED** in the trace: it only stores the game state); 300 more steps.
* Also in its 2D log: the **underwater screen tint**, a full-screen `njDrawPolygon2D` (exec `8C0197BA`, callers
  8C01C260 / 8C01C270, attr 0x60 = fill + translucent, colour `40000040`, steps 276-2414). Neither libsa1 nor G-SA1
  draws it today (2D polygons are not emitted); it belongs to the HUD round's POLY2D path (F2 / F6) - the exec is the
  camera code's (**UNVERIFIED** which task; check that LIB worlds run it).
* Uses for the next round: E1 (`SA1_EVENT_DEATH` cause DROWN at step 2115 and RESTART id 2 about 2354 in a DISC replay
  of a real stage), E2 (lives after the restart), the drown review's R4.5 H1 test (its frames 360 / 361 / 479 / 480 /
  719 / 720 = steps 1694 / 1695 / 1813 / 1814 / 2053 / 2054 here: odd steps draw nothing, so compare the even ones),
  W1's "stage water unchanged" regression, and the jukebox's jingle hold (A1 / J1: jingle 96 at the task's first
  frames, about steps 1334-1335 - the event steps come from LIB's own replay, the harness log has no sound events).
* Replay: a plain P7-style entry in `build_replay.py` (no sidecar); land dump `L01A2`.

---------------------------------------------------------------------------------------------------------------------

## 6. Verification (commands run, 2026-10-21 15:35-16:25)

| Check | Result |
|---|---|
| patched `t_sim.c` compiled from a copy first (`zig cc -target x86-windows-gnu -Wall -Wextra`), then applied | no warnings; `cmp` with the tested copy identical |
| `py -3.12 harness/build.py --files none --tag preporacle` | `sa1h_preporacle.exe` built (26 objects) |
| `regress.py --exe harness/build/sa1h_preporacle.exe` (all scripts of that time) | **39/39 identical** (default behaviour unchanged) |
| `regress.py --only amy_bird_ec1 --update`, then without `--update` (stock exe) | recorded; identical, sidecar identical |
| `regress.py --only hud_*,amy_bird_ec1 --update`, then check (stock exe) | 6 recorded; 7/7 identical |
| `regress.py --only big_drown_ec3 --update`, then check (stock exe) | recorded; identical |
| `make_sidecars.sh` (18 references, each with `--check` the stored `.bin` / `.obj.bin`) | every trace and sidecar identical, 18 `.d2d.txt` written |
| `draw2d.py check --exe harness/build/sa1h_preporacle.exe` | 17/17 identical (before `big_drown_ec3`); then `--only big_drown_ec3,amy_bird_ec1` 2/2: all 18 sidecars reproduce |
| full `regress.py` with the stock exe (`harness/build/sa1h.exe`, 14:12) after all additions | **46/46 identical** (the 38 older references unchanged, the 8 new ones deterministic) |

## 7. Files

* Harness: `harness/src/t_sim.c` (the `--log-2d` block "the game's 2D draws", the option, usage line, 6 small hooks),
  `harness/tools/draw2d.py` (new), `harness/README.md` (2D logs section and the new references),
  `harness/tests/sim/{amy_bird_ec1,hud_sonic_ec1,hud_tails_sh1,hud_knux_rm3,hud_amy_tp2,hud_big_ec3,hud_gamma_fe1,big_drown_ec3}.txt`,
  `harness/tests/sim/ho/{hud_rings,hud_gamma_fe1}.ho`.
* References: `build/traces/ref/{amy_bird_ec1,hud_*,big_drown_ec3}.{bin,csv}` (+ `.obj.bin` for the bird and the HUD
  refs) and `NAME.d2d.txt` for those plus `ec1_stand`, `ec1_run_jump`, `ec1_spindash`, `ec1_jump_hold`, `ec1_turns`,
  `tails_sh1`, `knux_rm3`, `amy_tp2`, `gamma_fe1`, `big_ec3`. `hud_gamma_fe1` is large (13.7 MB trace, 20 MB sidecar,
  22 MB 2D log).
* Docs: this file; `docs/research/sa1_code_map.{csv,md}` (the HUD row).
* Scratch (`build/research/preporacle/`): `patch/` (the patch and its test copy), `scripts/` (drafts, `run_trials.sh`),
  `bigx/` (Big's cast and drowning experiments: CSVs and 2D logs), `land/` (47 land dumps, `water_depth*.txt`),
  `dyn/` (the logs of the 13 stage runs with objects; their RAM images were deleted after `dynwater.py` read them:
  `sa1h_preporacle.exe trace --level L --act A --chr C --steps 30 --input "30 0 0" --out dyn/LxxAxCx --dump-ram-at end`
  recreates one), `tools/{water_depth,lowfloor,dynwater}.py`, `make_sidecars.sh`,
  `dump_lands.sh`, `regress_*.txt`.

## 8. Open / UNVERIFIED

* Big's lure never lands in water in `hud_big_ec3` (only 8 pairs of fishing draws); a longer fishing session (lure
  mode 5, the tension bar moving with a fish) needs a cast that lands in the lake (`8C0E93AE`) - not found with these
  inputs; with objects (fish) not tried.
* Whether libsa1's SH1 / RM3 worlds run Tails' race opponent (8C07FC70) and Knuckles' radar (8C0C967A), and which task
  `8C0197BA` (the underwater tint) is.
* The restart request step (about 2354) is computed from the task timer, not seen in the trace.
