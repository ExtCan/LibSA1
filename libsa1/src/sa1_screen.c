/*
 * sa1_screen.c - the HUD and the 2D screen output of a character (round 0.9.1, API 0.4 draft; include/libsa1.h
 * "screen / HUD"; sadc docs/research/gsa1_070_hud.md F4 / F6 with its Review R.3.2, R.4.4, R.5.3; sa1_screen.h).
 *
 * Asset layer (plain C99, no world headers). The context keeps each character's HUD settings by character slot
 * (sa1_api_ext.screen: SA1_COPT_HUD_PARTS / SA1_COPT_CLOCK / SA1_COPT_E102_NO_TIME_LIMIT of sa1_character_set_option,
 * sa1_character_set_time) and hands them to the character's world at once when there is one (src/world/sa1_world_hud.c
 * sa1s_world_apply), else to the world built later (sa1_character.c ensure_world -> sa1_screen_world_attach, before its
 * first step).
 *
 * sa1_character_screen builds the screen from the character's display capture (sa1_capture.h): the last pair the
 * character's tick captured, whose records hold the game's 2D draws (rt_draw.c: SPRITE2D with its priority, the running
 * exec and the texlist before the draw; POLY2D, njDrawPolygon2D). A record belongs to a HUD part when it lies in the
 * record range the world's HUD pass noted for that part in that pair (sa1s_world_ranges), else to SA1_HUD_CHARACTER (a
 * task's draw, made inside the executor: its exec is the source). The draws follow the SDK's rules as the 3D ones of
 * sa1_capture.c do (sprite size tanim px x sp->sx by tanim px x sp->sy around the cell's centre, UV / 256, flips,
 * NJD_SPRITE_ANGLE, the constant material's colour with NJD_SPRITE_COLOR, the blend of njColorBlendingMode with
 * NJD_SPRITE_ALPHA; a polygon's attr 0x20 fill (else its outline), 0x40 translucent, 0x80000000 textured (a one-texture
 * texlist's texture 0: njSetTextureNum is not recorded, as for the 3D polygons)) on the 640 x 480 screen, untransformed.
 * Order: the opaque draws, then the translucent ones, each sorted stably from the most negative priority (behind) to
 * the least (front): the Dreamcast's 2D layers are depth-sorted (opaque by the depth test, translucent by the PowerVR's
 * sort; whether SA1's translucent list is auto-sorted is UNVERIFIED, gsa1_070_hud.md 7: by priority is the faithful
 * choice under auto-sort). Display only: without SA1_HUD_TIME the rings part's TIME label (D_8C1BF1A4 cell 11) is drawn
 * as the game's ring-only icon D_8C1BF5B4 (the lower 47 of its 64 rows, at (16, 49): the adventure fields' sprite).
 */
#include "sa1_internal.h"
#include "sa1_api_world.h"
#include "sa1_capture.h"
#include "sa1_screen.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#define S_SCREEN_W      640.0f
#define S_SCREEN_H      480.0f
#define S_HUD_TLIST     0x8C1BF1A4u     /* D_8C1BF1A4: the HUD's texlist (CON_REGULAR's first 24 names) */
#define S_RING_ONLY     0x8C1BF5B4u     /* D_8C1BF5B4: the adventure fields' ring icon (an NJS_SPRITE, cell 0 of its tanim) */
#define S_LABEL_CELL    11u             /* hyoji_head: the TIME label above the ring icon */

/* Ninja sprite / polygon attributes (sa1_capture.c) */
#define S_SPRITE_ANGLE  0x01u
#define S_SPRITE_COLOR  0x02u
#define S_SPRITE_HFLIP  0x04u
#define S_SPRITE_VFLIP  0x08u
#define S_SPRITE_ALPHA  0x20u
#define S_POLY_FILL     0x20u
#define S_POLY_TRANS    0x40u
#define S_POLY_TEXTURE  0x80000000u
#define S_MAXPTS        512u

typedef struct s_item {
    uint32_t rec;               /* snapshot record index */
    uint32_t trans;             /* 1: blended */
    float pri;
    uint32_t part;
} s_item;

typedef struct sa1s_slot {
    sa1s_charcfg cfg;
    sa1_screen scr;
    sa1_screen_draw *draws;
    uint32_t ndraw, cap_draw;
    sa1_screen_sprite *spr;
    uint32_t nspr, cap_spr;
    sa1_screen_vertex *vtx;
    uint32_t nvtx, cap_vtx;
    s_item *items;
    uint32_t cap_items;
    uint32_t gen;
    /* what the last build was made of */
    int32_t built;
    sa1_character_id b_id;
    const void *b_cap;
    uint32_t b_windows, b_total, b_parts;
    int32_t b_frame;
} sa1s_slot;

struct sa1s_ctx {
    sa1s_slot *slot[SA1_MAX_INSTANCES];
};

/* ---- settings ----------------------------------------------------------------------------------------------------- */
static int s_slot_of(const sa1_context *ctx, sa1_character_id id, uint32_t *slot)
{
    uint32_t s = ((uint32_t)id & 0xFFFFu) - 1u, gen = ((uint32_t)id >> 16) & 0x7FFFu;
    if (ctx == NULL || id <= 0 || s >= SA1_MAX_INSTANCES || ctx->inst[s] == NULL || ctx->inst_gen[s] != gen) {
        return 0;
    }
    *slot = s;
    return 1;
}

/* the slot record of a live character (created on first use; fresh when the slot held another id); NULL: no such
 * character or no memory (*st says which) */
static sa1s_slot *s_slot(sa1_context *ctx, sa1_character_id id, sa1_status *st)
{
    sa1_api_ext *x;
    sa1s_slot *sl;
    uint32_t s;
    *st = SA1_ERR_NOT_FOUND;
    if (!s_slot_of(ctx, id, &s)) {
        return NULL;
    }
    *st = SA1_ERR_OUT_OF_MEMORY;
    x = sa1_api_ext_of(ctx);
    if (x->screen == NULL) {
        x->screen = (struct sa1s_ctx *)sa1_mem_alloc(ctx, sizeof *x->screen);
        if (x->screen == NULL) {
            return NULL;
        }
    }
    sl = x->screen->slot[s];
    if (sl == NULL) {
        sl = (sa1s_slot *)sa1_mem_alloc(ctx, sizeof *sl);
        if (sl == NULL) {
            return NULL;
        }
        x->screen->slot[s] = sl;
    }
    if (sl->cfg.id != id) {
        memset(&sl->cfg, 0, sizeof sl->cfg);
        sl->cfg.id = id;
        sl->built = 0;
    }
    *st = SA1_OK;
    return sl;
}

#if !defined(SA1_NO_WORLD)
/* the settings of a character if it has a record (NULL: none given) */
static const sa1s_slot *s_slot_peek(const sa1_context *ctx, sa1_character_id id)
{
    const sa1_api_ext *x;
    const sa1s_slot *sl;
    uint32_t s;
    if (!s_slot_of(ctx, id, &s)) {
        return NULL;
    }
    x = sa1_api_ext_of(ctx);
    sl = x->screen != NULL ? x->screen->slot[s] : NULL;
    return (sl != NULL && sl->cfg.id == id) ? sl : NULL;
}
#endif

static sa1_status s_apply(sa1_context *ctx, sa1_character_id id, const sa1s_charcfg *cfg, uint32_t what)
{
#if !defined(SA1_NO_WORLD)
    struct sa1g_world *w = sa1_character_world(ctx, id);
    sa1_status st;
    if (w == NULL) {
        return SA1_OK;
    }
    st = sa1s_world_apply(ctx, w, cfg, what);
    if (st != SA1_OK) {
        return sa1_fail(ctx, st, "character %d: the HUD settings: the world faulted", (int)id);
    }
    return SA1_OK;
#else
    (void)ctx;
    (void)id;
    (void)cfg;
    (void)what;
    return SA1_OK;
#endif
}

sa1_status sa1_screen_set_option(sa1_context *ctx, sa1_character_id id, uint32_t option, int32_t value)
{
    sa1s_slot *sl;
    sa1_status st;
    uint32_t what;
    if (ctx == NULL) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    if (option == SA1_COPT_HUD_PARTS && (value < 0 || ((uint32_t)value & ~SA1_HUD_ALL) != 0)) {
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "SA1_COPT_HUD_PARTS: unknown parts 0x%X", (unsigned)value);
    }
    sl = s_slot(ctx, id, &st);
    if (sl == NULL) {
        return st;
    }
    switch (option) {
    case SA1_COPT_CLOCK:
        sl->cfg.clock = value != 0;
        what = SA1S_SET_CLOCK;
        break;
    case SA1_COPT_E102_NO_TIME_LIMIT:
        sl->cfg.e102_free = value != 0;
        what = SA1S_SET_E102;
        break;
    case SA1_COPT_HUD_PARTS:
        sl->cfg.parts = (uint32_t)value;
        what = SA1S_SET_PARTS;
        break;
    default:
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_character_set_option: unknown option %u", (unsigned)option);
    }
    sl->cfg.set |= what;
    return s_apply(ctx, id, &sl->cfg, what);
}

sa1_status sa1_character_set_time(sa1_context *ctx, sa1_character_id id, int32_t minutes, int32_t seconds,
                                  int32_t frames)
{
    sa1s_slot *sl;
    sa1_status st;
    if (ctx == NULL) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    sl = s_slot(ctx, id, &st);
    if (sl == NULL) {
        return st;
    }
    sl->cfg.time[0] = minutes < 0 ? 0 : minutes > 99 ? 99 : minutes;
    sl->cfg.time[1] = seconds < 0 ? 0 : seconds > 59 ? 59 : seconds;
    sl->cfg.time[2] = frames < 0 ? 0 : frames > 59 ? 59 : frames;
    sl->cfg.set |= SA1S_SET_TIME;
    return s_apply(ctx, id, &sl->cfg, SA1S_SET_TIME);
}

sa1_status sa1_screen_world_attach(sa1_context *ctx, sa1_character_id id, struct sa1g_world *w)
{
#if !defined(SA1_NO_WORLD)
    const sa1s_slot *sl = s_slot_peek(ctx, id);
    if (sl == NULL || sl->cfg.set == 0 || w == NULL) {
        return SA1_OK;
    }
    return sa1s_world_apply(ctx, w, &sl->cfg, SA1S_SET_ALL);
#else
    (void)ctx;
    (void)id;
    (void)w;
    return SA1_OK;
#endif
}

void sa1_screen_state_tail(const struct sa1g_world *w, sa1_state_ex *x)
{
#if !defined(SA1_NO_WORLD)
    sa1s_tail t;
    sa1s_world_tail(w, &t);
    if (t.valid) {
        x->time_minutes = t.min;
        x->time_seconds = t.sec;
        x->time_frames = t.frames;
        x->clock_running = t.clock_running;
    }
#else
    (void)w;
    (void)x;
#endif
}

static void s_slot_free(const sa1_context *ctx, sa1s_slot *sl)
{
    if (sl == NULL) {
        return;
    }
    sa1_mem_free(ctx, sl->draws);
    sa1_mem_free(ctx, sl->spr);
    sa1_mem_free(ctx, sl->vtx);
    sa1_mem_free(ctx, sl->items);
    sa1_mem_free(ctx, sl);
}

void sa1_screen_free_all(sa1_context *ctx)
{
    sa1_api_ext *x;
    uint32_t s;
    if (ctx == NULL) {
        return;
    }
    x = sa1_api_ext_of(ctx);
    if (x->screen == NULL) {
        return;
    }
    for (s = 0; s < SA1_MAX_INSTANCES; s++) {
        s_slot_free(ctx, x->screen->slot[s]);
    }
    sa1_mem_free(ctx, x->screen);
    x->screen = NULL;
}

/* ---- the screen ------------------------------------------------------------------------------------------------- */
static int s_grow(const sa1_context *ctx, void **arr, uint32_t *cap, uint32_t need, size_t elem)
{
    void *n;
    uint32_t c = *cap ? *cap : 64u;
    if (need <= *cap) {
        return 1;
    }
    while (c < need) {
        c *= 2u;
    }
    n = sa1_mem_array(ctx, c, elem);
    if (n == NULL) {
        return 0;
    }
    if (*arr != NULL && *cap != 0) {
        memcpy(n, *arr, (size_t)*cap * elem);
    }
    sa1_mem_free(ctx, *arr);
    *arr = n;
    *cap = c;
    return 1;
}

static uint8_t s_unit_byte(float f)
{
    double x = (double)f * 255.0;
    if (!(x > 0.0)) {
        return 0;
    }
    return x >= 255.0 ? 255 : (uint8_t)(x + 0.5);
}

/* blend of a translucent draw from njColorBlendingMode's modes at the draw (sa1_capture.c ninja_blend). A source mode
 * NJD_COLOR_BLENDING_BOTHINVALPHA (0) / BOTHSRCALPHA (1) gives both factors (Ninja's "both" modes: source and
 * destination at once; the PowerVR's blend instructions have no such mode, so the destination comes from the source
 * mode): Big's HUD sets only the source (njColorBlendingMode(0, 1), code_8C0E6FE0.c) and draws over whatever destination
 * mode the draws before it left - the blob shadow's ONE in libsa1 worlds, the SDK's model draws' material blend
 * (INVSRCALPHA) on the Dreamcast (the reference's d2d logs: 0601). UNVERIFIED on hardware; the 3D emitter of
 * sa1_capture.c still maps the two modes one by one. */
static uint32_t s_blend(const sa1_cap_rec *r, int translucent, uint8_t *bs, uint8_t *bd)
{
    uint32_t src = r->blend & 0xFFu, dst = (r->blend >> 8) & 0xFFu;
    if (!translucent) {
        *bs = (uint8_t)SA1_BLEND_ONE;
        *bd = (uint8_t)SA1_BLEND_ZERO;
        return 0;
    }
    if (src == 0u || src == 1u) {
        dst = src;
    }
    *bs = sa1_blend_of_ninja(src, 0);
    *bd = sa1_blend_of_ninja(dst, 1);
    return SA1_DRAW_BLEND | (*bd == SA1_BLEND_ONE ? SA1_DRAW_ADDITIVE : 0u);
}

/* the record's priority and exec (0 for a payload without them) */
static float s_pri(const sa1_capture *c, const sa1_cap_rec *r, uint32_t *exec)
{
    const uint8_t *p;
    *exec = 0;
    if (r->kind == SA1_CAP_SPRITE2D) {
        p = sa1_capture_payload(c, r, SA1_CAP_SPRITE2D_PAY);
        if (p != NULL) {
            *exec = sa1_rd32(p + 0x38);
            return sa1_rdf32(p + 0x34);
        }
        return 0.0f;
    }
    p = sa1_capture_payload(c, r, SA1_CAP_POLY2D_HEAD);
    if (p != NULL) {
        *exec = sa1_rd32(p + 0x14);
        return sa1_rdf32(p + 0x10);
    }
    return 0.0f;
}

static int s_translucent(const sa1_cap_rec *r)
{
    return r->kind == SA1_CAP_SPRITE2D ? (r->attr & S_SPRITE_ALPHA) != 0 : (r->attr & S_POLY_TRANS) != 0;
}

/* a new draw, merged into the last one when it is the same sprite state */
static sa1_screen_draw *s_draw(sa1_context *ctx, sa1s_slot *sl, uint32_t prim, int32_t arc, int32_t tex, uint32_t flags,
                               uint8_t bs, uint8_t bd, float pri, uint32_t part, uint32_t source, int merge)
{
    sa1_screen_draw *d = sl->ndraw ? &sl->draws[sl->ndraw - 1u] : NULL;
    if (merge && d != NULL && d->prim == prim && d->archive == arc && d->texture == tex && d->flags == flags &&
        d->blend_src == bs && d->blend_dst == bd && d->priority == pri && d->part == part && d->source == source) {
        return d;
    }
    if (!s_grow(ctx, (void **)&sl->draws, &sl->cap_draw, sl->ndraw + 1u, sizeof *sl->draws)) {
        return NULL;
    }
    d = &sl->draws[sl->ndraw++];
    memset(d, 0, sizeof *d);
    d->prim = prim;
    d->first = prim == SA1_PRIM_SPRITES ? sl->nspr : sl->nvtx;
    d->archive = arc;
    d->texture = tex;
    d->flags = flags;
    d->blend_src = bs;
    d->blend_dst = bd;
    d->priority = pri;
    d->part = part;
    d->source = source;
    return d;
}

static sa1_status s_emit_sprite(sa1_context *ctx, sa1s_slot *sl, sa1_capture *c, const sa1_charset *cs,
                                const sa1_cap_rec *r, const s_item *it, int crop)
{
    const uint8_t *p = sa1_capture_payload(c, r, 0x34u);
    uint8_t sp[0x20], ta[0x14];
    sa1_screen_sprite *o;
    sa1_screen_draw *d;
    float sx, sy, tw, th, cx, cy, u0, v0, u1, v1, t, cm[4];
    uint32_t attr = r->attr, flags = SA1_DRAW_UNLIT, exec = 0, tl = r->texlist;
    int32_t arc = -1, tex;
    uint8_t bs, bd;
    if (p == NULL) {
        return SA1_OK;
    }
    memcpy(sp, p, sizeof sp);
    memcpy(ta, p + 0x20, sizeof ta);
    if (r->pay_len >= SA1_CAP_SPRITE2D_PAY) {
        exec = sa1_rd32(p + 0x38);
    }
    if (crop && tl == S_HUD_TLIST && sa1_rd16(ta + 0x10) == S_LABEL_CELL) {
        /* display only (Review R.4.4): the game's own ring-only sprite of the adventure fields in place of the TIME label */
        const uint8_t *rs = sa1_capture_read(c, S_RING_ONLY, 0x20u), *rt;
        /* its tlist is CON_REGULAR's own (8C1C3310), whose name 11 is D_8C1BF1A4's: the label's texture */
        if (rs != NULL) {
            rt = sa1_capture_read(c, sa1_rd32(rs + 0x1C), 0x14u);
            if (rt != NULL && sa1_rd16(rt + 0x10) == S_LABEL_CELL) {
                memcpy(sp, rs, 8);              /* p.x, p.y (its scale: the label's) */
                memcpy(ta, rt, sizeof ta);
            }
        }
    }
    sx = sa1_rdf32(sp + 0x0C);
    sy = sa1_rdf32(sp + 0x10);
    tw = (float)(int16_t)sa1_rd16(ta + 0x00);
    th = (float)(int16_t)sa1_rd16(ta + 0x02);
    cx = (float)(int16_t)sa1_rd16(ta + 0x04);
    cy = (float)(int16_t)sa1_rd16(ta + 0x06);
    u0 = (float)(int16_t)sa1_rd16(ta + 0x08) / 256.0f;
    v0 = (float)(int16_t)sa1_rd16(ta + 0x0A) / 256.0f;
    u1 = (float)(int16_t)sa1_rd16(ta + 0x0C) / 256.0f;
    v1 = (float)(int16_t)sa1_rd16(ta + 0x0E) / 256.0f;
    if (attr & S_SPRITE_HFLIP) {
        t = u0; u0 = u1; u1 = t;
    }
    if (attr & S_SPRITE_VFLIP) {
        t = v0; v0 = v1; v1 = t;
    }
    tex = sa1_capture_resolve(c, tl, (uint32_t)sa1_rd16(ta + 0x10), &arc);
    if (tex < 0) {
        arc = -1;
        flags |= SA1_DRAW_NO_TEXTURE;
    }
    flags |= s_blend(r, (attr & S_SPRITE_ALPHA) != 0, &bs, &bd);
    if (tex >= 0 && !(flags & SA1_DRAW_BLEND) && (attr & S_SPRITE_ALPHA) &&
        sa1_charset_texture_alpha(ctx, cs, arc, tex) == SA1_TEXALPHA_BINARY) {
        flags |= SA1_DRAW_ALPHA_TEST;
    }
    d = s_draw(ctx, sl, SA1_PRIM_SPRITES, arc, tex, flags, bs, bd, it->pri, it->part, exec, 1);
    if (d == NULL || !s_grow(ctx, (void **)&sl->spr, &sl->cap_spr, sl->nspr + 1u, sizeof *sl->spr)) {
        return SA1_ERR_OUT_OF_MEMORY;
    }
    o = &sl->spr[sl->nspr++];
    d->count++;
    memset(o, 0, sizeof *o);
    o->rect[0] = -cx * sx;
    o->rect[1] = -cy * sy;
    o->rect[2] = (tw - cx) * sx;
    o->rect[3] = (th - cy) * sy;
    o->pivot[0] = sa1_rdf32(sp + 0x00);
    o->pivot[1] = sa1_rdf32(sp + 0x04);
    o->angle = (attr & S_SPRITE_ANGLE) ? (float)((double)sa1_rds32(sp + 0x14) * (6.283185307179586 / 65536.0)) : 0.0f;
    o->uv[0] = u0;
    o->uv[1] = v0;
    o->uv[2] = u1;
    o->uv[3] = v1;
    memcpy(cm, r->cmat, sizeof cm);
    if ((attr & S_SPRITE_COLOR) && cm[0] == cm[0] && cm[1] == cm[1] && cm[2] == cm[2] && cm[3] == cm[3]) {
        o->color[0] = s_unit_byte(cm[1]);
        o->color[1] = s_unit_byte(cm[2]);
        o->color[2] = s_unit_byte(cm[3]);
        o->color[3] = s_unit_byte(cm[0]);
    } else {
        o->color[0] = o->color[1] = o->color[2] = o->color[3] = 255;
    }
    o->priority = it->pri;
    o->part = it->part;
    o->source = exec;
    return SA1_OK;
}

static sa1_status s_emit_poly(sa1_context *ctx, sa1s_slot *sl, sa1_capture *c, const sa1_charset *cs,
                              const sa1_cap_rec *r, const s_item *it)
{
    const uint8_t *p = sa1_capture_payload(c, r, SA1_CAP_POLY2D_HEAD), *pt, *pc, *pu;
    uint32_t num, hasc, hast, exec, flags = SA1_DRAW_UNLIT, nv, i, t, s[S_MAXPTS], lo = 0, hi;
    int32_t arc = -1, tex = -1;
    int fill = (r->attr & S_POLY_FILL) != 0, textured = 0;
    uint8_t bs, bd;
    sa1_screen_draw *d;
    if (p == NULL) {
        return SA1_OK;
    }
    num = sa1_rd32(p);
    hasc = sa1_rd32(p + 4) != 0;
    hast = sa1_rd32(p + 8) != 0;
    exec = sa1_rd32(p + 0x14);
    if (num < 2 || num > S_MAXPTS || SA1_CAP_POLY2D_HEAD + (uint64_t)num * (8u + 4u * hasc + 4u * hast) > r->pay_len) {
        return SA1_OK;
    }
    if (fill && num < 3) {
        return SA1_OK;
    }
    pt = p + SA1_CAP_POLY2D_HEAD;
    pc = pt + 8u * num;
    pu = pc + (hasc ? 4u * num : 0u);
    if (r->attr & S_POLY_TEXTURE) {
        if (fill && hast && r->texlist) {
            const uint8_t *tl = sa1_capture_read(c, r->texlist, 8u);
            if (tl != NULL && sa1_rd32(tl + 4) == 1u) {
                tex = sa1_capture_resolve(c, r->texlist, 0u, &arc);
            }
        }
        textured = tex >= 0;
        if (!textured) {
            arc = -1;
            tex = -1;
            flags |= SA1_DRAW_NO_TEXTURE;
        }
    }
    flags |= s_blend(r, (r->attr & S_POLY_TRANS) != 0, &bs, &bd);
    if (textured && !(flags & SA1_DRAW_BLEND) && (r->attr & S_POLY_TRANS) &&
        sa1_charset_texture_alpha(ctx, cs, arc, tex) == SA1_TEXALPHA_BINARY) {
        flags |= SA1_DRAW_ALPHA_TEST;
    }
    /* filled: one strip in the SDK's polygon order 0, n-1, 1, n-2, ... (njDrawPolygon3D's, sa1_capture.c emit_polygon;
       UNVERIFIED for 8C63EBC8's 2D routines: for a convex polygon only the diagonals of a gouraud fill can differ);
       outline: the closing edge (0, n-1), then (i-1, i) */
    hi = num - 1u;
    for (i = 0; i < num; i++) {
        s[i] = (i & 1u) ? hi-- : lo++;
    }
    nv = fill ? 3u * (num - 2u) : 2u * num;
    d = s_draw(ctx, sl, fill ? SA1_PRIM_TRIANGLES : SA1_PRIM_LINES, arc, tex, flags, bs, bd, it->pri, it->part, exec, 0);
    if (d == NULL || !s_grow(ctx, (void **)&sl->vtx, &sl->cap_vtx, sl->nvtx + nv, sizeof *sl->vtx)) {
        return SA1_ERR_OUT_OF_MEMORY;
    }
    for (t = 0; t < nv; t++) {
        sa1_screen_vertex *v = &sl->vtx[sl->nvtx + t];
        uint32_t k;
        if (fill) {
            uint32_t tri = t / 3u, corner = t % 3u;
            k = s[tri + (corner == 0 ? 0u : (tri & 1u) ? corner : 3u - corner)];
        } else {
            uint32_t seg = t / 2u, end = t % 2u;
            k = seg == 0 ? (end ? num - 1u : 0u) : (end ? seg : seg - 1u);
        }
        v->x = sa1_rdf32(pt + 8u * k);
        v->y = sa1_rdf32(pt + 8u * k + 4u);
        v->u = textured ? (float)(int16_t)sa1_rd16(pu + 4u * k) / 256.0f : 0.0f;
        v->v = textured ? (float)(int16_t)sa1_rd16(pu + 4u * k + 2u) / 256.0f : 0.0f;
        if (hasc) {
            uint32_t col = sa1_rd32(pc + 4u * k);
            v->color[0] = (uint8_t)(col >> 16);
            v->color[1] = (uint8_t)(col >> 8);
            v->color[2] = (uint8_t)col;
            v->color[3] = (uint8_t)(col >> 24);
        } else {
            v->color[0] = v->color[1] = v->color[2] = v->color[3] = 255;
        }
    }
    sl->nvtx += nv;
    d->count = nv;
    return SA1_OK;
}

/* stable: opaque before blended, then by priority (most negative first), then the game's order */
static int s_before(const s_item *a, const s_item *b)
{
    if (a->trans != b->trans) {
        return a->trans < b->trans;
    }
    if (a->pri != b->pri) {
        return a->pri < b->pri;
    }
    return a->rec < b->rec;
}

static sa1_status s_build(sa1_context *ctx, sa1s_slot *sl, sa1_capture *c, const sa1_charset *cs,
                          const struct sa1g_world *w, int32_t frame)
{
    const sa1_cap_rec *recs = NULL;
    uint32_t n = sa1_capture_records(c, &recs), i, k, nit = 0, parts = sl->cfg.parts, first[3], end[3];
    int ranges = 0, crop = (parts & SA1_HUD_RINGS) && !(parts & SA1_HUD_TIME);
    sa1_status st;
    sl->ndraw = sl->nspr = sl->nvtx = 0;
#if !defined(SA1_NO_WORLD)
    ranges = sa1s_world_ranges(w, frame, first, end);
#else
    (void)w;
    (void)frame;
#endif
    if (!ranges) {
        for (k = 0; k < 3u; k++) {
            first[k] = end[k] = 0;
        }
    }
    if (!s_grow(ctx, (void **)&sl->items, &sl->cap_items, n + 1u, sizeof *sl->items)) {
        return SA1_ERR_OUT_OF_MEMORY;
    }
    for (i = 0; i < n; i++) {
        const sa1_cap_rec *r = &recs[i];
        static const uint32_t part_of[3] = { SA1_HUD_RINGS, SA1_HUD_LIVES, SA1_HUD_TIME };
        s_item it;
        uint32_t exec;
        if (r->kind != SA1_CAP_SPRITE2D && r->kind != SA1_CAP_POLY2D) {
            continue;
        }
        it.rec = i;
        it.part = SA1_HUD_CHARACTER;
        for (k = 0; k < 3u; k++) {
            if (i >= first[k] && i < end[k]) {
                it.part = part_of[k];
            }
        }
        if (!(parts & it.part)) {
            continue;
        }
        it.trans = (uint32_t)s_translucent(r);
        it.pri = s_pri(c, r, &exec);
        if (!(it.pri == it.pri)) {
            it.pri = 0.0f;
        }
        /* insertion into the sorted list (at most a few hundred) */
        for (k = nit; k > 0 && s_before(&it, &sl->items[k - 1u]); k--) {
            sl->items[k] = sl->items[k - 1u];
        }
        sl->items[k] = it;
        nit++;
    }
    for (i = 0; i < nit; i++) {
        const s_item *it = &sl->items[i];
        const sa1_cap_rec *r = &recs[it->rec];
        if (r->kind == SA1_CAP_SPRITE2D) {
            st = s_emit_sprite(ctx, sl, c, cs, r, it, crop && it->part == SA1_HUD_RINGS);
        } else {
            st = s_emit_poly(ctx, sl, c, cs, r, it);
        }
        if (st != SA1_OK) {
            return st;
        }
    }
    return SA1_OK;
}

sa1_status sa1_character_screen(sa1_context *ctx, sa1_character_id id, const sa1_screen **out)
{
    sa1s_slot *sl;
    sa1_capture *c;
    sa1_capture_stats cst;
    const sa1_charset *cs;
    sa1_status st;
    uint32_t s;
    if (out != NULL) {
        *out = NULL;
    }
    if (ctx == NULL || out == NULL) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    sl = s_slot(ctx, id, &st);
    if (sl == NULL) {
        return st;
    }
    (void)s_slot_of(ctx, id, &s);
    c = sa1_character_capture(ctx, id);
    cs = &ctx->chars[ctx->inst[s]->kind >= 0 && ctx->inst[s]->kind < SA1_CHAR_COUNT ? ctx->inst[s]->kind : 0];
    memset(&cst, 0, sizeof cst);
    if (c != NULL) {
        sa1_capture_get_stats(c, &cst);
    }
    if (!sl->built || sl->b_id != id || sl->b_cap != (const void *)c || sl->b_windows != cst.windows ||
        sl->b_total != cst.total || sl->b_frame != cst.frame || sl->b_parts != sl->cfg.parts) {
        sl->ndraw = sl->nspr = sl->nvtx = 0;
        if (c != NULL && sl->cfg.parts != 0 && sa1_capture_valid(c)) {
            st = s_build(ctx, sl, c, cs, sa1_character_world(ctx, id), cst.frame);
            if (st != SA1_OK) {
                sl->built = 0;
                sl->ndraw = sl->nspr = sl->nvtx = 0;
                return sa1_fail(ctx, st, "character %d: sa1_character_screen: out of memory", (int)id);
            }
        }
        sl->built = 1;
        sl->b_id = id;
        sl->b_cap = c;
        sl->b_windows = cst.windows;
        sl->b_total = cst.total;
        sl->b_frame = cst.frame;
        sl->b_parts = sl->cfg.parts;
        sl->gen++;
    }
    memset(&sl->scr, 0, sizeof sl->scr);
    sl->scr.struct_size = sizeof sl->scr;
    sl->scr.generation = sl->gen;
    sl->scr.width = S_SCREEN_W;
    sl->scr.height = S_SCREEN_H;
    sl->scr.num_draws = sl->ndraw;
    sl->scr.draws = sl->draws;
    sl->scr.num_sprites = sl->nspr;
    sl->scr.sprites = sl->spr;
    sl->scr.num_vertices = sl->nvtx;
    sl->scr.vertices = sl->vtx;
    *out = &sl->scr;
    return SA1_OK;
}
