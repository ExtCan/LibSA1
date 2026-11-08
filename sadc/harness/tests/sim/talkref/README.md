# talkref: the talk references made at test time (G-SA1 1.0.0, prep lane p-refs-T)

Two references of the game's talking, made from the owner's disc each time they are needed and never committed. They are
the test-time halves of R5b's `text_test` (test T1) and `talk_test` / `feat_talk_test.lua` (tests T4 and T6), designed
in `docs/research/gsa1_100_npc.md` 9 and 15.2 and planned in `docs/research/gsa1_100_master.md` R0 ("Later prep") and R5b.

| File | What it is |
|---|---|
| `talk_pages.py` | Reference 1: every page of every talk file, typeset by the original code |
| `talk_ss.py` | Reference 2: real talks on the original game in Station Square |
| `talkref_drv.c` | The driver `talk_pages.py` links at test time with the harness's interpreter core (`harness/src`), in place of `main.c` |

**The owner's rules hold for both.** The game's bytes are read from `baserom/` and `disc/` at run time only. The outputs
hold numbers, names and SHA-1 only: no byte of the game's text, no glyph and no picture. They are written only under the
project's `build/` folder (the scripts refuse any other `--out`), which `repos/sync_repos.py` never enters (`SKIP_DIRS`
holds `build`). Never copy an output into a repository or a test reference.

## Reference 1: `talk_pages.py`, every page typeset by the original code

```
py -3.12 harness/tests/sim/talkref/talk_pages.py                     # every page, every language (about 2 minutes)
py -3.12 harness/tests/sim/talkref/talk_pages.py --files SS_MES_S_E.BIN
options: --disc DIR (disc/SONICADV), --rom FILE (baserom/1ST_READ.BIN), --out FILE (build/traces/talkref/pages.json),
         --work DIR (build/scratch/talkref/drv), --budget N (5,000,000 instructions per page), --recheck N (64)
```

* **What runs.** For each talk file `disc/SONICADV/*_MES_*.BIN` (99 on the owner's disc: Station Square `SS_`, Mystic
  Ruins `MR_`, the Past `PAST_`; the last letter is the language, J E F S G = font table rows 0-4), the driver loads the
  owner's `1ST_READ.BIN` at 8C010000 and the talk file where the game's loaders put it (0CB46000 for Station Square,
  0CB4A000 for the others; each file's table pointer is checked against it). For each page it starts again from that
  state, sets `B_8C754BFC` (the language) to the file's row and runs the game's **8C055794** (NH_MSG_Open with the
  runner's numbers: x 70, y 384, w 500, h 52, texture 100, buffer `B_8C87E900`; the first-page path of the talk runner's
  8C05F70A) and **8C054F2E** (the text routine) with the window structure `B_8C78C518` and the page's own pointer from the
  file. The 512 x 64 x 2-byte buffer and the 0x4000 bytes after it come back through a pipe; the script hashes them in
  memory.
* **The walk of a file** follows the game's format (`docs/research/v1_npcs.md` 1.4): the entries' `{condition, messages}`
  table, as many groups as the condition has -2 opcodes plus one, each a NULL-ended list of page pointers. A list that
  runs to the end of the file (one, `PAST_MES_M_S.BIN` entry 4) ends there, as the word past the file reads 0 in a clean
  load buffer (UNVERIFIED in the game); the file's `notes` say so.
* **The output** (`pages.json`): `rom_sha1`; `window` (the structure's fields after the open); per file its size, SHA-1,
  row, load address, entry and page counts and notes; per page:

| Field | Meaning |
|---|---|
| `file`, `row`, `entry`, `group`, `page`, `ptr` | which page: the file, the font row (0 J, 1 E, 2 F, 3 S, 4 G), its place in the file's lists, its pointer |
| `off`, `len`, `input_sha1` | the page's bytes in the file (from `off`, `len` bytes, the NUL not counted) and their SHA-1: a test reads the page from its own disc and checks it has the same bytes |
| `first`, `align` | the first byte; `align` is it when 0x07 (the block centred, 8C0550FC) or 0x09 (each line centred, 8C055050), else 0 (8C054F2E types from the first byte) |
| `breaks`, `inner_controls` | line breaks (0x0A) in the page; control bytes inside the page other than those (a second 0x07; only when not 0) |
| `status`, `icount` | 0, or the interpreter's fault kind; the instructions both calls took |
| `cursor` | the text routine's cursor x, y at the end (window structure +0x0E, +0x10) |
| `buffer_sha1` | **the SHA-1 of the 0x10000-byte buffer**: 512 x 64 pixels of 16 bits, little-endian, rows from the top: what `text_test` compares |
| `set_pixels`, `rows`, `cols`, `values` | pixels not 0; the first and last row and column holding one; the distinct pixel values |
| `past_buffer_bytes` (`past_buffer_sha1`) | bytes not 0 in the 0x4000 bytes after the buffer (and their SHA-1 when there are any) |

* **For `text_test`** (`gsa1_100_npc.md` 8 T1, 15.10): read each page's `len` bytes at `off` from the same disc file,
  check `input_sha1`, give `sa1_text_render` the row, `align` and the bytes after the first when `align` is not 0 (all of
  them when it is 0), and compare the SHA-1 of its 512 x 64 ARGB4444 buffer with `buffer_sha1`. Pages `sa1_text_render`
  refuses by its bounds (three lines, a line over 500 pixels) are compared as refusals; the summary counts them
  (`third_line`, `beyond_500px`).

## Reference 2: `talk_ss.py`, real talks on the original game in Station Square

```
py -3.12 harness/tests/sim/talkref/talk_ss.py                        # every case (about a minute)
py -3.12 harness/tests/sim/talkref/talk_ss.py --cases talk edge_in edge_out
options: --exe EXE (harness/build/sa1h.exe), --out FILE (build/traces/talkref/talk_ss.json),
         --work DIR (build/scratch/talkref/ss), --keep (keep the traces and logs)
```

* **What runs.** The stock harness's `trace` at level 26 act 0 with Sonic (he starts at 270, 4, 277, facing 0x4000;
  checked on every run), and one townsperson made as the SET spawner 8C091380 makes an object:
  `--create-task 0C90F35E:2:3@X,Y,Z,0,ANGY,0/0,0,0+ocp+late` (objexec_O_PEOPLE with IM 2 and list 3 from ADV00's object
  list entry at 0CA5E130; `ang.x` is the type, `ang.y` its facing, `scl.z` its message number in `SS_MES_S_E.BIN`).
  Everything else is the game's own code: the talker registers with the talk manager 8C10A146, the manager 8C10A28E picks
  it, 8C0509DA turns the ports off, 8C07256A stops Sonic, 8C07C318 turns his head, the runner opens the window and types
  the pages, A turns them, the end turns the ports on (8C0509B8).
* **What is recorded** per case: `--dump-world` of the talk manager (`D_8C5FBAC0`, `D_8C5FBAC4`), the ports
  (`D_8C19E3A8`-`AB`), the message state (`B_8C78C508`-`517`, `D_8C1BA1E0`-`E7`), the window structure `B_8C78C518`
  (+0x1F the band's alpha, +0x1B the text's), `per[0]` and the sub-step `B_8C754EC8`; `--log-sdk` of 8C0509DA, 8C0509B8,
  8C07256A, 8C055794, 8C054F2E, 8C055C22, 8C07C318, 8C07C376 (none of them yields to the event threads, which a hooked
  call must not); `--log-2d` (the band's njDrawPolygon2D from 8C054AF0 and its alpha); a RAM image of step 30, read for
  the person's node (radius, flags, place, angle) and Sonic's `p.rad` (playerwk +0x10C) and deleted at once.
* **The cases.** `talk` (a whole talk: B, then one A per page, the page count taken from a first run with twelve A
  presses); `edge_in` / `edge_out` (26.9 and 27.1 units: a man's 23 + p.rad 4), `v1_edge` (30.9: inside
  `v1_npcs.md` 1.3's 7 + 20 + 4), `fat_in` / `fat_out` (a fat man, motion set 4: 27.9 and 28.1), `behind` (the facing
  test 0x8000 off), `side_in` / `side_out` (14500 and 14600 off: the +-80 degrees are 14563), `above_*` / `below_*`
  (9.9 and 10.1 units), `button_a` / `_x` / `_y`, `walk_up` (Sonic walks up and presses B at the first step inside 27),
  `walk_early` (B at the last step outside 28.5).
* **The output** (`talk_ss.json`): per case `talk`, `pick_step`, the `node`, `p_rad`, the distances and the facing test
  as the game computes them, `calls` (the step of every logged call), `changes` (a row per step where the pick, a port
  or an alpha changes: step, picked, port 0, port 1, band alpha, text alpha), `band_draws` (step, alpha); for `talk` also
  `phases`: the open, each page turn and the close in steps, with the alphas step by step. `agrees` says whether every
  case came out as `gsa1_100_npc.md` 15.2 reads the code.

## What the first runs measured (2026-11-08, the owner's disc and the stock harness of that day)

* `talk_pages.py`: 99 files, 10,067 pages (E 2,095, F 2,091, S 2,098, G 2,013, J 1,770), no fault, 8,282 distinct
  buffers, the recheck of 64 pages identical, 130 s. Seven pages draw on a third line (11 end with the cursor on it,
  y 56): the text routine stops at the buffer's last row (8C054F00-8C054F0A compare the row with the texture height,
  READ), so nothing is written past the buffer on any page (MEASURED), against `v1_npcs.md` review R2.4. Eight pages
  reach column 500 or beyond. Three pages do not start with 0x07. 10,050 of the pages equal the research
  re-implementation `build/research/npcs/msg_render.py` byte for byte; the 17 others each hold a second 0x07 inside the
  page or a line of 494 pixels or more (in that script's widths), which it simplifies.
* `talk_ss.py`: all 18 cases as 15.2 reads the code. A man's node radius is 23.0 and a fat man's 24.0, Sonic's p.rad
  4.0: talk at 26.9, none at 27.1 and 30.9 (so 23 + 4 = 27 units, not 31), and at 27.9 / 28.1 for the fat man. The
  pick, the ports off, the speed cleared and the head turn come in the step of the press; the window opens 3 steps later;
  open 21 steps (the band +12 a step to 120, then 128 with the text +24 a step to 240, then 255); a page turn 14 steps
  from the A press (the text -32 x 7 to 31, the next page typed, +32 x 7 to 255: the text's alpha never reaches 0, so
  with the constant vertex colours of 8C054A00 the turn is not a fade); the close 23 steps from the last A (the text
  -24 x 10 then 0, the band -12 x 10 then 0, 2 more steps), then the ports on, the look off and the pick cleared in one
  step. The band is drawn on the even steps only (sub-step 0). Walking up: B at 26.66 units talks (Sonic's place after
  the step before: the manager runs before the player), the speed 0.75 goes to 0 in that step and Sonic does not move.
* In the harness the text's textured quad is never drawn: the window's texture handle (+0x28) stays 0 (the upload of
  8C055C4C never runs there), so only the band's draws are logged (MEASURED); the text's on and off steps come from its
  alpha.