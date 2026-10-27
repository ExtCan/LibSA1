/*
 * companion_test.c - round 0.9.1: Amy's bird through sa1_character_set_companion / sa1_character_companion (sadc
 * docs/research/gsa1_070_bird.md F1 and 7.3 with its Review R.2.1 / R.2.2 / R.4 (a) (b) (c); the review's probe
 * sadc/build/research/bird/review/review_probe.c is its draft).
 *
 *   companion_test [--verbose] GDI
 *
 * Built and run by tests/build_charopt.py. Context in SA1 space; a flat floor (GRID collision). Amy is created as G-SA1
 * creates her (SA1_CREATE_WORLD, no stage: Emerald Coast act 1, stage 0x0100, whose stage fails the spawner 8C0DEDC0's
 * gate) unless a case names Twinkle Park act 2 (stage 0x0301, which passes it: the game makes its own bird in step 0).
 *
 *   A  no call: no bird (the owner's 0.7.0 report)
 *   B  ON right after create_ex: one list-3 task, exec 8C0DE964 after one pair, about 12 above Amy; present = 1
 *   C  90 pairs running / jumping / hammer: 9 bird model records per pair (sub step 0, texlist_AMY), 13 render draws
 *      (archive 0 textures 44-52, textured, 348 triangles), 2 blob shadow draws (Amy's and the bird's), the bird's
 *      centroid at alpha 0.5 on the straight lerp while running
 *   D  ON again: still one bird
 *   E  OFF between pairs: gone after one pair, no bird draws; present = 0
 *   F  refusals: Sonic (SA1_ERR_INVALID_ARGUMENT), an unknown companion, an unknown id (SA1_ERR_NOT_FOUND), a world whose
 *      player 0 is not its Amy (CurrentCharacter 0: SA1_ERR_UNSUPPORTED)
 *   G  (review E) stage 0x0301, ON at create: exactly one bird after 31 pairs (F1 as first written made two)
 *   H  (review F) stage 0x0301, OFF at create: no bird after 31 pairs and no bird draw in any pair
 *   I  a world built later (no SA1_CREATE_WORLD), ON before the first tick: one bird after it
 *   J  a world built later at 0x0301, OFF before the first tick: no bird after 31 pairs
 *   K  the allocations of the context are balanced at sa1_shutdown (a counting host allocator)
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
#include "sa1_capture.h"

static int g_fail, g_ok, g_verbose;
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

#define PHYS(a)          ((a) & 0x1FFFFFFFu)
#define BIRD_INIT        0x8C0DECDAu
#define BIRD_EXEC        0x8C0DE964u
#define BIRD_DISP        0x8C0DE8C0u
#define BIRD_ACTION      0x8C4BEF90u     /* NJS_ACTION { object, motion } */
#define TEXLIST_AMY      0x8C4BF3ACu

/* ---- a counting allocator (K) ----------------------------------------------------------------------------------- */
static long g_allocs, g_frees;
static void *count_alloc(void *user, size_t n)
{
    void *p = malloc(n ? n : 1u);
    (void)user;
    if (p != NULL) {
        g_allocs++;
    }
    return p;
}

static void count_free(void *user, void *p)
{
    (void)user;
    if (p != NULL) {
        g_frees++;
        free(p);
    }
}

static void log_cb(void *user, int32_t level, const char *msg)
{
    (void)user;
    if (level >= SA1_LOG_WARN || g_verbose) {
        printf("    [libsa1] %s\n", msg);
    }
}

/* ---- the scene and the character ---------------------------------------------------------------------------------- */
static int scene(sa1_context *ctx)
{
    static sa1_collision_tri t[288];
    uint32_t n = 0;
    int i, j;
    for (i = -6; i < 6; i++) {
        for (j = -6; j < 6; j++) {
            float x0 = 100.0f * (float)i, x1 = x0 + 100.0f, z0 = 100.0f * (float)j, z1 = z0 + 100.0f;
            float a[3] = { x0, 0, z0 }, c[3] = { x0, 0, z1 }, d[3] = { x1, 0, z1 }, e[3] = { x1, 0, z0 };
            memset(&t[n], 0, sizeof t[n]);
            memcpy(t[n].v[0], a, 12);
            memcpy(t[n].v[1], c, 12);
            memcpy(t[n].v[2], d, 12);
            t[n++].surface = SA1_SURFACE_SOLID;
            memset(&t[n], 0, sizeof t[n]);
            memcpy(t[n].v[0], a, 12);
            memcpy(t[n].v[1], d, 12);
            memcpy(t[n].v[2], e, 12);
            t[n++].surface = SA1_SURFACE_SOLID;
        }
    }
    return sa1_collision_set_static(ctx, t, n) == SA1_OK ? 0 : -1;
}

/* level < 0: no stage (G-SA1's world); world: SA1_CREATE_WORLD */
static sa1_character_id mk(sa1_context *ctx, int32_t kind, int32_t level, int32_t act, int world)
{
    sa1_character_desc d;
    sa1_character_id id = 0;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.kind = kind;
    d.flags = (world ? SA1_CREATE_WORLD : 0u) | (level >= 0 ? SA1_CREATE_STAGE : 0u);
    d.level = level >= 0 ? level : 0;
    d.act = act;
    d.position[0] = 13.7f;
    d.position[1] = 2.0f;
    d.position[2] = 7.3f;
    d.yaw = 0.6f;
    if (SA1_FAILED(sa1_character_create_ex(ctx, &d, &id))) {
        printf("  create: %s\n", sa1_last_error(ctx));
        return 0;
    }
    return id;
}

static int tick(sa1_context *ctx, sa1_character_id id, uint32_t buttons, float sx, float sy)
{
    sa1_input in;
    sa1_event ev[64];
    uint32_t n;
    memset(&in, 0, sizeof in);
    in.struct_size = sizeof in;
    in.buttons = buttons;
    in.stick_x = sx;
    in.stick_y = sy;
    if (sa1_character_tick(ctx, id, &in, 1.0f / 30.0f, NULL, NULL) != SA1_OK) {
        printf("  tick: %s\n", sa1_last_error(ctx));
        return -1;
    }
    do {
        n = 0;
        sa1_poll_events(ctx, ev, 64, &n);
    } while (n == 64);
    return 0;
}

static uint32_t wrd(const sa1g_world *w, uint32_t g)
{
    uint32_t v = 0;
    if (w == NULL || sa1g_world_rd32(w, g, &v) != 0) {
        return 0;
    }
    return v;
}

static float wrdf(const sa1g_world *w, uint32_t g)
{
    uint32_t v = wrd(w, g);
    float f;
    memcpy(&f, &v, 4);
    return f;
}

/* the bird tasks (top-level, exec 8C0DECDA or 8C0DE964); *level / *tp of the first */
static uint32_t birds(const sa1g_world *w, uint32_t *tp_out, uint32_t *level)
{
    uint32_t n = 0, lv;
    for (lv = 0; lv < 8u; lv++) {
        uint32_t tp = wrd(w, SA1G_ADDR_TASK_LISTS + 4u * lv), guard = 0;
        while (tp != 0 && guard++ < 4096u) {
            uint32_t ex = PHYS(wrd(w, tp + 0x10u));
            if (ex == PHYS(BIRD_INIT) || ex == PHYS(BIRD_EXEC)) {
                if (n == 0) {
                    if (tp_out) {
                        *tp_out = tp;
                    }
                    if (level) {
                        *level = lv;
                    }
                }
                n++;
            }
            tp = wrd(w, tp);
        }
    }
    return n;
}

static uint32_t nbirds(sa1_context *ctx, sa1_character_id id)
{
    return birds(sa1_character_world(ctx, id), NULL, NULL);
}

/* the models of Birdie's tree (action -> object), for the capture records */
static uint32_t g_models[32], g_nmodels;
static void collect_models(const sa1g_world *w, uint32_t obj, int depth)
{
    while (obj != 0 && depth < 32 && g_nmodels < 32) {
        uint32_t m = wrd(w, obj + 4u);
        if (m != 0) {
            g_models[g_nmodels++] = PHYS(m);
        }
        collect_models(w, wrd(w, obj + 0x2Cu), depth + 1);
        obj = wrd(w, obj + 0x30u);
    }
}

static int is_bird_model(uint32_t m)
{
    uint32_t i;
    for (i = 0; i < g_nmodels; i++) {
        if (g_models[i] == PHYS(m)) {
            return 1;
        }
    }
    return 0;
}

typedef struct capc {
    uint32_t recs, sub1, texlist_amy;
} capc;

static void cap_count(sa1_context *ctx, sa1_character_id id, capc *b)
{
    sa1_capture *cap = sa1_character_capture(ctx, id);
    const sa1_cap_rec *r = NULL;
    uint32_t n = cap ? sa1_capture_records(cap, &r) : 0u, i;
    memset(b, 0, sizeof *b);
    for (i = 0; i < n; i++) {
        if (!SA1_CAP_IS_MODEL(r[i].kind) || !is_bird_model(r[i].obj)) {
            continue;
        }
        b->recs++;
        b->sub1 += r[i].sub != 0;
        b->texlist_amy += PHYS(r[i].texlist) == PHYS(TEXLIST_AMY);
    }
}

typedef struct rendc {
    uint32_t draws, tris, shadows, untextured, src_char;
    double c[3];
} rendc;

static void rend_count(sa1_context *ctx, sa1_character_id id, float alpha, rendc *b)
{
    const sa1_render *r = NULL;
    uint32_t d, t, k;
    memset(b, 0, sizeof *b);
    if (sa1_character_render(ctx, id, alpha, &r) != SA1_OK || r == NULL) {
        return;
    }
    for (d = 0; d < r->num_draws; d++) {
        const sa1_draw *dr = &r->draws[d];
        if (dr->flags & SA1_DRAW_SHADOW) {
            b->shadows++;
        }
        if (dr->prim != SA1_PRIM_TRIANGLES || dr->archive != 0 || dr->texture < 44 || dr->texture > 52) {
            continue;
        }
        b->draws++;
        b->untextured += (dr->flags & SA1_DRAW_NO_TEXTURE) != 0;
        b->src_char += dr->source == SA1_SOURCE_CHARACTER;
        for (t = dr->first; t < dr->first + dr->count && t < r->num_triangles; t++) {
            for (k = 0; k < 9; k++) {
                b->c[k % 3] += r->positions[9u * t + k];
            }
            b->tris++;
        }
    }
    if (b->tris) {
        for (k = 0; k < 3; k++) {
            b->c[k] /= 3.0 * (double)b->tris;
        }
    }
}

/* ---- the cases ---------------------------------------------------------------------------------------------------- */
static void t_abcde(sa1_context *ctx)
{
    sa1_character_id id;
    sa1g_world *w;
    uint32_t tp = 0, lv = 0, i, pairs = 0, with_recs = 0, sub1 = 0, tl_ok = 1, with_draws = 0, untex = 0, src_ok = 0;
    uint32_t tris_ok = 0, shadow2 = 0, shadow3 = 0;
    int32_t present = -1;
    capc cc;
    rendc rc;
    sa1g_player_ptrs pp;
    printf("A-E: Amy as G-SA1 creates her (stage 0x0100)\n");
    id = mk(ctx, SA1_CHAR_AMY, -1, 0, 1);
    w = id ? sa1_character_world(ctx, id) : NULL;
    if (w == NULL) {
        g_fail++;
        return;
    }
    collect_models(w, wrd(w, BIRD_ACTION), 0);
    for (i = 0; i < 30; i++) {
        if (tick(ctx, id, 0, 0.0f, i < 15 ? 1.0f : 0.0f)) {
            g_fail++;
            return;
        }
    }
    rend_count(ctx, id, 1.0f, &rc);
    sa1_character_companion(ctx, id, SA1_COMPANION_BIRD, &present);
    CHECK(nbirds(ctx, id) == 0 && rc.draws == 0 && present == 0, "A: no call, 30 pairs: %u bird task(s), %u bird draws, "
          "present %d (%u models in Birdie's tree)", (unsigned)nbirds(ctx, id), (unsigned)rc.draws, (int)present,
          (unsigned)g_nmodels);
    sa1_character_destroy(ctx, id);

    id = mk(ctx, SA1_CHAR_AMY, -1, 0, 1);
    w = id ? sa1_character_world(ctx, id) : NULL;
    if (w == NULL) {
        g_fail++;
        return;
    }
    CHECK(sa1_character_set_companion(ctx, id, SA1_COMPANION_BIRD, 1) == SA1_OK, "B: set_companion(BIRD, 1) right after "
          "create_ex");
    sa1_character_companion(ctx, id, SA1_COMPANION_BIRD, &present);
    CHECK(present == 1 && nbirds(ctx, id) == 1, "B: the task is in the world at once (present %d, %u task)", (int)present,
          (unsigned)nbirds(ctx, id));
    if (tick(ctx, id, 0, 0.0f, 0.0f)) {
        g_fail++;
        return;
    }
    birds(w, &tp, &lv);
    sa1g_world_player(w, &pp);
    {
        float dy = wrdf(w, wrd(w, tp + 0x20u) + 0x24u) - wrdf(w, pp.twp + 0x24u);
        CHECK(nbirds(ctx, id) == 1 && lv == 3 && PHYS(wrd(w, tp + 0x10u)) == PHYS(BIRD_EXEC) &&
                  PHYS(wrd(w, tp + 0x14u)) == PHYS(BIRD_DISP) && dy > 8.0f && dy < 16.0f,
              "B: after 1 pair one task in list %u, exec %08X disp %08X, %.2f above Amy", (unsigned)lv,
              (unsigned)wrd(w, tp + 0x10u), (unsigned)wrd(w, tp + 0x14u), dy);
    }
    for (i = 0; i < 90; i++) {
        uint32_t b = i < 40 ? 0u : (i % 30 == 5 ? SA1_BUTTON_JUMP : (i % 30 == 15 ? SA1_BUTTON_ACTION : 0u));
        if (tick(ctx, id, b, 0.0f, i < 60 ? 1.0f : 0.0f)) {
            g_fail++;
            return;
        }
        cap_count(ctx, id, &cc);
        rend_count(ctx, id, 1.0f, &rc);
        pairs++;
        with_recs += cc.recs == 9u;
        sub1 += cc.sub1;
        tl_ok &= cc.texlist_amy == cc.recs;
        with_draws += rc.draws == 13u;
        untex += rc.untextured;
        src_ok += rc.draws != 0 && rc.src_char == rc.draws;
        tris_ok += rc.tris == 348u;
        shadow2 += rc.shadows == 2u;
        shadow3 += rc.shadows > 2u;
        if (i == 30) {
            rendc m0, mh, m1;
            double e = 0.0, span = 0.0;
            int k;
            rend_count(ctx, id, 0.0f, &m0);
            rend_count(ctx, id, 0.5f, &mh);
            rend_count(ctx, id, 1.0f, &m1);
            for (k = 0; k < 3; k++) {
                e += (mh.c[k] - 0.5 * (m0.c[k] + m1.c[k])) * (mh.c[k] - 0.5 * (m0.c[k] + m1.c[k]));
                span += (m1.c[k] - m0.c[k]) * (m1.c[k] - m0.c[k]);
            }
            CHECK(m0.tris == 348u && m1.tris == 348u && sqrt(span) > 0.5 && sqrt(e) < 0.05 * sqrt(span),
                  "C: running (pair %u): the bird's centroid moves %.3f between alpha 0 and 1, %.4f from the midpoint at "
                  "alpha 0.5", (unsigned)(i + 2), sqrt(span), sqrt(e));
        }
    }
    CHECK(with_recs == pairs && sub1 == 0 && tl_ok, "C: 9 bird model records in %u of %u pairs, all on sub step 0, all "
          "texlist_AMY", (unsigned)with_recs, (unsigned)pairs);
    CHECK(with_draws == pairs && untex == 0 && src_ok == pairs && tris_ok == pairs, "C: 13 bird draws (archive 0 textures "
          "44-52, textured, SA1_SOURCE_CHARACTER, 348 triangles) in %u / %u / %u of %u pairs", (unsigned)with_draws,
          (unsigned)src_ok, (unsigned)tris_ok, (unsigned)pairs);
    /* the blobs vanish when their owner is high above the floor (the game's 8C09222C): not in every jump pair */
    CHECK(shadow2 * 4u >= pairs * 3u && shadow3 == 0, "C: 2 SA1_DRAW_SHADOW draws (Amy's and the bird's blob) in %u of %u "
          "pairs, more in %u", (unsigned)shadow2, (unsigned)pairs, (unsigned)shadow3);
    CHECK(sa1_character_set_companion(ctx, id, SA1_COMPANION_BIRD, 1) == SA1_OK && nbirds(ctx, id) == 1,
          "D: ON again: %u bird", (unsigned)nbirds(ctx, id));
    tick(ctx, id, 0, 0.0f, 0.0f);
    CHECK(nbirds(ctx, id) == 1, "D: after one more pair: %u bird", (unsigned)nbirds(ctx, id));
    CHECK(sa1_character_set_companion(ctx, id, SA1_COMPANION_BIRD, 0) == SA1_OK, "E: set_companion(BIRD, 0) between pairs");
    tick(ctx, id, 0, 0.0f, 0.0f);
    sa1_character_companion(ctx, id, SA1_COMPANION_BIRD, &present);
    rend_count(ctx, id, 1.0f, &rc);
    CHECK(nbirds(ctx, id) == 0 && present == 0 && rc.draws == 0 && rc.shadows == 1,
          "E: after one pair %u bird, present %d, %u bird draws, %u shadow draw", (unsigned)nbirds(ctx, id), (int)present,
          (unsigned)rc.draws, (unsigned)rc.shadows);
    for (i = 0; i < 10; i++) {
        tick(ctx, id, 0, 0.0f, 1.0f);
    }
    CHECK(nbirds(ctx, id) == 0, "E: still none 10 pairs later (%u)", (unsigned)nbirds(ctx, id));
    sa1_character_destroy(ctx, id);
}

static void cb_char0(sa1g_world *w, void *user)
{
    (void)user;
    *(int16_t *)sa1g_world_ptr(w, SA1G_ADDR_CUR_CHAR) = 0;
}

static void t_refusals(sa1_context *ctx)
{
    sa1_character_id id;
    int32_t present = -1;
    printf("F: refusals\n");
    id = mk(ctx, SA1_CHAR_SONIC, -1, 0, 1);
    CHECK(id != 0 && sa1_character_set_companion(ctx, id, SA1_COMPANION_BIRD, 1) == SA1_ERR_INVALID_ARGUMENT,
          "Sonic: SA1_ERR_INVALID_ARGUMENT (Amy only)");
    CHECK(sa1_character_companion(ctx, id, SA1_COMPANION_BIRD, &present) == SA1_OK && present == 0,
          "Sonic: companion() present %d", (int)present);
    sa1_character_destroy(ctx, id);
    id = mk(ctx, SA1_CHAR_AMY, -1, 0, 1);
    CHECK(sa1_character_set_companion(ctx, id, 7u, 1) == SA1_ERR_INVALID_ARGUMENT, "companion 7: SA1_ERR_INVALID_ARGUMENT");
    CHECK(sa1_character_set_companion(ctx, (sa1_character_id)0x7FFF00FF, SA1_COMPANION_BIRD, 1) == SA1_ERR_NOT_FOUND &&
              sa1_character_companion(ctx, (sa1_character_id)0x7FFF00FF, SA1_COMPANION_BIRD, &present) ==
                  SA1_ERR_NOT_FOUND,
          "an unknown id: SA1_ERR_NOT_FOUND");
    /* a follower world (round 6b: the leader's proxy in player slot 0, CurrentCharacter 0): refused */
    sa1g_world_call(sa1_character_world(ctx, id), cb_char0, NULL);
    CHECK(sa1_character_set_companion(ctx, id, SA1_COMPANION_BIRD, 1) == SA1_ERR_UNSUPPORTED && nbirds(ctx, id) == 0,
          "player 0 not the world's Amy (CurrentCharacter 0): SA1_ERR_UNSUPPORTED, no bird");
    sa1_character_destroy(ctx, id);
}

static void t_stage(sa1_context *ctx)
{
    static const struct { int32_t level, act, world, on; uint32_t want; const char *what; } cs[5] = {
        { 3, 1, 1, 1, 1u, "G: stage 0x0301, ON at create" },
        { 3, 1, 1, 0, 0u, "H: stage 0x0301, OFF at create" },
        { -1, 0, 0, 1, 1u, "I: stage 0x0100, a world built later, ON before the first tick" },
        { 3, 1, 0, 0, 0u, "J: stage 0x0301, a world built later, OFF before the first tick" },
        { -1, 0, 0, 0, 0u, "J2: stage 0x0100, a world built later, OFF before the first tick" },
    };
    int c;
    printf("G-J: the game's own gate\n");
    for (c = 0; c < 5; c++) {
        sa1_character_id id = mk(ctx, SA1_CHAR_AMY, cs[c].level, cs[c].act, cs[c].world);
        uint32_t i, after1 = 0, drawn = 0, maxn = 0;
        rendc rc;
        if (id == 0) {
            g_fail++;
            continue;
        }
        if (sa1_character_set_companion(ctx, id, SA1_COMPANION_BIRD, cs[c].on) != SA1_OK) {
            CHECK(0, "%s: set_companion: %s", cs[c].what, sa1_last_error(ctx));
            sa1_character_destroy(ctx, id);
            continue;
        }
        for (i = 0; i < 31; i++) {
            uint32_t n;
            if (tick(ctx, id, 0, 0.0f, i < 15 ? 1.0f : 0.0f)) {
                g_fail++;
                break;
            }
            n = nbirds(ctx, id);
            maxn = n > maxn ? n : maxn;
            if (i == 0) {
                after1 = n;
            }
            rend_count(ctx, id, 1.0f, &rc);
            drawn += rc.draws != 0;
        }
        CHECK(nbirds(ctx, id) == cs[c].want && maxn <= 1u && (cs[c].want || drawn == 0),
              "%s: %u bird(s) after 31 pairs (want %u; after 1 pair %u, at most %u), bird draws in %u pairs", cs[c].what,
              (unsigned)nbirds(ctx, id), (unsigned)cs[c].want, (unsigned)after1, (unsigned)maxn, (unsigned)drawn);
        sa1_character_destroy(ctx, id);
    }
}

int main(int argc, char **argv)
{
    const char *gdi = NULL;
    sa1_context *ctx = NULL;
    sa1_host h;
    sa1_coords co;
    sa1_load_options lo;
    int a;
    for (a = 1; a < argc; a++) {
        if (!strcmp(argv[a], "--verbose")) {
            g_verbose = 1;
        } else {
            gdi = argv[a];
        }
    }
    if (gdi == NULL) {
        gdi = getenv("SA1_TEST_GDI");
    }
    if (gdi == NULL) {
        printf("usage: companion_test [--verbose] GDI\n");
        return 2;
    }
    sa1_host_defaults(&h);
    h.log = log_cb;
    h.log_level = SA1_LOG_WARN;
    h.alloc = count_alloc;
    h.free = count_free;
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
    lo.characters = (1u << SA1_CHAR_AMY) | (1u << SA1_CHAR_SONIC);
    lo.flags = SA1_LOAD_DEFAULT;
    if (SA1_FAILED(sa1_load_disc(ctx, gdi, &lo)) || scene(ctx) != 0) {
        printf("context: %s\n", sa1_last_error(ctx));
        sa1_shutdown(ctx);
        return 1;
    }
    printf("libsa1 %s\n", sa1_version_string());
    t_abcde(ctx);
    t_refusals(ctx);
    t_stage(ctx);
    sa1_shutdown(ctx);
    CHECK(g_allocs == g_frees, "K: allocations balanced at sa1_shutdown (%ld allocs, %ld frees)", g_allocs, g_frees);
    printf("companion_test: %d ok, %d failed\n", g_ok, g_fail);
    return g_fail ? 1 : 0;
}
