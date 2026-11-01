/*
 * sa1_world_preview.c - object previews, the world side (API SA1_API_PREVIEW, round 0.9.4b; sadc docs/research/
 * objects2_plan.md 6.2 tasks 2-6 with its Review R.1, R.3, R.20, R.21; objects2_view.md 3 M4 / M7, 5.2). The context side
 * and the overview: src/sa1_preview.h, src/sa1_preview.c.
 *
 * The showcase world (review R.3, R.21): sa1w_ctx_get -> sa1w_ctx_wc -> the kind's pristine image from the character worlds'
 * image cache (composed here when nobody composed it yet, installed under the table lock as the bridge's wc_image does) ->
 * sa1g_world_create(w, &wc->shared) -> sa1g_world_apply_image -> sa1g_world_boot with sa1g_boot_defaults: CurrentCharacter
 * = the kind (sa1_world_boot.c writes it whatever SA1G_BOOT_NO_PLAYER says, so GetPlayerNumber gives the kind's Extra Life
 * face: code_8C04F698.c), level 1 act 0 (stage 0x0100: the ground box calls STG03's 0C907998 only in stage 0x0302), land
 * mode SA1G_LAND_EMPTY (LAND_UPDATE runs in every step), the clock stopped (no SA1G_BOOT_CLOCK), SA1G_BOOT_NO_PLAYER (no
 * player, no item-icon list task, no boot land update), the camera adapter task and the particle system as in character
 * worlds. Never sa1w_world_create (it makes a player and attaches the collision adapter and the character options): no
 * collision adapter, no host objects, no SET layer, no charopt / HUD / lane hooks - every hook of the world stays NULL. The
 * world's own host-side block (struct wp_world, the sa1g_world first) holds each slot's view distance for the executor
 * overrides.
 *
 * Per copy (task 3): CreateElementalTask(im, level, exec) of the catalogue (sa1set_type_of), twp pos = the slot, ang = 0
 * (SA1_PREVIEW_ROT_NONE / OUTER) or the record's BAMS angles in the order Z, X, Y (O_GRING: sa1w_set_angles, in the world's
 * FP mode as the SET layer converts a record), scl = params; tp->ocp = its condition B_8C79A3E0[slot + 1] (scCount 1,
 * scUserFlag, ssCondition, pTask, pObjEditEntry = its SET entry, unionStatus 0: 8C0919CC then never ranges it out).
 *
 * The view per exec (task 4): one executor override per type exec (sa1g_world_xhook: the replay walker sa1g_run_tasks, the
 * semantics of 8C0988A4); before calling the exec it loads XMTRX (the way sa1g_camera_view writes a view: x right, y down,
 * z forward, Ninja's row-vector convention), slot 0 of the matrix stack when the stack is at depth 0 (the base the capture
 * divides by: njPushMatrix stores XMTRX there anyway) and the camera taskwk's position (the eye, as a character world's
 * camera adapter has it since 0.9.4) with a view looking along -z at the copy's slot from SA1PV_VIEW_UP above and its
 * distance in front. The sphere test 8C038D00 (radius 20 in every display of the four types) then passes for every part
 * of the copy (sa1_preview.c sizes the distance from the copy's extent: review R.20).
 *
 * The ring places (task 6): a scratch task work in segment 0x18 (pos 0, ang, scl = params) and 8C087EEE (a line, i = 0..n)
 * or 8C088000 (a circle, i = 1..n) per ring, exactly as objexec_O_GRING's mode 0 calls them (code_8C087B80.c).
 *
 * Determinism: no host time, no rand(), single precision in the world's FP mode; x86 = x64.
 */
#include "sa1_world.h"
#include "sa1_internal.h"
#include "sa1_api_world.h"
#include "sa1_preview.h"

#include <stddef.h>
#include <string.h>

/* ---- the game's addresses ------------------------------------------------------------------------------------------ */
#define WP_ADDR_COND        0x8C79A3E0u     /* OBJ_CONDITION B_8C79A3E0[1024] (0x10 each) */
#define WP_ADDR_HEAP_FREE   0x8C79F03Cu     /* MEMBLK *B_8C79F03C: the heap's free list (code_8C098998.c) */
#define WP_ADDR_TASK_FREE   0x8C79F048u     /* task *B_8C79F048: the task system's free list */
#define WP_ADDR_MTX_DEPTH   0x8C88F69Cu     /* Sint32 B_8C88F69C: the matrix stack's depth (rt_matrix.c) */
#define WP_ADDR_MTX_BASE    0x8C88F5FCu     /* NJS_MATRIX_PTR B_8C88F5FC: the matrix stack's slot 0 (rt_matrix.c) */
#define WP_FN_FREETASK      0x8C098786u     /* FreeTask(tp): exec = DestroyTask */
#define WP_EXEC_DESTROY     0x0C09865Cu     /* DestroyTask, & 0x1FFFFFFF */
#define WP_FN_LINE_PLACE    0x8C087EEEu     /* func_8C087EEE(NJS_POINT3 *pos, taskwk *twp, Sint32 i): a line's ring */
#define WP_FN_CIRCLE_PLACE  0x8C088000u     /* func_8C088000(NJS_POINT3 *pos, taskwk *twp, Sint32 i): a circle's ring */
#define WP_BAD_EXEC         0x8C000004u     /* tests: an address without code (the forced fault) */

#define WP_MAGIC            0x56455250u     /* "PREV" */
#define WP_SETENTRY         0x20u

/* the world's block in segment 0x18 (made once after the boot; a showcase never rebuilds) */
typedef struct wp_state {
    uint32_t magic, self;
    uint8_t sets[SA1PV_SLOTS][WP_SETENTRY];     /* the SET entry of each slot's condition */
    uint8_t twp[0x40];                          /* the ring places' scratch task work */
    float place[4];                             /* the ring places' output point */
} wp_state;

typedef struct wp_world {
    sa1g_world w;                               /* first: the world the context side holds */
    sa1_context *ctx;
    uint32_t state;                             /* guest address of the wp_state block */
    float dist[SA1PV_SLOTS];                    /* each slot's view distance (SA1 units) */
} wp_world;

static wp_world *wp_of(struct sa1g_world *w)
{
    return (wp_world *)(void *)w;
}

/* ---- guest memory (the arena, bounded) ----------------------------------------------------------------------------- */
static int wp_arena(uint32_t g)
{
    return g != 0 && ((g >> 24) & 31u) == SA1G_SEG_ARENA;
}

static uint32_t wp_rd32(const sa1g_world *w, uint32_t g)
{
    uint32_t v = 0;
    if (!wp_arena(g) || sa1g_world_rd32(w, g & ~3u, &v) != 0) {
        return 0;
    }
    return v;
}

static void wp_wr32(sa1g_world *w, uint32_t g, uint32_t v)
{
    if (wp_arena(g)) {
        sa1g_world_wr32(w, g & ~3u, v);
    }
}

static void wp_wrf(sa1g_world *w, uint32_t g, float f)
{
    uint32_t v;
    memcpy(&v, &f, 4);
    wp_wr32(w, g, v);
}

static int wp_same(uint32_t a, uint32_t b)
{
    return (a & 0x1FFFFFFFu) == (b & 0x1FFFFFFFu);
}

static uint32_t wp_cond(uint32_t slot)
{
    return WP_ADDR_COND + 0x10u * (slot + SA1PV_COND0);
}

static wp_state *wp_state_of(const wp_world *ww)
{
    const sa1g_world *w = &ww->w;
    uint32_t g = ww->state;
    wp_state *S;
    if (g == 0 || w->ext == NULL || ((g >> 24) & 31u) != SA1G_SEG_EXT ||
        (size_t)(g & 0xFFFFFFu) + sizeof(wp_state) > w->ext_used) {
        return NULL;
    }
    S = (wp_state *)(void *)(w->ext + (g & 0xFFFFFFu));
    return S->magic == WP_MAGIC && S->self == g ? S : NULL;
}

static uint32_t wp_guest(const wp_state *S, const void *p)
{
    return S->self + (uint32_t)((const uint8_t *)p - (const uint8_t *)S);
}

int sa1wp_native(uint32_t a)
{
    return sa1_fn_name(a) != NULL;
}

/* ---- the view per exec (task 4) ------------------------------------------------------------------------------------ */
static void wp_view(wp_world *ww, uint32_t slot)
{
    sa1g_world *w = &ww->w;
    float c[3], e[3], ax[3][3] = { { 1.0f, 0.0f, 0.0f }, { 0.0f, -1.0f, 0.0f }, { 0.0f, 0.0f, -1.0f } };
    uint32_t cam, base, i, j;
    sa1pv_slot_pos(slot, c);
    e[0] = c[0];
    e[1] = c[1] + SA1PV_VIEW_UP;
    e[2] = c[2] + ww->dist[slot];
    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            w->xmtrx[4 * i + j] = ax[j][i];
        }
        w->xmtrx[4 * i + 3] = 0.0f;
    }
    for (j = 0; j < 3; j++) {
        w->xmtrx[12 + j] = -(e[0] * ax[j][0] + e[1] * ax[j][1] + e[2] * ax[j][2]);
    }
    w->xmtrx[15] = 1.0f;
    /* slot 0 of the matrix stack (the capture's base) while nothing is pushed */
    if (wp_rd32(w, WP_ADDR_MTX_DEPTH) == 0) {
        base = wp_rd32(w, WP_ADDR_MTX_BASE);
        if (wp_arena(base) && wp_arena(base + 63u)) {
            for (i = 0; i < 16; i++) {
                wp_wrf(w, base + 4u * i, w->xmtrx[i]);
            }
        }
    }
    /* the camera taskwk's position: the eye (a character world's camera adapter holds its virtual eye there) */
    cam = wp_rd32(w, SA1G_ADDR_CAMERA_TWP);
    if (wp_arena(cam)) {
        for (i = 0; i < 3; i++) {
            wp_wrf(w, cam + 0x20u + 4u * i, e[i]);
        }
    }
}

/* the executor override of the four type execs (inside the world): the copy's view, then its exec */
static void wp_exec(sa1g_world *w, uint32_t tp, void *user)
{
    wp_world *ww = (wp_world *)user;
    uint32_t ocp = wp_rd32(w, tp + 0x1Cu), exec = wp_rd32(w, tp + 0x10u);
    if (ocp >= wp_cond(0) && ocp < wp_cond(SA1PV_SLOTS) && ((ocp - wp_cond(0)) & 0xFu) == 0) {
        wp_view(ww, (ocp - wp_cond(0)) >> 4);
    }
    if (exec != 0) {
        ((void (*)(void *))sa1_fn(SA1_GFN_C(exec), 0u))(sa1_decode(tp));
    }
}

/* ---- the showcase world (task 2) --------------------------------------------------------------------------------- */
/* the kind's pristine image: the character worlds' image cache (composed here when nobody did yet, installed under the
 * table lock the way the bridge's wc_image / sa1_character_prepare install one) */
static sa1_status wp_image(sa1g_wctx *gc, int32_t kind, const sa1g_image **out)
{
    sa1g_image img;
    sa1_status st;
    int have, dup;
    *out = NULL;
    sa1g_table_lock();
    have = gc->composed[kind];
    sa1g_table_unlock();
    if (!have) {
        st = sa1g_image_compose(gc->ctx, gc->gdi, kind, 0, &img);
        if (st != SA1_OK) {
            return st;
        }
        sa1g_table_lock();
        dup = gc->composed[kind];
        if (!dup) {
            gc->images[kind] = img;
            gc->composed[kind] = 1;
        }
        sa1g_table_unlock();
        if (dup) {
            sa1g_image_free(&img);
        }
    }
    *out = &gc->images[kind];
    return SA1_OK;
}

static void wp_make_state(sa1g_world *w, void *user)
{
    wp_world *ww = (wp_world *)user;
    wp_state *S = NULL;
    uint32_t g = sa1g_world_ext_alloc(w, sizeof(wp_state), 16, (void **)&S);
    if (g == 0 || S == NULL) {
        sa1g_fatal(0, "preview: no room for the showcase's block in segment 0x18");
    }
    memset(S, 0, sizeof *S);
    S->magic = WP_MAGIC;
    S->self = g;
    ww->state = g;
}

sa1_status sa1wp_create(sa1_context *ctx, int32_t kind, struct sa1g_world **out)
{
    struct sa1w_ctx *wc = NULL;
    sa1g_wctx *gc;
    const sa1g_image *img = NULL;
    sa1g_boot_cfg cfg;
    wp_world *ww;
    sa1_status st;
    uint32_t k;
    static const uint32_t execs[4] = { 0x8C0869E8u, 0x8C087B80u, 0x8C05D10Cu, 0x8C05D9F6u };
    *out = NULL;
    if (!sa1g_game_ready()) {
        return sa1_fail(ctx, SA1_ERR_NOT_IMPLEMENTED, "previews need the lowered game code (not in this build)");
    }
    st = sa1w_ctx_get(ctx, &wc);
    if (st != SA1_OK || wc == NULL) {
        return sa1_fail(ctx, st != SA1_OK ? st : SA1_ERR_NOT_LOADED, "previews: no world state for this context (%s)",
                        sa1g_status_string(st));
    }
    gc = sa1w_ctx_wc(wc);
    st = wp_image(gc, kind, &img);
    if (st != SA1_OK) {
        return sa1_fail(ctx, st, "previews: the world image of %s: %s", sa1_character_name(kind) ? sa1_character_name(kind)
                        : "this kind", sa1g_status_string(st));
    }
    ww = (wp_world *)sa1_mem_alloc(ctx, sizeof *ww);
    if (ww == NULL) {
        return SA1_ERR_OUT_OF_MEMORY;
    }
    if (sa1g_world_create(&ww->w, &gc->shared) != 0) {
        sa1_mem_free(ctx, ww);
        return sa1_fail(ctx, SA1_ERR_OUT_OF_MEMORY, "previews: cannot reserve the showcase's 16 MB arena");
    }
    ww->ctx = ctx;
    ww->w.fresh = 1;                    /* nothing wrote the arena yet: apply_image need not zero it */
    st = sa1g_world_apply_image(&ww->w, img);
    if (st == SA1_OK) {
        ww->w.kind = kind;
        sa1g_boot_defaults(&cfg);
        cfg.chr = kind;
        cfg.level = 1;
        cfg.act = 0;
        cfg.flags = SA1G_BOOT_NO_PLAYER;
        cfg.land_mode = SA1G_LAND_EMPTY;
        st = sa1g_world_boot(&ww->w, &cfg);
    }
    if (st == SA1_OK) {
        st = sa1g_world_call(&ww->w, wp_make_state, ww);
    }
    for (k = 0; st == SA1_OK && k < 4; k++) {
        if (sa1wp_native(execs[k]) && sa1g_world_xhook(&ww->w, execs[k], wp_exec, ww) != 0) {
            st = SA1_ERR_LIMIT;
        }
    }
    /* round 0.9.5: the BOARD preview's exec (sa1_world_board.c, LIBSA1_BOARD) under its copy's view too */
    if (st == SA1_OK && sa1g_world_xhook(&ww->w, SA1WB_PREVIEW_CODE, wp_exec, ww) != 0) {
        st = SA1_ERR_LIMIT;
    }
    if (st != SA1_OK) {
        sa1_fail(ctx, st, "previews: the showcase of %s failed: %s", sa1_character_name(kind) ? sa1_character_name(kind)
                 : "this kind", ww->w.dead ? ww->w.fault.msg : sa1g_status_string(st));
        sa1g_world_destroy(&ww->w);
        sa1_mem_free(ctx, ww);
        return st;
    }
    for (k = 0; k < SA1PV_SLOTS; k++) {
        ww->dist[k] = SA1PV_VIEW_MIN;
    }
    *out = &ww->w;
    return SA1_OK;
}

void sa1wp_destroy(sa1_context *ctx, struct sa1g_world *w)
{
    if (w == NULL) {
        return;
    }
    sa1g_world_destroy(w);
    sa1_mem_free(ctx, wp_of(w));
}

void sa1wp_set_view(struct sa1g_world *w, uint32_t slot, float dist)
{
    if (w != NULL && slot < SA1PV_SLOTS) {
        wp_of(w)->dist[slot] = dist;
    }
}

const char *sa1wp_fault(const struct sa1g_world *w)
{
    return w != NULL && w->dead ? w->fault.msg : "";
}

/* ---- the copies (task 3) ------------------------------------------------------------------------------------------- */
typedef struct wp_add_job {
    wp_world *ww;
    const sa1pv_wcopy *c;
    uint32_t tp;
} wp_add_job;

static void wp_put16(uint8_t *p, uint32_t v)
{
    uint16_t x = (uint16_t)v;
    memcpy(p, &x, 2);
}

static void wp_add_cb(sa1g_world *w, void *user)
{
    wp_add_job *j = (wp_add_job *)user;
    const sa1pv_wcopy *c = j->c;
    const sa1set_type *t = sa1set_type_of(c->type);
    wp_state *S = wp_state_of(j->ww);
    uint32_t tp, twp, cond = wp_cond(c->slot), k, zero = 1;
    float pos[3];
    int32_t ang[3] = { 0, 0, 0 };
    uint8_t *se;
    j->tp = 0;
    if (c->type == SA1_PREVIEW_BOARD && S != NULL) {
        /* round 0.9.5 (SA1_PREVIEW_BOARD): libsa1's board task at the slot - the board's object and texlist in its taskwk
           (timer.ptr / value.ptr, as the board tasks' inits set them), drawn by the LIBSA1_BOARD exec */
        uint32_t obj = sa1wb_board_object(w->kind), tl = sa1wb_board_texlist(w->kind, (uint32_t)c->params[0]);
        sa1pv_slot_pos(c->slot, pos);
        if (obj == 0 || tl == 0) {
            return;
        }
        tp = sa1g_create_task(3, 2, SA1WB_PREVIEW_CODE);
        if (!wp_arena(tp)) {
            return;
        }
        twp = wp_rd32(w, tp + 0x20u);
        if (wp_arena(twp)) {
            wp_wr32(w, twp + 0x0Cu, obj);
            wp_wr32(w, twp + 0x10u, tl);
            for (k = 0; k < 3; k++) {
                wp_wrf(w, twp + 0x20u + 4u * k, pos[k]);
                wp_wrf(w, twp + 0x2Cu + 4u * k, 1.0f);
            }
        }
        se = S->sets[c->slot];
        memset(se, 0, WP_SETENTRY);
        memcpy(se + 8, pos, 12);
        wp_wr32(w, cond + 0u, 1u | ((c->cond & 0xFFFFu) << 16));
        wp_wr32(w, cond + 4u, tp);
        wp_wr32(w, cond + 8u, wp_guest(S, se));
        wp_wr32(w, cond + 12u, 0u);
        wp_wr32(w, tp + 0x1Cu, cond);
        j->tp = tp;
        return;
    }
    if (t == NULL || S == NULL) {
        return;
    }
    for (k = 0; k < 9; k++) {
        zero &= c->rotation[k] == 0.0f;
    }
    if (!zero && c->type == SA1_OBJTYPE_O_GRING) {
        sa1w_set_angles(&j->ww->ctx->coords, c->rotation, SA1_SET_ROT_ZXY, ang);   /* the world's FP mode, as a record */
    }
    sa1pv_slot_pos(c->slot, pos);
    tp = sa1g_create_task(t->im, t->level, t->exec);
    if (!wp_arena(tp)) {
        return;
    }
    twp = wp_rd32(w, tp + 0x20u);
    if (wp_arena(twp)) {
        for (k = 0; k < 3; k++) {
            wp_wr32(w, twp + 0x14u + 4u * k, (uint32_t)ang[k]);
            wp_wrf(w, twp + 0x20u + 4u * k, pos[k]);
            wp_wrf(w, twp + 0x2Cu + 4u * k, c->params[k]);
        }
    }
    se = S->sets[c->slot];
    memset(se, 0, WP_SETENTRY);
    wp_put16(se + 0, c->type - 1u);             /* usID: the type's index in a generated list (sa1_world_set.c) */
    wp_put16(se + 2, (uint32_t)ang[0]);
    wp_put16(se + 4, (uint32_t)ang[1]);
    wp_put16(se + 6, (uint32_t)ang[2]);
    memcpy(se + 8, pos, 12);
    memcpy(se + 20, c->params, 12);
    wp_wr32(w, cond + 0u, 1u | ((c->bits & 0xFFu) << 8) | ((c->cond & 0xFFFFu) << 16));  /* scCount, scUserFlag, ss */
    wp_wr32(w, cond + 4u, tp);                                                           /* pTask */
    wp_wr32(w, cond + 8u, wp_guest(S, se));                                              /* pObjEditEntry */
    wp_wr32(w, cond + 12u, 0u);                                                          /* unionStatus: no range-out */
    wp_wr32(w, tp + 0x1Cu, cond);                                                        /* tp->ocp */
    j->tp = tp;
}

sa1_status sa1wp_add(struct sa1g_world *w, const sa1pv_wcopy *c, uint32_t *tp)
{
    wp_add_job j;
    sa1_status st;
    *tp = 0;
    if (w == NULL || c == NULL || c->slot >= SA1PV_SLOTS ||
        (sa1set_type_of(c->type) == NULL && c->type != SA1_PREVIEW_BOARD)) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    j.ww = wp_of(w);
    j.c = c;
    j.tp = 0;
    st = sa1g_world_call(w, wp_add_cb, &j);
    if (st != SA1_OK) {
        return st;
    }
    *tp = j.tp;
    return j.tp != 0 ? SA1_OK : SA1_ERR_LIMIT;
}

/* the task that points at slot k's condition now and is not ending (as sa1_world_set.c's st_task) */
static uint32_t wp_task(const sa1g_world *w, uint32_t slot, int *ending)
{
    uint32_t c = wp_cond(slot), tp = wp_rd32(w, c + 4u), ex;
    if (ending != NULL) {
        *ending = 0;
    }
    if (!wp_arena(tp) || wp_rd32(w, tp + 0x1Cu) != c) {
        return 0;
    }
    ex = wp_rd32(w, tp + 0x10u);
    if (ex == 0) {
        return 0;
    }
    if (ending != NULL) {
        *ending = wp_same(ex, WP_EXEC_DESTROY);
    }
    return tp;
}

typedef struct wp_free_job {
    uint32_t tp;
} wp_free_job;

static void wp_free_cb(sa1g_world *w, void *user)
{
    (void)w;
    ((void (*)(void *))sa1_fn(SA1_GFN_C(WP_FN_FREETASK), 0u))(sa1_decode(((wp_free_job *)user)->tp));
}

sa1_status sa1wp_remove(struct sa1g_world *w, uint32_t slot, uint32_t tp)
{
    wp_free_job j;
    int ending = 0;
    uint32_t cur;
    if (w == NULL || slot >= SA1PV_SLOTS) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    cur = wp_task(w, slot, &ending);
    if (cur == 0 || cur != tp || ending) {
        return SA1_OK;                  /* the task ended already (or carries DestroyTask) */
    }
    j.tp = tp;
    return sa1g_world_call(w, wp_free_cb, &j);
}

int sa1wp_slot_busy(const struct sa1g_world *w, uint32_t slot)
{
    int ending = 0;
    return w != NULL && slot < SA1PV_SLOTS && wp_task(w, slot, &ending) != 0;
}

void sa1wp_release(struct sa1g_world *w, uint32_t slot)
{
    wp_state *S;
    uint32_t c = wp_cond(slot), i;
    if (w == NULL || slot >= SA1PV_SLOTS || w->dead) {
        return;
    }
    for (i = 0; i < 16u; i += 4u) {
        wp_wr32(w, c + i, 0u);
    }
    S = wp_state_of(wp_of(w));
    if (S != NULL) {
        memset(S->sets[slot], 0, WP_SETENTRY);
    }
}

/* ---- the pair -------------------------------------------------------------------------------------------------------- */
sa1_status sa1wp_pair(struct sa1g_world *w)
{
    sa1_event ev[64];
    sa1_status st;
    if (w == NULL) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    st = sa1g_world_pair(w);
    /* nothing of a showcase leaves it: SOUND, VOICE, CAPACITY, FAULT ... are dropped (the fault is the status) */
    while (sa1g_world_poll_events(w, ev, 64) == 64) {
    }
    return st;
}

/* ---- the heap (the free list of 8C098998's allocator; as sa1_world_set.c's st_heap_largest) ------------------------- */
static void wp_heap(const sa1g_world *w, uint32_t *largest, uint32_t *total)
{
    uint32_t blk = wp_rd32(w, WP_ADDR_HEAP_FREE), l = 0, t = 0, n = 0;
    while (wp_arena(blk) && n < 0x10000u) {
        uint32_t end = wp_rd32(w, blk + 8u), len;
        if (end == 0 || !wp_arena(end) || (end & 0xFFFFFFu) <= (blk & 0xFFFFFFu)) {
            break;
        }
        len = (end & 0xFFFFFFu) - (blk & 0xFFFFFFu);
        l = len > l ? len : l;
        t += len;
        blk = wp_rd32(w, blk);
        n++;
    }
    *largest = l;
    *total = t;
}

uint32_t sa1wp_heap_largest(const struct sa1g_world *w)
{
    uint32_t l = 0, t = 0;
    if (w != NULL && !w->dead) {
        wp_heap(w, &l, &t);
    }
    return l;
}

/* ---- the ring places (task 6) -------------------------------------------------------------------------------------- */
typedef struct wp_places_job {
    wp_world *ww;
    const float *params, *rotation;
    float *places;
    uint32_t *idx, n;
} wp_places_job;

static void wp_places_cb(sa1g_world *w, void *user)
{
    wp_places_job *j = (wp_places_job *)user;
    wp_state *S = wp_state_of(j->ww);
    int32_t ang[3] = { 0, 0, 0 }, n, i, circle;
    uint32_t k, zero = 1;
    void (*fn)(void *, void *, int32_t);
    (void)w;
    j->n = 0;
    if (S == NULL) {
        return;
    }
    for (k = 0; k < 9; k++) {
        zero &= j->rotation[k] == 0.0f;
    }
    if (!zero) {
        sa1w_set_angles(&j->ww->ctx->coords, j->rotation, SA1_SET_ROT_ZXY, ang);
    }
    memset(S->twp, 0, sizeof S->twp);
    for (k = 0; k < 3; k++) {
        memcpy(S->twp + 0x14u + 4u * k, &ang[k], 4);    /* taskwk.ang (pos stays 0) */
        memcpy(S->twp + 0x2Cu + 4u * k, &j->params[k], 4);
    }
    n = (int32_t)j->params[0];
    circle = j->params[2] == 1.0f;
    fn = (void (*)(void *, void *, int32_t))sa1_fn(SA1_GFN_C(circle ? WP_FN_CIRCLE_PLACE : WP_FN_LINE_PLACE), 0u);
    /* objexec_O_GRING mode 0: i = 0 .. (Sint32)scl.x; a circle skips i = 0 */
    for (i = 0; i < n + 1 && j->n < 16u; i++) {
        if (circle && i == 0) {
            continue;
        }
        memset(S->place, 0, sizeof S->place);
        fn(S->place, S->twp, i);
        memcpy(&j->places[3u * j->n], S->place, 12);
        j->idx[j->n] = (uint32_t)i;
        j->n++;
    }
}

sa1_status sa1wp_ring_places(struct sa1g_world *w, const float params[3], const float rotation[9], float places[48],
                             uint32_t idx[16], uint32_t *n)
{
    wp_places_job j;
    sa1_status st;
    *n = 0;
    if (w == NULL) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    if (!sa1wp_native(WP_FN_LINE_PLACE) || !sa1wp_native(WP_FN_CIRCLE_PLACE)) {
        return SA1_ERR_NOT_IMPLEMENTED;
    }
    j.ww = wp_of(w);
    j.params = params;
    j.rotation = rotation;
    j.places = places;
    j.idx = idx;
    j.n = 0;
    st = sa1g_world_call(w, wp_places_cb, &j);
    *n = j.n;
    return st;
}

/* ---- tests --------------------------------------------------------------------------------------------------------- */
void sa1wp_test_counts(const struct sa1g_world *w, sa1pv_test_info *out)
{
    static const uint32_t execs[4] = { 0x8C0869E8u, 0x8C087B80u, 0x8C05D10Cu, 0x8C05D9F6u };
    uint32_t tp, guard, lvl, k;
    if (w == NULL || w->dead) {
        return;
    }
    wp_heap(w, &out->heap_largest, &out->heap_free);
    out->free_tasks = 0;
    tp = wp_rd32(w, WP_ADDR_TASK_FREE);
    for (guard = 0; wp_arena(tp) && guard < 0x4000u; guard++) {
        out->free_tasks++;
        tp = wp_rd32(w, tp);
    }
    out->live_tasks = 0;
    for (lvl = 0; lvl < 8u; lvl++) {
        tp = wp_rd32(w, SA1G_ADDR_TASK_LISTS + 4u * lvl);
        for (guard = 0; wp_arena(tp) && guard < 0x2000u; guard++) {
            uint32_t ex = wp_rd32(w, tp + 0x10u);
            for (k = 0; k < 4; k++) {
                if (wp_same(ex, execs[k])) {
                    out->live_tasks++;
                }
            }
            tp = wp_rd32(w, tp);
        }
    }
}

sa1_status sa1wp_test_break(struct sa1g_world *w, uint32_t tp)
{
    if (w == NULL || !wp_arena(tp)) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    wp_wr32(w, tp + 0x10u, WP_BAD_EXEC);
    return SA1_OK;
}
