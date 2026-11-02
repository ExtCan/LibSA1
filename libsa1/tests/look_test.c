/*
 * look_test.c - round 0.9.8 (098-lib-lookcam): the head look (SA1_API_LOOK: sa1_character_look, SA1_COPT_SUPER_LOOK, the sa1_state_ex look
 * tail; sadc docs/research/gsa1_098_plan.md 6.3 part 2, gsa1_096_plan.md 6.2, look096.md 5, 7.1, 8). The expected numbers are look096.md 5's,
 * measured by the probe prep096/look/look_probe096.c (whose look was a simulation in a lane hook); here everything goes through the
 * public calls and the landed lane.
 *
 *   py -3.12 tests/build_look.py --run [--target all] [--debug] [-- --only 1,2,...]      (SA1_TEST_GDI)
 *
 * API NOTES (gsa1_098_plan.md 3.2: the full notes live here)
 *   - sa1_character_look(ctx, id, &look): between ticks. Latched until the next call, applied in every live step of the following ticks. POINT
 *     is in host space, the world converts it inside its FP mode each step (no lerp: the host calls every frame anyway). ANGLES: radians
 *     relative to the body, + yaw = the character's left, + pitch = up. CAMERA holds the pad's Z bit: the look tasks look at camera_twp->pos.
 *   - A look the game set wins unless SA1_LOOKF_OVERRIDE_GAME; look_owner tells the host (0 none or CAMERA, 1 the host's, 2 the game's).
 *   - sa1_state_ex tail (sizeof 0.3.0's 464 on): look_kind, look_owner, head_angle[2] (BAMS, + y left, + z down), eye_angle[2] (the left eye's); 0
 *     without a call. The state shows the last pair's end; a head moves at most 0x1000 BAMS a step: two pairs are at the clamp.
 *   - Refusals, SA1_ERR_UNSUPPORTED through sa1_refuse at DEBUG, the words in sa1_last_error: "E-102 has no look in the game", "Tikal cannot tick in
 *     libsa1 yet", "this character has no look in libsa1", a puppet, "a replay world takes no look"; while the gate is closed: "the head look is
 *     not built yet". SA1_ERR_INVALID_ARGUMENT: struct_size below pitch's end, kind above 3, unknown flags, OVERRIDE with OFF or CAMERA, a value not
 *     finite or above 1e30 in magnitude (only the fields the kind uses). SA1_ERR_NOT_FOUND. A character without a world gets it built.
 *   - SA1_COPT_SUPER_LOOK = 7, sa1_character_set_option, kind 8 and SA1_CREATE_SUPER_CAPABLE Sonic only, else UNSUPPORTED; setting it installs the lane
 *     (the world built when missing); 0 writes back what the three Super nodes held. Render only.
 *
 * Coordinates: the Source preset (host = 4 x (x, -z, y)), as G-SA1. Cases (look096.md 8.1, numbers as there):
 *   1  POINT per kind, every target of 5.2, per 60 Hz step; OFF back to rest in 2 steps; look_owner 1, look_kind POINT
 *   2  ANGLES: yaw +30 -> head y +0x1555, pitch +20 -> head z -0xE39
 *   3  CAMERA with the host camera: 5.3's angles, equal step for step to POINT at that place; the virtual eye; Tails in flight; E-102 refused
 *   4  render: triangles moved, worlds N / L / T; option 7
 *   5  the game's look wins (8C07C44E, 300 steps): look_owner 2, then the host's again; OVERRIDE from the first step
 *   6  no game state: sa1_state_ex without the weld points identical with and without a look, 90 pairs per kind; board 140 pairs
 *   8  an image rebuild keeps the look
 *   9  (after lib_follow_done.txt) POINT on a Tails follower: slot 1, +0x1800 left, the proxy's look work untouched; the Z bit in per[1].on survives the
 *      AI's pad write
 *   10 a world given only OFF, and a world with option 7 at 0, against a world never called: the whole arena identical after 300 pairs, every kind
 *   11 Big with the fishing camera RUNNING and a POINT look: the camera and lure numbers equal a run without the look
 *   12 refusals and argument checks
 *   (7, the hidden pair, is 098-lib-follow's FH3.)
 */
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libsa1.h"
#include "sa1_internal.h"
#include "sa1_api_world.h"
#include "sa1_coladapt.h"
#include "sa1_world.h"

static int g_fail, g_pass;
static const char *g_only;
#define CHECK(cond, ...)                                                                                                  \
    do {                                                                                                                  \
        if (cond) {                                                                                                       \
            g_pass++;                                                                                                     \
            printf("  ok    ");                                                                                           \
        } else {                                                                                                          \
            g_fail++;                                                                                                     \
            printf("  FAIL  ");                                                                                           \
        }                                                                                                                 \
        printf(__VA_ARGS__);                                                                                              \
        printf("\n");                                                                                                     \
    } while (0)

#define PI 3.14159265358979323846
#define ADDR_TASK_LISTS 0x8C79F04Cu
#define ADDR_PLAYER_TWP 0x8C78C608u
#define ADDR_PER0_ON    0x8C754C10u
#define ADDR_CAM_TWP    0x8C18B7ECu
#define ADDR_BPOS       0x8C6B6E68u
#define FN_TIMED_POINT  0x8C07C44Eu

static uint32_t g_det = 2166136261u;
static uint32_t fnv(uint32_t h, const void *p, size_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    size_t i;
    for (i = 0; i < n; i++) {
        h ^= b[i];
        h *= 16777619u;
    }
    return h;
}
static int want(const char *name)
{
    const char *p;
    size_t n = strlen(name);
    if (g_only == NULL) {
        return 1;
    }
    for (p = g_only; *p;) {
        const char *e = strchr(p, ',');
        size_t m = e ? (size_t)(e - p) : strlen(p);
        if (m == n && strncmp(p, name, n) == 0) {
            return 1;
        }
        p += m + (e ? 1 : 0);
        if (!e) {
            break;
        }
    }
    return 0;
}
static void log_cb(void *user, int32_t level, const char *msg)
{
    (void)user;
    if (level >= SA1_LOG_WARN) {
        printf("    [libsa1] %s\n", msg);
    }
}
static uint8_t *wp(const sa1g_world *w, uint32_t g, uint32_t n)
{
    if (g == 0 || ((g >> 24) & 31u) != SA1G_SEG_ARENA || (g & 0xFFFFFFu) > SA1G_ARENA_SIZE - n) {
        return NULL;
    }
    return w->ram + (g & 0xFFFFFFu);
}
static uint32_t r32(const sa1g_world *w, uint32_t g) { uint32_t v = 0; uint8_t *p = wp(w, g, 4); if (p) memcpy(&v, p, 4); return v; }
static int32_t r8s(const sa1g_world *w, uint32_t g) { uint8_t *p = wp(w, g, 1); return p ? (int8_t)*p : 0; }
static float rf(const sa1g_world *w, uint32_t g) { float v = 0; uint8_t *p = wp(w, g, 4); if (p) memcpy(&v, p, 4); return v; }
static int32_t ang16(uint32_t a) { a &= 0xFFFFu; return a >= 0x8000u ? (int32_t)a - 0x10000 : (int32_t)a; }
static void to_host(const float s[3], float h[3]) { h[0] = s[0] * 4.0f; h[1] = -s[2] * 4.0f; h[2] = s[1] * 4.0f; }

static uint32_t look_exec(int32_t chr)
{
    switch (chr) {
    case 0: return 0x8C0B28EEu;
    case 2: return 0x8C0BE1CEu;
    case 3: return 0x8C0C8A80u;
    case 5: return 0x8C0DDA00u;
    case 7: return 0x8C0E5DF0u;
    default: return 0;
    }
}
static uint32_t find_task_exec(const sa1g_world *w, uint32_t exec, int pno)
{
    uint32_t lvl;
    for (lvl = 0; lvl < 8; lvl++) {
        uint32_t tp = r32(w, ADDR_TASK_LISTS + 4u * lvl), guard = 0;
        while (tp != 0 && guard++ < 0x2000u) {
            if ((r32(w, tp + 0x10u) & 0x1FFFFFFFu) == (exec & 0x1FFFFFFFu) && (pno < 0 || r8s(w, r32(w, tp + 0x20u) + 0x08u) == pno)) {
                return tp;
            }
            tp = r32(w, tp);
        }
    }
    return 0;
}

typedef struct tris { sa1_collision_tri *t; uint32_t n, cap; } tris;
static void tri_add(tris *b, const float a[3], const float c[3], const float d[3], uint32_t surf)
{
    sa1_collision_tri *t;
    if (b->n == b->cap) {
        uint32_t cap = b->cap ? 2u * b->cap : 1024u;
        sa1_collision_tri *nt = (sa1_collision_tri *)realloc(b->t, cap * sizeof *nt);
        if (!nt) return;
        b->t = nt; b->cap = cap;
    }
    t = &b->t[b->n++];
    memcpy(t->v[0], a, 12); memcpy(t->v[1], c, 12); memcpy(t->v[2], d, 12);
    t->surface = surf;
}
static void rect(tris *b, float x0, float y0, float x1, float y1, float z, float tile, uint32_t surf)
{
    float x, y;
    for (x = x0; x < x1 - 0.01f; x += tile) {
        for (y = y0; y < y1 - 0.01f; y += tile) {
            float xe = x + tile < x1 ? x + tile : x1, ye = y + tile < y1 ? y + tile : y1;
            float a[3] = { x, y, z }, c[3] = { xe, y, z }, d[3] = { xe, ye, z }, e[3] = { x, ye, z };
            tri_add(b, a, c, d, surf);
            tri_add(b, a, d, e, surf);
        }
    }
}
/* ground 0: flat SOLID, every kind loaded; 1: the island of fish098 (Big + Sonic) */
static sa1_context *make_ctx(const char *gdi, int ground)
{
    sa1_context *ctx = NULL; sa1_host h; sa1_load_options lo; sa1_coords co; tris b;
    sa1_host_defaults(&h); h.log = log_cb; h.log_level = SA1_LOG_WARN;
    if (sa1_init(&h, &ctx) != SA1_OK) return NULL;
    memset(&co, 0, sizeof co); co.struct_size = sizeof co; sa1_coords_preset(SA1_COORDS_PRESET_SOURCE, &co); sa1_set_coords(ctx, &co);
    memset(&lo, 0, sizeof lo); lo.struct_size = sizeof lo;
    if (ground == 1) {
        lo.characters = (1u << SA1_CHAR_BIG) | (1u << SA1_CHAR_SONIC);
        lo.flags = SA1_LOAD_DEFAULT | SA1_LOAD_WORLD_IMAGES;
    } else {
        lo.characters = (1u << SA1_CHAR_SONIC) | (1u << SA1_CHAR_TAILS) | (1u << SA1_CHAR_KNUCKLES) | (1u << SA1_CHAR_AMY) |
                        (1u << SA1_CHAR_GAMMA) | (1u << SA1_CHAR_BIG) | (1u << SA1_CHAR_SUPER_SONIC);
        lo.flags = SA1_LOAD_DEFAULT | SA1_LOAD_WORLD_IMAGES | SA1_LOAD_SUPER_FORMS;
    }
    if (SA1_FAILED(sa1_load_disc(ctx, gdi, &lo))) { printf("load_disc: %s\n", sa1_last_error(ctx)); sa1_shutdown(ctx); return NULL; }
    memset(&b, 0, sizeof b);
    if (ground == 1) {
        rect(&b, -300, -300, 300, 300, 0, 300, SA1_SURFACE_SOLID);
        rect(&b, -6000, -6000, 6000, -300, -20, 1000, SA1_SURFACE_WATER);
        rect(&b, -6000, 300, 6000, 6000, -20, 1000, SA1_SURFACE_WATER);
        rect(&b, -6000, -300, -300, 300, -20, 600, SA1_SURFACE_WATER);
        rect(&b, 300, -300, 6000, 300, -20, 600, SA1_SURFACE_WATER);
        rect(&b, -6000, -6000, 6000, 6000, -600, 1000, SA1_SURFACE_SOLID);
    } else {
        rect(&b, -8192, -8192, 8192, 8192, 0, 1024, SA1_SURFACE_SOLID);
    }
    if (SA1_FAILED(sa1_collision_set_static(ctx, b.t, b.n))) { printf("collision: %s\n", sa1_last_error(ctx)); free(b.t); sa1_shutdown(ctx); return NULL; }
    free(b.t);
    return ctx;
}
typedef struct kinddef { const char *name; int32_t kind; uint32_t flags, super_flags; int32_t head_y, eye_y, eye_r, eye_z_up, eye_z_down; } kinddef;
/* look096.md 5.2 in BAMS: head y clamp, eyeL y at left 90, eyeR y at left 90, eye z at up 79 (-), eye z at down 34 (+) */
static const kinddef KINDS[] = {
    { "Sonic", SA1_CHAR_SONIC, 0, 0, 0x2000, -0x1B00, -0x200, -0xD00, 0x800 },
    { "Tails", SA1_CHAR_TAILS, 0, 0, 0x1800, -0x1000, -0x200, -0x800, 0x400 },
    { "Knuckles", SA1_CHAR_KNUCKLES, 0, 0, 0x2000, -0x1B00, -0x200, -0xD00, 0x800 },
    { "Amy", SA1_CHAR_AMY, 0, 0, 0x2000, -0x800, -0x200, -0xD00, 0x800 },
    { "Big", SA1_CHAR_BIG, 0, 0, 0x1800, -0x800, -0x400, -0x800, 0x800 },
    { "Super(8)", SA1_CHAR_SUPER_SONIC, 0, 0, 0x2000, -0x1B00, -0x200, -0xD00, 0x800 },
    { "General", SA1_CHAR_SONIC, SA1_CREATE_SUPER_CAPABLE | SA1_CREATE_SUPER, SA1_SUPER_GENERAL | SA1_SUPER_START | SA1_SUPER_NO_DRAIN, 0x2000, -0x1B00, -0x200, -0xD00, 0x800 },
};
#define NKINDS ((int)(sizeof KINDS / sizeof KINDS[0]))

static sa1_character_id make_char(sa1_context *ctx, const kinddef *k)
{
    sa1_character_desc d; sa1_character_id id = 0;
    memset(&d, 0, sizeof d); d.struct_size = sizeof d; d.kind = k->kind; d.flags = SA1_CREATE_WORLD | k->flags;
    d.super_flags = k->super_flags;
    if (SA1_FAILED(sa1_character_create_ex(ctx, &d, &id))) { printf("    create %s: %s\n", k->name, sa1_last_error(ctx)); return 0; }
    return id;
}
static int tick(sa1_context *ctx, sa1_character_id id, float sx, float sy, uint32_t buttons, float yaw)
{
    sa1_input in; memset(&in, 0, sizeof in); in.struct_size = sizeof in;
    in.stick_x = sx; in.stick_y = sy; in.buttons = buttons; in.camera_yaw = yaw;
    if (sa1_character_tick(ctx, id, &in, 1.0f / 30.0f, NULL, NULL) != SA1_OK) { printf("    tick: %s\n", sa1_last_error(ctx)); return -1; }
    return 0;
}
/* the body centre the look tasks subtract (CWP(ptwp)->info->center) */
static int center_of(const sa1g_world *w, uint32_t ptwp, float c[3])
{
    uint32_t cwp = r32(w, ptwp + 0x38u), info = cwp ? r32(w, cwp + 0x0Cu) : 0;
    if (!info) return 0;
    c[0] = rf(w, info + 0x08u); c[1] = rf(w, info + 0x0Cu); c[2] = rf(w, info + 0x10u);
    return 1;
}
/* a point at angle d (host convention: + = left) from the facing, distance R, up (SA1 space), returned in host space */
static void rel_point(const sa1g_world *w, double d_deg, float R, float up, float host[3])
{
    uint32_t ptwp = r32(w, ADDR_PLAYER_TWP);
    float c[3], s[3];
    double a = (double)(r32(w, ptwp + 0x18u) & 0xFFFFu) * 2.0 * PI / 65536.0 + d_deg * PI / 180.0;
    if (!center_of(w, ptwp, c)) { c[0] = rf(w, ptwp + 0x20u); c[1] = rf(w, ptwp + 0x24u) + 6.0f; c[2] = rf(w, ptwp + 0x28u); }
    s[0] = c[0] + (float)(R * cos(a));
    s[1] = c[1] + up;
    s[2] = c[2] - (float)(R * sin(a));
    to_host(s, host);
}
static sa1_status look_point(sa1_context *ctx, sa1_character_id id, const float host[3], uint32_t flags)
{
    sa1_look l;
    memset(&l, 0, sizeof l);
    l.struct_size = sizeof l;
    l.kind = SA1_LOOK_POINT;
    l.flags = flags;
    memcpy(l.point, host, sizeof l.point);
    return sa1_character_look(ctx, id, &l);
}
static sa1_status look_kind(sa1_context *ctx, sa1_character_id id, uint32_t kind)
{
    sa1_look l;
    memset(&l, 0, sizeof l);
    l.struct_size = sizeof l;
    l.kind = kind;
    return sa1_character_look(ctx, id, &l);
}
static void state_of(sa1_context *ctx, sa1_character_id id, sa1_state_ex *s)
{
    memset(s, 0, sizeof *s);
    s->struct_size = sizeof *s;
    sa1_character_state_ex(ctx, id, s);
}

/* ---- per-step log: a wrapper of the lane's post hook (it runs right after the emote lane's post) -------------------------- */
typedef struct steplog { int32_t head_y, head_z, eyeL_y, eyeL_z, eyeR_y; int tmode, evlook; } steplog;
#define MAXLOG 512
typedef struct logger {
    sa1g_world *w;
    sa1g_step_fn orig;
    int n;
    steplog s[MAXLOG];
} logger;
static logger *g_loggers[8];

static void log_post(sa1g_world *w, int32_t sub, void *user)
{
    logger *L = NULL;
    int i;
    for (i = 0; i < 8; i++) {
        if (g_loggers[i] != NULL && g_loggers[i]->w == w) {
            L = g_loggers[i];
        }
    }
    if (L == NULL) {
        return;
    }
    L->orig(w, sub, user);
    if (L->n < MAXLOG) {
        steplog *o = &L->s[L->n++];
        uint32_t tp = find_task_exec(w, look_exec(w->chr), -1);
        uint32_t ptwp = r32(w, ADDR_PLAYER_TWP);
        memset(o, 0, sizeof *o);
        o->tmode = -1;
        o->evlook = ptwp && r32(w, ptwp + 0x3Cu) ? r8s(w, r32(w, ptwp + 0x3Cu) + 4u) : -9;
        if (tp) {
            uint32_t lwp = r32(w, tp + 0x24u);
            o->tmode = r8s(w, r32(w, tp + 0x20u));
            o->head_y = ang16(r32(w, lwp + 0x08u));
            o->head_z = ang16(r32(w, lwp + 0x0Cu));
            o->eyeL_y = ang16(r32(w, lwp + 0x18u));
            o->eyeL_z = ang16(r32(w, lwp + 0x1Cu));
            o->eyeR_y = ang16(r32(w, lwp + 0x20u));
        }
    }
}
/* after the first look call of the world (the lane's hooks are installed) */
static logger *log_start(sa1g_world *w)
{
    logger *L = (logger *)calloc(1, sizeof *L);
    int i;
    if (L == NULL) {
        return NULL;
    }
    L->w = w;
    L->orig = w->look.post;
    w->look.post = log_post;
    for (i = 0; i < 8; i++) {
        if (g_loggers[i] == NULL) {
            g_loggers[i] = L;
            break;
        }
    }
    return L;
}
static void log_stop(logger *L)
{
    int i;
    L->w->look.post = L->orig;
    for (i = 0; i < 8; i++) {
        if (g_loggers[i] == L) {
            g_loggers[i] = NULL;
        }
    }
    free(L);
}
/* ---- 1: POINT -------------------------------------------------------------------------------------------------------------- */
typedef struct target { const char *name; double d; float R, up; } target;
static void t1_point(sa1_context *ctx)
{
    int i, p;
    printf("\n== 1: POINT per kind, per 60 Hz step\n");
    for (i = 0; i < NKINDS; i++) {
        const kinddef *K = &KINDS[i];
        sa1_character_id id = make_char(ctx, K);
        sa1g_world *w;
        logger *L;
        float h[3];
        sa1_state_ex st;
        int n0;
        int bad = 0;
#define STEPS_FROM(n) (&L->s[(n)])
        if (!id) {
            CHECK(0, "1 %s: created", K->name);
            continue;
        }
        for (p = 0; p < 4; p++) tick(ctx, id, 0, 0, 0, 0);
        w = sa1_character_world(ctx, id);
        rel_point(w, 90, 50, 0, h);
        CHECK(look_point(ctx, id, h, 0) == SA1_OK, "1 %s: POINT accepted", K->name);
        L = log_start(w);
        /* left 90: the head +0x1000 in its first step and at the clamp in its second */
        for (p = 0; p < 6; p++) tick(ctx, id, 0, 0, 0, 0);
        {
            const steplog *e = &L->s[L->n - 1];
            int first = K->head_y >= 0x2000 ? 0x1000 : 0x1000;
            CHECK(L->s[0].head_y == first && L->s[1].head_y == K->head_y && e->head_y == K->head_y && e->eyeL_y == K->eye_y && e->eyeR_y == K->eye_r
                  && e->tmode == 3 && e->evlook == 2,
                  "1 %s left 90: head %+d, %+d .. %+d (clamp %+d), eyes %+d / %+d (%+d / %+d), task mode %d, ev.look %d", K->name, L->s[0].head_y, L->s[1].head_y, e->head_y,
                  K->head_y, e->eyeL_y, e->eyeR_y, K->eye_y, K->eye_r, e->tmode, e->evlook);
            g_det = fnv(g_det, L->s, (size_t)L->n * sizeof L->s[0]);
        }
        state_of(ctx, id, &st);
        CHECK(st.look_kind == SA1_LOOK_POINT && st.look_owner == 1u && st.head_angle[0] == K->head_y && st.eye_angle[0] == K->eye_y,
              "1 %s the state tail: kind %u, owner %u, head %+d / %+d, eye %+d / %+d", K->name, (unsigned)st.look_kind, (unsigned)st.look_owner, (int)st.head_angle[0],
              (int)st.head_angle[1], (int)st.eye_angle[0], (int)st.eye_angle[1]);
        /* OFF: back to rest in 2 steps */
        look_kind(ctx, id, SA1_LOOK_OFF);
        n0 = L->n;
        for (p = 0; p < 3; p++) tick(ctx, id, 0, 0, 0, 0);
        {
            const steplog *e = &L->s[L->n - 1];
            CHECK(L->s[n0 + 1].head_y == 0 && e->head_y == 0 && e->eyeL_y == 0 && e->tmode == 1 && e->evlook == 0, "1 %s OFF: head %+d, %+d .. %+d, task mode %d, ev.look %d", K->name,
                  L->s[n0].head_y, L->s[n0 + 1].head_y, e->head_y, e->tmode, e->evlook);
        }
        state_of(ctx, id, &st);
        CHECK(st.look_kind == SA1_LOOK_OFF && st.look_owner == 0u, "1 %s OFF: the state's kind %u, owner %u", K->name, (unsigned)st.look_kind, (unsigned)st.look_owner);
        /* the rest of 5.2's targets */
        {
            static const target T[] = {
                { "left 30", 30, 50, 0 }, { "right 30", -30, 50, 0 }, { "right 90", -90, 50, 0 }, { "behind-left 170", 170, 50, 0 }, { "behind-right 170", -170, 50, 0 },
                { "behind 180", 180, 50, 0 }, { "up 45", 0, 50, 50 }, { "up 79", 0, 10, 50 }, { "down 34", 0, 30, -20 }, { "down 79", 0, 10, -50 }, { "far left 90", 90, 2000, 0 },
            };
            size_t k;
            for (k = 0; k < sizeof T / sizeof T[0]; k++) {
                int32_t wy, wz, wey = -999999, wez = -999999;
                const steplog *e;
                rel_point(w, T[k].d, T[k].R, T[k].up, h);
                look_point(ctx, id, h, 0);
                for (p = 0; p < 6; p++) tick(ctx, id, 0, 0, 0, 0);
                e = &L->s[L->n - 1];
                g_det = fnv(g_det, e, sizeof *e);
                wz = 0;
                switch (k) {
                case 0: wy = 0x1555; break;
                case 1: wy = -0x1555; break;
                case 2: wy = -K->head_y; wey = -K->eye_r; break;
                case 3: case 10: wy = K->head_y; wey = K->eye_y; break;
                case 4: case 5: wy = -K->head_y; wey = -K->eye_r; break;
                case 6: wy = 0; wz = -0x1800; wez = -0x800; break;
                case 7: wy = 0; wz = -0x1800; wez = K->eye_z_up; break;
                default: wy = 0; wz = 0x400; wez = K->eye_z_down; break;
                }
                if (abs(e->head_y - wy) > 3 || abs(e->head_z - wz) > 3 || (wey != -999999 && abs(e->eyeL_y - wey) > 3) || (wez != -999999 && abs(e->eyeL_z - wez) > 3)) {
                    bad++;
                    printf("    %s %s: head %+d / %+d (%+d / %+d), eye %+d / %+d (%+d / %+d)\n", K->name, T[k].name, e->head_y, e->head_z, wy, wz, e->eyeL_y, e->eyeL_z, wey, wez);
                }
                look_kind(ctx, id, SA1_LOOK_OFF);
                for (p = 0; p < 5; p++) tick(ctx, id, 0, 0, 0, 0);
            }
            CHECK(bad == 0, "1 %s: every target of look096.md 5.2 (%d off)", K->name, bad);
        }
        CHECK(!w->fault.kind, "1 %s: no fault (%s)", K->name, w->fault.kind ? w->fault.msg : "none");
        log_stop(L);
        sa1_character_destroy(ctx, id);
    }
}

/* ---- 2: ANGLES ------------------------------------------------------------------------------------------------------------- */
static void t2_angles(sa1_context *ctx)
{
    const kinddef *K = &KINDS[0];
    sa1_character_id id = make_char(ctx, K);
    sa1_look l;
    sa1_state_ex st;
    int p;
    printf("\n== 2: ANGLES\n");
    if (!id) {
        CHECK(0, "2 created");
        return;
    }
    for (p = 0; p < 4; p++) tick(ctx, id, 0, 0, 0, 0);
    memset(&l, 0, sizeof l);
    l.struct_size = sizeof l;
    l.kind = SA1_LOOK_ANGLES;
    l.yaw = (float)(30.0 * PI / 180.0);
    l.pitch = (float)(20.0 * PI / 180.0);
    CHECK(sa1_character_look(ctx, id, &l) == SA1_OK, "2 accepted");
    for (p = 0; p < 6; p++) tick(ctx, id, 0, 0, 0, 0);
    state_of(ctx, id, &st);
    CHECK(abs((int)st.head_angle[0] - 0x1555) <= 1 && abs((int)st.head_angle[1] + 0xE39) <= 1, "2 yaw +30 -> head y %+d (+0x1555 = %+d), pitch +20 up -> head z %+d (-0xE39 = %+d)",
          (int)st.head_angle[0], 0x1555, (int)st.head_angle[1], -0xE39);
    CHECK(st.look_kind == SA1_LOOK_ANGLES && st.look_owner == 1u, "2 the state: kind %u, owner %u", (unsigned)st.look_kind, (unsigned)st.look_owner);
    g_det = fnv(g_det, st.head_angle, sizeof st.head_angle);
    sa1_character_destroy(ctx, id);
}

/* ---- 3: CAMERA ------------------------------------------------------------------------------------------------------------- */
static int tick_host_cam(sa1_context *ctx, sa1_character_id id, const float host_eye[3], float yaw)
{
    sa1_input in;
    memset(&in, 0, sizeof in);
    in.struct_size = sizeof in;
    in.camera_yaw = yaw;
    memcpy(in.host_cam_pos, host_eye, sizeof in.host_cam_pos);
    memcpy(in.host_cam_target, host_eye, sizeof in.host_cam_target);
    in.host_flags |= SA1_INPUT_HOST_CAMERA;
    return sa1_character_tick(ctx, id, &in, 1.0f / 30.0f, NULL, NULL) == SA1_OK ? 0 : -1;
}
static void t3_camera(sa1_context *ctx)
{
    int i, p;
    printf("\n== 3: CAMERA (the pad's Z bit)\n");
    for (i = 0; i < NKINDS; i++) {
        const kinddef *K = &KINDS[i];
        sa1_character_id iz = make_char(ctx, K), ip = make_char(ctx, K);
        sa1g_world *wz, *wpt;
        logger *Lz, *Lp;
        float eye[3];
        int same = 1, n;
        if (!iz || !ip) {
            CHECK(0, "3 %s created", K->name);
            continue;
        }
        for (p = 0; p < 4; p++) { tick(ctx, iz, 0, 0, 0, 0); tick(ctx, ip, 0, 0, 0, 0); }
        wz = sa1_character_world(ctx, iz);
        wpt = sa1_character_world(ctx, ip);
        rel_point(wz, 90, 30, 5, eye);
        CHECK(look_kind(ctx, iz, SA1_LOOK_CAMERA) == SA1_OK, "3 %s CAMERA accepted", K->name);
        look_point(ctx, ip, eye, 0);
        Lz = log_start(wz);
        Lp = log_start(wpt);
        for (p = 0; p < 6; p++) {
            tick_host_cam(ctx, iz, eye, 0.0f);
            tick_host_cam(ctx, ip, eye, 0.0f);
        }
        n = Lz->n < Lp->n ? Lz->n : Lp->n;
        for (p = 0; p < n; p++) {
            const steplog *a = &Lz->s[p], *b = &Lp->s[p];
            if (a->head_y != b->head_y || a->head_z != b->head_z || a->eyeL_y != b->eyeL_y || a->eyeL_z != b->eyeL_z || a->eyeR_y != b->eyeR_y) {
                same = 0;
            }
        }
        CHECK(n == 12 && same, "3 %s the host eye 30 left, +5: equal to POINT at that place in every step (%d steps)", K->name, n);
        {
            const steplog *e = &Lz->s[n - 1];
            sa1_state_ex st;
            state_of(ctx, iz, &st);
            CHECK(e->head_y == K->head_y && e->tmode == 2 && e->evlook == 0 && st.look_kind == SA1_LOOK_CAMERA && st.look_owner == 0u,
                  "3 %s head %+d (clamp %+d) z %+d, task mode %d, ev.look %d; the state's kind %u, owner %u", K->name, e->head_y, K->head_y, e->head_z, e->tmode, e->evlook,
                  (unsigned)st.look_kind, (unsigned)st.look_owner);
            g_det = fnv(g_det, Lz->s, (size_t)n * sizeof Lz->s[0]);
        }
        /* no game press: the pad's press word never holds the bit */
        CHECK((r32(wz, ADDR_PER0_ON + 8u) & 0x100u) == 0u, "3 %s the press word never sees the bit", K->name);
        log_stop(Lz);
        log_stop(Lp);
        /* the virtual eye, camera yaw = facing: the eye exactly behind him (Sonic -0x2000: over the right shoulder, from rest; the head keeps the
           shorter way from where it is, so it comes back first) */
        look_kind(ctx, iz, SA1_LOOK_OFF);
        for (p = 0; p < 5; p++) tick(ctx, iz, 0, 0, 0, 0);
        look_kind(ctx, iz, SA1_LOOK_CAMERA);
        for (p = 0; p < 6; p++) {
            float yaw = (float)((double)(r32(wz, r32(wz, ADDR_PLAYER_TWP) + 0x18u) & 0xFFFFu) * 2.0 * PI / 65536.0);
            tick(ctx, iz, 0, 0, 0, yaw);
        }
        {
            sa1_state_ex st;
            state_of(ctx, iz, &st);
            CHECK(st.head_angle[0] == -K->head_y, "3 %s the virtual eye: head %+d (%+d)", K->name, (int)st.head_angle[0], -K->head_y);
        }
        look_kind(ctx, iz, SA1_LOOK_OFF);
        sa1_character_destroy(ctx, iz);
        sa1_character_destroy(ctx, ip);
    }
    /* Tails in flight: 8C0BE558 in its mode 2 only under the bit */
    {
        const kinddef *K = &KINDS[1];
        sa1_character_id id = make_char(ctx, K);
        sa1g_world *w = id ? sa1_character_world(ctx, id) : NULL;
        int flying = 0, mode2 = 0, eyes = 0;
        sa1_state_ex st;
        if (w != NULL) {
            float eye[3];
            for (p = 0; p < 4; p++) tick(ctx, id, 0, 0, 0, 0);
            rel_point(w, 90, 30, 5, eye);
            look_kind(ctx, id, SA1_LOOK_CAMERA);
            for (p = 0; p < 60; p++) {
                sa1_input in;
                uint32_t tp;
                memset(&in, 0, sizeof in);
                in.struct_size = sizeof in;
                memcpy(in.host_cam_pos, eye, sizeof in.host_cam_pos);
                memcpy(in.host_cam_target, eye, sizeof in.host_cam_target);
                in.host_flags |= SA1_INPUT_HOST_CAMERA;
                if (p < 2 || (p >= 8 && p < 50)) {
                    in.buttons = SA1_BUTTON_JUMP;
                }
                sa1_character_tick(ctx, id, &in, 1.0f / 30.0f, NULL, NULL);
                state_of(ctx, id, &st);
                if (st.action >= 37 && st.action <= 53) {
                    flying++;
                    tp = find_task_exec(w, 0x8C0BE558u, -1);
                    if (tp) {
                        uint32_t l2 = r32(w, tp + 0x24u);
                        if (r8s(w, r32(w, tp + 0x20u)) == 2) {
                            mode2++;
                        }
                        if (ang16(r32(w, l2 + 0x18u)) != 0) {
                            eyes++;
                        }
                    }
                }
            }
            CHECK(flying > 10 && mode2 == flying && eyes > 0, "3 Tails in flight (%d pairs): 8C0BE558 in mode 2 (%d), its eyes turned (%d)", flying, mode2, eyes);
            sa1_character_destroy(ctx, id);
        }
    }
}

/* ---- 4: render ------------------------------------------------------------------------------------------------------------- */
typedef struct snap { uint32_t n; float *pos; } snap;
static void take(sa1_context *ctx, sa1_character_id id, snap *s)
{
    const sa1_render *r = NULL;
    free(s->pos);
    memset(s, 0, sizeof *s);
    if (sa1_character_render(ctx, id, 1.0f, &r) != SA1_OK || !r) return;
    s->n = r->num_triangles;
    s->pos = (float *)malloc(9u * sizeof(float) * (s->n ? s->n : 1));
    if (s->pos && s->n) memcpy(s->pos, r->positions, 9u * sizeof(float) * s->n);
}
static int moved(const snap *a, const snap *b, double *mx)
{
    uint32_t i, k;
    int m = 0;
    *mx = 0;
    if (a->n != b->n || !a->pos || !b->pos) return -1;
    for (i = 0; i < a->n; i++) {
        double d = 0;
        for (k = 0; k < 9; k++) {
            double e = fabs((double)a->pos[9u * i + k] - (double)b->pos[9u * i + k]);
            if (e > d) d = e;
        }
        if (d > 0.01) m++;
        if (d > *mx) *mx = d;
    }
    return m;
}
typedef struct trio { sa1_character_id id[3]; sa1g_world *w[3]; snap s[3]; } trio;
static int trio_make(sa1_context *ctx, const kinddef *k, trio *t)
{
    int j;
    memset(t, 0, sizeof *t);
    for (j = 0; j < 3; j++) {
        t->id[j] = make_char(ctx, k);
        if (!t->id[j]) return 0;
        t->w[j] = sa1_character_world(ctx, t->id[j]);
    }
    return 1;
}
static void trio_free(sa1_context *ctx, trio *t)
{
    int j;
    for (j = 0; j < 3; j++) {
        if (t->id[j]) sa1_character_destroy(ctx, t->id[j]);
        free(t->s[j].pos);
    }
    memset(t, 0, sizeof *t);
}
/* L 90 left, T 90 right, 50 units beside the character (the host passes it every frame) */
static void trio_look(sa1_context *ctx, trio *t, int on)
{
    float h[3];
    if (!on) {
        return;
    }
    rel_point(t->w[1], 90, 50, 0, h);
    look_point(ctx, t->id[1], h, 0);
    rel_point(t->w[2], -90, 50, 0, h);
    look_point(ctx, t->id[2], h, 0);
}
static void trio_tick(sa1_context *ctx, trio *t, float sx, float sy, uint32_t b, float yaw)
{
    int j;
    for (j = 0; j < 3; j++) tick(ctx, t->id[j], sx, sy, b, yaw);
}
static int trio_moved(sa1_context *ctx, trio *t, double *mx)
{
    int j;
    for (j = 0; j < 3; j++) take(ctx, t->id[j], &t->s[j]);
    return moved(&t->s[0], &t->s[1], mx);
}
static void t4_render(sa1_context *ctx)
{
    static const int stand[NKINDS] = { 24, 405, 482, 483, 8, 0, 0 };
    static const int run[NKINDS] = { 490, 405, 482, 483, 8, 0, 0 };
    int i, p;
    printf("\n== 4: render (alpha 1): triangles moved against a world without a look\n");
    for (i = 0; i < NKINDS; i++) {
        trio t;
        double mx;
        int m;
        if (!trio_make(ctx, &KINDS[i], &t)) {
            CHECK(0, "4 %s created", KINDS[i].name);
            trio_free(ctx, &t);
            continue;
        }
        for (p = 0; p < 30; p++) { trio_look(ctx, &t, p >= 2); trio_tick(ctx, &t, 0, 0, 0, 0); }
        m = trio_moved(ctx, &t, &mx);
        if (stand[i] == 0) {
            CHECK(m == 0, "4 %s standing: %d moved (none: the game)", KINDS[i].name, m);
        } else {
            CHECK(m >= stand[i], "4 %s standing: %d moved, at least %d (%.2f HU)", KINDS[i].name, m, stand[i], mx);
        }
        g_det = fnv(g_det, &m, sizeof m);
        trio_free(ctx, &t);
        if (!trio_make(ctx, &KINDS[i], &t)) {
            trio_free(ctx, &t);
            continue;
        }
        for (p = 0; p < 40; p++) { trio_look(ctx, &t, p >= 2); trio_tick(ctx, &t, 0, 1, 0, 0); }
        m = trio_moved(ctx, &t, &mx);
        if (run[i] == 0) {
            CHECK(m == 0, "4 %s running: %d moved (none: the game)", KINDS[i].name, m);
        } else {
            CHECK(m >= run[i], "4 %s running: %d moved, at least %d (%.2f HU)", KINDS[i].name, m, run[i], mx);
        }
        trio_free(ctx, &t);
    }
    /* option 7 */
    {
        static const int want_stand[2] = { 487, 24 }, want_run[2] = { 487, 487 };
        int v;
        for (v = 0; v < 2; v++) {
            const kinddef *K = &KINDS[5 + v];
            trio t;
            double mx;
            int m, j, q;
            if (!trio_make(ctx, K, &t)) {
                CHECK(0, "4 option 7 %s created", K->name);
                trio_free(ctx, &t);
                continue;
            }
            for (j = 0; j < 3; j++) {
                CHECK(sa1_character_set_option(ctx, t.id[j], SA1_COPT_SUPER_LOOK, 1) == SA1_OK, "4 %s option 7 accepted", K->name);
            }
            for (p = 0; p < 30; p++) { trio_look(ctx, &t, p >= 2); trio_tick(ctx, &t, 0, 0, 0, 0); }
            m = trio_moved(ctx, &t, &mx);
            CHECK(m >= want_stand[v], "4 %s option 7 standing: %d moved, at least %d (%.2f HU)", K->name, m, want_stand[v], mx);
            for (p = 0; p < 40; p++) { trio_look(ctx, &t, 1); trio_tick(ctx, &t, 0, 1, 0, 0); }
            m = trio_moved(ctx, &t, &mx);
            CHECK(m >= want_run[v], "4 %s option 7 running: %d moved, at least %d (%.2f HU)", K->name, m, want_run[v], mx);
            /* 0 writes back what the nodes held: nothing moves against the world without a look any more */
            q = 0;
            for (j = 0; j < 3; j++) {
                sa1_character_set_option(ctx, t.id[j], SA1_COPT_SUPER_LOOK, 0);
            }
            for (p = 0; p < 8; p++) { trio_tick(ctx, &t, 0, 1, 0, 0); }
            for (j = 0; j < 3; j++) {
                static const uint32_t node[3] = { 0x8C9093A8u, 0x8C908FE0u, 0x8C909270u };
                uint32_t k;
                for (k = 0; k < 3u; k++) {
                    q += r32(t.w[1], node[k] + 0x14u + 4u * (uint32_t)j) != r32(t.w[0], node[k] + 0x14u + 4u * (uint32_t)j);
                }
            }
            CHECK(q == 0, "4 %s option 7 back to 0: the three nodes hold what they held (%d angles differ)", K->name, q);
            trio_free(ctx, &t);
        }
        /* a plain Sonic refuses */
        {
            sa1_character_id id = make_char(ctx, &KINDS[0]);
            CHECK(sa1_character_set_option(ctx, id, SA1_COPT_SUPER_LOOK, 1) == SA1_ERR_UNSUPPORTED, "4 option 7 on a plain Sonic: SA1_ERR_UNSUPPORTED (%s)", sa1_last_error(ctx));
            sa1_character_destroy(ctx, id);
        }
    }
}

/* ---- 9: followers (after lanes/lib_follow_done.txt; a follower world's character slot is 1) ----------------------------------- */
#define ADDR_PER1_ON (ADDR_PER0_ON + 0x34u)
static sa1g_step_fn g_t9_orig;
static uint32_t g_t9_steps, g_t9_bit;
static void t9_post(sa1g_world *w, int32_t sub, void *user)
{
    g_t9_orig(w, sub, user);
    g_t9_steps++;
    g_t9_bit += (r32(w, ADDR_PER1_ON) & 0x100u) != 0u;
}
static void t9_follower(sa1_context *ctx)
{
    const kinddef *K = &KINDS[0];
    sa1_character_id lead = make_char(ctx, K), fol = 0;
    sa1_follower_desc d;
    sa1g_world *w;
    sa1_status st;
    int p;
    printf("\n== 9: POINT and CAMERA on a Tails follower\n");
    if (!lead) {
        CHECK(0, "9 leader");
        return;
    }
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.kind = SA1_CHAR_TAILS;
    d.leader = lead;
    st = sa1_follower_create(ctx, &d, &fol);
    if (st == SA1_ERR_UNSUPPORTED) {
        printf("  note  no follower in this archive yet (%s): the case waits for lanes/lib_follow_done.txt\n", sa1_last_error(ctx));
        sa1_character_destroy(ctx, lead);
        return;
    }
    CHECK(st == SA1_OK, "9 a Tails follower of Sonic (%s)", st == SA1_OK ? "ok" : sa1_last_error(ctx));
    if (st != SA1_OK) {
        sa1_character_destroy(ctx, lead);
        return;
    }
    w = sa1_character_world(ctx, fol);
    CHECK(w != NULL && w->slot == 1, "9 its world's slot is 1");
    for (p = 0; p < 30; p++) {
        tick(ctx, lead, 0, 0, 0, 0);
        tick(ctx, fol, 0, 0, 0, 0);
    }
    {
        uint32_t ptwp = r32(w, ADDR_PLAYER_TWP + 4u), proxy, tp0, tp1;
        float c[3], s[3], h[3];
        double a;
        sa1_state_ex st2;
        center_of(w, ptwp, c);
        a = (double)(r32(w, ptwp + 0x18u) & 0xFFFFu) * 2.0 * PI / 65536.0 + PI / 2.0;
        s[0] = c[0] + (float)(50.0 * cos(a));
        s[1] = c[1];
        s[2] = c[2] - (float)(50.0 * sin(a));
        to_host(s, h);
        CHECK(look_point(ctx, fol, h, 0) == SA1_OK, "9 POINT accepted");
        for (p = 0; p < 6; p++) {
            tick(ctx, lead, 0, 0, 0, 0);
            tick(ctx, fol, 0, 0, 0, 0);
        }
        state_of(ctx, fol, &st2);
        tp1 = find_task_exec(w, 0x8C0BE1CEu, 1);
        CHECK(tp1 != 0 && st2.head_angle[0] == 0x1800 && st2.look_owner == 1u, "9 POINT on slot 1: head %+d (+0x1800), owner %u", (int)st2.head_angle[0], (unsigned)st2.look_owner);
        proxy = r32(w, ADDR_PLAYER_TWP);
        tp0 = find_task_exec(w, look_exec(w->chr), 0);
        CHECK(proxy == 0 || r8s(w, r32(w, proxy + 0x3Cu) + 4u) == 0, "9 the proxy's event look untouched");
        CHECK(tp0 == 0 || ang16(r32(w, r32(w, tp0 + 0x24u) + 0x08u)) == 0, "9 the proxy's look work untouched");
        look_kind(ctx, fol, SA1_LOOK_CAMERA);
        g_t9_orig = w->look.post;
        w->look.post = t9_post;
        g_t9_steps = g_t9_bit = 0;
        for (p = 0; p < 20; p++) {
            tick(ctx, lead, 0, 1, 0, 0);
            tick(ctx, fol, 0, 0, 0, 0);
        }
        CHECK(g_t9_steps >= 40u && g_t9_bit == g_t9_steps, "9 CAMERA: the bit in per[1].on survives the AI's pad write (%u of %u steps)", (unsigned)g_t9_bit, (unsigned)g_t9_steps);
        look_kind(ctx, fol, SA1_LOOK_OFF);
        g_t9_steps = g_t9_bit = 0;
        for (p = 0; p < 10; p++) {
            tick(ctx, lead, 0, 1, 0, 0);
            tick(ctx, fol, 0, 0, 0, 0);
        }
        CHECK(g_t9_bit <= 1u, "9 OFF: the bit is gone (%u of %u steps)", (unsigned)g_t9_bit, (unsigned)g_t9_steps);
        w->look.post = g_t9_orig;
        CHECK(!w->fault.kind, "9 no fault (%s)", w->fault.kind ? w->fault.msg : "none");
    }
    sa1_character_destroy(ctx, fol);
    sa1_character_destroy(ctx, lead);
}
/* ---- 5: the game's look wins ------------------------------------------------------------------------------------------------ */
static float g_game_point[3];
static void timed_point_cb(struct sa1g_world *w, void *user)
{
    (void)user;
    (void)w;
    ((void (*)(uint8_t, void *, uint32_t))sa1_fn(SA1_GFN_C(FN_TIMED_POINT), 0u))(0u, sa1_decode(0x8C6B6E68u), 300u);
}
static void t5_game_wins(sa1_context *ctx)
{
    int pass, p;
    printf("\n== 5: a look the game sets (8C07C44E, 300 steps): it wins; SA1_LOOKF_OVERRIDE_GAME over it\n");
    for (pass = 0; pass < 2; pass++) {
        const kinddef *K = &KINDS[0];
        sa1_character_id id = make_char(ctx, K);
        sa1g_world *w;
        float left[3], s[3];
        int own2 = 0, right = 0, own1_after = 0, left_after = 0, own1_during = 0, left_during = 0;
        uint32_t ptwp;
        if (!id) {
            CHECK(0, "5 created");
            return;
        }
        for (p = 0; p < 4; p++) tick(ctx, id, 0, 0, 0, 0);
        w = sa1_character_world(ctx, id);
        rel_point(w, 90, 50, 0, left);
        for (p = 0; p < 200; p++) {
            sa1_state_ex st;
            look_point(ctx, id, left, pass ? SA1_LOOKF_OVERRIDE_GAME : 0u);
            if (p == 10) {
                float c[3];
                double a;
                uint32_t v;
                ptwp = r32(w, ADDR_PLAYER_TWP);
                center_of(w, ptwp, c);
                a = (double)(r32(w, ptwp + 0x18u) & 0xFFFFu) * 2.0 * PI / 65536.0 - PI / 2.0;   /* 90 right */
                s[0] = c[0] + (float)(50.0 * cos(a));
                s[1] = c[1];
                s[2] = c[2] - (float)(50.0 * sin(a));
                memcpy(g_game_point, s, sizeof s);
                /* the point in a free guest word of the arena the game's own setter copies it from */
                memcpy(&v, &s[0], 4); sa1g_world_wr32(w, 0x8C6B6E68u, v);
                memcpy(&v, &s[1], 4); sa1g_world_wr32(w, 0x8C6B6E6Cu, v);
                memcpy(&v, &s[2], 4); sa1g_world_wr32(w, 0x8C6B6E70u, v);
                sa1g_world_call(w, timed_point_cb, NULL);
            }
            tick(ctx, id, 0, 0, 0, 0);
            state_of(ctx, id, &st);
            if (p >= 12 && p < 145) {
                own2 += st.look_owner == 2u;
                right += st.head_angle[0] == -0x2000;
                own1_during += st.look_owner == 1u;
                left_during += st.head_angle[0] == 0x2000;
            }
            if (p >= 165) {
                own1_after += st.look_owner == 1u;
                left_after += st.head_angle[0] == 0x2000;
            }
        }
        if (!pass) {
            CHECK(own2 == 133 && right == 133, "5 the game's look: pairs 12-144: look_owner 2 (%d), the head on its point (%d)", own2, right);
        } else {
            CHECK(own1_during == 133 && left_during == 133, "5 SA1_LOOKF_OVERRIDE_GAME: from the first step the host's (owner 1 %d, head left %d)", own1_during, left_during);
        }
        CHECK(own1_after == 35 && left_after == 35, "5 after its 300 steps: the host's look again, owner 1 (%d, head left %d)", own1_after, left_after);
        CHECK(!w->fault.kind, "5 no fault");
        sa1_character_destroy(ctx, id);
    }
}

/* ---- 6: no game state ------------------------------------------------------------------------------------------------------- */
static int state_game_differs(const sa1_state_ex *a, const sa1_state_ex *b)
{
    size_t o1 = offsetof(sa1_state_ex, attach_valid), o2 = offsetof(sa1_state_ex, rings), o3 = offsetof(sa1_state_ex, look_kind);
    return memcmp(a, b, o1) != 0 || memcmp((const char *)a + o2, (const char *)b + o2, o3 - o2) != 0;
}
static void t6_no_state(sa1_context *ctx)
{
    int i, p, j;
    printf("\n== 6: no game state\n");
    for (i = 0; i < NKINDS; i++) {
        trio t;
        int diff = 0, kpairs = 0;
        if (!trio_make(ctx, &KINDS[i], &t)) {
            CHECK(0, "6 %s created", KINDS[i].name);
            trio_free(ctx, &t);
            continue;
        }
        for (p = 0; p < 90; p++) {
            sa1_state_ex s0, s1, s2;
            trio_look(ctx, &t, p >= 2);
            trio_tick(ctx, &t, p >= 30 ? 0.3f : 0.0f, p >= 30 ? 1.0f : 0.0f, p == 60 ? SA1_BUTTON_JUMP : 0u, 0.0f);
            state_of(ctx, t.id[0], &s0);
            state_of(ctx, t.id[1], &s1);
            state_of(ctx, t.id[2], &s2);
            diff += state_game_differs(&s0, &s1) || state_game_differs(&s0, &s2);
            kpairs++;
        }
        CHECK(diff == 0, "6 %s: %d of %d pairs differ with a look left / right (sa1_state_ex without the weld points and the tail)", KINDS[i].name, diff, kpairs);
        trio_free(ctx, &t);
    }
    for (j = 0; j < 2; j++) {
        trio t;
        int diff = 0;
        if (!trio_make(ctx, &KINDS[j], &t)) {
            trio_free(ctx, &t);
            continue;
        }
        for (p = 0; p < 140; p++) {
            sa1_state_ex s0, s1, s2;
            int q;
            if (p == 10) {
                sa1_board_desc d;
                memset(&d, 0, sizeof d);
                d.struct_size = sizeof d;
                d.on = SA1_BOARD_ON;
                for (q = 0; q < 3; q++) {
                    sa1_character_board(ctx, t.id[q], &d);
                }
            }
            trio_look(ctx, &t, p >= 40);
            trio_tick(ctx, &t, (p >= 100 && p < 130) ? 0.7f : 0.0f, p >= 30 ? 1.0f : 0.0f, 0u, 0.0f);
            state_of(ctx, t.id[0], &s0);
            state_of(ctx, t.id[1], &s1);
            state_of(ctx, t.id[2], &s2);
            diff += state_game_differs(&s0, &s1) || state_game_differs(&s0, &s2);
        }
        CHECK(diff == 0, "6 %s on the board: %d of 140 pairs differ", KINDS[j].name, diff);
        trio_free(ctx, &t);
    }
}

/* ---- 8: a rebuild keeps the look --------------------------------------------------------------------------------------------- */
static int rebuild(sa1_context *ctx, sa1g_world *w)
{
    sa1g_boot_cfg cfg;
    uint32_t notes = 0;
    int32_t land = w->land_mode, chr = w->chr;
    uint32_t cap = w->window_cap;
    float pos[3];
    uint32_t ang = w->start_ang_y;
    memcpy(pos, w->start_pos, sizeof pos);
    if (sa1g_world_apply_image(w, w->image) != SA1_OK) {
        return -1;
    }
    sa1g_boot_defaults(&cfg);
    cfg.chr = chr;
    cfg.flags |= SA1G_BOOT_POS;
    memcpy(cfg.pos, pos, sizeof cfg.pos);
    cfg.ang[1] = ang;
    cfg.land_mode = land;
    cfg.window_cap = cap;
    if (sa1g_world_boot(w, &cfg) != SA1_OK) {
        return -1;
    }
    return sa1ca_world_attach(ctx, w, &notes) == SA1_OK ? 0 : -1;
}
static void t8_rebuild(sa1_context *ctx)
{
    const kinddef *K = &KINDS[2];
    sa1_character_id id = make_char(ctx, K);
    sa1g_world *w;
    float h[3];
    sa1_state_ex st;
    int p;
    printf("\n== 8: an image rebuild keeps the look\n");
    if (!id) {
        CHECK(0, "8 created");
        return;
    }
    for (p = 0; p < 4; p++) tick(ctx, id, 0, 0, 0, 0);
    w = sa1_character_world(ctx, id);
    rel_point(w, 90, 50, 0, h);
    look_point(ctx, id, h, 0);
    for (p = 0; p < 4; p++) tick(ctx, id, 0, 0, 0, 0);
    state_of(ctx, id, &st);
    CHECK(st.head_angle[0] == 0x2000, "8 before: head %+d", (int)st.head_angle[0]);
    CHECK(rebuild(ctx, w) == 0, "8 the rebuild (%s)", w->dead ? w->fault.msg : "ok");
    /* the look task comes with the first pair, is found at the next pair's first step */
    for (p = 0; p < 4; p++) tick(ctx, id, 0, 0, 0, 0);
    state_of(ctx, id, &st);
    CHECK(st.look_kind == SA1_LOOK_POINT && st.look_owner == 1u && st.head_angle[0] == 0x2000, "8 after: kind %u, owner %u, head %+d", (unsigned)st.look_kind, (unsigned)st.look_owner, (int)st.head_angle[0]);
    sa1_character_destroy(ctx, id);
}

/* ---- 10: a world given nothing active against one never called: the whole arena ------------------------------------------------ */
static void t10_arena(sa1_context *ctx)
{
    int i, p;
    printf("\n== 10: only OFF / option 7 at 0 against a world never called: the whole arena after 300 pairs\n");
    for (i = 0; i < NKINDS + 1; i++) {
        const kinddef *K = i < NKINDS ? &KINDS[i] : NULL;
        sa1_character_id a, b;
        sa1g_world *wa, *wb;
        int v;
        if (K == NULL) {
            /* E-102 refuses: nothing is installed */
            static const kinddef G = { "E-102", SA1_CHAR_GAMMA, 0, 0, 0, 0, 0, 0, 0 };
            K = &G;
        }
        a = make_char(ctx, K);
        b = make_char(ctx, K);
        if (!a || !b) {
            CHECK(0, "10 %s created", K->name);
            continue;
        }
        wa = sa1_character_world(ctx, a);
        wb = sa1_character_world(ctx, b);
        v = K->kind == SA1_CHAR_GAMMA ? (look_kind(ctx, b, SA1_LOOK_OFF) == SA1_ERR_UNSUPPORTED) : (look_kind(ctx, b, SA1_LOOK_OFF) == SA1_OK);
        CHECK(v, "10 %s: OFF %s", K->name, K->kind == SA1_CHAR_GAMMA ? "refused" : "accepted");
        if (K->kind == SA1_CHAR_SUPER_SONIC || (K->flags & SA1_CREATE_SUPER_CAPABLE)) {
            CHECK(sa1_character_set_option(ctx, b, SA1_COPT_SUPER_LOOK, 0) == SA1_OK, "10 %s: option 7 at 0 accepted", K->name);
        }
        for (p = 0; p < 300; p++) {
            float sx = (p >= 100 && p < 130) ? 0.6f : 0.0f, sy = p >= 30 ? 1.0f : 0.0f;
            uint32_t bt = (p == 60 || p == 61 || p == 200 || p == 201) ? SA1_BUTTON_JUMP : 0u;
            tick(ctx, a, sx, sy, bt, 0.0f);
            tick(ctx, b, sx, sy, bt, 0.0f);
        }
        {
            size_t k, first = (size_t)-1, count = 0;
            for (k = 0; k < SA1G_ARENA_SIZE; k++) {
                if (wa->ram[k] != wb->ram[k]) {
                    if (first == (size_t)-1) {
                        first = k;
                    }
                    count++;
                }
            }
            CHECK(count == 0, "10 %s: the arena identical after 300 pairs (%u bytes differ, first at %08X)", K->name, (unsigned)count, (unsigned)(first == (size_t)-1 ? 0u : 0x8C000000u + (uint32_t)first));
        }
        sa1_character_destroy(ctx, a);
        sa1_character_destroy(ctx, b);
    }
}

/* ---- 11: Big fishing and a look: the two lanes share no state -------------------------------------------------------------------- */
static void cast_script(int p, float *sy, uint32_t *b)
{
    *sy = 0;
    *b = 0;
    if (p >= 10 && p < 50) *b = SA1_BUTTON_ACTION;
    if (p >= 25 && p < 50) *sy = 1.0f;
}
typedef struct fish_rec { sa1_camera_state cam; float lure[3], t2[3]; int lm; } fish_rec;
static int fish_run(const char *gdi, int look, fish_rec *o, int n)
{
    sa1_context *ctx = make_ctx(gdi, 1);
    static const kinddef BIG = { "Big", SA1_CHAR_BIG, 0, 0, 0, 0, 0, 0, 0 };
    sa1_character_id id = ctx ? make_char(ctx, &BIG) : 0;
    sa1g_world *w;
    int p, running = 0;
    if (!id) {
        return -1;
    }
    w = sa1_character_world(ctx, id);
    for (p = 0; p < n; p++) {
        float sy, h[3];
        uint32_t b;
        sa1_input in;
        cast_script(p, &sy, &b);
        if (look && p >= 2) {
            rel_point(w, 90, 50, 0, h);
            look_point(ctx, id, h, 0);
        }
        memset(&in, 0, sizeof in);
        in.struct_size = sizeof in;
        in.stick_y = sy;
        in.buttons = b;
        in.host_flags = SA1_INPUT_GAME_CAMERA;
        if (sa1_character_tick(ctx, id, &in, 1.0f / 30.0f, NULL, NULL) != SA1_OK) {
            printf("    tick: %s\n", sa1_last_error(ctx));
            break;
        }
        memset(&o[p], 0, sizeof o[p]);
        o[p].cam.struct_size = sizeof o[p].cam;
        sa1_character_camera(ctx, id, 1.0f, &o[p].cam);
        if (o[p].cam.flags & SA1_CAMSTATE_RUNNING) {
            running++;
        }
        {
            uint32_t k;
            for (k = 0; k < 3u; k++) {
                o[p].lure[k] = rf(w, ADDR_BPOS + 4u * k);
                o[p].t2[k] = rf(w, ADDR_BPOS + 12u + 4u * k);
            }
            {
                uint32_t lt = r32(w, 0x8C7A40C8u);
                o[p].lm = lt ? r8s(w, r32(w, lt + 0x20u)) : -1;
            }
        }
    }
    sa1_shutdown(ctx);
    return running;
}
static fish_rec g_fa[200], g_fb[200];
static void t11_fishing(const char *gdi)
{
    int ra, rb, diff, p;
    printf("\n== 11: Big with the fishing camera RUNNING and a POINT look\n");
    ra = fish_run(gdi, 0, g_fa, 200);
    rb = fish_run(gdi, 1, g_fb, 200);
    diff = 0;
    for (p = 0; p < 200; p++) {
        diff += memcmp(&g_fa[p], &g_fb[p], sizeof g_fa[p]) != 0;
    }
    CHECK(ra > 100 && ra == rb, "11 the camera ran in the same pairs (%d / %d)", ra, rb);
    CHECK(diff == 0, "11 the camera and lure numbers equal a run without the look (%d of 200 pairs differ)", diff);
}

/* ---- 12: refusals -------------------------------------------------------------------------------------------------------------- */
static void t12_refusals(sa1_context *ctx)
{
    static const kinddef E102 = { "E-102", SA1_CHAR_GAMMA, 0, 0, 0, 0, 0, 0, 0 };
    sa1_character_id id = make_char(ctx, &KINDS[0]), g = make_char(ctx, &E102), pup = 0, lazy = 0;
    sa1_look l;
    sa1_character_desc d;
    printf("\n== 12: refusals\n");
    memset(&l, 0, sizeof l);
    l.struct_size = sizeof l;
    CHECK(sa1_character_look(NULL, id, &l) == SA1_ERR_INVALID_ARGUMENT, "12 no context");
    CHECK(sa1_character_look(ctx, id, NULL) == SA1_ERR_INVALID_ARGUMENT, "12 no look: %s", sa1_last_error(ctx));
    l.struct_size = (uint32_t)offsetof(sa1_look, pitch);
    CHECK(sa1_character_look(ctx, id, &l) == SA1_ERR_INVALID_ARGUMENT, "12 struct_size: %s", sa1_last_error(ctx));
    l.struct_size = sizeof l;
    l.kind = 4u;
    CHECK(sa1_character_look(ctx, id, &l) == SA1_ERR_INVALID_ARGUMENT, "12 kind 4: %s", sa1_last_error(ctx));
    l.kind = SA1_LOOK_POINT;
    l.flags = 2u;
    CHECK(sa1_character_look(ctx, id, &l) == SA1_ERR_INVALID_ARGUMENT, "12 unknown flags: %s", sa1_last_error(ctx));
    l.kind = SA1_LOOK_OFF;
    l.flags = SA1_LOOKF_OVERRIDE_GAME;
    CHECK(sa1_character_look(ctx, id, &l) == SA1_ERR_INVALID_ARGUMENT, "12 OVERRIDE with OFF: %s", sa1_last_error(ctx));
    l.kind = SA1_LOOK_CAMERA;
    CHECK(sa1_character_look(ctx, id, &l) == SA1_ERR_INVALID_ARGUMENT, "12 OVERRIDE with CAMERA: %s", sa1_last_error(ctx));
    l.flags = 0u;
    l.kind = SA1_LOOK_POINT;
    l.point[1] = (float)nan("");
    CHECK(sa1_character_look(ctx, id, &l) == SA1_ERR_INVALID_ARGUMENT, "12 a NaN: %s", sa1_last_error(ctx));
    l.point[1] = 1e31f;
    CHECK(sa1_character_look(ctx, id, &l) == SA1_ERR_INVALID_ARGUMENT, "12 1e31: %s", sa1_last_error(ctx));
    l.point[1] = 0.0f;
    l.kind = SA1_LOOK_ANGLES;
    l.yaw = (float)HUGE_VAL;
    CHECK(sa1_character_look(ctx, id, &l) == SA1_ERR_INVALID_ARGUMENT, "12 an infinite yaw: %s", sa1_last_error(ctx));
    l.yaw = 0.0f;
    CHECK(sa1_character_look(ctx, 12345, &l) == SA1_ERR_NOT_FOUND, "12 no such character: %s", sa1_last_error(ctx));
    CHECK(sa1_character_look(ctx, g, &l) == SA1_ERR_UNSUPPORTED, "12 E-102: %s", sa1_last_error(ctx));
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.kind = SA1_CHAR_SONIC;
    d.flags = SA1_CREATE_PUPPET;
    if (sa1_character_create_ex(ctx, &d, &pup) == SA1_OK) {
        CHECK(sa1_character_look(ctx, pup, &l) == SA1_ERR_UNSUPPORTED, "12 a puppet: %s", sa1_last_error(ctx));
        sa1_character_destroy(ctx, pup);
    }
    d.flags = 0;
    if (sa1_character_create_ex(ctx, &d, &lazy) == SA1_OK) {
        CHECK(sa1_character_world(ctx, lazy) == NULL && sa1_character_look(ctx, lazy, &l) == SA1_OK && sa1_character_world(ctx, lazy) != NULL, "12 a character without a world gets it built");
        sa1_character_destroy(ctx, lazy);
    }
    /* a replay world */
    {
        sa1g_world *w = sa1_character_world(ctx, id);
        w->replay = 1;
        CHECK(sa1_character_look(ctx, id, &l) == SA1_ERR_UNSUPPORTED, "12 a replay world: %s", sa1_last_error(ctx));
        w->replay = 0;
    }
    sa1_character_destroy(ctx, id);
    sa1_character_destroy(ctx, g);
}

int main(int argc, char **argv)
{
    const char *gdi = getenv("SA1_TEST_GDI");
    sa1_context *ctx;
    int i;
    setvbuf(stdout, NULL, _IOLBF, 0);
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--only") && i + 1 < argc) {
            g_only = argv[++i];
        } else {
            gdi = argv[i];
        }
    }
    if (gdi == NULL || !*gdi) {
        printf("usage: look_test [--only 1,2,...] GDI\n");
        return 2;
    }
    printf("libsa1 %s look_test: %d-bit\n", sa1_version_string(), (int)(sizeof(void *) * 8));
    ctx = make_ctx(gdi, 0);
    if (ctx == NULL) {
        return 1;
    }
    if (want("1")) t1_point(ctx);
    if (want("2")) t2_angles(ctx);
    if (want("3")) t3_camera(ctx);
    if (want("4")) t4_render(ctx);
    if (want("5")) t5_game_wins(ctx);
    if (want("6")) t6_no_state(ctx);
    if (want("8")) t8_rebuild(ctx);
    if (want("9")) t9_follower(ctx);
    if (want("10")) t10_arena(ctx);
    if (want("12")) t12_refusals(ctx);
    sa1_shutdown(ctx);
    if (want("11")) t11_fishing(gdi);
    printf("DETERMINISM look %08X\n", (unsigned)g_det);
    printf("look_test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
