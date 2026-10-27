# G-SA1 0.6.0 diagnosis, topic "board": Sonic and Tails should have board modes

Owner request (0.6.0 test, 2026-10-21): "Sonic and Tails should have Board modes". This is a read-only diagnosis and
feature design. Nothing in SADC/src, SADC/include, SADC/tools, SADC/harness, SADC/config, LIB or g-sa1 was edited. The
experiments are in `SADC/build/research/board/` (section 10). Abbreviations: LIB = `libsa1`, SADC = `sadc`.

## 0. Summary

**How the game does it.** Board mode is the game's own player mode. Stage code enters it in two steps:

1. `SetInputP(0, 44)` (`8C07C65E`) posts order 44.
2. `CreateElementalTask(3, 2, init)` creates the board task. The init function is `8C0B2EE6` for Sonic and `8C0BF7CE`
   for Tails.

Three places in the game do exactly this:

* the Sand Hill level task (SBOARD `0C9031AE`);
* the Ice Cap act 3 start task (STG08 `0C90EA14`);
* the race AI, for player 1 (`8C07FA54`).

Order 44 starts these modes:

| Character | Modes | Board actions | Board actions in the board task |
|---|---|---|---|
| Sonic | 62-68 | 102-124 | 102-124 |
| Tails | 48-54 | 107-127 | 107-127 |

The game leaves board mode with `SetInputP(0, 24)`: Ice Cap's goal check `0C90E900` does it.

**The physics is free 3D, not path-bound.** The board modes use the normal `PGetSpeed` / `PSetPosition` land
collision with four board accelerations in player core B: `8C0700C2` ground, `8C07095E` turn/brake, `8C07061C` air and
`8C070814` trick jump. Steering is relative to the camera taskwk `D_8C18B7EC`. The board feel comes from the land's
COL attribute:

* Ice Cap's snow, `0x8`: weight 0.15;
* Sand Hill's sand, `0x10`: weight 0.5;
* anything else: weight 1.0. The board then barely accelerates.

**What already works (reproduced).**

* The harness plays all four cases from the game's own stages. The board references below are bit-identical with the
  1,559 decompiled functions routed natively.
* A LIB world on host geometry rides the board when the probe posts order 44 and creates the board task.

**What breaks today (reproduced).**

1. **The free-object step `8C05872A` is not decompiled.** LIB stubs it as fatal, and it is on the entry path: Sonic's
   action keeps its old value for several steps after order 44 (15 in the Sand Hill reference, 4 in Ice Cap, about 8
   in LIB), and the board task runs `8C05872A` until then.
   Result: `SA1_ERR_NOT_IMPLEMENTED` at the first board pair, and again after getting off.
2. **On snow (COL `0x8`) or ice (`0x4`) the board calls STG08's snowball spray `0C900AE0`.** It is a fatal stub. The
   world dies within 1-3 s of leaning.
3. **On plain GMod land the board cruises.** It stays at about 1.0 unit/step on flat ground and on 15° slopes, and
   sticks on 30° slopes.
4. **The board is drawn untextured.** Its three textures are standalone PVR files: `MIW_B001.PVR`, `SB_BOARD1.PVR` and
   `MTX_BOARD0.PVR`.
5. **The board sounds are wrong under G-SA1's Emerald Coast bank.** The snow sounds are silent, and sand plays an
   Emerald Coast loop.
6. **Tails' jump grows on each re-entry.** `p.jmp_y_spd` is multiplied by 1.2 at each entry and never restored.

**Design.** A LIB call `sa1_character_board()` does exactly what the stage code does. Around it:

* per-world board **terrain** (snow / sand / the map's own surfaces);
* the board PVR textures as archives;
* a void policy for the STG08 effect;
* 5 functions to decompile (`8C05872A` and its closure, 1,614 bytes);
* a G-SA1 feature with a bindable key, a pad button and an Options > G-SA1 > Board page.

Verification:

* four new harness references (Sand Hill and Ice Cap act 3, Sonic and Tails);
* a strict LIB replay of the Sand Hill ones;
* a LIB `board_test` whose cases fail today and pass after the fixes.

---------------------------------------------------------------------------------------------------------------------

## 1. How the game enters, plays and leaves board mode

### 1.1 Who enters it (stage code, all assembly only)

**SBOARD (Sand Hill, level 38) level task `0C903148`** (`SADC/asm/overlays/sboard/text/0C903148.src`), a 4-state machine
on `twp->mode`:

```c
/* reconstructed from 0C903148.src; addresses are the original's */
case 0: func_0C9031AE(); twp->mode = 1;            /* falls into case 1 */
case 1: if (func_8C0A0C40(77, 8)) twp->mode = 2;   /* role UNVERIFIED (a wait) */
        break;
case 2: func_8C01937C(0x31C7);                     /* camera projection angle 70 deg (boot's 457.06 = 320/tan 35) */
        func_0C905480(); func_8C0509A0(); func_8C04C680(); twp->mode = 3; break;
case 3: func_0C903240(); break;                    /* every frame */

void func_0C9031AE(void) {                         /* the board start */
    SetInputP(0, 44);
    switch (GetPlayerNumber()) {                   /* character id */
    case 0: CreateElementalTask(3, 2, func_8C0B2EE6); break;    /* Sonic */
    case 2: CreateElementalTask(3, 2, func_8C0BF7CE); break;    /* Tails */
    }
}
void func_0C903240(void) {                         /* per frame */
    playerwk *pwp = func_8C07CFDE(0);
    func_0C90327E(pwp);                            /* speed FOV, below */
    if (playertwp[0]->pos.y < -7405.0f) { func_8C0A0C94(); func_8C07BD2A(0); }   /* course end, UNVERIFIED */
}
void func_0C90327E(playerwk *pwp) {                /* FOV from speed: 70 deg at rest, 100 deg from spd.x 7.82 */
    Float k = pwp->spd.x < 7.82f ? njSqrt(pwp->spd.x * 0.12787f) : 1.0f;
    func_8C01937C((Sint32)(12743.0f + 5461.0f * k));             /* FMAC; BAMS 0x31C7 .. 0x471C */
}
```

**STG08 (Ice Cap)**, all in `SADC/asm/overlays/stg08/text/0C90DD40.src`:

* `0C90EA14` (one-shot task) does the same `SetInputP(0, 44)` plus the board task for character 0 or 2, then
  `FreeTask` (`:1701-1735`).
* `0C90E900` (exec, `:1558-1700`):
  * when player 0 is on the ground within 30 units of (132, 375, -42), it calls `func_8C07C8E0(0, ...)` and creates
    the start push `0C90E810`;
  * within 200 units of (-3798, -23289, -5526) it posts `SetInputP(0, 24)` and calls the camera reset `8C01AE60`.
    That is the end of the board run;
  * it also eases the projection angle towards the same speed FOV, by at most `0x200` per frame (`AdjustAngle`).
* `0C90E810` (`:1458`) raises `pwp->spd.x` of players 0 and 1 to at least 4.0 for 100 frames, then frees itself.
* `0C90E4A0` (`:1016`), the act set-up, loads `s_sbmot.PRS` to `0x0CB08000` and `m_sbmot.PRS` to `0x0CADC000` for
  characters 0 and 2. `0x0CADC000` lies inside STG08's own image (`D_0CADC000`, STG08 ends `0x0CAEE49C`): M_SBMOT
  overwrites STG08's tail.

**Sand Hill's file loader** is `8C09CE90`, case 38 at `8C09D76E` in the level switch at `8C09CF28`. It loads
`sboard.PRS` to `0x0C900000` and, for characters 0 and 2, `s_sbmot.PRS` / `m_sbmot.PRS` to `0x0CB08000` /
`0x0CADC000` (`SADC/asm/1st_read/text/8C09CC80.src:1545-1570`). Ice Cap's case (`8C09D134`) loads only STG08: its act
set-up loads the board motions (above).

**The race AI** `8C07FA54` (`SADC/asm/1st_read/text/8C07F560.src:731`) does `SetInputP(1, 44)` plus
`CreateElementalTask(3, 2, 8C0B2EE6)` for the AI Sonic in Tails' Ice Cap race, with the board task's `counter.b[0] = 1`.
The board task's `counter.b[0]` is the player number (0 by `CAlloc` for the stage tasks).

Order 44 is posted nowhere else. All 66 SetInputP users were checked; the `#44` loads in ALERTWND / B_CHAOS4 are not
SetInputP arguments.

### 1.2 Order 44 in the characters (C, decompiled)

`SetInputP(pno, mode)` sets `twp->flag |= 0x1000; twp->smode = mode` (`SADC/src/1st_read/code_8C07C550.c:67`).

**Sonic.** `func_8C0AFF1A` case 44 (`code_8C0AFF1A.c:262-269`) sets mode 65 and `reqaction` 105 and clears flag 0x2000.
This handler is used by Sonic's normal modes. It is not used by the Super Sonic modes 71-78 (handler `8C0B0F24`), so
kind 8 cannot board.

**Tails.** `func_8C0BBDD6` case 44 (`code_8C0BBDD6.c:247-256`) sets mode 51 and `reqaction` 110, and also changes the
physics:

* `p.height += 2`;
* `p.center_height = height / 2`;
* `p.jmp_y_spd *= 1.2` (`0x3F999999`).

Order 44 is taken only by the normal handlers:

* Sonic `8C0AFF1A`: modes 1-17, 19, 32, 36-43, 46-61, 69, 70, 79;
* Tails `8C0BBDD6`: modes 1-15 (flight 15 included), 17, 19, 20, 32, 36-42, 44-47, 55-57, and 18 / 21 / 26 / 33 / 61
  while not holding.

It is not taken by:

* the holding / carrying handlers:
  * Sonic `8C0B07E4`: modes 21-23, 25-29, 33-35;
  * Tails `8C0BC61C`: modes 22, 23, 25, 27-29, 34, 35, and 18 / 21 / 26 / 33 / 61 while `flag & 0x800`;
* `8C0B0F24`: Sonic modes 20 and 71-78 (Super Sonic);
* modes without a handler: Sonic 18, 24, 30, 31, 44, 45, 80, 83; Tails 16, 24, 30, 31, 43, 58, 62.

The mode lists come from a script over `code_8C0A966C.c` and `code_8C0B70F4.c`: the first handler call in each case.

Orders live for one player exec. `func_8C06C6F8`, the last call of every main (`code_8C0A77BC.c:977`,
`code_8C0B6148.c:622`), clears `twp->flag & 0x1038` (`code_8C06C6A0.c:76`). An order that the current mode's handler
does not take is lost: in the traces, `flag` is `0000` again after step 1. The stage code posts the order from a task
that runs after the player, so the player sees it in the next step. A character who is holding something never boards. The
stage's board task is then left over as a free object.

### 1.3 Board modes and actions

| Sonic mode | Tails mode | role | mode machine (exits) | physics (main) |
|---|---|---|---|---|
| 62 | 48 | ride | jump (A, `press & D_8C19E390`) -> 66/52; jump on an id-9 object (jump ramp) -> 67/53 trick, actions 117-123 / 121-126 and score `8C0A77A0` / `8C0B6120` (game mode 9 only); stick away from the facing (`8C06CE40`) -> 64/50 | `8C0700C2(max 5.98)`, PGetSpeed, PSetPosition (2 = wall: crash 68/54, action 109/110 / 114/115, SE `0x458` / `0x469`, at stage `0x802` also SE 235); `8C0AEBC0` / `8C0BB68A` lean actions 107/108 / 112/113 (mtnmode 12); `8C07B694` effects |
| 64 | 50 | turn / brake | jump on release; back to 62/48 | `8C07095E` (stick = braking `-run_accel * mag`, lean), PGetSpeed, PSetPosition, `8C07B694` |
| 65 | 51 | airborne (entry: board drop, action 105 / 110) | landing -> 62/48 (action 102/107, or 113/118 if `spd.y <= -3`; `spd.x` set to 1.0 if <= 0.3) | `8C071D40`, `8C07061C`, PGetSpeed, PSetPosition |
| 66 | 52 | jump | landing -> 62/48 | as 65 |
| 67 | 53 | trick jump | landing -> 62/48 | `8C070814` |
| 68 | 54 | crash (wall, or damage: `8C0AFB10` maps modes 62..68 to 68 / action 110, `code_8C0AFB10.c:100`) | when the crash action ends -> 62/48 | as 64 |

Files:

* Sonic: `code_8C0A966C.c:1451-1665` (mode machine), `code_8C0A77BC.c:727-786` (physics);
* Tails: `code_8C0B70F4.c:1218-1430`, `code_8C0B6148.c:476-533`.

Every board mode first calls the board order handler (Sonic `func_8C0B0E94`, `code_8C0B0E94.c:17`; Tails
`func_8C0BCBCA`, `code_8C0BCBCA.c:56`). It takes only two orders:

* **24 (get off):**
  * Sonic: mode 12, action 18;
  * Tails: mode 10, action 19, `p.height = 9`, `center_height = 4.5`.
* **19 (goal / stop):**
  * Sonic: mode 20, action 75;
  * Tails: mode 62, action 54/56 from the race result `8C0818DE`.

The handler ignores every other order: springs, the interaction layer's orders, and so on. Tails' order 24 does not
undo `jmp_y_spd *= 1.2`.

Order 45 (Sonic mode 69, action 125; Tails mode 55, action 128, both mtnmode 10) is **not** board. Event overlays
EV0007-EV0016 load 45 (meaning UNVERIFIED).

The board actions live in the motion files:

* Sonic 102-124: all in S_SBMOT (`0x0CB099D8 .. 0x0CB46FD4`);
* Tails 107-127: all in M_SBMOT (`0x0CADD738 .. 0x0CB0258C`).

Dump: `SADC/build/research/board/dump_actions.py`. Actions 107/108 (Sonic) and 112/113 (Tails) are mtnmode 12: the
lean, whose frame the code sets from the stick.

On the board, A both crouches (held: action 124) and jumps (press in 62, release in 64). The probe confirms that
`SA1_BUTTON_JUMP` jumps. The naming of `D_8C19E38C` / `D_8C19E390` in `code_8C0A966C.c` ("jump" / "action") looks
swapped for this use (UNVERIFIED).

### 1.4 The board task (C, decompiled)

**Sonic** (`code_8C0B2EA0.c`):

* `func_8C0B2EE6` (`:78`), init:
  * `twp->timer.ptr` = `S_SBMOT_Sonic_Snowboard` (`0x0CB37454`);
  * `twp->value.ptr` = a texlist picked by `frame counter % 2`: `D_8C1C6380` (1ST_READ, texture `miw_b001`) or
    `0x0CB36F94` (S_SBMOT, texture `sb_board1`);
  * `mwp->weight = 0.02`;
  * texture load `8C608C82`;
  * installs exec `func_8C0B2F46` (`:114`), disp `lbl_8C0B3068` (`:151`) and dest `func_8C0B31C6`.
* While the player's action is 102..124, the exec glues the board to the player's first collision centre, with the
  angles (y mirrored) and the speed, and clears `flag & 8`. Otherwise, unless `flag & 8`, it runs the free-object step
  `func_8C05872A` (with a 5.22 y offset).
* The display:
  * draws the static model, or the board motion `D_8C1C6318[action - 102]` through `8C60E080` at the player's frame;
  * NULL entries (102, 113-115, 124) mean `njDrawObject`.

**Tails** (`code_8C0BF262.c:291`, `code_8C0BF804.c:138-210`): the same with `M_SBMOT_Tails_Snowboard` (`0x0CB029C4`),
texlist `0x0CB025A0` (`mtx_board0`), actions 107..127, offset 5.19 and table `D_8C2579BC`. Two differences:

* **no `flag & 8` test:** out of range it always runs `8C05872A`;
* it frees itself when the player slot is empty.

The textures are standalone PVR files on the disc, loaded by texname. Mapping a texname to `<NAME>.PVR` is expected
Ninja behaviour (UNVERIFIED); the files exist:

| file | size | SHA-1 | format |
|---|---|---|---|
| `MIW_B001.PVR` | 10,952 | `7eb21e78fa9d54ef00029043be91285bdde1de32` | GBIX 500 |
| `SB_BOARD1.PVR` | 10,956 | `d851ade3e5d8b610790a25aca1e8960aeae9bde6` | GBIX 1004, RGB565 twiddled mipmapped, 64x64 |
| `MTX_BOARD0.PVR` | 10,956 | `f41d0bf31bc1af46cf683bc55343161d519fee7a` | GBIX 1224, RGB565 twiddled mipmapped, 64x64 |

### 1.5 Physics, surfaces, effects and sounds

`8C0700C2` (`code_8C0700C2.c:28`):

* gravity weight in player space;
* air resistance above the 5.98 maximum;
* steering;
* forward push `run_accel` only while `spd.x <= jog_speed` (0.46);
* friction `lim_frict` / `grd_frict_z` times `mwp->weight`, applied only when there is a forward acceleration.

Steering uses `D_8C18B7EC->ang.y` and `->smode`. The lateral stick component relative to the camera turns the board,
so the camera yaw itself does not matter for steering. The probe gave identical paths at camera yaw 0 and 90°. But
"stick away from the board's facing" (`8C06CE40`) means brake (mode 64): with the camera in front of the board, the
forward key brakes. The probe at yaw 180° dropped from 1.0 to 0.46.

Land attribute (dig topic `gsa1_060_dig.md` section 3), `8C0751C0` (`code_8C0751C0.c:69`) sets `mwp->weight`:

| COL bit | `mwp->weight` |
|---|---|
| `0x8` | 0.15 |
| `0x4` | 0.1 |
| `0x10` | 0.5 |
| `0x80` | 1.5 |
| otherwise | 1.0 |

In addition, `8C074CF0` scales ground gravity by 0.3 on `last_attr & 8`.

`func_8C07B694` (`code_8C07B500.c:157`) runs in the ride modes:

* mtnmode 12 (leaning) and `spd^2 > 3`:
  * `attr & 0xC`: 5 % per step **STG08 `0C900AE0`** (16 snowball tasks with STG08's `STG08_Snowball` and texlist
    `D_0C914188`), and SE 237;
  * `attr & 0x10`: SE 691.
* Spray behind the board:
  * `attr & 8`: `8C09E474` (1ST_READ, generic particle list `D_8C1C42F0`), which works in LIB;
  * `attr & 0x10`: **SBOARD `0C900080`** (SBOARD's own list and exec `0C905140`).

Sound tones:

* bank 1, per-stage range 208-238: 230 / 231 (jump ramp), 235 (crash, Ice Cap only), 237 (snow);
* bank 1, range 690-698: 691 (sand);
* voices: `0x542`.. `0x576` (tricks).

---------------------------------------------------------------------------------------------------------------------

## 2. What the board needs from the stage

| need | game | for "board anywhere" |
|---|---|---|
| level number | `GetStageNumber() == 0x802` only adds crash SE 235; Tails' level set-up `8C0BFAEE` (Sand Hill: height 9) is stage start, not board | none (the world stays level 1) |
| SBOARD / STG08 tasks | the entry (1.1), the Ice Cap start push and goal, the speed FOV, the course objects (jump ramps id 9 -> tricks, cacti, sand worm, avalanche) | none required. Entry = the same two calls (5.1); FOV optional (5.6); ramps later (host objects with id 9, section 8) |
| camera | `D_8C18B7EC` must exist (dereferenced with any stick input) | LIB's camera adapter (always there); host camera behind the board recommended (1.5) |
| path data | none: free 3D land collision | none |
| land | snow `0x8` / sand `0x10` for the game's feel | terrain override or material mapping (5.2) |
| board motions / model | S_SBMOT / M_SBMOT, loaded by the stage | LIB loads both (`SA1_LOAD_BOARDS`, default, in G-SA1: `session.cpp:271`); world images hold them |
| board textures | 3 standalone PVRs | LIB loader (5.3) |
| effects | STG08 `0C900AE0` (snowballs), SBOARD `0C900080` (sand trail) | v1 void; v2 with the stage image (section 8) |
| sound bank | ICECAP.MLT / SANDHILL.MLT | switch the stage bank while boarding (5.4) |
| trick camera | `8C01AD2C` hooks + `D_8C112048[0x12] = 0` | hooks are void stubs; `D_8C112048 = 0x8C6B6EEC` (1ST_READ BSS, in the arena): safe |

---------------------------------------------------------------------------------------------------------------------

## 3. What SADC and LIB already have

**C coverage.** All of the board's player-side code has C and runs natively in LIB:

* `SetInputP`;
* both order handlers;
* both mode machines and mains;
* `8C0B0E94`, `8C0BCBCA`;
* `8C0700C2`, `8C07061C` / `8C070814`, `8C07095E`, `8C071D40`, `8C07B694` (NON_MATCHING, register allocation only);
* `8C0AEBC0`, `8C0BB68A`;
* both board tasks (`8C0B2F46` NON_MATCHING, 4 instructions);
* `8C0A77A0`, `8C0B6120`, `8C07B320`, `VectorAngle`, `8C07C9D2`, `8C07CB54`, `8C10D058`.

Measured with harness coverage. The four board traces execute 458 1ST_READ functions. The 215 of them without C are
the camera system (`8C019xxx-8C01Fxxx`), sound, display, libm, the race AI and **`8C05872A`, `8C058D3C`, `8C058DD4`**
(`SADC/build/research/board/not_native_1st.txt`).

**Missing (required).** The free-object step `8C05872A` and its closure, all in chunk
`asm/1st_read/text/8C0586A0.src`, functions 2-6 of 29, 1,614 bytes:

| function | size (bytes) |
|---|---|
| `8C05872A` | 752 |
| `8C058A1A` | 688 |
| `8C058D20` | 28 |
| `8C058D3C` | 30 |
| `8C058DD4` | 116 |

Its callees `8C056A04` (MSetPosition core), `8C02F63C` (CCL_Entry) and `8C042DC4` (LIB policy `ret 0`) are
available. LIB: `gen/policy/stubs.tsv:116` `fatal`. It is not queued in any workflow or staging.

**Overlays.**

* **SBOARD** is the **Sand Hill stage overlay**: landtable `sboard00` at `0x0C902DEC`, object list `0x0C902E58`,
  stage texlist `texlist_SANDBOARD`, cacti / gates / jump panels / sand worm models. Split into
  `asm/overlays/sboard/text` (19 files, `0C900000-0C905864`) and data. No C.
* **STG08**: split, no C.
* **S_SBMOT / M_SBMOT**: data only. LIB manifest `src/sa1_manifest.c:21-22`; world images
  `src/world/sa1_world_image.c:27-29`.

LIB policies:

* `0C900080` `void` (`stubs.tsv:42`);
* `0C900AE0` `fatal` (`stubs.tsv:47`);
* `8C062620` / `8C062704` `fatal` (only Tails' level set-up, not reached).

The motion draw `8C60E080` is in `src/rt/rt_draw.c:843`.

**Code-map corrections** (docs, fix F8). `sa1_code_map.csv:122` calls SBOARD a "board objects" overlay that "calls
Sonic 8C0B2EE6 and Tails 8C0BF7CE draw hooks". Correct reading:

* SBOARD is the Sand Hill stage;
* `8C0B2EE6` / `8C0BF7CE` are the board task's **init** (model, texture, exec / disp / dest);
* `sa1_code_map.md:220`: `8C0700C2-8C07095E` are the **board** accelerations, not "1-D (path) physics".

---------------------------------------------------------------------------------------------------------------------

## 4. Reproductions

### 4.1 Harness: the game's own board runs, and the decompiled C reproduces them

**Scripts.** Four scripts in `SADC/build/research/board/` (format of `harness/tests/sim`, `# args:` line, `--no-objects`):

| script | stage and character |
|---|---|
| `board_sh_sonic.txt` | Sand Hill, level 38 act 0, Sonic |
| `board_sh_tails.txt` | Sand Hill, level 38 act 0, Tails |
| `board_ic3_sonic.txt` | Ice Cap act 3, level 8 act 2, Sonic |
| `board_ic3_tails.txt` | Ice Cap act 3, level 8 act 2, Tails |

Each script is 411 steps: 120 neutral, forward, carve left and right, jump, hold A.

**Recording.** Recorded with `harness/build/sa1h.exe` (pure interpreter) into `ref_*.bin`. Each
`sa1h_nm_pc.exe ... --route --check ref_*.bin` (1,559 native functions) reports **"identical (411 steps)"** for all
four. Results:

| ref | modes | actions | max `spd.x` | land attr |
|---|---|---|---|---|
| board_sh_sonic | 1, 62, 65, 66 | 0, 102, 105-108, 111, 114, 115 | 4.76 | `A0000011` |
| board_sh_tails | 1, 48, 51, 52 | 0, 107, 110-113, 116, 119, 120 | 3.51 | `A0000011` |
| board_ic3_sonic | 1, 62, 64-66 | 0, 102-108, 111, 112 | 7.50 | `A0000009`, `C0000009`, `20000009` |
| board_ic3_tails | 1, 48, 50-52 | 0, 107-113, 116, 119, 120 | 7.50 | same |

The step at which order 44 arrives: step 1 (`smode` 44, `flag` `0x1000`) in all four.

Sonic's `mj.action` stays 0 until step 16 (Sand Hill) or step 5 (Ice Cap), while the motion links. In those steps the
board task runs `8C05872A`: the coverage lists `8C05872A`, `8C058D3C` and `8C058DD4` in every trace. Tails' action
becomes 110 at step 1.

Earlier exploratory runs `ic3_sonic`, `ic3_tails` and `sh_sonic` (420 steps) are also identical when routed.

### 4.2 LIB: board mode on host geometry (`board_probe.c`)

**What the probe does.** It builds against a locked snapshot of `LIB/build/x86_64-windows/libsa1.a`. It runs a Sonic or
Tails world on a 64-unit grid ground (flat or sloped). Inside `sa1g_world_call` it does what the stage does:
`SetInputP(0, 44)` plus `CreateElementalTask(3, 2, 8C0B2EE6 / 8C0BF7CE)`. Then 200 pairs:

* stick neutral, then forward from pair 60;
* right 0.7 at pairs 120-149;
* jump at 170.

**Stand-ins (probe only, not the game).** `PROBE_DEFER_BOARD` creates the board task once the action is a board action;
`PROBE_FREE_AT_EXIT` frees the task at the exit. Both avoid the `8C05872A` fatal. Pair numbers count from the
character's creation; the board starts at pair 10.

| case (Sonic) | `pspd.x` at pairs 20 / 60 / 100 / 150 | result |
|---|---|---|
| as the game (no stand-in), flat SOLID | - | **`SA1_ERR_NOT_IMPLEMENTED`: `func_8C05872A` at pair 10** (first board pair) |
| flat, any surface | 1.000 / 1.000 / 1.000 / 1.000 | cruises forever at 1.0 (the friction term needs a forward acceleration) |
| 15° SOLID | 0.987 / 0.987 / 0.987 / 0.987 | no acceleration downhill |
| 15° SAND (`0x11`) | 1.17 / 1.96 / 2.73 / 3.61 | glides; SBOARD trail is the `void` stub |
| 15° snow (raw `0x80000009`, Ice Cap's word) | 1.32 / 2.71 / - | **fatal `0C900AE0` at pair 75** |
| 15° SLIPPERY (`0x5`) | 1.34 / 2.82 / - | **fatal `0C900AE0` at pair 89** |
| 30° SOLID | 0.035 / 0.035 / 1.53 / - | stuck until the stick pushes |
| 30° SAND | 1.45 / 3.87 / 6.00 | |
| 45° SOLID | 0.91 / 4.15 / 6.02 | |
| exit (`SetInputP(0, 24)`), without the stand-in | mode 12 / action 18, then mode 2 run at 2.45 | **fatal `8C05872A` on the next pair** |
| camera yaw 0 / 90° / 180° (flat sand) | 0 and 90 identical; 180 brakes to 0.46 (mode 64) | |

**The board draw.** One draw of 44 triangles, **archive -1 (untextured)**, present from the pair after the board
task's init. It comes from the model path when the board action has no motion, and from the motion draw otherwise.

**Tails** (15° sand, enter / exit / enter): `p.jmp_y_spd` goes 1.66 -> **1.992** -> (exit) 1.992 -> **2.3904**.
`p.height` goes 9 -> 11 -> 9 -> 11. The `0C900AE0` and `8C05872A` behaviour is as for Sonic, without the `flag & 8`
skip: Tails' exec is fatal at the first board pair even with the probe's flag-8 stand-in.

### 4.3 Sound (`audio_probe.c`)

`sa1_audio_sound_info` under each stage bank:

| tone | default (`EMERALDCOAST.MLT`, G-SA1 0.6.0) | `ICECAP.MLT` (stage 8) | `SANDHILL.MLT` (stage 38) |
|---|---|---|---|
| 230 / 231 / 235 / 237 | none (silent) | program 22 / 23 (loop) / 27 / 29 | none |
| 691 | **EC program 1, looping 4.76 s (wrong sound)** | an Ice Cap program (wrong) | program 1, looping 1.556 s (right) |
| 17 / 33 (jump / land, bank 0) | same in all three | | |

---------------------------------------------------------------------------------------------------------------------

## 5. Design: "Board" in G-SA1, the game's own code anywhere

### 5.1 LIB API (API 0.4 draft; the game's entry and exit, nothing else)

```c
/* Board (sand / snow board, Sonic and Tails). on = 1 does exactly what the game's stage code does (SBOARD 0C9031AE,
 * STG08 0C90EA14): SetInputP(0, 44) and CreateElementalTask(3, 2, 8C0B2EE6 Sonic / 8C0BF7CE Tails), inside the world,
 * at once (between pairs: like a stage task at the end of the previous step's executor). on = 0 posts SetInputP(0, 24)
 * as Ice Cap's goal 0C90E900 does. */
typedef struct sa1_board_desc {
    uint32_t struct_size;
    int32_t  on;
    uint32_t terrain;       /* SA1_BOARD_TERRAIN_* while on the board */
    uint32_t flags;         /* SA1_BOARD_* */
} sa1_board_desc;
#define SA1_BOARD_TERRAIN_MAP   0u  /* the land's own attributes (host surface kinds) */
#define SA1_BOARD_TERRAIN_SNOW  1u  /* every static and moving land word: class bits 0x9C replaced by 0x8 (Ice Cap) */
#define SA1_BOARD_TERRAIN_SAND  2u  /* ... by 0x10 (Sand Hill) */
#define SA1_BOARD_REMOVE_ON_EXIT 0x1u   /* get off: FreeTask the board (default: it stays, the game's free object) */
#define SA1_BOARD_FAITHFUL_EXIT  0x2u   /* Tails: keep the game's jmp_y_spd x1.2 after getting off (default: restored
                                           from the physics table 8C1BDFE4 + 2 * 0x84, a documented host deviation like
                                           SA1_SUPER_FAITHFUL_REVERT) */
SA1_API sa1_status sa1_character_board(sa1_context *ctx, sa1_character_id id, const sa1_board_desc *d);
#define SA1_STATE_ON_BOARD   0x08u      /* sa1_state.flags: Sonic modes 62..68, Tails 48..54 */
#define SA1_SURFACE_SNOW     0x8000u    /* COL 0x8 (dig topic F8) */
```

**Rules.**

* **Kinds.** Only Sonic (0) and Tails (2) with their board images loaded. Otherwise `SA1_ERR_UNSUPPORTED`, which
  includes kind 8 (Super Sonic) and `SA1_LOAD_BOARDS` missing. A world without a camera adapter is also refused.
* **Orders not taken.** Orders last one step (1.2). After the next step the world checks the mode. If the order was not
  taken (holding or carrying something, a mode without a handler for 44: list in 1.2), the world frees the board task
  it created and reports
  it: `SA1_STATE_ON_BOARD` stays off, and a new `SA1_EVENT_BOARD` carries arg 0 = refused, 1 = on, 2 = off. G-SA1 then
  says "can't board now". Creating the task at once and freeing it after an unanswered order keeps the stage code's
  timing. The board task never feeds back into the player.
* **Re-entry.** A second `on` first frees the world's previous board task. The world records its guest address and
  checks that its exec is still `8C0B2F46` / `8C0BF804`.
* **Terrain.** The override is per world. The collision adapter applies it to the window copy
  (`sa1_coladapt_world.c`, after the `memcpy` of each COL entry, attr at `+0x20`, only on land words with `& 1`) and to
  `DynamicCOL_Add` (`attr | 0x08000000`). It is set at `on`, cleared at `off`. DISC / FAITHFUL worlds (no adapter)
  accept only `TERRAIN_MAP`.
* **Implementation.** New `src/world/sa1_world_board.c` plus `src/sa1_board.c`. Small patches to `sa1_world.h` (world
  state), `sa1_coladapt_world.c` (two OR sites), `sa1_coladapt.c` (`sa1ca_surface_to_col`: SNOW -> `0x8`), and
  `libsa1.h`.

### 5.2 Terrain

Three choices, an Options control:

* **Snow (Ice Cap)**, default: plays like Ice Cap act 3. Spray particles and snow SE 237 work. The snowball spray stays
  dormant until v2.
* **Sand (Sand Hill)**: plays like Sand Hill. Sand SE 691; no trail until v2.
* **The map's surfaces**: uses the dig topic's material classes (`gsa1_060_dig.md` 6.2), with "Snow board" assigned to
  GMod `snow` there. On untagged land the board behaves as measured in 4.2 (cruise or stuck); faithful, but not fun.

### 5.3 Textures

LIB loader, `SA1_LOAD_BOARDS`. Add one 1-texture archive per board texlist:

* Sonic `0x8C1C6380` (`MIW_B001.PVR`) and `0x0CB36F94` (`SB_BOARD1.PVR`);
* Tails `0x0CB025A0` (`MTX_BOARD0.PVR`).

`texlist_addr` is the board texlist, so `sa1_capture_resolve` (`sa1_capture.c:202`) resolves the board's draws by their
texname address. The files are read uncompressed through `sa1_disc`, with manifest entries (sizes and SHA-1s in 1.4)
and the README table. No game byte goes into a repo. G-SA1 already turns every archive into render targets.

The board's colours through the direct `8C60E080` path keep 4c F2(a)'s rules (palette-lit, white). The textured RGB
source of `8C6108B0` is UNVERIFIED; check it against `--disp` (section 8).

### 5.4 Effects and sound

* `stubs.tsv:47` `0C900AE0`: `fatal` -> `void`, evidence "display-only STG08 snowball spray: 16 tasks with STG08's model
  and texlist; STG08 is not in GMod worlds". It also skips that call's `rand()` draws. That is harmless: GMod worlds
  have no reference, and replays re-inject `rand` at every player entry.
* `8C09E474` (the snow spray) works as it is.
* G-SA1 switches the stage sound bank while boarding (`sa1_audio_set_stage`: 8 for snow, 38 for sand) and back after.
  Option "Board sounds", on.

### 5.5 Camera

The game's board camera stays behind the board, and the board's "brake" is the stick pointing away from it (1.5).

* G-SA1 option "Camera follows the board", on: while `SA1_STATE_ON_BOARD`, ease the camera yaw towards the board's
  yaw. Host camera, not game code.
* Option "Speed field of view (Sand Hill / Ice Cap)": `fov = (12743 + 5461 * k) * 360 / 65536` degrees, with `k` from
  1.1 (`0C90327E`). That is 70° at rest and 100° from `spd.x` 7.82. GMod's `fov` is horizontal at 4:3, like the
  game's 640-wide angle. Default off: it changes the player's view. Owner question.

### 5.6 G-SA1 feature (through the `g0-seam`)

**Module files.**

* `src/module/bind_board.cpp`: `gsa1.Board(on [, terrain, flags])` returns ok, msg; `gsa1.BoardState()` returns
  `{on, pending, kind_ok}`. `GSA1_FEATURE(board, 1, ...)`.
* `src/core/session.cpp` (p): keep the requested board state per local character, re-apply it after `Recreate`
  (upgrades) and respawn, and switch the audio stage on enter / exit.

**Lua.** `addon/lua/gsa1/feat/sh_board.lua` and `cl_board.lua`, `GSA1.ConVar`, `GSA1.AddMenuPage`.

* Commands: `gsa1_board` (toggle), `gsa1_board_on`, `gsa1_board_off`.
* Toggle with the key in `gsa1_board_key` (Options binder, `PlayerButtonDown`) or with the pad button in
  `gsa1_board_pad` (polled in `cl_pad.lua`).
* For Knuckles, Amy, E-102 and Big the toggle prints "Board mode: Sonic and Tails only".

**ConVars** (each one a control on the **Options > G-SA1 > Board** page):

| ConVar | meaning | default |
|---|---|---|
| `gsa1_board_key` | key that toggles the board (DBinder) | none (owner Q1) |
| `gsa1_board_pad` | controller button | none (owner Q1) |
| `gsa1_board_terrain` | 0 snow (Ice Cap), 1 sand (Sand Hill), 2 the map's surfaces | 0 (owner Q2) |
| `gsa1_board_sounds` | stage sound bank while boarding | 1 |
| `gsa1_board_camera` | camera turns behind the board | 1 |
| `gsa1_board_fov` | Sand Hill / Ice Cap speed FOV | 0 (owner Q3) |
| `gsa1_board_keep` | the board stays where you get off (the game's) | 1 (owner Q4) |
| `gsa1_board_tails_jump` | keep the game's Tails jump x1.2 after boarding | 0 |

The page also shows a status line: "On the board / Off / Can't board now (holding something)", from
`SA1_EVENT_BOARD`.

**Tests.**

* `tests/feat_board_test.cpp`: session-level, with `SA1_TEST_GDI`. Board on a Sonic session -> `BoardState().on`
  within 30 ticks; Knuckles -> the error text; Recreate keeps it.
* `tests/feat_board_binding_test.cpp`: mock Lua: `gsa1.FEATURES.board == 1`, argument checks.
* Lua seam test: the page builds and the toggle concommand exists.
* `README.md` (p): "Board" section and the controls table.

### 5.7 Verifying against the harness

1. **Decompiled code.** Copy the four scripts of 4.1 to `harness/tests/sim/` and record the references (user data,
   `build/traces/ref`, never committed).
   * After F1, `harness/build.py --non-matching --pc` and `regress.py --route --only board_sh_sonic,...` must stay
     identical.
   * The coverage of `trace --coverage` must no longer list `8C05872A` / `8C058D3C` / `8C058DD4` as thunks.
   * `harness diff func_8C05872A` (and the other four) must PASS.
2. **LIB = game (strict).**
   * Sand Hill: add DISC level 38 (`SBOARD.PRS`, 299,605 B, SHA-1 `1e5d198a5960657660a7b6cadfc31cea7fb045eb`) to
     `sa1ca_disc_stage` (`sa1_coladapt.c:2427-2447`: levels 0..10 and `STGnn` naming today; `SA1CA_STAGE_LEVELS` 12).
   * Replay `board_sh_sonic` / `board_sh_tails` in `tests/replay_test.c` (world_replay `wr_replay`). Enter the board
     with the API's internal function after record 0's step: where the level task posts it, since the record of step 1
     has `smode` 44. Expect 411 records identical (decoded fields and raw blobs).
   * The board task's position does not enter the player records; the order and its timing do.
   * Ice Cap act 3 follows later: STG08 with M_SBMOT composed **after** it (the game overwrites STG08's tail), and the
     start push `0C90E810` emulated in the test or decompiled.
3. **API = game's entry (unit).** After `sa1_character_board(on)`:
   * the world has `smode` 44 and `flag & 0x1000`;
   * there is a level-2 task whose exec after its first step is `8C0B2F46` / `8C0BF804`;
   * its `timer.ptr` is `0x0CB37454` / `0x0CB029C4` and `mwp->weight` is 0.02, as with the SBOARD task in the harness.
   The harness can dump the board task: `trace --calls` or a RAM read.

---------------------------------------------------------------------------------------------------------------------

## 6. Fixes

| # | fix | files | test (fails before, passes after) | owner |
|---|---|---|---|---|
| F1 | Decompile `8C05872A`, `8C058A1A`, `8C058D20`, `8C058D3C`, `8C058DD4` (chunk `8C0586A0`, 1,614 B), staged then landed; LIB relowers; remove `stubs.tsv:116` | `SADC/build/staging/1st_read/code_8C05872A.c` -> `SADC/src/1st_read/`; `LIB/gen/policy/stubs.tsv` | `harness diff` of the 5 PASS; `regress.py --route` on the 4 board refs identical with them native; LIB `board_test` "enter/exit" (F3) runs 600 pairs board on/off with no `SA1_ERR_NOT_IMPLEMENTED` (today: fatal at the first board pair, 4.2) | new decompiler `7-board-decomp` (staging, can start now, Wave C), landed by that wave's landing agent (L0 pattern) |
| F2 | `0C900AE0`: `fatal` -> `void` with evidence | `LIB/gen/policy/stubs.tsv:47` | `board_test` "snow lean": Sonic on `SA1_SURFACE_SNOW` and on SLIPPERY, 15°, leaning 300 pairs, no error (today: fatal at pair 75 / 89) | `7-lib-board` |
| F3 | `sa1_character_board`, `SA1_STATE_ON_BOARD`, terrain override, `SA1_SURFACE_SNOW` (dig F8), Tails exit restore, re-entry frees the old board, refusals | `LIB/include/libsa1.h` (p), `src/sa1_board.c` + `src/world/sa1_world_board.c` (new), `src/world/sa1_world.h` (p), `src/world/sa1_coladapt_world.c` (p), `src/sa1_coladapt.c` (p), `src/sa1_character.c` (p: state flag), `tests/board_test.c` + `tests/build_board.py` (new) | `board_test`: (a) Sonic / Tails enter: `ON_BOARD` within 10 pairs, action in 102..124 / 107..127; (b) flat SOLID + `TERRAIN_SNOW`: `attr & 0x8` on the board, not after exit; 15° slope speed >= 2.5 at pair 60 (`TERRAIN_MAP`: 0.987, 4.2); (c) Tails enter / exit / enter: `jmp_y_spd` 1.66 after each exit (2.3904 with `FAITHFUL_EXIT`); (d) Knuckles / kind 8 -> `SA1_ERR_UNSUPPORTED`; (e) Sonic carrying a held object (mode 23): order not taken -> `SA1_EVENT_BOARD` arg 0, no board task left, `ON_BOARD` off; (f) Tails flying (mode 15) boards (mode 51, then 48 on landing). All fail today (no API) | `7-lib-board`, after `6a-lib-super` (it owns `sa1_api_world.c`, `sa1_coladapt*`, `sa1_loader.c`, `sa1_manifest.c` in Wave C) |
| F4 | Board PVR archives | `LIB/src/sa1_loader.c` (p), `src/sa1_manifest.c` (p), `README.md` (p) | `board_test` "texture": the board draw (44 tris) has archive >= 0, texture 0, 64 x 64 (today: archive -1) | `7-lib-board` |
| F5 | DISC level 38 and the strict Sand Hill board replay | `LIB/src/sa1_coladapt.c` (p: stage names / levels), `tests/replay_test.c` + `tests/build_replay.py` (p: a `BOARD` list) | `board_sh_sonic` / `board_sh_tails` 411 / 411 identical (today: "no stage overlay for level 38", then fatal `8C05872A`) | `7-lib-board` (or the replay owner of that wave) |
| F6 | Board references in the harness | `SADC/harness/tests/sim/board_{sh,ic3}_{sonic,tails}.txt` (copies of the scratch scripts), refs in `build/traces/ref` | `regress.py --route --only board_*` identical (passes today with interpreted `8C05872A`; after F1 with it native) | the harness owner of the board round |
| F7 | G-SA1 feature "board" | 5.6 | `feat_board_test.cpp`, `feat_board_binding_test.cpp`, Lua seam test; owner test in GMod | `7-gsa1-board`, after `g0-seam` and F3 / F4 |
| F8 | Code-map corrections (section 3) | `SADC/docs/research/sa1_code_map.{md,csv}` | review | orchestrator / docs steward |

**Release.** One shippable round ("7 board": F1 -> F2-F5 -> F6 -> F7 -> verify), installed at a quiescent point
(rounds plan 2.5). The "map's surfaces" terrain needs the dig topic's G-SA1 surfaces work (`gsa1_060_dig.md` F1-F6).
F3 provides that topic's F8.

**Cross-topic hazard.** The dig topic maps GMod `ice` to SLIPPERY (COL `0x4`) by default. That is safe alone:
`8C07B694` runs only in board modes. With the board it is fatal until F2 lands.

---------------------------------------------------------------------------------------------------------------------

## 7. Owner questions (the design's defaults)

1. Which key and controller button toggle the board? Default: none bound; the Options page has a key binder and a pad
   choice.
2. What should the ground feel like while boarding: snow (Ice Cap), sand (Sand Hill), or the map's real surfaces?
   Default: snow.
3. The game widens the view at speed on the board (70° to 100°). Do you want that? Default: off.
4. When you get off, should the board stay on the ground like in the game, or vanish? Default: it stays, and a new board
   replaces it.
5. Board tricks need the game's jump ramps. Should a spawnable "board ramp" come later? Default: later.

## 8. Open / UNVERIFIED

* Texname -> `<NAME>.PVR` file mapping of `8C608C82` (the files exist; the mechanism is assumed).
* The roles of `8C0A0C40`, `8C0A0C94`, `8C07BD2A`, `8C07C8E0` and `8C01937C`. The last is the projection angle, from
  0x31C7 = boot's 457.06.
* Order 45's meaning (event overlays).
* `D_8C18B7EC->smode`: which camera mode sets it, and the inverted steering then.
* The board's textured colour through `8C60E080` / `8C6108B0` (round 4c G5); check against the harness `--disp`.
* Boarding while holding an object. The game's handlers drop order 44 there (1.2); this was not tried in a world.
* Whether STG08's tail that M_SBMOT overwrites in Ice Cap holds anything the act needs (the game does it, so it is
  faithful).
* **v2 (optional):** a board world image with STG08 (snow) or SBOARD (sand) in the `0x0C900000` slot instead of
  B_CHAOS7, plus the decompiled effect code:
  * STG08 `0C900AE0`, `0C900C32`, `0C900D20`, `0C900D96`;
  * SBOARD `0C900080`, `0C905140`;
  * their textures.
  This brings the snowball spray and the sand trail. It needs a world re-create when the board is turned on.
* **Tricks:** the jump ramp is any touched object with `twp->id == 9` (and a force work). An interaction-layer host
  object kind could provide it later.
* Followers (round 6b) could board when their leader boards (same API, the follower's world).

## 9. Answers to the task's questions, in short

* **Enter.**
  * Stage code SBOARD `0C9031AE` (Sand Hill) and STG08 `0C90EA14` (Ice Cap act 3), plus the race AI `8C07FA54`.
  * The calls: `SetInputP(pno, 44)` and `CreateElementalTask(3, 2, 8C0B2EE6 | 8C0BF7CE)`.
  * Modes: Sonic 62-68 (entry 65, action 105); Tails 48-54 (entry 51, action 110, `height + 2`, `jmp_y_spd x1.2`).
* **Leave.** `SetInputP(pno, 24)`: Ice Cap goal `0C90E900`. Order 19 is the goal stop.
* **Board model and draw.** The board task: model `0x0CB37454` / `0x0CB029C4`, board motions `D_8C1C6318` /
  `D_8C2579BC` via `8C60E080`, standalone PVR textures.
* **Physics.** Free 3D on slopes, not path-bound.
* **From the stage.** Nothing but snow / sand land attributes, the camera taskwk and the board files.
* **Have / missing.**
  * LIB has everything but `8C05872A` + closure (fatal), `0C900AE0` (fatal), the board textures, a request API and a
    terrain choice.
  * The SBOARD / STG08 stage code has no C and is not needed for "board anywhere".

## 10. Scratch files (`SADC/build/research/board/`)

* `dump_actions.py`, `dump_boardact.py`, `dump_tex.py`: action tables, board motion tables, board texlists (from the
  user's own `1ST_READ.BIN` / baserom overlays; prints only).
* `board_{sh,ic3}_{sonic,tails}.txt` and `ref_*.bin/.csv`, `nm_*.bin`: the four references and their routed checks.
  `ic3_*`, `sh_*` are the exploratory runs; `cov_*.txt` are coverage; `native_list.txt` / `not_native_1st.txt`.
* `board_probe.c`, `build_board_probe.py`, `out_x86_64_windows/board_probe.exe` (against a locked `libsa1.a`
  snapshot):

  ```
  board_probe GDI <sonic|tails> <flat|slopeNN> <solid|slippery|sand|solidraw|sandraw|snowraw> <pairs> [after]
  ```

  Environment: `PROBE_DEFER_BOARD`, `PROBE_FLAG8`, `PROBE_FREE_AT_EXIT`, `PROBE_REENTER`, `PROBE_CAM_YAW`. Outputs
  `probe_*.txt`.
* `audio_probe.c` (`audio_probe.txt`): the tone table of 4.3.

---------------------------------------------------------------------------------------------------------------------

## Review (adversarial)

Reviewer: independent adversarial pass, 2026-10-21. Read-only: nothing in SADC/src, include, tools, harness, config,
LIB or g-sa1 was edited. Experiments are in `SADC/build/research/boardreview/` (list at the end).

### R.1 Verified (re-run or re-read)

* **Harness references.** The four scripts re-recorded with the pure interpreter (`harness/build/sa1h.exe`, rebuilt
  10:52) are **identical** to the doc's `ref_*.bin` (411 steps each). `sa1h_nm_pc.exe trace ... --route --check` is
  **identical** for all four. Modes, actions, max `spd.x` (4.76 / 3.51 / 7.50 / 7.50), land words (`A0000011`;
  `20/A0/C0000009`), `smode` 44 at step 1, Sonic's first board action at step 16 (SH) / 5 (IC), Tails' at step 1: all
  as in 4.1.
* **Entry / exit asm.** SBOARD `0C9031AE` = `SetInputP(0, 44)`, `GetPlayerNumber` 0 -> `8C0B2EE6`, 2 -> `8C0BF7CE`,
  `CreateElementalTask(3, 2, init)` (`sboard/text/0C903148.src:78-130`); the FOV constants of `0C90327E` (7.82,
  0.12787, 12743, 5461) and the `y < -7405` check. STG08 `0C90EA14` (`0C90DD40.src:1701-1734`), `0C90E900` (start
  zone (132, 375, -42) r 30, goal (-3798, -23289, -5526) r 200, `SetInputP(0, 24)`, `8C01AE60`, pool `:1747-1798`),
  `0C90E810` (`spd.x >= 4.0` for players 0 and 1, `FreeTask` after 100 frames, `:1458-1504`). Race AI `8C07FA54`:
  `SetInputP(1, 44)`, board task, `twp->counter.b[0] = 1` (`8C07F560.src:731-796`).
* **C.** `code_8C0AFF1A.c:262-269`, `code_8C0BBDD6.c:247-256`, `code_8C07C550.c:67`, `code_8C06C6A0.c:76` with its
  call at the end of the mains (`code_8C0A77BC.c:977`, after the mode machine at `:141`; `code_8C0B6148.c:622`),
  `code_8C0B0E94.c:17`, `code_8C0BCBCA.c:56`, `code_8C0B2EA0.c:78-194`, `code_8C0BF804.c:138-217`,
  `code_8C0AFB10.c:100`, `code_8C07B500.c:157-218`, `code_8C0700C2.c`. `D_8C1C63A0 = 1` (texlist pick by parity).
* **LIB probe** (the doc's `board_probe.exe`, its locked snapshot; `libsa1.a` itself is absent during wave B): "as the
  game" -> `SA1_ERR_NOT_IMPLEMENTED func_8C05872A` at pair 10 for Sonic **and** Tails (standing action 1). Every
  stand-in row of 4.2 reproduced to the printed digits (flat 1.000; 15 deg SOLID 0.987; SAND 1.173 / 1.955 / 2.730 /
  3.605; snow fatal `0C900AE0` at pair 75; SLIPPERY at 89; 30 deg 0.035 then 1.534; 45 deg 6.018); exit -> fatal
  `8C05872A` on the next pair; Tails `jmp_y_spd` 1.66 -> 1.992 -> 2.3904, height 9 -> 11 -> 9 -> 11; one board draw of
  44 triangles with archive -1.
* **Audio** (`audio_probe.exe`): the 4.3 table exactly (230/231/235/237 empty under EMERALDCOAST and SANDHILL, programs
  22/23/27/29 under ICECAP; 691 = EC loop 4.763 s, SANDHILL loop 1.556 s, ICECAP 1.604 s one-shot).
* **Disc** (read through `tools/research/rip_character.py` GdiFS, prints only): `SONICADV/MIW_B001.PVR` 10,952 B,
  `SB_BOARD1.PVR` 10,956, `MTX_BOARD0.PVR` 10,956, SHA-1s and GBIX 500 / 1004 / 1224 as in 1.4; **all three** are
  RGB565, twiddled mipmapped, 64 x 64 (the doc gave no format for MIW_B001). `SBOARD.PRS` 299,605 B,
  `1e5d198a...` as in F5. No PVM holds the three texnames.
* **No board anywhere in G-SA1 or the LIB API** (grep of g-sa1 src/addon/tests and `libsa1.h`): the owner's symptom is
  a missing feature, as the doc says. `stubs.tsv:42 / 47 / 116 / 121 / 122` are as cited today (wave B's `l0-land`
  owns `stubs.tsv`: cite policies by address, not line). `session.cpp:271` loads `SA1_LOAD_DEFAULT`.
* **Code-map mislabels** confirmed (`sa1_code_map.csv:122`, `sa1_code_map.md:220`); also `.md:123` ("snowboard draw
  hook 8C0B2EE6"), `:361` and `:369` need the same correction.

### R.2 Refuted or corrected

1. **"Order 44 is posted nowhere else" is wrong: there is a fourth site.** The STG08 level task `0C90DFE0` (12-state
   switch) posts `SetInputP(0, 44)` in state 7 (`L0C90E2D2`, `JSR` at `0C90E2F2`, `0C90DD40.src:798-815`) after
   `8C07C2D0(0)` / `8C0509B8(0)` and before `8C08A5AE()` and the camera `8C01F4CE`, **without** creating a board task,
   then goes to state 8 (`0C90E314`: `0C90E900` every frame). Its exact role (act-3 ride start vs. restart) is
   UNVERIFIED. It matters for the API: the game has a path that posts 44 while a board task already exists, so
   "re-entry = post 44 again, keep the live board task when its exec is still `8C0B2F46` / `8C0BF804`" is the more
   faithful re-entry (5.1 "Re-entry" frees and re-creates).
2. **The `8C05872A` closure is smaller than F1 says.** `8C05872A` calls only `8C058D3C`, `8C058DD4` (+ `8C056A04`,
   `8C042DC4`, `njAddVector`, `njSubVector`) (`8C0586A0.src:152-533`). `8C058A1A` (the `CCL_Entry` variant; callers
   `8C064B20.src:224`, ADV02 `0C90EA60.src:222`) and `8C058D20` are not reached by the board. "Functions 2-6 of 29" is
   off by one: `8C058D5A` is function 6, `8C058DD4` function 7. Board need: 752 + 30 + 116 = **898 B**.
3. **`8C05872A` is not board-only, and is already requested.** It is the generic movable-object step (callers
   1ST_READ `8C064980` O_SAMPLE_S, ADV00 / ADV02 / ADV0130 objects; code map "ObjectMovable*"). `libsa1_interaction.md`
   6.7 (lines 864-867, 1394-1398) already asks for `8C0586A0` + `8C05872A` + `8C058D3C` + `8C058DD4` for HOLDABLE
   (`hold_ec1`). The stubs.tsv reason "called only by ... (snowboard)" is wrong too.
4. **"The coverage lists 8C05872A, 8C058D3C, 8C058DD4 in every board trace" is wrong.** `trace --coverage` on the four
   `board_*` scripts: present in `board_sh_sonic`, `board_ic3_sonic`, `board_ic3_tails`; **absent from
   `board_sh_tails`** (Tails' action is 110 from step 1, so `8C0BF804` never leaves its held branch). In
   `board_ic3_tails` the race-AI Sonic's board task (`8C0B2F46`) also runs, so no reference proves Tails' free branch.
5. **Sonic's "order 44 not taken" list is overstated.** Modes 20, 21, 22, 26 and 33 call `8C0AFF1A` (which takes 44)
   unless holding (`flag & 0x800`) (`code_8C0A966C.c:601-609, 854-859, 902-908, 1057-1062`); mode 20 uses `8C0B0F24`
   only with `equipment & 0x8000`. Always refused: 23, 25, 27, 28, 29, 34, 35 (`8C0B07E4`, no case 44) and 71-78.
   Tails' mode 22 is conditional like 26 (`code_8C0B70F4.c:901-907`). The API's empirical check is unaffected, but no
   status text or test may hard-code the doc's lists.
6. **"Camera yaw 0 vs 90 identical paths" is wrong after the first turn.** The doc's own `probe_cam_1.5708.txt` leaves
   the yaw-0 path at pair 160 (mode 64, `spd.x` 0.891, then 0.412 at 170); reproduced. The turn rate is
   camera-independent (`sang = SubAngle(0xC000 - cam->ang.y, ang)` cancels, `code_8C0700C2.c:76`), but the brake test
   `8C06CE40` (stick against the board's facing) is not. With G-SA1's mouse-look chase camera, holding W once the
   board has turned about 90 deg away from the view brakes it to <= 0.46 (yaw 180: 0.47 / 0.40, reproduced). "Camera
   follows the board" (5.5) is therefore needed, default on.
7. **"8C0751C0 sets mwp->weight"**: it is `func_8C075214` (`code_8C0751C0.c:49`, weights at `:69-78`).
8. **Tails' board handler does not ignore other orders.** `func_8C0BCBCA` returns 1 for any posted order
   (`code_8C0BCBCA.c:81`): the order is consumed and the rest of that step's mode code is skipped. Only Sonic's
   `8C0B0E94` returns 0 (`code_8C0B0E94.c:36`).
9. **"The board feel comes from the land attribute" holds only on slopes; on flat land the board keeps the speed it
   has.** Code: the push exists only while `spd.x <= jog_speed` 0.46 (`code_8C0700C2.c:103`), friction only opposes a
   non-zero `acc.x` (`:145-165`), air resistance only above `max_spd` 5.98 (`:55`). Measured (`review_probe.exe`,
   `PROBE_DEFER_BOARD`):

   | case | `spd.x` on the board, flat |
   |---|---|
   | standing entry, SOLID / snow `0x80000009` / sand `0x80000011` | 1.000 for 150 pairs, all three |
   | Sonic boards while running at 2.74 (SOLID) | 2.662 constant (pairs 80-220) |
   | Tails boards while running at 2.83 | 2.752 constant |
   | Sonic runs on snow, boards at 0.45 | 0.440 constant |
   | emulated Ice Cap start push (`0C90E810`: `spd.x` raised to 4.0, 50 pairs) | 4.000 until pair 170 (Sonic and Tails); snow: fatal `0C900AE0` at pair 73 (leaning at 4.0 is enough) |
   | a jump, then landing | 4.0 -> 2.11; 2.66 -> 1.64; 1.0 -> 1.22 |

   So `TERRAIN_SNOW` / `TERRAIN_SAND` change nothing on flat ground. On the owner's maps (gm_bigcity is flat streets)
   a board started from standing crawls at 1.0 SA1 unit per step = 240 Hammer units/s (G-SA1's 4 HU per unit,
   `g-sa1/README.md:925`), and one braking turn leaves it at <= 0.46. 5.2's "Snow: plays like Ice Cap act 3" is true
   on slopes only. This is faithful (both board stages are downhill), but it is what the owner will see.

### R.3 Gaps

1. **The owner's board spec is not designed.** `gsa1_rounds_plan.md:1276-1279` (written before this doc) and the
   status page: board mode is toggled "from the Q menu (Options > G-SA1)" **and** by spawnable **"Board On" / "Board
   Off" trigger boxes** under **Entities > G-SA1** (resizable volumes; entering one puts Sonic / Tails on or off the
   board). Section 5.6 has key / pad binders, concommands and a status line: no on/off control on the page and no
   entities. G-SA1 can do it: the server already moves the player entity to the character (`sv_gsa1.lua:61-72`), so
   the zones can be server SENTs that test the player's position and tell the owning client.
2. **Flat-ground speed (R.2.9) has no answer in the design.** A faithful option exists: the Ice Cap start push
   `0C90E810` (STG08's own rule: `spd.x >= 4.0` for 100 frames, about 0x56 bytes), decompiled and called natively at
   board-on and by Board On zones. Measured: on flat land the board then cruises at 4.0 (960 HU/s) and keeps it.
   Owner question (default on is suggested).
3. **The sound-bank switch (5.4) is session-wide and blocking.** G-SA1 has one `sa1_audio` for every character
   (`g-sa1/src/core/audio.cpp:66-68`). `sa1_audio_set_stage` replaces stage banks 0, 1, 2, 4 and 5, stops every
   mixer's sounds and reads the 1.06 MB (SANDHILL) / 1.20 MB (ICECAP) MLT **on the calling thread**
   (`libsa1_audio.h:103-106`): a hitch at every toggle (synchronous disc reads on the Lua thread were the main suspect of the 0.3 freezes,
   `gsa1_ramp_freeze.md`) and
   the other characters' stage tones change too.
4. **F5 strictness.** SBOARD `0C900080` (void in LIB) calls `rand` twice per sand-trail spawn
   (`sboard/text/0C900080.src:8-60, :81`) and is called inside the player step whenever `spd^2 > 3` on `0x10`. The
   end-of-step `rand_state` of `board_sh_*` will therefore differ from LIB's ("411/411 raw blobs" will fail; injection
   only fixes the next step's entry), unless `WR_CMP_NO_RAND` is used for those records or `0C900080` / `0C905140` are
   decompiled. Likewise `0C900AE0` really runs in `board_ic3_*` (92 snow-lean steps in `board_ic3_sonic`; overlay
   functions never appear in `--coverage` lists). UNVERIFIED until F5 exists.
5. **A board left behind can fall forever** (UNVERIFIED until F1). `8C05872A` applies `acc.y = -weight` off the
   ground, `spd.y` clamped (`8C0586A0.src:166-249`). A GMod world's collision window follows the player, so a board
   left more than the window radius away has no land under it. The keep-the-board default (owner Q4) needs a rule:
   `FreeTask` (the game's own) once the board is outside the window.
6. **Ownership is wider than "after 6a-lib-super".** F3's `sa1_world.h`, `sa1_character.c` and the `sa1_state_ex`
   tail are also patched by `5-lib-core` / `5-lib-game` (Wave C) and `6b-lib-proxy` (Wave D); `SA1_EVENT_HURT 14` is
   already taken (`libsa1_interaction.md:368`), so `SA1_EVENT_BOARD` must be numbered after the interaction events.
   F5 also needs `sa1_world_boot.c:255` (`STG%02d` in `sa1g_image_has_stage`), `sa1_world_image.c:402` and
   `sa1ca_disc_landtable` (`level < 12`, `sa1_coladapt.c:2500`).
7. **Conflict with the dig topic.** `gsa1_060_dig.md:265` makes GMod `snow` Diggable by default, and its page gives one
   kind per surface type (`:317`). 5.2's "Snow board assigned to GMod snow" cannot hold then. Either surfaces carry
   bits (DIG | SNOW) or the "map's surfaces" terrain adds `0x8` to Diggable snow while boarding.
8. **Multiplayer puppets.** The board is a separate task, not part of `Pose(action, frame)` (`sh_net.lua:16`): remote
   players would see the board pose without a board. Note it in the README for when puppets are used.
9. **Tails' exit restore.** Only the board changes `p.jmp_y_spd` (grep of `sadc/src`), so the physics-table restore
   equals a snapshot taken at `on`; a snapshot is the safer implementation (no table index assumptions).

### R.4 Required changes

| # | change | files | test (fails today, passes after) | owner |
|---|---|---|---|---|
| C1 | Fix R.2.1-R.2.8 in sections 0, 1.2, 1.3, 1.5, 3, 4.1, 4.2 and 9 | this doc | review | the board topic author |
| C2 | Merge F1 with `libsa1_interaction.md` 6.7: **one** staging unit for chunk `8C0586A0`: `8C0586A0` (ObjectMovableInitialize) + `8C05872A` + `8C058D3C` + `8C058DD4` (board needs only the last three); `8C058A1A` / `8C058D20` optional. Remove the `8C05872A` and `8C0586A0` stubs by address | `SADC/build/staging/1st_read/code_8C0586A0.c` (one file), then `src/1st_read/`; `LIB/gen/policy/stubs.tsv` | unchanged F1 tests, plus LIB `board_test` "Tails from standing": Tails boards from action 1 without the flag-8 stand-in (today: fatal at the first pair) | one Wave C decompiler for both round 5 and round 7 (the first round to need it), landed by that wave's landing agent |
| C3 | Board On / Board Off entities + Q-menu switch | `g-sa1/addon/lua/gsa1/feat/sh_board.lua` (the two SENTs: `Category = "G-SA1"`, `Spawnable = true`, box size as editable NetworkVars, drawn as a translucent box while the physics / tool gun is out), `feat/sv_board.lua` (server edge test of the player position per tick, net message to the owner), `feat/cl_board.lua` (a "Ride the board" checkbox / button on Options > G-SA1 > Board bound to `gsa1_board`) | Lua seam test: both classes registered in the "G-SA1" spawn category; a mock player crossing a Board On box sends exactly one on message, a Board Off box one off; the page has the switch. Owner test in GMod | `7-gsa1-board` |
| C4 | Start push option (R.3.2) | `SADC/build/staging/overlays/stg08/code_0C90E810.c` (decompile); `LIB/src/world/sa1_world_board.c` (`SA1_BOARD_START_PUSH` flag runs it as a world task); `gsa1_board_push` ConVar + control | `board_test` "flat": `TERRAIN_SNOW`, flat SOLID, from standing: 1.000 at pairs 20-150 without the flag (documents the game), >= 3.99 at pair 150 with it | `7-board-decomp` (C), `7-lib-board` |
| C5 | Board sounds without a session-wide reload: a secondary stage bank per mixer (tones 208-238 and 690-698 resolved from ICECAP / SANDHILL for the boarding character only), loaded on the loader thread at open or first use | `LIB/include/libsa1_audio.h`, `src/sa1_audio.c` (after `6c-lib-audio`); `g-sa1/src/core/audio.cpp` | `audio_test` "board bank": tone 237 resolves for mixer A (board, ICECAP) while mixer B still gets EC; toggling 10 times keeps the caller under 2 ms and stops no sound of mixer B | `6c-lib-audio` successor / `7-lib-board` |
| C6 | F5 test wording: compare `board_sh_*` with `WR_CMP_NO_RAND` and say why, or add `0C900080` / `0C905140` (SBOARD) to C2's decompile list | `LIB/tests/replay_test.c`, `build_replay.py` | as F5 | `7-lib-board` |
| C7 | Re-entry: post 44 again and keep a live game board task (R.2.1); free a kept board that leaves the collision window (R.3.5) | `LIB/src/world/sa1_world_board.c` | `board_test`: enter / exit / walk 3000 units / check the board task is freed; enter / exit / enter keeps one board task | `7-lib-board` |
| C8 | Ownership: F3 after `5-lib-game` and `6a-lib-super`, before `6b-lib-proxy`; `SA1_EVENT_BOARD` numbered after the interaction events; F5 file list adds `sa1_world_boot.c`, `sa1_world_image.c` | rounds plan | review | orchestrator |
| C9 | Dig-topic surface model: bits, not one kind (R.3.7) | `gsa1_060_dig.md` F-rows, G-SA1 surface page | `grid_test`: a DIG | SNOW triangle gives weight 0.15 and Knuckles digs it | dig topic owner |

**Verdict.** The diagnosis holds: no API, the two fatal stubs on the entry / snow paths, untextured board, wrong stage
bank and Tails' compounding jump all reproduce exactly, and F1-F4 are the right game-code fixes (F1 smaller and shared
with round 5). The design is incomplete for what the owner asked and will see: it lacks the Board On / Board Off
trigger boxes and the Q-menu switch, its terrain choice does nothing on the owner's flat maps (crawl at 1.0, or at <=
0.46 after a turn with a mouse-look camera), and its sound switch would reload the whole session's stage bank
synchronously.

**Reviewer's scratch files** (`SADC/build/research/boardreview/`): `review_probe.c` + `build_review_probe.py` (the
board probe plus `PROBE_RUN_PAIRS` and `PROBE_PUSH_PAIRS`, built against the board topic's locked snapshot;
`out_x86_64_windows/review_probe.exe`), `rv_*.txt` (probe outputs), `pure_board_*` (re-recorded references),
`nm_board_*` (routed checks), `covlist_board_*.txt` (coverage of the four references).
