# Sonic Adventure (DC, US 1.005): sound effects, voices and music

Research for libsa1's audio layer (`libsa1/include/libsa1_audio.h`, `libsa1/src/sa1_audio*.c`). It completes
section 8 of `sa1_assets.md` (which left the bank layout and the SE-id mapping open). Everything below was
read from the sound code of `1ST_READ.BIN` (8C092FA0-8C0957CE, 8C107864-8C107DAA) and checked against the
disc's files by decoding every bank (`libsa1/tests/audio_test.c`).

## 1. Where the sounds live

| What | File(s) on the disc | Loaded by |
|---|---|---|
| common SFX (bank 0: 40 programs, bank 2: 24) | any stage MLT (byte-identical except CASINOPOLIS bank 2 and ALIFE) | 8C09340C per stage |
| stage SFX (banks 1, 4, 5) | the stage's MLT | 8C09340C |
| player SFX (bank 3) | `P_SONICTAILS.MLT` (Sonic, Tails, Tikal, Eggman), `P_KNUCKLES`, `P_AMY`, `P_E102`, `P_BIG` | 8C09340C by `GetPlayerNumber()` |
| short voice SFX (bank 6) | `V_<c>_E.MLT` (English) or `V_<c>.MLT` (Japanese) by the byte at `0x8C754C00` (0 = Japanese) | 8C09340C |
| voice clips (ADX, mono 22050 Hz) | `EVENT_ADX_US.AFS` / `EVENT_ADX.AFS`, 2,044 entries: entry = voice number | 8C107864 (`ADXT_StartAfs`) |
| music and jingles (ADX, stereo 32/44.1 kHz) | `SONICADV/<name>.ADX`, name table `0x8C5FB8A8` (110 entries: 55 INVNCIBL, 71 ONE_UP, 85 SPEEDUP, 96 TIMER, 44 FISHGET, 46 FISHMISS, ...) | 8C10792A (`ADXT_StartFname`) |

Stage number (`GetStageNumber() >> 8`) to MLT (8C09340C's 43-case switch): 0 EGGCARRIER, 1 EMERALDCOAST,
2 WINDYVALLEY, 3 TWINKLEPARK, 4 HIGHWAY, 5 DRYMOUNTAIN, 6 SKYDECK (`SKYDECK_E` in English), 7 LOSTWORLD,
8 ICECAP, 9 CASINOPOLIS (+ `CASINO_<c>[VOICE[_E]]`), 10 FINALEGG, 12 HOTSHELTER, 15-19 CHAOS0/2/4/6/7B,
20-22 MOBILE1-3, 24 E101, 25 E101R, 26-28 STATIONSQUARE, 29-32 EGGCARRIER, 33-34 MYSTICRUIN, 35 CART,
36-37 SHOOTING, 38 SANDHILL, 39-42 ALIFE (+ `ALIFE_<c>[_E]`); 11, 13, 14, 23: DRYMOUNTAIN (the default).

## 2. Containers

**SMLT**: `"SMLT"`, u32 0x101, u32 entry count (21), header 0x20; entries of 0x20 bytes: magic (`SMPB`, `SMSB`,
`SFPB`/`SFOB`/`SFPW`), u32 bank, u32 AICA RAM address, u32 RAM size, u32 file offset (0xFFFFFFFF = not in this
file), u32 size, 8 zero bytes.

**SMPB** (program bank): `"SMPB"`, u32 1, u32 size, u32 0, then four `{u32 offset, u32 count}` pairs: the
program table (u32 offsets), velocity curves (128 bytes each), two small tables. Program: u32 layer
offsets[4] (0 = none) + 8 bytes. Layer header (0x10): u32 split count, u32 split offset, u32 delay (0 for
almost all; 2-225 in a few echo layers: read as ms, unverified), u8 bend range up, u8 bend range down
(2/2), 2 bytes. Split (0x30) = AICA channel registers 0x00-0x2A as the driver writes them, then the note
fields:

| Off | Field |
|---|---|
| 00 | u16: SA[22:16] bits 0-6, PCMS bits 7-8 (0 PCM16, 1 PCM8, 2 ADPCM, 3 ADPCM long stream), LPCTL bit 9 |
| 02 / 04 / 06 | SA[15:0] (sample start, offset in the SMPB) / LSA loop start / LEA end (samples) |
| 08 | AR (0-4), D1R (6-10), D2R (11-15) |
| 0A | RR (0-4), DL (5-9), KRS (10-13), LPSLNK (14) |
| 0C | OCT/FNS (0 in every split of the disc) |
| 0E | LFO (LFORE, LFOF, PLFOWS, PLFOS, ALFOWS, ALFOS) |
| 10 | ISEL / IMXL (effect send) |
| 12 | DIPAN (0-4), DISDL (8-11) |
| 14 | Q (0-4), LPOFF (5), VOFF (6), TL (8-15) |
| 16-1F | FLV0-4 (filter levels); 20-23 FAR, FD1R, FD2R, FRR |
| 24 / 25 | low / high note of the split |
| 26 / 27 | base note / fine tune (s8, 1/256 semitone) |
| 28-2F | 0xFFFF, two small bytes, 0x7F, 0 |

The sample plays at `44100 * 2^((note - base + fine / 256) / 12)` Hz: sound effects are note 60, so base 72 =
22050 Hz (most player sounds), 84 = 11025, 78 + 116/256 = 16000 (voice SFX), 66 + 116/256 = 32000. The data is
Yamaha/AICA 4-bit ADPCM (low nibble first; step 0x7F..0x6000, scale 230/230/230/230/307/409/512/614 / 256),
PCM8 or PCM16. A few programs are 4-9 zero samples (placeholders).

**SMSB** (sequence bank): `"SMSB"`, u32 version, u32 size, u32 count, u32 offsets[count]; each `"SMSD"`, u32 0x88,
u32 tempo, then events each followed by its delta time: notes 0x00-0x7F (channel = low nibble; note, velocity,
gate 1 byte or 2 BE if bit 5, delta 1 byte or 2 BE if bit 4), 0xBn control (controller; bit 7 = 2-byte delta),
0xCn program, 0xEn pitch bend (7 bit), 0x81 reference (u16 offset, u8 count), 0x82 rest, 0x83 end, 0x84 tempo
(u16), 0x88-0x8F rare extension commands. Parsed cleanly in 5,584 of the 5,596 sequences of the disc (the rest
use 0x81 / 0x88-0x8F). Timing (an assumption, not verified): 480 ticks per quarter note, tempo = ms per quarter
(800 in most sequences).

## 3. From the game's SE number ("tone") to a sound

* Play functions (gen/policy/stubs.tsv "event" lines) fill a 38-entry queue at `0x8C79E40C` (0x34 bytes: owner,
  timer, id, flags, tone, pan, volofs, max volume, pitch, position): 8C0940DA `(tone, id, pri, volofs)` timer
  120 frames; 8C09413A `+ x, y, z` positional (only within the tone's 3D radius, table `0x8C1C1BB4` /
  `0x8C1C1BFE`: 35 tones, 100 or 200 units); 8C0941FC `(tone, task, pri, volofs)` until stopped; 8C094330
  `(tone, task, pri, volofs, timer)` and 8C0943E0 (positional) refresh an existing entry of the same tone and
  owner every frame (keep-alive); 8C0945A0 `(tone)` and 8C0945D0 `(task)` stop. 8C04EF22 plays a tone 5 frames
  later (a task).
* The sound task (8C0948A0, once per main-loop frame) maps the tone through `{s32 threshold, s32 bank}` at
  `0x8C1C19B4` (63 entries, descending): the first `tone > threshold` gives the bank and
  `request = tone - threshold - 1`; no match = bank 0, request = tone. Tones above 1274 go to the voice player
  as voice `tone + 455`.
* Character ranges: bank 3 thresholds 722 SONICTAILS (14 programs), 736 KNUCKLES, 751 AMY, 776 E102, 795 BIG;
  bank 6 thresholds 1111 SONICTAILS (34), 1144 KNUCKLES, 1168 AMY, 1186 E102, 1199 BIG, 1214 the event voice
  banks (`E_xxxx.MLT`). Banks 1 and 5 are split into per-stage ranges.
* Normal tones (slots 0-31): MIDI port messages: CC32 bank, program change `request`, CC10 pan 64 (bank 0
  request 7, the ring, alternates 34 / 94), pitch bend centre, CC1 127, note on 60 velocity 127; CC7 volume =
  `106 + volofs` (`volofs / 6` when positive) clamped 0..127; positional entries subtract
  `(d - 20) * 0.00172 * 127` (d = distance from the camera, silent at 600). Note off when the timer runs out.
* 3D slot tones (8C093F02: 66, 67, 124, 160, 161, 239, 553, 554, 667, 777-781 (E-102), 1027, 1029, 1039, 1059,
  1080, 1215-1222, 1264-1271, 1273): sequence `request` of the bank's SMSB through another driver call
  (8C643B08); instance copies use alternate tones (8C095402, not reproduced).
* 8C095612 remaps banks 3/6 to 4/1 for some tone ranges when the `ALT_*.MLT` banks are loaded (scenes with two
  characters); not reproduced.

## 4. Music state (8C107xxx)

8C107AC6 `(n)`: background music n (also the saved track); 8C107C80 `(n)`: jingle n now (invincibility 55,
speed up 85, drowning countdown 96, fishing 44/46); 8C107B26: back to the saved track; 8C107ADA: stop;
8C107D80: music system init (stage start); 8C04EF52 `(n)`: 8C107AC6 five frames later.

More (research for the jukebox, `jukebox.md` sections 1.2-1.5 and its Review R1 / R2; in libsa1 since audio header
version 3: `sa1_audio_music_info`):

* **Playback.** Every frame 8C10792A starts a pending request with `ADXT_StartFname(h, name[n])` (8C644968), which
  stops the current track (8C644468) before it opens the file. A track with a header loop loops for as long as nothing
  else is requested; there is no loop code in 8C107xxx.
* **Jingles.** 8C107C80 requests the track without saving it and starts task 8C107CC2: when the music status is 0 or 5
  (stopped / play end) and the saved track has not changed, it requests the saved track again, so the background music
  restarts from its beginning (libsa1's mixer resumes it where it paused instead). Every call of 8C107C80 has a
  constant argument (`sadc/build/research/jukebox/music_calls.py`): 26 CONTINUE (8C04CA40, 8C04DA20, STG00 0C903140,
  MINICART 0C90A81E), 44 FISHGET (8C0E6446), 45 FISHING (8C0EB244), 46 FISHMISS (8C0E8562), 55 INVNCIBL (8C07D4A0),
  56 ITEM1 (ADV00 0C900E70, 0C906E80, 0C90D480), 85 SPEEDUP (8C07D2DA), 96 TIMER (8C07BE6E; the drowning countdown
  stops the music with 8C107ADA first). This set is libsa1's `SA1_AUDIO_MUSIC_JINGLE`. The 1-up is tone 704
  (8C08A120), not ONE_UP.ADX (71), which no code requests with a constant.
* **The sound test.** The menu overlay `ADVERTISE.PRS` (load address 0x8C900000, decompressed SHA-1
  5abb7a8b0d85e90b4ee9ee6815a5ad10ca5a81e7) holds 104 records `{char *title; s32 music}` from 0x8C90F5CC and a page
  table of six `{record *, count}` pairs at 0x8C90F90C: 11 / 40 / 11 / 12 / 10 / 20 (vocal songs and themes, action
  stages and bosses, fields and mini-games, events and appearances, menus and tutorials, jingles). The menu code
  8C907B80 reads both halves and passes the chosen record's music to 8C901940, which re-creates the music task
  (8C901874 -> 8C107AAA, then 8C107D80) and requests the track through task 8C9018B0 -> 8C107AC6: the ordinary
  background-music path, so a track loops at its header loop and a track without one plays once. (8C9019D2, which shares
  the literal pool, is the voice path.) Titles are ASCII with two spaces after a colon ("Emerald Coast:  Type 1").
* **69 NIGHTS_K.ADX** is in the name table but not on the disc; the game goes silent if it is requested (the stop
  comes first). libsa1 knows which files are on the disc from the SONICADV directory at open: a stream or a MUSIC /
  JINGLE cue for 69 is `SA1_ERR_NOT_FOUND` at once, and the cue stops the music like the game.
* **87 SSRACING.ADX** is truncated on the disc: the header says 6,731,879 frames (3:30.4), the file holds 5,632,416
  (2:56.0); libsa1 plays the frames present (`SA1_AUDIO_MUSIC_TRUNCATED`).
* **Not in the sound test:** 68 NIGHTS_A, 70 NIGHTS_S, 84 SONIC_CD, 87 SSRACING, 102 TITLE2 (plus the absent 69).
  TITLE2 is a menu piece: ADVERTISE's menu music table 8C90EE54 = {102, 101, 72, 24, 103}, played through 8C90198E ->
  8C901940 (index 0 at 8C908CCA; which screen is UNVERIFIED).

## 5. Open points

Tempo and layer-delay units; the driver's CC7 / velocity to TL curves (libsa1 uses `(cc / 106)^2` and
`(curve[vel] / 127)^2`); 0x81 and 0x88-0x8F sequence commands; LFO, filter and DSP effects (reported as layer
flags, not rendered); whether the sound task runs per 60 Hz step (timer 120 = 2 s, assumed) or per frame pair.
