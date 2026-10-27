/*
 * preview_test.c - round 0.9.4b (094b-lib-preview): the object previews (include/libsa1.h "object previews",
 * SA1_API_PREVIEW; src/sa1_preview.c, src/world/sa1_world_preview.c; sadc docs/research/objects2_plan.md 5.3 and 6.2 with its
 * Review R.1-R.3, R.10, R.20, R.21; objects2_view.md 3 and 5).
 *
 *   preview_test [--only NAME[,NAME]] [--list] [--verbose] [--guard-alloc] GDI     (GDI: or $SA1_TEST_GDI)
 *
 * Built and run by tests/build_preview.py (a snapshot of build/<target>/libsa1.a and LIB's headers). Every scene: native
 * coordinates (SA1_COORDS_PRESET_NATIVE: y up, 1 host unit = 1 SA1 unit = 4 HU in G-SA1). The oracle of the parity cases is
 * the game code in a character world: the same record placed through the SET layer (sa1_set_add) 100 units in front of the
 * world's virtual eye and in the air (the character stands on a 20 x 20 platform, nothing lies under the record: no blob
 * shadow on either side).
 *
 * ---- API notes (for 094b-gsa1-objects and the release step) -----------------------------------------------------------
 * Calls (the thread that ticks the context's characters; never inside a character tick):
 *   sa1_preview_type_get(ctx, type, &t)   available (the type's exec is native), rotation (RING OUTER = the record's yaw
 *                                         only, the spin's start; O_GRING INNER; ITEMBOX NONE; ITEMBOX_AIR OUTER = full ZXY),
 *                                         settle_steps (RING / O_GRING 0, ITEMBOX 47, ITEMBOX_AIR 23: measured, the steps
 *                                         run when the draws first have their final size; a broken ITEMBOX has no grow-in:
 *                                         the host's hand-over hold uses settle_steps for an intact box only)
 *   sa1_preview_add(ctx, &desc, &id)      kind Sonic 0, Tails 2, Knuckles 3, Amy 5, E-102 6, Big 7 (Eggman 1, Tikal 4,
 *                                         Super Sonic 8: SA1_ERR_UNSUPPORTED - G-SA1 uses Sonic's for them); RING params
 *                                         (0, 0, 0) only; O_GRING layouts reaching beyond 990 SA1 units from their centre
 *                                         (a line: |spacing| x ceil(n / 2); a circle: |radius|): SA1_ERR_LIMIT (such
 *                                         groups are drawn from RING copies at sa1_preview_ring_places anyway); state must
 *                                         hold SA1_SET_STATE_PRESENT (a taken ring / air box is not a preview: draw nothing)
 *   sa1_preview_tick(ctx, dt)             whole pairs (1/30 s) of every showcase with copies (at most 8 a call, the rest
 *                                         dropped, as character ticks); a showcase without copies is freed 60 pairs later,
 *                                         so keep ticking about 2-3 s after the last remove (review R.18)
 *   sa1_preview_render(ctx, id, a, &r)    OBJECT SPACE (host units / axes, origin = the record's position), the copy's own
 *                                         draws only; position 0, yaw 0, flags 0, generation = the copy's pairs; alpha 1 =
 *                                         last pair (the game's 30 Hz), 0..1 a vertex lerp (same draws only), < 0 the
 *                                         previews' accumulator. Before its first pair a copy has no draws (SA1_OK)
 *   sa1_preview_ring_places(ctx, kind, params, rotation, places, bits, cap, &n)   the game's own ring places of an O_GRING
 *                                         record (object space) and each ring's user bit 1 << i (a line i = 0..n, a circle
 *                                         i = 1..n): draw a RING copy at each untaken place
 * Measured (this test, Sonic's showcase unless said; see its output for every kind):
 *   RING 1 draw / 96 triangles (archive 2 texture 50), turns 4.00 degrees a pair; O_GRING line (7, 10, 0) 1 draw / 768
 *   (8 rings), circle (7, 20, 1) 672 (7 rings), line with bits 0x05: 6 rings, circle (8, 20, 1) with 0x7E: 2 rings (the
 *   game's Sint8 quirk); ITEMBOX 8 draws / 116 triangles, panel turns 6.00 degrees a pair; broken ITEMBOX 4 / 54;
 *   ITEMBOX_AIR 7 / 98 (all its contents). Extra Life panel: Sonic 2:31, Tails 1:4, Knuckles 2:0, Amy 2:1, E-102 5:3, Big 3:2.
 *   Every draw of a copy carries SA1_DRAW_OBJECT (and SA1_DRAW_EFFECT: the OBJ_REGULAR archive); no blob shadows.
 * README lines: "Without a character (and in the spawn menu) G-SA1 shows the game's own rings, ring lines, circles and
 *   item boxes from a hidden copy of the game with no player in it: they spin and turn like the real ones, but have no
 *   shadow underneath, every picture of one kind of object spins in step, and nothing can be collected without a
 *   character."
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
#include "sa1_preview.h"
#include "test_util.h"
#include "guard_alloc.c"    /* SA1_GUARD_ALLOC=1 or --guard-alloc: every library heap block against a guard page */
#if defined(_WIN32)
#include <windows.h>
BOOL WINAPI K32GetProcessMemoryInfo(HANDLE, void *, DWORD);
#endif

static int g_fail, g_ok, g_verbose;
static const char *g_gdi;
static sa1_context *g_ctx;

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
#define NOTE(...)                                                                                                       \
    do {                                                                                                                \
        printf("  note  ");                                                                                             \
        printf(__VA_ARGS__);                                                                                            \
        printf("\n");                                                                                                   \
    } while (0)

#define PAIR (1.0f / 30.0f)
#define A_COND 0x8C79A3E0u
static const char *k_kname[9] = { "Sonic", "Eggman", "Tails", "Knuckles", "Tikal", "Amy", "E-102", "Big", "Super Sonic" };
static const int32_t k_kinds[6] = { SA1_CHAR_SONIC, SA1_CHAR_TAILS, SA1_CHAR_KNUCKLES, SA1_CHAR_AMY, SA1_CHAR_GAMMA,
                                    SA1_CHAR_BIG };

/* ---- the counting allocator (the lifetime case's "0 live blocks") ----------------------------------------------------- */
static int64_t g_live;
static void *cnt_alloc(void *u, size_t n)
{
    void *p = ga_malloc(n);
    (void)u;
    if (p != NULL) {
        g_live++;
    }
    return p;
}
static void cnt_free(void *u, void *p)
{
    (void)u;
    if (p != NULL) {
        g_live--;
        ga_free(p);
    }
}

static void log_cb(void *user, int32_t level, const char *msg)
{
    (void)user;
    if (level >= SA1_LOG_WARN || g_verbose) {
        printf("    [libsa1] %s\n", msg);
    }
}

/* a context with the disc (the six kinds with a showcase); counting: the counting allocator */
static sa1_context *mkctx(int counting, int load)
{
    sa1_context *ctx = NULL;
    sa1_host h;
    sa1_coords co;
    sa1_load_options lo;
    sa1_status st;
    sa1_host_defaults(&h);
    ga_host(&h);
    if (counting) {
        h.alloc = cnt_alloc;
        h.free = cnt_free;
    }
    h.log = log_cb;
    h.log_level = SA1_LOG_WARN;
    if (sa1_init(&h, &ctx) != SA1_OK) {
        printf("  sa1_init failed\n");
        return NULL;
    }
    memset(&co, 0, sizeof co);
    co.struct_size = sizeof co;
    sa1_coords_preset(SA1_COORDS_PRESET_NATIVE, &co);
    sa1_set_coords(ctx, &co);
    if (!load) {
        return ctx;
    }
    memset(&lo, 0, sizeof lo);
    lo.struct_size = sizeof lo;
    lo.characters = (1u << SA1_CHAR_SONIC) | (1u << SA1_CHAR_TAILS) | (1u << SA1_CHAR_KNUCKLES) | (1u << SA1_CHAR_AMY) |
                    (1u << SA1_CHAR_GAMMA) | (1u << SA1_CHAR_BIG);
    lo.flags = SA1_LOAD_DEFAULT;
    st = sa1_load_disc(ctx, g_gdi, &lo);
    if (SA1_FAILED(st)) {
        printf("  sa1_load_disc: %s: %s\n", sa1_status_string(st), sa1_last_error(ctx));
        sa1_shutdown(ctx);
        return NULL;
    }
    return ctx;
}

/* the static collision: a square of half size h at y 0 around the origin */
static void floor_of(sa1_context *ctx, float h)
{
    sa1_collision_tri t[2];
    float q[4][3] = { { -h, 0, -h }, { -h, 0, h }, { h, 0, h }, { h, 0, -h } };
    memset(t, 0, sizeof t);
    memcpy(t[0].v[0], q[0], 12);
    memcpy(t[0].v[1], q[1], 12);
    memcpy(t[0].v[2], q[2], 12);
    memcpy(t[1].v[0], q[0], 12);
    memcpy(t[1].v[1], q[2], 12);
    memcpy(t[1].v[2], q[3], 12);
    t[0].surface = t[1].surface = SA1_SURFACE_SOLID;
    if (SA1_FAILED(sa1_collision_set_static(ctx, t, 2))) {
        printf("  set_static: %s\n", sa1_last_error(ctx));
    }
}

static sa1_character_id mk(sa1_context *ctx, int32_t kind, float x, float y, float z)
{
    sa1_character_desc d;
    sa1_character_id id = 0;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.kind = kind;
    d.flags = SA1_CREATE_WORLD | SA1_CREATE_SA1_SPACE | SA1_CREATE_ANGLES;
    d.position[0] = x;
    d.position[1] = y;
    d.position[2] = z;
    if (SA1_FAILED(sa1_character_create_ex(ctx, &d, &id))) {
        printf("  create %s: %s\n", k_kname[kind], sa1_last_error(ctx));
        return 0;
    }
    return id;
}

static int g_tick_faults;
static int tick_in(sa1_context *ctx, sa1_character_id id, float sx, float sy, uint32_t buttons)
{
    sa1_input in;
    sa1_status st;
    memset(&in, 0, sizeof in);
    in.struct_size = sizeof in;
    in.stick_x = sx;
    in.stick_y = sy;
    in.buttons = buttons;
    st = sa1_character_tick(ctx, id, &in, PAIR, NULL, NULL);
    if (st != SA1_OK) {
        g_tick_faults++;
        printf("  tick: %s: %s\n", sa1_status_string(st), sa1_last_error(ctx));
        return -1;
    }
    return 0;
}
static int tick(sa1_context *ctx, sa1_character_id id)
{
    return tick_in(ctx, id, 0.0f, 0.0f, 0u);
}

static int g_preview_faults;
static sa1_status ptick(sa1_context *ctx)
{
    sa1_status st = sa1_preview_tick(ctx, PAIR);
    if (st != SA1_OK) {
        g_preview_faults++;
        if (g_verbose) {
            printf("  preview tick: %s: %s\n", sa1_status_string(st), sa1_last_error(ctx));
        }
    }
    return st;
}

static void drain(sa1_context *ctx)
{
    sa1_event ev[64];
    sa1_set_event se[64];
    uint32_t n;
    do {
        n = 0;
        sa1_poll_events(ctx, ev, 64, &n);
    } while (n == 64);
    do {
        n = 0;
        sa1_poll_set_events(ctx, se, 64, &n);
    } while (n == 64);
}

/* ---- descriptions --------------------------------------------------------------------------------------------------- */
typedef struct pdesc {
    const char *name;
    uint32_t type;
    float p[3];
    uint32_t state, bits;
} pdesc;
#define PD_N 24u
static const pdesc k_desc[PD_N] = {
    { "RING", SA1_OBJTYPE_RING, { 0, 0, 0 }, 1, 0 },
    { "line (7, 10, 0)", SA1_OBJTYPE_O_GRING, { 7, 10, 0 }, 1, 0 },
    { "line (7, 10, 0) bits 05", SA1_OBJTYPE_O_GRING, { 7, 10, 0 }, 1, 0x05 },
    { "circle (7, 20, 1)", SA1_OBJTYPE_O_GRING, { 7, 20, 1 }, 1, 0 },
    { "circle (8, 20, 1) bits 7E", SA1_OBJTYPE_O_GRING, { 8, 20, 1 }, 1, 0x7E },
    { "ITEMBOX 0", SA1_OBJTYPE_ITEMBOX, { 0, 0, 0 }, 1, 0 },
    { "ITEMBOX 1", SA1_OBJTYPE_ITEMBOX, { 1, 0, 0 }, 1, 0 },
    { "ITEMBOX 2", SA1_OBJTYPE_ITEMBOX, { 2, 0, 0 }, 1, 0 },
    { "ITEMBOX 3", SA1_OBJTYPE_ITEMBOX, { 3, 0, 0 }, 1, 0 },
    { "ITEMBOX 4", SA1_OBJTYPE_ITEMBOX, { 4, 0, 0 }, 1, 0 },
    { "ITEMBOX 5", SA1_OBJTYPE_ITEMBOX, { 5, 0, 0 }, 1, 0 },
    { "ITEMBOX 6", SA1_OBJTYPE_ITEMBOX, { 6, 0, 0 }, 1, 0 },
    { "ITEMBOX 7", SA1_OBJTYPE_ITEMBOX, { 7, 0, 0 }, 1, 0 },
    { "ITEMBOX 8", SA1_OBJTYPE_ITEMBOX, { 8, 0, 0 }, 1, 0 },
    { "ITEMBOX 3 broken", SA1_OBJTYPE_ITEMBOX, { 3, 0, 0 }, 3, 0 },
    { "ITEMBOX_AIR 0", SA1_OBJTYPE_ITEMBOX_AIR, { 0, 0, 0 }, 1, 0 },
    { "ITEMBOX_AIR 1", SA1_OBJTYPE_ITEMBOX_AIR, { 1, 0, 0 }, 1, 0 },
    { "ITEMBOX_AIR 2", SA1_OBJTYPE_ITEMBOX_AIR, { 2, 0, 0 }, 1, 0 },
    { "ITEMBOX_AIR 3", SA1_OBJTYPE_ITEMBOX_AIR, { 3, 0, 0 }, 1, 0 },
    { "ITEMBOX_AIR 4", SA1_OBJTYPE_ITEMBOX_AIR, { 4, 0, 0 }, 1, 0 },
    { "ITEMBOX_AIR 5", SA1_OBJTYPE_ITEMBOX_AIR, { 5, 0, 0 }, 1, 0 },
    { "ITEMBOX_AIR 6", SA1_OBJTYPE_ITEMBOX_AIR, { 6, 0, 0 }, 1, 0 },
    { "ITEMBOX_AIR 7", SA1_OBJTYPE_ITEMBOX_AIR, { 7, 0, 0 }, 1, 0 },
    { "ITEMBOX_AIR 8", SA1_OBJTYPE_ITEMBOX_AIR, { 8, 0, 0 }, 1, 0 },
};

static sa1_status padd(sa1_context *ctx, int32_t kind, uint32_t type, const float p[3], uint32_t state, uint32_t bits,
                       const float rot[9], uint32_t *id)
{
    sa1_preview_desc d;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.kind = kind;
    d.type = type;
    memcpy(d.params, p, sizeof d.params);
    if (rot != NULL) {
        memcpy(d.rotation, rot, sizeof d.rotation);
    }
    d.state = state;
    d.user_bits = bits;
    return sa1_preview_add(ctx, &d, id);
}

static uint32_t rec_add(sa1_context *ctx, uint32_t type, const float pos[3], const float p[3], const float rot[9],
                        uint32_t state, uint32_t bits)
{
    sa1_set_record r;
    uint32_t id = 0;
    memset(&r, 0, sizeof r);
    r.struct_size = sizeof r;
    r.type = type;
    memcpy(r.position, pos, sizeof r.position);
    memcpy(r.params, p, sizeof r.params);
    if (rot != NULL) {
        memcpy(r.rotation, rot, sizeof r.rotation);
    }
    if (SA1_FAILED(sa1_set_add(ctx, &r, &id))) {
        printf("  sa1_set_add: %s\n", sa1_last_error(ctx));
        return 0;
    }
    if (state != SA1_SET_STATE_PRESENT || bits != 0) {
        sa1_set_state(ctx, id, state, bits);
    }
    return id;
}

/* ---- rotations (standard right-handed, column vectors, row-major; the SET layer's convention) ------------------------ */
static void mat_mul(const float a[9], const float b[9], float o[9])
{
    float t[9];
    int i, j;
    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            t[3 * i + j] = a[3 * i] * b[j] + a[3 * i + 1] * b[3 + j] + a[3 * i + 2] * b[6 + j];
        }
    }
    memcpy(o, t, sizeof t);
}
static void rot_axis(int axis, double deg, float m[9])
{
    double a = deg * 3.14159265358979323846 / 180.0, c = cos(a), s = sin(a);
    memset(m, 0, 9 * sizeof(float));
    if (axis == 0) {
        m[0] = 1; m[4] = (float)c; m[5] = (float)-s; m[7] = (float)s; m[8] = (float)c;
    } else if (axis == 1) {
        m[4] = 1; m[0] = (float)c; m[2] = (float)s; m[6] = (float)-s; m[8] = (float)c;
    } else {
        m[8] = 1; m[0] = (float)c; m[1] = (float)-s; m[3] = (float)s; m[4] = (float)c;
    }
}
/* Rz(z) Rx(x) Ry(y) (the game's ZXY order) */
static void rot_zxy(double z, double x, double y, float m[9])
{
    float a[9], b[9], c[9];
    rot_axis(2, z, a);
    rot_axis(0, x, b);
    rot_axis(1, y, c);
    mat_mul(a, b, m);
    mat_mul(m, c, m);
}

/* ---- the triangles of a render ----------------------------------------------------------------------------------------- */
typedef struct tris {
    uint32_t nd, nt, ns, nl;
    int32_t tex[128][2];
    uint32_t flags[128];
    double *p;                  /* 9 per triangle, relative to the origin given */
    uint32_t cap;
} tris;

static void tris_free(tris *t)
{
    free(t->p);
    memset(t, 0, sizeof *t);
}

/* objects_only: only SA1_DRAW_OBJECT draws (a character world's render) */
static void tris_of(const sa1_render *r, const float org[3], int objects_only, tris *o)
{
    uint32_t i, t, k;
    o->nd = o->nt = o->ns = o->nl = 0;
    if (r == NULL) {
        return;
    }
    for (i = 0; i < r->num_draws; i++) {
        const sa1_draw *d = &r->draws[i];
        if (objects_only && !(d->flags & SA1_DRAW_OBJECT)) {
            continue;
        }
        if (o->nd < 128u) {
            o->tex[o->nd][0] = d->archive;
            o->tex[o->nd][1] = d->texture;
            o->flags[o->nd] = d->flags;
        }
        o->nd++;
        if (d->prim == SA1_PRIM_SPRITES) {
            o->ns += d->count;
            continue;
        }
        if (d->prim == SA1_PRIM_LINES) {
            o->nl += d->count;
            continue;
        }
        if (o->nt + d->count > o->cap) {
            o->cap = (o->nt + d->count) * 2u + 64u;
            o->p = (double *)realloc(o->p, (size_t)o->cap * 9u * sizeof(double));
        }
        for (t = d->first; t < d->first + d->count && t < r->num_triangles; t++) {
            for (k = 0; k < 9; k++) {
                o->p[9u * o->nt + k] = (double)r->positions[9u * t + k] - (double)org[k % 3u];
            }
            o->nt++;
        }
    }
}

/* a and b: the same draws (count, triangles, textures, flags) and positions within tol; *worst the largest difference */
static int tris_same(const tris *a, const tris *b, double tol, double *worst, char *why, size_t whylen)
{
    uint32_t i;
    double w = 0.0;
    *worst = 0.0;
    if (a->nd != b->nd || a->nt != b->nt || a->ns != b->ns || a->nl != b->nl) {
        snprintf(why, whylen, "draws %u / %u, triangles %u / %u, sprites %u / %u, lines %u / %u", a->nd, b->nd, a->nt,
                 b->nt, a->ns, b->ns, a->nl, b->nl);
        return 0;
    }
    for (i = 0; i < a->nd && i < 128u; i++) {
        if (a->tex[i][0] != b->tex[i][0] || a->tex[i][1] != b->tex[i][1]) {
            snprintf(why, whylen, "draw %u texture %d:%d / %d:%d", i, a->tex[i][0], a->tex[i][1], b->tex[i][0],
                     b->tex[i][1]);
            return 0;
        }
        if (a->flags[i] != b->flags[i]) {
            snprintf(why, whylen, "draw %u flags %04X / %04X", i, a->flags[i], b->flags[i]);
            return 0;
        }
    }
    for (i = 0; i < 9u * a->nt; i++) {
        double d = fabs(a->p[i] - b->p[i]);
        if (d > w) {
            w = d;
        }
    }
    *worst = w;
    if (w > tol) {
        snprintf(why, whylen, "positions differ by %.5f", w);
        return 0;
    }
    snprintf(why, whylen, "ok");
    return 1;
}

static uint32_t render_crc(const sa1_render *r, uint32_t crc)
{
    uint32_t i;
    if (r == NULL) {
        return crc;
    }
    crc = tu_crc32(crc, (const uint8_t *)&r->num_draws, 4);
    crc = tu_crc32(crc, (const uint8_t *)&r->num_triangles, 4);
    crc = tu_crc32(crc, (const uint8_t *)&r->generation, 4);
    for (i = 0; i < r->num_draws; i++) {
        const sa1_draw *d = &r->draws[i];
        crc = tu_crc32(crc, (const uint8_t *)&d->prim, 4);
        crc = tu_crc32(crc, (const uint8_t *)&d->first, 4);
        crc = tu_crc32(crc, (const uint8_t *)&d->count, 4);
        crc = tu_crc32(crc, (const uint8_t *)&d->archive, 4);
        crc = tu_crc32(crc, (const uint8_t *)&d->texture, 4);
        crc = tu_crc32(crc, (const uint8_t *)&d->material, 4);
        crc = tu_crc32(crc, (const uint8_t *)&d->flags, 4);
        crc = tu_crc32(crc, &d->blend_src, 2);
    }
    if (r->num_triangles) {
        crc = tu_crc32(crc, (const uint8_t *)r->positions, 36u * r->num_triangles);
        crc = tu_crc32(crc, (const uint8_t *)r->normals, 36u * r->num_triangles);
        crc = tu_crc32(crc, (const uint8_t *)r->uvs, 24u * r->num_triangles);
        crc = tu_crc32(crc, r->colors, 12u * r->num_triangles);
    }
    if (r->num_sprites) {
        crc = tu_crc32(crc, (const uint8_t *)r->sprites, (uint32_t)(sizeof(sa1_sprite) * r->num_sprites));
    }
    if (r->num_line_vertices) {
        crc = tu_crc32(crc, (const uint8_t *)r->line_vertices, (uint32_t)(sizeof(sa1_line_vertex) * r->num_line_vertices));
    }
    return crc;
}

static double azimuth(double x, double z)
{
    return atan2(z, x) * 180.0 / 3.14159265358979323846;
}
/* the vertex (of the first nv) farthest from the vertical axis through (cx, cz): the steadiest azimuth */
static uint32_t far_vertex(const double *p, uint32_t nv, double cx, double cz)
{
    uint32_t v, best = 0;
    double bd = -1.0;
    for (v = 0; v < nv; v++) {
        double dx = p[3u * v] - cx, dz = p[3u * v + 2u] - cz, d = dx * dx + dz * dz;
        if (d > bd) {
            bd = d;
            best = v;
        }
    }
    return best;
}

static double wrap180(double d)
{
    while (d > 180.0) {
        d -= 360.0;
    }
    while (d < -180.0) {
        d += 360.0;
    }
    return d;
}

/* ---- (1) parity with the SET path ---------------------------------------------------------------------------------------- */
static const int32_t k_life[8][2] = { { 2, 31 }, { -1, -1 }, { 1, 4 }, { 2, 0 }, { -1, -1 }, { 2, 1 }, { 5, 3 }, { 3, 2 } };

static void t_parity(void)
{
    sa1_context *ctx = g_ctx;
    static const uint32_t at[4] = { 1, 3, 23, 30 };
    const float rec_pos[3] = { 60.0f, 20.0f, 0.0f }, zero[3] = { 0, 0, 0 };
    uint32_t ki, di, p, j;
    printf("parity (1): every type, content and kind: a copy against the same record in a character world of that kind, "
           "pairs 1, 3, 23, 30 after SPAWNED (exec ages 1, 5, 45, 59), the record 100 u in front of the virtual eye and in "
           "the air\n");
    floor_of(ctx, 10.0f);
    for (ki = 0; ki < 6; ki++) {
        int32_t kind = k_kinds[ki];
        sa1_character_id id = mk(ctx, kind, 0.0f, 1.0f, 0.0f);
        uint32_t cp[PD_N], bad = 0, compared = 0;
        double worst_all = 0.0;
        tris a, b;
        memset(&a, 0, sizeof a);
        memset(&b, 0, sizeof b);
        if (!id) {
            CHECK(0, "%s: no character world", k_kname[kind]);
            continue;
        }
        sa1_preview_clear(ctx);
        for (p = 0; p < 4; p++) {
            tick(ctx, id);
        }
        for (di = 0; di < PD_N; di++) {
            const pdesc *D = &k_desc[di];
            uint32_t rid = rec_add(ctx, D->type, rec_pos, D->p, NULL, D->state, D->bits), ok = 1;
            sa1_status st = padd(ctx, kind, D->type, D->p, D->state, D->bits, NULL, &cp[di]);
            char why[160] = "";
            if (!rid || st != SA1_OK) {
                CHECK(0, "%s %s: record %u, copy %s (%s)", k_kname[kind], D->name, rid, sa1_status_string(st),
                      sa1_last_error(ctx));
                cp[di] = 0;
                continue;
            }
            for (p = 1, j = 0; p <= 30; p++) {
                const sa1_render *rr = NULL, *rc = NULL;
                double worst = 0.0;
                tick(ctx, id);
                ptick(ctx);
                if (p != at[j]) {
                    continue;
                }
                j++;
                sa1_character_render(ctx, id, 1.0f, &rr);
                sa1_preview_render(ctx, cp[di], 1.0f, &rc);
                tris_of(rr, rec_pos, 1, &a);
                tris_of(rc, zero, 0, &b);
                compared++;
                if (!tris_same(&a, &b, 0.01, &worst, why, sizeof why)) {
                    if (ok) {
                        printf("        %s %s pair %u: record / copy: %s\n", k_kname[kind], D->name, p, why);
                    }
                    ok = 0;
                }
                if (worst > worst_all) {
                    worst_all = worst;
                }
                if (g_verbose && j == 4) {
                    printf("        %s %-26s draws %u triangles %u, worst %.6f\n", k_kname[kind], D->name, b.nd, b.nt,
                           worst);
                }
            }
            bad += !ok;
            /* the per-kind facts */
            if (kind == SA1_CHAR_SONIC && D->type == SA1_OBJTYPE_RING) {
                const sa1_render *r0 = NULL, *r1 = NULL;
                tris t0, t1;
                memset(&t0, 0, sizeof t0);
                memset(&t1, 0, sizeof t1);
                sa1_preview_render(ctx, cp[di], 0.0f, &r0);
                tris_of(r0, zero, 0, &t0);
                sa1_preview_render(ctx, cp[di], 1.0f, &r1);
                tris_of(r1, zero, 0, &t1);
                if (t0.nt && t1.nt) {
                    uint32_t fv = far_vertex(t1.p, 3u * t1.nt, 0.0, 0.0);
                    double da = wrap180(azimuth(t1.p[3u * fv], t1.p[3u * fv + 2u]) -
                                        azimuth(t0.p[3u * fv], t0.p[3u * fv + 2u]));
                    CHECK(fabs(fabs(da) - 4.0) < 0.01 && t1.nt == 96 && t1.nd == 1, "Sonic RING: 1 draw / %u triangles "
                          "(%d:%d), turns %.3f degrees a pair (4.00)", t1.nt, t1.tex[0][0], t1.tex[0][1], fabs(da));
                }
                tris_free(&t0);
                tris_free(&t1);
            }
            if (kind == SA1_CHAR_SONIC && D->type == SA1_OBJTYPE_ITEMBOX && D->p[0] == 3.0f && D->state == 1) {
                const sa1_render *r0 = NULL, *r1 = NULL;
                tris t0, t1;
                memset(&t0, 0, sizeof t0);
                memset(&t1, 0, sizeof t1);
                sa1_preview_render(ctx, cp[di], 0.0f, &r0);
                tris_of(r0, zero, 0, &t0);
                sa1_preview_render(ctx, cp[di], 1.0f, &r1);
                tris_of(r1, zero, 0, &t1);
                if (t0.nt == 116 && t1.nt == 116) {
                    /* the item panel: the last draw (2 triangles, 6 vertices) */
                    uint32_t q = 114u * 9u + 3u * far_vertex(&t1.p[114u * 9u], 6u, 0.0, 0.0);
                    double da = wrap180(azimuth(t1.p[q], t1.p[q + 2]) - azimuth(t0.p[q], t0.p[q + 2]));
                    CHECK(fabs(fabs(da) - 6.0) < 0.01 && t1.nd == 8, "Sonic ITEMBOX: 8 draws / %u triangles, the panel "
                          "turns %.3f degrees a pair (6.00)", t1.nt, fabs(da));
                }
                tris_free(&t0);
                tris_free(&t1);
            }
            if (D->type == SA1_OBJTYPE_ITEMBOX && D->p[0] == 6.0f) {
                const sa1_render *r1 = NULL;
                sa1_preview_render(ctx, cp[di], 1.0f, &r1);
                if (r1 != NULL && r1->num_draws > 0) {
                    const sa1_draw *ld = &r1->draws[r1->num_draws - 1u];
                    CHECK(ld->archive == k_life[kind][0] && ld->texture == k_life[kind][1], "%s: the Extra Life panel "
                          "is %d:%d (objects2_view.md M3: %d:%d)", k_kname[kind], ld->archive, ld->texture,
                          k_life[kind][0], k_life[kind][1]);
                }
            }
            if (kind == SA1_CHAR_SONIC && (D->bits == 0x05u || D->bits == 0x7Eu || D->state == 3u)) {
                const sa1_render *r1 = NULL;
                tris t1;
                memset(&t1, 0, sizeof t1);
                sa1_preview_render(ctx, cp[di], 1.0f, &r1);
                tris_of(r1, zero, 0, &t1);
                if (D->state == 3u) {
                    CHECK(t1.nd == 4 && t1.nt == 54, "Sonic broken ITEMBOX: %u draws / %u triangles (4 / 54)", t1.nd, t1.nt);
                } else {
                    uint32_t want = D->bits == 0x05u ? 6u : 2u;
                    CHECK(t1.nt == 96u * want, "Sonic %s: %u rings (%u%s)", D->name, t1.nt / 96u, want,
                          D->bits == 0x7Eu ? ": the game's Sint8 quirk, set_test (4)" : "");
                }
                tris_free(&t1);
            }
            sa1_set_remove(ctx, rid);
            tick(ctx, id);
            tick(ctx, id);
            ptick(ctx);
            ptick(ctx);
        }
        CHECK(bad == 0 && compared == 4u * PD_N, "%s: %u descriptions x 4 pairs equal the SET path (draws, triangles, "
              "textures, flags, positions within 0.01 u; worst %.5f u); %u copies alive in one showcase", k_kname[kind],
              (unsigned)PD_N, worst_all, (unsigned)PD_N);
        drain(ctx);
        tris_free(&a);
        tris_free(&b);
        sa1_character_destroy(ctx, id);
    }
    /* review R.1: the grid's corner slots (copies take the free slot nearest the origin: the corners come last) */
    {
        uint32_t ids[256], n = 0, k, i0 = 0, i255 = 0;
        tris t0, t1;
        double worst = 0.0, worst_box = 0.0;
        char why[160];
        int okr = 0, okb = 0;
        memset(&t0, 0, sizeof t0);
        memset(&t1, 0, sizeof t1);
        sa1_preview_clear(ctx);
        for (k = 0; k < 256; k++) {
            if (padd(ctx, SA1_CHAR_SONIC, SA1_OBJTYPE_RING, zero, 1, 0, NULL, &ids[n]) == SA1_OK) {
                if (sa1pv_test_slot(ctx, ids[n]) == 0) {
                    i0 = ids[n];
                }
                if (sa1pv_test_slot(ctx, ids[n]) == 255) {
                    i255 = ids[n];
                }
                n++;
            }
        }
        for (k = 0; k < 3; k++) {
            ptick(ctx);
        }
        if (n == 256 && i0 && i255 && sa1pv_test_slot(ctx, ids[0]) != 0) {
            const sa1_render *r = NULL;
            sa1_preview_render(ctx, i0, 1.0f, &r);
            tris_of(r, zero, 0, &t0);
            sa1_preview_render(ctx, i255, 1.0f, &r);
            tris_of(r, zero, 0, &t1);
            okr = tris_same(&t0, &t1, 0.002, &worst, why, sizeof why) && t0.nt == 96;
            printf("        the first copy took slot %d (a middle one), the 256th slot %d\n", sa1pv_test_slot(ctx, ids[0]),
                   sa1pv_test_slot(ctx, ids[255]));
        }
        sa1_preview_clear(ctx);
        {
            const float three[3] = { 3, 0, 0 };
            uint32_t b0 = 0, b1 = 0;
            for (k = 0; k < 252; k++) {
                padd(ctx, SA1_CHAR_SONIC, SA1_OBJTYPE_RING, zero, 1, 0, NULL, &ids[k]);
            }
            padd(ctx, SA1_CHAR_SONIC, SA1_OBJTYPE_ITEMBOX, three, 1, 0, NULL, &b0);     /* slot 0: the first corner */
            padd(ctx, SA1_CHAR_SONIC, SA1_OBJTYPE_RING, zero, 1, 0, NULL, &ids[252]);   /* 15 */
            padd(ctx, SA1_CHAR_SONIC, SA1_OBJTYPE_RING, zero, 1, 0, NULL, &ids[253]);   /* 240 */
            padd(ctx, SA1_CHAR_SONIC, SA1_OBJTYPE_ITEMBOX, three, 1, 0, NULL, &b1);     /* 255 */
            for (k = 0; k < 30; k++) {
                ptick(ctx);
            }
            if (sa1pv_test_slot(ctx, b0) == 0 && sa1pv_test_slot(ctx, b1) == 255) {
                const sa1_render *r = NULL;
                sa1_preview_render(ctx, b0, 1.0f, &r);
                tris_of(r, zero, 0, &t0);
                sa1_preview_render(ctx, b1, 1.0f, &r);
                tris_of(r, zero, 0, &t1);
                okb = tris_same(&t0, &t1, 0.002, &worst_box, why, sizeof why) && t0.nt == 116;
            }
        }
        CHECK(okr && okb, "review R.1: RING copies at slots 0 and 255 (the grid's corners, 15,360 u from the origin on each "
              "axis) equal in object space within %.6f u, ITEMBOX copies within %.6f u (0.002)", worst, worst_box);
        sa1_preview_clear(ctx);
        tris_free(&t0);
        tris_free(&t1);
    }
}

/* ---- (2) rotation ------------------------------------------------------------------------------------------------------ */
static void rot_apply(const float m[9], const double *p, double *o, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        const double *v = &p[3u * i];
        double *w = &o[3u * i];
        w[0] = m[0] * v[0] + m[1] * v[1] + m[2] * v[2];
        w[1] = m[3] * v[0] + m[4] * v[1] + m[5] * v[2];
        w[2] = m[6] * v[0] + m[7] * v[1] + m[8] * v[2];
    }
}

static void t_rotation(void)
{
    sa1_context *ctx = g_ctx;
    const float rec_pos[3] = { 60.0f, 20.0f, 0.0f }, zero[3] = { 0, 0, 0 }, six[3] = { 6, 0, 0 };
    sa1_character_id id;
    uint32_t k, p;
    printf("rotation (2): a RING copy under the host's outer yaw, an air box under a full outer rotation, an O_GRING copy "
           "with an inner rotation, against the records (1e-4 u; angles whole multiples of 0x800 BAMS)\n");
    floor_of(ctx, 10.0f);
    sa1_preview_clear(ctx);
    id = mk(ctx, SA1_CHAR_SONIC, 0.0f, 1.0f, 0.0f);
    if (!id) {
        CHECK(0, "no Sonic world");
        return;
    }
    for (k = 0; k < 4; k++) {
        tick(ctx, id);
    }
    for (k = 0; k < 4; k++) {
        static const double yaws[4] = { 90.0, 45.0, 202.5, -33.75 };
        static const double zxy[4][3] = { { 0, 0, 90 }, { 45, 22.5, 90 }, { -67.5, 11.25, 202.5 }, { 0, 0, 0 } };
        float rot[9];
        uint32_t type = k < 2 ? SA1_OBJTYPE_RING : SA1_OBJTYPE_ITEMBOX_AIR, rid, cid = 0;
        const float *prm = type == SA1_OBJTYPE_RING ? zero : six;
        tris a, b;
        double worst = 0.0, *q;
        uint32_t i, ok = 1;
        memset(&a, 0, sizeof a);
        memset(&b, 0, sizeof b);
        if (type == SA1_OBJTYPE_RING) {
            rot_axis(1, yaws[2 * k], rot);
        } else {
            rot_zxy(zxy[k - 1][0], zxy[k - 1][1], zxy[k - 1][2], rot);
        }
        rid = rec_add(ctx, type, rec_pos, prm, rot, 1, 0);
        padd(ctx, SA1_CHAR_SONIC, type, prm, 1, 0, NULL, &cid);
        for (p = 1; p <= 30; p++) {
            const sa1_render *rr = NULL, *rc = NULL;
            tick(ctx, id);
            ptick(ctx);
            if (p != 3 && p != 30) {
                continue;
            }
            sa1_character_render(ctx, id, 1.0f, &rr);
            sa1_preview_render(ctx, cid, 1.0f, &rc);
            tris_of(rr, rec_pos, 1, &a);
            tris_of(rc, zero, 0, &b);
            if (a.nt != b.nt || a.nt == 0) {
                ok = 0;
                continue;
            }
            q = (double *)malloc(9u * sizeof(double) * b.nt);
            rot_apply(rot, b.p, q, 3u * b.nt);
            for (i = 0; i < 9u * a.nt; i++) {
                double d = fabs(a.p[i] - q[i]);
                worst = d > worst ? d : worst;
            }
            free(q);
        }
        if (type == SA1_OBJTYPE_RING) {
            CHECK(ok && worst < 1e-4, "RING with yaw %.2f: the record's draws = the copy's under the outer yaw (worst %.6f "
                  "u, %u triangles)", yaws[2 * k], worst, a.nt);
        } else {
            CHECK(ok && worst < 1e-4, "ITEMBOX_AIR with Z %.2f X %.2f Y %.2f: the record's draws = the copy's under the "
                  "full outer rotation (worst %.6f u, %u triangles)", zxy[k - 1][0], zxy[k - 1][1], zxy[k - 1][2], worst,
                  a.nt);
        }
        sa1_set_remove(ctx, rid);
        sa1_preview_remove(ctx, cid);
        tick(ctx, id);
        tick(ctx, id);
        tris_free(&a);
        tris_free(&b);
    }
    /* O_GRING: the rotation inside the description */
    {
        static const float lay[2][3] = { { 7, 10, 0 }, { 6, 20, 1 } };
        for (k = 0; k < 2; k++) {
            float rot[9];
            uint32_t rid, cid = 0, ok = 1;
            tris a, b;
            double worst = 0.0, w;
            char why[160] = "";
            memset(&a, 0, sizeof a);
            memset(&b, 0, sizeof b);
            rot_zxy(k ? 22.5 : 0.0, k ? -45.0 : 30.0, k ? 101.25 : 33.75, rot);
            rid = rec_add(ctx, SA1_OBJTYPE_O_GRING, rec_pos, lay[k], rot, 1, 0);
            padd(ctx, SA1_CHAR_SONIC, SA1_OBJTYPE_O_GRING, lay[k], 1, 0, rot, &cid);
            for (p = 1; p <= 23; p++) {
                const sa1_render *rr = NULL, *rc = NULL;
                tick(ctx, id);
                ptick(ctx);
                if (p != 3 && p != 23) {
                    continue;
                }
                sa1_character_render(ctx, id, 1.0f, &rr);
                sa1_preview_render(ctx, cid, 1.0f, &rc);
                tris_of(rr, rec_pos, 1, &a);
                tris_of(rc, zero, 0, &b);
                ok &= (uint32_t)tris_same(&a, &b, 0.01, &w, why, sizeof why);
                worst = w > worst ? w : worst;
            }
            CHECK(ok && a.nt > 0, "O_GRING %s (%g, %g, %g) with an inner rotation: the copy equals the record (%u "
                  "triangles, worst %.5f u%s%s)", k ? "circle" : "line", lay[k][0], lay[k][1], lay[k][2], a.nt, worst,
                  ok ? "" : ": ", ok ? "" : why);
            sa1_set_remove(ctx, rid);
            sa1_preview_remove(ctx, cid);
            tick(ctx, id);
            tick(ctx, id);
            tris_free(&a);
            tris_free(&b);
        }
    }
    drain(ctx);
    sa1_character_destroy(ctx, id);
    sa1_preview_clear(ctx);
}

/* ---- (3) ring places --------------------------------------------------------------------------------------------------- */
static uint32_t rd32w(const sa1g_world *w, uint32_t g)
{
    uint32_t v = 0;
    if (w == NULL || g == 0 || ((g >> 24) & 31u) != SA1G_SEG_ARENA || sa1g_world_rd32(w, g & ~3u, &v) != 0) {
        return 0;
    }
    return v;
}
static float rdfw(const sa1g_world *w, uint32_t g)
{
    uint32_t v = rd32w(w, g);
    float f;
    memcpy(&f, &v, 4);
    return f;
}

/* the ring centres of the 96-triangle groups of a render (object space) */
static uint32_t ring_centres(const tris *t, double *c, uint32_t cap)
{
    uint32_t n = 0, r, i;
    for (r = 0; r + 96u <= t->nt && n < cap; r += 96u, n++) {
        double s[3] = { 0, 0, 0 };
        for (i = 0; i < 96u * 3u; i++) {
            s[0] += t->p[9u * r + 3u * i];
            s[1] += t->p[9u * r + 3u * i + 1u];
            s[2] += t->p[9u * r + 3u * i + 2u];
        }
        for (i = 0; i < 3; i++) {
            c[3u * n + i] = s[i] / (96.0 * 3.0);
        }
    }
    return n;
}

static void t_places(void)
{
    sa1_context *ctx = g_ctx;
    static const float lays[15][3] = { { 0, 2, 0 }, { 0, 10, 0 }, { 0, 100, 0 }, { 7, 2, 0 }, { 7, 10, 0 }, { 7, 100, 0 },
                                       { 1, 4, 1 }, { 1, 20, 1 }, { 1, 200, 1 }, { 4, 4, 1 }, { 4, 20, 1 }, { 4, 200, 1 },
                                       { 7, 4, 1 }, { 7, 20, 1 }, { 7, 200, 1 } };
    const float rec_pos[3] = { 300.0f, 100.0f, 0.0f }, zero[3] = { 0, 0, 0 };
    sa1_character_id id;
    sa1g_world *w;
    uint32_t L, R, k, bad_world = 0, cases = 0, bad_centre = 0, bad_ring = 0, rings_checked = 0, lag = 99;
    double worst_world = 0.0, worst_centre = 0.0, worst_ring = 0.0;
    uint32_t gid[60], ring_id = 0;
    float rots[4][9];
    printf("ring places (3): lines (n 0, 7; spacing 2, 10, 100) and circles (n 1, 4, 7; radius 4, 20, 200) at four rotations "
           "against the rings' twp->pos in a character world (1e-4), the O_GRING copies' ring centres and a RING copy drawn "
           "at every place\n");
    floor_of(ctx, 10.0f);
    sa1_preview_clear(ctx);
    memset(rots[0], 0, sizeof rots[0]);
    rot_axis(1, 37.0, rots[1]);
    rot_axis(1, 200.0, rots[2]);
    rot_zxy(20.0, 30.0, 0.0, rots[3]);
    id = mk(ctx, SA1_CHAR_SONIC, 0.0f, 1.0f, 0.0f);
    w = sa1_character_world(ctx, id);
    for (k = 0; k < 4; k++) {
        tick(ctx, id);
    }
    padd(ctx, SA1_CHAR_SONIC, SA1_OBJTYPE_RING, zero, 1, 0, NULL, &ring_id);
    for (L = 0; L < 15; L++) {
        for (R = 0; R < 4; R++) {
            float places[48];
            uint32_t bits[16], n = 0, rid, slot, tp, ct, got = 0, i;
            sa1_status st;
            double cpos[16][3];
            cases++;
            st = sa1_preview_ring_places(ctx, SA1_CHAR_SONIC, lays[L], R ? rots[R] : NULL, places, bits, 16, &n);
            rid = rec_add(ctx, SA1_OBJTYPE_O_GRING, rec_pos, lays[L], R ? rots[R] : NULL, 1, 0);
            tick(ctx, id);
            tick(ctx, id);
            slot = (uint32_t)sa1w_set_world_slot(w, rid);
            tp = rd32w(w, A_COND + 0x10u * slot + 4u);
            for (ct = rd32w(w, tp + 0x0Cu); ct != 0 && got < 16u; ct = rd32w(w, ct)) {
                uint32_t twp = rd32w(w, ct + 0x20u);
                for (i = 0; i < 3; i++) {
                    cpos[got][i] = (double)rdfw(w, twp + 0x20u + 4u * i) - (double)rec_pos[i];
                }
                got++;
            }
            if (st != SA1_OK || got != n || n == 0) {
                bad_world++;
                printf("        layout (%g, %g, %g) rotation %u: places %s, %u places, %u rings in the world\n", lays[L][0],
                       lays[L][1], lays[L][2], R, sa1_status_string(st), n, got);
            } else {
                for (i = 0; i < n; i++) {
                    double best = 1e30;
                    uint32_t c;
                    for (c = 0; c < got; c++) {
                        double d = fabs(cpos[c][0] - places[3 * i]) + fabs(cpos[c][1] - places[3 * i + 1]) +
                                   fabs(cpos[c][2] - places[3 * i + 2]);
                        best = d < best ? d : best;
                    }
                    worst_world = best > worst_world ? best : worst_world;
                    if (best > 1e-4) {
                        bad_world++;
                        break;
                    }
                }
            }
            sa1_set_remove(ctx, rid);
            tick(ctx, id);
            gid[L * 4u + R] = 0;
            padd(ctx, SA1_CHAR_SONIC, SA1_OBJTYPE_O_GRING, lays[L], 1, 0, R ? rots[R] : NULL, &gid[L * 4u + R]);
        }
    }
    CHECK(bad_world == 0 && cases == 60, "%u layouts x rotations: sa1_preview_ring_places = the rings' twp->pos of the same "
          "record in a character world minus its position (worst %.6f u, 1e-4)", cases, worst_world);
    /* the O_GRING copies' own ring draws and a RING copy at every place (the same age: the copies were made 0-60 pairs
       apart, every ring spins 4 degrees a pair, so the RING copy is compared after turning it to each ring's own phase) */
    for (k = 0; k < 3; k++) {
        ptick(ctx);
    }
    {
        const sa1_render *r = NULL;
        tris ring, grp;
        double m0[3] = { 0, 0, 0 };
        uint32_t i, g;
        memset(&ring, 0, sizeof ring);
        memset(&grp, 0, sizeof grp);
        sa1_preview_render(ctx, ring_id, 1.0f, &r);
        tris_of(r, zero, 0, &ring);
        ring_centres(&ring, m0, 1);
        for (g = 0; g < 60; g++) {
            float places[48];
            uint32_t bits[16], n = 0, nc;
            double cen[16][3];
            uint32_t L2 = g / 4u, R2 = g % 4u;
            if (!gid[g]) {
                bad_centre++;
                continue;
            }
            sa1_preview_ring_places(ctx, SA1_CHAR_SONIC, lays[L2], R2 ? rots[R2] : NULL, places, bits, 16, &n);
            sa1_preview_render(ctx, gid[g], 1.0f, &r);
            tris_of(r, zero, 0, &grp);
            nc = ring_centres(&grp, &cen[0][0], 16);
            if (nc != n) {
                bad_centre++;
                printf("        layout (%g, %g, %g) rotation %u: %u ring draws, %u places\n", lays[L2][0], lays[L2][1],
                       lays[L2][2], R2, nc, n);
                continue;
            }
            for (i = 0; i < n; i++) {
                double best = 1e30;
                uint32_t c, bc = 0;
                for (c = 0; c < nc; c++) {
                    double d = fmax(fabs(cen[c][0] - m0[0] - places[3 * i]), fmax(fabs(cen[c][1] - m0[1] - places[3 * i + 1]),
                                    fabs(cen[c][2] - m0[2] - places[3 * i + 2])));
                    if (d < best) {
                        best = d;
                        bc = c;
                    }
                }
                worst_centre = best > worst_centre ? best : worst_centre;
                if (best > 2e-3) {
                    bad_centre++;
                }
                /* the RING copy drawn at the place, turned about its vertical axis to the group ring's own phase (the
                   azimuth of its first vertex): then every vertex must agree */
                if (ring.nt == 96) {
                    const double *gv = &grp.p[9u * 96u * bc];
                    uint32_t fv = far_vertex(ring.p, 96u * 3u, 0.0, 0.0);
                    double ga = atan2(gv[3u * fv + 2u] - places[3 * i + 2], gv[3u * fv] - places[3 * i]);
                    double ra = atan2(ring.p[3u * fv + 2u], ring.p[3u * fv]), dth = ga - ra, cs = cos(dth), sn = sin(dth);
                    double worst = 0.0;
                    uint32_t v;
                    for (v = 0; v < 96u * 3u; v++) {
                        const double *rv = &ring.p[3u * v];
                        /* atan2(z, x) grows with the turn x' = c x - s z, z' = s x + c z */
                        double x = cs * rv[0] - sn * rv[2], z = sn * rv[0] + cs * rv[2];
                        double d = fmax(fabs(x + places[3 * i] - gv[3u * v]), fmax(fabs(rv[1] + places[3 * i + 1] -
                                        gv[3u * v + 1]), fabs(z + places[3 * i + 2] - gv[3u * v + 2])));
                        worst = d > worst ? d : worst;
                    }
                    rings_checked++;
                    worst_ring = worst > worst_ring ? worst : worst_ring;
                    if (worst > 2e-3) {
                        bad_ring++;
                    }
                    if (lag == 99 && g == 0) {
                        double steps = wrap180((ga - ra) * 180.0 / 3.14159265358979323846) / (364.0 * 360.0 / 65536.0);
                        lag = (uint32_t)(int32_t)floor(fabs(steps) + 0.5);
                    }
                }
            }
        }
        CHECK(bad_centre == 0, "the O_GRING copies' ring centres (minus a RING copy's own vertex centre) = the places "
              "(60 copies, worst %.6f u on an axis, 0.002: the game places a group's rings in float at the copy's slot)",
              worst_centre);
        CHECK(bad_ring == 0 && rings_checked > 0, "a RING copy drawn at every place = the O_GRING copy's ring there (%u rings, "
              "worst %.6f u on an axis after the spin phase, 0.002; the first group's rings lag the RING copy by %u step(s) of 2.00 "
              "degrees)", rings_checked, worst_ring, lag);
        tris_free(&ring);
        tris_free(&grp);
    }
    drain(ctx);
    sa1_character_destroy(ctx, id);
    sa1_preview_clear(ctx);
}

/* ---- (4) / (5) isolation ------------------------------------------------------------------------------------------------- */
static uint32_t arena_crc_masked(const sa1g_world *w)
{
    uint32_t crc = 0, i, v;
    if (w == NULL || w->ram == NULL) {
        return 0u;
    }
    for (i = 0; i < SA1G_ARENA_SIZE; i += 4u) {
        memcpy(&v, w->ram + i, 4);
        if (v != 0 && ((v >> 24) & 31u) == SA1G_SEG_EXT) {
            v = 0;
        }
        crc = tu_crc32(crc, (const uint8_t *)&v, 4);
    }
    return crc;
}

typedef struct iso_res {
    uint32_t state_crc, ev_crc, set_crc, arena, ev_n, set_n, faults, foreign_set, window_max, far_draws;
    uint32_t copy_crc[24];      /* per 100 pairs: every copy's render */
} iso_res;

/* a Sonic world with 20 records for `pairs` pairs (records added / updated / forced / removed on the way); copies: every
 * description of (1) for the six kinds made first and ticked with the world (0: none); world 0: copies alone */
static void iso_run(sa1_context *ctx, int world, int copies, uint32_t pairs, iso_res *o)
{
    sa1_character_id id = 0;
    sa1g_world *w = NULL;
    uint32_t recs[24], nrec = 0, cps[6 * PD_N], ncp = 0, p, k, ki;
    memset(o, 0, sizeof *o);
    sa1_preview_clear(ctx);
    drain(ctx);
    if (copies) {
        for (ki = 0; ki < 6; ki++) {
            for (k = 0; k < PD_N; k++) {
                const pdesc *D = &k_desc[k];
                if (padd(ctx, k_kinds[ki], D->type, D->p, D->state, D->bits, NULL, &cps[ncp]) == SA1_OK) {
                    ncp++;
                }
            }
        }
    }
    if (world) {
        id = mk(ctx, SA1_CHAR_SONIC, 0.0f, 1.0f, 0.0f);
        w = sa1_character_world(ctx, id);
        for (k = 0; k < 20; k++) {
            float pos[3] = { 30.0f + 25.0f * (float)k, k % 3 ? 6.0f : 0.0f, (float)((k * 37) % 41) - 20.0f };
            float prm[3] = { 0, 0, 0 };
            uint32_t type = k % 5 == 0 ? SA1_OBJTYPE_ITEMBOX : k % 5 == 1 ? SA1_OBJTYPE_O_GRING :
                            k % 5 == 2 ? SA1_OBJTYPE_ITEMBOX_AIR : SA1_OBJTYPE_RING;
            if (type == SA1_OBJTYPE_O_GRING) {
                prm[0] = 5;
                prm[1] = 12;
                prm[2] = (float)(k % 2);
            } else if (type != SA1_OBJTYPE_RING) {
                prm[0] = (float)(k % 9);
            }
            recs[nrec++] = rec_add(ctx, type, pos, prm, NULL, 1, 0);
        }
    }
    for (p = 0; p < pairs; p++) {
        if (world) {
            sa1_event ev[64];
            sa1_set_event se[64];
            sa1_state s;
            sa1set_world_info wi;
            uint32_t n, i;
            if (p == 200) {
                float pos[3] = { 140.0f, 0.0f, 30.0f }, prm[3] = { 6, 0, 0 };
                recs[nrec++] = rec_add(ctx, SA1_OBJTYPE_ITEMBOX, pos, prm, NULL, 1, 0);
            }
            if (p == 400) {
                sa1_set_record r;
                memset(&r, 0, sizeof r);
                r.struct_size = sizeof r;
                r.type = SA1_OBJTYPE_RING;
                r.position[0] = 90.0f;
                r.position[1] = 6.0f;
                r.position[2] = -10.0f;
                sa1_set_update(ctx, recs[3], &r);
                sa1_set_state(ctx, recs[6], SA1_SET_STATE_PRESENT, 0x03u);
            }
            if (p == 600) {
                sa1_set_remove(ctx, recs[4]);
                sa1_set_state(ctx, recs[0], SA1_SET_STATE_PRESENT | SA1_SET_STATE_BROKEN, 0);
            }
            tick_in(ctx, id, p % 300 < 120 ? 0.0f : 0.4f, p % 300 < 120 ? 1.0f : 0.0f, p % 97 == 50 ? SA1_BUTTON_JUMP : 0u);
            memset(&s, 0, sizeof s);
            s.struct_size = sizeof s;
            sa1_character_state(ctx, id, &s);
            o->state_crc = tu_crc32(o->state_crc, (const uint8_t *)s.position, 12);
            o->state_crc = tu_crc32(o->state_crc, (const uint8_t *)&s.action, 4);
            o->state_crc = tu_crc32(o->state_crc, (const uint8_t *)&s.frame, 4);
            do {
                n = 0;
                sa1_poll_events(ctx, ev, 64, &n);
                for (i = 0; i < n; i++) {
                    if (ev[i].type == SA1_EVENT_FAULT) {
                        o->faults++;
                    }
                    o->ev_crc = tu_crc32(o->ev_crc, (const uint8_t *)&ev[i].type, 4);
                    o->ev_crc = tu_crc32(o->ev_crc, (const uint8_t *)&ev[i].id, 4);
                    o->ev_crc = tu_crc32(o->ev_crc, (const uint8_t *)&ev[i].arg, 4);
                    o->ev_crc = tu_crc32(o->ev_crc, (const uint8_t *)&ev[i].step, 4);
                    o->ev_crc = tu_crc32(o->ev_crc, (const uint8_t *)ev[i].position, 12);
                    o->ev_n++;
                }
            } while (n == 64);
            do {
                n = 0;
                sa1_poll_set_events(ctx, se, 64, &n);
                for (i = 0; i < n; i++) {
                    uint32_t j, idx = 0xFFFFFFFFu;
                    for (j = 0; j < nrec; j++) {
                        if (recs[j] == se[i].record) {
                            idx = j;
                        }
                    }
                    if (idx == 0xFFFFFFFFu) {
                        o->foreign_set++;
                    }
                    o->set_crc = tu_crc32(o->set_crc, (const uint8_t *)&idx, 4);
                    o->set_crc = tu_crc32(o->set_crc, (const uint8_t *)&se[i].type, 4);
                    o->set_crc = tu_crc32(o->set_crc, (const uint8_t *)&se[i].condition, 4);
                    o->set_crc = tu_crc32(o->set_crc, (const uint8_t *)&se[i].step, 4);
                    o->set_crc = tu_crc32(o->set_crc, (const uint8_t *)se[i].position, 12);
                    o->set_n++;
                }
            } while (n == 64);
            sa1w_set_world_info(w, &wi);
            o->window_max = wi.window_records > o->window_max ? wi.window_records : o->window_max;
            if (p % 50 == 0) {
                /* the world's render: nothing far from the character (the showcases' copies lie 1,024 u and more from
                   the origin; the scene stays within 600 u of it) */
                const sa1_render *r = NULL;
                if (sa1_character_render(ctx, id, 1.0f, &r) == SA1_OK && r != NULL) {
                    for (i = 0; i < 3u * r->num_triangles; i++) {
                        const float *q = &r->positions[3u * i];
                        if (fabsf(q[0] - s.position[0]) > 900.0f || fabsf(q[2] - s.position[2]) > 900.0f) {
                            o->far_draws++;
                        }
                    }
                }
            }
        }
        if (copies) {
            ptick(ctx);
            if ((p + 1u) % 100u == 0 && (p + 1u) / 100u <= 24u) {
                uint32_t crc = 0, i;
                for (i = 0; i < ncp; i++) {
                    const sa1_render *r = NULL;
                    sa1_preview_render(ctx, cps[i], 1.0f, &r);
                    crc = render_crc(r, crc);
                }
                o->copy_crc[(p + 1u) / 100u - 1u] = crc;
            }
        }
    }
    if (world) {
        o->arena = arena_crc_masked(w);
        for (k = 0; k < nrec; k++) {
            sa1_set_remove(ctx, recs[k]);
        }
        sa1_character_destroy(ctx, id);
    }
    sa1_preview_clear(ctx);
    drain(ctx);
}

static void t_isolation(void)
{
    sa1_context *ctx = g_ctx;
    iso_res a, b, c;
    int pf = g_preview_faults;
    uint32_t k, same_copies = 1;
    printf("isolation (4) / set isolation (5): 1,000 pairs of a Sonic world with 20 records, without and with every "
           "description of (1) for the six kinds; and the copies alone\n");
    floor_of(ctx, 3000.0f);
    iso_run(ctx, 1, 0, 1000, &a);
    iso_run(ctx, 1, 1, 1000, &b);
    iso_run(ctx, 0, 1, 1000, &c);
    CHECK(g_preview_faults == pf && a.faults == 0 && b.faults == 0, "0 faults (showcases: %d, worlds %u / %u)",
          g_preview_faults - pf, a.faults, b.faults);
    CHECK(a.state_crc == b.state_crc && a.arena == b.arena, "the Sonic world's per-pair state and arena identical without "
          "and with 132 copies (state %08X / %08X, arena %08X / %08X)", a.state_crc, b.state_crc, a.arena, b.arena);
    CHECK(a.ev_crc == b.ev_crc && a.ev_n == b.ev_n, "sa1_poll_events identical (%u / %u events): nothing of a showcase - no "
          "SOUND, VOICE, CAPACITY or FAULT - reaches it (the audio cues are these events)", a.ev_n, b.ev_n);
    CHECK(a.set_crc == b.set_crc && a.set_n == b.set_n && b.foreign_set == 0, "sa1_poll_set_events identical (%u / %u), "
          "none for a record the test did not add (%u)", a.set_n, b.set_n, b.foreign_set);
    for (k = 0; k < 10; k++) {
        same_copies &= b.copy_crc[k] == c.copy_crc[k] && b.copy_crc[k] != 0;
    }
    CHECK(same_copies, "(5) the copies' draws with the world and its records added, updated, forced and removed equal a "
          "run of the copies alone (10 checkpoints, every copy's render)");
    CHECK(b.window_max <= 21 && b.far_draws == 0, "(5) no copy in the character world's window (at most %u records "
          "selected, 21 exist) or capture (%u vertices far from the character)", b.window_max, b.far_draws);
    floor_of(ctx, 10.0f);
}

/* ---- (6) lifetime and memory --------------------------------------------------------------------------------------- */
static void t_lifetime(void)
{
    sa1_context *ctx;
    sa1pv_test_info t0, t1;
    const float zero[3] = { 0, 0, 0 };
    uint32_t keep = 0, k, n, old_id = 0, idt = 0;
    sa1_status st;
    int64_t live0 = g_live;
    printf("lifetime and memory (6): a context of its own with a counting allocator (%s)\n",
           ga_enabled() ? "SA1_GUARD_ALLOC on" : "SA1_GUARD_ALLOC off: run with --guard-alloc for the use-after-free part");
    ctx = mkctx(1, 1);
    if (ctx == NULL) {
        CHECK(0, "no context");
        return;
    }
    floor_of(ctx, 10.0f);
    padd(ctx, SA1_CHAR_SONIC, SA1_OBJTYPE_RING, zero, 1, 0, NULL, &keep);
    sa1_preview_tick(ctx, PAIR);
    sa1_preview_tick(ctx, PAIR);
    sa1pv_test_showcase(ctx, SA1_CHAR_SONIC, &t0, NULL);
    for (k = 0; k < 200; k++) {
        const pdesc *D = &k_desc[k % PD_N];
        uint32_t id = 0;
        padd(ctx, SA1_CHAR_SONIC, D->type, D->p, D->state, D->bits, NULL, &id);
        sa1_preview_tick(ctx, PAIR);
        sa1_preview_remove(ctx, id);
        sa1_preview_tick(ctx, PAIR);
    }
    sa1_preview_tick(ctx, PAIR);
    sa1pv_test_showcase(ctx, SA1_CHAR_SONIC, &t1, NULL);
    CHECK(t1.heap_largest == t0.heap_largest && t1.free_tasks == t0.free_tasks && t1.leaving == 0 && t1.copies == 1,
          "200 descriptions added and removed: the showcase heap's largest block %u -> %u, free tasks %u -> %u, %u slots "
          "leaving", t0.heap_largest, t1.heap_largest, t0.free_tasks, t1.free_tasks, t1.leaving);
    sa1_preview_remove(ctx, keep);
    for (n = 0; n < 200; n++) {
        sa1_preview_tick(ctx, PAIR);
        if (sa1pv_test_showcase(ctx, SA1_CHAR_SONIC, &t1, NULL) != SA1_OK) {
            break;
        }
    }
    CHECK(n + 1u >= SA1_PREVIEW_IDLE_PAIRS && n + 1u <= SA1_PREVIEW_IDLE_PAIRS + 2u, "the showcase freed %u pairs after its "
          "last copy was removed (SA1_PREVIEW_IDLE_PAIRS %u, + the pair that releases the slot)", n + 1u,
          (unsigned)SA1_PREVIEW_IDLE_PAIRS);
    padd(ctx, SA1_CHAR_SONIC, SA1_OBJTYPE_RING, zero, 1, 0, NULL, &keep);
    sa1_preview_tick(ctx, PAIR);
    sa1_preview_clear(ctx);
    {
        const sa1_render *r = NULL;
        CHECK(sa1pv_test_showcase(ctx, SA1_CHAR_SONIC, &t1, NULL) == SA1_ERR_NOT_FOUND &&
              sa1_preview_render(ctx, keep, 1.0f, &r) == SA1_ERR_NOT_FOUND, "sa1_preview_clear frees the showcase and its "
              "copies at once");
    }
    /* a forced fault: Sonic's showcase dies, Tails' and the character worlds keep running */
    {
        uint32_t s1 = 0, s2 = 0, t1id = 0;
        sa1_character_id ch = mk(ctx, SA1_CHAR_SONIC, 0, 1, 0);
        const sa1_render *r = NULL;
        sa1_status a, b, c, d;
        padd(ctx, SA1_CHAR_SONIC, SA1_OBJTYPE_RING, zero, 1, 0, NULL, &s1);
        padd(ctx, SA1_CHAR_SONIC, SA1_OBJTYPE_RING, zero, 1, 0, NULL, &s2);
        padd(ctx, SA1_CHAR_TAILS, SA1_OBJTYPE_RING, zero, 1, 0, NULL, &t1id);
        sa1_preview_tick(ctx, PAIR);
        sa1pv_test_fault(ctx, s2);
        a = sa1_preview_tick(ctx, PAIR);
        printf("        the fault: %s\n", sa1_last_error(ctx));
        b = sa1_preview_render(ctx, s1, 1.0f, &r);
        c = sa1_preview_tick(ctx, PAIR);
        d = sa1_preview_render(ctx, t1id, 1.0f, &r);
        CHECK(a == SA1_ERR_GAME_FAULT && b == SA1_ERR_GAME_FAULT && c == SA1_OK && d == SA1_OK && r != NULL &&
              r->num_triangles == 96 && tick(ctx, ch) == 0, "a forced fault (an invalid exec in a copy): the tick %s, "
              "Sonic's copies %s, Tails' copy %s (%u triangles, still ticking: %s), the character world ticks",
              sa1_status_string(a), sa1_status_string(b), sa1_status_string(d), r ? r->num_triangles : 0,
              sa1_status_string(c));
        st = padd(ctx, SA1_CHAR_SONIC, SA1_OBJTYPE_RING, zero, 1, 0, NULL, &idt);
        CHECK(st == SA1_ERR_GAME_FAULT, "sa1_preview_add for the faulted kind: %s", sa1_status_string(st));
        sa1_preview_clear(ctx);
        st = padd(ctx, SA1_CHAR_SONIC, SA1_OBJTYPE_RING, zero, 1, 0, NULL, &idt);
        sa1_preview_tick(ctx, PAIR);
        sa1_preview_tick(ctx, PAIR);    /* a new ring draws from its second pair (mode 0 draws nothing) */
        CHECK(st == SA1_OK && sa1_preview_render(ctx, idt, 1.0f, &r) == SA1_OK && r->num_triangles == 96,
              "after sa1_preview_clear, add works again (%s)", sa1_status_string(st));
        /* remove-all path: a faulted showcase freed when its last copy goes */
        sa1pv_test_fault(ctx, idt);
        sa1_preview_tick(ctx, PAIR);
        sa1_preview_remove(ctx, idt);
        st = padd(ctx, SA1_CHAR_SONIC, SA1_OBJTYPE_RING, zero, 1, 0, NULL, &idt);
        CHECK(st == SA1_OK, "a faulted showcase is freed with its last copy: add works again (%s)", sa1_status_string(st));
        sa1_character_destroy(ctx, ch);
        sa1_preview_clear(ctx);
    }
    /* unload / reload */
    {
        sa1_load_options lo;
        const sa1_render *r = NULL;
        padd(ctx, SA1_CHAR_SONIC, SA1_OBJTYPE_RING, zero, 1, 0, NULL, &old_id);
        padd(ctx, SA1_CHAR_BIG, SA1_OBJTYPE_ITEMBOX, zero, 1, 0, NULL, &idt);
        sa1_preview_tick(ctx, PAIR);
        sa1_unload_disc(ctx);
        CHECK(sa1pv_test_showcase(ctx, SA1_CHAR_SONIC, &t1, NULL) == SA1_ERR_NOT_FOUND &&
              sa1pv_test_showcase(ctx, SA1_CHAR_BIG, &t1, NULL) == SA1_ERR_NOT_FOUND &&
              sa1_preview_render(ctx, old_id, 1.0f, &r) == SA1_ERR_NOT_FOUND, "sa1_unload_disc frees every showcase "
              "(ids then SA1_ERR_NOT_FOUND)");
        st = padd(ctx, SA1_CHAR_SONIC, SA1_OBJTYPE_RING, zero, 1, 0, NULL, &idt);
        CHECK(st == SA1_ERR_NOT_LOADED, "no disc: sa1_preview_add %s", sa1_status_string(st));
        memset(&lo, 0, sizeof lo);
        lo.struct_size = sizeof lo;
        lo.characters = (1u << SA1_CHAR_SONIC) | (1u << SA1_CHAR_BIG);
        lo.flags = SA1_LOAD_DEFAULT;
        sa1_load_disc(ctx, g_gdi, &lo);
        st = padd(ctx, SA1_CHAR_SONIC, SA1_OBJTYPE_RING, zero, 1, 0, NULL, &idt);
        sa1_preview_tick(ctx, PAIR);
        sa1_preview_tick(ctx, PAIR);
        CHECK(st == SA1_OK && sa1_preview_render(ctx, old_id, 1.0f, &r) == SA1_ERR_NOT_FOUND &&
              sa1_preview_render(ctx, idt, 1.0f, &r) == SA1_OK && r->num_triangles == 96, "after a reload add works (%s) "
              "and the old id stays SA1_ERR_NOT_FOUND", sa1_status_string(st));
    }
    /* shutdown with a live showcase (review R.2) */
    padd(ctx, SA1_CHAR_SONIC, SA1_OBJTYPE_ITEMBOX, zero, 1, 0, NULL, &idt);
    sa1_preview_tick(ctx, PAIR);
    sa1_shutdown(ctx);
    CHECK(g_live == live0, "sa1_shutdown with a live showcase: %lld library blocks left (0)%s", (long long)(g_live - live0),
          ga_enabled() ? "; the guard heap saw no use after free" : "");
}

/* ---- (7) arguments --------------------------------------------------------------------------------------------------- */
static void t_arguments(void)
{
    sa1_context *ctx = g_ctx, *bare;
    const float zero[3] = { 0, 0, 0 }, nine[3] = { 9, 0, 0 }, n16[3] = { 16, 10, 0 }, ringm1[3] = { -1, 0, 0 },
                far_line[3] = { 15, 200, 0 }, three[3] = { 3, 0, 0 };
    float rot[9];
    uint32_t id = 0, k, n = 0, at = 0;
    sa1_status st = SA1_OK, s1, s4, s8, sm, s9;
    const sa1_render *r = NULL;
    sa1_preview_info pi;
    printf("arguments (7)\n");
    sa1_preview_clear(ctx);
    for (k = 0; k < SA1_PREVIEW_MAX + 1u; k++) {
        st = padd(ctx, SA1_CHAR_SONIC, SA1_OBJTYPE_RING, zero, 1, 0, NULL, &id);
        if (st != SA1_OK) {
            break;
        }
    }
    CHECK(k == SA1_PREVIEW_MAX && st == SA1_ERR_LIMIT, "SA1_PREVIEW_MAX + 1 copies of a kind: the %u-th is %s", k + 1u,
          sa1_status_string(st));
    sa1_preview_clear(ctx);
    for (k = 0; k < 300; k++) {
        st = padd(ctx, SA1_CHAR_SONIC, SA1_OBJTYPE_ITEMBOX, three, 1, 0, NULL, &id);
        if (st != SA1_OK) {
            at = k;
            break;
        }
    }
    {
        sa1pv_test_info ti;
        sa1pv_test_showcase(ctx, SA1_CHAR_SONIC, &ti, NULL);
        CHECK(st == SA1_ERR_LIMIT && at * 8u + 8u + SA1PV_CAP_SPARE > ti.cap_capacity, "a record sum beyond the capture: "
              "ITEMBOX copy %u is %s (8 records each, the capture holds %u a pair, %u kept free)", at + 1u,
              sa1_status_string(st), ti.cap_capacity, (unsigned)SA1PV_CAP_SPARE);
    }
    sa1_preview_clear(ctx);
    s1 = padd(ctx, SA1_CHAR_EGGMAN, SA1_OBJTYPE_RING, zero, 1, 0, NULL, &id);
    s4 = padd(ctx, SA1_CHAR_TIKAL, SA1_OBJTYPE_RING, zero, 1, 0, NULL, &id);
    s8 = padd(ctx, SA1_CHAR_SUPER_SONIC, SA1_OBJTYPE_RING, zero, 1, 0, NULL, &id);
    sm = padd(ctx, -1, SA1_OBJTYPE_RING, zero, 1, 0, NULL, &id);
    s9 = padd(ctx, 9, SA1_OBJTYPE_RING, zero, 1, 0, NULL, &id);
    CHECK(s1 == SA1_ERR_UNSUPPORTED && s4 == SA1_ERR_UNSUPPORTED && s8 == SA1_ERR_UNSUPPORTED &&
          sm == SA1_ERR_INVALID_ARGUMENT && s9 == SA1_ERR_INVALID_ARGUMENT, "kinds 1, 4, 8: %s, %s, %s; -1 / 9: %s / %s",
          sa1_status_string(s1), sa1_status_string(s4), sa1_status_string(s8), sa1_status_string(sm),
          sa1_status_string(s9));
    rot_axis(1, 90.0, rot);
    s1 = padd(ctx, SA1_CHAR_SONIC, SA1_OBJTYPE_RING, zero, 1, 0, rot, &id);
    s4 = padd(ctx, SA1_CHAR_SONIC, SA1_OBJTYPE_ITEMBOX, zero, 1, 0, rot, &id);
    s8 = padd(ctx, SA1_CHAR_SONIC, SA1_OBJTYPE_ITEMBOX_AIR, zero, 1, 0, rot, &id);
    CHECK(s1 == SA1_ERR_INVALID_ARGUMENT && s4 == SA1_ERR_INVALID_ARGUMENT && s8 == SA1_ERR_INVALID_ARGUMENT,
          "a rotation on RING / ITEMBOX / ITEMBOX_AIR (the host applies those): %s / %s / %s", sa1_status_string(s1),
          sa1_status_string(s4), sa1_status_string(s8));
    s1 = padd(ctx, SA1_CHAR_SONIC, SA1_OBJTYPE_ITEMBOX, nine, 1, 0, NULL, &id);
    s4 = padd(ctx, SA1_CHAR_SONIC, SA1_OBJTYPE_O_GRING, n16, 1, 0, NULL, &id);
    s8 = padd(ctx, SA1_CHAR_SONIC, SA1_OBJTYPE_RING, ringm1, 1, 0, NULL, &id);
    sm = padd(ctx, SA1_CHAR_SONIC, SA1_OBJTYPE_O_GRING, far_line, 1, 0, NULL, &id);
    s9 = padd(ctx, SA1_CHAR_SONIC, 5u, zero, 1, 0, NULL, &id);
    CHECK(s1 == SA1_ERR_INVALID_ARGUMENT && s4 == SA1_ERR_INVALID_ARGUMENT && s8 == SA1_ERR_UNSUPPORTED &&
          sm == SA1_ERR_LIMIT && s9 == SA1_ERR_INVALID_ARGUMENT, "contents 9: %s; n 16: %s; a RING with params "
          "(-1, 0, 0): %s (review R.20); a line reaching 1,600 u: %s; type 5: %s", sa1_status_string(s1),
          sa1_status_string(s4), sa1_status_string(s8), sa1_status_string(sm), sa1_status_string(s9));
    s1 = padd(ctx, SA1_CHAR_SONIC, SA1_OBJTYPE_RING, zero, 0, 0, NULL, &id);
    s4 = padd(ctx, SA1_CHAR_SONIC, SA1_OBJTYPE_RING, zero, 3, 0, NULL, &id);
    s8 = padd(ctx, SA1_CHAR_SONIC, SA1_OBJTYPE_RING, zero, 1, 1, NULL, &id);
    CHECK(s1 == SA1_ERR_INVALID_ARGUMENT && s4 == SA1_ERR_INVALID_ARGUMENT && s8 == SA1_ERR_INVALID_ARGUMENT,
          "state 0 / BROKEN on a RING / user bits on a RING: %s / %s / %s", sa1_status_string(s1), sa1_status_string(s4),
          sa1_status_string(s8));
    memset(&pi, 0, sizeof pi);
    pi.struct_size = sizeof pi;
    s1 = sa1_preview_remove(ctx, 0x12345u);
    s4 = sa1_preview_render(ctx, 0x12345u, 1.0f, &r);
    s8 = sa1_preview_info_get(ctx, 0x12345u, &pi);
    CHECK(s1 == SA1_ERR_NOT_FOUND && s4 == SA1_ERR_NOT_FOUND && s8 == SA1_ERR_NOT_FOUND, "an unknown id: remove %s, render "
          "%s, info %s", sa1_status_string(s1), sa1_status_string(s4), sa1_status_string(s8));
    {
        float places[48];
        uint32_t bits[16];
        const float line[3] = { 7, 10, 0 };
        s1 = sa1_preview_ring_places(ctx, SA1_CHAR_SONIC, line, NULL, places, bits, 3, &n);
        CHECK(s1 == SA1_ERR_BUFFER_TOO_SMALL && n == 8, "ring places with room for 3: %s, count %u (8)",
              sa1_status_string(s1), n);
        s1 = sa1_preview_ring_places(ctx, SA1_CHAR_TIKAL, line, NULL, places, bits, 16, &n);
        CHECK(s1 == SA1_ERR_UNSUPPORTED, "ring places for Tikal: %s", sa1_status_string(s1));
    }
    {
        sa1_preview_type t;
        uint32_t ty, ok = 1;
        static const uint32_t rot_want[4] = { SA1_PREVIEW_ROT_OUTER, SA1_PREVIEW_ROT_INNER, SA1_PREVIEW_ROT_NONE,
                                              SA1_PREVIEW_ROT_OUTER };
        for (ty = 1; ty <= 4; ty++) {
            memset(&t, 0, sizeof t);
            t.struct_size = sizeof t;
            ok &= sa1_preview_type_get(ctx, ty, &t) == SA1_OK && t.available == 1 && t.rotation == rot_want[ty - 1];
            printf("        type %u: available %u rotation %u settle_steps %u\n", ty, t.available, t.rotation,
                   t.settle_steps);
        }
        memset(&t, 0, sizeof t);
        t.struct_size = sizeof t;
        CHECK(ok && sa1_preview_type_get(ctx, 5, &t) == SA1_ERR_NOT_FOUND, "sa1_preview_type_get: the four types "
              "available with rotations OUTER, INNER, NONE, OUTER; type 5 NOT_FOUND");
    }
    bare = mkctx(0, 0);
    if (bare != NULL) {
        s1 = padd(bare, SA1_CHAR_SONIC, SA1_OBJTYPE_RING, zero, 1, 0, NULL, &id);
        CHECK(s1 == SA1_ERR_NOT_LOADED, "no disc: %s", sa1_status_string(s1));
        sa1_shutdown(bare);
    }
    sa1_preview_clear(ctx);
}

/* ---- (8) interpolation ---------------------------------------------------------------------------------------------- */
static void t_interp(void)
{
    sa1_context *ctx = g_ctx;
    const float zero[3] = { 0, 0, 0 }, six[3] = { 6, 0, 0 };
    uint32_t ring = 0, air = 0, k, g0, g1, i;
    const sa1_render *r = NULL;
    tris a, b, c, d;
    double worst = 0.0;
    int exact0 = 1, exact1 = 1, changed_last = 1;
    printf("interpolation (8)\n");
    memset(&a, 0, sizeof a);
    memset(&b, 0, sizeof b);
    memset(&c, 0, sizeof c);
    memset(&d, 0, sizeof d);
    sa1_preview_clear(ctx);
    padd(ctx, SA1_CHAR_SONIC, SA1_OBJTYPE_RING, zero, 1, 0, NULL, &ring);
    for (k = 0; k < 5; k++) {
        ptick(ctx);
    }
    sa1_preview_render(ctx, ring, 1.0f, &r);
    g0 = r->generation;
    tris_of(r, zero, 0, &a);
    {
        /* alpha 1 is the last pair exactly: the frame itself */
        const float *p1 = r->positions;
        sa1_preview_render(ctx, ring, 5.0f, &r);
        exact1 = r->positions == p1 && r->interpolated == 0;
    }
    sa1_preview_render(ctx, ring, 0.0f, &r);
    tris_of(r, zero, 0, &b);
    sa1_preview_render(ctx, ring, 0.5f, &r);
    tris_of(r, zero, 0, &c);
    for (i = 0; i < 9u * a.nt && a.nt == b.nt && b.nt == c.nt; i++) {
        double mid = 0.5 * (a.p[i] + b.p[i]), dd = fabs(c.p[i] - mid);
        worst = dd > worst ? dd : worst;
    }
    ptick(ctx);
    sa1_preview_render(ctx, ring, 0.0f, &r);
    tris_of(r, zero, 0, &d);
    g1 = r->generation;
    for (i = 0; i < 9u * a.nt && a.nt == d.nt; i++) {
        exact0 &= a.p[i] == d.p[i];
    }
    CHECK(exact1 && a.nt == 96 && worst < 1e-5 && exact0 && g1 == g0 + 1u, "a RING copy: alpha 1 (and above) the last "
          "pair exactly; 0.5 the midpoint of alpha 0 and 1 (worst %.7f u); after one more pair alpha 0 is the former last "
          "pair bit for bit; generation %u -> %u", worst, g0, g1);
    /* a copy whose draws changed between its pairs: a new air box (no draw in its first pair, 7 in its second) */
    padd(ctx, SA1_CHAR_SONIC, SA1_OBJTYPE_ITEMBOX_AIR, six, 1, 0, NULL, &air);
    ptick(ctx);
    sa1_preview_render(ctx, air, 1.0f, &r);
    printf("        air box: pair 1 %u draws\n", r->num_draws);
    ptick(ctx);
    {
        const sa1_render *r0 = NULL, *r5 = NULL, *r1 = NULL;
        uint32_t n0, n5, n1;
        sa1_preview_render(ctx, air, 0.0f, &r0);
        n0 = r0->num_draws;
        sa1_preview_render(ctx, air, 0.5f, &r5);
        n5 = r5->num_draws;
        changed_last = r5->interpolated == 0;
        sa1_preview_render(ctx, air, 1.0f, &r1);
        n1 = r1->num_draws;
        CHECK(changed_last && n0 == n1 && n5 == n1 && n1 == 7, "a copy whose draws changed between its pairs (a new air "
              "box: pair 2 has %u draws): the last pair at every alpha (0: %u, 0.5: %u draws, not interpolated)", n1, n0, n5);
    }
    tris_free(&a);
    tris_free(&b);
    tris_free(&c);
    tris_free(&d);
    sa1_preview_clear(ctx);
}

/* ---- (1, task 7) the settle steps: the first pair whose draws have their final size ------------------------------------- */
static void t_settle(void)
{
    sa1_context *ctx = g_ctx;
    static const uint32_t types[2] = { SA1_OBJTYPE_ITEMBOX, SA1_OBJTYPE_ITEMBOX_AIR };
    const float three[3] = { 3, 0, 0 };
    float b[2][41][6];
    uint32_t settled[2][41], ids[2] = { 0, 0 }, k, p, i;
    printf("settle (1, task 7): the steps until a new box copy draws at its final size, against sa1_preview_type."
           "settle_steps and sa1_preview_info.settled\n");
    sa1_preview_clear(ctx);
    for (k = 0; k < 2; k++) {
        padd(ctx, SA1_CHAR_SONIC, types[k], three, 1, 0, NULL, &ids[k]);
    }
    for (p = 1; p <= 40; p++) {
        ptick(ctx);
        for (k = 0; k < 2; k++) {
            sa1_preview_info pi;
            memset(&pi, 0, sizeof pi);
            pi.struct_size = sizeof pi;
            sa1_preview_info_get(ctx, ids[k], &pi);
            memcpy(b[k][p], pi.bounds, sizeof pi.bounds);
            settled[k][p] = pi.settled;
        }
    }
    for (k = 0; k < 2; k++) {
        sa1_preview_type t;
        uint32_t first = 0;
        memset(&t, 0, sizeof t);
        t.struct_size = sizeof t;
        sa1_preview_type_get(ctx, types[k], &t);
        for (p = 40; p >= 1; p--) {
            int same = 1;
            for (i = 0; i < 6; i++) {
                same &= fabsf(b[k][p][i] - b[k][40][i]) < 1e-4f;
            }
            if (!same) {
                break;
            }
            first = p;
        }
        CHECK(first > 1 && settled[k][first] == 1 && settled[k][first - 1] == 0 && 2u * first >= t.settle_steps &&
              2u * (first - 1u) < t.settle_steps, "%s: drawn at its final size from pair %u (the exec of age %u: it has run "
              "%u steps), settled from that pair on (settle_steps %u)", types[k] == SA1_OBJTYPE_ITEMBOX ? "ITEMBOX" :
              "ITEMBOX_AIR", first, 2u * first - 1u, 2u * first, t.settle_steps);
    }
    sa1_preview_clear(ctx);
}

/* ---- (9) determinism ------------------------------------------------------------------------------------------------ */
static uint32_t det_run(sa1_context *ctx)
{
    uint32_t ids[2 * PD_N], n = 0, k, p, crc = 0;
    sa1_preview_clear(ctx);
    for (k = 0; k < 2u * PD_N; k++) {
        const pdesc *D = &k_desc[k % PD_N];
        if (padd(ctx, k < PD_N ? SA1_CHAR_SONIC : SA1_CHAR_KNUCKLES, D->type, D->p, D->state, D->bits, NULL, &ids[n]) ==
            SA1_OK) {
            n++;
        }
    }
    for (p = 0; p < 40; p++) {
        ptick(ctx);
        for (k = 0; k < n; k++) {
            const sa1_render *r = NULL;
            sa1_preview_render(ctx, ids[k], 1.0f, &r);
            crc = render_crc(r, crc);
            sa1_preview_render(ctx, ids[k], 0.5f, &r);
            crc = render_crc(r, crc);
        }
    }
    sa1_preview_clear(ctx);
    return crc ^ n;
}

static void t_determinism(void)
{
    uint32_t a, b;
    printf("determinism (9): Sonic's and Knuckles' copies of every description of (1), 40 pairs, every render at alpha 1 "
           "and 0.5, twice (build_preview.py compares x86 and x64)\n");
    a = det_run(g_ctx);
    b = det_run(g_ctx);
    CHECK(a == b, "two runs in a row: %08X / %08X", a, b);
    printf("DETERMINISM_PREVIEW %08X (every copy's render output)\n", a);
}

/* ---- (10) cost ------------------------------------------------------------------------------------------------------ */
static size_t wset(void)
{
#if defined(_WIN32)
    struct {
        DWORD cb, PageFaultCount;
        SIZE_T PeakWorkingSetSize, WorkingSetSize, a, b, c, d, PagefileUsage, PeakPagefileUsage;
    } m;
    memset(&m, 0, sizeof m);
    m.cb = sizeof m;
    K32GetProcessMemoryInfo(GetCurrentProcess(), &m, sizeof m);
    return m.WorkingSetSize;
#else
    return 0;
#endif
}

static void cost_of(const char *what, uint32_t nd, const pdesc *const *ds, uint32_t nring)
{
    sa1_context *ctx = g_ctx;
    uint32_t ids[300], n = 0, k, p;
    const float zero[3] = { 0, 0, 0 };
    double t0, tp, tr;
    size_t m0, m1;
    sa1_preview_clear(ctx);
    m0 = wset();
    for (k = 0; k < nd; k++) {
        padd(ctx, SA1_CHAR_SONIC, ds[k]->type, ds[k]->p, ds[k]->state, ds[k]->bits, NULL, &ids[n++]);
    }
    for (k = 0; k < nring; k++) {
        padd(ctx, SA1_CHAR_SONIC, SA1_OBJTYPE_RING, zero, 1, 0, NULL, &ids[n++]);
    }
    for (p = 0; p < 10; p++) {
        ptick(ctx);
    }
    m1 = wset();
    t0 = tu_now_ms();
    for (p = 0; p < 60; p++) {
        ptick(ctx);
    }
    tp = (tu_now_ms() - t0) / 60.0;
    t0 = tu_now_ms();
    for (p = 0; p < 60; p++) {
        for (k = 0; k < n; k++) {
            const sa1_render *r = NULL;
            sa1_preview_render(ctx, ids[k], 0.5f, &r);
        }
    }
    tr = (tu_now_ms() - t0) / 60.0;
    NOTE("%-36s %3u copies: a pair %.3f ms, rendering every copy (alpha 0.5) %.3f ms; working set %+.1f MB with the "
         "showcase", what, n, tp, tr, ((double)m1 - (double)m0) / 1048576.0);
    sa1_preview_clear(ctx);
}

static void t_cost(void)
{
    const pdesc *probe[17];
    uint32_t k, n = 0;
    printf("cost (10): report only\n");
    for (k = 0; k < PD_N && n < 17; k++) {
        const pdesc *D = &k_desc[k];
        if (D->bits == 0x7Eu || (D->type == SA1_OBJTYPE_ITEMBOX_AIR && ((int)D->p[0]) % 3 != 0)) {
            continue;
        }
        probe[n++] = D;
    }
    cost_of("the probe's 17 descriptions", n, probe, 0);
    cost_of("64 RING copies", 0, probe, 64);
    cost_of("256 RING copies", 0, probe, 256);
}

/* ---- main ----------------------------------------------------------------------------------------------------------- */
typedef struct test_case {
    const char *name;
    void (*fn)(void);
} test_case;
static const test_case k_tests[] = {
    { "parity", t_parity },       { "rotation", t_rotation },       { "places", t_places },
    { "isolation", t_isolation }, { "lifetime", t_lifetime },       { "arguments", t_arguments },
    { "interp", t_interp },       { "settle", t_settle },           { "determinism", t_determinism },
    { "cost", t_cost },
};

static int selected(const char *only, const char *name)
{
    size_t n = strlen(name);
    const char *p = only;
    if (only == NULL) {
        return 1;
    }
    while ((p = strstr(p, name)) != NULL) {
        if ((p == only || p[-1] == ',') && (p[n] == 0 || p[n] == ',')) {
            return 1;
        }
        p += n;
    }
    return 0;
}

int main(int argc, char **argv)
{
    const char *only = NULL;
    size_t i;
    int a;
    double t0;
    ga_args(&argc, argv);
    for (a = 1; a < argc; a++) {
        if (!strcmp(argv[a], "--verbose")) {
            g_verbose = 1;
        } else if (!strcmp(argv[a], "--only") && a + 1 < argc) {
            only = argv[++a];
        } else if (!strcmp(argv[a], "--list")) {
            for (i = 0; i < sizeof k_tests / sizeof k_tests[0]; i++) {
                printf("%s\n", k_tests[i].name);
            }
            return 0;
        } else {
            g_gdi = argv[a];
        }
    }
    if (g_gdi == NULL) {
        g_gdi = getenv("SA1_TEST_GDI");
    }
    if (g_gdi == NULL) {
        printf("preview_test: needs the disc (GDI argument or SA1_TEST_GDI)\n");
        return 2;
    }
    g_ctx = mkctx(0, 1);
    if (g_ctx == NULL) {
        return 1;
    }
    printf("libsa1 %s (SA1_API_PREVIEW %d)\n", sa1_version_string(), SA1_API_PREVIEW);
    for (i = 0; i < sizeof k_tests / sizeof k_tests[0]; i++) {
        if (!selected(only, k_tests[i].name)) {
            continue;
        }
        t0 = tu_now_ms();
        k_tests[i].fn();
        printf("  (%s: %.1f s)\n", k_tests[i].name, (tu_now_ms() - t0) / 1000.0);
    }
    sa1_shutdown(g_ctx);
    printf("preview_test: %d ok, %d failed, %d character tick faults, %d preview tick faults\n",
           g_ok, g_fail, g_tick_faults, g_preview_faults);
    return g_fail || g_tick_faults ? 1 : 0;
}
