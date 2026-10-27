/*
 * pairs_test.c - a tick that runs several pairs renders what one-pair ticks render (sadc docs/research/gsa1_060_pops.md
 * F1 and its Review R.3.1 / R.5.2; plan gsa1_rounds_plan.md 14.1d "pops").
 *
 * A host ticks once per frame: a frame of 33 ms or more runs k >= 2 pairs in one sa1_character_tick. libsa1 0.2 before
 * the 0.6.0 fixes captured only the last pair of such a tick, so the render output interpolated between a capture k
 * pairs old and the new one with an alpha meant for one pair: the drawn root (the camera's centre) and the body lagged up
 * to (k - 1)(1 - alpha) pairs behind the game and lurched forward in the next frames. The fix captures every pair of the
 * tick (with that pair's state for the motion hints and the CPU pose), so the output lies between the last two pairs.
 *
 * Two characters of the same kind on Emerald Coast act 1's own collision (DISC) run the same per-pair inputs: A ticks
 * with frame times of 1/60 s and long frames (38, 62, 92, 145 and 250 ms; a 62 ms frame every 0.2 s while the spin dash
 * charges: a steady action whose motion advance has no fixed rate, where the motion hint is taken from the pair before),
 * renders with alpha -1 (its accumulator) after every tick; B ticks one pair at a time (dt 1/30) until it has run as
 * many pairs as A and renders at A's alpha. Every frame, the WHOLE render output (positions, normals, UVs, colours,
 * draws, sprites, line vertices, root position and yaw, flags) must be bit-identical, and so must the state
 * (sa1_state_ex: the world runs the same steps either way). Before the fix the position check fails by up to tens of
 * HU (root and body); "F1 as written" in the doc (without the Review's refresh of the state between the captures)
 * fails inside the spin dash charge.
 *
 *   pairs_test [--seconds S] [--trace] GDI          (build / run: tests/build_pairs.py --run)
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libsa1.h"

static int g_fail, g_trace;

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

static void log_cb(void *user, int32_t level, const char *msg)
{
    (void)user;
    if (level >= SA1_LOG_WARN)
        printf("    [libsa1 %s] %s\n", level >= SA1_LOG_ERROR ? "error" : "warn", msg);
}

static sa1_context *mk_ctx(const char *gdi, int32_t kind)
{
    sa1_context *ctx = NULL;
    sa1_host h;
    sa1_load_options lo;
    sa1_collision_static col;
    sa1_host_defaults(&h);
    h.log = log_cb;
    h.log_level = SA1_LOG_WARN;
    if (sa1_init(&h, &ctx) != SA1_OK)
        return NULL;
    memset(&lo, 0, sizeof lo);
    lo.struct_size = sizeof lo;
    lo.characters = 1u << kind;
    lo.flags = SA1_LOAD_DEFAULT;
    memset(&col, 0, sizeof col);
    col.struct_size = sizeof col;
    col.mode = SA1_COLLISION_MODE_DISC;
    col.disc_level = 1;
    col.disc_act = 0;
    if (SA1_FAILED(sa1_load_disc(ctx, gdi, &lo)) || sa1_collision_set_static_ex(ctx, &col) != SA1_OK) {
        printf("  context: %s\n", sa1_last_error(ctx));
        sa1_shutdown(ctx);
        return NULL;
    }
    return ctx;
}

static sa1_character_id mk_char(sa1_context *ctx, int32_t kind)
{
    sa1_character_desc d;
    sa1_character_id id = 0;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.kind = kind;
    d.flags = SA1_CREATE_WORLD | SA1_CREATE_START_TABLE;
    if (SA1_FAILED(sa1_character_create_ex(ctx, &d, &id))) {
        printf("  create %s: %s\n", sa1_character_name(kind), sa1_last_error(ctx));
        return 0;
    }
    return id;
}

/* the input of pair p (it depends on the pairs run, not on the frame times, so both characters get the same input in
 * every pair): run, stop, charge a spin dash, release, run and jump, run in a curve, stop, charge again, run */
static void script(uint32_t p, sa1_input *in)
{
    double g = (double)p / 30.0;
    in->stick_x = 0.0f;
    in->stick_y = 0.0f;
    in->buttons = 0u;
    in->camera_yaw = 0.0f;
    if (g >= 1.0 && g < 3.0) {
        in->stick_y = 1.0f;
    } else if (g >= 4.5 && g < 6.5) {
        in->buttons = SA1_BUTTON_ACTION;            /* spin dash: charging */
    } else if (g >= 7.5 && g < 9.5) {
        in->stick_y = 1.0f;
        if (g >= 8.5 && g < 8.6)
            in->buttons = SA1_BUTTON_JUMP;
    } else if (g >= 9.5 && g < 11.0) {
        in->stick_y = 1.0f;
        in->stick_x = 0.7f;
    } else if (g >= 12.5 && g < 14.5) {
        in->buttons = SA1_BUTTON_ACTION;
    } else if (g >= 15.5) {
        in->stick_y = 1.0f;
        in->stick_x = -0.6f;
    }
}

/* frame times: 1/60 s, a long frame every 2 s (38, 62, 92, 145, 250 ms in turn), and while the spin dash charges (wall
 * time 4.6-6.4 s and 12.6-14.4 s) a 62 ms frame every 0.2 s and one of 145 ms */
typedef struct pacer {
    double t, next_long, next_charge;
    uint32_t k;
    int charge_long_done[2];
} pacer;

static float next_dt(pacer *p)
{
    static const float longs[5] = { 0.038f, 0.062f, 0.092f, 0.145f, 0.25f };
    float dt = 1.0f / 60.0f;
    int w = (p->t >= 4.6 && p->t < 6.4) ? 0 : (p->t >= 12.6 && p->t < 14.4) ? 1 : -1;
    if (p->t >= p->next_long) {
        dt = longs[p->k++ % 5u];
        p->next_long += 2.0;
    } else if (w >= 0 && !p->charge_long_done[w] && p->t >= (w ? 13.5 : 5.5)) {
        dt = 0.145f;
        p->charge_long_done[w] = 1;
    } else if (w >= 0 && p->t >= p->next_charge) {
        dt = 0.062f;
        p->next_charge = p->t + 0.2;
    }
    p->t += dt;
    return dt;
}

static uint32_t pairs_of(sa1_context *ctx, sa1_character_id id, sa1_state_ex *x)
{
    memset(x, 0, sizeof *x);
    x->struct_size = sizeof *x;
    sa1_character_state_ex(ctx, id, x);
    return (uint32_t)(x->step / 2u);
}

/* a copy of a render output (sa1_render is library-owned until the next call) */
typedef struct rcopy {
    sa1_render r;
    float *pos, *nrm, *uv;
    uint8_t *col;
    sa1_draw *draws;
    sa1_sprite *spr;
    sa1_line_vertex *lv;
} rcopy;

static void rcopy_free(rcopy *c)
{
    free(c->pos);
    free(c->nrm);
    free(c->uv);
    free(c->col);
    free(c->draws);
    free(c->spr);
    free(c->lv);
    memset(c, 0, sizeof *c);
}

static void *dup_bytes(const void *p, size_t n)
{
    void *q = malloc(n ? n : 1);
    if (q && n)
        memcpy(q, p, n);
    return q;
}

static void rcopy_of(const sa1_render *r, rcopy *c)
{
    rcopy_free(c);
    c->r = *r;
    c->pos = (float *)dup_bytes(r->positions, 9u * sizeof(float) * r->num_triangles);
    c->nrm = (float *)dup_bytes(r->normals, 9u * sizeof(float) * r->num_triangles);
    c->uv = (float *)dup_bytes(r->uvs, 6u * sizeof(float) * r->num_triangles);
    c->col = (uint8_t *)dup_bytes(r->colors, 12u * (size_t)r->num_triangles);
    c->draws = (sa1_draw *)dup_bytes(r->draws, sizeof(sa1_draw) * r->num_draws);
    c->spr = (sa1_sprite *)dup_bytes(r->sprites, sizeof(sa1_sprite) * r->num_sprites);
    c->lv = (sa1_line_vertex *)dup_bytes(r->line_vertices, sizeof(sa1_line_vertex) * r->num_line_vertices);
}

/* 1 when the two outputs are the same bit for bit (the generation counter aside); *dv: the largest position difference
 * of a vertex (when the topology is the same), *dr: of the root */
static int same_render(const rcopy *a, const sa1_render *b, double *dv, double *dr)
{
    uint32_t i, n9 = 9u * a->r.num_triangles;
    int same = 1;
    *dv = 0.0;
    *dr = 0.0;
    for (i = 0; i < 3; i++)
        *dr = fmax(*dr, fabs((double)a->r.position[i] - b->position[i]));
    if (a->r.num_triangles != b->num_triangles || a->r.num_draws != b->num_draws ||
        a->r.num_sprites != b->num_sprites || a->r.num_line_vertices != b->num_line_vertices) {
        *dv = 1e30;
        return 0;
    }
    for (i = 0; i < n9; i++)
        *dv = fmax(*dv, fabs((double)a->pos[i] - b->positions[i]));
    same &= memcmp(a->pos, b->positions, n9 * sizeof(float)) == 0;
    same &= memcmp(a->nrm, b->normals, n9 * sizeof(float)) == 0;
    same &= memcmp(a->uv, b->uvs, 6u * sizeof(float) * a->r.num_triangles) == 0;
    same &= memcmp(a->col, b->colors, 12u * (size_t)a->r.num_triangles) == 0;
    same &= memcmp(a->draws, b->draws, sizeof(sa1_draw) * a->r.num_draws) == 0;
    same &= memcmp(a->spr, b->sprites, sizeof(sa1_sprite) * a->r.num_sprites) == 0;
    same &= memcmp(a->lv, b->line_vertices, sizeof(sa1_line_vertex) * a->r.num_line_vertices) == 0;
    same &= memcmp(a->r.position, b->position, sizeof b->position) == 0;
    same &= memcmp(&a->r.yaw, &b->yaw, sizeof b->yaw) == 0;
    same &= memcmp(&a->r.alpha, &b->alpha, sizeof b->alpha) == 0;
    same &= a->r.interpolated == b->interpolated && a->r.flags == b->flags;
    return same;
}

static void run_kind(const char *gdi, int32_t kind, double seconds)
{
    sa1_context *ctx = mk_ctx(gdi, kind);
    sa1_character_id ida = ctx ? mk_char(ctx, kind) : 0, idb = ctx ? mk_char(ctx, kind) : 0;
    pacer pc;
    rcopy ra;
    uint32_t frames = 0, compared = 0, identical = 0, multi = 0, multi_charge = 0, maxk = 0, state_diff = 0;
    uint32_t bad_ref = 0, failed = 0, first_bad = 0;
    double worst_v = 0.0, worst_r = 0.0, worst_t = 0.0;
    memset(&pc, 0, sizeof pc);
    pc.next_long = 2.0;
    memset(&ra, 0, sizeof ra);
    printf("%s: %.0f s, A ticks per frame (1/60 s with long frames), B one pair per tick in step:\n",
           sa1_character_name(kind), seconds);
    if (!ida || !idb) {
        CHECK(0, "%s: context / characters", sa1_character_name(kind));
        if (ctx)
            sa1_shutdown(ctx);
        return;
    }
    while (pc.t < seconds) {
        sa1_input in;
        sa1_state_ex xa, xb;
        const sa1_render *r = NULL;
        uint32_t pa0, pa, pb, ran;
        float dt;
        double dv, dr;
        int same;
        memset(&in, 0, sizeof in);
        in.struct_size = sizeof in;
        pa0 = pairs_of(ctx, ida, &xa);
        script(pa0, &in);
        dt = next_dt(&pc);
        if (sa1_character_tick(ctx, ida, &in, dt, NULL, NULL) != SA1_OK ||
            sa1_character_render(ctx, ida, -1.0f, &r) != SA1_OK) {
            printf("  A tick / render at %.3f s: %s\n", pc.t, sa1_last_error(ctx));
            failed = 1;
            break;
        }
        rcopy_of(r, &ra);
        pa = pairs_of(ctx, ida, &xa);
        ran = pa - pa0;
        multi += ran >= 2u;
        multi_charge += ran >= 2u && xa.mode == 4;
        if (ran > maxk)
            maxk = ran;
        /* B: one pair per tick, the same input, until it has run as many pairs */
        for (pb = pairs_of(ctx, idb, &xb); pb < pa;) {
            uint32_t pb0 = pb;
            if (sa1_character_tick(ctx, idb, &in, 1.0f / 30.0f, NULL, NULL) != SA1_OK) {
                printf("  B tick: %s\n", sa1_last_error(ctx));
                failed = 1;
                break;
            }
            pb = pairs_of(ctx, idb, &xb);
            bad_ref += pb != pb0 + 1u;
        }
        if (failed || sa1_character_render(ctx, idb, ra.r.alpha, &r) != SA1_OK) {
            failed = 1;
            break;
        }
        frames++;
        state_diff += memcmp(&xa, &xb, sizeof xa) != 0;
        if (pa >= 2u) {
            compared++;
            same = same_render(&ra, r, &dv, &dr);
            identical += same;
            if (!same && !first_bad)
                first_bad = frames;
            if (dv < 1e29 && dv > worst_v) {
                worst_v = dv;
                worst_t = pc.t;
            }
            worst_r = fmax(worst_r, dr);
            if (g_trace && (ran >= 2u || !same))
                printf("    t %7.3f dt %5.1f ms pairs %u alpha %.3f mode %d action %d: %s (root %.4f, vertices %.4f HU)\n",
                       pc.t, dt * 1000.0, (unsigned)ran, (double)ra.r.alpha, (int)xa.mode, (int)xa.action,
                       same ? "identical" : "DIFFERS", dr, dv);
        }
    }
    rcopy_free(&ra);
    CHECK(!failed && bad_ref == 0 && multi >= 6u && (kind != SA1_CHAR_SONIC || multi_charge >= 3u) && maxk >= 5u,
          "%s: %u frames, %u ticks of 2+ pairs (largest %u), %u of them while the spin dash charges (mode 4); the "
          "reference ran one pair per tick (%u ticks did not)", sa1_character_name(kind), (unsigned)frames,
          (unsigned)multi, (unsigned)maxk, (unsigned)multi_charge, (unsigned)bad_ref);
    CHECK(!failed && state_diff == 0, "%s: the state (sa1_state_ex) is the same bit for bit in all %u frames: the world "
          "runs the same steps whether a tick runs one pair or several (%u differ)", sa1_character_name(kind),
          (unsigned)frames, (unsigned)state_diff);
    CHECK(!failed && compared > 0 && identical == compared,
          "%s: the whole render output (every vertex, normal, UV, colour, draw, sprite, line, the root and yaw) is "
          "bit-identical to one-pair ticking in %u of %u frames (first difference in frame %u; largest difference: "
          "root %.4f HU, a vertex %.4f HU at %.3f s)", sa1_character_name(kind), (unsigned)identical,
          (unsigned)compared, (unsigned)first_bad, worst_r, worst_v, worst_t);
    sa1_shutdown(ctx);
}

int main(int argc, char **argv)
{
    const char *gdi = getenv("SA1_TEST_GDI");
    double seconds = 16.5;
    int i;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--seconds") && i + 1 < argc)
            seconds = atof(argv[++i]);
        else if (!strcmp(argv[i], "--trace"))
            g_trace = 1;
        else
            gdi = argv[i];
    }
    if (!gdi || !*gdi) {
        printf("usage: pairs_test [--seconds S] [--trace] GDI (or SA1_TEST_GDI)\n");
        return 2;
    }
    printf("pairs_test: a tick of several pairs renders what one-pair ticks render (gsa1_060_pops.md F1)\n");
    run_kind(gdi, SA1_CHAR_SONIC, seconds);
    run_kind(gdi, SA1_CHAR_TAILS, 12.0);
    printf("pairs_test: %s (%d failed)\n", g_fail ? "FAIL" : "ok", g_fail);
    return g_fail ? 1 : 0;
}
