# Sonic Adventure (DC) character assets for a libsa1 / G-SA1 port

What a G64-style port must rip from the user's own GD-ROM dump at run time, where each asset
lives, in which format, and how to decode it. Everything below was measured on the user's dump
(`Sonic Adventure v1.005 (1999)(Sega)(US)(M5)[!][%51000-A].gdi`) unless marked otherwise.

* Companion table: `docs/research/sa1_assets.csv` (924 rows: every model, texlist, texture
  archive, physics entry, action-list entry (all 695 actions), weld list, motion overlay, sound bank
  and common effect). Columns: `character,asset_kind,source_file,address_or_offset,format,size,notes`.
* Proof of concept: `tools/research/rip_character.py` (section 10). It rips any of the 9
  characters straight from the `.gdi` into OBJ + MTL + PNG, optionally posed with any action frame.
* Related reports from the same research run: `g64.md`, `libsm64.md`, `gmod.md`, `sa1_code_map.csv`.

Addresses are SH-4 runtime addresses. `1ST_READ.BIN` is loaded at `0x8C010000`, so its file
offset is `address - 0x8C010000`. Overlay addresses use the overlay's own pointer spelling
(`0x0Cxxxxxx`).

## 1. Summary

* Every playable character can be ripped from **`1ST_READ.BIN` plus a few PRS files** in
  `SONICADV/`. Models, physics, action tables, weld tables and texture lists are plain data in
  `1ST_READ.BIN`. The exceptions are Super Sonic (models, texlist and 10 actions in the `B_CHAOS7.PRS`
  boss overlay) and Tikal (`TIKAL_PROG.PRS`).
* **Models** are Ninja *basic* models (`NJS_OBJECT` / `NJS_MODEL`, not chunk models). They contain
  strips, triangles, quads and N-gons, 16-bit UVs and per-vertex normals, but no vertex colours,
  skinning or weights. Body parts are rigid nodes. Seams are closed at run time by the
  game's **weld tables** (`PL_JOIN_VERTEX`).
* **Animations**: each character has a `PL_ACTION` table in `1ST_READ.BIN` (Sonic 142, Tails 132,
  Knuckles 111, Amy 98, Big 101, Gamma 87, Eggman 24 entries; 483 distinct motions in total). Between 26 and
  110 of each character's actions are in `1ST_READ.BIN`. The rest point into `<c>_MOT.PRS`
  (loaded at `0x0CC00000`), the snowboard overlays, `MINICART`, `B_CHAOS7`, and (Sonic only) six stage
  overlays. Every motion is `POS|ANG` or `POS|ANG|SCL` with linear keys, 16 bytes per key.
* **Textures**: one PRS-compressed PVM per character, plus effect PVMs. Across the whole disc only three
  pixel formats (ARGB1555, RGB565, ARGB4444) and seven layouts occur: twiddled, twiddled with mipmaps, VQ,
  VQ with mipmaps, small VQ, small VQ with mipmaps, and rectangle. The PoC decoder handles all of them.
* **Physics**: `player_parameter[8]` (0x84 bytes each) at `0x8C1BDFE4`, indexed by character id.
  **The SA Tools DC config puts 7 of the 8 entries at the wrong addresses** (stride 0x108 instead of 0x84). The
  `AdventureRip/figure/*/Physics.ini` files for Tikal, Amy, Gamma and Big contain garbage, and the
  "Eggman", "Tails" and "Knuckles" files actually hold the Tails, Tikal and Gamma entries.
* **Sound** (optional): `P_<char>.MLT` (SFX, bank 3), `V_<char>[_E].MLT` (voices, bank 6), and the
  common SFX banks 0 and 2 that are identical in every stage `.MLT`. The containers are SMLT/SMPB/SMSB.
  The tone data looks like AICA 4-bit ADPCM. Cutscene voices and music are CRI ADX (in an AFS archive).
* **PoC result**: all 9 characters rip correctly from the `.gdi` (geometry, UVs, normals and
  textures), and the action poses are correct. This was checked by rendering them, and it takes 0.2-0.6 s per character in Python.

## 2. Version identification and addressing

* Accept only a `1ST_READ.BIN` with SHA-1 `e74c849806db103aa87f84e41a382873bc96b6df`
  (MD5 `060cad2ceefc07c7429085f30a356046`, the `checkHashes` value of SA Tools `DC - SA1 (US).xml`).
  It is 6,735,488 bytes, at LBA 545711 in this dump. Other builds move every address. For example, Sonic's model
  root is at file offset `0x1BE76C` in US 1.005, `0x1C24A4` in US 1.004, `0x1D4A28` in JP and
  `0x1BEB4C` in International, according to SA Tools `DC_SA1/1004/1ST_READ.ini`, `DC_SA1/JP/1ST_READ_JP.ini` and `DC_SA1/INT/1ST_READ_INT.ini`.
  Supporting another build therefore needs its own address manifest. Those three SA Tools configs are the
  starting point for one.
* Overlays are PRS-compressed **memory images with absolute pointers**. Load addresses for the overlays
  that matter here:

| File | Load address | Character content |
|---|---|---|
| `S_MOT.PRS`, `K_MOT.PRS`, `A_MOT.PRS`, `B_MOT.PRS`, `E_MOT.PRS` | `0x0CC00000` | extra actions of Sonic (and loaded for Tails too), Knuckles, Amy, Big, Gamma |
| `S_SBMOT.PRS` | `0x0CB08000` | Sonic sandboard/snowboard actions + board model |
| `M_SBMOT.PRS` | `0x0CADC000` | Tails snowboard actions + board model |
| `TIKAL_PROG.PRS` | `0x0CB00000` | Tikal model, EV head, 3 motions + 1 shape motion, weld list, texlist |
| `B_CHAOS7.PRS` | `0x0C900000` | Super Sonic models, texlist, Super Sonic actions |
| `MINICART.PRS`, `STG02/04/06/07/08/09.PRS` | `0x0C900000` | one Twinkle Circuit cart action per character; Sonic stage-specific actions |

* `func_8C09D980(charId)` chooses the motion file: ids 0 and 2 load `s_mot.PRS`, 7 `b_mot.PRS`,
  5 `a_mot.PRS`, 3 `k_mot.PRS` and 6 `e_mot.PRS`. It then calls `func_8C09DA44(name, 0x0CC00000)`, which reads and
  PRS-decompresses the file to that address.
* `0x0Cxxxxxx` and `0x8Cxxxxxx` are the same RAM (uncached/cached mirrors). Resolve pointers by physical address
  (`addr & 0x1FFFFFFF`) and **per image**: all STG, ADV and boss overlays share `0x0C900000`.
* Pointers cross images in every direction:
  * 1ST_READ to overlay: Sonic's action list entries point into `S_MOT`, `S_SBMOT`, `STGxx` and `B_CHAOS7`, and 12 of
    Sonic's weld entries point at Super Sonic objects in `B_CHAOS7`.
  * Overlay to 1ST_READ: `B_CHAOS7` actions reuse 1ST_READ motions.
  * Overlay to overlay: Sonic actions 139 and 140 use a `B_CHAOS7` object with an `S_MOT` motion.

  A loader must only follow a pointer when the image that contains it is loaded, and must otherwise treat it
  as "not available".
* The data uses 32-bit little-endian pointers and IEEE floats, so the byte order is the same as x86. On a 64-bit host the structs
  cannot be used in place. **Deserialize into native structs** (or 32-bit offset handles); do not
  cast the raw image.

## 3. Runtime ripping pipeline (what libsa1 must implement)

1. **Read the `.gdi`.** The first line is the track count. Each following line is
   `num lba type sector_size filename offset`; type 4 means data and type 0 means audio. This dump has
   `1 0 4 2352 track01.bin`, `2 14714 0 2352 track02.raw` and `3 45000 4 2352 track03.bin`. The game filesystem is in
   the **last data track** (the high-density area, LBA 45000). For 2352-byte sectors the user data is bytes 16 to 2064 of each
   sector; 2048-byte sectors are used as they are. A sector's position in the track file is `(lba - track_lba) * sector_size`.
2. **ISO9660.** The primary volume descriptor is at `hd_lba + 16`; check for `CD001` at offset 1. The root
   directory record is at PVD offset 156. Directory records give the extent (LBA, offset 2) and size (offset 10). Names are
   uppercase with a `;1` suffix. Files needed: `1ST_READ.BIN` in the root, and `SONICADV/<name>` (see 3.1).
   This is the same logic as `tools/extract_gdi.py`, applied to single files.
3. **Verify** `1ST_READ.BIN` (SHA-1 above). Refuse, or select another manifest, on a mismatch.
4. **PRS-decompress** the overlays and PVMs. This is SEGA's LZ77 variant: control bits LSB-first, a control byte read
   lazily, literals, short copies (2-5 bytes, offset -256..-1) and long copies (3-256 bytes, offset
   -8192..-1). Our `tools/prs.py` documents the bitstream. A decompressor is about 60 lines of C.
5. **Map images**: `1ST_READ.BIN` at `0x8C010000`, plus each overlay at its load address.
6. **Walk from known roots** (section 4 onward): object trees, action tables, weld tables, the PVM load list and the physics
   entry. Build native copies and validate every pointer against the image range.
7. **Decode textures** (section 6) to RGBA8. Hand them to the host (see `gmod.md` section 4.3) together with the material
   flags.
8. Evaluate motions and welds in the library every tick and output posed geometry (see section 11).

### 3.1 Files to read per character

| Character | Always | Optional (features) |
|---|---|---|
| all | `1ST_READ.BIN` (6.7 MB) | `OBJ_REGULAR.PRS`, `EFF_REGULAR.PRS` (rings, shadow, sparkle, dust); `CON_REGULAR.PRS` (HUD) |
| Sonic | `SONIC.PRS`, `SON_EFF.PRS`, `S_MOT.PRS` | `S_SBMOT.PRS` (board), `B_CHAOS7.PRS` + `SUPERSONIC.PRS` (Super Sonic), `STG02/04/06/07/08/09.PRS` (stage actions) |
| Tails | `MILES.PRS` | `M_SBMOT.PRS`, `MINICART.PRS`, (`S_MOT.PRS` is loaded by the game but no Tails action uses it) |
| Knuckles | `KNUCKLES.PRS`, `KNU_EFF.PRS`, `K_MOT.PRS` | `MINICART.PRS` |
| Amy | `AMY.PRS`, `AMY_EFF.PRS`, `A_MOT.PRS` | `MINICART.PRS` |
| Big | `BIG.PRS`, `FISHING.PRS`, `BIG_KAERU.PRS`, `B_MOT.PRS` | `MINICART.PRS` |
| Gamma | `E102.PRS`, `E102BEAM.PRS`, `E102EFFECT.PRS`, `E102TIME.PRS`, `E102HIT.PRS`, `E_MOT.PRS` | `MINICART.PRS` |
| Eggman (NPC) | `EGGMAN.PRS` | - |
| Tikal (NPC) | `TIKAL_PROG.PRS`, `TIKAL.PRS` | - |
| sound | - | `P_<c>.MLT`, `V_<c>_E.MLT` or `V_<c>.MLT`, any stage `.MLT` (common banks) |

Compressed and decompressed sizes are in the CSV. Examples: `SONIC.PRS` is 36,861 bytes (108,000 decompressed),
`S_MOT.PRS` 214,890 (488,608), `B_CHAOS7.PRS` 873,457 (1,487,956) and `E102.PRS` 105,038 (210,176).

## 4. Models

### 4.1 Structures (as stored on the disc)

| Struct | Size | Layout (offset: field) |
|---|---|---|
| `NJS_OBJECT` | 0x34 | 00 u32 eval flags; 04 `NJS_MODEL*`; 08 f32 pos[3]; 14 s32 ang[3] (BAMS, 0x10000 = 360 deg); 20 f32 scl[3]; 2C child; 30 sibling |
| `NJS_MODEL` (basic) | 0x28 | 00 points (f32[3] x n); 04 normals (f32[3] x n); 08 s32 nbPoint; 0C meshsets; 10 materials; 14 u16 nbMeshset; 16 u16 nbMat; 18 f32 center[3]; 24 f32 radius |
| `NJS_MESHSET` | **0x18** on DC (SADX PC uses 0x1C) | 00 u16 type_matId (bits 14-15 poly type, bits 0-13 material index); 02 u16 polygon count; 04 s16* polys; 08 attrs; 0C per-corner normals; 10 per-corner colours; 14 per-corner UVs (`NJS_TEX` s16 u,v) |
| `NJS_MATERIAL` | 0x14 | 00 diffuse ARGB; 04 specular ARGB; 08 f32 exponent; 0C attr_texId (texture index = value & 0x3FFFFFFF, top 2 bits = callback flags); 10 attrflags (`NJD_FLAG_*`, filter bits 13-14, src blend bits 29-31, dst blend bits 26-28) |

Facts measured on all 9 character trees:

* Polygon types: Sonic, Super Sonic, Tails, Knuckles, Amy, Big and Tikal use **strips only** (type 3). Gamma and Eggman
  use triangles, quads and N-gons (types 0, 1, 2). For types 2 and 3, each polygon starts with a u16 header:
  the low 15 bits are the vertex count, and bit 15 means the winding starts reversed. Strip triangle *j* uses corners
  *j, j+1, j+2* and swaps the first two when *j* is odd XOR reversed. Quads are stored in strip order
  (0,1,2)+(2,1,3). N-gons are drawn as strips (SA Tools does the same). With these rules, 100% of Sonic's and Gamma's triangles are
  counter-clockwise when the vertex normals point at the viewer, in a right-handed Y-up frame.
* UVs: s16, where **255 = 1.0** (SA Tools uses /255). There is one UV per polygon corner, in the order the corners appear in the poly stream
  (count headers excluded).
  Every character meshset has UVs except a handful of untextured ones.
* Every model has per-vertex normals. No character meshset has attrs, per-corner normals or vertex colours.
* `NJD_EVAL_ZXY_ANG` (0x20) is never set in a character tree. Flags seen: UNIT_POS 1, UNIT_ANG 2, UNIT_SCL 4, HIDE 8 and
  BREAK 0x10, mostly on joint nodes that have no model. Transform per node: `T(pos) * Rz * Ry * Rx * S(scl)` with
  column vectors (X rotation applied first), and with the standard right-handed rotation sign (the same as
  SA Tools' renderer). Each node's world matrix is the parent's matrix times the local matrix.
* **Orientation**: Y up, right-handed. In model space the characters face **-X**. This is shown by the
  front view (section 10) and by the weld table's right hand/foot markers, which sit at -Z. SADX draws objects with
  `njRotateY(0x8000 - ang.y)`; confirm the same convention in the DC player display code. For Source
  (X forward, Y left, Z up) a proper rotation is `src = (-x, z, y) * scale`, with no mirroring.
* Size: in the standing pose (action 0, frame 0) Sonic spans y -5.0 to +4.7 (about 9.7 units, origin near the waist).
  Tails is 8.7 units, Knuckles 10.9, Amy 9.1, Big 19.5, Gamma 21.2 and Eggman 15.5. Physics `height` is 10 for Sonic.

### 4.2 Material features used by character models (renderer requirements)

| Feature | Use |
|---|---|
| Blend | every material is SRC_ALPHA / INV_SRC_ALPHA; some set `NJD_FLAG_USE_ALPHA` (Sonic 25, Super Sonic 24, Tails 12, Knuckles 12, Amy 18, Big 7, Gamma 2, Tikal 4 materials) |
| UV addressing | `CLAMP_U`/`CLAMP_V` on almost every material. **No `FLIP_U`/`FLIP_V` anywhere.** UVs reach 476/255 (Sonic), so clamping must be done per pixel, not per vertex. Tikal has unclamped materials with u up to 3197/255 (tiling) |
| Environment map (`USE_ENV`) | Gamma 24, Super Sonic 9, Eggman 4, Amy 3, Knuckles 2, Tikal 2 materials (sphere-map UVs from view-space normals) |
| Double sided | Eggman 11, Big 4, Gamma 3, Amy 2, Tikal 4 |
| Ignore light / flat | Gamma 7, Big 2 (+2 flat), Eggman 1 |
| Filter | bilinear (Sonic, Tails, Knuckles, Big, Eggman, Tikal, Super Sonic), point (Amy, Gamma) |
| Ignore specular | most body materials |

### 4.3 Per-character model inventory (US 1.005)

`n/v/t` = nodes, vertices, triangles. All rows (and their texture ids) are in the CSV.

**Sonic**: texlist SONIC `0x8C1C6C94` (28 textures), SON_EFF `0x8C1C6AB4` (29)

| Address | Model | n/v/t | Notes |
|---|---|---|---|
| `0x8C1CE76C` | main (`s_kihon10_sonic`) | 73/635/1001 | used by 125 of 142 actions |
| `0x8C1D630C` | curled | 74/635/1001 | spin/jump actions 14, 15, 83, 125 |
| `0x8C1DBB2C` | jump ball | 1/212/420 | action 32 |
| `0x8C1D66D0`, `0x8C1D6A94`, `0x8C1D6E68` / `0x8C1D722C`, `0x8C1D75F0`, `0x8C1D79D0` | left / right foot variants 1-3 | 1/20/32 each | swapped into the feet for the Light Speed Shoes upgrade; pointer table `0x8C1C5EE8` |
| `0x8C1E0550`, `0x8C1E0EB0` | Light Speed Shoes worn L/R | 2/45/74 | welded (weld entries 12, 13) |
| `0x8C1E12AC` | Crystal Ring worn | 4/60/92 | weld entry 14 |
| `0x8C1CD804` | `sonic_z` attach | -/205/382 | head attach of node 22 (`0x8C1CD82C`), also referenced at `0x8C1C5F18`; alternate head mesh |
| `0x8C1DFBD8` | EV head | 2/427/674 | cutscene head, shape motion `0x8C1FBBEC` |
| `0x8C255B10`, `0x8C25654C` | dash trail, jump-ball effect | 1/12/12, 1/52/100 | SON_EFF textures |
| `0x8C256B7C`, `0x8C256D38` | frozen ice block + particles | 1/26/48, 1/6/8 | texlist STX_ICE0 (Ice Cap) |
| `0x0CC02AD4` (S_MOT) | Light Speed aura | 73/207/290 | untextured copy of the skeleton |
| `0x0CB37454` (S_SBMOT) | snowboard | 1/24/44 | texture from the stage PVM |

**Super Sonic**: `B_CHAOS7.PRS`, texlist SUPERSONIC `0x0C901EA0` (73 textures)

| Address | Model | n/v/t |
|---|---|---|
| `0x0C909D2C` | main | 73/642/1046 |
| `0x0C90BD04` | jump ball | 1/212/420 |
| `0x0C90C620`, `0x0C90CFA4`, `0x0C90D928` | aura, aura dash 1, aura dash 2 | 1/58-61/108-112 |
| `0x0C90DC14`, `0x0C90DF00` | water splash L/R | 1/16/18 |

**Tails**: texlist MILES `0x8C257C54` (24)

| Address | Model | n/v/t | Notes |
|---|---|---|---|
| `0x8C25F968` | main | 68/649/997 | 109 actions |
| `0x8C26769C` | flight variant | 68/649/997 | 19 actions (flying, tails spun) |
| `0x8C26F3A8` | curled | 69/649/997 | 3 actions |
| `0x8C271370` | jump ball | 1/212/420 | 1 action |
| `0x8C273E64`, `0x8C274470`, `0x8C274A7C`, `0x8C275088`, `0x8C275694` | tail shapes L0, L1, R0, R1, U | 1/32/60 each | tail geometry variants used by code |
| `0x8C273864` | `miles_head_z` attach | -/228/393 | alternate head mesh |
| `0x8C279198` | EV head | 3/382/597 | cutscenes |
| `0x8C28FAB4`, `0x8C290484`, `0x8C290E54` | Rhythm Badge, Jet Anklet R/L (worn) | 1/109/142, 2/45/74 | upgrades |
| `0x0CB029C4` (M_SBMOT) | snowboard | 1/24/44 | |

**Knuckles**: texlist KNUCKLES `0x8C3537D8` (22), KNU_EFF `0x8C3538DC` (21)

| Address | Model | n/v/t | Notes |
|---|---|---|---|
| `0x8C35B094` | main | 73/624/981 | 80 actions |
| `0x8C36286C` | glide/climb variant | 73/624/981 | 27 actions |
| `0x8C36C084` | curled | 74/624/981 | 3 actions |
| `0x8C364834` | jump ball | 1/212/420 | 1 action |
| `0x8C36E40C` | `knuck_z` attach | -/212/410 | alternate head mesh |
| `0x8C37273C` | EV head | 2/440/712 | |
| `0x8C38E188`, `0x8C38E768`, `0x8C38ED2C`, `0x8C38F2F0` | Shovel Claw L/R, normal and with Fighting Gloves | 1/30/53 | upgrades |
| `0x8C38F788`, `0x8C390440`, `0x8C390010`, `0x8C390CE4` | Fighting Glove wrists L/R, claw parts L/R | 1/23-24, 3/48/66 | upgrades (the last two are unnamed in SA Tools) |
| `0x8C38D268`, `0x8C38DBA8` | punch effect, Maximum Heat effect | 1/65/112, 1/58/112 | KNU_EFF |

**Amy**: texlist AMY `0x8C4BF3AC` (53), AMY_EFF `0x8C4BF3E4` (4)

| Address | Model | n/v/t | Notes |
|---|---|---|---|
| `0x8C4C7C3C` | main (includes the Piko hammer) | 78/656/1000 | all 98 actions |
| `0x8C4CAF6C`, `0x8C4C9088` | `amy_z`, `amy_y` attaches | -/187, -/45 | alternate meshes |
| `0x8C4CBA84`, `0x8C4CC1D0` | Long Hammer, Warrior Feather | 2/54/86, 1/41/54 | upgrades |
| `0x8C4C859C` | barrel (hiding) | 3/54/74 | |
| `0x8C4BE5D0` | Birdie | 10/258/348 | companion bird, textures 44-52 of AMY |
| `0x8C4CC538` | hammer shockwave | 1/24/24 | AMY_EFF |
| `0x8C4D0718` | EV head | 5/427/687 | |

**Big**: texlist BIG `0x8C525FB0` (36), FISHING `0x8C54CB4C` (65), BIG_KAERU `0x8C566268` (8)

| Address | Model | n/v/t | Notes |
|---|---|---|---|
| `0x8C52F298` | main (includes the rod) | 73/729/1134 | all 94 non-empty actions |
| `0x8C530BB0` | `big_z` attach | -/143/282 | alternate mesh |
| `0x8C53232C`, `0x8C533000` | Life Belt, Power Rod | 3/84/148, 3/63/97 | upgrades |
| `0x8C54E36C`, `0x8C54FC70`, `0x8C551ADC`, `0x8C5537E8`, `0x8C5557A0` | lures (normal, blue, red, silver, gold) | 2-3/144-197 | FISHING textures |
| `0x8C555B08`, `0x8C555C3C` | cast circle, cast cursor | 1/21/15, 1/5/6 | |
| `0x8C5680E8` | Froggy | 15/144/200 | BIG_KAERU |
| `0x8C5367C4` | EV head | 7/346/492 | |

**Gamma**: texlists E102 `0x8C423454` (35), E102BEAM `0x8C423504` (14), E102EFFECT `0x8C42359C` (12),
E102TIME `0x8C422F04` (14), E102HIT `0x8C4BB1C4` (17)

| Address | Model | n/v/t | Notes |
|---|---|---|---|
| `0x8C42CC34` | main | 77/680/970 | all 87 actions; tris/quads/N-gons |
| `0x8C42DA90` | targeting arm | 12/66/80 | |
| `0x8C42E2C4`, `0x8C42EF4C`, `0x8C42F780`, `0x8C430408` | normal / laser blaster, and their "A" variants | 2/44-66 | Laser Blaster upgrade |
| `0x8C4309C4`, `0x8C431488` | hover jet (Jet Booster), scope | 1/24/36, 3/49/66 | |
| `0x8C431D88`, `0x8C431FB0`, `0x8C43222C`, `0x8C432584` | blaster flash, targeting beam, laser blast, hover-jet flame | small | E102BEAM / E102EFFECT |

**Eggman** (NPC; physics entry 1): model `0x8C57A6BC` (24/834/1138), texlist EGGMAN `0x8C570508` (19), 24 actions
using 5 motions. **Tikal** (NPC; physics entry 4): `TIKAL_PROG.PRS` model `0x0CB09818` (103/621/948), EV head
`0x0CB0DA04`, texlist TIKAL `0x0CB01814` (18), weld list `0x0CB015A8`. Neither is playable in the DC game. Their models,
physics and (for Eggman) action list are complete enough to drive them like a player.

## 5. Animations

### 5.1 Structures

| Struct | Size | Layout |
|---|---|---|
| `PL_ACTION` (SADX symbol name) | 0x10 | 00 `NJS_ACTION*` actptr; 04 u8 objnum (node count: 73, 74 or 1); 05 s8 mtnmode; 06 u16 next (action index to chain to); 08 f32 "frame" (blend/transition rate; SA Tools calls it TransitionSpeed); 0C f32 racio (playback speed) |
| `NJS_ACTION` | 0x08 | 00 `NJS_OBJECT*`; 04 `NJS_MOTION*` |
| `NJS_MOTION` | 0x0C | 00 mdata; 04 u32 nbFrame; 08 u16 type (`NJD_MTYPE_*` bits: 1 POS, 2 ANG, 4 SCL, 0x10 VERT, 0x20 NORM); 0A u16 inp_fn (low 4 bits = factor count, bits 6-7 = interpolation, 0 = linear) |
| mdata entry | 8 x factors | one per node, in object-tree order (node, children, then siblings, depth first; nodes without a model count too): `void* key[f]; u32 nbKeys[f]`, factors in POS, ANG, SCL order |
| `NJS_MKEY_F` (POS, SCL) | 0x10 | u32 frame; f32 x, y, z |
| `NJS_MKEY_A` (ANG) | 0x10 | u32 frame; s32 x, y, z (BAMS) |

### 5.2 Where every action lives (US 1.005)

| Character | Table | Entries | 1ST_READ | Overlays | Distinct motions | Frames min-max (sum) | Keys |
|---|---|---|---|---|---|---|---|
| Sonic | `0x8C1C55A0` | 142 | 41 | S_MOT 43, S_SBMOT 23, B_CHAOS7 10 (+2 B_CHAOS7 object with S_MOT motion), STG08 8, STG06 5, STG02 4, STG09 4, STG04 1, STG07 1 | 117 | 7-100 (3885) | 91,596 |
| Tails | `0x8C256D6C` | 132 | 110 | M_SBMOT 21, MINICART 1 | 87 | 2-95 (3190) | 71,327 |
| Knuckles | `0x8C3528CC` | 111 | 81 | K_MOT 29, MINICART 1 | 71 | 2-75 (2895) | 66,860 |
| Amy | `0x8C4BB1D0` | 98 | 37 | A_MOT 60, MINICART 1 | 67 | 2-90 (2500) | 61,930 |
| Big | `0x8C525564` | 101 | 26 | B_MOT 67, MINICART 1; 7 NULL entries (77, 79, 81-85) | 71 | 8-90 (2582) | 55,357 |
| Gamma | `0x8C4225D4` | 87 | 57 | E_MOT 29, MINICART 1 | 65 | 2-80 (2351) | 49,959 |
| Eggman | `0x8C57008C` | 24 | 24 | - | 5 | 30-40 (770) | 10,688 |

Each entry was resolved by checking the `NJS_ACTION` structure against every overlay that maps the address. Each
resolved to exactly one image. The per-action rows (with SA Tools names where they exist) are in the CSV. The STGxx actions are
Sonic's stage-specific moves: Windy Valley (STG02), Speed Highway (STG04), Sky Deck (STG06), Lost World (STG07),
Ice Cap (STG08) and Casinopolis (STG09). Sonic's actions 130-141 are Super Sonic (B_CHAOS7).
SA Tools lists more motions than the action tables reference. Motions listed but not referenced by any action:
S_MOT 0 of 40, K_MOT 6 of 33, A_MOT 6 of 56, B_MOT 3 of 58, E_MOT 1 of 24, S_SBMOT 16 of 38, M_SBMOT 9 of 23 and 1ST_READ
39 of 259 (`figure/*` only). One B_MOT and one E_MOT motion are used by an action but missing from SA Tools. The unreferenced ones are
probably reached directly from code or are leftovers; see open question 3.

### 5.3 Motion facts

* Types: only `0x3` (POS|ANG, 2 factors) and `0x7` (POS|ANG|SCL, 3 factors) in all 483 action motions;
  `inp_fn` is 2 or 3, so **no spline interpolation** anywhere. Every key track starts at frame 0 and ends at
  `nbFrame - 1`.
* Angles are raw s32 BAMS, up to 0x3FFFB (about 4 turns). 101 consecutive key pairs differ by more than 0x8000 (Eggman 85, Knuckles 7,
  Tails 3, Amy 3, Sonic 2, Gamma 1). Whether the game interpolates these as plain integers (the PoC
  does, and the poses look right) or along the shortest path must be read from the motion code. This is an open question.
* Motion data replaces the node's own pos/ang/scl only where a track exists. The PoC also clears the node's UNIT_* flag
  for that component.
* `mtnmode` values: 3, 4, 5, 6, 9, 10 and 12. From the data: 3 loops; 4 plays once and then switches to `next`; 9 and 10 are the
  speed-driven cycles (walk, jog, run, spin: Sonic actions 9-15, 83, 125, 141). 5, 6 and 12 are still to be identified from the
  `mtnjvwk` motion driver.
* **Shape motions** (vertex morphs, type `0x30` VERT|NORM): only the cutscene heads use them:
  1ST_READ `0x8C1FBBEC` (Sonic), `0x8C28E9BC` (Tails), `0x8C38C870` (Knuckles), `0x8C4E591C` (Amy),
  `0x8C53C310` (Big), and TIKAL_PROG `0x0CB23298`. Each key points to a full replacement vertex (or normal) array. Gameplay does not need them.

### 5.4 Minimal motion set for a GMod port

* Core moveset: `1ST_READ.BIN` + `<c>_MOT.PRS` (Sonic: `S_MOT.PRS`; Tails: nothing beyond 1ST_READ).
* Optional: `S_SBMOT`/`M_SBMOT` (boards), `MINICART` (cart), `B_CHAOS7` (Super Sonic), and Sonic's stage overlays.
  A port that does not load an overlay must mark those actions unavailable and never follow their pointers.
  The table entries still hold the stale overlay address.

### 5.5 Weld tables (`PL_JOIN_VERTEX`)

Layout (0x18 bytes, null-terminated): 00 base object; 04 source object; 08 destination object; 0C u8 vertex
count; 0D u8 type; 0E u8 srcdepth; 0F u8 dstdepth; 10 `NJS_POINT3*` org (NULL on disc, filled at run time); 14 u16* pairs.
Types (SA Tools `WeldType`): 1 average, 2 source, 3 destination, 4 right-hand position, 5 left-hand, 6 right-foot,
7 left-foot, 8 user0, 9 user1. The pair list is `(source vertex, destination vertex)`. In Sonic's bind pose the
52 type-2 pairs coincide to within 0.047 units on average (the reverse order is 35 units off). In the
running pose they drift apart (0.12 on average), so the game moves the destination vertices onto the source vertices every frame.
Without welding, seams are visible at the shoulders and hips (see the PoC renders).

| Character | Address | Entries | Types |
|---|---|---|---|
| Sonic | `0x8C1C5FA4` | 34 | 2 x27 (12 of them on Super Sonic objects in B_CHAOS7), 4, 5, 6 x2, 7 x2, 8 |
| Tails | `0x8C2576EC` | 29 | 2 x24, 4, 5, 6, 7, 8 (base objects: main and flight variant) |
| Knuckles | `0x8C353114` | 29 | 2 x24, 4-8 |
| Amy | `0x8C4BB8B4` | 17 | 2 x12, 4-8 |
| Big | `0x8C525C90` | 14 | 1 x4, 2 x4, 4-9 |
| Gamma | `0x8C422BE8` | 4 | 4-7 only (rigid robot) |
| Eggman | `0x8C5702BC` | 14 | 1 x10, 4-7 |
| Tikal | `0x0CB015A8` (TIKAL_PROG) | 14 | 2 x13, 3 |

The hand, foot and user positions (types 4-9) are where held items, upgrades and effects attach
(SADX: `playerwk.righthand_pos` and the related fields).

## 6. Textures

### 6.1 Which archives a character loads

The game's own table is the best index. At `0x8C1C2584` there are 8 pointers, indexed by character id, each to a
null-terminated list of `{char* pvm_name; NJS_TEXLIST* texlist}` (8 bytes per entry). The texture loaders
`func_8C098FAE`, `func_8C098FFC` and `func_8C099080` read it. The common list at `0x8C1C2450` has OBJ_REGULAR
`0x8C19F530` (96), EFF_REGULAR `0x8C1C3068` (138), CON_REGULAR `0x8C1C3310` (56), EXTRA, HINT and EME_KIRAN.

| Id | Character | List | Archives (texlist, textures) |
|---|---|---|---|
| 0 | Sonic | `0x8C1C24BC` | SONIC (`0x8C1C6C94`, 28), SON_EFF (`0x8C1C6AB4`, 29) |
| 1 | Eggman | `0x8C1C24E4` | EGGMAN (`0x8C570508`, 19) |
| 2 | Tails | `0x8C1C24D4` | MILES (`0x8C257C54`, 24) |
| 3 | Knuckles | `0x8C1C24F4` | KNUCKLES (`0x8C3537D8`, 22), KNU_EFF (`0x8C3538DC`, 21) |
| 4 | Tikal | `0x8C1C2574` | TIKAL (`0x0CB01814` in TIKAL_PROG, 18) |
| 5 | Amy | `0x8C1C250C` | AMY (`0x8C4BF3AC`, 53), AMY_EFF (`0x8C4BF3E4`, 4) |
| 6 | Gamma | `0x8C1C2544` | E102 (35), E102BEAM (14), E102EFFECT (12), E102TIME (14), E102HIT (17) |
| 7 | Big | `0x8C1C2524` | BIG (36), FISHING (65), BIG_KAERU (8) |
| - | Super Sonic | (B_CHAOS7) | SUPERSONIC (`0x0C901EA0`, 73) |

The files are `SONICADV/<NAME>.PRS`, a PRS-compressed PVM. There are no `.PVM` files on the DC disc. All 588 PVMs are
compressed this way; the 441 loose `.PVR` files are mostly UI and tutorial images. The texlists' `NJS_TEXNAME`
entries (12 bytes: name pointer, attr, texaddr) are named in some lists (SON_EFF, MILES, KNUCKLES, AMY, BIG_KAERU, E102TIME,
E102HIT) and nameless in others (SONIC, KNU_EFF, AMY_EFF, BIG, FISHING, E102, E102BEAM, E102EFFECT, EGGMAN, OBJ_REGULAR,
EFF_REGULAR). The loader ignores the names either way. The material texture id is simply **the index in the PVM**, which
is also the index in the texlist.

### 6.2 PVM container (`PVMH`)

* 00 `"PVMH"`; 04 u32 header size (data starts at 8 + this, 32-byte aligned); 08 u16 flags; 0A u16 count.
* Flags are `0x0109` in every character archive: entry names (bit 0), global index (bit 3) and PVRT headers
  (bit 8). Bits 1 and 2 (format and dimensions in the table) are not set. Entry = u16 id + 28-byte name + u32 GBIX = **34 bytes**,
  starting at 0x0C.
* Then one `PVRT` chunk per entry, in order. There is no per-entry GBIX chunk because the GBIX is in the table.
  Chunk: `"PVRT"`, u32 size (counted from offset 8, **including trailing padding**), u8 pixel format,
  u8 data format, u16 0, u16 width, u16 height, then data. Walk the chunks by `8 + size`, resyncing on the next
  `PVRT`/`GBIX` magic for safety.

### 6.3 PVR formats that actually occur, and the decoder spec

Survey of every PVRT chunk on the disc (588 PVMs + 441 PVRs): pixel formats are **only** 0 ARGB1555, 1 RGB565 and
2 ARGB4444 (no palettes, YUV, bump or 8888). Data formats:

| Code | Layout | Disc count | Character PVMs |
|---|---|---|---|
| 0x01 | square twiddled | about 2,030 | yes (SON_EFF, KNU_EFF, AMY_EFF, E102HIT, E102TIME, Super Sonic dash and ball) |
| 0x02 | square twiddled + mipmaps | 6,600+ | yes (most body textures) |
| 0x03 | VQ | about 570 | yes (Super Sonic 32, KNU_EFF, E102EFFECT, E102BEAM) |
| 0x04 | VQ + mipmaps | 5,600+ | yes (EGGMAN 4) |
| 0x09 | rectangle (linear scanlines) | about 1,800 | no (UI, HUD) |
| 0x10 / 0x11 | small VQ / small VQ + mipmaps | about 480 | no (OBJ_REGULAR: rings, items) |

Decoder rules, all verified by decoding every format and checking the images:

* Texels are little-endian u16. ARGB1555 has a 1-bit alpha (punch-through). ARGB4444 is alpha-blended. RGB565 is opaque.
* **Twiddled** (square, side *s*): texel (x, y) is stored at index `interleave(y bits -> even bits, x bits -> odd bits)`.
  Non-square twiddled data is laid out as consecutive square blocks; the game does not use it.
* **Mipmaps** are stored smallest first. For 16-bit twiddled, the 1x1 level is preceded by **6 padding bytes**. However, **46
  textures** on the disc (none of them character textures) use only 2. Rule: use 6 if the chunk is long enough for
  `6 + sum(levels) + w*h*2`, otherwise 2. This was checked visually; the other choice garbles the image.
* **VQ**: a 256-entry codebook (each entry is 4 texels = 8 bytes, stored in 2x2 twiddled order (0,0),(0,1),(1,0),(1,1)),
  followed by one byte per 2x2 block. The index map is twiddled at block level. VQ mipmaps put 1 padding byte before the
  index levels (1, 1, 4, 16, ... bytes).
* **Small VQ** stores only *N* codebook entries (16 for 16 px; 32 without mipmaps or 64 with mipmaps for 32 px; 52 textures use 256).
  The header does not store *N*. Derive it as the largest power of two that is at most
  `(data_len - index_bytes) / 8`. No index on the disc is ever >= *N*.
* The host only needs the top mip level. Mipmaps can be regenerated.

Sizes: character body textures are 8x8 to 64x64. E102TIME has one 128x128 texture and FISHING (Big) has a 256x256 one.
Decoded RGBA8 totals: Sonic+SON_EFF 197 KB, Super Sonic 659 KB, Tails 128 KB, Knuckles 330 KB, Amy 248 KB,
Big (with FISHING and BIG_KAERU) 837 KB, Gamma (all 5 archives) 793 KB, Eggman 135 KB, Tikal 113 KB. Examples: SONIC.PRS decompresses to 108,000 bytes (28
textures), SUPERSONIC 198,880 (73), AMY 161,408 (53) and E102 210,176 (35).

## 7. Physics and other per-character data

### 7.1 `player_parameter` table

`0x8C1BDFE4` holds 8 entries x 0x84 bytes, indexed by character id (0 Sonic, 1 Eggman, 2 Tails, 3 Knuckles,
4 Tikal, 5 Amy, 6 Gamma, 7 Big). It ends at `0x8C1BE404`. Code evidence: `func_8C06C350` (player
work setup) computes `id * 0x84` from the character id byte at `twp+9`, then copies 0x84 bytes from this table into the new
0x200-byte player work at **+0x64**. `func_8C075214` also reads the table.
**SA Tools `1st_read.ini` uses stride 0x108**, so its "Eggman", "Tails" and "Knuckles" entries are really entries 2, 4 and 6, and
its "Tikal", "Amy", "Gamma" and "Big" addresses point past the table. Do not use `AdventureRip/figure/*/Physics.ini`.

Fields (SADX `player_parameter`, SA Tools `PlayerParameter`): 00 s32 `jump2_timer`, then f32 at 04
`pos_error`, 08 `lim_h_spd`, 0C `lim_v_spd`, 10 `max_x_spd`, 14 `max_psh_spd`, 18 `jmp_y_spd`, 1C `nocon_speed`,
20 `slide_speed`, 24 `jog_speed`, 28 `run_speed`, 2C `rush_speed`, 30 `crash_speed`, 34 `dash_speed`, 38
`jmp_addit`, 3C `run_accel`, 40 `air_accel`, 44 `slow_down`, 48 `run_break`, 4C `air_break`, 50 `air_resist_air`,
54 `air_resist`, 58 `air_resist_y`, 5C `air_resist_z`, 60 `grd_frict`, 64 `grd_frict_z`, 68 `lim_frict`, 6C
`rat_bound`, 70 `rad`, 74 `height`, 78 `weight`, 7C `eyes_height`, 80 `center_height`.

Values that identify each entry (from the disc):

| Field | Sonic | Eggman | Tails | Knuckles | Tikal | Amy | Gamma | Big |
|---|---|---|---|---|---|---|---|---|
| address | `..DFE4` | `..E068` | `..E0EC` | `..E170` | `..E1F4` | `..E278` | `..E2FC` | `..E380` |
| max_x_spd | 3 | 3 | 2 | 2.5 | 3 | 0.05 | 2.5 | 1 |
| rush_speed | 2.3 | 2.3 | 2.8 | 3.1 | 2.3 | 2.3 | 2.5 | 2.3 |
| rad / height | 4 / 10 | 8.5 / 18 | 3.5 / 9 | 4 / 11.4 | 4 / 10 | 3.5 / 10 | 4 / 21 | 9.5 / 17 |
| weight | 0.08 | 0.08 | 0.08 | 0.08 | 0.08 | 0.04 | 0.1 | 0.135 |
| eyes / center height | 7 / 5.4 | 7 / 5.3 | 6 / 4.5 | 9 / 5.7 | 7 / 5 | 7 / 5 | 20 / 14 | 15 / 8 |

Super Sonic has no entry. He runs on Sonic's entry plus code-side changes (SADX has a separate
`Sonic_SuperPhysics_*` task). Find the DC equivalent in Sonic's code or in `B_CHAOS7`.

### 7.2 Other data the engine needs (rip by address, do not hard-code)

| Data | Where | Notes |
|---|---|---|
| Action tables | section 5.2 | `PL_ACTION` |
| Weld tables | section 5.5 | `PL_JOIN_VERTEX` |
| PVM load lists | section 6.1 | |
| Sonic collision shapes (candidate) | `0x8C1C5E80`, 2 x 0x30 | right after the action table. It looks like two `CCL_INFO` (attr 0x6E0 and 0x8FEE; 4, 4, 0.65). Referenced by Sonic's main `func_8C0A77BC`. The other characters probably have the same pattern next to their action tables. **Unverified** |
| Sonic part-swap table | `0x8C1C5EE8` | `NJS_OBJECT*` list (foot variants, head attach) used when upgrades are equipped |
| Start positions | `0x8C19C4F8` (stage), `0x8C19DA6C` (field), and the same for the other characters (CSV / SA Tools) | not needed in GMod |
| Code constants | literal pools of the player code | homing range, spin dash, glide, flight time and similar come with the decompiled code. `func_8C0A77BC` (Sonic) shows typical ones (5.0, 0.2, 0.3, 0.5) |

The game was built with `-fpu=single -round=zero` (docs/compiler.md). x86 SSE rounds to nearest, so exact physics
reproduction needs round-toward-zero float mode (`_MM_SET_ROUNDING_MODE(_MM_ROUND_TOWARD_ZERO)`) while ticking
the library, or the small drift must be accepted.

## 8. Sounds (optional for a first port)

`func_8C09340C` loads sound banks at stage start. It loads the stage `.MLT`, then the character's voice bank
`V_<c>.MLT` (Japanese) or `V_<c>_E.MLT` (English; selected by the byte at `0x8C754C00`), then the player SFX bank `P_<c>.MLT`.
Sonic and Tails share `*_SONICTAILS.MLT`. Casinopolis and the Chao gardens use their own variants
(`CASINO_<c>*.MLT`, `ALIFE_<c>[_E].MLT`).

**SMLT container** (`"SMLT"`, u32 version 0x101, u32 entry count 21, 0x20-byte header). Each 0x20-byte entry is
magic (`SMPB` tone bank, `SMSB` sequence bank, `SFPW`/`SFPB`/`SFOB` DSP), u32 bank number, u32 AICA RAM address, u32 RAM size,
u32 file offset (0xFFFFFFFF = not present), u32 file size, followed by 8 bytes of zero.

| Bank | Content | Source |
|---|---|---|
| 0, 2 | common SFX (rings, springs, items, ...); byte-identical in every stage MLT | any stage MLT, e.g. `EMERALDCOAST.MLT` (bank 0 is 261,376 bytes) |
| 1, 4, 5 | stage-specific | stage MLT |
| 3 | player SFX | `P_SONICTAILS.MLT` (SMPB 139,328 + SMSB 736 bytes), `P_KNUCKLES`, `P_AMY`, `P_BIG`, `P_E102` |
| 6 | voice clips | `V_SONICTAILS_E.MLT` (SMPB 196,096 + SMSB 1,440), `V_KNUCKLES_E`, `V_AMY_E`, `V_BIG_E`, `V_E102_E` (and the JP versions) |

* `SMPB` (MIDI program bank): header with offset/count pairs (programs at +0x30; P_SONICTAILS has 14 programs,
  each 0x58 bytes apart), a velocity curve, and then tone data. The tone data looks like **Yamaha AICA 4-bit ADPCM**.
  For example, program 0's split points at MPB offset 0x5C8 and the next tone starts 3,084 bytes later, which fits about
  6,160 4-bit samples. The nibble statistics also look like ADPCM. The exact split layout (offset, loop and format fields)
  is not decoded yet.
* `SMSB` (MIDI sequence bank): `SMSD` sequences, one per request number; P_SONICTAILS has 13.
* The mapping from the game's sound-effect id to (bank, request) is in the sound code and tables of `1ST_READ.BIN`
  (sd driver 1.00.17, `MASARU.DRV`). It still has to be located.
* Decoders a port needs: an SMLT/MPB parser and an AICA ADPCM decoder (the same algorithm vgmstream calls
  "AICA"/Yamaha ADPCM). For extras: CRI ADX (BGM `SONICADV/*.ADX`, stereo 44.1 kHz, ADX type 3), and AFS for
  `EVENT_ADX_US.AFS` (2,044 mono 22.05 kHz ADX voice clips, cutscenes and hints only).

## 9. Effects needed for a faithful look

| Effect | Assets |
|---|---|
| Spin ball / jump ball | jump-ball models (Sonic `0x8C1DBB2C`, Tails `0x8C271370`, Knuckles `0x8C364834`, Super Sonic `0x0C90BD04`); Sonic jump-ball effect shell `0x8C25654C` with SON_EFF `s_ball_a`, `s_ball_b` |
| Dash, homing, spin-dash trail | `sonic_dash` `0x8C255B10` + SON_EFF `s_dash01`-`s_dash23` (16x16 ARGB1555 sequence) |
| Light Speed Dash | S_MOT aura `0x0CC02AD4` + SON_EFF `lcharge01`-`04` |
| Super Sonic aura | `0x0C90C620`, `0x0C90CFA4`, `0x0C90D928` + SUPERSONIC `s1sonic*`, `s2sonic*`, `ss_dash*`; water splash `0x0C90DC14`, `0x0C90DF00` + `ss_waterl01`-`16` |
| Knuckles punch, Maximum Heat | `0x8C38D268`, `0x8C38DBA8` + KNU_EFF `k_punch*`, `k_combo01`, `k_mh*` |
| Amy hammer shockwave, hearts | `0x8C4CC538` + AMY_EFF `a_shock`, `a_heart2`, `a_piyo*` |
| Gamma shots, lock-on, hover | E102BEAM `e_sw*`, `beam`, `e102new*`; E102HIT `ebomb*`; E102EFFECT `pom*`, `e_jet*`; E102TIME (lock-on digits); models `0x8C431D88`, `0x8C431FB0`, `0x8C43222C`, `0x8C432584` |
| Ring sparkle, dust, splash, bubble, hit | EFF_REGULAR (texlist `0x8C1C3068`): `kiran` x4, `dust` x10, `splash_a` x8, `bubble`, `hit_l` x8, `hit` |
| Shields, invincibility | models Shield_Regular `0x8C1A0820`, Shield_Magnetic `0x8C1A03B0`, Invincibility_Ball `0x8C1A128C`, Invincibility_Beam `0x8C1A0938`; EFF_REGULAR `barria_t`/`barria_n`, `muteki`, `mutekiball` |
| Shadow | Character_Shadow_Blob `0x8C1ACA78` + OBJ_REGULAR `shadow` |
| Rings (collect, scatter on damage) | Ring `0x8C1A2204` + OBJ_REGULAR (texlist `0x8C19F530`) |
| Water ripple, upgrade pickup | Ripple_Effect `0x8C19FD0C`; Character_Upgrade_Effect, Light and Floor `0x8C1B7488`, `0x8C1B78E0`, `0x8C1B79E4` |

Sprite effects are drawn by code from `NJS_TEXANIM`/`NJS_SPRITE` data (texture id, size, UVs) that sits next to the effect
code in `1ST_READ.BIN`. Those tables are not catalogued yet. They are small, and the sizes can come from the decompiled code.

## 10. Proof of concept: `tools/research/rip_character.py`

```
py -3.12 tools/research/rip_character.py <game.gdi> <outdir> [--char sonic|tails|knuckles|amy|big|gamma|eggman|tikal|supersonic]
        [--action N [--frame F]] [--all-textures] [--preview [--yaw DEG]] [--list-actions]
```

* It uses only our own code: `tools/extract_gdi.py` (`parse_gdi`, `Track.read_sector`) and `tools/prs.py`
  (`decompress`). The ISO9660 single-file lookup, the Ninja model and motion readers, the PVM/PVR decoder, the PNG writer
  (zlib only) and the OBJ writer are new in this file. No PIL, numpy or SA Tools.
* Steps: open the `.gdi`, read `1ST_READ.BIN` and check its SHA-1, PRS-decompress any overlay needed and map it at its load address,
  walk the `NJS_OBJECT` tree, optionally apply one motion frame, triangulate into world space, read
  `SONICADV/<PVM>.PRS`, decode the textures and write `<char>.obj`, `.mtl` and `tex_NN_<name>.png`. `--preview` renders a
  z-buffered, textured PNG for checking.
* Results (bind pose; all with normals and UVs; 0 bad OBJ references; 0.2-0.6 s per run):

| Character | Root | Nodes | Vertices | Triangles | Textures used / in PVM |
|---|---|---|---|---|---|
| Sonic | `0x8C1CE76C` | 73 | 635 | 1001 | 16 / 28 |
| Super Sonic | `0x0C909D2C` (B_CHAOS7) | 73 | 642 | 1046 | 15 / 73 |
| Tails | `0x8C25F968` | 68 | 649 | 997 | 14 / 24 |
| Knuckles | `0x8C35B094` | 73 | 624 | 981 | 12 / 22 |
| Amy | `0x8C4C7C3C` | 78 | 656 | 1000 | 29 / 53 |
| Big | `0x8C52F298` | 73 | 729 | 1134 | 29 / 36 |
| Gamma | `0x8C42CC34` | 77 | 680 | 970 | 34 / 35 |
| Eggman | `0x8C57A6BC` | 24 | 834 | 1138 | 19 / 19 |
| Tikal | `0x0CB09818` (TIKAL_PROG) | 103 | 621 | 948 | 16 / 18 |

* What worked:
  * All 9 characters render correctly with their textures (front and side views).
  * Posed actions are correct: Sonic run (action 12, frame 8), curled spin (action 14, which switches to the 74-node object),
    carrying an object from `S_MOT.PRS` (action 34), and Tails, Amy, Big and Gamma idles.
  * Separate tests decoded every PVR layout on the disc: VQ, VQ+mips, small VQ (+mips), rectangle, and ARGB1555/4444 alpha.
  * Winding is consistent: the triangles are counter-clockwise against the normals.
* Not done in the PoC: welding (seams show in strongly bent poses), the swap-in of upgrade parts, environment-map UVs, sound
  decoding, and other disc versions (it refuses them on the SHA-1 check).

## 11. Requirements for the libsa1 asset layer

1. **Input and validation**: take a path to a `.gdi` (track files in the same folder; 2352- or 2048-byte sectors; the
   filesystem in the last data track). Hash `1ST_READ.BIN` and accept SHA-1 `e74c8498...` (US 1.005). Keep addresses in a
   per-version manifest (symbol to address and type), so that US 1.004, JP and INT can be added from the SA Tools configs.
   Never write game data anywhere except an optional user-local cache.
2. **Readers**: GDI/ISO9660 single-file reader, PRS decompressor, PVM parser, PVR decoder (3 pixel formats x
   twiddled/VQ/small-VQ/rect, with the mip-padding and small-VQ codebook rules in section 6.3). Optionally SMLT/MPB + AICA ADPCM, and ADX/AFS.
3. **Image manager**: map `1ST_READ.BIN` and the chosen overlays as separate images. Resolve pointers by physical
   address within the loaded set, and report a pointer into an unloaded image as missing (do not crash). Sonic needs
   `S_MOT` (and optionally `S_SBMOT`, `B_CHAOS7`, `STG02/04/06/07/08/09`); Knuckles, Amy, Big and Gamma need their `<c>_MOT`.
   All MOT files share `0x0CC00000`, so load only one character's MOT per image set, or keep one image set per character.
4. **Deserializers** to native structs: `NJS_OBJECT` trees (keep node order), basic `NJS_MODEL` (0x18-byte DC meshsets;
   all four poly encodings), `NJS_MATERIAL`, `NJS_TEXLIST`, `PL_ACTION` + `NJS_ACTION` + `NJS_MOTION` (factor layout from
   `type` and `inp_fn & 0xF`; `NJS_MKEY_F` and `NJS_MKEY_A`; shape keys optional), `PL_JOIN_VERTEX`, `player_parameter`,
   and the per-character PVM list at `0x8C1C2584`.
5. **Character descriptors** (manifest per character id): root object(s) (main, variants, curled, jump ball), action
   table and count, weld table, physics entry, PVM list, motion file, sound banks, and the upgrade/effect models in section 4.3.
6. **Runtime outputs** (in the spirit of libsm64's `SM64MarioGeometryBuffers`): each tick, the library evaluates the current
   action (motion frame, blending between actions using `PL_ACTION.frame`, playback speed `racio`, `next` chaining by
   `mtnmode`), applies the welds, and emits world-space triangles: position, normal, UV and texture id per corner, plus
   per-material flags (alpha, double-sided, ignore-light, env-map, clamp). About 1,000-1,150 triangles per character. Alternatively
   it emits node matrices for a skinned host mesh, but welding moves vertices, so a triangle-soup output is simpler.
7. **Textures to the host**: RGBA8 per texture id (no atlas is needed; characters use 12-34 textures, up to 64x64), plus
   clamp/filter flags. `gmod.md` recommends writing PNGs into `data/` and creating materials from them. Env-mapped
   materials need sphere-map UVs from view-space normals; compute them on the CPU per frame for those meshsets.
8. **Coordinate conversion** at the boundary: SA1 is Y-up, right-handed, models face -X; Source is Z-up, X-forward, Y-left,
   so use `(-x, z, y) * scale`. A scale of about 5-7 Source units per SA1 unit makes Sonic (9.7 units tall) roughly player-sized.
9. **What stays compiled into the library**: only the decompiled code (player state machines, physics routines,
   collision queries redirected to the host, the motion driver). All tables (physics, actions, welds, collision info,
   texture lists) are ripped by address, so no game data ships with the library.

## 12. Open questions

1. `mtnmode` 5, 6 and 12, and how `PL_ACTION.frame` (the blend value) is used: read the DC motion driver (the `mtnjvwk` users called from
   `func_8C0A77BC`).
2. Angle interpolation for keys more than 0x8000 apart (101 cases, 85 of them Eggman): plain integer or shortest path? Check the Ninja
   motion code in the library region (`0x8C5FBB60`+).
3. The 80 SA Tools-listed character motions that no action table references (section 5.2): are they reached through code
   (for example direct `NJS_ACTION*` or `NJS_MOTION*` references in the player code), or are they unused? Why is `s_mot.PRS` loaded
   for Tails, when none of Tails' actions use it?
4. Confirm the candidate collision info `0x8C1C5E80` (Sonic) and find the other characters' equivalents. Also confirm the
   part-swap table `0x8C1C5EE8` and the alternate head attaches (`*_z`, `amy_y`): when does the code swap them in?
5. The model-to-world placement: the root is at the waist (feet at y -5 for Sonic). Which offset does the display code add (for example
   `center_height`, or an `njTranslate` in the draw routine), and does it rotate by `0x8000 - ang.y` as SADX does?
6. Super Sonic's physics changes (code constants in Sonic's code or in B_CHAOS7) and his action/weld use outside the
   Perfect Chaos fight.
7. Sound: the MPB split/tone layout (format, loop and base-note fields), the SE id to (bank, request) table in
   `1ST_READ.BIN`, and whether any tones are PCM16/PCM8 rather than ADPCM.
8. Location of the `NJS_TEXANIM`/`NJS_SPRITE` tables for sprite effects (ring sparkle, dust, SON_EFF dash).
9. Other builds: generate manifests for US 1.004, JP and INT from the SA Tools configs. The overlay files (`*_MOT.PRS`, `B_CHAOS7.PRS`)
   may also differ between builds; hash them too.
10. Other dump formats (CHD, CDI, Redump cue/bin) are not handled. They would need their own readers or a one-time conversion step.

## 13. Sources and licences

* Measured directly on the user's disc (`sadc/disc`, `sadc/baserom`) with our own tools and the PoC. Addresses
  of named models and animations come from SA Tools `GameConfig/DC_SA1/*.ini` (US 1.005), cross-checked structurally.
  Physics addresses were re-derived (SA Tools is wrong here). Action locations, PVM lists, texlists for Super Sonic, Tikal and Eggman,
  weld semantics, PVR edge cases and sound bank layout were established in this study.
* X-Hax/sa_tools @ 5d52a39a04e0 (cloned to the scratchpad): read for the `PlayerParameter` field order, the `WeldType` enum,
  PVM flag names, meshset sizes, the UV scale and the rotation order. **No licence file in the repository** (only third-party
  licences under `Licenses/`), so treat it as all rights reserved: use it as a reference only and copy no code.
* X-Hax/sadx-mod-loader @ eb4cdcca9637 (`SADXModLoader/include`, sparse clone): `PL_ACTION`, `PL_JOIN_VERTEX`,
  `player_parameter`, `playerwk` and `mtnjvwk` field names (SADX PC, symbol-based names). Its README carries an
  "informational and educational purposes only" disclaimer and no open-source licence.
* libsm64/libsm64 @ fd11813208272b4271d92bd92feb8f3fdbe61be5: **CC0 1.0**. Its ROM-ripping model is the template here:
  `sm64_global_init(rom, ...)`, MIO0-decoded texture atlas, animation table read from a fixed ROM offset, and Mario's
  geometry imported from the decomp at build time.
* ckosmic/g64 @ 6de9c9eab3e4 and libsm64-gmod @ 198dc6383b94 (cloned as `g64` and `libsm64-gmod`): ROM path from the
  `g64_rompath` convar, then the binary module's `GlobalInit(path)` returns the atlases to Lua. **No licence file** in either
  repository. See `g64.md` for the full analysis.
* Our tools: `tools/extract_gdi.py`, `tools/prs.py` (project code).
