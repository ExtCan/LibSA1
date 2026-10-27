/*
 * water_host_test.c - round 0.9.2 (sadc docs/research/gsa1_090_water.md WTR-A / WTR-W1V with its Review R4 a-c, R3.3-R3.5;
 * gsa1_090_plan.md 6.3): the host's water at the character (sa1_input.host_flags) and round 0.9.1's deep-water rule W1.
 *
 *   water_host_test [--only NAME] [--verbose] [--trace] [--hash] [--pairs N] [--jump N]
 *                   [--map NAME SOUP WATER POINTS [--expect dry|wet]] GDI
 *
 * Built and run by tests/build_water_host.py (a snapshot of build/<target>/libsa1.a and its headers). The context is in
 * Source coordinates (SA1_COORDS_PRESET_SOURCE: Hammer units, z up, 4 HU per SA1 unit) and the collision is prepared as
 * G-SA1 prepares it (sa1_collision_prepare_tris with the defaults, then sa1_collision_set_prepared). The scenes are those of
 * sadc/build/research/water/water_probe.cpp. The host's water is a list of boxes (scenes) or brushes (--map); the flags of a
 * tick come from the state after the previous tick (sa1_state.position + 4 HU, what G-SA1 gives util.PointContents: Review
 * R3.5). "In water" is the game's own probe result, playerwk.shadow.Attr_top & 0x400002 (pwp + 0x1E4), after each tick.
 *
 *   tunnel   a dry room (ceiling 256 HU, slab 64) under 512 HU of water, 6 positions, 60 pairs: KNOWN dry -> 0 of 6 in
 *            water (without flags: 6 of 6, the report)
 *   shelf    water 1640 HU deep over a 32-HU shelf at 1024 HU (Review R4 b), 3 positions under the shelf, 240 pairs:
 *            KNOWN | IN_WATER -> in water 240 of 240 (without flags and in 0.9.1: dry, W1 blocked by the shelf)
 *   twolevel one 256-unit GRID cube holding two water levels (Review R4 a): a pit's pool below the character's floor and a
 *            water layer above the room's ceiling: KNOWN dry -> dry (the cube's box spans the character); dropped into the
 *            pit -> in water at the pit's level with one splash, at most one step after the pit alone without flags (the
 *            two levels are one GRID object), never at the upper level
 *   pool     512 HU of water, 6 positions: the host's flags (IN_WATER) -> 6 of 6 in water, as without flags
 *   enter    dropped into a pool from above with the host's flags, ticks of 1 and 4 pairs: the world byte for byte as
 *            without flags (the tick's dry flag does not drop the water the character fell into: the reference position);
 *            one splash; shallow-water jumps (J0) byte for byte as without flags
 *   e102     E-102 in the tunnel with KNOWN dry: byte for byte as without flags (no water probe; water is land for him)
 *   nohost   no flags given (struct_size before host_flags) and host_flags 0: the same arena and events; --hash prints one
 *            fingerprint line per scene to compare two builds (the build before this change: byte-identical)
 *   sky      W1V: a water layer 4766 units above, a solid ceiling between: breath 0, no water bit, W1 blocked
 *   roof     W1V: a rooftop pool (top 4080 HU) over a room (floor 0, roof 384 HU): breath 0, no water bit, W1 blocked
 *   mover    W1V (Review R3.4): deep water whose only ceiling is a moving object (sa1_collision_object_add, small
 *            triangles out of the probe's own reach): W1 blocked by its triangles (0.9.1: widened through it, in water)
 *   cost     W1V: W1's land test over a column of about 20,000 land triangles under deep water (none on the vertical
 *            line): the time of a tick without flags (the test runs) minus with IN_WATER (no test), < 0.5 ms per step on
 *            x64 (other targets print it)
 *   --map    opt-in owner-map check (skipped cleanly when a file is missing): SOUP is a G-SA1 map cache (.dat) or a raw
 *            sa1_collision_tri soup (.bin), WATER the map's world water brushes as planes (lines "brush ID N", then N lines
 *            "nx ny nz d": inside when every n.q - d <= 0.01, Source's rule), POINTS lines "x y z label"; each point
 *            stands --pairs pairs (60; --jump N: jump for 4 pairs every N) without flags and with the host's flags;
 *            --expect dry checks no pair in water with
 *            the flags, --expect wet that no point the game has in water without flags loses it with them (each with its
 *            splash). The soups of 0.9.2's check: the owner's gm_highway14800 cache, and map_dump (sadc/build/research/
 *            water) of the extracted BSPs for the owner maps whose caches are gone; WATER from bsp_water.py's brushes
 */
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(_WIN32)
#include <time.h>
#endif

#include "libsa1.h"
#include "sa1_internal.h"
#include "sa1_api_world.h"
#include "sa1_world.h"
#include "sa1_coladapt.h"

#if defined(_WIN32)
__declspec(dllimport) int __stdcall QueryPerformanceCounter(long long *c);
__declspec(dllimport) int __stdcall QueryPerformanceFrequency(long long *f);
#endif

static int g_verbose, g_trace, g_hash, g_fail, g_ok, g_pairs_override, g_jump;
#define CHECK(cond, ...)                                                                                                \
    do {                                                                                                                \
        int c_ = (cond) ? 1 : 0;                                                                                        \
        printf(c_ ? "  ok    " : "  FAIL  ");                                                                           \
        printf(__VA_ARGS__);                                                                                            \
        printf("\n");                                                                                                   \
        if (c_) {                                                                                                       \
            g_ok++;                                                                                                     \
        } else {                                                                                                        \
            g_fail++;                                                                                                   \
        }                                                                                                               \
    } while (0)

static double now_ms(void)
{
#if defined(_WIN32)
    static long long f;
    long long c = 0;
    if (f == 0) {
        QueryPerformanceFrequency(&f);
    }
    QueryPerformanceCounter(&c);
    return (double)c * 1000.0 / (double)f;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
#endif
}

static void log_cb(void *user, int32_t level, const char *msg)
{
    (void)user;
    if (level >= SA1_LOG_WARN || g_verbose) {
        printf("    [libsa1] %s\n", msg);
    }
}

/* ---- geometry (Hammer units, z up) ----------------------------------------------------------------------------------- */
typedef struct tbuf {
    sa1_collision_tri *t;
    uint32_t n, cap;
} tbuf;

static void tb_tri(tbuf *b, const float a[3], const float c[3], const float d[3], uint32_t s)
{
    sa1_collision_tri *t;
    if (b->n == b->cap) {
        b->cap = b->cap ? 2u * b->cap : 256u;
        b->t = (sa1_collision_tri *)realloc(b->t, (size_t)b->cap * sizeof *b->t);
    }
    t = &b->t[b->n++];
    memset(t, 0, sizeof *t);
    memcpy(t->v[0], a, 12);
    memcpy(t->v[1], c, 12);
    memcpy(t->v[2], d, 12);
    t->surface = s;
}

/* a quad p0..p3 wound so that its normal points along n */
static void tb_quad(tbuf *b, const float p[4][3], const float n[3], uint32_t s)
{
    float e1[3], e2[3], c[3];
    int k;
    for (k = 0; k < 3; k++) {
        e1[k] = p[1][k] - p[0][k];
        e2[k] = p[2][k] - p[0][k];
    }
    c[0] = e1[1] * e2[2] - e1[2] * e2[1];
    c[1] = e1[2] * e2[0] - e1[0] * e2[2];
    c[2] = e1[0] * e2[1] - e1[1] * e2[0];
    if (c[0] * n[0] + c[1] * n[1] + c[2] * n[2] >= 0.0f) {
        tb_tri(b, p[0], p[1], p[2], s);
        tb_tri(b, p[0], p[2], p[3], s);
    } else {
        tb_tri(b, p[0], p[2], p[1], s);
        tb_tri(b, p[0], p[3], p[2], s);
    }
}

static void tb_flat(tbuf *b, float x0, float x1, float y0, float y1, float z, float up, uint32_t s)
{
    float p[4][3] = { { x0, y0, z }, { x1, y0, z }, { x1, y1, z }, { x0, y1, z } };
    float n[3] = { 0.0f, 0.0f, up };
    tb_quad(b, p, n, s);
}

/* an axis-aligned box, faces outward */
static void tb_box(tbuf *b, float x0, float x1, float y0, float y1, float z0, float z1, uint32_t s)
{
    float nx[3] = { -1, 0, 0 }, px[3] = { 1, 0, 0 }, ny[3] = { 0, -1, 0 }, py[3] = { 0, 1, 0 };
    float X0[4][3] = { { x0, y0, z0 }, { x0, y1, z0 }, { x0, y1, z1 }, { x0, y0, z1 } };
    float X1[4][3] = { { x1, y0, z0 }, { x1, y1, z0 }, { x1, y1, z1 }, { x1, y0, z1 } };
    float Y0[4][3] = { { x0, y0, z0 }, { x1, y0, z0 }, { x1, y0, z1 }, { x0, y0, z1 } };
    float Y1[4][3] = { { x0, y1, z0 }, { x1, y1, z0 }, { x1, y1, z1 }, { x0, y1, z1 } };
    tb_flat(b, x0, x1, y0, y1, z1, 1.0f, s);
    tb_flat(b, x0, x1, y0, y1, z0, -1.0f, s);
    tb_quad(b, X0, nx, s);
    tb_quad(b, X1, px, s);
    tb_quad(b, Y0, ny, s);
    tb_quad(b, Y1, py, s);
}

/* ---- the host's water: boxes (scenes) or convex brushes (--map) ------------------------------------------------------ */
typedef struct hwater {
    uint32_t nbox;
    float box[8][6];            /* x0 x1 y0 y1 z0 z1 */
    uint32_t nbrush, nplane;
    uint32_t *first, *count;    /* per brush: its planes */
    float *plane;               /* 4 per plane: n, d */
    float lo[3], hi[3];         /* the brushes' bounds (a quick test) */
} hwater;

static void hw_box(hwater *h, float x0, float x1, float y0, float y1, float z0, float z1)
{
    float *b = h->box[h->nbox++];
    b[0] = x0;
    b[1] = x1;
    b[2] = y0;
    b[3] = y1;
    b[4] = z0;
    b[5] = z1;
}

/* util.PointContents at the feet + 4 HU has water (G-SA1's WTR-A test) */
static int host_in_water(const hwater *h, const float pos[3])
{
    float q[3] = { pos[0], pos[1], pos[2] + 4.0f };
    uint32_t i, j;
    for (i = 0; i < h->nbox; i++) {
        const float *b = h->box[i];
        if (q[0] >= b[0] && q[0] <= b[1] && q[1] >= b[2] && q[1] <= b[3] && q[2] >= b[4] && q[2] <= b[5]) {
            return 1;
        }
    }
    for (i = 0; i < h->nbrush; i++) {
        int in = 1;
        for (j = 0; j < h->count[i] && in; j++) {
            const float *pl = h->plane + 4u * (h->first[i] + j);
            in = pl[0] * q[0] + pl[1] * q[1] + pl[2] * q[2] - pl[3] <= 0.01f;
        }
        if (in && h->count[i] != 0) {
            return 1;
        }
    }
    return 0;
}

static int install(sa1_context *ctx, const sa1_collision_tri *t, uint32_t n, sa1_collision_prepared_info *out)
{
    sa1_collision_prepared *p = NULL;
    sa1_collision_prepared_info info;
    sa1_status st = sa1_collision_prepare_tris(ctx, t, n, NULL, &p);
    memset(&info, 0, sizeof info);
    info.struct_size = sizeof info;
    if (p != NULL) {
        sa1_collision_prepared_info_get(p, &info);
    }
    if (SA1_FAILED(st) || p == NULL || info.status != SA1_OK) {
        printf("  prepare: %s: %s\n", sa1_status_string(st), p ? info.message : "-");
        sa1_collision_prepared_release(p);
        return -1;
    }
    if (g_verbose) {
        printf("    prepared: %u triangles -> %u GRID triangles in %u objects, %u bytes, %.0f ms\n", info.triangles,
               info.grid_triangles, info.grid_objects, info.bytes, info.ms);
    }
    if (out != NULL) {
        *out = info;
    }
    st = sa1_collision_set_prepared(ctx, p);
    sa1_collision_prepared_release(p);
    return st == SA1_OK ? 0 : -1;
}

static int install_buf(sa1_context *ctx, tbuf *b)
{
    int r = install(ctx, b->t, b->n, NULL);
    free(b->t);
    memset(b, 0, sizeof *b);
    return r;
}

/* ---- running one character --------------------------------------------------------------------------------------------- */
enum { H_NONE, H_ZERO, H_HOST, H_DRY, H_WET };
static const char *const hname[5] = { "no flags", "host_flags 0", "host water", "KNOWN dry", "KNOWN|IN_WATER" };

typedef struct ropt {
    int32_t kind;
    int ticks, per_tick, jump;
    float sx, sy;
    int mode;
    const hwater *hw;
} ropt;

typedef struct rres {
    int ok, ticks, wet, transitions, false_exits, splashes, max_breath, last_breath;
    int first_wet;                  /* the first tick in water (-1: never) */
    uint32_t first_splash;          /* the step of the first splash (0xFFFFFFFF: none) */
    uint32_t known, inwater;        /* ticks whose flags had KNOWN / IN_WATER */
    float level_min, level_max;     /* the water level the game saw (HU) */
    uint64_t ev_hash, ram_hash;
    sa1g_water_stats ws;
#if defined(SA1G_HOSTWATER)
    sa1g_hostwater hw;
#endif
    sa1_state_ex x;
} rres;

static uint64_t mix64(uint64_t h, uint64_t v)
{
    h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
    h *= 0xFF51AFD7ED558CCDull;
    return h ^ (h >> 33);
}

static uint64_t ram_hash(const sa1g_world *w)
{
    uint64_t h = 0xCBF29CE484222325ull, v;
    size_t i;
    if (w == NULL || w->ram == NULL) {
        return 0;
    }
    for (i = 0; i + 8u <= (size_t)SA1G_ARENA_SIZE; i += 8u) {
        memcpy(&v, w->ram + i, 8);
        h = (h ^ v) * 0x100000001B3ull;
    }
    return mix64(h, 0);
}

static void ev_take(sa1_context *ctx, rres *R)
{
    sa1_event ev[64];
    uint32_t n, k;
    do {
        n = 0;
        sa1_poll_events(ctx, ev, 64, &n);
        for (k = 0; k < n; k++) {
            const sa1_event *e = &ev[k];
            uint32_t pb[3];
            memcpy(pb, e->position, sizeof pb);
            R->ev_hash = mix64(R->ev_hash, ((uint64_t)e->type << 32) | e->id);
            R->ev_hash = mix64(R->ev_hash, ((uint64_t)e->bank << 32) | e->arg);
            R->ev_hash = mix64(R->ev_hash, ((uint64_t)e->step << 32) | pb[0]);
            R->ev_hash = mix64(R->ev_hash, ((uint64_t)pb[1] << 32) | pb[2]);
            if (e->type == SA1_EVENT_SOUND && e->id == 0x2CDu) {
                if (R->splashes == 0) {
                    R->first_splash = e->step;
                }
                R->splashes++;
            }
        }
    } while (n == 64);
}

/* the game's water probe result of player 0 (playerwk.shadow: Attr_top +0x08, y_top +0x10) */
static int probe_water(const sa1g_world *w, float *level)
{
    sa1g_player_ptrs pp;
    uint32_t at = 0, yt = 0;
    float y;
    sa1g_world_player(w, &pp);
    if (pp.pwp == 0) {
        return 0;
    }
    sa1g_world_rd32(w, pp.pwp + 0x1DCu + 0x08u, &at);
    sa1g_world_rd32(w, pp.pwp + 0x1DCu + 0x10u, &yt);
    memcpy(&y, &yt, 4);
    *level = y * 4.0f;
    return (at & 0x400002u) != 0;
}

static int run_char(sa1_context *ctx, const float pos[3], const ropt *o, rres *R)
{
    sa1_character_desc d;
    sa1_character_id id = 0;
    sa1_status st;
    sa1g_world *w;
    float last[3], prev_level = 0.0f;
    int t, prev_wet = -1;
    memset(R, 0, sizeof *R);
    R->first_wet = -1;
    R->first_splash = 0xFFFFFFFFu;
    R->level_min = 1e30f;
    R->level_max = -1e30f;
    R->ev_hash = 0x84222325CBF29CE4ull;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.kind = o->kind;
    d.flags = SA1_CREATE_WORLD;
    memcpy(d.position, pos, sizeof d.position);
    st = sa1_character_create_ex(ctx, &d, &id);
    if (SA1_FAILED(st)) {
        printf("  create at (%.0f %.0f %.0f): %s: %s\n", pos[0], pos[1], pos[2], sa1_status_string(st), sa1_last_error(ctx));
        return -1;
    }
    ev_take(ctx, R);
    w = sa1_character_world(ctx, id);
    memcpy(last, pos, sizeof last);
    for (t = 0; t < o->ticks; t++) {
        sa1_input in;
        float level = 0.0f;
        int wet;
        memset(&in, 0, sizeof in);
        in.struct_size = o->mode == H_NONE ? (uint32_t)offsetof(sa1_input, host_flags) : (uint32_t)sizeof in;
        in.stick_x = o->sx;
        in.stick_y = o->sy;
        if (o->jump > 0 && t >= 8 && (t % o->jump) < 4) {
            in.buttons = SA1_BUTTON_JUMP;
        }
        if (o->mode == H_HOST) {
            in.host_flags = SA1_INPUT_HOST_WATER_KNOWN | (host_in_water(o->hw, last) ? SA1_INPUT_HOST_IN_WATER : 0u);
        } else if (o->mode == H_DRY) {
            in.host_flags = SA1_INPUT_HOST_WATER_KNOWN;
        } else if (o->mode == H_WET) {
            in.host_flags = SA1_INPUT_HOST_WATER_KNOWN | SA1_INPUT_HOST_IN_WATER;
        }
        R->known += (in.host_flags & SA1_INPUT_HOST_WATER_KNOWN) != 0;
        R->inwater += (in.host_flags & SA1_INPUT_HOST_IN_WATER) != 0;
        st = sa1_character_tick(ctx, id, &in, (float)o->per_tick / 30.0f, NULL, NULL);
        if (st != SA1_OK) {
            printf("  tick %d: %s: %s\n", t, sa1_status_string(st), sa1_last_error(ctx));
            sa1_character_destroy(ctx, id);
            return -1;
        }
        memset(&R->x, 0, sizeof R->x);
        R->x.struct_size = sizeof R->x;
        sa1_character_state_ex(ctx, id, &R->x);
        memcpy(last, R->x.position, sizeof last);
        ev_take(ctx, R);
        wet = probe_water(w, &level);
        R->ticks++;
        R->wet += wet;
        if (wet) {
            if (R->first_wet < 0) {
                R->first_wet = t;
            }
            R->level_min = level < R->level_min ? level : R->level_min;
            R->level_max = level > R->level_max ? level : R->level_max;
        }
        if (prev_wet >= 0 && wet != prev_wet) {
            R->transitions++;
            if (!wet && R->x.position[2] + 4.0f < prev_level - 2.0f) {
                R->false_exits++;   /* the surface seen before is still above the probe point */
            }
        }
        if (wet) {
            prev_level = level;
        }
        prev_wet = wet;
        if (R->x.breath_timer > R->max_breath) {
            R->max_breath = R->x.breath_timer;
        }
        R->last_breath = R->x.breath_timer;
        if (g_trace) {
            printf("    tick %4d flags %u pos (%8.1f %8.1f %8.1f) %s level %8.1f breath %4d act %3d\n", t,
                   (unsigned)in.host_flags, R->x.position[0], R->x.position[1], R->x.position[2], wet ? "WATER" : "-    ",
                   level, (int)R->x.breath_timer, (int)R->x.action);
        }
    }
    sa1ca_world_water(w, &R->ws);
#if defined(SA1G_HOSTWATER)
    R->hw = w->hostwater;
#endif
    R->ram_hash = ram_hash(w);
    sa1_character_destroy(ctx, id);
    ev_take(ctx, R);
    R->ok = 1;
    return 0;
}

static void ropt_init(ropt *o, int32_t kind, int ticks, int mode, const hwater *hw)
{
    memset(o, 0, sizeof *o);
    o->kind = kind;
    o->ticks = g_pairs_override > 0 ? g_pairs_override : ticks;
    o->per_tick = 1;
    o->mode = mode;
    o->hw = hw;
}

static void print_run(const char *what, const float pos[3], int mode, const rres *R)
{
    printf("    %-24s (%6.0f %6.0f %6.0f) %-14s in water %3d/%3d, transitions %d (false exits %d), splashes %d, "
           "breath max %d, W1 found %u widened %u blocked %u", what, pos[0], pos[1], pos[2], hname[mode], R->wet, R->ticks,
           R->transitions, R->false_exits, R->splashes, R->max_breath, (unsigned)R->ws.found, (unsigned)R->ws.widened,
           (unsigned)R->ws.blocked);
#if defined(SA1G_HOSTWATER)
    printf("; host: dry %u (left out %u, entered %u), wet %u (through %u, far %u), moving blocks %u",
           (unsigned)R->hw.dry_steps, (unsigned)R->hw.dry_left_out, (unsigned)R->hw.dry_entered, (unsigned)R->hw.wet_steps,
           (unsigned)R->hw.wet_through, (unsigned)R->hw.wet_far, (unsigned)R->hw.moving_blocked);
#endif
    if (R->wet) {
        printf("; level %.1f..%.1f HU", R->level_min, R->level_max);
    }
    printf("\n");
}

/* ---- scenes (water_probe.cpp's, Hammer units) ------------------------------------------------------------------------- */
#define W_ 4096.0f
static const float kPos6[6][2] = { { 8, 8 }, { 512, 8 }, { 512, 512 }, { -300, 200 }, { 300, -200 }, { 900, 900 } };

static int scene_tunnel(sa1_context *ctx, hwater *h, float hc, float slab, float depth)
{
    tbuf b;
    memset(&b, 0, sizeof b);
    memset(h, 0, sizeof *h);
    tb_box(&b, -W_, W_, -W_, W_, -256, 0, SA1_SURFACE_SOLID);
    tb_box(&b, -W_, W_, -W_, W_, hc, hc + slab, SA1_SURFACE_SOLID);
    tb_box(&b, -W_, W_, -W_, W_, hc + slab, hc + slab + depth, SA1_SURFACE_WATER);
    hw_box(h, -W_, W_, -W_, W_, hc + slab, hc + slab + depth);
    return install_buf(ctx, &b);
}

static int scene_pool(sa1_context *ctx, hwater *h, float depth)
{
    tbuf b;
    memset(&b, 0, sizeof b);
    memset(h, 0, sizeof *h);
    tb_box(&b, -W_, W_, -W_, W_, -256, 0, SA1_SURFACE_SOLID);
    tb_box(&b, -W_, W_, -W_, W_, 0, depth, SA1_SURFACE_WATER);
    hw_box(h, -W_, W_, -W_, W_, 0, depth);
    return install_buf(ctx, &b);
}

static int scene_shelf(sa1_context *ctx, hwater *h, float depth, float hc, float slab)
{
    tbuf b;
    memset(&b, 0, sizeof b);
    memset(h, 0, sizeof *h);
    tb_box(&b, -W_, W_, -W_, W_, -256, 0, SA1_SURFACE_SOLID);
    tb_box(&b, 0, W_, -W_, W_, hc, hc + slab, SA1_SURFACE_SOLID);
    tb_box(&b, -W_, W_, -W_, W_, 0, depth, SA1_SURFACE_WATER);
    hw_box(h, -W_, W_, -W_, W_, 0, depth);
    return install_buf(ctx, &b);
}

/* one GRID cube (x, y 0..1024, z 0..1024 HU: 256 SA1 units) with two water levels: a pit's pool (floor 0, top 32) next to
   the character's raised floor (top 128), and a water layer (top 768) over the room's ceiling slab (512..576) */
static int scene_twolevel_ex(sa1_context *ctx, hwater *h, int upper)
{
    tbuf b;
    memset(&b, 0, sizeof b);
    memset(h, 0, sizeof *h);
    tb_box(&b, -W_, W_, -W_, W_, -256, 0, SA1_SURFACE_SOLID);
    tb_box(&b, -W_, 600, -W_, W_, 0, 128, SA1_SURFACE_SOLID);
    tb_box(&b, 1000, W_, -W_, W_, 0, 128, SA1_SURFACE_SOLID);
    tb_box(&b, 600, 1000, -W_, 100, 0, 128, SA1_SURFACE_SOLID);
    tb_box(&b, 600, 1000, 900, W_, 0, 128, SA1_SURFACE_SOLID);
    tb_box(&b, 600, 1000, 100, 900, 0, 32, SA1_SURFACE_WATER);
    tb_box(&b, -W_, W_, -W_, W_, 512, 576, SA1_SURFACE_SOLID);
    hw_box(h, 600, 1000, 100, 900, 0, 32);
    if (upper) {
        tb_box(&b, -W_, W_, -W_, W_, 576, 768, SA1_SURFACE_WATER);
        hw_box(h, -W_, W_, -W_, W_, 576, 768);
    }
    return install_buf(ctx, &b);
}

static int scene_twolevel(sa1_context *ctx, hwater *h)
{
    return scene_twolevel_ex(ctx, h, 1);
}

/* W1V: a water layer 4766 SA1 units (19064 HU) above the character, 48 HU thick (gm_highway14800's 3D-skybox sea), with a
   solid ceiling slab between */
static int scene_sky(sa1_context *ctx, hwater *h)
{
    tbuf b;
    memset(&b, 0, sizeof b);
    memset(h, 0, sizeof *h);
    tb_box(&b, -W_, W_, -W_, W_, -256, 0, SA1_SURFACE_SOLID);
    tb_box(&b, -W_, W_, -W_, W_, 400, 464, SA1_SURFACE_SOLID);
    tb_box(&b, -W_, W_, -W_, W_, 19064, 19112, SA1_SURFACE_WATER);
    hw_box(h, -W_, W_, -W_, W_, 19064, 19112);
    return install_buf(ctx, &b);
}

/* W1V: rp_nycity's rooftop pool (top 4080 HU) over a room (floor 0, the building's roof 384..448) */
static int scene_roof(sa1_context *ctx, hwater *h)
{
    tbuf b;
    memset(&b, 0, sizeof b);
    memset(h, 0, sizeof *h);
    tb_box(&b, -W_, W_, -W_, W_, -256, 0, SA1_SURFACE_SOLID);
    tb_box(&b, -2048, 2048, -2048, 2048, 384, 448, SA1_SURFACE_SOLID);
    tb_box(&b, -1024, 1024, -1024, 1024, 3900, 3950, SA1_SURFACE_SOLID);
    tb_box(&b, -1024, 1024, -1024, 1024, 3950, 4080, SA1_SURFACE_WATER);
    hw_box(h, -1024, 1024, -1024, 1024, 3950, 4080);
    return install_buf(ctx, &b);
}

/* W1V (Review R3.4): deep water (top 2448 HU, 612 units) with no GRID land between it and the floor; the ceiling at 400 HU
   is a moving object: one upward face of 64-HU quads (16 units: out of 8C02D2E0's own reach, about 32 units) */
static int scene_mover(sa1_context *ctx, hwater *h, uint32_t *obj)
{
    tbuf b, m;
    float at[3] = { 512.0f, 512.0f, 400.0f };
    int i, j;
    sa1_status st;
    memset(&b, 0, sizeof b);
    memset(&m, 0, sizeof m);
    memset(h, 0, sizeof *h);
    tb_box(&b, -W_, W_, -W_, W_, -256, 0, SA1_SURFACE_SOLID);
    tb_box(&b, -W_, W_, -W_, W_, 2400, 2448, SA1_SURFACE_WATER);
    hw_box(h, -W_, W_, -W_, W_, 2400, 2448);
    if (install_buf(ctx, &b) != 0) {
        return -1;
    }
    for (i = -16; i < 16; i++) {
        for (j = -16; j < 16; j++) {
            tb_flat(&m, 64.0f * (float)i, 64.0f * (float)(i + 1), 64.0f * (float)j, 64.0f * (float)(j + 1), 0.0f, 1.0f,
                    SA1_SURFACE_SOLID);
        }
    }
    st = sa1_collision_object_add(ctx, m.t, m.n, at, NULL, obj);
    free(m.t);
    if (SA1_FAILED(st)) {
        printf("  object_add: %s: %s\n", sa1_status_string(st), sa1_last_error(ctx));
        return -1;
    }
    return 0;
}

/* W1V cost: deep water (top 2400 HU, 600 units) and, in the character's cell column (x, y 0..1024), ten levels of 32-HU
   tiles (z 200..2000, a 0..4 HU jitter so that nothing merges) with a 64-HU hole over the character at (512 512):
   about 20,000 land triangles whose boxes cover the character, none on its vertical line */
static int scene_cost(sa1_context *ctx, hwater *h, sa1_collision_prepared_info *info)
{
    tbuf b;
    int L, i, j, r;
    memset(&b, 0, sizeof b);
    memset(h, 0, sizeof *h);
    tb_box(&b, -W_, W_, -W_, W_, -256, 0, SA1_SURFACE_SOLID);
    tb_box(&b, -W_, W_, -W_, W_, 0, 2400, SA1_SURFACE_WATER);
    hw_box(h, -W_, W_, -W_, W_, 0, 2400);
    for (L = 1; L <= 10; L++) {
        for (i = 0; i < 32; i++) {
            for (j = 0; j < 32; j++) {
                float z = 200.0f * (float)L + (float)((i * 7 + j * 3) % 5);
                if ((i == 15 || i == 16) && (j == 15 || j == 16)) {
                    continue;
                }
                tb_flat(&b, 32.0f * (float)i, 32.0f * (float)(i + 1), 32.0f * (float)j, 32.0f * (float)(j + 1), z, 1.0f,
                        SA1_SURFACE_SOLID);
            }
        }
    }
    r = install(ctx, b.t, b.n, info);
    free(b.t);
    return r;
}

/* ---- the tests ------------------------------------------------------------------------------------------------------ */
static void t_tunnel(sa1_context *ctx)
{
    hwater h;
    int p, m, wetpos[2] = { 0, 0 }, wetpairs[2] = { 0, 0 };
    uint32_t left = 0;
    printf("tunnel: a dry room (floor 0, ceiling slab 256..320 HU) under water up to 832 HU, Sonic standing 60 pairs\n");
    if (scene_tunnel(ctx, &h, 256, 64, 512) != 0) {
        g_fail++;
        return;
    }
    for (p = 0; p < 6; p++) {
        float pos[3] = { kPos6[p][0], kPos6[p][1], 4.0f };
        for (m = 0; m < 2; m++) {
            ropt o;
            rres R;
            ropt_init(&o, SA1_CHAR_SONIC, 60, m == 0 ? H_NONE : H_HOST, &h);
            if (run_char(ctx, pos, &o, &R) != 0) {
                g_fail++;
                continue;
            }
            print_run("tunnel 256/64/512", pos, o.mode, &R);
            wetpos[m] += R.wet > 0;
            wetpairs[m] += R.wet;
#if defined(SA1G_HOSTWATER)
            if (m == 1) {
                left += R.hw.dry_left_out;
            }
#endif
            if (m == 1 && R.inwater != 0) {
                printf("  note: the host's boxes gave IN_WATER at (%.0f %.0f)\n", pos[0], pos[1]);
            }
        }
    }
    CHECK(wetpos[0] == 6, "without flags the game's rule: %d of 6 positions in water (the report: 6 of 6)", wetpos[0]);
    CHECK(wetpos[1] == 0 && wetpairs[1] == 0, "KNOWN dry: %d of 6 positions in water (%d pairs); water objects left out of "
          "the window %u times", wetpos[1], wetpairs[1], (unsigned)left);
}

static void t_shelf(sa1_context *ctx)
{
    static const float at[3][2] = { { 512, 512 }, { 300, -200 }, { 900, 900 } };
    hwater h;
    int p;
    printf("shelf: water 1640 HU deep, a 32-HU shelf at 1024 HU over x > 0, Sonic standing under it 240 pairs (Review R4 b)\n");
    if (scene_shelf(ctx, &h, 1640, 1024, 32) != 0) {
        g_fail++;
        return;
    }
    for (p = 0; p < 3; p++) {
        float pos[3] = { at[p][0], at[p][1], 4.0f };
        ropt o;
        rres R0, R1;
        ropt_init(&o, SA1_CHAR_SONIC, 240, H_NONE, &h);
        if (run_char(ctx, pos, &o, &R0) != 0) {
            g_fail++;
            continue;
        }
        print_run("shelf 1640/1024/32", pos, o.mode, &R0);
        o.mode = H_HOST;
        if (run_char(ctx, pos, &o, &R1) != 0) {
            g_fail++;
            continue;
        }
        print_run("shelf 1640/1024/32", pos, o.mode, &R1);
        CHECK(R0.wet == 0 && R0.ws.blocked > 0, "(%.0f %.0f) without flags: in water %d/%d, W1 blocked %u (0.9.1's land test: "
              "the shelf)", pos[0], pos[1], R0.wet, R0.ticks, (unsigned)R0.ws.blocked);
        CHECK(R1.inwater == (uint32_t)R1.ticks && R1.wet == R1.ticks && R1.ws.blocked == 0 && R1.max_breath > 300,
              "(%.0f %.0f) KNOWN | IN_WATER: in water %d/%d pairs, W1 blocked %u, widened %u, breath %d", pos[0], pos[1],
              R1.wet, R1.ticks, (unsigned)R1.ws.blocked, (unsigned)R1.ws.widened, R1.max_breath);
    }
}

static float rdf(const void *p)
{
    float v;
    memcpy(&v, p, 4);
    return v;
}

static uint32_t rdu(const void *p)
{
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

/* the GRID water objects of the character's column whose box covers its x / z and spans its probe point (SA1 space):
   a "box bottom above the character" test would keep them (Review R4 a); each is printed; *with_above counts them too */
static uint32_t spanning_water(sa1_context *ctx, const float pos[3], uint32_t *with_above)
{
    sa1_character_desc d;
    sa1_character_id id = 0;
    sa1ca_world_stats cs;
    sa1_state_ex x;
    sa1_input in;
    const sa1g_world *w;
    const uint8_t *gh, *cols, *hash, *boxes;
    uint32_t hsize, nobj, h, probes, first = 0, cnt = 0, k, n = 0;
    float cell, p[3];
    int32_t ix, iz;
    *with_above = 0;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.kind = SA1_CHAR_SONIC;
    d.flags = SA1_CREATE_WORLD;
    memcpy(d.position, pos, sizeof d.position);
    if (SA1_FAILED(sa1_character_create_ex(ctx, &d, &id))) {
        return 0;
    }
    memset(&in, 0, sizeof in);
    in.struct_size = sizeof in;
    sa1_character_tick(ctx, id, &in, 1.0f / 30.0f, NULL, NULL);
    memset(&x, 0, sizeof x);
    x.struct_size = sizeof x;
    sa1_character_state_ex(ctx, id, &x);
    memcpy(p, x.sa1_position, sizeof p);
    w = sa1_character_world(ctx, id);
    sa1ca_world_stats_get(w, &cs);
    if (cs.grid == 0) {
        sa1_character_destroy(ctx, id);
        return 0;
    }
    gh = (const uint8_t *)sa1g_world_ptr(w, cs.grid);
    hsize = rdu(gh + SA1CA_GH_HASH_SIZE);
    nobj = rdu(gh + SA1CA_GH_OBJECTS);
    cols = (const uint8_t *)sa1g_world_ptr(w, rdu(gh + SA1CA_GH_COLS));
    hash = (const uint8_t *)sa1g_world_ptr(w, rdu(gh + SA1CA_GH_HASH));
    boxes = (const uint8_t *)sa1g_world_ptr(w, rdu(gh + SA1CA_GH_BOXES));
    cell = rdf(gh + SA1CA_GH_CELL);
    ix = (int32_t)floorf(p[0] / cell);
    iz = (int32_t)floorf(p[2] / cell);
    for (h = sa1ca_grid_hash(ix, iz) & (hsize - 1u), probes = 0; hsize && probes < hsize; h = (h + 1u) & (hsize - 1u),
        probes++) {
        const uint8_t *he = hash + SA1CA_GRID_HASH_ENTRY * h;
        if (rdu(he + 12) == 0) {
            break;
        }
        if ((int32_t)rdu(he) == ix && (int32_t)rdu(he + 4) == iz) {
            first = rdu(he + 8);
            cnt = rdu(he + 12);
            break;
        }
    }
    for (k = 0; k < cnt && first + k < nobj && boxes != NULL; k++) {
        const uint8_t *c = cols + SA1CA_COL_SIZE * (first + k), *bx = boxes + 24u * (first + k);
        if (!(rdu(c + 0x20) & 0x400002u) || p[0] < rdf(bx) || p[0] > rdf(bx + 12) || p[2] < rdf(bx + 8) ||
            p[2] > rdf(bx + 20) || !(rdf(bx + 4) < p[1] + 1.0f) || !(rdf(bx + 16) > p[1] + 1.0f)) {
            continue;
        }
        n++;
        printf("    GRID water object %u: box y %.1f .. %.1f (SA1 units) around the probe point %.1f\n",
               (unsigned)(first + k), rdf(bx + 4), rdf(bx + 16), p[1] + 1.0f);
        *with_above += 1;
    }
    sa1_character_destroy(ctx, id);
    {
        sa1_event ev[64];
        uint32_t m = 0;
        do {
            sa1_poll_events(ctx, ev, 64, &m);
        } while (m == 64);
    }
    return n;
}

/* the land triangles W1's land test walks at pos (SA1 space after one pair): GRID land objects of the 3 x 3 columns whose
   box covers the character's x / z and reaches into (probe point, ytop) (cw_water_blocked's selection) */
static uint32_t land_test_tris(sa1_context *ctx, const float pos[3], float ytop, uint32_t *objs)
{
    sa1_character_desc d;
    sa1_character_id id = 0;
    sa1ca_world_stats cs;
    sa1_state_ex x;
    sa1_input in;
    const sa1g_world *w;
    const uint8_t *gh, *cols, *hash, *boxes, *polys;
    uint32_t hsize, nobj, tris = 0;
    float cell, p[3];
    int32_t ix, iz, cx, cz;
    *objs = 0;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.kind = SA1_CHAR_SONIC;
    d.flags = SA1_CREATE_WORLD;
    memcpy(d.position, pos, sizeof d.position);
    if (SA1_FAILED(sa1_character_create_ex(ctx, &d, &id))) {
        return 0;
    }
    memset(&in, 0, sizeof in);
    in.struct_size = sizeof in;
    sa1_character_tick(ctx, id, &in, 1.0f / 30.0f, NULL, NULL);
    memset(&x, 0, sizeof x);
    x.struct_size = sizeof x;
    sa1_character_state_ex(ctx, id, &x);
    memcpy(p, x.sa1_position, sizeof p);
    w = sa1_character_world(ctx, id);
    sa1ca_world_stats_get(w, &cs);
    gh = cs.grid ? (const uint8_t *)sa1g_world_ptr(w, cs.grid) : NULL;
    if (gh == NULL || rdu(gh + SA1CA_GH_BOXES) == 0) {
        sa1_character_destroy(ctx, id);
        return 0;
    }
    hsize = rdu(gh + SA1CA_GH_HASH_SIZE);
    nobj = rdu(gh + SA1CA_GH_OBJECTS);
    cols = (const uint8_t *)sa1g_world_ptr(w, rdu(gh + SA1CA_GH_COLS));
    polys = (const uint8_t *)sa1g_world_ptr(w, rdu(gh + SA1CA_GH_POLYS));
    hash = (const uint8_t *)sa1g_world_ptr(w, rdu(gh + SA1CA_GH_HASH));
    boxes = (const uint8_t *)sa1g_world_ptr(w, rdu(gh + SA1CA_GH_BOXES));
    cell = rdf(gh + SA1CA_GH_CELL);
    cx = (int32_t)floorf(p[0] / cell);
    cz = (int32_t)floorf(p[2] / cell);
    for (ix = cx - 1; ix <= cx + 1; ix++) {
        for (iz = cz - 1; iz <= cz + 1; iz++) {
            uint32_t h, probes, first = 0, cnt = 0, k;
            for (h = sa1ca_grid_hash(ix, iz) & (hsize - 1u), probes = 0; hsize && probes < hsize;
                 h = (h + 1u) & (hsize - 1u), probes++) {
                const uint8_t *he = hash + SA1CA_GRID_HASH_ENTRY * h;
                if (rdu(he + 12) == 0) {
                    break;
                }
                if ((int32_t)rdu(he) == ix && (int32_t)rdu(he + 4) == iz) {
                    first = rdu(he + 8);
                    cnt = rdu(he + 12);
                    break;
                }
            }
            for (k = 0; k < cnt && first + k < nobj; k++) {
                const uint8_t *c = cols + SA1CA_COL_SIZE * (first + k), *bx = boxes + 24u * (first + k);
                uint32_t attr = rdu(c + 0x20);
                if (!(attr & 1u) || (attr & 0x400002u) || p[0] < rdf(bx) || p[0] > rdf(bx + 12) || p[2] < rdf(bx + 8) ||
                    p[2] > rdf(bx + 20) || !(rdf(bx + 16) > p[1] + 1.0f) || !(rdf(bx + 4) < ytop)) {
                    continue;
                }
                (*objs)++;
                tris += rdu(polys + 4u * (first + k));
            }
        }
    }
    sa1_character_destroy(ctx, id);
    {
        sa1_event ev[64];
        uint32_t m = 0;
        do {
            sa1_poll_events(ctx, ev, 64, &m);
        } while (m == 64);
    }
    return tris;
}

static void t_twolevel(sa1_context *ctx)
{
    static const float at[2][2] = { { 200, 500 }, { 400, 300 } };
    hwater h;
    int p;
    printf("twolevel: one 256-unit GRID cube with two water levels (a pit's pool at 32 HU, a layer at 768 HU over the room's "
           "ceiling), Sonic on the floor at 128 HU between them, 60 pairs (Review R4 a)\n");
    if (scene_twolevel(ctx, &h) != 0) {
        g_fail++;
        return;
    }
    for (p = 0; p < 2; p++) {
        float pos[3] = { at[p][0], at[p][1], 132.0f };
        ropt o;
        rres R0, R1;
        ropt_init(&o, SA1_CHAR_SONIC, 60, H_NONE, &h);
        if (run_char(ctx, pos, &o, &R0) != 0 || (o.mode = H_HOST, run_char(ctx, pos, &o, &R1)) != 0) {
            g_fail++;
            continue;
        }
        print_run("twolevel floor", pos, H_NONE, &R0);
        print_run("twolevel floor", pos, H_HOST, &R1);
        if (p == 0) {
            uint32_t above = 0, span = spanning_water(ctx, pos, &above);
            CHECK(span > 0, "the cube's water object spans the character (%u of them): a box test would keep it", span);
        }
        CHECK(R0.wet == R0.ticks, "(%.0f %.0f) without flags: in water %d/%d (the game sees the upper level, %.0f HU)", pos[0],
              pos[1], R0.wet, R0.ticks, R0.level_max);
        CHECK(R1.wet == 0 && R1.inwater == 0, "(%.0f %.0f) KNOWN dry: in water %d/%d", pos[0], pos[1], R1.wet, R1.ticks);
    }
    {
        /* dropped into the pit's pool from above (the upper layer is over it from the start): the water below is not left
           out. The reference is the same scene without the upper layer, without flags: there the game sees only the pit */
        float pos[3] = { 800.0f, 500.0f, 300.0f };
        ropt o;
        rres R0, R1, Rref;
        int k, ticks[2] = { 90, 30 };
        for (k = 0; k < 2; k++) {
            int per = k == 0 ? 1 : 4;
            if (scene_twolevel_ex(ctx, &h, 0) != 0) {
                g_fail++;
                return;
            }
            ropt_init(&o, SA1_CHAR_SONIC, ticks[k], H_NONE, &h);
            o.per_tick = per;
            if (run_char(ctx, pos, &o, &Rref) != 0 || scene_twolevel(ctx, &h) != 0) {
                g_fail++;
                return;
            }
            if (run_char(ctx, pos, &o, &R0) != 0 || (o.mode = H_HOST, run_char(ctx, pos, &o, &R1)) != 0) {
                g_fail++;
                return;
            }
            print_run("pit only (reference)", pos, H_NONE, &Rref);
            print_run("twolevel drop into pit", pos, H_NONE, &R0);
            print_run("twolevel drop into pit", pos, H_HOST, &R1);
            /* the pit's surface and the upper layer are one GRID object: while the character is above the pit's surface
               the object is out of the copy (the upper layer is over it), so the entry is seen one step later than in
               the pit alone (the next refresh, with the surface over the probe point, keeps it: cw_water_dry's
               "entered"); never water from the upper layer */
            CHECK(Rref.splashes == 1 && R1.splashes == 1 && R1.first_splash >= Rref.first_splash &&
                      R1.first_splash <= Rref.first_splash + 1u && R1.first_wet == Rref.first_wet && R1.wet == Rref.wet &&
                      R1.level_min > 31.0f && R1.level_max < 33.0f && R1.inwater > 0,
                  "%d pair(s) per tick, dropped into the pit: splash at step %d (the pit alone without flags: %d, at most "
                  "one step later; with the upper layer without flags: %d, wet from pair %d), %d splash(es), in water %d "
                  "pairs from pair %d (%d, %d), level %.0f..%.0f HU", per, (int)R1.first_splash, (int)Rref.first_splash,
                  (int)R0.first_splash, R0.first_wet, R1.splashes, R1.wet, R1.first_wet, Rref.wet, Rref.first_wet,
                  R1.level_min, R1.level_max);
        }
    }
}

static void t_pool(sa1_context *ctx)
{
    hwater h;
    int p, all = 0;
    printf("pool: 512 HU of water, Sonic on the floor, 60 pairs, the host's flags\n");
    if (scene_pool(ctx, &h, 512) != 0) {
        g_fail++;
        return;
    }
    for (p = 0; p < 6; p++) {
        float pos[3] = { kPos6[p][0], kPos6[p][1], 4.0f };
        ropt o;
        rres R0, R1;
        ropt_init(&o, SA1_CHAR_SONIC, 60, H_NONE, &h);
        if (run_char(ctx, pos, &o, &R0) != 0 || (o.mode = H_HOST, run_char(ctx, pos, &o, &R1)) != 0) {
            g_fail++;
            continue;
        }
        print_run("pool 512", pos, H_HOST, &R1);
        all += R1.wet == R1.ticks && R1.inwater == (uint32_t)R1.ticks && R1.ram_hash == R0.ram_hash &&
               R1.ev_hash == R0.ev_hash;
    }
    CHECK(all == 6, "KNOWN | IN_WATER from the host's water: %d of 6 positions in water every pair, the world byte for byte "
          "as without flags", all);
}

static void t_enter(sa1_context *ctx)
{
    hwater h;
    int k;
    printf("enter: Sonic dropped from 900 HU into a pool (top 512 HU), the host's flags, ticks of 1 and 4 pairs\n");
    if (scene_pool(ctx, &h, 512) != 0) {
        g_fail++;
        return;
    }
    for (k = 0; k < 2; k++) {
        float pos[3] = { 512.0f, 512.0f, 900.0f };
        int per = k == 0 ? 1 : 4;
        ropt o;
        rres R0, R1;
        ropt_init(&o, SA1_CHAR_SONIC, 120 / per, H_NONE, &h);
        o.per_tick = per;
        if (run_char(ctx, pos, &o, &R0) != 0 || (o.mode = H_HOST, run_char(ctx, pos, &o, &R1)) != 0) {
            g_fail++;
            continue;
        }
        print_run(per == 1 ? "enter, 1 pair/tick" : "enter, 4 pairs/tick", pos, H_NONE, &R0);
        print_run(per == 1 ? "enter, 1 pair/tick" : "enter, 4 pairs/tick", pos, H_HOST, &R1);
        CHECK(R1.splashes == 1 && R1.first_splash == R0.first_splash && R1.transitions == 1 && R1.ram_hash == R0.ram_hash &&
                  R1.ev_hash == R0.ev_hash && R1.known == (uint32_t)R1.ticks && R1.inwater > 0,
              "%d pair(s) per tick: one splash at step %d (without flags %d), %d transition(s); the world byte for byte as "
              "without flags (arena %s, events %s)", per, (int)R1.first_splash, (int)R0.first_splash, R1.transitions,
              R1.ram_hash == R0.ram_hash ? "same" : "DIFFERENT", R1.ev_hash == R0.ev_hash ? "same" : "DIFFERENT");
    }
    if (scene_pool(ctx, &h, 64) != 0) {
        g_fail++;
        return;
    }
    for (k = 0; k < 2; k++) {
        float pos[3] = { 300.0f, -200.0f, 4.0f };
        int per = k == 0 ? 1 : 4;
        ropt o;
        rres R0, R1;
        ropt_init(&o, SA1_CHAR_SONIC, 240 / per, H_NONE, &h);
        o.per_tick = per;
        o.jump = per == 1 ? 30 : 8;
        if (run_char(ctx, pos, &o, &R0) != 0 || (o.mode = H_HOST, run_char(ctx, pos, &o, &R1)) != 0) {
            g_fail++;
            continue;
        }
        print_run("shallow 64, jumps", pos, H_HOST, &R1);
        CHECK(R0.splashes > 1 && R1.splashes == R0.splashes && R1.transitions == R0.transitions &&
                  R1.ram_hash == R0.ram_hash && R1.ev_hash == R0.ev_hash && R1.inwater > 0 &&
                  R1.inwater < (uint32_t)R1.ticks,
              "shallow water (J0), %d pair(s) per tick, jumps: %d splashes, %d transitions (without flags %d, %d), the world "
              "byte for byte as without flags", per, R1.splashes, R1.transitions, R0.splashes, R0.transitions);
    }
}

static void t_e102(sa1_context *ctx)
{
    hwater h;
    float pos[3] = { 512.0f, 512.0f, 4.0f };
    ropt o;
    rres R0, R1;
    printf("e102: E-102 in the tunnel, KNOWN dry\n");
    if (scene_tunnel(ctx, &h, 256, 64, 512) != 0) {
        g_fail++;
        return;
    }
    ropt_init(&o, SA1_CHAR_GAMMA, 60, H_NONE, &h);
    if (run_char(ctx, pos, &o, &R0) != 0 || (o.mode = H_DRY, run_char(ctx, pos, &o, &R1)) != 0) {
        g_fail++;
        return;
    }
    print_run("e102 tunnel", pos, H_DRY, &R1);
    CHECK(R1.max_breath == 0 && R1.ram_hash == R0.ram_hash && R1.ev_hash == R0.ev_hash,
          "E-102: breath %d, the world byte for byte as without flags (the window keeps his water objects: land for him)",
          R1.max_breath);
}

/* the no-flag fingerprints: arena + events after each scene, without flags and with host_flags 0 */
static void t_nohost(sa1_context *ctx)
{
    hwater h;
    int s, same = 0, total = 0;
    printf("nohost: every scene without flags (struct_size before host_flags) and with host_flags 0\n");
    for (s = 0; s < 7; s++) {
        float pos[3] = { 512.0f, 512.0f, 4.0f };
        const char *name = "";
        int r = -1, ticks = 60;
        uint32_t obj = 0;
        ropt o;
        rres R0, R1;
        switch (s) {
        case 0: name = "tunnel"; r = scene_tunnel(ctx, &h, 256, 64, 512); break;
        case 1: name = "shelf"; r = scene_shelf(ctx, &h, 1640, 1024, 32); break;
        case 2: name = "pool"; r = scene_pool(ctx, &h, 512); break;
        case 3: name = "twolevel"; r = scene_twolevel(ctx, &h); pos[0] = 200.0f; pos[1] = 500.0f; pos[2] = 132.0f; break;
        case 4: name = "sky"; r = scene_sky(ctx, &h); break;
        case 5: name = "roof"; r = scene_roof(ctx, &h); break;
        default: name = "enter"; r = scene_pool(ctx, &h, 512); pos[2] = 900.0f; ticks = 120; break;
        }
        if (r != 0) {
            g_fail++;
            continue;
        }
        ropt_init(&o, SA1_CHAR_SONIC, ticks, H_NONE, &h);
        if (run_char(ctx, pos, &o, &R0) != 0 || (o.mode = H_ZERO, run_char(ctx, pos, &o, &R1)) != 0) {
            g_fail++;
            continue;
        }
        total++;
        same += R0.ram_hash == R1.ram_hash && R0.ev_hash == R1.ev_hash;
        if (g_hash) {
            printf("HASH %-9s arena %016llX events %016llX wet %d/%d splashes %d W1 %u/%u/%u/%u\n", name,
                   (unsigned long long)R0.ram_hash, (unsigned long long)R0.ev_hash, R0.wet, R0.ticks, R0.splashes,
                   (unsigned)R0.ws.found, (unsigned)R0.ws.inserted, (unsigned)R0.ws.widened, (unsigned)R0.ws.blocked);
            /* the same scene with the host's flags: one line to compare between targets (x86 = x64) */
            o.mode = H_HOST;
            if (run_char(ctx, pos, &o, &R1) == 0) {
                printf("HASHH %-9s arena %016llX events %016llX wet %d/%d splashes %d\n", name,
                       (unsigned long long)R1.ram_hash, (unsigned long long)R1.ev_hash, R1.wet, R1.ticks, R1.splashes);
            }
        }
        (void)obj;
    }
    CHECK(total == 7 && same == total, "%d of %d scenes: the same arena and events with no flags and with host_flags 0", same,
          total);
}

static void t_sky(sa1_context *ctx)
{
    hwater h;
    float pos[3] = { 512.0f, 512.0f, 4.0f };
    ropt o;
    rres R;
    printf("sky (W1V): a water layer 4766 units above, a solid slab at 400..464 HU between, 120 pairs\n");
    if (scene_sky(ctx, &h) != 0) {
        g_fail++;
        return;
    }
    ropt_init(&o, SA1_CHAR_SONIC, 120, H_NONE, &h);
    if (run_char(ctx, pos, &o, &R) != 0) {
        g_fail++;
        return;
    }
    print_run("sky", pos, o.mode, &R);
    CHECK(R.max_breath == 0 && R.wet == 0 && R.ws.found > 0 && R.ws.blocked > 0 && R.ws.widened == 0,
          "without flags: breath %d, in water %d pairs, W1 found %u, blocked %u, widened %u", R.max_breath, R.wet,
          (unsigned)R.ws.found, (unsigned)R.ws.blocked, (unsigned)R.ws.widened);
}

static void t_roof(sa1_context *ctx)
{
    hwater h;
    float pos[3] = { 512.0f, 512.0f, 4.0f };
    ropt o;
    rres R;
    printf("roof (W1V): a rooftop pool (top 4080 HU) over a room (floor 0, roof 384..448 HU), 120 pairs\n");
    if (scene_roof(ctx, &h) != 0) {
        g_fail++;
        return;
    }
    ropt_init(&o, SA1_CHAR_SONIC, 120, H_NONE, &h);
    if (run_char(ctx, pos, &o, &R) != 0) {
        g_fail++;
        return;
    }
    print_run("roof", pos, o.mode, &R);
    CHECK(R.max_breath == 0 && R.wet == 0 && R.ws.found > 0 && R.ws.blocked > 0 && R.ws.widened == 0,
          "without flags: breath %d, in water %d pairs, W1 found %u, blocked %u, widened %u", R.max_breath, R.wet,
          (unsigned)R.ws.found, (unsigned)R.ws.blocked, (unsigned)R.ws.widened);
}

static void t_mover(sa1_context *ctx)
{
    hwater h;
    float pos[3] = { 512.0f, 512.0f, 4.0f };
    uint32_t obj = 0;
    ropt o;
    rres R;
    printf("mover (W1V, Review R3.4): water up to 2448 HU, the only ceiling a moving object at 400 HU, 120 pairs\n");
    if (scene_mover(ctx, &h, &obj) != 0) {
        g_fail++;
        return;
    }
    ropt_init(&o, SA1_CHAR_SONIC, 120, H_NONE, &h);
    if (run_char(ctx, pos, &o, &R) != 0) {
        sa1_collision_object_remove(ctx, obj);
        g_fail++;
        return;
    }
    sa1_collision_object_remove(ctx, obj);
    print_run("mover", pos, o.mode, &R);
    CHECK(R.max_breath == 0 && R.wet == 0 && R.ws.found > 0 && R.ws.blocked > 0 && R.ws.widened == 0,
          "without flags: breath %d, in water %d pairs, W1 found %u, blocked %u, widened %u (0.9.1: widened through the "
          "moving ceiling)", R.max_breath, R.wet, (unsigned)R.ws.found, (unsigned)R.ws.blocked, (unsigned)R.ws.widened);
}

static void t_cost(sa1_context *ctx)
{
    hwater h;
    float pos[3] = { 512.0f, 512.0f, 4.0f };
    sa1_collision_prepared_info info;
    double best[2] = { 1e30, 1e30 };
    int rep, m, ticks = 200, wet[2] = { 0, 0 };
    uint32_t blocked = 1, walked, wobjs = 0;
    printf("cost (W1V): W1's land test over a column of land triangles under deep water (600 units), 200 pairs\n");
    memset(&info, 0, sizeof info);
    if (scene_cost(ctx, &h, &info) != 0) {
        g_fail++;
        return;
    }
    printf("    scene: %u triangles -> %u GRID triangles in %u objects\n", info.triangles, info.grid_triangles,
           info.grid_objects);
    walked = land_test_tris(ctx, pos, 600.0f, &wobjs);
    printf("    the land test walks %u triangles in %u objects per step\n", (unsigned)walked, (unsigned)wobjs);
    for (rep = 0; rep < 3; rep++) {
        for (m = 0; m < 2; m++) {
            ropt o;
            rres R;
            double t0, t1;
            ropt_init(&o, SA1_CHAR_SONIC, ticks, m == 0 ? H_NONE : H_WET, &h);
            t0 = now_ms();
            if (run_char(ctx, pos, &o, &R) != 0) {
                g_fail++;
                return;
            }
            t1 = now_ms() - t0;
            best[m] = t1 < best[m] ? t1 : best[m];
            wet[m] = R.wet;
            if (m == 0) {
                blocked = R.ws.blocked;
            }
            if (g_verbose || rep == 0) {
                print_run("cost", pos, o.mode, &R);
            }
        }
    }
    {
        double per = (best[0] - best[1]) / (2.0 * (double)ticks);
        printf("    run without flags (land test every step) %.1f ms, with IN_WATER (no land test) %.1f ms: %.4f ms per step\n",
               best[0], best[1], per);
#if defined(__x86_64__) || defined(_M_X64)
        CHECK(wet[0] == ticks && wet[1] == ticks && blocked == 0 && walked >= 20000u && per < 0.5,
              "W1's land test over %u land triangles (%u objects): %.4f ms per step (< 0.5 on x64; the character in water "
              "every pair: %d/%d)", (unsigned)walked, (unsigned)wobjs, per, wet[0], ticks);
#else
        /* the plan's bound is for x64; other targets report the time */
        CHECK(wet[0] == ticks && wet[1] == ticks && blocked == 0 && walked >= 20000u,
              "W1's land test over %u land triangles (%u objects): %.4f ms per step (informational on this target; the "
              "character in water every pair: %d/%d)", (unsigned)walked, (unsigned)wobjs, per, wet[0], ticks);
#endif
    }
}

/* ---- --map: the owner's map collision -------------------------------------------------------------------------------- */
static int load_soup(const char *path, sa1_collision_tri **out, uint32_t *n)
{
    FILE *f = fopen(path, "rb");
    size_t L = strlen(path);
    long sz;
    *out = NULL;
    *n = 0;
    if (f == NULL) {
        return -1;
    }
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (L > 4 && strcmp(path + L - 4, ".dat") == 0) {
        unsigned char hd[56];
        uint32_t wt, sb;
        if (fread(hd, 1, 56, f) != 56 || memcmp(hd, "GSA1MAPC", 8) != 0) {
            fclose(f);
            return -1;
        }
        memcpy(&wt, hd + 40, 4);
        memcpy(&sb, hd + 48, 4);
        fseek(f, 56 + (long)sb, SEEK_SET);
        *n = wt;
    } else {
        *n = (uint32_t)(sz / 40);
    }
    *out = (sa1_collision_tri *)malloc((size_t)*n * sizeof **out + 1u);
    if (*out == NULL || fread(*out, 40, *n, f) != *n) {
        fclose(f);
        free(*out);
        *out = NULL;
        return -1;
    }
    fclose(f);
    return 0;
}

static int load_water(const char *path, hwater *h)
{
    FILE *f = fopen(path, "r");
    char line[512];
    uint32_t capb = 0, capp = 0;
    memset(h, 0, sizeof *h);
    if (f == NULL) {
        return -1;
    }
    while (fgets(line, sizeof line, f)) {
        unsigned id = 0, np = 0;
        float pl[4];
        if (sscanf(line, "brush %u %u", &id, &np) == 2) {
            if (h->nbrush == capb) {
                capb = capb ? 2u * capb : 64u;
                h->first = (uint32_t *)realloc(h->first, capb * sizeof *h->first);
                h->count = (uint32_t *)realloc(h->count, capb * sizeof *h->count);
            }
            h->first[h->nbrush] = h->nplane;
            h->count[h->nbrush] = 0;
            h->nbrush++;
        } else if (h->nbrush != 0 && sscanf(line, "%f %f %f %f", &pl[0], &pl[1], &pl[2], &pl[3]) == 4) {
            if (h->nplane == capp) {
                capp = capp ? 2u * capp : 256u;
                h->plane = (float *)realloc(h->plane, 4u * capp * sizeof *h->plane);
            }
            memcpy(h->plane + 4u * h->nplane, pl, sizeof pl);
            h->nplane++;
            h->count[h->nbrush - 1u]++;
        }
    }
    fclose(f);
    return 0;
}

static int t_map(sa1_context *ctx, const char *name, const char *soup, const char *water, const char *points,
                 const char *expect)
{
    sa1_collision_tri *t = NULL;
    uint32_t n = 0, np = 0, wet0 = 0, wet1 = 0, splash1 = 0, ip = 0, lost = 0;
    hwater h;
    FILE *f;
    char line[512];
    printf("map %s: %s, water %s, points %s\n", name, soup, water, points);
    if (load_soup(soup, &t, &n) != 0 || load_water(water, &h) != 0 || (f = fopen(points, "r")) == NULL) {
        printf("  skipped: a file is missing or unreadable (opt-in check)\n");
        free(t);
        return 0;
    }
    printf("    %u triangles, %u water brushes\n", n, (unsigned)h.nbrush);
    if (install(ctx, t, n, NULL) != 0) {
        free(t);
        fclose(f);
        g_fail++;
        return -1;
    }
    free(t);
    while (fgets(line, sizeof line, f)) {
        float pos[3];
        char lab[256] = "";
        ropt o;
        rres R0, R1;
        if (line[0] == '#' || sscanf(line, "%f %f %f %255[^\n]", &pos[0], &pos[1], &pos[2], lab) < 3) {
            continue;
        }
        ropt_init(&o, SA1_CHAR_SONIC, 60, H_NONE, &h);
        o.jump = g_jump;
        if (run_char(ctx, pos, &o, &R0) != 0 || (o.mode = H_HOST, run_char(ctx, pos, &o, &R1)) != 0) {
            continue;
        }
        np++;
        wet0 += R0.wet > 0;
        wet1 += R1.wet > 0;
        splash1 += R1.wet > 0 && R1.splashes > 0;
        lost += R0.wet > 0 && R1.wet == 0;
        ip += R1.inwater > 0;
        printf("    %-30s (%7.0f %7.0f %7.0f): without flags in water %3d/%3d (splashes %d); host's flags (IN_WATER %u "
               "ticks) in water %3d/%3d (splashes %d)%s\n", lab, pos[0], pos[1], pos[2], R0.wet, R0.ticks, R0.splashes,
               (unsigned)R1.inwater, R1.wet, R1.ticks, R1.splashes, R1.wet ? "" : "");
        fflush(stdout);
    }
    fclose(f);
    printf("    %s: %u points: in water without flags %u, with the host's flags %u (host IN_WATER at %u)\n", name,
           (unsigned)np, (unsigned)wet0, (unsigned)wet1, (unsigned)ip);
    if (expect != NULL && strcmp(expect, "dry") == 0) {
        CHECK(np > 0 && wet1 == 0, "%s: %u of %u points in water with the host's flags (without: %u)", name, (unsigned)wet1,
              (unsigned)np, (unsigned)wet0);
    } else if (expect != NULL && strcmp(expect, "wet") == 0) {
        /* real water stays water: no point the game had in water loses it (points the game never has in water, with or
           without flags, are the game's own rule - a structure over the character, J2 - or a cut water top, R3.3) */
        CHECK(np > 0 && wet0 > 0 && lost == 0 && wet1 >= wet0 && splash1 == wet1,
              "%s: %u of %u points in water with the host's flags, all %u with the splash (without flags %u; lost %u)", name,
              (unsigned)wet1, (unsigned)np, (unsigned)splash1, (unsigned)wet0, (unsigned)lost);
    }
    free(h.first);
    free(h.count);
    free(h.plane);
    return 0;
}

int main(int argc, char **argv)
{
    static const struct { const char *name; void (*fn)(sa1_context *); } tests[] = {
        { "tunnel", t_tunnel }, { "shelf", t_shelf }, { "twolevel", t_twolevel }, { "pool", t_pool }, { "enter", t_enter },
        { "e102", t_e102 }, { "nohost", t_nohost }, { "sky", t_sky }, { "roof", t_roof }, { "mover", t_mover },
        { "cost", t_cost },
    };
    const char *gdi = NULL, *only = NULL, *map[4] = { NULL, NULL, NULL, NULL }, *expect = NULL;
    sa1_context *ctx = NULL;
    sa1_host h;
    sa1_coords co;
    sa1_load_options lo;
    sa1_status st;
    size_t i;
    int a;
    for (a = 1; a < argc; a++) {
        if (!strcmp(argv[a], "--verbose")) {
            g_verbose = 1;
        } else if (!strcmp(argv[a], "--trace")) {
            g_trace = 1;
        } else if (!strcmp(argv[a], "--hash")) {
            g_hash = 1;
        } else if (!strcmp(argv[a], "--only") && a + 1 < argc) {
            only = argv[++a];
        } else if (!strcmp(argv[a], "--pairs") && a + 1 < argc) {
            g_pairs_override = atoi(argv[++a]);
        } else if (!strcmp(argv[a], "--jump") && a + 1 < argc) {
            g_jump = atoi(argv[++a]);
        } else if (!strcmp(argv[a], "--expect") && a + 1 < argc) {
            expect = argv[++a];
        } else if (!strcmp(argv[a], "--map") && a + 4 < argc) {
            map[0] = argv[++a];
            map[1] = argv[++a];
            map[2] = argv[++a];
            map[3] = argv[++a];
        } else {
            gdi = argv[a];
        }
    }
    if (gdi == NULL) {
        gdi = getenv("SA1_TEST_GDI");
    }
    if (gdi == NULL) {
        printf("usage: water_host_test [--only NAME] [--verbose] [--trace] [--hash] [--pairs N] [--jump N] "
               "[--map NAME SOUP WATER POINTS [--expect dry|wet]] GDI\n");
        return 2;
    }
    sa1_host_defaults(&h);
    h.log = log_cb;
    h.log_level = SA1_LOG_INFO;
    if (sa1_init(&h, &ctx) != SA1_OK) {
        printf("sa1_init failed\n");
        return 1;
    }
    memset(&co, 0, sizeof co);
    co.struct_size = sizeof co;
    sa1_coords_preset(SA1_COORDS_PRESET_SOURCE, &co);
    sa1_set_coords(ctx, &co);
    memset(&lo, 0, sizeof lo);
    lo.struct_size = sizeof lo;
    lo.characters = (1u << SA1_CHAR_SONIC) | (1u << SA1_CHAR_GAMMA);
    lo.flags = SA1_LOAD_DEFAULT | SA1_LOAD_WORLD_IMAGES;
    st = sa1_load_disc(ctx, gdi, &lo);
    if (SA1_FAILED(st)) {
        printf("sa1_load_disc: %s: %s\n", sa1_status_string(st), sa1_last_error(ctx));
        sa1_shutdown(ctx);
        return 1;
    }
    printf("libsa1 %s%s\n", sa1_version_string(),
#if defined(SA1G_HOSTWATER)
           ""
#else
           " (a build without the host's water: the 'before' run)"
#endif
    );
    if (map[0] != NULL) {
        t_map(ctx, map[0], map[1], map[2], map[3], expect);
    } else {
        for (i = 0; i < sizeof tests / sizeof tests[0]; i++) {
            if (only != NULL && strcmp(only, tests[i].name) != 0) {
                continue;
            }
            tests[i].fn(ctx);
        }
    }
    sa1_shutdown(ctx);
    printf("water_host_test: %d ok, %d failed\n", g_ok, g_fail);
    return g_fail ? 1 : 0;
}
