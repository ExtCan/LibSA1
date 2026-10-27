# G-SA1 1.x design: triggers, cinematic cameras and splines (`v1_triggers`)

Research and design for the owner's 1.0 roadmap item "Triggers such as camera switching for cinematic gameplay,
automated spline (with a tool to create them point by point, with separate spline IDs so they don't mess with each
other) for scripted events", plus the owner's Board On / Board Off boxes (plan 14.1c). Read-only research: nothing in
SADC/src, LIB or g-sa1 was changed. Scratch scripts and outputs: `SADC/build/research/triggers/` (section 10).
Abbreviations: SADC = `sadc`, LIB = `libsa1`, "step" = one 60 Hz logic step, BAMS = 16-bit angle (0x10000 = 360 deg),
SA1 units (1 SA1 unit = 4 Hammer units, `GSA1.SCALE`). Addresses are 1ST_READ unless an overlay is named. Status
figures come from `build/research/triggers/status.py`, which reuses `tools/progress.py`'s rules (a function counts when
a C definition exists in `src/`; NON_MATCHING-only definitions are listed separately).

---------------------------------------------------------------------------------------------------------------------

## 0. Summary

**The game's camera** (`8C019360-8C025960`, 185 functions, 49,070 bytes, **0 % decompiled**) is a camera task
(`8C0197BA`, created by `8C01957E` from the stage start `8C04E660`) driven by a **mode table of 76 modes**
(`8C195310`, 16 bytes each; the game's own debug names at `8C18B898`: FOLLOW, KNUCKLES, MAGONOTE, SONIC, ASHLAND,
FIXED, KLAMATH, LINE, POINT, NEWFOLLOW, PATHCAM, SURVEY, ...), a **transition ("adjust") table of 27 entries**
(`8C1957D0`, NONE, NORMAL(_S), SLOW(_S), TIME, THREE1..5(C), RELATIVE1..6(C); only 6 distinct functions on DC) and a
**priority stack of camera levels** with the game's own names NORMAL / AREA / EVENT / COMP / COLI / DEBUG. Stages switch
cameras with **trigger volumes from the per-act `CAM*.BIN` files** on the disc: 101 files, **4,894 entries of 0x40
bytes**, each a sphere, plane or block (the game's `CAMCOL_SPHERE / PLANE / BLOCK`) that carries a camera mode,
priority, transition and two points + angles + distance. Objects and the characters push **event cameras** through
`8C01AD2C` / `8C01ABDC` and release them with `8C01AE60`. The format of these volumes is exactly what a GMod camera
trigger needs, so the design keeps it: **a G-SA1 camera trigger is a CAM entry**, and the camera code that runs it
should be the game's own, decompiled into the local character's libsa1 world (92 functions / 26,848 bytes of the camera
plus 600 bytes of landtable code and 9 small Ninja camera functions). Today libsa1 replaces the camera by an empty
adapter task and stubs the 12 camera calls of the characters; G-SA1 draws its own chase camera.

**The path system is already fully decompiled.** The path library (`8C089180-8C089F20`, 17/17 functions), the
player's path physics (`8C06F530`, `8C078308`, `8C0784F8`, `8C078710`), the player API orders that put a character
on a path (`8C07BFFA`, `8C07C02E`, `RunAlongPathP 8C07C0A0`, `RunWithSeeingPathP 8C07C0E4`, `HangDownFromPathP
8C07C120`), the cutscene path player (`8C04B7BA` / `8C04B9E0`), the cutscene "walk to point" locks (`8C07C1B4..8C07C270`
+ `8C06EAC0`) and the cutscene animation queue (`8C04B4D2` / `8C04B528` / `8C04B57C`) are all matched C. Only the stage
path-work objects (loops, hang bars, path cameras: `8C06AF00-8C06C0C0`, 15 functions / 4,436 bytes) are still
assembly. A path is a polyline (`PATHTAG` 16 bytes + `PATHTBL` 20 bytes per point: up-vector angles, length to the next
point, position); the stages hold 87 of them with 8,335 points (measured). So **G-SA1 splines can be driven entirely by
the game's own code today**: G-SA1 samples a node spline into the game's path format, libsa1 writes it into the world
and posts the game's order. Six ways to ride, all game code: steer (`RunAlongPathP`, normal physics), path run / path
roll (orders 1 / 2), hang and shimmy (orders 31 / 29), cutscene ride (event path with ground snap and a chosen action),
and physics drive (event lock 3 re-aimed along the spline).

**Design.** One entity family, `gsa1_trigger` (box / sphere / plane volumes = the CAM shapes), with actions Board On,
Board Off, Camera, Spline and Event, presets in the G-SA1 spawn tab, a "G-SA1 Trigger" tool, and a "G-SA1 Spline"
tool that places `gsa1_spline_node` entities point by point under a spline ID (IDs remapped on dupe paste so two
splines never share one). Camera triggers become the world's CAM table and the game's camera runs them (with a host
"chase camera" mode at level NORMAL so transitions into and out of trigger cameras use the game's own blends). Spline
and board triggers are evaluated by G-SA1 on the client (the character's world is client-side) and call new libsa1
path/board functions; event triggers fire server-side actions. Saves and dupes work through the entities' own
NetworkVars (spline points are node entities), with spline IDs and trigger names remapped on dupe paste.

**Recommended order:** T1 trigger family + Board/Event actions (no decompilation) -> T2 splines + rides (LIB path API,
no decompilation) -> T3 camera (decompile the camera subset, LIB camera API, CalcView) -> T4 loop/hang/path-camera game
objects (decompile `8C06AF00-8C06C0C0`). Until T3, G-SA1 should make GMod's own Camera tool work while a character is
active (CalcView must yield to `ply:GetViewEntity()`, plan 14.1e) instead of shipping a non-game stand-in camera.

---------------------------------------------------------------------------------------------------------------------

## 1. SA1's camera system

### 1.1 Where it lives and how much is decompiled

| Range | What | Functions | Bytes | Decompiled |
|---|---|---|---|---|
| `8C019360-8C025960` | camera: task, CAM volume scan, mode/adjust tables' functions, event API, debug display | 185 | 49,070 | **0** |
| `8C048B00-8C0491C0` | scripted camera helper (calls `8C01AD2C`) | 6 | 1,600 | 1 (18 B) |
| `8C0491C0-8C04B440` | event (cutscene) system incl. its camera calls (machinima, not this topic) | 106 | 7,702 | 1 (6 B) |
| `8C09CD6E` | CAM file name + load (`"cam" + LLAA + char letter + ".bin"`; the stage load `8C09CE90`, 0xAAC, also todo) | 1 | 118 | no |
| `8C0522C0` | landtable gather used by the camera's wall/ground collision (`8C01BD9E`) | 1 | 600 | **no** |

Functions of the camera that **other code calls** (externally called entry points, xrefs):

| Address | Size | Callers | Role (from the C-like reading below) |
|---|---|---|---|
| `8C01957E` | 0x96 | `8C04E660` | create the camera task: `CreateElementalTask(2, 0, 8C0197BA)`, globals, `njSetPerspective`-like `8C640500(0x31C7)` (0x31C7 BAMS = 70.0 deg) |
| `8C01976C` | 0x4E | 4 (act change) | reload the act's CAM table (`8C1958B4[level][act]`) |
| `8C01AAE0` | 0xFC | camera task | set the NORMAL (level 0) camera: mode index + adjust (SADX `CameraSetNormalCamera` analogue) |
| `8C01ABDC` | 0x11E | 3 in 1ST_READ (Knuckles `8C0C193A`/`8C0C450A`, Big `8C0E6586`) + 30 in overlays (bosses, stages, MINICART) | push an EVENT (level 2) camera by mode index + adjust |
| `8C01AD2C` | 0x100 | 19 in 1ST_READ (events, objects, game mode, Sonic, Tails, Big) + 25 in overlays | push an EVENT camera by **function pointer** + adjust + conversion kind (SADX `CameraSetEventCameraFunc`) |
| `8C01AE60` | 0xA6 | 23 in 1ST_READ (incl. Sonic's mode machine) + 24 in overlays | pop the EVENT camera (SADX `CameraReleaseEventCamera`) |
| `8C01B040` / `8C01B158` | 0x118 / 0xA2 | camera, Big's fishing | push / pop a COLI (level 4) camera (the player's free camera uses it) |
| `8C0213E0` | 0x36 | game mode, Tails | event camera with function `8C021432` (look-around camera on a path) |
| `8C024714` | 0x54 | `8C0617D0` (stage object) | RuinWaka1 camera helper |
| `8C01FB98`, `8C01FD6C` | 0x1D4, 0x170 | Sonic / Tails mode machines (as pointers) | board-trick event cameras (left / right orbit), they read the stage path (`8C0897F8`, `8C089A40`) |
| `8C021B60` | 0x9F2 | `8C06BAD2` (as pointer) | path-follow camera of the stage path-camera objects (1.8) |

### 1.2 Task, globals and work areas (read from the Ghidra export and the asm; field names are descriptive)

* **Task** `8C0197BA` (exec), disp `8C019768`, dest `8C019760`. twp mode 0 init, 1 first frame (`8C019400` aims the
  camera at player 0 + (0, 7.5, 0) and stores 50.0 at work +8, then `8C019F4A`, `8C01A100`), 2 per step:
  `8C01B500`, `8C01B554`, **`8C01995E` (CAM volume scan)**, `8C019F4A` (run mode + adjust), `8C01A100` (Ninja camera:
  `8C6404C8/8C64041C/8C64048C` roll/pitch/yaw, `8C64065C` translate, `8C640540` set), `8C01BD9E` (final view +
  camera collision against the land via `8C0522C0` / `8C02CA00`), `8C01B436`, `8C01C2D8`, `8C01A5AE` (debug: cycle the
  NORMAL mode with a debug button).
* **Globals** (BSS): `8C6B704C` level, `8C6B704E` act, `8C6B7050` = 0x4C (mode count), `8C6B7054` = `8C195310` (mode
  table), `8C6B7058` = 0x1B (adjust count), `8C6B705C` = `8C1957D0` (adjust table) - the four are copied from `8C1958A8`
  (`001B004C 8C195310 8C1957D0`); `8C6B7060` = the act's CAM file (Sint16 count at +0), `8C6B7064` = its entries
  (file + 0x40); `8C6B7068` = 0x31C7 perspective; `8C6B7080` / `8C6B7084` + `8C6B7088` = pending NORMAL mode / adjust.
* **Camera control work** `*(8C11204C)` (SADX `camcont_wp` analogue): +0 dirty, +4 Sint16 current CAM entry (-1 =
  none), +6 mode, +7 conversion kind (mode table byte 5: 1, 2 or 3 -> `8C01A718` / `8C01A680` / `8C01A60C`, which turn
  position + target into the camera taskwk's position and BAMS angles), +8 **level**, +9 adjust, +0xB flags (bit 1 =
  pushed by code, bit 0 = adjust has the "S" flag), +0xC mode function, +0x10 adjust function, +0x14 timer; then one
  saved slot per level: +0x18 (previous level), +0x30 (entry), +0x3C (flags), +0x44 (mode function), +0x5C (adjust
  function), +0x74 (mode), +0x7A (adjust).
* **Camera work** `*(8C112048)`: +0xC position, +0x18 target, +0x24/+0x28/+0x2C angles (the mode functions write it).
* **Camera parameters** `8C6B6FEC` (argument of every mode function; filled from the CAM entry by `8C01B308`):
  +0 / +2 Sint16 angles (entry +0x20 / +0x22), +4 point B (entry +0x30), +0x10 point A (entry +0x24), +0x1C distance
  (entry +0x3C), +0x20 timer.
* **Output**: the camera taskwk `D_8C18B7EC` (+0x14 ang x/y/z, +0x20 position), the view matrix, and Ninja's camera
  `8C788984`. 34 player functions read `D_8C18B7EC` (LIB integration 6.7); the stick conversion `8C05117E` turns the
  stick by the camera's ang.y, so in SA1 **the stick is relative to whatever camera is active**.

### 1.3 Levels (the priority stack)

Debug names at `8C18C5C8` ("NORMAL  CAMERA" ...) and `8C18B80C` ("NORMAL:" ...). A higher level masks a lower one; each
push saves the level it covers in that level's slot, each pop restores it.

| Level | Name | Set by | Popped by |
|---|---|---|---|
| 0 | NORMAL | `8C01AAE0(mode, adjust)`; the act's first CAM entry (entry 0) is the stage's default | - |
| 1 | AREA | CAM volumes whose mode has level 1 (the `A_` modes) | leaving the volume (`8C01A9B8`) |
| 2 | EVENT | `8C01ABDC` (mode) / `8C01AD2C` (function) from objects, events, characters | `8C01AE60` |
| 3 | COMP ("compulsory") | CAM volumes whose mode has level 3 (`C_` modes) | leaving the volume |
| 4 | COLI | `8C01B040` (the player's free camera SURVEY); COLI CAM entries (`COLLI`, `COL_` modes) are camera-collision volumes, not cameras (1.7) | `8C01B158` |
| 5 | DEBUG | EDITOR / GURIGURI debug modes | - |

### 1.4 The 76 modes (`8C195310`: {mode function, byte level, byte conversion kind, int "selectable as NORMAL", function 2})

The kind byte (1, 2 or 3) picks how `8C019F4A` turns the camera work into the camera taskwk (`8C01A718`, `8C01A680`
look-at from position and target, `8C01A60C`); the adjust (transition) is not in this table, it comes from the CAM
entry (+0x02) or from the caller of `8C01AAE0` / `8C01ABDC` / `8C01AD2C`. The int is 1 for the modes the debug cycle
`8C01A5AE` may pick as NORMAL. Function 2 is `8C01E92A` (copy the parameters' A and B points into the camera work)
for the A/B-point modes.
"CAM uses" counts entries in all 101 CAM files (1.6). Sizes from xrefs. Semantics: **R** = read from the code,
**M** = matches the measured EC1 trace (1.9), else UNVERIFIED (SADX name analogy only).

| # | Name | Function | Size | Level | Kind | CAM uses | What it does |
|---|---|---|---|---|---|---|---|
| 0-3 | FOLLOW, A_, C_, COL_ | `8C01E6A6` | 0xFE | 0/1/3/4 | 1 | 253 / 0 / 3 / 36 | follow camera (UNVERIFIED) |
| 4-7 | KNUCKLES, A_, C_, COL_ | `8C023360` | 0x5D6 | 0/1/3/4 | 2 | 538 / 70 / 20 / 73 | the general player-controllable area camera; also the fallback of ASHLAND/KLAMATH when the player turns the camera (R) |
| 8-11 | KNUCKLES2 ... | `8C023360` | | | 2 | 25 / 5 / 6 / 18 | same function (R), variant parameters |
| 12-15 | MAGONOTE ... | `8C01EB02` | 0x4FC | | 2 | 39 / 11 / 4 / 42 | Tails ("Miles") camera (UNVERIFIED) |
| 16-19 | SONIC ... | `8C023C5E` (2nd entry of `8C023C20`) | - | | 2 | 82 / 13 / 2 / 9 | UNVERIFIED |
| 20-22 | ASHLAND, A_, C_ | `8C020FE2` | 0x102 | 0/1/3 | 2 | 353 / 424 / 27 | **camera at point B, looking at the player + (0, 7, 0)** (R); the camera keys switch to `8C023360` until released, then it blends back with adjust 26 (R) |
| 23-25 | ASHLAND_I ... | `8C020FE2` | | | 2 | 115 / 134 / 5 | same without the player takeover, and the free camera is refused (R: `8C019ADC` excludes 0x17-0x19) |
| 26-27 | FISHING, A_ | `8C0239C2` | 0x108 | | 2 | 0 | Big's fishing |
| 28-30 | FIXED, A_, C_ | `8C024538` | 0x4A | 0/1/3 | 2 | 11 / 33 / 4 | **camera at point B looking at point A** (R); refuses the free camera (R) |
| 31-33 | KLAMATH, A_, C_ | `8C020D60` | 0x23C | 0/1/3 | 2 | 443 / 117 / 20 | **pivot camera: on the ray from point B through the player, at the entry's distance, raised** (R+M); falls back to `8C023360` on camera input (R) |
| 34-35 | LINE, A_ | `8C020C44` | 0xAA | 0/1 | 2 | 7 / 2 | camera on a line, from point A (UNVERIFIED) |
| 36-38 | NEWFOLLOW ... | `8C024586` | 0x164 | 0/1/3 | 3 | 1 / 0 / 0 | UNVERIFIED |
| 39-41 | POINT, A_, C_ | `8C01FF20` | 0x1A8 | 0/1/3 | 2 | 365 / 235 / 22 | camera behind the player on the line from point A through him, at the distance, looking at the player + 7.5 (R, partly) |
| 42-44 | SONIC_P ... | `8C023C20` | 0x1F4 | | 2 | 1 | UNVERIFIED |
| 45 | ADVERTISE | `8C01E800` | 0x30 | 2 | 1 | - | demo |
| 46, 47 | BACK, BACK2 | `0C900414`, `0C9001C0` | overlay | 2 | | - | stage-overlay cameras |
| 48-54 | BUILDING, CART, CHAOS, CHAOS_P, CHAOS_STINIT, CHAOS_STD, CHAOS_W | various | | | | 3 / 2 / - | stage / boss specific |
| 55-57 | E101R, E103, EGM3 | `0C900000`, `8C02493C`, `0C900160` | | | | - | boss cameras (two in overlays) |
| 58-59 | FOLLOW_G, A_ | `8C024768` | 0x1CA | 0/1 | 1 | 0 | UNVERIFIED |
| 60 | LR | `8C01DFA0` | 0x1BE | 2 | 2 | - | UNVERIFIED |
| 61 | COLLI | `8C024582` | 4 | 4 | | **1,217** | camera-collision volume marker (1.7) |
| 62-68 | RuinWaka1, SNOWBOARD, **SURVEY** (0x40), TAIHO, TORNADE, TWO_HARES, LEAVE | various | | | | 0 / 5 / 16 / ... | SURVEY = the player's free camera at level 4 (R) |
| 69-72 | AVOID, A_, C_, COL_ | `8C024B5C` | 0x22E | 0/1/3/4 | 2 | 47 / 0 / 0 / 29 | UNVERIFIED |
| 73-74 | EDITOR, GURIGURI | `8C01E830`, `8C01E9C0` | | 5 | 1 | - | debug |
| 75 | PATHCAM | `8C01F13E` | 0x21A | 2 | 3 | - | path camera (UNVERIFIED parameters) |

Full table with every row: `build/research/triggers/camtable.py` (prints names, functions, sizes, levels, kinds).

### 1.5 Transitions (adjust table `8C1957D0`, 27 x {flag, function}; names at `8C18C218`)

| Adjust | Function | Size | Behaviour (Ghidra reading; constants from the pool) |
|---|---|---|---|
| 0 NONE | `8C024D8E` | 0x3C | cut |
| 1 NORMAL, 2 NORMAL_S (flag 0x8000) | `8C024DCA` | 0x246 | chase: per step the angles move at most 0x100 BAMS (1.41 deg) on one axis and 0x200 (2.81 deg) on the other; distance thresholds 0.3 / 20 (which axis is which: UNVERIFIED) |
| 3 SLOW, 4 SLOW_S | `8C025010` | 0x292 | slower chase (UNVERIFIED constants) |
| 5 TIME | `8C0252A2` | 0xBC | timed blend, t = timer x 1/120: **120 steps = 2 s**, then adjust becomes NONE |
| 6-26 THREE1..5(C), RELATIVE1..6(C) | `8C02535E` (all 21 share it) | 0x124 | timed blend whose duration comes from `8C025482`/`8C025524`; at the end adjust becomes NONE |

`8C019F4A` runs the adjust only when the camera task has run more than 60 steps since it was (re)initialised
(`0x3C < 8C6B7078`); the "S" flag (0x8000) sets control-work flag bit 0, which also skips it while the change timer
(+0x14, steps since the last camera change) is 0 (reading of `8C019F4A` / `8C0197BA`, UNVERIFIED in effect). The 4,894 CAM entries use RELATIVE3C 1,091, RELATIVE3 881,
NONE 849, NORMAL 520, RELATIVE2C 462, ... (1.6): **stages mostly use the timed RELATIVE blends**.

### 1.6 The CAM files (camera trigger volumes on the disc)

* **Files**: `SONICADV/CAMllaaX.BIN` (`ll` level, `aa` act; `X` = character letter S, M (Miles), K, A, E, B) and
  `CAMEC..`, `CAMSS..`, `CAMMR..`, `CAMPAST..`, `CAMGARDEN..`, `CAMAL_RACE..`, `CAMMCART..`, `CAMSBOARD..`, `CAMSHT..`.
  **101 files, not compressed.** `8C09CD6E` builds the name from the character's letter (table `8C1C4090`) and falls back
  to the table's first letter (S) when that file does not exist.
* **Loaded by** the stage load `8C09CE90` into **`0x0CB50000`** (act 0), `0x0CB58000` (act 1), `0x0CB60000` (act 2)
  (and `0x0CB54000 / 0CB5C000 / 0CB64000` for stages with more acts; per-level pointer table `8C1958B4`, 8 acts x 4
  bytes per level). SET files go to `0x0CB68000..`.
* **Format** (measured: for all 101 files `0x40 + count x 0x40 == file size`):

| Offset | Type | Field |
|---|---|---|
| header +0x00 | Sint16 | entry count; rest of the 0x40-byte header zero |
| +0x00 | Sint8 | mode (index into the 76-mode table) |
| +0x01 | Sint8 | priority (within a level; higher wins) |
| +0x02 | Uint8 | adjust used when the volume is entered |
| +0x03 | Sint8 | shape: 0 sphere, 1 plane, 2 block |
| +0x04 / +0x06 | Sint16 | volume angles x / y (BAMS) |
| +0x08 | float[3] | volume centre |
| +0x14 | float[3] | volume size: block half extents; plane half width / half height; sphere: **+0x1C = radius squared** (the test compares the squared distance with it directly, asm `8C01A1AC`; 212 of 244 Emerald Coast + Station Square spheres also store the radius in +0x14) |
| +0x20 / +0x22 | Sint16 | camera angles (parameters +0 / +2) |
| +0x24 | float[3] | point A ("look" point: FIXED's target) |
| +0x30 | float[3] | point B ("camera" point: FIXED / ASHLAND camera, KLAMATH pivot) |
| +0x3C | float | distance |

* **Entry 0 is the act's default camera** (`8C01951E` takes its mode as the starting mode; the scan never tests it).
* **Statistics** (`camfiles.py`): 4,894 entries; shapes BLOCK 2,480, SPHERE 2,023, PLANE 391; by level of the mode
  (entries 1..n-1): NORMAL 2,198, AREA 1,043, COMP 113, COLI 1,439 (743 spheres, 660 blocks, 36 planes); priorities 0
  (4,116) to 14. Sizes (non-COLLI): block half extents quartiles x 30 / 50 / 91, y 28 / 41 / 70, z 30 / 51 / 93 SA1
  units (a median block is about 400 x 330 x 410 Hammer units); sphere radius 35 / 56 / 82; plane half sizes 44-94 x
  34-92; camera distance quartiles 50 / 60 / 80 (min 1, max 1,200). The largest file is `CAM0301S.BIN` (203 entries).

### 1.7 How the volumes switch the camera

`8C01995E` (per step, after `8C019ADC`): if the act has a CAM table, walk the entries **from the last one down to entry
1**; consider an entry only if its mode's level is >= the current level; test **player 0's position + (0, 4, 0)**
against its shape:

* sphere `8C01A1AC`: `|p - centre|^2 < size.z` (squared radius stored in the file);
* block `8C01A480`: bounding-sphere pre-test with `|size|^2`, then the point is moved into the volume's frame (translate,
  rotate x / y by the volume angles, `njInvertMatrix`) and compared with the half extents;
* plane `8C01A240`: bounding-sphere pre-test with `size.x^2 + size.y^2`, then the side of the plane is checked with the
  player's previous position (`GetPlayerPosition 8C07CE82(0, 1/2)`), i.e. a **gate that fires when crossed**.

Then, by the current level: NORMAL (`8C019BE6`): enter any hit non-COLI entry; AREA / COMP (`8C019C50`): leave the
current entry when the player is no longer inside (pop), enter an entry of a higher level, or of the same level with a
higher priority byte; EVENT (`8C019DC4`): COLI **planes** crossed during an event camera rewrite the camera that the
event will return to, and a volume with a higher priority than the current entry ends the event camera. Entering is
`8C01A7D6(mode, entry)`: save the current state in the new level's slot, set level, entry, mode function, adjust
(the entry's adjust when either priority is < 10, else 0) and conversion kind.

**COLI entries** (mode COLLI and the `COL_` modes, level 4) are not cameras: the mode functions FOLLOW-like,
KNUCKLES, KLAMATH, POINT, SNOWBOARD, FISHING call `8C01B684` (spheres `8C01B84E`, blocks `8C01BBA0`) and `8C01B782`
(planes `8C01B900`) to **push the camera out of them** - invisible camera-only walls. `8C01C480` draws all volumes in
the debug display (`8C01C540`, `njDrawLine3D`).

**The player's free camera**: `8C019ADC` pushes SURVEY (mode 0x40) at level COLI with adjust TIME when buttons in mask
0xF0 of the game's button word `B_8C754C10` are pressed (not in the ASHLAND_I or FIXED families, not within 30 steps of
the last change), and pops it on buttons 0x606 or analog input `8C754C24 / 8C754C26` (button mapping UNVERIFIED).

### 1.8 Cameras set by code

* **Event cameras**: `8C01AD2C` has 44 callers (1ST_READ 19: the camera's own `8C01B640`, `8C01FB98`, `8C01FD6C`,
  `8C0213E0`; scripted camera `8C048B00`, `8C04917A`; events `8C04A75A`; game mode `8C04EFE0`; stage objects `8C0617D0`,
  `8C063E4A`; path-camera object `8C06BAD2`; player `8C07BC0E`; Sonic `8C0A966C`, `8C0B1352`; Tails `8C0B70F4`; Big's
  fishing `8C0E6586`, `8C0E91E4`, `8C0E943C`, `8C0E9CC6`; overlays 25: AL_RACE, B_CHAOS2/4, B_E101, B_EGM2, B_ROBO,
  STG01-STG12), `8C01ABDC` 33 (30 in boss / stage / MINICART overlays), `8C01AE60` 47 (`callers_all.py`).
* **Stage path cameras** (Red Mountain act 2 = stage 0501, Final Egg acts 2-3 = 0A01 / 0A02): path tasks with exec `8C06C022..8C06C0A8` (thunks
  that set a variant 0..9 and call `8C06BEA0`, which computes the path's bounding box +/- 50 and installs `8C06BAD2`).
  When player 0 is inside the box and no EVENT/COMP camera is active, `8C06BAD2` pushes an event camera with function
  `8C021B60` (0x9F2 bytes), which keeps the camera on the path relative to the player; it releases it on exit. Variant
  records: pointer table `8C1BB4C0` -> 10 records of 0x70 bytes at `8C1BB060..8C1BB450` (first words 24, 0, 52.0,
  40.0 or 60.0, 0.55/0.65, 0.93/0.90, 48.0, 35.0, 6.0/15.0, ...; +0x38 first point, +0x3C points left out at the end;
  field meanings UNVERIFIED). **This is the game's "camera rides a spline" mechanism.**
* **Board trick cameras** `8C01FB98` / `8C01FD6C` (chosen at random, adjust NONE, kind 2) when Sonic or Tails jump a board
  ramp at speed > 4.14 (Sonic `code_8C0A966C.c:1517`, Tails `code_8C0B70F4.c:1281`).
* SET objects of single stages: `O_PURS_CAMERA` and `_0_LIGHT_CAMERA` (Final Egg), `CAM_COLLI` (Chao gardens).

### 1.9 Measured: the original camera crossing volumes (harness trace)

`sa1h trace --level 1 --act 0 --chr 0 --input "30 0 0; 700 0 -127"` (Sonic runs forward in Emerald Coast act 1, the
game's camera task running in the interpreter; trace CSV columns `cam_*`), analysed against `CAM0100S.BIN` with
`ec1_cam_analyze.py`:

| Steps | Sonic inside | Camera |
|---|---|---|
| 0-185 | #73 POINT (block, dist 40), briefly #86 KNUCKLES | 43-45 units from Sonic, ~14 above |
| 195-280 | #20 **KLAMATH** (block, half extents 266 x 116 x 60, pivot B (-613.6, 35.0, 485.9), dist 70, adjust RELATIVE3) | swings out: horizontal distance 44 -> 67.6 -> 68.3, height +31; at steps 244-256 it sits within 2-7 units of "on the ray from B through Sonic at distance 70" (e.g. (-374.7, 217.0) vs (-369.4, 217.5)), still converging (timed blend) |
| 280-720 | no volume (NORMAL default) / #24 A_ASHLAND at 387 | 66-93 units, turning slowly as Sonic stops at a wall |

So the volumes behave as read from the code; exact parameter semantics of each mode are still to be confirmed by
decompiling them (section 6).

### 1.10 The camera today

* **LIB**: worlds create a level-0 **camera adapter** task (`CreateElementalTask(2, 0, NULL)`, the game's empty exec
  `8C098440`) whose taskwk is `D_8C18B7EC`; live mode writes the host camera into it before each step: ang.y =
  `round(camera_yaw / 2 pi x 65536) - 0x4000`, smode, position (`sa1_world_tick.c sa1g_camera_refresh`, integration
  6.7); `sa1g_camera_view` loads a synthetic view (40 units behind player 0, 7 up) into XMTRX for the game's visibility
  test `8C038D00`. Replay mode injects the trace's camera blob (`px_cam_raw`, 0x40 bytes) at player entry.
  `gen/policy/stubs.tsv`: the camera hooks `8C019368, 8C01A7A4, 8C01ABDC, 8C01AD2C, 8C01AE60, 8C01AF06, 8C01B158,
  8C0213E0, 8C0EAD6A` are **void stubs**; `8C01FB98, 8C01FD6C, 8C021700, 8C023360, 8C0239C2` are address-only;
  `8C01976C` is fatal. So the characters' event cameras do nothing in GMod.
* **G-SA1** (`addon/lua/gsa1/cl_character.lua`, `CalcView` "gsa1_view"): its own chase camera - mouse orbit, distance
  `gsa1_cam_distance` (default 150 Hammer units = 37.5 SA1 units; the game's area cameras median 60), height
  `gsa1_cam_height`, hull traces against brushes; the camera yaw is the stick's frame. Known bug (plan 14.1e): it
  overrides the view even while the player looks through a `gmod_cameraprop` or any view entity.

---------------------------------------------------------------------------------------------------------------------

## 2. SA1's path system

### 2.1 Data structures (decompiled; `code_8C08966E.c`, `code_8C089AA0.c`, `code_8C089C80.c`)

```c
typedef struct { Sint16 slangx, slangz; Float length; Float x, y, z; } PATHTBL;            /* 0x14 per point */
typedef struct { Uint8 slattr; Uint8 unk1; Uint16 points; Float totallen;
                 PATHTBL *tblhead; void (*pathctrl)(task *); } PATHTAG;                     /* 0x10 */
typedef struct { Angle slangx, slangz, angx, angz; Float onpathpos; Float xpos, ypos, zpos;
                 Float xpos2, ypos2, zpos2; NJS_VECTOR normal, normala, front; } PATHINFO;   /* 0x50 */
```

* `slattr` 0 = open (evaluation fails outside 0..totallen), 1 = closed loop (`onpathpos` wrapped by `8C056784`).
* `length` = distance to the next point; the last point's length is 0; `totallen` = sum of lengths. **Measured** on
  Emerald Coast's paths `0CAA958C` / `0CAA9E44`: |length - distance| < 0.00025, last length 0.0; segment lengths 0.16-10.5
  (mean 6.35) and 0.24-216 (mean 24).
* Evaluation (`8C0897F8`, GetStatusOnPath): find the segment containing `onpathpos`, **linear interpolation** of the
  position (`xpos2`), the up vector from the angles: `normal = (-sin z cos x, cos z cos x, sin x)` (point's own) and
  `normala` (angles interpolated with the neighbours' half-way values, `8C08966E`), `front` = unit segment direction.
  So the game's path is a polyline with per-point bank; smoothness comes from dense points.
* Two more evaluators exist: a **quadratic B-spline** path (`8C089C80`, `SPLINE_TAG` 0x18: centre line + side line
  tables of 16-byte points, uniform basis b0 = (1-t)^2/2, b1 = t(1-t) + 1/2, b2 = t^2/2; normal from the side line;
  no caller found - function pointer use UNVERIFIED) and a **natural cubic spline** builder (`8C089184`, `8C08925A`,
  `8C0893E0`, 48-byte knots) called only by the dead `8C089528`.

### 2.2 The path library (`8C089180-8C089F20`: 17 functions, 3,294 bytes, all matched C)

| Address | Size | Role |
|---|---|---|
| `8C089560` / `8C089598` / `8C08960A` | 0x38 / 0x72 / 0x64 | path manager (created by the stage start): for each `PATHTAG` of the stage's list (`D_8C1BDF24`) with a `pathctrl`, `CreateElementalTask(2, 2, pathctrl)` with `twp->value = tag`, `timer.w[0] = stage` |
| `8C08966E` | 0x18A | neighbour points + half-way angles |
| `8C0897F8` | 0x216 | **status on path** (position, up, front at `onpathpos`); 29 callers in 1ST_READ and 13 overlays |
| `8C089A0E` / `8C089A40` / `8C089A6E` | | distance -> point index, point index -> distance, wrap |
| `8C089AA0` | 0xFA | **nearest point on the path** to a position (min distance over segments via `8C02B0B4`, returns the distance along the path) - used by `RunAlongPathP` and 7 overlay objects |
| `8C089B9A` / `8C089C12` / `8C089C80` | | quadratic B-spline evaluation (2.1) |
| `8C089184` / `8C08925A` / `8C0893E0` / `8C089528` | | natural cubic spline (dead code) |

### 2.3 Paths in the stages (measured, `paths.py`)

`D_8C1BDF24` (1ST_READ) = 21 `{stage, PATHTAG **list}` records ending with 0xFFFF; the lists live in the stage
overlays. **87 paths, 8,335 points.** Examples: Emerald Coast act 1: 5 paths (two loop paths of 82 / 76 points and
514 / 452 units with exec `8C06B102`, two with stage execs `0C90B7A0/0C90B7B0`, one without exec); Windy Valley act 3
(`0202`): 201-204-point tornado paths (exec `0C908AE0`, which posts order 1); Ice Cap act 3 (`0802`): ten 241-point
avalanche paths of 12,900-14,600 units; Speed Highway act 3 (`0402`): a closed 200-point path (`slattr` 1, 8,113
units); Red Mountain act 2 (`0501`): six path cameras (`8C06C02A..8C06C0A8`); Final Egg act 2: two path cameras
(`8C06C022`) and two loops. Execs: none 19, `8C06B102` (loop) 14, `8C06C022` (path camera variant 0) 4,
`8C06B43C` 3, plus 47 stage-specific objects.

### 2.4 Path-work objects (`8C06AF00-8C06C0C0`: 16 functions, 4,446 bytes, 1 decompiled = 10 bytes)

| Function | Size | What it does (asm / Ghidra reading) |
|---|---|---|
| `8C06B102` | 0x33A | **loop trigger**: first exec computes the path's bounding box +/- 40; then, for players 0 and 1 inside the box, if a path point is within **40 units** (`FCMP` against 40.0 at `8C06B3D0`) -> `RunAlongPathP(pno, path)`; per-player cooldown of 60 steps |
| `8C06B43C` | 0x316 | same trigger for `RunWithSeeingPathP` (shimmy) |
| `8C06B752` | 0x380 | same trigger for `HangDownFromPathP` (hang) |
| `8C06BEA0` + `8C06BAD2` | 0x182 + 0x3A8 | path camera (1.8) |
| `8C06C022..8C06C0A8` | 8-10 each | variant thunks 0..9 in address order (0, 1, 5, 9 checked) |
| `8C06AF00` | 0x202 | debug: draws the path with fading lines (`njDrawLine3D`) |

### 2.5 The player on a path: orders, modes, speeds

Every order is `twp->flag |= 0x1000; twp->smode = order; twp->value = PATHTAG` (the same mechanism as `SetInputP`);
the character's next-action handler turns it into a mode (decompiled handlers: Sonic `8C0AFF1A`/`8C0B07E4`, Tails
`8C0BBDD6`/`8C0BC61C`, Knuckles `8C0C5BCA`/`8C0C61F4`, E-102 `8C0D079A`/`8C0D0B16`, Amy `8C0DB67E`..`8C0DBC16`, Big
`8C0E43CA`..`8C0E488C`).

| Order (API) | Sonic | Tails | Knuckles | E-102 | Amy | Big | Movement |
|---|---|---|---|---|---|---|---|
| 1 (`8C07BFFA`, also +5.0 forward speed) "path run" | mode 3, act 11 | 3 / 11 | 3 / 11 | 3 / 3 | 3 / 11 | 3 / 9 | `8C078308`: position = path point, `hpos += spd.x` (signed by facing); speed from `8C06F25C` (plain acceleration: gravity, air resistance, stick x run_accel, static friction) and `8C0725C0` (forward-only integration), so the stick and the path's slope set the speed; yaw turns to the tangent (`AdjustAngle` 0x800/step), ang.x/z = path up angles |
| 2 (`8C07C02E`) "path roll" | mode 7, act 15 (ball, flag 0x500) | 5 / 15 | not handled | 3 / 3 | not handled | not handled | as order 1, balled |
| 26 (`RunAlongPathP 8C07C0A0`) "steer" | flag 0x2000 when running (mode 2 / 5 or spd > run_speed) | flag (mode 25 or spd) | flag | flag | flag | flag | **normal physics and collision**; `8C06F530` turns the yaw toward the path (projected on the ground plane), `hpos += spd.x`, speed raised to `dash_speed` while the stick is held; leaves when the path ends or the direction degenerates |
| 29 (`RunWithSeeingPathP 8C07C0E4`, start point n) "shimmy" | mode 36, act 67 | 36 / 91 | 42 / 101 | - | - | - | `8C0784F8` + `8C0AE760`: hang below the path (position lowered by the character's height), hand over hand while the stick is held, motion frame stepped by 2 |
| 31 (`HangDownFromPathP 8C07C120`, point n, rotation) "hang" | mode 38, act 69 | 38 / 93 | 44 / 104 | 47 / 66 | 40 / 72 | 29 / 46 | `8C078710` + `8C0AE97E`: position = path point - height, `hpos += spd.x`, clamped to the ends |

* Leaving: open paths end when `8C0897F8` fails (outside 0..totallen): orders 1/2 continue with the world speed; 29/31
  clamp `hpos` to 0.01..totallen-0.01 and stop; 26 clears flag 0x2000.
* Who posts them in the game: `RunAlongPathP` - loop objects `8C06B102`, Sky Deck `STG06:0C900000`; order 1 - Windy Valley
  `STG02:0C908AE0`, Final Egg `STG10:0C900780`, Perfect Chaos `B_CHAOS7:0C91AEE0`; order 2 - `STG02:0C908CE0`;
  `HangDownFromPathP` - `8C06B752`, Mystic Ruins `ADV02:0C913F42`, Casinopolis `STG09:0C917E68`;
  `CheckPlayerRideOnPath 8C07C17A` (flag 0x2000 and this path) - `ADV02:0C913F12`.

### 2.6 Cutscene movement: event paths, "walk to", animation queue (`8C04B440-8C04BF00`: 17/17 decompiled)

* **Event path** (`eventwk.list4C`, `include/sa1/engine.h`): `8C04B7BA(twp, EV_PATH *path, float speed, int mode)`
  appends `{mode, path, speed}`; `EV_PATH = {num, points}`, `EV_PATH_POINT = {Sint8 ang[3] (1/256 turn), pad, float
  pos[3]}` (16 bytes); speed in **points per step**; mode bit 0 loop, bit 1 relative to the task's position. While the
  list is non-empty the character's handler switches to its event-path mode - **Sonic 80, Tails 58, Knuckles 49,
  E-102 55, Amy 45, Big 52** - whose exec is `8C04B9E0`: interpolate position and angles between points, optionally
  **snap to the ground** (eventwk.mode bit 0 height, bit 1 slope angles), advance the frame by the speed, wrap or go to
  the next queued path. `8C04B82E` clears the queue.
* **Locks** (`8C06EAC0`, used by `PGetAcceleration` while `twp->flag & 0x4000`): `8C07C1B4(pno, x, z)` lock 2 "walk
  to (x, z)" at `f34` (0 = run_speed) with acceleration `f38` (0 = run_accel), **stops and sets lock 1 within 1.0
  unit**; `8C07C1EE` lock 3 "run along vector (x, z)" (length = speed, never arrives); `8C07C240` lock 4 "push by
  vector"; `8C07C270` lock 1 "hold"; any other lock value releases. All with the game's own physics and collision.
* **Animation queue** (`eventwk.list40`): `8C04B4D2` / `8C04B528` append an `EV_ACTION_LIST {mode (loop), linkframe
  (blend frames), NJS_ACTION, texlist, next, speed (frames per step)}`; the characters' displays draw it via `8C04B57C`
  instead of their own action when the queue is non-empty (Sonic `code_8C0AE0E0.c:133`).
* The same locks also steer a character in the air (`8C06EFCE`, air acceleration while the input is locked).

---------------------------------------------------------------------------------------------------------------------

## 3. Design: one trigger family

### 3.1 The entity `gsa1_trigger`

A scripted entity (`lua/entities/gsa1_trigger/`), server-owned, networked to every client
(`ENT:UpdateTransmitState()` returns `TRANSMIT_ALWAYS`: the character can be anywhere, PVS would hide triggers).

**Volume** (the game's three shapes, so camera triggers convert 1:1 into CAM entries):

| Shape | Properties | Test (host side, mirrors the game) |
|---|---|---|
| Box (default) | half extents (Vector), yaw + pitch (the entity's angles; the CAM entry keeps x/y angles, so roll is ignored) | point in oriented box |
| Sphere | radius | distance < radius |
| Gate (plane) | half width, half height, angles | segment from the previous to the current position crosses the rectangle (the game's plane is a crossing test, 1.7) |

The tested point is the character's position + 4 SA1 units up (16 Hammer units), as the game tests `pos + (0, 4, 0)`.
Defaults follow the stage data (1.6): box 200 x 165 x 205 half extents in Hammer units, sphere radius 225.

**Common properties** (NetworkVars with `Edit` metadata, so GMod's "Edit Properties" window edits them; the tool panel
sets them on creation): Name (for chaining), Enabled, Action (below), Who (local character / any G-SA1 character /
any player incl. plain GMod players), Character filter (bit mask of the 7 kinds), Fire (on enter / on leave / while
inside / on enter, once), Cooldown (s), Then (name of another trigger + delay: chaining for scripted events).

**Actions**

| Action | What happens | Where it runs |
|---|---|---|
| Board On / Board Off | the board round's `gsa1.Board(true/false)` (gsa1_060_board.md 5.6) for Sonic and Tails | client, local character |
| Camera | the trigger is a camera volume: mode, priority, transition, points A/B, angles, distance (section 4) | the character's world (the game's camera) |
| Spline | put the character on spline ID N in a ride mode (section 5) | client -> libsa1 |
| Event | a list of steps: show text (v1_npcs' message window), play a sound / jingle / music track from the disc (libsa1 audio ids), give rings / lives, set a checkpoint, teleport the character, fire a map entity input (`ent:Fire`), enable / disable another trigger | server (validated), then the client for character effects |
| Kill | the game's death (`sa1_character_damage` with `SA1_DAMAGE_KILL`) | client |

Presets in the spawn menu are thin subclasses (`ENT.Base = "gsa1_trigger"`, e.g. `gsa1_trigger_board_on`,
`gsa1_trigger_board_off`, `gsa1_trigger_camera_fixed`, ...), which is the safe way to give one class several spawn
icons with different defaults (whether `list.Set("SpawnableEntities", ...)` key values reach a SENT is UNVERIFIED).
The board spec's "Entities > G-SA1 > Board On / Board Off" is met by `ENT.Spawnable = true; ENT.Category = "G-SA1"`.

**Evaluation.** The character's world runs on its owner's client, so the client tests the triggers: after every
`gsa1` tick, for each 60 Hz step the tick ran, the character's position (libsa1 state; for multi-step ticks the
segment between the previous and the current position) against every enabled trigger in a per-map spatial list
(rebuilt when triggers change; hundreds of volumes cost microseconds). Enter / leave events are edge-triggered per
trigger. Camera triggers are not tested by Lua: they are the world's CAM table and the game's camera tests them
(section 4). Server-side actions are sent with one net message (trigger entity + event); the server checks that the
player's character is plausibly inside (distance from the player entity, which the server already moves to the
character, `sv_gsa1.lua`) and rate-limits. For plain GMod players (Who = any player) the server uses ordinary touch
(`SetTrigger(true)`, `StartTouch` / `EndTouch`) - UNVERIFIED for players in noclip, which is how G-SA1 parks the
player entity of a character, so characters never rely on it.

**Visibility.** Triggers draw only for the person editing: with the toolgun (G-SA1 tools), the physgun or the Camera
tool out, or with the Q-menu option "Show triggers and splines" (default: with tools). Box / sphere wireframes
(`render.DrawWireframeBox`, `render.DrawWireframeSphere`), a translucent fill coloured by action, the name and action
as a 3D2D label, camera triggers also their points A/B, the view frustum of FIXED and the pivot ray of KLAMATH.

### 3.2 Spawn menu, tools and options

* **G-SA1 spawn tab** (v1_arch's `spawnmenu.AddCreationTab`), category **Triggers**: Board On, Board Off, Camera
  (Fixed, Watch, Pivot, Area follow, Path), Spline start, Event, Kill. Spawning places a default box at the aim point.
* **Tool "G-SA1 Trigger"** (`lua/weapons/gmod_tool/stools/gsa1_trigger.lua`, `TOOL.Category = "G-SA1"`): left click
  = first corner, second left click = opposite corner (box sized and placed between them, aligned to the player's yaw),
  right click = select a trigger (then its properties in the panel), reload = remove; the panel holds the action and its
  settings; "Set point A / B here" buttons for camera triggers (also "use the aimed Camera-tool camera as point B").
* **Tool "G-SA1 Spline"** (section 5.1).
* **Options > G-SA1 > Triggers** page (owner rule: every option has a control): show triggers and splines (always /
  with tools / never); camera triggers on/off; transitions (as the game / always cut / always smooth 0.5 s);
  "keep the stick direction across camera cuts" (default on); "let the camera keys look around inside trigger cameras
  (the game's takeover)" (default on); "triggers of other players affect me" (default off); spline ride defaults.

### 3.3 Saves, dupes, undo

* NetworkVars are saved and restored by GMod's saves and the duplicator (DT table); positions and angles are the
  entity's own. Spline nodes carry their spline ID and index as NetworkVars too (section 5.1), so a save restores every
  spline exactly.
* **Dupe paste and IDs**: `ENT:PostEntityPaste(ply, ent, created)` builds one remap table per paste (keyed by the
  `created` table): every spline ID in the paste that collides with an ID already on the map (and not part of the
  paste) gets the next free ID; nodes and the triggers of that paste that reference the ID get the new one. So pasting
  the same dupe twice gives two independent splines. Trigger names (chaining) are remapped the same way.
* `undo.Create` / `cleanup.Register("gsa1_triggers")`; the tool adds every created entity to the player's undo.
* Optional per-map file `data/gsa1/maps/<map>.json` ("Save this map's triggers" / "Load on map start" buttons on the
  Triggers page; default off) for owners who do not use saves.

---------------------------------------------------------------------------------------------------------------------

## 4. Design: camera triggers with the game's camera

### 4.1 Principle

A camera trigger **is** a CAM entry. G-SA1 converts its camera triggers into a CAM table (host -> SA1 coordinates,
volume sizes divided by 4, angles to BAMS, sphere radius squared into +0x1C, entry 0 = the HOST default) and hands it to
the local character's world; the world runs **the game's camera task** instead of the adapter; libsa1 reports the
camera (position, angles, field of view, level, mode, blend progress) after every step; G-SA1's `CalcView` shows it,
interpolated with the same alpha as the character. The game's own scan, priority stack, transitions, player takeover,
free camera and COLI camera walls then work as in the game, and the camera's own collision runs against the map's
collision (the world's landtable is the GMod map).

Followers' worlds (round 6b) keep the adapter: only the viewer's character runs the camera.

### 4.2 The modes offered

| G-SA1 preset | Game mode (levels AREA / COMP) | Trigger properties -> entry fields |
|---|---|---|
| Fixed (camera at a point, looking at a point) | FIXED 28 / A_FIXED 29 / C_FIXED 30 | point B = camera, point A = look-at |
| Watch (camera at a point, looking at the character) | A_ASHLAND 21 (player may look around) / A_ASHLAND_I 24 (locked) | point B = camera |
| Pivot (camera behind the character on the ray from a pivot) | A_KLAMATH 32 | point B = pivot, distance |
| Point (camera on the line from a point through the character) | A_POINT 40 | point A, distance |
| Area follow (the game's usual area camera) | A_KNUCKLES 5 | distance, angles (+0x20 / +0x22) |
| Path (camera rides a camera spline) | the path-camera object `8C06BAD2` + `8C021B60` (1.8) with the camera spline's `PATHTAG`; PATHCAM 75 as alternative | camera spline ID, variant 0..9 |
| Camera wall (keeps the camera out) | COLLI 61 | shape only |
| Advanced | any of the 76 modes, raw fields | all fields |

The NORMAL (level 0) camera is a new **HOST mode**: a 77th entry appended to a world copy of the mode table (count 0x4D
in the camera's globals) whose function lives in LIB's synthetic code image (`LIBSA1_HOST`, the host-object layer's
mechanism, `libsa1_interaction.md` 2.3) and writes the host's chase camera (position, target) into the camera work
each step. G-SA1 keeps its chase camera and mouse orbit exactly as today while no trigger camera is active, and the
game's transitions blend between it and the trigger cameras both ways (entering uses the volume's adjust, leaving
restores the saved slot's). Option "Use the game's own follow camera" swaps HOST for FOLLOW (owner question 1).

Priority: AREA triggers (default) nest by the volume's priority byte (0-14 in the game); COMP triggers win over event
cameras and AREA volumes; scripted event cameras (Event action "camera") use the EVENT level through `8C01AD2C` /
`8C01ABDC` with a mode, so a cutscene can take the camera and give it back.

Transitions offered (the volume's adjust): Cut (NONE), Chase (NORMAL), Slow chase (SLOW), 2 s blend (TIME), Timed
blends (RELATIVE1..6, THREE1..5 and their C variants) - the game's names in the Advanced panel.

### 4.3 Input, view and interpolation

* The stick is turned by the game camera's ang.y (`8C05117E`), as in SA1. G-SA1 option "keep the stick direction across
  camera cuts" (default on): while the stick stays deflected after a camera change, G-SA1 keeps feeding the previous
  camera yaw (host input mapping; no game code changes).
* Mouse / right stick inside a trigger camera: forwarded as the game's camera buttons / analog (`B_8C754C10`,
  `8C754C20..26`), which gives ASHLAND / KLAMATH their takeover to the area camera and the SURVEY free camera; option
  off = ignored.
* Field of view: the game's 0x31C7 BAMS = 70.0 deg (horizontal at 640 x 480); G-SA1 converts to GMod's `fov` for the
  screen's aspect (the board doc's formula, gsa1_060_board.md 5.5).
* `CalcView` yields when `ply:GetViewEntity() ~= ply` (gmod_cameraprop, other addons' view entities; plan 14.1e) and
  when the character is inactive.
* Interpolation: libsa1 keeps the camera of the last two steps and returns the camera at the render alpha (positions
  lerped, angles slerped), with the drawn step's convention of round 4c.

### 4.4 LIB API (draft, `SA1_API_CAMERA`)

```c
typedef struct sa1_cam_volume {          /* one CAM entry in host space */
    uint32_t struct_size;
    uint8_t  mode, priority, adjust, shape;  /* game values (shape 0 sphere, 1 plane, 2 block) */
    float    centre[3], size[3];         /* size: half extents / radius in x */
    float    vol_pitch, vol_yaw;         /* radians */
    float    point_a[3], point_b[3];
    float    cam_pitch, cam_yaw;         /* radians (entry +0x20 / +0x22) */
    float    distance;                   /* host units */
} sa1_cam_volume;
#define SA1_CAMERA_GAME     0x1u   /* run the game's camera task (else the adapter, today's behaviour) */
#define SA1_CAMERA_FREEKEYS 0x2u   /* forward the host camera keys as the game's camera buttons / analog */
SA1_API sa1_status sa1_camera_configure(sa1_context*, sa1_character_id, uint32_t flags);
SA1_API sa1_status sa1_camera_set_volumes(sa1_context*, sa1_character_id, const sa1_cam_volume*, uint32_t n);
SA1_API sa1_status sa1_camera_event(sa1_context*, sa1_character_id, int32_t mode, uint32_t adjust); /* -1 = release */
typedef struct sa1_camera_state {
    uint32_t struct_size;
    float position[3], target[3], angles[3], fov;     /* host space, at the render alpha */
    int32_t level, mode, entry; uint32_t adjust; float blend;  /* entry -1: none */
} sa1_camera_state;
SA1_API sa1_status sa1_character_camera(sa1_context*, sa1_character_id, float alpha, sa1_camera_state *out);
/* sa1_input gains host_cam_pos[3], host_cam_target[3] (read by the HOST mode) when struct_size covers them */
```

Implementation notes: the world creates the camera with `8C01957E` (it needs the CAM table pointer: the world writes the
table into its extension segment and sets `8C6B7060 / 8C6B7064` after creation; a per-world copy of the mode table with
the HOST row; `8C1958A8`'s counts copied as the code does); `GetStageNumber` keeps the world's level/act;
`8C04FE40` / `8C04FE4A` (game state) keep their policies; the 34 player readers of `D_8C18B7EC` now see the real camera
(including `ang.z >= 0x4000` steering inversion, integration 6.7). Replay mode keeps injecting the trace's camera blob
unless the new "camera native" replay option is set (section 6).

### 4.5 What must be decompiled for this

| Part | Functions | Bytes | Notes |
|---|---|---|---|
| Camera core (task, scan, shapes, stack, adjusts, conversions, final view and collision) | 55 | 12,276 | closure of `8C01957E 8C0197BA 8C01976C 8C01AAE0 8C01ABDC 8C01AD2C 8C01AE60 8C01B040 8C01B158 8C01AF06 8C01A7D6 8C01A9B8` + the 5 adjust functions |
| Modes for G-SA1 (FOLLOW, FIXED, ASHLAND, POINT, KLAMATH, LINE, KNUCKLES, NEWFOLLOW, PATHCAM, SURVEY, path cameras `8C01FB98 8C01FD6C 8C021432 8C021B60`) + COLI helpers | 47 | 15,650 | overlaps the core |
| **Union** | **92** | **26,848** | of 185 / 49,070 (the rest: MAGONOTE, SONIC, FISHING, SNOWBOARD, CHAOS, CART, TORNADE, E103, AVOID, editor and debug display) |
| `8C0522C0` landtable gather | 1 | 600 | also needed by shadows `8C091C20` |
| `8C04FE4A` game-state helper | 1 | 32 | or a LIB policy |
| Path-camera objects `8C06BEA0`, `8C06BAD2`, thunks | 12 | ~1,420 | part of `8C06AF00-8C06C0C0` (section 5) |
| Ninja (clean-room in LIB rt) | 9 | - | `8C640500` (perspective, called with 0x31C7), `8C60957E` (second path of `8C01A100`, role UNVERIFIED), `8C64041C`, `8C64048C`, `8C6404C8` (camera rotations), `8C64065C`, `8C640540` (camera translate / set), `8C63E1D4`, `8C63EBC8` (draws in `8C01BD9E`: capture or no-op) |

The other game functions the subset calls are decompiled already (angle utils, `8C02A680`, `8C02CA00` NM,
`8C057B40` NM, `8C078EB0`, `8C07CE82`, `8C07CFDE`, `8C0897F8`, `8C089A40`, the task system, libm `8C10D058`,
`8C10D0F8`, `8C10CDDC` in LIB rt).

---------------------------------------------------------------------------------------------------------------------

## 5. Design: splines, the spline tool and spline triggers

### 5.1 Spline nodes, IDs and the tool

* **`gsa1_spline_node`**: a small entity per point (diamond model, invisible like triggers unless editing), NetworkVars
  SplineID (Int, 1..65535), Index (Int), Roll (Float, deg), Speed (Float, optional per-node speed for rides), Wait (s);
  the node's position is the point, its angles give the up vector when "Up from node" is set. The lowest-index node
  carries the spline's settings: Closed, Smooth (Catmull-Rom) or Straight, Sample spacing, Up mode (world up / surface
  normal at placement / node angles), Label, Colour.
* Physgun-movable (frozen, `COLLISION_GROUP_WORLD`) for fine editing; every move re-samples that spline only.
* **Tool "G-SA1 Spline"**: Spline ID control (number + "New spline" = next free ID + a list of the map's splines with
  their point counts and colours); left click = append a node at the aim point (+ height offset slider, "up = surface
  normal" for wall and ceiling paths such as loops); Shift + left = insert between the two nearest nodes; right click =
  select a node (Roll / Speed / Wait / Index in the panel; right click again = move it to the aim point); reload =
  delete the node; Shift + reload = delete the whole spline (asks); "Close loop", "Reverse", "Test ride" (rides the local
  character along it with the panel's mode). Preview: the sampled path as beams (`render.DrawBeam`), every 10th sample's
  up vector, node indices; other IDs dimmed.
* Separate IDs never interact: editing, sampling, LIB upload and rides are per ID; the dupe remap (3.3) keeps them unique.

### 5.2 Sampling into the game's formats

G-SA1 samples the node curve (centripetal Catmull-Rom, or straight segments) at the spacing (default 5 SA1 units = 20
Hammer units; the game's loop paths average 6.35) and builds:

* **`PATHTBL`** per sample: position (SA1 space), `length` = distance to the next sample (0 for the last; `totallen` =
  sum), and the bank from the up vector u (after rolling by the node roll about the tangent): `slangx = asin(u.z)`,
  `slangz = atan2(-u.x, u.y)` in BAMS - the inverse of `8C0897F8`'s normal formula, and exactly what `8C089C80` computes
  for its own normal. `slattr` = Closed.
* **`EV_PATH`** for cutscene rides: uniformly spaced samples (spacing s), angles as Sint8 1/256 turns (x, y = heading,
  z), speed in points per step = ride speed / s.

### 5.3 Ride modes (Spline action and the tool's Test ride)

| G-SA1 mode | Game mechanism (all decompiled) | Collision | Speed | Characters | Use |
|---|---|---|---|---|---|
| Steer | `RunAlongPathP` (order 26) | yes (normal physics) | the character's; dash_speed while the stick is held | all 6 (only while running) | loops, tubes, guided runs |
| Path run | order 1 (`8C07BFFA`) | no (position on the path) | stick + gravity along the path, +5 at entry (G-SA1 can hold the stick forward for "automatic") | all 6 | rails, tornado-style runs |
| Path roll | order 2 | no | as path run, balled | Sonic, Tails, E-102 | ball rails |
| Hang | `HangDownFromPathP` (order 31) | no | stick | all 6 | zip bars |
| Shimmy | `RunWithSeeingPathP` (order 29) | no | stick, hand over hand | Sonic, Tails, Knuckles | hanging bars |
| **Ride** (cutscene, default for scripted events) | event path `8C04B7BA` / `8C04B9E0` + optional animation `8C04B4D2` | no; optional ground snap (eventwk mode bits) | fixed (points per step) | all 6 | scripted movement, machinima |
| **Drive** (physics) | lock 3 `8C07C1EE`, re-aimed every step at the point `lookahead` units ahead of the nearest point (`8C089AA0` on the PATHTAG); lock 2 `8C07C1B4` for the last point (stops within 1 unit) | yes | target speed (lock 3 vector length) | all 6 | "walk / run there by yourself" |

Ride's animation: "auto" (the character's walk / jog / run action by speed, chosen from its action list), a fixed
action index, or none (the character keeps whatever it is doing). Drive and Ride lock the stick and buttons while
active (option "jump cancels the ride"). End of the spline: release (default), stop, loop, or "then spline N" (chains).

### 5.4 LIB API (draft, `SA1_API_PATH`)

```c
typedef struct sa1_path_point { float pos[3]; float up[3]; } sa1_path_point;   /* host space, pre-sampled */
#define SA1_PATH_CLOSED 0x1u
SA1_API sa1_status sa1_path_set(sa1_context*, uint32_t path_id, const sa1_path_point*, uint32_t n, uint32_t flags);
SA1_API sa1_status sa1_path_remove(sa1_context*, uint32_t path_id);   /* deferred while a character rides it */
#define SA1_RIDE_STEER  26u     /* RunAlongPathP 8C07C0A0 */
#define SA1_RIDE_RUN     1u     /* 8C07BFFA */
#define SA1_RIDE_ROLL    2u     /* 8C07C02E */
#define SA1_RIDE_SHIMMY 29u     /* RunWithSeeingPathP 8C07C0E4 */
#define SA1_RIDE_HANG   31u     /* HangDownFromPathP 8C07C120 */
#define SA1_RIDE_EVENT  0x100u  /* event path 8C04B7BA (+ action queue 8C04B4D2) */
#define SA1_RIDE_DRIVE  0x101u  /* lock 3 8C07C1EE re-aimed each step, lock 2 8C07C1B4 at the end */
typedef struct sa1_ride {
    uint32_t struct_size, path_id, mode, flags;   /* flags: REVERSE, FROM_NEAREST, GROUND_SNAP, LOOP, KEEP_INPUT */
    float speed, lookahead; uint32_t start_point; int32_t action; float action_speed;
} sa1_ride;
SA1_API sa1_status sa1_character_ride(sa1_context*, sa1_character_id, const sa1_ride*);   /* before the next step */
SA1_API sa1_status sa1_character_ride_stop(sa1_context*, sa1_character_id);
/* SA1_EVENT_RIDE: arg = started / ended / left the path; sa1_state_ex gains ride path id + distance along it */
```

LIB writes `PATHTAG` + `PATHTBL` (16 + 20 n bytes) and `EV_PATH` (8 + 16 n bytes) into each world's extension segment
when a ride starts (a 1,000-point spline = 36 KB per world), posts the order with the decompiled API function inside
the world (as `sa1_character_damage` does), and for Drive re-aims lock 3 before each step. The data stays until the
character leaves the path (flag 0x2000 / mode / queue checks), then it is freed - a removal during a ride is deferred
(a freed `PATHTAG` under `twp->value` would fault). G-SA1 needs no knowledge of the game's structures.

### 5.5 Spline triggers and scripted events

A Spline trigger names a spline ID and a ride (mode, speed, direction, start = nearest point or the start, end
behaviour, animation, camera: keep / camera spline / event camera mode). Scripted events chain: an Event trigger can
"start ride on spline N", "wait until the ride ends", "then trigger X", so "run along the bridge, cut to a fixed camera,
show a text, release" is three triggers. The machinima creator (2.0) reuses the same rides and EVENT cameras as
keyframed tracks.

A "Loop" option on a spline (after T4) creates the game's own loop object `8C06B102` in the character's world with
that `PATHTAG` (bounding box +/- 40, 40-unit proximity, 60-step cooldown, `RunAlongPathP`), so loops placed by the
owner trigger exactly as the game's do, without a trigger volume.

### 5.6 Camera splines

A Path camera trigger references a spline ID used as a camera path: LIB places its `PATHTAG` and runs the path-camera
logic (`8C06BEA0` / `8C06BAD2` with `8C021B60` and a variant record) for the local character, which is the game's own
"camera follows the player along a path" (Red Mountain, Final Egg). Until those are decompiled (T4), Path cameras are
not offered.

---------------------------------------------------------------------------------------------------------------------

## 6. Verification

* **Camera functions**: harness `diff` (original vs native) per decompiled function, then `route_matrix.py`.
* **Camera end to end, with no new references**: all 15 harness references already contain the original camera's
  output every step (`px_cam_raw`, the camera taskwk at player entry; CSV `cam_x..cam_ang_y`). A LIB replay mode
  "camera native" runs the decompiled camera in DISC worlds (the stage's own CAM file loaded where the game loads it,
  `0x0CB50000`) instead of injecting the blob, and compares the camera taskwk with `px_cam_raw` bit-exactly at every
  step (EC1 x5, SH1, RM3, TP2, FE1, EC3 + the obj_* scripts): exercises POINT, KLAMATH, ASHLAND, KNUCKLES and their
  transitions in real stages (1.9 shows Sonic crossing them).
* **Synthetic camera volumes**: a harness option that loads a CAM file from a given path instead of `CAMllaaX.BIN` (one
  `hle` file-system redirect) lets the original game run G-SA1-made tables: FIXED / ASHLAND / KLAMATH / COLLI volumes
  around the EC1 start, recorded as new references, replayed by LIB.
* **Paths**: a harness option (or a hostobj-style script line) that writes a `PATHTAG` into guest memory and posts
  orders 1 / 2 / 26 / 29 / 31, an event path and lock 3 at given steps, for each character; LIB replays them strictly.
  The path functions themselves are already matched and routed.
* **G-SA1 (offline, LuaJIT mocks)**: trigger tests (shapes, swept gate crossing, enter / leave edges, chaining,
  cooldowns), sampler tests (lengths sum to totallen, up-vector round trip through `8C0897F8`'s formula, EV speed),
  dupe-remap tests (pasting twice gives distinct IDs and consistent references), CalcView yield test.

---------------------------------------------------------------------------------------------------------------------

## 7. Work items (order, effort, dependencies)

| # | Item | Effort | Depends on |
|---|---|---|---|
| T1a | G-SA1: `gsa1_trigger` family (shapes, properties, Edit Properties, visibility, client evaluator with swept tests, server event path with checks), Trigger tool, G-SA1-tab presets, Options > Triggers page, saves / dupes / undo / cleanup, tests | M-L | v1_arch's G-SA1 tab (or the Entities tab meanwhile) |
| T1b | Actions Board On / Off (calls the board round's `gsa1.Board`) and Event (text, sound, rings, checkpoint, teleport, map input, chaining) | S-M | board round; v1_npcs for the text window |
| T1c | CalcView yields to view entities (14.1e) so GMod's Camera tool gives fixed cinematic views now | S | none |
| T2a | LIB path API (`sa1_path_set`, `sa1_character_ride`, events, state), Drive re-aim, deferred frees; tests per character | M | none (all game code decompiled) |
| T2b | G-SA1 spline nodes, Spline tool, sampler, ID management + dupe remap, previews, Test ride, Spline action | L | T1a, T2a |
| T2c | Harness path references (orders, event path, lock 3, 6 characters) + LIB strict replay | M | T2a |
| T3a | SADC: decompile the camera subset (92 functions / 26,848 B) + `8C0522C0` (600 B) | L (one decomp round, FPU-dense code) | none |
| T3b | LIB rt: the 9 Ninja camera / draw functions, clean-room, checked in harness `--rt` | S-M | none |
| T3c | LIB camera in worlds: SA1_CAMERA_GAME, HOST mode row, CAM volume segment, camera state API with interpolation, camera keys, "camera native" replay against the 15 references | M-L | T3a, T3b |
| T3d | G-SA1 camera triggers (conversion to CAM volumes, presets, points A/B tooling, CalcView from `sa1_character_camera`, stick-hold option, FOV) | M | T1a, T3c |
| T4 | SADC: path-work objects `8C06AF00-8C06C0C0` (15 functions / 4,436 B); LIB: Loop / Hang / Shimmy objects and Path cameras in worlds; G-SA1 "Loop" spline option and Path camera preset | M | T2a, T3c |
| T5 | (optional) the remaining 93 camera functions (22,222 B: MAGONOTE, SONIC, AVOID, SNOWBOARD, CART, ...) for "Advanced" mode completeness | M | T3a |

Bytes still to decompile for this topic: camera subset 26,848 + `8C0522C0` 600 + `8C04FE4A` 32 + path-work objects
4,436 = **31,916 bytes** (T3a + T4); the whole camera would be 49,070 + 600 + 4,436.

---------------------------------------------------------------------------------------------------------------------

## 8. Owner questions (each has a default the design uses)

1. **Normal camera between trigger cameras**: G-SA1's chase camera with mouse orbit (default), or the game's own
   follow camera everywhere?
2. **Looking around inside a trigger camera**: as the game (the camera keys take over and it blends back; Watch-locked
   and Fixed cameras refuse) (default), always free, or never?
3. **Stick across camera cuts**: keep the old direction while the stick is held (default) or the game's immediate
   re-mapping?
4. **Who is affected by a trigger**: only the player whose character enters (default) or everyone (shared cinematic
   cameras / events)? Do plain GMod players (no character) use Event triggers (default yes) and camera triggers
   (default no)?
5. **Scripted ride default**: Ride (exact, cutscene-like, passes through walls unless ground snap) (default) or Drive
   (the character runs there with its own physics and can be blocked)?
6. **Splines**: smooth curves through the nodes (default) or straight segments like the game's data?
7. **Showing triggers**: only while editing (default), always, never.
8. **Per-map trigger files** in `data/gsa1/maps/` in addition to saves and dupes (default off)?
9. **Before the camera decompilation lands**: no stand-in camera (default; GMod's Camera tool works after T1c), or a
   temporary non-game Fixed camera?

---------------------------------------------------------------------------------------------------------------------

## 9. Risks and UNVERIFIED points

* **Camera decompilation**: 92 functions of FPU-heavy code (Ghidra's SH-4 pair handling is noisy; e.g. `8C01A240` /
  `8C01A480` / `8C020D60`); camera code is 0 % today. The camera's collision walks the land list from `8C0522C0`; in
  GRID worlds the landtable is a window around the character, and the game's distances reach 1,200 units, so far camera
  points may not collide with geometry outside the window (measure after T3c; widen the window for the camera world).
* **Mode semantics** marked UNVERIFIED in 1.4 (FOLLOW, MAGONOTE, SONIC, LINE, NEWFOLLOW, PATHCAM, AVOID, the path
  camera records, which NORMAL axis is clamped to 0x100 vs 0x200) must be confirmed while decompiling; the EC1
  measurement only covers POINT / KLAMATH / ASHLAND loosely.
* **Analog and steering**: with the game camera on, the stick follows it, and `ang.z >= 0x4000` inverts steering;
  mouse look must not fight the camera; the HOST row is a new mode in a world copy of the table - code that indexes
  the table by mode (debug display, `8C01A5AE`) must stay within 77 entries.
* **Replay determinism**: the camera feeds the player (stick conversion, visibility), so a world with the camera on
  must record the camera inputs (host camera for HOST mode) in the machinima recorder (v1_machinima).
* **Path data lifetime** in guest memory (deferred frees), per-world memory (36 KB per 1,000-point spline per world),
  and up vectors: modes 1 / 2 / 31 set the character's ang.x / ang.z from them - a wrong up flips the character
  (Emerald Coast's loop paths store "down" up vectors `(0.06, -1.0, 0)`, harmless there only because order 26 does
  not read them).
* **Event path** rides pass through walls unless ground snap is on; lock 2 stops the character at each waypoint, hence
  Drive uses lock 3 re-aimed every step.
* **Order coverage**: order 2 is not handled by Knuckles, Amy, Big; order 29 not by E-102, Amy, Big (the handler has no
  case: the request is ignored) - the tool must grey those out per character.
* **GMod side**: `TRANSMIT_ALWAYS` for many triggers (fine for hundreds); NetworkVar limits (32 per type, 4 strings)
  rule out storing point tables in one entity (hence node entities); whether noclip players fire map triggers (G-SA1
  parks the player entity in noclip) and whether SpawnableEntities key values reach SENTs are UNVERIFIED.
* **Map triggers** (`trigger_teleport`, `trigger_push`, ...) act on the GMod player entity, not the libsa1 character:
  a separate item (detect server-side teleports of a character's player entity and teleport the character).

---------------------------------------------------------------------------------------------------------------------

## 10. Scratch files (`SADC/build/research/triggers/`)

| File | What |
|---|---|
| `status.py` | per-range decomp status (tools/progress.py rules) |
| `camfuncs.py` | functions of a range with sizes, callers, external callers |
| `ptrscan.py` | data words pointing into a code range (found the mode and adjust tables) |
| `camtable.py` | the 76-mode table and the 27-entry adjust table with the game's debug names |
| `camfiles.py` | parses all 101 `CAM*.BIN` files: counts, modes, shapes, adjusts, priorities (`-f` per file) |
| `closure.py` | callee closure of camera roots: functions, bytes, undecompiled externals |
| `callers_all.py` | callers of addresses across 1ST_READ and every overlay xrefs |
| `paths.py` | the 21 stage path lists (`-v` per path: points, lengths, execs) |
| `ec1_cam.bin/.csv`, `ec1_cam_analyze.py` | harness trace of Sonic in EC1 with the original camera; volumes crossed vs camera |
| `t_cam0.bin/.csv` | 30-step smoke trace |

---------------------------------------------------------------------------------------------------------------------

## Review (adversarial)

Reviewer pass, 2026-10-21. Read-only: nothing in SADC/src, LIB or g-sa1 was changed. Every claim below was re-measured
from the asm / Ghidra export / xrefs / disc files / LIB and G-SA1 sources; scratch scripts in
`SADC/build/research/triggersreview/` (`refmodes.py`: which CAM volumes each reference trace enters; `nm_undercount.py`:
NON_MATCHING definitions that the progress regex misses; `core.txt`, `union.txt`: recomputed camera closures).

### R1. Verified

* **Camera range and status**: `8C019360-8C025960` = 185 functions / 49,070 B, 0 C definitions (status.py re-run; no
  camera C in `src/` or `build/staging/`). `8C01957E` is called only by `8C04E660`; it creates `8C0197BA` with
  `CreateElementalTask(2, 0, ...)`, sets disp `8C019768` / dest `8C019760`, `8C6B7068 = 0x31C7` and calls
  `8C640500(0x31C7)`.
* **Mode / adjust tables**: 76 x 16 B at `8C195310`, 27 x 8 B at `8C1957D0`, names at `8C18B898` / `8C18C218`
  (camtable.py re-run). `8C1958A8` = `{0x4C, 0x1B, 8C195310, 8C1957D0}`. The only data words holding `8C195310` /
  `8C1957D0` are in `8C1958A8`; all code reads the tables through `8C6B7054` (16 pool refs) / `8C6B705C` (10), and
  `8C1958A8` is read only by `8C01957E` (`8C01968C`) and `8C01976C` (`8C019808`). A per-world table copy with a 77th
  HOST row is therefore feasible by patching the world's `8C1958A8` words (count 0x4D, pointer) - no code change.
* **CAM files**: 101 files, 4,894 entries, `0x40 + n x 0x40 == size` for all; shape / adjust / priority statistics as
  in 1.6 (camfiles.py re-run). Pointer table `8C1958B4` (8 acts x 4 B per level) as described; the name builder
  `8C09CD6E` falls back to the first letter (S) when the character's file is missing (Ghidra).
* **Volume tests**: sphere `8C01A1AC` = `|playertwp[0].pos + (0,4,0) - centre|^2 < entry+0x1C` (no sqrt, no
  re-squaring); the scan walks entries count-1 .. 1 (entry 0 never tested; `8C01951E` takes entry 0's mode at creation).
  Plane `8C01A240`: bounding-sphere pre-test with `size.x^2 + size.y^2`, then the crossing is tested between the
  player's positions **1 and 2 steps ago** (`8C07CE82(0, 1/2)`, history `B_8C78C690`), i.e. one step late.
* **Callers**: `8C01AD2C` 44, `8C01ABDC` 33, `8C01AE60` 47 caller functions (callers_all.py re-run; lists match 1.8).
* **Subset size**: the closure of the T3a roots is 90 functions / 26,718 B; + `8C01E92A` (76 B, table function 2) +
  `8C0213E0` (54 B) = **92 / 26,848 B** as stated. Core closure 55 / 12,276 B as stated.
* **Ninja dependencies**: of all library functions the subset calls, LIB rt already has every matrix / vector / draw
  function (`njRotateX`, `njInvertMatrix`, `njDistanceP2P`, `njProjectScreen`, ... in `rt_matrix.c` / `rt_vector.c` /
  `rt_draw.c`); exactly the 9 listed in 4.5 are missing (`8C63E1D4` is a *fatal* stub today, `8C63EBC8` void).
* **Paths**: path library 17/17 matched; orders `8C07BF00-8C07C300` 13/13 matched (order 1 adds 5.0 to `spd.x`, order
  26 calls `8C089AA0`, order 29 calls `8C089A40`); per-character order handling re-read from the six handlers' case
  lists (Knuckles no 2; E-102 no 29; Amy and Big no 2 / 29) and the event-path modes 80 / 58 / 49 / 55 / 45 / 52
  (`ewp->list4C != NULL` in each handler). 87 paths / 8,335 points (paths.py re-run). Path-work objects 16 functions /
  4,446 B, 15 todo / 4,436 B. Locks `8C07C1B4 / 1EE / 240 / 270` as described; every player gets an `eventwk` from
  `8C06C300 -> 8C04B440`, so locks and queues are usable in LIB worlds.
* **Transitions**: TIME = timer x 0.008333 (120 steps); the adjust runs only while `8C6B7078 > 0x3C` (`8C019F4A`).
* **EC1 measurement** (1.9) reproduced with ec1_cam_analyze.py.
* **Reference coverage** (`refmodes.py`, approximate box / sphere tests, planes by bounding sphere only): the 15
  references enter only FOLLOW, KNUCKLES, POINT, KLAMATH and ASHLAND(_I) volumes (entry 0 POINT / KNUCKLES / FOLLOW):
  all inside the subset. The debug NORMAL cycle `8C01A5AE` is inert: its button mask `D_8C19E394` is 0 in the image and
  `8C01A5AE` is its only reader.
* **GMod**: `duplicator.lua` stores `DT = ent:GetNetworkVars()` (253-254), restores it (323, 827) and calls
  `PostEntityPaste(ply, ent, CreatedEntities)` (861-862). The 3.1 UNVERIFIED point resolves: a `SpawnableEntities`
  list entry with `KeyValues` reaches the entity (`sandbox/gamemode/commands.lua:844-848`: `ents.Create(ClassName)`,
  `SetKeyValue` before `Spawn`), but the entity's `SpawnFunction` is bypassed (spawn at hit + normal x 16). Spline nodes
  with `COLLISION_GROUP_WORLD` are already skipped by `cl_collision.lua` (`NO_PLAYER`, line 25).

### R2. Refuted or corrected

1. **`8C0522C0` is not undecompiled.** It has NON_MATCHING C (`src/1st_read/code_8C0522C0.c`, "1 instruction short")
   that LIB already lowers and runs every step: it is the land gather of the player collision (`8C028BDC`, `8C02A680`,
   `8C02B1A0`, ...; `libsa1/src/sa1_coladapt.h` header). status.py (and `tools/progress.py`, same `NM_RE`) misses it
   because its NM block contains a nested `#ifdef TARGET_PC ... #else`, and `NM_RE` stops at the first `^#else`
   (`nm_undercount.py`: 2 functions / 2,162 B in 1ST_READ are missed this way, the other is `8C105036`). Drop it from
   T3a, 4.5, 7 and the byte count.
2. **T1c is mostly done.** `g-sa1/addon/lua/gsa1/cl_character.lua:470-472` already returns from `CalcView` when
   `ply:GetViewEntity() ~= ply` (file written today 14:41, before this doc). The "known bug" in 1.10 is stale; T1c
   shrinks to input / HUD behaviour while viewing through a camera plus an owner test.
3. **Five, not six, adjust functions**: `8C024D8E`, `8C024DCA`, `8C025010`, `8C0252A2`, `8C02535E` (section 0 says 6,
   4.5 says 5).
4. **Sizes** (xrefs): KLAMATH `8C020D60` is 0x282 (not 0x23C); `8C01ABDC` is 0x150 (not 0x11E). `8C01976C` has 21
   callers (4 in 1ST_READ, 17 in overlays), not 4.
5. **"Camera native" replay on all 15 references** is not possible in LIB: the 5 `obj_*` references are harness-only
   (their SET objects are not replayed; `libsa1/tests/build_replay.py` replays M1 + M11 + P7 = 10, plus 14 HOSTOBJ
   references). Use those **24** LIB-replayable references (all record `px_cam_raw` per step at player entry,
   `harness/src/t_sim.c:816`). And "no new recordings" holds only for FOLLOW / KNUCKLES / POINT / KLAMATH / ASHLAND:
   FIXED (the main preset), LINE, NEWFOLLOW, PATHCAM, SURVEY and COLI walls are entered by no reference, so the
   synthetic-CAM references of section 6 are **required** for T3c, not optional.
6. **CAM table install order (4.4) is wrong.** Creation itself reads the table: `8C01957E` loads `8C6B7060/64` from
   `8C1958B4[level][act]` and calls `8C01951E`, which dereferences `*8C6B7064` (entry 0's mode) - so "write the table
   and set `8C6B7060 / 8C6B7064` after creation" reads garbage at creation. Also the scan `8C01995E` and the COLI helper
   `8C01B684` gate on the **static** `8C1958B4[GetStageNumber()] != 0`, not on `8C6B7060`. Correct mechanism: before
   `8C01957E`, point the world's `8C1958B4[level][act]` word at the table (LIB already patches `DeathZoneTable` per
   world the same way, `sa1_coladapt_world.c:301`). Levels 11, 13 and 14 have no CAM row (all zero).
7. **`8C60957E` is not a Ninja camera-rotation function**: it reads as Ninja's camera-motion player (motion data:
   frame vs count, type bits 0xC0, interpolation table `8C6734DC`; callees `8C609264..8C6094E0`, `8C650874`), entered
   by `8C01A100` only when `8C6B706C != 0`; the only code with pool references to `8C6B706C` is `8C01957E` (zeroes it),
   `8C0197BA`, `8C01A100`, `8C01B640` (no direct callers). Not needed for T3: keep a fatal stub; move it to machinima
   (cutscene camera motion). With its motion callees it is larger than T3b's "S-M" suggests.
8. **SNOWBOARD `8C01F4CE` (1,152 B) is inside the 92-function union** (called by the board-trick cameras
   `8C01FB98` / `8C01FD6C`), although 4.5 lists SNOWBOARD among the excluded modes. The byte total already includes it.
9. **Ride table "all 6"**: the owner's roster has 7 characters (+ Super Sonic). Tikal (`TIKAL_PROG`,
   `src/overlays/tikal_prog/code_0CB00780.c`) handles only order 26 and the event path (mode 9) - no 1 / 2 / 29 / 31.
   E-102's order 2 gives mode 3 / act 3 (not balled). Super Sonic: UNVERIFIED.
10. **"then it is freed" (5.4)**: LIB's extension segment is a bump allocator without per-allocation free
    (`sa1g_world_ext_alloc` / `sa1g_world_ext_reset` only, `libsa1/src/world/sa1_world.c:964-982`; 16 MB per world).
    A 36 KB upload per ride leaks: about 450 rides of a 1,000-point spline per world session; CAM re-uploads leak too.
11. **`8C06B752` (hang trigger)** is the exec of no stage path (paths.py: execs `8C06B102` x14, `8C06B43C` x3, path
    cameras); 2.5's "who posts HangDownFromPathP: `8C06B752`" is unattested in the stage data (harmless for T4).

### R3. Gaps

1. **Camera functions the playable characters reach are missing from the subset.** With `SA1_CAMERA_GAME` the
   characters' camera calls stop being void stubs (`gen/policy/stubs.tsv`) and the pushed functions run. Not in the 92:
   * `8C021700` (0x146): pushed by `8C07BC0E` (the death path after `8C07BBC2`, i.e. every death zone and the Kill
     action) and by Sonic `8C0B1352` (Light Speed Attack, `code_8C0B11C0.c:133`);
   * `8C0246EA` LEAVE (0x2A): Knuckles `8C01ABDC(68, 0)` in his mode 11 (`code_8C0C193A.c:418`, `code_8C0C450A.c:66`);
   * `8C0239C2` FISHING (0x108) and `8C0EAD6A` (0xA0, Big's camera hook, undecompiled): Big's fishing;
   * `8C01A7A4` (0x32, Sonic `code_8C0A77BC.c:797`), `8C019368` (0xE, Tails `code_8C0B6148.c:171`).
   6 functions / 856 B. Without them a world calls an address-only function on its first death, LSA, Knuckles mode 11
   or cast. Optional: `8C01B640` (68 B) + COLLI `8C024582` (4 B).
2. **T4 path cameras** also need `8C01B35E` / `8C01B36C` (14 B each, camera range, called by `8C06BAD2`).
3. **Live trigger edits vs entry indices.** The camera keeps CAM entry indices (control work +4, per-level slots +0x30)
   and re-reads the entry each step (`8C01B308(8C6B7064 + entry x 0x40)` in `8C019F4A`). Removing or reordering entries
   while the character is inside one leaves a stale index. Keep indices stable (append; a deleted trigger becomes a
   never-matching dummy, e.g. sphere with +0x1C = -1) or pop / re-init (`8C01951E`, a cut) on change. The game's slot
   is 16 KB (255 entries); `count` is Sint16, mode Sint8 (77 fits).
4. **Visibility coupling.** The game camera's view lands in XMTRX and drives `8C038D00`, hence the display path
   (drawing, and Tails' / Knuckles' display-side logic, `visibility_fidelity.md`) and the capture. With a far or FIXED
   trigger camera the character can be off-screen: the capture falls back to the CPU pose and other players see that.
   Under `SA1_CAMERA_GAME` LIB must also stop `sa1g_camera_refresh` / `sa1g_camera_view` (`sa1_world_tick.c:52, 83`),
   which the doc does not say. Objects measure spawn / LOD against `D_8C18B7EC` (`v1_objects.md` lines 119 and 227):
   a far camera despawns objects near the character (faithful, but it couples T3 with the objects round).
5. **HOST mode feel.** Showing the chase camera through the 60 Hz game camera (+ interpolation + `8C01BD9E`'s own
   collision on top of G-SA1's hull traces) changes today's per-frame chase camera. Let `CalcView` keep its per-frame
   chase camera while the level is NORMAL with HOST and no blend runs; use `sa1_character_camera` only for trigger /
   event cameras and blends (HOST still feeds the game the same yaw, so the stick frame does not change).
6. **L / R reach the camera already**: `sa1g_pad_from_input` passes `SA1_BUTTON_L / R` as full analog triggers to every
   world (`sa1_world_tick.c:256-257`), which is the game camera's own rotation input; `SA1_CAMERA_FREEKEYS` must gate
   them, or HOST mode loses control whenever G-SA1 binds L / R. The free-camera masks of `8C019ADC` (0xF0 / 0x606, stick
   `8C754C24/26` = PDS x1 / y1) need the same audit.
7. **Architecture mismatch with v1_arch.** v1_arch 4.2 / line 451 puts triggers and splines on the shared catalogue
   (`feat/sh_objects.lua`, `gsa1_obj_base`, generated classes, the `ENT.GSA1Object` exclusion) and 4.3 stores "spline
   points" in entity modifiers; this doc uses standalone `lua/entities/gsa1_trigger` subclasses and one
   `gsa1_spline_node` entity per point. Pick one before T1a; nodes and triggers need `GSA1Object` so round 5's
   host-object scanner never classifies them. Neither doc says how the trigger feature registers through the G-SA1
   feature seam (`feat/`, `gsa1_feature.cpp`, `feature_page`).
8. **T1b dependencies**: LIB has `sa1_character_add_rings` but no lives or checkpoint API; those come from the objects
   round. Server plausibility checks rely on `sv_gsa1.lua` moving the parked player to the character, which it does
   only while its follow ConVar is on (`sv_gsa1.lua:153`).
9. **Camera switching before T3** (owner's actual request): a trigger action "view through a placed Camera-tool
   camera" (server `SetViewEntity` on enter, restore on leave) is GMod's own camera, not a stand-in of SA1 logic, and
   works now that `CalcView` yields. Offer it in T1 (owner question 9).
10. **Transition option "always smooth 0.5 s"** has no game adjust behind it: offer the game's own (NONE, TIME = 2 s, or
    the RELATIVE / THREE whose duration `8C025482 / 8C025524` gives, measured) or drop it.
11. **Stage number**: a world must keep a level whose `8C1958B4` row is non-zero (live worlds use level 1; levels 11,
    13, 14 would disable the scan unless LIB patches the row, R2.6).
12. **Tooling**: `tools/progress.py`'s `NM_RE` undercounts NON_MATCHING code with nested `#if` (R2.1).

### R4. Bytes still to decompile (corrected)

| Item | Functions | Bytes |
|---|---|---|
| T3a camera subset as stated | 92 | 26,848 |
| + character-reached camera functions (R3.1) | 6 | 856 |
| + `8C04FE4A` (or keep a LIB policy) | 1 | 32 |
| - `8C0522C0` (already NON_MATCHING C, live in LIB) | - | (was 600) |
| T4 path-work objects + `8C01B35E` / `8C01B36C` | 15 + 2 | 4,436 + 28 |
| **Total** | **116** | **32,200** (doc: 31,916) |

`8C60957E` and its Ninja motion callees are clean-room LIB work, not counted (and not needed for T3, R2.7).

### R5. Required changes to the design

1. Remove `8C0522C0` from T3a / 4.5 / 7; add `8C021700`, `8C0246EA`, `8C0239C2`, `8C0EAD6A`, `8C01A7A4`, `8C019368` to T3a
   (98 functions / 27,704 B), and `8C01B35E` / `8C01B36C` to T4. Fix the section 0 count of adjust functions (5) and the
   two sizes in 1.1 / 1.4.
2. Rewrite the 4.4 implementation note: install the CAM table by patching the world's `8C1958B4[level][act]` (and
   `8C1958A8` for the HOST row) **before** `8C01957E`; keep entry indices stable across edits (R3.3); cap at 255 entries.
3. LIB: allocate the CAM table and the path buffers once per world (a fixed pool with its own free list in the
   extension segment, or one buffer per spline ID reused) - the extension segment cannot free.
4. T1c: mark the CalcView yield as done; keep only input / HUD while viewing through a camera; add the "view through
   a Camera-tool camera" trigger action to T1 (R3.9).
5. T3c: switch off `sa1g_camera_refresh` / `sa1g_camera_view` under `SA1_CAMERA_GAME`; gate L / R and the free-camera
   masks with `SA1_CAMERA_FREEKEYS`; keep the per-frame chase camera for NORMAL / HOST (R3.5); state the visibility /
   capture consequence for off-screen characters and decide (keep the game's behaviour, or force the display pass).
6. Verification: "camera native" against the 10 strict + 14 HOSTOBJ LIB references (not the obj_* ones); the
   synthetic CAM references (FIXED, LINE, ASHLAND_I, COLLI walls, SURVEY) are mandatory; add a death-zone and an LSA
   reference with the game camera on (exercises `8C021700`).
7. Ride table: add Tikal (Steer, Ride and Drive only) and mark Super Sonic UNVERIFIED; E-102's "Path roll" is not a ball.
8. Align with v1_arch on one entity model (catalogue / base / modifiers vs standalone classes / node entities) before
   T1a; tag trigger and node entities `GSA1Object`.
9. Drop or re-base the "always smooth 0.5 s" transition on a game adjust.
10. Name `8C60957E` correctly (camera motion) and move it out of T3b.

### R6. Verdict

The direction holds: the camera data model, the CAM file format, the priority stack, the callers, the subset size,
the fully decompiled path system and the "splines first" order are confirmed by the asm, the disc files and the LIB
code. T2 (splines / rides) is ready to start as written, with the allocator fix (R5.3) and Tikal added. T3 is not
ready as written: the subset misses six camera functions that ordinary play reaches (death, Light Speed Attack,
Knuckles, Big's fishing), the CAM install order would read garbage at creation, live trigger edits can leave stale
entry indices, and the verification set must be the 24 LIB-replayable references plus mandatory synthetic CAM
references. T1c is already in the tree. Bytes still to decompile for this topic: **32,200 B** (T3a 27,736 incl.
`8C04FE4A`, T4 4,464), not 31,916.
