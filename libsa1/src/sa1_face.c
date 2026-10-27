/*
 * sa1_face.c - the game's event faces, the API side (include/libsa1.h "faces", SA1_API_FACE, round 0.9.4a; sadc
 * docs/research/faces_wheel_plan.md 4.2-4.4 and 5.3 with its Review R.2, R.3, R.6, R.10, R.11, R.13; faces_events.md 2-8):
 * sa1_character_face_mode / sa1_character_face / sa1_character_face_sequence / sa1_character_face_level and
 * sa1_face_info_get; round 0.9.4c (SA1_API_FACE_MOUTH, sadc docs/research/phoneme_plan.md 4.2-4.6, 5.3-5.4 with its Review):
 * sa1_character_face_mouth, SA1_FACE_SAME_PAIR in the level's style, the per-kind loudness ladders of sa1f_compute.
 *
 * Layering (review R.13): plain C99 with the asset-layer flags (-std=c99 -Iinclude -Isrc; MSVC through CMake with
 * SA1_NO_WORLD): this file includes libsa1.h, sa1_internal.h, sa1_api_world.h and sa1_face.h only, never world/sa1_world.h
 * (it needs sadc's sa1/guest.h); it reaches a world through the functions of sa1_face.h, defined in
 * src/world/sa1_world_face.c. Order of a call: the argument syntax, the character and its kind, then
 * sa1w_character_world_ensure, then the kind's letter mask (computed by the world side from its own arena, never through
 * the disc read of sa1_face_info_get), then the latch. Nothing is applied on an error. SA1_NO_WORLD: validation, then
 * SA1_ERR_NOT_IMPLEMENTED.
 *
 * sa1_character_face_level is the host's per-frame call: no allocation, no log on its path (its errors are recorded in
 * sa1_last_error only); the level is a state that lasts until the next level call (review R.6), so a frame that runs two
 * pairs keeps it.
 *
 * sa1_face_info_get (review R.2): sa1_load_disc frees every image it read and parses no face data, so each call opens the
 * disc of the last sa1_load_disc, reads 1ST_READ.BIN (Tikal: also TIKAL_PROG.PRS), checks its SHA-1, computes the numbers
 * (sa1f_compute) and frees everything: only numbers reach the caller. Tens of ms: a host calls it once per kind.
 *
 * The per-kind EV heads (sa1f_kind_of) are numbers checked against the face task creators of sadc (the addresses the
 * creators store: code_8C0B2338.c:50-58, code_8C0BF262.c:257-290, code_8C0C8A80.c:199-212, code_8C0DDA00.c:183-196,
 * code_8C0E59C0.c:35-48, overlays/tikal_prog/code_0CB00780.c:334-347; config/symbols.1st_read.txt Sonic_EV_Head ...);
 * tests/face_test.c F13 compares them with the face work every world's creator made (playerwk.pfp object / motion).
 */
#include "sa1_internal.h"
#include "sa1_api_world.h"
#include "sa1_face.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ---- the EV heads -------------------------------------------------------------------------------------------------- */
static const sa1f_kind k_kinds[SA1_CHAR_COUNT] = {
    { 0x8C1DFBD8u, 0x8C1FBBECu },   /* 0 Sonic: Sonic_EV_Head, sonic_face_s_head (creator 8C0B2338) */
    { 0u, 0u },                     /* 1 Eggman: none */
    { 0x8C279198u, 0x8C28E9BCu },   /* 2 Tails: Tails_EV_Head, miles_face_m_head (creator 8C0BF76A; both parents) */
    { 0x8C37273Cu, 0x8C38C870u },   /* 3 Knuckles: Knuckles_EV_Head, knuckles_face_k_head (creator 8C0C8E8E) */
    { 0x0CB0DA04u, 0x0CB23298u },   /* 4 Tikal: TIKAL_PROG_Tikal_EV_Head, ..._Shapes (creator 0CB00ED4) */
    { 0x8C4D0718u, 0x8C4E591Cu },   /* 5 Amy: Amy_EV_Head, amy_face_a_head (creator 8C0DDDB2) */
    { 0u, 0u },                     /* 6 E-102: no face task */
    { 0x8C5367C4u, 0x8C53C310u },   /* 7 Big: Big_EV_Head, big_face_b_head (creator 8C0E59C0) */
    { 0u, 0u },                     /* 8 Super Sonic: no EV head in the game (B_CHAOS7 has none) */
};

const sa1f_kind *sa1f_kind_of(int32_t kind)
{
    return kind >= 0 && kind < SA1_CHAR_COUNT ? &k_kinds[kind] : NULL;
}

static const char *const k_display[SA1_CHAR_COUNT] = {
    "Sonic", "Eggman", "Tails", "Knuckles", "Tikal", "Amy", "E-102", "Big", "Super Sonic"
};

/* round 0.9.4c: the SAME_PAIR bit the calls accept - 0 when libsa1.h does not define SA1_FACE_SAME_PAIR (phoneme_plan.md 5.3,
 * review R.2: not built = an unknown flag, SA1_ERR_INVALID_ARGUMENT, so no host compiles against a flag that latches nothing) */
#if defined(SA1_FACE_SAME_PAIR)
static const uint32_t k_same_pair = SA1F_SAME_PAIR;
#else
static const uint32_t k_same_pair = 0u;
#endif

/* ---- the numbers ------------------------------------------------------------------------------------------------- */
static int mem_rd32(const sa1f_mem *m, uint32_t g, uint32_t *v)
{
    const uint8_t *p = m->ptr(m->user, g, 4u);
    if (p == NULL) {
        return 0;
    }
    *v = sa1_rd32(p);
    return 1;
}

/* 8C07B080's walk: the node (its NJS_MDATA2 is mdata[*idx]), its child subtree, then its sibling chain */
static int walk_keys(const sa1f_mem *m, uint32_t obj, uint32_t mdata, uint32_t *idx, uint32_t *keys, uint32_t depth)
{
    uint32_t p0 = 0, n0 = 0, child = 0, sib = 0;
    if (depth > SA1F_MAX_NODES || *idx >= SA1F_MAX_NODES) {
        return 0;
    }
    if (!mem_rd32(m, mdata + 0x10u * *idx, &p0) || !mem_rd32(m, mdata + 0x10u * *idx + 8u, &n0) ||
        !mem_rd32(m, obj + 0x2Cu, &child) || !mem_rd32(m, obj + 0x30u, &sib)) {
        return 0;
    }
    if (p0 != 0u && n0 < *keys) {
        *keys = n0;
    }
    if (child != 0u) {
        (*idx)++;
        if (!walk_keys(m, child, mdata, idx, keys, depth + 1u)) {
            return 0;
        }
    }
    if (sib != 0u) {
        (*idx)++;
        if (!walk_keys(m, sib, mdata, idx, keys, depth + 1u)) {
            return 0;
        }
    }
    return 1;
}

/* round 0.9.4c (sadc docs/research/phoneme_plan.md 4.6, phoneme_shapes.md 6): the loudness ladder of each kind, closed to open
 * by the measured mouth opening of the rest-pose renders (UNVERIFIED by the owner's eye in game). Round 0.9.4a used 0 5 4 6 7
 * for every kind with 20 or more keys; measured, that order is not monotonic (Sonic's mouth shrank to the small "o" at
 * three-quarter level, Knuckles' closed: his key 6 is closed). Tikal (not measured, he cannot tick) keeps 0.9.4a's. */
typedef struct sa1f_ladder {
    uint8_t n, keys[7];
} sa1f_ladder;
static const sa1f_ladder k_ladders[SA1_CHAR_COUNT] = {
    { 3, { 0, 4, 16 } },            /* 0 Sonic */
    { 0, { 0 } },                   /* 1 Eggman: no face */
    { 4, { 0, 4, 7, 8 } },          /* 2 Tails */
    { 4, { 0, 4, 7, 16 } },         /* 3 Knuckles */
    { 5, { 0, 5, 4, 6, 7 } },       /* 4 Tikal (0.9.4a's, not measured) */
    { 5, { 0, 7, 4, 8, 16 } },      /* 5 Amy */
    { 0, { 0 } },                   /* 6 E-102: no face task */
    { 3, { 0, 4, 5 } },             /* 7 Big (unchanged) */
    { 0, { 0 } },                   /* 8 Super Sonic: no EV head */
};

sa1_status sa1f_compute(const sa1f_mem *m, int32_t kind, sa1_face_info *out)
{
    const sa1f_kind *k = sa1f_kind_of(kind);
    uint32_t n, i, maxkey[SA1F_NUM_ANIMS];
    if (m == NULL || out == NULL || k == NULL) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    out->keys = 0;
    out->letters = 0;
    out->ladder_len = 0;
    memset(out->ladder, 0, sizeof out->ladder);
    for (n = 0; n < SA1F_NUM_ANIMS; n++) {
        uint32_t p = 0, total = 0;
        int32_t last = 0, first = 0, top = -1;
        if (!mem_rd32(m, SA1F_ADDR_FACETAB + 4u * n, &p)) {
            return SA1_ERR_BAD_FORMAT;
        }
        for (i = 0; i < SA1F_MAX_STEPS; i++) {
            const uint8_t *s = m->ptr(m->user, p + 2u * i, 2u);
            int32_t t, key;
            if (s == NULL) {
                return SA1_ERR_BAD_FORMAT;
            }
            t = (int8_t)s[0];
            key = (int8_t)s[1];
            if (t == 0) {
                break;
            }
            if (t < 0 || key < 0) {
                return SA1_ERR_BAD_FORMAT;
            }
            if (i == 0) {
                first = t;
            }
            total += (uint32_t)t;
            last = key;
            top = key > top ? key : top;
        }
        if (i == 0 || i == SA1F_MAX_STEPS) {
            return SA1_ERR_BAD_FORMAT;
        }
        out->steps[n] = (uint16_t)total;
        out->first[n] = (uint8_t)first;
        out->last_key[n] = (uint8_t)last;
        maxkey[n] = (uint32_t)top;
    }
    if (k->ev_head != 0u) {
        uint32_t mdata = 0, idx = 0, keys = 0xFFFFFFFFu;
        if (!mem_rd32(m, k->motion, &mdata) || !walk_keys(m, k->ev_head, mdata, &idx, &keys, 0u) ||
            keys == 0xFFFFFFFFu || keys == 0u || keys > 64u) {
            return SA1_ERR_BAD_FORMAT;
        }
        out->keys = keys;
        for (n = 0; n < SA1F_NUM_ANIMS; n++) {
            if (maxkey[n] < keys) {
                out->letters |= 1u << n;
            }
        }
        {
            const sa1f_ladder *l = &k_ladders[kind];
            uint32_t j, ok = l->n >= 2u;
            for (j = 0; j < l->n; j++) {
                ok &= l->keys[j] < keys ? 1u : 0u;      /* every rung a key of this head (Big: 8 keys) */
            }
            if (ok) {
                out->ladder_len = l->n;
                memcpy(out->ladder, l->keys, l->n);
            }
        }
    }
    return SA1_OK;
}

/* ---- sa1_face_info_get: the disc ------------------------------------------------------------------------------------ */
typedef struct disc_view {
    const uint8_t *data[2];
    uint32_t base[2], size[2];      /* phys load address, bytes */
} disc_view;

static const uint8_t *disc_ptr(const void *user, uint32_t guest, uint32_t n)
{
    const disc_view *v = (const disc_view *)user;
    uint32_t phys = guest & 0x1FFFFFFFu, i;
    for (i = 0; i < 2u; i++) {
        if (v->data[i] != NULL && phys >= v->base[i] && phys - v->base[i] <= v->size[i] &&
            n <= v->size[i] - (phys - v->base[i])) {
            return v->data[i] + (phys - v->base[i]);
        }
    }
    return NULL;
}

sa1_status sa1_face_info_get(sa1_context *ctx, int32_t kind, sa1_face_info *out)
{
    sa1_api_ext *x;
    sa1_disc disc;
    sa1_face_info info;
    disc_view v;
    sa1f_mem m;
    uint8_t *fr = NULL, *raw = NULL, *tk = NULL;
    uint32_t frn = 0, rawn = 0, tkn = 0;
    char hex[41];
    sa1_status st;
    if (ctx == NULL || out == NULL || out->struct_size < sizeof(uint32_t)) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    if (kind < 0 || kind >= SA1_CHAR_COUNT) {
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_face_info_get: unknown character kind %d", (int)kind);
    }
    x = sa1_api_ext_of(ctx);
    if (!ctx->disc_loaded || x == NULL || !x->gdi[0]) {
        return sa1_fail(ctx, SA1_ERR_NOT_LOADED, "sa1_face_info_get: no disc loaded (the numbers are read from the disc of "
                        "sa1_load_disc)");
    }
    memset(&disc, 0, sizeof disc);
    st = sa1_disc_open(ctx, x->gdi, &disc);
    if (st != SA1_OK) {
        return sa1_fail(ctx, st, "sa1_face_info_get: cannot open the disc (%s)", sa1_status_string(st));
    }
    st = sa1_disc_read_file(&disc, SA1_FIRST_READ_NAME, &fr, &frn);
    if (st == SA1_OK) {
        sa1_sha1_hex(fr, frn, hex);
        if (strcmp(hex, SA1_FIRST_READ_SHA1) != 0) {
            st = SA1_ERR_BAD_HASH;
        }
    }
    if (st == SA1_OK && kind == SA1_CHAR_TIKAL) {
        const sa1_file_desc *fd = sa1_manifest_file("TIKAL_PROG.PRS");
        st = sa1_disc_read_file(&disc, "SONICADV/TIKAL_PROG.PRS", &raw, &rawn);
        if (st == SA1_OK) {
            sa1_sha1_hex(raw, rawn, hex);
            if (fd == NULL || fd->size != rawn || strcmp(fd->sha1, hex) != 0) {
                st = SA1_ERR_BAD_HASH;
            }
        }
        if (st == SA1_OK) {
            st = sa1_prs_decompress(ctx, raw, rawn, SA1_MAX_UNPACKED_SIZE, &tk, &tkn);
        }
    }
    sa1_disc_close(&disc);
    sa1_mem_free(ctx, raw);
    if (st != SA1_OK) {
        sa1_mem_free(ctx, fr);
        sa1_mem_free(ctx, tk);
        return sa1_fail(ctx, st, "sa1_face_info_get: cannot read %s from the disc (%s)",
                        kind == SA1_CHAR_TIKAL ? "1ST_READ.BIN / TIKAL_PROG.PRS" : "1ST_READ.BIN", sa1_status_string(st));
    }
    memset(&v, 0, sizeof v);
    v.data[0] = fr;
    v.base[0] = 0x0C010000u;
    v.size[0] = frn;
    v.data[1] = tk;
    v.base[1] = 0x0CB00000u;
    v.size[1] = tkn;
    m.ptr = disc_ptr;
    m.user = &v;
    memset(&info, 0, sizeof info);
    info.struct_size = sizeof info;
    st = sa1f_compute(&m, kind, &info);
    sa1_mem_free(ctx, fr);
    sa1_mem_free(ctx, tk);
    if (st != SA1_OK) {
        return sa1_fail(ctx, st, "sa1_face_info_get: the face data of %s is not as expected", k_display[kind]);
    }
    sa1_copy_out(out, &info, sizeof info);
    return SA1_OK;
}

/* ---- the calls -------------------------------------------------------------------------------------------------- */
/* sa1_last_error without a log line (the per-frame path) */
static sa1_status quiet(sa1_context *ctx, sa1_status st, const char *fmt, ...) SA1_PRINTF(3, 4);
static sa1_status quiet(sa1_context *ctx, sa1_status st, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(ctx->error, sizeof ctx->error, fmt, ap);
    va_end(ap);
    return st;
}


/* The character's kind, refusals for the kinds without a face (Eggman, Tikal, E-102, Super Sonic), and its world (built if
 * needed). SA1_NO_WORLD: SA1_ERR_NOT_IMPLEMENTED. `what` names the call in the error text. */
static sa1_status face_world(sa1_context *ctx, sa1_character_id id, int silent, const char *what, int32_t *kind_out,
                             struct sa1g_world **out)
{
    uint32_t slot = ((uint32_t)id & 0xFFFFu) - 1u, gen = ((uint32_t)id >> 16) & 0x7FFFu;
    int32_t kind;
    *out = NULL;
    if (id <= 0 || slot >= SA1_MAX_INSTANCES || ctx->inst[slot] == NULL || ctx->inst_gen[slot] != gen) {
        return silent ? quiet(ctx, SA1_ERR_NOT_FOUND, "%s: no character %d", what, (int)id)
                      : sa1_fail(ctx, SA1_ERR_NOT_FOUND, "%s: no character %d", what, (int)id);
    }
    kind = ctx->inst[slot]->kind;
    *kind_out = kind;
    if (kind < 0 || kind >= SA1_CHAR_COUNT || sa1f_kind_of(kind)->ev_head == 0u || kind == SA1_CHAR_TIKAL) {
        const char *why = kind == SA1_CHAR_GAMMA ? "E-102 has no face in the game (no face task)"
                        : kind == SA1_CHAR_SUPER_SONIC ? "Super Sonic has no event head in the game"
                        : kind == SA1_CHAR_TIKAL ? "Tikal cannot tick in libsa1 yet"
                        : "this character has no event face";
        return silent ? quiet(ctx, SA1_ERR_UNSUPPORTED, "%s: %s", what, why)
                      : sa1_fail(ctx, SA1_ERR_UNSUPPORTED, "%s: %s", what, why);
    }
#if defined(SA1_NO_WORLD)
    (void)out;
    return quiet(ctx, SA1_ERR_NOT_IMPLEMENTED, "%s: this libsa1 build has no world core (SA1_NO_WORLD): no game logic",
                 what);
#else
    {
        /* the world that exists already (the per-frame level path: no lookup beyond this, no log), else built now */
        struct sa1g_world *w = sa1_character_world(ctx, id);
        sa1_status st = SA1_OK;
        if (w == NULL) {
            st = sa1w_character_world_ensure(ctx, id, &w);
        }
        if (SA1_FAILED(st) || w == NULL) {
            return SA1_FAILED(st) ? st : SA1_ERR_NOT_LOADED;
        }
        *out = w;
        return SA1_OK;
    }
#endif
}

#if !defined(SA1_NO_WORLD)
static char letter_of(uint32_t n)
{
    return (char)(n < 26u ? 'A' + (int)n : 'a' + (int)n - 26);
}

/* the letters of `bad` (a mask), as "F, G, Z" */
static void letters_text(uint32_t bad, char *buf, size_t cap)
{
    uint32_t n;
    size_t o = 0;
    buf[0] = 0;
    for (n = 0; n < SA1F_NUM_ANIMS && o + 4u < cap; n++) {
        if (bad & (1u << n)) {
            if (o != 0) {
                buf[o++] = ',';
                buf[o++] = ' ';
            }
            buf[o++] = letter_of(n);
            buf[o] = 0;
        }
    }
}

static sa1_status check_letters(sa1_context *ctx, struct sa1g_world *w, int32_t kind, uint32_t used, const char *what)
{
    uint32_t ok = sa1wf_world_letters(w), bad = used & ~ok;
    char txt[96];
    if (bad == 0u) {
        return SA1_OK;
    }
    letters_text(bad, txt, sizeof txt);
    return sa1_fail(ctx, SA1_ERR_UNSUPPORTED, "%s: %s's face cannot show %s", what, k_display[kind], txt);
}
#endif

sa1_status sa1_character_face_mode(sa1_context *ctx, sa1_character_id id, uint32_t mode)
{
    static const char what[] = "sa1_character_face_mode";
    struct sa1g_world *w = NULL;
    int32_t kind = -1;
    sa1_status st;
    if (ctx == NULL) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    if ((mode & ~(0xFFu | SA1_FACE_PACE_CUTSCENE)) != 0u || (mode & 0xFFu) > SA1_FACE_MODE_EVENT) {
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "%s: mode %#x (SA1_FACE_MODE_GAME / EVENT, | SA1_FACE_PACE_CUTSCENE)",
                        what, (unsigned)mode);
    }
    st = face_world(ctx, id, 0, what, &kind, &w);
    if (st != SA1_OK) {
        return st;
    }
#if defined(SA1_NO_WORLD)
    return SA1_ERR_NOT_IMPLEMENTED;
#else
    return sa1wf_latch_mode(ctx, w, mode);
#endif
}

sa1_status sa1_character_face(sa1_context *ctx, sa1_character_id id, const char *script, uint32_t flags)
{
    static const char what[] = "sa1_character_face";
    struct sa1g_world *w = NULL;
    int32_t kind = -1;
    uint32_t len = 0, used = 0, zero = 0;
    sa1_status st;
    if (ctx == NULL) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    if ((flags & ~(SA1_FACE_NOW | SA1_FACE_LOOP)) != 0u) {
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "%s: flags %#x (SA1_FACE_NOW | SA1_FACE_LOOP)", what,
                        (unsigned)flags);
    }
    if (script != NULL) {
        for (len = 0; script[len] != 0; len++) {
            char c = script[len];
            if (len >= SA1F_SCRIPT_MAX) {
                return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "%s: more than %u letters", what, (unsigned)SA1F_SCRIPT_MAX);
            }
            if (c >= 'A' && c <= 'Z') {
                used |= 1u << (uint32_t)(c - 'A');
            } else if (c >= 'a' && c <= 'e') {
                used |= 1u << (uint32_t)(c - 'a' + 26);
            } else if (c == '0') {
                zero = 1;
            } else {
                return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "%s: '%c' (byte %u) is not a face letter A-Z, a-e or 0",
                                what, (c >= 32 && c < 127) ? c : '?', (unsigned)(uint8_t)c);
            }
        }
    }
    if (len != 0 && (flags & SA1_FACE_LOOP) && zero) {
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "%s: a looping script may not contain '0' (it ends with the next "
                        "face call)", what);
    }
    st = face_world(ctx, id, 0, what, &kind, &w);
    if (st != SA1_OK) {
        return st;
    }
#if defined(SA1_NO_WORLD)
    (void)used;
    return SA1_ERR_NOT_IMPLEMENTED;
#else
    if (len == 0) {
        return sa1wf_latch_stop(ctx, w);
    }
    st = check_letters(ctx, w, kind, used, what);
    if (st != SA1_OK) {
        return st;
    }
    return sa1wf_latch_script(ctx, w, script, len, flags);
#endif
}

sa1_status sa1_character_face_sequence(sa1_context *ctx, sa1_character_id id, const sa1_face_entry *entries,
                                       uint32_t count, uint32_t flags)
{
    static const char what[] = "sa1_character_face_sequence";
    struct sa1g_world *w = NULL;
    int32_t kind = -1;
    uint32_t i, used = 0;
    sa1_status st;
    if (ctx == NULL) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    if ((flags & ~SA1_FACE_HOLD_LAST) != 0u) {
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "%s: flags %#x (SA1_FACE_HOLD_LAST)", what, (unsigned)flags);
    }
    if (count > SA1_FACE_SEQ_MAX) {
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "%s: %u entries (at most %u)", what, (unsigned)count,
                        (unsigned)SA1_FACE_SEQ_MAX);
    }
    if (count != 0u && entries == NULL) {
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "%s: no entries", what);
    }
    for (i = 0; i < count; i++) {
        const sa1_face_entry *e = &entries[i];
        if (e->anim >= SA1F_NUM_ANIMS || e->enter > SA1F_STEP_TIME_MAX || e->hold > SA1_FACE_HOLD_MAX ||
            e->reserved != 0u || e->reserved2 != 0u) {
            return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "%s: entry %u: animation %u (0-30), enter %u (0-127), hold %u "
                            "(0-%u), reserved %u / %u (0)", what, (unsigned)i, (unsigned)e->anim, (unsigned)e->enter,
                            (unsigned)e->hold, (unsigned)SA1_FACE_HOLD_MAX, (unsigned)e->reserved,
                            (unsigned)e->reserved2);
        }
        used |= 1u << e->anim;
    }
    st = face_world(ctx, id, 0, what, &kind, &w);
    if (st != SA1_OK) {
        return st;
    }
#if defined(SA1_NO_WORLD)
    (void)used;
    return SA1_ERR_NOT_IMPLEMENTED;
#else
    if (count == 0u) {
        return sa1wf_latch_stop(ctx, w);
    }
    st = check_letters(ctx, w, kind, used, what);
    if (st != SA1_OK) {
        return st;
    }
    return sa1wf_latch_sequence(ctx, w, entries, count, flags);
#endif
}

sa1_status sa1_character_face_level(sa1_context *ctx, sa1_character_id id, float level, uint32_t style)
{
    static const char what[] = "sa1_character_face_level";
    struct sa1g_world *w = NULL;
    int32_t kind = -1, q;
    sa1_status st;
    if (ctx == NULL) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    /* round 0.9.4c: SA1_FACE_SAME_PAIR may be OR'ed into the LADDER style (not TALK); unknown when not built (k_same_pair) */
    if ((style & ~k_same_pair) > SA1_FACE_LEVEL_TALK ||
        (style != (style & ~k_same_pair) && (style & ~k_same_pair) == SA1_FACE_LEVEL_TALK) || level != level) {
        return quiet(ctx, SA1_ERR_INVALID_ARGUMENT, "%s: style %#x (0 ladder, 1 talk%s), level %s", what, (unsigned)style,
                     k_same_pair ? "; SA1_FACE_SAME_PAIR only with the ladder" : "", level != level ? "NaN" : "ok");
    }
    /* quantized here, in the host's rounding mode: the world works with integers only (no CRT maths, any MXCSR) */
    if (level < 0.0f) {
        q = -1;
    } else if (level >= 1.0f) {
        q = 4096;
    } else {
        q = (int32_t)(level * 4096.0f + 0.5f);
    }
    st = face_world(ctx, id, 1, what, &kind, &w);
    if (st != SA1_OK) {
        return st;
    }
#if defined(SA1_NO_WORLD)
    (void)q;
    return SA1_ERR_NOT_IMPLEMENTED;
#else
    return sa1wf_latch_level(ctx, w, q, style);
#endif
}

/* round 0.9.4c (SA1_API_FACE_MOUTH; sadc docs/research/phoneme_plan.md 4.2, 5.3, 5.4): the host's own mouth. The same
 * per-frame path as the level (no allocation, no log line: errors only in sa1_last_error); the keys are checked by the world
 * side against the world's own EV head (its key count, cached in the face block) - also on this per-frame path. */
sa1_status sa1_character_face_mouth(sa1_context *ctx, sa1_character_id id, uint32_t key_a, uint32_t key_b, float weight,
                                    uint32_t flags)
{
    static const char what[] = "sa1_character_face_mouth";
    struct sa1g_world *w = NULL;
    int32_t kind = -1, q;
    sa1_status st;
    if (ctx == NULL) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    if ((flags & ~k_same_pair) != 0u || weight != weight) {
        return quiet(ctx, SA1_ERR_INVALID_ARGUMENT, "%s: flags %#x (%s), weight %s", what, (unsigned)flags,
                     k_same_pair ? "0 or SA1_FACE_SAME_PAIR" : "0", weight != weight ? "NaN" : "ok");
    }
    /* quantized here as the level's (the world works with integers only); > 1 is 1, < 0 is off */
    if (weight < 0.0f) {
        q = -1;
    } else if (weight >= 1.0f) {
        q = 4096;
    } else {
        q = (int32_t)(weight * 4096.0f + 0.5f);
    }
    st = face_world(ctx, id, 1, what, &kind, &w);
    if (st != SA1_OK) {
        return st;
    }
#if defined(SA1_NO_WORLD)
    (void)q;
    (void)key_a;
    (void)key_b;
    return SA1_ERR_NOT_IMPLEMENTED;
#else
    {
        uint32_t keys = 0;
        st = sa1wf_latch_mouth(ctx, w, key_a, key_b, q, flags, &keys);
        if (st == SA1_ERR_INVALID_ARGUMENT) {
            uint32_t bad = key_a >= keys ? key_a : key_b;
            if (keys == 0u) {
                return quiet(ctx, st, "%s: %s's face keys could not be read from the world", what, k_display[kind]);
            }
            return quiet(ctx, st, "%s: key %u is not one of %s's %u face keys (0-%u)", what, (unsigned)bad,
                         k_display[kind], (unsigned)keys, (unsigned)(keys - 1u));
        }
        return st;
    }
#endif
}
