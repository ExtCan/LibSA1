# G-SA1 0.9.4a addendum: event faces, microphone lip sync and the emote wheel (`faces_wheel_plan`)

Status: plan, written 2026-10-26 evening (planner step "faces-design" of `sadc/tools/workflows/gsa1_prep_faces_wheel.js`, run
wf_e15aeb14-07a). An ADDENDUM to `super2_plan.md` (round 0.9.4a): it adds four agents and their files, contracts, options and
checks to that round; everything `super2_plan.md` says about the Super Sonic polish stays as it is. Read-only planning: this file
is the only file written; nothing in LIB, g-sa1, SADC/src, SADC/config, SADC/include or SADC/harness was changed or built.
REVIEWED (adversarial, the same evening): see "## Review (adversarial)" at the end; its fixes are applied in place, each marked
"(review R.n)", and it wins where it disagrees with the text.

Inputs, each read in full:

* the owner's request (2026-10-26, rounds plan 14.1s) and its binding addition `super2_plan.md` 10.2 (CORRECTED reading: each
  wheel slot = a body animation + an optional face animation; the Left Trigger in the open wheel plays the highlighted slot's face
  animation WITH its body animation, at the centre only the neutral face animation; user-made face sequences with enter / hold
  frames made on an Options page);
* the research of this prep run: `faces_events.md` (res-faces: the game's face code, EV heads, the face block, lip sync L2,
  measured in a copy of the 0.9.3a archive), `emote_wheel_lib.md` and `emote_actions_{sonic,tails,knuckles,amy,gamma,big,
  supersonic}.tsv` (res-emote: the cutscenes' event action queue, every animation with internal names, measured in a copy of the
  0.9.4 archive), `faces_wheel_ui.md` (res-ui: the wheel, pages, ConVars, Garry's Mod's lip sync, the microphone through GMod's
  own `bass.dll`, seams);
* `super2_plan.md` (0.9.4a as planned: 3.1-3.3, 4, 5.1-5.5, 6, 10.1-10.3, its Review), `gsa1_rounds_plan.md` 2.1-2.5 and
  14.1a-14.1t, `v1_roadmap.md` 6 (D12, D13) and 7 (2.0 M1 faces), `v1_machinima.md` 2 (by reference of the research);
* the trees as they are tonight, read only: LIB `include/libsa1.h` (sections, `sa1_state_ex`, `sa1_action_info`, status codes),
  `src/world/sa1_world.h` (`sa1g_world`, its hook fields), `src/world/sa1_world_tick.c` (`sa1g_step_game`), `src/sa1_api_world.h`
  (`sa1w_pstate`), `src/world/sa1_api_world.c` (`sa1w_world_pstate`), `src/sa1_character.c` (`state_of`, `ensure_world`,
  `sa1_character_world`), `src/sa1_charopt.c` (the asset-layer bridge pattern), `src/world/sa1_world_set.c` (`st_state_of`: a
  segment-0x18 block validated by magic), `src/world/sa1_world_image.c` (what a rebuild resets), `src/sa1_manifest.c`
  (`k_super_subst`, `sa1_manifest_char`), `build.py` (sources by glob); SADC `code_8C04BC70.c` / `code_8C04B7BA.c` (the face block
  reader, EV_SetFace's body); g-sa1 `gsa1_feature.hpp`, `bind_super.cpp` (`session()->get(id)`), `session.hpp` (`Character`),
  `cl_pad.lua` (LT = pad bit 0x10000), `feat/sh_super.lua` (`S.PAD_BUTTONS`), `feat/cl_super.lua` (`S.coreSonic`),
  `cl_warm_census.lua`, `sh_config.lua` (`GSA1.CHARACTERS`, 8 entries), the feature page orders; the workflow
  `sadc/tools/workflows/gsa1_round_094a.js` as it is now (5 agents).

Abbreviations as in `super2_plan.md`: SADC, LIB, G-SA1, GDI, maps; "step" = 1/60 s, "pair" = 1/30 s. "EV head" = the event head,
"face block" = `eventwk` 0x5C-0x6B (`face`, `face_next`, `face_time`, `face_count`, `face_script`, `face_seq`; faces_events.md
2.3), "face work" = `PL_FACE` (`playerwk` +0x70), "the queue" = the player's event action queue (`eventwk.list40`). Letters
'A'-'Z', 'a'-'e' = the game's face animations 0-30 (faceTbl `D_8C19C32C`). **UNVERIFIED** marks what is only derived. Agent texts
are pasted after the rounds plan's COMMON preamble (its 2.1), `gsa1_090_plan.md`'s PRE-090, `super2_plan.md`'s PRE-094A (its
3.1) and this addendum's PRE-094A-FW (3.1), in that order.

---------------------------------------------------------------------------------------------------------------------

## 0. Summary

**What the addendum adds to 0.9.4a.** The owner's request, built on the game's own code:

| Part | How (one line) | LIB | G-SA1 |
|---|---|---|---|
| Event faces + the switch | the host takes the place of a cutscene's face script: the game's EV_SetFace / EV_ClrFace bodies (8C04BBCE / 8C04BD60), its reader and blend draw the EV head that is already in every world | `094a-lib-faces` | `094a-gsa1-faces` |
| Face animations (owner's sequences, 10.2) | the owner's entries become the game's own step data `{time, key}` in a per-world buffer that the game's reader walks: enter = the first step's time, the game's steps, hold = repeated steps on the last key | `094a-lib-faces` | `094a-gsa1-faces` (the maker on the Faces page) |
| Lip sync | GMod's loudness (or the microphone read through GMod's own `bass.dll`) -> one level per frame -> the mouth on the game's mouth keys, written into the face block and drawn by the game's own blend | `094a-lib-faces` | `094a-gsa1-faces` |
| The emote wheel | the cutscenes' event action queue (EV_SetAction / SetMotion / ClrAction bodies 8C04B4D2 / 8C04B528 / 8C04B48C), drawn by each character's own display; the game keeps running underneath (identity test: state and rand bit-identical) | `094a-lib-emote` | `094a-gsa1-wheel` |
| The wheel's face trigger (10.2) | LT (rebindable key + controller button) in the open wheel plays the slot's face animation together with its body animation; at the centre the neutral face animation only | - | `094a-gsa1-wheel` (+ `GSA1.Faces` of `094a-gsa1-faces`) |
| The list with internal names | a generated table of names and numbers (from the TSVs) in libsa1, frames from the disc at run time; the module lists it without a world | `094a-lib-emote` | `094a-gsa1-wheel` (the Emote wheel page) |

**No decompilation is needed** (faces_events.md 0.1, emote_wheel_lib.md 2.5): every function is C in SADC and lowered into every
world; the event-library asm wrappers are not used (libsa1 calls the C bodies they jump to). So there is no staging agent.

**Agents: 4 added, 9 in the round (the cap).**

```
LIB, in parallel:   094a-lib-anim (+ the contract of 5.3 / 5.4 in its first step) | 094a-lib-gold | 094a-lib-audio
                    | 094a-lib-faces | 094a-lib-emote
G-SA1, in parallel: 094a-gsa1 (Super, unchanged) | 094a-gsa1-faces | 094a-gsa1-wheel
then:               094a-verify (super2_plan.md 6.5 + this addendum's 6.7) -> release 0.9.4a
```

The new LIB lanes write only new files; every shared LIB file stays `094a-lib-anim`'s (it lands the hooks the lanes need as part
of its first step, exactly as written in 5.3 / 5.4). The G-SA1 agents share no file (`tests/lua_mock.lua` stays `094a-gsa1`'s;
`cl_character.lua` is `094a-gsa1-wheel`'s small patch; `core_test.cpp` / `cl_warm_census.lua` are `094a-gsa1-faces`'). If the
orchestrator needs a slot (e.g. for `objects2_plan.md` lanes), 4.9 says which agents fold together.

**Every deviation is host-only and option-gated:** a world that never gets a face or emote call runs byte-for-byte as before, so
every strict reference list replays unchanged by construction; the new tests prove "no call = unchanged".

---------------------------------------------------------------------------------------------------------------------

## 1. What the owner gets (plain words)

* **Event faces.** Q menu > Options > G-SA1 > **Faces**: a switch "Event faces". On: your character always wears the cutscene head
  and blinks like in the game's cutscenes. Off (default): the normal gameplay face; expressions and talking still show the
  cutscene head while they play and then go back. A key can flip the switch.
* **Your own face animations.** On the same page, a list of the game's 31 face expressions (neutral, blink, talking, surprised,
  happy, sad, eyes closed ...; Big shows the 14 his face can do, E-102 and Super Sonic have none in the game). Double-click one
  and it goes into your animation; each entry has two numbers: how many frames to **enter** the expression and how many to
  **hold** it (60 frames = 1 second). Move entries up and down, remove them, preview on your character, give the animation a name
  and save as many as you like. Ten optional face hotkeys play them.
* **Lip sync.** Hold your voice chat key (X) and talk: your character's mouth opens with your voice, like Garry's Mod player
  models. If Garry's Mod does not report your own voice level in single player, G-SA1 listens to the microphone itself (only how
  loud it is; nothing is recorded or sent), and only while you hold the key. A meter on the Faces page shows it working.
* **The emote wheel.** Hold **TAB** (controller: **D-pad down**): a ring of 8 slots appears in the middle of the screen while the
  game keeps running (no slow motion). Point with the mouse or the right stick (the camera does not turn), let go: your character
  plays that animation. Let go in the middle: nothing. Up to 4 pages (mouse wheel / LB / RB). Each character has his own wheel.
* **Faces in the wheel.** Each slot can also have one of your face animations. While the wheel is open, press **LT** (keyboard:
  **left mouse button**; both can be changed): the slot's face animation plays together with its body animation and the wheel
  closes. With nothing highlighted, LT plays only the wheel's "centre" face animation and your character keeps doing what he does.
* **Choosing what is on the wheel.** Q menu > Options > G-SA1 > **Emote wheel**: pick a character, click a slot (or the centre) on
  the little wheel, double-click an animation in the list - every animation of that character by its internal name (like
  `s_kimepose0`) with its number and length - and pick a face animation for the slot. "Play" shows it on your character.
* **Nothing else changes:** without using these, every character plays exactly as in 0.9.4. While an animation plays, the game
  keeps running underneath (gravity, water, getting hit, the idle voice); moving, jumping or the action button ends it at once.

---------------------------------------------------------------------------------------------------------------------

## 2. State and facts

### 2.1 Versions and running work (2026-10-26 evening)

* **Released and installed:** G-SA1 0.9.4 (18:24; LIB snapshot 7203ffbe...). **Running:** the 0.9.4.1 fix
  (`gsa1_fix_0941.js`: g-sa1 objects files only; LIB unchanged) and the objects2 prep (`objects2_plan.md`, "lanes for 0.9.4a if
  they fit"). **Planned:** 0.9.4a = `super2_plan.md` (5 agents, workflow `gsa1_round_094a.js` ready, not launched) + this addendum.
* **BASE of every agent here:** the released 0.9.4, or 0.9.4.1 if it is released before 0.9.4a starts (its files are g-sa1's
  objects feature, which no agent of this addendum edits). The round-start copy `SADC/build/research/super2/base094/` of
  `super2_plan.md` 3.3 is the LIB snapshot the new lanes may build their scratch probes against.
* **Research copies:** `SADC/build/research/faces094a/lib/` (0.9.4 snapshot, libsa1.a sha1 7203ffbe3330b535fa525af0ea8b6f71aaf9d0ac,
  res-emote) and `SADC/build/research/faces094a/res_faces/lib/` (0.9.3a, sha1 e1cd5fdec684466542de02d954daa60c2e5f28b1, res-faces).
  Both are read-only for the round's agents.

### 2.2 What the research established (the facts this addendum builds on)

| # | Fact (source) | Consequence |
|---|---|---|
| 1 | Every face function is C, matched (8C04BC70 NON_MATCHING, functionally complete) and lowered; nothing stubbed. EV_SetFace 8C04B39E / EV_ClrFace 8C04B3B4 are 22 / 14 B of asm that only add `if (tp)` and `tp->twp` (faces_events.md 2.1, 5.1) | libsa1 calls 8C04BBCE / 8C04BD60 / 8C07B2DC directly; no staging |
| 2 | The face block reader 8C04BCC8: when `++face_count >= face_time`: `face = face_next`, `count = 0`, at a sequence end (`face_seq` NULL or its time byte 0) the next letter from the script (8C04BC20 -> 8C04BC70), then reads the step `{Sint8 time, Sint8 key}` (`code_8C04BC70.c:91-112`); 8C04BBCE starts a stopped block with `time 1, count 0, face = face_next = 0` (`code_8C04B7BA.c:260-273`) | a host step buffer as `face_seq` plays any timing the game's step format can express; a step's time is 1..127 (Sint8) |
| 3 | A face = the EV head linked in place of the static face (one sibling pointer), its vertices blended between shape keys; no textures, no node motion; the data is in 1ST_READ and the textures in the archives libsa1 loads; the hurt face 'I' already shows in G-SA1 (faces_events.md 3, 5.1) | nothing new to load; the render path already carries it |
| 4 | Keys: Sonic 21, Tails / Knuckles / Amy / Tikal 20, Big 8 (6 distinct face shapes: the wiki's "6 faces"); valid animations: all 31, Big 14 (A B C D E K L S T U V W X Y, mask 0x01FC0C1F); Big 'G' faults the world in 8C055EFE; E-102 has no face task; Super Sonic (kind 8 and the General form while Super) has no EV head (faces_events.md 4) | validation by a mask computed from the data; refusals, never a fault |
| 5 | The draw precedes the face task in a step: a face change shows one pair after it is applied; event faces have priority over gameplay faces (a hurt face waits, then plays stale unless PFaceCancel 8C07B2DC); a finished script idles on rand() (5.4) | PFaceCancel when host face activity ends; the idle is EVENT mode only |
| 6 | Lip sync L2: each pair `face / face_next` = two neighbours on the mouth ladder (0-5-4-6-7; Big 0-4-5), `face_time 64`, `face_count = round(r x 64) - 1`, the reader parked on a held step `{127, face_next}`: drawn exactly one pair later (rms 0) (6.6) | the default lip-sync style |
| 7 | The cutscenes animate a live player through the event action queue, with the player in his ordinary standing mode and the physics running; the display draws the queue head with 8C04B57C instead of the gameplay action; identity test: 1,300 pairs, emoting 1,110: state and rand identical (emote_wheel_lib.md 2, 4.5) | the emote mechanism; nothing written into game state by the host |
| 8 | 634 of 671 table rows play at first try; 2 faults, both a blend between objects of different node counts (73 -> 74 nodes); an arbitrary motion crashed the process (Amy's barrel motion) (4.1) | link only between entries of one object; only table rows and a validated extras list are accepted |
| 9 | The queue advances once per pair for Sonic, Tails, Knuckles, Big (and kind 8: Sonic's display) and twice for Amy and E-102 (4.3) | speed 2 x racio vs racio |
| 10 | Super Sonic: kind 8 needs the Super body (0C909D2C) / ball (0C90BD04) for Sonic's objects and the SUPERSONIC texlist 0C901EA0 (`sa1_manifest.c` `k_super_subst`); the General form takes `plactptr` (GT / GA) as is; 130-134 need GA's lifted copies (4.7) | the substitution from `sa1_manifest_char(8)->subst`; a getter for GA's lifted actions from `094a-lib-anim` |
| 11 | GMod's own mouths: `IsSpeaking() and clamp(VoiceVolume() x 2, 0, 2)`; the local player's level in single player is UNVERIFIED; GMod's 64-bit `bass.dll` exports the BASS recording API; G-SA1 already binds that dll by `GetModuleHandleW` (faces_wheel_ui.md 1.2, 5) | the Automatic source: GMod's level, else the module's own microphone level |
| 12 | The core material census lacks the EV-only states (Sonic 25-27, Tails 19-20, Knuckles 2-4, Amy 9-10, flags 0x30); `cl_super.lua` copies the core census for Sonic at load (`S.coreSonic`) (faces_events.md 5.3; faces_wheel_ui.md 7.3) | a face segment in core_test's census and a regenerated `cl_warm_census.lua` (no `cl_super.lua` change) |
| 13 | LIB compiles every `src/*.c` and `src/world/*.c` by glob (`build.py:430-431`); world features keep their per-world state in segment 0x18 validated by magic (`sa1_world_set.c` `st_state_of`) and their hooks in `sa1g_world`, which survive a rebuild (`sa1_world_image.c` resets the segment, not the hooks) | the lanes' design (5.4); a new .c file must compile the moment it appears in `src/` |

### 2.3 What this addendum changes in `super2_plan.md`

* `094a-lib-anim` gets a task 1e (the contract of 5.3 / 5.4, in its first step) and a test case (19): 6.1.
* `094a-gsa1` runs in parallel with the two new G-SA1 agents (no shared file) and keeps `tests/lua_mock.lua`: 6.6.
* `094a-verify` gets the checks of 6.7; the release step the lines of 6.8.
* `super2_plan.md` 5.1's order and 5.2's table are extended by 5.1 / 5.2 here; the header contract 5.3 by 5.3 here; the internal
  contract 5.4 by 5.4 here. Nothing of the Super Sonic polish changes.

---------------------------------------------------------------------------------------------------------------------

## 3. Rules for the addendum's agents

### 3.1 PRE-094A-FW (paste after PRE-094A for 094a-lib-faces, 094a-lib-emote, 094a-gsa1-faces, 094a-gsa1-wheel)

```
ADDENDUM (SADC/docs/research/faces_wheel_plan.md) to round 0.9.4a: the owner's request of 2026-10-26 (rounds plan 14.1s;
super2_plan.md 10.2, binding). EVENT FACES: "animatable, easy to use, a switch to toggle between event faces and regular faces,
microphone support (like how Garry's Mod can lip sync speaking into microphones)". EMOTE WHEEL: "a holdable button that will open a
radial menu (like GTA V's weapon wheel without the slow-mo) that when a hold is let go, it will play the selected animation ...
swappable with a menu in the G-SA1 options tab that lists all the animations with their internal names (custom naming for easier
picking will come later) ... unique per character". 10.2 (CORRECTED): each wheel slot = a BODY animation + an optional FACE
animation; the Left Trigger (rebindable: a key AND a controller button) in the open wheel plays the highlighted slot's face animation
TOGETHER WITH its body animation, at the centre ONLY the neutral face animation; a face animation is a user-made sequence of the
game's face animations, each entry with frames to ENTER and frames to HOLD, made on an Options page. The Super Sonic polish
(super2_plan.md) is the same round: never touch its agents' files (super2_plan.md 5.2; this addendum's 5.2).
READ faces_wheel_plan.md sections 0-5 and YOUR section in 6 (and its "## Review (adversarial)" if present: it wins), super2_plan.md
3.1-3.3 and 5, and the research your text names. Where faces_events.md, emote_wheel_lib.md or faces_wheel_ui.md disagree with
faces_wheel_plan.md, the plan wins (appendix A maps their placeholder names to the final ones). Research probes and their copies:
SADC/build/research/faces094a/res_faces/ (probe_faces.c, static_faces.py, big_keys.py, out_*.txt) and SADC/build/research/
faces094a/ (probe_emote.c, scripts/, out/): copy what you need into your own scratch folder (no '-' in paths); never edit them.
NO DECOMPILATION: every function this addendum calls is C in SADC and lowered into every libsa1 world; the event-library asm
wrappers (8C04B39E, 8C04B3B4, 8C04A120, 8C04A142, 8C04A168) are not used - call the C bodies they jump to. SADC is not edited.
DEVIATIONS: the host writes into the face block (faces_wheel_plan.md 4.2) and every emote call happen only in a world that gets one
of the new calls; the reference worlds never do, so every strict list replays bit-exact by construction, and your tests show "no
call = unchanged". GAME BYTES: names and numbers only (internal motion names, SA Tools labels, action / extra numbers, addresses,
letters); never a face table, step data, key data or motion bytes in a repo or the addon - libsa1 reads them from the disc at run
time. OWNER UI RULE: every option is a ConVar with a control on a Q-menu page (Options > G-SA1); console commands are extras.
```

### 3.2 Builds, new files, reports

* `super2_plan.md` 3.2 applies (report schemas, file writing, the build lock, `--lock-timeout 1800`, absolute `--gdi` / `--maps`,
  the build governor). Five LIB agents build at the same time in this round: builds are serialised by the lock; a failure in a
  file you do not own: wait and retry, report it if it persists. Use `--clean` only in your final gate run.
* **New source files appear in `src/` only when they compile** (LIB and g-sa1 compile their source folders by glob): develop in
  your scratch folder and copy a file into the tree when it builds; a half-written file in `src/` breaks every other agent's build.
* **The LIB lanes wait for the contract** (5.3 / 5.4): until `094a-lib-anim` reports its step 1 (or `src/world/sa1_world.h` has
  `SA1W_HAS_LANE_HOOKS` and `include/libsa1.h` has `SA1_API_FACE` and `SA1_API_EMOTE`), work in scratch only.
* New C files compile in every LIB target (`--target all`: x64 / x86 Windows, Linux) and with `SA1_NO_WORLD` (the asset-only
  build): the public functions then return `SA1_ERR_NOT_IMPLEMENTED`, as `sa1_character_add_rings` does.
* Reports: `{done[], verified, not_done, api_notes, files_changed[]}`; keep `api_notes` at most about 4,500 characters (the workflow
  passes a slice of it to the G-SA1 agents) and put the full API notes as a comment block at the top of your test file.
* Owner-checklist lines go into `api_notes` (README and versions belong to the release step).

---------------------------------------------------------------------------------------------------------------------

## 4. Decisions this addendum takes (planner defaults, not owner questions)

### 4.1 Reconciling the three research documents and `super2_plan.md` 10.2

| Point | faces_events.md | emote_wheel_lib.md | faces_wheel_ui.md | This plan |
|---|---|---|---|---|
| Face API | `sa1_character_face_mode / _face (script) / _face_level`, `sa1_face_info_get` | - | `FaceInfo / FaceMode / Face / FaceStop / FaceLevel / FaceState` (placeholders) | those four LIB calls **plus `sa1_character_face_sequence`** (the owner's enter / hold sequences, 10.2) and three more `sa1_face_info` arrays; module names in 5.6 |
| Emote catalogue | - | grows `sa1_action_info` with names / class | `EmoteList(kind, form)` without a world | **a separate list** `sa1_emote_count / sa1_emote_get(kind, form, i)` (rows and extras in one list, no world): `sa1_action_info` and `sa1_character.c` stay `094a-lib-anim`'s |
| Wheel slot | - | a slot = a row or an extra | slot kinds anim / face / cmd, page 2 = faces | **a slot = a body animation (row, extra, `(stop)` or none) + an optional face animation**; the wheel's centre has a face animation; no face-only page (10.2 replaces it) |
| Release / click | - | release plays | release plays; left click / A = play now; right click / B = cancel | release = body only; **face trigger (LT / left mouse button) = face + body, at the centre the neutral face only, then the wheel closes**; right click / B = cancel; no separate "play now" |
| Face picker | - | - | a 31-row list, play / hold, a letter-string box, hotkeys with letters | the 31-row list feeds **the face animation maker** (sequences of entries with enter / hold); hotkeys play saved face animations; no letter-string box in the UI (the game's script call stays in the module for tests and 2.0) |
| Lip sync | L2 ladder (default), L1, talk | - | sources Automatic / GMod / microphone, smoothing, push-to-talk | as faces_wheel_ui.md 5 with L2 in libsa1; the priority rule is enforced in LIB (4.4) |
| Render follow-ups | hidden pairs draw the static face (CPU pose) | the hint and the CPU pose should follow the queue head | - | **deferred** (4.8): measured deviation acceptable, hidden pairs rare; risk rows in 11 |
| Ownership | a new `sa1_world_face.c` + a hook patch | `sa1_world_emote.c` + patches of `sa1_character.c`, `sa1_api_world.*`, `sa1_world_tick.c` | new g-sa1 feature files + two `cl_character.lua` seams | the lanes write **new files only**; every shared LIB file is `094a-lib-anim`'s, which lands the hooks (5.4) |

### 4.2 The face mechanism, exactly (faces_events.md terms)

The host takes the place of a cutscene's face script. Per world, the face lane keeps a block `sf_state` in segment 0x18 (magic and
self like `sa1_world_set.c`'s `st_state`; made at the first face call, made again after a rebuild): the latched request (the last
call of each kind before a pair), the latched level and style, the activity, the held key, two script buffers (66 bytes each,
alternating), two step buffers (`{Sint8 time, Sint8 key}` pairs, alternating, each ended by a zero step), a held step `{127, k},
{0, 0}`, the lip-sync held step `{127, k}, {0, 0}`, the close step `{6, 0}, {0, 0}`, the two terminator scripts `""` (EVENT) and
`"0"` (GAME), and (only with the cutscene pace) the 31 doubled sequences with the 31 table pointers they replace. The alternation
guarantees that a pointer the game's reader still holds (`face_script`, `face_seq`) is never overwritten in the same pair.
`sa1g_lane.persist[0]` keeps the mode across rebuilds.

Names: `P` = player 0's taskwk (`playertwp[0]`), `F` = its face block, `W` = its face work (`playerpwp[0]->pfp`).
`interrupt(script, seq)` (the NOW of faces_events.md 6.3, refined):

```
if F.face_time == 0:  8C04BBCE(P, script)          -- the game's start: time 1, count 0, face = face_next = 0
else:
    if F.face_count * 2 < F.face_time: F.face_next = F.face   -- the nearer of the running step's two keys (below half: its
                                                                  start key), so the snap is at most half a step's blend
    F.face_count = F.face_time - 1                 -- the running step ends at the next step, on that key
    F.face_script = script
F.face_seq = seq                                   -- NULL: the reader takes the script's first letter at the step's end
```

**At every pair start** (`face.pre`, sub step 0, before the executor; every other sub step: nothing), in this order:

1. **Not ready:** no `P`, no `P->ewp`, or no `W` (E-102: never; a world right after a rebuild: until PInitialize ran): keep
   everything latched, do nothing. (review R.8) `P` is read again at every pair start: when it is not the last pair's (a
   re-created player task), activity and hold are dropped (NONE) and the applied mode is applied again to the new face block.
2. **Mode** (when the latched mode differs from the applied one): EVENT on: `8C04BBCE(P, "")` when `face_time == 0`; EVENT off:
   `8C04BD60(P)`, `8C07B2DC(0)` (PFaceCancel: a gameplay face that waited behind the event face is dropped), activity and hold
   cleared; PACE_CUTSCENE on (EVENT only): the world's `D_8C19C32C[0..30]` point at the doubled copies (every time x 2, all <= 120);
   off, or EVENT off: the original pointers back.
3. **Request** (the last latched one):
   * STOP (`sa1_character_face(NULL / "")`, `count 0`): **end** (below).
   * SCRIPT: copied into the next script buffer; GAME mode without '0': one appended; without NOW: `8C04BBCE(P, buffer)` (the
     game's queue: it starts when the running animation ends); with NOW: `interrupt(buffer, NULL)`. Activity SCRIPT; with LOOP:
     HELD (re-armed in 5). (review R.3) With LOOP no '0' is appended, and a LOOP script that contains '0' is refused
     (SA1_ERR_INVALID_ARGUMENT): the reader takes the next letter only at a sequence end, so an appended '0' is read at the end
     of the first pass (8C04BC20 -> -1 -> 8C04BC70 -> 8C04BD60, `code_8C04B7BA.c:289-290`, `code_8C04BC70.c:101`) while
     `face_script` still points at that '0' - the re-arm of 5 never sees the script's end and a GAME-mode loop (and the TALK
     lip-sync style) stops after one pass. A looping script ends with the next face call (End below). A script has ended when
     `face_script` is NULL (8C04BC20 dropped it: EVENT idles) or `face_time` is 0 (GAME).
   * SEQUENCE: the entries become the next step buffer (4.3); `interrupt(terminator, buffer)` with terminator `""` (EVENT) or
     `"0"` (GAME). Activity SEQUENCE; the hold key = the last entry's last key when HOLD_LAST.
   * (review R.4) **Over a running gameplay face:** when a SEQUENCE, a NOW SCRIPT, EVENT on or lip sync STARTS while the face
     work runs a gameplay sequence (`W.seq != NULL`: the face task is in its modes 2 / 3, `code_8C07B080.c:176-210`, where it
     advances the face block every step but keeps showing the gameplay face until that sequence ends - 32 steps of 'I'), call
     `8C07B2DC(0)` first: the face task drops it (mode 3 -> 2 -> 1) and shows the host face two steps later instead of
     skipping its first steps unseen. Nothing else changes the game's own gameplay faces.
4. **Lip sync** (the latched level `L`, style):
   * `L >= 0` and the activity is not SEQUENCE and not a NOW SCRIPT that still runs: LADDER - `F.face = ladder[i]`,
     `F.face_next = ladder[i + 1]`, `F.face_time = 64`, `F.face_count = round(r x 64) - 1`, `F.face_script = NULL`,
     `F.face_seq` = the lip-sync held step `{127, ladder[i + 1]}` (`p = L x (n - 1)`, `i = floor(p)` clamped to `n - 2`,
     `r = p - i`); TALK - at the first talking pair the script `"CDE"` with NOW | LOOP (as 3). Activity LIPSYNC.
   * `L < 0` and the activity is LIPSYNC: `interrupt(terminator, close step {6, 0})` - the mouth closes over 6 steps, then the
     terminator (EVENT: the idle; GAME: the regular face). With a hold key: the comeback (5) instead of the terminator.
5. **Upkeep:**
   * a HOLD_LAST SEQUENCE whose reader has reached its last step (the held step `{127, key}`): activity HELD;
   * any other SEQUENCE / NOW SCRIPT that ended (`face_time == 0`, or `face_seq` outside its buffer): activity NONE; GAME mode:
     `8C07B2DC(0)` ONCE, on that transition (review R.6: never on a pair that merely is NONE - G-SA1 gives every world it
     creates face calls, so a per-pair cancel would erase every hurt face of every character).
   * HELD with a hold key: when `F.face_seq` points right after the held step, set it back to the held step (the step re-reads
     itself: the game's way of holding a face, faces_events.md 6.4); after lip sync: the comeback = a step buffer `{8, key},
     {127, key}` through `interrupt`, then held.
   * HELD by a LOOP script: `8C04BBCE(P, the same buffer)` when its reader took the last letter (`face_script` NULL or at its 0).
   * EVENT mode, `face_time == 0` and nothing else active: `8C04BBCE(P, "")` (keep-alive: the idle).

**End** (STOP, or the sequence's natural end): EVENT: `interrupt("", NULL)` (the idle); GAME: `8C04BD60(P)`, `8C07B2DC(0)`.

**The fill** (`face.fill`, from `sa1w_world_pstate`): `face_mode` = the applied mode; `face_shown` = `W.parent->sibling ==
W.object` AND the tree holding `W.parent` is the one the display draws (review R.5: not for kind 0 while the General look is
on - `w->sg.look`: the face task keeps linking Sonic's EV head into his normal tree, which is not drawn while Super, so the
link test alone says 1 there, faces_events.md 4.4 - and not while the event action queue's head entry draws another object
than the main body: the ball, the curled body, Tails' / Knuckles' flight model) (the EV head is in the drawn tree after the
pair; it is drawn from the next pair); `face_from / face_to / face_rate` =
`F.face / F.face_next / F.face_count / F.face_time` while `face_time != 0`, else the face work's `face / face_next / rate` while
a gameplay face blends (modes 2 / 3), else -1 / -1 / 0; `face_letter` = the face animation whose steps the reader is in (a
pointer into `D_8C19C32C[n]` or its paced copy: n; inside a step buffer: the entry's animation from a parallel byte map; a held,
lip-sync or close step: -2), else -1; `face_activity` = `SA1_FACE_ACT_*` (GAMEPLAY when the face work runs a sequence).

Host-only writes (documented in the header, faces_events.md 6.1 plus 10.2): the interrupt's three fields, `face_seq` pointing at
a host buffer, the lip-sync writes, the paced table pointers. Everything else is the game's code called the way an event calls
it; the reader, the blend (8C07B080 / 8C055EFE), the link of the EV head and the draw are untouched.

### 4.3 Face animations (the owner's sequences): semantics and units

* **Unit: game frames = 60 Hz steps** (the face table's own unit: the game advances a face once per logic step). The UI says
  "frames (60 = 1 second)" and shows seconds beside every number.
* **An entry `{animation, enter, hold}`** plays one of the 31 face animations with the owner's timing, expressed as the game's own
  step data:
  * **enter** = the time of the animation's FIRST step - the blend from whatever face shows into its first key - 1..127 frames;
    0 (the default) = the game's own first step time (`sa1_face_info.first[n]`);
  * then the animation's other steps with the game's times (most expressions are one or two steps; talking and blinking are
    little animations of 3-9 steps, which keep their game timing);
  * **hold** = 0..3600 more frames on the animation's LAST key (`sa1_face_info.last_key[n]`), written as steps `{<= 127, key}`.
  * The UI shows per entry: "enter 10 + plays 0 + hold 90 = 100 frames (1.67 s)" (the middle number is `steps[n] - first[n]`).
* **A face animation** = 1..32 entries, a name, "back to neutral over N frames" (default 8 = the game's 'A'; 0 = none: G-SA1 then
  appends no entry; else it appends `{A, N, 0}`, so the face blends back to the neutral key before the switch's face takes over)
  and "keep the last face" (HOLD_LAST: the last key stays until the next face command; then "back to neutral" is not appended).
  (review R.10) The appended `{A, N, 0}` is one of the call's entries: a face animation with "back" has at most 31 entries of
  its own (SA1_FACE_SEQ_MAX - 1), else libsa1 refuses the whole call; the maker and face_anims.txt enforce it.
* **Start:** always NOW (the face trigger, a hotkey and Preview want it at once): from the face that shows, with the snap rule of
  4.2 (at most half of the running step's blend).
* **Pace:** the cutscene pace (an option, default off) doubles the game's steps inside entries, never the owner's enter / hold.
* **Validation (whole call refused, nothing applied):** count 0..32 (0 = stop), animation 0..30, enter 0..127, hold 0..3600,
  every animation in the character's mask (Big: 14). The step buffer holds at most 32 x (18 + 29) + 2 steps (Q has 18 steps; a
  3600-frame hold is 29 steps): 1,536 steps per buffer.

### 4.4 Which face wins

LIB enforces one rule (so G-SA1, the hotkeys and 2.0's journal all see the same behaviour): **a running face animation (SEQUENCE)
or NOW script > lip sync > a held face (HOLD_LAST, a LOOP script) > the switch's base (EVENT: the idle; GAME: the regular face).**
So a face animation interrupts talking and talking resumes the pair after it ends; talking covers a held mood, which blends back
(8 frames) when the level goes off; a new face animation replaces the previous one. Gameplay faces keep the game's priority: while
any host face activity runs, a hit's 'I' waits behind it (the game's event priority) and is dropped (PFaceCancel) when the activity
ends; with event faces ON a hit shows no hurt face (the game's events behave the same; owner question FQ8).
(review R.4) The rule needs the PFaceCancel at a host START of 4.2 step 3: the game's face task by itself shows a gameplay face
that is already running to its end and lets the face block run on unseen meanwhile (a face animation pressed half a second
after a hit would lose its first half).

### 4.5 Emotes: the mechanism and its rules (emote_wheel_lib.md 5.1 with these decisions)

* **Start** (latched, applied by `emote.pre` at the next pair start, sub step 0, after the pad update and before the executor;
  before the world's first pair: kept until after it, because 8C04BF00 needs `jvmode == 1`): refuse when not standing (mode 1;
  kind 8: 71; or the held-object flag 0x800, hurt `wtimer != 0`, the board) unless IN_AIR (then only hurt refuses); `8C04B48C`
  (clear: no stale blend source, no game leak); per item: a table row - kind 8 with an object of Sonic's: `8C04B528(twp, the
  substituted object, the row's motion, 0x0C901EA0, speed, mode, link)` from `sa1_manifest_char(8)->subst`; the General form,
  rows 130-134: the lifted action of `sa1w_world_super_lifted` (5.4); otherwise `8C04B4D2(twp, plactptr[row].actptr, NULL, speed,
  mode, link)` (`plactptr` = GT / GA in the General form); an extra - `8C04B528(twp, the kind's main body, the extra's motion,
  NULL, ...)`; Amy's attack rows (81-95) - `8C0DE46C(0, 1.0)` unless NO_PROPS, undone with 0.0 at the end.
* (review R.8) **jvmode at EVERY start**, not only before the world's first pair: a start (and the queueing of a later group,
  R.1) happens only at a pair start that reads `playerpwp[0]->mj.jvmode == 1`; else the request stays latched. 8C04BF00 spins
  for ever for any other value (`code_8C04BF00.c:12-13`, the `for (;;)` at its end): a hang of the host thread that no fault
  handler catches. A re-created player task (PInitialize sets 0, `code_8C06C300.c:77`) drops the running emote.
* (review R.7) **Heap headroom:** every queue entry is a `CAlloc` from the game heap and 8C04B4D2 / 8C04B528 write through its
  result unchecked (`MAlloc` returns NULL when the heap is full, `code_8C098998.c:99-158`): before queueing, the largest free
  block of the heap's free list (`B_8C79F03C`, walked as `sa1_world_set.c` `st_heap_largest` does) must exceed 4 KB plus the
  entries; else refuse with SA1_ERR_LIMIT ("the game's memory is full") and queue nothing.
* (review R.16) Refusal reasons in plain words: "in the air", "hurt", "carrying something", "on the board", "moving" (on the
  ground, not in the standing mode: walking, running, rolling), "busy" (any other state), "the game's memory is full".
* **Speed** = `racio x 2` for kinds 0, 2, 3, 7, 8 (their displays advance the queue once per pair) and `racio` for 5, 6 (twice);
  rows with `mtnmode` 9 / 10 / 11 (speed-driven: walks, runs, the ball): 1 frame per step (x 2 / x 1 the same way); 12 (caller-set:
  ladders, monkey bars): 0.5 per step; times the item's multiplier. **Link** = 0 for the first item, the emote's link (default 8)
  between items of the same object, 0 across objects (the measured 73 -> 74-node fault). **Mode bit** = loop for the last item when
  it plays LOOP (a row's default: loop rows loop, once and hold rows once). **CHAIN** = the row's `next` chain up to its loop
  (75 > 76 > stand). A once emote gets a trailing stand entry (the form's row 0 from `plactptr`; kind 8: 130 on the Super body),
  link 8 when it has the emote's object, so it blends back (end pop 0.00, measured).
* (review R.1) **Never an unblended entry behind another entry in one queue.** 8C04B57C's else branch sets `buf44` to the
  finished entry WITHOUT freeing the buf44 it held (`code_8C04B440.c:203`; only the link branch frees it, `:175`), so every
  transition into an entry with linkframe 0 leaks one entry of the game heap although every start clears: measured 48 bytes
  per transition (review probe `SADC/build/research/faces094a/review/out/probe_leak.txt`: row 3 then the stand with link 0, or
  82 then 83 with the link rule's 0, lose 480 bytes in 10 runs; with link 8, or one item, 0). So the lane splits an emote into
  GROUPS at every unblended transition (another object; link 0; CHAIN items across objects; the trailing stand of another
  object) and queues one group at a time: when the queue has run empty (`list40` NULL in the post hook) it calls 8C04B48C
  (which frees `buf44`) and queues the next group in the same post hook, so the next pair draws it (no gap). STOP_BLEND on a
  loop of another object than the stand: at the loop's wrap (the queue frame smaller than the last pair's) clear and queue the
  stand as its own group. At the natural end (the queue empty, no group left) 8C04B48C too, so the heap is back to its start.
* **Each pair:** before it (sub step 0, after the pad update): cancel (8C04B48C) when `GetAnalog(twp, NULL, NULL)` (8C06CD22) is
  non-zero or the pad's press has the jump / action masks (`D_8C19E38C` / `D_8C19E390`) - unless KEEP_INPUT; after it (`emote.post`,
  sub step 1): cancel when the gameplay left the standing state, `wtimer != 0`, the held-object flag changed, the form changed
  (equipment 0x8000 or the look), or the board; the queue empty: the emote is over (Amy's hammer hidden). (review R.15) With
  IN_AIR only the standing-state rule is dropped: the form change, the board and the held-object change still cancel (the
  queued objects and texlists belong to the old form; a pose with a prop picked up mid-emote would hold nothing).
* (review R.17) **The queue moves only on pairs the character is drawn:** 8C04B57C runs inside the display, which Sonic,
  Tails, Knuckles and Big skip when the visibility test 8C038D00 fails - and live worlds test against the host camera (the
  camera adapter, `sa1_world_tick.c` `sa1g_camera_view`). An emote of a character outside the Garry's Mod view pauses and
  goes on when he is seen again; the lane uses no pair-count time limits, and the tests' worlds keep the character in view.
* **Stop:** NOW = 8C04B48C at the next pair start; BLEND = the stand entry appended after the current item (a loop finishes its
  cycle, then blends).
* **The catalogue** (no world): one list per (kind, form) of every OFFERED entry - table rows that are in G-SA1's worlds (not the
  stage / MINICART / empty rows; kind 8 not the curled-body rows 14, 15, 83, 125; Super-only rows 130-141 only in the Super forms)
  and the validated extras (the TSVs' `wheel = yes` extras) - with internal name, SA Tools label, class, flags, frames and play
  mode (rows' frames and modes from the loaded character set, `sa1_action_info_get`; names, labels, classes, the extras'
  addresses and (review R.2) the extras' frame counts from a generated table of names and numbers - `sa1_load_disc` frees every
  image it read (`sa1_loader.c:601-602`) and parses only the action table's motions, so no extra motion is left to read
  without a world; emote_test E12 checks each count against the disc). Duplicates (same DRAWN object and motion, kind 8's
  substituted body included) carry `dup_of` - (review R.9) never the same internal name: names are not unique (Knuckles'
  `k_panch0` is rows 39 and 54, two different motions; `s_kimepose0` is rows 75 and 139; `ss_move2` rows 133 and 134). Anything
  not in the list is refused.
* **Not done by the host:** no pad-off, no SetInputP, no event-running flag (`B_8C7492AC`), no camera - an emote is the
  cutscene's animation call only.

### 4.6 The wheel and the face trigger (G-SA1; faces_wheel_ui.md 2-3 with 10.2)

* **Per entry of `GSA1.CHARACTERS`** (8: sonic, tails, knuckles, amy, gamma, big, supersonic, supersonic_general): a wheel of up
  to 4 pages x 8 slots (slot 1 up, then clockwise) and a centre. A Sonic who turned Super keeps Sonic's wheel (his entry is sonic;
  his rows play on the golden body through GT).
* **A slot** = body (an offered row `a<n>`, an extra `x<n>` with its internal name, `(stop)`, or none) + face (a face animation
  name, or none) + options (`loop`). **The centre** = a face animation (or none).
* **Hold the wheel key** (TAB / D-pad down): open. **Release**: the highlighted slot's body animation (none: nothing; `(stop)`:
  EmoteStop; the playing one again: STOP_BLEND, emote_wheel_lib.md E4). **Face trigger** (left mouse button / LT, each rebindable)
  while open: the highlighted slot's face animation and body animation together, or at the centre only the centre's face
  animation; the wheel closes; the later release plays nothing (one press, one play). An entry without faces (E-102, both Super
  Sonics): the trigger plays the body only.
* **Selection:** the mouse moves a virtual cursor (the view does not turn); inside 0.35 of the radius = the centre. The right stick
  points (dead zone 0.5); when it returns to rest the slot stays lit for `gsa1_wheel_stick_keep` seconds (0.5; 0 = until another
  direction, the GTA way), then the centre - so a controller player can reach the centre for the neutral face. Keys 1-8 light a
  slot. Hysteresis 4 degrees. Right click / B cancel.
* **Nothing leaks:** `InputMouseApply` (mouse -> cursor), the `GSA1.PadView` wrap (no camera turn, no stored turn), the chained
  `GSA1.FilterButtons` (presses that start while open never reach the game; a charge held before opening goes on), the wheel key's
  own GMod bind blocked while a character exists (TAB = scoreboard otherwise), the mouse wheel through the new seam
  `GSA1.BindOverride` (pages, not the camera distance), `GSA1.FilterStick` for "stop while choosing" (option).

### 4.7 Lip sync (faces_wheel_ui.md 5 with faces_events.md 6.6)

Sources Automatic (default: GMod's voice level when it reported one in the last 0.5 s of talking, else the module's microphone),
GMod voice chat, the microphone (BASS recording through GMod's own `bass.dll`, resolved by `GetModuleHandleW` + `GetProcAddress`,
16 kHz mono float, 10 ms blocks, one loudness number kept). Push-to-talk by default (the `+voicerecord` key, X for the owner):
capture paused between presses. Smoothing in Lua (gain 2 = GMod's x 2, gate 0.05, rise 30 ms, release 0.12 s, hang 0.6 s), one
`gsa1.FaceLevel` per frame, -1 after the hang. Style LADDER (default) / TALK. Estimated 80-130 ms behind the voice (UNVERIFIED).

### 4.8 Deferred (not in 0.9.4a; risk rows in 11)

* The CPU pose of a hidden pair draws the static face (faces) and the gameplay pose (emotes); the interpolation hint reads the
  gameplay action while an emote plays (measured mid-pair deviation <= 0.81 against the gameplay's own 1.20: no visible error).
  A later round extends `094a-lib-anim`'s hint helper with the queue head.
* Faces / emotes for other players (puppets, 1.0.x), the recorder's journal (2.0 M0: every new call is already latched at the
  pair start, `v1_roadmap.md` D13), Super Sonic faces (none in the game), Tikal (cannot tick), board rows while boarding (0.9.5).

### 4.9 Agent count and the fold rule

Nine agents is the round's cap. If the orchestrator needs a slot (e.g. an `objects2_plan.md` lane): fold `094a-gsa1-wheel` into
`094a-gsa1-faces` (one agent with both texts of 6.4 / 6.5 and the union of their files; the Lua contract 5.5 becomes internal);
second choice, fold `094a-lib-emote` into `094a-lib-faces`. Never fold a lane into a Super agent.

---------------------------------------------------------------------------------------------------------------------

## 5. Ownership, order, contracts

### 5.1 Order (extends `super2_plan.md` 5.1)

```
0.9.4 (or 0.9.4.1) released  +  base094 copy (orchestrator, super2_plan.md 3.3)
   |
   |-- 094a-lib-anim   step 1 = super2_plan.md 5.3 / 5.4 AND this plan's 5.3 / 5.4 (one build; time reported), then its own tasks
   |-- 094a-lib-gold
   |-- 094a-lib-audio
   |-- 094a-lib-faces  (new files only; into src/ once the contract has landed)
   '-- 094a-lib-emote  (new files only; into src/ once the contract has landed)
              |  (all five reported)
   |-- 094a-gsa1        (Super Sonic, super2_plan.md 6.4; owner of tests/lua_mock.lua)
   |-- 094a-gsa1-faces  (feature "faces": module, microphone, Faces page, face animation maker, lip sync, the census face segment)
   '-- 094a-gsa1-wheel  (feature "emote": module, the wheel, the Emote wheel page, the face trigger, the cl_character.lua seams)
              |  (all three reported)
   094a-verify (super2_plan.md 6.5 + this plan's 6.7)  -->  release 0.9.4a (super2_plan.md 6.6 + this plan's 6.8)
```

All G-SA1 agents start after all LIB agents (the g-sa1 build snapshots LIB's build: a LIB tree another agent still edits is no
base). The three G-SA1 agents build g-sa1 at the same time (the 0.9.2 / 0.9.3 "wave G1" precedent): no shared file; a build that
fails in another agent's file: wait and retry.

### 5.2 File ownership (adds to `super2_plan.md` 5.2; p = a named small patch, re-read right before editing)

| Agent | Owns | Window |
|---|---|---|
| 094a-lib-anim (added to its list) | LIB `src/world/sa1_world_tick.c` (p: `sa1g_step_game`'s four lane calls only); in files it already owns: the 5.3 header text, `sa1g_lane` and the two members in `sa1_world.h`, the `sa1w_pstate` tail and three declarations in `src/sa1_api_world.h`, the fill calls in `sa1w_world_pstate` (`sa1_api_world.c`), `state_of` and `sa1w_character_world_ensure` (`sa1_character.c`), `sa1w_world_super_lifted` (`sa1_world_super.c`), super_test case (19) | round start (step 1) |
| 094a-lib-faces | LIB `src/sa1_face.c`, `src/sa1_face.h`, `src/world/sa1_world_face.c` (all new), `tests/face_test.c`, `tests/build_face.py` (new) | round start (scratch), the tree after the contract |
| 094a-lib-emote | LIB `src/sa1_emote.c`, `src/sa1_emote.h`, `src/sa1_emote_names.c` (generated), `src/world/sa1_world_emote.c` (all new), `tools/gen_emote_names.py` (new folder `tools/`), `tests/emote_test.c`, `tests/build_emote.py` (new) | as 094a-lib-faces |
| 094a-gsa1-faces | g-sa1 `src/module/bind_faces.cpp`, `src/core/mic.hpp`, `src/core/mic.cpp`, `addon/lua/gsa1/feat/sh_faces.lua`, `addon/lua/gsa1/feat/cl_faces.lua`, `tests/feat_faces_test.lua`, `tests/feat_faces_binding_test.cpp`, `tests/feat_faces_test.cpp` (all new); (p) `tests/core_test.cpp` (the census script's face segment only), `addon/lua/gsa1/cl_warm_census.lua` (regenerated by `tools/gen_warm_census.py`, never by hand) | after the five LIB agents |
| 094a-gsa1-wheel | g-sa1 `src/module/bind_emote.cpp`, `addon/lua/gsa1/feat/sh_wheel.lua`, `addon/lua/gsa1/feat/cl_wheel.lua`, `tests/feat_wheel_test.lua`, `tests/feat_emote_binding_test.cpp`, `tests/feat_emote_test.cpp` (all new); (p) `addon/lua/gsa1/cl_character.lua` (the two seams of 6.5 task 4 only) | after the five LIB agents |
| 094a-gsa1 (unchanged list) | as `super2_plan.md` 5.2; `tests/lua_mock.lua` (p) stays its own: the faces / wheel agents put their stubs into their own test files | after the five LIB agents |
| 094a-verify | nothing (scratch copies only) | after all eight |
| release step | versions, README, BUILD_INFO, the install, status page, HANDOFF, rounds plan 14 (6.8) | after the verifier holds |

Conflicts checked: the two new LIB lanes write only new files and share none (`sa1_face.h` and `sa1_emote.h` are each lane's own
internal header; `sa1_api_world.h` is read, never edited, by them). Every shared LIB file of the lanes' hooks (`libsa1.h`,
`sa1_world.h`, `sa1_world_tick.c`, `sa1_api_world.h`, `sa1_api_world.c`, `sa1_character.c`, `sa1_world_super.c`) has one editor
this round, `094a-lib-anim`. `094a-lib-gold` and `094a-lib-audio` touch none of the new files. In g-sa1 the three agents share
nothing: `session.*`, `gsa1_module.cpp`, `cl_textures.lua`, `lua_mock.lua`, `lua_check.py` are `094a-gsa1`'s patches; `core_test.cpp`
and `cl_warm_census.lua` the faces agent's; `cl_character.lua` the wheel agent's; the feature files are new. Neither new G-SA1
agent edits `cl_menu.lua`, `sh_config.lua`, `autorun`, `build.py` or `README.md` (features register themselves; tests are
globbed; the README is the release step's). No agent edits `cl_super.lua` for the census: its `S.coreSonic` copies the regenerated
core census at load (`feat/cl_super.lua:296-312`). SADC: nothing.

### 5.3 The header contract, extended (`LIB/include/libsa1.h`; written by `094a-lib-anim` in its first step)

Comments may be tightened, never their meaning changed. The names are new (no number of `super2_plan.md` 5.3 is shared).

```c
/* the history comment's round 0.9.4a line (super2_plan.md 5.3) gains, after "libsa1_audio.h version 5: stage rules;": */
 *                    the game's event faces (SA1_API_FACE: sa1_character_face_mode / _face / _face_sequence / _face_level,
 *                    sa1_face_info_get) and emotes through the cutscenes' event action queue (SA1_API_EMOTE:
 *                    sa1_character_emote / _emote_stop, sa1_emote_count / sa1_emote_get); sa1_state_ex's face and emote
 *                    fields (sadc docs/research/faces_wheel_plan.md).

/* sa1_state_ex, at its end (after every field the released 0.9.4 header has): */
    /* round 0.9.4a (SA1_API_FACE), written when struct_size covers them. Without a world, or in a world that never had a
       face call: face_mode 0, face_shown 0, face_from / face_to / face_letter -1, face_rate 0, face_activity 0 */
    uint32_t face_mode;         /* SA1_FACE_MODE_* | SA1_FACE_PACE_CUTSCENE as applied */
    uint32_t face_shown;        /* 1: after the pair the EV head is linked into the tree the display draws (it is drawn from the
                                   next pair); 0 while the General look is on or an emote draws another object (review R.5) */
    int32_t  face_from, face_to;/* the keys being blended: the event face block's while it runs, else the gameplay face work's
                                   while a gameplay face blends; -1 none */
    float    face_rate;         /* 0..1 between them */
    int32_t  face_letter;       /* the face animation being played 0..30 ('A'..'e'); -2 a host step (a hold, lip sync); -1 none */
    uint32_t face_activity;     /* SA1_FACE_ACT_* */
    /* round 0.9.4a (SA1_API_EMOTE), written when struct_size covers them (no world / no emote: 0 and -1) */
    uint32_t emote_state;       /* SA1_EMOTE_STATE_* */
    int32_t  emote_item;        /* the item playing (0..count-1; the trailing stand entry: count), -1 none */
    int32_t  emote_row;         /* its action row, -1 for an extra or none */
    int32_t  emote_extra;       /* its extra index (sa1_emote_info.index of an EXTRA entry), -1 for a row or none */
    float    emote_frame;       /* the queue head's frame (eventwk f3C) */

/* after the last section of the released header (the SET section), two new sections: */

/* ---- faces (SA1_API_FACE, round 0.9.4a; sadc docs/research/faces_events.md, faces_wheel_plan.md 4.2-4.4) -----------------
 * The game's event faces: the EV head (a second head whose vertices the game blends between shape keys) driven by the game's
 * own face block (eventwk 0x5C-0x6B: EV_SetFace's body 8C04BBCE, the reader 8C04BC20 / 8C04BC70 / 8C04BCC8, EV_ClrFace's body
 * 8C04BD60), drawn by the face task 8C07B106 with the blend 8C07B080 / 8C055EFE. libsa1 takes the place of a cutscene's face
 * script: every call is latched and applied at the start of the character's next pair (before the executor); the last call
 * of a kind before a pair wins. A world that never gets a face call runs exactly as before (the reference worlds never do).
 * Characters: Sonic (21 keys), Tails, Knuckles, Amy (20): all 31 face animations; Big (8 keys, 6 face shapes): the 14 of
 * sa1_face_info.letters; E-102, Eggman, Tikal and Super Sonic (kind 8): SA1_ERR_UNSUPPORTED; Sonic in the General form:
 * accepted, shown only while he is not Super (the game has no Super Sonic event head). A puppet: SA1_ERR_UNSUPPORTED.
 * Which face shows: a running face animation or NOW script > lip sync > a held face > the base (EVENT: the game's idle;
 * GAME: the regular face). While any of these runs a gameplay face (a hit's 'I') waits, as in the game's events, and is
 * dropped (PFaceCancel 8C07B2DC) when it ends. Host-only writes (only in worlds that get these calls): the NOW interrupt
 * (face_next / face_count / face_script), face_seq pointing at a step buffer of the host (face animations, holds, lip sync),
 * the lip-sync writes, the face table's pointers while PACE_CUTSCENE, and PFaceCancel when a host face STARTS over a running
 * gameplay face (the game's face task would show the gameplay face to its end and run the host face unseen; review R.4).
 * Host face calls without activity (GAME mode, lip sync off) leave the game's faces exactly as without a call (review R.6). */
#define SA1_API_FACE 1

#define SA1_FACE_MODE_GAME       0u     /* the default: the regular face, with the game's gameplay faces (hurt 'I', frozen 'e') */
#define SA1_FACE_MODE_EVENT      1u     /* the EV head always, with the game's idle (8C04BC70: neutral 'A' 75 %, blink 'B'
                                           25 %, 8 steps each); leaving it: 8C04BD60 and PFaceCancel */
#define SA1_FACE_PACE_CUTSCENE 0x100u   /* mode flag (EVENT only): the game's face animations at cutscene speed - the world's
                                           face table points at copies with every step time doubled (events run one logic
                                           step per frame); the host's enter / hold frames are not scaled */
SA1_API sa1_status sa1_character_face_mode(sa1_context *ctx, sa1_character_id id, uint32_t mode);

/* The game's EV_SetFace: script = 1..64 of 'A'-'Z', 'a'-'e' (face animations 0-30, faceTbl D_8C19C32C) and '0' (back to
 * the regular face); NULL / "" = stop (EVENT: the idle; GAME: the regular face). GAME mode: a '0' is appended when missing,
 * except with SA1_FACE_LOOP (a looping script ends with the next face call and may not contain '0': SA1_ERR_INVALID_ARGUMENT;
 * review R.3). 'A' in a script is the game's idle choice (8C04BC70 takes n == 0 as the idle: 'A' 75 % / 'B' 25 %, rand()),
 * as in the game's events; a face animation (sa1_character_face_sequence) plays 'A' itself (review R.11).
 * Without SA1_FACE_NOW the game's queue: the script starts when the running animation ends. Errors (nothing applied): a
 * script byte outside 'A'-'Z', 'a'-'e', '0' or more than 64 of them SA1_ERR_INVALID_ARGUMENT; a letter the character cannot
 * show SA1_ERR_UNSUPPORTED. */
#define SA1_FACE_NOW   0x1u    /* start at the next step: the running step ends on the nearer of its two keys */
#define SA1_FACE_LOOP  0x2u    /* repeat the script until the next face call (a one-letter script holds that face) */
SA1_API sa1_status sa1_character_face(sa1_context *ctx, sa1_character_id id, const char *script, uint32_t flags);

/* A face animation made by the host: entries played in order (always NOW), each one of the game's face animations with the
 * host's timing written as the game's own step data: enter = the time of the animation's first step (the blend from the face
 * shown before into its first key; 0 = the game's own, sa1_face_info.first), the animation's other steps with the game's
 * times, then hold more steps on its last key (steps {<= 127, key}). At the end: EVENT the idle, GAME the regular face; with
 * SA1_FACE_HOLD_LAST the last key stays until the next face call. count 0 = stop. Refused as a whole (nothing applied):
 * count > SA1_FACE_SEQ_MAX, anim > 30, enter > 127, hold > SA1_FACE_HOLD_MAX, reserved fields != 0: SA1_ERR_INVALID_ARGUMENT;
 * an animation the character cannot show: SA1_ERR_UNSUPPORTED. Frames are 60 Hz logic steps. */
typedef struct sa1_face_entry {
    uint8_t  anim;              /* 0..30 = 'A'..'Z', 'a'..'e' */
    uint8_t  reserved;          /* 0 */
    uint16_t enter;             /* 0..127 steps; 0 = the game's first step time */
    uint16_t hold;              /* 0..SA1_FACE_HOLD_MAX steps on the last key */
    uint16_t reserved2;         /* 0 */
} sa1_face_entry;
#define SA1_FACE_SEQ_MAX     32u
#define SA1_FACE_HOLD_MAX  3600u
#define SA1_FACE_HOLD_LAST   0x4u
SA1_API sa1_status sa1_character_face_sequence(sa1_context *ctx, sa1_character_id id, const sa1_face_entry *entries,
                                               uint32_t count, uint32_t flags);

/* Lip sync: level 0..1 from the host (its loudness), < 0 = off; the last value before a pair counts; cheap to call every
 * frame. While on (and no face animation runs) the EV head shows, also in GAME mode; off: the mouth closes over 6 steps, then
 * a held face comes back (blended over 8 steps) or the base. */
#define SA1_FACE_LEVEL_LADDER 0u   /* the mouth on the character's ladder (sa1_face_info.ladder) by the level: the face block's
                                      keys, face_time 64 and face_count written each pair, the reader parked on a held step;
                                      drawn by the game's blend one pair later */
#define SA1_FACE_LEVEL_TALK   1u   /* the game's talk animations C, D, E looped (NOW | LOOP) while the level is >= 0 */
SA1_API sa1_status sa1_character_face_level(sa1_context *ctx, sa1_character_id id, float level, uint32_t style);

#define SA1_FACE_ACT_NONE      0u  /* the base */
#define SA1_FACE_ACT_SEQUENCE  1u  /* a face animation of the host (sa1_character_face_sequence) */
#define SA1_FACE_ACT_SCRIPT    2u  /* a script (sa1_character_face) */
#define SA1_FACE_ACT_HELD      3u  /* HOLD_LAST's last key, or a LOOP script */
#define SA1_FACE_ACT_LIPSYNC   4u
#define SA1_FACE_ACT_GAMEPLAY  5u  /* the game's own gameplay face (PFaceChange: hurt, frozen) */

typedef struct sa1_face_info {
    uint32_t struct_size;
    uint32_t keys;              /* vertex keys of the EV head's shape motion: 21 Sonic, 20 Tails / Knuckles / Amy / Tikal,
                                   8 Big; 0 none (E-102, Eggman, Super Sonic) */
    uint32_t letters;           /* bit n: face animation n uses only keys the character has (0x7FFFFFFF; Big 0x01FC0C1F) */
    uint32_t ladder_len;        /* 5; Big 3; 0 none */
    uint8_t  ladder[8];         /* the mouth keys from closed to open: 0 5 4 6 7; Big 0 4 5 (order UNVERIFIED by eye) */
    uint16_t steps[31];         /* each face animation's length in 60 Hz steps at gameplay pace */
    uint8_t  first[31];         /* its first step's time (sa1_face_entry.enter's default) */
    uint8_t  last_key[31];      /* the key it ends on (what sa1_face_entry.hold holds) */
} sa1_face_info;
/* No world needed: each call reads 1ST_READ.BIN (Tikal: TIKAL_PROG.PRS) from the disc of the last sa1_load_disc and computes
 * the numbers (tens of ms; nothing is kept - sa1_load_disc frees the images it read: call it once per kind and keep the
 * result; review R.2). kind 8: keys 0; SA1_ERR_NOT_LOADED without a disc. */
SA1_API sa1_status sa1_face_info_get(sa1_context *ctx, int32_t kind, sa1_face_info *out);

/* ---- emotes (SA1_API_EMOTE, round 0.9.4a; sadc docs/research/emote_wheel_lib.md, faces_wheel_plan.md 4.5) ----------------
 * Any of a character's animations, played the way the game's cutscenes animate a live player: the event action queue of the
 * player's event work (EV_SetAction's body 8C04B4D2, EV_SetMotion's 8C04B528, EV_ClrAction's 8C04B48C), drawn by the
 * character's own display through 8C04B57C (welds 8C04BF00) instead of the gameplay action while the queue is not empty. The
 * gameplay keeps running underneath (the character stands: gravity, ground, water, damage and the idle timer are the game's);
 * the host writes nothing into the game state. Started at the character's next pair; cancelled before a pair whose input would
 * move him (GetAnalog 8C06CD22, the jump / action presses) and after a pair that left the standing state, hurt him, changed
 * the held object or the form. Only the entries of sa1_emote_get are accepted. A world without an emote call runs exactly as
 * before (the reference worlds never call one). The queue's own code leaks a finished entry when the next one has no blend
 * (8C04B57C), so libsa1 never queues an unblended entry behind another: such items start after a clear (review R.1). The queue
 * moves only on pairs the character is drawn: outside the host camera's view an emote pauses (review R.17). */
#define SA1_API_EMOTE 1

typedef struct sa1_emote_item {
    uint32_t source;            /* SA1_EMOTE_ROW or SA1_EMOTE_EXTRA */
    uint32_t index;             /* the action row, or the extra index (sa1_emote_info.index of an EXTRA entry) */
    int32_t  play;              /* SA1_EMOTE_PLAY_* */
    float    speed;             /* multiplier of the game's rate; 0 = 1 */
} sa1_emote_item;
#define SA1_EMOTE_ROW          0u
#define SA1_EMOTE_EXTRA        1u
#define SA1_EMOTE_PLAY_DEFAULT 0   /* the row's own: loop rows loop, once and hold rows once */
#define SA1_EMOTE_PLAY_ONCE    1
#define SA1_EMOTE_PLAY_LOOP    2
#define SA1_EMOTE_MAX_ITEMS    8u
typedef struct sa1_emote {
    uint32_t       struct_size;
    uint32_t       count;       /* 1..SA1_EMOTE_MAX_ITEMS, played in order; only the last may loop */
    sa1_emote_item items[SA1_EMOTE_MAX_ITEMS];
    int32_t        link;        /* blend frames between items of one object and into the stand; -1 = 8, 0 = none */
    uint32_t       flags;       /* SA1_EMOTE_* */
} sa1_emote;
#define SA1_EMOTE_CHAIN      0x1u  /* a once item: also the game's PL_ACTION.next chain up to its loop (75 > 76 > stand) */
#define SA1_EMOTE_NO_TRAIL   0x2u  /* no trailing stand entry after a once emote (the gameplay stand pops back) */
#define SA1_EMOTE_IN_AIR     0x4u  /* may start when not standing; then cancelled only by input, hurt and the object changes
                                       (the form, the board, the held object; review R.15) */
#define SA1_EMOTE_KEEP_INPUT 0x8u  /* no input cancel (the host cancels) */
#define SA1_EMOTE_NO_PROPS   0x10u /* Amy: do not show the hammer (8C0DE46C) for her attack rows */
/* Errors: SA1_ERR_NOT_FOUND (no character), SA1_ERR_UNSUPPORTED (a puppet, Eggman, Tikal; or not standing without
 * SA1_EMOTE_IN_AIR: nothing queued; sa1_last_error says why in plain words: "in the air", "hurt", "carrying something",
 * "on the board", "moving", "busy"; review R.16), SA1_ERR_INVALID_ARGUMENT (an entry not offered for this kind and form),
 * SA1_ERR_LIMIT ("the game's memory is full": the game heap cannot take the queue entries; review R.7) */
SA1_API sa1_status sa1_character_emote(sa1_context *ctx, sa1_character_id id, const sa1_emote *emote);
#define SA1_EMOTE_STOP_NOW   0u    /* EV_ClrAction at the next pair start: the gameplay pose */
#define SA1_EMOTE_STOP_BLEND 1u    /* the stand blended in after the current item (a loop finishes its cycle) */
SA1_API sa1_status sa1_character_emote_stop(sa1_context *ctx, sa1_character_id id, uint32_t how);
#define SA1_EMOTE_STATE_NONE    0u
#define SA1_EMOTE_STATE_PLAYING 1u
#define SA1_EMOTE_STATE_ENDING  2u /* blending into the stand */

/* The catalogue of one character entry: every offered table row (in G-SA1's worlds, not empty, not a stage or MINICART row;
 * kind 8: Sonic's rows on the Super body except the curled-body rows; rows 130-141 only for kind 8 and SA1_EMOTE_FORM_SUPER)
 * and every validated extra motion. No world needed: names, labels, classes, the extras' addresses and frame counts are a
 * generated table of names and numbers; rows' frames and play modes come from the loaded character set (sa1_action_info_get;
 * review R.2). Internal names are not unique (two motions may share one): dup_of compares the drawn object and the motion. */
#define SA1_EMOTE_FORM_NORMAL 0u
#define SA1_EMOTE_FORM_SUPER  1u   /* kind 0: the General form while Super (GT / GA rows and 130-141); kind 8 is always Super */
#define SA1_EMOTE_CLASS_STAND    0u
#define SA1_EMOTE_CLASS_IDLE     1u
#define SA1_EMOTE_CLASS_POSE     2u
#define SA1_EMOTE_CLASS_MOVE     3u
#define SA1_EMOTE_CLASS_AIR      4u
#define SA1_EMOTE_CLASS_BRAKE    5u
#define SA1_EMOTE_CLASS_BALL     6u
#define SA1_EMOTE_CLASS_ATTACK   7u
#define SA1_EMOTE_CLASS_CARRY    8u
#define SA1_EMOTE_CLASS_SETPIECE 9u
#define SA1_EMOTE_CLASS_HURT    10u
#define SA1_EMOTE_CLASS_FISH    11u
#define SA1_EMOTE_CLASS_SWIM    12u
#define SA1_EMOTE_CLASS_BOARD   13u
#define SA1_EMOTE_CLASS_SUPER   14u
#define SA1_EMOTE_F_NEEDS_PROP  0x1u  /* drawn without its object (held object, stage piece, fishing tasks, water) */
#define SA1_EMOTE_F_HOLD        0x2u  /* the game holds its last frame (mtnmode 5 / 7); played once */
#define SA1_EMOTE_F_SUPER_ONLY  0x4u  /* 130-141 */
#define SA1_EMOTE_F_BOARD       0x8u  /* a board motion (the board is not drawn before 0.9.5) */
#define SA1_EMOTE_F_OTHER_OBJECT 0x10u /* not the main body (ball, curled body, Tails' / Knuckles' flight model): never blended
                                           from an entry of another object */
#define SA1_EMOTE_F_EXTRA       0x20u /* not in the action table (EV_SetMotion on the main body) */
typedef struct sa1_emote_info {
    uint32_t    struct_size;
    uint32_t    source;          /* SA1_EMOTE_ROW / SA1_EMOTE_EXTRA */
    uint32_t    index;           /* the row, or the extra index */
    const char *internal_name;   /* "s_kimepose0": the developers' motion file name (SA Tools labels' file names) */
    const char *label;           /* "S_MOT_Sonic_Level_Clear_Pose" (SA Tools; a seed for custom names later) */
    uint32_t    emote_class;     /* SA1_EMOTE_CLASS_* */
    uint32_t    flags;           /* SA1_EMOTE_F_* */
    uint32_t    frames;          /* NJS_MOTION.nbFrame (from the disc) */
    int32_t     play;            /* SA1_EMOTE_PLAY_LOOP or SA1_EMOTE_PLAY_ONCE: how it plays by default */
    int32_t     dup_of;          /* the list position of the first entry with the same object and motion, -1 none */
} sa1_emote_info;
SA1_API uint32_t   sa1_emote_count(const sa1_context *ctx, int32_t kind, uint32_t form);   /* 0 without a disc */
SA1_API sa1_status sa1_emote_get(const sa1_context *ctx, int32_t kind, uint32_t form, uint32_t i, sa1_emote_info *out);
```

### 5.4 The internal contract, extended (written by `094a-lib-anim` in its first step)

```c
/* src/world/sa1_world.h - before sa1g_world: */
/* round 0.9.4a (sadc docs/research/faces_wheel_plan.md 5.4): the hooks of a host feature of a character world, installed by
   the feature's own code at its first call for the world (faces: src/world/sa1_world_face.c, emotes: src/world/sa1_world_emote.c);
   all NULL / 0 until then, so a world that never gets such a call runs exactly as before. Kept across image rebuilds (zeroed
   with the world); `state` points into segment 0x18, which every rebuild resets: the feature validates its block by magic and
   self (as sa1_world_set.c's st_state) and makes it again; persist[] holds the feature's values that survive a rebuild. */
#define SA1W_HAS_LANE_HOOKS 1
struct sa1w_pstate;
typedef struct sa1g_lane {
    sa1g_step_fn pre;           /* sa1g_step_game, every step: right after set_pre (before the executor; sub 0 = pair start) */
    sa1g_step_fn post;          /* sa1g_step_game, every step: right after set_post */
    void (*fill)(const struct sa1g_world *w, struct sa1w_pstate *o);   /* sa1w_world_pstate, last */
    sa1_context *ctx;           /* the context whose calls the world runs */
    uint32_t state;             /* guest address of the feature's block in segment 0x18 (0: none) */
    uint32_t persist[8];
} sa1g_lane;
/* sa1g_world, at its end (after set_test_pre): */
    sa1g_lane face;             /* round 0.9.4a: faces (094a-lib-faces) */
    sa1g_lane emote;            /* round 0.9.4a: emotes (094a-lib-emote) */

/* src/world/sa1_world_tick.c, sa1g_step_game - after the set_pre call: */
    if (w->face.pre != NULL) {
        w->face.pre(w, sub, w->user);       /* round 0.9.4a: latched face calls (sa1_world_face.c) */
    }
    if (w->emote.pre != NULL) {
        w->emote.pre(w, sub, w->user);      /* round 0.9.4a: emote start / input cancel (sa1_world_emote.c) */
    }
/*   - after the set_post call: */
    if (w->emote.post != NULL) {
        w->emote.post(w, sub, w->user);     /* round 0.9.4a: the state cancel, the end (sa1_world_emote.c) */
    }
    if (w->face.post != NULL) {
        w->face.post(w, sub, w->user);      /* round 0.9.4a (unused today; kept symmetric) */
    }

/* src/sa1_api_world.h - sa1w_pstate, at its end: */
    /* round 0.9.4a (faces_wheel_plan.md 5.4): sa1w_world_pstate sets the "none" values (face_from / face_to / face_letter,
       emote_item / emote_row / emote_extra -1, the rest 0), then calls w->face.fill and w->emote.fill */
    uint32_t face_mode, face_shown, face_activity;
    int32_t  face_from, face_to, face_letter;
    float    face_rate;
    uint32_t emote_state;
    int32_t  emote_item, emote_row, emote_extra;
    float    emote_frame;
/* src/sa1_api_world.h - declarations (094a-lib-anim defines both): */
#define SA1W_HAS_SUPER_LIFTED 1
/* the guest address of GA's NJS_ACTION {the Super body, the lifted copy} for the game's Super actions 130-134 (the actptr of
   GA rows 0 / 9 / 10 / 11 / 12; 133 and 134 share one copy); 0 for any other action, a world that is not General, or before
   GA is built (src/world/sa1_world_super.c) */
uint32_t sa1w_world_super_lifted(const struct sa1g_world *w, uint32_t action);
#define SA1W_HAS_WORLD_ENSURE 1
/* the character's world, built if needed (sa1_character.c's ensure_world); SA1_ERR_NOT_FOUND no character,
   SA1_ERR_UNSUPPORTED a puppet (no world), SA1_ERR_NOT_IMPLEMENTED with SA1_NO_WORLD; *out NULL on any error; SA1_PARTIAL
   (built without land) is no error: *out set (review R.12) */
sa1_status sa1w_character_world_ensure(sa1_context *ctx, sa1_character_id id, struct sa1g_world **out);
```

`src/world/sa1_api_world.c` `sa1w_world_pstate`: the "none" values, then `if (w->face.fill) w->face.fill(w, o); if
(w->emote.fill) w->emote.fill(w, o);` as its last statements (also when the player is missing: the fills handle that).
(review R.12) The function returns early today (`sa1_api_world.c:678` no arena, `:685` no player): the "none" values go
right after its `memset`, before the first return, and the no-player return is restructured so the fills run there too
(no arena: the "none" values only).
`src/sa1_character.c` `state_of`: copies the twelve fields (review R.12: seven face, five emote) into `sa1_state_ex` (the
no-world branch writes the "none" values).
The lanes include `sa1_api_world.h` and `world/sa1_world.h` read-only; each installs its hooks itself (`w->face.pre = ...;
w->face.fill = ...; w->face.ctx = ctx;`) at its first call for a world, after `sa1w_character_world_ensure`.

### 5.5 The Lua contract (`GSA1.Faces`, made by `094a-gsa1-faces`, used by `094a-gsa1-wheel`)

```lua
-- feat/sh_faces.lua / feat/cl_faces.lua (094a-gsa1-faces); cl_faces.lua loads before cl_wheel.lua (alphabetical)
GSA1.Faces = F
F.Library()                 -- array (in the page's order) of { name, entries = { {letter, enter, hold}, ... }, back, hold_last }
F.Find(name)                -- that face animation or nil
F.HasFaces(entry_name)      -- false for gamma, supersonic, supersonic_general (and when the module has no feature "faces")
F.CanShow(name, entry_name) -- true | false, why ("Big can't show F, Z")
F.Play(C, name, why)        -- plays it on the character table C now (gsa1.FaceSequence); true | nil, reason; why = "wheel",
                            -- "hotkey", "preview" (log lines)
F.Stop(C)
-- hook.Run("GSA1FaceLibraryChanged", change)  change = { renamed = { [old] = new }, deleted = { [name] = true } } after every
-- save, rename or delete; the wheel updates its slots' face names (renamed) or clears them (deleted) and redraws its combos
```

`094a-gsa1-wheel` uses only these names, guarded (`if GSA1.Faces and GSA1.Faces.Play then`), and stubs them in its tests.

### 5.6 The module contract (features `faces` 1 and `emote` 1; every function `true | nil, message, code` unless noted)

| Lua | libsa1 behind it | Notes |
|---|---|---|
| `gsa1.FaceInfo(kind)` -> `{has_face, keys, letters, ladder = "0,5,4,6,7", steps = {[0..30]}, first = {...}, last = {...}}` | `sa1_face_info_get` | no world; nil, msg without the disc; (review R.2) reads the disc each call: once per kind (F.Info caches) |
| `gsa1.FaceMode(id, event, pace)` | `sa1_character_face_mode` | booleans |
| `gsa1.FaceSequence(id, spec, flags)` | `sa1_character_face_sequence` | spec `"b:10:90,A:8:0"` (letter:enter:hold); flags `FACE.HOLD_LAST`; parsed in C, refused with the reason |
| `gsa1.FaceScript(id, letters, flags)` | `sa1_character_face` | the game's EV_SetFace path (tests, console `gsa1_face_script`, 2.0) |
| `gsa1.FaceStop(id)` | `sa1_character_face_sequence(.., 0, ..)` | |
| `gsa1.FaceLevel(id, level, style)` | `sa1_character_face_level` | per frame: numbers only, no table, returns nothing |
| `gsa1.FaceState(id)` -> `mode, shown, from, to, rate, letter, activity` | `sa1_state_ex` (the session's last state) | multiple returns, no table |
| `gsa1.MicDevices()` -> `{ {index, name, default, enabled}, ... }`; `gsa1.MicOpen(device)`; `gsa1.MicListen(on)`; `gsa1.MicClose()`; `gsa1.MicLevel()` -> `level, peak, state` | GMod's `bass.dll` recording API | `src/core/mic.*`; state `off` / `paused` / `on` / `failed: <why>` |
| `gsa1.FACE` | constants | MODE_GAME, MODE_EVENT, PACE_CUTSCENE, NOW, LOOP, HOLD_LAST, LEVEL_LADDER, LEVEL_TALK, ACT_*, SEQ_MAX, HOLD_MAX |
| `gsa1.EmoteList(kind, form)` -> `{n, [i] = {key = "a75" / "x3", source, index, name, label, class, frames, play = "loop" / "once" / "hold", flags, dup}}` | `sa1_emote_count / _get` | no world; form 0 / 1 |
| `gsa1.Emote(id, key, flags)` -> `true` or `nil, reason, code` | `sa1_character_emote` (one item, CHAIN, link -1) | flags `EMOTE.LOOP`, `EMOTE.IN_AIR`; reason in plain words |
| `gsa1.EmoteStop(id, how)` | `sa1_character_emote_stop` | how `EMOTE.STOP_NOW` / `STOP_BLEND` |
| `gsa1.EmoteState(id)` -> `state, key, item, frame` | `sa1_state_ex` | multiple returns |
| `gsa1.EMOTE` | constants | LOOP, IN_AIR, STOP_NOW, STOP_BLEND, STATE_*, CLASS_*, F_*, FORM_* |

### 5.7 Hand-overs

* `094a-lib-anim` -> all: the time its step 1 built, the real names (expected exactly 5.3 / 5.4), test (19)'s result.
* `094a-lib-faces` -> `094a-gsa1-faces`: the API, `sa1_face_info` per kind (keys, letters, ladders, steps / first / last_key of
  the 31), the measured timelines for its core test (F3, F7, F15), the refusals' texts, the README lines.
* `094a-lib-emote` -> `094a-gsa1-wheel`: the API, the list sizes per (kind, form), the refusal texts, the pacing per kind, the
  README lines.
* `094a-gsa1-faces` <-> `094a-gsa1-wheel`: only 5.5 (they run in parallel).

---------------------------------------------------------------------------------------------------------------------

## 6. Agent texts

### 6.1 `094a-lib-anim`: addendum (append to `super2_plan.md` 6.1's text; the same agent)

```
ADDENDUM (faces_wheel_plan.md 5.3 / 5.4): two new LIB lanes, 094a-lib-faces and 094a-lib-emote, run in parallel with you and write
only new files; every shared LIB file they need is yours this round. Read faces_wheel_plan.md 0, 2.3, 4.2 (only to know what the
hooks serve), 5.2-5.4.
1e THE LANES' CONTRACT, in the SAME first step as your task 1 (one build; report the time it built - both lanes wait for it):
   (a) include/libsa1.h: faces_wheel_plan.md 5.3 verbatim (the history text, the sa1_state_ex tail after every field the released
       0.9.4 header has, the "faces" and "emotes" sections after the last section) - comments may be tightened, meanings never;
   (b) src/world/sa1_world.h: SA1W_HAS_LANE_HOOKS, struct sa1w_pstate's forward declaration, sa1g_lane, and the members face and
       emote at the end of sa1g_world (5.4);
   (c) src/world/sa1_world_tick.c (p, sa1g_step_game only): the four calls of 5.4 (face.pre and emote.pre right after the set_pre
       call; emote.post and face.post right after the set_post call); nothing else of the file;
   (d) src/sa1_api_world.h: the sa1w_pstate tail, SA1W_HAS_SUPER_LIFTED with sa1w_world_super_lifted, SA1W_HAS_WORLD_ENSURE with
       sa1w_character_world_ensure (5.4);
   (e) src/world/sa1_api_world.c, sa1w_world_pstate: the "none" values (face_from / face_to / face_letter / emote_item / emote_row /
       emote_extra -1, the rest 0), then the two fill calls as its last statements; (review R.12) the "none" values right after
       its memset, before its first early return (:678), and the fills on the no-player path (:685) too;
   (f) src/sa1_character.c: state_of copies the twelve fields (review R.12; the no-world branch the "none" values);
       sa1w_character_world_ensure = ensure_world plus SA1_ERR_UNSUPPORTED for a puppet (SA1_CREATE_PUPPET) and
       SA1_ERR_NOT_IMPLEMENTED with SA1_NO_WORLD; SA1_PARTIAL (no land) is success with *out set;
   (g) src/world/sa1_world_super.c: sa1w_world_super_lifted (GA's actptr of rows 0 / 9 / 10 / 11 / 12 for 130 / 131 / 132 / 133 /
       134 once GA exists; 0 otherwise) - it may return 0 until your task 2 lands GA; it must be right at your final gate.
   Then "py -3.12 build.py" (the strict lists must still pass: nothing calls the hooks yet). The lanes compile their new files only
   after this build.
Files added to your list: src/world/sa1_world_tick.c (p: those four calls only).
Test added - super_test case (19) lane_contract (fails before: the names do not exist): a General world and a plain Sonic world that
got no face / emote call have face.pre / emote.pre / both fills NULL and replay as before; sa1w_world_super_lifted in the General
world after its first pair returns non-zero addresses for 130-134 whose NJS_ACTION objects are the Super body and whose motions'
node-0 position keys are the originals + L (the same copies as test (1)), 133 and 134 equal, 0 for 129 / 135 / kind 8 / plain
Sonic; sa1_state_ex of a fresh character and of a ticking one without face / emote calls: face_from / face_to / face_letter /
emote_item / emote_row / emote_extra -1, the other new fields 0; an sa1_state_ex with the 0.9.4 struct_size is still filled up
to that size only.
Hand in api_notes: the step-1 build time, the real names (expected exactly 5.3 / 5.4), case (19)'s result.
```

### 6.2 `094a-lib-faces`

```
YOU ARE AGENT "094a-lib-faces" (round 0.9.4a addendum, LIB; in parallel with 094a-lib-anim, 094a-lib-gold, 094a-lib-audio and
094a-lib-emote). You write ONLY new files; you do NOT edit include/libsa1.h, src/world/sa1_world.h, src/world/sa1_world_tick.c,
src/sa1_api_world.h, src/world/sa1_api_world.c or src/sa1_character.c - 094a-lib-anim writes your names and hooks there in its
first step (faces_wheel_plan.md 5.3 / 5.4): develop in your scratch folder and put your files into the tree only after that step has
built (check SA1W_HAS_LANE_HOOKS in src/world/sa1_world.h and SA1_API_FACE in include/libsa1.h; until then also check your sources
against a scratch copy of the tree with 5.3 / 5.4 applied by you). Research: faces_events.md (all: 2-8), super2_plan.md 10.2,
faces_wheel_plan.md 4.2-4.4 (the mechanism you implement, exactly) and 5.3 (your API's semantics). Probe to port:
SADC/build/research/faces094a/res_faces/probe_faces.c (how it reaches player 0's taskwk, the face block, the face work, the head
linked under the face parent, and calls 8C04BBCE / 8C04BD60 between pairs) and static_faces.py / big_keys.py (the per-kind EV heads,
shape motions and keys). Why: the owner wants event faces with a switch, face animations he makes himself (enter / hold frames per
entry) and lip sync; the game's face code already runs in every world and draws the EV head (the hurt face shows today), but nothing
can drive it.
Tasks:
0 BASELINE before your first file goes into the tree: LIB "py -3.12 build.py --test" as the round start left it (keep the output);
  probe_faces.c rebuilt against SADC/build/research/super2/base094 (copied into your scratch) reproduces out_probe.txt's timelines
  (F3, F4, F7 numbers) - quote them.
1 THE PUBLIC SIDE, src/sa1_face.c (+ src/sa1_face.h, your internal header shared with the world file): sa1_character_face_mode,
  sa1_character_face, sa1_character_face_sequence, sa1_character_face_level, sa1_face_info_get (5.3). Validation first (5.3's
  errors; nothing applied on an error; sa1_fail texts in plain words: "Big's face cannot show F, G, Z"), then
  sa1w_character_world_ensure, then the world side's latch (4.2: the last call of each kind before a pair wins; the level is a
  float written every frame - no allocation, no log on that path; the level is a STATE that lasts until the next level call,
  so a frame that runs two pairs keeps it - review R.6). SA1_NO_WORLD: validation, then SA1_ERR_NOT_IMPLEMENTED.
  (review R.13) LAYERING: src/*.c compile with -std=c99 -Iinclude -Isrc only (build.py:430-440; CMake / MSVC for SA1_NO_WORLD):
  sa1_face.c includes libsa1.h, sa1_internal.h, sa1_api_world.h and sa1_face.h - never world/sa1_world.h (it needs SADC's
  sa1/guest.h) - and reaches the world through functions declared in sa1_face.h (struct sa1g_world opaque) and defined in
  src/world/sa1_world_face.c. Order of a call: the argument syntax, then sa1w_character_world_ensure, then the kind's letter mask
  (the world side computes it from its own arena with the rule below - never through sa1_face_info_get's disc read), then the
  latch (validated by magic before every write, also from the API side).
  sa1_face_info_get without a world, (review R.2) FROM THE DISC: sa1_load_disc frees every image it read (sa1_loader.c:601-602)
  and parses no face data, so nothing of 1ST_READ is left to read - open the disc of the last sa1_load_disc
  (sa1_api_ext_of(ctx)->gdi) with sa1_disc_open / sa1_disc_read_file, read 1ST_READ.BIN (Tikal: TIKAL_PROG.PRS through
  sa1_prs), compute, close; keep only the numbers in the caller's struct (no cache unless it is thread-safe; the context's
  calls are serialised by the host, other contexts may run on other threads): keys = the key count of the kind's EV-head shape
  motion (the motion the face task creator stores: Sonic 0x8C1FBBEC ...;
  faces_events.md 2.1 / 3 / 4.1 - keep a per-kind address table of numbers, verify each against the creator's code), letters = bit n
  when every key of faceTbl D_8C19C32C[n]'s steps is below keys (computed, never typed in: expect 0x7FFFFFFF and Big 0x01FC0C1F),
  steps / first / last_key from the table, ladder 0 5 4 6 7 (keys >= 8) / 0 4 5 (Big); kind 8, E-102, Eggman: keys 0; Tikal from
  TIKAL_PROG.PRS on the disc (review R.2); no disc: SA1_ERR_NOT_LOADED.
2 THE WORLD SIDE, src/world/sa1_world_face.c: at the first call for a world install w->face.pre / fill / ctx; the block sf_state in
  segment 0x18 (sa1g_world_ext_alloc; magic + self, made again after a rebuild - w->face.persist[0] keeps the mode); the pair-start
  algorithm of faces_wheel_plan.md 4.2 EXACTLY (not ready; mode; request; lip sync; upkeep; end) with the interrupt's nearer-key
  rule, the alternating script and step buffers, the held / lip-sync / close steps, the cutscene pace (31 doubled copies, the
  table's pointers swapped in and restored) and PFaceCancel where 4.2 / 4.4 say; the sequence builder of 4.3 (enter = the first
  step's time, 0 = the game's; the other steps from the world's current table; hold as steps {<= 127, last key}; HOLD_LAST's held
  step; at most 1,536 steps per buffer); the fill of 4.2 (face_shown from the face work's parent / sibling / object, face_letter
  through a parallel byte map of each step buffer). Call game functions the way the world files do (sa1_world_super.c's typed
  function pointers through the code map; SA1_GFN_C for guest pointers); pass guest addresses of your buffers, never host pointers.
  Sub steps other than 0: return at once. Nothing in the reference worlds: the hook is installed only by a call.
  REVIEW CHANGES to the mechanism (faces_wheel_plan.md 4.2 as amended): (R.3) LOOP scripts get no '0' and may not contain one;
  (R.4) PFaceCancel at a host START over a running gameplay face (W.seq != NULL); (R.5) face_shown also needs the drawn tree
  (not while w->sg.look; not while the queue head is another object); (R.6) 8C07B2DC only on the transition to NONE, never
  per pair - a world with only GAME-mode / level -1 calls must stay the game (F25); (R.8) P re-read every pair, a new P drops
  the activity and gets the mode again.
3 DOCUMENT in your file headers: the host-only writes (4.2's list), the priority (4.4), the UNVERIFIED points (the ladder's 5 / 4
  order by eye, the cutscene pace not run in the harness, the hidden-pair CPU pose drawing the static face - deferred, 4.8).
Files: LIB src/sa1_face.c, src/sa1_face.h, src/world/sa1_world_face.c (new), tests/face_test.c, tests/build_face.py (new; built like
tests/build_super.py against a snapshot of build/<target>/libsa1.a and its headers taken under the build lock; --run, --gdi,
--target windows / all; cases selectable by name). Nothing else.
Tests - tests/face_test.c on the owner's disc (flat GRID floor, one world per kind; read everything through the public API:
sa1_state_ex's face fields and sa1_character_render's draws; the hit through sa1_character_damage). Every case fails before (the
calls do not exist) and passes after; numbers are faces_events.md 5.4 / 7's measurements:
F1 no_call: Sonic, Tails, Knuckles, Amy, Big, 200 pairs and a hit, no face call: the game's hurt EV head pairs 1-16 for the four
   (EV-only textures drawn), none for Big; w->face.pre NULL; the face fields "none".
F2 event_mode: EVENT on (Sonic): face_shown 1 after the first pair, EV-only textures drawn from the second; 1,800 pairs: 450 idle
   sequences, 'B' 20-30 % (exact per seed: report it); EVENT off: face_shown 0 after one pair, the static face from the next.
F3 script_queue: "CDE" (no NOW) in EVENT mode: C 24 pairs, D 16, E 8, then the idle; every EV draw carries the EV-only textures
   (Sonic 25-27, Tails 19-20, Knuckles 2-4, Amy 9-10); face_letter 2 / 3 / 4 then 0 / 1.
F4 queue_vs_now: "V" two pairs into 'C': queue - 'V' after 21 pairs; NOW - from the next step; the drawn snap at the interrupt is at
   most half of the running step's blend (the nearer-key rule: rate below 0.5 -> its start key).
F5 loop: "Z" LOOP 20 pairs: face_from = face_to = 8 after the first 10 steps, activity HELD. (review R.3) In GAME mode AND in
   EVENT mode: in GAME mode face_shown stays 1 for all 20 pairs (no '0' appended; before: the static face after 10 steps); a
   LOOP script with '0': SA1_ERR_INVALID_ARGUMENT, nothing applied.
F6 game_end: GAME "E": EV head 8 pairs, then the static face (the appended '0'); activity NONE; the face work's seq NULL.
F7 lipsync_ladder: a 15-value envelope (Sonic and Big): the drawn position at pair n+1 = the written one at pair n + 1/64 (fit
   residual < 1e-4); the keys stay on the ladder (Big 0-4-5).
F8 lipsync_off: GAME, level < 0 after talking: the mouth closes over 6 steps (3 pairs) to key 0, then the static face the next pair;
   EVENT: then the idle.
F9 refusals_big: "G", "Z", "F" SA1_ERR_UNSUPPORTED; "q", "1" SA1_ERR_INVALID_ARGUMENT; a sequence with G UNSUPPORTED; the running
   activity continues unchanged, no fault (world alive after 100 pairs).
F10 no_face_kinds: E-102 and kind 8: SA1_ERR_UNSUPPORTED for every call; the General form: accepted, face_shown 0 while Super, 1 within
   2 pairs after SA1_ORDER_NORMAL; a puppet: SA1_ERR_UNSUPPORTED. (review R.5) face_shown is 0 while Super although the face
   task keeps Sonic's EV head linked into his undrawn normal tree (the link test alone gives 1).
F11 pace: PACE_CUTSCENE + "C": 48 pairs (24 without); off again: the table's 31 pointers equal the image's.
F12 event_off_cancel: EVENT on, a hit, EVENT off: no 'I' afterwards (PFaceCancel); the same with EVENT on and no switch: the hit's 'I'
   never shows while EVENT stays on (the game's priority; report it).
F13 info: sa1_face_info_get without any world (a fresh context with the disc): keys 21 / 20 / 20 / 20 / 8 (Sonic, Tails, Knuckles,
   Amy, Big), 0 (E-102, kind 8); letters 0x7FFFFFFF / Big 0x01FC0C1F; steps[2] 48, first[2] 5, last_key[25] 8, steps[30] 182;
   ladders 0-5-4-6-7 / 0-4-5; without a disc SA1_ERR_NOT_LOADED.
F14 threads: x86 = x64; two worlds on two threads with the same calls: identical face timelines.
F15 sequence_timing: GAME, {Z, 6, 30}: face_shown from the call's pair, face 0 -> 8 over 6 steps (3 pairs), 8 held 30 steps (15
   pairs), then the static face; face_letter 25 then -2; activity SEQUENCE then NONE.
F16 sequence_game_timing: {C, 0, 0} gives the same per-pair face_from / face_to / face_rate as sa1_character_face("C", NOW) (from the
   same state), and (review R.19) both show 'C' (face_letter 2 for 24 pairs): two no-ops would also be equal.
F17 sequence_event: EVENT, {b, 10, 90}, {A, 12, 0}: 10 + 90 + 12 steps, then the idle (face_letter 27, -2, 0, then 0 / 1).
F18 now_over_long_step: NOW into 'N' (48, 11) at count 5: face_next becomes face (snap to the start key, rate 0.10 -> 0); NOW over lip
   sync: starts at the next step.
F19 hold_last: {b, 10, 0} HOLD_LAST: key 17 held 600 pairs (re-armed), activity HELD; a new sequence then starts from 17.
F20 priority: lip sync at 0.8 every pair during F15's sequence: the sequence's timeline equals F15's, lip sync from the pair after its
   end; a held face + lip sync: the mouth ladder shows; level -1: close (6 steps), then {8, 17} {127, 17} and HELD again.
F21 sequence_args: count 33, anim 31, enter 128, hold 3601, reserved 1: SA1_ERR_INVALID_ARGUMENT, nothing applied; count 0 = stop.
F22 hurt_during: GAME, a hit during a sequence: no 'I' after it (PFaceCancel at its end).
F23 identity: two Sonic worlds, the same 1,300-pair input, one with GAME-mode sequences, scripts and lip sync throughout: position,
   speed, mode, action, rings and rand identical in every pair; with EVENT mode: all identical but rand from the first idle sequence
   on (report the first differing pair). (review R.11) The GAME-mode scripts contain no 'A' (a script's 'A' is the game's
   random idle choice: it calls rand()); one extra run with "A" reports where rand differs.
F24 rebuild_memory: under SA1_GUARD_ALLOC=1, 500 calls of each kind and a world rebuild between them (the test harness's revive / a
   re-applied image as the other tests do it): EVENT mode is back within 2 pairs after the rebuild (persist[0]); after destroy 0 live
   host blocks; the ext segment use stays bounded (one block per world).
F25 game_calls_only (review R.6): a Sonic world given sa1_character_face_mode(GAME) at creation and
   sa1_character_face_level(-1) before every pair, 200 pairs and a hit: the face block, the face work and the drawn faces (the
   hurt 'I' pairs 1-16) equal F1's no-call world pair for pair; position, speed, mode, action, rings and rand identical (fails
   before: no calls; fails with a per-pair PFaceCancel: no 'I').
F26 over_gameplay_face (review R.4): GAME, a hit, 4 pairs later {Z, 6, 30}: 'I' is dropped at the start and Z's first step is
   drawn from the 2nd pair after the call (without the cancel: Z waits behind 'I' and loses its first steps unseen); the same
   with lip sync starting during 'I'.
F27 talk_game (review R.3): GAME mode, style TALK, level 0.6 for 100 pairs: the EV head shows C / D / E in every pair (never the
   static face), face_letter 2 / 3 / 4; then level -1: closes over 6 steps, then the static face.
Gates: LIB "py -3.12 build.py --clean --test --target all" as your LAST build (strict: M1, M11, P7, HOSTOBJ, RINGS, DROWN, HUD, SUPER,
ITEMBOX; x86 = x64; unchanged by construction); tests/build_face.py --run (x64, x86; F24 under the guard allocator);
tests/build_memcheck.py --run; super_test (the old cases and (19)) and hostobj_test unchanged.
Hand 094a-gsa1-faces in api_notes: the API as built, sa1_face_info per kind, F3 / F7 / F15's numbers for its core test, the refusal
texts, the README lines (the event face switch, face animations, lip sync; a hit shows no hurt face while event faces are on, as in
the game's events).
```

### 6.3 `094a-lib-emote`

```
YOU ARE AGENT "094a-lib-emote" (round 0.9.4a addendum, LIB; in parallel with 094a-lib-anim, 094a-lib-gold, 094a-lib-audio and
094a-lib-faces). You write ONLY new files; you do NOT edit include/libsa1.h, src/world/sa1_world.h, src/world/sa1_world_tick.c,
src/sa1_api_world.h, src/world/sa1_api_world.c, src/sa1_character.c, src/sa1_manifest.c or src/world/sa1_world_super.c -
094a-lib-anim writes your names and hooks in its first step (faces_wheel_plan.md 5.3 / 5.4; develop in scratch until it has built,
as 3.2 says). Research: emote_wheel_lib.md (all: 2-8), emote_actions_{sonic,tails,knuckles,amy,gamma,big,supersonic}.tsv,
faces_wheel_plan.md 4.5 (the mechanism, with this plan's decisions) and 5.3. Probe to port: SADC/build/research/faces094a/
probe_emote.c (evq_cb: the queue calls from outside the world; the catalogue, identity, air, pops, super parts) with its
build_probe.py; scripts/ (dump_actions.py, annotate_actions.py: how the TSVs were made). Why: the owner's emote wheel plays any of a
character's animations, listed by internal name; the game's cutscenes already animate live players through the event action queue,
which every world has, and the probe showed it leaves the game state bit-identical.
Tasks:
0 BASELINE before your first file goes into the tree: LIB "py -3.12 build.py --test" (keep the output); probe_emote.c rebuilt against
  SADC/build/research/super2/base094 (in your scratch) reproduces out/probe_run.txt's identity and pacing results - quote them.
1 THE NAMES: tools/gen_emote_names.py (new LIB folder tools/) reads the seven TSVs (path argument; default
  ../sadc/docs/research next to LIB) and writes src/sa1_emote_names.c: per kind and form the OFFERED entries (rows with wheel = yes;
  the extras with wheel = yes; kind 8 and the General form from emote_actions_supersonic.tsv's kind8 / general columns), each with
  internal_name, label, class, flags (NEEDS_PROP from needs, HOLD from play, SUPER_ONLY, BOARD, OTHER_OBJECT from object / nodes,
  EXTRA), dup_of, and for extras the motion address; a header comment "GENERATED by tools/gen_emote_names.py from sadc
  docs/research/emote_actions_*.tsv: names and numbers only (no game data)"; --check compares the file with the TSVs. Rows'
  frames and play modes are NOT in the table: sa1_emote_get takes them from sa1_action_info_get (num_frames / mode of the parsed
  character set). (review R.2) The extras' frame counts ARE in the table (numbers, the TSVs' frames column): sa1_load_disc frees
  every image it read (sa1_loader.c:601-602) and parses only the action table's motions, so no extra's NJS_MOTION is left to
  read without a world; E12 checks each count against nbFrame at the extra's motion address in a world of its kind.
  (review R.9) dup_of per (kind, form) list from the object actually drawn (kind 8: the substituted Super body / ball) and the
  motion - never from the name: names are not unique (Knuckles' k_panch0 = rows 39 and 54, different motions; s_kimepose0 =
  rows 75 and 139, the same drawn object and motion in kind 8 and the General form, so dup there; ss_move2 = 133 and 134).
2 THE PUBLIC SIDE, src/sa1_emote.c (+ src/sa1_emote.h): sa1_emote_count / sa1_emote_get (no world; 0 / SA1_ERR_NOT_LOADED without
  a disc), sa1_character_emote, sa1_character_emote_stop (5.3): validation against the (kind, form) list of the character as it is
  (kind 0 with the General look while Super: FORM_SUPER; refusals with plain-word sa1_fail texts: "in the air", "hurt", "carrying
  something", "on the board", "<name> is not available for <character>"; (review R.16) "moving", "busy"; (review R.7) "the
  game's memory is full" with SA1_ERR_LIMIT), sa1w_character_world_ensure, the latch. (review R.13) src/sa1_emote.c and
  src/sa1_emote_names.c are src/*.c files (-std=c99, no world/sa1_world.h - it needs SADC's sa1/guest.h): the world side is
  reached through functions declared in sa1_emote.h and defined in src/world/sa1_world_emote.c.
  SA1_NO_WORLD: validation, then SA1_ERR_NOT_IMPLEMENTED.
3 THE WORLD SIDE, src/world/sa1_world_emote.c: install w->emote.pre / post / fill / ctx at the first call; a block in segment 0x18
  (magic + self; nothing persists across a rebuild: a rebuild ends the emote); faces_wheel_plan.md 4.5 EXACTLY: the start at a pair
  start (refused before the first pair: kept until after it - jvmode 1), the clear 8C04B48C first, per item 8C04B4D2 / 8C04B528 with
  the speed rule (2 x racio for kinds 0, 2, 3, 7, 8; racio for 5, 6; mtnmode 9 / 10 / 11 one frame per step, 12 0.5 per step, both
  scaled the same way), the link rule (0 across objects), the loop bit, CHAIN, the trailing stand entry; kind 8 - Sonic's objects
  substituted from sa1_manifest_char(SA1_CHAR_SUPER_SONIC)->subst with texlist 0x0C901EA0; the General form - plactptr as is, rows
  130-134 from sa1w_world_super_lifted (fall back to the B_CHAOS7 action with one SA1_LOG_WARN if it returns 0); Amy's 8C0DE46C
  (0, 1.0) / (0, 0.0); the input cancel before the pair (GetAnalog 8C06CD22 with NULL outputs, the jump / action press masks
  D_8C19E38C / D_8C19E390) and the state cancel after it (mode 1 / 71, wtimer, the 0x800 held flag, equipment 0x8000 and the look,
  the board); STOP NOW / BLEND; the fill (state, item, row / extra, eventwk f3C). No pad-off, no SetInputP, no event-running flag.
  REVIEW CHANGES (faces_wheel_plan.md 4.5 as amended): (R.1) GROUPS - never an unblended entry behind another in one queue
  (8C04B57C leaks the finished entry then: 48 bytes each, review probe out/probe_leak.txt); queue one group of blended entries,
  and when the queue has run empty (post hook) clear with 8C04B48C and queue the next group in the same hook; clear at the
  natural end too; (R.7) the heap headroom check before queueing (largest free block of B_8C79F03C > 4 KB + the entries, else
  SA1_ERR_LIMIT); (R.8) jvmode == 1 read at EVERY start and group start (else stay latched: 8C04BF00 spins for ever otherwise),
  a re-created player task drops the emote; (R.15) IN_AIR keeps the form / board / held-object cancels; (R.17) no pair-count
  time limits (the queue moves only on drawn pairs).
4 DOCUMENT: the cutscene precedent (EV001B), the identity result, the deferred render hint / CPU pose (faces_wheel_plan.md 4.8), the
  UNVERIFIED points (Big's rod in fishing rows, board rows before 0.9.5).
Files: LIB src/sa1_emote.c, src/sa1_emote.h, src/sa1_emote_names.c (generated), src/world/sa1_world_emote.c, tools/gen_emote_names.py
(all new), tests/emote_test.c, tests/build_emote.py (new; as tests/build_super.py). Nothing else.
Tests - tests/emote_test.c on the owner's disc (each fails before - the calls do not exist - and passes after):
E1 identity: two Sonic worlds, the same 1,300 pairs of input, one emoting 1,100+ pairs with cancels by input: position, speed, mode,
   action, flags, rings and rand identical in every pair.
E2 catalogue: every entry of every list (kinds 0, 2, 3, 5, 6, 7 normal; 0 SUPER; 8) plays without a fault: body draws > 0; once
   entries end within frames / speed + 2 pairs; the gameplay mode stays 1 (71); report the list sizes (they equal the TSVs' offered
   counts). (review R.17) The test camera keeps the character in view (the probe's set-up): the queue moves only on drawn pairs;
   E2 / E4 / E6's pair counts hold only then.
E3 cross_object: Sonic 82 then 83 and Knuckles 33 then 34 as two items (link 8): no fault, the second starts unblended.
E4 pacing: the queue frame advance per pair = 2 x racio for every kind, kind 8 included (the probe's adv_per_pair).
E5 cancel: stick (GetAnalog), jump, action: the queue empty before the pair; that pair's state equals the no-emote world's; hurt, a
   spring, the transformation and the revert, leaving the ground: cancelled after the pair; KEEP_INPUT: not cancelled by input.
E6 end: a once row with the trailing stand: end jump < 0.05 units; NO_TRAIL: the game's pop (0.02-0.20); STOP_BLEND on a loop ends
   within one loop length + link; STOP_NOW: the gameplay pose the next pair.
E7 air: IN_AIR from 60 units up: falls and lands as without (the same pairs), the queue plays throughout; without it:
   SA1_ERR_UNSUPPORTED with "in the air".
E8 first_pair: an emote asked before the first pair runs after it (never with jvmode != 1).
E9 super: kind 8 - Sonic's rows on the Super body (every body draw archive 0, SUPERSONIC), 130-141 own; the curled-body rows refused;
   General - GT rows draw archive 5; 130 drawn from sa1w_world_super_lifted: feet -1.17..-0.40 and top 11.04..11.64 (+-0.02, kind 8's
   range) where B_CHAOS7's own action is 1.0 lower; the revert cancels.
E10 amy: an attack row shows the hammer (+66 triangles: 932 -> 998), its end / a cancel / NO_PROPS: 932.
E11 refusals: an extra index past the list, a stage row (Sonic 88), an empty Big row (77), Super-only rows in a plain world, Amy's
   ad_dwalk (not in the list): SA1_ERR_INVALID_ARGUMENT; a puppet SA1_ERR_UNSUPPORTED; nothing queued.
E12 list: sa1_emote_count / get without a world (a fresh context with the disc): every internal_name and label equals the TSVs';
   frames equal sa1_action_info_get's num_frames for rows and (review R.2) each extra's table count equals nbFrame at its motion
   address in a world of its kind; dup_of points at an entry with the same drawn object and motion (review R.9: Knuckles' rows
   39 and 54, both k_panch0, are NOT duplicates); without a
   disc 0; gen_emote_names.py --check passes.
E13 with_faces: an emote and (when 094a-lib-faces has landed - (review R.14) NOT "#if defined(SA1_API_FACE)": 094a-lib-anim's step 1
   defines it before any face function exists, so emote_test would not link; build_emote.py passes -DEMOTE_TEST_FACES=1 only when
   the snapshot's libsa1.a defines sa1_character_face_sequence, else reports E13 "not run: faces not landed" for the verifier)
   a face sequence at once: both play, the face
   block's timeline equals one without the emote (8C04B48C never touches it).
E14 memcheck: under SA1_GUARD_ALLOC=1, 200 emotes started / cancelled / ended: the game heap's free total returns to its start value
   (review R.1: including emotes with unblended transitions - Tails' m_atack_1 (flight model) with its trailing stand, Sonic
   82 then 83, a two-item emote with link 0, CHAIN across objects - each of which leaked 48 bytes per run before the group rule),
   0 live host blocks after destroy.
E15 threads: x86 = x64; two worlds on two threads: identical.
E16 no_call: a world without an emote call has w->emote.pre NULL and replays as before.
E17 heap_low (review R.7): the test takes game heap blocks (the game's MAlloc through sa1g_world_call) until the largest free
   block is below the headroom: sa1_character_emote returns SA1_ERR_LIMIT "the game's memory is full", nothing queued, no fault;
   freed again, the same emote plays.
E18 jvmode_guard (review R.8): a request made while playerpwp[0]->mj.jvmode is not 1 (before the first pair; and after the test
   wrote 0 between pairs) is not queued at a pair start that reads != 1 and starts at the first one that reads 1;
   build_emote.py runs the binary with a time limit (a hang fails the case).
E19 groups (review R.1): Tails m_atack_1 (flight model, once) with the trailing stand: the stand is queued only after the flight
   entry ended (clear + queue in one post hook), every pair in between draws an emote entry, the heap's free total is back to
   its start; a loop of another object with STOP_BLEND ends at its wrap, then the stand.
Gates: LIB "py -3.12 build.py --clean --test --target all" as your LAST build (strict, every list; x86 = x64); tests/build_emote.py
--run (x64, x86; E14 under the guard allocator); tests/build_memcheck.py --run; super_test and hostobj_test unchanged.
Hand 094a-gsa1-wheel in api_notes: the API as built, the list sizes per (kind, form), the refusal texts, the pacing per kind, the
README lines (every animation by its internal name; animations of props play without the prop; moving cancels).
```

### 6.4 `094a-gsa1-faces`

```
YOU ARE AGENT "094a-gsa1-faces" (round 0.9.4a addendum, G-SA1 feature "faces"; after the five LIB agents have reported - their
api_notes are your API and their numbers win over this text; in parallel with 094a-gsa1 (Super Sonic) and 094a-gsa1-wheel, whose
files you never edit). Research: faces_wheel_ui.md 1, 4, 5, 6.1, 6.3, 7, 8.2, 9, 10.1 (items 15-22), 10.2, 10.3, 12;
faces_events.md 0, 4, 5.3, 6.10, 9; super2_plan.md 10.2; faces_wheel_plan.md 4.2-4.4, 4.7, 5.5, 5.6, 7.2, 8.2, 8.3. Owner rule:
every option is a ConVar with a control on a Q-menu page (Options > G-SA1). Why: the owner asked for event faces "animatable, easy
to use", a switch between event and regular faces, face animations he makes (a list of expressions; double-click appends; each
entry with frames to enter and frames to hold) and microphone lip sync like Garry's Mod's own.
Tasks:
1 THE CENSUS FIRST (other agents' builds run warm_test): tests/core_test.cpp (p, the census script only): when libsa1 has
  SA1_API_FACE, append to the scripted play of each face-capable character (kinds 0, 2, 3, 5, 7) a face segment - event mode on and
  a face animation through every valid letter (Tails also once in flight) - so every EV-only material state is drawn (Sonic 25-27,
  Tails 19-20, Knuckles 2-4, Amy 9-10 with flags 0x30, 27 also 0x20; Big 2 / 27); then "py -3.12 tools/gen_warm_census.py" regenerates
  addon/lua/gsa1/cl_warm_census.lua (never by hand) and tests/warm_test.lua passes. Land both files together, in one go.
2 THE MODULE, feature "faces" 1: src/module/bind_faces.cpp - faces_wheel_plan.md 5.6 (FaceInfo without a world, FaceMode,
  FaceSequence with the spec parsed and checked in C, FaceScript, FaceStop, FaceLevel - numbers only, no table, nothing returned,
  never an error thrown, FaceState from the character's last sa1_state_ex, the Mic* functions, gsa1.FACE from libsa1's macros);
  characters through session()->get(id) and session()->ctx() as bind_super.cpp does (no session.* edit). src/core/mic.hpp / mic.cpp
  (faces_wheel_ui.md 6.1): the BASS recording functions resolved from the already loaded bass.dll (GetModuleHandleW(L"bass.dll") +
  GetProcAddress, as src/core/bass.cpp does; bass.cpp is not edited); BASS_RecordInit(device) - BASS_ERROR_ALREADY means GMod
  initialised it: BASS_RecordSetDevice, never free it; BASS_RecordStart(16000 Hz, mono, float, 10 ms period, a RECORDPROC that only
  computes the block's RMS and peak into atomics - no Lua on that thread); MicListen = BASS_ChannelPlay / BASS_ChannelPause; the
  level = the RMS as dBFS mapped -60..-10 dB -> 0..1; the feature's close hook (kCloseDefault) stops the channel and frees only
  what it initialised; failure texts in plain words ("Windows does not let desktop apps use the microphone (Settings > Privacy &
  security > Microphone)"; Linux "not available"); a fake function table injectable for tests. Nothing is recorded or stored.
3 THE LUA, addon/lua/gsa1/feat/sh_faces.lua: GSA1.Faces = F with the contract of faces_wheel_plan.md 5.5 (094a-gsa1-wheel calls
  it), the 31-row catalogue (letter, our label, a short description in G-SA1's own words - (review R.19) not the sadocs
  wiki's text copied into the addon), the spec format
  "letter:enter:hold" (parse / serialize), the library file data/gsa1/face_anims.txt (8.2: parse, serialize, the defaults of 8.3,
  bad lines skipped and counted, names unique, at most 32 entries - (review R.10) 31 when "back" is not 0 and "keep the last
  face" is off, because G-SA1 appends {A, back, 0} unless back is 0 or "keep the last face" is ticked and libsa1 refuses more
  than SA1_FACE_SEQ_MAX (32) entries as a whole), F.CanShow from gsa1.FaceInfo's mask, F.Smooth (the lip-sync maths of
  faces_wheel_ui.md 5.4, pure). (review R.2) gsa1.FaceInfo(kind) reads the disc in libsa1 (tens of ms per call): F.Info(kind)
  calls it once per kind per session and keeps the table; never per frame.
  addon/lua/gsa1/feat/cl_faces.lua: the ConVars of faces_wheel_plan.md 7.2 (GSA1.ConVar); the switch (gsa1.FaceMode at once when it
  changes, and at GSA1CharacterCreated only while it is on - (review R.6) a character of an owner who never uses faces gets no
  face call at all; the key; gsa1_face_toggle); the ten hotkeys (a press plays the named face animation; a second press stops a
  "keep the last face" one; nothing while typing, in a menu or with the vgui cursor); lip sync per frame in GSA1PreTick
  (faces_wheel_ui.md 5.2-5.4: the source logic, MicOpen / MicListen on the talk key, gsa1.FaceLevel(id, s or -1, style)) - G-SA1
  sends levels whatever plays: libsa1 applies the priority (4.4); (review R.6) a level every frame while talking, ONE -1 when
  talking ends (also when lip sync is switched off, the source fails or the character changes while talking), then no FaceLevel
  call until the next talk (libsa1 keeps the last level as a state); the "Faces" page (order 285; 7.2's layout): Event faces, Face
  animations (the maker: the character combo - any entry with faces, default the current one; the expression list of that entry's
  valid face animations - #, letter, name, frames; DOUBLE-CLICK appends an entry with enter 0 = "the game's" and hold 0; the
  animation's entry list - #, expression, enter, plays, hold, total - with Remove, Up, Down and two number fields for the selected
  entry, enter 0-127 and hold 0-3600; "Back to neutral over N frames" 0-60 (8); "Keep the last face"; the name field; Save, Save as
  new, Rename, Delete; the library combo; Preview on the current character; the total "100 frames (1.67 s)"; the unit line "Frames:
  60 = 1 second (the game's own face timing)"; "Big can't show: ..." when the entry cannot show an entry), Face hotkeys (collapsed:
  ten rows, a DBinder and a library combo each), Lip sync (the controls of 7.2, the live meter: "Garry's Mod voice: speaking yes,
  level 0.43" / "Microphone: 0.40 (Headset Microphone)" / "Lip sync now: 0.38"); hook.Run("GSA1FaceLibraryChanged", change) after
  every save / rename / delete; status lines and "faces" log lines (faces_wheel_ui.md 9); console commands gsa1_face_toggle,
  gsa1_face_play <name>, gsa1_face_stop, gsa1_face_script <letters>, gsa1_lipsync_status.
Files: g-sa1 src/module/bind_faces.cpp, src/core/mic.hpp, src/core/mic.cpp, addon/lua/gsa1/feat/sh_faces.lua,
addon/lua/gsa1/feat/cl_faces.lua, tests/feat_faces_test.lua, tests/feat_faces_binding_test.cpp, tests/feat_faces_test.cpp (all
new); (p) tests/core_test.cpp (the census face segment only), addon/lua/gsa1/cl_warm_census.lua (regenerated only). NOT
tests/lua_mock.lua (094a-gsa1's: put your stubs in your own test files), cl_character.lua, cl_menu.lua, sh_config.lua, session.*,
gsa1_module.cpp, README, versions.
Tests (fail before -> pass after):
* feat_faces_test.lua (stubs in the file: input.*, the player's IsSpeaking / VoiceVolume / IsTyping, file.Read / Write over a
  memory table, the mock module's Face* / Mic* recording their calls): (1) every gsa1_face* / gsa1_lipsync* ConVar of 7.2 exists
  with its default and is bound to a control on the "Faces" page; (2) the switch calls gsa1.FaceMode(id, true / false, pace) once at
  once and (review R.6) for a new character only while the switch is on; never for gamma or the Super entries; (3) the library: the defaults without a file; a save / load
  round trip is equal; rename and delete fire GSA1FaceLibraryChanged with the change table; bad lines counted; unique names; 32
  entries at most; the read-only mirror gsa1_face_summary; (4) the maker: a double-click appends {letter, 0, 0}; Up / Down /
  Remove; the number fields clamp; the total line; (review R.10) a 32nd entry is refused while back > 0 and "keep" is off, and
  a 32-entry line in face_anims.txt with back > 0 is loaded as "keep" off with back 0 and a status note; Big's expression list is exactly the module mask's 14; Preview of the default
  "Happy" sends gsa1.FaceSequence(id, "b:10:90,A:8:0", 0) and with "keep the last face" "b:10:90" with FACE.HOLD_LAST; (5) F.Play /
  F.CanShow / F.Stop (Big + "Happy": false, "Big can't show b"); (6) hotkeys: once per press, a second press stops a held one,
  nothing while typing or in a menu; (7) the GMod source: a scripted IsSpeaking / VoiceVolume sequence gives FaceLevel values equal
  to F.Smooth's, exactly one -1 after the hang and then no FaceLevel call (review R.6), nothing with gsa1_lipsync 0 (one -1
  when it is switched off while talking); (8) Automatic: VoiceVolume 0 for 0.5 s while the talk key is held
  -> MicOpen once, MicListen(true) while held and (false) on release; never opened without the key (listen 0); one log line per
  decision; (9) E-102: no Face* call ever and the page says "E-102 has no face in the game".
* feat_faces_binding_test.cpp: gsa1.FACE equals libsa1's macros; gsa1.FEATURES.faces 1; FaceSequence with "b:10", "f:0:0",
  "b:200:0" and 33 entries -> nil, message, "bad_argument"; FaceInfo(kind) for every kind without a world (Big's mask 0x01FC0C1F);
  FaceLevel without a character returns nothing and throws nothing; mic.cpp with a fake BASS table: a 1 kHz sine whose RMS is -20 dBFS (peak
  0.1414; review R.19) ->
  level 0.80 +- 0.02, silence 0, BASS_ERROR_ALREADY -> the SetDevice path and no free at close, an init failure -> "failed: ...",
  MicListen(false) -> no more blocks counted.
* feat_faces_test.cpp (the disc; through Session's render as feat_super_test does): event mode on -> within 2 pairs Sonic's batches
  include archive 0 textures 25-27 with the EV-only states, off -> gone within 2 pairs; Big's EV head; FaceSequence "Z:6:30" ->
  FaceState's `to` 8 within 4 pairs and the static head again after about 18 pairs (094a-lib-faces' F15 numbers); a FaceLevel ramp
  0 -> 1 -> -1 walks the mouth ladder (from / to on it) and ends with the static head; the core census contains the EV-only rows
  (tools/gen_warm_census.py --check passes) and warm_test.lua warms them.
Gates: g-sa1 "py -3.12 build.py --clean --test --gdi <GDI> --maps <maps> --lock-timeout 1800" (ABSOLUTE paths), "py -3.12
tools/lua_check.py"; every 0.9.4 test and every test of 094a-gsa1 / 094a-gsa1-wheel unchanged (a failure in their files while they
work: wait and retry).
Owner-checklist lines into api_notes (6.8 lists the expected ones).
```

### 6.5 `094a-gsa1-wheel`

```
YOU ARE AGENT "094a-gsa1-wheel" (round 0.9.4a addendum, G-SA1 feature "emote" and the wheel; after the five LIB agents have
reported - their api_notes are your API; in parallel with 094a-gsa1 and 094a-gsa1-faces, whose files you never edit). Research:
faces_wheel_ui.md 1, 2, 3, 6.2, 7, 8.1, 9, 10.1 (items 1-14), 10.2, 10.3, 11, 12; emote_wheel_lib.md 0, 3, 4.4-4.7, 5.4, 8;
super2_plan.md 10.2 (the CORRECTED reading: binding); faces_wheel_plan.md 4.5, 4.6, 5.5, 5.6, 7.1, 8.1, 8.3. Owner rule: every
option is a ConVar with a control on a Q-menu page. Why: the owner wants a holdable button opening a GTA-V-style radial menu (no
slow motion) that plays the selected animation on release, slots swappable on an Options page that lists every animation by its
internal name, a wheel per character - and (10.2) each slot with a face animation, played together with the body animation by the
Left Trigger (or a rebindable key / button) in the open wheel, the centre's face animation alone when nothing is highlighted.
Tasks:
1 THE MODULE, feature "emote" 1: src/module/bind_emote.cpp - faces_wheel_plan.md 5.6: EmoteList(kind, form) without a world (from
  sa1_emote_count / get with the session's context; keys "a<row>" / "x<extra>"; play "loop" / "once" / "hold"), Emote(id, key,
  flags) (one item, SA1_EMOTE_CHAIN, link -1, EMOTE.LOOP -> SA1_EMOTE_PLAY_LOOP, EMOTE.IN_AIR -> SA1_EMOTE_IN_AIR; refusals as nil,
  the libsa1 text in plain words (scrubbed), a code), EmoteStop, EmoteState from the character's last sa1_state_ex, gsa1.EMOTE;
  characters through session()->get(id) (no session.* edit).
2 THE LUA, addon/lua/gsa1/feat/sh_wheel.lua: GSA1.Wheel = W - geometry (slot of a direction: slot 1 up, clockwise; 4-degree
  hysteresis; the centre inside 0.35 of the radius), the stick's keep timer (gsa1_wheel_stick_keep), the 6 convex quads per sector
  (precomputed per screen and wheel size), the file data/gsa1/emote_wheel.txt (8.1: parse, serialize, the read-only mirror
  gsa1_wheel_summary, (review R.9) a slot found again by its key while the key's entry still has the stored internal name,
  else by that name (the first entry with it that is no dup_of another) - internal names are NOT unique: Knuckles' k_panch0 is
  rows 39 and 54, two different motions - a missing name kept and shown "missing: <name>"), W.DEFAULTS (8.3, names only), the
  reserved-key check (faces_wheel_ui.md 2.5: the character's own keys, the interact, Super and voice keys refused for the wheel key;
  mouse buttons ARE allowed for the face key, which acts only while the wheel is open). addon/lua/gsa1/feat/cl_wheel.lua: the
  ConVars of 7.1; input (faces_wheel_ui.md 2.3 with 4.6 here): Think polling of the wheel key / button and the face key / button
  (edges), InputMouseApply only while open (cursor, zero mouse, return true), the GSA1.PadView wrap (no turn, no stored turn, Back
  swallowed), the chained GSA1.FilterButtons (presses that start while open never reach the game until released; SA1 L / R masked
  while open), (review R.18) the chained GSA1.ExtraButtons (cl_interact.lua's synthetic action press of the interact / lift keys:
  0 while open, the original otherwise), the PadView wrap CALLING the original and restoring the view angles after it (cl_pad.lua
  keeps the pending turn and the reset in locals: a wrap that skips the call saves the turn up for a jump at close), with
  gsa1_pad_dpad 1 and a D-pad wheel button FilterStick zeroes the stick while that button is held and the left stick rests (the
  D-pad would walk him while he chooses), its own PlayerBindPress hook blocking the wheel key's GMod bind while a character
  exists, the GSA1.BindOverride
  answer (mouse wheel = pages), GSA1.FilterStick (gsa1_wheel_move 0), the closing rules (character removed / dead / switched, the
  vgui cursor, the game UI, typing, GSA1.InputBlocked()); gsa1.Emote's flags: EMOTE.LOOP when the slot has "loop it",
  EMOTE.IN_AIR when gsa1_wheel_air is 1; RELEASE = the highlighted slot's body (none: nothing; "(stop)":
  EmoteStop; the playing one again: EmoteStop(STOP_BLEND) - (review R.18) "the playing one" = the key G-SA1 itself started
  last while EmoteState says PLAYING / ENDING: EmoteState's row follows CHAIN (75, then 76)); THE FACE TRIGGER (left mouse button / LT by default) = the highlighted
  slot's face animation (GSA1.Faces.Play(C, name, "wheel")) AND its body animation (gsa1.Emote) in the same frame, or at the centre
  only the centre's face animation; the wheel closes; the later release plays nothing (one press, one play); an entry without faces:
  the body only; the HUD (HUDPaint "gsa1_wheel" while open: sectors, slot numbers, internal names shortened with "...", a small face
  mark on slots with a face, the centre text - the highlighted entry's full name, "#75 - 65 frames - once", its face animation; at the
  centre the centre's face animation -, page dots, the hint "release: play - LT / left click: with its face - right click / B:
  cancel"); the toast (gsa1_wheel_toast; (review R.16) a refusal's libsa1 reason - with gsa1_wheel_move 1 and the stick held at
  the release that is "moving", and the toast adds "let go of the stick to pose"); "emote" log lines (faces_wheel_ui.md 9); console +gsa1_wheel / -gsa1_wheel,
  gsa1_emote <name or key>, gsa1_emote_stop. THE PAGE "Emote wheel" (order 280; 7.1): the keys and buttons; the character combo (all
  8 entries, editable without spawning; default the current one); pages 1-4 and "Clear page"; the mini wheel with a centre target
  (click selects a slot or the centre); for the selection: the body line (internal name, #, frames, plays) with Play / Clear, "loop
  it", "Face with it:" (a combo of GSA1.Faces.Library() names + "none"; greyed with "E-102 / Super Sonic have no face in the game";
  a name the entry cannot show is marked and refused with F.CanShow's reason); for the centre only the face combo; the list - every
  offered animation of the entry (gsa1.EmoteList), columns key / #, Internal name, Frames, Plays, Group, On wheel ("p1 s3"),
  duplicates folded BY dup_of (one row per drawn object and motion, "+2 variants" in the tooltip with the SA Tools label;
  (review R.9) never by name: Knuckles' rows 39 and 54 are both k_panch0 and both listed, with their numbers), a find box (name
  or number)
  and a group filter (All, then the classes; page-local, no ConVar); DOUBLE-CLICK puts the row into the selected slot and selects the
  next slot; right-click menu Put in slot 1-8 / Play now / Copy name; "Reset <character>'s wheel to the defaults"; the status line;
  GSA1FaceLibraryChanged updates renamed faces and clears deleted ones (and saves). Every call into GSA1.Faces is guarded (5.5).
3 CHECK THE DEFAULTS: every name of W.DEFAULTS (8.3) resolves to an offered entry of its (kind, form) list on the disc (the core test).
  (review R.18) Through a file, as warm_test.lua reads core_test's census: feat_emote_test.cpp writes
  build/<target>/emote_lists.txt (kind, form, key, internal name, dup_of of every entry: numbers and names only) and
  feat_wheel_test.lua checks W.DEFAULTS against it (skipped with a note when the file is missing); the C++ test does not parse Lua.
4 THE SEAMS, addon/lua/gsa1/cl_character.lua (p; re-read right before editing; anchor by function): (a) at the top of the
  PlayerBindPress "gsa1" function: `if GSA1.BindOverride then local r = GSA1.BindOverride(ply, b, pressed, code) if r ~= nil then
  return r end end` with the default `GSA1.BindOverride = GSA1.BindOverride or function() return nil end` in the seams block;
  (b) in tick_character right before GSA1.FilterButtons: `if GSA1.FilterStick then sx, sy = GSA1.FilterStick(C, sx, sy) end`
  (pcall'd like FilterButtons) with its default; (c) the seams comment lists both. Nothing else of the file.
Files: g-sa1 src/module/bind_emote.cpp, addon/lua/gsa1/feat/sh_wheel.lua, addon/lua/gsa1/feat/cl_wheel.lua,
tests/feat_wheel_test.lua, tests/feat_emote_binding_test.cpp, tests/feat_emote_test.cpp (all new); (p) addon/lua/gsa1/cl_character.lua
(task 4 only). NOT tests/lua_mock.lua (094a-gsa1's: stubs in your own test files), cl_super.lua, cl_menu.lua, sh_config.lua,
session.*, gsa1_module.cpp, README, versions; never the faces files (094a-gsa1-faces').
Tests (fail before -> pass after):
* feat_wheel_test.lua (stubs in the file: input.IsButtonDown / IsKeyDown / IsMouseDown / LookupBinding / GetKeyCode, GSA1.pad with
  raw / rx / ry, a cmd with GetMouseX / Y, SetMouseX / Y, GetViewAngles / SetViewAngles, GetMouseWheel, surface.DrawPoly counted,
  GSA1.Faces stubbed with recording Play / CanShow / Library, the mock module's EmoteList / Emote / EmoteStop / EmoteState): (1)
  every gsa1_wheel* ConVar of 7.1 exists with its default and is bound to a control on the "Emote wheel" page; (2) holding the key
  opens within one frame, HUDPaint draws only while open, a release with nothing highlighted calls gsa1.Emote zero times; (3) mouse:
  InputMouseApply(cmd, 0, -400) returns true, zeroes the mouse, lights slot 1; the 8 directions give slots 1..8; inside the centre
  none; the view angles unchanged; closed: nil; (4) stick: ry -1 lights slot 1; at rest it stays lit for gsa1_wheel_stick_keep s,
  then the centre (0: stays); a release while lit -> exactly one gsa1.Emote(id, <slot 1's key>, flags); B cancels; (5) no view turn
  and no stored turn through PadView while open; (6) clicks pressed while open are masked from GSA1.FilterButtons until released (the
  Super gesture's filter never sees them); a jump held before opening stays held; (review R.18) GSA1.ExtraButtons gives 0 while
  open, and a pad turn pending while open does not turn the view after the close (PadView's original was called and its angles
  restored); (7) the mouse wheel through GSA1.BindOverride
  pages while open and leaves gsa1_cam_distance alone; closed, the camera distance changes as before; (8) the wheel key's GMod bind
  is blocked only while a character exists; (9) closing on: character removed, vgui cursor, game UI, typing, InputBlocked -> nothing
  plays; (10) pages: empty ones skipped, LB / RB and keys 1-8, "Clear page" survives a reload; (11) storage: the format of 8.1 round
  trip, the face column and the centre line, a missing name kept and shown, a renumbered row found by name, bad lines counted, the
  mirror, (review R.9) slots a39 / a54 (both k_panch0) and a75 / a139 (both s_kimepose0) keep their own keys after a reload;
  (12) the page lists the mock module's rows by internal name and number, duplicates folded by dup_of (two different motions
  with one name are two rows), the find box and group filter
  work, double-click fills the selected slot, another entry's wheel is editable with no character; (13) a refusal shows the
  module's reason in the toast and an "emote ... refused" log line; (14) gsa1_wheel_move 0: FilterStick returns 0, 0 only while open;
  (15) THE FACE TRIGGER: slot 1 lit with body a75 and face "Happy": a left-click edge (and separately an LT edge, raw & 0x10000)
  calls gsa1.Emote(id, "a75", ...) and GSA1.Faces.Play(C, "Happy", "wheel") once each and closes; the later release calls nothing;
  at the centre only GSA1.Faces.Play(C, <the centre's face>); the gamma entry: only gsa1.Emote; a face the entry cannot show: the
  body plays and the toast gives F.CanShow's reason; rebinding gsa1_wheel_face_key / _pad moves the trigger; (16)
  GSA1FaceLibraryChanged: a renamed face is renamed in every slot and the centre, a deleted one cleared, the file saved; (17) the
  playing emote chosen again -> EmoteStop(id, STOP_BLEND); "(stop)" -> EmoteStop(id, STOP_NOW); (18) a slot with "loop it" sends
  EMOTE.LOOP and gsa1_wheel_air 1 sends EMOTE.IN_AIR (0: neither).
* feat_emote_binding_test.cpp: gsa1.EMOTE equals libsa1's macros; gsa1.FEATURES.emote 1; EmoteList(kind, form) for each of the 8
  entries without a world: non-empty names, keys parse back to (source, index); Emote with an unknown key -> nil, "unknown ...",
  "bad_argument"; with no character -> nil.
* feat_emote_test.cpp (the disc; Session's render): gsa1.Emote("a3") on a standing Sonic: EmoteState playing within 2 pairs and the
  drawn body differs from the stand; a stick input ends it and the next pair's state equals a no-emote session's; in the air: refused
  with "in the air"; running: refused with "moving" (review R.16); writes emote_lists.txt (task 3); every W.DEFAULTS slot of the 8 entries resolves by name to an offered entry of its (kind, form) list and plays
  for one pair without a fault.
Gates: g-sa1 "py -3.12 build.py --clean --test --gdi <GDI> --maps <maps> --lock-timeout 1800" (ABSOLUTE), "py -3.12
tools/lua_check.py"; every 0.9.4 test and the other two agents' tests unchanged (wait and retry while they work).
Owner-checklist lines into api_notes (6.8).
```

### 6.6 `094a-gsa1`: addendum (append to `super2_plan.md` 6.4's text; the same agent)

```
ADDENDUM (faces_wheel_plan.md 5.1 / 5.2): you run IN PARALLEL with 094a-gsa1-faces and 094a-gsa1-wheel (no shared file). You keep
tests/lua_mock.lua (p): they put their stubs into their own test files. tests/core_test.cpp and addon/lua/gsa1/cl_warm_census.lua are
094a-gsa1-faces' this round (its census face segment changes the core census; your S.CENSUS is feat_super_test's and is not
affected; cl_super.lua's S.coreSonic copies the regenerated core census at load - no edit of yours for it); cl_character.lua is
094a-gsa1-wheel's patch. A g-sa1 build or test failing in their files while they work: wait and retry; report it if it persists.
Your gates add their tests once they have landed (every test of the tree must pass at your final gate).
```

### 6.7 `094a-verify`: addendum (append to `super2_plan.md` 6.5's text; the same agent, also verifying this addendum)

```
ADDENDUM - also verify faces_wheel_plan.md (claims: the reports of 094a-lib-faces, 094a-lib-emote, 094a-gsa1-faces, 094a-gsa1-wheel
and 094a-lib-anim's task 1e / case (19)). From a clean state, in addition to super2_plan.md 6.5:
* LIB: tests/build_face.py --run and tests/build_emote.py --run (x64, x86; F24 and E14 under SA1_GUARD_ALLOC=1); super_test case
  (19); "py -3.12 tools/gen_emote_names.py --check" (the generated table equals the TSVs); the strict lists unchanged (the same
  build.py --clean --test --target all run); from SCRATCH COPIES rebuilt against this round's LIB (never edit SADC/build/research/*):
  res_faces/probe_faces.c (its timelines: 'CDE' 24 / 16 / 8 pairs, the queue's 21 pairs, L2's +1/64, Big 'G' now refused instead of
  faulting through the API) and probe_emote.c's identity part (state and rand identical) - they agree with face_test / emote_test;
* g-sa1: the same build.py --clean --test run covers feat_faces_* and feat_wheel_* / feat_emote_*; tools/gen_warm_census.py --check;
* "no call = unchanged": a world never given a face / emote call has w->face.pre and w->emote.pre NULL (case (19), F1, E16) and
  every strict reference list replays bit-exact;
* the mechanism is the game's: the face lane calls 8C04BBCE / 8C04BD60 / 8C07B2DC and writes only the fields faces_wheel_plan.md
  4.2 lists (read sa1_world_face.c: no other guest write); the emote lane calls 8C04B48C / 8C04B4D2 / 8C04B528 / 8C0DE46C and
  reads GetAnalog's result, and writes no game state (no SetInputP, no pad-off, no event-running flag, no reqaction);
* the owner rules: no game bytes in LIB or the addon - sa1_emote_names.c holds names, labels, classes, flags, numbers and
  addresses only; no face table, step, key or motion data anywhere in the repos or the addon (search the new files); the
  microphone is reduced to a level in memory and never written (no file write in mic.cpp, no network); every new ConVar of
  faces_wheel_plan.md 7 has a control on its page (the Lua tests' bound-ConVar checks pass and you read the page builders);
* ownership: the new lanes edited only their own new files; libsa1.h, sa1_world.h, sa1_world_tick.c (sa1g_step_game's four calls
  only), sa1_api_world.*, sa1_character.c had one editor (094a-lib-anim); cl_character.lua changed only by the two seams;
  core_test.cpp only by the census segment; cl_warm_census.lua equals gen_warm_census.py's output; lua_mock.lua only by 094a-gsa1;
* the owner's 10.2 (CORRECTED reading): release plays the body only; the face trigger (left mouse button and LT by default, both
  rebindable with a Q-menu control each) plays the slot's face animation together with its body animation, at the centre only the
  centre's face animation, then closes and the release plays nothing; face animations are entry lists with enter / hold frames,
  edited on the Faces page (double-click appends, remove, up / down, both numbers, preview, names, several saved), assignable to
  slots and the centre per character; the frame unit is stated in the UI (60 per second);
* Big's 14 face animations offered and the 17 others refused (never a fault); E-102 and both Super Sonics: no face controls / calls;
* (review) the review's required changes (R.20): the game heap's free total after E14 / E19 is back at its start (R.1; the raw
  queue's leak is shown by SADC/build/research/faces094a/review/probe_leak.c, the lane must not have it); F25 - a world with only
  GAME-mode / level -1 calls is the game (R.6); F26 (R.4); F5 in GAME mode and F27 (R.3); F10's face_shown 0 while Super (R.5);
  E17 / E18 (R.7 / R.8); sa1_face_info_get on a fresh context without a world (R.2); feat_faces_test.lua: no face call for a
  character while the switch is off and nobody talks (R.6); the wheel keeps a39 / a54 apart (R.9);
* every "Required change" of this addendum's review, and every owner-checklist line of 6.8 is plausible and testable in game.
Report {holds, fails, evidence} for both plans.
```

### 6.8 Release step: addendum and owner checklist (append to `super2_plan.md` 6.6)

1. **Versions:** G-SA1 features `faces` 1 and `emote` 1 (set by their agents); the one `SA1_VERSION_MINOR` bump of 0.9.4a covers
   both plans (its history line is 5.3's); module / addon VERSION "0.9.4a".
2. **README:** "What 0.9.4a changes" gains three parts in the owner's words (event faces and the face animation maker; lip sync;
   the emote wheel with its face trigger) plus the spawn-menu tab (rounds plan 14.1t, already in the dev tree); new sections
   "Faces" and "Emote wheel" (pages, keys, the data files `data/gsa1/face_anims.txt` and `data/gsa1/emote_wheel.txt`, the module
   functions of 5.6, the microphone privacy line: only a loudness number, nothing recorded or sent, only while the talk key is
   held by default); known limits (11: a hit shows no hurt face while event faces are on; the face moves at the game's 30 Hz
   rate; props are not drawn in prop animations; others do not see faces or emotes yet; (review R.17) an animation pauses while
   the character is outside the camera's view and goes on when he is seen again); the owner checklist below.
3. Gates and the install as `super2_plan.md` 6.6; the material census was regenerated by `094a-gsa1-faces` (no map-cache rebuild).
4. **Records:** status page, `gsa1_progress.html`, HANDOFF, memory, rounds plan 14 (0.9.4a released with faces and the wheel; the
   owner questions of 10 still open).

**Owner checklist (README):**

1. **Event faces.** Q menu > Options > G-SA1 > Faces: tick "Event faces" - Sonic wears his cutscene head and blinks now and then;
   untick - the normal face. Bind the switch key and flip it while running. With event faces on, get hit: no hurt face (the game's
   cutscenes do the same; tell me if you want it otherwise).
2. **A face animation.** On the same page pick Sonic, double-click "Happy" then "Neutral"; set Happy's enter to 10 and hold to 90;
   "Preview": he smiles quickly, keeps it about 1.5 s, then relaxes. Save it as "Grin". Make another with "Surprised" and "Eyes
   closed"; move entries up / down; remove one. Pick Big: only his 14 expressions are listed; E-102 and Super Sonic: the page says
   they have no face in the game.
3. **Hotkeys.** Bind "Grin" to a key in "Face hotkeys" and press it while running.
4. **Lip sync.** Hold your voice chat key (X) and talk: the mouth opens and closes with your voice; stop talking: it closes and the
   face goes back after a moment. The meter on the page shows which source works ("Garry's Mod voice" or "Microphone"). Try both
   mouth styles.
5. **The wheel.** Hold TAB (controller: D-pad down): the ring shows, the game keeps running and the camera stays still while you
   point with the mouse (or the right stick). Release on Sonic's level-clear pose: he plays it and goes back to standing; move:
   it stops at once. Release in the middle: nothing. The mouse wheel / LB / RB flips pages.
6. **Faces in the wheel.** Q menu > Options > G-SA1 > Emote wheel: pick a slot, set "Face with it" to "Grin"; click the centre and
   give it a face animation. In game: open the wheel, point at the slot and press LT (keyboard: left click): the pose and the grin
   play together and the wheel closes. Open it again and press LT without pointing: only the centre's face plays while you keep
   running.
7. **Choosing animations.** On the Emote wheel page pick Knuckles: the list shows all his animations by internal name (like
   `k_taiki1`) with number and length; double-click one into a slot, "Play" it. Each character keeps his own wheel; "Reset to the
   defaults" brings the starting set back.
8. **Nothing else changed:** without the wheel and faces every character plays as in 0.9.4; the Super Sonic polish items of
   `super2_plan.md` 6.6 are checked as listed there.
9. Report anything that feels wrong with `!note <text>`.

---------------------------------------------------------------------------------------------------------------------

## 7. Every option, its Q-menu control and its default

All client ConVars (`GSA1.ConVar`, archived); "at once" = applied to a running character without re-creating it.

### 7.1 Q menu > Options > G-SA1 > **Emote wheel** (order 280; `094a-gsa1-wheel`)

| Control (label) | ConVar | Default | Help line |
|---|---|---|---|
| checkbox "Emote wheel" | `gsa1_wheel` | 1 | "Hold the wheel key, point at an animation with the mouse or the right stick, release to play it. The game keeps running; the camera does not turn while you choose." |
| DBinder "Wheel key (hold)" + "No key" | `gsa1_wheel_key` | 67 (TAB) | "TAB like GTA V's weapon wheel; while you are a character TAB does not open the scoreboard. Keys your character uses are refused." |
| combo "Controller button (hold)" (`S.PAD_BUTTONS`) | `gsa1_wheel_pad` | 0x0002 (D-pad down) | "LB is GTA's, but it is the game's L (Big's fishing camera uses L + R)." |
| DBinder "Face trigger in the open wheel" | `gsa1_wheel_face_key` | 107 (left mouse button) | "Plays the highlighted slot's face animation together with its animation, or only the centre's face animation; the wheel closes. Mouse buttons are fine: they do nothing else while the wheel is open." |
| combo "Face trigger (controller)" | `gsa1_wheel_face_pad` | 0x10000 (LT) | "LT does not turn the camera while the wheel is open." |
| slider "Mouse sensitivity in the wheel" 0.25-4 | `gsa1_wheel_mouse` | 1 | "How far the mouse moves to reach a slot." |
| slider "Right stick: point at least" 0.2-0.9 | `gsa1_wheel_deadzone` | 0.5 | |
| slider "Right stick: keep the slot lit for (s)" 0-2 | `gsa1_wheel_stick_keep` | 0.5 | "After the stick goes back to rest; then the centre is chosen. 0 = it stays lit (GTA style; the centre only if you never pointed)." |
| slider "Wheel size" 0.5-2 | `gsa1_wheel_size` | 1 | |
| slider "Wheel background" 0.2-1 | `gsa1_wheel_opacity` | 0.75 | |
| checkbox "The character can keep moving while the wheel is open" | `gsa1_wheel_move` | 1 | "Off: he stops (no stick) while you choose." |
| checkbox "Allow animations in the air" | `gsa1_wheel_air` | 0 | "On: an animation can start while jumping or falling; he keeps falling and lands while posing." |
| checkbox "A quick tap plays the last one again" | `gsa1_wheel_tap` | 0 | |
| checkbox "Show the name of what plays" | `gsa1_wheel_toast` | 1 | "Also says why an animation could not play." |
| status line (read-only) | `gsa1_wheel_summary` | "" | the mirror of `emote_wheel.txt` for the session log ("sonic 9/33; big 9/33") |
| the character combo, pages, mini wheel with centre, slot editor (body, "loop it", "Face with it"), the list (find box, group filter), Play / Clear / Clear page / Reset | (the file `data/gsa1/emote_wheel.txt`, 8.1) | per entry: 8.3 | the slots are data, edited by these controls (as the Surfaces page's table) |

### 7.2 Q menu > Options > G-SA1 > **Faces** (order 285; `094a-gsa1-faces`)

| Section | Control (label) | ConVar | Default | Help line |
|---|---|---|---|---|
| Event faces | checkbox "Event faces (the cutscene face) instead of the regular face" | `gsa1_face_event` | 0 | "On: the character always wears its cutscene head and blinks like in the game's cutscenes (a hit then shows no hurt face, as in the game's events). Off: the regular face; face animations and talking show the cutscene head while they play. Applies at once." |
| Event faces | DBinder "Switch key" + "No key" | `gsa1_face_event_key` | 0 | "Or bind gsa1_face_toggle." |
| Event faces | checkbox "Cutscene speed for blinks and the game's expressions" | `gsa1_face_pace` | 0 | "The game's cutscenes play faces at half speed. Your own enter / hold frames are never changed." |
| Face animations | the maker (character combo, expression list, entry list with Remove / Up / Down and the enter / hold fields, "Back to neutral over N frames", "Keep the last face", name, Save / Save as new / Rename / Delete, library combo, Preview) | (the file `data/gsa1/face_anims.txt`, 8.2) | 8.3 | "Double-click an expression to add it. Frames: 60 = 1 second (the game's own face timing). Enter: how long the change into the expression takes (0 = the game's). Hold: how long its last face stays." |
| Face animations | status line (read-only) | `gsa1_face_summary` | "" | the mirror of `face_anims.txt` ("11 face animations") for the session log |
| Face hotkeys (collapsed) | 10 x (DBinder "Key n", combo "Face animation") | `gsa1_face_hotkey_1` .. `_10`, `gsa1_face_hotkey_anim_1` .. `_10` | 0, "" | "A press plays it; a face animation that keeps its last face is let go by a second press." |
| Lip sync | checkbox "Lip sync: the mouth moves when you talk" | `gsa1_lipsync` | 1 | "Like Garry's Mod's own mouths. Only while you hold your voice chat key unless you choose 'always'." |
| Lip sync | combo "Voice from": Automatic / Garry's Mod voice chat / The microphone (G-SA1 listens itself) | `gsa1_lipsync_source` | 0 | "Automatic uses Garry's Mod's voice level when it reports yours, else G-SA1 reads the microphone itself (only the loudness; nothing is recorded or sent)." |
| Lip sync | combo "Listen": Only while I hold the voice chat key / Always (open microphone) | `gsa1_lipsync_listen` | 0 | |
| Lip sync | DBinder "Talk key (none = your voice chat key)" | `gsa1_lipsync_key` | 0 | |
| Lip sync | combo "Microphone": Windows default / the device names | `gsa1_lipsync_device` | -1 | |
| Lip sync | combo "Mouth": Opens with your voice (like Garry's Mod) / The game's talking animations | `gsa1_lipsync_style` | 0 | |
| Lip sync | slider "Sensitivity" 0.5-8 | `gsa1_lipsync_gain` | 2 | "2 is Garry's Mod's own." |
| Lip sync | slider "Ignore sounds quieter than" 0-0.5 | `gsa1_lipsync_gate` | 0.05 | "Raise it if the mouth moves with background noise." |
| Lip sync | slider "Mouth closes in (s)" 0.03-0.5 | `gsa1_lipsync_release` | 0.12 | |
| Lip sync | slider "Back to the regular face after (s) of silence" 0.2-3 | `gsa1_lipsync_hang` | 0.6 | |
| Lip sync | live meter + lines | - | - | "Garry's Mod voice: speaking yes, level 0.43" / "Microphone: 0.40 (Headset Microphone)" / "Lip sync now: 0.38" |

Console extras (never the only way): `gsa1_face_toggle`, `gsa1_face_play <name>`, `gsa1_face_stop`, `gsa1_face_script <letters>`,
`gsa1_lipsync_status`, `+gsa1_wheel` / `-gsa1_wheel`, `gsa1_emote <name or key>`, `gsa1_emote_stop`.

### 7.3 libsa1 (host API; the reference worlds never call any of it)

| Call / option | Values | Default | Worlds |
|---|---|---|---|
| `sa1_character_face_mode` | GAME / EVENT, flag PACE_CUTSCENE | GAME, no call | Sonic, Tails, Knuckles, Amy, Big (General Sonic: shown while not Super) |
| `sa1_character_face` / `_face_sequence` / `_face_level` | 5.3 | no call | the same |
| `sa1_character_emote` / `_emote_stop` | 5.3 | no call | the six playable kinds and kind 8 (not puppets, Eggman, Tikal) |

---------------------------------------------------------------------------------------------------------------------

## 8. Data files and defaults

### 8.1 `garrysmod/data/gsa1/emote_wheel.txt` (`094a-gsa1-wheel`)

```
# G-SA1 emote wheel - written by Q menu > Options > G-SA1 > Emote wheel; one line per slot, tab-separated:
# entry  page  slot  body  name  face  options  label
#   entry    a GSA1.CHARACTERS name: sonic tails knuckles amy gamma big supersonic supersonic_general
#   page     1-4 and slot 1-8 (1 = up, then clockwise); page 0 slot 0 = the centre (its body is always "-")
#   body     a<row> (an action row) | x<extra> (an extra motion) | stop | - (no body animation)
#   name     the body's internal name ("-" for stop / -): the key is used while its entry has this name, else the name finds
#            the entry again (a renumbered row; review R.9: names are not unique, so never the name alone); a name found
#            nowhere is kept in the file and shown as "missing: <name>"
#   face     a face animation's name in face_anims.txt, or "-"
#   options  comma list (loop) or "-"
#   label    reserved for custom names (later); empty
version	1
sonic	1	1	a75	s_kimepose0	Happy	-	
sonic	0	0	-	-	Happy	-	
```

A character's wheel is its defaults (8.3) until its first edit; from then on the file holds all of its slots (written whole, so
cleared slots stay cleared: a line with body "-" and face "-"). "Reset to the defaults" removes its lines. Bad lines are skipped
and counted on the page's status line. The read-only mirror `gsa1_wheel_summary` carries "entry used/33" per edited entry.

### 8.2 `garrysmod/data/gsa1/face_anims.txt` (`094a-gsa1-faces`)

```
# G-SA1 face animations - written by Q menu > Options > G-SA1 > Faces; one face animation per line, tab-separated:
# name  back  keep  entries  label
#   name     1-32 characters, unique, no tab or comma
#   back     frames (60 = 1 s) of the blend back to the neutral face at the end; 0 = none (G-SA1 appends A:<back>:0)
#   keep     1 = the last face stays until the next face command (then "back" is not appended); 0 = no
#   entries  comma list of letter:enter:hold - letter A-Z a-e = the game's face animations 0-30; enter 0-127 (0 = the
#            game's own); hold 0-3600; at most 32 entries, 31 when back is not 0 and keep is 0 (review R.10)
#   label    reserved; empty
version	1
Happy	8	0	b:10:90	
Talk	8	0	C:0:0,D:0:0,E:0:0	
```

### 8.3 Defaults (names only; G-SA1's own words; letters are the game's animation ids, not data)

**Face animations** (`F.DEFAULTS` in `sh_faces.lua`):

| Name | Entries (letter:enter:hold) | back | keep | Big can show |
|---|---|---|---|---|
| Happy | b:10:90 | 8 | 0 | no |
| Surprised | Z:6:60 | 8 | 0 | no |
| Sad | N:20:60 | 12 | 0 | no |
| Talk | C:0:0, D:0:0, E:0:0 | 8 | 0 | yes |
| Blink | B:0:0 | 0 | 0 | yes |
| Eyes closed | V:8:60, X:0:0 | 0 | 0 | yes |
| Sleepy | W:0:0, U:0:30, X:0:0 | 0 | 0 | yes |
| Ha! | P:0:30 | 8 | 0 | no |
| Angry talk | F:0:0 | 8 | 0 | no |
| Shock | J:0:30 | 12 | 0 | no |
| Squint | T:0:0 | 8 | 0 | yes |

**Wheels, page 1** (`W.DEFAULTS` in `sh_wheel.lua`; bodies from `emote_wheel_lib.md` 3.4, checked in the TSVs: every one is offered
with `wheel = yes`; `x` = an extra motion; faces from the table above; pages 2-4 empty):

| Entry | Slots 1-8 (body / face) | Centre |
|---|---|---|
| sonic | a75 s_kimepose0 / Happy; a3 s_wait3 / -; a5 s_wait_a1 / Blink; a26 s_rollfly / Surprised; a70 s_zitabata / Shock; a81 s_furatsuki0 / Eyes closed; a84 s_awa0 / -; a47 s_tukamari0 / - | Happy |
| tails | a54 m_kimepose0 / Happy; a56 m_losepose0 / Sad; x m_taiki0 / Sleepy; a5 m_wait_a1 / Blink; a33 m_rollfly0 / Surprised; a94 m_zitabata / Shock; a37 m_flyup / -; a43 m_atack_1 / - | Happy |
| knuckles | a40 k_kimepose1 / Happy; x k_taiki1 / Angry talk; x k_wait_b1 / -; x k_item_fg1 / -; a5 k_wait_a1 / Blink; a39 k_panch0 / -; a56 k_panch2 / -; a30 k_rollfly0 / Surprised | Happy |
| amy | a42 a_kimepose0 / Happy; x a_taiki0 / Angry talk; x a_kakure0 / Surprised; x a_mogura1 / -; a5 a_wait_a1 / Blink; a85 a_rollat0 / -; a81 a_attack1 / -; a87 a_rollat2 / Eyes closed | Happy |
| gamma | a67 e_taiki0; x e_kimepose0; a43 e_wait2; a46 e_wwait0; a49 e_hover1; a77 e_item_f1; a80 e_item_g1; a62 e_shoot0 (no faces) | - |
| big | a48 b_kimepose0 / Talk; a2 b_wait_a1 / Blink; a60 b_joysmall / Talk; a61 b_joymiddle / -; a62 b_joybig0 / Talk; a47 b_zitabata0 / -; a92 b_wwait0 / Sleepy; x b_item_u1 / - | Talk |
| supersonic, supersonic_general | a130 ss_wait0; a139 s_kimepose0; a131 ss_move0; a132 ss_move1; a133 ss_move2; a3 s_wait3; a5 s_wait_a1; a81 s_furatsuki0 (no faces) | - |

Extras are stored with their list index and matched by name (`x<index>` is filled from `gsa1.EmoteList` at the first use).
(review R.9) Rows and extras alike: the stored key wins while its entry still has the stored internal name; only then the name
(the first entry with it that is no `dup_of` another). Internal names are not unique (Knuckles' `k_panch0` = rows 39 and 54,
different motions; `s_kimepose0` = 75 and 139; `ss_move2` = 133 and 134).

---------------------------------------------------------------------------------------------------------------------

## 9. Acceptance (this addendum's part of 0.9.4a)

| Release | Accepted when (in addition to `super2_plan.md` 9) |
|---|---|
| 0.9.4a | LIB strict replay of every list unchanged (x86 = x64); super_test (19); face_test F1-F27 and emote_test E1-E19 pass (review: F25-F27, E17-E19 added) (x64, x86; F24 / E14 under the guard allocator); `gen_emote_names.py --check`; the research probes rebuilt against the final LIB agree; g-sa1 feat_faces_*, feat_wheel_test.lua, feat_emote_* pass, `gen_warm_census.py --check` with the EV-only rows; every ConVar of 7 has its control; the owner rules hold (no game bytes; the microphone only a number in memory); the ownership rules hold; 10.2's corrected reading is what the wheel does |

---------------------------------------------------------------------------------------------------------------------

## 10. Owner questions (none blocks; the plan proceeds on the default)

| # | Question (plain words) | Default |
|---|---|---|
| FQ1 | Which key opens the wheel: TAB (like GTA V on PC; while you are a character TAB then does not open the scoreboard) or H (free, no clash)? | TAB |
| FQ2 | Controller button for the wheel: D-pad down (free today) or LB (like GTA, but LB is the game's L, which Big's fishing camera uses)? | D-pad down |
| FQ3 | The face trigger on keyboard and mouse: the left mouse button while the wheel is open (it does nothing else then)? (Controller: LT, as you asked.) | left mouse button |
| FQ4 | Event faces when you start: the normal face (the switch turns the cutscene head on), or the cutscene head all the time? | the normal face |
| FQ5 | For expressions that are little animations themselves (talking, blinking): "enter" sets how fast the first change happens and "hold" how long the last face stays; the steps in between keep the game's own timing. OK, or should "enter" stretch the whole little animation? | as described |
| FQ6 | Frames are the game's own face frames: 60 per second. OK? | 60 per second |
| FQ7 | At the end of a face animation the face blends back to neutral over 8 frames (a setting per animation; 0 = jumps back like the game). OK? | 8 frames |
| FQ8 | With event faces on, a hit shows no hurt face (the game's cutscenes work that way). Keep, or show the hurt face over everything? | keep the game's way |
| FQ9 | Lip sync: may G-SA1 listen to your microphone itself (only the loudness, nothing recorded or sent) when Garry's Mod gives no voice level in single player - only while you hold your voice chat key? | yes, only while the key is held |
| FQ10 | Mouth style: opens with your voice like Garry's Mod's mouths, or the game's own talking animations while you speak? | opens with your voice |
| FQ11 | Faces at the game's cutscene speed (slower blinks and expressions) as an option? | option, off |
| FQ12 | Super Sonic has no cutscene face in the game. Keep it that way, or put Sonic's face on him (blue skin and green eyes would show on the golden body)? | keep (no face) |
| FQ13 | Animations from the wheel while jumping or falling? | only standing; option "Allow animations in the air" off |
| FQ14 | Choosing the animation that is playing again: let it finish and blend back to standing, or stop at once? | blend back |
| FQ15 | The list shows every animation, also the ones made for props that are not there (carrying, climbing, the board, Big's fishing): show them all (marked), or hide those? | show all, marked |
| FQ16 | The game's own variants of one animation (the same move with another loop setting): one line per animation in the list? | one line per animation |
| FQ17 | The game says its idle line after 30 seconds of standing - also while your character does an animation from the wheel? | yes, the game's rule |
| FQ18 | Controller: after you point and let the stick go back, the slot stays lit for half a second, then the centre is chosen (so LT at the centre reaches the centre's face). OK? | 0.5 s (a slider) |
| FQ19 | (review R.19) You asked for a list of ALL the animations. About 35 of the game's rows cannot play in G-SA1 at all (they belong to stages such as Twinkle Park, the mine cart, or are empty). Show them greyed with the reason, or leave them out? | leave them out (the README names them) |

The questions of `faces_events.md` 9, `emote_wheel_lib.md` 8 and `faces_wheel_ui.md` 11 are all in this list or settled by 10.2
(the wheel's face page and the slot kinds of `faces_wheel_ui.md` are replaced by the owner's slot = body + face).


### 10.1 OWNER ANSWERS (2026-10-26 ~20:45, BINDING: they override the defaults above and any agent text that assumes otherwise)

| # | Owner's answer | What it means |
|---|---|---|
| FQ1 | "TAB" | default: TAB opens the wheel |
| FQ2 | "D-pad Down is good" | default |
| FQ3 | "Left Mouse is good" | default: the left mouse button is the keyboard / mouse face trigger |
| FQ4 | "Normal face" | default: event faces start off (the switch turns the cutscene head on) |
| FQ9 | "Single Player lip sync is good" | default: G-SA1 may read the microphone's loudness itself in single player, only while the voice key is held |
| FQ10 | "Like Garry's Mod" | default: the mouth opens with the voice |
| FQ12 | "No Super Sonic cutscene face" | default: Super Sonic keeps no face |
| FQ13 | "With an option to allow them in air" | default: standing only, plus the option "Allow animations in the air" (off) with its Q-menu control |
| FQ19 | "Show them greyed out with a hover tooltip that states why they are unusable for now" | NOT the default. The Emote wheel page lists the ~35 unplayable rows too, GREYED OUT (not assignable to a slot; double-click does nothing), each with a HOVER TOOLTIP giving the reason in plain words (e.g. "Twinkle Park's own animation: needs that stage's objects - not usable yet", "mine cart", "empty in the game's table"). The rows come from the addon's generated name table (names and numbers only, from emote_actions_*.tsv with each row's reason); the libsa1 catalogue API need not return them. Tests: the page shows them greyed with a tooltip and refuses to assign them. |

FQ5-FQ8, FQ11, FQ14-FQ18 were not asked; their defaults stand.

---------------------------------------------------------------------------------------------------------------------

## 11. Risks and UNVERIFIED points

| Risk / point | Where | Mitigation |
|---|---|---|
| Five LIB agents at once; the lanes depend on `094a-lib-anim`'s step 1 | LIB | the contract is written out verbatim here (5.3 / 5.4) and is step 1; the lanes work in scratch until it has built; new files enter `src/` only when they compile; the lock and wait-and-retry; the verifier rebuilds from clean |
| `094a-lib-anim` grows (task 1e, case (19)) | LIB | 1e is text pasted from 5.3 / 5.4 plus two small functions; the getter may return 0 until its GA lands |
| The NOW interrupt snaps the face (at most half of the running step's blend: the nearer key) | look | a host refinement of the game's interrupt (measured snap was small); face_test F4 / F18; keys only blend key to key, a smoother start is impossible with the game's block |
| The face shows one pair after the body when both start together (the draw precedes the face task) | look | 33 ms; the game's own order; noted in the README |
| Mouth ladder order 0-5-4-6-7 (5 before 4) | look | **UNVERIFIED** by eye per character; `sa1_face_info.ladder` can be changed in LIB alone if the owner reports it |
| `LocalPlayer():VoiceVolume()` / `IsSpeaking()` for yourself in single player | lip sync | **UNVERIFIED**; Automatic falls back to the module's microphone; the meter shows both |
| BASS recording on GMod's `bass.dll` next to Steam voice on the same microphone; the Windows microphone indicator while paused | lip sync | **UNVERIFIED**; failures are reported in plain words and never touch the sound output; push-to-talk keeps capture paused between presses |
| Lip-sync latency 80-130 ms | lip sync | **UNVERIFIED** until measured in game |
| The cutscene pace (events one step per frame) | faces | asm + the wiki, not run in the harness; only an option, off by default |
| The CPU pose of a hidden pair draws the static face (faces) / the gameplay pose (emotes); the render hint reads the gameplay action while emoting | LIB host | deferred (4.8): measured deviation <= 0.81 vs the gameplay's 1.20; hidden pairs are rare |
| Tails' and Big's eyes do not follow look-at while the EV head shows (eye nodes inside the swapped subtree) | look | the game's data; matters for 0.9.6's head look-at |
| Sonic's quill / Big's head wobble pause while the EV head shows | look | the game's own behaviour in events |
| A hit shows no hurt face while event faces are on | play | the game's event priority; owner question FQ8 |
| EVENT mode's idle uses `rand()`: random outcomes (box contents, ring scatter) differ from a world without event faces | game code | option-gated; face_test F23 shows everything else identical; GAME-mode face animations and lip sync keep rand identical |
| Props are not drawn in prop animations (carrying, set pieces, fishing, the board before 0.9.5); Big's rod in fishing rows | look | the list marks them (`F_NEEDS_PROP`); Big's rod **UNVERIFIED** |
| An arbitrary motion can crash the process | LIB | only the generated list is accepted (E11); `ad_dwalk` is not in it |
| (review R.1) The event queue's own code leaks a finished entry behind an unblended one (48 bytes each, measured) | LIB | the group rule of 4.5 (never an unblended entry behind another), E14 / E19 |
| (review R.4) A gameplay face that already runs hides a host face (the face task's modes 2 / 3) | look | PFaceCancel at a host start (4.2 step 3), F26 |
| (review R.8) 8C04BF00 spins for ever unless jvmode is 1 (a hang, not a fault) | LIB | jvmode read at every start, E18 with a time limit |
| (review R.17) The emote queue moves only on pairs the character is drawn (visibility test against the host camera) | look | the emote pauses off screen; no pair-count limits; README known limit |
| Three G-SA1 agents build g-sa1 at once; the core census changes under the others' warm_test | G-SA1 | the faces agent lands the census and the regenerated file first and together; wait-and-retry; the verifier rebuilds from clean |
| The face trigger LT is also a camera trigger | G-SA1 | while the wheel is open the PadView wrap consumes camera turns (test (5) of 6.5) |
| HUDPaint under `cl_drawhud 0` | G-SA1 | the wheel hides with the HUD (**UNVERIFIED** whether GMod calls HUDPaint then) |
| TAB blocks the scoreboard while a character exists | G-SA1 | owner question FQ1 (H as the alternative) |
| Others do not see faces or emotes (puppets come with 1.0.x); the recorder (2.0) | later | every new LIB call is latched at the pair start (`v1_roadmap.md` D13), so the journal can record them; remote lip sync can come from `ply:VoiceVolume()` locally |
| `objects2_plan.md` may want lanes in 0.9.4a | round size | 4.9's fold rule |
| 0.9.4.1 changes g-sa1 objects files before the round | base | no agent here edits them; base = the released version (2.1) |

---------------------------------------------------------------------------------------------------------------------

## 12. How this changes `gsa1_round_094a.js` (the orchestrator edits the workflow)

1. **PRE:** add to the READ line "SADC/docs/research/faces_wheel_plan.md (sections 0-5, YOUR section in 6, its Review if present)"
   and "rounds plan 14.1a-14.1t"; add a second block `PRE_FW` = the text of 3.1 here (pasted after PRE for the four new agents
   only). The super2 agents keep their PRE unchanged except `094a-lib-anim`, `094a-gsa1` and `094a-verify`, whose prompts get one
   line each: "ALSO faces_wheel_plan.md 6.1" (resp. 6.6, 6.7).
2. **Phase LIB:** `parallel` of five:
   `094a-lib-anim` ("... YOUR TASK: super2_plan.md 6.1 with its review changes AND faces_wheel_plan.md 6.1 (task 1e in your first
   step: land both contracts in one build and report its time; case (19))"), `094a-lib-gold`, `094a-lib-audio` (unchanged),
   `094a-lib-faces` (`${PRE}\n${PRE_FW}\nYOU ARE AGENT "094a-lib-faces". YOUR TASK: faces_wheel_plan.md section 6.2 ...`),
   `094a-lib-emote` (the same with 6.3).
3. **Phase G-SA1:** `parallel` of three, each with all five LIB reports: `094a-gsa1` (its text + "ALSO faces_wheel_plan.md 6.6:
   you run in parallel with the faces / wheel agents; lua_mock.lua stays yours"), `094a-gsa1-faces` (6.4), `094a-gsa1-wheel` (6.5).
4. **Phase Verify:** the one verifier with every report (eight) and "super2_plan.md 6.5 AND faces_wheel_plan.md 6.7".
5. **Report slices:** the `s()` helper gives each G-SA1 agent `api_notes` up to 5,000 characters for `094a-lib-faces` and
   `094a-lib-emote` (their APIs are larger; 3.2 asks them to stay under 4,500); `done` up to 3,500 as now.
6. **meta.description:** "... plus event faces (switch, face animations, microphone lip sync) and the emote wheel with its face
   trigger"; phases unchanged (LIB, G-SA1, Verify).
7. Before launching: dry-run the prompt strings with node (Windows paths need doubled backslashes: `\f`, `\v` become control
   characters), and check that `release_0.9.4` (or `release_0.9.4.1`) is frozen and `super2/base094` copied (`super2_plan.md` 3.3).
8. A verifier fail gets a follow-up fix run outside the nine (a fix agent with the failing agent's files, then a fresh verifier),
   as `super2_plan.md` 0 says.

---------------------------------------------------------------------------------------------------------------------

## 13. Owner-facing summary (plain words; for the status page)

**What you asked for:** event faces you can animate yourself, a switch between the cutscene face and the normal face, lip sync with
your microphone, and an emote wheel like GTA V's (without the slow motion) whose animations you choose per character from a list
of every animation by its internal name - plus a face animation on each wheel slot, played together with the slot's animation when
you press the Left Trigger in the open wheel. All of it comes with 0.9.4a, together with the Super Sonic polish.

**What you will get:**

* A **Faces** page: the event face switch (and a key for it); a maker for your own face animations - double-click the game's
  expressions into a list, give each one "enter" and "hold" frames (60 frames = 1 second), preview, name and save as many as you
  like; ten face hotkeys; lip sync with a live meter.
* **Lip sync:** hold your voice chat key and talk - the mouth follows your voice like Garry's Mod's player models. If Garry's Mod
  gives no voice level for yourself in single player, G-SA1 listens to the microphone itself, only for loudness and only while you
  hold the key.
* The **emote wheel:** hold TAB (or D-pad down), point, let go. Each slot can carry a face animation too: press LT (or left click)
  instead of letting go and both play together; with nothing pointed at, LT plays the wheel's centre face while you keep moving.
* An **Emote wheel** page: every animation of each character by its internal name, double-click into a slot, pick a face animation
  for the slot and for the centre. Each character keeps his own wheel; good starting wheels are filled in.

**Good to know:** everything uses the game's own code - the cutscene faces and the way the game's cutscenes animate characters -
so while your character poses, the game keeps running underneath (gravity, water, getting hit), and moving ends the pose at once.
Big's face can only do 14 of the game's 31 expressions; E-102 and Super Sonic have no cutscene face in the game. Animations made
for props (carrying, climbing, fishing) play without the prop. Others in multiplayer do not see faces and poses yet.

**Things you can change if you prefer** (otherwise I go with the choice in brackets): the wheel key [TAB, or H]; the controller
wheel button [D-pad down, or LB]; the face trigger on keyboard [left mouse button]; the cutscene face all the time [no, only while
it plays]; how "enter" works for talking / blinking expressions [the first change only]; the blend back to neutral [8 frames];
the hurt face while event faces are on [the game's way: none]; G-SA1 listening to the microphone while you hold X [yes]; mouth
style [opens with your voice]; a slower cutscene face speed [off]; a face for Super Sonic [no]; animations in the air [no];
choosing the playing animation again [blend back]; prop animations in the list [shown, marked].

---------------------------------------------------------------------------------------------------------------------

## Appendix A: names - research placeholders and the final names

| Research name | Final (this plan) |
|---|---|
| faces_events.md `sa1_character_face_mode / _face / _face_level`, `sa1_face_info_get`, `sa1_face_info {keys, letters, ladder, ladder_len, steps}` | the same, `sa1_face_info` with `ladder_len` before `ladder`, plus `first[31]`, `last_key[31]`; new `sa1_character_face_sequence` + `sa1_face_entry`, `SA1_FACE_HOLD_LAST`, `SA1_FACE_SEQ_MAX`, `SA1_FACE_HOLD_MAX`, `SA1_FACE_ACT_*` |
| faces_events.md `sa1_state_ex.face_mode / face_shown / face_from / face_to / face_rate / face_letter` | the same plus `face_activity` |
| emote_wheel_lib.md `sa1_character_emote / _emote_stop`, `sa1_emote`, `sa1_emote_item`, flags | the same |
| emote_wheel_lib.md `sa1_action_info` growth (`internal_name`, `label`, `emote_class`, `emote_flags`), `sa1_emote_extra_count / _get`, `SA1_EMOTE_OFFERED`, `_DUP` | **`sa1_emote_count / sa1_emote_get(kind, form, i)` with `sa1_emote_info`** (rows and extras in one list; only offered entries; `dup_of`); `sa1_action_info` unchanged |
| emote_wheel_lib.md `sa1_state_ex.emote_state / emote_item / emote_row / emote_extra / emote_frame` | the same |
| faces_wheel_ui.md `gsa1.FaceInfo / FaceMode(id, on) / Face(id, letters, flags) / FaceStop / FaceLevel / FaceState(id, out)` | `FaceInfo(kind)`, `FaceMode(id, event, pace)`, **`FaceSequence(id, spec, flags)`** (the UI's way), `FaceScript(id, letters, flags)` (tests / console), `FaceStop`, `FaceLevel`, `FaceState(id)` with multiple returns |
| faces_wheel_ui.md `gsa1.EmoteList / Emote(id, action, flags) / EmoteStop / EmoteState(id, out)` | `EmoteList(kind, form)`, `Emote(id, key, flags)` with keys `a<row>` / `x<extra>`, `EmoteStop(id, how)`, `EmoteState(id)` |
| faces_wheel_ui.md slot kinds anim / face / cmd, page 2 = faces, `face_on / face_off / ...` commands, left click / A = play now | slot = body + face (+ loop); the centre's face; `(stop)` the only command; the face trigger replaces "play now" (10.2) |
| faces_wheel_ui.md `gsa1_face_key_<n>`, `gsa1_face_expr_<n>`, `gsa1_face_hold_<n>` | `gsa1_face_hotkey_<n>`, `gsa1_face_hotkey_anim_<n>` (a face animation name; "keep the last face" is the animation's own) |
| faces_wheel_ui.md `gsa1_wheel_all` (hide special rows) | dropped: every offered animation is listed, marked (FQ15); a page-local group filter |
| (new) | `gsa1_wheel_face_key`, `gsa1_wheel_face_pad`, `gsa1_wheel_stick_keep`, `gsa1_wheel_air`, `gsa1_face_pace`, `gsa1_face_summary` |

## Appendix B: evidence this plan read beyond the research (read only, 2026-10-26 evening)

* LIB: `libsa1.h` sections and `sa1_state_ex` (`:876-934`), `sa1_action_info` (`:334-348`); `src/world/sa1_world.h` `sa1g_world`
  (`:433-590`: hook fields `hostobj_pre`, `charopt_pre`, `hud_fn`, `set_pre` / `set_post`); `src/world/sa1_world_tick.c`
  `sa1g_step_game` (`:179-215`); `src/sa1_api_world.h` `sa1w_pstate` (`:93-126`), `sa1_character_world` (`:296`);
  `src/world/sa1_api_world.c` `sa1w_world_pstate` (`:671`); `src/sa1_character.c` `state_of` (`:742`), `ensure_world` (`:368`,
  static); `src/sa1_charopt.c` (the asset-layer bridge); `src/world/sa1_world_set.c` `st_state_of` (`:248-261`),
  `sa1g_set_attach` (`:1198`); `src/world/sa1_world_image.c` `sa1g_world_apply_image` (`:627`: the segment reset, hooks kept);
  `src/sa1_manifest.c` `k_super_subst` (`:88`) and `sa1_manifest_char`; `build.py:430-431` (sources by glob); no `tools/` folder.
* SADC: `src/1st_read/code_8C04BC70.c` (8C04BC70 / 8C04BCC8 / 8C04BD60), `code_8C04B7BA.c` (8C04BBCE / 8C04BC20).
* g-sa1: `src/module/gsa1_feature.hpp`, `bind_super.cpp` (`session()->get(id)`), `src/core/session.hpp` (`Character`),
  `addon/lua/gsa1/cl_pad.lua:123` (LT = 0x10000), `feat/sh_super.lua:219-224` (`S.PAD_BUTTONS`), `feat/cl_super.lua:296-312`,
  `cl_warm_census.lua`, `sh_config.lua:16-25`, page orders (250 interact / objects, 260 HUD, 270 Super Sonic, 300 Surfaces).
* `sadc/tools/workflows/gsa1_round_094a.js` (5 agents, 3 phases), `gsa1_fix_0941.js`, `HANDOFF.md` 2026-10-26 entries.

---------------------------------------------------------------------------------------------------------------------

## Review (adversarial)

Reviewer: workflow step "faces-review" of the prep run (`gsa1_prep_faces_wheel.js`), 2026-10-26 evening; it wrote none of the
plan or the research. It wins where it disagrees with the text above; its fixes are already applied in place, each marked
"(review R.n)". Read: this plan in full; `faces_events.md`, `emote_wheel_lib.md`, `faces_wheel_ui.md` in full; the seven TSVs
(checked by script); `super2_plan.md` 5-6, 10.1-10.3 and its Review; rounds plan 2.1 and 14.1s / 14.1t. Code read (read only):
SADC `code_8C04BC70.c`, `code_8C04B7BA.c`, `code_8C07B080.c` (the face task 8C07B106), `code_8C04B440.c` (8C04B48C / 8C04B4D2 /
8C04B528 / 8C04B57C), `code_8C04BF00.c`, `code_8C06C300.c`, `code_8C07A55C.c` (jvmode), `code_8C06CCA0.c` (GetAnalog),
`code_8C0DE2A0.c` / `code_8C0DA106.c` (Amy's hammer), `code_8C098998.c` (MAlloc); LIB `include/libsa1.h`, `src/world/sa1_world.h`,
`sa1_world_tick.c`, `sa1_world_image.c` (apply_image, reset_run), `sa1_world.c` (ext alloc), `sa1_world_set.c` (heap guard),
`src/sa1_api_world.h`, `src/world/sa1_api_world.c`, `src/sa1_character.c` (ensure_world, state_of, sa1_action_info_get, the
private sa1_char), `src/sa1_loader.c` (sa1_load_disc, build_character), `src/sa1_internal.h`, `build.py` (globs, flags, link
test), `CMakeLists.txt` (SA1_NO_WORLD); g-sa1 `cl_character.lua`, `cl_pad.lua`, `sh_config.lua`, `feat/cl_super.lua`,
`feat/sh_super.lua`, `feat/cl_interact.lua`, `feat/cl_tools.lua`, `autorun/gsa1_autorun.lua`, `gsa1_feature.hpp`, `session.hpp`
/ `.cpp`, `bass.cpp`, `tests/core_test.cpp` (the census), `tests/feat_super_test.lua` and `warm_test.lua` (census users),
`tools/lua_check.py`, `build.py`. Nothing in LIB, g-sa1, SADC/src, config, include or harness was edited or built.

Probe: `SADC/build/research/faces094a/review/probe_leak.c` (with `build_probe.py` copied from `../`), built and run against a
COPY of the 0.9.4 snapshot in `review/lib/` (libsa1.a sha1 7203ffbe3330b535fa525af0ea8b6f71aaf9d0ac, libsa1.h
cf501e01dbeb4c5b8b8a03140b274c6370a8143c, copied 2026-10-26 19:56 from `faces094a/lib/`) at IDLE priority on the owner's GDI;
output `review/out/probe_leak.txt`. Numbers only.

### R.0 Verified (holds)

* **The face code.** EV_SetFace's body 8C04BBCE stores the script and starts a stopped block with time 1, count 0, keys 0
  (`code_8C04B7BA.c:260-273`); the reader 8C04BCC8 and 8C04BC20 / 8C04BC70 behave as 4.2 uses them (`code_8C04BC70.c:55-112`);
  the face task 8C07B106 advances the block every step and shows it from its modes 1 / 4 (`code_8C07B080.c:145-239`); the face
  block layout and PL_FACE are as `faces_events.md` 2.3 says. The interrupt and its nearer-key rule are sound against that
  reader (count = time - 1 ends the step at the next face-task call on face_next, which the rule may set to the start key),
  as are the held step {127, k} with its re-arm and the lip-sync parking.
* **Face counts.** Big's mask 0x01FC0C1F = A-E, K, L, S-Y (every key < 8), E-102 without a face task, Super Sonic without an
  EV head, F13's numbers (steps[2] 48, first[2] 5, last_key[25] 8, steps[30] 182) all follow `faces_events.md` 4.2's table.
* **No stale guest pointer after a rebuild.** `sa1g_world_apply_image` zeroes the whole arena (the face block and the queue go
  with it) and resets segment 0x18 (`sa1_world_image.c:627-686`); the hooks and `persist[]` survive (`reset_run` keeps them).
  Segment 0x18 is 16 MB (`sa1_world.h:81`); the face table write is per world (each world has its own arena).
* **The hooks' place.** `sa1g_step_game` runs the pad update, hostobj_pre, set_pre, then the executor
  (`sa1_world_tick.c:179-215`): face.pre / emote.pre after set_pre see this step's converted pad; GetAnalog takes NULL
  outputs (`code_8C06CCA0.c:53-80`).
* **Ownership and header.** No file of the addendum is in `super2_plan.md` 5.2's lists except those given to 094a-lib-anim
  (its own) and `sa1_world_tick.c` (nobody's before); the G-SA1 agents share no file; `core_test.cpp`, `cl_warm_census.lua` and
  `cl_character.lua` have no other editor this round. No SA1_FACE_* / SA1_EMOTE_* / sa1_face* / sa1_emote* name exists in
  `libsa1.h` today; the additions are struct tails and two new sections.
* **Defaults.** Every body of 8.3 is an offered entry (`wheel = yes`) with the stated internal name; the Super defaults are
  offered in the kind8 / general columns; Big's wheel uses only faces in his mask (Talk, Blink, Sleepy). Script over the TSVs.
* **G-SA1 facts.** LT = raw 0x10000 and "camera left" by default (`cl_pad.lua:123`, `sh_config.lua:115-141`); the weapon slot
  binds 0-10 and lastinv are swallowed while a character exists (`cl_character.lua:971-986`); +attack / +attack2 / +duck are the
  SA1 action button (`cl_character.lua:729`), so the left-mouse face trigger is masked by the chained FilterButtons;
  `cl_super.lua`'s filter chains the default and `cl_wheel.lua` loads after it (sh_ then cl_ files, each alphabetical:
  `gsa1_autorun.lua:7-56`); Session keeps the whole `sa1_state_ex` sized by the compiled header (`session.cpp:507-511`); every
  `src/module/*.cpp` and `tests/feat_*` file is globbed (`build.py:62-77`); `S.coreSonic` copies the core census at load and
  `feat_super_test.lua` / `warm_test.lua` count at run time, so the regenerated census breaks none of 094a-gsa1's tests.
* **Q menu and assets.** Every ConVar of section 7 has a control; the page orders 280 / 285 are free. The generated table holds
  names, labels, classes, flags and addresses (and, by R.2, the extras' frame counts: numbers); no face table, step, key or
  motion data anywhere.

### R.1-R.19 Findings (severity, evidence, fix; every fix is applied in the text above)

**R.1 MEDIUM - the event queue leaks the game heap at every unblended transition, although every start clears.** 8C04B57C's
else branch ends an entry with `ewp->buf44 = node` without freeing the buf44 it held; only the link branch frees it
(`code_8C04B440.c:175`, `:203`). The plan's own link rule (0 across objects), the trailing stand of another object (Tails'
m_atack_1 on the flight model is a default slot), CHAIN across objects and a host link 0 all make such transitions. Measured
(`review/out/probe_leak.txt`, Sonic, 10 runs each, every run started by 8C04B48C): one item 0 bytes; row 3 then the stand with
link 8: 0; with link 0: -480 bytes (48 per run); the same with a clear at each end: -480; 82 then 83 with the rule's link 0:
-480. E14 ("no buf44 leak: every start clears") would have failed, and a session loses 48 bytes of the game heap (about 384 KB
free in a fresh Sonic world) per such emote, for good. Fix: 4.5 "never an unblended entry behind another" (groups; clear and
queue the next group in the post hook; clear at the natural end), 5.3's emote comment, 6.3 task 3, E14 extended, E19, 11.

**R.2 MEDIUM - "from the loaded disc without a world" cannot be written as specified.** `sa1_load_disc` frees every image it
read (`sa1_loader.c:601-602`) and `build_character` parses only the action table's motions, the textures and the body
(`sa1_loader.c:455-520`); `sa1_action_info_get` reads that parsed set, not 1ST_READ. So neither `sa1_face_info_get` (the face
table, the EV heads' shape motions) nor an extra's nbFrame in `sa1_emote_get` has data to read without a world. Fix:
`sa1_face_info_get` reads 1ST_READ.BIN (Tikal: TIKAL_PROG.PRS) from the disc of the last `sa1_load_disc` on each call (a
non-const ctx in 5.3: `-Wcast-qual` is on for src/*.c), G-SA1 keeps it per kind, the world side computes its letter mask from
its own arena; the extras' frame counts go into the generated table as numbers (the owner rules allow generated tables of
numbers) and E12 checks them against the disc in a world. Applied: 4.5, 5.3 (two comments, the signature), 5.6, 6.2 task 1,
6.3 task 1 and E12, 6.4 task 3.

**R.3 MEDIUM - a LOOP script never loops in GAME mode, and the TALK lip-sync style stops after one pass.** 4.2 appends '0' to
every GAME-mode script and re-arms a LOOP "when its reader took the last letter (face_script NULL or at its 0)". The reader
takes a letter only at a sequence end: after the last letter `face_script` points at the appended '0', which is read at that
letter's end (8C04BC20 -> -1, 8C04BC70 -> 8C04BD60: `code_8C04B7BA.c:289-290`, `code_8C04BC70.c:101`) - the re-arm condition
is never true in time. Fix: no '0' with LOOP, '0' refused in a LOOP script, the script-end detector stated (4.2, 5.3, 6.2
task 2), F5 in both modes, F27.

**R.4 MEDIUM - 4.4's priority is not what the game's face task gives.** In GAME mode a host face that starts while a gameplay
face runs (`W.seq` set: modes 2 / 3) is not shown: the task keeps showing the gameplay face to its end while 8C04BCC8 advances
the host's block unseen (`code_8C07B080.c:176-210`; it looks at `face_time` only in mode 1). A face animation pressed half a
second after a hit loses its first half; lip sync started during 'I' is mute for up to 32 steps. Fix: PFaceCancel (the game's
own 8C07B2DC) at a host START over a running gameplay face (4.2 step 3, 4.4, 5.3, 6.2 task 2), F26, a risk row.

**R.5 MEDIUM - `face_shown` = "parent->sibling == object" says 1 while a General Sonic is Super.** The face task keeps linking
Sonic's EV head into his normal tree, which is not drawn then (`faces_events.md` 4.4, measured: the script advances, 0 EV
draws). F10 asks for 0 there, so the fill as defined failed its own test; the same for an emote that draws another object
(ball, curled body, flight model) while the EV head sits in the body tree. Fix: the fill also requires the drawn tree (not
`w->sg.look`; the queue head is the main body): 4.2, 5.3, F10.

**R.6 MEDIUM - "no call = unchanged" protects the reference worlds, not the owner's.** G-SA1 as specified calls FaceMode for
every new character and FaceLevel(-1) every frame, so every G-SA1 world runs the face lane. Read per pair, 4.2's upkeep line
"GAME mode and NONE: 8C07B2DC(0)" then cancels every hurt face of every character (Sonic, Tails, Knuckles and Amy show 'I'
today), and no test covered a world with calls but no activity. Fix: PFaceCancel only on the transition to NONE (4.2), the
header sentence (5.3), F25 (calls only = the game, pair for pair), the level as a state in LIB, and G-SA1 sends FaceMode for a
new character only while the switch is on and one -1 when talking ends (6.4 task 3 and tests (2), (7)).

**R.7 LOW - an emote in a heap-starved world faults it.** `MAlloc` returns NULL when no block fits (`code_8C098998.c:99-158`)
and 8C04B4D2 / 8C04B528 write through CAlloc's result unchecked (`code_8C04B440.c:83-123`); 0.9.4's rings and item boxes
already needed a heap guard (`sa1_world_set.c:969-1007`). Fix: a headroom check before queueing, SA1_ERR_LIMIT "the game's
memory is full" (4.5, 5.3, 6.3), E17.

**R.8 LOW - jvmode is guarded only before the world's first pair.** 8C04BF00 spins for ever unless jvmode is 1
(`code_8C04BF00.c:12-13`, the `for (;;)` at `:227`) - a hang of Garry's Mod's thread that no fault handler catches - and a
re-created player task is at 0 again (`code_8C06C300.c:77`), 2 when freed (`:172`). Fix: jvmode read at every start and group
start; a new player task drops the emote and the face lane's activity (4.2 step 1, 4.5, 6.3 task 3), E18 with a time limit.

**R.9 LOW - internal names are not unique, and the plan folded and matched by name.** Knuckles' `k_panch0` is two different
motions (row 39: K_MOT 0CC436CC, 75 frames, hold; row 54: 1ST_READ 8C3CAD04, 32 frames, once); `s_kimepose0` is rows 75 and 139,
`ss_move2` rows 133 and 134 (TSVs). "One row per internal name" hid a real animation from the owner's list, and "names matched
first" reloads a slot as the wrong row. Fix: fold by `dup_of` (drawn object and motion), match by key while the key's name
still matches, the name only as the fallback (4.5, 5.3, 6.3 task 1 / E12, 6.5 task 2 and tests (11) / (12), 8.1).

**R.10 LOW - SA1_FACE_SEQ_MAX is 32 and G-SA1 appends {A, back, 0}:** a 32-entry face animation with "back" (default 8) is a
33-entry call, refused as a whole - the owner's longest animations would silently never play. Fix: 31 own entries when back is
appended (4.3, 6.4 task 3 / test (4), 8.2).

**R.11 LOW - 'A' in a SCRIPT is not the neutral face:** 8C04BC70 treats n == 0 as the idle and calls rand()
(`code_8C04BC70.c:63-84`); 5.3 promised the 31 animations and F23 rand identity for GAME-mode scripts. Fix: documented in 5.3;
F23's scripts contain no 'A'.

**R.12 LOW - contract text.** "The eleven fields" are twelve (seven face, five emote); `sa1w_world_pstate` returns early twice
(`sa1_api_world.c:678`, `:685`), so "the none values, then the fills as its last statements" leaves -1 fields at 0 on the
no-player path unless the function is restructured; `ensure_world` returns SA1_PARTIAL with a valid world (built without land),
which "*out NULL on any error" would have turned into a failure. Fix: 5.4, 6.1 (e) / (f).

**R.13 LOW - layering of the new LIB files.** src/*.c compile with `-std=c99 -Iinclude -Isrc` only (`build.py:430-440`; MSVC
through CMake for SA1_NO_WORLD); `world/sa1_world.h` includes SADC's `sa1/guest.h`, which is not on that path, and the sa1_char
record is private to `sa1_character.c` (`:271`). So `sa1_face.c` / `sa1_emote.c` reach the world only through their own headers'
functions and take the kind from the world after `sa1w_character_world_ensure`. Fix: 6.2 task 1, 6.3 task 2 (order: syntax,
ensure, the kind's mask, the latch).

**R.14 LOW - E13's `#if defined(SA1_API_FACE)` is true from 094a-lib-anim's step 1**, long before 094a-lib-faces lands
`sa1_face.c`: emote_test would reference `sa1_character_face_sequence` and fail to link in 094a-lib-emote's own gate. Fix:
build_emote.py enables E13 only when the snapshot's archive defines the function (6.3 E13).

**R.15 LOW - IN_AIR "cancelled only by input and hurt"** let a form change (the transformation, a revert) or a picked-up prop
keep playing the old form's objects or an empty-handed pose. Fix: IN_AIR drops only the standing-state rule (4.5, 5.3).

**R.16 LOW - no refusal reason for a character on the ground but not in his standing mode** ("moving"), which with
`gsa1_wheel_move` 1 (the default) is the usual case at the release. Fix: "moving" / "busy" (4.5, 5.3, 6.3 task 2), the toast's
hint and feat_emote_test (6.5).

**R.17 LOW - the queue moves only on pairs the character is drawn.** 8C04B57C is inside the display, which skips on a failed
visibility test, and live worlds test against the host camera (`sa1_world_tick.c` `sa1g_camera_view`): an emote off screen
pauses; E2 / E4 / E6's pair counts need a camera that sees him. Fix: no pair-count limits in the lane, the test note, a README
known limit, a risk row (4.5, 6.3, 6.8, 11).

**R.18 LOW - G-SA1 input leaks and loose ends.** The interact / lift keys still press the game's action button through
`GSA1.ExtraButtons` while the wheel is open (added after FilterButtons: `cl_character.lua:796-811`); a PadView wrap that skips
the original saves the pending pad turn for a jump at close (`cl_pad.lua` keeps `pend_yaw` / `pend_pitch` / `reset_to` in
locals: `:61-117`); with `gsa1_pad_dpad` 1 a D-pad wheel button walks the character while he chooses (`sh_config.lua:196`);
EmoteState's row follows CHAIN, so "the playing one again" must use the key G-SA1 started; a C++ core test cannot read
W.DEFAULTS from Lua. Fix: 6.5 tasks 2 / 3 and tests (5), (6).

**R.19 LOW (advisory).** The catalogue's descriptions should be G-SA1's own words, not the sadocs wiki's text in the addon; the
owner asked for "all the animations" while the list holds only playable ones (FQ19 added); the mic binding test's "-20 dBFS" was
ambiguous (RMS: level 0.80; peak: 0.74); F16 also passed with both calls doing nothing. Fix: 6.4 task 3 and its binding test,
10 (FQ19), 6.2 F16.

### R.20 Required changes (the verifier checks these; already applied above)

R.1 (4.5, 5.3, 6.3 task 3, E14, E19), R.2 (4.5, 5.3, 5.6, 6.2 task 1, 6.3 task 1 / E12, 6.4 task 3), R.3 (4.2, 5.3, 6.2 task 2,
F5, F27), R.4 (4.2, 4.4, 5.3, 6.2 task 2, F26), R.5 (4.2, 5.3, F10), R.6 (4.2, 5.3, 6.2 tasks 1 / 2, F25, 6.4 task 3 and its tests
(2) / (7)), R.7 (4.5, 5.3, 6.3, E17), R.8 (4.2, 4.5, 6.3, E18), R.9 (4.5, 5.3, 6.3, 6.5, 8.1), R.10 (4.3, 6.4, 8.2), R.12 (5.4,
6.1). Advisory, applied too: R.11, R.13-R.19.

### R.21 Checked and not a finding

* "(no label) E_MOT_0CC3AE68" (E-102 row 84) and "(no label) B_MOT_0CC99638" (Big row 100) are marked placeholders, not invented
  names; every other offered row and extra has its SA Tools file name, and no extra shares a name with a row.
* Amy's hammer setter 8C0DE46C writes playerwk free.b[5] / f[6] / f[7], which only her display reads
  (`code_8C0DA106.c:108-131`; the mode machine only writes them): no gameplay effect; after (0, 0.0) the hammer shrinks over 10
  displays, the game's own look.
* The face lane's buffers live in segment 0x18, not in the game heap; an emote entry is 0x18 bytes plus the allocator's header
  (48 bytes per leaked entry, measured).
* A GAME-mode SEQUENCE from a stopped block: 8C04BBCE("0") then face_seq = the buffer - the first face-task call reads the
  buffer's first step (enter); EVENT's terminator "" idles. The pace copies (times <= 60 doubled to <= 120 in a Sint8) stay valid
  while the reader may still point into them. The step-buffer bound (1,536) is ample; the builder should still refuse rather
  than overflow when the data would exceed it.
* The census face segment: kinds 0, 2, 3, 5, 7 are the face-capable ones; Tails' flight head is the same EV head
  (`out_static.txt`); the census pipeline (core_test -> gen_warm_census.py -> warm_test.lua) is as 6.4 task 1 uses it.
* TAB, D-pad down, LT and the left mouse button collide with no G-SA1 default (Super: G / RS click; interact: E; note key: none);
  keys 1-8 are already swallowed while a character exists.
* The super2 contract and this one land in one step by one editor in different parts of the header; `sa1w_pstate` gets both
  tails; `sa1_state_ex` gets only this plan's.

### R.22 Verdict

**GO** with the fixes applied. No HIGH finding: every address, count and mechanism the plan builds on holds against the code,
nothing needs decompiling, the reference worlds stay unchanged by construction, and the ownership has no collision with
`super2_plan.md`'s agents. The six MEDIUM findings would each have cost an agent loop or put a visible defect in front of the
owner: R.1 a heap leak per emote that E14 would only have found after the fact (measured), R.2 an API that could not be written
as specified, R.3 a broken loop and TALK style, R.4 / R.5 a priority and a state field the code does not give, R.6 lost hurt
faces in every G-SA1 world.
