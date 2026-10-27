# libsa1 host objects: SA1-style interaction with the host world

Design, 2026-10-20. Documents only: nothing here is implemented yet. Related: `docs/libsa1_integration.md` (world
core, GUEST-ARENA, tick contract, moving collision objects 5.5), `LIB/include/libsa1.h` (API 0.2),
`g-sa1/README.md` (GMod host 0.3.0).

Owner's test of G-SA1 0.2 (the part this design answers): *"The characters don't react to damage and don't do
damage. Gamma can't lock on to bad guys and vehicles."* Earlier owner preference: every option belongs in the Q menu
(Options -> G-SA1); console commands are extras.

## 0. Summary

* SA1's characters interact with enemies through one engine system, **CCL** (object-vs-object collision,
  1ST_READ 8C02E820-8C0344A0, all decompiled and lowered into libsa1). Every object enters a per-id list each step
  (`CCL_Entry` 8C02F63C); one pass after the task executor (8C0342E0) tests the lists pairwise, records hits, compares
  **attack and defence levels** (2 bits each per shape) and sets `taskwk.flag 4` ("damaged") on the loser, pushes
  solid shapes apart, and fills the players' **target lists** (homing attack, Light Speed Attack, throws, Knuckles'
  Maximum Heat). E-102 searches the enemy lists (ids 2 and 3) directly for its lock-on.
* Enemies are ordinary tasks with a CCL shape table (id 3), a 0x23C-byte enemy work as `tp->mwp`, and a damage check
  (8C0489E4) that bounces the player (8C048980), adds score and marks the work "killed by a player". A player learns it
  was hit from the same `taskwk.flag 4`, read by its mode machine at the start of its next exec: knock-back, 120 steps
  of invulnerability, then the barrier or the ring scatter (8C086F5E: up to 20 rings, death at 0).
* **Design:** a libsa1 *host object* layer. The host registers proxy objects (ENEMY, TARGET, BREAKABLE, HAZARD) with
  shapes in host space; libsa1 mirrors them into **every ticking character's world** as **real game tasks** built
  exactly like SA1 enemies (CCL id 3 or 4, the game's own CCL_INFO bits, a 0x23C enemy work, a SET-condition stand-in)
  whose exec is a libsa1 host function at a synthetic code address (0x7F000100). Because they are real CCL objects,
  the decompiled player code reacts natively: homing attack, Light Speed Attack, E-102 lock-on and lasers, Knuckles'
  Maximum Heat, bounce, ring loss, invulnerability, death. libsa1 reports per-step events back (OBJECT_HIT with attack
  kind / level / damage / position, LOCKED / TARGETED / UNLOCKED for E-102, character HURT) and gives the host a damage
  request (`sa1_character_damage`) that feeds the game's own hurt path, plus options (no death, keep rings) and the
  ring count.
* **G-SA1:** the client scans NPCs, NextBots, vehicles, breakables and hazards near the character and keeps them as
  host objects; hits go to the server as validated `DamageInfo` (attacker = the player); the server's
  `EntityTakeDamage` on an active G-SA1 player is turned into a ring-losing hit on the client; a ring counter HUD and
  every option are on the Q menu page.
* **Game code to decompile first: none is required** (everything the layer calls is lowered). Several untraced or
  NON_MATCHING paths must be verified with new reference traces (4.1), and CCL_Init's endless loop on a failed
  allocation needs a `PC_ONLY` fatal exit (an SADC change, requested from the owner of that tree).

Notation: SA1 units (1 SA1 unit = 4 Hammer units in G-SA1's Source preset); "step" = one 60 Hz logic step, "pair" =
the two steps of one 30 Hz frame (integration design 6.4).

Correction to the task brief: B_8C73EE38 / B_8C742288 are the **player target lists** (homing, throws, Maximum Heat,
Big); E-102's lock-on does not use them, it walks the CCL lists of ids 2 and 3 (B_8C745908 / B_8C745B08). 8C091B8A
clears the SET condition bits of a locked target when E-102 fires (1.6: a pitfall for proxies).

---

## 1. How SA1 does it (research)

All addresses are 1ST_READ (US 1.005). Every function named in this section is lowered into libsa1 unless marked
otherwise (checked against `build/pclower/functions.json` and `LIB/gen/policy/stubs.tsv`).

### 1.1 The CCL lists and the pass

`CCL_Init(tp, info, id, 1, n)` (8C02F4B0, `code_8C02F27C.c`) CAllocs a colliwk (0x1C) and a copy of the caller's
CCL_INFO table (n x 0x34), sets `cwp->flag |= 0x20`, `|= 0x800` for ids 2, 3 and 7 (= "can be targeted"), and the
bounding radius (`range`: 25.0 for ids 0/1, else 8C02F400 = 1.01 x max over the shapes of |center| + extent).
`CCL_Entry(twp)` (8C02F63C) must be called by the object's exec **every step**: it resets the hit state of the last
pass (8C02F5E0: hit fields, `attr & ~0x100` and `flag & ~0x10` per shape, `taskwk.flag &= ~0x3C`,
`colliwk.flag &= 0xBF21`) and appends the colliwk to the list of its id (skipped when `D_8C19B468`, the event mode, is
set or the task's exec is `DestroyTask`).

| id | List (count) | Max | Registered by (examples, CCL_INFO table) | Tested against (pass function) | Targetable (`flag 0x800`) |
|---|---|---|---|---|---|
| 0 | B_8C7456E8 (B_8C7456D8) | 8 | players: Sonic 8C1C5E80, Tails 8C2575AC, Knuckles 8C352FBC, E-102 8C422B80, Amy 8C4BB7F0, Big 8C525BB4 | 0, 1, 6, 7 (8C0335D4); 2, 3 (8C033800); 4, 5 (8C033A10); 8 (8C033CDC) | no (player 0 is a target of player 1 when its colliwk has 0x800) |
| 1 | B_8C745708 (B_8C7456DA) | 128 | character projectiles / attack objects: E-102 laser shot D_8C422DF8 (8C0D3DA0), its impact sphere D_8C4BB090 (8C0D65BA), Big's lure D_8C566010 (8C0E943C), Sonic's sparkles D_8C1C6B10 (8C0B5B80) | 0; 2-7 (8C0335D4) | no |
| 2 | B_8C745908 (B_8C752BD8) | 128 | air item box D_8C1B9C08 (8C05D9C0), the rival-fight targets 8C082D60 / 8C085532 | 0, 1 | yes |
| 3 | B_8C745B08 (B_8C752BDA) | 256 | enemies: Kiki D_8C5C2504, Rhinotank D_8C5992B8, Egg Keeper D_8C5B7EB4, Unidus D_8C5C4F00, E-100 Zero D_8C5C6B18; animals D_8C111DCC | 0, 1; 3 x 3 (8C03415E); 3 x 4, 3 x 5 (8C033DAE) | yes |
| 4 | B_8C745F08 (B_8C7456DC) | 512 | objects: Unidus spikes D_8C5C4F34, Zero's catch probe D_8C5C6B80 and shock wave D_8C5C6D1C, event model D_8C19C1C4, character helper objects | 0 (the player's first shape against forms 0-5, boxes with landing), 1, 3 | no |
| 5 | B_8C746708 (B_8C7456DE) | 128 | objects | 0, 1, 3, 5 x 5 (8C034250) | no |
| 6 | B_8C746908 (B_8C7456E0) | 256 | animals D_8C111D64 | 0, 1 | no |
| 7 | B_8C746D08 (B_8C7456E2) | 256 | rings (objexec_RING 8C0869E8, scattered rings 8C0870EA, D_8C1BF0BC) | 0, 1 | yes (`twp->id == 11`: light speed dash list instead) |
| 8 | B_8C747108 (B_8C7456E4) | 256 | shapes of forms 6/7 only | 0 | no |

The pass `func_8C0342E0` (`code_8C033DAE.c`) runs once per step after the task executor (libsa1 step order,
integration 6.4: pad, executor, **CCL**, collision adapter, land update). It empties the four target lists, runs
`func_8C033F00` (id 0 x all, id 1 x 2..7), `func_8C03415E` (id 3 x 3, 3 x 4, 3 x 5) and `func_8C034250` (id 5 x 5),
then resets every list count (8C02F81A). So a list holds exactly the objects whose exec called CCL_Entry earlier in the
same step, and the hit state an object reads in its exec is the result of the previous step's pass.

Every pair first passes a bounding-sphere test on the task positions, `func_8C03357A`:
`|pos1 - pos2|^2 < (range1 + range2)^2`. Before the test it calls `func_8C03339E` with that squared distance: when
cwp1 is a player (id 0), cwp2 has `colliwk.flag 0x800` and the squared distance is **below 10000 (100 units)**,
cwp2's task goes into the player's target list (`B_8C73EE38` for player 0, `B_8C742288` for player 1; 1160 entries,
NULL-terminated `PL_TARGET {twp, dist}`; tasks with `twp->id == 11` go to the light speed dash lists `B_8C741280` /
`B_8C7446D0`, 512 entries).

### 1.2 Shapes and their bits

`CCL_INFO` (0x34, `include/sa1/ccl.h`):

| Field | Bits | Meaning (code that establishes it) |
|---|---|---|
| attr | 0x0000000F | form: 0 sphere (a = radius), 1 cylinder (a = radius, b = half height, vertical), 2 cylinder 2, 3 capsule (a = radius, b = half length along the shape's axis), 4 box (a/b/c half extents, rotated about Y by angy, plus the owner's ang.y unless 0x20/0x2000), 5 box 2 (Y and Z rotation), 6/7 special (against the player's first shape only) |
| | 0x10 | shape off (every pair loop skips it) |
| | 0x20 | `center` is absolute (world); the players keep their shape 0 there, rewritten every step (Sonic 8C0AE110) |
| | 0x40 | rotation order Y, Z, X for the centre (else Z, X, Y) |
| | 0x80 | **damageable**: the shape can be out-levelled (8C02FA20) |
| | 0x100 | drawn this step (debug display only) |
| | 0x200 | on the attacker's shape: no bounce; the enemy work gets 0.7 / flag 0x800 instead (8C0489E4) |
| | 0x800 | on the hit shape: no "push" mode for Sonic (SonicModeMachine mode 2) |
| | 0x2000 | `center` is an offset from `twp->pos`, **not rotated** (8C02FC12, 8C02FD32); homing (8C0AF280) and Knuckles (8C0C8F20) add `twp->pos` for every shape without 0x20 |
| | 0x10000 / 0x20000 / 0x40000 / 0x80000 | ignore colliwk ids 0-1 / 2-3 / 4-5 / 6-7: the pair filter 8C0332A0 skips the shape pair |
| | 0x100000 | E-102: lockable but not counted (no lock number, no time bonus; 8C0D32A0) |
| | 0x200000 | the other side's hit slots stop updating for the rest of the pass (8C02F8A0) |
| flag | 0x03 | **attack level** 0..3 |
| | 0x0C | **defence level** 0..3 (`>> 2`) |
| | 0x10 | out-levelled in the last pass (set by 8C02FA20, cleared by CCL_Entry) |
| | 0x20 / 0x40 / 0x80 | the attack applies to colliwk id 0 / ids 2-3 / ids 4-5 |
| b05 | 0x01 / 0x02 / 0x04 / 0x08 | this shape **pushes** ids 0 / 2-3 / 4-5 / 6-7 out (8C02FB3A) |
| | 0x10 / 0x20 / 0x40 / 0x80 | this shape is **not pushed** by ids 0 / 2-3 / 4-5 / 6-7 |
| center, a, b, c, angx/y/z | | geometry |
| hit_info, hit_cwp | | per shape: the other colliwk and its shape index of the last recorded hit |

`colliwk` (0x1C): `id`; `flag` (0x02 hit this pass, 0x04 / 0x08 / 0x10 capsule cap / side / cap contact, 0x20
initialised, 0x80 pushed by a solid, 0x800 targetable, 0x4000 hit slots frozen); `nbInfo`; `test_num`; `range`;
`info`; `mytask`; `my_num` (own shape of the last hit); `hit_num` (shape index in `hit_cwp`); `hit_cwp`.

Supported form pairs (from the dispatchers 8C0335D4, 8C033800, 8C033A10, 8C033DAE): a player's sphere (every
character's shape 0) works against forms 0, 1 (cylinder; in 8C033800 the sphere is treated as a cylinder of radius a,
half height b: 8C030634), 3 (capsule, 8C030F20) and 4 (box, 8C031B88; with landing for ids 4/5, 8C031F88).
**Capsule x capsule and cylinder x capsule are compiled out** (8C0315C0 / 8C031726 build the segments and return
100000.0f without testing). Tails' flight cylinder does not test against boxes. Proxies therefore use spheres,
vertical cylinders and Y-rotated boxes only.

### 1.3 A hit

On an overlap the shape test calls `func_8C02F8A0` (record: both colliwks get `flag |= 2`, `hit_cwp`, `hit_num`,
`my_num`, per shape `hit_cwp` / `hit_info`; a slot that holds a player is not overwritten by a non-player; both tasks
get `taskwk.flag |= 8`) and `func_8C02FA20` (levels): with `atk1 = ci1->flag & 3`, `def2 = (ci2->flag >> 2) & 3`,
**if ci2 has attr 0x80, atk1 > def2 and ci1's applies-to bit covers cwp2's id, then `ci2->flag |= 0x10` and
`twp2->flag |= 4`** (and the same the other way round). The sphere test then pushes the tasks apart along the centre
line by the overlap, each side only if 8C02FB3A allows it (y ignored between two players).

### 1.4 The characters' shapes and levels

Shape tables (dumped from the user's 1ST_READ; the centre of shape 0 is absolute and rewritten every step):

| Character | Shape | attr | flag (atk/def) | b05 | a / b | Role |
|---|---|---|---|---|---|---|
| Sonic | 0 | 0xA0 sphere | 0xE0 (levels set per step) | 0x06 | 4 / 4 | body |
| Tails | 0 | 0xA0 | 0xE0 | 0x8F | 4 / 4 | body |
| | 1, 2 | 0xB0 (off) | 0xE9 (1/2) | 0x82 | 6 | tail attack (mode 20, at the tail tips) |
| | 3 | 0xB1 cylinder (off) | 0xE9 (1/2) | 0x82 | 5.5 / 1.75 | flying (mode 15, 8.25 above the body: hits from below) |
| Knuckles | 0 | 0xA0 | 0xE0 | 0x8F | 4 / 4 | body |
| | 1 | 0xB0 (off) | 0xEB (3/2) | 0xF2 | 8 | punches (actions 54-63, hit frames from D_8C15BFEC) |
| | 2, 3 | 0xB0 (off) | 0xE9 (1/2), 0xE0 (0/0) | 0xF2 | 4.5, 2.75 | action 51 (digging) |
| E-102 | 0, 1 | 0xA0, 0xA0 | 0xE0 | 0x8F | 6, 4 | body (levels on both) |
| Amy | 0 | 0xA0 | 0xE0 | 0x8F | 4 / 4 | body |
| | 1 | 0xB0 (off) | 0xE9 (1/2) | 0xF0 | 7.5 (9 with the upgrade) | hammer (actions 82, 86, 89, 95) |
| Big | 0 | 0xA0 | 0xE0 | 0x8F | 4 / 4 | body |
| | 1 | 0xB0 (off) | 0xEE (2/3) | 0xF0 | 8 | while `twp->flag & 0x400` (an attack state; which move: to verify) |

Levels written every step by the mains on the body shape (atk/def): **Sonic** (SonicMain 8C0A77BC): invincibility item
or Super Sonic 3/3, mode 44 (Light Speed Attack) 2/3 (8C0B11C0 writes 3/3 when it starts), mode 6 (spin dash release)
1/3, ball (`twp->flag 0x100`: jump, roll, homing) 1/1, mode 45 (light speed dash) 0/1, else 0/0. **Tails**
(8C0BF262): ball 1/1, else 0/0. **Knuckles**: invincible 3/3, ball 1/1, mode 18 (Maximum Heat, 8C0C8F20) 3/3, mode 52
0/1; during the punches' hit frames 0/3 on the body while shape 1 is on (8C0C7786). **E-102** (E102Main): invincible
3/3, mode 53 0/1, else 0/0 (both shapes). **Amy**: hammer actions 0/3 on the body (8C0DCBA0). **Big**: invincible 3/3,
mode 51 0/1, else 0/0.

Attack objects in list 1: E-102's laser shot (atk 3, def 3, applies to ids 2-5), its impact sphere (atk 2, ids 2-3,
radius grows over 60 steps), Big's lure (atk 2, def 3, ids 2-3), Sonic's sparkles (atk 1, def 3, ids 2-3;
equipment bit 2).

### 1.5 How a player learns it was hit

1. The pass sets `taskwk.flag |= 4` on the player (an enemy shape out-levelled the player's shape 0), or the land
   under the player has COL 0x10000 (`pwp->attr & 0x10000`, the HURT surface).
2. The character's mode machine reads it at the start of its next exec, e.g. SonicModeMachine (8C0A966C):
   * invincibility item (`pwp->item & 0x8000`): `flag |= 0x8000`, `wtimer = 0`, no damage;
   * `wtimer != 0` (invulnerable): `wtimer--`, `flag &= ~4` (the hit is dropped);
   * else the damage check (Sonic 8C0AFB10, E-102 8C0D04A0, the others' counterparts): `twp->id != 2`, flag 4 or the
     HURT surface -> `wtimer = 120`, pad vibration, voice / SE, knock-back mode (Sonic: mode 16, action 23,
     `spd = (-run_speed, jmp_y_spd)` in player space, i.e. backwards along the facing, not away from the enemy;
     special cases for the board modes 46..50 and 62..68; Super Sonic only halves `spd.x`), returns 1;
   * on 1: `flag |= 0x8000`, a held object is dropped, state flags cleared, then the **barrier** (`item & 3`: barrier
     1, magnetic barrier 2) is consumed, **or** `func_8C086F5E(pno)`: up to 20 rings scattered (tasks 8C0870EA, CCL
     id 7, collectable again), ring count to 0 (8C08A074), SE 0; **with 0 rings `func_8C07BBC2(0)`**: death request
     (smode 50, `item |= 0x4000`, task 8C07BA44), death SE 23 (0x4AD for E-102).
3. While `wtimer` counts down the body is drawn only when `wtimer & 2` is 0 (blinking, 8C0AE17A).

Ring count `B_8C752BE4` (Sint16; 8C08A198 adds and gives the 1-up per 100 into `B_8C752BE0`), score `B_8C752BF4`
(8C08A2A2 adds; 8C08A272 keeps a 120-step chain bonus capped at 500). Item bits: 1 barrier (display task 8C044060,
granted by 8C07D43E), 2 magnetic barrier (8C043C9E, 8C07D3DC), 0x8000 invincibility (8C044284, 8C07D4A0), 0x4000 dead.

### 1.6 How SA1 enemies are built

| Enemy (exec) | CCL id | Shapes (attr / flag atk-def / b05 / size) | Notes |
|---|---|---|---|
| Kiki, E_SARU (objexec_E_SARU 8C0FBE04) | 3 | sphere 0x80 / 0x21 (1-0) / 0x50 / r 4.5 | enemy work from 8C047040; dies on 8C0489E4; death 8C0FCB50: explosion 8C045736, debris 8C0486A0, sparks 8C0487B6, animal 8C015720 |
| Rhinotank, E_SAITO (created by 8C0F2C44) | 3 | body cylinder 0x81 / 0x21 (1-0) / 0x0F (solid) / r 8.5 h 8.5; horn sphere 0x80 / 0x26 (2-1) / 0x0F / r 4.5 | the horn hurts a rolling player (2 > 1) and needs atk 2 to break |
| Egg Keeper, E_ROBO (8C0F8E10) | 3 | sphere 0x80 / 0x21 / 0x12 / r 9; cylinder 0x91 (off) / 0x29 (1-2) / r 22 h 11 | second shape switched on for its attack |
| Unidus, E_UNI_A/B/C (8C1006D4 ...) | 3 | sphere 0x80 / 0x21 / 0x10 / r 4.5 | spikes 8C0FE568: id 4, sphere 0x80 / 0x26 (2-1) / r 3.5 |
| E-100 Zero (8C102EAE, exec 8C1032AC; C) | 3 | capsule 0x2083 / 0x20 (0-0) / 0x3F (solid) / r 8 b 10; hand sphere 0xB0 (off) / 0x2D (1-3) / 0xFF | knocked back, not killed, by `flag & 4` (Amy's hammer); damage accumulates in B_8C7A52D0 |
| Rival target (8C085532, exec 8C085660; C) | 2 | player-like sphere 0x80 / 0x00 / 0x10 | an E-102 lock-on target that follows another player: clears its own `0x800` every step (no homing target); when a laser shot (id 1) of its own marker hits it, it sets the followed player's `flag 4` and releases the marker (8C0D5D90). **The game's own precedent for a host-object proxy** |

Common parts:
* **Enemy work**: `func_8C047040(tp, twp)` CAllocs 0x23C bytes into `tp->mwp` (the task is created without a motion
  work), copies `twp->pos` to +0x50, sets defaults (+0xA0 .. +0xC4, +0x1A8 = 0x100, +0x1D4 = 0x38E3, +0x1D8/+0x1DC)
  and `flag(+0x4E) |= 0x480, &= 0xCFFF`. Fields read by shared code: +0x08 (0.7 after a no-bounce hit), +0x40 E-102's
  lock-on marker task, +0x44 E-102's saved `tp->dest`, +0x4E flags (0x1000 hit by a player / projectile, i.e.
  "killed"; 0x800 no-bounce hit; 0x400 cleared by a bounce), +0x1A0 squash speed (0.35 on a hit, spring 8C0488A2).
  `func_8C047040` itself is not lowered (fatal stub, `LIB/gen/policy/stubs.tsv`).
* **Damage check** `func_8C0489E4(twp, ewp)` (C, `code_8C0486A0.c`): if `twp->flag & 4`: squash, and if `hit_cwp->id`
  is 0 or 1: `flag |= 0x1000`, score +10 (8C08A2A2), then either the no-bounce marks (attacker shape attr 0x200) or
  **`func_8C048980(pno)`: if that player is in a ball (`flag 0x100`), airborne (`!(flag & 3)`) and is Sonic, Tails or
  Knuckles: `SetVelocityP(pno, 0, 2, 0)` (8C07C676, world speed 2 per step upward) and vibration (6, 30)**; returns 1.
  Without flag 4 it asks `func_8C05E5C2(&pos)` (the bomb item's sphere; a "ret 0" stub in libsa1). For a projectile
  the attacker's owner is not `playertwp[0]`, so pno is 1, `playertwp[1]` is NULL in a libsa1 world and nothing
  bounces, as in the game.
* **SET condition**: SET objects have `tp->ocp` (OBJ_CONDITION). When E-102 fires, `func_8C0D46CE` clears it on
  every locked target with 8C091B8A, and **if `target->ocp` is NULL it dereferences `target->ptp->ocp`**
  (`code_8C0D46CE.c`, case 2): a top-level task without ocp (`ptp` NULL) faults there. Proxies must carry an ocp.
* **Death**: enemies set a death mode and finally `func_8C091BA4` (exec = DestroyTask); DestroyTask (8C09865C) runs
  `tp->dest`, which E-102 may have hooked (1.7). Score is paid by the damage check; enemies drop no rings; animals
  (8C015720 -> 8C015820 with the level's table D_8C1623D0, lowered) and, for Knuckles, emerald shards (8C0C9BE2 /
  8C0C9C9A, not lowered) are the "drops".

### 1.7 Target consumers

| Consumer | Code | Targets from | Rules |
|---|---|---|---|
| Sonic's homing attack | 8C0AF18C (start, mode 8 -> 14, Super Sonic 77), 8C0AF280 (each step) | player target list (<= 100 units) and the partner player | aims at shape 0's centre (+ pos unless attr 0x20); nearest within +-0x5000 (112.5 degrees) of the facing (Super Sonic: any direction); turns 0x800 (0x1000) per step; speed 5 (10); mode 14 ends when Sonic's colliwk hit an id 0/2/3 object, after 360 steps, on the action button or on landing |
| Light Speed Attack | 8C0B11C0 / 8C0AF5A0 (mode 44, equipment 4) | player target list | atk 2-3, def 3 |
| Light speed dash | 8C0AF90A | id 7 objects with `twp->id 11` | rings only |
| Knuckles' Maximum Heat | 8C0C8F20 (mode 18) | player target list | front half, nearest; turns 0x1000 per step; atk 3 |
| Throw at target | Sonic 8C0B199A, Tails 8C0BD098, Knuckles 8C0C6A7A, E-102 8C0D11DC, Amy 8C0DC3C0, Big 8C0E5166 | player target list | aim the held object |
| Big | 8C0EB432 | player target list | "a target within 50 units" |
| Turn to target | 8C06D136 (every main) | player target list | |
| **E-102 lock-on** | 8C0D33A0 (laser state machine) -> search task 8C0D38C6 (level 6) -> 8C0D37F4 / 8C0D36C4 | **the CCL lists of ids 2 and 3 of the current step** | shape 0's centre within the laser's reach (250 units, `laser_len2` 62500), within 30 degrees (0x1555) up to 50 units, beyond that 12 degrees (0x888) and at most 60 units from the laser line; the owner task needs an `mwp`, no marker yet (`mwp+0x40`), shape 0's defence < 3 and `mwp` flag 0x1000 clear; one new lock every 8 steps, at most 32; every listed task's `dest` is hooked (8C0D332C saves it at `mwp+0x44` and installs 8C0D32C0) |

E-102 after a lock: a marker task 8C0D577C (level 5) follows the target's shape 0 centre and draws the lock-on
(captured by the render API). On release the arm (8C0D46CE) fires shot tasks 8C0D3DA0 (id 1) one at a time at the
nearest locked target; a shot homes on its marker at 5 units per step and snaps to it within 16 units; it out-levels
the target (atk 3). The target's death destroys its task -> the hooked dest 8C0D32C0 releases the marker (8C0D5D90),
which releases the shot (impact sphere 8C0D65BA) and, when the target was hit by a player (`mwp flag 0x1000`) and
counts (no 0x100000), grants the time bonus (8C0D5064).

### 1.8 Timing inside one step

`player exec (level 1) -> enemy execs (level 3: read the last pass, react, move, CCL_Entry) -> E-102's lock search
(level 6) -> CCL pass`. An enemy bounces the player in the step after the contact (after the player's exec of that
step), and the player is hurt in the step after the contact (its own exec reads flag 4). libsa1's executor walk
(8C0988A4, or the world's own `sa1g_run_tasks`) is the game's: list 7 as level 8, then levels 0..6.

---

## 2. Design: host objects in libsa1

### 2.1 Kinds

| Kind | colliwk id | Damageable (attr 0x80) | Hurts on touch (flag atk, 0x20) | Homing / throw / Max Heat target (0x800) | E-102 lock-on | Default solid (b05 0x01) |
|---|---|---|---|---|---|---|
| `SA1_HOSTOBJ_ENEMY` | 3 | yes | yes (touch level 1) | yes | yes, counted (time bonus) | yes |
| `SA1_HOSTOBJ_TARGET` | 3 | yes | no (touch level 0) | yes (`NO_HOMING` drops it) | yes, not counted (attr 0x100000 on shape 0) | no |
| `SA1_HOSTOBJ_BREAKABLE` | 4 | yes | no | no | no | no |
| `SA1_HOSTOBJ_HAZARD` | 4 | no | yes (touch level 2) | no | no | no |

"Solid" (pushing the character out) is CCL push, meant for NPC bodies the character should not walk through. Large
static or moving solids (vehicles, props) stay `sa1_collision_object_*` (GRID DynamicCOL, integration 5.5); a vehicle
can be both a moving collision object and a TARGET.

Levels: `armor` 0..3 is the defence level of every shape: 0 = any attack; 1 = needs level 2 (Light Speed Attack,
Knuckles' punch and Maximum Heat, E-102's laser and impact, Big's lure and attack, invincibility); 2 = only level 3;
3 = invulnerable and not lockable. `touch` 0..3 is the attack level against the character: 1 hurts unless the
character is in a ball or invulnerable; 2 hurts unless it spin-dashes, is in the Light Speed Attack or is invincible;
3: only invincibility and Super Sonic protect.

### 2.2 Public API (draft, API 0.3)

```c
/* ---- host objects (API 0.3 draft) ---------------------------------------------------- */
#define SA1_API_HOSTOBJ 1

#define SA1_HOSTOBJ_ENEMY      1u
#define SA1_HOSTOBJ_TARGET     2u
#define SA1_HOSTOBJ_BREAKABLE  3u
#define SA1_HOSTOBJ_HAZARD     4u

/* sa1_hostobj_desc.flags */
#define SA1_HOSTOBJ_SOLID        0x01u  /* pushes the character out (b05 0x01); default for ENEMY */
#define SA1_HOSTOBJ_NOT_SOLID    0x02u  /* overrides the kind's default */
#define SA1_HOSTOBJ_NO_HOMING    0x04u  /* never in the target lists (homing, LSA, throws, Max Heat, Big) */
#define SA1_HOSTOBJ_NO_LOCKON    0x08u  /* E-102 never locks it */
#define SA1_HOSTOBJ_NO_BONUS     0x10u  /* E-102 locks it without counting it (attr 0x100000); default for TARGET */
#define SA1_HOSTOBJ_DISABLED     0x20u  /* in the world but every shape off (no hits, no targeting) */
#define SA1_HOSTOBJ_REPORT_TOUCH 0x40u  /* SA1_HOSTOBJ_EVENT_TOUCH when the character starts touching it */

#define SA1_HOSTOBJ_MAX_SHAPES 4u
#define SA1_SHAPE_SPHERE   0u   /* size[0] radius */
#define SA1_SHAPE_CYLINDER 1u   /* size[0] radius, size[1] half height (along the host up axis) */
#define SA1_SHAPE_BOX      4u   /* size[] half extents (host axes before yaw), rotated by yaw about up */

/* sa1_hostobj_shape.flags */
#define SA1_HSHAPE_NO_DAMAGE 0x1u       /* this shape is not damageable (no attr 0x80) */
#define SA1_HSHAPE_NO_TOUCH  0x2u       /* this shape does not hurt (attack level 0) */
#define SA1_HSHAPE_OFF       0x4u       /* attr 0x10 */

typedef struct sa1_hostobj_shape {
    uint32_t form;              /* SA1_SHAPE_* */
    float    center[3];         /* offset from the object's position, host axes and units (not rotated) */
    float    size[3];           /* host units */
    float    yaw;               /* BOX: radians, the sa1_pose.yaw convention */
    uint32_t flags;             /* SA1_HSHAPE_* */
} sa1_hostobj_shape;

typedef struct sa1_hostobj_desc {
    uint32_t struct_size;
    uint32_t kind;              /* SA1_HOSTOBJ_* */
    uint32_t flags;             /* SA1_HOSTOBJ_* flags */
    float    position[3];       /* host space */
    float    yaw;
    int32_t  armor;             /* 0..3, -1 = the kind's default (0) */
    int32_t  touch;             /* 0..3, -1 = the kind's default */
    uint32_t cooldown_steps;    /* invulnerable after a hit (0 = 20 steps) */
    uint64_t user;              /* host tag, returned in events (G-SA1: the entity index) */
    uint32_t num_shapes;        /* 1..4; shape 0 is the aim point (homing, E-102) */
    sa1_hostobj_shape shapes[SA1_HOSTOBJ_MAX_SHAPES];
} sa1_hostobj_desc;

SA1_API sa1_status sa1_hostobj_add(sa1_context *ctx, const sa1_hostobj_desc *desc, uint32_t *out_id);
SA1_API sa1_status sa1_hostobj_update(sa1_context *ctx, uint32_t id, const sa1_hostobj_desc *desc);
SA1_API sa1_status sa1_hostobj_move(sa1_context *ctx, uint32_t id, const float position[3], float yaw);
#define SA1_HOSTOBJ_REMOVE_SILENT 0u    /* left or deleted: no "killed" bookkeeping */
#define SA1_HOSTOBJ_REMOVE_KILLED 1u    /* destroyed by the character: E-102's time bonus, marker release */
SA1_API sa1_status sa1_hostobj_remove(sa1_context *ctx, uint32_t id, uint32_t how);

typedef struct sa1_hostobj_config {
    uint32_t struct_size;
    uint32_t window;            /* proxies per world, 1..64 (default 64) */
    float    enter_radius;      /* host units from the character (default 280 SA1 units) */
    float    leave_radius;      /* default 320 SA1 units */
} sa1_hostobj_config;
SA1_API sa1_status sa1_hostobj_configure(sa1_context *ctx, const sa1_hostobj_config *cfg);

/* events */
#define SA1_HOSTOBJ_EVENT_HIT       1u  /* the character damaged the object */
#define SA1_HOSTOBJ_EVENT_LOCKED    2u  /* E-102 locked on (marker created) */
#define SA1_HOSTOBJ_EVENT_TARGETED  3u  /* E-102 fired a shot at it */
#define SA1_HOSTOBJ_EVENT_UNLOCKED  4u  /* the marker was released */
#define SA1_HOSTOBJ_EVENT_TOUCH     5u  /* REPORT_TOUCH: the character started touching it */

#define SA1_HITF_BOUNCED     0x1u       /* the game bounced the character (8C048980 applied the speed) */
#define SA1_HITF_PROJECTILE  0x2u       /* the attacker was a list-1 object (shot, lure, ...) */
#define SA1_HITF_LOCKED      0x4u       /* the object was locked by E-102 when hit */

typedef struct sa1_hostobj_event {
    sa1_character_id character;
    uint32_t type;              /* SA1_HOSTOBJ_EVENT_* */
    uint32_t object;            /* host object id */
    uint32_t attack;            /* HIT: SA1_ATTACK_* (2.6) */
    uint32_t level;             /* HIT: attack level that beat the armor (1..3) */
    uint32_t damage;            /* HIT: level - armor (>= 1), "SA1 damage units" */
    uint32_t shape;             /* HIT / TOUCH: the object's shape */
    uint32_t flags;             /* SA1_HITF_* */
    uint64_t user;
    float    position[3];       /* host space: the attacking shape's centre */
    float    direction[3];      /* unit, host axes: the attacker's velocity (zero at rest) */
    float    speed;             /* host units per second */
    uint32_t step;
} sa1_hostobj_event;
SA1_API sa1_status sa1_poll_hostobj_events(sa1_context *ctx, sa1_hostobj_event *out, uint32_t capacity,
                                           uint32_t *count);

/* the character side */
#define SA1_EVENT_HURT 14           /* sa1_event: the character was hit (2.5) */
#define SA1_HURT_BARRIER   0x01u    /* a real barrier absorbed it */
#define SA1_HURT_ABSORBED  0x02u    /* an option absorbed it (no death / keep rings) */
#define SA1_HURT_FATAL     0x04u    /* no rings: the death sequence started */
#define SA1_HURT_HOST      0x08u    /* from sa1_character_damage */
#define SA1_HURT_SURFACE   0x10u    /* the HURT surface */
#define SA1_HURT_IGNORED   0x20u    /* a host request during invulnerability (nothing happened) */

#define SA1_DAMAGE_FACE_SOURCE 0x1u /* turn the character to the source first: knocked back away from it */
#define SA1_DAMAGE_KILL        0x2u /* the game's death request (8C07BBC2), regardless of rings */
typedef struct sa1_damage {
    uint32_t struct_size;
    uint32_t flags;             /* SA1_DAMAGE_* */
    float    source[3];         /* host space */
    uint32_t object;            /* host object credited in the HURT event (0 = none) */
} sa1_damage;
SA1_API sa1_status sa1_character_damage(sa1_context *ctx, sa1_character_id id, const sa1_damage *d);

#define SA1_COPT_NO_DEATH    1u     /* a hit with 0 rings knocks back without killing */
#define SA1_COPT_KEEP_RINGS  2u     /* hits knock back without scattering rings */
#define SA1_COPT_NO_DAMAGE   3u     /* hits are ignored entirely */
SA1_API sa1_status sa1_character_set_option(sa1_context *ctx, sa1_character_id id, uint32_t option,
                                            int32_t value);
SA1_API sa1_status sa1_character_add_rings(sa1_context *ctx, sa1_character_id id, int32_t n);

#define SA1_CAPACITY_HOST_OBJECTS 5u    /* SA1_EVENT_CAPACITY: a proxy could not be created (pool / heap) */
```

`sa1_state_ex` grows at its end (struct_size keeps older hosts working): `int32_t rings` (B_8C752BE4), `int32_t lives`
(B_8C752BE0), `uint32_t score` (B_8C752BF4), `uint32_t invulnerable_steps` (`taskwk.wtimer`), `uint32_t hurt_steps`
(steps since the last HURT, saturating).

`SA1_EVENT_HURT` packs into the existing `sa1_event`: `id` = the credited host object (0 = surface, a host request
without an object, or a game object), `arg` = rings lost (bits 0-15) | `SA1_HURT_*` << 16, `bank` = rings left,
`position` = the character.

Registry limits: 1024 host objects per context (`SA1_ERR_LIMIT`), 1..4 shapes, sizes up to 1000 SA1 units, finite
positions. Objects are context-wide (like moving collision objects): every ticking world mirrors them.

### 2.3 Per-world layout

When a world is attached (boot, or the first host object), libsa1 allocates in the world's extension segment (0x18)
one `HOSTOBJ` block: a header (magic `SAHO`, version, window size, event queue head) and 64 slots of 0x140 bytes:

| Slot offset | Content |
|---|---|
| +0x00 | host object id (0 = free), generation |
| +0x08 | the proxy task (guest address) and the slot state (FREE, LIVE, LEAVING) |
| +0x10 | **OBJ_CONDITION stand-in** (0x10 bytes, `ssCondition = 1`): `tp->ocp` (1.6: E-102's firing code needs it) |
| +0x20 | latched pose: previous and next position (SA1 space), previous and next yaw (BAMS) |
| +0x40 | CCL_INFO template, 4 x 0x34 (the source of CCL_Init and of later shape changes) |
| +0x110 | cooldown, armor, touch, flags, last marker, shot seen, touching, hit snapshot (attack, level, shape, attacker colliwk and owner task and exec, position, direction, speed) |

The block is part of the world's guest memory, so RAM dumps and transplants carry it; nothing points into it except
through guest addresses (`tp->ocp`).

**The proxy task** (created at the start of a pair, inside the world, in ascending object id):

1. `tp = CreateElementalTask(2, 3, 0x7F000100)` (8C09852E): a taskwk only, **level 3** like SET objects (after the
   character at level 1, before E-102's search task at level 6); exec = the host function `hostobj_exec`; no disp
   (the host draws its own entities); no dest.
2. `tp->mwp = CAlloc(1, 0x23C)` (8C098B0C): the enemy work, set up as 8C047040 sets the fields shared code reads
   (`+0x4E = 0x0480`, `+0x50 = pos`, the rest zero). DestroyTask frees it.
3. `tp->ocp` = the slot's stand-in. `twp->id = 0` (no liftable class for 8C06C880), `twp->flag = 0` (never 0x40), no
   `twp->id 11`; `twp->counter.w[0]` = slot index, `twp->counter.w[1] = 0x484F` ('HO').
4. `CCL_Init(tp, slot.template, id, 1, n)` (8C02F4B0) with id 3 (ENEMY, TARGET) or 4 (BREAKABLE, HAZARD).
   **Heap headroom is checked first** (2.8): CCL_Init loops forever when CAlloc fails.

Shape template words (`form` from the shape):

| | ENEMY | TARGET | BREAKABLE | HAZARD |
|---|---|---|---|---|
| attr | `form \| 0x2000 \| 0x80 \| 0xE0000` | same, `\| 0x100000` on shape 0 | same as ENEMY | `form \| 0x2000 \| 0xE0000` |
| flag | `touch \| armor << 2 \| 0x20` (default 0x21, Kiki's) | `armor << 2` | `armor << 2` | `touch \| 3 << 2 \| 0x20` (default 0x2E) |
| b05 | `0xF0 \| SOLID` (0xF1) | 0xF0 | 0xF0 | 0xF0 |

`0x2000` keeps the centre an unrotated offset from `twp->pos` (the form every consumer handles); `0xE0000` makes the
pair filter skip proxy x proxy and proxy x game-object pairs (ids 2-7), so only characters (id 0) and their projectiles
(id 1) interact with proxies; `0xF0` means CCL never pushes a proxy (the host owns its position). A BOX keeps its yaw in
`angy` (with 0x2000 the owner's `ang.y` is not added, 8C03431C). `SA1_HSHAPE_*` clear 0x80 / the attack level or set
0x10 per shape. Sizes and offsets are converted to SA1 space with the context's `sa1_coords` (radii and half heights
divided by the scale, offsets through the axis matrix; the host up axis is SA1's Y in the Source preset).

**Code address.** `0x7F000100` (`addr & 0x1FFFFFFF` = 0x1F000100: SH-4 area 7, never game code) is served by a
synthetic code-map image `LIBSA1_HOST` (lo 0x1F000000, hi 0x1F010000) that the world lists in `fn_images` like an
overlay image (integration 2.5): its lookup returns `{0x7F000100, signature of v:p (0xDA9162B5 in the current map),
hostobj_exec, "hostobj_exec"}`. The game's executor (8C0988A4), DestroyTask's dest calls and the replay walk all reach
it through `sa1_fn` unchanged. No xhook is used (xhooks belong to the replay executor, 16 at most). No SADC change.

### 2.4 One step

| When | What |
|---|---|
| Pair start, sub 0, before the executor (`hostobj_pre`) | latch the host registry; window: the objects within `enter_radius` of the character (kept until `leave_radius`), nearest first by (squared distance, id), at most `window`; FreeTask (8C098786) the proxies of objects that left (SILENT: clear `mwp` flag 0x1000 first) or were removed (KILLED: set 0x1000); create proxies for objects that entered (2.3); write the latched poses; apply shape / flag changes to the templates and mark the proxies dirty |
| Every step, before the executor | character damage requests and options (2.5); hurt attribution |
| Executor, level 3 | `hostobj_exec` of every proxy (below) |
| After the CCL pass (`hostobj_post`, before the collision adapter) | snapshot every proxy hit of this pass (attacker colliwk, its owner task and exec, its shape index, attr and attack level, the attacker's centre via 8C02FC12 and `mwp->spd`) and the character's state (mode, `flag`, `item`, `equipment`, shape levels); detect the character's hurt (2.5); push events |

`hostobj_exec(tp)` (host code, inside the world; game functions are called through the code map as the collision
adapter does):

```
slot = slot_of(tp)                  /* twp->counter.w[0], checked against the slot's tp */
twp = tp->twp; cwp = twp->cwp; ewp = tp->mwp
/* 1. the last pass (CCL_Entry has not reset it yet) */
if slot.cooldown and --slot.cooldown == 0:
    restore the armor levels and the homing flag; clear ewp+0x4E 0x1000 (unless leaving)
if (twp->flag & 4) and not slot.cooldown:
    if the snapshot's attacker is still valid (same colliwk, owner task alive with the same exec):
        func_8C0489E4(twp, ewp)     /* the game's damage check: squash, 0x1000, score +10, bounce 8C048980 */
    else:                           /* attacker gone since the pass: the same effects without dereferencing it */
        ewp+0x1A0 = 0.35; ewp+0x4E |= 0x1000; 8C08A2A2(10); 8C048980(pno of the snapshot's owner)
    classify (2.6) from the snapshot -> attack, level; damage = level - armor
    push HIT {object, attack, level, damage, shape = cwp->my_num, BOUNCED if the character was ball + airborne, ...}
    if ewp+0x40 (E-102 marker) and the attacker is E-102's shot: func_8C0D5D90(tp)   /* what the death would do */
    slot.cooldown = cooldown_steps; every shape's defence = 3; cwp->flag &= ~0x800; ewp+0x4E |= 0x1000
elif REPORT_TOUCH and (cwp->flag & 2) and cwp->hit_cwp->id == 0 and not slot.touching:
    push TOUCH
/* 2. E-102 */
m = ewp+0x40: LOCKED when it appears; TARGETED when its awp+0x14 (the shot task) first becomes non-zero;
              UNLOCKED when it goes away
/* 3. pose of this step: sub 0 = midpoint of the pair's move, sub 1 = its end (as moving objects, integration 5.5) */
twp->pos, twp->ang.y; dirty shapes: copy the template geometry into cwp->info[i] (hit fields kept), 8C02F400(cwp)
NO_HOMING or cooldown: cwp->flag &= ~0x800 (CCL_Entry keeps 0x800; 8C085660 does the same)
NO_LOCKON: ewp+0x4E |= 0x1000 and shape 0 attr |= 0x100000 (8C0D37F4 skips it; no bonus at destruction)
DISABLED: every shape attr |= 0x10
/* 4. this step's lists */
CCL_Entry(twp)                      /* 8C02F63C */
```

The bounce happens in the step after the contact, after the character's exec, exactly where an SA1 enemy's exec
applies it. Sonic's homing attack ends by itself on the contact (mode 14 sees `hit_cwp` id 3) and the bounce sends
him up; during the cooldown the proxy is not in the target list, so jumping again in the air homes onto the **next**
proxy: homing chains need no special code. A proxy that survives (the host did not remove it) is a target again after
the cooldown, like an SA1 boss. E-102's marker is released at the hit (the game releases it at the target's death one
step later), so a shot never stays glued to a surviving target.

Removal: `sa1_hostobj_remove` takes effect at the next pair start: KILLED sets `ewp+0x4E |= 0x1000`, SILENT clears it,
then FreeTask. DestroyTask runs the dest that E-102 may have hooked (8C0D32C0: marker release; the time bonus only for
KILLED, counted objects), frees the taskwk, colliwk, CCL table and enemy work, and clears the stand-in's bit 0.

### 2.5 Character damage and options

* **Host damage** (`sa1_character_damage`): queued, applied before the executor of the next step: unless
  `SA1_COPT_NO_DAMAGE`, `twp->flag |= 4` on the character (exactly what an enemy contact leaves after a pass); with
  FACE_SOURCE, `twp->ang.y` and `mwp->ang_aim.y` are first turned to the source (the game knocks back along the
  facing); with KILL, `func_8C07BBC2(0)` instead. The game's own path does the rest (invulnerability, voice, barrier,
  ring scatter, death). During invulnerability the request does nothing and HURT carries `SA1_HURT_IGNORED`.
* **Attribution**: before the executor, if the character's `twp->flag & 4` is set and its `colliwk.hit_cwp` belongs to
  a proxy, that object is the source; a host request names its own; otherwise the HURT surface
  (`pwp->attr & 0x10000`) or a game object.
* **Detection** (`hostobj_post`): HURT when `wtimer` became 120 in this step (the damage checks set exactly 120);
  rings lost = rings before the executor - rings now; FATAL when the death request (smode 50 / `item 0x4000`)
  appeared; BARRIER when `item & 3` was consumed.
* **Options**, without touching game code: `NO_DEATH` / `KEEP_RINGS` put a *phantom barrier* (`pwp->item |= 1`
  without creating the barrier display task 8C044060) before the executor when a hit is pending (flag 4 or the HURT
  surface, `wtimer == 0`, no invincibility, no real barrier) and either the rings are 0 (NO_DEATH) or always
  (KEEP_RINGS). The game consumes it like a barrier (knock-back, invulnerability, no rings lost, no death); if it was
  not consumed (special modes), it is removed after the step. `NO_DAMAGE` keeps `wtimer` at least 1 before the executor
  (the mode machine then drops the hit; `wtimer & 2` stays 0, so no blinking). Drowning, kill planes and crushing still
  kill.
* `sa1_character_add_rings` calls 8C08A198 inside the world (the 1-up and its jingle included).

### 2.6 Attack kinds

Classified from the snapshot taken right after the pass (the character changes state in its next exec: Sonic's homing
turns into a jump on the contact).

| SA1_ATTACK_* | = | Rule (first match) | Level |
|---|---|---|---|
| LASER | 11 | attacker id 1, owner exec 8C0D3DA0 (E-102's shot) | 3 |
| LASER_BLAST | 12 | id 1, exec 8C0D65BA (E-102's impact sphere) | 2 |
| LURE | 13 | id 1, exec 8C0E943C (Big's lure) | 2 |
| SPARKLE | 15 | id 1, exec 8C0B5994 (Sonic's equipment-4 sparkles) | 1 |
| OTHER | 0 | any other id 1 | |
| INVINCIBLE | 16 | id 0, `item & 0x8000` | 3 |
| SUPER | 17 | id 0, `equipment & 0x8000` (Super Sonic) | 3 |
| TAIL | 5 | Tails, shape 1 or 2 (mode 20) | 1 |
| TAILS_FLIGHT | 6 | Tails, shape 3 (mode 15) | 1 |
| PUNCH | 7 | Knuckles, shape 1 | 3 |
| DIG | 9 | Knuckles, shape 2 or 3 (action 51) | 1 |
| HAMMER | 10 | Amy, shape 1 | 1 |
| BIG_ATTACK | 14 | Big, shape 1 (`twp->flag 0x400`; which move: to verify) | 2 |
| HOMING | 3 | Sonic, mode 14 or 77 | 1 |
| LIGHT_SPEED_ATTACK | 4 | Sonic, mode 44 | 2-3 |
| SPIN_DASH | 2 | Sonic, mode 6 | 1 |
| MAX_HEAT | 8 | Knuckles, mode 18 | 3 |
| SPIN | 1 | `twp->flag 0x100` (jump, roll; Knuckles' glide if it sets 0x100: to verify) | 1 |
| OTHER | 0 | anything else that out-levelled the proxy | |

`level` is the attacker shape's attack level at the pass; `damage` = level - armor (at least 1).

### 2.7 Limits and cost

| Limit | Value | Source |
|---|---|---|
| Host objects per context | 1024 | registry |
| Proxies per world | 64 (window) | slot block; task pool 4096; heap 576 KB at about 1 KB per proxy (task, taskwk 0x40, enemy work 0x23C, colliwk 0x1C, CCL table n x 0x34, allocation headers) |
| CCL lists 3 / 4 | 256 / 512 per step | shared with the world's own objects (E-100 Zero; scattered rings are id 7) |
| Target lists | 1160 | B_8C747708 |
| E-102 | 32 locks per laser, one per 8 steps | 8C0D38C6 |
| Event queue | 256 per world, overflow counted | |
| Window radius | 280 / 320 SA1 units | E-102 reaches 250, homing 100 |

Cost per step with 64 proxies: 64 execs (well under a microsecond each), the character x 64 shape tests, and
64 x 63 / 2 bounding tests in the id 3 x 3 loop (the 0xE0000 bits stop them before any shape test): tens of
microseconds next to the ~50 microsecond tick.

### 2.8 Multi-world, determinism, safety

* **Worlds**: one per ticking character (integration 6.8). Every world mirrors the context's host objects in its own
  window and reports its own events (`character`). A hit in one world does not change another world's proxy; the host
  decides (G-SA1 removes the object when the NPC dies, which reaches every world).
* **Determinism**: host objects are inputs, latched per pair; the host-space to SA1-space conversion happens inside the
  world (its FP mode, MXCSR 0xFFC0) at the latch; the window order is (squared distance, id); slots are the lowest free
  ones; creation is in ascending id. The layer calls no `rand()` and reads no host time. A replay that feeds the same
  per-pair object stream reproduces bit for bit (traces v2 get an optional object-stream blob).
* **Threading**: API calls take the context's tick lock, as `sa1_collision_object_*` do.
* **Safety**:
  * CCL_Init spins forever (`for (;;) njPrintC("CANT GET COLLI_WORK!!!!")`) when CAlloc fails. The layer refuses to
    create a proxy unless the heap has 4 KB free (a native walk of the heap's free list), and SADC should add
    `PC_ONLY(sa1g_fatal(...))` in that loop, as for MFree's double free (request, 4.1).
  * CreateElementalTask can return 0 (pool full): the slot stays empty, `SA1_EVENT_CAPACITY` with
    `SA1_CAPACITY_HOST_OBJECTS`.
  * Every pointer the exec follows is validated natively (slot <-> task <-> taskwk <-> colliwk); a proxy whose task was
    destroyed by someone else (nothing in the game does that today) is recreated at the next pair.
  * Proxies are top level (`ptp` NULL) but always have an `ocp`, so 8C0D46CE never dereferences NULL.
  * `twp->id = 0` and no `flag 0x40`: the lift / push classification (8C06C880) ignores proxies; no `twp->id 11`: not
    a light speed dash target.

---

## 3. G-SA1 mapping

G-SA1's module is client side (`gmcl_gsa1`): the world runs on the owner's client, damage to NPCs must happen on the
server, and the server must forward damage to the owner. Everything below fits the existing owner-authoritative model
(`sh_net.lua`): the server already keeps the invisible, noclipped player entity at the character's position, so NPCs
aim at it and bullets hit it there.

### 3.1 Entities to host objects (client, new `cl_hostobj.lua`)

Scan every 0.1 s with `ents.FindInSphere(character, 2400)` (600 SA1 units); register what is new, `ObjMove` the
registered ones once per pair (30 Hz), remove what is beyond 2800 or became invalid (`EntityRemoved`).

| GMod entity | Kind | Shapes (from `OBBMins` / `OBBMaxs`, world axes) | Notes |
|---|---|---|---|
| NPC (`IsNPC`), NextBot (`IsNextBot`), hostile or neutral | ENEMY (touch 1, solid) | cylinder: radius 0.4 x (dx + dy) / 2, half height dz / 2, centre `OBBCenter`; flying NPCs (helicopter, gunship, manhack, scanner) a sphere | relation from the server (3.3); until then by class (friendly list: citizens, Alyx, Barney, vortigaunts, Kleiner, Eli, Mossman, dog, monk) |
| friendly NPC | none, or TARGET with "Friendly NPCs are targets" | | |
| other player (`IsPlayer`), PvP on | ENEMY with touch 0 (their own character attacks them in their own world) | cylinder from the player hull | server ConVar |
| vehicle (`IsVehicle`, `prop_vehicle_*`, LVS / simfphys bases by class prefix) | TARGET (NO_BONUS, not solid) | box (half extents, yaw) | homing by option; should also become a moving collision object (G-SA1 "not done yet") |
| breakable (`func_breakable`, `func_breakable_surf`, `func_physbox`, `prop_physics*` with the server's `gsa1_hp` NW2 tag) | BREAKABLE (TARGET with "E-102 targets props") | box | |
| hazard (`env_fire`, `entityflame`, `npc_grenade_frag`, `grenade_ar2`, `rpg_missile`, `prop_combine_ball`, `crossbow_bolt`) | HAZARD (touch 2) | sphere from the OBB | most of them also hurt the player entity through GMod damage (3.3); the invulnerability absorbs the duplicate |

`user` = entity index. Sizes are clamped to 4..1000 SA1 units.

### 3.2 Hits: client -> server

On `SA1_HOSTOBJ_EVENT_HIT`: `net.Start("gsa1_hit")` {UInt(13) entindex, UInt(5) attack, UInt(2) level, UInt(2)
damage units, Vector position, Normal direction, Float speed}, at most 30 per second.

Server (`sv_gsa1.lua`), per message:
* sender active (`ply.GSA1Active`), entity valid and not the sender, `hook.Run("GSA1CanDamage", ply, ent, attack)`
  not false (gamemodes can veto), the PvP ConVar for players, relation not friendly unless allowed;
* distance from `ply:GetPos()` (the character) to `ent:NearestPoint(position)` at most the attack's reach (melee 240
  HU, LURE 800, LASER and LASER_BLAST 1100) plus 64 HU plus `ply:Ping()` x 2 HU (a fast character's latency), and the
  hit position within 200 HU of the entity;
* at most one hit per (player, entity) per 0.15 s;
* `DamageInfo`: attacker = ply, inflictor = ply, damage from the table below x `gsa1_sv_damage_scale` (or
  `ent:Health() + 1` with "one-hit kills"), type, position, force = direction x damage x 300 (ragdolls fly like SA1's
  enemies), reported position = ply; `ent:TakeDamageInfo(d)`; vehicles also get
  `phys:ApplyForceOffset(direction x mass x 200, position)`.

| Attack | GMod damage type | Default damage |
|---|---|---|
| SPIN, HOMING, TAIL, TAILS_FLIGHT, DIG | DMG_CLUB | 50 (HOMING 60) |
| SPIN_DASH | DMG_CLUB | 75 |
| LIGHT_SPEED_ATTACK, MAX_HEAT | DMG_CLUB \| DMG_SHOCK | 150 |
| PUNCH, HAMMER | DMG_CLUB | 90 |
| BIG_ATTACK | DMG_CLUB | 80 |
| LURE | DMG_CLUB | 50 |
| LASER | DMG_ENERGYBEAM | 80 |
| LASER_BLAST | DMG_BLAST | 30 |
| SPARKLE | DMG_SHOCK | 20 |
| INVINCIBLE, SUPER | DMG_CLUB \| DMG_SHOCK | 200 |
| OTHER | DMG_CLUB | 40 |

`OnNPCKilled(npc, attacker)` with an active G-SA1 attacker sends `gsa1_kill` {entindex} to that player: the client
removes the object with `REMOVE_KILLED` (E-102's time bonus, marker release); every other removal is SILENT.

### 3.3 Damage taken: server -> client

`EntityTakeDamage(target, dmg)` for an active G-SA1 player, by `gsa1_sv_damage_mode`: 0 **rings** (default): forward
and block (`return true`); 1 **health**: let GMod apply it, no forward; 2 **both**. Not forwarded: DMG_FALL (already
0), DMG_DROWN / DMG_DROWNRECOVER (the game has its own drowning), damage below `gsa1_sv_min_damage` (1).
`gsa1_hurt` {Float amount, UInt(32) type, Vector source (`dmg:GetDamagePosition()`, else the attacker's
`WorldSpaceCenter`), UInt(13) attacker entindex, Bool kill (damage >= `gsa1_sv_kill_damage`, default 1000, e.g. a
`trigger_hurt` pit)}, at most 10 per second per player. The client calls `gsa1.Damage(handle, flags, source, the
attacker's object id if registered)`, with FACE_SOURCE under "Knock back away from the attacker". The game decides the
rest (the invulnerability absorbs a stream of bullets: one ring loss per 2 seconds).

Death: `SA1_EVENT_DEATH` keeps G-SA1's respawn; with `gsa1_sv_kill_on_death` the client sends `gsa1_dead` and the
server kills the player (off by default).

Relations: every 0.5 s the server sends each active player `gsa1_rel` {count, (entindex, relation in 2 bits)} for the
NPCs within 3000 HU whose `npc:Disposition(ply)` changed (D_HT / D_FR hostile, D_NU neutral, D_LI friendly); NextBots
stay hostile unless their class is on the friendly list.

### 3.4 HUD, sounds, effects

* Ring counter (HUDPaint, top left like SA1: ring icon and digits, flashing red at 0) from `GetState().rings`; options
  for score and lives. With damage mode "rings" the GMod health HUD is hidden while active (`HUDShouldDraw`).
* Ring loss, hurt voices, the scattered rings (drawn by the world, collectable again), E-102's markers, beams and shots
  are the game's own (events, mixer, render API): nothing to add.
* On HIT the client may play the SA1 enemy-hit sound through the libsa1 mixer (`sa1_audio_mixer_event`; the id is to
  be taken from the enemies' death code, e.g. Kiki 8C0FDBA4 / 8C0FDA80) and an explosion effect when the kill notice
  comes back.

### 3.5 Options (Q menu: Options -> G-SA1 -> "Interaction"; the console as an extra)

| Option | ConVar | Default |
|---|---|---|
| Interaction with the world (master switch) | `gsa1_interact` | 1 |
| Enemies hurt on touch | `gsa1_touch_hurt` | 1 |
| The character can die (off: a hit at 0 rings only knocks back) | `gsa1_can_die` | 1 |
| Lose rings when hit | `gsa1_lose_rings` | 1 |
| Knock back away from the attacker | `gsa1_knockback_from_source` | 0 |
| Solid NPCs | `gsa1_solid_npcs` | 1 |
| Homing attack targets vehicles | `gsa1_homing_vehicles` | 1 |
| E-102 targets vehicles | `gsa1_lock_vehicles` | 1 |
| Props as targets (off / breakables / all) | `gsa1_target_props` | 1 |
| Friendly NPCs are targets | `gsa1_target_friendly` | 0 |
| Ring counter / score | `gsa1_hud_rings`, `gsa1_hud_score` | 1, 0 |
| Objects near the character (16..64) | `gsa1_obj_window` | 64 |
| Debug: draw the object shapes | `gsa1_obj_debug` | 0 |
| Server: characters damage NPCs | `gsa1_sv_damage` | 1 |
| Server: damage scale | `gsa1_sv_damage_scale` | 1 |
| Server: one-hit kills (SA1 style) | `gsa1_sv_onehit` | 0 |
| Server: damage mode (rings / health / both) | `gsa1_sv_damage_mode` | 0 |
| Server: PvP | `gsa1_sv_pvp` | 0 |
| Server: the character's death kills the player | `gsa1_sv_kill_on_death` | 0 |

Server options sit in an admin section of the same page (in single player the player is the admin); changes go
through `RunConsoleCommand`.

### 3.6 Module additions (`gsa1.*`)

`ObjAdd(kind, flags, user, px, py, pz, yaw, armor, touch, cooldown, nshapes, form, cx, cy, cz, sx, sy, sz, syaw, ...)`
-> id; `ObjMove(id, px, py, pz, yaw)`; `ObjUpdate(...)`; `ObjRemove(id, killed)`; `ObjEvents()` -> array of tables;
`ObjConfig(window)`; `Damage(handle, flags, sx, sy, sz, object)`; `SetCharOption(handle, option, value)`;
`AddRings(handle, n)`; `GetState` gains `rings`, `lives`, `score`, `invuln`. Flat arguments and no tables on the hot
path (the render path's rule).

---

## 4. Implementation plan

### 4.1 Game code: decompile, verify, fix

Nothing has to be decompiled before the layer works. CCL (8C02E820-8C0344A0), the enemy damage check and bounce
(8C0489E4, 8C048980), score (8C08A2A2), rings (8C08A198, 8C086F5E, 8C0870EA, 8C08A074), death (8C07BBC2, 8C07BA44), the
characters' damage checks, homing (8C0AF18C, 8C0AF280), Light Speed Attack (8C0AF5A0), Maximum Heat (8C0C8F20), E-102's
lock-on, markers, shots and bonus (8C0D32A0-8C0D5E26, 8C0D62E0-8C0D67C0), the animals (8C015720, 8C015820) and the
task / heap functions are all lowered.

To verify with new reference traces before release (untraced or NON_MATCHING paths): the hurt paths of every character
(8C0AFB10, 8C0BBA4E, 8C0C5680, 8C0D04A0, 8C0DAC46, 8C0E3EC0 and the barrier / ring branches of their mode machines),
8C086F5E and 8C0870EA (NON_MATCHING; the ring scatter with rings and with 0 rings), 8C02F8A0 / 8C030634 / 8C030540
(NON_MATCHING shape tests), 8C0AF280 (NON_MATCHING homing step) and E-102's lock-on chain (never reached by
`gamma_fe1`). Integration section 11 already lists "damage with 0 rings" for Phase 7.

Requests to the SADC tree (another workflow owns it right now):
1. `code_8C02F27C.c` CCL_Init: `PC_ONLY(sa1g_fatal(0x8C02F4B0, "CCL_Init: CAlloc failed"))` inside the `for (;;)`.
2. Optional decompiles: 8C05E5C2 (80 B, the bomb item's sphere; now a "ret 0" stub); 8C047040 (230 B, the enemy work
   initialiser; now a fatal stub: with it the proxy would use the game's own initialiser); 8C045736, 8C048528,
   8C048624 (the explosion sprite and the debris / spark tasks of an SA1 enemy's death: a "game-style destruction"
   option).
3. A harness scenario feature: a native test exec at 0x7F000100 (the harness already routes native functions by
   address), so that the oracle runs the same proxy (4.3).

### 4.2 libsa1 phases

| Phase | Work | Files | Exit |
|---|---|---|---|
| H1 | registry, descriptors, conversion, API, event queue, `sa1_state_ex` additions | `include/libsa1.h`, `src/sa1_hostobj.c` | API unit tests: limits, conversion, struct_size handling |
| H2 | world side: slot block, the `LIBSA1_HOST` code image, proxy creation / destruction, window, exec, pre / post hooks in `sa1g_step_game` | `src/world/sa1_world_hostobj.c`, `sa1_world_tick.c`, `sa1_world.h` | `tests/hostobj_test.c` cases 1-5 below; the 10 reference traces still bit-exact with no objects |
| H3 | character damage, options, rings, HURT attribution | same | cases 2, 7 |
| H4 | E-102 events, classification table, cooldown, TOUCH | same | case 6 |
| H5 | oracle traces and replays (4.3), x86 / x64, multi-world | `tests/build_replay.py`, harness scenarios | replays bit-exact; cases 8-10 |

`tests/hostobj_test.c` (synthetic proxies on a flat GRID floor, SA1 space, scripted pads; `tests/build_hostobj.py
--run` with `SA1_TEST_GDI`):

1. **Jump on an enemy**: Sonic and an ENEMY sphere r 4.5 (Kiki's numbers) 30 units ahead; run and jump onto it.
   Expect one HIT (SPIN, level 1, damage 1, BOUNCED), Sonic's `mwp->spd.y == 2.0` after the next step (SetVelocityP),
   score +10, the proxy in cooldown and absent from `B_8C73EE38` for 20 steps.
2. **Touch an enemy**: rings set to 5 (`add_rings`), walk into the same proxy. Expect HURT (rings lost 5, source = the
   object), `wtimer == 120`, Sonic in mode 16, 5 ring tasks (8C0870EA) at level 2; walking on, rings come back (ring
   SE events); a second touch within 120 steps does nothing; with 0 rings the next touch gives HURT FATAL and
   `SA1_EVENT_DEATH`; with `SA1_COPT_NO_DEATH` the same touch gives HURT ABSORBED and no death.
3. **Rolling through**: a spin dash into a touch-1 ENEMY: HIT SPIN_DASH and no HURT; a touch-2 HAZARD hurts a jumping
   Sonic but not a spin-dashing one.
4. **Armor**: an armor-1 ENEMY survives a jump (no HIT) and Knuckles' punch hits it (PUNCH, level 3, damage 2).
5. **Homing chain**: three ENEMYs on a line 40 units apart, 10 units up; jump, then press jump in the air three times.
   Expect three HITs (HOMING) in order, each followed by a bounce, and no HURT.
6. **E-102 lock-on**: E-102 and three ENEMYs within 200 units in front; hold the laser button 60 steps, release.
   Expect three LOCKED, three TARGETED and three HITs (LASER, level 3, PROJECTILE); `REMOVE_KILLED` on all three gives
   three time-bonus calls (the bonus task 8C0D520A exists) and no fault; TARGET (NO_BONUS) objects give no bonus.
7. **Host damage**: `sa1_character_damage` with FACE_SOURCE from behind: Sonic turns to the source and is knocked away
   from it; a second request during the invulnerability gives HURT IGNORED.
8. **Window and churn**: 1024 objects on a grid, Sonic running through: at most 64 proxies, no CAPACITY event, the
   heap's free space back to its starting value once all are gone; removing a locked object releases its marker.
9. **Two worlds**: Sonic and Knuckles worlds with the same objects: hits are reported per character, and each world's
   results equal a run of that world alone.
10. **Determinism**: cases 5 and 6 on x86 and x64 and twice in a row: identical event streams and world RAM.

### 4.3 Oracle against the original game

The SH-4 harness runs the original code. New scenarios (SADC harness, coordinated with its owner) build the same proxy
in the harness world (the same task, enemy work, stand-in ocp and CCL table, and the native test exec at 0x7F000100
doing steps 1-4 of 2.4, with the game functions it calls run by the interpreter), with the scripts of cases 1, 2, 5
and 6, recorded as v2 traces with an object-stream blob. libsa1 replays them strictly bit-exact (player state, rings,
score, E-102's work). Separately, one trace with a real SA1 enemy (an Unidus or a Kiki from Emerald Coast act 1's SET
list) checks the player-side reactions against a real enemy up to the kill (player state only; the enemy itself is
not replayed).

### 4.4 G-SA1 phases

| Phase | Work | Tests |
|---|---|---|
| G1 | module bindings (3.6) | `binding_test`: every new function through the mock Lua stack |
| G2 | `cl_hostobj.lua` (scan, classification, shapes, moves, events to the server), `sv_gsa1.lua` (hit validation, DamageInfo, kill notices, the EntityTakeDamage forward, relations), network strings | `tools/lua_check.py`; `core_test`: a synthetic NPC list through the classifier and the module on gm_construct (jump on a "zombie" box, get the HIT) |
| G3 | HUD, the Options page section, ConVars | lua_check; the in-game checklist |

In-game checklist for the owner: Sonic jumps on a zombie (it takes damage, Sonic bounces); a combine soldier shoots
Sonic (rings scatter, Sonic blinks and picks them up again); with 0 rings a hit kills (or not, with "can die" off); a
homing chain over three headcrabs; Knuckles punches an antlion; Amy's hammer on a manhack; E-102 locks on to two
zombies and a jeep and destroys the zombies (time bonus) while the jeep is pushed; Tails' tail attack; Big's lure on a
headcrab; the ring HUD; every option on the Q menu page.

---

## 5. Open questions for the owner

1. Damage mode default: "rings" (SA1 rules, GMod health untouched) is proposed; "both" is closer to a GMod gamemode.
2. One-hit kills off by default (GMod NPCs keep their health) or on (the SA1 feel)?
3. Should touching an NPC hurt (SA1: yes) by default? Proposed: yes for hostile NPCs.
4. PvP between G-SA1 players: off by default; with it, should an attack on another character also knock it back (the
   victim's world gets a host damage request) as well as cost rings?
5. Game-style destruction effects (explosion sprite, animals) for NPC kills need the optional decompiles (4.1 item 2).


**Owner decisions (2026-10-20):** (1) damage mode default = rings (SA1 rules; GMod health untouched); (2) attacks on NPCs do SCALED GMod damage per attack kind (tunable in the Options tab), not one-hit kills; (3) touching a hostile NPC hurts (hostile NPCs only); (4) PvP ON by default (server option to disable): attacks knock the other character back and cost rings; (5) additionally: props the player could pick up with E must be holdable by the characters (SA1's own hold/carry/throw mechanic; a HOLDABLE host-object kind; the real prop follows the character's hands and gets the throw velocity).
(6) owner request: separate toggles per target category instead of one switch: Attack Friendly NPCs, Attack Hostile NPCs, Attack Neutral NPCs, Attack Vehicles, Attack Players, Attack Props/Breakables (plus per-category lock-on/homing targeting and touch-harm where it applies), all in the Options tab. (7) owner request: GMod balloons (gmod_balloon) are homing-attack targets; hitting one pops it and launches the character upward with a vertical boost as in the later Sonic games (use the game's own spring/launch path so the animation and state are right; the boost strength is an option).

## Appendix: addresses used by the layer

| What | Address |
|---|---|
| CCL_Init / CCL_Entry / range / hit reset / free | 8C02F4B0 / 8C02F63C / 8C02F400 / 8C02F5E0 / 8C0344A0 |
| CCL pass / list reset | 8C0342E0 / 8C02F81A |
| Shape centre (world) | 8C02FC12 (8C02FD32 for cylinders) |
| Hit record / levels / push filter / pair filter | 8C02F8A0 / 8C02FA20 / 8C02FB3A / 8C0332A0 |
| Target list fill | 8C03339E (lists B_8C73EE38, B_8C742288, B_8C741280, B_8C7446D0; counts B_8C747708 .. B_8C74770E) |
| CCL lists of ids 2 / 3 / 4 | B_8C745908 (B_8C752BD8) / B_8C745B08 (B_8C752BDA) / B_8C745F08 (B_8C7456DC) |
| CreateElementalTask / FreeTask / DestroyTask / CAlloc | 8C09852E / 8C098786 / 8C09865C / 8C098B0C |
| Enemy damage check / bounce / SetVelocityP | 8C0489E4 / 8C048980 / 8C07C676 |
| Score / ring add / ring count / scatter / death | 8C08A2A2 / 8C08A198 / B_8C752BE4 / 8C086F5E / 8C07BBC2 |
| Barrier / magnetic barrier / invincibility grants | 8C07D43E / 8C07D3DC / 8C07D4A0 (items 1 / 2 / 0x8000) |
| E-102 lock search / validity / dest hook / marker exec / release | 8C0D38C6 / 8C0D32A0 / 8C0D32C0, 8C0D332C / 8C0D577C / 8C0D5D90, 8C0D5DF6 |
| E-102 shot / impact / bonus | 8C0D3DA0 / 8C0D65BA / 8C0D5064 |
| Homing / Light Speed Attack / Maximum Heat | 8C0AF18C, 8C0AF280 / 8C0AF5A0 / 8C0C8F20 |
| Host code address (new) | 0x7F000100 (`LIBSA1_HOST` image, signature v:p) |

---

## 6. Addendum (round 5 planning, 2026-10-21)

Research and design for the owner decisions of 2026-10-20 that sections 2-4 do not cover yet (4 PvP on, 5 HOLDABLE,
6 per-category toggles, 7 BALLOON; 1-3 are restated in the ConVars of 6.3) and for followers, plus an adversarial
re-check of 2.3-2.5 against the current LIB (after round 4b). Round 4b2 was editing LIB while this was
read, so LIB line numbers are "as read on 2026-10-21": the function names are the stable reference. Where this
section and sections 2-4 disagree, this section wins. Scratch and tools: `SADC/build/research/interaction_addendum/`
(`rd.py` reads 1ST_READ words and floats, `ccl.py` dumps CCL_INFO tables, `find_id4.py` / `find_flag40.py` asm scans).

### 6.0 What changes in sections 2-4

* Two new kinds: `SA1_HOSTOBJ_HOLDABLE` (5: CCL id 4, taskwk.id 4 = the game's lift class 0x200) and
  `SA1_HOSTOBJ_BALLOON` (6: CCL id 3, a homing / lock-on target without touch harm whose hit launches the character
  through the game's spring request). 6.1, 6.2.
* 3.1's classification and 3.5's single switches are replaced by per-category rows (attack, targeting, touch harm)
  and per-attack damage ConVars; PvP is ON and knocks the victim back in the victim's own world. 6.3.
* 2.3 corrections: segment 0x18 is not carried by RAM images or transplants; the 0x140-byte slot cannot hold the
  listed fields; CCL_Init has a second, silent failure; the host code image must be appended to `fn_images`
  explicitly. 6.4.
* Follower worlds mirror host objects through a per-world kind mask; their HIT events are not forwarded by default;
  the owner's attacks never hit his own followers by default. 6.5.
* New SADC requests (4.1): decompile `8C0586A0` (ObjectMovableInitialize, 138 B, today a fatal stub,
  `LIB/gen/policy/stubs.tsv:121`) so a HOLDABLE proxy is set up by the game's own initialiser; for the real-object
  reference also O_SAMPLE_S (`8C064980` init, `8C0649CC` exec) and the movable step `8C05872A` (fatal stub,
  `stubs.tsv:122`) with its small helpers `8C058D3C` / `8C058DD4`. 6.7.

### 6.1 HOLDABLE (owner decision 5)

#### 6.1.1 How SA1 lifts, carries, puts down and throws

All addresses 1ST_READ; C in `SADC/src/1st_read`.

**Liftable objects.** A movable object is set up by `func_8C0586A0(twp, mwp, n)` (SADX ObjectMovableInitialize, for
orientation only; no C). It sets `twp->id = n` and, from the asm 8C0586A0-8C05872A (raw MOV.L constants, Ghidra
`8C0586A0.c`):

| n | twp->flag gains | mwp+0x34 (rad) | mwp+0x3C (weight: gravity per step) |
|---|---|---|---|
| 4 | 0x240 | 5.0 | 0.02 |
| 5 | 0x640 | 20.0 | 0.1 |
| 6 | 0x640 | 30.0 | 0.2 |
| 7 | 0x840 | 5.0 | 1.0 |

It returns 0 (does nothing) without a motionwk. `twp->flag 0x40` means "a player may take it"; `0x1000` means "held"
(set by the player at the pick-up, cleared at the release). The class a character sees is `func_8C06C880`
(`code_8C06C880.c:26-121`):

| taskwk.id | Sonic, Tails, Knuckles, Amy, E-102 | Big | Eggman, Tikal |
|---|---|---|---|
| 4 | 0x200: lift and carry | 0x200 | 0 |
| 5 | 0x400: grab, then push | 0x600: lift as well | 0 |
| 6 | 0 | 0x600 | 0 |
| 7 | 0x800: heavy push (Sonic mode 34, action 49) | 0x800 | 0 |

Every playable character lifts class 4; only Big lifts the heavy classes 5 and 6 (the game's own "Big is strong"
rule). Super Sonic never lifts (`super_sonic.md` 1.5). A candidate is the task that the character's colliwk hit in the
last pass, with **both** bits 2 (hit) and 8 (side contact) set in the character's `colliwk.flag`, the character not
hurt (`taskwk.flag 4`), the object's `twp->flag 0x40` set and `0x1000` clear (`code_8C06C880.c:80-90`). The side bit comes from the id 0 x id 4/5
pass `func_8C033A10` (`code_8C033800.c`): the character's shape 0 against an object **cylinder** goes to `8C0301A0`,
whose SIDE branch sets `cwp1->flag` and `cwp2->flag |= 8` (`code_8C0301A0.c:109-117`; it needs the character's feet
more than 5 units below the cylinder's top); a box goes to `8C031F88` (SIDE as well); a **sphere** goes to `8C02FF7E`
(`code_8C02FD32.c`), which sets only the hit bit 2. Liftable shapes are therefore cylinders (the game's own samples)
or boxes, never spheres.

**Pick-up (Sonic; the others are the same with their own modes and actions).** In modes 1 / 2 (`code_8C0A966C.c`
SonicModeMachine) `func_8C0B16BC` (`code_8C0B16BC.c:91`): the action button pressed (`B_8C754C08[pno].press &
D_8C19E3A0`; the mask word is 0x602 in 1ST_READ) and a class != 0: class 0x200 starts the lift (mode 21, action 33, speed
capped at jog speed). `func_8C0B17AE` (:129): from motion frame 12, if the object is straight ahead (`func_8C06CF60`
class 4: the facing within 0x1C00, about 39 degrees, of the object, `code_8C06CCA0.c:133-151`): mode 23, SE 0x45A, `pwp->htp = hit_cwp->mytask`,
`htp->twp->flag |= 0x1000`, the character's `twp->flag |= 0x800` and `&= ~8`; otherwise back to standing. The other
characters' handlers: Tails `8C0BCBCA` (:146, :182), Knuckles `8C0C67BC` (:66, :102), E-102 `8C0D0B16` / `8C0D10BC`,
Amy `8C0DBC16` / `8C0DC1E2`, Big `8C0E4E60` (:51, :95).

**Where the game puts it: the hand path, not PSetPosition.** SonicMain (`code_8C0A77BC.c`) runs, after
`PSetMotion`, `PJoinVertexes` (:959; only when not in a ball), which evaluates the current action's node matrices
from the unit matrix and stores `pwp->righthand_pos/_vec` and `lefthand_pos/_vec` (weld types 4 / 5,
`code_8C07A55C.c`; model space), then `func_8C0AE110` (writes the body shape 0 centre, absolute), then, when
`pwp->htp != NULL`, the hold update `func_8C0B1DE0` (:964; `code_8C0B1C56.c:141-196`):

```
htwp->pos = character's shape 0 centre (absolute)
hand = (righthand_pos + lefthand_pos) / 2         (throw actions 59-62: righthand_pos + 2.5 * righthand_vec)
htwp->pos += R(ang.z, ang.x, 0x8000 - ang.y) * (hand - centre of htwp's CCL shape 0)
```

The held object's **angles are not touched**. The hand positions are computed in the exec, so they are valid in a
libsa1 world without the display pass. A non-0x200 object is not placed; it is dropped when the character hits
another object. Per character: Sonic `8C0B1DE0`, Tails `8C0BD4CC`, Knuckles `8C0C6E80`, E-102 `8C0D1672`, Amy
`8C0DC7DE`, Big `8C0E5698`.

**What the held object's own exec does.** The game's movable step `func_8C05872A` (SADX ObjectMovableSRegularExecute,
orientation only; no C; Ghidra `8C05872A.c`): while `twp->flag & 0x1000` it does **nothing**: no physics, no
`CCL_Entry` (so the held object is in no CCL list and cannot collide with its holder). Otherwise: `acc.y = -weight`
(0 on the ground, flag 1), horizontal damping 0.996 in the air, 0.1 on the ground (|v| <= 1 becomes 0),
`spd.y += acc.y` clamped to +-6, land collision (`8C056A04`), then `CCL_Entry` and `8C058D3C` (`flag &= ~0x38`,
`acc = 0`). A holdable object therefore needs: `taskwk.id` = class, `flag 0x40` (+ the class bits), a colliwk with at
least **two** shapes (8C0B1DE0 reads shape 0's centre; the put-down reads shape 1's `a`), and a **motionwk**
(`func_8C06C880` returns 0 for a held task without `mwp`, `code_8C06C880.c:41-43`; the throw writes `htp->mwp->spd`).

**Throw.** Carrying (modes 23 / 25 / 28 / 29), the action button (`func_8C0B184C`, `code_8C0B16BC.c:153`): in the air
mode 33 (action 61 aimed / 62 not), on the ground mode 26 (59 / 60), or the put-down (mode 22, action 63) when slower
than jog speed; "aimed" when the character's target list is not empty (`func_8C06D118`). At the release frame
`func_8C0B199A` (:194) clears the object's 0x1000, writes `htp->mwp->spd` in world space (SA1 units per step), clears
the character's 0x800 and `htp`, SE 0x45B:

| Character | Aimed (action / frame) | Fixed speed (action / frame) | Put down | Code |
|---|---|---|---|---|
| Sonic | 59 / 20, 61 / 7 | 60 / 24, 62 / 6 | 63 / 12 | 8C0B199A |
| Tails | 84 / 20, 86 / 7 | 85 / 24, 87 / 6 | 88 / 12 | 8C0BD098 |
| Knuckles | 96 / 20, 98 / 7 | 97 / 24, 99 / 6 | 100 / 12 | 8C0C6A7A |
| E-102 | 35 / 20, 37 / 7 | 36 / 24, 38 / 6 | 39 / 12 | 8C0D11DC |
| Amy | 67 / 20, 69 / 7 | 68 / 24, 70 / 6 | 71 / 12 | 8C0DC3C0 |
| Big | 41 / 12, 43 / 7 (speed 4) | 42 / 16, 44 / 6: (3, 3, 0) | 80 / 8, 45 / 8: let go, no speed | 8C0E5166 |

Aimed: `v = (horizontal distance, dy, 0)` to the shape 0 centre of the **nearest** target-list entry, normalised, x 3
(Big x 4), player space to world (`PConvertVector_P2G`): thrown at the target's elevation along the facing
(`code_8C0B16BC.c:216-247`). Fixed: player-space (2, 0.8, 0) (:249-263). In host units (Source preset, 4 HU per SA1
unit, 60 steps per second): 3 /step = 720 HU/s, (2, 0.8, 0) = 517 HU/s at 21.8 degrees up, Big's 4 = 960 HU/s and
(3, 3, 0) = 1018 HU/s at 45 degrees.

**Put-down** (action 63, frame 12): 0x1000 cleared, no speed, `func_8C07DC2A` pushes the object out horizontally to
`pwp->p.rad + htwp's shape 1 a` from the character (`code_8C07DC2A.c`). Big has no push-out.

**Drop** (hurt, or the object lost): `func_8C0B1D60` (`code_8C0B1C56.c:106-126`; SonicModeMachine calls it on a hurt,
`code_8C0A966C.c:134-136`): 0x1000 and 0x800 cleared, a 0x200 object gets player-space (1, 1, 0) (339 HU/s at 45
degrees), `htp = NULL`. Per character: Sonic `8C0B1D60`, Tails `8C0BD440(twp, pwp)`, Knuckles `8C0C6DE0`, E-102
`8C0D15C0`, Amy `8C0DC75E`, Big `8C0E55E2` (all `(twp, mwp, pwp)` except Tails).

**An SA1 liftable object for a reference trace: O_SAMPLE_S** (`objexec_O_SAMPLE_S` 8C064980, 1ST_READ, in Mystic
Ruins' object list only). Init (asm `8C064980.src`): `twp->counter` = model 0x8C199918, `CCL_Init(tp, D_8C1BA668, 4,
1, 2)`, `func_8C0586A0(twp, mwp, 4)`, dest `8C064A60` (RTS), exec `8C0649CC`: `func_8C0919CC` range check (410 units
without an ocp, so no SET data is needed), `func_8C05872A`, then draws the model at pos + 2.5 Y, scale 0.25. CCL table
(`ccl.py 8C1BA668 2`): shape 0 cylinder attr 0x881, flag 0, b05 0xF0, centre (0, 2, 0), a 15, b 4 (the grab range);
shape 1 cylinder attr 0x881, b05 0xAF (pushes everyone), centre (0, 2, 0), a 0.1, b 2.5 (the body). A harness scenario
can spawn it anywhere with `CreateElementalTask(3, 3, 0x8C064980)` (`im & 2` allocates the taskwk, `im & 1` the
motionwk: `code_8C098444.c:146-196`).
Other class-4 users exist in real stages (Sky Deck O_LEVER, Hot Shelter O_KAIDAN and 0C908780, Kiki's thrown task
8C0FBB00 in 1ST_READ; whether those are liftable in play is **UNVERIFIED**).

#### 6.1.2 Design: the HOLDABLE kind (on top of 2.1-2.4)

| | HOLDABLE |
|---|---|
| colliwk id | 4 (the id 0 x 4 pass sets the side bit; not in the target lists, never locked by E-102) |
| task | `CreateElementalTask(3, 3, 0x7F000100)`: taskwk **and motionwk** (`im & 1`). Not the 0x23C enemy work of 2.3: the throw writes `htp->mwp->spd` at +0x04..+0x0F, and E-102 never needs +0x40 / +0x44 of an id-4 object |
| setup | `func_8C0586A0(twp, mwp, 4)` (needs its C: 6.7): `taskwk.id = 4`, `flag 0x240`, rad 5, weight 0.02. With `SA1_HOSTOBJ_HEAVY`: class 5 (only Big lifts it; the others enter the grab / push modes, whose push the host does not model: **UNVERIFIED** behaviour, off by default) |
| ocp | the slot's stand-in (uniform with the other kinds; not needed by any id-4 consumer) |
| CCL shape 0 (grab) | cylinder, attr `0x881` plus 0x2000 and 0xE0000, flag 0, b05 0xF0; centre = the body centre (8C0B1DE0 then puts the body centre at the hands, as O_SAMPLE_S whose two shapes share (0, 2, 0)); radius = max(15, body radius + 8) SA1 units (O_SAMPLE_S: 15 for a body of 0.1); half height = max(6, body half height), so its top is at least 6 above the body centre and 8C0301A0's SIDE test (feet more than 5 below the top) fires for a character on the same floor |
| CCL shape 1 (body) | cylinder, attr `0x881` plus 0x2000 and 0xE0000, flag 0; b05 0xF1 (pushes the character; the game's sample pushes everyone, 0xAF) or 0xF0 with `NOT_SOLID`; radius / half height = the host body (put-down distance = character rad + this radius) |
| sizes | host shapes are converted to these two cylinders (a box gives its horizontal half diagonal and half height); body radius <= 32 SA1 units (128 HU) |

`attr 0x800` keeps Sonic out of the push mode against it, `0x80` (damageable, defence 0) is the game's own value: an
attack out-levels it and sets `taskwk.flag 4`, which the proxy reports as a HIT only with `SA1_HOSTOBJ_HOLD_HITS`
(a breakable prop: "Attack props" in 6.3), otherwise ignores.

**hostobj_exec for HOLDABLE** (replaces steps 1-4 of 2.4 for this kind):

```
held = (twp->flag & 0x1000) and playerpwp[pno]->htp == tp   /* 8C0B17AE sets both; pno = the world's character slot */
if held:
    if slot.state != HELD: state = HELD; push LIFTED
    return                                 /* as 8C05872A: no pose write, no CCL_Entry */
if slot.state == HELD:                     /* the character's throw / put-down / drop cleared 0x1000 */
    push RELEASED {reason, velocity = mwp->spd (world, SA1 units per step) -> host units/s, position = twp->pos,
                   target = the host object of the aimed target-list entry, 0 if none}
    mwp->spd = mwp->acc = 0; state = FREE  /* the host owns the flight from here */
    keep twp->pos until the next host pose (the host moves the prop there)
twp->flag = HOLD_LOCKED ? flag | 0x1000 : flag & ~0x1000
                                           /* the game's own "already held": 8C06C880 skips it */
if flag 4 and HOLD_HITS: classify and push HIT (2.4 step 1 without 8C0489E4)
pose of this step (2.4 step 3); CCL_Entry(twp)
```

Release reason: PUT_DOWN when the speed is 0 and the holder's action is its put-down / let-go action, THROW_AIMED /
THROW for the aimed / fixed actions of the table above, DROP otherwise (hurt, lost object), FORCED for
`sa1_hostobj_release`. While HELD the latch of 2.4 does not write the pose (the character owns it).

**Per-step held state** (computed in `hostobj_post` from the world after each step, read by the host per frame):
`position` and `position_prev` = the proxy's `twp->pos` after this and the previous step (host space, the same
reference point the host registered: the host interpolates with the character's own `alpha`), `yaw` = the holder's
facing (`ang.y`, the `sa1_pose.yaw` convention), `hand` = the hand anchor of 8C0B1DE0 (body centre + R * hand, host
space), `phase` = LIFTING (mode 21 or the kind's lift mode, before frame 12), CARRYING, THROWING (a throw / put-down
action until its release frame).

**Removal and release while held** (new; DestroyTask frees the works and sets `tp->twp = NULL`, 6.4 row 6, so a
held proxy must never be destroyed under its holder): `sa1_hostobj_release`, a removal or the window dropping a held
proxy runs, in `hostobj_pre` before the executor, the holder's own drop function (`8C0B1D60`, `8C0BD440`,
`8C0C6DE0`, `8C0D15C0`, `8C0DC75E`, `8C0E55E2` by kind, with `playertwp/mwp/pwp[pno]` of that world): it clears the
character's 0x800 and the object's 0x1000 and sets `htp = NULL`; the mode machine then returns to standing or falling
by itself (Sonic `8C0B184C`, `code_8C0B16BC.c:176-187`). Only then `FreeTask`. RELEASED (FORCED) is reported.

**Limits.** The game's own liftables are small (class 4: rad 5 SA1 units, 20 HU). G-SA1 uses GMod's own "pick up
with E" rule instead (owner decision 5): `prop_physics*`, motion enabled, mass <= 35 kg and largest extent <= 128 HU
by default (the Source SDK player +use limits `PLAYER_MAX_LIFT_MASS` / `PLAYER_MAX_LIFT_SIZE` of `hl2_player`;
**UNVERIFIED** that GMod uses the same numbers), `AllowPlayerPickup` not false; both numbers are server ConVars. Class
4 objects fall at 0.02 SA1 units per step squared = 288 HU/s^2 in the game; GMod props fall at `sv_gravity` (600):
thrown props drop about twice as fast as in SA1 (owner question 6.8 Q4).

#### 6.1.3 G-SA1 mapping

* **Server tags candidates**: on entity creation and on freeze / unfreeze / physgun drop, `sv_gsa1.lua` sets NW2Bool
  `gsa1_hold` on `prop_physics*` within `gsa1_sv_hold_mass` / `gsa1_sv_hold_size` with motion enabled (the client
  cannot read a server prop's mass). The client registers tagged props as HOLDABLE (with `HOLD_HITS` when the prop
  has health and "Attack props" is on; `HOLD_LOCKED` while NW2Entity `gsa1_holder` is another player).
* **LIFTED** -> `net "gsa1_hold"` {entindex, 1}. The server validates: `gsa1_sv_hold`, still tagged, not held by a
  player (`ent:IsPlayerHolding()`) or another character (`ent.GSA1HeldBy`), within 200 HU + ping allowance of
  `ply:GetPos()`, `hook.Run("AllowPlayerPickup", ply, ent)` and `hook.Run("GSA1CanHold", ply, ent)` not false. Accept:
  `ent.GSA1HeldBy = ply`, NW2Entity `gsa1_holder`, `phys:EnableMotion(false)` (**frozen while held**), collision
  group `COLLISION_GROUP_DEBRIS` (it no longer shoves NPCs or players; the old group is kept). Refuse:
  `gsa1_hold_deny` -> the client calls `ObjRelease(id)` (the character drops it, 6.1.2).
* **While held**: the owner's client draws the prop each frame at the interpolated held pose
  (`SetRenderOrigin` / `SetRenderAngles`: no network latency for the owner) and sends `gsa1_hold_pose` {entindex,
  pos, yaw} unreliable at <= 30 Hz; the server `SetPos` / `SetAngles` the frozen prop (others see it follow). Angles:
  the prop's pitch and roll are kept, its yaw follows the character with the offset it had at the pick-up.
* **RELEASED** -> `gsa1_hold` {entindex, 0, pos, velocity (HU/s), reason}: the server sets the position, restores
  the collision group, `EnableMotion(true)`, `Wake()`, `SetVelocity(velocity)`, `ent:SetPhysicsAttacker(ply, 5)` (a
  thrown prop's impact damage on NPCs is credited to the player), clears the NW2 values. Server-side ends (the prop
  removed, picked up by a physgun, the player leaving or the character removed) release it the same way and send
  `gsa1_hold_drop` to the holder (-> `ObjRelease`).
* **Q menu**: "Pick up props" (`gsa1_hold`); admin: "Characters can hold props" (`gsa1_sv_hold`), "Max mass (kg)"
  (`gsa1_sv_hold_mass`, 1-250, 35), "Max size (HU)" (`gsa1_sv_hold_size`, 16-256, 128). The pick-up uses the button
  G-SA1 already maps to the game's action button; no new key.

### 6.2 BALLOON (owner decision 7)

#### 6.2.1 SA1's balloon and spring paths

**SA1's own balloon is not a pop-and-launch object.** `objexec_O_BALOON` 8C059E20 (1ST_READ, entry 12 of most stage
object lists; model 0x8C1AB8BC `baloon_esc_baloon`) is **Amy's goal balloon**: `CCL_Init(tp, D_8C1B96A8, 4, 1, 2)`
(sphere r 17 at +17, cylinder r 1 b 20); mode 1 (`8C059F40`, asm `8C059E20.src`) bobs, and when player 0 is Amy
(`8C07CE40(0) == 5`) within 8 units of a point 22 below it: mode 2, `SetInputP(0, 12)` (park), `8C08A5B6()`,
`8C04EFE0()` (**UNVERIFIED**: the timer stop and the clear sequence), then it rises carrying her (8C059FFC). The SADX
OBalloon / Balloon_Main names in `reference/sadx_pc_functions.csv` were used for orientation only.

**The spring path.** `objexec_SPRING` 8C065800 (ground spring) calls `func_8C065A98(tp, 0)` after SE 9; SPRINGB
8C065EE4 calls it with type 1 (asm `8C065800.src`, call sites 8C065A52 / 8C06611C). 8C065A98: the launch direction
is `R(spring angles) * D_8C1550AC` with `D_8C1550AC = (0, 5, 0)` (read from 1ST_READ); a character 2-14 horizontal
units from the spring top is first moved 2 units towards it (`SetInputP(pno, 12)`, position, `SetInputP(pno, 24)`);
then

* type 0 (ground spring): `func_8C07C720(pno, dir)` (`code_8C07C550.c:106-114`): `SetInputP(pno, 3)` (flag 0x1000,
  smode 3) and `SetVelocityP` `8C07C676` (world speed into `mwp->spd`, player space into `pwp->spd`): **5 SA1 units
  per step along the spring's up, 300 SA1 units/s = 1200 HU/s** in the Source preset;
* type 1 (SPRINGB): `func_8C07C768(pno, scl.y + 5, &ang, (int)max(scl.z, 0))` (`code_8C07C738.c`): smode 4,
  player-space speed (0, s, 0), the character rotated to the spring and no-control time `scl.z` steps;
* then the rumble `8C109EAC(pno, 4, 15, 0)` (a RUMBLE event in libsa1) and `twp->counter.f = 0.1`.

Order 3 (and 8) is taken by every character's next-action handler:

| Character | smode 3 (also 8) | Holding variant | Then |
|---|---|---|---|
| Sonic | mode 9, action 16 (Sonic_Spring_Launch), clears 0x2500 (`code_8C0AFF1A.c:67-72`) | `8C0B07E4` | mode 12, action 17 when `spd.y <= 0` (`code_8C0A966C.c` case 9) |
| Tails | mode 7, action 17 (`code_8C0BBDD6.c:59-63`) | mode 30, action 80 (`8C0BC61C`) | |
| Knuckles | mode 7, action 17 (`code_8C0C5BCA.c:59-64`) | mode 36, action 92 (`8C0C61F4`) | |
| E-102 | mode 5, action 6, ang.x/z = 0 (`code_8C0D079A.c:48-56`) | mode 24, action 31 (`8C0D0B16`) | |
| Amy | mode 6, action 16 (`code_8C0DB67E.c:80-85`) | mode 32, action 63 (`8C0DBC16`) | |
| Big | mode 5, action 12 (`code_8C0E43CA.c:82-90`) | mode 24, action 37 (`8C0E488C`) | |
| Super Sonic | modes 75 / 76 (requests 4 / 3), action 135 (`super_sonic.md` 1.5) | none | |

A spring launch uncurls the character (Sonic's ball flag 0x100 is cleared, mode 9 then the fall mode 12): Sonic
cannot home again until he lands. The enemy bounce `8C048980` (2 per step up, only in a ball, airborne, Sonic / Tails /
Knuckles) keeps the ball and mode 8, so it allows homing chains.

#### 6.2.2 Design: the BALLOON kind

| | BALLOON |
|---|---|
| colliwk id | 3: in the player target lists (homing, Light Speed Attack, Maximum Heat, aimed throws, Big's 8C0EB432, turn to target 8C06D136) and in E-102's lock-on lists |
| task | as ENEMY / TARGET (2.3): taskwk, the 0x23C enemy work (E-102's marker at +0x40, its dest at +0x44, the `mwp` test of 8C0D37F4), the ocp stand-in |
| shapes | from the host (a sphere or a vertical cylinder from the OBB); attr `form` plus 0x2000, 0x80, 0xE0000, and 0x100000 on shape 0 (lockable, no time bonus); flag 0 (armor 0, touch 0, no applies-to bits: never hurts); b05 0xF0 (not solid) |
| hit | any attack of level >= 1 (`atk > def 0`); with `SA1_HOSTOBJ_POP_ON_TOUCH` also plain body contact (the REPORT_TOUCH test of 2.4) |

**hostobj_exec for BALLOON**, on `taskwk.flag 4` (or a touch with POP_ON_TOUCH), instead of 2.4 step 1's
`8C0489E4` (no squash, no score, no enemy bounce):

```
classify the snapshot (2.6) -> attack, level, PROJECTILE
if the attacker is the character's own body (colliwk id 0, owner playertwp[pno]):
    v = desc.launch_speed converted to SA1 units per step, 0 -> 5.0 (objexec_SPRING's speed)
    h = KEEP_HSPEED ? (mwp->spd.x, mwp->spd.z) of the character : (0, 0)
    if LAUNCH_BOUNCE and 8C048980's conditions hold (ball, airborne, Sonic / Tails / Knuckles):
        func_8C07C676(pno, h.x, v, h.z)       /* SetVelocityP only: the ball and mode 8 stay, homing can chain */
    else:
        func_8C07C720(pno, h.x, v, h.z)       /* SetInputP(pno, 3) + SetVelocityP: the game's ground-spring request */
    func_8C109EAC(pno, 4, 15, 0)              /* the spring's rumble (RUMBLE event) */
    flags |= SA1_HITF_LAUNCHED
if ewp+0x40 (E-102 marker) and the attacker is E-102's shot: func_8C0D5D90(tp)   /* as ENEMY */
push HIT; cooldown (defence 3, no 0x800) until the host removes it (the pop) or the cooldown ends
```

A projectile (E-102's laser and impact, Big's lure, Sonic's sparkles) pops the balloon without launching anybody. The
request is handled by the character's next exec (one step after the proxy's exec, as an enemy bounce) through the
next-action handler, which most mode cases call first (as for the game's springs); in a mode that does not call it
the request stays pending (which modes: **UNVERIFIED**, test 6.7 case 16). No game SE is played: GMod's own pop
effect plays `garrysmod/balloon_pop_cute.wav` (`sandbox/entities/effects/balloon_pop.lua:7`).

**What each character does with a balloon** (attack levels from 1.4; only Sonic homes):

| Character | Pops it with | Aims at it | Launched as |
|---|---|---|---|
| Sonic | jump / roll / homing attack (ball, level 1), spin dash, Light Speed Attack, invincibility / Super; sparkles (projectile, no launch) | homing attack (8C0AF18C / 8C0AF280), Light Speed Attack | mode 9 action 16, then the fall (mode 12, action 17): no homing until he lands (with LAUNCH_BOUNCE: stays in mode 8 and can chain) |
| Tails | jump ball, tail attack (shapes 1-2), flight from below (shape 3) | no | mode 7, action 17 |
| Knuckles | jump ball, punches, Maximum Heat, digging | Maximum Heat (8C0C8F20) | mode 7, action 17 |
| Amy | hammer (her jump does not attack) | no | mode 6, action 16 |
| E-102 | laser shot / impact only (projectiles: pops, no launch); the body never attacks | lock-on (8C0D33A0 -> 8C0D38C6) | (only with POP_ON_TOUCH) mode 5, action 6 |
| Big | lure (projectile, no launch); the attack shape 1 (`twp->flag 0x400`) | Big's 8C0EB432 | mode 5, action 12 |
| any, holding a prop | as above | aimed throws | the holding variant of the table in 6.2.1 (keeps the prop) |

#### 6.2.3 G-SA1 mapping

* The client registers `gmod_balloon` entities (class name) as BALLOON: a sphere from the OBB (the balloon
  models are roughly 20-40 HU, **UNVERIFIED**; clamped to 4..1000 SA1 units as 3.1), `launch_speed` from
  `gsa1_balloon_speed` (HU/s, 0 = the game's 1200 HU/s), POP_ON_TOUCH from `gsa1_balloon_touch`, LAUNCH_BOUNCE from `gsa1_balloon_launch`.
* The launch happens at once in the character's world (no round trip). The HIT goes to the server as 3.2
  (`gsa1_hit`); the server checks `gsa1_sv_balloon_pop` and the 3.2 distance rule and applies a `DamageInfo` of 1
  with attacker = the player: `gmod_balloon:OnTakeDamage` (`sandbox/entities/entities/gmod_balloon.lua:48-75`) plays
  the pop effect, explodes an explosive balloon (whose blast then hurts the character through 3.3) and removes it;
  an `Indestructible` balloon ignores the damage and stays a bounce pad (the proxy's cooldown ends after 20 steps).
  The removal reaches every world as SILENT (`EntityRemoved`).

### 6.3 Per-category toggles, PvP and scaled damage (owner decisions 1-4 and 6)

#### 6.3.1 GMod class -> kind and flags

Relations as 3.3 (`gsa1_rel`; NextBots hostile unless on the friendly list; before the first relation message, by
class). Every category has an **Attack** switch (HIT events of that category are sent to the server: GMod damage; when
off the proxy still reacts in the world, so the character bounces as in SA1, but nothing is sent), a **Targeting**
choice (0 off: `NO_HOMING` and `NO_LOCKON`; 1 E-102 lock-on only: `NO_HOMING`; 2 homing and lock-on: no flag;
"homing" means every consumer of the player target list: homing attack, Light Speed Attack, Maximum Heat, aimed throws,
Big's aim) and, where it applies, **Touch harm**. A category with attack, targeting and touch all off registers no
proxy at all.

| Category | GMod entities | Kind (flags) | Touch harm (default) | Targeting (default) | Attack (default) |
|---|---|---|---|---|---|
| Hostile NPCs | `IsNPC()` / `IsNextBot()`, relation D_HT or D_FR | ENEMY (SOLID by `gsa1_solid_npcs`) | yes, touch 1 (on: decision 3) | 2 | on |
| Neutral NPCs | relation D_NU | ENEMY, touch 0 unless the touch switch is on | yes (off: decision 3 is hostile only) | 2 | on |
| Friendly NPCs | relation D_LI; the friendly class list until a relation arrives | TARGET (NO_BONUS) | never | 0 | off |
| Vehicles | `IsVehicle()`, `prop_vehicle_*`, LVS / simfphys bases by class prefix | TARGET (NO_BONUS, not solid; also a moving collision object) | never | 2 | on |
| Players (PvP) | `IsPlayer()`, not the local player | ENEMY, touch 0 (NO_BONUS) | never (decision 3) | 2 | on (only if the server's PvP is on) |
| Props / breakables | `func_breakable`, `func_breakable_surf`, `func_physbox`, `prop_physics*` with health (NW2 `gsa1_hp`) | BREAKABLE (id 4: no targeting); TARGET (id 3) when targeting > 0 | never | 1 | on |
| Holdable props | `prop_physics*` tagged `gsa1_hold` (6.1.3) | HOLDABLE (HOLD_HITS when it has health and props' attack is on) | never | n/a (id 4) | the props row |
| Balloons | `gmod_balloon` | BALLOON (6.2) | never | 2 | pop (the server's `gsa1_sv_balloon_pop`) |
| Hazards | `env_fire`, `entityflame`, `npc_grenade_frag`, `grenade_ar2`, `rpg_missile`, `prop_combine_ball`, `crossbow_bolt` | HAZARD (touch 2) | yes (on) | never | never |

A prop that is both holdable and breakable is one HOLDABLE object. A toggle change re-applies the flags of that
category's registered objects (`ObjUpdate`) or adds / removes them.

#### 6.3.2 ConVars (they replace 3.5's single switches)

Client (`FCVAR_ARCHIVE`, Q menu > Options > G-SA1 > "Interaction"; one row per category: an Attack checkbox, a
Targeting combo "Off / E-102 lock-on only / Homing and lock-on", a Touch checkbox where it applies):

| ConVar | Default | Notes |
|---|---|---|
| `gsa1_interact` | 1 | master switch (3.5) |
| `gsa1_hostile_attack`, `gsa1_hostile_target`, `gsa1_hostile_touch` | 1, 2, 1 | |
| `gsa1_neutral_attack`, `gsa1_neutral_target`, `gsa1_neutral_touch` | 1, 2, 0 | |
| `gsa1_friendly_attack`, `gsa1_friendly_target` | 0, 0 | replaces `gsa1_target_friendly` |
| `gsa1_vehicle_attack`, `gsa1_vehicle_target` | 1, 2 | replaces `gsa1_homing_vehicles`, `gsa1_lock_vehicles` |
| `gsa1_player_attack`, `gsa1_player_target` | 1, 2 | |
| `gsa1_prop_attack`, `gsa1_prop_target` | 1, 1 | replaces `gsa1_target_props` |
| `gsa1_hazard_touch` | 1 | replaces `gsa1_touch_hurt` with the two NPC touch switches |
| `gsa1_hold` | 1 | pick up props (6.1) |
| `gsa1_balloon`, `gsa1_balloon_launch`, `gsa1_balloon_speed`, `gsa1_balloon_touch` | 1, 0, 0, 0 | balloons are targets; 0 spring / 1 bounce; HU/s, 0 = the game's 1200; touching pops |
| kept from 3.5 | | `gsa1_can_die`, `gsa1_lose_rings`, `gsa1_knockback_from_source`, `gsa1_solid_npcs`, `gsa1_hud_rings`, `gsa1_hud_score`, `gsa1_obj_window`, `gsa1_obj_debug` |
| followers (6.5, round 6b) | | `gsa1_follower_attack` 0, `gsa1_hit_own_followers` 0 |

Server (`FCVAR_ARCHIVE` and `FCVAR_REPLICATED`; an admin section of the same page, changes through
`RunConsoleCommand`):

| ConVar | Default | Notes |
|---|---|---|
| `gsa1_sv_damage` | 1 | characters damage NPCs / props / players |
| `gsa1_sv_damage_mode` | 0 | rings (decision 1) / health / both (3.3) |
| `gsa1_sv_pvp` | **1** | decision 4 (3.5 had 0) |
| `gsa1_sv_pvp_knockback` | 1 | the victim is knocked back away from the attacker (6.3.3) |
| `gsa1_sv_dmg_scale` | 1.0 | multiplies every per-attack value |
| `gsa1_sv_dmg_<kind>` | 3.2's table | one per SA1_ATTACK_* (decision 2), below |
| `gsa1_sv_hold`, `gsa1_sv_hold_mass`, `gsa1_sv_hold_size` | 1, 35, 128 | 6.1 |
| `gsa1_sv_balloon_pop` | 1 | 6.2 |
| `gsa1_sv_min_damage`, `gsa1_sv_kill_damage`, `gsa1_sv_kill_on_death` | 1, 1000, 0 | 3.3 |

`gsa1_sv_onehit` is dropped (decision 2: scaled damage, not one-hit kills).

Per-attack damage (decision 2; GMod damage = value x `gsa1_sv_dmg_scale`; the damage types stay those of 3.2; Q menu:
admin sub-section "Damage per attack", one slider 0-500 per kind and a "Reset to defaults" button):

| SA1_ATTACK_* (2.6) | ConVar | Default | SA1_ATTACK_* | ConVar | Default |
|---|---|---|---|---|---|
| OTHER 0 | `gsa1_sv_dmg_other` | 40 | DIG 9 | `gsa1_sv_dmg_dig` | 50 |
| SPIN 1 | `gsa1_sv_dmg_spin` | 50 | HAMMER 10 | `gsa1_sv_dmg_hammer` | 90 |
| SPIN_DASH 2 | `gsa1_sv_dmg_spindash` | 75 | LASER 11 | `gsa1_sv_dmg_laser` | 80 |
| HOMING 3 | `gsa1_sv_dmg_homing` | 60 | LASER_BLAST 12 | `gsa1_sv_dmg_blast` | 30 |
| LIGHT_SPEED_ATTACK 4 | `gsa1_sv_dmg_lsa` | 150 | LURE 13 | `gsa1_sv_dmg_lure` | 50 |
| TAIL 5 | `gsa1_sv_dmg_tail` | 50 | BIG_ATTACK 14 | `gsa1_sv_dmg_big` | 80 |
| TAILS_FLIGHT 6 | `gsa1_sv_dmg_flight` | 50 | SPARKLE 15 | `gsa1_sv_dmg_sparkle` | 20 |
| PUNCH 7 | `gsa1_sv_dmg_punch` | 90 | INVINCIBLE 16 | `gsa1_sv_dmg_invincible` | 200 |
| MAX_HEAT 8 | `gsa1_sv_dmg_maxheat` | 150 | SUPER 17 | `gsa1_sv_dmg_super` | 200 |

#### 6.3.3 PvP (decision 4: on by default, knock-back and rings)

1. The attacker's world reports HIT on the victim's ENEMY proxy (touch 0: contact alone never hurts; the attacker
   bounces off as from an enemy) -> `gsa1_hit` as 3.2.
2. The server validates as 3.2 plus `gsa1_sv_pvp`, victim != attacker, `hook.Run("GSA1CanDamage", ply, victim,
   attack)` not false. Victim with an active G-SA1 character: `gsa1_hurt` to the victim {amount = the scaled damage,
   type, source = the attacker's `GetPos()` (the attacker's character), attacker entindex, kill = false, pvp = true};
   with damage mode rings no GMod damage (decision 1), with health / both also the `DamageInfo`. Victim without a
   character: the `DamageInfo` only.
3. The victim's client: `gsa1.Damage(handle, FACE_SOURCE if gsa1_sv_pvp_knockback, source, the attacker's object
   id)` -> in the victim's own world the character turns to the attacker and the game's own hurt path knocks it back
   away from him, scatters rings and starts the 120-step invulnerability (2.5); the victim's own `gsa1_can_die` /
   `gsa1_lose_rings` apply. The invulnerability (2 s) and the attacker-side cooldown (20 steps) stop juggling.

### 6.4 Adversarial re-check of 2.3-2.5 against the current LIB

| # | Claim | Result | Evidence |
|---|---|---|---|
| 1 | A `LIBSA1_HOST` image listed in `w->fn_images` is reached by `sa1g_fn_image_lookup` | **VERIFIED, with four corrections** | The generated lookup walks `sa1g_fn_active` (the NULL-terminated copy of `w->fn_images` made at world entry, `sa1_world.c` memcpy, :1009 as read), tests `key = g & 0x1FFFFFFF` against `[lo, hi)` and calls `m->lookup(g)` (`SADC/tools/pclower.py` `gen_fntab`, :2532-2600). (a) `sa1g_world_fn_images` (`sa1_world_image.c:554-573`) rebuilds the list **only** from PRS spans matched by name and SHA-1, apply_ram resets it to the base map (:590), and transplant = apply_ram (`sa1_world_boot.c:544-546`): the host image must be appended explicitly after every rebuild. (b) At most `SA1G_FN_ACTIVE_MAX` = 4 images (`sa1_world.h:167`): host + 3 overlays. (c) Signature: the executor calls exec and disp with `0xDA9162B5u /* v:p */` (lowered `code_8C09878C.c:949, 971`), DestroyTask calls dest with it (lowered `code_8C098444.c:573`), the world walker `sa1g_exec_task` passes 0 (`sa1_world_boot.c:677`): the entry must carry 0xDA9162B5 (pclower's hash of "v:p"; re-derive if that hash changes). (d) With `freeze_unknown` (transplants) the walker skips execs without a name (:672): the entry needs its name. Note: `sa1_fn_addr` (native to DC address, traces and debug) knows only generated tables and returns 0 for `hostobj_exec` (harmless) |
| 2 | The slot block in the extension segment 0x18 (budget; "RAM dumps and transplants carry it") | **Budget VERIFIED; "carried" REFUTED; slot size REFUTED** | 16 MB per world (`SA1G_EXT_SIZE`, `sa1_world.h:81`), bump-allocated, committed on demand, never freed until a reset (`sa1g_world_ext_alloc` / `sa1g_world_ext_reset`, `sa1_world.c`). Today's users are small: R4 pcdata, placeholders (`sa1_world_image.c:471-473`), the window landtable `0x24 x cap` (`sa1_world_boot.c:296-297`), the collision adapter state with 2 x 4096 8-byte candidates (64 KB) and moving-object model memory that only grows (`sa1_coladapt_world.c:353, 380, 605`). But segment 0x18 is host memory **outside the 16 MB arena**: apply_ram copies the arena only and resets the extension (`sa1_world_image.c:530-535, 584`), transplant is apply_ram, apply_image resets it as well (:612). So: allocate the block after boot and re-attach after every apply_ram / transplant (proxies inside a transplanted RAM image are not adopted); world-RAM comparisons (4.2 case 10) must compare the block separately; the harness oracle (4.3) has no segment 0x18 and keeps its stand-in and template in DC RAM. The 0x30 bytes at +0x110 cannot hold the listed fields (the hit snapshot alone: three guest pointers, two vectors and a speed, about 44 bytes): keep only what game code dereferences in segment 0x18 (the ocp stand-in 0x10 and the CCL template 4 x 0x34: 0xE0 per slot, 14 KB for 64), and the rest (cooldown, levels, flags, markers, the snapshot, the held state of 6.1) host-side in the world |
| 3 | CCL_Init loops forever when CAlloc fails | **VERIFIED, plus a second failure** | Still in SADC/src: `code_8C02F27C.c:166-171` (`for (;;)` with a debug print; in libsa1 the world thread spins, no fault, no watchdog). Round A4 staged the `PC_ONLY(sa1g_fatal(0x8C02F4B0, ...))` exit (`SADC/build/staging/1st_read/code_8C02F27C.c:178`); L0 lands it. **New:** when the second CAlloc (the `n x 0x34` table) fails, CCL_Init returns silently with the colliwk leaked and `twp->cwp` **not set** (:172-191), and the proxy's next `CCL_Entry` would dereference NULL. The layer checks `twp->cwp != NULL` after CCL_Init and otherwise frees the proxy (`SA1_CAPACITY_HOST_OBJECTS`); the 4 KB headroom test of 2.8 covers both allocations |
| 4 | `8C0D46CE` dereferences `target->ptp->ocp` when `target->ocp` is NULL | **VERIFIED** | `code_8C0D46CE.c:192-196` (case 2): `if (target_tp->ocp) 8C091B8A(target) else if (target_tp->ptp->ocp)`: a top-level task without an ocp faults; the test runs before the `target_tp->twp == NULL` check. 8C091B8A clears `ssCondition` 0x8001 (`code_8C091B8A.c`); the 0x10-byte stand-in (`include/task.h:150-157`) is enough. Only id 2 / 3 proxies (ENEMY, TARGET, BALLOON) can be locked; id-4 kinds never reach it |
| 5 | `sa1g_step_game` can call the pre and post hooks | **VERIFIED** | `sa1_world_tick.c` `sa1g_step_game` (:156-185 as read): camera, the pad update 8C050BE4 (:163), replay injection; the executor branch (:166-171: 8C0988A4, or `sa1g_run_tasks` with replay / xhooks / freeze_unknown); CCL 8C0342E0 (:173); particles; `adapter_fn` (:179-181); land update; tail. `hostobj_pre` goes after the pad update and injection and before the executor branch (both executor paths see it; window and creation on `sub == 0`, damage requests every step); `hostobj_post` right after the CCL call (the hit fields stay until each object's next CCL_Entry; the pass resets only the list counts, 8C02F81A). Follow the `adapter_fn` precedent: a per-world pointer that is NULL without host objects, so the 10 strict reference replays run unchanged. `sa1g_world_step` (one step) also runs `w->step` |
| 6 | Proxies are freed with `FreeTask` at the pair start (2.4) | **VERIFIED safe for hit pointers; new hazard for held objects** | `FreeTask` 8C098786 is `tp->exec = DestroyTask` (`code_8C098444.c:349-352`): the proxy is destroyed by the executor at level 3 of the same step, after the character's exec (level 1) read and reset (its CCL_Entry, 8C02F5E0) the hit fields of the last pass, so nothing reads a freed colliwk; DestroyTask runs E-102's hooked dest first (:282-284). DestroyTask frees twp / mwp / fwp / awp with MFree and sets `tp->twp = NULL` (:315-326, `code_8C098998.c`): a HOLDABLE held by a character (`pwp->htp`) must be dropped by the character first (6.1.2) |

### 6.5 Followers (round 6b)

Round 6b's production follower is its own world: a proxy leader in slot 0 (no CCL entry) and the follower in slot 1,
driven by the game's 1.5P AI (`docs/research/follower_ai.md` 4, 5.1). Host objects are context-wide (2.2), so every
ticking world, follower worlds included, mirrors them:

* **Window** centred on slot 1 (the follower), not the proxy leader (`follower_ai.md` Review R.2.3).
* **Per-world kind mask** (new API, 6.6): character worlds mirror every kind; G-SA1 gives follower worlds ENEMY and
  HAZARD (touch harm and knock-back), plus TARGET and BALLOON only when `gsa1_follower_attack` is on, never HOLDABLE
  (the 1.5P AI presses `D_8C19E390` (jump) and, only in the air, `D_8C19E38C` (0x402), `code_8C082160.c:129-205`; the
  lift needs a ground press of `D_8C19E3A0` (0x602), so a follower practically never lifts: **UNVERIFIED** by test,
  and not needed).
* **Hits on the follower**: the game's slot-1 rules apply by themselves: knock-back and 120 steps of invulnerability,
  no ring loss (`8C086F5E` acts for player 0 only), no death (death zones test player 0, drowning sets `flag 4` for
  pno != 0) (`follower_ai.md` 1.5). HURT events of a follower world carry the follower's id, rings lost 0, never FATAL.
  E-102 and Big call `8C07BBC2(pno)` directly in a few paths (**UNVERIFIED** for followers, as 1.5 says).
* **The follower's attacks**: its world reports HITs with the follower's id. Default: **not forwarded** to the server
  (followers do not damage NPCs; owner question 6.8 Q1, `gsa1_follower_attack`). The in-world effects stay: the
  follower bounces off an enemy (8C0489E4 picks pno 1 when the attacker's owner is not `playertwp[0]`, so
  `8C048980(1)` bounces the follower), and that world's proxy enters its cooldown (other worlds are not affected, 2.8).
  A follower uses the "others" target list `B_8C742288` (`code_8C03339E.c`), so a Sonic follower can home onto
  proxies.
* **PvP and the owner's own followers**: they are not server entities, so 3.2 never sees them. G-SA1 does not
  register its own followers as host objects in the owner's character world: the owner's attacks never hit them
  (default; `gsa1_hit_own_followers` 1 would register them as ENEMY touch 0 and turn a HIT into a local
  `sa1_character_damage(follower, FACE_SOURCE, source = the owner's character)`: knock-back only, by the slot-1
  rules). Other players' followers (multiplayer puppets, later) follow their owner's PvP rule through the server.
* **Damage to a follower from GMod** (bullets at the invisible player entity) never reaches followers: 3.3 forwards
  damage to the player's own character only.

### 6.6 API additions (draft API 0.3, in the host-objects section of `libsa1.h`)

```c
/* ---- host objects: round 5 addendum (6.1-6.5) ----------------------------------------------------------- */
#define SA1_HOSTOBJ_HOLDABLE   5u   /* a prop the characters lift, carry, put down and throw (CCL id 4, class 0x200) */
#define SA1_HOSTOBJ_BALLOON    6u   /* homing / lock-on target, never hurts; a hit pops it and launches the character */

/* sa1_hostobj_desc.flags, new bits */
#define SA1_HOSTOBJ_HOLD_HITS      0x0080u  /* HOLDABLE: report HIT when an attack out-levels it (breakable prop) */
#define SA1_HOSTOBJ_HOLD_LOCKED    0x0100u  /* HOLDABLE: held elsewhere / not allowed: taskwk.flag 0x1000 (8C06C880 skips it) */
#define SA1_HOSTOBJ_HEAVY          0x0200u  /* HOLDABLE: class 5 instead of 4 (only Big lifts it; 6.1.2) */
#define SA1_HOSTOBJ_POP_ON_TOUCH   0x0400u  /* BALLOON: body contact counts as a hit */
#define SA1_HOSTOBJ_LAUNCH_BOUNCE  0x0800u  /* BALLOON: SetVelocityP only when 8C048980's conditions hold (keeps the ball) */
#define SA1_HOSTOBJ_KEEP_HSPEED    0x1000u  /* BALLOON: keep the character's horizontal speed in the launch */

/* sa1_hostobj_desc grows at its end (struct_size) */
    float    launch_speed;      /* BALLOON: host units/s upward; 0 = the game's ground spring (5 SA1 units/step) */
    float    grab_reach;        /* HOLDABLE: host units added to the body radius for the grab cylinder (0 = 8 SA1 units, min radius 15) */

/* events, new types (sa1_hostobj_event) */
#define SA1_HOSTOBJ_EVENT_LIFTED    6u  /* a character took a HOLDABLE (its pwp->htp is the proxy) */
#define SA1_HOSTOBJ_EVENT_RELEASED  7u  /* thrown / put down / dropped: position, direction, speed (host units/s) */
#define SA1_HITF_LAUNCHED           0x8u    /* HIT on a BALLOON: the character was launched */
#define SA1_RELEASE_THROW           1u
#define SA1_RELEASE_THROW_AIMED     2u
#define SA1_RELEASE_PUT_DOWN        3u
#define SA1_RELEASE_DROP            4u      /* hurt or lost (the character's own drop) */
#define SA1_RELEASE_FORCED          5u      /* sa1_hostobj_release, removal, window */
/* sa1_hostobj_event grows at its end: */
    uint32_t reason;            /* RELEASED: SA1_RELEASE_* */
    uint32_t target;            /* RELEASED (aimed): the host object of the nearest target-list entry, 0 = none */

/* the held state of a character (per step; 6.1.2) */
#define SA1_HOLD_NONE      0u
#define SA1_HOLD_LIFTING   1u
#define SA1_HOLD_CARRYING  2u
#define SA1_HOLD_THROWING  3u
typedef struct sa1_hostobj_held {
    uint32_t struct_size;
    uint32_t object;            /* host object id, 0 = nothing held */
    uint32_t phase;             /* SA1_HOLD_* */
    float    position[3];       /* host space: the proxy's position after the last step (the registered reference point) */
    float    position_prev[3];  /* after the step before: interpolate with the character's alpha */
    float    yaw;               /* the holder's facing, radians (sa1_pose.yaw) */
    float    hand[3];           /* host space: the hand anchor of the kind's hold update (8C0B1DE0 ...) */
} sa1_hostobj_held;
SA1_API sa1_status sa1_character_held(sa1_context *ctx, sa1_character_id id, sa1_hostobj_held *out);
SA1_API sa1_status sa1_hostobj_release(sa1_context *ctx, uint32_t id);  /* the holder drops it (its own drop function) */

/* which kinds a world mirrors (followers, 6.5); default: all */
#define SA1_HOSTOBJ_KIND_BIT(k) (1u << (k))
SA1_API sa1_status sa1_hostobj_world_mask(sa1_context *ctx, sa1_character_id id, uint32_t kind_mask);
```

`sa1_state_ex` tail (after 2.2's additions): `uint32_t held_object` (0 = none). G-SA1 module (3.6 additions):
`ObjAdd` takes `launch_speed` and `grab_reach` as trailing optional numbers; `ObjHeld(handle)` -> object, phase, x, y,
z, px, py, pz, yaw, hx, hy, hz (flat returns, no table); `ObjRelease(id)`; `ObjWorldMask(handle, mask)`; `ObjEvents`
tables gain `reason` and `target`. Lua: `gsa1_hold`, `gsa1_hold_pose`, `gsa1_hold_drop`, `gsa1_hold_deny` network
strings (6.1.3); the 6.3.2 ConVars with their Q-menu controls.

### 6.7 Tests, references and requests for the round 5 agents

`tests/hostobj_test.c` (4.2) gains, for `5-lib-game`:

11. **Lift, carry, throw** (Sonic, flat GRID): a HOLDABLE cylinder (body r 2) 10 units ahead; walk, press the action
    button: mode 21 action 33, at motion frame 12 mode 23 and SE 0x45A, LIFTED, the proxy's `flag 0x1000`, the proxy
    absent from CCL list 4 (no CCL_Entry) and at 8C0B1DE0's position every step; run and press: mode 26 action 60,
    at frame 24 RELEASED (THROW) with P2G(2, 0.8, 0) x 60 x scale; below jog speed: action 63, at frame 12 RELEASED
    (PUT_DOWN), speed 0, pushed out to `p.rad + body radius`.
12. **Aimed throw**: an ENEMY within 100 units: action 59, frame 20, speed 3 per step towards its shape 0 centre,
    `target` = that ENEMY.
13. **Every kind** lifts (Tails, Knuckles, Amy, E-102, Big: Big's speeds 4 and (3, 3, 0)); a sphere-only HOLDABLE is
    not liftable (no side bit); HEAVY: only Big lifts.
14. **Hurt while carrying**: an ENEMY touch -> the kind's drop, RELEASED (DROP) with P2G(1, 1, 0).
15. **Forced release and removal while held**: `sa1_hostobj_release` and `sa1_hostobj_remove` at random steps of a
    carry (1000 runs): the drop function runs first, the proxy is destroyed afterwards, no fault, no stale `htp`.
16. **Balloon**: homing onto a BALLOON 40 units ahead: HIT (HOMING, LAUNCHED), the next step Sonic in mode 9 action 16
    with `mwp->spd == (0, 5, 0)`, then mode 12 action 17; LAUNCH_BOUNCE: `spd.y == v`, mode 8 kept, a second balloon
    can be homed; E-102's laser: HIT (LASER, PROJECTILE), no launch; POP_ON_TOUCH: walking into it launches; one
    launch per character mode family (which modes leave the request pending, 6.2.2).
17. **Follower world mask**: a follower world with ENEMY and HAZARD only: a touch gives HURT (knock-back), rings
    unchanged, never FATAL; its HITs carry the follower's id.

References for `5-oracle` (4.3; sidecar `NAME.obj.bin`, plan 2.6):

* `hold_ec1`: Emerald Coast act 1, Sonic, `--no-objects`, O_SAMPLE_S spawned by a new harness scenario feature
  (`CreateElementalTask(3, 3, 0x8C064980)` 30 units ahead); script: walk, lift, carry, fixed throw, wait for the
  landing, lift again, put down; an aimed-throw variant runs with the stage objects on and an enemy within 100 units
  (which Emerald Coast enemy is reachable: **UNVERIFIED**). Records player state plus the object's taskwk /
  motionwk. libsa1 replays it bit-exact only with the object's C (request below); without it, the
  proxy oracle of 4.3 (the native test exec at 0x7F000100 doing 6.1.2's HOLDABLE steps) is the bit-exact reference
  and `hold_ec1` checks the character side while the object is held.
* `balloon_ec1`: the BALLOON proxy oracle (the native test exec) with a homing attack, a projectile and a touch.
* `spring_ec1`: a real ground spring (8C065800), so the launch half of `balloon_ec1` has a real-object counterpart.

SADC requests (Wave A style, staged first):

1. `8C0586A0` ObjectMovableInitialize (138 B; fatal stub `stubs.tsv:121`): the HOLDABLE proxy calls it (owner rule:
   the game's own initialiser).
2. For `hold_ec1` in libsa1: O_SAMPLE_S `8C064980` (init) and `8C0649CC` (exec), `8C05872A` (the movable step,
   0x2F0 B with its pool; fatal stub `stubs.tsv:122`), `8C058D3C`, `8C058DD4` (small helpers;
   the object land collision `8C056A04` already has C, `code_8C056A04.c`); the range check `8C0919CC` is a "ret 0" stub in LIB today (`stubs.tsv`).
3. Nothing is needed for BALLOON: `8C07C720`, `8C07C676`, `8C109EAC` (a RUMBLE event) and every character's smode 3
   handler are lowered; only `func_8C0940DA`-style SE events and the rumble go to the host.

### 6.8 Owner questions (each with the default this design uses)

* **Q1** Should followers damage NPCs with their attacks? Default no (`gsa1_follower_attack` 0).
* **Q2** Balloons: pop on touch, or only when attacked? Default attacked (`gsa1_balloon_touch` 0); and the launch:
  the game's spring (default, as asked: the spring pose, but no homing until landing) or the enemy bounce that keeps
  homing chains (`gsa1_balloon_launch` 1)?
* **Q3** Big lifting heavy props (the game's own rule for classes 5 / 6) beyond GMod's E-pickup limits? Default no.
* **Q4** Thrown props fly with GMod's gravity (about twice SA1's for small objects). Default GMod physics; an
  "SA1 throw arc" (a counter-force while flying) is possible later.
* **Q5** May the owner's attacks hit his own followers? Default no (`gsa1_hit_own_followers` 0).

### 6.9 UNVERIFIED points carried forward

* Which modes of each character do not call the next-action handler (a balloon launch stays pending there).
* The meaning of `8C08A5B6` / `8C04EFE0` in Amy's goal balloon; whether Sky Deck O_LEVER, Hot Shelter O_KAIDAN /
  0C908780 and Kiki's 8C0FBB00 are liftable in play.
* HEAVY (class 5) behaviour for the non-Big characters (grab and push modes against a host-moved prop).
* GMod's +use limits (35 kg, 128 HU) and the `gmod_balloon` model sizes.
* That the 1.5P AI never presses the action button on the ground (6.5).
* The direct `8C07BBC2(pno)` calls of E-102 and Big for followers (`follower_ai.md` 1.5).

---

## 7. Corrections (2026-10-21, after round 5's game layer and oracle)

Found by round 5's `5-lib-game` (`LIB/tests/hostobj_test.c`) and `5-oracle` (the HOSTOBJ references), written up by the fix
round's leftovers step. Sections 1-6 are left as they were written; **where they disagree with this section, this section
wins**. SADC `src/1st_read` line numbers are as read on 2026-10-21; LIB names are function names.

**C1. Sonic's body levels: the spin dash is ball level 1/1, mode 6 is the light speed dash** (1.4, 2.1, 2.6, 4.2 case 3).
SonicMain writes the levels of the body shape every step (`code_8C0A77BC.c:143-173`, first match): invincibility
(`item & 0x8000`) 3/3; Super Sonic (`equipment & 0x8000`) 3/3; mode 44 (Light Speed Attack) 2/3; **mode 6 1/3**; ball
(`twp->flag & 0x100`) 1/1; mode 45 0/1; else 0/0. Mode 6 is the **light speed dash** (entered by 8C0B1352; its case
follows the id-11 objects, the rings, and gives up after 360 steps: `code_8C0A966C.c:263-300`), not the spin dash's
release. The spin dash is modes 4 (charge) and 5 (the roll it releases: 8C0B11C0, action 14, `twp->flag |= 0x500`,
`code_8C0A966C.c:221-261`), so a spin-dashing Sonic carries the ball's levels, **attack 1 / defence 1**. Mode 45 is not
the light speed dash either (its case only waits for smode 28 / 50, `code_8C0A966C.c:1134-1148`; what it is:
UNVERIFIED). Consequences:
* 2.1, touch levels: 2 hurts unless the character is in the light speed dash (Sonic's mode 6), the Light Speed Attack
  (44), invincible or Super Sonic: **only those have defence 3**. A touch-2 HAZARD **hurts a spin-dashing Sonic**, as it
  hurts a jumping one (4.2 case 3 expected the opposite; hostobj_test's roll case checks the game's rule). Touch 3 does
  not beat defence 3 either (8C02FA20 needs attack > defence), so the light speed dash and the Light Speed Attack
  protect from it as invincibility and Super Sonic do (2.1 named only the last two).
* 1.4's row "mode 6 (spin dash release) 1/3 ... mode 45 (light speed dash) 0/1" reads: mode 6 (light speed dash) 1/3,
  modes 4 / 5 (spin dash) through the ball rule 1/1, mode 45 (UNVERIFIED) 0/1.
* 2.6: `SA1_ATTACK_SPIN_DASH` (2) is Sonic's modes 4 and 5 **and** his light speed dash (mode 6), tested before the ball
  rule (LIB `sa1_world_hostobj.c` classification; the `libsa1.h` comment of the constant). The light speed dash has no
  kind of its own.

**C2. Hurt while carrying: the drop gives no speed** (6.1.1 "Drop", 6.7 case 14). The drop functions clear the
character's 0x800 **before** they ask the class (Sonic's `func_8C0B1D60`, `code_8C0B1C56.c:106-126`; 5-lib-game found
the same order in the other characters' drops). `func_8C06C880` then takes its candidate branch, which needs the hit and side bits and **no hurt flag 4**
(`code_8C06C880.c:80-82`): on a hurt the class is 0 and the P2G(1, 1, 0) speed is never written. The object is let go
with the speed it has (0 while held): `SA1_HOSTOBJ_EVENT_RELEASED` with `SA1_RELEASE_DROP` carries speed 0 (6.7 case 14
expected P2G(1, 1, 0)). The oracle saw the same for a forced release: in `hold_ec1` (step 430) the original drop gave
speed 0. The speed is written only when the character is not hurt and is in side contact with a liftable object at
the drop (the class is then that object's).

**C3. E-102's time bonus at a KILLED removal** (1.7, 2.4 "Removal"). The layer releases E-102's marker at the hit (2.4
step 1), so the hooked dest 8C0D32C0 finds no marker when the proxy is destroyed and would pay nothing. LIB pays the
bonus itself: at a KILLED removal of a counted, hooked proxy (not NO_BONUS, not NO_LOCKON) it calls 8C0D5064 with the
shot group remembered at the hit (or `8C0D5064(NULL)` when that group is no longer the running `D_8C423048`), then
clears the enemy work's 0x1000 so that the dest does not pay a second time. Three counted targets pay 1 + 2 + 3 = 6, as
the game does with the marker still attached; SILENT, NO_BONUS and NO_LOCKON pay nothing.

**C4. The other characters' damage checks** (4.1, "to verify with new reference traces"). 8C0BBA4E, 8C0C5680, 8C0DAC46
and 8C0E3EC0 are chunk (file) names. The damage checks are Tails `func_8C0BBAE2` (`code_8C0BBA4E.c`), Knuckles
`func_8C0C57C8` (`code_8C0C5680.c`), Amy `func_8C0DB000` (`code_8C0DAC46.c`) and Big `func_8C0E40CE`
(`code_8C0E3EC0.c`); Sonic 8C0AFB10 and E-102 8C0D04A0 are right.

**C5. Death events** (2.5, 3.3). Worlds built through the character API now emit `SA1_EVENT_DEATH` whenever
`playerwk.item` 0x4000 appears, with arg `SA1_DEATH_OTHER` 0 (drowning, a death zone, a kill plane ...) / `SA1_DEATH_HURT`
1 (a hit with no rings: HURT with `SA1_HURT_FATAL` comes first, in the same step) / `SA1_DEATH_HOST` 2
(`SA1_DAMAGE_KILL`), and id = the credited object.

**C6. Observations of round 5 (no design change; for hosts and later rounds).**
* The homing attack gives up on a target above the character when the attack starts (8C0AF280: v.y > 0 with
  `free.w[2] < 0`): a balloon or an NPC proxy above the player is not homed onto from below. Game behaviour.
* Proxies never reach the cylinder x cylinder test 8C030540 or the capsule test 8C030F20 (their 0xE0000 attr bits skip
  ids 2-7), so the HOSTOBJ references do not cover those two NON_MATCHING shape tests of 4.1.
* No reference executes E-102's impact sphere 8C0D65BA (the marker is released at the hit; why the impact sphere does
  not appear: UNVERIFIED).
* GRID seams and the lift pull (6.1.1): for Tails, Knuckles, Amy and E-102 the pull of 8C06CB40 (`taskwk.pos` moved 1
  unit per step, no speed) loses the ground for one step on a GRID seam (the corner of four 1024-HU tiles: y +0.041,
  speed 0, attr 0), and their lift modes abort into a fall; Sonic and Big are not affected. Whether the original does
  the same on its own seams: UNVERIFIED. A host can shorten the pull with a smaller `grab_reach`.
* In hostobj_test, releasing a charged light speed dash went to mode 18 (an order-35 request): the light speed dash was
  not exercised against proxies (cause not investigated).
* The real-enemy reference of 4.3 is a Rhinotank (`obj_ec1_kill`, player state only), not a Kiki or an Unidus: Emerald
  Coast act 1's object list has no E_UNI and its nearest Kiki sits on a ledge.
