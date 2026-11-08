/*
 * sa1_emote_ev.c - cutscene animations in the emote catalogue: the loader, its cache, the motion block's checker and the
 * lift option (round 100-EV, SA1_API_EMOTE_EVENT; include/libsa1.h "cutscene animations"; sadc docs/research/emotes_ev.md
 * 3.2-3.7 with its review RX.2, RX.6, RX.10).
 *
 * THE LOADER (any thread; the rules of libsa1.h "Preparation on host threads"). sa1_emote_event_load(ctx, kind, index) reads
 * the entry's home cutscene SONICADV/EVxxxx.PRS from the disc of the last sa1_load_disc through a worker context (the host's
 * allocator and file callbacks only: it never calls the host's log, never writes ctx->error or this thread's
 * sa1_last_error of ctx), checks the file's size and SHA-1 against the generated table (src/sa1_emote_events.c: a different
 * file is never accepted, the table's addresses are only valid for the US 1.005 disc), unpacks it with LIB's PRS (cap 1 MB),
 * checks the image size, then walks every catalogue entry of every kind whose home is that cutscene as the generator did:
 * the NJS_ACTION {the kind's body, the motion}, the header inside the image, nbFrame / type / inp_fn = the table's, per node
 * (the table's node count) each channel pointer 0 or inside the image and aligned, its key count <= nbFrame + 1, its keys
 * inside the image, the total = the table's bytes. Each passes into one host block (a relocatable NJS_MOTION: pointers are
 * offsets from the block's start; sa1em_motion_block in src/sa1_emote.h) and sa1em_motion_check; the unpacked image is freed
 * at once. The blocks are published under a short spin lock; a second thread that loaded the same file meanwhile frees its
 * own copies (one copy each, no waiting). A failed file keeps its status and a short reason in the cache (SA1_ERR_BAD_HASH,
 * SA1_ERR_BAD_FORMAT; an I/O or memory failure is not kept: the next call tries again); the game thread's
 * sa1_character_emote reports it (sa1_fail there, where logging is allowed: review RX.6). Never at the same time as
 * sa1_load_disc, sa1_unload_disc, sa1_set_coords or sa1_shutdown of the context: G-SA1 drains its loads before them.
 *
 * THE CACHE lives in ctx->emote_ev (zero until the first load or option, freed with the disc by sa1_unload_disc, as
 * ctx->skin). The lift option (SA1_COPT_EMOTE_LIFT, host deviation D-EV1) is kept there per character slot and generation;
 * the reference worlds never set it, so sa1ev_lift_of answers 0 at once for them.
 */
#include "sa1_emote_ev.h"

#include <stdio.h>
#include <string.h>

#define SA1EV_MAGIC     0x56454D45u     /* "EMEV" */
#define SA1EV_MAX_IMAGE (1u << 20)      /* EV0034 is the largest: 524,656 bytes (MEASURED, emotes_ev.md 2.1) */
#define SA1EV_WHY       96u
#define SA1EV_LOAD      0x0CB80000u     /* every cutscene image loads here */
#define SA1EV_MAX_KEYS  (16u * 1024u * 1024u)

typedef struct sa1ev_state {
    uint32_t magic;
    const sa1em_motion_block *volatile *blk[SA1_CHAR_COUNT];   /* per kind: one pointer per entry, NULL until loaded */
    int32_t fstat[256];                 /* per file: 0 none / tried, < 0 the kept failure */
    char why[256][SA1EV_WHY];
    uint32_t loaded, bytes;             /* entries loaded, their bytes */
    uint16_t lift[SA1_MAX_INSTANCES];   /* SA1_COPT_EMOTE_LIFT: the slot's generation + 1 while on, 0 off */
    uint32_t lift_any;
} sa1ev_state;

typedef char sa1ev_files_check[256 >= 145 ? 1 : -1];

static volatile int32_t g_lock;

static void ev_lock(void)
{
    while (sa1_atomic_add32(&g_lock, 1) != 1) {
        sa1_atomic_add32(&g_lock, -1);
    }
}

static void ev_unlock(void)
{
    sa1_atomic_add32(&g_lock, -1);
}

/* ---- the catalogue --------------------------------------------------------------------------------------------------- */
const sa1ev_entry *sa1ev_entry_of(int32_t kind, uint32_t index)
{
    const sa1ev_list *l = sa1ev_list_of(kind);
    return l != NULL && index < l->count ? &l->e[index] : NULL;
}

const sa1ev_file *sa1ev_file_of(uint32_t event)
{
    uint32_t i;
    for (i = 0; i < sa1ev_num_files; i++) {
        if (sa1ev_files[i].event == event) {
            return &sa1ev_files[i];
        }
    }
    return NULL;
}

/* the gameplay bodies the cutscenes bind (MEASURED, emotes_ev.md 2.2; the generator's CHARS) */
uint32_t sa1ev_body(int32_t kind, uint32_t object)
{
    switch (kind) {
    case SA1_CHAR_SONIC:
        return object == 0 ? 0x8C1CE76Cu : 0u;
    case SA1_CHAR_TAILS:
        return object == 0 ? 0x8C25F968u : object == 1 ? 0x8C26769Cu : 0u;
    case SA1_CHAR_KNUCKLES:
        return object == 0 ? 0x8C35B094u : 0u;
    case SA1_CHAR_AMY:
        return object == 0 ? 0x8C4C7C3Cu : 0u;
    case SA1_CHAR_GAMMA:
        return object == 0 ? 0x8C42CC34u : 0u;
    case SA1_CHAR_BIG:
        return object == 0 ? 0x8C52F298u : 0u;
    default:
        return 0u;
    }
}

/* the body's node count (the per-node table of a motion for it) */
static uint32_t ev_body_nodes(int32_t kind)
{
    switch (kind) {
    case SA1_CHAR_SONIC:
    case SA1_CHAR_KNUCKLES:
    case SA1_CHAR_BIG:
        return 73u;
    case SA1_CHAR_TAILS:
        return 68u;
    case SA1_CHAR_AMY:
        return 78u;
    case SA1_CHAR_GAMMA:
        return 77u;
    default:
        return 0u;
    }
}

/* ---- the block's checker (the seam of src/sa1_emote.h; photo mode's loaded motions reuse it) --------------------------- */
sa1_status sa1em_motion_check(int32_t kind, uint32_t object, const sa1em_motion_block *b)
{
    uint32_t nf, i, k, md, nodes;
    if (b == NULL || b->data == NULL || b->size < 12u || b->size > SA1EM_SLAB_SIZE) {
        return SA1_ERR_BAD_FORMAT;
    }
    nodes = ev_body_nodes(kind);
    if (nodes == 0u || sa1ev_body(kind, object) == 0u || b->nodes != nodes) {
        return SA1_ERR_BAD_FORMAT;
    }
    md = sa1_rd32(b->data);
    nf = (uint32_t)(b->inp_fn & 0xFu);
    if (sa1_rd32(b->data + 4) != b->frames || sa1_rd16(b->data + 8) != b->type || sa1_rd16(b->data + 10) != b->inp_fn) {
        return SA1_ERR_BAD_FORMAT;
    }
    if (b->frames < 1u || b->frames > 100000u || nf < 1u || nf > 3u || (b->type & ~7u) != 0u ||
        (uint32_t)((b->type & 1u) + ((b->type >> 1) & 1u) + ((b->type >> 2) & 1u)) != nf) {
        return SA1_ERR_BAD_FORMAT;
    }
    if (md != 12u || (uint64_t)md + 8ull * nf * nodes > b->size) {
        return SA1_ERR_BAD_FORMAT;
    }
    for (i = 0; i < nodes; i++) {
        const uint8_t *e = b->data + md + 8u * nf * i;
        for (k = 0; k < nf; k++) {
            uint32_t p = sa1_rd32(e + 4u * k), n = sa1_rd32(e + 4u * (nf + k)), j;
            if (p == 0u || n == 0u) {
                continue;
            }
            if ((p & 3u) != 0u || p < md + 8u * nf * nodes || n > b->frames + 1u || (uint64_t)p + 16ull * n > b->size) {
                return SA1_ERR_BAD_FORMAT;
            }
            for (j = 0; j < n; j++) {
                if (sa1_rd32(b->data + p + 16u * j) > b->frames) {
                    return SA1_ERR_BAD_FORMAT;      /* a key frame past the motion */
                }
            }
        }
    }
    return SA1_OK;
}

/* ---- the state ---------------------------------------------------------------------------------------------------------- */
/* the context's state, made on first use (under the lock: any thread) */
static sa1ev_state *ev_state(sa1_context *ctx)
{
    sa1ev_state *S;
    ev_lock();
    S = ctx->emote_ev;
    ev_unlock();
    if (S != NULL) {
        return S;
    }
    S = (sa1ev_state *)sa1_mem_alloc(ctx, sizeof *S);
    if (S == NULL) {
        return NULL;
    }
    S->magic = SA1EV_MAGIC;
    ev_lock();
    if (ctx->emote_ev == NULL) {
        ctx->emote_ev = S;
        S = NULL;
    }
    ev_unlock();
    if (S != NULL) {
        sa1_mem_free(ctx, S);   /* another thread made it meanwhile */
    }
    return ctx->emote_ev;
}

void sa1ev_free(sa1_context *ctx)
{
    sa1ev_state *S;
    int32_t k;
    uint32_t i;
    if (ctx == NULL || ctx->emote_ev == NULL) {
        return;
    }
    S = ctx->emote_ev;
    ctx->emote_ev = NULL;
    for (k = 0; k < SA1_CHAR_COUNT; k++) {
        const sa1ev_list *l = sa1ev_list_of(k);
        if (S->blk[k] == NULL) {
            continue;
        }
        for (i = 0; l != NULL && i < l->count; i++) {
            sa1em_motion_block *b = (sa1em_motion_block *)(uintptr_t)S->blk[k][i];
            if (b != NULL) {
                sa1_mem_free(ctx, b);
            }
        }
        sa1_mem_free(ctx, (void *)(uintptr_t)S->blk[k]);
    }
    sa1_mem_free(ctx, S);
}

/* ---- the loader --------------------------------------------------------------------------------------------------------- */
typedef struct ev_image {
    const uint8_t *d;
    uint32_t n;
} ev_image;

static int ev_in(const ev_image *im, uint32_t a, uint32_t len)
{
    uint32_t p = a & 0x1FFFFFFFu, b = SA1EV_LOAD & 0x1FFFFFFFu;
    return a != 0u && (a & 3u) == 0u && p >= b && (uint64_t)(p - b) + len <= im->n;
}

static uint32_t ev_rd(const ev_image *im, uint32_t a)
{
    return sa1_rd32(im->d + ((a & 0x1FFFFFFFu) - (SA1EV_LOAD & 0x1FFFFFFFu)));
}

/* one entry's motion out of the image into a new block; NULL with the reason in why */
static sa1em_motion_block *ev_extract(sa1_context *wctx, const ev_image *im, int32_t kind, const sa1ev_entry *e, char *why,
                                      size_t n)
{
    uint32_t md, nbf, typ, inp, nf, nodes = e->nodes, i, k, total, off;
    sa1em_motion_block *b;
    uint8_t *data;
    if (!ev_in(im, e->action, 8u) || (ev_rd(im, e->action) & 0x1FFFFFFFu) != (sa1ev_body(kind, e->object) & 0x1FFFFFFFu) ||
        ev_rd(im, e->action + 4u) != e->motion) {
        snprintf(why, n, "%s: its NJS_ACTION is not the table's", e->name);
        return NULL;
    }
    if (!ev_in(im, e->motion, 12u)) {
        snprintf(why, n, "%s: the motion is outside the cutscene", e->name);
        return NULL;
    }
    md = ev_rd(im, e->motion);
    nbf = ev_rd(im, e->motion + 4u);
    typ = ev_rd(im, e->motion + 8u) & 0xFFFFu;
    inp = ev_rd(im, e->motion + 8u) >> 16;
    nf = inp & 0xFu;
    if (nbf != e->frames || typ != e->type || inp != e->inp_fn || nf < 1u || nf > 3u || nodes != ev_body_nodes(kind)) {
        snprintf(why, n, "%s: the motion's header is not the table's", e->name);
        return NULL;
    }
    if (!ev_in(im, md, 8u * nf * nodes)) {
        snprintf(why, n, "%s: the per-node table is outside the cutscene", e->name);
        return NULL;
    }
    total = 12u + 8u * nf * nodes;
    for (i = 0; i < nodes; i++) {
        for (k = 0; k < nf; k++) {
            uint32_t p = ev_rd(im, md + 8u * nf * i + 4u * k), c = ev_rd(im, md + 8u * nf * i + 4u * (nf + k));
            if (p == 0u || c == 0u) {
                continue;
            }
            if (c > nbf + 1u || !ev_in(im, p, 16u * c)) {
                snprintf(why, n, "%s: node %u's keys are outside the cutscene", e->name, (unsigned)i);
                return NULL;
            }
            total += 16u * c;
        }
    }
    if (total != e->bytes || total > SA1EV_MAX_KEYS) {
        snprintf(why, n, "%s: %u bytes, the table says %u", e->name, (unsigned)total, (unsigned)e->bytes);
        return NULL;
    }
    b = (sa1em_motion_block *)sa1_mem_alloc(wctx, sizeof *b + total);
    if (b == NULL) {
        snprintf(why, n, "out of memory");
        return NULL;
    }
    data = (uint8_t *)(b + 1);
    memset(data, 0, total);
    {
        uint32_t hdr[3];
        hdr[0] = 12u;
        hdr[1] = nbf;
        hdr[2] = typ | (inp << 16);
        for (i = 0; i < 3u; i++) {
            data[4u * i + 0u] = (uint8_t)hdr[i];
            data[4u * i + 1u] = (uint8_t)(hdr[i] >> 8);
            data[4u * i + 2u] = (uint8_t)(hdr[i] >> 16);
            data[4u * i + 3u] = (uint8_t)(hdr[i] >> 24);
        }
    }
    off = 12u + 8u * nf * nodes;
    for (i = 0; i < nodes; i++) {
        uint8_t *ent = data + 12u + 8u * nf * i;
        for (k = 0; k < nf; k++) {
            uint32_t p = ev_rd(im, md + 8u * nf * i + 4u * k), c = ev_rd(im, md + 8u * nf * i + 4u * (nf + k)), v;
            v = (p != 0u && c != 0u) ? off : 0u;
            ent[4u * k + 0u] = (uint8_t)v;
            ent[4u * k + 1u] = (uint8_t)(v >> 8);
            ent[4u * k + 2u] = (uint8_t)(v >> 16);
            ent[4u * k + 3u] = (uint8_t)(v >> 24);
            ent[4u * (nf + k) + 0u] = (uint8_t)c;
            ent[4u * (nf + k) + 1u] = (uint8_t)(c >> 8);
            ent[4u * (nf + k) + 2u] = (uint8_t)(c >> 16);
            ent[4u * (nf + k) + 3u] = (uint8_t)(c >> 24);
            if (v != 0u) {
                memcpy(data + off, im->d + ((p & 0x1FFFFFFFu) - (SA1EV_LOAD & 0x1FFFFFFFu)), 16u * c);
                off += 16u * c;
            }
        }
    }
    b->size = total;
    b->nodes = nodes;
    b->frames = nbf;
    b->type = (uint16_t)typ;
    b->inp_fn = (uint16_t)inp;
    b->data = data;
    if (sa1em_motion_check(kind, e->object, b) != SA1_OK) {
        snprintf(why, n, "%s: the copy fails the motion check", e->name);
        sa1_mem_free(wctx, b);
        return NULL;
    }
    return b;
}

/* the per-kind pointer arrays (under the lock) */
static int ev_arrays(sa1_context *ctx, sa1ev_state *S)
{
    int32_t k;
    for (k = 0; k < SA1_CHAR_COUNT; k++) {
        const sa1ev_list *l = sa1ev_list_of(k);
        if (l != NULL && S->blk[k] == NULL) {
            void *a = sa1_mem_array(ctx, l->count, sizeof(void *));
            if (a == NULL) {
                return 0;
            }
            ev_lock();
            if (S->blk[k] == NULL) {
                S->blk[k] = (const sa1em_motion_block *volatile *)a;
                a = NULL;
            }
            ev_unlock();
            if (a != NULL) {
                sa1_mem_free(ctx, a);
            }
        }
    }
    return 1;
}

static void ev_keep(sa1ev_state *S, uint32_t f, sa1_status st, const char *why)
{
    ev_lock();
    if (S->fstat[f] == 0) {
        S->fstat[f] = st;
        snprintf(S->why[f], sizeof S->why[f], "%s", why);
    }
    ev_unlock();
}

sa1_status sa1_emote_event_load(sa1_context *ctx, int32_t kind, uint32_t index)
{
    const sa1ev_entry *e = sa1ev_entry_of(kind, index);
    const sa1ev_file *fd;
    sa1ev_state *S;
    sa1_api_ext *x;
    sa1_context *wctx;
    sa1_disc disc;
    uint8_t *raw = NULL, *img = NULL;
    uint32_t rawn = 0, imgn = 0, fi, kk;
    sa1_status st;
    char path[48], hex[41], why[SA1EV_WHY];
    sa1em_motion_block *got[SA1_CHAR_COUNT][64];
    uint32_t got_i[SA1_CHAR_COUNT][64], ngot[SA1_CHAR_COUNT];
    if (ctx == NULL || e == NULL || e->file >= sa1ev_num_files) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    x = sa1_api_ext_of(ctx);
    if (!ctx->disc_loaded || x == NULL || !x->gdi[0]) {
        return SA1_ERR_NOT_LOADED;
    }
    S = ev_state(ctx);
    if (S == NULL || !ev_arrays(ctx, S)) {
        return SA1_ERR_OUT_OF_MEMORY;
    }
    fi = e->file;
    fd = &sa1ev_files[fi];
    ev_lock();
    st = S->blk[kind][index] != NULL ? SA1_OK : S->fstat[fi] < 0 ? (sa1_status)S->fstat[fi] : SA1_PARTIAL;
    ev_unlock();
    if (st != SA1_PARTIAL) {
        return st;
    }
    wctx = sa1_worker_ctx_create(ctx);
    if (wctx == NULL) {
        return SA1_ERR_OUT_OF_MEMORY;
    }
    memset(&disc, 0, sizeof disc);
    snprintf(path, sizeof path, "SONICADV/EV%04X.PRS", (unsigned)fd->event);
    st = sa1_disc_open(wctx, x->gdi, &disc);
    if (st == SA1_OK) {
        st = sa1_disc_read_file(&disc, path, &raw, &rawn);
        sa1_disc_close(&disc);
        if (st != SA1_OK) {
            st = SA1_ERR_IO;
        }
    }
    if (st == SA1_OK) {
        sa1_sha1_hex(raw, rawn, hex);
        if (rawn != fd->prs_size || strcmp(hex, fd->sha1) != 0) {
            st = SA1_ERR_BAD_HASH;
            snprintf(why, sizeof why, "your disc's EV%04X.PRS does not match the US 1.005 disc", (unsigned)fd->event);
            ev_keep(S, fi, st, why);
        }
    }
    if (st == SA1_OK) {
        st = sa1_prs_decompress(wctx, raw, rawn, SA1EV_MAX_IMAGE, &img, &imgn);
        if (st == SA1_OK && imgn != fd->image_size) {
            st = SA1_ERR_BAD_FORMAT;
        }
        if (st == SA1_ERR_BAD_FORMAT || (st != SA1_OK && st != SA1_ERR_OUT_OF_MEMORY)) {
            st = SA1_ERR_BAD_FORMAT;
            snprintf(why, sizeof why, "EV%04X.PRS does not unpack to the cutscene's image", (unsigned)fd->event);
            ev_keep(S, fi, st, why);
        }
    }
    sa1_mem_free(wctx, raw);
    memset(ngot, 0, sizeof ngot);
    if (st == SA1_OK) {
        /* every entry of every kind whose home is this cutscene: one read serves the cutscene's whole list */
        ev_image im;
        im.d = img;
        im.n = imgn;
        for (kk = 0; kk < (uint32_t)SA1_CHAR_COUNT && st == SA1_OK; kk++) {
            const sa1ev_list *l = sa1ev_list_of((int32_t)kk);
            uint32_t i;
            for (i = 0; l != NULL && i < l->count && st == SA1_OK; i++) {
                sa1em_motion_block *b;
                if (l->e[i].file != fi) {
                    continue;
                }
                if (ngot[kk] >= 64u) {
                    st = SA1_ERR_LIMIT;
                    break;
                }
                b = ev_extract(wctx, &im, (int32_t)kk, &l->e[i], why, sizeof why);
                if (b == NULL) {
                    st = strcmp(why, "out of memory") == 0 ? SA1_ERR_OUT_OF_MEMORY : SA1_ERR_BAD_FORMAT;
                    if (st == SA1_ERR_BAD_FORMAT) {
                        ev_keep(S, fi, st, why);
                    }
                    break;
                }
                got[kk][ngot[kk]] = b;
                got_i[kk][ngot[kk]] = i;
                ngot[kk]++;
            }
        }
    }
    sa1_mem_free(wctx, img);
    /* publish (or free on a failure / what another thread published first) */
    for (kk = 0; kk < (uint32_t)SA1_CHAR_COUNT; kk++) {
        uint32_t j;
        for (j = 0; j < ngot[kk]; j++) {
            sa1em_motion_block *b = got[kk][j];
            if (st == SA1_OK) {
                ev_lock();
                if (S->blk[kk][got_i[kk][j]] == NULL) {
                    S->blk[kk][got_i[kk][j]] = b;
                    S->loaded++;
                    S->bytes += b->size;
                    b = NULL;
                }
                ev_unlock();
            }
            if (b != NULL) {
                sa1_mem_free(wctx, b);
            }
        }
    }
    sa1_worker_ctx_destroy(wctx);
    return st;
}

int32_t sa1_emote_event_ready(const sa1_context *ctx, int32_t kind, uint32_t index)
{
    const sa1ev_entry *e = sa1ev_entry_of(kind, index);
    sa1ev_state *S;
    int32_t r = 0;
    if (ctx == NULL || e == NULL) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    ev_lock();
    S = ctx->emote_ev;
    if (S != NULL && S->blk[kind] != NULL && S->blk[kind][index] != NULL) {
        r = 1;
    } else if (S != NULL && e->file < sa1ev_num_files && S->fstat[e->file] < 0) {
        r = S->fstat[e->file];
    }
    ev_unlock();
    return r;
}

const sa1em_motion_block *sa1ev_block(const sa1_context *ctx, int32_t kind, uint32_t index, sa1_status *st, char *why,
                                      size_t n)
{
    const sa1ev_entry *e = sa1ev_entry_of(kind, index);
    const sa1em_motion_block *b = NULL;
    sa1ev_state *S;
    *st = SA1_ERR_NOT_LOADED;
    if (why != NULL && n > 0) {
        why[0] = 0;
    }
    if (ctx == NULL || e == NULL) {
        *st = SA1_ERR_INVALID_ARGUMENT;
        return NULL;
    }
    ev_lock();
    S = ctx->emote_ev;
    if (S != NULL && S->blk[kind] != NULL) {
        b = S->blk[kind][index];
    }
    if (b != NULL) {
        *st = SA1_OK;
    } else if (S != NULL && S->fstat[e->file] < 0) {
        *st = (sa1_status)S->fstat[e->file];
        if (why != NULL) {
            snprintf(why, n, "%s", S->why[e->file]);
        }
    }
    ev_unlock();
    return b;
}

/* tests and the status line: the entries loaded and their bytes */
void sa1ev_stats(const sa1_context *ctx, uint32_t *loaded, uint32_t *bytes)
{
    sa1ev_state *S;
    ev_lock();
    S = ctx != NULL ? ctx->emote_ev : NULL;
    *loaded = S != NULL ? S->loaded : 0u;
    *bytes = S != NULL ? S->bytes : 0u;
    ev_unlock();
}

/* ---- the lift option ------------------------------------------------------------------------------------------------------ */
sa1_status sa1ev_set_lift(sa1_context *ctx, sa1_character_id id, int32_t value)
{
    uint32_t slot = ((uint32_t)id & 0xFFFFu) - 1u, gen = ((uint32_t)id >> 16) & 0x7FFFu;
    sa1ev_state *S;
    if (ctx == NULL || id <= 0 || slot >= SA1_MAX_INSTANCES || ctx->inst[slot] == NULL || ctx->inst_gen[slot] != gen) {
        return SA1_ERR_NOT_FOUND;
    }
    if (value != 0 && value != 1) {
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "SA1_COPT_EMOTE_LIFT: %d (0 off, 1 on)", (int)value);
    }
    if (value == 0 && ctx->emote_ev == NULL) {
        return SA1_OK;
    }
    S = ev_state(ctx);
    if (S == NULL) {
        return sa1_fail(ctx, SA1_ERR_OUT_OF_MEMORY, "SA1_COPT_EMOTE_LIFT: out of memory");
    }
    ev_lock();
    S->lift[slot] = value ? (uint16_t)(gen + 1u) : 0u;
    S->lift_any |= (uint32_t)value;
    ev_unlock();
    return SA1_OK;
}

float sa1ev_lift_of(const sa1_context *ctx, const void *inst, const struct sa1g_world *w)
{
    const sa1ev_state *S = ctx != NULL ? ctx->emote_ev : NULL;
    uint32_t slot;
    if (S == NULL || !S->lift_any || inst == NULL || w == NULL) {
        return 0.0f;
    }
    for (slot = 0; slot < SA1_MAX_INSTANCES; slot++) {
        if ((const void *)ctx->inst[slot] == inst) {
            break;
        }
    }
    if (slot >= SA1_MAX_INSTANCES || S->lift[slot] != (uint16_t)(ctx->inst_gen[slot] + 1u)) {
        return 0.0f;
    }
#if !defined(SA1_NO_WORLD)
    return sa1em_world_lift(w);
#else
    return 0.0f;
#endif
}
