/*
 * sa1_capture.c - the host side of the display capture (sadc docs/libsa1_integration.md 7.2, D10; sa1_capture.h).
 *
 * The runtime (src/rt/rt_draw.c, section 5) records the game's draws into a capture block that lives in the world's
 * R4 pcdata (segment 0x18, guest 0x98000000 + ...). This file finds that block (once per world, by its magic: the
 * block is a static of the lowered runtime, with no symbol to link against, and builds without game code have no
 * block at all), opens and closes the capture window around a tick, snapshots the records (and, version 2, the
 * payload pool: sprites and point lists as they were at the draw) of the last pair and emits them through
 * sa1_pose.c's emitter: model draws as triangles, and with the v2 options effects as sprites, line segments and
 * polygons, every draw with its texture resolved into the character's archives (sa1_capture_resolve). The effect
 * draws follow the SDK's own rules (round 4c, sadc docs/research/lightspeed_visuals.md: sprite sizes, polygon and
 * line topology, the one-texture polygon texture; see emit_sprite / emit_lines / emit_polygon).
 *
 * Asset-layer file (C99, no world headers): the few world-core entry points it needs are declared here with the
 * opaque world type; every guest address is bounds-checked against the arena and the capture block's pcdata
 * before it is read (sa1_capture_read), outside any world call.
 */
#include "sa1_capture.h"
#include "sa1_api_world.h"

#include <stddef.h>

#if !defined(SA1_NO_WORLD)
/* world core (src/world/sa1_world.h; one tick at a time, these only compute host addresses) */
void *sa1g_world_ptr(const struct sa1g_world *w, uint32_t guest);
int sa1g_world_rd32(const struct sa1g_world *w, uint32_t guest, uint32_t *v);
uint32_t sa1g_pcd_size(void);
#endif

#define CAP_SEG_ARENA   0x0Cu
#define CAP_SEG_EXT     0x18u
#define CAP_ARENA_SIZE  0x01000000u
#define CAP_EXT_GUEST   0x98000000u
#define CAP_ADDR_FRAME  0x8C752BE8u     /* B_8C752BE8: frame counter, + B_8C754EC4 after each pair */
#define CAP_ADDR_SPF    0x8C754EC4u     /* B_8C754EC4: steps per frame (2) */

/* Ninja sprite attributes (njDrawSprite3D, 8C09E778) */
#define NJ_SPRITE_ANGLE 0x01u
#define NJ_SPRITE_COLOR 0x02u
#define NJ_SPRITE_HFLIP 0x04u
#define NJ_SPRITE_VFLIP 0x08u
#define NJ_SPRITE_SCALE 0x10u
#define NJ_SPRITE_ALPHA 0x20u
/* point-list draws (njDrawLine3D 8C63CE24, njDrawPolygon3D 8C63D414, njDrawTriangle3D 8C63D66C; from the SDK code's
 * attribute tests, lightspeed_visuals.md Review R.1 / R.2.3) */
#define NJ_DRAW_FAN       0x01u         /* triangles: every triangle starts at point 0; lines: every line does */
#define NJ_DRAW_CONNECTED 0x02u         /* triangles: a strip (stride 1); lines: any of 0x0F gives stride 1 */
#define NJ_DRAW_FILL      0x20u         /* polygons / triangles filled (8C64FAE8 / 8C64FC68); without it the SDK draws
                                           their edges as lines (8C64FE0C / 8C650144). njDrawLine3D does not test it */
#define NJ_DRAW_TRANS     0x40u         /* translucent (njColorBlendingMode) */
#define NJ_DRAW_TEXTURE   0x80000000u   /* textured with texture n of the current texlist (njSetTextureNum: not recorded,
                                           so only a one-texture texlist's texture 0 is known) */

struct sa1_capture {
    struct sa1g_world *w;
    uint32_t block;                     /* guest address of the header */
    uint32_t recs;                      /* guest address of the records */
    uint32_t pcd_size;                  /* bytes of pcdata at CAP_EXT_GUEST */
    uint32_t frame_open;                /* frame counter when the window opened */
    uint32_t total_open;                /* records over the world's life when the window opened */
    int32_t valid;                      /* a window ran pairs */
    sa1_cap_hdr hdr;                    /* copy of the header at the last close */
    sa1_cap_rec *snap;                  /* records of the last pair that drew */
    uint32_t nsnap;
    int32_t snap_frame;
    uint8_t *pay;                       /* payload of the last pair that drew (pay_cap bytes) */
    uint32_t npay;
    uint32_t windows, empty_windows, hidden_windows;
    uint32_t vis_shown, vis_hidden;     /* the snapshot's pair: sub-step-0 visibility answers (v4) */
    int32_t hidden;                     /* the snapshot is hidden (sa1_capture_hidden) */
    uint32_t emitted, excluded, clipped, foreign;  /* the last emit */
    uint32_t duplicates, effect_draws, unresolved, sprites, lines, polygons, payload_lost;
    uint32_t zero_sprites, outlines, poly_textured, poly_untextured;
    uint32_t shadows, others;           /* the last emit: blob shadow draws marked, draws left out by only_model */
    uint32_t object_models, object_sprites;     /* the last emit: SA1_DRAW_OBJECT draws (round 0.9.4) */
    int32_t super_gold;                 /* round 0.9.4a: the snapshot's pair is shown in Super Sonic's gold (cap_super_gold) */
    uint32_t gold_sprites, gold_polygons, gold_models;  /* the last emit: SA1_DRAW_SUPER_GOLD draws (round 0.9.4a) */
    /* texture resolution: the NJS_TEXNAME arrays of the character's archives in the world */
    const sa1_context *ctx;
    const sa1_charset *cs;
    uint32_t narc;
    uint32_t arc_tex[SA1_MAX_ARCHIVES], arc_n[SA1_MAX_ARCHIVES];
    uint8_t *alpha[SA1_MAX_ARCHIVES];   /* alpha kind per texture + 1 (0: not computed yet) */
    sa1_meshbuf buf;
};

const uint8_t *sa1_capture_read(void *capture, uint32_t g, uint32_t n)
{
#if !defined(SA1_NO_WORLD)
    const sa1_capture *c = (const sa1_capture *)capture;
    uint32_t seg = (g >> 24) & 31u, off = g & 0xFFFFFFu;
    if (c == NULL || g == 0 || n == 0)
        return NULL;
    if (seg == CAP_SEG_ARENA) {
        if (n > CAP_ARENA_SIZE || off > CAP_ARENA_SIZE - n)
            return NULL;
    } else if (seg == CAP_SEG_EXT) {
        if (n > c->pcd_size || off > c->pcd_size - n)
            return NULL;
    } else {
        return NULL;
    }
    return (const uint8_t *)sa1g_world_ptr(c->w, g);
#else
    (void)capture;
    (void)g;
    (void)n;
    return NULL;
#endif
}

static uint8_t *cap_write_ptr(sa1_capture *c, uint32_t g, uint32_t n)
{
    return (uint8_t *)(uintptr_t)sa1_capture_read(c, g, n);
}

static uint32_t cap_rd32(const sa1_capture *c, uint32_t g)
{
    const uint8_t *p = sa1_capture_read((void *)(uintptr_t)c, g, 4);
    return p ? sa1_rd32(p) : 0u;
}

sa1_status sa1_capture_attach(sa1_context *ctx, struct sa1g_world *w, sa1_capture **out)
{
#if !defined(SA1_NO_WORLD)
    sa1_capture *c;
    uint32_t off;
    *out = NULL;
    if (!ctx || !w)
        return SA1_ERR_INVALID_ARGUMENT;
    c = (sa1_capture *)sa1_mem_alloc(ctx, sizeof *c);
    if (!c)
        return SA1_ERR_OUT_OF_MEMORY;
    c->w = w;
    c->ctx = ctx;
    c->pcd_size = sa1g_pcd_size();
    /* the header: its magic, its own sizes, the records and the payload pool inside the same pcdata */
    for (off = 0; off + sizeof(sa1_cap_hdr) <= c->pcd_size; off += 4) {
        const uint8_t *p = sa1_capture_read(c, CAP_EXT_GUEST + off, (uint32_t)sizeof(sa1_cap_hdr));
        sa1_cap_hdr h;
        if (!p || sa1_rd32(p) != SA1_CAP_MAGIC0)
            continue;
        memcpy(&h, p, sizeof h);
        if (h.magic[1] != SA1_CAP_MAGIC1 || h.magic[2] != SA1_CAP_MAGIC2 || h.magic[3] != SA1_CAP_MAGIC3)
            continue;
        if (h.version != SA1_CAP_VERSION || h.hdr_size != sizeof(sa1_cap_hdr) || h.rec_size != sizeof(sa1_cap_rec) ||
            h.capacity == 0 || h.capacity > 65536u ||
            !sa1_capture_read(c, h.rec, h.capacity * (uint32_t)sizeof(sa1_cap_rec)) ||
            h.pay_cap == 0 || h.pay_cap > SA1_CAP_PAY || !sa1_capture_read(c, h.pay, h.pay_cap))
            break;
        c->block = CAP_EXT_GUEST + off;
        c->recs = h.rec;
        c->snap = (sa1_cap_rec *)sa1_mem_array(ctx, h.capacity, sizeof(sa1_cap_rec));
        c->pay = (uint8_t *)sa1_mem_array(ctx, h.pay_cap, 1);
        if (!c->snap || !c->pay) {
            sa1_mem_free(ctx, c->snap);
            sa1_mem_free(ctx, c->pay);
            sa1_mem_free(ctx, c);
            return SA1_ERR_OUT_OF_MEMORY;
        }
        c->hdr = h;
        *out = c;
        return SA1_OK;
    }
    sa1_mem_free(ctx, c);
    return SA1_ERR_NOT_FOUND;
#else
    (void)w;
    *out = NULL;
    (void)ctx;
    return SA1_ERR_NOT_IMPLEMENTED;
#endif
}

void sa1_capture_free(sa1_context *ctx, sa1_capture *c)
{
    uint32_t a;
    if (!c)
        return;
    sa1_meshbuf_free_exec(ctx, &c->buf);
    sa1_meshbuf_free(ctx, &c->buf);
    for (a = 0; a < SA1_MAX_ARCHIVES; a++)
        sa1_mem_free(ctx, c->alpha[a]);
    sa1_mem_free(ctx, c->snap);
    sa1_mem_free(ctx, c->pay);
    sa1_mem_free(ctx, c);
}

void sa1_capture_set_archives(sa1_capture *c, const sa1_charset *cs)
{
    uint32_t a;
    if (!c || !cs)
        return;
    c->cs = cs;
    c->narc = cs->num_archives < SA1_MAX_ARCHIVES ? cs->num_archives : SA1_MAX_ARCHIVES;
    for (a = 0; a < c->narc; a++) {
        uint32_t tl = cs->archives[a].texlist_addr;
        c->arc_tex[a] = cap_rd32(c, tl) & 0x1FFFFFFFu;
        c->arc_n[a] = tl ? cap_rd32(c, tl + 4u) : 0u;
        if (c->arc_n[a] > SA1_MAX_TEXTURES)
            c->arc_n[a] = 0;
        if (!c->alpha[a] && cs->archives[a].num_textures)
            c->alpha[a] = (uint8_t *)sa1_mem_array(c->ctx, cs->archives[a].num_textures, 1);
    }
}

int32_t sa1_capture_resolve(void *capture, uint32_t texlist, uint32_t id, int32_t *archive)
{
    const sa1_capture *c = (const sa1_capture *)capture;
    uint32_t tex, n, addr, a;
    *archive = -1;
    if (!c || !texlist)
        return -1;
    tex = cap_rd32(c, texlist);
    n = cap_rd32(c, texlist + 4u);
    if (id >= n || !tex)
        return -1;
    addr = (tex & 0x1FFFFFFFu) + 12u * id;
    for (a = 0; a < c->narc; a++) {
        uint32_t base = c->arc_tex[a], rel;
        if (!base || addr < base || addr - base >= 12u * c->arc_n[a])
            continue;
        rel = addr - base;
        if (rel % 12u || !c->cs || rel / 12u >= c->cs->archives[a].num_textures)
            continue;
        *archive = (int32_t)a;
        return (int32_t)(rel / 12u);
    }
    return -1;
}

/* the alpha kind of a character texture, cached per capture */
static uint32_t cap_tex_alpha(void *capture, int32_t archive, int32_t index)
{
    sa1_capture *c = (sa1_capture *)capture;
    const sa1_archive *ar;
    if (!c || !c->cs || archive < 0 || (uint32_t)archive >= c->narc || index < 0)
        return SA1_TEXALPHA_NONE;
    ar = &c->cs->archives[archive];
    if ((uint32_t)index >= ar->num_textures)
        return SA1_TEXALPHA_NONE;
    if (!c->alpha[archive])
        return sa1_texture_alpha_kind(ar->tex[index].rgba, ar->tex[index].width, ar->tex[index].height);
    if (!c->alpha[archive][index])
        c->alpha[archive][index] = (uint8_t)(1u + sa1_texture_alpha_kind(ar->tex[index].rgba, ar->tex[index].width,
                                                                          ar->tex[index].height));
    return c->alpha[archive][index] - 1u;
}

sa1_status sa1_capture_reserve(sa1_context *ctx, sa1_capture *c, uint32_t nv, uint32_t nt, uint32_t nn)
{
    if (!ctx || !c)
        return SA1_ERR_INVALID_ARGUMENT;
    return sa1_meshbuf_reserve(ctx, &c->buf, nv, nt, nn);
}

/* v4: the visibility answers of the snapshot's pair (the header counts them per frame counter value, like the
 * records), and whether the snapshot is hidden: the pair asked the visibility test and every answer was "not
 * visible" (sa1_capture_hidden; the draws that do not ask it - the blob shadow, Big's lure - may still be there).
 * frame_end: the frame counter at the close; a pair that drew nothing is the snapshot frame frame_end - spf in live
 * mode, and frame_end itself for a replay step (its counter is injected, it does not advance), which is the one
 * value no live pair asks the test with */
static void cap_vis_of_snapshot(sa1_capture *c, uint32_t frame_end)
{
    c->vis_shown = 0;
    c->vis_hidden = 0;
    if (c->hdr.version >= 4u && (c->hdr.vis_frame == (uint32_t)c->snap_frame ||
                                 (c->nsnap == 0 && c->hdr.vis_frame == frame_end))) {
        c->vis_shown = c->hdr.vis_shown;
        c->vis_hidden = c->hdr.vis_hidden;
    }
    c->hidden = c->vis_hidden != 0 && c->vis_shown == 0;
    if (c->hidden)
        c->hidden_windows++;
}

void sa1_capture_open(sa1_capture *c)
{
    uint8_t *h;
    uint32_t one = 1, zero = 0;
    if (!c)
        return;
    h = cap_write_ptr(c, c->block, (uint32_t)sizeof(sa1_cap_hdr));
    if (!h)
        return;
    c->frame_open = cap_rd32(c, CAP_ADDR_FRAME);
    c->total_open = sa1_rd32(h + offsetof(sa1_cap_hdr, total));
    /* a fresh buffer: the window's first pair (or replay step) starts it; later pairs of a live tick restart it
     * themselves (rt_cap_new: a new frame counter value) */
    memcpy(h + offsetof(sa1_cap_hdr, count), &zero, 4);
    memcpy(h + offsetof(sa1_cap_hdr, dropped), &zero, 4);
    memcpy(h + offsetof(sa1_cap_hdr, ninja), &zero, 4);
    memcpy(h + offsetof(sa1_cap_hdr, pay_used), &zero, 4);
    memcpy(h + offsetof(sa1_cap_hdr, on), &one, 4);
}

/* Round 0.9.4a (sadc docs/research/super2_plan.md 5.4 and 6.2 task 2 with its Review R.7; super2_lsd.md 4.3 / 4.4): the
 * golden Light Speed charge (SA1_SUPOPT_LSD_GOLD). The world says once per pair whether that pair was drawn with the General
 * Super look on and the option set (sa1w_world_lsd_gold, src/world/sa1_world_super.c); nothing else of the world is read.
 * It is recorded only where the snapshot is replaced (the empty-pair branch and the filled one of sa1_capture_close), so
 * the value always belongs to the records it flags: a window that ran no pair and drew nothing keeps the snapshot and its
 * value. The emit then marks the draws (cap_gold_texlist, cap_gold_glow). Built before the world's half has landed (or
 * without worlds): 0, the game's colours. */
static int32_t cap_super_gold(const sa1_capture *c)
{
#if !defined(SA1_NO_WORLD) && defined(SA1W_HAS_LSD_GOLD)
    return sa1w_world_lsd_gold(c->w) ? 1 : 0;
#else
    (void)c;
    return 0;
#endif
}

void sa1_capture_close(sa1_capture *c)
{
    uint8_t *h;
    uint32_t zero = 0, frame_end, spf, n;
    if (!c)
        return;
    h = cap_write_ptr(c, c->block, (uint32_t)sizeof(sa1_cap_hdr));
    if (!h)
        return;
    memcpy(h + offsetof(sa1_cap_hdr, on), &zero, 4);
    memcpy(&c->hdr, h, sizeof c->hdr);
    frame_end = cap_rd32(c, CAP_ADDR_FRAME);
    spf = cap_rd32(c, CAP_ADDR_SPF);
    /* Live pairs advance the frame counter by B_8C754EC4 after their steps (8C08A5BE), so the last pair drew with
     * frame_end - spf; replay steps get the record's counter injected (no advance), so their draws carry
     * frame_end. Nothing drawn and no pair run: the previous snapshot stays (and its gold decision). */
    if (c->hdr.total == c->total_open && frame_end == c->frame_open)
        return;
    c->valid = 1;
    c->windows++;
    if (c->hdr.total == c->total_open || (c->hdr.frame != frame_end - spf && c->hdr.frame != frame_end)) {
        /* the last pair drew nothing (the buffer holds an older pair, or nothing) */
        c->nsnap = 0;
        c->npay = 0;
        c->snap_frame = (int32_t)(frame_end - spf);
        c->empty_windows++;
        c->super_gold = cap_super_gold(c);
        cap_vis_of_snapshot(c, frame_end);
        return;
    }
    n = c->hdr.count < c->hdr.capacity ? c->hdr.count : c->hdr.capacity;
    if (n) {
        const uint8_t *r = sa1_capture_read(c, c->recs, n * (uint32_t)sizeof(sa1_cap_rec));
        if (!r)
            n = 0;
        else
            memcpy(c->snap, r, (size_t)n * sizeof(sa1_cap_rec));
    }
    c->nsnap = n;
    c->npay = 0;
    if (n && c->hdr.pay_used && c->hdr.pay_used <= c->hdr.pay_cap) {
        const uint8_t *q = sa1_capture_read(c, c->hdr.pay, c->hdr.pay_used);
        if (q) {
            memcpy(c->pay, q, c->hdr.pay_used);
            c->npay = c->hdr.pay_used;
        }
    }
    c->snap_frame = (int32_t)c->hdr.frame;
    c->super_gold = cap_super_gold(c);
    cap_vis_of_snapshot(c, frame_end);
}

int sa1_capture_valid(const sa1_capture *c)
{
    return c != NULL && c->valid;
}

int sa1_capture_hidden(const sa1_capture *c)
{
    return c != NULL && c->valid && c->hidden;
}

uint32_t sa1_capture_records(const sa1_capture *c, const sa1_cap_rec **recs)
{
    if (!c) {
        if (recs)
            *recs = NULL;
        return 0;
    }
    if (recs)
        *recs = c->snap;
    return c->nsnap;
}

const uint8_t *sa1_capture_payload(const sa1_capture *c, const sa1_cap_rec *r, uint32_t n)
{
    if (!c || !r || r->pay == SA1_CAP_NOPAY || r->pay_len < n || r->pay > c->npay || r->pay_len > c->npay - r->pay)
        return NULL;
    return c->pay + r->pay;
}

int sa1_capture_model_snapshot(const sa1_capture *c, const sa1_cap_rec *r, sa1_emit_info *ei)
{
    const uint8_t *p;
    uint32_t np, nmat, fl;
    uint64_t len;
    ei->snap = 0;
    ei->snap_mats = ei->snap_pts = ei->snap_nrm = NULL;
    ei->snap_np = ei->snap_nmat = 0;
    if (!r || !SA1_CAP_IS_MODEL(r->kind))
        return 0;
    p = sa1_capture_payload(c, r, 16u);
    if (!p)
        return 0;
    np = sa1_rd32(p);
    nmat = sa1_rd32(p + 4);
    fl = sa1_rd32(p + 8);
    len = 16u + 20u * (uint64_t)nmat + 12u * (uint64_t)np * ((fl & SA1_CAP_MSNAP_NORMALS) ? 2u : 1u);
    if (np > SA1_MAX_POINTS || nmat > SA1_MAX_MATERIALS || len != r->pay_len)
        return 0;
    ei->snap = 1;
    ei->snap_nmat = nmat;
    ei->snap_np = np;
    ei->snap_mats = nmat ? p + 16 : NULL;
    ei->snap_pts = np ? p + 16 + 20u * nmat : NULL;
    ei->snap_nrm = (np && (fl & SA1_CAP_MSNAP_NORMALS)) ? p + 16 + 20u * nmat + 12u * np : NULL;
    return 1;
}

void sa1_capture_get_stats(const sa1_capture *c, sa1_capture_stats *out)
{
    uint32_t i;
    memset(out, 0, sizeof *out);
    if (!c)
        return;
    out->version = c->hdr.version;
    out->records = c->nsnap;
    for (i = 0; i < c->nsnap; i++)
        out->model_records += SA1_CAP_IS_MODEL(c->snap[i].kind) ? 1u : 0u;
    out->dropped = c->nsnap ? c->hdr.dropped : 0u;
    out->total = c->hdr.total;
    out->vis_calls = c->hdr.vis_calls;
    out->vis_shown = c->vis_shown;
    out->vis_hidden = c->vis_hidden;
    out->hidden = (uint32_t)c->hidden;
    out->hidden_windows = c->hidden_windows;
    memcpy(out->calls, c->hdr.calls, sizeof out->calls);
    out->windows = c->windows;
    out->empty_windows = c->empty_windows;
    out->frame = c->snap_frame;
    out->block = c->block;
    out->emitted = c->emitted;
    out->excluded = c->excluded;
    out->clipped = c->clipped;
    out->foreign = c->foreign;
    out->bad_models = c->buf.bad_models;
    out->mirrored = c->buf.mirrored;
    out->duplicates = c->duplicates;
    out->effect_draws = c->effect_draws;
    out->unresolved = c->unresolved;
    out->sprites = c->sprites;
    out->lines = c->lines;
    out->polygons = c->polygons;
    out->payload_lost = c->payload_lost;
    out->pay_used = c->npay;
    out->zero_sprites = c->zero_sprites;
    out->outlines = c->outlines;
    out->poly_textured = c->poly_textured;
    out->poly_untextured = c->poly_untextured;
    out->shadows = c->shadows;
    out->others = c->others;
    out->object_models = c->object_models;
    out->object_sprites = c->object_sprites;
    out->super_gold = (uint32_t)c->super_gold;
    out->gold_sprites = c->gold_sprites;
    out->gold_polygons = c->gold_polygons;
    out->gold_models = c->gold_models;
    for (i = 0; i < c->nsnap; i++)
        out->object_records += sa1_capture_object_exec(c->snap[i].exec) ? 1u : 0u;
}

uint32_t sa1_capture_shadow_model(sa1_capture *c)
{
    return c ? cap_rd32(c, SA1_CAP_SHADOW_OBJECT + 4u) : 0u;
}

/* ---- game OBJECT draws (round 0.9.4; sa1_capture.h, version 5) -------------------------------------------------------
 * sadc docs/research/gsa1_094_plan.md 6.2 task 2 with its Review R.1 / R.2, gsa1_091b_visuals.md 4.2 R-F4 with its Review
 * R.3.7 / R.3.8. The execs whose draws are game objects, each with its evidence (sadc src/1st_read). The capture records
 * the running exec D_8C1C1D40, which only the task executor's level walker sets around each exec call
 * (code_8C09878C.c:34-38; the display walker leaves it 0); each of these execs calls its display itself, so every model,
 * blob shadow and sprite of the object carries it.
 *   8C0869E8 objexec_RING (code_8C0869E0.c): the SET ring. Mode 1 calls its display func_8C086D00 (the visibility test,
 *            the blob shadow through 8C092124, the ring model D_8C1A21DC through 8C03700C); mode 2 the sparkle
 *            func_8C086DE6 (three sprites B_8C79897C, texlist D_8C1BF014).
 *   8C087B80 objexec_O_GRING (code_8C087B80.c): the ring line / circle. Its children are objexec_RING tasks
 *            (CreateChildTask(2, objexec_RING, tp), :76), but the group runs them from its own exec through
 *            func_8C098E98 (:99), which calls ctp->exec without setting D_8C1C1D40 (code_8C098E98.c:5-16): every child's
 *            model, blob shadow and sparkle carries the group's exec (gsa1_094_plan.md Review R.2).
 *   8C0870EA func_8C0870EA (code_8C0870EA.c; display func_8C0877A0, code_8C0876F4.c:99-125: the blob through 8C036F12
 *            and the ring model D_8C1A21DC; the sparkle func_8C086DE6 in mode 4): the dropped ring. Every one is a
 *            CreateElementalTask(10, 2, func_8C0870EA): func_8C087A9E (code_8C0876F4.c:196-213; called by Knuckles' dig
 *            code_8C0C8534.c, the dig's ring fountain func_8C0CB1A0 of code_8C0CAE60.c - which draws nothing itself - and
 *            CMN_KUSA's drop in asm 8C066320.src), the hurt scatter func_8C086F5E (code_8C086F5E.c:70) and a SET ring
 *            whose ground went away (code_8C0869E0.c:165).
 *   8C05D10C objexec_O_ITEMBOX (code_8C05D0A0.c): the ground item box; its exec calls its displays func_8C05D56E (the
 *            base D_8C1AC944, the case D_8C1AC1A8, the lid D_8C1ABD30, the item panel D_8C1AB994) and func_8C05D67E (the
 *            shell). The break debris (func_8C05D6AC -> 8C09E3E8) is a particle: the particle manager 8C09DF64 draws it
 *            (8C09E778) after the task executor (sa1_world_tick.c, as 8C04E714 runs it), so its records carry exec 0:
 *            an effect, not an OBJECT draw.
 *   8C05D9F6 objexec_O_ItemBoxAir (code_8C05D9C0.c:95-160): the air item box; its exec calls its display func_8C05DC60
 *            (code_8C05DC60.c: Item_Box_Air 8C1B87BC with the panel D_8C1B80C4; D_8C1B7FC0 through 8C6215D6 while it
 *            breaks, a debug-kind record the emitter does not walk).
 * The ring model D_8C1A21DC is drawn only by 8C086D00 and 8C0877A0 (sadc src: the only two references). */
static const uint32_t cap_object_exec[] = { 0x8C0869E8u, 0x8C087B80u, 0x8C0870EAu, 0x8C05D10Cu, 0x8C05D9F6u };
static const char *const cap_object_name[] = { "objexec_RING", "objexec_O_GRING", "func_8C0870EA (dropped ring)",
                                               "objexec_O_ITEMBOX", "objexec_O_ItemBoxAir" };
#define CAP_OBJECT_EXECS (sizeof cap_object_exec / sizeof cap_object_exec[0])

int sa1_capture_object_exec(uint32_t exec)
{
    uint32_t i;
    if ((exec & 0x1FFFFFFFu) == 0)
        return 0;
    for (i = 0; i < CAP_OBJECT_EXECS; i++)
        if ((exec & 0x1FFFFFFFu) == (cap_object_exec[i] & 0x1FFFFFFFu))
            return 1;
    return 0;
}

uint32_t sa1_capture_object_execs(const uint32_t **execs, const char *const **names)
{
    if (execs)
        *execs = cap_object_exec;
    if (names)
        *names = cap_object_name;
    return (uint32_t)CAP_OBJECT_EXECS;
}

void sa1_meshbuf_free_exec(const sa1_context *ctx, sa1_meshbuf *b)
{
    if (!b)
        return;
    sa1_mem_free(ctx, b->node_exec);
    sa1_mem_free(ctx, b->spr_exec);
    b->node_exec = NULL;
    b->spr_exec = NULL;
    b->cap_exec_n = 0;
    b->cap_exec_spr = 0;
}

/* node_exec / spr_exec with room for nn nodes and ns sprites (the first `used` entries kept) */
static sa1_status exec_room(const sa1_context *ctx, sa1_meshbuf *b, uint32_t nn, uint32_t ns)
{
    if (nn > b->cap_exec_n) {
        uint32_t c = nn < 64u ? 64u : nn + nn / 2u;
        uint32_t *p = (uint32_t *)sa1_mem_array(ctx, c, sizeof(uint32_t));
        if (!p)
            return SA1_ERR_OUT_OF_MEMORY;
        if (b->node_exec && b->nn)
            memcpy(p, b->node_exec, (size_t)(b->nn < b->cap_exec_n ? b->nn : b->cap_exec_n) * sizeof(uint32_t));
        sa1_mem_free(ctx, b->node_exec);
        b->node_exec = p;
        b->cap_exec_n = c;
    }
    if (ns > b->cap_exec_spr) {
        uint32_t c = ns < 32u ? 32u : ns + ns / 2u;
        uint32_t *p = (uint32_t *)sa1_mem_array(ctx, c, sizeof(uint32_t));
        if (!p)
            return SA1_ERR_OUT_OF_MEMORY;
        if (b->spr_exec && b->nspr)
            memcpy(p, b->spr_exec, (size_t)(b->nspr < b->cap_exec_spr ? b->nspr : b->cap_exec_spr) * sizeof(uint32_t));
        sa1_mem_free(ctx, b->spr_exec);
        b->spr_exec = p;
        b->cap_exec_spr = c;
    }
    return SA1_OK;
}

sa1_status sa1_meshbuf_reserve_exec(const sa1_context *ctx, sa1_meshbuf *b)
{
    if (!ctx || !b)
        return SA1_ERR_INVALID_ARGUMENT;
    return exec_room(ctx, b, b->cap_n, b->cap_spr);
}

/* inverse of a Ninja matrix (row-vector convention, affine: rows 0-2 linear, row 3 translation); 0 if singular */
static int ninja_inverse(const float b[16], double inv[16])
{
    double a00 = b[0], a01 = b[1], a02 = b[2], a10 = b[4], a11 = b[5], a12 = b[6], a20 = b[8], a21 = b[9], a22 = b[10];
    double c00 = a11 * a22 - a12 * a21, c01 = a02 * a21 - a01 * a22, c02 = a01 * a12 - a02 * a11;
    double c10 = a12 * a20 - a10 * a22, c11 = a00 * a22 - a02 * a20, c12 = a02 * a10 - a00 * a12;
    double c20 = a10 * a21 - a11 * a20, c21 = a01 * a20 - a00 * a21, c22 = a00 * a11 - a01 * a10;
    double det = a00 * c00 + a01 * c10 + a02 * c20, k;
    int i;
    if (!(fabs(det) > 1e-30) || det != det)
        return 0;
    k = 1.0 / det;
    inv[0] = c00 * k; inv[1] = c01 * k; inv[2] = c02 * k; inv[3] = 0.0;
    inv[4] = c10 * k; inv[5] = c11 * k; inv[6] = c12 * k; inv[7] = 0.0;
    inv[8] = c20 * k; inv[9] = c21 * k; inv[10] = c22 * k; inv[11] = 0.0;
    for (i = 0; i < 3; i++)
        inv[12 + i] = -((double)b[12] * inv[i] + (double)b[13] * inv[4 + i] + (double)b[14] * inv[8 + i]);
    inv[15] = 1.0;
    return 1;
}

void sa1_capture_draw_state(const sa1_cap_rec *r, sa1_draw_state *ds)
{
    memset(ds, 0, sizeof *ds);
    ds->attr_masks = (r->ctl3d & 0x4u) != 0;
    ds->attr_and = r->attr_and;
    ds->attr_or = r->attr_or;
    /* 8C037294's order: offset material (0x20) first, a constant material (0x10) wins; 8C6209A8 the same (0x10
       replaces and skips the 0x20 add, 8C620B3A-8C620B5A) */
    ds->material = (r->ctl3d & 0x10u) ? 1 : (r->ctl3d & 0x20u) ? -1 : 0;
    /* the hand-written renderer's records, and 8C60E080's node draws (walked with it here; their original njDrawModel
       8C6108B0 has an UNVERIFIED textured RGB source: today's rules kept). 8C60E49A's (SA1_CAPF_NJ_ACTION) are
       Ninja's 8C6209A8: all four channels, not palette-lit, a textured material white under control-3D 0x200 */
    if (r->kind == SA1_CAP_NJMOTION && (r->flags & SA1_CAPF_NJ_ACTION)) {
        ds->hand = 0;
        ds->tex_white = (r->ctl3d & 0x200u) != 0;
    } else {
        ds->hand = r->kind == SA1_CAP_RENDER || r->kind == SA1_CAP_RENDER2 || r->kind == SA1_CAP_NJMOTION;
    }
    memcpy(ds->cmat, r->cmat, sizeof ds->cmat);
    if (!(ds->cmat[0] == ds->cmat[0]) || !(ds->cmat[1] == ds->cmat[1]) || !(ds->cmat[2] == ds->cmat[2]) ||
        !(ds->cmat[3] == ds->cmat[3]))
        ds->material = 0;
}

int sa1_capture_world_matrix(const sa1_cap_rec *r, double m[12])
{
    double inv[16], w[16];
    int i, j, ok = ninja_inverse(r->base, inv);
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            if (ok) {
                w[4 * i + j] = (double)r->m[4 * i] * inv[j] + (double)r->m[4 * i + 1] * inv[4 + j] +
                               (double)r->m[4 * i + 2] * inv[8 + j] + (double)r->m[4 * i + 3] * inv[12 + j];
            } else {
                w[4 * i + j] = r->m[4 * i + j];
            }
        }
    }
    /* p_world = p * W (row vector) -> column form: m[i][j] = W[j][i], translation m[i][3] = W[3][i] */
    for (i = 0; i < 3; i++) {
        m[4 * i + 0] = w[0 + i];
        m[4 * i + 1] = w[4 + i];
        m[4 * i + 2] = w[8 + i];
        m[4 * i + 3] = w[12 + i];
    }
    return ok;
}

/* ---- effects: sprites, line segments, polygons (v2 payload) --------------------------------------------------- */
static void xform(const double m[12], const float in[3], float out[3])
{
    double x = in[0], y = in[1], z = in[2];
    out[0] = (float)(m[0] * x + m[1] * y + m[2] * z + m[3]);
    out[1] = (float)(m[4] * x + m[5] * y + m[6] * z + m[7]);
    out[2] = (float)(m[8] * x + m[9] * y + m[10] * z + m[11]);
}

static uint8_t unit_byte(float f)
{
    double x = (double)f * 255.0;
    if (!(x > 0.0))
        return 0;
    return x >= 255.0 ? 255 : (uint8_t)(x + 0.5);
}

static void argb_bytes(uint32_t col, uint8_t out[4])
{
    out[0] = (uint8_t)(col >> 16);
    out[1] = (uint8_t)(col >> 8);
    out[2] = (uint8_t)col;
    out[3] = (uint8_t)(col >> 24);
}

static uint64_t sig_mix(uint64_t h, uint32_t v)
{
    uint32_t k;
    for (k = 0; k < 4; k++) {
        h ^= (v >> (8u * k)) & 0xFFu;
        h *= 1099511628211ull;
    }
    return h;
}

/* blend state of a point-list / sprite draw from njColorBlendingMode's modes at the draw */
static uint32_t ninja_blend(const sa1_cap_rec *r, int translucent, uint8_t *bs, uint8_t *bd)
{
    uint32_t f = 0;
    if (!translucent) {
        *bs = (uint8_t)SA1_BLEND_ONE;
        *bd = (uint8_t)SA1_BLEND_ZERO;
        return 0;
    }
    *bs = sa1_blend_of_ninja(r->blend & 0xFFu, 0);
    *bd = sa1_blend_of_ninja((r->blend >> 8) & 0xFFu, 1);
    f |= SA1_DRAW_BLEND;
    if (*bd == SA1_BLEND_ONE)
        f |= SA1_DRAW_ADDITIVE;
    return f;
}

/* ---- the golden Light Speed charge (round 0.9.4a; sadc docs/research/super2_plan.md 6.2 tasks 3 / 4 with its 10.1 O3 /
 * O4, super2_lsd.md 2.2, 4.3, 4.4) ---------------------------------------------------------------------------------------
 * While the snapshot's c->super_gold (cap_super_gold: the General look on and SA1_SUPOPT_LSD_GOLD 1 in its pair):
 *   M2  a sprite or polygon draw whose record's texlist is one of the PLAIN charge's one-texture texlists and that is drawn
 *       with its texture gets SA1_DRAW_SUPER_GOLD: 8C1C6AA4 = {lcharge02}, the sparkles (the sprite D_8C1C6AD0[0]'s tlist,
 *       8C0B58E0's njDrawSprite3D, code_8C0B58E0.c:38), and 8C1C6A94 = {lcharge01}, the streaks (D_8C1C6A94[0]: 8C0B504A's
 *       njSetTexture + njDrawTriangle3D, code_8C0B4F00.c:131-137). The host draws the texture's
 *       SA1_TEXTURE_VARIANT_SUPER_GOLD (sa1_texture.c) in place of it; archive and texture stay the game's. Both are
 *       indexed by btimer = equipment & 1: with the Crystal Ring the game draws 8C1C6AAC = {lcharge04} and 8C1C6A9C =
 *       {lcharge03}, its green ones, which keep the game's colours (owner answer O3, super2_plan.md 10.1). The four
 *       texlists are 1ST_READ data (asm/1st_read/data/8C1C1194.src: their texnames are SON_EFF's 25-28) that no other draw
 *       uses. The flag is set before emit_sprite's merge test, so a gold draw never merges with one without it (polygon
 *       draws never merge).
 *   M3  the charged glow: Ninja's action draw 8C60E49A (SA1_CAPF_NJ_ACTION) of the S_MOT aura object 0x0CC02AD4, three
 *       passes by 8C0B4F00 (code_8C0B4F00.c:95-110), whose 20 node models' mats pointer is 0x0CC00000 (the S_MOT image's
 *       first material, attr 0x26182400: lightspeed_visuals.md 3.2). 8C60E49A's only other caller, 8C105036, is not
 *       Sonic's code; Sonic's snowboard draws with 8C60E080 (not NJ_ACTION, S_SBMOT's material: lightspeed_visuals.md
 *       Review R.2.2). After the emitter wrote the vertex colours from the game's constant material (unchanged:
 *       ds_channel's clamping included), each vertex RGB becomes max(r, g, b) + min(r, g, b) - c per channel (T2: the
 *       complementary hue, value and saturation kept, so the game's pulse is kept: blue (0, 61, 255) -> gold (255, 194,
 *       0)); alpha kept. The draw also gets SA1_DRAW_SUPER_GOLD (information: its colours are gold already) through the
 *       emitter's extra flags, before its merge test (add_tri_draw, sa1_pose.c). The game's glow is the same blue with the
 *       Crystal Ring: gold either way (owner answer O4).
 * Presentation only: the emit reads the snapshot and writes the host's buffers, nothing of the world (gold_test (3)). */
#define CAP_GOLD_STREAK   0x0C1C6A94u   /* D_8C1C6A94[0] = {lcharge01} (compared & 0x1FFFFFFF) */
#define CAP_GOLD_SPARKLE  0x0C1C6AA4u   /* D_8C1C6AA4 = {lcharge02} */
#define CAP_GLOW_MATS     0x0CC00000u   /* the S_MOT aura model's materials (S_MOT's first bytes) */

static int cap_gold_texlist(const sa1_capture *c, uint32_t texlist)
{
    uint32_t t = texlist & 0x1FFFFFFFu;
    return c->super_gold && (t == CAP_GOLD_STREAK || t == CAP_GOLD_SPARKLE);
}

static int cap_gold_glow(const sa1_capture *c, const sa1_cap_rec *r)
{
    const uint8_t *mp;
    if (!c->super_gold || r->kind != SA1_CAP_NJMOTION || !(r->flags & SA1_CAPF_NJ_ACTION))
        return 0;
    mp = sa1_capture_read((void *)(uintptr_t)c, r->obj, 0x28u);
    return mp != NULL && (sa1_rd32(mp + 16) & 0x1FFFFFFFu) == CAP_GLOW_MATS;
}

/* T2 on the corner colours of triangles [t0, t1) of b */
static void cap_gold_complement(sa1_meshbuf *b, uint32_t t0, uint32_t t1)
{
    uint32_t t, k;
    for (t = t0; t < t1; t++) {
        for (k = 0; k < 3; k++) {
            uint8_t *p = b->col + 12u * t + 4u * k;
            uint32_t mx = p[0], mn = p[0];
            if (p[1] > mx) mx = p[1];
            if (p[2] > mx) mx = p[2];
            if (p[1] < mn) mn = p[1];
            if (p[2] < mn) mn = p[2];
            p[0] = (uint8_t)(mx + mn - p[0]);
            p[1] = (uint8_t)(mx + mn - p[1]);
            p[2] = (uint8_t)(mx + mn - p[2]);
        }
    }
}

/* A 3D sprite (njDrawSprite3D 8C63EF74, the game's 8C09E778; lightspeed_visuals.md 2.2, F1). The quad's size is the
 * game's: tanim px x sp->sx by tanim px x sp->sy (payload +0x0C / +0x10), in SA1 units, and
 *   - scaled (NJ_SPRITE_SCALE, 8C63F038 / 8C09E880): only sp->p goes through the matrix, the offsets are added on the
 *     screen: the matrix's scale does not apply; the game's own draw 8C09E880 loads sp->sx into both scale registers
 *     (8C09E904-8C09E90C), so its records are sx by sx;
 *   - unscaled (8C63F2D4 / 8C09EB08): the corners p + (x, y, 0) lie in the matrix's local plane and each goes through
 *     XMTRX (8C63F3F2-8C63F42C): x the lengths of the model -> SA1 world matrix's x and y axes (the view in slot 0 is
 *     rigid). The quad then also turns and mirrors with the matrix: its local x / y axes in view space (XMTRX rows 0
 *     and 1; x right, y down) give the in-plane turn (added to the sprite's own angle) and, when they mirror, a flipped
 *     rect and v (a camera-facing billboard whose local y is the world's up is mirrored: the SDK draws texture row v0
 *     at local -y, the bottom of the screen; UNVERIFIED on a Dreamcast screen). Limits: a quad the matrix tilts out
 *     of the view plane stays camera-facing (host billboard), and an NJ_SPRITE_ANGLE sprite under a non-uniform matrix
 *     is a parallelogram that rect + angle cannot express (drawn as the rect turned by the angle).
 * A quad of zero extent (njScale(0) on each charge sparkle's last draw) draws nothing: left out (zero_sprites).
 * UVs are the tanim's s16 x 1/256 (0x3B800000 at 8C63F058 / 8C63F3B8). */
static sa1_status emit_sprite(sa1_context *ctx, sa1_capture *c, sa1_meshbuf *b, const sa1_cap_rec *r, const double m[12])
{
    const uint8_t *p = sa1_capture_payload(c, r, 0x34u);
    sa1_sprite *sp;
    sa1_draw *d;
    float pos[3], w[3], sx, sy, k = ctx->coords.scale, tsx, tsy, cx, cy, u0, v0, u1, v1, t, cm[4], x0, y0, x1, y1;
    double ang = 0.0, turn = 0.0;
    uint32_t attr = r->attr, flags = SA1_DRAW_EFFECT | SA1_DRAW_UNLIT | SA1_DRAW_DOUBLE_SIDED, ta = SA1_TEXALPHA_NONE;
    int32_t arc = -1, tex;
    int mirror = 0;
    uint8_t bs, bd;
    sa1_status st;
    if (!p) {
        c->payload_lost++;
        return SA1_OK;
    }
    pos[0] = sa1_rdf32(p);
    pos[1] = sa1_rdf32(p + 4);
    pos[2] = sa1_rdf32(p + 8);
    sx = sa1_rdf32(p + 0x0C);
    sy = sa1_rdf32(p + 0x10);
    if (attr & NJ_SPRITE_SCALE) {
        if (r->flags & SA1_CAPF_GAME_SPRITE)
            sy = sx;
    } else {
        double xx = r->m[0], xy = r->m[1], yx = r->m[4], yy = r->m[5];
        sx *= (float)sqrt(m[0] * m[0] + m[4] * m[4] + m[8] * m[8]);
        sy *= (float)sqrt(m[1] * m[1] + m[5] * m[5] + m[9] * m[9]);
        if (xx * xx + xy * xy > 0.0)
            turn = atan2(xy, xx);
        mirror = xx * yy - xy * yx < 0.0;
        if (!(turn == turn))
            turn = 0.0;
    }
    tsx = (float)(int16_t)sa1_rd16(p + 0x20);
    tsy = (float)(int16_t)sa1_rd16(p + 0x22);
    cx = (float)(int16_t)sa1_rd16(p + 0x24);
    cy = (float)(int16_t)sa1_rd16(p + 0x26);
    x0 = -cx * sx;
    x1 = (tsx - cx) * sx;
    y0 = -cy * sy;
    y1 = (tsy - cy) * sy;
    if (!(fabsf(x1 - x0) > 0.0f) || !(fabsf(y1 - y0) > 0.0f)) {
        c->zero_sprites++;
        return SA1_OK;
    }
    u0 = (float)(int16_t)sa1_rd16(p + 0x28) / 256.0f;
    v0 = (float)(int16_t)sa1_rd16(p + 0x2A) / 256.0f;
    u1 = (float)(int16_t)sa1_rd16(p + 0x2C) / 256.0f;
    v1 = (float)(int16_t)sa1_rd16(p + 0x2E) / 256.0f;
    if (attr & NJ_SPRITE_HFLIP) {
        t = u0; u0 = u1; u1 = t;
    }
    if (attr & NJ_SPRITE_VFLIP) {
        t = v0; v0 = v1; v1 = t;
    }
    if (attr & NJ_SPRITE_ANGLE)
        ang = (double)(int32_t)sa1_rd32(p + 0x14) * (6.283185307179586 / 65536.0);
    if (mirror) {
        /* screen = R(turn) * diag(1, -1) * R(ang) * local = R(turn - ang) * diag(1, -1) * local */
        t = y0; y0 = -y1; y1 = -t;
        t = v0; v0 = v1; v1 = t;
        ang = turn - ang;
    } else {
        ang = turn + ang;
    }
    tex = sa1_capture_resolve(c, r->texlist, (uint32_t)(uint16_t)sa1_rd16(p + 0x30), &arc);
    if (tex < 0) {
        arc = -1;
        flags |= SA1_DRAW_NO_TEXTURE;
        c->unresolved++;
    } else {
        ta = cap_tex_alpha(c, arc, tex);
    }
    flags |= ninja_blend(r, (attr & NJ_SPRITE_ALPHA) != 0, &bs, &bd);
    if (!(flags & SA1_DRAW_BLEND) && ta == SA1_TEXALPHA_BINARY && (attr & NJ_SPRITE_ALPHA))
        flags |= SA1_DRAW_ALPHA_TEST;
    if (sa1_capture_object_exec(r->exec))
        flags |= SA1_DRAW_OBJECT;   /* round 0.9.4: a game object's sprite (a ring's sparkle), part of the merge key below */
    if (tex >= 0 && cap_gold_texlist(c, r->texlist)) {
        flags |= SA1_DRAW_SUPER_GOLD;   /* round 0.9.4a M2: a plain charge sparkle, part of the merge key below */
        c->gold_sprites++;
    }
    st = sa1_meshbuf_reserve_extra(ctx, b, 1u, 1u, 0u);
    if (st != SA1_OK)
        return st;
    sp = &b->spr[b->nspr];
    xform(m, pos, w);
    sa1_to_host_point(ctx, w, sp->position);
    sp->rect[0] = x0 * k;
    sp->rect[1] = y0 * k;
    sp->rect[2] = x1 * k;
    sp->rect[3] = y1 * k;
    sp->angle = (float)ang;
    sp->uv[0] = u0;
    sp->uv[1] = v0;
    sp->uv[2] = u1;
    sp->uv[3] = v1;
    memcpy(cm, r->cmat, sizeof cm);
    if ((attr & NJ_SPRITE_COLOR) && cm[0] == cm[0] && cm[1] == cm[1] && cm[2] == cm[2] && cm[3] == cm[3]) {
        sp->color[0] = unit_byte(cm[1]);
        sp->color[1] = unit_byte(cm[2]);
        sp->color[2] = unit_byte(cm[3]);
        sp->color[3] = unit_byte(cm[0]);
    } else {
        sp->color[0] = sp->color[1] = sp->color[2] = sp->color[3] = 255;
    }
    d = b->ndraw ? &b->draws[b->ndraw - 1u] : NULL;
    if (d && d->prim == SA1_PRIM_SPRITES && d->first + d->count == b->nspr && d->archive == arc && d->texture == tex &&
        d->flags == flags && d->blend_src == bs && d->blend_dst == bd) {
        d->count++;
    } else {
        d = sa1_meshbuf_add_draw(ctx, b);
        if (!d)
            return SA1_ERR_OUT_OF_MEMORY;
        d->prim = SA1_PRIM_SPRITES;
        d->first = b->nspr;
        d->count = 1;
        d->archive = arc;
        d->texture = tex;
        d->flags = flags;
        d->blend_src = bs;
        d->blend_dst = bd;
        d->source = (uint8_t)SA1_SOURCE_SPRITE;
        d->diffuse[0] = d->diffuse[1] = d->diffuse[2] = d->diffuse[3] = 255;
    }
    b->nspr++;
    b->sig = sig_mix(b->sig, 0x5000u);
    c->sprites++;
    return SA1_OK;
}

/* the points (host space), colours and (uv != NULL) UVs of a point-list payload (rt_draw.c rt_cap_points: u32 points,
 * u32 has colours, u32 has UVs, u32 p->num, the points, the NJS_COLORs, the NJS_TEXs); returns the point count.
 * *has_uv: the payload has UVs (s16 u, v x 1/256: 8C64FC68 at 8C64FCB6-8C64FCD0, L8C64FDF4 = 0x3B800000) */
static uint32_t point_list(sa1_context *ctx, const sa1_capture *c, const sa1_cap_rec *r, const double m[12],
                           float *pos, uint8_t *col, float *uv, int *has_uv, uint32_t cap)
{
    const uint8_t *p = sa1_capture_payload(c, r, 16u), *q, *tq;
    uint32_t num, hasc, hast, i;
    if (has_uv)
        *has_uv = 0;
    if (!p)
        return 0;
    num = sa1_rd32(p);
    hasc = sa1_rd32(p + 4);
    hast = sa1_rd32(p + 8);
    if (num > cap || 16u + (uint64_t)num * (12u + 4u * (hasc != 0) + 4u * (hast != 0)) > r->pay_len)
        return 0;
    q = p + 16;
    tq = q + 12u * num + (hasc ? 4u * num : 0u);
    for (i = 0; i < num; i++) {
        float in[3], w[3];
        in[0] = sa1_rdf32(q + 12u * i);
        in[1] = sa1_rdf32(q + 12u * i + 4u);
        in[2] = sa1_rdf32(q + 12u * i + 8u);
        xform(m, in, w);
        sa1_to_host_point(ctx, w, pos + 3u * i);
        if (hasc)
            argb_bytes(sa1_rd32(q + 12u * num + 4u * i), col + 4u * i);
        else
            col[4u * i] = col[4u * i + 1u] = col[4u * i + 2u] = col[4u * i + 3u] = 255;
        if (uv) {
            uv[2u * i] = hast ? (float)(int16_t)sa1_rd16(tq + 4u * i) / 256.0f : 0.0f;
            uv[2u * i + 1u] = hast ? (float)(int16_t)sa1_rd16(tq + 4u * i + 2u) / 256.0f : 0.0f;
        }
    }
    if (has_uv)
        *has_uv = hast != 0;
    return num;
}

#define CAP_MAXPTS 512u

/* line segments (point indices in seg, 2 per segment) of one draw as SA1_PRIM_LINES, merged into the previous draw when
 * it is a line draw of the same source and state ending where these start */
static sa1_status add_segments(sa1_context *ctx, sa1_meshbuf *b, const float *pos, const uint8_t *col,
                               const uint16_t *seg, uint32_t nseg, uint32_t flags, uint8_t bs, uint8_t bd, uint8_t source)
{
    sa1_draw *d;
    uint32_t s, k;
    sa1_status st = sa1_meshbuf_reserve_extra(ctx, b, 1u, 0u, 2u * nseg);
    if (st != SA1_OK)
        return st;
    d = b->ndraw ? &b->draws[b->ndraw - 1u] : NULL;
    if (d && d->prim == SA1_PRIM_LINES && d->first + d->count == b->nlv / 2u && d->flags == flags &&
        d->blend_src == bs && d->blend_dst == bd && d->source == source) {
        d->count += nseg;   /* consecutive line draws with the same state: one draw */
    } else {
        d = sa1_meshbuf_add_draw(ctx, b);
        if (!d)
            return SA1_ERR_OUT_OF_MEMORY;
        d->prim = SA1_PRIM_LINES;
        d->first = b->nlv / 2u;
        d->count = nseg;
        d->archive = -1;
        d->texture = -1;
        d->flags = flags;
        d->blend_src = bs;
        d->blend_dst = bd;
        d->source = source;
        d->diffuse[0] = d->diffuse[1] = d->diffuse[2] = d->diffuse[3] = 255;
    }
    for (s = 0; s < nseg; s++) {
        sa1_line_vertex *v = &b->lv[b->nlv];
        for (k = 0; k < 2; k++) {
            uint32_t i = seg[2u * s + k];
            memcpy(v[k].position, pos + 3u * i, 3 * sizeof(float));
            memcpy(v[k].color, col + 4u * i, 4);
        }
        b->nlv += 2u;
    }
    return SA1_OK;
}

/* njDrawLine3D 8C63CE24 (lightspeed_visuals.md F5, Review R.1): n lines; stride = attr & 0x0F ? 1 : 2 (8C63CEF2-8C63CEFC,
 * 8C63CF4C), line i from point (attr & 1 ? 0 : i * stride) to point i * stride + 1 (8C63CF82-8C63CF96): 0x40 pairs,
 * 0x42 a polyline (Big's fishing line: 12 points, n 11), 0x41 a fan from point 0 */
static sa1_status emit_lines(sa1_context *ctx, sa1_capture *c, sa1_meshbuf *b, const sa1_cap_rec *r, const double m[12])
{
    float pos[3 * CAP_MAXPTS];
    uint8_t col[4 * CAP_MAXPTS];
    uint16_t seg[2 * CAP_MAXPTS];
    uint32_t num = point_list(ctx, c, r, m, pos, col, NULL, NULL, CAP_MAXPTS), nseg, s, flags, stride;
    uint8_t bs, bd;
    sa1_status st;
    if (num < 2) {
        if (!sa1_capture_payload(c, r, 16u))
            c->payload_lost++;
        return SA1_OK;
    }
    stride = (r->attr & 0x0Fu) ? 1u : 2u;
    nseg = (num - 2u) / stride + 1u;    /* the lines whose second point i * stride + 1 exists */
    if (r->n < nseg)
        nseg = r->n;
    if (nseg == 0)
        return SA1_OK;
    for (s = 0; s < nseg; s++) {
        seg[2u * s] = (uint16_t)((r->attr & NJ_DRAW_FAN) ? 0u : s * stride);
        seg[2u * s + 1u] = (uint16_t)(s * stride + 1u);
    }
    flags = SA1_DRAW_EFFECT | SA1_DRAW_UNLIT | ninja_blend(r, (r->attr & NJ_DRAW_TRANS) != 0, &bs, &bd) |
            (sa1_capture_object_exec(r->exec) ? SA1_DRAW_OBJECT : 0u);
    st = add_segments(ctx, b, pos, col, seg, nseg, flags, bs, bd, (uint8_t)SA1_SOURCE_LINE);
    if (st != SA1_OK)
        return st;
    b->sig = sig_mix(sig_mix(b->sig, 0x1100u), nseg);
    c->lines++;
    return SA1_OK;
}

/* The 3D polygon draws (lightspeed_visuals.md F4, Review R.2.3 / R.2.5 / R.4):
 *   SA1_CAP_TRI3D  = 8C63D414 njDrawPolygon3D: n points, one convex polygon. Filled (attr 0x20) the SDK submits it as
 *                    one strip in the order 0, n-1, 1, n-2, 2, ... (8C64FB14-8C64FC3E: a quad's diagonal is 1-3);
 *   SA1_CAP_POLY3D = 8C63D66C njDrawTriangle3D: n triangles, stride = attr & 2 ? 1 : attr & 1 ? 2 : 3, triangle i =
 *                    (attr & 1 ? 0 : i * stride, 1 + i * stride, 2 + i * stride) (8C63D716-8C63D7AE, 8C63D922-8C63D93A):
 *                    0x2 a strip, 0x3 a fan, 0x1 a fan of every other point (0, 1 + 2i, 2 + 2i), 0 separate.
 * Triangles are emitted with the polygon's / strip's own orientation (every other strip triangle turned back); the
 * draws are double-sided. Without fill the SDK draws the edges as screen-space line quads (8C64FE0C / 8C650144: the
 * closing edge (0, n-1) first, then (i-1, i); a triangle's three edges): emitted as line segments (outlines, untextured).
 * Textures (phase 1, host only): njSetTextureNum's number is not recorded, so a textured draw (attr 0x80000000) whose
 * texlist has exactly one texture (every one observed: lcharge01 / lcharge03 of the charge ribbons, the barrier's
 * D_8C19B630; Review R.1) uses texture 0 with the payload's UVs; any other stays untextured (SA1_DRAW_NO_TEXTURE,
 * counted: phase 2 is the rt texture-number hook and record v5). */
static sa1_status emit_polygon(sa1_context *ctx, sa1_capture *c, sa1_meshbuf *b, const sa1_cap_rec *r,
                               const double m[12])
{
    float pos[3 * CAP_MAXPTS], uv[2 * CAP_MAXPTS];
    uint8_t col[4 * CAP_MAXPTS];
    uint16_t tri[3 * CAP_MAXPTS];
    uint32_t num, ntri = 0, nused = 0, t, i, nv0, nt0, flags;
    uint32_t ta = SA1_TEXALPHA_NONE;
    int has_uv = 0, fill = (r->attr & NJ_DRAW_FILL) != 0, textured = 0;
    int32_t arc = -1, tex = -1;
    uint8_t bs, bd;
    sa1_draw *d;
    sa1_status st;
    num = point_list(ctx, c, r, m, pos, col, uv, &has_uv, CAP_MAXPTS);
    if (num < 3) {
        if (!sa1_capture_payload(c, r, 16u))
            c->payload_lost++;
        return SA1_OK;
    }
    if (r->kind == SA1_CAP_TRI3D) {
        uint32_t np = r->n < num ? r->n : num, s[CAP_MAXPTS], lo = 0, hi;
        if (np < 3)
            return SA1_OK;
        hi = np - 1u;
        for (i = 0; i < np; i++)            /* 0, n-1, 1, n-2, ... */
            s[i] = (i & 1u) ? hi-- : lo++;
        ntri = np - 2u;
        for (t = 0; t < ntri; t++) {
            /* the polygon's orientation (points in order): (s[t], s[t+2], s[t+1]) for even t, as listed for odd t */
            tri[3u * t] = (uint16_t)s[t];
            tri[3u * t + 1u] = (uint16_t)s[(t & 1u) ? t + 1u : t + 2u];
            tri[3u * t + 2u] = (uint16_t)s[(t & 1u) ? t + 2u : t + 1u];
        }
        nused = np;
    } else {
        uint32_t stride = (r->attr & NJ_DRAW_CONNECTED) ? 1u : (r->attr & NJ_DRAW_FAN) ? 2u : 3u;
        for (t = 0; t < r->n && t * stride + 2u < num; t++) {
            uint32_t v0 = (r->attr & NJ_DRAW_FAN) ? 0u : t * stride, v1 = 1u + t * stride, v2 = v1 + 1u;
            int back = (r->attr & 3u) == NJ_DRAW_CONNECTED && (t & 1u);     /* a strip's odd triangles */
            tri[3u * t] = (uint16_t)v0;
            tri[3u * t + 1u] = (uint16_t)(back ? v2 : v1);
            tri[3u * t + 2u] = (uint16_t)(back ? v1 : v2);
            if (v2 + 1u > nused)
                nused = v2 + 1u;
        }
        ntri = t;
    }
    if (ntri == 0)
        return SA1_OK;
    if (!fill) {
        /* the outline: line segments in the SDK's edge order */
        uint16_t seg[2 * 3 * CAP_MAXPTS];
        uint32_t nseg = 0;
        if (r->kind == SA1_CAP_TRI3D) {
            seg[0] = 0;
            seg[1] = (uint16_t)(nused - 1u);
            for (i = 1, nseg = 1; i < nused; i++, nseg++) {
                seg[2u * nseg] = (uint16_t)(i - 1u);
                seg[2u * nseg + 1u] = (uint16_t)i;
            }
        } else {
            for (t = 0; t < ntri; t++, nseg += 3u) {
                uint16_t a = tri[3u * t], e1 = tri[3u * t + 1u], e2 = tri[3u * t + 2u];
                if ((r->attr & 3u) == NJ_DRAW_CONNECTED && (t & 1u)) {
                    uint16_t x = e1; e1 = e2; e2 = x;   /* the SDK's vertex order */
                }
                seg[2u * nseg] = a;      seg[2u * nseg + 1u] = e2;
                seg[2u * nseg + 2u] = a; seg[2u * nseg + 3u] = e1;
                seg[2u * nseg + 4u] = e1; seg[2u * nseg + 5u] = e2;
            }
        }
        flags = SA1_DRAW_EFFECT | SA1_DRAW_UNLIT | ninja_blend(r, (r->attr & NJ_DRAW_TRANS) != 0, &bs, &bd) |
                ((r->attr & NJ_DRAW_TEXTURE) ? SA1_DRAW_NO_TEXTURE : 0u) |
                (sa1_capture_object_exec(r->exec) ? SA1_DRAW_OBJECT : 0u);
        st = add_segments(ctx, b, pos, col, seg, nseg, flags, bs, bd, (uint8_t)SA1_SOURCE_POLYGON);
        if (st != SA1_OK)
            return st;
        b->sig = sig_mix(sig_mix(b->sig, 0x2300u), nseg);
        c->outlines++;
        c->polygons++;
        return SA1_OK;
    }
    if (r->attr & NJ_DRAW_TEXTURE) {
        if (has_uv && r->texlist && cap_rd32(c, r->texlist + 4u) == 1u)
            tex = sa1_capture_resolve(c, r->texlist, 0u, &arc);
        textured = tex >= 0;
        if (textured) {
            ta = cap_tex_alpha(c, arc, tex);
            c->poly_textured++;
        } else {
            arc = -1;
            tex = -1;
            c->poly_untextured++;
        }
    }
    st = sa1_meshbuf_reserve(ctx, b, b->nv + nused, b->nt + ntri, b->nn);
    if (st == SA1_OK)
        st = sa1_meshbuf_reserve_extra(ctx, b, 1u, 0u, 0u);
    if (st != SA1_OK)
        return st;
    nv0 = b->nv;
    nt0 = b->nt;
    for (i = 0; i < nused; i++)
        memcpy(b->pos + 3u * (nv0 + i), pos + 3u * i, 3 * sizeof(float));
    for (t = 0; t < ntri; t++) {
        uint32_t c3[3], k, o = b->nt;
        c3[0] = tri[3u * t];
        c3[1] = tri[3u * t + 1u];
        c3[2] = tri[3u * t + 2u];
        if (ctx->coords.flags & SA1_COORDS_FLIP_WINDING) {
            uint32_t tmp = c3[1]; c3[1] = c3[2]; c3[2] = tmp;
        }
        for (k = 0; k < 3; k++) {
            b->idx[3u * o + k] = nv0 + c3[k];
            b->uv[6u * o + 2u * k] = textured ? uv[2u * c3[k]] : 0.0f;
            b->uv[6u * o + 2u * k + 1u] = textured ? uv[2u * c3[k] + 1u] : 0.0f;
            memcpy(b->col + 12u * o + 4u * k, col + 4u * c3[k], 4);
        }
        b->tex[o] = (int16_t)(arc == 0 ? tex : -1);
        b->tarc[o] = (int16_t)arc;
        b->tidx[o] = (int16_t)tex;
        b->mat[o] = 0;
        b->fl[o] = (uint8_t)(SA1_TRI_HAS_VCOLOR | (textured ? SA1_TRI_HAS_UV : 0u));
        b->nt = o + 1u;
    }
    /* normals: the plane of the first three points, the same for every point */
    {
        const float *a = pos, *p1 = pos + 3u, *p2 = pos + 6u;
        double e1[3], e2[3], n[3], l;
        for (i = 0; i < 3; i++) {
            e1[i] = (double)p1[i] - a[i];
            e2[i] = (double)p2[i] - a[i];
        }
        n[0] = e1[1] * e2[2] - e1[2] * e2[1];
        n[1] = e1[2] * e2[0] - e1[0] * e2[2];
        n[2] = e1[0] * e2[1] - e1[1] * e2[0];
        l = sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        for (i = 0; i < nused; i++) {
            float *o = b->nrm + 3u * (nv0 + i);
            if (l > 1e-20) {
                o[0] = (float)(n[0] / l); o[1] = (float)(n[1] / l); o[2] = (float)(n[2] / l);
            } else {
                o[0] = 0.0f; o[1] = 0.0f; o[2] = 1.0f;
            }
        }
    }
    b->nv = nv0 + nused;
    flags = SA1_DRAW_EFFECT | SA1_DRAW_UNLIT | SA1_DRAW_DOUBLE_SIDED |
            ninja_blend(r, (r->attr & NJ_DRAW_TRANS) != 0, &bs, &bd) |
            (((r->attr & NJ_DRAW_TEXTURE) && !textured) ? SA1_DRAW_NO_TEXTURE : 0u) |
            (sa1_capture_object_exec(r->exec) ? SA1_DRAW_OBJECT : 0u);
    /* the texture's alpha classified as emit_sprite does */
    if (!(flags & SA1_DRAW_BLEND) && ta == SA1_TEXALPHA_BINARY && (r->attr & NJ_DRAW_TRANS))
        flags |= SA1_DRAW_ALPHA_TEST;
    if (textured && cap_gold_texlist(c, r->texlist)) {
        flags |= SA1_DRAW_SUPER_GOLD;   /* round 0.9.4a M2: a plain charge streak */
        c->gold_polygons++;
    }
    d = sa1_meshbuf_add_draw(ctx, b);
    if (!d)
        return SA1_ERR_OUT_OF_MEMORY;
    d->prim = SA1_PRIM_TRIANGLES;
    d->first = nt0;
    d->count = ntri;
    d->archive = arc;
    d->texture = tex;
    d->flags = flags;
    d->blend_src = bs;
    d->blend_dst = bd;
    d->source = (uint8_t)SA1_SOURCE_POLYGON;
    d->diffuse[0] = d->diffuse[1] = d->diffuse[2] = d->diffuse[3] = 255;
    b->sig = sig_mix(sig_mix(sig_mix(b->sig, 0x2200u), nused), ntri);
    c->polygons++;
    return SA1_OK;
}

/* a model draw that repeats an earlier one of the pair (same model, XMTRX and base, and the same v3 snapshot: the
 * runtime shares the bytes of equal snapshots) under a fading constant material, where that earlier draw is not fading
 * itself: blended over an opaque copy of itself on the Dreamcast it changes nothing (Tails' afterimage copies of the
 * unmoved body). A repeat of a draw that fades too is kept: the game blends both (Tails' curled body inside the ball on
 * ball pairs: coverage 7/9 with both translucent copies, 2/3 with one; sadc docs/research/gsa1_060_render.md 4.3.1).
 * Every earlier match is looked at, so the result does not depend on the order of the fading and the opaque one
 * (Review R3.6). */
static int duplicate_draw(const sa1_capture *c, uint32_t i, const sa1_draw_state *ds)
{
    const sa1_cap_rec *r = &c->snap[i];
    uint32_t k;
    if (!(ds->material < 0 && ds->cmat[0] < 0.0f))
        return 0;
    for (k = 0; k < i; k++) {
        const sa1_cap_rec *q = &c->snap[k];
        sa1_draw_state qs;
        if (!(SA1_CAP_IS_MODEL(q->kind) && q->obj == r->obj && q->pay == r->pay && memcmp(q->m, r->m, sizeof q->m) == 0 &&
              memcmp(q->base, r->base, sizeof q->base) == 0))
            continue;
        sa1_capture_draw_state(q, &qs);
        if (!(qs.material < 0 && qs.cmat[0] < 0.0f))
            return 1;
    }
    return 0;
}

static int clipped(const sa1_capture_emit_opts *o, const double m[12])
{
    double dx, dy, dz;
    if (!o->clip)
        return 0;
    dx = m[3] - o->clip_center[0];
    dy = m[7] - o->clip_center[1];
    dz = m[11] - o->clip_center[2];
    return !(dx * dx + dy * dy + dz * dz <= (double)o->clip_radius * o->clip_radius);
}

sa1_status sa1_capture_emit_into(sa1_context *ctx, sa1_capture *c, const sa1_capture_emit_opts *o, sa1_meshbuf *b,
                                 sa1_geometry *g)
{
    uint32_t i;
    sa1_status st;
    if (!ctx || !c || !o || !b || !g)
        return SA1_ERR_INVALID_ARGUMENT;
    sa1_meshbuf_reset(b);
    c->emitted = c->excluded = c->clipped = c->foreign = 0;
    c->duplicates = c->effect_draws = c->unresolved = c->sprites = c->lines = c->polygons = c->payload_lost = 0;
    c->zero_sprites = c->outlines = c->poly_textured = c->poly_untextured = 0;
    c->shadows = c->others = 0;
    c->object_models = c->object_sprites = 0;
    c->gold_sprites = c->gold_polygons = c->gold_models = 0;
    if (o->cs && c->cs != o->cs)
        sa1_capture_set_archives(c, o->cs);
    for (i = 0; i < c->nsnap; i++) {
        const sa1_cap_rec *r = &c->snap[i];
        double m[12];
        sa1_draw_state ds;
        sa1_emit_info ei;
        int own, shadow, glow, object = sa1_capture_object_exec(r->exec);
        uint32_t nn0 = b->nn, ns0 = b->nspr, nlv0 = b->nlv, nt0 = b->nt, k;
        if (!SA1_CAP_IS_MODEL(r->kind)) {
            if (o->only_model || !o->effects ||
                (r->kind != SA1_CAP_SPRITE3D && r->kind != SA1_CAP_LINE3D && r->kind != SA1_CAP_TRI3D &&
                 r->kind != SA1_CAP_POLY3D))
                continue;
            sa1_capture_world_matrix(r, m);
            if (clipped(o, m)) {
                c->clipped++;
                continue;
            }
            if (r->kind == SA1_CAP_SPRITE3D)
                st = emit_sprite(ctx, c, b, r, m);
            else if (r->kind == SA1_CAP_LINE3D)
                st = emit_lines(ctx, c, b, r, m);
            else
                st = emit_polygon(ctx, c, b, r, m);
            if (st != SA1_OK)
                return st;
            /* round 0.9.4: who drew the sprites (spr_exec), and the count of OBJECT ones (a line / polygon record: 1) */
            if (object)
                c->object_sprites += b->nspr > ns0 ? b->nspr - ns0 : (b->nlv > nlv0 || b->nt > nt0) ? 1u : 0u;
            if (o->exec_arrays && b->nspr > ns0) {
                st = exec_room(ctx, b, b->nn, b->nspr);
                if (st != SA1_OK)
                    return st;
                for (k = ns0; k < b->nspr; k++)
                    b->spr_exec[k] = r->exec;
            }
            continue;
        }
        if (o->kinds && !(o->kinds & (1u << r->kind)))
            continue;
        if (o->only_model && (r->obj & 0x1FFFFFFFu) != (o->only_model & 0x1FFFFFFFu)) {
            c->others++;
            continue;
        }
        if (o->exclude_model && (r->obj & 0x1FFFFFFFu) == (o->exclude_model & 0x1FFFFFFFu)) {
            c->excluded++;
            continue;
        }
        shadow = o->shadow_model && (r->obj & 0x1FFFFFFFu) == (o->shadow_model & 0x1FFFFFFFu);
        sa1_capture_world_matrix(r, m);
        if (clipped(o, m)) {
            c->clipped++;
            continue;
        }
        own = o->texlist0 != 0 && (r->texlist & 0x1FFFFFFFu) == (o->texlist0 & 0x1FFFFFFFu);
        if (!own) {
            c->foreign++;
            if (o->own_texlist_only)
                continue;
        }
        sa1_capture_draw_state(r, &ds);
        if (o->dedupe && duplicate_draw(c, i, &ds)) {
            c->duplicates++;
            continue;
        }
        memset(&ei, 0, sizeof ei);
        ei.reader = sa1_capture_read;
        ei.user = c;
        ei.ds = &ds;
        ei.ctx = ctx;
        ei.cs = c->cs;
        ei.texlist = r->texlist;
        ei.depth = r->depth;
        ei.textured = own;
        ei.source = (uint8_t)(ds.hand ? SA1_SOURCE_CHARACTER : SA1_SOURCE_MODEL);
        if (o->cs) {
            ei.resolve = sa1_capture_resolve;
            ei.tex_alpha = cap_tex_alpha;
        }
        if (!o->live_models)
            sa1_capture_model_snapshot(c, r, &ei);
        if (ds.material < 0 && ds.cmat[0] < 0.0f)
            ei.extra_flags |= SA1_DRAW_AFTERIMAGE;
        if (!own)
            ei.extra_flags |= SA1_DRAW_EFFECT;
        if (shadow)
            /* the blob shadow: drawn and marked (sadc docs/research/gsa1_060_shadow.md L3, Review R.4.1: it keeps
               SA1_DRAW_EFFECT, set here too while its texture setup leaves the character's own texlist current) */
            ei.extra_flags |= SA1_DRAW_SHADOW | SA1_DRAW_EFFECT;
        if (object)
            /* round 0.9.4: a game object's draw (its model, its blob shadow): SA1_DRAW_OBJECT in addition to the other
               flags, before the emitter's merge test (add_tri_draw, sa1_pose.c), so it never merges with a draw that is
               not one (a ring's blob shadow with the character's: gsa1_094_plan.md Review R.1) */
            ei.extra_flags |= SA1_DRAW_OBJECT;
        glow = cap_gold_glow(c, r);
        if (glow)
            /* round 0.9.4a M3: the charged glow in gold (the comment above cap_gold_texlist); the flag before the
               emitter's merge test */
            ei.extra_flags |= SA1_DRAW_SUPER_GOLD;
        st = sa1_pose_emit_model_ex(ctx, b, &ei, r->obj, m);
        if (st == SA1_ERR_OUT_OF_MEMORY)
            return st;
        if (st == SA1_OK && glow) {
            cap_gold_complement(b, nt0, b->nt);
            c->gold_models++;
        }
        c->emitted += st == SA1_OK;
        c->effect_draws += st == SA1_OK && !own;
        c->shadows += st == SA1_OK && shadow;
        if (st == SA1_OK && b->nn > nn0) {
            if (object) {
                for (k = nn0; k < b->nn; k++)
                    b->node_fl[k] |= (uint8_t)SA1_NODE_OBJECT;
                c->object_models++;
            }
            if (o->exec_arrays) {
                st = exec_room(ctx, b, b->nn, b->nspr);
                if (st != SA1_OK)
                    return st;
                for (k = nn0; k < b->nn; k++)
                    b->node_exec[k] = r->exec;
            }
        }
    }
    for (i = 0; i < b->ndraw; i++)
        if ((b->draws[i].flags & SA1_DRAW_NO_TEXTURE) && b->draws[i].source != SA1_SOURCE_SPRITE &&
            b->draws[i].source != SA1_SOURCE_POLYGON)
            c->unresolved++;
    sa1_meshbuf_geometry(b, g);
    return SA1_OK;
}

sa1_status sa1_capture_emit(sa1_context *ctx, sa1_capture *c, const sa1_capture_emit_opts *o, sa1_geometry *g)
{
    if (!c)
        return SA1_ERR_INVALID_ARGUMENT;
    return sa1_capture_emit_into(ctx, c, o, &c->buf, g);
}