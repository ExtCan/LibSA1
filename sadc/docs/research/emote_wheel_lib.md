# The emote wheel in libsa1: playing any of a character's animations with the game's own code (`emote_wheel_lib`)

Agent `res-emote`, prep run for round 0.9.4a (owner request of 2026-10-26, rounds plan 14.1s), written 2026-10-26 around
19:00. Research only: nothing in LIB, g-sa1, SADC/src, SADC/config, SADC/include or SADC/harness was changed or built in
(round 0.9.4 was being released there). Every experiment ran against a COPY of the released 0.9.4 libsa1 snapshot in
`SADC/build/research/faces094a/lib/` (section 9). Game bytes were only read from the owner's disc at run time (the probe)
and from the local images SADC already holds (the table dumps); the files written here hold numbers, addresses and names
only.

Owner request (the part this document serves): *"a holdable button that will open a radial menu (like GTA V's weapon wheel
without the slow-mo) that when a hold is let go, it will play the selected animation. The animations that are in the wheel
should be swappable with a menu in the G-SA1 options tab that lists all the animations with their internal names (custom
naming for easier picking will come later). The animations in the animation wheel should be unique per character."*
The faces half of the request is `faces_events.md`; the wheel's page, key and drawing are `faces_wheel_ui.md`; the round
plan is `faces_wheel_plan.md`.

Abbreviations as in the rounds plan (SADC, LIB, G-SA1, GDI). "Step" = 1/60 s, "pair" = two steps (1/30 s, one
`sa1_character_tick` dt of 1/30). "EV queue" = the player's event action queue (`taskwk.ewp->list40`). **UNVERIFIED** marks
what is only derived.

Companion data files (this document's section 3 explains the columns):
`emote_actions_sonic.tsv`, `emote_actions_tails.tsv`, `emote_actions_knuckles.tsv`, `emote_actions_amy.tsv`,
`emote_actions_gamma.tsv`, `emote_actions_big.tsv`, `emote_actions_supersonic.tsv` (all in `SADC/docs/research/`).

---------------------------------------------------------------------------------------------------------------------

## 0. Summary

1. **The game already has the mechanism: the cutscenes' event action queue.** Every player has an event work
   (`taskwk.ewp`, allocated by PInitialize through 8C04B440, `code_8C06C300.c:70`, in every libsa1 world). `EV_SetAction` (wrapper 8C04A120,
   asm; body **8C04B4D2**, C) and `EV_SetMotion` (8C04A142 -> **8C04B528**) append `{loop bit, blend frames, NJS_ACTION,
   texlist, speed}` to it; **every character's own display** (Sonic 8C0AE17A, Tails 8C0BA820, Knuckles 8C0C46AC, E-102
   8C0CFACE, Amy 8C0DA122, Big 8C0E3A40, Tikal 0CB008A0) draws the queue head with **8C04B57C** instead of the gameplay
   action while the queue is not empty, re-welds the body for it (8C04BF00) and advances it; `EV_ClrAction` (8C04A168 ->
   **8C04B48C**) empties it. The cutscenes use exactly this on live players: they turn the pad off (8C04921A, 202 event
   overlays), put the player into his normal standing state (8C04A080: SetInputP(pno, 24) + zero velocity, 186 overlays),
   then play the character's own gameplay actions through the queue (`SonicActionList_000_action` is imported by 47 event
   overlays; EV001B queues action 2 at 0.5 frames per step, looped). No new decompilation is needed: the C bodies the
   wrappers call are decompiled and lowered in every world.
2. **The game logic is untouched by it (measured).** Two Sonic worlds fed the same 1,300 pairs of input, one of them
   emoting for 1,110 pairs (and cancelling three emotes by input): position, speed, mode, gameplay action, flags, rings and
   the rand state are **identical in every pair**. The character keeps the world's rules while he emotes: gravity, ground,
   slopes, water, damage, moving floors and the idle timer all run as when he simply stands (an emote started 60 units up
   falls, lands and keeps playing).
3. **Every animation of the six playable characters plays.** Of the 671 table rows, 634 played without a fault in a world
   of their character at the first try; 35 are not in G-SA1's worlds at all (stage overlays and MINICART, which those
   worlds never hold, and Big's 7 empty rows); 2 faulted, both for one reason: the queue blends **between its own entries
   only**, and it faults when it blends a 73-node motion onto the 74-node curled body (Sonic 83, Knuckles 34; the same
   object and motion played fine after a clear) - libsa1 must allow a blend only from an entry of the same object
   (section 4.1). 35 more motions of the characters' own files that no table row uses (Tails' unused yawn, Knuckles' fist
   shaking and fighting stance, Amy's frustration and hiding, E-102's level clear pose A, ...) play on the body with
   `EV_SetMotion`. Super Sonic: 80 of Sonic's rows on the Super body in kind 8 (with the SUPERSONIC texlist) and 107 GT
   rows in the General form, plus his own 130-141 in both. One extra motion (Amy's barrel walk, the barrel's own motion)
   crashed the probe **process**: only validated motions may be offered.
4. **Behaviour.** The queue advances once per pair for Sonic, Tails, Knuckles and Big (their displays draw on sub step 0
   only) and twice for Amy and E-102: libsa1 picks the speed so that every emote plays at the game's own rate (`ev_speed`
   in the TSVs). A once-emote ends by itself and the gameplay stand comes back (end pop 0.02-0.20 SA1 units; 0.00 with a
   trailing stand entry); a loop plays until movement input or the wheel again. Movement input cancels **before** the
   pair that reads it, by the game's own tests (GetAnalog 8C06CD22, the jump / action press masks), so the gameplay that
   follows is bit-identical to a world that never emoted. Props: carried objects, stage set pieces, the board and Big's
   fishing tasks are not there (the pose plays empty-handed); Amy's hammer is shown by the event library's own setter
   8C0DE46C (measured: 932 -> 998 triangles).
5. **Mechanism and API.** libsa1 `sa1_character_emote(ctx, id, &emote)` / `sa1_character_emote_stop(ctx, id, how)`, run
   between pairs through `sa1g_world_call`, calling 8C04B48C / 8C04B4D2 / 8C04B528 (and 8C0DE46C for Amy) - the calls the
   cutscenes make - plus the cancel rules at the latch, the action names as metadata, the emote in `sa1_state_ex`, and the
   render hint / CPU pose following the queue. Never called in the reference worlds; the strict replays are unaffected.
   Rejected: the scripted-pose request (SetInputP 12 -> mode 20 + a host-written reqaction): it freezes the physics (he
   hangs in the air: measured), needs a host write of reqaction, and in the General Super form its release sends him into
   the game's Super modes 71-78 (measured: libsa1's own leak warning fires).

---------------------------------------------------------------------------------------------------------------------

## 1. Sources and method

| What | How |
|---|---|
| Event control of players | SADC C: `code_8C07C550.c` (SetInputP), `code_8C0AFF1A.c` and the other characters' request handlers, `code_8C0A966C.c` / `code_8C0A77BC.c` (Sonic's mode machine and main), `code_8C04B440.c` (the queue), `code_8C04BF00.c` (event welds), `code_8C0AE0E0.c` and the other displays, `code_8C0509A0.c` / `code_8C050BE4.c` (pad on / off); asm `asm/1st_read/text/8C0491C0.src` (the event library, 106 functions, 6 B of C) and `asm/overlays/ev001b/text/0CB80020.src` (one real cutscene) |
| Prior research | `v1_machinima.md` 3 with its Review R.2 (pacing, display gating), `super_general_design.md` 4.4 / I.2 (RC12 audit), `super2_anim.md` (Super Sonic's motions), `super2_plan.md` 5-6 / 10.1 |
| The action tables | `build/research/faces094a/scripts/dump_actions.py`: walks SonicActionList 8C1C55A0 (142), TailsActionList 8C256D6C (132), KnucklesActionList 8C3528CC (111), AmyActionList 8C4BB1D0 (98), GammaActionList 8C4225D4 (87), BigActionList 8C525564 (101) in the local images (`build/1ST_READ.BIN`, `baserom/overlays/*.BIN`), each row resolved in the image its world holds (LIB `src/sa1_manifest.c` overrides), names from `reference/dc_sa1_data_labels.csv` (SA Tools labels; `sat_filename` = the developers' file name = the internal name); `scripts/setters.py` lists the decompiled functions that request each action; `scripts/annotate_actions.py` writes the TSVs |
| Behaviour | `build/research/faces094a/probe_emote.c` against the copied 0.9.4 archive, on the owner's GDI: a catalogue of every row of every character (`out/catalogue.tsv`), both Super Sonics (`out/catalogue_super.tsv`), the extra motions, an identity test, an in-air test, blends and pops, interpolation, Amy's hammer (`out/probe_run.txt`, `out/probe_pops.txt`) |

---------------------------------------------------------------------------------------------------------------------

## 2. How the game puts a player under script control (task item 1)

### 2.1 Requests: SetInputP and the request handlers

`SetInputP(pno, mode)` (8C07C65E, `code_8C07C550.c:67-74`) only sets `taskwk.flag |= 0x1000` and `taskwk.smode = mode`. Each
character's "next action" handler takes it at its next step (Sonic 8C0AFF1A, `code_8C0AFF1A.c:63-66`; holding variants
8C0B07E4, Super 8C0B0F24 in `code_8C0B0E94.c`) - in almost every mode, because nearly every case of the mode machines calls
the handler first. Before the request, the handler checks the event work: `ewp->lock` (the cutscene locks "walk to / run
along / push / hold / turn", 8C07C1B4-8C07C270) and `ewp->list4C` (an event path forces the path mode, Sonic 80).

The requests that matter for a pose:

| Request | Sonic (`code_8C0AFF1A.c`) | Tails | Knuckles | Amy | E-102 | Big |
|---|---|---|---|---|---|---|
| 12 "stop" | mode 20, action 0, speed cleared, ang x/z 0 (123-132) | mode 18, 0 | mode 16, 0 | mode 13, 0 | mode 14, 0 | mode 14, stand selector |
| 9 / 10 (pose variants of 12) | mode 20, actions 44 / 45 (STG02 overlay) | 18, 69 / 70 | 16, 81 / 82 | 13, 54 (9) | 14, 0 | 14, stand |
| 24 "release" | 8C0A9660: mode 1, waittimer 0, stand selector (236-242) | 8C0B70E8: mode 1 | 8C0C4F40: mode 1 | 8C0DA6E0: mode 1 | 8C0CD09C: mode 1 | 8C0E65BE: mode 1, action 0 |
| 34 "look around" | mode 79, action 3 (368-372) | mode 57, action 3 | - | - | - | - |

The "event-stop" modes of request 12 (Sonic 20, Tails 18, Knuckles 16, Amy 13, E-102 14, Big 14) have **no physics at all**
(`case 20: break;` in SonicMain, `code_8C0A77BC.c:469-470`; the same empty case in TailsMain `code_8C0B6148.c:343`,
KnucklesMain `code_8C0C0640.c:333`, AmyMain `code_8C0D6B40.c:273`, E102Main `code_8C0CBE00.c:512`, BigMain
`code_8C0DFFA0.c:335`) and their mode-machine case only calls the request handler (Sonic `code_8C0A966C.c:601-609`), so the
pad is ignored and the player freezes where he is, also in the air. Mode 79 (request 34) is different: stand physics run
(`code_8C0A77BC.c:906-911`) and the mode returns to standing when the once-action ends (`code_8C0A966C.c:825-829`) - but only
Sonic and Tails have it, and only for their action 3.

The standing state is **mode 1 for all six characters** (the release functions above); kind 8's Super stand is mode 71.

### 2.2 The event action queue (what plays an arbitrary animation)

`code_8C04B440.c` (all C, matching or functionally verified; `include/sa1/engine.h:87-115` for `eventwk`):

| Function | Wrapper (asm, event library) | What it does |
|---|---|---|
| 8C04B4D2 `(twp, NJS_ACTION *, texlist, speed, mode, linkframe)` (83-101) | 8C04A120 EV_SetAction `(tp, action, texlist, speed, mode, linkframe)`: `if (tp) 8C04B4D2(tp->twp, ...)` | appends a `CAlloc`'d 0x18-byte entry: mode bit 0 = loop when last, linkframe, a COPY of the NJS_ACTION, texlist (NULL: keep the display's), speed (frames per call) |
| 8C04B528 `(twp, object, motion, texlist, speed, mode, linkframe)` (103-123) | 8C04A142 EV_SetMotion | the same with object and motion given separately |
| 8C04B48C `(twp)` (62-81) | 8C04A168 EV_ClrAction | frees the queue and the blend source (`buf44`), frame 0 |
| 8C04B57C `(twp)` (125-209) | - | the player: draws the head entry (`8C040942` = the action draw) at `ewp->f3C`; if the previous entry (`buf44`) exists and the head has a linkframe, draws a link (`8C041C92`) from the previous entry's end frame (frame 0 if it looped) to the head's frame 0 over linkframe calls; welds through 8C04BF00; advances `f3C += speed`; at the end of a once entry (or the wrap of a looping entry that has a successor) moves on, keeping the finished entry as `buf44` |
| 8C04BF00 (`code_8C04BF00.c`) | - | EV_PJoinVertexes: the player's welds re-evaluated for the queued (object, motion, frame), only weld entries of that object; node count from `plactptr[0].objnum`. **Requires `mj.jvmode == 1`**: for 0 or 2 the original spins forever (`code_8C04BF00.c:12-13`). jvmode is 0 from PInitialize until the first PJoinVertexes (the world's first step) and 2 after the player is freed |
| 8C04A180 | EV_WaitAction | the event script waits (8C049A60) while the queue is not empty |

Each display draws the queue instead of the gameplay action (Sonic `code_8C0AE0E0.c:133-134`: `if (ewp->list40 != NULL)
func_8C04B57C(twp); else ... plactptr[action] ...`; Tails `code_8C0BA820.c:159-160`, Knuckles `code_8C0C46AC.c:163`, E-102
`code_8C0CFACE.c:244-246` (also the node matrices 54 / 30 for his jets and arm), Amy `code_8C0DA106.c:183`, Big
`code_8C0E386E.c:107`, Tikal `code_0CB00780.c:108`). Everything else of the step is the gameplay's: the mode machine, the
physics, PSetMotion on the gameplay action, the gameplay welds (PJoinVertexes, then 8C04BF00 re-welds for the drawn pose),
the collision entry, the face task, the look task, the shadow. Consequences that matter for emotes:

* **Blends only between queue entries.** The first entry after a clear (or in a fresh world) starts without a blend:
  it pops in from the gameplay pose (measured 0.2-2.2 SA1 units, 4.4). The cutscenes do the same: EV001B clears the queue,
  then queues its first action with linkframe 0.
* **A blend needs the same node structure on both sides** (the link motion pairs node by node; 8C04B57C does not check).
  Measured fault: Sonic 83 (curled body, 74 nodes) queued right after 82 (body, 73 nodes) ended: access violation at guest
  0x16 in 8C057BC0, the world dead (Knuckles 34 after 33: the same). Same-object or same-node-count blends are safe (Tails'
  body and flight model have 68 nodes each: no fault).
* **The display decides the pace.** Sonic, Tails, Knuckles and Big draw only when the visibility test 8C038D00 passes, and
  it returns 0 on sub step 1 (LIB `src/rt/rt_draw.c` section 6, lines 1043 / 1066): their queues advance **once per pair**.
  Amy's and E-102's displays have no visibility test: **twice per pair** (`v1_machinima.md` Review R.2; measured: with the
  speeds of 4.3, `adv_per_pair` = 2 x racio in every row the probe could measure). Every display skips the advance while `wtimer & 2`
  (the hurt blink).
* **A small game leak.** A finished entry replaces `buf44` without freeing the old one when it has no linkframe (8C04B57C
  else-branch). Emotes avoid it by clearing (8C04B48C frees both) before each new emote.

### 2.3 What a cutscene does with a player (EV001B, and the import counts of the 207 event overlays)

The start of EV001B's script (`asm/overlays/ev001b/text/0CB80020.src`, 0CB800A0-0CB80142): 8C04A75A (event start), **8C04921A
= PadReadOff** (`code_8C0509A0.c:15-17`: `D_8C19E3A8 = 0`, after which 8C050BE4 hands every player a neutral pad record),
8C049220, **8C04A080(0)**, camera set-up, then for the player: 8C049DA0(0) (the player's task), **8C04A168** (ClrAction),
**8C04A120(tp, &SonicActionList_002_action, texlist, 0.5, 1 (loop), 0)**, position and angle (8C049EDA / 8C049F40) ... and
PadReadOn (8C049214) at the end.

8C04A080 (`8C0491C0.src:2083`) is the event library's player reset: 8C049E90 (ClrAction, ClrPath, look-at off, 8C04A416,
EV_ClrFace 8C04BD60), **SetInputP(pno, 24)** (back to the standing mode), SetVelocityP(pno, 0, 0, 0), and the drop of a held
object. Import counts over the 207 event overlays (`build/research/machinima/ev_stats.txt`): PadReadOff 202, 8C04A080 186,
EV_GetPlayer 189, EV_SetPos 184, EV_SetAng 182, **EV_SetAction 171**, EV_ClrAction 154, EV_SetFace 132, EV_SetPath 47,
`SonicActionList_000_action` 47, `SonicActionList_002_action` 28, `KnucklesActionList_000_action` 29, `AmyActionList_000_action`
23, `GammaActionList_000_action` 23; the scripted-pose wrapper 8C07CBBC (requests 12 / 9 / 10) is used by **one** overlay.

So the cutscenes' "event mode" for a player is: the pad off, the player in his ordinary **standing mode with the game's
physics running**, the animation from the event queue, movement from paths (`list4C`, the path mode) or the locks. It is
not the frozen request-12 mode. The global "event running" flag is `B_8C7492AC` (8C049A4E returns it as a bool); it gates
e.g. the idle voice (`code_8C07DCE0.c:75`); libsa1 never sets it, and an emote must not (unknown other effects).

### 2.4 What libsa1 already does with requests (super_general_design.md I.2, RC12)

libsa1 sends 46 / 47 (`SA1_ORDER_SUPER` / `NORMAL`), 3 (8C07C720, the balloon launch), 50 (8C07BBC2, deaths); G-SA1 sends
only the two orders. No libsa1 or G-SA1 path sends 9, 10 or 12-23 today, and `sa1_world_super.c` counts a General world
that reaches the Super modes through mode 20 (SA1W_SUPER_WARN_LEAK). Nothing touches the event work. The probe confirmed
the RC12 hazard: SetInputP(0, 12) then 24 in a General world ends in mode 71 with the warning "the General Super Sonic is in
the game's Super modes 71-78".

### 2.5 Decompilation state of what the mechanism calls

| Range | Status | Needed? |
|---|---|---|
| 8C04B440-8C04C100 (queue, player, event welds, face block) | 16 C + 2 NON_MATCHING (8C04BC70, 8C04BF00), lowered in every world | yes, as is |
| 8C04A120 / 8C04A142 / 8C04A168 / 8C04A180 (EV_SetAction / SetMotion / ClrAction / WaitAction wrappers, 34 + 38 + 24 + 42 B, asm) | asm | no: libsa1 calls the C bodies they call (the wrappers add `if (tp)` and `tp->twp`); optional for exactness |
| 8C0DE46C (Amy's hammer show / hide, `code_8C0DE2A0.c:85`) | C | yes for Amy's attack rows |
| GetAnalog 8C06CD22 (the game's stick test) | C | yes for the cancel rule |
| The rest of the event library (scheduler, camera, messages) | asm | no |

---------------------------------------------------------------------------------------------------------------------

## 3. Every animation of each character, with internal names (task item 2)

### 3.1 The files

One TSV per character (`emote_actions_<char>.tsv`; first line a `#` comment, then a header). One row per row of the
character's action table (PL_ACTION list: `{NJS_ACTION *actptr; Uint8 objnum; Sint8 mtnmode; Uint8 next; float frame;
float racio}`, `include/sa1/motion.h`), then one row per **extra** motion (action `-`).

| Column | Meaning |
|---|---|
| `action` | the row of the table (the number G-SA1 stores in a wheel slot); `-` for an extra motion |
| `internal_name` | the motion's file name from the SA Tools labels (`figure/sonic/models/s_wait0.nam.saanim` -> `s_wait0`): the developers' own name the owner asked for. Unlabelled rows (stage and MINICART motions, two others) get `(no label) <image>_<motion address>` |
| `label` | the SA Tools descriptive label (`Sonic_Stand`, `S_MOT_Sonic_Level_Clear_Pose`) - a ready "custom name" seed for later |
| `class` | stand, idle, pose, move, air, brake, ball, attack, carry, setpiece, hurt, fish, swim, board, super, stage, empty; `extra:<class>` for extra motions; `no` for extra motions that are not body motions |
| `wheel` | `yes` (plays in that character's worlds), `super-only` (B_CHAOS7 rows: the Super forms), `no` (not in G-SA1's worlds, or not a body motion) |
| `play` | `loop` (mtnmode 3 / 9 / 10 / 12), `once`, `hold` (once, then hold the last frame: mtnmode 5 / 7) |
| `frames`, `mtnmode`, `next`, `link`, `racio` | the game's PL_ACTION / NJS_MOTION fields (link = PL_ACTION.frame, the gameplay blend rate; racio = frames per step) |
| `chain` | for once-rows: the game's `next` chain up to its loop (e.g. Sonic 4 > 5 > 6 > 0, the idle) - what the gameplay plays after it |
| `ev_speed` | the event-queue speed (frames per display call) that reproduces the gameplay rate in a libsa1 world: racio x 2 for Sonic / Tails / Knuckles / Big (one advance per pair), racio for Amy / E-102; speed-scaled rows (mtnmode 9 / 10 / 11) use 1 frame per step, caller-set rows (12) 0.5 (host choice, 4.3) |
| `object`, `nodes` | the drawn object (SA Tools label) and its node count |
| `image`, `motion_addr`, `action_addr` | where the data lives (1ST_READ, S_MOT, K_MOT, A_MOT, B_MOT, E_MOT, S_SBMOT, M_SBMOT, B_CHAOS7, STGxx, MINICART) |
| `dup_of` | the first row with the same object and motion (G-SA1's list can hide duplicates: Knuckles 57-69 are all `k_wait0` chain rows, ...) |
| `needs` | props and warnings (held object, stage object, board, fishing tasks, B_CHAOS7, Amy's hammer call, no blend across objects) |
| `set_by` | the decompiled functions that request the row (constant writes to `reqaction`, with the mode they set): where the game uses it |
| `probe` | the catalogue result (pairs played, frames per pair, body draws, body extents, difference from the stand) |

`emote_actions_supersonic.tsv` maps Sonic's 142 rows onto the two Super Sonics (`kind8`, `general` columns: how each row is
played there, or why not).

### 3.2 Counts

| Character | Rows | Play in G-SA1's worlds | Not available | Extra motions playable | Notes |
|---|---|---|---|---|---|
| Sonic | 142 | 107 (+ the 12 Super rows 130-141, offered in the Super forms: a plain Sonic world holds B_CHAOS7 too, but his display gives them Sonic's texlist) | 23: stage overlays STG02 (40, 42, 44, 45), STG04 (46), STG07 (88), STG06 (89-93), STG08 (94-101), STG09 (126-129) | 0 (the 17 labelled extras are the EV head's shape motion and the board model's own motions) | 102-124 board rows: S_SBMOT is loaded, the board model is not drawn (0.9.5) |
| Tails | 132 | 131 | 89 (MINICART) | 9 (`m_taiki0-2` the unused yawn idle, `m_suberi0-1` sliding, `m_h_baneup0-3` jump with an object) | 37-53, 110, 111 use the flight model (68 nodes, as the body: blends between them are safe) |
| Knuckles | 111 | 110 | 36 (MINICART) | 12 (`k_taiki0-2` fist shaking, `k_wait_b0-1` fighting stance (both "unused idle"), `k_item_c0-2` / `k_item_fg0-2` looking at the shovel claws / fighting gloves, `k_kabe1`) | about 30 rows use the flight model (73 nodes, as the body) |
| Amy | 98 | 97 | 76 (MINICART) | 8 (`a_mogura0-2` crouching, `a_kakure0` hiding, `a_taiki0-2` frustration, `a_zitabata` launched) | attack rows 81-95: the hammer needs 8C0DE46C (4.2) |
| E-102 | 87 | 86 | 72 (MINICART) | 3 (`e_kimepose0` Level Clear Pose A, `e_kimepose0_e_kihon0_e102` (110 frames), `e_hstop0`) | 84 unlabelled (the end of his shaking, set by E102ModeMachine mode 54) |
| Big | 101 | 93 | 77, 79, 81-85 (empty rows), 96 (MINICART) | 3 (`b_item_u0-2`) | 100 unlabelled: his death on the ground (BigMain mode 54); fishing rows 49-73 without the fishing tasks |
| Super Sonic (kind 8) | 142 (Sonic's) | 92 (12 own rows 130-141 + 80 of Sonic's on the Super body) | the 23 stage rows, the 4 curled-body rows 14, 15, 83, 125 (no Super twin), the 23 board rows 102-124 (no S_SBMOT in his world) | - | Sonic's own NJS_ACTION as is draws Sonic's body with the SUPERSONIC texlist (wrong textures): libsa1 substitutes the object |
| Super Sonic (General) | 142 (GT) | 119 (GT rows on the golden body, 130-141 own) | the stage rows | - | 130-134 sit 1.0 low at Sonic's center_height: 0.9.4a N1's lifted copies (`super2_plan.md` 5.3, GA / `sg.lift`) |

Tikal has no action table (her model and motions are in TIKAL_PROG; she cannot tick yet: `sa1_character_tick` returns
SA1_ERR_UNSUPPORTED) and Eggman's 24 rows belong to a character that cannot tick; neither is in the wheel.

### 3.3 Internal names: what is there, what is not

* 671 table rows: 634 have an SA Tools motion label with a file name. The 37 unlabelled ones are Sonic's 23 stage rows,
  the 5 MINICART rows, Big's 7 empty rows, Big's 100 and E-102's 84 (the last two named here from the code that requests
  them).
* Several rows share a motion and differ only in loop mode, `next` or speed (the game's chain variants): `dup_of` marks
  them. For the owner's list the useful unit is the motion (the internal name), with one representative row.
* The labels are SA Tools' (community) names; the file names (`s_kimepose0`, `k_taiki1`, `ss_wait0`) are the developers'.
  Only names and addresses are kept: a generated name table in libsa1 or G-SA1 is allowed (owner rules: names, numbers,
  code maps).

### 3.4 Suggested starting wheels (8 slots, unique per character; the owner swaps them on the Options page)

| Character | Suggested slots (row or extra motion) |
|---|---|
| Sonic | 75 `s_kimepose0` (level clear pose), 3 `s_wait3` (look around), 5 `s_wait_a1` (idle), 26 `s_rollfly` (caught in wind), 70 `s_zitabata` (launched), 81 `s_furatsuki0` (dizzy), 84 `s_awa0` (air bubble), 47 `s_tukamari0` (hang on) |
| Tails | 54 `m_kimepose0`, 56 `m_losepose0` (lost race, chains into 57), `m_taiki0` (yawn, extra), 5 `m_wait_a1`, 33 `m_rollfly0`, 94 `m_zitabata`, 37 `m_flyup`, 43 `m_atack_1` |
| Knuckles | 40 `k_kimepose1`, `k_taiki1` (fist shaking, extra), `k_wait_b1` (fighting stance, extra), `k_item_fg1` (looks at his gloves, extra), 5 `k_wait_a1`, 39 `k_panch0`, 56 `k_panch2`, 30 `k_rollfly0` |
| Amy | 42 `a_kimepose0`, `a_taiki0` (frustration, extra), `a_kakure0` (hiding, extra), `a_mogura1` (crouch, extra), 5 `a_wait_a1`, 85 `a_rollat0` (hammer spin, with the hammer), 81 `a_attack1`, 87 `a_rollat2` (dizzy) |
| E-102 | 67 `e_taiki0` (level clear B), `e_kimepose0` (level clear A, extra), 43 `e_wait2` (fighting stance), 46 `e_wwait0` (propeller mode), 49 `e_hover1`, 77 `e_item_f1`, 80 `e_item_g1`, 62 `e_shoot0` |
| Big | 48 `b_kimepose0`, 2 `b_wait_a1`, 60 `b_joysmall`, 61 `b_joymiddle`, 62 `b_joybig0` (caught Froggy), 47 `b_zitabata0`, 92 `b_wwait0`, `b_item_u1` (extra) |
| Super Sonic (both) | 130 `ss_wait0`, 139 `s_kimepose0`, 131-133 `ss_move0-2`, plus any of Sonic's rows on the golden body |

These are only defaults for the first spawn; the choice is the owner's (and `faces_wheel_ui.md`'s).

---------------------------------------------------------------------------------------------------------------------

## 4. Which animations are safe, and what happens while one plays (task item 3)

### 4.1 Catalogue (every row of every character, `out/catalogue.tsv`)

Per character: one world on a flat GRID floor, settled 6 pairs; for each row with `sa1_action_info.status == OK` and an
NJS_ACTION in the world: queue it with 8C04B4D2 (speed `ev_speed`, the row's own loop / once, linkframe 8), play it to its
end (once) or 60 pairs (loop), then clear; a new world after a fault.

| Character | Played | Faults | Not loaded | Body draws while emoting | Advance per pair |
|---|---|---|---|---|---|
| Sonic | 118 | 1: row 83 (curled body) blended from 82 | 23 | 45 (46 curled, 1 ball, 47 Super body) | 2 x racio (speed doubled) |
| Tails | 131 | 0 | 1 | 48 (60 standing: the tail after-images are a gameplay-only draw) | 2 x racio |
| Knuckles | 109 | 1: row 34 (curled body) blended from 33 | 1 | 31-32 | 2 x racio |
| Amy | 97 | 0 | 1 | 55 | 2 x racio (speed = racio, two advances) |
| E-102 | 86 | 0 | 1 | 78 | 2 x racio |
| Big | 93 | 0 | 8 | 71 | 2 x racio |

In every played row the gameplay position did not move (drift 0.00) and the gameplay mode stayed 1. The two faults are the
cross-object blend of 2.2, not the rows themselves (rows 14, 15, 125 - the same curled body - played after a clear). Rule
for libsa1: **blend (linkframe > 0) only when the previous entry has the same object; otherwise clear first and start
without a blend.**

Extra motions (`out/probe_run.txt`, EV_SetMotion on the main body, once): all 35 listed in 3.2 played (no fault, the full
body drawn, 1.1-14.6 units away from the stand pose, i.e. visibly different). **Amy's `ad_dwalk` (0CC9368C, the barrel's
own "walking motions") crashed the probe process with an access violation** - not a world fault libsa1 caught. A motion
made for another object reads past its node data; libsa1 must accept only rows of the table and a validated list of
extra motions (the TSVs' `wheel = yes` rows), never an arbitrary motion address from a host.

### 4.2 Props and other tasks

| Class | What is missing | Safe? |
|---|---|---|
| carry (hold / with object / throw) | the held object: the hands pose around nothing | yes |
| setpiece (tree, pulley, rocket, monkey bars, ladder, barrel, Sky Deck cylinder, handle, balloon, bumper car, jump panel, shaking an object, Knuckles' wall climbing) | the stage object | yes |
| board (S_SBMOT 102-124, M_SBMOT 107-127) | the board model and the board task (0.9.5) | yes; better offered with the board |
| fish (Big 49-73) | the line, lure and fish tasks; the rod is part of Big's model (the body's triangle count does not change: **UNVERIFIED** whether a rod shows) | yes |
| swim (Big 86-92) | water (drawn in place on land; in water the game's own swim physics run underneath) | yes |
| attack (Amy 81-95) | the hammer: Amy's display scales the hammer node by `free.f[7]`, which follows the gameplay action (`code_8C0DA106.c:108-122`); the event library's **8C0DE46C(pno, 1.0)** forces it shown (`free.b[5] = 1`), (pno, 0.0) hides it again. Measured 932 -> 998 body triangles and back | yes, with the call |
| attack (E-102 62-65, Knuckles' punches, Tails' tail attack) | the beam / hit effects (gameplay tasks); E-102's display draws his jets and arm from the queued pose's node matrices (`code_8C0CFACE.c:244-246`) | yes |
| hurt (damage, death, drown, launched, dizzy) | nothing: visual only - no damage, no death, no ring loss | yes |
| ball (spin / curled / jump ball, other objects) | nothing; never blended from a body entry (4.1) | yes, with the rule |
| super (130-141) | B_CHAOS7: only in the Super forms | Super forms only |
| stage (STGxx), MINICART | the overlay: never in a G-SA1 world (the 0x0C900000 slot holds B_CHAOS7 or the stage stand-in) | not offered |

### 4.3 Pace

The gameplay plays a row at `racio` frames per 60 Hz step (PSetMotion mode 3 / 4 / 5, `code_8C079476.c:107-191`). The queue
advances `speed` per display call: once per pair for Sonic / Tails / Knuckles / Big, twice for Amy / E-102 (2.2). So
`speed = 2 x racio` for the first four and `racio` for Amy and E-102 reproduce the game's rate (the TSVs' `ev_speed`;
measured: `adv_per_pair` = 2 x racio in every row the probe could measure). Rows whose gameplay rate follows the speed (mtnmode 9 / 10 / 11: walks,
runs, the ball) have no rate when standing; 1 frame per step is a host choice that looks like a jog in place, and caller-set
rows (mtnmode 12: ladders, monkey bars, board leans) 0.5. A host speed multiplier can scale all of it. (The cutscenes pick
their own speeds: EV001B plays row 2 at 0.5 per call, twice the gameplay rate.)

### 4.4 How an emote ends, and the pops (`out/probe_pops.txt`; "jump" = the largest change of a body signature - extents
and centroid - between two consecutive pairs, SA1 units; the game's own stand -> walk: 0.24)

| Case | Measured |
|---|---|
| Once-row, queue runs out: the gameplay stand is drawn again | end pop 0.02 (row 3), 0.14 (24), 0.20 (59) |
| Once-row followed by a trailing stand entry (row 0, linkframe 8) | the emote blends into the stand; end pop **0.00** |
| First entry after a clear (no blend source) | pop-in 0.23 (row 3), 0.97 (59), 2.21 (24, whose first frame is a fall) |
| A lead-in entry (the gameplay's current row, finished in one call) before the emote | 0.66-1.52: no better (its blend source is the lead-in's last frame, not the gameplay frame) - not recommended |
| A loop cleared by stick input (the gameplay run starts in the same pair) | 1.18 |
| For comparison, mechanism B (request 12 + reqaction, released by 24) | while playing 0.25-1.98, release 0.01-1.07 (the gameplay's own PSetMotion blend) |

So: a once-emote ends by itself into the stand (with the trailing stand entry, invisibly); a loop plays until movement
input (cancel now) or the wheel again (stop with a blend: append the stand entry; the loop finishes its cycle and blends
- at most one loop length later). A hold-row (mtnmode 5) is played as once and ends; "hold the last frame" needs the loop
bit on a one-frame tail, which the queue cannot express without a host-made motion: offer hold-rows as once (default) or
as a loop. The game's own follow-ups make good multi-item emotes instead: 75 `s_kimepose0` then 76 `s_kimepose1` (Level
Clear Pose, then back to standing; one object, so blended), Tails 56 > 57 (lost race), Knuckles' `k_taiki0` > `k_taiki1`
> `k_taiki2`.

### 4.5 Physics and the game state while emoting

* **Identity (`out/probe_run.txt`).** World A: Sonic, 1,300 pairs (stand, run 60 pairs, jump, a short walk, a long stand).
  World B: the same input; emotes queued at pairs 20 (row 26 loop), 130 (3 once), 200 (81 loop), 300 (26 loop); the walk
  input cancels the emote before its pair. Result: **position, speed, mode, gameplay action, flags, rings identical in
  every pair; rand identical**; 1,110 pairs with an emote drawn; 3 cancels; both worlds hit the idle after 1,800 steps and
  sent the idle voice (8C07DCE0: voice 390 or the level's line), because the gameplay underneath simply stands.
* **In the air (Sonic created 60 units up).** Mechanism A: he falls with the game's physics (mode 12), lands at pair 21
  (mode 1), the queue keeps playing throughout. Mechanism B (request 12): he hangs at y 59.92 for 20 pairs; request 24 lets
  him fall.
* **What runs underneath** (the gameplay's standing mode with a neutral or real pad): gravity, ground following and the
  slope stand (on slopes steeper than about 25 degrees he slides as when standing), water (the drowning countdown runs while
  he emotes under water), moving floors carry him, damage hurts him (the knock-back moves him while the emote is drawn, and
  the queue pauses during the blink), springs and dash panels launch him, the idle timer runs (the idle voice after 30 s).
  All of it is the game's own; libsa1 cancels the emote when the gameplay leaves the standing state (4.6), so the pose does
  not slide around while he is knocked back or launched.

### 4.6 Movement input cancels: the game's own tests, at the latch

A world reads the host pad at the start of a pair. If an emote plays and the pair's pad would move him - the game's own
stick test **GetAnalog(twp, NULL, NULL)** (8C06CD22, `include/sa1/player.h:294`; dead zone and no-control timer included) is
non-zero, or the pad's press has the jump / action masks (`D_8C19E38C` / `D_8C19E390`, the masks the mode machines test) -
libsa1 calls 8C04B48C before the pair's first step. The pair then runs exactly as in a world that never emoted (the identity
test does this from the host; inside libsa1 it is the same order). The same check after the pair cancels when the gameplay
left the standing state: `taskwk.mode != 1` (kind 8: 71), `wtimer != 0` (hurt), the held-object flag 0x800 changed, the
form changed (Super / normal: the queued objects belong to the other form), or the board. With the option "allow in the
air" (default off) the state rule is only "hurt"; the emote then plays through falls and landings as in 4.5.

### 4.7 Super Sonic, both forms (`out/catalogue_super.tsv`, `emote_actions_supersonic.tsv`)

* **Kind 8 (Vs. Perfect Chaos)** runs SonicActionList as is: rows 0-129 name Sonic's body, which the SUPERSONIC texlist
  (archive 0 of his kind) cannot texture (measured: Sonic's NJS_ACTION 0 as is drew Sonic's body with archive 0 = wrong
  textures). libsa1 plays them with 8C04B528 on the Super body 0C909D2C (the jump ball 8C1DBB2C -> 0C90BD04; the curled
  body has no Super twin: not offered) and passes texlist 0C901EA0. Measured: 92 rows, every body draw archive 0, no fault.
  130-141 play from their own NJS_ACTIONs (B_CHAOS7). His standing mode is 71; the cancel rule uses it.
* **General form** (kind 0 with `SA1_SUPER_GENERAL`, and a Sonic who turned Super): the table in `plactptr` is GT (Super
  objects on Sonic's motions) and texlist_SONIC holds the Super look, so libsa1 takes the row from `plactptr` and keeps the
  texlist. Measured: 119 rows, every body draw archive 5 (SUPERSONIC in Sonic's set), no fault. Rows 130-134 from B_CHAOS7
  stand 1.0 lower than in kind 8 at Sonic's center_height 5.4: use 0.9.4a N1's lifted copies (`super2_plan.md` 5.3 / 6.1:
  GA, `sg.lift`) when that lands, else note the toe dip.
* **Form changes** (the transformation, the revert at 0 rings, `SA1_ORDER_*`) cancel the emote (the queued objects belong to
  the old form).
* **Mechanism B is unusable here** (2.4): request 12 then 24 sends the General form into the Super modes.

### 4.8 The board (0.9.5)

The board is a separate model drawn by the board task, placed from the player; its own motions (`sb_*`, `mb_*`, listed as
`no` extras) follow the board actions. An emote while boarding would pose the body off the board. The standing rule (mode 1)
already excludes the board modes; the board rows (S_SBMOT / M_SBMOT) can be offered when 0.9.5 lands, played while on the
board only if the board task follows the drawn pose (**UNVERIFIED**; 0.9.5's research).

### 4.9 The host side of libsa1 while emoting

* **Render interpolation**: the pair plan's motion hint reads the gameplay action (`sa1_character.c` `pair_info` /
  `frame_advance` / `hint_node`, as `super2_anim.md` 5.7 found for the General form). Measured with emotes drawn: largest
  mid-pair deviation of the body extents 0.01-0.81 (14 loops of all six characters) against 1.20 for the gameplay's own
  run from a stand: no visible error today. The hint should still follow the queue head (object, motion, `f3C`, speed) -
  the same kind of host fix as N1's 6.3.
* **CPU pose of a hidden pair** (`pose_general` / `pose_eval_current`): shows the gameplay pose unless it reads the queue;
  the same fix.
* **State**: `sa1_state_ex.action / frame` stay the gameplay's (correct for the game); the emote needs its own fields (5.2).
* **Multiplayer / puppets** (`sh_net.lua`, CPU pose from action + frame): an emote reaches other players only if its row (or
  extra motion id) and frame travel in the pose stream - with the "others can see you" work of 1.0.x.
* **Materials**: the emote draws the body's own textures (and the SUPERSONIC archive in the Super forms, Amy's hammer
  materials); nothing new for the pre-warm census.

### 4.10 Mechanism B (scripted pose), measured and rejected

Request 12, then `pwp->mj.reqaction = row` written by the host, then request 24 to release. It plays through PSetMotion,
so blends, chains and holds are the game's, and the render hint, CPU pose and puppets work unchanged. But: (a) no physics in
the event-stop modes - he hangs in the air and does not follow floors (measured); (b) the game never requests arbitrary rows
- the reqaction write is a host deviation of game logic; (c) the General Super form breaks (2.4); (d) the cutscenes do not
use it for animation (one overlay of 207). Not proposed; documented for the record.

---------------------------------------------------------------------------------------------------------------------

## 5. The mechanism and the API (task item 4)

### 5.1 The minimal faithful mechanism

An emote is what a cutscene does to a player, minus the pad-off and the camera:

1. **Start** (between pairs, `sa1g_world_call`): refuse before the world's first pair (8C04BF00 needs `jvmode == 1`; keep the
   request and run it after the first pair, as `SA1_ORDER_*` does) and when the rules of 4.6 say "not standing" (unless
   in-air is allowed). Then 8C04B48C (clear: no stale blend source, no leak), then for each item of the emote:
   * a table row: kind 8 with an object of Sonic's -> 8C04B528(twp, Super object, row motion, 0C901EA0, speed, mode, link);
     every other case -> 8C04B4D2(twp, `plactptr[row].actptr`, NULL, speed, mode, link) (`plactptr`: GT / GA in the
     General form, the table in every other world);
   * an extra motion (validated list) -> 8C04B528(twp, the main body, motion, NULL, speed, mode, link);
   * speed = the TSV rule (4.3) x the host multiplier; mode bit 0 = loop for the last item; link = 0 for the first item, the
     host's (default 8) for later items of the same object, 0 across objects;
   * Amy's attack rows: 8C0DE46C(0, 1.0) (undone with 0.0 when the emote ends or is cancelled).
   * For a once-emote, a trailing entry: the stand row of the form (0; kind 8: 130 on the Super body; the General form:
     `plactptr[0]`), once, link 8 if it has the emote's object (else 0), so the emote blends into the stand (4.4).
2. **Each pair** (inside the world tick, before the first step): the input rule of 4.6 (GetAnalog / press masks) -> clear;
   after the pair: the state rule -> clear; the queue empty -> the emote is over (undo Amy's hammer).
3. **Stop** by the host: NOW = 8C04B48C; BLEND = append the stand entry (link 8) after the current entry.

Everything the world executes is the game's own code; the only host decisions are which row, how fast, and when to clear -
the decisions a cutscene script makes. Nothing is written into game state by the host.

### 5.2 API sketch (`LIB/include/libsa1.h`, its own section; numbers taken at edit time)

```c
/* ---- emotes (API 0.x, SA1_API_EMOTE; sadc docs/research/emote_wheel_lib.md) ------------------------------------------ */
#define SA1_API_EMOTE 1

/* one animation of an emote: a row of the kind's action table, or an extra motion of the kind's validated list */
typedef struct sa1_emote_item {
    uint32_t source;            /* SA1_EMOTE_ROW or SA1_EMOTE_EXTRA */
    uint32_t index;             /* the action row (sa1_action_info) / the extra index (sa1_emote_extra_get) */
    int32_t  play;              /* SA1_EMOTE_PLAY_DEFAULT (the row's own: loop / once; hold rows once), _ONCE, _LOOP */
    float    speed;             /* multiplier of the game's rate (racio); 0 = 1 */
} sa1_emote_item;
#define SA1_EMOTE_ROW    0u
#define SA1_EMOTE_EXTRA  1u
#define SA1_EMOTE_PLAY_DEFAULT 0
#define SA1_EMOTE_PLAY_ONCE    1
#define SA1_EMOTE_PLAY_LOOP    2

typedef struct sa1_emote {
    uint32_t       struct_size;
    uint32_t       count;       /* 1..SA1_EMOTE_MAX_ITEMS items played in order (the last may loop) */
    sa1_emote_item items[8];
    int32_t        link;        /* blend frames between items of the same object (and into the stand); -1 = 8, 0 = none */
    uint32_t       flags;       /* SA1_EMOTE_* */
} sa1_emote;
#define SA1_EMOTE_MAX_ITEMS  8u
#define SA1_EMOTE_CHAIN      0x1u /* a once-row: also the game's PL_ACTION.next chain up to its loop (4 > 5 > 6 > 0) */
#define SA1_EMOTE_NO_TRAIL   0x2u /* no trailing stand entry after a once-emote (the gameplay stand pops back) */
#define SA1_EMOTE_IN_AIR     0x4u /* may start when not standing; cancelled only by input and hurt (4.6) */
#define SA1_EMOTE_KEEP_INPUT 0x8u /* no input cancel (the host cancels itself) */
#define SA1_EMOTE_NO_PROPS   0x10u /* Amy: do not show the hammer */

/* Starts an emote at the character's next pair (before its first pair: right after it). Replaces a running one.
 * SA1_ERR_NOT_FOUND (no character), SA1_ERR_UNSUPPORTED (a puppet, Eggman / Tikal; or not standing and no
 * SA1_EMOTE_IN_AIR: nothing queued, sa1_last_error says why), SA1_ERR_INVALID_ARGUMENT (a row or extra not offered for
 * this kind and form: stage / MINICART rows, empty rows, the curled body in kind 8, Super rows outside the Super forms) */
SA1_API sa1_status sa1_character_emote(sa1_context *ctx, sa1_character_id id, const sa1_emote *emote);
#define SA1_EMOTE_STOP_NOW   0u   /* EV_ClrAction now: the gameplay pose in the next pair */
#define SA1_EMOTE_STOP_BLEND 1u   /* the stand blended in after the current item (a loop finishes its cycle) */
SA1_API sa1_status sa1_character_emote_stop(sa1_context *ctx, sa1_character_id id, uint32_t how);

/* the catalogue: sa1_action_info grows at its end (struct_size): */
    const char *internal_name;  /* "s_kimepose0"; "" when the disc data has none */
    const char *label;          /* "S_MOT_Sonic_Level_Clear_Pose" (SA Tools) */
    uint32_t    emote_class;    /* SA1_EMOTE_CLASS_* (stand, idle, pose, move, air, brake, ball, attack, carry, setpiece,
                                   hurt, fish, swim, board, super, stage, empty) */
    uint32_t    emote_flags;    /* SA1_EMOTE_OFFERED, _NEEDS_PROP, _SUPER_ONLY, _BOARD, _DUP (a duplicate of another row) */
SA1_API uint32_t   sa1_emote_extra_count(const sa1_context *ctx, int32_t kind);
SA1_API sa1_status sa1_emote_extra_get(const sa1_context *ctx, int32_t kind, uint32_t index, sa1_action_info *out);

/* sa1_state_ex grows at its end: */
    uint32_t emote_state;       /* 0 none, 1 playing, 2 blending into the stand */
    int32_t  emote_item;        /* the item playing (0..count-1), -1 none */
    int32_t  emote_row;         /* its row (-1 for an extra) */
    int32_t  emote_extra;       /* its extra index (-1 for a row) */
    float    emote_frame;       /* ewp->f3C */
```

Option plumbing: none needed in the game - the queue only runs when the host calls. The reference worlds never call it, so
`build.py --test` (M1, M11, P7, HOSTOBJ, RINGS, DROWN, HUD, SUPER, ITEMBOX) is unaffected by construction.

### 5.3 LIB internals (described; LIB is not edited by this research)

| File | Change |
|---|---|
| `src/world/sa1_world_emote.c` (new) | the world side: the start (5.1 step 1) inside `sa1g_world_call` with the game functions through `sa1_fn` (8C04B48C, 8C04B4D2, 8C04B528, 8C0DE46C, 8C06CD22); the pending request before the first pair; the per-pair rules (5.1 step 2) at the latch and after the pair; the Super object substitution (k_super_subst of `sa1_manifest.c`) and the SUPERSONIC texlist for kind 8; the extra-motion list per kind |
| `src/world/sa1_world_tick.c` (p), `sa1_world.h` (p) | one call before the first step of a pair and one after it while an emote is active |
| `src/sa1_api_world.c` / `.h` (p) | the state fields |
| `src/sa1_character.c` (p) | the API entry points; `sa1_action_info` names / class; the render hint and the CPU pose read the queue head while it plays (4.9) |
| `src/sa1_emote_names.c` (generated) | names, labels, classes, flags, extra-motion lists from these TSVs (metadata only) |
| `tests/emote_test.c`, `tests/build_emote.py` (new) | section 6 |
| `include/libsa1.h` | 5.2 |

The probe's 30-line helper shows the calls work from outside the world as they are (`probe_emote.c` `evq_cb`).

### 5.4 What G-SA1 needs from this (the UI itself: `faces_wheel_ui.md`)

* The list for the Options page: per kind, every row with `emote_flags & SA1_EMOTE_OFFERED` and every extra, showing
  `internal_name` (the owner's request), grouped by class, duplicates folded; Super rows only for the Super entries.
* The wheel per character kind (unique per character; the Super entries use Sonic's list on the golden body plus
  130-141): a ConVar per kind holding the slot list (rows as numbers, extras as `x<index>`), a Q-menu control per slot.
* The hold key: open the wheel on press, `sa1_character_emote` on release with the selected slot; selecting the playing
  emote again = `SA1_EMOTE_STOP_BLEND`.
* Input: G-SA1 keeps sending the real pad; libsa1 cancels on movement by the game's own rule. While the wheel is open the
  mouse moves the cursor, not the camera.
* Logging: one "emote" line per start / end / cancel (row, internal name, why it ended), so the owner's notes can be matched.

---------------------------------------------------------------------------------------------------------------------

## 6. Tests

LIB `tests/emote_test.c` (with the disc), each a case of the probe made strict:

| Case | Checks |
|---|---|
| emote_identity | two worlds, 1,300 pairs of the same input, one emoting (queued rows, cancels by input): position, speed, mode, action, flags, rings, rand identical in every pair |
| emote_catalogue | every offered row and extra of every kind (and both Super forms) plays without a fault; body draws > 0; the queue ends for once-rows within frames / speed + 2 pairs; the gameplay mode stays 1 (71) |
| emote_cross_object | Sonic 82 then 83, Knuckles 33 then 34 (the probe's faults): no fault (the link rule) |
| emote_pacing | per kind: the queue's frame advance per pair = 2 x racio (Sonic, Tails, Knuckles, Big with doubled speed; Amy, E-102 plain) |
| emote_cancel | stick input (GetAnalog), jump, action: the queue is empty before the pair; that pair's state equals the no-emote world; hurt, a spring, the form change and leaving the ground cancel after the pair |
| emote_end | once-row with the trailing stand: the end jump < 0.05; `SA1_EMOTE_STOP_BLEND` on a loop ends within one loop length + link |
| emote_air | with SA1_EMOTE_IN_AIR from 60 units: he falls and lands as without an emote; without it: SA1_ERR_UNSUPPORTED |
| emote_first_pair | an emote before the first pair is carried out after it (never with jvmode != 1) |
| emote_super | kind 8: Sonic's rows on the Super body, every body draw archive 0; 130-141; the curled rows refused; General: GT rows archive 5; a revert cancels |
| emote_amy | an attack row shows the hammer (+66 triangles), the end / cancel hides it |
| emote_extra | the validated extras play; any other motion is refused (SA1_ERR_INVALID_ARGUMENT); `ad_dwalk` is not in the list |
| emote_render | mid-pair deviation while emoting below the gameplay bound; the CPU pose of a hidden pair equals the drawn emote (after the 4.9 fix) |
| emote_memcheck | SA1_GUARD_ALLOC: 200 emotes started / cancelled / ended: the world heap returns to its level, 0 live host blocks |
| (gates) | `build.py --test --target all` strict replays unchanged; `super_test` KIND8_TRACE unchanged |

G-SA1: binding tests for the new functions and constants; Lua tests of the wheel's ConVars and the Options list (rows come
from the module, not from a hand list); a core test that plays one emote per kind through Session's render.

---------------------------------------------------------------------------------------------------------------------

## 7. Risks and UNVERIFIED points

| Point | Status |
|---|---|
| How the emotes look in Garry's Mod | **UNVERIFIED** (not run in game; GMod builds were not allowed now). Numbers are from libsa1's render output |
| The pop-in of the first entry (no blend from the gameplay pose) | the game's own rule; measured up to 2.2 units for a row that starts in a fall pose; most stand-like rows 0.2-1.0 |
| Arbitrary motions | a mismatched motion crashed the process: only the validated list may be accepted (4.1) |
| Cross-object blends | fault: the link rule is required (4.1) |
| The idle voice and other gameplay events while emoting | the game's rule (he is standing); an owner question (8) |
| Tikal, Eggman | cannot tick; not in the wheel |
| Board rows while boarding | 0.9.5 (4.8) |
| Big's rod during fishing rows | **UNVERIFIED** visually (4.2) |
| The SA Tools labels name the motions; the descriptions in 3.4 are from the labels, not from rendered pictures | names only |
| Multiplayer | emotes stay local until the pose stream carries them (4.9) |
| The recorder (2.0) | an emote is a host call between pairs: a journal entry with its pair index (`v1_machinima.md` 4.2); deterministic (the identity test) |

---------------------------------------------------------------------------------------------------------------------

## 8. Owner questions (each with a default; nothing blocks)

| # | Question (plain words) | Default |
|---|---|---|
| E1 | Emotes in the air: start only while standing (the default), or also while jumping / falling (the character keeps falling and lands while posing)? | standing only; an option "Allow emotes in the air" off |
| E2 | When a character stands still for 30 seconds the game plays his idle voice line - also while he is doing an emote? | yes, the game's rule |
| E3 | The list shows every animation, including ones meant for objects that are not there (carrying, climbing, the board, Big's fishing). Show them too, or hide them by default? | show all, grouped, with a note for the ones that need an object |
| E4 | Choosing the playing emote again on the wheel: stop it at once, or let it finish its loop and blend back into standing? | blend back |
| E5 | Animations that are the same move in the game's list several times (the game's variants): show each, or one per animation? | one per animation (the internal name) |

---------------------------------------------------------------------------------------------------------------------

## 9. Evidence (`SADC/build/research/faces094a/`)

* `lib/libsa1.a` = `g-sa1/build/deps/x86_64-windows/libsa1.a` of the 0.9.4 snapshot (sha1
  7203ffbe3330b535fa525af0ea8b6f71aaf9d0ac, LIB build 2026-10-26 17:54:28, snapshot 18:20:57, copied 18:26:56);
  `lib/include/libsa1.h` sha1 cf501e01dbeb4c5b8b8a03140b274c6370a8143c; `lib/README.txt`, `lib/SNAPSHOT_source.txt`.
* `probe_emote.c`, `build_probe.py` (zig cc, IDLE priority, against `lib/` only): `set SA1_TEST_GDI=<GDI>` then
  `py -3.12 build_probe.py probe_emote.c --run` (`EMOTE_ONLY=cat,unb,amy,interp,csup,id,air,pop,super` selects parts; the
  barrel motion is left out because it crashes the process). Outputs: `out/catalogue.tsv` (every row of six characters),
  `out/catalogue_super.tsv` (both Super Sonics), `out/probe_run.txt` (all parts), `out/probe_pops.txt` (the blends).
  The probe declares five internal libsa1 functions (`sa1_character_world`, `sa1g_world_ptr`, `sa1g_world_ext_alloc`,
  `sa1g_world_call`, `sa1_fn`; the segment table is not needed) and reads no internal struct.
* `scripts/dump_actions.py` -> `out/raw_<char>.tsv`, `out/unbound_<char>.tsv`; `scripts/setters.py` -> `out/setters.json`;
  `scripts/annotate_actions.py` -> `SADC/docs/research/emote_actions_*.tsv` (`py -3.12 -I <script>`). They read the local
  images and the labels only; numbers and names are written.
