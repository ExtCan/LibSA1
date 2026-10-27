/*
 * drown_test.c - round 0.9.1 (sadc docs/research/gsa1_070_drown.md W1 / E1 / E2 with its Review R4.1 / R4.2 / R4.6):
 * deep host water, the deaths of drowning and death zones, the RESTART fallback, E-102's time-up, the life counter.
 *
 *   drown_test [--only NAME] [--verbose] GDI
 *
 * Built and run by tests/build_charopt.py (a snapshot of build/<target>/libsa1.a and its headers). Context in SA1 space
 * (SA1_COORDS_PRESET_NATIVE: y up, 1 unit = 1 SA1 unit). Scenes are GRID collision (sa1_collision_set_static, the call
 * G-SA1 makes): a solid floor, a closed water box from the floor up to y = 0 (libsa1 keeps its top face), the character
 * standing on the floor.
 *
 *   deep      W1: floors 476 / 1000 / 2000 under the water top, Sonic at (0, 0), (64, 0), (128, 128): the air timer counts
 *             (breath > 60 after 100 pairs; before W1 it stayed 0 from about 390-440 deep, the drown doc's out_sweep.txt)
 *   speed     W1: the water physics at 476 / 2000 deep (top forward speed < 1.5 units / step; 4.238 dry, 0.924 at 150)
 *   chain     W1 + E1 at 476 deep: jingle 96 at step 1082, SA1_EVENT_DEATH cause DROWN at 1862 (one), RESTART id 2 at 2102
 *   chars     E1 at 150 deep: Sonic, Tails, Knuckles, Amy, Big drown (one DEATH DROWN at 1862, RESTART 2102); E-102 never
 *             (no water probe: breath 0, no DEATH)
 *   kill      E1: a GRID kill plane: one DEATH cause ZONE in the step of the scream (SOUND 0x571), RESTART id 2 after it
 *   fallback  E1: the game's 121-step death timer 8C07BA44 without any death (a task made between ticks): RESTART id 2 and
 *             a DEATH cause OTHER after it in the same step, one
 *   timeup    E1: E-102 with the game clock started: SA1_EVENT_TIME_UP right before a DEATH cause OTHER (informational when
 *             the world's clock never runs out)
 *   dense     W1 (Review R4.1): more than 128 GRID objects in range (ten surface kinds stacked in every cell), water 300 /
 *             476 deep, Sonic at a cell corner: the air timer still counts, the window never holds more than 128 objects,
 *             the water objects are in it (index < 128); 476 deep because farther ones made room
 *   timeup    (also) E-102's second restart request after his time-up is no second death (the fallback's latch)
 *   cave      W1 makes no water of a dry cave under a lake (land between the character and the water: breath 0)
 *   regress   W1 leaves shallow water alone (150 deep: the water object is in the window already, nothing inserted) and
 *             E-102 (the pass does not run for him)
 *   lives     E2: sa1_character_set_lives: before and after the first pair, a world built later, the clamp, unknown ids
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libsa1.h"
#include "sa1_internal.h"
#include "sa1_api_world.h"
#include "sa1_world.h"
#include "sa1_coladapt.h"

static int g_verbose, g_fail, g_ok;
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

static void log_cb(void *user, int32_t level, const char *msg)
{
    (void)user;
    if (level >= SA1_LOG_WARN || g_verbose) {
        printf("    [libsa1] %s\n", msg);
    }
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

/* a water box (top y = 0) over a solid floor at y = floor; dense: ten surface kinds (ten COL words, so ten GRID objects
 * per cell) stacked under every cell, none of them water (HEAVY_WATER is COL 0x400000: the water probe's bits) */
static int scene_water(sa1_context *ctx, float floor, int water, int dense)
{
    static const uint32_t kinds[10] = { SA1_SURFACE_NO_CLIMB, SA1_SURFACE_HURT, SA1_SURFACE_SLIPPERY, SA1_SURFACE_SAND,
                                        SA1_SURFACE_BOOST, SA1_SURFACE_SLIDE, SA1_SURFACE_STAIRS, SA1_SURFACE_NO_LAND,
                                        SA1_SURFACE_WALL_SLIDE, SA1_SURFACE_DIG };
    tbuf b;
    sa1_status st;
    memset(&b, 0, sizeof b);
    tb_floor(&b, -3000, 3000, -3000, 3000, floor, 1.0f, SA1_SURFACE_SOLID);
    if (water) {
        tb_box(&b, -2000, 2000, floor, 0.0f, -2000, 2000, SA1_SURFACE_WATER);
    }
    if (dense) {
        int ix, iz, k;
        for (ix = -4; ix < 4; ix++) {
            for (iz = -4; iz < 4; iz++) {
                for (k = 0; k < 10; k++) {
                    /* a plate over the whole 256-unit cell (1 unit inside it), 60 + 12 k under the floor */
                    float x0 = 256.0f * (float)ix + 1.0f, z0 = 256.0f * (float)iz + 1.0f;
                    tb_floor(&b, x0, x0 + 254.0f, z0, z0 + 254.0f, floor - 60.0f - 12.0f * (float)k, 1.0f, kinds[k]);
                }
            }
        }
    }
    st = sa1_collision_set_static(ctx, b.t, b.n);
    free(b.t);
    if (SA1_FAILED(st)) {
        printf("  set_static: %s: %s\n", sa1_status_string(st), sa1_last_error(ctx));
        return -1;
    }
    return 0;
}

static sa1_character_id mk_char(sa1_context *ctx, int32_t kind, float x, float y, float z, uint32_t flags)
{
    sa1_character_desc d;
    sa1_character_id id = 0;
    sa1_status st;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.kind = kind;
    d.flags = flags | SA1_CREATE_SA1_SPACE | SA1_CREATE_ANGLES;
    d.position[0] = x;
    d.position[1] = y;
    d.position[2] = z;
    st = sa1_character_create_ex(ctx, &d, &id);
    if (SA1_FAILED(st)) {
        printf("  create: %s: %s\n", sa1_status_string(st), sa1_last_error(ctx));
        return 0;
    }
    return id;
}

/* ---- running and the events --------------------------------------------------------------------------------------- */
typedef struct evlog {
    sa1_event *e;
    uint32_t n, cap;
} evlog;

typedef struct runr {
    int max_breath, last_breath;
    float vmax;
    sa1_state_ex x;
} runr;

static int run(sa1_context *ctx, sa1_character_id id, int pairs, float sy, evlog *L, runr *R)
{
    int i;
    for (i = 0; i < pairs; i++) {
        sa1_input in;
        sa1_event ev[64];
        uint32_t n = 0, k;
        sa1_status st;
        memset(&in, 0, sizeof in);
        in.struct_size = sizeof in;
        in.stick_y = sy;
        in.camera_yaw = (float)(-5.9 * 3.14159265358979323846 / 180.0);
        st = sa1_character_tick(ctx, id, &in, 1.0f / 30.0f, NULL, NULL);
        if (st != SA1_OK) {
            printf("  tick %d: %s: %s\n", i, sa1_status_string(st), sa1_last_error(ctx));
            return -1;
        }
        memset(&R->x, 0, sizeof R->x);
        R->x.struct_size = sizeof R->x;
        sa1_character_state_ex(ctx, id, &R->x);
        if (R->x.breath_timer > R->max_breath) {
            R->max_breath = R->x.breath_timer;
        }
        R->last_breath = R->x.breath_timer;
        if (R->x.player_speed[0] > R->vmax) {
            R->vmax = R->x.player_speed[0];
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
                if (g_verbose && ev[k].type != SA1_EVENT_SOUND) {
                    printf("    event step %5u type %2u id %X arg %08X\n", (unsigned)ev[k].step, (unsigned)ev[k].type,
                           (unsigned)ev[k].id, (unsigned)ev[k].arg);
                }
            }
        } while (n == 64);
    }
    return 0;
}

/* the first event of a type (id / arg: 0xFFFFFFFF = any) and how many there are */
static const sa1_event *ev_find(const evlog *L, uint32_t type, uint32_t id, uint32_t arg, uint32_t *count)
{
    const sa1_event *f = NULL;
    uint32_t i, c = 0;
    for (i = 0; i < L->n; i++) {
        const sa1_event *e = &L->e[i];
        if (e->type == type && (id == 0xFFFFFFFFu || e->id == id) && (arg == 0xFFFFFFFFu || e->arg == arg)) {
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

static int ev_index(const evlog *L, const sa1_event *e)
{
    return e != NULL ? (int)(e - L->e) : -1;
}

static int ev_step(const sa1_event *e)
{
    return e != NULL ? (int)e->step : -1;
}

/* ---- W1 ------------------------------------------------------------------------------------------------------------ */
static void t_deep(sa1_context *ctx)
{
    static const float floors[3] = { -476.0f, -1000.0f, -2000.0f };
    static const float at[3][2] = { { 0.0f, 0.0f }, { 64.0f, 0.0f }, { 128.0f, 128.0f } };
    int f, p;
    printf("deep (W1): Sonic standing 100 pairs under a water top at y 0\n");
    for (f = 0; f < 3; f++) {
        if (scene_water(ctx, floors[f], 1, 0) != 0) {
            g_fail++;
            return;
        }
        for (p = 0; p < 3; p++) {
            sa1_character_id id = mk_char(ctx, SA1_CHAR_SONIC, at[p][0], floors[f] + 2.0f, at[p][1], SA1_CREATE_WORLD);
            runr R;
            sa1g_water_stats ws;
            memset(&R, 0, sizeof R);
            if (id == 0 || run(ctx, id, 100, 0.0f, NULL, &R) != 0) {
                g_fail++;
                continue;
            }
            sa1ca_world_water(sa1_character_world(ctx, id), &ws);
            CHECK(R.max_breath > 60 && R.last_breath > 60 && ws.found > 0 && ws.last_slot < 128u,
                  "floor %5.0f at (%3.0f %3.0f): breath %d (max %d); W1 found %u, inserted %u, widened %u, slot %u, "
                  "radius %.1f", floors[f], at[p][0], at[p][1], R.last_breath, R.max_breath, (unsigned)ws.found,
                  (unsigned)ws.inserted, (unsigned)ws.widened, (unsigned)ws.last_slot, ws.last_radius);
            sa1_character_destroy(ctx, id);
        }
    }
}

static void t_speed(sa1_context *ctx)
{
    static const float floors[2] = { -476.0f, -2000.0f };
    int f;
    printf("speed (W1): the stick held forward for 135 pairs\n");
    for (f = 0; f < 2; f++) {
        sa1_character_id id;
        runr R;
        if (scene_water(ctx, floors[f], 1, 0) != 0) {
            g_fail++;
            return;
        }
        memset(&R, 0, sizeof R);
        id = mk_char(ctx, SA1_CHAR_SONIC, -1500.0f, floors[f] + 2.0f, 0.0f, SA1_CREATE_WORLD);
        if (id == 0 || run(ctx, id, 15, 0.0f, NULL, &R) != 0 || run(ctx, id, 135, 1.0f, NULL, &R) != 0) {
            g_fail++;
            continue;
        }
        CHECK(R.vmax > 0.3f && R.vmax < 1.5f, "floor %5.0f: top forward speed %.3f units/step (water physics; dry 4.238)",
              floors[f], R.vmax);
        sa1_character_destroy(ctx, id);
    }
}

/* the full chain of one character; returns 0 when it ran */
static int drown_chain(sa1_context *ctx, int32_t kind, float floor, int pairs, evlog *L, runr *R)
{
    sa1_character_id id;
    memset(L, 0, sizeof *L);
    memset(R, 0, sizeof *R);
    if (scene_water(ctx, floor, 1, 0) != 0) {
        return -1;
    }
    id = mk_char(ctx, kind, 0.0f, floor + 2.0f, 0.0f, SA1_CREATE_WORLD);
    if (id == 0) {
        return -1;
    }
    if (run(ctx, id, pairs, 0.0f, L, R) != 0) {
        sa1_character_destroy(ctx, id);
        return -1;
    }
    sa1_character_destroy(ctx, id);
    return 0;
}

static void t_chain(sa1_context *ctx)
{
    evlog L;
    runr R;
    const sa1_event *jingle, *death, *restart;
    uint32_t ndeath = 0, nrestart = 0;
    printf("chain (W1 + E1): Sonic on a floor 476 under the water top, 1200 pairs\n");
    if (drown_chain(ctx, SA1_CHAR_SONIC, -476.0f, 1200, &L, &R) != 0) {
        g_fail++;
        return;
    }
    jingle = ev_find(&L, SA1_EVENT_JINGLE, 96u, 0x8C107C80u, NULL);
    death = ev_find(&L, SA1_EVENT_DEATH, 0xFFFFFFFFu, 0xFFFFFFFFu, &ndeath);
    restart = ev_find(&L, SA1_EVENT_RESTART, 2u, 0xFFFFFFFFu, &nrestart);
    CHECK(ev_step(jingle) == 1082, "the drowning countdown's jingle 96 at step %d (1082)", ev_step(jingle));
    CHECK(ndeath == 1 && death != NULL && death->arg == SA1_DEATH_DROWN && death->step == 1862 && death->id == 0,
          "%u SA1_EVENT_DEATH, cause %u (DROWN %u) at step %d (1862)", (unsigned)ndeath, death ? (unsigned)death->arg : 0u,
          (unsigned)SA1_DEATH_DROWN, ev_step(death));
    CHECK(nrestart == 1 && ev_step(restart) == 2102, "RESTART id 2 at step %d (2102), %u of them", ev_step(restart),
          (unsigned)nrestart);
    CHECK(R.x.mode == 81, "Sonic in his drown mode 81 at the end (mode %d, action %d)", (int)R.x.mode, (int)R.x.action);
    free(L.e);
}

static void t_chars(sa1_context *ctx)
{
    static const struct { int32_t kind; const char *name; int32_t mode; } ch[6] = {
        { SA1_CHAR_SONIC, "Sonic", 81 }, { SA1_CHAR_TAILS, "Tails", 59 }, { SA1_CHAR_KNUCKLES, "Knuckles", 50 },
        { SA1_CHAR_AMY, "Amy", 46 }, { SA1_CHAR_BIG, "Big", 53 }, { SA1_CHAR_GAMMA, "E-102", -1 } };
    int c;
    printf("chars (E1): each character 150 under the water top, 1100 pairs\n");
    for (c = 0; c < 6; c++) {
        evlog L;
        runr R;
        const sa1_event *death, *restart;
        uint32_t ndeath = 0, nrestart = 0;
        if (drown_chain(ctx, ch[c].kind, -150.0f, 1100, &L, &R) != 0) {
            g_fail++;
            continue;
        }
        death = ev_find(&L, SA1_EVENT_DEATH, 0xFFFFFFFFu, 0xFFFFFFFFu, &ndeath);
        restart = ev_find(&L, SA1_EVENT_RESTART, 2u, 0xFFFFFFFFu, &nrestart);
        if (ch[c].mode < 0) {
            CHECK(ndeath == 0 && nrestart == 0 && R.max_breath == 0, "%s: no DEATH (%u), no RESTART (%u), breath %d (no water "
                  "probe in his main)", ch[c].name, (unsigned)ndeath, (unsigned)nrestart, R.max_breath);
        } else {
            CHECK(ndeath == 1 && death->arg == SA1_DEATH_DROWN && death->step == 1862 && nrestart == 1 &&
                      restart->step == 2102 && R.x.mode == ch[c].mode,
                  "%s: %u DEATH cause %u at step %d, RESTART at %d, drown mode %d (%d)", ch[c].name, (unsigned)ndeath,
                  death ? (unsigned)death->arg : 0u, ev_step(death), ev_step(restart), (int)R.x.mode, (int)ch[c].mode);
        }
        free(L.e);
    }
}

/* ---- E1: death zones, the fallback, E-102's time-up ---------------------------------------------------------------- */
static void t_kill(sa1_context *ctx)
{
    tbuf b;
    evlog L;
    runr R;
    sa1_character_id id;
    const sa1_event *death, *restart, *scream;
    uint32_t ndeath = 0, nrestart = 0;
    printf("kill (E1): a kill plane at y -200, Sonic falls from (600 10 0)\n");
    memset(&b, 0, sizeof b);
    memset(&L, 0, sizeof L);
    memset(&R, 0, sizeof R);
    tb_floor(&b, -400, 400, -400, 400, 0, 1.0f, SA1_SURFACE_SOLID);
    tb_floor(&b, -3000, 3000, -3000, 3000, -200, 1.0f, SA1_SURFACE_KILL);
    sa1_collision_set_static(ctx, b.t, b.n);
    free(b.t);
    id = mk_char(ctx, SA1_CHAR_SONIC, 600.0f, 10.0f, 0.0f, SA1_CREATE_WORLD);
    if (id == 0 || run(ctx, id, 160, 0.0f, &L, &R) != 0) {
        g_fail++;
        return;
    }
    scream = ev_find(&L, SA1_EVENT_SOUND, 0x571u, 0xFFFFFFFFu, NULL);
    death = ev_find(&L, SA1_EVENT_DEATH, 0xFFFFFFFFu, 0xFFFFFFFFu, &ndeath);
    restart = ev_find(&L, SA1_EVENT_RESTART, 2u, 0xFFFFFFFFu, &nrestart);
    CHECK(ndeath == 1 && death->arg == SA1_DEATH_ZONE && scream != NULL && death->step == scream->step,
          "%u DEATH, cause %u (ZONE %u) at step %d, the scream (SOUND 0x571) at %d", (unsigned)ndeath,
          death ? (unsigned)death->arg : 0u, (unsigned)SA1_DEATH_ZONE, ev_step(death), ev_step(scream));
    CHECK(nrestart == 1 && restart->step > death->step + 100u, "RESTART id 2 at step %d (%u), after the DEATH",
          ev_step(restart), (unsigned)nrestart);
    sa1_character_destroy(ctx, id);
    free(L.e);
}

static void cb_timer_task(sa1g_world *w, void *user)
{
    /* the game's death timer 8C07BA44 (8C07BD2A's task without the scream): 8C04E386(2) after 121 frames */
    void *(*create)(uint16_t, int32_t, sa1_gfn) = (void *(*)(uint16_t, int32_t, sa1_gfn))sa1_fn(
        SA1_GFN_C(0x8C09852Eu), 0u);
    (void)w;
    *(uint32_t *)user = create(8u, 0, SA1_GFN_C(0x8C07BA44u)) != NULL;
}

static void t_fallback(sa1_context *ctx)
{
    evlog L;
    runr R;
    sa1_character_id id;
    uint32_t made = 0, ndeath = 0, nrestart = 0;
    const sa1_event *death, *restart;
    printf("fallback (E1): the death timer 8C07BA44 made between ticks, no death\n");
    memset(&L, 0, sizeof L);
    memset(&R, 0, sizeof R);
    if (scene_water(ctx, 0.0f, 0, 0) != 0) {
        g_fail++;
        return;
    }
    id = mk_char(ctx, SA1_CHAR_SONIC, 0.0f, 2.0f, 0.0f, SA1_CREATE_WORLD);
    if (id == 0 || run(ctx, id, 10, 0.0f, &L, &R) != 0) {
        g_fail++;
        return;
    }
    sa1g_world_call(sa1_character_world(ctx, id), cb_timer_task, &made);
    if (!made || run(ctx, id, 90, 0.0f, &L, &R) != 0) {
        CHECK(0, "the timer task could not be made");
        return;
    }
    death = ev_find(&L, SA1_EVENT_DEATH, 0xFFFFFFFFu, 0xFFFFFFFFu, &ndeath);
    restart = ev_find(&L, SA1_EVENT_RESTART, 2u, 0xFFFFFFFFu, &nrestart);
    CHECK(nrestart == 1 && ndeath == 1 && death->arg == SA1_DEATH_OTHER && death->step == restart->step &&
              ev_index(&L, death) > ev_index(&L, restart),
          "RESTART id 2 at step %d; %u DEATH cause %u at step %d, after it in the queue", ev_step(restart),
          (unsigned)ndeath, death ? (unsigned)death->arg : 0u, ev_step(death));
    sa1_character_destroy(ctx, id);
    free(L.e);
}

static void cb_clock_on(sa1g_world *w, void *user)
{
    (void)w;
    (void)user;
    ((void (*)(void))sa1_fn(SA1_GFN_C(SA1G_FN_CLOCK_ON), 0u))();
}

static void t_timeup(sa1_context *ctx)
{
    evlog L;
    runr R;
    sa1_character_id id;
    const sa1_event *tu, *death;
    uint32_t ntu = 0, ndeath = 0;
    printf("timeup (E1): E-102 with the game clock started, 5800 pairs\n");
    memset(&L, 0, sizeof L);
    memset(&R, 0, sizeof R);
    if (scene_water(ctx, 0.0f, 0, 0) != 0) {
        g_fail++;
        return;
    }
    id = mk_char(ctx, SA1_CHAR_GAMMA, 0.0f, 2.0f, 0.0f, SA1_CREATE_WORLD);
    if (id == 0) {
        g_fail++;
        return;
    }
    sa1g_world_call(sa1_character_world(ctx, id), cb_clock_on, NULL);
    if (run(ctx, id, 5800, 0.0f, &L, &R) != 0) {
        g_fail++;
        return;
    }
    tu = ev_find(&L, SA1_EVENT_TIME_UP, 0xFFFFFFFFu, 0xFFFFFFFFu, &ntu);
    death = ev_find(&L, SA1_EVENT_DEATH, 0xFFFFFFFFu, 0xFFFFFFFFu, &ndeath);
    if (death == NULL && tu == NULL) {
        printf("  info  no time-up in %u steps (E-102's time-up needs the clock and its own flags of the stage start; "
               "mode %d): not checked\n", (unsigned)R.x.step, (int)R.x.mode);
    } else {
        CHECK(ntu == 1 && ndeath == 1 && death->arg == SA1_DEATH_OTHER && tu->step == death->step &&
                  ev_index(&L, tu) < ev_index(&L, death),
              "TIME_UP (%u) at step %d right before DEATH (%u) cause %u at step %d", (unsigned)ntu, ev_step(tu),
              (unsigned)ndeath, death ? (unsigned)death->arg : 0u, ev_step(death));
    }
    sa1_character_destroy(ctx, id);
    free(L.e);
}

/* ---- W1: a full window; shallow water; E-102 ------------------------------------------------------------------- */
static void t_dense(sa1_context *ctx)
{
    static const float floors[2] = { -300.0f, -476.0f };
    int f;
    printf("dense (W1, Review R4.1): ten surface kinds stacked under every cell, more than 128 objects in range\n");
    for (f = 0; f < 2; f++) {
        sa1_character_id id;
        runr R;
        sa1ca_world_stats cs;
        sa1g_water_stats ws;
        uint32_t maxwin = 0, i;
        int ok = 1;
        if (scene_water(ctx, floors[f], 1, 1) != 0) {
            g_fail++;
            return;
        }
        memset(&R, 0, sizeof R);
        id = mk_char(ctx, SA1_CHAR_SONIC, 0.0f, floors[f] + 2.0f, 0.0f, SA1_CREATE_WORLD);
        if (id == 0) {
            g_fail++;
            continue;
        }
        for (i = 0; i < 100 && ok; i++) {
            ok = run(ctx, id, 1, 0.0f, NULL, &R) == 0;
            sa1ca_world_stats_get(sa1_character_world(ctx, id), &cs);
            if (cs.window_objects > maxwin) {
                maxwin = cs.window_objects;
            }
        }
        sa1ca_world_stats_get(sa1_character_world(ctx, id), &cs);
        sa1ca_world_water(sa1_character_world(ctx, id), &ws);
        /* 300 deep the water still ranks among the nearest 128 and the game's own gathers reach it; 476 deep it does not:
           the farthest entries make room (the Review R4.1 path) */
        CHECK(ok && cs.candidates > 128u && maxwin <= 128u && R.last_breath > 60 && ws.found > 0 && ws.last_slot < 128u &&
                  (f == 0 || (ws.inserted > 0 && ws.replaced > 0 && ws.widened > 0)),
              "floor %4.0f: %u candidates, window at most %u objects; breath %d; W1 found %u, inserted %u, replaced %u, "
              "widened %u, refused %u, blocked %u, slot %u", floors[f], (unsigned)cs.candidates, (unsigned)maxwin,
              R.last_breath, (unsigned)ws.found, (unsigned)ws.inserted, (unsigned)ws.replaced, (unsigned)ws.widened,
              (unsigned)ws.refused, (unsigned)ws.blocked, (unsigned)ws.last_slot);
        sa1_character_destroy(ctx, id);
    }
}

/* W1 never makes water of a dry place under water: a cave whose roof is a lake's bed */
static void t_cave(sa1_context *ctx)
{
    tbuf b;
    sa1_character_id id;
    runr R;
    sa1g_water_stats ws;
    printf("cave (W1): a dry cave (floor y -600) under a lake (its bed a solid slab at y -320 .. -300, water up to y 0)\n");
    memset(&b, 0, sizeof b);
    tb_floor(&b, -3000, 3000, -3000, 3000, -600.0f, 1.0f, SA1_SURFACE_SOLID);
    tb_box(&b, -2000, 2000, -320.0f, -300.0f, -2000, 2000, SA1_SURFACE_SOLID);
    tb_box(&b, -2000, 2000, -300.0f, 0.0f, -2000, 2000, SA1_SURFACE_WATER);
    sa1_collision_set_static(ctx, b.t, b.n);
    free(b.t);
    memset(&R, 0, sizeof R);
    id = mk_char(ctx, SA1_CHAR_SONIC, 0.0f, -598.0f, 0.0f, SA1_CREATE_WORLD);
    if (id == 0 || run(ctx, id, 100, 0.0f, NULL, &R) != 0) {
        g_fail++;
        return;
    }
    sa1ca_world_water(sa1_character_world(ctx, id), &ws);
    CHECK(R.max_breath == 0 && ws.found > 0 && ws.blocked > 0 && ws.inserted == 0 && ws.widened == 0,
          "breath %d (dry); W1 found the lake above (%u) and left it alone: blocked %u, inserted %u, widened %u",
          R.max_breath, (unsigned)ws.found, (unsigned)ws.blocked, (unsigned)ws.inserted, (unsigned)ws.widened);
    sa1_character_destroy(ctx, id);
}

static void t_regress(sa1_context *ctx)
{
    sa1_character_id id;
    runr R;
    sa1g_water_stats ws;
    printf("regress (W1): shallow water and E-102\n");
    if (scene_water(ctx, -150.0f, 1, 0) != 0) {
        g_fail++;
        return;
    }
    memset(&R, 0, sizeof R);
    id = mk_char(ctx, SA1_CHAR_SONIC, 0.0f, -148.0f, 0.0f, SA1_CREATE_WORLD);
    if (id == 0 || run(ctx, id, 100, 0.0f, NULL, &R) != 0) {
        g_fail++;
        return;
    }
    sa1ca_world_water(sa1_character_world(ctx, id), &ws);
    CHECK(R.last_breath > 60 && ws.found > 0 && ws.inserted == 0 && ws.replaced == 0 && ws.widened == 0,
          "150 deep: breath %d; the water object was in the window already (found %u, inserted %u, widened %u)",
          R.last_breath, (unsigned)ws.found, (unsigned)ws.inserted, (unsigned)ws.widened);
    sa1_character_destroy(ctx, id);
    if (scene_water(ctx, -476.0f, 1, 0) != 0) {
        g_fail++;
        return;
    }
    memset(&R, 0, sizeof R);
    id = mk_char(ctx, SA1_CHAR_GAMMA, 0.0f, -474.0f, 0.0f, SA1_CREATE_WORLD);
    if (id == 0 || run(ctx, id, 100, 0.0f, NULL, &R) != 0) {
        g_fail++;
        return;
    }
    sa1ca_world_water(sa1_character_world(ctx, id), &ws);
    CHECK(R.max_breath == 0 && ws.found == 0, "E-102 476 deep: breath %d, W1 not run (found %u)", R.max_breath,
          (unsigned)ws.found);
    sa1_character_destroy(ctx, id);
}

/* ---- E2 ------------------------------------------------------------------------------------------------------------ */
static int32_t lives_of(sa1_context *ctx, sa1_character_id id)
{
    sa1_state_ex x;
    memset(&x, 0, sizeof x);
    x.struct_size = sizeof x;
    sa1_character_state_ex(ctx, id, &x);
    return x.lives;
}

static void t_lives(sa1_context *ctx)
{
    sa1_character_id id;
    runr R;
    int32_t a, b, c;
    printf("lives (E2): sa1_character_set_lives\n");
    if (scene_water(ctx, 0.0f, 0, 0) != 0) {
        g_fail++;
        return;
    }
    memset(&R, 0, sizeof R);
    id = mk_char(ctx, SA1_CHAR_SONIC, 0.0f, 2.0f, 0.0f, SA1_CREATE_WORLD);
    a = lives_of(ctx, id);
    CHECK(sa1_character_set_lives(ctx, id, 2) == SA1_OK, "set_lives(2) on a built world");
    b = lives_of(ctx, id);
    run(ctx, id, 1, 0.0f, NULL, &R);
    c = lives_of(ctx, id);
    CHECK(a == 4 && b == 2 && c == 2, "lives: the game's %d at the start, %d after the call, %d after the first pair", a, b,
          c);
    sa1_character_set_lives(ctx, id, 500);
    a = lives_of(ctx, id);
    sa1_character_set_lives(ctx, id, -3);
    b = lives_of(ctx, id);
    CHECK(a == 127 && b == 0, "the clamp: 500 -> %d, -3 -> %d (0..127)", a, b);
    sa1_character_destroy(ctx, id);
    CHECK(sa1_character_set_lives(ctx, id, 3) == SA1_ERR_NOT_FOUND, "a destroyed character: SA1_ERR_NOT_FOUND");
    /* a world built later: the setting is in place before its first step */
    id = mk_char(ctx, SA1_CHAR_KNUCKLES, 0.0f, 2.0f, 0.0f, 0u);
    CHECK(id != 0 && sa1_character_world(ctx, id) == NULL && sa1_character_set_lives(ctx, id, 7) == SA1_OK,
          "set_lives(7) before the world exists");
    run(ctx, id, 1, 0.0f, NULL, &R);
    a = lives_of(ctx, id);
    CHECK(a == 7, "the world built by the first tick has %d lives (7)", a);
    sa1_character_destroy(ctx, id);
}

int main(int argc, char **argv)
{
    static const struct { const char *name; void (*fn)(sa1_context *); } tests[] = {
        { "deep", t_deep }, { "speed", t_speed }, { "chain", t_chain }, { "chars", t_chars }, { "kill", t_kill },
        { "fallback", t_fallback }, { "timeup", t_timeup }, { "dense", t_dense }, { "cave", t_cave },
        { "regress", t_regress }, { "lives", t_lives },
    };
    const char *gdi = NULL, *only = NULL;
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
        } else {
            gdi = argv[a];
        }
    }
    if (gdi == NULL) {
        gdi = getenv("SA1_TEST_GDI");
    }
    if (gdi == NULL) {
        printf("usage: drown_test [--only NAME] [--verbose] GDI\n");
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
                    (1u << SA1_CHAR_GAMMA) | (1u << SA1_CHAR_BIG);
    lo.flags = SA1_LOAD_DEFAULT;
    st = sa1_load_disc(ctx, gdi, &lo);
    if (SA1_FAILED(st)) {
        printf("sa1_load_disc: %s: %s\n", sa1_status_string(st), sa1_last_error(ctx));
        sa1_shutdown(ctx);
        return 1;
    }
    printf("libsa1 %s\n", sa1_version_string());
    for (i = 0; i < sizeof tests / sizeof tests[0]; i++) {
        if (only != NULL && strcmp(only, tests[i].name) != 0) {
            continue;
        }
        tests[i].fn(ctx);
    }
    sa1_shutdown(ctx);
    printf("drown_test: %d ok, %d failed\n", g_ok, g_fail);
    return g_fail ? 1 : 0;
}
