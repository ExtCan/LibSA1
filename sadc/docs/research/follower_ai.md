# Follower characters: the game's "1.5P" Tails AI, libsa1 worlds and G-SA1 (round 6b research)

Status: research / design only (2026-10-21). Nothing in LIB, g-sa1, SADC/src, include, tools, harness or config was
changed. Scratch scripts, interpreter traces and their summaries: `SADC/build/research/follower_ai/` (`xq.py` xref
queries, `status.py` / `stat_list.py` decompilation status, `rd.py` image reader, `ramq.py` / `ramdiff.py` RAM-image
readers, `ram_summaries.txt`, `wv_*.bin/.csv` traces, `wv_cov.txt` coverage). Abbreviations: SADC =
`<project>\sadc`, LIB = `...\libsa1`, G-SA1 = `...\g-sa1`. "Measured" = run in this session.

Owner request: "Spawnable follower Tails + the other characters can follow. (There is a following NPC mode in the game
for Sonic that will have Tails follow him around in any stage)".

---------------------------------------------------------------------------------------------------------------------

## 0. Summary

* The game's follower is **"1.5P"** (its own name: the first entry of the leftover debug list "NPC [%s]" at
  `D_8C1B9658[0]` -> `"1.5P"`, 1ST_READ 0x8C1549FC). It is a level-0 task (`func_8C081D4E`) that **synthesises pad
  input for player 1** - `B_8C754C08[1].on/.press` and the analog record `B_8C754DCC[1] = {angle, stroke}` - from the
  current positions and speeds of player 0 and player 1. It does **not** read the 256-frame position history (it only
  rewrites player 1's entry after teleporting him) and it does not use stage paths (the race AI 8C07F560 does).
  Respawn: when Tails is more than 1000 units from the leader, or more than 50 units away and off-screen for more than
  300 frames, he is put 50 units above the **camera position** and starts flying (forced action 48). It never
  despawns; slot-1 players never die (no death zones, no ring scatter, no drowning death).
* All 22 functions of the AI (8C081AC0-8C082D60) **have C in SADC** (18 matched, 4 NON_MATCHING), and the files are
  already lowered into libsa1 (`SADC/build/pclower/ga64/lowered/code_8C081AC0.c`, `code_8C081D00.c`). **Measured:**
  in the reference simulator, Sonic in Windy Valley act 1 gets the AI from the game's own loader; with the four AI
  files routed to the native NON_MATCHING C, the whole 16 MB guest RAM is **byte-identical** to the pure interpreter
  at steps 300, 436 and the end (660 steps; placement, follow, fall, distance respawn and flight exercised).
* It needs both characters in one world **as written** (it reads `playertwp[0]`, drives slot 1), but from player 0 it
  only reads position, facing, two speeds and the existence of the task/work structures. In libsa1 the leader can
  therefore be a **proxy** (a mirror of any leader) in slot 0 of a dedicated follower world.
* Recommendation (section 4): **one follower world per follower: a proxy leader in slot 0, the follower in slot 1,
  the game's own 1.5P AI driving it**, the world configured as the game's own 1.5P situation (Sonic's game:
  `CurrentCharacter = 0`, stage 0x0100, `B_8C78B45A = 1`), so the game's predicate `func_8C082820` returns 1 without
  any override. Works for any leader (any kind, a remote player, a GMod NPC/player entity, another follower), any
  number of followers (one world each), Tails first, then the other kinds behind per-kind tests. The literal DC
  configuration (Tails in slot 1 of the leader's own world, "A") is kept as a **libsa1 test configuration** for
  bit-exact verification against new interpreter references, not as the production path.

---------------------------------------------------------------------------------------------------------------------

## 1. How the game's follower works

### 1.1 Code, data and decompilation status

| Range / symbol | What | Status (read-only scan, `status.py`) |
|---|---|---|
| `8C081AC0-8C082D60` (code map: "Tails follower AI (2P Tails)") | 22 functions: creation `func_8C081AC0`, supervisor `func_8C081D4E`, steering `func_8C082160`, respawn test `func_8C082570`, placement `func_8C0826B0`, predicate `func_8C082820` + flag `func_8C0829E6`, helpers | **all 22 have C**: 18 matched (2,642 B), 4 NON_MATCHING (1,882 B: `8C082160`, `8C082570`, `8C0826B0`, `8C082820`; reasons in the files: register choice, FSCA, jump-table entry shcdiff cannot evaluate) |
| `SADC/src/1st_read/code_8C081AC0.c` | `func_8C081AC0` create, `8C081B66` dest, `8C081B6E` free, `8C081B8A` reset to mode 0, `8C081C00` park+port on, `8C081C36` respawn request, `8C081C66` remove slot-1 Tails, `8C081CAC` recreate Tails | matched |
| `SADC/src/1st_read/code_8C081D00.c` | `func_8C081D00` stop player (speeds 0, SetInputP 48), `func_8C081D4E` supervisor (exec of the AI task) | matched |
| `SADC/src/1st_read/code_8C082160.c` | `func_8C082160` steering, `func_8C082570` respawn test, `func_8C0826B0` placement, `func_8C0826F4` | 3 NON_MATCHING, 1 matched |
| `SADC/src/1st_read/code_8C08273C.c` | `func_8C08273C` brake direction, `func_8C0827C4` (race/boss helper), `func_8C082820` predicate (NON_MATCHING), `func_8C0829E6` flag | |
| `SADC/src/1st_read/code_8C082A60.c` | `func_8C082A60(a)`: `B_8C78C24C = a` (the hold flag); 8C082A66-8C082C5A: stage title card (not AI) | matched |
| `8C0584A0-8C0586A0` ("NPC/AI hooks", 17 functions, 392 B) | wrappers: `8C0585AE`->`8C081C66`, `8C0585B4`->`8C081CAC` (event system 8C049692 / 8C049744), `8C0585BA` / `8C0585C2` set / clear the hold flag, `8C058646` reset; debug menu `8C0584A0` / `8C0584F8` | **ASM only** (no C) |
| `8C07F560-8C081AC0` race AI ("MILESRACE", "MRACE_EGGMOBLE") | Sonic as Tails' race opponent; path-based (8C06638E family), reads the position history (8C080EBA -> GetPlayerPosition 8C07CE82) | 636 B matched, 9,342 B ASM; not a follower, not usable on GMod maps (needs the stage's paths) |
| Globals | `B_8C78C248` AI task, `B_8C78C24C` hold flag, `B_8C78B45A` 1.5P flag (part of the 18-byte block `B_8C78B450` cleared by `8C058646` on game-mode change), per[1] `B_8C754C3C`, analog[1] `B_8C754DD4`, port-B record pointer `B_8C754DAC`, port enables `D_8C19E3A9[2]` | |

`tools/progress.py` was not run because it writes temporary files next to the sources in `SADC/src`; the scan
`build/research/follower_ai/status.py` applies the same rules (C definition outside `#ifdef NON_MATCHING` = matched)
read-only.

The debug selector (`func_8C0584A0`: D-pad left/right of **controller 2** (`B_8C754DAC`, press bits 0x40 / 0x80)
cycles 7 entries and prints "NPC [%s]"; `func_8C0584F8` creates entry 0 "1.5P" with `func_8C081AC0` and entries 1-6
("BOSS SONIC vs KNUCKLES", "BOSS SONIC vs E102", "BOSS KNUCKLES vs SONIC", "BOSS KNUCKLES vs MILES", "BOSS E102 vs
SONIC", "BOSS E102 vs MILES") with the boss AIs `8C083F7C` / `8C083A1E`) is **dead code**: no caller in the xrefs and
no pointer to either function in 1ST_READ or any of the 248 overlay images (word search of every `build/*.BIN`).

### 1.2 When the game creates it (and the "any stage" question)

* `LoadPlayerTask` 8C04F3C0 (Ghidra `build/ghidra/1ST_READ/decomp/8C04F3C0.c`), second loop iteration (slot 1): only
  when `CurrentCharacter` (`B_8C7493C8`) is 0 (Sonic) it calls `func_8C081AC0(player 0's task)`; then for every
  character `func_8C07F560(chr, 1, level)` (races).
* `func_8C081AC0` (`code_8C081AC0.c:23-56`): if `B_8C78C248` exists, return it; create only if
  `((GetStageNumber() >> 8) & 0xF) == 9` (Casinopolis; level 0x19 would also match - E-101 mk II in SADX numbering,
  UNVERIFIED for DC - which Sonic never plays) **or**
  `func_8C082820()`. It creates the AI task `CreateElementalTask(2, 0, func_8C081D4E)` (list 0: runs before the
  players in list 1) with `counter.b[1] = 2`, `counter.b[0] = 1`, dest `func_8C081B66`, then the Tails player task
  `CreateElementalTask(7, 1, TailsMain)` with `counter.b[0] = 1` (player number) / `b[1] = 2`, placed 10 units behind
  the leader by `func_8C0826B0` (`pos - 10 * (cos ang.y, 0, sin ang.y)`), AI mode 0, hold flag 0.
* `func_8C082820` (`code_8C08273C.c:73-180`), evaluated at creation **and every frame** by the supervisor:
  returns 0 if `B_8C78C24C` (hold) or `GetPlayerNumber() != 0` (not Sonic's game) or an event is running
  (`func_8C049A4E() == 1`). Otherwise per level (flag = `func_8C0829E6`: `B_8C78B45A == 1`, or event flag 0x48, or
  (flag2 0x100 and not flag2 0x1A), or (event flag 0xA6 and not flag2 0xD); flag2 = `B_8C799B88[]` via `8C049A5A`):

| Level | 0 HH | 1 EC | 2 WV | 3 TP | 4 SH | 5 RM | 6 SD | 7 LW | 8 IC | 9 CP | 10 FE | other (fields, bosses) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Tails follows | no | flag | **yes** | flag, acts 0-1 (act 0 returns 1 but also sets the hold via `8C0585BA`, so the next frame parks him; probably the roller coaster, UNVERIFIED) | flag, acts 0 / 2 | flag, acts 0-1 | **yes** | flag, acts 0-1 | acts 0-1 | acts 0-1 (2-3: parked) | flag | no |

  So "any stage" is not the game's rule; the owner's "any stage" is what G-SA1 will give (section 5). Stage objects
  toggle the hold flag: STG07 (Lost World) calls `func_8C082A60` from 3 places (overlay words at 0x5870, 0x60B4,
  0x76DC), STG03 (Twinkle Park) clears it through `8C0585C2` (words at 0x7E34, 0x8004). Events remove / re-create the
  slot-1 Tails (`8C081C66` / `8C081CAC` through 8C049692 / 8C049744). `8C058646` (called by the game loop 8C053B60)
  clears `B_8C78B450[0..17]` and `B_8C78C248`. `B_8C78B45A` is written only by the ADVERTISE (menu) overlay
  (8C9031C0, 8C90321E, 8C903AE0, 8C909778); its menu meaning (probably Trial mode) is **UNVERIFIED**.
* **Measured** (`sa1h trace`, original code in the interpreter, `ram_summaries.txt`): Windy Valley act 1 (`--level 2
  --act 0 --chr 0`): AI task at 0x8C7EEA40, Tails in slot 1; Emerald Coast act 1 (`--level 1`, flag clear): no AI
  task, slot 1 empty; Casinopolis act 3 (`--level 9 --act 2`): AI task created by the level-9 gate but in mode 6,
  Tails at y + 1,000,000 (1000030.875 vs Sonic 30.167), Tails mode 18 / smode 12.

### 1.3 The steering: pad input for player 1 (`func_8C082160`, `code_8C082160.c:59-221`)

Each frame, with p1 = leader (`playertwp[0]`), p2 = follower (`playertwp[1]`), cs = their `playerwk` (through
`playermwp[n]->work`):

* Target point: 5 units from the leader towards the follower (`v = unit(p2 - p1)` in XZ, `* 5`); `dist` = horizontal
  distance from the follower to it, `ang = atan2` towards it (`8C085A50`). The angle is written straight into
  `B_8C754DCC[1].ang` (a world angle, as the analog conversion 8C05117E would produce), so the camera does not matter.
* smode 0: wait until both `playerwk` exist -> 1.
* smode 1 (on foot): `dist < 15`: if the leader is slower than its jog speed (`spd.x < p.jog_speed`, playerwk +0x38 /
  +0xC0) and the follower faster than its run speed (+0xC4): brake (stick opposite to the follower's world velocity,
  `8C08273C`); else stroke 0.005 when standing (turn in place) or 0. `15 <= dist < 40`: stroke
  `(dist - 20) * 0.025 + 0.5`; `>= 40`: 1. If on the ground and the leader is more than 20 above and slower than its
  run speed: **press jump** -> smode 2.
* smode 2 (jumping): stroke `dist * 0.05` (0 below 0.1, max 1); when the leader is above (`p1.y + 3 > p2.y`): press
  jump again while falling (`mwp.spd.y < 0`) -> smode 3 (Tails: flight), else hold jump; landed -> 1.
* smode 3 (flying): same stroke; landed -> 1; within 20 and above the leader: press the action button
  (`D_8C19E38C` = 0x402, B|X), else hold jump (keep flying).
* smode 4 (fall until 20 above, then fly) and smode 5 (move straight to the leader by a unit vector per frame until
  within 50 or `flag & 4`) have **no writer**: nothing in 1ST_READ sets smode 4/5 of the AI task (its only pointer,
  `B_8C78C248`, is read by 8C081AC0 / B6E / C00 / C66 / CAC / D4E / 8C058646). Dead states.
* Stuck rule: if the follower moved less than 1 unit since the last frame for more than 60 frames while `dist > 30`:
  press jump, smode 2.
* Writes `per[1].on`, `per[1].press` (`B_8C754C3C`, = `B_8C754C08[1]`) and `B_8C754DD4` (= `B_8C754DCC[1]`); copies
  the follower's pos/ang into the AI task. Constants checked against the literal pool (`asm/1st_read/text/8C081AC0.src`:
  5.0 0x40A00000, 15 0x41700000, 20 0x41A00000, 40 0x42200000, 0.005 0x3BA3D70A, 0.05 0x3D4CCCCC, 0.1 0x3DCCCCCC).
  Button masks from the image: `D_8C19E390` = 0x4 (A, jump), `D_8C19E38C` = 0x402.

Measured in Windy Valley (`ram_summaries.txt`): step 2 Tails exactly 10 units behind Sonic (1220 vs 1230, Sonic facing
+X); idle: analog[1] stroke 0.005; step 180 (Sonic running): Tails 76.7 behind, stroke 1.0; port enables (1, 0) after
mode 0 (port 1 reading off).

### 1.4 The supervisor (`func_8C081D4E`, `code_8C081D00.c:91-235`)

Every frame: if `func_8C082820() == 0`: `SetInputP(1, 12)`, port 1 on, mode 6. Clears bit 0x20 of `flag4` in every CCL
info of player 1 (`code_8C081D00.c:118-121`; meaning **UNVERIFIED**, probably keeps Tails from pushing / being pushed by
player 0). Modes:

| Mode | Reached | Does |
|---|---|---|
| 0 | creation, `8C081B8A` | port 1 reading off (`8C0509DA(1)`), Tails 10 behind the leader (`8C0826B0`), history entry rewritten (`8C07CEF2(1, 0, pos)`), -> 4 |
| 4 | normal | if the real **controller 2** (`B_8C754DA8[1]`) has buttons 0x306F6 or a stick: port 1 reading on, -> 5 (a second human takes Tails over, as in Sonic 2); else steering (1.3) and the respawn test (1.5) |
| 5 | human 2P | after 300 frames without input on controller 2: back to 4 |
| 2 | respawn (`8C081C36`: `SetInputP(1, 24)`, port off) | Tails = camera position (`8C049160`) + 50 Y, history rewritten, speeds 0 + `SetInputP(1, 48)` (`8C081D00`), A held and pressed, smode 3 (flying), -> 4 |
| 6 | predicate false | Tails parked at leader + 1,000,000 Y, `SetInputP(1, 12)` each frame; when the predicate turns 1: `8C081B8A` -> 0 |
| 1, 3, 7, 8 | no writer reachable (1 only from `8C081C00`, which has no caller in any image; 3 needs `8C082570() == 2`, which it never returns; 7 has no writer) | dead |

Forced actions (`SetInputP` 8C07C65E: `flag |= 0x1000`, `smode = n`) as Tails' NAct `func_8C0BBDD6` handles them
(`code_8C0BBDD6.c`): 12 -> mode 18 (stand, speeds cleared), 24 -> release to normal (8C0B70E8), 48 -> mode 15
**flight** (action 37, SE 0x46D), 49 -> mode 61 (action 41).

### 1.5 Respawn, despawn, life and death of a slot-1 player

* `func_8C082570` (`code_8C082160.c:224-256`): 3D distance > 1000 -> respawn; < 50 -> counter 0; else the follower is
  projected with the **current matrix** (`njCalcPoint(NULL, ...)`, the camera's view left in XMTRX) and `8C085F14`
  (`dst = (B_8C88F618 * r * x, B_8C88F61C * r * y, z)`, `r = B_8C88F5E4 / z`): if `z < -50` or outside half the screen
  (`B_8C88F5E8 / 2`, `B_8C88F5EC / 2`) the counter (AI task +8) grows, and above 300 frames (5 s) -> respawn.
* **Measured** (`wv_sonic_b`): Tails fell into a pit: step 430 distance 992.1, counter 182; by step 436 (distance
  passed 1000) he was at (1519.4, -474.5, 125.8) with the camera at (1517.6, -525.0, 128.6) - 50.5 above it after a
  few flight frames - in mode 15 / smode 48, AI smode 3, A held (`per[1].on` 0x4), stroke 1.0, counter reset. The
  coverage of the run (`wv_cov.txt`) lists 8C081C36, 8C081D00, 8C081D4E, 8C082160, 8C082570, 8C0826B0, 8C082820,
  8C0829E6, TailsMain, TailsModeMachine and 8C0BBDD6.
* No despawn: the task lives until the stage ends (or an event frees Tails through 8C081C66).
* Slot 1 never dies and loses nothing: the death-zone task 8C07BAC0 tests player 0 only (`code_8C07BAC0.c`, comment
  and `ptwp = playertwp[0]`); every mode machine calls the ring scatter `func_8C086F5E(twp->counter.b[0])`, which acts
  only for flag 0 (`code_8C086F5E.c`; callers SonicModeMachine 8C0A966C:141, Tails 8C0B70F4:170, Knuckles
  8C0C193A:177, E-102 8C0CD0A8:225, Amy 8C0D7960:166, Big 8C0E0E70:180); drowning sets `flag |= 4` instead of the
  death sequence for pno != 0 (`code_8C07BE68.c:91`). Exceptions: E-102's and Big's direct `8C07BBC2(pno)` calls
  (8C0CD0A8:746, 8C0E0E70:716, 8C0D23E4:77 - E-102's timer) would kill a slot-1 E-102 / Big (**UNVERIFIED** when
  they fire for a follower; libsa1 worlds stop the clock by default).
* Player-1 differences inside Tails' code: his own non-positional SEs are muted (`func_8C0BFCA2`:
  `if (counter.b[0] != 1) func_8C0940DA(...)`, `code_8C0BF804.c:292`), the flight camera change is player 0 only
  (`code_8C0B70F4.c:339`), idle voices are player 0 only (`code_8C07DCE0.c:33`), homing/target lists use the
  "others" list `B_8C742288` (`code_8C06CFCC.c:90-118`). Tails' code calls `GetPlayerNumber` nowhere (xref scan).

### 1.6 Dependencies

| Dependency | Used for | State in SADC / libsa1 |
|---|---|---|
| `playertp/twp/mwp/pwp[0..1]` (`B_8C78C648/608/628/668`) | leader and follower state | PInitialize fills them per pno (`code_8C06C300.c:72-75`) |
| `B_8C754C08[1]`, `B_8C754DCC[1]`, port enables `D_8C19E3A9[]`, pad update 8C050BE4 | steering output | with port 1 off the pad update **clears** per[1] each step (`code_8C050BE4.c:100-117`), then the list-0 AI writes it before list 1 runs; libsa1's tick ORs per[0]/per[1] presses across the pair (`sa1_world_tick.c` `sa1g_press_collect`) as 8C04E95A does |
| `B_8C754DA8[1]` (pdGetPeripheral, port B) | 2P takeover | rt has 4 ports (`rt/rt_input.c`); libsa1 feeds port A only, `sa1g_world_set_pad(w, port, ...)` exists (`sa1_world_tick.c`) |
| `SetInputP` + the character's NAct cases 12 / 24 / 48 | park, release, flight | Tails 12/24/48/49; Knuckles 48 = mode 11 action 51 (glide, `code_8C0C5BCA.c`); Sonic, E-102, Amy, Big have 12 and 24, **no 48** (case counts in `code_8C0AFF1A.c`, `code_8C0D079A.c`, `code_8C0DBC16.c`, `code_8C0E488C.c`) |
| position history `8C07CEF2` (`code_8C07CE40.c`) | rewritten after teleports only | matched C |
| camera position `func_8C049160` (`*out = camera_twp->pos`, 18 B) | respawn point | **no C**; libsa1 stub `fatal` (`LIB/gen/policy/stubs.tsv:101`); and libsa1 never sets the camera adapter's position (only `camera.ang` / `smode` are written, `sa1_world_tick.c:50-63`: `w->camera.pos` stays 0) |
| current matrix / screen (`njCalcPoint`, `8C085F14`, `B_8C88F5E4..F5EC`, `B_8C88F618/1C`) | off-screen test | rt `njCalcPoint(NULL)` uses XMTRX (`rt/rt_matrix.c:594`); live worlds load a view 40 units behind player 0 along the host camera yaw (`sa1_world_tick.c:79-120`); boot sets the 640 x 480 screen (`sa1_world_boot.c:155-191`) |
| `func_8C049A4E` (event running, 12 B) | predicate | no C; stub `ret 0` (`stubs.tsv:103`) |
| `func_8C049A5A` (`B_8C799B88[n]`, 6 B) | flag | no C; stub `fatal` (`stubs.tsv:104`) - reached only if `B_8C78B45A != 1` and event flag 0x48 is clear |
| `func_8C0585BA` (8 B) | Twinkle Park act 0 | no C; stub `fatal` (`stubs.tsv:119`) - reached only on level 3 act 0 |
| vector helpers 8C08593C / 8C085980 / 8C0859E4 / 8C085A50 / 8C085F14 | maths | NON_MATCHING C (hand-written asm module) |
| `GetStageNumber` / `GetPlayerNumber` | predicate | NON_MATCHING C |
| land collision | follower physics | the game gathers around players 0 **and 1** (design 6.8); libsa1's GRID window is built around player 0 only (`sa1_coladapt_world.c:1000-1040`, range `D_8C19E96C` = 400, `:849-852`) |

### 1.7 One world, or not?

As written the AI and the follower must share a world with player 0: it reads `playertwp[0]`, `playermwp[0]->work`,
`func_8C07CFA0(0)` (must be non-NULL) and writes slot 1's pad. From player 0 it reads only: `twp->pos`, `twp->ang.y`
(mode-0 placement), `playerwk.spd.x`, `p.jog_speed`, `p.run_speed`, and the existence of the task / work pointers.
The camera view (XMTRX) it uses for the off-screen test is the camera behind player 0. Nothing else of the leader.

Two characters in one libsa1 world (one 16 MB DC-layout arena) are limited by the overlays: `S_MOT`, `K_MOT`, `A_MOT`,
`B_MOT` and `E_MOT` all load at **0x0CC00000** (`LIB/src/sa1_manifest.c:16-20`, `sa1_assets.md:60`), so only Tails
(110 of his 132 actions in 1ST_READ, the rest snowboard `M_SBMOT` 0x0CADC000 and cart) can share a world with another
kind. Two Tails cannot share one cleanly either: Tails' display and effects use singletons (tail bend phase
`B_8C7A2188` / amount `B_8C7A218C` advanced by every Tails display, `code_8C0BDC80.c:74-100`; trail polygon buffers
`B_8C7A22D0..`, `B_8C7A2310..`, `code_8C0BFE00.c`, `code_8C0C023E.c`).

### 1.8 Other kinds as player 1

The game itself runs Sonic (Tails' races, boss "KNUCKLES/E102 vs SONIC"), Knuckles and E-102 (boss "SONIC vs ...")
and Tails as player 1: the boss AI `func_8C083F7C` creates SonicMain / KnucklesMain / E102Main with
`counter.b[0] = 1` (`code_8C083E5C.c:99-113`), the race AI SonicMain for slot 1 (`8C07F560`). Amy and Big are never player 1 (**UNVERIFIED** beyond the boss list and the loader). Hard-coded
player-0 references (counted with a regex over each character's files): Tails 1 (Sand Hill height), E-102 2, Sonic 5,
Knuckles 10 (mostly `B_8C754C08[0]` button reads for Maximum Heat / digging), Amy 21 (pad 0 reads, target record
`code_8C0DFE60.c`), Big 57 (almost all in fishing 8C0E66xx-8C0EAxxx, which the AI never starts). A slot-1 character
whose code reads pad 0 just does not see those buttons (the AI never presses them anyway).

---------------------------------------------------------------------------------------------------------------------

## 2. libsa1 today

* **One world per ticking character, the character in slot 0** (design D2, `libsa1_integration.md:242`, owner
  sign-off; 6.8 at line 868: "Optional later: slot 1 in the same world for the Tails AI follower"). A world = 16 MB
  arena + the kind's image (`LIB/src/world/sa1_world_image.c:26-36`: Sonic S_MOT/S_SBMOT/B_CHAOS7, Tails M_SBMOT,
  Knuckles K_MOT ...). Cost: 0.010-0.037 ms per 60 Hz step (`libsa1_integration.md` "Measured facts"),
  create/destroy 5-36 ms, GRID set-up for later characters 5-7 ms (`gsa1_ramp_freeze.md:400-420`).
* Player creation replaces LoadPlayerTask for slot 0 only; the slot-1 iteration (`8C081AC0` and `8C07F560`) is
  deliberately skipped (`LIB/src/world/sa1_world_boot.c:347-407`, comment at 395-406).
* The AI code **is in the DLL** (lowered: `SADC/build/pclower/ga64/lowered/code_8C081AC0.c`, `code_8C081D00.c`;
  6 code-map hits in `ga64/sa1g_fntab.c`), never called.
* Everything "the character" is player 0: `sa1g_world_player` (`sa1_world_boot.c:714-738`) feeds state, capture,
  collision window and moving objects; the camera view is behind player 0 (`sa1_world_tick.c:81-120`); the camera
  position is never set (above); events, render and sound are per world = per character id (`libsa1.h:1093-1107`).
* Capture records carry no task / player (`LIB/src/sa1_capture.h:92-109`): a world drawing two characters could not
  split its draws without a new record field.
* Input: `sa1_input` is stick + camera yaw + buttons for port A (`libsa1.h:475-481`); no analog override.
* Collision static and moving objects are context-wide (shared by all worlds); the GRID window and the moving-object
  gather are centred on player 0 (`sa1_coladapt_world.c:1000-1040`); G-SA1 commits movers around the **first**
  ticking character only (`g-sa1/src/core/session.cpp:299-312`).

---------------------------------------------------------------------------------------------------------------------

## 3. Options

**A - faithful same-world.** Tails in slot 1 of the leader's world, the AI created by the game's own `func_8C081AC0`.
Exactly the DC configuration when the leader is Sonic (predicate satisfied by `B_8C78B45A = 1` on stage 0x0100, no
code change). For other leaders `func_8C082820` returns 0 because `GetPlayerNumber() != 0`; making it 1 needs either
`CurrentCharacter = 0` (changes 23 statically reachable `GetPlayerNumber` callers, e.g. Knuckles' 8C0C88E6, Amy's
8C0DEDC0 / 8C0DF060, the ring scatter's death sound: not acceptable for the leader) or a per-world interposition of
`func_8C082820` (pclower has no such mechanism today; the calls are direct C calls). Tails-follows-Tails is out
(singletons), non-Tails followers are out (0x0CC00000). Needs a two-character capture / render / event / sound split
and a second collision centre. Follows only a locally ticked libsa1 character.

**B - host follower controller.** The follower is an ordinary slot-0 world; the host computes its `sa1_input` from the
leader (1.3's rules ported to host code). Any kind, any target. But it is a re-implementation of game logic that
exists (owner rule), and `sa1_input` cannot express the AI's output exactly (world angle + stroke: G-SA1 has to go
through the stick, the camera yaw and the inverse dead zone); flight / respawn have to be re-invented.

**C - proxy-leader follower world (recommended).** Each follower gets its own world: slot 0 = a **proxy leader**
(task + taskwk/motionwk/playerwk in the arena, empty exec, fields written by libsa1 from the real leader each step),
slot 1 = the follower's main, and the game's own AI task. The world is set up as the game's 1.5P situation:
`CurrentCharacter = 0` (Sonic's game: for Tails this is the game's own 1.5P setting, for Knuckles and E-102 the setting
of the boss AIs "SONIC vs KNUCKLES / E102"; Sonic, Amy and Big as player 1 of Sonic's game are not game
configurations), level 1 act 0, `B_8C78B45A = 1` -> `func_8C082820` returns 1 unchanged.

| | A same world | B host controller | C proxy leader |
|---|---|---|---|
| Steering / respawn logic | game | host re-implementation | game |
| Leader kinds | Sonic without code change; others need an interposition; not Tails | any | any |
| Targets | local libsa1 character only | anything | anything (libsa1 character, remote puppet, GMod entity, another follower) |
| Follower kinds | Tails only (0x0CC00000) | all | all (Tails first; others behind tests) |
| Several followers | one per leader world | yes | yes (one world each, ~16 MB reserved, ~0.02-0.07 ms per pair) |
| libsa1 work | slot-1 creation, two-character capture/render/events/sound split, collision centres, camera pos | analog override (optional) | slot-1 creation + proxy, "character slot" = 1, collision centres, camera pos, per-step leader snapshots |
| Fault isolation | leader dies with the follower | per world | per world (a follower fault never stops the player's character) |
| Fidelity vs DC | exact (Sonic leader) | approximate | AI and Tails code exact; leader seen through the proxy: the follower's rand() stream and land gather are not shared with the leader's world (cosmetic: idle choices, effects); leader-to-follower timing exact with per-step snapshots |
| Verification | bit-exact replay of new interpreter refs | behavioural only | bit-exact for slot 1 with the proxy fed from a ref (plus rand / XMTRX injection at player-1 entry) |

---------------------------------------------------------------------------------------------------------------------

## 4. Recommendation

**C for every follower, A only as a libsa1 test configuration.** Reasons:

1. It runs the game's own 1.5P code unchanged - steering, jump/flight, stuck rule, distance and off-screen respawn,
   2P takeover - with no override of the game's predicate (the world is the game's own 1.5P setting).
2. It answers every part of the request with one mechanism: Tails follows any character (Tails included), any player
   or NPC, another follower; several followers; other kinds follow too.
3. It keeps design D2 (one world per ticking character) and libsa1's per-world capture, render, events, sound and
   fault isolation as they are; the only "slot" change is which player a world reports.
4. It is verifiable: A-configuration replays prove the AI and slot-1 Tails against the interpreter; C replays with the
   proxy fed from the same reference prove the proxy.

Non-Tails followers use the same AI. What they do with it is their own game code: jump towards a higher leader (all),
second press in the air = Sonic's homing / jump dash, Knuckles' glide, E-102's hover (held), nothing for Amy / Big;
respawn `SetInputP(1, 48)` is flight for Tails, glide for Knuckles and ignored by the others (they drop from 50 above
the camera). Gate: Tails ships first; Sonic, Knuckles, E-102 (game-supported as player 1) after the bring-up test of
6, Amy and Big last (never player 1 in the game, 21 / 57 hard-coded player-0 references). A kind that fails its gate
falls back to B for that kind only (decided by the owner).

---------------------------------------------------------------------------------------------------------------------

## 5. Design

### 5.1 libsa1 (API 0.3 draft)

```c
#define SA1_API_FOLLOWER 1
typedef struct sa1_leader_state {       /* a host-fed leader (GMod entity, remote player, "stay here") */
    uint32_t struct_size;
    float    position[3];               /* host space */
    float    velocity[3];               /* host units per second */
    float    yaw;                       /* radians, the sa1_pose.yaw convention */
    uint32_t flags;                     /* SA1_LEADER_ON_GROUND */
    int32_t  kind;                      /* SA1_CHAR_* whose jog / run speeds the AI compares (-1: Sonic's) */
} sa1_leader_state;
typedef struct sa1_follower_desc {
    uint32_t struct_size;
    int32_t  kind;                      /* the follower (SA1_CHAR_TAILS first) */
    sa1_character_id leader;            /* a ticking character of this context, or 0 = host-fed */
    uint32_t flags;                     /* SA1_FOLLOW_UPGRADES, SA1_FOLLOW_SEED ... (as sa1_character_desc) */
    uint32_t seed; uint16_t upgrade_flags[32]; uint32_t num_upgrade_flags;
} sa1_follower_desc;
SA1_API sa1_status sa1_follower_create(sa1_context *, const sa1_follower_desc *, sa1_character_id *out);
SA1_API sa1_status sa1_follower_set_leader(sa1_context *, sa1_character_id f, sa1_character_id leader); /* 0: host-fed */
SA1_API sa1_status sa1_follower_feed(sa1_context *, sa1_character_id f, const sa1_leader_state *);
SA1_API sa1_status sa1_follower_respawn(sa1_context *, sa1_character_id f);   /* the game's own 8C081C36 path */
SA1_API sa1_status sa1_follower_info(const sa1_context *, sa1_character_id f, sa1_follower_info *);
/* AI mode / smode, off-screen counter, distance; ticking, state, render, events, destroy: the sa1_character_* calls
   (sa1_character_tick ignores the input of a follower) */
```

Follower world boot (`sa1_world_boot.c`, all game steps through the code map, inside the world):
1. Image of the follower's kind; runtime, heap, tasks, collision, globals as today, but `CurrentCharacter = 0`,
   stage 0x0100, `B_8C78B45A = 1`; no stage tasks, no death-zone task, clock stopped (E-102's timer).
2. Proxy leader in slot 0: `CreateElementalTask(7, 1, 8C098440 empty exec)`, a `playerwk` in the arena (not
   PInitialize: it would create the breath-bubble helper `func_8C0B3A2E` for player 0, `code_8C0B37F2.c`),
   `mwp->work = pwp`, `p` = the leader kind's physics row (table 0x8C1BDFE4), `counter.b[0] = 0`, `b[1]` = leader
   kind, slots 0 written. No CCL entry (the AI does not read player 0's `cwp`; whether other code dereferences
   `playertwp[0]->cwp` unchecked is **UNVERIFIED**: the bring-up test must run every kind for 60k+ steps).
3. Follower: Tails = the game's own `func_8C081AC0(proxy task)`; other kinds = the same three steps with their main
   (`CreateElementalTask(2, 0, 8C081D4E)` + `B_8C78C248` + dest 8C081B66, `CreateElementalTask(7, 1, <main>)` with
   `counter.b[0] = 1`, `b[1] = kind`, `8C0826B0` placement): the LoadPlayerTask-style glue libsa1 already writes for
   slot 0.
4. Before each step: proxy `pos`, `ang`, `flag & 3`, `mwp.spd`, `pwp.spd`, `pwp.p` from the leader. For a libsa1 leader
   the leader's world keeps a 4-entry ring of per-step player-0 snapshots (SA1 space, bit for bit); follower step k of
   a pair takes the snapshot after the leader's step k-1 (the DC order: the list-0 AI runs before list-1 players), so
   the host ticks leaders before their followers (G-SA1 orders them; chains topologically). Host-fed leaders: the
   converted `sa1_leader_state` for both sub-steps.
5. Camera adapter: **every live world** writes the eye of its synthetic view (40 behind, 7 above player 0,
   `sa1_world_tick.c:79-120`) into `camera_twp->pos` (today 0,0,0 - the respawn would put the follower 50 above the
   world origin). In a follower world player 0 is the proxy, so the view and the off-screen test are the leader's
   camera, as on the DC, and the respawn drops / flies the follower in above and behind the leader.
6. World "character slot" = 1: state, render, capture-hidden fallback, sound position and events of the follower id
   come from slot 1; the GRID window and moving-object gather use players 0 **and** 1 (union of two 400-unit windows).

Missing game code to decompile first (SADC, tiny): `8C049160` (18 B), `8C049A5A` (6 B, in chunk 8C0491C0),
`8C0584A0-8C058664` (17 functions, 392 B, incl. `8C0585BA` / `8C0585C2`), so `stubs.tsv` lines 101/104/119 go away.

### 5.2 G-SA1

* **Spawning** (owner rule: everything in Q menu > Options > G-SA1): a new page **"Followers"** in `cl_menu.lua`:
  character combo (default Tails), "Spawn follower", "Remove last", "Remove all", follow target combo (Me; Nearest
  player; a player list; Last follower (chain); Stay here), max followers slider (default 3, max 8: each is a world),
  "use this character's ticked upgrades", status lines (per follower: AI state, distance, respawns). ConVars
  `gsa1_follower_kind`, `gsa1_follower_target`, `gsa1_follower_max`, `gsa1_follower_upgrades`; console commands
  `gsa1_follower_spawn [kind] [target]`, `gsa1_follower_remove [all]`, `gsa1_follower_target <who>` as extras.
  Optional later: right-click (properties) on a follower: Follow me / Stay / Remove.
* **Targets**: the local character -> `leader = its id` (exact, per step); no local character -> the local player
  entity, fed each frame (`GetPos`, `GetVelocity`, `OnGround`, eye yaw); other players / NPCs -> their entity; another
  follower -> its id; Stay -> the last fed state frozen (the AI idles there). Target gone (disconnect, NPC removed,
  owner's character removed) -> fall back to the owner's player entity.
* **Module** (`gsa1_module.cpp`, `session.cpp/.hpp`): `CreateFollower(kind, pos, yawDeg, targetId|nil [, upgrades])`,
  `FollowerTarget(id, targetId|nil)`, `FollowerFeed(id, pos, vel, yawDeg, ground)`, `FollowerRespawn(id)`,
  `FollowerInfo(id)`; `Character.follower` + leader link; tick order leaders first; `commit_movers` around every
  ticking character (union) instead of the first; followers are non-local for the mixer
  (`sa1_audio_mixer_character(..., local = 0)`, so their positional sounds come from them; the game itself mutes
  Tails' own non-positional SEs as player 1).
* **Lua** (`cl_follower.lua`, new): list of followers, each with the same render entity as the player's character
  (`ents.CreateClientside("gsa1_render")`, `GSA1.Tex.Prepare(kind)`), ticked in `Think` after the player's character
  (`cl_character.lua:tick_character`), events per id (FAULT -> remove and say so; no DEATH expected for slot 1).
  Despawn: owner request, map change (`gmod13_close` already destroys all), world fault, cap lowered.
* **Multiplayer** (later, with the puppet relay of `sh_net.lua`): followers are owned by the spawning client and sent
  as extra state packets (owner, index); other clients pose puppets.
* **Interaction (round 5)**: host objects must be registered in follower worlds as well (per-world proxies,
  `libsa1_interaction.md` 2.3); slot-1 followers only get knocked back by hits (the game's own player-1 rule).

---------------------------------------------------------------------------------------------------------------------

## 6. Work plan (after round 4b has landed: it edits collision, capture, pose and audio)

| Step | Where | Work | Test / exit |
|---|---|---|---|
| F0 | SADC `src/1st_read` | C for `8C049160`, `8C049A5A`, `8C0584A0-8C058664` (new `code_8C0584A0.c`) | build OK (497 checks), shcdiff, harness `diff` |
| F1 | SADC `harness` | `trace --pno 1` (record player 1 as well: a second record block or a sidecar file, trace v3) and refs: `wv_follow_run` (Windy Valley act 1: run, jump, turns, flight), `wv_follow_pit` (the measured distance respawn), `wv_follow_offscreen` (leader on a ledge Tails cannot reach for > 300 frames), `sd_follow` (Sky Deck) | refs deterministic; `regress.py --route` identical; routing only the AI files: RAM identical (already measured for one script, 1.3-1.5) |
| F2 | LIB world | A-config test boot (`SA1G_BOOT_FOLLOWER_SAME`: LoadPlayerTask's slot-1 call for Sonic) | `tests/build_replay.py`: new refs bit-exact, players 0 and 1 |
| F3 | LIB world + API | C: proxy, follower boot, per-step snapshots, character slot, camera position, collision window union, `sa1_follower_*` | replay of the F1 refs with the proxy fed from player 0's records (+ rand / XMTRX / camera injection at player-1 entry): player 1 bit-exact; x86 / x64 identical |
| F4 | LIB tests | `tests/follower_test.c` on GRID (flat, ramp, ledge, pit, 400+ unit gaps): leader scripts x follower kinds: distance stats, respawn counts, faults / unimplemented = 0 in 60k random-leader steps per kind | gate per kind (Tails, then Sonic / Knuckles / E-102, then Amy / Big) |
| F5 | G-SA1 | module + session + `cl_follower.lua` + Options page "Followers" + ConVars / commands; README | core / binding tests (lifecycle, chains, target loss, cap), `diag_maps --followers N` timing |
| F6 | Owner | install (only when gmod.exe is not running), in-game test, status page update | owner sign-off |

---------------------------------------------------------------------------------------------------------------------

## 7. Questions for the owner

1. Default follower Tails and at most 3 at a time (8 allowed in the slider)? Each follower is a full game world.
2. Should the other characters use the game's own (Tails-tuned) follower AI - they cannot fly, so they catch up by
   dropping in near the camera - or a separate "assist" (e.g. warp behind you after N seconds stuck) for them?
3. Follow targets for the first version: you only, or also other players / NPCs / other followers (chains)?
4. Keep the game's behaviour that a following Tails' own sound effects are silent (positional sounds still play)?
5. The original lets a second controller take Tails over (and gives him back to the AI after 5 s idle). Wanted?
6. Should followers attack NPCs or props (the game's follower never attacks; touching hazards only knocks him back)?

---------------------------------------------------------------------------------------------------------------------

## 8. Risks and UNVERIFIED points

* The A-config proof covers one Windy Valley script (modes 0, 4, 2; smodes 0, 1, 3; flight, distance respawn).
  Untested paths: smode 2 jump-then-fly, the stuck rule, the off-screen respawn, the 2P takeover, Tails' flight end
  after the action press. F1 adds scripts for them. With **all** decompiled code routed (`sa1h_nm_pc.exe --route`)
  the player-0 trace is identical; the RAM differs outside the guest stack in regions that also differ without the AI
  (heap headers 0x8C7EE9F4.., 0x8C88FE2C, 0x8C8A3845: routing artefacts) plus 6,133 B at `B_8C79F208` (read by
  8C099750 / 8C099872) and, at the end, 4 B at 0x8C6B6F58 (camera module state) - not analysed.
* Proxy completeness: code other than the AI that reads player 0 (CCL target lists compare pointers only,
  `code_8C03339E.c:53-66`; bubbles; Big / Amy player-0 reads) may meet a work structure without CCL or motion data.
  Faults stay inside the follower's world (libsa1 marks it dead; the player's character keeps running).
* Amy and Big as player 1 are not game configurations; E-102 and Big call the death start `8C07BBC2(pno)` directly.
* Meaning of the CCL bit cleared every frame (`flag4 & ~0x20`) and of `B_8C78B45A` in the menus.
* rand(): in C the follower's world has its own rand stream, on the DC Sonic and Tails share one; idle animation
  choices and effects of both will differ from a DC 1.5P run (not their movement, which uses no rand in the AI).
* The collision window union and the per-world camera position touch files round 4b is editing
  (`sa1_coladapt_world.c`, `sa1_world_tick.c`): schedule F3 after 4b.
* Memory: worlds reserve 16 MB each; G-SA1 measured +80 KB private memory per create/destroy cycle
  (`gsa1_ramp_freeze.md:412`) - spawning and removing followers repeatedly makes that leak visible.

## 9. How the measurements were made

```
cd SADC
harness/build/sa1h.exe trace --level 2 --act 0 --chr 0 --no-objects \
  --input "60 0 0; 240 0 -127; 1 0 -127 A; 119 0 -127; 120 127 0; 120 0 0" \
  --out build/research/follower_ai/wv_sonic --dump-ram-at 0,2,4,60,180,300,420,540,end \
  --coverage build/research/follower_ai/wv_cov.txt              # 660 steps, 8.3 s
  ... --steps 470 --out .../wv_sonic_b --dump-ram-at 430,436,442,448,454,460,466
harness/build/sa1h_nm_pc.exe trace (same) --route-only code_8C081AC0.c,code_8C081D00.c,code_8C082160.c,code_8C08273C.c \
  --check build/research/follower_ai/wv_sonic.bin --dump-ram-at 300,436,end   # check identical; RAM 0 differing bytes
harness/build/sa1h.exe trace --level 1 --act 0 ... --input "10 0 0" --dump-ram-at end     # EC1: no AI
harness/build/sa1h.exe trace --level 9 --act 2 ... --input "10 0 0" --dump-ram-at end     # CP act 3: parked
py -3.12 build/research/follower_ai/ramq.py <ram files>      # summaries kept in ram_summaries.txt (RAM images deleted)
```

---------------------------------------------------------------------------------------------------------------------

## Review (adversarial)

Reviewer pass, 2026-10-21, read-only (nothing in LIB, g-sa1, SADC/src, include, tools, harness or config changed).
Scratch: `SADC/build/research/follower_review/` (`ram_check.txt`; `reach.py` static call closure + GetPlayerNumber
callers; `p0refs.py` / `p0deref.py` player-slot readers; `camreaders.py` camera_twp readers; `rev_ovl.py` constant /
callee search over all 249 `build/*.xrefs.json`; `pure.*` / `route.*` traces). The investigator's text above is
unchanged.

### R.1 Verified

| # | Claim | Evidence |
|---|---|---|
| V1 | "1.5P" is entry 0 of the dead "NPC [%s]" list | `build/1ST_READ.BIN`: `D_8C1B9658[0..6]` -> 0x8C1549FC "1.5P", 0x8C154A04 "BOSS SONIC vs KNUCKLES" ... 0x8C154A74 "BOSS E102 vs MILES"; format 0x8C1549F0 "NPC [%s]". Selector reads `B_8C754DAC->press` bits 0x80 / 0x40 (`asm/1st_read/text/8C0584A0.src` 8C0584AC-8C0584C8). No aligned or unaligned word equal to 8C0584A0, 8C0584F8, 8C081C00, 8C081B6E or 8C05865C in any of the 249 `build/*.BIN` |
| V2 | LoadPlayerTask calls `func_8C081AC0(player 0 task)` in iteration 1 only for CurrentCharacter 0 | `build/ghidra/1ST_READ/decomp/8C04F3C0.c` (the `else` branch of the loop) |
| V3 | Creation, supervisor, steering, respawn test, placement as described in 1.2-1.5 | `code_8C081AC0.c:23-54`, `code_8C081D00.c:91-230`, `code_8C082160.c:59-270`; constants and masks (`D_8C19E38C` = 0x402, `D_8C19E390` = 0x4 read from the image) |
| V4 | Predicate table | `code_8C08273C.c:81-178`, every row matches. Note: `func_8C0829E6` is called before the switch for **every** level (line 84), so with `B_8C78B45A = 1` it returns at its first test and never reaches `func_8C049A5A` (fatal stub, `LIB/gen/policy/stubs.tsv:104`) |
| V5 | `func_8C082570` returns only 0 / 1 (supervisor mode 3 dead); modes 1, 7 and smodes 4, 5 have no writer | asm 8C08269A `MOV #1,R0` / 8C0826A2 `MOV #0,R0` are the only return values; all mode / smode stores are in 8C081AC0 / B8A / C00 / C36 / D4E / 8C082160 |
| V6 | RAM byte-identical with the 4 AI files routed | **Re-run**: `sa1h.exe` vs `sa1h_nm_pc.exe --route-only` (same script): "routing 18 native decompiled functions", `--check` identical, RAM SHA-1 equal at step 300, 436 and end (`follower_review/ram_check.txt`). The investigator's `wv_pure.csv` / `wv_route_ai.csv` differ only in the `icount` column (660 rows) |
| V7 | Decompilation status | `status.py` re-run: 18 MATCHED 2,642 B, 4 NON_MATCHING 1,882 B in 8C081AC0-8C082D60; 8C0584A0-8C0586A0 = 17 functions, 392 B (xrefs sizes) |
| V8 | AI already in the DLL, never called | all 18 routed functions are `def` in `LIB/build/pclower/ga64/sa1g_fntab.c:2765-2783` (the tree LIB links), `func_8C049160` is a stub (:2361); slot-1 skip comment `sa1_world_boot.c:395-406` |
| V9 | Camera position never set in libsa1 | `sa1g_camera_refresh` copies `w->camera.pos` (`sa1_world_tick.c:61`), but nothing assigns it (only `ang[1]`: `sa1_world_api.c:193`, `sa1_api_world.c:287`; `w` zeroed `sa1_world.c:862`) |
| V10 | Overlay conflict, Tails singletons, forced actions | `sa1_manifest.c:16-20`; `code_8C0BDC80.c:74-86`; `case 48` only in Tails `code_8C0BBDD6.c:362` and Knuckles `code_8C0C5BCA.c:286` (Sonic / E-102 / Amy / Big files have 12 and 24 only; Sonic's unhandled request returns 0, `code_8C0AFF1A.c:430`) |
| V11 | Slot 1 does not die / lose rings (Tails) | death zone `code_8C07BAC0.c:113`; drowning `code_8C07BE68.c:89-92`; from the TailsMain closure the only caller of the death start 8C07BBC2 is the ring scatter (flag 0 only). Nuance: `func_8C086F5E` draws `rand()` before testing the flag (`code_8C086F5E.c:54`), so a slot-1 hit still consumes one rand |
| V12 | `B_8C78B45A = 1` is side-effect free inside a world | its only 1ST_READ readers are `func_8C0829E6` and the unreferenced getter `func_8C05865C`; the rest of its block `B_8C78B450` is menu / save state (ADVERTISE, SUMMARY) |
| V13 | No false 2P takeover in libsa1 | unconnected rt ports report support 0 (`rt/rt_input.c:85-94`), so `B_8C754DA8[1]` = `&D_8C19E3AC`, whose 0x34 bytes are all 0 in the image: mode 4 never sees controller-2 input |
| V14 | Proxy sufficiency for Tails (static part) | `p0deref.py`: in the static closure of TailsMain + 8C081D4E (1,159 functions) the only **non-indexed** reads of `playertwp/mwp/pwp/tp[0]` are the AI, the camera adjusters 8C01E32A-8C023360 (camera task code, not run in worlds), the ring scatter (flag 0 only) and the SET-layout check 8C0919E8. GetPlayerNumber callers in that closure: 8C07CE40, 8C082820, 8C086F5E, 8C094FBE (voice bank load, reached only from the event code 8C049560). Calls through pointers are not covered: the 60k-step test stays necessary |
| V15 | G-SA1 commits movers around the first ticking character | `g-sa1/src/core/session.cpp:299-312` |

### R.2 Refuted or corrected

1. **Boss labels are reversed (1.8, 3.C).** `func_8C0584F8` maps list entry i (1-6) to kind i-1 (asm L8C058534-L8C05854C);
   `func_8C083F7C` creates SonicMain for kinds 0/1, KnucklesMain for 2/3, E102Main for 4/5 (`code_8C083E5C.c:96-113`,
   `kind` at +0). So "BOSS SONIC vs KNUCKLES / E102" = **Sonic** is the AI player 1 (Knuckles' / E-102's game);
   Knuckles and E-102 are player 1 of Sonic's game in "BOSS KNUCKLES vs SONIC" / "BOSS E102 vs SONIC". The conclusion
   (Knuckles / E-102 as player 1 with CurrentCharacter 0 is a game configuration) stands with the corrected names. The
   real callers of the boss AIs are 8C0585CA / 8C0585D0 from `func_8C09072C`.
2. **STG03 also sets the hold.** `asm/overlays/stg03/text/0C907A60.src`: pool L0C907E30 = `func_8C0585BA` (set) used
   at 0C907CC8 and 0C907DA8; `func_8C0585C2` (clear) at 0C907CDC and through L0C908004. Not "clears it" only.
3. **The game's land gather uses 40, not 400, around player 1.** `func_8C052518` uses `D_8C19E96C` (400) for the first
   position only and 40 for the second (`code_8C052518.c:158/186` moving entries, `:200/232` landtable entries). The
   1.6 row "gathers around players 0 and 1" and 5.1 step 6 "union of two 400-unit windows" overstate what the game needs.
   In option C the proxy collides with nothing: the GRID window and the moving-object gather only need to be centred on
   **slot 1**; a second 400-unit window around the proxy costs capacity (`SA1_CAPACITY_WINDOW_POLYGONS`). The union is
   needed only in the A configuration.
4. **Task order is head-first.** `func_8C0985EC` links each new task at the **head** of its list
   (`code_8C098444.c:195, 223-231`) and the executor walks from the head (`sa1_world_boot.c:681-700`). The stage start
   creates the camera task (8C01957E) before LoadPlayerTask (`ghidra .../8C04E660.c:11, 16`). So on the DC the AI runs
   **before** the camera task in list 0 (it sees XMTRX and `camera_twp->pos` as left by the previous step), and Tails
   (created after Sonic) runs **before** Sonic in list 1. "The AI reads the leader after step k-1" stays true; the
   injection points of F2 / F3 do not (next item).
5. **F3 "rand / XMTRX / camera injection at player-1 entry" is the wrong point for XMTRX and the camera.** The AI reads
   XMTRX (`njCalcPoint`, `code_8C082160.c:238`) and the camera position (`func_8C049160`, `code_8C081D00.c:142`) in
   list 0, before any player. libsa1 replay skips the synthetic view (`sa1_world_tick.c:86`) and injects only at
   player-0 entry (`sa1_world_boot.c:586-597, 620-647`). Bit-exact replays (F2 and F3) need an injection at the AI
   task's entry; rand at the first player entry, which is Tails' (item 4), not Sonic's.
6. **A 4-entry snapshot ring is too small.** `sa1_character_tick` runs up to `SA1G_MAX_PAIRS_PER_TICK` = 8 pairs (16
   steps) per call (`sa1_world.h:481`, `max_pairs` 0 at `sa1_api_world.c:289`). Ticking the leader then the follower
   per host tick needs up to 17 snapshots, and the follower must run exactly the leader's pair count instead of its own
   dt accumulator (which drops backlog on its own, `sa1_world_tick.c:497-502`).
7. **"Leader-to-follower timing exact" (3, table) is overstated.** At the end of step k the land gather (and its 128-entry
   caps) uses player 0 **after** step k on the DC; a proxy refreshed only before the step holds step k-1. Exactness
   needs a second proxy refresh after the executor (before CCL / land update), or the doc should list it as a deviation.
8. **F2 is internally inconsistent.** "B_8C78B45A=1 on stage 0x0100" cannot replay the F1 refs, which are Windy Valley
   / Sky Deck with the flag clear. Replaying them makes `func_8C0829E6` reach `func_8C049A5A` every frame (`wv_cov.txt`
   lists 8C049A5A and 8C09068E), a fatal stub today: F2 depends on F0's `8C049A5A`.
9. Smaller corrections: "All 22 functions of the AI" includes the stage title card (8C082A66-8C082C5A) and the
   race / boss helper 8C0827C4; mode 4 runs the steering and the respawn test whether or not controller 2 is active, and
   mode 5 runs the respawn test too (`code_8C081D00.c:158-199`); libsa1's press OR acts on the peripheral records
   `B_8C754DA8[0..1] + 0x10` (`sa1_world_tick.c:290-318`), not on the game records the AI writes; `B_8C78B45A` is also
   cleared by 8C058646 (memset of `B_8C78B450[0..17]`).

### R.3 Gaps

* **G1 Camera yaw of a follower world is undefined.** `sa1g_camera_view` builds XMTRX from `w->camera.ang[1]`, which only
  the tick input sets; 5.1 says the follower's input is ignored and `sa1_leader_state` has no yaw. The off-screen test
  and the respawn eye would then use one fixed world direction. Define the source: the leader world's `camera.ang`
  (libsa1 leader) or the host viewer's yaw (host-fed leader).
* **G2 Follower visibility / rendering.** With the view 40 behind the proxy, a follower trailing more than about 40
  units behind the leader along the view is behind the synthetic eye (step 180 of the measured run: 76.7 behind). The
  display's visibility test then fails, libsa1 renders the CPU-pose fallback (`sa1_character.c:936-945`) and skips
  Tails' display-side logic and effects (`sa1_world_tick.c:65-69`). Not discussed; expected to be frequent while running
  (**UNVERIFIED**, measure in F4). Option to put to the owner: load a follower-centred view after the AI task (list 0)
  and before list 1, so the AI keeps the leader camera and the follower stays visible.
* **G3 "Every live world writes its eye into camera_twp->pos" changes existing characters.** 43 functions in the static
  closure of the six mains read `D_8C18B7EC` (`camreaders.py`), among them Tails' eye target (`code_8C0BE558.c:203-207`:
  today Tails looks at the world origin), Big (`code_8C0E6500.c:41-45`, `code_8C0EAA80.c:66`, `code_8C0EAEC0.c:61`,
  `code_8C0EC212.c:176`), `code_8C1032AC.c:582`, rings `code_8C0869E0.c:153`. Probably an improvement, but a visible
  change outside the follower feature. Also `sa1_world.h:351` says the adapter is meant to carry the **host** camera:
  synthetic eye vs GMod view origin is an owner decision. Limit it to follower worlds in F3, or make it its own item
  with capture checks.
* **G4 Respawn-point safety.** The synthetic eye has no wall avoidance (the DC camera has). Eye + 50 Y can be inside
  geometry or over a void on GMod maps; Knuckles' glide aside, non-Tails followers ignore 48 and fall, which can loop
  (distance > 1000 -> respawn -> fall). F4 needs "leader backed against a wall" and "leader on a ledge over a void".
* **G5 CurrentCharacter = 0 for the follower itself.** 3.A rejects it for the leader but 3.C imposes it on every
  follower. GetPlayerNumber callers in each main's static closure: Sonic 4, Tails 4, E-102 3, Amy 7, Big 14, Knuckles 17
  (`reach.py`). Examples: Amy's 8C0DEDC0 / 8C0DF060 return 0 unless the game is Amy's (`code_8C0DEDC0.c:41`,
  `code_8C0DEEA0.c:112`), so Amy-specific stage features are off; Knuckles' 8C0C88E6 is ASM (**UNVERIFIED**). F4 needs
  this list per kind.
* **G6 The proxy's `counter.b[1]` is read by the follower.** The follower's own breath-bubble helper picks its
  parameters by player 0's character (`code_8C0B37F2.c:151`; no set for E-102: the helper frees itself, NULL-safe at
  8C0B3B74), also `code_8C043A20.c:118` and `code_8C05DF18.c:80-81`. A host-fed leader ("-1: Sonic's") must write 0.
* **G7 CCL between the players.** On the DC both players' colliwk are in the same CCL pass and the AI clears `flag4`
  0x20 on Tails' infos every frame (meaning **UNVERIFIED**). The proxy has no CCL, so F3 bit-exactness may fail on
  CCL-derived state if Sonic-Tails contacts exist. F2 (A config) is where to measure it.
* **G8 Raw-stick readers.** The AI writes `on` / `press` / analog only; `per[1].x1/y1` stay 0. Amy's
  `code_8C0DCBA0.c:277-282` and the board code `code_8C06FB8C.c:80-96` read the raw stick of `B_8C754C08[pno]`, so an
  AI-driven Amy in that mode, or a board rider, steers as with a centred stick.
* **G9 Boot config.** `sa1g_boot_cfg.chr` sets CurrentCharacter, the main, the start table, E-102's timer set-up and
  `w->chr` (`sa1_world_boot.c:430, 518`; `w->chr` feeds `cw_window`, `sa1_coladapt_world.c:1038`). F3 needs a separate
  current-character field.
* **G10 Owner decisions.** D2 (`libsa1_integration.md:242`, owner sign-off) and 6.8 (:875-876) describe the follower
  as "slot 1 in the same world". Moving production to option C deviates from that note, and section 7 does not ask.
  Interaction decision (4) PvP (`libsa1_interaction.md:819`): may the owner's attacks hit his own followers?
* **G11 Game code for player-1 creation exists.** For Sonic / Knuckles / E-102, `func_8C083F7C` (matched,
  `code_8C083E5C.c:88-118`) is the game's own slot-1 creation. Owner rule: use it rather than new glue; only the
  AI-task half of `func_8C081AC0` needs glue.
* **G12** How the DC runs Sonic with Knuckles / E-102 in the boss fights when S_MOT / K_MOT / E_MOT all load at 0x0CC00000
  is not explained (**UNVERIFIED**). The "only Tails can share" statement holds for libsa1's images, not as a game fact.
* **G13 F1 must record more than player 1:** the AI task state (mode, smode, `counter.w[0]` at +8, timers), `per[1]` and
  `analog[1]` after the AI, XMTRX and the camera taskwk at AI entry, and rand at the first player entry. The off-screen
  script needs a distance of 50-1000 and 301+ off-screen frames, which flight makes hard on a ledge. A leader that
  outruns Tails is more reliable; check that the counter passes 300 in the summary.
* **G14 API details.** `sa1_follower_info` is not declared. A host-fed `velocity` must be turned into `pwp->spd.x`
  (player-space forward speed per 1/60 s) for the brake / jump tests. Per-follower cost "0.02-0.07 ms per pair" is an
  estimate, not a measurement.

### R.4 Required changes to the work plan

| Step | Change |
|---|---|
| F0 | `8C049160` is the only stub option C reaches (respawn). `8C049A5A` (and the `8C09068E` path) is **required** by F2, not optional. The debug selector 8C0584A0 / 8C0584F8 (dead, calls the SDK print 8C641110) can stay ASM |
| F1 | Trace v3 as in G13; the off-screen script must show counter > 300 and a respawn by the counter |
| F2 | Boot the refs' own stage (0x0200 / 0x0600, DISC landtable, flag clear), not 0x0100. Depends on F0. Add injection at the AI task's entry (XMTRX, camera blob) and at the first player entry (rand) to `sa1g_run_tasks` (`sa1_world_boot.c`). Report CCL-state differences (G7) |
| F3 | (a) camera yaw source (G1); (b) snapshot ring >= 2 x `SA1G_MAX_PAIRS_PER_TICK` + 1, follower pairs slaved to the leader's pairs; (c) second proxy refresh before CCL / land update, or a documented deviation (R.2.7); (d) window and moving-object gather centred on slot 1 only (R.2.3); (e) camera position written in follower worlds only, unless the owner approves it for all (G3); (f) visibility decision (G2); (g) boot-config split (G9); (h) `func_8C083F7C` for Sonic / Knuckles / E-102 (G11); (i) the test files (e.g. `tests/follower_replay.c`, `build_follower.py`) in the file list; injection points per R.2.5 |
| F4 | Add: per-kind GetPlayerNumber audit (G5), respawn-point safety (G4), hidden-capture fraction (G2), an E-102 leader (G6), Amy's raw-stick mode (G8) |
| F5 | Unchanged (the Options page meets the owner rule); add a PvP-on-own-followers option if the owner wants it (G10) |
| 7 | New owner questions: production option C vs D2's same-world note; camera eye source (synthetic vs GMod view) and whether every world gets a camera position; follower visibility policy (G2) |

**Verdict:** the research part is correct and reproducible. 1.5P is what the doc says it is, and the four NON_MATCHING
functions are functionally verified for this script (re-measured). Option C is feasible and the best-justified choice.
It is not ready to implement: the camera / view handling (yaw, visibility, eye placement and its side effect on all
worlds), snapshot sizing and pair slaving, the replay injection points, and F2's stage / dependency need the changes
above. Approve with required changes.
