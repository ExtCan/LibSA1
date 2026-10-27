# Reference simulator (`sa1h trace`)

The SH-4 interpreter harness (`harness/`, see `harness/README.md`) can run the original game the way
the Dreamcast does between two vblanks: the game's own init, stage load, stage start and per-frame
logic, from the user's own `1ST_READ.BIN` and disc files, with high-level emulation (HLE) only where
the game talks to hardware. The player is driven by a scripted pad and the player state is recorded
after every 60 Hz logic step. The resulting **trace** is the reference a PC port of the physics
(libsa1, `docs/libsa1_plan.md`) is compared to: validation ladder step 3 of
`docs/research/critique.md` 5.9.

Everything the simulator produces from the game files (traces, geometry dumps) goes to `build/`,
which is git-ignored. Nothing from the game is compiled into the harness.

## Quick start

```
py -3.12 harness/build.py                                   # harness/build/sa1h.exe

# Emerald Coast act 1, Sonic, no objects: stand 30 steps, run 120, jump, run, turn
harness/build/sa1h.exe trace --level 1 --act 0 --no-objects \
    --input "30 0 0; 120 0 -127; 1 0 -127 A; 60 0 -127; 60 60 -100" --out build/traces/run_jump

harness/build/sa1h.exe trace --input @harness/tests/sim/ec1_spindash.txt --no-objects
py -3.12 harness/tests/sim/regress.py                       # all sample scripts vs stored references
py -3.12 harness/tools/sa1trace.py dump build/traces/run_jump.bin --from 145 --to 160
py -3.12 harness/tools/sa1trace.py diff REF.bin OTHER.bin --ulps 4
```

Output of the first command (abridged):

```
sim: game init func_8C053AC0
sim: character 0: func_8C04F5F4 (motion file, textures)
sim: stage load func_8C04E490 (level 1 act 0)
sim: stage start func_8C04E660
sim: ready after 211258242 instructions; ... 2 logic steps per frame
  start: mode   0 smode   0 flag 0000 pos (   -9.0000    16.0000     4.0000) ang (0000 6000 0000)
  step    0: in (   0,   0,0000) mode   1 act   0 pos (   -9.0000    16.0000     4.0000) ... attr 00000000 land 18
  step   30: in (   0,-127,0000) mode   2 act   9 pos (   -9.0362     3.9976     4.0342) ... attr A0100001 land 18
  step  150: in (   0,-127,0004) mode   8 act  14 pos ( -159.5175     3.5685    83.0695) ... attr 00000000 land 20
  step  195: ...                  mode   2 act  12 pos ( -250.9234     1.5721   129.4101) ... attr A0100001 land 23
sim: 271 steps, 263884 instructions per step
```

CSV excerpt of the same run (`build/traces/ref/ec1_run_jump.csv`, selected columns):

```
step,sub,in_y,in_buttons,an_ang,an_stroke,mode,action,pos_x,pos_y,pos_z,pspd_x,pspd_y,ang_y,attr,land_count
0,0,0,0,0000,0,1,0,-9,16,4,0,0,6000,00000000,18
1,1,0,0,0000,0,12,0,-9,15.9199991,4,0,-0.0799999982,6000,00000000,18
19,1,0,0,0000,0,1,1,-8.99999809,3.99764824,4.00020075,0,0,6000,A0100001,18
30,0,-127,0,6C15,0.990317225,2,9,-9.03628063,3.99764729,4.03423023,0.0495158546,0,6182,A0100001,18
149,1,-127,0,6CAA,0.990317225,2,12,-157.063309,2.08201361,81.791214,2.73824573,0,6CAA,A0100001,20
150,0,-127,4,6CAD,0.990317225,8,14,-159.517563,3.56856418,83.0695038,2.76715946,1.48655057,6CAA,00000000,20
170,0,-127,0,6CE0,0.990317225,8,14,-201.552307,14.5845938,104.629776,2.13269138,-0.240886286,6CCC,00000000,24
195,1,-127,0,6D1B,0.990317225,2,12,-250.923477,1.57215452,129.410126,2.40708137,0,6D1B,A0100001,23
240,0,-100,0,758D,0.645275831,2,12,-371.413513,4,159.200836,3.13661098,0,758D,A0100001,22
```

Sonic drops from the start position (y 16, gravity -0.08 per step), lands on the deck at step 19
(mode 1, ground attribute `A0100001` = the deck polygon's COL flags), accelerates when the stick is
pushed (mode 2, actions 9-12), jumps on A (mode 8, action 14, vertical speed 1.49 after the first
step; Sonic's `jmp_y_spd` is 1.66), lands at step ~195 and turns with the stick.

## What runs

All of it is original code from `1ST_READ.BIN` and the stage overlay, in the interpreter, in the
order the game's in-game state machine `8C04CA40` uses (states 0/1 -> 4/0xF):

| Step | Function | What it does |
|---|---|---|
| 1 | `8C053AC0` | game init: Ninja/Kamui/Shinobi SDK, sound driver (`MASARU.DRV`), GD file system, pad |
| 2 | `8C04F5F4(chr)` | character select: `S_MOT.PRS` -> `0x0CC00000` (`8C09D980`), `SONIC.PRS`/`SON_EFF.PRS` textures |
| 3 | `8C04E490` | stage load: `stg01.PRS` decompressed by the game's PRS decoder `8C04C5E0` into `0x0C900000`; `SET0100S.BIN`.. (objects) to `0x0CB68000`.., `CAM0100S.BIN`.. (camera layout) to `0x0CB50000`..; landtable sphere init `8C052BA0` over `D_8C19E9FC[level][0..7]`; sound banks, PVM textures, heap `8C098A10`, task system `8C09844A`, SET manager task |
| 4 | `8C04E660` | stage start: `8C051760(2,2)` (2 vsyncs and 2 logic steps per display frame), camera, sky/fog task `8C052FA0`, per-level tasks (`D_8C19E0E0`, `D_8C19DE30`), **landtable manager task `8C0519C0`**, `LoadPlayerTask 8C04F3C0` (start position `8C04F8FA`, `Sonic_Start_Array`), death zones `8C07BAC0`, CCL, effects |
| per frame | `8C04E95A` | `B_8C754EC4` (= 2) logic steps `8C04E714` with `B_8C754EC8` = 0, 1; pad refresh `8C0517F6` between them; press bits OR-ed across the steps; game timers `8C08A4BA`, `8C08A724` |
| per step | `8C04E714` | pad `8C050BE4` (pdGetPeripheral, camera-relative analog `8C05117E`), `8C099A50`, task executor `8C0988A4` (player, camera, landtable task, objects), CCL `8C0342E0`, `8C09DF64`, **land collision world `8C028EC2`** (gathers the landtable list `8C052518` -> `B_8C754EF0/B_8C754EF4`), event/HUD checks |
| optional | `8C0988C4` | display pass (`--disp`): not part of a normal frame (the game runs it only from the pause and frozen states, with `B_8C754EC8` = 0); it changes the logic of some scripts (Determinism, below), so the references do not use it |

How the landtable gets to the collision code: `D_8C19E9FC` is a static `[level][8 acts]` table of
landtable pointers into the stage overlay (Emerald Coast: `0C981554`, `0C9DEB60`, `0CA1DD58`, the
same addresses as SA Tools' `STG01.ini`). The landtable manager task's first exec (`8C0519C0` mode 0
-> `8C051CC0`) sets `B_8C7596F4` = current landtable from that table; every step the collision world
`8C028EC2` calls `8C052518`, which copies COL entries with `flags & 0x400003` near the players into
`B_8C7584F4`/`B_8C757EF4` and `B_8C754EF4` (count `B_8C754EF0`), from which `8C0522C0` gathers per
query. So loading the overlay and running the stage start is all it takes; no global is poked.

The trace records the state after each `8C04E714`: the step is wrapped by a hook (set the pad for this
step, run the original `8C04E714`, record), so every logic step is bracketed exactly.

## HLE: what the simulator replaces

| Address | What | Replacement |
|---|---|---|
| `8C6016D6`, `8C600E2C`, `8C600F1E`, `8C600DF4` | gdFsOpen / GetFileSize / Read / Close (Shinobi) | read the file from the disc folder (`disc/SONICADV/`, `--disc DIR`), upper-cased name; handles are fake (`0x8C000800 + 16*k`) |
| `8C601F5A` | pdGetPeripheral (Shinobi) | port A: a standard controller (`id` 1, `support 0x000F06FE`) whose `on/off/press/release/x1/y1` come from the input script for the current step (record at `0x8C000400`); ports B-D: an all-zero record (not connected) |
| `8C601E2A` | peripheral server (maple DMA) | nothing |
| `8C06C1B2` | frame time from TMU1 (`0xFFD80018`) | returns 0.0: the frame loop never adds catch-up steps |
| `8C06C182`, `8C06C15A` | debug timing (TMU1) | nothing / 0.0 (needed for `--route`: the decompiled copies would read the hardware address natively) |
| `8C604594` | SDK vsync wait | return |
| `8C604702` | Ninja's `njInitSystem(mode, frame, count)` (until 2026-10-20 this table called it a vsync wait), called once by the game's video init `8C010180` | by default its view part (`hle_nj_init_system`): the mode words `B_8C88F620/24/28`, the screen size from the mode bits as the original computes it (640 x 480 for the VGA mode 1 the BIOS HLE reports; `B_8C88F5E8/EC`, `B_8C88F644/48`), then the original `njInitView 8C604BA0` in the interpreter (distance 500, aspect 1, 1, near / far, clip rectangle (0, 0, 639, 479)); Kamui, PVR, interrupt handlers and the Holly busy-wait stay out. `--no-screen`: return (the old behaviour: the screen stays 0, see "The screen" below) |
| `8C03700C`, `8C037098` | the game's hand-written model renderers (sub step 0 only; own frustum test `8C03718C`) | return (with the screen only: they would now reach their TA output, which reads renderer state that is never set up; they only draw) |
| `8C051790` | frame-end vsync wait + render | return (only called by init here) |
| `8C037108` | land strip renderer (SH-4 asm, only caller the land draw `8C051E56`) | return (reads strip data through renderer state that is never set up) |
| `8C65A5DC` | sound driver handshake (SDK, from sound init `8C092FA0`) | return 0 |
| `8C10C1C8` | AFS partition open for voice ADX (BGM/voice task `8C107864`) | return -1 (failure path) |
| BIOS vectors `8C0000B0..E0` | syscalls | `hle_install_bios` (existing) |
| `SLEEP`, `TRAPA` | | no-ops |

Plus, per display frame, `B_8C754EC0` (vblank countdown) is set to 0 so the waits in `8C0517F6`
fall through. The vblank interrupt handler `8C051810` is not run: it only calls the per-stage
vblank callbacks of `D_8C19E3E4` (fog/display: `8C05180C` is empty, `8C051900`..`8C05194C` set fog
tables) and SDK display/GD functions.

`--discover` keeps the `boot`-style auto-stubber as a safety net (on a fault: stub the innermost SDK
function, restore RAM, retry the frame) and lists what it had to stub; with the table above it
stubs nothing: all 35 acts of levels 1-10 and 12 load and run (Sonic, objects on, 151 steps of
stand/run/jump) without a fault. Acts Sonic does not play (no entry in his start table) start at
(0,0,0) and fall. Without `--discover` a fault stops the run and prints the call
stack.

Global state the harness sets itself: `CurrentCharacter B_8C7493C8`, `CurrentLevel B_8C7493BA`,
`CurrentAct B_8C7493BC`, game mode `tcrf_LevelSelectVar 8C760970 = 4` (in game), `B_8C754EC0 = 0`
before every frame, and with `--spf N` `B_8C754EC4 = N` after the stage start. Low-RAM areas used
by HLE: `0x8C000100` (BIOS stubs), `0x8C000400`/`0x8C000480` (pad records), `0x8C000800` (file
handles), `0x8C000C00` (scratch for `--dump-land`).

## Determinism

Same build + same input = bitwise identical trace (`--check REF.bin` compares every record byte
except `icount`; `regress.py` does this for every sample script). Nothing depends on the host
clock or on uninitialised host memory: the guest RAM starts zeroed, the game's `rand()`
(`8C10D334`, state `D_8C5FBB08`) is in guest RAM, the frame timer is HLE'd to 0.

Checked equivalences (Emerald Coast act 1, 181-3334 steps): with and without the display pass
(`--disp`): identical **for the Sonic Emerald Coast scripts only** - Amy's display advances her
hammer morph (`pwp+0x98`) on every call, so with `--disp` `amy_tp2` differs from step 204 and
`obj_tp2_amy` from step 156 (`docs/research/visibility_fidelity.md` 2); `--disp` is not hardware
behaviour (the game runs the display pass only while paused or frozen). With `--spf 1` (one logic
step per display frame, `B_8C754EC8` always 0, the setting `critique.md` 4.1 proposes for the port)
the decoded player fields of those scripts are identical, only `frame`/`sub` differ.

## Input scripts

One segment per line (or `;`-separated on the command line): `STEPS X Y [BUTTONS]`

* `STEPS`: number of 60 Hz logic steps the segment lasts;
* `X Y`: analog stick, -128..127 (`Y < 0` = up = "away from the camera");
* `BUTTONS`: `A B C D X Y Z START UP DOWN LEFT RIGHT L R` joined by `+`, or a hex mask
  (Katana `PDD_DGT_*` bits: A `0x4`, B `0x2`, X `0x400`, Y `0x200`, START `0x8`);
* `#` comments; `STEPS:X,Y,BUTTONS` works too. After the last segment the pad is neutral.

A button held for one step is a tap; "press" edges are computed per step like the maple poll
(once per vblank) does. Samples: `harness/tests/sim/*.txt` (stand, run + jump, jump hold, spin
dash, circle), all for `--level 1 --act 0 --no-objects`.

The stick is camera-relative: the camera task runs, so "forward" follows the camera. The trace
records the result (`analog_ang`, `analog_stroke`), which is what the player code reads.

## Options

| Option | |
|---|---|
| `--level N --act N --chr N` | stage and character (defaults 1, 0, 0). Characters: 0 Sonic, 2 Tails, 3 Knuckles, 5 Amy, 6 E-102, 7 Big; the start position comes from the character's start table, a missing entry gives (0,0,0) |
| `--frames N` / `--steps N` | length (default: the script's length) |
| `--input "..."` / `--input @file` | input script |
| `--out PREFIX` | `PREFIX.bin` + `PREFIX.csv` (default `build/traces/L<level>A<act>C<chr>_<script>`) |
| `--trace-v1` / `--trace-v2` | trace version written (default 2; v1 = the 972-byte records of before, byte-identical to old recordings) |
| `--check REF.bin` / `--check-decoded REF.bin` | compare with an earlier trace (all bytes but `icount` / only the decoded fields: without the raw blobs, XMTRX, the camera blob and the blob addresses); a version-1 reference is compared with the v1 part of the records; exit status 1 on a difference |
| `--no-objects` | the SET files are served empty: no rings, springs, enemies (level code tasks still run) |
| `--spf N` | logic steps per display frame (game: 2) |
| `--disp` | also run the display pass each frame (not hardware behaviour, see Determinism) |
| `--no-screen` | leave `njInitSystem` a no-op: the zero screen of the references before 2026-10-20 (`<name>.noscreen.bin`); header flag 8 clear. `--screen` (the default since then) is still accepted |
| `--log-vis FILE` | one line per call of the sphere visibility test `8C038D00`: phase, step, sub step, caller (PR), x y z r, the view-space centre `XMTRX * (x, y, z, 1)`, the result; the screen words whenever they change |
| `--vis-force 0\|1\|sub0`, `--vis-only PR,..` | experiments: answer the visibility test instead of running it (always 0, always 1, 1 on sub step 0 only), or run it only for these return addresses (0 for the others) |
| `--route`, `--route-all`, `--route-only F,..`, `--route-except F,..` | run natively compiled decompiled C instead of the originals (during the frames, or from the start); `F` = name, address or `code_XXXXXXXX.c` |
| `--dump-land PREFIX` | the act's collision geometry as the collision cache build emits it: `PREFIX.tri` + `PREFIX.col` (version 2) + `PREFIX.obj` (below) |
| `--dump-ram FILE` | 16 MB guest RAM image (`0x8C000000-0x8CFFFFFF`) after the stage start |
| `--dump-ram-at N[,N..][,end]` | 16 MB RAM image at the start of logic step N (before it runs), `end` = after the last step: `<out>.stepN.ram`, `<out>.end.ram` |
| `--log-sdk FILE` [`--sdk-list FILE`] | every call from game code into the SDK / C runtime, one text line each (below) |
| `--dump-world FILE` [`--world-syms FILE`] [`--world-all`] | the game's non-pointer globals after the stage start and their changes after every step (below) |
| `--census FILE` | bytes and 4 KB blocks the game writes during setup and during the steps (below; pure runs) |
| `--coverage FILE` | the functions whose entry ran in the interpreter during the logic steps |
| `--discover`, `-v`, `--print N`, `--fast` | auto-stub unknown faults; list files read; progress every N steps; no shadow call stack |

## Trace format

`harness/src/sa1_trace.h` (C, also the format a libsa1 test can write); `harness/tools/sa1trace.py`
reads versions 1 and 2. Header: magic `SA1TRC01`, version (2), record size, level/act/character,
steps per frame, flags (1 display pass / 2 no objects / 4 routed / 8 screen), CRC-32 of the files `baserom/1ST_READ.BIN`
and `baserom/overlays/STGnn.BIN` (the decompressed stage image), landtable address, start position.
One record per logic step (version 2: 1,188 bytes = the 972-byte version-1 record, unchanged, plus
the v2 block described in "Trace version 2" below):

* `step`, `frame`, `sub` (= `B_8C754EC8`), `icount`;
* input: `stick_x/y`, `buttons` (given), `pad_on/pad_press` (`B_8C754C08[0]` after the pad update),
  `analog_ang/analog_stroke` (`B_8C754DCC[0]`, camera-relative, what `GetAnalog 8C06CD22` returns);
* decoded state: taskwk `mode smode id btimer flag wtimer ang pos`, motionwk `spd acc ang_aim`,
  playerwk `spd acc floor_normal dotp attr last_attr flag jumptimer nocontimer waittimer
  mj.reqaction/action/lastaction/nextaction/nframe equipment item`, `land_count` (`B_8C754EF0`),
  `rand_state` (`D_8C5FBB08`);
* raw DC-layout copies of taskwk (0x40), motionwk (0x40), playerwk (0x200) and csts (0x84)
  (`include/task.h`, `include/sa1/player.h`, `include/sa1/collision.h`; pointers inside are guest
  addresses).

The CSV has the decoded fields with `%.9g` floats (round-trip exact) and hex angles/flags.

`sa1trace.py info|dump|diff|script`: `diff` compares two traces field by field, floats in ulps
(`--ulps N`, default 0) or absolute (`--abs X`), angles modulo 0x10000, and prints the first step at
which each field diverges and the maximum error; `script` turns a trace back into an input script.

## How a libsa1 test replays a trace

1. **Same geometry.** The header's `stage_crc32` identifies the stage image
   (`baserom/overlays/STG01.BIN` = the decompressed `stg01.PRS` the game loads at `0x0C900000`) and
   `landtable` the act's landtable in it. After the stage load 1,286 bytes of the image in RAM differ
   from the file on Emerald Coast: texture lists that the texture loader fills in; no COL entry and
   no model is changed (the sphere init `8C052BA0` leaves this stage's entries as they are), so the
   file is the collision data. `--dump-ram FILE` writes the whole 16 MB of guest RAM after the
   stage start for anything else a test wants to start from. libsa1's landtable path deserialises that landtable (COL list at `+0x0C`, 0x24
   per entry: sphere, `NJS_OBJECT*` at `+0x18`, flags at `+0x20`) from the user's file. For a test
   of a host-geometry path, `--dump-land PREFIX` writes the act's polygons as `PREFIX.tri`,
   their COL entries as `PREFIX.col` (version 2, see "Landtable dump version 2" below) and
   `PREFIX.obj` (collidable entries only). The vertices are transformed by the game's own Ninja
   calls in the interpreter exactly as the collision cache build `8C028EC2` / `8C028B00` does;
   Emerald Coast act 1: 445 COL entries, 151 collidable (13,701 triangles), 27,132 triangles; the
   polygon under the start point is at y = 4.0 with flags `A0100001`, the `attr` the player code
   reports when standing there.
2. **Same start.** Create the character at `header.start_pos/start_ang` (the record of step 0 shows
   the state after the first step; the raw blobs of any record can seed a mid-run start).
3. **Same input, at the player-code level.** For each record `i`: set the port-0 pad record
   (`on = pad_on`, `press = pad_press`) and the player-0 analog (`ang = analog_ang`,
   `stroke = analog_stroke`) - this bypasses the camera, which libsa1 does not have - and
   `B_8C754EC8 = sub`; seed the game RNG with the record's `px_rand` (version 2: the rand state at
   the entry of the player's exec; the previous record's `rand_state` is the same value except
   where a task that runs before the player draws from `rand()`, e.g. on Speed Highway) and, for a
   reseeded step, the camera taskwk (`px_cam_raw`) and XMTRX (`px_xmtrx`).
4. **One tick** = one `8C04E714` restricted to the player task (and the land collision it uses).
5. **Compare** the state with record `i` (`sa1trace.py diff` if libsa1 writes the same format, or the
   same comparison in the test). Exactness to expect: bit-exact where the code is matched C
   compiled with the right FP contraction (see findings), a few ulps otherwise; diverging control
   flow (mode/action) is a real bug.

Traces with `--no-objects` are the ones to use for the character alone; with objects, springs, dash
panels, enemies and rings act on the player through the CCL and the player API.

## Validating decompiled C inside the simulation

`trace --route --check REF.bin` runs the whole simulation with every natively compiled decompiled
function (`src/1st_read/*.c`, matching build) replacing its original, and compares with the pure
trace. `harness/tools/route_bisect.py -- <trace options>` bisects a difference down to the
function that causes the earliest divergence (`--route-only` on halves, ~10 runs, ~30 s):

```
py -3.12 harness/tools/route_bisect.py --exe harness/build/sa1h_nm.exe -- \
    --input "30 0 0; 90 0 -127; 1 0 -127 A; 60 0 -127"
...
culprit: 8C02D2E0 func_8C02D2E0 -> check: first difference at step 1: | step 1: pwp_raw+0x1E3: 00 (reference) vs 80 (now)
```

`--decoded` makes the bisector compare only the decoded player fields (`--check-decoded`), which
skips differences in fields the player physics does not use (e.g. the shadow work inside playerwk);
`--route-except` in the trace options excludes known culprits to find the next one.
`py -3.12 harness/tests/sim/regress.py --route` does the routed check for all sample scripts.

## Findings so far

* **Matching build, 528 routed functions (latest sources): identical traces** on all sample scripts
  and on a run with objects (icount aside).
* **FP contraction is per expression, not global.** `func_8C027360` (cross product of two triangle
  edges, used by the land collision) is `FMUL, FMUL, FSUB` in the original; compiled with the
  harness's `-mfma -ffp-contract=on`, clang fuses `a*b - c*d` into one FMA, and the floor normal
  differs by one ulp at step 95 of a run (bisected in 25 s). `PSetPosition` (`8C072B44`,
  NON_MATCHING) alone gives the same divergence; built with `--no-fma` both reproduce the reference
  exactly. Compiling everything without FMA instead diverges at step 4 (functions where SHC did
  emit `FMAC`). libsa1 needs `-ffp-contract=off` plus explicit fused operations where SHC used
  `FMAC` (or per-function flags). With 778 routed NON_MATCHING functions the decoded player fields
  are otherwise identical to the reference for 181 steps.
* NON_MATCHING build (`--non-matching`): `func_8C02D2E0` and `func_8C02D410` (shadow/ground probes,
  `code_8C02D060.c`) write different shadow fields than the original (`playerwk.shadow.angz`
  `0x80000000` vs 0, `Attr_bottom`); `func_8C02D278` is a register-convention helper (vector in/out
  in `FR4-FR6`, `FIPR`/`FSRRA`) declared as `void (NJS_VECTOR *)`, so its routed adapter is wrong and
  guest callers loop (`diff` also reports 100 % mismatch).
* `include/sa1/engine.h` has `#define GetStageNumber GetStageNumber`, which silently undid
  `gen_native`'s per-file renames of conflicting prototypes (link error); `gen_native.py` now
  re-applies the renames after the last `#include`.
* Sonic's start on Emerald Coast act 1 is (-9, 16, 4), facing `0x6000`; he lands at step 19 on the
  deck at y 3.997648 (the polygon is at 4.0; collision keeps a small gap).

## Limits and what is not modelled

* No interrupts: the vblank handler and its per-stage callbacks (display/fog only) do not run; the
  frame timer says "no time passed", so there are never catch-up steps (on hardware a slow frame
  adds a third step).
* The in-game state machine `8C04CA40` is not run: no fade-in (on hardware the pad is ignored during
  the fade; give neutral input for the first steps), no pause, no death/restart, no act clear.
  The death-zone task and the game's event flags still run; a death sets the request
  (`DAT_8C19DE24`) but nothing acts on it.
* Rendering, sound and the voice/BGM streaming are stubbed or never reached; textures are loaded
  into RAM by the game's loaders, the Kamui texture upload is whatever the SDK code does without
  hardware (no fault).
* Float behaviour is the interpreter's (`harness/README.md`): exact for add/mul/div/sqrt/FMAC under
  RZ/DN, approximate for `FSCA`, `FSRRA`, `FIPR`, `FTRV` (hardware tables are not public). A trace
  is therefore exact w.r.t. the interpreter, and within the FSCA/FSRRA approximations of real
  hardware. A real-hardware or emulator recording would be needed to close that gap.
* Characters other than Sonic run (Tails, Knuckles: start tables have no Emerald Coast entry, so
  they start at (0,0,0) and fall to the ground there); their own stages are not checked yet.
* Speed: setup ~211 M instructions (about 1 s); 0.2-0.6 M instructions per logic step, about
  1-5 ms per step on one core (a minute of gameplay with objects: ~25 s).
* Native code has no instruction budget: with `--route`, a decompiled function that loops forever
  hangs the run (the interpreter's budget only stops original code). `diff all` has a per-file
  wall-clock limit for the same reason (`--child-timeout`).

## libsa1 configuration and the verification loop (2026-10-19)

* `py -3.12 harness/build.py --non-matching --pc` builds `harness/build/sa1h_nm_pc.exe` with the decompiled code
  compiled exactly as libsa1 compiles it: `-DTARGET_PC` (fixed-size types, PC-only code paths), no FP contraction
  (`-mfma` only for the explicit `SA1_FMAC`/`SA1_FMAC_ACC`). This is the build that has to reproduce the traces.
  `--tag T` gives a private build (`harness/build/sa1h_T.exe`, own generated/object directories), so several builds can
  run at once; `--files src/1st_read/F.c` makes only that file native (everything else runs as original code).
* Sample scripts now cover every playable character on one of their own stages (first line `# args:`):
  `tails_sh1` (Speed Highway 1), `knux_rm3` (Red Mountain 3), `amy_tp2` (Twinkle Park 2), `gamma_fe1` (Final Egg 1),
  `big_ec3` (Emerald Coast 3), plus the five Sonic `ec1_*` scripts.
* `trace --coverage FILE` writes the functions whose entry executed during the logic steps (pure run).
  `build/verify/coverage/<script>.txt` holds them for each script.
* `py -3.12 harness/tools/route_matrix.py [--exe ...]` routes every native function ALONE into every script that
  executes it and records "ok" / first difference / fault (`build/verify/route_matrix*.json`).
* `py -3.12 tools/fmac_audit.py [file]`: FMAC count of the original vs fused operations of the PC build, per function.
* `py -3.12 tools/make_fix_units.py` turns the matrix + FMAC audit into units for `tools/workflows/verify_fix.js`.
* Routing respects `--route-only/--route-except` also for calls through function pointers (task exec functions):
  before, a natively running caller (e.g. the task executor) reached every native implementation.
* `gen_native.py`: the game's own `rand`/`memset`/`strlen` are renamed per file to thunks (they used to exclude the
  file), `__asm_stub_sink` and the PC runtime pieces (`sa1_tmu`, `njFraction`; `harness/src/pc_runtime.c`) are
  provided natively, overlay/data-file symbols are resolved (`build/<OVERLAY>.symbols.txt`,
  `config/asset_symbols.txt`, names ending in their address), declarations from host system headers are ignored, and
  the link uses a response file.

Result: all 10 scripts bit-identical with everything routed in the libsa1 configuration (see docs/fixlist.md).

## libsa1 verification outputs: trace v2 and friends (2026-10-20)

The harness additions of `docs/libsa1_integration.md` 9.2 (Phase 0 item 6). Everything below is in
`harness/src/t_sim.c` / `sa1_trace.h` (`trace` options) and in `harness/tools/` (`sa1trace.py`,
`sdk_boundary.py`, `world_syms.py`, `write_census.py`).

### Trace version 2

`trace` writes version 2 (`--trace-v1` still writes version 1: byte-identical to the old recordings,
instruction counts included). A v2 record is the v1 record, unchanged, followed by:

| Field | Meaning |
|---|---|
| `px_exec`, `px_calls` | the hooked exec address of the player-0 task and how often it was entered in this step (1 in every step of the 15 references; 0 = not seen, the `px_*` fields are then 0) |
| `px_rand` | `D_8C5FBB08` (rand state) at the entry of the player's exec |
| `px_frame_counter`, `px_rings`, `px_gravity[3]` | `B_8C752BE8`, `B_8C752BE4` (Sint16), `B_8C754BDC` at the same moment |
| `px_xmtrx[16]` | XMTRX (the FPU's XF bank, bit patterns, `m[k] = XF[k]` as libsa1's `SA1_XMTRX`) at the same moment |
| `px_cam_twp`, `px_cam_raw[0x40]` | `D_8C18B7EC` (camera taskwk) and its DC-layout bytes at the same moment |
| `frame_counter`, `rings`, `gravity[3]` | the same globals after the step, like the v1 fields |
| `ptp_addr`, `twp_addr`, `mwp_addr`, `pwp_addr`, `csts_addr` | guest addresses of the player task and of the four blobs of this record |

"Player-exec entry" is the state the player code starts from: the task executor runs the camera and
the level tasks first, and they can draw from `rand()` and leave XMTRX behind. The capture is a hook
on the six character mains (SonicMain `8C0A77BC`, TailsMain `8C0B6148`, KnucklesMain `8C0C0640`,
AmyMain `8C0D6B40`, E102Main `8C0CBE00`, BigMain `8C0DFFA0`, i.e. the execs `LoadPlayerTask
8C04F3C0` creates) and on whatever `playertp[0]->exec` (`B_8C78C648[0]` +0x10) points to, re-armed at
every step. It fires on the first entry whose R4 is `playertp[0]`, or, before the main's first run
(PInitialize fills `playertp[]` during step 0), the task whose taskwk has player number
`counter.b[0] == 0`. The hook then runs the exec unchanged: the original in the interpreter with the
registers as they were (the hook steps aside for the call), or, when native routing had a hook at
that address, that hook, i.e. the routed native exec. A native exec called through a host function
pointer by native code (possible only with `--route-all`) is not seen (`px_calls` 0).

Observed in the 15 references: `px_rand` equals the previous record's `rand_state` everywhere except
`tails_sh1` (413 of 414 steps: a Speed Highway task draws before the player); the frame counter
stays 0 (only the state machine `8C04CA40`, which the simulator does not run, advances it); rings
change in the `obj_*` scripts only; at step 0 the camera taskwk is still empty (mode 1, position
(0,0,0)), its first exec comes after the player's.

`--check REF.bin` compares every byte but `icount`, with a v2 reference the v2 block too; a v1
reference is compared with the v1 part. `--check-decoded` leaves out the raw blobs, XMTRX, the
camera blob and the blob addresses (heap addresses differ between worlds; XMTRX and the camera are
inputs a libsa1 world injects).

**References.** `build/traces/ref/<script>.bin` are version 2 (re-recorded 2026-10-20 with a pure
build; `<script>.v1.bin` / `.v1.csv` are the version-1 files they replaced). `regress.py`:

```
py -3.12 harness/tests/sim/regress.py --exe EXE [--route] [--v1-compat] [--only a,b] [--refdir D] [--outdir D]
py -3.12 harness/tests/sim/regress.py --exe EXE --update     # re-record; a v1 <name>.bin is kept as <name>.v1.bin
```

`--v1-compat` also checks that the v1 part of every new trace equals `<name>.v1.bin` (`sa1trace.py
compat`: decoded fields byte-identical, raw blobs reported). A build whose `trace` only writes
version 1 (any exe built before this change, e.g. the shared `sa1h_nm_pc.exe`) is checked against
`<name>.v1.bin` automatically (`sa1trace.reference_for`, also used by `route_matrix.py`); a direct
`trace --check build/traces/ref/X.bin` with such an exe fails with "not a version-1 trace" - use
`X.v1.bin` or rebuild.

Verified on all 15 scripts (the 10 of the design plus the five `obj_*`): the v1 part of each v2
recording is byte-identical to the v1 reference, raw blobs included (`regress.py --v1-compat`);
pure runs are deterministic over the whole v2 record; a private `--non-matching --pc --rt` build with
everything routed (1,570 native functions, runtime included) reproduces all 15 v2 references
including the v2 block (`regress.py --route`), and so does a `--non-matching --pc` build (1,481
native functions; `regress.py --route --v1-compat`).

### `sa1trace.py`

| Command | |
|---|---|
| `info T` | header; for v2 the exec address(es) seen and the steps without one |
| `dump T [--from N --to N --fields ...]` | decoded fields per step (v2 fields by name: `px_rand`, `rings`, ...) |
| `diff REF OTHER [--ulps N / --abs X] [--raw [--heap-by-structure] [--no-phys]]` | decoded fields (v1, and the v2 scalars when both are v2); `--raw` adds the raw blobs, the camera blob, XMTRX and the blob addresses |
| `compat V1.bin V2.bin` | the v1 part of a v2 trace against a v1 trace |
| `land PREFIX [--col N]` | a `--dump-land` output (v1 or v2) |
| `landcheck PREFIX RAM` | the collision cache (colobj/COLPOLY) of a RAM image against a v2 dump |
| `world A B` | first difference of two `--dump-world` files |

Pointer words in the raw blobs (`twp` +0x38/+0x3C, `mwp` +0x00, `pwp` +0x64..+0x78 and
`mj.spdp/workp/plactptr/pljvptr/actwkptr` +0x138..+0x148; union words such as `twp.value` or
`pwp.free.ptr[0]` when both sides hold a guest RAM address) are compared by physical address
(`& 0x1FFFFFFF`, so P0/P1/P2 aliases are equal). `--heap-by-structure` maps a pointer into one of the
record's own blobs to `twp+0x20`, `pwp+0x38`, ... and any other pointer into the game heap
(`B_8C7EE8E0`, 0x90010 bytes) to `heap`: two runs whose heap blocks sit at other addresses compare
equal when the pointers mean the same thing (checked with a copy of `ec1_run_jump` whose 4,064 heap
pointer words were moved by 0x100: `--raw` reports them, `--raw --heap-by-structure` is clean).

### Landtable dump version 2 (`--dump-land PREFIX`)

The triangles exactly as the collision cache build puts them into the COLPOLY pool: for each COL
(landtable order) `8C028EC2` -> `8C028B00` walks the entry's NJS_OBJECT, children first, then the
node, under `unit * T(pos) * Rz * Ry * Rx * S(scl)` per node (angles skipped when 0; the evalflags
are ignored, unlike the probes); `8C0287A0` emits, per meshset and polygon/strip, triangle i as
(v[i], v[i+1], v[i+2]) when `(flip + i) & 1 == 0`, else (v[i], v[i+2], v[i+1]) (`flip` = strip
header bit 0x8000; quads are 4-vertex polygons; strips over 0x400 vertices are cut like the
original). The SDK matrix code runs in the interpreter, so the world vertices are bit-exact.

* `PREFIX.tri`: header `{"LAND", 2, ntri, ncol, tri_size 108, col_size 136, landtable, nnodes}`, then
  per triangle: world `v[9]`, COL flags, COL index, model-space `lv[9]`, material `attrflags`
  (`COLPOLY.attr`; 0 without materials), emission index within the COL, node object address, node
  index (walk order), meshset, polygon, triangle-in-polygon, the three point indices, meshset type.
* `PREFIX.col`: header `{"LCOL", 2, ncol, 136, landtable, ntri}`, then per COL: index, flags,
  center, radius (`xWidth`), `yWidth`, `zWidth`, blockbit, object address, evalflags, pos, ang, scl,
  model address, model center and r, the world centre `M * model->center` as 8C028EC2 computes
  `colobj.center`, the triangle range, node count, point / meshset / material counts.
* `PREFIX.obj`: collidable entries (`flags & 0x400003`) for viewing.

Checked against the game (`sa1trace.py landcheck PREFIX <out>.end.ram`): at the end of the runs on
the six stages of the sample scripts (EC1, EC3, SH1, RM3, TP2, FE1) the 108 landtable objects in the
collision cache (10,915 polygons) have exactly the dump's triangles (vertex bits and `attr`, as
sets: the pool is sorted by z) and `colobj.center == world_center`. Emerald Coast act 1: 445 COLs,
all single-node, all meshsets strips (0xC000), evalflags 0x14 / 0x16 / 0x17; 27,132 triangles,
13,701 of them collidable. No COL of the six stages has child nodes; EC3 has 4 quad triangles.

### RAM images (`--dump-ram-at`)

`--dump-ram-at 0,30,150,end` writes `<out>.step0.ram`, ... : the 16 MB at the start of step N, after
step N-1 and the frame code between them (`end`: after the last step). `step0` differs from
`--dump-ram` (after the stage start) only in `B_8C754EC0` (the harness clears it before each frame)
and the frame function's guest stack frame (0x8C00F3xx). M1a's `build/traces/ec1_start.ram`
(2026-10-06) is byte-identical to a fresh `--dump-ram` of the EC1 scripts (`--level 1 --act 0
--no-objects`; the setup does not depend on the input), so the five EC1 references start from it.

### SDK call log (`--log-sdk FILE`)

One line per call from game code into the SDK or the C runtime (libsa1's `SA1_DEBUG_SDKLOG` writes
the same lines, so the first differing line is the first diverging Ninja call):

```
# <phase> <step> <seq> <ADDR> <name>(<args>) -> <ret>
S 0 10 8C639C70 njPushMatrix(0) -> 1
S 0 15 8C10D0F8 atan2f(f00000000,f3F7FFFFE) -> f00000000
F 12 3 8C601874 pdGetPeripheral(0) -> 8C000400
```

* phase `S` = inside logic step `<step>`, `F` = frame code after step `<step>` (before the next
  one; -1 before step 0), `D` = display pass (`--disp`); `<seq>` counts from 0 per phase and step;
* arguments per the function's signature: integers and pointers as `%X` (after the signature's
  8/16-bit extension), floats as `f` + the 8 hex digits of their bits; return `-` for void; an
  unknown signature prints `r4=..,r5=..,r6=..,r7=..,fr4=f..,..,fr7=f..` and `r0=..,fr0=f..`;
* only boundary crossings: SDK code calling SDK code and game callbacks run by the SDK are not
  logged; neither are calls from natively compiled game code straight into natively compiled
  runtime code (`--rt` builds with `--route`); thunk calls into the interpreter are.

The functions and signatures come from `build/verify/sdk_boundary.txt`, written by
`py -3.12 harness/tools/sdk_boundary.py`: every 1ST_READ entry in the SDK area (8C5FBB60-8C67C680)
or the C runtime (8C10C10C-8C111194) that game code of 1ST_READ or an overlay calls or whose address
it loads (304 functions, 132 with a signature from the harness builds' thunk/native tables; the
register-convention helpers `_divls/_divlu/_modls/_modlu/__quick_mvn`, 8C10CA5C-8C10D008, are left
out: libsa1 inlines them). Without the file, `--log-sdk` hooks the SDK functions of the build's own
thunk/native tables. The hooks chain to HLE stubs and routed native code; a log run reproduces the
reference trace, and the pure logs of two different builds are identical (`ec1_run_jump`: 505,680
calls, 27 MB; the same run fully routed in the `--rt` build logs 56,855). Steps past the requested
count (the last frame may run one more step) are not logged.

### World dump (`--dump-world FILE`)

The bytes of the game's globals: all of them after the stage start (`@start`) and, after every
logic step, the runs that changed (`@step N`):

```
@step 30
D_8C5FBB08+0 ACD96359
B_8C6BACF0+38 049F14BDEEB222B7681A0A3D
```

Rule (libsa1 writes the same): per symbol in address order, each maximal run of changed bytes as
`<name>+<offset hex> <bytes hex>`, at most 32 bytes per line. The symbols come from
`build/verify/world_syms.txt` (`py -3.12 harness/tools/world_syms.py`: every data symbol of
1ST_READ from `build/1ST_READ.symbols.txt`, size = distance to the next symbol, class from the C
declarations: `v` no pointers, `p` pointer, `m` struct/union with pointers, `u` unknown; the heap
arena and the NJS_OBJECT pool `B_8C75D310` are forced to `m`; "referenced" = declared by the
decompiled C or in the data list of a harness build's native manifest, `sh4_native_datas`): by
default the referenced ones of class `v` or `u` (548 symbols, 197,741 bytes), `--world-all` all of
them, `--world-syms FILE` another list. Without the file the data symbols of the build's own native
manifest are used. `sa1trace.py world A B` replays both files and names the first block and the
symbols that differ. The classes are a heuristic until pclower's `sa1g_symtab.h` exists (design
2.4): a symbol declared as raw bytes that holds pointers shows up as a difference.

`ec1_run_jump`, pure vs fully routed (`--non-matching --pc --rt`): the 272 blocks are identical
except `D_8C67348C` (from step 2 on: 00001801 pure, FFFFFFFF routed), the texture-key cache of the
current texture (`rt_texture.c`): the original draw functions update it, the runtime's draws
(`rt_draw.c`, no-ops) do not; routing everything but `rt_draw.c` makes the dumps identical. Render
state only; the traces are identical.

### Write census (`harness/tools/write_census.py`)

`py -3.12 harness/tools/write_census.py --exe <pure build>` runs every script with `trace --census
build/verify/census/<script>.txt` and writes `build/verify/write_census.txt` / `.json`. The harness
compares the RAM after the stage start with the RAM after the ROM load (setup) and, at every step,
the 4 KB blocks the interpreter dirtied (`sh4_ram_dirty`) with the previous step (steps), so a value
written and later restored still counts; it records the changed byte ranges and the written blocks
(also writes of unchanged values: the copy-on-write granularity). Native code does not mark blocks
dirty: take the census from pure runs. Not with `--discover`.

Result over the 15 scripts (2026-10-20): per script 1,432-1,640 blocks written during setup, 112-255
during the steps, 1,474-1,682 in total (5.8-6.6 MB), 35-63 blocks written only during the steps;
1,856 blocks (7.25 MB) over all scripts. Setup is dominated by the overlay area (stage, motions,
textures: up to 1,172 blocks); during the steps the BSS (up to 119 blocks / 273 KB changed), the
overlay area (61 / 222 KB, texture and object state), the heap (24 / 80 KB) and the SDK BSS after
the heap (32 / 9 KB) change. 591 symbols change during the steps; the list (with class and the
scripts that change them) is in `write_census.txt`.

## The screen (2026-10-20)

`docs/research/visibility_fidelity.md` found that the simulator's stub list turned Ninja's
`njInitSystem 8C604702` into a no-op labelled "vsync wait". Without it the screen size, the aspect, the
2D clip rectangle and the projection distance the game derives from the width stay 0, so the game's
sphere visibility test `8C038D00` never passes and `njProjectScreen` maps every point to the screen
centre. On a Dreamcast the player is on screen: the test passes on every sub step 0, and the visible
paths of Tails' (`8C0BA820`) and Knuckles' (`8C0C46AC`) displays carry logic (the collision-shape-0
centre, Tails' tail morph and flight actions 38 / 39, counters), as do some object displays (Kiki
`8C0FC160` draws from `rand()`).

Since 2026-10-20 the screen is set up by default (HLE table above) and every reference has it:

* `trace` sets the screen unless `--no-screen`; header flag bit 3 (8) marks it (`sa1_trace.h`).
* `regress.py --update` (private pure build `build.py --files none --tag vis`) re-recorded all 15
  references; they are byte-identical to the candidates of `build/traces/ref_disp/`. The references
  before that are kept as `build/traces/ref/<name>.noscreen.bin` / `.csv` (`regress.py` moves a version-2
  reference without flag 8 there before recording; `regress.py --ref-suffix .noscreen` checks them,
  running the build with `--no-screen`). 10 scripts are identical record for record to their old
  references; `tails_sh1`, `knux_rm3` (raw blobs only), `obj_ec1_sonic`, `obj_ec3_big` and `obj_rm3_knux`
  differ (`--v1-compat`: their `*.v1.bin` no longer match; `sa1trace.py compat` ignores flag 8).
* `regress.py` runs the build in each reference's screen mode (flag 8 -> the screen, `--screen` for
  the 2026-10-20 experiment builds; no flag 8 -> `--no-screen`); `--extra "OPTS"` appends options to
  every script's command, also to the scripts with a `# args:` line (whose line replaces `--args`).
  `route_matrix.py` follows the reference's flag too.
* Coverage (`build/verify/coverage/*.txt`, pure runs; the old files are in `coverage_noscreen/`): the
  draw paths now run - the game's Ninja motion player copy `8C03FE1A`-`8C040942`, the palette bank
  `8C038CEC`, Tails' flight-tail spin `8C0BDB72` and the morph `8C055ECE`, object displays and their
  SDK draws; `8C03700C` / `8C037098` / `8C03718C` no longer execute (HLE above).
* Routed checks (`build.py --non-matching --pc`): the hand-written sprite module is reached now
  (`8C09E778` -> `8C09E880` / `8C09EB08` -> `8C09EE44`); its C descriptions take the store-queue cursor
  in R2 and have no C caller, so an ABI adapter hands them garbage (the first routed run faulted at
  `8C09E7D0`). `gen_native.py` / `gen_ga.py` give the functions of `NO_ROUTE` no adapter: they are
  never routed. After that `regress.py --route` is 15/15 identical against the new references (the display
  paths that never ran before, Tails' `8C0BA820` / `8C0BDB72`, the morph `8C055ECE`, the Ninja motion player copy,
  the object displays, all match), and so is the `--non-matching --pc --rt` build, whose runtime has the exact
  visibility test (`libsa1/src/rt/rt_draw.c` section 6: `sa1h diff func_8C038D00 --ulps 0` PASS on 2,000 cold,
  2,000 warm and 20,000 inputs; routed, it answers the ~140,000 calls of the 15 scripts as the original does).
* `route_matrix.py` (`build/verify/route_matrix_screen.json` / `.txt`, coverage-filtered: 3,403 pairs of 1,559
  native functions): only the known register-convention helper `8C02D410` breaks a trace (step 1 in every script
  that runs it, as before). No decompiled function had to change; the SHC build stays byte-identical.
* Write census re-taken (`build/verify/write_census.txt`; before: `write_census_noscreen.txt`): 1,865 blocks over
  all scripts (1,856 before), 430 written during the steps (the draw paths), 663 symbols change during the steps
  (591 before).
* Other derived data re-taken with the screen: `build/traces/m1_ec1_stand.*.ram`, `m1_ec1_run_jump.*.ram`
  (libsa1's transplant and exit-3 tests; the zero-screen images are in `build/traces/m1_noscreen/`) and
  `build/traces/capture_harness/` (libsa1's `capture_test --harness`: ec1_run_jump / ec1_jump_hold / ec1_spindash
  with `--log-sdk` of the event stubs and `--dump-ram-at`).
* libsa1 follows (its `README.md`, "The screen and the visibility test"): the boot sets this screen, the runtime's
  `8C038D00` is the original's test, and the ten traces replay strictly against the new references.
