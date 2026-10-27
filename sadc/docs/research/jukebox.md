# G-SA1 Jukebox: the game's music from the owner's disc (round 6c research)

Research and design for owner request 4c of `HANDOFF.md` (a jukebox in Garry's Mod that plays Sonic Adventure's music
from the owner's own disc; track list, play/stop, volume; Options tab). Read-only step: nothing in libsa1 (LIB), G-SA1
or the SADC sources was changed. Evidence was produced by scratch tools in `sadc/build/research/jukebox/` (appendix A);
every claim cites an address, a file:line or a measurement. Anything not checked is marked **UNVERIFIED**.

File state read: `g-sa1` as of 2026-10-21 03:30 (addon `GSA1.VERSION = "0.5.0"`, module `GSA1_API_VERSION 5`; round 4b
is editing g-sa1 and LIB concurrently, so line numbers below can drift), LIB `src/sa1_audio*.c` of 2026-10-20 14:42,
`libsa1.a` of 2026-10-20 14:59 (copied under LIB's build lock for the probes).

## 0. Summary and recommendation

* **The game has its own sound test with titles.** `ADVERTISE.PRS` (the menus) holds 104 records `{char *title; s32
  music;}` at `0x8C90F5CC` in six pages (page table `0x8C90F90C`), e.g. "Open Your Heart" -> music 66 `MAINTHEM.ADX`,
  "Emerald Coast:  Type 1" -> 28 `ECOAST1.ADX`. So no soundtrack-title table has to be made: the titles are ripped from
  the disc at run time like everything else. 5 files are not in the sound test (`NIGHTS_A`, `NIGHTS_S`, `SONIC_CD`,
  `SSRACING`, `TITLE2`); music 69 `NIGHTS_K.ADX` is named in the game's table but **not on the disc**.
* **The music:** 110 music numbers (name table `0x8C5FB8A8`), 109 ADX files on the disc: CRI ADX v3, stereo, 63 at
  44.1 kHz and 46 at 32 kHz, 79 with a loop, 30 without, 432.5 MB, 2 h 50 min in total (section 1, full table 1.6).
* **G-SA1 today** mixes all game sound itself (libsa1's mixer) on the Lua main thread into one push stream on Garry's
  Mod's own `bass.dll`, keeping only 60-200 ms queued; music cues are global (one BGM slot, one jingle slot, not per
  character) and background music is off by default (section 2).
* **Recommended design (section 6):** a jukebox engine in the G-SA1 module with **its own music-only libsa1 audio object
  on its own worker thread** (measured: +1.7 MB, 17 ms to open with the OS cache warm), which reads and decodes the ADX
  files off the Lua thread and feeds **one BASS push stream per playing jukebox at the track's own sample rate**, with
  1.5 s queued: music keeps playing through Lua-thread hitches (the owner's freezes) and BASS resamples. Volume and
  3D (distance curve of the game + pan) are BASS channel attributes set every frame from the Lua thread. One small,
  additive LIB API (`sa1_audio_music_info`: the sound-test titles and pages and the ADX header data without reading
  whole files) gives the track list. On top: a **Q menu > Options > G-SA1 > Jukebox** page (personal, only you hear
  it) and a spawnable **world jukebox entity** whose state lives on the server (music number, start time, mode) and
  that every client with G-SA1 and a disc plays from its own disc, positioned and synchronised by `CurTime()`.
* Rejected: `sound.PlayFile` (cannot read the disc, BASS has no ADX decoder, writing WAVs puts game audio on disk),
  `sound.Generate` (no 32 kHz, whole PCM in a Lua string), host MUSIC cues into the game mixer (no seek / state / 3D,
  collides with the game's own BGM and jingles, Lua-thread fed), a BASS callback into libsa1 (thread contract, unload).

## 1. The music on the disc

### 1.1 Name table and files

* `1ST_READ.BIN` `0x8C5FB8A8`: `const char *name[110]` ("ADVAMY.ADX" at `0x8C15D58C` ... "WNDYVLY3.ADX" at
  `0x8C15DBA8`); the word after it, `0x8C5FBA60`, is the music-enabled flag that 8C107AC6 tests, so the table has
  exactly 110 entries. LIB reads it the same way (`libsa1/src/sa1_audio.c:22`, loop at `:431-455`), and
  `sa1_audio_music_count` = 110 (probe).
* `SONICADV/` holds 109 `*.ADX` music files (plus the two voice AFS archives); every name in the table but one is on
  the disc, and no music file on the disc is missing from the table (`music_survey.log`, read straight from the .gdi).
  **69 `NIGHTS_K.ADX` is absent**: `sa1_audio_stream_open` returns "not found"; a mixer MUSIC cue for it returns OK,
  fades out the current music and plays nothing (probe, section 4).
* Every file: CRI ADX header version 3, encoding 3, 18-byte blocks, 4 bits, 2 channels, `(c)CRI` signature; no
  version-4 headers, so LIB's v3 loop parsing (`sa1_audio_codec.c` `sa1a_adx_parse`) applies to all. Rates: 63 files
  44100 Hz, 46 files 32000 Hz. Loop (header loop flag): 79 files; 30 without. Sizes 156,653 B (FISHMISS) to
  15,405,246 B (AMY.ADX); 432.5 MB together; 10,212 s of audio (probe decode total). All 109 are distinct (SHA-1).
* **`SSRACING.ADX` is truncated on the disc**: the header says 6,731,879 frames (3:30.4) but the 6,336,512-byte file
  holds 5,632,416 frames (2:56.0); LIB clamps to the data present (`sa1a_adx_parse`, "clamp the sample count") and
  the probe decodes exactly 5,632,416 frames.
* Loops: intro up to 54.1 s (TICAL.ADX), loop end at or within 0.23 s of the end of the file (32 kHz files keep a short
  tail after the loop end that a looping play never reaches). LIB's decoder wraps from `loop_end` to `loop_start` and
  restores the predictor history saved at the loop-start block (`sa1_audio_codec.c` `adx_decode_block` / `seek`), so
  the seam is the encoder's intended continuation; the probe read 3,000 frames across `loop_end` with `loop = 1` for
  all 79 looping files and none stopped. A backward seek re-decodes from block 0 (0.128 ms per second of audio:
  worst case about 43 ms for a 5:36 file).

### 1.2 How the game plays music (1ST_READ `8C107800-8C107E40`, Ghidra pseudo-C in `sadc/build/ghidra/1ST_READ/decomp`)

* The music task (init 8C107864, exec 8C10792A, created by 8C107D80 through 8C09852E) owns two ADXT players: music
  `DAT_8C5FBA70` = 8C64413C(2 channels, work, 0x2D0C4) (ADXT_Create; UNVERIFIED name) and voice `DAT_8C5FBA74` (1
  channel). Their output levels are set with 8C6445EA(handle, -10) for music and (handle, 0) for voice; 8C6445EA
  stores the value at handle+0x40 and looks like ADXT_SetOutVol (0.1 dB units, so music at -1.0 dB) - **UNVERIFIED**
  (Crazy Taxi's ADXT_SetOutVol is 0x18 bytes, this one 0x12; `sadc/reference/katana_sdk_names.csv`).
* State words: `DAT_8C5FBAA8` = requested music (-1 none), `DAT_8C5FBAB0` = saved music, `DAT_8C5FBAB8` = a stage's base
  music, `DAT_8C5FBAAC` = the music ADXT status (8C644500). Every frame 8C10792A starts a pending request with
  `ADXT_StartFname(name[n])` (8C644968, named in `sadc/build/1ST_READ.symbols.txt`).
* Entry points: 8C107AC6(n) request + save; 8C107DAA(n) request + save + base; 8C107C80(n) **jingle**: request only,
  plus a task 8C107CC2 that, when the music status is 0 or 5 (stopped / play end) and the saved track has not changed,
  requests the saved track again - i.e. the background music **restarts from its beginning** after a jingle;
  8C107B26 request the saved track; 8C107DC2 request the base track; 8C107ADA stop (8C644468, which ADXT_StartFname and ADXT_StartAfs 8C644904 also call before
  they start: ADXT_Stop, name UNVERIFIED) and forget the saved track; 8C04EF52(n) request five frames later (task 8C04EED2).
* No loop code in 8C107xxx: a track with a header loop loops for as long as nothing else is requested (ADXT plays the
  header loop; this is also why the sound test can simply call 8C107AC6, section 1.4).
* Pause: 8C0946BE silences every SE slot and both ADXT players (8C107BF2: -960 on both); 8C094720 restores (-10 music,
  0 voice); both are called from 8C04CA40 and 8C04DA20 (which also play CONTINUE as a jingle).

### 1.3 Who requests which music (every call site in the generated asm)

`music_calls.py` scans all `sadc/asm/**.src` (1ST_READ and the 248 overlays) for JSR / JMP through a literal-pool
address of the music functions and resolves R4 when it is a constant: 397 call sites (233 request+save, 20
request+base, 14 jingle, 1 delayed, 4 restore, 45 stop, 80 music-system init). Constant jingle arguments (8C107C80):

| music | file | where (function) |
|---|---|---|
| 26 | CONTINUE.ADX | 8C04CA40, 8C04DA20 (with the pause mute), MINICART 0C90A81E, STG00 0C903140 |
| 44 / 46 | FISHGET / FISHMISS | 8C0E6446 / 8C0E8562 (Big's fishing) |
| 45 | FISHING.ADX (loops) | 8C0EB244 (fish hooked) |
| 55 | INVNCIBL.ADX | 8C07D4A0 (tail call) |
| 56 | ITEM1.ADX | ADV00 0C900E70, 0C906E80, 0C90D480 (x2) |
| 85 | SPEEDUP.ADX | 8C07D2DA |
| 96 | TIMER.ADX | 8C07BE6E (drowning countdown: stops the music with 8C107ADA first, later 8C107B26 through 8C07BE68) |

Other constant requests: field music in 8C04935C (stage 26 Station Square: 76; stages 29-32 Egg Carrier: 31; stage 33
Mystic Ruins: 67 in acts 0-2, 42 FINALEG1 in act 3), RNDCLEAR 75 (8C08C100; delayed 8C0BFAEE, 8C04EFE0, 8C04F20A), CHARACTR 24 and TICAL 95 in the title/menu
code 8C09A420, event overlays `EV00xx` (EGCARER1 31 from 30 of them, TICAL 95 from 19, ...), bosses and stages through
`set_base` (STG10 42/43, STG12 79/80, SHOOTING 78). Stage and act music comes from per-overlay byte tables (`MOV.B
@R14,R4` sites in STG00-STG12, B_CHAOS*, ADV0x, MINICART): not resolved, not needed for a jukebox.
**The 1-up is not `ONE_UP.ADX`**: 8C08A120 plays tone 0x2C0 (704) through 8C0940DA; ONE_UP.ADX (music 71) has no
constant call site anywhere.

### 1.4 The game's own sound test (ADVERTISE)

SA1 does have an in-game sound test (the question was open in the request):

* `SONICADV/ADVERTISE.PRS` (176,538 B; decompressed 937,008 B, SHA-1 `5abb7a8b0d85e90b4ee9ee6815a5ad10ca5a81e7`, load
  address `0x8C900000`, `sadc/baserom/overlays.json`) is the menu overlay (title, file select, options, trial, ...).
  Its texture list names `ava_sndtest` (`0x8C90F388`) and `ava_sndtest_e` (`0x8C90F494`).
* Records `{const char *title; s32 music;}`, 104 of them, at `0x8C90F5CC-0x8C90F90C` (`asm/overlays/advertise/data/
  8C90EAF0.src:866-898`), and a page table at `0x8C90F90C`: six `{record *, count}` pairs = (0x8C90F5CC, 11),
  (0x8C90F624, 40), (0x8C90F764, 11), (0x8C90F7BC, 12), (0x8C90F81C, 10), (0x8C90F86C, 20). The page table is read by
  the menu code 8C907B80 (two literal-pool references, `asm/overlays/advertise/text/8C907B80.src:631, 1260`).
* Each music number appears at most once: 104 different numbers; with the 5 unlisted files and the missing 69 that is
  all 110.
* Titles are English ASCII (one table for both menu languages) with two spaces after a colon, exactly as in the game:
  "SONIC:  It Doesn't Matter", "Emerald Coast:  BIG:Fishin'", "ENEMY:  Perfect CHAOS 2nd.", "Jingle#A", ...
* Playback: the menus request music through 8C9019D2, whose literal pool holds 8C107D80 (music system init),
  8C09852E (task create) and task 8C9018B0; 8C9018B0 calls 8C107AC6(work[0]) after a short delay
  (`8C901820.src:120-149`): the ordinary background-music path, so a sound-test track loops at its ADX loop exactly
  as in the stages. That the sound-test page code reaches 8C9019D2 with the record's music number is **UNVERIFIED**
  (not traced instruction by instruction); the sound test was not run in the harness, and how the player reaches it
  in the retail menus (Options?) is **UNVERIFIED**.
* The page captions are in the `ava_sndtest` textures (not read). Our names for them (used in section 6.5):

| page | entries | records | contents |
|---|---|---|---|
| 1 | 0-10 | 11 | vocal songs and character themes ("Open Your Heart", "SONIC:  It Doesn't Matter", ..., Theme of "CHAO") |
| 2 | 11-50 | 40 | action stages (two or three tracks each) and bosses ("ENEMY:  ...") |
| 3 | 51-61 | 11 | adventure fields and mini-games (Station Square ... "Mini-Game:  CHAO Race - Goal") |
| 4 | 62-73 | 12 | event music ("Sadness", "Strain", ...) and character appearances ("Appearance:  SONIC", ...) |
| 5 | 74-83 | 10 | menus and tutorials ("Title", "Options+VM", "Choose Your Buddy!", "Trial", "Tutorial:  ...") |
| 6 | 84-103 | 20 | jingles and short pieces ("AMY:  Hurry Up!", "BIG:  Hit!", "Speed Up", ..., "Jingle#A"-"Jingle#E") |

* A second, developer sound test for sound effects is in 1ST_READ: 8C094E80 prints "SOUND TEST:B[  ] N[  ]"
  (`0x8C159D84`) and 8C0957CE plays bank / number (bank 15 = music through 8C107AC6, above 15 voices, else MIDI
  programs). It has no titles.

### 1.5 Tracks that need special handling in a jukebox

| music | file | issue | jukebox treatment |
|---|---|---|---|
| 69 | NIGHTS_K.ADX | named, not on the disc | never listed |
| 87 | SSRACING.ADX | truncated (2:56 of a 3:30 header), not in the sound test | listed as "not in the sound test"; length = the data present |
| 68, 70, 84, 102 | NIGHTS_A, NIGHTS_S, SONIC_CD, TITLE2 | not in the sound test; no constant call site found | a separate group, titled by file name (official titles: owner question 1) |
| 30 files | e.g. vocal songs, TITLE, title cards, jingles | no loop | "repeat" restarts them from 0; list modes go on after one play |
| 15 files | non-looping and under 25 s (CHAOGOAL, EVTBGM05, FISHGET, FISHMISS, INVNCIBL, ITEM1, ONE_UP, RNDCLEAR, SPEEDUP, TIMER, TITL_EGG, TITL_MR1, TITL_MR2, TITL_SS, TITLE2) | jingles / title cards | left out of "play the list" and shuffle unless the owner ticks "include jingles" |

### 1.6 The full table

`page.entry` = the sound-test page (1.4) and the position on it; "game use" lists the constant jingle call sites
(1.3). Lengths are the frames present / rate; loop times from the ADX header. Generated by
`sadc/build/research/jukebox/gen_table.py` from the disc (`music_survey.py`), cross-checked against libsa1's
`sa1_audio_stream_open` / `_read` for all 109 files (`jukebox_probe.log`: identical channels, rates, frames, loop
start and end).

| # | file | page.entry | sound-test title (ADVERTISE 0x8C90F5CC) | Hz | length | loop (start - end) | MB | game use |
|---|---|---|---|---|---|---|---|---|
| 0 | ADVAMY.ADX | 5.8 | Tutorial:  AMY | 44100 | 1:29.7 | 0:18.4 - 1:29.7 | 4.25 |  |
| 1 | ADVBIG.ADX | 5.10 | Tutorial:  BIG | 44100 | 0:58.0 | 0:01.2 - 0:58.0 | 2.75 |  |
| 2 | ADVE102.ADX | 5.9 | Tutorial:  E-102 | 32000 | 1:04.2 | 0:08.0 - 1:04.0 | 2.21 |  |
| 3 | ADVKNKLS.ADX | 5.7 | Tutorial:  KNUCKLES | 44100 | 1:21.3 | 0:21.8 - 1:21.3 | 3.85 |  |
| 4 | ADVMILES.ADX | 5.6 | Tutorial:  MILES | 44100 | 1:12.3 | 0:02.3 - 1:12.3 | 3.43 |  |
| 5 | ADVSONIC.ADX | 5.5 | Tutorial:  SONIC | 44100 | 0:49.5 | 0:07.8 - 0:49.5 | 2.34 |  |
| 6 | AMY.ADX | 1.5 | AMY:  My Sweet Passion | 44100 | 5:10.5 | no | 14.69 |  |
| 7 | BIG.ADX | 1.6 | BIG:  Lazy Days ~Livin' in Paradise~ | 44100 | 4:02.4 | no | 11.47 |  |
| 8 | BOSSALL.ADX | 2.38 | ENEMY:  Others | 32000 | 1:18.4 | 0:09.5 - 1:18.2 | 2.69 |  |
| 9 | BOSSE101.ADX | 2.35 | ENEMY:  E-101 Improved | 32000 | 2:18.2 | 0:18.0 - 2:18.0 | 4.75 |  |
| 10 | BOSSEVNT.ADX | 2.39 | ENEMY:  Event | 32000 | 1:43.7 | 0:06.8 - 1:43.4 | 3.56 |  |
| 11 | BOSSTRGT.ADX | 2.40 | ENEMY:  E-Series | 44100 | 1:22.2 | 0:02.2 - 1:22.2 | 3.89 |  |
| 12 | CASINO1.ADX | 2.20 | Casinopolis:  Main Hall | 44100 | 1:56.5 | 0:05.2 - 1:56.5 | 5.51 |  |
| 13 | CASINO2.ADX | 2.21 | Casinopolis:  Dilapidated Way | 44100 | 1:23.0 | 0:08.0 - 1:23.0 | 3.93 |  |
| 14 | CASINO3.ADX | 2.22 | Casinopolis:  SONIC Pinball | 44100 | 1:18.3 | 0:06.9 - 1:18.3 | 3.71 |  |
| 15 | CASINO4.ADX | 2.23 | Casinopolis:  NiGHTS Pinball | 44100 | 1:16.6 | 0:00.6 - 1:16.6 | 3.63 |  |
| 16 | CHAO.ADX | 1.11 | Theme of "CHAO" | 32000 | 2:09.0 | 0:27.3 - 2:08.9 | 4.43 |  |
| 17 | CHAOGOAL.ADX | 3.11 | Mini-Game:  CHAO Race - Goal | 32000 | 0:06.3 | no | 0.21 |  |
| 18 | CHAOHALL.ADX | 3.9 | Mini-Game:  CHAO Race - Entrance | 32000 | 1:07.2 | 0:30.1 - 1:07.0 | 2.31 |  |
| 19 | CHAORACE.ADX | 3.10 | Mini-Game:  CHAO Race | 32000 | 2:04.3 | 0:06.1 - 2:04.2 | 4.27 |  |
| 20 | CHAOS.ADX | 2.31 | ENEMY:  CHAOS | 44100 | 2:21.7 | 0:03.3 - 2:21.7 | 6.71 |  |
| 21 | CHAOS_6.ADX | 2.32 | ENEMY:  CHAOS ver.6 | 44100 | 1:37.6 | 0:12.5 - 1:37.6 | 4.62 |  |
| 22 | CHAOS_P1.ADX | 2.33 | ENEMY:  Perfect CHAOS 1st. | 44100 | 2:10.5 | 0:04.8 - 2:10.5 | 6.18 |  |
| 23 | CHAOS_P2.ADX | 2.34 | ENEMY:  Perfect CHAOS 2nd. | 44100 | 1:17.9 | 0:07.2 - 1:17.9 | 3.69 |  |
| 24 | CHARACTR.ADX | 5.3 | Choose Your Buddy! | 32000 | 1:06.4 | 0:04.2 - 1:06.1 | 2.28 |  |
| 25 | CIRCUIT.ADX | 3.6 | Mini-Game:  Twinkle Circuit | 32000 | 1:56.2 | 0:28.3 - 1:56.0 | 3.99 |  |
| 26 | CONTINUE.ADX | 6.11 | Continue? | 32000 | 0:30.2 | no | 1.04 | jingle (0C903140, 0C90A81E, 8C04CA40, 8C04DA20) |
| 27 | E102.ADX | 1.7 | Theme of "E-102" | 32000 | 4:32.1 | no | 9.34 |  |
| 28 | ECOAST1.ADX | 2.1 | Emerald Coast:  Type 1 | 44100 | 1:52.1 | 0:42.8 - 1:52.1 | 5.31 |  |
| 29 | ECOAST2.ADX | 2.2 | Emerald Coast:  Type 2 | 44100 | 1:57.1 | 0:28.9 - 1:57.1 | 5.54 |  |
| 30 | ECOAST3.ADX | 2.3 | Emerald Coast:  BIG:Fishin' | 44100 | 1:39.3 | 0:02.1 - 1:39.3 | 4.70 |  |
| 31 | EGCARER1.ADX | 3.3 | Egg Carrier | 32000 | 2:32.6 | 0:34.3 - 2:32.4 | 5.24 |  |
| 32 | EGCARER2.ADX | 3.4 | Egg Carrier ~the Ocean~ | 32000 | 1:36.9 | 0:07.8 - 1:36.7 | 3.33 |  |
| 33 | EGGMAN.ADX | 1.8 | Theme of "Dr.EGGMAN" | 44100 | 1:31.6 | 0:34.4 - 1:31.6 | 4.34 |  |
| 34 | EGGMBL23.ADX | 2.36 | ENEMY:  Eggmobile2&3 | 44100 | 0:53.9 | 0:03.1 - 0:53.9 | 2.55 |  |
| 35 | EGGROBO.ADX | 2.37 | ENEMY:  ZERO | 44100 | 1:14.7 | 0:08.8 - 1:14.7 | 3.54 |  |
| 36 | EVTBGM00.ADX | 4.1 | Sadness | 44100 | 0:44.0 | 0:12.8 - 0:44.0 | 2.09 |  |
| 37 | EVTBGM01.ADX | 4.2 | Strain | 44100 | 0:56.5 | 0:07.3 - 0:56.5 | 2.68 |  |
| 38 | EVTBGM02.ADX | 4.3 | Unbound | 44100 | 0:56.5 | 0:23.1 - 0:56.5 | 2.67 |  |
| 39 | EVTBGM03.ADX | 4.4 | Good-bye! | 44100 | 1:18.3 | no | 3.71 |  |
| 40 | EVTBGM04.ADX | 4.5 | The Past | 44100 | 1:29.6 | no | 4.24 |  |
| 41 | EVTBGM05.ADX | 4.6 | Fanfare 4 "Dr.EGGMAN" | 32000 | 0:24.5 | no | 0.84 |  |
| 42 | FINALEG1.ADX | 2.29 | Final Egg:  Type 1 | 44100 | 1:34.1 | 0:16.8 - 1:34.1 | 4.46 |  |
| 43 | FINALEG2.ADX | 2.30 | Final Egg:  Type 2 | 44100 | 1:46.1 | 0:29.8 - 1:46.1 | 5.02 |  |
| 44 | FISHGET.ADX | 6.3 | BIG:  Get! | 32000 | 0:07.6 | no | 0.26 | jingle (8C0E6446) |
| 45 | FISHING.ADX | 6.2 | BIG:  Hit! | 44100 | 0:50.1 | 0:10.1 - 0:50.1 | 2.38 | jingle (8C0EB244) |
| 46 | FISHMISS.ADX | 6.4 | BIG:  Miss! | 32000 | 0:04.9 | no | 0.17 | jingle (8C0E8562) |
| 47 | HAMMER.ADX | 3.8 | Mini-Game:  Hedgehog Hammer | 32000 | 0:40.5 | 0:09.4 - 0:40.5 | 1.39 |  |
| 48 | HIGHWAY1.ADX | 2.17 | Speed Highway:  Speed Highway | 44100 | 1:51.5 | 0:07.1 - 1:51.5 | 5.28 |  |
| 49 | HIGHWAY2.ADX | 2.18 | Speed Highway:  Goin' Down | 32000 | 0:40.3 | no | 1.38 |  |
| 50 | HIGHWAY3.ADX | 2.19 | Speed Highway:  At Dawn | 44100 | 2:19.7 | 0:19.7 - 2:19.7 | 6.61 |  |
| 51 | HURRYUP.ADX | 6.1 | AMY:  Hurry Up! | 32000 | 0:28.6 | 0:06.6 - 0:28.4 | 0.98 |  |
| 52 | ICECAP1.ADX | 2.4 | Icecap:  Snowy Mountain | 44100 | 1:42.3 | 0:03.1 - 1:42.3 | 4.84 |  |
| 53 | ICECAP2.ADX | 2.5 | Icecap:  Limestone Cave | 44100 | 1:27.5 | 0:03.6 - 1:27.5 | 4.14 |  |
| 54 | ICECAP3.ADX | 2.6 | Icecap:  Snowboard | 44100 | 2:21.5 | 0:11.3 - 2:21.5 | 6.70 |  |
| 55 | INVNCIBL.ADX | 6.6 | Invincible | 44100 | 0:23.5 | no | 1.11 | jingle (8C07D4A0) |
| 56 | ITEM1.ADX | 6.9 | Item | 44100 | 0:06.0 | no | 0.28 | jingle (0C900E70, 0C906E80, 0C90D480) |
| 57 | JINGLE_1.ADX | 6.16 | Jingle#A | 32000 | 1:28.4 | 0:01.0 - 1:28.3 | 3.04 |  |
| 58 | JINGLE_2.ADX | 6.17 | Jingle#B | 32000 | 1:06.7 | 0:04.7 - 1:06.6 | 2.29 |  |
| 59 | JINGLE_3.ADX | 6.18 | Jingle#C | 32000 | 1:41.2 | 0:39.1 - 1:41.0 | 3.48 |  |
| 60 | JINGLE_4.ADX | 6.19 | Jingle#D | 32000 | 0:58.2 | 0:02.5 - 0:58.2 | 2.00 |  |
| 61 | JINGLE_5.ADX | 6.20 | Jingle#E | 32000 | 0:27.2 | 0:03.1 - 0:27.1 | 0.94 |  |
| 62 | KNUCKLES.ADX | 1.4 | KNUCKLES:  Unknown from M.E. | 44100 | 4:29.1 | no | 12.73 |  |
| 63 | LSTWRLD1.ADX | 2.10 | Lost World:  Tricky Maze | 44100 | 2:07.6 | 0:08.6 - 2:07.6 | 6.04 |  |
| 64 | LSTWRLD2.ADX | 2.11 | L.World:  DANGER - Chased by Rock! | 44100 | 1:07.6 | 0:08.1 - 1:07.6 | 3.20 |  |
| 65 | LSTWRLD3.ADX | 2.12 | Lost World:  Leading Lights | 44100 | 1:26.9 | 0:14.2 - 1:26.9 | 4.12 |  |
| 66 | MAINTHEM.ADX | 1.1 | Open Your Heart | 44100 | 4:32.7 | no | 12.90 |  |
| 67 | MSTCLN.ADX | 3.2 | Mystic Ruin | 44100 | 1:59.0 | 0:11.3 - 1:59.0 | 5.63 |  |
| 68 | NIGHTS_A.ADX | - | *(not in the sound test)* | 32000 | 4:25.2 | no | 9.11 |  |
| 69 | NIGHTS_K.ADX | - | **not on the disc** | | | | | |
| 70 | NIGHTS_S.ADX | - | *(not in the sound test)* | 32000 | 5:35.9 | no | 11.53 |  |
| 71 | ONE_UP.ADX | 6.8 | Extend | 32000 | 0:06.3 | no | 0.21 |  |
| 72 | OPTION.ADX | 5.2 | Options+VM | 44100 | 0:54.6 | 0:19.2 - 0:54.6 | 2.58 |  |
| 73 | REDMNTN1.ADX | 2.27 | Red Mountain:  Type 1 | 44100 | 1:27.5 | 0:18.0 - 1:27.5 | 4.14 |  |
| 74 | REDMNTN2.ADX | 2.28 | Red Mountain:  Type 2 | 32000 | 4:41.3 | 0:08.5 - 4:41.2 | 9.66 |  |
| 75 | RNDCLEAR.ADX | 6.7 | Round Clear | 32000 | 0:07.6 | no | 0.26 |  |
| 76 | S_SQUARE.ADX | 3.1 | Station Square | 44100 | 1:35.4 | 0:04.7 - 1:35.4 | 4.52 |  |
| 77 | SANDHILL.ADX | 3.7 | Mini-Game:  Sand Hill | 32000 | 1:44.2 | 0:21.1 - 1:44.1 | 3.58 |  |
| 78 | SCRAMBLE.ADX | 3.5 | Mini-Game:  TORNADO Scramble | 32000 | 1:08.3 | 0:16.1 - 1:08.1 | 2.35 |  |
| 79 | SHELTER1.ADX | 2.15 | Hot Shelter:  Type 1 | 44100 | 2:01.9 | 0:33.5 - 2:01.9 | 5.77 |  |
| 80 | SHELTER2.ADX | 2.16 | Hot Shelter:  Type 2 | 44100 | 1:37.8 | 0:17.9 - 1:37.8 | 4.63 |  |
| 81 | SKYDECK1.ADX | 2.13 | Sky Deck:  Type 1 | 44100 | 1:36.8 | 0:03.2 - 1:36.8 | 4.58 |  |
| 82 | SKYDECK2.ADX | 2.14 | Sky Deck:  Type 2 | 32000 | 1:26.8 | 0:07.5 - 1:26.6 | 2.98 |  |
| 83 | SONIC.ADX | 1.2 | SONIC:  It Doesn't Matter | 44100 | 4:27.2 | no | 12.64 |  |
| 84 | SONIC_CD.ADX | - | *(not in the sound test)* | 32000 | 3:01.2 | 0:01.7 - 3:01.1 | 6.22 |  |
| 85 | SPEEDUP.ADX | 6.5 | Speed Up | 32000 | 0:15.8 | no | 0.54 | jingle (8C07D2DA) |
| 86 | SPRSONIC.ADX | 1.9 | Theme of "SUPER SONIC" | 44100 | 0:52.9 | 0:19.5 - 0:52.9 | 2.51 |  |
| 87 | SSRACING.ADX | - | *(not in the sound test)* | 32000 | 2:56.0 (header 3:30.4; file truncated) | no | 6.04 |  |
| 88 | TAILS.ADX | 1.3 | MILES:  Believe In Myself | 44100 | 3:50.8 | no | 10.92 |  |
| 89 | THEAMY.ADX | 4.10 | Appearance:  AMY | 44100 | 1:35.7 | 0:00.4 - 1:35.7 | 4.53 |  |
| 90 | THEBIG.ADX | 4.12 | Appearance:  BIG | 44100 | 1:04.4 | 0:05.9 - 1:04.4 | 3.05 |  |
| 91 | THEE102.ADX | 4.11 | Appearance:  E-102 | 32000 | 1:20.5 | 0:24.4 - 1:20.5 | 2.77 |  |
| 92 | THEKNKLS.ADX | 4.9 | Appearance:  KNUCKLES | 44100 | 1:12.5 | 0:03.9 - 1:12.5 | 3.43 |  |
| 93 | THEMILES.ADX | 4.8 | Appearance:  MILES | 44100 | 0:44.6 | 0:03.0 - 0:44.6 | 2.11 |  |
| 94 | THESONIC.ADX | 4.7 | Appearance:  SONIC | 44100 | 0:55.2 | 0:11.8 - 0:55.2 | 2.61 |  |
| 95 | TICAL.ADX | 1.10 | Theme of "TIKAL" | 32000 | 1:57.3 | 0:54.1 - 1:57.3 | 4.03 |  |
| 96 | TIMER.ADX | 6.10 | Timer | 32000 | 0:15.2 | no | 0.52 | jingle (8C07BE6E) |
| 97 | TITL_EGG.ADX | 6.15 | Title:  Egg Carrier | 32000 | 0:10.1 | no | 0.35 |  |
| 98 | TITL_MR1.ADX | 6.13 | Title:  Mystic Ruin | 32000 | 0:08.2 | no | 0.28 |  |
| 99 | TITL_MR2.ADX | 6.14 | Title:  M. Ruin ~the Past~ | 32000 | 0:09.2 | no | 0.32 |  |
| 100 | TITL_SS.ADX | 6.12 | Title:  Station Square | 32000 | 0:06.0 | no | 0.21 |  |
| 101 | TITLE.ADX | 5.1 | Title | 44100 | 0:57.6 | no | 2.73 |  |
| 102 | TITLE2.ADX | - | *(not in the sound test)* | 44100 | 0:13.1 | no | 0.62 |  |
| 103 | TRIAL.ADX | 5.4 | Trial | 44100 | 0:53.9 | 0:03.4 - 0:53.9 | 2.55 |  |
| 104 | TWNKLPK1.ADX | 2.24 | Twinkle Park:  Twinkle Park | 32000 | 1:48.3 | 0:27.4 - 1:48.2 | 3.72 |  |
| 105 | TWNKLPK2.ADX | 2.25 | Twinkle Park:  Pleasure Castle | 44100 | 1:27.6 | 0:02.4 - 1:27.6 | 4.15 |  |
| 106 | TWNKLPK3.ADX | 2.26 | Twinkle Park:  AMY:Mirror Room | 32000 | 0:50.3 | 0:04.7 - 0:50.2 | 1.73 |  |
| 107 | WNDYVLY1.ADX | 2.7 | Windy Valley:  Windy Hill | 44100 | 1:29.8 | 0:02.5 - 1:29.8 | 4.25 |  |
| 108 | WNDYVLY2.ADX | 2.8 | Windy Valley:  Tornado | 32000 | 0:50.9 | 0:15.8 - 0:50.6 | 1.75 |  |
| 109 | WNDYVLY3.ADX | 2.9 | Windy Valley:  The Air | 44100 | 1:27.6 | 0:05.3 - 1:27.6 | 4.14 |  |

## 2. How G-SA1 plays audio today

* **Backend.** Garry's Mod's own BASS 2.4: the module takes the already-loaded `bass.dll` with `GetModuleHandleW`
  (`g-sa1/src/core/audio.cpp:219`; `libbass.so` with `RTLD_NOLOAD` on Linux), checks version 2.4 and binds 12
  functions (`:228-245`). One push stream (`STREAMPROC_PUSH`), 44.1 kHz stereo 16-bit (`:263`), BASS playback buffer
  0 so the push queue is the latency (`:269`). Nothing is written to disk; G-SA1 never loads a library.
* **Mixing and threads.** libsa1's mixer (`sa1_audio_mixer`, one per client, `Session::audio_`) runs on the Lua main
  thread: the `Think` hook `gsa1_tick` (`addon/lua/gsa1/cl_character.lua:366`) calls `update_audio` (`:292`, called at
  `:388`) -> `gsa1.AudioUpdate` (`src/module/gsa1_module.cpp:1717`), which renders enough to keep
  `clamp(2 x frame time + 30 ms, 60 ms, 200 ms)` queued (`:1747-1756`). libsa1's loader thread reads whole ADX files
  into a clip cache (48 slots, 64 MB LRU, the jingles pinned: `libsa1/src/sa1_audio_load.c:30-41`) and decodes the
  sound-effect tones; the mixer never touches the disc. A Lua-thread stall longer than the queue is an audible gap.
* **Events.** After each tick `Session::take_events` hands every libsa1 event to the mixer (`src/core/session.cpp:526`);
  a character's position goes to the mixer each tick, local characters as 2D sources (`:625`), puppets 3D (`:662`);
  a destroyed character's sounds are stopped (`:477`).
* **2D / 3D.** Positional cues use the game's curve (`libsa1/src/sa1_audio_mix.c:791` `spatial`: CC7 = 106 - 127 f,
  gain (CC7 / 106)^2, soft pan 0.6 from the listener's right); the listener is the chase camera or the eyes
  (`cl_character.lua:292-305`). The local character's own sounds are 2D like in the game; other characters' voices
  are 3D when `gsa1_sound_3d_voices` is on.
* **Options.** ConVars `gsa1_sound`, `gsa1_sound_volume` / `_sfx` / `_voice` / `_music` (0..4), `_voices`,
  `_jingles`, `_bgm` (default 0), `_3d_voices`, `_japanese` (`addon/lua/gsa1/sh_config.lua:134-144`); the Q menu page
  Options > G-SA1 > Sound (`cl_menu.lua:195`, registered at `:277`). Garry's Mod's `volume` ConVar is applied unless
  GMod already scales BASS's streams (`gsa1_module.cpp:1744-1746`, `audio.cpp:339`); output pauses in the
  single-player escape menu and with `snd_mute_losefocus` (`cl_character.lua:287-305`).
* **Music cues: global, not per character.** The mixer has one background-music slot and one jingle slot
  (`sa1_audio_mix.c:572-574`, `:1059-1080`): a MUSIC cue from any character is remembered and played only when
  `gsa1_sound_bgm` is on (default off: the character's world has no music of its own, `g-sa1/README.md:515-516`); a JINGLE
  (8C107C80) fades the previous jingle and plays globally, 2D; while it plays the background clip is skipped and does
  not advance (`:1366-1369`), so after the jingle it resumes where it was (the game restarts the saved track, 1.2);
  RESTORE fades the jingle, MUSIC_STOP stops both. `sa1_audio_mixer_stop(character)` stops only that character's
  voices (`:1193-1212`): a removed character's jingle plays on to its end.
* **No music API for Lua.** Lua cannot ask for a music number; the only music is what the game's code requests.
* **Multiplayer**: remote puppets are not implemented yet (`addon/lua/gsa1/sh_net.lua:4-9`); the server side is pure
  Lua and has neither the module nor a disc (`sv_gsa1.lua:1`).

## 3. Garry's Mod realities

* `sound.PlayFile` / `sound.PlayURL` (BASS-based `IGModAudioChannel`) open files from Garry's Mod's own file system or
  URLs only: the .gdi is outside it, and BASS has no CRI ADX decoder anyway. Decoding to WAV in `data/` (the plan of
  `docs/research/gmod.md` 6.2) would write the owner's game audio to disk (a vocal song is about 50 MB of WAV),
  contradicts G-SA1's "nothing from the disc is written anywhere" (`cl_menu.lua` disc page) and needs `SetTime`
  polling for loop points; `gmod.md` also notes that looping `.wav` in 3D mode is broken (issue 1752).
* `sound.Generate` takes 11025 / 22050 / 44100 Hz only (46 tracks are 32 kHz), the PCM as a Lua string (a 5-minute
  stereo track is about 53 MB) and identifiers that cannot be replaced (`gmod.md` 6.2).
* **The module already mixes PCM itself and pushes it into GMod's BASS** (section 2), so a jukebox does the same. BASS
  attributes the jukebox needs beyond today's binding: `BASS_ATTRIB_PAN` (3), `BASS_ChannelSlideAttribute`; streams
  can be created at any rate (32000 / 44100) and BASS resamples to the device.
* The server (dedicated or listen) has no module and no disc: it can hold only numbers (music number, start time,
  mode, lengths reported by a client). Every client decodes from its own disc; nothing ripped goes over the network
  (`sh_net.lua:10`), titles included (each client resolves a music number to a title from its own disc). Players
  without G-SA1 (the module cannot come from the Workshop) or without a disc hear nothing.
* `CurTime()` is the clock shared by server and clients. Networked entity variables reach a client only while it
  has the entity (PVS) unless the entity transmits always.
* Lua-thread hitches (the owner's freezes: `docs/research/gsa1_ramp_freeze.md`; whole-BSP `file.Read` of workshop maps,
  map collision builds) starve any stream that the Lua thread feeds; music gaps are far more noticeable than a missed
  sound effect.

## 4. Measurements

Probes linked against a copy of `libsa1.a` (x86_64-windows, 2026-10-20 14:59), run on this machine against the .gdi
(`jukebox_probe.log`, `probe_mem.log`). The first pass ran without a warm OS file cache for most files.

| what | result |
|---|---|
| `sa1_audio_open`, `SA1_AUDIO_NO_PRELOAD`, first run | 311.5 ms |
| a further audio object, OS cache warm: default + preload / `NO_PRELOAD` / `NO_PRELOAD`+`NO_VOICES`+Sonic only (+`SYNC_LOADS`) | 18 ms + 29 ms preload, +19.9 MB / 17.7 ms, +3.1 MB / 17.5 ms, +1.8 MB (16.5 ms, +1.7 MB) |
| `sa1_audio_stream_open` (reads the whole file on the calling thread) | 0.2 ms (FISHGET, 274 KB) to 837 ms (AMY.ADX, 15.4 MB); 12 of 109 files over 400 ms |
| ADX decode, all 109 files | 1,304 ms for 10,212 s = **0.128 ms per second of stereo audio** (one core) |
| loop wrap (`stream_read`, loop = 1, across `loop_end`) | all 79 looping files continue (3,000 of 3,000 frames) |
| host-made `SA1_AUDIO_CUE_MUSIC` (MAINTHEM, cold) into a mixer with `SA1_AUDIO_MIX_MUSIC` | call 0.01 ms; first sound after 150 ms of rendered audio (215 ms wall); warm again: 10 ms |
| JINGLE 85 over it / RESTORE | only the jingle is heard (RMS 4015, 2 clips active) / back to the music (1 clip) |
| MUSIC cue for 69 (NIGHTS_K, absent) | returns OK; the previous music fades, nothing plays (0 active), `read_errors` 1 |
| two mixers on one audio object | play two different tracks at the same time (RMS 4138 and 7559) |
| mixer render, 10 s with one music clip | 6.5 ms |
| loader thread, slowest whole-file read (warm) | 13.3 ms |

Peaks: 99 of 109 files reach full scale (32767 / -32768) when decoded, so a jukebox gain above 1 would clip.

## 5. Options considered

| | option | verdict |
|---|---|---|
| A | `sound.PlayFile` on WAVs decoded into `data/` | rejected: game audio on disk, 50 MB per song, loop points by polling, 3D loop bug (section 3) |
| B | `sound.Generate` + `EmitSound` / `CreateSound` | rejected: no 32 kHz, whole PCM in Lua strings, fixed identifiers |
| C | host-made MUSIC cues into the existing game mixer (works with today's LIB: section 4) | rejected as the design (fine as a one-hour stop-gap): one slot shared with the game's own BGM and jingles (a jingle mutes it, a game MUSIC cue replaces it, MUSIC_STOP kills it), no seek, position, pause, end-of-track or 3D, fed from the Lua thread with 60-200 ms of queue |
| D | a BASS `STREAMPROC` callback that calls libsa1 on BASS's thread | rejected: libsa1 audio objects, mixers and streams are single-thread (`libsa1_audio.h` header comment "Threading"), `stream_open` reads whole files (up to 837 ms) and must not run on GMod's audio thread, a callback into the DLL at module unload is a crash risk |
| E | BASS 3D (`BASS_SAMPLE_3D`, `BASS_ChannelSet3DPosition`) for world jukeboxes | not chosen: depends on how GMod sets BASS's 3D listener, units and factors (**UNVERIFIED**); a gain and pan computed by G-SA1 with the game's curve is predictable and matches the other G-SA1 sounds |
| F | a thread-safe "player" object in LIB | good later, but changes LIB's threading contract while round 4b edits the audio loader; F's benefit is had with a second, private audio object (+1.8 MB, 17 ms) |
| **G** | **module jukebox engine: own music-only audio object on its own thread, one BASS push stream per playing jukebox, gains from the Lua thread; small LIB info API** | **recommended (section 6)** |

## 6. Recommended design

### 6.1 Overview

```
 Lua main thread (client)                     jukebox worker thread (module)             Garry's Mod's BASS
 ---------------------------------            ------------------------------------       ---------------------
 cl_menu.lua  Q menu > G-SA1 > Jukebox  -+    Jukebox (src/core/jukebox.cpp):            one push stream per
 cl_jukebox.lua  playlist, world sync,   |    own sa1_context + music-only sa1_audio     playing channel, at
   ducking, gsa1.JukeboxUpdate per frame |cmd stream_open / read / seek (libsa1)  -put-> the track's rate
                                         +--> keeps 1.5 s queued per channel             (BASS resamples)
 entities/gsa1_jukebox.lua (client part) <-st                                   <-VOL/PAN- set from the Lua thread
 ---------------------------------------------------------------------------------------------------------------
 server (pure Lua): sv_jukebox.lua + gsa1_jukebox entity vars: music, start time (CurTime), mode, volume, radius
   -> every client with G-SA1 + disc plays the same music number from its own disc, at CurTime() - start time
```

Channel 0 is the personal jukebox (2D, only this client hears it); channels 1-4 play the nearest audible world
jukeboxes (3D). The game's own sound (libsa1 mixer, section 2) is untouched except for a "duck" gain on its music.

### 6.2 LIB: one additive API (header version 3) - work item J0

Needed for the track list (titles, pages, lengths, loops) without reading 432 MB; the rest of the jukebox uses the
existing stream API. Proposed (`libsa1/include/libsa1_audio.h`):

```c
#define SA1_AUDIO_HEADER_VERSION 3
#define SA1_AUDIO_MUSIC_ONLY      0x40u /* sa1_audio_open: no sound banks, no voice archive: music numbers, info and
                                           streams only (a jukebox's own object) */
/* sa1_audio_music_info.flags */
#define SA1_AUDIO_MUSIC_ON_DISC   0x1u  /* the file exists (69 NIGHTS_K.ADX does not) */
#define SA1_AUDIO_MUSIC_LISTED    0x2u  /* in the game's sound test (ADVERTISE.PRS 0x8C90F5CC) */
#define SA1_AUDIO_MUSIC_JINGLE    0x4u  /* the game plays it over the background music (8C107C80): 26 44 45 46 55 56 85 96 */
#define SA1_AUDIO_MUSIC_TRUNCATED 0x8u  /* fewer frames in the file than its header says (87 SSRACING.ADX) */

typedef struct sa1_audio_music_info {
    uint32_t    struct_size;
    uint32_t    music;
    const char *file;          /* "ECOAST1.ADX" (name table 0x8C5FB8A8) */
    const char *title;         /* the sound-test title ("Emerald Coast:  Type 1"); NULL when not listed or when
                                  ADVERTISE.PRS is not the supported one */
    int32_t     page, entry;   /* sound-test page 0..5 and position on it; -1 when not listed */
    uint32_t    flags;         /* SA1_AUDIO_MUSIC_* */
    uint32_t    channels, sample_rate, num_frames;   /* num_frames: the frames present in the file */
    int32_t     loop;
    uint32_t    loop_start, loop_end;                /* frames; loop_end exclusive (as sa1_audio_stream_info) */
    uint32_t    file_size;
} sa1_audio_music_info;

/* Reads the ADX header (the first 4,096 bytes of the file; the largest data offset on the disc is 2,096) on the
 * calling thread the first time a music number is asked for, and ADVERTISE.PRS once (176,538 bytes, PRS-decompressed
 * and checked by SHA-1). SA1_ERR_NOT_FOUND for music >= sa1_audio_music_count. */
SA1_API sa1_status sa1_audio_music_info(sa1_audio *audio, uint32_t music, sa1_audio_music_info *out);
```

Implementation notes: `sa1_audio.c` already has `read_sonicadv`, the SONICADV directory and `ADDR_MUSIC_NAMES`; LIB
has a PRS decoder (`src/sa1_prs.c`) and SHA-1 (`src/sa1_sha1.c`). Titles: walk the six `{record *, count}` pairs at
`0x8C90F90C` (load address `0x8C900000`), records `{u32 title, s32 music}`, strings ASCII up to 64 bytes; accept only
the supported ADVERTISE (decompressed SHA-1 `5abb7a8b0d85e90b4ee9ee6815a5ad10ca5a81e7`, PRS SHA-1
`1166c5d474790fb18f304b0d4a544301c3547236` per `sadc/baserom/overlays.json`) unless `SA1_AUDIO_ACCEPT_MODIFIED`.
The jingle set is a generated table with its call sites in a comment (section 1.3). Also make `sa1a_adx_exists`
report `SA1_ERR_NOT_FOUND` for a music file that is not in the SONICADV directory (today it returns OK for any index
below the count, `sa1_audio.c:505-520`, so a cue for 69 fades the current music and fails later in the loader).
`SA1_AUDIO_MUSIC_ONLY` is optional (without it the jukebox opens with `NO_PRELOAD | NO_VOICES` and Sonic's banks
only: +1.8 MB, section 4). The threading contract does not change: the jukebox calls these on its own object.

Until J0 lands, the engine (J1) can list file names and learn a track's length when it first opens it.

### 6.3 The module's jukebox engine - work item J1

`g-sa1/src/core/jukebox.{hpp,cpp}` (core: no GMod dependency, linked into the tests) and `g-sa1/src/core/bass.{hpp,
cpp}` (the BASS binding taken out of `audio.cpp`'s private `AudioOut::Fns`, plus `BASS_ATTRIB_PAN` and
`BASS_ChannelSlideAttribute`, so `AudioOut` and the jukebox share it).

* **Own libsa1 objects, own thread.** `Jukebox::start(gdi)` starts one worker thread that creates its own
  `sa1_context` (`sa1_init`; its log callback appends to a mutex-protected queue that the Lua thread drains into G-SA1's
  log, because this callback runs on the worker) and its own `sa1_audio` from the same .gdi path the session uses
  (`SA1_AUDIO_MUSIC_ONLY` after J0, else `NO_PRELOAD | NO_VOICES | SYNC_LOADS`, `characters = 1 << SA1_CHAR_SONIC`).
  Only this thread ever touches them, which keeps libsa1's one-thread-per-object rule. It then reads
  `sa1_audio_music_info` for all 110 numbers and publishes the immutable track list. Opened lazily at the first
  jukebox use, independent of `gsa1_sound` and of `gsa1.AudioOpen` / `AudioClose`.
* **Channels.** 0 = personal (2D), 1-4 = world (3D); each has a plan, an `sa1_audio_stream`, a BASS handle, the
  frames pushed and a state (`idle`, `loading`, `playing`, `paused`, `ended`, `failed`). Commands from the Lua thread
  go through a mutex-protected mailbox; the worker wakes on a condition variable or every 20 ms.
* **Plans.** `forever` (like the game and its sound test: loop at the header loop; a file without a loop starts again
  at 0), `loops n` (play to `loop_start + n x (loop_end - loop_start)`, then fade out over f seconds; a file without a
  loop plays once), `once` (to the end of the file, the loop ignored). Plan time t maps to a source frame:
  `t x rate` before `loop_end`, else `loop_start + (t x rate - loop_start) mod (loop_end - loop_start)`; a t beyond the
  plan's end gives `ended` at once (a late joiner of a finished track).
* **Feeding.** For every playing channel the worker keeps 1.5 s queued: `queued = BASS_StreamPutData(h, NULL, 0) +
  BASS_ChannelGetData(h, NULL, BASS_DATA_AVAILABLE)` (as `AudioOut::queued_frames`, `audio.cpp:284-293`), decodes 0.25 s
  blocks with `sa1_audio_stream_read` (loop flag from the plan), applies fade envelopes to the PCM and pushes them.
  Decoding costs 0.128 ms per second of audio (section 4): negligible.
* **Output.** One BASS push stream per channel, created at the track's own rate (`BASS_StreamCreate(rate, 2, 0,
  STREAMPROC_PUSH)`, playback buffer 0 like `audio.cpp:263-269`), so BASS resamples 32 kHz tracks with its own
  interpolation and no G-SA1 resampler is involved. A start, seek or track change creates a new stream and slides the
  old one's volume to 0 over 50 ms before freeing it: no queue flush is needed and there is no click.
* **Blocking opens.** `sa1_audio_stream_open` reads the whole file on the worker (up to 837 ms cold, section 4); the
  other channels keep playing from their 1.5 s of queue. The next track of the personal playlist is opened ahead of
  time (prefetch) so list play has no gap.
* **Position and sync.** Position = frames pushed - frames queued, reported as plan time. `JukeboxSync(ch, t)` (world
  channels, about once a second) seeks when the drift exceeds 0.25 s. A backward seek re-decodes from the start of the
  file (at most about 43 ms, on the worker).
* **Gain and 3D.** The Lua thread passes the listener every frame (`gsa1.JukeboxUpdate`, called next to
  `update_audio`, same origin and angles, `cl_character.lua:292-305`) and each world channel's position, radius and
  volume (`gsa1.JukeboxPlace`). The module computes the gain with the game's curve (as `sa1_audio_mix.c:791`: near =
  0.1 x radius, far = radius; gain `(cc / 106)^2`, `cc = 106 - 127 f`, silent from about 0.85 x radius) and pan `0.6 x`
  the listener-right component, multiplies jukebox volume x world volume (or personal volume) x Garry's Mod's
  `volume` when GMod does not scale BASS streams itself (the rule of `gsa1_module.cpp:1744-1746`) x optionally
  `snd_musicvolume`, and applies it with `BASS_ChannelSlideAttribute(VOL / PAN, 100 ms)` when it changed. Attribute
  changes act at once although 1.5 s is queued. Only the 4 loudest audible world jukeboxes get a channel.
* **Pause.** `BASS_ChannelPause` / `BASS_ChannelPlay` per channel; the personal channel follows G-SA1's paused rule
  (single-player escape menu, `snd_mute_losefocus`) unless "keep playing in the background" is ticked; world channels
  follow it too (in single player `CurTime()` stops as well, so sync holds).
* **Ducking the game's music.** New `Audio::set_duck(g)` multiplies the game mixer's music channel
  (`sa1_audio_mixer_volume(SA1_AUDIO_VOL_MUSIC)`) by g, ramped over 0.5 s in `AudioUpdate`; `gsa1_sound_music` keeps
  its own value. Lua sets g = 0 while the option is on and any jukebox channel is audible (gain > 0.05), else 1.
* **Errors.** A music number that is not on the disc (69) or fails to open goes to `failed` with the message; it is
  never in the list. If ADVERTISE is not the supported version the titles are the file names.
* **Memory.** Each playing channel holds its whole ADX (`sa1_audio_stream` keeps a copy of the file): up to 15.4 MB, plus
  one prefetched track; worst case 6 x 15.4 = 92 MB, typically 4-13 MB.
* **Shutdown.** `GMOD_MODULE_CLOSE` (`gsa1_module.cpp:2181`) stops the jukebox first: join the worker, free the BASS
  streams (BASS stays loaded: it belongs to GMod), close the stream, audio object and context. Choosing another disc
  restarts the jukebox.

### 6.4 Module Lua API (module API 6)

| function | returns | notes |
|---|---|---|
| `gsa1.MusicList()` | `nil, "loading"` or a list | entries on the disc only: `{music, file, title, page, entry, seconds, loop, loop_start, loop_end, rate, listed, jingle, short}`; `short` = no loop and under 25 s (section 1.5) |
| `gsa1.JukeboxPlay(ch, music, start, plan, loops, fade_in, fade_out)` | `ok, message` | ch 0..4; plan `"forever"`, `"loops"`, `"once"`; start in plan seconds |
| `gsa1.JukeboxStop(ch, fade)` / `gsa1.JukeboxPause(ch, paused)` / `gsa1.JukeboxSeek(ch, t)` / `gsa1.JukeboxSync(ch, t)` | - | Sync seeks only on a drift over 0.25 s |
| `gsa1.JukeboxVolume(ch, volume)` | - | 0..1 (99 of 109 files peak at full scale, so no gain above 1) |
| `gsa1.JukeboxPlace(ch, pos, radius)` / `gsa1.JukeboxPlace(ch, nil)` | - | 3D for a world channel / 2D |
| `gsa1.JukeboxUpdate(eye, forward, up, gameVolume, paused)` | number of audible channels | every frame, after `update_audio` |
| `gsa1.JukeboxState(ch)` | `{music, state, position, length, loops_done, error}` | |
| `gsa1.JukeboxStatus()` | `{ready, tracks, active, memory_mb, max_open_ms, underruns, error}` | status line, `gsa1_status` |
| `gsa1.AudioDuck(gain)` | - | the game music multiplier (0..1, ramped) |

### 6.5 The Q menu page (Options > G-SA1 > Jukebox) - work item J2

Registered like the other pages (`cl_menu.lua:272-280`: `spawnmenu.AddToolMenuOption("Options", "G-SA1",
"gsa1_jukebox", "Jukebox", ...)`). Every option is a ConVar with a control on this page (owner rule); console commands
are extras.

| control | ConVar (client, archived) | default |
|---|---|---|
| Now playing: title (file), time / length, "loops from m:ss", state (loading / failed: message) | - | - |
| Play, Pause, Stop, Previous, Next buttons; seek slider | - | - |
| Group: All, the six sound-test pages (section 1.4 names), Not in the sound test | - | All |
| Track list (number, title, length, loops yes/no); double-click plays | - | - |
| Play mode: Repeat the track (like the game's sound test) / Play the list / Shuffle | `gsa1_jukebox_mode` 0 / 1 / 2 | 0 |
| Loops before the next track (list and shuffle) | `gsa1_jukebox_loops` 1..10 | 2 |
| Fade-out at the end of a looping track (s) | `gsa1_jukebox_fade` 0..10 | 4 |
| Include jingles and title cards in the list and shuffle (the 15 short tracks) | `gsa1_jukebox_jingles` | 0 |
| Show the tracks that are not in the game's sound test (5) | `gsa1_jukebox_hidden` | owner question 1 |
| Volume | `gsa1_jukebox_volume` 0..1 | 1 |
| Silence the game's music and jingles while a jukebox plays | `gsa1_jukebox_duck` | 1 |
| Follow Garry's Mod's music volume (`snd_musicvolume`) | `gsa1_jukebox_gmod_music` | 1 |
| Keep playing while the game is paused or in the background | `gsa1_jukebox_background` | 0 |
| Hear jukeboxes placed in the world | `gsa1_jukebox_world` | 1 |
| World jukebox volume | `gsa1_jukebox_world_volume` 0..1 | 1 |
| My jukebox mutes the world jukeboxes | `gsa1_jukebox_solo` | 1 |
| "Place a jukebox in front of me" (runs `gm_spawnsent gsa1_jukebox`) | - | - |
| Status line (engine ready, tracks, memory, slowest open, errors) | - | - |

Console extras: `gsa1_jukebox_play <number | file | words of the title>`, `gsa1_jukebox_stop`, `_pause`, `_next`,
`_prev`, `gsa1_jukebox_list`. The list widget (filter, list, transport, now playing) is one Lua function used both by
this page and by the world jukebox's window.

### 6.6 The world jukebox (spawnable entity) - work item J3

* **Entity** `gsa1_jukebox` (`addon/lua/entities/gsa1_jukebox.lua`, scripted entity, `base_gmodentity`, Spawnable,
  category "G-SA1", Entities tab). Model: a stock Half-Life 2 prop such as `models/props_lab/citizenradio.mdl` (no
  game assets; that the model is in every install is **UNVERIFIED**). `UpdateTransmitState` returns
  `TRANSMIT_ALWAYS` (few entities, and a client must know the state even out of its PVS).
* **Server state** (`SetupDataTables` NetworkVars): `Track` (int, -1 = none), `StartTime` (float, `CurTime()` at plan
  time 0), `PausedAt` (float plan time, -1 = running), `Plan` (int: forever / loops / once / list / shuffle), `Loops`,
  `Volume` (0..1), `Radius` (Hammer units, default 1500, server maximum), `PlanLength` (seconds until the end of the
  plan, 0 = forever), `Playlist` (comma-separated music numbers, at most 110 x 4 characters) and `PlayIndex`.
  Nothing else; titles are never sent.
* **Commands** (client -> server, `net` message `gsa1_jukebox`): play / pause / resume / stop / next / previous / seek /
  volume / radius / plan, with the entity. The sending client adds the lengths it read from its own disc (plan length,
  and per playlist entry), because the server has no disc; the server checks ranges (music 0..109, finite numbers,
  lengths 0..600 s), rate-limits (5 per second per player) and permissions (below), then sets the variables. Starting
  a track sets `StartTime = CurTime() + 0.5` so most clients have read the file before it starts (cold opens: up to
  0.84 s here).
* **Auto-advance** on the server (`ENT:Think` every 0.5 s): when `CurTime() >= StartTime + PlanLength` in a list or
  shuffle plan, the next entry starts (shuffle with `math.random` on the server, without repeats until the list is
  used up).
* **Clients** (`cl_jukebox.lua`): for each jukebox, compare the variables with what its channel plays; on a change
  call `JukeboxPlay(slot, Track, CurTime() - StartTime, ...)` (late joiners and reconnects start at the right place),
  `JukeboxSync` about once a second, `JukeboxPlace(slot, ent:GetPos(), Radius)` every frame. A client that has the
  module but no disc or no engine yet shows "Sonic Adventure track N" in the window.
* **Use (E)**: if allowed, the server sends `gsa1_jukebox_ui` to the player, whose client opens a window with the same
  list widget bound to the entity (commands go to the server). Others see "Now playing: ..." on hover (title from
  their own disc).
* **Permissions** (server ConVars, also on the page for admins): `gsa1_sv_jukebox_anyone` 0 (0: the player who spawned
  it - `ENT:GetCreator()` - and admins; 1: anyone), `sbox_maxgsa1_jukeboxes` 2, `gsa1_sv_jukebox_max_radius` 4000,
  admin command `gsa1_jukebox_stopall`.

Who hears what:

| listener | personal jukebox | world jukebox |
|---|---|---|
| you (module + disc) | yes, 2D, only you | yes, 3D within its radius, in sync with everyone |
| other players with G-SA1 and a disc | no | yes, the same music at the same position (within about 0.25 s) |
| players without the module or a disc | no | no (the window / hover text says why) |
| dedicated server | - | holds the state only (pure Lua) |

### 6.7 What the owner gets

Q menu > Options > G-SA1 > Jukebox: the game's 104 sound-test tracks under their own titles (plus the 5 hidden files
if wanted), play / pause / stop / next / previous / shuffle / repeat, volume, "silence the game's music"; the music
keeps playing through hitches (1.5 s queued by a thread of the module, not by Lua). Entities > G-SA1 > Jukebox: a radio
that plays a chosen track or list to everyone nearby who has G-SA1 and the disc.

## 7. Work plan

| item | when | files | tests |
|---|---|---|---|
| **J0** LIB `sa1_audio_music_info` (+ `MUSIC_ONLY`, missing-file check) | after round 4b releases `libsa1/src/sa1_audio*.c` | `libsa1/include/libsa1_audio.h`, `libsa1/src/sa1_audio.c`, `libsa1/src/sa1_audio.h`, `libsa1/tests/audio_test.c`; `sadc/docs/research/sa1_sound.md` section 4 (sound test, 69, 87, jingle list) | `audio_test`: 110 infos, 109 `ON_DISC`, 104 `LISTED`, pages 11/40/11/12/10/20, titles 66 "Open Your Heart", 28 "Emerald Coast:  Type 1", 61 "Jingle#E" (page 5 entry 19, 0-based); `JINGLE` = {26, 44, 45, 46, 55, 56, 85, 96}; `TRUNCATED` = {87}; for all 109 the info equals `sa1_audio_stream_open`'s (channels, rate, frames, loop); at most 2 sectors read per info (counting file callback); a modified ADVERTISE (file callback flips a byte) gives NULL titles and SA1_OK; `MUSIC_ONLY` opens without banks and streams still work; a MUSIC cue for 69 returns NOT_FOUND and leaves the current music playing |
| **J1** module engine | after J0 (or before it with file names) | `g-sa1/src/core/bass.{hpp,cpp}` (new), `src/core/audio.{hpp,cpp}` (use `bass.hpp`, `Audio::set_duck`), `src/core/jukebox.{hpp,cpp}` (new), `src/module/gsa1_module.cpp` (6.4 functions, API 6, close order), `build.py` (sources, test), `tests/jukebox_test.cpp` (new), `tests/binding_test.cpp`, `tests/core_test.cpp` | `jukebox_test` (offline, with the disc, a fake output that records PCM and drains in real time): PCM at gain 1 equals `sa1_audio_stream_read` sample for sample (track 28 from 0 s and from 70 s); the loop seam equals a `loop = 1` stream read; `loops 2` ends at `loop_start + 2 x loop_len + fade` (one block); `once` ends at `num_frames`; a start beyond the plan's end is `ended`; pause / resume keeps the position; seek forward and back; 69 is not listed and `play(69)` is `failed`; a slow disc (file callback sleeping 1 s) while another channel plays causes no underrun; 5 channels at once; shutdown with all channels playing balances allocations; 3D gain curve and pan against the formula; `binding_test`: argument checks, list and state shapes; `core_test`: the duck ramps the game mixer's music volume and restores it |
| **J2** personal jukebox | after J1 | `addon/lua/gsa1/cl_jukebox.lua` (new), `addon/lua/gsa1/sh_jukebox.lua` (new: pure functions - next / previous, shuffle order, plan length, filters - shared with the server), `cl_menu.lua` (page + list widget), `sh_config.lua` (ConVars), `cl_character.lua` (call `JukeboxUpdate` beside `update_audio`), `autorun/gsa1_autorun.lua`, `g-sa1/README.md` | `tests/lua/jukebox_logic_test.lua` for `sh_jukebox.lua` (plain Lua 5.1 / LuaJIT if one is available - **UNVERIFIED** on this machine - else run in game with `lua_openscript_cl`); in game: the list shows 104 titles in six groups (+5), every control, game jingle silenced and restored (needs a way to start a jingle: e.g. the drowning countdown, or a debug command), a 1 s Lua stall (`lua_run_cl local t = SysTime() while SysTime() - t < 1 do end`) leaves no gap in the music |
| **J3** world jukebox | after J2 | `addon/lua/entities/gsa1_jukebox.lua` (new), `addon/lua/gsa1/sv_jukebox.lua` (new), `cl_jukebox.lua`, `sh_jukebox.lua`, `sh_config.lua` (net strings `gsa1_jukebox`, `gsa1_jukebox_ui`, server ConVars), `autorun/gsa1_autorun.lua` | `jukebox_logic_test.lua` (auto-advance, permission and range checks as pure functions); in game: spawn, E opens the window, 3D fade with distance and pan, two jukeboxes, limit `sbox_maxgsa1_jukeboxes`; multiplayer sync and late join need a second client (owner: a listen server with a second machine or a second Garry's Mod instance - **UNVERIFIED** that the owner can run one); a dedicated server without the module loads the addon |
| **J4** install and owner test | after an independent check of J1-J3 | GMod install as in `HANDOFF.md` item 6; `sadc/docs/gsa1_progress.html` and the status page | owner's in-game checklist (J2, J3) |

J1 and J2 do not depend on round 6a/6b. J0 is the only LIB change and is small (one function, one flag, a table).

## 8. Questions for the owner

1. The 5 tracks that are not in the game's sound test (`NIGHTS_A`, `NIGHTS_S`, `SONIC_CD`, `SSRACING` - cut short on the
   disc at 2:56 -, `TITLE2`): show them (in their own group) or hide them? Should they get official soundtrack titles
   (a small names-only table in G-SA1; which songs they are is **UNVERIFIED**: the file names suggest NiGHTS into
   Dreams songs, a Sonic CD song, Sonic R's "Super Sonic Racing" and a second title piece), or stay under their file
   names?
2. Default play mode: repeat the chosen track forever like the game's own sound test, or play the list (each looping
   track twice through its loop, then fade)?
3. "Silence the game's music while a jukebox plays": should the drowning countdown stay audible (it warns that the
   character is drowning)?
4. World jukeboxes: may only the player who placed one (and admins) change its music, or anyone?
5. World jukebox look: a stock Garry's Mod / Half-Life 2 radio model, or something from Sonic Adventure ripped from
   the disc at run time (more work)?

## 9. Risks and unverified points

* Calling BASS from the jukebox thread (`BASS_StreamPutData`, stream create / free) while Garry's Mod and G-SA1's Lua
  thread use it: BASS is meant to be thread-safe, **UNVERIFIED** inside GMod. Fallback: the worker decodes into a ring
  buffer and the Lua thread pushes (with 1.5 s queued, only stalls longer than that would gap).
* The sound test's call chain to 8C9019D2 and its menu entry; the ADXT function names 8C64413C / 8C6445EA and the
  -1 dB music level: **UNVERIFIED** (section 1.2, 1.4).
* Sync between clients is about +-0.25 s (CurTime, different cold-open times, drift correction threshold); fine for
  music, not sample-exact.
* Cold opens up to 0.84 s here (slower discs or network drives: more): the window shows "loading"; world tracks start
  0.5 s after the command to hide most of it.
* Memory per playing channel = the whole ADX (up to 15.4 MB); 4 world channels at most.
* Round 4b is editing LIB's audio loader and g-sa1 now: J0 must be rebased on its result; line numbers here may move.
* GMod's in-game BASS behaviour for push streams has not been checked by this round (G-SA1 0.2+ uses one); whether the
  owner has heard G-SA1's game sound in game so far is **UNVERIFIED** here.

## 10. Side findings (outside the jukebox)

* LIB resumes the background music after a jingle where it paused (`sa1_audio_mix.c:1366-1369`), the game restarts the
  saved track from the beginning (8C107CC2 -> 8C10792A `ADXT_StartFname`). Only matters with `gsa1_sound_bgm` on.
* `k_jingles` preloads `ONE_UP.ADX` (`sa1_audio_load.c:41`) but the game's 1-up is tone 704 (8C08A120 -> 8C0940DA)
  and ONE_UP.ADX has no constant call site: 225 KB pinned for nothing (harmless).
* `sa1a_adx_exists` accepts music 69 (`sa1_audio.c:505-520`): a MUSIC cue for it fades the current music and plays
  nothing (fix in J0).
* `sa1_audio_mixer_stop(character)` leaves that character's jingle playing (`sa1_audio_mix.c:1193-1212`); with
  followers (round 6b) the global jingle slot is shared by all characters.
* 8C107DAA (request + stage base) and 8C107DC2 (restore base) are not in LIB's event map (`gen/policy/stubs.tsv` lines
  108, 186-192 cover 8C04EF52, 8C107AAA, AC6, ADA, B26, BA0, C80, D80); stage code that uses them (STG10, STG12,
  SHOOTING, B_CHAOS*) would produce no MUSIC cue (**UNVERIFIED** whether any libsa1 world runs such code).
  `stubs.tsv:188` gives 8C107ADA the shared text "music state request without a number (restore the saved track
  ...)"; it is the stop (8C644468, saved track = -1), which LIB's cue map already has right (`FN_MUSIC_STOP`,
  `sa1_audio.c:1190`).

## Appendix A: scratch tools and logs (`sadc/build/research/jukebox/`)

| file | what |
|---|---|
| `music_survey.py` | reads the .gdi (ISO9660 via `sadc/tools/extract_gdi.py`, PRS via `sadc/tools/prs.py`): name table, ADVERTISE sound test, every ADX header; checks 1ST_READ and ADVERTISE SHA-1; writes `music_survey.{json,csv,log}` |
| `music_calls.py` | every music-function call site in `sadc/asm/**.src` with constant R4; `music_calls.{json,log}` |
| `gen_table.py` | the table of section 1.6 (`music_table.md`) |
| `find_strings.py`, `advertise_strings.py`, `soundtest_table.py` | string search in the game binaries; the sound-test strings and table (`advertise_strings.txt`) |
| `jukebox_probe.c`, `build_probe.py` | libsa1 stream / mixer measurements (section 4) against a copy of `libsa1.a` taken under LIB's build lock; `jukebox_probe.log` |
| `probe_mem.c` | cost of a further audio object; `probe_mem.log` |

Re-run: `py -3.12 -I music_survey.py [gdi]`, `py -3.12 -I music_calls.py`, `py -3.12 -I gen_table.py`,
`py -3.12 -I build_probe.py` then `jukebox_probe.exe <gdi>`; `probe_mem.exe` is built with the same zig command plus
`-lpsapi`. The logs hold no game audio; the JSON / CSV / table hold file names, titles and numbers only.

## Review (adversarial)

Round 6c adversarial review of this document, 2026-10-21, read-only. Scratch evidence in
`sadc/build/research/jukebox_review/`: `agg.py` (aggregates of `../jukebox/music_survey.csv`), `probe_stats.py`
(recount of `../jukebox/jukebox_probe.log`), `menu_music.py` (R4 of every ADVERTISE call of a menu music helper),
`probe_mem1.c` / `.exe` / `.log` (cost of one extra audio object, one configuration per fresh process, against the
investigator's `libsa1_copy.a`). Nothing else was written. The investigator's text above is unchanged.

### R1. Verified

* **Sound-test data.** `asm/overlays/advertise/data/8C90EAF0.src:866-898` holds 104 `{title *, s32 music}` records
  at 8C90F5CC..8C90F90C. The page table at 8C90F90C is (F5CC,11) (F624,40) (F764,11) (F7BC,12) (F81C,10) (F86C,20):
  the counts sum to 104, and each pointer is the previous one + 8 x count. Every page.entry -> music pair in table 1.6
  was checked by hand against the `.DATA.L` words (all six pages). No music number appears twice. The unlisted
  numbers are 68, 69, 70, 84, 87, 102.
* **8C907B80 reads both halves of the page table.** The count is `@(8C90F910 + page*8)` at 8C907E5C
  (`8C907B80.src:471`). The record pointer is `@(8C90F90C + page*8)` at 8C907F2C-8C907F3A (`:584-589`).
* **ADVERTISE textures and overlay facts.** `ava_sndtest` / `ava_sndtest_e` are at 8C90F388 / 8C90F494
  (`8C90EAF0.src:731-732, 802-803`). ADVERTISE loads at 0x8C900000, with `sha1_bin` 5abb7a8b..., `sha1_prs`
  1166c5d4... and `prs_size` 176,538 (`baserom/overlays.json:1516-1518`).
* **Name table.** 0x8C5FB8A8 + 110 x 4 = 0x8C5FBA60. That word is the flag tested by 8C107AC6 (literal L8C107B88 in
  `1st_read/text/8C107A60.src`), by 8C107C80 and by 8C107DAA.
* **Music state machine (asm read).**
  * 8C107AC6: if flag { AA8 = n; AB0 = n }.
  * 8C107ADA: if handle { 8C644468(h); AB0 = -1 }.
  * 8C107B26: AA8 = AB0 when AB0 != -1.
  * 8C107C80: AA8 = n, then CreateChildTask (8C098CA0) of 8C107CC2 with work[16] = n and work[8] = AB0. 8C107CC2
    frees itself when AB0 has changed; otherwise, at status 0 or 5, it re-requests AB0 (a restart from the
    beginning).
  * 8C10792A calls `ADXT_StartFname(h, name[AA8])` with no "already playing" test, then sets AAC = 8C644500(h).
  * 8C107864 calls 8C64413C(2, *8C5FBA68, 0x2D0C4) and 8C64413C(1, *8C5FBA6C, 0xD884), then 8C6445EA(h, -10) for
    music and 8C6445EA(h, 0) for voice.
  * 8C107D80 creates the task through CreateElementalTask = 8C09852E (`build/1ST_READ.symbols.txt:2478`).
  * 8C107AAA DestroyTasks the music task 8C5FBABC.
* **More evidence for the -1 dB reading (name still UNVERIFIED).** The pause helper 8C107BF2 passes
  (-4 << 8) + 64 = -960 to 8C6445EA for both players, and 8C107C22 restores -10. -960 is the floor of CRI's
  ADXT_SetOutVol range (0.1 dB units, -96 dB).
* **ADXT_StartFname stops first.** ADXT_StartFname 8C644968 calls 8C644468 before it opens the file
  (`1st_read/text/8C644904.src`, literal L8C644B14 = `_func_8C644468`).
* **Other call sites.**
  * Field music 8C04935C: stage 26 -> 76; stages 29-32 -> 31 (8C0493BE-8C0493EC).
  * The 1-up at 8C08A120 plays tone (3 << 8) - 64 = 704 (`8C089F20.src`).
  * The developer sound-test string is at 0x8C159D84 (`1st_read/data/8C159194.src:2094`).
* **The 14 jingle call sites** (`music_calls.json`, call addresses):
  * 26: 8C04D5E6, 8C04E194, STG00 0C903210, MINICART 0C90A926.
  * 44: 8C0E6466. 45: 8C0EB25A. 46: 8C0E88B4.
  * 55: 8C07D4FE.
  * 56: ADV00 0C901050, 0C906F50, 0C90D652, 0C90D694.
  * 85: 8C07D34E. 96: 8C07BFDA.

  None of the scan's dynamic (non-constant) call sites is a jingle.
* **Disc survey, recomputed by `agg.py`.**
  * 110 names, 109 files on the disc, all ADX v3 stereo.
  * 63 x 44100 Hz + 46 x 32000 Hz; 79 loop, 30 do not.
  * 453,511,558 B in total and 10,212.3 s of frames present.
  * Largest data offset 2,096.
  * Only 87 is truncated (6,731,879 -> 5,632,416 frames).
  * The 15 short tracks are exactly the ones listed; the longest intro is TICAL's 54.12 s.
* **Probe log recount (`probe_stats.py`).**
  * 109 files open with OK; 69 gives "not found".
  * 99 of 109 files peak at full scale. The 10 that do not: CASINO4 17652, ECOAST3, EVTBGM00, EVTBGM02, ICECAP2,
    JINGLE_5, LSTWRLD2, NIGHTS_A, NIGHTS_S, TITL_MR2.
  * 12 opens take over 400 ms; the slowest is AMY.ADX at 837.2 ms.
  * 79 of 79 looping files continue across `loop_end`.
  * Decoded frames = frames for all 109.
* **Cost of a second audio object, re-measured** (`probe_mem1.log`). The original `probe_mem.c` measured all four
  configurations in one process, after freeing a 19.9 MB object, so heap reuse could have hidden memory. In fresh
  processes:
  * NO_PRELOAD | NO_VOICES, Sonic: +1.81 MB, 16.8 ms. With SYNC_LOADS: +1.78 MB, 16.6 ms.
  * With a default "session" object already open in the same process: +2.15 / +2.12 MB, 16.9 / 17.6 ms.

  The doc's +1.7-1.8 MB / 17 ms stands; next to G-SA1's own audio object it is +2.1 MB.
* **LIB.**
  * `sa1a_adx_exists` accepts any index below the count (`sa1_audio.c:514-518`).
  * `sa1_audio_stream_open` reads the whole file on the caller's thread (`:1096-1101`) and keeps it (`s->data`,
    freed in `sa1_audio_stream_close`, `:1169`).
  * `sa1a_adx_parse` clamps the frame count to the blocks present (`sa1_audio_codec.c:84-87`).
  * Loop history is saved at the loop-start block and restored on wrap (`:127-130`, `:172-176`). A backward seek
    decodes again from block 0 (`:177-184`).
  * Jingles and background music both use VOL_MUSIC, and background music is skipped while a jingle plays
    (`sa1_audio_mix.c:1366-1369`). Ducking VOL_MUSIC therefore silences both.
  * Lines 572-574, 791, 1059-1080 and 1193-1212 say what the doc says.
* **G-SA1.**
  * `audio.cpp`: `GetModuleHandleW` at :219, the 12 symbols at :228-234, StreamCreate :263, buffer 0 :269,
    `queued_frames` :284-293, `global_stream_volume` :339.
  * `gsa1_module.cpp`: API 5 at :47, AudioUpdate at :1717 and :1744-1756, GMOD_MODULE_CLOSE at :2181. The Lua table
    has no music function (:2035-2085).
  * Lua and other files: `cl_character.lua:286-306, 366-391`, `sh_config.lua:4-5, 134-144`, `cl_menu.lua:273-279`,
    `session.cpp:477/526/625/662`, `sh_net.lua:10`, `sv_gsa1.lua:1`.
* **GMod `sound.Generate`** (wiki, fetched 2026-10-21). It takes only 11025 / 22050 / 44100 Hz. The sample data can
  be a function, a table, or a binary string of 16-bit samples. The channel count is not documented (UNVERIFIED).

### R2. Refuted or corrected

1. **Sound-test call chain** (1.4, section 9, the summary). 8C9019D2 is not the music path.
   * **8C9019D2 is the voice path.** 8C9019D2 (`advertise/text/8C901820.src:252`) creates task 8C90199A (literal
     L8C901A88). That task calls 8C107BA0, the voice request (`:239`, L8C901A7C).
   * **8C901940 is the music entry** (`:174`):
     * It first calls 8C901874, which destroys the pending request tasks 8C9610D4 / 8C9610D0 and tail-calls
       8C107AAA. 8C107AAA destroys the music task, and with it both ADXT players (destructor 8C107A60 ->
       8C6442F2).
     * Then 8C107D80 creates a new music task.
     * Then task 8C9018B0 (`:120`) calls 8C107AC6(work[0]) on its second frame when *8C9610D8 is set (`:137`).
   * **Why they were confused.** The literal pool L8C901A60.. is shared by 8C901940 and 8C9019D2.
   * **The page code does pass the record's number** (`8C907B80.src:569-597`, 8C907F12-8C907F4A). When 8C900D60() != 0
     (a button test; which button is UNVERIFIED) and *8C9610D8 == 1:
     * R4 = `@(4, @(8C90F90C + page*8) + entry*8)`, where page = work+56 and entry = byte work+54;
     * then `JMP 8C901940`.
   * **Stop and entry.** The stop test (8C900D70) calls 8C9018A4 -> 8C107ADA (`:616`). Entering the page calls
     8C901940(-1) (8C907D2A, `:305`), which re-creates the music system and plays nothing.
   * **Result.** The doc's UNVERIFIED point is settled, and its conclusion stands: the sound test uses the ordinary
     background-music path, and tracks loop at the ADX header loop. The function names must be fixed.
2. **"Repeat the track (like the game's sound test)" is wrong for non-looping files.** The game's sound test plays a
   non-looping file once and stops: ADXT ends at PLAYEND, and `8C907B80.src` never reads the music status. The status
   getters 8C107AF6 and 8C9018AA are referenced only from `8C901820.src`, `8C908B40.src`, `8C909560.src` and
   `8C90CEC0.src`. A "forever" plan that restarts a non-looping file at 0 is a jukebox choice; 6.3 and 6.5 should
   say so.
3. **TITLE2 (102) is used by the game.**
   * **The table.** ADVERTISE's menu music table 8C90EE54 = {102, 101, 72, 24, 103} (`8C90EAF0.src:625`) is indexed
     by 8C90198E -> 8C901940.
   * **The call.** 8C90198E(0) is called at 8C908CCA (`8C908B40.src:298`). The same code also reads the music
     status (8C9018AA).
   * **Why the scan missed it.** `music_calls.py` resolves only a constant R4 at direct calls of the music
     functions. Its "never a constant argument" list therefore wrongly suggests that TITLE (101), OPTION (72),
     TRIAL (103) and TITLE2 (102) are unused.
   * **All table calls** (`menu_music.py`): index 0 at 8C908CCA; 1 at 8C90973A; 2 at 8C90437A, 8C904670, 8C9067E4,
     8C906C7C, 8C90ADA0 and 8C90B1CA; 3 at 8C90324A; 4 at 8C90589C, 8C90A070 and 8C90CA66.
   * **Fix.** 1.5 and question 1 should call TITLE2 a menu piece that plays before TITLE; which screen is
     UNVERIFIED. For 68, 70 and 84 no reference was found here either, but table-driven use elsewhere is not ruled
     out (UNVERIFIED).
4. **Smallest file.** "Sizes 156,653 B (FISHMISS)" gives FISHMISS's frame count. Its size is 176,310 B.
5. **Units.** "432.5 MB" is 432.5 MiB = 453.5 MB (453,511,558 B). The MB column of table 1.6 is also in MiB.
6. **Loop tail.** "At or within 0.23 s of the end" should be "within 0.264 s": CHARACTR.ADX has 8,443 frames at
   32 kHz after `loop_end`.
7. **"The game's curve ... near = 0.1 x radius"** (6.3). The game's distances are SA1A_NEAR 20 and SA1A_FAR 600
   (`libsa1/src/sa1_audio.h:29-30`), so near = far / 30, and sound is silent from 0.840 x far, not 0.851. The CC7
   shape is the game's; 0.1 is a new parameter. Either use far / 30 or call 0.1 a G-SA1 choice.
8. **The J0 test "a MUSIC cue for 69 returns NOT_FOUND and leaves the current music playing" is not achievable as
   planned, and it is not the game's behaviour.**
   * **Not achievable.** `sa1_audio_mixer_cue` sets `bgm_index` / `bgm_set` and fades the background music
     (`sa1_audio_mix.c:1060-1064`) before `start_clip` -> `sa1a_clip_get` -> `sa1a_adx_exists`
     (`sa1_audio_load.c:430`). Keeping the music would need an edit to `sa1_audio_mix.c`, which J0 does not list.
   * **Not the game's behaviour.** 8C107AC6(69) -> 8C10792A -> ADXT_StartFname stops the current track (8C644468)
     before it fails to open the file, so the game goes silent. Today's LIB (fade, then nothing) already matches the
     game; only the status code is wrong.
   * **Correct test.** The cue returns SA1_ERR_NOT_FOUND synchronously, `read_errors` does not grow, and the music
     stops as in the game.
9. **The server range check "lengths 0..600 s"** (6.6) contradicts `gsa1_jukebox_loops` 1..10 and fade 0..10. Plan
   lengths (loop_start + n loops + 10 s fade) pass 600 s for:
   * 1 track at n = 3 (REDMNTN2, 826.5 s + fade);
   * 2 tracks at n = 4;
   * 57 of the 79 looping tracks at n = 10 (the longest is REDMNTN2 at 2,735 s).

   Use 3,600 s, or compute the limit from the ConVar ranges.

### R3. Gaps

* **G1 - Who owns the BASS handles.** The Lua thread sets VOL / PAN every frame, while the worker creates and frees
  streams on start, seek and track change and runs the old stream's 50 ms fade-out. Without a rule:
  * the Lua thread can slide a freed or fading handle and cancel the fade;
  * a new stream can play at volume 1 until the next Lua frame.

  Rule needed: the Lua thread only posts target gain and pan (atomics or the mailbox). The worker owns every BASS
  handle and applies attributes; it wakes every 20 ms, and 100 ms slides hide that delay. Each new stream gets the
  current gain and pan before its first BASS_ChannelPlay.
* **G2 - Blocking opens vs the 1.5 s queue.** One worker does every `stream_open`. Two cold opens back to back (a
  prefetch plus a world track, or two world jukeboxes starting together) take 2 x 837 ms, more than 1.5 s, so a
  channel underruns. On a slower drive, one open can be enough.
  * **Scheduling.** Before each blocking open, top up every channel to the full queue. Do at most one open per wake.
    Prefetch only when all channels are full.
  * **Queue size.** Raise the queue to 3 s. A seek or track change creates a new stream, so queue depth adds no
    latency to any control; the cost is about 0.5 MB per channel.
  * **Better (J0 option).** A lazy stream open that reads ADX blocks on demand. Today's decoder needs the whole buffer
    (`sa1a_adx_dec.d`). A lazy open removes both the block and the 15 MB per channel.
* **G3 - Start-time anchoring for world sync.** `JukeboxPlay(slot, Track, CurTime() - StartTime)` is computed
  before the open. A cold open (up to 0.84 s) makes each client start late by its own open time. JukeboxSync then
  seeks within about 1 s, a second audible jump. JukeboxPlay should take the plan time together with a timestamp
  from the module's monotonic clock. The worker then computes the start frame when it begins pushing:
  t0 + time elapsed since the call.
* **G4 - The slow-disc test is ill-defined.** LIB reads 64 sectors per host read (`sa1_disc.c:17`, `:290-292`), so a
  15 MB file takes about 118 `file_read` calls. A callback that sleeps 1 s per call would take two minutes. Specify a
  total added delay of 1.2 s per open (about 10 ms per call), assert that no other channel underruns, and add a case
  with two opens in a row.
* **G5 - Disc path.** The jukebox should work without a character and before `gsa1.Init`: the owner may only want
  music. Take the path from `gsa1::load_config` as Init does (`gsa1_module.cpp:339-343`), not from Session.
* **G6 - API version pair.** `cl_module.lua:53` refuses a module unless `gsa1.API_VERSION == GSA1.REQUIRED_API`
  (`sh_config.lua:5`). J1 bumps the module to 6, but `sh_config.lua` belongs to J2, so J1 on its own would be refused
  by the addon. Bump both in one change, and use "current + 1": round 4b may bump the version too.
* **G7 - Round 4b overlap.** J1 moves the BASS binding out of `audio.cpp` and adds `set_duck` (`audio.hpp/.cpp`,
  `gsa1_module.cpp`). J2 edits `cl_character.lua`, `sh_config.lua` and `cl_menu.lua`. All of these are under
  `g-sa1/**`, which round 4b is editing now, so J1 and J2 need the same "after round 4b" condition J0 has. J0's
  "header version 3" can also clash, because 4b edits the audio loader and may bump SA1_AUDIO_HEADER_VERSION itself:
  use "the next version after 4b".
* **G8 - Reference level.** The game plays music at 8C6445EA(h, -10). That is -1.0 dB if the unit is 0.1 dB, which
  the -960 pause value supports. LIB's mixer and the proposed jukebox play at 0 dB. Decide whether jukebox volume 1
  means the game's level (gain 0.891). That also gives 1 dB of headroom for the 99 files that peak at full scale.
* **G9 - Music volume argument.** 6.3 multiplies by `snd_musicvolume`, but the `JukeboxUpdate` signature in 6.4 has
  no argument for it. Add one, or pass it through JukeboxVolume.
* **G10 - BASS failure and shutdown.**
  * **Recovery.** If GMod frees and re-initialises BASS (for example on a device change; UNVERIFIED that it does),
    BASS_StreamPutData fails. Today's AudioOut still reports `running()` after a failed push (`audio.cpp:303-305`).
    The engine should recreate its streams on a handle error and report it in JukeboxStatus.
  * **Shutdown.** GMOD_MODULE_CLOSE joins the worker, which may be inside a `stream_open` that cannot be cancelled
    (up to 0.84 s). That is acceptable but should be documented.
* **G11 - Sandbox details for J3** (UNVERIFIED in game).
  * **Spawn limit.** `sbox_maxgsa1_jukeboxes` is not automatic: Sandbox's default PlayerSpawnSENT checks only the
    `sents` limit. J3 needs a CreateConVar, plus `ply:CheckLimit("gsa1_jukeboxes")` / AddCount in a PlayerSpawnSENT
    hook.
  * **gm_spawnsent.** It exists only in Sandbox-derived gamemodes.
  * **String NetworkVar.** The length limit (believed to be 511) is UNVERIFIED; the Playlist needs at most 436
    characters.
* **G12 - Clock edge cases** (UNVERIFIED in game).
  * **Timescale.** CurTime follows `host_timescale`, so under a timescale other than 1 Sync would seek every second.
  * **Precision.** CurTime is a float in the engine. On a long-running server it loses precision: 1 unit in the last
    place is 0.0625 s after about 6 days and 0.125 s after about 12 days.

  Suppress Sync while CurTime advances at a rate different from RealTime.
* **G13 - Scope.** `HANDOFF.md` item 4c asks for "track list, play/stop, volume; Options tab". J0-J2 deliver that.
  J3 (a world entity with multiplayer sync) was not requested. G-SA1 has no multiplayer yet (`sh_net.lua:5`), and
  whether the owner can test with a second client is UNVERIFIED. Make J3 an owner question (add it to section 8)
  rather than part of the default plan.
* **G14 - Missing tests.**
  * **Lua.** Run `g-sa1/tools/lua_check.py` (the existing offline syntax and global checker) on the new Lua files.
    No standalone Lua is available on this machine: no `lua`, `luajit` or `lua5.1` on PATH and no `lupa` for
    py -3.12. So `jukebox_logic_test.lua` can only run in game unless a small Python harness is written.
  * **Bindings.** `binding_test` should check API 6 and the API / REQUIRED_API pair.
  * **Engine.** `jukebox_test` should cover handle ownership (G1) and late-start anchoring (G3).
  * **J0.** "At most 2 sectors per info" must exclude the first call, which also reads ADVERTISE (87 sectors) and
    the directory. Count after a warm-up call.
* **G15 - Track list at start.** Reading all 110 infos at engine start means 109 random 2-sector reads: about 1 s on
  a cold hard disk (UNVERIFIED). That is fine on the worker, but the list shows "loading" meanwhile. J0 should keep
  ADVERTISE's 937 KB decompressed buffer only until the title strings (about 3 KB) are copied.
* **G16 - Map change** (UNVERIFIED). The client Lua state, and with it the module (GMOD_MODULE_CLOSE), is torn down
  on a map change or disconnect, so the personal jukebox stops. If it should resume, remember the last track and
  position in a ConVar; this is an owner question.

### R4. Required changes to the work plan

* **Text.**
  * Fix the sound-test chain in 1.4, section 9 and the summary (R2.1).
  * Correct what 1.5 and question 1 say about TITLE2 (R2.3).
  * In 1.1, fix the smallest size, the MiB units and the loop tail (R2.4-6).
  * In 6.3, fix the near distance (R2.7).
  * In 6.5, drop "like the game" for non-looping tracks (R2.2).
* **J0.**
  * Replace the test for 69 (R2.8).
  * Compute ON_DISC once at open, as a bitmap from the SONICADV directory, so `sa1a_adx_exists` stays O(1) on the
    mixer's thread.
  * Header version: the next one after round 4b.
  * Run the 2-sector test after a warm-up call.
  * Optional: a lazy stream open (G2).
* **J1.** Add:
  * handle ownership (G1);
  * open scheduling and a 3 s queue (G2);
  * start anchoring (G3);
  * the disc path from the config (G5);
  * the module API and the addon's REQUIRED_API bumped together (G6);
  * the "after round 4b" condition (G7);
  * BASS error recovery (G10);
  * the music-volume argument (G9);
  * a choice of reference level (G8);
  * a well-defined slow-disc test (G4).
* **J2.**
  * Add `lua_check.py`.
  * Ship REQUIRED_API with J1.
  * Start after round 4b.
* **J3.**
  * Ask the owner first (G13).
  * Use a 3,600 s length limit (R2.9).
  * Add the Sandbox limit hook (G11).
  * Suppress Sync under a timescale other than 1 (G12).
