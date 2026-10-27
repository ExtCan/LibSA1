# G-SA1 1.0: spawnable NPCs with owner-defined text (v1 research and design)

Research and design for the owner's 1.0 roadmap item "NPCs spawnable with customizable text" (owner roadmap of
2026-10-21, `gsa1_rounds_plan.md` 14.1f). Read-only step: nothing in `SADC/src`, LIB or G-SA1 was changed. Evidence
comes from scratch tools in `SADC/build/research/npcs/` (appendix A). Every claim cites an address, a file or a
measurement. Anything not checked is marked **UNVERIFIED**.

Abbreviations: SADC = `<project>\sadc`, LIB = `...\libsa1`, G-SA1 = `...\g-sa1`, SET = the disc's
object layout files, "step" = one 60 Hz game step (libsa1 runs them in pairs), SJIS = Shift-JIS.

State read on 2026-10-21 at 14:20: G-SA1 0.7.0 (`sh_config.lua:4`), libsa1 0.2.0 API with host objects (API 0.3 section,
`libsa1.h:1411-1657`), and SADC progress 1,223 / 4,119 1ST_READ game functions with C (220,168 / 973,430 bytes,
22.62 %). The overlay images ADV00, ADV02 and ADV03 have no C yet. Other rounds are editing LIB and g-sa1 at the same
time, so line numbers can drift.

---------------------------------------------------------------------------------------------------------------------

## 0. Summary and recommendation

**How SA1 does NPC talk.** All of the findings below are new; none of them was in the code map before.

* **The townspeople.** Station Square, Mystic Ruins and the Past have townspeople. They are the SET object
  **`O PEOPLE`**: ADV00 `objexec_O_PEOPLE` `0C90F35E`, ADV02 `0C911DFE`, ADV03 `0C902F84`.
  * The SET record picks the NPC:
    * `ang.x` = model type (0-25 in Station Square);
    * `ang.y` = facing;
    * `scl.x` = draw range squared (0 means 410 units);
    * `scl.z` = message number.
  * There are **32 model types**: 26 in Station Square (`SS_PEOPLE`, `SS_EKIIN`, `SS_MIZUGI`, `SS_BURGER`), 3 explorers
    in the Mystic Ruins (`TANKEN`) and 3 echidnas in the Past (`PAST_KN_FAM`).
  * Each type has a **motion set of 7 motions**: idle, idle variants, two talk motions, walk and a flinch. It also has a
    head height and a talk radius (section 1.2).
  * The SET files place **233 of them**: 195 in Station Square, 34 in the Mystic Ruins and 4 in the Past.
  * **The Egg Carrier has no talking townspeople.** ADV0100 and ADV0130 import only the hint API, not the talk API.
* **Talking: the talk manager.** A 1ST_READ task, created by `8C10A120` at stage start (`8C04E660`), holds a list of
  talk targets registered with `8C10A146`.
  * When the player presses an **action button** (`D_8C19E3A0` = `0x602`: B, X or Y) inside a target's radius, it
    picks the nearest target. The radius is the NPC's value plus `pwp->p.rad`. The player must be in front of the NPC
    (within ±80° of its facing) and, if the target asks for it, within 10 units of its height.
  * It then sets the target's flag, **turns the controller ports off and stops the player** (`8C10A260`: `8C0509DA`,
    `8C07256A` PClearSpeed).
  * **There is no on-screen talk prompt.** The manager has no display function, and the NPC draws only its model.
* **Text: the talk files on the disc.** `SS_MES_<c>_<l>.BIN`, `MR_MES_...` and `PAST_MES_...` exist per character
  (S, M, K, A, E, B, plus L for the last story) and per language (J, E, F, S, G): **99 files, 1,169,620 bytes**.
  * The game loads them to a fixed address: `0x0CB46000` (Station Square) or `0x0CB4A000` (the other two fields).
  * Format: a count, a table pointer, then per NPC a **condition script** (story-flag tests: `8C05F26A` evaluates it)
    and **groups of pages**.
  * English holds **310 NPC entries, 727 conditional variants and 2,095 pages**. Every page has at most 2 lines and
    begins with the block-centre code `0x07`.
  * **No talk line is voiced** (the VOICE opcode never appears in a talk file). Voices are used only by the hint tables:
    pairs of page and duration plus a voice number (for example Big's "Froggy? Where are you?", voice 2012, 250
    steps).
* **The message window.** It is the game's `MSGC` text engine (`8C0548E0-8C055D20`, 40 functions, 5,078 bytes):
  * 24 x 24 1-bit glyphs, read from **fonts stored in 1ST_READ**: Kanji `0x8C112B84` (3,423 glyphs: JIS row 1-2,
    katakana and all 2,965 level-1 kanji), European ASCII `0x8C14EE3C` and US ASCII `0x8C152D3C`, with width tables;
  * the text is typeset into a **512 x 64 ARGB4444 texture**;
  * the window is a **full-width translucent black band** (y 380-440) with the text quad at (70, 384);
  * the window is driven by **coroutines**: the talk runner `8C05F902` and the voiced hint runner `8C05F860`, both on
    the thread system `8C099A00-8C099DA0`. They fade the window in over 10 + 10 steps, swap pages over 7 + 7 steps and
    wait for A, B, X or Y. The thread switching is assembly-level, and **libsa1 worlds skip it today**
    (`sa1_world_tick.c:172`).
  * I rendered pages from the disc's fonts with a re-implementation of the engine. English, Japanese and German come
    out right (appendix A), so the glyph format is **verified**.
* **Decompilation status: 0 bytes of all of this have C.** About 16 KB in about 150 functions is needed for 1.0
  (section 1.8).

**Recommended design** (section 3):

1. **Text engine first.** Decompile the MSG engine (40 functions, 5,078 bytes). Expose it as a small libsa1 text API,
   `sa1_text_*`, that runs **the game's own typesetting code** in a service world:
   * render a page to a 512 x 64 bitmap;
   * measure lines with the game's width table;
   * convert UTF-8 into the game's encodings (ASCII, Latin-1, SJIS) and report characters the fonts lack;
   * paginate owner text into the game's 2-line pages;
   * list the game's own NPC lines from the disc talk files, for an "import a line from the game" browser.

   G-SA1's text editor uses it for exact previews.
2. **NPCs run as the game's own `O PEOPLE` task in a per-field "NPC world"**, starting with an ADV00 world for the 26
   Station Square types. Decompile the ADV00 people code: 44 functions, 6,160 bytes.
   * Owner text is compiled into **the game's own talk-file format** in that world. Each NPC is one entry with no
     condition, which `8C05F26A` accepts.
   * A talk is run by the game's talk runner (`8C05F1C4` / `8C05F902`), or for a voiced NPC by the hint runner
     (`8C05F196` / `8C05F860`), with the game's own fades, page swaps and button waits.
   * This needs a **coroutine shim in libsa1's runtime**: host fibers replace only the three SH-4 context-switch
     primitives, and the thread list code is decompiled. The shim is also the prerequisite for running the game's
     EVENT scripts in 2.0 (see `v1_machinima.md`).
3. **Talking.**
   * **G-SA1 characters** talk through **the game's talk manager inside their own world**. A new host-object kind,
     `TALKABLE`, registers each NPC entity there through `8C10A146`. The game decides when a press starts a talk, then
     freezes and stops the character. G-SA1 receives `SA1_HOSTOBJ_EVENT_TALK`, starts the NPC's talk in the NPC world,
     and calls `sa1_character_talk_end` at the end. That call runs the game's `8C10A1C6` and the look-at stop
     `8C07C376`.
   * **Plain Garry's Mod players** talk with **+use (E)**.
   * Only the talker sees the text, as in the game. An option shows it to everyone nearby.
4. **The window on screen.** The NPC world's `MSG_Disp` draws two primitives: an untextured band and a quad that uses a
   dynamic texture. They are captured as **2D primitives**: a new capture of the 2D polygon function 8C63EBC8 plus
   memory textures. This must be coordinated with the HUD round's 2D capture (`gsa1_070_hud.md`). G-SA1's HUD layer
   draws them on the 640 x 480 canvas, scaled to the screen. The band can stretch to the full width at 21:9.
5. **Fallback** if the coroutine shim or the polygon capture is late, and the only window for clients without the disc:
   G-SA1 draws the window itself, from the `sa1_text_render` bitmap and the constants in section 1.6. That port of the
   game's window timing is checked against a harness reference.
6. **Q menu.**
   * NPCs category in the **G-SA1 creation tab** (`v1_arch.md`): townspeople by field, with icons rendered at run time.
   * **NPC text tool**: click an NPC to edit its pages, preview, voice, page timing and alignment, or import a line.
   * **Options > G-SA1 > NPCs**: font and language, window scale, crisp or smooth text, show text of voiced lines,
     talk key for plain players, freeze plain players, optional prompt, and draw range.
7. **Phase 2.**
   * Mystic Ruins explorers (ADV02 people: 36 functions, 5,374 bytes) and Past echidnas (ADV03 people: 45 functions,
     6,158 bytes).
   * Walkers along splines from `v1_triggers`, using the game's walker code.
   * The game's yes/no choice window (`8C055460`) for branching talk.
   * Cast NPCs (Sonic, Tails and the others) with event motions, together with the 2.0 machinima work.

---------------------------------------------------------------------------------------------------------------------

## 1. How SA1 does it (research)

### 1.1 Where it lives

| Component | Image and range | Size | C today | Evidence |
|---|---|---|---|---|
| MSG text engine: open, clear, put, glyph, width, centre, display, texture queue, close | 1ST_READ `8C0548E0-8C055460` + `8C055B80-8C055D20` | 26 functions, 3,424 B | none | Ghidra pseudo-C `build/ghidra/1ST_READ/decomp/8C0548F4.c` ... `8C055CDE.c`; asm `asm/1st_read/text/8C0548E0.src` |
| MSG prefab windows: choice menu, auto-advancing lists, language override | 1ST_READ `8C055460-8C055B80` | 14 functions, 1,654 B | none | `8C055460.c` (up/down choice), `8C0557E0.c`, `8C055888.c`, `8C055954.c`, `8C055A2E.c` |
| Talk and hint API, condition-script evaluator, runners, window choreography | 1ST_READ `8C05F000-8C05FCFC` | 31 functions, 2,986 B | none | `8C05F022`, `8C05F036`, `8C05F04A`, `8C05F140`, `8C05F196`, `8C05F1C4`, `8C05F26A`, `8C05F860`, `8C05F902`, `8C05FB0A`, ... |
| Talk manager (talk-target list and pick) | 1ST_READ `8C10A120-8C10A4E0` | 8 functions, 888 B | none | `8C10A146` register, `8C10A1E0` unregister, `8C10A28E` pick, `8C10A260` start, `8C10A1C6` end |
| Thread system (coroutines) | 1ST_READ `8C099A00-8C099DA0` | 23 functions, 852 B | none | `8C099A50` yield, `8C099C0A` frame init, `8C099C3C` create (0x1000-byte stacks), `8C099D2A` work area |
| Voice and music control | 1ST_READ `8C107800-8C107E40` | 25 functions, 1,060 B | none (libsa1's audio layer turns its calls into events) | `8C107BA0` play voice, `8C107BB0` stop, `8C107BC2` status |
| Fonts and width tables | 1ST_READ data `0x8C112B84-0x8C15497C`, font table `0x8C15497C` | 270,840 B | (data) | section 1.7 |
| Station Square townspeople: SET object, walkers, talk glue | ADV00 `0C90F2C0-0C910D60` | 44 functions, 6,160 B | none | Ghidra run on ADV00 (appendix A) |
| Station Square hint glue | ADV00 `0C910D60-0C911212` | 8 functions, 2,424 B | none | |
| Mystic Ruins townspeople | ADV02 `0C911D60-0C913320` | 36 functions, 5,374 B | none | `objexec_O_PEOPLE` `0C911DFE` |
| Past townspeople | ADV03 `0C902F00-0C9049A0` | 45 functions, 6,158 B | none | `objexec_O_PEOPLE` `0C902F84` |
| Talk files | disc `SONICADV/{SS,MR,PAST}_MES_<c>_<l>.BIN` | 99 files, 1,169,620 B | (data) | section 1.4 |
| NPC textures | `SS_PEOPLE.PRS` (86,822 B), `SS_EKIIN` (14,238), `SS_MIZUGI` (9,841), `SS_BURGER` (25,006), `TANKEN` (76,905), `PAST_KN_FAM` (17,703) | | (data) | stage texture list `0x8C1C2070` and on (1ST_READ); ADV02 `0x0C902858`, ADV03 `0x0C900224` |
| Walker paths | `SS_PATH_C/O/S.BIN` (620, 1,180, 1,320 B), `PAST_PATH.BIN` (1,440 B) | | (data) | name table ADV00 `0x0CB32EBC` |

The code map (`sa1_code_map.csv`) had `8C0548E0-8C055D20` as "PVR texture sprite helpers / 2D drawing (low)". It is
the message engine. The row `8C0584A0` "NPC/AI hooks ('NPC [%s]')" concerns AI-controlled players (the Tails AI
`8C081AC0`, race opponents), not townspeople.

Callers, from `build/1ST_READ.xrefs.json` and the overlays' `*.imports.txt`:

* ADV00, ADV02 and ADV03 import the talk API: `8C05F022` sets the talk table, `8C05F140` reads a talk entry,
  `8C05F01C` sets the done flag, and `8C05F1C4` is reached through `8C05F140`'s callers. They also import the hint API
  (`8C05F036`, `8C05F04A`, `8C05F196`) and the talk manager (`8C10A146`, `8C10A1C6`, `8C10A1E0`).
* ADV0100 and ADV0130 (Egg Carrier) import only the hint API and the talk manager. They have no talk table, and no
  `O PEOPLE` in their object lists.
* `8C055878` and the other prefab windows are used by events, the menus (ADVERTISE, SUMMARY), STG03 and STG06.
* In 1ST_READ, `8C061198` (the Tikal hint orb family next to `objexec_O_TIKAL` `8C060460`) also registers with the
  talk manager.

### 1.2 The townspeople object (`O PEOPLE`)

**SET record → behaviour.** The edit display `0C90F6CA` labels these fields: "< MODEL INDEX", "< ROTATE Y",
"< Range Out(SQR)", "< Message ID = %d". `objexec_O_PEOPLE` (ADV00 `0C90F35E`) does the following:

* if `scl.z` is below the entry count of the loaded talk file (`*(int *)0x0CB46000`), it creates the talker
  (`0C91043E`):
  * reads the message with `8C05F140(scl.z, buf)`;
  * sets the model and motion set by type (`0C910540`: `ang.x` clamped to 0-25);
  * `CCL_Init(tp, set + 0x44, 4, 1, 1)`: the NPC is solid;
  * registers with the talk manager: `8C10A146(set[0x58] + 20.0, tp, &pos, &ang.y, &twp->timer, 2)`;
* otherwise, only in debug mode (`B_8C19B468`), it draws an editor model;
* a draw range below 0.01 becomes 168,100 (410²).

Per frame (`0C910900`), it does the following:

* reads the distance to player 0 (`8C07CE82` / `8C085A36`) into `twp+0x1C`;
* calls `0C9107A0`, which starts a talk when the talk manager has set `twp->timer`. A spin hit with player flag
  `0x400` instead plays the flinch motion 6;
* steps the motion through a per-mode table (`0x0CB32F5C`: `0C910588` idle with random variants, `0C91064E` talk,
  `0C910740` one-shot, then back; these roles are **UNVERIFIED**);
* draws only within range (`0C910AC0`), plus the blob shadow (`0C910CC0` → `8C03700C`). It animates only when the
  player is within 150 units (`0C910588` sets `work[0xC] = distance² < 22,500`). Farther away, `0C910AC0` draws the
  model without motion (`8C036F12`), a level of detail.

**Talk start (`0C9107A0`).**

1. Play talk motion 3 or 4 (`rand() & 1`).
2. `8C05F140(message, buf)` reads the message, `8C05F01C(&twp->btimer)` sets the done-flag pointer, and
   `8C05F1C4(buf)` starts the runner.
3. `8C07C318(0, head)` turns the **player's** head to the NPC's head. The head is the NPC position plus the motion
   set's head height at `+0x3C`.
4. `twp->timer = 0`.

When the runner ends, it clears `btimer` (`8C05FB74`). The talk motion function `0C91064E` then calls `8C10A1C6`, which
turns the controller ports back on, and `8C07C376(0)`, which stops the look-at. It then picks a random idle.

**Townspeople never turn.** Only the walker code writes the heading (`twp+0x18` in `0C90FA70`); the standing talker
keeps the SET facing. The talk-manager facing test therefore matters: you must stand in front of them.

**Model types** (ADV00 table `0x0CB32CCC`, 12-byte entries `{NJS_OBJECT *, NJS_TEXLIST *, motion set}`; names from
SA Tools labels in `reference/dc_sa1_data_labels.csv`):

| Type | Model (SA Tools name) | Texture archive | Set |
|---|---|---|---|
| 0-3 | Man A Blue Suit, Man B Brown Suit Moustache, Man C Red Suit with Hat, Man D Blue Suit with Hat Moustache | SS_PEOPLE | 0 |
| 4 | Man A Station Attendant | SS_EKIIN | 0 |
| 5 | Man B Burger Shop Uniform | SS_BURGER | 0 |
| 6-9 | Woman A-D (dresses, miniskirt) | SS_PEOPLE | 1 |
| 10-12 | Woman C / B / A swimsuits | SS_MIZUGI | 2 |
| 13 | Woman D Burger Shop Uniform | SS_BURGER | 1 |
| 14-17 | Girl A-D | SS_PEOPLE | 3 |
| 18-21 | Fat Man A-D | SS_PEOPLE | 4 |
| 22-25 | Boy B, A, C, D | SS_PEOPLE | 5 |
| MR 0-2 | Jungle Explorer A (tall, binoculars), C (short, beard), D (short, glasses) | TANKEN (9 textures) | 0, 1, 1 |
| Past 0-2 | Echidna Normal, Big, Tall | PAST_KN_FAM (15 textures) | 0, 1, 2 |

**Motion sets** (ADV00 `0x0CB2C1C4 + set * 0x78`). Each set has 7 pairs of `{NJS_MOTION *, speed}`, then parameters:

* `+0x38`: a float. It is 16.4 for set 0. This is **UNVERIFIED**: it could be the head-height figure that sets 1-5
  give as 15.6, 15.6, 9.6, 13.8 and 9.8.
* `+0x3C`: the head height used for the look-at.
* `+0x44`: the CCL info.
* `+0x58`: the talk radius base: 7, 6, 6, 4, 5 and 4 for sets 0-5. The talk radius is this plus 20.

The motion frame counts were read from the `NJS_MOTION` headers:

| Set | 0 idle | 1 | 2 | 3 talk | 4 talk | 5 walk | 6 flinch |
|---|---|---|---|---|---|---|---|
| 0 men | 25 | 25 | 25 | 15 | 15 | 32 | 40 |
| 1, 2 women | 26 | 26 | 24 | 17 | 17 | 32 | 39 |
| 3 girls | 15 | 20 | 20 | 15 | 15 | 32 | 41 |
| 4 fat men | 33 | 32 | 32 | 40 | 32 | 32 | 40 |
| 5 boys | 16 | 10 | 10 | 29 | 29 | 32 | 41 |

The roles are inferred from the code: motion `(rand & 1) + 3` while talking, motion 5 for walkers (`0C90FD38` creates
walkers with mode 5), and mode 6 for the spin-hit flinch. The labels "idle" for 0-2 and "flinch" for 6 are
**UNVERIFIED**.

**Placements in the game** (SET files, `build/research/npcs`, object index 88 / 60 / 38):

* Station Square: 195 across 30 SET files, using 19 of the 26 types.
* Mystic Ruins: 34, using types 0-2.
* Past: 4, using type 0.

**Walkers.** `SS_PATH_<act>.BIN` path records (20-byte points) are loaded per act by `0C90F49E`. Walkers follow them
(`0C90FA70` interpolates and sets the heading) and can be talked to like standing NPCs: `0C90FD38` calls `0C91043E`. The
record layout is **UNVERIFIED** beyond the 20-byte stride and the start, model and message bytes read in `0C90FD38`.

### 1.3 Talk interaction: the talk manager

`8C10A120` is called by the stage start `8C04E660`. It creates the task `8C10A28E`, with a list head at
`D_8C5FBAC0`, an active entry at `D_8C5FBAC4`, and no display function.

Register (`8C10A146`): `(float radius, key, NJS_POINT3 *pos, Angle *ang, int *flag, flags)`. This creates or updates a
32-byte node: `+8` key, `+0xC` pos, `+0x10` ang, `+0x14` flag, `+0x18` radius, `+0x1C` flags. `8C10A1E0` removes the
node.

Pick (`8C10A28E`, asm `asm/1st_read/text/8C10A120.src`). It runs every step, and only if:

* the list is not empty;
* `per[0].press (B_8C754C08 + 0x10) & D_8C19E3A0` is set (`0x602` = B|X|Y);
* no talk is active.

Then:

* `best = 10000.0` (100²). `r0 = pwp->p.rad` (`player.h:254`, offset 0x10C), or 0 with no player.
* For each node, with `d = player.pos - node.pos`:
  * a node without a position becomes the default candidate if none was found yet;
  * `a = 8C085A50(d.x, d.z)` (an atan2-style BAMS angle);
  * **player facing test**, only with flag 4: `|short(player.ang.y + a + 0x4000)| < 7281` (±40°);
  * **NPC facing test**, when an angle pointer is given: `|short(a - *ang + 0x4000)| < 14563` (±80°);
  * **height test**, only with flag 2: `|d.y| < 10.0`;
  * the node is taken if `d.x² + d.z² < (radius + r0)²` and the distance is below `best`.
* For the chosen node, `8C10A260` runs:
  * `*flag = 1` and `D_8C5FBAC4 = node`;
  * `8C0509DA(0xFF)` turns both controller ports off, and `8C050BE4` runs a pad update. With a port off, the pad update
    clears `per[]` (`follower_ai.md` 1.5);
  * `8C07256A(mwp, pwp)` (PClearSpeed) stops the player.
* End (`8C10A1C6`): `*active->flag = 0`, then `8C0509B8(0xFF)` turns the ports back on.

`O PEOPLE` passes flags 2 and its `ang.y`, so the talk rule for a man is:

* stand within `7 + 20 + p.rad` units horizontally (about 31 SA1 units, 124 Hammer units, for Sonic, with `p.rad` 4.0:
  `player.h:70`);
* stand within 10 units vertically;
* stand within ±80° in front of him;
* press B, X or Y.

The facing direction and sign of the ±80° test are read from the code. The sign convention is **UNVERIFIED**.

**Page advance reads the raw pad.** The runner's wait `8C05F7E0` tests `(D_8C19E390 | D_8C19E3A0) &
B_8C754DA8[0]->press` (`0x4 | 0x602` = A, B, X or Y). `B_8C754DA8[0]` is the raw peripheral record, not `per[]`, so
pages advance while the ports are off and the player is frozen.

**No prompt.** `8C10A28E` draws nothing. `O PEOPLE`'s display (`0C910AC0`) draws only the model and its shadow. No
other display in the ADV00 people code was found. So SA1 has no "talk" prompt; players learn to press B.

### 1.4 Talk text: the `*_MES_*.BIN` files

**Loading.** `0C90F2C0` (ADV00), `0C911D60` (ADV02) and `0C902F00` (ADV03) do the following:

* pick a file name by language (`B_8C754BFC`: 0 J, 1 E, 2 F, 3 S, 4 G) and character (`GetPlayerNumber` `8C04F698`:
  S, L, M, K, -, A, E, B). The Last story (`B_8C799978`) uses `L`. The name table is at ADV00 `0x0CB2C494`;
* load the file with `8C09DA40` to `0x0CB46000` (Station Square) or `0x0CB4A000` (Mystic Ruins, Past);
* call `8C05F022(*(0x...004))` to set the talk table, and `8C05F036(hint table[language])` (ADV00 `0x0CB2C534`) to set
  the hint table.

**Format** (`mes_dump.py`; every one of the 99 files parses):

```
+0  s32 count                       (entries; O PEOPLE compares scl.z against it)
+4  u32 table                       (absolute pointer for the load address)
table[count]: { s16 *condition; char **messages; }      8 bytes per entry
condition: s16 opcodes, read by 8C05F26A:
   -7 f... (NPC flag list, 8C05F458 on B_8C799920[]), -6 f... (event flag list, 8C049A5A)
        a plain flag requires "flag clear"; 0x4000|f requires "flag set"; a list ends at the next negative value;
        within one list the items are ORed (any hit passes); consecutive opcodes are ANDed
   -5 m (character bit mask, 1 << GetPlayerNumber), -4 v (voice number), -3 f (flag to set after the talk)
   -2 (next alternative = next message group), -1 (end)
messages: groups one after another; talk files: char *page[] ending with NULL;
          hint tables: { char *page; s32 steps; }[] ending with NULL (steps 0 = wait for a button)
condition NULL: 8C05F26A returns the first group directly
```

**Totals** (`mes_stats.py`):

| Field | Language | Files | NPC entries | Groups | Pages | Bytes |
|---|---|---|---|---|---|---|
| Station Square | each of E/F/S/G/J | 7 | 219 | 494 | 1,376 E, 1,374 F, 1,375 S, 1,376 G, 1,171 J | 128,728-163,268 |
| Mystic Ruins | E/F/S/J (G: 6 files) | 7 (6) | 61 (G 50) | 192 (G 163) | 565 E | 51,848-78,720 |
| Past | each | 6 (no L) | 30 | 41 | 154 E | 13,724-15,168 |

Opcode counts over all files: `-7` 4,228, `-2` 2,067, `-6` 1,902, `-3` 45, `-5` 5, `-4` **0**. Talk lines are never
voiced.

Quirks:

* `MR_MES_K_G.BIN` is **missing from the disc**. In German, Knuckles' Mystic Ruins people would get a table of 0 and
  stay silent. This is **UNVERIFIED in the game**; it follows from `0C911D60`'s `-1` path.
* `PAST_MES_M_S.BIN`'s last page list runs to the end of the file without its NULL. The game reads one word past the
  file in its load buffer.

**Typical page** (`SS_MES_S_E` entry 0): `"\x07I'm Mr. Know-It-All. \nHere's a clue..."`. Measured over every page with
the game's widths (`msg_render.py`):

* at most **2 lines** per page;
* the widest line is 438 px in English, 481 px in Japanese and 496 px in German, against a window width of 500.

### 1.5 Runners, coroutines and the window choreography

**Talk API.**

* `8C05F1C4({pages, flag})`: talk, with no voice. It runs `8C05FB0A(8C05F902, pages, 0)`, then sets flag `f` (`8C05F240`).
* `8C05F196({pairs, voice, flag})`: voiced hint. If `voice >= 0` it calls `8C107BA0(voice)` and runs
  `8C05FB0A(8C05F860, pairs, has_voice)`.
* `8C05F000` / `8C05F00C`: pages advance every 60 steps, or every N steps.
* `8C05F210`: abort, through `D_8C1BA1E4 = 1` and voice stop `8C107BB0`.

**Starting a runner.** `8C05FB0A` creates a task, `8C05FC00`. When the game is not paused (`B_8C754EC8 == 0`), that
task starts a **thread** with `8C099C86(runner, arg, 0)` and the destructor `8C05FB74`. The destructor closes the window
(`8C055C22`) and clears the done flag that `8C05F01C` set. At most one message runs at a time (`D_8C78C50C`).

**Runner steps.**

* Each runner calls `8C04C688` at the start (pause off: `D_8C19DE26 = 0`) and `8C04C680` at the end.
* For each page it calls `8C05F70A`:
  * on the first page it opens `D_8C78C518` with `NH_MSG_Open(x 70, y 384, w 500, h 52, index 100, buffer
    0x8C87E900)` (`8C055794` → `8C055B80`) and runs the open fade;
  * on later pages it runs the page swap.
* It then waits (`8C05F7E0`) for a button, or for N steps.

**Threads.** `8C099C3C` allocates a stack from the game heap: 0x1000 bytes by default, with the work area at `+0x24`,
destructor `+0x18`, return value `+0x20`, and child and sibling links. `8C099A50` saves the callee-saved registers on
that stack and switches to the next thread. `8C099C0A` builds the initial frame: return address `8C099AAC`, GBR,
MACH, MACL and fr12-fr15, with the saved stack pointer at thread `+0x10`.

**The game step `8C04E714` calls `8C099A50` once per step**, so threads advance at 60 Hz. libsa1's step leaves that
call out.

Choreography (from `8C05F520`, `8C05F5B0`, `8C05F660`, `8C05F696` and `8C05F70A`; each iteration is one step that
draws `MSG_Disp` and yields; window alpha = `MSGC+0x1F`, text alpha = `MSGC+0x1B`):

| Phase | Steps | What changes |
|---|---|---|
| Open | 10 + 10 | window alpha 0 → 120 in steps of +12, then set to 128; text alpha 0 → 240 in steps of +24, then 255 |
| Page swap | 7 + 7 | text alpha −32 x 7, then clear, render and queue the upload; then +32 x 7 |
| Wait | until a button or N steps | talk: A/B/X/Y on the raw pad; hint pairs: the page's step count (0 = button) |
| Close | 10 + 10 + 2 | text alpha −24 x 10, then 0; window alpha −12 x 10, then 0; 2 more steps drawn |

**Two fidelity notes from the asm.** Both are to be confirmed by the harness reference in work item W11.

* `MSG_Disp` (`8C054A00`) writes the text colour `MSGC+0x18` into the text quad's vertex colours and then overwrites
  all four with the constants `0xEEDDDDEE` / `0xEED0D0E0` (`8C054B58-8C054B72`). The text alpha therefore only switches
  the text on (alpha not 0) or off. **The text pops in and out while the band fades.**
* A new page's texture is uploaded at frame end (`8C051790` → `8C055C4C`, at most 8 per frame), so it shows from the
  next frame pair.

**Voiced lines and subtitles.** With a voice, `8C05F70A` sets `MSGC.kind = 1`. `MSG_Disp` skips a kind-1 window while
`B_8C788C10` is 0. That flag is initialised from the save's option bit (`8C04C98E`: `D_8C798BF1 & 2`) and toggled with
Y during events (`8C0491F0`). In other words, it is the game's "show text for voiced messages" switch.

### 1.6 The message window (`MSGC` and `MSG_Disp`)

**`MSGC`** (`8C0548F4` MSG_Open with a heap buffer; `8C055B80` NH variant with a caller buffer, 32-byte aligned):

| Offset | Field | Default |
|---|---|---|
| +0 | u8 kind (1 = voiced) | 0 |
| +2, +4 | s16 x, y | 70, 384 |
| +6, +8 | s16 width, height | 500, 52 |
| +0A, +0C | u16 texture w, h (powers of two, at least 8) | 512, 64 |
| +0E, +10 | s16 cursor x, y | 0 |
| +12, +14 | s16 character spacing, line spacing | 2, 4 |
| +16 | u16 glyph colour (ARGB4444) | 0xFFFF |
| +18 | u32 text colour; its alpha byte `+1B` gates the text | 0xFFFFFFFF |
| +1C | u32 window colour; alpha `+1F` | 0 |
| +20 | u16 *buffer | |
| +24 | texture global index | 100 / 101 |
| +28 | texture handle (texlist), set by the upload queue | |
| +2C, +30 | texlist count (1) and its texture name (12 bytes), filled by the upload queue `8C055C4C` | (structure size 0x3C: the queue's stride) |

**Typesetting (the game code to run).**

* `8C054F2E` puts text. A first byte `0x09` centres each line (`8C055050`). A first byte `0x07` centres the block: all
  lines start at `(width - widest) / 2` (`8C0550FC`).
* Bytes in `[lo, hi]` of the language row are SJIS lead bytes. That range is 0x80-0xFF for Japanese and English and
  none for French, Spanish and German.
* `8C054D6C` puts one character:
  * `0x0A` starts a new line (`y += line spacing + 24`);
  * a character that does not fit the **texture** width also wraps (mid-word). The game's authors broke every line
    themselves;
  * the glyph is copied as 24 rows x 3 bytes, least significant bit first, into the 16-bit buffer (1 = glyph colour,
    0 = 0);
  * then `x += spacing + width`.
* `8C054D08` gives the width: `widths[c - 0x20] + 1`, or 24 for a double-byte character.
* `8C054BDC` gives the glyph address: `ascii + (c - 0x20) * 0x48`, or the SJIS index below. **Neither function checks
  the range.**

**Display** (`8C054A00`, asm with the literal pool at `8C054C40-8C054C78`). It draws nothing when the game is paused
(`B_8C754EC8`), or for a kind-1 window when subtitles are off.

* **Band**, when the window alpha is not 0:
  * `njColorBlendingMode(src SRCALPHA 8, dst INVSRCALPHA 6)`;
  * `8C63EBC8` (a 2D polygon draw: `(NJS_POINT2COL *, n, pri, attr)`; the name is **UNVERIFIED**) with 4 points:
    `(0, y-4) (0, y+h+4) (640, y+h+4) (640, y-4)`, that is **x 0-640, y 380-440**;
  * all colours = the window colour (black, alpha up to 128), pri −1.2, attr `0x60`.
* **Text**, when the text alpha is not 0 and the texture is loaded:
  * `njSetTexture(handle)` and `njSetTextureNum(0)`;
  * a textured quad from (x, y) to (x + 512, y + 64), that is **(70, 384)-(582, 448)**;
  * UVs `(0,0) (0,256) (256,256) (256,0)` from `D_8C1549E0`;
  * vertex colours `0xEEDDDDEE`, `0xEED0D0E0`, `0xEEDDDDEE`, `0xEED0D0E0` (top-left, bottom-left, bottom-right,
    top-right). That is a slightly bluish white at 93 % (alpha 0xEE), shaded diagonally;
  * attr `0x80000060` (textured, transparent).
* The texture upload (`8C055C4C`): `8C608914(info, buffer, 0x900, w, h)` and `8C608922(name, info, index,
  0x40800000)`, then a texlist load `8C608C82`. Format `0x900` is ARGB4444 rectangle (**UNVERIFIED** decoding of the
  bits).

### 1.7 The font (1ST_READ data)

Font table `0x8C15497C`: 5 rows (one per language) x 0x14 bytes `{u8 *remap; s8 *widths; u8 *ascii; u8 *kanji;
u8 lo, hi;}`. The rows were read from the image:

| Row (language) | widths | ASCII font | Kanji | lo-hi |
|---|---|---|---|---|
| 0 Japanese, 1 English | `0x8C15483C` (96 B) | `0x8C152D3C` "ASCII24E" (0x1B00 = 96 glyphs, 0x20-0x7F) | `0x8C112B84` | 0x80-0xFF |
| 2 French, 3 Spanish, 4 German | `0x8C15489C` (224 B) | `0x8C14EE3C` "ASCII24S" (0x3F00 = 224 glyphs, 0x20-0xFF, Latin-1) | `0x8C112B84` | none |

* The data is contiguous: Kanji `0x8C112B84 + 0x3C2B8 = 0x8C14EE3C`, then ASCII24S, then ASCII24E to `0x8C15483C`, then
  the width tables to the font table.
* The SA Tools label puts ASCII24S at `0x8C14EE39`. That is 3 bytes early: the table points at `0x8C14EE3C`.
* Every glyph is 24 x 24 at 1 bit per pixel: 72 bytes, rows of 3 bytes, least significant bit = leftmost pixel.

**SJIS mapping** (`8C054BDC`):

```
i = (lo < 0x7F ? lo - 0x40 : lo - 0x41) + (hi - 0x81) * 0xBC
i < 0x582 ? (i > 0x119 ? i - 4 : i) : i - 0x3B8
```

* The 0x3C2B8-byte Kanji font has exactly **3,423 glyphs**: 458 non-kanji (row 0x81 symbols, row 0x82 full-width
  digits, Latin letters and hiragana, and the 86 katakana of row 0x83) plus **2,965 = all of JIS level 1** (`0x889F` →
  index 458).
* Greek, Cyrillic, box drawing, level-2 kanji and half-width katakana are absent. Because nothing checks the range,
  they would read other data. **Host text must be filtered.**

**Verified by rendering.** `msg_render.py` re-implements `8C054BDC`, `8C054D08`, `8C054D6C`, `8C0550FC` and `8C055050`
over `baserom/1ST_READ.BIN` and writes the 512 x 64 buffer. Four samples:

* `en.png`: "I'm Mr. Know-It-All. / Here's a clue..." in the US font;
* `jp.png`: the same NPC in Japanese, with kana and kanji;
* `de.png`: "Größe über Straße / Ça va? ¿Qué? ñ" in the European font;
* `en_s.png`: the English page in the European font, a nearly identical style.

All four come out legible and correctly placed. Over every page of three talk files (`SS_MES_S_E`, `_J`, `_G`), the
number of glyph runs per page (what G-SA1's texture painter would paint) averages 592-664, with a maximum of 1,278.

### 1.8 What has to be decompiled, and what exists

All bytes below are still to decompile. The dependencies are listed by status: C = matched in `SADC/src`, NM = C
exists only as functionally verified NON_MATCHING, - = asm only.

| Unit | Functions | Bytes | Needed for | Notes |
|---|---|---|---|---|
| MSG core `8C0548E0-8C055460`, `8C055B80-8C055D20` | 26 | 3,424 | text API, window | leaf C, no threads; the display `8C054A00` uses the 2D polygon draw and textures |
| MSG prefab windows `8C055460-8C055B80` | 14 | 1,654 | phase 2 (choice window) | threads |
| Talk and hint API + runners `8C05F000-8C05FCFC` | 31 | 2,986 | NPC world talk | threads; uses `8C04C680`/`688` (asm, 8 B each), voice `8C107BA0` |
| Talk manager `8C10A120-8C10A4E0` | 8 | 888 | G-SA1 characters talking | deps: `8C0509B8`/`8C0509DA` C, `8C050BE4` NM, `8C07256A` C, `8C07CFDE` C, `8C07CE82` C, `8C085A50` NM |
| Thread lists `8C099A00-8C099DA0` | 23 | 852 | runners (and 2.0 EVENT scripts) | plus 3 primitives replaced in rt (section 3.4) |
| ADV00 people `0C90F2C0-0C910D60` | 44 | 6,160 | Station Square NPC types | first C for any ADV image; deps: `8C049A4E` (12 B, -), `8C042DC4` (4 B, -), `8C038CEC` (14 B, -), `8C03700C` (124 B, -), `8C0344A0` C, `CCL_Init` C, `8C0409C0` C, `8C041D68` NM, `8C07C318`/`C376` C, `rand` 8C10D334 (-) |
| ADV02 people `0C911D60-0C913320` | 36 | 5,374 | phase 2 (explorers) | |
| ADV03 people `0C902F00-0C9049A0` | 45 | 6,158 | phase 2 (echidnas) | **split bug**: `build/ADV03.manifest.json` marks `0x0C9040E4-0x0C90C0E4` as data although functions run to `0x0C904E7C` (the xrefs list them), so it must be re-split first |

1.0 scope: MSG core and prefabs (5,078 B), talk API (2,986 B), talk manager (888 B), thread lists (852 B) and ADV00
people (6,160 B), plus about 160 B of small dependencies. That is **about 16.1 KB in about 152 functions**. Phase 2 adds
11.5 KB in 81 functions. The overlays as a whole are ADV00 499 functions / 67,446 B, ADV02 381 / 72,436 and ADV03 136 /
16,080, all asm today (`status.py` over the overlays' xrefs).

---------------------------------------------------------------------------------------------------------------------

## 2. What libsa1 and G-SA1 have today

* **Worlds.** One guest-arena world per character, 16 MB DC layout. The step `sa1g_step_game` reproduces `8C04E714`
  **without** the coroutine switch `8C099A50` (`sa1_world_tick.c:20, 172`). The display pass runs once per pair. Stage
  overlays are lowered per image with their own code maps (STG01, STG05, B_CHAOS7 and TIKAL_PROG have C). No ADV image
  has C or a code map yet.
* **Host objects** (API 0.3, `libsa1.h:1411-1657`; design `libsa1_interaction.md` 2.1-2.6): ENEMY, TARGET, BREAKABLE
  and HAZARD proxies in every character world, plus events (`sa1_poll_hostobj_events`). HOLDABLE and BALLOON are
  planned. A TALKABLE kind fits the same model (section 3.3).
* **Capture.** `sa1_capture.h:74-86` records `njDrawSprite2D` (SPRITE2D), 3D sprites, lines and 3D polygons, and
  models. There is **no record for 8C63EBC8** (2D polygons) and **no memory textures**: `rt_texture.c` 8C608C82
  "nothing was uploaded". The HUD diagnosis (`gsa1_070_hud.md`, being written; probe
  `build/research/hud/hud_probe.c`) already pulls SPRITE2D records of the HUD pass out of worlds.
* **Audio.** `sa1_audio_mixer_cue` with `SA1_AUDIO_CUE_VOICE` plays entry `index` of `EVENT_ADX_US.AFS` (2,044 clips),
  positioned and limited by distance (`libsa1_audio.h`). The game's voice requests `8C107BA0` arrive as events.
* **G-SA1.**
  * The feature seam (`gsa1_feature.hpp`): a feature has its own `bind_<f>.cpp`, `core/<f>.*`, `feat/{sh,cl,sv}_<f>.lua`
    and tests.
  * Textures are painted into render targets run by run (`cl_textures.lua:1-60`), because GLua cannot create a texture
    from bytes.
  * The Q menu has Options > G-SA1 pages (`cl_menu.lua:363-369`). The G-SA1 creation tab is designed in `v1_arch.md`.
  * There is no HUD layer yet (`gsa1_070_hud.md`).
  * Units: 4 Hammer units per SA1 unit (`sh_config.lua:6`).

---------------------------------------------------------------------------------------------------------------------

## 3. Design

### 3.1 Overview

```
 server (authority)                         each client with G-SA1 + disc
 ------------------                         ---------------------------------------------------------------
 gsa1_npc entity  --net: model, pos,  --->  libsa1: NPC world "ADV00" (one per field in use)
  pages, voice,       pages (compressed)       O PEOPLE task per NPC entity (game code), talk table built from
  options, outputs                             the owner's pages (game format), talk and hint runners (game code,
                                               coroutine shim), MSG engine (game code)  --> 2D capture of the window
                                            libsa1: character world(s)
                                               TALKABLE proxy per NPC -> game talk manager 8C10A28E picks it
                                               -> SA1_HOSTOBJ_EVENT_TALK; freeze and look-at by game code
                                            G-SA1: NPC renderer (render batches of the NPC world), HUD layer draws
                                               the captured window, editor and preview (sa1_text_render), +use for
                                               plain players, voice through the mixer
```

### 3.2 libsa1: text API (game typesetting as a library call)

A small service world, created on first use, holds 1ST_READ only: no stage boot, no character. The decompiled MSG core
runs there. Proposed header section, `SA1_API_TEXT 1`:

```c
#define SA1_LANG_JAPANESE 0   /* = B_8C754BFC values: the font table row (0x8C15497C) */
#define SA1_LANG_ENGLISH  1
#define SA1_LANG_FRENCH   2
#define SA1_LANG_SPANISH  3
#define SA1_LANG_GERMAN   4

#define SA1_TEXT_BLOCK  0x07u  /* the game's page default: block centred (8C0550FC) */
#define SA1_TEXT_CENTER 0x09u  /* every line centred (8C055050) */
#define SA1_TEXT_LEFT   0x00u

typedef struct sa1_text_desc {
    uint32_t struct_size;
    int32_t  language;        /* SA1_LANG_* (font and widths) */
    uint32_t width, height;   /* text area; the message window is 500 x 52 (8C055794) */
    uint32_t align;           /* SA1_TEXT_* (prepended like the game's first byte) */
} sa1_text_desc;

/* Typesets game-encoded bytes with the game's own MSG_Open (NH) + MSG_Puts and copies the
 * ARGB4444 buffer (tex_w x tex_h, powers of two: 512 x 64 for the window). */
SA1_API sa1_status sa1_text_render(sa1_context *ctx, const sa1_text_desc *d, const char *text, uint32_t len,
                                   uint16_t *pixels, uint32_t capacity, uint32_t *tex_w, uint32_t *tex_h);
/* Line widths in pixels with the game's width function 8C054D08 and spacing. */
SA1_API sa1_status sa1_text_measure(sa1_context *ctx, int32_t language, const char *text, uint32_t len,
                                    uint32_t *widths, uint32_t max_lines, uint32_t *lines);
/* UTF-8 -> game bytes for a language: ASCII (English), Latin-1 (French, Spanish, German), CP932 (Japanese,
 * only the 3,423 glyphs of the font); characters the font lacks become '?' and are counted. */
SA1_API sa1_status sa1_text_encode(sa1_context *ctx, int32_t language, const char *utf8,
                                   char *out, uint32_t capacity, uint32_t *len, uint32_t *unsupported);
/* Word-wraps UTF-8 text into game pages of at most 2 lines of at most `width` px; '\n' forces a line break
 * and a blank line forces a page break. */
SA1_API sa1_status sa1_text_paginate(sa1_context *ctx, int32_t language, const char *utf8, uint32_t width,
                                     uint32_t max_lines, char *out_pages, uint32_t capacity, uint32_t *pages);
/* The game's own NPC lines (disc talk files): field 0 Station Square, 1 Mystic Ruins, 2 Past; character
 * letter index; language. Returns entry / group / page counts and the page bytes (game encoding). */
SA1_API sa1_status sa1_text_game_lines(sa1_context *ctx, int32_t field, int32_t character, int32_t language,
                                       uint32_t entry, uint32_t group, ...);
```

Notes on the API:

* **The encodings are host conversions.** They are character-set tables, not game data. Japanese uses
  `WideCharToMultiByte(932)` on Windows and `iconv("CP932")` on Linux, then a range check against the font's three SJIS
  blocks (section 1.7). Text is checked before any game code sees it, because `8C054BDC` reads past the font on bad
  input.
* **Speed.** The game's rasterizer is a few thousand byte operations per page, so `sa1_text_render` runs in
  microseconds (**UNVERIFIED**; to be measured).
* **Oracle test.** Every page of every talk file (2,095 English and 1,770 Japanese pages, plus the 3 European
  languages) is rendered by libsa1 and by the harness running the original MSG code (`harness/` `call` on `8C055B80` +
  `8C054F2E`). The buffers must be equal byte for byte.

### 3.3 libsa1: NPC worlds and the `O PEOPLE` task

**One NPC world per field image in use** (ADV00 first). An NPC world is a guest-arena world booted with:

* 1ST_READ and the field's overlay (ADV00 is 2,328,347 bytes decompressed);
* the field's NPC texture archives (`SS_PEOPLE`, `SS_EKIIN`, `SS_MIZUGI`, `SS_BURGER`);
* the talk manager task;
* **no character**. The player-0 position that the NPCs read (`8C07CE82`) is a stand-in set by the host every step to
  the local viewer, the same idea as the camera adapter. The game's draw-range test (410 units by default) and its
  level of detail (animated only within 150 units: `0C910588`, 22,500 = 150²; a still model beyond) then behave as
  in the game for whoever is watching.

API sketch, `SA1_API_NPC 1`:

```c
typedef struct sa1_npc_desc {
    uint32_t struct_size;
    int32_t  field;           /* 0 Station Square (ADV00), 1 Mystic Ruins (ADV02), 2 Past (ADV03) */
    int32_t  type;            /* model type: 0-25 / 0-2 / 0-2 (SET ang.x) */
    float    position[3];     /* host space */
    float    yaw;             /* host yaw (SET ang.y) */
    float    draw_range;      /* host units; 0 = the game's 410 SA1 units (scl.x = 0) */
    uint32_t flags;           /* SA1_NPC_NOT_SOLID, SA1_NPC_NO_FLINCH, ... */
} sa1_npc_desc;
SA1_API sa1_status sa1_npc_create(sa1_context *ctx, const sa1_npc_desc *d, uint32_t *out_id);
SA1_API sa1_status sa1_npc_move(sa1_context *ctx, uint32_t id, const float position[3], float yaw);
SA1_API sa1_status sa1_npc_destroy(sa1_context *ctx, uint32_t id);
/* The owner's pages, already game-encoded and paginated (sa1_text_*); voice < 0 = none; steps[i] per page
 * (voiced: 0 = wait for a button); align = the first byte of every page. */
SA1_API sa1_status sa1_npc_set_text(sa1_context *ctx, uint32_t id, int32_t language, const char *const *pages,
                                    uint32_t npages, int32_t voice, const uint32_t *steps, uint32_t align);
SA1_API sa1_status sa1_npc_talk(sa1_context *ctx, uint32_t id);          /* = the talk manager's *flag = 1 */
SA1_API sa1_status sa1_npc_talk_abort(sa1_context *ctx);                /* = 8C05F210 */
SA1_API sa1_status sa1_npc_world_input(sa1_context *ctx, int32_t field, uint32_t buttons); /* raw pad: advance */
SA1_API sa1_status sa1_npc_tick(sa1_context *ctx, double dt, const float viewer[3]);    /* every NPC world */
SA1_API sa1_status sa1_npc_render(sa1_context *ctx, uint32_t id, float alpha, const sa1_render **out);
SA1_API sa1_status sa1_npc_render_2d(sa1_context *ctx, int32_t field, const sa1_render2d **out); /* window */
/* events through sa1_poll_events: SA1_EVENT_NPC_TALK_BEGIN / _PAGE / _END {npc, page}, SA1_EVENT_NPC_LOOK_AT
 * {npc, head position} (the game's 8C07C318 call), voice cues as today (8C107BA0) */
```

**How the owner's text gets to the game code.**

* LIB keeps, in the NPC world's heap, a talk table in the **game's own format** (section 1.4). There is one entry per
  NPC, with condition NULL, so `8C05F26A` returns its pages, and the entry number is the NPC's `scl.z`.
* The table is installed with `8C05F022` (talk) or as a hint-format table with `8C05F036` (voiced NPCs: `{page, steps}`
  pairs plus a `-4 voice` condition opcode).
* The count is written at the field's talk-file address: `0x0CB46000` for ADV00, because `O PEOPLE` compares `scl.z`
  with that word. That the region is free is **UNVERIFIED**: the game loads the file there, and nothing else in ADV00
  was seen to use it. LIB checks it against the world's heap map.
* A text change rewrites the table; while a talk is running, the change waits until it ends.

**Talk start and end.**

* `sa1_npc_talk` sets the NPC's `twp->timer` exactly as `8C10A260` would. The game's own `0C9107A0` then picks the talk
  motion and calls `8C05F140` / `8C05F01C` / `8C05F1C4`.
* For a voiced NPC, LIB's hook on `8C05F1C4` (a world x-hook, like the existing executor overrides) calls `8C05F196`
  with the hint-format record instead, so the game's voiced runner plays the voice and times the pages.
* The look-at `8C07C318` and the end `8C10A1C6` / `8C07C376`, which in the NPC world concern a player that is not
  there, become events for the host.

**Memory and cost.**

* One world per field in use: at most 3 x 16 MB address space. Committed memory is about 1ST_READ (6.7 MB) plus the
  overlay (2.3 MB) plus heap, about 10 MB each (**UNVERIFIED**; to be measured).
* NPCs themselves are game tasks: tens of them cost a few microseconds per step (**UNVERIFIED**).
* The NPC worlds are cosmetic per client. Random idles differ between clients, which the game would also do.

### 3.4 libsa1: coroutine shim (the runners)

* The thread list code `8C099A00-8C099DA0` is decompiled.
* The three SH-4 context-switch primitives become LIB runtime functions, like the SHC runtime, because C cannot express
  them:
  * `8C099A50` (yield to the next thread);
  * `8C099B2C` (exit or switch);
  * `8C099C0A` (initial frame).
* Each game thread gets a host fiber: `CreateFiber` on Windows, `makecontext`/`swapcontext` or a small assembly switch
  on Linux, with a 256 KB host stack. The game stack is still allocated from the game heap (`8C099C3C`), so heap state
  and references match the game.
* The world step calls the switch where `8C04E714` does, which is one line in `sa1_world_tick.c`.
* Requirements:
  * MXCSR is saved and restored per fiber;
  * the world's fault handling (VEH / SIGSEGV) must work on fiber stacks;
  * a world destroyed with live threads frees their fibers.
* **Shared with 2.0.** The EVENT system's scripts (`8C049692`, `8C049744`, `8C049A60`, `8C049CE6`, `8C04A2CA`) run on
  the same threads. Machinima wants to run the game's cutscene scripts and subtitles (`v1_machinima.md`), so this work
  is not NPC-only.

### 3.5 libsa1: TALKABLE host objects (G-SA1 characters talk like the game)

* **New kind `SA1_HOSTOBJ_TALKABLE`** (fits `libsa1_interaction.md` 2.1):
  * a proxy with a solid cylinder shape (the NPC's CCL info);
  * a talk node registered with the character world's talk manager through `8C10A146`, with the proxy's `pos`, `ang`
    and `flag` in arena memory, `radius = set[0x58] + 20`, and flags 2. NPCs of other kinds use 2 by default;
  * the proxy's `flag` is checked in the `hostobj_post` hook. When it is set, it raises `SA1_HOSTOBJ_EVENT_TALK
    {proxy}` and clears the flag.
* **Talk manager in character worlds.** The boot creates `8C10A120` when the first TALKABLE is added. Boot already
  reproduces other parts of `8C04E660`: `sa1_world_boot.c:243`.
* **What the game code then does in the character world:**
  * turns off the ports, so the character stops responding (`8C10A260`);
  * clears the character's speed;
  * turns the head on the host's `sa1_character_look_at(ctx, id, head)`, which runs `8C07C318`.
* **`sa1_character_talk_end(ctx, id)`** runs `8C10A1C6` and `8C07C376(0)`.
* **Safety.** A character world whose talk end never arrives would stay frozen. LIB therefore ends the talk by itself
  if the host destroys the NPC or calls `sa1_hostobj_remove`, and G-SA1 sets a timeout.

### 3.6 G-SA1: the NPC entity and editor

**Feature `npc`** (seam layout):

* `src/module/bind_npc.cpp` and `src/core/npc.{hpp,cpp}`: NPC worlds, text API wrappers, page bitmap to run-length
  runs for the painter;
* `addon/lua/gsa1/feat/{sh,cl,sv}_npc.lua`;
* `addon/lua/entities/gsa1_npc/` (shared, init, cl_init);
* `addon/lua/weapons/gmod_tool/stools/gsa1_npctext.lua`;
* tests `feat_npc_test.cpp` and `feat_npc_binding_test.cpp`.

**Entity `gsa1_npc`.**

* The server holds `field`, `type`, `yaw`, `draw_range`, `language` (`auto` follows Options), `align`, `pages` (UTF-8,
  at most 64 pages of at most 2 lines; at most 8 KB in total), `voice` (-1 or 0-2043), `steps[]`, `solid`,
  `talk_mode` (game rule, +use only, or both), `once_per_player` and `on_end` (an output for triggers in `v1_triggers`).
* Physics: an invisible box sized from the model's bounds. The model is drawn by G-SA1 from `sa1_npc_render`, through
  the character render path.
* Clients without the module or the disc see a placeholder and the fallback window (3.7).

**Persistence.**

* `duplicator.StoreEntityModifier(ent, "gsa1_npc", data)` covers dupes, saves (`gmsave` goes through the duplicator)
  and undo.
* The server validates all fields: lengths, control characters, page count.
* Clients receive the data compressed (`util.Compress` of JSON) when they join and when it is edited. Edits go through
  a net message, rate-limited and allowed only to the owner or admins (CPPI).

**Editor** (NPC text tool: left click edits, right click copies the text, reload pastes it; also from the entity's
context menu):

* A page list. Each page is a 2-line text box with a **live preview from `sa1_text_render`**: the exact game glyphs and
  wrapping in a 512 x 64 render target. Lines over 500 px, more than 2 lines, or unsupported characters are marked.
* "Paste long text": `sa1_text_paginate` splits it into game pages.
* Alignment: block centre (the game), centre each line, or left.
* Voice: a number 0-2043 with play and stop through the mixer. Labels come from the hint tables that use a clip; the
  coverage is **UNVERIFIED**. With a voice, each page has a step count; 0 waits for a button. "Spread over the clip"
  divides the clip's length by the pages' text length.
* **"Import a line from the game"** (`sa1_text_game_lines`): field → story character → language → NPC entry → variant
  (the groups) → pages. It reads the owner's disc at run time; nothing is shipped.
* A model picker with icons rendered at run time from the NPC world, cached in `data/gsa1/icons/`, which is local to
  the owner's machine.

### 3.7 G-SA1: the window on screen (HUD layer)

* **Normal path.** Every frame, G-SA1 draws the NPC world's 2D capture (`sa1_npc_render_2d`) in the HUD layer of
  `gsa1_070_hud.md`. That gives two primitives:
  * the band: an untextured quad with vertex colours;
  * the text: a textured quad with vertex colours, on the NPC world's memory texture.

  The memory texture is exposed with a serial number. G-SA1 repaints its 512 x 64 render target when the serial changes,
  which is once per page and at most 1,278 runs.
* **Mapping 640 x 480 to the screen.** Use the HUD layer's 4:3 safe area. For the owner's 3440 x 1440 that is a scale
  of 3, a 1920-px-wide area and a 760-px offset.
  * The band covers x 0-640 of the 4:3 area. Option "band spans the whole screen" (on by default), because the game's
    band is the whole screen.
  * The text quad: GMod's `surface` cannot give per-vertex colours, so it is a `mesh` quad (`MATERIAL_QUADS`) in
    `cam.Start2D` with a material made from the render target with `$vertexcolor 1` and `$vertexalpha 1`.
  * Options: "crisp text" (point filtering, off by default; on, it shows the 24-px glyphs at x3 blocky), "message window
    scale" (0.5-1.0 of the HUD scale), and "move up when the HUD is at the bottom" (**UNVERIFIED** whether the game's
    HUD and the window overlap: the life counter is at y 400-448, `hud_probe` out_c).
* **Fallback window** (clients without the disc, or before the shim and polygon capture land). G-SA1 draws the same
  geometry and colours (section 1.6) and runs the choreography of section 1.5 from a step clock:
  * text from `sa1_text_render` when the disc is present;
  * otherwise a Garry's Mod font (a "Trebuchet24"-like stand-in), in the same box and paging.

  The fallback is compared against the harness reference (W11): window alphas per step and the step of each page swap.
* **Talking hides parts of the HUD.** The game draws the window over its own HUD. The option "hide the GMod crosshair
  while talking" is on by default.

### 3.8 Talk interaction

**G-SA1 characters (the game rule).**

1. Every NPC entity becomes a TALKABLE proxy in each **local** character world.
2. The game's talk manager decides: action button (B/X/Y, which G-SA1 maps today), radius, facing and height.
3. On `SA1_HOSTOBJ_EVENT_TALK`, G-SA1:
   * calls `sa1_npc_talk(npc)`;
   * forwards the character's A/B/X/Y presses to the NPC world (`sa1_npc_world_input`);
   * calls `sa1_character_look_at` on `SA1_EVENT_NPC_LOOK_AT`.
4. On `_END` it calls `sa1_character_talk_end`.

The character stands still and looks at the NPC's head, as in the game. The camera does not change, because the game
has no talk camera.

**Plain Garry's Mod players.**

* `ENT:Use` on the server (E within the use range) sends a net message to that player, who starts the talk in their
  NPC world.
* Pages advance with E, left click or Space. These presses go to the NPC world as A.
* The talk is aborted (`sa1_npc_talk_abort`, the game's abort, which closes the window) if the player moves more than
  200 Hammer units away, dies, or opens the spawn menu.
* Option "freeze plain players while talking" is off by default.

**Prompt.** The game has none (section 1.3), so the default is **none**. An option shows "Talk" above the NPC, set in
the game's own font with `sa1_text_render` from the disc, when the local character or player is within the game's talk
radius and facing range.

**Multiplayer.**

* Talks are client-local. Only the talker sees the window, as in the game.
* Option "show NPC text to players within N units" (off by default): the server relays the page index, and every client
  shows the same window. Each client's NPC world renders the window itself, so each client sees it in its own font and
  language.
* The server only learns `talk begin` and `talk end`, for `once_per_player` and outputs.

**Voice.** A voiced NPC plays its clip from its position (`SA1_AUDIO_CUE_VOICE`, position = NPC, the game's distance
limit of 600 SA1 units), and the pages follow their step counts. The game's subtitles switch `B_8C788C10` is driven by
the option "show text of voiced lines" (on by default).

### 3.9 Q menu (every option has a control)

* **G-SA1 creation tab → NPCs** (`v1_arch.md`):
  * Station Square: 26 types;
  * Mystic Ruins: 3 types (phase 2);
  * Past: 3 types (phase 2);
  * "Cast" (phase 2).

  Click to spawn in front of you, facing you.
* **Tool "NPC text"**, in the G-SA1 tool category: the editor of 3.6.
* **Options > G-SA1 > NPCs**:
  * text language (Auto: follows G-SA1's language; English; Japanese; French; Spanish; German);
  * show text of voiced lines;
  * message window scale;
  * band spans the whole screen;
  * crisp text;
  * talk prompt;
  * talk key for plain players (binder, default E);
  * freeze plain players;
  * show others' talks within N units;
  * default draw range;
  * maximum NPCs per player (admin);
  * NPC solid;
  * NPC flinch on spin;
  * fallback window for clients without the disc;
  * status: NPC worlds and memory.

### 3.10 Cast members as NPCs (phase 2)

Sonic, Tails, Knuckles, Amy, Big, E-102 and Tikal can stand as NPCs: a libsa1 character world with no input, which
already plays the game's idles. It gets a TALKABLE proxy in the other characters' worlds, and the same window. Each one
is a 16 MB world, so the number is limited (4 by default).

The game gives playable characters no talk motion outside events. Talking poses come with the event motions of 2.0
(`v1_machinima.md`). Eggman (`EGGMAN.PRS`) and Tikal (`TIKAL_PROG`, 13 of 17 functions matched) also have NPC-mode
code.

---------------------------------------------------------------------------------------------------------------------

## 4. Verification

* **Harness references** (SH-4 interpreter running the original code; new trace kinds):
  * (a) text buffers: `NH_MSG_Open` + `MSG_Puts` for every talk-file page in 5 languages, against `sa1_text_render`,
    byte for byte;
  * (b) a talk session in Station Square: a SET-placed `O PEOPLE`, the talk manager pick (positions and facing at the
    radius boundary), runner steps (window alpha, text alpha, page-swap step, `kind`), voice requests, and
    `D_8C19E3A9` port state per step, against an NPC world plus a character world with a TALKABLE proxy;
  * (c) `O PEOPLE` idle and talk motion frames over 600 steps against the NPC world, strict like the 10 reference
    traces.
* **LIB tests**:
  * the coroutine shim: threads created and destroyed inside threads, a destructor, a world destroyed with live
    threads, a fault inside a thread, and x86 against x64;
  * encode and paginate: every glyph of the 3 fonts round-trips; unsupported characters become '?'; pagination of
    10,000 random sentences never exceeds 500 px or 2 lines.
* **G-SA1**:
  * Lua tests of the entity data validation and dupes;
  * a binding test of page bitmap to runs;
  * the fallback choreography against reference (b);
  * an in-game checklist: talk from the front, side and back, at the radius edge and on stairs (the height test); E as
    a plain player; voice; dupes and saves; a client without the disc; 21:9.

---------------------------------------------------------------------------------------------------------------------

## 5. Work items and phasing

| # | Item | Effort | Depends on |
|---|---|---|---|
| W1 | Decompile the MSG engine `8C0548E0-8C055D20` (40 functions, 5,078 B) | M (one decomp batch) | - |
| W2 | Decompile the talk manager `8C10A120-8C10A4E0` (8 functions, 888 B) | S | - |
| W3 | Decompile the talk and hint API and runners `8C05F000-8C05FCFC` (31 functions, 2,986 B) plus `8C04C680`/`688` | M | - |
| W4 | Decompile the thread lists `8C099A00-8C099DA0` (23 functions, 852 B); LIB rt coroutine primitives (fibers); step calls the switch | M-L | W3 for the runner test |
| W5 | ADV00 image setup (src dir, code map, lowering like STG01) and decompile the people code `0C90F2C0-0C910D60` (44 functions, 6,160 B) plus small dependencies | M | - |
| W6 | LIB `sa1_text_*` (service world, encodings, paginate, game-lines reader) | M | W1 |
| W7 | LIB NPC worlds and `sa1_npc_*` (field boot, viewer stand-in, talk table builder, hooks, events, textures) | L | W1, W3, W4, W5, W9 |
| W8 | LIB TALKABLE kind, talk manager in character worlds, `sa1_character_talk_end` / `look_at` | M | W2, round 5 host objects |
| W9 | LIB 2D capture of 8C63EBC8 and memory textures (njSetTextureInfo/Name and texlist load), with the frame-end upload `8C055C4C` in the pair | M | HUD round's 2D output (`gsa1_070_hud.md`) |
| W10 | G-SA1 feature `npc`: entity, editor and tool, persistence, network, HUD drawing, talk (characters and +use), voice, Options page | L | W6-W9, HUD layer, `v1_arch` creation tab |
| W11 | Verification: harness references (a)-(c), LIB and G-SA1 tests | M | W1-W10 |
| W12 | Phase 2: ADV02 and ADV03 people (81 functions, 11.5 KB; re-split ADV03 first), walkers along splines (`v1_triggers` + the walker code), choice window `8C055460` for branching talk, cast NPCs | L | 1.0 NPCs, `v1_triggers`, machinima event motions |

* **Order.** W1, W2, W3 and W5 can run as one decomp wave now (into `SADC/build/staging`, as wave A did). Then W4 and
  W6. Then W7, W8 and W9. Then W10 and W11.
* **What the owner sees first.** W6 plus the fallback window (W10 without W7) gives a spawnable talking NPC with the
  game's font and window early. The model would be a placeholder until W7 lands; whether that is wanted is owner
  question 6.

---------------------------------------------------------------------------------------------------------------------

## 6. Owner questions

1. **Who sees the text?** Only the player who talks, as in the game (default), or everyone nearby (an option)?
2. **Plain Garry's Mod players.** Talk with E (default)? Should they be frozen while talking, like the game freezes
   characters? (Default: not frozen.)
3. **Talk prompt.** The game shows none. Keep it that way (default), or show "Talk" above the NPC when you can talk?
4. **Voices.** Only the game's voice clips from the disc (default for 1.0), or also your own sound files? Your own files
   would come with the 2.0 audio player.
5. **Accents and Japanese.** The US game's English font has no accented letters. Text with accents would use the
   European version's font, which looks almost the same (appendix A, `de.png`). Is switching automatically OK
   (default)?
6. **Early preview.** Should a first release have talking NPCs with the game's window and font but a placeholder body,
   before the townspeople models land? (Default: no, wait for the models.)
7. **Cast members** (Sonic, Tails and the others) as NPCs: in 1.0 standing idle (each costs as much as a playable
   character), or wait for 2.0's cutscene animations? (Default: 2.0.)
8. **Spin attacks on NPCs.** The game's townspeople only flinch. Keep that (default), or let NPC entities take round 5
   damage?

---------------------------------------------------------------------------------------------------------------------

## 7. Risks

* **The coroutine shim (W4)** is the largest technical risk: fibers together with the world's fault handling,
  MXCSR, and x86/x64/Linux. Mitigation: the fallback window (3.7) does not need it, and 2.0 needs the shim anyway.
* **2D polygon capture and memory textures (W9)** are new kinds in the capture path. They must be coordinated with the
  HUD round, which owns the 2D output format.
* **First ADV image (W5).** No ADV overlay has a code map or lowering yet, and ADV03's manifest misclassifies its NPC
  code as data (1.8). The talk-file address `0x0CB46000` is assumed free (3.3, **UNVERIFIED**).
* **Fidelity points still open:**
  * the text alpha has no visible effect (constant vertex colours);
  * threads advance per step;
  * the sign of the ±80° facing test;
  * the motion roles 0-2 and 6.

  All four are marked **UNVERIFIED** and covered by W11.
* **Text safety.** The game's glyph lookup has no range check. Every host string must pass `sa1_text_encode`, and the
  server validates lengths and control bytes (only `\n` is allowed, and `0x07` / `0x09` are added by the API).
* **Memory.** Each NPC world costs 16 MB of address space (about 10 MB committed, **UNVERIFIED**); 3 fields plus cast
  NPCs add up. Limits are set in Options.
* **Concurrency.** Other rounds edit LIB worlds, capture and g-sa1. This feature keeps to its own files (seam) and to
  new API sections. Changes in `sa1_world_tick.c` (the coroutine switch) and the capture are small but shared.

---------------------------------------------------------------------------------------------------------------------

## Appendix A. Scratch tools and evidence (`SADC/build/research/npcs/`)

| File | What |
|---|---|
| `status.py` | decompilation status per address range for any image (C in `src`, staging, NON_MATCHING), from a stable snapshot of the xrefs (`1ST_READ.xrefs.snap.json`, copied because another round was rewriting `build/1ST_READ.xrefs.json`) |
| `ghidra/ADV00`, `ADV02`, `ADV03` | Ghidra 12.1.4 headless runs of the decompressed overlays at `0x0C900000` (seeds from the split's xrefs; pseudo-C of the people code in `decomp/*.c`; project in `ghidra/proj`) |
| `mes_dump.py` | decodes a talk file: condition scripts (`+f` = flag must be set, `-f` = clear) and pages |
| `mes_stats.py` | totals over all 99 talk files and opcode counts |
| `msg_render.py` | re-implementation, for measurement, of the glyph lookup, widths, rasterizer and centring over `baserom/1ST_READ.BIN`; writes the 512 x 64 buffer as PNG |
| `en.png`, `jp.png`, `de.png`, `en_s.png` | rendered pages (English, Japanese, German, English in the European font) |

Other inputs:

* `build/ghidra/1ST_READ/decomp/*.c` (pseudo-C) and `asm/1st_read/text/8C0548E0.src`, `8C10A120.src` (constants);
* `build/*.imports.txt` (which overlays call which 1ST_READ functions);
* `reference/dc_sa1_data_labels.csv` (model and font labels);
* the SET files `SETSS*`, `SETMR*` and `SETPAST*` in `disc/SONICADV`.

---------------------------------------------------------------------------------------------------------------------

## Review (adversarial)

Reviewer pass of 2026-10-21, read-only. Scratch in `SADC/build/research/npcsreview/`: `status_live.py` (the doc's
`status.py` against the live xrefs and `src`), `union.py` (summed xref sizes against the bytes they actually cover),
`deps.py`, `ext.py`, `adv00refs.py` (external callees and their status), `fns.py`, `callers*.py`, `setcount.py`,
`mescheck.py`, `mes3.py`, `afs.py`, `hint.py`, `thrc.py`. LIB and G-SA1 were read as they stood at about 16:00.

### R1. Verified

* **Decomp status.** 0 bytes with C, staging or NON_MATCHING in `8C0548E0-8C055D20` (40 functions),
  `8C05F000-8C05FCFC` (31), `8C10A120-8C10A4E0` (8), `8C099A00-8C099DA0` (23), `8C107800-8C107E40` (25) and
  `8C04C680`/`688`. ADV00/ADV02/ADV03 people: 44/36/45 functions, 6,160/5,374/6,158 B by summed xref sizes. Overlay
  totals 499/67,446, 381/72,436, 136/16,080. `tools/progress.py`: 1,223/4,119, 220,168/973,430 B (22.62 %).
  Dependency status as in 1.8 (8C0509B8/DA, 8C07256A, 8C07CFDE, 8C07CE82, 8C07C318/C376, 8C0344A0, 8C0409C0 C;
  8C050BE4, 8C085A50, 8C041D68 NM).
* **Talk manager** (`8C10A120.src`). Node layout and `MAlloc(32)`; pick preconditions (list, `B_8C754C08+0x10 &
  D_8C19E3A0`, `D_8C19E3A0` = 0x602, `D_8C5FBAC4 == 0`); pool constants 10000.0, 7281.0, 14563.0, 10.0
  (`8C10A3C4/3D0/4C0/4C4`); radius + `pwp+0x10C` (`p.rad`, 4.0, `player.h:70/254`); flag-4 player facing, NPC facing,
  flag-2 height tests; `8C10A260` = `*flag = 1`, `8C0509DA(0xFF)`, `8C050BE4`, `8C07256A`; `8C10A1C6` end; unregister
  ends an active talk; task destructor `8C10A45E`, no display. `8C0509DA/B8` write `D_8C19E3A9[0..1]`.
* **MSG_Disp pool.** `B_8C788C10` gate for kind 1; band x 0/640, y-4 / y+h+4, pri `0xBF999999` (-1.2), attr 0x60;
  text attr 0x80000060; `njColorBlendingMode(0,8)`, `(1,6)`; `8C63EBC8` with 4 points. The four colour words are
  overwritten with `0xEEDDDDEE`/`0xEED0D0E0` (`8C054B5E-8C054B72`), so `MSGC+0x1B` only gates the text. UVs at
  `D_8C1549E0` are u16 pairs (0,0) (0,256) (256,256) (256,0).
* **Fonts.** Table rows read from `1ST_READ.BIN` exactly as in 1.7; 0x3C2B8/72 = 3,423, 0x3F00/72 = 224,
  0x1B00/72 = 96; the data is contiguous. `de.png` and `jp.png` are legible and placed as described.
* **Disc.** 99 `*_MES_*.BIN`, 1,169,620 B; `MR_MES_K_G.BIN` absent; English 310 entries, 727 groups, 2,095 pages;
  Japanese 1,770 pages; opcode counts (VOICE 0); `PAST_MES_M_S` entry 4 runs off the file. `O PEOPLE` placements
  195 / 34 / 4, 19 Station Square types (in 28 of the 30 SS SET files). `EVENT_ADX_US.AFS` and `EVENT_ADX.AFS`: 2,044
  entries each. NPC archive and path file sizes as in 1.1.
* **ADV00 people** (Ghidra outputs re-read). Loader `0C90F2C0` (`8C09DA40` to `0x0CB46000`, `8C05F022`,
  `8C05F036`); `objexec_O_PEOPLE` compares `scl.z` with `(float)*(int *)0x0CB46000`; `0C91043E` registers
  `8C10A146(set[0x58] + 20, tp, &pos, &ang.y, &timer, 2)`; talk start `0C9107A0`; end in `0C91064E`; constants
  168,100 and 22,500. The per-stage hint table `D_8C1B9E7C` is NULL for stage 26, so `8C05F036`'s table is the one
  Station Square uses.
* **ADV03 split bug**: chunk `0x0C9040E4-0x0C90C0E4` is kind `data` and holds 23 functions as `.DATA.L`.
* **Egg Carrier**: ADV0100/ADV0130 import the hint API and the talk manager, not `8C05F022`/`8C05F140`.
* **Threads.** `8C099A50` saves PR, GBR, MACH, MACL, R8-R14, R4, FR12-FR15 and switches through the tree walk
  `8C099A1C`; default stack 0x1000 (`.W` at `8C099D50`), from `MAlloc`, top `| 0x80000000`; `8C04E714` is one of its
  21 callers; LIB skips it (`sa1_world_tick.c` header and `sa1g_step_game`).
* **LIB/G-SA1 facts**: `8C63EBC8` is a void stub (`gen/policy/stubs.tsv`), `8C608C82`/`8C6088BC` are no-ops
  (`rt_texture.c`); `SA1_AUDIO_CUE_VOICE`; 4 Hammer units per SA1 unit; feature pages through `GSA1.AddMenuPage`.

### R2. Refuted (with the correct facts)

1. **`B_8C754EC8` is not a pause flag.** It is the sub-step index (LIB `SA1G_ADDR_SUB_STEP`, `sa1_world.h:551`;
   pairs run it 0 then 1). So `MSG_Disp`, the runners' per-step draws (they call `8C054A00` only when it is 0) and
   `O PEOPLE`'s display `0C910AC0` draw on **sub-step 0 only**, and `8C05FC00` starts the thread only on sub-step 0.
   The window is drawn from inside the coroutine switch during the step, not from a display pass. LIB's capture window
   covers the whole tick (particles on sub 0 are recorded), so this works, but the HUD round's `exec` field
   (`D_8C1C1D40`) will read 0 there, which the HUD design treats as "the HUD pass".
2. **Byte counts are inflated by overlapping xref extents.** Covered bytes: MSG 4,806 (not 5,078: `8C054F2E`'s 700 B
   extent swallows `8C054FEC` and `8C055050`); talk API 2,928 (`8C05F196`'s 212 B swallows `8C05F1E0` and `8C05F210`);
   talk manager 852 (not 888); ADV00 people 5,894 + 142 missing (item 3) = about 6,036; ADV02 5,126; ADV03 5,892.
   1.0 is about **15.5 KB**, not 16.1 KB.
3. **Split defects inside the 1.0 ranges** (the doc reports only ADV03's):
   * `8C05F1C4`, the entry the people code calls, has no symbol: it is `.DATA.L H'6E432FE6,...` in `8C05F000.src`
     (28 B; decoded: `if (rec[0]) 8C05FB0A(8C05F902, rec[0], 0);` then `BRA 8C05F240(rec[1])`). ADV00, ADV02 and
     ADV03 reference it as a raw `H'8C05F1C4`. Its tail `8C05F240` (flag setter, 42 B) is shared with `8C05F196` and
     has no symbol either.
   * `8C0550FC` (block centring) is a label in `8C055050`'s chunk, reached by `BRA` from `8C054F2E`; not in the xrefs.
   * ADV00 `0C90FFD2` (98 B) and `0C910034` (44 B) are emitted as `.DATA` and missing from `build/ADV00.xrefs.json`;
     `0C910060` (40 B, 0 callers) is not a function start but the rest of `0C910034`.
4. **"Every page has at most 2 lines" is false.** `MR_MES_E_E.BIN` entry 2 group 2 (English: "But I'm still seeing
   new / things / every day."), `SS_MES_E_G` entry 18 group 2 and `SS_MES_E_S` entry 5 group 4 have three non-empty
   lines. The rasterizer `8C054D6C` skips a glyph only when the cursor y >= texture height and clips rows only at the
   top (`CMP/HS` against the buffer base, `8C054E6E`), so a third line at y 56 writes rows 56-79: up to 16 KB past the
   512 x 64 buffer. **UNVERIFIED in the game**; W11 (a) must include these pages.
5. **"Neither function checks the range" is too broad.** `8C054BDC` clamps the low end (`c - 0x20 < 0` gives glyph 0,
   `8C054C0C`), and the European rows cover every byte 0x20-0xFF. Only double-byte codes in rows 0/1 are unchecked (in
   US English a Latin-1 byte >= 0x80 is taken as a lead byte and eats the next byte). Filtering is still required.
6. **The coroutine shim is not shared with 2.0.** `v1_machinima.md` says the cutscene scripts "are not needed and are
   not decompiled" (its lines 37-38 and 60) and M5 subtitles need no decompilation. W4 is justified by the owner rule
   (once W3 lands the runners are game code), and its cost is NPC-only. Section 7's mitigation is wrong.
7. **The "small dependencies" are not 1.0 work.** `rand` (`rt_misc.c`), `8C03700C`, `8C038CEC`, `8C038D00`
   (`rt_draw.c`) are LIB runtime; `8C049A4E` and `8C042DC4` are `stubs.tsv` lines (ret 0).
8. Font data `0x8C112B84-0x8C15497C` is **269,816 B**, not 270,840. `B_8C788C10` is set to 1 when the save option
   bit 2 is **clear** (`8C04C98E`).
9. Drift since 14:20: G-SA1 is **0.8.0** (`sh_config.lua:4`); the host-object section of `libsa1.h` starts at line
   1324 and `SA1_HOSTOBJ_HOLDABLE` (5) is already in (line 1565).

### R3. Gaps

1. **LIB has no body for functions every talk calls.** `8C107BB0` (voice stop: called unconditionally by `8C05FB0A`,
   also `8C05F210`, `8C05F1E0`), `8C107BC2`, `8C608914` and `8C608922` (called by the upload `8C055C4C`) have neither
   an rt body nor a `stubs.tsv` line. The first talk would fault (code map). Add them to W3/W7/W9.
2. **Voice order.** `8C107BA0` only stores a pending id for the voice manager; `8C107BB0` stops the playing handle at
   once. `8C05F196` calls `8C107BA0`, then `8C05FB0A` calls `8C107BB0`. LIB emits `SA1_EVENT_VOICE` at the
   `8C107BA0` call, so a plain stop event for `8C107BB0` would cut every voiced NPC's clip in the same step. W7 must
   keep the deferral (or drop a stop that follows a request in the same step); W11 (b) must test it.
3. **The shim against LIB's world runtime.**
   * LIB's stack window (`SA1G_SEG_STACK`, guest `0x9D000000`) maps [entry frame - 16 MB, entry frame) of the host
     stack. A fiber stack lies outside it, so encoding a fiber local as a guest pointer faults. Rebase the window on
     every switch.
   * The `SA1G_WORLD_TRY` recovery point lives on the entry stack. A fault inside a fiber must switch back before
     unwinding.
   * The root thread `8C099A00` (memset of `B_8C7A0A0C`, called by `8C04E660`) must be in the NPC-world boot.
   * The entry trampoline `8C099AAC` and exit `8C099AB2` belong in the design.
   * Re-rate W4 as **L**.
4. **The talk manager in an NPC world can fire on its own.** `sa1_npc_talk` bypasses `8C10A260`, so `D_8C5FBAC4`
   stays 0 and the pick stays armed. If a page-advance press reaches `per[0]` (ports on: `8C050BE4` copies raw into
   `B_8C754C08`), the pick runs against the viewer stand-in, and `8C10A260` calls `8C07256A(*B_8C78C628,
   playerpwp[0])` with no player. Do not create `8C10A120` in NPC worlds (register, unregister and end work on the
   list alone; free nodes on destroy), or keep the ports off there and feed only the raw record.
5. **The viewer stand-in is under-specified.**
   * `8C07CE82` needs `playertwp[0] != NULL` and reads the history rings `B_8C78C690`/`B_8C792690`. Write them with
     `8C07CEF2`.
   * `8C07C318` and `8C07C376` write `playertwp[0]->ewp`.
   * `0C9107A0`'s flinch needs the player's collision with flag 0x400. No player means the "NPC flinch on spin"
     option has no source: forward hits from the TALKABLE proxy or drop the option.
   * Whether LIB character players have an `ewp` for the look-at is **UNVERIFIED**.
6. **TALKABLE needs per-type data that character worlds lack.** The radius base (`+0x58`) and the CCL info (`+0x44`)
   are ADV00 data, which character worlds do not hold. Expose per-type talk info from the NPC world, or put radius,
   height test and CCL size in the descriptor.
7. **2D capture.** The HUD round's POLY2D payload (`gsa1_070_hud.md` 4.1.3) carries points, colours, UVs, pri and
   exec but no texture identity, and `sa1_screen_draw` names textures by disc archive. The text quad needs the
   current texlist and number plus a memory-texture handle and serial. Make W9 an extension of HUD F2, not a second
   kind.
8. **Owner rules.**
   * Imported game lines would sit as text in the entity, in its dupes and saves (which can go to the Workshop), and
     go to clients without the disc. Store a reference {field, character, language, entry, group, page} that each
     client resolves from its own disc, as `v1_machinima.md` does for game subtitles.
   * Icons cached in `data/gsa1/icons/` are renders of game assets on disk. G-SA1 says "Nothing is written to disk"
     for textures, and its map cache holds "no game data". Keep icons in memory, or ask the owner.
9. **Encoding.** `WideCharToMultiByte`/`iconv` break LIB's "no dependencies beyond the C library", and they map
   U+301C/U+FF5E and U+2212 differently per platform. Use a generated Unicode-to-SJIS table limited to the 3,423
   glyphs (the DLL may hold generated tables).
10. **"Auto" language.** G-SA1 has no text language, only `gsa1_sound_japanese` (voices). Define what Auto follows.
11. **Phase 2 walkers.** `v1_arch.md` (lines 33 and 240) makes walking NPCs ACTORs: simulated in the owner's world
   and pose-streamed. This doc makes them per-client and cosmetic. Reconcile this before W12, because +use and talk
   targets need the server's positions.
12. **The per-act loader.** `0C90F620` calls `0C910D60`/`0C910E2C` (in the hint-glue range) and `0C90F49E` (paths).
   State that NPC worlds skip `0C90F2C0`/`0C90F620`, or add those bytes.

### R4. Required changes

1. Add **W0 split fixes (S)** before W1/W3/W5: symbols for `8C05F1C4` and `8C05F240` and a correct `8C05F196`
   extent; `8C0550FC`; ADV00 `0C90FFD2`/`0C910034`/`0C910060`. ADV03 stays in W12.
2. Fix `B_8C754EC8` in 1.5, 1.6 and 3.7 (sub-step 0 only, drawn during the step).
3. Restate sizes as covered bytes (about 15.5 KB) and take LIB-provided dependencies out of the decomp scope.
4. Add the `8C107BB0`/`8C107BC2` events and stubs, the `8C608914`/`8C608922` rt bodies, and deferred-voice semantics
   to W7/W9.
5. W4: stack-window rebasing, recovery outside fibers, `8C099A00` in the boot; effort L; drop "shared with 2.0".
6. W7: no talk-manager pick in NPC worlds; specify the stand-in; flinch forwarding or no flinch option.
7. W8: a per-type talk-info API. W9: extend the HUD round's POLY2D.
8. W10: game lines by reference, icons in memory, an embedded SJIS table, a defined Auto language. Pagination and the
   server validation must cap at 2 lines and 500 px.
9. Owner questions: add "game lines stored by reference" and "icon cache on disk".
