/*
 * grid_test.c - GRID collision through the public API (sadc docs/libsa1_integration.md 5.4-5.8, the Phase 6 exit
 * "synthetic suite") and GRID on Emerald Coast act 1 against the reference traces.
 *
 *   grid_test [--only NAME]... [--no-ec1] [--verbose] SADC [GDI]       (GDI: or $SA1_TEST_GDI)
 *
 * Every synthetic test is one host scene: triangles through sa1_collision_set_static (the production path for
 * hosts with a triangle soup) or moving objects through sa1_collision_object_add / move / remove, Sonic created
 * through sa1_character_create_ex and driven by sa1_input through sa1_character_tick (whole 1/30 s pairs), the
 * results read from sa1_character_state_ex and sa1_poll_events. The context uses SA1_COORDS_PRESET_NATIVE, so
 * host space is SA1 space (y up; a host yaw t faces (cos t, 0, -sin t)). Internal reads (tests only): the
 * adapter's per-world counters (sa1ca_world_stats_get) and motionwk.weight for the surface kinds.
 *
 *   api        set_static / set_static_ex GRID descriptions, parameters, limits, info, rotation -> BAMS
 *   flat       land on a plane, stand, run along the camera yaw, jump and land again
 *   ramp       run up a 30 degree ramp: on the ground, on the surface, the floor normal
 *   stairs     one step of height h for h = 1 .. 20: the highest step Sonic runs up without jumping (step-up
 *              height, reported); a STAIRS-flagged ramp laid over steps he cannot climb
 *   wall       run into a 90 degree wall: stopped in front of it
 *   water      fall into a closed water box: water physics, the breath timer, the floor below
 *   moving     stand on a platform that moves horizontally and vertically: carried with it
 *   rotating   stand off-centre on a platform turning about the vertical: carried around, facing turned
 *   kill       fall through a kill plane: the game's death sequence (scream, restart request)
 *   surfaces   slippery / hurt / stairs floors: the game's reaction to the COL bits
 *   capacity   a dense floor (window polygon budget) and more moving objects than a world holds
 *   stacked    a floor above a floor (gaps 64 / 128 / 256), thin roofs (8 .. 0.5 units and a single plane), a ramp
 *              up to the upper floor, the upper floor's top and edges on cell boundaries; land on it, stand, run
 *              across it (jump on the way) and off its far edge onto the lower floor, in the default GRID and with
 *              SA1_GRID_CUT at 256- and 128-unit cells: never below the upper floor while over it, the lower floor
 *              only after the edge, the NEAR-poly list (8C028BFE, 96 entries) never full (floors thinner than 2
 *              units: known, their underside pulls Sonic down now and then). Then the failure G-SA1
 *              showed on gm_construct (2026-10-20, "on top of the garage you fall into the floor of it"): a roof
 *              built of stacked slabs, the hidden faces between them sent as land; the extra polygons around the
 *              character fill the NEAR-poly list, the rest (the roof's top among them) is never tested and Sonic
 *              drops through (a game limit: hosts must send the visible surfaces only; reported as known)
 *   needle     gm_freespace_13's big ramp rebuilt (a 1328 x 59 x 3 plank at 24.5 degrees, each face 2 long triangles):
 *              Sonic, Tails, Knuckles run up it (run, dash panels 6 / 10 / 16, spin dash) and are created above it, and
 *              never fall through; the NEAR-poly list never fills (sadc docs/research/gsa1_ramp_freeze.md: longest-edge
 *              halving made the faces into needles that filled the 96-entry list, fix 1: well-shaped GRID cuts)
 *   plank      the same with the layers around it on gm_freespace_13 (rails, beams under the underside, posts): before
 *              the fix 19 of 24 runs fell through; with SA1_GRID_DC_LIMITS (the game's 96-entry list) reported only
 *   underside  plank ramps whose undersides lie within the sphere's reach (ramp_diag slab, the falls of round 4b: Knuckles
 *              45 degrees on slabs 3 / 4 / 6 thick, Sonic 30 degrees on 6, the crest of 0.5 / 1 platforms): with the GRID's
 *              undersides within reach left out (c2, the default) none falls (with SA1_GRID_KEEP_UNDERSIDES, reported);
 *              the cost before round 0.9.3 (now SA1_GRID_UNDERSIDES_DROP): a platform 2 thick is one-way (Sonic jumps up
 *              through it and lands on it); by default its flat underside is front-only and stops him; a slab 20 thick
 *              keeps its underside, SA1_GRID_KEEP_UNDERSIDES keeps every underside
 *   limits     PC-only capacities of host worlds (fixes 3, 4, 10): the NEAR-poly list of 512 (round 0.9.2; 256 before; 96
 *              with SA1_GRID_DC_LIMITS)
 *              and its SA1_CAPACITY_NEAR_POLYGONS reports, the matrix stack restored at each step after 8C029B00's
 *              TOUCH-list overflows (SA1_CAPACITY_MATRIX_STACK), a cell of 8192 triangles (more than the window's polygon
 *              budget) split into objects of at most 1024 so that the window never leaves out the floor under the
 *              character
 *   ec1        GRID on the L01A0 dump triangles (world space, raw COL words, the dump's strip parity): the 5 EC1
 *              reference scripts replayed with their trace injection (world_replay.c) in three GRID set-ups with the
 *              partition of the game's own stages, SA1_GRID_MIXED_ORIENTATIONS (the defaults; SA1_GRID_CUT with 256- and
 *              128-unit cells), and a fourth with the default orientation classes of round 0.9.2 (ec1_stand and
 *              ec1_jump_hold bit-identical, ec1_turns turns differently from step 156: known): the mode / action
 *              sequence must stay the
 *              reference's with nothing left out of the window near the character, ec1_stand and ec1_jump_hold
 *              must be bit-identical with the defaults; position errors and the first bit difference are reported.
 *              Why the others differ (--verbose prints the first difference): the game resolves the touches of
 *              each land object together (8C029B00 / 8C028100: find_count, averaged pushback), and two polygons
 *              of one original COL can lie in two cells (ec1_run_jump step 61: 2 touches in one object in the
 *              reference, 1 + 1 in GRID); cut pieces change the pushback through 8C029B00's frame angles,
 *              quantised to 1/65536 turn around the polygon's first corner (SA1_GRID_CUT: from the landing at
 *              step 17). Afterwards the differences grow without changing a mode or action.
 * Round 0.9.2 (sadc docs/research/gsa1_090_plan.md 6.2; E-102 and Big are loaded too):
 *   hits       24 land objects of one cell whose COL spheres reach E-102's land-collision query (gsa1_090_collision.md C3):
 *              the lowered 8C073160's arrays (192) never fill and their canary stays clean (before 0.9.2: 24 objects into
 *              hits[16], a host crash); round 0.9.3: all 24 are in the window and in the query (0.9.2's bound of 15 left
 *              floors and ceilings in reach out), with SA1_GRID_DC_LIMITS the window still lets at most 15 in
 *              (SA1_CAPACITY_LAND_QUERY)
 *   wallfloor  coll_diag's wall scene at offsets 0 / +-10 / +-100 / +1000 HU from the cell plane (C1 / F2, Review R.2 b,
 *              R.4.2): Knuckles and E-102 into 20 / 24 HU box walls, Big dashing into single double-sided 2-8 HU faces;
 *              none ends inside or past a wall (before: walls shared GRID objects with the floor)
 *   ceiling    a ceiling 16 units over its floor in one cell (Review R.3.4): Sonic and Big do not sink
 *   kinds      SA1_SURFACE_SNOW / SPEED_PATH / FOOTPRINTS reach the game as COL 0x8 / 0x800000 / 0x100000 and come back in
 *              sa1_state.surface; Sonic on snow reaches 0.35 after 45 pairs (gsa1_090_surfaces.md F1)
 *   huge       GRID-CAP (gsa1_090_water.md): 2.6 million small triangles still prepare, 2.9 million are refused by the
 *              counting pass within a second, with the size needed in the message
 * Round 0.9.3 (sadc docs/research/gsa1_091b_ceiling.md FIX-C1 / C2 and gsa1_091a_loop.md LOOP-L1, with their Reviews; Amy is
 * loaded too):
 *   jumpceiling  thin roofs (12 .. 3 thick over 32 units of room) keep their flat undersides as front-only polygons: no kind
 *              jumps through them (standing, twice, running at 3 / 6 / 9); with SA1_GRID_UNDERSIDES_DROP they are one-way
 *              again (and a host's FRONT_ONLY triangles are left out); host-marked FRONT_ONLY ceilings; roofs 0.5 / 1 / 2
 *              thick (residual 1: the thin top is not touched from more than a radius behind by a rising sphere) and
 *              landings on them from above; gm_bigcity's house as data (77 spots, every kind at 12, the ceiling unmarked
 *              and host-marked); a moving slab with a FRONT_ONLY underside (jump from below, landing on top)
 *   loop       the synthetic loop of the loop doc (R 512 HU, 32 thick, 16 segments) at dash-panel speeds: with its inner
 *              faces SA1_SURFACE_CURVED GRID keeps its ceiling (12 curved pieces) and Sonic completes it at 9 / 10 / 12 / 16
 *              and falls back inside at 6 / 7; unflagged with SA1_GRID_UNDERSIDES_DROP (round 0.9.2) he ends on top
 */
#include "world_replay.c"

#include <math.h>

#include "sa1_api_world.h"
#include "sa1_coladapt.h"

#define PI 3.14159265358979323846
/* camera yaw that makes a forward stick run along +x: the game's analog conversion (8C05117E) turns a full
 * forward stick about 5.9 degrees off the camera direction (its dead-zone bias, kept by libsa1) */
#define YAW_X ((float)(-5.9 * PI / 180.0))

static int g_fail, g_known;
static long g_allocs, g_frees;
static int g_verbose;

#define CHECK(cond, ...)                                                       \
    do {                                                                       \
        if (cond) {                                                            \
            printf("  ok    ");                                                \
        } else {                                                               \
            printf("  FAIL  ");                                                \
            g_fail++;                                                          \
        }                                                                      \
        printf(__VA_ARGS__);                                                   \
        printf("\n");                                                          \
    } while (0)

#define INFO(...)                                                              \
    do {                                                                       \
        printf("  info  ");                                                    \
        printf(__VA_ARGS__);                                                   \
        printf("\n");                                                          \
    } while (0)

static void log_cb(void *user, int32_t level, const char *msg)
{
    (void)user;
    if (level >= SA1_LOG_WARN || g_verbose) {
        printf("    [libsa1 %s] %s\n", level >= SA1_LOG_ERROR ? "error" : level >= SA1_LOG_WARN ? "warn" : "debug", msg);
    }
}

static void *alloc_cb(void *user, size_t n)
{
    (void)user;
    g_allocs++;
    return ga_malloc(n);        /* tests/guard_alloc.h: malloc unless SA1_GUARD_ALLOC / --guard-alloc */
}

static void free_cb(void *user, void *p)
{
    (void)user;
    if (p != NULL) {
        g_frees++;
    }
    ga_free(p);
}

static sa1_context *make_ctx(const char *gdi)
{
    sa1_context *ctx = NULL;
    sa1_host h;
    sa1_load_options lo;
    sa1_coords co;
    sa1_status st;
    sa1_host_defaults(&h);
    h.log = log_cb;
    h.log_level = g_verbose ? SA1_LOG_DEBUG : SA1_LOG_WARN;
    h.alloc = alloc_cb;
    h.free = free_cb;
    if (sa1_init(&h, &ctx) != SA1_OK) {
        return NULL;
    }
    memset(&co, 0, sizeof co);
    co.struct_size = sizeof co;
    sa1_coords_preset(SA1_COORDS_PRESET_NATIVE, &co);
    sa1_set_coords(ctx, &co);
    memset(&lo, 0, sizeof lo);
    lo.struct_size = sizeof lo;
    lo.characters = (1u << SA1_CHAR_SONIC) | (1u << SA1_CHAR_TAILS) | (1u << SA1_CHAR_KNUCKLES) | (1u << SA1_CHAR_GAMMA)
                    | (1u << SA1_CHAR_BIG)   /* E-102 and Big: round 0.9.2's hits / wallfloor / ceiling */
                    | (1u << SA1_CHAR_AMY);  /* round 0.9.3: jumpceiling (all six kinds) */
    lo.flags = SA1_LOAD_DEFAULT;
    st = sa1_load_disc(ctx, gdi, &lo);
    if (SA1_FAILED(st)) {
        printf("    sa1_load_disc: %s: %s\n", sa1_status_string(st), sa1_last_error(ctx));
        sa1_shutdown(ctx);
        return NULL;
    }
    return ctx;
}

/* ---- scenes ----------------------------------------------------------------------------------------- */
typedef struct tbuf {
    sa1_collision_tri *t;
    uint32_t n, cap;
} tbuf;

static void tb_free(tbuf *b)
{
    free(b->t);
    memset(b, 0, sizeof *b);
}

static void tb_tri(tbuf *b, const float a[3], const float c[3], const float d[3], uint32_t s)
{
    sa1_collision_tri *t;
    if (b->n == b->cap) {
        b->cap = b->cap ? 2u * b->cap : 64u;
        b->t = (sa1_collision_tri *)realloc(b->t, (size_t)b->cap * sizeof *b->t);
    }
    t = &b->t[b->n++];
    memcpy(t->v[0], a, 12);
    memcpy(t->v[1], c, 12);
    memcpy(t->v[2], d, 12);
    t->surface = s;
}

/* a planar quad p0 p1 p2 p3 (in order around it) whose front faces along n */
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

/* a horizontal rectangle at height y facing up (+1) or down (-1) */
static void tb_floor(tbuf *b, float x0, float x1, float z0, float z1, float y, float up, uint32_t s)
{
    float p[4][3] = { { x0, y, z0 }, { x1, y, z0 }, { x1, y, z1 }, { x0, y, z1 } };
    float n[3] = { 0.0f, up, 0.0f };
    tb_quad(b, p, n, s);
}

/* a vertical rectangle in the plane x = x, facing nx (+1 / -1) */
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

/* an axis-aligned box with outward faces */
static void tb_box(tbuf *b, float x0, float x1, float y0, float y1, float z0, float z1, uint32_t s)
{
    tb_floor(b, x0, x1, z0, z1, y1, 1.0f, s);
    tb_floor(b, x0, x1, z0, z1, y0, -1.0f, s);
    tb_wall_x(b, x0, y0, y1, z0, z1, -1.0f, s);
    tb_wall_x(b, x1, y0, y1, z0, z1, 1.0f, s);
    tb_wall_z(b, z0, y0, y1, x0, x1, -1.0f, s);
    tb_wall_z(b, z1, y0, y1, x0, x1, 1.0f, s);
}

/* ---- Sonic ------------------------------------------------------------------------------------------ */
static sa1_status sonic_at(sa1_context *ctx, float x, float y, float z, int32_t angy, sa1_character_id *id)
{
    sa1_character_desc d;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.kind = SA1_CHAR_SONIC;
    d.flags = SA1_CREATE_WORLD | SA1_CREATE_SA1_SPACE | SA1_CREATE_ANGLES;
    d.position[0] = x;
    d.position[1] = y;
    d.position[2] = z;
    d.angle[1] = angy;
    return sa1_character_create_ex(ctx, &d, id);
}

typedef struct run {
    sa1_context *ctx;
    sa1_character_id id;
    sa1_state_ex x;
    sa1_status st;
    uint32_t pairs;
    /* mode / action transitions */
    char log[1024];
    size_t loglen;
    int32_t last_mode, last_action;
} run;

static void run_init(run *r, sa1_context *ctx, sa1_character_id id)
{
    memset(r, 0, sizeof *r);
    r->ctx = ctx;
    r->id = id;
    r->last_mode = -1;
    r->last_action = -1;
    r->x.struct_size = sizeof r->x;
    sa1_character_state_ex(ctx, id, &r->x);
}

/* one pair with the stick (x, y), camera yaw and buttons */
static sa1_status pair(run *r, float sx, float sy, float yaw, uint32_t buttons)
{
    sa1_input in;
    memset(&in, 0, sizeof in);
    in.struct_size = sizeof in;
    in.stick_x = sx;
    in.stick_y = sy;
    in.camera_yaw = yaw;
    in.buttons = buttons;
    r->st = sa1_character_tick(r->ctx, r->id, &in, 1.0f / 30.0f, NULL, NULL);
    r->x.struct_size = sizeof r->x;
    sa1_character_state_ex(r->ctx, r->id, &r->x);
    r->pairs++;
    if (r->x.mode != r->last_mode || r->x.action != r->last_action) {
        if (r->loglen + 24 < sizeof r->log) {
            r->loglen += (size_t)snprintf(r->log + r->loglen, sizeof r->log - r->loglen, "%s%d/%d@%u",
                                          r->loglen ? " " : "", (int)r->x.mode, (int)r->x.action,
                                          (unsigned)r->x.step);
        }
        r->last_mode = r->x.mode;
        r->last_action = r->x.action;
    }
    if (g_verbose) {
        printf("      pair %3u step %4u mode %2d/%2d act %3d pos (%9.3f %9.3f %9.3f) gnd %d attr %08X pspd %.3f\n",
               (unsigned)r->pairs, (unsigned)r->x.step, (int)r->x.mode, (int)r->x.smode, (int)r->x.action,
               r->x.sa1_position[0], r->x.sa1_position[1], r->x.sa1_position[2],
               (r->x.flags & SA1_STATE_ON_GROUND) != 0, (unsigned)r->x.attr, r->x.player_speed[0]);
    }
    return r->st;
}

static int grounded(const run *r)
{
    return (r->x.flags & SA1_STATE_ON_GROUND) != 0;
}

static sa1g_world *world_of(const run *r)
{
    return sa1_character_world(r->ctx, r->id);
}

static void wstats(const run *r, sa1ca_world_stats *ws)
{
    sa1ca_world_stats_get(world_of(r), ws);
}

/* motionwk.weight of player 0 (the surface's acceleration factor, 8C075214) */
static float player_weight(const run *r)
{
    sa1g_world *w = world_of(r);
    sa1g_player_ptrs pp;
    float f = 0.0f;
    if (w == NULL) {
        return -1.0f;
    }
    sa1g_world_player(w, &pp);
    if (pp.mwp == 0) {
        return -1.0f;
    }
    memcpy(&f, (const uint8_t *)sa1g_world_ptr(w, pp.mwp) + 0x3C, 4);
    return f;
}

static double wrap_deg(double a)
{
    while (a > 180.0) {
        a -= 360.0;
    }
    while (a <= -180.0) {
        a += 360.0;
    }
    return a;
}

static sa1_status set_static(sa1_context *ctx, const tbuf *b)
{
    return sa1_collision_set_static(ctx, b->t, b->n);
}

/* ---- the tests -------------------------------------------------------------------------------------- */
typedef struct G {
    const char *sadc, *gdi;
    sa1_context *ctx;
} G;

static void test_api(G *E)
{
    sa1_context *ctx = E->ctx;
    sa1_collision_info info;
    sa1_collision_static d;
    sa1_collision_grid gp;
    sa1_collision_group grp;
    float pts[9] = { 0, 0, 0, 0, 0, 100, 100, 0, 0 };
    uint32_t idx[3] = { 0, 1, 2 }, id = 0, k;
    tbuf b;
    sa1_status st;
    int ok = 1;
    printf("api:\n");
    memset(&b, 0, sizeof b);
    tb_floor(&b, -500, 500, -500, 500, 0, 1, SA1_SURFACE_SOLID);
    tb_floor(&b, -500, 500, -500, 500, -50, 1, SA1_SURFACE_KILL);
    tb_floor(&b, -50, 50, -50, 50, -10, -1, SA1_SURFACE_WATER);
    st = set_static(ctx, &b);
    memset(&info, 0, sizeof info);
    info.struct_size = sizeof info;
    sa1_collision_info_get(ctx, &info);
    CHECK(st == SA1_OK && info.mode == SA1_COLLISION_MODE_GRID && info.triangles == 6 && info.collidable_triangles == 4
              && info.grid_cell_size == 256.0f && info.moving_objects == 0,
          "sa1_collision_set_static: GRID, %u triangles (%u collidable: the kill plane is not land), cell %g",
          (unsigned)info.triangles, (unsigned)info.collidable_triangles, info.grid_cell_size);
    /* set_static_ex GRID with parameters */
    memset(&gp, 0, sizeof gp);
    gp.struct_size = sizeof gp;
    gp.cell_size = 64.0f;
    gp.flags = 0x80u;
    memset(&grp, 0, sizeof grp);
    grp.struct_size = sizeof grp;
    grp.surface = SA1_SURFACE_SOLID;
    grp.num_points = 3;
    grp.num_tris = 1;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.mode = SA1_COLLISION_MODE_GRID;
    d.points = pts;
    d.num_points = 3;
    d.indices = idx;
    d.num_tris = 1;
    d.groups = &grp;
    d.num_groups = 1;
    d.grid = &gp;
    CHECK(sa1_collision_set_static_ex(ctx, &d) == SA1_ERR_INVALID_ARGUMENT, "unknown sa1_collision_grid flags refused");
    gp.flags = SA1_GRID_CUT;
    gp.max_edge = -1.0f;
    CHECK(sa1_collision_set_static_ex(ctx, &d) == SA1_ERR_INVALID_ARGUMENT, "a negative max_edge refused");
    gp.max_edge = 0.0f;
    st = sa1_collision_set_static_ex(ctx, &d);
    sa1_collision_info_get(ctx, &info);
    CHECK(st == SA1_OK && info.mode == SA1_COLLISION_MODE_GRID && info.grid_cell_size == 64.0f && info.groups == 1,
          "sa1_collision_set_static_ex(GRID, cell 64, CUT): %s, cell %g", sa1_status_string(st),
          info.grid_cell_size);
    d.struct_size = (uint32_t)offsetof(sa1_collision_static, grid);
    st = sa1_collision_set_static_ex(ctx, &d);
    sa1_collision_info_get(ctx, &info);
    CHECK(st == SA1_OK && info.grid_cell_size == 256.0f, "a 0.2-draft sa1_collision_static without `grid`: defaults (%s)",
          sa1_status_string(st));
    /* moving-object limits */
    {
        sa1_collision_tri t[5];
        memset(t, 0, sizeof t);
        for (k = 0; k < 5; k++) {
            t[k].v[1][2] = 10.0f;
            t[k].v[2][0] = 10.0f;
            t[k].surface = (k == 0 ? SA1_SURFACE_SOLID : 0u) | (k == 1 ? SA1_SURFACE_HURT : 0u)
                           | (k == 2 ? SA1_SURFACE_SLIPPERY : 0u) | (k == 3 ? SA1_SURFACE_SAND : 0u)
                           | (k == 4 ? SA1_SURFACE_BOOST : 0u);
        }
        CHECK(sa1_collision_object_add(ctx, t, 5, NULL, NULL, &id) == SA1_ERR_LIMIT,
              "a moving object with 5 surface kinds: SA1_ERR_LIMIT (at most %u)", (unsigned)SA1_MAX_OBJECT_GROUPS);
        t[0].v[2][0] = 1500.0f;
        CHECK(sa1_collision_object_add(ctx, t, 1, NULL, NULL, &id) == SA1_ERR_LIMIT,
              "a moving object reaching 1500 units from its origin: SA1_ERR_LIMIT");
        t[0].v[2][0] = 10.0f;
        st = sa1_collision_object_add(ctx, t, 1, NULL, NULL, &id);
        sa1_collision_info_get(ctx, &info);
        CHECK(st == SA1_OK && info.moving_objects == 1 && sa1_collision_object_remove(ctx, id) == SA1_OK,
              "add / remove a moving object (info.moving_objects %u)", (unsigned)info.moving_objects);
    }
    /* rotation -> BAMS: R(BAMS) == R for random rotations (the moving-object transform) */
    {
        uint32_t seed = 12345u, worst_k = 0;
        double worst = 0.0;
        for (k = 0; k < 2000; k++) {
            double a[3], r[9], q[9], c0, s0, c1, s1, c2, s2, e = 0.0;
            float rf[9];
            int32_t ang[3];
            int i;
            for (i = 0; i < 3; i++) {
                seed = seed * 1103515245u + 12345u;
                a[i] = ((double)(seed >> 8) / 16777216.0) * 2.0 * PI - PI;
            }
            if (k % 7 == 0) {
                a[1] = PI / 2;      /* gimbal lock */
            }
            c0 = cos(a[0]); s0 = sin(a[0]); c1 = cos(a[1]); s1 = sin(a[1]); c2 = cos(a[2]); s2 = sin(a[2]);
            r[0] = c2 * c1; r[1] = c2 * s1 * s0 - s2 * c0; r[2] = c2 * s1 * c0 + s2 * s0;
            r[3] = s2 * c1; r[4] = s2 * s1 * s0 + c2 * c0; r[5] = s2 * s1 * c0 - c2 * s0;
            r[6] = -s1; r[7] = c1 * s0; r[8] = c1 * c0;
            for (i = 0; i < 9; i++) {
                rf[i] = (float)r[i];
            }
            sa1ca_rotation_to_bams(rf, ang);
            c0 = cos(ang[0] * 2 * PI / 65536); s0 = sin(ang[0] * 2 * PI / 65536);
            c1 = cos(ang[1] * 2 * PI / 65536); s1 = sin(ang[1] * 2 * PI / 65536);
            c2 = cos(ang[2] * 2 * PI / 65536); s2 = sin(ang[2] * 2 * PI / 65536);
            q[0] = c2 * c1; q[1] = c2 * s1 * s0 - s2 * c0; q[2] = c2 * s1 * c0 + s2 * s0;
            q[3] = s2 * c1; q[4] = s2 * s1 * s0 + c2 * c0; q[5] = s2 * s1 * c0 - c2 * s0;
            q[6] = -s1; q[7] = c1 * s0; q[8] = c1 * c0;
            for (i = 0; i < 9; i++) {
                e = fabs(q[i] - r[i]) > e ? fabs(q[i] - r[i]) : e;
            }
            if (e > worst) {
                worst = e;
                worst_k = k;
            }
        }
        ok = worst < 3e-4;
        CHECK(ok, "rotation -> BAMS (Rz * Ry * Rx, gimbal lock included): largest matrix error %.2e (case %u) over 2000 "
              "rotations (1 BAMS = 9.6e-5 rad)", worst, (unsigned)worst_k);
    }
    sa1_collision_set_static(ctx, NULL, 0);
    tb_free(&b);
}

/* a plane: land, stand, run along the camera yaw, jump */
static void test_flat(G *E)
{
    sa1_context *ctx = E->ctx;
    sa1_collision_info info;
    sa1ca_world_stats ws;
    sa1_character_id id = 0;
    sa1_status st;
    tbuf b;
    run r;
    int i, land = -1, air = 0, relanded = 0;
    float x0, top = 0.0f;
    printf("flat: a 4096 x 4096 plane at y = 0\n");
    memset(&b, 0, sizeof b);
    tb_floor(&b, -2048, 2048, -2048, 2048, 0, 1, SA1_SURFACE_SOLID);
    set_static(ctx, &b);
    st = sonic_at(ctx, 0, 30, 0, 0, &id);
    memset(&info, 0, sizeof info);
    info.struct_size = sizeof info;
    sa1_collision_info_get(ctx, &info);
    CHECK(st == SA1_OK && info.builds == 1 && info.grid_objects > 0,
          "sa1_character_create_ex(SONIC, WORLD) on GRID collision: %s (not SA1_PARTIAL); %u objects (cell %g), %u "
          "triangles, %u cut", sa1_status_string(st), (unsigned)info.grid_objects, info.grid_cell_size,
          (unsigned)info.grid_triangles, (unsigned)info.grid_cut);
    if (st != SA1_OK) {
        printf("    %s\n", sa1_last_error(ctx));
        tb_free(&b);
        return;
    }
    run_init(&r, ctx, id);
    for (i = 0; i < 45 && r.st == SA1_OK; i++) {
        pair(&r, 0, 0, 0, 0);
        if (land < 0 && grounded(&r)) {
            land = (int)r.x.step;
        }
    }
    wstats(&r, &ws);
    CHECK(r.st == SA1_OK && land > 0 && grounded(&r) && fabsf(r.x.sa1_position[1]) < 0.01f && r.x.mode == 1
              && (r.x.surface & SA1_SURFACE_SOLID),
          "lands at step %d and stands: mode %d, y %g, surface %X (attr %08X); window %u objects / %u polygons",
          land, (int)r.x.mode, r.x.sa1_position[1], (unsigned)r.x.surface, (unsigned)r.x.attr,
          (unsigned)ws.window_objects, (unsigned)ws.window_polygons);
    CHECK(ws.attached && ws.grid != 0 && ws.window_objects > 0 && ws.window_objects <= 128,
          "the window holds the objects near Sonic (%u of %u candidates)", (unsigned)ws.window_objects,
          (unsigned)ws.candidates);
    x0 = r.x.sa1_position[0];
    for (i = 0; i < 60 && r.st == SA1_OK; i++) {
        pair(&r, 0, 1, 0, 0);
    }
    {
        double dir = atan2(-(double)r.x.sa1_position[2], (double)(r.x.sa1_position[0] - x0)) * 180.0 / PI;
        CHECK(r.st == SA1_OK && grounded(&r) && r.x.sa1_position[0] - x0 > 100.0f && fabs(dir) < 8.0
                  && fabsf(r.x.sa1_position[1]) < 0.01f && r.x.mode == 2,
              "stick forward, camera yaw 0: runs along +x (%.1f units in 2 s at %.1f deg: the game's dead-zone bias of "
              "about 6 deg; y %g, mode %d action %d, speed %.2f per step)", r.x.sa1_position[0] - x0, dir,
              r.x.sa1_position[1], (int)r.x.mode, (int)r.x.action, r.x.player_speed[0]);
    }
    for (i = 0; i < 40 && r.st == SA1_OK; i++) {
        pair(&r, 0, 1, 0, i < 6 ? SA1_BUTTON_JUMP : 0u);
        if (!grounded(&r)) {
            air++;
            top = r.x.sa1_position[1] > top ? r.x.sa1_position[1] : top;
        } else if (air) {
            relanded = 1;
        }
    }
    CHECK(r.st == SA1_OK && air > 5 && top > 20.0f && relanded && grounded(&r),
          "jump: %d pairs in the air, %.1f units high, lands again (mode %d)", air, top, (int)r.x.mode);
    printf("    mode/action/step: %s\n", r.log);
    sa1_character_destroy(ctx, id);
    tb_free(&b);
}

/* a 30 degree ramp */
static void test_ramp(G *E)
{
    sa1_context *ctx = E->ctx;
    sa1_character_id id = 0;
    const float t30 = 0.57735027f;
    float p[4][3], n[3] = { 0, 1, 0 }, maxy = 0.0f, worst = 0.0f, nrm[3] = { 0, 0, 0 };
    tbuf b;
    run r;
    int i, on_ramp = 0, air_on_ramp = 0;
    printf("ramp: 30 degrees from x = 200 to 700 (height %.1f)\n", 500.0f * t30);
    memset(&b, 0, sizeof b);
    tb_floor(&b, -1400, 200, -300, 300, 0, 1, SA1_SURFACE_SOLID);
    p[0][0] = 200; p[0][1] = 0; p[0][2] = -300;
    p[1][0] = 700; p[1][1] = 500.0f * t30; p[1][2] = -300;
    p[2][0] = 700; p[2][1] = 500.0f * t30; p[2][2] = 300;
    p[3][0] = 200; p[3][1] = 0; p[3][2] = 300;
    tb_quad(&b, p, n, SA1_SURFACE_SOLID);
    tb_floor(&b, 700, 1400, -300, 300, 500.0f * t30, 1, SA1_SURFACE_SOLID);
    set_static(ctx, &b);
    if (sonic_at(ctx, -1200, 5, 0, 0, &id) != SA1_OK) {
        CHECK(0, "create: %s", sa1_last_error(ctx));
        tb_free(&b);
        return;
    }
    run_init(&r, ctx, id);
    for (i = 0; i < 10; i++) {
        pair(&r, 0, 0, 0, 0);
    }
    for (i = 0; i < 400 && r.st == SA1_OK && r.x.sa1_position[0] < 690.0f; i++) {
        pair(&r, 0, 1, YAW_X, 0);
        if (r.x.sa1_position[0] > 260.0f && r.x.sa1_position[0] < 650.0f) {
            float want = (r.x.sa1_position[0] - 200.0f) * t30, e = fabsf(r.x.sa1_position[1] - want);
            on_ramp++;
            air_on_ramp += !grounded(&r);
            worst = e > worst ? e : worst;
            memcpy(nrm, r.x.floor_normal, sizeof nrm);
        }
        maxy = r.x.sa1_position[1] > maxy ? r.x.sa1_position[1] : maxy;
    }
    CHECK(r.st == SA1_OK && on_ramp >= 3 && air_on_ramp == 0 && worst < 1.0f && maxy > 250.0f,
          "runs up the ramp: %d pairs on it, all on the ground, at most %.3f units off the surface, reaches y %.1f",
          on_ramp, air_on_ramp ? -1.0f : worst, maxy);
    CHECK(fabsf(nrm[0] + 0.5f) < 0.02f && fabsf(nrm[1] - 0.8660254f) < 0.02f && fabsf(nrm[2]) < 0.02f,
          "floor normal on the ramp (%.4f %.4f %.4f), expected (-0.5 0.866 0)", nrm[0], nrm[1], nrm[2]);
    printf("    mode/action/step: %s\n", r.log);
    sa1_character_destroy(ctx, id);
    tb_free(&b);
}

/* one step of height h: does Sonic get on top running (or walking) without jumping? */
static int step_climb(sa1_context *ctx, float h, float stick, int run_up, float *final_y, float *speed)
{
    sa1_character_id id = 0;
    tbuf b;
    run r;
    int i, ok;
    float best_x = -1e9f, spd = 0.0f;
    int stuck = 0;
    memset(&b, 0, sizeof b);
    tb_floor(&b, -1600, 300, -300, 300, 0, 1, SA1_SURFACE_SOLID);
    tb_wall_x(&b, 300, 0, h, -300, 300, -1, SA1_SURFACE_SOLID);
    tb_floor(&b, 300, 1200, -300, 300, h, 1, SA1_SURFACE_SOLID);
    set_static(ctx, &b);
    if (sonic_at(ctx, run_up ? -1400.0f : 250.0f, 3, 0, 0, &id) != SA1_OK) {
        tb_free(&b);
        return -1;
    }
    run_init(&r, ctx, id);
    for (i = 0; i < 8; i++) {
        pair(&r, 0, 0, 0, 0);
    }
    for (i = 0; i < 500 && r.st == SA1_OK && r.x.sa1_position[0] < 400.0f && stuck < 30; i++) {
        pair(&r, 0, stick, YAW_X, 0);
        if (r.x.sa1_position[0] > best_x + 0.5f) {
            best_x = r.x.sa1_position[0];
            stuck = 0;
        } else {
            stuck++;
        }
        if (r.x.sa1_position[0] < 295.0f && r.x.sa1_position[0] > 250.0f && r.x.player_speed[0] > spd) {
            spd = r.x.player_speed[0];    /* the speed running into the step */
        }
    }
    *final_y = r.x.sa1_position[1];
    *speed = spd;
    ok = r.st == SA1_OK && r.x.sa1_position[0] > 320.0f && fabsf(r.x.sa1_position[1] - h) < 0.5f;
    sa1_character_destroy(ctx, id);
    tb_free(&b);
    return ok;
}

static void test_stairs(G *E)
{
    sa1_context *ctx = E->ctx;
    int h, best_run = 0, first_fail_run = 0, best_walk = 0, first_fail_walk = 0, mono = 1, last = 1;
    float y, spd_run = 0.0f, spd_walk = 0.0f, spd;
    printf("stairs: the step-up height (one step of height h = 1 .. 20 units; running from a 1700-unit run-up, "
           "slowly from 50 units)\n");
    (void)last;
    for (h = 1; h <= 20; h++) {
        int ok = step_climb(ctx, (float)h, 1.0f, 1, &y, &spd);
        spd_run = spd;
        if (ok < 0) {
            CHECK(0, "create: %s", sa1_last_error(ctx));
            return;
        }
        if (g_verbose) {
            printf("    run  h %2d: %s (y %.2f, speed %.2f)\n", h, ok ? "up" : "stopped", y, spd);
        }
        if (ok && first_fail_run) {
            mono = 0;
        }
        if (ok) {
            best_run = h;
        } else if (!first_fail_run) {
            first_fail_run = h;
        }
    }
    for (h = 1; h <= 12; h++) {
        int ok = step_climb(ctx, (float)h, 1.0f, 0, &y, &spd);
        spd_walk = spd;
        if (g_verbose) {
            printf("    slow h %2d: %s (y %.2f, speed %.2f)\n", h, ok ? "up" : "stopped", y, spd);
        }
        if (ok) {
            best_walk = h;
        } else if (!first_fail_walk) {
            first_fail_walk = h;
        }
    }
    CHECK(best_run >= 2 && first_fail_run > 0 && mono,
          "step-up height: running (%.2f units per step at the step), steps up to %d units are climbed without a jump "
          "(the first refused: %d; monotonic: %s); slowly (%.2f per step): up to %d (first refused %d)",
          spd_run, best_run, first_fail_run, mono ? "yes" : "no", spd_walk, best_walk, first_fail_walk);
    /* a STAIRS ramp over steps that are too high: 0x4000 in the attribute while on it, climbed */
    {
        sa1_character_id id = 0;
        float p[4][3], n[3] = { 0, 1, 0 };
        tbuf b;
        run r;
        int i, k, seen = 0;
        float sh = (float)(first_fail_run ? first_fail_run + 4 : 24);
        memset(&b, 0, sizeof b);
        tb_floor(&b, -1600, 200, -200, 200, 0, 1, SA1_SURFACE_SOLID);
        for (k = 0; k < 6; k++) {      /* the steps (solid, too high to step up) */
            float x0 = 200.0f + 40.0f * (float)k;
            tb_wall_x(&b, x0, sh * (float)k, sh * (float)(k + 1), -200, 200, -1, SA1_SURFACE_SOLID);
            tb_floor(&b, x0, x0 + 40.0f, -200, 200, sh * (float)(k + 1), 1, SA1_SURFACE_SOLID);
        }
        tb_floor(&b, 440, 1200, -200, 200, sh * 6.0f, 1, SA1_SURFACE_SOLID);
        /* the ramp over them: the line through the nosings (200 + 40 k, sh (k + 1)), from the floor at x = 160 to the
         * top tread at x = 400, 0.25 above them (a Source-style clip ramp) */
        p[0][0] = 160; p[0][1] = 0.25f; p[0][2] = -100;
        p[1][0] = 400; p[1][1] = sh * 6.0f + 0.25f; p[1][2] = -100;
        p[2][0] = 400; p[2][1] = sh * 6.0f + 0.25f; p[2][2] = 100;
        p[3][0] = 160; p[3][1] = 0.25f; p[3][2] = 100;
        tb_quad(&b, p, n, SA1_SURFACE_SOLID | SA1_SURFACE_STAIRS);
        set_static(ctx, &b);
        if (sonic_at(ctx, -1400, 3, 0, 0, &id) == SA1_OK) {
            run_init(&r, ctx, id);
            for (i = 0; i < 8; i++) {
                pair(&r, 0, 0, 0, 0);
            }
            for (i = 0; i < 400 && r.st == SA1_OK && r.x.sa1_position[0] < 470.0f; i++) {
                pair(&r, 0, 1, YAW_X, 0);
                if (grounded(&r) && (r.x.attr & 0x4000u) && (r.x.surface & SA1_SURFACE_STAIRS)) {
                    seen++;
                }
            }
            CHECK(r.st == SA1_OK && seen > 0 && r.x.sa1_position[0] > 400.0f && fabsf(r.x.sa1_position[1] - sh * 6.0f) < 2.0f,
                  "a STAIRS (COL 0x4000) ramp over %g-unit steps: %d pairs on it in stair mode, at the top (x %.1f, y %.1f)",
                  sh, seen, r.x.sa1_position[0], r.x.sa1_position[1]);
            printf("    mode/action/step: %s\n", r.log);
            sa1_character_destroy(ctx, id);
        } else {
            CHECK(0, "create: %s", sa1_last_error(ctx));
        }
        tb_free(&b);
    }
}

/* a 90 degree wall */
static void test_wall(G *E)
{
    sa1_context *ctx = E->ctx;
    sa1_character_id id = 0;
    tbuf b;
    run r;
    int i, hit = 1000;
    float maxx = -1e9f, hit_speed = 0.0f;
    printf("wall: a 90 degree wall at x = 300\n");
    memset(&b, 0, sizeof b);
    tb_floor(&b, -1600, 800, -400, 400, 0, 1, SA1_SURFACE_SOLID);
    tb_wall_x(&b, 300, -10, 300, -400, 400, -1, SA1_SURFACE_SOLID);
    set_static(ctx, &b);
    if (sonic_at(ctx, -1400, 3, 0, 0, &id) != SA1_OK) {
        CHECK(0, "create: %s", sa1_last_error(ctx));
        tb_free(&b);
        return;
    }
    run_init(&r, ctx, id);
    for (i = 0; i < 8; i++) {
        pair(&r, 0, 0, 0, 0);
    }
    for (i = 0; i < 400 && r.st == SA1_OK && (maxx < 280.0f || i < hit + 30); i++) {
        pair(&r, 0, 1, YAW_X, 0);
        maxx = r.x.sa1_position[0] > maxx ? r.x.sa1_position[0] : maxx;
        if (hit == 1000 && maxx >= 280.0f) {
            hit = i;
            hit_speed = r.x.player_speed[0];
        }
    }
    CHECK(r.st == SA1_OK && maxx < 300.0f && maxx > 280.0f && grounded(&r) && fabsf(r.x.sa1_position[2]) < 30.0f,
          "runs into the wall at %.2f units per step and stays in front of it: largest x %.2f (wall at 300), 30 pairs "
          "later x %.2f, mode %d action %d, speed %.3f", hit_speed, maxx, r.x.sa1_position[0], (int)r.x.mode,
          (int)r.x.action, r.x.player_speed[0]);
    printf("    mode/action/step: %s\n", r.log);
    sa1_character_destroy(ctx, id);
    tb_free(&b);
}

/* a closed water box over a floor */
static void test_water(G *E)
{
    sa1_context *ctx = E->ctx;
    sa1_collision_info info;
    sa1_character_id id = 0;
    tbuf b;
    run r, dry;
    int i, k, max_breath = 0, landed = -1, landed_dry = -1;
    float vy_water = 0.0f, vy_dry = 0.0f;
    printf("water: a water box (top y = 0) over a floor at y = -150; the same fall without water\n");
    for (k = 0; k < 2; k++) {
        run *rr = k ? &dry : &r;
        memset(&b, 0, sizeof b);
        tb_floor(&b, -600, 600, -600, 600, -150, 1, SA1_SURFACE_SOLID);
        if (k == 0) {
            tb_box(&b, -400, 400, -150, 0, -400, 400, SA1_SURFACE_WATER);
        }
        set_static(ctx, &b);
        memset(&info, 0, sizeof info);
        info.struct_size = sizeof info;
        if (sonic_at(ctx, 0, 40, 0, 0, &id) != SA1_OK) {
            CHECK(0, "create: %s", sa1_last_error(ctx));
            tb_free(&b);
            return;
        }
        sa1_collision_info_get(ctx, &info);
        if (k == 0) {
            CHECK(info.grid_dropped == 10, "the water box keeps its top face only: %u of its 12 triangles dropped "
                  "(%u land objects)", (unsigned)info.grid_dropped, (unsigned)info.grid_objects);
        }
        run_init(rr, ctx, id);
        for (i = 0; i < 150 && rr->st == SA1_OK; i++) {
            pair(rr, 0, 0, 0, 0);
            if (rr->x.sa1_position[1] < -60.0f && rr->x.sa1_position[1] > -100.0f) {
                if (k == 0 && vy_water == 0.0f) {
                    vy_water = rr->x.speed[1];
                } else if (k == 1 && vy_dry == 0.0f) {
                    vy_dry = rr->x.speed[1];
                }
            }
            if (k == 0 && rr->x.breath_timer > max_breath) {
                max_breath = rr->x.breath_timer;
            }
            if (grounded(rr) && rr->x.sa1_position[1] < -140.0f) {
                if (k == 0 && landed < 0) {
                    landed = (int)rr->x.step;
                } else if (k == 1 && landed_dry < 0) {
                    landed_dry = (int)rr->x.step;
                }
            }
        }
        if (k == 0) {
            printf("    mode/action/step: %s\n", rr->log);
        }
        sa1_character_destroy(ctx, id);
        tb_free(&b);
    }
    CHECK(r.st == SA1_OK && dry.st == SA1_OK && vy_water < 0.0f && vy_dry < 0.0f && fabsf(vy_water) < 0.8f * fabsf(vy_dry),
          "water physics: falling speed at y -60 .. -100 is %.3f in the water, %.3f without", vy_water, vy_dry);
    CHECK(max_breath > 60 && landed > 0 && landed > landed_dry,
          "under water the breath timer counts (%d steps); the floor is reached at step %d (dry: %d)", max_breath, landed,
          landed_dry);
}

/* a platform that moves */
static void test_moving(G *E)
{
    sa1_context *ctx = E->ctx;
    sa1_character_id id = 0;
    sa1ca_world_stats ws;
    tbuf pl;
    run r;
    uint32_t oid = 0;
    int i, ground_pairs = 0;
    float pos[3] = { 0, 0, 0 }, rel0[3] = { 0, 0, 0 }, worst = 0.0f, wy = 0.0f;
    sa1_status st;
    printf("moving: a 160 x 160 platform (no static collision) moving along +x, then up and down\n");
    sa1_collision_set_static(ctx, NULL, 0);
    memset(&pl, 0, sizeof pl);
    tb_box(&pl, -80, 80, -20, 0, -80, 80, SA1_SURFACE_SOLID);
    st = sa1_collision_object_add(ctx, pl.t, pl.n, pos, NULL, &oid);
    CHECK(st == SA1_OK, "sa1_collision_object_add: %s", sa1_status_string(st));
    st = sonic_at(ctx, 20, 30, 10, 0, &id);
    CHECK(st == SA1_PARTIAL, "a world without static collision: %s (moving objects still collide)", sa1_status_string(st));
    run_init(&r, ctx, id);
    for (i = 0; i < 30 && r.st == SA1_OK; i++) {
        pair(&r, 0, 0, 0, 0);
    }
    wstats(&r, &ws);
    CHECK(grounded(&r) && fabsf(r.x.sa1_position[1]) < 0.1f && ws.moving == 1 && ws.moving_entries == 1,
          "lands on the platform: y %g, mode %d; the world carries %u object(s), %u DynamicCOL entr%s",
          r.x.sa1_position[1], (int)r.x.mode, (unsigned)ws.moving, (unsigned)ws.moving_entries,
          ws.moving_entries == 1 ? "y" : "ies");
    for (i = 0; i < 3; i++) {
        rel0[i] = r.x.sa1_position[i] - pos[i];
    }
    for (i = 0; i < 120 && r.st == SA1_OK; i++) {
        float e = 0.0f;
        int k;
        if (i < 60) {
            pos[0] += 3.0f;                         /* 90 units / s along x */
        } else {
            pos[1] = 60.0f * sinf((float)(i - 60) * 0.1f);   /* up and down */
        }
        sa1_collision_object_move(ctx, oid, pos, NULL);
        pair(&r, 0, 0, 0, 0);
        ground_pairs += grounded(&r);
        for (k = 0; k < 3; k++) {
            float d = r.x.sa1_position[k] - pos[k] - rel0[k];
            e = fabsf(d) > e ? fabsf(d) : e;
        }
        worst = e > worst ? e : worst;
        if (i == 59) {
            wy = r.x.sa1_position[0];
        }
    }
    CHECK(r.st == SA1_OK && ground_pairs >= 115 && worst < 2.0f && wy > 150.0f,
          "carried: %d of 120 pairs on the ground, Sonic stays within %.3f units of his place on the platform (the game "
          "carries him one step after the platform moved: 1.5 units at 3 per pair) (x %.1f after 180 units of travel)",
          ground_pairs, worst, wy);
    /* jump off a moving platform keeps its speed; remove it: Sonic falls */
    sa1_collision_object_remove(ctx, oid);
    for (i = 0; i < 10 && r.st == SA1_OK; i++) {
        pair(&r, 0, 0, 0, 0);
    }
    wstats(&r, &ws);
    CHECK(r.st == SA1_OK && !grounded(&r) && r.x.sa1_position[1] < pos[1] - 5.0f && ws.moving == 0 && ws.moving_entries == 0,
          "removed: Sonic falls (y %.1f, platform was at %.1f); the world withdrew the DynamicCOL entry", r.x.sa1_position[1],
          pos[1]);
    printf("    mode/action/step: %s\n", r.log);
    sa1_character_destroy(ctx, id);
    tb_free(&pl);
}

/* a platform turning about the vertical */
static void test_rotating(G *E)
{
    sa1_context *ctx = E->ctx;
    sa1_character_id id = 0;
    tbuf pl;
    run r;
    uint32_t oid = 0;
    int i, ground_pairs = 0;
    float pos[3] = { 0, 0, 0 }, rot[9], th = 0.0f, r0, a0, yaw0, rad_err = 0.0f;
    double turned, moved_ang, yaw_turn;
    printf("rotating: a 300 x 300 platform turning about the vertical by 1.5 degrees per pair, Sonic 80 units off-centre\n");
    sa1_collision_set_static(ctx, NULL, 0);
    memset(&pl, 0, sizeof pl);
    tb_box(&pl, -150, 150, -20, 0, -150, 150, SA1_SURFACE_SOLID);
    sa1_collision_object_add(ctx, pl.t, pl.n, pos, NULL, &oid);
    if (sonic_at(ctx, 80, 20, 0, 0, &id) < 0) {
        CHECK(0, "create: %s", sa1_last_error(ctx));
        tb_free(&pl);
        return;
    }
    run_init(&r, ctx, id);
    for (i = 0; i < 30 && r.st == SA1_OK; i++) {
        pair(&r, 0, 0, 0, 0);
    }
    r0 = hypotf(r.x.sa1_position[0], r.x.sa1_position[2]);
    a0 = atan2f(-r.x.sa1_position[2], r.x.sa1_position[0]);
    yaw0 = r.x.yaw;
    for (i = 0; i < 100 && r.st == SA1_OK; i++) {
        float c, s, rr;
        th += 1.5f * (float)(PI / 180.0);
        c = cosf(th);
        s = sinf(th);
        /* a host-space rotation about +y by th (counter-clockwise seen from above: x -> -z) */
        rot[0] = c;  rot[1] = 0; rot[2] = s;
        rot[3] = 0;  rot[4] = 1; rot[5] = 0;
        rot[6] = -s; rot[7] = 0; rot[8] = c;
        sa1_collision_object_move(ctx, oid, pos, rot);
        pair(&r, 0, 0, 0, 0);
        ground_pairs += grounded(&r);
        rr = hypotf(r.x.sa1_position[0], r.x.sa1_position[2]);
        rad_err = fabsf(rr - r0) > rad_err ? fabsf(rr - r0) : rad_err;
    }
    turned = th * 180.0 / PI;
    moved_ang = wrap_deg((atan2f(-r.x.sa1_position[2], r.x.sa1_position[0]) - a0) * 180.0 / PI);
    yaw_turn = wrap_deg((r.x.yaw - yaw0) * 180.0 / PI);
    CHECK(r.st == SA1_OK && ground_pairs >= 98 && rad_err < 3.0f && fabs(moved_ang - turned) < 4.0 && fabs(yaw_turn - turned) < 4.0,
          "carried around: %d of 100 pairs on the ground, radius %.1f kept within %.2f, moved %.1f deg and turned %.1f deg "
          "while the platform turned %.1f deg", ground_pairs, r0, rad_err, moved_ang, yaw_turn, turned);
    printf("    mode/action/step: %s\n", r.log);
    sa1_collision_object_remove(ctx, oid);
    sa1_character_destroy(ctx, id);
    tb_free(&pl);
}

/* a kill plane */
static void test_kill(G *E)
{
    sa1_context *ctx = E->ctx;
    sa1_collision_info info;
    sa1ca_world_stats ws;
    sa1_character_id id = 0;
    sa1_event ev[64];
    tbuf b;
    run r;
    uint32_t n = 0, k;
    int i, scream = -1, restart = -1, passed = -1;
    printf("kill: a kill plane at y = -200 (no floor)\n");
    memset(&b, 0, sizeof b);
    tb_floor(&b, -400, 400, -400, 400, 0, 1, SA1_SURFACE_SOLID);
    tb_floor(&b, -3000, 3000, -3000, 3000, -200, 1, SA1_SURFACE_KILL);
    tb_floor(&b, -3000, 3000, -3000, 3000, -260, -1, SA1_SURFACE_KILL);    /* facing down: dropped */
    set_static(ctx, &b);
    if (sonic_at(ctx, 600, 10, 0, 0, &id) != SA1_OK) {
        CHECK(0, "create: %s", sa1_last_error(ctx));
        tb_free(&b);
        return;
    }
    memset(&info, 0, sizeof info);
    info.struct_size = sizeof info;
    sa1_collision_info_get(ctx, &info);
    run_init(&r, ctx, id);
    wstats(&r, &ws);
    CHECK(info.grid_kill_objects > 0 && ws.kill_objects == info.grid_kill_objects,
          "%u kill plane objects, the world's death zones (%u)", (unsigned)info.grid_kill_objects, (unsigned)ws.kill_objects);
    sa1_poll_events(ctx, ev, 64, &n);
    for (i = 0; i < 160 && r.st == SA1_OK; i++) {
        pair(&r, 0, 0, 0, 0);
        if (passed < 0 && r.x.sa1_position[1] < -200.0f) {
            passed = (int)r.x.step;
        }
        n = 0;
        sa1_poll_events(ctx, ev, 64, &n);
        for (k = 0; k < n; k++) {
            if (ev[k].type == SA1_EVENT_SOUND && ev[k].id == 0x571 && scream < 0) {
                scream = (int)ev[k].step;
            }
            if (ev[k].type == SA1_EVENT_RESTART && restart < 0) {
                restart = (int)ev[k].step;
            }
            if (g_verbose) {
                printf("      event type %u id %X arg %u step %u\n", (unsigned)ev[k].type, (unsigned)ev[k].id,
                       (unsigned)ev[k].arg, (unsigned)ev[k].step);
            }
        }
    }
    CHECK(r.st == SA1_OK && passed > 0 && scream >= passed - 2 && scream <= passed + 4 && restart > scream + 100,
          "falls through the plane at step %d: the death sequence starts (scream 0x571 at step %d, restart request at step "
          "%d)", passed, scream, restart);
    sa1_character_destroy(ctx, id);
    tb_free(&b);
}

/* surfaces: slippery, hurt */
static void test_surfaces(G *E)
{
    sa1_context *ctx = E->ctx;
    static const struct {
        uint32_t surface;
        const char *name;
        float weight;
    } kinds[] = {
        { SA1_SURFACE_SOLID, "solid", 1.0f },
        { SA1_SURFACE_SLIPPERY, "slippery (COL 0x4)", 0.099999996f },
        { SA1_SURFACE_SAND, "sand (COL 0x10)", 0.5f },
        { SA1_SURFACE_BOOST, "boost (COL 0x80)", 1.5f },
    };
    sa1_character_id id = 0;
    tbuf b;
    run r;
    uint32_t k;
    int i;
    printf("surfaces: a floor of each kind (implied SOLID)\n");
    for (k = 0; k < sizeof kinds / sizeof kinds[0]; k++) {
        float wgt;
        memset(&b, 0, sizeof b);
        tb_floor(&b, -600, 600, -600, 600, 0, 1, kinds[k].surface);
        set_static(ctx, &b);
        if (sonic_at(ctx, 0, 10, 0, 0, &id) != SA1_OK) {
            CHECK(0, "create: %s", sa1_last_error(ctx));
            tb_free(&b);
            return;
        }
        run_init(&r, ctx, id);
        for (i = 0; i < 20 && r.st == SA1_OK; i++) {
            pair(&r, 0, i > 10 ? 1.0f : 0.0f, 0, 0);
        }
        wgt = player_weight(&r);
        CHECK(r.st == SA1_OK && grounded(&r) && wgt == kinds[k].weight && (r.x.surface & kinds[k].surface) == kinds[k].surface,
              "%s: on the ground, motionwk.weight %g (8C075214), surface %X attr %08X", kinds[k].name, wgt,
              (unsigned)r.x.surface, (unsigned)r.x.attr);
        sa1_character_destroy(ctx, id);
        tb_free(&b);
    }
    /* hurt floor */
    {
        int hurt = -1, mode_hurt = 0;
        float hurt_x = 0.0f;
        uint32_t s0;
        memset(&b, 0, sizeof b);
        tb_floor(&b, -600, 600, -600, 600, 0, 1, SA1_SURFACE_SOLID);
        tb_floor(&b, 200, 600, -600, 600, 0.5f, 1, SA1_SURFACE_HURT);
        set_static(ctx, &b);
        if (sonic_at(ctx, 150, 10, 0, 0, &id) == SA1_OK) {
            run_init(&r, ctx, id);
            for (i = 0; i < 10; i++) {
                pair(&r, 0, 0, 0, 0);
            }
            s0 = r.x.mode;
            for (i = 0; i < 90 && r.st == SA1_OK; i++) {
                float x = r.x.sa1_position[0];
                pair(&r, 0, 1, YAW_X, 0);
                if (hurt < 0 && r.x.mode != 1 && r.x.mode != 2 && r.x.mode != (int32_t)s0) {
                    hurt = (int)r.x.step;
                    mode_hurt = r.x.mode;
                    hurt_x = x;
                }
            }
            CHECK(r.st == SA1_OK && hurt > 0 && mode_hurt == 82 && hurt_x > 190.0f,
                  "a HURT floor (COL 0x10000, from x = 200): the game's damage reaction, knock-back mode %d at step %d "
                  "(x %.1f; no rings: the game's hit rule)", mode_hurt, hurt, hurt_x);
            printf("    mode/action/step: %s\n", r.log);
            sa1_character_destroy(ctx, id);
        }
        tb_free(&b);
    }
}

/* the window's limits */
static void test_capacity(G *E)
{
    sa1_context *ctx = E->ctx;
    sa1_collision_info info;
    sa1ca_world_stats ws;
    sa1_character_id id = 0;
    sa1_event ev[64];
    tbuf b, pl;
    run r;
    uint32_t n = 0, k, oids[70], cap_poly = 0, cap_moving = 0;
    int i, j;
    float y_min = 1e9f, y_max = -1e9f;
    double t0;
    printf("capacity: a 3072 x 3072 floor of 12-unit triangles (131,072), then 70 moving objects\n");
    memset(&b, 0, sizeof b);
    for (i = 0; i < 256; i++) {
        for (j = 0; j < 256; j++) {
            float x0 = -1536.0f + 12.0f * (float)i, z0 = -1536.0f + 12.0f * (float)j;
            tb_floor(&b, x0, x0 + 12.0f, z0, z0 + 12.0f, 0, 1, SA1_SURFACE_SOLID);
        }
    }
    t0 = sa1_now_ms();
    set_static(ctx, &b);
    if (sonic_at(ctx, 0, 10, 0, 0, &id) != SA1_OK) {
        CHECK(0, "create: %s", sa1_last_error(ctx));
        tb_free(&b);
        return;
    }
    memset(&info, 0, sizeof info);
    info.struct_size = sizeof info;
    sa1_collision_info_get(ctx, &info);
    INFO("GRID synthesis of %u triangles: %u objects, %u bytes, %.0f ms (with the world)", (unsigned)b.n,
         (unsigned)info.grid_objects, (unsigned)info.bytes, sa1_now_ms() - t0);
    run_init(&r, ctx, id);
    t0 = sa1_now_ms();
    for (i = 0; i < 150 && r.st == SA1_OK; i++) {
        pair(&r, (float)sin(i * 0.05), 1, 0, 0);
        if (i > 20) {
            y_min = r.x.sa1_position[1] < y_min ? r.x.sa1_position[1] : y_min;
            y_max = r.x.sa1_position[1] > y_max ? r.x.sa1_position[1] : y_max;
        }
        n = 0;
        sa1_poll_events(ctx, ev, 64, &n);
        for (k = 0; k < n; k++) {
            cap_poly += ev[k].type == SA1_EVENT_CAPACITY && ev[k].id == SA1_CAPACITY_WINDOW_POLYGONS;
        }
    }
    wstats(&r, &ws);
    /* round 0.9.3: the budget leaves far objects out (counted), never one within 40 units, so nothing is reported */
    CHECK(r.st == SA1_OK && grounded(&r) && y_min > -0.01f && y_max < 0.01f && ws.window_polygons <= 7680u
              && ws.over_polygons > 0 && ws.must_over == 0 && cap_poly == 0,
          "runs on it for 150 pairs (%.3f ms per pair): the window keeps %u objects / %u polygons (budget 7680), %u steps "
          "left far objects out, %u one within 40 units (SA1_EVENT_CAPACITY %u reported %u time(s): only for those)",
          (sa1_now_ms() - t0) / 150.0, (unsigned)ws.window_objects, (unsigned)ws.window_polygons,
          (unsigned)ws.over_polygons, (unsigned)ws.must_over, (unsigned)SA1_CAPACITY_WINDOW_POLYGONS, (unsigned)cap_poly);
    sa1_character_destroy(ctx, id);
    tb_free(&b);
    /* more moving objects than a world holds */
    memset(&b, 0, sizeof b);
    tb_floor(&b, -2000, 2000, -2000, 2000, 0, 1, SA1_SURFACE_SOLID);
    set_static(ctx, &b);
    memset(&pl, 0, sizeof pl);
    tb_box(&pl, -10, 10, 0, 10, -10, 10, SA1_SURFACE_SOLID);
    for (k = 0; k < 70; k++) {
        float p[3] = { -1000.0f + 30.0f * (float)k, 0, 500 };
        sa1_collision_object_add(ctx, pl.t, pl.n, p, NULL, &oids[k]);
    }
    if (sonic_at(ctx, 0, 10, 0, 0, &id) == SA1_OK) {
        run_init(&r, ctx, id);
        for (i = 0; i < 10 && r.st == SA1_OK; i++) {
            pair(&r, 0, 0, 0, 0);
            n = 0;
            sa1_poll_events(ctx, ev, 64, &n);
            for (k = 0; k < n; k++) {
                cap_moving += ev[k].type == SA1_EVENT_CAPACITY && ev[k].id == SA1_CAPACITY_MOVING_OBJECTS;
            }
        }
        wstats(&r, &ws);
        CHECK(r.st == SA1_OK && ws.moving == 64 && ws.moving_entries == 64 && cap_moving == 1 && grounded(&r),
              "70 moving objects: the world carries %u (%u DynamicCOL entries), SA1_EVENT_CAPACITY %u reported %u time(s)",
              (unsigned)ws.moving, (unsigned)ws.moving_entries, (unsigned)SA1_CAPACITY_MOVING_OBJECTS, (unsigned)cap_moving);
        for (k = 0; k < 10; k++) {
            sa1_collision_object_remove(ctx, oids[k]);
        }
        for (i = 0; i < 2 && r.st == SA1_OK; i++) {
            pair(&r, 0, 0, 0, 0);
        }
        wstats(&r, &ws);
        CHECK(r.st == SA1_OK && ws.moving == 60, "10 removed: the world now carries all 60 (%u)", (unsigned)ws.moving);
        sa1_character_destroy(ctx, id);
    }
    for (k = 10; k < 70; k++) {
        sa1_collision_object_remove(ctx, oids[k]);
    }
    tb_free(&pl);
    tb_free(&b);
    /* 21 land objects at the character's feet (stacked floors with distinct raw COL words): 8C073160 collects every object
     * of the character's query into hits[16] / sorted[16] without a bound check; round 0.9.2's window kept 15 (the query
     * bound, SA1_CAPACITY_LAND_QUERY; 14 by box distance before); round 0.9.3: the lowered arrays hold 192, the window
     * keeps all 21 and the query gathers them all */
    {
        float pts[12] = { -100, 0, -100, 100, 0, -100, 100, 0, 100, -100, 0, 100 };
        uint32_t idx[6] = { 0, 3, 2, 0, 2, 1 }, cap_near = 0, kk;
        float *allp = (float *)malloc(21u * sizeof pts);
        uint32_t *alli = (uint32_t *)malloc(21u * sizeof idx);
        sa1_collision_group grp[21];
        sa1_collision_static d;
        uint32_t ng = 0;
        memset(grp, 0, sizeof grp);
        for (kk = 1; kk <= 23; kk++) {
            uint32_t q;
            if (kk == 8 || kk == 16) {
                continue;       /* 0x08000000 (rebuilt every update) and 0x10000000 (UseRotation) */
            }
            for (q = 0; q < 12; q++) {
                allp[12u * ng + q] = pts[q] + ((q % 3u) == 1u ? 0.02f * (float)ng : 0.0f);
            }
            for (q = 0; q < 6; q++) {
                alli[6u * ng + q] = idx[q];
            }
            grp[ng].struct_size = sizeof grp[ng];
            grp[ng].flags = SA1_COLGROUP_RAW_FLAGS;
            grp[ng].surface = 0x1u | (kk << 24);
            grp[ng].scale[0] = grp[ng].scale[1] = grp[ng].scale[2] = 1.0f;
            grp[ng].first_point = 4u * ng;
            grp[ng].num_points = 4;
            grp[ng].first_tri = 2u * ng;
            grp[ng].num_tris = 2;
            ng++;
        }
        memset(&d, 0, sizeof d);
        d.struct_size = sizeof d;
        d.mode = SA1_COLLISION_MODE_GRID;
        d.points = allp;
        d.num_points = 4u * ng;
        d.indices = alli;
        d.num_tris = 2u * ng;
        d.groups = grp;
        d.num_groups = ng;
        sa1_collision_set_static_ex(ctx, &d);
        if (sonic_at(ctx, 10, 20, 10, 0, &id) == SA1_OK) {
            run_init(&r, ctx, id);
            for (i = 0; i < 40 && r.st == SA1_OK; i++) {
                pair(&r, 0, i > 25 ? 1.0f : 0.0f, YAW_X, 0);
                n = 0;
                sa1_poll_events(ctx, ev, 64, &n);
                for (k = 0; k < n; k++) {
                    cap_near += ev[k].type == SA1_EVENT_CAPACITY && ev[k].id == SA1_CAPACITY_LAND_QUERY;
                }
            }
            wstats(&r, &ws);
            CHECK(r.st == SA1_OK && grounded(&r) && ws.over_near == 0 && cap_near == 0 && ws.window_objects >= ng
                      && ws.query_cap == 191u && ws.query_max >= ng && ws.hits_max >= ng && ws.hits_max <= 192u
                      && ws.hits_dropped == 0u && ws.hits_canary == 0u,
                  "%u stacked land objects at Sonic's feet: the window keeps %u (bound %u), %u steps left some out "
                  "(SA1_EVENT_CAPACITY %u: %u); his land collision gathered up to %u (arrays 192, canary clean: %u); "
                  "Sonic stands and runs (mode %d, y %.2f)", (unsigned)ng, (unsigned)ws.window_objects,
                  (unsigned)ws.query_cap, (unsigned)ws.over_near, (unsigned)SA1_CAPACITY_LAND_QUERY, (unsigned)cap_near,
                  (unsigned)ws.hits_max, (unsigned)ws.hits_canary, (int)r.x.mode, r.x.sa1_position[1]);
            sa1_character_destroy(ctx, id);
        } else {
            CHECK(0, "create: %s", sa1_last_error(ctx));
        }
        free(allp);
        free(alli);
        sa1_collision_set_static(ctx, NULL, 0);
    }
}

/* ---- GRID on Emerald Coast act 1 ---------------------------------------------------------------------- */
typedef struct ec1_res {
    uint32_t steps, first_mode, first_action, pos_bad, first_pos_bad, first_bits;
    double max_err, max_err_before;     /* largest position error (all compared steps / before the first mode diff) */
    int fault;
    double ms;                          /* time in the steps */
    uint32_t win_max, poly_max, over;   /* largest window, steps that left objects within 200 units out */
    uint32_t far;                       /* steps that left only farther objects out */
} ec1_res;

static void ec1_replay(sa1_context *ctx, const wr_trace *t, ec1_res *res, uint32_t stop)
{
    sa1_character_desc d;
    sa1_character_id id = 0;
    sa1g_world *w;
    uint32_t i;
    int k;
    sa1_status st;
    memset(res, 0, sizeof *res);
    res->first_mode = res->first_action = res->first_pos_bad = res->first_bits = 0xFFFFFFFFu;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.kind = t->h.chr;
    d.flags = SA1_CREATE_WORLD | SA1_CREATE_SA1_SPACE | SA1_CREATE_ANGLES | SA1_CREATE_STAGE;
    memcpy(d.position, t->h.start_pos, sizeof d.position);
    for (k = 0; k < 3; k++) {
        d.angle[k] = (int32_t)t->h.start_ang[k];
    }
    d.level = t->h.level;
    d.act = t->h.act;
    st = sa1_character_create_ex(ctx, &d, &id);
    w = sa1_character_world(ctx, id);
    if (st != SA1_OK || w == NULL) {
        printf("    create: %s: %s\n", sa1_status_string(st), sa1_last_error(ctx));
        res->fault = 1;
        return;
    }
    w->replay = 1;
    for (i = 0; i < t->n && i < stop; i++) {
        const sa1_trace_rec *r = &t->r[i];
        sa1_trace_rec mine;
        double e = 0.0;
        sa1ca_world_stats ws;
        double t0;
        wr_inject(w, r, SA1G_INJ_ALL, 0);
        t0 = sa1_now_ms();
        st = sa1g_world_step(w, (int32_t)r->sub);
        res->ms += sa1_now_ms() - t0;
        if (st != SA1_OK) {
            printf("    step %u: %s: %s\n", (unsigned)i, sa1g_status_string(st), w->fault.msg);
            res->fault = 1;
            break;
        }
        sa1ca_world_stats_get(w, &ws);
        res->win_max = ws.window_objects > res->win_max ? ws.window_objects : res->win_max;
        res->poly_max = ws.window_polygons > res->poly_max ? ws.window_polygons : res->poly_max;
        res->over = ws.over_objects + ws.over_polygons + ws.over_near;
        res->far = ws.over_far;
        wr_record(w, r, &mine);
        res->steps++;
        if (res->first_bits == 0xFFFFFFFFu
            && (memcmp(mine.pos, r->pos, 12) || memcmp(mine.mspd, r->mspd, 12) || memcmp(mine.floor_normal, r->floor_normal, 12)
                || memcmp(mine.pspd, r->pspd, 12) || mine.attr != r->attr)) {
            res->first_bits = i;
            if (g_verbose) {
                const float *a = (const float *)(const void *)(r->csts_raw + 0x3C), *c = (const float *)(const void *)(mine.csts_raw + 0x3C);
                printf("      first bit difference at step %u (mode %d/%d act %d):\n", (unsigned)i, r->mode, r->smode, r->action);
                printf("        pos   ref (%.9g %.9g %.9g) grid (%.9g %.9g %.9g)\n", r->pos[0], r->pos[1], r->pos[2],
                       mine.pos[0], mine.pos[1], mine.pos[2]);
                printf("        mspd  ref (%.9g %.9g %.9g) grid (%.9g %.9g %.9g)\n", r->mspd[0], r->mspd[1], r->mspd[2],
                       mine.mspd[0], mine.mspd[1], mine.mspd[2]);
                printf("        floor ref (%.9g %.9g %.9g) grid (%.9g %.9g %.9g) attr %08X / %08X\n", r->floor_normal[0],
                       r->floor_normal[1], r->floor_normal[2], mine.floor_normal[0], mine.floor_normal[1],
                       mine.floor_normal[2], (unsigned)r->attr, (unsigned)mine.attr);
                printf("        csts normal ref (%.9g %.9g %.9g) grid (%.9g %.9g %.9g); find_count %u / %u\n", a[0], a[1], a[2],
                       c[0], c[1], c[2], (unsigned)wr_u16(r->csts_raw + 0x28), (unsigned)wr_u16(mine.csts_raw + 0x28));
            }
        }
        if ((mine.mode != r->mode || mine.smode != r->smode) && res->first_mode == 0xFFFFFFFFu) {
            res->first_mode = i;
        }
        if (mine.action != r->action && res->first_action == 0xFFFFFFFFu) {
            res->first_action = i;
        }
        for (k = 0; k < 3; k++) {
            double dd = fabs((double)mine.pos[k] - (double)r->pos[k]);
            e = dd > e ? dd : e;
        }
        res->max_err = e > res->max_err ? e : res->max_err;
        if (res->first_mode == 0xFFFFFFFFu && res->first_action == 0xFFFFFFFFu) {
            res->max_err_before = e > res->max_err_before ? e : res->max_err_before;
        }
        if (e > 1e-3) {
            res->pos_bad++;
            if (res->first_pos_bad == 0xFFFFFFFFu) {
                res->first_pos_bad = i;
                if (g_verbose) {
                    printf("      first > 1e-3: step %u ref (%f %f %f) mode %d/%d act %d, grid (%f %f %f) mode %d/%d act %d\n",
                           (unsigned)i, r->pos[0], r->pos[1], r->pos[2], r->mode, r->smode, r->action, mine.pos[0],
                           mine.pos[1], mine.pos[2], mine.mode, mine.smode, mine.action);
                }
            }
        }
    }
    sa1_character_destroy(ctx, id);
}

static void test_ec1(G *E)
{
    static const char *const names[] = { "ec1_stand", "ec1_run_jump", "ec1_turns", "ec1_jump_hold", "ec1_spindash" };
    static const char *const cfg_name[] = { "the defaults (whole triangles in 256-unit cells, max edge 128)",
                                            "SA1_GRID_CUT at 256-unit cells, max edge 128",
                                            "SA1_GRID_CUT at 128-unit cells, max edge 128",
                                            "the defaults with orientation classes (round 0.9.2 F2: floors, walls and "
                                            "ceilings in objects of their own)" };
    sa1_context *ctx = E->ctx;
    char prefix[1100], path[1100];
    sa1ca_dump dump;
    sa1_collision_static d;
    sa1_collision_grid gp;
    sa1_collision_group *groups = NULL;
    sa1_collision_info info;
    uint32_t *idx = NULL, i, ng = 0, c;
    wr_trace tr[5];
    sa1_status st;
    int cfg;
    printf("ec1: GRID on the Emerald Coast act 1 landtable dump (L01A0: world-space triangles, raw COL words)\n");
    snprintf(prefix, sizeof prefix, "%s/build/traces/land/L01A0", E->sadc);
    st = sa1ca_dump_load(ctx, prefix, &dump);
    if (st != SA1_OK) {
        CHECK(0, "landtable dump %s: %s", prefix, sa1_last_error(ctx));
        return;
    }
    memset(tr, 0, sizeof tr);
    for (i = 0; i < 5; i++) {
        snprintf(path, sizeof path, "%s/build/traces/ref/%s.bin", E->sadc, names[i]);
        if (wr_trace_load(path, &tr[i]) != 0) {
            CHECK(0, "reference trace %s", path);
        }
    }
    /* one group per collidable COL, world-space corners (the game's njCalcPoint results) as its points */
    groups = (sa1_collision_group *)calloc(dump.ncol, sizeof *groups);
    idx = (uint32_t *)malloc((size_t)dump.ntri * 3u * sizeof(uint32_t));
    for (i = 0; i < dump.ntri; i++) {
        idx[3 * i] = 0;
        idx[3 * i + 1] = 1;
        idx[3 * i + 2] = 2;
    }
    for (c = 0; c < dump.ncol; c++) {
        const sa1_collision_group *g0 = &dump.groups[c];
        sa1_collision_group *g = &groups[ng];
        if (!(g0->surface & 0x400003u) || g0->num_tris == 0) {
            continue;
        }
        memset(g, 0, sizeof *g);
        g->struct_size = sizeof *g;
        g->flags = SA1_COLGROUP_RAW_FLAGS;
        g->surface = g0->surface;
        g->scale[0] = g->scale[1] = g->scale[2] = 1.0f;
        g->first_point = 3u * g0->first_tri;
        g->num_points = 3u * g0->num_tris;
        g->first_tri = g0->first_tri;
        g->num_tris = g0->num_tris;
        ng++;
    }
    /* indices are relative to the group's first point: triangle t of the group uses points 3t .. 3t + 2 */
    for (c = 0; c < ng; c++) {
        for (i = 0; i < groups[c].num_tris; i++) {
            idx[3u * (groups[c].first_tri + i)] = 3u * i;
            idx[3u * (groups[c].first_tri + i) + 1] = 3u * i + 1;
            idx[3u * (groups[c].first_tri + i) + 2] = 3u * i + 2;
        }
    }
    for (cfg = 0; cfg < 4; cfg++) {
        memset(&gp, 0, sizeof gp);
        gp.struct_size = sizeof gp;
        if (cfg == 1) {
            gp.flags = SA1_GRID_CUT;
        } else if (cfg == 2) {
            gp.flags = SA1_GRID_CUT;
            gp.cell_size = 128.0f;
        }
        if (cfg < 3) {
            /* the comparison with the game's own stage: the partition before round 0.9.2 (one object per cell and COL
               word), with which the 5 scripts keep the reference's modes and actions */
            gp.flags |= SA1_GRID_MIXED_ORIENTATIONS;
        }
        memset(&d, 0, sizeof d);
        d.struct_size = sizeof d;
        d.mode = SA1_COLLISION_MODE_GRID;
        d.flags = SA1_COLLISION_SA1_SPACE;
        d.points = dump.world;
        d.num_points = 3u * dump.ntri;
        d.indices = idx;
        d.materials = dump.materials;
        d.tri_flags = dump.tri_flags;
        d.num_tris = dump.ntri;
        d.groups = groups;
        d.num_groups = ng;
        d.grid = &gp;
        st = sa1_collision_set_static_ex(ctx, &d);
        if (st != SA1_OK) {
            CHECK(0, "set_static_ex(GRID): %s", sa1_last_error(ctx));
            break;
        }
        printf("  %s:\n", cfg_name[cfg]);
        for (i = 0; i < 5; i++) {
            ec1_res res;
            uint32_t stop = tr[i].n, firstdiff;
            int spindash = !strcmp(names[i], "ec1_spindash");
            if (tr[i].n == 0) {
                continue;
            }
            if (spindash) {
                stop = 144;     /* from there the reference player is in the STG01 water plane (replay_test.c) */
            }
            ec1_replay(ctx, &tr[i], &res, stop);
            if (i == 0) {
                memset(&info, 0, sizeof info);
                info.struct_size = sizeof info;
                sa1_collision_info_get(ctx, &info);
                INFO("%u collidable COLs -> %u objects, %u triangles (%u split, %u cut, %u dropped, %u undersides within "
                     "reach left out), %u bytes", (unsigned)ng, (unsigned)info.grid_objects, (unsigned)info.grid_triangles,
                     (unsigned)info.grid_split, (unsigned)info.grid_cut, (unsigned)info.grid_dropped,
                     (unsigned)info.grid_undersides, (unsigned)info.bytes);
            }
            firstdiff = res.first_mode < res.first_action ? res.first_mode : res.first_action;
            if (firstdiff == 0xFFFFFFFFu) {
                snprintf(path, sizeof path, "identical");
            } else {
                snprintf(path, sizeof path, "differs from step %u", (unsigned)firstdiff);
            }
            printf("    %-14s %3u steps: mode/action sequence %s; position error max %.3g (%.3g before any mode/action "
                   "difference), %u steps > 1e-3 (first %d); bit-identical physics to step %d; window up to %u objects / "
                   "%u polygons, %u steps over a limit (%u full beyond 200 units); %.4f ms per step\n", names[i],
                   (unsigned)res.steps, path, res.max_err, res.max_err_before, (unsigned)res.pos_bad,
                   res.first_pos_bad == 0xFFFFFFFFu ? -1 : (int)res.first_pos_bad,
                   res.first_bits == 0xFFFFFFFFu ? (int)res.steps : (int)res.first_bits - 1, (unsigned)res.win_max,
                   (unsigned)res.poly_max, (unsigned)res.over, (unsigned)res.far, res.steps ? res.ms / res.steps : 0.0);
            if (cfg == 3 && !strcmp(names[i], "ec1_turns")) {
                /* F2 regroups EC1's walls: one turn against a wall resolves differently (measured: from step 156) */
                printf("  known %s with orientation classes: %s (the game's own partition mixes walls and floors: "
                       "SA1_GRID_MIXED_ORIENTATIONS above keeps it)\n", names[i], firstdiff == 0xFFFFFFFFu ?
                       "the reference mode/action sequence" : path);
                g_known += firstdiff != 0xFFFFFFFFu;
                CHECK(!res.fault && res.steps == stop, "%s: runs %u steps without a fault", names[i], (unsigned)res.steps);
                continue;
            }
            CHECK(!res.fault && res.steps == stop && firstdiff == 0xFFFFFFFFu && (res.over == 0 || cfg == 2),
                  "%s: the reference mode/action sequence over %u steps%s", names[i], (unsigned)res.steps,
                  spindash ? " (to step 143: then the reference is in the STG01 water plane)" : "");
            if ((cfg == 0 || cfg == 3) && (i == 0 || i == 3)) {
                CHECK(res.first_bits == 0xFFFFFFFFu, "%s: uncut triangles: every step bit-identical to the reference "
                      "(position, speeds, floor normal, attribute)", names[i]);
            }
        }
    }
    for (i = 0; i < 5; i++) {
        wr_trace_free(&tr[i]);
    }
    free(groups);
    free(idx);
    sa1ca_dump_free(ctx, &dump);
    sa1_collision_set_static(ctx, NULL, 0);
}

/* ---- main ---------------------------------------------------------------------------------------------- */
typedef struct test {
    const char *name;
    void (*fn)(G *);
} test;

/* ---- stacked floors ---------------------------------------------------------------------------------- */
#define STK_NEAR_COUNT 0x8C73E27Cu      /* B_8C73E27C: entries of the NEAR-poly list of the last 8C028BFE call */
#define STK_NEAR_MAX   96               /* the game's B_8C73E280[96] (host worlds: 512, sa1ca_world_stats.near_cap) */

static int near_count(const run *r)
{
    sa1g_world *w = world_of(r);
    int32_t n = 0;
    const void *p;
    if (w == NULL) {
        return -1;
    }
    p = sa1g_world_ptr(w, STK_NEAR_COUNT);
    if (p == NULL) {
        return -1;
    }
    memcpy(&n, p, 4);
    return (int)n;
}

/* b's triangles through set_static_ex as one GRID group (every triangle has b->t[0].surface) */
static sa1_status set_static_grid(sa1_context *ctx, const tbuf *b, float cell, uint32_t flags)
{
    sa1_collision_static d;
    sa1_collision_grid gp;
    sa1_collision_group grp;
    float *pts;
    uint32_t *idx, i, k;
    sa1_status st;
    pts = (float *)malloc((size_t)b->n * 9u * sizeof(float));
    idx = (uint32_t *)malloc((size_t)b->n * 3u * sizeof(uint32_t));
    if (pts == NULL || idx == NULL) {
        free(pts);
        free(idx);
        return SA1_ERR_OUT_OF_MEMORY;
    }
    for (i = 0; i < b->n; i++) {
        memcpy(pts + 9u * i, b->t[i].v, 9u * sizeof(float));
        for (k = 0; k < 3; k++) {
            idx[3u * i + k] = 3u * i + k;
        }
    }
    memset(&gp, 0, sizeof gp);
    gp.struct_size = sizeof gp;
    gp.flags = flags;
    gp.cell_size = cell;
    memset(&grp, 0, sizeof grp);
    grp.struct_size = sizeof grp;
    grp.surface = b->n ? b->t[0].surface : SA1_SURFACE_SOLID;
    grp.num_points = 3u * b->n;
    grp.num_tris = b->n;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.mode = SA1_COLLISION_MODE_GRID;
    d.points = pts;
    d.num_points = 3u * b->n;
    d.indices = idx;
    d.num_tris = b->n;
    d.groups = &grp;
    d.num_groups = 1;
    d.grid = &gp;
    st = sa1_collision_set_static_ex(ctx, &d);
    free(pts);
    free(idx);
    return st;
}

typedef struct stk_case {
    const char *name;
    float h;                    /* top of the upper floor (the lower one is y = 0) */
    float thick;                /* slab thickness; 0 = a single upward plane */
    float ux0, ux1, uz0, uz1;   /* the upper floor's extent; Sonic runs along +x across it and off ux1 */
    float sz;                   /* Sonic's z */
    int ramp;                   /* 1: start on the lower floor and run up a 20 degree ramp ending at ux0 */
    int stack;                  /* > 0: the upper slab built of `stack` stacked slabs (hidden faces between them) */
    int known;                  /* a game limit: a failure counts as a known issue */
} stk_case;

typedef struct stk_result {
    int created, landed_upper, reached_upper, sank, below_lower, jumped, jump_top_ok, relanded_upper, lower_after;
    float land_y, min_y_over, top;
    int near_max, near_full, near_cap;
    uint32_t pairs;
    char log[1024];
} stk_result;

static void stk_scene(tbuf *b, const stk_case *c)
{
    memset(b, 0, sizeof *b);
    tb_floor(b, -2560, 2560, -1024, 1024, 0, 1, SA1_SURFACE_SOLID);
    if (c->stack > 0) {
        int k;
        float t = c->thick / (float)c->stack;
        for (k = 0; k < c->stack; k++) {
            tb_box(b, c->ux0, c->ux1, c->h - c->thick + t * (float)k, c->h - c->thick + t * (float)(k + 1), c->uz0, c->uz1,
                   SA1_SURFACE_SOLID);
        }
    } else if (c->thick > 0.0f) {
        tb_box(b, c->ux0, c->ux1, c->h - c->thick, c->h, c->uz0, c->uz1, SA1_SURFACE_SOLID);
    } else {
        tb_floor(b, c->ux0, c->ux1, c->uz0, c->uz1, c->h, 1, SA1_SURFACE_SOLID);
    }
    if (c->ramp) {
        /* 20 degrees up to the upper floor's -x edge */
        float len = c->h / 0.36397023f, p[4][3], n[3] = { 0, 1, 0 };
        p[0][0] = c->ux0 - len; p[0][1] = 0;    p[0][2] = c->uz0;
        p[1][0] = c->ux0;       p[1][1] = c->h; p[1][2] = c->uz0;
        p[2][0] = c->ux0;       p[2][1] = c->h; p[2][2] = c->uz1;
        p[3][0] = c->ux0 - len; p[3][1] = 0;    p[3][2] = c->uz1;
        tb_quad(b, p, n, SA1_SURFACE_SOLID);
    }
}

/* config 0: set_static (defaults), 1: SA1_GRID_CUT at 256-unit cells, 2: SA1_GRID_CUT at 128-unit cells */
static void stk_run(sa1_context *ctx, const stk_case *c, int cfg, stk_result *R)
{
    sa1_character_id id = 0;
    tbuf b;
    run r;
    int i, over, jump_left = -1, air = 0;
    float sx, sy, margin = 6.0f;
    sa1_status st;
    memset(R, 0, sizeof *R);
    R->min_y_over = 1e9f;
    stk_scene(&b, c);
    st = cfg == 0 ? set_static(ctx, &b) : set_static_grid(ctx, &b, cfg == 1 ? 256.0f : 128.0f, SA1_GRID_CUT);
    tb_free(&b);
    if (st != SA1_OK) {
        printf("    set_static: %s\n", sa1_last_error(ctx));
        return;
    }
    if (c->ramp) {
        sx = c->ux0 - c->h / 0.36397023f - 300.0f;
        sy = 3.0f;
    } else {
        sx = c->ux0 + 60.0f;
        sy = c->h + 6.0f;
    }
    if (sonic_at(ctx, sx, sy, c->sz, 0, &id) != SA1_OK) {
        printf("    create: %s\n", sa1_last_error(ctx));
        return;
    }
    R->created = 1;
    run_init(&r, ctx, id);
    for (i = 0; i < 20 && r.st == SA1_OK; i++) {
        pair(&r, 0, 0, 0, 0);
    }
    R->land_y = r.x.sa1_position[1];
    R->landed_upper = !c->ramp && grounded(&r) && fabsf(r.x.sa1_position[1] - c->h) < 0.5f;
    R->reached_upper = R->landed_upper;
    for (i = 0; i < 400 && r.st == SA1_OK; i++) {
        uint32_t btn = 0;
        float x, y, z;
        int n;
        if (jump_left < 0 && R->reached_upper && r.x.sa1_position[0] > c->ux0 + 400.0f && grounded(&r)) {
            jump_left = 6;      /* jump once, over the upper floor */
            R->jumped = 1;
        }
        if (jump_left > 0) {
            btn = SA1_BUTTON_JUMP;
            jump_left--;
        }
        pair(&r, 0, 1, YAW_X, btn);
        n = near_count(&r);
        R->near_max = n > R->near_max ? n : R->near_max;
        {
            sa1ca_world_stats ws;
            wstats(&r, &ws);
            R->near_full += n >= (int)(ws.near_cap ? ws.near_cap : STK_NEAR_MAX);
            R->near_cap = (int)(ws.near_cap ? ws.near_cap : STK_NEAR_MAX);
        }
        x = r.x.sa1_position[0];
        y = r.x.sa1_position[1];
        z = r.x.sa1_position[2];
        over = x > c->ux0 + margin && x < c->ux1 - margin && z > c->uz0 + margin && z < c->uz1 - margin;
        if (!R->reached_upper && grounded(&r) && fabsf(y - c->h) < 0.5f && over) {
            R->reached_upper = 1;
        }
        if (R->reached_upper && over) {
            R->min_y_over = y < R->min_y_over ? y : R->min_y_over;
            R->sank += y < c->h - 1.0f;
        }
        if (y < -1.0f) {
            R->below_lower++;
        }
        if (R->jumped && jump_left >= 0 && !R->relanded_upper) {
            if (!grounded(&r)) {
                air++;
                R->top = y - c->h > R->top ? y - c->h : R->top;
            } else if (air > 3 && fabsf(y - c->h) < 0.5f) {
                R->relanded_upper = 1;
            }
        }
        if (x > c->ux1 + 150.0f && grounded(&r) && fabsf(y) < 0.5f) {
            R->lower_after = 1;
            break;
        }
    }
    R->jump_top_ok = R->top > 20.0f;
    R->pairs = r.pairs;
    memcpy(R->log, r.log, sizeof R->log);
    sa1_character_destroy(ctx, id);
}

static void test_stacked(G *E)
{
    static const stk_case cases[] = {
        /* name                           h      thick  ux0     ux1    uz0     uz1    sz    ramp stack known */
        { "gap 64",                       64.0f,  8.0f, -320.0f, 900.0f, -300.0f, 300.0f,  0.0f, 0, 0, 0 },
        { "gap 128",                     128.0f,  8.0f, -320.0f, 900.0f, -300.0f, 300.0f,  0.0f, 0, 0, 0 },
        { "gap 256",                     256.0f,  8.0f, -320.0f, 900.0f, -300.0f, 300.0f,  0.0f, 0, 0, 0 },
        { "thin roof 4",                 128.0f,  4.0f, -320.0f, 900.0f, -300.0f, 300.0f,  0.0f, 0, 0, 0 },
        { "thin roof 2",                 128.0f,  2.0f, -320.0f, 900.0f, -300.0f, 300.0f,  0.0f, 0, 0, 0 },
        { "thin roof 2 (z 60)",          128.0f,  2.0f, -320.0f, 900.0f, -300.0f, 300.0f, 60.0f, 0, 0, 0 },
        { "single plane",                128.0f,  0.0f, -320.0f, 900.0f, -300.0f, 300.0f,  0.0f, 0, 0, 0 },
        { "ramp up",                     100.0f,  8.0f, -100.0f, 900.0f, -300.0f, 300.0f,  0.0f, 1, 0, 0 },
        { "top on a cell boundary",      256.0f, 16.0f, -320.0f, 900.0f, -300.0f, 300.0f,  0.0f, 0, 0, 0 },
        { "edges on cell boundaries",    128.0f,  8.0f, -256.0f, 768.0f, -256.0f, 256.0f,  0.0f, 0, 0, 0 },
        { "edges on 128 boundaries",     128.0f,  8.0f, -384.0f, 640.0f, -128.0f, 128.0f, -0.5f, 0, 0, 0 },
        /* thinner than 2 units: the underside comes within the reach of the land collision's sphere (radius grown by
         * the speed) from above and its pushback pulls Sonic down a few units now and then (G-SA1 thickens such
         * floors: BspWorldOptions::thin_floor_min) */
        { "thin roof 1",                 128.0f,  1.0f, -320.0f, 900.0f, -300.0f, 300.0f, 60.0f, 0, 0, 1 },
        { "thin roof 0.5",               128.0f,  0.5f, -320.0f, 900.0f, -300.0f, 300.0f,  0.0f, 0, 0, 1 },
    };
    static const char *const cfg_name[] = { "defaults", "CUT 256", "CUT 128" };
    stk_case bad;
    stk_result R;
    int ci, cfg, near_max = 0;
    printf("stacked: a floor above a floor (the lower one at y = 0), Sonic lands on the upper one, runs along +x across "
           "it (jumping on the way) and off its far edge\n");
    for (ci = 0; ci < (int)(sizeof cases / sizeof cases[0]); ci++) {
        const stk_case *c = &cases[ci];
        for (cfg = 0; cfg < 3; cfg++) {
            int ok;
            stk_run(E->ctx, c, cfg, &R);
            ok = R.created && R.reached_upper && (c->ramp || R.landed_upper) && R.sank == 0 && R.below_lower == 0
                 && R.jumped && R.jump_top_ok && R.relanded_upper && R.lower_after && R.near_full == 0;
            near_max = R.near_max > near_max ? R.near_max : near_max;
            if (c->known && !ok) {
                g_known++;
                INFO("known (thinner than 2 units): %-14s %-8s: lowest %.2f over it (%d pairs below %.2f)", c->name,
                     cfg_name[cfg], R.min_y_over < 1e8f ? R.min_y_over : -1.0f, R.sank, c->h - 1.0f);
                continue;
            }
            CHECK(ok,
                  "%-25s %-8s: %s y %.2f; over it never below %.2f (lowest %.2f, %d pairs sank); jump +%.1f, back on it: "
                  "%s; off the edge onto the lower floor: %s%s; NEAR-poly list up to %d of %d",
                  c->name, cfg_name[cfg], c->ramp ? "up the ramp, reaches the upper floor:" : "lands on the upper floor:",
                  c->ramp ? (R.reached_upper ? c->h : -1.0f) : R.land_y, c->h - 1.0f,
                  R.min_y_over < 1e8f ? R.min_y_over : -1.0f, R.sank, R.top, R.relanded_upper ? "yes" : "NO",
                  R.lower_after ? "yes" : "NO", R.below_lower ? " (BELOW the lower floor)" : "", R.near_max, R.near_cap);
            if (!ok || g_verbose) {
                printf("    mode/action/step: %s\n", R.log);
            }
        }
    }
    INFO("largest NEAR-poly count in these scenes: %d (the list holds %d)", near_max, R.near_cap);
    /* the gm_construct failure: hidden faces between stacked brushes */
    bad = cases[1];
    bad.name = "stacked slabs";
    bad.thick = 16.0f;
    bad.stack = 40;
    stk_run(E->ctx, &bad, 0, &R);
    if (R.near_full > 0 && (R.sank > 0 || !R.landed_upper)) {
        g_known++;
        INFO("known (a game limit): an upper floor built of 40 stacked slabs (78 hidden faces under its top): the "
             "NEAR-poly list is full in %d pairs (up to %d of %d) and Sonic does not stay on the floor (landed at y %.2f, "
             "lowest %.2f over it); hosts must not send hidden faces (G-SA1: BspWorldOptions::remove_hidden)",
             R.near_full, R.near_max, R.near_cap, R.land_y, R.min_y_over < 1e8f ? R.min_y_over : -1.0f);
    } else {
        INFO("an upper floor built of 40 stacked slabs: NEAR-poly list up to %d, full in %d pairs, landed at y %.2f, "
             "lowest %.2f over it", R.near_max, R.near_full, R.land_y, R.min_y_over < 1e8f ? R.min_y_over : -1.0f);
    }
    sa1_collision_set_static(E->ctx, NULL, 0);
}

/* ---- long faces: the needle plank and a gm_freespace_13-like ramp (sadc docs/research/gsa1_ramp_freeze.md) ---------- */
#define PLK_LEN   1328.0f       /* gm_freespace_13's big ramp in SA1 units: a 5312 x 236 x 12 HU plank at 24.5 degrees */
#define PLK_HALFW 29.5f
#define PLK_T     3.0f

/* a box sheared along a slope (as a BSP brush comes out: every face a quad of 2 long triangles): x0..x1 horizontally,
 * bottom y = y0 + (x - x0) * t, height h, z0..z1, faces outward */
static void tb_sbox(tbuf *b, float x0, float x1, float y0, float t, float h, float z0, float z1)
{
    float ya = y0, yb = y0 + (x1 - x0) * t, nup[3] = { -t, 1, 0 }, ndn[3] = { t, -1, 0 };
    float top[4][3] = { { x0, ya + h, z0 }, { x1, yb + h, z0 }, { x1, yb + h, z1 }, { x0, ya + h, z1 } };
    float bot[4][3] = { { x0, ya, z0 }, { x1, yb, z0 }, { x1, yb, z1 }, { x0, ya, z1 } };
    float s0[4][3] = { { x0, ya, z0 }, { x1, yb, z0 }, { x1, yb + h, z0 }, { x0, ya + h, z0 } };
    float s1[4][3] = { { x0, ya, z1 }, { x1, yb, z1 }, { x1, yb + h, z1 }, { x0, ya + h, z1 } };
    float e0[4][3] = { { x0, ya, z0 }, { x0, ya, z1 }, { x0, ya + h, z1 }, { x0, ya + h, z0 } };
    float e1[4][3] = { { x1, yb, z0 }, { x1, yb, z1 }, { x1, yb + h, z1 }, { x1, yb + h, z0 } };
    float nz0[3] = { 0, 0, -1 }, nz1[3] = { 0, 0, 1 }, nx0[3] = { -1, 0, 0 }, nx1[3] = { 1, 0, 0 };
    tb_quad(b, top, nup, SA1_SURFACE_SOLID);
    tb_quad(b, bot, ndn, SA1_SURFACE_SOLID);
    tb_quad(b, s0, nz0, SA1_SURFACE_SOLID);
    tb_quad(b, s1, nz1, SA1_SURFACE_SOLID);
    tb_quad(b, e0, nx0, SA1_SURFACE_SOLID);
    tb_quad(b, e1, nx1, SA1_SURFACE_SOLID);
}

static float plk_slope(void)
{
    return (float)tan(24.5 * PI / 180.0);
}

/* the plank's top surface at x (the ground before it, the platform after it) */
static float plk_surf(float x)
{
    return x <= 0.0f ? 0.0f : x < PLK_LEN ? x * plk_slope() : PLK_LEN * plk_slope();
}

/* full: 0 = the needle scene of ramp_diag (the plank, the platform at its top, the ground under both); 1 = the
 * gm_freespace_13-like ramp: also two rails along the plank, three beams under it (6 x 6, their tops 7 units under the
 * plank's underside) and six posts from the ground (the layers that filled the NEAR-poly list there) */
static void plk_scene(tbuf *b, int full)
{
    float t = plk_slope(), H = PLK_LEN * plk_slope(), up[3] = { 0, 1, 0 };
    float fl[4][3] = { { -3000, 0, -600 }, { 2400, 0, -600 }, { 2400, 0, 600 }, { -3000, 0, 600 } };
    int k;
    memset(b, 0, sizeof *b);
    tb_quad(b, fl, up, SA1_SURFACE_SOLID);
    tb_sbox(b, 0, PLK_LEN, -PLK_T, t, PLK_T, -PLK_HALFW, PLK_HALFW);
    tb_sbox(b, PLK_LEN, PLK_LEN + 400, H - PLK_T, 0, PLK_T, -PLK_HALFW - 40, PLK_HALFW + 40);
    if (!full) {
        return;
    }
    tb_sbox(b, 0, PLK_LEN, -PLK_T, t, PLK_T + 6, -PLK_HALFW - 2, -PLK_HALFW);
    tb_sbox(b, 0, PLK_LEN, -PLK_T, t, PLK_T + 6, PLK_HALFW, PLK_HALFW + 2);
    for (k = -1; k <= 1; k++) {
        float z = 20.0f * (float)k;
        tb_sbox(b, 30, PLK_LEN, 30 * t - PLK_T - 7, t, 6, z - 3, z + 3);
    }
    for (k = 0; k < 6; k++) {
        float x = 100.0f + 230.0f * (float)k;
        tb_sbox(b, x, x + 6, 0, 0, x * t - PLK_T - 8, -6, 6);
    }
}

typedef struct plk_res {
    int fell, reached, created;
    float x, y, deepest;
    uint32_t near_max, near_reports;
} plk_res;

/* one run: approach 0 run, 1 boost6, 2 boost10, 3 boost16 (ground speed set at x > -150, a dash panel), 4 spin dash
 * (Sonic), 5..7 created 4 units above the plank at x 300 / 700 / 1100 and standing */
static void plk_run(sa1_context *ctx, int32_t kind, int ap, plk_res *R)
{
    static const float boost[] = { 0, 6, 10, 16, 0 }, drop_x[] = { 300, 700, 1100 };
    sa1_character_desc d;
    sa1_character_id id = 0;
    sa1_event ev[64];
    sa1ca_world_stats ws;
    run r;
    int i, below = 0, boosted = 0;
    uint32_t n, k;
    memset(R, 0, sizeof *R);
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.kind = kind;
    d.flags = SA1_CREATE_WORLD | SA1_CREATE_SA1_SPACE | SA1_CREATE_ANGLES;
    d.position[0] = ap >= 5 ? drop_x[ap - 5] : ap == 4 ? -400.0f : ap == 0 ? -600.0f : -800.0f;
    d.position[1] = plk_surf(d.position[0]) + 4.0f;
    if (sa1_character_create_ex(ctx, &d, &id) != SA1_OK) {
        return;
    }
    R->created = 1;
    run_init(&r, ctx, id);
    for (i = 0; i < 520 && r.st == SA1_OK; i++) {
        float sy = ap >= 5 || i < 8 ? 0.0f : 1.0f, dep = 0.0f;
        uint32_t btn = 0;
        if (ap == 4 && i >= 8 && i < 38) {
            btn = SA1_BUTTON_ACTION;
            sy = 0.0f;
        }
        pair(&r, 0, sy, YAW_X, btn);
        n = 0;
        sa1_poll_events(ctx, ev, 64, &n);
        for (k = 0; k < n; k++) {
            R->near_reports += ev[k].type == SA1_EVENT_CAPACITY && ev[k].id == SA1_CAPACITY_NEAR_POLYGONS;
        }
        if (ap >= 1 && ap <= 3 && !boosted && r.x.sa1_position[0] > -150.0f) {
            sa1g_player_ptrs pp;
            sa1g_world_player(world_of(&r), &pp);
            if (pp.pwp != 0) {
                memcpy((uint8_t *)sa1g_world_ptr(world_of(&r), pp.pwp) + 0x38, &boost[ap], 4);   /* playerwk.spd.x */
            }
            boosted = 1;
        }
        if (r.x.sa1_position[0] > 1.0f && r.x.sa1_position[0] < PLK_LEN + 380.0f
            && fabsf(r.x.sa1_position[2]) < PLK_HALFW - 6.0f) {
            dep = plk_surf(r.x.sa1_position[0]) - r.x.sa1_position[1];
        }
        R->deepest = dep > R->deepest ? dep : R->deepest;
        if (dep > 3.0f) {
            if (++below >= 3) {
                R->fell = 1;        /* more than 3 below the surface for 6 steps */
                break;
            }
        } else {
            below = 0;
        }
        if (r.x.sa1_position[0] > PLK_LEN + 60.0f && grounded(&r)) {
            R->reached = 1;
            break;
        }
        if (ap >= 5 && i >= 60) {
            break;
        }
    }
    R->x = r.x.sa1_position[0];
    R->y = r.x.sa1_position[1];
    wstats(&r, &ws);
    R->near_max = ws.near_max;
    sa1_character_destroy(ctx, id);
}

/* cfg 0: set_static (GRID defaults); 1: SA1_GRID_DC_LIMITS (the game's 96-entry NEAR-poly list) */
static void plk_test(G *E, int full, int cfg)
{
    static const int32_t kinds[] = { SA1_CHAR_SONIC, SA1_CHAR_TAILS, SA1_CHAR_KNUCKLES };
    static const char *const apn[] = { "run", "boost6", "boost10", "boost16", "spindash", "drop@300", "drop@700",
                                       "drop@1100" };
    sa1_collision_info info;
    tbuf b;
    int ki, ap, runs = 0, falls = 0, created = 0;
    uint32_t near_max = 0, reports = 0;
    char line[2048];
    size_t len = 0;
    sa1_status st;
    plk_scene(&b, full);
    st = cfg == 0 ? set_static(E->ctx, &b) : set_static_grid(E->ctx, &b, 0.0f, SA1_GRID_DC_LIMITS);
    if (st != SA1_OK) {
        CHECK(0, "set_static: %s", sa1_last_error(E->ctx));
        tb_free(&b);
        return;
    }
    line[0] = 0;
    for (ki = 0; ki < 3; ki++) {
        for (ap = 0; ap < 8; ap++) {
            plk_res R;
            if (ap == 4 && kinds[ki] != SA1_CHAR_SONIC) {
                continue;
            }
            plk_run(E->ctx, kinds[ki], ap, &R);
            runs++;
            created += R.created;
            falls += R.fell;
            near_max = R.near_max > near_max ? R.near_max : near_max;
            reports += R.near_reports;
            if ((R.fell || g_verbose) && len + 128 < sizeof line) {
                len += (size_t)snprintf(line + len, sizeof line - len, " %s %s %s at x %.0f (deepest %.1f, NEAR %u);",
                                        sa1_character_name(kinds[ki]), apn[ap], R.fell ? "FELL" : R.reached ? "up" : "stood",
                                        R.x, R.deepest, (unsigned)R.near_max);
            }
        }
    }
    memset(&info, 0, sizeof info);
    info.struct_size = sizeof info;
    sa1_collision_info_get(E->ctx, &info);
    if (cfg == 0) {
        CHECK(created == runs && falls == 0 && near_max < 256u && reports == 0,
              "%s (%u long triangles in, %u GRID triangles): %d runs (Sonic, Tails, Knuckles: run, dash panels 6 / 10 / 16, "
              "spin dash, created above it and standing; %d created), %d fell through; the fullest NEAR-poly list %u of "
              "256, SA1_CAPACITY_NEAR_POLYGONS reported %u time(s)%s", full ? "a gm_freespace_13-like ramp" : "the needle plank",
              (unsigned)b.n, (unsigned)info.grid_triangles, runs, created, falls, (unsigned)near_max, (unsigned)reports,
              len ? line : "");
    } else {
        INFO("the same with SA1_GRID_DC_LIMITS (the game's 96-entry list): %d of %d runs fell through, the list full in "
             "%u reports; the fullest list %u%s", falls, runs, (unsigned)reports, (unsigned)near_max, len ? line : "");
    }
    tb_free(&b);
}

static void test_needle(G *E)
{
    printf("needle: gm_freespace_13's big ramp rebuilt: a plank 1328 x 59 x 3 at 24.5 degrees, each face 2 long triangles "
           "(longest-edge halving made them into needles that filled the 96-entry NEAR-poly list)\n");
    plk_test(E, 0, 0);
}

static void test_plank(G *E)
{
    printf("plank: the needle plank with what lies around it on gm_freespace_13 (rails, beams 7 units under its underside, "
           "posts): before the well-shaped GRID cut (2026-10-20) 19 of 24 such runs fell through, the list always full\n");
    plk_test(E, 1, 0);
    plk_test(E, 1, 1);
    sa1_collision_set_static(E->ctx, NULL, 0);
}

/* ---- undersides within reach (GRID c2; sadc docs/research/gsa1_ramp_freeze.md, mechanism B) ------------------------ */
#define UND_H 300.0f

/* ramp_diag's slab scene: the floor y = 0 (on under everything), the slope deg degrees from x = 0 up to y = UND_H as a slab
 * t thick (top, underside, foot and head ends, sides), a platform t thick and 1200 long at the top; 1200 wide (z +-600);
 * every face a quad of 2 triangles */
static void und_scene(tbuf *b, float deg, float t, float *x_top)
{
    float s = sinf(deg * (float)PI / 180.0f), c = cosf(deg * (float)PI / 180.0f), xt = UND_H / s * c, W = 600.0f;
    float nx = -s, ny = c, a2x = t * s, a2y = -t * c, b2x = xt + t * s, b2y = UND_H - t * c;
    float top[4][3] = { { 0, 0, -W }, { xt, UND_H, -W }, { xt, UND_H, W }, { 0, 0, W } };
    float und[4][3] = { { a2x, a2y, -W }, { b2x, b2y, -W }, { b2x, b2y, W }, { a2x, a2y, W } };
    float foot[4][3] = { { a2x, a2y, -W }, { 0, 0, -W }, { 0, 0, W }, { a2x, a2y, W } };
    float head[4][3] = { { xt, UND_H, -W }, { b2x, b2y, -W }, { b2x, b2y, W }, { xt, UND_H, W } };
    float s0[4][3] = { { 0, 0, -W }, { xt, UND_H, -W }, { b2x, b2y, -W }, { a2x, a2y, -W } };
    float s1[4][3] = { { 0, 0, W }, { xt, UND_H, W }, { b2x, b2y, W }, { a2x, a2y, W } };
    float n_top[3] = { nx, ny, 0 }, n_und[3] = { -nx, -ny, 0 }, n_foot[3] = { -c, -s, 0 }, n_head[3] = { c, s, 0 };
    float n_s0[3] = { 0, 0, -1 }, n_s1[3] = { 0, 0, 1 };
    memset(b, 0, sizeof *b);
    tb_floor(b, -4200.0f, xt + 1800.0f, -W - 300.0f, W + 300.0f, 0.0f, 1.0f, SA1_SURFACE_SOLID);
    tb_quad(b, top, n_top, SA1_SURFACE_SOLID);
    tb_quad(b, und, n_und, SA1_SURFACE_SOLID);
    tb_quad(b, foot, n_foot, SA1_SURFACE_SOLID);
    tb_quad(b, head, n_head, SA1_SURFACE_SOLID);
    tb_quad(b, s0, n_s0, SA1_SURFACE_SOLID);
    tb_quad(b, s1, n_s1, SA1_SURFACE_SOLID);
    tb_box(b, xt, xt + 1200.0f, UND_H - t, UND_H, -W, W, SA1_SURFACE_SOLID);
    *x_top = xt;
}

typedef struct und_res {
    int fell, reached;
    float deepest, x;
} und_res;

/* from x = -800 at full stick, the ground speed set to `boost` at x > -150 (a dash panel), up to the platform */
static void und_run(sa1_context *ctx, int32_t kind, float deg, float xt, float boost, und_res *R)
{
    sa1_character_desc d;
    sa1_character_id id = 0;
    run r;
    int i, below = 0, boosted = 0;
    float tn = tanf(deg * (float)PI / 180.0f);
    memset(R, 0, sizeof *R);
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.kind = kind;
    d.flags = SA1_CREATE_WORLD | SA1_CREATE_SA1_SPACE | SA1_CREATE_ANGLES;
    d.position[0] = -800.0f;
    d.position[1] = 3.0f;
    if (sa1_character_create_ex(ctx, &d, &id) != SA1_OK) {
        R->fell = 1;
        return;
    }
    run_init(&r, ctx, id);
    for (i = 0; i < 600 && r.st == SA1_OK; i++) {
        float x, dep = 0.0f;
        pair(&r, 0, i < 8 ? 0.0f : 1.0f, YAW_X, 0);
        if (!boosted && r.x.sa1_position[0] > -150.0f) {
            sa1g_player_ptrs pp;
            sa1g_world_player(world_of(&r), &pp);
            if (pp.pwp != 0) {
                memcpy((uint8_t *)sa1g_world_ptr(world_of(&r), pp.pwp) + 0x38, &boost, 4);     /* playerwk.spd.x */
            }
            boosted = 1;
        }
        x = r.x.sa1_position[0];
        if (x > 1.0f && x < xt + 1190.0f && fabsf(r.x.sa1_position[2]) < 590.0f) {
            dep = (x < xt ? x * tn : UND_H) - r.x.sa1_position[1];
        }
        R->deepest = dep > R->deepest ? dep : R->deepest;
        if (dep > 3.0f) {
            if (++below >= 3) {
                R->fell = 1;        /* more than 3 below the surface for 6 steps */
                break;
            }
        } else {
            below = 0;
        }
        if (x > xt + 60.0f && grounded(&r) && fabsf(r.x.sa1_position[1] - UND_H) < 1.0f) {
            R->reached = 1;
            break;
        }
    }
    R->x = r.x.sa1_position[0];
    sa1_character_destroy(ctx, id);
}

/* a character standing under a platform (its bottom at y0, t thick) jumps: where does it end? */
static float und_jump(sa1_context *ctx, float y0, float t, uint32_t flags, uint32_t *undersides, uint32_t *front)
{
    sa1_collision_info info;
    tbuf b;
    run r;
    sa1_character_id id = 0;
    int i;
    float y = -1e9f;
    memset(&b, 0, sizeof b);
    tb_floor(&b, -1000, 1000, -1000, 1000, 0.0f, 1.0f, SA1_SURFACE_SOLID);
    tb_box(&b, -150, 150, y0, y0 + t, -150, 150, SA1_SURFACE_SOLID);
    set_static_grid(ctx, &b, 0.0f, flags);
    memset(&info, 0, sizeof info);
    info.struct_size = sizeof info;
    if (sonic_at(ctx, 0.0f, 1.0f, 0.0f, 0, &id) == SA1_OK) {
        sa1_collision_info_get(ctx, &info);
        *undersides = info.grid_undersides;
        if (front != NULL) {
            *front = info.grid_front_only;
        }
        run_init(&r, ctx, id);
        for (i = 0; i < 90 && r.st == SA1_OK; i++) {
            pair(&r, 0, 0, YAW_X, (i >= 10 && i < 30) ? SA1_BUTTON_JUMP : 0u);
        }
        y = grounded(&r) ? r.x.sa1_position[1] : -1e9f;
        sa1_character_destroy(ctx, id);
    }
    tb_free(&b);
    return y;
}

static void test_underside(G *E)
{
    static const struct {
        int32_t kind;
        float deg, t, boost;
    } cases[] = {
        { SA1_CHAR_KNUCKLES, 45.0f, 4.0f, 10.0f }, { SA1_CHAR_KNUCKLES, 45.0f, 3.0f, 16.0f },
        { SA1_CHAR_KNUCKLES, 45.0f, 6.0f, 16.0f }, { SA1_CHAR_SONIC, 30.0f, 6.0f, 16.0f },
        { SA1_CHAR_SONIC, 20.0f, 0.5f, 16.0f },    { SA1_CHAR_KNUCKLES, 20.0f, 1.0f, 16.0f },
        { SA1_CHAR_SONIC, 45.0f, 0.5f, 16.0f },    { SA1_CHAR_KNUCKLES, 30.0f, 8.0f, 16.0f },
    };
    int cfg, ci;
    printf("underside: plank ramps whose undersides lie within the sphere's reach (ramp_diag slab: the falls of round 4b)\n");
    for (cfg = 0; cfg < 2; cfg++) {
        int falls = 0, reached = 0, n = 0;
        uint32_t und = 0;
        float deepest = 0.0f;
        char line[1024];
        size_t len = 0;
        line[0] = 0;
        for (ci = 0; ci < (int)(sizeof cases / sizeof cases[0]); ci++) {
            sa1_collision_info info;
            und_res R;
            tbuf b;
            float xt;
            und_scene(&b, cases[ci].deg, cases[ci].t, &xt);
            set_static_grid(E->ctx, &b, 0.0f, cfg ? SA1_GRID_KEEP_UNDERSIDES : 0u);
            tb_free(&b);
            und_run(E->ctx, cases[ci].kind, cases[ci].deg, xt, cases[ci].boost, &R);
            memset(&info, 0, sizeof info);
            info.struct_size = sizeof info;
            sa1_collision_info_get(E->ctx, &info);
            und += info.grid_undersides;
            n++;
            falls += R.fell;
            reached += R.reached;
            deepest = R.deepest > deepest ? R.deepest : deepest;
            if ((R.fell || g_verbose) && len + 96 < sizeof line) {
                len += (size_t)snprintf(line + len, sizeof line - len, " %s %.0f deg T %.1f boost %.0f: %s at x %.0f;",
                                        sa1_character_name(cases[ci].kind), cases[ci].deg, cases[ci].t, cases[ci].boost,
                                        R.fell ? "FELL" : R.reached ? "up" : "stopped", R.x);
            }
        }
        if (cfg == 0) {
            CHECK(falls == 0 && reached == n && und > 0,
                  "defaults: %d runs (Sonic / Knuckles, dash panels 10 / 16, slabs 0.5 .. 8 thick at 20 .. 45 degrees): %d fell, "
                  "%d reached the platform, deepest %.2f below the surface; %u undersides within reach left out%s", n, falls,
                  reached, deepest, (unsigned)und, len ? line : "");
        } else {
            INFO("SA1_GRID_KEEP_UNDERSIDES (the undersides kept, round 4b): %d of %d runs fell, deepest %.1f%s", falls, n,
                 deepest, len ? line : "");
        }
    }
    {
        uint32_t u1 = 0, u2 = 0, u3 = 0, u4 = 0, f4 = 0;
        float y1 = und_jump(E->ctx, 18.0f, 2.0f, SA1_GRID_UNDERSIDES_DROP, &u1, NULL);
        float y2 = und_jump(E->ctx, 18.0f, 2.0f, SA1_GRID_KEEP_UNDERSIDES, &u2, NULL);
        float y3 = und_jump(E->ctx, 18.0f, 20.0f, 0u, &u3, NULL);
        float y4 = und_jump(E->ctx, 18.0f, 2.0f, 0u, &u4, &f4);
        CHECK(fabsf(y1 - 20.0f) < 1.0f && u1 > 0,
              "SA1_GRID_UNDERSIDES_DROP (before round 0.9.3): a platform 2 thick (bottom at 18) is one-way: Sonic jumps up "
              "through it from below and lands on it (y %.2f; %u undersides left out)", y1, (unsigned)u1);
        CHECK(fabsf(y4) < 1.0f && u4 == 0 && f4 >= 2u,
              "round 0.9.3 (FIX-C2, the default): its flat underside stays front-only and stops him (back on the floor: y %.2f; "
              "%u front-only, %u left out)", y4, (unsigned)f4, (unsigned)u4);
        CHECK(fabsf(y2) < 1.0f && u2 == 0, "with SA1_GRID_KEEP_UNDERSIDES its underside stops him (back on the floor: y %.2f)",
              y2);
        CHECK(fabsf(y3) < 1.0f && u3 == 0,
              "a slab 20 thick (more than under_reach 12) keeps its underside: back on the floor (y %.2f)", y3);
    }
    sa1_collision_set_static(E->ctx, NULL, 0);
}

/* ---- PC-only capacities of host worlds (design 5.7; docs/research/gsa1_ramp_freeze.md fixes 3, 4, 10) ------------- */
#define LIM_MTX_DEPTH 0x8C88F69Cu       /* B_8C88F69C: Ninja matrix stack depth */

static void test_limits(G *E)
{
    sa1_context *ctx = E->ctx;
    sa1_collision_info info;
    sa1ca_world_stats ws;
    sa1_character_id id = 0;
    sa1_event ev[64];
    tbuf b;
    run r;
    uint32_t n, k, rep_near = 0, rep_mtx = 0, depth = 0, depth_max = 0;
    int i, j, cfg;
    float ymin = 1e9f;
    printf("limits: the raised NEAR-poly list (512 in host worlds), its reports, the matrix stack restore, cells fuller than "
           "the window's polygon budget\n");
    /* 1. the capacity in effect */
    for (cfg = 0; cfg < 3; cfg++) {
        memset(&b, 0, sizeof b);
        tb_floor(&b, -1000, 1000, -1000, 1000, 0, 1, SA1_SURFACE_SOLID);
        if (cfg == 0) {
            set_static(ctx, &b);
        } else if (cfg == 1) {
            set_static_grid(ctx, &b, 0.0f, SA1_GRID_DC_LIMITS);
        } else {
            sa1_collision_set_static(ctx, NULL, 0);
        }
        tb_free(&b);
        if (SA1_FAILED(sonic_at(ctx, 0, 10, 0, 0, &id))) {     /* no static collision: SA1_PARTIAL */
            CHECK(0, "create: %s", sa1_last_error(ctx));
            return;
        }
        run_init(&r, ctx, id);
        pair(&r, 0, 0, 0, 0);
        wstats(&r, &ws);
        CHECK(ws.near_cap == (cfg == 1 ? 96u : 512u), "%s: NEAR-poly list of %u", cfg == 0 ? "GRID (defaults)"
              : cfg == 1 ? "GRID with SA1_GRID_DC_LIMITS" : "no static collision", (unsigned)ws.near_cap);
        sa1_character_destroy(ctx, id);
    }
    /* 2. floors stacked 1 unit apart: 40 never fill the raised list (the game's 96 is full and the character falls through
     * them all); 160 at a quarter unit fill it too (80 at half a unit filled the 256 of rounds before 0.9.2); every
     * episode of full lists is reported once */
    for (cfg = 0; cfg < 3; cfg++) {
        int nf = cfg == 2 ? 160 : 40;
        float gap = cfg == 2 ? 0.25f : 1.0f;
        memset(&b, 0, sizeof b);
        for (j = 0; j < nf; j++) {
            tb_floor(&b, -1024, 1024, -1024, 1024, -gap * (float)j, 1, SA1_SURFACE_SOLID);
        }
        if (cfg == 1) {
            set_static_grid(ctx, &b, 0.0f, SA1_GRID_DC_LIMITS);
        } else {
            set_static(ctx, &b);
        }
        tb_free(&b);
        rep_near = 0;
        ymin = 1e9f;
        if (sonic_at(ctx, 3, 12, 5, 0, &id) != SA1_OK) {
            CHECK(0, "create: %s", sa1_last_error(ctx));
            return;
        }
        run_init(&r, ctx, id);
        for (i = 0; i < 60 && r.st == SA1_OK; i++) {
            pair(&r, 0, i > 20 ? 1.0f : 0.0f, YAW_X, 0);
            n = 0;
            sa1_poll_events(ctx, ev, 64, &n);
            for (k = 0; k < n; k++) {
                rep_near += ev[k].type == SA1_EVENT_CAPACITY && ev[k].id == SA1_CAPACITY_NEAR_POLYGONS;
            }
            ymin = r.x.sa1_position[1] < ymin ? r.x.sa1_position[1] : ymin;
        }
        wstats(&r, &ws);
        if (cfg == 0) {
            CHECK(r.st == SA1_OK && ws.over_near_polys == 0 && rep_near == 0 && ymin > -1.0f,
                  "40 floors 1 unit apart: the NEAR-poly list (%u) never full (up to %u), Sonic stays on the top floor "
                  "(lowest y %.2f)", (unsigned)ws.near_cap, (unsigned)ws.near_max, ymin);
        } else {
            CHECK(r.st == SA1_OK && ws.over_near_polys > 0 && rep_near >= 1 && rep_near <= 3 && ws.near_max == ws.near_cap,
                  "%s: the NEAR-poly list (%u) full in %u steps, SA1_EVENT_CAPACITY %u reported %u time(s) (lowest y %.2f%s)",
                  cfg == 1 ? "40 floors 1 unit apart with SA1_GRID_DC_LIMITS" : "160 floors a quarter unit apart",
                  (unsigned)ws.near_cap, (unsigned)ws.over_near_polys, (unsigned)SA1_CAPACITY_NEAR_POLYGONS,
                  (unsigned)rep_near, ymin, ymin < -1.0f ? ": through the floors, as the game's own list does" : "");
        }
        sa1_character_destroy(ctx, id);
    }
    /* 3. 20 coplanar copies of a floor: 8C029B00 touches 16 polygons of one object and leaves a matrix push behind */
    memset(&b, 0, sizeof b);
    for (j = 0; j < 20; j++) {
        tb_floor(&b, -1024, 1024, -1024, 1024, 0, 1, SA1_SURFACE_SOLID);
    }
    set_static(ctx, &b);
    tb_free(&b);
    if (sonic_at(ctx, 3, 12, 5, 0, &id) != SA1_OK) {
        CHECK(0, "create: %s", sa1_last_error(ctx));
        return;
    }
    run_init(&r, ctx, id);
    for (i = 0; i < 300 && r.st == SA1_OK; i++) {
        pair(&r, (float)sin(i * 0.05), 1, 0, i % 50 == 25 ? SA1_BUTTON_JUMP : 0u);
        n = 0;
        sa1_poll_events(ctx, ev, 64, &n);
        for (k = 0; k < n; k++) {
            rep_mtx += ev[k].type == SA1_EVENT_CAPACITY && ev[k].id == SA1_CAPACITY_MATRIX_STACK;
        }
        depth = 0;
        sa1g_world_rd32(world_of(&r), LIM_MTX_DEPTH, &depth);
        depth_max = depth > depth_max ? depth : depth_max;
    }
    wstats(&r, &ws);
    CHECK(r.st == SA1_OK && ws.matrix_base == 1u && ws.matrix_restores > 0 && rep_mtx >= 1 && depth_max < 20u,
          "20 coplanar copies of a floor (TOUCH-list overflows): the matrix stack restored to depth %u in %u steps (%u "
          "slots), SA1_EVENT_CAPACITY %u reported %u time(s); deepest after a pair %u (the game's limit 128; before the "
          "restore it reached 126)", (unsigned)ws.matrix_base, (unsigned)ws.matrix_restores, (unsigned)ws.matrix_slots,
          (unsigned)SA1_CAPACITY_MATRIX_STACK, (unsigned)rep_mtx, (unsigned)depth_max);
    sa1_character_destroy(ctx, id);
    /* 4. one 256-unit cell of 4-unit tiles (8192 triangles, nothing below): more than the window's polygon budget (7680),
     * one object before (never in the window: the character fell through), parts of at most 1024 now */
    memset(&b, 0, sizeof b);
    for (i = 0; i < 64; i++) {
        for (j = 0; j < 64; j++) {
            float x0 = 4.0f * (float)i, z0 = 4.0f * (float)j;
            tb_floor(&b, x0, x0 + 4.0f, z0, z0 + 4.0f, 0, 1, SA1_SURFACE_SOLID);
        }
    }
    set_static(ctx, &b);
    tb_free(&b);
    ymin = 1e9f;
    if (sonic_at(ctx, 131, 10, 125, 0, &id) != SA1_OK) {
        CHECK(0, "create: %s", sa1_last_error(ctx));
        return;
    }
    run_init(&r, ctx, id);
    for (i = 0; i < 60 && r.st == SA1_OK; i++) {
        pair(&r, 0, i > 20 ? 0.5f : 0.0f, YAW_X, 0);
        ymin = r.x.sa1_position[1] < ymin ? r.x.sa1_position[1] : ymin;
    }
    wstats(&r, &ws);
    memset(&info, 0, sizeof info);
    info.struct_size = sizeof info;
    sa1_collision_info_get(ctx, &info);
    CHECK(r.st == SA1_OK && grounded(&r) && ymin > -1.0f && info.grid_objects >= 8u && ws.window_objects > 0,
          "a 256-unit cell of 8192 triangles: %u GRID objects (at most 1024 triangles each), the window keeps %u objects / "
          "%u polygons; Sonic lands and runs on it (lowest y %.2f)", (unsigned)info.grid_objects,
          (unsigned)ws.window_objects, (unsigned)ws.window_polygons, ymin);
    sa1_character_destroy(ctx, id);
    sa1_collision_set_static(ctx, NULL, 0);
}

/* ---- round 0.9.2 (sadc docs/research/gsa1_090_plan.md 6.2): the land-collision query, walls in their own objects, the
 * surface kinds, the GRID size check ------------------------------------------------------------------------------- */
static uint32_t w_rd32(const void *p)
{
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

static float w_rdf(const void *p)
{
    float v;
    memcpy(&v, p, 4);
    return v;
}

/* a character of any kind (SA1 space, BAMS yaw) */
static sa1_status chr_at(sa1_context *ctx, int32_t kind, float x, float y, float z, int32_t angy, sa1_character_id *id)
{
    sa1_character_desc d;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.kind = kind;
    d.flags = SA1_CREATE_WORLD | SA1_CREATE_SA1_SPACE | SA1_CREATE_ANGLES;
    d.position[0] = x;
    d.position[1] = y;
    d.position[2] = z;
    d.angle[1] = angy;
    return sa1_character_create_ex(ctx, &d, id);
}

/* the adapter's counters; zeroed first (a "before" run links a library whose struct is shorter) */
static void wstats0(const run *r, sa1ca_world_stats *ws)
{
    memset(ws, 0, sizeof *ws);
    sa1ca_world_stats_get(world_of(r), ws);
}

/* window entries whose COL sphere reaches a query sphere at q of radius rq: what 8C0522C0 gathers for the character's next
 * land collision (8C073160 copies them all into its arrays; water only for E-102) */
static uint32_t window_gather(const run *r, const float q[3], float rq, int32_t kind)
{
    sa1g_world *w = world_of(r);
    void *hdr = NULL, *cols = NULL;
    uint32_t cap = 0, n, i, cnt = 0;
    uint16_t h = 0;
    if (w == NULL || sa1g_world_window(w, &hdr, &cols, &cap) != 0 || hdr == NULL || cols == NULL) {
        return 0;
    }
    memcpy(&h, hdr, 2);
    n = h;
    for (i = 0; i < n && i < cap; i++) {
        const uint8_t *c = (const uint8_t *)cols + SA1CA_COL_SIZE * i;
        float dx = w_rdf(c) - q[0], dy = w_rdf(c + 4) - q[1], dz = w_rdf(c + 8) - q[2];
        if ((w_rd32(c + 0x20) & 0x400002u) && kind != SA1_CHAR_GAMMA) {
            continue;
        }
        if (sqrtf(dx * dx + dy * dy + dz * dz) - rq < w_rdf(c + 0x0C)) {
            cnt++;
        }
    }
    return cnt;
}

/* distinct COL words of land objects, with bits no 1ST_READ land code reads (sadc docs/research/gsa1_090_surfaces.md 2) */
static uint32_t idle_word(uint32_t k)
{
    static const uint32_t bits[] = { 0x200u, 0x400u, 0x800u, 0x2000u, 0x8000u, 0x40000u, 0x80000u, 0x1000000u, 0x2000000u,
                                     0x4000000u };
    uint32_t w = 0x1u, i;
    for (i = 0; i < 10u; i++) {
        if (k & (1u << i)) {
            w |= bits[i];
        }
    }
    return w;
}

/* hits (gsa1_090_collision.md C3 / F1): 24 land objects of one 256-unit cell, each a COL word of its own, whose COL spheres
 * all reach E-102's query: the floor under him (with its part in the next cell, z < 0) and 23 strips 70-140 units away
 * on it (their boxes far: the guard of 0.9.1, by box
 * distance, let them all in, and 8C073160 copied 24 objects into its 16-entry arrays - a host crash); E-102 walks to and
 * fro and jumps for 600 steps. Round 0.9.2: the window let at most 15 of them into his query (SA1_CAPACITY_LAND_QUERY),
 * the lowered arrays hold 192 and their canary stays clean. Round 0.9.3 (the 0.9.2 owner logs' "query took 15 objects":
 * the 15 left floors and ceilings in reach out): with the arrays the window keeps every object in reach - his query
 * gathers all of them, LAND_QUERY and LAND_HITS never fire; with SA1_GRID_DC_LIMITS the bound stays 15 (dc 1). */
static void test_hits_run(G *E, int dc)
{
    enum { NW = 24 };
    sa1_context *ctx = E->ctx;
    float pts[NW * 12];
    uint32_t idx[NW * 6], k, n = 0, cap_query = 0, cap_hits = 0, gather_max = 0, gather_first = 0, steps_grounded = 0;
    sa1_collision_group grp[NW];
    sa1_collision_static d;
    sa1_collision_grid gp;
    sa1_character_id id = 0;
    sa1ca_world_stats ws;
    sa1_event ev[64];
    run r;
    int i;
    float xmin = 1e9f, xmax = -1e9f;
    printf("hits%s: 24 COL words in one cell (25 land objects with the floor's part in the next cell) whose COL spheres "
           "reach E-102's query; he walks and jumps 600 steps\n", dc ? " (SA1_GRID_DC_LIMITS)" : "");
    memset(grp, 0, sizeof grp);
    for (k = 0; k < NW; k++) {
        float z0 = k == 0 ? -200.0f : 120.0f + 3.0f * (float)(k - 1), z1 = k == 0 ? 200.0f : z0 + 1.0f;
        float q[12] = { 0, 0, z0, 256, 0, z0, 256, 0, z1, 0, 0, z1 };
        static const uint32_t t[6] = { 0, 3, 2, 0, 2, 1 };
        memcpy(pts + 12u * k, q, sizeof q);
        memcpy(idx + 6u * k, t, sizeof t);
        grp[k].struct_size = sizeof grp[k];
        grp[k].flags = SA1_COLGROUP_RAW_FLAGS;
        grp[k].surface = idle_word(k);
        grp[k].scale[0] = grp[k].scale[1] = grp[k].scale[2] = 1.0f;
        grp[k].first_point = 4u * k;
        grp[k].num_points = 4;
        grp[k].first_tri = 2u * k;
        grp[k].num_tris = 2;
    }
    memset(&d, 0, sizeof d);
    memset(&gp, 0, sizeof gp);
    gp.struct_size = sizeof gp;
    gp.flags = SA1_GRID_DC_LIMITS;
    d.struct_size = sizeof d;
    d.mode = SA1_COLLISION_MODE_GRID;
    d.flags = SA1_COLLISION_SA1_SPACE;
    d.points = pts;
    d.num_points = 4u * NW;
    d.indices = idx;
    d.num_tris = 2u * NW;
    d.groups = grp;
    d.num_groups = NW;
    d.grid = dc ? &gp : NULL;
    if (SA1_FAILED(sa1_collision_set_static_ex(ctx, &d)) || chr_at(ctx, SA1_CHAR_GAMMA, 128, 2, 50, 0, &id) != SA1_OK) {
        CHECK(0, "set-up: %s", sa1_last_error(ctx));
        sa1_collision_set_static(ctx, NULL, 0);
        return;
    }
    run_init(&r, ctx, id);
    for (i = 0; i < 300 && r.st == SA1_OK; i++) {
        float yaw = (((i - 10) / 25) % 2) ? (float)PI + YAW_X : YAW_X, q[3];     /* along +x, then back along -x */
        uint32_t g;
        pair(&r, 0, i < 10 ? 0.0f : 0.8f, yaw, (i % 30) == 15 ? SA1_BUTTON_JUMP : 0u);
        q[0] = r.x.sa1_position[0];
        q[1] = r.x.sa1_position[1] + 10.5f;    /* E-102's sphere: r 10.5 above the feet (upright) */
        q[2] = r.x.sa1_position[2];
        g = window_gather(&r, q, 10.5f, SA1_CHAR_GAMMA);
        if (i == 0) {
            gather_first = g;
        }
        gather_max = g > gather_max ? g : gather_max;
        steps_grounded += grounded(&r) ? 2u : 0u;
        xmin = r.x.sa1_position[0] < xmin ? r.x.sa1_position[0] : xmin;
        xmax = r.x.sa1_position[0] > xmax ? r.x.sa1_position[0] : xmax;
        n = 0;
        sa1_poll_events(ctx, ev, 64, &n);
        for (k = 0; k < n; k++) {
            cap_query += ev[k].type == SA1_EVENT_CAPACITY && ev[k].id == SA1_CAPACITY_LAND_QUERY;
            cap_hits += ev[k].type == SA1_EVENT_CAPACITY && ev[k].id == SA1_CAPACITY_LAND_HITS;
        }
        if (i % 50 == 49) {
            fflush(stdout);
        }
    }
    wstats0(&r, &ws);
    INFO("E-102 ran %u steps (%s), x %.1f .. %.1f, %u steps on the ground, at the end (%.1f %.1f %.1f) mode %d; window: "
         "%u objects; objects his next query gathers: %u at the start, up to %u (the game's arrays hold 16); %s",
         (unsigned)r.x.step, sa1_status_string(r.st), xmin, xmax, (unsigned)steps_grounded, r.x.sa1_position[0],
         r.x.sa1_position[1], r.x.sa1_position[2], (int)r.x.mode, (unsigned)ws.window_objects, (unsigned)gather_first,
         (unsigned)gather_max, r.log);
    INFO("query bound %u: r %.2f, up to %u objects in reach kept, %u steps left some out (SA1_CAPACITY_LAND_QUERY %u "
         "time(s)); lowered arrays: %u, the most one call collected %u, %u left out (SA1_CAPACITY_LAND_HITS %u), canary "
         "changes %u", (unsigned)ws.query_cap, (double)ws.query_radius_x100 / 100.0, (unsigned)ws.query_max,
         (unsigned)ws.over_near, (unsigned)cap_query, (unsigned)ws.hits_cap, (unsigned)ws.hits_max,
         (unsigned)ws.hits_dropped, (unsigned)cap_hits, (unsigned)ws.hits_canary);
    if (dc) {
        CHECK(r.st == SA1_OK && r.x.step >= 600u && r.x.sa1_position[1] > -1.0f && r.x.sa1_position[2] > -200.0f
                  && r.x.sa1_position[2] < 200.0f && ws.query_cap == 15u && gather_max <= 15u && ws.query_max <= 15u
                  && ws.over_near > 0u && cap_query >= 1u && ws.hits_cap == 192u && ws.hits_max >= 1u
                  && ws.hits_max <= 15u && ws.hits_canary == 0u && ws.hits_dropped == 0u,
              "with the DC limits: 24 objects in reach of E-102's query, his land collision never gathers more than 15 "
              "(%u), the arrays (%u) never fill and their canary is clean (%u)", (unsigned)gather_max,
              (unsigned)ws.hits_cap, (unsigned)ws.hits_canary);
    } else {
        CHECK(r.st == SA1_OK && r.x.step >= 600u && r.x.sa1_position[1] > -1.0f && r.x.sa1_position[2] > -200.0f
                  && r.x.sa1_position[2] < 200.0f && ws.query_cap == 191u && gather_max >= 24u && ws.query_max >= 24u
                  && ws.over_near == 0u && cap_query == 0u && cap_hits == 0u && ws.hits_cap == 192u
                  && ws.hits_max >= 24u && ws.hits_max <= 192u && ws.hits_canary == 0u && ws.hits_dropped == 0u,
              "24 objects in reach of E-102's query: all in the window and in his land collision (gathered up to %u, "
              "one call up to %u of the arrays' %u; LAND_QUERY %u, LAND_HITS %u), the canary is clean (%u)",
              (unsigned)gather_max, (unsigned)ws.hits_max, (unsigned)ws.hits_cap, (unsigned)cap_query, (unsigned)cap_hits,
              (unsigned)ws.hits_canary);
    }
    sa1_character_destroy(ctx, id);
    sa1_collision_set_static(ctx, NULL, 0);
}

static void test_hits(G *E)
{
    test_hits_run(E, 0);
    test_hits_run(E, 1);
}
/* wallfloor (gsa1_090_collision.md C1 / F2 with its Review R.2 b, R.4.2): coll_diag's wall scene in SA1 units - a wall 750
 * long and 40 high across the run, T thick, on a floor; its front face at z = -ofs (the cell plane z = 0 at ofs 0). A box
 * wall (T 5 / 6 units = 20 / 24 HU: single-sided faces, the floor cut around its foot as G-SA1's hidden-face removal leaves
 * it) shared GRID objects with the floor at its foot: Knuckles and E-102 running in at 35 degrees crept into it and came
 * out behind it at offsets 0, -10, -100 HU; a single double-sided face (a thin wall of 2-8 HU as G-SA1 merges it) let Big
 * through when he dashed into it at -10 / -100 / +1000 HU. Outcome: the sphere centre's signed distance to the front face
 * (in front / inside / past the back face), after 3 s of trying to get out when inside; going over the top is not a fail */
enum { WF_FRONT = 0, WF_INSIDE = 1, WF_THROUGH = 2, WF_OVER = 3, WF_NONE = 4, WF_FAIL = 5 };

static void wf_scene(tbuf *b, float zf, float th, int ds)
{
    const float X = 375.0f, H = 40.0f, F = 1000.0f;
    float zb = zf - th;
    if (ds) {
        tb_floor(b, -F, F, -F, F, 0, 1, SA1_SURFACE_SOLID);
        tb_wall_z(b, zf - 0.5f * th, 0, H, -X, X, 1, SA1_SURFACE_SOLID | SA1_SURFACE_DOUBLE_SIDED);
        return;
    }
    tb_floor(b, -F, F, zf, F, 0, 1, SA1_SURFACE_SOLID);
    tb_floor(b, -F, F, -F, zb, 0, 1, SA1_SURFACE_SOLID);
    tb_floor(b, -F, -X, zb, zf, 0, 1, SA1_SURFACE_SOLID);
    tb_floor(b, X, F, zb, zf, 0, 1, SA1_SURFACE_SOLID);
    tb_wall_z(b, zf, 0, H, -X, X, 1, SA1_SURFACE_SOLID);
    tb_wall_z(b, zb, 0, H, -X, X, -1, SA1_SURFACE_SOLID);
    tb_floor(b, -X, X, zb, zf, H, 1, SA1_SURFACE_SOLID);
    tb_wall_x(b, -X, 0, H, zb, zf, -1, SA1_SURFACE_SOLID);
    tb_wall_x(b, X, 0, H, zb, zf, 1, SA1_SURFACE_SOLID);
}

/* the ground speed (playerwk.spd.x) as a dash panel sets it */
static void set_ground_speed(const run *r, float v)
{
    sa1g_world *w = world_of(r);
    sa1g_player_ptrs pp;
    if (w == NULL) {
        return;
    }
    sa1g_world_player(w, &pp);
    if (pp.pwp != 0) {
        memcpy((uint8_t *)sa1g_world_ptr(w, pp.pwp) + 0x38, &v, 4);
    }
}

static float kind_radius(int32_t kind)
{
    switch (kind) {
    case SA1_CHAR_TAILS:
        return 4.5f;
    case SA1_CHAR_KNUCKLES:
        return 5.7f;
    case SA1_CHAR_GAMMA:
        return 10.5f;
    case SA1_CHAR_BIG:
        return 8.5f;
    default:
        return 5.0f;
    }
}

static int wf_run(sa1_context *ctx, int32_t kind, float zf, float th, float boost, float ang_deg, float x0)
{
    const double a = ang_deg * PI / 180.0, t = atan2(cos(a), sin(a));   /* the run's direction (sin a, 0, -cos a) */
    float rr = kind_radius(kind), d = 0.0f, d_before;
    sa1_character_id id = 0;
    int f, contact = -1, boosted = 0, crossed = 0, over = 0, outcome;
    run r;
    if (chr_at(ctx, kind, x0 - (float)sin(a) * 75.0f, 2.0f, zf + (float)cos(a) * 75.0f,
               (int32_t)(t / (2.0 * PI) * 65536.0), &id) != SA1_OK) {
        return WF_FAIL;
    }
    run_init(&r, ctx, id);
    for (f = 0; f < 130 && r.st == SA1_OK; f++) {
        d_before = r.x.sa1_position[2] - zf;
        if (boost > 0.0f && !boosted && f >= 5 && grounded(&r) && d_before < rr + 2.5f * boost) {
            set_ground_speed(&r, boost);
            boosted = 1;
        }
        pair(&r, 0, f < 5 ? 0.0f : 1.0f, (float)t + YAW_X, 0);
        d = r.x.sa1_position[2] - zf;
        if (!crossed && d < 0.0f && d_before >= 0.0f) {
            crossed = 1;
            over = r.x.sa1_position[1] > 40.0f - 0.5f;    /* over the top, not through the face */
        }
        if (contact < 0 && d < rr + 2.0f) {
            contact = f;
        }
        if (contact >= 0 && f > contact + 45) {
            break;
        }
    }
    if (r.st != SA1_OK) {
        sa1_character_destroy(ctx, id);
        return WF_FAIL;
    }
    outcome = contact < 0 ? WF_NONE : d > 0.0f ? WF_FRONT : d >= -th ? WF_INSIDE : WF_THROUGH;
    if ((outcome == WF_INSIDE || outcome == WF_THROUGH) && over) {
        outcome = WF_OVER;
    }
    if (outcome == WF_INSIDE) {
        /* try to get out: back the way it came, with jumps, 3 s */
        for (f = 0; f < 90 && r.st == SA1_OK; f++) {
            pair(&r, 0, -1.0f, (float)t + YAW_X, (f % 20) < 4 ? SA1_BUTTON_JUMP : 0u);
        }
        d = r.x.sa1_position[2] - zf;
        outcome = r.st != SA1_OK ? WF_FAIL : d > 0.0f ? WF_FRONT : d >= -th ? WF_INSIDE : WF_THROUGH;
    }
    sa1_character_destroy(ctx, id);
    return outcome;
}

static void test_wallfloor(G *E)
{
    static const float ofs_hu[] = { 0.0f, 10.0f, -10.0f, 100.0f, -100.0f, 1000.0f };
    static const float boosts[] = { 0.0f, 10.0f, 16.0f };
    static const float ds_t[] = { 2.0f, 4.0f, 8.0f };
    sa1_context *ctx = E->ctx;
    uint32_t oi, bad_box = 0, bad_ds = 0, runs = 0, fails = 0;
    printf("wallfloor: a wall across the run at offsets 0 / +-10 / +-100 / +1000 HU from the cell plane; runs ending inside "
           "or past it (going over the top is not counted)\n");
    for (oi = 0; oi < sizeof ofs_hu / sizeof ofs_hu[0]; oi++) {
        float zf = -ofs_hu[oi] / 4.0f;      /* the review's CD_WALL_OFS: + moves the wall towards the runner's back */
        uint32_t ti, bad_o = 0, bad_o_ds = 0, n_o = 0, n_o_ds = 0;
        char what[512];
        size_t wl = 0;
        what[0] = 0;
        /* box walls 20 / 24 HU: Knuckles and E-102 */
        for (ti = 0; ti < 2; ti++) {
            float th = ti == 0 ? 5.0f : 6.0f;
            tbuf b;
            uint32_t ki, bi, ai, xi;
            memset(&b, 0, sizeof b);
            wf_scene(&b, zf, th, 0);
            set_static(ctx, &b);
            for (ki = 0; ki < 2; ki++) {
                int32_t kind = ki == 0 ? SA1_CHAR_KNUCKLES : SA1_CHAR_GAMMA;
                for (bi = 0; bi < 3; bi++) {
                    for (ai = 0; ai < 2; ai++) {
                        for (xi = 0; xi < 2; xi++) {
                            int o = wf_run(ctx, kind, zf, th, boosts[bi], ai ? 35.0f : 0.0f, xi ? 25.0f : -75.0f);
                            n_o++;
                            fails += o == WF_FAIL;
                            if (o == WF_INSIDE || o == WF_THROUGH || o == WF_FAIL) {
                                bad_o++;
                                if (wl + 48 < sizeof what) {
                                    wl += (size_t)snprintf(what + wl, sizeof what - wl, " %s/%.0fHU/v%.0f/%sdeg:%s",
                                                           kind == SA1_CHAR_GAMMA ? "E102" : "Knux", th * 4.0f,
                                                           (double)boosts[bi], ai ? "35" : "0",
                                                           o == WF_INSIDE ? "in" : o == WF_THROUGH ? "past" : "fail");
                                }
                            }
                        }
                    }
                }
            }
            tb_free(&b);
        }
        /* single double-sided faces 2 / 4 / 8 HU: Big dashing */
        for (ti = 0; ti < 3; ti++) {
            float th = ds_t[ti] / 4.0f;
            tbuf b;
            uint32_t bi, ai, xi;
            memset(&b, 0, sizeof b);
            wf_scene(&b, zf, th, 1);
            set_static(ctx, &b);
            for (bi = 1; bi < 3; bi++) {
                for (ai = 0; ai < 2; ai++) {
                    for (xi = 0; xi < 2; xi++) {
                        int o = wf_run(ctx, SA1_CHAR_BIG, zf, th, boosts[bi], ai ? 35.0f : 0.0f, xi ? 25.0f : -75.0f);
                        n_o_ds++;
                        fails += o == WF_FAIL;
                        if (o == WF_INSIDE || o == WF_THROUGH || o == WF_FAIL) {
                            bad_o_ds++;
                            if (wl + 48 < sizeof what) {
                                wl += (size_t)snprintf(what + wl, sizeof what - wl, " Big/ds%.0fHU/v%.0f/%sdeg:%s",
                                                       (double)ds_t[ti], (double)boosts[bi], ai ? "35" : "0",
                                                       o == WF_INSIDE ? "in" : o == WF_THROUGH ? "past" : "fail");
                            }
                        }
                    }
                }
            }
            tb_free(&b);
        }
        printf("    offset %+6.0f HU: box walls %u of %u runs inside or past, double-sided faces %u of %u%s%s\n",
               (double)ofs_hu[oi], (unsigned)bad_o, (unsigned)n_o, (unsigned)bad_o_ds, (unsigned)n_o_ds,
               wl ? ":" : "", what);
        fflush(stdout);
        bad_box += bad_o;
        bad_ds += bad_o_ds;
        runs += n_o + n_o_ds;
    }
    CHECK(bad_box == 0 && bad_ds == 0 && fails == 0, "%u runs into walls at every offset: none ends inside or past a box wall "
          "(%u) or a double-sided face (%u); %u failed ticks", (unsigned)runs, (unsigned)bad_box, (unsigned)bad_ds,
          (unsigned)fails);
    sa1_collision_set_static(ctx, NULL, 0);
}

/* ceiling (gsa1_090_collision.md Review R.3.4): a ceiling 16 units (64 HU) over its floor in one cell (one COL word): with
 * floor and ceiling in one land object the game averaged their push-backs whenever the sphere touched both (Big's sphere
 * is 17 across). Sonic and Big stand, run and jump under it for 150 pairs: neither sinks into the floor */
static void test_ceiling(G *E)
{
    sa1_context *ctx = E->ctx;
    static const int32_t kinds[] = { SA1_CHAR_SONIC, SA1_CHAR_BIG };
    sa1_collision_info info;
    tbuf b;
    uint32_t k;
    printf("ceiling: a downward face 16 units over the floor in one cell\n");
    memset(&b, 0, sizeof b);
    tb_floor(&b, 0, 256, 0, 256, 0, 1, SA1_SURFACE_SOLID);
    tb_floor(&b, 0, 256, 0, 256, 16, -1, SA1_SURFACE_SOLID);
    set_static(ctx, &b);
    tb_free(&b);
    for (k = 0; k < 2; k++) {
        sa1_character_id id = 0;
        run r;
        int i;
        float ymin = 1e9f, ymax = -1e9f;
        if (chr_at(ctx, kinds[k], 60, 1, 128, 0, &id) != SA1_OK) {
            CHECK(0, "create: %s", sa1_last_error(ctx));
            continue;
        }
        run_init(&r, ctx, id);
        for (i = 0; i < 150 && r.st == SA1_OK; i++) {
            pair(&r, 0, i < 10 ? 0.0f : ((i / 30) % 2 ? -0.7f : 0.7f), YAW_X, (i % 30) == 20 ? SA1_BUTTON_JUMP : 0u);
            if (i >= 10) {
                ymin = r.x.sa1_position[1] < ymin ? r.x.sa1_position[1] : ymin;
                ymax = r.x.sa1_position[1] > ymax ? r.x.sa1_position[1] : ymax;
            }
        }
        if (k == 0) {
            memset(&info, 0, sizeof info);
            info.struct_size = sizeof info;
            sa1_collision_info_get(ctx, &info);
            INFO("GRID: %u land objects", (unsigned)info.grid_objects);
        }
        CHECK(r.st == SA1_OK && ymin > -0.5f && ymax < 16.0f, "%s under the ceiling: feet between %.2f and %.2f (mode/action "
              "%s)", kinds[k] == SA1_CHAR_BIG ? "Big" : "Sonic", ymin, ymax, r.log);
        sa1_character_destroy(ctx, id);
    }
    sa1_collision_set_static(ctx, NULL, 0);
}

/* kinds (gsa1_090_surfaces.md F1 with its Review R.3, C8): the new surface kinds reach the game as their COL bits and come
 * back in sa1_state.surface */
static void test_kinds(G *E)
{
    sa1_context *ctx = E->ctx;
    static const struct {
        uint32_t surface;
        const char *name;
        uint32_t col;
    } kinds[] = {
        { SA1_SURFACE_SNOW, "SNOW", 0x8u },
        { SA1_SURFACE_SPEED_PATH, "SPEED_PATH", 0x800000u },
        { SA1_SURFACE_FOOTPRINTS, "FOOTPRINTS", 0x100000u },
    };
    uint32_t k;
    printf("kinds: SNOW / SPEED_PATH / FOOTPRINTS floors\n");
    for (k = 0; k < 4; k++) {
        uint32_t surf = k < 3 ? kinds[k].surface : SA1_SURFACE_SOLID;
        sa1_character_id id = 0;
        tbuf b;
        run r;
        int i;
        float v45 = 0.0f;
        uint32_t attr = 0, rep = 0;
        memset(&b, 0, sizeof b);
        tb_floor(&b, -600, 600, -600, 600, 0, 1, surf);
        set_static(ctx, &b);
        tb_free(&b);
        if (sonic_at(ctx, 0, 2, 0, 0, &id) != SA1_OK) {
            CHECK(0, "create: %s", sa1_last_error(ctx));
            continue;
        }
        run_init(&r, ctx, id);
        for (i = 0; i < 55 && r.st == SA1_OK; i++) {
            pair(&r, 0, i >= 10 ? 1.0f : 0.0f, 0, 0);
            if (i == 9) {
                attr = r.x.attr;
                rep = r.x.surface;
            }
        }
        v45 = r.x.player_speed[0];
        if (k == 0) {
            CHECK(r.st == SA1_OK && (attr & 0x8u) && (rep & SA1_SURFACE_SNOW) && v45 > 0.33f && v45 < 0.37f,
                  "SNOW: the game's attribute %08X, sa1_state.surface %04X; ground speed after 45 pairs of running %.3f "
                  "(snow 0.351, normal ground 2.061: gsa1_090_surfaces.md 3)", (unsigned)attr, (unsigned)rep, v45);
        } else if (k < 3) {
            CHECK(r.st == SA1_OK && (attr & kinds[k].col) == kinds[k].col && (rep & kinds[k].surface),
                  "%s: the game's attribute %08X (COL %X), sa1_state.surface %05X", kinds[k].name, (unsigned)attr,
                  (unsigned)kinds[k].col, (unsigned)rep);
        } else {
            CHECK(r.st == SA1_OK && v45 > 2.0f && v45 < 2.1f && !(rep & (SA1_SURFACE_SNOW | SA1_SURFACE_SPEED_PATH |
                                                                        SA1_SURFACE_FOOTPRINTS)),
                  "normal ground: speed after 45 pairs %.3f, surface %04X", v45, (unsigned)rep);
        }
        sa1_character_destroy(ctx, id);
    }
    sa1_collision_set_static(ctx, NULL, 0);
}

/* huge (gsa1_090_water.md GRID-CAP with its Review R2.5): 12-unit quads in layers 128 units apart (water_probe's "huge"
 * scene: never cut, about 25 bytes per GRID triangle): 2.6 million triangles still prepare (65.7 MB of the 64 MiB
 * segment); 2.9 million fail in the counting pass, in well under a second, with the size needed in the message (before:
 * only after the whole cut, in the layout pass) */
static void huge_soup(uint32_t want, sa1_collision_tri **out)
{
    const uint32_t side = 640;
    const float q = 12.0f;
    sa1_collision_tri *t = (sa1_collision_tri *)malloc((size_t)want * sizeof *t);
    uint32_t n = 0, L, i, j;
    for (L = 0; n < want; L++) {
        for (i = 0; i < side && n < want; i++) {
            for (j = 0; j < side && n < want; j++) {
                float x = -(float)side * q * 0.5f + (float)i * q, z = -(float)side * q * 0.5f + (float)j * q;
                float y = 128.0f * (float)L + 0.25f * (float)((i * 7u + j * 3u) % 5u);
                float a[3] = { x, y, z }, b[3] = { x + q, y, z }, c[3] = { x + q, y, z + q }, e[3] = { x, y, z + q };
                /* up-facing (+y): a e c, a c b */
                memcpy(t[n].v[0], a, 12);
                memcpy(t[n].v[1], e, 12);
                memcpy(t[n].v[2], c, 12);
                t[n++].surface = SA1_SURFACE_SOLID;
                if (n < want) {
                    memcpy(t[n].v[0], a, 12);
                    memcpy(t[n].v[1], c, 12);
                    memcpy(t[n].v[2], b, 12);
                    t[n++].surface = SA1_SURFACE_SOLID;
                }
            }
        }
    }
    *out = t;
}

static void test_huge(G *E)
{
    static const uint32_t sizes[] = { 2600000u, 2900000u };
    sa1_context *ctx = E->ctx;
    uint32_t k;
    printf("huge: 2.6 and 2.9 million small floor triangles prepared (sa1_collision_prepare_tris)\n");
    for (k = 0; k < 2; k++) {
        sa1_collision_tri *t = NULL;
        sa1_collision_prepared *p = NULL;
        sa1_collision_prepared_info pi;
        sa1_status st;
        double t0, ms;
        huge_soup(sizes[k], &t);
        if (t == NULL) {
            CHECK(0, "out of memory");
            return;
        }
        t0 = sa1_now_ms();
        st = sa1_collision_prepare_tris(ctx, t, sizes[k], NULL, &p);
        ms = sa1_now_ms() - t0;
        memset(&pi, 0, sizeof pi);
        pi.struct_size = sizeof pi;
        if (p != NULL) {
            sa1_collision_prepared_info_get(p, &pi);
        }
        if (k == 0) {
            CHECK(st == SA1_OK && pi.status == SA1_OK && pi.grid_triangles == sizes[k],
                  "%u triangles: prepared in %.0f ms (%u GRID triangles, %u bytes)%s%s", (unsigned)sizes[k], ms,
                  (unsigned)pi.grid_triangles, (unsigned)pi.bytes, pi.message[0] ? ": " : "", pi.message);
        } else {
            CHECK(SA1_FAILED(pi.status) && strstr(pi.message, "needs about") != NULL && ms < 1000.0,
                  "%u triangles: refused in %.0f ms: %s", (unsigned)sizes[k], ms, pi.message);
        }
        sa1_collision_prepared_release(p);
        free(t);
    }
}

/* ---- round 0.9.3: solid ceilings over thin roofs (front-only undersides) and curved surfaces ------------------------ */
/* sadc docs/research/gsa1_091b_ceiling.md FIX-C1 / C2 and gsa1_091a_loop.md LOOP-L1, with their Reviews */
#define JC_KINDS 6
static const int32_t jc_kinds[JC_KINDS] = { SA1_CHAR_SONIC, SA1_CHAR_TAILS, SA1_CHAR_KNUCKLES, SA1_CHAR_AMY, SA1_CHAR_GAMMA,
                                            SA1_CHAR_BIG };

typedef struct jc_res {
    int through, fault, bonk;
    float top_max, end_y;
} jc_res;

/* a character of `kind` on the floor y = 0 at (x, z) jumps under a ceiling plane y = ceil. approach 0: A held 20 pairs (40
 * steps, the stick idle); 1: A held, released, A again 20 pairs later (the jump's second press: Sonic's homing attack, Tails'
 * flight, Knuckles' glide, Big's / Amy's / E-102's own moves); 2: a running jump along +x: the ground speed set to `speed` for
 * 8 pairs (a dash panel), then A held 20 pairs. through: the feet above the ceiling plane + 0.5 at any time */
static void jc_jump(sa1_context *ctx, int32_t kind, float x, float z, float ceil, int approach, float speed, jc_res *R)
{
    sa1_character_id id = 0;
    run r;
    int i;
    memset(R, 0, sizeof *R);
    R->top_max = -1e9f;
    if (chr_at(ctx, kind, x, 1.0f, z, 0, &id) != SA1_OK) {
        R->fault = 1;
        return;
    }
    run_init(&r, ctx, id);
    for (i = 0; i < 90 && r.st == SA1_OK; i++) {
        uint32_t btn = 0;
        float sy = 0.0f, top;
        if (approach == 0) {
            btn = (i >= 10 && i < 30) ? SA1_BUTTON_JUMP : 0u;
        } else if (approach == 1) {
            btn = ((i >= 10 && i < 30) || (i >= 40 && i < 60)) ? SA1_BUTTON_JUMP : 0u;
        } else {
            sy = (i >= 4 && i < 12) ? 1.0f : 0.0f;
            if (i >= 4 && i < 12 && grounded(&r)) {
                set_ground_speed(&r, speed);
            }
            btn = (i >= 12 && i < 32) ? SA1_BUTTON_JUMP : 0u;
        }
        pair(&r, 0, sy, YAW_X, btn);
        top = r.x.sa1_position[1] + 2.0f * kind_radius(kind);
        R->top_max = top > R->top_max ? top : R->top_max;
        if (r.x.sa1_position[1] > ceil + 0.5f) {
            R->through = 1;
        }
    }
    R->fault = r.st != SA1_OK;
    R->end_y = r.x.sa1_position[1];
    R->bonk = !R->through && R->top_max >= ceil - 1.0f;
    sa1_character_destroy(ctx, id);
}

static const char *jc_kind_name(int32_t kind)
{
    switch (kind) {
    case SA1_CHAR_SONIC: return "Sonic";
    case SA1_CHAR_TAILS: return "Tails";
    case SA1_CHAR_KNUCKLES: return "Knuckles";
    case SA1_CHAR_AMY: return "Amy";
    case SA1_CHAR_GAMMA: return "E-102";
    case SA1_CHAR_BIG: return "Big";
    }
    return "?";
}

/* the GRID counters of the current geometry */
static void jc_info(sa1_context *ctx, sa1_collision_info *info)
{
    memset(info, 0, sizeof *info);
    info->struct_size = sizeof *info;
    sa1_collision_info_get(ctx, info);
}

/* a floor (y 0) and a roof slab over it: underside at y 32 (128 HU of room), t thick; 1400 x 600 so that running jumps
 * stay under it */
static void jc_slab(tbuf *b, float t, uint32_t under_surface)
{
    memset(b, 0, sizeof *b);
    tb_floor(b, -500, 1700, -600, 600, 0.0f, 1.0f, SA1_SURFACE_SOLID);
    tb_floor(b, -200, 1200, -300, 300, 32.0f + t, 1.0f, SA1_SURFACE_SOLID);
    tb_floor(b, -200, 1200, -300, 300, 32.0f, -1.0f, SA1_SURFACE_SOLID | under_surface);
    tb_wall_x(b, -200, 32.0f, 32.0f + t, -300, 300, -1.0f, SA1_SURFACE_SOLID);
    tb_wall_x(b, 1200, 32.0f, 32.0f + t, -300, 300, 1.0f, SA1_SURFACE_SOLID);
    tb_wall_z(b, -300, 32.0f, 32.0f + t, -200, 1200, -1.0f, SA1_SURFACE_SOLID);
    tb_wall_z(b, 300, 32.0f, 32.0f + t, -200, 1200, 1.0f, SA1_SURFACE_SOLID);
}

/* every kind at 12 spots (standing jump), 2 spots (jump twice), running jumps at 3 / 6 / 9 units per step: through count */
static uint32_t jc_slab_runs(sa1_context *ctx, uint32_t *runs, uint32_t *bonks, char *what, size_t wsz)
{
    static const float spx[4] = { -120.0f, -40.0f, 40.0f, 120.0f }, spz[3] = { -150.0f, 0.0f, 150.0f };
    static const float speeds[3] = { 3.0f, 6.0f, 9.0f };
    uint32_t k, s, th = 0;
    size_t wl = strlen(what);
    for (k = 0; k < JC_KINDS; k++) {
        for (s = 0; s < 12u + 2u + 3u; s++) {
            jc_res R;
            int ap = s < 12u ? 0 : (s < 14u ? 1 : 2);
            float x = s < 12u ? spx[s % 4u] : (s < 14u ? (s == 12u ? -100.0f : 100.0f) : -100.0f);
            float z = s < 12u ? spz[s / 4u] : 0.0f;
            jc_jump(ctx, jc_kinds[k], x, z, 32.0f, ap, ap == 2 ? speeds[s - 14u] : 0.0f, &R);
            (*runs)++;
            *bonks += (uint32_t)R.bonk;
            if (R.through || R.fault) {
                th++;
                if (wl + 64 < wsz) {
                    wl += (size_t)snprintf(what + wl, wsz - wl, " %s/%s%s%s", jc_kind_name(jc_kinds[k]),
                                           ap == 0 ? "jump" : ap == 1 ? "twice" : "run",
                                           ap == 2 ? (s == 14u ? "3" : s == 15u ? "6" : "9") : "", R.fault ? "/FAULT" : "");
                }
            }
        }
    }
    return th;
}

/* gm_bigcity's house (gsa1_091b_ceiling.md 2) as data, SA1 units (x = (x_hu - 176) / 4, z = (y_hu + 320) / 4, y = (z_hu +
 * 11136) / 4): a 72 x 96 room, the floor at 0, the flat ceiling at 32 (the gabled roof brush's bottom face), the roof 0 thick
 * at the eaves (x 0 / 72) and 8 at the ridge (x 36), the 8 HU walls as G-SA1 makes them: double-sided mid faces at x 5 / 67
 * and z 1 / 95, normals -x / -z. mark: the ceiling arrives FRONT_ONLY from the host (G-SA1's FIX-C3) instead of unmarked */
static void jc_house(tbuf *b, int mark)
{
    float w0[4][3] = { { 0, 32, 0 }, { 36, 40, 0 }, { 36, 40, 96 }, { 0, 32, 96 } };
    float w1[4][3] = { { 36, 40, 0 }, { 72, 32, 0 }, { 72, 32, 96 }, { 36, 40, 96 } };
    float n0[3] = { -0.2169f, 0.9762f, 0 }, n1[3] = { 0.2169f, 0.9762f, 0 };
    float g0[3] = { 0, 32, 0 }, g1[3] = { 36, 40, 0 }, g2[3] = { 72, 32, 0 };
    float h0[3] = { 0, 32, 96 }, h1[3] = { 36, 40, 96 }, h2[3] = { 72, 32, 96 };
    const uint32_t ds = SA1_SURFACE_SOLID | SA1_SURFACE_DOUBLE_SIDED;
    memset(b, 0, sizeof *b);
    tb_floor(b, -200, 272, -200, 296, 0.0f, 1.0f, SA1_SURFACE_SOLID);
    tb_floor(b, 0, 72, 0, 96, 32.0f, -1.0f, SA1_SURFACE_SOLID | (mark ? SA1_SURFACE_FRONT_ONLY : 0u));
    tb_quad(b, w0, n0, SA1_SURFACE_SOLID);
    tb_quad(b, w1, n1, SA1_SURFACE_SOLID);
    tb_tri(b, g0, g1, g2, SA1_SURFACE_SOLID);      /* the gable ends: -z ... */
    tb_tri(b, h0, h2, h1, SA1_SURFACE_SOLID);      /* ... and +z */
    tb_wall_x(b, 5, 0, 32, 1, 95, -1.0f, ds);
    tb_wall_x(b, 67, 0, 32, 1, 95, -1.0f, ds);
    tb_wall_z(b, 1, 0, 32, 5, 67, -1.0f, ds);
    tb_wall_z(b, 95, 0, 32, 5, 67, -1.0f, ds);
}

/* a slab as a moving collision object (G-SA1's props and brush entities): its FRONT_ONLY underside stops a jump from below;
 * a character dropped onto it lands on top */
static void jc_mover(G *E)
{
    sa1_context *ctx = E->ctx;
    tbuf fl, sl;
    uint32_t oid = 0, k;
    float pos[3] = { 0.0f, 0.0f, 0.0f };
    memset(&fl, 0, sizeof fl);
    memset(&sl, 0, sizeof sl);
    tb_floor(&fl, -300, 300, -300, 300, 0.0f, 1.0f, SA1_SURFACE_SOLID);
    set_static(ctx, &fl);
    tb_floor(&sl, -60, 60, -60, 60, 20.0f, 1.0f, SA1_SURFACE_SOLID);
    tb_floor(&sl, -60, 60, -60, 60, 18.0f, -1.0f, SA1_SURFACE_SOLID | SA1_SURFACE_FRONT_ONLY);
    tb_wall_x(&sl, -60, 18, 20, -60, 60, -1.0f, SA1_SURFACE_SOLID);
    tb_wall_x(&sl, 60, 18, 20, -60, 60, 1.0f, SA1_SURFACE_SOLID);
    tb_wall_z(&sl, -60, 18, 20, -60, 60, -1.0f, SA1_SURFACE_SOLID);
    tb_wall_z(&sl, 60, 18, 20, -60, 60, 1.0f, SA1_SURFACE_SOLID);
    if (sa1_collision_object_add(ctx, sl.t, sl.n, pos, NULL, &oid) != SA1_OK) {
        CHECK(0, "mover: %s", sa1_last_error(ctx));
        tb_free(&fl);
        tb_free(&sl);
        return;
    }
    for (k = 0; k < 2; k++) {
        sa1_character_id id = 0;
        sa1ca_world_stats ws;
        run r;
        int i;
        float ymax = -1e9f;
        if (chr_at(ctx, SA1_CHAR_SONIC, 0.0f, k ? 40.0f : 1.0f, 0.0f, 0, &id) != SA1_OK) {
            CHECK(0, "create: %s", sa1_last_error(ctx));
            continue;
        }
        run_init(&r, ctx, id);
        for (i = 0; i < 70 && r.st == SA1_OK; i++) {
            pair(&r, 0, 0, YAW_X, (k == 0 && i >= 10 && i < 30) ? SA1_BUTTON_JUMP : 0u);
            ymax = r.x.sa1_position[1] > ymax ? r.x.sa1_position[1] : ymax;
        }
        wstats0(&r, &ws);
        if (k == 0) {
            CHECK(r.st == SA1_OK && ymax < 18.5f && ws.front_only_on == 1u,
                  "a moving 2-thick slab with a FRONT_ONLY underside 18 over the floor: Sonic's jump from below is stopped "
                  "(feet up to %.2f, end %.2f; the world's front-only rule %s)", (double)ymax, (double)r.x.sa1_position[1],
                  ws.front_only_on ? "on" : "off");
        } else {
            CHECK(r.st == SA1_OK && grounded(&r) && fabsf(r.x.sa1_position[1] - 20.0f) < 1.0f,
                  "Sonic dropped onto it lands on its top (y %.2f; %u front-only polygons skipped from behind)",
                  (double)r.x.sa1_position[1], (unsigned)ws.front_only_skipped);
        }
        sa1_character_destroy(ctx, id);
    }
    sa1_collision_object_remove(ctx, oid);
    sa1_collision_set_static(ctx, NULL, 0);
    tb_free(&fl);
    tb_free(&sl);
}

/* b's triangles, each with its own surface word, prepared with GRID flags (the production path of a host soup:
 * sa1_collision_prepare_tris -> sa1_collision_set_prepared, as G-SA1 installs a map) */
static sa1_status set_flat_grid(sa1_context *ctx, const tbuf *b, uint32_t flags)
{
    sa1_collision_grid gp;
    sa1_collision_prepared *p = NULL;
    sa1_status st;
    memset(&gp, 0, sizeof gp);
    gp.struct_size = sizeof gp;
    gp.flags = flags;
    st = sa1_collision_prepare_tris(ctx, b->t, b->n, &gp, &p);
    if (st == SA1_OK) {
        st = sa1_collision_set_prepared(ctx, p);
    }
    sa1_collision_prepared_release(p);
    return st;
}

static void test_jumpceiling(G *E)
{
    static const float ts[] = { 12.0f, 8.0f, 6.0f, 4.0f, 3.0f };
    sa1_context *ctx = E->ctx;
    sa1_collision_info info;
    tbuf b;
    uint32_t ti, k, total_th = 0, total_runs = 0;
    printf("jumpceiling: thin roofs over a room keep their flat undersides as front-only polygons (FIX-C1 / C2): no kind jumps "
           "through them from below at any jump; jumped from above they are absent as before (mechanism B: 'underside')\n");
    /* 1. flat slabs t = 12 .. 3 units over 32 units (128 HU) of room */
    for (ti = 0; ti < sizeof ts / sizeof ts[0]; ti++) {
        uint32_t runs = 0, bonks = 0, th;
        char what[600];
        what[0] = 0;
        jc_slab(&b, ts[ti], 0u);
        set_static(ctx, &b);
        tb_free(&b);
        th = jc_slab_runs(ctx, &runs, &bonks, what, sizeof what);
        jc_info(ctx, &info);
        total_th += th;
        total_runs += runs;
        CHECK(th == 0 && info.grid_front_only > 0 && bonks > 0,
              "a roof %.0f thick (%.0f HU): %u runs (6 kinds: 12 standing jumps, 2 jump twice, running jumps at 3 / 6 / 9), %u "
              "through, %u stopped at the ceiling; %u front-only, %u undersides left out%s%s", (double)ts[ti],
              (double)ts[ti] * 4.0, (unsigned)runs, (unsigned)th, (unsigned)bonks, (unsigned)info.grid_front_only,
              (unsigned)info.grid_undersides, what[0] ? ":" : "", what);
        fflush(stdout);
    }
    /* the same roof 4 thick with the option off (SA1_GRID_UNDERSIDES_DROP: before round 0.9.3) and host-marked */
    {
        uint32_t runs = 0, bonks = 0, th;
        char what[600];
        what[0] = 0;
        jc_slab(&b, 4.0f, 0u);
        set_flat_grid(ctx, &b, SA1_GRID_UNDERSIDES_DROP);
        th = jc_slab_runs(ctx, &runs, &bonks, what, sizeof what);
        jc_info(ctx, &info);
        CHECK(th > 0 && info.grid_front_only == 0 && info.grid_undersides > 0,
              "SA1_GRID_UNDERSIDES_DROP (the one-way roofs before round 0.9.3): %u of %u runs through the 4-thick roof "
              "(%u undersides left out, %u front-only)", (unsigned)th, (unsigned)runs, (unsigned)info.grid_undersides,
              (unsigned)info.grid_front_only);
        tb_free(&b);
        jc_slab(&b, 4.0f, SA1_SURFACE_FRONT_ONLY);
        set_flat_grid(ctx, &b, SA1_GRID_UNDERSIDES_DROP);
        jc_info(ctx, &info);
        CHECK(info.grid_front_only == 0 && info.grid_undersides >= 2u,
              "with SA1_GRID_UNDERSIDES_DROP a host-marked FRONT_ONLY underside is left out too (%u left out, %u front-only)",
              (unsigned)info.grid_undersides, (unsigned)info.grid_front_only);
        set_flat_grid(ctx, &b, 0u);
        runs = bonks = 0;
        what[0] = 0;
        th = jc_slab_runs(ctx, &runs, &bonks, what, sizeof what);
        jc_info(ctx, &info);
        CHECK(th == 0 && info.grid_front_only >= 2u, "host-marked FRONT_ONLY (G-SA1's FIX-C3), the defaults: %u of %u runs "
              "through, %u front-only%s%s", (unsigned)th, (unsigned)runs, (unsigned)info.grid_front_only, what[0] ? ":" : "",
              what);
        tb_free(&b);
    }
    /* residual 1 (gsa1_091b_ceiling.md 7): a slab thinner than one step's rise - its single-sided top, touched from behind at
       the end of the step, won and put the character on the roof (the game's own rule for its single-polygon platforms).
       The top of a slab over a front-only underside (SA1CA_MAT_THIN_TOP) is not touched from more than a radius behind
       by a rising sphere: a roof 0.5 / 1 / 2 thick stops every kind. From above those roofs are solid as before: every
       kind dropped onto them, and running across them, stays on top */
    {
        static const float thin[3] = { 0.5f, 1.0f, 2.0f };
        uint32_t ti2, kk, th = 0, n = 0, fell = 0, nd = 0;
        char what[400];
        size_t wl = 0;
        what[0] = 0;
        for (ti2 = 0; ti2 < 3; ti2++) {
            jc_slab(&b, thin[ti2], 0u);
            set_static(ctx, &b);
            tb_free(&b);
            for (kk = 0; kk < JC_KINDS; kk++) {
                jc_res R;
                sa1_character_id id = 0;
                jc_jump(ctx, jc_kinds[kk], 0.0f, 0.0f, 32.0f, 0, 0.0f, &R);
                n++;
                if (R.through || R.fault) {
                    th++;
                    if (wl + 40 < sizeof what) {
                        wl += (size_t)snprintf(what + wl, sizeof what - wl, " %s/T%.1f", jc_kind_name(jc_kinds[kk]),
                                               (double)thin[ti2]);
                    }
                }
                /* dropped from 30 above the roof, then run across it */
                if (chr_at(ctx, jc_kinds[kk], 0.0f, 32.0f + thin[ti2] + 30.0f, 0.0f, 0, &id) == SA1_OK) {
                    run r;
                    int i;
                    float ymin = 1e9f;
                    run_init(&r, ctx, id);
                    for (i = 0; i < 80 && r.st == SA1_OK; i++) {
                        pair(&r, 0, i >= 30 && i < 60 ? 1.0f : 0.0f, YAW_X, (i == 45) ? SA1_BUTTON_JUMP : 0u);
                        if (i >= 20) {
                            ymin = r.x.sa1_position[1] < ymin ? r.x.sa1_position[1] : ymin;
                        }
                    }
                    nd++;
                    if (r.st != SA1_OK || ymin < 32.0f + thin[ti2] - 1.0f) {
                        fell++;
                        if (wl + 48 < sizeof what) {
                            wl += (size_t)snprintf(what + wl, sizeof what - wl, " %s/T%.1f fell to %.1f",
                                                   jc_kind_name(jc_kinds[kk]), (double)thin[ti2], (double)ymin);
                        }
                    }
                    sa1_character_destroy(ctx, id);
                }
            }
        }
        CHECK(th == 0 && fell == 0,
              "roofs 0.5 / 1 / 2 thick (thinner than a step's rise, residual 1): %u of %u jumps from below through; %u of %u "
              "characters dropped onto them and running across them fell into them%s%s", (unsigned)th, (unsigned)n,
              (unsigned)fell, (unsigned)nd, what[0] ? ":" : "", what);
    }
    /* 2. gm_bigcity's house: Sonic and Tails at the 77 spots of the ceiling doc, every kind at 12 (walls included) */
    for (k = 0; k < 2; k++) {
        uint32_t x, z, th_s = 0, th_t = 0, n = 0, th6 = 0, n6 = 0, kk;
        char what[600];
        size_t wl = 0;
        what[0] = 0;
        jc_house(&b, (int)k);
        set_static(ctx, &b);
        tb_free(&b);
        for (x = 0; x < 7; x++) {
            for (z = 0; z < 11; z++) {
                jc_res a, c;
                float px = 10.0f + 8.0f * (float)x, pz = 6.0f + 8.0f * (float)z;
                jc_jump(ctx, SA1_CHAR_SONIC, px, pz, 32.0f, 0, 0.0f, &a);
                jc_jump(ctx, SA1_CHAR_TAILS, px, pz, 32.0f, 0, 0.0f, &c);
                n++;
                th_s += (uint32_t)(a.through || a.fault);
                th_t += (uint32_t)(c.through || c.fault);
                if ((a.through || c.through) && wl + 40 < sizeof what) {
                    wl += (size_t)snprintf(what + wl, sizeof what - wl, " (%.0f %.0f)%s%s", (double)px, (double)pz,
                                           a.through ? "S" : "", c.through ? "T" : "");
                }
            }
        }
        for (kk = 0; kk < JC_KINDS; kk++) {
            static const float hx[3] = { 10.0f, 34.0f, 58.0f }, hz[4] = { 6.0f, 30.0f, 62.0f, 86.0f };
            for (x = 0; x < 12; x++) {
                jc_res a;
                jc_jump(ctx, jc_kinds[kk], hx[x % 3u], hz[x / 3u], 32.0f, x % 2u, 0.0f, &a);
                n6++;
                if (a.through || a.fault) {
                    th6++;
                    if (wl + 40 < sizeof what) {
                        wl += (size_t)snprintf(what + wl, sizeof what - wl, " %s(%.0f %.0f)", jc_kind_name(jc_kinds[kk]),
                                               (double)hx[x % 3u], (double)hz[x / 3u]);
                    }
                }
            }
        }
        jc_info(ctx, &info);
        CHECK(th_s == 0 && th_t == 0 && th6 == 0,
              "gm_bigcity's house (roof 0 .. 8 thick, walls as double-sided mid faces), ceiling %s: %u spots: Sonic %u / Tails %u "
              "through; every kind at 12 spots (jump / jump twice): %u of %u through; %u front-only%s%s",
              k ? "host-marked FRONT_ONLY" : "unmarked (GRID c2 decides)", (unsigned)n, (unsigned)th_s, (unsigned)th_t,
              (unsigned)th6, (unsigned)n6, (unsigned)info.grid_front_only, what[0] ? ":" : "", what);
        fflush(stdout);
    }
    /* 3. gm_construct_in_flatgrass_v3 (the owner's 0.9.2 notes): the flatgrass ground is a 1 HU slab (brush 117, z -8193 ..
       -8192) over a 59 HU crawlspace (brush 55's top at -8252); the terrain's base brush 732 (8 HU, -8608 .. -8600) lies 72 HU
       over the garage roof (brush 685, top -8680). Every kind that fits under them (E-102 is 84 HU tall, Big 68: they do
       not fit under 59 HU, their spheres reach through a 1 HU slab while they stand) jumps without going through */
    {
        static const struct {
            const char *name;
            float room, t;
        } rooms[2] = { { "flatgrass ground: 1 HU over 59 HU", 14.75f, 0.25f }, { "terrain base: 8 HU over 72 HU", 18.0f, 2.0f } };
        uint32_t ri, kk;
        for (ri = 0; ri < 2; ri++) {
            uint32_t th = 0, n = 0, unfit = 0, unfit_th = 0;
            char what[300];
            size_t wl = 0;
            what[0] = 0;
            memset(&b, 0, sizeof b);
            tb_floor(&b, -500, 1700, -600, 600, 0.0f, 1.0f, SA1_SURFACE_SOLID);
            tb_floor(&b, -200, 1200, -300, 300, rooms[ri].room + rooms[ri].t, 1.0f, SA1_SURFACE_SOLID);
            tb_floor(&b, -200, 1200, -300, 300, rooms[ri].room, -1.0f, SA1_SURFACE_SOLID);
            set_static(ctx, &b);
            tb_free(&b);
            for (kk = 0; kk < JC_KINDS; kk++) {
                int fits = 2.0f * kind_radius(jc_kinds[kk]) <= rooms[ri].room;
                uint32_t s;
                for (s = 0; s < 4; s++) {
                    jc_res R;
                    jc_jump(ctx, jc_kinds[kk], s < 2 ? -100.0f + 200.0f * (float)s : -100.0f, 0.0f, rooms[ri].room,
                            s < 2 ? 0 : (s == 2 ? 1 : 2), 6.0f, &R);
                    if (!fits) {
                        unfit++;
                        unfit_th += (uint32_t)R.through;
                        continue;
                    }
                    n++;
                    if (R.through || R.fault) {
                        th++;
                        if (wl + 40 < sizeof what) {
                            wl += (size_t)snprintf(what + wl, sizeof what - wl, " %s/%u", jc_kind_name(jc_kinds[kk]),
                                                   (unsigned)s);
                        }
                    }
                }
            }
            CHECK(th == 0, "gm_construct_in_flatgrass_v3's %s: %u jumps (standing, twice, running) of the kinds that fit, %u "
                  "through%s%s", rooms[ri].name, (unsigned)n, (unsigned)th, what[0] ? ":" : "", what);
            if (unfit > 0) {
                printf("  known %s: kinds taller than the room (%u jumps: E-102 / Big) cannot stand under it: %u went "
                       "through (their spheres reach through the slab while they stand)\n", rooms[ri].name,
                       (unsigned)unfit, (unsigned)unfit_th);
                g_known += unfit_th > 0;
            }
        }
    }
    INFO("thin roofs: %u runs, %u through", (unsigned)total_runs, (unsigned)total_th);
    sa1_collision_set_static(ctx, NULL, 0);
    jc_mover(E);
}

/* LOOP-L1 (gsa1_091a_loop.md 8 with its Review R.4): the synthetic loop of loop_diag --synth "512 32 16 320" in SA1 units: inner
 * radius 128, 8 thick, 16 segments, 80 wide (z +-40), centre (0, 128), the bottom on the floor at x 0; the run comes along -x
 * from x 606, the ground speed set while x > 0.6 R (a dash panel), stick held. The exit segments below 15 units are left out
 * (a real loop's exit lies beside its entry), so 14 segments; completed: 12 degrees short of their end (303 degrees). Each
 * kept segment as a closed slab: its inner face towards the axis (the running surface), the outer face, the two sides; the
 * end caps of the ribbon. curved: the inner faces carry SA1_SURFACE_CURVED (what G-SA1's curved-surface detector, LOOP-G1,
 * marks). Before: the inner faces of the upper half lie 8 units under the outer faces (within under_reach), so GRID c2 left
 * them out (or, round 0.9.3 without the flag, made the near-horizontal ones front-only) */
#define LP_R 128.0
#define LP_T 8.0
#define LP_W 80.0
#define LP_N 16

static uint32_t loop_scene(tbuf *b, int curved)
{
    uint32_t inner = SA1_SURFACE_SOLID | (curved ? SA1_SURFACE_CURVED : 0u), last = 0;
    int k, first = -1;
    memset(b, 0, sizeof *b);
    tb_floor(b, -800, 1000, -300, 300, 0.0f, 1.0f, SA1_SURFACE_SOLID);
    for (k = 0; k < LP_N; k++) {
        double a0 = -PI / 2 - 2 * PI * k / LP_N, a1 = -PI / 2 - 2 * PI * (k + 1) / LP_N, am = 0.5 * (a0 + a1), zmin = 1e300;
        float pi0[3], pi1[3], po0[3], po1[3], q[4][3], n[3];
        int j;
        for (j = 0; j < 4; j++) {
            double a = j & 1 ? a1 : a0, r = j & 2 ? LP_R + LP_T : LP_R, y = LP_R + r * sin(a);
            zmin = y < zmin ? y : zmin;
        }
        if (k >= LP_N / 2 && zmin < 15.0) {
            continue;
        }
        if (first < 0) {
            first = k;
        }
        last = (uint32_t)k;
        pi0[0] = (float)(LP_R * cos(a0)); pi0[1] = (float)(LP_R + LP_R * sin(a0)); pi0[2] = 0;
        pi1[0] = (float)(LP_R * cos(a1)); pi1[1] = (float)(LP_R + LP_R * sin(a1)); pi1[2] = 0;
        po0[0] = (float)((LP_R + LP_T) * cos(a0)); po0[1] = (float)(LP_R + (LP_R + LP_T) * sin(a0)); po0[2] = 0;
        po1[0] = (float)((LP_R + LP_T) * cos(a1)); po1[1] = (float)(LP_R + (LP_R + LP_T) * sin(a1)); po1[2] = 0;
#define LP_Q(i, p, zz) (q[i][0] = (p)[0], q[i][1] = (p)[1], q[i][2] = (float)(zz))
        /* inner face: towards the axis */
        LP_Q(0, pi0, -LP_W / 2); LP_Q(1, pi1, -LP_W / 2); LP_Q(2, pi1, LP_W / 2); LP_Q(3, pi0, LP_W / 2);
        n[0] = (float)-cos(am); n[1] = (float)-sin(am); n[2] = 0;
        tb_quad(b, q, n, inner);
        /* outer face */
        LP_Q(0, po0, -LP_W / 2); LP_Q(1, po1, -LP_W / 2); LP_Q(2, po1, LP_W / 2); LP_Q(3, po0, LP_W / 2);
        n[0] = (float)cos(am); n[1] = (float)sin(am);
        tb_quad(b, q, n, SA1_SURFACE_SOLID);
        /* the sides */
        LP_Q(0, pi0, -LP_W / 2); LP_Q(1, pi1, -LP_W / 2); LP_Q(2, po1, -LP_W / 2); LP_Q(3, po0, -LP_W / 2);
        n[0] = 0; n[1] = 0; n[2] = -1;
        tb_quad(b, q, n, SA1_SURFACE_SOLID);
        LP_Q(0, pi0, LP_W / 2); LP_Q(1, pi1, LP_W / 2); LP_Q(2, po1, LP_W / 2); LP_Q(3, po0, LP_W / 2);
        n[2] = 1;
        tb_quad(b, q, n, SA1_SURFACE_SOLID);
    }
    {
        /* the end cap of the last kept segment, facing along the travel (decreasing angle) */
        double a1 = -PI / 2 - 2 * PI * (last + 1) / LP_N;
        float pi1[3], po1[3], q[4][3], n[3];
        pi1[0] = (float)(LP_R * cos(a1)); pi1[1] = (float)(LP_R + LP_R * sin(a1)); pi1[2] = 0;
        po1[0] = (float)((LP_R + LP_T) * cos(a1)); po1[1] = (float)(LP_R + (LP_R + LP_T) * sin(a1)); po1[2] = 0;
        LP_Q(0, pi1, -LP_W / 2); LP_Q(1, po1, -LP_W / 2); LP_Q(2, po1, LP_W / 2); LP_Q(3, pi1, LP_W / 2);
        n[0] = (float)sin(a1); n[1] = (float)-cos(a1); n[2] = 0;
        tb_quad(b, q, n, SA1_SURFACE_SOLID);
#undef LP_Q
    }
    return last;
}

typedef struct loop_res {
    double phi_max;             /* progress around the loop while on it (degrees) */
    int entered, on_top, fault;
    float end[3];
} loop_res;

static void loop_run(sa1_context *ctx, float speed, loop_res *R)
{
    sa1_character_id id = 0;
    run r;
    int i, entered_at = -1, have = 0;
    double phi_prev = 0.0, phi_un = 0.0;
    memset(R, 0, sizeof *R);
    R->phi_max = -1e9;
    if (chr_at(ctx, SA1_CHAR_SONIC, 606.0f, 2.0f, 0.0f, 0x8000, &id) != SA1_OK) {
        R->fault = 1;
        return;
    }
    run_init(&r, ctx, id);
    for (i = 0; i < 450 && r.st == SA1_OK; i++) {
        double dx, dy, rr, phi, d;
        int on_loop;
        if (speed > 0.0f && grounded(&r) && !R->entered && r.x.sa1_position[0] > 0.6f * (float)LP_R) {
            set_ground_speed(&r, speed);
        }
        pair(&r, 0, i < 12 ? 0.0f : 1.0f, (float)PI + YAW_X, 0);
        dx = r.x.sa1_position[0];
        dy = r.x.sa1_position[1] - LP_R;
        rr = sqrt(dx * dx + dy * dy);
        phi = -90.0 - atan2(dy, dx) * 180.0 / PI;      /* 0 at the bottom, 90 on the -x side, 180 at the top */
        if (!have) {
            phi_un = phi;
            have = 1;
        } else {
            d = phi - phi_prev;
            while (d > 180.0) {
                d -= 360.0;
            }
            while (d < -180.0) {
                d += 360.0;
            }
            phi_un += d;
        }
        phi_prev = phi;
        on_loop = grounded(&r) && fabs(rr - LP_R) < 6.0 && fabs(r.x.sa1_position[2]) < LP_W / 2 + 10.0;
        if (!R->entered && on_loop && fabs(dx) < 0.25 * LP_R) {
            R->entered = 1;
            entered_at = i;
            phi_un = wrap_deg(phi);
            if (phi_un < -90.0) {
                phi_un += 360.0;
            }
        }
        if (R->entered && on_loop) {
            R->phi_max = phi_un > R->phi_max ? phi_un : R->phi_max;
        }
        if (dy > 0.0 && rr > LP_R + LP_T - 1.5 && fabs(dx) < LP_R + LP_T) {
            R->on_top = 1;
        }
        if (R->entered && i > entered_at + 300) {
            break;
        }
    }
    R->fault = r.st != SA1_OK;
    memcpy(R->end, r.x.sa1_position, sizeof R->end);
    sa1_character_destroy(ctx, id);
}

static void test_loop(G *E)
{
    static const float speeds[] = { 6.0f, 7.0f, 8.0f, 9.0f, 10.0f, 12.0f, 16.0f };
    static const char *const vname[] = { "GRID_UNDERSIDES_DROP, unflagged (round 0.9.2)", "the defaults, unflagged",
                                         "SA1_SURFACE_CURVED inner faces (LOOP-L1)", "SA1_GRID_KEEP_UNDERSIDES (control)" };
    sa1_context *ctx = E->ctx;
    double complete = 0.0;
    int v;
    printf("loop: the synthetic loop of gsa1_091a_loop.md (R 512 HU, 32 HU thick, 16 segments): Sonic at dash-panel speeds\n");
    for (v = 0; v < 4; v++) {
        sa1_collision_info info;
        tbuf b;
        uint32_t s, last, done = 0, top = 0, kept_slow = 0;
        char line[600];
        size_t len = 0;
        line[0] = 0;
        last = loop_scene(&b, v == 2);
        complete = 360.0 * (last + 1) / LP_N - 12.0;
        set_flat_grid(ctx, &b, v == 0 ? SA1_GRID_UNDERSIDES_DROP : v == 3 ? SA1_GRID_KEEP_UNDERSIDES : 0u);
        tb_free(&b);
        for (s = 0; s < sizeof speeds / sizeof speeds[0]; s++) {
            loop_res R;
            int ok;
            loop_run(ctx, speeds[s], &R);
            ok = R.entered && R.phi_max >= complete && !R.fault;
            done += (uint32_t)(ok && speeds[s] >= 9.0f);
            top += (uint32_t)(R.on_top && !ok);
            kept_slow += (uint32_t)(!ok && !R.on_top && speeds[s] <= 7.0f);
            if (len + 80 < sizeof line) {
                len += (size_t)snprintf(line + len, sizeof line - len, " %.0f: %s %.0f deg;", (double)speeds[s],
                                        R.fault ? "FAULT" : ok ? "completed" : R.on_top ? "ON TOP" : !R.entered ? "never reached"
                                                                                       : "fell off",
                                        R.phi_max < -1e8 ? 0.0 : R.phi_max);
            }
        }
        jc_info(ctx, &info);
        if (v == 2) {
            CHECK(done == 4u && top == 0 && kept_slow == 2u && info.grid_curved > 0 && info.grid_undersides == 0,
                  "%s: completed at 9 / 10 / 12 / 16 (%u of 4), never on top (%u), 6 / 7 fall back inside (%u of 2); %u curved "
                  "pieces kept, %u undersides left out, %u front-only:%s", vname[v], (unsigned)done, (unsigned)top,
                  (unsigned)kept_slow, (unsigned)info.grid_curved, (unsigned)info.grid_undersides,
                  (unsigned)info.grid_front_only, line);
        } else if (v == 0) {
            CHECK(done < 4u && info.grid_undersides > 0 && info.grid_curved == 0,
                  "%s: the loop loses its ceiling (%u undersides left out): %u of 4 runs at 9-16 complete, %u end on top:%s",
                  vname[v], (unsigned)info.grid_undersides, (unsigned)done, (unsigned)top, line);
        } else {
            INFO("%s: completed at 9-16: %u of 4, on top %u (%u undersides left out, %u front-only, %u curved):%s", vname[v],
                 (unsigned)done, (unsigned)top, (unsigned)info.grid_undersides, (unsigned)info.grid_front_only,
                 (unsigned)info.grid_curved, line);
        }
        fflush(stdout);
    }
    sa1_collision_set_static(ctx, NULL, 0);
}

int main(int argc, char **argv)
{
    static const test tests[] = {
        { "api", test_api },         { "flat", test_flat },       { "ramp", test_ramp },
        { "stairs", test_stairs },   { "wall", test_wall },       { "water", test_water },
        { "moving", test_moving },   { "rotating", test_rotating }, { "kill", test_kill },
        { "surfaces", test_surfaces }, { "capacity", test_capacity }, { "stacked", test_stacked },
        { "needle", test_needle },   { "plank", test_plank },     { "underside", test_underside },
        { "limits", test_limits },   { "ec1", test_ec1 },
        /* round 0.9.2 */
        { "hits", test_hits },       { "wallfloor", test_wallfloor }, { "ceiling", test_ceiling },
        { "kinds", test_kinds },     { "huge", test_huge },
        /* round 0.9.3 */
        { "jumpceiling", test_jumpceiling }, { "loop", test_loop },
    };
    static G E;
    const char *only[16];
    int nonly = 0, i, ec1 = 1;
    uint32_t t;
    (void)wr_replay;
    (void)wr_result_print;
    (void)wr_trace_write;
    (void)wr_rand_ahead;
    ga_args(&argc, argv);
    E.gdi = getenv("SA1_TEST_GDI");
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--only") && i + 1 < argc && nonly < 16) {
            only[nonly++] = argv[++i];
        } else if (!strcmp(argv[i], "--no-ec1")) {
            ec1 = 0;
        } else if (!strcmp(argv[i], "--verbose")) {
            g_verbose = 1;
        } else if (E.sadc == NULL) {
            E.sadc = argv[i];
        } else {
            E.gdi = argv[i];
        }
    }
    if (E.sadc == NULL || E.gdi == NULL || !*E.gdi) {
        printf("usage: grid_test [--only NAME]... [--no-ec1] [--verbose] SADC [GDI]\n");
        return 2;
    }
    printf("libsa1 %s GRID collision test: %d-bit, game code: %s\n", sa1_version_string(), (int)(sizeof(void *) * 8),
           sa1g_game_info());
    if (!sa1g_game_ready()) {
        printf("FAIL: the step's game code is not linked\n");
        return 1;
    }
    E.ctx = make_ctx(E.gdi);
    if (E.ctx == NULL) {
        printf("FAIL: context / disc\n");
        return 1;
    }
    for (t = 0; t < sizeof tests / sizeof tests[0]; t++) {
        int run_it = nonly == 0;
        for (i = 0; i < nonly; i++) {
            run_it |= !strcmp(only[i], tests[t].name);
        }
        if (!strcmp(tests[t].name, "ec1") && !ec1) {
            run_it = 0;
        }
        if (run_it) {
            double t0 = sa1_now_ms();
            tests[t].fn(&E);
            if (g_verbose) {
                printf("    (%s: %.0f ms)\n", tests[t].name, sa1_now_ms() - t0);
            }
        }
    }
    sa1_shutdown(E.ctx);
    CHECK(g_allocs == g_frees, "every context allocation was freed (%ld allocations, %ld frees)", g_allocs, g_frees);
    printf("%s: %d failure(s), %d known issue(s)\n", g_fail ? "FAIL" : "PASS", g_fail, g_known);
    return g_fail ? 1 : 0;
}
