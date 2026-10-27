/*
 * flicker_test.c - the render root is the root of the drawn step, and libsa1 says which frames it moved with the root
 * (sadc docs/research/gsa1_vertical_flicker.md fix B with its review R.4; gsa1_lsd_lag.md review C4 / G1).
 *
 *   flicker_test [--only NAME] [--fps 60|144|100/200|60on84] [--trace NAME] GDI    (build: tests/build_flicker.py)
 *
 * The game draws a character once per pair of 60 Hz steps: the character renderer 8C037098 draws on sub step 0 only
 * (sa1_capture.h), so the body libsa1 captures stands where the character was after the FIRST step of the pair.
 * sa1_state is the state after the second step. Up to the libsa1 of G-SA1 0.5.x, sa1_render.position (the root hosts
 * hang their camera on) was the state's, one 60 Hz step ahead of the drawn body: the body sank / trailed by one step of
 * movement and caught up a pair later (steps, stairs, landings, falls, the Light Speed release). Fix B records player
 * 0's taskwk.pos / ang after sub step 0 (sa1_world_tick.c sa1g_note_drawn) and renders around that root.
 *
 * Every run: a character created in a synthetic GRID world (host space = SA1_COORDS_PRESET_SOURCE, G-SA1's: 4 HU per
 * SA1 unit, z up), ticked with a frame time sequence and rendered every frame as a host does (sa1_character_tick(dt),
 * then sa1_character_render(-1)); sa1_character_render(1.0) and the geometry give the last pair as drawn. Per pair:
 *
 *   drawn     sa1_render.position at alpha 1 equals the world's sub-step-0 root (sa1w_pstate.drawn_pos in host space)
 *             bit for bit (internal; built only against a libsa1 with SA1W_PSTATE_DRAWN);
 *   node      the drawn body's root node (node 0 of the geometry: the first model the renderer drew) against the render
 *             root at alpha 1, in the character's facing frame, in the airborne pairs of a steady pose (the same action
 *             as the pair before and its draws matched with the previous pair's: the fall pose, the jump ball): it is
 *             the render root moved up by the display's centre offset (Sonic's and E-102's displays draw the root at
 *             center_height above taskwk.pos, Tails', Knuckles' and Amy's at half the height: sa1_character.c
 *             pose_eval_current) within 0.5 HU, and it does not move against the render root by more than 0.5 HU over
 *             the pose, from 0 to 1589 HU/s. With the state's root (libsa1 before fix B, emulated from sa1_state in the
 *             same run) the node trails it by one step of movement (a fall: +23.8 .. +48.1 HU against +21.6). On the
 *             ground the root node's own animation (the run's bob, 4 HU) hides a 0.5 HU test; there the per-frame
 *             change below checks the same thing;
 *   lead      the root node ahead of the render root along the horizontal velocity, against standing, the median of
 *             the pairs at 90 % of top speed and more: |lead| < 2 HU (with the state's root: 17 HU at 1043 HU/s,
 *             40 HU after a spin dash).
 *
 * Per frame (body = the centroid of the body draws, root = sa1_render.position at the frame's alpha, as G-SA1 uses
 * them for the drawn body and the camera):
 *
 *   change    of body - root between two frames of one pair along z: under 1.5 HU at 144 fps, 2 HU at 60 fps and
 *             100/200 fps, 2.9 HU at 60 fps on an 84 Hz display (frames of 1 and 2 / 84 s), for the fall and the
 *             stairs (before: 4.7 / 11.2 and 6.4 / 15.4 HU);
 *   release   the Light Speed Dash released after a full charge (Sonic with the Light Speed Shoes, the Crystal Ring and
 *             the Ancient Light, action held 1.0 - 5.0 s): from 4.9 to 5.4 s every frame's change of body - root
 *             along the path is at most 4 HU (before: the body fell 31.5 + 31.2 HU behind in two frames at 60 fps,
 *             44.9 HU in one on the 84 Hz display, and snapped back by 2 x 25.9 HU when the dash stopped);
 *   moved     every frame (from the first): a frame that no draw of which matched the previous pair
 *             (sa1_render.interpolated == 0) at alpha < 1 is either the last pair moved along with the interpolated root
 *             (every triangle = render(1.0)'s + position - render(1.0).position, within float rounding) and says so
 *             (SA1_RENDER_MOVED_WITH_ROOT), or the last pair as it is (no flag). Hosts must not move a flagged frame
 *             again (G-SA1's 0.4 follow_root did: cause A of the vertical flicker).
 *
 * The world's game state is not touched by any of this: fix B only reads guest memory after sub step 0 (the replay
 * test, build.py --test, checks the 10 reference traces stay bit-exact).
 */
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libsa1.h"
#include "sa1_internal.h"
#include "sa1_world.h"
#include "sa1_api_world.h"

#define EVENT_FLAGS 0x8C799920u             /* B_8C799920: the game's 512 event flags (one byte each) */

static int g_fail, g_checks;
static const char *g_trace;

#define CHECK(cond, ...)                                                       \
    do {                                                                       \
        g_checks++;                                                            \
        if (cond) {                                                            \
            printf("  ok    ");                                                \
        } else {                                                               \
            printf("  FAIL  ");                                                \
            g_fail++;                                                          \
        }                                                                      \
        printf(__VA_ARGS__);                                                   \
        printf("\n");                                                          \
    } while (0)

static void log_cb(void *user, int32_t level, const char *msg)
{
    (void)user;
    if (level >= SA1_LOG_WARN) {
        printf("    [libsa1] %s\n", msg);
    }
}

/* ---- worlds (host space: x, y horizontal, z up, HU) ----------------------------------------------------------- */
typedef struct tris {
    sa1_collision_tri *t;
    uint32_t n, cap;
} tris;

static void tri_add(tris *b, const float a[3], const float c[3], const float d[3])
{
    sa1_collision_tri *t;
    if (b->n == b->cap) {
        uint32_t cap = b->cap ? 2u * b->cap : 4096u;
        sa1_collision_tri *nt = (sa1_collision_tri *)realloc(b->t, cap * sizeof *nt);
        if (nt == NULL) {
            return;
        }
        b->t = nt;
        b->cap = cap;
    }
    t = &b->t[b->n++];
    memcpy(t->v[0], a, sizeof t->v[0]);
    memcpy(t->v[1], c, sizeof t->v[1]);
    memcpy(t->v[2], d, sizeof t->v[2]);
    t->surface = SA1_SURFACE_SOLID;
}

/* counter-clockwise from the front: a b c d */
static void quad(tris *b, const float a[3], const float c[3], const float d[3], const float e[3])
{
    tri_add(b, a, c, d);
    tri_add(b, a, d, e);
}

static void floor_rect(tris *b, float x0, float y0, float x1, float y1, float z, float tile)
{
    float x, y;
    for (x = x0; x < x1 - 0.01f; x += tile) {
        for (y = y0; y < y1 - 0.01f; y += tile) {
            float xe = x + tile < x1 ? x + tile : x1, ye = y + tile < y1 ? y + tile : y1;
            float a[3] = { x, y, z }, c[3] = { xe, y, z }, d[3] = { xe, ye, z }, e[3] = { x, ye, z };
            quad(b, a, c, d, e);
        }
    }
}

static void box(tris *b, float x0, float y0, float z0, float x1, float y1, float z1)
{
    float p[8][3] = { { x0, y0, z0 }, { x1, y0, z0 }, { x1, y1, z0 }, { x0, y1, z0 },
                      { x0, y0, z1 }, { x1, y0, z1 }, { x1, y1, z1 }, { x0, y1, z1 } };
    quad(b, p[4], p[5], p[6], p[7]);
    quad(b, p[0], p[3], p[2], p[1]);
    quad(b, p[0], p[1], p[5], p[4]);
    quad(b, p[2], p[3], p[7], p[6]);
    quad(b, p[1], p[2], p[6], p[5]);
    quad(b, p[3], p[0], p[4], p[7]);
}

/* n steps of rise h and depth d going up along +x from x0, w wide, then a landing */
static void stairs(tris *b, float x0, int n, float h, float d, float w)
{
    int i;
    for (i = 0; i < n; i++) {
        float x = x0 + (float)i * d, z = (float)(i + 1) * h;
        float r0[3] = { x, -w, z - h }, r1[3] = { x, w, z - h }, r2[3] = { x, w, z }, r3[3] = { x, -w, z };
        float t0[3] = { x, -w, z }, t1[3] = { x + d, -w, z }, t2[3] = { x + d, w, z }, t3[3] = { x, w, z };
        quad(b, r0, r1, r2, r3);
        quad(b, t0, t1, t2, t3);
    }
    floor_rect(b, x0 + (float)n * d, -w, x0 + (float)n * d + 2048.0f, w, (float)n * h, 512.0f);
}

enum { W_FLAT, W_TOWER, W_STAIRS, W_LEDGE, W_COUNT };
static const char *const k_world[W_COUNT] = { "flat", "tower", "stairs", "ledges" };

static void build_world(int which, tris *b)
{
    memset(b, 0, sizeof *b);
    floor_rect(b, -16384.0f, -16384.0f, 16384.0f, 16384.0f, 0.0f, 1024.0f);
    if (which == W_TOWER) {
        box(b, -15600.0f, -512.0f, 0.0f, -14400.0f, 512.0f, 3000.0f);     /* 3000 HU high, the floor below */
    } else if (which == W_STAIRS) {
        stairs(b, -14000.0f, 24, 16.0f, 24.0f, 512.0f);                    /* raw steps: 24 of 16 x 24 HU */
    } else if (which == W_LEDGE) {
        stairs(b, -14700.0f, 3, 40.0f, 400.0f, 512.0f);                    /* three 40-HU step-ups */
    }
}

/* ---- scenarios (the research's: sadc/build/research/vflicker/vf_test.cpp, lsd_lag_review/lsd_motion.cpp) ------ */
typedef void (*script_fn)(double t, sa1_input *in);

static int hold(double t, double a, double b)
{
    return t >= a && t < b;
}

static void s_run_off(double t, sa1_input *in)
{
    in->stick_y = hold(t, 0.5, 4.0) ? 1.0f : 0.0f;
}

static void s_jump_run(double t, sa1_input *in)
{
    in->stick_y = t >= 0.5 ? 1.0f : 0.0f;
    in->buttons = (hold(t, 3.0, 3.3) || hold(t, 4.6, 4.7)) ? SA1_BUTTON_JUMP : 0u;
}

static void s_jump_stand(double t, sa1_input *in)
{
    in->buttons = (hold(t, 1.0, 1.3) || hold(t, 2.5, 2.6)) ? SA1_BUTTON_JUMP : 0u;
}

static void s_forward(double t, sa1_input *in)
{
    in->stick_y = t >= 0.5 ? 1.0f : 0.0f;
}

static void s_spindash_run(double t, sa1_input *in)
{
    in->stick_y = t >= 0.5 ? 1.0f : 0.0f;
    in->buttons = hold(t, 2.5, 3.5) ? SA1_BUTTON_ACTION : 0u;
}

static void s_lsd_stand(double t, sa1_input *in)
{
    in->buttons = hold(t, 1.0, 5.0) ? SA1_BUTTON_ACTION : 0u;
}

#define SC_NODE     0x1u    /* node check (airborne steady poses: falls, jumps) */
#define SC_CHANGE   0x2u    /* change-per-frame check (the research's fall and stairs cases) */
#define SC_LEAD     0x4u    /* lead check at top speed */
#define SC_RELEASE  0x8u    /* the Light Speed release window */
#define SC_BALL     0x10u   /* the jump ball (one drawn model, airborne) has its root at the display centre */

typedef struct scenario {
    const char *name;
    int world;
    int32_t kind;
    float pos[3];
    float seconds, from;
    script_fn script;
    uint32_t checks;        /* SC_* */
    uint32_t min_steady;    /* SC_NODE: airborne pairs repeating a pose the run must have (proof it is exercised) */
    int32_t centre_action;  /* the action whose pose has the root node at the display centre (-1: none) */
    uint16_t upgrades[4];   /* event flags (0: none) */
} scenario;

static const scenario k_sc[] = {
    { "sonic_fall", W_TOWER, SA1_CHAR_SONIC, { -15400.0f, 0.0f, 3040.0f }, 7.0f, 1.0f, s_run_off, SC_NODE | SC_CHANGE,
      40, 18, { 0 } },      /* action 18: the fall pose, its root node at the display centre */
    { "knuckles_fall", W_TOWER, SA1_CHAR_KNUCKLES, { -15400.0f, 0.0f, 3040.0f }, 7.0f, 1.0f, s_run_off, SC_NODE, 40, -1,
      { 0 } },
    { "sonic_jump_run", W_FLAT, SA1_CHAR_SONIC, { -15000.0f, 0.0f, 40.0f }, 6.0f, 2.9f, s_jump_run, SC_NODE | SC_BALL, 5, -1,
      { 0 } },
    { "knuckles_jump_stand", W_FLAT, SA1_CHAR_KNUCKLES, { -15000.0f, 0.0f, 40.0f }, 4.0f, 0.9f, s_jump_stand,
      SC_NODE | SC_BALL, 3, -1, { 0 } },
    { "amy_stairs", W_STAIRS, SA1_CHAR_AMY, { -14600.0f, 0.0f, 40.0f }, 6.0f, 0.8f, s_forward, SC_CHANGE, 0, -1, { 0 } },
    { "sonic_stairs", W_STAIRS, SA1_CHAR_SONIC, { -14600.0f, 0.0f, 40.0f }, 5.0f, 0.8f, s_forward, SC_CHANGE, 0, -1,
      { 0 } },
    { "gamma_ledges", W_LEDGE, SA1_CHAR_GAMMA, { -15000.0f, 0.0f, 40.0f }, 6.0f, 0.8f, s_forward, SC_CHANGE, 0, -1,
      { 0 } },
    { "sonic_flat_run", W_FLAT, SA1_CHAR_SONIC, { -15000.0f, 0.0f, 40.0f }, 6.0f, 1.0f, s_forward, SC_LEAD, 0, -1,
      { 0 } },
    { "sonic_spindash_run", W_FLAT, SA1_CHAR_SONIC, { -15000.0f, 0.0f, 40.0f }, 8.0f, 0.9f, s_spindash_run, SC_LEAD, 0,
      -1, { 0 } },
    { "lsd_release", W_FLAT, SA1_CHAR_SONIC, { -15000.0f, 0.0f, 40.0f }, 8.0f, 0.9f, s_lsd_stand, SC_RELEASE | SC_LEAD,
      0, -1, { 0x8D, 0x8E, 0x9F, 0 } },    /* Light Speed Shoes, Crystal Ring, Ancient Light (G-SA1 kUpgrades) */
};
#define NSC (sizeof k_sc / sizeof k_sc[0])

typedef struct rate {
    const char *name;
    float dt[5];
    uint32_t n;
    float change_limit;     /* SC_CHANGE limit (HU): 2 HU per 1/60 s frame, 1.5 at 144 fps */
} rate;

static const rate k_rates[] = {
    { "60", { 1.0f / 60.0f }, 1, 2.0f },
    { "144", { 1.0f / 144.0f }, 1, 1.5f },
    { "100/200", { 1.0f / 100.0f, 1.0f / 200.0f }, 2, 2.0f },
    { "60on84", { 2.0f / 84.0f, 1.0f / 84.0f, 2.0f / 84.0f, 1.0f / 84.0f, 1.0f / 84.0f }, 5, 2.9f },
};
#define NRATE (sizeof k_rates / sizeof k_rates[0])

/* ---- one run ---------------------------------------------------------------------------------------------------- */
typedef struct frame {
    double t;
    float alpha;
    uint32_t interp, rflags, newpair, pair_matched, ntri;
    uint32_t ntri1, nnodes;     /* the last pair: triangles, drawn models (nodes of the geometry) */
    float frame;                /* sa1_state.frame (the motion frame after the pair) */
    float root[3], root1[3], yaw1, body[3], node0[3], nrot[9], st[3], st_yaw, vel[3], drawn[3];
    int have_body, have_node, have_drawn, ground;
    int32_t action, req_action;
    float moved_err;        /* max over the corners: |tri(alpha) - (tri(1) + root - root1)| */
    float asis_err;         /* max over the corners: |tri(alpha) - tri(1)| */
} frame;

typedef struct run {
    frame *f;
    uint32_t n, cap;
} run;

static int is_body(const sa1_draw *d)
{
    return d->prim == SA1_PRIM_TRIANGLES && d->archive == 0 && !(d->flags & SA1_DRAW_EFFECT) &&
           (d->source == SA1_SOURCE_CHARACTER || d->source == SA1_SOURCE_MODEL || d->source == SA1_SOURCE_POSE);
}

static int body_centroid(const sa1_render *r, float out[3])
{
    double s[3] = { 0.0, 0.0, 0.0 };
    uint64_t n = 0;
    uint32_t d, t, k;
    for (d = 0; d < r->num_draws; d++) {
        const sa1_draw *dr = &r->draws[d];
        if (!is_body(dr)) {
            continue;
        }
        for (t = dr->first; t < dr->first + dr->count && t < r->num_triangles; t++) {
            for (k = 0; k < 9; k++) {
                s[k % 3] += r->positions[9u * t + k];
            }
            n += 3;
        }
    }
    if (n == 0) {
        return 0;
    }
    for (k = 0; k < 3; k++) {
        out[k] = (float)(s[k] / (double)n);
    }
    return 1;
}

static int set_event_flags(sa1_context *ctx, sa1_character_id id, const uint16_t *flags)
{
    struct sa1g_world *w = sa1_character_world(ctx, id);
    uint32_t i;
    if (w == NULL) {
        return -1;
    }
    for (i = 0; i < 4 && flags[i]; i++) {
        uint32_t a = EVENT_FLAGS + flags[i], word = a & ~3u, v = 0, sh = 8u * (a & 3u);
        if (sa1g_world_rd32(w, word, &v) != 0) {
            return -1;
        }
        v = (v & ~(0xFFu << sh)) | (1u << sh);
        if (sa1g_world_wr32(w, word, v) != 0) {
            return -1;
        }
    }
    return 0;
}

static int run_one(sa1_context *ctx, const scenario *sc, const rate *rt, run *R)
{
    sa1_character_desc d;
    sa1_character_id id = 0;
    sa1_status st;
    float *tri1 = NULL;
    uint32_t cap1 = 0, k = 0, last_gen = 0xFFFFFFFFu;
    double t = 0.0;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.kind = sc->kind;
    d.flags = SA1_CREATE_WORLD;
    memcpy(d.position, sc->pos, sizeof d.position);
    st = sa1_character_create_ex(ctx, &d, &id);
    if (SA1_FAILED(st)) {
        printf("    create %s: %s: %s\n", sc->name, sa1_status_string(st), sa1_last_error(ctx));
        return -1;
    }
    if (sc->upgrades[0] && set_event_flags(ctx, id, sc->upgrades) != 0) {
        printf("    %s: the upgrades cannot be set\n", sc->name);
        sa1_character_destroy(ctx, id);
        return -1;
    }
    R->n = 0;
    while (t < (double)sc->seconds) {
        sa1_input in;
        sa1_state_ex s;
        const sa1_render *r = NULL;
        const sa1_geometry *g = NULL;
        frame *f;
        float dt = rt->dt[k++ % rt->n], sh[3];
        uint32_t i, nt1;
        t += dt;
        memset(&in, 0, sizeof in);
        in.struct_size = sizeof in;
        sc->script(t, &in);
        st = sa1_character_tick(ctx, id, &in, dt, NULL, NULL);
        if (st != SA1_OK) {
            printf("    %s: tick at t %.3f: %s: %s\n", sc->name, t, sa1_status_string(st), sa1_last_error(ctx));
            break;
        }
        if (R->n == R->cap) {
            uint32_t nc = R->cap ? 2u * R->cap : 4096u;
            frame *nf = (frame *)realloc(R->f, nc * sizeof *nf);
            if (nf == NULL) {
                break;
            }
            R->f = nf;
            R->cap = nc;
        }
        f = &R->f[R->n++];
        memset(f, 0, sizeof *f);
        f->t = t;
        memset(&s, 0, sizeof s);
        s.struct_size = sizeof s;
        sa1_character_state_ex(ctx, id, &s);
        f->req_action = s.req_action;
        f->frame = s.frame;
        memcpy(f->st, s.position, sizeof f->st);
        f->st_yaw = s.yaw;
        memcpy(f->vel, s.velocity, sizeof f->vel);
        f->ground = (s.flags & SA1_STATE_ON_GROUND) != 0;
        f->action = s.action;
        /* the last pair as drawn */
        if (sa1_character_render(ctx, id, 1.0f, &r) != SA1_OK || r == NULL) {
            printf("    %s: render(1) at t %.3f failed\n", sc->name, t);
            break;
        }
        memcpy(f->root1, r->position, sizeof f->root1);
        f->yaw1 = r->yaw;
        f->newpair = r->generation != last_gen;
        last_gen = r->generation;
        nt1 = r->num_triangles;
        f->ntri1 = nt1;
        if (nt1 > cap1) {
            float *nb = (float *)realloc(tri1, 9u * sizeof(float) * nt1);
            if (nb == NULL) {
                break;
            }
            tri1 = nb;
            cap1 = nt1;
        }
        if (nt1) {
            memcpy(tri1, r->positions, 9u * sizeof(float) * nt1);
        }
        if (sa1_character_geometry(ctx, id, &g) == SA1_OK && g != NULL && g->num_nodes > 0) {
            f->node0[0] = g->node_matrices[3];
            f->node0[1] = g->node_matrices[7];
            f->node0[2] = g->node_matrices[11];
            for (i = 0; i < 3; i++) {
                f->nrot[3 * i] = g->node_matrices[4 * i];
                f->nrot[3 * i + 1] = g->node_matrices[4 * i + 1];
                f->nrot[3 * i + 2] = g->node_matrices[4 * i + 2];
            }
            f->have_node = 1;
            f->nnodes = g->num_nodes;
#if defined(SA1_DRAW_SHADOW)
            /* the blob shadow (0.6.0 fixes: kept in the render output, SA1_DRAW_SHADOW) is a drawn model of its own: the
               body's models are the others (the jump ball is one) */
            for (i = 0; i < r->num_draws; i++) {
                f->nnodes -= (r->draws[i].flags & SA1_DRAW_SHADOW) != 0 && f->nnodes > 0;
            }
#endif
        }
        /* whether the last pair's draws matched the previous pair's (between them) */
        if (sa1_character_render(ctx, id, 0.5f, &r) == SA1_OK && r != NULL) {
            f->pair_matched = r->interpolated;
        }
#if defined(SA1W_PSTATE_DRAWN)
        {
            sa1w_pstate ps;
            sa1w_world_pstate(sa1_character_world(ctx, id), &ps);
            if (ps.valid && ps.drawn_valid) {
                sa1_to_host_point(ctx, ps.drawn_pos, f->drawn);
                f->have_drawn = 1;
            }
        }
#endif
        /* the frame as a host renders it */
        if (sa1_character_render(ctx, id, -1.0f, &r) != SA1_OK || r == NULL) {
            printf("    %s: render(-1) at t %.3f failed\n", sc->name, t);
            break;
        }
        f->alpha = r->alpha;
        f->interp = r->interpolated;
#if defined(SA1_RENDER_MOVED_WITH_ROOT)
        f->rflags = r->struct_size >= offsetof(sa1_render, flags) + sizeof(r->flags) ? r->flags : 0u;
#endif
        memcpy(f->root, r->position, sizeof f->root);
        f->have_body = body_centroid(r, f->body);
        f->ntri = r->num_triangles;
        for (i = 0; i < 3; i++) {
            sh[i] = f->root[i] - f->root1[i];
        }
        if (r->num_triangles == nt1) {
            for (i = 0; i < 9u * nt1; i++) {
                float a = fabsf(r->positions[i] - tri1[i]), b = fabsf(r->positions[i] - (tri1[i] + sh[i % 3]));
                f->asis_err = a > f->asis_err ? a : f->asis_err;
                f->moved_err = b > f->moved_err ? b : f->moved_err;
            }
        } else {
            f->asis_err = f->moved_err = 1e30f;
        }
    }
    free(tri1);
    sa1_character_destroy(ctx, id);
    return 0;
}

/* ---- analysis --------------------------------------------------------------------------------------------------- */
/* the node / lead metrics against one root (the render root, or the state's: libsa1 before fix B) */
typedef struct rootm {
    uint32_t pose_pairs;        /* airborne pairs that drew a pose drawn before (same action, motion frame, models) */
    double pose_spread;         /* max |node0 - root| difference between two such pairs (facing frame) */
    double pose_dv;             /* ... the largest speed difference between the two (HU/s) */
    uint32_t centre_pairs;      /* the scenario's centre pose (sonic_fall: the fall pose), past its link blend */
    double centre_err, centre_t;    /* max |node0 - root - (0, 0, display centre)| over them, where */
    float centre_vmax;          /* the fastest of them (HU/s) */
    double lead_med, lead_max;  /* median / largest |.| lead at >= 90 % of the top horizontal speed */
    uint32_t lead_n;
} rootm;

typedef struct result {
    uint32_t frames, pairs, raw_frames;
    uint32_t drawn_pairs, drawn_exact;
    uint32_t ref_pairs;
    float ref[3];               /* node0 - root1 standing (facing frame), before 0.45 s */
    rootm m[2];                 /* [0] the render root, [1] the state's root (before fix B) */
    double st_off_max;          /* max |state - root1|: one step of movement after fix B, 0 before */
    double in_z, in_path, edge_z, edge_path;
    double in_t;
    double win_path, win_t;     /* SC_RELEASE: max |change along the path| in 4.9 .. 5.4 s */
    double behind2, snap2;      /* ... the largest two-frame sums against / with the path */
    float max_vz, max_speed, top_h;
    /* reporting (every frame) */
    uint32_t moved_frames, moved_flagged, moved_unflagged, flagged_not_moved, asis_frames, asis_bad;
} result;

static float dot3(const float a[3], const float b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

/* a - b in the frame of a character facing yaw (x forward, y left, z up: host SOURCE axes turned by -yaw) */
static void local_of(const float a[3], const float b[3], float yaw, float out[3])
{
    float dx = a[0] - b[0], dy = a[1] - b[1], c = cosf(yaw), s = sinf(yaw);
    out[0] = c * dx + s * dy;
    out[1] = -s * dx + c * dy;
    out[2] = a[2] - b[2];
}

/* a node rotation (row-major 3x3, host axes) in the facing frame of yaw */
static void rot_local(const float r[9], float yaw, float out[9])
{
    float c = cosf(yaw), s = sinf(yaw);
    int j;
    for (j = 0; j < 3; j++) {
        out[j] = c * r[j] + s * r[3 + j];
        out[3 + j] = -s * r[j] + c * r[3 + j];
        out[6 + j] = r[6 + j];
    }
}

static float rot_diff(const float a[9], const float b[9])
{
    float m = 0.0f;
    int j;
    for (j = 0; j < 9; j++) {
        m = fabsf(a[j] - b[j]) > m ? fabsf(a[j] - b[j]) : m;
    }
    return m;
}

static int cmp_double(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}

static void root_metrics(const scenario *sc, const run *R, const result *Q, int which, float up, rootm *M)
{
    typedef struct pose_seen {
        int32_t action;
        uint32_t frame_bits, ntri, nnodes;
        float e[3], speed;
    } pose_seen;
    uint32_t i, k, nl = 0, np = 0, run_len = 0;
    int32_t run_action = -1;
    double *leads = (double *)malloc(sizeof(double) * (R->n + 1u));
    pose_seen *seen = (pose_seen *)malloc(sizeof(pose_seen) * (R->n + 1u));
    memset(M, 0, sizeof *M);
    for (i = 0; i < R->n; i++) {
        const frame *f = &R->f[i];
        const float *root = which ? f->st : f->root1;
        float yaw = which ? f->st_yaw : f->yaw1, e[3], sp = sqrtf(dot3(f->vel, f->vel));
        if (!f->newpair) {
            continue;
        }
        if (!f->ground && f->action == run_action) {
            run_len++;
        } else {
            run_action = f->ground ? -1 : f->action;
            run_len = f->ground ? 0u : 1u;
        }
        if (f->t < (double)sc->from || !f->have_node) {
            continue;
        }
        local_of(f->node0, root, yaw, e);
        /* the same pose drawn again (airborne, matched with the pair before: not a model switch) */
        if (!f->ground && f->pair_matched && seen != NULL) {
            pose_seen *p = &seen[np];
            uint32_t j, frame_bits;
            memcpy(&frame_bits, &f->frame, sizeof frame_bits);
            for (j = 0; j < np; j++) {
                const pose_seen *q = &seen[j];
                if (q->action == f->action && q->frame_bits == frame_bits && q->ntri == f->ntri1 &&
                    q->nnodes == f->nnodes) {
                    double d = 0.0, dv = fabs((double)sp - q->speed);
                    for (k = 0; k < 3; k++) {
                        d += ((double)e[k] - q->e[k]) * ((double)e[k] - q->e[k]);
                    }
                    d = sqrt(d);
                    M->pose_pairs++;
                    M->pose_spread = d > M->pose_spread ? d : M->pose_spread;
                    M->pose_dv = dv > M->pose_dv ? dv : M->pose_dv;
                    break;
                }
            }
            p->action = f->action;
            p->frame_bits = frame_bits;
            p->ntri = f->ntri1;
            p->nnodes = f->nnodes;
            memcpy(p->e, e, sizeof p->e);
            p->speed = sp;
            np++;
        }
        /* the pose whose root node is the model's root at the display centre, past its link blend */
        if (!f->ground && ((sc->centre_action >= 0 && f->action == sc->centre_action && run_len >= 3) ||
                           ((sc->checks & SC_BALL) && f->nnodes == 1 && f->pair_matched))) {
            double ae = sqrt((double)e[0] * e[0] + (double)e[1] * e[1] + ((double)e[2] - up) * ((double)e[2] - up));
            M->centre_pairs++;
            M->centre_vmax = sp > M->centre_vmax ? sp : M->centre_vmax;
            if (ae > M->centre_err) {
                M->centre_err = ae;
                M->centre_t = f->t;
            }
        }
        /* lead: along the horizontal velocity, against standing */
        {
            float hv = sqrtf(f->vel[0] * f->vel[0] + f->vel[1] * f->vel[1]);
            if (leads != NULL && Q->ref_pairs && hv >= 0.9f * Q->top_h && hv > 100.0f) {
                float d[3] = { f->vel[0] / hv, f->vel[1] / hv, 0.0f }, w[3], c = cosf(yaw), s = sinf(yaw);
                /* node0 - root - the standing offset turned to the current facing (world axes) */
                w[0] = (f->node0[0] - root[0]) - (c * Q->ref[0] - s * Q->ref[1]);
                w[1] = (f->node0[1] - root[1]) - (s * Q->ref[0] + c * Q->ref[1]);
                w[2] = 0.0f;
                leads[nl++] = -(double)dot3(w, d);
            }
        }
    }
    free(seen);
    if (nl) {
        qsort(leads, nl, sizeof *leads, cmp_double);
        M->lead_med = leads[nl / 2];
        M->lead_max = fabs(leads[0]) > fabs(leads[nl - 1]) ? leads[0] : leads[nl - 1];
        M->lead_n = nl;
    }
    free(leads);
}

static void analyse(const scenario *sc, const run *R, result *Q, float up, int trace)
{
    uint32_t i, k;
    float pp = 0.0f;                /* previous frame's change along the path (release window) */
    int have_pr = 0;
    memset(Q, 0, sizeof *Q);
    /* reference: node0 - root1 standing (facing frame), in the pairs before 0.45 s (the character stands still) */
    for (i = 0; i < R->n; i++) {
        const frame *f = &R->f[i];
        if (f->t >= 0.45) {
            break;
        }
        if (f->newpair && f->have_node && f->t > 0.2) {
            local_of(f->node0, f->root1, f->yaw1, Q->ref);
            Q->ref_pairs++;
        }
    }
    for (i = 0; i < R->n; i++) {
        const frame *f = &R->f[i];
        float hv = sqrtf(f->vel[0] * f->vel[0] + f->vel[1] * f->vel[1]);
        if (f->t >= (double)sc->from && f->newpair) {
            Q->top_h = hv > Q->top_h ? hv : Q->top_h;
        }
        /* how libsa1 says it placed a frame (cause A), every frame */
        if (!f->interp && f->alpha < 1.0f) {
            float shl = sqrtf((f->root[0] - f->root1[0]) * (f->root[0] - f->root1[0]) +
                              (f->root[1] - f->root1[1]) * (f->root[1] - f->root1[1]) +
                              (f->root[2] - f->root1[2]) * (f->root[2] - f->root1[2]));
            int moved = f->moved_err < 0.01f && shl > 0.05f;
            Q->moved_frames += moved;
            Q->moved_flagged += moved && (f->rflags & 1u);
            Q->moved_unflagged += moved && !(f->rflags & 1u);
            Q->flagged_not_moved += (f->rflags & 1u) && f->moved_err >= 0.01f;
            if (!(f->rflags & 1u)) {
                Q->asis_frames++;
                Q->asis_bad += f->asis_err != 0.0f;
            }
        }
    }
    root_metrics(sc, R, Q, 0, up, &Q->m[0]);
    root_metrics(sc, R, Q, 1, up, &Q->m[1]);
    for (i = 0; i < R->n; i++) {
        const frame *f = &R->f[i];
        float sp = sqrtf(dot3(f->vel, f->vel));
        if (f->t < (double)sc->from) {
            continue;
        }
        Q->frames++;
        Q->max_speed = sp > Q->max_speed ? sp : Q->max_speed;
        Q->max_vz = fabsf(f->vel[2]) > Q->max_vz ? fabsf(f->vel[2]) : Q->max_vz;
        Q->raw_frames += !f->interp;
        if (f->newpair) {
            double so = sqrt((double)(f->st[0] - f->root1[0]) * (f->st[0] - f->root1[0]) +
                             (double)(f->st[1] - f->root1[1]) * (f->st[1] - f->root1[1]) +
                             (double)(f->st[2] - f->root1[2]) * (f->st[2] - f->root1[2]));
            Q->pairs++;
            Q->st_off_max = so > Q->st_off_max ? so : Q->st_off_max;
            if (f->have_drawn) {
                Q->drawn_pairs++;
                Q->drawn_exact += memcmp(f->drawn, f->root1, sizeof f->drawn) == 0;
            }
            if (trace) {
                float e[3], es[3], rl[9];
                static float prl[9];
                local_of(f->node0, f->root1, f->yaw1, e);
                local_of(f->node0, f->st, f->st_yaw, es);
                rot_local(f->nrot, f->yaw1, rl);
                printf("    t %7.4f %s%s act %3d/%3d %s vz %7.0f | root1 %9.2f %9.2f %8.2f | node0 - root (facing) %+7.2f "
                       "%+7.2f %+7.2f, - state %+7.2f %+7.2f %+7.2f | node turn %.4f | drawn %s\n",
                       f->t, f->pair_matched ? "match" : "NEW  ", f->rflags & 2u ? " D" : "  ", (int)f->action,
                       (int)f->req_action, f->ground ? "G" : "A", f->vel[2], f->root1[0], f->root1[1], f->root1[2], e[0],
                       e[1], e[2], es[0], es[1], es[2], (double)rot_diff(rl, prl),
                       f->have_drawn ? (memcmp(f->drawn, f->root1, sizeof f->drawn) == 0 ? "exact" : "DIFF") : "-");
                memcpy(prl, rl, sizeof prl);
            }
        }
        /* per frame: the change of body - root against the frame before */
        if (i > 0 && f->have_body && R->f[i - 1].have_body) {
            const frame *a = &R->f[i - 1];
            float dr[3], drel[3], d[3] = { 0.0f, 0.0f, 1.0f }, dl, cz, cp;
            for (k = 0; k < 3; k++) {
                dr[k] = f->root[k] - a->root[k];
                drel[k] = (f->body[k] - f->root[k]) - (a->body[k] - a->root[k]);
            }
            dl = sqrtf(dot3(dr, dr));
            if (dl > 1e-3f) {
                for (k = 0; k < 3; k++) {
                    d[k] = dr[k] / dl;
                }
            }
            cz = fabsf(drel[2]);
            cp = dot3(drel, d);
            if (f->newpair) {
                Q->edge_z = cz > Q->edge_z ? cz : Q->edge_z;
                Q->edge_path = fabsf(cp) > Q->edge_path ? fabsf(cp) : Q->edge_path;
            } else {
                if (cz > Q->in_z) {
                    Q->in_z = cz;
                    Q->in_t = f->t;
                }
                Q->in_path = fabsf(cp) > Q->in_path ? fabsf(cp) : Q->in_path;
            }
            if ((sc->checks & SC_RELEASE) && f->t >= 4.9 && f->t <= 5.4) {
                float rel[3] = { f->body[0] - f->root[0], f->body[1] - f->root[1], f->body[2] - f->root[2] };
                if (fabsf(cp) > Q->win_path) {
                    Q->win_path = fabsf(cp);
                    Q->win_t = f->t;
                }
                if (have_pr) {
                    double s2 = (double)pp + cp;
                    Q->behind2 = -s2 > Q->behind2 ? -s2 : Q->behind2;
                    Q->snap2 = s2 > Q->snap2 ? s2 : Q->snap2;
                }
                pp = cp;
                have_pr = 1;
                if (trace) {
                    printf("    release t %.4f a %.3f %s fl %u act %3d | root moved %6.2f | body - root along the path %+7.2f "
                           "(change %+7.2f)\n", f->t, f->alpha, f->interp ? "lerp" : "RAW ", f->rflags, (int)f->action, dl,
                           dot3(rel, d), cp);
                }
            }
        }
    }
}

/* ---- main ------------------------------------------------------------------------------------------------------- */
#if defined(SA1W_PSTATE_DRAWN)
static const char k_build_note[] = "";
#else
static const char k_build_note[] = "; built against a libsa1 without SA1W_PSTATE_DRAWN (before fix B): measuring";
#endif

static sa1_context *make_ctx(const char *gdi)
{
    sa1_context *ctx = NULL;
    sa1_host h;
    sa1_load_options lo;
    sa1_coords co;
    sa1_status st;
    sa1_host_defaults(&h);
    h.log = log_cb;
    h.log_level = SA1_LOG_WARN;
    if (sa1_init(&h, &ctx) != SA1_OK) {
        return NULL;
    }
    memset(&co, 0, sizeof co);
    co.struct_size = sizeof co;
    sa1_coords_preset(SA1_COORDS_PRESET_SOURCE, &co);     /* G-SA1's: 4 HU per SA1 unit, z up */
    sa1_set_coords(ctx, &co);
    memset(&lo, 0, sizeof lo);
    lo.struct_size = sizeof lo;
    lo.characters = (1u << SA1_CHAR_SONIC) | (1u << SA1_CHAR_KNUCKLES) | (1u << SA1_CHAR_AMY) | (1u << SA1_CHAR_GAMMA);
    lo.flags = SA1_LOAD_DEFAULT | SA1_LOAD_WORLD_IMAGES;
    st = sa1_load_disc(ctx, gdi, &lo);
    if (SA1_FAILED(st)) {
        printf("sa1_load_disc: %s: %s\n", sa1_status_string(st), sa1_last_error(ctx));
        sa1_shutdown(ctx);
        return NULL;
    }
    return ctx;
}

/* the display's centre offset above taskwk.pos (host units): sa1_character.c pose_eval_current's rule */
static float centre_up(sa1_context *ctx, int32_t kind)
{
    sa1_physics ph;
    float a[3] = { 0.0f, 0.0f, 0.0f }, b[3], h0[3], h1[3];
    memset(&ph, 0, sizeof ph);
    if (sa1_physics_get(ctx, kind, &ph) != SA1_OK) {
        return 0.0f;
    }
    b[0] = 0.0f;
    b[1] = (kind == SA1_CHAR_TAILS || kind == SA1_CHAR_KNUCKLES || kind == SA1_CHAR_AMY) ? ph.height * 0.5f
                                                                                         : ph.center_height;
    b[2] = 0.0f;
    sa1_to_host_point(ctx, a, h0);
    sa1_to_host_point(ctx, b, h1);
    return h1[2] - h0[2];
}

int main(int argc, char **argv)
{
    const char *gdi = getenv("SA1_TEST_GDI"), *only = NULL, *fps = NULL;
    sa1_context *ctx;
    run R;
    int i, cur_world = -1;
    uint32_t s, q;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--only") && i + 1 < argc) {
            only = argv[++i];
        } else if (!strcmp(argv[i], "--fps") && i + 1 < argc) {
            fps = argv[++i];
        } else if (!strcmp(argv[i], "--trace") && i + 1 < argc) {
            g_trace = argv[++i];
        } else {
            gdi = argv[i];
        }
    }
    if (gdi == NULL) {
        printf("usage: flicker_test [--only NAME] [--fps 60|144|100/200|60on84] [--trace NAME] GDI (or $SA1_TEST_GDI)\n");
        return 2;
    }
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("libsa1 %s flicker test: the render root is the drawn step's (gsa1_vertical_flicker.md fix B)%s\n",
           sa1_version_string(), k_build_note);
    ctx = make_ctx(gdi);
    if (ctx == NULL) {
        return 2;
    }
    memset(&R, 0, sizeof R);
    for (s = 0; s < NSC; s++) {
        const scenario *sc = &k_sc[s];
        float up;
        if (only != NULL && strstr(sc->name, only) == NULL) {
            continue;
        }
        if (sc->world != cur_world) {
            tris b;
            sa1_status st;
            build_world(sc->world, &b);
            st = sa1_collision_set_static(ctx, b.t, b.n);
            free(b.t);
            if (SA1_FAILED(st)) {
                printf("world %s: %s: %s\n", k_world[sc->world], sa1_status_string(st), sa1_last_error(ctx));
                return 2;
            }
            cur_world = sc->world;
        }
        up = centre_up(ctx, sc->kind);
        printf("\n== %s (world %s, %.0f s, measured from %.1f s; display centre %.2f HU above the root)\n", sc->name,
               k_world[sc->world], (double)sc->seconds, (double)sc->from, (double)up);
        for (q = 0; q < NRATE; q++) {
            const rate *rt = &k_rates[q];
            result Q;
            int trace;
            if (fps != NULL && strcmp(fps, rt->name) != 0) {
                continue;
            }
            if ((sc->checks & SC_RELEASE) && strcmp(rt->name, "60") != 0 && strcmp(rt->name, "60on84") != 0) {
                continue;
            }
            if (run_one(ctx, sc, rt, &R) != 0) {
                CHECK(0, "%s@%s: the run", sc->name, rt->name);
                continue;
            }
            trace = g_trace != NULL && strstr(sc->name, g_trace) != NULL;
            if (trace) {
                printf("  -- trace %s@%s\n", sc->name, rt->name);
            }
            analyse(sc, &R, &Q, up, trace);
            printf("  %s@%s: %u frames (%u not interpolated), %u pairs, top speed %.0f HU/s, vz %.0f | state - root up to "
                   "%.2f HU | body - root per frame: in pairs z %.2f path %.2f (worst t %.3f), at pair edges z %.2f "
                   "path %.2f\n", sc->name, rt->name, Q.frames, Q.raw_frames, Q.pairs, (double)Q.max_speed,
                   (double)Q.max_vz, Q.st_off_max, Q.in_z, Q.in_path, Q.in_t, Q.edge_z, Q.edge_path);
            printf("      root node: %u airborne pairs repeat a pose (speeds up to %.0f HU/s apart), the node against the "
                   "root differs by %.3f HU between them (with the state's root, before fix B: %.2f HU)",
                   Q.m[0].pose_pairs, Q.m[0].pose_dv, Q.m[0].pose_spread, Q.m[1].pose_spread);
            if (sc->centre_action >= 0 || (sc->checks & SC_BALL)) {
                printf("; %u pairs of %s (up to %.0f HU/s): %.3f HU off the render root + the display centre (t %.3f; "
                       "with the state's root %.2f HU)", Q.m[0].centre_pairs, sc->centre_action >= 0 ? "the fall pose"
                       : "the jump ball", (double)Q.m[0].centre_vmax, Q.m[0].centre_err, Q.m[0].centre_t,
                       Q.m[1].centre_err);
            }
            printf("\n");
            printf("      lead at >= %.0f HU/s (%u pairs): median %.2f, largest %.2f HU | with the state's root: median "
                   "%.2f, largest %.2f HU\n", 0.9 * (double)Q.top_h, Q.m[0].lead_n, Q.m[0].lead_med, Q.m[0].lead_max,
                   Q.m[1].lead_med, Q.m[1].lead_max);
#if defined(SA1W_PSTATE_DRAWN)
            CHECK(Q.drawn_pairs == Q.pairs && Q.pairs > 0 && Q.drawn_exact == Q.drawn_pairs,
                  "%s@%s: in all %u pairs the render root at alpha 1 is the world's sub-step-0 root bit for bit (%u of %u)",
                  sc->name, rt->name, Q.pairs, Q.drawn_exact, Q.drawn_pairs);
#endif
            if (sc->checks & SC_NODE) {
                CHECK(Q.m[0].pose_pairs >= sc->min_steady && Q.m[0].pose_spread < 0.5,
                      "%s@%s: per node: %u airborne pairs (at least %u) draw a pose drawn before at another speed (up to "
                      "%.0f HU/s apart); the drawn root node sits at the same place against the render root within %.3f "
                      "HU (< 0.5)", sc->name, rt->name, Q.m[0].pose_pairs, sc->min_steady, Q.m[0].pose_dv,
                      Q.m[0].pose_spread);
                if (sc->centre_action >= 0 || (sc->checks & SC_BALL)) {
                    CHECK(Q.m[0].centre_pairs >= 10 && Q.m[0].centre_err < 0.5,
                          "%s@%s: per node: in %u pairs of %s (up to %.0f HU/s) the drawn root node is the render root + "
                          "the display centre within %.3f HU (< 0.5)", sc->name, rt->name, Q.m[0].centre_pairs,
                          sc->centre_action >= 0 ? "the fall pose (action 18, past its link blend)"
                          : "the jump ball (one drawn model)", (double)Q.m[0].centre_vmax, Q.m[0].centre_err);
                }
            }
            if (sc->checks & SC_CHANGE) {
                CHECK(Q.in_z < (double)rt->change_limit,
                      "%s@%s: body - root changes by %.2f HU along z per frame within a pair (limit %.1f)", sc->name,
                      rt->name, Q.in_z, (double)rt->change_limit);
            }
            if (sc->checks & SC_LEAD) {
                CHECK(Q.top_h > 900.0f && Q.m[0].lead_n > 0 && fabs(Q.m[0].lead_med) < 2.0,
                      "%s@%s: at >= %.0f HU/s the root node leads the render root by %.2f HU (median of %u pairs; "
                      "|lead| < 2, as standing)", sc->name, rt->name, 0.9 * (double)Q.top_h, Q.m[0].lead_med,
                      Q.m[0].lead_n);
            }
            if (sc->checks & SC_RELEASE) {
                printf("      release window 4.9 - 5.4 s: largest change of body - root along the path %.2f HU (t %.3f); "
                       "two frames: %.2f HU behind, %.2f HU back\n", Q.win_path, Q.win_t, Q.behind2, Q.snap2);
                CHECK(Q.win_path <= 4.0, "%s@%s: Light Speed release: body - root changes by at most 4 HU along the path "
                      "per frame (%.2f)", sc->name, rt->name, Q.win_path);
            }
#if defined(SA1_RENDER_MOVED_WITH_ROOT)
            CHECK(Q.moved_unflagged == 0 && Q.flagged_not_moved == 0 && Q.asis_bad == 0,
                  "%s@%s: reporting (every frame): %u frames not interpolated were moved with the root, %u of them "
                  "flagged SA1_RENDER_MOVED_WITH_ROOT; %u flagged but not moved; %u of %u unflagged ones not the last "
                  "pair as it is", sc->name, rt->name, Q.moved_frames, Q.moved_flagged, Q.flagged_not_moved, Q.asis_bad,
                  Q.asis_frames);
#else
            printf("      (no SA1_RENDER_MOVED_WITH_ROOT) %u frames not interpolated were moved with the root, reported as "
                   "interpolated == 0 like the last pair as it is\n", Q.moved_frames);
#endif
        }
    }
    free(R.f);
    sa1_shutdown(ctx);
    printf("\n%s: %d of %d checks failed\n", g_fail ? "FAIL" : "PASS", g_fail, g_checks);
    return g_fail ? 1 : 0;
}
