# G-SA1 1.x: badniks and bosses (v1_enemies)

Design, 2026-10-21. Read-only research (workflow `gsa1-design-v1`, topic `enemies`); nothing here is implemented.
Companions: `v1_arch.md` (the object architecture this doc plugs into: SET layer, actors, arena worlds, the G-SA1 tab;
read it first), `v1_objects.md`, `v1_triggers.md` (splines), `v1_npcs.md`, `v1_roadmap.md`. Builds on
`docs/libsa1_interaction.md` (round 5 host objects; its 1.6 describes SA1 enemies), `docs/research/follower_ai.md`
(proxy-leader worlds), `docs/research/sa1_code_map.md`, `docs/research/sa1_sound.md`.

Roots: `SADC` = `<project>\sadc`, `LIB` = `...\libsa1`, `GSA1` = `...\g-sa1`. Addresses are 1ST_READ
(US 1.005) unless an overlay is named; overlay addresses are the overlay's own `0x0C9xxxxx` pointers. Units: SA1 units
(4 HU each in G-SA1's Source preset); "step" = one 60 Hz logic step. "C" = a C definition exists (matched or
NON_MATCHING, both lowered into libsa1); "asm" = still assembly. Scratch, scripts and RAM dumps:
`SADC/build/research/enemies/` (appendix).

Decomp status was measured with a nested-`#if`-aware variant of `tools/progress.py`'s rules (`status2.py`):
progress.py's NON_MATCHING regex stops at the first nested `#else` (e.g. `code_8C0522C0.c` line 71), so it under-counts
NON_MATCHING code. Whole game today: 4,119 functions, 973,430 B; matched 1,217 functions / 218,136 B, NON_MATCHING 393 /
291,458 B.

---

## 0. Summary

* **Catalogue (measured).** 12 badnik types run from 1ST_READ (9 species: Kiki, Rhinotank, Sweep, Boa-Boa, Leon, Egg
  Keeper, Unidus A/B/C (C = the fire variant), Spinner A/B/C (C = Electric Spinner)); 5 more live in stage overlays
  (Buyon STG03, Cop Speeder STG04, the Kiki aircraft STG06, Ice Ball = the "Snowman" STG08, Beat from the Final Egg
  gachapon STG10). The E-100 series: E-100 Zero (Amy's chaser, 1ST_READ), E-101 Beta and mkII (boss overlays B_E101 /
  B_E101_R), E-103 Delta / E-104 Epsilon / E-105 Zeta (Gamma's in-stage bosses in STG02 / STG05 / STG12). Bosses: 11 boss
  levels (15-25: Chaos 0/2/4/6, Perfect Chaos, Egg Hornet, Egg Walker, Egg Viper, Zero, E-101 Beta, E-101 mkII) and 6
  character rival fights driven by the game's own boss AI (AI Sonic / Knuckles / E-102). There is no "Sharky": the
  shark is a Big fishing target and the Emerald Coast orca is a scripted pier chase (STG01 code), neither a badnik.
* **Decomp status.** Every badnik's code is still assembly: 58,424 B in 6 TUs for the 12 1ST_READ types, about 6.4 KB of
  shared helpers (the enemy common library, the explosion effect), 38 KB for the stage-overlay badniks. Already C: the
  game's damage check and bounce (8C0489E4 / 8C048980), score, the animals (all 8,932 B), the debris / sparks, the SET
  "gone" call, E-100 Zero's main and exec (12.3 of 17.7 KB) and the character-rival AI (10.1 of 11.6 KB). The 11 boss
  overlays hold 270.5 KB of asm (whole overlays, arena objects included) plus 14.7 KB of shared Chaos / boss-gauge code
  in 1ST_READ.
* **How they target (measured in code).** The enemy AI is single-target: every distance / angle helper is called with
  player number literal 0 (19 call sites in 8C0F2BA0-8C107800), and the wake-up test 8C04712A looks at `playertwp[0]` and `[1]` only (150 units
  within +-80 degrees of the facing, or 35 units anywhere). All 11 boss overlays import `playertwp` (B_8C78C608) and read
  slot 0. libsa1's one-character-per-world model therefore matches SA1's own single-player situation exactly.
* **How they die.** The game's own path, all C except the effects: `twp->flag & 4` from the CCL pass -> 8C0489E4 (squash,
  "killed by a player", score +10 into the 120-step chain, bounce 8C048980 when the attacker is player 0/1 in a ball and
  airborne) -> the enemy's death mode: explosion 8C045736 (asm), debris 8C0486A0, sparks 8C0487B6, an animal of the
  stage's set (8C015720, table D_8C1623D0[level]), then 8C091BA4 (the SET record is gone for good). No rings. E-102's lock
  hook gives the time bonus (8C0D5064).
* **Recommendation for badniks** (consistent with `v1_arch.md`: badniks are ACTORs): each badnik runs as the game's own
  SET object in exactly one character world (its owner's: the nearest active character), with zero latency and exactly
  SA1 behaviour; the other characters' worlds see a round-5 ENEMY proxy and its projectiles as HAZARD proxies; remote and
  GMod-weapon hits come back as v1_arch's ATTACKER injection. **First wave: all 12 1ST_READ badniks** in one
  decompilation wave (about 65 KB; start with the Rhinotank, which already has an original-game kill trace
  `harness/tests/sim/obj_ec1_kill.txt`, then Kiki and the Spinners). Hostility to plain GMod players / NPCs: touch damage
  from the badniks' own attack shapes first (cheap, server-applied), "hunting" them with the game's AI later through
  proxy-target worlds (a 16 MB world per hunted entity, follower-style).
* **Recommendation for bosses** (three tiers): (R) **arena-free now**: the six character rival fights (the game's AI
  player in slot 1 of the fighter's own world, exactly the game's set-up; about 3.5 KB left to decompile) and E-100 Zero
  as a chaser (5.4 KB left). (A) **arena bosses** in *arena worlds*: the fighter's world is rebuilt as the boss level
  (the B_* image, the arena's own landtable as DISC collision, the level task), and G-SA1 draws that arena at an anchor in
  the map ("arena pocket"); measured in the original game (harness): levels 15-25 boot and run 300 steps except Egg Viper
  (22), which faults in frame 1. Order by size and risk: E-101 Beta (9 KB), Egg Hornet (16 KB), Chaos 0 (19 KB + the
  shared Chaos code), Chaos 2, E-101 mkII, Chaos 4, Zero, Egg Walker, Chaos 6, then Egg Viper, Perfect Chaos last (needs
  Super Sonic, Tikal and an arena about 24,000 x 41,500 HU, beyond Source's 32,768 HU world at G-SA1's scale). (S)
  **stage-pack bosses**: E-103 Delta and E-104 Epsilon initialise like badniks (home = spawn point) and should work in open
  space once their stage pack is loaded; E-105 sits in its Hot Shelter pit.

---

## 1. Catalogue

### 1.1 Badniks in 1ST_READ (code, data, placements)

Exec addresses from the object lists (`reference/dc_sa1_known_symbols.csv`, cross-checked in the binaries). SET
placements are counted in the disc's SET files (`build/research/objects/set_usage.json`, spot-checked: `SET0100S.BIN`
type 30 = 2 Rhinotanks of 788 records, `SET1002S.BIN` type 56 = 101). "Own" = bytes reachable only from that exec in
the enemy region (`closure.py`); all of it is asm.

| Badnik (list name) | Exec | Placements (SET files) | Code (own) | Models / motions (1ST_READ data, SA Tools labels) | Textures (disc PRS, texlist) |
|---|---|---|---|---|---|
| Kiki (E SARU) | 8C0FBE04 | 253: EC 1-3, TP, RM, HS (100 in SET1202E) | 39 fns, 11,374 B (TU 8C0FAD60) | `Kiki` 8C5BB0A0, tail 8C5BB22C, 6 motions 8C5BC554-8C5C0F98; `Bomb` 8C5C1740, `Spikeball` 8C5C24D0 | E_SARU 11,072 B (8C5B8264), E_BOMB 821 (8C5C0FC4), TOGEBALL_TOGEBALL 3,388 (8C5C178C) |
| Rhinotank (E SAITO) | 8C0F3EAA (init 8C0F2C44) | 15: EC, WV, IC | 26 fns, 4,424 B (TU 8C0F2BA0) | `Rhinotank` 8C59C12C, jets 8C59D24C, walk / attack motions | E_SAI 24,888 (8C599490) |
| Boa-Boa (E HEVY = hebi) | 8C0F4ED6 | 14: WV, LW, IC | 22 fns, 3,512 B (TU 8C0F2BA0) | head 8C59DEBC, body short / long, tail 8C59F76C | E_SNAKE 16,529 (8C59D544) |
| Sweep (E AMEMB = amenbo) | 8C0F5040 | 7: EC2, TP2 | 21 fns, 2,948 B (TU 8C0F5040) | `Sweeper` 8C5A2E6C, 3 motions | E_AMENBO 37,780 (8C59F8B4) |
| Leon (E LEON) | 8C0F5D40 | 46: WV, LW, HS | 22 fns, 7,524 B (TU 8C0F5D40) | body / head / tongue / tail 8C5ADCFC-8C5AF5D0, 3 motions | E_LEON 28,565 (8C5B3E18) |
| Egg Keeper (E ROBO; also "E HIYOKO" in FE's list) | 8C0F8E10 | 25: FE, HS | 51 fns, 10,622 B (TU 8C0F5D40) | `Egg_Keeper` 8C5B75CC + animation set 8C5B7EA0 | E_ROBO 27,127 (8C5B3FC4) |
| Unidus A (E UNI A) | 8C1006D4 | 11: SH, SD | 27 fns, 4,086 B (2,260 own) | `Unidas` 8C5C3700 | UNI_A_UNIBODY 3,539 (8C5C268C) |
| Unidus B (E UNI B) | 8C100A96 | 9: SH, SD, CP | 26 fns, 4,104 B (2,278 own) | same | same |
| Unidus C, fire (E UNI C) | 8C100E20 | 21: RM, LW | 28 fns, 4,608 B (3,588 own) | `Gola` 8C5C47CC, `Gola_Fireball` 8C5C4ECC (SA Tools names) | UNI_C_UNIBODY 3,760 (8C5C3758) |
| Spinner A (SPINA A) | 8C102AD4 | 81: SH, RM, SD, CP, FE | 15 fns, 2,602 B (1,142 own) | `Spinner` 8C5C68BC | SUPI_SUPI 19,783 (8C5C50A0) |
| Spinner B, "BIRIBIRI NASHI" (no electricity) | 8C102C04 | 142 | 15 fns, 2,462 B (1,002 own) | same | same |
| Spinner C, "BIRIBIRI ARI" = **Electric Spinner** | 8C102D2A | 50 | 15 fns, 5,074 B (3,614 own) | same | same |

TU totals (all asm): Rhinotank + Boa-Boa 8C0F2BA0-8C0F5040 51 fns 8,192 B; Sweep 8C0F5040 21 / 2,948; Leon + Egg Keeper
8C0F5D40-8C0FAD60 74 / 18,152; Kiki 8C0FAD60-8C0FDEE0 39 / 11,374; Unidus 8C0FDEE0-8C101020 55 / 10,500; Spinner
8C101020-8C102E80 24 / 7,258: **264 functions, 58,424 B**. The models, motions, CCL tables and texlists of all of them
(and E-100 Zero's) sit in 1ST_READ data 0x8C599250-0x8C5FBB60 (about 404 KB), ripped at run time like the characters';
only the PVM archives above have to be read from the disc.

Object-list entries (from the stage lists `D_8C1C143C[level * 8 + act]`, `OBJ_ITEMENTRY` per `v1_arch.md` 1.1): every
badnik is `im 2` (taskwk only), task list 3; spawn distance 600 units for Kiki / Sweep in EC, 200-600 elsewhere, default
400 otherwise; Spinners use flag 4 ("spawned at the first pass").

**SET parameters** (the three floats of a record become `twp->scl`; the meanings below are the game's own debug "edit"
labels printed by each type's edit display, `reference/strings_1st_read.txt`):

| Type | Edit labels (function) | Notes |
|---|---|---|
| Kiki | `POSITION`, `00STRONG FFWEAK&EME`, `[+ NOCOLLI][- COLLI]`, `[+ BOMB][- TOGE]`, `RADIUS` (8C0FC76C) | "sclY >= 1000.0 DE BOMB NAGEMASEN": with scl.y >= 1000 it throws nothing; the sign picks bomb or spike ball |
| Leon | `ROT Y`, `EMERALD`, `SEARCH RANGE`, `MOVE RANGE` (8C0F7160) | two layouts |
| Unidus | `ROT Y`, `EMERALD`, `MOVE RANGE`; "1000<sclz NOCOLLI" (8C0FFC40, 8C0FFFDC) | |
| Spinner A | `OBJECT ANG`, `EMERALD`, `SEARCH RANGE` (8C10183A) | |
| Spinner B / C | `PHASE`, `OBJECT ANG`, `SPEED&EME`, `X WIDTH`, `Y WIDTH`, `Z WIDTH` (8C1019B0 / 8C101BB4) | patrol box; C has the electric field |
| Rhinotank | `EDIT` (8C0F3D34) | parameters UNVERIFIED |

`EMERALD` makes the enemy carry one of Knuckles' emerald shards (drop code 8C0C9BE2 / 8C0C9C9A, asm, 17 + 10 callers):
G-SA1 keeps it off.

**CCL shapes** (the tables passed to `CCL_Init`, dumped from the user's 1ST_READ by `ccl_dump.py`; levels are
attack / defence as in `libsa1_interaction.md` 1.2; the table length is not known from the call, rows past the real
count are omitted; Rhinotank, Kiki, Egg Keeper, Unidus and Zero agree with the interaction doc's 1.6):

| Type | Table | Shapes |
|---|---|---|
| Rhinotank | 8C5992B8 | body cylinder r 8.5 h 8.5 touch 1 armour 0, solid; horn sphere r 4.5 touch **2** armour **1** (a rolling player is hurt by the horn; breaking it needs attack 2) |
| Boa-Boa | 8C59D280 / D2B4 / D384 / D420 (segments) | chains of spheres r 5-7 touch 1 armour 0; one capsule r 6 len 8 touch 2 armour 3 (role UNVERIFIED) |
| Sweep | 8C5AAE3C | cylinder r 13.5 h 4 touch 1; inner cylinder r 8 touch 0 |
| Leon | 8C5B3E20 | body cylinder r 10 h 8.5 touch 1; tongue capsules r 2 touch 1 |
| Egg Keeper | 8C5B7EB4 (+ 8C5B7F1C) | sphere r 9 touch 1; attack cylinder r 22 h 11 off until it attacks, touch 1 armour 2; guard cylinders armour 3 |
| Kiki | 8C5C2504 (+ 8C5C2538 projectiles) | sphere r 4.5 touch 1; bomb r 5 touch 2; spike ball r 3.5 touch 2 armour 1 |
| Unidus | 8C5C4F00; spikes 8C5C4F34 (CCL id 4, 8C0FE568) | body sphere r 4.5 touch 1; spikes r 3.5 touch 2 armour 1 |
| Spinner | 8C5C68F0 | blade cylinder r 11.2 h 0.7 touch 1; core cylinder r 6 h 6 touch 1 solid; field sphere r 20 touch 2 armour 2, off (Spinner C's electricity, UNVERIFIED) |

### 1.2 Badniks in stage overlays

All load at 0x0C900000 with their stage (models, motions and texlists are overlay data). Own code by closure from the
exec (`ovlclosure.py`), all asm.

| Badnik | Overlay, exec | Placements | Code (own) | Models (SA Tools) / textures | What it needs |
|---|---|---|---|---|---|
| Buyon | STG03 0C909E9E | 8 (TP2/3) | 9 fns, 3,828 B | `STG03_Buyon_*` 0x0C9C47C8-0C9C6264; E_BUYON 60,827 B (0C9C3690) | own damage code (calls the score directly), SetVelocityP on the player |
| Cop Speeder (E POLICE) | STG04 0C90C160 | 53 (SH) | 23 fns, 11,102 B | `STG04_Cop_Speeder_*` 0x0CA79F0C / 0CA7BFBC; NISEPAT 27,750 (0CA79758) | **a path**: calls the path setup 8C089AA0 (C); which stage path a record selects is UNVERIFIED. A G-SA1 spline (`v1_triggers.md`) could feed it |
| Kiki aircraft (E ACLIFT) | STG06 0C90826A | 1 (SET0601M) | 32 fns, 5,996 B | `STG06_Aircraft_*` 0x0CADA2F8-0CAE1CE4 (missile, Kiki pilots, lifts, radar); E_AIRCRAFT 45,138 | in-game name UNVERIFIED |
| Ice Ball (E SMAN, texture E_SNOWMAN) = the "Snowman" | STG08 0C90FF40 | 6 (IC 1-2) | 43 fns, 8,468 B | `STG08_Ice_Ball_Driver` 0CABEF20, `_Tank` 0CAC08EC; E_SNOWMAN 38,162 | SetInputP on the player (an ordered player state; which one UNVERIFIED) |
| Beat (E GACHA) + gachapon (O GACHA, COLLI GACHA) | STG10 0C90C7F0, 0C90F540, 0C90E058 | 101 Beat, 12 machines, 11 colliders (SET1002S) | 35 fns 6,452 B + 14 fns 2,150 B | `STG10_Beat` 0CB021B4 (+2 LODs); GACHAPON 14,545 | the machine dispenses Beats |

Not badniks (left out of the catalogue's spawnables): E CART (STG03 0C90C8C4, 24,554 B: enemy carts on Twinkle Park's
track, needs the cart and track; calls KillPlayer / RingScatter), MINICART's E_Cart (Twinkle Circuit opponents), STG05
O ZAKO / O ZAKO_B / Mtn_SpiderA/B / O_Bat (no CCL, no enemy calls: scenery), STG12 O_KURAGE (scenery), Sky Chase's BOSS /
ZAKO1 (the Tornado shooter), the Emerald Coast **orca** (STG01 0C9091C0-0C909EB2, about 3.3 KB, triggered by the SET
objects O_AO_SUMMON / O_AO_KILL / O_PO_SUMMON: a scripted chase along the pier).

### 1.3 The E-100 series

| Robot | Where | Code | Status | Notes |
|---|---|---|---|---|
| E-100 Zero (chaser) | 1ST_READ 8C102E80-8C107800; created by 8C107744 (from Amy's code 8C0DDFB4 / 8C0DE022 and the triggers EROB_COLLI 8C0A2680 (49 placements: TP, FE, HS), STG10 C_APP_ZERO / C_KILL_ZERO); texture load 8C107700 ("EGGROB") from AmyMain | 52 fns, 17,716 B | main 8C102EAE and exec 8C1032AC (NON_MATCHING, 4,892 B) are C: 5,138 B matched + 7,180 NM; **5,398 B asm left** (draws 8C104DBC, 8C105036 ..., event variants 8C107020 / 8C107102) | model `E_100_Zero` 8C5CE6E4 + 21 motions; EGGROB 62,508 B. Knocked back (not killed) by flag 4; damage accumulates in B_8C7A52D0 (interaction 1.6). Catching a non-Amy player: UNVERIFIED |
| E-101 Beta | B_E101 (level 24) | whole overlay 51 fns, 9,054 B | asm | boss, 1.4 |
| E-101 mkII | B_E101_R (level 25) | 153 fns, 25,188 B | asm | boss, 1.4 |
| E-103 Delta | STG02 0C901440 (list type 31, SET0200E: 1) | 57 fns, 10,448 B | asm | models `STG02_E_103*` 0C9F3D00-0CA12858; WINDY_E103 113,749 B; calls EnemyInitialize (home = spawn point), the land gather 8C0522C0, the boss gauge, the bird release 8C0158B6 |
| E-104 Epsilon | STG05 0C900BA0 (type 89, SET0501E: 1) | 57 fns, 10,520 B | asm | `STG05_E_104*` 0CAB6900-0CAD53E8; MOUNTAIN_E104 122,782 B; same calls as E-103 |
| E-105 Zeta | STG12 act 2 list type 91 "E E105", exec 0C9142E0 (list 2) | about 40 fns, 6,578 B (range 0C9142E0-0C916600) | asm | `STG12_E_105_*` 0CA927F8-0CA9F8E4; E105 113,657 B; a turret boss in a pit (UNVERIFIED how much of the act it needs) |

E-103 / E-104 / E-105 old-model archives (E103OLD / E104OLD / E105OLD.PRS) belong to the cutscenes.

### 1.4 Bosses (boss levels)

Level -> overlay from the stage loader 8C09CE90's switch (Ghidra), level task from `D_8C19DE30[level]`, sky /
background task from `D_8C19E0E0[level]`, SET / CAM files from the disc. Code = the whole overlay (it includes the
arena's own objects). Arena = bounding spheres of the landtable's solid COL entries (an over-estimate), x 4 for HU.
Boss main = the overlay task alive after 300 idle steps in the original game (harness, 3.1).

| Level | Boss (fighters in the game) | Overlay: image / PRS | Code (fns, B) | Level task; boss main (measured) | Arena (landtable in the overlay) | Assets on the disc | Needs from its world |
|---|---|---|---|---|---|---|---|
| 15 | Chaos 0 (Sonic) | B_CHAOS0 392,812 / 192,876 | 75, 19,126 | 0C900000; 0C9023CE | `landtable0b00` 63 COL, 16 solid, 1,697 x 1,697 (6,800 HU) | CHAOS0*.PRS, CHAOS_*.PRS (shared), SET1500S (lamp posts O_POLE, rings), CHAOS0.MLT, CHAOS.ADX | the lamp posts it jumps onto (SET objects), rain, police cars / helicopter / clock (arena objects), 4 camera calls (stubbed) |
| 16 | Chaos 2 (Knuckles) | B_CHAOS2 469,772 / 235,939 | 71, 24,648 | 0C900080; 0C902ED6 | `landtable1600` 1,697 x 1,697 | CHAOS2*.PRS, SET1600S (16 tables) | 8 camera calls |
| 17 | Chaos 4 (Sonic, Tails, Knuckles) | B_CHAOS4 383,276 / 215,553 | 120, 25,422 | 0C900000; 0C900E20 (UNVERIFIED as the main) | `landtable1700` 745 x 745: **a pond** | CHAOS4_*.PRS (NUMA swamp water, lily pads, waves), SET1700S | water and the lily pads; the land gather 8C0522C0 |
| 18 | Chaos 6 (Sonic / Big act 0, Knuckles act 1) | B_CHAOS6 1,644,776 / 854,296 | 102, 27,112 | 0C900000; 0C902598 | `landtable1800` / `1801` 3,000 x 3,000: the Egg Carrier deck | CHAOS6*.PRS (EISEI ice bombs, EGGMAN), SET1800S / B, SET1801K | frogs / ice bombs per character, deck edges |
| 19 | Perfect Chaos (Super Sonic) | B_CHAOS7 1,487,956 / 873,457 (+ TIKAL_PROG) | 191, 33,604 (5,840 C: Super Sonic support) | 0C900000 | `landtable1900` 305 COL, 5,983 x 10,377 (**about 24,000 x 41,500 HU**) | CHAOS7_*.PRS, SET1900S, **CAM1900S**, CHAOS7A/B.MLT, CHAOS_P1/P2.ADX | Super Sonic (round 6a), Tikal, the path work, the camera file; the harness reference `super_pc_play` exists |
| 20 | Egg Hornet (Sonic, Tails) | B_EGM1 421,244 / 270,893 | 137, 16,016 | 0C900000; 0C9006C0 | `landtableegm1` 1,418 x 1,074 | EGM1*.PRS (LAND, SORA, MIS, JET ...), MOBILE1.MLT | flies around the arena; 3 camera calls |
| 21 | Egg Walker (Tails) | B_EGM2 598,392 / 255,593 | 129, 27,892 | 0C900000; 0C902D2E (UNVERIFIED) | `landtableegm2` 559 x 760 (the street; the casino buildings are objects) | EGM2_*.PRS, MOBILE2.MLT | the breakable buildings |
| 22 | Egg Viper (Sonic) | B_EGM3 364,836 / 166,677 | 178, 39,348 | 0C900000 | `landtableegm3` 2,022 x 2,022, a pit to y -50,528 | EGM3*.PRS, MOBILE3.MLT | the floor tiles it destroys, 3 event-system calls; **faults in the original-game harness at frame 1** (3.1) |
| 23 | Zero (Amy) | B_ROBO 197,224 / 112,534 | 182, 28,946 | 0C900000; 0C902740 (UNVERIFIED) | `landtablerobo` 828 x 772 (the deck, E101R_TIKEI textures) | EGGROB, EROBO.PB / .NB, EGGROBO.ADX | 8 char-boss-AI helper imports, electric fences |
| 24 | E-101 Beta (Gamma) | B_E101 266,732 / 140,647 | 51, 9,054 | 0C900000; 0C90140A | `landtable2300` 536 x 545 | E101.PRS, E101_TIKEI, E101.MLT, BOSSE101.ADX | the smallest boss |
| 25 | E-101 mkII (Gamma) | B_E101_R 188,936 / 108,131 | 153, 25,188 | 0C900060; 0C902EC0 (UNVERIFIED) | `landtable2400` 828 x 772 (same deck as Zero) | E101R*.PRS / .NB / .PB, E101R.MLT | |

Every boss overlay imports the shared boss gauge 8C0F1EE6 / 8C0F234E (asm; the gauge task 8C0F1C40 is alive in every
boss level measured), `GetStageNumber`, 8C04F6A0 (returns the byte B_8C7493CA; meaning UNVERIFIED, set per fight) and
3-8 camera functions (8C01937C, 8C01ABDC, 8C0208E0, 8C020AE0, 8C01AD2C ...; libsa1 stubs the camera hooks, design 10.5).
The Chaos fights also use the shared Chaos code 8C0EEAA0-8C0F1C40 and 8C0F2480-8C0F2BA0 (76 fns, 12,714 B, asm; e.g.
the 8C0EEAA0 drops: 89-147 live tasks in Chaos 0 / 2 / 4). Total: **270,516 B asm in the 11 boss overlays** + 14,674 B
in 1ST_READ (Chaos code + gauge / name 8C0F1C40-8C0F2480, 1,960 B).

### 1.5 Character rival fights (1ST_READ, the game's own AI player)

Strings at 0x8C154A04: "BOSS SONIC vs KNUCKLES", "SONIC vs E102", "KNUCKLES vs SONIC", "KNUCKLES vs MILES", "E102 vs
SONIC", "E102 vs MILES": the AI fighter first, the player second (the AI kinds are exactly the three the creator
8C083F7C can make: SonicMain / KnucklesMain / E102Main as player 1). A SET-side request (B_8C79A39C, filled by
8C09072C) reaches the dispatcher 8C083A1E (asm, 98 B), which starts the AI 8C082D60 (Sonic), 8C084260 (Knuckles) or
8C084AE0 (E-102, which also creates the "rival target" 8C085532 for Gamma's lock-on). The AI writes player 1's pad
(key masks D_8C19E38C / 8C19E390) as the follower AI does. Region 8C082D60-8C0869E0: 58 fns, 11,646 B, **2,580
matched + 7,494 NON_MATCHING, 1,572 B asm (7 fns)**. The fighters are full characters (all C), so these fights need no
arena: they happened in adventure fields.

### 1.6 Shared support code

| Piece | Range | Status | Used by |
|---|---|---|---|
| Enemy common library (SADX `Enemy*` analogue, same order: 8C047040 EnemyInitialize (C, 0x23C enemy work), 8C04712A EnemySearchPlayer, 8C047260-8C047680 + 8C047D5A ground / wall / shadow (EnemyCheckGroundCollision-like; via the land gather 8C0522C0), 8C047D7E / 8C047D90 / 8C047DA2 preserve home / previous / back, 8C047E10-8C048060 aim / home / player distances and angles (8C047FCE EnemyDist2FromPlayer, 8C047FFC EnemyCalcPlayerAngle), 8C0480C6, 8C048160 (camera-relative aim), 8C0486A0 debris, 8C0487B6 sparks, 8C0488A2 squash, 8C048980 EnemyBumpPlayer, 8C0489E4 EnemyCheckDamage) | 8C047040-8C048B00, 39 fns, 6,398 B | 1,052 matched + 278 NM; **33 fns 5,068 B asm**, of which the badniks need 21 fns, about 3.7 KB | all badniks, E-103/104, several bosses |
| Sprite effects (explosion 8C045736, 11 badniks; 8C04536E, 8C0456F4, 8C045A70, 8C04666A, 8C0467E0 and their helpers) | 8C0445A0-8C047040 | 902 B matched, 7,516 asm; badniks need 13 fns, about 1.7 KB | all badniks |
| Animals (8C015720 random animal of the level's set D_8C1623D0[level], 8C015798 / 8C0158B6 fixed ones, the 15 animal kinds) | 8C015720-8C017B20, 46 fns, 8,932 B | **all C** | every death |
| SET "gone" 8C091BA4, condition bits 8C091B8A | | C | deaths, E-102 |
| SET range-out with radius 8C0919E8 (+ 8C09111A) | | asm, 216 B | 8 badniks |
| Kiki's vertex-buffer draw 8C09F080-8C09F380 (calls the renderer 8C03700C, served by `LIB/src/rt/rt_draw.c`) | | asm, 706 B | Kiki |
| Score 8C08A2A2 (+10, chain 8C08A272), bounce 8C048980, E-102 bonus 8C0D5064 / release 8C0D5D90 | | C | deaths |
| Bomb item test 8C05E5C2 (every enemy dies in the bomb box's sphere) | | asm, libsa1 "ret 0" | 8C0489E4 |
| Boss gauge / name 8C0F1C40-8C0F2480 | 7 fns, 1,960 B | asm | all bosses, E-103/104/105 |

Stubbed or provided by libsa1 already (no decompilation needed for enemies): the SE calls 8C0940DA / 8C09413A / 8C094330
/ 8C0943E0 (sound events), the C runtime (rand 8C10D334, atan2f 8C10D0F8, ...), every Ninja function the badniks call
(26 math + njSetTexture, njColorBlendingMode, njDrawSprite3D, njPrint: all in `LIB/src/rt`), 8C04FE40 (game-mode test,
`ret 0`), 8C049A4E (event running, `ret 0`), 8C0919CC (range-out, `ret 0`), the renderer 8C03700C / 8C038D00 (rt_draw).

### 1.7 The names the owner listed

| Owner's name | In the game |
|---|---|
| Kiki, Rhinotank, Spinner, Leon, Boa-Boa, Egg Keeper, Sweep, Unidus | 1.1 (Boa-Boa = E HEVY, Sweep = E AMEMB) |
| Electric Spinner | Spinner C (SPINA C, "BIRIBIRI ARI") |
| Cop Speeder, Buyon | 1.2 (STG04, STG03) |
| Ice Ball, Snowman | **one badnik**: E SMAN in STG08, texture E_SNOWMAN, models "Ice Ball Driver / Tank" |
| Sharky? | none: the shark (`STG01_Shark`, `same_s_hontai`, texture SAME) is a Big fishing target; the Emerald Coast orca is a scripted chase (1.2) |
| E-100 series as enemies | 1.3 |
| Chaos 0/2/4/6, Perfect Chaos, Egg Hornet / Walker / Viper, E-101 Beta / mkII, Zero | 1.4 |

---

## 2. How SA1 enemies work (code facts)

### 2.1 Spawn

The SET manager (8C091148) creates each record's task with `CreateElementalTask(entry.im, entry.list, entry.exec)`,
`tp->ocp` = the record's `OBJ_CONDITION`, `twp->pos` / `ang` / `scl` = the record's position / rotation / three
parameters (`v1_arch.md` 1.1). The badnik's exec then calls EnemyInitialize 8C047040 (enemy work 0x23C into `tp->mwp`:
home = `twp->pos` at +0x50; view half-angle 0x38E3 = 80 degrees at +0x1D4, view range 150^2 at +0x1D8, near range 35^2
at +0x1DC) and `CCL_Init(tp, table, 3, ...)` (id 3: enemies; Unidus spikes id 4). It calls `CCL_Entry` 8C02F63C every
step.

### 2.2 Finding and targeting the player

* **Wake-up**: EnemySearchPlayer 8C04712A loops `playertwp[0]` and `playertwp[1]` (Ghidra: the loop counter stops after
  1) and returns 1 when a player is within the view range and +-80 degrees of `twp->ang.y`, or within 35 units at any
  angle. It returns a boolean, not which player.
* **Chasing and aiming**: EnemyDist2FromPlayer 8C047FCE and EnemyCalcPlayerAngle 8C047FFC take a player number; all 19
  call sites in the enemy code pass the literal 0 (Ghidra pseudo-C of 8C0F2BA0-8C107800). 8C048160 aims around
  `playertwp[pno]` with the camera's rotation (`DAT_8C18B7EC`, the camera work).
* **Bosses**: all 11 boss overlays import `playertwp` (B_8C78C608); E-100 Zero chases player 0 as well.
* **Consequence**: SA1 enemies are single-target, and in libsa1's model (one character per world, player 0) a badnik
  living in a character's world behaves exactly as in the game. A badnik cannot pick between several characters by
  itself; choosing its target = choosing the world it lives in (`v1_arch.md` 3.4 "ownership").

### 2.3 Hitting and being hit

Through the CCL pass after the executor (interaction 1.1-1.5). A player's body shape out-levels an enemy shape with
attr 0x80 -> the enemy gets `twp->flag |= 4`; an enemy's attack shape out-levels the player's -> the player gets flag 4
and its mode machine runs the hurt path (knock-back along the player's facing, 120 steps of invulnerability, barrier or
ring scatter, death at 0 rings). Projectiles are tasks of their own (Kiki's bombs 8C0FAD60 family, Unidus spikes
8C0FE568, E-10x missiles) with their own shapes. Some badniks also give orders to player 0: SetInputP 8C07C65E (Egg
Keeper, Ice Ball), SetVelocityP 8C07C676 (Sweep, Buyon, the aircraft, Ice Ball, Beat), EnemyBumpPlayer 8C048980
(Boa-Boa): they assume the player is in the same world.

### 2.4 Death

`8C0489E4` (C, `code_8C0486A0.c:134`): on flag 4: squash 0.35, and when the hitting colliwk is a player (id 0) or a
player projectile (id 1): flag 0x1000 ("killed by a player"), `func_8C08A2A2(10)` (score, with the 120-step chain of
8C08A272), then either the no-bounce marks (attacker attr 0x200) or `8C048980(pno)` with pno = 0 if the attacker is
`playertwp[0]`, else 1 (bounce: `SetVelocityP(pno, 0, 2, 0)` for Sonic / Tails / Knuckles in a ball in the air, and a
vibration). Without flag 4 it asks 8C05E5C2 (the bomb item). The badnik's death mode then plays the explosion 8C045736,
debris 8C0486A0, sparks 8C0487B6, spawns an animal 8C015720 (`D_8C1623D0[GetStageNumber() >> 8][rand]`: 4 kinds per
stage, the fifth at 10 %), and ends with 8C091BA4 (record gone, task destroyed). E-103 / E-104 / E-105 release their
bird with 8C0158B6. Enemies drop no rings. If E-102 had it locked, the hooked dest 8C0D32C0 releases the marker and
pays the time bonus (interaction 1.7).

### 2.5 What each needs from its world

| Need | Badniks (1ST_READ) | Stage badniks | Bosses |
|---|---|---|---|
| Ground and walls | the land gather 8C0522C0 (C) through the enemy library: **any GMod geometry** (GRID) works | same | the arena's landtable (DISC collision) |
| Water | Sweep skates on water polygons (UNVERIFIED how it behaves on dry ground) | | Chaos 4's pond, Perfect Chaos's sea plane |
| Overlay slot 0x0C900000 | none | their STGnn (a Sonic world holds B_CHAOS7 there: `v1_arch.md` 2.7 stage packs) | their B_* image |
| Camera / view | the visibility test 8C038D00 (both display and some logic) and 8C048160 use the world's view: libsa1's virtual view 40 units behind player 0 (`sa1g_camera_view`) | same | boss cameras are stubbed; G-SA1 needs its own boss camera (lock-on to the boss) |
| SET record / condition | `tp->ocp` for 8C091BA4 / E-102's firing (8C0D46CE dereferences `ptp->ocp` when `ocp` is NULL) | same | some bosses read their SET objects (Chaos 0 poles, Chaos 2 tables, Chaos 6 frogs / bombs) |
| Stage number | animals by `GetStageNumber()` (libsa1's default stage 1 = Emerald Coast's animals) | | `GetStageNumber`, 8C04F6A0 |
| Textures | the PVMs of 1.1 (texlists in 1ST_READ) + the animals' (GOMA, PEN, RAKO, USA, KOAR, ...: the stage texture tables at 0x8C1C1D44-0x8C1C20A0 and in the stage overlays name them) | the overlay's texlists | the overlay's |
| Sound | SEs in **bank 4** (Kiki's tone 819 -> bank 4 via the table at 0x8C1C19B4; all badniks UNVERIFIED): measured identical (112,448 B SMPB + 768 B SMSB) in 16 MLTs (EMERALDCOAST ... HOTSHELTER, CHAOS0-7A, MOBILE1-3); SKYDECK, ICECAP, FINALEGG, E101/E101R/EGGCARRIER and STATIONSQUARE/MYSTICRUIN have larger variants | the stage's MLT | the boss MLT (CHAOS0.MLT ... E101R.MLT) and music ADX |

---

## 3. Measurements

### 3.1 Boss levels in the original game (harness)

`harness/build/sa1h.exe trace --level L --act A --chr C --input @build/research/enemies/idle300.txt --dump-ram-at
120,end` (300 idle steps), then a task census of the RAM dumps (`census.py`, after `v1_arch`'s `ramstat.py`):

| Level (fighter) | Result | SH-4 instructions / step | Tasks at the end | Overlay tasks alive (exec x count) |
|---|---|---|---|---|
| 15 Chaos 0 (Sonic) | 300 steps | 642,199 | 415 | 0C9023CE x1, rain 0C903800 x218, grass 0C903C80 x57, police cars 0C904DAA x4, helicopters x2, poles 0C9051CE x8; 1ST_READ Chaos drops 8C0EEAA0 x89 |
| 16 Chaos 2 (Knuckles) | 300 | 356,722 | 194 | 0C902ED6, tables 0C906804 x16; 8C0EEAA0 x140 |
| 17 Chaos 4 (Sonic) | 300 | 700,641 | 170 | 0C900E20, 0C9043E2 x10, 0C905B94 x13, 0C9061E4 x13 |
| 18 act 0 Chaos 6 (Sonic) | 300 | 400,525 | 36 | 0C902598, 0C905434, 0C906CDA |
| 18 act 1 Chaos 6 (Knuckles) | 300 | 529,043 | 66 | 0C902598, 0C904376 x6, smoke 0C906A00 x13 |
| 20 Egg Hornet (Sonic) | 300 | 232,893 | 38 | 0C9006C0, 0C9038F0 |
| 21 Egg Walker (Tails) | 300 | 249,297 | 54 | 0C902D2E, 0C902DE6 x3, 0C900C00, 0C907740 |
| 22 Egg Viper (Sonic) | **fault in frame 1**: "bad branch target at 00000000" | - | - | (the harness README also notes a wild pointer at 0C90052A) |
| 23 Zero (Amy) | 300 | 391,666 | 35 | 0C902740, 0C90531C, 0C905760, 0C90673E, 0C908084, 0C908138 |
| 24 E-101 Beta (Gamma) | 300 | 340,170 | 31 | 0C90140A |
| 25 E-101 mkII (Gamma) | 300 | 280,774 | 40 | 0C902EC0, 0C904D76 |

The boss gauge task 8C0F1C40 is alive in all ten. For scale, Emerald Coast act 1 with its objects runs 599,484
instructions per step (`v1_arch.md` 1.2): a boss arena costs about what a dense stage costs. Perfect Chaos (level 19) has
its own references (`super_pc_play`, `harness/README.md`).

### 3.2 Other measurements

* SET placements (1.1, 1.2): Kiki 253, Spinner A / B / C 81 / 142 / 50, Leon 46, Egg Keeper 25, Unidus A / B / C 11 / 9 / 21,
  Rhinotank 15, Boa-Boa 14, Sweep 7; Cop Speeder 53, Beat 101, Buyon 8, Ice Ball 6, aircraft 1; E-103 / E-104 1 each; 49
  Zero triggers.
* Sound bank 4 identity across MLTs (2.5): `sha1` of each MLT's bank-4 SMPB / SMSB (`mlt_bank4.py`).
* Arena extents (1.4): `arena_extents.py`.

---

## 4. Running them in G-SA1

### 4.1 Badniks as actors (follows `v1_arch.md` 2.2 / 3.4)

* **Owner world.** The badnik's record lives in the SET layer of every world, but only the owner's world spawns it (the
  nearest active character within the type's list distance, with hysteresis, chosen by the server). There it is exactly
  the game: CCL both ways, the bounce, the knock-back, E-102's lock-on and lasers, Big's lure, Tails' tails, Knuckles'
  Maximum Heat, death, animal, score. A hand-off re-spawns the record in the new owner's world at the last streamed pose
  (condition bit 1); the badnik's internal state restarts, as when the game re-spawns an object that went out of range.
* **Other worlds.** A round-5 ENEMY host object with the badnik's own shapes and levels (copied from its live CCL table
  every pair: centres, sizes, on / off, attack and defence levels, so the horn of a Rhinotank still needs attack 2), and
  **HAZARD proxies for its projectiles** (Kiki's bombs and spike balls, Unidus' spikes, the E-10x missiles: every CCL
  entry of ids 3-5 created by the badnik's task tree). Hits on the ENEMY proxy reach the owner as v1_arch's ATTACKER
  (a list-1 shape with the hit's level): the game's own 8C0489E4 then kills it in the owner's world. A badnik touching
  another character hurts it in that character's world through the proxy's touch level, natively.
* **Render.** Draws of the owner world attributed to the badnik's task tree (v1_arch 5.3), drawn once; the other
  clients draw the puppet from the pose stream (model + action + frame from their own disc). The visibility test uses the
  owner world's virtual view: a badnik behind the owner's camera does not draw in the game; the host must then show the
  CPU pose (as `sa1_capture_hidden` does for characters) or give the world the host camera (v1_arch's "host camera in
  the world").
* **Death** reaches every client as the record's GONE (8C091BA4 clears bit 15). Score: the owner world's score counter
  (B_8C752BF4) changes; G-SA1 credits it to the owner's HUD; remote kills credit nobody in the owner world (as for
  E-102's shots, interaction 1.6), so the attacker's client adds the +10 itself (option). Animals live in the owner
  world as its tasks (drawn by attribution).
* **Respawn.** SA1 never respawns a killed SET enemy within an act; G-SA1 follows v1_arch 3.3 (per-entity "Respawn after
  (s)", default never).

### 4.2 Plain GMod players and NPCs

The owner wants badniks "optionally hostile to GMod players / NPCs". Two levels:

1. **Touch damage (with the first wave).** The owner world exports the badnik's (and its projectiles') attack shapes
   each pair (libsa1 already knows them from its CCL table); the server tests them against plain players' and NPCs'
   hulls and applies scaled GMod damage (round 5's "touching hostile NPCs hurts" mapping in reverse). The badnik does not
   chase them. Q-menu switch, default off; damage per touch level.
2. **Hunting (later).** For a hunted GMod entity, a *proxy-target world*: slot 0 = a proxy player fed from the entity
   (the follower design's proxy leader, `follower_ai.md` section 4 option C), the badniks that target it spawned there.
   Flag 4 on the proxy becomes GMod damage, SetVelocityP / SetInputP orders become a GMod push or are ignored, GMod
   weapon damage comes back as ATTACKER. Cost: one 16 MB world per hunted entity (cap it, e.g. 4), the order mapping is
   host code (not game logic, as v1_arch 2.1 warns), so keep it optional.

### 4.3 Stage-overlay badniks

Through `v1_arch.md` 2.7 "stage packs" (one STGnn per map session composed into every local world): Buyon (STG03), Cop
Speeder (STG04, plus a path: a G-SA1 spline converted into the game's path structure that 8C089AA0 reads; the format is
`v1_triggers.md`'s), the aircraft (STG06), Ice Ball (STG08), Beat with its gachapon (STG10). Sonic loses Super Sonic's
aura while a pack is loaded. About 38 KB to decompile in total.

### 4.4 Bosses

**Tier R: no arena (first).**

* **Character rivals**: run the game's set-up exactly: the AI fighter as player 1 in the *fighter's own world* (the
  follower round's "faithful same-world" configuration F2, `gsa1_rounds_plan.md` 12.1), created through 8C083F7C with
  the request block the SET side fills (B_8C79A39C). Arena-free (the fights took place in adventure fields), any map.
  To decompile: the dispatcher 8C083A1E and the 6 other asm functions of the region (1,572 B) and the gauge (1,960 B).
  Open: whether the AI handles player kinds the game never paired with it (Amy, Big as the target) - UNVERIFIED, offer
  them marked "unofficial".
* **E-100 Zero as a chaser**: an ACTOR spawned through 8C107744 in its owner's world; it chases player 0, is knocked
  back by attacks, catches Amy. 5.4 KB asm left. Behaviour against other characters UNVERIFIED (its catch probe
  D_8C5C6B80).

**Tier A: arena worlds ("arena pocket").** `v1_arch.md` 2.7: the fighter's world is rebuilt as the boss level:

* the world image with the boss overlay at 0x0C900000 (plus TIKAL_PROG for level 19), the level (15-25) and act,
  `SA1_CREATE_STAGE_TASKS` extended from STGnn to B_* (the level task `D_8C19DE30[level]`, the sky task
  `D_8C19E0E0[level]`), the SET / CAM files of the level, **DISC collision of the arena's own landtable** (the mode exists:
  `SA1_COLLISION_MODE_DISC`; collision is context-wide today, so the arena needs its own `sa1_context` or per-world
  collision);
* G-SA1 draws the arena (the landtable's visible models and GeoAnim parts through the existing model emitter, textures
  from the boss's PVMs) at an **anchor** chosen in the map, with the arena's SA1 origin mapped to it; the GMod map does not
  collide inside the pocket; other players see the arena, the boss puppet and the fighter;
* the fight is one fighter per arena in 1.x (the boss reads `playertwp[0]` only); co-op later through actor proxies
  (v1_arch 3.4) in the other fighters' arena worlds;
* the boss camera: SA1's boss cameras are stubbed in libsa1; G-SA1 adds a "look at the boss" camera mode in the pocket
  (`v1_triggers.md` camera modes);
* the end: the boss's defeat calls the stage-clear path (8C04EFE0, a void stub today) -> a new libsa1 event, G-SA1 shows
  the result and returns the fighter to the map (character state: rings, upgrades carried over).

Feasibility, per boss:

| Boss | Arbitrary map? | Why | Order |
|---|---|---|---|
| E-101 Beta | needs its small hall (536 x 545) | measured alive in the harness; smallest overlay (9 KB) | 1 |
| Egg Hornet | needs its arena (flight around it) | 16 KB; runs in the harness | 2 |
| Chaos 0 | needs the plaza and the poles (SET objects) | 19 KB + the Chaos code (12.7 KB, reused by every Chaos); 415 tasks (rain) | 3 |
| Chaos 2 | needs its arena (tables) | 24.6 KB | 4 |
| E-101 mkII | needs the deck | 25.2 KB | 5 |
| Chaos 4 | needs the pond (water, lily pads) | 25.4 KB | 6 |
| Zero | needs the deck and its fences | 28.9 KB; uses char-boss-AI helpers | 7 |
| Egg Walker | needs the street and buildings | 27.9 KB | 8 |
| Chaos 6 | needs the deck; per-character items | 27.1 KB | 9 |
| Egg Viper | needs the floor it destroys | 39.3 KB; **no oracle until the harness fault is fixed** | 10 |
| Perfect Chaos | needs the flooded city, Super Sonic, Tikal, the camera file | 27.8 KB left; the arena is larger than Source's coordinate range at 4 HU per unit (render-only placement or a smaller scale) | last |

**Tier S: stage-pack bosses.** E-103 Delta and E-104 Epsilon call EnemyInitialize (home = the spawn point), the land
gather and the boss gauge: they should fight anywhere in open space once STG02 / STG05 is the session's pack
(UNVERIFIED: hard-coded positions in their data tables were not checked). E-105 Zeta is a turret in Hot Shelter's pit:
treat it as an arena boss (its act's landtable) or leave it out.

### 4.5 The G-SA1 tab

Categories of `v1_arch.md` 4.1: **Badniks** (one entity per type: Kiki, Rhinotank, Sweep, Boa-Boa, Leon, Egg Keeper,
Unidus, Fire Unidus, Spinner, Patrol Spinner, Electric Spinner; later Buyon, Cop Speeder, Ice Ball, Beat, aircraft) and
**Bosses** (Rivals: Knuckles / Sonic / E-102 AI; E-100 Zero; the arena bosses greyed until their arena world exists).
Properties map to the SET parameters (1.1): Kiki's projectile (bomb / spike ball / none), ranges (search, move, radius)
in HU, the Spinner B / C patrol box (drawn as a gizmo), phase / speed, "no collision"; the emerald flag is not exposed.
Per-entity options: respawn time, "hostile to GMod players / NPCs" (overrides the server default), animal set (the
stage whose animals it releases; default: its home stage). Q menu > Options > G-SA1 > "Enemies": master switch, touch
damage to plain players / NPCs and its scale, hunting (later), score credit for remote kills, animals on / off, boss
camera, arena anchor rules.

### 4.6 What libsa1 needs (beyond v1_arch's SET layer, actors and ATTACKER)

1. Per-world export of an actor's live CCL shapes and of its task tree's projectile shapes (for proxies and touch damage).
2. Texture archives beyond the character's: the enemy PVMs (1.1) and the animals', resolved by texlist address (the
   stage texture tables give the name of every texlist).
3. Audio: bank 4 from a stage MLT (EMERALDCOAST.MLT is enough for the 1ST_READ badniks), the boss MLTs.
4. A virtual stage number per world for the animals (v1_arch 5.2) and B_8C7493CA for bosses.
5. Arena worlds: `SA1_CREATE_STAGE_TASKS` for B_* images, DISC collision per arena (own context or per-world
   collision), landtable render output, the stage-clear event, the boss HP (gauge state) in `sa1_state_ex` for G-SA1's
   HUD (the CHAOS_LIFEGAUGE texture, 2,351 B).
6. Same-world player 1 (F2) promoted from a test configuration to a supported mode for the rivals.

### 4.7 Verification

* **Oracles exist**: `harness/tests/sim/obj_ec1_kill.txt` (Sonic homes onto a Rhinotank in EC1 and kills it: the damage
  check, the bounce, the death sequence; harness only today). New scripts per badnik in its home stage (Kiki EC1, Spinners
  SH / SD, Leon / Boa-Boa WV, Egg Keeper FE, Unidus SH / RM, Sweep EC2) with v1_arch's `--set` sidecar, recorded with
  the pure interpreter and replayed strictly by libsa1 once the code is C.
* **Per-function**: `harness diff` for each decompiled function (the project's usual SHC match / NON_MATCHING loop).
* **Bosses**: the level boots of 3.1 are the starting references (record input scripts that fight); Egg Viper needs the
  harness fault fixed first.

---

## 5. Recommendation

1. **Badnik wave E1 (first badnik release)**: decompile the 12 1ST_READ badnik execs (58,424 B, 264 fns) plus their
   shared asm (enemy library 21 fns / about 3.7 KB, effects 13 fns / about 1.7 KB, SET range 216 B, Kiki's draw helper
   706 B): **about 65 KB** (v1_arch's closure upper bound: 67,966 B, 319 fns). Order: Rhinotank (reference trace
   exists) -> Kiki (253 placements, projectiles) -> Spinner A / B / C -> Unidus A / B / C -> Leon, Egg Keeper, Boa-Boa,
   Sweep. Ship as v1_arch's actors with touch damage to plain GMod players / NPCs as an option.
2. **Boss tier R right after E1**: the six character rivals (3.5 KB to decompile) and E-100 Zero (5.4 KB): real bosses
   without any arena.
3. **Arena worlds** (v1_arch S5), starting with E-101 Beta and Egg Hornet to build the pocket (render, anchor, return),
   then the Chaos fights (the shared Chaos code once), then the rest; Perfect Chaos after round 6a.
4. **Stage packs**: the five stage badniks and E-103 / E-104 when v1_arch's stage packs land.
5. **Hunting GMod entities** (proxy-target worlds) only if the owner wants more than touch damage.

---

## 6. Work items

See the structured report (`work_items`); summary: SADC decomp E1 (65 KB) -> libsa1 actor support (shape export,
textures, bank 4, stage number) -> G-SA1 Badniks category + options -> rivals (F2 + 3.5 KB) and Zero (5.4 KB) -> arena
worlds (infrastructure, then per boss 9-39 KB each) -> stage packs (38 KB badniks + 21 KB E-103/104) -> hunting.

---

## 7. Owner questions (defaults in brackets)

1. GMod players / NPCs: touch damage only, or should badniks also chase them (a 16 MB world per hunted entity)? [touch
   damage, off by default, Q-menu switch; hunting later]
2. Bosses in an "arena pocket" (the boss's own arena appears at the spawn point; the fight happens there) rather than on
   the GMod map? [yes]
3. One fighter per boss in 1.x (others watch)? [yes]
4. Killed badniks: gone until reset (SA1's rule) or respawn after a time? [gone; per-entity respawn time]
5. Score and animals: show the score in the HUD and release animals like the game? [yes, both toggleable]
6. GMod weapon damage on badniks and bosses: how much damage is one SA1 hit? [badnik: any hit of 10+ kills; boss: one
   hit per 50 damage, with the game's own invulnerability time]
7. Rivals: allow any character against any AI rival, beyond the game's six pairings? [yes, marked "unofficial"]
8. Perfect Chaos: last, and at a reduced arena scale if needed? [yes]

---

## 8. Risks and UNVERIFIED points

* Volume: 65 KB (E1) + 38 KB (stage badniks) + 21 KB (E-103/104) + 270.5 KB (boss overlays) + 14.7 KB (Chaos / gauge).
* Hand-offs reset a badnik's state mid-attack; projectiles of the old owner vanish.
* Logic gated by the visibility test and the camera work (8C038D00, 8C048160, 8C0919E8): behaviour depends on the
  world's view; libsa1's virtual view is not GMod's.
* Overlay slot: stage badniks and bosses displace B_CHAOS7 in Sonic's world.
* Player orders from badniks (SetInputP / SetVelocityP) are native only in the owner world; in proxy-target worlds they
  need host mapping.
* Egg Viper faults in the original-game harness (frame 1): no oracle until fixed.
* Perfect Chaos's arena exceeds Source's world at 4 HU per unit (bounding-sphere estimate 5,983 x 10,377 units).
* Bosses read SET objects (poles, tables, frogs, ice bombs), the event system (Egg Viper: 8C049882, 8C0499C0) and
  8C04F6A0 / B_8C7493CA (meaning UNVERIFIED); the stage-clear path is a void stub.
* UNVERIFIED: the bank of every badnik SE (only Kiki's 819 checked); boss mains marked UNVERIFIED in 1.4; Sweep on dry
  ground; Ice Ball's SetInputP order; Cop Speeder's path source; E-103 / E-104 arena independence; Zero against non-Amy
  characters; rival AI against other kinds; CCL table lengths past the dumped rows; the aircraft's in-game name.

---

## Appendix: scripts and data (`SADC/build/research/enemies/`)

| File | What |
|---|---|
| `status2.py` | decomp status per range with nested-`#if`-aware NON_MATCHING detection |
| `closure.py`, `ovlclosure.py`, `deps.py`, `sig.py`, `sig2.py` | call-graph closures per exec, external dependencies, enemy-signature calls |
| `regionmap.py` -> `region_full.txt`; `ovlmap.py` -> `stg01map.txt`, `stg12map.txt` | functions with their data references (SA Tools labels) |
| `ovltex.py`, `dumptex.py`, `findptr.py` | stage texture tables (PVM name, texlist), pointer search |
| `ovlstat.py`, `floats.py` | overlay sizes; float-constant scan (no absolute arena constants found in literal pools) |
| `labels.txt` | SA Tools labels of enemy / boss data |
| `census.py`, `bossboot/*.ram`, `idle300.txt` | the boss-level runs of 3.1 |
| `ccl_dump.py`, `mlt_bank4.py`, `arena_extents.py` | the CCL table dump (1.1), the MLT bank-4 hashes (2.5), the arena extents (1.4); run from `SADC` |
| (inline, this session) | the object-list entries of 1.1 (read from `D_8C1C143C` and the STG binaries) |

---

## Review (adversarial)

Reviewer pass, 2026-10-21, read-only. Scratch in `SADC/build/research/enemiesreview/`: `ranges.py` (sizes per range),
`st.py` / `ovst.py` (status2 per range and per overlay), `deps.py` (transitive asm closure of the 12 badnik execs
outside their range), `bossdeps.py` / `bossdeps2.py` (1ST_READ asm each boss overlay needs), `cclcalls2.py` (the
arguments of every CCL_Init call in 8C0F2BA0-8C107800, read from the asm), `gauge.py` (title task vs gauge task in the
`bossboot` RAM dumps), `vbox.py` (vertex bounds of the solid COL of the arenas). Status figures use the doc's
`status2.py` (nested-`#if` aware); a C definition in `#ifdef NON_MATCHING` counts as NM.

### R.1 Verified

* **Execs.** All 12 are object-list execs in `build/1ST_READ.xrefs.json`: `objexec_E_SARU` 8C0FBE04, `E_SAITO` 8C0F3EAA
  (init 8C0F2C44), `E_HEVY` 8C0F4ED6, `E_AMEMB` 8C0F5040, `E_LEON` 8C0F5D40, `E_ROBO` 8C0F8E10, `E_UNI_A/B/C`
  8C1006D4 / 8C100A96 / 8C100E20, `SPINA_A/B/C` 8C102AD4 / 8C102C04 / 8C102D2A. The SET edit labels of 1.1 (`SEARCH RANGE`,
  `MOVE RANGE`, `sclY >= 1000.0 DE BOMB NAGEMASEN`, `SPINNA B (BIRIBIRI NASHI)`, `SPINNA C (BIRIBIRI ARI)` ...) exist in
  `reference/strings_1st_read.txt`.
* **Decomp status.** 8C0F2BA0-8C102E80 holds 264 functions and 58,424 B, with no C definition. The six ranges are
  51 / 8,192, 21 / 2,948, 74 / 18,152, 39 / 11,374, 55 / 10,500 and 24 / 7,258. These ranges are logical groups: the
  asm split has 18 files there. Whole game: 4,119 / 973,430 B, matched 1,217 / 218,136, NM 393 / 291,458.
  * E-100 Zero, 8C102E80-8C107800: 52 / 17,716 B. 24 matched (5,138 B), 3 NM (7,180 B), 25 asm (5,398 B).
  * Rivals, 8C082D60-8C0869E0: 58 / 11,646 B. 17 matched (2,580), 34 NM (7,494), 7 asm (1,572): 8C083818, 8C0839EC,
    8C083A08, 8C083A1E, 8C083AA0, 8C083AA6, 8C083AAC.
  * Chaos code: 66 / 11,212 + 10 / 1,502 = 76 / 12,714. With the gauge and title code (7 / 1,960) that makes
    14,674 B, all asm.
  * Enemy library 39 / 6,398 (5 / 1,052 matched, 1 / 278 NM, 33 / 5,068 asm). Effects 54 / 8,418 (6 / 902 matched).
    Animals 46 / 8,932, all C.
  * B_CHAOS7 5,480 matched + 360 NM, which gives 270,516 B of asm in the 11 boss overlays.
* **E1 closure (`deps.py`).** Outside its own range, the transitive asm the 12 execs need is:
  * enemy library 21 functions / 3,748 B;
  * effects 14 / 1,882 B (the doc says 13 / 1.7 KB);
  * 8C0919E8 + 8C09111A, 216 B;
  * Kiki's 8C09F080-8C09F380, 706 B;
  * the renderer 8C03700C ... 8C038D00, served by `rt_draw`, and the C runtime, served by `rt`;
  * the emerald-shard path 8C0C95B2 / 8C0C9BE2 / 8C0C9C9A / 8C0C9F5A + 8C05B260 / 8C05B3C0 / 8C05B8C0, 2,386 B,
    unreached without EMERALD.

  That is 64,976 B in all, which agrees with "about 65 KB". The badnik code references neither GetStageNumber,
  GetPlayerNumber nor any overlay address, so it is map-independent. Only the animals and the textures depend on the
  stage.
* **Data and disc.** 0x8C599250-0x8C5FBB60 is 403,728 B and holds every model and motion label of 1.1 and 1.3, from
  `Rhinotank` 8C59C12C to `E_100_Zero_Idle_2` 8C5FB890. Every PRS size in 1.1, 1.2 and 4.6 matches
  `disc/SONICADV`. Placements in `set_usage.json`: 674 in 1ST_READ + 169 in stage overlays = 843.
* **Targeting.**
  * The 19 calls of 8C047FCE / 8C047FFC in 8C0F2BA0-8C107800 pass player 0. The asm spot check at 8C0F8834 / 8C0F8836
    has `MOV #0,R5` in the delay slot.
  * The badnik pseudo-C reads only `DAT_8c78c608` (76 refs). No boss overlay references `B_8C78C608+4`.
  * 8C04712A loops over slots 0 and 1. The EnemyInitialize defaults are 0x38E3, 22500.0 and 1225.0
    (`code_8C047040.c:91-93`).
* **Death.** 8C0489E4 and 8C048980 are matched (`code_8C0486A0.c:115-166`). 8C045736 is asm (86 B, 11 callers).
  8C091BA4 clears ssCondition 0x8001 and sets exec = DestroyTask. Score 8C08A2A2 adds to `B_8C752BF4`. Animals:
  `D_8C1623D0[level][n]`, with n = 4 at 10 %.
* **Bosses.**
  * The image / PRS sizes and function counts match `baserom/overlays.json` and the overlay xrefs.
  * `D_8C19DE30` / `D_8C19E0E0`, levels 15-25, are as in 1.4.
  * `D_8C19E9FC[level][0]` points into the B_* image for levels 15-25: 15 -> 0C9001D8 (63 COL), 19 -> 0C901214
    (305 COL), 22 -> 0C9580F4.
  * Only Chaos levels have SET files: SET1500S ... SET1801K, SET1900S, and CAM1900S. Levels 20-25 have none.
* **Harness (3.1).** The per-step means were recomputed from `bossboot/*.csv`: 642,199 / 356,722 / 700,641 / 400,525 /
  529,043 / 232,893 / 249,297 / 391,666 / 340,170 / 280,774. Level 22 stops after 2 records, as `harness/README.md`
  ("wild pointer 0C90052A") says.
* **Sound.** Re-running `mlt_bank4.py` gives 16 identical bank-4 sets (112,448 B SMPB + 768 B SMSB).
* **Perfect Chaos** exceeds Source's world even with exact bounds. The vertex AABB of the 108 solid COL of 0C901214 is
  5,400 x 9,812 units = 21,600 x 39,248 HU, against 32,768. The sphere estimates are high for the other arenas: by
  vertex bounds, Chaos 6 is 2,121 x 2,121 units, Chaos 0 1,200 x 1,200 and E-101 Beta 395 x 401, and Egg Viper's pit
  reaches y -50,009.
* **Stage overlays.** The `ovlclosure.py` figures were reproduced. Two notes:
  * E GACHA (6,452) and O GACHA (2,150) share 476 B, which the table counts twice.
  * COLLI GACHA (0C90E058, 1,122 B) belongs to the gachapon but is missing from the sum.

  E-103 / E-104 / E-105 = 10,448 / 10,520 / 6,578 B. The E-105 list entry is at STG12 0x0CA89F28 (02 02, exec
  0C9142E0, name "E E105").
* **LIB stubs.** In `gen/policy/stubs.tsv`, 8C04EFE0 is `void` (:105), 8C0919CC is `ret 0` (:139) and 8C05E5C2 is
  `ret 0` (:116).

### R.2 Refuted (with the correct facts)

1. **"The table length is not known from the call" (1.1).** It is known. On the DC, CCL_Init is `(tp, info, id, 1,
   nbInfo)`, with nbInfo as the 5th (stack) argument (`include/sa1/ccl.h:79-86`, `code_8C02F27C.c:170`).

   | Table | Rows (nbInfo) | CCL id |
   |---|---|---|
   | Rhinotank 8C5992B8 | 2 (byte D_8C15D090) | 3 |
   | Boa-Boa segment 8C59D280 | 1 | **2** |
   | Boa-Boa segment 8C59D2B4 | 2 | **2** |
   | Boa-Boa segment 8C59D384 | 2 | **2** |
   | Boa-Boa segment 8C59D420 | 1 | **2** |
   | Sweep 8C5AAE3C | 1 | 3 |
   | Leon 8C5B3E20 | 3 | 3 |
   | Egg Keeper 8C5B7EB4 | 2 | 3 |
   | Egg Keeper 8C5B7F1C | 2 | 3 |
   | Kiki 8C5C2504 | 1 | 3 |
   | Kiki's thrown object 8C5C2538 | 3 | 3 |
   | Unidus 8C5C4F00 | 1 | 3 |
   | Unidus spikes 8C5C4F34 | 1 | 4 |
   | Spinner A/B/C 8C5C68F0 | 3 | **2** |
   | Zero 8C5C6B18 | 2 | 3 (2 in the event variant 8C107020) |

   `ccl_dump.py` always prints up to 4 rows, so the CCL table of 1.1 lists rows that are not shapes:
   * **Sweep** has only the r 13.5 h 4 cylinder.
   * **Boa-Boa** segments are spheres only (r 5.5 / 5 / 7 / 6, touch 1, armour 0). The "capsule r 6 len 8 touch 2
     armour 3" row (8C59D3EC) lies past the count, and the head task (kind 0) has no CCL_Init.
   * **Kiki's thrown object** has three rows:
     * cylinder r 7.2 h 3, attr 0x811 (off), armour 3;
     * sphere r 5, touch 2, attr 0x10 (off until it explodes);
     * cylinder r 3 h 3, touch 0, the damageable body.

     The "spike ball r 3.5 touch 2 armour 1" row lies past the count. There is only one CCL_Init site for both
     projectiles (8C0FBB00), so how a spike ball differs from a bomb is UNVERIFIED.
2. **"CCL_Init(tp, table, 3, ...) (id 3: enemies)" (2.1) holds for most types only.** Spinner A/B/C and the Boa-Boa
   segments register **id 2**. Ids 2 and 3 are both E-102 lock-on lists, but id 2 is not paired in the 3 x 3 enemy
   loop.
3. **The wake-up ranges are defaults, not the badniks' values.** Most inits overwrite the EnemyInitialize defaults
   (Ghidra, squared floats):

   | Badnik | View range | Half-angle | Near range |
   |---|---|---|---|
   | Rhinotank (8C0F2C44) | 100 | 80 degrees | 15 |
   | Sweep | 200 | 80 degrees | 30 |
   | Unidus A/B | 300 | 0x8000 (any direction) | 200 |
   | Unidus C | 400 | any direction | 280 |
   | Spinner (8C101020) | 80 | 0x71C7 (+-160 degrees) | 20 |
   | Leon | | switches 0x38E3 / 0x8000 at run time (8C0F5AAA / 8C0F5AFC) | |

   Whether the SET "SEARCH RANGE" parameter overrides these again is UNVERIFIED. This matters for ownership radii
   (owner world = nearest character, 4.1).
4. **8C0F1C40 is not the boss gauge.**
   * 8C0F1C40 is the boss **title** task, created by 8C0F1EE6. It builds sprite text from its `char *` argument,
     centres it at x 320, plays a per-level sound for levels 15-22 through 8C0940DA, and frees itself after its timer.
   * The life gauge is the exec 8C0F2228, which draws through 8C0F20A0 / njDrawSprite2D. It is created by 8C0F234E,
     or as a child by 8C0F2400 (B_CHAOS7).
   * In the RAM dumps, 8C0F2228 is alive only in levels 20 and 21 at step 300 (`gauge.py`).
   * The HP that `sa1_state_ex` should export is `B_8C7A50E8` (current, float) and `B_8C7A50F4` (maximum, int);
     `B_8C7A50EC` is the displayed value. These meanings come from the pseudo-C and are UNVERIFIED.
5. **"Every boss overlay imports 8C0F1EE6 / 8C0F234E, GetStageNumber, 8C04F6A0" (1.4) is too broad.**
   * B_CHAOS7 imports 8C0F2400 instead of 8C0F234E.
   * B_CHAOS2, B_EGM3, B_ROBO, B_E101 and B_E101_R do not import GetStageNumber.
   * B_E101 does not import 8C04F6A0.
   * All 11 import 8C04EFE0.

   Also, "libsa1 stubs the camera hooks" is true only for 8C01ABDC and 8C01AD2C. 8C01937C, 8C0208E0 and 8C020AE0 are
   asm and not stubbed.
6. **Remote-kill score (4.1).** The doc says "remote kills credit nobody in the owner world, so the attacker's client
   adds the +10 itself". The code says otherwise:
   * 8C0489E4 adds +10 for attacker ids 0 **and 1** (`code_8C0486A0.c:144-146`). v1_arch's ATTACKER is id 1, so the
     owner world gets +10, as v1_arch 5.4 itself says.
   * The ENEMY proxy's exec in the attacker's world also runs 8C0489E4, so that world gets +10 as well.

   A remote kill therefore adds +10 in both worlds. G-SA1 must add nothing, and must choose which increment the HUD
   shows.
7. **Rival fights (1.5, tier R).**
   * **Not SET-side.** `B_8C79A39C` is filled by 8C09072C. That task is created at level start by 8C090060 (called
     from 8C04DA20) when `D_8C1C0020 != -1`, which is scene data; the kind comes from `D_8C1C0258[B_8C79A398]`. The end
     callback 8C090800 does three things:
     * on a win (8C0585E2), it sets the story flag `B_8C799920[...]` and sets `D_8C1C0020` to -1;
     * it frees the AI (8C08403E);
     * it restores the music (8C095742).

     That path holds 660 B of asm that is not counted: 8C09072C 136, 8C090800 56, 8C095742 140, 8C0956BE 132,
     8C0909A4 168 and 8C107AAA 28. Host code may replace it.
   * **Not map-independent.** 8C083EF8 (C) moves the fight task to fixed coordinates `D_8C1BEFB0` chosen by stage:
     0x0000 -> (72, -56, -144), 0x2100 -> (73, 72, 158), 0x1D02 -> (0, 1525.7, 3345). 8C083AAC (asm, 944 B) creates the
     fight's rings, in a circle for stage 0 and at the fixed points `D_8C1593E0` for 0x2100. On a G-SA1 map (virtual
     stage 1) no case applies. "Arena-free, any map" therefore needs those positions supplied by host code or by a
     virtual stage; the owner should decide.
   * **No boss gauge.** No 1ST_READ or ADV code calls 8C0F1EE6 / 8C0F234E / 8C0F2400; their callers are B_*,
     STG02 / 05 / 12 and SHOOTING. Tier R needs neither the gauge (1,960 B) nor the 2D HUD round.
8. **E-100 Zero as a chaser for any character.**
   * The creator 8C102EAE returns NULL unless `GetPlayerNumber() == 5` (CurrentCharacter = Amy)
     (`code_8C102EAE.c:77`), and 8C107744 goes through it.
   * The catch reads Amy's state: 8C0DDDE6 returns 0 for any other character (`code_8C0DDA00.c:198-209`).
   * In a non-Amy world the game itself refuses to create Zero. Creating its task (exec 8C1030F0) from host code
     bypasses a game-logic guard; the owner should decide.
   * 8C1030F0 also calls STG03 code (`STG03_FUNC_0C907998`) when the stage is 0x302.
   * 8C105036 (the beam) is NM C, not asm. The asm that is left includes 8C105920 (604 B, "the beam caught player 0")
     and 8C107700 (the EGGROB load, reached only from AmyMain).
9. **"Audio bank 4 from a stage MLT" is not new libsa1 work.** `libsa1_audio.h` `sa1_audio_desc.stage` (default 1,
   EMERALDCOAST.MLT) already loads banks 0, 1, 2, 4 and 5, and `sa1_audio_set_stage` switches them.
10. **DISC collision covers levels 0-11 only.** `SA1CA_STAGE_LEVELS` is 12 (`sa1_coladapt.c:264, 2500`), and the
    loader opens `"STG%02d.PRS"` (`:2447`). Arena worlds need three changes:
    * the level range raised to 25;
    * a level -> B_* file map;
    * the landtable taken from `D_8C19E9FC`, which already has entries for levels 15-25.
11. **Placements by stage (1.1).** Totals are right; three location lists are not:
    * Rhinotank is in EC and WV only (SET0100E/S, 0102B, 0200E, 0202M/S), not IC.
    * Unidus B is in SH and SD only.
    * Spinner A is in SH, RM, SD and FE. CP has Spinner B and C, not A.
12. **Level 19 was not in the 300-step census.** There are no `l19` files. "Levels 15-25 run except 22" rests on
    `super_pc_play` for Perfect Chaos.
13. **Attribution (4.2, 1.7).** "The owner wants ... 'optionally hostile to GMod players / NPCs'" and the "names the
    owner listed" quote the workflow brief (`tools/workflows/gsa1_design_v1.js:50-56`). The owner's own words are
    only "Badniks and Bosses". Hostility belongs in section 7 as an owner question, which it already is (Q1).

### R.3 Gaps

1. **The host-object API cannot mirror badniks as described in 4.1.** `sa1_hostobj_desc` has one `armor` and one
   `touch` per object, at most 4 shapes, the forms sphere / cylinder / box, and id 3 for ENEMY. A badnik mirror also
   needs:
   * per-shape attack / defence levels: Rhinotank horn 2 / 1 against the body's 1 / 0, Spinner field 2 / 2 against the
     blade's 1 / 0, Egg Keeper's attack cylinder armour 2;
   * capsules (form 3): Leon's tongue (attr 0x83 / 0xE3), Zero's body (0x2083);
   * the live on / off state (attr 0x10);
   * id 2 for Spinners and Boa-Boa, unless id 3 is accepted.

   The doc's projectile filter "ids 3-5" misses id 2. Kiki's bomb is id 3 with a damageable body, so it is not a
   HAZARD. Add a libsa1 item for this: per-shape levels and form, or a "mirror this CCL table" proxy kind.
2. **Bosses need about 17.9 KB more 1ST_READ asm than counted** (`bossdeps2.py`). This is the union over the 11
   overlays, excluding stubs, `rt_draw` (8C036F00-8C038E00), the C runtime (>= 8C10C000) and the Chaos / gauge code:
   * motion-link / weld 8C018120-8C019360: 8C01814E 1,152, 8C0185CE 1,956, 8C018E40 1,268 ...;
   * camera helpers 8C01937C / 8C0208E0 / 8C020AE0;
   * effects.

   Per boss, Chaos and gauge code included:

   | Boss | fns | 1ST_READ asm needed (B) |
   |---|---|---|
   | E-101 Beta | 15 | 5,736 (title / gauge 1,866, weld 3,310, camera 262, effects 298) |
   | Egg Hornet | | 7,562 |
   | Chaos 0 | | 18,964 |
   | Chaos 2 | | 20,362 |
   | Chaos 4 | | 14,704 |
   | Chaos 6 | | 14,980 |
   | Perfect Chaos | | 10,558 |
   | Egg Walker | | 10,490 |
   | Egg Viper | | 9,324 |
   | Zero | | 9,996 |
   | E-101 mkII | | 9,870 |

   So "E-101 Beta (9 KB)" is about 14.8 KB, and the XL item is about 303 KB, not 285.
3. **ATTACKER bounces player 1 in two-player worlds.** For an attacker that is not `playertwp[0]`, 8C0489E4 calls
   8C048980(1). In an F2 world (rivals, followers), player 1 is the AI fighter or the follower, so a remote kill
   bounces it if it is in a ball in the air. Give ATTACKER shapes attr 0x200 (the no-bounce marks), or accept the
   bounce.
4. **Hunting (4.2).** The proxy leader of `follower_ai.md` option C has an empty exec and "collides with nothing". It
   has no colliwk, so a badnik can never set its flag 4. A hunted-entity proxy needs a player CCL (id 0 shapes,
   CCL_Entry every step) and its own hurt handling. That is unlisted libsa1 work.
5. **Hand-off moves the home.** A hand-off re-spawns the badnik at the last streamed pose, and that pose becomes
   EnemyInitialize's home (+0x50). Every hand-off then re-centres MOVE RANGE and the Spinner B / C X / Y / Z WIDTH box,
   so patrols drift. Re-spawn at the record's own position, as SA1 does after a range-out, or state that the drift is
   accepted.
6. **E1 verification sees only the player.** `obj_ec1_kill` records the player only, so a badnik that diverges inside
   passes. Each per-badnik reference needs:
   * the badnik's taskwk pos / flag / mode in a sidecar: the `.obj.bin` of `--create-task` already does this, and
     `--set` needs the same;
   * `harness diff` for each function.
7. **The item-box bomb works in one world only.** 8C05E582 / 8C05E5C2 (v1_objects) kill badniks only in the bomb's
   world. Badniks owned by other worlds need an ATTACKER relay of the bomb's sphere.
8. **Overlap with v1_arch SET-0.** 8C0919E8 and 8C09111A are already in v1_arch's SET-0 batch. Once SET-0 lands,
   8C0919CC stops returning 0, so badniks range out of their owner world; the hand-off design (4.1) must handle that.
9. **Audio banks are per client, not per world.** An arena world or a stage pack switches the client's stage banks
   0, 1, 2, 4 and 5 for every world. Per-world banks are not designed.

### R.4 Required changes

1. 1.1 and the CCL table:
   * use each call's nbInfo and id (R.2.1-2);
   * remove Sweep's inner cylinder, Boa-Boa's capsule and Kiki's spike-ball row;
   * mark Spinner and Boa-Boa as id 2.
2. 2.2: give the per-badnik wake ranges (R.2.3), not the defaults.
3. 1.4 and 3.1: call 8C0F1C40 the title task, call 8C0F2228 the gauge, and add the HP addresses (R.2.4). Correct the
   import list (R.2.5).
4. Tier R:
   * drop the gauge and 2D HUD dependency;
   * add the rival request and end path (660 B) and the stage-fixed positions (R.2.7);
   * add an owner question on how rival fights are placed on a map.
5. Zero:
   * state the Amy-only creation guard;
   * add an owner question: Amy-only, or bypass the guard (R.2.8).
6. 4.1: rewrite the score paragraph (+10 in both worlds, R.2.6) and the hand-off position (R.3.5).
7. 4.6 and the libsa1 work item:
   * drop "bank 4 from a stage MLT";
   * add the host-object shape extensions (R.3.1), DISC levels 15-25 (R.2.10), ATTACKER no-bounce (R.3.3) and the
     hunted-proxy CCL (R.3.4).
8. Effort: tier A is about 303 KB (R.3.2), E-101 Beta about 14.8 KB, and the stage badniks about 38.6 KB (with
   COLLI GACHA, without the double count).
9. 1.1 location lists (R.2.11). 3.1: say that level 19 was not run (R.2.12). 4.2 and 1.7: attribute the brief, not the
   owner (R.2.13).
