/*
 * set_test.c - round 0.9.4 (094-lib-set): SET objects in libsa1 - rings, ring lines / circles and both item boxes placed
 * through the API (include/libsa1.h "SET objects", SA1_API_SET) and run by the game's own SET manager in every world
 * (src/sa1_set.c, src/world/sa1_world_set.c; sadc docs/research/gsa1_094_plan.md 6.3 with its Review R.3-R.16,
 * gsa1_094_items.md, gsa1_090_rings.md 4.1 with its Review).
 *
 *   set_test [--only NAME[,NAME]] [--list] [--verbose] [--guard-alloc] GDI     (GDI: or $SA1_TEST_GDI)
 *
 * Built and run by tests/build_set.py (a snapshot of build/<target>/libsa1.a and LIB's headers). Records go through the
 * API only (never the harness's --create-task). Every scene: native coordinates (SA1_COORDS_PRESET_NATIVE: y up, 1 unit =
 * 1 SA1 unit = 4 HU in G-SA1), a flat GRID floor 6000 x 6000 at y = 0 (sa1_collision_set_static, the call G-SA1 makes),
 * characters made with SA1_CREATE_WORLD at the given place facing +X, one pair (1/30 s) per sa1_character_tick, the
 * camera yaw 0 (a forward stick moves along +X). Each case prints what it checks; the numbers of the plan's set_test list
 * are in the case names. "HU" in the output = 4 x SA1 units (G-SA1's scale).
 *
 * Round 0.9.4b (094b-lib-range; sadc docs/research/objects2_plan.md 6.1 with its Review, 11.0 O1, 11.1): the cases r1-r12
 * (the plan's (R1)-(R12)) and bigcircle - the spawn range of the generated list, the window's order B, the maxima 200 /
 * 100 against the game's collision lists, the capture at the maxima, the sides (view_tan), sa1_set_live, a 128-ring
 * circle with a window below 128. The old cases run with range 0 / view_tan 0 (the game's), as in 0.9.4.
 *
 * API NOTES of round 0.9.4b for 094b-gsa1-objects (094b-lib-range; the measured numbers are this file's r-cases, x64 = x86):
 *   sa1_set_config (struct_size-gated tail; a 0.9.4-sized struct keeps both values):
 *     range     SA1 units, 0 = the game's 400 (0.9.4 exactly), else SA1_SET_RANGE_MIN 400 .. SA1_SET_RANGE_MAX 3000 (owner
 *               answer O1: 12,000 HU, the far clip). HU = range x coords.scale (4 in G-SA1): 4,800 HU -> 1200. Every
 *               character world of the context, applied at its next pair start. sa1_object_info.range reports it in host
 *               units (1,600 HU at 0, 4,800 after 1200, 12,000 at 3000).
 *     view_tan  0 = the game's 4:3 test (tan 35.0 = 0.700), else 0.700 .. SA1_SET_VIEW_TAN_MAX 4.0: tan of the host
 *               view's HORIZONTAL half angle (Source's hor+: tan(h) = tan(fov / 2) x (w / h) / (4 / 3); 3440 x 1440 at fov
 *               92.5 -> 1.873, 16:9 at fov 75 -> 1.025). Only the four SET types' sideways visibility tests; up / down /
 *               depth and the eye stay the world's virtual eye (40 u behind, 7 up, level).
 *     maxima    SA1_SET_RING_WINDOW_MAX 200, SA1_SET_BOX_WINDOW_MAX 100 (were 70 / 32); defaults stay 64 / 32 in libsa1.
 *   sa1_set_live(ctx, id, records, capacity, &count): ids whose task lives after the last pair (ascending);
 *     SA1_ERR_BUFFER_TOO_SMALL with count; SA1_ERR_NOT_FOUND unknown character; 0 for a puppet / no SET state.
 *   The window: reach = range (400 at 0) + margin, tested at the record's place (where the game's spawner tests it);
 *     above 400 order B (tier 0 within 400, tier 1 in front of the virtual eye's plane, tier 2 the rest; tier 1 / 2 held
 *     SA1_SET_TIER_HOLD 30 pairs); at every range a ring line / circle orders by its nearest untaken ring (11.1).
 *   RT_CAP_MAX 2,048 (r5: 1,024 dropped 101 records and the HUD at the maxima with blob shadows).
 * Measured: the road (200 rings 20 u apart) first spawn = range - 8 u: 392 / 792 / 1,192 / 1,592 / 1,992 / 2,993 u at
 *   400 / 800 / 1,200 / 1,600 / 2,000 / 3,000 (windows 200 / 100); 1,200 with ring_window 128: 1,192 u; 1,600 with 128:
 *   1,592 u ahead; 1,600 with 64: 875 u (0.9.4's nearest-first order: 631). The field (1.3 B) at 1,600: 41 rings drawn
 *   with view_tan 0, 49 with 1.025, 64 with 1.873 (kx_scale 0.6831 / 0.3738). Range 1,200 for Big's lure: the eye is the
 *   centre. CCL at 200 / 100 with a scatter and a fountain: list 7 <= 225 entries a step, list 2 <= 100; 300 ring tasks
 *   (registry override): 0 of the 50 nearest taken. Heap at 200 + 100 live: largest block >= 216.8 KB. Capture at the
 *   maxima: <= 1,125 records (925 without shadows).
 * For 0.9.6 (followers): range and view_tan apply to EVERY character world of the context; follower worlds must keep 400
 *   (a per-world opt-out; objects2_range.md 3.2 item 6: 99 Knuckles at 5x would each run up to 200 ring tasks).
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
#include "sa1_hostobj.h"    /* sa1ho_world_heap: the game heap */
#include "sa1_capture.h"    /* round 0.9.4b: sa1_capture_get_stats (r5) */
#include "test_util.h"
#include "guard_alloc.c"    /* SA1_GUARD_ALLOC=1 or --guard-alloc: every library heap block against a guard page */
#define sa1_host_defaults(h) (sa1_host_defaults(h), ga_host(h))

static int g_fail, g_ok, g_verbose;
static const char *g_gdi;

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

/* ---- addresses (US v1.005) ---------------------------------------------------------------------------------------- */
#define A_TASK_LISTS    0x8C79F04Cu     /* task *B_8C79F04C[8] */
#define A_TASK_FREE     0x8C79F048u     /* task *B_8C79F048: the free list */
#define A_PLAYER_TWP    0x8C78C608u     /* playertwp[8] */
#define A_PLAYER_MWP    0x8C78C628u
#define A_PLAYER_PWP    0x8C78C668u
#define A_RINGS         0x8C752BE4u     /* Sint16 */
#define A_LIVES         0x8C752BE0u     /* Sint8 */
#define A_COND          0x8C79A3E0u     /* OBJ_CONDITION[1024] */
#define A_COUNT         0x8C79A3DCu     /* Sint16 B_8C79A3DC */
#define A_ITEMTABLE     0x8C79A3CCu
#define A_ICONS         0x8C78C394u     /* {mode, num, timer, entry[20]} */
#define A_JUMPPANELS    0x8C78C4E0u     /* the jump panel list right behind the icon list */
#define A_BIGFISH       0x8C7A40E8u     /* Uint16 B_8C7A40E8 (0x40: Big's lure in the water) */
#define A_CAMERA_TWP    0x8C18B7ECu     /* taskwk *D_8C18B7EC */
#define A_BOMB_R        0x8C78C390u     /* Float: the bomb sphere's radius (< 0: off) */
#define X_RING          0x8C0869E8u
#define X_GRING         0x8C087B80u
#define X_GBOX          0x8C05D10Cu
#define X_ABOX          0x8C05D9F6u
#define X_MANAGER       0x8C090FA0u
#define X_DESTROY       0x8C09865Cu
#define X_SPEEDSHOES    0x8C07D2DAu
#define X_SPEEDTIMER    0x8C07D274u
#define FN_MALLOC       0x8C098A80u     /* void *MAlloc(Sint32) */
#define FN_MFREE        0x8C098B42u     /* void MFree(void *) */
#define FN_DIGFOUNTAIN  0x8C0CB30Eu     /* the dig-up ring fountain at pos (32 rings) */
#define SE_RING         7u
#define SE_ITEM         11u
#define SE_1UP          0x2C0u

static const char *k_items[9] = { "speed shoes", "invincibility", "5 rings", "10 rings", "random rings", "barrier",
                                  "1-up", "bomb", "magnetic barrier" };
static const char *k_kinds[9] = { "Sonic", "Eggman", "Tails", "Knuckles", "Tikal", "Amy", "E-102", "Big", "Super Sonic" };

static void log_cb(void *user, int32_t level, const char *msg)
{
    (void)user;
    if (level >= SA1_LOG_WARN || g_verbose) {
        printf("    [libsa1] %s\n", msg);
    }
}

/* ---- guest memory --------------------------------------------------------------------------------------------------- */
static int arena(uint32_t g)
{
    return g != 0 && ((g >> 24) & 31u) == SA1G_SEG_ARENA;
}

static uint32_t rd32(const sa1g_world *w, uint32_t g)
{
    uint32_t v = 0;
    if (w == NULL || sa1g_world_rd32(w, g & ~3u, &v) != 0) {
        return 0;
    }
    return v;
}

static uint32_t rd16(const sa1g_world *w, uint32_t g)
{
    return (rd32(w, g & ~3u) >> (8u * (g & 2u))) & 0xFFFFu;
}

static uint32_t rd8(const sa1g_world *w, uint32_t g)
{
    return (rd32(w, g & ~3u) >> (8u * (g & 3u))) & 0xFFu;
}

static float rdf(const sa1g_world *w, uint32_t g)
{
    uint32_t v = rd32(w, g);
    float f;
    memcpy(&f, &v, 4);
    return f;
}

static void wr32(sa1g_world *w, uint32_t g, uint32_t v)
{
    if (w != NULL) {
        sa1g_world_wr32(w, g & ~3u, v);
    }
}

static void wr16(sa1g_world *w, uint32_t g, uint32_t v)
{
    uint32_t a = g & ~3u, sh = 8u * (g & 2u);
    wr32(w, a, (rd32(w, a) & ~(0xFFFFu << sh)) | ((v & 0xFFFFu) << sh));
}

static void wr8(sa1g_world *w, uint32_t g, uint32_t v)
{
    uint32_t a = g & ~3u, sh = 8u * (g & 3u);
    wr32(w, a, (rd32(w, a) & ~(0xFFu << sh)) | ((v & 0xFFu) << sh));
}

static void wrf(sa1g_world *w, uint32_t g, float f)
{
    uint32_t v;
    memcpy(&v, &f, 4);
    wr32(w, g, v);
}

static int same(uint32_t a, uint32_t b)
{
    return (a & 0x1FFFFFFFu) == (b & 0x1FFFFFFFu);
}

static uint32_t twp0(const sa1g_world *w)
{
    uint32_t t = rd32(w, A_PLAYER_TWP);
    return arena(t) ? t : 0u;
}

static void ppos(const sa1g_world *w, float p[3])
{
    uint32_t t = twp0(w);
    p[0] = t ? rdf(w, t + 0x20u) : 0.0f;
    p[1] = t ? rdf(w, t + 0x24u) : 0.0f;
    p[2] = t ? rdf(w, t + 0x28u) : 0.0f;
}

/* Sonic's run from (0, 0.5, 0) with the stick forward drifts to -z (measured: z = -0.1035 x) */
static float lane_z(float x)
{
    return -0.1035f * x;
}

static float dist3(const float a[3], const float b[3])
{
    float d[3] = { a[0] - b[0], a[1] - b[1], a[2] - b[2] };
    return sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
}

static int16_t rings_of(const sa1g_world *w)
{
    return (int16_t)rd16(w, A_RINGS);
}

static uint32_t task_mode(const sa1g_world *w, uint32_t tp)
{
    uint32_t twp = arena(tp) ? rd32(w, tp + 0x20u) : 0u;
    return arena(twp) ? rd8(w, twp) : 0xEEu;
}

/* the tasks of the eight lists (top level) whose exec is `exec` */
static uint32_t count_exec(const sa1g_world *w, uint32_t exec, uint32_t *first)
{
    uint32_t lv, n = 0;
    if (first != NULL) {
        *first = 0;
    }
    for (lv = 0; lv < 8u; lv++) {
        uint32_t tp = rd32(w, A_TASK_LISTS + 4u * lv), guard = 0;
        while (arena(tp) && guard++ < 0x2000u) {
            if (same(rd32(w, tp + 0x10u), exec)) {
                if (first != NULL && n == 0) {
                    *first = tp;
                }
                n++;
            }
            tp = rd32(w, tp);
        }
    }
    return n;
}

/* every task (children included, depth 3) whose colliwk id is `id` and that entered CCL this step-ish (a colliwk) */
static uint32_t count_ccl_in(const sa1g_world *w, uint32_t tp, uint32_t id, int depth)
{
    uint32_t n = 0, guard = 0;
    while (arena(tp) && guard++ < 0x2000u) {
        uint32_t twp = rd32(w, tp + 0x20u), cwp = arena(twp) ? rd32(w, twp + 0x38u) : 0u, ex = rd32(w, tp + 0x10u);
        if (arena(cwp) && rd16(w, cwp) == id && ex != 0 && !same(ex, X_DESTROY)) {
            n++;
        }
        if (depth < 3) {
            n += count_ccl_in(w, rd32(w, tp + 0x0Cu), id, depth + 1);
        }
        tp = rd32(w, tp);
    }
    return n;
}

static uint32_t count_ccl(const sa1g_world *w, uint32_t id)
{
    uint32_t lv, n = 0;
    for (lv = 0; lv < 8u; lv++) {
        n += count_ccl_in(w, rd32(w, A_TASK_LISTS + 4u * lv), id, 0);
    }
    return n;
}

static uint32_t free_tasks(const sa1g_world *w)
{
    uint32_t tp = rd32(w, A_TASK_FREE), n = 0;
    while (arena(tp) && n < 0x2000u) {
        n++;
        tp = rd32(w, tp);
    }
    return n;
}

static uint32_t crc_guest(const sa1g_world *w, uint32_t g, uint32_t n)
{
    uint32_t crc = 0, i;
    for (i = 0; i < n; i += 4u) {
        uint32_t v = rd32(w, g + i);
        crc = tu_crc32(crc, (const uint8_t *)&v, 4);
    }
    return crc;
}

/* the arena with every aligned word that names the extension segment 0x18 hashed as 0 (as super_test's
 * DETERMINISM_MASKED, round 0.9.3a review RC6): the SET layer's object list and SET entries live in segment 0x18, whose
 * layout differs between the targets (ga32 keeps the R4 pcdata in static storage, ga64 at the start of segment 0x18), so
 * B_8C79A3CC and each condition's pObjEditEntry are other guest words on x86 and x64 */
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

/* ---- calls inside a world --------------------------------------------------------------------------------------------- */
typedef struct call_req {
    uint32_t fn;
    uint32_t a;
    int32_t n;
    float pos[3];
    uint32_t ret;
} call_req;

static void malloc_cb(sa1g_world *w, void *user)
{
    call_req *r = (call_req *)user;
    void *p = ((void *(*)(int32_t))sa1_fn(SA1_GFN_C(FN_MALLOC), 0u))(r->n);
    (void)w;
    r->ret = p != NULL ? sa1_encode(p) : 0u;
}

static void mfree_cb(sa1g_world *w, void *user)
{
    call_req *r = (call_req *)user;
    (void)w;
    ((void (*)(void *))sa1_fn(SA1_GFN_C(FN_MFREE), 0u))(sa1_decode(r->a));
}

/* the game's air box spawner 8C05D9C0 (pos, item) at player 0 (what Knuckles' dig outcome 5 calls) */
static void airbox_cb(sa1g_world *w, void *user)
{
    uint32_t t = twp0(w);
    if (t) {
        ((void (*)(void *, int32_t))sa1_fn(SA1_GFN_C(0x8C05D9C0u), 0u))(sa1g_world_ptr(w, t + 0x20u), *(int32_t *)user);
    }
}

static void fountain_cb(sa1g_world *w, void *user)
{
    call_req *r = (call_req *)user;
    uint32_t t = twp0(w);
    float keep[3];
    uint8_t *p;
    if (!t) {
        return;
    }
    /* the fountain reads its position through a pointer: player 0's own taskwk position, moved there for the call */
    p = (uint8_t *)sa1g_world_ptr(w, t + 0x20u);
    memcpy(keep, p, 12);
    memcpy(p, r->pos, 12);
    ((void (*)(void *))sa1_fn(SA1_GFN_C(FN_DIGFOUNTAIN), 0u))(p);
    memcpy(p, keep, 12);
}

/* ---- the scene and the characters -------------------------------------------------------------------------------- */
static int scene(sa1_context *ctx)
{
    sa1_collision_tri t[2];
    float q[4][3] = { { -3000, 0, -3000 }, { -3000, 0, 3000 }, { 3000, 0, 3000 }, { 3000, 0, -3000 } };
    sa1_status st;
    memset(t, 0, sizeof t);
    memcpy(t[0].v[0], q[0], 12);
    memcpy(t[0].v[1], q[1], 12);
    memcpy(t[0].v[2], q[2], 12);
    memcpy(t[1].v[0], q[0], 12);
    memcpy(t[1].v[1], q[2], 12);
    memcpy(t[1].v[2], q[3], 12);
    t[0].surface = t[1].surface = SA1_SURFACE_SOLID;
    st = sa1_collision_set_static(ctx, t, 2);
    if (SA1_FAILED(st)) {
        printf("  set_static: %s: %s\n", sa1_status_string(st), sa1_last_error(ctx));
        return -1;
    }
    return 0;
}

/* kind at (x, y, z) facing +X; rings >= 0: start rings; kind 8 and the General Super Sonic (kind 0 with general 1) get
 * SA1_SUPER_NO_DRAIN (and the General SA1_SUPER_START) */
static sa1_character_id mk(sa1_context *ctx, int32_t kind, float x, float y, float z, int32_t rings, int general)
{
    sa1_character_desc d;
    sa1_character_id id = 0;
    sa1_status st;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.kind = kind;
    d.flags = SA1_CREATE_WORLD | SA1_CREATE_SA1_SPACE | SA1_CREATE_ANGLES;
    d.position[0] = x;
    d.position[1] = y;
    d.position[2] = z;
    if (rings >= 0) {
        d.flags |= SA1_CREATE_START_RINGS;
        d.start_rings = rings;
    }
    if (kind == SA1_CHAR_SUPER_SONIC) {
        d.flags |= SA1_CREATE_SUPER;
        d.super_flags = SA1_SUPER_NO_DRAIN;
    } else if (general) {
        d.flags |= SA1_CREATE_SUPER | SA1_CREATE_SUPER_CAPABLE;
        d.super_flags = SA1_SUPER_GENERAL | SA1_SUPER_START | SA1_SUPER_NO_DRAIN;
    }
    st = sa1_character_create_ex(ctx, &d, &id);
    if (SA1_FAILED(st)) {
        printf("  create %s: %s: %s\n", kind >= 0 && kind < 9 ? k_kinds[kind] : "?", sa1_status_string(st),
               sa1_last_error(ctx));
        return 0;
    }
    return id;
}

/* ---- running, with the event logs --------------------------------------------------------------------------------- */
typedef struct evlog {
    sa1_event *e;
    uint32_t n, cap;
    sa1_set_event *s;
    uint32_t ns, caps;
} evlog;
static evlog g_log;

static void log_reset(void)
{
    g_log.n = 0;
    g_log.ns = 0;
}

static void poll_all(sa1_context *ctx)
{
    sa1_event ev[64];
    sa1_set_event se[64];
    uint32_t n, k;
    do {
        n = 0;
        sa1_poll_events(ctx, ev, 64, &n);
        for (k = 0; k < n; k++) {
            if (g_log.n == g_log.cap) {
                g_log.cap = g_log.cap ? 2u * g_log.cap : 1024u;
                g_log.e = (sa1_event *)realloc(g_log.e, (size_t)g_log.cap * sizeof *g_log.e);
            }
            g_log.e[g_log.n++] = ev[k];
        }
    } while (n == 64);
    do {
        n = 0;
        sa1_poll_set_events(ctx, se, 64, &n);
        for (k = 0; k < n; k++) {
            if (g_log.ns == g_log.caps) {
                g_log.caps = g_log.caps ? 2u * g_log.caps : 1024u;
                g_log.s = (sa1_set_event *)realloc(g_log.s, (size_t)g_log.caps * sizeof *g_log.s);
            }
            g_log.s[g_log.ns++] = se[k];
            if (g_verbose) {
                printf("    set event %u record %08X cond %04X bits %02X step %u cause %u touch (%.1f %.1f %.1f) "
                       "attacker (%.1f %.1f %.1f)\n", (unsigned)se[k].type, (unsigned)se[k].record,
                       (unsigned)se[k].condition, (unsigned)se[k].user_bits, (unsigned)se[k].step, (unsigned)se[k].cause,
                       (double)se[k].position[0], (double)se[k].position[1], (double)se[k].position[2],
                       (double)se[k].attacker[0], (double)se[k].attacker[1], (double)se[k].attacker[2]);
            }
        }
    } while (n == 64);
}

static int g_faults;

static int tick(sa1_context *ctx, sa1_character_id id, float sx, float sy, uint32_t buttons)
{
    sa1_input in;
    sa1_status st;
    memset(&in, 0, sizeof in);
    in.struct_size = sizeof in;
    in.stick_x = sx;
    in.stick_y = sy;
    in.buttons = buttons;
    st = sa1_character_tick(ctx, id, &in, 1.0f / 30.0f, NULL, NULL);
    poll_all(ctx);
    if (st != SA1_OK) {
        g_faults++;
        printf("  tick: %s: %s\n", sa1_status_string(st), sa1_last_error(ctx));
        return -1;
    }
    return 0;
}

static int run(sa1_context *ctx, sa1_character_id id, int pairs, float sx, float sy, uint32_t buttons)
{
    int i;
    for (i = 0; i < pairs; i++) {
        if (tick(ctx, id, sx, sy, buttons) != 0) {
            return -1;
        }
    }
    return 0;
}

static uint32_t n_sound(uint32_t se, sa1_character_id who)
{
    uint32_t i, c = 0;
    for (i = 0; i < g_log.n; i++) {
        c += g_log.e[i].type == SA1_EVENT_SOUND && g_log.e[i].id == se && (who == 0 || g_log.e[i].character == who);
    }
    return c;
}

static uint32_t n_capacity(uint32_t id, sa1_character_id who)
{
    uint32_t i, c = 0;
    for (i = 0; i < g_log.n; i++) {
        c += g_log.e[i].type == SA1_EVENT_CAPACITY && g_log.e[i].id == id && (who == 0 || g_log.e[i].character == who);
    }
    return c;
}

static uint32_t n_faults(void)
{
    uint32_t i, c = 0;
    for (i = 0; i < g_log.n; i++) {
        c += g_log.e[i].type == SA1_EVENT_FAULT || g_log.e[i].type == SA1_EVENT_UNIMPLEMENTED;
    }
    return c;
}

/* set events of a type (and record, 0 = any; character, 0 = any); *last = the last one */
static uint32_t n_set(uint32_t type, uint32_t record, sa1_character_id who, const sa1_set_event **last)
{
    uint32_t i, c = 0;
    if (last != NULL) {
        *last = NULL;
    }
    for (i = 0; i < g_log.ns; i++) {
        const sa1_set_event *e = &g_log.s[i];
        if (e->type == type && (record == 0 || e->record == record) && (who == 0 || e->character == who)) {
            c++;
            if (last != NULL) {
                *last = e;
            }
        }
    }
    return c;
}

/* ---- records -------------------------------------------------------------------------------------------------------- */
#define MAX_RECS 8192u
static uint32_t g_recs[MAX_RECS], g_nrecs;

static void yaw_matrix(float deg, float m[9])
{
    float a = deg * 0.017453292f, c = cosf(a), s = sinf(a);
    /* SA1 space (native coordinates): R = Ry(a), column vectors, row-major */
    m[0] = c;
    m[1] = 0.0f;
    m[2] = s;
    m[3] = 0.0f;
    m[4] = 1.0f;
    m[5] = 0.0f;
    m[6] = -s;
    m[7] = 0.0f;
    m[8] = c;
}

static uint32_t rec_ex(sa1_context *ctx, uint32_t type, float x, float y, float z, float p0, float p1, float p2,
                       uint32_t kinds, float yaw_deg, sa1_status *st_out)
{
    sa1_set_record r;
    uint32_t id = 0;
    sa1_status st;
    memset(&r, 0, sizeof r);
    r.struct_size = sizeof r;
    r.type = type;
    r.position[0] = x;
    r.position[1] = y;
    r.position[2] = z;
    if (yaw_deg != 0.0f) {
        yaw_matrix(yaw_deg, r.rotation);
    }
    r.params[0] = p0;
    r.params[1] = p1;
    r.params[2] = p2;
    r.kinds = kinds;
    r.user = 0x1000u + g_nrecs;
    st = sa1_set_add(ctx, &r, &id);
    if (st_out != NULL) {
        *st_out = st;
    } else if (st != SA1_OK) {
        printf("  sa1_set_add: %s: %s\n", sa1_status_string(st), sa1_last_error(ctx));
    }
    if (st == SA1_OK && g_nrecs < MAX_RECS) {
        g_recs[g_nrecs++] = id;
    }
    return id;
}

static uint32_t rec(sa1_context *ctx, uint32_t type, float x, float y, float z, float p0, float p1, float p2)
{
    return rec_ex(ctx, type, x, y, z, p0, p1, p2, 0u, 0.0f, NULL);
}

static void move_rec(sa1_context *ctx, uint32_t id, uint32_t type, float x, float y, float z, float p0, float p1, float p2,
                     uint32_t kinds)
{
    sa1_set_record r;
    sa1_status st;
    memset(&r, 0, sizeof r);
    r.struct_size = sizeof r;
    r.type = type;
    r.position[0] = x;
    r.position[1] = y;
    r.position[2] = z;
    r.params[0] = p0;
    r.params[1] = p1;
    r.params[2] = p2;
    r.kinds = kinds;
    st = sa1_set_update(ctx, id, &r);
    if (st != SA1_OK) {
        printf("  sa1_set_update: %s: %s\n", sa1_status_string(st), sa1_last_error(ctx));
    }
}

static void clear_recs(sa1_context *ctx)
{
    uint32_t i;
    for (i = 0; i < g_nrecs; i++) {
        sa1_set_remove(ctx, g_recs[i]);
    }
    g_nrecs = 0;
}

/* the condition of a record in a world (0xFFFFFFFF: no slot) and the task pointing at it (0: none) */
static uint32_t cond_of(const sa1g_world *w, uint32_t id)
{
    int32_t k = sa1w_set_world_slot(w, id);
    return k < 0 ? 0xFFFFFFFFu : rd16(w, A_COND + 0x10u * (uint32_t)k + 2u);
}

static uint32_t bits_of(const sa1g_world *w, uint32_t id)
{
    int32_t k = sa1w_set_world_slot(w, id);
    return k < 0 ? 0u : rd8(w, A_COND + 0x10u * (uint32_t)k + 1u);
}

static uint32_t task_of(const sa1g_world *w, uint32_t id)
{
    int32_t k = sa1w_set_world_slot(w, id);
    uint32_t c, tp;
    if (k < 0) {
        return 0;
    }
    c = A_COND + 0x10u * (uint32_t)k;
    tp = rd32(w, c + 4u);
    return arena(tp) && rd32(w, tp + 0x1Cu) == c && rd32(w, tp + 0x10u) != 0 ? tp : 0u;
}

static uint32_t children_of(const sa1g_world *w, uint32_t tp, uint32_t *alive)
{
    uint32_t ct = arena(tp) ? rd32(w, tp + 0x0Cu) : 0u, n = 0, a = 0, guard = 0;
    while (arena(ct) && guard++ < 64u) {
        n++;
        a += same(rd32(w, ct + 0x10u), X_RING);
        ct = rd32(w, ct);
    }
    if (alive != NULL) {
        *alive = a;
    }
    return n;
}

static void info_of(const sa1g_world *w, sa1set_world_info *i)
{
    sa1w_set_world_info(w, i);
}

static void heap_of(const sa1g_world *w, uint32_t *total, uint32_t *largest)
{
    sa1ho_world_heap(w, total, largest);
}

/* ---- (20) availability and the catalogue ----------------------------------------------------------------------- */
static void t_catalogue(sa1_context *ctx)
{
    static const struct { uint32_t type; const char *name; uint32_t exec, im, level, model, rot, states, budget; } want[4] = {
        { 1, "RING", 0x8C0869E8u, 2, 3, 0x8C1A2204u, SA1_SET_ROT_Y, SA1_SET_STATE_PRESENT, SA1_SET_BUDGET_RING },
        { 2, "O GRING", 0x8C087B80u, 2, 3, 0x8C1A2204u, SA1_SET_ROT_ZXY, SA1_SET_STATE_PRESENT, SA1_SET_BUDGET_RING },
        { 3, "O ITEMBOX", 0x8C05D10Cu, 2, 3, 0x8C1AC96Cu, SA1_SET_ROT_NONE, SA1_SET_STATE_PRESENT | SA1_SET_STATE_BROKEN,
          SA1_SET_BUDGET_BOX },
        { 4, "O ItemBoxAir", 0x8C05D9F6u, 15, 3, 0x8C1B87BCu, SA1_SET_ROT_ZXY, SA1_SET_STATE_PRESENT, SA1_SET_BUDGET_BOX },
    };
    sa1_object_info oi;
    sa1_status st;
    uint32_t t, id, ok = 1, tsv_ok = 0, tsv_rows = 0;
    char *text;
    size_t len = 0;
    printf("catalogue (20): sa1_object_info_get of types 1-4, gen/objtypes.tsv, availability\n");
    CHECK(sa1_object_count(ctx) == 4u, "sa1_object_count = %u", (unsigned)sa1_object_count(ctx));
    for (t = 1; t <= 4; t++) {
        memset(&oi, 0, sizeof oi);
        oi.struct_size = sizeof oi;
        st = sa1_object_info_get(ctx, t, &oi);
        ok = st == SA1_OK && oi.type == t && oi.name != NULL && !strcmp(oi.name, want[t - 1].name) &&
             oi.exec == want[t - 1].exec && oi.im == want[t - 1].im && oi.level == want[t - 1].level &&
             oi.list_flags == 0 && oi.range == 400.0f && oi.cls == SA1_OBJCLASS_CONSUMABLE && oi.model == want[t - 1].model &&
             oi.rotation_order == want[t - 1].rot && oi.available == 1 && oi.states == want[t - 1].states &&
             oi.budget == want[t - 1].budget;
        CHECK(ok, "type %u: \"%s\" exec %08X im %u level %u range %.0f model %08X rotation %u states %u budget %u "
              "available %u", (unsigned)t, oi.name ? oi.name : "?", (unsigned)oi.exec, (unsigned)oi.im, (unsigned)oi.level,
              (double)oi.range, (unsigned)oi.model, (unsigned)oi.rotation_order, (unsigned)oi.states, (unsigned)oi.budget,
              (unsigned)oi.available);
    }
    memset(&oi, 0, sizeof oi);
    oi.struct_size = sizeof oi;
    CHECK(sa1_object_info_get(ctx, 5, &oi) == SA1_ERR_NOT_FOUND && sa1_object_info_get(ctx, 0, &oi) == SA1_ERR_NOT_FOUND,
          "types 0 and 5: SA1_ERR_NOT_FOUND");
    /* gen/objtypes.tsv: the same numbers and names */
    text = tu_read_file("gen/objtypes.tsv", &len);
    if (text != NULL) {
        char *line = strtok(text, "\n");
        while (line != NULL) {
            unsigned idn = 0, exec = 0, im = 0, lv = 0;
            char name[32];
            if (line[0] != '#' && sscanf(line, "%u\t%31[^\t]\t%x\t%u\t%u", &idn, name, &exec, &im, &lv) == 5 && idn >= 1 &&
                idn <= 4) {
                tsv_rows++;
                tsv_ok += !strcmp(name, want[idn - 1].name) && exec == want[idn - 1].exec && im == want[idn - 1].im &&
                          lv == want[idn - 1].level;
            }
            line = strtok(NULL, "\n");
        }
        free(text);
    }
    CHECK(tsv_rows == 4 && tsv_ok == 4, "gen/objtypes.tsv: %u rows, %u agree with the library's catalogue",
          (unsigned)tsv_rows, (unsigned)tsv_ok);
    /* a type made unavailable by the test hook */
    sa1set_test_unavailable(ctx, SA1_OBJTYPE_ITEMBOX, 1);
    memset(&oi, 0, sizeof oi);
    oi.struct_size = sizeof oi;
    sa1_object_info_get(ctx, SA1_OBJTYPE_ITEMBOX, &oi);
    st = SA1_OK;
    id = rec_ex(ctx, SA1_OBJTYPE_ITEMBOX, 0, 0, 0, 2, 0, 0, 0, 0.0f, &st);
    CHECK(oi.available == 0 && st == SA1_ERR_UNSUPPORTED && id == 0, "ITEMBOX made unavailable: available %u, sa1_set_add "
          "-> %s", (unsigned)oi.available, sa1_status_string(st));
    sa1set_test_unavailable(ctx, SA1_OBJTYPE_ITEMBOX, 0);
    /* the record checks */
    {
        static const struct { uint32_t type; float p[3]; const char *what; } bad[] = {
            { SA1_OBJTYPE_ITEMBOX, { 9, 0, 0 }, "contents 9" },
            { SA1_OBJTYPE_ITEMBOX_AIR, { 2.5f, 0, 0 }, "contents 2.5" },
            { SA1_OBJTYPE_ITEMBOX, { -1, 0, 0 }, "contents -1" },
            { SA1_OBJTYPE_O_GRING, { 7, 10, 2 }, "O_GRING scl.z 2" },
            { SA1_OBJTYPE_O_GRING, { 16, 10, 0 }, "O_GRING 16 rings" },
            { 5, { 0, 0, 0 }, "type 5" },
        };
        uint32_t i, refused = 0;
        for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
            st = SA1_OK;
            id = rec_ex(ctx, bad[i].type, 0, 0, 0, bad[i].p[0], bad[i].p[1], bad[i].p[2], 0, 0.0f, &st);
            refused += st == SA1_ERR_INVALID_ARGUMENT && id == 0;
            if (st != SA1_ERR_INVALID_ARGUMENT) {
                printf("    %s: %s\n", bad[i].what, sa1_status_string(st));
            }
        }
        CHECK(refused == sizeof bad / sizeof bad[0], "bad records refused with SA1_ERR_INVALID_ARGUMENT: %u of %u",
              (unsigned)refused, (unsigned)(sizeof bad / sizeof bad[0]));
    }
    {
        sa1_set_config c;
        memset(&c, 0, sizeof c);
        c.struct_size = sizeof c;
        c.ring_window = SA1_SET_RING_WINDOW_MAX + 1u;   /* 151 until round 0.9.4b (maximum 70 then; now 200) */
        c.box_window = 32;
        c.margin = 100;
        CHECK(sa1_set_configure(ctx, &c) == SA1_ERR_INVALID_ARGUMENT, "sa1_set_configure ring_window %u: refused",
              (unsigned)c.ring_window);
        c.ring_window = 64;
        CHECK(sa1_set_configure(ctx, &c) == SA1_OK, "sa1_set_configure 64 / 32 / 100: ok");
    }
    clear_recs(ctx);
}

/* ---- (21) no records: nothing written --------------------------------------------------------------------------- */
static void t_norecords(sa1_context *ctx)
{
    sa1_character_id a, b, c;
    sa1g_world *wa, *wb, *wc;
    uint32_t i, diff_ab = 0, diff_ac = 0, outside_cam = 0, cam = 0, first_out = 0;
    printf("norecords (21): a world of a context without records against one without the SET hooks\n");
    clear_recs(ctx);
    a = mk(ctx, SA1_CHAR_SONIC, 0, 0.5f, 0, 0, 0);
    b = mk(ctx, SA1_CHAR_SONIC, 0, 0.5f, 0, 0, 0);
    c = mk(ctx, SA1_CHAR_SONIC, 0, 0.5f, 0, 0, 0);
    if (!a || !b || !c) {
        g_fail++;
        return;
    }
    wa = sa1_character_world(ctx, a);
    wb = sa1_character_world(ctx, b);
    wc = sa1_character_world(ctx, c);
    wb->set_pre = NULL;     /* a 0.9.3a world: no SET hooks */
    wb->set_post = NULL;
    wc->set_pre = NULL;
    wc->set_post = NULL;
    wc->set_no_eye = 1;     /* and the camera taskwk of 0.9.3a: (0, 0, 0) */
    for (i = 0; i < 60; i++) {
        float sy = i < 40 ? 1.0f : 0.0f;
        tick(ctx, a, 0.0f, sy, i == 20 ? SA1_BUTTON_JUMP : 0u);
        tick(ctx, b, 0.0f, sy, i == 20 ? SA1_BUTTON_JUMP : 0u);
        tick(ctx, c, 0.0f, sy, i == 20 ? SA1_BUTTON_JUMP : 0u);
    }
    {
        sa1set_world_info inf;
        info_of(wa, &inf);
        CHECK(inf.active == 0 && count_exec(wa, X_MANAGER, NULL) == 0 && rd32(wa, A_ITEMTABLE) == 0 && wa->set_state == 0,
              "no SET state, no manager task, B_8C79A3CC untouched");
    }
    {
        uint32_t ct = rd32(wa, A_CAMERA_TWP);
        for (i = 0; i < SA1G_ARENA_SIZE; i++) {
            uint32_t g = 0x8C000000u + i;
            diff_ab += wa->ram[i] != wb->ram[i];
            if (wa->ram[i] != wc->ram[i]) {
                diff_ac++;
                if (g >= ct + 0x20u && g < ct + 0x2Cu) {
                    cam++;
                } else if (!outside_cam++) {
                    first_out = g;
                }
            }
        }
    }
    CHECK(diff_ab == 0 && wa->steps == wb->steps, "the arena of the world with the SET hooks equals the one without them "
          "(%u bytes differ; both live, the eye written in both)", (unsigned)diff_ab);
    if (outside_cam == 0) {
        CHECK(cam > 0, "against a world without the eye: %u bytes differ, all in the camera taskwk's position (task 4 "
              "writes the eye in every live world: review R.11)", (unsigned)diff_ac);
    } else {
        NOTE("against a world without the eye: %u bytes differ, %u in the camera taskwk's position, %u elsewhere (first "
             "%08X): readers of camera_twp->pos in a running Sonic world (report)", (unsigned)diff_ac, (unsigned)cam,
             (unsigned)outside_cam, (unsigned)first_out);
        CHECK(1, "(the difference outside the camera position is the intended effect of the eye; listed above)");
    }
    sa1_character_destroy(ctx, a);
    sa1_character_destroy(ctx, b);
    sa1_character_destroy(ctx, c);
}

/* ---- (1) / (2) rings ---------------------------------------------------------------------------------------------- */
static void t_rings(sa1_context *ctx)
{
    static const float xs[5] = { 40.0f, 60.0f, 80.0f, 100.0f, 120.0f };
    sa1_character_id s, k;
    sa1g_world *ws, *wk;
    uint32_t ids[5], i, gone = 0, near = 0, se7, gone_all;
    float worst = 0.0f;
    printf("rings (1, 2, 2b): 5 RING records ahead of Sonic; the same in a Knuckles world, PRESENT 0 forced\n");
    clear_recs(ctx);
    log_reset();
    s = mk(ctx, SA1_CHAR_SONIC, 0, 0.5f, 0, 0, 0);
    k = mk(ctx, SA1_CHAR_KNUCKLES, 0, 0.5f, 600, 0, 0);
    if (!s || !k) {
        g_fail++;
        return;
    }
    ws = sa1_character_world(ctx, s);
    wk = sa1_character_world(ctx, k);
    for (i = 0; i < 5; i++) {
        ids[i] = rec(ctx, SA1_OBJTYPE_RING, xs[i], 6.0f, lane_z(xs[i]), 0, 0, 0);
    }
    run(ctx, k, 2, 0, 0, 0);
    tick(ctx, s, 0, 0, 0);
    {
        sa1set_world_info inf;
        info_of(ws, &inf);
        CHECK(inf.active && inf.manager != 0 && rd8(ws, rd32(ws, inf.manager + 0x20u)) == 1u &&
                  rd32(ws, A_ITEMTABLE) != 0 && inf.slots == 5 && rd16(ws, A_COUNT) == 5u,
              "the manager task (exec 8C090FA0, level 8 = list 7) in mode 1, the object list set, 5 slots, B_8C79A3DC %u",
              (unsigned)rd16(ws, A_COUNT));
        CHECK(count_exec(ws, X_RING, NULL) == 5u && n_set(SA1_SET_EVENT_SPAWNED, 0, s, NULL) == 5u,
              "5 RING tasks spawned by the game's spawner, 5 SPAWNED events");
    }
    for (i = 0; i < 70; i++) {
        tick(ctx, s, 0.0f, 1.0f, 0);
    }
    run(ctx, s, 20, 0, 0, 0);
    se7 = n_sound(SE_RING, s);
    gone_all = n_set(SA1_SET_EVENT_GONE, 0, s, NULL);
    for (i = 0; i < 5; i++) {
        const sa1_set_event *e = NULL;
        float rp[3], d;
        rp[0] = xs[i];
        rp[1] = 6.0f;
        rp[2] = lane_z(xs[i]);
        gone += n_set(SA1_SET_EVENT_GONE, ids[i], s, &e) == 1u;
        if (e != NULL) {
            d = dist3(e->position, rp);
            worst = d > worst ? d : worst;
            near += d <= 16.0f && e->cause == SA1_SET_CAUSE_BODY;
        }
    }
    CHECK(gone == 5 && gone_all == 5 && rings_of(ws) == 5 && se7 == 5,
          "Sonic ran through: one GONE per ring (%u of 5, %u in all), rings %d, SE 7 x%u", (unsigned)gone,
          (unsigned)gone_all, (int)rings_of(ws), (unsigned)se7);
    CHECK(near == 5, "each touch (cause BODY) within 64 HU of its ring: the farthest %.1f units (%.0f HU)", (double)worst,
          (double)(worst * 4.0f));
    CHECK(!(cond_of(ws, ids[0]) & 0x8000u), "a collected ring's condition: %04X (bit 15 clear)",
          (unsigned)(cond_of(ws, ids[0]) & 0xFFFFu));
    /* the host restores a ring the world took (a server refused the collection): PRESENT 1 again brings it back */
    {
        uint32_t se0 = n_sound(SE_RING, s);
        int32_t rg = rings_of(ws);
        sa1_set_state(ctx, ids[0], SA1_SET_STATE_PRESENT, 0);
        run(ctx, s, 2, 0, 0, 0);
        CHECK(task_of(ws, ids[0]) != 0 && cond_of(ws, ids[0]) == 0x8001u && n_sound(SE_RING, s) == se0 &&
                  rings_of(ws) == rg,
              "PRESENT 1 on a ring Sonic took: back in his world (condition %04X), silently",
              (unsigned)(cond_of(ws, ids[0]) & 0xFFFFu));
    }
    /* (2) Knuckles: the same rings moved next to him (an update: the record moves, the state is the host's) */
    log_reset();
    for (i = 0; i < 5; i++) {
        sa1_set_state(ctx, ids[i], SA1_SET_STATE_PRESENT, 0);
        move_rec(ctx, ids[i], SA1_OBJTYPE_RING, xs[i], 6.0f, 600.0f, 0, 0, 0, 1u << SA1_CHAR_KNUCKLES);
    }
    run(ctx, k, 3, 0, 0, 0);
    tick(ctx, s, 0, 0, 0);
    CHECK(count_exec(wk, X_RING, NULL) == 5u && cond_of(wk, ids[2]) == 0x8001u && cond_of(ws, ids[2]) == 0xFFFFFFFFu,
          "Knuckles' world: 5 RING tasks (condition %04X); Sonic's world has no slot for them (kinds)",
          (unsigned)(cond_of(wk, ids[2]) & 0xFFFFu));
    for (i = 0; i < 5; i++) {
        sa1_set_state(ctx, ids[i], 0u, 0u);
    }
    run(ctx, k, 4, 0, 0, 0);
    {
        uint32_t kept = 0;
        for (i = 0; i < 5; i++) {
            kept += sa1w_set_world_slot(wk, ids[i]) >= 0 && !(cond_of(wk, ids[i]) & 0x8000u);
        }
        CHECK(n_sound(SE_RING, k) == 0 && rings_of(wk) == 0 && count_exec(wk, X_RING, NULL) == 0 && kept == 5,
              "PRESENT 0 forced: SE 7 x%u, rings %d, ring tasks %u, the 5 slots kept with bit 15 clear (%u; review R.16)",
              (unsigned)n_sound(SE_RING, k), (int)rings_of(wk), (unsigned)count_exec(wk, X_RING, NULL), (unsigned)kept);
        CHECK(n_set(SA1_SET_EVENT_GONE, 0, k, NULL) == 0 && n_set(SA1_SET_EVENT_DESPAWNED, 0, k, NULL) == 5,
              "(2b) no GONE (a forced write raises none), %u DESPAWNED", (unsigned)n_set(SA1_SET_EVENT_DESPAWNED, 0, k, NULL));
    }
    for (i = 0; i < 5; i++) {
        move_rec(ctx, ids[i], SA1_OBJTYPE_RING, xs[i], 6.0f, 2000.0f, 0, 0, 0, 1u << SA1_CHAR_KNUCKLES);
    }
    run(ctx, k, 3, 0, 0, 0);
    {
        sa1set_world_info inf;
        info_of(wk, &inf);
        CHECK(sa1w_set_world_slot(wk, ids[0]) < 0 && inf.slots == 0 && rd16(wk, A_COUNT) == 0 &&
                  rd16(wk, A_COND + 2u) == 0,
              "out of the window: the slots released and zeroed (slots %u, B_8C79A3DC %u)", (unsigned)inf.slots,
              (unsigned)rd16(wk, A_COUNT));
    }
    clear_recs(ctx);
    sa1_character_destroy(ctx, s);
    sa1_character_destroy(ctx, k);
}

/* ---- (3) / (3b) / (4) ring groups ------------------------------------------------------------------------------------ */
static uint32_t sparkling_child(const sa1g_world *w, uint32_t tp)
{
    uint32_t ct = arena(tp) ? rd32(w, tp + 0x0Cu) : 0u, k = 0;
    for (; arena(ct) && k < 16; ct = rd32(w, ct), k++) {
        if (task_mode(w, ct) == 2u) {
            return ct;
        }
    }
    return 0;
}

static void t_groups(sa1_context *ctx)
{
    sa1_character_id s, t;
    sa1g_world *ws, *wt;
    uint32_t g, gt, n_before, n_after, alive, bits, ub;
    const sa1_set_event *e = NULL;
    int i;
    printf("groups (3, 3b, 4): O_GRING lines and circles (USERBITS, forced bits, the circle-of-8 quirk)\n");
    clear_recs(ctx);
    log_reset();
    s = mk(ctx, SA1_CHAR_SONIC, 0, 0.5f, 0, 0, 0);
    t = mk(ctx, SA1_CHAR_TAILS, -200, 0.5f, 0, 0, 0);
    if (!s || !t) {
        g_fail++;
        return;
    }
    ws = sa1_character_world(ctx, s);
    wt = sa1_character_world(ctx, t);
    /* a line along z through (60, 6, 0): rings at z = 0, +20, -20, +40, ... (the game alternates around the centre) */
    g = rec(ctx, SA1_OBJTYPE_O_GRING, 60.0f, 6.0f, 0.0f, 7, 20, 0);
    tick(ctx, s, 0, 0, 0);
    run(ctx, t, 2, 0, 0, 0);
    gt = task_of(wt, g);
    n_before = children_of(wt, gt, &alive);
    CHECK(gt != 0 && n_before == 8 && alive == 8, "Tails' world: the group with %u rings (a line of 7 + 1)",
          (unsigned)n_before);
    for (i = 0; i < 40; i++) {
        tick(ctx, s, 0.0f, 1.0f, 0);
    }
    run(ctx, s, 12, 0, 0, 0);
    bits = bits_of(ws, g);
    CHECK(n_set(SA1_SET_EVENT_USERBITS, g, s, &e) >= 1 && e != NULL && e->user_bits == bits && bits != 0 &&
              e->cause == SA1_SET_CAUSE_BODY,
          "Sonic ran through the line: USERBITS %02X (the world's scUserFlag %02X), rings %d, cause %u",
          e ? (unsigned)e->user_bits : 0u, (unsigned)bits, (int)rings_of(ws), e ? (unsigned)e->cause : 9u);
    CHECK(n_set(SA1_SET_EVENT_GONE, g, s, NULL) == 0, "no GONE for a group (its bit 15 stays)");
    /* the host shares the bits: Tails' group is made again without those rings */
    sa1_set_state(ctx, g, SA1_SET_STATE_PRESENT, bits);
    run(ctx, t, 3, 0, 0, 0);
    gt = task_of(wt, g);
    n_after = children_of(wt, gt, &alive);
    {
        uint32_t pc = 0, b;
        for (b = 0; b < 8; b++) {
            pc += (bits >> b) & 1u;
        }
        CHECK(gt != 0 && n_after == 8u - pc && bits_of(wt, g) == bits && n_set(SA1_SET_EVENT_USERBITS, g, t, NULL) == 0,
              "USERBITS forced into Tails' world: the group re-made with %u rings, scUserFlag %02X, no USERBITS there",
              (unsigned)n_after, (unsigned)bits_of(wt, g));
    }
    /* the echo in Sonic's world changes nothing (review R.10) */
    {
        uint32_t tp0 = task_of(ws, g);
        tick(ctx, s, 0, 0, 0);
        CHECK(task_of(ws, g) == tp0 && tp0 != 0, "the echo of Sonic's own bits: his group not re-made");
    }
    /* (3b) a second line; bit 0x02 (another world's ring) arrives while Sonic's ring sparkles */
    {
        uint32_t g2 = rec(ctx, SA1_OBJTYPE_O_GRING, 260.0f, 6.0f, 0.0f, 7, 20, 0), sp = 0, mine;
        int32_t r0;
        tick(ctx, s, 0, 0, 0);
        r0 = rings_of(ws);
        for (i = 0; i < 80 && !sp; i++) {
            tick(ctx, s, 0.0f, 1.0f, 0);
            sp = sparkling_child(ws, task_of(ws, g2));
        }
        mine = sp ? rd8(ws, rd32(ws, sp + 0x20u) + 0x0Cu) : 0u;
        sa1_set_state(ctx, g2, SA1_SET_STATE_PRESENT, mine == 0x02u ? 0x04u : 0x02u);
        tick(ctx, s, 0, 0, 0);
        ub = bits_of(ws, g2);
        run(ctx, s, 14, 0, 0, 0);
        n_after = children_of(ws, task_of(ws, g2), &alive);
        CHECK(sp && !(ub & (mine == 0x02u ? 0x04u : 0x02u)) && bits_of(ws, g2) == (mine | (mine == 0x02u ? 0x04u : 0x02u)) &&
                  n_after == 6 && rings_of(ws) - r0 >= 1,
              "(3b) a forced bit while the local ring (bit %02X) sparkles: deferred (scUserFlag %02X right after), then "
              "%02X, the group re-made with %u rings, rings +%d (the local ring stays gone)", (unsigned)mine, (unsigned)ub,
              (unsigned)bits_of(ws, g2), (unsigned)n_after, (int)(rings_of(ws) - r0));
    }
    /* (4) a circle of 8: the 8th ring has no bit (Sint8 scUserFlag); re-made with 0x7E -> 2 rings, 0xFE -> 0 */
    {
        uint32_t c = rec(ctx, SA1_OBJTYPE_O_GRING, -200.0f, 6.0f, 300.0f, 8, 20, 1), tp;
        run(ctx, t, 2, 0, 0, 0);
        tp = task_of(wt, c);
        n_before = children_of(wt, tp, NULL);
        sa1_set_state(ctx, c, SA1_SET_STATE_PRESENT, 0x7Eu);
        run(ctx, t, 3, 0, 0, 0);
        n_after = children_of(wt, task_of(wt, c), NULL);
        CHECK(n_before == 8 && n_after == 2, "(4) a circle (8, 20, 1): %u rings; bits 0x7E -> %u rings (bit 7 unset, ring "
              "8 has none)", (unsigned)n_before, (unsigned)n_after);
        sa1_set_state(ctx, c, SA1_SET_STATE_PRESENT, 0xFEu);
        run(ctx, t, 3, 0, 0, 0);
        n_after = children_of(wt, task_of(wt, c), NULL);
        CHECK(n_after == 0, "(4) bits 0xFE -> %u rings (the Sint8 0xFE sign-extends over ring 8's bit 0x100)",
              (unsigned)n_after);
    }
    /* the rotation: a line turned 90 degrees about the up axis (the SET entry's ang.y = 0x4000: njRotateY in the group's
     * placement 8C087EEE) lies along +X instead of +Z */
    {
        uint32_t rl = rec_ex(ctx, SA1_OBJTYPE_O_GRING, -200.0f, 6.0f, -300.0f, 3, 20, 0, 0, 90.0f, NULL), tp, ct, n = 0,
                 along = 0;
        float xmin = 1e9f, xmax = -1e9f;
        run(ctx, t, 2, 0, 0, 0);
        tp = task_of(wt, rl);
        for (ct = arena(tp) ? rd32(wt, tp + 0x0Cu) : 0u; arena(ct) && n < 16; ct = rd32(wt, ct), n++) {
            uint32_t ctw = rd32(wt, ct + 0x20u);
            float x = rdf(wt, ctw + 0x20u), z = rdf(wt, ctw + 0x28u);
            along += fabsf(z + 300.0f) < 0.5f;
            xmin = x < xmin ? x : xmin;
            xmax = x > xmax ? x : xmax;
        }
        CHECK(n == 4 && along == 4 && xmax - xmin > 50.0f, "a line (3, 20, 0) turned 90 degrees: %u rings, %u on the line "
              "z = -300, spread along x %.1f units (the host rotation into the SET entry's BAMS: ang.y 0x4000)", (unsigned)n,
              (unsigned)along, (double)(xmax - xmin));
    }
    /* (4) Sonic runs through a circle of 8: rings taken against the bits recorded */
    {
        float sp0[3];
        uint32_t c2, took8 = 0, k, b, nb = 0;
        ppos(ws, sp0);
        c2 = rec(ctx, SA1_OBJTYPE_O_GRING, sp0[0] + 120.0f, 6.0f, sp0[2] + 0.1035f * 0.0f - 0.1035f * 120.0f, 8, 20, 1);
        int32_t r0;
        tick(ctx, s, 0, 0, 0);
        r0 = rings_of(ws);
        for (i = 0; i < 60; i++) {
            uint32_t tp = task_of(ws, c2), ct;
            tick(ctx, s, 0.0f, 1.0f, 0);
            for (ct = arena(tp) ? rd32(ws, tp + 0x0Cu) : 0u, k = 0; arena(ct) && k < 16; ct = rd32(ws, ct), k++) {
                uint32_t ctw = rd32(ws, ct + 0x20u);
                if (arena(ctw) && rd8(ws, ctw) >= 2u && rd8(ws, ctw + 0x0Cu) == 0u) {
                    took8 = 1;
                }
            }
        }
        run(ctx, s, 12, 0, 0, 0);
        b = bits_of(ws, c2);
        for (k = 0; k < 8; k++) {
            nb += (b >> k) & 1u;
        }
        CHECK((int32_t)nb + (int32_t)took8 == rings_of(ws) - r0,
              "(4) Sonic through a circle of 8: rings +%d, %u bits recorded (%02X), ring 8 taken: %s (the game's quirk: "
              "ring 8 counts no bit)", (int)(rings_of(ws) - r0), (unsigned)nb, (unsigned)b, took8 ? "yes" : "no");
    }
    clear_recs(ctx);
    sa1_character_destroy(ctx, s);
    sa1_character_destroy(ctx, t);
}


/* ---- (12) the ground box, per character ----------------------------------------------------------------------------- */
/* player 0 moved to (x, y, z) between ticks with no speed (a test's teleport) */
static void tele(sa1g_world *w, float x, float y, float z)
{
    uint32_t t = twp0(w), m = rd32(w, A_PLAYER_MWP), p = rd32(w, A_PLAYER_PWP), k;
    if (!t) {
        return;
    }
    wrf(w, t + 0x20u, x);
    wrf(w, t + 0x24u, y);
    wrf(w, t + 0x28u, z);
    for (k = 0; k < 3; k++) {
        if (arena(m)) {
            wrf(w, m + 0x04u + 4u * k, 0.0f);
        }
        if (arena(p)) {
            wrf(w, p + 0x38u + 4u * k, 0.0f);
        }
    }
}

static float pspeed_y(const sa1g_world *w)
{
    uint32_t m = rd32(w, A_PLAYER_MWP);
    return arena(m) ? rdf(w, m + 0x08u) : 0.0f;
}

enum { GB_WALK, GB_LID, GB_ROLL, GB_SPINDASH, GB_BALL, GB_PUNCH, GB_LOCKFIRE, GB_STAND, GB_RUNINTO, GB_ACTION };

typedef struct gb_res {
    uint32_t broken, gone, cond_before, cond_after, cond_fire, shell, faults;
    int32_t rings;
    uint32_t cause;
    float touch_d, att_d, bounce;
    int32_t step_hit;
} gb_res;

/* one ground box (contents 2: five rings) and one character doing `how`; d = the start distance along +X */
static void gb_run(sa1_context *ctx, int32_t kind, int general, int how, float d, gb_res *r)
{
    float bp[3] = { 0.0f, 0.0f, 0.0f }, p[3];
    sa1_character_id id;
    sa1g_world *w;
    uint32_t box, i, tp, last = 0;
    int32_t r0;
    const sa1_set_event *e = NULL;
    memset(r, 0, sizeof *r);
    r->step_hit = -1;
    clear_recs(ctx);
    log_reset();
    id = mk(ctx, kind, -d, 0.5f, 0.0f, 10, general);
    if (!id) {
        r->faults = 1;
        return;
    }
    w = sa1_character_world(ctx, id);
    if (how == GB_ROLL || how == GB_RUNINTO || how == GB_WALK || how == GB_BALL) {
        bp[2] = lane_z(d);              /* on the character's run line (it drifts to -z) */
    }
    box = rec(ctx, SA1_OBJTYPE_ITEMBOX, bp[0], bp[1], bp[2], 2, 0, 0);
    r0 = rings_of(w);
    run(ctx, id, 2, 0, 0, 0);
    r->cond_before = cond_of(w, box);   /* spawned, still growing (46 steps without collision) */
    run(ctx, id, 28, 0, 0, 0);          /* grown; Super Sonic turned Super */
    for (i = 0; i < 140; i++) {
        float sy = 0.0f;
        uint32_t b = 0;
        switch (how) {
        case GB_WALK:
        case GB_RUNINTO:
            sy = r->broken ? 0.0f : 1.0f;
            break;
        case GB_LID:
            if (i == 0) {
                tele(w, bp[0], 30.0f, bp[2]);
            }
            break;
        case GB_ROLL:
            ppos(w, p);
            sy = r->broken ? 0.0f : 1.0f;
            b = (bp[0] - p[0] < 28.0f && bp[0] - p[0] > 22.0f) ? SA1_BUTTON_ACTION : 0u;
            break;
        case GB_SPINDASH:
            sy = i < 25 ? 1.0f : 0.0f;
            b = i >= 30 && i < 60 ? SA1_BUTTON_ACTION : 0u;
            break;
        case GB_BALL:
            sy = r->broken ? 0.0f : 1.0f;
            b = i >= 3 && i < 13 ? SA1_BUTTON_JUMP : 0u;
            break;
        case GB_PUNCH:
        case GB_ACTION:
            b = i == 2 || i == 20 ? SA1_BUTTON_ACTION : 0u;
            break;
        case GB_LOCKFIRE:
            b = i >= 5 && i < 40 ? SA1_BUTTON_ACTION : 0u;
            break;
        default:
            break;
        }
        if (tick(ctx, id, 0.0f, sy, b) != 0) {
            r->faults++;
            break;
        }
        {
            uint32_t c = cond_of(w, box);
            if (c != last) {
                if (g_verbose) {
                    ppos(w, p);
                    printf("    pair %3u: condition %04X, box mode %u, player (%.1f %.1f %.1f)\n", (unsigned)i,
                           (unsigned)c, (unsigned)task_mode(w, task_of(w, box)), (double)p[0], (double)p[1], (double)p[2]);
                }
                if (c == 0x0010u) {
                    r->cond_fire = c;
                }
                last = c;
            }
        }
        if (r->broken == 0 && n_set(SA1_SET_EVENT_BROKEN, box, id, NULL) != 0) {
            r->broken = 1;
            r->step_hit = (int32_t)i;
        }
        if (r->broken && (int32_t)i <= r->step_hit + 3) {
            float vy = pspeed_y(w);
            r->bounce = vy > r->bounce ? vy : r->bounce;
        }
    }
    r->broken = n_set(SA1_SET_EVENT_BROKEN, box, id, &e);
    r->gone = n_set(SA1_SET_EVENT_GONE, box, id, NULL);
    r->cond_after = cond_of(w, box);
    r->rings = rings_of(w) - r0;
    tp = task_of(w, box);
    if (tp) {
        uint32_t twp = rd32(w, tp + 0x20u), cwp = arena(twp) ? rd32(w, twp + 0x38u) : 0u;
        r->shell = task_mode(w, tp) >= 4u && arena(cwp) && rd16(w, cwp) == 4u;
    }
    if (e != NULL) {
        r->cause = e->cause;
        r->touch_d = dist3(e->position, bp);
        r->att_d = dist3(e->attacker, bp);
    }
    r->faults += n_faults();
    clear_recs(ctx);
    sa1_character_destroy(ctx, id);
}

static void gb_report(const char *what, const gb_res *r)
{
    printf("    %-34s broken %u gone %u, condition %04X -> %04X%s, shell %u, rings %+d, cause %u, touch %.1f (%.0f HU), "
           "attacker %.1f (%.0f HU) from the box, bounce %.2f, pair %d, faults %u\n", what, (unsigned)r->broken,
           (unsigned)r->gone, (unsigned)(r->cond_before & 0xFFFFu), (unsigned)(r->cond_after & 0xFFFFu),
           r->cond_fire ? " (0010 at the fire)" : "", (unsigned)r->shell, (int)r->rings, (unsigned)r->cause,
           (double)r->touch_d, (double)(r->touch_d * 4.0f), (double)r->att_d, (double)(r->att_d * 4.0f), (double)r->bounce,
           (int)r->step_hit, (unsigned)r->faults);
}

/* a normal break: one BROKEN, 0x8011 -> 0x8015, the shell, +5 rings, a body touch within 80 HU of the box */
static int gb_ok(const gb_res *r, uint32_t cause)
{
    return r->broken == 1 && r->gone == 0 && r->cond_before == 0x8011u && r->cond_after == 0x8015u && r->shell &&
           r->rings == 5 && r->cause == cause && r->faults == 0 &&
           (cause == SA1_SET_CAUSE_PROJECTILE ? r->att_d <= 20.0f : r->touch_d <= 20.0f);
}

static void t_groundbox(sa1_context *ctx)
{
    gb_res r;
    printf("groundbox (12): a ground box (5 rings) and each character's ways to break it\n");
    gb_run(ctx, SA1_CHAR_SONIC, 0, GB_LID, 40.0f, &r);
    gb_report("Sonic lands on the lid", &r);
    CHECK(gb_ok(&r, SA1_SET_CAUSE_BODY) && r.bounce >= 1.5f, "Sonic lands on the lid: one BROKEN, 8011 -> 8015, the shell "
          "(CCL id 4), rings +5, BODY within 80 HU, bounced (speed y %.2f)", (double)r.bounce);
    gb_run(ctx, SA1_CHAR_SONIC, 0, GB_WALK, 30.0f, &r);
    gb_report("Sonic walks into it", &r);
    CHECK(r.broken == 0 && r.cond_after == 0x8011u && r.rings == 0 && r.faults == 0,
          "Sonic walking into it: blocked, no break (condition %04X)", (unsigned)r.cond_after);
    gb_run(ctx, SA1_CHAR_SONIC, 0, GB_ROLL, 120.0f, &r);
    gb_report("Sonic's roll", &r);
    CHECK(gb_ok(&r, SA1_SET_CAUSE_BODY), "Sonic's roll breaks it");
    gb_run(ctx, SA1_CHAR_SONIC, 0, GB_SPINDASH, 30.0f, &r);
    gb_report("Sonic's spin dash in contact", &r);
    CHECK(gb_ok(&r, SA1_SET_CAUSE_BODY), "Sonic's spin dash (charged in contact) breaks it");
    gb_run(ctx, SA1_CHAR_TAILS, 0, GB_BALL, 30.0f, &r);
    gb_report("Tails' jump ball", &r);
    CHECK(gb_ok(&r, SA1_SET_CAUSE_BODY), "Tails' jump ball breaks it");
    gb_run(ctx, SA1_CHAR_KNUCKLES, 0, GB_PUNCH, 9.6f, &r);
    gb_report("Knuckles' punch", &r);
    CHECK(gb_ok(&r, SA1_SET_CAUSE_BODY), "Knuckles' punch breaks it");
    gb_run(ctx, SA1_CHAR_AMY, 0, GB_LID, 40.0f, &r);
    gb_report("Amy lands on the lid", &r);
    CHECK(gb_ok(&r, SA1_SET_CAUSE_BODY), "Amy lands on the lid: breaks it");
    gb_run(ctx, SA1_CHAR_GAMMA, 0, GB_LOCKFIRE, 40.0f, &r);
    gb_report("E-102 locks on and fires", &r);
    CHECK(r.broken == 1 && r.gone == 0 && r.cond_before == 0x8011u && r.cond_fire == 0x0010u && r.cond_after == 0x0014u &&
              r.shell && r.rings == 5 && r.cause == SA1_SET_CAUSE_PROJECTILE && r.att_d <= 20.0f && r.faults == 0,
          "E-102's lock-on: 8011 -> 0010 at the fire, then 0014, one BROKEN, no GONE, rings +5, PROJECTILE with the "
          "attacker %.1f units (%.0f HU) from the box", (double)r.att_d, (double)(r.att_d * 4.0f));
    gb_run(ctx, SA1_CHAR_BIG, 0, GB_STAND, 14.0f, &r);
    gb_report("Big stands next to it", &r);
    CHECK(gb_ok(&r, SA1_SET_CAUSE_BODY), "Big standing next to it breaks it (touch %.0f HU)", (double)(r.touch_d * 4.0f));
    gb_run(ctx, SA1_CHAR_SUPER_SONIC, 0, GB_RUNINTO, 40.0f, &r);
    gb_report("Super Sonic (Vs. Perfect Chaos)", &r);
    CHECK(gb_ok(&r, SA1_SET_CAUSE_BODY), "Super Sonic (kind 8) runs into it: breaks it");
    gb_run(ctx, SA1_CHAR_SONIC, 1, GB_RUNINTO, 40.0f, &r);
    gb_report("Super Sonic (General Gameplay)", &r);
    CHECK(gb_ok(&r, SA1_SET_CAUSE_BODY), "the General Super Sonic runs into it: breaks it");
    /* measured, no pass / fail (review R.6): Tails' tail attack and Amy's hammer, adjacent and facing */
    gb_run(ctx, SA1_CHAR_TAILS, 0, GB_ACTION, 9.0f, &r);
    gb_report("Tails' tail attack (adjacent)", &r);
    NOTE("Tails' tail attack, adjacent (9 units) and facing: %s (pair %d)", r.broken ? "BREAKS the ground box" :
         "no break", (int)r.step_hit);
    gb_run(ctx, SA1_CHAR_AMY, 0, GB_ACTION, 9.0f, &r);
    gb_report("Amy's hammer (adjacent)", &r);
    NOTE("Amy's hammer, adjacent (9 units) and facing: %s (pair %d)", r.broken ? "BREAKS the ground box" : "no break",
         (int)r.step_hit);
}

/* ---- (13) the air box, per character; E-102's lock-on -------------------------------------------------------------- */
typedef struct call_task {
    uint32_t tp;
} call_task;

static void b8a_cb(sa1g_world *w, void *user)
{
    /* 8C091B8A: what E-102's firing does to every box he locked on (ssCondition &= ~0x8001; the task lives on) */
    (void)w;
    ((void (*)(void *))sa1_fn(SA1_GFN_C(0x8C091B8Au), 0u))(sa1_decode(((call_task *)user)->tp));
}

static void t_airbox(sa1_context *ctx)
{
    static const int32_t kinds[6] = { SA1_CHAR_SONIC, SA1_CHAR_TAILS, SA1_CHAR_KNUCKLES, SA1_CHAR_AMY, SA1_CHAR_GAMMA,
                                      SA1_CHAR_BIG };
    float bp[3] = { 12.0f, 0.0f, 0.0f };
    uint32_t i, k;
    printf("airbox (13): an air box (5 rings) per character; E-102's lock-on, a missed shot, a box freed under his lock\n");
    for (k = 0; k < 6; k++) {
        sa1_character_id id;
        sa1g_world *w;
        uint32_t box, ok_gone, dstep = 0;
        int32_t r0;
        const sa1_set_event *e = NULL;
        clear_recs(ctx);
        log_reset();
        id = mk(ctx, kinds[k], 0, 0.5f, 0, 10, 0);
        if (!id) {
            g_fail++;
            continue;
        }
        w = sa1_character_world(ctx, id);
        r0 = rings_of(w);
        box = rec(ctx, SA1_OBJTYPE_ITEMBOX_AIR, bp[0], bp[1], bp[2], 2, 0, 0);
        for (i = 0; i < 90; i++) {
            tick(ctx, id, 0.0f, i < 30 ? 1.0f : 0.0f, 0);
        }
        ok_gone = n_set(SA1_SET_EVENT_GONE, box, id, &e);
        if (e != NULL) {
            dstep = e->step - e->touch_step;
        }
        CHECK(ok_gone == 1 && e != NULL && e->condition == 0x0010u && rings_of(w) - r0 == 5 && e->cause == SA1_SET_CAUSE_BODY &&
                  dist3(e->position, bp) <= 20.0f && dstep >= 20 && dstep <= 26 && n_faults() == 0,
              "%s walks into it: one GONE (condition %04X), %u steps after the touch, rings %+d, touch %.1f units (%.0f HU) "
              "from the box", k_kinds[kinds[k]], e ? (unsigned)e->condition : 0u, (unsigned)dstep, (int)(rings_of(w) - r0),
              e ? (double)dist3(e->position, bp) : -1.0, e ? (double)(dist3(e->position, bp) * 4.0f) : -1.0);
        sa1_character_destroy(ctx, id);
    }
    /* E-102: lock on (B held), fire (release): no event at the fire, one GONE when the task ends after the hit */
    {
        sa1_character_id id;
        sa1g_world *w;
        uint32_t box, fire_pair = 0, gone_pair = 0, ev_at_fire = 0, c;
        const sa1_set_event *e = NULL;
        float ap[3] = { 40.0f, 6.0f, 0.0f };
        clear_recs(ctx);
        log_reset();
        id = mk(ctx, SA1_CHAR_GAMMA, 0, 0.5f, 0, 10, 0);
        w = sa1_character_world(ctx, id);
        box = rec(ctx, SA1_OBJTYPE_ITEMBOX_AIR, ap[0], ap[1], ap[2], 3, 0, 0);
        run(ctx, id, 30, 0, 0, 0);
        for (i = 0; i < 120; i++) {
            uint32_t before = g_log.ns;
            tick(ctx, id, 0.0f, 0.0f, i >= 5 && i < 40 ? SA1_BUTTON_ACTION : 0u);
            c = cond_of(w, box);
            if (!fire_pair && c == 0x0010u && task_of(w, box) != 0) {
                fire_pair = i;
                ev_at_fire = g_log.ns - before;
            }
            if (!gone_pair && n_set(SA1_SET_EVENT_GONE, box, id, NULL)) {
                gone_pair = i;
            }
        }
        n_set(SA1_SET_EVENT_GONE, box, id, &e);
        CHECK(fire_pair && ev_at_fire == 0 && gone_pair > fire_pair && n_set(SA1_SET_EVENT_GONE, box, id, NULL) == 1 &&
                  e != NULL && e->cause == SA1_SET_CAUSE_PROJECTILE && dist3(e->attacker, ap) <= 20.0f && n_faults() == 0,
              "E-102's lock-on: 8011 -> 0010 at pair %u with the box alive and no event; one GONE at pair %u, PROJECTILE, the "
              "attacker %.1f units from the box", (unsigned)fire_pair, (unsigned)gone_pair,
              e ? (double)dist3(e->attacker, ap) : -1.0);
        sa1_character_destroy(ctx, id);
    }
    /* a missed shot (the box lives with 0x8001 cleared: 8C091B8A), then the record leaves the window and comes back */
    {
        sa1_character_id id;
        sa1g_world *w;
        uint32_t box, c1, c2;
        call_task ct;
        clear_recs(ctx);
        log_reset();
        id = mk(ctx, SA1_CHAR_GAMMA, 0, 0.5f, 0, 10, 0);
        w = sa1_character_world(ctx, id);
        box = rec(ctx, SA1_OBJTYPE_ITEMBOX_AIR, 40.0f, 6.0f, 0.0f, 3, 0, 0);
        run(ctx, id, 30, 0, 0, 0);
        ct.tp = task_of(w, box);
        sa1g_world_call(w, b8a_cb, &ct);
        run(ctx, id, 5, 0, 0, 0);
        c1 = cond_of(w, box);
        move_rec(ctx, box, SA1_OBJTYPE_ITEMBOX_AIR, 1500.0f, 6.0f, 0.0f, 3, 0, 0, 0);
        run(ctx, id, 4, 0, 0, 0);
        move_rec(ctx, box, SA1_OBJTYPE_ITEMBOX_AIR, 40.0f, 6.0f, 0.0f, 3, 0, 0, 0);
        run(ctx, id, 4, 0, 0, 0);
        c2 = cond_of(w, box);
        CHECK(c1 == 0x0010u && c2 == 0x8011u && task_of(w, box) != 0 && n_set(SA1_SET_EVENT_GONE, box, id, NULL) == 0,
              "a missed shot leaves %04X with the box alive; out of the window and back: %04X, the box intact again, no "
              "phantom GONE", (unsigned)c1, (unsigned)c2);
        sa1_character_destroy(ctx, id);
    }
    /* E-102 locked on a box that libsa1 frees (the window, a forced state, an update), then fires: no fault, the slot
     * untouched */
    for (k = 0; k < 3; k++) {
        static const char *how[3] = { "leaves the window", "is forced absent", "is moved (update)" };
        sa1_character_id id;
        sa1g_world *w;
        uint32_t box, c_after;
        clear_recs(ctx);
        log_reset();
        id = mk(ctx, SA1_CHAR_GAMMA, 0, 0.5f, 0, 10, 0);
        w = sa1_character_world(ctx, id);
        box = rec(ctx, SA1_OBJTYPE_ITEMBOX_AIR, 40.0f, 6.0f, 0.0f, 3, 0, 0);
        run(ctx, id, 30, 0, 0, 0);
        run(ctx, id, 25, 0, 0, SA1_BUTTON_ACTION);  /* locked on */
        if (k == 0) {
            move_rec(ctx, box, SA1_OBJTYPE_ITEMBOX_AIR, 1500.0f, 6.0f, 0.0f, 3, 0, 0, 0);
        } else if (k == 1) {
            sa1_set_state(ctx, box, 0u, 0u);
        } else {
            move_rec(ctx, box, SA1_OBJTYPE_ITEMBOX_AIR, 40.0f, 6.0f, 60.0f, 3, 0, 0, 0);
        }
        run(ctx, id, 4, 0, 0, SA1_BUTTON_ACTION);
        run(ctx, id, 40, 0, 0, 0);                  /* released: he fires */
        c_after = cond_of(w, box);
        CHECK(n_faults() == 0 && g_faults == 0 && n_set(SA1_SET_EVENT_GONE, box, id, NULL) == 0 &&
                  (k == 0 ? c_after == 0xFFFFFFFFu : k == 1 ? c_after == 0x0000u || c_after == 0x0010u : c_after == 0x8011u),
              "E-102 locked on a box that %s, then fires: no fault, no GONE, the slot %04X", how[k],
              (unsigned)(c_after & 0xFFFFu));
        sa1_character_destroy(ctx, id);
    }
    clear_recs(ctx);
}

/* ---- (14) contents 0..8, both kinds -------------------------------------------------------------------------------------- */
static void t_contents(sa1_context *ctx)
{
    uint32_t kind, c;
    printf("contents (14): contents 0..8 in both kinds (Sonic), and E-102's laser on every content\n");
    for (kind = SA1_OBJTYPE_ITEMBOX; kind <= SA1_OBJTYPE_ITEMBOX_AIR; kind++) {
        for (c = 0; c <= 8; c++) {
            sa1_character_id id;
            sa1g_world *w;
            uint32_t box, ok, icons0, se11, pwp, item, lives0;
            int32_t r0, dr;
            float maxbomb = -1.0f;
            int i;
            clear_recs(ctx);
            log_reset();
            id = mk(ctx, SA1_CHAR_SONIC, 0, 0.5f, 0, 10, 0);
            w = sa1_character_world(ctx, id);
            r0 = rings_of(w);
            lives0 = rd8(w, A_LIVES);
            icons0 = rd32(w, A_ICONS + 4u);
            box = rec(ctx, kind, kind == SA1_OBJTYPE_ITEMBOX ? 12.0f : 30.0f, 0.0f, 0.0f, (float)c, 0, 0);
            run(ctx, id, 30, 0, 0, 0);
            if (kind == SA1_OBJTYPE_ITEMBOX) {
                tele(w, 12.0f, 30.0f, 0.0f);
            }
            for (i = 0; i < 30; i++) {
                tick(ctx, id, 0.0f, kind == SA1_OBJTYPE_ITEMBOX_AIR && i < 22 ? 1.0f : 0.0f, 0);
                if (c == 7) {
                    float br = rdf(w, A_BOMB_R);
                    maxbomb = br > maxbomb ? br : maxbomb;
                }
            }
            pwp = rd32(w, A_PLAYER_PWP);
            item = arena(pwp) ? rd16(w, pwp + 6u) : 0u;
            dr = rings_of(w) - r0;
            se11 = n_sound(SE_ITEM, id);
            switch (c) {
            case 0:
                ok = count_exec(w, X_SPEEDSHOES, NULL) + count_exec(w, X_SPEEDTIMER, NULL) == 1u;
                break;
            case 1:
                ok = (item & 0x8000u) != 0;
                break;
            case 2:
                ok = dr == 5;
                break;
            case 3:
                ok = dr == 10;
                break;
            case 4:
                ok = dr == 1 || dr == 5 || dr == 10 || dr == 15 || dr == 20 || dr == 40;
                break;
            case 5:
                ok = (item & 3u) == 1u;
                break;
            case 6:
                ok = rd8(w, A_LIVES) == lives0 + 1u && n_sound(SE_1UP, id) >= 1;
                break;
            case 7:
                ok = maxbomb > 10.0f;
                break;
            default:
                ok = (item & 3u) == 2u;
                break;
            }
            ok = ok && se11 >= 1 && rd32(w, A_ICONS + 4u) == icons0 + 1u && n_faults() == 0 &&
                 (kind == SA1_OBJTYPE_ITEMBOX ? n_set(SA1_SET_EVENT_BROKEN, box, id, NULL) == 1
                                              : n_set(SA1_SET_EVENT_GONE, box, id, NULL) == 1);
            CHECK(ok, "%s, contents %u (%s): rings %+d, item %04X, lives %+d, SE 11 x%u, icons %u -> %u%s", kind ==
                  SA1_OBJTYPE_ITEMBOX ? "ground box" : "air box", (unsigned)c, k_items[c], (int)dr, (unsigned)item,
                  (int)rd8(w, A_LIVES) - (int)lives0, (unsigned)se11, (unsigned)icons0, (unsigned)rd32(w, A_ICONS + 4u),
                  c == 7 ? " (the bomb sphere grew)" : "");
            sa1_character_destroy(ctx, id);
        }
    }
    /* E-102's laser on every content of both kinds: no fault (the speed shoes take the shot's counter.b[0]) */
    {
        uint32_t faults = 0, broke = 0;
        for (kind = SA1_OBJTYPE_ITEMBOX; kind <= SA1_OBJTYPE_ITEMBOX_AIR; kind++) {
            for (c = 0; c <= 8; c++) {
                sa1_character_id id;
                uint32_t box;
                int before = g_faults;
                clear_recs(ctx);
                log_reset();
                id = mk(ctx, SA1_CHAR_GAMMA, 0, 0.5f, 0, 10, 0);
                box = rec(ctx, kind, 40.0f, kind == SA1_OBJTYPE_ITEMBOX ? 0.0f : 6.0f, 0.0f, (float)c, 0, 0);
                run(ctx, id, 30, 0, 0, 0);
                run(ctx, id, 35, 0, 0, SA1_BUTTON_ACTION);
                run(ctx, id, 60, 0, 0, 0);
                faults += n_faults() + (uint32_t)(g_faults - before);
                broke += kind == SA1_OBJTYPE_ITEMBOX ? n_set(SA1_SET_EVENT_BROKEN, box, id, NULL) == 1
                                                      : n_set(SA1_SET_EVENT_GONE, box, id, NULL) == 1;
                sa1_character_destroy(ctx, id);
            }
        }
        CHECK(faults == 0 && broke == 18, "E-102's laser on contents 0..8 of both kinds: %u of 18 broken, %u faults",
              (unsigned)broke, (unsigned)faults);
    }
    clear_recs(ctx);
}

/* ---- (15) / (16) forced states of the boxes --------------------------------------------------------------------------- */
static void t_forced_boxes(sa1_context *ctx)
{
    sa1_character_id a, b;
    sa1g_world *wa, *wb;
    uint32_t box, icons0, tp;
    int32_t ra0, rb0;
    int i;
    printf("forced_boxes (15, 16): BROKEN forced into a second world, BROKEN 0, a change during mode 3, E-102's 0014; the "
           "air box forced absent and back\n");
    clear_recs(ctx);
    log_reset();
    a = mk(ctx, SA1_CHAR_SONIC, 0, 0.5f, 0, 10, 0);
    b = mk(ctx, SA1_CHAR_KNUCKLES, 2.4f, 0.5f, 0, 10, 0);   /* 9.6 units from the box: his punch reaches it */
    wa = sa1_character_world(ctx, a);
    wb = sa1_character_world(ctx, b);
    box = rec(ctx, SA1_OBJTYPE_ITEMBOX, 12.0f, 0.0f, 0.0f, 2, 0, 0);
    run(ctx, a, 30, 0, 0, 0);
    run(ctx, b, 30, 0, 0, 0);
    ra0 = rings_of(wa);
    rb0 = rings_of(wb);
    icons0 = rd32(wb, A_ICONS + 4u);
    /* Sonic breaks it (the lid); the host shares BROKEN 1: Knuckles' intact box becomes the shell silently */
    tele(wa, 12.0f, 30.0f, 0.0f);
    run(ctx, a, 30, 0, 0, 0);
    sa1_set_state(ctx, box, SA1_SET_STATE_PRESENT | SA1_SET_STATE_BROKEN, 0);
    run(ctx, b, 3, 0, 0, 0);
    tp = task_of(wb, box);
    CHECK(rings_of(wa) - ra0 == 5 && cond_of(wb, box) == 0x8015u && tp != 0 && task_mode(wb, tp) >= 4u &&
              rings_of(wb) == rb0 && n_sound(SE_ITEM, b) == 0 && rd32(wb, A_ICONS + 4u) == icons0 &&
              n_set(SA1_SET_EVENT_BROKEN, box, b, NULL) == 0,
          "(15) BROKEN forced into Knuckles' world: his box is the shell (condition %04X, mode %u), rings %+d, SE 11 x%u, "
          "icons %u -> %u, no BROKEN event there", (unsigned)(cond_of(wb, box) & 0xFFFFu), (unsigned)task_mode(wb, tp),
          (int)(rings_of(wb) - rb0), (unsigned)n_sound(SE_ITEM, b), (unsigned)icons0, (unsigned)rd32(wb, A_ICONS + 4u));
    /* BROKEN 0: the shell freed, an intact box grows in (mode 2 after 46 steps) and breaks again with its item */
    sa1_set_state(ctx, box, SA1_SET_STATE_PRESENT, 0);
    {
        int grown = -1;
        for (i = 0; i < 40; i++) {
            tick(ctx, b, 0, 0, 0);
            if (grown < 0 && task_mode(wb, task_of(wb, box)) == 2u) {
                grown = i;
            }
        }
        run(ctx, b, 2, 0, 0, SA1_BUTTON_ACTION);
        run(ctx, b, 20, 0, 0, 0);
        CHECK(grown >= 20 && grown <= 26 && cond_of(wb, box) == 0x8015u && rings_of(wb) - rb0 == 5 &&
                  n_set(SA1_SET_EVENT_BROKEN, box, b, NULL) == 1,
              "(15) BROKEN 0: an intact box grows in (mode 2 at pair %d), Knuckles' punch breaks it: rings %+d, one BROKEN",
              grown, (int)(rings_of(wb) - rb0));
    }
    /* a forced change while the local box is in mode 3: applied one step later */
    {
        uint32_t d0, twp;
        sa1set_world_info inf;
        sa1_set_state(ctx, box, SA1_SET_STATE_PRESENT, 0);
        run(ctx, b, 30, 0, 0, 0);                   /* intact and grown again */
        tp = task_of(wb, box);
        twp = rd32(wb, tp + 0x20u);
        info_of(wb, &inf);
        d0 = inf.deferred;
        wr8(wb, twp, 3u);                           /* the box breaking (its mode 3 step) ... */
        sa1_set_state(ctx, box, SA1_SET_STATE_PRESENT, 0);  /* ... and BROKEN 0 arrives for it */
        tick(ctx, b, 0, 0, 0);
        info_of(wb, &inf);
        CHECK(inf.deferred == d0 + 1u && !(cond_of(wb, box) & 4u) && n_faults() == 0,
              "(15) a forced write during mode 3 waits one step (deferred %u), then applies: condition %04X",
              (unsigned)(inf.deferred - d0), (unsigned)(cond_of(wb, box) & 0xFFFFu));
    }
    clear_recs(ctx);
    sa1_character_destroy(ctx, a);
    sa1_character_destroy(ctx, b);
    /* BROKEN 0 in E-102's world after his shot (0014): an intact box grows back */
    {
        sa1_character_id g = mk(ctx, SA1_CHAR_GAMMA, 0, 0.5f, 0, 10, 0);
        sa1g_world *wg = sa1_character_world(ctx, g);
        uint32_t c1;
        log_reset();
        box = rec(ctx, SA1_OBJTYPE_ITEMBOX, 40.0f, 0.0f, 0.0f, 2, 0, 0);
        run(ctx, g, 30, 0, 0, 0);
        run(ctx, g, 35, 0, 0, SA1_BUTTON_ACTION);
        run(ctx, g, 60, 0, 0, 0);
        c1 = cond_of(wg, box);
        sa1_set_state(ctx, box, SA1_SET_STATE_PRESENT, 0);
        run(ctx, g, 30, 0, 0, 0);
        CHECK(c1 == 0x0014u && cond_of(wg, box) == 0x8011u && task_mode(wg, task_of(wg, box)) == 2u,
              "(15, review R.10) E-102's shot left %04X; BROKEN 0: %04X, an intact box (mode %u)", (unsigned)c1,
              (unsigned)(cond_of(wg, box) & 0xFFFFu), (unsigned)task_mode(wg, task_of(wg, box)));
        clear_recs(ctx);
        sa1_character_destroy(ctx, g);
    }
    /* (16) the air box forced absent (silent, no GONE) and present again */
    {
        sa1_character_id s = mk(ctx, SA1_CHAR_SONIC, 0, 0.5f, 0, 10, 0);
        sa1g_world *ws = sa1_character_world(ctx, s);
        uint32_t ab, gone_tasks, back;
        log_reset();
        ab = rec(ctx, SA1_OBJTYPE_ITEMBOX_AIR, 60.0f, 6.0f, 0.0f, 3, 0, 0);
        run(ctx, s, 5, 0, 0, 0);
        sa1_set_state(ctx, ab, 0u, 0u);
        run(ctx, s, 3, 0, 0, 0);
        gone_tasks = count_exec(ws, X_ABOX, NULL);
        sa1_set_state(ctx, ab, SA1_SET_STATE_PRESENT, 0u);
        run(ctx, s, 3, 0, 0, 0);
        back = count_exec(ws, X_ABOX, NULL);
        CHECK(gone_tasks == 0 && back == 1 && n_set(SA1_SET_EVENT_GONE, ab, s, NULL) == 0 && n_sound(SE_ITEM, s) == 0 &&
                  cond_of(ws, ab) == 0x8011u,
              "(16) the air box forced absent: removed silently (tasks %u, no GONE, no SE 11); present again: back (tasks "
              "%u, condition %04X)", (unsigned)gone_tasks, (unsigned)back, (unsigned)(cond_of(ws, ab) & 0xFFFFu));
        clear_recs(ctx);
        sa1_character_destroy(ctx, s);
    }
}

/* ---- (7) rebuild: a world made again runs every record with the registry's state ------------------------------------ */
static void t_rebuild(sa1_context *ctx)
{
    sa1_character_id s;
    sa1g_world *w;
    uint32_t r1, r2, r3, box, tp;
    int32_t r0;
    printf("rebuild (7): a character re-created mid-run re-attaches every record with the registry's state\n");
    clear_recs(ctx);
    log_reset();
    s = mk(ctx, SA1_CHAR_SONIC, 0, 0.5f, 0, 0, 0);
    w = sa1_character_world(ctx, s);
    r1 = rec(ctx, SA1_OBJTYPE_RING, 40.0f, 6.0f, lane_z(40.0f), 0, 0, 0);
    r2 = rec(ctx, SA1_OBJTYPE_RING, 0.0f, 6.0f, 60.0f, 0, 0, 0);
    r3 = rec(ctx, SA1_OBJTYPE_RING, 0.0f, 6.0f, -60.0f, 0, 0, 0);
    box = rec(ctx, SA1_OBJTYPE_ITEMBOX, -40.0f, 0.0f, 0.0f, 3, 0, 0);
    run(ctx, s, 30, 0, 0, 0);
    run(ctx, s, 34, 0.0f, 1.0f, 0);     /* takes ring 1 */
    run(ctx, s, 10, 0, 0, 0);
    tele(w, -40.0f, 30.0f, 0.0f);       /* lands on the box */
    run(ctx, s, 20, 0, 0, 0);
    CHECK(n_set(SA1_SET_EVENT_GONE, r1, s, NULL) == 1 && n_set(SA1_SET_EVENT_BROKEN, box, s, NULL) == 1,
          "ring 1 taken (GONE), the box broken (BROKEN); the host records both");
    sa1_set_state(ctx, r1, 0u, 0u);
    sa1_set_state(ctx, box, SA1_SET_STATE_PRESENT | SA1_SET_STATE_BROKEN, 0u);
    sa1_character_destroy(ctx, s);
    log_reset();
    s = mk(ctx, SA1_CHAR_SONIC, 0, 0.5f, 0, 0, 0);
    w = sa1_character_world(ctx, s);
    r0 = rings_of(w);
    run(ctx, s, 30, 0, 0, 0);
    tp = task_of(w, box);
    CHECK(task_of(w, r1) == 0 && !(cond_of(w, r1) & 0x8000u) && task_of(w, r2) != 0 && task_of(w, r3) != 0 &&
              cond_of(w, box) == 0x8015u && tp != 0 && task_mode(w, tp) >= 4u && rings_of(w) == r0 &&
              n_sound(SE_ITEM, s) == 0 && n_set(SA1_SET_EVENT_BROKEN, box, s, NULL) == 0,
          "the new world: ring 1 absent (condition %04X), rings 2 and 3 there, the box the shell (condition %04X, mode %u), "
          "no item, no SE 11, no event", (unsigned)(cond_of(w, r1) & 0xFFFFu), (unsigned)(cond_of(w, box) & 0xFFFFu),
          (unsigned)task_mode(w, tp));
    clear_recs(ctx);
    sa1_character_destroy(ctx, s);
}

/* ---- (17) slots: 200 cycles of air boxes broken, removed and re-added ------------------------------------------------- */
static void t_slots(sa1_context *ctx)
{
    sa1_character_id s;
    sa1g_world *w;
    uint32_t cyc, gone = 0, phantom = 0, reuse_bad = 0, max_count = 0, early_remove = 0;
    sa1set_world_info inf;
    printf("slots (17): 200 cycles of air boxes broken, removed and re-added\n");
    clear_recs(ctx);
    log_reset();
    s = mk(ctx, SA1_CHAR_SONIC, 0, 0.5f, 0, 0, 0);
    w = sa1_character_world(ctx, s);
    /* a box every 26 pairs never lets the item icon list drain (134 steps after the last icon): the guard keeps the
     * game's memory safe (case iconguard) */
    sa1_character_set_game_option(ctx, s, SA1_GOPT_ITEM_ICON_LIMIT, 1);
    run(ctx, s, 2, 0, 0, 0);
    for (cyc = 0; cyc < 200; cyc++) {
        uint32_t id = rec(ctx, SA1_OBJTYPE_ITEMBOX_AIR, 0.0f, 0.0f, 0.0f, 2, 0, 0), i, k, old_slot_task = 0;
        int32_t slot;
        run(ctx, s, 12, 0, 0, 0);       /* the box opens (20 steps) and breaks on Sonic */
        slot = sa1w_set_world_slot(w, id);
        if (cyc % 3 == 0) {
            /* removed while it breaks (mode 2-4): the slot waits for the game's end */
            old_slot_task = task_of(w, id);
            sa1_set_remove(ctx, id);
            early_remove += old_slot_task != 0;
            /* a new record must not take that slot while the task points at it */
            {
                uint32_t id2 = rec(ctx, SA1_OBJTYPE_RING, 0.0f, 6.0f, 300.0f, 0, 0, 0);
                tick(ctx, s, 0, 0, 0);
                if (old_slot_task != 0 && slot >= 0 && sa1w_set_world_slot(w, id2) == slot &&
                    rd32(w, old_slot_task + 0x1Cu) == A_COND + 0x10u * (uint32_t)slot) {
                    reuse_bad++;
                }
                run(ctx, s, 14, 0, 0, 0);
                sa1_set_remove(ctx, id2);
            }
        } else {
            run(ctx, s, 14, 0, 0, 0);
            sa1_set_remove(ctx, id);
        }
        tick(ctx, s, 0, 0, 0);
        k = rd16(w, A_COUNT);
        max_count = k > max_count ? k : max_count;
        (void)i;
        g_nrecs = 0;
    }
    run(ctx, s, 30, 0, 0, 0);
    gone = n_set(SA1_SET_EVENT_GONE, 0, s, NULL);
    phantom = gone > 200 ? gone - 200 : 0;
    info_of(w, &inf);
    CHECK(gone == 200 && phantom == 0 && reuse_bad == 0 && inf.slots == 0 && rd16(w, A_COUNT) == 0 && max_count <= 3 &&
              rings_of(w) == 1000 && n_faults() == 0,
          "200 cycles (%u removed mid-break): %u GONE (no phantom), no slot taken while a task points at it (%u), every "
          "slot released (%u left, B_8C79A3DC %u, at most %u), rings %d", (unsigned)early_remove, (unsigned)gone,
          (unsigned)reuse_bad, (unsigned)inf.slots, (unsigned)rd16(w, A_COUNT), (unsigned)max_count, (int)rings_of(w));
    clear_recs(ctx);
    sa1_character_destroy(ctx, s);
}

/* ---- (18) the box window ------------------------------------------------------------------------------------------------ */
static void t_boxwindow(sa1_context *ctx)
{
    static const uint32_t wins[2] = { 16u, SA1_SET_BOX_WINDOW_MAX };
    uint32_t wi;
    printf("boxwindow (18): 200 ground + 200 air box records around Sonic\n");
    {
        sa1_set_config c;
        memset(&c, 0, sizeof c);
        c.struct_size = sizeof c;
        c.ring_window = SA1_SET_RING_WINDOW_MAX + 1u;
        c.box_window = 32;
        c.margin = 100.0f;
        CHECK(sa1_set_configure(ctx, &c) == SA1_ERR_INVALID_ARGUMENT, "ring_window %u refused (maximum %u)",
              (unsigned)c.ring_window, (unsigned)SA1_SET_RING_WINDOW_MAX);
        c.ring_window = 64;
        c.box_window = SA1_SET_BOX_WINDOW_MAX + 1u;
        CHECK(sa1_set_configure(ctx, &c) == SA1_ERR_INVALID_ARGUMENT, "box_window %u refused (maximum %u)",
              (unsigned)c.box_window, (unsigned)SA1_SET_BOX_WINDOW_MAX);
    }
    for (wi = 0; wi < 2; wi++) {
        sa1_character_id s;
        sa1g_world *w;
        sa1_set_config c;
        uint32_t i, maxlive = 0, maxccl2 = 0, heap0 = 0, heap1 = 0, l0, l1, live;
        sa1set_world_info inf;
        clear_recs(ctx);
        log_reset();
        memset(&c, 0, sizeof c);
        c.struct_size = sizeof c;
        c.ring_window = 64;
        c.box_window = wins[wi];
        c.margin = 100.0f;
        sa1_set_configure(ctx, &c);
        s = mk(ctx, SA1_CHAR_SONIC, 0, 0.5f, 0, 0, 0);
        w = sa1_character_world(ctx, s);
        rec(ctx, SA1_OBJTYPE_RING, 0.0f, 6.0f, 2000.0f, 0, 0, 0);    /* the manager, without boxes */
        run(ctx, s, 3, 0, 0, 0);
        heap_of(w, &heap0, &l0);
        for (i = 0; i < 400; i++) {
            float a = (float)i * 0.0157f * 4.0f, rr = 40.0f + (float)(i % 50) * 6.0f;
            rec(ctx, i < 200 ? SA1_OBJTYPE_ITEMBOX : SA1_OBJTYPE_ITEMBOX_AIR, rr * cosf(a), i < 200 ? 0.0f : 6.0f,
                rr * sinf(a), 3, 0, 0);
        }
        for (i = 0; i < 40; i++) {
            tick(ctx, s, 0, 0, 0);
            info_of(w, &inf);
            maxlive = inf.live_boxes > maxlive ? inf.live_boxes : maxlive;
            live = count_ccl(w, 2u);
            maxccl2 = live > maxccl2 ? live : maxccl2;
        }
        heap_of(w, &heap1, &l1);
        info_of(w, &inf);
        CHECK(maxlive <= wins[wi] && inf.live_boxes == wins[wi] && maxccl2 <= 128u && n_faults() == 0 &&
                  n_capacity(SA1_CAPACITY_SET_WINDOW, s) == 1,
              "box_window %u: live boxes at most %u (now %u), CCL list 2 at most %u (128), one SET_WINDOW capacity event",
              (unsigned)wins[wi], (unsigned)maxlive, (unsigned)inf.live_boxes, (unsigned)maxccl2);
        NOTE("heap: %u B free before, %u after %u live boxes: %u B per box (largest block %u -> %u)", (unsigned)heap0,
             (unsigned)heap1, (unsigned)inf.live_boxes, inf.live_boxes ? (unsigned)((heap0 - heap1) / inf.live_boxes) : 0u,
             (unsigned)l0, (unsigned)l1);
        clear_recs(ctx);
        sa1_character_destroy(ctx, s);
    }
    {
        sa1_set_config c;
        memset(&c, 0, sizeof c);
        c.struct_size = sizeof c;
        c.ring_window = 64;
        c.box_window = 32;
        c.margin = 100.0f;
        sa1_set_configure(ctx, &c);
    }
    /* the heap per box of each kind and form (task 6) */
    {
        static const struct { uint32_t type; const char *what; int shell; } kinds[3] = {
            { SA1_OBJTYPE_ITEMBOX, "ground box, intact", 0 }, { SA1_OBJTYPE_ITEMBOX, "ground box, broken (shell)", 1 },
            { SA1_OBJTYPE_ITEMBOX_AIR, "air box", 0 } };
        uint32_t k, i;
        for (k = 0; k < 3; k++) {
            sa1_character_id s = mk(ctx, SA1_CHAR_SONIC, 0, 0.5f, 0, 0, 0);
            sa1g_world *w = sa1_character_world(ctx, s);
            uint32_t h0, h1, l, ids[20], ccl0, ccl1;
            rec(ctx, SA1_OBJTYPE_RING, 0.0f, 6.0f, 2000.0f, 0, 0, 0);
            run(ctx, s, 3, 0, 0, 0);
            heap_of(w, &h0, &l);
            ccl0 = count_ccl(w, 2u) + count_ccl(w, 4u);
            for (i = 0; i < 20; i++) {
                ids[i] = rec(ctx, kinds[k].type, 100.0f + 15.0f * (float)i, kinds[k].type == SA1_OBJTYPE_ITEMBOX ? 0.0f : 6.0f,
                             80.0f, 3, 0, 0);
                if (kinds[k].shell) {
                    sa1_set_state(ctx, ids[i], SA1_SET_STATE_PRESENT | SA1_SET_STATE_BROKEN, 0);
                }
            }
            run(ctx, s, 30, 0, 0, 0);
            heap_of(w, &h1, &l);
            ccl1 = count_ccl(w, 2u) + count_ccl(w, 4u);
            NOTE("%s: %u B of game heap per box (20 boxes: %u -> %u), %u CCL entries per box (list %u)", kinds[k].what,
                 (unsigned)((h0 - h1) / 20u), (unsigned)h0, (unsigned)h1, (unsigned)((ccl1 - ccl0) / 20u),
                 kinds[k].shell ? 4u : 2u);
            clear_recs(ctx);
            sa1_character_destroy(ctx, s);
        }
    }
}

/* ---- (19) the item icon list guard ------------------------------------------------------------------------------------ */
static void t_iconguard(sa1_context *ctx)
{
    uint32_t pass;
    sa1_set_config c;
    printf("iconguard (19): the item icon list (20 entries) with SA1_GOPT_ITEM_ICON_LIMIT, and the game's overflow without "
           "it\n");
    memset(&c, 0, sizeof c);
    c.struct_size = sizeof c;
    c.ring_window = 64;
    c.box_window = SA1_SET_BOX_WINDOW_MAX;
    c.margin = 100.0f;
    sa1_set_configure(ctx, &c);
    for (pass = 0; pass < 3; pass++) {
        sa1_character_id s;
        sa1g_world *w;
        uint32_t i, maxnum = 0, jp0, jp1, n = pass == 0 ? 25u : pass == 1 ? 40u : 25u, gone;
        int32_t r0;
        clear_recs(ctx);
        log_reset();
        s = mk(ctx, SA1_CHAR_SONIC, 0, 0.5f, 0, 0, 0);
        w = sa1_character_world(ctx, s);
        CHECK(sa1_character_set_game_option(ctx, s, SA1_GOPT_ITEM_ICON_LIMIT, pass < 2 ? 1 : 0) == SA1_OK,
              "SA1_GOPT_ITEM_ICON_LIMIT %d", pass < 2 ? 1 : 0);
        run(ctx, s, 2, 0, 0, 0);
        r0 = rings_of(w);
        jp0 = crc_guest(w, A_JUMPPANELS, 0x40u);
        if (pass == 0) {
            /* 25 boxes broken one every 30 pairs (each break restarts the list's slide-in: it never drains) */
            for (i = 0; i < n; i++) {
                rec(ctx, SA1_OBJTYPE_ITEMBOX_AIR, 0.0f, 0.0f, 0.0f, 2, 0, 0);
                run(ctx, s, 30, 0, 0, 0);
                maxnum = rd32(w, A_ICONS + 4u) > maxnum ? rd32(w, A_ICONS + 4u) : maxnum;
            }
        } else if (pass == 1) {
            /* 40 boxes at once: more than the box window holds, so the game's own air box spawner 8C05D9C0 (what
             * Knuckles' dig calls) makes them at Sonic; the guard is in the game's icon code, whoever made the box */
            for (i = 0; i < n; i++) {
                int32_t item = 2;
                sa1g_world_call(w, airbox_cb, &item);
            }
            for (i = 0; i < 40; i++) {
                if (tick(ctx, s, 0, 0, 0) != 0) {
                    break;
                }
                maxnum = rd32(w, A_ICONS + 4u) > maxnum ? rd32(w, A_ICONS + 4u) : maxnum;
            }
        } else {
            /* n boxes at once */
            for (i = 0; i < n; i++) {
                rec(ctx, SA1_OBJTYPE_ITEMBOX_AIR, 0.0f, 0.0f, 0.0f, 2, 0, 0);
            }
            for (i = 0; i < 40; i++) {
                if (tick(ctx, s, 0, 0, 0) != 0) {
                    break;
                }
                maxnum = rd32(w, A_ICONS + 4u) > maxnum ? rd32(w, A_ICONS + 4u) : maxnum;
            }
        }
        jp1 = crc_guest(w, A_JUMPPANELS, 0x40u);
        gone = pass == 1 ? n : n_set(SA1_SET_EVENT_GONE, 0, s, NULL);   /* the spawner's boxes have no record */
        if (g_verbose || (pass < 2 && (maxnum > 20u || jp0 != jp1 || gone != n || n_faults() != 0))) {
            printf("    pass %u: max count %u, jump panel list crc %08X -> %08X, GONE %u of %u, faults %u / %d\n",
                   (unsigned)pass, (unsigned)maxnum, (unsigned)jp0, (unsigned)jp1, (unsigned)gone, (unsigned)n,
                   (unsigned)n_faults(), g_faults);
        }
        if (pass < 2) {
            CHECK(maxnum <= 20u && jp0 == jp1 && gone == n && rings_of(w) - r0 == (int32_t)(5u * n) && n_faults() == 0 &&
                      g_faults == 0,
                  "%u boxes %s with the limit: the list's count at most %u, B_8C78C4E0 unchanged, every item given (rings "
                  "%+d), no fault", (unsigned)n, pass == 0 ? "one every 30 pairs" : "in one pair", (unsigned)maxnum,
                  (int)(rings_of(w) - r0));
        } else {
            CHECK(maxnum > 20u && jp0 != jp1, "25 boxes at once without the limit: the game's overflow (count %u, "
                  "B_8C78C4E0 overwritten) - the default stays the game's", (unsigned)maxnum);
        }
        clear_recs(ctx);
        sa1_character_destroy(ctx, s);
    }
    c.box_window = 32;
    sa1_set_configure(ctx, &c);
}

/* ---- (24) no free between a touch and the game's end ------------------------------------------------------------------ */
static void t_touchwindow(sa1_context *ctx)
{
    static const char *how[3] = { "sa1_set_update", "PRESENT 0", "the record leaving the window" };
    uint32_t kind, h;
    printf("touchwindow (24): an update, a forced PRESENT 0, a window leave right after a touch\n");
    for (kind = 0; kind < 3; kind++) {
        for (h = 0; h < 3; h++) {
            sa1_character_id s;
            sa1g_world *w;
            uint32_t id, i, touched = 0, mode = 0, type = kind == 0 ? SA1_OBJTYPE_RING : kind == 1 ? SA1_OBJTYPE_ITEMBOX_AIR
                                                                                                    : SA1_OBJTYPE_ITEMBOX;
            int32_t r0;
            uint32_t ev;
            clear_recs(ctx);
            log_reset();
            s = mk(ctx, SA1_CHAR_SONIC, 0, 0.5f, 0, 0, 0);
            w = sa1_character_world(ctx, s);
            r0 = rings_of(w);
            id = rec(ctx, type, 30.0f, type == SA1_OBJTYPE_RING ? 6.0f : 0.0f, lane_z(30.0f), 2, 0, 0);
            run(ctx, s, 30, 0, 0, 0);
            if (type == SA1_OBJTYPE_ITEMBOX) {
                tele(w, 30.0f, 30.0f, lane_z(30.0f));
            }
            for (i = 0; i < 40 && !touched; i++) {
                uint32_t tp;
                tick(ctx, s, 0.0f, type == SA1_OBJTYPE_ITEMBOX ? 0.0f : 1.0f, 0);
                tp = task_of(w, id);
                mode = task_mode(w, tp);
                touched = tp != 0 && (type == SA1_OBJTYPE_ITEMBOX ? mode >= 3u : mode >= 2u);
            }
            if (h == 0) {
                move_rec(ctx, id, type, 30.0f, type == SA1_OBJTYPE_RING ? 6.0f : 0.0f, 120.0f, 2, 0, 0, 0);
            } else if (h == 1) {
                sa1_set_state(ctx, id, 0u, 0u);
            } else {
                move_rec(ctx, id, type, 30.0f, 0.0f, lane_z(30.0f), 2, 0, 0, 1u << SA1_CHAR_KNUCKLES);
            }
            run(ctx, s, 40, 0, 0, 0);
            ev = type == SA1_OBJTYPE_ITEMBOX ? n_set(SA1_SET_EVENT_BROKEN, id, s, NULL)
                                             : n_set(SA1_SET_EVENT_GONE, id, s, NULL);
            CHECK(touched && ev == 1 && rings_of(w) - r0 == (type == SA1_OBJTYPE_RING ? 1 : 5) &&
                      n_sound(type == SA1_OBJTYPE_RING ? SE_RING : SE_ITEM, s) == 1 && n_faults() == 0,
                  "%s: %s in the pair after the touch (mode %u): the %s given once (rings %+d), %s raised (%u)",
                  type == SA1_OBJTYPE_RING ? "RING" : type == SA1_OBJTYPE_ITEMBOX_AIR ? "air box" : "ground box", how[h],
                  (unsigned)mode, type == SA1_OBJTYPE_RING ? "ring" : "item", (int)(rings_of(w) - r0),
                  type == SA1_OBJTYPE_ITEMBOX ? "BROKEN" : "GONE", (unsigned)ev);
            clear_recs(ctx);
            sa1_character_destroy(ctx, s);
        }
    }
}

/* ---- (23) MAGNET: the magnetic barrier pulls rings ---------------------------------------------------------------------- */
static void t_magnet(sa1_context *ctx)
{
    static const float dists[2] = { 40.0f, 48.0f };
    uint32_t k;
    printf("magnet (23): Sonic with the magnetic barrier, a RING 40 / 48 units away; without the barrier; running away\n");
    for (k = 0; k < 4; k++) {
        sa1_character_id s;
        sa1g_world *w;
        uint32_t id, i, pwp;
        float sp[3], rp[3];
        const sa1_set_event *e = NULL;
        int barrier = k != 2, away = k == 3;
        clear_recs(ctx);
        log_reset();
        s = mk(ctx, SA1_CHAR_SONIC, 0, 0.5f, 0, 0, 0);
        w = sa1_character_world(ctx, s);
        if (barrier) {
            rec(ctx, SA1_OBJTYPE_ITEMBOX_AIR, 0.0f, 0.0f, 0.0f, 8, 0, 0);
        }
        run(ctx, s, 40, 0, 0, 0);
        pwp = rd32(w, A_PLAYER_PWP);
        ppos(w, sp);
        rp[0] = sp[0] + (away ? 44.0f : dists[k & 1]);
        rp[1] = sp[1] + 6.0f;
        rp[2] = sp[2];
        id = rec(ctx, SA1_OBJTYPE_RING, rp[0], rp[1], rp[2], 0, 0, 0);
        for (i = 0; i < 90; i++) {
            tick(ctx, s, 0.0f, away && i > 3 ? -1.0f : 0.0f, 0);
        }
        n_set(SA1_SET_EVENT_GONE, id, s, &e);
        if (k < 2) {
            CHECK((rd16(w, pwp + 6u) & 3u) == 2u && e != NULL && e->cause == SA1_SET_CAUSE_MAGNET &&
                      dist3(e->attacker, e->position) <= 16.0f,
                  "a RING %.0f units away with the barrier: collected, cause %u, the touch %.1f units (%.0f HU) from the "
                  "record, the attacker (the ring) %.1f units (%.0f HU) from the touch", (double)dists[k & 1],
                  e ? (unsigned)e->cause : 9u, e ? (double)dist3(e->position, rp) : -1.0,
                  e ? (double)(dist3(e->position, rp) * 4.0f) : -1.0, e ? (double)dist3(e->attacker, e->position) : -1.0,
                  e ? (double)(dist3(e->attacker, e->position) * 4.0f) : -1.0);
        } else if (k == 2) {
            CHECK(e == NULL && task_of(w, id) != 0, "a RING 40 units away without the barrier: not collected in 90 pairs");
        } else {
            NOTE("Sonic running away from a pulled ring (44 units ahead, he runs back): %s, the touch %.1f units (%.0f HU) "
                 "from the record's place (the server's MAGNET reach of 264 HU follows this number)",
                 e ? "collected" : "not collected", e ? (double)dist3(e->position, rp) : 0.0,
                 e ? (double)(dist3(e->position, rp) * 4.0f) : 0.0);
            CHECK(1, "(measured, no pass / fail)");
        }
        clear_recs(ctx);
        sa1_character_destroy(ctx, s);
    }
}

/* ---- (11) Big's lure in the water: the spawner centres on the camera taskwk (the world's eye) ------------------------------ */
static void hold_fish(struct sa1g_world *w, int32_t sub)
{
    (void)sub;
    wr16(w, A_BIGFISH, rd16(w, A_BIGFISH) | 0x40u);
}

static void t_bigeye(sa1_context *ctx)
{
    uint32_t pass;
    printf("bigeye (11): Big with B_8C7A40E8 & 0x40 held; records 30 units away and 420 behind him along the view\n");
    for (pass = 0; pass < 3; pass++) {
        sa1_character_id b;
        sa1g_world *w;
        uint32_t near_r, near_b, far_r;
        float bx = 1500.0f;
        clear_recs(ctx);
        log_reset();
        b = mk(ctx, SA1_CHAR_BIG, bx, 0.5f, 0, 0, 0);
        w = sa1_character_world(ctx, b);
        sa1w_set_world_test(w, pass == 2, pass != 1 ? hold_fish : NULL);
        near_r = rec(ctx, SA1_OBJTYPE_RING, bx + 30.0f, 6.0f, 20.0f, 0, 0, 0);
        near_b = rec(ctx, SA1_OBJTYPE_ITEMBOX, bx + 30.0f, 0.0f, -20.0f, 3, 0, 0);
        far_r = rec(ctx, SA1_OBJTYPE_RING, bx - 420.0f, 6.0f, 0.0f, 0, 0, 0);
        run(ctx, b, 6, 0, 0, 0);
        if (pass == 0) {
            float cp[3];
            uint32_t ct = rd32(w, A_CAMERA_TWP);
            cp[0] = rdf(w, ct + 0x20u);
            cp[1] = rdf(w, ct + 0x24u);
            cp[2] = rdf(w, ct + 0x28u);
            CHECK(task_of(w, near_r) != 0 && task_of(w, near_b) != 0 && task_of(w, far_r) != 0,
                  "the bit held: the RING and the ITEMBOX 30 units away spawn, and the RING 420 units behind Big (%.0f "
                  "from the eye at (%.0f %.0f %.0f)) spawns", (double)(cp[0] - (bx - 420.0f)), (double)cp[0], (double)cp[1],
                  (double)cp[2]);
        } else if (pass == 1) {
            CHECK(task_of(w, near_r) != 0 && task_of(w, near_b) != 0 && task_of(w, far_r) == 0,
                  "without the bit: the spawner centres on Big, the record 420 units away does not spawn");
        } else {
            CHECK(task_of(w, near_r) == 0 && task_of(w, near_b) == 0 && task_of(w, far_r) == 0,
                  "the control: the bit held, the eye write off (the camera taskwk at (0, 0, 0), Big 1500 units from it): "
                  "nothing spawns");
        }
        sa1w_set_world_test(w, 0, NULL);
        clear_recs(ctx);
        sa1_character_destroy(ctx, b);
    }
}

/* ---- (5) / (5b) churn: 5,000 RING records, 64 lines of 8 ------------------------------------------------------------------- */
static void t_churn(sa1_context *ctx)
{
    sa1_character_id s;
    sa1g_world *w;
    sa1set_world_info inf;
    uint32_t i, j, maxlive = 0, h0, h1, l0, l1, f0, f1, refused, far_id;
    int32_t r0;
    printf("churn (5, 5b): 5,000 RING records on a grid, Sonic running through; 64 line records of 8\n");
    clear_recs(ctx);
    log_reset();
    s = mk(ctx, SA1_CHAR_SONIC, 0, 0.5f, 0, 0, 0);
    w = sa1_character_world(ctx, s);
    sa1_character_set_game_option(ctx, s, SA1_GOPT_ITEM_ICON_LIMIT, 1);
    far_id = rec(ctx, SA1_OBJTYPE_RING, 0.0f, 6.0f, 2900.0f, 0, 0, 0);     /* the manager without spawns */
    run(ctx, s, 20, 0, 0, 0);
    heap_of(w, &h0, &l0);
    f0 = free_tasks(w);
    r0 = rings_of(w);
    for (i = 0; i < 100; i++) {
        for (j = 0; j < 50; j++) {
            rec(ctx, SA1_OBJTYPE_RING, 20.0f + 10.0f * (float)i, 6.0f, -250.0f + 10.0f * (float)j, 0, 0, 0);
        }
    }
    for (i = 0; i < 120; i++) {
        tick(ctx, s, 0.0f, 1.0f, 0);
        info_of(w, &inf);
        maxlive = inf.live_ring_tasks > maxlive ? inf.live_ring_tasks : maxlive;
    }
    refused = inf.refused_window;
    CHECK(maxlive <= 64u && rings_of(w) > r0 && n_faults() == 0 && n_capacity(SA1_CAPACITY_SET_WINDOW, s) >= 1,
          "5,000 RING records, Sonic running 120 pairs: live ring tasks at most %u (ring_window 64), rings %+d, %u refusals "
          "(SET_WINDOW capacity events %u)", (unsigned)maxlive, (int)(rings_of(w) - r0), (unsigned)refused,
          (unsigned)n_capacity(SA1_CAPACITY_SET_WINDOW, s));
    clear_recs(ctx);
    far_id = rec(ctx, SA1_OBJTYPE_RING, 0.0f, 6.0f, 2900.0f, 0, 0, 0);
    run(ctx, s, 90, 0, 0, 0);
    heap_of(w, &h1, &l1);
    f1 = free_tasks(w);
    info_of(w, &inf);
    CHECK(h1 == h0 && f1 == f0 && inf.slots == 0, "the records removed: game heap %u -> %u B free, free tasks %u -> %u, "
          "slots %u (back to their start values)", (unsigned)h0, (unsigned)h1, (unsigned)f0, (unsigned)f1,
          (unsigned)inf.slots);
    (void)far_id;
    /* (5b) 64 line records of 8 rings around Sonic */
    clear_recs(ctx);
    maxlive = 0;
    {
        float p[3];
        ppos(w, p);
        for (i = 0; i < 64; i++) {
            float a = (float)i * 0.0981747f, rr = 60.0f + 4.0f * (float)i;
            rec_ex(ctx, SA1_OBJTYPE_O_GRING, p[0] + rr * cosf(a), 6.0f, p[2] + rr * sinf(a), 7, 10, 0, 0, 0.0f, NULL);
        }
    }
    for (i = 0; i < 20; i++) {
        tick(ctx, s, 0, 0, 0);
        info_of(w, &inf);
        maxlive = inf.live_ring_tasks > maxlive ? inf.live_ring_tasks : maxlive;
    }
    CHECK(maxlive <= 64u && inf.ring_units <= 64u && inf.window_records == 8u,
          "(5b) 64 lines of 8 rings: live ring tasks at most %u, %u ring units in %u records", (unsigned)maxlive,
          (unsigned)inf.ring_units, (unsigned)inf.window_records);
    clear_recs(ctx);
    sa1_character_destroy(ctx, s);
}

/* ---- (6) the heap guard -------------------------------------------------------------------------------------------------- */
static void t_heapguard(sa1_context *ctx)
{
    sa1_character_id s;
    sa1g_world *w;
    uint32_t blocks[64], nb = 0, tot, largest, id, i;
    call_req r;
    sa1set_world_info inf;
    printf("heapguard (6): below 32 KB in one heap block no new spawn, one SET_HEAP event, no fault, no hang\n");
    clear_recs(ctx);
    log_reset();
    s = mk(ctx, SA1_CHAR_SONIC, 0, 0.5f, 0, 0, 0);
    w = sa1_character_world(ctx, s);
    rec(ctx, SA1_OBJTYPE_RING, 0.0f, 6.0f, 2900.0f, 0, 0, 0);
    run(ctx, s, 3, 0, 0, 0);
    heap_of(w, &tot, &largest);
    while (largest >= 32768u && nb < 64u) {
        memset(&r, 0, sizeof r);
        r.n = (int32_t)(largest >= 49152u ? 16384u : largest - 24576u);
        sa1g_world_call(w, malloc_cb, &r);
        if (r.ret == 0) {
            break;
        }
        blocks[nb++] = r.ret;
        heap_of(w, &tot, &largest);
    }
    id = rec(ctx, SA1_OBJTYPE_RING, 30.0f, 6.0f, 0.0f, 0, 0, 0);
    run(ctx, s, 10, 0, 0, 0);
    info_of(w, &inf);
    CHECK(largest < 32768u && task_of(w, id) == 0 && n_set(SA1_SET_EVENT_SPAWNED, id, s, NULL) == 0 &&
              n_capacity(SA1_CAPACITY_SET_HEAP, s) == 1 && n_faults() == 0 && cond_of(w, id) == 0x8000u,
          "the largest free block %u B (%u blocks taken): the RING 30 units away does not spawn (condition %04X), one "
          "SET_HEAP capacity event (%u), no fault", (unsigned)largest, (unsigned)nb, (unsigned)(cond_of(w, id) & 0xFFFFu),
          (unsigned)n_capacity(SA1_CAPACITY_SET_HEAP, s));
    for (i = 0; i < nb; i++) {
        memset(&r, 0, sizeof r);
        r.a = blocks[i];
        sa1g_world_call(w, mfree_cb, &r);
    }
    run(ctx, s, 3, 0, 0, 0);
    CHECK(task_of(w, id) != 0 && n_set(SA1_SET_EVENT_SPAWNED, id, s, NULL) == 1,
          "the heap back: the ring spawns (%u SPAWNED)", (unsigned)n_set(SA1_SET_EVENT_SPAWNED, id, s, NULL));
    clear_recs(ctx);
    sa1_character_destroy(ctx, s);
}

/* ---- (8) every kind collects rings ------------------------------------------------------------------------------------- */
static void t_kinds(sa1_context *ctx)
{
    static const struct { int32_t kind; int general; int pairs; } ks[8] = {
        { SA1_CHAR_SONIC, 0, 60 }, { SA1_CHAR_TAILS, 0, 60 }, { SA1_CHAR_KNUCKLES, 0, 60 }, { SA1_CHAR_AMY, 0, 60 },
        { SA1_CHAR_GAMMA, 0, 80 }, { SA1_CHAR_BIG, 0, 140 }, { SA1_CHAR_SUPER_SONIC, 0, 60 }, { SA1_CHAR_SONIC, 1, 60 },
    };
    uint32_t k;
    printf("kinds (8): every kind takes 3 rings\n");
    for (k = 0; k < 8; k++) {
        sa1_character_id id;
        sa1g_world *w;
        uint32_t i;
        int32_t r0;
        clear_recs(ctx);
        log_reset();
        id = mk(ctx, ks[k].kind, 0, 0.5f, 0, 10, ks[k].general);
        if (!id) {
            g_fail++;
            continue;
        }
        w = sa1_character_world(ctx, id);
        run(ctx, id, 4, 0, 0, 0);
        r0 = rings_of(w);
        for (i = 0; i < 3; i++) {
            rec(ctx, SA1_OBJTYPE_RING, 25.0f + 12.0f * (float)i, 6.0f, lane_z(25.0f + 12.0f * (float)i), 0, 0, 0);
        }
        run(ctx, id, ks[k].pairs, 0.0f, 1.0f, 0);
        run(ctx, id, 20, 0, 0, 0);
        CHECK(rings_of(w) - r0 == 3 && n_sound(SE_RING, id) == 3 && n_set(SA1_SET_EVENT_GONE, 0, id, NULL) == 3 &&
                  n_faults() == 0,
              "%s%s: rings %+d, SE 7 x%u, %u GONE", k_kinds[ks[k].kind], ks[k].general ? " (General Super Sonic)" : "",
              (int)(rings_of(w) - r0), (unsigned)n_sound(SE_RING, id), (unsigned)n_set(SA1_SET_EVENT_GONE, 0, id, NULL));
        sa1_character_destroy(ctx, id);
    }
    clear_recs(ctx);
}

/* ---- (9) determinism ----------------------------------------------------------------------------------------------------- */
static void det_run(sa1_context *ctx, uint32_t *ev_crc, uint32_t *ram_crc)
{
    sa1_character_id s, t;
    sa1g_world *w;
    uint32_t i, crc = 0;
    clear_recs(ctx);
    log_reset();
    s = mk(ctx, SA1_CHAR_SONIC, 0, 0.5f, 0, 0, 0);
    t = mk(ctx, SA1_CHAR_TAILS, -100.0f, 0.5f, 40.0f, 0, 0);
    w = sa1_character_world(ctx, s);
    for (i = 0; i < 4; i++) {
        rec(ctx, SA1_OBJTYPE_RING, 30.0f + 15.0f * (float)i, 6.0f, lane_z(30.0f + 15.0f * (float)i), 0, 0, 0);
    }
    rec_ex(ctx, SA1_OBJTYPE_O_GRING, 140.0f, 6.0f, lane_z(140.0f), 7, 15, 0, 0, 37.0f, NULL);
    rec(ctx, SA1_OBJTYPE_ITEMBOX_AIR, 200.0f, 0.0f, lane_z(200.0f), 3, 0, 0);
    rec(ctx, SA1_OBJTYPE_ITEMBOX, 260.0f, 0.0f, lane_z(260.0f) + 30.0f, 4, 0, 0);
    rec_ex(ctx, SA1_OBJTYPE_ITEMBOX_AIR, -60.0f, 8.0f, 40.0f, 6, 0, 0, 0, 123.0f, NULL);
    for (i = 0; i < 90; i++) {
        tick(ctx, s, 0.0f, i < 70 ? 1.0f : 0.0f, i == 50 ? SA1_BUTTON_JUMP : 0u);
        tick(ctx, t, i < 30 ? 0.3f : 0.0f, 0.0f, 0);
    }
    for (i = 0; i < g_log.ns; i++) {
        const sa1_set_event *e = &g_log.s[i];
        uint32_t ch = e->character == s ? 1u : 2u;
        crc = tu_crc32(crc, (const uint8_t *)&ch, 4);
        crc = tu_crc32(crc, (const uint8_t *)&e->type, 4);
        crc = tu_crc32(crc, (const uint8_t *)&e->condition, 4);
        crc = tu_crc32(crc, (const uint8_t *)&e->user_bits, 4);
        crc = tu_crc32(crc, (const uint8_t *)&e->step, 4);
        crc = tu_crc32(crc, (const uint8_t *)&e->cause, 4);
        crc = tu_crc32(crc, (const uint8_t *)&e->touch_step, 4);
        crc = tu_crc32(crc, (const uint8_t *)e->position, 12);
        crc = tu_crc32(crc, (const uint8_t *)e->attacker, 12);
    }
    *ev_crc = crc ^ g_log.ns;
    *ram_crc = arena_crc_masked(w) ^ arena_crc_masked(sa1_character_world(ctx, t));
    clear_recs(ctx);
    sa1_character_destroy(ctx, s);
    sa1_character_destroy(ctx, t);
}

static void t_determinism(sa1_context *ctx)
{
    uint32_t e1, r1, e2, r2;
    printf("determinism (9): a fixed scenario's set events and arenas, twice (build_set.py compares x86 and x64)\n");
    det_run(ctx, &e1, &r1);
    det_run(ctx, &e2, &r2);
    CHECK(e1 == e2 && r1 == r2, "two runs in a row: events %08X / %08X, arenas %08X / %08X", (unsigned)e1, (unsigned)e2,
          (unsigned)r1, (unsigned)r2);
    printf("DETERMINISM_SET %08X %08X (events, the arenas with the words naming segment 0x18 masked)\n",
           (unsigned)e1, (unsigned)r1);
}

/* ---- (10) the budgets and the capture: rings and boxes at the window, a hurt scatter, a dig fountain ------------------- */
typedef struct cap_res {
    uint32_t ccl7, ccl2, hud, char_draws, obj_draws, faults;
} cap_res;

static void cap_run(sa1_context *ctx, uint32_t nrings, uint32_t nboxes, int extras, cap_res *cr)
{
    sa1_character_id s;
    sa1g_world *w;
    sa1_set_config c;
    uint32_t i;
    memset(cr, 0, sizeof *cr);
    cr->hud = 0xFFFFFFFFu;
    clear_recs(ctx);
    log_reset();
    memset(&c, 0, sizeof c);
    c.struct_size = sizeof c;
    c.ring_window = 64;
    c.box_window = 32;
    c.margin = 100.0f;
    sa1_set_configure(ctx, &c);
    {
        /* beyond the API's maxima for the measurement: the registry's own fields (a test's hook) */
        sa1set_registry *R = sa1set_registry_of(ctx);
        R->ring_window = nrings;
        R->box_window = nboxes;
        R->config_version++;
    }
    s = mk(ctx, SA1_CHAR_SONIC, 0, 0.5f, 0, 50, 0);
    w = sa1_character_world(ctx, s);
    sa1_character_set_option(ctx, s, SA1_COPT_HUD_PARTS, (int32_t)SA1_HUD_ALL);
    sa1_character_set_game_option(ctx, s, SA1_GOPT_ITEM_ICON_LIMIT, 1);
    /* everything in view: ahead (+X) in a fan, rings and boxes alternating */
    for (i = 0; i < nrings + (nrings > 1u ? 20u : 0u); i++) {
        float x = 40.0f + 2.0f * (float)i, z = ((float)(i % 9) - 4.0f) * 0.08f * x;
        rec(ctx, SA1_OBJTYPE_RING, x, 6.0f + (float)(i % 3) * 4.0f, z, 0, 0, 0);
    }
    for (i = 0; i < nboxes + (nboxes > 1u ? 10u : 0u); i++) {
        float x = 45.0f + 3.0f * (float)i, z = ((float)(i % 7) - 3.0f) * 0.1f * x;
        rec(ctx, i & 1 ? SA1_OBJTYPE_ITEMBOX_AIR : SA1_OBJTYPE_ITEMBOX, x, i & 1 ? 8.0f : 0.0f, z, 3, 0, 0);
    }
    run(ctx, s, 30, 0, 0, 0);
    if (extras) {
        sa1_damage d;
        call_req fr;
        memset(&d, 0, sizeof d);
        d.struct_size = sizeof d;
        sa1_character_damage(ctx, s, &d);       /* the hurt scatter (20 rings) */
        tick(ctx, s, 0, 0, 0);
        memset(&fr, 0, sizeof fr);
        ppos(w, fr.pos);
        fr.pos[0] -= 30.0f;
        sa1g_world_call(w, fountain_cb, &fr);   /* the dig fountain (32 rings) */
    }
    for (i = 0; i < 60; i++) {
        const sa1_render *rd = NULL;
        const sa1_screen *sc = NULL;
        uint32_t k, hud = 0, chd = 0, obd = 0, c7, c2;
        if (tick(ctx, s, 0, 0, 0) != 0) {
            cr->faults++;
            break;
        }
        c7 = count_ccl(w, 7u);
        c2 = count_ccl(w, 2u);
        cr->ccl7 = c7 > cr->ccl7 ? c7 : cr->ccl7;
        cr->ccl2 = c2 > cr->ccl2 ? c2 : cr->ccl2;
        if (sa1_character_render(ctx, s, 1.0f, &rd) == SA1_OK && rd != NULL) {
            for (k = 0; k < rd->num_draws; k++) {
                if (rd->draws[k].flags & 0x1000u) {    /* SA1_DRAW_OBJECT */
                    obd++;
                } else if (!(rd->draws[k].flags & SA1_DRAW_EFFECT)) {
                    chd++;
                }
            }
        }
        if (sa1_character_screen(ctx, s, &sc) == SA1_OK && sc != NULL) {
            for (k = 0; k < sc->num_sprites; k++) {
                hud += sc->sprites[k].part == SA1_HUD_RINGS;
            }
        }
        cr->hud = hud < cr->hud ? hud : cr->hud;
        cr->char_draws = chd > cr->char_draws ? chd : cr->char_draws;   /* the hurt blink hides the body every other pair */
        cr->obj_draws = obd > cr->obj_draws ? obd : cr->obj_draws;
    }
    cr->faults += n_faults();
    clear_recs(ctx);
    sa1_character_destroy(ctx, s);
    c.ring_window = 64;
    c.box_window = 32;
    sa1_set_configure(ctx, &c);
}

static void t_capture(sa1_context *ctx)
{
    static const uint32_t tries[][2] = { { 64, 32 }, { SA1_SET_RING_WINDOW_MAX, SA1_SET_BOX_WINDOW_MAX }, { 150, 100 },
                                         { 150, 32 }, { 64, 100 } };
    cap_res cr;
    uint32_t t, lo, hi, mid;
    printf("capture (10): ring_window rings and box_window boxes all in view, a hurt scatter (20) and a dig fountain (32)\n");
    for (t = 0; t < sizeof tries / sizeof tries[0]; t++) {
        cap_run(ctx, tries[t][0], tries[t][1], 1, &cr);
        printf("    rings %3u, boxes %3u: CCL list 7 at most %u, list 2 at most %u, HUD ring sprites at least %u, the "
               "character's non-effect draws %u (most), object draws at most %u, faults %u\n", (unsigned)tries[t][0],
               (unsigned)tries[t][1], (unsigned)cr.ccl7, (unsigned)cr.ccl2, (unsigned)cr.hud, (unsigned)cr.char_draws,
               (unsigned)cr.obj_draws, (unsigned)cr.faults);
        if (t < 2) {
            CHECK(cr.ccl7 <= 256u && cr.ccl2 <= 128u && cr.hud > 0 && cr.char_draws > 0 && cr.faults == 0,
                  "at the %s (%u / %u): CCL lists within 256 / 128, the HUD's ring count drawn, the character drawn",
                  t == 0 ? "defaults" : "maxima", (unsigned)tries[t][0], (unsigned)tries[t][1]);
        } else {
            CHECK(cr.ccl7 <= 256u && cr.ccl2 <= 128u && cr.char_draws > 0 && cr.faults == 0,
                  "rings %u / boxes %u (beyond the maxima): CCL lists within 256 / 128, the character drawn, no fault, no "
                  "hang (the HUD may drop: measured below)",
                  (unsigned)tries[t][0], (unsigned)tries[t][1]);
        }
    }
    /* the largest windows that keep the HUD (review R.14): rings alone (boxes 1), boxes with the default 64 rings; both
     * with the scatter and the fountain */
    lo = 64;
    hi = 400;                       /* round 0.9.4b: 150 at RT_CAP_MAX 512 */
    while (lo < hi) {
        mid = (lo + hi + 1u) / 2u;
        cap_run(ctx, mid, 1, 1, &cr);
        if (cr.hud > 0) {
            lo = mid;
        } else {
            hi = mid - 1u;
        }
    }
    NOTE("the most ring tasks in view that keep the HUD (a scatter and a fountain too, no boxes): %u", (unsigned)lo);
    lo = 1;
    hi = 100;
    while (lo < hi) {
        mid = (lo + hi + 1u) / 2u;
        cap_run(ctx, 64, mid, 1, &cr);
        if (cr.hud > 0) {
            lo = mid;
        } else {
            hi = mid - 1u;
        }
    }
    NOTE("the most boxes in view with 64 rings that keep the HUD (a scatter and a fountain too): %u", (unsigned)lo);
    lo = 1;
    hi = 100;
    while (lo < hi) {
        mid = (lo + hi + 1u) / 2u;
        cap_run(ctx, 1, mid, 0, &cr);
        if (cr.hud > 0) {
            lo = mid;
        } else {
            hi = mid - 1u;
        }
    }
    NOTE("the most boxes in view alone (no rings, no scatter) that keep the HUD: %u", (unsigned)lo);
    {
        static const uint32_t bs[4] = { 32, 24, 16, 8 };
        uint32_t b;
        for (b = 0; b < 4; b++) {
            lo = 1;
            hi = 400;               /* round 0.9.4b: 150 at RT_CAP_MAX 512 */
            while (lo < hi) {
                mid = (lo + hi + 1u) / 2u;
                cap_run(ctx, mid, bs[b], 1, &cr);
                if (cr.hud > 0) {
                    lo = mid;
                } else {
                    hi = mid - 1u;
                }
            }
            NOTE("with %u boxes in view (and a scatter and a fountain): the most ring tasks that keep the HUD: %u",
                 (unsigned)bs[b], (unsigned)lo);
        }
    }
}

/* ==== round 0.9.4b (094b-lib-range; sadc docs/research/objects2_plan.md 6.1 with its Review R.4, R.6, R.12-R.15, 11.0 O1,
 *      11.1): the range, the window's order B, the maxima, the capture of 1,024, the sides, the live records, big circles.
 *      Each case leaves the context configured as the old cases expect (windows 64 / 32, range 0, view_tan 0, the scene's
 *      6000 x 6000 floor). ------------------------------------------------------------------------------------------------ */
#define A_CCL7_ARR  0x8C746D08u     /* colliwk *B_8C746D08[256]: CCL list 7 (code_8C02F27C.c func_8C02F63C, case 7) */
#define A_CCL2_ARR  0x8C745908u     /* colliwk *B_8C745908[128]: list 2 (case 2, count B_8C752BD8) */
#define RING_TRIS   96u             /* triangles of the ring model (D_8C1A21DC) */

/* the round 0.9.4b cases start from an empty registry: an earlier case may leave a record it does not track (a full
 * run measured one such record taking a unit of the budget) */
static void clear_all(sa1_context *ctx)
{
    sa1set_registry *R = sa1set_registry_of(ctx);
    uint32_t i, n = 0;
    clear_recs(ctx);
    for (i = 0; R != NULL && i < R->hiwater; i++) {
        if (R->ent[i].id != 0 && sa1_set_remove(ctx, R->ent[i].id) == SA1_OK) {
            n++;
        }
    }
    if (n != 0) {
        NOTE("%u record(s) an earlier case left in the registry removed first", (unsigned)n);
    }
}

static sa1_status cfg_set(sa1_context *ctx, uint32_t rw, uint32_t bw, float range, float view_tan)
{
    sa1_set_config c;
    sa1_status st;
    memset(&c, 0, sizeof c);
    c.struct_size = sizeof c;
    c.ring_window = rw;
    c.box_window = bw;
    c.margin = 100.0f;
    c.list = SA1_SETLIST_LIBSA1;
    c.range = range;
    c.view_tan = view_tan;
    st = sa1_set_configure(ctx, &c);
    if (st != SA1_OK) {
        printf("    sa1_set_configure(%u, %u, %g, %g): %s: %s\n", (unsigned)rw, (unsigned)bw, (double)range,
               (double)view_tan, sa1_status_string(st), sa1_last_error(ctx));
    }
    return st;
}

static void cfg_reset(sa1_context *ctx)
{
    sa1set_registry *R;
    cfg_set(ctx, 64, 32, 0.0f, 0.0f);
    R = sa1set_registry_of(ctx);
    if (R != NULL) {
        R->tier_hold = -1;
        R->config_version++;
    }
}

/* the scene's floor with half size h (the road needs more than the scene's 3000) */
static int scene_half(sa1_context *ctx, float h)
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
    return SA1_FAILED(sa1_collision_set_static(ctx, t, 2)) ? -1 : 0;
}

static int tick_yaw(sa1_context *ctx, sa1_character_id id, float sx, float sy, float yaw)
{
    sa1_input in;
    sa1_status st;
    memset(&in, 0, sizeof in);
    in.struct_size = sizeof in;
    in.stick_x = sx;
    in.stick_y = sy;
    in.camera_yaw = yaw;
    st = sa1_character_tick(ctx, id, &in, 1.0f / 30.0f, NULL, NULL);
    poll_all(ctx);
    if (st != SA1_OK) {
        g_faults++;
        printf("  tick: %s: %s\n", sa1_status_string(st), sa1_last_error(ctx));
        return -1;
    }
    return 0;
}

/* the generated list's four entries (attribute, fRange) through the world's segments (the list lives in segment 0x18) */
static int list_entries(sa1g_world *w, uint32_t attr[4], float fr[4])
{
    uint32_t tab = rd32(w, A_ITEMTABLE), ent = 0, k;
    int16_t n = 0;
    const uint8_t *t, *e;
    if (tab == 0 || (t = (const uint8_t *)sa1g_world_ptr(w, tab)) == NULL) {
        return -1;
    }
    memcpy(&n, t + 2, 2);
    memcpy(&ent, t + 4, 4);
    if (n != 4 || ent == 0 || (e = (const uint8_t *)sa1g_world_ptr(w, ent)) == NULL) {
        return -1;
    }
    for (k = 0; k < 4u; k++) {
        int16_t a;
        memcpy(&a, e + 0x14u * k + 2u, 2);
        attr[k] = (uint32_t)(uint16_t)a;
        memcpy(&fr[k], e + 0x14u * k + 4u, 4);
    }
    return 0;
}

static int list_is(sa1g_world *w, uint32_t want_attr, float want_fr)
{
    uint32_t a[4], k;
    float f[4];
    if (list_entries(w, a, f) != 0) {
        return 0;
    }
    for (k = 0; k < 4u; k++) {
        if (a[k] != want_attr || memcmp(&f[k], &want_fr, 4) != 0) {
            return 0;
        }
    }
    return 1;
}

/* ring models in the character's render (OBJECT triangle draws, not the blob shadows, 96 triangles each); far = the
 * farthest from c; near_of: counts only models within 15 units of that point (NULL: all) */
static uint32_t drawn_rings(sa1_context *ctx, sa1_character_id s, const float c[3], const float *near_of, float *far)
{
    const sa1_render *rd = NULL;
    uint32_t k, n = 0;
    if (far != NULL) {
        *far = 0.0f;
    }
    if (sa1_character_render(ctx, s, 1.0f, &rd) != SA1_OK || rd == NULL) {
        return 0;
    }
    for (k = 0; k < rd->num_draws; k++) {
        const sa1_draw *d = &rd->draws[k];
        uint32_t t, j;
        if (!(d->flags & 0x1000u) || (d->flags & SA1_DRAW_SHADOW) || d->prim != SA1_PRIM_TRIANGLES) {
            continue;
        }
        for (t = 0; t + RING_TRIS <= d->count; t += RING_TRIS) {
            double m[3] = { 0, 0, 0 };
            float q[3];
            for (j = 0; j < RING_TRIS * 9u; j++) {
                m[j % 3u] += rd->positions[9u * (d->first + t) + j];
            }
            for (j = 0; j < 3u; j++) {
                q[j] = (float)(m[j] / (RING_TRIS * 3.0));
            }
            if (near_of != NULL && dist3(q, near_of) > 15.0f) {
                continue;
            }
            if (far != NULL && c != NULL && dist3(q, c) > *far) {
                *far = dist3(q, c);
            }
            n++;
        }
    }
    return n;
}

static int cmp_float(const void *a, const void *b)
{
    float x = *(const float *)a, y = *(const float *)b;
    return x < y ? -1 : x > y;
}

/* ---- (R1) range 0 / 400: the list and the worlds exactly 0.9.4's ---------------------------------------------------- */
static void r1_run(sa1_context *ctx, float range, uint32_t *ev_crc, uint32_t *ram_crc, uint32_t *list_ok)
{
    sa1_character_id s;
    sa1g_world *w;
    uint32_t i, crc = 0, base;
    clear_all(ctx);
    log_reset();
    cfg_set(ctx, 64, 32, range, 0.0f);
    s = mk(ctx, SA1_CHAR_SONIC, 0, 0.5f, 0, 0, 0);
    w = sa1_character_world(ctx, s);
    base = g_nrecs;
    for (i = 0; i < 12; i++) {
        float x = 60.0f + 40.0f * (float)i;
        rec(ctx, SA1_OBJTYPE_RING, x, 6.0f, lane_z(x), 0, 0, 0);
    }
    rec(ctx, SA1_OBJTYPE_O_GRING, 700.0f, 6.0f, lane_z(700.0f), 7, 10, 0);
    rec(ctx, SA1_OBJTYPE_ITEMBOX, 300.0f, 0.0f, lane_z(300.0f) + 3.0f, 3, 0, 0);
    rec(ctx, SA1_OBJTYPE_ITEMBOX_AIR, 900.0f, 8.0f, lane_z(900.0f), 2, 0, 0);
    rec(ctx, SA1_OBJTYPE_RING, -450.0f, 6.0f, 0.0f, 0, 0, 0);
    run(ctx, s, 2, 0, 0, 0);
    *list_ok = (uint32_t)list_is(w, 0u, 0.0f);
    run(ctx, s, 130, 0, 1.0f, 0);
    for (i = 0; i < g_log.ns; i++) {
        const sa1_set_event *e = &g_log.s[i];
        uint32_t v[5] = { e->type, 0xFFFFu, e->condition, e->user_bits, e->step }, k;
        for (k = base; k < g_nrecs; k++) {
            if (g_recs[k] == e->record) {
                v[1] = k - base;            /* the record's place in this run's list (ids differ between runs) */
            }
        }
        crc = tu_crc32(crc, (const uint8_t *)v, sizeof v);
    }
    *ev_crc = crc;
    *ram_crc = arena_crc_masked(w);
    clear_recs(ctx);
    sa1_character_destroy(ctx, s);
}

static void t_r1(sa1_context *ctx)
{
    uint32_t e0, r0, l0, e4, r4, l4, i;
    sa1_character_id s;
    sa1g_world *w;
    printf("r1 (R1): range 0 and 400 run exactly as 0.9.4 (the list's attribute 0 / fRange 0; the same events and arena)\n");
    r1_run(ctx, 0.0f, &e0, &r0, &l0);
    r1_run(ctx, 400.0f, &e4, &r4, &l4);
    CHECK(l0 && l4, "the generated list in guest memory: attribute 0, fRange 0 with range 0 and with range 400");
    CHECK(e0 == e4 && r0 == r4, "Sonic running through 12 rings, a line, both boxes: range 400 = range 0 (events %08X / "
          "%08X, arenas %08X / %08X)", (unsigned)e0, (unsigned)e4, (unsigned)r0, (unsigned)r4);
    /* the entries follow the range at a pair start */
    clear_recs(ctx);
    s = mk(ctx, SA1_CHAR_SONIC, 0, 0.5f, 0, 0, 0);
    w = sa1_character_world(ctx, s);
    rec(ctx, SA1_OBJTYPE_RING, 60.0f, 6.0f, 30.0f, 0, 0, 0);
    run(ctx, s, 2, 0, 0, 0);
    {
        static const float rs[4] = { 1200.0f, 3000.0f, 400.0f, 0.0f };
        uint32_t ok = 0;
        for (i = 0; i < 4u; i++) {
            cfg_set(ctx, 64, 32, rs[i], 0.0f);
            run(ctx, s, 1, 0, 0, 0);
            ok += rs[i] > 400.0f ? list_is(w, 1u, rs[i] * rs[i]) : list_is(w, 0u, 0.0f);
        }
        CHECK(ok == 4u, "the entries at the next pair start: 1200 -> attribute 1, fRange 1,440,000; 3000 -> 9,000,000; 400 "
              "and 0 -> attribute 0, fRange 0 (%u of 4)", (unsigned)ok);
    }
    clear_recs(ctx);
    sa1_character_destroy(ctx, s);
    cfg_reset(ctx);
}

/* ---- (R2) the road: the first-spawn distance follows the range ------------------------------------------------------ */
typedef struct road_res {
    float median, min, farthest_ahead;
    uint32_t n, live_max, behind_max, pairs;
} road_res;

static void road_run(sa1_context *ctx, float range, uint32_t rw, road_res *rr)
{
    static float sd[256];
    static int32_t spawn_pair[256];
    sa1_character_id s;
    sa1g_world *w;
    uint32_t i, p, nsd = 0, base;
    float c[3];
    memset(rr, 0, sizeof *rr);
    clear_all(ctx);
    log_reset();
    cfg_set(ctx, rw, 100, range, 0.0f);
    base = g_nrecs;
    for (i = 0; i < 200; i++) {
        float x = 60.0f + 20.0f * (float)i;
        rec(ctx, SA1_OBJTYPE_RING, x, 8.0f, lane_z(x) + 25.0f, 0, 0, 0);
        spawn_pair[i] = -1;
    }
    s = mk(ctx, SA1_CHAR_SONIC, 0, 0.5f, 0, 50, 0);
    w = sa1_character_world(ctx, s);
    for (p = 0; p < 420; p++) {
        uint32_t live = 0, behind = 0;
        float ahead = 0.0f;
        if (tick(ctx, s, 0, p < 2 ? 0.0f : 1.0f, 0) != 0) {
            break;
        }
        ppos(w, c);
        for (i = 0; i < 200; i++) {
            float x = 60.0f + 20.0f * (float)i, q[3] = { x, 8.0f, lane_z(x) + 25.0f };
            if (task_of(w, g_recs[base + i]) == 0) {
                continue;
            }
            live++;
            behind += x < c[0];
            if (x - c[0] > ahead) {
                ahead = x - c[0];
            }
            if (spawn_pair[i] < 0) {
                spawn_pair[i] = (int32_t)p;
                if (p > 3u) {
                    sd[nsd++] = dist3(q, c);
                }
            }
        }
        rr->live_max = live > rr->live_max ? live : rr->live_max;
        rr->behind_max = behind > rr->behind_max ? behind : rr->behind_max;
        if (p > 30u && ahead > rr->farthest_ahead) {
            rr->farthest_ahead = ahead;
        }
        if (c[0] > 4300.0f) {
            break;
        }
    }
    rr->pairs = p;
    qsort(sd, nsd, sizeof sd[0], cmp_float);
    rr->n = nsd;
    rr->median = nsd ? sd[nsd / 2u] : 0.0f;
    rr->min = nsd ? sd[0] : 0.0f;
    clear_recs(ctx);
    sa1_character_destroy(ctx, s);
}

static void t_r2(sa1_context *ctx)
{
    static const float rs[6] = { 400.0f, 800.0f, 1200.0f, 1600.0f, 2000.0f, SA1_SET_RANGE_MAX };
    road_res rr;
    uint32_t i;
    printf("r2 (R2): the road - 200 rings 20 units apart, 25 beside Sonic's run (8.9 units a pair), 420 pairs\n");
    scene_half(ctx, 6000.0f);
    for (i = 0; i < 6u; i++) {
        road_run(ctx, rs[i], SA1_SET_RING_WINDOW_MAX, &rr);
        CHECK(rr.n >= 10u && fabsf(rr.median - (rs[i] - 8.0f)) <= 5.0f, "range %4.0f, windows %u / %u: first spawned at a "
              "median %.1f units (%.0f HU; want %.0f +-5; %u rings, min %.1f), live at most %u (%u behind him), ahead up to "
              "%.0f", (double)rs[i], (unsigned)SA1_SET_RING_WINDOW_MAX, (unsigned)SA1_SET_BOX_WINDOW_MAX,
              (double)rr.median, 4.0 * rr.median, (double)(rs[i] - 8.0f), (unsigned)rr.n, (double)rr.min,
              (unsigned)rr.live_max, (unsigned)rr.behind_max, (double)rr.farthest_ahead);
    }
    road_run(ctx, 1200.0f, 128, &rr);
    CHECK(rr.n >= 10u && rr.median >= 1150.0f, "range 1200, ring_window 128 (order B): first spawned at a median %.1f units "
          "(at least 1,150), live at most %u (%u behind)", (double)rr.median, (unsigned)rr.live_max, (unsigned)rr.behind_max);
    road_run(ctx, 1600.0f, 128, &rr);
    CHECK(rr.n >= 10u && rr.median >= 1500.0f && rr.farthest_ahead >= 1500.0f, "range 1600, ring_window 128: rings reach "
          "%.0f units ahead (at least 1,500; computed 1,580), first spawned at a median %.1f, live at most %u (%u behind)",
          (double)rr.farthest_ahead, (double)rr.median, (unsigned)rr.live_max, (unsigned)rr.behind_max);
    road_run(ctx, 1600.0f, 64, &rr);
    NOTE("range 1600 with 0.9.4's ring_window 64 (order B): first spawned at a median %.1f units, ahead up to %.0f "
         "(0.9.4's nearest-first order: 631 / about 640, objects2_range.md 1.3 A / G)", (double)rr.median,
         (double)rr.farthest_ahead);
    scene(ctx);
    cfg_reset(ctx);
}

/* ---- (R3) a configuration change mid-run ------------------------------------------------------------------------------- */
static void t_r3(sa1_context *ctx)
{
    sa1_character_id s;
    sa1g_world *w;
    uint32_t i, base, live12 = 0, live16 = 0, live0 = 0, bad12 = 0, bad16 = 0, bad0 = 0, ok16, ok0, gone, brk, ub;
    uint32_t desp;
    printf("r3 (R3): a standing Sonic, 28 rings along +X 100 .. 2800 units; range 1200 -> 1600 -> 0\n");
    clear_all(ctx);
    log_reset();
    cfg_set(ctx, SA1_SET_RING_WINDOW_MAX, SA1_SET_BOX_WINDOW_MAX, 1200.0f, 0.0f);
    s = mk(ctx, SA1_CHAR_SONIC, 0, 0.5f, 0, 0, 0);
    w = sa1_character_world(ctx, s);
    base = g_nrecs;
    for (i = 0; i < 28u; i++) {
        rec(ctx, SA1_OBJTYPE_RING, 100.0f * (float)(i + 1u), 8.0f, 40.0f, 0, 0, 0);
    }
    run(ctx, s, 10, 0, 0, 0);
    for (i = 0; i < 28u; i++) {
        float d = 100.0f * (float)(i + 1u);
        int live = task_of(w, g_recs[base + i]) != 0, want = d <= 1199.0f;
        live12 += live;
        bad12 += live != want && fabsf(d - 1200.0f) > 5.0f;
    }
    cfg_set(ctx, SA1_SET_RING_WINDOW_MAX, SA1_SET_BOX_WINDOW_MAX, 1600.0f, 0.0f);
    run(ctx, s, 1, 0, 0, 0);
    ok16 = (uint32_t)list_is(w, 1u, 1600.0f * 1600.0f);
    for (i = 0; i < 28u; i++) {
        float d = 100.0f * (float)(i + 1u);
        int live = task_of(w, g_recs[base + i]) != 0, want = d <= 1599.0f;
        live16 += live;
        bad16 += live != want && fabsf(d - 1600.0f) > 5.0f;
    }
    CHECK(bad12 == 0 && bad16 == 0 && live16 > live12, "the next pair's spawns use the new range: %u live at 1200, %u one "
          "pair after 1600 was set (those within the range: %u wrong)", (unsigned)live12, (unsigned)live16,
          (unsigned)(bad12 + bad16));
    log_reset();
    cfg_set(ctx, SA1_SET_RING_WINDOW_MAX, SA1_SET_BOX_WINDOW_MAX, 0.0f, 0.0f);
    run(ctx, s, 2, 0, 0, 0);
    ok0 = (uint32_t)list_is(w, 0u, 0.0f);
    for (i = 0; i < 28u; i++) {
        float d = 100.0f * (float)(i + 1u);
        int live = task_of(w, g_recs[base + i]) != 0, want = d <= 400.0f;
        live0 += live;
        bad0 += live != want && d <= 400.0f;            /* within 400: still live */
        bad0 += live && d > 500.0f;                      /* beyond the reach 400 + 100: freed */
    }
    gone = n_set(SA1_SET_EVENT_GONE, 0, 0, NULL);
    brk = n_set(SA1_SET_EVENT_BROKEN, 0, 0, NULL);
    ub = n_set(SA1_SET_EVENT_USERBITS, 0, 0, NULL);
    desp = n_set(SA1_SET_EVENT_DESPAWNED, 0, 0, NULL);
    CHECK(bad0 == 0 && desp == live16 - live0 && gone == 0 && brk == 0 && ub == 0, "lowered to 0: %u live (within 500), %u "
          "freed silently (DESPAWNED %u; GONE %u, BROKEN %u, USERBITS %u)", (unsigned)live0, (unsigned)(live16 - live0),
          (unsigned)desp, (unsigned)gone, (unsigned)brk, (unsigned)ub);
    CHECK(ok16 && ok0, "the list entries rewritten (guest read): 1600 -> attribute 1, fRange 2,560,000; 0 -> attribute 0, "
          "fRange 0");
    clear_recs(ctx);
    sa1_character_destroy(ctx, s);
    cfg_reset(ctx);
}

/* ---- (R4) the CCL ceiling (game logic) ---------------------------------------------------------------------------------- */
/* a test pre hook: the list-7 / list-2 entries of the PREVIOUS step (the arrays are zeroed here before every executor, so
 * the entries written in a step are exactly B_8C7456E2 / B_8C752BD8 of that step: CCL clears the counts at its end,
 * 8C0342E0 -> 8C02F81A, before any post hook could read them) */
static uint32_t g_ccl7_max, g_ccl2_max, g_ccl_steps;
static int g_shadow_force;          /* (R5): every RING task's ground height (scl.y) set to the floor's 0 */

static void ccl_count(struct sa1g_world *w)
{
    uint32_t k, n7 = 0, n2 = 0;
    for (k = 0; k < 256u; k++) {
        n7 += rd32(w, A_CCL7_ARR + 4u * k) != 0;
    }
    for (k = 0; k < 128u; k++) {
        n2 += rd32(w, A_CCL2_ARR + 4u * k) != 0;
    }
    g_ccl7_max = n7 > g_ccl7_max ? n7 : g_ccl7_max;
    g_ccl2_max = n2 > g_ccl2_max ? n2 : g_ccl2_max;
}

static void ccl_hook(struct sa1g_world *w, int32_t sub)
{
    uint32_t k;
    (void)sub;
    ccl_count(w);
    for (k = 0; k < 256u; k++) {
        wr32(w, A_CCL7_ARR + 4u * k, 0u);
    }
    for (k = 0; k < 128u; k++) {
        wr32(w, A_CCL2_ARR + 4u * k, 0u);
    }
    g_ccl_steps++;
    if (g_shadow_force) {
        uint32_t lv;
        for (lv = 0; lv < 8u; lv++) {
            uint32_t tp = rd32(w, A_TASK_LISTS + 4u * lv), guard = 0;
            while (arena(tp) && guard++ < 0x2000u) {
                if (same(rd32(w, tp + 0x10u), X_RING)) {
                    uint32_t twp = rd32(w, tp + 0x20u);
                    if (arena(twp) && rdf(w, twp + 0x30u) == -1000000.0f) {
                        wrf(w, twp + 0x30u, 0.0f);  /* the shadow's ground: the GRID floor (8C091C20 finds none) */
                    }
                }
                tp = rd32(w, tp);
            }
        }
    }
}

typedef struct mx_res {
    uint32_t ccl7, ccl2, hud, char_draws, obj_draws, shadows, records, dropped, pay_lost, pay_used, faults, live_rings;
    uint32_t live_boxes, heap_min;
} mx_res;

/* the maxima scene of (R4) / (R5): nrings RING records and nboxes boxes in the virtual camera's view (cap_run's fan),
 * range 800 so the whole fan spawns, a hurt scatter (20) and a dig fountain (32) */
static void mx_run(sa1_context *ctx, uint32_t nrings, uint32_t nboxes, int shadows, mx_res *mr)
{
    sa1_character_id s;
    sa1g_world *w;
    uint32_t i;
    sa1_damage d;
    call_req fr;
    memset(mr, 0, sizeof *mr);
    mr->hud = 0xFFFFFFFFu;
    mr->heap_min = 0xFFFFFFFFu;
    clear_all(ctx);
    log_reset();
    cfg_set(ctx, nrings, nboxes, 800.0f, 0.0f);
    s = mk(ctx, SA1_CHAR_SONIC, 0, 0.5f, 0, 50, 0);
    w = sa1_character_world(ctx, s);
    sa1_character_set_option(ctx, s, SA1_COPT_HUD_PARTS, (int32_t)SA1_HUD_ALL);
    sa1_character_set_game_option(ctx, s, SA1_GOPT_ITEM_ICON_LIMIT, 1);
    g_ccl7_max = g_ccl2_max = g_ccl_steps = 0;
    g_shadow_force = shadows;
    sa1w_set_world_test(w, 0, ccl_hook);
    for (i = 0; i < nrings + 20u; i++) {
        float x = 40.0f + 2.0f * (float)i, z = ((float)(i % 9) - 4.0f) * 0.08f * x;
        rec(ctx, SA1_OBJTYPE_RING, x, 6.0f + (float)(i % 3) * 4.0f, z, 0, 0, 0);
    }
    for (i = 0; i < nboxes + 10u; i++) {
        float x = 45.0f + 3.0f * (float)i, z = ((float)(i % 7) - 3.0f) * 0.1f * x;
        rec(ctx, i & 1 ? SA1_OBJTYPE_ITEMBOX_AIR : SA1_OBJTYPE_ITEMBOX, x, i & 1 ? 8.0f : 0.0f, z, 3, 0, 0);
    }
    run(ctx, s, 30, 0, 0, 0);
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    sa1_character_damage(ctx, s, &d);           /* the hurt scatter (20 rings) */
    tick(ctx, s, 0, 0, 0);
    memset(&fr, 0, sizeof fr);
    ppos(w, fr.pos);
    fr.pos[0] -= 30.0f;
    sa1g_world_call(w, fountain_cb, &fr);       /* the dig fountain (32 rings) */
    for (i = 0; i < 60; i++) {
        const sa1_render *rd = NULL;
        const sa1_screen *sc = NULL;
        sa1_capture_stats cs;
        sa1set_world_info inf;
        uint32_t k, hud = 0, chd = 0, obd = 0, shd = 0;
        if (tick(ctx, s, 0, 0, 0) != 0) {
            mr->faults++;
            break;
        }
        if (sa1_character_render(ctx, s, 1.0f, &rd) == SA1_OK && rd != NULL) {
            for (k = 0; k < rd->num_draws; k++) {
                if (rd->draws[k].flags & SA1_DRAW_SHADOW) {
                    shd++;
                } else if (rd->draws[k].flags & 0x1000u) {
                    obd++;
                } else if (!(rd->draws[k].flags & SA1_DRAW_EFFECT)) {
                    chd++;
                }
            }
        }
        if (sa1_character_screen(ctx, s, &sc) == SA1_OK && sc != NULL) {
            for (k = 0; k < sc->num_sprites; k++) {
                hud += sc->sprites[k].part == SA1_HUD_RINGS;
            }
        }
        memset(&cs, 0, sizeof cs);
        sa1_capture_get_stats(sa1_character_capture(ctx, s), &cs);
        info_of(w, &inf);
        mr->hud = hud < mr->hud ? hud : mr->hud;
        mr->char_draws = chd > mr->char_draws ? chd : mr->char_draws;
        mr->obj_draws = obd > mr->obj_draws ? obd : mr->obj_draws;
        mr->shadows = shd > mr->shadows ? shd : mr->shadows;
        mr->records = cs.records > mr->records ? cs.records : mr->records;
        mr->dropped = cs.dropped > mr->dropped ? cs.dropped : mr->dropped;
        mr->pay_lost = cs.payload_lost > mr->pay_lost ? cs.payload_lost : mr->pay_lost;
        mr->pay_used = cs.pay_used > mr->pay_used ? cs.pay_used : mr->pay_used;
        mr->live_rings = inf.live_ring_tasks > mr->live_rings ? inf.live_ring_tasks : mr->live_rings;
        mr->live_boxes = inf.live_boxes > mr->live_boxes ? inf.live_boxes : mr->live_boxes;
        mr->heap_min = inf.heap_largest < mr->heap_min ? inf.heap_largest : mr->heap_min;
    }
    ccl_count(w);
    mr->ccl7 = g_ccl7_max;
    mr->ccl2 = g_ccl2_max;
    mr->faults += n_faults();
    g_shadow_force = 0;
    sa1w_set_world_test(w, 0, NULL);
    clear_recs(ctx);
    sa1_character_destroy(ctx, s);
}

/* objects2_range.md 1.3 F's ccl2: Sonic stands 20 pairs in a fan of n rings (all spawn in the first pair, the nearest the
 * oldest tasks), then runs into it for 40 pairs; returns how many of the 50 nearest were taken */
static uint32_t fan_taken(sa1_context *ctx, uint32_t n, uint32_t window, uint32_t *live_max)
{
    sa1_character_id s;
    sa1g_world *w;
    uint32_t i, p, taken = 0, base;
    sa1set_registry *R;
    clear_all(ctx);
    log_reset();
    cfg_set(ctx, window <= SA1_SET_RING_WINDOW_MAX ? window : 64u, 32, n * 2.0f + 60.0f > 400.0f ? n * 2.0f + 60.0f : 0.0f,
            0.0f);
    if (window > SA1_SET_RING_WINDOW_MAX && (R = sa1set_registry_of(ctx)) != NULL) {
        R->ring_window = window;            /* DOCUMENT only: past the maximum through the registry (set_test (10)'s way) */
        R->config_version++;
    }
    base = g_nrecs;
    for (i = 0; i < n; i++) {
        float x = 40.0f + 2.0f * (float)i, z = ((float)(i % 9) - 4.0f) * 0.08f * x;
        rec(ctx, SA1_OBJTYPE_RING, x, 6.0f + (float)(i % 3) * 4.0f, z, 0, 0, 0);
    }
    s = mk(ctx, SA1_CHAR_SONIC, 0, 0.5f, 0, 0, 0);
    w = sa1_character_world(ctx, s);
    *live_max = 0;
    for (p = 0; p < 60u; p++) {
        sa1set_world_info inf;
        if (tick(ctx, s, 0, p < 20u ? 0.0f : 1.0f, 0) != 0) {
            break;
        }
        info_of(w, &inf);
        *live_max = inf.live_ring_tasks > *live_max ? inf.live_ring_tasks : *live_max;
    }
    for (i = 0; i < 50u && i < n; i++) {
        uint32_t c = cond_of(w, g_recs[base + i]);
        taken += c != 0xFFFFFFFFu && !(c & 0x8000u);
    }
    clear_recs(ctx);
    sa1_character_destroy(ctx, s);
    return taken;
}

static void t_r4(sa1_context *ctx)
{
    mx_res mr;
    uint32_t t50, t200, t300, l50, l200, l300;
    printf("r4 (R4): the game's collision lists are the ceiling: list 7 takes 256 entries a step, list 2 128\n");
    mx_run(ctx, SA1_SET_RING_WINDOW_MAX, SA1_SET_BOX_WINDOW_MAX, 0, &mr);
    CHECK(mr.ccl7 < 256u && mr.ccl2 < 128u && mr.live_rings >= SA1_SET_RING_WINDOW_MAX && mr.faults == 0,
          "ring_window %u, box_window %u, a hurt scatter and a dig fountain in the same pairs: list 7 at most %u entries a "
          "step (below 256), list 2 at most %u (below 128); %u ring tasks and %u boxes live (%u steps counted)",
          (unsigned)SA1_SET_RING_WINDOW_MAX, (unsigned)SA1_SET_BOX_WINDOW_MAX, (unsigned)mr.ccl7, (unsigned)mr.ccl2,
          (unsigned)mr.live_rings, (unsigned)mr.live_boxes, (unsigned)g_ccl_steps);
    t50 = fan_taken(ctx, 50, 50, &l50);
    t200 = fan_taken(ctx, 200, 200, &l200);
    CHECK(t200 == t50 && t50 > 0, "a standing fan of 200 (window 200), then the run: %u of the 50 nearest taken, as with "
          "50 rings (%u; live %u / %u)", (unsigned)t200, (unsigned)t50, (unsigned)l200, (unsigned)l50);
    t300 = fan_taken(ctx, 300, 300, &l300);
    NOTE("DOCUMENT (the reason for the maxima): 300 ring tasks through the registry's override: %u of the 50 nearest taken "
         "(live %u; objects2_range.md 1.3 F: 0)", (unsigned)t300, (unsigned)l300);
    cfg_reset(ctx);
}

/* ---- (R5) the capture at the maxima (review R.4: it decides RT_CAP_MAX 1,024 or 2,048) --------------------------------- */
static void t_r5(sa1_context *ctx)
{
    mx_res mr;
    printf("r5 (R5): every object of the maxima in the virtual camera's view, with blob shadows, a scatter and a fountain\n");
    mx_run(ctx, SA1_SET_RING_WINDOW_MAX, SA1_SET_BOX_WINDOW_MAX, 1, &mr);
    CHECK(mr.dropped == 0 && mr.hud > 0 && mr.hud != 0xFFFFFFFFu && mr.pay_lost == 0 && mr.char_draws > 0 &&
          mr.shadows >= SA1_SET_RING_WINDOW_MAX / 2u && mr.faults == 0,
          "%u ring tasks and %u boxes live: %u capture records at most (of the capacity), %u dropped, the HUD's ring digits "
          "drawn (at least %u sprites), %u records lost their payload (%u bytes used), %u blob-shadow draws (rings: 2 records "
          "each), %u object draws, the character drawn", (unsigned)mr.live_rings, (unsigned)mr.live_boxes,
          (unsigned)mr.records, (unsigned)mr.dropped, (unsigned)mr.hud, (unsigned)mr.pay_lost, (unsigned)mr.pay_used,
          (unsigned)mr.shadows, (unsigned)mr.obj_draws);
    mx_run(ctx, SA1_SET_RING_WINDOW_MAX, SA1_SET_BOX_WINDOW_MAX, 0, &mr);
    NOTE("without the forced shadows: %u records at most, %u dropped, HUD %u, %u shadow draws", (unsigned)mr.records,
         (unsigned)mr.dropped, (unsigned)mr.hud, (unsigned)mr.shadows);
    cfg_reset(ctx);
}

/* ---- (R11) the heap at the maxima ----------------------------------------------------------------------------------- */
static void t_r11(sa1_context *ctx)
{
    mx_res mr;
    ga_stats g0, g1;
    memset(&g0, 0, sizeof g0);
    memset(&g1, 0, sizeof g1);
    printf("r11 (R11): the game heap with 200 rings and 100 boxes live; the library's blocks after the world is gone\n");
    if (ga_enabled()) {
        ga_stats_get(&g0);
    }
    mx_run(ctx, SA1_SET_RING_WINDOW_MAX, SA1_SET_BOX_WINDOW_MAX, 0, &mr);
    if (ga_enabled()) {
        ga_stats_get(&g1);
    }
    CHECK(mr.heap_min > 32768u && mr.live_rings >= SA1_SET_RING_WINDOW_MAX && mr.live_boxes >= SA1_SET_BOX_WINDOW_MAX,
          "the game heap's largest free block stays above the 32 KB guard: %.1f KB at least (%u ring tasks, %u boxes live)",
          mr.heap_min / 1024.0, (unsigned)mr.live_rings, (unsigned)mr.live_boxes);
    if (ga_enabled()) {
        CHECK(g1.live == g0.live, "guard allocator: live library blocks before the world %llu, after it was destroyed %llu",
              (unsigned long long)g0.live, (unsigned long long)g1.live);
    } else {
        NOTE("the library's live blocks: run set_test --guard-alloc (SA1_GUARD_ALLOC=1) for the before / after check");
    }
    cfg_reset(ctx);
}

/* ---- (R6) order B ------------------------------------------------------------------------------------------------------- */
/* 300 rings: a 20 x 15 grid 150 units apart, x -1425 .. 1425, z -1050 .. 1050, around Sonic at the origin */
static uint32_t r6_field(sa1_context *ctx)
{
    uint32_t i, j, base = g_nrecs;
    for (j = 0; j < 15u; j++) {
        for (i = 0; i < 20u; i++) {
            rec(ctx, SA1_OBJTYPE_RING, -1425.0f + 150.0f * (float)i, 8.0f, -1050.0f + 150.0f * (float)j, 0, 0, 0);
        }
    }
    return base;
}

static float r6_x(uint32_t k)
{
    return -1425.0f + 150.0f * (float)(k % 20u);
}

static float r6_z(uint32_t k)
{
    return -1050.0f + 150.0f * (float)(k / 20u);
}

/* the selected (slotted, active) records' hash and counts; c = Sonic, fwd = +1 (camera yaw 0) or -1 (yaw pi) */
typedef struct r6_sel {
    uint32_t sel, in400, in400_unsel, far_sel;
    float t1_sel_far, t2_unsel_near;            /* the farthest selected tier-1 / nearest unselected tier-2 (beyond 400) */
    uint32_t hash;
} r6_sel;

static void r6_scan(sa1g_world *w, uint32_t base, const float c[3], float fwd, r6_sel *o)
{
    uint32_t k;
    memset(o, 0, sizeof *o);
    o->t2_unsel_near = 1e9f;
    for (k = 0; k < 300u; k++) {
        float q[3] = { r6_x(k), 8.0f, r6_z(k) }, d = dist3(q, c), eye = c[0] - 40.0f * fwd;
        int sel = sa1w_set_world_slot(w, g_recs[base + k]) >= 0, t1 = (q[0] - eye) * fwd > 0.0f;
        if (sel) {
            uint32_t id = g_recs[base + k];
            o->sel++;
            o->hash = tu_crc32(o->hash, (const uint8_t *)&id, 4);
        }
        if (d <= 400.0f) {
            o->in400++;
            o->in400_unsel += !sel;
        } else {
            o->far_sel += sel;
            if (sel && t1 && d > o->t1_sel_far) {
                o->t1_sel_far = d;
            }
            if (!sel && !t1 && d < o->t2_unsel_near) {
                o->t2_unsel_near = d;
            }
        }
    }
}

static uint32_t r6_sweep(sa1_context *ctx, int32_t hold, uint32_t *flips, uint32_t *held)
{
    sa1_character_id s;
    sa1g_world *w;
    sa1set_registry *R;
    sa1set_world_info inf;
    uint32_t p, frees;
    clear_all(ctx);
    log_reset();
    cfg_set(ctx, 64, 32, 1600.0f, 0.0f);
    R = sa1set_registry_of(ctx);
    R->tier_hold = hold;
    R->config_version++;
    r6_field(ctx);
    s = mk(ctx, SA1_CHAR_SONIC, 0, 0.5f, 0, 0, 0);
    w = sa1_character_world(ctx, s);
    run(ctx, s, 10, 0, 0, 0);
    log_reset();
    /* a camera that swings slowly to and fro across the sides (yaw 90 degrees +- 40, 2 degrees a pair) */
    for (p = 0; p < 300u; p++) {
        float a = (float)((p / 40u) & 1u ? 40u - p % 40u : p % 40u) * 2.0f - 40.0f;
        tick_yaw(ctx, s, 0, 0, (90.0f + a) * 0.017453292f);
    }
    frees = n_set(SA1_SET_EVENT_DESPAWNED, 0, 0, NULL);
    info_of(w, &inf);
    *flips = inf.tier_flips;
    *held = inf.tier_held;
    clear_recs(ctx);
    sa1_character_destroy(ctx, s);
    R->tier_hold = -1;
    return frees;
}

static void t_r6(sa1_context *ctx)
{
    sa1_character_id s;
    sa1g_world *w;
    uint32_t base, p, desp, frees30, frees0, f30, h30, f0, h0, near_freed = 0, i, det = 0;
    float c[3];
    r6_sel a, b;
    printf("r6 (R6): order B - a field of 300 rings 150 units apart around Sonic, ring_window 64, range 1600\n");
    clear_all(ctx);
    log_reset();
    cfg_set(ctx, 64, 32, 1600.0f, 0.0f);
    base = r6_field(ctx);
    s = mk(ctx, SA1_CHAR_SONIC, 0, 0.5f, 0, 0, 0);
    w = sa1_character_world(ctx, s);
    run(ctx, s, 10, 0, 0, 0);
    ppos(w, c);
    r6_scan(w, base, c, 1.0f, &a);
    {
        sa1set_world_info inf;
        sa1set_registry *R = sa1set_registry_of(ctx);
        info_of(w, &inf);
        CHECK(a.in400 > 0 && a.in400_unsel == 0 && a.sel == 64u, "every record within 400 units is selected before any "
              "farther one: %u within 400 (%u not selected), %u selected (%u beyond 400; the window: %u records, %u ring "
              "units, %u slots, ring_window %u, live ring tasks %u)", (unsigned)a.in400, (unsigned)a.in400_unsel,
              (unsigned)a.sel, (unsigned)a.far_sel, (unsigned)inf.window_records, (unsigned)inf.ring_units,
              (unsigned)inf.slots, R != NULL ? (unsigned)R->ring_window : 0u, (unsigned)inf.live_ring_tasks);
        if (a.sel != 64u && R != NULL) {
            uint32_t i2, k2;
            for (i2 = 0; i2 < R->hiwater; i2++) {
                const sa1set_entry *e2 = &R->ent[i2];
                int mine = 0;
                if (e2->id == 0) {
                    continue;
                }
                for (k2 = 0; k2 < 300u; k2++) {
                    mine |= g_recs[base + k2] == e2->id;
                }
                if (!mine) {
                    printf("    a registry record not of this case: id %08X type %u at (%.0f %.0f %.0f), slot %d\n",
                           (unsigned)e2->id, (unsigned)e2->rec.type, (double)e2->rec.position[0],
                           (double)e2->rec.position[1], (double)e2->rec.position[2], (int)sa1w_set_world_slot(w, e2->id));
                }
            }
            for (k2 = 0; k2 < 300u; k2++) {
                uint32_t n2 = 0;
                for (i2 = 0; i2 < 300u; i2++) {
                    n2 += g_recs[base + i2] == g_recs[base + k2];
                }
                if (n2 != 1u || sa1set_entry_of(R, g_recs[base + k2]) == NULL) {
                    printf("    record %u of the field: id %08X appears %u times, in the registry %d\n", (unsigned)k2,
                           (unsigned)g_recs[base + k2], (unsigned)n2, sa1set_entry_of(R, g_recs[base + k2]) != NULL);
                }
            }
        }
    }
    CHECK(a.t1_sel_far > a.t2_unsel_near && a.t2_unsel_near < 1e8f, "range 1600: a selected tier-1 record (in front of the "
          "virtual eye) %.0f units away, farther than an unselected tier-2 record %.0f away (nearest first could not)",
          (double)a.t1_sel_far, (double)a.t2_unsel_near);
    log_reset();
    for (p = 0; p < 300u; p++) {
        tick(ctx, s, 0, 0, 0);
        r6_scan(w, base, c, 1.0f, &b);
        det = tu_crc32(det, (const uint8_t *)&b.hash, 4);
    }
    desp = n_set(SA1_SET_EVENT_DESPAWNED, 0, 0, NULL);
    CHECK(desp == 0, "300 pairs at a constant camera yaw: %u tasks freed", (unsigned)desp);
    /* a 180-degree turn */
    log_reset();
    for (p = 0; p < 40u; p++) {
        tick_yaw(ctx, s, 0, 0, 3.14159265f);
        r6_scan(w, base, c, -1.0f, &b);
        det = tu_crc32(det, (const uint8_t *)&b.hash, 4);
    }
    for (i = 0; i < g_log.ns; i++) {
        const sa1_set_event *e = &g_log.s[i];
        uint32_t k;
        if (e->type != SA1_SET_EVENT_DESPAWNED) {
            continue;
        }
        for (k = 0; k < 300u; k++) {
            float q[3] = { r6_x(k), 8.0f, r6_z(k) };
            if (g_recs[base + k] == e->record && dist3(q, c) <= 400.0f) {
                near_freed++;
            }
        }
    }
    r6_scan(w, base, c, -1.0f, &b);
    CHECK(near_freed == 0 && b.in400_unsel == 0 && b.t1_sel_far > b.t2_unsel_near && b.t1_sel_far > 400.0f,
          "a 180-degree turn: no record within 400 units freed (%u), tier 1 and 2 swapped (now ahead: a selected record "
          "%.0f away in front of the turned eye, an unselected one %.0f away behind it; %u frees)", (unsigned)near_freed,
          (double)b.t1_sel_far, (double)b.t2_unsel_near, (unsigned)n_set(SA1_SET_EVENT_DESPAWNED, 0, 0, NULL));
    /* (review R.12) a record that comes within 400 units is selected in that pair's window: Sonic moved 600 units back
       (behind the eye: tier 2), the camera still turned */
    tele(w, 600.0f, 0.5f, 0.0f);
    tick_yaw(ctx, s, 0, 0, 3.14159265f);
    ppos(w, c);
    r6_scan(w, base, c, -1.0f, &b);
    CHECK(b.in400 > 0 && b.in400_unsel == 0, "moved 600 units: all %u records now within 400 units are selected in the next "
          "pair's window (%u not; no tier hold for tier 0)", (unsigned)b.in400, (unsigned)b.in400_unsel);
    clear_recs(ctx);
    sa1_character_destroy(ctx, s);
    frees30 = r6_sweep(ctx, -1, &f30, &h30);
    frees0 = r6_sweep(ctx, 0, &f0, &h0);
    CHECK(frees30 <= frees0, "a camera swinging +-40 degrees across the sides for 300 pairs: %u frees with SA1_SET_TIER_HOLD "
          "%u (tier changes %u taken, %u held), %u with no hold (%u changes)", (unsigned)frees30,
          (unsigned)SA1_SET_TIER_HOLD, (unsigned)f30, (unsigned)h30, (unsigned)frees0, (unsigned)f0);
    printf("DETERMINISM_SET_ORDER %08X (the selected ids per pair)\n", (unsigned)det);
    cfg_reset(ctx);
}

/* ---- (R7) Big's lure: the spawner centres on the eye ----------------------------------------------------------------- */
static void t_r7(sa1_context *ctx)
{
    sa1_character_id b;
    sa1g_world *w;
    uint32_t in, out;
    float bx = 1500.0f, eye;
    printf("r7 (R7): Big with B_8C7A40E8 & 0x40 held, range 1200: the spawner's centre is the virtual eye\n");
    clear_all(ctx);
    log_reset();
    cfg_set(ctx, 64, 32, 1200.0f, 0.0f);
    b = mk(ctx, SA1_CHAR_BIG, bx, 0.5f, 0, 0, 0);
    w = sa1_character_world(ctx, b);
    sa1w_set_world_test(w, 0, hold_fish);
    eye = bx - 40.0f;                       /* SA1G_VIEW_DIST behind him along the camera's yaw 0 */
    in = rec(ctx, SA1_OBJTYPE_RING, eye - 1190.0f, 7.5f, 0.0f, 0, 0, 0);
    out = rec(ctx, SA1_OBJTYPE_RING, eye - 1215.0f, 7.5f, 0.0f, 0, 0, 0);
    run(ctx, b, 6, 0, 0, 0);
    CHECK(task_of(w, in) != 0 && task_of(w, out) == 0, "a record 1,190 units from the eye (%.0f from Big) spawns; one 1,215 "
          "from the eye (%.0f from Big, inside the window's reach) does not", (double)(bx - (eye - 1190.0f)),
          (double)(bx - (eye - 1215.0f)));
    sa1w_set_world_test(w, 0, NULL);
    clear_recs(ctx);
    sa1_character_destroy(ctx, b);
    cfg_reset(ctx);
}

/* ---- (R8) the sides: view_tan ------------------------------------------------------------------------------------------- */
/* objects2_range.md 1.3 B's field: 200 rings, a 20 x 10 grid 200 units apart, x -1000 .. 2800, z -900 .. 900, y 8 */
static void r8_field(sa1_context *ctx)
{
    uint32_t i, j;
    for (j = 0; j < 10u; j++) {
        for (i = 0; i < 20u; i++) {
            rec(ctx, SA1_OBJTYPE_RING, -1000.0f + 200.0f * (float)i, 8.0f, -900.0f + 200.0f * (float)j, 0, 0, 0);
        }
    }
}

/* the most ring models drawn over 30 pairs after 20 (Sonic standing at the origin, camera yaw 0); near_of: only that one */
static uint32_t r8_drawn(sa1_context *ctx, float view_tan, const float *near_of, float *scale)
{
    sa1_character_id s;
    sa1g_world *w;
    sa1set_world_info inf;
    uint32_t p, most = 0;
    float c[3];
    cfg_set(ctx, SA1_SET_RING_WINDOW_MAX, SA1_SET_BOX_WINDOW_MAX, 1600.0f, view_tan);
    s = mk(ctx, SA1_CHAR_SONIC, 0, 0.5f, 0, 0, 0);
    w = sa1_character_world(ctx, s);
    run(ctx, s, 20, 0, 0, 0);
    for (p = 0; p < 30u; p++) {
        uint32_t n;
        tick(ctx, s, 0, 0, 0);
        ppos(w, c);
        n = drawn_rings(ctx, s, c, near_of, NULL);
        most = n > most ? n : most;
    }
    info_of(w, &inf);
    if (scale != NULL) {
        *scale = inf.objview_found ? inf.objview_scale : -1.0f;
    }
    sa1_character_destroy(ctx, s);
    return most;
}

/* one character kind's 300 pairs (a scripted stick, jumps) beside a road of rings: a hash of every pair's state, the
 * SET events and the rings; drawn = the most ring models drawn in a pair */
static sa1_state g_r8st[2][300];
static uint32_t g_r8ev[2], g_r8ram[2];

static uint32_t r8_kind_run(sa1_context *ctx, int32_t kind, float view_tan, uint32_t *drawn, int16_t *rings, int slot)
{
    sa1_character_id s;
    sa1g_world *w;
    uint32_t p, h = 0, i, base;
    float c[3];
    clear_all(ctx);
    log_reset();
    cfg_set(ctx, SA1_SET_RING_WINDOW_MAX, SA1_SET_BOX_WINDOW_MAX, 1600.0f, view_tan);
    base = g_nrecs;
    for (i = 0; i < 120u; i++) {
        float x = 60.0f + 25.0f * (float)i;
        rec(ctx, SA1_OBJTYPE_RING, x, 6.0f, lane_z(x) + (float)((int)(i % 5u) - 2) * 120.0f, 0, 0, 0);
    }
    s = mk(ctx, kind, 0, 0.5f, 0, 0, 0);
    w = sa1_character_world(ctx, s);
    *drawn = 0;
    g_r8ram[slot] = 0;
    for (p = 0; p < 300u; p++) {
        sa1_state st;
        uint32_t n;
        float sy = p < 10u ? 0.0f : 1.0f, sx = (p / 50u) & 1u ? 0.4f : -0.3f;
        tick(ctx, s, sx, sy, (p % 60u) > 50u ? SA1_BUTTON_JUMP : 0u);
        memset(&st, 0, sizeof st);
        st.struct_size = sizeof st;
        sa1_character_state(ctx, s, &st);
        h = tu_crc32(h, (const uint8_t *)&st, sizeof st);
        g_r8st[slot][p] = st;
        {
            uint32_t ram = arena_crc_masked(w);
            g_r8ram[slot] = tu_crc32(g_r8ram[slot], (const uint8_t *)&ram, 4);
        }
        ppos(w, c);
        n = drawn_rings(ctx, s, c, NULL, NULL);
        *drawn = n > *drawn ? n : *drawn;
    }
    for (i = 0; i < g_log.ns; i++) {
        const sa1_set_event *e = &g_log.s[i];
        uint32_t v[4] = { e->type, 0xFFFFu, e->condition, e->step }, k;
        for (k = base; k < g_nrecs; k++) {
            if (g_recs[k] == e->record) {
                v[1] = k - base;            /* the record's place in this run's list (ids differ between runs) */
            }
        }
        h = tu_crc32(h, (const uint8_t *)v, sizeof v);
    }
    *rings = rings_of(w);
    g_r8ev[slot] = g_log.ns;
    clear_recs(ctx);
    sa1_character_destroy(ctx, s);
    return h;
}

static void t_r8(sa1_context *ctx)
{
    static const int32_t kinds[3] = { SA1_CHAR_TAILS, SA1_CHAR_KNUCKLES, SA1_CHAR_SONIC };
    uint32_t d0, d1, d2, k, h0, h1, n0, n1, hid45, drw30;
    int16_t r0, r1;
    float s0, s1, s2;
    printf("r8 (R8): the sides - view_tan widens only the four SET types' sideways tests\n");
    clear_all(ctx);
    log_reset();
    r8_field(ctx);
    d0 = r8_drawn(ctx, 0.0f, NULL, &s0);
    d1 = r8_drawn(ctx, 1.873f, NULL, &s1);
    d2 = r8_drawn(ctx, 1.025f, NULL, &s2);
    CHECK(d0 >= 39u && d0 <= 43u && d1 >= 62u && d1 <= 66u && d2 >= 48u && d2 <= 52u,
          "the field at range 1600: %u rings drawn with view_tan 0 (41 +-2), %u with 1.873 (the owner's 3440 x 1440 at fov "
          "92.5: 64 +-2), %u with 1.025 (16:9 at fov 75: 50 +-2)", (unsigned)d0, (unsigned)d1, (unsigned)d2);
    CHECK(s0 == 0.0f && s1 > 0.37f && s1 < 0.38f && s2 > 0.68f && s2 < 0.69f, "the rt_objview block's kx_scale: %.4f, %.4f, "
          "%.4f (the game's tan 0.7001 / view_tan)", (double)s0, (double)s1, (double)s2);
    clear_recs(ctx);
    /* up / down stay the game's: a ring 1,000 units from the eye 45 degrees above the level view, and one 30 degrees above */
    {
        float eye[3] = { -40.0f, 0.5f + 7.0f, 0.0f }, q45[3], q30[3];
        q45[0] = eye[0] + 1000.0f * 0.70710678f;
        q45[1] = eye[1] + 1000.0f * 0.70710678f;
        q45[2] = 0.0f;
        q30[0] = eye[0] + 1000.0f * 0.8660254f;
        q30[1] = eye[1] + 1000.0f * 0.5f;
        q30[2] = 0.0f;
        hid45 = 0;
        drw30 = 0;
        rec(ctx, SA1_OBJTYPE_RING, q45[0], q45[1], q45[2], 0, 0, 0);
        hid45 += r8_drawn(ctx, 0.0f, q45, NULL) == 0;
        hid45 += r8_drawn(ctx, 1.873f, q45, NULL) == 0;
        clear_recs(ctx);
        rec(ctx, SA1_OBJTYPE_RING, q30[0], q30[1], q30[2], 0, 0, 0);
        drw30 += r8_drawn(ctx, 0.0f, q30, NULL) == 1;
        drw30 += r8_drawn(ctx, 1.873f, q30, NULL) == 1;
        clear_recs(ctx);
        CHECK(hid45 == 2 && drw30 == 2, "a ring 1,000 units away 45 degrees above the level view: hidden with view_tan 0 and "
              "1.873 (%u of 2); one 30 degrees above: drawn with both (%u of 2)", (unsigned)hid45, (unsigned)drw30);
    }
    for (k = 0; k < 3u; k++) {
        h0 = r8_kind_run(ctx, kinds[k], 0.0f, &n0, &r0, 0);
        h1 = r8_kind_run(ctx, kinds[k], 1.873f, &n1, &r1, 1);
        if (h0 != h1) {
            uint32_t p;
            for (p = 0; p < 300u && !memcmp(&g_r8st[0][p], &g_r8st[1][p], sizeof g_r8st[0][p]); p++) {
            }
            if (p < 300u) {
                const sa1_state *a = &g_r8st[0][p], *b = &g_r8st[1][p];
                printf("    first difference at pair %u: pos (%.3f %.3f %.3f) / (%.3f %.3f %.3f), action %d / %d, frame %.2f / "
                       "%.2f, flags %X / %X\n", (unsigned)p, (double)a->position[0], (double)a->position[1],
                       (double)a->position[2], (double)b->position[0], (double)b->position[1], (double)b->position[2],
                       (int)a->action, (int)b->action, (double)a->frame, (double)b->frame, (unsigned)a->flags,
                       (unsigned)b->flags);
            } else {
                printf("    the states are equal in every pair; SET events %u / %u\n", (unsigned)g_r8ev[0],
                       (unsigned)g_r8ev[1]);
            }
        }
        CHECK(g_r8ram[0] == g_r8ram[1], "%s: the whole arena (the words naming segment 0x18 masked) after every pair "
              "identical with view_tan 0 and 1.873 (%08X / %08X): the visible paths of the four types' displays write no "
              "game state", k_kinds[kinds[k]], (unsigned)g_r8ram[0], (unsigned)g_r8ram[1]);
        CHECK(h0 == h1 && r0 == r1, "%s, 300 pairs beside 120 rings (running, steering, jumping): every pair's state, the SET "
              "events and the rings (%d / %d) identical with view_tan 0 and 1.873 (hashes %08X / %08X; rings drawn at most "
              "%u / %u)", k_kinds[kinds[k]], (int)r0, (int)r1, (unsigned)h0, (unsigned)h1, (unsigned)n0, (unsigned)n1);
    }
    {
        uint32_t ev[2] = { 0, 0 }, v, i, base;
        int16_t got[2] = { 0, 0 };
        for (v = 0; v < 2u; v++) {
            sa1_character_id sc;
            sa1g_world *wc;
            clear_all(ctx);
            log_reset();
            cfg_set(ctx, SA1_SET_RING_WINDOW_MAX, SA1_SET_BOX_WINDOW_MAX, 1600.0f, v ? 1.873f : 0.0f);
            base = g_nrecs;
            for (i = 0; i < 40u; i++) {
                float x = 100.0f + 50.0f * (float)i;
                rec(ctx, SA1_OBJTYPE_RING, x, 6.0f, lane_z(x), 0, 0, 0);
            }
            sc = mk(ctx, SA1_CHAR_SONIC, 0, 0.5f, 0, 0, 0);
            wc = sa1_character_world(ctx, sc);
            run(ctx, sc, 280, 0, 1.0f, 0);
            got[v] = rings_of(wc);
            for (i = 0; i < g_log.ns; i++) {
                uint32_t e[3] = { g_log.s[i].type, 0xFFFFu, g_log.s[i].step }, kk;
                for (kk = base; kk < g_nrecs; kk++) {
                    if (g_recs[kk] == g_log.s[i].record) {
                        e[1] = kk - base;
                    }
                }
                ev[v] = tu_crc32(ev[v], (const uint8_t *)e, sizeof e);
            }
            clear_recs(ctx);
            sa1_character_destroy(ctx, sc);
        }
        CHECK(got[0] == got[1] && got[0] > 30 && ev[0] == ev[1], "ring collection unchanged: Sonic runs through 40 rings on "
              "his path - %d / %d collected with view_tan 0 / 1.873, the SET events identical (%08X / %08X)", (int)got[0],
              (int)got[1], (unsigned)ev[0], (unsigned)ev[1]);
    }
    r8_field(ctx);
    d1 = r8_drawn(ctx, 1.873f, NULL, NULL);
    d0 = r8_drawn(ctx, 0.0f, NULL, &s0);
    CHECK(s0 == 0.0f && d0 >= 39u && d0 <= 43u, "view_tan back to 0 after 1.873 (%u drawn): kx_scale %.1f in the block, %u "
          "drawn (the game's answers)", (unsigned)d1, (double)s0, (unsigned)d0);
    clear_recs(ctx);
    cfg_reset(ctx);
}

/* ---- (R9) arguments, (R10) sa1_object_info.range ---------------------------------------------------------------------- */
static void t_r9(sa1_context *ctx)
{
    static const float bad_r[4] = { 399.0f, SA1_SET_RANGE_MAX + 1.0f, -1.0f, 0.0f };
    static const float bad_v[2] = { 0.5f, SA1_SET_VIEW_TAN_MAX + 0.1f };
    sa1_set_config c;
    sa1set_registry *R;
    uint32_t i, refused = 0;
    float nan;
    uint32_t nanbits = 0x7FC00000u;
    memcpy(&nan, &nanbits, 4);
    printf("r9 (R9): sa1_set_configure's checks\n");
    for (i = 0; i < 4u; i++) {
        float r = i == 3u ? nan : bad_r[i];
        refused += cfg_set(ctx, 64, 32, r, 0.0f) == SA1_ERR_INVALID_ARGUMENT;
    }
    for (i = 0; i < 2u; i++) {
        refused += cfg_set(ctx, 64, 32, 0.0f, bad_v[i]) == SA1_ERR_INVALID_ARGUMENT;
    }
    refused += cfg_set(ctx, SA1_SET_RING_WINDOW_MAX + 1u, 32, 0.0f, 0.0f) == SA1_ERR_INVALID_ARGUMENT;
    refused += cfg_set(ctx, 64, SA1_SET_BOX_WINDOW_MAX + 1u, 0.0f, 0.0f) == SA1_ERR_INVALID_ARGUMENT;
    CHECK(refused == 8u, "range 399, %.0f, -1, NaN; view_tan 0.5, %.1f; ring_window %u, box_window %u: "
          "SA1_ERR_INVALID_ARGUMENT (%u of 8)", (double)(SA1_SET_RANGE_MAX + 1.0f), (double)(SA1_SET_VIEW_TAN_MAX + 0.1f),
          (unsigned)(SA1_SET_RING_WINDOW_MAX + 1u), (unsigned)(SA1_SET_BOX_WINDOW_MAX + 1u), (unsigned)refused);
    CHECK(cfg_set(ctx, 200, 100, SA1_SET_RANGE_MAX, SA1_SET_VIEW_TAN_MAX) == SA1_OK &&
          cfg_set(ctx, 64, 32, 400.0f, 0.700f) == SA1_OK, "the limits themselves are accepted (range 400 / %.0f, view_tan "
          "0.700 / %.1f, windows 200 / 100)", (double)SA1_SET_RANGE_MAX, (double)SA1_SET_VIEW_TAN_MAX);
    cfg_set(ctx, 64, 32, 1200.0f, 1.5f);
    memset(&c, 0, sizeof c);
    c.struct_size = (uint32_t)offsetof(sa1_set_config, range);     /* a host built against 0.9.4's header */
    c.ring_window = 70;
    c.box_window = 30;
    c.margin = 100.0f;
    c.list = SA1_SETLIST_LIBSA1;
    R = sa1set_registry_of(ctx);
    CHECK(sa1_set_configure(ctx, &c) == SA1_OK && R != NULL && R->range == 1200.0f && R->view_tan == 1.5f &&
          R->ring_window == 70u, "a struct_size that ends before range keeps range and view_tan (%.0f, %.2f) and takes the "
          "windows", R != NULL ? (double)R->range : -1.0, R != NULL ? (double)R->view_tan : -1.0);
    cfg_reset(ctx);
}

static void t_r10(sa1_context *ctx)
{
    sa1_coords co;
    sa1_object_info oi;
    float r0, r1, r2;
    printf("r10 (R10): sa1_object_info.range follows the range (host units)\n");
    clear_recs(ctx);
    memset(&co, 0, sizeof co);
    co.struct_size = sizeof co;
    sa1_coords_preset(SA1_COORDS_PRESET_SOURCE, &co);
    sa1_set_coords(ctx, &co);
    memset(&oi, 0, sizeof oi);
    oi.struct_size = sizeof oi;
    cfg_set(ctx, 64, 32, 0.0f, 0.0f);
    sa1_object_info_get(ctx, SA1_OBJTYPE_RING, &oi);
    r0 = oi.range;
    cfg_set(ctx, 64, 32, 1200.0f, 0.0f);
    sa1_object_info_get(ctx, SA1_OBJTYPE_ITEMBOX, &oi);
    r1 = oi.range;
    cfg_set(ctx, 64, 32, SA1_SET_RANGE_MAX, 0.0f);
    sa1_object_info_get(ctx, SA1_OBJTYPE_O_GRING, &oi);
    r2 = oi.range;
    sa1_coords_preset(SA1_COORDS_PRESET_NATIVE, &co);
    sa1_set_coords(ctx, &co);
    CHECK(r0 == 1600.0f && r1 == 4800.0f && r2 == 4.0f * SA1_SET_RANGE_MAX, "coords scale 4 (Hammer units): %.0f HU with "
          "range 0, %.0f after range 1200, %.0f at the maximum %.0f (12,000 HU: owner answer O1)", (double)r0, (double)r1,
          (double)r2, (double)SA1_SET_RANGE_MAX);
    cfg_reset(ctx);
}

/* ---- (R12) sa1_set_live (review R.6) ------------------------------------------------------------------------------------ */
#define R12_MAX 512u
static uint32_t g_live[R12_MAX], g_want[R12_MAX];

static int cmp_u32(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : x > y;
}

/* the records with a SPAWNED and no later DESPAWNED of character s in the log, ascending */
static uint32_t live_from_events(sa1_character_id s, uint32_t *out, uint32_t cap)
{
    uint32_t i, n = 0, k;
    for (i = 0; i < g_log.ns; i++) {
        const sa1_set_event *e = &g_log.s[i];
        if (e->character != s) {
            continue;
        }
        for (k = 0; k < n && out[k] != e->record; k++) {
        }
        if (e->type == SA1_SET_EVENT_SPAWNED && k == n && n < cap) {
            out[n++] = e->record;
        } else if (e->type == SA1_SET_EVENT_DESPAWNED && k < n) {
            out[k] = out[--n];
        }
    }
    qsort(out, n, sizeof out[0], cmp_u32);
    return n;
}

static void t_r12(sa1_context *ctx)
{
    sa1_character_id s;
    sa1g_world *w;
    sa1set_world_info inf;
    uint32_t p, i, n = 0, nw, pairs_ok = 0, asc = 1, cnt = 0, small_cnt = 0, ok_tasks = 0, base;
    sa1_status st;
    printf("r12 (R12): sa1_set_live - the records whose task lives after each pair\n");
    clear_all(ctx);
    log_reset();
    cfg_set(ctx, 64, 32, 800.0f, 0.0f);
    for (i = 0; i < 80u; i++) {
        float x = 60.0f + 30.0f * (float)i;
        rec(ctx, SA1_OBJTYPE_RING, x, 6.0f, lane_z(x) + ((i & 3u) == 0u ? 0.0f : 60.0f), 0, 0, 0);
    }
    rec(ctx, SA1_OBJTYPE_O_GRING, 900.0f, 6.0f, lane_z(900.0f), 7, 10, 0);
    rec(ctx, SA1_OBJTYPE_O_GRING, 1500.0f, 6.0f, lane_z(1500.0f) + 50.0f, 6, 20, 1);
    rec(ctx, SA1_OBJTYPE_ITEMBOX, 600.0f, 0.0f, lane_z(600.0f) + 60.0f, 3, 0, 0);
    rec(ctx, SA1_OBJTYPE_ITEMBOX_AIR, 1200.0f, 8.0f, lane_z(1200.0f) - 60.0f, 2, 0, 0);
    s = mk(ctx, SA1_CHAR_SONIC, 0, 0.5f, 0, 0, 0);
    w = sa1_character_world(ctx, s);
    for (p = 0; p < 240u; p++) {
        uint32_t k, same_set;
        tick(ctx, s, 0, p < 2u ? 0.0f : 1.0f, 0);
        st = sa1_set_live(ctx, s, g_live, R12_MAX, &nw);
        n = live_from_events(s, g_want, R12_MAX);
        same_set = st == SA1_OK && nw == n && memcmp(g_live, g_want, n * sizeof g_live[0]) == 0;
        pairs_ok += same_set;
        for (k = 1; k < nw && k < R12_MAX; k++) {
            asc &= g_live[k - 1u] < g_live[k];
        }
        if (p == 120u) {
            cnt = nw;
        }
    }
    CHECK(pairs_ok == 240u && asc, "240 pairs of a run through 80 rings, two groups and two boxes (range 800, window 64): "
          "after every pair exactly the records with a SPAWNED and no later DESPAWNED (%u of 240 pairs), ascending",
          (unsigned)pairs_ok);
    if (cnt > 1u) {
        uint32_t one = 0;
        st = sa1_set_live(ctx, s, &one, 1, &small_cnt);
        CHECK(st == SA1_ERR_BUFFER_TOO_SMALL && small_cnt == nw, "capacity 1: SA1_ERR_BUFFER_TOO_SMALL with the count %u",
              (unsigned)small_cnt);
    }
    st = sa1_set_live(ctx, s, NULL, 0, &small_cnt);
    CHECK((st == SA1_OK || st == SA1_ERR_BUFFER_TOO_SMALL) && small_cnt == nw, "NULL with capacity 0: the count %u (%s)",
          (unsigned)small_cnt, sa1_status_string(st));
    clear_recs(ctx);
    sa1_character_destroy(ctx, s);
    st = sa1_set_live(ctx, s, g_live, R12_MAX, &nw);
    CHECK(st == SA1_ERR_NOT_FOUND, "a destroyed character: %s", sa1_status_string(st));
    /* 300 records spawning in one pair (more than the 256-event queue) */
    log_reset();
    cfg_set(ctx, SA1_SET_RING_WINDOW_MAX, SA1_SET_BOX_WINDOW_MAX, 1200.0f, 0.0f);
    base = g_nrecs;
    for (i = 0; i < 200u; i++) {
        rec(ctx, SA1_OBJTYPE_RING, -700.0f + 70.0f * (float)(i % 20u), 8.0f, -700.0f + 140.0f * (float)(i / 20u), 0, 0, 0);
    }
    for (i = 0; i < 100u; i++) {
        rec(ctx, (i & 1u) ? SA1_OBJTYPE_ITEMBOX_AIR : SA1_OBJTYPE_ITEMBOX, -665.0f + 140.0f * (float)(i % 10u),
            (i & 1u) ? 10.0f : 0.0f, -630.0f + 140.0f * (float)(i / 10u), 3, 0, 0);
    }
    s = mk(ctx, SA1_CHAR_SONIC, 0, 0.5f, 0, 0, 0);
    w = sa1_character_world(ctx, s);
    tick(ctx, s, 0, 0, 0);
    info_of(w, &inf);
    st = sa1_set_live(ctx, s, g_live, R12_MAX, &nw);
    for (i = 0; i < 300u; i++) {
        uint32_t id = g_recs[base + i];
        ok_tasks += task_of(w, id) != 0 && bsearch(&id, g_live, nw, sizeof g_live[0], cmp_u32) != NULL;
    }
    CHECK(st == SA1_OK && nw == 300u && ok_tasks == 300u, "300 records spawned in the first pair (%u SET events dropped by "
          "the 256-event queue): sa1_set_live lists %u, each with its task (%u)", (unsigned)inf.events_dropped,
          (unsigned)nw, (unsigned)ok_tasks);
    clear_recs(ctx);
    sa1_character_destroy(ctx, s);
    cfg_reset(ctx);
}

/* ---- (11.1) a big ring circle: 0.9.4.1 builds 128 rings as 32 circle records of 4 rings sharing the centre ------------- */
typedef struct bc_ring {
    float p[3];
    float ang;
} bc_ring;

static int cmp_ang(const void *a, const void *b)
{
    float x = ((const bc_ring *)a)->ang, y = ((const bc_ring *)b)->ang;
    return x < y ? -1 : x > y;
}

/* a ring child of a ring group in mode 1 (not taken) within 0.5 units of q */
static int bc_ring_live(const sa1g_world *w, const float q[3])
{
    uint32_t lv;
    for (lv = 0; lv < 8u; lv++) {
        uint32_t tp = rd32(w, A_TASK_LISTS + 4u * lv), guard = 0;
        while (arena(tp) && guard++ < 0x2000u) {
            if (same(rd32(w, tp + 0x10u), X_GRING)) {
                uint32_t ct = rd32(w, tp + 0x0Cu), g2 = 0;
                while (arena(ct) && g2++ < 64u) {
                    uint32_t twp = rd32(w, ct + 0x20u);
                    float p[3] = { rdf(w, twp + 0x20u), rdf(w, twp + 0x24u), rdf(w, twp + 0x28u) };
                    if (same(rd32(w, ct + 0x10u), X_RING) && (rd32(w, twp) & 0xFFu) == 1u && dist3(p, q) < 0.5f) {
                        return 1;
                    }
                    ct = rd32(w, ct);
                }
            }
            tp = rd32(w, tp);
        }
    }
    return 0;
}

static void t_bigcircle(sa1_context *ctx)
{
    static bc_ring rings[128];
    sa1_character_id s;
    sa1g_world *w;
    const float cx = 400.0f, cy = 6.0f, cz = 300.0f, R = 360.0f;  /* rings 17.7 units apart: one taken at a time */
    uint32_t k, i, j, nr = 0, live_ok = 0, collected = 0, place_ok = 0, place_n = 0, lv;
    int16_t r0, r1;
    printf("bigcircle (11.1): a circle of 128 rings as 0.9.4.1 sends it (32 records of 4 rings, each turned by k x 360 / "
           "128 about the shared centre), ring_window 64, range 0: Sonic walks around it\n");
    clear_all(ctx);
    log_reset();
    cfg_set(ctx, 64, 32, 0.0f, 0.0f);
    for (k = 0; k < 32u; k++) {
        float yaw = (float)k * 360.0f / 128.0f, m[9];
        rec_ex(ctx, SA1_OBJTYPE_O_GRING, cx, cy, cz, 4, R, 1, 0u, yaw, NULL);
        yaw_matrix(yaw, m);
        for (i = 1; i <= 4u; i++) {
            double a = (double)i * 90.0 * 3.14159265358979 / 180.0;
            float v[3] = { R * (float)sin(a), 0.0f, R * (float)cos(a) };
            bc_ring *rg = &rings[nr++];
            rg->p[0] = cx + m[0] * v[0] + m[1] * v[1] + m[2] * v[2];
            rg->p[1] = cy + m[3] * v[0] + m[4] * v[1] + m[5] * v[2];
            rg->p[2] = cz + m[6] * v[0] + m[7] * v[1] + m[8] * v[2];
            rg->ang = atan2f(rg->p[0] - cx, rg->p[2] - cz);
        }
    }
    qsort(rings, nr, sizeof rings[0], cmp_ang);
    s = mk(ctx, SA1_CHAR_SONIC, cx, 0.5f, cz - R - 30.0f, 0, 0);
    w = sa1_character_world(ctx, s);
    run(ctx, s, 3, 0, 0, 0);
    /* the game placed every live child ring where the window's nearest-ring rule (st_gring_near) puts it */
    for (lv = 0; lv < 8u; lv++) {
        uint32_t tp = rd32(w, A_TASK_LISTS + 4u * lv), guard = 0;
        while (arena(tp) && guard++ < 0x2000u) {
            if (same(rd32(w, tp + 0x10u), X_GRING)) {
                uint32_t ct = rd32(w, tp + 0x0Cu), g2 = 0;
                while (arena(ct) && g2++ < 64u) {
                    uint32_t twp = rd32(w, ct + 0x20u);
                    float q[3] = { rdf(w, twp + 0x20u), rdf(w, twp + 0x24u), rdf(w, twp + 0x28u) }, best = 1e9f;
                    for (j = 0; j < nr; j++) {
                        float d = dist3(q, rings[j].p);
                        best = d < best ? d : best;
                    }
                    place_n++;
                    place_ok += best < 0.05f;
                    ct = rd32(w, ct);
                }
            }
            tp = rd32(w, tp);
        }
    }
    CHECK(place_n == 64u && place_ok == place_n, "window 64: %u child rings live (16 records x 4), each where the layout "
          "formula puts it (%u within 0.05 units)", (unsigned)place_n, (unsigned)place_ok);
    r0 = rings_of(w);
    for (j = 0; j < nr; j++) {
        const float *q = rings[j].p;
        float dx = q[0] - cx, dz = q[2] - cz, l = sqrtf(dx * dx + dz * dz);
        /* 30 units outside the ring (the circle's centre stays within the game's 400: the spawner makes every record the
           window gives a slot), then onto it */
        tele(w, cx + dx / l * (R + 30.0f), 0.5f, cz + dz / l * (R + 30.0f));
        run(ctx, s, 2, 0, 0, 0);
        live_ok += (uint32_t)bc_ring_live(w, q);
        tele(w, q[0], 0.5f, q[2]);
        run(ctx, s, 2, 0, 0, 0);
        collected += !bc_ring_live(w, q);
    }
    r1 = rings_of(w);
    CHECK(live_ok == nr && collected == nr && r1 - r0 >= (int16_t)nr, "walking around it: every one of the %u rings became "
          "live next to Sonic (%u) and was collected (%u; the ring counter %d -> %d)", (unsigned)nr, (unsigned)live_ok,
          (unsigned)collected, (int)r0, (int)r1);
    clear_recs(ctx);
    sa1_character_destroy(ctx, s);
    cfg_reset(ctx);
}

/* ---- (11.1) st_window's under-fill (0.9.4's verifier LOW): a protected slot held by an earlier pass counted again ------- */
static void t_underfill(sa1_context *ctx)
{
    sa1_character_id s;
    sa1g_world *w;
    uint32_t a, b, c, d, e, ta, tb, ma, mb, lc, ld, le;
    printf("underfill (11.1): ring_window 4; rings A and B collected (sparkling: the touch rule keeps their tasks), Sonic moved "
           "300 units to rings C, D, E: the window must give C and D the two units A and B leave\n");
    clear_all(ctx);
    log_reset();
    cfg_set(ctx, 4, 32, 0.0f, 0.0f);
    a = rec(ctx, SA1_OBJTYPE_RING, 0.0f, 6.0f, 0.0f, 0, 0, 0);
    b = rec(ctx, SA1_OBJTYPE_RING, 3.0f, 6.0f, 0.0f, 0, 0, 0);
    c = rec(ctx, SA1_OBJTYPE_RING, 320.0f, 8.0f, 0.0f, 0, 0, 0);
    d = rec(ctx, SA1_OBJTYPE_RING, 300.0f, 8.0f, 30.0f, 0, 0, 0);
    e = rec(ctx, SA1_OBJTYPE_RING, 280.0f, 8.0f, -35.0f, 0, 0, 0);
    s = mk(ctx, SA1_CHAR_SONIC, 0, 0.5f, 0, 0, 0);
    w = sa1_character_world(ctx, s);
    run(ctx, s, 2, 0, 0, 0);
    ta = task_of(w, a);
    tb = task_of(w, b);
    ma = task_mode(w, ta);
    mb = task_mode(w, tb);
    tele(w, 300.0f, 0.5f, 0.0f);
    run(ctx, s, 1, 0, 0, 0);
    lc = task_of(w, c) != 0;
    ld = task_of(w, d) != 0;
    le = task_of(w, e) != 0;
    CHECK(ta != 0 && tb != 0 && (ma == 2u || ma == 3u) && (mb == 2u || mb == 3u) && task_of(w, a) != 0 &&
          task_of(w, b) != 0 && lc && ld && !le, "A and B sparkling (modes %u / %u) keep 2 of the 4 units; C and D live (%u, "
          "%u), E refused (%u): no unit lost to counting A or B twice (0.9.4's passes left C, D and E without a task)",
          (unsigned)ma, (unsigned)mb, (unsigned)lc, (unsigned)ld, (unsigned)le);
    clear_recs(ctx);
    sa1_character_destroy(ctx, s);
    cfg_reset(ctx);
}

/* ---- round 0.9.4b: the registry's last entry (index 16383) had an id no call could reach (SA1SET_IDX_BITS 14) --------- */
static void t_idwrap(sa1_context *ctx)
{
    sa1set_registry *R;
    uint32_t i, id, bad = 0, live = 0;
    sa1_status st;
    printf("idwrap: %u adds and removes (every registry entry used, then reused): every id reachable, none left behind\n",
           (unsigned)(SA1_SET_MAX_RECORDS + 64u));
    clear_all(ctx);
    for (i = 0; i < SA1_SET_MAX_RECORDS + 64u; i++) {
        id = rec(ctx, SA1_OBJTYPE_RING, (float)(i % 100u) * 10.0f, 6.0f, 2000.0f, 0, 0, 0);
        st = sa1_set_state(ctx, id, SA1_SET_STATE_PRESENT, 0);
        bad += id == 0 || st != SA1_OK;
        st = sa1_set_remove(ctx, id);
        bad += st != SA1_OK;
        g_nrecs = 0;
    }
    R = sa1set_registry_of(ctx);
    for (i = 0; R != NULL && i < R->hiwater; i++) {
        live += R->ent[i].id != 0;
    }
    CHECK(bad == 0 && R != NULL && R->count == 0 && live == 0, "%u failed calls; the registry holds %u records (%u entries "
          "with an id) after removing all of them", (unsigned)bad, R != NULL ? (unsigned)R->count : 0u, (unsigned)live);
}

/* ---- main ---------------------------------------------------------------------------------------------------------- */
typedef struct test_case {
    const char *name;
    void (*fn)(sa1_context *);
} test_case;

static const test_case k_tests[] = {
    { "catalogue", t_catalogue }, { "norecords", t_norecords }, { "rings", t_rings }, { "groups", t_groups },
   
    { "groundbox", t_groundbox },
    { "airbox", t_airbox }, { "contents", t_contents }, { "forced_boxes", t_forced_boxes },
    { "rebuild", t_rebuild }, { "slots", t_slots }, { "boxwindow", t_boxwindow }, { "iconguard", t_iconguard }, { "touchwindow", t_touchwindow }, { "magnet", t_magnet }, { "bigeye", t_bigeye },
    { "churn", t_churn }, { "heapguard", t_heapguard }, { "kinds", t_kinds }, { "determinism", t_determinism }, { "capture", t_capture },
    /* round 0.9.4b (094b-lib-range) */
    { "r1", t_r1 }, { "r2", t_r2 }, { "r3", t_r3 }, { "r4", t_r4 }, { "r5", t_r5 }, { "r6", t_r6 }, { "r7", t_r7 },
    { "r8", t_r8 }, { "r9", t_r9 }, { "r10", t_r10 }, { "r11", t_r11 }, { "r12", t_r12 }, { "bigcircle", t_bigcircle },
    { "underfill", t_underfill }, { "idwrap", t_idwrap },
   
    /* TESTS_MORE */
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
    sa1_context *ctx = NULL;
    sa1_host h;
    sa1_coords co;
    sa1_load_options lo;
    sa1_status st;
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
        printf("set_test: needs the disc (GDI argument or SA1_TEST_GDI)\n");
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
    sa1_coords_preset(SA1_COORDS_PRESET_NATIVE, &co);
    sa1_set_coords(ctx, &co);
    memset(&lo, 0, sizeof lo);
    lo.struct_size = sizeof lo;
    lo.characters = (1u << SA1_CHAR_SONIC) | (1u << SA1_CHAR_TAILS) | (1u << SA1_CHAR_KNUCKLES) | (1u << SA1_CHAR_AMY) |
                    (1u << SA1_CHAR_GAMMA) | (1u << SA1_CHAR_BIG) | (1u << SA1_CHAR_SUPER_SONIC);
    lo.flags = SA1_LOAD_DEFAULT | SA1_LOAD_SUPER_FORMS;
    st = sa1_load_disc(ctx, g_gdi, &lo);
    if (SA1_FAILED(st)) {
        printf("sa1_load_disc: %s: %s\n", sa1_status_string(st), sa1_last_error(ctx));
        sa1_shutdown(ctx);
        return 1;
    }
    if (scene(ctx) != 0) {
        sa1_shutdown(ctx);
        return 1;
    }
    printf("libsa1 %s (SA1_API_SET %d)\n", sa1_version_string(), SA1_API_SET);
    for (i = 0; i < sizeof k_tests / sizeof k_tests[0]; i++) {
        if (!selected(only, k_tests[i].name)) {
            continue;
        }
        t0 = tu_now_ms();
        k_tests[i].fn(ctx);
        printf("  (%s: %.1f s)\n", k_tests[i].name, (tu_now_ms() - t0) / 1000.0);
    }
    sa1_shutdown(ctx);
    free(g_log.e);
    free(g_log.s);
    printf("set_test: %d ok, %d failed, %d tick faults\n", g_ok, g_fail, g_faults);
    return g_fail || g_faults ? 1 : 0;
}
