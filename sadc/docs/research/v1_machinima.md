# v1_machinima: first look at G-SA1 2.0, the machinima creator

Status: research and design, 2026-10-21 (read-only round; nothing in SADC/src, LIB or g-sa1 was changed). Topic of the
owner's 2.0 roadmap: "Add a feature that will give the characters their EVENT faces, with toggleable expressions and
optional hotkeys that will switch to bound expressions. Add a replay feature for recordable actions, cutscene animation
selections, audio player (from the game and from MP3/WAV/OGG/ADX), subtitles, keyframes. All of which can be saved to
it's own file and loaded in for taking breaks or sending to friends."

Every number below was measured on the owner's disc (US 1.005, the extracted files in `sadc/disc` and the overlay images
in `sadc/build/*.BIN`) or read from SADC's decompiled C, unless it is marked UNVERIFIED. Scratch tools and their outputs:
`sadc/build/research/machinima/` (appendix A). Related designs: `v1_arch.md` (game objects inside worlds),
`v1_triggers.md` (cameras, splines), `v1_npcs.md` (the game's message window and font), `libsa1_integration.md`
(worlds, determinism), `libsa1_interaction.md` (host objects), `jukebox.md` (the module's audio engine), `sa1_sound.md`.

---------------------------------------------------------------------------------------------------------------------

## 0. Summary

**EVENT faces are already in every libsa1 world; only an API and a page are missing.** The game gives Sonic, Tails,
Knuckles, Amy, Big and Tikal a second head, the "EV head", whose vertices are blended between the keys of a shape motion
(Sonic 21 keys, Tails / Knuckles / Amy / Tikal 20, Big 8). All of it is plain data in `1ST_READ.BIN` (Tikal's in
`TIKAL_PROG.PRS`) and uses the character's own texture archive, so libsa1 loads nothing new. The face task that shows it
is created by every character's main at init and runs in G-SA1 0.7 today: it swaps the head node's sibling to the EV head
and blends two keys (`8C07B080`, `8C07B106`). Expressions are the game's 31 face sequences `D_8C19C32C` ('A'-'Z', 'a'-'e':
{frames, key} steps) and event "face scripts" (strings of those letters; 623 of them in 127 event overlays). All this
code is decompiled (face code 7 of 7 functions, event work 16 + 2 NON_MATCHING of 18, player event control and look-at
setters 15 of 15). There is **no lip sync** in SA1: events play talk sequences 'C' / 'D' / 'E' (48 / 32 / 16 steps) for
each line, written by hand. G-SA1 can do the same for any audio, timed from the clip's loudness.

**Cutscene animations are data the game already knows how to play on a live character.** 155 of the 207 event
overlays (`EVxxxx.PRS`, file name format `?ev%04X.prs` at `8C112AEB`, loaded at `0x0CB80000`) hold 923 `NJS_ACTION`
bindings of a character's own model tree to an event motion: **404 distinct motions, 4.6 MB of keys** (Amy 89, Sonic 80,
Tails 61 + 4 flight, Knuckles 41, E-102 37, Big 36, Tikal 36, Eggman 20; median 30 frames, longest 400). Events play them
through the character's event work (`eventwk`): an action queue with blend-in (`8C04B4D2`, `8C04B528`, player
`8C04B57C`), a path queue (`8C04B7BA`, `8C04B9E0`), "walk to / run along / hold" locks (`8C07C1B4`-`8C07C270`), look-at
(`8C07C2E2`-`8C07C376`) and the face block, all decompiled. libsa1 only has to copy a motion out of the overlay into the
world's extension segment and call these functions. The cutscene scripts themselves (8,861 functions, 2.0 MB of code)
are not needed and are not decompiled.

**The recorder is a journal of everything that crosses the libsa1 API into a world, written inside libsa1 at the point
where the world latches it.** A world's evolution depends only on its creation descriptor, its static collision, and the
per-pair stream of pad input, camera yaw, moving collision objects, host objects, damage / ring / option calls and the
new face and event commands. Recording those (in SA1 space, as the world consumed them) and replaying them through the
same live path reproduces the world bit for bit, as the ten reference traces already do. The map's collision is
recorded as a fingerprint (not as geometry); a different map version, libsa1 logic or settings is detected and the actor
falls back to a recorded "ghost" (position, action, frame, face per pair). Sizes from the structures (gate J4 measures
them): about 16 bytes per pair of input (about 30 KB per minute before compression) and about 40 bytes per pair of ghost.

**One project file, references only.** A versioned chunked file in `garrysmod/data/gsa1/machinima/` holds the journals,
ghosts, keyframes, audio clip placements, subtitles and the owner's own text. Game content (voices, music, SE, event
motions, the game's subtitle lines) is stored as references (AFS index, music number, overlay + address, ...) that a
friend's G-SA1 resolves from their own disc; the writer refuses any disc bytes. The owner's own MP3 / WAV / OGG / ADX
files are referenced by name and hash, optionally packed in.

**Phases** (section 10): M1 faces and hotkeys (first, small, visible in normal play) - M0 the journal and a minimal
record / play - M2 event motions and the event-command API - M3 the Director (timeline window, takes, camera keys,
project file v1) - M4 audio tracks - M5 subtitles - M6 character choreography keyframes and puppet actors (Eggman, Tikal)
- M7 seek snapshots, prop restore, offline-render mode, sharing polish. About 8-9 rounds. Required decompilation is
small: 180 bytes of event-library wrappers (optional; the C they call exists), Eggman 4,468 bytes (11
functions) for an Eggman actor; the event library's remaining 7.7 KB and the 2.0 MB of cutscene scripts are not needed.

---------------------------------------------------------------------------------------------------------------------

## 1. Sources and method

| What | How it was measured (`sadc/build/research/machinima/`) |
|---|---|
| EV heads, face motions, key counts | `face_motions.py`: walks each EV head tree and its `NJS_MOTION` (`mdata` = one `NJS_MDATA2` per node) in `disc/1ST_READ.BIN` |
| Face sequence table | `face_table.py`: dumps `D_8C19C32C` (pointer table, `{Sint8 time, Sint8 key}` steps ending at time 0) |
| What each key looks like | `face_sheet.py` + `faces/*.png`: renders every key of every EV head as the face task shows it (head node's sibling switched to the EV head, its points set to key k), using SADC's ripper `tools/research/rip_character.py`. The PNGs are scratch renders of the owner's disc for this research only; nothing is shipped |
| Face scripts in events | `face_scripts.py`: strings of the EV overlays' xrefs that consist only of the script letters |
| Event motions | `ev_motions.py` (`ev_motions.csv`), `ev_motions2.py`, `ev_motions3.py`: scans every EV overlay image for 8-byte `NJS_ACTION` pairs {character root object, valid `NJS_MOTION` inside the overlay}; distinct motions by SHA-1 of their keys; code-pool references cross-checked |
| Event overlay sizes, imports | `ev_stats.py` (`ev_stats.txt`): functions and code bytes from `build/EVxxxx.xrefs.json`, image and PRS sizes, which 1ST_READ functions the overlays import |
| Decompilation status | `range_status.py` (same rules as `tools/progress.py`, which only reports `8C...` regions) |
| Subtitles, event sound banks | inline scripts recorded in appendix A (5-language pointer tables; `E_xxxx.MLT` headers) |
| libsa1 and G-SA1 facts | `libsa1/include/libsa1.h`, `libsa1/src/world/*`, `g-sa1/README.md`, `g-sa1/build/x86_64-windows/material_census.txt`, `docs/libsa1_integration.md` 6.4-6.8 and 8, `docs/libsa1_interaction.md` 2.8 |

---------------------------------------------------------------------------------------------------------------------

## 2. EVENT faces

### 2.1 The data on the disc

| Character | EV head object | Nodes / verts / tris | Face shape motion | Keys (array index: keyframe) | Key data | Head node whose sibling is switched (static face) | EV head textures (own archive) |
|---|---|---|---|---|---|---|---|
| Sonic | `0x8C1DFBD8` (`Sonic_EV_Head`) | 2 / 427 / 674 | `0x8C1FBBEC` `sonic_face_s_head`, 24 frames, VERT+NORM | 21 (frames 0-19 and 23) on both nodes, vertex and normal keys | ~215 KB | `0x8C1CDABC` -> `0x8C1CD82C` | SONIC 0, 5, 7, 8, 11, 25, 26, 27 |
| Tails | `0x8C279198` (`Tails_EV_Head`) | 3 / 382 / 597 | `0x8C28E9BC`, 20 frames | 20 on node 0 (vertex keys only); nodes 1-2 (8 points each) static | ~88 KB | `0x8C25F830` -> `0x8C25F7FC`; flight model `0x8C267564` -> `0x8C267530` (chosen per step by `8C0BF796`) | MILES 3, 4, 16, 17, 19, 20 |
| Knuckles | `0x8C37273C` | 2 / 440 / 712 | `0x8C38C870`, 20 frames | 20, vertex and normal | ~211 KB | `0x8C35AB60` -> `0x8C35A8D0` | KNUCKLES 1, 2, 3, 4, 5, 8, 17, 19 |
| Amy | `0x8C4D0718` | 5 / 427 / 687 | `0x8C4E591C`, 20 frames | 20 on nodes 0, 3, 4 (vertex only) | ~86 KB | `0x8C4C75C8` -> `0x8C4C72FC` | AMY 8, 9, 10, 12, 16, 25, 34 |
| Big | `0x8C5367C4` | 7 / 346 / 492 | `0x8C53C310`, 9 frames | **8** on nodes 0, 1, 4 | ~60 KB | `0x8C5294EC` -> `0x8C5291DC` | BIG 1, 2, 6, 18, 19, 21, 27, 29, 30, 34, 35 |
| Tikal | `0x0CB0DA04` (TIKAL_PROG) | 5 / 428 / 691 | `0x0CB23298`, 20 frames | 20 | | `0x0CB06D50` -> `0x0CB06AC0` (`func_0CB00ED4`) | TIKAL 2, 6, 7, 8, 10, 11, 16, 17 |
| E-102, Eggman, Super Sonic | none found (E-102 is a robot; no Eggman EV head in `1ST_READ` per `sa1_assets.csv`; Super Sonic's tree is in `B_CHAOS7` and the face task only switches the normal Sonic tree: **UNVERIFIED** whether B_CHAOS7 has its own) | | | | | | |

Facts that matter for a port:

* The EV head replaces only the **static face part**, not the head: for Sonic the switched node `0x8C1CD82C` (2 nodes:
  the face and `sonic_z`, 205 points) becomes the EV head (2 nodes: a 222-point face with eyelids and mouth, and a
  205-point copy of `sonic_z`). The head joint `0x8C1CDDE8` (the look task's head object) is an ancestor of the switched
  node, and the eyeballs `0x8C1CDA20` / `0x8C1CDCB0` are separate nodes outside it, so head turns and eye movement
  still apply with the EV face (measured on Sonic's tree; the other characters are built the same way, **UNVERIFIED**
  node by node). Sonic's quill wobble (`8C0B2376`) morphs the static `sonic_z` only, so it pauses while the EV face is
  shown.
* The face code indexes keys by **array position**, not by keyframe (`md->p[0][fp->face].key`), so Sonic's 21st key
  (frame 23) is index 20.
* Nothing here needs a new file: the EV heads and face motions are in the 1ST_READ image every world already holds (Tikal:
  the TIKAL_PROG image his world would hold), and their textures are in the character's own archive, which libsa1 already
  decodes.

### 2.2 How the game shows a face

**Face task.** Every character's main creates it at init, in gameplay too: `CreateElementalTask(2, 3, creator)` with
creators Sonic `8C0B2338`, Tails `8C0BF76A`, Knuckles `8C0C8E8E`, Amy `8C0DDDB2`, Big `8C0E59C0`, Tikal `0CB00ED4`. The
creator `CAlloc`s a 0x20-byte `PL_FACE` (`include/sa1/player.h` `sa1_PL_FACE`: face, face_next, step, rate, rate_spd,
seq, parent, sibling, object, motion) into `tp->mwp` and installs exec `8C07B106` and dest `8C07B0F6` (Tails uses his own
exec `8C0BF796` that picks the parent node of the normal or the flight model, then calls `8C07B106`). The exec publishes
the work as `playerpwp[pno]->pfp` and runs a small mode machine:

| Mode | Meaning |
|---|---|
| 0 | init; static face unless a sequence is already set |
| 1 | static face (`parent->sibling = sibling`); goes to 4 when the player's event face runs (`facewk.face_time != 0`), else to 2 when a gameplay sequence is set |
| 2 / 3 | gameplay sequence `pfp->seq`: blend from `face` to `seq[step].face` over `seq[step].time` steps, then the next step; at the end back to 1 |
| 4 | event face: follow the player's `eventwk` face block every step (`rate = face_count / face_time`); back to 1 when `face_time` becomes 0 |

In modes 3 and 4 it sets `parent->sibling = object` (the EV head) and calls `8C07B080`, which walks the EV head tree and,
for each node with keys, writes `points = lerp(key[face], key[face_next], rate)` with `8C055EFE`. The vertices are written
into the model in the world's own RAM every step; libsa1's display capture then draws the EV head like any other part.

**Two drivers.**

1. **Gameplay sequences**: `8C07B2B8(pno, n)` sets `pfp->seq = D_8C19C32C[n]` (SADX analogue `PFaceChange`), `8C07B2DC`
   clears it (`PFaceCancel`). Callers: Sonic `8C0A77BC` (mode 43: sequence 30 'e'), `8C0AFB10` (mode 16, action 23:
   sequence 8 'I'), Tails `8C0BBAE2` / `8C0BBDD6` / `8C0BC61C` (mode 13, action 26: 'I'), Knuckles `8C0C0640`,
   `8C0C57C8`, Amy `8C0DB000` (the callers in `1ST_READ.xrefs.json`). So the game already shows EV faces in a few
   gameplay moments, and G-SA1 0.7 runs that code.
2. **Event faces**: the player's `eventwk` (allocated by `PInitialize` through `8C04B440` for every player, so it exists
   in every libsa1 world) has a face block at 0x5C: face, face_next, face_time, face_count, `face_script` (0x64),
   `face_seq` (0x68). `8C04BBCE` (wrapper `8C04B39E`, SADX `EV_SetFace`) sets the script; `8C04BCC8` (called by the face
   task every step) advances it; `8C04BC20` reads one script character ('A'-'Z' = sequence 0-25, 'a'-'z' = 26-51, '0' =
   stop, anything else = its byte value); `8C04BC70` starts sequence n (n < 0: stop; n = 0 or an empty sequence: **a
   random idle, 'A' with probability 0.75 or the blink 'B'**); `8C04BD60` (wrapper `8C04B3B4`, `EV_ClrFace`) stops.

Consequences, all read from the C:

* When a script ends, `8C04BC20` returns 0 and the block keeps running the random idle ('A' / 'B': neutral with an
  occasional blink) **forever**, until a '0' or `EV_ClrFace`. So "EV face with natural blinking" is simply a script
  that ends; the static face comes back only on a stop.
* The event face has priority: in mode 1 the face task checks the event face first, and while it runs, gameplay
  sequences (the jump face) wait.
* The idle uses the world's `rand()`: deterministic per world (it is part of the recorded world, section 4).
* Two traps for a host that writes scripts or picks sequences: (a) a script character outside 'A'-'Z', 'a'-'e', '0'
  indexes past the 31-entry table (letters 'f'-'z' give 31-51; digits give their byte value): **out-of-table read**;
  (b) a sequence whose keys exceed the character's key count reads past the key array: **Big has 8 keys** and 17 of
  the 31 sequences use keys 8-19. The game's own events never do either (section 2.4); G-SA1 must validate.

### 2.3 The expression catalogue

The 31 sequences (`face_table.py`; "steps" = 60 Hz steps, `8C04BCC8` runs once per step from the face task):

| Seq | Letter | Steps {time, key} (abridged) | Keys | Length | Reads as (descriptive) | Valid for Big |
|---|---|---|---|---|---|---|
| 0 | A | (8,0) | 0 | 8 | neutral (also the idle) | yes |
| 1 | B | (2,1)(1,2)(1,3)(1,3)(1,2)(1,1)(1,0) | 0-3 | 8 | blink (also the idle, 25 %) | yes |
| 2 | C | (5,6)(5,5)(6,4)(6,0)(4,7)(6,5)(5,4)(6,6)(5,7) | 0,4-7 | 48 | talk (long) | yes |
| 3 | D | (4,4)(6,5)(6,7)(4,0)(4,4)(4,7)(4,5) | 0,4,5,7 | 32 | talk (medium) | yes |
| 4 | E | (4,7)(6,6)(6,5) | 5-7 | 16 | talk (short) | yes |
| 5 | F | (3,6)(4,15)(4,4)(3,13)(4,14)(4,5)(6,14)(4,7) | 4-7,13-15 | 32 | angry talk | no |
| 6 | G | (5,5)(6,6)(8,17)(5,0)(8,16)(5,5)(5,17)(6,19) | 0,5,6,16,17,19 | 48 | happy talk | no |
| 7 | H | blink, then (2,8)(24,8)(8,13) | 1-3,8,13 | 44 | blink, surprise, anger | no |
| 8 | I | (4,11)(8,9)(20,9) | 9,11 | 32 | effort / yell (gameplay jump face) | no |
| 9 | J | (4,0)(32,9)(12,9) | 0,9 | 48 | yell | no |
| 10 | K | lids down 6+6+6, hold 34, up 4+4+4 | 0-3 | 64 | slow blink | yes |
| 11 | L | (2,1)(3,2)(3,3)(2,3)(2,2)(2,1)(2,0) | 0-3 | 16 | blink (slower) | yes |
| 12 | M | (4,0)(48,8)(12,6) | 0,6,8 | 64 | surprised | no |
| 13 | N | (48,11)(16,11) | 11 | 64 | worried (held) | no |
| 14 | O | (48,12)(16,12) | 12 | 64 | sad (held) | no |
| 15 | P | (4,4)(5,17)(7,17) | 4,17 | 16 | "ha!" -> happy | no |
| 16 | Q | 16/17 alternating x 9 | 16,17 | 32 | laughing | no |
| 17 | R | (18,8)(10,8)(36,6) | 6,8 | 64 | surprised "oh" | no |
| 18 | S | (6,6)(5,7)(5,7) | 6,7 | 16 | short "oh" | yes |
| 19 | T | (4,0)(8,1)(8,2)(38,2)(6,1) | 0-2 | 64 | narrowed eyes | yes |
| 20 | U | (8,0)(8,1)(8,2)(8,3) | 0-3 | 32 | eyes slowly closing | yes |
| 21 | V | (16,3) | 3 | 16 | eyes closed | yes |
| 22 | W | (10,2)(12,1)(10,2) | 1,2 | 32 | half-closed | yes |
| 23 | X | (8,3)(8,2)(8,1)(8,0) | 0-3 | 32 | eyes slowly opening | yes |
| 24 | Y | (10,1) | 1 | 10 | lids a third down | yes |
| 25 | Z | (10,8) | 8 | 10 | surprised (hold) | no |
| 26 | a | (10,11) | 11 | 10 | worried (hold) | no |
| 27 | b | (10,17) | 17 | 10 | happy (hold) | no |
| 28 | c | (10,18) | 18 | 10 | smile (hold) | no |
| 29 | d | (10,19) | 19 | 10 | slight smile (hold) | no |
| 30 | e | (2,8)(60,8)(60,8)(60,8) | 8 | 182 | long surprise (Sonic gameplay mode 43) | no |

Keys as rendered (`faces/*_faces_yaw90.png`; consistent for Tails, Knuckles, Amy and Tikal, Sonic's art differs in
detail; the labels are ours, not the developers'): 0 neutral; 1-3 eyelids a third, two thirds, closed; 4-7 talking
mouth shapes (4 small open, 5 narrow, 6 round "o", 7 open "a"); 8 surprised "o"; 9 yelling "ah"; 10 grimace / frown
(**no sequence uses key 10**: only reachable by a custom expression); 11 worried; 12 sad; 13 angry, mouth shut; 14 angry
shout; 15 annoyed; 16 laughing; 17 happy, mouth open; 18 smile; 19 slight smile; Sonic 20 (frame 23) another neutral
(unused by every sequence). Big's 8 keys are neutral, blink 1-3 and four mouth shapes; his sequences are A, B, C, D, E,
K, L, S, T, U, V, W, X, Y (14 of 31). Knuckles' key 19 looked malformed in the research render (**UNVERIFIED**: render
artefact or real).

### 2.4 Lip sync

SA1 has none. Each spoken line gets a hand-written face script: in the 127 event overlays that set faces there are 623
such strings (438 distinct, median length 4, longest 80). Letter use: C 648, A 606, D 517, E 500, V 467, F 254, G 124, Z
117, b 117, B 110, N 100, ... (Q, Y, c, d, e never; no letter beyond 'e' and no digit other than '0', so the game's own
data never hits the trap of 2.2). Talk is therefore "C/D/E chunks for as long as the line lasts" (48, 32, 16 steps =
0.8, 0.53, 0.27 s). Typical scripts: "CDE", "CD", "FE" (angry), "VVVV..." (eyes closed), "CDAAAA..." (talk, then hold).

### 2.5 Look-at

The same event work drives the head and eyes (`eventwk` 0x04-0x23, decompiled setters in player common): `8C07C34C`
look at an angle (mode 1), `8C07C318` at a position (mode 2), `8C07C2E2` at a task plus offset (mode 3), `8C07C376` stop
(mode 0). Each character's look task (Sonic `8C0B28EE`: mode 1 follows the turn, mode 2 looks at the camera while pad bit
0x100 (Z) is held, mode 3 follows the event look) rotates the head and eye nodes within limits (`lookwk`, 0x88 bytes).
The look task rotates the head joint and the eyeball nodes, which stay in the tree while the EV face is shown (2.1), so
look-at and expressions combine (structure measured on Sonic; behaviour in a world **UNVERIFIED** until M1's test).

### 2.6 Decompilation status (measured with `range_status.py`)

| Range | What | Functions | Status |
|---|---|---|---|
| `8C07AEF8-8C07B300` | eyes/look helpers, face blend, face task, `PFaceChange`/`PFaceCancel` | 7, 974 B | all C |
| `8C04B440-8C04C100` | event work: alloc/free, action queue + player, path queue, face block, `EV_PJoinVertexes` | 18, 4,198 B | 16 C, 2 NON_MATCHING (`8C04BC70` registers only; `8C04BF00` 1,758 B), 0 asm |
| `8C07C120-8C07C400` | player event control: path follow, locks 1-4, rotation, look-at setters | 15, 732 B | all C |
| face creators / look tasks / mains | Sonic `8C0B2338` file, Tails `8C0BF262`, Knuckles `8C0C8A80`, Amy `8C0DDA00`, Big `8C0E59C0`; look tasks `8C0B2890` (3 C + 1 NM), `8C0BE160` (2 C) | | C |
| `TIKAL_PROG` | Tikal incl. his face creator | 17, 4,622 B | all 17 have C (13 matched, per the project notes) |
| `8C0491C0-8C04B440` | the event library (EV_* API wrappers, scheduler, event camera, messages) | 106, 7,702 B | 1 C (6 B); **7,696 B asm**. Only wrappers such as `8C04B39E` (EV_SetFace, 22 B) and `8C04B3B4` (EV_ClrFace, 14 B) touch faces; libsa1 can call the C they call |

### 2.7 What libsa1 needs for faces

Nothing to load. An API that drives the game's own face code, validates, and journals (section 4):

```c
/* sa1_face_cmd.kind */
#define SA1_FACE_STOP    0  /* the game's stop (8C04BD60): static face again; gameplay faces resume */
#define SA1_FACE_SCRIPT  1  /* a face script ('A'-'Z', 'a'-'e', '0'), copied into the world's extension segment and set
                               with 8C04BBCE; validated against the table (31) and the character's key count */
#define SA1_FACE_STEPS   2  /* a custom sequence {time 1..127 steps, key} x n (n <= 64), copied into the world and started
                               as the block's face_seq (the same 2-byte steps 8C04BCC8 plays); keys < key count */
#define SA1_FACE_HOLD    3  /* hold key k: a custom sequence {127, k} re-armed by libsa1 before it runs out */
typedef struct sa1_face_cmd { uint32_t struct_size, kind; char script[96]; uint8_t steps[128]; uint32_t nsteps;
                              int32_t key; uint32_t flags; } sa1_face_cmd;
SA1_API sa1_status sa1_character_face(sa1_context *ctx, sa1_character_id id, const sa1_face_cmd *cmd);

typedef struct sa1_face_info { uint32_t struct_size; uint32_t keys;      /* 0: no EV head (E-102, Eggman) */
                               uint32_t sequences;                     /* 31 */
                               uint32_t valid_sequences;               /* bit n: every key of sequence n < keys */
                               uint32_t used_keys; } sa1_face_info;    /* keys reachable through sequences */
SA1_API sa1_status sa1_face_info_get(const sa1_context *ctx, int32_t kind, sa1_face_info *out);

/* look-at through 8C07C34C / 8C07C318 / 8C07C2E2 / 8C07C376; mode 2 positions in host space are converted inside the
   world at the latch; mode 3 takes a host object (its proxy task) */
SA1_API sa1_status sa1_character_look(sa1_context *ctx, sa1_character_id id, uint32_t mode, const float v[3],
                                      uint32_t hostobj);
```

* The calls are latched and applied at the start of the world's next step (like host objects), so they are journal
  entries with a step index.
* `sa1_state_ex` gains the face state (`face`, `face_next`, `rate`, EV head shown) for ghosts and the UI.
* Wrappers `8C04B39E` / `8C04B3B4` (36 bytes) can be decompiled so that libsa1 calls exactly what the events call; not
  required (the C they call exists).
* **Render**: the face changes vertices once per step, not a transform: interpolated frames between pairs show the
  pair's vertices (30 Hz face steps). Check at 144 fps that a face transition does not pop against the interpolated body
  (**UNVERIFIED**; `sa1_character_mesh_lerp` could lerp the EV head's vertices).
* **Pre-warm gap (measured)**: the EV heads' textures are missing from G-SA1's material census
  (`material_census.txt`, archive 0): Sonic 25, 26, 27; Tails 19, 20; Knuckles 2, 3, 4; Amy 9, 10; Big 2. The scripted
  census play never showed an EV head. A face shown for the first time would create its materials on first use, the
  pattern the owner saw as a freeze in 0.5. The census run must show every EV head (a scripted face pass), or the
  pre-warm must add the EV head's materials.
* Tikal and Eggman cannot tick yet (`sa1_character_tick` returns `SA1_ERR_UNSUPPORTED`); Tikal's face needs his world
  (M6), Eggman has no EV head. For pose-only puppets the CPU pose path would blend the same keys (the math of
  `8C055EFE` is a lerp).

### 2.8 G-SA1 design: Faces

A **Faces** page (Q -> Options -> G-SA1 -> Faces; feature `faces` in its own files per the feature seam):

* **Face mode** per character (`gsa1_face_mode`): *Game* (as now: static face, the game's own gameplay faces),
  *Expressive* (EV face always on with the game's idle blinking: a script "A" at spawn, re-armed after a stop; gameplay
  faces such as the jump face are then suppressed, as in the game's events), *Off* (static face, gameplay faces too are
  cancelled with `8C07B2DC` each pair: **UNVERIFIED** that this is wanted).
* **Expressions**: a list of named presets built from the table in 2.3 (the 31 sequences, with names) plus custom ones
  (pick keys and timing on a small strip editor; held keys; key 10 and Sonic's key 20 available here). Entries not valid
  for the current character are greyed out (Big: 14 of 31). "Play" previews on the character. Thumbnails, if wanted, are
  rendered at run time from the disc (never shipped).
* **Hold vs. play once**: a preset either plays and returns to the idle (the game's behaviour) or is held until the next
  command (SA1_FACE_HOLD).
* **Hotkeys**: 10 slots, each a key binder (`DBinder`) stored in a ConVar and an expression; plus console commands
  `gsa1_face <preset>`, `gsa1_face_slot <n>`, `gsa1_face_stop` for users who prefer `bind`; an optional controller
  combination (hold LB + D-pad / face buttons, off by default because LB is SA1's L). Default: no keys bound (owner
  question). Hotkey presses are journal entries, so recordings reproduce them.
* **Talk**: "talk while this audio plays" builds a face script for the clip's length from C / D / E chunks; with
  "follow the loudness" it uses the clip's RMS envelope (module-side, 30 Hz): silent stretches become 'A', loud ones
  'C' / 'D', short bursts 'E' (and 'F' or 'G' if the line's mood preset is angry / happy).
* **Look**: "look at the camera" (mode 2 with the view position every pair), "look at" a picked entity (a host object
  or a position), "free".
* Everything is per character and, like every G-SA1 option, has a Q-menu control.

---------------------------------------------------------------------------------------------------------------------

## 3. Cutscene animations (event motions)

### 3.1 What is on the disc

| Item | Measured |
|---|---|
| Event overlays | 207 `EVxxxx.PRS` (EV0001-EV0180 hex range with gaps; name format `?ev%04X.prs` at `8C112AEB`), all loaded at `0x0CB80000`; 11,634,447 bytes compressed, 23,703,524 bytes as images (median 76 KB, largest EV0034 524,656 B: it ends at `0x0CC00170`, past the `0x0CC00000` where `S_MOT` etc. load, so whole EV images must not be mapped into worlds) |
| Their code | 8,861 functions, 2,007,752 bytes (the cutscene scripts: EV_* calls, camera, objects); 0 decompiled; they import the event library (`8C04921A`/`8C049220`/`8C04A840` from 202 overlays), `CreateElementalTask`, sound, `njRotate*`, ... |
| Character event motions | 923 `NJS_ACTION` bindings {character root, event motion} in 155 overlays; **404 distinct motions** (by key content; 188 shared between overlays), **4,597,280 bytes** (average 11 KB, largest 90 KB); frames: min 2, median 30, max 400 |
| Per character (distinct / overlays) | Sonic 80 / 72, Amy 89 / 29, Tails 61 / 52 (+ 4 on the flight model, 5 overlays), Knuckles 41 / 28, E-102 37 / 21, Big 36 / 16, Tikal 36 / 14, Eggman 20 / 13 |
| Other event models | event-only objects (helicopter, police car, Chaos 0, Egg Mobile, the Tornado, Tails' intro head, ...) with their own motions; SA Tools labels name 148 items in 13 overlays (`reference/dc_sa1_data_labels.csv`); only 17 motions are referenced from code pools instead of `NJS_ACTION` data, none next to a character root |
| Event texture archives | 36 `EV_*.PRS` (e.g. `EV_EGGMAN_BODY`, `EV_TR1`, `EV_E10x_BODY`) for event objects; not needed for character motions |
| Speed | events pass a speed per action; `EV001B` passes 0.5 (literal `0x3F000000` next to `SonicActionList_002_action`): 0.5 motion frames per display call (**UNVERIFIED** that it is the common value) |
| Gameplay actions | in addition, the characters' own action lists: 695 actions, 483 distinct motions (`sa1_assets.md`), selectable the same way |

Event names: the event number is the overlay number. Community lists name the SADX events by number (e.g.
`reference/sadx_pc_variables.csv` `EV_Sonic_CrystalRingGet`, ...); a generated name table in the DLL is allowed (names,
not data).

### 3.2 How events play them

* `8C04A120` (EV_SetAction, 34 B, asm) -> `8C04B4D2` (C): appends `{mode (bit 0: loop when last), linkframe, NJS_ACTION
  copy, texlist, next, speed}` (0x18 bytes, `CAlloc`) to `eventwk.list40`; `8C04A142` -> `8C04B528` (C) the same with a
  separate object and motion; `8C04A168` -> `8C04B48C` (C) clears.
* The character's **display** (`8C0AE17A` Sonic, `8C0BA820` Tails, `8C0C46AC` Knuckles, ... Tikal `0CB008A0`) draws
  the queue head with `8C04B57C` instead of its gameplay action when `list40` is set. `8C04B57C` blends in from the
  previous entry over `linkframe` frames (`8C041C92`), welds with `8C04BF00` (`EV_PJoinVertexes`, NON_MATCHING C), and
  **advances the frame and the queue itself, in the display pass** (`frame += speed`, then the next entry).
* Path queue: `8C04A1AC` -> `8C04B7BA` (C) appends `{mode, EV_PATH *, speed}`; `8C04B9E0` (C, called by every main)
  interpolates `pos` / `ang` between path points (`EV_PATH_POINT` = 3 signed bytes of angle in 1/256 turns + position);
  `list4C != NULL` forces the character into its event-path mode (Sonic mode 80, `8C0AFF1A`).
* Locks (player common, C): `8C07C1B4` walk to (x, z) with the character's own run physics and animations,
  `8C07C1EE` run along a direction, `8C07C240` push along a vector, `8C07C270` hold still, executed by `8C06EAC0` (C) /
  `8C06EFCE` (NON_MATCHING); `8C07C120` puts a player on a game path (`flag 0x1000`, smode 31).
* Event motions are bound to the **same model trees** the gameplay uses (Sonic `0x8C1CE76C`, ...), so the face task's
  EV head and the upgrades' welded parts work during them as in the game.

### 3.3 Design

* **Catalogue (generated, in the DLL)**: built offline from the developer's disc by a SADC tool (the `ev_motions.py`
  scan made exact): per entry the overlay name, the decompressed image's SHA-1, the `NJS_ACTION` address, the character
  kind and model, frames, a name ("EV0003 Sonic 2", plus a community event title when known) and a default speed. Only
  addresses, sizes and names: no data.
* **Loader (libsa1)**: on request, read `EVxxxx.PRS` from the disc on a worker thread, PRS-decompress (largest image
  525 KB), check the SHA-1, and copy the motion (`NJS_MOTION` + `NJS_MDATA` + key arrays, at most 90 KB) into each
  actor world's extension segment (`0x98000000`, bump allocator, 16 MB), relocating its pointers. The world never maps
  the overlay. A per-context cache keeps decoded motions by catalogue id.
* **API** (all journaled, applied at the next step):
  `sa1_character_event_action(id, {motion ref = catalogue id or own action index, speed, loop, linkframe, append|replace})`
  -> `8C04B4D2`; `sa1_character_event_path(id, points[], n, speed, flags)` -> path data in the extension segment +
  `8C04B7BA`; `sa1_character_event_move(id, lock, xz)` -> `8C07C1B4`..`8C07C270`; `sa1_character_event_clear(id,
  what)` -> `8C04B48C` / `8C04B82E` / `8C04BD60` / `8C07C2D0`; `sa1_character_event_mode(id, on)`: the pad ignored
  while a performance plays (neutral input; whether events also set other flags such as the "event running" byte
  `B_8C7492AC`, idle voices and vibration gates, is **UNVERIFIED** and is M2's first research item).
* **Determinism note**: because `8C04B57C` advances event actions in the display pass, and the display returns early
  when `8C038D00` finds the character invisible, a world that plays event actions must run its display pass every pair,
  also while replaying fast (seek). libsa1's live worlds use the camera-adapter view centred behind player 0, so the
  character is always "visible" and the result depends only on recorded inputs.
* **Puppets**: Eggman (kind 1) and Tikal (kind 4) cannot tick yet. Option A (M6): give them worlds (Tikal's code has C;
  Eggman needs `8C0ED880-8C0EEAA0`, 11 functions, 4,468 bytes, all asm). Option B (earlier): extend the CPU pose path
  (`sa1_character_pose`) to take a catalogue motion and a face key pair, for pose-only actors.

---------------------------------------------------------------------------------------------------------------------

## 4. Recorder and replayer

### 4.1 What decides a world

Measured from the code: `libsa1/src/world/sa1_world_tick.c` (the step and pair), `sa1_world.h` (pad, taps, camera,
inject), `sa1_api_world.c`, `libsa1.h`, and `libsa1_interaction.md` 2.8.

| Input | Where it enters | Record as | Size |
|---|---|---|---|
| Creation | `sa1_character_create[_ex]`: kind, position / angles, level / act, seed (default: the game's `srand(0xDEAD0CAB)`), upgrades (event flags), start rings, flags (stage tasks ...), options set before the world | the full `sa1_character_desc` with the position in SA1 space (bit-exact, `SA1_CREATE_SA1_SPACE` + `SA1_CREATE_ANGLES`) | ~100 B per world lifetime |
| Static collision | `sa1_collision_set_static[_ex]` / `sa1_collision_set_prepared` (G-SA1: the map's BSP, displacements, static props, cleaned; GRID synthesis; surface types) | a **fingerprint**: SHA-1 of the triangles and surface words given to the adapter, the adapter / GRID parameters and libsa1's adapter version; the map name, BSP hash, workshop id | ~100 B |
| Pad | `sa1_input` -> `sa1g_pad_from_input` (buttons to Katana bits, stick to -128..127) -> the world's host pad; presses between pairs ORed (`taps`) | the converted pad and taps as the pair consumed them | 12-16 B per pair |
| Camera | `sa1_input.camera_yaw` -> `w->camera.ang[1]` (BAMS) -> the camera adapter task and the view (`sa1g_camera_view`) | the BAMS value per pair | 2 B per pair |
| Moving collision objects | `sa1_collision_object_add / move / remove` (G-SA1: brush entities, props, vehicles near the character, nearest first, parked when far) | the latched SA1-space state per step when it changed (exact floats, XOR-delta) | 0 when still; ~30 B per moving object per step |
| Host objects | `sa1_hostobj_*` (NPC / player / holdable / balloon proxies), `sa1_hostobj_configure`, `world_mask`, `release` | the latched per-pair object stream (the interaction design already makes it deterministic: latch in the world's FP mode, window order (distance, id), lowest free slots) | per object per pair while it moves |
| Character calls | `sa1_character_damage`, `add_rings`, `set_option`, faces, look, event commands, later 1.0's object commands (`v1_arch.md`) | the call with its step index | small |
| Steps | the dt accumulator (2 steps per pair, at most 8 pairs per tick, backlog dropped) | nothing: entries carry the pair / step index, the player runs pairs, not seconds | - |
| Display pass | runs after each pair in G-SA1 (capture on) | a flag in the header; replays run it every pair (3.3) | - |

Inside a world nothing else varies: per-world `rand`, frame counter, heap (fixed arena), no clock reads, no host calls
in a tick, MXCSR fixed during the tick (`libsa1_integration.md` 8). The ten reference traces replay strictly bit-exact
on x86 and x64, a second run in one process is byte-identical, and worlds do not depend on how ticks are interleaved
(Phase 7). What has **not** been tested is a recorded G-SA1 session with host objects and movers (gate J1 below).

Two rules follow:

1. **Record inside libsa1, at the latch, in SA1 space.** Conversions from host floats happen outside the world's FP
   mode in some paths (Source can change the host's MXCSR, **UNVERIFIED** in GMod); recording the converted values makes
   replays independent of the host's FP state, of G-SA1's frame timing, and of Lua.
2. **Replay through the live path.** The journal player sets the same host pad, camera and latched objects and runs
   `sa1g_world_pair` - the code path that recorded it. (libsa1's existing "replay mode" injects harness trace records:
   rand, XMTRX, camera blob; it is for exactness tests against the interpreter and is not used here.)

### 4.2 The journal (libsa1)

* `sa1_journal_begin(ctx, id, flags)`: from the next pair on, the world appends entries to an in-memory journal; the
  header holds the creation descriptor, the collision fingerprint, the logic fingerprint (a hash of the code map and the
  lowered build: `sa1g_game_info()`-style), libsa1's version and the journal format version.
  `sa1_journal_end` / `sa1_journal_bytes` return a serialized, versioned blob (no host pointers, no disc data).
* Entries: `{step index (varint delta), kind (u8), payload}`: PAD, CAMERA, MOVER (add / move / remove / park), HOSTOBJ
  (latched delta), DAMAGE, RINGS, OPTION, FACE, LOOK, EV_ACTION, EV_PATH, EV_MOVE, EV_CLEAR, EV_MODE, COLLISION (a new
  fingerprint at this step), OBJECT (1.0), END. Payloads are exact bit patterns.
* A world lifetime is one **segment**. G-SA1's respawn (falling out, death), a character switch or a transformation
  that rebuilds the world starts a new segment with its own descriptor.
* `sa1_character_create_replay(ctx, blob, size, flags, &id)` builds a world from the descriptor (refusing a disc whose
  `1ST_READ.BIN` is not the journal's), and `sa1_character_replay_to(ctx, id, pair)` runs pairs from the journal
  (forward only; backwards = recreate and run again, or restore a snapshot, 4.4). Live input is ignored while a world
  replays. The render API works unchanged, so a replay draws exactly like live play.
* The journal also records a **ghost**: per pair the state (`sa1_state_ex` subset: SA1 position, angles, action, frame,
  flags, rings, face state, event action id and frame), about 40 bytes.

### 4.3 Ghosts, divergence and fallback

* On replay, each pair's state is compared with the ghost. Identical = exact replay. A difference means something
  outside the journal changed (another libsa1 build with a NON_MATCHING fix, another map version, other collision
  settings): the actor switches to **ghost playback** from that pair (CPU pose at the ghost's position / action / frame,
  plus the face keys) and the Director shows "approximate from 12.4 s: the map's collision differs".
* Before playing, fingerprints are compared: a logic or collision mismatch is announced up front (play anyway with
  checking, or play as ghost).
* Ghosts also give cheap previews (scrubbing many actors) and the multiplayer path (`sh_net.lua`'s puppets pose from
  the same records).

### 4.4 Seeking

Re-simulating from the start is exact and simple. Cost (measured elsewhere, not re-measured here): 0.010-0.037 ms per
60 Hz step in replay mode without display (`libsa1_integration.md`, tick time), and under 0.3 ms per pair with display while
Sonic charges the Light Speed Dash (`g-sa1/README.md` 0.6.0). A seek to 10 minutes (18,000 pairs) of one actor therefore
costs about 0.4-1.3 s without display and up to about 5 s with it; event performances need the display pass (3.3).
Hence, in M7: **world snapshots** every 10-30 s of recording (the 16 MB arena, the used part of the extension segment and
the world struct's host-side fields; stored as a delta against the pristine image, likely 1-3 MB each, **UNVERIFIED**),
and a display pass without capture for fast-forward (**UNVERIFIED** saving).

### 4.5 Takes and several actors

Each actor is its own world (as followers are, plan 14.1a). Recording is layered: record actor A; then record actor B
while A replays; B's world sees A as a host object (its proxy positions from A's replay) and B's journal records those
latched inputs. A was recorded without B, so A does not react to B (as in any layered take system); recording both live
at once is possible later (two controllers, or one player plus followers). Every actor's replay is independent, so
an actor can be re-recorded without touching the others.

### 4.6 What is not in a world

* **The GMod scene**: props, NPCs, doors move by GMod physics and are not reproduced by the journal. The character's world
  still gets them exactly (movers and host objects are journaled), so a replay is right even if the props now stand
  elsewhere - but it can look wrong (standing on an invisible crate). Options: show recorded movers as wire boxes in the
  Director; later (M7) "restore props": record the transforms of the movers' entities and drive them (frozen, moved
  per frame, server side) during playback.
* **The view**: the recording also stores G-SA1's camera (position, angles, FOV per pair), usable as a camera track.
* **Sound**: the worlds' SE / voices / music cues are reproduced by replaying (they are world events); the jukebox and
  owner audio are timeline clips (section 5).

### 4.7 Verification (gates for M0)

| Gate | Test | Pass |
|---|---|---|
| J1 live record = replay | `g-sa1/tests/diag_maps.cpp`-style scripted play on the 13 test maps (ramps, crates as movers, a few host objects), recorded, then replayed in a fresh process; compare the world RAM hash (arena + extension segment) after every pair | identical every pair, x86_64-windows and x86-windows, twice |
| J2 interleaving | 3 actors (two recorded live, one replaying) ticked in different orders and frame rates (60 / 144 / uneven dt) | each world identical to its solo replay |
| J3 fingerprints | change one triangle of the map, one GRID parameter, the libsa1 build id | each mismatch detected before play; divergence detected at the first differing pair |
| J4 journal size | 10 minutes of play on gm_bigcity with movers | report bytes per minute, raw and compressed |
| J5 faces / events | the face and event commands journaled; a face script and an event action queue replayed; display pass on and off at seek | identical (display on); a detected divergence with display off is acceptable only if seek never skips it |
| J6 old journals | a journal from build N played by build N+1 | exact, or a clean ghost fallback; never a crash |

---------------------------------------------------------------------------------------------------------------------

## 5. Audio track

| Source | On the disc (measured / from `sa1_sound.md`) | Player |
|---|---|---|
| Voices | `EVENT_ADX_US.AFS` 2,044 mono 22,050 Hz ADX clips (65,200,128 B); `EVENT_ADX.AFS` Japanese (73,484,288 B); clip = voice number | libsa1 audio streams (`SA1_AUDIO_SOURCE_VOICE`) |
| Music and jingles | 110 music numbers (name table `0x8C5FB8A8`), incl. **event music 36-41 `EVTBGM00-05.ADX`** and 10 `BOSSEVNT.ADX`; 69 `NIGHTS_K` absent; 87 truncated | libsa1 audio streams (`SA1_AUDIO_SOURCE_MUSIC`), the jukebox engine |
| SE | tones of the common banks 0 / 2, the character banks 3 / 6, a stage's banks 1 / 4 / 5 (`sa1_audio_sound_info`) | libsa1 audio + mixer |
| Event SE | 51 `E_xxxx.MLT` (6,701,280 B; the overlay names its bank, e.g. `e_0001.mlt` in EV0001): bank 6 with 706 programs and 927 sequences in total, plus SFPB / SFOB; loaded in place of the voice-SFX bank (`sa1_sound.md` 3: threshold 1214) | **new**: `sa1_audio_load_event_bank(audio, n)` (libsa1 audio, clean-room as the rest) |
| Owner MP3 / WAV / OGG | files the owner puts in `garrysmod/data/gsa1/audio/` (or `sound/gsa1/`) | the module, through Garry's Mod's own BASS (`BASS_StreamCreateFile` from memory: Lua reads the bytes, the module never takes a path), or `sound.PlayFile` (relative to `garrysmod/`; **UNVERIFIED** for `data/` in game) |
| Owner ADX | the same folder | libsa1's ADX decoder from memory (**new**: `sa1_audio_stream_open_memory`); a file whose SHA-1 equals a disc file becomes a disc reference |

Design: a **timeline audio engine** in the module, an extension of the jukebox's (`jukebox.md` 6.3: its own libsa1
audio objects, decoding on its own thread, one BASS push stream per playing source at the source's rate, 1.5 s queued
so the music survives Lua hitches). The Director's clock is the master; each clip has {source ref, start time, offset,
length, gain envelope, pan or "from actor" (3D from an actor's position), loop}. On play / seek the engine positions every
clip and re-syncs a clip that drifts more than 30 ms (**UNVERIFIED** threshold). The game's own SE / voices / music cues
that the replaying worlds emit play through G-SA1's normal sound path, as in live play. A "speaker" link ties a voice or
owner clip to an actor: its face talks (2.8) for the clip's length.

---------------------------------------------------------------------------------------------------------------------

## 6. Subtitles

* **The game's lines are data in the event overlays**: 176 overlays have a 5-language table (Japanese, English, French,
  Spanish, German, in that order) of pointers to equal-length arrays of lines; each line starts with byte 0x07 (a
  control byte, **UNVERIFIED** meaning). Example EV0003: table at `0x0CB81D2C`, 5 lines per language ("Huh?", "What?
  Tails?", ...). Heuristic count: about 1,152 lines per language. Which voice clip goes with which line is decided by
  each event's code (an `EV_Msg` call next to a voice call); a generated voice <-> line table needs a static analysis of
  the 2.0 MB of event code (M5, **UNVERIFIED** completeness).
* **Subtitle track**: cues {start, end, text, style, speaker actor}. Text is the owner's own (UTF-8), or a **reference**
  to a game line (overlay, language, index) that is read from the friend's own disc at load; never copied into the file.
  Choosing a game voice offers its line as a starting text (shown from the disc, saved as a reference unless the owner
  edits it).
* **Look**: the game's message window and font when `v1_npcs.md`'s message drawing exists (the HUD layer); otherwise a
  clean GMod font (`surface.CreateFont`) with outline, position and size settings. Export of the owner's own subtitles
  as `.srt` (`data/gsa1/machinima/<name>.srt.txt`) for video editing.

---------------------------------------------------------------------------------------------------------------------

## 7. Keyframes

The Director's time base is the **pair** (1/30 s, the world's input resolution); clips and keys are stored in seconds
(double) and snap to pairs where they drive a world. Rendering interpolates between pairs (`sa1_character_render` alpha),
so playback is smooth at any frame rate and in slow motion.

**Camera tracks (G-SA1 side)**: keys {time, position, angles, FOV, roll}; interpolation per segment: cut, linear,
smooth (centripetal Catmull-Rom on position, quaternion squad on rotation), ease in / out; modes: free, follow an actor
(offset, smoothing), look at an actor or entity, ride a spline (`v1_triggers.md`'s spline tool and spline IDs, shared),
"use the recorded play camera", "cut to a placed `gmod_cameraprop`". While the Director plays, its view is the view; it
yields like G-SA1's chase camera must when the player looks through a camera entity (plan 14.1e).

**Character keys compile to the game's own event commands** (so the motion is the game's): a position key with "walk
there" -> lock 2 (`8C07C1B4`, the character runs there with its own physics and animations); "glide along" -> an event
path (`8C04B7BA`: G-SA1 samples a Catmull-Rom curve through the keys into `EV_PATH_POINT`s; the game interpolates
linearly between them); an animation key -> an event action (catalogue or gameplay action; loop, blend-in frames, speed);
face and look keys -> 2.7. A key list per actor becomes journal entries at their pairs, so a choreographed actor
replays exactly like a recorded one, and a recorded take can be continued with keys (or the reverse).

---------------------------------------------------------------------------------------------------------------------

## 8. The project file

**Where**: `garrysmod/data/gsa1/machinima/<name>.dat` (Lua `file.Open(..., "wb", "DATA")`; `.dat` is believed to be
allowed for `DATA` writes, **UNVERIFIED**, fallback `.txt`). Lua reads and writes the bytes; the module serializes and
parses them (fuzz-tested C++, the module's rule of no paths).

**Layout** (little-endian):

```
"GSA1MACH"  u16 format_major  u16 format_minor  u32 header_len  header (UTF-8 JSON)
chunks:     fourcc  u32 chunk_version  u32 flags (bit 0 = zlib)  u64 length  payload
trailer:    "END!"  SHA-256 of everything before
```

Header JSON (unknown fields are kept on re-save):

```json
{ "title": "...", "author": "...", "created": "2026-10-21T18:00:00Z", "modified": "...",
  "app": { "gsa1": "2.0.0", "module": "2.0.0", "libsa1": "1.4.0", "logic": "<sha1 of the logic build>" },
  "requires": { "disc": "SA1 DC US 1.005", "first_read_sha1": "e74c849806db103aa87f84e41a382873bc96b6df",
                "map": { "name": "gm_bigcity", "bsp_sha1": "...", "workshop": null },
                "collision": "<fingerprint>", "features": { "faces": 1, "events": 1 } },
  "timeline": { "length": 184.3, "pairs": 5529 },
  "actors": [ { "id": 1, "name": "Sonic", "kind": 0 } ],
  "assets": [ { "kind": "owner-audio", "name": "theme.ogg", "size": 1834221, "sha256": "...", "packed": false } ] }
```

| Chunk | Content |
|---|---|
| `ACTR` | actors: kind, name, appearance options (upgrades, face mode), segments (each a creation descriptor) |
| `JRNL` | one per actor segment: the libsa1 journal blob (4.2) |
| `GHST` | one per actor segment: the ghost track |
| `CAMK` | camera tracks and keys |
| `CHRK` | character key lists (the Director's editable form; their compiled entries are in `JRNL`) |
| `AUDI` | audio clips: source refs (voice n, music n, tone n + bank, event bank n, owner file name + hash), times, envelopes |
| `SUBT` | subtitle cues: owner text or game-line references |
| `MOVR` | optional: recorded GMod entity transforms for "restore props" |
| `PACK` | optional: the owner's own audio files, only when ticked |
| `THMB` | optional: a thumbnail the owner took in GMod |

**Never embed game data** (owner rule): the writer accepts only references for game items (voice index, music number,
tone, `E_xxxx` bank number, catalogue id = overlay name + image SHA-1 + `NJS_ACTION` address, game line = overlay +
language + index); a validator rejects any chunk that carries PCM, ADX, textures, models, motion keys or game text. The
journal and ghost hold the owner's inputs and positions only. A friend needs: their own disc (same `1ST_READ.BIN`
SHA-1; the file names the build, a public hash), G-SA1 with the file's major format, the same map for exact replays
(else ghost playback), and the owner's audio files when they were not packed.

**Versioning**: format_major changes only for an incompatible layout (readers keep migrations for old majors); minor for
additive changes (unknown chunks are skipped and preserved). Each chunk has its own version; `JRNL` also carries the
libsa1 journal version and the logic fingerprint, which decide between exact and ghost playback (4.3).

**Sizes** (estimates from 4.1, **UNVERIFIED** until J4): one actor, 10 minutes: journal pad + camera about 0.3 MB raw
(compresses well), ghost about 0.7 MB raw, movers 0-6 MB raw on prop-heavy maps (XOR-delta + zlib: a fraction).

---------------------------------------------------------------------------------------------------------------------

## 9. G-SA1 UI

* **Q -> Options -> G-SA1 -> Faces** (2.8) and **Machinima** (settings: project folder, default camera smoothing,
  subtitle look, audio sync, "approximate playback" behaviour, hotkeys for record / play / stop / mark, render mode).
  Every option has a control there (owner rule).
* **The Director**: a Derma window opened from the Machinima page, the 1.0 G-SA1 creation tab (when it exists:
  `v1_arch.md`), the command `gsa1_director` or a bindable key. Parts: transport (record, play, pause, stop, loop, speed
  0.1-2x, "fixed step" render mode), a timeline with one lane per actor (takes, key lists, faces), camera lanes, audio
  lanes, a subtitle lane; a browser for the event-motion catalogue (by character and event, preview on the actor), the
  game's voices / music / SE, owner files; property panels; project open / save / save as / recent; a "what this file
  needs" report (disc build, map, missing owner files, approximate actors).
* **Render mode "fixed step"**: the Director's clock advances exactly 1/60 s (or 1/30) per rendered frame regardless of
  wall time, so Source's `host_framerate` / `startmovie` or a screen recorder get frame-exact output
  (**UNVERIFIED** in GMod x64).
* **Recording**: pick the actor (the player's own character or a new actor), countdown, record; the Q menu and HUD show
  "REC"; stop writes the take; undo / redo per take.

---------------------------------------------------------------------------------------------------------------------

## 10. Phases and effort

A "round" below is one workflow round as used for 0.5-0.8 (a few LIB / G-SA1 / decomp agents plus an independent
verifier). Order recommended: M1 first (small, visible in normal play, needs no recorder), then M0, then the rest.

| Phase | Goal | Decompilation | LIB | G-SA1 | Verification | Effort |
|---|---|---|---|---|---|---|
| M1 Faces | EV faces, expressions, hotkeys, look-at, talk from audio | none required; optional `8C04B39E`, `8C04B3B4` (36 B) | `sa1_character_face`, `sa1_face_info_get`, `sa1_character_look`, face state in `sa1_state_ex`, validation (Big, script letters), journal-ready command queue | feature `faces`: page, presets with names, custom strip editor, 10 hotkey slots + commands, Expressive / Game / Off, look at camera / entity, talk timer; census + pre-warm of the EV heads' materials | harness reference of a face script in a world (`hx`-style), libsa1 strict replay of it; core_test census includes EV heads; Lua tests of the page | 1 round (M) |
| M0 Journal | record and replay one actor exactly; ghost; fingerprints | none | journal (begin / end / bytes), replay create / run, ghost, collision and logic fingerprints, segments; tests J1-J4, J6 | minimal record / play buttons, file save / load (`JRNL`, `GHST`, header), divergence message | J1-J4, J6 | 1 round (L) |
| M2 Event motions | play any of the 404 cutscene motions (and the 695 gameplay actions) on a live actor | wrappers `8C04A120`, `8C04A142`, `8C04A168`, `8C04A1AC`, `8C04A220` (144 B, optional); research the event-mode flags (part of the 7.7 KB event library) | catalogue generator (SADC tool) + generated table; EV overlay loader + motion copy into the extension segment; event action / path / move / clear / mode API, journaled | catalogue browser with preview, "perform" on an actor | per motion: libsa1 world pose vs `rip_character.py` CPU pose at frames 0 / mid / end; a harness run of one real event (EV0003) as the oracle for queue timing; J5 | 1-1.5 rounds (M) |
| M3 Director | timeline window, takes, multi-actor layering, camera tracks, project file v1 | none | snapshots optional | Director window, lanes, transport, camera keys and modes, project file (all chunks but `MOVR` / `PACK`), the needs report | file round-trip and fuzzing; Lua tests against a mock; owner test | 2 rounds (L) |
| M4 Audio | voices, music, SE, event SE, owner MP3 / WAV / OGG / ADX on the timeline | none | `sa1_audio_load_event_bank`, `sa1_audio_stream_open_memory` | timeline audio engine (from the jukebox's), browser, envelopes, speaker links | decode tests of all 51 event banks; sync drift measurement over 10 min | 1 round (M) |
| M5 Subtitles | subtitle lane, game-line references, look, `.srt` export | none (static analysis of event code for voice <-> line, no decompilation) | none | subtitle lane and renderer (HUD layer / `v1_npcs.md` window) | reference resolution on 176 overlays; text never in saved files (validator test) | 0.5-1 round (S-M) |
| M6 Choreography and puppets | character keys compiled to event commands; Tikal and Eggman as actors | Eggman `8C0ED880-8C0EEAA0` 11 functions, 4,468 B (for a ticking Eggman); Tikal is C | ticking Tikal / Eggman worlds or the pose path with event motions and face keys | key editor per actor (walk to, glide, animate, face, look) | strict replays of choreographed takes; Eggman / Tikal references in the harness | 1 round (M) |
| M7 Polish | seek snapshots, fast-forward without capture, restore props, fixed-step render, sharing UX | none | world snapshot / restore | `MOVR`, `PACK`, render mode, recent files, migration of v1 files | seek time after 10 min under 0.5 s (target); restore-props test | 1 round (M) |

Total: about 8-9 rounds. Dependencies: M0 before M3 (and M2's journaling); M1's commands use M0's queue format (they can
ship before M0: the queue simply is not recorded yet); M5 needs the HUD layer or `v1_npcs.md`'s message window for the
game look; M3's spline camera shares `v1_triggers.md`'s spline tool; 1.0's objects (`v1_arch.md`) add OBJECT journal
entries.

---------------------------------------------------------------------------------------------------------------------

## 11. Decompilation still to do (bytes)

| Item | Functions | Bytes | Needed? |
|---|---|---|---|
| Face code, event work, player event setters, look tasks, face creators | 0 asm | 0 | already C (2 NON_MATCHING: `8C04BC70`, `8C04BF00`) |
| Event-library wrappers for faces (`8C04B39E`, `8C04B3B4`) | 2 | 36 | optional (M1) |
| Event-library wrappers for actions / paths (`8C04A120`, `8C04A142`, `8C04A168`, `8C04A1AC`, `8C04A220`) | 5 | 144 | optional (M2) |
| Rest of the event library `8C0491C0-8C04B440` (scheduler, event camera, messages, EV_* API) | 98 | 7,516 | no for this design; read for the event-mode flags (M2 research); the message part may serve `v1_npcs.md` |
| Eggman `8C0ED880-8C0EEAA0` | 11 | 4,468 | only for a ticking Eggman actor (M6) |
| Cutscene scripts in 207 EV overlays | 8,861 | 2,007,752 | no (only their data is used); a "play the game's own cutscene" feature would need them (not proposed) |
| Sound / ADX | - | - | clean-room in libsa1 audio (owner rule), no decompilation |

---------------------------------------------------------------------------------------------------------------------

## 12. Risks and UNVERIFIED points

1. **Live determinism with host objects and movers is untested** (J1). The reference traces prove the step; a G-SA1 session
   adds host-object latching, mover refresh inside steps, collision prepared on a worker, and Lua-side respawns. Recording
   at the latch in SA1 space removes host FP and timing effects; J1 must still prove it.
2. **Any libsa1 logic change** (a NON_MATCHING fix, a new stage task) can change old recordings: detected by the logic
   fingerprint and the ghost compare, played as ghost from the first difference. Keep old journals in the test suite (J6).
3. **Map or collision changes** (workshop update, new GRID parameters, the surface table): same fallback.
4. **Display-pass dependence**: event actions advance in the display pass (`8C04B57C`), so seeking must run the display;
   seek cost up to about 5 s per 10 minutes without snapshots (4.4).
5. **Out-of-range face data**: script letters beyond 'e' and Big's keys beyond 7 read past game tables; libsa1 must
   validate (2.2).
6. **EV head materials not pre-warmed** (2.7): a first face could hitch.
7. **Face morph and interpolation** at high frame rates (30 Hz vertex steps) untested.
8. Look-at together with the EV face in a running world (2.5: the node structure allows it); whether events set more
   flags than the pad (3.3).
9. **Voice <-> subtitle mapping** needs static analysis of uncompiled event code; it can be incomplete (then the
   browser offers voices without text).
10. **GMod file rules**: `.dat` writes in `DATA`, `sound.PlayFile` from `data/`, `startmovie` in GMod x64: **UNVERIFIED**.
11. **Copyright hygiene**: game voices, music, motions and subtitle text must stay references; the validator and a test
    that scans saved files for disc byte runs (e.g. the ADX header `0x8000` + CRI signature, PVR/GBIX, motion key arrays
    of the catalogue) guard it.
12. **Memory**: 16 MB per actor world; 8 actors 128 MB plus snapshots.
13. The key names in 2.3 are descriptive (from renders), not the developers' names.

---------------------------------------------------------------------------------------------------------------------

## 13. Questions for the owner

1. Faces in normal play: should characters use their EVENT face all the time with the game's own blinking
   ("Expressive"), or only when you pick an expression ("Game", today's look)? Default: Expressive on, switchable.
2. Expression hotkeys: which keys by default? Default: none bound; 10 slots you can bind in the Q menu (plus console
   commands); an optional controller combo (hold LB + D-pad) off by default.
3. While an expression hotkey is pressed: play the expression once and go back to blinking (as the game does), or hold
   it until the next key? Default: per preset, "play once" for talking and reactions, "hold" for moods.
4. Recording: one character at a time with layered takes (default), or several at once from the start?
5. Should replays also move GMod props back to where they were when you recorded (they are frozen and moved by G-SA1
   during playback)? Default: off at first (M7).
6. Your own audio files: keep them out of the project file (a friend needs the same files) or pack them in when you tick
   a box? Default: not packed.
7. Subtitles: the game's own message window and font, or a clean modern font? Default: the game's look when the HUD
   layer exists, otherwise clean.
8. Eggman and Tikal as actors (Eggman needs 4.5 KB of decompiling): wanted in the first Director release, or later?
   Default: later (M6).
9. Talking faces for your own voice clips: automatic from loudness (default) or simple C-D-E chunks?
10. Japanese voices / subtitles as an option? Default: English, Japanese selectable.

---------------------------------------------------------------------------------------------------------------------

## 14. Owner-facing summary (plain words)

* **Faces first.** The game already has a second, "talking" head for Sonic, Tails, Knuckles, Amy, Big and Tikal with
  20 or so expressions (Big has 8), and G-SA1 already runs the code that shows it. We add a Faces page: turn the
  expressive face on, pick expressions (blink, talk, surprised, angry, happy, sad, eyes closed, ...), bind them to keys,
  make a character look at the camera, and make the mouth move while a sound plays. The game has no real lip sync either:
  it plays "talking" mouth loops for the length of each line, and we do the same.
* **Cutscene animations.** The game's cutscenes contain about 400 different character animations (Sonic 80, Amy 89,
  Tails 65, Knuckles 41, Gamma 37, Big 36, Tikal 36, Eggman 20). Your characters can perform any of them, with the game's
  own blending, and walk or glide between marks using the game's own cutscene moves.
* **Recording.** Because the characters are exact copies of the game's logic, a recording only needs your button
  presses and what the world around the character did; playing it back gives the identical performance. If the map or
  G-SA1 changed since, the recording still plays from stored positions.
* **The Director**: a timeline window with your characters, camera moves, sounds (the game's voices, music and effects
  from your disc, plus your own MP3 / WAV / OGG / ADX), subtitles and keyframes, saved as one file in
  `garrysmod/data/gsa1/machinima`. Friends who own the game can open it; it never contains anything from the disc.
* About 8-9 rounds of work, faces first.

---------------------------------------------------------------------------------------------------------------------

## Appendix A: scratch tools and outputs (`sadc/build/research/machinima/`)

| File | What |
|---|---|
| `ev_stats.py` -> `ev_stats.txt` | per EV overlay: functions, code bytes, image and PRS sizes, imported 1ST_READ symbols with overlay counts |
| `face_motions.py` | EV head trees and face motions: nodes, points, key counts, keyframes, key bytes |
| `face_table.py` | the 31 face sequences of `D_8C19C32C` |
| `face_scripts.py` | face scripts in the EV overlays (counts, letters) |
| `face_sheet.py` -> `faces/<char>_faces_yaw90.png` | contact sheets of every key of every EV head (env `FACE_MARGIN`, `FACE_DY`, `FACE_SIZE`, `FACE_COLS`) |
| `ev_motions.py` -> `ev_motions.csv` | `NJS_ACTION` bindings of character trees to event motions (overlay, action address, character, motion, frames) |
| `ev_motions2.py`, `ev_motions3.py` | distinct motions by content; code-pool motion references |
| `range_status.py` (+ `status_cache.json`) | decompilation status of address ranges (`py -3.12 range_status.py 8C04B440:8C04C100:label!`, `!` lists functions) |

Inline measurements (re-run from the commands in this round's transcript if needed): the subtitle tables (5 pointers to
equal-length arrays of 0x07-prefixed strings: 176 overlays, about 1,152 lines per language); `E_xxxx.MLT` headers (51
files, bank 6, 706 programs, 927 sequences); the music name table entries 10 and 36-41; the material census textures per
kind.

---------------------------------------------------------------------------------------------------------------------

## Review (adversarial)

Reviewer round, 2026-10-21. Read-only against `sadc/src`, the asm, `build/*.BIN`, `baserom/1ST_READ.BIN`, `disc/`,
`libsa1/` and `g-sa1/`. Scratch tools: `sadc/build/research/machinimareview/` (`evhead_tex.py`, `evhead_mat.py`,
`distinct_bytes.py`, `subs.py`, `subs2.py`, and copies of `ev_motions*.py` / `range_status.py` that write only there).

### R.1 Verified

* **EV heads** (`face_motions.py` re-run, `evhead_tex.py`): addresses, nodes / points (Sonic 2 / 427, Tails 3 / 382,
  Knuckles 2 / 440, Amy 5 / 427, Big 7 / 346), keys (Sonic 21 = frames 0-19 and 23, Tails / Knuckles / Amy 20, Big 8
  on nodes 0, 1 and 4), Tikal `0x0CB0DA04` (5 nodes, 428 points, motion `0x0CB23298`, 20 keys; TIKAL_PROG image
  218,836 B). The EV-head texture lists in 2.1 match the materials exactly. In every case the static face node and the EV
  head have the same node count.
* **Face code** (`code_8C07B080.c`, `code_8C04BC70.c`, `code_8C04B7BA.c`): the mode machine of `8C07B106`, the
  array-index key access `md->p[0][fp->face].key`, the letter mapping in `8C04BC20`, the 0.75 / 0.25 random idle in
  `8C04BC70`, and "a finished script idles forever" (`8C04BC20` returns 0, then `8C04BC70(0)`). `D_8C19C32C` has 31
  entries. Entry 31 is `0x00020109`, which is not a pointer, so letters 'f'-'z' and digits fault in a libsa1 world. The
  table in 2.3 and the result "Big: 14 of 31 valid" are both correct.
* **Face task creation**: `CreateElementalTask(2, 3, creator)` runs in each main's mode 0 (Sonic `code_8C0A77BC.c:216`,
  Tails `code_8C0B6148.c:161`, Knuckles `code_8C0C0640.c:150`, Amy `code_8C0D6B40.c:125`, Big `code_8C0DFFA0.c:189`,
  Tikal `code_0CB00000.c:120`). Each creator has exactly one caller in the xrefs, its main.
* **Decompilation status** (`range_status.py` re-run):
  * `8C07AEF8-8C07B300`: 7 functions, 974 B, all C.
  * `8C04B440-8C04C100`: 18 functions, 4,198 B. 16 are C; `8C04BC70` and `8C04BF00` are NON_MATCHING.
  * `8C07C120-8C07C400`: 15 functions, 732 B, all C.
  * Event library `8C0491C0-8C04B440`: 106 functions, 7,702 B, of which 6 B are C.
  * Eggman `8C0ED880-8C0EEAA0`: 11 functions, 4,468 B, all asm.
  * Wrappers: 22 + 14 = 36 B, and 34 + 38 + 24 + 24 + 24 = 144 B.
  * TIKAL_PROG: 17 functions, 4,622 B.
  * Every character has a look task in C. NON_MATCHING ones: Sonic `8C0B28EE`, Knuckles `8C0C8534` / `8C0C876C`,
    Amy `8C0DD490`.
* **Event motions** (`ev_motions.py`, `ev_motions2.py`, `ev_motions3.py` re-run, `distinct_bytes.py`):
  * 923 bindings in 155 of 207 overlays; 404 distinct motions; 4,597,280 B (keys + mdata + headers), largest 90,188 B.
  * 188 motions are shared between overlays. Only 17 motions are referenced from code pools, and none of them next to a
    character root.
  * Frames per binding: minimum 2, median 30, maximum 400.
* **Overlays**: 207 images, 23,703,524 B (PRS 11,634,447 B). EV0034 is 524,656 B and ends at `0x0CC00170`, past
  S_MOT's `0x0CC00000`. `?ev%04X.prs` is at `8C112AEB`. 8,861 functions, 2,007,752 B. The locks are executed in
  gameplay by `8C06EAC0` when `flag & 0x4000` is set, so M2's walk-to is feasible.
* **Audio and text**:
  * Both AFS files have 2,044 entries: 65,200,128 B (US) and 73,484,288 B (JP).
  * The music table has 110 names: 36-41 are `EVTBGM00-05`, 10 is `BOSSEVNT`, and 69 `NIGHTS_K` is missing from the disc.
  * 51 `E_*.MLT` files, 6,701,280 B in all.
  * EV0003's table at `0x0CB81D2C` holds 5 languages x 5 lines, each line starting with 0x07.
  * `1ST_READ.BIN` SHA-1 is `e74c8498...b6df`.
* **libsa1 facts**:
  * The extension segment is 16 MB, served by the bump allocator `sa1g_world_ext_alloc`.
  * At most 8 pairs run per tick.
  * The only camera input is yaw (`sa1_input.camera_yaw` -> `w->camera.ang[1]`).
  * `SA1_ERR_UNSUPPORTED` is returned for Eggman, Tikal and Super Sonic (`libsa1.h:534`).
  * Every API name the doc cites exists.
* **Census**: the current `material_census.txt` (14:43) lacks Sonic 25-27, Tails 19-20, Knuckles 2-4, Amy 9-10 and Big 2.

### R.2 Refuted (with the correct facts)

1. **"8C04B57C advances the event queue in the display pass" is wrong.** Every consequence the doc draws from it falls
   with it: 3.3's determinism note, the 4.1 row "Display pass: runs after each pair in G-SA1", the "with display" seek
   cost in 4.4, J5's "display pass on and off", risk 4, and the recommendation to "run the display pass every pair".
   * The DC never runs `8C0988C4` during play. Only the pause and frozen-state frames call it
     (`docs/research/visibility_fidelity.md` section 2).
   * libsa1 never runs it either. `sa1g_world.display` is zeroed in `sa1g_world_create`, and no code in LIB or g-sa1
     sets it.
   * `8C04B57C` runs inside the step, from the character's display, which each main calls directly: Sonic
     `code_8C0A77BC.c:962`, Tails `:612`, Knuckles `:642`, E-102 `code_8C0CBE00.c:735`, Amy `code_8C0D6B40.c:498`,
     Big `code_8C0DFFA0.c:579`, Tikal `code_0CB00000.c:206`.
   * Sonic, Tails, Knuckles and Big draw only when `8C038D00` passes. It returns 0 on sub step 1, so their queues
     advance **once per pair**.
   * Amy (`8C0DA122`), E-102 (`8C0CFACE`) and Tikal (`0CB008A0`) have no visibility test and no EC8 gate, so their
     queues advance **twice per pair**.
   * Every character skips the advance while `wtimer & 2` (the damage blink); Knuckles also skips it while `wtimer < 0`.
   * Enabling the display pass would be a fidelity bug: a third advance per pair for Amy, E-102 and Tikal, and Amy's
     hammer morph runs a third time.
2. **Events run with one logic step per frame** (strong evidence from the asm; not yet run in the harness).
   * `8C049298`, called by the event library's `8C0499C0`, calls `8C051760(2, 1)`, and `8C051760` stores its second
     argument in `B_8C754EC4` (steps per frame). `8C049560` restores `8C051760(2, 2)`.
   * With EC4 = 1, every step is sub step 0. On the DC, every character's queue therefore advances once per 60 Hz step
     during a cutscene, so an event speed of 0.5 means 0.5 frames per step, not "per display call".
   * Under libsa1's pair contract, Sonic, Tails, Knuckles and Big would play catalogue motions at half the cutscene rate,
     and their blend-ins would take twice as long. Amy, E-102 and Tikal would play at the right rate.
3. **'I' (sequence 8) is the hurt face, not the jump face.** Its callers are the damage paths:
   * Sonic `8C0AFB10`: knock-back mode 16, action 23, `wtimer = 120`.
   * Knuckles `code_8C0C5680.c:114` and Amy `code_8C0DAC46.c:185`: `spd.x = -run_speed`, `wtimer = 120`.
   * Tails: the same sequence in its own damage paths.

   So Expressive mode suppresses the hurt face. It also follows that 0.7 already shows the EV head whenever Sonic,
   Tails, Knuckles or Amy is hit.
4. **The disc has 29 `EV_*.PRS` files, not 36.**
5. **2.7 "Render" and risk 7 are mostly already handled.** `sa1_character_render` lerps the points the game changed
   between pairs, in model space (`libsa1.h:597-598`), using capture-v3 point snapshots taken at draw time
   (`rt_draw.c:416-426`). No 30 Hz pop is expected. The one case left is the single unmatched pair when the static face
   and the EV head swap: they are different models (Sonic's static face has 205 points, the EV face 222).
6. **Pre-warm, only partly true.** `cl_warm.lua` item 1 already warms every shader state, and item 3 warms every
   archive-0 texture in the body state (opaque, palette-lit).
   * What is not warmed: only the EV-head materials with IGNORE_LIGHT (attrflags `0x9621A400` / `0x9721A400`, which
     `sa1_draw_classify` turns into `SA1_DRAW_UNLIT`). These are Sonic 25-27, Tails 19-20, Knuckles 2-4 and Amy 9-10.
   * Big 2 and the lit variants are already covered.
   * The owner's 0.7 log (2,376 s) shows "0 materials made at their first use".

   Expect at most a few CreateMaterial calls, not a 0.5-style freeze. Adding them to the census is still right.
7. **The subtitle line count does not reproduce.** The overlay count does (177 with a relaxed scan, 143 with a strict
   equal-stride scan), but the lines per language come out at 912 (relaxed) and 760 (strict), not "about 1,152". State
   a range.
8. **The face-script counts do not reproduce from the saved tool.**
   * `face_scripts.py` finds 657 strings, 436 distinct, in 132 overlays.
   * These include strings that are not face scripts: STOP, RUN, CHAOS0, HU.
   * Its regex caps strings at 64 characters, which contradicts "longest 80".
   * The doc's figures (623 / 438 / 127; A 606, V 467) need the exact filter saved alongside them.
9. Tikal is female (`libsa1_integration.md` 6.3a). Change "his" to "her" in 2.7, 3.3 and M6.

### R.3 Gaps

* **G1 Face interrupt.** `8C04BBCE` only stores the script, and `8C04BCC8` reads it only after the running sequence ends.
  A hotkey therefore waits:
  * up to 0.8 s during talk 'C';
  * up to 3 s during 'e';
  * up to 2.1 s during a `SA1_FACE_HOLD {127, k}`.

  Sending STOP first snaps to the static face for one step and then blends in from key 0 (`8C04BBCE` sets
  face = face_next = 0). The API needs a defined interrupt: clear `face_seq` and set `face_count = face_time - 1`, so the
  next step blends from the current key. This is a host write, and it must be journaled.
* **G2 Event pacing** (from R.2 item 2). M2 must choose one of two options:
  * (a) EC4 = 1 for a performing world. This is faithful to the game. But `sa1g_world_pair` writes EC4 = 2 on every
    pair, 43 functions gate on EC8, and the capture and draw contract changes.
  * (b) Scale speed and linkframe for the gated characters. Not exact: linkframe is a `Uint8`.

  The EV0003 harness oracle must run the event library's EC4 = 1, and an Amy or E-102 event is needed as a second
  oracle, because their displays are ungated.
* **G3 Logic fingerprint scope.** It must cover all of libsa1's own logic that is not lowered game C:
  * rt (`8C038D00` in `rt_draw.c`, trig);
  * `sa1_coladapt_world.c` (61 KB) and `sa1_world_hostobj.c` (89 KB);
  * the stage stand-ins;
  * the SHA-1s of the overlay images in use (STGxx, S_MOT, TIKAL_PROG, EV).
* **G4 Extension memory.** The bump allocator never frees. Cache motions per world by catalogue id and set a budget.
* **G5 Collision on replay.** A COLLISION entry at step N needs the collision prepared on the worker to be ready by that
  step. Block, or prepare before play starts.
* **G6 Eggman needs more than 4,468 B.** He also calls `8C04F8FA` (start position, 576 B, asm; libsa1 replaced it for
  players) and needs a create path. Tikal needs her create path and TIKAL_PROG composition.
* **G7 Multiplayer.** Neither faces nor Director playback reach other players. Puppets use the CPU pose path, which has
  no EV-head blend. Put face state into the `sh_net.lua` stream, or defer it explicitly.
* **G8 Damage during a performance.** While `wtimer & 2` is set, actions pause and drift from the audio. Set
  `SA1_COPT_NO_DAMAGE` during performances.
* **G9 Timeline audio.** The jukebox engine keeps 1.5 s queued per stream. Seek, pause and scrub need a flush and a
  sample-accurate start.
* **G10 Big in events.** The claim that the game's events never hit Big's key trap was checked only for letters. In
  the 16 overlays with Big bindings, EV001C, EV00D3, EV00DF and EV00FF hold scripts with letters Big cannot play, but
  other characters appear in those overlays too. UNVERIFIED.
* **G11 Material that gets past the validator.** THMB (a GMod screenshot showing ripped textures) and game lines that
  the owner edited would pass the never-embed validator. Owner question.
* **G12 Super Sonic.** The face task switches the normal Sonic tree, so on the B_CHAOS7 tree faces would not show.
  State this explicitly.

### R.4 Required changes

1. Remove every "display pass" premise (3.2, 3.3, 4.1, 4.4, J5, risk 4, recommendation 6). Replace it with: "events
   advance inside the step; never enable `8C0988C4`".
2. Add event pacing (G2) to M2: make it M2's first research item, with its own gate.
3. Relabel 'I' as the hurt face (2.2, 2.3, 2.8).
4. Change "36" to "29" `EV_*.PRS`.
5. Specify the face interrupt (G1).
6. Narrow the pre-warm item to the unlit EV-head states.
7. Widen the logic fingerprint (G3); add per-world motion dedup (G4) and a collision-ready rule for replay (G5).
8. Turn the subtitle and face-script counts into reproducible ranges.
9. Re-estimate the Eggman and Tikal work (G6).
10. Add multiplayer faces (G7).
11. Raise the total from 8-9 rounds to about 10-12. M2 gains pacing work, and a Derma Director in 2 rounds is tight.

**Verdict.** The data, addresses and decompilation status hold up well. M1 (faces first) stands once the interrupt
semantics are added. M2's timing model is wrong: the display-pass premise is false, and events actually run with
EC4 = 1. M2 needs redesign before implementation.
