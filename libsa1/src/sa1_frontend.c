/*
 * sa1_frontend.c - the game's own front end, the API (SA1_API_FRONTEND, round 0.9.8; include/libsa1.h "the game's own front end"; sadc
 * docs/research/gsa1_098_plan.md 5.3, 6.5, charsel098.md 5; src/sa1_frontend.h has the overview). Asset layer: plain C, no world
 * headers; the world lies behind src/world/sa1_world_frontend.c.
 *
 * sa1_frontend_open: one per context. The world, the archives and their texlists, the capture of its draws (sa1_capture.h: the
 * window lies around every frame; the menu's frame counter stands still, so its word in the capture block is reset after each one),
 * and the game's 131 frames to the first drawn frame, run here. Events of those frames stay queued.
 *
 * sa1_frontend_tick: whole frames of 1/60 s for dt, at most 8. What the host presses between two frames reaches the game's next one,
 * also when it was let go again (the game reads edges). The dialog skip's press stays withheld until it is released.
 *
 * What a tick reads from the game afterwards: the screen's proc and slot, the master's mode and result. Those become the state, and
 * its changes the events SA1_EVENT_FRONTEND; a result or a screen change ends the screen as the game's main loop would: later ticks
 * run no frames (frames_run 0), and the last draws stay. The results 103 and 104 of the choices Ending and Instruction (dialog only)
 * are the same DONE for a host (charsel098.md 5.2).
 *
 * Output: sa1_frontend_screen (sa1_screen.c's source for the capture kinds 14, 15), sa1_frontend_render (the selected slot's main only:
 * sa1_capture_emit_opts.only_exec; the game's display walk draws everything a second time with exec 0, and Amy and E-102 draw at their
 * parked place every frame), sa1_frontend_view.
 */
#include "sa1_internal.h"
#include "sa1_capture.h"
#include "sa1_frontend.h"
#include "sa1_skin.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* The gate per feature (gsa1_098_plan.md 3.2): 098-lib-frontend sets it to 1 right before its final gate */
#define SA1FE_BUILT 1

#define FE_OPEN_FRAMES   131        /* the screen's creation: first drawn frame = 130, the music at 131 */
#define FE_TICK_FRAMES   8u
#define FE_QUEUE         512u
#define FE_SE_CONFIRM    22u        /* the game's confirm tone: SE 21 and SE 2 belong to the dialog */
#define FE_SCREEN_DIST   457.059f   /* 8C640500(0x31C7) */
#define FE_MASTER_SELECT 1          /* master mode: the select screen */

struct sa1_frontend {
    sa1_context *ctx;
    uint32_t flags;
#if !defined(SA1_NO_WORLD)
    struct sa1g_world *w;
#endif
    sa1_capture *cap;
    uint32_t cap_block, cap_total;
    int32_t drew;                   /* the last frame drew */
    struct sa1s_fe *screen;
    /* the host's pad, SA1_FRONTEND_B_* */
    double acc;
    uint32_t held, taps, withheld;
    int16_t x1, y1;
    /* the state */
    int32_t frame, slot, slots, phase, pose_frames, kind, proc;
    int32_t picked, ended, run_on;
    uint32_t faults;
    char fault[256];
    /* the events */
    sa1_event *q;
    uint32_t q_head, q_count, q_lost, ev_other;
    /* the render output of the last frame */
    sa1_meshbuf mb;
    sa1_render render;
    float *pos, *nrm;
    uint32_t cap_tris;
    int32_t render_frame, render_kind;
    uint32_t render_gen;
    /* round 0.9.9f (sa1_frontend_set_skin; NULL: Sonic): slot 0 draws the skin */
    struct sa1k_char *skin;
};

#if !defined(SA1_NO_WORLD)
/* ---- the events ----------------------------------------------------------------------------------------------------------- */
static void fe_push(sa1_frontend *fe, const sa1_event *e)
{
    if (fe->q_count == FE_QUEUE) {
        fe->q_head = (fe->q_head + 1u) % FE_QUEUE;
        fe->q_count--;
        fe->q_lost++;
    }
    fe->q[(fe->q_head + fe->q_count) % FE_QUEUE] = *e;
    fe->q_count++;
}

static void fe_push_own(sa1_frontend *fe, int32_t frame, uint32_t type, uint32_t id, uint32_t arg)
{
    sa1_event e;
    memset(&e, 0, sizeof e);
    e.type = type;
    e.id = id;
    e.arg = arg;
    e.step = (uint32_t)frame;
    fe_push(fe, &e);
}

/* ---- one frame -------------------------------------------------------------------------------------------------------------- */
static uint32_t fe_katana(uint32_t b)
{
    uint32_t k = 0;
    k |= (b & SA1_FRONTEND_B_LEFT) ? SA1FW_PAD_LEFT : 0u;
    k |= (b & SA1_FRONTEND_B_RIGHT) ? SA1FW_PAD_RIGHT : 0u;
    k |= (b & SA1_FRONTEND_B_UP) ? SA1FW_PAD_UP : 0u;
    k |= (b & SA1_FRONTEND_B_DOWN) ? SA1FW_PAD_DOWN : 0u;
    k |= (b & SA1_FRONTEND_B_A) ? SA1FW_PAD_A : 0u;
    k |= (b & SA1_FRONTEND_B_B) ? SA1FW_PAD_B : 0u;
    k |= (b & SA1_FRONTEND_B_START) ? SA1FW_PAD_START : 0u;
    return k;
}

static int32_t fe_phase(const sa1_frontend *fe, const sa1fw_info *inf)
{
    if (fe->ended) {
        return 4;
    }
    switch (inf->proc) {
    case 0:
        return 0;
    case 1:
    case 2:
        return 1;
    case 3:
    case 9:
        return 3;
    case 4:
    case 5:
    case 6:
    case 7:
        return 2;
    case 8:
        return (fe->flags & SA1_FRONTEND_SHOW_DIALOG) ? 2 : 3;
    default:
        return 4;
    }
}

/* the state and its events after a frame */
static void fe_after_frame(sa1_frontend *fe, const sa1fw_info *inf, int32_t f)
{
    if (inf->select_alive) {
        if (inf->slot != fe->slot && !fe->ended) {
            fe->slot = inf->slot;
            fe->kind = sa1fw_slot_kind(fe->w, fe->slot);
            fe_push_own(fe, f, SA1_EVENT_FRONTEND, (uint32_t)fe->slot, SA1_FRONTEND_EV_SELECT);
        }
        if (inf->proc == 9 && fe->proc != 9 && !fe->picked) {
            fe->picked = 1;
            fe_push_own(fe, f, SA1_EVENT_FRONTEND, (uint32_t)fe->slot, SA1_FRONTEND_EV_PICK);
        }
        fe->proc = inf->proc;
        fe->pose_frames = inf->proc == 9 ? inf->timer : fe->pose_frames;
    }
    if (!fe->ended && inf->master_alive) {
        if (inf->result != 0) {
            fe->ended = 1;
            fe_push_own(fe, f, SA1_EVENT_FRONTEND, (uint32_t)fe->slot, SA1_FRONTEND_EV_DONE);
        } else if (inf->mode != FE_MASTER_SELECT && !fe->picked) {
            fe->ended = 1;
            fe_push_own(fe, f, SA1_EVENT_FRONTEND, (uint32_t)-1, SA1_FRONTEND_EV_CANCEL);
        }
    }
    fe->phase = fe_phase(fe, inf);
}

static void fe_dead(sa1_frontend *fe, sa1_status st)
{
    fe->faults++;
    snprintf(fe->fault, sizeof fe->fault, "%s", sa1fw_fault(fe->w));
    if (fe->fault[0] == 0) {
        snprintf(fe->fault, sizeof fe->fault, "%s", sa1_status_string(st));
    }
}

static sa1_status fe_frame(sa1_frontend *fe, int quiet)
{
    sa1fw_pad pad;
    sa1fw_info inf;
    sa1_capture_stats cst;
    sa1_event ev[64];
    uint32_t bits = fe->held | fe->taps, presses = fe->taps, n, k;
    int32_t f = fe->frame;
    sa1_status st;
    fe->taps = 0;
    fe->withheld &= bits;
    memset(&pad, 0, sizeof pad);
    memset(&inf, 0, sizeof inf);
    pad.on = fe_katana(bits & ~fe->withheld);
    pad.x1 = fe->x1;
    pad.y1 = fe->y1;
    pad.confirm = (presses & ~fe->withheld & (SA1_FRONTEND_B_A | SA1_FRONTEND_B_START)) != 0;
    sa1_capture_open(fe->cap);
    st = sa1fw_frame(fe->w, &pad, &inf);
    sa1_capture_close(fe->cap);
    if (st != SA1_OK) {
        fe_dead(fe, st);
        return st;
    }
    sa1_capture_get_stats(fe->cap, &cst);
    fe->drew = cst.total != fe->cap_total;
    fe->cap_total = cst.total;
    fe->cap_block = cst.block;
    sa1fw_reset_cap_frame(fe->w, cst.block);
    fe->frame = f + 1;
    /* the world's events, in their order, with the frame they came in */
    while ((n = sa1fw_poll(fe->w, ev, 64u)) > 0) {
        for (k = 0; k < n; k++) {
            if (ev[k].type == SA1_EVENT_SOUND || ev[k].type == SA1_EVENT_VOICE || ev[k].type == SA1_EVENT_JINGLE) {
                ev[k].character = 0;
                ev[k].step = (uint32_t)f;
                fe_push(fe, &ev[k]);
            } else {
                fe->ev_other++;
            }
        }
        if (n < 64u) {
            break;
        }
    }
    fe->slots = inf.slots;
    if (inf.skipped) {
        fe->withheld |= bits & (SA1_FRONTEND_B_A | SA1_FRONTEND_B_START | SA1_FRONTEND_B_B);
        fe_push_own(fe, f, SA1_EVENT_SOUND, FE_SE_CONFIRM, 0u);
    }
    if (!quiet) {
        fe_after_frame(fe, &inf, f);
    }
    return SA1_OK;
}

static void fe_free(sa1_frontend *fe)
{
    sa1_context *ctx = fe->ctx;
    sa1_meshbuf_free_exec(ctx, &fe->mb);
    sa1_meshbuf_free(ctx, &fe->mb);
    sa1_mem_free(ctx, fe->pos);
    sa1_mem_free(ctx, fe->nrm);
    sa1_screen_fe_free(ctx, fe->screen);
    sa1_capture_free(ctx, fe->cap);
    sa1fw_destroy(ctx, fe->w);
    sa1_mem_free(ctx, fe->q);
    sa1k_char_free(ctx, fe->skin);
    sa1_mem_free(ctx, fe);
}
#endif /* !SA1_NO_WORLD */

/* ---- the API ------------------------------------------------------------------------------------------------------------------ */
sa1_status sa1_frontend_open(sa1_context *ctx, const sa1_frontend_desc *d, sa1_frontend **out)
{
    sa1_frontend_desc desc;
#if !defined(SA1_NO_WORLD) && (SA1FE_BUILT || !defined(__OPTIMIZE__))
    sa1_frontend *fe;
    sa1fw_info inf;
    sa1_status st;
    int32_t i;
#endif
    if (out != NULL) {
        *out = NULL;
    }
    if (ctx == NULL) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    if (d == NULL || d->struct_size < offsetof(sa1_frontend_desc, open_slots) + sizeof d->open_slots || out == NULL) {
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_frontend_open: NULL or struct_size too small");
    }
    memset(&desc, 0, sizeof desc);
    memcpy(&desc, d, d->struct_size < sizeof desc ? d->struct_size : sizeof desc);
    desc.struct_size = sizeof desc;
    if ((desc.flags & ~SA1_FRONTEND_SHOW_DIALOG) != 0) {
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_frontend_open: unknown flags 0x%X", (unsigned)desc.flags);
    }
    if (desc.language < 0 || desc.language > 4) {
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_frontend_open: language %d is not 0..4", (int)desc.language);
    }
    if (desc.slot < 0 || (uint32_t)desc.slot >= SA1_FRONTEND_SLOTS) {
        return sa1_fail(ctx, SA1_ERR_INVALID_ARGUMENT, "sa1_frontend_open: slot %d is not 0..6", (int)desc.slot);
    }
    if (desc.slot == 6 && !(desc.open_slots & (1u << 6))) {
        desc.slot = 0;              /* a ring of 6 has no slot 6 */
    }
#if defined(SA1_NO_WORLD)
    return sa1_fail(ctx, SA1_ERR_NOT_IMPLEMENTED, "the front end needs the world core (not in this build)");
#elif defined(__OPTIMIZE__) && !SA1FE_BUILT
    return sa1_refuse(ctx, SA1_ERR_UNSUPPORTED, SA1_LOG_DEBUG, "the front end is not built yet");
#else
    if (!ctx->disc_loaded) {
        return sa1_fail(ctx, SA1_ERR_NOT_LOADED, "sa1_frontend_open: no disc is loaded");
    }
    if (ctx->frontend_open != NULL) {
        return sa1_fail(ctx, SA1_ERR_LIMIT, "sa1_frontend_open: one front end per context is open already");
    }
    fe = (sa1_frontend *)sa1_mem_alloc(ctx, sizeof *fe);
    if (fe == NULL) {
        return SA1_ERR_OUT_OF_MEMORY;
    }
    fe->ctx = ctx;
    fe->flags = desc.flags;
    fe->q = (sa1_event *)sa1_mem_array(ctx, FE_QUEUE, sizeof(sa1_event));
    fe->slot = -1;
    fe->render_frame = -1;
    st = fe->q != NULL ? sa1fw_create(ctx, &desc, &fe->w) : SA1_ERR_OUT_OF_MEMORY;
    if (st == SA1_OK) {
        st = sa1_capture_attach(ctx, fe->w, &fe->cap);
        if (st != SA1_OK) {
            st = sa1_fail(ctx, st == SA1_ERR_NOT_FOUND ? SA1_ERR_NOT_IMPLEMENTED : st,
                          "the front end: its world has no display capture block");
        }
    }
    if (st == SA1_OK) {
        sa1_capture_set_archives(fe->cap, &ctx->frontend_set);
        st = sa1_screen_fe_create(ctx, &ctx->frontend_set, &fe->screen);
    }
    if (st == SA1_OK && (sa1_meshbuf_reserve(ctx, &fe->mb, 16384u, 16384u, 1024u) != SA1_OK ||
                         sa1_meshbuf_reserve_extra(ctx, &fe->mb, 1024u, 256u, 256u) != SA1_OK ||
                         sa1_meshbuf_reserve_scratch(ctx, &fe->mb, 4096u, 2048u) != SA1_OK)) {
        st = SA1_ERR_OUT_OF_MEMORY;
    }
    if (st != SA1_OK) {
        fe_free(fe);
        return st;
    }
    for (i = 0; i < FE_OPEN_FRAMES; i++) {
        st = fe_frame(fe, 1);
        if (st != SA1_OK) {
            st = sa1_fail(ctx, SA1_ERR_GAME_FAULT, "the front end faulted in the screen's creation (frame %d): %s", (int)i, fe->fault);
            fe_free(fe);
            return st;
        }
    }
    /* the state of the first drawn frame */
    memset(&inf, 0, sizeof inf);
    sa1fw_read(fe->w, &inf);
    fe->slot = inf.select_alive ? inf.slot : desc.slot;
    fe->kind = sa1fw_slot_kind(fe->w, fe->slot);
    fe->proc = inf.select_alive ? inf.proc : 0;
    ctx->frontend_open = fe;
    *out = fe;
    return SA1_OK;
#endif
}

void sa1_frontend_close(sa1_frontend *fe)
{
#if !defined(SA1_NO_WORLD)
    if (fe != NULL) {
        if (fe->ctx->frontend_open == fe) {
            fe->ctx->frontend_open = NULL;
        }
        fe_free(fe);
    }
#else
    (void)fe;
#endif
}

sa1_status sa1_frontend_tick(sa1_frontend *fe, float dt, const sa1_frontend_input *in, uint32_t *frames_run)
{
#if !defined(SA1_NO_WORLD)
    sa1_frontend_input v;
    uint32_t n, i;
    double whole;
    if (frames_run != NULL) {
        *frames_run = 0;
    }
    if (fe == NULL || !(dt >= 0.0f) || (in != NULL && in->struct_size < sizeof(uint32_t))) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    if (fe->faults) {
        return sa1_fail(fe->ctx, SA1_ERR_GAME_FAULT, "the front end faulted: %s", fe->fault);
    }
    memset(&v, 0, sizeof v);
    if (in != NULL) {
        memcpy(&v, in, in->struct_size < sizeof v ? in->struct_size : sizeof v);
    }
    fe->taps |= v.buttons & ~fe->held;
    fe->held = v.buttons;
    {
        /* as sa1g_stick (sa1_world_tick.c): -1..1 -> -127..127; +y forward on the host, y < 0 forward on the pad */
        float x = v.stick_x, y = -v.stick_y;
        x = x > 1.0f ? 1.0f : x < -1.0f ? -1.0f : x;
        y = y > 1.0f ? 1.0f : y < -1.0f ? -1.0f : y;
        fe->x1 = (int16_t)(x >= 0.0f ? x * 127.0f + 0.5f : x * 127.0f - 0.5f);
        fe->y1 = (int16_t)(y >= 0.0f ? y * 127.0f + 0.5f : y * 127.0f - 0.5f);
    }
    fe->acc += (double)dt;
    whole = floor(fe->acc * 60.0 + 1e-6);
    n = whole >= (double)FE_TICK_FRAMES ? FE_TICK_FRAMES : (uint32_t)whole;
    /* the rest is kept, a backlog beyond the limit is not: the game would run fast for as long as it took */
    fe->acc = whole >= (double)FE_TICK_FRAMES ? 0.0 : fe->acc - (double)n / 60.0;
    if (fe->acc < 0.0) {
        fe->acc = 0.0;
    }
    for (i = 0; i < n; i++) {
        sa1_status st;
        if (fe->ended && !fe->run_on) {
            break;
        }
        st = fe_frame(fe, 0);
        if (st != SA1_OK) {
            return sa1_fail(fe->ctx, SA1_ERR_GAME_FAULT, "the front end faulted: %s", fe->fault);
        }
        if (frames_run != NULL) {
            (*frames_run)++;
        }
    }
    return SA1_OK;
#else
    (void)dt;
    (void)in;
    if (frames_run != NULL) {
        *frames_run = 0;
    }
    return fe == NULL ? SA1_ERR_INVALID_ARGUMENT : SA1_ERR_NOT_IMPLEMENTED;
#endif
}

sa1_status sa1_frontend_state_get(const sa1_frontend *fe, sa1_frontend_state *out)
{
    sa1_frontend_state s;
    if (fe == NULL || out == NULL || out->struct_size < sizeof(uint32_t)) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    memset(&s, 0, sizeof s);
    s.struct_size = sizeof s;
    s.slot = fe->slot;
    s.slots = fe->slots;
    s.phase = fe->phase;
    s.frame = fe->frame;
    s.pose_frames = fe->pose_frames;
    s.kind = fe->kind;
    s.faults = fe->faults;
    sa1_copy_out(out, &s, sizeof s);
    if (fe->faults) {
        return sa1_fail(fe->ctx, SA1_ERR_GAME_FAULT, "the front end faulted: %s", fe->fault);
    }
    return SA1_OK;
}

sa1_status sa1_frontend_poll(sa1_frontend *fe, sa1_event *out, uint32_t capacity, uint32_t *count)
{
    uint32_t n = 0;
    if (count != NULL) {
        *count = 0;
    }
    if (fe == NULL || (out == NULL && capacity != 0)) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    while (n < capacity && fe->q_count != 0) {
        out[n++] = fe->q[fe->q_head];
        fe->q_head = (fe->q_head + 1u) % FE_QUEUE;
        fe->q_count--;
    }
    if (count != NULL) {
        *count = n;
    }
    if (fe->faults) {
        return sa1_fail(fe->ctx, SA1_ERR_GAME_FAULT, "the front end faulted: %s", fe->fault);
    }
    return SA1_OK;
}

sa1_status sa1_frontend_screen(sa1_frontend *fe, const sa1_screen **out)
{
    if (out != NULL) {
        *out = NULL;
    }
    if (fe == NULL || out == NULL) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    if (fe->faults) {
        return sa1_fail(fe->ctx, SA1_ERR_GAME_FAULT, "the front end faulted: %s", fe->fault);
    }
    return sa1_screen_fe_build(fe->ctx, fe->screen, fe->cap, fe->drew, out);
}

#if !defined(SA1_NO_WORLD)
static sa1_status fe_render_build(sa1_frontend *fe)
{
    sa1_context *ctx = fe->ctx;
    const sa1_charset *cs;
    sa1_capture_emit_opts o;
    sa1_geometry g;
    sa1_meshbuf *b = &fe->mb;
    sa1_status st;
    uint32_t t, k;
    fe->render_kind = sa1fw_slot_kind(fe->w, fe->slot);
    cs = fe->render_kind == SA1_KIND_FRONTEND ? &ctx->frontend_set : &ctx->chars[fe->render_kind];
    sa1_meshbuf_reset(b);
    if (fe->drew) {
        memset(&o, 0, sizeof o);
        o.texlist0 = cs->num_archives ? cs->archives[0].texlist_addr : 0u;
        o.shadow_model = sa1_capture_shadow_model(fe->cap);
        o.cs = cs;
        o.effects = 1;
        o.dedupe = 1;
        /* the game parks hidden models far away (E-102's at y = -1000000): as the screen's own bounds in charsel098.md 3.3 */
        o.clip = 1;
        o.clip_center[2] = -30.0f;
        o.clip_radius = 100.0f;
        o.only_exec = sa1fw_slot_exec(fe->w, fe->slot);
        o.skin = fe->slot == 0 ? fe->skin : NULL;     /* round 0.9.9f: nothing else moves, the game does not know */
        st = sa1_capture_emit_into(ctx, fe->cap, &o, b, &g);
        if (st != SA1_OK) {
            return st;
        }
        if (o.skin != NULL) {
            st = sa1k_append(ctx, o.skin, cs, b);
            if (st != SA1_OK) {
                return st;
            }
            sa1_meshbuf_geometry(b, &g);
        }
    }
    if (b->nt > fe->cap_tris) {
        uint32_t c = b->nt + 256u;
        float *p = (float *)sa1_mem_array(ctx, 9u * (size_t)c, sizeof(float));
        float *n = (float *)sa1_mem_array(ctx, 9u * (size_t)c, sizeof(float));
        if (p == NULL || n == NULL) {
            sa1_mem_free(ctx, p);
            sa1_mem_free(ctx, n);
            return SA1_ERR_OUT_OF_MEMORY;
        }
        sa1_mem_free(ctx, fe->pos);
        sa1_mem_free(ctx, fe->nrm);
        fe->pos = p;
        fe->nrm = n;
        fe->cap_tris = c;
    }
    for (t = 0; t < 3u * b->nt; t++) {
        uint32_t vi = b->idx[t];
        for (k = 0; k < 3u; k++) {
            fe->pos[3u * t + k] = vi < b->nv ? b->pos[3u * vi + k] : 0.0f;
            fe->nrm[3u * t + k] = vi < b->nv ? b->nrm[3u * vi + k] : 0.0f;
        }
    }
    memset(&fe->render, 0, sizeof fe->render);
    fe->render.struct_size = sizeof fe->render;
    fe->render.generation = ++fe->render_gen;
    fe->render.alpha = 1.0f;
    fe->render.num_triangles = b->nt;
    fe->render.positions = fe->pos;
    fe->render.normals = fe->nrm;
    fe->render.uvs = b->uv;
    fe->render.colors = b->col;
    fe->render.num_draws = b->ndraw;
    fe->render.draws = b->draws;
    fe->render.num_sprites = b->nspr;
    fe->render.sprites = b->spr;
    fe->render.num_line_vertices = b->nlv;
    fe->render.line_vertices = b->lv;
    fe->render_frame = fe->frame;
    return SA1_OK;
}
#endif

sa1_status sa1_frontend_set_skin(sa1_frontend *fe, const sa1_skin_desc *skin)
{
#if !defined(SA1_NO_WORLD)
    sa1_status st;
    sa1k_char *sc = NULL;
    if (fe == NULL) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    if (skin != NULL) {
        st = sa1k_load(fe->ctx, skin);
        if (st != SA1_OK) {
            return st;
        }
        st = sa1k_char_new(fe->ctx, 0u, &sc);
        if (st != SA1_OK) {
            return st;
        }
    }
    sa1k_char_free(fe->ctx, fe->skin);
    fe->skin = sc;
    fe->render_frame = -1;  /* the next sa1_frontend_render builds again */
    return SA1_OK;
#else
    (void)fe;
    (void)skin;
    return SA1_ERR_UNSUPPORTED;
#endif
}

sa1k_char *sa1fe_test_skin(const sa1_frontend *fe)
{
#if !defined(SA1_NO_WORLD)
    return fe != NULL ? fe->skin : NULL;
#else
    (void)fe;
    return NULL;
#endif
}

sa1_status sa1_frontend_render(sa1_frontend *fe, const sa1_render **out, int32_t *kind)
{
    if (out != NULL) {
        *out = NULL;
    }
    if (fe == NULL || out == NULL) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    if (fe->faults) {
        return sa1_fail(fe->ctx, SA1_ERR_GAME_FAULT, "the front end faulted: %s", fe->fault);
    }
#if !defined(SA1_NO_WORLD)
    if (fe->render_frame != fe->frame) {
        sa1_status st = fe_render_build(fe);
        if (st != SA1_OK) {
            return sa1_fail(fe->ctx, st, "sa1_frontend_render: %s", sa1_status_string(st));
        }
    }
    *out = &fe->render;
    if (kind != NULL) {
        *kind = fe->render_kind;
    }
    return SA1_OK;
#else
    (void)kind;
    return SA1_ERR_NOT_IMPLEMENTED;
#endif
}

sa1_status sa1_frontend_view(const sa1_frontend *fe, float eye[3], float forward[3], float up[3], float *fov_degrees)
{
    static const float o[3] = { 0.0f, 0.0f, 0.0f }, f[3] = { 0.0f, 0.0f, -1.0f }, u[3] = { 0.0f, 1.0f, 0.0f };
    if (fe == NULL) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    if (eye != NULL) {
        sa1_to_host_point(fe->ctx, o, eye);
    }
    if (forward != NULL) {
        sa1_to_host_dir(fe->ctx, f, forward);
    }
    if (up != NULL) {
        sa1_to_host_dir(fe->ctx, u, up);
    }
    if (fov_degrees != NULL) {
        *fov_degrees = (float)(2.0 * atan(320.0 / (double)FE_SCREEN_DIST) * (180.0 / 3.14159265358979323846));
    }
    return SA1_OK;
}

/* ---- tests -------------------------------------------------------------------------------------------------------------------- */
void sa1fe_test_get(const sa1_frontend *fe, sa1fe_test_info *out)
{
    memset(out, 0, sizeof *out);
    if (fe == NULL) {
        return;
    }
    out->screen_misses = sa1_screen_fe_misses(fe->screen);
    out->events_other = fe->ev_other;
    out->events_lost = fe->q_lost;
    out->cap_block = fe->cap_block;
    out->cap = fe->cap;
#if !defined(SA1_NO_WORLD)
    out->world = fe->w;
#endif
}

void sa1fe_test_run_on(sa1_frontend *fe, int32_t on)
{
    if (fe != NULL) {
        fe->run_on = on;
    }
}
