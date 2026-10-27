/*
 * hostobj_test.c - libsa1 host objects (API 0.3, round 5's 5-lib-core: sadc docs/libsa1_interaction.md 4.2 with the
 * addendum, section 6), the ring API and the contacts with moving collision objects.
 *
 *   hostobj_test [--only NAME] [--verbose] GDI         (build and run: tests/build_hostobj.py --run)
 *
 * Every world: a flat GRID floor (host space = SA1_COORDS_PRESET_SOURCE: 4 HU per SA1 unit, z up), a character created
 * with SA1_CREATE_WORLD, ticked one pair per call (dt = 1/30 s) through the public API with scripted input. A step
 * wrapper (w->step) records player 0's state after every 60 Hz step and whether a watched proxy is in player 0's target
 * list B_8C73EE38 (it calls the default step, then the pair's frame advance that sa1g_world_pair leaves to the default
 * step, so the world runs exactly as without it).
 *
 *   api        the registry without a world: descriptions checked, ids, update / move / remove, the 1024 limit,
 *              configure, events and info of a context without characters
 *   rings      start_rings (50; 150 clamped to 99, no 1-up at the start), sa1_character_add_rings (the 1-up at 100 and
 *              its jingle SE 0x2C0, lives 4 -> 5; the count stays within 0..999), the sa1_state_ex tail
 *   inert      the layer changes nothing without objects in a world's window: the arena after 150 pairs of a script is
 *              bit-identical with the hooks detached, attached without objects, and with an object far away
 *   jump       (4.2 case 1) Sonic runs and jumps onto an ENEMY sphere r 4.5 (Kiki's numbers) 30 units ahead: one HIT
 *              (SPIN, level 1, damage 1, BOUNCED), motionwk.spd.y == 2.0 after the step of the hit (8C048980's
 *              SetVelocityP), score +10, no hurt, the proxy out of the target list for the 20 steps of its cooldown
 *   touch      a non-solid TARGET box with REPORT_TOUCH: running through it gives one TOUCH, no HIT, no hurt
 *   homing     (4.2 case 5) three ENEMYs on a line 40 units apart, 10 units up; jump, then the jump button in the air
 *              three times: three HITs (HOMING) in order, each BOUNCED, no hurt (the host removes each one KILLED on
 *              its hit, as G-SA1 does when the NPC dies)
 *   churn      (4.2 case 8) 1024 TARGETs on a grid above Sonic's run: at most 64 proxies, no CAPACITY event, every proxy
 *              created is destroyed, and the heap's free space back to its start once the objects are removed; the cost
 *              of a step with 64 proxies
 *   determinism (4.2 case 10) the homing chain twice in one process: the same event stream and the same arena; prints
 *              "DETERMINISM <crc>" for the x86 / x64 comparison of build_hostobj.py
 *   contacts   a moving collision object (a 128 x 128 x 64 HU box): Sonic runs into its side -> SIDE contacts with the
 *              normal facing him (-x), the game's wall flag; Sonic dropped onto it -> LANDING contacts, RIDING, normal +z
 *
 * Round 5's game side (5-lib-game: design 2.5, 2.6, 4.2 and the addendum 6.1-6.7):
 *   damage     (4.2 case 2) rings 5, walking into a touch-1 ENEMY: HURT (the object, 5 rings lost), wtimer 120, mode 16,
 *              5 scattered ring tasks, collectable again; nothing inside the 120 steps; NO_DEATH at 0 rings: HURT
 *              ABSORBED, no death; KEEP_RINGS: ABSORBED, rings kept; NO_DAMAGE: no HURT, no blink; 0 rings: HURT FATAL
 *              and SA1_EVENT_DEATH (cause HURT, the object) in the same step
 *   hostdmg    (4.2 case 7) sa1_character_damage FACE_SOURCE from behind: HURT HOST, he turns to the source and is knocked
 *              away from it; again during the invulnerability: HURT IGNORED; 4 pending at most; KILL: SA1_EVENT_DEATH
 *   early      (round 0.9.2, sadc docs/research/gsa1_090_faults.md F1 with its Review R.3.5) all six kinds,
 *              sa1_character_damage with flags 0, FACE_SOURCE and KILL right after the creation, before the first tick
 *              (G-SA1's respawn under fire): SA1_OK for 4 pairs and one SA1_EVENT_HURT (HOST, wtimer 120: the hit is
 *              held until the step after PInitialize), resp. one SA1_EVENT_DEATH (cause HOST); 0.9.1 faulted Sonic
 *              (8C0AFB10, read 00000004), Tails (8C0BBAE2) and, with KILL, all six (8C07BBC2), and the others lost the hit.
 *              A request that finds player 0's playerwk missing outside mode 0 (hidden for one step around the pre hook)
 *              is dropped, and the next request works
 *   roll       (4.2 case 3) a spin dash into a touch-1 ENEMY: HIT SPIN_DASH, no HURT; a touch-2 HAZARD hurts a jumping
 *              Sonic, not a spin-dashing one
 *   armor      (4.2 case 4) an armor-1 ENEMY survives Sonic's jump (no HIT); Knuckles' punch: HIT PUNCH, level 3, damage 2
 *   e102       (4.2 case 6) E-102 locks on three ENEMYs: LOCKED / TARGETED / HIT LASER (PROJECTILE) / UNLOCKED x3;
 *              REMOVE_KILLED pays the shot group's time bonus (1 + 2 + 3); TARGETs (NO_BONUS) and a SILENT removal pay none;
 *              NO_LOCKON: never locked
 *   worlds     (4.2 case 9) Sonic and Knuckles worlds with the same objects: hits per character, each world equal to a run
 *              of it alone (events and arena)
 *   lift       (6.7 cases 11, 12, 14) Sonic lifts a HOLDABLE (mode 21, LIFTED in mode 23, SE 0x45A, htp, 0x1000), carries
 *              it (no CCL entry, at the hands, sa1_character_held), throws it (THROW, P2G(2, 0.8, 0)), the proxy keeps the
 *              release point; puts it down (PUT_DOWN, speed 0, p.rad + body radius); aims it at an ENEMY (THROW_AIMED, 3
 *              per step, the target); is hurt while carrying (DROP)
 *   kinds      (6.7 case 13) Tails, Knuckles, Amy, E-102 and Big lift and throw (Big: (3, 3, 0)); a sphere-only HOLDABLE
 *              is not lifted; HEAVY: Sonic does not lift it, Big does
 *   forced     (6.7 case 15) sa1_hostobj_release / sa1_hostobj_remove at random steps of a carry (--runs, default 1000):
 *              the holder's own drop first, RELEASED FORCED, no fault, no stale htp
 *   balloon    (6.7 case 16) homing onto a BALLOON: HIT HOMING LAUNCHED, spd (0, 5, 0), mode 9 action 16, then 12 / 17;
 *              LAUNCH_BOUNCE at 600 HU/s: spd.y 2.5, the ball kept, a second balloon homed; E-102's laser: HIT LASER, not
 *              launched; POP_ON_TOUCH: every character launched by its own order-3 handler
 *   mask       (6.7 case 17, without a follower world: round 6b) sa1_hostobj_world_mask: only the mask's kinds are mirrored,
 *              a masked held object is RELEASED FORCED; a mask / option set before the world is built applies to it
 *   flags      DISABLED (no HIT, no hurt), NO_HOMING (never in the target list), HOLD_LOCKED (not lifted; an update that
 *              clears it: lifted, sa1_character_held LIFTING before LIFTED), HOLD_HITS (a jump into it is a HIT)
 *   determinism5 (4.2 case 10 for these paths) a lift and throw, a balloon launch, an enemy touch and a host hit twice
 *              in a row: the same events and arena; prints "DETERMINISM5 <events> <game events> <arena>" for x86 / x64
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
#include "sa1_hostobj.h"
#include "test_util.h"
#include "guard_alloc.c"    /* SA1_GUARD_ALLOC=1 or --guard-alloc: every library heap block against a guard page */

static int g_fail, g_checks, g_verbose;
static float g_jump_d = 40.0f;      /* jump: the jump is pressed this far (HU) before the enemy's centre */

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

/* ---- geometry (host space: x, y horizontal, z up, HU) ------------------------------------------------------------ */
typedef struct tris {
    sa1_collision_tri *t;
    uint32_t n, cap;
} tris;

static void tri_add(tris *b, const float a[3], const float c[3], const float d[3])
{
    sa1_collision_tri *t;
    if (b->n == b->cap) {
        uint32_t cap = b->cap ? 2u * b->cap : 1024u;
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

/* a closed box, faces counter-clockwise from outside */
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

/* ---- the recording step ------------------------------------------------------------------------------------------ */
#define REC_MAX 16384u
#define ADDR_TARGETS0   0x8C73EE38u     /* PL_TARGET B_8C73EE38[1160 + 1]: {twp, dist} */
#define ADDR_TARGETS0_N 0x8C747708u     /* Uint16 B_8C747708 */

typedef struct rec {
    uint32_t step;
    float pos[3], spd[3];
    int32_t mode;
    uint32_t tflag, wtimer;
    int in_list;                /* the watched proxy is in player 0's target list after this step's pass */
    int32_t action;             /* playerwk.mj.reqaction */
    float frame;                /* playerwk.mj.nframe */
} rec;

static rec g_rec[REC_MAX];
static uint32_t g_nrec;
static uint32_t g_watch;        /* guest taskwk of the watched proxy (0: none) */

static void rec_step(sa1g_world *w, int32_t sub, void *user)
{
    sa1g_player_ptrs pp;
    rec *r;
    uint32_t k, v = 0;
    sa1g_step_game(w, sub, user);
    if (sub == 1) {
        sa1g_call_dc(SA1G_FN_FRAME_ADVANCE);    /* what sa1g_world_pair does after a pair of the default step */
    }
    if (g_nrec >= REC_MAX) {
        return;
    }
    r = &g_rec[g_nrec++];
    memset(r, 0, sizeof *r);
    r->step = (uint32_t)w->steps;
    sa1g_world_player(w, &pp);
    if (pp.twp != 0) {
        const uint8_t *t = (const uint8_t *)sa1g_world_ptr(w, pp.twp);
        uint16_t f, wt;
        r->mode = (int8_t)t[0];
        memcpy(&f, t + 4, 2);
        memcpy(&wt, t + 6, 2);
        r->tflag = f;
        r->wtimer = wt;
        memcpy(r->pos, t + 0x20, sizeof r->pos);
    }
    if (pp.mwp != 0) {
        memcpy(r->spd, (const uint8_t *)sa1g_world_ptr(w, pp.mwp) + 4, sizeof r->spd);
    }
    if (pp.pwp != 0) {
        int16_t a;
        memcpy(&a, (const uint8_t *)sa1g_world_ptr(w, pp.pwp) + 0x124, 2);
        r->action = a;
        memcpy(&r->frame, (const uint8_t *)sa1g_world_ptr(w, pp.pwp) + 0x130, 4);
    }
    if (g_watch != 0) {
        uint16_t n = 0;
        memcpy(&n, sa1g_world_ptr(w, ADDR_TARGETS0_N), 2);
        for (k = 0; k < n && k < 1160u; k++) {
            if (sa1g_world_rd32(w, ADDR_TARGETS0 + 8u * k, &v) == 0 && v == g_watch) {
                r->in_list = 1;
                break;
            }
        }
    }
}

static const rec *rec_at(uint32_t step)
{
    uint32_t i;
    for (i = 0; i < g_nrec; i++) {
        if (g_rec[i].step == step) {
            return &g_rec[i];
        }
    }
    return NULL;
}

/* ---- context, characters, ticks ------------------------------------------------------------------------------------ */
#define HURT_X0 6000.0f                 /* the HURT platform: x, y 6000 .. 6256 HU, 16 HU high */
static sa1_context *make_ctx(const char *gdi)
{
    sa1_context *ctx = NULL;
    sa1_host h;
    sa1_load_options lo;
    sa1_coords co;
    sa1_status st;
    tris b;
    sa1_host_defaults(&h);
    h.log = log_cb;
    h.log_level = SA1_LOG_WARN;
    ga_host(&h);
    if (sa1_init(&h, &ctx) != SA1_OK) {
        return NULL;
    }
    memset(&co, 0, sizeof co);
    co.struct_size = sizeof co;
    sa1_coords_preset(SA1_COORDS_PRESET_SOURCE, &co);
    sa1_set_coords(ctx, &co);
    if (gdi != NULL) {
        memset(&lo, 0, sizeof lo);
        lo.struct_size = sizeof lo;
        lo.characters = (1u << SA1_CHAR_SONIC) | (1u << SA1_CHAR_TAILS) | (1u << SA1_CHAR_KNUCKLES) |
                        (1u << SA1_CHAR_AMY) | (1u << SA1_CHAR_GAMMA) | (1u << SA1_CHAR_BIG);
        lo.flags = SA1_LOAD_DEFAULT | SA1_LOAD_WORLD_IMAGES;
        st = sa1_load_disc(ctx, gdi, &lo);
        if (SA1_FAILED(st)) {
            printf("sa1_load_disc: %s: %s\n", sa1_status_string(st), sa1_last_error(ctx));
            sa1_shutdown(ctx);
            return NULL;
        }
        memset(&b, 0, sizeof b);
        floor_rect(&b, -8192.0f, -8192.0f, 8192.0f, 8192.0f, 0.0f, 1024.0f);
        {
            /* a HURT platform far from everything else (damage: SA1_HURT_SURFACE) */
            uint32_t k0 = b.n, k;
            box(&b, HURT_X0, HURT_X0, 0.0f, HURT_X0 + 256.0f, HURT_X0 + 256.0f, 16.0f);
            for (k = k0; k < b.n; k++) {
                b.t[k].surface = SA1_SURFACE_SOLID | SA1_SURFACE_HURT;
            }
        }
        st = sa1_collision_set_static(ctx, b.t, b.n);
        free(b.t);
        if (SA1_FAILED(st)) {
            printf("sa1_collision_set_static: %s\n", sa1_status_string(st));
            sa1_shutdown(ctx);
            return NULL;
        }
    }
    return ctx;
}

static sa1_character_id make_char(sa1_context *ctx, int32_t kind, float x, float y, float z, int32_t start_rings,
                                  int record)
{
    sa1_character_desc d;
    sa1_character_id id = 0;
    sa1_status st;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.kind = kind;
    d.flags = SA1_CREATE_WORLD | (start_rings >= 0 ? SA1_CREATE_START_RINGS : 0u);
    d.position[0] = x;
    d.position[1] = y;
    d.position[2] = z;
    d.start_rings = start_rings;
    st = sa1_character_create_ex(ctx, &d, &id);
    if (SA1_FAILED(st)) {
        printf("    create: %s: %s\n", sa1_status_string(st), sa1_last_error(ctx));
        return 0;
    }
    if (record) {
        sa1g_world *w = sa1_character_world(ctx, id);
        if (w != NULL) {
            w->step = rec_step;
        }
        g_nrec = 0;
        g_watch = 0;
    }
    return id;
}

static sa1_status tick(sa1_context *ctx, sa1_character_id id, float sx, float sy, uint32_t buttons)
{
    sa1_input in;
    sa1_status st;
    memset(&in, 0, sizeof in);
    in.struct_size = sizeof in;
    in.stick_x = sx;
    in.stick_y = sy;
    in.buttons = buttons;
    st = sa1_character_tick(ctx, id, &in, 1.0f / 30.0f, NULL, NULL);
    if (st != SA1_OK) {
        printf("    tick: %s: %s\n", sa1_status_string(st), sa1_last_error(ctx));
    }
    return st;
}

static void state_of(sa1_context *ctx, sa1_character_id id, sa1_state_ex *s)
{
    memset(s, 0, sizeof *s);
    s->struct_size = sizeof *s;
    sa1_character_state_ex(ctx, id, s);
}

/* host objects' events, collected */
typedef struct evlist {
    sa1_hostobj_event e[4096];
    uint32_t n;
} evlist;

static void poll_ho(sa1_context *ctx, evlist *L)
{
    uint32_t got = 0;
    if (L->n < 4096u) {
        sa1_poll_hostobj_events(ctx, L->e + L->n, 4096u - L->n, &got);
        L->n += got;
    }
}

/* game events (sa1_poll_events): count of a type / id */
static uint32_t poll_game(sa1_context *ctx, uint32_t type, uint32_t id, uint32_t *cap_events)
{
    sa1_event ev[256];
    uint32_t got = 0, i, n = 0;
    sa1_poll_events(ctx, ev, 256, &got);
    for (i = 0; i < got; i++) {
        if (ev[i].type == type && (id == 0xFFFFFFFFu || ev[i].id == id)) {
            n++;
        }
        if (cap_events != NULL && ev[i].type == SA1_EVENT_CAPACITY && ev[i].id == SA1_CAPACITY_HOST_OBJECTS) {
            (*cap_events)++;
        }
    }
    return n;
}

static void enemy_desc(sa1_hostobj_desc *d, uint32_t kind, float x, float y, float z, float r)
{
    memset(d, 0, sizeof *d);
    d->struct_size = sizeof *d;
    d->kind = kind;
    d->position[0] = x;
    d->position[1] = y;
    d->position[2] = z;
    d->armor = -1;
    d->touch = -1;
    d->num_shapes = 1;
    d->shapes[0].form = SA1_SHAPE_SPHERE;
    d->shapes[0].size[0] = r;
}

static void remove_all(sa1_context *ctx, const uint32_t *ids, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        sa1_hostobj_remove(ctx, ids[i], SA1_HOSTOBJ_REMOVE_SILENT);
    }
}

static uint32_t arena_crc(const sa1g_world *w)
{
    return w != NULL && w->ram != NULL ? tu_crc32(0, w->ram, SA1G_ARENA_SIZE) : 0u;
}

/* ---- api --------------------------------------------------------------------------------------------------------- */
static void t_api(void)
{
    sa1_context *ctx = make_ctx(NULL);
    sa1_hostobj_desc d;
    sa1_hostobj_config cfg;
    sa1_hostobj_info info;
    sa1_hostobj_event ev[4];
    uint32_t id = 0, id2 = 0, ids[1100], i, n = 0, got = 9;
    sa1_status st;
    float p[3] = { 1.0f, 2.0f, 3.0f };
    printf("\n== api (no disc: the registry alone)\n");
    if (ctx == NULL) {
        CHECK(0, "sa1_init");
        return;
    }
    enemy_desc(&d, SA1_HOSTOBJ_ENEMY, 0.0f, 0.0f, 0.0f, 18.0f);
    st = sa1_hostobj_add(ctx, &d, &id);
    CHECK(st == SA1_OK && id != 0, "add: %s, id %u", sa1_status_string(st), (unsigned)id);
    d.struct_size = 8;
    CHECK(sa1_hostobj_add(ctx, &d, &id2) == SA1_ERR_INVALID_ARGUMENT && id2 == 0, "add with a short struct_size refused");
    enemy_desc(&d, 9, 0.0f, 0.0f, 0.0f, 18.0f);
    CHECK(sa1_hostobj_add(ctx, &d, &id2) == SA1_ERR_INVALID_ARGUMENT, "unknown kind refused");
    enemy_desc(&d, SA1_HOSTOBJ_ENEMY, 0.0f, 0.0f, 0.0f, 0.0f);
    CHECK(sa1_hostobj_add(ctx, &d, &id2) == SA1_ERR_INVALID_ARGUMENT, "zero radius refused");
    enemy_desc(&d, SA1_HOSTOBJ_ENEMY, 0.0f, 0.0f, 0.0f, 4001.0f);
    CHECK(sa1_hostobj_add(ctx, &d, &id2) == SA1_ERR_INVALID_ARGUMENT, "radius beyond 1000 SA1 units refused");
    enemy_desc(&d, SA1_HOSTOBJ_ENEMY, 0.0f, 0.0f, 0.0f, 18.0f);
    d.shapes[0].form = 3;
    CHECK(sa1_hostobj_add(ctx, &d, &id2) == SA1_ERR_INVALID_ARGUMENT, "capsule form refused");
    enemy_desc(&d, SA1_HOSTOBJ_ENEMY, 0.0f, 0.0f, 0.0f, 18.0f);
    d.num_shapes = 5;
    CHECK(sa1_hostobj_add(ctx, &d, &id2) == SA1_ERR_INVALID_ARGUMENT, "5 shapes refused");
    enemy_desc(&d, SA1_HOSTOBJ_ENEMY, 0.0f, 0.0f, 0.0f, 18.0f);
    d.armor = 4;
    CHECK(sa1_hostobj_add(ctx, &d, &id2) == SA1_ERR_INVALID_ARGUMENT, "armor 4 refused");
    enemy_desc(&d, SA1_HOSTOBJ_ENEMY, NAN, 0.0f, 0.0f, 18.0f);
    CHECK(sa1_hostobj_add(ctx, &d, &id2) == SA1_ERR_INVALID_ARGUMENT, "NaN position refused");
    enemy_desc(&d, SA1_HOSTOBJ_TARGET, 0.0f, 0.0f, 0.0f, 18.0f);
    d.flags = 0x80000000u;
    CHECK(sa1_hostobj_add(ctx, &d, &id2) == SA1_ERR_INVALID_ARGUMENT, "unknown flags refused");
    enemy_desc(&d, SA1_HOSTOBJ_HAZARD, 0.0f, 0.0f, 0.0f, 18.0f);
    d.num_shapes = 2;
    d.shapes[1].form = SA1_SHAPE_BOX;
    d.shapes[1].size[0] = 10.0f;
    d.shapes[1].size[1] = 20.0f;
    d.shapes[1].size[2] = 30.0f;
    CHECK(sa1_hostobj_update(ctx, id, &d) == SA1_OK, "update to a HAZARD with a box");
    CHECK(sa1_hostobj_update(ctx, id + 1u, &d) == SA1_ERR_NOT_FOUND, "update of an unknown id: NOT_FOUND");
    CHECK(sa1_hostobj_move(ctx, id, p, 1.0f) == SA1_OK, "move");
    p[1] = INFINITY;
    CHECK(sa1_hostobj_move(ctx, id, p, 1.0f) == SA1_ERR_INVALID_ARGUMENT, "move to infinity refused");
    CHECK(sa1_hostobj_remove(ctx, id, 7) == SA1_ERR_INVALID_ARGUMENT, "remove with an unknown 'how' refused");
    CHECK(sa1_hostobj_remove(ctx, id, SA1_HOSTOBJ_REMOVE_KILLED) == SA1_OK, "remove (KILLED)");
    CHECK(sa1_hostobj_remove(ctx, id, SA1_HOSTOBJ_REMOVE_KILLED) == SA1_ERR_NOT_FOUND, "remove twice: NOT_FOUND");
    CHECK(sa1_hostobj_move(ctx, id, p, 1.0f) == SA1_ERR_NOT_FOUND, "move of a removed id: NOT_FOUND");
    {
        const sa1ho_registry *r = sa1ho_registry_of(ctx);
        uint32_t idx = (id & SA1HO_IDX_MASK) - 1u;
        CHECK(r != NULL && r->ent[idx].removed_id == id && r->ent[idx].removed_how == SA1_HOSTOBJ_REMOVE_KILLED,
              "the registry remembers how the id was removed");
    }
    enemy_desc(&d, SA1_HOSTOBJ_TARGET, 0.0f, 0.0f, 0.0f, 18.0f);
    for (i = 0; i < 1100u; i++) {
        st = sa1_hostobj_add(ctx, &d, &ids[i]);
        if (st != SA1_OK) {
            break;
        }
        n++;
    }
    CHECK(n == 1024u && st == SA1_ERR_LIMIT, "1024 objects per context, then SA1_ERR_LIMIT (%u added, %s)", (unsigned)n,
          sa1_status_string(st));
    {
        int uniq = 1;
        for (i = 1; i < n && uniq; i++) {
            uniq = ids[i] != ids[i - 1] && ids[i] != id;
        }
        CHECK(uniq, "ids are distinct and the removed id is not given out again at once");
    }
    remove_all(ctx, ids, n);
    memset(&info, 0, sizeof info);
    info.struct_size = sizeof info;
    CHECK(sa1_hostobj_info_get(ctx, 0, &info) == SA1_OK && info.objects == 0, "info: %u objects after removing all",
          (unsigned)info.objects);
    CHECK(sa1_hostobj_info_get(ctx, 12345, &info) == SA1_ERR_NOT_FOUND, "info of an unknown character: NOT_FOUND");
    memset(&cfg, 0, sizeof cfg);
    cfg.struct_size = sizeof cfg;
    cfg.window = 65;
    CHECK(sa1_hostobj_configure(ctx, &cfg) == SA1_ERR_INVALID_ARGUMENT, "window 65 refused");
    cfg.window = 16;
    cfg.enter_radius = 800.0f;
    cfg.leave_radius = 900.0f;
    CHECK(sa1_hostobj_configure(ctx, &cfg) == SA1_OK, "configure window 16, 800 / 900 HU");
    CHECK(sa1_poll_hostobj_events(ctx, ev, 4, &got) == SA1_OK && got == 0, "events without characters: none");
    /* round 5: the HOLDABLE / BALLOON kinds and their tails */
    enemy_desc(&d, SA1_HOSTOBJ_BALLOON, 0.0f, 0.0f, 0.0f, 18.0f);
    d.flags = SA1_HOSTOBJ_POP_ON_TOUCH | SA1_HOSTOBJ_LAUNCH_BOUNCE | SA1_HOSTOBJ_KEEP_HSPEED;
    d.launch_speed = 900.0f;
    CHECK(sa1_hostobj_add(ctx, &d, &id) == SA1_OK, "a BALLOON with launch_speed 900 HU/s");
    CHECK(sa1_hostobj_release(ctx, id) == SA1_ERR_INVALID_ARGUMENT, "sa1_hostobj_release of a BALLOON refused");
    d.launch_speed = -1.0f;
    CHECK(sa1_hostobj_update(ctx, id, &d) == SA1_ERR_INVALID_ARGUMENT, "a negative launch_speed refused");
    sa1_hostobj_remove(ctx, id, SA1_HOSTOBJ_REMOVE_SILENT);
    enemy_desc(&d, SA1_HOSTOBJ_HOLDABLE, 0.0f, 0.0f, 0.0f, 8.0f);
    d.shapes[0].form = SA1_SHAPE_BOX;
    d.shapes[0].size[0] = 60.0f;
    d.shapes[0].size[1] = 60.0f;
    d.shapes[0].size[2] = 20.0f;
    d.flags = SA1_HOSTOBJ_HOLD_HITS | SA1_HOSTOBJ_HOLD_LOCKED | SA1_HOSTOBJ_HEAVY;
    d.grab_reach = 40.0f;
    CHECK(sa1_hostobj_add(ctx, &d, &id) == SA1_OK, "a HOLDABLE box (half diagonal 85 HU = 21 SA1 units) with its flags");
    CHECK(sa1_hostobj_release(ctx, id) == SA1_OK, "sa1_hostobj_release of a HOLDABLE");
    d.shapes[0].size[0] = 120.0f;
    d.shapes[0].size[1] = 120.0f;
    CHECK(sa1_hostobj_update(ctx, id, &d) == SA1_ERR_INVALID_ARGUMENT, "a HOLDABLE body wider than 32 SA1 units refused");
    sa1_hostobj_remove(ctx, id, SA1_HOSTOBJ_REMOVE_SILENT);
    enemy_desc(&d, SA1_HOSTOBJ_HOLDABLE, 0.0f, 0.0f, 0.0f, 8.0f);
    d.struct_size = (uint32_t)SA1HO_DESC_MIN;   /* a 0.3-core host: no tails */
    CHECK(sa1_hostobj_add(ctx, &d, &id) == SA1_OK, "a description of the 0.3 core size (%u bytes) is accepted",
          (unsigned)SA1HO_DESC_MIN);
    sa1_hostobj_remove(ctx, id, SA1_HOSTOBJ_REMOVE_SILENT);
    enemy_desc(&d, 7, 0.0f, 0.0f, 0.0f, 18.0f);
    CHECK(sa1_hostobj_add(ctx, &d, &id) == SA1_ERR_INVALID_ARGUMENT, "kind 7 refused");
    {
        sa1_hostobj_held hd;
        sa1_damage dm;
        memset(&hd, 0, sizeof hd);
        hd.struct_size = sizeof hd;
        memset(&dm, 0, sizeof dm);
        dm.struct_size = sizeof dm;
        CHECK(sa1_character_held(ctx, 77, &hd) == SA1_ERR_NOT_FOUND, "held of an unknown character: NOT_FOUND");
        CHECK(sa1_character_damage(ctx, 77, &dm) == SA1_ERR_NOT_FOUND, "damage of an unknown character: NOT_FOUND");
        CHECK(sa1_character_set_option(ctx, 77, SA1_COPT_NO_DEATH, 1) == SA1_ERR_NOT_FOUND,
              "set_option of an unknown character: NOT_FOUND");
        CHECK(sa1_hostobj_world_mask(ctx, 77, 0) == SA1_ERR_NOT_FOUND, "world_mask of an unknown character: NOT_FOUND");
    }
    CHECK(sa1_character_add_rings(ctx, 77, 5) == SA1_ERR_NOT_FOUND, "add_rings of an unknown character: NOT_FOUND");
    {
        sa1_contact c[2];
        CHECK(sa1_character_contacts(ctx, 77, c, 2, &got) == SA1_ERR_NOT_FOUND, "contacts of an unknown character: "
              "NOT_FOUND");
    }
    sa1_shutdown(ctx);
    CHECK(sa1ho_registry_of(ctx) == NULL, "sa1_shutdown frees the context's registry");
}

/* ---- rings ------------------------------------------------------------------------------------------------------- */
static void t_rings(sa1_context *ctx)
{
    sa1_character_id id;
    sa1_state_ex s;
    uint32_t i, se;
    printf("\n== rings\n");
    id = make_char(ctx, SA1_CHAR_SONIC, 0.0f, 0.0f, 8.0f, 50, 0);
    state_of(ctx, id, &s);
    CHECK(id != 0 && s.rings == 50 && s.lives == 4 && s.score == 0, "start_rings 50: rings %d, lives %d, score %u before "
          "the first tick", (int)s.rings, (int)s.lives, (unsigned)s.score);
    CHECK(s.hurt_steps == 0xFFFFFFFFu && s.invulnerable_steps == 0, "hurt_steps 0xFFFFFFFF, invulnerable 0 at the start");
    for (i = 0; i < 10; i++) {
        tick(ctx, id, 0.0f, 0.0f, 0);
    }
    poll_game(ctx, 0, 0, NULL);
    state_of(ctx, id, &s);
    CHECK(s.rings == 50 && s.step == 20, "after 10 pairs: rings %d, step %u", (int)s.rings, (unsigned)s.step);
    CHECK(sa1_character_add_rings(ctx, id, 60) == SA1_OK, "add_rings(60)");
    state_of(ctx, id, &s);
    se = poll_game(ctx, SA1_EVENT_SOUND, 0x2C0u, NULL);
    CHECK(s.rings == 110 && s.lives == 5 && se == 1, "rings %d, lives %d (the 1-up at 100), the 1-up jingle SE 0x2C0 x%u",
          (int)s.rings, (int)s.lives, (unsigned)se);
    CHECK(sa1_character_add_rings(ctx, id, 2000) == SA1_OK, "add_rings(2000)");
    state_of(ctx, id, &s);
    CHECK(s.rings == 999, "rings capped at 999: %d (lives %d)", (int)s.rings, (int)s.lives);
    CHECK(sa1_character_add_rings(ctx, id, -5000) == SA1_OK, "add_rings(-5000)");
    state_of(ctx, id, &s);
    CHECK(s.rings == 0, "rings floored at 0: %d", (int)s.rings);
    poll_game(ctx, 0, 0, NULL);
    sa1_character_destroy(ctx, id);
    id = make_char(ctx, SA1_CHAR_SONIC, 0.0f, 0.0f, 8.0f, 150, 0);
    state_of(ctx, id, &s);
    se = poll_game(ctx, SA1_EVENT_SOUND, 0x2C0u, NULL);
    CHECK(s.rings == 99 && s.lives == 4 && se == 0, "start_rings 150 clamped to %d, lives %d, no 1-up jingle (%u)",
          (int)s.rings, (int)s.lives, (unsigned)se);
    sa1_character_destroy(ctx, id);
    id = make_char(ctx, SA1_CHAR_SONIC, 0.0f, 0.0f, 8.0f, -1, 0);
    state_of(ctx, id, &s);
    CHECK(s.rings == 0, "no start_rings: the game's 0 (%d)", (int)s.rings);
    sa1_character_destroy(ctx, id);
}

/* ---- inert ------------------------------------------------------------------------------------------------------- */
static uint32_t inert_run(sa1_context *ctx, int mode)
{
    sa1_character_id id = make_char(ctx, SA1_CHAR_SONIC, 0.0f, 0.0f, 8.0f, -1, 0);
    sa1g_world *w = sa1_character_world(ctx, id);
    uint32_t i, crc, far_id = 0;
    if (w == NULL) {
        return 0;
    }
    if (mode == 0) {
        w->hostobj_pre = NULL;      /* detached: the world core alone */
        w->hostobj_post = NULL;
    } else if (mode == 2) {
        sa1_hostobj_desc d;
        enemy_desc(&d, SA1_HOSTOBJ_ENEMY, 100000.0f, 0.0f, 18.0f, 18.0f);
        sa1_hostobj_add(ctx, &d, &far_id);
    }
    for (i = 0; i < 150; i++) {
        uint32_t b = (i == 40 || i == 90) ? SA1_BUTTON_JUMP : 0u;
        tick(ctx, id, 0.3f, i < 120 ? 1.0f : 0.0f, b);
    }
    crc = arena_crc(w);
    if (far_id != 0) {
        sa1_hostobj_remove(ctx, far_id, SA1_HOSTOBJ_REMOVE_SILENT);
    }
    sa1_character_destroy(ctx, id);
    poll_game(ctx, 0, 0, NULL);
    return crc;
}

static void t_inert(sa1_context *ctx)
{
    uint32_t a, b, c;
    printf("\n== inert\n");
    a = inert_run(ctx, 0);
    b = inert_run(ctx, 1);
    c = inert_run(ctx, 2);
    CHECK(a != 0 && a == b, "hooks attached without objects: arena %08X, detached %08X", (unsigned)b, (unsigned)a);
    CHECK(a == c, "an object out of every window: arena %08X", (unsigned)c);
}

/* ---- jump (case 1) ----------------------------------------------------------------------------------------------- */
static void t_jump(sa1_context *ctx)
{
    sa1_character_id id;
    sa1g_world *w;
    sa1_hostobj_desc d;
    sa1_state_ex s;
    evlist *L = (evlist *)calloc(1, sizeof *L);
    uint32_t obj = 0, i, hits = 0, pressed = 0, hit_step = 0, absent = 0, back = 0, score_hit = 0, in_cool = 0;
    const sa1_hostobj_event *h = NULL;
    printf("\n== jump (4.2 case 1)\n");
    id = make_char(ctx, SA1_CHAR_SONIC, 0.0f, 0.0f, 8.0f, -1, 1);
    w = sa1_character_world(ctx, id);
    if (id == 0 || w == NULL || L == NULL) {
        CHECK(0, "Sonic");
        free(L);
        return;
    }
    for (i = 0; i < 20; i++) {
        tick(ctx, id, 0.0f, 0.0f, 0);
    }
    state_of(ctx, id, &s);
    enemy_desc(&d, SA1_HOSTOBJ_ENEMY, s.position[0] + 120.0f, s.position[1], 18.0f, 18.0f);    /* 30 units ahead */
    d.user = 0xABCDEF0123ull;
    CHECK(sa1_hostobj_add(ctx, &d, &obj) == SA1_OK, "ENEMY sphere r 4.5 at 30 units (id %u)", (unsigned)obj);
    for (i = 0; i < 120 && hits == 0; i++) {
        uint32_t b = 0;
        state_of(ctx, id, &s);
        if (!pressed && s.position[0] >= d.position[0] - g_jump_d) {
            b = SA1_BUTTON_JUMP;
            pressed = i;
        }
        if (g_verbose) {
            printf("        pair %u: x %.1f z %.1f mode %d flag 0x%X%s\n", (unsigned)i, (double)s.position[0],
                   (double)s.position[2], (int)s.mode, (unsigned)s.game_flags, b ? " JUMP" : "");
        }
        tick(ctx, id, 0.0f, 1.0f, b);
        if (g_watch == 0) {
            g_watch = sa1ho_world_proxy_twp(w, obj);
        }
        poll_ho(ctx, L);
        hits = L->n;
    }
    state_of(ctx, id, &s);
    score_hit = s.score;                /* right after the tick of the hit */
    /* the cooldown (20 steps) and a little more; then the host's decision: killed (a bounce off a surviving object lands
     * on it again once its cooldown is over, as on an SA1 boss) */
    for (i = 0; i < 12; i++) {
        tick(ctx, id, 0.0f, 0.0f, 0);
        poll_ho(ctx, L);
    }
    for (i = 0; i < L->n; i++) {
        if (L->e[i].type == SA1_HOSTOBJ_EVENT_HIT && h == NULL) {
            h = &L->e[i];
        }
    }
    if (h != NULL) {
        for (i = 0; i < L->n; i++) {
            in_cool += L->e[i].type == SA1_HOSTOBJ_EVENT_HIT && L->e[i].step < h->step + 20u;
        }
    }
    CHECK(h != NULL && in_cool == 1, "one HIT (%u within the cooldown; jump pressed at pair %u, %.0f HU before the enemy)",
          (unsigned)in_cool, (unsigned)pressed, (double)g_jump_d);
    if (h != NULL) {
        const rec *r;
        CHECK(h->object == obj && h->user == 0xABCDEF0123ull && h->character == id, "HIT names the object, its user tag "
              "and the character");
        CHECK(h->attack == SA1_ATTACK_SPIN && h->level == 1 && h->damage == 1, "attack %u (SPIN %u), level %u, damage %u",
              (unsigned)h->attack, (unsigned)SA1_ATTACK_SPIN, (unsigned)h->level, (unsigned)h->damage);
        CHECK(h->flags & SA1_HITF_BOUNCED, "BOUNCED (flags 0x%X)", (unsigned)h->flags);
        hit_step = h->step;
        r = rec_at(hit_step);
        CHECK(r != NULL && r->spd[1] == 2.0f, "motionwk.spd.y after the step of the hit (step %u): %.6f (8C048980: 2.0)",
              (unsigned)hit_step, r ? (double)r->spd[1] : -1.0);
        for (i = 0; i < 20; i++) {
            const rec *q = rec_at(hit_step + i);
            absent += q != NULL && !q->in_list;
        }
        for (i = 20; i < 60 && !back; i++) {
            const rec *q = rec_at(hit_step + i);
            back = q != NULL && q->in_list ? i : 0;
        }
        CHECK(absent == 20, "the proxy is out of player 0's target list for the 20 steps of the cooldown (%u)",
              (unsigned)absent);
        printf("        (in the target list again %u steps after the hit; the hit event at %.1f %.1f %.1f HU, speed %.0f "
               "HU/s)\n", (unsigned)back, (double)h->position[0], (double)h->position[1], (double)h->position[2],
               (double)h->speed);
    }
    CHECK(score_hit == 10, "score %u after the hit (+10 from the game's damage check)", (unsigned)score_hit);
    sa1_hostobj_remove(ctx, obj, SA1_HOSTOBJ_REMOVE_KILLED);
    for (i = 0; i < 30; i++) {
        tick(ctx, id, 0.0f, 0.0f, 0);
        poll_ho(ctx, L);
    }
    state_of(ctx, id, &s);
    CHECK(s.hurt_steps == 0xFFFFFFFFu, "no hurt (hurt_steps 0x%X)", (unsigned)s.hurt_steps);
    {
        sa1_hostobj_info info;
        memset(&info, 0, sizeof info);
        info.struct_size = sizeof info;
        sa1_hostobj_info_get(ctx, id, &info);
        CHECK(info.proxies == 0 && info.created == 1 && info.destroyed == 1, "removed: proxies %u, created %u, destroyed "
              "%u", (unsigned)info.proxies, (unsigned)info.created, (unsigned)info.destroyed);
    }
    poll_game(ctx, 0, 0, NULL);
    sa1_character_destroy(ctx, id);
    free(L);
}

/* ---- touch: REPORT_TOUCH on a box ------------------------------------------------------------------------------------- */
static void t_touch(sa1_context *ctx)
{
    sa1_character_id id;
    sa1_hostobj_desc d;
    sa1_state_ex s;
    evlist *L = (evlist *)calloc(1, sizeof *L);
    uint32_t obj = 0, i, touches = 0, hits = 0;
    float x_in = 0.0f;
    printf("\n== touch (REPORT_TOUCH, a box shape)\n");
    id = make_char(ctx, SA1_CHAR_SONIC, 0.0f, 0.0f, 8.0f, -1, 0);
    if (id == 0 || L == NULL) {
        CHECK(0, "Sonic");
        free(L);
        return;
    }
    for (i = 0; i < 20; i++) {
        tick(ctx, id, 0.0f, 0.0f, 0);
    }
    state_of(ctx, id, &s);
    /* a TARGET box 80 x 80 x 40 HU on the floor 200 HU ahead, not solid, touch 0: the character runs through it */
    enemy_desc(&d, SA1_HOSTOBJ_TARGET, s.position[0] + 200.0f, s.position[1], 0.0f, 1.0f);
    d.flags = SA1_HOSTOBJ_REPORT_TOUCH | SA1_HOSTOBJ_NO_HOMING;
    d.shapes[0].form = SA1_SHAPE_BOX;
    d.shapes[0].center[2] = 20.0f;
    d.shapes[0].size[0] = 40.0f;
    d.shapes[0].size[1] = 40.0f;
    d.shapes[0].size[2] = 20.0f;
    CHECK(sa1_hostobj_add(ctx, &d, &obj) == SA1_OK, "TARGET box (REPORT_TOUCH) 200 HU ahead");
    for (i = 0; i < 60; i++) {
        uint32_t before = L->n, k;
        tick(ctx, id, 0.0f, 1.0f, 0);
        poll_ho(ctx, L);
        for (k = before; k < L->n; k++) {
            if (L->e[k].type == SA1_HOSTOBJ_EVENT_TOUCH && L->e[k].object == obj) {
                touches++;
                if (touches == 1) {
                    state_of(ctx, id, &s);
                    x_in = s.position[0];
                }
            }
            hits += L->e[k].type == SA1_HOSTOBJ_EVENT_HIT;
        }
    }
    state_of(ctx, id, &s);
    CHECK(touches == 1, "one TOUCH while running through it (%u; at x %.1f HU, the box spans %.0f .. %.0f)",
          (unsigned)touches, (double)x_in, (double)(d.position[0] - 40.0f), (double)(d.position[0] + 40.0f));
    CHECK(hits == 0 && s.hurt_steps == 0xFFFFFFFFu, "no HIT (%u), no hurt", (unsigned)hits);
    CHECK(s.position[0] > d.position[0] + 100.0f, "not solid: Sonic ran through to x %.0f HU", (double)s.position[0]);
    sa1_hostobj_remove(ctx, obj, SA1_HOSTOBJ_REMOVE_SILENT);
    tick(ctx, id, 0.0f, 0.0f, 0);
    poll_game(ctx, 0, 0, NULL);
    sa1_character_destroy(ctx, id);
    free(L);
}

/* ---- homing (case 5) and determinism (case 10) ----------------------------------------------------------------------- */
typedef struct homing_out {
    uint32_t hits, order_ok, homing, bounced, hurt;
    uint32_t crc_events, crc_arena;
    uint32_t steps[3];
} homing_out;

static void homing_run(sa1_context *ctx, homing_out *o, int verbose)
{
    sa1_character_id id;
    sa1g_world *w;
    sa1_hostobj_desc d;
    sa1_state_ex s;
    evlist *L = (evlist *)calloc(1, sizeof *L);
    uint32_t obj[3] = { 0, 0, 0 }, i, k, nh = 0, wait = 0, crc = 0;
    float x0;
    memset(o, 0, sizeof *o);
    id = make_char(ctx, SA1_CHAR_SONIC, 0.0f, 0.0f, 8.0f, -1, 1);
    w = sa1_character_world(ctx, id);
    if (id == 0 || w == NULL || L == NULL) {
        free(L);
        return;
    }
    for (i = 0; i < 20; i++) {
        tick(ctx, id, 0.0f, 0.0f, 0);
    }
    state_of(ctx, id, &s);
    x0 = s.position[0];
    for (k = 0; k < 3; k++) {
        enemy_desc(&d, SA1_HOSTOBJ_ENEMY, x0 + 160.0f * (float)(k + 1), s.position[1], 40.0f, 18.0f);
        d.user = k;
        sa1_hostobj_add(ctx, &d, &obj[k]);
    }
    tick(ctx, id, 0.0f, 0.0f, 0);
    tick(ctx, id, 0.0f, 0.0f, SA1_BUTTON_JUMP);         /* the jump */
    wait = 6;
    for (i = 0; i < 240 && nh < 3; i++) {
        uint32_t b = 0, before = L->n;
        if (wait != 0 && --wait == 0) {
            b = SA1_BUTTON_JUMP;                        /* the homing attack (in the air) */
        }
        tick(ctx, id, 0.0f, 0.0f, b);
        poll_ho(ctx, L);
        for (k = before; k < L->n; k++) {
            if (L->e[k].type == SA1_HOSTOBJ_EVENT_HIT) {
                nh++;
                wait = 5;
                /* the host's decision: the hit kills it (G-SA1 removes a killed NPC: REMOVE_KILLED at the next pair) */
                sa1_hostobj_remove(ctx, L->e[k].object, SA1_HOSTOBJ_REMOVE_KILLED);
            }
        }
    }
    for (i = 0; i < 40; i++) {
        tick(ctx, id, 0.0f, 0.0f, 0);
        poll_ho(ctx, L);
    }
    state_of(ctx, id, &s);
    o->hurt = s.hurt_steps != 0xFFFFFFFFu;
    o->order_ok = 1;
    for (k = 0; k < L->n; k++) {
        const sa1_hostobj_event *e = &L->e[k];
        if (e->type != SA1_HOSTOBJ_EVENT_HIT) {
            continue;
        }
        if (o->hits < 3) {
            o->order_ok &= e->object == obj[o->hits] && e->user == o->hits;
            o->steps[o->hits] = e->step;
        }
        o->hits++;
        o->homing += e->attack == SA1_ATTACK_HOMING;
        o->bounced += (e->flags & SA1_HITF_BOUNCED) != 0;
        if (verbose) {
            printf("        HIT object #%u at step %u: attack %u level %u damage %u flags 0x%X pos %.1f %.1f %.1f\n",
                   (unsigned)e->user, (unsigned)e->step, (unsigned)e->attack, (unsigned)e->level, (unsigned)e->damage,
                   (unsigned)e->flags, (double)e->position[0], (double)e->position[1], (double)e->position[2]);
        }
        /* the event stream without the ids (they differ between runs) */
        {
            uint32_t v[8] = { e->type, (uint32_t)e->user, e->attack, e->level, e->damage, e->shape, e->flags, e->step };
            crc = tu_crc32(crc, (const uint8_t *)v, sizeof v);
            crc = tu_crc32(crc, (const uint8_t *)e->position, sizeof e->position);
            crc = tu_crc32(crc, (const uint8_t *)e->direction, sizeof e->direction);
            crc = tu_crc32(crc, (const uint8_t *)&e->speed, sizeof e->speed);
        }
    }
    o->crc_events = crc;
    o->crc_arena = arena_crc(w);
    remove_all(ctx, obj, 3);        /* the ones not hit (NOT_FOUND for the others) */
    tick(ctx, id, 0.0f, 0.0f, 0);
    poll_game(ctx, 0, 0, NULL);
    sa1_character_destroy(ctx, id);
    free(L);
}

static void t_homing(sa1_context *ctx)
{
    homing_out o;
    printf("\n== homing (4.2 case 5)\n");
    homing_run(ctx, &o, 1);
    CHECK(o.hits == 3 && o.order_ok, "three HITs in order (%u, steps %u %u %u)", (unsigned)o.hits, (unsigned)o.steps[0],
          (unsigned)o.steps[1], (unsigned)o.steps[2]);
    CHECK(o.homing == 3, "all HOMING (%u)", (unsigned)o.homing);
    CHECK(o.bounced == 3, "each followed by the game's bounce (%u)", (unsigned)o.bounced);
    CHECK(!o.hurt, "no hurt");
}

static void t_determinism(sa1_context *ctx)
{
    homing_out a, b;
    printf("\n== determinism (4.2 case 10)\n");
    homing_run(ctx, &a, 0);
    homing_run(ctx, &b, 0);
    CHECK(a.hits == 3 && a.crc_events == b.crc_events, "the event stream twice in a row: %08X / %08X",
          (unsigned)a.crc_events, (unsigned)b.crc_events);
    CHECK(a.crc_arena == b.crc_arena, "the arena twice in a row: %08X / %08X", (unsigned)a.crc_arena,
          (unsigned)b.crc_arena);
    printf("DETERMINISM %08X %08X\n", (unsigned)a.crc_events, (unsigned)a.crc_arena);
}

/* ---- churn (case 8) ---------------------------------------------------------------------------------------------- */
static void t_churn(sa1_context *ctx)
{
    sa1_character_id id;
    sa1g_world *w;
    sa1_hostobj_desc d;
    sa1_hostobj_info info;
    sa1_state_ex s;
    uint32_t *ids = (uint32_t *)calloc(1024, sizeof *ids), i, k, n = 0, maxp = 0, caps = 0, free0 = 0, free1 = 0;
    uint32_t largest = 0;
    double t0, ms = 0.0, ms_full = 0.0;
    uint32_t full_pairs = 0;
    printf("\n== churn (4.2 case 8)\n");
    id = make_char(ctx, SA1_CHAR_SONIC, -1600.0f, 0.0f, 8.0f, -1, 0);
    w = sa1_character_world(ctx, id);
    if (id == 0 || w == NULL || ids == NULL) {
        CHECK(0, "Sonic");
        free(ids);
        return;
    }
    for (i = 0; i < 20; i++) {
        tick(ctx, id, 0.0f, 0.0f, 0);
    }
    sa1ho_world_heap(w, &free0, &largest);
    state_of(ctx, id, &s);
    for (i = 0; i < 32; i++) {
        for (k = 0; k < 32; k++) {
            enemy_desc(&d, SA1_HOSTOBJ_TARGET, -1550.0f + 100.0f * (float)i, -1550.0f + 100.0f * (float)k, 600.0f, 20.0f);
            if (sa1_hostobj_add(ctx, &d, &ids[n]) == SA1_OK) {
                n++;
            }
        }
    }
    CHECK(n == 1024, "1024 TARGETs on a 32 x 32 grid 150 units above the floor (%u)", (unsigned)n);
    memset(&info, 0, sizeof info);
    info.struct_size = sizeof info;
    for (i = 0; i < 300; i++) {
        state_of(ctx, id, &s);
        t0 = tu_now_ms();
        tick(ctx, id, 0.0f, i < 270 && s.position[0] < 2400.0f ? 1.0f : 0.0f, 0);
        t0 = tu_now_ms() - t0;
        ms += t0;
        sa1_hostobj_info_get(ctx, id, &info);
        if (info.proxies == 64) {
            ms_full += t0;
            full_pairs++;
        }
        maxp = info.proxies > maxp ? info.proxies : maxp;
        poll_game(ctx, 0, 0, &caps);
    }
    state_of(ctx, id, &s);
    printf("        Sonic ran to x %.0f HU; created %u, destroyed %u, refused %u\n", (double)s.position[0],
           (unsigned)info.created, (unsigned)info.destroyed, (unsigned)info.refused);
    CHECK(maxp <= 64 && maxp == 64, "at most 64 proxies (max %u)", (unsigned)maxp);
    CHECK(caps == 0 && info.refused == 0, "no CAPACITY event (%u), nothing refused (%u)", (unsigned)caps,
          (unsigned)info.refused);
    CHECK(info.created > 64 && info.created == info.destroyed + info.proxies, "churn: created %u = destroyed %u + live %u",
          (unsigned)info.created, (unsigned)info.destroyed, (unsigned)info.proxies);
    printf("        cost: %.3f ms per pair over the run; %.3f ms per pair with 64 proxies (%u pairs) = %.1f us per step\n",
           ms / 300.0, full_pairs ? ms_full / (double)full_pairs : 0.0, (unsigned)full_pairs,
           full_pairs ? ms_full / (double)full_pairs * 500.0 : 0.0);
    remove_all(ctx, ids, n);
    tick(ctx, id, 0.0f, 0.0f, 0);
    tick(ctx, id, 0.0f, 0.0f, 0);
    sa1_hostobj_info_get(ctx, id, &info);
    sa1ho_world_heap(w, &free1, &largest);
    CHECK(info.proxies == 0 && info.created == info.destroyed, "all removed: proxies %u (created %u, destroyed %u)",
          (unsigned)info.proxies, (unsigned)info.created, (unsigned)info.destroyed);
    CHECK(free0 == free1 && free1 == info.heap_free, "the heap's free space back to its start: %u -> %u bytes (largest %u)",
          (unsigned)free0, (unsigned)free1, (unsigned)largest);
    poll_game(ctx, 0, 0, NULL);
    sa1_character_destroy(ctx, id);
    free(ids);
}

/* ---- contacts ----------------------------------------------------------------------------------------------------- */
static uint32_t box_object(sa1_context *ctx, float x, float y, float z)
{
    tris b;
    uint32_t id = 0;
    float pos[3] = { x, y, z };
    sa1_status st;
    memset(&b, 0, sizeof b);
    box(&b, -64.0f, -64.0f, 0.0f, 64.0f, 64.0f, 64.0f);
    st = sa1_collision_object_add(ctx, b.t, b.n, pos, NULL, &id);
    free(b.t);
    return st == SA1_OK ? id : 0u;
}

static void t_contacts(sa1_context *ctx)
{
    sa1_character_id id;
    sa1_contact c[512];
    sa1_state_ex s;
    uint32_t mover, i, k, got = 0, side = 0, side_ok = 0, wall = 0, first = 0, land = 0, land_ok = 0, riding = 0;
    uint32_t other = 0, v_ok = 0;
    printf("\n== contacts (moving collision objects)\n");
    mover = box_object(ctx, 400.0f, 0.0f, 0.0f);
    CHECK(mover != 0, "a 128 x 128 x 64 HU box as a moving collision object at x 400 HU (id %u)", (unsigned)mover);
    /* Sonic runs into its side (the face at x 336) */
    id = make_char(ctx, SA1_CHAR_SONIC, 0.0f, 0.0f, 8.0f, -1, 0);
    for (i = 0; i < 20; i++) {
        tick(ctx, id, 0.0f, 0.0f, 0);
    }
    sa1_character_contacts(ctx, id, c, 512, &got);
    CHECK(got == 0, "no contact while standing away from it (%u)", (unsigned)got);
    for (i = 0; i < 90; i++) {
        tick(ctx, id, 0.0f, 1.0f, 0);
        got = 0;
        sa1_character_contacts(ctx, id, c, 512, &got);
        for (k = 0; k < got; k++) {
            if (c[k].object != mover || c[k].character != id) {
                other++;
                continue;
            }
            if (c[k].type == SA1_CONTACT_SIDE) {
                side++;
                side_ok += c[k].normal[0] < -0.9f && fabsf(c[k].normal[2]) < 0.2f;
                wall += (c[k].flags & SA1_CONTACTF_WALL) != 0;
                if (c[k].flags & SA1_CONTACTF_FIRST) {
                    first++;
                    v_ok += c[k].velocity[0] > 0.0f;
                    if (g_verbose || first == 1) {
                        printf("        first side contact at step %u: point %.1f %.1f %.1f normal %.2f %.2f %.2f velocity "
                               "%.0f -> %.0f HU/s, depth %.2f HU, flags 0x%X\n", (unsigned)c[k].step,
                               (double)c[k].point[0], (double)c[k].point[1], (double)c[k].point[2], (double)c[k].normal[0],
                               (double)c[k].normal[1], (double)c[k].normal[2], (double)c[k].velocity[0],
                               (double)c[k].velocity_after[0], (double)c[k].depth, (unsigned)c[k].flags);
                    }
                }
            } else {
                other++;
            }
        }
    }
    state_of(ctx, id, &s);
    CHECK(side > 0 && side_ok == side, "SIDE contacts while pushing against it: %u, normal -x in %u", (unsigned)side,
          (unsigned)side_ok);
    CHECK(wall > 0, "the game's wall response marked %u of them (SA1_CONTACTF_WALL)", (unsigned)wall);
    CHECK(first >= 1 && v_ok == first, "the first contact carries the run's velocity into the box (%u of %u)",
          (unsigned)v_ok, (unsigned)first);
    CHECK(other == 0 && s.position[0] < 340.0f, "no other contact types (%u); Sonic stopped at x %.1f HU, outside the box",
          (unsigned)other, (double)s.position[0]);
    sa1_character_destroy(ctx, id);
    /* Sonic dropped onto it */
    id = make_char(ctx, SA1_CHAR_SONIC, 400.0f, 0.0f, 120.0f, -1, 0);
    other = 0;
    for (i = 0; i < 40; i++) {
        tick(ctx, id, 0.0f, 0.0f, 0);
        got = 0;
        sa1_character_contacts(ctx, id, c, 512, &got);
        for (k = 0; k < got; k++) {
            if (c[k].type == SA1_CONTACT_LANDING && c[k].object == mover) {
                land++;
                land_ok += c[k].normal[2] > 0.9f;
                riding += (c[k].flags & SA1_CONTACTF_RIDING) != 0;
            } else {
                other++;
            }
        }
    }
    state_of(ctx, id, &s);
    CHECK(land >= 40 && land_ok == land, "LANDING contacts while standing on it: %u, normal +z in %u", (unsigned)land,
          (unsigned)land_ok);
    CHECK(riding >= 40, "the game carries him (RIDING, playerwk.ttp) in %u", (unsigned)riding);
    CHECK(other == 0 && s.position[2] > 60.0f, "no other contacts (%u); Sonic stands at z %.1f HU on the box",
          (unsigned)other, (double)s.position[2]);
    sa1_character_destroy(ctx, id);
    sa1_collision_object_remove(ctx, mover);
    poll_game(ctx, 0, 0, NULL);
}

/* ==== round 5's game side (5-lib-game): design 4.2 cases 2, 3, 4, 6, 7, 9 and the addendum's 11-17 ================== */
#define ADDR_PLAYER_PWP  0x8C78C668u
#define ADDR_TASK_LISTS  0x8C79F04Cu
#define ADDR_BONUS_TASK  0x8C423048u    /* task *D_8C423048: E-102's running bonus counter (8C0D520A) */
#define EXEC_RING_SCATTER 0x0C0870EAu   /* the scattered rings (8C086F5E) */

static const uint8_t *wptr(const sa1g_world *w, uint32_t g, uint32_t n)
{
    if (g == 0 || ((g >> 24) & 31u) != SA1G_SEG_ARENA || (g & 0xFFFFFFu) > SA1G_ARENA_SIZE - n) {
        return NULL;
    }
    return w->ram + (g & 0xFFFFFFu);
}

static uint32_t wrd32(const sa1g_world *w, uint32_t g)
{
    const uint8_t *p = wptr(w, g, 4);
    uint32_t v = 0;
    if (p != NULL) {
        memcpy(&v, p, 4);
    }
    return v;
}

static uint32_t wrd16(const sa1g_world *w, uint32_t g)
{
    const uint8_t *p = wptr(w, g, 2);
    uint16_t v = 0;
    if (p != NULL) {
        memcpy(&v, p, 2);
    }
    return v;
}

static uint32_t wrd8(const sa1g_world *w, uint32_t g)
{
    const uint8_t *p = wptr(w, g, 1);
    return p != NULL ? *p : 0u;
}

static float wrdf(const sa1g_world *w, uint32_t g)
{
    uint32_t v = wrd32(w, g);
    float f;
    memcpy(&f, &v, 4);
    return f;
}

/* tasks of every level whose exec is `exec` (the top-level lists, as the executor walks them) */
static uint32_t count_tasks(const sa1g_world *w, uint32_t exec, uint32_t *first)
{
    uint32_t lvl, n = 0;
    if (first != NULL) {
        *first = 0;
    }
    for (lvl = 0; lvl < 8; lvl++) {
        uint32_t tp = wrd32(w, ADDR_TASK_LISTS + 4u * lvl), guard = 0;
        while (tp != 0 && guard++ < 0x2000u) {
            if ((wrd32(w, tp + 0x10u) & 0x1FFFFFFFu) == (exec & 0x1FFFFFFFu)) {
                if (first != NULL && n == 0) {
                    *first = tp;
                }
                n++;
            }
            tp = wrd32(w, tp);
        }
    }
    return n;
}

/* game events (sa1_poll_events), collected */
typedef struct glist {
    sa1_event e[2048];
    uint32_t n;
} glist;

static void gpoll(sa1_context *ctx, glist *G)
{
    uint32_t got = 0;
    if (G->n < 2048u) {
        sa1_poll_events(ctx, G->e + G->n, 2048u - G->n, &got);
        G->n += got;
    } else {
        sa1_event tmp[64];
        sa1_poll_events(ctx, tmp, 64, &got);
    }
}

static uint32_t gcount(const glist *G, uint32_t from, uint32_t type, uint32_t id)
{
    uint32_t i, n = 0;
    for (i = from; i < G->n; i++) {
        n += G->e[i].type == type && (id == 0xFFFFFFFFu || G->e[i].id == id);
    }
    return n;
}

static const sa1_event *gfind(const glist *G, uint32_t from, uint32_t type)
{
    uint32_t i;
    for (i = from; i < G->n; i++) {
        if (G->e[i].type == type) {
            return &G->e[i];
        }
    }
    return NULL;
}

static const sa1_hostobj_event *hfind(const evlist *L, uint32_t from, uint32_t type, uint32_t object)
{
    uint32_t i;
    for (i = from; i < L->n; i++) {
        if (L->e[i].type == type && (object == 0 || L->e[i].object == object)) {
            return &L->e[i];
        }
    }
    return NULL;
}

static uint32_t hcount(const evlist *L, uint32_t from, uint32_t type, uint32_t object)
{
    uint32_t i, n = 0;
    for (i = from; i < L->n; i++) {
        n += L->e[i].type == type && (object == 0 || L->e[i].object == object);
    }
    return n;
}

/* player 0's guest state */
typedef struct pst {
    uint32_t twp, mwp, pwp, cwp;
    int32_t mode, action;
    float frame;
    uint32_t tflag, wtimer, item, htp, ang_y, chr;
    float pos[3], spd[3];
} pst;

static void pstate(const sa1g_world *w, pst *p)
{
    sa1g_player_ptrs pp;
    memset(p, 0, sizeof *p);
    sa1g_world_player(w, &pp);
    p->twp = pp.twp;
    p->mwp = pp.mwp;
    p->pwp = pp.pwp;
    if (pp.twp != 0) {
        uint32_t k;
        p->mode = (int8_t)wrd8(w, pp.twp);
        p->tflag = wrd16(w, pp.twp + 4u);
        p->wtimer = wrd16(w, pp.twp + 6u);
        p->chr = wrd8(w, pp.twp + 9u);
        p->ang_y = wrd32(w, pp.twp + 0x18u) & 0xFFFFu;
        p->cwp = wrd32(w, pp.twp + 0x38u);
        for (k = 0; k < 3; k++) {
            p->pos[k] = wrdf(w, pp.twp + 0x20u + 4u * k);
        }
    }
    if (pp.mwp != 0) {
        uint32_t k;
        for (k = 0; k < 3; k++) {
            p->spd[k] = wrdf(w, pp.mwp + 4u + 4u * k);
        }
    }
    if (pp.pwp != 0) {
        p->item = wrd16(w, pp.pwp + 6u);
        p->htp = wrd32(w, pp.pwp + 0x68u);
        p->action = (int16_t)wrd16(w, pp.pwp + 0x124u);
        p->frame = wrdf(w, pp.pwp + 0x130u);
    }
}

/* a HOLDABLE cylinder: the reference point on the floor at (x, y, z), body radius r and half height h (HU), its centre h
 * above the reference point */
static void hold_desc(sa1_hostobj_desc *d, float x, float y, float z, float r, float h)
{
    enemy_desc(d, SA1_HOSTOBJ_HOLDABLE, x, y, z, r);
    d->shapes[0].form = SA1_SHAPE_CYLINDER;
    d->shapes[0].size[0] = r;
    d->shapes[0].size[1] = h;
    d->shapes[0].center[2] = h;
}

static void move_obj(sa1_context *ctx, uint32_t id, float x, float y, float z)
{
    float p[3] = { x, y, z };
    sa1_hostobj_move(ctx, id, p, 0.0f);
}

/* ticks until player 0's wtimer is 0 (at most n pairs) */
static void wait_vulnerable(sa1_context *ctx, sa1_character_id id, sa1g_world *w, uint32_t n)
{
    pst p;
    uint32_t i;
    for (i = 0; i < n; i++) {
        pstate(w, &p);
        if (p.wtimer == 0 && (p.tflag & 3u)) {
            break;
        }
        tick(ctx, id, 0.0f, 0.0f, 0);
    }
}

static uint32_t hurt_flags(const sa1_event *e)
{
    return e != NULL ? e->arg >> 16 : 0u;
}

static void settle(sa1_context *ctx, sa1_character_id id, uint32_t pairs)
{
    uint32_t i;
    for (i = 0; i < pairs; i++) {
        tick(ctx, id, 0.0f, 0.0f, 0);
    }
}

/* ---- damage: touch (4.2 case 2) and the options --------------------------------------------------------------------- */
static void t_damage(sa1_context *ctx)
{
    sa1_character_id id;
    sa1g_world *w;
    sa1_hostobj_desc d;
    sa1_state_ex s;
    evlist *L = (evlist *)calloc(1, sizeof *L);
    glist *G = (glist *)calloc(1, sizeof *G);
    uint32_t obj = 0, i, g0, rings_tasks = 0, hurt_step = 0, back = 0, se_ring = 0;
    const sa1_event *h;
    pst p;
    printf("\n== damage (4.2 case 2: touching an enemy; the options NO_DEATH, KEEP_RINGS, NO_DAMAGE)\n");
    id = make_char(ctx, SA1_CHAR_SONIC, 0.0f, 0.0f, 8.0f, -1, 1);
    w = sa1_character_world(ctx, id);
    if (id == 0 || w == NULL || L == NULL || G == NULL) {
        CHECK(0, "Sonic");
        free(L);
        free(G);
        return;
    }
    settle(ctx, id, 20);
    gpoll(ctx, G);
    sa1_character_add_rings(ctx, id, 5);
    state_of(ctx, id, &s);
    enemy_desc(&d, SA1_HOSTOBJ_ENEMY, s.position[0] + 120.0f, s.position[1], 18.0f, 18.0f);
    d.user = 2;
    CHECK(sa1_hostobj_add(ctx, &d, &obj) == SA1_OK && s.rings == 5, "rings 5, an ENEMY (touch 1) 30 units ahead");
    g0 = G->n;
    /* a: walk into it */
    for (i = 0; i < 90; i++) {
        tick(ctx, id, 0.0f, 1.0f, 0);
        gpoll(ctx, G);
        poll_ho(ctx, L);
        if (g_verbose) {
            pstate(w, &p);
            printf("        walk pair %u: x %.1f mode %d flag 0x%X wtimer %u cwp flag 0x%X hit 0x%X\n", (unsigned)i,
                   (double)p.pos[0], (int)p.mode, (unsigned)p.tflag, (unsigned)p.wtimer, (unsigned)wrd16(w, p.cwp + 2u),
                   (unsigned)wrd32(w, p.cwp + 0x18u));
        }
        if (gfind(G, g0, SA1_EVENT_HURT) != NULL) {
            break;
        }
    }
    h = gfind(G, g0, SA1_EVENT_HURT);
    pstate(w, &p);
    rings_tasks = count_tasks(w, EXEC_RING_SCATTER, NULL);
    CHECK(h != NULL && h->id == obj && (h->arg & 0xFFFFu) == 5 && hurt_flags(h) == 0 && h->bank == 0,
          "HURT: source = the object (%u), rings lost %u, flags 0x%X, rings left %u", h ? (unsigned)h->id : 0u,
          h ? (unsigned)(h->arg & 0xFFFFu) : 0u, (unsigned)hurt_flags(h), h ? (unsigned)h->bank : 0u);
    if (h != NULL) {
        const rec *r = rec_at(h->step);
        hurt_step = h->step;
        CHECK(r != NULL && r->wtimer == 120 && r->mode == 16, "at the step of the hit: wtimer %u, mode %d (16: knock-back)",
              r ? (unsigned)r->wtimer : 0u, r ? (int)r->mode : -1);
    }
    CHECK(rings_tasks == 5, "5 scattered ring tasks (8C0870EA) right after the hit (%u)", (unsigned)rings_tasks);
    CHECK(hcount(L, 0, SA1_HOSTOBJ_EVENT_HIT, 0) == 0, "no HIT on the enemy (Sonic's body has no attack level)");
    /* b: inside the 120 steps the enemy sits on Sonic: nothing */
    pstate(w, &p);
    sa1_hostobj_remove(ctx, obj, SA1_HOSTOBJ_REMOVE_SILENT);
    tick(ctx, id, 0.0f, 0.0f, 0);
    enemy_desc(&d, SA1_HOSTOBJ_ENEMY, 0, 0, 0, 18.0f);
    state_of(ctx, id, &s);
    memcpy(d.position, s.position, sizeof d.position);
    d.position[2] += 18.0f;
    d.flags = SA1_HOSTOBJ_NOT_SOLID;
    sa1_hostobj_add(ctx, &d, &obj);
    g0 = G->n;
    for (i = 0; i < 20; i++) {
        tick(ctx, id, 0.0f, 0.0f, 0);
        state_of(ctx, id, &s);
        move_obj(ctx, obj, s.position[0], s.position[1], s.position[2] + 18.0f);
        gpoll(ctx, G);
    }
    pstate(w, &p);
    CHECK(gcount(G, g0, SA1_EVENT_HURT, 0xFFFFFFFFu) == 0 && p.wtimer > 0,
          "a second touch inside the invulnerability does nothing (wtimer %u)", (unsigned)p.wtimer);
    /* the rings come back: walk around the spot for a while */
    sa1_hostobj_remove(ctx, obj, SA1_HOSTOBJ_REMOVE_SILENT);
    g0 = G->n;
    for (i = 0; i < 120; i++) {
        float sy = (i / 20u) % 2u ? -1.0f : 1.0f;
        tick(ctx, id, 0.0f, i < 60 ? sy : 0.0f, 0);
        gpoll(ctx, G);
    }
    state_of(ctx, id, &s);
    back = (uint32_t)s.rings;
    se_ring = gcount(G, g0, SA1_EVENT_SOUND, 0xFFFFFFFFu);
    printf("        walking around the spot: %u of the 5 rings collected again (%u sound events); hit at step %u\n",
           (unsigned)back, (unsigned)se_ring, (unsigned)hurt_step);
    CHECK(back >= 1, "the scattered rings are collectable again (%u)", (unsigned)back);
    wait_vulnerable(ctx, id, w, 200);
    /* c: 0 rings + NO_DEATH: absorbed */
    sa1_character_add_rings(ctx, id, -999);
    CHECK(sa1_character_set_option(ctx, id, SA1_COPT_NO_DEATH, 1) == SA1_OK, "set NO_DEATH");
    state_of(ctx, id, &s);
    enemy_desc(&d, SA1_HOSTOBJ_ENEMY, s.position[0], s.position[1], s.position[2] + 18.0f, 18.0f);
    d.flags = SA1_HOSTOBJ_NOT_SOLID;
    sa1_hostobj_add(ctx, &d, &obj);
    g0 = G->n;
    for (i = 0; i < 10 && gfind(G, g0, SA1_EVENT_HURT) == NULL; i++) {
        tick(ctx, id, 0.0f, 0.0f, 0);
        gpoll(ctx, G);
    }
    sa1_hostobj_remove(ctx, obj, SA1_HOSTOBJ_REMOVE_SILENT);
    settle(ctx, id, 3);
    gpoll(ctx, G);
    h = gfind(G, g0, SA1_EVENT_HURT);
    pstate(w, &p);
    CHECK(h != NULL && (hurt_flags(h) & SA1_HURT_ABSORBED) && !(hurt_flags(h) & SA1_HURT_FATAL) &&
              gcount(G, g0, SA1_EVENT_DEATH, 0xFFFFFFFFu) == 0 && !(p.item & 0x4000u) && !(p.item & 3u),
          "0 rings, NO_DEATH: HURT ABSORBED (flags 0x%X), no death, no barrier left (item 0x%X)", (unsigned)hurt_flags(h),
          (unsigned)p.item);
    sa1_character_set_option(ctx, id, SA1_COPT_NO_DEATH, 0);
    wait_vulnerable(ctx, id, w, 200);
    /* d: KEEP_RINGS */
    sa1_character_add_rings(ctx, id, 7);
    sa1_character_set_option(ctx, id, SA1_COPT_KEEP_RINGS, 1);
    state_of(ctx, id, &s);
    enemy_desc(&d, SA1_HOSTOBJ_ENEMY, s.position[0], s.position[1], s.position[2] + 18.0f, 18.0f);
    d.flags = SA1_HOSTOBJ_NOT_SOLID;
    sa1_hostobj_add(ctx, &d, &obj);
    g0 = G->n;
    for (i = 0; i < 10 && gfind(G, g0, SA1_EVENT_HURT) == NULL; i++) {
        tick(ctx, id, 0.0f, 0.0f, 0);
        gpoll(ctx, G);
    }
    sa1_hostobj_remove(ctx, obj, SA1_HOSTOBJ_REMOVE_SILENT);
    settle(ctx, id, 3);
    gpoll(ctx, G);
    h = gfind(G, g0, SA1_EVENT_HURT);
    state_of(ctx, id, &s);
    CHECK(h != NULL && (hurt_flags(h) & SA1_HURT_ABSORBED) && (h->arg & 0xFFFFu) == 0 && s.rings == 7 &&
              count_tasks(w, EXEC_RING_SCATTER, NULL) == 0 && s.invulnerable_steps > 0,
          "7 rings, KEEP_RINGS: HURT ABSORBED, rings lost %u, rings %d, knocked back and invulnerable (%u)",
          h ? (unsigned)(h->arg & 0xFFFFu) : 0u, (int)s.rings, (unsigned)s.invulnerable_steps);
    sa1_character_set_option(ctx, id, SA1_COPT_KEEP_RINGS, 0);
    wait_vulnerable(ctx, id, w, 200);
    /* e: NO_DAMAGE */
    sa1_character_set_option(ctx, id, SA1_COPT_NO_DAMAGE, 1);
    state_of(ctx, id, &s);
    enemy_desc(&d, SA1_HOSTOBJ_ENEMY, s.position[0], s.position[1], s.position[2] + 18.0f, 18.0f);
    d.flags = SA1_HOSTOBJ_NOT_SOLID;
    sa1_hostobj_add(ctx, &d, &obj);
    g0 = G->n;
    {
        uint32_t maxw = 0;
        for (i = 0; i < 30; i++) {
            tick(ctx, id, 0.0f, 0.0f, 0);
            state_of(ctx, id, &s);
            move_obj(ctx, obj, s.position[0], s.position[1], s.position[2] + 18.0f);
            maxw = s.invulnerable_steps > maxw ? s.invulnerable_steps : maxw;
            gpoll(ctx, G);
        }
        CHECK(gcount(G, g0, SA1_EVENT_HURT, 0xFFFFFFFFu) == 0 && maxw <= 1 && s.rings == 7,
              "NO_DAMAGE: 30 pairs on an enemy: no HURT, wtimer at most %u (no blink), rings %d", (unsigned)maxw,
              (int)s.rings);
    }
    sa1_hostobj_remove(ctx, obj, SA1_HOSTOBJ_REMOVE_SILENT);
    settle(ctx, id, 2);     /* the removal takes effect at the next pair; the hit of its last pass is still pending then */
    sa1_character_set_option(ctx, id, SA1_COPT_NO_DAMAGE, 0);
    settle(ctx, id, 2);
    wait_vulnerable(ctx, id, w, 200);
    /* f: 0 rings, no option: FATAL and DEATH */
    sa1_character_add_rings(ctx, id, -999);
    state_of(ctx, id, &s);
    enemy_desc(&d, SA1_HOSTOBJ_ENEMY, s.position[0], s.position[1], s.position[2] + 18.0f, 18.0f);
    d.flags = SA1_HOSTOBJ_NOT_SOLID;
    sa1_hostobj_add(ctx, &d, &obj);
    g0 = G->n;
    for (i = 0; i < 10 && gfind(G, g0, SA1_EVENT_HURT) == NULL; i++) {
        tick(ctx, id, 0.0f, 0.0f, 0);
        gpoll(ctx, G);
        if (g_verbose) {
            pstate(w, &p);
            printf("        f pair %u: mode %d flag 0x%X wtimer %u item 0x%X cwp flag 0x%X hit 0x%X proxy 0x%X\n",
                   (unsigned)i, (int)p.mode, (unsigned)p.tflag, (unsigned)p.wtimer, (unsigned)p.item,
                   (unsigned)wrd16(w, p.cwp + 2u), (unsigned)wrd32(w, p.cwp + 0x18u),
                   (unsigned)sa1ho_world_proxy_twp(w, obj));
        }
    }
    h = gfind(G, g0, SA1_EVENT_HURT);
    {
        const sa1_event *dth = gfind(G, g0, SA1_EVENT_DEATH);
        pstate(w, &p);
        CHECK(h != NULL && (hurt_flags(h) & SA1_HURT_FATAL) && h->id == obj && dth != NULL &&
                  dth->arg == SA1_DEATH_HURT && dth->id == obj && dth->step == h->step && (p.item & 0x4000u),
              "0 rings: HURT FATAL (flags 0x%X) and SA1_EVENT_DEATH (cause %u, object %s) in the same step; item 0x%X",
              (unsigned)hurt_flags(h), dth ? (unsigned)dth->arg : 99u, dth && dth->id == obj ? "credited" : "-",
              (unsigned)p.item);
    }
    sa1_hostobj_remove(ctx, obj, SA1_HOSTOBJ_REMOVE_SILENT);
    settle(ctx, id, 2);
    gpoll(ctx, G);
    sa1_character_destroy(ctx, id);
    /* g: the HURT surface (COL 0x10000): HURT with SA1_HURT_SURFACE, no object */
    id = make_char(ctx, SA1_CHAR_SONIC, HURT_X0 + 128.0f, HURT_X0 + 128.0f, 40.0f, 3, 1);
    g0 = G->n;
    for (i = 0; i < 40 && gfind(G, g0, SA1_EVENT_HURT) == NULL; i++) {
        tick(ctx, id, 0.0f, 0.0f, 0);
        gpoll(ctx, G);
    }
    h = gfind(G, g0, SA1_EVENT_HURT);
    CHECK(h != NULL && (hurt_flags(h) & SA1_HURT_SURFACE) && h->id == 0 && (h->arg & 0xFFFFu) == 3,
          "landing on a HURT surface: HURT SURFACE (flags 0x%X), no object, rings lost %u", (unsigned)hurt_flags(h),
          h ? (unsigned)(h->arg & 0xFFFFu) : 0u);
    settle(ctx, id, 2);
    gpoll(ctx, G);
    sa1_character_destroy(ctx, id);
    free(L);
    free(G);
}

/* ---- host damage (4.2 case 7) ------------------------------------------------------------------------------------- */
static void t_hostdmg(sa1_context *ctx)
{
    sa1_character_id id;
    sa1g_world *w;
    sa1_damage dm;
    sa1_state_ex s0, s;
    glist *G = (glist *)calloc(1, sizeof *G);
    uint32_t i, g0;
    const sa1_event *h;
    pst p;
    printf("\n== host damage (4.2 case 7: sa1_character_damage, FACE_SOURCE, IGNORED, KILL)\n");
    id = make_char(ctx, SA1_CHAR_SONIC, 0.0f, 0.0f, 8.0f, 10, 1);
    w = sa1_character_world(ctx, id);
    if (id == 0 || w == NULL || G == NULL) {
        CHECK(0, "Sonic");
        free(G);
        return;
    }
    settle(ctx, id, 20);
    gpoll(ctx, G);
    state_of(ctx, id, &s0);
    memset(&dm, 0, sizeof dm);
    dm.struct_size = sizeof dm;
    dm.flags = SA1_DAMAGE_FACE_SOURCE;
    dm.source[0] = s0.position[0] - 200.0f;            /* behind him (he faces +x) */
    dm.source[1] = s0.position[1];
    dm.source[2] = s0.position[2];
    dm.object = 777;
    CHECK(sa1_character_damage(ctx, id, &dm) == SA1_OK, "a host damage request from behind (FACE_SOURCE, object 777)");
    g0 = G->n;
    for (i = 0; i < 2; i++) {
        tick(ctx, id, 0.0f, 0.0f, 0);
        gpoll(ctx, G);
    }
    h = gfind(G, g0, SA1_EVENT_HURT);
    pstate(w, &p);
    CHECK(h != NULL && h->id == 777 && (hurt_flags(h) & SA1_HURT_HOST) && (h->arg & 0xFFFFu) == 10,
          "HURT HOST, credited to 777, rings lost %u (flags 0x%X)", h ? (unsigned)(h->arg & 0xFFFFu) : 0u,
          (unsigned)hurt_flags(h));
    CHECK(((p.ang_y + 0x800u) & 0xFFFFu) < 0x1000u + 0x8000u && ((p.ang_y + 0x800u) & 0xFFFFu) >= 0x8000u,
          "he turned to the source: ang.y 0x%04X (0x8000 = facing -x)", (unsigned)p.ang_y);
    for (i = 0; i < 10; i++) {
        tick(ctx, id, 0.0f, 0.0f, 0);
    }
    state_of(ctx, id, &s);
    CHECK(s.position[0] > s0.position[0] + 8.0f, "knocked back away from the source: x %.1f -> %.1f HU",
          (double)s0.position[0], (double)s.position[0]);
    gpoll(ctx, G);
    g0 = G->n;
    CHECK(sa1_character_damage(ctx, id, &dm) == SA1_OK, "a second request during the invulnerability");
    tick(ctx, id, 0.0f, 0.0f, 0);
    gpoll(ctx, G);
    h = gfind(G, g0, SA1_EVENT_HURT);
    CHECK(h != NULL && (hurt_flags(h) & SA1_HURT_IGNORED) && (hurt_flags(h) & SA1_HURT_HOST) && (h->arg & 0xFFFFu) == 0,
          "HURT IGNORED | HOST (flags 0x%X)", (unsigned)hurt_flags(h));
    for (i = 0; i < 6; i++) {
        CHECK(i < 4 ? sa1_character_damage(ctx, id, &dm) == SA1_OK : sa1_character_damage(ctx, id, &dm) == SA1_ERR_LIMIT,
              "request %u: %s", (unsigned)i, i < 4 ? "queued" : "SA1_ERR_LIMIT (4 pending)");
    }
    tick(ctx, id, 0.0f, 0.0f, 0);
    wait_vulnerable(ctx, id, w, 200);
    gpoll(ctx, G);
    g0 = G->n;
    dm.flags = SA1_DAMAGE_KILL;
    dm.object = 778;
    sa1_character_damage(ctx, id, &dm);
    for (i = 0; i < 2; i++) {
        tick(ctx, id, 0.0f, 0.0f, 0);
        gpoll(ctx, G);
    }
    {
        const sa1_event *dth = gfind(G, g0, SA1_EVENT_DEATH);
        pstate(w, &p);
        CHECK(dth != NULL && dth->arg == SA1_DEATH_HOST && dth->id == 778 && (p.item & 0x4000u) &&
                  gcount(G, g0, SA1_EVENT_HURT, 0xFFFFFFFFu) == 0,
              "KILL: SA1_EVENT_DEATH (cause %u, object %u), the game's death request (item 0x%X), no HURT",
              dth ? (unsigned)dth->arg : 99u, dth ? (unsigned)dth->id : 0u, (unsigned)p.item);
    }
    {
        sa1_damage bad = dm;
        bad.flags = 0x80;
        CHECK(sa1_character_damage(ctx, id, &bad) == SA1_ERR_INVALID_ARGUMENT, "unknown damage flags refused");
        CHECK(sa1_character_damage(ctx, 12345, &dm) == SA1_ERR_NOT_FOUND, "an unknown character: NOT_FOUND");
        CHECK(sa1_character_set_option(ctx, id, 9, 1) == SA1_ERR_INVALID_ARGUMENT, "an unknown option refused");
    }
    settle(ctx, id, 2);
    gpoll(ctx, G);
    sa1_character_destroy(ctx, id);
    free(G);
}

/* ---- host damage before the first step (round 0.9.2, faults F1) ----------------------------------------------------- */
static sa1g_step_fn g_early_pre;    /* the world's own hostobj_pre */
static int g_early_hide;            /* steps whose pre hook runs with playerpwp[0] hidden */

static void early_pre(sa1g_world *w, int32_t sub, void *user)
{
    uint32_t pwp = 0;
    if (g_early_hide > 0) {
        g_early_hide--;
        sa1g_world_rd32(w, SA1G_ADDR_PLAYER_PWP, &pwp);
        sa1g_world_wr32(w, SA1G_ADDR_PLAYER_PWP, 0);
        g_early_pre(w, sub, user);
        sa1g_world_wr32(w, SA1G_ADDR_PLAYER_PWP, pwp);     /* the executor sees the game's own state */
        return;
    }
    g_early_pre(w, sub, user);
}

static void t_early(sa1_context *ctx)
{
    static const int32_t kinds[6] = { SA1_CHAR_SONIC, SA1_CHAR_TAILS, SA1_CHAR_KNUCKLES, SA1_CHAR_AMY, SA1_CHAR_GAMMA,
                                      SA1_CHAR_BIG };
    static const uint32_t how[3] = { 0u, SA1_DAMAGE_FACE_SOURCE, SA1_DAMAGE_KILL };
    glist *G = (glist *)calloc(1, sizeof *G);
    uint32_t k, h, i;
    printf("\n== early: host damage before the character's first step (round 0.9.2, faults F1)\n");
    if (G == NULL) {
        CHECK(0, "memory");
        return;
    }
    for (k = 0; k < 6; k++) {
        for (h = 0; h < 3; h++) {
            const char *nm = sa1_character_name(kinds[k]);
            sa1_character_id id = make_char(ctx, kinds[k], 0.0f, 0.0f, 8.0f, 5, 0);
            sa1g_world *w = sa1_character_world(ctx, id);
            sa1_damage dm;
            sa1_status sd, st = SA1_OK;
            const sa1_event *e;
            uint32_t g0, hurts, deaths, wt = 0;
            pst p;
            if (id == 0 || w == NULL) {
                CHECK(0, "%s", nm);
                continue;
            }
            gpoll(ctx, G);
            g0 = G->n;
            memset(&dm, 0, sizeof dm);
            dm.struct_size = sizeof dm;
            dm.flags = how[h];
            dm.source[0] = -160.0f;                     /* 40 units behind the start (host space: HU) */
            dm.source[2] = 8.0f;
            dm.object = 900u + 10u * k + h;
            sd = sa1_character_damage(ctx, id, &dm);
            for (i = 0; i < 4 && st == SA1_OK; i++) {
                st = tick(ctx, id, 0.0f, 0.0f, 0);
                if (i == 0) {
                    pstate(w, &p);
                    wt = p.wtimer;
                }
            }
            gpoll(ctx, G);
            hurts = gcount(G, g0, SA1_EVENT_HURT, 0xFFFFFFFFu);
            deaths = gcount(G, g0, SA1_EVENT_DEATH, 0xFFFFFFFFu);
            if (how[h] & SA1_DAMAGE_KILL) {
                e = gfind(G, g0, SA1_EVENT_DEATH);
                CHECK(sd == SA1_OK && st == SA1_OK && deaths == 1 && e != NULL && e->arg == SA1_DEATH_HOST &&
                          e->id == dm.object && hurts == 0,
                      "%-8s KILL before the first tick: request %s, 4 pairs %s, DEATH %u (cause %u, object %u), HURT %u", nm,
                      sa1_status_string(sd), sa1_status_string(st), (unsigned)deaths, e ? (unsigned)e->arg : 99u,
                      e ? (unsigned)e->id : 0u, (unsigned)hurts);
            } else {
                e = gfind(G, g0, SA1_EVENT_HURT);
                CHECK(sd == SA1_OK && st == SA1_OK && hurts == 1 && e != NULL && (hurt_flags(e) & SA1_HURT_HOST) &&
                          !(hurt_flags(e) & SA1_HURT_IGNORED) && e->id == dm.object && e->step == 1u && wt >= 118u,
                      "%-8s hit (flags %u) before the first tick: request %s, 4 pairs %s, HURT %u (flags 0x%X, step %u), "
                      "wtimer %u after the first pair", nm, (unsigned)how[h], sa1_status_string(sd),
                      sa1_status_string(st), (unsigned)hurts, (unsigned)hurt_flags(e), e ? (unsigned)e->step : 0u,
                      (unsigned)wt);
            }
            if (st != SA1_OK) {
                printf("        %s\n", sa1_last_error(ctx));
            }
            settle(ctx, id, 1);
            gpoll(ctx, G);
            sa1_character_destroy(ctx, id);
        }
    }
    /* a request while the playerwk is missing outside mode 0: dropped (Review R.3.5), the queue free again */
    {
        sa1_character_id id = make_char(ctx, SA1_CHAR_SONIC, 0.0f, 0.0f, 8.0f, 5, 0);
        sa1g_world *w = sa1_character_world(ctx, id);
        sa1_damage dm;
        sa1_status st = SA1_OK;
        uint32_t g0, hurts_drop, hurts_next;
        pst p;
        if (id == 0 || w == NULL) {
            CHECK(0, "Sonic");
            free(G);
            return;
        }
        settle(ctx, id, 10);
        pstate(w, &p);
        g_early_pre = w->hostobj_pre;
        w->hostobj_pre = early_pre;
        memset(&dm, 0, sizeof dm);
        dm.struct_size = sizeof dm;
        dm.object = 990;
        gpoll(ctx, G);
        g0 = G->n;
        sa1_character_damage(ctx, id, &dm);
        g_early_hide = 1;                       /* the next step's pre hook finds no playerwk (mode p.mode != 0) */
        for (i = 0; i < 4 && st == SA1_OK; i++) {
            st = tick(ctx, id, 0.0f, 0.0f, 0);
        }
        gpoll(ctx, G);
        hurts_drop = gcount(G, g0, SA1_EVENT_HURT, 0xFFFFFFFFu);
        g0 = G->n;
        dm.object = 991;
        sa1_character_damage(ctx, id, &dm);
        for (i = 0; i < 2 && st == SA1_OK; i++) {
            st = tick(ctx, id, 0.0f, 0.0f, 0);
        }
        gpoll(ctx, G);
        hurts_next = gcount(G, g0, SA1_EVENT_HURT, 991u);
        CHECK(p.mode != 0 && st == SA1_OK && hurts_drop == 0 && hurts_next == 1 && g_early_hide == 0,
              "Sonic in mode %d, playerwk hidden for one step: the request is dropped (HURT %u in 4 pairs); the next one "
              "hurts (HURT %u)", (int)p.mode, (unsigned)hurts_drop, (unsigned)hurts_next);
        w->hostobj_pre = g_early_pre;
        settle(ctx, id, 2);
        gpoll(ctx, G);
        sa1_character_destroy(ctx, id);
    }
    free(G);
}

/* ---- rolling through (4.2 case 3) ----------------------------------------------------------------------------------- */
/* Sonic charges a spin dash (the action button held while standing), releases it and rolls into the object 50 units
 * ahead; returns the hostobj / game events of the run in L / G */
static void spin_dash_into(sa1_context *ctx, sa1_character_id id, sa1g_world *w, evlist *L, glist *G, uint32_t pairs)
{
    uint32_t i;
    pst p;
    for (i = 0; i < 20; i++) {
        tick(ctx, id, 0.0f, 0.0f, SA1_BUTTON_ACTION);        /* charge */
        poll_ho(ctx, L);
        gpoll(ctx, G);
    }
    pstate(w, &p);
    if (g_verbose) {
        printf("        charging: mode %d action %d\n", (int)p.mode, (int)p.action);
    }
    for (i = 0; i < pairs; i++) {
        tick(ctx, id, 0.0f, 0.0f, 0);                        /* release: the dash */
        poll_ho(ctx, L);
        gpoll(ctx, G);
        if (g_verbose && i < 30) {
            pstate(w, &p);
            printf("        dash pair %u: mode %d action %d x %.1f flag 0x%X\n", (unsigned)i, (int)p.mode, (int)p.action,
                   (double)p.pos[0], (unsigned)p.tflag);
        }
    }
}

static void t_roll(sa1_context *ctx)
{
    sa1_character_id id;
    sa1g_world *w;
    sa1_hostobj_desc d;
    sa1_state_ex s;
    evlist *L = (evlist *)calloc(1, sizeof *L);
    glist *G = (glist *)calloc(1, sizeof *G);
    uint32_t obj = 0, i, g0, l0;
    const sa1_hostobj_event *h;
    printf("\n== rolling through (4.2 case 3: spin dash vs touch 1, a touch-2 HAZARD)\n");
    id = make_char(ctx, SA1_CHAR_SONIC, 0.0f, 0.0f, 8.0f, 20, 1);
    w = sa1_character_world(ctx, id);
    if (id == 0 || w == NULL || L == NULL || G == NULL) {
        CHECK(0, "Sonic");
        free(L);
        free(G);
        return;
    }
    settle(ctx, id, 20);
    gpoll(ctx, G);
    state_of(ctx, id, &s);
    enemy_desc(&d, SA1_HOSTOBJ_ENEMY, s.position[0] + 200.0f, s.position[1], 18.0f, 18.0f);
    sa1_hostobj_add(ctx, &d, &obj);
    g0 = G->n;
    spin_dash_into(ctx, id, w, L, G, 40);
    h = hfind(L, 0, SA1_HOSTOBJ_EVENT_HIT, obj);
    CHECK(h != NULL && h->attack == SA1_ATTACK_SPIN_DASH && h->level == 1,
          "spin dash into a touch-1 ENEMY: HIT attack %u (SPIN_DASH %u), level %u", h ? (unsigned)h->attack : 99u,
          (unsigned)SA1_ATTACK_SPIN_DASH, h ? (unsigned)h->level : 0u);
    CHECK(gcount(G, g0, SA1_EVENT_HURT, 0xFFFFFFFFu) == 0, "no HURT");
    sa1_hostobj_remove(ctx, obj, SA1_HOSTOBJ_REMOVE_KILLED);
    settle(ctx, id, 30);
    /* a touch-2 HAZARD 20 units above him: jumping into it (the ball, defence 1) hurts */
    state_of(ctx, id, &s);
    enemy_desc(&d, SA1_HOSTOBJ_HAZARD, s.position[0], s.position[1], s.position[2] + 90.0f, 20.0f);
    sa1_hostobj_add(ctx, &d, &obj);
    settle(ctx, id, 2);
    gpoll(ctx, G);
    g0 = G->n;
    l0 = L->n;
    for (i = 0; i < 40 && gfind(G, g0, SA1_EVENT_HURT) == NULL; i++) {
        tick(ctx, id, 0.0f, 0.0f, i < 12 ? SA1_BUTTON_JUMP : 0u);
        gpoll(ctx, G);
        poll_ho(ctx, L);
    }
    {
        const sa1_event *e = gfind(G, g0, SA1_EVENT_HURT);
        CHECK(e != NULL && e->id == obj, "a jumping Sonic (ball, defence 1) is hurt by a touch-2 HAZARD (HURT from it: %s)",
              e ? "yes" : "no");
        CHECK(hcount(L, l0, SA1_HOSTOBJ_EVENT_HIT, obj) == 0, "a HAZARD is not damageable: no HIT");
    }
    sa1_hostobj_remove(ctx, obj, SA1_HOSTOBJ_REMOVE_SILENT);
    wait_vulnerable(ctx, id, w, 200);
    settle(ctx, id, 20);
    /* the spin dash is a ball too (mode 5: attack 1, defence 1): the touch-2 HAZARD hurts it as well */
    state_of(ctx, id, &s);
    enemy_desc(&d, SA1_HOSTOBJ_HAZARD, s.position[0] + 200.0f, s.position[1], 18.0f, 18.0f);
    sa1_hostobj_add(ctx, &d, &obj);
    gpoll(ctx, G);
    g0 = G->n;
    spin_dash_into(ctx, id, w, L, G, 40);
    {
        const sa1_event *e = gfind(G, g0, SA1_EVENT_HURT);
        CHECK(e != NULL && e->id == obj, "a spin-dashing Sonic (the roll, mode 5: defence 1) is hurt by it too (%s)",
              e ? "HURT" : "no HURT");
    }
    sa1_hostobj_remove(ctx, obj, SA1_HOSTOBJ_REMOVE_SILENT);
    settle(ctx, id, 2);
    gpoll(ctx, G);
    sa1_character_destroy(ctx, id);
    /* (only defence 3 protects from touch 2: the light speed dash (mode 6), the Light Speed Attack (44), invincibility) */
    sa1_character_destroy(ctx, id);
    free(L);
    free(G);
}

/* ---- armor (4.2 case 4) ------------------------------------------------------------------------------------------- */
static void t_armor(sa1_context *ctx)
{
    sa1_character_id id;
    sa1g_world *w;
    sa1_hostobj_desc d;
    sa1_state_ex s;
    evlist *L = (evlist *)calloc(1, sizeof *L);
    glist *G = (glist *)calloc(1, sizeof *G);
    uint32_t obj = 0, i, pressed = 0, l0, g0;
    const sa1_hostobj_event *h;
    printf("\n== armor (4.2 case 4: an armor-1 ENEMY survives a jump; Knuckles' punch out-levels it)\n");
    id = make_char(ctx, SA1_CHAR_SONIC, 0.0f, 0.0f, 8.0f, 10, 1);
    w = sa1_character_world(ctx, id);
    if (id == 0 || w == NULL || L == NULL || G == NULL) {
        CHECK(0, "Sonic");
        free(L);
        free(G);
        return;
    }
    settle(ctx, id, 20);
    state_of(ctx, id, &s);
    enemy_desc(&d, SA1_HOSTOBJ_ENEMY, s.position[0] + 120.0f, s.position[1], 18.0f, 18.0f);
    d.armor = 1;
    sa1_hostobj_add(ctx, &d, &obj);
    gpoll(ctx, G);
    g0 = G->n;
    for (i = 0; i < 60; i++) {
        uint32_t b = 0;
        state_of(ctx, id, &s);
        if (!pressed && s.position[0] >= d.position[0] - g_jump_d) {
            b = SA1_BUTTON_JUMP;
            pressed = i + 1;
        }
        tick(ctx, id, 0.0f, i < 40 ? 1.0f : 0.0f, b);
        poll_ho(ctx, L);
        gpoll(ctx, G);
    }
    CHECK(pressed && hcount(L, 0, SA1_HOSTOBJ_EVENT_HIT, obj) == 0, "Sonic jumps onto it (attack 1 vs defence 1): no HIT");
    CHECK(gcount(G, g0, SA1_EVENT_HURT, 0xFFFFFFFFu) == 0, "and no HURT (its touch 1 vs the ball's defence 1)");
    sa1_hostobj_remove(ctx, obj, SA1_HOSTOBJ_REMOVE_SILENT);
    settle(ctx, id, 2);
    gpoll(ctx, G);
    sa1_character_destroy(ctx, id);
    /* Knuckles' punch (the action button on the ground: shape 1, attack 3) */
    id = make_char(ctx, SA1_CHAR_KNUCKLES, 0.0f, 0.0f, 8.0f, 10, 1);
    w = sa1_character_world(ctx, id);
    if (id == 0 || w == NULL) {
        CHECK(0, "Knuckles");
        free(L);
        free(G);
        return;
    }
    settle(ctx, id, 20);
    state_of(ctx, id, &s);
    enemy_desc(&d, SA1_HOSTOBJ_ENEMY, s.position[0] + 56.0f, s.position[1], 18.0f, 18.0f);
    d.armor = 1;
    d.touch = 0;
    sa1_hostobj_add(ctx, &d, &obj);
    gpoll(ctx, G);
    l0 = L->n;
    g0 = G->n;
    for (i = 0; i < 40 && hfind(L, l0, SA1_HOSTOBJ_EVENT_HIT, obj) == NULL; i++) {
        tick(ctx, id, 0.0f, 0.0f, (i % 6) < 2 ? SA1_BUTTON_ACTION : 0u);
        poll_ho(ctx, L);
        gpoll(ctx, G);
        if (g_verbose) {
            pst p;
            pstate(w, &p);
            printf("        punch pair %u: mode %d action %d frame %.1f\n", (unsigned)i, (int)p.mode, (int)p.action,
                   (double)p.frame);
        }
    }
    h = hfind(L, l0, SA1_HOSTOBJ_EVENT_HIT, obj);
    CHECK(h != NULL && h->attack == SA1_ATTACK_PUNCH && h->level == 3 && h->damage == 2,
          "Knuckles' punch: HIT attack %u (PUNCH %u), level %u, damage %u (3 - armor 1)", h ? (unsigned)h->attack : 99u,
          (unsigned)SA1_ATTACK_PUNCH, h ? (unsigned)h->level : 0u, h ? (unsigned)h->damage : 0u);
    CHECK(gcount(G, g0, SA1_EVENT_HURT, 0xFFFFFFFFu) == 0, "Knuckles unhurt");
    sa1_hostobj_remove(ctx, obj, SA1_HOSTOBJ_REMOVE_SILENT);
    settle(ctx, id, 2);
    gpoll(ctx, G);
    sa1_character_destroy(ctx, id);
    free(L);
    free(G);
}

/* ---- E-102 (4.2 case 6) ------------------------------------------------------------------------------------------- */
typedef struct e102_out {
    uint32_t locked, targeted, unlocked, hits, laser, projectile, faults;
    int32_t bonus_total;        /* the running bonus counter's total (COUNTWK +0x10), the largest seen */
    uint32_t bonus_tasks;
} e102_out;

static void e102_run(sa1_context *ctx, uint32_t kind, uint32_t flags, uint32_t how, e102_out *o)
{
    sa1_character_id id;
    sa1g_world *w;
    sa1_hostobj_desc d;
    sa1_state_ex s;
    evlist *L = (evlist *)calloc(1, sizeof *L);
    uint32_t obj[3] = { 0, 0, 0 }, i, k, bt;
    memset(o, 0, sizeof *o);
    id = make_char(ctx, SA1_CHAR_GAMMA, 0.0f, 0.0f, 8.0f, -1, 1);
    w = sa1_character_world(ctx, id);
    if (id == 0 || w == NULL || L == NULL) {
        free(L);
        return;
    }
    settle(ctx, id, 20);
    state_of(ctx, id, &s);
    for (k = 0; k < 3; k++) {
        enemy_desc(&d, kind, s.position[0] + 240.0f + 120.0f * (float)k, s.position[1] + 40.0f * (float)k - 40.0f,
                   40.0f, 18.0f);
        d.touch = 0;
        d.flags = flags | SA1_HOSTOBJ_NOT_SOLID;
        d.user = k;
        sa1_hostobj_add(ctx, &d, &obj[k]);
    }
    tick(ctx, id, 0.0f, 0.0f, 0);
    for (i = 0; i < 30; i++) {
        tick(ctx, id, 0.0f, 0.0f, SA1_BUTTON_ACTION);       /* hold the laser 60 steps */
        poll_ho(ctx, L);
    }
    for (i = 0; i < 150; i++) {
        tick(ctx, id, 0.0f, 0.0f, 0);                       /* release: the shots */
        poll_ho(ctx, L);
        if (hcount(L, 0, SA1_HOSTOBJ_EVENT_HIT, 0) >= 3 && i > 40) {
            break;
        }
    }
    /* the host's decision: all three killed (or removed silently) */
    for (k = 0; k < 3; k++) {
        sa1_hostobj_remove(ctx, obj[k], how);
    }
    for (i = 0; i < 60; i++) {
        tick(ctx, id, 0.0f, 0.0f, 0);
        poll_ho(ctx, L);
        bt = wrd32(w, ADDR_BONUS_TASK);
        if (bt != 0) {
            uint32_t awp = wrd32(w, bt + 0x2Cu);
            int32_t total = awp ? (int32_t)wrd32(w, awp + 0x10u) : 0;
            o->bonus_total = total > o->bonus_total ? total : o->bonus_total;
            o->bonus_tasks = 1;
        }
    }
    for (i = 0; i < L->n; i++) {
        const sa1_hostobj_event *e = &L->e[i];
        o->locked += e->type == SA1_HOSTOBJ_EVENT_LOCKED;
        o->targeted += e->type == SA1_HOSTOBJ_EVENT_TARGETED;
        o->unlocked += e->type == SA1_HOSTOBJ_EVENT_UNLOCKED;
        if (e->type == SA1_HOSTOBJ_EVENT_HIT) {
            o->hits++;
            o->laser += e->attack == SA1_ATTACK_LASER && e->level == 3;
            o->projectile += (e->flags & SA1_HITF_PROJECTILE) != 0;
            if (g_verbose) {
                printf("        HIT #%u step %u attack %u level %u flags 0x%X\n", (unsigned)e->user, (unsigned)e->step,
                       (unsigned)e->attack, (unsigned)e->level, (unsigned)e->flags);
            }
        }
    }
    o->faults = sa1g_world_current() != NULL || w->dead;
    settle(ctx, id, 2);
    poll_game(ctx, 0, 0, NULL);
    sa1_character_destroy(ctx, id);
    free(L);
}

static void t_e102(sa1_context *ctx)
{
    e102_out a, b;
    printf("\n== E-102 (4.2 case 6: lock-on, shots, the time bonus at REMOVE_KILLED)\n");
    e102_run(ctx, SA1_HOSTOBJ_ENEMY, 0, SA1_HOSTOBJ_REMOVE_KILLED, &a);
    CHECK(a.locked == 3 && a.targeted == 3, "three ENEMYs: LOCKED x%u, TARGETED x%u", (unsigned)a.locked,
          (unsigned)a.targeted);
    CHECK(a.hits == 3 && a.laser == 3 && a.projectile == 3, "three HITs, LASER level 3 x%u, PROJECTILE x%u",
          (unsigned)a.laser, (unsigned)a.projectile);
    CHECK(a.unlocked == 3, "the markers released at the hits: UNLOCKED x%u", (unsigned)a.unlocked);
    CHECK(a.bonus_tasks && a.bonus_total == 6 && !a.faults,
          "REMOVE_KILLED on all three: the shot group's time bonus counts 3 targets (total %d = 1 + 2 + 3), no fault",
          (int)a.bonus_total);
    e102_run(ctx, SA1_HOSTOBJ_TARGET, 0, SA1_HOSTOBJ_REMOVE_KILLED, &b);
    CHECK(b.locked == 3 && b.hits == 3 && b.bonus_total == 0,
          "three TARGETs (NO_BONUS): LOCKED x%u, HIT x%u, no time bonus (total %d)", (unsigned)b.locked, (unsigned)b.hits,
          (int)b.bonus_total);
    e102_run(ctx, SA1_HOSTOBJ_ENEMY, 0, SA1_HOSTOBJ_REMOVE_SILENT, &b);
    CHECK(b.hits == 3 && b.bonus_total == 0, "three ENEMYs hit, removed SILENT: no time bonus (total %d)",
          (int)b.bonus_total);
    e102_run(ctx, SA1_HOSTOBJ_ENEMY, SA1_HOSTOBJ_NO_LOCKON, SA1_HOSTOBJ_REMOVE_KILLED, &b);
    CHECK(b.locked == 0 && b.hits == 0, "three ENEMYs with NO_LOCKON: never locked (LOCKED x%u, HIT x%u)",
          (unsigned)b.locked, (unsigned)b.hits);
}

/* ---- two worlds (4.2 case 9) -------------------------------------------------------------------------------------- */
typedef struct two_out {
    uint32_t hits[2], crc[2], arena[2];
} two_out;

/* Sonic (0) and / or Knuckles (1) run and jump through three ENEMYs; the same objects for both */
static void two_run(sa1_context *ctx, int with0, int with1, two_out *o)
{
    sa1_character_id id[2] = { 0, 0 };
    sa1_hostobj_desc d;
    sa1_hostobj_event ev[512];
    uint32_t obj[3], i, k, n, c;
    memset(o, 0, sizeof *o);
    if (with0) {
        id[0] = make_char(ctx, SA1_CHAR_SONIC, 0.0f, 200.0f, 8.0f, -1, 0);
    }
    if (with1) {
        id[1] = make_char(ctx, SA1_CHAR_KNUCKLES, 0.0f, 200.0f, 8.0f, -1, 0);    /* its own world: the same path */
    }
    for (k = 0; k < 3; k++) {
        enemy_desc(&d, SA1_HOSTOBJ_ENEMY, 300.0f + 200.0f * (float)k, 200.0f, 18.0f, 18.0f);
        d.user = 10 + k;
        d.shapes[0].form = SA1_SHAPE_CYLINDER;
        d.shapes[0].size[0] = 18.0f;
        d.shapes[0].size[1] = 300.0f;      /* a tall cylinder: a jump anywhere near it hits it */
        d.touch = 0;
        d.flags = SA1_HOSTOBJ_NOT_SOLID;
        sa1_hostobj_add(ctx, &d, &obj[k]);
    }
    for (i = 0; i < 160; i++) {
        uint32_t b = (i % 16u) >= 4u && (i % 16u) < 10u ? SA1_BUTTON_JUMP : 0u;
        for (c = 0; c < 2; c++) {
            if (id[c] != 0) {
                tick(ctx, id[c], 0.0f, i < 140 ? 1.0f : 0.0f, b);
            }
        }
        n = 0;
        sa1_poll_hostobj_events(ctx, ev, 512, &n);
        for (k = 0; k < n; k++) {
            uint32_t who = ev[k].character == id[0] ? 0u : 1u, v[6];
            if (ev[k].type != SA1_HOSTOBJ_EVENT_HIT) {
                continue;
            }
            o->hits[who]++;
            v[0] = ev[k].type;
            v[1] = (uint32_t)ev[k].user;
            v[2] = ev[k].attack;
            v[3] = ev[k].flags;
            v[4] = ev[k].step;
            v[5] = ev[k].level;
            o->crc[who] = tu_crc32(o->crc[who], (const uint8_t *)v, sizeof v);
            o->crc[who] = tu_crc32(o->crc[who], (const uint8_t *)ev[k].position, sizeof ev[k].position);
        }
    }
    for (c = 0; c < 2; c++) {
        if (id[c] != 0) {
            o->arena[c] = arena_crc(sa1_character_world(ctx, id[c]));
        }
    }
    remove_all(ctx, obj, 3);
    for (c = 0; c < 2; c++) {
        if (id[c] != 0) {
            tick(ctx, id[c], 0.0f, 0.0f, 0);
            sa1_character_destroy(ctx, id[c]);
        }
    }
    poll_game(ctx, 0, 0, NULL);
}

static void t_worlds(sa1_context *ctx)
{
    two_out both, s0, s1;
    printf("\n== two worlds (4.2 case 9: Sonic and Knuckles, the same objects)\n");
    two_run(ctx, 1, 1, &both);
    two_run(ctx, 1, 0, &s0);
    two_run(ctx, 0, 1, &s1);
    printf("        HITs: Sonic %u, Knuckles %u (together); %u / %u alone\n", (unsigned)both.hits[0],
           (unsigned)both.hits[1], (unsigned)s0.hits[0], (unsigned)s1.hits[1]);
    CHECK(both.hits[0] > 0 && both.hits[1] > 0, "hits are reported per character");
    CHECK(both.crc[0] == s0.crc[0] && both.arena[0] == s0.arena[0], "Sonic's world equals a run of it alone (events %08X / "
          "%08X, arena %08X / %08X)", (unsigned)both.crc[0], (unsigned)s0.crc[0], (unsigned)both.arena[0],
          (unsigned)s0.arena[0]);
    CHECK(both.crc[1] == s1.crc[1] && both.arena[1] == s1.arena[1], "Knuckles' world equals a run of it alone (events %08X "
          "/ %08X, arena %08X / %08X)", (unsigned)both.crc[1], (unsigned)s1.crc[1], (unsigned)both.arena[1],
          (unsigned)s1.arena[1]);
}

/* ---- HOLDABLE (addendum 6.7 cases 11-15) -------------------------------------------------------------------------- */
#define SE_LIFT 0x45Au

/* the character presses the action button once and waits for LIFTED (at most n pairs); returns the pairs it took, 0 */
static uint32_t do_lift(sa1_context *ctx, sa1_character_id id, evlist *L, uint32_t obj, uint32_t n)
{
    uint32_t i, l0 = L->n;
    for (i = 0; i < n; i++) {
        tick(ctx, id, 0.0f, 0.0f, i == 0 ? SA1_BUTTON_ACTION : 0u);
        poll_ho(ctx, L);
        if (g_verbose && i < 6 && g_nrec >= 2) {
            uint32_t j;
            for (j = g_nrec - 2; j < g_nrec; j++) {
                printf("          step %u: mode %d flag 0x%X pos %.3f %.3f %.3f spd %.3f %.3f %.3f%s", (unsigned)g_rec[j].step,
                       (int)g_rec[j].mode, (unsigned)g_rec[j].tflag, (double)g_rec[j].pos[0], (double)g_rec[j].pos[1],
                       (double)g_rec[j].pos[2], (double)g_rec[j].spd[0], (double)g_rec[j].spd[1], (double)g_rec[j].spd[2],
                       "\n");
            }
        }
        if (g_verbose && i < 6) {
            sa1_state_ex x;
            state_of(ctx, id, &x);
            printf("          state: attr 0x%X last 0x%X normal %.3f %.3f %.3f land %u nocon %d pspd %.3f %.3f%s",
                   (unsigned)x.attr, (unsigned)x.last_attr, (double)x.floor_normal[0], (double)x.floor_normal[1],
                   (double)x.floor_normal[2], (unsigned)x.land_count, (int)x.nocon_timer, (double)x.player_speed[0],
                   (double)x.player_speed[1], "\n");
        }
        if (g_verbose && i < 24) {
            pst q;
            sa1g_world *w = sa1_character_world(ctx, id);
            pstate(w, &q);
            printf("        lift pair %u: mode %d action %d frame %.1f flag 0x%X cwp 0x%X hit 0x%X htp 0x%X pos %.2f %.2f "
                   "spd %.2f %.2f %.2f hitnum %d%s",
                   (unsigned)i, (int)q.mode, (int)q.action, (double)q.frame, (unsigned)q.tflag,
                   (unsigned)wrd16(w, q.cwp + 2u), (unsigned)wrd32(w, q.cwp + 0x18u), (unsigned)q.htp, (double)q.pos[0],
                   (double)q.pos[1], (double)q.spd[0], (double)q.spd[1], (double)q.spd[2],
                   (int)(int16_t)wrd16(w, q.cwp + 0x16u), "\n");
        }
        if (hfind(L, l0, SA1_HOSTOBJ_EVENT_LIFTED, obj) != NULL) {
            return i + 1;
        }
    }
    return 0;
}

/* runs `run` pairs forward, then presses the action button and waits for RELEASED (at most n pairs) */
static const sa1_hostobj_event *do_release(sa1_context *ctx, sa1_character_id id, evlist *L, uint32_t obj, uint32_t run,
                                           float stick, uint32_t n)
{
    uint32_t i, l0 = L->n;
    for (i = 0; i < run; i++) {
        tick(ctx, id, 0.0f, stick, 0);
        poll_ho(ctx, L);
    }
    for (i = 0; i < n; i++) {
        tick(ctx, id, 0.0f, stick, i == 0 ? SA1_BUTTON_ACTION : 0u);
        poll_ho(ctx, L);
        if (hfind(L, l0, SA1_HOSTOBJ_EVENT_RELEASED, obj) != NULL) {
            return hfind(L, l0, SA1_HOSTOBJ_EVENT_RELEASED, obj);
        }
    }
    return NULL;
}

/* the speed (host units/s) the game's player-space speed (x, y, 0) per step gives: |v| x 60 x 4 (Source preset) */
static float hu_speed(float x, float y)
{
    return sqrtf(x * x + y * y) * 60.0f * 4.0f;
}

static void t_lift(sa1_context *ctx)
{
    sa1_character_id id;
    sa1g_world *w;
    sa1_hostobj_desc d;
    sa1_hostobj_held hd;
    sa1_state_ex s, s1;
    evlist *L = (evlist *)calloc(1, sizeof *L);
    glist *G = (glist *)calloc(1, sizeof *G);
    uint32_t obj = 0, en = 0, i, took, g0, l0, lift_step = 0, m21 = 0, ptwp = 0, pcwp = 0, self_hits = 0, far = 0;
    uint32_t carried = 0, phase_ok = 0;
    const sa1_hostobj_event *e;
    pst p;
    printf("\n== lift, carry, throw, put down (addendum 6.7 cases 11, 12, 14)\n");
    id = make_char(ctx, SA1_CHAR_SONIC, 0.0f, 0.0f, 8.0f, -1, 1);
    w = sa1_character_world(ctx, id);
    if (id == 0 || w == NULL || L == NULL || G == NULL) {
        CHECK(0, "Sonic");
        free(L);
        free(G);
        return;
    }
    settle(ctx, id, 20);
    gpoll(ctx, G);
    state_of(ctx, id, &s);
    hold_desc(&d, s.position[0] + 40.0f, s.position[1], 0.0f, 8.0f, 8.0f);  /* body r 2, half height 2, 10 units ahead */
    d.user = 11;
    CHECK(sa1_hostobj_add(ctx, &d, &obj) == SA1_OK, "a HOLDABLE cylinder (body r 2) 10 units ahead");
    settle(ctx, id, 2);
    ptwp = sa1ho_world_proxy_twp(w, obj);
    pcwp = wrd32(w, ptwp + 0x38u);
    CHECK(ptwp != 0 && wrd8(w, ptwp + 2u) == 4 && (wrd16(w, ptwp + 4u) & 0x240u) == 0x240u,
          "its proxy: taskwk.id %u (the lift class), flag 0x%X (8C0586A0: 0x240)", (unsigned)wrd8(w, ptwp + 2u),
          (unsigned)wrd16(w, ptwp + 4u));
    g0 = G->n;
    l0 = L->n;
    g_nrec = 0;
    took = do_lift(ctx, id, L, obj, 40);
    gpoll(ctx, G);
    e = hfind(L, l0, SA1_HOSTOBJ_EVENT_LIFTED, obj);
    for (i = 0; i < g_nrec; i++) {
        m21 += g_rec[i].mode == 21;
    }
    CHECK(took && e != NULL, "LIFTED after %u pairs (Sonic in mode 21 for %u steps)", (unsigned)took, (unsigned)m21);
    if (e != NULL) {
        const rec *r = rec_at(e->step);
        lift_step = e->step;
        CHECK(r != NULL && r->mode == 23, "at the LIFTED step Sonic is in mode 23 (carry; %d)", r ? (int)r->mode : -1);
    }
    CHECK(gcount(G, g0, SA1_EVENT_SOUND, SE_LIFT) == 1, "the lift's SE 0x45A");
    pstate(w, &p);
    CHECK(p.htp != 0 && wrd32(w, p.htp + 0x20u) == ptwp && (wrd16(w, ptwp + 4u) & 0x1000u) && (p.tflag & 0x800u),
          "playerwk.htp is the proxy, its taskwk flag 0x1000, Sonic's 0x800 (flag 0x%X)", (unsigned)wrd16(w, ptwp + 4u));
    state_of(ctx, id, &s);
    CHECK(s.held_object == obj, "sa1_state_ex.held_object = the object");
    /* carry: walk 30 pairs; the proxy rides at the hands and never collides with its holder */
    g_nrec = 0;
    for (i = 0; i < 30; i++) {
        float dx, dy, dz;
        tick(ctx, id, 0.0f, 1.0f, 0);
        poll_ho(ctx, L);
        pstate(w, &p);
        memset(&hd, 0, sizeof hd);
        hd.struct_size = sizeof hd;
        sa1_character_held(ctx, id, &hd);
        carried += hd.object == obj;
        phase_ok += hd.phase == SA1_HOLD_CARRYING;
        if (wrd32(w, p.cwp + 0x18u) == pcwp && (wrd16(w, p.cwp + 2u) & 2u)) {
            self_hits++;
        }
        dx = wrdf(w, ptwp + 0x20u) - p.pos[0];
        dy = wrdf(w, ptwp + 0x24u) - p.pos[1];
        dz = wrdf(w, ptwp + 0x28u) - p.pos[2];
        far += dx * dx + dy * dy + dz * dz > 20.0f * 20.0f;
    }
    state_of(ctx, id, &s);
    CHECK(carried == 30 && phase_ok == 30, "sa1_character_held: the object, CARRYING, every pair (%u / %u)",
          (unsigned)carried, (unsigned)phase_ok);
    CHECK(self_hits == 0, "the held proxy makes no CCL entry: Sonic's colliwk never hits it (%u)", (unsigned)self_hits);
    CHECK(far == 0, "it stays at the hands while he walks (more than 20 units away in %u pairs)", (unsigned)far);
    {
        float hx = hd.hand[0] - hd.position[0], hy = hd.hand[1] - hd.position[1], hz = hd.hand[2] - hd.position[2];
        printf("        held pose: position %.1f %.1f %.1f, hand %.1f %.1f %.1f (offset %.1f HU), yaw %.3f (state %.3f)\n",
               (double)hd.position[0], (double)hd.position[1], (double)hd.position[2], (double)hd.hand[0],
               (double)hd.hand[1], (double)hd.hand[2], (double)sqrtf(hx * hx + hy * hy + hz * hz), (double)hd.yaw,
               (double)s.yaw);
        CHECK(sqrtf(hx * hx + hy * hy + hz * hz) < 16.0f && fabsf(hd.yaw - s.yaw) < 0.01f,
              "the hand anchor is next to the reported position, the yaw is the holder's");
    }
    /* run and throw (fixed speed: no target in the list) */
    e = do_release(ctx, id, L, obj, 25, 1.0f, 60);
    state_of(ctx, id, &s);
    CHECK(e != NULL && e->reason == SA1_RELEASE_THROW, "running + the action button: RELEASED reason %u (THROW %u)",
          e ? (unsigned)e->reason : 99u, (unsigned)SA1_RELEASE_THROW);
    if (e != NULL) {
        const rec *r = rec_at(e->step);
        float want = hu_speed(2.0f, 0.8f);
        CHECK(fabsf(e->speed - want) < 1.0f && e->direction[2] > 0.3f && e->direction[2] < 0.45f,
              "the game's fixed throw P2G(2, 0.8, 0): %.1f HU/s (%.1f), direction up %.3f", (double)e->speed,
              (double)want, (double)e->direction[2]);
        CHECK(r != NULL && r->action == 60, "Sonic's action at the release: %d (60)", r ? (int)r->action : -1);
    }
    pstate(w, &p);
    CHECK(p.htp == 0 && !(wrd16(w, ptwp + 4u) & 0x1000u) && s.held_object == 0, "released: htp NULL, the object's 0x1000 "
          "clear, held_object 0");
    {
        float before[3], after[3];
        for (i = 0; i < 3; i++) {
            before[i] = wrdf(w, ptwp + 0x20u + 4u * i);
        }
        settle(ctx, id, 10);
        for (i = 0; i < 3; i++) {
            after[i] = wrdf(w, ptwp + 0x20u + 4u * i);
        }
        CHECK(!memcmp(before, after, sizeof before), "the proxy keeps the release point until the host moves it");
    }
    /* put it in front again; lift; put down below jog speed */
    settle(ctx, id, 30);
    state_of(ctx, id, &s);
    move_obj(ctx, obj, s.position[0] + 40.0f * cosf(s.yaw), s.position[1] + 40.0f * sinf(s.yaw), 0.0f);
    settle(ctx, id, 2);
    took = do_lift(ctx, id, L, obj, 40);
    CHECK(took != 0, "lifted again (%u pairs)", (unsigned)took);
    settle(ctx, id, 4);
    e = do_release(ctx, id, L, obj, 0, 0.0f, 60);
    pstate(w, &p);
    CHECK(e != NULL && e->reason == SA1_RELEASE_PUT_DOWN && e->speed == 0.0f,
          "standing + the action button: RELEASED PUT_DOWN (%u), speed %.2f", e ? (unsigned)e->reason : 99u,
          e ? (double)e->speed : -1.0);
    if (e != NULL) {
        float dx = wrdf(w, ptwp + 0x20u) - p.pos[0], dz = wrdf(w, ptwp + 0x28u) - p.pos[2];
        float want = wrdf(w, p.pwp + 0x10Cu) + 2.0f;
        CHECK(fabsf(sqrtf(dx * dx + dz * dz) - want) < 0.05f, "pushed out to p.rad + body radius: %.3f (%.3f) units",
              (double)sqrtf(dx * dx + dz * dz), (double)want);
    }
    /* aimed throw (case 12): an ENEMY in the target list */
    settle(ctx, id, 10);
    state_of(ctx, id, &s);
    move_obj(ctx, obj, s.position[0] + 40.0f * cosf(s.yaw), s.position[1] + 40.0f * sinf(s.yaw), 0.0f);
    settle(ctx, id, 2);
    took = do_lift(ctx, id, L, obj, 40);
    state_of(ctx, id, &s);
    enemy_desc(&d, SA1_HOSTOBJ_ENEMY, s.position[0] + 300.0f * cosf(s.yaw), s.position[1] + 300.0f * sinf(s.yaw),
               s.position[2] + 80.0f, 18.0f);
    d.touch = 0;
    d.flags = SA1_HOSTOBJ_NOT_SOLID;
    d.user = 12;
    sa1_hostobj_add(ctx, &d, &en);
    e = do_release(ctx, id, L, obj, 10, 1.0f, 60);
    CHECK(took && e != NULL && e->reason == SA1_RELEASE_THROW_AIMED && e->target == en,
          "an ENEMY within 100 units: RELEASED THROW_AIMED (%u), target = the ENEMY (%s)", e ? (unsigned)e->reason : 99u,
          e && e->target == en ? "yes" : "no");
    if (e != NULL) {
        const rec *r = rec_at(e->step);
        CHECK(fabsf(e->speed - 720.0f) < 1.0f && r != NULL && r->action == 59,
              "speed 3 per step = %.1f HU/s (720), action %d (59)", (double)e->speed, r ? (int)r->action : -1);
    }
    sa1_hostobj_remove(ctx, en, SA1_HOSTOBJ_REMOVE_SILENT);
    /* hurt while carrying (case 14) */
    settle(ctx, id, 30);
    state_of(ctx, id, &s);
    move_obj(ctx, obj, s.position[0] + 40.0f * cosf(s.yaw), s.position[1] + 40.0f * sinf(s.yaw), 0.0f);
    settle(ctx, id, 2);
    sa1_character_add_rings(ctx, id, 3);
    took = do_lift(ctx, id, L, obj, 40);
    settle(ctx, id, 3);
    state_of(ctx, id, &s);
    enemy_desc(&d, SA1_HOSTOBJ_ENEMY, s.position[0], s.position[1], s.position[2] + 18.0f, 18.0f);
    d.flags = SA1_HOSTOBJ_NOT_SOLID;
    sa1_hostobj_add(ctx, &d, &en);
    l0 = L->n;
    gpoll(ctx, G);
    g0 = G->n;
    for (i = 0; i < 10 && hfind(L, l0, SA1_HOSTOBJ_EVENT_RELEASED, obj) == NULL; i++) {
        tick(ctx, id, 0.0f, 0.0f, 0);
        poll_ho(ctx, L);
        gpoll(ctx, G);
    }
    e = hfind(L, l0, SA1_HOSTOBJ_EVENT_RELEASED, obj);
    CHECK(took && e != NULL && e->reason == SA1_RELEASE_DROP && gfind(G, g0, SA1_EVENT_HURT) != NULL,
          "an ENEMY touches him while he carries: HURT and RELEASED DROP (%u)", e ? (unsigned)e->reason : 99u);
    if (e != NULL) {
        printf("        the drop's speed: %.1f HU/s (the drop gives P2G(1, 1, 0) = %.0f HU/s only when 8C06C880 still sees "
               "the object: the hurt flag 4 is set at the drop)\n", (double)e->speed, (double)hu_speed(1.0f, 1.0f));
    }
    sa1_hostobj_remove(ctx, en, SA1_HOSTOBJ_REMOVE_SILENT);
    sa1_hostobj_remove(ctx, obj, SA1_HOSTOBJ_REMOVE_SILENT);
    settle(ctx, id, 2);
    gpoll(ctx, G);
    (void)lift_step;
    (void)s1;
    sa1_character_destroy(ctx, id);
    free(L);
    free(G);
}

/* ---- every kind lifts (case 13) ----------------------------------------------------------------------------------- */
static void t_kinds(sa1_context *ctx)
{
    static const struct {
        int32_t kind;
        const char *name;
        float fx, fy;           /* the fixed throw, player space per step */
    } k[] = {
        { SA1_CHAR_TAILS, "Tails", 2.0f, 0.8f },
        { SA1_CHAR_KNUCKLES, "Knuckles", 2.0f, 0.8f },
        { SA1_CHAR_AMY, "Amy", 2.0f, 0.8f },
        { SA1_CHAR_GAMMA, "E-102", 2.0f, 0.8f },
        { SA1_CHAR_BIG, "Big", 3.0f, 3.0f },
    };
    sa1_hostobj_desc d;
    sa1_state_ex s;
    evlist *L = (evlist *)calloc(1, sizeof *L);
    uint32_t i, obj, took;
    const sa1_hostobj_event *e;
    printf("\n== every kind lifts (addendum 6.7 case 13)\n");
    if (L == NULL) {
        return;
    }
    for (i = 0; i < sizeof k / sizeof k[0]; i++) {
        /* away from the floor's tile corners: standing on a seam of the GRID floor (here the corner of four 1024-HU tiles
         * at the origin), the land collision loses the ground for a step while the lift pulls the character (8C06CB40
         * moves taskwk.pos without speed) and Tails / Knuckles / Amy / E-102 drop the lift (their lift modes test the
         * ground, e.g. Tails 8C0BCC9A); Sonic's lift mode does not test it */
        sa1_character_id id = make_char(ctx, k[i].kind, 300.0f, 300.0f, 8.0f, -1, 1);
        sa1g_world *w = sa1_character_world(ctx, id);
        if (id == 0 || w == NULL) {
            CHECK(0, "%s", k[i].name);
            continue;
        }
        settle(ctx, id, 20);
        state_of(ctx, id, &s);
        hold_desc(&d, s.position[0] + 40.0f * cosf(s.yaw), s.position[1] + 40.0f * sinf(s.yaw), 0.0f, 8.0f, 8.0f);
        obj = 0;
        sa1_hostobj_add(ctx, &d, &obj);
        settle(ctx, id, 2);
        if (g_verbose) {
            pst q;
            uint32_t pt = sa1ho_world_proxy_twp(w, obj);
            pstate(w, &q);
            printf("        %s: pos %.1f %.1f %.1f yaw %.3f mode %d; proxy pos %.1f %.1f %.1f; cwp flag 0x%X hit 0x%X "
                   "(proxy cwp 0x%X)%s", k[i].name, (double)q.pos[0], (double)q.pos[1], (double)q.pos[2],
                   (double)s.yaw, (int)q.mode, (double)wrdf(w, pt + 0x20u), (double)wrdf(w, pt + 0x24u),
                   (double)wrdf(w, pt + 0x28u), (unsigned)wrd16(w, q.cwp + 2u), (unsigned)wrd32(w, q.cwp + 0x18u),
                   (unsigned)wrd32(w, pt + 0x38u), "\n");
        }
        took = do_lift(ctx, id, L, obj, 60);
        CHECK(took != 0, "%s lifts it (LIFTED after %u pairs)", k[i].name, (unsigned)took);
        if (took) {
            pst p;
            settle(ctx, id, 4);
            e = do_release(ctx, id, L, obj, 25, 1.0f, 80);
            pstate(w, &p);
            if (e != NULL && e->reason == SA1_RELEASE_THROW) {
                CHECK(fabsf(e->speed - hu_speed(k[i].fx, k[i].fy)) < 1.0f, "%s throws it: %.1f HU/s (P2G(%.0f, %.1f, 0) = "
                      "%.1f)", k[i].name, (double)e->speed, (double)k[i].fx, (double)k[i].fy,
                      (double)hu_speed(k[i].fx, k[i].fy));
            } else {
                CHECK(e != NULL, "%s lets it go: RELEASED reason %u, %.1f HU/s (action %d)", k[i].name,
                      e ? (unsigned)e->reason : 99u, e ? (double)e->speed : -1.0, (int)p.action);
            }
        }
        sa1_hostobj_remove(ctx, obj, SA1_HOSTOBJ_REMOVE_SILENT);
        settle(ctx, id, 2);
        poll_game(ctx, 0, 0, NULL);
        sa1_character_destroy(ctx, id);
    }
    /* a sphere-only HOLDABLE: no side contact, never lifted */
    {
        sa1_character_id id = make_char(ctx, SA1_CHAR_SONIC, 0.0f, 0.0f, 8.0f, -1, 1);
        settle(ctx, id, 20);
        state_of(ctx, id, &s);
        enemy_desc(&d, SA1_HOSTOBJ_HOLDABLE, s.position[0] + 40.0f, s.position[1], 8.0f, 8.0f);
        obj = 0;
        sa1_hostobj_add(ctx, &d, &obj);
        settle(ctx, id, 2);
        took = do_lift(ctx, id, L, obj, 40);
        CHECK(obj != 0 && took == 0, "a sphere-only HOLDABLE is not lifted (no side contact)");
        sa1_hostobj_remove(ctx, obj, SA1_HOSTOBJ_REMOVE_SILENT);
        /* HEAVY: Sonic grabs and pushes (class 0x400), never lifts */
        settle(ctx, id, 4);
        state_of(ctx, id, &s);
        hold_desc(&d, s.position[0] + 40.0f * cosf(s.yaw), s.position[1] + 40.0f * sinf(s.yaw), 0.0f, 8.0f, 8.0f);
        d.flags = SA1_HOSTOBJ_HEAVY;
        obj = 0;
        sa1_hostobj_add(ctx, &d, &obj);
        settle(ctx, id, 2);
        took = do_lift(ctx, id, L, obj, 40);
        CHECK(obj != 0 && took == 0, "a HEAVY HOLDABLE: Sonic does not lift it");
        sa1_hostobj_remove(ctx, obj, SA1_HOSTOBJ_REMOVE_SILENT);
        settle(ctx, id, 4);
        {
            pst p;
            pstate(sa1_character_world(ctx, id), &p);
            CHECK(p.htp == 0 && !(p.tflag & 0x800u), "and after its removal he holds nothing (htp 0x%X)", (unsigned)p.htp);
        }
        poll_game(ctx, 0, 0, NULL);
        sa1_character_destroy(ctx, id);
    }
    {
        sa1_character_id id = make_char(ctx, SA1_CHAR_BIG, 0.0f, 0.0f, 8.0f, -1, 1);
        settle(ctx, id, 20);
        state_of(ctx, id, &s);
        hold_desc(&d, s.position[0] + 40.0f * cosf(s.yaw), s.position[1] + 40.0f * sinf(s.yaw), 0.0f, 8.0f, 8.0f);
        d.flags = SA1_HOSTOBJ_HEAVY;
        obj = 0;
        sa1_hostobj_add(ctx, &d, &obj);
        settle(ctx, id, 2);
        took = do_lift(ctx, id, L, obj, 60);
        CHECK(took != 0, "a HEAVY HOLDABLE: Big lifts it (class 5 = 0x600 for Big)");
        sa1_hostobj_remove(ctx, obj, SA1_HOSTOBJ_REMOVE_SILENT);
        settle(ctx, id, 4);
        poll_game(ctx, 0, 0, NULL);
        sa1_character_destroy(ctx, id);
    }
    free(L);
}

/* ---- forced release and removal while held (case 15) -------------------------------------------------------------- */
static uint32_t g_lcg = 12345u;

static uint32_t lcg(void)
{
    g_lcg = g_lcg * 1103515245u + 12345u;
    return (g_lcg >> 16) & 0x7FFFu;
}

static void t_forced(sa1_context *ctx, uint32_t runs)
{
    sa1_character_id id;
    sa1g_world *w;
    sa1_hostobj_desc d;
    sa1_state_ex s;
    evlist *L = (evlist *)calloc(1, sizeof *L);
    uint32_t obj = 0, r, i, held_runs = 0, forced = 0, stale = 0, lifts_failed = 0, removes = 0, releases = 0;
    double t0;
    printf("\n== forced release and removal while held (addendum 6.7 case 15, %u runs)\n", (unsigned)runs);
    id = make_char(ctx, SA1_CHAR_SONIC, 0.0f, 0.0f, 8.0f, -1, 0);
    w = sa1_character_world(ctx, id);
    if (id == 0 || w == NULL || L == NULL) {
        CHECK(0, "Sonic");
        free(L);
        return;
    }
    settle(ctx, id, 20);
    state_of(ctx, id, &s);
    hold_desc(&d, s.position[0] + 40.0f, s.position[1], 0.0f, 8.0f, 8.0f);
    sa1_hostobj_add(ctx, &d, &obj);
    t0 = tu_now_ms();
    for (r = 0; r < runs; r++) {
        uint32_t wait = lcg() % 16u, op = lcg() % 2u, l0, held;
        pst p;
        state_of(ctx, id, &s);
        move_obj(ctx, obj, s.position[0] + 40.0f * cosf(s.yaw), s.position[1] + 40.0f * sinf(s.yaw), 0.0f);
        settle(ctx, id, 2);
        L->n = 0;
        if (do_lift(ctx, id, L, obj, 40) == 0) {
            lifts_failed++;
            settle(ctx, id, 10);
            continue;
        }
        for (i = 0; i < wait; i++) {
            tick(ctx, id, 0.0f, (lcg() % 3u) ? 0.0f : 0.5f, 0);
            poll_ho(ctx, L);
        }
        pstate(w, &p);
        held = p.htp != 0;
        held_runs += held;
        l0 = L->n;
        if (op == 0) {
            sa1_hostobj_release(ctx, obj);
            releases++;
        } else {
            sa1_hostobj_remove(ctx, obj, SA1_HOSTOBJ_REMOVE_SILENT);
            removes++;
        }
        tick(ctx, id, 0.0f, 0.0f, 0);
        poll_ho(ctx, L);
        pstate(w, &p);
        forced += hfind(L, l0, SA1_HOSTOBJ_EVENT_RELEASED, obj) != NULL &&
                  hfind(L, l0, SA1_HOSTOBJ_EVENT_RELEASED, obj)->reason == SA1_RELEASE_FORCED;
        stale += p.htp != 0 || (p.tflag & 0x800u) != 0;
        if (op == 1) {
            sa1_hostobj_add(ctx, &d, &obj);
        }
        settle(ctx, id, 3);
        if (w->dead) {
            break;
        }
    }
    printf("        %u runs (%u releases, %u removals) in %.0f ms; held at the request in %u; lifts that failed %u\n",
           (unsigned)r, (unsigned)releases, (unsigned)removes, tu_now_ms() - t0, (unsigned)held_runs,
           (unsigned)lifts_failed);
    CHECK(r == runs && !w->dead, "no fault (%u runs)", (unsigned)r);
    CHECK(stale == 0, "no stale htp / hold flag after a forced release or a removal (%u)", (unsigned)stale);
    CHECK(forced == held_runs && held_runs > runs / 2u, "every held one reported RELEASED (FORCED): %u of %u",
          (unsigned)forced, (unsigned)held_runs);
    sa1_hostobj_remove(ctx, obj, SA1_HOSTOBJ_REMOVE_SILENT);
    settle(ctx, id, 2);
    {
        sa1_hostobj_info info;
        memset(&info, 0, sizeof info);
        info.struct_size = sizeof info;
        sa1_hostobj_info_get(ctx, id, &info);
        CHECK(info.proxies == 0 && info.created == info.destroyed, "every proxy destroyed (created %u, destroyed %u)",
              (unsigned)info.created, (unsigned)info.destroyed);
    }
    poll_game(ctx, 0, 0, NULL);
    sa1_character_destroy(ctx, id);
    free(L);
}

/* ---- BALLOON (addendum 6.7 case 16) ------------------------------------------------------------------------------- */
static void balloon_desc(sa1_hostobj_desc *d, float x, float y, float z, uint32_t flags, float launch)
{
    enemy_desc(d, SA1_HOSTOBJ_BALLOON, x, y, z, 18.0f);
    d->flags = flags;
    d->launch_speed = launch;
}

/* jump, then the jump button in the air every `gap` pairs (the homing attack); stops at the n-th HIT */
static void homing_into(sa1_context *ctx, sa1_character_id id, evlist *L, uint32_t nhits, uint32_t pairs)
{
    uint32_t i, wait = 6, l0 = L->n;
    tick(ctx, id, 0.0f, 0.0f, SA1_BUTTON_JUMP);
    poll_ho(ctx, L);
    for (i = 0; i < pairs && hcount(L, l0, SA1_HOSTOBJ_EVENT_HIT, 0) < nhits; i++) {
        uint32_t b = 0, before = L->n;
        if (wait != 0 && --wait == 0) {
            b = SA1_BUTTON_JUMP;
        }
        tick(ctx, id, 0.0f, 0.0f, b);
        poll_ho(ctx, L);
        if (hcount(L, before, SA1_HOSTOBJ_EVENT_HIT, 0) != 0) {
            wait = 4;
        }
    }
    tick(ctx, id, 0.0f, 0.0f, 0);       /* the step after the hit is recorded */
    poll_ho(ctx, L);
}

static void t_balloon(sa1_context *ctx)
{
    static const struct {
        int32_t kind;
        const char *name;
        int32_t mode, action;   /* the next-action handler of order 3 (6.2.1) */
    } k[] = {
        { SA1_CHAR_SONIC, "Sonic", 9, 16 },
        { SA1_CHAR_TAILS, "Tails", 7, 17 },
        { SA1_CHAR_KNUCKLES, "Knuckles", 7, 17 },
        { SA1_CHAR_AMY, "Amy", 6, 16 },
        { SA1_CHAR_GAMMA, "E-102", 5, 6 },
        { SA1_CHAR_BIG, "Big", 5, 12 },
    };
    sa1_character_id id;
    sa1g_world *w;
    sa1_hostobj_desc d;
    sa1_state_ex s;
    evlist *L = (evlist *)calloc(1, sizeof *L);
    glist *G = (glist *)calloc(1, sizeof *G);
    uint32_t b1 = 0, b2 = 0, i, g0;
    const sa1_hostobj_event *h;
    printf("\n== balloons (addendum 6.7 case 16)\n");
    if (L == NULL || G == NULL) {
        free(L);
        free(G);
        return;
    }
    /* a: the homing attack onto a balloon: the game's ground-spring launch */
    id = make_char(ctx, SA1_CHAR_SONIC, 0.0f, 0.0f, 8.0f, -1, 1);
    w = sa1_character_world(ctx, id);
    settle(ctx, id, 20);
    state_of(ctx, id, &s);
    balloon_desc(&d, s.position[0] + 160.0f, s.position[1], 40.0f, 0, 0.0f);
    sa1_hostobj_add(ctx, &d, &b1);
    gpoll(ctx, G);
    g0 = G->n;
    homing_into(ctx, id, L, 1, 120);
    h = hfind(L, 0, SA1_HOSTOBJ_EVENT_HIT, b1);
    CHECK(h != NULL && h->attack == SA1_ATTACK_HOMING && (h->flags & SA1_HITF_LAUNCHED) && !(h->flags & SA1_HITF_BOUNCED),
          "homing onto a BALLOON: HIT attack %u (HOMING), flags 0x%X (LAUNCHED, not BOUNCED)", h ? (unsigned)h->attack : 99u,
          h ? (unsigned)h->flags : 0u);
    if (h != NULL) {
        const rec *r0 = rec_at(h->step), *r1 = rec_at(h->step + 1u);
        uint32_t m12 = 0;
        CHECK(r0 != NULL && r0->spd[0] == 0.0f && r0->spd[1] == 5.0f && r0->spd[2] == 0.0f,
              "the step of the hit: motionwk.spd (%.3f, %.3f, %.3f) (8C07C720: (0, 5, 0))", r0 ? (double)r0->spd[0] : 0.0,
              r0 ? (double)r0->spd[1] : 0.0, r0 ? (double)r0->spd[2] : 0.0);
        CHECK(r1 != NULL && r1->mode == 9 && r1->action == 16, "the next step: mode %d action %d (9 / 16: Sonic_Spring_Launch)",
              r1 ? (int)r1->mode : -1, r1 ? (int)r1->action : -1);
        for (i = 0; i < 40; i++) {
            tick(ctx, id, 0.0f, 0.0f, 0);
        }
        for (i = 0; i < g_nrec; i++) {
            m12 += g_rec[i].step > h->step && g_rec[i].mode == 12 && g_rec[i].action == 17;
        }
        CHECK(m12 > 0, "then the fall: mode 12 action 17 (%u steps)", (unsigned)m12);
    }
    gpoll(ctx, G);
    CHECK(gcount(G, g0, SA1_EVENT_RUMBLE, 0xFFFFFFFFu) >= 1, "the spring's rumble (SA1_EVENT_RUMBLE)");
    sa1_hostobj_remove(ctx, b1, SA1_HOSTOBJ_REMOVE_SILENT);
    settle(ctx, id, 40);
    /* b: LAUNCH_BOUNCE at 600 HU/s: SetVelocityP only, the ball stays, a second balloon is homed */
    state_of(ctx, id, &s);
    balloon_desc(&d, s.position[0] + 160.0f, s.position[1], 40.0f, SA1_HOSTOBJ_LAUNCH_BOUNCE, 600.0f);
    sa1_hostobj_add(ctx, &d, &b1);
    balloon_desc(&d, s.position[0] + 330.0f, s.position[1], 70.0f, SA1_HOSTOBJ_LAUNCH_BOUNCE, 600.0f);
    sa1_hostobj_add(ctx, &d, &b2);
    L->n = 0;
    g_nrec = 0;
    homing_into(ctx, id, L, 1, 120);
    h = hfind(L, 0, SA1_HOSTOBJ_EVENT_HIT, b1);
    if (h != NULL) {
        const rec *r0 = rec_at(h->step), *r1 = rec_at(h->step + 1u);
        CHECK((h->flags & SA1_HITF_LAUNCHED) && r0 != NULL && r0->spd[1] == 2.5f && r1 != NULL && r1->mode != 9 &&
                  (r1->tflag & 0x100u),
              "LAUNCH_BOUNCE: spd.y %.3f (600 HU/s = 2.5 per step), next step mode %d (not the spring's 9), ball kept "
              "(flag 0x%X)", r0 ? (double)r0->spd[1] : 0.0, r1 ? (int)r1->mode : -1, r1 ? (unsigned)r1->tflag : 0u);
    } else {
        CHECK(0, "LAUNCH_BOUNCE: no HIT on the first balloon");
    }
    settle(ctx, id, 4);
    homing_into(ctx, id, L, 1, 120);       /* in the air: its first press is the homing attack */
    h = hfind(L, 0, SA1_HOSTOBJ_EVENT_HIT, b2);
    CHECK(h != NULL && h->attack == SA1_ATTACK_HOMING, "a second balloon is homed after the bounce (%s)",
          h ? "HIT HOMING" : "no HIT");
    sa1_hostobj_remove(ctx, b1, SA1_HOSTOBJ_REMOVE_SILENT);
    sa1_hostobj_remove(ctx, b2, SA1_HOSTOBJ_REMOVE_SILENT);
    settle(ctx, id, 2);
    gpoll(ctx, G);
    sa1_character_destroy(ctx, id);
    /* c: E-102's laser pops it without launching anybody */
    id = make_char(ctx, SA1_CHAR_GAMMA, 0.0f, 0.0f, 8.0f, -1, 1);
    settle(ctx, id, 20);
    state_of(ctx, id, &s);
    balloon_desc(&d, s.position[0] + 240.0f, s.position[1], 40.0f, 0, 0.0f);
    sa1_hostobj_add(ctx, &d, &b1);
    L->n = 0;
    for (i = 0; i < 30; i++) {
        tick(ctx, id, 0.0f, 0.0f, SA1_BUTTON_ACTION);
        poll_ho(ctx, L);
    }
    for (i = 0; i < 120 && hfind(L, 0, SA1_HOSTOBJ_EVENT_HIT, b1) == NULL; i++) {
        tick(ctx, id, 0.0f, 0.0f, 0);
        poll_ho(ctx, L);
    }
    h = hfind(L, 0, SA1_HOSTOBJ_EVENT_HIT, b1);
    CHECK(h != NULL && h->attack == SA1_ATTACK_LASER && (h->flags & SA1_HITF_PROJECTILE) &&
              !(h->flags & SA1_HITF_LAUNCHED) && hfind(L, 0, SA1_HOSTOBJ_EVENT_LOCKED, b1) != NULL,
          "E-102 locks on and fires: HIT LASER, PROJECTILE, not LAUNCHED (flags 0x%X)", h ? (unsigned)h->flags : 0u);
    sa1_hostobj_remove(ctx, b1, SA1_HOSTOBJ_REMOVE_SILENT);
    settle(ctx, id, 2);
    gpoll(ctx, G);
    sa1_character_destroy(ctx, id);
    /* d: POP_ON_TOUCH, every character: walking into it launches by the character's own order-3 handler */
    for (i = 0; i < sizeof k / sizeof k[0]; i++) {
        uint32_t j, launched = 0;
        id = make_char(ctx, k[i].kind, 0.0f, 0.0f, 8.0f, -1, 1);
        w = sa1_character_world(ctx, id);
        settle(ctx, id, 20);
        state_of(ctx, id, &s);
        balloon_desc(&d, s.position[0] + 100.0f * cosf(s.yaw), s.position[1] + 100.0f * sinf(s.yaw), 18.0f,
                     SA1_HOSTOBJ_POP_ON_TOUCH, 0.0f);
        b1 = 0;
        sa1_hostobj_add(ctx, &d, &b1);
        L->n = 0;
        g_nrec = 0;
        for (j = 0; j < 60 && hfind(L, 0, SA1_HOSTOBJ_EVENT_HIT, b1) == NULL; j++) {
            tick(ctx, id, 0.0f, 1.0f, 0);
            poll_ho(ctx, L);
        }
        tick(ctx, id, 0.0f, 1.0f, 0);
        poll_ho(ctx, L);
        h = hfind(L, 0, SA1_HOSTOBJ_EVENT_HIT, b1);
        if (h != NULL) {
            const rec *r1 = rec_at(h->step + 1u);
            launched = (h->flags & SA1_HITF_LAUNCHED) && r1 != NULL && r1->mode == k[i].mode && r1->action == k[i].action;
            CHECK(launched, "%s walks into a POP_ON_TOUCH balloon: LAUNCHED, the next step mode %d action %d (%d / %d)",
                  k[i].name, r1 ? (int)r1->mode : -1, r1 ? (int)r1->action : -1, (int)k[i].mode, (int)k[i].action);
        } else {
            CHECK(0, "%s walks into a POP_ON_TOUCH balloon: no HIT", k[i].name);
        }
        (void)w;
        sa1_hostobj_remove(ctx, b1, SA1_HOSTOBJ_REMOVE_SILENT);
        settle(ctx, id, 2);
        gpoll(ctx, G);
        sa1_character_destroy(ctx, id);
    }
    free(L);
    free(G);
}

/* ---- the world mask (addendum 6.7 case 17, the part that needs no follower world) ------------------------------- */
static void t_mask(sa1_context *ctx)
{
    sa1_character_id id;
    sa1g_world *w;
    sa1_hostobj_desc d;
    sa1_state_ex s;
    evlist *L = (evlist *)calloc(1, sizeof *L);
    uint32_t en = 0, ta = 0, ho = 0, ba = 0, took;
    const sa1_hostobj_event *e;
    pst p;
    printf("\n== the world's kind mask (addendum 6.5 / 6.7 case 17)\n");
    id = make_char(ctx, SA1_CHAR_SONIC, 0.0f, 0.0f, 8.0f, -1, 1);
    w = sa1_character_world(ctx, id);
    if (id == 0 || w == NULL || L == NULL) {
        CHECK(0, "Sonic");
        free(L);
        return;
    }
    CHECK(sa1_hostobj_world_mask(ctx, id, SA1_HOSTOBJ_KIND_BIT(SA1_HOSTOBJ_ENEMY) |
                                              SA1_HOSTOBJ_KIND_BIT(SA1_HOSTOBJ_HAZARD)) == SA1_OK,
          "mask ENEMY | HAZARD (a follower world's default)");
    CHECK(sa1_hostobj_world_mask(ctx, id, 1u) == SA1_ERR_INVALID_ARGUMENT, "kind bit 0 refused");
    settle(ctx, id, 20);
    state_of(ctx, id, &s);
    enemy_desc(&d, SA1_HOSTOBJ_ENEMY, s.position[0] + 400.0f, s.position[1], 18.0f, 18.0f);
    sa1_hostobj_add(ctx, &d, &en);
    enemy_desc(&d, SA1_HOSTOBJ_TARGET, s.position[0] + 400.0f, s.position[1] + 100.0f, 18.0f, 18.0f);
    sa1_hostobj_add(ctx, &d, &ta);
    balloon_desc(&d, s.position[0] + 400.0f, s.position[1] - 100.0f, 18.0f, 0, 0.0f);
    sa1_hostobj_add(ctx, &d, &ba);
    hold_desc(&d, s.position[0] + 40.0f, s.position[1], 0.0f, 8.0f, 8.0f);
    sa1_hostobj_add(ctx, &d, &ho);
    settle(ctx, id, 2);
    CHECK(sa1ho_world_proxy_twp(w, en) != 0 && sa1ho_world_proxy_twp(w, ta) == 0 && sa1ho_world_proxy_twp(w, ba) == 0 &&
              sa1ho_world_proxy_twp(w, ho) == 0,
          "only the ENEMY is mirrored (proxies: ENEMY %s, TARGET %s, BALLOON %s, HOLDABLE %s)",
          sa1ho_world_proxy_twp(w, en) ? "yes" : "no", sa1ho_world_proxy_twp(w, ta) ? "yes" : "no",
          sa1ho_world_proxy_twp(w, ba) ? "yes" : "no", sa1ho_world_proxy_twp(w, ho) ? "yes" : "no");
    took = do_lift(ctx, id, L, ho, 30);
    CHECK(took == 0, "the masked HOLDABLE cannot be lifted");
    settle(ctx, id, 40);            /* the press above may have started something else (a spin dash) */
    state_of(ctx, id, &s);
    move_obj(ctx, ho, s.position[0] + 40.0f * cosf(s.yaw), s.position[1] + 40.0f * sinf(s.yaw), 0.0f);
    sa1_hostobj_world_mask(ctx, id, 0);
    settle(ctx, id, 2);
    CHECK(sa1ho_world_proxy_twp(w, en) && sa1ho_world_proxy_twp(w, ta) && sa1ho_world_proxy_twp(w, ba) &&
              sa1ho_world_proxy_twp(w, ho), "mask 0: every kind is mirrored at the next pair");
    took = do_lift(ctx, id, L, ho, 40);
    CHECK(took != 0, "the HOLDABLE is lifted");
    settle(ctx, id, 3);
    L->n = 0;
    sa1_hostobj_world_mask(ctx, id, SA1_HOSTOBJ_KIND_BIT(SA1_HOSTOBJ_ENEMY));
    settle(ctx, id, 2);
    poll_ho(ctx, L);
    e = hfind(L, 0, SA1_HOSTOBJ_EVENT_RELEASED, ho);
    pstate(w, &p);
    CHECK(e != NULL && e->reason == SA1_RELEASE_FORCED && p.htp == 0 && sa1ho_world_proxy_twp(w, ho) == 0,
          "masking a held HOLDABLE: RELEASED FORCED, htp NULL, the proxy gone");
    /* a mask set before the world exists is applied when it is built */
    {
        sa1_character_desc cd;
        sa1_character_id id2 = 0;
        memset(&cd, 0, sizeof cd);
        cd.struct_size = sizeof cd;
        cd.kind = SA1_CHAR_SONIC;
        cd.position[2] = 8.0f;
        sa1_character_create_ex(ctx, &cd, &id2);       /* no world yet */
        CHECK(sa1_hostobj_world_mask(ctx, id2, SA1_HOSTOBJ_KIND_BIT(SA1_HOSTOBJ_HAZARD)) == SA1_OK &&
                  sa1_character_set_option(ctx, id2, SA1_COPT_KEEP_RINGS, 1) == SA1_OK,
              "the mask and an option set before the world is built");
        tick(ctx, id2, 0.0f, 0.0f, 0);                  /* builds it */
        {
            sa1g_world *w2 = sa1_character_world(ctx, id2);
            CHECK(w2 != NULL && w2->ho.kind_mask == SA1_HOSTOBJ_KIND_BIT(SA1_HOSTOBJ_HAZARD) &&
                      w2->ho.opts == (1u << SA1_COPT_KEEP_RINGS), "and applied to the world built later");
        }
        sa1_character_destroy(ctx, id2);
    }
    remove_all(ctx, &en, 1);
    remove_all(ctx, &ta, 1);
    remove_all(ctx, &ba, 1);
    remove_all(ctx, &ho, 1);
    settle(ctx, id, 2);
    poll_game(ctx, 0, 0, NULL);
    sa1_character_destroy(ctx, id);
    free(L);
}

/* ---- the per-object flags (DISABLED, NO_HOMING, HOLD_LOCKED, HOLD_HITS) and the LIFTING phase ------------------- */
/* Sonic runs and jumps onto the object 30 units ahead (as the jump part); returns the HITs on it */
static uint32_t jump_onto(sa1_context *ctx, sa1_character_id id, evlist *L, uint32_t obj, float x)
{
    uint32_t i, pressed = 0, l0 = L->n;
    sa1_state_ex s;
    for (i = 0; i < 70; i++) {
        uint32_t b = 0;
        state_of(ctx, id, &s);
        if (!pressed && s.position[0] >= x - g_jump_d) {
            b = SA1_BUTTON_JUMP;
            pressed = 1;
        }
        tick(ctx, id, 0.0f, i < 50 ? 1.0f : 0.0f, b);
        poll_ho(ctx, L);
    }
    return hcount(L, l0, SA1_HOSTOBJ_EVENT_HIT, obj);
}

static void t_flags(sa1_context *ctx)
{
    sa1_character_id id;
    sa1g_world *w;
    sa1_hostobj_desc d;
    sa1_hostobj_held hd;
    sa1_state_ex s;
    evlist *L = (evlist *)calloc(1, sizeof *L);
    uint32_t obj = 0, i, n, listed = 0, lifting = 0, took;
    printf("\n== flags (DISABLED, NO_HOMING, HOLD_LOCKED, HOLD_HITS) and the LIFTING phase\n");
    if (L == NULL) {
        return;
    }
    /* DISABLED: Sonic jumps onto it, nothing */
    id = make_char(ctx, SA1_CHAR_SONIC, 0.0f, 0.0f, 8.0f, -1, 1);
    w = sa1_character_world(ctx, id);
    settle(ctx, id, 20);
    state_of(ctx, id, &s);
    enemy_desc(&d, SA1_HOSTOBJ_ENEMY, s.position[0] + 120.0f, s.position[1], 18.0f, 18.0f);
    d.flags = SA1_HOSTOBJ_DISABLED;
    sa1_hostobj_add(ctx, &d, &obj);
    n = jump_onto(ctx, id, L, obj, d.position[0]);
    state_of(ctx, id, &s);
    CHECK(n == 0 && s.hurt_steps == 0xFFFFFFFFu, "DISABLED: a jump through it gives no HIT (%u) and no hurt", (unsigned)n);
    sa1_hostobj_remove(ctx, obj, SA1_HOSTOBJ_REMOVE_SILENT);
    settle(ctx, id, 2);
    sa1_character_destroy(ctx, id);
    /* NO_HOMING: never in player 0's target list (the homing attack, the Light Speed Attack, aimed throws ...) */
    id = make_char(ctx, SA1_CHAR_SONIC, 0.0f, 0.0f, 8.0f, -1, 1);
    w = sa1_character_world(ctx, id);
    settle(ctx, id, 20);
    state_of(ctx, id, &s);
    enemy_desc(&d, SA1_HOSTOBJ_TARGET, s.position[0] + 160.0f, s.position[1], 40.0f, 18.0f);
    d.flags = SA1_HOSTOBJ_NO_HOMING;
    sa1_hostobj_add(ctx, &d, &obj);
    settle(ctx, id, 2);
    g_watch = sa1ho_world_proxy_twp(w, obj);
    g_nrec = 0;
    settle(ctx, id, 20);
    for (i = 0; i < g_nrec; i++) {
        listed += g_rec[i].in_list;
    }
    sa1_hostobj_remove(ctx, obj, SA1_HOSTOBJ_REMOVE_SILENT);
    settle(ctx, id, 2);
    enemy_desc(&d, SA1_HOSTOBJ_TARGET, s.position[0] + 160.0f, s.position[1], 40.0f, 18.0f);
    sa1_hostobj_add(ctx, &d, &obj);
    settle(ctx, id, 2);
    g_watch = sa1ho_world_proxy_twp(w, obj);
    g_nrec = 0;
    settle(ctx, id, 20);
    n = 0;
    for (i = 0; i < g_nrec; i++) {
        n += g_rec[i].in_list;
    }
    CHECK(listed == 0 && n == g_nrec && n > 0, "NO_HOMING: in the target list in %u of 40 steps (without it: %u of %u)",
          (unsigned)listed, (unsigned)n, (unsigned)g_nrec);
    g_watch = 0;
    sa1_hostobj_remove(ctx, obj, SA1_HOSTOBJ_REMOVE_SILENT);
    settle(ctx, id, 2);
    /* HOLD_LOCKED: taskwk.flag 0x1000, the game's "already held": not lifted */
    state_of(ctx, id, &s);
    hold_desc(&d, s.position[0] + 40.0f * cosf(s.yaw), s.position[1] + 40.0f * sinf(s.yaw), 0.0f, 8.0f, 8.0f);
    d.flags = SA1_HOSTOBJ_HOLD_LOCKED;
    sa1_hostobj_add(ctx, &d, &obj);
    settle(ctx, id, 2);
    took = do_lift(ctx, id, L, obj, 30);
    CHECK(took == 0, "HOLD_LOCKED: not lifted");
    /* the same object unlocked (an update): lifted; sa1_character_held reports LIFTING before LIFTED */
    settle(ctx, id, 30);
    state_of(ctx, id, &s);
    hold_desc(&d, s.position[0] + 40.0f * cosf(s.yaw), s.position[1] + 40.0f * sinf(s.yaw), 0.0f, 8.0f, 8.0f);
    sa1_hostobj_update(ctx, obj, &d);
    settle(ctx, id, 2);
    {
        uint32_t l0 = L->n;
        for (i = 0; i < 40 && hfind(L, l0, SA1_HOSTOBJ_EVENT_LIFTED, obj) == NULL; i++) {
            tick(ctx, id, 0.0f, 0.0f, i == 0 ? SA1_BUTTON_ACTION : 0u);
            poll_ho(ctx, L);
            memset(&hd, 0, sizeof hd);
            hd.struct_size = sizeof hd;
            sa1_character_held(ctx, id, &hd);
            lifting += hd.phase == SA1_HOLD_LIFTING && hd.object == obj;
        }
        CHECK(hfind(L, l0, SA1_HOSTOBJ_EVENT_LIFTED, obj) != NULL && lifting > 0,
              "updated without HOLD_LOCKED: lifted, LIFTING reported for %u pairs before", (unsigned)lifting);
    }
    sa1_hostobj_release(ctx, obj);
    settle(ctx, id, 2);
    sa1_hostobj_remove(ctx, obj, SA1_HOSTOBJ_REMOVE_SILENT);
    settle(ctx, id, 2);
    sa1_character_destroy(ctx, id);
    /* HOLD_HITS: a breakable prop reports the attack that out-levels it */
    for (i = 0; i < 2; i++) {
        uint32_t hits;
        id = make_char(ctx, SA1_CHAR_SONIC, 0.0f, 0.0f, 8.0f, -1, 1);
        settle(ctx, id, 20);
        state_of(ctx, id, &s);
        hold_desc(&d, s.position[0] + 120.0f, s.position[1], 0.0f, 12.0f, 12.0f);
        d.flags = i ? SA1_HOSTOBJ_HOLD_HITS : 0u;
        obj = 0;
        sa1_hostobj_add(ctx, &d, &obj);
        hits = jump_onto(ctx, id, L, obj, d.position[0]);
        if (i) {
            const sa1_hostobj_event *e = hfind(L, 0, SA1_HOSTOBJ_EVENT_HIT, obj);
            CHECK(hits >= 1 && e != NULL && e->attack == SA1_ATTACK_SPIN, "HOLD_HITS: Sonic's jump into it is a HIT (%u, "
                  "attack %u)", (unsigned)hits, e ? (unsigned)e->attack : 99u);
        } else {
            CHECK(hits == 0, "without HOLD_HITS the same jump reports nothing (%u)", (unsigned)hits);
        }
        sa1_hostobj_remove(ctx, obj, SA1_HOSTOBJ_REMOVE_SILENT);
        settle(ctx, id, 2);
        poll_game(ctx, 0, 0, NULL);
        sa1_character_destroy(ctx, id);
    }
    (void)w;
    free(L);
}

/* ---- determinism of round 5's paths (4.2 case 10 for the game side) ------------------------------------------------- */
typedef struct r5_out {
    uint32_t events, game, arena, n;
} r5_out;

static void r5_hash_events(const evlist *L, uint32_t *crc)
{
    uint32_t i;
    for (i = 0; i < L->n; i++) {
        const sa1_hostobj_event *e = &L->e[i];
        uint32_t v[10] = { e->type, (uint32_t)e->user, e->attack, e->level, e->damage, e->shape, e->flags, e->step,
                           e->reason, e->target != 0 };
        *crc = tu_crc32(*crc, (const uint8_t *)v, sizeof v);
        *crc = tu_crc32(*crc, (const uint8_t *)e->position, sizeof e->position);
        *crc = tu_crc32(*crc, (const uint8_t *)e->direction, sizeof e->direction);
        *crc = tu_crc32(*crc, (const uint8_t *)&e->speed, sizeof e->speed);
    }
}

/* Sonic lifts and throws a HOLDABLE, homes onto a BALLOON, touches an ENEMY with rings, takes a host hit with
 * FACE_SOURCE; the hostobj events, the HURT / DEATH / RUMBLE events and the arena */
static void r5_run(sa1_context *ctx, r5_out *o)
{
    sa1_character_id id;
    sa1g_world *w;
    sa1_hostobj_desc d;
    sa1_state_ex s;
    sa1_damage dm;
    evlist *L = (evlist *)calloc(1, sizeof *L);
    glist *G = (glist *)calloc(1, sizeof *G);
    uint32_t ho = 0, ba = 0, en = 0, i;
    memset(o, 0, sizeof *o);
    id = make_char(ctx, SA1_CHAR_SONIC, 0.0f, 0.0f, 8.0f, 5, 1);
    w = sa1_character_world(ctx, id);
    if (id == 0 || w == NULL || L == NULL || G == NULL) {
        free(L);
        free(G);
        return;
    }
    settle(ctx, id, 20);
    state_of(ctx, id, &s);
    hold_desc(&d, s.position[0] + 40.0f, s.position[1], 0.0f, 8.0f, 8.0f);
    d.user = 1;
    sa1_hostobj_add(ctx, &d, &ho);
    settle(ctx, id, 2);
    do_lift(ctx, id, L, ho, 40);
    do_release(ctx, id, L, ho, 25, 1.0f, 60);
    sa1_hostobj_remove(ctx, ho, SA1_HOSTOBJ_REMOVE_SILENT);
    settle(ctx, id, 30);
    state_of(ctx, id, &s);
    balloon_desc(&d, s.position[0] + 160.0f * cosf(s.yaw), s.position[1] + 160.0f * sinf(s.yaw), 40.0f, 0, 0.0f);
    d.user = 2;
    sa1_hostobj_add(ctx, &d, &ba);
    homing_into(ctx, id, L, 1, 120);
    sa1_hostobj_remove(ctx, ba, SA1_HOSTOBJ_REMOVE_SILENT);
    settle(ctx, id, 40);
    state_of(ctx, id, &s);
    enemy_desc(&d, SA1_HOSTOBJ_ENEMY, s.position[0], s.position[1], s.position[2] + 18.0f, 18.0f);
    d.flags = SA1_HOSTOBJ_NOT_SOLID;
    d.user = 3;
    sa1_hostobj_add(ctx, &d, &en);
    for (i = 0; i < 10; i++) {
        tick(ctx, id, 0.0f, 0.0f, 0);
        poll_ho(ctx, L);
        gpoll(ctx, G);
    }
    sa1_hostobj_remove(ctx, en, SA1_HOSTOBJ_REMOVE_SILENT);
    settle(ctx, id, 2);
    wait_vulnerable(ctx, id, w, 200);
    sa1_character_add_rings(ctx, id, 3);
    memset(&dm, 0, sizeof dm);
    dm.struct_size = sizeof dm;
    dm.flags = SA1_DAMAGE_FACE_SOURCE;
    state_of(ctx, id, &s);
    dm.source[0] = s.position[0] + 50.0f;
    dm.source[1] = s.position[1] - 70.0f;
    dm.source[2] = s.position[2];
    dm.object = 9;
    sa1_character_damage(ctx, id, &dm);
    for (i = 0; i < 30; i++) {
        tick(ctx, id, 0.0f, 0.0f, 0);
        poll_ho(ctx, L);
        gpoll(ctx, G);
    }
    r5_hash_events(L, &o->events);
    for (i = 0; i < G->n; i++) {
        const sa1_event *e = &G->e[i];
        if (e->type == SA1_EVENT_HURT || e->type == SA1_EVENT_DEATH || e->type == SA1_EVENT_RUMBLE) {
            uint32_t v[5] = { e->type, e->id < 16u ? e->id : 0xFFFFu, e->bank, e->arg, e->step };
            o->game = tu_crc32(o->game, (const uint8_t *)v, sizeof v);
            o->game = tu_crc32(o->game, (const uint8_t *)e->position, sizeof e->position);
        }
    }
    o->n = L->n;
    o->arena = arena_crc(w);
    settle(ctx, id, 2);
    poll_game(ctx, 0, 0, NULL);
    sa1_character_destroy(ctx, id);
    free(L);
    free(G);
}

static void t_determinism5(sa1_context *ctx)
{
    r5_out a, b;
    printf("\n== determinism of round 5's paths (lift, throw, balloon, touch, host damage)\n");
    r5_run(ctx, &a);
    r5_run(ctx, &b);
    CHECK(a.n >= 3 && a.events == b.events && a.game == b.game, "events twice in a row: %08X %08X / %08X %08X (%u)",
          (unsigned)a.events, (unsigned)a.game, (unsigned)b.events, (unsigned)b.game, (unsigned)a.n);
    CHECK(a.arena == b.arena, "the arena twice in a row: %08X / %08X", (unsigned)a.arena, (unsigned)b.arena);
    printf("DETERMINISM5 %08X %08X %08X\n", (unsigned)a.events, (unsigned)a.game, (unsigned)a.arena);
}

/* ---- main ---------------------------------------------------------------------------------------------------------- */
int main(int argc, char **argv)
{
    const char *gdi = getenv("SA1_TEST_GDI"), *only = NULL;
    sa1_context *ctx;
    uint32_t runs = 1000;
    int i;
    ga_args(&argc, argv);
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--only") && i + 1 < argc) {
            only = argv[++i];
        } else if (!strcmp(argv[i], "--verbose")) {
            g_verbose = 1;
        } else if (!strcmp(argv[i], "--jump") && i + 1 < argc) {
            g_jump_d = (float)atof(argv[++i]);
        } else if (!strcmp(argv[i], "--runs") && i + 1 < argc) {
            runs = (uint32_t)atoi(argv[++i]);
        } else {
            gdi = argv[i];
        }
    }
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("libsa1 %s host-object test (API 0.3, sadc docs/libsa1_interaction.md 4.2), %u-bit\n", sa1_version_string(),
           (unsigned)(sizeof(void *) * 8u));
    if (only == NULL || strstr("api", only)) {
        t_api();
    }
    if (gdi == NULL) {
        printf("no disc (GDI argument or SA1_TEST_GDI): the world parts are skipped\n");
    } else {
        ctx = make_ctx(gdi);
        if (ctx == NULL) {
            return 2;
        }
        if (only == NULL || strstr("rings", only)) {
            t_rings(ctx);
        }
        if (only == NULL || strstr("inert", only)) {
            t_inert(ctx);
        }
        if (only == NULL || strstr("jump", only)) {
            t_jump(ctx);
        }
        if (only == NULL || strstr("touch", only)) {
            t_touch(ctx);
        }
        if (only == NULL || strstr("homing", only)) {
            t_homing(ctx);
        }
        if (only == NULL || strstr("churn", only)) {
            t_churn(ctx);
        }
        if (only == NULL || strstr("determinism", only)) {
            t_determinism(ctx);
        }
        if (only == NULL || strstr("contacts", only)) {
            t_contacts(ctx);
        }
        if (only == NULL || strstr("damage", only)) {
            t_damage(ctx);
        }
        if (only == NULL || strstr("hostdmg", only)) {
            t_hostdmg(ctx);
        }
        if (only == NULL || strstr("early", only)) {
            t_early(ctx);
        }
        if (only == NULL || strstr("roll", only)) {
            t_roll(ctx);
        }
        if (only == NULL || strstr("armor", only)) {
            t_armor(ctx);
        }
        if (only == NULL || strstr("e102", only)) {
            t_e102(ctx);
        }
        if (only == NULL || strstr("worlds", only)) {
            t_worlds(ctx);
        }
        if (only == NULL || strstr("lift", only)) {
            t_lift(ctx);
        }
        if (only == NULL || strstr("kinds", only)) {
            t_kinds(ctx);
        }
        if (only == NULL || strstr("forced", only)) {
            t_forced(ctx, runs);
        }
        if (only == NULL || strstr("balloon", only)) {
            t_balloon(ctx);
        }
        if (only == NULL || strstr("mask", only)) {
            t_mask(ctx);
        }
        if (only == NULL || strstr("flags", only)) {
            t_flags(ctx);
        }
        if (only == NULL || strstr("determinism5", only)) {
            t_determinism5(ctx);
        }
        sa1_shutdown(ctx);
    }
    if (ga_enabled()) {
        uint32_t damaged = ga_check();
        CHECK(damaged == 0, "guard allocator: %u damaged blocks", (unsigned)damaged);
    }
    printf("\nhostobj_test: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
