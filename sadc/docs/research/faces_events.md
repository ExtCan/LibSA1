# G-SA1 0.9.4a addition: the game's faces - EV heads, face scripts, gameplay faces - and what libsa1 needs for event faces and lip sync (`faces_events`)

Status: research, 2026-10-26 evening (agent "res-faces" of the 0.9.4a faces / wheel preparation; companion documents
`faces_wheel_ui.md` (res-ui, the G-SA1 side) and `emote_wheel_lib.md` (res-emote, the body animations)). Read-only for the
trees: nothing in LIB, g-sa1, SADC/src, SADC/config, SADC/include or SADC/harness was changed, no build script of those trees
ran, nothing was copied into the Garry's Mod install. Experiments ran only in
`SADC/build/research/faces094a/res_faces/` against a COPY of the 0.9.3a libsa1 snapshot (appendix A). Round 0.9.4 (rings and
item boxes) was building while this was written.

Owner request (2026-10-26, rounds plan 14.1s): event faces, animatable and easy to use, a switch between event faces and
regular faces, microphone lip sync "like how Garry's Mod can lip sync speaking into microphones"; reference
https://sadocs.unreliable.network/wiki/Sonic_Adventure/Character_Faces#Animation_List (read for this document; it describes
SADX on PC - every address below is the Dreamcast's, found in SADC).

Abbreviations: SADC / LIB / G-SA1 = `<project>\{sadc,libsa1,g-sa1}`. "Step" = one 60 Hz logic step,
"pair" = two steps (1/30 s, one `sa1_character_tick` pair; the character is drawn on its first step). "EV head" = the event
head, "static face" = the gameplay head part it replaces. Letters 'A'-'Z', 'a'-'e' = face sequences 0-30. **UNVERIFIED**
marks what is derived and not measured. "Measured" = `out_probe.txt` / `out_big_invalid.txt` / `out_static.txt` of
appendix A, unless another source is named.

---------------------------------------------------------------------------------------------------------------------

## 0. Summary

1. **All of the face code is decompiled and already runs in every libsa1 world.** EV_SetFace = `8C04B39E` (asm wrapper) ->
   `8C04BBCE` (C), EV_ClrFace = `8C04B3B4` (asm wrapper) -> `8C04BD60` (C), PFaceChange = `8C07B2B8`, PFaceCancel =
   `8C07B2DC`, PlayerFaceMotionExec = the face task exec `8C07B106` (with the blend `8C07B080` and the lerp `8C055EFE`), the
   face table faceTbl = `D_8C19C32C` (31 sequences of `{Sint8 time, Sint8 key}` steps). Every C function is matched except
   `8C04BC70` (NON_MATCHING, registers only); all are lowered into libsa1 (`sa1g_fntab.c`), none is stubbed. The two event
   library wrappers are 36 bytes of asm and are not needed (libsa1 can call the C they jump to).
2. **A face is a second head model whose vertices are blended between shape keys.** Each character with a face has an
   "EV head" (Sonic 2 nodes / 427 points, Tails 3 / 382, Knuckles 2 / 440, Amy 5 / 427, Big 7 / 346, Tikal 5 / 428) and a
   shape motion of vertex keys. The face task links the EV head in place of the static face (one sibling pointer of the
   head node) and writes `points = lerp(key[face], key[face_next], rate)` into it every step. No texture swaps, no node
   motion, and the normals are never blended (only the vertex keys `p[0]` are read, `code_8C07B080.c:105-111`). All of it
   is plain data in `1ST_READ.BIN` (always resident; Tikal's in `TIKAL_PROG.PRS`) and uses the character's own texture
   archive, which libsa1 already loads: **nothing new to load**.
3. **Per character:** Sonic 21 keys (20 distinct: 19 = 20), Tails / Knuckles / Amy / Tikal 20, all 31 letters valid;
   **Big 8 keys, 7 distinct (5 = 6), and on his face mesh only 6 distinct shapes (5 = 6 = 7) - the wiki's "6 faces"**; 14
   letters valid (A B C D E K L S T U V W X Y); 11 letters read his normal-key array as vertices (a wrong face, no crash) and
   6 letters (G P Q b c d) read garbage pointers: **measured world fault in `8C055EFE`** (libsa1 catches it, the world is
   dead). E-102: no face task at all (measured `playerwk.pfp` NULL). **Super Sonic: no EV head in the game** - the face task
   runs (it switches Sonic's normal tree, which is not drawn), so neither kind 8 nor the General form shows a face while
   Super (measured: 0 EV draws). Eggman: none. Tikal: has one, but cannot tick in libsa1.
4. **libsa1 today:** the game's own hurt face 'I' already shows on Sonic, Tails, Knuckles and Amy when they are hit
   (measured: EV head drawn with its own textures for the 32 steps of 'I'); the display capture snapshots the morphed
   points of every EV-head draw (v3 snapshots) and `sa1_character_render` emits them with the EV-only textures and lerps the
   points between pairs. Gaps: no API; the CPU pose of a hidden pair draws the static face (`sa1_pose.c` uses the charset's
   static tree); the EV-only texture states (flags 0x30) are missing from G-SA1's material census (pre-warm).
5. **Mechanism (all host-driven, all off in the reference worlds):** libsa1 latches face calls and applies them at the
   start of the next pair, before the executor, through the game's own functions: mode switch = `8C04BBCE("")` (the EV head
   with the game's idle: neutral 'A' 75 %, blink 'B' 25 %, 8 steps each, forever) / `8C04BD60` (back to the regular face);
   play = `8C04BBCE(script)` (the game queues: a new script starts when the running sequence ends, measured up to 42 steps
   later) plus an optional interrupt; hold = re-arm the one-letter script each pair (measured: no gap); cutscene pace = the
   world's face table pointed at copies with doubled times (events run 30 steps/s); **lip sync** = each pair the host level
   becomes the face block's two keys and rate on a mouth ladder (closed 0 - narrow 5 - small 4 - round 6 - open 7), drawn by
   the game's own mode-4 blend exactly one pair later (measured, rms 0). API sketch in 6.9:
   `sa1_character_face_mode`, `sa1_character_face`, `sa1_character_face_level`, `sa1_face_info_get`.

---------------------------------------------------------------------------------------------------------------------

## 1. Sources and method

| What | How |
|---|---|
| Code | SADC C (file:line below), the asm of the event library wrappers (`asm/1st_read/text/8C04B360.src`), libsa1's lowered code map (`LIB/build/pclower/ga64/sa1g_fntab.c`, read only) and `gen/policy/stubs.tsv` |
| Data | `static_faces.py` and `big_keys.py` (appendix A) walk the EV heads, static faces, shape motions and look-at tables in SADC's built images (`build/1ST_READ.BIN`, `build/TIKAL_PROG.BIN`, `build/B_CHAOS7.BIN`); numbers only, no game bytes written |
| Behaviour in libsa1 | `probe_faces.c` against a COPY of the 0.9.3a archive (appendix A): one world per character on a flat GRID floor; between pairs it writes exactly what the game's own setters write (8C04BBCE / 8C04BD60 / 8C07B2B8 stores), and reads per pair the face block, the face work, the head linked under the face parent, the capture records of the pair (model draws and their v3 point snapshots) and `sa1_character_render`'s draws. The drawn EV face is identified by a least-squares fit of the snapshot to `lerp(key a, key b, t)` (residual 0.0000 in every fit) |
| Hurt path | `sa1_character_damage` (the game's own damage path: taskwk.flag 4) |
| Earlier research | `v1_machinima.md` 2 with its Review (R.1-R.4) - confirmed here where re-measured; `sa1_assets.csv`; the sadocs wiki page (SADX) |

---------------------------------------------------------------------------------------------------------------------

## 2. (Q1) The code: Dreamcast addresses, decompilation state, structures

### 2.1 Functions

| SADX name (wiki) | DC address | What it does | SADC source | State | In libsa1 |
|---|---|---|---|---|---|
| `EV_SetFace(task *tp, char *str)` | `8C04B39E` | `if (tp) 8C04BBCE(tp->twp, str)` (tail jump, `MOV.L @(32,R4)`) | `asm/1st_read/text/8C04B360.src:52-64` | asm (22 B) | not lowered; not needed |
| (its body) | `8C04BBCE` | store `face_script`; if no face runs (`face_time == 0`): `count = 0, time = 1, face = face_next = 0` | `src/1st_read/code_8C04B7BA.c:260-273` | matched | fntab `0x8C04BBCE` |
| `EV_ClrFace(task *tp)` | `8C04B3B4` | `if (tp) 8C04BD60(tp->twp)` | `8C04B360.src:65-73` | asm (14 B) | not needed |
| (its body) | `8C04BD60` | `face_script = face_seq = NULL, face_time = 0` | `code_8C04BC70.c:115-123` | matched | fntab |
| (script reader) | `8C04BC20` | next script char: 'A'-'Z' -> 0-25, 'a'-'z' -> 26-51, '0' -> -1, other -> its byte; 0 at the end (script dropped) | `code_8C04B7BA.c:276-298` | matched | fntab |
| (sequence start) | `8C04BC70` | n < 0: stop (8C04BD60); n == 0 or empty: random idle, `rand() * (1/32768) < 0.75` ? 'A' : 'B'; else `face_seq = D_8C19C32C[n]` | `code_8C04BC70.c:63-84` | NON_MATCHING (registers only), functionally complete | fntab |
| (face block step) | `8C04BCC8` | if `face_time`: `++count >= time` -> `face = face_next, count = 0`, at a sequence end the next one from the script, then read the next step `{time, face_next}` | `code_8C04BC70.c:91-112` | matched | fntab |
| `PFaceChange(pno, faceno)` | `8C07B2B8` | `playerpwp[pno]->pfp->seq = D_8C19C32C[n]` | `code_8C07B2B8.c:36-46` | matched | fntab |
| `PFaceCancel(pno)` | `8C07B2DC` | `pfp->seq = NULL` | `code_8C07B2B8.c:49-59` | matched | fntab |
| `PlayerFaceMotionExec(task *tp)` (PC 0x440560) | `8C07B106` | the face task exec: calls 8C04BCC8, then the mode machine (2.4) | `code_8C07B080.c:145-239` | matched | fntab |
| (blend a tree) | `8C07B080` | for each node of the EV head tree (child, then sibling, index in tree order): `points = lerp(md->p[0][face].key, md->p[0][face_next].key, rate)` | `code_8C07B080.c:96-122` | matched | fntab |
| (dest) | `8C07B0F6` | `parent->sibling = sibling` (the static face back) | `code_8C07B080.c:125-133` | matched | fntab |
| (lerp) | `8C055EFE` | `dst = a * (1 - t) + b * t` over 3n floats (SA1_FMAC) | `code_8C055ECE.c:55-72` | matched | fntab |
| face task creators | Sonic `8C0B2338`, Tails `8C0BF76A` (exec `8C0BF796` picks the normal or the flight head each step, then 8C07B106), Knuckles `8C0C8E8E`, Amy `8C0DDDB2`, Big `8C0E59C0`, Tikal `0CB00ED4` | `CAlloc(1, 0x20)` the PL_FACE into `tp->mwp`, set parent / sibling / object / motion, exec 8C07B106, dest 8C07B0F6 | `code_8C0B2338.c:50-61`, `code_8C0BF262.c:257-288`, `code_8C0C8A80.c:199-212`, `code_8C0DDA00.c:183-196`, `code_8C0E59C0.c:35-48`, `src/overlays/tikal_prog/code_0CB00780.c:334-347` | matched (Tikal's creator is an ASM_STUB in `code_0CB00000.c:382`, its C in `code_0CB00780.c`) | fntab (Tikal: TIKAL_PROG map) |
| created by | each main's mode 0: `CreateElementalTask(2, 3, creator)` | | Sonic `code_8C0A77BC.c:216`, Tails `code_8C0B6148.c:161`, Knuckles `code_8C0C0640.c:150`, Amy `code_8C0D6B40.c:125`, Big `code_8C0DFFA0.c:189`, Tikal `code_0CB00000.c:120`; E-102 has none | | |

The callers of PFaceChange in gameplay (all measured or read): the damage paths with 'I' (8): Sonic `code_8C0AFB10.c:115`,
Tails `code_8C0BBA4E.c:89`, `code_8C0BBDD6.c:311`, `code_8C0BC61C.c:219`, Knuckles `code_8C0C5680.c:114`, Amy
`code_8C0DAC46.c:185` (Big's damage path has none: measured, no face on a hit); 'e' (30) every step of Sonic's mode 43
(`code_8C0A77BC.c:439`) and Knuckles' mode 48 (`code_8C0C0640.c:450`), the frozen-in-ice modes, which PFaceCancel when they end
(`code_8C0A966C.c:802,821`, `code_8C0C193A.c:929,948`).

### 2.2 The face table (faceTbl, `D_8C19C32C`)

31 pointers at `0x8C19C32C` to sequences at `0x8C19C1F8`-`0x8C19C327`; entry 31 is `0x00020109`, not a pointer, so a script
letter 'f'-'z' or any digit other than '0' indexes garbage (`include/sa1/engine.h:223`; review R.1 of `v1_machinima.md`). A
step is `{Sint8 time (steps), Sint8 key}`; `time == 0` ends the sequence. Section 4.2 lists all 31.

### 2.3 Structures (DC layout; libsa1 worlds use the same guest layout)

**facewk** (the event face block; SADX `ewp->face`: old, __new, nbFrame, frame, dataPtr, tablePtr) is `eventwk` bytes
0x5C-0x6B, `playertwp[pno]->ewp` (taskwk +0x3C, `include/task.h:73`; eventwk `include/sa1/engine.h:87-115`, allocated
0x7C bytes by `8C04B440` from PInitialize for every player, so it exists in every world):

| Offset in eventwk | Field | SADX name | Meaning |
|---|---|---|---|
| 0x5C | `Uint8 face` | old | key blended from |
| 0x5D | `Uint8 face_next` | __new | key blended to |
| 0x5E | `Sint16 face_time` | nbFrame | steps of the current step; **0 = no event face** |
| 0x60 | `Sint16 face_count` | frame | steps done; the drawn rate is `count / time` |
| 0x64 | `const char *face_script` | dataPtr | the script, read one letter per finished sequence |
| 0x68 | `const char *face_seq` | tablePtr | the reader in the current sequence (points at the next unread step) |

**PL_FACE** (the gameplay face work; SADX's PL_FACE has more fields: lastface, face, reqface, tblpoint, frame, framespeed,
tbl, sibling, faceorg, facebuf, facetypes - the DC's is 0x20 bytes) is `playerpwp[pno]->pfp` (playerwk +0x070,
`include/sa1/player.h:158`; layout `sa1_PL_FACE`, `player.h:116-133`): 0x00 `face`, 0x01 `face_next`, 0x03 `step`, 0x04
`rate`, 0x08 `rate_spd`, 0x0C `seq` (PFaceChange's sequence, NULL = none), 0x10 `parent` (the node whose sibling is
switched), 0x14 `sibling` (the static face), 0x18 `object` (the EV head), 0x1C `motion` (the shape motion).

### 2.4 One step of the face task (`8C07B106`, `code_8C07B080.c:145-239`)

1. `8C04BCC8(player)`: advance the event face block (only face tasks and the event test task 8C04BD9E call it: a character
   without a face task never advances it).
2. The mode machine:

| Mode | What | Head linked |
|---|---|---|
| 0 | publish `pfp`; to 1 (or 2 if a sequence is already set) | static |
| 1 | if `face_time != 0` -> 4 (falls through); else if `pfp->seq` -> 2 (next step); else stays | static |
| 2 / 3 | gameplay sequence: `face = face_next`, next step's key, `rate_spd = 1 / time`, blend; at the end `seq = NULL` and back to 1 | EV head while blending |
| 4 | event face: `face, face_next` from the block, `rate = count / time`, blend; back to 1 when `face_time == 0` | EV head |

Consequences (all measured in libsa1, section 5):

* **The event face has priority**: while `face_time != 0` the task never looks at `pfp->seq`, so the hurt face waits; it plays
  (stale) as soon as the event face stops if it is still set (measured: a hit during an event face, then EV_ClrFace -> the
  full 'I' plays afterwards).
* **A finished script idles forever**: `8C04BC20` returns 0 -> `8C04BC70(0)` -> 'A' (75 %) or the blink 'B' (25 %), 8 steps
  each, until a '0' in a script or EV_ClrFace (wiki: "plays a blinking animation whenever it's loaded").
* **A new script waits**: `8C04BBCE` only stores the pointer while a face runs; the letter is read when the running sequence
  ends (measured: "V" set two pairs into 'C' started 21 pairs later).
* **The character is drawn before its face task runs in the step** (the display is inside the player main, the face task is
  a later task), so the drawn face is the previous step's and a link change shows one pair late (measured: EV_SetFace ->
  first EV draw in the 2nd pair; EV_ClrFace -> the static face from the 2nd pair).

### 2.5 Pace: gameplay vs cutscenes

The face block advances once per logic step. Gameplay runs 2 steps per 30 Hz frame (60 steps/s); the event library sets one
step per frame (`8C049298` -> `8C051760(2, 1)`, restored by `8C051760(2, 2)` in `8C049560`; `v1_machinima.md` Review R.2
item 2, asm only), so **faces in cutscenes run at half the gameplay speed** (30 steps/s) - the wiki: "Animations look very
fast outside events because the game is slowed down during events." A libsa1 world is gameplay: 'C' lasts 0.8 s there,
1.6 s in a cutscene. **UNVERIFIED** in a harness run (asm + the wiki agree).

---------------------------------------------------------------------------------------------------------------------

## 3. (Q2) What a face is in the data

* **A separate model plus vertex shape keys.** The EV head is its own NJS_OBJECT tree (`Sonic_EV_Head` 0x8C1DFBD8 ... ,
  `config/symbols.1st_read.txt`); its shape motion (`sonic_face_s_head` 0x8C1FBBEC, type 0x30 VERT|NORM, inp_fn 2) has one
  NJS_MDATA2 per node in tree order, `p[0]` vertex keys (`{keyframe, NJS_POINT3 *}`), `p[1]` normal keys. The face code
  indexes the key ARRAY (`md->p[0][face].key`), not the keyframe (Sonic's 21st key is frame 23, index 20), and reads only
  `p[0]`: the EV head keeps its model normals in every expression (lighting of a face does not follow the morph; for the
  palette-lit parts this is the game's look).
* **No texture changes, no node motion, no eye swaps** in the face code. The eyes, mouth and lids are geometry of the EV head
  textured with the character's own archive. Textures the EV head uses that the static face does not (the "EV-only"
  textures): Sonic 25, 26, 27; Tails 19, 20; Knuckles 2, 3, 4; Amy 9, 10; Big 2, 27 (not on his static face, but a texture of them is drawn elsewhere on his body: measured render draws with them while the static face shows; 27 is in G-SA1's census);
  Tikal 16, 17. libsa1 draws them with flags 0x0030 (UNLIT | PALETTE_LIT; materials 0x9629A400 / 0x9721A400 /
  0x9621A400: IGNORE_LIGHT) plus a few palette-lit ones (measured, 5.2).
* **The swap is one pointer.** The creator stores `parent` = the head node whose `sibling` link holds the static face, and
  `sibling` = that static face (checked: in every image the parent's sibling is the static face). Linking `parent->sibling =
  object` puts the EV head into the drawn tree; the EV head's own sibling chain has the same node count as the static face's
  (2 / 3 / 2 / 5 / 7 / 5 nodes), so the rest of the tree is unchanged. Parents: Sonic `8C1CDABC` (static `8C1CD82C`), Tails
  `8C25F830` (`8C25F7FC`) and in flight `8C267564` (`8C267530`), Knuckles `8C35AB60` (`8C35A8D0`), Amy `8C4C75C8`
  (`8C4C72FC`), Big `8C5294EC` (`8C5291DC`), Tikal `0CB06D50` (`0CB06AC0`).
* **What pauses while the EV head shows:** Sonic's quill wobble (8C0B2376 morphs the static head model `D_8C1CD82C`) and
  Big's head wobble (8C0E59FE, `D_8C5291DC`) work on the static face only.
* **Look-at:** the head joints are ancestors of the swapped node (head turns keep working). The eye nodes the look task
  moves are outside the swapped subtree for Sonic, Knuckles and Amy (eyes keep following), but **inside it for Tails (both
  heads) and Big** (`8C25D164` / `8C25D2E0`, `8C265014` / `8C264E98`, `8C52739C` / `8C5274E8`): while their EV head shows,
  the look task moves eyes that are not drawn, so their eyes stay centred (matters for 0.9.6's head look-at; static result).
* **On the disc:** `1ST_READ.BIN` (loaded at boot, resident) for Sonic, Tails, Knuckles, Amy and Big - the EV head, the shape
  motion and the face table; Tikal's in `TIKAL_PROG.PRS` (`0x0CB0DA04`, motion `0x0CB23298`); textures in `SONIC.PRS`,
  `MILES.PRS`, `KNUCKLES.PRS`, `AMY.PRS`, `BIG.PRS`, `TIKAL.PRS` (`sa1_assets.csv`). Key data per head: Sonic about 215 KB,
  Tails 88 KB, Knuckles 211 KB, Amy 86 KB, Big 60 KB. libsa1 composes 1ST_READ into every world and loads the archives: the
  data is there in every world today.

---------------------------------------------------------------------------------------------------------------------

## 4. (Q3) Per character

### 4.1 Overview

| Character (kind) | EV head (nodes / points) | Keys (array) | Distinct keys | Valid letters | Gameplay faces in the game | Face in libsa1 now |
|---|---|---|---|---|---|---|
| Sonic (0) | `8C1DFBD8` (2 / 222 + 205) | 21 (frames 0-19, 23) | 20 (19 = 20) | all 31 | 'I' when hit; 'e' frozen (mode 43) | yes (measured: hurt face) |
| Tails (2) | `8C279198` (3 / 366 + 8 + 8), the same head for the flight model | 20 (node 0 only) | 20 | all 31 | 'I' when hit (3 paths) | yes |
| Knuckles (3) | `8C37273C` (2 / 239 + 201) | 20 | 20 | all 31 | 'I' when hit; 'e' (mode 48) | yes |
| Amy (5) | `8C4D0718` (5 / 330 + 20 + 49 + 14 + 14) | 20 (nodes 0, 3, 4) | 20 | all 31 | 'I' when hit | yes |
| Big (7) | `8C5367C4` (7 / 294 + 10 + 6 + 6 + 10 + 10 + 10) | 8 (nodes 0, 1, 4; 9 frames) | 7 (5 = 6); face mesh 6 (5 = 6 = 7) | 14: A B C D E K L S T U V W X Y | none | yes (no gameplay use) |
| Tikal (4) | `0CB0DA04` (5 / 428) | 20 | 20 | all 31 | - | cannot tick (`SA1_ERR_UNSUPPORTED`) |
| E-102 (6), Eggman (1) | none | - | - | none | - | no face task (measured `pfp` NULL); a script never advances |
| Super Sonic (8) and the General form while Super | none in the game | - | - | none | - | the face task runs on Sonic's normal tree, which is not drawn: no face (measured) |

Keys past the end (what an invalid letter reads; `out_static.txt`): Tails and Amy key 20 = NULL / garbage, Knuckles key 20
= his normal keys (wrong face); only Sonic has a key 20. Letters never exceed 19, so for the four 20-key characters every
letter is safe; custom key steps must stay below the key count.

### 4.2 The 31 face animations

Steps `{time, key}` from the table (measured, `D_8C19C32C`); "s" = seconds at gameplay pace (60 steps/s) / cutscene pace
(30 steps/s); wiki = the sadocs description; Big: ok / **wrong** (keys 8-15: his normal-key array read as vertices, no
crash) / **FAULT** (a key >= 16: a garbage key pointer; measured world fault for 'G').

| ID | Letter | Steps {time,key} | Keys | Steps | s | Wiki (SADX) | Big |
|---|---|---|---|---|---|---|---|
| 0 | A | (8,0) | 0 | 8 | 0.13 / 0.27 | no expression (also the idle) | ok |
| 1 | B | (2,1)(1,2)(1,3)(1,3)(1,2)(1,1)(1,0) | 0-3 | 8 | 0.13 / 0.27 | blink (also the idle, 25 %) | ok |
| 2 | C | (5,6)(5,5)(6,4)(6,0)(4,7)(6,5)(5,4)(6,6)(5,7) | 0,4-7 | 48 | 0.80 / 1.60 | talking, with D and E | ok |
| 3 | D | (4,4)(6,5)(6,7)(4,0)(4,4)(4,7)(4,5) | 0,4,5,7 | 32 | 0.53 / 1.07 | talking | ok |
| 4 | E | (4,7)(6,6)(6,5) | 5-7 | 16 | 0.27 / 0.53 | talking | ok |
| 5 | F | (3,6)(4,15)(4,4)(3,13)(4,14)(4,5)(6,14)(4,7) | 4-7,13-15 | 32 | 0.53 / 1.07 | talking ("Something buggin' you?") | wrong |
| 6 | G | (5,5)(6,6)(8,17)(5,0)(8,16)(5,5)(5,17)(6,19) | 0,5,6,16,17,19 | 48 | 0.80 / 1.60 | talking | FAULT |
| 7 | H | (2,1)(2,2)(2,3)(2,3)(2,1)(2,8)(24,8)(8,13) | 1-3,8,13 | 44 | 0.73 / 1.47 | blink, surprised, buggin' | wrong |
| 8 | I | (4,11)(8,9)(20,9) | 9,11 | 32 | 0.53 / 1.07 | hurt (gameplay damage) | wrong |
| 9 | J | (4,0)(32,9)(12,9) | 0,9 | 48 | 0.80 / 1.60 | slow shock | wrong |
| 10 | K | (6,1)(6,2)(6,3)(34,3)(4,2)(4,1)(4,0) | 0-3 | 64 | 1.07 / 2.13 | eyes closed a few seconds | ok |
| 11 | L | (2,1)(3,2)(3,3)(2,3)(2,2)(2,1)(2,0) | 0-3 | 16 | 0.27 / 0.53 | eyes closed, shorter | ok |
| 12 | M | (4,0)(48,8)(12,6) | 0,6,8 | 64 | 1.07 / 2.13 | slow surprise | wrong |
| 13 | N | (48,11)(16,11) | 11 | 64 | 1.07 / 2.13 | sad | wrong |
| 14 | O | (48,12)(16,12) | 12 | 64 | 1.07 / 2.13 | very sad | wrong |
| 15 | P | (4,4)(5,17)(7,17) | 4,17 | 16 | 0.27 / 0.53 | happy | FAULT |
| 16 | Q | 16 / 17 alternating x 9 | 16,17 | 32 | 0.53 / 1.07 | very happy (unused) | FAULT |
| 17 | R | (18,8)(10,8)(36,6) | 6,8 | 64 | 1.07 / 2.13 | slow surprise, slower | wrong |
| 18 | S | (6,6)(5,7)(5,7) | 6,7 | 16 | 0.27 / 0.53 | talking (rare) | ok |
| 19 | T | (4,0)(8,1)(8,2)(38,2)(6,1) | 0-2 | 64 | 1.07 / 2.13 | half-closed lids | ok |
| 20 | U | (8,0)(8,1)(8,2)(8,3) | 0-3 | 32 | 0.53 / 1.07 | half of a slow blink | ok |
| 21 | V | (16,3) | 3 | 16 | 0.27 / 0.53 | eyes closed | ok |
| 22 | W | (10,2)(12,1)(10,2) | 1,2 | 32 | 0.53 / 1.07 | sleepy lids | ok |
| 23 | X | (8,3)(8,2)(8,1)(8,0) | 0-3 | 32 | 0.53 / 1.07 | eyes opening | ok |
| 24 | Y | (10,1) | 1 | 10 | 0.17 / 0.33 | quarter-closed lids (unused) | ok |
| 25 | Z | (10,8) | 8 | 10 | 0.17 / 0.33 | surprised | wrong (measured: a held wrong face) |
| 26 | a | (10,11) | 11 | 10 | 0.17 / 0.33 | sad (Amy, EV0072) | wrong |
| 27 | b | (10,17) | 17 | 10 | 0.17 / 0.33 | happy | FAULT |
| 28 | c | (10,18) | 18 | 10 | 0.17 / 0.33 | side mouth (unused) | FAULT |
| 29 | d | (10,19) | 19 | 10 | 0.17 / 0.33 | side mouth (unused) | FAULT |
| 30 | e | (2,8)(60,8)(60,8)(60,8) | 8 | 182 | 3.03 / 6.07 | surprised (frozen in Ice Cap) | wrong |

Every sequence blends from the face it starts on (the previous target) - the wiki's "faces always interpolate toward the
next one". A one-step sequence (A, V, Y, Z, a-d) is a hold of one key; the game makes a still face by playing it again
(re-armed script or, in gameplay, PFaceChange every step as Sonic's mode 43 does).

Key meanings as rendered for `v1_machinima.md` 2.3 (labels ours; consistent for Tails, Knuckles, Amy, Tikal, Sonic's art
differs in detail): 0 neutral; 1-3 lids a third / two thirds / closed; 4-7 mouth shapes (4 small open, 5 narrow, 6 round
"o", 7 open "a"); 8 surprised; 9 yell; 10 grimace (unused by every sequence); 11 worried; 12 sad; 13 angry, mouth shut; 14
angry shout; 15 annoyed; 16 laughing; 17 happy, mouth open; 18 smile; 19 slight smile; Sonic 20 = 19. Where the change is
(rms distance from key 0 per node, `big_keys.py`): Sonic's blink keys 1-3 move only node 1 (the face; node 0, the head with
quills, is identical for keys 0-3); Amy's and Tikal's lid nodes (14 points) are identical for the mouth keys.

### 4.3 Big

* 8 vertex keys on nodes 0 (the face, 294 points), 1 and 4 (10 points each, texture 35: his lids, by what keys 1-3 move), 9 frames. Keys 5 and 6 are
  identical on every node; on the face mesh 5 = 6 = 7, on node 1 4 = 5 = 6 (`big_keys.py`). So his main face has 6 distinct
  shapes {0, 1, 2, 3, 4, 5 = 6 = 7} - this is the wiki's "Big has only 6 faces". 0 neutral, 1-3 lids, 4 and 5-7 mouth open
  (two openings).
* Measured in libsa1: 'C', 'D', 'E', 'S' play with his two mouth shapes (`out_probe.txt` "Big"); 'Z' (key 8) draws a held
  wrong face (index 8 reads the first normal key: no fault); 'F' (keys 13-15) a wrong face, no fault; **'G' (keys 16, 17,
  19): `access violation (read) at guest 1F93CB81 ... in func_8C055EFE` - the world faults** (`out_big_invalid.txt`;
  libsa1 catches it and the tick returns the game-code fault). On the DC a garbage key pointer is an address error (a crash,
  as the wiki says for SADX).
* "Two pairs of eyes" (wiki): the EV head brings its own eye whites (nodes 5 / 6, texture 19), pupils (2 / 3, texture 6) and
  lids (1 / 4, texture 35); the static face has eyes too (texture 19 / 6 / 0). The jaw is on his body. The game never shows
  Big's EV head in gameplay (no PFaceChange caller). Big's look-at eyes are inside the swapped subtree (3).

### 4.4 Super Sonic (kind 8 and the General form)

* `B_CHAOS7` has no EV head or shape motion (no symbol, no asset row; `sa1_assets.csv` lists body, ball, auras, splashes) and
  the SUPERSONIC archive has no EV-only textures. The Super body `0C909D2C` is a 73-node tree that mirrors Sonic's 73 nodes:
  node 18 `0C90907C` is the face parent, node 22 `0C908DEC` the static head (194 points, SUPERSONIC textures 5, 6, 13), and no
  node uses one of Sonic's models (`out_static.txt`).
* SonicMain creates the face task in kind 8 too (measured `pfp` set, parent `8C1CDABC`): it links Sonic's EV head into
  Sonic's NORMAL tree, which is not drawn while Super. Measured in kind 8 and in the General form (equipment 0x8000): the
  script advances (C1 ... C6), 0 EV draws, 0 static face draws. When the General form reverts to normal Sonic, the face
  shows again (the block keeps running).
* So "Super Sonic has no face in the game". Showing one would need a host deviation (link Sonic's EV head under the Super
  tree's node 18 and draw its SONIC textures, which have Sonic's blue skin and green eyes on a gold body) - not proposed
  (owner question 9.3).

### 4.5 E-102, Eggman, Tikal

E-102's main creates no face task (no `exec = func_8C07B106` outside the five creators and Tikal's; measured `pfp` NULL):
an EV_SetFace on him only stores the pointer; nothing draws. Eggman has no EV head (and is not playable). Tikal has a full
20-key EV head in TIKAL_PROG, but Tikal cannot tick in libsa1 yet.

---------------------------------------------------------------------------------------------------------------------

## 5. (Q4) What libsa1 runs and captures today (0.9.3a archive, measured)

### 5.1 The code runs

Every face function is lowered (`sa1g_fntab.c:2481-2485` 8C04BBCE / 8C04BC20 / 8C04BC70 / 8C04BCC8 / 8C04BD60, `:2569`
8C055EFE, `:2820-2824` 8C07B080 / 8C07B0F6 / 8C07B106 / 8C07B2B8 / 8C07B2DC, `:3276` 8C0B2338, `:3398-3399` 8C0BF76A /
8C0BF796, `:3467` 8C0C8E8E, `:3669` 8C0DDDB2, `:3741` 8C0E59C0); `gen/policy/stubs.tsv` (232 lines) has no face entry; every
character's face task exists from the first pairs (measured `pfp`: Sonic `8C8210B0`, Tails `8C8215F0`, Knuckles
`8C821660`, Amy `8C820A00`, Big `8C820C30`, kind 8 `8C821C50`; E-102 none). The event library itself (`8C099A50`, the event
coroutines) is skipped by the world step (`sa1_world_tick.c` step sequence), so nothing but the host will ever set an event
face.

**The game's hurt face already shows** (Sonic, Tails, Knuckles, Amy; one token per pair: E = EV head drawn, s = static face,
- = body not drawn by the damage blink):

```
hurt: [s gI0] [- gI0] [E 0>11@0.75] [-] [E 9>11@0.63] [-] [E 9>11@0.13] [-] [E 9] [-] [E 9] [-] [E 9] [-] [E 9] [-] [E 9] [-] [s] ...
```

The EV head is linked for the 32 steps of 'I' (pairs 1-16), drawn on the pairs the blink shows the body. Big: no face.

### 5.2 Capture, render, interpolation

* **Capture:** every EV-head model draw of every pair had a v3 point snapshot (`rt_draw.c` model snapshot, RT_CAP_MSNAP; 63
  of 63 EV pairs in "CDE" for each character), and the snapshot is exactly `lerp(key a, key b, t)` of the face block's keys
  (fit residual 0.0000) - the morph reaches the host as the game drew it.
* **Render:** `sa1_character_render` emits the EV head with the character's archive-0 textures, among them the EV-only ones
  (measured draws: Sonic 25 / 26 / 27 material 0x9629A400 flags 0x0030, 27 also 0x9429A400 flags 0x0020; Tails 19 / 20
  0x9721A400 flags 0x0030, 20 also 0x9429A400 0x0020; Knuckles 2 / 3 / 4 0x9621A400 0x0030, 2 / 4 also 0x94..A400 0x0020;
  Amy 9 / 10 0x9621A400 0x0030; Big 2 / 27 0x9429A400 0x0020).
* **Interpolation:** "points the game changed between the pairs (welds, morphs, shape motion) are lerped in the model's
  space" (`libsa1.h` of 0.9.3a, line 648; line 652 in LIB's header today): a face blend is smooth at any frame rate. The static <-> EV switch is a model
  change (unmatched between two pairs): it switches at a pair boundary, which is how the game shows it.
* **Lag:** the draw precedes the face task in a step (2.4): every face change is drawn one pair after the call is applied,
  and G-SA1 draws one pair behind for interpolation. Script to screen: about 2 pairs (67 ms) plus the host frame.

### 5.3 Gaps

| Gap | Evidence | Needed |
|---|---|---|
| No API: nothing can set a script, the mode, a level | `libsa1.h` has no face call (`v1_roadmap.md` "Not in LIB yet ... FACE") | section 6 |
| A hidden pair (the game's visibility test said "not visible") is drawn with the CPU pose, which evaluates the charset's static object tree (`sa1_pose.c:315` `sa1_pose_evaluate` from `cs->objects`; called from `sa1_character.c:1109` `pose_eval_current`, `:1324`): the static face, never the EV head | code reading; **UNVERIFIED** by a run (hidden pairs are rare in G-SA1) | optional: let the CPU pose swap in the EV head node and the face block's lerp (the math of 8C055EFE), or accept the pop on hidden pairs |
| Pre-warm: G-SA1's material census (`g-sa1/build/x86_64-windows/material_census.txt`, 2026-10-26 18:22) has none of the EV-only states (Sonic 25-27, Tails 19-20, Knuckles 2-4, Amy 9-10 with flags 0x30; Big 27 0x20 is there) | awk over the census | the census run must show every EV head (a scripted face pass in core_test) - G-SA1 side |
| Determinism: the idle draws `rand()` every 8 steps | `code_8C04BC70.c:77` | none (option-gated; recorder journals face calls later) |
| Gameplay hurt face suppressed in event mode, replayed when leaving it | measured (2.4) | leaving event mode calls the game's PFaceCancel `8C07B2DC` (6.7) |

### 5.4 Measured timelines (Sonic; the other characters identical, Big with his own keys)

| Test | Result |
|---|---|
| EV_SetFace("CDE") from the regular face | pair 0 static (draw before the task), from pair 1 EV head; C pairs 0-23 (48 steps), D 24-39 (32), E 40-47 (16), then the idle 'A' / 'B' for ever |
| EV_ClrFace | the EV head is drawn one more pair, then the static face |
| "E0" (the script's own stop) | E for 8 pairs, then the static face |
| EV_SetFace("V") two pairs into 'C' | 'V' starts 21 pairs later, when 'C' ends (the game's queue) |
| interrupt: `count = time - 1`, `seq = NULL`, script "Z" | 'Z' starts at the next step; the running step snaps to its target first (5>6 at 0.6 -> 5, then 5>8) |
| hold 'Z' (the host re-arms the one-letter script when the reader took it) | key 8 held 20 pairs, re-armed 4 times (every 10 steps), no gap; 'C' loops with period 24 pairs |
| idle after an empty script, 1,800 pairs | 450 sequences: 'A' 346, 'B' 104 (23 %): one blink per 0.58 s at gameplay pace (Big: 110 B, 0.55 s) |
| 'C' with doubled step times (a host copy) | 48 pairs = 1.6 s (the cutscene duration) |
| PFaceChange('P') / PFaceCancel | P plays 16 steps then the static face; a cancel mid-'I' -> static the next pair |
| hit while an event face runs | 'V' continues (event priority), the body blinks; after EV_ClrFace the stale 'I' plays fully |
| E-102 EV_SetFace | nothing advances, nothing drawn |
| kind 8 / General while Super | the script advances, 0 EV draws |
| Big 'G' | world fault in 8C055EFE |

---------------------------------------------------------------------------------------------------------------------

## 6. (Q5) The minimal faithful mechanism

### 6.1 Principle

In the game an event script is the only caller of EV_SetFace / EV_ClrFace. libsa1 worlds run no events, so the **host
takes the place of the event script**: it asks for faces through a libsa1 call; libsa1 latches the request and applies it at
the start of the next pair (sub step 0, before the executor; a `face_pre` hook next to `hostobj_pre` in `sa1g_step_game`,
`sa1_world_tick.c:190-195`, or a block in `sa1_world_charopt.c`), calling the game's own functions with the strings in the
world's extension segment. The face task, the reader, the blend and the draw stay the game's. Host-only writes are limited to
four documented cases (interrupt, lip sync, the pace table, the loop re-arm through 8C04BBCE itself). A world that never gets
a face call is byte-for-byte unchanged (the reference traces never call it).

### 6.2 The switch: event faces or regular faces

* **Event (on):** `8C04BBCE(player, "")` - the EV head with the game's idle (neutral, random blinks), the "loaded event
  head" of the wiki. libsa1 keeps it on: at each pair start, if `face_time == 0` (a '0' ended it, or it never started), it
  calls `8C04BBCE("")` again. Expressions and lip sync run on top; the idle resumes after them.
* **Regular (off, default):** `8C04BD60(player)` (EV_ClrFace) and `8C07B2DC(0)` (PFaceCancel: drops a hurt face that waited
  behind the event face). The game's gameplay faces (hurt, frozen) work as in the game. An expression asked for in this mode
  gets the game's trailing '0' (back to the regular face at its end); lip sync shows the EV head while the level is >= 0 and
  ends with EV_ClrFace (6.6).
* Characters without a face (E-102, Eggman, kind 8, a General Sonic while Super): the call succeeds or returns
  `SA1_ERR_UNSUPPORTED` (6.9); nothing is drawn.

### 6.3 Playing a face animation or a sequence string

* **Queue (default, the game's):** `8C04BBCE(player, script)` with the script copied into a per-world buffer (two buffers,
  alternating, so a pointer the reader still holds is never overwritten). The script starts when the running sequence ends
  (0.13 s during the idle, up to 3 s during 'e').
* **Now (host interrupt, review G1 of `v1_machinima.md`):** also set `face_seq = NULL` and `face_count = face_time - 1` (when
  `face_time > 1`): the running step ends at the next step on its target key, then the reader takes the script's first
  letter. Measured: the new expression starts at the next step; the face snaps from the running blend to that step's target
  (at most one step's blend: the measured snap was 6>5 at rate 0.6 -> 5). For hotkeys and the wheel, Now is the useful
  default; scripts typed by the owner may use Queue.
* **Stop:** in event mode the idle; in regular mode `8C04BD60` (the game's EV_ClrFace) - or a trailing '0'.
* Letters: 1-64 of 'A'-'Z', 'a'-'e', and '0' (stop). Anything else is refused (the table has 31 entries: a letter 'f'-'z' or a
  digit indexes garbage, 2.2).

### 6.4 Holding an expression

The game holds a face by repeating one animation (wiki; Sonic's frozen mode calls PFaceChange every step). libsa1's hold
re-arms: at each pair start, while the hold is on and the reader has taken the last letter (`face_script == NULL` or
`*face_script == 0`), call `8C04BBCE(player, same buffer)` again (with `face_time != 0` it stores only the pointer). Measured:
no gap, key 8 constant for 'Z'; a talk letter loops ('C' every 24 pairs). One-key letters (A, V, Y, Z, a, b, c, d, also N, O
which end on their key) give a still face; others loop the animation.

### 6.5 Pace (gameplay or cutscene)

Option per character, default **gameplay** (the game's speed in a world) - owner question 9.2. Cutscene pace = while event
mode is on, the world's own `D_8C19C32C[0..30]` point at a copy of the 31 sequences with every time doubled (all times are
<= 60, so <= 120 fits the Sint8; the copy lives in the extension segment, written once per world; restored when the option
or event mode goes off). Scripts, the idle and lip sync's ladder are unaffected in mechanism; everything plays at half speed
(measured with a doubled 'C': 48 pairs = 1.6 s). Data of that world only; reference worlds never set it.

### 6.6 Lip sync from a host loudness value

SA1 has no lip sync (each event line has a hand-written script of C / D / E chunks, `v1_machinima.md` 2.4). Two ways, both
measured in libsa1, both drawn by the game's mode-4 blend (`8C07B106` + `8C055EFE`):

**L2, the mouth ladder (recommended for "Garry's Mod style": the mouth opens with the voice).** Each pair, libsa1 maps the
latched level `s` (0..1) to a position on the character's mouth ladder and writes the face block directly: `face = ladder[i],
face_next = ladder[i + 1], face_time = 64, face_count = round(r x 64) - 1` (`p = s x (n - 1)`, `i = floor(p)`, `r = p - i`),
`face_script = NULL`, `face_seq` = a per-world held step `{127, face_next}` so a count that reaches the time ends on the upper
key. The face task adds one step and draws `rate = count / time`. Measured (Sonic, 15 pairs): the drawn mouth at pair n+1 is
exactly the position written at pair n plus 1/64 (e.g. written 0>5 at 0.406 -> drawn 0>5 at 0.422; residual 0). Latency:
exactly one pair after the latch (plus G-SA1's one-pair render delay). Continuous, no chunks; the eyes stay open while
talking (the mouth keys have open lids), as in the game's own talk sequences, which never blink.

* Ladder (by openness, rms distance from key 0 on the mouth node and the research renders): Sonic, Tails, Knuckles, Amy,
  Tikal `0 (closed) - 5 (narrow) - 4 (small open) - 6 (round "o") - 7 (open "a")`; Big `0 - 4 - 5` (his 5 = 6 = 7). The
  order 5 / 4 is a choice (**UNVERIFIED** visually per character; libsa1 exposes the ladder in `sa1_face_info`, so G-SA1 can
  change it without a libsa1 change).

**L1, game steps.** Each pair, a one-step host sequence `{2, key(level)}` (a double buffer) becomes `face_seq`; the reader
takes it when the running step ends. Measured: after the idle step ended (up to 8 steps on the first call), the key follows
every pair; the drawn face is the midpoint between the previous and the new key one pair later. Discrete; no direct write
of `face`/`face_count`. Kept as a fallback.

**Game talk (style 1 of `faces_wheel_ui.md` 5.4):** while talking, loop the script "CDE" (6.4) with Now on the first
pair, and stop (idle or regular face) after the hang time. The cutscene look; does not follow syllables.

Rules:

* `sa1_character_face_level(level)` with level >= 0 turns lip sync on; < 0 off. In regular mode it links the EV head while
  on and ends with EV_ClrFace; in event mode it returns to the idle (the next pair calls `8C04BBCE("")`).
* A level call overrides a queued or held expression while it is >= 0; an expression asked for with Now pauses lip sync for
  its length (the G-SA1 priority of `faces_wheel_ui.md` 4.3 is enforced in G-SA1: it simply stops sending levels).
* Only the last level before a pair counts (the host may call every frame).

### 6.7 Gameplay faces and event mode

The game's priority stays: while the event face runs, a hit's 'I' and the frozen 'e' wait (`faces_wheel_ui.md` 4.1 already
says so to the owner). When event mode goes off, libsa1 also calls `8C07B2DC(0)` (the game's PFaceCancel) so a stale hurt
face does not play after the switch (measured otherwise: the full 'I' plays). In regular mode nothing changes.

### 6.8 Validation (never a fault)

| Input | Result |
|---|---|
| a script letter outside 'A'-'Z', 'a'-'e', '0' | `SA1_ERR_INVALID_ARGUMENT` |
| a letter the character's keys cannot play (Big: F G H I J M N O P Q R Z a b c d e) | `SA1_ERR_UNSUPPORTED` (the whole call; nothing applied) |
| a ladder key or custom step key >= the key count | refused at `sa1_face_info` level (libsa1 builds ladders from its own table) |
| kind 1 (Eggman), 4 (Tikal: cannot tick), 6 (E-102), 8 | `SA1_ERR_UNSUPPORTED` |
| kind 0 with the General form | accepted (the block runs; drawn only while not Super); `sa1_state_ex.face_shown` says whether the EV head is in the drawn tree |

The letter mask per kind comes from the character's key count and the table (a 31-bit mask; computed from the world's own
data at load, no hard-coded list): 0x7FFFFFFF for 20 / 21 keys, Big 0x01FC0C1F (A-E, K, L, S-Y).

### 6.9 API sketch (`LIB/include/libsa1.h`, own section, feature macro)

```c
/* ---- faces (round 0.9.4a; SA1_API_FACE; sadc docs/research/faces_events.md) ---------------------------------------
 * The game's event faces: the EV head (a second head whose vertices the game blends between shape keys) driven by the
 * game's own face block (EV_SetFace 8C04BBCE, the reader 8C04BCC8, EV_ClrFace 8C04BD60, the face task 8C07B106). Calls are
 * latched and applied at the start of the character's next pair (sub step 0, before the executor). No call = the game
 * unchanged (the reference worlds). */
#define SA1_API_FACE 1

#define SA1_FACE_MODE_GAME    0u  /* default: the regular face; the game's own gameplay faces (hurt 'I', frozen 'e') */
#define SA1_FACE_MODE_EVENT   1u  /* the EV head always, with the game's idle (neutral 'A' 75 %, blink 'B' 25 %); leaving
                                     it also cancels a waiting gameplay face (8C07B2DC) */
#define SA1_FACE_PACE_CUTSCENE 0x100u /* mode flag: while EVENT, the world's face table points at copies with every step
                                     time doubled (cutscenes run one logic step per frame) */
SA1_API sa1_status sa1_character_face_mode(sa1_context *ctx, sa1_character_id id, uint32_t mode);

#define SA1_FACE_NOW   0x1u       /* start at the next step (the running step ends on its target), not after the
                                     running animation (the game's queue) */
#define SA1_FACE_LOOP  0x2u       /* repeat the script until the next sa1_character_face / stop (a one-letter hold) */
/* script: 1..64 of 'A'-'Z', 'a'-'e' (the game's 31 face animations, faceTbl D_8C19C32C) and '0' (stop); NULL / "" =
   stop (EVENT: the idle; GAME: EV_ClrFace). In GAME mode a script without '0' gets one at its end. Errors: 6.8. */
SA1_API sa1_status sa1_character_face(sa1_context *ctx, sa1_character_id id, const char *script, uint32_t flags);

/* lip sync: level 0..1 (the host's loudness), < 0 = off; the last value before a pair counts. Style: */
#define SA1_FACE_LEVEL_LADDER 0u  /* the mouth on the character's ladder (sa1_face_info.ladder), drawn one pair later */
#define SA1_FACE_LEVEL_TALK   1u  /* the game's talk animations C, D, E looped while level >= 0 */
SA1_API sa1_status sa1_character_face_level(sa1_context *ctx, sa1_character_id id, float level, uint32_t style);

typedef struct sa1_face_info {
    uint32_t struct_size;
    uint32_t keys;            /* vertex keys of the EV head: 21 Sonic, 20 Tails / Knuckles / Amy / Tikal, 8 Big, 0 none */
    uint32_t letters;         /* bit n: face animation n (0 'A' .. 30 'e') uses only keys the character has */
    uint8_t  ladder[8];       /* the mouth ladder, closed to open; ladder_len entries */
    uint32_t ladder_len;      /* 5; Big 3 */
    uint16_t steps[31];       /* length of each animation in steps (the same for every character) */
} sa1_face_info;
/* works without a world (from the loaded image); kind 8: keys 0 (no EV head in the game) */
SA1_API sa1_status sa1_face_info_get(const sa1_context *ctx, int32_t kind, sa1_face_info *out);

/* sa1_state_ex, at its end (struct_size guarded): */
    uint32_t face_mode;       /* SA1_FACE_MODE_* | flags as set */
    uint32_t face_shown;      /* 1: the EV head is linked under the face parent of the drawn tree */
    int32_t  face_from, face_to;  /* keys of the face block (event) or of the face work (gameplay); -1 none */
    float    face_rate;       /* 0..1 */
    int32_t  face_letter;     /* the running animation 0..30, -1 none, -2 lip sync / host step */
```

Internal (LIB): a per-world `sa1g_face` block (mode, flags, pending script + flags, level, style, loop state, the two script
buffers, the held step, the L1 double buffer and the pace table copy in the extension segment, about 1 KB; allocated once),
the `face_pre` hook, `sa1_face_info` computed from `D_8C19C32C` and the face work's motion at load. File ownership is the
planner's (expected: one LIB agent owning a new `src/world/sa1_world_face.c`, a small hook patch in `sa1_world_tick.c` /
`sa1_world.h`, the `libsa1.h` section through the round's header editor, `tests/face_test.c`).

### 6.10 For the G-SA1 side (`faces_wheel_ui.md`)

The UI document's assumptions hold, with these facts from this research: Big's 14 letters are final (and his "6 faces" are
the 6 distinct face-mesh shapes); Super Sonic (both) has no face in the game (grey the controls; status "Super Sonic has no
event head in the game"); E-102 none; a General Sonic's face works while he is not Super. Its style-0 ladder order should
come from `sa1_face_info.ladder` (this research: 0-5-4-6-7, Big 0-4-5). The module needs no vertex work: the EV head arrives
as ordinary draws. Pre-warm: add a face pass to core_test's census (5.3).

---------------------------------------------------------------------------------------------------------------------

## 7. Tests (with the expected numbers measured here)

LIB `tests/face_test.c` (flat GRID floor, worlds per kind; each case re-reads the face block / face work through the
public `sa1_state_ex` fields and the render draws):

| # | Case | Expected |
|---|---|---|
| F1 | no face call, 200 pairs idle + a hit (Sonic, Tails, Knuckles, Amy, Big) | the game: hurt EV head pairs 1-16 for the four, none for Big; and the 10 reference traces + all strict gates unchanged (no face call in them) |
| F2 | EVENT mode on (Sonic) | `face_shown` from the 2nd pair; 1,800 pairs: 450 idle sequences, B 20-30 % (rand-dependent, exact per seed) |
| F3 | `sa1_character_face("CDE", 0)` in EVENT mode | C for 24 pairs, D 16, E 8, then the idle; every EV draw carries the EV-only textures (Sonic 25-27, Tails 19-20, Knuckles 2-4, Amy 9-10) |
| F4 | Queue vs Now: "V" two pairs into 'C' | Queue: V after 21 pairs; Now: V from the next step |
| F5 | LOOP "Z" 20 pairs | key 8 held (face_from = face_to = 8 after the first 10 steps) |
| F6 | GAME mode "E" | EV head for 8 pairs, then the static face (the appended '0') |
| F7 | lip sync L2, a 15-value envelope | the drawn position at pair n+1 = written position at pair n + 1/64 (fit residual < 1e-4) |
| F8 | level < 0 after talking, GAME mode | static face from the 2nd pair |
| F9 | Big: "G", "Z", "F", "q", "1" | `SA1_ERR_UNSUPPORTED` / `SA1_ERR_UNSUPPORTED` / `SA1_ERR_UNSUPPORTED` / `SA1_ERR_INVALID_ARGUMENT` x2; the world keeps running |
| F10 | E-102, kind 8 | `SA1_ERR_UNSUPPORTED`; General form: accepted, `face_shown` 0 while Super, 1 after the revert |
| F11 | PACE_CUTSCENE + "C" | 48 pairs |
| F12 | EVENT on, hit, EVENT off | no 'I' after the switch (PFaceCancel) |
| F13 | `sa1_face_info_get` | keys 21/20/20/20/8/0, letters 0x7FFFFFFF and Big 0x01FC0C1F, steps[2] = 48, ladders 0-5-4-6-7 / 0-4-5 |
| F14 | x86 / x64, two worlds on two threads | identical face timelines |

G-SA1: the census contains the EV-only states (flags 0x30 rows for the four characters); binding tests for the calls and the
refusals; the owner checklist: switch on/off, a hit in each mode, Big's greyed letters, Super Sonic's status line, talking
with the microphone.

---------------------------------------------------------------------------------------------------------------------

## 8. Risks and UNVERIFIED points

| Point | Status / mitigation |
|---|---|
| Cutscene pace = half speed (events one step per frame) | asm + wiki, not run in the harness; it is only an option |
| Mouth ladder order (5 before 4) | rms distances and the research renders; check visually per character (G-SA1 can override the ladder) |
| L2 writes the face block directly | the drawing and blending stay the game's; the reader is parked on a held step; measured exact; only in worlds that get level calls |
| Interrupt snaps up to one step's blend | measured small (one step of a 3-8-step blend); Queue is the game's behaviour if preferred |
| Hidden pairs draw the static face (CPU pose) | code reading; rare; optional CPU-pose support later |
| Tails / Big eyes do not follow look-at while the EV head shows | static result (eye nodes inside the swapped subtree); matters for 0.9.6 |
| Sonic's quill / Big's head wobble pause in event mode | the game's own behaviour in events |
| First use of the EV-only materials may create them in game | census gap; small (`v1_machinima.md` R.2-6) |
| The idle uses `rand()` | a world with faces diverges from one without in later random choices; option-gated; the 2.0 recorder must journal face calls |
| Multiplayer: faces are not networked | later (MP round: face state in the pose stream; remote lip sync from `ply:VoiceVolume()` locally) |
| Super Sonic face | none in the game; a host deviation would be needed (owner question) |
| Concurrency of this research | `faces094a/` is shared with res-emote, which replaced `faces094a/lib/` with the 0.9.4 snapshot at 18:26; this research's probes use their own copy in `res_faces/lib/` (appendix A) |

---------------------------------------------------------------------------------------------------------------------

## 9. Owner questions (each with the default this research suggests)

1. **Event faces switch default:** off (the regular face), the switch turns them on for every character that has one.
2. **Pace:** the game's gameplay speed (default) or the slower cutscene speed for expressions and blinking while event faces
   are on? (Cutscene: blinks every 1.2 s on average instead of 0.6 s; 'C' 1.6 s instead of 0.8 s.)
3. **Super Sonic:** no face (default; the game has none), or a host-made face (Sonic's event head on the golden body; blue
   skin and green eyes would show)?
4. **Lip sync style:** the mouth follows the voice (default, the Garry's Mod way) or the game's talk animations.

---------------------------------------------------------------------------------------------------------------------

## Appendix A: files of this research (`SADC/build/research/faces094a/res_faces/`)

| File | What |
|---|---|
| `lib/libsa1.a`, `lib/include/*.h`, `lib/README.txt` | the COPY used: 0.9.3a snapshot, copied 2026-10-26 18:31 from `SADC/build/research/super2/anim/lib` (= `g-sa1/build/deps` of 2026-10-26 14:21); `libsa1.a` sha1 `e1cd5fdec684466542de02d954daa60c2e5f28b1`, `libsa1.h` sha1 `9a599dae4e99da8613874e2716b7738d8abf7928` |
| `static_faces.py` -> `out_static.txt` | EV heads, static faces, keys, past-end pointers, valid letters, EV-only textures, Super Sonic's tree, look-at eye nodes, identical keys |
| `big_keys.py` | per-node identical keys and rms distances from key 0 |
| `probe_faces.c`, `build_probe.py` -> `out_probe.txt` | the libsa1 measurements of sections 4-6 (`py -3.12 build_probe.py probe_faces.c --run`, SA1_TEST_GDI set; built and run at IDLE priority) |
| `out_big_invalid.txt` | `probe_faces.exe --big-invalid <GDI>`: Big 'F' (wrong face) and 'G' (world fault) |

Probe internals used (no struct layout): `sa1_character_world`, `sa1g_world_ptr`, `sa1g_world_ext_alloc`, `sa1g_pcd_size`;
the capture header is found by its magic in the pcdata segment (0x98000000), as `sa1_world_hud.c` does.
