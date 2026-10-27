/*
 * super_test.c - round 6a: Super Sonic in libsa1 (include/libsa1.h "Super Sonic (API 0.4)"; sadc
 * docs/research/super_sonic.md 6.4 with its Review R.4, gsa1_rounds_plan.md 11.1 / 11.2 / 14.1a, gsa1_090_plan.md 7.3).
 *
 *   super_test [--only NAME | --from NAME] [--verbose] GDI
 *
 * Built and run by tests/build_super.py (a snapshot of build/<target>/libsa1.a and its headers). The context is in SA1
 * space (SA1_COORDS_PRESET_NATIVE: y up, 1 unit = 1 SA1 unit). GRID scenes use sa1_collision_set_static (the call G-SA1
 * makes); the DISC scenes use the Perfect Chaos stage's own landtable (sa1_collision_set_static_ex DISC level 19).
 *
 *   boot       GRID, kind 8 on a flat floor: by step 2 mode 71, equipment 0x8000, p.air_accel 0.05, p.center_height 6.4,
 *              the aura and physics tasks in the task lists, the level task stand-in and no trigger left, the 12 Super welds
 *              at inpmode 2, 50 rings, SA1_STATE_SUPER, no sea plane (GRID); the D_8C19C078 effect list twice on level 19
 *              (once in a level-1 Sonic world)
 *   render     the render of a Super pair: an additive (SRC_ALPHA / ONE) double-sided draw with SUPERSONIC textures (the
 *              aura), the body textured from SUPERSONIC and not SA1_DRAW_EFFECT
 *   drain      the drain: 50 -> 49 sixty steps after the first drain call, one ring per 60 steps; at 0 rings (start 1):
 *              equipment clear, mode 1, the revert fix (pwp->p equals Sonic's table entry 0, every byte), the aura and
 *              physics tasks gone, a run afterwards is a normal run; SA1_SUPER_FAITHFUL_REVERT keeps the game's bug
 *              (max_x_spd -0.008, run_accel -0.028, air_resist -0.001, air_resist_air -0.002)
 *   keep       SA1_SUPER_KEEP_AT_ZERO: Super at 0 rings for good; SA1_SUPER_NO_DRAIN: 50 rings for good
 *   whoosh     the aura's whoosh state machine: SA1_EVENT_SOUND 1035 above speed 7, 1036 (and the stop of 1035) above
 *              13, the stop of 1036 below 6
 *   disc       DISC level 19 (the stage's landtable): the sea plane only with SA1_SUPER_SEA_PLANE (default on without
 *              super_flags), it stays at the spawn for 600 steps and follows from then on; after the revert the plane
 *              sinks (mode 2) and about 100 steps later the death request (smode 50, item 0x4000)
 *   water      SA1_CREATE_WATER_FLOOR: Super Sonic stands and runs on a GRID WATER face, also with the host's water flags
 *              KNOWN | IN_WATER and KNOWN alone (WTR-A); without the flag he sinks to the floor
 *   transform  the owner's transformation (gsa1_rounds_plan.md 14.1a): a running super-capable Sonic turns Super with
 *              sa1_character_order(SA1_ORDER_SUPER) in his own world - position, facing, speed and rings carried - and
 *              back with SA1_ORDER_NORMAL (the revert fix: he runs on); a kind-8 world turned back takes SA1_ORDER_SUPER
 *              again; the alternative, a re-create as kind 8 at the same place, measured (the speed is lost)
 *   invalid    the refusals: another stage, another character, DISC collision of another stage, struct sizes, flags,
 *              orders to a world that is not super-capable
 *   determinism  "DETERMINISM <hash>" of a fixed scenario (build_super.py compares x86 and x64); round 0.9.3a: also
 *              "DETERMINISM_MASKED <hash>" (words naming the extension segment hashed as 0: review RC6 / R.2.4)
 *   kind8_trace  round 0.9.3a (review RC6): the Vs. Perfect Chaos Super Sonic's fixed scenario per pair, "KIND8_TRACE
 *              <hash>" (the behaviour gate: equal before and after a round; SUPER_TEST_TRACE=FILE writes the lines)
 * Round 0.9.3a, the General form (sadc docs/research/super_general_design.md 8.2 with its Review R.4):
 *   general_boot / general_welds / general_transform_air / general_look / general_moves / general_speed / general_revert /
 *   general_keep / general_start / general_link / general_cpu_pose / general_bitguard / general_aura / general_effects /
 *   aura_anim / idle_by_form / water_while_super / held_drawn / kind8_extra (each prints what it checks)
 * Round 0.9.4 (the 0.9.3a verifier's note, gsa1_094 round): general_reorder - SA1_ORDER_NORMAL then SA1_ORDER_SUPER with no
 *   pair between: one aura task and one physics task while Super (0.9.3a made a second of each); SUPER then NORMAL with no
 *   pair between: both free themselves
 * Round 0.9.4a (sadc docs/research/super2_plan.md 6.1 with its Review and the owner's answers 10.1; faces_wheel_plan.md 6.1):
 *   SA1_SUPOPT_SUPER_ANIMS (GA: the game's Super motions 130-134 for standing and moving, lifted by 6.4 - 5.4; the idle
 *   4 -> 5 -> 6 stays Sonic's: owner answer O1) - general_anims_table (1), _stand (2), _bands (3), _identity (4), _kept (5),
 *   _idle (6), _switch (7), _welds_carry (8), _cpu_pose (9), _hint (10), _extreme (10b: the owner's 0.9.4 note "the run
 *   animations break a little bit when moving at extreme speed": the render hint now starts at the drawn frame, sa1_world.h
 *   drawn_frame), _revert (11), _rebuild (12), _kind8 (13),
 *   _slope (14, measured), _lsd_aura (15, measured), general_effects_dust (16: SA1_SUPER_EFFECTS_DUST), supopt_contract
 *   (17: sa1w_world_lsd_gold), general_anims_memcheck (18; SA1_GUARD_ALLOC=1 for the host heap), lane_contract (19: the
 *   face / emote lanes' hooks, sa1w_world_super_lifted, sa1_state_ex's face / emote fields)
 */
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libsa1.h"
#include "sa1_internal.h"
#include "sa1_api_world.h"
#include "sa1_world.h"
#include "sa1_coladapt.h"
#include "sa1_hostobj.h"   /* sa1ho_world_heap: the game heap (general_memcheck) */
#include "sa1_capture.h"   /* sa1_character_lerp_stats: the interpolation plan (general_anims_extreme) */

/* the page-guard allocator (tests/guard_alloc.h): SA1_GUARD_ALLOC=1 puts libsa1's heap blocks against no-access pages */
#include "guard_alloc.c"
#define sa1_host_defaults(h) (sa1_host_defaults(h), ga_host(h))

static int g_verbose, g_fail, g_ok;
static uint32_t g_k8_trace, g_det_masked;      /* round 0.9.4a: the hashes of kind8_trace / determinism when they ran */
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

static const char *g_gdi;

static void log_cb(void *user, int32_t level, const char *msg)
{
    (void)user;
    if (level >= SA1_LOG_WARN || g_verbose) {
        printf("    [libsa1] %s\n", msg);
    }
}

/* ---- addresses (US v1.005) --------------------------------------------------------------------------------------------- */
#define A_TASK_LISTS    0x8C79F04Cu     /* task *B_8C79F04C[8] */
#define A_PLAYER_TWP    0x8C78C608u     /* playertwp[8] */
#define A_PLAYER_PWP    0x8C78C668u     /* playerpwp[8] */
#define A_RINGS         0x8C752BE4u     /* Sint16 */
#define A_PHYSICS       0x8C1BDFE4u     /* player_parameter[8], 0x84 each */
#define A_WELDS         0x8C1C5FA4u     /* SonicWeldList: PL_JOIN_VERTEX[], 0x18 each */
#define A_FX_LISTS      0x8C7A0D20u     /* the effect lists (next +0, desc +0x10) */
#define A_FX_DESC_19    0x8C19C058u     /* the descriptor of definition 0x8C19C06C (its list slot is D_8C19C078), which level
                                           19's table D_8C1C4140[19] lists a second time */
#define O_SUPER_OBJECT  0x0C909D2Cu     /* Super Sonic's model (the 12 welds' objptr) */
#define X_AURA_INIT     0x0C900480u
#define X_AURA          0x0C91D486u
#define X_PHYS_INIT     0x8C0B32B6u
#define X_PHYS          0x8C0B3280u
#define X_LEVEL         0x0C900000u
#define X_PLANE         0x0C91514Au
#define X_TRIGGER       0x0C914D60u

static uint32_t rd32(const sa1g_world *w, uint32_t g)
{
    uint32_t v = 0;
    sa1g_world_rd32(w, g, &v);
    return v;
}

static float rdf(const sa1g_world *w, uint32_t g)
{
    uint32_t v = rd32(w, g);
    float f;
    memcpy(&f, &v, 4);
    return f;
}

static void wrf(sa1g_world *w, uint32_t g, float f)
{
    uint32_t v;
    memcpy(&v, &f, 4);
    sa1g_world_wr32(w, g, v);
}

static int arena(uint32_t g)
{
    return g != 0 && ((g >> 24) & 31u) == SA1G_SEG_ARENA;
}

static uint32_t pwp0(const sa1g_world *w)
{
    uint32_t p = rd32(w, A_PLAYER_PWP);
    return arena(p) ? p : 0u;
}

static int16_t rings_of(const sa1g_world *w)
{
    return (int16_t)(rd32(w, A_RINGS) & 0xFFFFu);
}

/* the first task with exec `exec` (children too, depth first); its guest address or 0 */
static uint32_t find_in(const sa1g_world *w, uint32_t tp, uint32_t exec, int depth)
{
    uint32_t guard = 0;
    while (arena(tp) && guard++ < 4096u) {
        uint32_t c;
        if ((rd32(w, tp + 0x10u) & 0x1FFFFFFFu) == (exec & 0x1FFFFFFFu)) {
            return tp;
        }
        if (depth < 4 && (c = find_in(w, rd32(w, tp + 0x0Cu), exec, depth + 1)) != 0) {
            return c;
        }
        tp = rd32(w, tp);
    }
    return 0;
}

static uint32_t find_task(const sa1g_world *w, uint32_t exec)
{
    uint32_t l, t;
    for (l = 0; l < 8; l++) {
        if ((t = find_in(w, rd32(w, A_TASK_LISTS + 4u * l), exec, 0)) != 0) {
            return t;
        }
    }
    return 0;
}

/* ---- geometry ----------------------------------------------------------------------------------------------------- */
typedef struct tbuf {
    sa1_collision_tri *t;
    uint32_t n, cap;
} tbuf;

static void tb_tri(tbuf *b, const float a[3], const float c[3], const float d[3], uint32_t s)
{
    sa1_collision_tri *t;
    if (b->n == b->cap) {
        b->cap = b->cap ? 2u * b->cap : 64u;
        b->t = (sa1_collision_tri *)realloc(b->t, (size_t)b->cap * sizeof *b->t);
    }
    t = &b->t[b->n++];
    memset(t, 0, sizeof *t);
    memcpy(t->v[0], a, 12);
    memcpy(t->v[1], c, 12);
    memcpy(t->v[2], d, 12);
    t->surface = s;
}

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

static void tb_floor(tbuf *b, float x0, float x1, float z0, float z1, float y, float up, uint32_t s)
{
    float p[4][3] = { { x0, y, z0 }, { x1, y, z0 }, { x1, y, z1 }, { x0, y, z1 } };
    float n[3] = { 0.0f, up, 0.0f };
    tb_quad(b, p, n, s);
}

static void tb_wall_x(tbuf *b, float x, float y0, float y1, float z0, float z1, float nx, uint32_t s)
{
    float p[4][3] = { { x, y0, z0 }, { x, y0, z1 }, { x, y1, z1 }, { x, y1, z0 } };
    float n[3] = { nx, 0.0f, 0.0f };
    tb_quad(b, p, n, s);
}

static void tb_wall_z(tbuf *b, float z, float y0, float y1, float x0, float x1, float nz, uint32_t s)
{
    float p[4][3] = { { x0, y0, z }, { x1, y0, z }, { x1, y1, z }, { x0, y1, z } };
    float n[3] = { 0.0f, 0.0f, nz };
    tb_quad(b, p, n, s);
}

static void tb_box(tbuf *b, float x0, float x1, float y0, float y1, float z0, float z1, uint32_t s)
{
    tb_floor(b, x0, x1, z0, z1, y1, 1.0f, s);
    tb_floor(b, x0, x1, z0, z1, y0, -1.0f, s);
    tb_wall_x(b, x0, y0, y1, z0, z1, -1.0f, s);
    tb_wall_x(b, x1, y0, y1, z0, z1, 1.0f, s);
    tb_wall_z(b, z0, y0, y1, x0, x1, -1.0f, s);
    tb_wall_z(b, z1, y0, y1, x0, x1, 1.0f, s);
}

/* round 0.9.4a: the context's collision segment is a bump allocator that keeps the GRID of every geometry generation a world
 * was built with (worlds of older generations keep theirs: src/world/sa1_api_world.c faithful_landtable), and this suite's
 * cases switch scenes often in one context (a scene_big of 20000 is 13 MB of GRID): from the 0.9.4a cases on the segment
 * ran full ("collision adapter: no room for ... bytes of GRID collision"). A scene helper asked for the scene the context
 * already has keeps it (the same geometry: nothing a case sees changes); a direct sa1_collision_set_static* forgets it. */
static struct {
    const sa1_context *ctx;
    int kind, water;
    float h, top;
} g_scene;
/* the runner turns the cache on at the first 0.9.4a case (general_anims_table): the cases before it set every scene anew
   exactly as in 0.9.4, so the GRID's place in the collision segment - which the arena hashes DETERMINISM / _MASKED see
   through the landtable pointers - stays the 0.9.4 baseline's */
static int g_scene_cache;

static int scene_same(const sa1_context *ctx, int kind, float h, int water, float top)
{
    return g_scene_cache && g_scene.ctx == ctx && g_scene.kind == kind && g_scene.h == h && g_scene.water == water &&
           g_scene.top == top;
}

static void scene_note(const sa1_context *ctx, int kind, float h, int water, float top)
{
    g_scene.ctx = ctx;
    g_scene.kind = kind;
    g_scene.h = h;
    g_scene.water = water;
    g_scene.top = top;
}

static void scene_forget(void)
{
    memset(&g_scene, 0, sizeof g_scene);
}

/* a flat floor at y = 0 (6000 x 6000), or a WATER box (top y = 0) over a floor at y = -100 */
static int scene_grid(sa1_context *ctx, int water)
{
    tbuf b;
    sa1_status st;
    if (scene_same(ctx, 1, 0.0f, water, 0.0f)) {
        return 0;
    }
    scene_forget();
    memset(&b, 0, sizeof b);
    if (water) {
        tb_floor(&b, -3000, 3000, -3000, 3000, -100.0f, 1.0f, SA1_SURFACE_SOLID);
        tb_box(&b, -2000, 2000, -100.0f, 0.0f, -2000, 2000, SA1_SURFACE_WATER);
    } else {
        tb_floor(&b, -3000, 3000, -3000, 3000, 0.0f, 1.0f, SA1_SURFACE_SOLID);
    }
    st = sa1_collision_set_static(ctx, b.t, b.n);
    free(b.t);
    if (SA1_FAILED(st)) {
        printf("  set_static: %s: %s\n", sa1_status_string(st), sa1_last_error(ctx));
        return -1;
    }
    scene_note(ctx, 1, 0.0f, water, 0.0f);
    return 0;
}

static int scene_disc(sa1_context *ctx)
{
    sa1_collision_static d;
    sa1_status st;
    if (scene_same(ctx, 4, 19.0f, 0, 0.0f)) {
        return 0;
    }
    scene_forget();
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.mode = SA1_COLLISION_MODE_DISC;
    d.disc_level = 19;
    d.disc_act = 0;
    st = sa1_collision_set_static_ex(ctx, &d);
    if (st != SA1_OK) {
        printf("  set_static_ex(DISC 19): %s: %s\n", sa1_status_string(st), sa1_last_error(ctx));
        return -1;
    }
    scene_note(ctx, 4, 19.0f, 0, 0.0f);
    return 0;
}

/* a character in SA1 space; super_given: pass super_flags with SA1_CREATE_SUPER */
static sa1_character_id mk(sa1_context *ctx, int32_t kind, const float pos[3], uint32_t flags, int32_t rings,
                           int super_given, uint32_t super_flags, sa1_status *st_out)
{
    sa1_character_desc d;
    sa1_character_id id = 0;
    sa1_status st;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.kind = kind;
    d.flags = flags | SA1_CREATE_WORLD | SA1_CREATE_SA1_SPACE | SA1_CREATE_ANGLES;
    if (pos != NULL) {
        memcpy(d.position, pos, sizeof d.position);
    } else {
        d.flags |= SA1_CREATE_START_TABLE;
    }
    if (rings >= 0) {
        d.flags |= SA1_CREATE_START_RINGS;
        d.start_rings = rings;
    }
    if (super_given) {
        d.flags |= SA1_CREATE_SUPER;
        d.super_flags = super_flags;
    }
    st = sa1_character_create_ex(ctx, &d, &id);
    if (st_out != NULL) {
        *st_out = st;
    }
    if (SA1_FAILED(st)) {
        if (st_out == NULL) {
            printf("  create: %s: %s\n", sa1_status_string(st), sa1_last_error(ctx));
        }
        return 0;
    }
    return id;
}

/* ---- running ------------------------------------------------------------------------------------------------------- */
typedef struct evlog {
    sa1_event *e;
    uint32_t n, cap;
} evlog;

static uint32_t g_host_flags;

static int tick(sa1_context *ctx, sa1_character_id id, float sx, float sy, uint32_t buttons, evlog *L, sa1_state_ex *x)
{
    sa1_input in;
    sa1_event ev[64];
    uint32_t n = 0, k;
    sa1_status st;
    memset(&in, 0, sizeof in);
    in.struct_size = sizeof in;
    in.stick_x = sx;
    in.stick_y = sy;
    in.buttons = buttons;
    in.camera_yaw = 0.0f;
    in.host_flags = g_host_flags;
    st = sa1_character_tick(ctx, id, &in, 1.0f / 30.0f, NULL, NULL);
    if (st != SA1_OK) {
        printf("  tick: %s: %s\n", sa1_status_string(st), sa1_last_error(ctx));
        return -1;
    }
    if (x != NULL) {
        memset(x, 0, sizeof *x);
        x->struct_size = sizeof *x;
        sa1_character_state_ex(ctx, id, x);
    }
    do {
        n = 0;
        sa1_poll_events(ctx, ev, 64, &n);
        for (k = 0; k < n && L != NULL; k++) {
            if (L->n == L->cap) {
                L->cap = L->cap ? 2u * L->cap : 256u;
                L->e = (sa1_event *)realloc(L->e, (size_t)L->cap * sizeof *L->e);
            }
            L->e[L->n++] = ev[k];
        }
    } while (n == 64);
    return 0;
}

static int run(sa1_context *ctx, sa1_character_id id, int pairs, float sx, float sy, evlog *L, sa1_state_ex *x)
{
    int i;
    for (i = 0; i < pairs; i++) {
        if (tick(ctx, id, sx, sy, 0, L, x) != 0) {
            return -1;
        }
    }
    return 0;
}

static const sa1_event *ev_find(const evlog *L, uint32_t type, uint32_t id, uint32_t from, uint32_t *count)
{
    const sa1_event *f = NULL;
    uint32_t i, c = 0;
    for (i = from; i < L->n; i++) {
        const sa1_event *e = &L->e[i];
        if (e->type == type && (id == 0xFFFFFFFFu || e->id == id)) {
            if (f == NULL) {
                f = e;
            }
            c++;
        }
    }
    if (count != NULL) {
        *count = c;
    }
    return f;
}

static int32_t archive_named(const sa1_context *ctx, int32_t kind, const char *name)
{
    uint32_t a;
    for (a = 0; a < 16u; a++) {
        const char *nm = NULL;
        uint32_t cnt = 0;
        if (sa1_texture_archive(ctx, kind, a, &nm, &cnt) != SA1_OK) {
            break;
        }
        if (nm != NULL && strcmp(nm, name) == 0) {
            return (int32_t)a;
        }
    }
    return -1;
}

/* the 12 Super Sonic weld entries at inpmode 2 (SonicMain mode 0 on stage 0x13xx) */
static void welds_of(const sa1g_world *w, uint32_t *n_super, uint32_t *n_on)
{
    uint32_t e, k;
    *n_super = *n_on = 0;
    for (k = 0, e = A_WELDS; k < 64u; k++, e += 0x18u) {
        uint32_t obj = rd32(w, e);
        if (obj == 0) {
            break;
        }
        if ((obj & 0x1FFFFFFFu) == (O_SUPER_OBJECT & 0x1FFFFFFFu)) {
            (*n_super)++;
            *n_on += (rd32(w, e + 0x0Cu) >> 8 & 0xFFu) == 2u;
        }
    }
}

/* effect lists whose descriptor is desc */
static uint32_t fx_lists(const sa1g_world *w, uint32_t desc)
{
    uint32_t l = rd32(w, A_FX_LISTS), n = 0, guard = 0;
    while (arena(l) && guard++ < 1024u) {
        n += (rd32(w, l + 0x10u) & 0x1FFFFFFFu) == (desc & 0x1FFFFFFFu);
        l = rd32(w, l);
    }
    return n;
}

/* playerwk.p of player 0 against Sonic's table entry: the fields that differ (bit mask of the 33 words) */
static uint64_t phys_diff(const sa1g_world *w, uint32_t pwp)
{
    uint64_t m = 0;
    uint32_t k;
    for (k = 0; k < 0x84u / 4u; k++) {
        if (rd32(w, pwp + 0x9Cu + 4u * k) != rd32(w, A_PHYSICS + 4u * k)) {
            m |= (uint64_t)1 << k;
        }
    }
    return m;
}

/* ---- boot ---------------------------------------------------------------------------------------------------------- */
static void t_boot(sa1_context *ctx)
{
    static const float at[3] = { 0.0f, 0.5f, 0.0f };
    sa1_character_id id, son;
    sa1_state_ex x;
    sa1g_world *w;
    uint32_t pwp, ns = 0, non = 0, aura, phys, lvl, trig, plane;
    float aa, ch;
    int i, step_super = -1;
    printf("boot (GRID): Super Sonic on a flat floor\n");
    if (scene_grid(ctx, 0) != 0 || (id = mk(ctx, SA1_CHAR_SUPER_SONIC, at, 0, -1, 0, 0, NULL)) == 0) {
        g_fail++;
        return;
    }
    w = sa1_character_world(ctx, id);
    CHECK(w != NULL && w->kind == SA1_CHAR_SUPER_SONIC && w->chr == 0 && w->level == 19 && w->act == 0 &&
              (w->super & SA1G_SUPER_WORLD) && (w->super & SA1G_SUPER_TRIGGER) && !(w->super & SA1G_SUPER_PLANE),
          "the world: kind %d, game character %d, stage %d act %d, super bits %X (WORLD | TRIGGER, no PLANE on GRID)",
          w ? (int)w->kind : -1, w ? (int)w->chr : -1, w ? (int)w->level : -1, w ? (int)w->act : -1, w ? (unsigned)w->super : 0u);
    if (w == NULL) {
        return;
    }
    printf("    stage tasks: %s\n", w->stage_note);
    for (i = 0; i < 4; i++) {
        if (tick(ctx, id, 0.0f, 0.0f, 0, NULL, &x) != 0) {
            return;
        }
        if (step_super < 0 && (x.equipment & 0x8000u)) {
            step_super = (int)x.step - 2;   /* the pair's first step */
        }
        if (g_verbose) {
            printf("    pair %d: step %u mode %d smode %d action %d equipment %04X rings %d\n", i, (unsigned)x.step,
                   (int)x.mode, (int)x.smode, (int)x.action, (unsigned)x.equipment, (int)x.rings);
        }
        if (i == 1) {
            pwp = pwp0(w);
            aa = rdf(w, pwp + 0x9Cu + 0x40u);
            ch = rdf(w, pwp + 0x9Cu + 0x80u);
            CHECK((x.equipment & 0x8000u) && x.mode == 71 && x.req_action == 130,
                  "after step 3: equipment %04X, mode %d, reqaction %d (71 / 130 from step 2)", (unsigned)x.equipment,
                  (int)x.mode, (int)x.req_action);
            /* the physics task's pool words 0x3D4CCCCC (0.05, rounded down) and 0x40CCCCCC (6.4) */
            CHECK(rd32(w, pwp + 0x9Cu + 0x40u) == 0x3D4CCCCCu && rd32(w, pwp + 0x9Cu + 0x80u) == 0x40CCCCCCu,
                  "p.air_accel %.9g (0.05: 3D4CCCCC), p.center_height %.9g (6.4: 40CCCCCC)", (double)aa, (double)ch);
            aura = find_task(w, X_AURA) ? find_task(w, X_AURA) : find_task(w, X_AURA_INIT);
            phys = find_task(w, X_PHYS) ? find_task(w, X_PHYS) : find_task(w, X_PHYS_INIT);
            lvl = find_task(w, X_LEVEL);
            trig = find_task(w, X_TRIGGER);
            plane = find_task(w, X_PLANE);
            CHECK(aura != 0 && phys != 0 && lvl != 0 && trig == 0 && plane == 0 && rd32(w, 0x8C752BD4u) == lvl,
                  "tasks: aura %08X, physics %08X, level task %08X (B_8C752BD4 %08X), trigger gone (%08X), no sea plane "
                  "(%08X)", (unsigned)aura, (unsigned)phys, (unsigned)lvl, (unsigned)rd32(w, 0x8C752BD4u), (unsigned)trig,
                  (unsigned)plane);
            welds_of(w, &ns, &non);
            CHECK(ns == 12 && non == 12, "the Super welds: %u of SonicWeldList's entries are Super Sonic's, %u at inpmode 2",
                  (unsigned)ns, (unsigned)non);
            CHECK(x.rings == 50 && (x.flags & SA1_STATE_SUPER), "rings %d (50), SA1_STATE_SUPER %s", (int)x.rings,
                  (x.flags & SA1_STATE_SUPER) ? "set" : "clear");
        }
    }
    CHECK(step_super == 2, "Super in the pair of steps %d-%d (the trigger's second step is step 2)", step_super,
          step_super + 1);
    /* the level's effect list (super_sonic.md Review R.3.1): D_8C19C078 twice on level 19, once on level 1 */
    son = mk(ctx, SA1_CHAR_SONIC, at, 0, -1, 0, 0, NULL);
    CHECK(son != 0 && fx_lists(w, A_FX_DESC_19) == 2 && fx_lists(sa1_character_world(ctx, son), A_FX_DESC_19) == 1,
          "the D_8C19C078 effect list: %u in the level-19 world, %u in a level-1 Sonic world", (unsigned)fx_lists(w,
          A_FX_DESC_19), son ? (unsigned)fx_lists(sa1_character_world(ctx, son), A_FX_DESC_19) : 0u);
    sa1_character_destroy(ctx, son);
    sa1_character_destroy(ctx, id);
}

/* ---- render -------------------------------------------------------------------------------------------------------- */
/* the draws of the last pair: additive SUPERSONIC draws (the aura) and body draws (SA1_SOURCE_CHARACTER) of an archive */
typedef struct rstat {
    uint32_t additive_ss, additive_ds, body, body_effect, body_arch_other;
    uint32_t untextured_opaque;     /* triangle draws without a texture (or NO_TEXTURE) and not blended */
} rstat;

static int render_stats(sa1_context *ctx, sa1_character_id id, int32_t arch_ss, int32_t arch_body, rstat *r)
{
    const sa1_render *rd = NULL;
    uint32_t i;
    memset(r, 0, sizeof *r);
    if (sa1_character_render(ctx, id, 1.0f, &rd) != SA1_OK || rd == NULL) {
        return -1;
    }
    for (i = 0; i < rd->num_draws; i++) {
        const sa1_draw *d = &rd->draws[i];
        if (g_verbose) {
            printf("    draw %u: prim %u source %u archive %d texture %d flags %04X blend %u/%u\n", (unsigned)i,
                   (unsigned)d->prim, (unsigned)d->source, (int)d->archive, (int)d->texture, (unsigned)d->flags,
                   (unsigned)d->blend_src, (unsigned)d->blend_dst);
        }
        if (d->prim != SA1_PRIM_TRIANGLES) {
            continue;
        }
        if ((d->archive < 0 || (d->flags & SA1_DRAW_NO_TEXTURE)) && !(d->flags & SA1_DRAW_BLEND)) {
            r->untextured_opaque++;
        }
        if ((d->flags & SA1_DRAW_ADDITIVE) && d->archive == arch_ss) {
            r->additive_ss++;
            r->additive_ds += (d->flags & SA1_DRAW_DOUBLE_SIDED) != 0;
        }
        /* the body: the character renderer's draws but the aura (additive; drawn through the same renderer) */
        if (d->source == SA1_SOURCE_CHARACTER && !(d->flags & (SA1_DRAW_SHADOW | SA1_DRAW_ADDITIVE)) && d->archive >= 0) {
            r->body++;
            r->body_effect += (d->flags & SA1_DRAW_EFFECT) != 0;
            r->body_arch_other += d->archive != arch_body;
        }
    }
    return 0;
}

static void t_render(sa1_context *ctx)
{
    static const float at[3] = { 0.0f, 0.5f, 0.0f };
    sa1_character_id id;
    int32_t a_ss, a_son;
    rstat r;
    printf("render (GRID): the draws of Super pairs\n");
    if (scene_grid(ctx, 0) != 0 || (id = mk(ctx, SA1_CHAR_SUPER_SONIC, at, 0, -1, 0, 0, NULL)) == 0) {
        g_fail++;
        return;
    }
    a_ss = archive_named(ctx, SA1_CHAR_SUPER_SONIC, "SUPERSONIC");
    a_son = archive_named(ctx, SA1_CHAR_SUPER_SONIC, "SONIC");
    CHECK(a_ss == 0 && a_son == 4 && archive_named(ctx, SA1_CHAR_SUPER_SONIC, "SON_EFF") == 5 &&
              archive_named(ctx, SA1_CHAR_SUPER_SONIC, "EFF_REGULAR") == 2 &&
              archive_named(ctx, SA1_CHAR_SONIC, "SUPERSONIC") == 5 && archive_named(ctx, SA1_CHAR_SONIC, "SONIC") == 0,
          "archives: Super Sonic SUPERSONIC %d, EFF_REGULAR %d, SONIC %d, SON_EFF %d; Sonic SONIC %d, SUPERSONIC %d", a_ss,
          archive_named(ctx, SA1_CHAR_SUPER_SONIC, "EFF_REGULAR"), a_son, archive_named(ctx, SA1_CHAR_SUPER_SONIC, "SON_EFF"),
          archive_named(ctx, SA1_CHAR_SONIC, "SONIC"), archive_named(ctx, SA1_CHAR_SONIC, "SUPERSONIC"));
    if (run(ctx, id, 20, 0.0f, 0.0f, NULL, NULL) != 0 || render_stats(ctx, id, a_ss, a_ss, &r) != 0) {
        g_fail++;
        sa1_character_destroy(ctx, id);
        return;
    }
    CHECK(r.additive_ss > 0 && r.additive_ds == r.additive_ss,
          "standing: %u additive draws with SUPERSONIC textures (the aura), %u of them double-sided", (unsigned)r.additive_ss,
          (unsigned)r.additive_ds);
    CHECK(r.body > 0 && r.body_effect == 0 && r.body_arch_other == 0,
          "the body: %u draws, %u flagged SA1_DRAW_EFFECT, %u from another archive than SUPERSONIC", (unsigned)r.body,
          (unsigned)r.body_effect, (unsigned)r.body_arch_other);
    CHECK(r.untextured_opaque == 0, "no untextured opaque draw (%u; lightspeed_visuals.md 7's audit rule)",
          (unsigned)r.untextured_opaque);
    /* running: the aura's dash models (speed above 7 / 13) and the effect clones */
    {
        rstat r2;
        int ok = 1, p;
        uint32_t add = 0, uo = 0;
        for (p = 0; p < 60 && ok; p++) {
            sa1g_world *w = sa1_character_world(ctx, id);
            uint32_t pwp = pwp0(w);
            if (pwp != 0) {
                wrf(w, pwp + 0x38u, p < 30 ? 9.0f : 14.0f);
            }
            ok = tick(ctx, id, 0.0f, 1.0f, 0, NULL, NULL) == 0 && render_stats(ctx, id, a_ss, a_ss, &r2) == 0;
            add += r2.additive_ss;
            uo += r2.untextured_opaque;
        }
        CHECK(ok && add > 0 && uo == 0, "running at speed 9 / 14 for 60 pairs: %u additive SUPERSONIC draws, %u untextured "
              "opaque draws", (unsigned)add, (unsigned)uo);
    }
    sa1_character_destroy(ctx, id);
}

/* ---- the drain and the revert ------------------------------------------------------------------------------------------ */
/* the steps (pair granularity: the first step of the pair) at which the ring count first went below `from` */
static int pair_of_ring(sa1_context *ctx, sa1_character_id id, int16_t below, int max_pairs, sa1_state_ex *x)
{
    int i;
    for (i = 0; i < max_pairs; i++) {
        if (tick(ctx, id, 0.0f, 0.0f, 0, NULL, x) != 0) {
            return -1;
        }
        if (x->rings < below) {
            return (int)x->step - 2;
        }
    }
    return -1;
}

static void t_drain(sa1_context *ctx)
{
    static const float at[3] = { 0.0f, 0.5f, 0.0f };
    sa1_character_id id;
    sa1_state_ex x;
    sa1g_world *w;
    int s49, s48, f;
    printf("drain (GRID): the ring drain 0C917088 and the revert at 0 rings\n");
    if (scene_grid(ctx, 0) != 0 || (id = mk(ctx, SA1_CHAR_SUPER_SONIC, at, 0, -1, 0, 0, NULL)) == 0) {
        g_fail++;
        return;
    }
    s49 = pair_of_ring(ctx, id, 50, 100, &x);
    s48 = pair_of_ring(ctx, id, 49, 100, &x);
    w = sa1_character_world(ctx, id);
    /* the stand-in's first drain call is in step 3 (Super from step 2, after the level task in its list); the 60th call
       takes the ring: step 62 */
    CHECK(s49 == 62 && s48 == 122 && w->super_drains > 100, "50 -> 49 in the pair of step %d (62), 49 -> 48 in %d (122); "
          "%u drain calls", s49, s48, (unsigned)w->super_drains);
    sa1_character_destroy(ctx, id);
    for (f = 0; f < 2; f++) {
        uint32_t pwp;
        uint64_t diff;
        int i, srev = -1;
        float vmax = 0.0f;
        id = mk(ctx, SA1_CHAR_SUPER_SONIC, at, 0, 1, f, f ? SA1_SUPER_FAITHFUL_REVERT : 0u, NULL);
        if (id == 0) {
            g_fail++;
            return;
        }
        w = sa1_character_world(ctx, id);
        for (i = 0; i < 60 && srev < 0; i++) {
            if (tick(ctx, id, 0.0f, 0.0f, 0, NULL, &x) != 0) {
                break;
            }
            if (i > 2 && !(x.equipment & 0x8000u)) {
                srev = (int)x.step - 2;
            }
        }
        pwp = pwp0(w);
        diff = phys_diff(w, pwp);
        if (!f) {
            CHECK(srev == 62 && x.rings == 0 && x.mode == 1 && !(x.flags & SA1_STATE_SUPER),
                  "start 1 ring: back to normal Sonic in the pair of step %d (62: the drain at 0 sends request 47), rings "
                  "%d, mode %d", srev, (int)x.rings, (int)x.mode);
            CHECK(diff == 0 && w->super_reverts == 1, "the revert fix: playerwk.p equals Sonic's table entry (differing "
                  "words %llX), %u fix", (unsigned long long)diff, (unsigned)w->super_reverts);
        } else {
            /* the bug: max_x_spd (word 4) = -0.008, run_accel (15) = -0.028, air_resist_air (20) = -0.002, air_resist
               (21) = -0.001 */
            CHECK(diff == (((uint64_t)1 << 4) | ((uint64_t)1 << 15) | ((uint64_t)1 << 20) | ((uint64_t)1 << 21)) &&
                      rd32(w, pwp + 0xACu) == rd32(w, A_PHYSICS + 0x54u) && rd32(w, pwp + 0xD8u) == rd32(w, A_PHYSICS + 0x50u) &&
                      rd32(w, pwp + 0xF0u) == 0xBA83126Eu && rd32(w, pwp + 0xECu) == 0xBB03126Eu && w->super_reverts == 0,
                  "SA1_SUPER_FAITHFUL_REVERT: the game's bug (words %llX: max_x_spd %.9g, run_accel %.9g, air_resist %.9g, "
                  "air_resist_air %.9g)", (unsigned long long)diff, (double)rdf(w, pwp + 0xACu), (double)rdf(w, pwp + 0xD8u),
                  (double)rdf(w, pwp + 0xF0u), (double)rdf(w, pwp + 0xECu));
        }
        if (run(ctx, id, 2, 0.0f, 0.0f, NULL, &x) == 0) {
            CHECK(find_task(w, X_AURA) == 0 && find_task(w, X_PHYS) == 0, "%s: the aura and physics tasks freed themselves",
                  f ? "faithful" : "fixed");
        }
        for (i = 0; i < 90; i++) {
            if (tick(ctx, id, 0.0f, 1.0f, 0, NULL, &x) != 0) {
                break;
            }
            if (x.player_speed[0] > vmax) {
                vmax = x.player_speed[0];
            }
        }
        if (!f) {
            CHECK(vmax > 3.0f && x.mode >= 2 && x.mode <= 4, "fixed: the stick forward for 90 pairs: top speed %.3f, mode %d "
                  "(a normal run)", (double)vmax, (int)x.mode);
        } else {
            printf("  info  faithful: the stick forward for 90 pairs: top speed %.3f, mode %d (the game's bug; in the game "
                   "the sea plane kills him first)\n", (double)vmax, (int)x.mode);
        }
        sa1_character_destroy(ctx, id);
    }
}

static void t_keep(sa1_context *ctx)
{
    static const float at[3] = { 0.0f, 0.5f, 0.0f };
    sa1_character_id id;
    sa1_state_ex x;
    printf("keep (GRID): SA1_SUPER_KEEP_AT_ZERO, SA1_SUPER_NO_DRAIN\n");
    if (scene_grid(ctx, 0) != 0 || (id = mk(ctx, SA1_CHAR_SUPER_SONIC, at, 0, 1, 1, SA1_SUPER_KEEP_AT_ZERO, NULL)) == 0) {
        g_fail++;
        return;
    }
    if (run(ctx, id, 150, 0.0f, 0.0f, NULL, &x) == 0) {
        CHECK(x.rings == 0 && (x.equipment & 0x8000u) && x.mode >= 71 && x.mode <= 78,
              "KEEP_AT_ZERO, 1 ring, 300 steps: rings %d, equipment %04X, mode %d (Super)", (int)x.rings,
              (unsigned)x.equipment, (int)x.mode);
    }
    sa1_character_destroy(ctx, id);
    if ((id = mk(ctx, SA1_CHAR_SUPER_SONIC, at, 0, -1, 1, SA1_SUPER_NO_DRAIN, NULL)) == 0) {
        g_fail++;
        return;
    }
    if (run(ctx, id, 150, 0.0f, 0.0f, NULL, &x) == 0) {
        CHECK(x.rings == 50 && (x.equipment & 0x8000u) && sa1_character_world(ctx, id)->super_drains == 0,
              "NO_DRAIN, 300 steps: rings %d, equipment %04X, %u drain calls", (int)x.rings, (unsigned)x.equipment,
              (unsigned)sa1_character_world(ctx, id)->super_drains);
    }
    sa1_character_destroy(ctx, id);
}

/* ---- the aura's whoosh sounds ------------------------------------------------------------------------------------------ */
/* playerwk.spd.x set before a pair (the aura reads it after the player in each step) */
static int pair_at_speed(sa1_context *ctx, sa1_character_id id, float spd, evlog *L, sa1_state_ex *x)
{
    sa1g_world *w = sa1_character_world(ctx, id);
    uint32_t pwp = pwp0(w);
    if (pwp == 0) {
        return -1;
    }
    wrf(w, pwp + 0x38u, spd);
    return tick(ctx, id, 0.0f, 0.0f, 0, L, x);
}

static void t_whoosh(sa1_context *ctx)
{
    static const float at[3] = { 0.0f, 0.5f, 0.0f };
    sa1_character_id id;
    sa1_state_ex x;
    evlog L;
    uint32_t n35 = 0, n36 = 0, s35 = 0, s36 = 0, mark;
    printf("whoosh (GRID): the aura's sound state machine (7 / 13 / 6)\n");
    memset(&L, 0, sizeof L);
    if (scene_grid(ctx, 0) != 0 ||
        (id = mk(ctx, SA1_CHAR_SUPER_SONIC, at, 0, -1, 1, SA1_SUPER_NO_DRAIN, NULL)) == 0) {
        g_fail++;
        return;
    }
    if (run(ctx, id, 10, 0.0f, 0.0f, &L, &x) != 0) {
        g_fail++;
        return;
    }
    ev_find(&L, SA1_EVENT_SOUND, 1035u, 0, &n35);
    ev_find(&L, SA1_EVENT_SOUND, 1036u, 0, &n36);
    CHECK(n35 == 0 && n36 == 0, "standing: no whoosh (%u / %u)", (unsigned)n35, (unsigned)n36);
    mark = L.n;
    pair_at_speed(ctx, id, 8.5f, &L, &x);
    ev_find(&L, SA1_EVENT_SOUND, 1035u, mark, &n35);
    ev_find(&L, SA1_EVENT_SOUND, 1036u, mark, &n36);
    CHECK(n35 == 1 && n36 == 0, "speed 8.5 (above 7): SOUND 1035 x%u, 1036 x%u (speed after the pair %.3f)", (unsigned)n35,
          (unsigned)n36, (double)x.player_speed[0]);
    mark = L.n;
    pair_at_speed(ctx, id, 14.5f, &L, &x);
    ev_find(&L, SA1_EVENT_SOUND, 1036u, mark, &n36);
    ev_find(&L, SA1_EVENT_SOUND_STOP, 1035u, mark, &s35);
    CHECK(n36 == 1 && s35 == 1, "speed 14.5 (above 13): SOUND 1036 x%u, SOUND_STOP 1035 x%u", (unsigned)n36, (unsigned)s35);
    mark = L.n;
    pair_at_speed(ctx, id, 2.0f, &L, &x);
    ev_find(&L, SA1_EVENT_SOUND_STOP, 1036u, mark, &s36);
    CHECK(s36 == 1, "speed 2 (below 6): SOUND_STOP 1036 x%u", (unsigned)s36);
    if (g_verbose) {
        uint32_t i;
        for (i = 0; i < L.n; i++) {
            if (L.e[i].type == SA1_EVENT_SOUND || L.e[i].type == SA1_EVENT_SOUND_STOP) {
                printf("    step %u type %u id %u bank %u\n", (unsigned)L.e[i].step, (unsigned)L.e[i].type,
                       (unsigned)L.e[i].id, (unsigned)L.e[i].bank);
            }
        }
    }
    free(L.e);
    sa1_character_destroy(ctx, id);
}

/* ---- DISC level 19: the sea plane -------------------------------------------------------------------------------------- */
static void t_disc(sa1_context *ctx)
{
    sa1_character_id id;
    sa1_state_ex x;
    sa1g_world *w;
    uint32_t plane, ptwp;
    float sx = 0.0f, sz = 0.0f, px, pz;
    int k, i, mode2 = -1, death = -1;
    printf("disc (DISC level 19, the stage's landtable): the sea plane\n");
    if (scene_disc(ctx) != 0) {
        g_fail++;
        return;
    }
    for (k = 0; k < 3; k++) {
        /* 0: no super_flags (the default: the plane); 1: super_flags 0; 2: SA1_SUPER_SEA_PLANE */
        id = mk(ctx, SA1_CHAR_SUPER_SONIC, NULL, 0, -1, k != 0, k == 2 ? (SA1_SUPER_SEA_PLANE | SA1_SUPER_NO_DRAIN) :
                SA1_SUPER_NO_DRAIN, NULL);
        if (id == 0 || run(ctx, id, 2, 0.0f, 0.0f, NULL, &x) != 0) {
            g_fail++;
            continue;
        }
        w = sa1_character_world(ctx, id);
        plane = find_task(w, X_PLANE);
        CHECK((plane != 0) == (k != 1) && (x.equipment & 0x8000u) && w->level == 19,
              "%s: sea plane task %08X, equipment %04X", k == 0 ? "no super_flags" : k == 1 ? "super_flags 0" :
              "SA1_SUPER_SEA_PLANE", (unsigned)plane, (unsigned)x.equipment);
        if (k != 2 || plane == 0) {
            sa1_character_destroy(ctx, id);
            continue;
        }
        ptwp = rd32(w, plane + 0x20u);
        sx = rdf(w, ptwp + 0x20u);
        sz = rdf(w, ptwp + 0x28u);
        /* stand 290 pairs (to step 584) on the plane at the spawn: it stays */
        run(ctx, id, 290, 0.0f, 0.0f, NULL, &x);
        px = rdf(w, ptwp + 0x20u);
        pz = rdf(w, ptwp + 0x28u);
        CHECK(px == sx && pz == sz && x.sa1_position[1] > -1.0f && x.sa1_position[1] < 1.0f,
              "step %u: the plane at the spawn (%.2f %.2f) = (%.2f %.2f); Sonic on it at y %.3f", (unsigned)x.step,
              (double)sx, (double)sz, (double)px, (double)pz, (double)x.sa1_position[1]);
        /* run from step 620 on: from step 600 the plane follows */
        run(ctx, id, 20, 0.0f, 0.0f, NULL, &x);
        run(ctx, id, 60, 0.0f, 1.0f, NULL, &x);
        px = rdf(w, ptwp + 0x20u);
        pz = rdf(w, ptwp + 0x28u);
        CHECK(fabsf(px - x.sa1_position[0]) < 20.0f && fabsf(pz - x.sa1_position[2]) < 20.0f &&
                  (fabsf(px - sx) > 50.0f || fabsf(pz - sz) > 50.0f) && x.sa1_position[1] > -1.0f,
              "step %u: the plane follows (%.2f %.2f), Sonic at (%.2f %.3f %.2f) far from the spawn", (unsigned)x.step,
              (double)px, (double)pz, (double)x.sa1_position[0], (double)x.sa1_position[1], (double)x.sa1_position[2]);
        /* back to normal Sonic: the plane's mode 2 within 2 steps, then it sinks 0.1 a step and below -10 (about 100
           steps) starts the death (8C07BBC2: smode 50, item 0x4000) */
        sa1_character_order(ctx, id, SA1_ORDER_NORMAL);
        for (i = 0; i < 120; i++) {
            if (tick(ctx, id, 0.0f, 0.0f, 0, NULL, &x) != 0) {
                break;
            }
            if (mode2 < 0 && *(const uint8_t *)sa1g_world_ptr(w, ptwp) == 2) {
                mode2 = (int)x.step;
            }
            if (death < 0 && (x.item & 0x4000u)) {
                death = (int)x.step;
            }
        }
        CHECK(mode2 > 0 && death > mode2 && death - mode2 >= 96 && death - mode2 <= 106,
              "after the revert: the plane's mode 2 by step %d, the death request (item 0x4000) by step %d (%d steps "
              "later; the game: about 100), mode %d", mode2, death, death - mode2, (int)x.mode);
        sa1_character_destroy(ctx, id);
    }
}

/* ---- the water floor --------------------------------------------------------------------------------------------------- */
static void t_water(sa1_context *ctx)
{
    static const float at[3] = { 0.0f, 3.0f, 0.0f };
    static const struct { int32_t kind; uint32_t flags, host; const char *what; } c[5] = {
        { SA1_CHAR_SUPER_SONIC, SA1_CREATE_WATER_FLOOR, 0, "Super Sonic, WATER_FLOOR" },
        { SA1_CHAR_SUPER_SONIC, SA1_CREATE_WATER_FLOOR, SA1_INPUT_HOST_WATER_KNOWN | SA1_INPUT_HOST_IN_WATER,
          "Super Sonic, WATER_FLOOR, host KNOWN | IN_WATER" },
        { SA1_CHAR_SUPER_SONIC, SA1_CREATE_WATER_FLOOR, SA1_INPUT_HOST_WATER_KNOWN, "Super Sonic, WATER_FLOOR, host KNOWN dry" },
        { SA1_CHAR_SONIC, SA1_CREATE_WATER_FLOOR, 0, "Sonic, WATER_FLOOR" },
        { SA1_CHAR_SUPER_SONIC, 0, 0, "Super Sonic without the flag" },
    };
    int i;
    printf("water (GRID): a WATER box (top y 0) over a floor at y -100\n");
    if (scene_grid(ctx, 1) != 0) {
        g_fail++;
        return;
    }
    for (i = 0; i < 5; i++) {
        sa1_character_id id = mk(ctx, c[i].kind, at, c[i].flags, -1, c[i].kind == SA1_CHAR_SUPER_SONIC,
                                 c[i].kind == SA1_CHAR_SUPER_SONIC ? SA1_SUPER_NO_DRAIN : 0u, NULL);
        sa1_state_ex x;
        sa1ca_world_stats ws;
        float ymin = 1e9f, xmax;
        int p;
        if (id == 0) {
            g_fail++;
            continue;
        }
        g_host_flags = c[i].host;
        for (p = 0; p < 150; p++) {
            if (tick(ctx, id, 0.0f, p < 30 ? 0.0f : 1.0f, 0, NULL, &x) != 0) {
                break;
            }
            if (p >= 10 && x.sa1_position[1] < ymin) {
                ymin = x.sa1_position[1];
            }
        }
        g_host_flags = 0;
        xmax = (float)sqrt((double)x.sa1_position[0] * x.sa1_position[0] + (double)x.sa1_position[2] * x.sa1_position[2]);
        sa1ca_world_stats_get(sa1_character_world(ctx, id), &ws);
        if (c[i].flags) {
            CHECK(ymin > -1.0f && xmax > 100.0f && (x.flags & SA1_STATE_ON_GROUND) && ws.water_floors > 0,
                  "%s: lowest y %.3f over 140 pairs (on the water top), %.0f units run, on the ground %s, %u floor copies",
                  c[i].what, (double)ymin, (double)xmax, (x.flags & SA1_STATE_ON_GROUND) ? "yes" : "no",
                  (unsigned)ws.water_floors);
        } else {
            CHECK(ymin < -90.0f && ws.water_floors == 0, "%s: sinks to y %.3f (the floor at -100)", c[i].what, (double)ymin);
        }
        sa1_character_destroy(ctx, id);
    }
}

/* ---- the transformation (gsa1_rounds_plan.md 14.1a) ------------------------------------------------------------------ */
static void t_transform(sa1_context *ctx)
{
    static const float at[3] = { -2000.0f, 0.5f, 0.0f };
    sa1_character_id id, id8;
    sa1_state_ex x0, x1, x2;
    sa1g_world *w;
    int32_t a_ss = archive_named(ctx, SA1_CHAR_SONIC, "SUPERSONIC");
    rstat r;
    float d, v;
    sa1_status st;
    int i;
    printf("transform (GRID): a running Sonic turns Super in his own world and back\n");
    if (scene_grid(ctx, 0) != 0 || (id = mk(ctx, SA1_CHAR_SONIC, at, SA1_CREATE_SUPER_CAPABLE, 50, 0, 0, NULL)) == 0) {
        g_fail++;
        return;
    }
    w = sa1_character_world(ctx, id);
    CHECK(w->level == 19 && (w->super & SA1G_SUPER_WORLD) && !(w->super & SA1G_SUPER_TRIGGER) && find_task(w, X_LEVEL) != 0,
          "super-capable Sonic: stage %d, super bits %X (WORLD, no TRIGGER), level task %08X", (int)w->level,
          (unsigned)w->super, (unsigned)find_task(w, X_LEVEL));
    if (run(ctx, id, 60, 0.0f, 1.0f, NULL, &x0) != 0) {
        g_fail++;
        return;
    }
    CHECK(render_stats(ctx, id, a_ss, 0, &r) == 0 && !(x0.equipment & 0x8000u) && x0.player_speed[0] > 2.0f && r.body > 0 &&
              r.body_effect == 0 && r.body_arch_other == 0,
          "running normal Sonic: mode %d, speed %.3f, rings %d, body %u draws from SONIC (0), %u effects", (int)x0.mode,
          (double)x0.player_speed[0], (int)x0.rings, (unsigned)r.body, (unsigned)r.body_effect);
    st = sa1_character_order(ctx, id, SA1_ORDER_SUPER);
    if (st != SA1_OK || tick(ctx, id, 0.0f, 1.0f, 0, NULL, &x1) != 0) {
        CHECK(0, "sa1_character_order(SUPER): %s %s", sa1_status_string(st), sa1_last_error(ctx));
        return;
    }
    d = (float)sqrt((double)(x1.sa1_position[0] - x0.sa1_position[0]) * (x1.sa1_position[0] - x0.sa1_position[0]) +
                    (double)(x1.sa1_position[2] - x0.sa1_position[2]) * (x1.sa1_position[2] - x0.sa1_position[2]));
    CHECK((x1.equipment & 0x8000u) && (x1.flags & SA1_STATE_SUPER) && x1.player_speed[0] > 0.9f * x0.player_speed[0] &&
              d > 1.5f * x0.player_speed[0] && d < 2.5f * x0.player_speed[0] + 1.0f &&
              ((x1.angle[1] - x0.angle[1]) & 0xFFFF) < 0x200 && x1.rings == x0.rings,
          "SA1_ORDER_SUPER: Super in the next pair (mode %d, action %d), speed %.3f -> %.3f, moved %.2f in the pair, "
          "ang.y %04X -> %04X, rings %d -> %d", (int)x1.mode, (int)x1.action, (double)x0.player_speed[0],
          (double)x1.player_speed[0], (double)d, (unsigned)(x0.angle[1] & 0xFFFF), (unsigned)(x1.angle[1] & 0xFFFF),
          (int)x0.rings, (int)x1.rings);
    run(ctx, id, 30, 0.0f, 1.0f, NULL, &x2);
    CHECK((x2.equipment & 0x8000u) && x2.mode >= 71 && x2.mode <= 78 && render_stats(ctx, id, a_ss, a_ss, &r) == 0 &&
              r.additive_ss > 0 && r.body > 0 && r.body_effect == 0 && r.body_arch_other == 0,
          "30 pairs later: Super (mode %d, speed %.3f, rings %d); the aura %u additive SUPERSONIC draws, the body %u draws "
          "from SUPERSONIC (archive %d), %u effects", (int)x2.mode, (double)x2.player_speed[0], (int)x2.rings,
          (unsigned)r.additive_ss, (unsigned)r.body, a_ss, (unsigned)r.body_effect);
    CHECK(sa1_character_order(ctx, id, SA1_ORDER_SUPER) == SA1_OK, "SA1_ORDER_SUPER while Super: SA1_OK, nothing sent");
    x0 = x2;
    st = sa1_character_order(ctx, id, SA1_ORDER_NORMAL);
    if (st != SA1_OK || tick(ctx, id, 0.0f, 1.0f, 0, NULL, &x1) != 0) {
        CHECK(0, "sa1_character_order(NORMAL): %s %s", sa1_status_string(st), sa1_last_error(ctx));
        return;
    }
    CHECK(!(x1.equipment & 0x8000u) && phys_diff(w, pwp0(w)) == 0 && w->super_reverts == 1,
          "SA1_ORDER_NORMAL: normal Sonic in the next pair (mode %d, speed %.3f -> %.3f), playerwk.p = Sonic's entry (the "
          "revert fix)", (int)x1.mode, (double)x0.player_speed[0], (double)x1.player_speed[0]);
    v = 0.0f;
    for (i = 0; i < 30; i++) {
        tick(ctx, id, 0.0f, 1.0f, 0, NULL, &x2);
        if (x2.player_speed[0] > v) {
            v = x2.player_speed[0];
        }
    }
    CHECK(v > 3.0f && !(x2.equipment & 0x8000u) && render_stats(ctx, id, a_ss, 0, &r) == 0 && r.body_effect == 0 &&
              r.body_arch_other == 0, "he runs on as normal Sonic (top speed %.3f, mode %d); the body from SONIC again",
          (double)v, (int)x2.mode);
    sa1_character_destroy(ctx, id);

    /* a kind-8 world after the drain's revert takes SA1_ORDER_SUPER again */
    id8 = mk(ctx, SA1_CHAR_SUPER_SONIC, at, 0, 1, 0, 0, NULL);
    if (id8 == 0 || run(ctx, id8, 40, 0.0f, 0.0f, NULL, &x1) != 0) {
        g_fail++;
        return;
    }
    {
        int32_t a8ss = archive_named(ctx, SA1_CHAR_SUPER_SONIC, "SUPERSONIC");
        int32_t a8son = archive_named(ctx, SA1_CHAR_SUPER_SONIC, "SONIC");
        rstat r1;
        int ok1 = render_stats(ctx, id8, a8ss, a8son, &r1) == 0;
        sa1_character_add_rings(ctx, id8, 50);
        st = sa1_character_order(ctx, id8, SA1_ORDER_SUPER);
        run(ctx, id8, 2, 0.0f, 0.0f, NULL, &x2);
        CHECK(!(x1.equipment & 0x8000u) && ok1 && r1.body > 0 && r1.body_effect == 0 && r1.body_arch_other == 0 &&
                  st == SA1_OK && (x2.equipment & 0x8000u) && x2.rings == 50,
              "kind 8 after the drain's revert: normal Sonic drawn from SONIC (archive %d: %u draws, %u effects); "
              "+50 rings and SA1_ORDER_SUPER: Super again (equipment %04X, rings %d)", a8son, (unsigned)r1.body,
              (unsigned)r1.body_effect, (unsigned)x2.equipment, (int)x2.rings);
    }
    sa1_character_destroy(ctx, id8);

    /* the alternative, measured: re-create the running Sonic as kind 8 at the same place, yaw and rings */
    if ((id = mk(ctx, SA1_CHAR_SONIC, at, 0, 50, 0, 0, NULL)) == 0 || run(ctx, id, 60, 0.0f, 1.0f, NULL, &x0) != 0) {
        g_fail++;
        return;
    }
    {
        sa1_character_desc dd;
        memset(&dd, 0, sizeof dd);
        dd.struct_size = sizeof dd;
        dd.kind = SA1_CHAR_SUPER_SONIC;
        dd.flags = SA1_CREATE_WORLD | SA1_CREATE_SA1_SPACE | SA1_CREATE_ANGLES | SA1_CREATE_START_RINGS;
        memcpy(dd.position, x0.sa1_position, sizeof dd.position);
        memcpy(dd.angle, x0.angle, sizeof dd.angle);
        dd.start_rings = x0.rings;
        sa1_character_destroy(ctx, id);
        id8 = 0;
        st = sa1_character_create_ex(ctx, &dd, &id8);
        if (SA1_FAILED(st) || tick(ctx, id8, 0.0f, 1.0f, 0, NULL, &x1) != 0 || tick(ctx, id8, 0.0f, 1.0f, 0, NULL, &x2) != 0) {
            CHECK(0, "re-create: %s %s", sa1_status_string(st), sa1_last_error(ctx));
            return;
        }
        CHECK(x1.player_speed[0] < 0.5f * x0.player_speed[0] && !(x1.equipment & 0x8000u) && (x2.equipment & 0x8000u),
              "re-create as kind 8 (the alternative): speed %.3f -> %.3f after its first pair (lost), not Super before its "
              "second pair (equipment %04X, then %04X): the in-world order is the clean way", (double)x0.player_speed[0],
              (double)x1.player_speed[0], (unsigned)x1.equipment, (unsigned)x2.equipment);
        sa1_character_destroy(ctx, id8);
    }
}

/* ---- refusals ---------------------------------------------------------------------------------------------------------- */
static void t_invalid(sa1_context *ctx)
{
    static const float at[3] = { 0.0f, 0.5f, 0.0f };
    sa1_character_desc d;
    sa1_character_id id = 0;
    sa1_status st;
    printf("invalid: the refusals\n");
    if (scene_grid(ctx, 0) != 0) {
        g_fail++;
        return;
    }
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.kind = SA1_CHAR_SUPER_SONIC;
    d.flags = SA1_CREATE_WORLD | SA1_CREATE_SA1_SPACE | SA1_CREATE_STAGE;
    d.level = 1;
    st = sa1_character_create_ex(ctx, &d, &id);
    CHECK(st == SA1_ERR_INVALID_ARGUMENT, "kind 8 with SA1_CREATE_STAGE level 1: %s", sa1_status_string(st));
    d.level = 19;
    d.act = 1;
    st = sa1_character_create_ex(ctx, &d, &id);
    CHECK(st == SA1_ERR_INVALID_ARGUMENT, "kind 8 on level 19 act 1: %s", sa1_status_string(st));
    d.act = 0;
    st = sa1_character_create_ex(ctx, &d, &id);
    CHECK(st == SA1_OK || st == SA1_PARTIAL, "kind 8 with SA1_CREATE_STAGE level 19 act 0: %s", sa1_status_string(st));
    sa1_character_destroy(ctx, id);
    st = SA1_OK;
    CHECK(mk(ctx, SA1_CHAR_TAILS, at, SA1_CREATE_SUPER_CAPABLE, -1, 0, 0, &st) == 0 && st == SA1_ERR_INVALID_ARGUMENT,
          "Tails with SA1_CREATE_SUPER_CAPABLE: %s", sa1_status_string(st));
    CHECK(mk(ctx, SA1_CHAR_SONIC, at, 0, -1, 1, SA1_SUPER_NO_DRAIN, &st) == 0 && st == SA1_ERR_INVALID_ARGUMENT,
          "plain Sonic with SA1_CREATE_SUPER: %s", sa1_status_string(st));
    CHECK(mk(ctx, SA1_CHAR_SUPER_SONIC, at, 0, -1, 1, 0x100u, &st) == 0 && st == SA1_ERR_INVALID_ARGUMENT,
          "unknown super_flags: %s", sa1_status_string(st));
    d.flags = SA1_CREATE_SA1_SPACE | SA1_CREATE_SUPER;
    d.struct_size = (uint32_t)offsetof(sa1_character_desc, super_flags);
    st = sa1_character_create_ex(ctx, &d, &id);
    CHECK(st == SA1_ERR_INVALID_ARGUMENT, "SA1_CREATE_SUPER with a struct_size short of super_flags: %s",
          sa1_status_string(st));
    if ((id = mk(ctx, SA1_CHAR_SONIC, at, 0, -1, 0, 0, NULL)) != 0) {
        st = sa1_character_order(ctx, id, SA1_ORDER_SUPER);
        CHECK(st == SA1_ERR_UNSUPPORTED, "SA1_ORDER_SUPER to a Sonic that is not super-capable: %s", sa1_status_string(st));
        sa1_character_destroy(ctx, id);
    }
    if ((id = mk(ctx, SA1_CHAR_SUPER_SONIC, at, 0, -1, 0, 0, NULL)) != 0) {
        st = sa1_character_order(ctx, id, 3);
        CHECK(st == SA1_ERR_UNSUPPORTED, "order 3 (a spring): %s", sa1_status_string(st));
        st = sa1_character_order(ctx, id, SA1_ORDER_NORMAL);
        CHECK(st == SA1_OK, "SA1_ORDER_NORMAL before the first pair (not Super yet): %s", sa1_status_string(st));
        sa1_character_destroy(ctx, id);
    }
    CHECK(sa1_character_order(ctx, 0x7FFF0001, SA1_ORDER_SUPER) == SA1_ERR_NOT_FOUND, "an unknown character: NOT_FOUND");
}

/* a DISC context of another stage refuses Super Sonic's world */
static void t_invalid_disc(sa1_context *ctx)
{
    sa1_collision_static d;
    sa1_status st = SA1_OK;
    static const float at[3] = { 0.0f, 0.5f, 0.0f };
    scene_forget();
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.mode = SA1_COLLISION_MODE_DISC;
    d.disc_level = 1;
    if (sa1_collision_set_static_ex(ctx, &d) != SA1_OK) {
        g_fail++;
        return;
    }
    CHECK(mk(ctx, SA1_CHAR_SUPER_SONIC, at, 0, -1, 0, 0, &st) == 0 && st == SA1_ERR_INVALID_ARGUMENT,
          "kind 8 with DISC collision of Emerald Coast: %s", sa1_status_string(st));
    d.disc_level = 18;
    st = sa1_collision_set_static_ex(ctx, &d);
    CHECK(st == SA1_ERR_INVALID_ARGUMENT, "DISC level 18: %s (level 19 is the only boss stage DISC mode takes)",
          sa1_status_string(st));
}

/* without SA1_LOAD_SUPER_FORMS: Sonic's archives as before, no super-capable world */
static void t_forms(sa1_context *unused)
{
    static const float at[3] = { 0.0f, 0.5f, 0.0f };
    sa1_context *ctx = NULL;
    sa1_host h;
    sa1_load_options lo;
    sa1_status st = SA1_OK;
    sa1_character_id id;
    (void)unused;
    printf("forms: a context loaded without SA1_LOAD_SUPER_FORMS\n");
    sa1_host_defaults(&h);
    h.log = log_cb;
    h.log_level = SA1_LOG_WARN;
    if (sa1_init(&h, &ctx) != SA1_OK) {
        g_fail++;
        return;
    }
    memset(&lo, 0, sizeof lo);
    lo.struct_size = sizeof lo;
    lo.characters = (1u << SA1_CHAR_SONIC) | (1u << SA1_CHAR_SUPER_SONIC);
    lo.flags = SA1_LOAD_DEFAULT;
    if (SA1_FAILED(sa1_load_disc(ctx, g_gdi, &lo)) || scene_grid(ctx, 0) != 0) {
        g_fail++;
        sa1_shutdown(ctx);
        scene_forget();     /* g_scene named this context */
        return;
    }
    CHECK(archive_named(ctx, SA1_CHAR_SONIC, "SUPERSONIC") < 0 && archive_named(ctx, SA1_CHAR_SONIC, "CON_REGULAR") == 4 &&
              archive_named(ctx, SA1_CHAR_SUPER_SONIC, "SONIC") == 4,
          "Sonic's archives as before 6a (SUPERSONIC %d, CON_REGULAR %d the last); Super Sonic still has SONIC (%d)",
          archive_named(ctx, SA1_CHAR_SONIC, "SUPERSONIC"), archive_named(ctx, SA1_CHAR_SONIC, "CON_REGULAR"),
          archive_named(ctx, SA1_CHAR_SUPER_SONIC, "SONIC"));
    CHECK(mk(ctx, SA1_CHAR_SONIC, at, SA1_CREATE_SUPER_CAPABLE, -1, 0, 0, &st) == 0 && st == SA1_ERR_NOT_LOADED,
          "SA1_CREATE_SUPER_CAPABLE without the Super form's textures: %s", sa1_status_string(st));
    id = mk(ctx, SA1_CHAR_SUPER_SONIC, at, 0, -1, 0, 0, NULL);
    CHECK(id != 0, "Super Sonic without SA1_LOAD_SUPER_FORMS: created");
    sa1_shutdown(ctx);
    scene_forget();     /* g_scene named this context */
}

/* ---- determinism ------------------------------------------------------------------------------------------------------- */
/* round 0.9.3a (super_general_design.md Review RC6, R.2.4): the arena hashed with every aligned word that names the
 * extension segment 0x18 (pcdata, the window landtable, placeholders: their addresses move when pcdata grows) as 0 */
static uint32_t arena_hash_masked(const sa1g_world *w)
{
    uint32_t h = 2166136261u, i, v, k;
    for (i = 0; i < SA1G_ARENA_SIZE; i += 4u) {
        memcpy(&v, w->ram + i, 4);
        if (v != 0 && ((v >> 24) & 31u) == SA1G_SEG_EXT) {
            v = 0;
        }
        for (k = 0; k < 4u; k++) {
            h = (h ^ ((v >> (8u * k)) & 0xFFu)) * 16777619u;
        }
    }
    return h;
}

/* RC6: the behaviour gate of the Vs. Perfect Chaos Super Sonic (kind 8). A fixed scenario (runs, jumps, the action button
 * in the air, the drain's revert at 0 rings, +50 rings and SA1_ORDER_SUPER again, runs) with one line per pair: step,
 * position, speeds, mode, action, equipment, rings, flags, every SA1_EVENT (type / id) and the body draws (count,
 * archive and texture of each). "KIND8_TRACE <hash>" must be the same before and after a round; SUPER_TEST_TRACE=FILE
 * writes the lines. */
static void t_kind8_trace(sa1_context *ctx)
{
    static const float at[3] = { 0.0f, 0.5f, 0.0f };
    sa1_character_id id;
    sa1_state_ex x;
    evlog L;
    uint32_t h = 2166136261u, i, k, from = 0;
    const char *path = getenv("SUPER_TEST_TRACE");
    FILE *f = path != NULL && path[0] ? fopen(path, "w") : NULL;
    int32_t a_ss = archive_named(ctx, SA1_CHAR_SUPER_SONIC, "SUPERSONIC");
    printf("kind8_trace: the Vs. Perfect Chaos Super Sonic's fixed scenario, per pair (review RC6)\n");
    memset(&L, 0, sizeof L);
    if (scene_grid(ctx, 0) != 0 || (id = mk(ctx, SA1_CHAR_SUPER_SONIC, at, 0, 5, 0, 0, NULL)) == 0) {
        g_fail++;
        if (f) {
            fclose(f);
        }
        return;
    }
    for (i = 0; i < 420; i++) {
        float sx = (i / 50u) & 1u ? 0.7f : -0.3f;
        uint32_t b = (i % 37u) == 5u ? SA1_BUTTON_JUMP : 0u;
        const sa1_render *rd = NULL;
        char line[2048];
        int n;
        uint32_t fp[6], body = 0;
        if ((i % 37u) == 12u) {
            b |= SA1_BUTTON_ACTION;         /* in the air: the game's air stop (mode 74) while Super */
        }
        if (i == 250u) {
            sa1_character_add_rings(ctx, id, 50);
            sa1_character_order(ctx, id, SA1_ORDER_SUPER);
        }
        if (tick(ctx, id, sx, 1.0f, b, &L, &x) != 0) {
            g_fail++;
            break;
        }
        memcpy(fp, x.sa1_position, 12);
        memcpy(fp + 3, x.player_speed, 12);
        n = snprintf(line, sizeof line, "%u %08X %08X %08X %08X %08X %08X m%d a%d e%04X r%d f%X ev", (unsigned)x.step,
                     (unsigned)fp[0], (unsigned)fp[1], (unsigned)fp[2], (unsigned)fp[3], (unsigned)fp[4], (unsigned)fp[5],
                     (int)x.mode, (int)x.action, (unsigned)x.equipment, (int)x.rings, (unsigned)x.flags);
        for (k = from; k < L.n && n < (int)sizeof line - 32; k++) {
            n += snprintf(line + n, sizeof line - (size_t)n, " %u:%u", (unsigned)L.e[k].type, (unsigned)L.e[k].id);
        }
        from = L.n;
        n += snprintf(line + n, sizeof line - (size_t)n, " d");
        if (sa1_character_render(ctx, id, 1.0f, &rd) == SA1_OK && rd != NULL) {
            for (k = 0; k < rd->num_draws && n < (int)sizeof line - 32; k++) {
                const sa1_draw *d = &rd->draws[k];
                if (d->prim != SA1_PRIM_TRIANGLES || d->source != SA1_SOURCE_CHARACTER) {
                    continue;
                }
                body++;
                n += snprintf(line + n, sizeof line - (size_t)n, " %d/%d/%X", (int)d->archive, (int)d->texture,
                              (unsigned)d->flags);
            }
        }
        snprintf(line + n, sizeof line - (size_t)n, " n%u\n", (unsigned)body);
        for (k = 0; line[k]; k++) {
            h = (h ^ (uint8_t)line[k]) * 16777619u;
        }
        if (f) {
            fputs(line, f);
        }
    }
    if (f) {
        fclose(f);
    }
    g_k8_trace = h;     /* round 0.9.4a: general_anims_kind8 compares it with the 0.9.4 baseline */
    printf("KIND8_TRACE %08X (420 pairs; step %u, mode %d, equipment %04X, rings %d; SUPERSONIC archive %d)\n", (unsigned)h,
           (unsigned)x.step, (int)x.mode, (unsigned)x.equipment, (int)x.rings, (int)a_ss);
    CHECK((x.equipment & 0x8000u) && sa1_character_world(ctx, id)->super_reverts == 1,
          "the scenario: reverted once at 0 rings, Super again by SA1_ORDER_SUPER at pair 250");
    free(L.e);
    sa1_character_destroy(ctx, id);
}

static void t_determinism(sa1_context *ctx)
{
    static const float at[3] = { 0.0f, 0.5f, 0.0f };
    sa1_character_id id;
    sa1_state_ex x;
    sa1g_world *w;
    uint32_t h = 2166136261u, i;
    printf("determinism: Super Sonic, 400 pairs of runs, jumps and a revert\n");
    if (scene_grid(ctx, 0) != 0 || (id = mk(ctx, SA1_CHAR_SUPER_SONIC, at, 0, 5, 0, 0, NULL)) == 0) {
        g_fail++;
        return;
    }
    for (i = 0; i < 400; i++) {
        float sx = (i / 50u) & 1u ? 0.7f : -0.3f;
        tick(ctx, id, sx, 1.0f, (i % 37u) == 5u ? SA1_BUTTON_JUMP : 0u, NULL, &x);
    }
    w = sa1_character_world(ctx, id);
    for (i = 0; i < SA1G_ARENA_SIZE; i++) {
        h = (h ^ w->ram[i]) * 16777619u;
    }
    printf("DETERMINISM %08X (step %u, mode %d, equipment %04X, rings %d, reverts %u)\n", (unsigned)h, (unsigned)x.step,
           (int)x.mode, (unsigned)x.equipment, (int)x.rings, (unsigned)w->super_reverts);
    g_det_masked = arena_hash_masked(w);
    printf("DETERMINISM_MASKED %08X (aligned words naming the extension segment 0x18 hashed as 0: round 0.9.3a review "
           "RC6, R.2.4)\n", (unsigned)g_det_masked);
    CHECK(!(x.equipment & 0x8000u) && w->super_reverts == 1, "the 5 rings ran out: reverted once");
    sa1_character_destroy(ctx, id);
}


/* ==== round 0.9.3a: the General form (sadc docs/research/super_general_design.md 8.2 with its Review R.4) ============= */
#define A_SONIC_ACTIONS 0x8C1C55A0u     /* SonicActionList */
#define A_TEXLIST_SONIC 0x8C1C6C94u     /* texlist_SONIC {textures, n} */
#define A_CUR_LEVEL     0x8C7493BAu     /* Sint16 CurrentLevel */
#define A_LEVEL_TASK    0x8C752BD4u     /* the level task */
#define A_EVENT_FLAGS   0x8C799920u     /* Uint8 per flag */
#define O_SUPER_BALL    0x0C90BD04u
#define X_LIST2_L1      0x0C90023Eu     /* level 1's list-2 task (not created in a General world) */
#define X_TRAIL_SS      0x0C91E26Au     /* the Super trail's exec (0C9004F2 installs it) */
#define X_BALLAI_SS     0x0C91DE82u     /* the Super ball after-images' exec (0C9004B6) */
#define NAMES_SUPER     0x0C901B1Cu
#define NAMES_SONIC     0x8C1C6B44u

static uint32_t rd16w(const sa1g_world *w, uint32_t g)
{
    return (rd32(w, g & ~3u) >> (8u * (g & 2u))) & 0xFFFFu;
}

static uint32_t rd8w(const sa1g_world *w, uint32_t g)
{
    return (rd32(w, g & ~3u) >> (8u * (g & 3u))) & 0xFFu;
}

static void wr8w(sa1g_world *w, uint32_t g, uint32_t v)
{
    uint32_t a = g & ~3u, sh = 8u * (g & 3u), o = rd32(w, a);
    sa1g_world_wr32(w, a, (o & ~(0xFFu << sh)) | ((v & 0xFFu) << sh));
}

static void wr16w(sa1g_world *w, uint32_t g, uint32_t v)
{
    uint32_t a = g & ~3u, sh = 8u * (g & 2u), o = rd32(w, a);
    sa1g_world_wr32(w, a, (o & ~(0xFFFFu << sh)) | ((v & 0xFFFFu) << sh));
}

static uint32_t twp0w(const sa1g_world *w)
{
    uint32_t t = rd32(w, A_PLAYER_TWP);
    return arena(t) ? t : 0u;
}

/* a General Sonic (kind 0, SA1_CREATE_SUPER_CAPABLE, super_flags SA1_SUPER_GENERAL | extra) */
static sa1_character_id mk_gen(sa1_context *ctx, const float pos[3], int32_t rings, uint32_t extra, sa1_status *st)
{
    return mk(ctx, SA1_CHAR_SONIC, pos, SA1_CREATE_SUPER_CAPABLE, rings, 1, SA1_SUPER_GENERAL | extra, st);
}

/* Sonic's three upgrades as G-SA1 sets them (event flags 0x8D shoes, 0x8E ring, 0x9F light: one byte each), into a world
 * built and not yet stepped */
static void give_upgrades(sa1_context *ctx, sa1_character_id id)
{
    sa1g_world *w = sa1_character_world(ctx, id);
    if (w != NULL) {
        wr8w(w, A_EVENT_FLAGS + 0x8Du, 1);
        wr8w(w, A_EVENT_FLAGS + 0x8Eu, 1);
        wr8w(w, A_EVENT_FLAGS + 0x9Fu, 1);
    }
}

/* SA1 floor scenes: a flat floor of half size h at y = 0, or (water) a WATER box of half size h, top y = top, over a floor
 * at y = -100 */
static int scene_big(sa1_context *ctx, float h, int water, float top)
{
    tbuf b;
    sa1_status st;
    if (scene_same(ctx, 2, h, water, top)) {
        return 0;
    }
    scene_forget();
    memset(&b, 0, sizeof b);
    if (water) {
        tb_floor(&b, -h - 1000.0f, h + 1000.0f, -h - 1000.0f, h + 1000.0f, -100.0f, 1.0f, SA1_SURFACE_SOLID);
        tb_box(&b, -h, h, -100.0f, top, -h, h, SA1_SURFACE_WATER);
    } else {
        tb_floor(&b, -h, h, -h, h, 0.0f, 1.0f, SA1_SURFACE_SOLID);
    }
    st = sa1_collision_set_static(ctx, b.t, b.n);
    free(b.t);
    if (SA1_FAILED(st)) {
        printf("  set_static: %s: %s\n", sa1_status_string(st), sa1_last_error(ctx));
        return -1;
    }
    scene_note(ctx, 2, h, water, top);
    return 0;
}

/* a flat floor at y = 0 of tiles (256 units, as tests/hostobj_test.c's floor_rect: the hold tests) */
static int scene_tiled(sa1_context *ctx, float h)
{
    tbuf b;
    sa1_status st;
    float x, z;
    if (scene_same(ctx, 3, h, 0, 0.0f)) {
        return 0;
    }
    scene_forget();
    memset(&b, 0, sizeof b);
    for (x = -h; x < h; x += 256.0f) {
        for (z = -h; z < h; z += 256.0f) {
            tb_floor(&b, x, x + 256.0f, z, z + 256.0f, 0.0f, 1.0f, SA1_SURFACE_SOLID);
        }
    }
    st = sa1_collision_set_static(ctx, b.t, b.n);
    free(b.t);
    if (SA1_FAILED(st)) {
        printf("  set_static: %s: %s\n", sa1_status_string(st), sa1_last_error(ctx));
        return -1;
    }
    scene_note(ctx, 3, h, 0, 0.0f);
    return 0;
}

/* the hold tests' context: tests/hostobj_test.c's environment exactly (SA1_COORDS_PRESET_SOURCE: 4 HU per SA1 unit, z up;
 * a floor of 1024-HU tiles over +-8192 HU at z 0), where its lift test lifts; NULL on failure */
static sa1_context *hold_ctx(void)
{
    sa1_context *c = NULL;
    sa1_host h;
    sa1_coords co;
    sa1_load_options lo;
    tbuf b;
    float x, y;
    sa1_host_defaults(&h);
    h.log = log_cb;
    h.log_level = SA1_LOG_WARN;
    if (sa1_init(&h, &c) != SA1_OK) {
        return NULL;
    }
    memset(&co, 0, sizeof co);
    co.struct_size = sizeof co;
    sa1_coords_preset(SA1_COORDS_PRESET_SOURCE, &co);
    sa1_set_coords(c, &co);
    memset(&lo, 0, sizeof lo);
    lo.struct_size = sizeof lo;
    lo.characters = (1u << SA1_CHAR_SONIC) | (1u << SA1_CHAR_SUPER_SONIC);
    lo.flags = SA1_LOAD_DEFAULT | SA1_LOAD_SUPER_FORMS;
    if (SA1_FAILED(sa1_load_disc(c, g_gdi, &lo))) {
        sa1_shutdown(c);
        return NULL;
    }
    memset(&b, 0, sizeof b);
    for (x = -8192.0f; x < 8192.0f - 0.01f; x += 1024.0f) {
        for (y = -8192.0f; y < 8192.0f - 0.01f; y += 1024.0f) {
            float q[4][3] = { { x, y, 0.0f }, { x + 1024.0f, y, 0.0f }, { x + 1024.0f, y + 1024.0f, 0.0f },
                              { x, y + 1024.0f, 0.0f } };
            float n[3] = { 0.0f, 0.0f, 1.0f };
            tb_quad(&b, q, n, SA1_SURFACE_SOLID);
        }
    }
    if (SA1_FAILED(sa1_collision_set_static(c, b.t, b.n))) {
        free(b.t);
        sa1_shutdown(c);
        return NULL;
    }
    free(b.t);
    return c;
}

/* a character of the hold context at (0, 0, 8 HU) (hostobj_test's make_char); super_flags with SA1_CREATE_SUPER when sf */
static sa1_character_id hold_char(sa1_context *c, int32_t kind, uint32_t flags, uint32_t sf)
{
    sa1_character_desc d;
    sa1_character_id id = 0;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.kind = kind;
    d.flags = SA1_CREATE_WORLD | SA1_CREATE_START_RINGS | flags | (sf ? SA1_CREATE_SUPER : 0u);
    d.position[2] = 8.0f;
    d.start_rings = 50;
    d.super_flags = sf;
    if (SA1_FAILED(sa1_character_create_ex(c, &d, &id))) {
        printf("  create: %s\n", sa1_last_error(c));
        return 0;
    }
    return id;
}

/* a HOLDABLE cylinder 40 HU ahead of the character (body r 8 HU, half height 8 HU), lifted with the action button; the
 * object id (0: not lifted within 40 pairs) */
static uint32_t hold_lift(sa1_context *c, sa1_character_id id)
{
    sa1_hostobj_desc d;
    sa1_state_ex x;
    uint32_t obj = 0;
    int i;
    run(c, id, 20, 0.0f, 0.0f, NULL, &x);
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.kind = SA1_HOSTOBJ_HOLDABLE;
    d.position[0] = x.position[0] + 40.0f;
    d.position[1] = x.position[1];
    d.position[2] = 0.0f;
    d.armor = -1;
    d.touch = -1;
    d.num_shapes = 1;
    d.shapes[0].form = SA1_SHAPE_CYLINDER;
    d.shapes[0].size[0] = 8.0f;
    d.shapes[0].size[1] = 8.0f;
    d.shapes[0].center[2] = 8.0f;
    if (sa1_hostobj_add(c, &d, &obj) != SA1_OK) {
        printf("  sa1_hostobj_add: %s\n", sa1_last_error(c));
        return 0;
    }
    run(c, id, 2, 0.0f, 0.0f, NULL, &x);
    tick(c, id, 0.0f, 0.0f, SA1_BUTTON_ACTION, NULL, &x);
    for (i = 0; i < 40 && x.held_object != obj; i++) {
        tick(c, id, 0.0f, 0.0f, 0, NULL, &x);
    }
    if (x.held_object != obj) {
        sa1_hostobj_remove(c, obj, SA1_HOSTOBJ_REMOVE_SILENT);
        return 0;
    }
    return obj;
}

/* jump from a run and tick until he is in mode 8 (the jump), at most n pairs; 0 when he is */
static int into_jump(sa1_context *ctx, sa1_character_id id, float sy, sa1_state_ex *x)
{
    int i;
    tick(ctx, id, 0.0f, sy, SA1_BUTTON_JUMP, NULL, x);
    for (i = 0; i < 6; i++) {
        if (x->mode == 8) {
            return 0;
        }
        tick(ctx, id, 0.0f, sy, SA1_BUTTON_JUMP, NULL, x);
    }
    return x->mode == 8 ? 0 : -1;
}

/* the lowest point of the character's body draws above the drawn root (the feet against the ground) */
static float feet_of(sa1_context *ctx, sa1_character_id id)
{
    const sa1_render *rd = NULL;
    uint32_t i, k, t, n = 0;
    float lo = 1e9f;
    if (sa1_character_render(ctx, id, 1.0f, &rd) != SA1_OK || rd == NULL) {
        return 0;
    }
    for (i = 0; i < rd->num_draws; i++) {
        const sa1_draw *d = &rd->draws[i];
        if (d->prim != SA1_PRIM_TRIANGLES || (d->flags & (SA1_DRAW_SHADOW | SA1_DRAW_ADDITIVE)) ||
            d->source != SA1_SOURCE_CHARACTER) {
            continue;
        }
        for (t = d->first; t < d->first + d->count; t++) {
            for (k = 0; k < 3; k++) {
                float y = rd->positions[9 * t + 3 * k + 1];
                if (y < lo) {
                    lo = y;
                }
                n++;
            }
        }
    }
    return n ? lo - rd->position[1] : 0;
}

/* the texture of the aura's additive SUPERSONIC draw of the last pair (-1: none) */
static int32_t aura_tex(sa1_context *ctx, sa1_character_id id, int32_t a_ss)
{
    const sa1_render *rd = NULL;
    uint32_t i;
    if (sa1_character_render(ctx, id, 1.0f, &rd) != SA1_OK || rd == NULL) {
        return -1;
    }
    for (i = 0; i < rd->num_draws; i++) {
        const sa1_draw *d = &rd->draws[i];
        if (d->prim == SA1_PRIM_TRIANGLES && (d->flags & SA1_DRAW_ADDITIVE) && d->archive == a_ss) {
            return d->texture;
        }
    }
    return -1;
}

/* forced idles (the probe's way: waittimer 1801, then 40 pairs standing): a histogram of SA1_EVENT_VOICE ids */
static void idle_voices(sa1_context *ctx, sa1_character_id id, int n, uint32_t *v227, uint32_t *v390, uint32_t *v388,
                        uint32_t *v396)
{
    sa1g_world *w = sa1_character_world(ctx, id);
    evlog L;
    uint32_t i;
    int k;
    memset(&L, 0, sizeof L);
    *v227 = *v390 = *v388 = *v396 = 0;
    for (k = 0; k < n; k++) {
        uint32_t pwp = pwp0(w);
        if (pwp) {
            wr16w(w, pwp + 0x0Eu, 1801);
        }
        tick(ctx, id, 0, 0, 0, &L, NULL);
        tick(ctx, id, 0, 0, 0, &L, NULL);
        run(ctx, id, 40, 0, 0, &L, NULL);
    }
    for (i = 0; i < L.n; i++) {
        if (L.e[i].type != SA1_EVENT_VOICE) {
            continue;
        }
        *v227 += L.e[i].id == 227u;
        *v390 += L.e[i].id == 390u;
        *v388 += L.e[i].id == 388u;
        *v396 += L.e[i].id == 396u;
    }
    free(L.e);
}

/* a word of the arena or of the extension segment (GT / GW live there; rd32 reads the arena only) */
static uint32_t x32(const sa1g_world *w, uint32_t g)
{
    uint32_t v = 0;
    if (g != 0 && (((g >> 24) & 31u) == SA1G_SEG_ARENA || ((g >> 24) & 31u) == SA1G_SEG_EXT)) {
        memcpy(&v, sa1g_world_ptr(w, g), 4);
    }
    return v;
}

static uint32_t x8(const sa1g_world *w, uint32_t g)
{
    return (x32(w, g & ~3u) >> (8u * (g & 3u))) & 0xFFu;
}

/* the General weld list: entries, Super entries at inpmode 2 with org, depths set (Super and hand entries) */
static void gw_stats(const sa1g_world *w, uint32_t *n, uint32_t *n_super, uint32_t *n_org, uint32_t *n_depth)
{
    uint32_t e = w->sg.gw, k;
    *n = *n_super = *n_org = *n_depth = 0;
    for (k = 0; e != 0 && k < 80u && x32(w, e) != 0; k++, e += 0x18u) {
        (*n)++;
        if ((x32(w, e) & 0x1FFFFFFFu) == (O_SUPER_OBJECT & 0x1FFFFFFFu)) {
            uint32_t inp = x8(w, e + 0x0Du);
            if (inp == 2u) {
                (*n_super)++;
                *n_org += x32(w, e + 0x10u) != 0;
            }
            *n_depth += x8(w, e + 0x0Eu) != 0xFFu;
        }
    }
}

static void t_general_boot(sa1_context *ctx)
{
    static const float at[3] = { 0.0f, 0.5f, 0.0f };
    sa1_character_id id;
    sa1g_world *w;
    sa1_state_ex x;
    sa1_status st;
    uint32_t k, lvl_bad = 0, n, ns, no, nd;
    int b7 = 0, i;
    printf("general_boot (GRID): the General world is a level-1 Sonic world with B_CHAOS7\n");
    if (scene_grid(ctx, 0) != 0 || (id = mk_gen(ctx, at, 50, 0, NULL)) == 0) {
        g_fail++;
        return;
    }
    w = sa1_character_world(ctx, id);
    for (k = 0; k < w->num_fn_images; k++) {
        b7 |= strcmp(w->fn_images[k]->name, "B_CHAOS7") == 0;
    }
    CHECK(w->level == 1 && w->act == 0 && b7 && (w->super & SA1G_SUPER_WORLD) && (w->super & SA1G_SUPER_GENERAL) &&
              !(w->super & (SA1G_SUPER_TRIGGER | SA1G_SUPER_PLANE)),
          "the world: stage %d act %d, B_CHAOS7 code map %s, super bits %X (WORLD | GENERAL, no TRIGGER / PLANE)",
          (int)w->level, (int)w->act, b7 ? "active" : "MISSING", (unsigned)w->super);
    printf("    stage tasks: %s\n", w->stage_note);
    for (i = 0; i < 20; i++) {
        tick(ctx, id, 0.0f, 0.0f, 0, NULL, &x);
        lvl_bad += (int16_t)rd16w(w, A_CUR_LEVEL) != 1;
    }
    CHECK(lvl_bad == 0 && find_task(w, X_LEVEL) != 0 && rd32(w, A_LEVEL_TASK) == find_task(w, X_LEVEL) &&
              find_task(w, X_LIST2_L1) == 0 && find_task(w, X_TRIGGER) == 0 && find_task(w, X_PLANE) == 0,
          "20 pairs: CurrentLevel 1 at every pair (%u not), the stand-in is the level task (%08X), no list-2 task, trigger "
          "or sea plane", (unsigned)lvl_bad, (unsigned)find_task(w, X_LEVEL));
    gw_stats(w, &n, &ns, &no, &nd);
    CHECK(w->sg.welds == 1 && n == 39 && ns == 12 && no == 12 && rd32(w, pwp0(w) + 0x144u) == w->sg.gw,
          "the welds after pair 1: GW is pljvptr, %u entries (39), %u Super at inpmode 2 with %u org buffers",
          (unsigned)n, (unsigned)ns, (unsigned)no);
    CHECK(!(x.equipment & 0x8000u) && x.rings == 50, "he starts normal (equipment %04X), 50 rings", (unsigned)x.equipment);
    {
        void *ram = calloc(1, SA1G_ARENA_SIZE);
        st = ram != NULL ? sa1g_world_transplant(w, ram, SA1G_ARENA_SIZE) : SA1_ERR_OUT_OF_MEMORY;
        free(ram);
        CHECK(st == SA1_ERR_UNSUPPORTED && w->booted, "sa1g_world_transplant of a General world: %s (review RC16)",
              sa1_status_string(st));
    }
    sa1_character_destroy(ctx, id);
    /* the refusals */
    st = SA1_OK;
    CHECK(mk(ctx, SA1_CHAR_SUPER_SONIC, at, 0, -1, 1, SA1_SUPER_GENERAL, &st) == 0 && st == SA1_ERR_INVALID_ARGUMENT,
          "kind 8 with SA1_SUPER_GENERAL: %s", sa1_status_string(st));
    CHECK(mk(ctx, SA1_CHAR_SONIC, at, SA1_CREATE_SUPER_CAPABLE, -1, 1, SA1_SUPER_START, &st) == 0 &&
              st == SA1_ERR_INVALID_ARGUMENT, "SA1_SUPER_START without SA1_SUPER_GENERAL: %s", sa1_status_string(st));
    CHECK(mk(ctx, SA1_CHAR_SONIC, at, SA1_CREATE_SUPER_CAPABLE, -1, 1, SA1_SUPER_START_NORMAL, &st) == 0 &&
              st == SA1_ERR_INVALID_ARGUMENT, "SA1_SUPER_START_NORMAL for kind 0: %s", sa1_status_string(st));
    CHECK(mk(ctx, SA1_CHAR_SONIC, at, SA1_CREATE_SUPER_CAPABLE | SA1_CREATE_STAGE_TASKS, -1, 1, SA1_SUPER_GENERAL, &st)
              == 0 && st == SA1_ERR_INVALID_ARGUMENT, "General with SA1_CREATE_STAGE_TASKS: %s", sa1_status_string(st));
    CHECK(mk(ctx, SA1_CHAR_SONIC, at, 0, -1, 1, SA1_SUPER_GENERAL, &st) == 0 && st == SA1_ERR_INVALID_ARGUMENT,
          "SA1_SUPER_GENERAL without SA1_CREATE_SUPER_CAPABLE: %s", sa1_status_string(st));
    /* level 19 with the General form: mode 0 sets the 12 Super welds up itself, GW copies them */
    {
        sa1_character_desc d;
        memset(&d, 0, sizeof d);
        d.struct_size = sizeof d;
        d.kind = SA1_CHAR_SONIC;
        d.flags = SA1_CREATE_WORLD | SA1_CREATE_SA1_SPACE | SA1_CREATE_ANGLES | SA1_CREATE_SUPER_CAPABLE |
                  SA1_CREATE_SUPER | SA1_CREATE_STAGE;
        d.super_flags = SA1_SUPER_GENERAL;
        d.level = 19;
        memcpy(d.position, at, sizeof d.position);
        id = 0;
        st = sa1_character_create_ex(ctx, &d, &id);
        if (!SA1_FAILED(st) && id != 0) {
            run(ctx, id, 3, 0.0f, 0.0f, NULL, &x);
            w = sa1_character_world(ctx, id);
            gw_stats(w, &n, &ns, &no, &nd);
            CHECK(w->level == 19 && w->sg.welds == 1 && n == 39 && ns == 12 && no == 12 && !(x.equipment & 0x8000u),
                  "General on level 19: stage %d, GW %u entries, %u Super welds with %u org buffers (mode 0's own)",
                  (int)w->level, (unsigned)n, (unsigned)ns, (unsigned)no);
            sa1_character_destroy(ctx, id);
        } else {
            CHECK(0, "General on level 19: %s %s", sa1_status_string(st), sa1_last_error(ctx));
        }
        /* DISC collision of Emerald Coast: refused */
    }
}

static void t_general_welds(sa1_context *ctx)
{
    static const float at[3] = { 0.0f, 0.5f, 0.0f };
    sa1_character_id id;
    sa1g_world *w;
    sa1_state_ex x;
    uint32_t n, ns, no, nd, son_super_on = 0, son_super = 0, e, k;
    float h[3][3];
    int i, moved;
    printf("general_welds (GRID): the game's weld set-up after the first pair (design 4.2)\n");
    if (scene_grid(ctx, 0) != 0 || (id = mk_gen(ctx, at, 50, 0, NULL)) == 0) {
        g_fail++;
        return;
    }
    w = sa1_character_world(ctx, id);
    CHECK(w->sg.welds == 0 && w->sg.gw == 0, "before the first pair: no GW yet");
    tick(ctx, id, 0.0f, 0.0f, 0, NULL, &x);
    gw_stats(w, &n, &ns, &no, &nd);
    for (k = 0, e = A_WELDS; k < 64u && rd32(w, e) != 0; k++, e += 0x18u) {
        if ((rd32(w, e) & 0x1FFFFFFFu) == (O_SUPER_OBJECT & 0x1FFFFFFFu)) {
            son_super++;
            son_super_on += rd8w(w, e + 0x0Du) != 0;
        }
    }
    CHECK(w->sg.welds == 1 && n == 39 && ns == 12 && no == 12 && nd == 17 && rd32(w, pwp0(w) + 0x144u) == w->sg.gw &&
              son_super == 12 && son_super_on == 0,
          "after pair 1: GW %u entries (34 + 5), %u Super entries at inpmode 2, %u org buffers, %u of 17 depths set; "
          "SonicWeldList's own %u Super entries still at inpmode 0 (%u not)", (unsigned)n, (unsigned)ns, (unsigned)no,
          (unsigned)nd, (unsigned)son_super, (unsigned)son_super_on);
    sa1_character_order(ctx, id, SA1_ORDER_SUPER);
    for (i = 0; i < 40; i++) {
        tick(ctx, id, 0.0f, 1.0f, 0, NULL, &x);
    }
    for (i = 0; i < 3; i++) {
        tick(ctx, id, 0.0f, 1.0f, 0, NULL, &x);
        memcpy(h[i], sa1g_world_ptr(w, pwp0(w) + 0x14Cu), 12);
    }
    moved = fabsf(h[0][0] - h[1][0]) + fabsf(h[0][1] - h[1][1]) + fabsf(h[0][2] - h[1][2]) +
            fabsf(h[1][0] - h[2][0]) + fabsf(h[1][1] - h[2][1]) + fabsf(h[1][2] - h[2][2]) > 0.01f;
    CHECK((x.equipment & 0x8000u) && moved, "running Super: the right-hand point moves with the Super tree ((%.2f %.2f %.2f) "
          "(%.2f %.2f %.2f) (%.2f %.2f %.2f))", (double)h[0][0], (double)h[0][1], (double)h[0][2], (double)h[1][0],
          (double)h[1][1], (double)h[1][2], (double)h[2][0], (double)h[2][1], (double)h[2][2]);
    sa1_character_destroy(ctx, id);
}

/* the body draws of the last pair by archive: total, from archive a, effect-flagged, CPU-pose draws */
typedef struct bstat {
    uint32_t body, in_arch, effect, pose, pose_in_arch, additive_in_arch;
} bstat;

static void body_stats(sa1_context *ctx, sa1_character_id id, int32_t arch, bstat *b)
{
    const sa1_render *rd = NULL;
    uint32_t i;
    memset(b, 0, sizeof *b);
    if (sa1_character_render(ctx, id, 1.0f, &rd) != SA1_OK || rd == NULL) {
        return;
    }
    for (i = 0; i < rd->num_draws; i++) {
        const sa1_draw *d = &rd->draws[i];
        if (d->prim != SA1_PRIM_TRIANGLES) {
            continue;
        }
        if ((d->flags & SA1_DRAW_ADDITIVE) && d->archive == arch) {
            b->additive_in_arch++;
        }
        if (d->source == SA1_SOURCE_POSE && d->archive >= 0) {
            b->pose++;
            b->pose_in_arch += d->archive == arch;
        }
        if (d->source == SA1_SOURCE_CHARACTER && !(d->flags & (SA1_DRAW_SHADOW | SA1_DRAW_ADDITIVE)) && d->archive >= 0) {
            b->body++;
            b->in_arch += d->archive == arch;
            b->effect += (d->flags & SA1_DRAW_EFFECT) != 0;
        }
    }
}

static void t_general_transform_air(sa1_context *ctx)
{
    static const float at[3] = { -2000.0f, 0.5f, 0.0f };
    sa1_character_id id;
    sa1g_world *w;
    sa1_state_ex x0, x1;
    evlog L;
    int32_t a_ss = archive_named(ctx, SA1_CHAR_SONIC, "SUPERSONIC");
    uint32_t n1116 = 0, k;
    int voice;
    bstat b;
    printf("general_transform_air (GRID): SA1_ORDER_SUPER in mid-jump keeps the jump (design 4.4)\n");
    for (voice = 1; voice >= 0; voice--) {
        memset(&L, 0, sizeof L);
        if (scene_grid(ctx, 0) != 0 || (id = mk_gen(ctx, at, 50, 0, NULL)) == 0) {
            g_fail++;
            return;
        }
        w = sa1_character_world(ctx, id);
        if (voice) {
            CHECK(sa1_character_set_super_option(ctx, id, SA1_SUPOPT_VOICE, 1116) == SA1_OK, "SA1_SUPOPT_VOICE 1116");
        }
        run(ctx, id, 30, 0.0f, 1.0f, NULL, &x0);
        if (into_jump(ctx, id, 1.0f, &x0) != 0) {
            CHECK(0, "no jump (mode %d)", (int)x0.mode);
            sa1_character_destroy(ctx, id);
            return;
        }
        tick(ctx, id, 0.0f, 1.0f, SA1_BUTTON_JUMP, NULL, &x0);
        CHECK(sa1_character_order(ctx, id, SA1_ORDER_SUPER) == SA1_OK, "SA1_ORDER_SUPER in mode %d (action %d)",
              (int)x0.mode, (int)x0.action);
        tick(ctx, id, 0.0f, 1.0f, SA1_BUTTON_JUMP, &L, &x1);
        for (k = 0; k < L.n; k++) {
            n1116 += L.e[k].type == SA1_EVENT_SOUND && L.e[k].id == 1116u;
        }
        body_stats(ctx, id, a_ss, &b);
        CHECK(x0.mode == 8 && x1.mode == 8 && x1.action == x0.action && (x1.equipment & 0x8000u) &&
                  fabsf(x1.player_speed[0] - x0.player_speed[0]) < 0.3f && find_task(w, X_PHYS) != 0 &&
                  (find_task(w, X_AURA) != 0 || find_task(w, X_AURA_INIT) != 0),
              "%s: mode %d -> %d, action %d -> %d, speed (%.3f %.3f) -> (%.3f %.3f), equipment %04X, the aura and physics "
              "tasks", voice ? "voice 1116" : "no voice", (int)x0.mode, (int)x1.mode, (int)x0.action, (int)x1.action,
              (double)x0.player_speed[0], (double)x0.player_speed[1], (double)x1.player_speed[0],
              (double)x1.player_speed[1], (unsigned)x1.equipment);
        CHECK(voice ? n1116 == 1 : n1116 == 0, "SA1_EVENT_SOUND 1116: %u (%s)", (unsigned)n1116, voice ? "1" : "0");
        CHECK(b.body >= 1 && b.in_arch == b.body && b.effect == 0, "the jump ball: %u body draws, %u of SUPERSONIC "
              "(archive %d), %u effect-flagged", (unsigned)b.body, (unsigned)b.in_arch, (int)a_ss, (unsigned)b.effect);
        n1116 = 0;
        free(L.e);
        sa1_character_destroy(ctx, id);
    }
}

static void t_general_look(sa1_context *ctx)
{
    static const float at[3] = { -2000.0f, 0.5f, 0.0f };
    sa1_character_id id, son;
    sa1g_world *w;
    sa1_state_ex x;
    int32_t a_ss = archive_named(ctx, SA1_CHAR_SONIC, "SUPERSONIC");
    bstat b;
    float f_son, f_gen, f_k8;
    int up;
    printf("general_look (GRID): the Super body and textures on Sonic's actions (design 4.3, review RC15)\n");
    for (up = 0; up < 2; up++) {
        if (scene_grid(ctx, 0) != 0 || (id = mk_gen(ctx, at, 50, SA1_SUPER_START | SA1_SUPER_NO_DRAIN, NULL)) == 0) {
            g_fail++;
            return;
        }
        if (up) {
            give_upgrades(ctx, id);
        }
        w = sa1_character_world(ctx, id);
        run(ctx, id, 60, 0.0f, 1.0f, NULL, &x);
        body_stats(ctx, id, a_ss, &b);
        CHECK(x.mode == 2 && (x.equipment & 0x8000u) && (x.equipment & 7u) == (up ? 7u : 0u) && b.body == 47 &&
                  b.in_arch == 47 && b.effect == 0 && rd32(w, A_TEXLIST_SONIC) == NAMES_SUPER,
              "running%s: mode %d, equipment %04X, %u body draws (47), %u of SUPERSONIC, %u effect-flagged; texlist_SONIC "
              "names %08X", up ? " with Sonic's three upgrades" : "", (int)x.mode, (unsigned)x.equipment,
              (unsigned)b.body, (unsigned)b.in_arch, (unsigned)b.effect, (unsigned)rd32(w, A_TEXLIST_SONIC));
        if (!up) {
            run(ctx, id, 80, 0.0f, 0.0f, NULL, &x);
            f_gen = feet_of(ctx, id);
            if (g_verbose) {
                int q;
                sa1_character_id ps = mk(ctx, SA1_CHAR_SONIC, at, 0, -1, 0, 0, NULL);
                sa1_state_ex y;
                run(ctx, ps, 60, 0.0f, 1.0f, NULL, &y);
                for (q = 0; q < 200; q++) {
                    tick(ctx, id, 0.0f, 0.0f, 0, NULL, &x);
                    tick(ctx, ps, 0.0f, 0.0f, 0, NULL, &y);
                    if (q % 10 == 0) {
                        printf("    pair %d: General mode %d action %d feet %.3f | Sonic mode %d action %d feet %.3f" "%c", q,
                               (int)x.mode, (int)x.action, (double)feet_of(ctx, id), (int)y.mode, (int)y.action,
                               (double)feet_of(ctx, ps), 10);
                    }
                }
                sa1_character_destroy(ctx, ps);
            }
            son = mk(ctx, SA1_CHAR_SONIC, at, 0, -1, 0, 0, NULL);
            run(ctx, son, 60, 0.0f, 0.0f, NULL, NULL);
            f_son = feet_of(ctx, son);
            sa1_character_destroy(ctx, son);
            son = mk(ctx, SA1_CHAR_SUPER_SONIC, at, 0, -1, 1, SA1_SUPER_NO_DRAIN, NULL);
            run(ctx, son, 60, 0.0f, 0.0f, NULL, NULL);
            f_k8 = feet_of(ctx, son);
            sa1_character_destroy(ctx, son);
            printf("  info  standing, the lowest body point above the drawn root: General %.3f (mode %d action %d), plain "
                   "Sonic %.3f, kind 8 (the game's Super stand, action 130) %.3f\n", (double)f_gen, (int)x.mode,
                   (int)x.action, (double)f_son, (double)f_k8);
            /* design 8.2 asked for 0.25 of plain Sonic's (UNVERIFIED): at the physics task's center_height 6.4 the Super body
               stood 1.02 above the ground (Sonic 0.40); with Sonic's 5.4 (sa1_world_super.c su_physics_init) it touches it */
            CHECK(x.mode == 1 && f_gen > -0.1f && fabsf(f_gen - f_son) < 0.45f && rd32(sa1_character_world(ctx, id),
                  pwp0(sa1_character_world(ctx, id)) + 0x11Cu) == rd32(sa1_character_world(ctx, id), A_PHYSICS + 0x80u),
                  "standing: the Super body's lowest point %.3f above the root (on the ground; plain Sonic's %.3f), "
                  "center_height Sonic's", (double)f_gen, (double)f_son);
        }
        sa1_character_destroy(ctx, id);
    }
}

/* the Light Speed charge, the spin dash, the Super homing and a carried HOLDABLE in the General form */
static void t_general_moves(sa1_context *ctx)
{
    static const float at[3] = { -2000.0f, 0.5f, 0.0f };
    sa1_character_id id;
    sa1g_world *w;
    sa1_state_ex x;
    int32_t a_ss = archive_named(ctx, SA1_CHAR_SONIC, "SUPERSONIC");
    bstat b;
    int i, m4 = 0, m5 = 0, charged = 0, sup;
    float vh[2] = { 0.0f, 0.0f };
    printf("general_moves (GRID): Sonic's moves in the General form\n");
    if (scene_grid(ctx, 0) != 0) {
        g_fail++;
        return;
    }
    /* spin dash and the Light Speed charge with the shoes (equipment 0x8007) */
    if ((id = mk_gen(ctx, at, 50, SA1_SUPER_START | SA1_SUPER_NO_DRAIN, NULL)) == 0) {
        g_fail++;
        return;
    }
    give_upgrades(ctx, id);
    w = sa1_character_world(ctx, id);
    run(ctx, id, 10, 0.0f, 0.0f, NULL, &x);
    for (i = 0; i < 80; i++) {
        tick(ctx, id, 0.0f, 0.0f, SA1_BUTTON_ACTION, NULL, &x);
        m4 += x.mode == 4;
        charged |= (rd16w(w, twp0w(w) + 4u) & 0x200u) != 0;
        if (i == 10) {
            body_stats(ctx, id, a_ss, &b);
        }
    }
    CHECK(m4 > 0 && charged && (x.equipment & 0x8007u) == 0x8007u && b.body >= 1 && b.in_arch == b.body,
          "holding the action button 80 pairs with the shoes: mode 4 (spin dash charge) %u pairs, the Light Speed charge "
          "(taskwk.flag 0x200) %s, equipment %04X, the Super ball (%u of %u draws SUPERSONIC)", (unsigned)m4,
          charged ? "reached" : "NOT reached", (unsigned)x.equipment, (unsigned)b.in_arch, (unsigned)b.body);
    for (i = 0; i < 20; i++) {
        tick(ctx, id, 0.0f, 0.0f, 0, NULL, &x);
        m5 += x.mode == 5;
    }
    printf("  info  after the release: %u pairs in mode 5, mode %d now, speed %.3f\n", (unsigned)m5, (int)x.mode,
           (double)x.player_speed[0]);
    sa1_character_destroy(ctx, id);
    /* plain spin dash (no shoes): modes 4 / 5 with the Super ball */
    if ((id = mk_gen(ctx, at, 50, SA1_SUPER_START | SA1_SUPER_NO_DRAIN, NULL)) == 0) {
        g_fail++;
        return;
    }
    run(ctx, id, 10, 0.0f, 0.0f, NULL, &x);
    m4 = m5 = 0;
    for (i = 0; i < 20; i++) {
        tick(ctx, id, 0.0f, 0.0f, SA1_BUTTON_ACTION, NULL, &x);
        m4 += x.mode == 4;
    }
    for (i = 0; i < 6; i++) {
        tick(ctx, id, 0.0f, 0.0f, 0, NULL, &x);
        m5 += x.mode == 5;
        if (i == 1) {
            body_stats(ctx, id, a_ss, &b);
        }
    }
    CHECK(m4 > 0 && m5 > 0 && (x.equipment & 0x8000u) && b.body >= 1 && b.in_arch == b.body,
          "spin dash: mode 4 %u pairs, mode 5 %u pairs, still Super, the Super ball (%u of %u draws)", (unsigned)m4,
          (unsigned)m5, (unsigned)b.in_arch, (unsigned)b.body);
    sa1_character_destroy(ctx, id);
    /* the air dash (jump, jump again): the game's Super homing by the bit, against plain Sonic's */
    for (sup = 0; sup < 2; sup++) {
        id = sup ? mk_gen(ctx, at, 50, SA1_SUPER_START | SA1_SUPER_NO_DRAIN, NULL) : mk(ctx, SA1_CHAR_SONIC, at, 0, 50, 0, 0,
                                                                                      NULL);
        if (id == 0) {
            g_fail++;
            return;
        }
        run(ctx, id, 20, 0.0f, 1.0f, NULL, &x);
        into_jump(ctx, id, 1.0f, &x);
        tick(ctx, id, 0.0f, 1.0f, 0, NULL, &x);
        tick(ctx, id, 0.0f, 1.0f, SA1_BUTTON_JUMP, NULL, &x);
        for (i = 0; i < 8; i++) {
            tick(ctx, id, 0.0f, 1.0f, 0, NULL, &x);
            if (x.player_speed[0] > vh[sup]) {
                vh[sup] = x.player_speed[0];
            }
        }
        sa1_character_destroy(ctx, id);
    }
    CHECK(vh[1] > 5.5f && vh[1] > 1.8f * vh[0], "the air dash without a target: top speed %.2f as General Super (the "
          "bit's Super dash; res-game 6.33 over 8 pairs), %.2f as plain Sonic (2.97)", (double)vh[1], (double)vh[0]);
    /* carrying: a HOLDABLE lifted and carried by the General Super Sonic (the hold context: hostobj_test's environment) */
    {
        sa1_context *hc = hold_ctx();
        int32_t a_h = hc ? archive_named(hc, SA1_CHAR_SONIC, "SUPERSONIC") : -1;
        uint32_t obj = 0, held = 0, near = 0;
        sa1_hostobj_held hd;
        id = hc ? hold_char(hc, SA1_CHAR_SONIC, SA1_CREATE_SUPER_CAPABLE,
                            SA1_SUPER_GENERAL | SA1_SUPER_START | SA1_SUPER_NO_DRAIN) : 0;
        if (id == 0 || (obj = hold_lift(hc, id)) == 0) {
            CHECK(0, "carrying: the General Super Sonic did not lift the HOLDABLE");
        } else {
            for (i = 0; i < 30; i++) {
                tick(hc, id, 0.0f, 1.0f, 0, NULL, &x);
                memset(&hd, 0, sizeof hd);
                hd.struct_size = sizeof hd;
                sa1_character_held(hc, id, &hd);
                held += hd.object == obj;
                near += hd.object == obj && fabsf(hd.position[0] - hd.hand[0]) + fabsf(hd.position[1] - hd.hand[1]) +
                                                    fabsf(hd.position[2] - hd.hand[2]) < 48.0f;
            }
            body_stats(hc, id, a_h, &b);
            CHECK(held == 30 && near == 30 && (x.equipment & 0x8000u) && b.body > 0 && b.in_arch == b.body,
                  "carrying: held %u of 30 pairs, at the hand anchor %u, still Super, the Super body (%u of %u draws)",
                  (unsigned)held, (unsigned)near, (unsigned)b.in_arch, (unsigned)b.body);
        }
        if (hc) {
            sa1_shutdown(hc);
        }
    }
}

/* top run speed after 150 pairs with the stick forward on a 20,000-unit floor */
static float top_speed(sa1_context *ctx, sa1_character_id id, int pairs)
{
    sa1_state_ex x;
    float v = 0.0f;
    int i;
    for (i = 0; i < pairs; i++) {
        tick(ctx, id, 0.0f, 1.0f, 0, NULL, &x);
        if (x.player_speed[0] > v) {
            v = x.player_speed[0];
        }
    }
    return v;
}

/* a running jump after 60 pairs: the horizontal distance from the take-off to the landing */
static float run_jump(sa1_context *ctx, sa1_character_id id)
{
    sa1_state_ex x;
    float p0[3];
    int i;
    run(ctx, id, 60, 0.0f, 1.0f, NULL, &x);
    memcpy(p0, x.sa1_position, sizeof p0);
    tick(ctx, id, 0.0f, 1.0f, SA1_BUTTON_JUMP, NULL, &x);
    for (i = 0; i < 120; i++) {
        tick(ctx, id, 0.0f, 1.0f, i < 20 ? SA1_BUTTON_JUMP : 0u, NULL, &x);
        if (i > 3 && (x.flags & SA1_STATE_ON_GROUND)) {
            break;
        }
    }
    return sqrtf((x.sa1_position[0] - p0[0]) * (x.sa1_position[0] - p0[0]) +
                 (x.sa1_position[2] - p0[2]) * (x.sa1_position[2] - p0[2]));
}

static void t_general_speed(sa1_context *ctx)
{
    static const float at[3] = { -9500.0f, 0.5f, 0.0f };
    static const float want[4] = { 5.90f, 5.90f, 4.88f, 4.31f };
    static const char *const name[4] = { "GAME", "RUN", "HALF", "SONIC" };
    sa1_character_id id;
    sa1g_world *w;
    float v, j_son = 0.0f, j_run = 0.0f, v_son;
    int s;
    printf("general_speed (GRID, a 20,000-unit floor): the four speed sets (design 4.5)\n");
    if (scene_big(ctx, 10000.0f, 0, 0.0f) != 0) {
        g_fail++;
        return;
    }
    id = mk(ctx, SA1_CHAR_SONIC, at, 0, 50, 0, 0, NULL);
    v_son = top_speed(ctx, id, 150);
    sa1_character_destroy(ctx, id);
    id = mk(ctx, SA1_CHAR_SONIC, at, 0, 50, 0, 0, NULL);
    j_son = run_jump(ctx, id);
    sa1_character_destroy(ctx, id);
    for (s = 0; s < 4; s++) {
        if ((id = mk_gen(ctx, at, 50, SA1_SUPER_START | SA1_SUPER_NO_DRAIN, NULL)) == 0) {
            g_fail++;
            return;
        }
        sa1_character_set_super_option(ctx, id, SA1_SUPOPT_SPEED, s);
        v = top_speed(ctx, id, 150);
        CHECK(fabsf(v - want[s]) < 0.05f, "%s: the run speed after 150 pairs %.3f (res-game %.2f; plain Sonic %.3f: his "
              "top)", name[s], (double)v, (double)want[s], (double)v_son);
        sa1_character_destroy(ctx, id);
        if (s == SA1_SUPER_SPEED_RUN) {
            id = mk_gen(ctx, at, 50, SA1_SUPER_START | SA1_SUPER_NO_DRAIN, NULL);
            sa1_character_set_super_option(ctx, id, SA1_SUPOPT_SPEED, s);
            j_run = run_jump(ctx, id);
            sa1_character_destroy(ctx, id);
        }
    }
    CHECK(fabsf(j_run - j_son) < 1.0f, "a running jump after 60 pairs: RUN %.1f units, plain Sonic %.1f (Sonic's jumps)",
          (double)j_run, (double)j_son);
    /* a set change while Super applies at once */
    id = mk_gen(ctx, at, 50, SA1_SUPER_START | SA1_SUPER_NO_DRAIN, NULL);
    run(ctx, id, 4, 0.0f, 0.0f, NULL, NULL);
    w = sa1_character_world(ctx, id);
    {
        uint32_t pwp = pwp0(w), g0 = rd32(w, pwp + 0xECu), g1, g2;
        sa1_character_set_super_option(ctx, id, SA1_SUPOPT_SPEED, SA1_SUPER_SPEED_RUN);
        g1 = rd32(w, pwp + 0xECu);
        sa1_character_set_super_option(ctx, id, SA1_SUPOPT_SPEED, SA1_SUPER_SPEED_GAME);
        g2 = rd32(w, pwp + 0xECu);
        CHECK(g0 == 0xBB03126Eu && g1 == rd32(w, A_PHYSICS + 0x50u) && rd32(w, pwp + 0xDCu) == 0x3D4CCCCCu &&
                  g2 == 0xBB03126Eu && rd32(w, pwp + 0xF0u) == 0xBA83126Eu,
              "while Super: GAME air_resist_air %08X, RUN at once %08X (Sonic's %08X), GAME again %08X", (unsigned)g0,
              (unsigned)g1, (unsigned)rd32(w, A_PHYSICS + 0x50u), (unsigned)g2);
    }
    sa1_character_destroy(ctx, id);
}

static void t_general_revert(sa1_context *ctx)
{
    static const float at[3] = { -2000.0f, 0.5f, 0.0f };
    sa1_character_id id;
    sa1g_world *w;
    sa1_state_ex x;
    int32_t a_son = archive_named(ctx, SA1_CHAR_SONIC, "SONIC");
    bstat b;
    int i, srev = -1, smode47 = 0;
    printf("general_revert (GRID): back to Sonic by order and by the drain at 0 rings (design 4.4 / 4.6)\n");
    if (scene_grid(ctx, 0) != 0 || (id = mk_gen(ctx, at, 50, SA1_SUPER_START | SA1_SUPER_NO_DRAIN, NULL)) == 0) {
        g_fail++;
        return;
    }
    give_upgrades(ctx, id);
    w = sa1_character_world(ctx, id);
    run(ctx, id, 40, 0.0f, 1.0f, NULL, &x);
    CHECK(sa1_character_order(ctx, id, SA1_ORDER_NORMAL) == SA1_OK, "SA1_ORDER_NORMAL while Super (equipment %04X)",
          (unsigned)x.equipment);
    run(ctx, id, 2, 0.0f, 1.0f, NULL, &x);
    body_stats(ctx, id, a_son, &b);
    CHECK(!(x.equipment & 0x8000u) && (x.equipment & 7u) == 7u && phys_diff(w, pwp0(w)) == 0 && find_task(w, X_AURA) == 0 &&
              find_task(w, X_PHYS) == 0 && w->sg.look == 0 && rd32(w, A_TEXLIST_SONIC) == NAMES_SONIC &&
              rd32(w, pwp0(w) + 0x140u) == A_SONIC_ACTIONS,
          "the order: equipment %04X (the upgrades kept), playerwk.p = Sonic's entry (mask %llX), the aura and physics "
          "tasks gone, the look off", (unsigned)x.equipment, (unsigned long long)phys_diff(w, pwp0(w)));
    run(ctx, id, 20, 0.0f, 1.0f, NULL, &x);
    body_stats(ctx, id, a_son, &b);
    CHECK(x.mode == 2 && b.body == 49 && b.in_arch == 49 && b.effect == 0, "running as Sonic: %u body draws (45 + the 4 "
          "upgrade parts), %u of SONIC, %u effect-flagged", (unsigned)b.body, (unsigned)b.in_arch, (unsigned)b.effect);
    sa1_character_destroy(ctx, id);
    /* the drain: 1 ring, Super after pair 1, the revert at 0 rings in place of the drain's request 47 */
    if ((id = mk_gen(ctx, at, 1, SA1_SUPER_START, NULL)) == 0) {
        g_fail++;
        return;
    }
    w = sa1_character_world(ctx, id);
    for (i = 0; i < 80 && srev < 0; i++) {
        uint32_t t;
        tick(ctx, id, 0.0f, 0.0f, 0, NULL, &x);
        t = twp0w(w);
        smode47 += t != 0 && (rd16w(w, t + 4u) & 0x1000u) && rd8w(w, t + 1u) == 47u;
        if (i > 2 && !(x.equipment & 0x8000u)) {
            srev = (int)x.step - 2;
        }
    }
    run(ctx, id, 2, 0.0f, 0.0f, NULL, &x);
    CHECK(srev > 0 && w->sg.drain_reverts == 1 && smode47 == 0 && x.rings == 0 && w->sg.look == 0 &&
              phys_diff(w, pwp0(w)) == 0 && x.mode >= 1 && x.mode <= 2,
          "the drain at 0 rings: back to Sonic in the pair of step %d, %u drain revert, no request 47 pending (%u), rings "
          "%d, mode %d, playerwk.p = Sonic's entry", srev, (unsigned)w->sg.drain_reverts, (unsigned)smode47, (int)x.rings,
          (int)x.mode);
    sa1_character_destroy(ctx, id);
}

/* the top-level tasks of the eight lists whose exec is a or b (DestroyTask-marked tasks are not counted) */
static uint32_t count_tasks(const sa1g_world *w, uint32_t a, uint32_t b)
{
    uint32_t lv, n = 0;
    for (lv = 0; lv < 8u; lv++) {
        uint32_t tp = rd32(w, A_TASK_LISTS + 4u * lv), guard = 0;
        while (arena(tp) && guard++ < 0x2000u) {
            uint32_t ex = rd32(w, tp + 0x10u) & 0x1FFFFFFFu;
            n += ex == (a & 0x1FFFFFFFu) || ex == (b & 0x1FFFFFFFu);
            tp = rd32(w, tp);
        }
    }
    return n;
}

static void t_general_reorder(sa1_context *ctx)
{
    static const float at[3] = { -2000.0f, 0.5f, 0.0f };
    sa1_character_id id;
    sa1g_world *w;
    sa1_state_ex x;
    uint32_t na, np;
    sa1_status s1, s2;
    printf("general_reorder (GRID): NORMAL then SUPER with no pair between keeps one aura and one physics task\n");
    if (scene_grid(ctx, 0) != 0 || (id = mk_gen(ctx, at, 50, SA1_SUPER_START | SA1_SUPER_NO_DRAIN, NULL)) == 0) {
        g_fail++;
        return;
    }
    give_upgrades(ctx, id);
    w = sa1_character_world(ctx, id);
    run(ctx, id, 30, 0.0f, 1.0f, NULL, &x);
    na = count_tasks(w, X_AURA_INIT, X_AURA);
    np = count_tasks(w, X_PHYS_INIT, X_PHYS);
    CHECK((x.equipment & 0x8000u) && na == 1 && np == 1, "Super after SA1_SUPER_START: %u aura, %u physics task(s)",
          (unsigned)na, (unsigned)np);
    s1 = sa1_character_order(ctx, id, SA1_ORDER_NORMAL);
    s2 = sa1_character_order(ctx, id, SA1_ORDER_SUPER);
    run(ctx, id, 3, 0.0f, 1.0f, NULL, &x);
    na = count_tasks(w, X_AURA_INIT, X_AURA);
    np = count_tasks(w, X_PHYS_INIT, X_PHYS);
    CHECK(s1 == SA1_OK && s2 == SA1_OK && (x.equipment & 0x8000u) && na == 1 && np == 1 && w->sg.look == 1,
          "NORMAL then SUPER, no pair between: Super (equipment %04X), %u aura task(s), %u physics task(s) (0.9.3a: 2 / 2)",
          (unsigned)x.equipment, (unsigned)na, (unsigned)np);
    run(ctx, id, 30, 0.0f, 1.0f, NULL, &x);
    CHECK((x.equipment & 0x8000u) && count_tasks(w, X_AURA_INIT, X_AURA) == 1 && count_tasks(w, X_PHYS_INIT, X_PHYS) == 1,
          "30 pairs later: still one of each");
    s1 = sa1_character_order(ctx, id, SA1_ORDER_SUPER);
    s2 = sa1_character_order(ctx, id, SA1_ORDER_NORMAL);
    s1 = s1 == SA1_OK ? sa1_character_order(ctx, id, SA1_ORDER_SUPER) : s1;
    run(ctx, id, 3, 0.0f, 1.0f, NULL, &x);
    CHECK(s1 == SA1_OK && s2 == SA1_OK && (x.equipment & 0x8000u) && count_tasks(w, X_AURA_INIT, X_AURA) == 1 &&
              count_tasks(w, X_PHYS_INIT, X_PHYS) == 1,
          "SUPER (already), NORMAL, SUPER with no pair between: %u aura, %u physics task(s)",
          (unsigned)count_tasks(w, X_AURA_INIT, X_AURA), (unsigned)count_tasks(w, X_PHYS_INIT, X_PHYS));
    s1 = sa1_character_order(ctx, id, SA1_ORDER_NORMAL);
    run(ctx, id, 3, 0.0f, 1.0f, NULL, &x);
    CHECK(s1 == SA1_OK && !(x.equipment & 0x8000u) && count_tasks(w, X_AURA_INIT, X_AURA) == 0 &&
              count_tasks(w, X_PHYS_INIT, X_PHYS) == 0 && phys_diff(w, pwp0(w)) == 0,
          "NORMAL: both tasks free themselves, playerwk.p = Sonic's entry (mask %llX)",
          (unsigned long long)phys_diff(w, pwp0(w)));
    sa1_character_destroy(ctx, id);
}

static void t_general_keep(sa1_context *ctx)
{
    static const float at[3] = { 0.0f, 0.5f, 0.0f };
    sa1_character_id id;
    sa1_state_ex x;
    printf("general_keep / general_nodrain (GRID)\n");
    if (scene_grid(ctx, 0) != 0 || (id = mk_gen(ctx, at, 1, SA1_SUPER_START | SA1_SUPER_KEEP_AT_ZERO, NULL)) == 0) {
        g_fail++;
        return;
    }
    run(ctx, id, 150, 0.0f, 0.0f, NULL, &x);
    CHECK(x.rings == 0 && (x.equipment & 0x8000u) && x.mode < 71, "KEEP_AT_ZERO, 1 ring, 300 steps: rings %d, equipment "
          "%04X, mode %d (Sonic's modes)", (int)x.rings, (unsigned)x.equipment, (int)x.mode);
    sa1_character_destroy(ctx, id);
    if ((id = mk_gen(ctx, at, 50, SA1_SUPER_START | SA1_SUPER_NO_DRAIN, NULL)) == 0) {
        g_fail++;
        return;
    }
    run(ctx, id, 150, 0.0f, 0.0f, NULL, &x);
    CHECK(x.rings == 50 && (x.equipment & 0x8000u) && sa1_character_world(ctx, id)->super_drains == 0,
          "NO_DRAIN, 300 steps: rings %d, equipment %04X, %u drain calls", (int)x.rings, (unsigned)x.equipment,
          (unsigned)sa1_character_world(ctx, id)->super_drains);
    sa1_character_destroy(ctx, id);
}

static void t_general_start(sa1_context *ctx)
{
    static const float at[3] = { 0.0f, 0.5f, 0.0f };
    sa1_character_id id;
    sa1_state_ex x;
    evlog L;
    uint32_t n;
    printf("general_start (GRID): SA1_SUPER_START, and an order before the first pair\n");
    memset(&L, 0, sizeof L);
    if (scene_grid(ctx, 0) != 0 || (id = mk_gen(ctx, at, 50, SA1_SUPER_START, NULL)) == 0) {
        g_fail++;
        return;
    }
    sa1_character_set_super_option(ctx, id, SA1_SUPOPT_VOICE, 1116);
    tick(ctx, id, 0.0f, 0.0f, 0, &L, &x);
    ev_find(&L, SA1_EVENT_SOUND, 1116u, 0, &n);
    CHECK((x.equipment & 0x8000u) && x.step == 2 && n == 0, "SA1_SUPER_START: Super right after pair 1 (step %u, "
          "equipment %04X), no voice (%u)", (unsigned)x.step, (unsigned)x.equipment, (unsigned)n);
    sa1_character_destroy(ctx, id);
    L.n = 0;
    if ((id = mk_gen(ctx, at, 50, 0, NULL)) == 0) {
        g_fail++;
        return;
    }
    sa1_character_set_super_option(ctx, id, SA1_SUPOPT_VOICE, 1116);
    CHECK(sa1_character_order(ctx, id, SA1_ORDER_SUPER) == SA1_OK, "SA1_ORDER_SUPER before the first pair: SA1_OK (it "
          "waits)");
    tick(ctx, id, 0.0f, 0.0f, 0, &L, &x);
    ev_find(&L, SA1_EVENT_SOUND, 1116u, 0, &n);
    CHECK((x.equipment & 0x8000u) && n == 1, "after pair 1: Super (equipment %04X), the voice once (%u)",
          (unsigned)x.equipment, (unsigned)n);
    free(L.e);
    sa1_character_destroy(ctx, id);
}

static void t_general_link(sa1_context *ctx)
{
    static const float at[3] = { -2000.0f, 0.5f, 0.0f };
    sa1_character_id id;
    sa1g_world *w;
    sa1_state_ex x;
    int32_t a_ss = archive_named(ctx, SA1_CHAR_SONIC, "SUPERSONIC");
    bstat b;
    int i, found = -1;
    printf("general_link (GRID): a transformation during a link action (mtnmode 2)\n");
    if (scene_grid(ctx, 0) != 0 || (id = mk_gen(ctx, at, 50, SA1_SUPER_NO_DRAIN, NULL)) == 0) {
        g_fail++;
        return;
    }
    w = sa1_character_world(ctx, id);
    run(ctx, id, 2, 0.0f, 0.0f, NULL, &x);
    for (i = 0; i < 400 && found < 0; i++) {
        uint32_t b0 = (i % 40) == 10 ? SA1_BUTTON_JUMP : 0u;
        tick(ctx, id, (i / 60) % 2 ? 0.6f : 0.0f, (i % 80) < 50 ? 1.0f : 0.0f, b0, NULL, &x);
        if ((int16_t)rd16w(w, pwp0(w) + 0x120u) == 2 && arena(rd32(w, pwp0(w) + 0x148u)) &&
            (rd32(w, rd32(w, pwp0(w) + 0x148u)) & 0x1FFFFFFFu) == 0x0C1CE76Cu) {
            found = i;
        }
    }
    if (found < 0) {
        printf("  info  no link action on Sonic's body in 400 pairs: general_link not run\n");
        sa1_character_destroy(ctx, id);
        return;
    }
    sa1_character_order(ctx, id, SA1_ORDER_SUPER);
    {
        uint32_t lk = rd32(w, pwp0(w) + 0x148u), obj = rd32(w, lk);
        CHECK(w->sg.link_fixes == 1 && (obj & 0x1FFFFFFFu) == (O_SUPER_OBJECT & 0x1FFFFFFFu),
              "pair %d, mtnmode 2: the transformation re-points the link action's object at the Super body (%08X, %u fix)",
              found, (unsigned)obj, (unsigned)w->sg.link_fixes);
    }
    tick(ctx, id, 0.0f, 0.0f, 0, NULL, &x);
    body_stats(ctx, id, a_ss, &b);
    CHECK(b.body > 0 && b.in_arch == b.body, "the next pair: %u body draws, %u of SUPERSONIC (none of SONIC)",
          (unsigned)b.body, (unsigned)b.in_arch);
    sa1_character_destroy(ctx, id);
}

/* the screen words njInitSystem sets (tests/world_replay.c wr_screen_of): zero, the visibility test never passes */
static void zero_screen(sa1g_world *w)
{
    static const uint32_t words[] = { 0x8C88F5E4u, 0x8C88F5E8u, 0x8C88F5ECu, 0x8C88F600u, 0x8C88F604u, 0x8C88F608u,
                                      0x8C88F60Cu, 0x8C88F618u, 0x8C88F61Cu, 0x8C88F620u, 0x8C88F624u, 0x8C88F628u,
                                      0x8C88F644u, 0x8C88F648u };
    size_t i;
    for (i = 0; i < sizeof words / sizeof words[0]; i++) {
        sa1g_world_wr32(w, words[i], 0u);
    }
}

static void t_general_cpu_pose(sa1_context *ctx)
{
    static const float at[3] = { -2000.0f, 0.5f, 0.0f };
    sa1_character_id id;
    sa1_state_ex x;
    int32_t a_ss = archive_named(ctx, SA1_CHAR_SONIC, "SUPERSONIC");
    bstat b;
    int ball;
    printf("general_cpu_pose (GRID): hidden pairs of the General form draw the Super body / ball (review RC15)\n");
    for (ball = 0; ball < 2; ball++) {
        if (scene_grid(ctx, 0) != 0 || (id = mk_gen(ctx, at, 50, SA1_SUPER_START | SA1_SUPER_NO_DRAIN, NULL)) == 0) {
            g_fail++;
            return;
        }
        run(ctx, id, 10, 0.0f, 0.0f, NULL, &x);
        if (ball) {
            run(ctx, id, 10, 0.0f, 1.0f, NULL, &x);
            into_jump(ctx, id, 1.0f, &x);
        }
        zero_screen(sa1_character_world(ctx, id));
        tick(ctx, id, 0.0f, ball ? 1.0f : 0.0f, ball ? SA1_BUTTON_JUMP : 0u, NULL, &x);
        tick(ctx, id, 0.0f, ball ? 1.0f : 0.0f, ball ? SA1_BUTTON_JUMP : 0u, NULL, &x);
        body_stats(ctx, id, a_ss, &b);
        CHECK((x.equipment & 0x8000u) && b.pose > 0 && b.pose_in_arch == b.pose && b.body == 0,
              "%s, the camera blind (hidden pairs): the CPU pose has %u draws, %u of SUPERSONIC (archive %d); mode %d action "
              "%d", ball ? "in the jump ball" : "standing", (unsigned)b.pose, (unsigned)b.pose_in_arch, (int)a_ss,
              (int)x.mode, (int)x.action);
        sa1_character_destroy(ctx, id);
    }
}

typedef struct setinput_req {
    int32_t req;
} setinput_req;

static void setinput_cb(sa1g_world *w, void *user)
{
    (void)w;
    ((void (*)(uint8_t, int32_t))sa1_fn(SA1_GFN_C(0x8C07C65Eu), 0u))(0, ((setinput_req *)user)->req);
}

static void t_general_bitguard(sa1_context *ctx)
{
    static const float at[3] = { 0.0f, 0.5f, 0.0f };
    sa1_character_id id;
    sa1g_world *w;
    sa1_state_ex x;
    setinput_req q;
    int i, m20 = 0;
    printf("general_bitguard (GRID): the game clears the bit itself (mode 20's Super handler takes request 47)\n");
    if (scene_grid(ctx, 0) != 0 || (id = mk_gen(ctx, at, 50, SA1_SUPER_START | SA1_SUPER_NO_DRAIN, NULL)) == 0) {
        g_fail++;
        return;
    }
    w = sa1_character_world(ctx, id);
    run(ctx, id, 10, 0.0f, 0.0f, NULL, &x);
    q.req = 12;     /* a scripted pose: mode 20 */
    sa1g_world_call(w, setinput_cb, &q);
    for (i = 0; i < 4; i++) {
        tick(ctx, id, 0.0f, 0.0f, 0, NULL, &x);
        m20 += x.mode == 20;
    }
    q.req = 47;
    sa1g_world_call(w, setinput_cb, &q);
    run(ctx, id, 3, 0.0f, 0.0f, NULL, &x);
    if (m20 > 0 && !(x.equipment & 0x8000u)) {
        CHECK(w->sg.look == 0 && w->sg.bit_reverts == 1 && rd32(w, A_TEXLIST_SONIC) == NAMES_SONIC &&
                  rd32(w, pwp0(w) + 0x140u) == A_SONIC_ACTIONS,
              "request 12 (mode 20 %u pairs), then 47: the game cleared the bit; the look followed at the next pair "
              "(%u bit revert)", (unsigned)m20, (unsigned)w->sg.bit_reverts);
    } else {
        /* the game path did not clear it: the guard is shown with a direct clear (the same state the game leaves) */
        uint32_t pwp = pwp0(w);
        printf("  info  request 12 / 47: mode 20 in %u pairs, equipment %04X: the bit cleared directly instead\n",
               (unsigned)m20, (unsigned)x.equipment);
        wr16w(w, pwp + 4u, rd16w(w, pwp + 4u) & 0x7FFFu);
        tick(ctx, id, 0.0f, 0.0f, 0, NULL, &x);
        CHECK(w->sg.look == 0 && w->sg.bit_reverts >= 1 && rd32(w, A_TEXLIST_SONIC) == NAMES_SONIC,
              "the bit cleared outside the form's own revert: the look followed at the next pair (%u bit revert)",
              (unsigned)w->sg.bit_reverts);
    }
    sa1_character_destroy(ctx, id);
}

static void t_general_aura(sa1_context *ctx)
{
    static const float at[3] = { 0.0f, 0.5f, 0.0f };
    sa1_character_id id, id8, son;
    sa1g_world *w;
    sa1_state_ex x;
    int32_t a_ss = archive_named(ctx, SA1_CHAR_SONIC, "SUPERSONIC");
    bstat b0, b1, b2;
    uint32_t t_on, t_off, t_again;
    printf("general_aura (GRID): SA1_SUPOPT_NO_AURA and the option refusals\n");
    if (scene_grid(ctx, 0) != 0 || (id = mk_gen(ctx, at, 50, SA1_SUPER_START | SA1_SUPER_NO_DRAIN, NULL)) == 0) {
        g_fail++;
        return;
    }
    w = sa1_character_world(ctx, id);
    run(ctx, id, 5, 0.0f, 0.0f, NULL, &x);
    body_stats(ctx, id, a_ss, &b0);
    t_on = find_task(w, X_AURA);
    sa1_character_set_super_option(ctx, id, SA1_SUPOPT_NO_AURA, 1);
    t_off = find_task(w, X_AURA);
    run(ctx, id, 2, 0.0f, 0.0f, NULL, &x);
    body_stats(ctx, id, a_ss, &b1);
    sa1_character_set_super_option(ctx, id, SA1_SUPOPT_NO_AURA, 0);
    t_again = find_task(w, X_AURA_INIT);
    run(ctx, id, 2, 0.0f, 0.0f, NULL, &x);
    body_stats(ctx, id, a_ss, &b2);
    CHECK(t_on != 0 && b0.additive_in_arch > 0 && t_off == 0 && b1.additive_in_arch == 0 && t_again != 0 &&
              b2.additive_in_arch > 0 && (x.equipment & 0x8000u),
          "NO_AURA while Super: the aura task freed at once (%08X -> %08X), no aura draw (%u -> %u); cleared: a new task "
          "(%08X), the aura again (%u)", (unsigned)t_on, (unsigned)t_off, (unsigned)b0.additive_in_arch,
          (unsigned)b1.additive_in_arch, (unsigned)t_again, (unsigned)b2.additive_in_arch);
    sa1_character_destroy(ctx, id);
    id8 = mk(ctx, SA1_CHAR_SUPER_SONIC, at, 0, -1, 0, 0, NULL);
    son = mk(ctx, SA1_CHAR_SONIC, at, 0, -1, 0, 0, NULL);
    CHECK(sa1_character_set_super_option(ctx, id8, SA1_SUPOPT_NO_AURA, 1) == SA1_ERR_UNSUPPORTED &&
              sa1_character_set_super_option(ctx, id8, SA1_SUPOPT_SPEED, 1) == SA1_ERR_UNSUPPORTED &&
              sa1_character_set_super_option(ctx, id8, SA1_SUPOPT_SUPER_EFFECTS, 1) == SA1_ERR_UNSUPPORTED &&
              sa1_character_set_super_option(ctx, id8, SA1_SUPOPT_VOICE, 1116) == SA1_OK &&
              sa1_character_set_super_option(ctx, son, SA1_SUPOPT_VOICE, 1116) == SA1_ERR_UNSUPPORTED &&
              sa1_character_set_super_option(ctx, id8, SA1_SUPOPT_VOICE, 4096) == SA1_ERR_INVALID_ARGUMENT &&
              sa1_character_set_super_option(ctx, id8, 9, 0) == SA1_ERR_INVALID_ARGUMENT &&
              sa1_character_set_super_option(ctx, 0x7FFF0001, SA1_SUPOPT_VOICE, 1) == SA1_ERR_NOT_FOUND &&
              sa1_character_set_game_option(ctx, id8, SA1_GOPT_SUPER_AURA_ANIM, 3) == SA1_ERR_INVALID_ARGUMENT,
          "kind 8: NO_AURA / SPEED / SUPER_EFFECTS UNSUPPORTED, VOICE ok; plain Sonic: VOICE UNSUPPORTED; bad values / "
          "options INVALID_ARGUMENT; unknown id NOT_FOUND");
    sa1_character_destroy(ctx, id8);
    sa1_character_destroy(ctx, son);
}

/* the standing aura's texture over 16 pairs: distinct textures, and pairs whose texture differs from the pair before */
static void aura_seq(sa1_context *ctx, sa1_character_id id, int32_t a_ss, uint32_t *distinct, uint32_t *changes,
                     int32_t *steps, char *txt, size_t cap)
{
    int32_t t[16], i, j;
    size_t n = 0;
    uint32_t seen = 0;
    *distinct = *changes = 0;
    for (i = 0; i < 16; i++) {
        tick(ctx, id, 0.0f, 0.0f, 0, NULL, NULL);
        t[i] = aura_tex(ctx, id, a_ss);
        n += (size_t)snprintf(txt + n, n < cap ? cap - n : 0, "%d ", (int)t[i]);
        if (t[i] >= 0 && t[i] < 32) {
            seen |= 1u << t[i];
        }
        if (i > 0 && t[i] != t[i - 1]) {
            (*changes)++;
            *steps = t[i] - t[i - 1];
        }
    }
    for (j = 0; j < 32; j++) {
        *distinct += (seen >> j) & 1u;
    }
}

static void t_aura_anim(sa1_context *ctx)
{
    static const float at[3] = { 0.0f, 0.5f, 0.0f };
    static const char *const name[3] = { "Dreamcast", "calm", "slow" };
    int32_t a_ss8 = archive_named(ctx, SA1_CHAR_SUPER_SONIC, "SUPERSONIC");
    int32_t a_ss0 = archive_named(ctx, SA1_CHAR_SONIC, "SUPERSONIC");
    int k, v;
    printf("aura_anim (GRID): SA1_GOPT_SUPER_AURA_ANIM, the standing aura's texture per pair (design 4.8)\n");
    if (scene_grid(ctx, 0) != 0) {
        g_fail++;
        return;
    }
    for (k = 0; k < 2; k++) {
        for (v = 0; v < 3; v++) {
            sa1_character_id id = k ? mk_gen(ctx, at, 50, SA1_SUPER_START | SA1_SUPER_NO_DRAIN, NULL)
                                    : mk(ctx, SA1_CHAR_SUPER_SONIC, at, 0, -1, 1, SA1_SUPER_NO_DRAIN, NULL);
            uint32_t distinct, changes;
            int32_t step = 0;
            char txt[256];
            if (id == 0) {
                g_fail++;
                return;
            }
            CHECK(sa1_character_set_game_option(ctx, id, SA1_GOPT_SUPER_AURA_ANIM, v) == SA1_OK, "%s: set %s",
                  k ? "General" : "kind 8", name[v]);
            run(ctx, id, 6, 0.0f, 0.0f, NULL, NULL);
            aura_seq(ctx, id, k ? a_ss0 : a_ss8, &distinct, &changes, &step, txt, sizeof txt);
            if (v == 0) {
                CHECK(distinct == 4 && changes == 15, "%s, Dreamcast: 4 textures, a new one each pair (%u, %u changes): %s",
                      k ? "General" : "kind 8", (unsigned)distinct, (unsigned)changes, txt);
            } else if (v == 1) {
                CHECK(distinct == 8 && changes == 15, "%s, calm: all 8 textures in order, a new one each pair (%u, %u "
                      "changes): %s", k ? "General" : "kind 8", (unsigned)distinct, (unsigned)changes, txt);
            } else {
                CHECK(distinct == 8 && changes >= 7 && changes <= 8, "%s, slow: 8 textures, each for two pairs (%u, %u "
                      "changes): %s", k ? "General" : "kind 8", (unsigned)distinct, (unsigned)changes, txt);
            }
            sa1_character_destroy(ctx, id);
        }
    }
}

static void t_idle_by_form(sa1_context *ctx)
{
    static const float at[3] = { 0.0f, 0.5f, 0.0f };
    sa1_character_id id;
    sa1g_world *w;
    uint32_t a, b, c, d;
    printf("idle_by_form (GRID): SA1_GOPT_SUPER_IDLE_BY_FORM, 40 forced idles each (design 4.9)\n");
    if (scene_grid(ctx, 0) != 0 || (id = mk_gen(ctx, at, 50, SA1_SUPER_NO_DRAIN, NULL)) == 0) {
        g_fail++;
        return;
    }
    w = sa1_character_world(ctx, id);
    run(ctx, id, 4, 0.0f, 0.0f, NULL, NULL);
    idle_voices(ctx, id, 40, &a, &b, &c, &d);
    CHECK(a + b > 0 && c + d == 0, "General, normal: 227 x%u, 390 x%u, 388 x%u, 396 x%u (Sonic's own)", (unsigned)a,
          (unsigned)b, (unsigned)c, (unsigned)d);
    sa1_character_order(ctx, id, SA1_ORDER_SUPER);
    idle_voices(ctx, id, 40, &a, &b, &c, &d);
    CHECK(a + b > 0 && c + d == 0, "General, Super, no option: 227 x%u, 390 x%u, 388 x%u, 396 x%u (Sonic's: level 1)",
          (unsigned)a, (unsigned)b, (unsigned)c, (unsigned)d);
    sa1_character_set_game_option(ctx, id, SA1_GOPT_SUPER_IDLE_BY_FORM, 1);
    idle_voices(ctx, id, 40, &a, &b, &c, &d);
    CHECK(a + b == 0 && c + d > 0 && (int16_t)rd16w(w, A_CUR_LEVEL) == 1, "General, Super, the option: 227 x%u, 390 x%u, "
          "388 x%u, 396 x%u (Super Sonic's); CurrentLevel %d", (unsigned)a, (unsigned)b, (unsigned)c, (unsigned)d,
          (int)(int16_t)rd16w(w, A_CUR_LEVEL));
    sa1_character_order(ctx, id, SA1_ORDER_NORMAL);
    idle_voices(ctx, id, 40, &a, &b, &c, &d);
    CHECK(a + b > 0 && c + d == 0, "General, back to normal, the option: 227 x%u, 390 x%u, 388 x%u, 396 x%u",
          (unsigned)a, (unsigned)b, (unsigned)c, (unsigned)d);
    sa1_character_destroy(ctx, id);
    /* kind 8: Super with the option, and after the revert (1 ring: the drain) */
    if ((id = mk(ctx, SA1_CHAR_SUPER_SONIC, at, 0, -1, 1, SA1_SUPER_NO_DRAIN, NULL)) == 0) {
        g_fail++;
        return;
    }
    w = sa1_character_world(ctx, id);
    run(ctx, id, 4, 0.0f, 0.0f, NULL, NULL);
    idle_voices(ctx, id, 10, &a, &b, &c, &d);
    printf("  info  kind 8, Super (the game's modes 71-78 have no idle): 227 x%u, 390 x%u, 388 x%u, 396 x%u\n", (unsigned)a,
           (unsigned)b, (unsigned)c, (unsigned)d);
    sa1_character_order(ctx, id, SA1_ORDER_NORMAL);
    run(ctx, id, 4, 0.0f, 0.0f, NULL, NULL);
    idle_voices(ctx, id, 40, &a, &b, &c, &d);
    CHECK(a + b == 0 && c + d > 0, "kind 8 after the revert, no option: 227 x%u, 390 x%u, 388 x%u, 396 x%u (level 19's: the "
          "owner's note 470)", (unsigned)a, (unsigned)b, (unsigned)c, (unsigned)d);
    sa1_character_set_game_option(ctx, id, SA1_GOPT_SUPER_IDLE_BY_FORM, 1);
    idle_voices(ctx, id, 40, &a, &b, &c, &d);
    CHECK(a + b > 0 && c + d == 0 && (int16_t)rd16w(w, A_CUR_LEVEL) == 19, "kind 8 after the revert, the option: 227 "
          "x%u, 390 x%u, 388 x%u, 396 x%u (Sonic's own); CurrentLevel %d (never written)", (unsigned)a, (unsigned)b,
          (unsigned)c, (unsigned)d, (int)(int16_t)rd16w(w, A_CUR_LEVEL));
    sa1_character_destroy(ctx, id);
}

static void t_water_while_super(sa1_context *ctx)
{
    sa1_character_id id;
    sa1_state_ex x;
    float ymin = 1e9f, x0;
    int i, inwater = 0;
    const uint32_t gf = SA1_SUPER_WATER_WHILE_SUPER | SA1_SUPER_NO_DRAIN;
    printf("water_while_super (GRID): SA1_SUPER_WATER_WHILE_SUPER (design 4.10, review RC5)\n");
    /* (a) a 16,000-unit WATER box, top y 0: 600 pairs at RUN's top speed */
    if (scene_big(ctx, 8000.0f, 1, 0.0f) != 0) {
        g_fail++;
        return;
    }
    {
        static const float at[3] = { -7500.0f, 1.0f, 0.0f };
        if ((id = mk_gen(ctx, at, 50, gf | SA1_SUPER_START, NULL)) == 0) {
            g_fail++;
            return;
        }
        sa1_character_set_super_option(ctx, id, SA1_SUPOPT_SPEED, SA1_SUPER_SPEED_RUN);
        tick(ctx, id, 0.0f, 0.0f, 0, NULL, &x);
        x0 = x.sa1_position[0];
        for (i = 0; i < 600; i++) {
            tick(ctx, id, 0.0f, 1.0f, 0, NULL, &x);
            if (x.sa1_position[1] < ymin) {
                ymin = x.sa1_position[1];
            }
            if (g_verbose && i % 50 == 0) {
                printf("    water pair %d: x %.1f y %.3f mode %d action %d speed %.3f %.3f attr %X%c", i,
                       (double)x.sa1_position[0], (double)x.sa1_position[1], (int)x.mode, (int)x.action,
                       (double)x.player_speed[0], (double)x.player_speed[1], (unsigned)x.attr, 10);
            }
        }
        CHECK(ymin > -1.0f && x.sa1_position[0] - x0 > 5000.0f && (x.equipment & 0x8000u),
              "(a) 600 pairs running on the water: lowest y %.3f, %.0f units run, speed %.2f (RUN keeps speeding up on a "
              "long straight: the Super ground drag -0.001)", (double)ymin, (double)(x.sa1_position[0] - x0),
              (double)x.player_speed[0]);
        /* (d) the revert on the water drops him in */
        sa1_character_order(ctx, id, SA1_ORDER_NORMAL);
        run(ctx, id, 90, 0.0f, 0.0f, NULL, &x);
        CHECK(x.sa1_position[1] < -10.0f && !(x.equipment & 0x8000u), "(d) back to Sonic on the water: he sinks (y %.2f "
              "after 90 pairs)", (double)x.sa1_position[1]);
        sa1_character_destroy(ctx, id);
    }
    /* (b) landing on it from 100 units above */
    {
        static const float at[3] = { 0.0f, 100.0f, 0.0f };
        ymin = 1e9f;
        if ((id = mk_gen(ctx, at, 50, gf | SA1_SUPER_START, NULL)) == 0) {
            g_fail++;
            return;
        }
        for (i = 0; i < 120; i++) {
            tick(ctx, id, 0.0f, 0.0f, 0, NULL, &x);
            if (x.sa1_position[1] < ymin) {
                ymin = x.sa1_position[1];
            }
        }
        CHECK(ymin > -1.0f && (x.flags & SA1_STATE_ON_GROUND) && x.sa1_position[1] > -1.0f && x.sa1_position[1] < 1.0f,
              "(b) a fall from 100 units above: he lands on the water (lowest y %.3f, on the ground)", (double)ymin);
        sa1_character_destroy(ctx, id);
    }
    /* (c) a transformation inside deep water (top 600 units above him) keeps him in the water */
    if (scene_big(ctx, 3000.0f, 1, 600.0f) != 0) {
        g_fail++;
        return;
    }
    {
        static const float at[3] = { 0.0f, -99.0f, 0.0f };
        if ((id = mk_gen(ctx, at, 50, gf, NULL)) == 0) {
            g_fail++;
            return;
        }
        run(ctx, id, 30, 0.0f, 0.0f, NULL, &x);
        sa1_character_order(ctx, id, SA1_ORDER_SUPER);
        for (i = 0; i < 60; i++) {
            tick(ctx, id, 0.0f, 0.0f, i == 10 ? SA1_BUTTON_JUMP : 0u, NULL, &x);
            inwater += (x.water_attr & 0x400002u) != 0;
        }
        CHECK((x.equipment & 0x8000u) && inwater == 60 && x.sa1_position[1] < 500.0f, "(c) Super deep in the water (top 600 "
              "above): in water (the game's probe) %u of 60 pairs, y %.2f (not on top)", (unsigned)inwater,
              (double)x.sa1_position[1]);
        sa1_character_destroy(ctx, id);
    }
}

/* the offset of a point from the render root along the run (Source preset: z up, yaw about z, 0 = +X) */
static float along_run(const float pt[3], const float root[3], float yaw)
{
    return (pt[0] - root[0]) * cosf(yaw) + (pt[1] - root[1]) * sinf(yaw);
}

/* res-game's measure (super_general_game.md 6, probe_hold.c): within each pair, the prop's offset along the run from the
 * body's render root at alpha 0 and alpha 1. Drawn-step positions (drawn_prev -> drawn) against the root's own drawn steps
 * keep it constant (no 30 Hz sawtooth); position_prev -> position (the last pair's two steps) does not. */
static void t_held_drawn(sa1_context *unused)
{
    sa1_context *ctx = hold_ctx();
    sa1_character_id id;
    sa1_state_ex x;
    sa1_hostobj_held hd;
    uint32_t obj = 0;
    float worst_d = 0.0f, worst_o = 0.0f, sum_d = 0.0f, sum_o = 0.0f;
    int i, n = 0, n3 = 0, moved = 0;
    (void)unused;
    printf("held_drawn (GRID, Source preset: HU): the held object at the drawn steps (design 4.12)\n");
    if (ctx == NULL || (id = hold_char(ctx, SA1_CHAR_SONIC, 0, 0)) == 0 || (obj = hold_lift(ctx, id)) == 0) {
        CHECK(0, "Sonic did not lift the HOLDABLE");
        if (ctx) {
            sa1_shutdown(ctx);
        }
        return;
    }
    run(ctx, id, 20, 0.0f, 1.0f, NULL, &x);
    for (i = 0; i < 49; i++) {
        const sa1_render *r = NULL;
        float p0[3], p1[3], y0, y1, fd, fo;
        if (i == 30) {
            sa1_input in;
            memset(&in, 0, sizeof in);
            in.struct_size = sizeof in;
            in.stick_y = 1.0f;
            sa1_character_tick(ctx, id, &in, 3.0f / 30.0f, NULL, NULL);    /* a tick of 3 pairs */
        } else {
            tick(ctx, id, 0.0f, 1.0f, 0, NULL, &x);
        }
        memset(&hd, 0, sizeof hd);
        hd.struct_size = sizeof hd;
        if (sa1_character_held(ctx, id, &hd) != SA1_OK || hd.object != obj) {
            continue;
        }
        if (sa1_character_render(ctx, id, 0.0f, &r) != SA1_OK || r == NULL) {
            continue;
        }
        memcpy(p0, r->position, sizeof p0);
        y0 = r->yaw;
        if (sa1_character_render(ctx, id, 1.0f, &r) != SA1_OK || r == NULL) {
            continue;
        }
        memcpy(p1, r->position, sizeof p1);
        y1 = r->yaw;
        fd = fabsf(along_run(hd.drawn, p1, y1) - along_run(hd.drawn_prev, p0, y0));
        fo = fabsf(along_run(hd.position, p1, y1) - along_run(hd.position_prev, p0, y0));
        if (g_verbose) {
            printf("    pair %d: along the run drawn %.4f / %.4f, old %.2f / %.2f, yaw %.4f / %.4f, root step %.2f%c", i,
                   (double)along_run(hd.drawn_prev, p0, y0), (double)along_run(hd.drawn, p1, y1),
                   (double)along_run(hd.position_prev, p0, y0), (double)along_run(hd.position, p1, y1), (double)y0,
                   (double)y1, (double)sqrtf((p1[0] - p0[0]) * (p1[0] - p0[0]) + (p1[1] - p0[1]) * (p1[1] - p0[1])), 10);
        }
        worst_d = fd > worst_d ? fd : worst_d;
        moved += fd >= 0.04f;
        worst_o = fo > worst_o ? fo : worst_o;
        sum_d += fd;
        sum_o += fo;
        n++;
        if (i == 30) {
            n3 = fd < 0.04f;
        }
    }
    /* a pair in which the carry pose itself changes (the walk -> run carry action) moves the prop for real: at most 2 */
    CHECK(n >= 45 && moved <= 2 && n3 && sum_o / (n ? n : 1) > 1.0f, "%d pairs carried at a run: the prop's offset along the "
          "run from the render root, alpha 0 against alpha 1: drawn_prev -> drawn mean %.4f HU, %d pairs over 0.04 HU = 0.01 "
          "units (the carry pose changing: worst %.4f), after the 3-pair tick %s; position_prev -> position mean %.2f worst "
          "%.2f HU (the 30 Hz sawtooth G-SA1 0.9.3 showed)", n, (double)(sum_d / (n ? n : 1)), moved, (double)worst_d,
          n3 ? "under 0.04" : "OVER", (double)(sum_o / (n ? n : 1)), (double)worst_o);
    sa1_hostobj_remove(ctx, obj, SA1_HOSTOBJ_REMOVE_SILENT);
    sa1_character_destroy(ctx, id);
    sa1_shutdown(ctx);
}

static void t_kind8_extra(sa1_context *ctx)
{
    static const float at[3] = { 0.0f, 0.5f, 0.0f };
    sa1_character_id id;
    sa1g_world *w;
    sa1_state_ex x;
    evlog L;
    uint32_t n, eq_super = 0;
    int i;
    printf("kind8_extra (GRID): kind 8 with Sonic's upgrades, SA1_SUPER_START_NORMAL, one voice per transformation\n");
    memset(&L, 0, sizeof L);
    if (scene_grid(ctx, 0) != 0 || (id = mk(ctx, SA1_CHAR_SUPER_SONIC, at, 0, 1, 0, 0, NULL)) == 0) {
        g_fail++;
        return;
    }
    give_upgrades(ctx, id);
    for (i = 0; i < 80; i++) {
        tick(ctx, id, 0.0f, 0.0f, 0, NULL, &x);
        if (i == 2) {
            eq_super = x.equipment;
        }
    }
    CHECK(eq_super == 0x8007u && x.equipment == 0x0007u, "kind 8 with Sonic's upgrade flags: %04X while Super, %04X after "
          "the drain's revert", (unsigned)eq_super, (unsigned)x.equipment);
    sa1_character_destroy(ctx, id);
    if ((id = mk(ctx, SA1_CHAR_SUPER_SONIC, at, 0, 50, 1, SA1_SUPER_START_NORMAL, NULL)) == 0) {
        g_fail++;
        return;
    }
    w = sa1_character_world(ctx, id);
    sa1_character_set_super_option(ctx, id, SA1_SUPOPT_VOICE, 1116);
    run(ctx, id, 10, 0.0f, 0.0f, &L, &x);
    ev_find(&L, SA1_EVENT_SOUND, 1116u, 0, &n);
    CHECK(!(x.equipment & 0x8000u) && find_task(w, X_TRIGGER) == 0 && w->level == 19 && n == 0,
          "SA1_SUPER_START_NORMAL: kind 8 starts as normal Sonic (equipment %04X, no trigger, stage %d)",
          (unsigned)x.equipment, (int)w->level);
    CHECK(sa1_character_order(ctx, id, SA1_ORDER_SUPER) == SA1_OK && sa1_character_order(ctx, id, SA1_ORDER_SUPER) == SA1_OK,
          "SA1_ORDER_SUPER twice before a pair (the key and the gesture in one frame)");
    run(ctx, id, 4, 0.0f, 0.0f, &L, &x);
    ev_find(&L, SA1_EVENT_SOUND, 1116u, 0, &n);
    CHECK((x.equipment & 0x8000u) && x.mode >= 71 && x.mode <= 78 && n == 1 && w->sg.voices == 1,
          "the game's request 46 taken: Super (mode %d), the voice 1116 once (%u)", (int)x.mode, (unsigned)n);
    free(L.e);
    sa1_character_destroy(ctx, id);
    /* the game's own trigger plays no voice */
    memset(&L, 0, sizeof L);
    if ((id = mk(ctx, SA1_CHAR_SUPER_SONIC, at, 0, 50, 0, 0, NULL)) == 0) {
        g_fail++;
        return;
    }
    sa1_character_set_super_option(ctx, id, SA1_SUPOPT_VOICE, 1116);
    run(ctx, id, 6, 0.0f, 0.0f, &L, &x);
    ev_find(&L, SA1_EVENT_SOUND, 1116u, 0, &n);
    CHECK((x.equipment & 0x8000u) && n == 0, "kind 8's own trigger: Super, no voice (%u)", (unsigned)n);
    free(L.e);
    sa1_character_destroy(ctx, id);
}

static void t_general_effects(sa1_context *ctx)
{
    static const float at[3] = { -2000.0f, 0.5f, 0.0f };
    int on;
    printf("general_effects (GRID): SA1_SUPOPT_SUPER_EFFECTS, the game's Super trail and ball after-images (RC14)\n");
    if (scene_grid(ctx, 0) != 0) {
        g_fail++;
        return;
    }
    for (on = 0; on < 2; on++) {
        sa1_character_id id = mk_gen(ctx, at, 50, SA1_SUPER_START | SA1_SUPER_NO_DRAIN, NULL);
        sa1g_world *w;
        sa1_state_ex x;
        uint32_t ss = 0;
        int i;
        if (id == 0) {
            g_fail++;
            return;
        }
        w = sa1_character_world(ctx, id);
        if (on) {
            sa1_character_set_super_option(ctx, id, SA1_SUPOPT_SUPER_EFFECTS, 1);
        }
        run(ctx, id, 6, 0.0f, 0.0f, NULL, &x);
        for (i = 0; i < 20; i++) {
            tick(ctx, id, 0.0f, 0.0f, SA1_BUTTON_ACTION, NULL, &x);
        }
        for (i = 0; i < 30; i++) {
            tick(ctx, id, 0.0f, 1.0f, (i == 12 || i == 20) ? SA1_BUTTON_JUMP : 0u, NULL, &x);
            ss += find_task(w, X_TRAIL_SS) != 0 || find_task(w, X_BALLAI_SS) != 0;
        }
        if (on) {
            CHECK(w->sg.effect_clones > 0 && ss > 0, "on: %u first execs ran the Super clones, %u pairs with a Super trail "
                  "/ after-image task", (unsigned)w->sg.effect_clones, (unsigned)ss);
        } else {
            CHECK(w->sg.effect_clones == 0 && ss == 0, "off (the default): Sonic's own effects (%u clones, %u pairs)",
                  (unsigned)w->sg.effect_clones, (unsigned)ss);
        }
        sa1_character_destroy(ctx, id);
    }
}

/* round 0.9.3a (design 8.2 memcheck): the game heap of a General world through transformations, reverts and the player's
 * own destruction. The weld set-up MAllocs the 12 Super welds' org buffers in the world's heap (sa1_world_super.c); the
 * transformation's tasks are freed by themselves at the revert; the player's destructor (PJoinVertexes jvmode 2 through
 * GW) frees every org buffer once (a double free is a world fault: MFree's loop, sa1g_fatal). The host heap is checked by
 * running super_test under SA1_GUARD_ALLOC=1 (tests/guard_alloc.h). */
typedef struct freetask_req {
    uint32_t tp;
} freetask_req;

static void freetask_cb(sa1g_world *w, void *user)
{
    (void)w;
    ((void (*)(void *))sa1_fn(SA1_GFN_C(0x8C098786u), 0u))(sa1_decode(((freetask_req *)user)->tp));
}

static void t_general_memcheck(sa1_context *ctx)
{
    static const float at[3] = { 0.0f, 0.5f, 0.0f };
    sa1_character_id id;
    sa1g_world *w;
    sa1_state_ex x;
    uint32_t f0 = 0, f1 = 0, f2 = 0, lg = 0, org = 0, k, e;
    freetask_req q;
    int i;
    printf("general_memcheck (GRID): the game heap through 10 transformations and the player's destruction\n");
    if (scene_grid(ctx, 0) != 0 || (id = mk_gen(ctx, at, 50, SA1_SUPER_NO_DRAIN, NULL)) == 0) {
        g_fail++;
        return;
    }
    w = sa1_character_world(ctx, id);
    run(ctx, id, 4, 0.0f, 0.0f, NULL, &x);
    sa1ho_world_heap(w, &f0, &lg);
    for (i = 0; i < 10; i++) {
        sa1_character_order(ctx, id, SA1_ORDER_SUPER);
        run(ctx, id, 6, 0.0f, 1.0f, NULL, &x);
        sa1_character_order(ctx, id, SA1_ORDER_NORMAL);
        run(ctx, id, 6, 0.0f, 0.0f, NULL, &x);
    }
    sa1ho_world_heap(w, &f1, &lg);
    for (k = 0, e = w->sg.gw; e != 0 && k < 80u && x32(w, e) != 0; k++, e += 0x18u) {
        org += x32(w, e + 0x10u) != 0;
    }
    CHECK(w->sg.transforms == 10 && w->sg.reverts == 10 && f1 == f0 && !w->dead,
          "10 transformations and reverts: %u / %u, the game heap's free bytes %u -> %u (no leak), the world alive",
          (unsigned)w->sg.transforms, (unsigned)w->sg.reverts, (unsigned)f0, (unsigned)f1);
    q.tp = rd32(w, 0x8C78C648u);    /* playertp[0] */
    sa1g_world_call(w, freetask_cb, &q);
    run(ctx, id, 3, 0.0f, 0.0f, NULL, &x);
    sa1ho_world_heap(w, &f2, &lg);
    CHECK(!w->dead && f2 > f1, "the player freed (FreeTask): its destructor freed %u org buffers' worth through GW and the "
          "rest of the player (free bytes %u -> %u), no double free (the world is alive: %s)", (unsigned)org,
          (unsigned)f1, (unsigned)f2, w->dead ? w->fault.msg : "yes");
    sa1_character_destroy(ctx, id);
}

/* ==== round 0.9.4a: the General form's own Super Sonic poses (SA1_SUPOPT_SUPER_ANIMS), the golden dust (SA1_SUPER_EFFECTS_
 *      DUST), the contracts (sadc docs/research/super2_plan.md 6.1 with its Review R.1 / R.8 / R.9 and the owner's answers
 *      10.1; faces_wheel_plan.md 6.1 case (19)) ================================================================== */
#define P_MTNMODE       0x120u          /* playerwk.mj.mtnmode (Sint16) */
#define P_REQACTION     0x124u          /* playerwk.mj.reqaction */
#define P_NFRAME        0x130u          /* playerwk.mj.nframe */
#define P_PLACTPTR      0x140u          /* playerwk.mj.plactptr */
#define P_ACTWKPTR      0x148u          /* playerwk.mj.actwkptr (the link action) */
#define P_RHAND         0x14Cu          /* playerwk.righthand_pos (player space) */
#define A_PHYS_CH_SUPER 0x8C0B337Cu     /* the physics task 8C0B32B6's center_height pool word (6.4) */
#define X_DUST          0x8C0B3FA6u     /* the spin dash dust's first exec (8C0B2D40 creates it) */
static const uint32_t k_ss_mot[4] = { 0x0C914D00u, 0x0C90EEA4u, 0x0C90FFC8u, 0x0C91116Cu };   /* 130 131 132 133 (= 134) */

/* the body's lowest / highest point above the drawn root and its horizontal span (the research probe's feet / feet_a:
 * triangle draws of `source`, not the shadow, not additive, textured) */
static uint32_t ext_of(sa1_context *ctx, sa1_character_id id, float alpha, uint32_t source, float *lo_out, float *hi_out,
                       float *span_out)
{
    const sa1_render *rd = NULL;
    uint32_t i, k, t, n = 0;
    float lo = 1e9f, hi = -1e9f, x0 = 1e9f, x1 = -1e9f;
    *lo_out = *hi_out = 0.0f;
    if (span_out != NULL) {
        *span_out = 0.0f;
    }
    if (sa1_character_render(ctx, id, alpha, &rd) != SA1_OK || rd == NULL) {
        return 0;
    }
    for (i = 0; i < rd->num_draws; i++) {
        const sa1_draw *d = &rd->draws[i];
        if (d->prim != SA1_PRIM_TRIANGLES || d->source != source || (d->flags & (SA1_DRAW_SHADOW | SA1_DRAW_ADDITIVE)) ||
            d->archive < 0) {
            continue;
        }
        n++;
        for (t = d->first; t < d->first + d->count; t++) {
            for (k = 0; k < 3; k++) {
                float y = rd->positions[9 * t + 3 * k + 1], xz = rd->positions[9 * t + 3 * k] - rd->position[0];
                lo = y < lo ? y : lo;
                hi = y > hi ? y : hi;
                x0 = xz < x0 ? xz : x0;
                x1 = xz > x1 ? xz : x1;
            }
        }
    }
    if (n) {
        *lo_out = lo - rd->position[1];
        *hi_out = hi - rd->position[1];
        if (span_out != NULL) {
            *span_out = x1 - x0;
        }
    }
    return n;
}

/* the motion Sonic's display draws: the link action's while mtnmode is 2 (*link 1), else plactptr[reqaction] */
static uint32_t drawn_mot(const sa1g_world *w, int *link)
{
    uint32_t pwp = pwp0(w), act;
    *link = 0;
    if (pwp == 0) {
        return 0;
    }
    if ((int16_t)rd16w(w, pwp + P_MTNMODE) == 2 && arena(rd32(w, pwp + P_ACTWKPTR))) {
        *link = 1;
        return x32(w, rd32(w, pwp + P_ACTWKPTR) + 4u);
    }
    act = x32(w, rd32(w, pwp + P_PLACTPTR) + 16u * rd16w(w, pwp + P_REQACTION));
    return x32(w, act + 4u);
}

/* a min / max of the body's extents and speed */
typedef struct mrange {
    int n;
    float lo0, lo1, hi0, hi1, s0, s1;
} mrange;

static void mr_init(mrange *m)
{
    m->n = 0;
    m->lo0 = m->hi0 = m->s0 = 1e9f;
    m->lo1 = m->hi1 = m->s1 = -1e9f;
}

static void mr_add(mrange *m, float lo, float hi, float spd)
{
    m->n++;
    m->lo0 = lo < m->lo0 ? lo : m->lo0;
    m->lo1 = lo > m->lo1 ? lo : m->lo1;
    m->hi0 = hi < m->hi0 ? hi : m->hi0;
    m->hi1 = hi > m->hi1 ? hi : m->hi1;
    m->s0 = spd < m->s0 ? spd : m->s0;
    m->s1 = spd > m->s1 ? spd : m->s1;
}

static int mr_near(const mrange *a, const mrange *b, float tol)
{
    return a->n > 0 && b->n > 0 && fabsf(a->lo0 - b->lo0) <= tol && fabsf(a->lo1 - b->lo1) <= tol &&
           fabsf(a->hi0 - b->hi0) <= tol && fabsf(a->hi1 - b->hi1) <= tol;
}

/* a General Sonic with the Super poses option, Super from the first pair (no drain) */
static sa1_character_id mk_anims(sa1_context *ctx, const float at[3], int32_t anims, uint32_t extra)
{
    sa1_character_id id = mk_gen(ctx, at, 50, SA1_SUPER_START | SA1_SUPER_NO_DRAIN | extra, NULL);
    if (id != 0 && sa1_character_set_super_option(ctx, id, SA1_SUPOPT_SUPER_ANIMS, anims) != SA1_OK) {
        printf("  SA1_SUPOPT_SUPER_ANIMS %d: %s\n", (int)anims, sa1_last_error(ctx));
    }
    return id;
}


/* (1) GA's rows and the lifted copies */
static void t_general_anims_table(sa1_context *ctx)
{
    static const float at[3] = { -2000.0f, 0.5f, 0.0f };
    sa1_character_id id;
    sa1g_world *w;
    sa1_state_ex x;
    uint32_t k, j, nmap = 0, nequal = 0, nbad = 0, bad_row = 0xFFFFFFFFu, nkeep = 0;
    float ch_s, ch_n, lift;
    printf("general_anims_table (GRID): GA, the action table of SA1_SUPOPT_SUPER_ANIMS (super2_plan.md 6.1 task 2, O1)\n");
    if (scene_grid(ctx, 0) != 0 || (id = mk_anims(ctx, at, 1, 0)) == 0) {
        g_fail++;
        return;
    }
    w = sa1_character_world(ctx, id);
    run(ctx, id, 3, 0.0f, 0.0f, NULL, &x);
    ch_s = rdf(w, A_PHYS_CH_SUPER);
    ch_n = rdf(w, A_PHYSICS + 0x80u);
    lift = w->sg.lift;
    CHECK(w->sg.ga != 0 && w->sg.gt != 0 && !w->sg.ga_failed && fabsf(lift - 1.0f) < 0.001f && lift == ch_s - ch_n,
          "GA built after the first pair (%08X, GT %08X); L = %.6f = the physics task's center_height %.6f - Sonic's %.6f "
          "(read from the image)", (unsigned)w->sg.ga, (unsigned)w->sg.gt, (double)lift, (double)ch_s, (double)ch_n);
    for (k = 0; k < 142u; k++) {
        uint32_t s = sa1w_super_anims_row(k), ga = w->sg.ga + 16u * k, gt = w->sg.gt + 16u * k, src, act, ok;
        if (s == 0) {
            uint32_t same = 1;
            for (j = 0; j < 16u; j += 4u) {
                same &= x32(w, ga + j) == x32(w, gt + j);
            }
            nequal += same;
            if (!same && bad_row == 0xFFFFFFFFu) {
                bad_row = k;
            }
            nkeep += (k >= 4u && k <= 6u) && same;
            continue;
        }
        nmap++;
        src = A_SONIC_ACTIONS + 16u * s;
        act = x32(w, ga);
        ok = act == w->sg.ga_act[s == 134u ? 3u : s - 130u] &&
             (x32(w, act) & 0x1FFFFFFFu) == (O_SUPER_OBJECT & 0x1FFFFFFFu) &&
             x32(w, act + 4u) == w->sg.ga_mot[s == 134u ? 3u : s - 130u] && x8(w, ga + 4u) == 73u &&
             x32(w, ga + 8u) == rd32(w, src + 8u) && x32(w, ga + 12u) == rd32(w, src + 12u);
        if (k == 1u) {
            /* the chain row 1 -> 2 keeps Sonic's mtnmode (4) and next (2) */
            ok = ok && x8(w, ga + 5u) == rd8w(w, A_SONIC_ACTIONS + 16u + 5u) && x8(w, ga + 6u) == rd8w(w, A_SONIC_ACTIONS + 16u + 6u);
        } else {
            ok = ok && x8(w, ga + 5u) == rd8w(w, src + 5u) && x8(w, ga + 6u) == k;
        }
        if (!ok) {
            nbad++;
            if (bad_row == 0xFFFFFFFFu) {
                bad_row = k;
            }
        }
    }
    CHECK(nmap == 11u && nequal == 131u && nbad == 0 && nkeep == 3u,
          "GA's rows: %u remapped (0, 1, 2, 7, 8, 74 -> 130; 9 -> 131; 10 -> 132; 11 -> 133; 12, 13 -> 134: actptr = {the Super "
          "body, the lifted copy}, objnum 73, link speed / racio the Super action's, loops: its mtnmode, next = the row; the "
          "chain row 1 Sonic's), %u byte-equal to GT (the idle 4 / 5 / 6 among them: %u, owner answer O1), %u wrong (first %d)",
          (unsigned)nmap, (unsigned)nequal, (unsigned)nkeep, (unsigned)nbad, (int)bad_row);
    for (k = 0; k < 4u; k++) {
        uint32_t c = w->sg.ga_mot[k], o = k_ss_mot[k], md = x32(w, c), omd = rd32(w, o), nf = rd32(w, o + 4u);
        uint32_t nb0 = rd32(w, omd + 8u), p0 = rd32(w, omd), cp0 = x32(w, md), cnb0 = x32(w, md + 8u), i, keys_ok = 1;
        uint32_t ents_ok = 1;
        float rest[3];
        for (i = 0; i < 3u; i++) {
            rest[i] = rdf(w, O_SUPER_OBJECT + 8u + 4u * i);
        }
        for (i = 16u; i < 73u * 16u; i += 4u) {
            ents_ok &= x32(w, md + i) == rd32(w, omd + i);
        }
        ents_ok &= x32(w, md + 4u) == rd32(w, omd + 4u) && x32(w, md + 12u) == rd32(w, omd + 12u);   /* node 0's angles */
        if (nb0 != 0) {
            keys_ok = cnb0 == nb0;
            for (i = 0; keys_ok && i < nb0; i++) {
                uint32_t a = p0 + 16u * i, b = cp0 + 16u * i;
                float y0 = rdf(w, a + 8u), y1;
                uint32_t v = x32(w, b + 8u);
                memcpy(&y1, &v, 4);
                /* y + L is computed inside the world (the game's FP mode, round toward zero): within an ulp of the host's */
                keys_ok = x32(w, b) == rd32(w, a) && x32(w, b + 4u) == rd32(w, a + 4u) && x32(w, b + 12u) == rd32(w, a + 12u) &&
                          fabsf(y1 - (y0 + lift)) <= 1e-5f;
            }
        } else {
            keys_ok = cnb0 == nf;
            for (i = 0; keys_ok && i < nf; i++) {
                uint32_t b = cp0 + 16u * i, v[3];
                float f[3];
                v[0] = x32(w, b + 4u);
                v[1] = x32(w, b + 8u);
                v[2] = x32(w, b + 12u);
                memcpy(f, v, 12);
                keys_ok = x32(w, b) == i && f[0] == rest[0] && f[1] == rest[1] + lift && f[2] == rest[2];
            }
        }
        CHECK(x32(w, c + 4u) == nf && (x32(w, c + 8u) & 0xFFFFu) == rd16w(w, o + 8u) && ents_ok && keys_ok,
              "the lifted copy of %08X (%u frames): nodes 1-72 and node 0's angles the game's, node 0's position keys %s "
              "(%u keys)", (unsigned)o, (unsigned)nf, nb0 ? "the game's + (0, L, 0)" : "one per frame at the rest position "
              "+ (0, L, 0) (the game's motion has none)", (unsigned)(nb0 ? nb0 : nf));
    }
    sa1_character_destroy(ctx, id);
}

/* (2) standing: the floating stand of kind 8 */
static void t_general_anims_stand(sa1_context *ctx)
{
    static const float at[3] = { -2000.0f, 0.5f, 0.0f };
    sa1_character_id id;
    sa1g_world *w;
    sa1_state_ex x;
    mrange k8, g1, g0;
    uint32_t n130 = 0, nlink = 0, nsonic = 0;
    int i, link;
    float lo, hi;
    printf("general_anims_stand (GRID): standing 140 pairs - the game's floating stand 130 (super2_anim.md 5.1)\n");
    mr_init(&k8);
    mr_init(&g1);
    mr_init(&g0);
    if (scene_grid(ctx, 0) != 0 || (id = mk(ctx, SA1_CHAR_SUPER_SONIC, at, 0, -1, 1, SA1_SUPER_NO_DRAIN, NULL)) == 0) {
        g_fail++;
        return;
    }
    run(ctx, id, 30, 0.0f, 0.0f, NULL, &x);
    for (i = 0; i < 140; i++) {
        tick(ctx, id, 0.0f, 0.0f, 0, NULL, &x);
        if (x.action == 130 && ext_of(ctx, id, 1.0f, SA1_SOURCE_CHARACTER, &lo, &hi, NULL)) {
            mr_add(&k8, lo, hi, x.player_speed[0]);
        }
    }
    sa1_character_destroy(ctx, id);
    if ((id = mk_anims(ctx, at, 1, 0)) == 0) {
        g_fail++;
        return;
    }
    w = sa1_character_world(ctx, id);
    run(ctx, id, 30, 0.0f, 0.0f, NULL, &x);
    for (i = 0; i < 140; i++) {
        uint32_t m;
        tick(ctx, id, 0.0f, 0.0f, 0, NULL, &x);
        m = drawn_mot(w, &link);
        nlink += link;
        if (!link && m == w->sg.ga_mot[0]) {
            n130++;
            if (ext_of(ctx, id, 1.0f, SA1_SOURCE_CHARACTER, &lo, &hi, NULL)) {
                mr_add(&g1, lo, hi, x.player_speed[0]);
            }
        }
    }
    sa1_character_destroy(ctx, id);
    if ((id = mk_anims(ctx, at, 0, 0)) == 0) {
        g_fail++;
        return;
    }
    w = sa1_character_world(ctx, id);
    run(ctx, id, 30, 0.0f, 0.0f, NULL, &x);
    for (i = 0; i < 140; i++) {
        uint32_t m;
        tick(ctx, id, 0.0f, 0.0f, 0, NULL, &x);
        m = drawn_mot(w, &link);
        if (!link && m == x32(w, x32(w, A_SONIC_ACTIONS + 16u * (uint32_t)x.req_action) + 4u)) {
            nsonic++;
            if (ext_of(ctx, id, 1.0f, SA1_SOURCE_CHARACTER, &lo, &hi, NULL)) {
                mr_add(&g0, lo, hi, x.player_speed[0]);
            }
        }
    }
    sa1_character_destroy(ctx, id);
    printf("  info  kind 8 (action 130, %d pairs): feet %.3f..%.3f top %.3f..%.3f; General option 1 (%d pairs): feet %.3f..%.3f "
           "top %.3f..%.3f; option 0 (%d pairs): feet %.3f..%.3f top %.3f..%.3f\n", k8.n, (double)k8.lo0, (double)k8.lo1,
           (double)k8.hi0, (double)k8.hi1, g1.n, (double)g1.lo0, (double)g1.lo1, (double)g1.hi0, (double)g1.hi1, g0.n,
           (double)g0.lo0, (double)g0.lo1, (double)g0.hi0, (double)g0.hi1);
    CHECK(n130 + nlink == 140u && n130 >= 130u && mr_near(&g1, &k8, 0.02f) && g1.lo0 > -1.17f - 0.02f &&
              g1.lo1 < -0.40f + 0.02f && g1.hi0 > 11.04f - 0.02f && g1.hi1 < 11.64f + 0.02f,
          "option 1: the drawn motion is the lifted Stationary copy in %u of 140 pairs (%u in a link), its extents equal "
          "kind 8's 130 within 0.02 and inside -1.17..-0.40 / 11.04..11.64", (unsigned)n130, (unsigned)nlink);
    CHECK(nsonic >= 130u && g0.lo0 > -0.3f && g0.lo1 < 0.3f && g0.hi1 < 10.7f,
          "option 0: Sonic's stand motions on the Super body (%u pairs: the stand and the alternate stand 1 -> 2), on the "
          "ground: feet %.3f..%.3f, top %.3f..%.3f (0.9.3a's look; Sonic's stand 0 alone: 0.01..0.02)", (unsigned)nsonic,
          (double)g0.lo0, (double)g0.lo1, (double)g0.hi0, (double)g0.hi1);
}

/* (3) moving: 131-134 by the game's speed bands */
static void t_general_anims_bands(sa1_context *ctx)
{
    static const float at[3] = { -2000.0f, 0.5f, 0.0f };
    static const float sticks[3] = { 0.15f, 0.35f, 1.0f };
    static const int pairs[3] = { 100, 100, 300 };
    sa1_character_id id;
    sa1g_world *w;
    sa1_state_ex x;
    mrange gb[4], kb[4];
    uint32_t wrong = 0, n13 = 0, n13ok = 0, sampled = 0;
    int i, p, b, link, all = 1, mono = 1;
    float lo, hi;
    printf("general_anims_bands (GRID): stick 0.15 / 0.35 / 1.0 from a stand - 131-134 by the game's speed bands\n");
    for (b = 0; b < 4; b++) {
        mr_init(&gb[b]);
        mr_init(&kb[b]);
    }
    if (scene_big(ctx, 20000.0f, 0, 0.0f) != 0) {
        g_fail++;
        return;
    }
    if ((id = mk(ctx, SA1_CHAR_SUPER_SONIC, at, 0, -1, 1, SA1_SUPER_NO_DRAIN, NULL)) == 0) {
        g_fail++;
        return;
    }
    w = sa1_character_world(ctx, id);
    run(ctx, id, 20, 0.0f, 0.0f, NULL, &x);
    for (p = 0; p < 3; p++) {
        for (i = 0; i < pairs[p]; i++) {
            tick(ctx, id, 0.0f, sticks[p], 0, NULL, &x);
            if (x.action >= 131 && x.action <= 134 && (int16_t)rd16w(w, pwp0(w) + P_MTNMODE) != 2 &&
                ext_of(ctx, id, 1.0f, SA1_SOURCE_CHARACTER, &lo, &hi, NULL)) {
                mr_add(&kb[x.action - 131], lo, hi, x.player_speed[0]);
            }
        }
    }
    sa1_character_destroy(ctx, id);
    if ((id = mk_anims(ctx, at, 1, 0)) == 0) {
        g_fail++;
        return;
    }
    w = sa1_character_world(ctx, id);
    run(ctx, id, 20, 0.0f, 0.0f, NULL, &x);
    for (p = 0; p < 3; p++) {
        for (i = 0; i < pairs[p]; i++) {
            uint32_t m, s;
            tick(ctx, id, 0.0f, sticks[p], 0, NULL, &x);
            m = drawn_mot(w, &link);
            s = sa1w_super_anims_row((uint32_t)x.req_action);
            if (link || s < 131u || rd32(w, pwp0(w) + P_PLACTPTR) != w->sg.ga) {
                continue;
            }
            sampled++;
            if (m != w->sg.ga_mot[s == 134u ? 3u : s - 130u]) {
                wrong++;
            }
            if (x.req_action == 13) {
                n13++;
                n13ok += m == w->sg.ga_mot[3];
            }
            if (ext_of(ctx, id, 1.0f, SA1_SOURCE_CHARACTER, &lo, &hi, NULL)) {
                mr_add(&gb[s - 131u], lo, hi, x.player_speed[0]);
            }
        }
    }
    for (b = 0; b < 4; b++) {
        int both = gb[b].n >= 16 && kb[b].n >= 16;
        printf("  info  %d: General %3d pairs spd.x %.2f..%.2f feet %.3f..%.3f top %.3f..%.3f | kind 8 %3d pairs spd.x %.2f..%.2f "
               "feet %.3f..%.3f top %.3f..%.3f%s\n", 131 + b, gb[b].n, (double)gb[b].s0, (double)gb[b].s1, (double)gb[b].lo0,
               (double)gb[b].lo1, (double)gb[b].hi0, (double)gb[b].hi1, kb[b].n, (double)kb[b].s0, (double)kb[b].s1,
               (double)kb[b].lo0, (double)kb[b].lo1, (double)kb[b].hi0, (double)kb[b].hi1,
               both ? "" : " (fewer than 16 pairs: extents not compared)");
        all = all && gb[b].n > 0 && (!both || mr_near(&gb[b], &kb[b], 0.02f));
        if (b > 0 && gb[b].n > 0 && gb[b - 1].n > 0 && gb[b].s0 < gb[b - 1].s1 - 0.001f) {
            mono = 0;
        }
    }
    CHECK(sampled > 200u && wrong == 0 && all && mono,
          "option 1: in %u steady pairs the drawn motion is the lifted copy of the band's Super motion (%u wrong); every band "
          "drawn, its speeds above the band below (the game's thresholds), its extents kind 8's within 0.02",
          (unsigned)sampled, (unsigned)wrong);
    CHECK(n13 > 0 && n13ok == n13, "the sprint (action 13, above the dash speed): %u pairs, %u drawn with 134's copy",
          (unsigned)n13, (unsigned)n13ok);
    sa1_character_destroy(ctx, id);
}

/* (4) / (5): the research's 1,436-pair script (super2_anim.md 5) in two General worlds, the option 0 and 1, in lockstep */
#define ID_PAIRS 1436
typedef struct id_rec {
    float pos[3], spd[3];
    int32_t ang[3], mode, rings, action, mtnmode;
    uint32_t equip, rnd;
    float nframe, lo, hi;
    uint32_t nev;
} id_rec;
typedef struct id_ev {
    uint32_t pair, type, id;
} id_ev;
typedef struct id_runs {
    int done, n, fail;
    id_rec r[2][ID_PAIRS];
    id_ev *ev[2];
    uint32_t nev[2], cap[2];
} id_runs;
static id_runs *g_id;

static void id_script(int i, float *sy, uint32_t *b, int *idle, int *sonic_speed)
{
    *sy = 0.0f;
    *b = 0;
    *idle = 0;
    *sonic_speed = 0;
    if (i < 140) {                          /* stand */
    } else if (i < 210) {
        *sy = 0.15f;                        /* walk 0.15 */
    } else if (i < 280) {
        *sy = 0.35f;                        /* walk 0.35 */
    } else if (i < 580) {
        *sy = 1.0f;                         /* run 1.0 */
    } else if (i < 620) {
        *sy = -1.0f;                        /* brake (stick back) */
    } else if (i < 690) {                   /* stand */
    } else if (i < 694) {
        *b = SA1_BUTTON_JUMP;               /* jump */
    } else if (i == 694) {
        *b = SA1_BUTTON_ACTION;             /* the air action */
    } else if (i < 775) {                   /* fall, land */
    } else if (i < 975) {
        *idle = i == 775;                   /* the idle: waittimer forced to 1801 */
    } else if (i < 1075) {
        *b = SA1_BUTTON_ACTION;             /* the Light Speed charge */
    } else if (i < 1136) {                  /* the release */
    } else if (i < 1176) {
        *b = SA1_BUTTON_ACTION;             /* the spin dash (speed set SONIC: the roll ends with the uncurl) */
        *sonic_speed = i == 1136;
    }
}

static void id_events(id_runs *R, int k, const evlog *L, uint32_t from, int pair)
{
    uint32_t i;
    for (i = from; i < L->n; i++) {
        if (L->e[i].type != SA1_EVENT_SOUND && L->e[i].type != SA1_EVENT_VOICE) {
            continue;
        }
        if (R->nev[k] == R->cap[k]) {
            R->cap[k] = R->cap[k] ? 2u * R->cap[k] : 256u;
            R->ev[k] = (id_ev *)realloc(R->ev[k], (size_t)R->cap[k] * sizeof *R->ev[k]);
        }
        R->ev[k][R->nev[k]].pair = (uint32_t)pair;
        R->ev[k][R->nev[k]].type = L->e[i].type;
        R->ev[k][R->nev[k]].id = L->e[i].id;
        R->nev[k]++;
    }
}

static id_runs *id_run(sa1_context *ctx)
{
    static const float at[3] = { -2000.0f, 0.5f, 0.0f };
    sa1_character_id id[2];
    sa1g_world *w[2];
    evlog L[2];
    int i, k;
    if (g_id != NULL) {
        return g_id;
    }
    g_id = (id_runs *)calloc(1, sizeof *g_id);
    if (scene_big(ctx, 20000.0f, 0, 0.0f) != 0) {
        g_id->fail = 1;
        return g_id;
    }
    memset(L, 0, sizeof L);
    for (k = 0; k < 2; k++) {
        if ((id[k] = mk_anims(ctx, at, k, 0)) == 0) {
            g_id->fail = 1;
            return g_id;
        }
        give_upgrades(ctx, id[k]);
        w[k] = sa1_character_world(ctx, id[k]);
    }
    for (i = 0; i < ID_PAIRS; i++) {
        float sy;
        uint32_t b;
        int idle, sonic_speed;
        id_script(i, &sy, &b, &idle, &sonic_speed);
        for (k = 0; k < 2; k++) {
            sa1_state_ex x;
            id_rec *r = &g_id->r[k][i];
            uint32_t from = L[k].n, pwp = pwp0(w[k]);
            if (idle && pwp) {
                wr16w(w[k], pwp + 0x0Eu, 1801);
            }
            if (sonic_speed) {
                sa1_character_set_super_option(ctx, id[k], SA1_SUPOPT_SPEED, SA1_SUPER_SPEED_SONIC);
            }
            if (tick(ctx, id[k], 0.0f, sy, b, &L[k], &x) != 0) {
                g_id->fail = 1;
                break;
            }
            memcpy(r->pos, x.sa1_position, 12);
            memcpy(r->spd, x.player_speed, 12);
            memcpy(r->ang, x.angle, 12);
            r->mode = x.mode;
            r->rings = x.rings;
            r->action = x.action;
            r->equip = x.equipment;
            r->rnd = x.rand_state;
            pwp = pwp0(w[k]);
            r->mtnmode = pwp ? (int16_t)rd16w(w[k], pwp + P_MTNMODE) : -1;
            r->nframe = x.frame;
            ext_of(ctx, id[k], 1.0f, SA1_SOURCE_CHARACTER, &r->lo, &r->hi, NULL);
            id_events(g_id, k, &L[k], from, i);
        }
        if (g_id->fail) {
            break;
        }
    }
    g_id->n = i;
    g_id->done = 1;
    for (k = 0; k < 2; k++) {
        free(L[k].e);
        sa1_character_destroy(ctx, id[k]);
    }
    return g_id;
}

static void t_general_anims_identity(sa1_context *ctx)
{
    id_runs *R;
    int i, nstate = 0, first = -1, nrand = 0, firstr = -1, nact = 0;
    uint32_t a = 0, b = 0, shown = 0, ndiff = 0;
    printf("general_anims_identity (GRID): the research's 1,436-pair script with the option 0 and 1 (super2_anim.md 5.3)\n");
    R = id_run(ctx);
    if (R->fail || R->n != ID_PAIRS) {
        CHECK(0, "the script ran %d of %d pairs", R->n, ID_PAIRS);
        return;
    }
    for (i = 0; i < R->n; i++) {
        const id_rec *p = &R->r[0][i], *q = &R->r[1][i];
        int d = memcmp(p->pos, q->pos, 12) || memcmp(p->spd, q->spd, 12) || memcmp(p->ang, q->ang, 12) || p->mode != q->mode ||
                p->rings != q->rings || p->equip != q->equip;
        if (d) {
            nstate++;
            first = first < 0 ? i : first;
        }
        if (p->rnd != q->rnd) {
            nrand++;
            firstr = firstr < 0 ? i : firstr;
        }
        nact += p->action != q->action;
    }
    CHECK(nstate == 0, "position, player speed, angles, mode, rings and equipment identical in every pair: %d of %d differ "
          "(first %d); the motion state (mj.action) differs in %d pairs (the 32-frame Stationary's chains)", nstate, R->n,
          first, nact);
    printf("  info  the rand state differs in %d pairs, from pair %d on (the bubble task 8C0B3B1A bursts on motion wraps: "
           "super2_anim.md 5.3; reported, not compared)\n", nrand, firstr);
    /* the SOUND / VOICE streams: every difference listed (pair, type, id) */
    while (a < R->nev[0] || b < R->nev[1]) {
        const id_ev *e0 = a < R->nev[0] ? &R->ev[0][a] : NULL, *e1 = b < R->nev[1] ? &R->ev[1][b] : NULL;
        if (e0 && e1 && e0->pair == e1->pair && e0->type == e1->type && e0->id == e1->id) {
            a++;
            b++;
            continue;
        }
        ndiff++;
        if (e0 && (!e1 || e0->pair <= e1->pair)) {
            if (shown++ < 24u) {
                printf("    only with the option 0: pair %u %s %u (action %d / %d)\n", (unsigned)e0->pair,
                       e0->type == SA1_EVENT_VOICE ? "VOICE" : "SOUND", (unsigned)e0->id, R->r[0][e0->pair].action,
                       R->r[1][e0->pair].action);
            }
            a++;
        } else {
            if (shown++ < 24u) {
                printf("    only with the option 1: pair %u %s %u (action %d / %d)\n", (unsigned)e1->pair,
                       e1->type == SA1_EVENT_VOICE ? "VOICE" : "SOUND", (unsigned)e1->id, R->r[0][e1->pair].action,
                       R->r[1][e1->pair].action);
            }
            b++;
        }
    }
    printf("  info  SOUND / VOICE events: %u with the option 0, %u with 1, %u unmatched (listed above, at most 24)\n",
           (unsigned)R->nev[0], (unsigned)R->nev[1], (unsigned)ndiff);
}

/* (5) Sonic's kept moves look the same with the option 0 and 1 */
static void t_general_anims_kept(sa1_context *ctx)
{
    static const struct { const char *name; int a0, a1; } fam[5] = {
        { "the ball (14, 15, 32)", 14, 15 }, { "the brake (20)", 20, 20 }, { "the fall (18)", 18, 18 },
        { "the uncurl (29)", 29, 29 }, { "the Light Speed Dash (64)", 64, 64 },
    };
    id_runs *R;
    int f, i, ok = 1, total = 0;
    printf("general_anims_kept (GRID): the kept Sonic moves have the same extents with the option 0 and 1\n");
    R = id_run(ctx);
    if (R->fail || R->n != ID_PAIRS) {
        CHECK(0, "the script ran %d of %d pairs", R->n, ID_PAIRS);
        return;
    }
    for (f = 0; f < 5; f++) {
        mrange m0, m1;
        float worst = 0.0f;
        int n = 0;
        mr_init(&m0);
        mr_init(&m1);
        for (i = 0; i < R->n; i++) {
            const id_rec *p = &R->r[0][i], *q = &R->r[1][i];
            int in = p->action == fam[f].a0 || p->action == fam[f].a1 || (f == 0 && p->action == 32);
            /* not in a link, and not the pair right after one: the capture draws the pair's sub step 0, where the link
               from the move before (Sonic's run or 134's copy) may still be blending */
            if (!in || p->action != q->action || p->mtnmode == 2 || q->mtnmode == 2 || p->nframe != q->nframe ||
                (i > 0 && (R->r[0][i - 1].mtnmode == 2 || R->r[1][i - 1].mtnmode == 2))) {
                continue;
            }
            n++;
            mr_add(&m0, p->lo, p->hi, 0.0f);
            mr_add(&m1, q->lo, q->hi, 0.0f);
            worst = fabsf(p->lo - q->lo) > worst ? fabsf(p->lo - q->lo) : worst;
            worst = fabsf(p->hi - q->hi) > worst ? fabsf(p->hi - q->hi) : worst;
            if (g_verbose && (fabsf(p->lo - q->lo) > 0.01f || fabsf(p->hi - q->hi) > 0.01f)) {
                printf("    pair %d: action %d frame %.2f mtnmode %d / %d, feet %.3f / %.3f, top %.3f / %.3f (pair before: "
                       "action %d / %d, mtnmode %d / %d)\n", i, p->action, (double)p->nframe, p->mtnmode, q->mtnmode,
                       (double)p->lo, (double)q->lo, (double)p->hi, (double)q->hi, i ? R->r[0][i - 1].action : -1,
                       i ? R->r[1][i - 1].action : -1, i ? R->r[0][i - 1].mtnmode : -1, i ? R->r[1][i - 1].mtnmode : -1);
            }
        }
        total += n;
        printf("  info  %-28s %4d pairs at the same frame: feet %.3f..%.3f / %.3f..%.3f, top %.3f..%.3f / %.3f..%.3f, "
               "largest difference %.4f\n", fam[f].name, n, (double)m0.lo0, (double)m0.lo1, (double)m1.lo0, (double)m1.lo1,
               (double)m0.hi0, (double)m0.hi1, (double)m1.hi0, (double)m1.hi1, (double)worst);
        ok = ok && worst <= 0.01f;
    }
    CHECK(ok && total > 100, "the ball, the brake, the fall, the uncurl and the Light Speed Dash: the same extents with the "
          "option 0 and 1 (%d pairs compared, every difference within 0.01)", total);
}

/* (6) the idle after standing still (owner answer O1): Sonic's foot tap on the Super body, then the float again */
static void t_general_anims_idle(sa1_context *ctx)
{
    static const float at[3] = { -2000.0f, 0.5f, 0.0f };
    sa1_character_id id;
    sa1g_world *w;
    sa1_state_ex x;
    uint32_t seen = 0, gt_ok = 0, idle_pairs = 0, back130 = 0, v227, v390, v388, v396;
    float lo, hi, lo_min = 1e9f;
    int i, link, after = 0;
    printf("general_anims_idle (GRID): waittimer 1801 - the idle 4 -> 5 -> 6 is Sonic's on the Super body, then 130 (O1)\n");
    if (scene_grid(ctx, 0) != 0 || (id = mk_anims(ctx, at, 1, 0)) == 0) {
        g_fail++;
        return;
    }
    w = sa1_character_world(ctx, id);
    sa1_character_set_game_option(ctx, id, SA1_GOPT_SUPER_IDLE_BY_FORM, 1);
    run(ctx, id, 30, 0.0f, 0.0f, NULL, &x);
    wr16w(w, pwp0(w) + 0x0Eu, 1801);
    for (i = 0; i < 300; i++) {
        uint32_t m, a;
        tick(ctx, id, 0.0f, 0.0f, 0, NULL, &x);
        m = drawn_mot(w, &link);
        a = (uint32_t)x.req_action;
        if (link) {
            continue;
        }
        if (a >= 4u && a <= 6u) {
            uint32_t gt_mot = x32(w, x32(w, w->sg.gt + 16u * a) + 4u);
            seen |= 1u << a;
            idle_pairs++;
            gt_ok += m == gt_mot && m == rd32(w, rd32(w, A_SONIC_ACTIONS + 16u * a) + 4u) &&
                     rd32(w, pwp0(w) + P_PLACTPTR) == w->sg.ga &&
                     (x32(w, x32(w, w->sg.ga + 16u * a)) & 0x1FFFFFFFu) == (O_SUPER_OBJECT & 0x1FFFFFFFu);
            if (ext_of(ctx, id, 1.0f, SA1_SOURCE_CHARACTER, &lo, &hi, NULL)) {
                lo_min = lo < lo_min ? lo : lo_min;
            }
            after = 1;
        } else if (after && m == w->sg.ga_mot[0]) {
            back130++;
        }
    }
    CHECK(seen == 0x70u && gt_ok == idle_pairs && idle_pairs > 0 && lo_min > -0.35f && back130 > 0,
          "actions 4, 5, 6 drawn (%s): Sonic's own idle motions on the Super body in %u of %u pairs (the lowest point %.3f: "
          "standing, not floating); afterwards the float (130) again in %u pairs", seen == 0x70u ? "all three" : "NOT all",
          (unsigned)gt_ok, (unsigned)idle_pairs, (double)lo_min, (unsigned)back130);
    idle_voices(ctx, id, 2, &v227, &v390, &v388, &v396);
    CHECK(v388 + v396 > 0, "the idle voice still comes (SA1_GOPT_SUPER_IDLE_BY_FORM 1): 388 x%u, 396 x%u (227 x%u, 390 x%u)",
          (unsigned)v388, (unsigned)v396, (unsigned)v227, (unsigned)v390);
    sa1_character_destroy(ctx, id);
}

/* (7) the option toggled at run time: in the sprint and during links */
static void t_general_anims_switch(sa1_context *ctx)
{
    static const float at[3] = { -2000.0f, 0.5f, 0.0f };
    sa1_character_id id;
    sa1g_world *w;
    sa1_state_ex x;
    int32_t a_ss = archive_named(ctx, SA1_CHAR_SONIC, "SUPERSONIC");
    bstat bs;
    uint32_t pwp, m, links = 0, links_ok = 0, frames_bad = 0, draws_bad = 0, toggles = 0;
    float f0;
    int i, link, on = 0;
    printf("general_anims_switch (GRID): SA1_SUPOPT_SUPER_ANIMS toggled in the sprint and during links (super2_anim.md 5.5)\n");
    if (scene_big(ctx, 20000.0f, 0, 0.0f) != 0 || (id = mk_anims(ctx, at, 0, 0)) == 0) {
        g_fail++;
        return;
    }
    w = sa1_character_world(ctx, id);
    sa1_character_set_super_option(ctx, id, SA1_SUPOPT_SPEED, SA1_SUPER_SPEED_RUN);
    run(ctx, id, 6, 0.0f, 0.0f, NULL, &x);
    pwp = pwp0(w);
    run(ctx, id, 260, 0.0f, 1.0f, NULL, &x);
    for (i = 0; i < 60 && !(x.req_action == 13 && rdf(w, pwp + P_NFRAME) > 9.0f); i++) {
        tick(ctx, id, 0.0f, 1.0f, 0, NULL, &x);
    }
    f0 = rdf(w, pwp + P_NFRAME);
    sa1_character_set_super_option(ctx, id, SA1_SUPOPT_SUPER_ANIMS, 1);
    tick(ctx, id, 0.0f, 1.0f, 0, NULL, &x);
    m = drawn_mot(w, &link);
    body_stats(ctx, id, a_ss, &bs);
    CHECK(x.req_action == 13 && rd32(w, pwp + P_PLACTPTR) == w->sg.ga && m == w->sg.ga_mot[3] &&
              rdf(w, pwp + P_NFRAME) < 8.0f && bs.body == 47 && bs.in_arch == 47,
          "switched on in the sprint at frame %.2f of 33: the next pair draws 134's copy at frame %.2f of 8 (wrapped), %u "
          "body draws, %u of SUPERSONIC", (double)f0, (double)rdf(w, pwp + P_NFRAME), (unsigned)bs.body, (unsigned)bs.in_arch);
    on = 1;
    /* links: stick changes, jumps and landings; the option toggled right before every pair that starts in a link */
    for (i = 0; i < 900 && toggles < 12u; i++) {
        float sy = (i / 40) % 3 == 0 ? 1.0f : ((i / 40) % 3 == 1 ? 0.3f : 0.0f);
        uint32_t b = (i % 70) == 30 ? SA1_BUTTON_JUMP : 0u, nb;
        int linking = (int16_t)rd16w(w, pwp + P_MTNMODE) == 2;
        if (linking || (i % 97) == 0) {
            on ^= 1;
            sa1_character_set_super_option(ctx, id, SA1_SUPOPT_SUPER_ANIMS, on);
            toggles++;
        }
        tick(ctx, id, 0.0f, sy, b, NULL, &x);
        m = drawn_mot(w, &link);
        body_stats(ctx, id, a_ss, &bs);
        if (link) {
            uint32_t lk = rd32(w, pwp + P_ACTWKPTR);
            links++;
            links_ok += (rd32(w, lk) & 0x1FFFFFFFu) == (O_SUPER_OBJECT & 0x1FFFFFFFu) ||
                        (rd32(w, lk) & 0x1FFFFFFFu) == (O_SUPER_BALL & 0x1FFFFFFFu);
        } else if (m != 0) {
            nb = x32(w, m + 4u);
            frames_bad += !(rdf(w, pwp + P_NFRAME) >= 0.0f && rdf(w, pwp + P_NFRAME) <= (float)nb);
        }
        draws_bad += bs.body == 0 || bs.in_arch != bs.body;
    }
    CHECK(toggles >= 6u && links > 0 && links_ok == links && frames_bad == 0 && draws_bad == 0,
          "%u toggles (%u in links): every link action's object a Super object (%u of %u), no drawn frame outside its "
          "motion (%u), every pair's body all SUPERSONIC (%u bad)", (unsigned)toggles, (unsigned)links, (unsigned)links_ok,
          (unsigned)links, (unsigned)frames_bad, (unsigned)draws_bad);
    sa1_character_destroy(ctx, id);
}

/* (8) the welds and the hand while floating; carrying (review R.9) */
typedef struct carry_rec {
    int32_t action, mtnmode;
    uint32_t held;
    float pos[3];
} carry_rec;

static int carry_run(int anims, carry_rec *out, int n, uint32_t *gt_kept)
{
    sa1_context *hc = hold_ctx();
    sa1_character_id id;
    sa1g_world *w;
    sa1_state_ex x;
    uint32_t obj;
    int i;
    *gt_kept = 0;
    if (hc == NULL) {
        return -1;
    }
    id = hold_char(hc, SA1_CHAR_SONIC, SA1_CREATE_SUPER_CAPABLE, SA1_SUPER_GENERAL | SA1_SUPER_START | SA1_SUPER_NO_DRAIN);
    if (id == 0 || sa1_character_set_super_option(hc, id, SA1_SUPOPT_SUPER_ANIMS, anims) != SA1_OK ||
        (obj = hold_lift(hc, id)) == 0) {
        sa1_shutdown(hc);
        return -1;
    }
    w = sa1_character_world(hc, id);
    for (i = 0; i < n; i++) {
        sa1_hostobj_held hd;
        uint32_t pwp, a;
        int link;
        tick(hc, id, 0.0f, (i / 30) % 2 ? 0.6f : 0.0f, 0, NULL, &x);
        memset(&hd, 0, sizeof hd);
        hd.struct_size = sizeof hd;
        sa1_character_held(hc, id, &hd);
        pwp = pwp0(w);
        out[i].action = x.req_action;
        out[i].mtnmode = pwp ? (int16_t)rd16w(w, pwp + P_MTNMODE) : -1;
        out[i].held = hd.object == obj;
        memcpy(out[i].pos, hd.position, 12);
        a = (uint32_t)x.req_action;
        if (a >= 33u && a <= 38u && drawn_mot(w, &link) == rd32(w, rd32(w, A_SONIC_ACTIONS + 16u * a) + 4u) && !link) {
            (*gt_kept)++;
        }
    }
    sa1_shutdown(hc);
    return 0;
}

static void t_general_anims_welds_carry(sa1_context *ctx)
{
    static const float at[3] = { -2000.0f, 0.5f, 0.0f };
    carry_rec r0[90], r1[90];
    sa1_character_id id;
    sa1g_world *w;
    sa1_state_ex x;
    uint32_t n, n_super, n_org, n_depth, k0, k1, carry = 0, near = 0;
    float y0 = 1e9f, y1 = -1e9f, worst_link = 0.0f, worst = 0.0f;
    int i, started = 0;
    printf("general_anims_welds_carry (GRID): the 12 Super welds and the hand points while floating; carrying (review R.9)\n");
    if (scene_grid(ctx, 0) != 0 || (id = mk_anims(ctx, at, 1, 0)) == 0) {
        g_fail++;
        return;
    }
    w = sa1_character_world(ctx, id);
    run(ctx, id, 30, 0.0f, 0.0f, NULL, &x);
    gw_stats(w, &n, &n_super, &n_org, &n_depth);
    for (i = 0; i < 140; i++) {
        float y;
        tick(ctx, id, 0.0f, 0.0f, 0, NULL, &x);
        y = rdf(w, pwp0(w) + P_RHAND + 4u);
        y0 = y < y0 ? y : y0;
        y1 = y > y1 ? y : y1;
    }
    CHECK(n_super == 12u && n_org == 12u && w->sg.welds == 1, "the 12 Super welds set up (%u at inpmode 2, %u with org "
          "buffers) and active", (unsigned)n_super, (unsigned)n_org);
    CHECK(y0 > -2.08f - 0.05f && y1 < -1.65f + 0.05f && !(y0 < -2.4f || y1 > -1.3f),
          "floating: the right-hand point's y %.3f..%.3f (the Super motion's -2.08..-1.65, not Sonic's stand -2.57..-1.11)",
          (double)y0, (double)y1);
    sa1_character_destroy(ctx, id);
    if (carry_run(0, r0, 90, &k0) != 0 || carry_run(1, r1, 90, &k1) != 0) {
        CHECK(0, "carrying: the General Super Sonic did not lift the HOLDABLE (Source preset, hostobj_test's lift)");
        return;
    }
    for (i = 0; i < 90; i++) {
        float d;
        if (!r0[i].held || !r1[i].held) {
            continue;
        }
        d = fabsf(r0[i].pos[0] - r1[i].pos[0]) + fabsf(r0[i].pos[1] - r1[i].pos[1]) + fabsf(r0[i].pos[2] - r1[i].pos[2]);
        if (!started) {
            /* the pick-up link: the hands start from the floating pose (reported, no pass / fail) */
            worst_link = d > worst_link ? d : worst_link;
            if (r1[i].action >= 33 && r1[i].action <= 38 && r1[i].mtnmode != 2 && r0[i].mtnmode != 2) {
                started = 1;
            } else {
                continue;
            }
        }
        carry++;
        near += d <= 0.05f * 4.0f;      /* 0.05 SA1 units = 0.2 HU (Source preset) */
        worst = d > worst ? d : worst;
    }
    printf("  info  the pick-up link: the held object's largest gap to the option-0 run %.3f HU (the hands start from the "
           "floating pose)\n", (double)worst_link);
    CHECK(carry > 30u && near == carry && k1 > 0, "carrying with the option 1: the carry actions 33-38 drawn with Sonic's "
          "motions in %u pairs (option 0: %u); from the first pair after the pick-up link on, the held object within 0.05 "
          "units of the option-0 run's place in %u of %u pairs (worst %.3f HU)", (unsigned)k1, (unsigned)k0,
          (unsigned)near, (unsigned)carry, (double)worst);
}

/* (9) the CPU pose of a hidden pair while floating */
static void t_general_anims_cpu_pose(sa1_context *ctx)
{
    static const float at[3] = { -2000.0f, 0.5f, 0.0f }, at2[3] = { -1800.0f, 0.5f, 0.0f };
    int32_t a_ss = archive_named(ctx, SA1_CHAR_SONIC, "SUPERSONIC");
    sa1_character_id id, ref;
    sa1_state_ex x, y;
    bstat b;
    float plo, phi, clo, chi, worst = 0.0f;
    int i, n = 0, ball;
    printf("general_anims_cpu_pose (GRID): hidden pairs of the floating stand: the CPU pose at the game's placement\n");
    if (scene_grid(ctx, 0) != 0 || (id = mk_anims(ctx, at, 1, 0)) == 0 || (ref = mk_anims(ctx, at2, 1, 0)) == 0) {
        g_fail++;
        return;
    }
    run(ctx, id, 30, 0.0f, 0.0f, NULL, &x);
    run(ctx, ref, 30, 0.0f, 0.0f, NULL, &y);
    zero_screen(sa1_character_world(ctx, id));
    for (i = 0; i < 40; i++) {
        tick(ctx, id, 0.0f, 0.0f, 0, NULL, &x);
        tick(ctx, ref, 0.0f, 0.0f, 0, NULL, &y);
        if (i < 2 || !ext_of(ctx, id, 1.0f, SA1_SOURCE_POSE, &plo, &phi, NULL) ||
            !ext_of(ctx, ref, 1.0f, SA1_SOURCE_CHARACTER, &clo, &chi, NULL)) {
            continue;
        }
        n++;
        worst = fabsf(plo - clo) > worst ? fabsf(plo - clo) : worst;
    }
    body_stats(ctx, id, a_ss, &b);
    CHECK(n > 30 && worst <= 0.05f && b.pose > 0 && b.pose_in_arch == b.pose,
          "standing, the camera blind: the CPU pose's feet within %.3f of a visible twin's captured feet over %d pairs (0.05; "
          "0.9.4: 0.08 against -1.00), %u of %u pose draws SUPERSONIC", (double)worst, n, (unsigned)b.pose_in_arch,
          (unsigned)b.pose);
    sa1_character_destroy(ctx, id);
    sa1_character_destroy(ctx, ref);
    for (ball = 1; ball < 2; ball++) {
        if ((id = mk_anims(ctx, at, 1, 0)) == 0) {
            g_fail++;
            return;
        }
        run(ctx, id, 10, 0.0f, 0.0f, NULL, &x);
        run(ctx, id, 10, 0.0f, 1.0f, NULL, &x);
        into_jump(ctx, id, 1.0f, &x);
        zero_screen(sa1_character_world(ctx, id));
        tick(ctx, id, 0.0f, 1.0f, SA1_BUTTON_JUMP, NULL, &x);
        tick(ctx, id, 0.0f, 1.0f, SA1_BUTTON_JUMP, NULL, &x);
        body_stats(ctx, id, a_ss, &b);
        CHECK(b.pose > 0 && b.pose_in_arch == b.pose && b.body == 0, "in the jump ball, the camera blind: the CPU pose has %u "
              "draws, %u of SUPERSONIC (the Super ball, as 0.9.3a); action %d", (unsigned)b.pose, (unsigned)b.pose_in_arch,
              (int)x.action);
        sa1_character_destroy(ctx, id);
    }
}

/* (10) the render interpolation's hint (review R.1): the drawn object and motion */
static float mid_dev(sa1_context *ctx, sa1_character_id id)
{
    float l0, h0, s0, l1, h1, s1, lm, hm, sm, dv;
    ext_of(ctx, id, 0.0f, SA1_SOURCE_CHARACTER, &l0, &h0, &s0);
    ext_of(ctx, id, 1.0f, SA1_SOURCE_CHARACTER, &l1, &h1, &s1);
    ext_of(ctx, id, 0.5f, SA1_SOURCE_CHARACTER, &lm, &hm, &sm);
    dv = fabsf(lm - 0.5f * (l0 + l1));
    dv = fabsf(hm - 0.5f * (h0 + h1)) > dv ? fabsf(hm - 0.5f * (h0 + h1)) : dv;
    dv = fabsf(sm - 0.5f * (s0 + s1)) > dv ? fabsf(sm - 0.5f * (s0 + s1)) : dv;
    return dv;
}

/* the hint of the last pair against the expected (object, motion) of action `a` (its info's addresses); 1 match, 0 not,
 * -1 no hint this pair */
static int hint_is(sa1_context *ctx, sa1_character_id id, int32_t a, uint32_t want_obj, uint32_t want_mot, uint32_t *got_mot)
{
    sa1w_hint_info h;
    if (sa1w_test_hint(ctx, id, &h) != SA1_OK || !h.valid || !h.dvalid || h.action != a || h.node < 0) {
        return -1;
    }
    *got_mot = h.hmot;
    return h.hobj == (want_obj & 0x1FFFFFFFu) && h.hmot == (want_mot & 0x1FFFFFFFu);
}

static uint32_t act_mot(sa1_context *ctx, uint32_t a)
{
    sa1_action_info ai;
    memset(&ai, 0, sizeof ai);
    ai.struct_size = sizeof ai;
    return sa1_action_info_get(ctx, SA1_CHAR_SONIC, a, &ai) == SA1_OK ? ai.motion_addr : 0u;
}

typedef struct gt_hint {
    uint32_t n, ok, legacy_n, legacy_ok, legacy_mot;
} gt_hint;

/* one General world (option `anims`), the moves that reach the GT rows 13, 20 and 29 (and the GA rows); per row the hint */
static void hint_run(sa1_context *ctx, int anims, int legacy, gt_hint *rows, uint32_t *ga_n, uint32_t *ga_ok,
                     float *dev_sprint)
{
    static const float at[3] = { -2000.0f, 0.5f, 0.0f };
    static const int32_t gt_rows[3] = { 13, 20, 29 };
    sa1_character_id id;
    sa1_state_ex x;
    int i, r;
    *dev_sprint = 0.0f;
    sa1w_test_hint_legacy(legacy);
    if ((id = mk_anims(ctx, at, anims, 0)) == 0) {
        sa1w_test_hint_legacy(0);
        return;
    }
    give_upgrades(ctx, id);
    sa1_character_set_super_option(ctx, id, SA1_SUPOPT_SPEED, SA1_SUPER_SPEED_RUN);
    for (i = 0; i < 760; i++) {
        float sy = i < 60 ? 0.0f : (i < 100 ? 0.15f : (i < 140 ? 0.35f : (i < 440 ? 1.0f : (i < 470 ? -1.0f : 0.0f))));
        uint32_t b = (i >= 520 && i < 620) ? SA1_BUTTON_ACTION : 0u, got = 0;
        int32_t a;
        tick(ctx, id, 0.0f, sy, b, NULL, &x);
        a = x.action;
        if (x.action == x.req_action && x.action == 13) {
            float d = mid_dev(ctx, id);
            *dev_sprint = d > *dev_sprint ? d : *dev_sprint;
            if (g_verbose && d > 0.3f) {
                sa1w_hint_info h;
                sa1w_test_hint(ctx, id, &h);
                printf("    %s option %d pair %d: deviation %.3f, spd.x %.2f frame %.2f, hint valid %d dvalid %d df %.2f node %d "
                       "motion %08X%c", legacy ? "0.9.4" : "now", anims, i, (double)d, (double)x.player_speed[0],
                       (double)x.frame, h.valid, h.dvalid, h.df, h.node, (unsigned)h.hmot, 10);
            }
        }
        if (anims && sa1w_super_anims_row((uint32_t)a) != 0) {
            uint32_t s = sa1w_super_anims_row((uint32_t)a);
            int h = hint_is(ctx, id, a, O_SUPER_OBJECT, act_mot(ctx, s), &got);
            if (h >= 0 && !legacy) {
                (*ga_n)++;
                *ga_ok += (uint32_t)h;
            }
            continue;
        }
        for (r = 0; r < 3; r++) {
            int h;
            if (a != gt_rows[r]) {
                continue;
            }
            h = hint_is(ctx, id, a, O_SUPER_OBJECT, act_mot(ctx, (uint32_t)a), &got);
            if (h < 0) {
                continue;
            }
            if (legacy) {
                rows[r].legacy_n++;
                rows[r].legacy_ok += (uint32_t)h;
                rows[r].legacy_mot = got;
            } else {
                rows[r].n++;
                rows[r].ok += (uint32_t)h;
            }
        }
    }
    sa1w_test_hint_legacy(0);
    sa1_character_destroy(ctx, id);
}

static void t_general_anims_hint(sa1_context *ctx)
{
    static const char *const rn[3] = { "13 (sprint)", "20 (brake)", "29 (uncurl)" };
    gt_hint rows[2][3];
    uint32_t ga_n = 0, ga_ok = 0, dummy_n = 0, dummy_ok = 0;
    float dev[2] = { 0.0f, 0.0f }, dev_l[2] = { 0.0f, 0.0f };
    int o, r, gt_all = 1;
    printf("general_anims_hint (GRID): the interpolation hint follows the drawn object and motion (review R.1)\n");
    if (scene_big(ctx, 20000.0f, 0, 0.0f) != 0) {
        g_fail++;
        return;
    }
    memset(rows, 0, sizeof rows);
    for (o = 0; o < 2; o++) {
        hint_run(ctx, o, 0, rows[o], o ? &ga_n : &dummy_n, o ? &ga_ok : &dummy_ok, &dev[o]);
        hint_run(ctx, o, 1, rows[o], &dummy_n, &dummy_ok, &dev_l[o]);
    }
    CHECK(ga_n > 100u && ga_ok == ga_n, "option 1, GA rows: the hint's object and motion are the game's Super action's "
          "(cs->actions[s]) in %u of %u steady pairs", (unsigned)ga_ok, (unsigned)ga_n);
    for (o = 0; o < 2; o++) {
        for (r = 0; r < 3; r++) {
            const gt_hint *g = &rows[o][r];
            if (o == 1 && r == 0) {
                continue;       /* 13 is a GA row with the option 1 */
            }
            printf("  info  option %d, GT row %-12s: hint = the Super body with Sonic's own motion in %u of %u pairs; 0.9.4's "
                   "hint: %u of %u (it took motion %08X)\n", o, rn[r], (unsigned)g->ok, (unsigned)g->n,
                   (unsigned)g->legacy_ok, (unsigned)g->legacy_n, (unsigned)g->legacy_mot);
            if (g->n == 0 || g->ok != g->n) {
                gt_all = 0;
            }
        }
    }
    CHECK(gt_all, "GT rows 13 (option 0), 20 and 29 (both options): the hint is the Super body with the row's own motion in "
          "every steady pair (0.9.4's frame-count fallback took a Super action's motion)");
    printf("  info  the largest mid-pair deviation of the body extent in the sprint (action 13), option 0: %.3f now, %.3f with "
           "0.9.4's hint (the probe: V0 1.82); option 1: %.3f now, %.3f with 0.9.4's hint\n", (double)dev[0],
           (double)dev_l[0], (double)dev[1], (double)dev_l[1]);
}

/* (10b) the owner's 0.9.4 note "the run animations break a little bit when moving at extreme speed" (General Super Sonic,
 * mode 2 action 13: SADC/build/research/owner_logs/log_094_20261026.txt 18:50:16): the sprint at the RUN speed set's top
 * speeds, every steady pair interpolated at alpha 0.5, each root-frame model's turn between the pairs against the drawn
 * motion's own net turn (sa1w_test_hint), with 0.9.4's hint and now, the poses off (GT row 13: Sonic's sprint on the Super
 * body; 0.9.4's frame-count fallback took 136's s_baneup1) and on (GA: 134's copy) */
typedef struct xs_acc {
    uint32_t pairs, checked, wrong, snapped, over_half, hinted;
    double worst, worst_mid, df_lo, df_hi;
    float spd_lo, spd_hi;
} xs_acc;

static void xs_run(sa1_context *ctx, int anims, int legacy, xs_acc *A)
{
    static const float at[3] = { 0.0f, 0.5f, 0.0f };
    sa1_character_id id;
    sa1_state_ex x;
    int i;
    memset(A, 0, sizeof *A);
    A->df_lo = 1e9;
    A->spd_lo = 1e9f;
    sa1w_test_hint_legacy(legacy);
    if ((id = mk_anims(ctx, at, anims, 0)) == 0) {
        sa1w_test_hint_legacy(0);
        g_fail++;
        return;
    }
    give_upgrades(ctx, id);
    sa1_character_set_super_option(ctx, id, SA1_SUPOPT_SPEED, SA1_SUPER_SPEED_RUN);
    for (i = 0; i < 700; i++) {
        const sa1_render *rd = NULL;
        sa1w_hint_info h;
        sa1_lerp_stats ls;
        tick(ctx, id, 0.0f, 1.0f, 0, NULL, &x);
        if (i < 250 || x.action != 13 || x.req_action != 13 || sa1_character_render(ctx, id, 0.5f, &rd) != SA1_OK ||
            sa1w_test_hint(ctx, id, &h) != SA1_OK || !h.valid || h.checked == 0) {
            continue;
        }
        memset(&ls, 0, sizeof ls);
        ls.struct_size = sizeof ls;
        if (sa1_character_lerp_stats(ctx, id, &ls) != SA1_OK) {
            continue;
        }
        A->pairs++;
        A->checked += h.checked;
        A->wrong += h.wrong;
        A->snapped += h.snapped;
        A->over_half += ls.over_half_turn;
        A->hinted += ls.hinted;
        A->worst = h.worst > A->worst ? h.worst : A->worst;
        A->worst_mid = h.worst_mid > A->worst_mid ? h.worst_mid : A->worst_mid;
        A->df_lo = h.tdf < A->df_lo ? h.tdf : A->df_lo;
        A->df_hi = h.tdf > A->df_hi ? h.tdf : A->df_hi;
        A->spd_lo = x.player_speed[0] < A->spd_lo ? x.player_speed[0] : A->spd_lo;
        A->spd_hi = x.player_speed[0] > A->spd_hi ? x.player_speed[0] : A->spd_hi;
        if (g_verbose && h.wrong) {
            printf("    %s option %d pair %d: spd.x %.2f, drawn df %.2f (the plan's %.2f), %u of %u models off (%u snapped), "
                   "worst %.1f degrees\n", legacy ? "0.9.4" : "now", anims, i, (double)x.player_speed[0], h.tdf, h.df,
                   (unsigned)h.wrong, (unsigned)h.checked, (unsigned)h.snapped, h.worst * 57.29577951308232);
        }
    }
    sa1w_test_hint_legacy(0);
    sa1_character_destroy(ctx, id);
}

static void t_general_anims_extreme(sa1_context *ctx)
{
    xs_acc A[2][2];
    int o, l, ok = 1;
    printf("general_anims_extreme (GRID): the sprint at extreme speed, interpolated (the owner's 0.9.4 note; review R.1)\n");
    if (scene_big(ctx, 20000.0f, 0, 0.0f) != 0) {
        g_fail++;
        return;
    }
    for (o = 0; o < 2; o++) {
        for (l = 0; l < 2; l++) {
            const xs_acc *a;
            xs_run(ctx, o, l, &A[o][l]);
            a = &A[o][l];
            printf("  info  option %d, %s: %u steady sprint pairs at spd.x %.2f..%.2f (drawn frame advance %.2f..%.2f per "
                   "pair), %u root-frame models checked: %u turned more than 20 degrees off the drawn motion (%u SNAPs), the "
                   "worst %.1f degrees, at mid-pair %.1f; the plan's stats: %u hinted, %u over half a turn\n", o,
                   l ? "0.9.4's hint" : "now", (unsigned)a->pairs, (double)a->spd_lo, (double)a->spd_hi, a->df_lo, a->df_hi,
                   (unsigned)a->checked, (unsigned)a->wrong, (unsigned)a->snapped, a->worst * 57.29577951308232,
                   a->worst_mid * 57.29577951308232, (unsigned)a->hinted, (unsigned)a->over_half);
        }
        if (A[o][0].pairs < 100u || A[o][0].spd_hi < 9.0f || A[o][0].wrong != 0u || A[o][0].snapped != 0u) {
            ok = 0;
        }
    }
    CHECK(ok, "now, the poses off and on: at least 100 steady sprint pairs up to spd.x >= 9 (%.2f / %.2f), every root-frame "
          "model turning as the drawn motion does (%u / %u off by more than 20 degrees, %u / %u SNAPs)",
          (double)A[0][0].spd_hi, (double)A[1][0].spd_hi, (unsigned)A[0][0].wrong, (unsigned)A[1][0].wrong,
          (unsigned)A[0][0].snapped, (unsigned)A[1][0].snapped);
}

/* (11) the revert at 0 rings in 134 and in the stand; a transformation in a jump */
static void t_general_anims_revert(sa1_context *ctx)
{
    static const float at[3] = { -2000.0f, 0.5f, 0.0f };
    int32_t a_son = archive_named(ctx, SA1_CHAR_SONIC, "SONIC");
    sa1_character_id id;
    sa1g_world *w;
    sa1_state_ex x;
    bstat b;
    int phase, i, link;
    printf("general_anims_revert (GRID): the revert at 0 rings from 134 and from the stand; a transformation in a jump\n");
    if (scene_big(ctx, 20000.0f, 0, 0.0f) != 0) {
        g_fail++;
        return;
    }
    for (phase = 0; phase < 2; phase++) {
        uint32_t pre_act = 0, m, nb;
        id = mk_gen(ctx, at, 3, SA1_SUPER_START, NULL);
        if (id == 0) {
            g_fail++;
            return;
        }
        sa1_character_set_super_option(ctx, id, SA1_SUPOPT_SUPER_ANIMS, 1);
        sa1_character_set_super_option(ctx, id, SA1_SUPOPT_SPEED, SA1_SUPER_SPEED_RUN);
        w = sa1_character_world(ctx, id);
        for (i = 0; i < 400; i++) {
            tick(ctx, id, 0.0f, phase == 0 ? 1.0f : 0.0f, 0, NULL, &x);
            if (!(x.equipment & 0x8000u)) {
                break;
            }
            pre_act = drawn_mot(w, &link);
        }
        tick(ctx, id, 0.0f, phase == 0 ? 1.0f : 0.0f, 0, NULL, &x);
        m = drawn_mot(w, &link);
        nb = x32(w, m + 4u);
        body_stats(ctx, id, a_son, &b);
        CHECK(i < 400 && !(x.equipment & 0x8000u) && rd32(w, pwp0(w) + P_PLACTPTR) == A_SONIC_ACTIONS &&
                  (link || m == rd32(w, rd32(w, A_SONIC_ACTIONS + 16u * (uint32_t)x.req_action) + 4u)) && b.body > 0 &&
                  b.in_arch == b.body && x.frame >= 0.0f && x.frame <= (float)nb,
              "the revert at 0 rings %s (drawn before: %08X): the next pair draws Sonic's action %d from SONIC's archive "
              "(%u of %u body draws), frame %.2f of %u", phase == 0 ? "in 134" : "in the stand", (unsigned)pre_act,
              (int)x.req_action, (unsigned)b.in_arch, (unsigned)b.body, (double)x.frame, (unsigned)nb);
        sa1_character_destroy(ctx, id);
    }
    /* a transformation in a jump with the option on: the next stand floats */
    if ((id = mk_gen(ctx, at, 50, SA1_SUPER_NO_DRAIN, NULL)) == 0) {
        g_fail++;
        return;
    }
    sa1_character_set_super_option(ctx, id, SA1_SUPOPT_SUPER_ANIMS, 1);
    w = sa1_character_world(ctx, id);
    run(ctx, id, 10, 0.0f, 0.0f, NULL, &x);
    into_jump(ctx, id, 0.0f, &x);
    sa1_character_order(ctx, id, SA1_ORDER_SUPER);
    for (i = 0; i < 120; i++) {
        tick(ctx, id, 0.0f, 0.0f, 0, NULL, &x);
    }
    CHECK((x.equipment & 0x8000u) && rd32(w, pwp0(w) + P_PLACTPTR) == w->sg.ga && drawn_mot(w, &link) == w->sg.ga_mot[0],
          "Super in a jump (the order in the air), landed: the stand draws 130's copy (action %d)", (int)x.req_action);
    sa1_character_destroy(ctx, id);
}

/* (12) a General world re-created with the option set */
static void t_general_anims_rebuild(sa1_context *ctx)
{
    static const float at[3] = { -2000.0f, 0.5f, 0.0f };
    sa1_character_id id;
    sa1g_world *w;
    sa1_state_ex x;
    int k, link;
    printf("general_anims_rebuild (GRID): a General world re-created with the option set floats after its first pair\n");
    if (scene_grid(ctx, 0) != 0) {
        g_fail++;
        return;
    }
    for (k = 0; k < 2; k++) {
        if ((id = mk_anims(ctx, at, 1, 0)) == 0) {
            g_fail++;
            return;
        }
        w = sa1_character_world(ctx, id);
        tick(ctx, id, 0.0f, 0.0f, 0, NULL, &x);
        CHECK(w->sg.anims == 1 && w->sg.ga != 0 && w->sg.look && rd32(w, pwp0(w) + P_PLACTPTR) == w->sg.ga,
              "%s: after the first pair GA is built and in plactptr (the option from the attach)", k ? "re-created" : "created");
        run(ctx, id, 20, 0.0f, 0.0f, NULL, &x);
        CHECK(drawn_mot(w, &link) == w->sg.ga_mot[0] && !link, "%s: standing, 130's copy drawn (action %d)",
              k ? "re-created" : "created", (int)x.req_action);
        sa1_character_destroy(ctx, id);
    }
}

/* (13) the refusals; Vs. Perfect Chaos stays the game */
static void t_general_anims_kind8(sa1_context *ctx)
{
    static const float at[3] = { 0.0f, 0.5f, 0.0f };
    sa1_character_id k8, son, old, gen;
    sa1_status s[12];
    printf("general_anims_kind8 (GRID): the new options refuse kind 8 and every Sonic that is not General\n");
    if (scene_grid(ctx, 0) != 0 || (k8 = mk(ctx, SA1_CHAR_SUPER_SONIC, at, 0, -1, 1, SA1_SUPER_NO_DRAIN, NULL)) == 0 ||
        (son = mk(ctx, SA1_CHAR_SONIC, at, 0, -1, 0, 0, NULL)) == 0 ||
        (old = mk(ctx, SA1_CHAR_SONIC, at, SA1_CREATE_SUPER_CAPABLE, 50, 0, 0, NULL)) == 0 ||
        (gen = mk_gen(ctx, at, 50, SA1_SUPER_NO_DRAIN, NULL)) == 0) {
        g_fail++;
        return;
    }
    s[0] = sa1_character_set_super_option(ctx, k8, SA1_SUPOPT_SUPER_ANIMS, 1);
    s[1] = sa1_character_set_super_option(ctx, k8, SA1_SUPOPT_LSD_GOLD, 1);
    s[2] = sa1_character_set_super_option(ctx, son, SA1_SUPOPT_SUPER_ANIMS, 1);
    s[3] = sa1_character_set_super_option(ctx, son, SA1_SUPOPT_LSD_GOLD, 1);
    s[4] = sa1_character_set_super_option(ctx, old, SA1_SUPOPT_SUPER_ANIMS, 1);
    s[5] = sa1_character_set_super_option(ctx, old, SA1_SUPOPT_LSD_GOLD, 1);
    s[6] = sa1_character_set_super_option(ctx, gen, SA1_SUPOPT_SUPER_ANIMS, 2);
    s[7] = sa1_character_set_super_option(ctx, gen, SA1_SUPOPT_LSD_GOLD, 2);
    s[8] = sa1_character_set_super_option(ctx, gen, SA1_SUPOPT_SUPER_EFFECTS, 4);
    s[9] = sa1_character_set_super_option(ctx, k8, SA1_SUPOPT_SUPER_EFFECTS, 3);
    s[10] = sa1_character_set_super_option(ctx, gen, SA1_SUPOPT_SUPER_ANIMS, 1);
    s[11] = sa1_character_set_super_option(ctx, gen, 7u, 1);
    CHECK(s[0] == SA1_ERR_UNSUPPORTED && s[1] == SA1_ERR_UNSUPPORTED && s[2] == SA1_ERR_UNSUPPORTED &&
              s[3] == SA1_ERR_UNSUPPORTED && s[4] == SA1_ERR_UNSUPPORTED && s[5] == SA1_ERR_UNSUPPORTED,
          "SUPER_ANIMS / LSD_GOLD: kind 8 %d / %d, plain Sonic %d / %d, a 0.9.3-style super-capable Sonic %d / %d "
          "(SA1_ERR_UNSUPPORTED)", (int)s[0], (int)s[1], (int)s[2], (int)s[3], (int)s[4], (int)s[5]);
    CHECK(s[6] == SA1_ERR_INVALID_ARGUMENT && s[7] == SA1_ERR_INVALID_ARGUMENT && s[8] == SA1_ERR_INVALID_ARGUMENT &&
              s[9] == SA1_ERR_UNSUPPORTED && s[10] == SA1_OK && s[11] == SA1_ERR_INVALID_ARGUMENT,
          "a General world: SUPER_ANIMS 2 %d, LSD_GOLD 2 %d, SUPER_EFFECTS 4 %d (SA1_ERR_INVALID_ARGUMENT), option 7 %d; "
          "SUPER_EFFECTS 3 on kind 8 %d (SA1_ERR_UNSUPPORTED); SUPER_ANIMS 1 %d", (int)s[6], (int)s[7], (int)s[8], (int)s[11],
          (int)s[9], (int)s[10]);
    sa1_character_destroy(ctx, k8);
    sa1_character_destroy(ctx, son);
    sa1_character_destroy(ctx, old);
    sa1_character_destroy(ctx, gen);
    if (g_k8_trace != 0 && g_det_masked != 0) {
        CHECK(g_k8_trace == 0x0F982DA6u && g_det_masked == 0xCF97EECBu, "Vs. Perfect Chaos stays the game: KIND8_TRACE %08X "
              "and DETERMINISM_MASKED %08X equal to the 0.9.4 baseline (0F982DA6 / CF97EECB)", (unsigned)g_k8_trace,
              (unsigned)g_det_masked);
    } else {
        printf("  info  kind8_trace / determinism did not run in this process: their hashes not compared\n");
    }
}

/* (14) standing on a 30-degree slope with the option (measured, no pass / fail: owner question O2) */
static void t_general_anims_slope(sa1_context *ctx)
{
    const float tn = 0.57735027f;      /* tan(30 degrees): the ground y = x * tan. The plan said 20 degrees, but Sonic's stand
                                          picks the slope stands 7 / 8 only between 25 and 45 degrees (8C0AE0E0's
                                          njCos(0x11C7) / njCos(0x2000): code_8C0AE0E0.c), so 20 degrees stands in 0 / 1 / 2 */
    tbuf tb;
    int dir;
    printf("general_anims_slope (GRID, measured): standing on a 30-degree slope (actions 7 / 8) with the option\n");
    memset(&tb, 0, sizeof tb);
    {
        float p[4][3] = { { -400.0f, -400.0f * tn, -400.0f }, { 400.0f, 400.0f * tn, -400.0f },
                          { 400.0f, 400.0f * tn, 400.0f }, { -400.0f, -400.0f * tn, 400.0f } };
        float n[3] = { -tn, 1.0f, 0.0f };
        tb_quad(&tb, p, n, SA1_SURFACE_SOLID);
    }
    scene_forget();
    if (SA1_FAILED(sa1_collision_set_static(ctx, tb.t, tb.n))) {
        free(tb.t);
        g_fail++;
        return;
    }
    free(tb.t);
    for (dir = 0; dir < 2; dir++) {
        sa1_character_desc d;
        sa1_character_id id = 0;
        sa1g_world *w;
        sa1_state_ex x;
        const sa1_render *rd = NULL;
        float up_min = 1e9f, down_min = 1e9f;
        uint32_t acts = 0, i, k, t;
        int link;
        memset(&d, 0, sizeof d);
        d.struct_size = sizeof d;
        d.kind = SA1_CHAR_SONIC;
        d.flags = SA1_CREATE_WORLD | SA1_CREATE_SA1_SPACE | SA1_CREATE_ANGLES | SA1_CREATE_SUPER_CAPABLE | SA1_CREATE_SUPER |
                  SA1_CREATE_START_RINGS;
        d.position[1] = 1.0f;
        d.angle[1] = dir ? 0x8000 : 0;
        d.start_rings = 50;
        d.super_flags = SA1_SUPER_GENERAL | SA1_SUPER_START | SA1_SUPER_NO_DRAIN;
        if (SA1_FAILED(sa1_character_create_ex(ctx, &d, &id))) {
            g_fail++;
            return;
        }
        sa1_character_set_super_option(ctx, id, SA1_SUPOPT_SUPER_ANIMS, 1);
        w = sa1_character_world(ctx, id);
        run(ctx, id, 40, 0.0f, 0.0f, NULL, &x);
        for (i = 0; i < 64; i++) {
            tick(ctx, id, 0.0f, 0.0f, 0, NULL, &x);
            acts |= (x.req_action == 7) ? 1u : (x.req_action == 8 ? 2u : 0u);
            if (sa1_character_render(ctx, id, 1.0f, &rd) != SA1_OK || rd == NULL) {
                continue;
            }
            for (k = 0; k < rd->num_draws; k++) {
                const sa1_draw *dr = &rd->draws[k];
                if (dr->prim != SA1_PRIM_TRIANGLES || dr->source != SA1_SOURCE_CHARACTER ||
                    (dr->flags & (SA1_DRAW_SHADOW | SA1_DRAW_ADDITIVE)) || dr->archive < 0) {
                    continue;
                }
                for (t = dr->first * 9u; t < (dr->first + dr->count) * 9u; t += 3u) {
                    float px = rd->positions[t], py = rd->positions[t + 1], c = py - px * tn;
                    if (px > rd->position[0]) {
                        up_min = c < up_min ? c : up_min;
                    } else {
                        down_min = c < down_min ? c : down_min;
                    }
                }
            }
        }
        printf("  info  facing %s: actions 7 / 8 seen %X, drawn %s; the lowest body point against the ground under the "
               "uphill side %.3f, the downhill side %.3f (SA1 units; < 0: in the slope)\n", dir ? "downhill" : "uphill",
               (unsigned)acts, drawn_mot(w, &link) == w->sg.ga_mot[0] ? "130's copy" : "another motion", (double)up_min,
               (double)down_min);
        sa1_character_destroy(ctx, id);
    }
}

/* (15) the Light Speed glow's lowest point at a charged stand (measured) */
static void t_general_anims_lsd_aura(sa1_context *ctx)
{
    static const float at[3] = { -2000.0f, 0.5f, 0.0f };
    int o;
    printf("general_anims_lsd_aura (GRID, measured): the charged glow's lowest point with the option 0 and 1\n");
    if (scene_grid(ctx, 0) != 0) {
        g_fail++;
        return;
    }
    for (o = 0; o < 2; o++) {
        sa1_character_id id = mk_anims(ctx, at, o, 0);
        sa1_state_ex x;
        float lo_min = 1e9f, lo_max = -1e9f;
        uint32_t charged = 0, i, k, t;
        if (id == 0) {
            g_fail++;
            return;
        }
        give_upgrades(ctx, id);
        run(ctx, id, 10, 0.0f, 0.0f, NULL, &x);
        for (i = 0; i < 130; i++) {
            const sa1_render *rd = NULL;
            float lo = 1e9f;
            uint32_t n = 0;
            tick(ctx, id, 0.0f, 0.0f, SA1_BUTTON_ACTION, NULL, &x);
            if (!(x.game_flags & 0x200u) || x.mode != 1 || sa1_character_render(ctx, id, 1.0f, &rd) != SA1_OK || rd == NULL) {
                continue;
            }
            for (k = 0; k < rd->num_draws; k++) {
                const sa1_draw *d = &rd->draws[k];
                /* the glow: the aura skeleton's untextured draws under the constant material of 8C0B4F00 (super2_lsd.md
                   2.2; the research probe's class "charged glow": archive -1, material 0x26182400) */
                if (d->prim != SA1_PRIM_TRIANGLES || (d->flags & SA1_DRAW_SHADOW) || d->archive >= 0 ||
                    d->material != 0x26182400u) {
                    continue;
                }
                n++;
                for (t = d->first * 9u; t < (d->first + d->count) * 9u; t += 3u) {
                    lo = rd->positions[t + 1] < lo ? rd->positions[t + 1] : lo;
                }
            }
            if (n) {
                charged++;
                lo -= rd->position[1];
                lo_min = lo < lo_min ? lo : lo_min;
                lo_max = lo > lo_max ? lo : lo_max;
            }
        }
        printf("  info  option %d: %u charged standing pairs with the glow; its lowest point %.3f..%.3f (research: -0.72 with "
               "Sonic's stand, -0.95 with the float)\n", o, (unsigned)charged, (double)lo_min, (double)lo_max);
        sa1_character_destroy(ctx, id);
    }
}

/* (16) SA1_SUPOPT_SUPER_EFFECTS' bits: the golden dust (M1) */
static int32_t tex_named(sa1_context *ctx, int32_t arch, const char *name)
{
    uint32_t i, n = 0;
    const char *an = NULL;
    if (arch < 0 || sa1_texture_archive(ctx, SA1_CHAR_SONIC, (uint32_t)arch, &an, &n) != SA1_OK) {
        return -1;
    }
    for (i = 0; i < n; i++) {
        sa1_texture_info ti;
        memset(&ti, 0, sizeof ti);
        ti.struct_size = sizeof ti;
        if (sa1_texture_get(ctx, SA1_CHAR_SONIC, (uint32_t)arch, i, &ti) == SA1_OK && ti.name != NULL &&
            strcmp(ti.name, name) == 0) {
            return (int32_t)i;
        }
    }
    return -1;
}

typedef struct dust_ids {
    int32_t a_eff, t_a, t_b, a_ss, s_a, s_b;
} dust_ids;

typedef struct dust_cnt {
    uint32_t blue, gold, gold_flags_ok;
} dust_cnt;

static void dust_count(sa1_context *ctx, sa1_character_id id, const dust_ids *di, dust_cnt *c)
{
    const sa1_render *rd = NULL;
    uint32_t k;
    if (sa1_character_render(ctx, id, 1.0f, &rd) != SA1_OK || rd == NULL) {
        return;
    }
    for (k = 0; k < rd->num_draws; k++) {
        const sa1_draw *d = &rd->draws[k];
        uint32_t f = d->flags & 0x0FFFu;
        if (d->prim != SA1_PRIM_TRIANGLES) {
            continue;
        }
        if (d->archive == di->a_eff && (d->texture == di->t_a || d->texture == di->t_b)) {
            c->blue++;
        } else if (d->archive == di->a_ss && (d->texture == di->s_a || d->texture == di->s_b) && (f == 0x271u || f == 0x371u)) {
            c->gold++;
            c->gold_flags_ok++;
        }
    }
}

/* the spin dash charge (20 pairs) and the Light Speed charge (100 pairs) with the shoes; the dust counted while Super and
 * charging (mode 4: the roll's ball after-images 8C0B3FC0 share s_ball_a / _b and are left out) */
static uint32_t dust_script(sa1_context *ctx, sa1_character_id id, const dust_ids *di, dust_cnt *c, uint32_t *rnd, int n)
{
    sa1_state_ex x;
    int i;
    uint32_t sup = 0;
    for (i = 0; i < n; i++) {
        uint32_t b = (i >= 10 && i < 30) || (i >= 60 && i < 160) ? SA1_BUTTON_ACTION : 0u;
        tick(ctx, id, 0.0f, 0.0f, b, NULL, &x);
        if (rnd != NULL) {
            rnd[i] = x.rand_state;
        }
        if ((x.equipment & 0x8000u) && x.mode == 4) {
            sup++;
            dust_count(ctx, id, di, c);
        }
    }
    return sup;
}

static void t_general_effects_dust(sa1_context *ctx)
{
    static const float at[3] = { -2000.0f, 0.5f, 0.0f };
    static const int32_t vals[3] = { 1, 2, 3 };
    dust_ids di;
    dust_cnt c[3], cn;
    uint32_t r1[200], r3[200], trail[3];
    int v, i, same = 1;
    sa1_status s4, sk8;
    sa1_character_id id, k8;
    printf("general_effects_dust (GRID): SA1_SUPOPT_SUPER_EFFECTS bits - the dust takes the Super after-images' gold (M1)\n");
    di.a_eff = archive_named(ctx, SA1_CHAR_SONIC, "SON_EFF");
    di.a_ss = archive_named(ctx, SA1_CHAR_SONIC, "SUPERSONIC");
    di.t_a = tex_named(ctx, di.a_eff, "s_ball_a");
    di.t_b = tex_named(ctx, di.a_eff, "s_ball_b");
    di.s_a = tex_named(ctx, di.a_ss, "ss_ball_a");
    di.s_b = tex_named(ctx, di.a_ss, "ss_ball_b");
    if (scene_grid(ctx, 0) != 0 || di.t_a < 0 || di.t_b < 0 || di.s_a < 0 || di.s_b < 0) {
        CHECK(0, "the dust's textures by name: SON_EFF %d s_ball_a %d _b %d, SUPERSONIC %d ss_ball_a %d _b %d", (int)di.a_eff,
              (int)di.t_a, (int)di.t_b, (int)di.a_ss, (int)di.s_a, (int)di.s_b);
        return;
    }
    memset(c, 0, sizeof c);
    for (v = 0; v < 3; v++) {
        sa1g_world *w;
        if ((id = mk_gen(ctx, at, 50, SA1_SUPER_START | SA1_SUPER_NO_DRAIN, NULL)) == 0) {
            g_fail++;
            return;
        }
        give_upgrades(ctx, id);
        sa1_character_set_super_option(ctx, id, SA1_SUPOPT_SUPER_EFFECTS, vals[v]);
        w = sa1_character_world(ctx, id);
        dust_script(ctx, id, &di, &c[v], vals[v] == 1 ? r1 : (vals[v] == 3 ? r3 : NULL), 200);
        trail[v] = w->sg.effect_clones;
        sa1_character_destroy(ctx, id);
    }
    printf("  info  dust draws while Super (blue s_ball / gold ss_ball): 1 %u / %u, 2 %u / %u, 3 %u / %u; clone first "
           "execs %u / %u / %u\n", (unsigned)c[0].blue, (unsigned)c[0].gold, (unsigned)c[1].blue, (unsigned)c[1].gold,
           (unsigned)c[2].blue, (unsigned)c[2].gold, (unsigned)trail[0], (unsigned)trail[1], (unsigned)trail[2]);
    CHECK(c[2].blue == 0 && c[2].gold > 20u && c[2].gold_flags_ok == c[2].gold,
          "3 (CLONES | DUST): no SON_EFF s_ball draw while Super, %u dust draws SUPERSONIC ss_ball_a / _b with the flags "
          "0x271 / 0x371 (the first draw of each dust included: the set-up picks the texlist)", (unsigned)c[2].gold);
    CHECK(c[0].blue > 20u && trail[0] > 0, "1 (CLONES, 0.9.3a): the blue dust (%u draws), the clones run (%u)",
          (unsigned)c[0].blue, (unsigned)trail[0]);
    CHECK(c[1].blue == 0 && c[1].gold > 20u && trail[1] == 0, "2 (DUST): the gold dust (%u draws), Sonic's own trail and "
          "after-images (no clone)", (unsigned)c[1].gold);
    for (i = 0; i < 200; i++) {
        same &= r1[i] == r3[i];
    }
    CHECK(same, "the rand stream of the script equal with 1 and 3 (the block swaps only the pointer)");
    /* normal Sonic after the revert, with 3 */
    if ((id = mk_gen(ctx, at, 50, SA1_SUPER_START | SA1_SUPER_NO_DRAIN, NULL)) == 0) {
        g_fail++;
        return;
    }
    give_upgrades(ctx, id);
    sa1_character_set_super_option(ctx, id, SA1_SUPOPT_SUPER_EFFECTS, 3);
    run(ctx, id, 4, 0.0f, 0.0f, NULL, NULL);
    sa1_character_order(ctx, id, SA1_ORDER_NORMAL);
    memset(&cn, 0, sizeof cn);
    {
        sa1_state_ex x;
        for (i = 0; i < 30; i++) {
            tick(ctx, id, 0.0f, 0.0f, (i >= 5 && i < 25) ? SA1_BUTTON_ACTION : 0u, NULL, &x);
            if (!(x.equipment & 0x8000u) && x.mode == 4) {
                dust_count(ctx, id, &di, &cn);
            }
        }
    }
    CHECK(cn.blue > 5u && cn.gold == 0, "normal Sonic after the revert with 3: the blue dust (%u draws, %u gold)",
          (unsigned)cn.blue, (unsigned)cn.gold);
    s4 = sa1_character_set_super_option(ctx, id, SA1_SUPOPT_SUPER_EFFECTS, 4);
    sa1_character_destroy(ctx, id);
    k8 = mk(ctx, SA1_CHAR_SUPER_SONIC, at, 0, -1, 1, SA1_SUPER_NO_DRAIN, NULL);
    sk8 = k8 ? sa1_character_set_super_option(ctx, k8, SA1_SUPOPT_SUPER_EFFECTS, 3) : SA1_OK;
    if (k8) {
        sa1_character_destroy(ctx, k8);
    }
    CHECK(s4 == SA1_ERR_INVALID_ARGUMENT && sk8 == SA1_ERR_UNSUPPORTED, "4: %d (SA1_ERR_INVALID_ARGUMENT); kind 8: %d "
          "(SA1_ERR_UNSUPPORTED)", (int)s4, (int)sk8);
}

/* (17) the internal contract's sa1w_world_lsd_gold */
static void t_supopt_contract(sa1_context *ctx)
{
    static const float at[3] = { -2000.0f, 0.5f, 0.0f };
    sa1_character_id id, k8, son;
    sa1g_world *w;
    sa1_state_ex x;
    int before, off, on, after_order, next_pair, v_k8, v_son;
    printf("supopt_contract (GRID): sa1w_world_lsd_gold (super2_plan.md 5.4)\n");
    if (scene_grid(ctx, 0) != 0 || (id = mk_gen(ctx, at, 50, SA1_SUPER_START | SA1_SUPER_NO_DRAIN, NULL)) == 0) {
        g_fail++;
        return;
    }
    w = sa1_character_world(ctx, id);
    before = sa1w_world_lsd_gold(w);
    run(ctx, id, 6, 0.0f, 0.0f, NULL, &x);
    off = sa1w_world_lsd_gold(w);
    sa1_character_set_super_option(ctx, id, SA1_SUPOPT_LSD_GOLD, 1);
    tick(ctx, id, 0.0f, 0.0f, 0, NULL, &x);
    on = sa1w_world_lsd_gold(w);
    sa1_character_order(ctx, id, SA1_ORDER_NORMAL);
    after_order = sa1w_world_lsd_gold(w);
    tick(ctx, id, 0.0f, 0.0f, 0, NULL, &x);
    next_pair = sa1w_world_lsd_gold(w);
    k8 = mk(ctx, SA1_CHAR_SUPER_SONIC, at, 0, -1, 1, SA1_SUPER_NO_DRAIN, NULL);
    son = mk(ctx, SA1_CHAR_SONIC, at, 0, -1, 0, 0, NULL);
    run(ctx, k8, 4, 0.0f, 0.0f, NULL, NULL);
    run(ctx, son, 4, 0.0f, 0.0f, NULL, NULL);
    v_k8 = sa1w_world_lsd_gold(sa1_character_world(ctx, k8));
    v_son = sa1w_world_lsd_gold(sa1_character_world(ctx, son));
    CHECK(before == 0 && off == 0 && on == 1 && after_order == 1 && next_pair == 0 && v_k8 == 0 && v_son == 0 &&
              sa1w_world_lsd_gold(NULL) == 0,
          "before the first pair %d, option 0 %d, option 1 after a Super pair %d, right after SA1_ORDER_NORMAL %d (the last "
          "pair was drawn Super), the pair after the revert %d; kind 8 %d, plain Sonic %d, NULL %d", before, off, on,
          after_order, next_pair, v_k8, v_son, sa1w_world_lsd_gold(NULL));
    sa1_character_destroy(ctx, id);
    sa1_character_destroy(ctx, k8);
    sa1_character_destroy(ctx, son);
}

/* (18) memory: toggling the options through 10 transformations, then the character's destruction (SA1_GUARD_ALLOC=1) */
static void t_general_anims_memcheck(sa1_context *unused)
{
    static const float at[3] = { 0.0f, 0.5f, 0.0f };
    sa1_context *c = NULL;
    sa1_host h;
    sa1_coords co;
    sa1_load_options lo;
    sa1_character_id id;
    sa1g_world *w;
    ga_stats s0, s1;
    uint32_t f0 = 0, f1 = 0, fa = 0, lg = 0;
    int i;
    (void)unused;
    printf("general_anims_memcheck (GRID): SUPER_ANIMS / SUPER_EFFECTS 3 through 10 transformations; the destruction\n");
    ga_stats_get(&s0);
    sa1_host_defaults(&h);
    h.log = log_cb;
    h.log_level = SA1_LOG_WARN;
    if (sa1_init(&h, &c) != SA1_OK) {
        g_fail++;
        return;
    }
    memset(&co, 0, sizeof co);
    co.struct_size = sizeof co;
    sa1_coords_preset(SA1_COORDS_PRESET_NATIVE, &co);
    sa1_set_coords(c, &co);
    memset(&lo, 0, sizeof lo);
    lo.struct_size = sizeof lo;
    lo.characters = 1u << SA1_CHAR_SONIC;
    lo.flags = SA1_LOAD_DEFAULT | SA1_LOAD_SUPER_FORMS;
    if (SA1_FAILED(sa1_load_disc(c, g_gdi, &lo)) || scene_grid(c, 0) != 0 ||
        (id = mk_gen(c, at, 50, SA1_SUPER_NO_DRAIN, NULL)) == 0) {
        sa1_shutdown(c);
        scene_forget();     /* g_scene named this context */
        g_fail++;
        return;
    }
    w = sa1_character_world(c, id);
    run(c, id, 4, 0.0f, 0.0f, NULL, NULL);
    sa1ho_world_heap(w, &f0, &lg);
    for (i = 0; i < 10; i++) {
        sa1_character_set_super_option(c, id, SA1_SUPOPT_SUPER_ANIMS, i & 1);
        sa1_character_set_super_option(c, id, SA1_SUPOPT_SUPER_EFFECTS, (i & 2) ? 3 : 0);
        sa1_character_order(c, id, SA1_ORDER_SUPER);
        run(c, id, 6, 0.0f, 1.0f, NULL, NULL);
        sa1_character_set_super_option(c, id, SA1_SUPOPT_SUPER_ANIMS, !(i & 1));
        run(c, id, 4, 0.0f, 0.0f, NULL, NULL);
        sa1_character_order(c, id, SA1_ORDER_NORMAL);
        run(c, id, 6, 0.0f, 0.0f, NULL, NULL);
        if (i == 0) {
            sa1ho_world_heap(w, &fa, &lg);  /* after the first cycle: the game's own one-time tasks of the first run are gone */
        }
    }
    sa1ho_world_heap(w, &f1, &lg);
    /* the first cycle may free more than it takes (measured: 379648 -> 381136 free, a start-up task of the first standing
       pairs ends), so the comparison is cycle 1 against cycle 10, and never below the start */
    CHECK(f1 == fa && f1 >= f0 && !w->dead && w->sg.transforms == 10u, "the game heap's free bytes %u after the first "
          "transformation and revert, %u after the tenth (the start: %u), the options toggled (%u transformations), the "
          "world alive", (unsigned)fa, (unsigned)f1, (unsigned)f0, (unsigned)w->sg.transforms);
    sa1_character_destroy(c, id);
    sa1_shutdown(c);
    scene_forget();     /* g_scene named this context */
    ga_stats_get(&s1);
    if (ga_enabled()) {
        CHECK(s1.live == s0.live && s1.damaged == 0, "the guard heap: %llu live blocks before the case's context, %llu after "
              "its shutdown (0 left), %u damaged (no double free)", (unsigned long long)s0.live,
              (unsigned long long)s1.live, (unsigned)s1.damaged);
    } else {
        printf("  info  the guard allocator is off (run with SA1_GUARD_ALLOC=1 for the host heap check)\n");
    }
}

/* (19) the lanes' contract (faces_wheel_plan.md 5.4, review R.12) */
static void t_lane_contract(sa1_context *ctx)
{
    static const float at[3] = { -2000.0f, 0.5f, 0.0f };
    sa1_character_id gen, son, k8, bare;
    sa1g_world *wg, *ws;
    sa1_state_ex x, y;
    uint32_t a, lifted[5], k, ok_lift = 1;
    unsigned char buf[sizeof(sa1_state_ex)];
    size_t old_size = offsetof(sa1_state_ex, face_mode), i;
    int hooks_null, none_fresh, none_tick, partial_ok = 1;
    printf("lane_contract (GRID): the face / emote lanes' hooks, sa1w_world_super_lifted, sa1_state_ex's new fields\n");
    if (scene_grid(ctx, 0) != 0 || (gen = mk_anims(ctx, at, 1, 0)) == 0 || (son = mk(ctx, SA1_CHAR_SONIC, at, 0, -1, 0, 0,
                                                                                         NULL)) == 0) {
        g_fail++;
        return;
    }
    wg = sa1_character_world(ctx, gen);
    ws = sa1_character_world(ctx, son);
    memset(&x, 0, sizeof x);
    x.struct_size = sizeof x;
    sa1_character_state_ex(ctx, gen, &x);
    none_fresh = x.face_from == -1 && x.face_to == -1 && x.face_letter == -1 && x.emote_item == -1 && x.emote_row == -1 &&
                 x.emote_extra == -1 && x.face_mode == 0 && x.face_shown == 0 && x.face_rate == 0.0f && x.face_activity == 0 &&
                 x.emote_state == 0 && x.emote_frame == 0.0f;
    a = sa1w_world_super_lifted(wg, 130);
    run(ctx, gen, 60, 0.0f, 1.0f, NULL, &x);
    run(ctx, son, 60, 0.0f, 1.0f, NULL, &y);
    none_tick = x.face_from == -1 && x.face_to == -1 && x.face_letter == -1 && x.emote_item == -1 && x.emote_row == -1 &&
                x.emote_extra == -1 && x.face_mode == 0 && x.face_shown == 0 && x.face_activity == 0 && x.emote_state == 0 &&
                y.face_from == -1 && y.emote_item == -1 && y.emote_extra == -1 && y.face_letter == -1;
    hooks_null = wg->face.pre == NULL && wg->face.post == NULL && wg->face.fill == NULL && wg->emote.pre == NULL &&
                 wg->emote.post == NULL && wg->emote.fill == NULL && wg->face.state == 0 && wg->emote.state == 0 &&
                 ws->face.pre == NULL && ws->emote.pre == NULL && ws->face.fill == NULL && ws->emote.fill == NULL;
    CHECK(hooks_null && !wg->dead && !ws->dead, "a General world and a plain Sonic world without face / emote calls: every "
          "lane hook NULL, both ran 60 pairs");
    for (k = 0; k < 5u; k++) {
        lifted[k] = sa1w_world_super_lifted(wg, 130u + k);
        ok_lift = ok_lift && lifted[k] != 0 && (x32(wg, lifted[k]) & 0x1FFFFFFFu) == (O_SUPER_OBJECT & 0x1FFFFFFFu) &&
                  x32(wg, lifted[k] + 4u) == wg->sg.ga_mot[k == 4u ? 3u : k];
    }
    {
        /* the copies' node-0 position keys: the original's + L (130: keys; 131: one per frame) */
        uint32_t c = x32(wg, lifted[0] + 4u), md = x32(wg, c), p0 = x32(wg, md), omd = rd32(wg, k_ss_mot[0]);
        float yk, y0 = rdf(wg, rd32(wg, omd) + 8u);
        uint32_t v = x32(wg, p0 + 8u);
        memcpy(&yk, &v, 4);
        ok_lift = ok_lift && fabsf(yk - (y0 + wg->sg.lift)) <= 1e-5f;
    }
    CHECK(a == 0 && ok_lift && lifted[3] == lifted[4] && sa1w_world_super_lifted(wg, 129) == 0 &&
              sa1w_world_super_lifted(wg, 135) == 0 && sa1w_world_super_lifted(ws, 130) == 0 &&
              sa1w_world_super_lifted(NULL, 130) == 0,
          "sa1w_world_super_lifted: 0 before the first pair; after it 130-134 the NJS_ACTIONs {the Super body, the lifted "
          "copy} (133 = 134), the keys the original's + L; 0 for 129, 135, plain Sonic, NULL");
    k8 = mk(ctx, SA1_CHAR_SUPER_SONIC, at, 0, -1, 1, SA1_SUPER_NO_DRAIN, NULL);
    run(ctx, k8, 3, 0.0f, 0.0f, NULL, NULL);
    CHECK(k8 && sa1w_world_super_lifted(sa1_character_world(ctx, k8), 130) == 0, "kind 8: 0");
    if (k8) {
        sa1_character_destroy(ctx, k8);
    }
    bare = 0;
    if (sa1_character_create(ctx, SA1_CHAR_SONIC, at, 0.0f, &bare) == SA1_OK) {
        memset(&y, 0, sizeof y);
        y.struct_size = sizeof y;
        sa1_character_state_ex(ctx, bare, &y);
        none_fresh = none_fresh && y.face_from == -1 && y.face_letter == -1 && y.emote_item == -1 && y.emote_row == -1 &&
                     y.emote_extra == -1 && y.face_mode == 0;
        sa1_character_destroy(ctx, bare);
    }
    CHECK(none_fresh && none_tick, "sa1_state_ex of a fresh character (with and without a world) and of ticking ones: "
          "face_from / face_to / face_letter / emote_item / emote_row / emote_extra -1, the other new fields 0");
    memset(buf, 0xAB, sizeof buf);
    {
        uint32_t sz = (uint32_t)old_size;
        memcpy(buf, &sz, sizeof sz);
    }
    sa1_character_state_ex(ctx, gen, (sa1_state_ex *)(void *)buf);
    for (i = old_size; i < sizeof buf; i++) {
        partial_ok = partial_ok && buf[i] == 0xABu;
    }
    CHECK(partial_ok && ((sa1_state_ex *)(void *)buf)->step != 0, "an sa1_state_ex of the 0.9.4 size (%u bytes): filled up to "
          "that size only (the %u bytes after it untouched)", (unsigned)old_size, (unsigned)(sizeof buf - old_size));
    sa1_character_destroy(ctx, gen);
    sa1_character_destroy(ctx, son);
}

int main(int argc, char **argv)
{
    static const struct { const char *name; void (*fn)(sa1_context *); } tests[] = {
        { "boot", t_boot }, { "render", t_render }, { "drain", t_drain }, { "keep", t_keep }, { "whoosh", t_whoosh },
        { "water", t_water }, { "transform", t_transform }, { "invalid", t_invalid }, { "determinism", t_determinism },
        { "disc", t_disc }, { "invalid_disc", t_invalid_disc }, { "forms", t_forms }, { "kind8_trace", t_kind8_trace },
        /* round 0.9.3a: the General form */
        { "general_boot", t_general_boot }, { "general_welds", t_general_welds },
        { "general_transform_air", t_general_transform_air }, { "general_look", t_general_look },
        { "general_moves", t_general_moves }, { "general_speed", t_general_speed }, { "general_revert", t_general_revert },
        { "general_reorder", t_general_reorder },
        { "general_keep", t_general_keep }, { "general_start", t_general_start }, { "general_link", t_general_link },
        { "general_cpu_pose", t_general_cpu_pose }, { "general_bitguard", t_general_bitguard },
        { "general_aura", t_general_aura }, { "general_effects", t_general_effects }, { "aura_anim", t_aura_anim },
        { "idle_by_form", t_idle_by_form }, { "water_while_super", t_water_while_super }, { "held_drawn", t_held_drawn },
        { "kind8_extra", t_kind8_extra }, { "general_memcheck", t_general_memcheck },
        /* round 0.9.4a: the General form's own poses, the golden dust, the contracts (super2_plan.md 6.1) */
        { "general_anims_table", t_general_anims_table }, { "general_anims_stand", t_general_anims_stand },
        { "general_anims_bands", t_general_anims_bands }, { "general_anims_identity", t_general_anims_identity },
        { "general_anims_kept", t_general_anims_kept }, { "general_anims_idle", t_general_anims_idle },
        { "general_anims_switch", t_general_anims_switch }, { "general_anims_welds_carry", t_general_anims_welds_carry },
        { "general_anims_cpu_pose", t_general_anims_cpu_pose }, { "general_anims_hint", t_general_anims_hint },
        { "general_anims_extreme", t_general_anims_extreme },
        { "general_anims_revert", t_general_anims_revert }, { "general_anims_rebuild", t_general_anims_rebuild },
        { "general_anims_kind8", t_general_anims_kind8 }, { "general_anims_slope", t_general_anims_slope },
        { "general_anims_lsd_aura", t_general_anims_lsd_aura }, { "general_effects_dust", t_general_effects_dust },
        { "supopt_contract", t_supopt_contract }, { "general_anims_memcheck", t_general_anims_memcheck },
        { "lane_contract", t_lane_contract },
    };
    const char *only = NULL, *from = NULL;   /* --from NAME: that case and every later one (a run of the tail) */
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
        } else if (!strcmp(argv[a], "--only") && a + 1 < argc) {
            only = argv[++a];
        } else if (!strcmp(argv[a], "--from") && a + 1 < argc) {
            from = argv[++a];
        } else {
            g_gdi = argv[a];
        }
    }
    if (g_gdi == NULL) {
        g_gdi = getenv("SA1_TEST_GDI");
    }
    if (g_gdi == NULL) {
        printf("usage: super_test [--only NAME] [--verbose] GDI\n");
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
    lo.characters = (1u << SA1_CHAR_SONIC) | (1u << SA1_CHAR_SUPER_SONIC) | (1u << SA1_CHAR_TAILS);
    lo.flags = SA1_LOAD_DEFAULT | SA1_LOAD_SUPER_FORMS;
    st = sa1_load_disc(ctx, g_gdi, &lo);
    if (SA1_FAILED(st)) {
        printf("sa1_load_disc: %s: %s\n", sa1_status_string(st), sa1_last_error(ctx));
        sa1_shutdown(ctx);
        return 1;
    }
    printf("libsa1 %s (SA1_API_SUPER %d)\n", sa1_version_string(), SA1_API_SUPER);
    for (i = 0; i < sizeof tests / sizeof tests[0]; i++) {
        if (only != NULL && strcmp(only, tests[i].name) != 0) {
            continue;
        }
        if (from != NULL) {
            if (strcmp(from, tests[i].name) != 0) {
                continue;
            }
            from = NULL;        /* this case and every later one */
        }
        if (strcmp(tests[i].name, "general_anims_table") == 0) {
            g_scene_cache = 1;  /* round 0.9.4a: from the first 0.9.4a case on (g_scene) */
        }
        tests[i].fn(ctx);
    }
    sa1_shutdown(ctx);
    printf("super_test: %d ok, %d failed\n", g_ok, g_fail);
    return g_fail ? 1 : 0;
}
