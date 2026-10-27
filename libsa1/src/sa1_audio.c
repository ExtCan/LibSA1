/*
 * libsa1 audio: disc access, sound banks, tone resolution, cues and the public data API
 * (include/libsa1_audio.h).  Research: sadc docs/research/sa1_assets.md section 8 and the sound code of
 * 1ST_READ.BIN: 8C09340C (bank loading per stage and character), 8C0940DA / 8C09413A / 8C0941FC /
 * 8C094330 / 8C0943E0 (play), 8C0945A0 / 8C0945D0 (stop), 8C0948A0 (the sound task: tone -> bank and
 * request through the table at 0x8C1C19B4, program change + note 60 on a MIDI port), 8C093F02 (the 3D
 * slot tones that play a sequence), 8C0953C6 (3D radius table), 8C107864 (voices: ADXT_StartAfs on
 * event_adx[_us].afs) and the music name table at 0x8C5FB8A8.
 *
 * Every byte read from the disc is untrusted: all offsets are checked against the buffer they index.
 */
#include "sa1_audio.h"

#include <math.h>
#include <stdio.h>

#define FIRST_READ_BASE   0x8C010000u
#define ADDR_TONE_RANGES  0x8C1C19B4u   /* {s32 threshold, s32 bank}[], descending, ends with a negative one */
#define ADDR_3D_TONES     0x8C1C1BB4u   /* s16 tone[35] */
#define ADDR_3D_PARAMS    0x8C1C1BFEu   /* {s16 radius, s16 alternate tone, s16, s16}[35] */
#define NUM_3D            35u
#define ADDR_MUSIC_NAMES  0x8C5FB8A8u   /* const char *name[] ("ADVAMY.ADX", ...) */

/* the game's sound test in the menu overlay (sadc baserom/overlays.json "ADVERTISE"; jukebox.md 1.4 and its Review
 * R1 / R2.1): six {record *, count} pages at 0x8C90F90C (read by the menu code 8C907B80), records {char *title; s32
 * music} (104 in all, from 0x8C90F5CC); the chosen record's music goes to 8C901940, which re-creates the music task
 * and requests it through 8C107AC6 */
#define ADV_FILE          "ADVERTISE.PRS"
#define ADV_SHA1          "5abb7a8b0d85e90b4ee9ee6815a5ad10ca5a81e7"   /* the decompressed overlay */
#define ADV_LOAD          0x8C900000u
#define ADV_PAGES         0x8C90F90Cu
#define ADV_NUM_PAGES     6u
#define ADV_MAX_PER_PAGE  64u
#define ADV_MAX_TITLE     64u           /* bytes of a title without its NUL */
#define MAX_ADV_PRS       (1u << 20)
#define MAX_ADV_SIZE      (4u << 20)
#define ADX_HEADER_READ   4096u         /* sa1_audio_music_info reads this much of a file (the largest data offset on
                                           the disc is 2,096) */

#define MAX_MLT_SIZE      (8u << 20)
#define MAX_ADX_SIZE      (32u << 20)
#define MAX_VOICE_SIZE    (2u << 20)

/* The music numbers the game plays as jingles: 8C107C80 requests the track without saving it and starts task
 * 8C107CC2, which requests the saved background track again (from its beginning) when the jingle has ended.
 * Generated from every call of 8C107C80 in sadc/asm whose R4 is a constant (sadc/build/research/jukebox/
 * music_calls.py -> music_calls.json; no call passes a computed number), call address (function):
 *   26 CONTINUE.ADX  8C04D5E6 (8C04CA40), 8C04E194 (8C04DA20), STG00 0C903210 (0C903140), MINICART 0C90A926 (0C90A81E)
 *   44 FISHGET.ADX   8C0E6466 (8C0E6446)                     45 FISHING.ADX   8C0EB25A (8C0EB244)
 *   46 FISHMISS.ADX  8C0E88B4 (8C0E8562)                     55 INVNCIBL.ADX  8C07D4FE (8C07D4A0)
 *   56 ITEM1.ADX     ADV00 0C901050 (0C900E70), 0C906F50 (0C906E80), 0C90D652, 0C90D694 (0C90D480)
 *   85 SPEEDUP.ADX   8C07D34E (8C07D2DA)                     96 TIMER.ADX     8C07BFDA (8C07BE6E)
 * These eight are SA1_AUDIO_MUSIC_JINGLE; what each is for is sa1_audio_music_state.jingle_kind (header version 4). */
int32_t sa1a_jingle_kind(uint32_t music)
{
    switch (music) {
    case 96: return SA1_AUDIO_JINGLE_COUNTDOWN;     /* 8C07BE6E: the drowning countdown */
    case 55: return SA1_AUDIO_JINGLE_INVINCIBLE;    /* 8C07D4A0: the invincibility item */
    case 85: return SA1_AUDIO_JINGLE_SPEED_UP;      /* 8C07D2DA: the speed shoes */
    case 45: return SA1_AUDIO_JINGLE_FISHING;       /* 8C0EB244 (with Big's fishing display task 8C0EB184) */
    case 44: return SA1_AUDIO_JINGLE_FISH_CAUGHT;   /* 8C0E6446 */
    case 46: return SA1_AUDIO_JINGLE_FISH_LOST;     /* 8C0E8562: the line breaks at tension 1.0 */
    case 56: return SA1_AUDIO_JINGLE_ITEM;          /* ADV00 0C900E70, 0C906E80, 0C90D480 */
    case 26: return SA1_AUDIO_JINGLE_CONTINUE;      /* 8C04CA40, 8C04DA20, STG00 0C903140, MINICART 0C90A81E */
    default: return SA1_AUDIO_JINGLE_OTHER;
    }
}

/* the stage MLT of each stage number: 8C09340C switches on GetStageNumber() >> 8 (43 cases; NULL = its
 * default, drymountain.mlt); Sky Deck has an English variant */
static const char *const k_stage_mlt[43] = {
    "EGGCARRIER.MLT", "EMERALDCOAST.MLT", "WINDYVALLEY.MLT", "TWINKLEPARK.MLT", "HIGHWAY.MLT",
    "DRYMOUNTAIN.MLT", "SKYDECK.MLT", "LOSTWORLD.MLT", "ICECAP.MLT", "CASINOPOLIS.MLT", "FINALEGG.MLT", NULL,
    "HOTSHELTER.MLT", NULL, NULL, "CHAOS0.MLT", "CHAOS2.MLT", "CHAOS4.MLT", "CHAOS6.MLT", "CHAOS7B.MLT",
    "MOBILE1.MLT", "MOBILE2.MLT", "MOBILE3.MLT", NULL, "E101.MLT", "E101R.MLT", "STATIONSQUARE.MLT",
    "STATIONSQUARE.MLT", "STATIONSQUARE.MLT", "EGGCARRIER.MLT", "EGGCARRIER.MLT", "EGGCARRIER.MLT",
    "EGGCARRIER.MLT", "MYSTICRUIN.MLT", "MYSTICRUIN.MLT", "CART.MLT", "SHOOTING.MLT", "SHOOTING.MLT",
    "SANDHILL.MLT", "ALIFE.MLT", "ALIFE.MLT", "ALIFE.MLT", "ALIFE.MLT"
};

/* character sound groups (8C09340C after the stage MLT: by GetPlayerNumber(); Sonic, Tails and the
 * default share the SONICTAILS files), with the threshold of their tone range in banks 3 and 6 */
static const char *const k_group_name[SA1A_NUM_GROUPS] = { "SONICTAILS", "KNUCKLES", "AMY", "E102", "BIG" };
static const int32_t k_group_bank3[SA1A_NUM_GROUPS] = { 722, 736, 751, 776, 795 };
static const int32_t k_group_bank6[SA1A_NUM_GROUPS] = { 1111, 1144, 1168, 1186, 1199 };

static uint32_t char_group(int32_t kind)
{
    switch (kind) {
    case SA1_CHAR_KNUCKLES: return 1;
    case SA1_CHAR_AMY:      return 2;
    case SA1_CHAR_GAMMA:    return 3;
    case SA1_CHAR_BIG:      return 4;
    default:                return 0;
    }
}

/* 8C093F02: tones that take one of the 3D slots (32..36); 8C093B2A then plays the bank's sequence */
static int is_sequence_tone(uint32_t t)
{
    static const uint16_t k[] = { 66, 67, 124, 160, 161, 239, 553, 554, 667, 1027, 1029, 1039, 1059, 1080, 1273 };
    size_t i;
    if ((t >= 777 && t <= 781) || (t >= 1215 && t <= 1222) || (t >= 1264 && t <= 1271))
        return 1;
    for (i = 0; i < sizeof k / sizeof k[0]; i++)
        if (k[i] == t)
            return 1;
    return 0;
}

const char *sa1_audio_stage_file(int32_t stage, uint32_t flags)
{
    if (stage == SA1_AUDIO_STAGE_DEFAULT)
        stage = 1;
    if (stage < 0 || stage >= 43)
        return NULL;
    if (stage == 6)
        return (flags & SA1_AUDIO_JAPANESE) ? "SKYDECK.MLT" : "SKYDECK_E.MLT";
    return k_stage_mlt[stage] ? k_stage_mlt[stage] : "DRYMOUNTAIN.MLT";
}

/* ------------------------------------------------------------------------- */
/* ISO9660 (the SONICADV directory)                                          */
/* ------------------------------------------------------------------------- */

static int name_ieq(const uint8_t *a, const char *b, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        int x = a[i], y = (unsigned char)b[i];
        if (x >= 'a' && x <= 'z')
            x -= 32;
        if (y >= 'a' && y <= 'z')
            y -= 32;
        if (x != y)
            return 0;
    }
    return 1;
}

static sa1_status dir_find(const uint8_t *dir, uint32_t dir_size, const char *name, uint32_t *lba, uint32_t *size,
                           int *is_dir)
{
    size_t nlen = strlen(name);
    uint32_t pos = 0;
    while (pos < dir_size) {
        const uint8_t *r = dir + pos;
        uint32_t rlen = r[0], nl, k;
        if (rlen == 0) {
            pos = (pos / 2048u + 1u) * 2048u;
            continue;
        }
        if (rlen < 34 || rlen > dir_size - pos)
            return SA1_ERR_BAD_FORMAT;
        nl = r[32];
        if (33u + nl > rlen)
            return SA1_ERR_BAD_FORMAT;
        for (k = 0; k < nl && r[33 + k] != ';'; k++)
            ;
        while (k > 0 && r[33 + k - 1] == '.')
            k--;
        if (k == nlen && name_ieq(r + 33, name, nlen)) {
            if (r[26] != 0 || r[27] != 0 || (r[25] & 0x80))
                return SA1_ERR_UNSUPPORTED;
            *lba = sa1_rd32(r + 2);
            *size = sa1_rd32(r + 10);
            *is_dir = (r[25] & 2) != 0;
            return SA1_OK;
        }
        pos += rlen;
    }
    return SA1_ERR_NOT_FOUND;
}

/* Reads `len` bytes at `off` of a file that starts at sector `lba` and is `file_size` bytes long. */
static sa1_status read_range(sa1_audio *a, uint32_t lba, uint32_t file_size, uint32_t off, uint32_t len, uint8_t *out)
{
    uint32_t first, nsec;
    uint8_t *tmp;
    sa1_status st;
    if (off > file_size || len > file_size - off)
        return SA1_ERR_BAD_FORMAT;
    if (len == 0)
        return SA1_OK;
    first = off / 2048u;
    nsec = (uint32_t)(((uint64_t)off + len + 2047u) / 2048u) - first;
    if (lba > 0xFFFFFFFFu - first)
        return SA1_ERR_BAD_FORMAT;
    tmp = (uint8_t *)sa1_mem_alloc(a->ctx, (size_t)nsec * 2048u);
    if (!tmp)
        return SA1_ERR_OUT_OF_MEMORY;
    sa1a_disc_lock(a);      /* the loader thread reads too (sa1_audio_load.c) */
    st = sa1_disc_read_sectors(&a->disc, lba + first, nsec, tmp);
    sa1a_disc_unlock(a);
    if (st == SA1_OK)
        memcpy(out, tmp + (off % 2048u), len);
    sa1_mem_free(a->ctx, tmp);
    return st;
}

static sa1_status sonicadv_locate(sa1_audio *a, const char *name, uint32_t *lba, uint32_t *size)
{
    int is_dir = 0;
    sa1_status st;
    if (!a->sonicadv_dir)
        return SA1_ERR_NOT_FOUND;
    st = dir_find(a->sonicadv_dir, a->sonicadv_size, name, lba, size, &is_dir);
    if (st == SA1_OK && is_dir)
        st = SA1_ERR_NOT_FOUND;
    return st;
}

static sa1_status read_sonicadv(sa1_audio *a, const char *name, uint32_t max, uint8_t **data, uint32_t *size)
{
    uint32_t lba, fsize;
    uint8_t *buf;
    sa1_status st = sonicadv_locate(a, name, &lba, &fsize);
    *data = NULL;
    *size = 0;
    if (st != SA1_OK)
        return st;
    if (fsize > max)
        return SA1_ERR_LIMIT;
    buf = (uint8_t *)sa1_mem_alloc(a->ctx, fsize ? fsize : 1u);
    if (!buf)
        return SA1_ERR_OUT_OF_MEMORY;
    st = read_range(a, lba, fsize, 0, fsize, buf);
    if (st != SA1_OK) {
        sa1_mem_free(a->ctx, buf);
        return st;
    }
    *data = buf;
    *size = fsize;
    return SA1_OK;
}

static sa1_status load_sonicadv_dir(sa1_audio *a)
{
    uint8_t *root;
    uint32_t nsec, lba, size;
    int is_dir = 0;
    sa1_status st;
    if (a->disc.root_size == 0 || a->disc.root_size > SA1_MAX_DIR_SIZE)
        return SA1_ERR_BAD_FORMAT;
    nsec = (a->disc.root_size + 2047u) / 2048u;
    root = (uint8_t *)sa1_mem_alloc(a->ctx, (size_t)nsec * 2048u);
    if (!root)
        return SA1_ERR_OUT_OF_MEMORY;
    st = sa1_disc_read_sectors(&a->disc, a->disc.root_lba, nsec, root);
    if (st == SA1_OK)
        st = dir_find(root, a->disc.root_size, "SONICADV", &lba, &size, &is_dir);
    sa1_mem_free(a->ctx, root);
    if (st != SA1_OK)
        return st;
    if (!is_dir || size == 0 || size > SA1_MAX_DIR_SIZE)
        return SA1_ERR_BAD_FORMAT;
    nsec = (size + 2047u) / 2048u;
    a->sonicadv_dir = (uint8_t *)sa1_mem_alloc(a->ctx, (size_t)nsec * 2048u);
    if (!a->sonicadv_dir)
        return SA1_ERR_OUT_OF_MEMORY;
    st = sa1_disc_read_sectors(&a->disc, lba, nsec, a->sonicadv_dir);
    if (st != SA1_OK) {
        sa1_mem_free(a->ctx, a->sonicadv_dir);
        a->sonicadv_dir = NULL;
        return st;
    }
    a->sonicadv_lba = lba;
    a->sonicadv_size = size;
    return SA1_OK;
}

/* which music files are on the disc, and where: once, at open (after load_tables and load_sonicadv_dir), so that
 * sa1a_adx_exists is a bit test on the mixer's and the loader's threads */
static void load_music_files(sa1_audio *a)
{
    uint32_t i;
    memset(a->music_on_disc, 0, sizeof a->music_on_disc);
    for (i = 0; i < SA1A_MAX_MUSIC; i++) {
        sa1a_music *e = &a->music_files[i];
        memset(e, 0, sizeof *e);
        e->title = e->page = e->entry = -1;
        if (i < a->num_music && sonicadv_locate(a, a->music[i], &e->lba, &e->size) == SA1_OK)
            a->music_on_disc[i / 32u] |= 1u << (i % 32u);
    }
}

static int music_on_disc(const sa1_audio *a, uint32_t music)
{
    return music < a->num_music && (a->music_on_disc[music / 32u] >> (music % 32u)) & 1u;
}

/* ------------------------------------------------------------------------- */
/* MLT files and banks                                                       */
/* ------------------------------------------------------------------------- */
/*
 * SMLT: "SMLT", u32 version (0x101), u32 entry count, 0x20 bytes header; entries of 0x20 bytes: magic
 * (SMPB program bank, SMSB sequence bank, SFPB/SFOB/SFPW DSP), u32 bank, u32 AICA address, u32 AICA size,
 * u32 file offset (0xFFFFFFFF: not in this file), u32 size, 8 bytes 0.
 * SMPB: "SMPB", u32 version 1, u32 size, u32 0, then {u32 offset, u32 count} pairs: programs (a table of
 * u32 program offsets), velocity curves (128 bytes each), two more tables.
 * SMSB: "SMSB", u32 version, u32 size, u32 sequence count, u32 sequence offsets[count] ("SMSD" each).
 */
static sa1_status mlt_section(const uint8_t *d, uint32_t len, const char *magic, uint32_t bank, uint32_t *off,
                              uint32_t *size)
{
    uint32_t n, i;
    if (len < 0x20 || memcmp(d, "SMLT", 4) != 0)
        return SA1_ERR_BAD_FORMAT;
    n = sa1_rd32(d + 8);
    if (n > 64 || 0x20u + n * 0x20u > len)
        return SA1_ERR_BAD_FORMAT;
    for (i = 0; i < n; i++) {
        const uint8_t *e = d + 0x20u + i * 0x20u;
        if (memcmp(e, magic, 4) == 0 && sa1_rd32(e + 4) == bank) {
            uint32_t o = sa1_rd32(e + 16), s = sa1_rd32(e + 20);
            if (o == 0xFFFFFFFFu)
                return SA1_ERR_NOT_FOUND;
            if (o > len || s > len - o)
                return SA1_ERR_BAD_FORMAT;
            *off = o;
            *size = s;
            return SA1_OK;
        }
    }
    return SA1_ERR_NOT_FOUND;
}

static void bank_free(sa1_audio *a, sa1a_bank *b)
{
    uint32_t i;
    for (i = 0; i < b->pcm.count; i++)
        sa1_mem_free(a->ctx, SA1_VEC(b->pcm, sa1a_pcm)[i].pcm);
    sa1_vec_free(a->ctx, &b->pcm);
    sa1_mem_free(a->ctx, b->mpb);
    sa1_mem_free(a->ctx, b->msb);
    memset(b, 0, sizeof *b);
}

static sa1_status bank_load(sa1_audio *a, sa1a_bank *b, const uint8_t *mlt, uint32_t len, uint32_t game_bank,
                            const char *file)
{
    uint32_t off, size, ptab, pcnt, i;
    sa1_status st = mlt_section(mlt, len, "SMPB", game_bank, &off, &size);
    bank_free(a, b);
    if (st != SA1_OK)
        return st;
    if (size < 0x30 || memcmp(mlt + off, "SMPB", 4) != 0)
        return SA1_ERR_BAD_FORMAT;
    ptab = sa1_rd32(mlt + off + 0x10);
    pcnt = sa1_rd32(mlt + off + 0x14);
    if (pcnt > 1024 || ptab > size || pcnt * 4u > size - ptab)
        return SA1_ERR_BAD_FORMAT;
    b->mpb = (uint8_t *)sa1_mem_alloc(a->ctx, size);
    if (!b->mpb)
        return SA1_ERR_OUT_OF_MEMORY;
    memcpy(b->mpb, mlt + off, size);
    b->mpb_size = size;
    b->num_programs = pcnt;
    {
        uint32_t vo = sa1_rd32(b->mpb + 0x18), vn = sa1_rd32(b->mpb + 0x1C);
        if (vn >= 1 && vo <= size && size - vo >= 128u)
            memcpy(b->vel_curve, b->mpb + vo, 128);
        else
            for (i = 0; i < 128; i++)
                b->vel_curve[i] = (uint8_t)i;
    }
    if (mlt_section(mlt, len, "SMSB", game_bank, &off, &size) == SA1_OK && size >= 0x10 &&
        memcmp(mlt + off, "SMSB", 4) == 0) {
        uint32_t n = sa1_rd32(mlt + off + 12);
        if (n <= 4096 && 16u + n * 4u <= size) {
            b->msb = (uint8_t *)sa1_mem_alloc(a->ctx, size);
            if (!b->msb) {
                bank_free(a, b);
                return SA1_ERR_OUT_OF_MEMORY;
            }
            memcpy(b->msb, mlt + off, size);
            b->msb_size = size;
            b->num_sequences = n;
        }
    }
    b->pcm.elem = sizeof(sa1a_pcm);
    b->game_bank = game_bank;
    snprintf(b->file, sizeof b->file, "%s", file);
    b->loaded = 1;
    return SA1_OK;
}

/* Loads the given game banks of one MLT into `banks` (indexed by game bank when stage != 0, else banks[0]).
 * Returns SA1_OK, or the status of reading the file. */
static sa1_status load_mlt(sa1_audio *a, const char *file, const uint32_t *game_banks, uint32_t n,
                           sa1a_bank *const *targets)
{
    uint8_t *data;
    uint32_t size, i;
    sa1_status st = read_sonicadv(a, file, MAX_MLT_SIZE, &data, &size);
    if (st != SA1_OK) {
        sa1_log(a->ctx, SA1_LOG_WARN, "audio: cannot read SONICADV/%s (%s)", file, sa1_status_string(st));
        return st;
    }
    for (i = 0; i < n; i++) {
        sa1_status bs = bank_load(a, targets[i], data, size, game_banks[i], file);
        if (bs == SA1_ERR_OUT_OF_MEMORY) {
            st = bs;
            break;
        }
        if (bs != SA1_OK && bs != SA1_ERR_NOT_FOUND)
            sa1_log(a->ctx, SA1_LOG_WARN, "audio: %s bank %u: %s", file, (unsigned)game_banks[i],
                    sa1_status_string(bs));
    }
    sa1_mem_free(a->ctx, data);
    return st;
}

static sa1_status load_stage_banks(sa1_audio *a, int32_t stage)
{
    static const uint32_t k_banks[5] = { 0, 1, 2, 4, 5 };
    sa1a_bank *t[5];
    const char *file = sa1_audio_stage_file(stage, a->flags);
    uint32_t i;
    if (!file)
        return SA1_ERR_INVALID_ARGUMENT;
    for (i = 0; i < 5; i++)
        t[i] = &a->stage_banks[k_banks[i]];
    return load_mlt(a, file, k_banks, 5, t);
}

/* stage rules: reads `stage`'s MLT and loads the game banks in `banks` into out[] (indexed by game bank). Called by the
 * loader thread without any lock (read_range takes the disc lock) and never logs; *loaded gets a bit per bank loaded. */
sa1_status sa1a_set_read(sa1_audio *a, int32_t stage, uint32_t banks, uint32_t set, sa1a_bank *out, uint32_t *loaded)
{
    static const uint32_t k_banks[5] = { 0, 1, 2, 4, 5 };
    const char *file = sa1_audio_stage_file(stage, a->flags);
    uint8_t *data;
    uint32_t size, i;
    sa1_status st;
    *loaded = 0;
    if (!file)
        return SA1_ERR_INVALID_ARGUMENT;
    st = read_sonicadv(a, file, MAX_MLT_SIZE, &data, &size);
    if (st != SA1_OK)
        return st;
    for (i = 0; i < 5; i++) {
        uint32_t gb = k_banks[i];
        if (!(banks & (1u << gb)))
            continue;
        if (bank_load(a, &out[gb], data, size, gb, file) == SA1_OK) {
            out[gb].set = set;
            *loaded |= 1u << gb;
        }
    }
    sa1_mem_free(a->ctx, data);
    return SA1_OK;
}

void sa1a_bank_free(sa1_audio *a, sa1a_bank *b)
{
    bank_free(a, b);
}

static sa1_status load_group_banks(sa1_audio *a, uint32_t g)
{
    char name[48];
    uint32_t b3 = 3, b6 = 6;
    sa1a_bank *t;
    sa1_status st, st2;
    snprintf(name, sizeof name, "P_%s.MLT", k_group_name[g]);
    t = &a->player[g];
    st = load_mlt(a, name, &b3, 1, &t);
    snprintf(name, sizeof name, "V_%s%s.MLT", k_group_name[g], (a->flags & SA1_AUDIO_JAPANESE) ? "" : "_E");
    t = &a->voiceb[g];
    st2 = load_mlt(a, name, &b6, 1, &t);
    return st != SA1_OK ? st : st2;
}

/* ------------------------------------------------------------------------- */
/* Tables of 1ST_READ.BIN, the voice archive                                 */
/* ------------------------------------------------------------------------- */

static sa1_status load_tables(sa1_audio *a)
{
    uint8_t *d = NULL;
    uint32_t n = 0, i, off;
    char hex[41];
    sa1_status st = sa1_disc_read_file(&a->disc, SA1_FIRST_READ_NAME, &d, &n);
    if (st != SA1_OK)
        return sa1_fail(a->ctx, st, "audio: cannot read %s (%s)", SA1_FIRST_READ_NAME, sa1_status_string(st));
    sa1_sha1_hex(d, n, hex);
    if (strcmp(hex, SA1_FIRST_READ_SHA1) != 0 && !(a->flags & SA1_AUDIO_ACCEPT_MODIFIED)) {
        sa1_mem_free(a->ctx, d);
        return sa1_fail(a->ctx, SA1_ERR_BAD_HASH, "audio: %s is not the supported version (SHA-1 %s)",
                        SA1_FIRST_READ_NAME, hex);
    }
    /* tone ranges */
    a->num_ranges = 0;
    for (i = 0; i < SA1A_MAX_RANGES; i++) {
        int32_t th, bk;
        off = ADDR_TONE_RANGES - FIRST_READ_BASE + i * 8u;
        if (off + 8u > n)
            break;
        th = sa1_rds32(d + off);
        bk = sa1_rds32(d + off + 4);
        if (th < 0)
            break;
        if (bk < 0 || bk >= (int32_t)SA1A_GAME_BANKS || th > (int32_t)SA1A_MAX_TONE ||
            (i > 0 && th >= a->ranges[i - 1].threshold)) {
            st = SA1_ERR_BAD_FORMAT;
            break;
        }
        a->ranges[i].threshold = th;
        a->ranges[i].bank = bk;
        a->num_ranges = i + 1;
    }
    if (st == SA1_OK && a->num_ranges == 0)
        st = SA1_ERR_BAD_FORMAT;
    /* 3D tones and their radius */
    a->num_3d = 0;
    for (i = 0; st == SA1_OK && i < NUM_3D; i++) {
        uint32_t o1 = ADDR_3D_TONES - FIRST_READ_BASE + i * 2u, o2 = ADDR_3D_PARAMS - FIRST_READ_BASE + i * 8u;
        if (o1 + 2u > n || o2 + 2u > n)
            break;
        a->tone3d[i] = (int16_t)sa1_rd16(d + o1);
        a->radius3d[i] = (int16_t)sa1_rd16(d + o2);
        a->num_3d = i + 1;
    }
    /* music names */
    a->num_music = 0;
    for (i = 0; st == SA1_OK && i < SA1A_MAX_MUSIC; i++) {
        uint32_t ptr, so, k;
        off = ADDR_MUSIC_NAMES - FIRST_READ_BASE + i * 4u;
        if (off + 4u > n)
            break;
        ptr = sa1_rd32(d + off);
        if (ptr < FIRST_READ_BASE || ptr - FIRST_READ_BASE >= n)
            break;
        so = ptr - FIRST_READ_BASE;
        for (k = 0; k < 15 && so + k < n && d[so + k]; k++) {
            char ch = (char)d[so + k];
            if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_' ||
                  ch == '.'))
                break;
            a->music[i][k] = (ch >= 'a' && ch <= 'z') ? (char)(ch - 32) : ch;
        }
        a->music[i][k] = 0;
        if (k < 5 || so + k >= n || d[so + k] != 0 || strcmp(a->music[i] + k - 4, ".ADX") != 0) {
            a->music[i][0] = 0;
            break;
        }
        a->num_music = i + 1;
    }
    sa1_mem_free(a->ctx, d);
    if (st != SA1_OK)
        return sa1_fail(a->ctx, st, "audio: the sound tables of %s are malformed", SA1_FIRST_READ_NAME);
    return SA1_OK;
}

static sa1_status load_afs(sa1_audio *a)
{
    const char *name = (a->flags & SA1_AUDIO_JAPANESE) ? "EVENT_ADX.AFS" : "EVENT_ADX_US.AFS";
    uint8_t head[8];
    uint8_t *toc;
    uint32_t lba, size, n, i;
    sa1_status st = sonicadv_locate(a, name, &lba, &size);
    if (st == SA1_OK)
        st = read_range(a, lba, size, 0, 8, head);
    if (st == SA1_OK && (memcmp(head, "AFS", 4) != 0 || (n = sa1_rd32(head + 4)) == 0 || n > SA1A_MAX_VOICES))
        st = SA1_ERR_BAD_FORMAT;
    if (st != SA1_OK) {
        sa1_log(a->ctx, SA1_LOG_WARN, "audio: no voice archive SONICADV/%s (%s)", name, sa1_status_string(st));
        return st;
    }
    toc = (uint8_t *)sa1_mem_alloc(a->ctx, (size_t)n * 8u);
    a->afs_toc = (uint32_t *)sa1_mem_array(a->ctx, (size_t)n * 2u, sizeof(uint32_t));
    if (!toc || !a->afs_toc) {
        sa1_mem_free(a->ctx, toc);
        sa1_mem_free(a->ctx, a->afs_toc);
        a->afs_toc = NULL;
        return SA1_ERR_OUT_OF_MEMORY;
    }
    st = read_range(a, lba, size, 8, n * 8u, toc);
    for (i = 0; st == SA1_OK && i < n; i++) {
        uint32_t o = sa1_rd32(toc + i * 8u), s = sa1_rd32(toc + i * 8u + 4u);
        if (o > size || s > size - o || s > MAX_VOICE_SIZE)
            o = s = 0;                          /* an unusable entry plays nothing */
        a->afs_toc[i * 2u] = o;
        a->afs_toc[i * 2u + 1u] = s;
    }
    sa1_mem_free(a->ctx, toc);
    if (st != SA1_OK) {
        sa1_mem_free(a->ctx, a->afs_toc);
        a->afs_toc = NULL;
        return st;
    }
    a->afs_lba = lba;
    a->afs_size = size;
    a->num_voices = n;
    return SA1_OK;
}

sa1_status sa1a_adx_exists(const sa1_audio *a, int32_t source, uint32_t index)
{
    if (source == SA1_AUDIO_SOURCE_VOICE) {
        if (!a->afs_toc)
            return SA1_ERR_NOT_LOADED;
        if (index >= a->num_voices || a->afs_toc[index * 2u + 1u] == 0)
            return SA1_ERR_NOT_FOUND;
        return SA1_OK;
    }
    if (source == SA1_AUDIO_SOURCE_MUSIC) {
        if (a->flags & SA1_AUDIO_NO_MUSIC)
            return SA1_ERR_NOT_LOADED;
        return music_on_disc(a, index) ? SA1_OK : SA1_ERR_NOT_FOUND;   /* 69 NIGHTS_K.ADX: named, not on the disc */
    }
    return SA1_ERR_INVALID_ARGUMENT;
}

sa1_status sa1a_read_adx(sa1_audio *a, int32_t source, uint32_t index, uint8_t **data, uint32_t *size)
{
    *data = NULL;
    *size = 0;
    if (source == SA1_AUDIO_SOURCE_VOICE) {
        uint32_t o, s;
        uint8_t *buf;
        sa1_status st;
        if (!a->afs_toc)
            return SA1_ERR_NOT_LOADED;
        if (index >= a->num_voices)
            return SA1_ERR_NOT_FOUND;
        o = a->afs_toc[index * 2u];
        s = a->afs_toc[index * 2u + 1u];
        if (s == 0)
            return SA1_ERR_NOT_FOUND;
        buf = (uint8_t *)sa1_mem_alloc(a->ctx, s);
        if (!buf)
            return SA1_ERR_OUT_OF_MEMORY;
        st = read_range(a, a->afs_lba, a->afs_size, o, s, buf);
        if (st != SA1_OK) {
            sa1_mem_free(a->ctx, buf);
            return st;
        }
        *data = buf;
        *size = s;
        return SA1_OK;
    }
    if (source == SA1_AUDIO_SOURCE_MUSIC) {
        const sa1a_music *e;
        uint8_t *buf;
        sa1_status st;
        if (a->flags & SA1_AUDIO_NO_MUSIC)
            return SA1_ERR_NOT_LOADED;
        if (!music_on_disc(a, index))
            return SA1_ERR_NOT_FOUND;
        e = &a->music_files[index];
        if (e->size > MAX_ADX_SIZE)
            return SA1_ERR_LIMIT;
        buf = (uint8_t *)sa1_mem_alloc(a->ctx, e->size ? e->size : 1u);
        if (!buf)
            return SA1_ERR_OUT_OF_MEMORY;
        st = read_range(a, e->lba, e->size, 0, e->size, buf);
        if (st != SA1_OK) {
            sa1_mem_free(a->ctx, buf);
            return st;
        }
        *data = buf;
        *size = e->size;
        return SA1_OK;
    }
    return SA1_ERR_INVALID_ARGUMENT;
}

/* ------------------------------------------------------------------------- */
/* Programs, layers, samples                                                 */
/* ------------------------------------------------------------------------- */

static const sa1a_pcm *bank_pcm(sa1_audio *a, sa1a_bank *b, uint32_t addr, uint32_t fmt, uint32_t count)
{
    uint32_t i, bytes;
    int32_t peak = 0;
    sa1a_pcm *e;
    int16_t *pcm;
    for (i = 0; i < b->pcm.count; i++) {
        e = &SA1_VEC(b->pcm, sa1a_pcm)[i];
        if (e->addr == addr && e->fmt == fmt && e->count == count)
            return e;
    }
    bytes = fmt == SA1_AUDIO_FORMAT_PCM16 ? count * 2u : fmt == SA1_AUDIO_FORMAT_PCM8 ? count : (count + 1u) / 2u;
    if (count == 0 || addr > b->mpb_size || bytes > b->mpb_size - addr)
        return NULL;
    pcm = (int16_t *)sa1_mem_array(a->ctx, count, sizeof(int16_t));
    if (!pcm)
        return NULL;
    if (fmt == SA1_AUDIO_FORMAT_PCM16) {
        for (i = 0; i < count; i++)
            pcm[i] = (int16_t)sa1_rd16(b->mpb + addr + i * 2u);
    } else if (fmt == SA1_AUDIO_FORMAT_PCM8) {
        for (i = 0; i < count; i++)
            pcm[i] = (int16_t)((int32_t)(int8_t)b->mpb[addr + i] * 256);
    } else {
        sa1a_adpcm_decode(b->mpb + addr, count, pcm);
    }
    for (i = 0; i < count; i++) {
        int32_t x = pcm[i] < 0 ? -(int32_t)pcm[i] : pcm[i];
        if (x > peak)
            peak = x;
    }
    e = (sa1a_pcm *)sa1_vec_push(a->ctx, &b->pcm);
    if (!e) {
        sa1_mem_free(a->ctx, pcm);
        return NULL;
    }
    e->addr = addr;
    e->fmt = fmt;
    e->count = count;
    e->pcm = pcm;
    e->peak = peak;
    return e;
}

static float db_to_gain(float db) { return (float)pow(10.0, (double)db / 20.0); }

/* One split -> a layer as the AICA plays it for `note`.  Split (0x30 bytes): AICA channel registers 0x00-0x2A
 * (0 SA high/PCMS/LPCTL, 2 SA, 4 LSA, 6 LEA, 8 AR/D1R/D2R, A RR/DL/KRS/LPSLNK, C OCT/FNS, E LFO, 10 ISEL/IMXL,
 * 12 DIPAN/DISDL, 14 Q/LPOFF/TL, 16 FLV0-4, 20 FAR/FD1R/FD2R/FRR), then 24 low note, 25 high note, 26 base
 * note, 27 fine tune (1/256 semitone), 28 u16 0xFFFF, 2A two bytes, 2C 0x7F. */
static int split_layer(sa1_audio *a, sa1a_bank *b, const uint8_t *d, uint32_t delay_ms, uint8_t bend_up,
                       uint8_t bend_down, int32_t note, sa1a_layer *L)
{
    uint32_t r0 = sa1_rd16(d), sa = sa1_rd16(d + 2), lsa = sa1_rd16(d + 4), lea = sa1_rd16(d + 6);
    uint32_t eg1 = sa1_rd16(d + 8), eg2 = sa1_rd16(d + 10), pitch = sa1_rd16(d + 12), lfo = sa1_rd16(d + 14);
    uint32_t send = sa1_rd16(d + 0x10), pan = sa1_rd16(d + 0x12), tlq = sa1_rd16(d + 0x14);
    uint32_t fmt = (r0 >> 7) & 3u, addr = ((r0 & 0x7Fu) << 16) | sa, i;
    int32_t oct, krs_base;
    double semis, ratio;
    const sa1a_pcm *pcm;
    memset(L, 0, sizeof *L);
    if (lea == 0)
        return 0;
    pcm = bank_pcm(a, b, addr, fmt, lea);
    if (!pcm || pcm->peak < SA1A_SILENT_PEAK)
        return 0;
    L->pub.struct_size = sizeof L->pub;
    L->pub.pcm = pcm->pcm;
    L->pub.num_samples = lea;
    L->pub.loop = ((r0 >> 9) & 1u) && lsa < lea;
    L->pub.loop_start = L->pub.loop ? lsa : 0;
    L->pub.loop_end = lea;
    L->pub.format = fmt;
    L->pub.base_note = d[0x26];
    L->pub.low_note = d[0x24];
    L->pub.high_note = d[0x25];
    L->pub.fine_tune = (int8_t)d[0x27];
    L->pub.lfo = (uint16_t)lfo;
    L->pub.delay = (float)(delay_ms < 10000u ? delay_ms : 10000u) / 1000.0f;   /* layer header +8 (ms) */
    /* pitch: the note relative to the base note, the fine tune and the split's own OCT/FNS */
    {
        int32_t o = (int32_t)((pitch >> 11) & 15u);
        if (o & 8)
            o -= 16;
        semis = (double)(note - (int32_t)d[0x26]) + (double)(int8_t)d[0x27] / 256.0 +
                12.0 * ((double)o + log(1.0 + (double)(pitch & 0x3FFu) / 1024.0) / log(2.0));
    }
    ratio = pow(2.0, semis / 12.0);
    L->pub.sample_rate = (float)(SA1A_AICA_RATE * ratio);
    /* level and pan: TL 0.375 dB per step, DISDL 3 dB per step (0 = off), DIPAN attenuates one side */
    L->total_level = (uint8_t)(tlq >> 8);
    L->direct_level = (uint8_t)((pan >> 8) & 15u);
    L->dipan = (uint8_t)(pan & 31u);
    L->pub.gain = L->direct_level ? db_to_gain(-0.375f * (float)L->total_level - 3.0f * (float)(15 - L->direct_level))
                                  : 0.0f;
    {
        uint32_t p = L->dipan & 15u;
        float att = (float)((p & 1u) * 3u + ((p >> 1) & 1u) * 6u + ((p >> 2) & 1u) * 12u + ((p >> 3) & 1u) * 24u);
        float g = p == 15u ? 0.0f : db_to_gain(-att);
        L->pub.pan = (L->dipan < 16u) ? 1.0f - g : g - 1.0f;   /* < 0x10: the left side is attenuated */
    }
    /* envelope; rate index = 2 R + (KRS == 15 ? 0 : octave + 2 KRS + FNS bit 9) */
    L->ar = (uint8_t)(eg1 & 31u);
    L->d1r = (uint8_t)((eg1 >> 6) & 31u);
    L->d2r = (uint8_t)((eg1 >> 11) & 31u);
    L->rr = (uint8_t)(eg2 & 31u);
    L->dl = (uint8_t)((eg2 >> 5) & 31u);
    L->krs = (uint8_t)((eg2 >> 10) & 15u);
    oct = (int32_t)floor(semis / 12.0);
    krs_base = 0;
    if (L->krs != 15u) {
        double fns = (ratio / pow(2.0, (double)oct) - 1.0) * 1024.0;
        krs_base = oct + 2 * (int32_t)L->krs + ((((int32_t)fns) >> 9) & 1);
    }
#define RATE_IDX(r) ((r) == 0 ? 0 : ((int32_t)(r) * 2 + krs_base < 0 ? 0 : ((int32_t)(r) * 2 + krs_base > 63 ? 63 : (int32_t)(r) * 2 + krs_base)))
    L->pub.attack = sa1a_attack_seconds(RATE_IDX(L->ar));
    L->pub.decay1 = L->dl ? sa1a_decay_db_per_second(RATE_IDX(L->d1r)) : 0.0f;
    L->pub.decay_level = 3.0f * (float)L->dl;
    L->pub.decay2 = sa1a_decay_db_per_second(RATE_IDX(L->d2r));
    L->pub.release = sa1a_decay_db_per_second(RATE_IDX(L->rr));
#undef RATE_IDX
    /* features the PCM does not carry */
    if (lfo & 0xE7u)
        L->pub.flags |= SA1_AUDIO_LAYER_LFO;
    if (!(tlq & 0x20u)) {
        for (i = 0; i < 5; i++)
            if (sa1_rd16(d + 0x16 + i * 2u) < 0x1FF8u)
                L->pub.flags |= SA1_AUDIO_LAYER_FILTER;
    }
    if ((send >> 4) & 15u)
        L->pub.flags |= SA1_AUDIO_LAYER_EFFECT;
    L->bend_up = (float)bend_up;
    L->bend_down = (float)bend_down;
    return 1;
}

uint32_t sa1a_program_layers(sa1_audio *a, sa1a_bank *b, uint32_t prog, int32_t note, sa1a_layer *out)
{
    uint32_t ptab, po, li, count = 0;
    if (!b || !b->loaded || prog >= b->num_programs)
        return 0;
    ptab = sa1_rd32(b->mpb + 0x10);
    if (ptab > b->mpb_size || prog >= (b->mpb_size - ptab) / 4u)
        return 0;
    po = sa1_rd32(b->mpb + ptab + prog * 4u);
    if (po == 0 || po > b->mpb_size || b->mpb_size - po < 0x18u)
        return 0;
    for (li = 0; li < SA1A_MAX_LAYERS; li++) {
        uint32_t lo = sa1_rd32(b->mpb + po + li * 4u), nspl, so, s;
        if (lo == 0 || lo > b->mpb_size || b->mpb_size - lo < 0x10u)
            continue;
        nspl = sa1_rd32(b->mpb + lo);
        so = sa1_rd32(b->mpb + lo + 4u);
        if (nspl > 128u || so > b->mpb_size || nspl * 0x30u > b->mpb_size - so)
            continue;
        for (s = 0; s < nspl; s++) {
            const uint8_t *d = b->mpb + so + s * 0x30u;
            if (note < (int32_t)d[0x24] || note > (int32_t)d[0x25])
                continue;
            if (split_layer(a, b, d, sa1_rd32(b->mpb + lo + 8u), b->mpb[lo + 12u], b->mpb[lo + 13u], note,
                            &out[count]))
                count++;
            break;
        }
    }
    return count;
}

sa1a_bank *sa1a_stage_bank(sa1_audio *a, uint32_t game_bank)
{
    if (game_bank >= SA1A_GAME_BANKS || game_bank == 3 || game_bank == 6)
        return NULL;
    return a->stage_banks[game_bank].loaded ? &a->stage_banks[game_bank] : NULL;
}

sa1a_bank *sa1a_set_bank(sa1_audio *a, uint32_t set, uint32_t game_bank)
{
    sa1a_bank *b;
    if (set == 0)
        return sa1a_stage_bank(a, game_bank);
    if (set > SA1_AUDIO_MAX_STAGE_SETS || game_bank >= SA1A_GAME_BANKS || game_bank == 3 || game_bank == 6)
        return NULL;
    b = &a->xset[set - 1].banks[game_bank];
    return b->loaded ? b : NULL;
}

int32_t sa1a_tone_game_bank(const sa1_audio *a, uint32_t tone)
{
    uint32_t i;
    for (i = 0; i < a->num_ranges; i++)
        if ((int32_t)tone > a->ranges[i].threshold)
            return a->ranges[i].bank;
    return 0;
}

float sa1a_tone_radius(const sa1_audio *a, uint32_t tone)
{
    uint32_t i;
    for (i = 0; i < a->num_3d; i++)
        if ((uint32_t)(int32_t)a->tone3d[i] == tone)
            return a->radius3d[i] > 0 ? (float)a->radius3d[i] : 0.0f;
    return 0.0f;
}

/* ------------------------------------------------------------------------- */
/* Tones                                                                     */
/* ------------------------------------------------------------------------- */

static void sound_free(sa1_audio *a, sa1a_sound *s)
{
    if (!s)
        return;
    sa1_mem_free(a->ctx, s->rendered);
    sa1_mem_free(a->ctx, s);
}

static void sounds_clear_all(sa1_audio *a)
{
    uint32_t k;
    for (k = 0; k < SA1A_KEYS; k++) {
        sound_free(a, a->sounds[k]);
        a->sounds[k] = NULL;
    }
}

/* the object's own stage (set 0: keys 0..SA1A_MAX_TONE): sa1_audio_set_stage */
static void sounds_clear(sa1_audio *a)
{
    uint32_t t;
    for (t = 0; t <= SA1A_MAX_TONE; t++) {
        sound_free(a, a->sounds[t]);
        a->sounds[t] = NULL;
    }
}

/* the bank object behind a tone (8C0948A0's lookup) */
static sa1a_bank *tone_bank(sa1_audio *a, uint32_t key, uint32_t *game_bank, uint32_t *request, int32_t *stage_dep)
{
    uint32_t i, g, tone = SA1A_KEY_TONE(key);
    int32_t th = -1, bk = 0;
    for (i = 0; i < a->num_ranges; i++) {
        if ((int32_t)tone > a->ranges[i].threshold) {
            th = a->ranges[i].threshold;
            bk = a->ranges[i].bank;
            break;
        }
    }
    *game_bank = (uint32_t)bk;
    *request = th < 0 ? tone : tone - (uint32_t)th - 1u;
    *stage_dep = 0;
    if (bk == 3 || bk == 6) {
        for (g = 0; g < SA1A_NUM_GROUPS; g++) {
            if ((bk == 3 ? k_group_bank3[g] : k_group_bank6[g]) == th) {
                sa1a_bank *b = bk == 3 ? &a->player[g] : &a->voiceb[g];
                return b->loaded ? b : NULL;
            }
        }
        return NULL;
    }
    *stage_dep = 1;
    return sa1a_set_bank(a, SA1A_KEY_SET(key), (uint32_t)bk);
}

static float layer_duration(const sa1_audio_layer *l)
{
    float r = l->sample_rate > 1.0f ? l->sample_rate : 1.0f;
    return l->delay + (float)l->num_samples / r;
}

const sa1a_sound *sa1a_sound_get(sa1_audio *a, uint32_t key, sa1_status *st)
{
    sa1a_sound *s;
    sa1a_bank *b;
    uint32_t i, tone = SA1A_KEY_TONE(key);
    *st = SA1_OK;
    if (key >= SA1A_KEYS) {
        *st = SA1_ERR_NOT_FOUND;
        return NULL;
    }
    if (a->sounds[key])
        return a->sounds[key];
    s = (sa1a_sound *)sa1_mem_alloc(a->ctx, sizeof *s);
    if (!s) {
        *st = SA1_ERR_OUT_OF_MEMORY;
        return NULL;
    }
    s->radius = sa1a_tone_radius(a, tone);
    if (tone > SA1A_LAST_SE) {
        s->kind = SA1_AUDIO_SOUND_VOICE;
        s->voice = tone + SA1A_VOICE_OFFSET;
    } else {
        b = tone_bank(a, key, &s->bank, &s->request, &s->stage_bank);
        s->kind = SA1_AUDIO_SOUND_NONE;
        s->file = b ? b->file : NULL;
        if (b && is_sequence_tone(tone)) {
            int16_t *pcm = NULL;
            uint32_t count = 0;
            sa1_status rs = sa1a_render_sequence(a, b, s->request, &pcm, &count);   /* b->set: its other banks */
            if (rs == SA1_ERR_OUT_OF_MEMORY) {
                sa1_mem_free(a->ctx, s);
                *st = rs;
                return NULL;
            }
            if (pcm && count) {
                sa1a_layer *L = &s->layers[0];
                s->rendered = pcm;
                s->kind = SA1_AUDIO_SOUND_SEQUENCE;
                s->num_layers = 1;
                L->pub.struct_size = sizeof L->pub;
                L->pub.pcm = pcm;
                L->pub.num_samples = count;
                L->pub.loop_end = count;
                L->pub.sample_rate = (float)SA1A_SEQ_RATE;
                L->pub.gain = 1.0f;
                L->pub.release = 200.0f;        /* a short fade instead of a click when it is cut */
                L->pub.format = SA1_AUDIO_FORMAT_RENDERED;
                L->pub.base_note = SA1A_NOTE;
                L->pub.high_note = 127;
                L->direct_level = 15;
                L->bend_up = L->bend_down = 2.0f;
            }
        } else if (b) {
            s->num_layers = sa1a_program_layers(a, b, s->request, SA1A_NOTE, s->layers);
            if (s->num_layers)
                s->kind = SA1_AUDIO_SOUND_PROGRAM;
        }
        for (i = 0; i < s->num_layers; i++) {
            const sa1_audio_layer *l = &s->layers[i].pub;
            float dur = l->loop ? l->delay + (float)l->loop_end / (l->sample_rate > 1.0f ? l->sample_rate : 1.0f)
                                : layer_duration(l);
            if (l->loop)
                s->loops = 1;
            if (dur > s->duration)
                s->duration = dur;
        }
    }
    a->sounds[key] = s;
    return s;
}

/* ------------------------------------------------------------------------- */
/* Public API                                                                */
/* ------------------------------------------------------------------------- */

static void preload(sa1_audio *a)
{
    uint32_t t;
    sa1_status st;
    for (t = 0; t <= SA1A_LAST_SE; t++)
        (void)sa1a_sound_get(a, t, &st);
}

sa1_status sa1_audio_open(sa1_context *ctx, const sa1_audio_desc *desc, sa1_audio **out)
{
    sa1_audio *a;
    sa1_status st;
    int partial = 0;
    uint32_t g;
    if (out)
        *out = NULL;
    if (!ctx || !desc || !out || desc->struct_size < sizeof(sa1_audio_desc) || !desc->gdi_path)
        return SA1_ERR_INVALID_ARGUMENT;
    if (desc->stage != SA1_AUDIO_STAGE_DEFAULT && !sa1_audio_stage_file(desc->stage, desc->flags))
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "audio: stage %d is out of range", (int)desc->stage);
    a = (sa1_audio *)sa1_mem_alloc(ctx, sizeof *a);
    if (!a)
        return SA1_ERR_OUT_OF_MEMORY;
    a->ctx = ctx;
    a->flags = desc->flags;
    a->stage = desc->stage == SA1_AUDIO_STAGE_DEFAULT ? 1 : desc->stage;
    a->characters = desc->characters ? desc->characters : SA1_CHAR_MASK_ALL;
    st = sa1_disc_open(ctx, desc->gdi_path, &a->disc);
    if (st != SA1_OK) {
        sa1_mem_free(ctx, a);
        return st;
    }
    a->disc_open = 1;
    st = load_tables(a);
    if (st == SA1_OK) {
        st = load_sonicadv_dir(a);
        if (st != SA1_OK)
            st = sa1_fail(ctx, st, "audio: no SONICADV directory on the disc (%s)", sa1_status_string(st));
    }
    if (st != SA1_OK) {
        sa1_audio_close(a);
        return st;
    }
    load_music_files(a);
    if (a->flags & SA1_AUDIO_MUSIC_ONLY)
        goto banks_done;        /* a music-only object: no banks, no voice archive, no preload */
    st = load_stage_banks(a, a->stage);
    if (st == SA1_ERR_OUT_OF_MEMORY) {
        sa1_audio_close(a);
        return st;
    }
    partial |= st != SA1_OK;
    for (g = 0; g < SA1A_NUM_GROUPS; g++) {
        int32_t k, want = 0;
        for (k = 0; k < SA1_CHAR_COUNT; k++)
            if ((a->characters & (1u << k)) && char_group(k) == g)
                want = 1;
        if (!want)
            continue;
        st = load_group_banks(a, g);
        if (st == SA1_ERR_OUT_OF_MEMORY) {
            sa1_audio_close(a);
            return st;
        }
        partial |= st != SA1_OK;
    }
    if (!(a->flags & SA1_AUDIO_NO_VOICES)) {
        st = load_afs(a);
        if (st == SA1_ERR_OUT_OF_MEMORY) {
            sa1_audio_close(a);
            return st;
        }
        partial |= st != SA1_OK;
    }
    if (a->flags & SA1_AUDIO_PRELOAD)
        preload(a);
banks_done:
    /* the loader thread (sa1_audio_load.c): from here on clips are read and tones decoded there; it preloads the
       jingles and every tone of the banks now, in the background */
    st = sa1a_loader_init(a);
    if (st != SA1_OK) {
        sa1_audio_close(a);
        return st;
    }
    if (!(a->flags & (SA1_AUDIO_SYNC_LOADS | SA1_AUDIO_NO_PRELOAD | SA1_AUDIO_MUSIC_ONLY)))
        (void)sa1_audio_preload(a, SA1_AUDIO_PRELOAD_ALL);
    {
        uint32_t on_disc = 0, i;
        for (i = 0; i < a->num_music; i++)
            on_disc += (uint32_t)music_on_disc(a, i);
        sa1_log(ctx, SA1_LOG_INFO, "audio: %u tone ranges, %u voices, %u music numbers (%u files on the disc), %s, %s",
                (unsigned)a->num_ranges, (unsigned)a->num_voices, (unsigned)a->num_music, (unsigned)on_disc,
                (a->flags & SA1_AUDIO_MUSIC_ONLY) ? "music only" : sa1_audio_stage_file(a->stage, a->flags),
                sa1a_threaded(a) ? "loader thread" : "clips load on the calling thread");
    }
    *out = a;
    return partial ? SA1_PARTIAL : SA1_OK;
}

void sa1_audio_close(sa1_audio *a)
{
    uint32_t i;
    if (!a)
        return;
    sa1a_mixers_free_all(a);
    while (a->streams)
        sa1_audio_stream_close(a->streams);
    sa1a_loader_free(a);    /* joins the thread: nothing reads the banks or the disc after this */
    sounds_clear_all(a);
    for (i = 0; i < SA1A_GAME_BANKS; i++)
        bank_free(a, &a->stage_banks[i]);
    {
        uint32_t x;
        for (x = 0; x < SA1_AUDIO_MAX_STAGE_SETS; x++)
            for (i = 0; i < SA1A_GAME_BANKS; i++)
                bank_free(a, &a->xset[x].banks[i]);
    }
    for (i = 0; i < SA1A_NUM_GROUPS; i++) {
        bank_free(a, &a->player[i]);
        bank_free(a, &a->voiceb[i]);
    }
    sa1_mem_free(a->ctx, a->afs_toc);
    sa1_mem_free(a->ctx, a->sonicadv_dir);
    sa1_mem_free(a->ctx, a->titles);
    if (a->disc_open)
        sa1_disc_close(&a->disc);
    sa1_mem_free(a->ctx, a);
}

sa1_status sa1_audio_set_stage(sa1_audio *a, int32_t stage)
{
    uint32_t i;
    sa1_status st;
    if (!a)
        return SA1_ERR_INVALID_ARGUMENT;
    if (stage == SA1_AUDIO_STAGE_DEFAULT)
        stage = 1;
    if (!sa1_audio_stage_file(stage, a->flags))
        return SA1_ERR_INVALID_ARGUMENT;
    if (a->flags & SA1_AUDIO_MUSIC_ONLY) {
        a->stage = stage;       /* no banks to replace */
        return SA1_OK;
    }
    sa1a_mixers_drop_stage(a);
    sa1a_tone_lock(a);      /* waits for a tone the loader is decoding; it decodes none until the unlock */
    sa1a_tones_dropped(a);  /* the mixers stop finding the old tones; the loader decodes the new ones again if it
                               preloaded them (after the unlock) */
    sounds_clear(a);
    for (i = 0; i < SA1A_GAME_BANKS; i++)
        bank_free(a, &a->stage_banks[i]);
    a->stage = stage;
    st = load_stage_banks(a, stage);
    if (st == SA1_OK && (a->flags & SA1_AUDIO_PRELOAD))
        preload(a);
    sa1a_tone_unlock(a);
    return st == SA1_OK ? SA1_OK : (st == SA1_ERR_OUT_OF_MEMORY ? st : SA1_PARTIAL);
}

sa1_status sa1_audio_sound_info(sa1_audio *a, uint32_t tone, sa1_audio_sound *out)
{
    sa1_audio_sound info;
    sa1_status st;
    const sa1a_sound *s;
    if (!a || !out || out->struct_size < sizeof(uint32_t))
        return SA1_ERR_INVALID_ARGUMENT;
    if (tone > SA1A_MAX_TONE)
        return SA1_ERR_NOT_FOUND;   /* a plain tone: never another set's key */
    sa1a_tone_lock(a);
    s = sa1a_sound_get(a, tone, &st);
    sa1a_tone_unlock(a);
    if (!s)
        return st;
    memset(&info, 0, sizeof info);
    info.struct_size = sizeof info;
    info.tone = tone;
    info.kind = s->kind;
    info.bank = s->bank;
    info.request = s->request;
    info.file = s->file;
    info.voice = s->voice;
    info.num_layers = s->num_layers;
    info.loops = s->loops;
    info.duration = s->duration;
    info.radius = s->radius;
    sa1_copy_out(out, &info, sizeof info);
    return SA1_OK;
}

sa1_status sa1_audio_sound_layer(sa1_audio *a, uint32_t tone, uint32_t layer, sa1_audio_layer *out)
{
    sa1_status st;
    const sa1a_sound *s;
    if (!a || !out || out->struct_size < sizeof(uint32_t))
        return SA1_ERR_INVALID_ARGUMENT;
    if (tone > SA1A_MAX_TONE)
        return SA1_ERR_NOT_FOUND;
    sa1a_tone_lock(a);
    s = sa1a_sound_get(a, tone, &st);
    sa1a_tone_unlock(a);
    if (!s)
        return st;
    if (layer >= s->num_layers)
        return SA1_ERR_NOT_FOUND;
    sa1_copy_out(out, &s->layers[layer].pub, sizeof s->layers[layer].pub);
    return SA1_OK;
}

uint32_t sa1_audio_voice_count(const sa1_audio *a) { return a ? a->num_voices : 0u; }
uint32_t sa1_audio_music_count(const sa1_audio *a) { return a ? a->num_music : 0u; }

const char *sa1_audio_music_name(const sa1_audio *a, uint32_t music)
{
    return (a && music < a->num_music) ? a->music[music] : NULL;
}

/* ------------------------------------------------------------------------- */
/* Music list: the sound-test titles (ADVERTISE.PRS) and the ADX headers     */
/* ------------------------------------------------------------------------- */

/* the offset in the ADVERTISE image of `len` bytes at SH-4 address `addr` (0x8C / 0xAC / 0x0C aliases); 0 if outside */
static int adv_off(uint32_t addr, uint32_t len, uint32_t size, uint32_t *off)
{
    uint32_t phys = addr & 0x1FFFFFFFu, base = ADV_LOAD & 0x1FFFFFFFu;
    if (phys < base || phys - base > size || len > size - (phys - base))
        return 0;
    *off = phys - base;
    return 1;
}

/* the length of the title at `off`: printable ASCII, at most ADV_MAX_TITLE bytes, then a NUL; 0 if it is not one */
static uint32_t adv_title_len(const uint8_t *d, uint32_t size, uint32_t off)
{
    uint32_t k;
    for (k = 0; k <= ADV_MAX_TITLE && off + k < size; k++) {
        uint8_t c = d[off + k];
        if (c == 0)
            return k;
        if (c < 0x20u || c > 0x7Eu)
            return 0;
    }
    return 0;
}

/* Reads the game's sound test from ADVERTISE.PRS, once: the title, page and entry of every listed music number. Only
 * the title strings are kept (the 937 KB overlay is freed at once). SA1_OK also when there are no titles (the file is
 * missing, malformed or not the supported version: logged once); running out of memory is returned and tried again by
 * the next call. */
static sa1_status load_titles(sa1_audio *a)
{
    uint8_t *prs = NULL, *d = NULL;
    uint32_t prs_size = 0, size = 0, p, i, total = 0, n = 0, bad = 0;
    int32_t page_of[SA1A_MAX_MUSIC], entry_of[SA1A_MAX_MUSIC];
    uint32_t src_of[SA1A_MAX_MUSIC], len_of[SA1A_MAX_MUSIC];
    char hex[41];
    sa1_status st;
    for (i = 0; i < SA1A_MAX_MUSIC; i++) {
        page_of[i] = entry_of[i] = -1;
        src_of[i] = len_of[i] = 0;
    }
    st = read_sonicadv(a, ADV_FILE, MAX_ADV_PRS, &prs, &prs_size);
    if (st == SA1_OK) {
        st = sa1_prs_decompress(a->ctx, prs, prs_size, MAX_ADV_SIZE, &d, &size);
        sa1_mem_free(a->ctx, prs);
    }
    if (st == SA1_ERR_OUT_OF_MEMORY)
        return st;
    if (st != SA1_OK) {
        sa1_log(a->ctx, SA1_LOG_WARN, "audio: cannot read SONICADV/%s (%s): the music has no sound-test titles",
                ADV_FILE, sa1_status_string(st));
        a->titles_read = 1;
        return SA1_OK;
    }
    sa1_sha1_hex(d, size, hex);
    if (strcmp(hex, ADV_SHA1) != 0 && !(a->flags & SA1_AUDIO_ACCEPT_MODIFIED)) {
        sa1_log(a->ctx, SA1_LOG_WARN, "audio: SONICADV/%s is not the supported version (SHA-1 %s): the music has no "
                "sound-test titles", ADV_FILE, hex);
        sa1_mem_free(a->ctx, d);
        a->titles_read = 1;
        return SA1_OK;
    }
    /* the six pages {record *, count} and their records {char *title, s32 music}; every value is checked (a modified
       overlay is accepted with SA1_AUDIO_ACCEPT_MODIFIED); a music number keeps its first record */
    for (p = 0; p < ADV_NUM_PAGES; p++) {
        uint32_t po, ro, cnt, e;
        if (!adv_off(ADV_PAGES + p * 8u, 8u, size, &po)) {
            bad++;
            break;
        }
        cnt = sa1_rd32(d + po + 4u);
        if (cnt > ADV_MAX_PER_PAGE || !adv_off(sa1_rd32(d + po), cnt * 8u, size, &ro)) {
            bad++;
            continue;
        }
        for (e = 0; e < cnt; e++) {
            int32_t music = sa1_rds32(d + ro + e * 8u + 4u);
            uint32_t to = 0, len = 0;
            if (music < 0 || (uint32_t)music >= a->num_music || page_of[music] >= 0 ||
                !adv_off(sa1_rd32(d + ro + e * 8u), 1u, size, &to) || (len = adv_title_len(d, size, to)) == 0) {
                bad++;
                continue;
            }
            page_of[music] = (int32_t)p;
            entry_of[music] = (int32_t)e;
            src_of[music] = to;
            len_of[music] = len;
            total += len + 1u;
            n++;
        }
    }
    if (n) {
        a->titles = (char *)sa1_mem_alloc(a->ctx, total);
        if (!a->titles) {
            sa1_mem_free(a->ctx, d);
            return SA1_ERR_OUT_OF_MEMORY;
        }
        total = 0;
        for (i = 0; i < SA1A_MAX_MUSIC; i++) {
            sa1a_music *m = &a->music_files[i];
            if (page_of[i] < 0)
                continue;
            memcpy(a->titles + total, d + src_of[i], len_of[i]);
            a->titles[total + len_of[i]] = 0;
            m->title = (int32_t)total;
            m->page = page_of[i];
            m->entry = entry_of[i];
            total += len_of[i] + 1u;
        }
    }
    sa1_mem_free(a->ctx, d);
    a->titles_read = 1;
    if (bad || !n)
        sa1_log(a->ctx, SA1_LOG_WARN, "audio: SONICADV/%s: %u sound-test titles, %u records or pages not usable",
                ADV_FILE, (unsigned)n, (unsigned)bad);
    return SA1_OK;
}

/* Reads a music file's ADX header (the first ADX_HEADER_READ bytes: 2 sectors) into a->music_files[music]. */
static sa1_status read_music_header(sa1_audio *a, uint32_t music)
{
    sa1a_music *e = &a->music_files[music];
    uint32_t len = e->size < ADX_HEADER_READ ? e->size : ADX_HEADER_READ, coff;
    uint8_t *buf;
    sa1a_adx h;
    sa1_status st;
    if (len < 0x20u)
        return SA1_ERR_BAD_FORMAT;
    buf = (uint8_t *)sa1_mem_alloc(a->ctx, len);
    if (!buf)
        return SA1_ERR_OUT_OF_MEMORY;
    st = read_range(a, e->lba, e->size, 0, len, buf);
    if (st == SA1_OK) {
        /* sa1a_adx_parse reads only bytes [0, data offset) of the buffer (the header up to "(c)CRI" and the 4 bytes
           after it), which must be inside the part read; it gets the whole file's size so that it clamps the frame
           count to the blocks of the whole file, as sa1_audio_stream_open's parse of the whole file does */
        coff = ((uint32_t)buf[2] << 8) | buf[3];
        if ((((uint32_t)buf[0] << 8) | buf[1]) != 0x8000u || coff + 4u > len)
            st = SA1_ERR_BAD_FORMAT;
        else
            st = sa1a_adx_parse(buf, e->size, &h);
    }
    if (st == SA1_OK) {
        e->h = h;
        e->header_frames = ((uint32_t)buf[12] << 24) | ((uint32_t)buf[13] << 16) | ((uint32_t)buf[14] << 8) | buf[15];
        e->have_header = 1;
    }
    sa1_mem_free(a->ctx, buf);
    return st;
}

sa1_status sa1_audio_music_info(sa1_audio *a, uint32_t music, sa1_audio_music *out)
{
    sa1_audio_music info;
    const sa1a_music *e;
    sa1_status st = SA1_OK;
    if (!a || !out || out->struct_size < sizeof(uint32_t))
        return SA1_ERR_INVALID_ARGUMENT;
    if (music >= a->num_music)
        return SA1_ERR_NOT_FOUND;
    if (!a->titles_read) {
        st = load_titles(a);
        if (st != SA1_OK)
            return st;
    }
    e = &a->music_files[music];
    if (music_on_disc(a, music) && !e->have_header)
        st = read_music_header(a, music);
    memset(&info, 0, sizeof info);
    info.struct_size = sizeof info;
    info.music = music;
    info.file = a->music[music];
    info.title = (a->titles && e->title >= 0) ? a->titles + e->title : NULL;
    info.page = info.title ? e->page : -1;
    info.entry = info.title ? e->entry : -1;
    if (info.title)
        info.flags |= SA1_AUDIO_MUSIC_LISTED;
    if (sa1a_jingle_kind(music) != SA1_AUDIO_JINGLE_OTHER)
        info.flags |= SA1_AUDIO_MUSIC_JINGLE;
    if (music_on_disc(a, music)) {
        info.flags |= SA1_AUDIO_MUSIC_ON_DISC;
        info.file_size = e->size;
        if (e->have_header) {
            info.channels = e->h.channels;
            info.sample_rate = e->h.rate;
            info.num_frames = e->h.frames;
            info.loop = e->h.loop;
            info.loop_start = e->h.loop_start;
            info.loop_end = e->h.loop_end;
            if (e->header_frames > e->h.frames)
                info.flags |= SA1_AUDIO_MUSIC_TRUNCATED;
        }
    }
    sa1_copy_out(out, &info, sizeof info);
    return st;
}

/* ------------------------------------------------------------------------- */
/* Streams                                                                   */
/* ------------------------------------------------------------------------- */

#define LAZY_STEP         (256u << 10)  /* bytes a lazy stream reads at least at a time (about 1.6 s of music) */

struct sa1_audio_stream {
    sa1_audio               *audio;
    struct sa1_audio_stream *next;
    uint8_t                 *data;          /* the whole file (a lazy stream: read up to `loaded`, zeros after it) */
    uint32_t                 size;
    uint32_t                 lba;           /* a lazy stream: the file's first sector */
    uint32_t                 loaded;        /* bytes of data read from the start of the file (= size unless lazy) */
    sa1a_adx_dec             dec;
};

/* A lazy stream reads the file on to the end of the block that holds `frame` (at least LAZY_STEP more, whole sectors),
 * so that every byte the decoder reads up to that frame is there: the decoder reads blocks from the start of the file
 * up to the frame it decodes (sequential reads, loop wraps backwards, seeks forwards block by block). */
static sa1_status stream_need(sa1_audio_stream *s, uint32_t frame)
{
    uint64_t need;
    uint32_t len;
    sa1_status st;
    if (s->loaded >= s->size)
        return SA1_OK;
    need = (uint64_t)s->dec.h.data_off + ((uint64_t)(frame / 32u) + 1u) * s->dec.h.block * s->dec.h.channels;
    if (need <= s->loaded)
        return SA1_OK;
    if (need < (uint64_t)s->loaded + LAZY_STEP)
        need = (uint64_t)s->loaded + LAZY_STEP;
    need = (need + 2047u) & ~(uint64_t)2047u;
    if (need > s->size)
        need = s->size;
    len = (uint32_t)need - s->loaded;
    st = read_range(s->audio, s->lba, s->size, s->loaded, len, s->data + s->loaded);
    if (st == SA1_OK)
        s->loaded += len;
    return st;
}

sa1_status sa1_audio_stream_open(sa1_audio *a, int32_t source, uint32_t index, sa1_audio_stream **out,
                                 sa1_audio_stream_info *info)
{
    sa1_audio_stream *s;
    sa1a_adx h;
    sa1_status st;
    uint8_t *data;
    uint32_t size, loaded, lba = 0;
    if (out)
        *out = NULL;
    if (!a || !out || (info && info->struct_size < sizeof(uint32_t)))
        return SA1_ERR_INVALID_ARGUMENT;
    st = sa1a_adx_exists(a, source, index);
    if (st != SA1_OK)
        return st;
    data = sa1a_clip_copy(a, source, index, &size);
    loaded = size;
    if (!data && source == SA1_AUDIO_SOURCE_MUSIC && (a->flags & SA1_AUDIO_LAZY_STREAMS)) {
        /* lazy: a buffer of the file's size, of which only the header is read now */
        const sa1a_music *e = &a->music_files[index];
        size = e->size;
        lba = e->lba;
        if (size > MAX_ADX_SIZE)
            return SA1_ERR_LIMIT;
        data = (uint8_t *)sa1_mem_alloc(a->ctx, size ? size : 1u);
        if (!data)
            return SA1_ERR_OUT_OF_MEMORY;
        loaded = size < ADX_HEADER_READ ? size : ADX_HEADER_READ;
        st = read_range(a, lba, size, 0, loaded, data);
        /* the header must lie in the part read: sa1a_adx_parse reads only bytes before the data offset */
        if (st == SA1_OK && (loaded < 0x20u || (((uint32_t)data[2] << 8) | data[3]) + 4u > loaded))
            st = SA1_ERR_BAD_FORMAT;
        if (st != SA1_OK) {
            sa1_mem_free(a->ctx, data);
            return st;
        }
        sa1a_count_caller_read(a);
    } else if (!data) {
        st = sa1a_read_adx(a, source, index, &data, &size);
        if (st != SA1_OK)
            return st;
        loaded = size;
        sa1a_count_caller_read(a);
    }
    st = sa1a_adx_parse(data, size, &h);
    if (st != SA1_OK) {
        sa1_mem_free(a->ctx, data);
        return st;
    }
    s = (sa1_audio_stream *)sa1_mem_alloc(a->ctx, sizeof *s);
    if (!s) {
        sa1_mem_free(a->ctx, data);
        return SA1_ERR_OUT_OF_MEMORY;
    }
    s->audio = a;
    s->data = data;
    s->size = size;
    s->lba = lba;
    s->loaded = loaded;
    sa1a_adx_dec_init(&s->dec, data, size, &h);
    s->next = a->streams;
    a->streams = s;
    if (info) {
        sa1_audio_stream_info i;
        memset(&i, 0, sizeof i);
        i.struct_size = sizeof i;
        i.channels = h.channels;
        i.sample_rate = h.rate;
        i.num_frames = h.frames;
        i.loop = h.loop;
        i.loop_start = h.loop_start;
        i.loop_end = h.loop_end;
        sa1_copy_out(info, &i, sizeof i);
    }
    *out = s;
    return SA1_OK;
}

sa1_status sa1_audio_stream_read(sa1_audio_stream *s, int16_t *out, uint32_t frames, int32_t loop, uint32_t *got)
{
    uint32_t n;
    if (got)
        *got = 0;
    if (!s || (!out && frames))
        return SA1_ERR_INVALID_ARGUMENT;
    if (s->loaded < s->size && frames) {
        /* a lazy stream: the last frame this read can decode is before the end (loop: the loop end; a wrap goes
           back to the loop start, which is before it) */
        uint32_t end = (loop && s->dec.h.loop) ? s->dec.h.loop_end : s->dec.h.frames, pos = s->dec.pos;
        if (end > 0 && (pos < end || (loop && s->dec.h.loop))) {
            sa1_status st = stream_need(s, pos < end && frames < end - pos ? pos + frames - 1u : end - 1u);
            if (st != SA1_OK)
                return st;
        }
    }
    n = sa1a_adx_dec_read(&s->dec, out, frames, loop != 0);
    if (got)
        *got = n;
    return SA1_OK;
}

sa1_status sa1_audio_stream_seek(sa1_audio_stream *s, uint32_t frame)
{
    if (!s)
        return SA1_ERR_INVALID_ARGUMENT;
    if (frame > s->dec.h.frames)
        return SA1_ERR_INVALID_ARGUMENT;
    if (s->loaded < s->size) {
        sa1_status st = stream_need(s, frame);  /* a lazy stream: the seek decodes up to the frame's block */
        if (st != SA1_OK)
            return st;
    }
    sa1a_adx_dec_seek(&s->dec, frame);
    return SA1_OK;
}

void sa1_audio_stream_close(sa1_audio_stream *s)
{
    sa1_audio_stream **pp;
    if (!s)
        return;
    for (pp = &s->audio->streams; *pp; pp = &(*pp)->next) {
        if (*pp == s) {
            *pp = s->next;
            break;
        }
    }
    sa1_mem_free(s->audio->ctx, s->data);
    sa1_mem_free(s->audio->ctx, s);
}

/* ------------------------------------------------------------------------- */
/* Cues                                                                      */
/* ------------------------------------------------------------------------- */

/* the DC functions behind the events (gen/policy/stubs.tsv) */
#define FN_SE_ONESHOT      0x8C0940DAu  /* (tone, id, pri, volofs): timer 120, flags 0x11 */
#define FN_SE_AT           0x8C09413Au  /* (tone, id, pri, volofs, x, y, z): timer 120, positional */
#define FN_SE_OWNED        0x8C0941FCu  /* (tone, task, pri, volofs): timer -1 (until stopped) */
#define FN_SE_KEEP         0x8C094330u  /* (tone, task, pri, volofs, timer): refreshed every frame */
#define FN_SE_KEEP_AT      0x8C0943E0u  /* (tone, task, pri, volofs, timer, x, y, z): positional */
#define FN_SE_STOP         0x8C0945A0u  /* (tone) */
#define FN_SE_STOP_TASK    0x8C0945D0u  /* (task) */
#define FN_SE_DELAYED      0x8C04EF22u  /* (tone): a task plays it 5 frames later */
#define FN_MUSIC_DELAYED   0x8C04EF52u  /* (music): a task sets it 5 frames later */
#define FN_VOICE           0x8C107BA0u  /* (voice) */
#define FN_MUSIC_SET       0x8C107AC6u  /* (music): background music */
#define FN_MUSIC_JINGLE    0x8C107C80u  /* (music): temporary music (invincibility, speed up, countdown) */
#define FN_MUSIC_STOP      0x8C107ADAu
#define FN_MUSIC_RESTORE   0x8C107B26u

#define KEEP_LIFETIME      0.1f         /* the game's keep-alive timers are 2-4 frames; a host tick is 2 */

sa1_status sa1_audio_cue_from_event(sa1_audio *a, const sa1_event *ev, sa1_audio_cue *out)
{
    sa1_audio_cue c;
    sa1_coords co;
    float scale = 1.0f;
    if (!a || !ev || !out || out->struct_size < sizeof(uint32_t))
        return SA1_ERR_INVALID_ARGUMENT;
    co.struct_size = sizeof co;
    if (sa1_get_coords(a->ctx, &co) == SA1_OK && co.scale > 0.0f)
        scale = co.scale;
    memset(&c, 0, sizeof c);
    c.struct_size = sizeof c;
    c.character = ev->character;
    c.source = ev->arg;
    c.position[0] = ev->position[0];
    c.position[1] = ev->position[1];
    c.position[2] = ev->position[2];
    c.min_distance = SA1A_NEAR * scale;
    c.max_distance = SA1A_FAR * scale;
    c.volume = 1.0f;
    c.lifetime = -1.0f;
    switch (ev->type) {
    case SA1_EVENT_SOUND:
    case SA1_EVENT_VOICE:
        if (ev->type == SA1_EVENT_VOICE && ev->arg != FN_SE_DELAYED) {
            c.type = SA1_AUDIO_CUE_VOICE;
            c.index = ev->id;
            break;
        }
        c.tone = ev->id;
        c.type = SA1_AUDIO_CUE_PLAY;
        c.lifetime = 120.0f * SA1A_FRAME_SECONDS;
        switch (ev->arg) {
        case FN_SE_AT:
            c.flags = SA1_AUDIO_CUE_POSITIONAL;
            break;
        case FN_SE_OWNED:
            c.flags = SA1_AUDIO_CUE_OWNED;
            c.lifetime = -1.0f;
            break;
        case FN_SE_KEEP:
            c.type = SA1_AUDIO_CUE_KEEP;
            c.flags = SA1_AUDIO_CUE_OWNED;
            c.lifetime = KEEP_LIFETIME;
            break;
        case FN_SE_KEEP_AT:
            c.type = SA1_AUDIO_CUE_KEEP;
            c.flags = SA1_AUDIO_CUE_OWNED | SA1_AUDIO_CUE_POSITIONAL;
            c.lifetime = KEEP_LIFETIME;
            break;
        case FN_SE_DELAYED:
            c.delay = 5.0f * SA1A_FRAME_SECONDS;
            break;
        default:
            break;
        }
        if (c.tone > SA1A_LAST_SE) {           /* 8C0948A0 hands such tones to the voice player */
            c.type = SA1_AUDIO_CUE_VOICE;
            c.index = c.tone + SA1A_VOICE_OFFSET;
            c.flags &= ~SA1_AUDIO_CUE_POSITIONAL;
            c.lifetime = -1.0f;
        } else if (c.flags & SA1_AUDIO_CUE_POSITIONAL) {
            c.start_radius = sa1a_tone_radius(a, c.tone) * scale;
        }
        break;
    case SA1_EVENT_SOUND_STOP:
        c.type = SA1_AUDIO_CUE_STOP;
        c.tone = ev->id;
        if (ev->arg == FN_SE_STOP_TASK)
            c.tone = 0;
        break;
    case SA1_EVENT_JINGLE:
        switch (ev->arg) {
        case FN_MUSIC_JINGLE:
            c.type = SA1_AUDIO_CUE_JINGLE;
            c.index = ev->id;
            break;
        case FN_MUSIC_SET:
            c.type = SA1_AUDIO_CUE_MUSIC;
            c.index = ev->id;
            break;
        case FN_MUSIC_DELAYED:
            c.type = SA1_AUDIO_CUE_MUSIC;
            c.index = ev->id;
            c.delay = 5.0f * SA1A_FRAME_SECONDS;
            break;
        case FN_MUSIC_STOP:
            c.type = SA1_AUDIO_CUE_MUSIC_STOP;
            break;
        case FN_MUSIC_RESTORE:
            c.type = SA1_AUDIO_CUE_MUSIC_RESTORE;
            break;
        default:
            c.type = SA1_AUDIO_CUE_NONE;        /* music system init / helper task bookkeeping */
            break;
        }
        break;
    default:
        c.type = SA1_AUDIO_CUE_NONE;
        break;
    }
    sa1_copy_out(out, &c, sizeof c);
    return SA1_OK;
}
