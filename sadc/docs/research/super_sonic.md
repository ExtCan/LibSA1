# Super Sonic as a playable character (round 6a research)

Date: 2026-10-21. Read-only research: nothing in LIB, g-sa1, SADC/src, include, tools, harness or config was
changed. Scratch scripts, traces and dumps: `SADC/build/research/super_sonic/` (index in section 10).
Addresses are US v1.005. "Verified" = read in the disassembly and/or measured in a trace; **UNVERIFIED** marks
what is only derived.

## 0. Summary

* **How the game makes Sonic super.** The Perfect Chaos stage (level 19 = `B_CHAOS7.PRS` at 0x0C900000) never
  writes the flag itself. Its task 0C914D60 calls the player-order function `SetInputP(0, 46)` (8C07C65E). Sonic's
  own request handler then does the transformation. In SADX terms this is PL_OP SUPER. The handler is
  `func_8C0AFF1A` case 46, or `func_8C0B07E4` case 46 while Sonic holds an object. It does four things:
  - sets `playerwk.equipment |= 0x8000` (playerwk +0x004, a Sint16);
  - creates the aura task 0C900480 in B_CHAOS7;
  - creates the physics task 8C0B32B6 in 1ST_READ;
  - switches to mode 71 with action 130.

  Measured in the harness: equipment = 32768 and smode 46 at step 6 of a level-19 trace.
* **No physics-table entry.** Super Sonic uses Sonic's entry 0 of the 8 x 0x84 physics table at 0x8C1BDFE4. The
  task 8C0B32B6 overrides four fields while the bit is set:
  - air_resist from -0.008 to -0.001;
  - air_resist_air from -0.028 to -0.002;
  - air_accel from 0.031 to 0.05;
  - center_height from 5.4 to 6.4.

  **Game bug:** its destructor 8C0B3240 restores the wrong fields. Reverting leaves max_x_spd = -0.008 and
  run_accel = -0.028.
* **Model, textures and motions.** Sonic's own action table (8C1C55A0) holds actions 130-141, which use the Super
  Sonic objects in B_CHAOS7. The display (8C0AE17A) picks the SUPERSONIC texlist 0x0C901EA0 for actions 130-141. The
  12 Super Sonic weld entries in Sonic's weld table are switched on (inpmode 2) only when `GetStageNumber() & 0xFF00
  == 0x1300` at Sonic's mode 0 (SonicMain 8C0A77BC).
* **What runs where.**
  - 1ST_READ: all player logic. This is Sonic's mode machine with super modes 71-78, the request handler
    8C0B0F24, the homing / damage / gravity-alignment checks, the physics task and the run-action selector
    8C0AE6D8. **All of it already has C**, and it is **verified**: two level-19 Super Sonic reference traces (909 and
    5,000 steps) replay bit-identically in the GA-routed harness (1,523 native functions).
  - B_CHAOS7: only the presentation, plus the stage's trigger and ring rules. That is 21 functions, 5,250 B, none
    decompiled:
    - aura task (2,410 B), with a whoosh-sound state machine;
    - three effect tasks (2,726 B), instruction-for-instruction clones of decompiled 1ST_READ effect tasks;
    - trigger 0C914D60 (46 B);
    - ring drain 0C917088 (68 B).

  Also needed: 1ST_READ 8C046A9C (130 B, particle spawner, no C).
* **Overlay layout is not a problem.** In memory, B_CHAOS7 occupies 0x0C900000-0x0CA6B454, and its BSS extends to
  0x0CA6C310 or beyond. S_SBMOT starts at 0x0CB08000 and S_MOT is at 0x0CC00000. LIB's Sonic world image already
  holds all three (`sa1_world_image.c:27-28`). The only conflict is with stage overlays (STGxx, MINICART, SHOOTING)
  in the same 0x0C900000 slot. G-SA1's WINDOW/GRID worlds hold none of those.
* **Minimal faithful plan.**
  1. Decompile the 22 functions above.
  2. In LIB, make kind 8 a Sonic world (chr 0) booted at level 19 act 0, with B_CHAOS7 required and no stage tasks.
     LIB creates the game's own trigger task 0C914D60 at boot. From then on the game's own path transforms Sonic.
  3. Host options: start rings, ring drain (the game's 0C917088 on a LIB driver task), behaviour at 0 rings, a
     physics fix for the revert, water as floor, stage sound bank 19, and transform-from-Sonic.

  Transform-from-Sonic is **not in SA1**. It is a host feature.
* **Verification.**
  - Level-19 references already work in the stock harness. The boss and the stage run interpreted, and Super
    Sonic is controllable after the boss intro (stick locked until step 498).
  - Once B_CHAOS7 has C: route it in the harness. This needs an image guard, because 0C9004D4 is also a function
    entry in STG05, 0C900480 in B_EGM3, and 0C914D60 in ADV02 and MINICART.
  - LIB itself needs new "free-roam" references: level 19 with the level tasks skipped and the trigger task
    created. LIB replays those strictly (section 6).

## 1. The transformation in detail (verified)

### 1.1 Trigger (B_CHAOS7, Perfect Chaos stage)

| Step | Code | What |
|---|---|---|
| level task | 0x0C900000 (exec of `D_8C19DE30[19]`; split as *data* in `asm/overlays/b_chaos7/data/0C900000.src`) | mode switch over 0C914E36 / 0C914F36 / 0C914F78 |
| stage init | 0C914DC6 (from 0C914F78) | stage textures, object lists; `8C098CA0(6, 0C914D60, tp)` (child task), boss 0C917300, music task 0C914D8E |
| trigger | 0C914D60 (46 B) | `twp->timer++`; when > 1: `SetInputP(0, 46)` and `DestroyTask(tp)` (8C09865C) |
| list-2 task | `D_8C19E0E0[19]` = 0C900080 (656 B) | fog / lighting by stage (not Super Sonic) |
| boss init | 0C917300 state code at 0C91741E-0C91742A | `8C08A074()` (rings = 0) then `8C08A198(50)`: **Perfect Chaos starts with 50 rings** |

Harness trace `ss_l19_idle` (level 19, act 0, Sonic, no input):

| Step | State |
|---|---|
| 0 | mode 1 |
| 1-5 | mode 12, action 18; the cause is stage code, mechanism **UNVERIFIED** |
| 5 | rings = 50 |
| 6 | smode 46, equipment 0x8000, mode 71 / reqaction 130 |
| 7-68 | mode 74 (fall) |
| 69 | lands on the water plane (attr 0x28000000) and stands in mode 71, action 130 |

### 1.2 The request handler (1ST_READ)

`code_8C0AFF1A.c:406-418` (and the same case in `code_8C0B07E4.c:293`):

```c
case 46:
    pwp->equipment |= 0x8000;
    tp = CreateElementalTask(2, 2, 0x0C900480);          /* aura task, B_CHAOS7 */
    if (tp != NULL) tp->twp->counter.b[0] = twp->counter.b[0];
    if ((tp = CreateElementalTask(8, 2, func_8C0B32B6)) != NULL)   /* physics task */
        *(Sint8 *)tp->awp = twp->counter.b[0];
    twp->mode = 71; pwp->mj.reqaction = 130; twp->flag &= ~0x2500;
```

Request 46 is taken in almost every normal mode. `func_8C0AFF1A` is called from modes 1-17, 19-21, 26, 32, 33,
36-43, 46-61, 69, 70 and 79. `func_8C0B07E4` covers 20, 21, 23, 25-29, 33-35 (from `code_8C0A966C.c`). In the
super modes the handler is `func_8C0B0F24`, which has no case 46, so a second request 46 is ignored.

The flag: `playerwk.equipment`, offset 0x004, Sint16, bit 0x8000 (`include/sa1/player.h:137`). It is distinct from
`item & 0x8000`, the invincibility power-up, which `SonicModeMachine` turns into `twp->flag 0x8000`
(`code_8C0A966C.c:128`).

### 1.3 Physics (1ST_READ 8C0B3240-8C0B332E, `code_8C0B3240.c`, asm `asm/1st_read/text/8C0B3240.src`)

`PInitialize` copies `Sonic_Physics_Data[chr]` (`code_8C06C300.c:68`). The table at 0x8C1BDFE4 holds 8 entries:

| Index | Character | center_height |
|---|---|---|
| 0 | Sonic | 5.4 |
| 1 | Eggman | 5.3 |
| 2 | Tails | 4.5 |
| 3 | Knuckles | 5.7 |
| 4 | Tikal | 5.0 |
| 5 | Amy | 5.0 |
| 6 | E-102 | 14 |
| 7 | Big | 8 |

There is no Super Sonic entry. The task `func_8C0B32B6` overrides playerwk fields; the field names below are from
the header:

| Field (p offset) | Sonic (entry 0) | Super (pool word) |
|---|---|---|
| air_resist (+0x54, pwp+0xF0) | -0.008 | -0.001 (0xBA83126E) |
| air_resist_air (+0x50, pwp+0xEC) | -0.028 | -0.002 (0xBB03126E) |
| air_accel (+0x40, pwp+0xDC) | 0.031 | 0.05 (0x3D4CCCCC) |
| center_height (+0x80, pwp+0x11C) | 5.4 | 6.4 (0x40CCCCCC) |

The exec `func_8C0B3280` destroys the task when the player is gone or the bit 0x8000 is clear (8C0B32A2). The dest
`func_8C0B3240` writes the saved air_resist into **max_x_spd** (pwp+0xAC) and the saved air_resist_air into
**run_accel** (pwp+0xD8). It restores air_accel correctly and sets center_height = 5.4 (0x40ACCCCC).

After a revert, Sonic therefore has max_x_spd = -0.008 and run_accel = -0.028, and keeps the Super values for
air_resist and air_resist_air. This is a game bug. The game hides it because a revert only happens when Perfect
Chaos ends or the rings run out. Its in-play effect is **UNVERIFIED**: no trace reached 0 rings. Section 4.4 has
the host fix.

Measured: max player speed in the run segment of `ss_l19_play` (mode 72, steps 500-720) is 10.15 units/step. The
5,000-step trace peaks at 24.1, but that includes stage-driven segments (boss launches, path mode 73), so it is
not a player-physics number.

### 1.4 Revert and death requests (`func_8C0B0F24`, `code_8C0B0E94.c:39`)

* **47 (PL_OP NORMAL):** `equipment &= ~0x8000`, back to standing (`func_8C0A9660`: mode 1). Lines 109-116.
* **50 (KILLED):** `equipment &= ~0x8000`, mode 82, action 86, SE 0x570. Lines 122-128.
* **Who sends 47:** the boss's ring drain 0C917088, when the ring count reaches 0 (1.6). The aura task and the
  physics task free themselves on the next step once the bit is clear.

### 1.5 Super modes and actions

Super Sonic's actions 130-141 come from Sonic's action table 0x8C1C55A0 (dumped from the disc image):

| Action | Object | Motion | Meaning |
|---|---|---|---|
| 130 | 0x0C909D2C Super Sonic | 0x0C914D00 Stationary | stand |
| 131 | " | 0x0C90EEA4 Begin Moving | |
| 132 | " | 0x0C90FFC8 Move at Low Speed | |
| 133, 134 | " | 0x0C91116C Move at High Speed | |
| 135 | " | Sonic_Spring_Launch (1ST_READ) | spring |
| 136 | " | Begin Falling From Spring Launch (1ST_READ) | |
| 137 | " | Sonic_Fall (1ST_READ) | fall |
| 138 | " | Sonic_Landing (1ST_READ) | |
| 139 | " | S_MOT Level Clear Pose | |
| 140 | " | S_MOT Level Clear Pose to Standing | |
| 141 | 0x0C90BD04 Super Sonic Jumpball | Sonic_Spin_Jumpball | jump ball |

Physics per mode in SonicMain (`code_8C0A77BC.c:839-930`); transitions in SonicModeMachine
(`code_8C0A966C.c:1686-1843`):

| Mode | Meaning | Reached by |
|---|---|---|
| 71 | stand | request 46 / 24, landing, stop |
| 72 | move: run actions 131-134 by speed, `func_8C0AE6D8` (100 B) | stick |
| 73 | path (request 1, hpos = 0), trail effect | stage paths (the boss uses it) |
| 74 | air / fall, action 137 | leaving the ground; action button `func_8C0B11C0` / `func_8C0AFE20` (in the air: stop, speed 0, measured at step 728) |
| 75, 76 | spring launch (requests 4 / 3), action 135 | springs |
| 77 | homing attack (`func_8C0AF18C` / `func_8C0AF280`), 360 frames max | jump again in the air |
| 78 | jump, action 141 | jump button (`func_8C0AFC98`) |

Super Sonic has no spin dash, light speed dash or object holding: the super handlers never call 8C0B12E0,
8C0B1352 or 8C0B14E0. Debug mode 83 returns to mode 71 only for stage 0x13xx (`code_8C0A966C.c:1870`).

Trace `ss_l19_play` (script `ss_play.txt`):

| Step | Event |
|---|---|
| 614 | run (mode 72, action 134) |
| 620 | jump (78, 141) |
| 636 | homing (77) |
| 641 | fall (74) |
| 717 | jump |
| 728 | B in the air: mode 74, speed 0 |
| 745 | stand (71) |

### 1.6 Rings (B_CHAOS7 boss code)

`func_0C917088` (68 B) is the drain (SADX `SuperSonic_SubtractRings`). It takes R4 = the caller's taskwk:

```c
if ((Sint16)func_8C08A2BC() > 0) {          /* ring count B_8C752BE4 */
    if (++*(Sint16 *)((Uint8 *)twp + 0x0E) >= 60) { *(Sint16 *)((Uint8 *)twp + 0x0E) = 0; func_8C08A198(-1); }
} else {
    func_8C08A074();                        /* rings = 0 */
    SetInputP(0, 47);                       /* back to normal Sonic (player 0 only) */
}
```

It is called only from the boss main 0C917300 (at 0C917650), in one fight state, while a float at +0x70 of a
saved pointer is > 0. The meaning of that condition is **UNVERIFIED**.

Measured in `ss_l19_drain` (5,000 steps, stick forward):

* The drain starts after the intro: rings 50 to 49 at step 558, then 1 ring every 60 steps.
* It pauses during the stage's path segment: rings stay at 4 from step 3765 (mode 73) until about step 4853.
* Rings are also picked up on the course.
* The count never reached 0, so the revert at 0 rings is derived from the code only (**UNVERIFIED** in a run).

### 1.7 Readers of `equipment & 0x8000` in 1ST_READ

Asm scan: `MOV.W @(4,Rn)` followed by `TST`/`AND`/`OR` with a 0x8000 register. Script `scan_equip.py`; output
`equip8000_scan.txt`. The `OR`/`AND` hits at the top of every character's mode machine are `twp->flag` (also at
+4), and so are 8C082FD4, 8C084324 and 8C084BA0; they are excluded below.

| Function | Address of the test | Effect when super |
|---|---|---|
| SonicMain 8C0A77BC | 8C0A786E (`code_8C0A77BC.c:152`) | Sonic's CCL info attack/defence levels 3/3, as with the invincibility item |
| SonicModeMachine 8C0A966C | 8C0AABE2 (`:602`) | mode 20 dispatches to `func_8C0B0F24` |
| SonicModeMachine 8C0A966C | 8C0AE06C (`:1870`) | debug mode 83 exit (with stage 0x13xx) |
| func_8C0AF18C (homing start) | 8C0AF1A0, 8C0AF1FC (`code_8C0AEFA0.c:123,137`) | effect task 8C0B2DD8 (B_CHAOS7 0C9004D4) instead of 8C0B2DB2; straight dash at speed 10 instead of 5 |
| func_8C0AF280 (homing step) | 8C0AF3BC, 8C0AF47C, 8C0AF502 (`:208,228,236`) | targets in every direction (no 0x5000 cone), turn rate 0x1000 instead of 0x800, speed 10 instead of 5 |
| func_8C0AFB10 (damage) | 8C0AFC16 (`code_8C0AFB10.c:105`) | a hit only halves `spd.x`: no knock-back, no ring loss, no death; `wtimer = 120` (blinking) is still set |
| func_8C071D40 (player common) | 8C071D60 (`code_8C071B26.c:102`) | aligns ang.x/z to the gravity angles at any speed (normal: only below dash_speed) |
| func_8C0B3280 (physics task) | 8C0B32A2 | frees itself (dest restores, 1.3) |

Writers of the bit:

| Function | Change |
|---|---|
| `func_8C0AFF1A` / `func_8C0B07E4` | set (request 46) |
| `func_8C0B0F24` | clear (requests 47, 50) |

Not by the flag:

* The texture swap: `code_8C0AE0E0.c:111`, by action range 130-141.
* The welds: by stage, see 1.8.
* The idle voice: `func_8C07DCE0` plays voice 396 instead of 390 for Sonic on **level 19**
  (`code_8C07DCE0.c:45`).

**Water running is not code.** The Perfect Chaos water is a solid plane of landtable 1900 (0x0C901214; attr
0x28000000 measured). No 1ST_READ function tests the flag for water. Drowning has no super check either: the
breath code 8C06C6F8 has no 0x8000 test in the scan. Whether Super Sonic can drown is **UNVERIFIED** in a run.

### 1.8 Welds (why the stage number matters)

SonicMain mode 0 (`code_8C0A77BC.c:197-207`) sets `inpmode = 2` on every `SonicWeldList` (0x8C1C5FA4) entry whose
`objptr` is the Super Sonic object 0x0C909D2C. That applies when `(GetStageNumber() & 0xFF00) == 0x1300`; otherwise
it sets `inpmode = 0`.

`PJoinVertexes` (`code_8C07A55C.c:72-100`) skips inpmode 0 at setup (no `org` buffer, no depths) and does nothing
for it later. So a Sonic created on any other stage shows Super Sonic **without his 12 welds** (open seams). The
inpmode cannot be switched after mode 0, because the setup (`jvmode` 0 to 1) runs once.

**The world must say stage 0x13xx when Sonic's task starts.**

### 1.9 B_CHAOS7 Super Sonic code (no C yet)

The SADX names are in brackets (`reference/sadx_pc_functions.csv`).

| Function | Size | Role |
|---|---|---|
| 0C900480 [Sonic_SuperAura_Load] | 28 | exec = 0C91D486, disp = 0C91D222, dest = 0C90049C; first display; sound state byte 0x0CA6C310 = 0 |
| 0C90049C [Sonic_SuperAura_Delete] | 26 | `func_8C0945A0(0x40B)`, `(0x40C)`: stop tones 1035, 1036 |
| 0C91D486 [Sonic_SuperAura_Main] | 1710 | see the list below |
| 0C91D222 [Sonic_SuperAura_Display] | 612 | see the list below |
| 0C91D200 | 34 | fills the particle parameters D_0CA6B418, tail-calls 8C046A9C |
| 0C9004B6 / 0C91DB60 / 0C91DDB6 / 0C91DE82 | 30+338+204+80 | super variant of 8C0B3FC0 (8C0B41E0 / 8C0B4600 / 8C0B46CC): jump-ball after-images (mode 77); texlists ss_ball_a / ss_ball_b |
| 0C9004D4 / 0C91DCB2 / 0C91DED2 / 0C91DFE0 | 30+260+148+54 | super variant of 8C0B3FDA (8C0B4332 / 8C0B4724 / 8C0B4820): homing start ball |
| 0C9004F2 / 0C91E06A / 0C91E1D2 / 0C91E26A / 0C91E5E2 / 0C91E016 | 46+360+152+888+52+84 | super variant of 8C0B48B4 (8C0B48E0 / 8C0B4A48 / 8C0B4B20 / 8C0B4E62 / 8C0B4860): Sonic_Effect_Trail copy (modes 73, 77); texlists ss_dash01-20 (D_0CA6B2E0[], D_0CA6B380) |
| 0C914D60 | 46 | the trigger (1.1) |
| 0C917088 [SuperSonic_SubtractRings] | 68 | the drain (1.6) |

The aura exec 0C91D486 does the following:

* Frees itself when `GetPlayerWorkPtr(pno)` is NULL or bit 0x8000 is clear.
* Advances the animation counter `btimer++` and sets `scl.x = sqrt(|spd.x|) * 0.4` (water-effect scale).
* On the ground, when `spd.x > 2` and the land object stood on (`pwp->lclop->model`) is B_CHAOS7's water model
  D_0C966D08, it spawns random water particles (0C91D200 to 8C046A9C).
* Runs the whoosh state machine:

  | State | Condition | Action |
  |---|---|---|
  | 0 | `spd.x > 7` | play tone 1035, go to state 1 |
  | 1 | `spd.x > 13` | stop 1035, play 1036, go to state 2 |
  | 1 | `spd.x < 6` | stop 1035, go to state 0 |
  | 2 | `spd.x < 6` | stop 1036, go to state 0 |

  Sounds are played with `func_8C0940DA(tone, 0, 0, 127)`.
* Finally calls the display.

The aura display 0C91D222 does the following:

* Skips sub-step 1 (`B_8C754EC8 != 0`).
* Sets the 8-frame texlists D_0CA6B210[btimer & 7] (s1sonic) and D_0CA6B250 (s2sonic).
* Builds the matrix at the player with rotations Z, X, -Y and a mirroring `njScale(NULL, -1, 1, 1)`.
* Draws one model by speed with 8C036F12:

  | Speed (`spd.x`) | Model |
  |---|---|
  | > 13 | Aura_Dash_2 (0x0C90D928) |
  | > 7 | Aura_Dash_1 (0x0C90CFA4) |
  | otherwise | Aura (0x0C90C620) |

* On the water model it also draws the water splashes (0x0C90DC14, 0x0C90DF00, texlists D_0CA6B290 ss_waterl*).

Aura materials (decoded from the disc image): flags 0x86B0A400 = SRC_ALPHA / ONE (**additive**), USE_ALPHA,
DOUBLE_SIDE, IGNORE_LIGHT, texture. The water splashes use 0x96B1A400 (SRC_ALPHA / INV_SRC_ALPHA).

**The effect tasks are clones.** Matched against their 1ST_READ originals (`simil.py`, opcodes with operand shape;
`pairs.txt`):

| Original | Clone | Instructions | Note |
|---|---|---|---|
| 8C0B41E0 | 0C91DB60 | 169 / 169 | identical |
| 8C0B4332 | 0C91DCB2 | 91 / 91 | identical |
| 8C0B4600 | 0C91DDB6 | 102 / 102 | identical |
| 8C0B4724 | 0C91DED2 | 74 / 74 | identical |
| 8C0B4860 | 0C91E016 | 42 / 42 | identical |
| 8C0B4A48 | 0C91E1D2 | 76 / 76 | identical |
| 8C0B4E62 | 0C91E5E2 | 26 / 26 | identical |
| 8C0B48E0 | 0C91E06A | 141 of 142 | |
| 8C0B4B20 | 0C91E26A | 329 of 369 | |
| 8C0B46CC | 0C91DE82 | 22 of 40 | |
| 8C0B4820 | 0C91DFE0 | 18 of 27 | |

The pools differ: SUPERSONIC slices instead of SON_EFF. The originals have C (`code_8C0B4040.c`,
`code_8C0B4600.c`, `code_8C0B48E0.c`), so these clones are cheap to decompile.

All effect texlists are slices of the SUPERSONIC NJS_TEXNAME array (0x0C901B1C, 73 names). LIB resolves textures by
texname address (`LIB/src/sa1_capture.c:194-217`), so they resolve into the SUPERSONIC archive with no extra table.

### 1.10 Sound and music

* **Tones.** 1035 / 1036 map through the tone table 0x8C1C19B4 to **bank 5**, threshold 1026, requests 8 / 9. The
  range 1027-1038 is the Perfect Chaos stage range, which also includes the stage loop 1033 played by 0C914E36.
  Bank 5 comes from the stage MLT. Stage 19 is `CHAOS7B.MLT` (`LIB/src/sa1_audio.c:33`). With another stage bank
  loaded, these tones play whatever program 8 / 9 that bank holds. That is derived from the table only; the
  contents of CHAOS7B.MLT bank 5 are **UNVERIFIED**.
* **Music.** No Super Sonic music in 1ST_READ. The Perfect Chaos stage starts music itself:
  - 0C914D8E after 60 frames calls 8C107D80 and 8C107DAA;
  - the boss calls 8C107AAA by phase.

  The track list holds CHAOS_P1.ADX (22) and CHAOS_P2.ADX (23). Which plays when is **UNVERIFIED**.
* **Voice.** The level-19 idle voice is 396 (1.7).

## 2. What lives where

| Piece | Image | C | Verified | Needed for free roam |
|---|---|---|---|---|
| Request handlers 8C0AFF1A / 8C0B07E4 (case 46), 8C0B0F24 (606 B) | 1ST_READ | yes | routed traces (5) | yes |
| Super modes in SonicMain / SonicModeMachine | 1ST_READ | yes | routed traces | yes |
| Physics task 8C0B32B6 (120) / 8C0B3280 (54) / 8C0B3240 (64) | 1ST_READ | yes | routed traces; the dest only by reading | yes |
| Run selector 8C0AE6D8 (100); effect creators 8C0B2CB2 / 8C0B2D8C / 8C0B2DD8 (38 each) | 1ST_READ | yes | routed traces | yes |
| Homing / damage / gravity-alignment branches | 1ST_READ | yes | routed traces (homing, run); damage branch not hit | yes |
| SetInputP 8C07C65E | 1ST_READ | yes (`code_8C07C550.c:67`) | routed traces | yes |
| Rings 8C08A074 / 8C08A198 / 8C08A2BC | 1ST_READ | yes | routed traces | for the ring options |
| Particle spawner 8C046A9C (130) | 1ST_READ | **no** | - | link only: reached by the water splash, which is dormant off the B_CHAOS7 water |
| Aura task (5 functions, 2,410 B) | B_CHAOS7 | **no** | runs interpreted in the level-19 traces | yes |
| Effect tasks (14 functions, 2,726 B) | B_CHAOS7 | **no** | interpreted (their 1ST_READ creators are in the coverage) | yes |
| Trigger 0C914D60 (46) | B_CHAOS7 | **no** | interpreted | yes: the transformation itself |
| Drain 0C917088 (68) | B_CHAOS7 | **no** | interpreted | only for the drain option |
| Boss, level task, list-2 task, camera, debris objects (about 30 KB) | B_CHAOS7 | no | - | **no** |

Coverage (`cov_super_only.txt`): functions in the player range that ran only in the Super trace, not in the six
EC1 Sonic traces:

* 8C07506E, SetInputP, 8C07CB54;
* 8C0AE6D8, 8C0B0F24, 8C0B2CB2, 8C0B2D8C, 8C0B2DD8, 8C0B3280, 8C0B32B6;
* rings 8C08A074 / 8C08A2BC / 8C08A580 / 8C08A5AE / 8C08A5B6.

## 3. Current state

**SADC**

* B_CHAOS7 is split. `config/overlays/b_chaos7.yaml` sets the code ranges 0C900080-0C900560, 0C914D60-0C91C9B8 and
  0C91CA78-0C91E664, plus the PRS SHA-1. `build/B_CHAOS7.symbols.txt` lists 191 functions.
* Split defects (they do not block this work):
  - the level task 0x0C900000-0x0C900080 is classed as data;
  - 0C91C9C0-0C91CA78 is code dumped as `.DATA.L`.
* No `src/overlays/b_chaos7`.
* 1ST_READ names 0C900480 twice: `func_0C900480` in `code_8C0AFF1A.c:27` and `B_CHAOS7_exec_0C900480` in
  `code_8C0B07E4.c:47`.
* Data references by name: `B_CHAOS7_Super_Sonic` (`code_8C0A77BC.c:36`) and `B_CHAOS7_texlist_SUPERSONIC`
  (`code_8C0AE0E0.c:45`).

**LIB**

* Kind 8 is loaded (manifest `sa1_manifest.c:128-130`: the SUPERSONIC archive only, object substitutions for
  puppets).
* The world image for kind 8 requires B_CHAOS7 (`world/sa1_world_image.c:28`).
* Kind 8 cannot tick:
  - `world/sa1_api_world.c:173` rejects `kind > 7`;
  - `world/sa1_world_boot.c:107-114` has no character descriptor for 8.
* `gen/policy/stubs.tsv:43-47`: 0C900480 (both names), 0C9004B6, 0C9004D4 and 0C9004F2 are `fatal` stubs.
* DISC mode reads only STG00-STG10 (`sa1_coladapt.c:2152-2156`).
* Audio already maps stage 19 to CHAOS7B.MLT.
* The loader loads only the explicit archive for kind 8 (`sa1_loader.c:391-397`), so SONIC and SON_EFF are
  missing for a reverted Super Sonic.

**G-SA1** (0.5.0 source)

* Kind 8 is excluded from `kPlayableMask` (`src/core/session.cpp:20`).
* `GSA1.CHARACTERS` (`addon/lua/gsa1/sh_config.lua:9-17`) says that Super Sonic cannot be played.

**Harness**

* Level 19 loads and runs with the stock `sa1h.exe`.
* `file_crcs` (`harness/src/t_sim.c:1826`) only knows `STG%02d`, so level-19 traces have stage_crc32 = 0 although
  the landtable is 0x0C901214.
* The harness has a single code map, so routing overlay C by address needs a guard (6.2).

**Memory layout for a Super Sonic world**

| Overlay | Range | Note |
|---|---|---|
| B_CHAOS7 | 0x0C900000-0x0CA6B454 | file |
| B_CHAOS7 BSS | to at least 0x0CA6C310 | highest BSS address referenced, 119 references |
| S_SBMOT | from 0x0CB08000 | |
| S_MOT | 0x0CC00000-0x0CC77490 | |
| TIKAL_PROG | 0x0CB00000-0x0CB356D4 | not in a Sonic world |

B_CHAOS7, S_SBMOT and S_MOT do not overlap. G-SA1 Sonic worlds already compose all three. Only a stage overlay
cannot coexist, so a Super Sonic world can never use DISC collision of levels 1-10; level 19's own landtable is
fine.

## 4. Design: a free-roaming Super Sonic in LIB

### 4.1 World and boot (the game's own path)

1. Kind 8 uses the Sonic image with B_CHAOS7 required, plus the new B_CHAOS7 code map (pclower image, activated by
   the PRS SHA-1).
2. Boot with chr 0, so that `counter.b[1] = 0`, the main is SonicMain, and the physics entry is 0.
3. Use **level 19 act 0** (stage 0x1300) by default. `SA1_CREATE_STAGE` with another level is rejected
   (`SA1_ERR_INVALID_ARGUMENT`), because of the welds (1.8). No stage tasks.
4. After `LoadPlayerTask`, create the game's trigger task:
   `CreateElementalTask(2, 0, B_CHAOS7__func_0C914D60)`. It runs before the player (list 0). Its second exec
   calls `SetInputP(0, 46)`, which is step 1. Sonic is in mode 1 or 9 at step 1, and both take request 46.

   From there everything is the game's own code:
   - the request handler;
   - the aura task 0C900480;
   - the physics task;
   - the super modes and effects.

   The harness free-roam references use the same task at the same point (6.3).

Side effects of stage 0x1300 for a normal-mode Sonic, for example after a revert:

* the idle voice 396;
* the level-19 voice table `D_8C1BE7A0[19]`;
* debug mode 83 returns to super.

The death zones are LIB's own kill-plane list. No stage task reads the level.

### 4.2 Rings (host options, game code)

* **Start rings.** `func_8C08A074()` then `func_8C08A198(n)` at boot, the boss's own sequence. Default 50, the
  Perfect Chaos rule. With the interaction layer (`libsa1_interaction.md` 2.2) the count is also in
  `sa1_state_ex.rings`.
* **Drain.** A LIB driver task with its own taskwk calls the decompiled `B_CHAOS7__func_0C917088(twp)` once per
  step while bit 0x8000 is set. Its exec is a synthetic adapter (0x7F00xxxx, like the camera adapter); the counter
  is at taskwk+0x0E. The game gates the drain by boss state; free roam has no boss, so the option decides
  instead. This is a host composition of game code; document it as such.
* **At 0 rings.**
  - Revert, the game's behaviour: 0C917088 itself calls `SetInputP(0, 47)`.
  - Stay Super: the driver stops calling 0C917088 when the count is 0, so the game code never sees 0.

### 4.3 Transform from Sonic (host feature, not in SA1)

Later games transform with 50 rings and a jump press in the air. Two implementations:

* **v1, recommended.** G-SA1 re-creates the character in place as kind 8, carrying the ring count over (the
  `GSA1.Recreate` path already used for upgrades). Reverting is a re-create as kind 0. This has no level-19 side
  effects on normal Sonic and no physics bug. Momentum is lost unless LIB gains a start velocity.
* **v2, optional.** A "super-capable" Sonic world: kind 0 booted at stage 0x1300, with the SUPERSONIC archive added
  to Sonic's archives. A new `sa1_character_order(ctx, id, 46 | 47)` calls `SetInputP` inside the world. The game
  then transforms with momentum, but needs the physics fix (4.4) and accepts the level-19 side effects (4.1).

### 4.4 Revert physics fix (host deviation)

The fix is the option `SA1_SUPER_FAITHFUL_REVERT`, off by default. Without the option, after any step in which
bit 0x8000 went from 1 to 0, LIB rewrites `pwp->p` max_x_spd, run_accel, air_resist and air_resist_air from
`Sonic_Physics_Data[0]` in the arena (0x8C1BDFE4 + 0x10 / 0x3C / 0x54 / 0x50). This is listed in the API docs as
a patch of game state. With the option, LIB keeps the game's bug.

### 4.5 Textures, rendering, audio, water

* **Archives for kind 8:** SUPERSONIC (archive 0, texlist 0x0C901EA0), then Sonic's PVM list (SONIC, SON_EFF),
  then the common EFF_REGULAR. This needs a loader change: explicit archive plus PVM list.
* **Rendering:**
  - the aura is additive, double-sided and drawn under a mirrored matrix;
  - the effect clones use `njSetConstantMaterial` (8C60EDF0) and the draw flags 8C05694A, the same path as
    Sonic's own jump-ball effects and the round-4c light-speed aura;
  - aura draws come from the exec (sub-step 0 only) and from `tp->disp`;
  - the capture classifies them as effect draws, because the sub-texlists are not texlist 0x0C901EA0. Check this
    in the LIB test.
* **Audio:** while the local character is Super Sonic, G-SA1 calls `sa1_audio_set_stage(audio, 19)` (CHAOS7B.MLT)
  so that tones 1035 / 1036 are the right sounds. This is an option and on by default. Otherwise those two tones
  should be muted, not played from the wrong bank.
* **Water:** GMod water is not solid. The host option "Super Sonic runs on water" exports water surfaces as
  `SA1_SURFACE_SOLID` instead of `WATER` for kind-8 worlds. It is on by default; the Perfect Chaos water is a
  solid plane too. The game's splash effect stays dormant: it compares the land model with B_CHAOS7's D_0C966D08.
  Whether the collision adapter could alias that model is **UNVERIFIED**, so leave it out.
* **Interaction (round 5 layer):**
  - attack level 3/3;
  - host damage only halves speed (blinking for 120 steps);
  - homing targets host objects in every direction.

### 4.6 API (draft, API 0.4)

* `SA1_CHAR_SUPER_SONIC` ticks.
* `sa1_character_desc` grows at its end; `struct_size` keeps old hosts working:

  ```c
  uint32_t super_flags;   /* SA1_SUPER_*, kind 8 (and super-capable Sonic) only */
  int32_t  super_rings;   /* SA1_SUPER_RINGS: start rings (default 50) */
  #define SA1_SUPER_RINGS           0x1u  /* super_rings is given */
  #define SA1_SUPER_NO_DRAIN        0x2u  /* do not run 0C917088 */
  #define SA1_SUPER_KEEP_AT_ZERO    0x4u  /* drain stops at 0: stays Super */
  #define SA1_SUPER_FAITHFUL_REVERT 0x8u  /* keep the game's physics-restore bug */
  #define SA1_CREATE_SUPER_CAPABLE  0x200u /* kind 0: stage 0x1300 + SUPERSONIC archive (4.3 v2) */
  ```

* Optional: `sa1_character_order(ctx, id, int32_t order)` = `SetInputP(0, order)` in the world. This is the
  generic PL_OP entry point for orders such as SUPER 46, NORMAL 47, springs 3 / 4 and path 1.
* `sa1_state_ex.equipment & 0x8000` already reports Super. Rings come from the interaction layer.
* `sa1_character_tick` doc line 520: remove "Super Sonic" from the list of kinds that cannot tick.

### 4.7 G-SA1 and the Q menu (Options > G-SA1 > "Super Sonic")

| Option | ConVar (suggested) | Default |
|---|---|---|
| Super Sonic in the character list | (always) | - |
| Start rings | `gsa1_super_rings` | 50 |
| Lose a ring every second | `gsa1_super_drain` | 1 (game) |
| When rings run out: back to Sonic / stay Super | `gsa1_super_at_zero` | 0 = back to Sonic (game) |
| Keep the game's physics bug after turning back | `gsa1_super_faithful_revert` | 0 |
| Run on water | `gsa1_super_water_floor` | 1 |
| Perfect Chaos sound bank while Super | `gsa1_super_sounds` | 1 |
| Super music (jukebox track) | `gsa1_super_music` | off (owner) |
| Transform from Sonic: needs N rings, key / double jump | `gsa1_super_transform`, `gsa1_super_transform_rings` | off / 50 |

## 5. Decompile list

Every function needs C in its own image directory: `SADC/src/overlays/b_chaos7/` for B_CHAOS7 and `src/1st_read`
for 8C046A9C. Use the decomp workflow `tools/workflows/decomp_libsa1.js` with image B_CHAOS7 and dir
`overlays/b_chaos7`, as done for TIKAL_PROG. Sizes are from `build/B_CHAOS7.xrefs.json`, with pools.

| File (suggested) | Functions | Bytes | Template |
|---|---|---|---|
| `code_0C900480.c` | 0C900480, 0C90049C, 0C9004B6, 0C9004D4, 0C9004F2 | 160 | small wrappers |
| `code_0C91D200.c` | 0C91D200, 0C91D222, 0C91D486 | 2,356 | new (FP-heavy; SA1_FMAC per `tools/fmac_audit.py`) |
| `code_0C91DB60.c` | 0C91DB60, 0C91DCB2, 0C91DDB6, 0C91DE82, 0C91DED2, 0C91DFE0, 0C91E016, 0C91E06A, 0C91E1D2, 0C91E26A, 0C91E5E2 | 2,620 | 1ST_READ `code_8C0B4040.c` / `code_8C0B4600.c` / `code_8C0B48E0.c` (7 identical shapes) |
| `code_0C914D60.c` | 0C914D60 | 46 | - |
| `code_0C917088.c` | 0C917088 | 68 | - |
| `src/1st_read` (chunk 8C046960) | 8C046A9C | 130 | particle code 8C046960 (LIB `rt_effect.c`) |

Total: 22 functions, 5,380 B. All outside calls are covered:

* the SDK and libm in LIB `src/rt`;
* sound stubs 8C0940DA / 8C0945A0 as `event` lines in `stubs.tsv:147,152`;
* everything else in 1ST_READ has C.

Also: rename to one canonical name per address (`func_0C900480` vs `B_CHAOS7_exec_0C900480`), and drop
`stubs.tsv:43-47`.

## 6. Verification

### 6.1 Done in this research

* **Reference traces with the stock interpreter** (`harness/build/sa1h.exe trace --level 19 --act 0 --chr 0`, objects
  on):

  | Trace | Steps | Content |
  |---|---|---|
  | `ss_l19_idle` | 240 | |
  | `ss_l19_moves` | 402 | stick locked |
  | `ss_l19_long` | 3,000 | |
  | `ss_l19_play` | 909 | script `ss_play.txt`: transform, stand, run, jump, homing, air stop, turn |
  | `ss_l19_drain` | 5,000 | |

  Setup takes 210 M instructions. A step takes 0.37-0.43 M instructions. The stick is locked by the stage until
  step 498.
* **Routed checks:**
  - `sa1h_ga_rsp.exe --route` (1,523 GA-lowered functions, LIB's model): **identical** on `ss_l19_play` (909 steps)
    and `ss_l19_drain` (5,000 steps).
  - `sa1h_nm_pc.exe --route` (1,559 functions): identical on `ss_l19_play`. On `ss_l19_drain` it first differs at
    step 1661 (`twp->flag` 0x8, then one ring). A manual bisect (`bisect.py`) isolates `func_8C087E86`
    (`code_8C087B80.c:168`, the O_GRING ring-group helper). It compares `ctp->exec == DestroyTask`, and a guest
    exec word never equals a native function address in the `--pc` build. This is a harness `--pc` artifact, not
    Super Sonic. The GA build passes the same steps.

### 6.2 After the B_CHAOS7 decompile (harness)

* Add `harness/tests/sim/super_pc_*.txt` with `# args: --level 19 --act 0 --chr 0` (the scripts above) as
  references. Record them with the screen on.
* `harness/build.py --non-matching --pc --ga --files ... src/overlays/b_chaos7/*.c`, then
  `regress.py --route` on all references.
* **Required first: an image guard for routed overlay functions.** Route an address 0x0C9xxxxx only while the
  0x0C900000 occupant is the TU's image (CRC / SHA-1 of the loaded overlay). Function entries of other overlays at
  the same addresses:

  | Address | Also an entry in |
  |---|---|
  | 0C900480 | B_EGM3 |
  | 0C9004D4 | **STG05**: knux_rm3 runs it |
  | 0C914D60 | ADV02, MINICART |

* `file_crcs`: level 19 should name B_CHAOS7, and the other boss levels their own files.

### 6.3 LIB replay: free-roam references (new harness options)

A full Perfect Chaos reference cannot be replayed by LIB without decompiling the boss, which launches Sonic, locks
input and owns the drain. Instead, add two harness options:

* `--no-level-tasks`: do not create `D_8C19E0E0[level]` / `D_8C19DE30[level]` at the stage start.
* `--create-task ADDR[,LIST]`: after `LoadPlayerTask`, `CreateElementalTask(2, 0, ADDR)`, original code.

Record with them, using `--level 19 --act 0 --chr 0 --no-level-tasks --create-task 0C914D60`:

* `super_free_run`: run, jumps, homing, springs if objects;
* `super_free_long`: 3,000 steps.

The land is landtable 1900 (water plane and highway). LIB replays them strictly in DISC mode level 19. That needs
a `sa1ca_disc_stage` mapping 19 to B_CHAOS7.PRS, which is already the kind's span, so `disc_image()` must not
drop it.

Exit criteria: V5 / V6 bit-exact, x86 = x64, and like the other references, every record compared.

### 6.4 LIB synthetic tests (GRID, no reference)

New `tests/super_test.c`. Every step below is checked with the game's own code paths:

1. Create kind 8 on a flat GRID floor.
2. By step 2: mode 71, equipment 0x8000, `p.air_accel == 0.05f`, `p.center_height == 6.4f`, and an aura task and
   a physics task in the task lists.
3. The 12 Super welds are active (inpmode 2).
4. The render has an additive draw with SUPERSONIC textures.
5. Drain on: rings 50 to 49 after 60 steps. At 0: equipment clear, mode 1. With the fix, `pwp->p` equals table
   entry 0 except the 4 fields.
6. Keep-at-zero: stays Super.
7. Whoosh events 1035 / 1036 at `spd.x` 7 / 13.

### 6.5 Owner test

G-SA1 with kind 8 in gm_construct and the owner's freespace map:

* transformation look and aura;
* speed;
* homing on NPCs;
* water;
* drain HUD.

## 7. Work plan

| # | Work | Files | Tests / exit |
|---|---|---|---|
| S0 | SADC housekeeping: names from SADX (Sonic_SuperAura_*, SuperSonic_SubtractRings) in `config/symbols.b_chaos7.txt`; optionally reclassify 0C900000-0C900080 and 0C91C9C0-0C91CA78 as code in `config/overlays/b_chaos7.yaml`; one name per address in 1ST_READ | config, `src/1st_read/code_8C0AFF1A.c`, `code_8C0B07E4.c` | full build 497/497 byte-identical |
| S1 | Decompile the 22 functions (section 5) | `src/overlays/b_chaos7/*.c`, `src/1st_read/code_8C046960.c` (or the file that owns 8C046A9C) | `tools/shcdiff.py` MATCH or NON_MATCHING with 6.2 / 6.3; `tools/fmac_audit.py`; full build identical |
| S2 | Harness: image guard for overlay routing; `file_crcs` for level 19; `--no-level-tasks`, `--create-task`; new references `super_pc_*`, `super_free_*` | `harness/src/t_sim.c`, `harness/tools/gen_native.py` / `gen_ga.py`, `harness/tests/sim/*.txt` | `regress.py --route` all identical (existing 15 + new) |
| S3 | pclower image B_CHAOS7 into LIB; drop the fatal stubs; base map + 8C046A9C | `tools/pclower.py --all` output, `LIB/gen/policy/stubs.tsv` | LIB `build.py --test` (10 references still strict) |
| S4 | LIB kind 8: boot (chr 0, level 19, trigger task), archives (SUPERSONIC + SONIC + SON_EFF + EFF_REGULAR), DISC level 19, ring options, drain driver task, revert fix, desc fields, docs | `LIB/src/world/sa1_api_world.c`, `sa1_world_api.c`, `sa1_world_boot.c`, `sa1_world_image.c`, `sa1_coladapt.c`, `sa1_loader.c`, `sa1_manifest.c`, `include/libsa1.h` | `tests/super_test.c` (6.4); `tests/build_replay.py --run` incl. `super_free_*` strict, x86 + x64 |
| S5 | G-SA1: kind 8 playable, Q menu page (4.7), audio stage 19, water as floor for kind 8, ring HUD (interaction layer), transform v1 (re-create) | `g-sa1/src/core/session.cpp`, `audio.cpp`, `bsp.cpp` / `phy.cpp` (surface kinds), `addon/lua/gsa1/sh_config.lua`, `cl_menu.lua`, `cl_character.lua` | g-sa1 core / binding tests (create kind 8, options round trip); owner test |
| S6 (optional) | Transform v2 in-world (`SA1_CREATE_SUPER_CAPABLE`, `sa1_character_order`) | LIB world + API | super_test v2 cases |

Order: S1 and S2 can start in parallel. The S2 image guard must land before any B_CHAOS7 C is routed. S3 and S4
follow S1; S5 follows S4. Round 4b, which is running now, owns LIB and g-sa1. Start S3-S5 only after it lands.

## 8. Open questions for the owner

1. Ring drain on by default, as in the game? Start with 50 rings?
2. When the rings run out: turn back into Sonic (the game) or stay Super?
3. Transform from normal Sonic, which SA1 does not have: wanted? If so, which trigger (rings needed, key or double
   jump) and should it keep momentum (v2) or is a re-spawn in place fine (v1)?
4. Super Sonic stands on GMod water, as on the Perfect Chaos sea: on by default?
5. Music while Super: which track (Perfect Chaos phase music or a jukebox pick), on or off by default?
6. Should hits from NPCs slow Super Sonic down? In the game they halve his speed and nothing else.

## 9. Risks

* **The revert physics bug.** Faithful reverting breaks Sonic's running. The default is the host fix; the option
  keeps the bug.
* **Stage 0x1300 side effects** on normal-mode code in a kind-8 world (4.1). Keep the stage in kind-8 worlds only,
  unless S6 is wanted.
* **Overlay address collisions** in the harness routing (6.2) would silently misroute STG05 code in knux_rm3.
* **The audio bank:** without CHAOS7B.MLT, 1035 / 1036 play foreign programs.
* **0C91D486** (1,710 B, FP and rand) may stay NON_MATCHING. It must then be functionally verified by 6.2 and 6.3,
  since the drain and splash paths are only reached in the full stage traces.
* **Ghidra pseudo-C** for B_CHAOS7 (`ghidra_out/`) gets the FPU mode wrong (it splits on the SZ / PR flags).
  Use the asm and the 1ST_READ originals instead.

## 10. Evidence index (`SADC/build/research/super_sonic/`)

* **Scripts:**
  - `scan_equip.py`, `equip8000_scan.txt`, `const8000.txt`: the flag readers (1.7).
  - `annot.py` and `0C91D486.annot.txt`, `0C91DB60.annot.txt`, `0C917300.annot.txt`: asm with resolved pools.
  - `simil.py`, `pairs.txt`: clone similarity (1.9).
  - `bisect.py`: route bisect (6.1).
* **Ghidra:** `run_ghidra.sh`, `ghidra_proj/`, `ghidra_out/`. This is B_CHAOS7 imported with the SADC setup
  scripts into a private project. It is reliable for integer and control flow only.
* **Traces:**
  - `ss_l19_{idle,moves,long,play,drain}.bin/.csv`, scripts `ss_moves.txt`, `ss_play.txt`;
  - routed runs `*_route.*` (pc) and `*_ga.*` (ga); `ga_route_checks.log`.
* **Coverage:** `cov_*.cov`, `cov_sonic_union.txt`, `cov_super_only.txt` (EC1 references against Perfect Chaos).

Commands (from `SADC`):

```
harness/build/sa1h.exe trace --level 19 --act 0 --chr 0 --input @build/research/super_sonic/ss_play.txt --out build/research/super_sonic/ss_l19_play --coverage build/research/super_sonic/ss_l19_play.cov
harness/build/sa1h_ga_rsp.exe trace --level 19 --act 0 --chr 0 --input @build/research/super_sonic/ss_play.txt --out build/research/super_sonic/ss_l19_play_ga --check build/research/super_sonic/ss_l19_play.bin --route
py -3.12 harness/tools/sa1trace.py dump --from 4 --to 8 --fields mode,smode,equipment,item,reqaction,action build/research/super_sonic/ss_l19_idle.bin
```

## Review (adversarial)

Reviewer, 2026-10-21. This review is read-only. Nothing outside `docs/research/` and
`build/research/super_sonic_review/` was written. The evidence in that folder:

* `plane_check.py` and `.log`: the landtable, the plane object, the start table, the effect table and the music
  table, read from the local images;
* `L19A0.tri/.col/.obj` and `l19_dump.*`: from `harness/build/sa1h.exe trace --level 19 --act 0 --chr 0 --steps 2
  --dump-land`;
* `under_start.py` and `.log`;
* `knux_rm3*`, `obj_rm3*`, `list_rm3.txt`: `--log-sdk` call logs.

### R.1 Verified

| Claim | Evidence |
|---|---|
| Trigger 0C914D60: `timer++`; when > 1, `SetInputP(0, 46)` and a tail call of DestroyTask. It fires on its 2nd exec | asm 0C914D60-0C914D8C |
| The trigger is created by `CreateChildTask(6, 0C914D60, tp)` | 0C914DF4-0C914DFC; pool L0C914EC4 = 8C098CA0 |
| Case 46 sets the bit, creates the aura and physics tasks, then mode 71 / action 130 | `code_8C0AFF1A.c:406-418` |
| The super modes use `func_8C0B0F24`, which has no case 46 | `code_8C0B0E94.c:39-136` |
| Request 47 clears the bit and goes to `func_8C0A9660` (mode 1) | `code_8C0A9660.c:16-21` |
| Request 50 clears the bit and goes to mode 82 | `code_8C0B0E94.c` |
| The flag is `playerwk.equipment` +0x004, Sint16 | `include/sa1/player.h:137` |
| The physics table at 0x8C1BDFE4 has 8 x 0x84 entries; the 9th slot is garbage | dumped from `build/1ST_READ.BIN` |
| Sonic's air_resist -0.008, air_resist_air -0.028, air_accel 0.031, center_height 5.4 | same dump |
| Super pool words 0xBA83126E / 0xBB03126E / 0x3D4CCCCC / 0x40CCCCCC | 8C0B3370-8C0B337C |
| The destructor writes +0xAC, +0xD8, +0xDC and +0x11C (the bug) | asm 8C0B3252-8C0B3278 |
| SonicWeldList has 34 entries, 12 with objptr 0x0C909D2C; the disc stores inpmode 2 for all 12. So SonicMain mode 0 switches them *off* on every stage that is not 0x13xx | `code_8C0A77BC.c:197-207` |
| Actions 130-141: every NJS_ACTION is in B_CHAOS7 (0x0C914D0C-0x0C914D44, 0x0C90EEB0, 0x0C90FFD4, 0x0C911178) | dump of 0x8C1C55A0 |
| The texlist is chosen by reqaction / action 130-141 | `code_8C0AE0E0.c:110-114` |
| SUPERSONIC texlist 0x0C901EA0 = {0x0C901B1C, 73} | `plane_check.py` |
| Every aura / effect texlist at 0x0CA6B210-0x0CA6B410 is a 1-2 entry slice of the SUPERSONIC names. The dash names run to ss_dash23, not 20 | `plane_check.py` |
| 21 functions, 5,250 B (aura 2,410; effects 106 + 2,620; trigger 46; drain 68) | `build/B_CHAOS7.xrefs.json` |
| 191 functions in B_CHAOS7; 8C046A9C is 130 B and has no C | xrefs; `build/B_CHAOS7.symbols.txt` |
| Overlay sizes: B_CHAOS7.BIN 1,487,956 B, ending at 0x0CA6B454 | file size |
| The highest aligned BSS pool constant is 0x0CA6C310 | scan of the BIN |
| S_SBMOT is 0x0CB08000 + 261,688 B; nothing overlaps | file sizes |
| LIB's Sonic image includes B_CHAOS7 (optional); the Super Sonic image requires it | `sa1_world_image.c:27-28` |
| LIB rejects kind > 7 | `sa1_api_world.c:173` |
| `k_chr` has no chr 8 | `sa1_world_boot.c:107-114` |
| The B_CHAOS7 stubs | `stubs.tsv:43-47` |
| DISC mode allows only level <= 10 | `sa1_coladapt.c:2152` |
| The kind-8 manifest entry | `sa1_manifest.c:126-130` |
| Stage 19 maps to CHAOS7B.MLT | `sa1_audio.c:33` |
| G-SA1 excludes kind 8 | `session.cpp:19-20`; `sh_config.lua:9-17` (version 0.5.0) |
| `GSA1.Recreate` exists | `cl_character.lua:209` |
| `file_crcs` only knows `STG%02d` | `t_sim.c:1815-1828` |
| 15 harness references | `harness/tests/sim/*.txt` |
| GA routed checks: identical on 909 steps and on 5,000 steps | `ga_route_checks.log` |
| The GA registry routes 8C0B0F24, 8C0B32B6, 8C0AE6D8 and 8C08A2BC | `gen_tag_ga_rsp/native_registry.c` |
| Drain 0C917088 matches the C in 1.6 | asm 0C917088-0C9170CA |
| The drain's call site is gated on the float at +0x70 being > 0 | 0C917640-0C917652 |
| The boss sets 50 rings | 0C91741E-0C91742A |
| The drain takes 50 to 49 at step 558, then one ring every 60 steps | `ss_l19_drain.csv` |
| Tones 1035 / 1036 map to bank 5, threshold 1026 | table at 0x8C1C19B4 |
| Aura wrappers: exec, disp, dest, the first display, and the state byte at 0x0CA6C310 | asm 0C900480-0C9004B4 |
| Whoosh thresholds 7 / 13 / 6 and `8C0940DA(tone, 0, 0, 127)` | asm 0C91DAA0-0C91DAD6 |
| The splash test is `pwp->lclop (+0x74)->model == D_0C966D08` | asm 0C91D58C, 0C91DA10 |
| Damage while Super: `spd.x *= 0.5` and `wtimer = 120`, then return 0, so there is no ring scatter | `code_8C0AFB10.c`; caller `code_8C0A966C.c:141` |
| The idle voice is 396 on level 19 | `code_8C07DCE0.c:45` |
| Peak run speed 10.1498 at step 619 | `ss_l19_play.csv` |
| The mode sequence of 1.5 | `ss_l19_play.csv` |
| Function-symbol collisions: 0C900480 (B_EGM3), 0C9004D4 (STG05), 0C914D60 (ADV02, MINICART) | `build/*.symbols.txt` |
| Only welds, debug mode 83 and the voice compare the stage with 0x13 | see the next paragraph |

For the last row, these are all the GetStageNumber callers in the player code: 8C06CD22, 8C0730D8, 8C0777E0,
8C07BAC0, 8C07D2DA, 8C07DCE0, 8C0A77BC, 8C0A966C and 8C0AEFA0. Apart from 8C07BAC0, which reads the death-zone
table and was not checked, the rest compare other stages.

### R.2 Refuted (with the correct facts)

1. **"Water running is not code. The sea is a solid plane of landtable 0x0C901214 (attr 0x28000000)" is wrong.**
   - The landtable has 305 COL entries; 108 of them are collidable. Its attrs are 0x20200001 x104, 0xA0000001 x3,
     0x20000001 x1, 0x80000020 x3 and 0x80000000 x194. None has 0x28000000, and none uses the plane object.
   - The only reference to the plane object `B_CHAOS7_Collision_Plane` (0x0C966D30) is the pool word 0x0C915304,
     in the B_CHAOS7 task **0C91514A**. The object's evalflags are 0x17 and its model is D_0C966D08: an 80 x 80
     quad at y = 0.
   - The level task's act-0 mode 0C914E36 creates that task: `CreateElementalTask(6, 0, 0C91514A)` at
     0C914E54-0C914E62.

   What the task does:
   - **Mode 0:** if `GetStageNumber() != 0x1300`, it frees itself (mode 3, 8C091BA4). Otherwise it does the
     following:
     - takes a land object from 8C052A1E;
     - copies the plane's evalflags & ~3 and its model;
     - sets scale 1 and angle 0;
     - registers the object with `func_8C0528AE(0x28000000, tp, obj)` (SADX RegisterCollisionEntry,
       `code_8C052518.c:251`);
     - sets dest = 0C915120 (withdraw and free).
   - **Mode 1, first 600 steps** (`timer < 0x258`, 0C91525E-0C91526E): the exec returns before the follow code.
     The plane stays where mode 0 put it: at the player's x and z when the task was created, y = 0.
     `ss_l19_play` is consistent with this:
     - Sonic stands at the spawn on attr 0x28000000 until step 480;
     - the stage moves him to y = 443.8 at step 488;
     - he lands on attr 0x28000000 at step 614, at (-71.1, -0.009, -1277.4), 610 units from the spawn.
   - **Mode 1, from step 600:** 0C9150BA (102 B) runs every step and moves the plane under `playertwp[0]`: x and z
     from the player, y from `twp->scl.y`. It sets taskwk.flag 0x100 from `8C07CD16(&pos, 80.0)`.
   - **Also from step 600:** `wtimer` counts up to 61, and from then on the bit is tested every step. While the
     bit is set, `wtimer` goes back to 60. When it is clear, the task switches to mode 2 within 2 steps.
   - **Mode 2:** the plane sinks 0.1 per step. Below -10, about 100 steps later, it calls `func_8C07BBC2(0)`,
     which starts the death (`smode = 50`, `code_8C07BAC0.c:153-166`).

   The landing at step 68-69 of `ss_l19_idle` (attr 0x28000000, y = -0.0115) is this dynamic plane. The aura's
   splash test is the same plane: `lclop->model == D_0C966D08`. It is the same pattern as Emerald Coast's ocean
   0C909F20 (DynamicCOL 0x88000002), which LIB already creates (`sa1_world_stage.c:9-13, 80-131`).

   Consequences:
   - **The free-roam references of 6.3 cannot work as specified.** No collidable landtable triangle lies under
     Sonic's level-19 start (-16, 150, -667) at any height (`under_start.log`: 0 hits). The landtable's collidable
     rule is `flags & 0x400003`. With `--no-level-tasks` there is no plane, so the recorded Super Sonic falls
     without end.
   - Even with the plane, it covers only an 80 x 80 quad around the spawn for the first 600 steps. In the stage,
     the boss intro holds Sonic there (stick locked until step 498). A free-roam script must stand still for
     about 600 steps first, or the stand-in must start the plane's timer at 600. That would be a host deviation.
   - The game's real behaviour at 0 rings is a revert followed by death. The plane sinks and calls 8C07BBC2
     about 100 steps after the revert. So in SA1 the revert-physics bug is live for at most about 100 steps.
   - The level task also clamps `playertwp[0]->pos.y` to >= 0. This happens in 0C914F36 / 0C914F78
     (0C914F40-0C914F54, 0C914F86-0C914F9A) and in the level task's own code at 0x0C900000.
2. **"0C9004D4 is an STG05 entry that knux_rm3 runs" is wrong.**
   - Hooked alone with `--log-sdk`, it logs **0 calls** in `knux_rm3` (319 steps) and in `obj_rm3_knux` (689 steps).
   - In the same runs, the controls STG05 0C9057B8 and 0C9131AA logged 319 / 318 and 689 / 688 calls.

   knux_rm3 therefore cannot "confirm the guard" (S2 exit criterion). The address collision itself is real.
3. **"Drop `stubs.tsv:43-47`" (5, S3) is wrong.**
   - A base-image stub at an overlay-slot address stays a base stub after the overlay is decompiled. pclower
     keeps it (`tools/pclower.py:73-75`).
   - The generated stub first asks the running world's overlay image, through `sa1g_fn_image_lookup`, and only
     then applies the policy (`gen/gen_stubs.py:16-21, 228-240`).
   - The lines stay needed: `fatal` still applies when a world does not hold B_CHAOS7, and `gen_stubs.py --strict`
     flags a stub without a line. Keep them and update the evidence column.
4. **"The drain driver's exec is a synthetic adapter 0x7F00xxxx, like the camera adapter" is wrong.**
   - The camera adapter is `CreateElementalTask(2, 0, NULL)`, the game's empty exec 8C098440
     (`sa1_world_boot.c:16-18, 447-455`).
   - No 0x7F00 address exists in LIB.
   - The existing mechanism is `sa1g_world_xhook`: an executor override keyed by a real DC exec address, used by
     the stage stand-ins (`sa1_world_stage.c:133-200`).
5. **"Kind 8 loads only the SUPERSONIC archive" is half wrong.**
   - `load_common_textures` (EFF_REGULAR) runs for every kind (`sa1_loader.c:469-470`).
   - Only Sonic's PVM list is missing: SONIC 0x8C1C6C94 and SON_EFF 0x8C1C6AB4, from 0x8C1C2584[0].
6. **The 4.1 side effect "debug mode 83 returns to super" for a reverted Sonic is wrong.**
   `code_8C0A966C.c:1870` also requires `equipment & 0x8000`, so a reverted Sonic leaves mode 83 to mode 2 as on
   any stage.
7. **Music: "No Super Sonic music exists in 1ST_READ" is misleading.**
   - The 1ST_READ track table 0x8C5FB8A8 holds **SPRSONIC.ADX at index 86**, "Theme of SUPER SONIC"
     (`jukebox.md:249`). No code was found that plays it on the transformation.
   - The UNVERIFIED "which plays when" is partly resolved:
     - stage init 0C914DC6 creates the music task 0C914D8E with mode 22, so CHAOS_P1 starts after 60 steps
       (0C914E12-0C914E20; 0C914D8E calls `8C107DAA(twp->mode)`);
     - 0C914FEE, called only by the boss 0C917300 (xrefs), creates it with mode 23, so CHAOS_P2 plays. **UNVERIFIED:**
       which boss phase that is.
8. **Minor corrections.**
   - S_MOT ends at 0x0CC774A0 (488,608 B), not at 0x0CC77490.
   - `func_8C0AFF1A` is also called from mode 22 (`code_8C0A966C.c:902-907`).
   - The 8C0B07E4 case 46 first calls `func_8C0B1D60` (drop the held object), so it is not "the same case".
   - The drain pause (1.6): rings are 4 from step 3738, and the next decrement is at 4771, not "about 4853".

### R.3 Gaps

1. **Level-19 effect list.**
   - D_8C1C4140[19] = 0x8C1C4138 = {0x8C19C06C}. That definition is also the common list's 10th entry
     (D_8C1C40E8). So on level 19 the game creates the D_8C19C078 list twice.
   - 8C046A9C spawns the splash into the **second** list.
   - LIB creates the level's definitions only when the image holds `STG%02d.PRS` (`sa1_world_boot.c:250-277`). For
     level 19 it never does, so its heap and particle-list order differ from every level-19 reference.
2. **DISC level 19 needs more than a stage mapping.** Four places reject or break it:
   - `SA1CA_STAGE_LEVELS` = 12 (`sa1_coladapt.h:84`), checked in `sa1_collision_set_static_ex`
     (`sa1_coladapt.c:262`);
   - `sa1ca_disc_landtable` (`:2209`);
   - `sa1ca_disc_stage` (`:2152`);
   - `disc_image`, which composes kind 8 with B_CHAOS7 required and then calls `sa1g_image_add_stage`, a second
     occupant of 0x0C900000 (`sa1_api_world.c:90-122`).

   FAITHFUL replays also need `build/traces/land/L19A0` (a sample is in the review folder).
3. **Capture and posing after an in-world revert.**
   - For kind 8, `texlist0` is archive 0 = SUPERSONIC (`sa1_character.c:949`).
   - A reverted body is drawn with `texlist_SONIC`. It is therefore "foreign", flagged `SA1_DRAW_EFFECT`
     (`sa1_capture.c:904-933`), and dropped from v1 `sa1_mesh` output (`sa1_character.c:677-687`).
   - The hidden-camera path and puppets pose kind 8 with `k_super_subst` (`sa1_manifest.c:85-88`). Normal-Sonic
     actions then show the Super model.
   - The transform-v2 world (kind 0 with SUPERSONIC) has the mirror problem.
4. **"Water as SOLID for kind-8 worlds" does not fit the API.**
   - Static collision belongs to the context (`sa1_collision_set_static`, G-SA1 `session.cpp:290`), not to a
     world.
   - Exporting water brushes as SOLID (`bsp.cpp:1385-1393`) also turns their side and bottom faces into walls.
     WATER keeps only the upward faces (`libsa1.h:808-812`).
5. **The revert fix by table copy can clobber other writers of `max_x_spd`.** The speed-up tasks 8C07D2A2 /
   8C0B2EA0 save and restore it (`player.h:46`). A fix based on the physics task's own saves (awp +4 / +8) plus a
   pre-dest snapshot is closer to the intent.
6. **Start rings duplicate a planned API.**
   - The interaction design already has `sa1_character_add_rings` (8C08A198 in the world,
     `libsa1_interaction.md:391, 529`) and `sa1_state_ex.rings`. `super_rings` duplicates them.
   - 8C08A198 with a start of 100 or more calls 8C08A120 (a 1-up and its jingle).
   - With the drain on and 0 rings at boot, the first drain call reverts at once. Rings must be set before the
     first tick.
7. **Two decompiled paths have no reference in the plan:** the drain, and the aura's splash (0C91D200 -> 8C046A9C,
   with rand). The planned references contain neither, so only S2 routing of `ss_l19_drain` / `ss_l19_play` checks
   them functionally.
8. **Smaller points.**
   - `sa1_audio_set_stage` is per audio object, so it switches bank 5 for everything that client plays.
   - Remote puppets (multiplayer not done yet, `sh_net.lua:5-9`) would show kind 8 without its aura.

### R.4 Required changes to the work plan

| # | Change |
|---|---|
| S1 | Add the water-plane task 0C91514A (420 B), 0C9150BA (102 B) and 0C915120 (42 B): **+564 B**, 25 functions in all. 0C91514A is also a MINICART function entry, so add it to the guard list. |
| S2 | Free-roam references must also create the plane. Use `--create-task 0C91514A` (im 6, list 0) next to 0C914D60 (im 6, as the original child task). The alternative is a harness level-task stand-in with the same two tasks. |
| S2 | Drop "knux_rm3 confirms the guard". Test the guard with a run that executes a colliding address. |
| S2 | Record new land dumps (L19A0). |
| S3 | Keep `stubs.tsv:43-47` (R.2.3). |
| S4 | Boot kind 8 with a **B_CHAOS7 level-task stand-in** in `k_standins` (0x0C900000, image B_CHAOS7), following the Emerald Coast precedent. In its first exec it creates `CreateChildTask(6, 0C914D60, tp)` and the plane `CreateElementalTask(6, 0, 0C91514A)`; its later modes call the drain when the option is on. No ad-hoc boot path, no synthetic exec. |
| S4 | Fix the four DISC-level-19 sites (R.3.2), not only `sa1ca_disc_stage`. |
| S4 | Create D_8C1C4140[level] whenever its definitions lie in 1ST_READ (R.3.1). |
| S4 | Add only SONIC and SON_EFF to kind 8 (EFF_REGULAR is already there). |
| S4 | Replay `super_free_*` with `SA1_SUPER_NO_DRAIN`. |
| S4 / S5 | **Revert:** G-SA1 re-creates the character as kind 0 when the bit clears, for the drain revert and for death. This avoids R.3.3 and the physics bug in the host. Keep the LIB fix for API users, built as in R.3.5. |
| S4 / S5 | Rename the 0-ring options: the game's own behaviour is revert then drown (R.2.1). "Back to Sonic" is a host adaptation. |
| S5 | "Run on water" needs a per-world surface remap in LIB's collision adapter (upward faces only), or a LIB-placed plane built from the game task 0C91514A at the host water height (a host deviation). G-SA1 cannot do it by re-exporting the context-wide geometry. Add `sa1_coladapt*.c` to S4's files. |
| S5 | The default Super music track is SPRSONIC.ADX (86). CHAOS_P1 (22) and CHAOS_P2 (23) are the stage tracks. All are Q-menu choices. |
| S2 / 6.3 | Free-roam scripts start with about 600 steps of standing, while the plane stays at the spawn (R.2.1). |
| 6.4 | Add tests: the stand-in creates the plane; the plane follows only from step 600; with the bit clear and the plane enabled, the death request about 100 steps after mode 2; the D_8C19C078 list count on level 19; capture after a revert. |

