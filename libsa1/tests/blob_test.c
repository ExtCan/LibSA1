/*
 * blob_test.c - the game's blob shadow in the render output (0.6.0 fixes: sadc docs/research/gsa1_060_shadow.md L3-L5
 * and its Review R.3.4 / R.3.5 / R.4; owner report on G-SA1 0.6.0: "characters have no shadows").
 *
 * Every character's shadow code (8C0925A0 -> 8C09247E; E-102: 8C09222C) draws Character_Shadow_Blob on sub step 0 of
 * every pair. libsa1 0.2 left that draw out of the render output ("hosts draw their own"); now it is kept and marked
 * SA1_DRAW_SHADOW, keeping SA1_DRAW_EFFECT (so the v0.1 / v0.2 mesh_out leaves it out with the effects). Its texture is
 * OBJ_REGULAR's "shadow" once the world sets the stage-start texlist and loads OBJ_REGULAR (L1 / L2, another agent's
 * part); before that it is untextured (SA1_DRAW_NO_TEXTURE) and this test says which it saw.
 *
 * Scene (the shadow research's, sadc build/research/shadow/shadow_probe.c): NATIVE coordinates (host = SA1 units), a
 * GRID floor of 100-unit quads at y = 0; characters start at (13.7, 2, 7.3), off the quads' vertices and diagonals.
 *   1. all six characters standing, 12 pairs: exactly one SA1_DRAW_SHADOW draw of 2 triangles (njDrawObject /
 *      njDrawModel, SOURCE_MODEL), BLEND | EFFECT | SHADOW, SA / ISA, the material's alpha 204 in its corners; its
 *      centre is (x, y_bottom + 0.3, z) of SUB STEP 0, the step that draws it (Review R.3.5: the state after the pair is
 *      sub step 1's; read by a wrapper around the world's step), E-102's at the floor + 0.3; flat and turned with the
 *      character.
 *   2. Sonic jumping: at alpha 0.5 the blob's centre and size lie between the two pairs' (it is interpolated like every
 *      drawn model; it takes no anchor).
 *   3. Sonic dropped from 150 units: in the first pair whose capture holds the blob again (it came back into the probe's
 *      reach), the blob of the last pair only is not moved with the root vertically (Review R.3.4): at alpha 0 and 0.5 its
 *      corners have the last pair's heights while the root is up to a pair's fall above.
 *   4. hidden pairs (the zero screen: the visibility test never passes, the render output is the CPU pose): the blob is
 *      still drawn, after the pose (L4).
 *   5. the tick's mesh_out: without the render streams (v0.1) the blob's triangles are left out; with them (archive and
 *      draw_flags) its 2 triangles carry SA1_DRAW_SHADOW.
 *
 *   blob_test [--verbose] GDI          (build / run: tests/build_blob.py --run)
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
#include "sa1_capture.h"

static int g_fail, g_verbose;

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

#define PWP_SHADOW 0x1DCu   /* playerwk.shadow (include/sa1/player.h): angx, angz, Attr_top, Attr_bottom, y_top, y_bottom */

static void log_cb(void *user, int32_t level, const char *msg)
{
    (void)user;
    if (level >= SA1_LOG_WARN)
        printf("    [libsa1 %s] %s\n", level >= SA1_LOG_ERROR ? "error" : "warn", msg);
}

/* ---- the scene ---- */
typedef struct tbuf {
    sa1_collision_tri t[512];
    uint32_t n;
} tbuf;

static void tri(tbuf *b, const float a[3], const float c[3], const float d[3])
{
    sa1_collision_tri *t = &b->t[b->n++];
    memset(t, 0, sizeof *t);
    memcpy(t->v[0], a, 12);
    memcpy(t->v[1], c, 12);
    memcpy(t->v[2], d, 12);
    t->surface = SA1_SURFACE_SOLID;
}

static void scene(tbuf *b)
{
    int i, j;
    b->n = 0;
    for (i = -6; i < 6; i++) {
        for (j = -6; j < 6; j++) {
            float x0 = 100.0f * (float)i, x1 = x0 + 100.0f, z0 = 100.0f * (float)j, z1 = z0 + 100.0f;
            float a[3] = { x0, 0, z0 }, c[3] = { x0, 0, z1 }, d[3] = { x1, 0, z1 }, e[3] = { x1, 0, z0 };
            tri(b, a, c, d);
            tri(b, a, d, e);
        }
    }
}

/* ---- sub step 0's values: a wrapper around the world's step ---- */
static sa1g_step_fn g_orig_step;
static void *g_orig_user;
static float g_pos0[3], g_ybottom0;
static int g_have0;

static void wrap_step(sa1g_world *w, int32_t sub, void *user)
{
    sa1g_player_ptrs pp;
    (void)user;
    g_orig_step(w, sub, g_orig_user);
    if (sub != 0)
        return;
    g_have0 = 0;
    sa1g_world_player(w, &pp);
    if (pp.twp && pp.pwp) {
        const uint8_t *t = (const uint8_t *)sa1g_world_ptr(w, pp.twp + 0x20u);
        const uint8_t *s = (const uint8_t *)sa1g_world_ptr(w, pp.pwp + PWP_SHADOW + 0x14u);
        if (t && s) {
            memcpy(g_pos0, t, sizeof g_pos0);
            memcpy(&g_ybottom0, s, sizeof g_ybottom0);
            g_have0 = 1;
        }
    }
}

static void wrap(sa1_context *ctx, sa1_character_id id)
{
    sa1g_world *w = sa1_character_world(ctx, id);
    if (w && w->step != wrap_step) {
        g_orig_step = w->step;
        g_orig_user = w->user;
        w->step = wrap_step;
    }
}

static void unwrap(sa1_context *ctx, sa1_character_id id)
{
    sa1g_world *w = sa1_character_world(ctx, id);
    if (w && w->step == wrap_step)
        w->step = g_orig_step;
}

/* ---- the blob in a render output ---- */
typedef struct blob {
    int n;                          /* SA1_DRAW_SHADOW draws */
    const sa1_draw *d;
    double c[3];                    /* centre: the mean of the distinct corners */
    double ext[2];                  /* the quad's two edge lengths */
    double ymin, ymax;              /* corner heights */
    uint32_t nc;                    /* distinct corners */
    float corner[8][3];
    int alpha204, after_body;
} blob;

static void blob_of(const sa1_render *r, blob *b)
{
    uint32_t d, t, k, body = 0, i;
    memset(b, 0, sizeof *b);
    b->ymin = 1e30;
    b->ymax = -1e30;
    b->alpha204 = 1;
    for (d = 0; d < r->num_draws; d++) {
        if (r->draws[d].flags & SA1_DRAW_SHADOW) {
            b->n++;
            b->d = &r->draws[d];
        } else if (r->draws[d].source == SA1_SOURCE_CHARACTER && r->draws[d].archive == 0 && r->draws[d].order > body) {
            /* the body: the character renderer with the model's own textures (Big's fishing gear, drawn after the blob
               with its own texlist, is not the body) */
            body = r->draws[d].order;
        }
    }
    if (b->n != 1 || b->d->prim != SA1_PRIM_TRIANGLES)
        return;
    b->after_body = b->d->order > body;
    for (t = b->d->first; t < b->d->first + b->d->count; t++) {
        for (k = 0; k < 3; k++) {
            const float *p = r->positions + 9u * t + 3u * k;
            int seen = 0;
            b->alpha204 &= r->colors[12u * t + 4u * k + 3u] == 204u;
            for (i = 0; i < b->nc; i++)
                seen |= memcmp(b->corner[i], p, 12) == 0;
            if (!seen && b->nc < 8u)
                memcpy(b->corner[b->nc++], p, 12);
        }
    }
    for (i = 0; i < b->nc; i++) {
        for (k = 0; k < 3; k++)
            b->c[k] += b->corner[i][k] / (double)b->nc;
        b->ymin = fmin(b->ymin, b->corner[i][1]);
        b->ymax = fmax(b->ymax, b->corner[i][1]);
    }
    if (b->nc == 4) {
        /* corners 0 1 3 / 0 3 2 of the model: in the emitted order, the edges from the first corner to its two
           neighbours are the two sides (the diagonal is the longest) */
        double l[3];
        for (i = 1; i < 4; i++) {
            double dx = b->corner[i][0] - b->corner[0][0], dy = b->corner[i][1] - b->corner[0][1];
            double dz = b->corner[i][2] - b->corner[0][2];
            l[i - 1] = sqrt(dx * dx + dy * dy + dz * dz);
        }
        if (l[0] >= l[1] && l[0] >= l[2]) {
            b->ext[0] = l[1];
            b->ext[1] = l[2];
        } else if (l[1] >= l[2]) {
            b->ext[0] = l[0];
            b->ext[1] = l[2];
        } else {
            b->ext[0] = l[0];
            b->ext[1] = l[1];
        }
    }
}

static const char *blob_tex(const sa1_context *ctx, int32_t kind, const sa1_draw *d, char *buf, size_t n)
{
    sa1_texture_info ti;
    const char *an = NULL;
    uint32_t na = 0;
    if (d->archive < 0) {
        snprintf(buf, n, "untextured%s", (d->flags & SA1_DRAW_NO_TEXTURE) ? " (NO_TEXTURE)" : "");
        return buf;
    }
    memset(&ti, 0, sizeof ti);
    ti.struct_size = sizeof ti;
    sa1_texture_archive(ctx, kind, (uint32_t)d->archive, &an, &na);
    if (sa1_texture_get(ctx, kind, (uint32_t)d->archive, (uint32_t)d->texture, &ti) == SA1_OK)
        snprintf(buf, n, "%s:%s", an ? an : "?", ti.name ? ti.name : "?");
    else
        snprintf(buf, n, "archive %d texture %d", (int)d->archive, (int)d->texture);
    return buf;
}

static sa1_context *mk_ctx(const char *gdi)
{
    static tbuf B;
    static const int32_t kinds[] = { SA1_CHAR_SONIC, SA1_CHAR_TAILS, SA1_CHAR_KNUCKLES, SA1_CHAR_AMY, SA1_CHAR_GAMMA,
                                     SA1_CHAR_BIG };
    sa1_context *ctx = NULL;
    sa1_host h;
    sa1_load_options lo;
    sa1_coords co;
    uint32_t k;
    sa1_host_defaults(&h);
    h.log = log_cb;
    h.log_level = SA1_LOG_WARN;
    if (sa1_init(&h, &ctx) != SA1_OK)
        return NULL;
    memset(&co, 0, sizeof co);
    co.struct_size = sizeof co;
    sa1_coords_preset(SA1_COORDS_PRESET_NATIVE, &co);
    sa1_set_coords(ctx, &co);
    memset(&lo, 0, sizeof lo);
    lo.struct_size = sizeof lo;
    for (k = 0; k < sizeof kinds / sizeof kinds[0]; k++)
        lo.characters |= 1u << kinds[k];
    lo.flags = SA1_LOAD_DEFAULT;
    scene(&B);
    if (SA1_FAILED(sa1_load_disc(ctx, gdi, &lo)) || sa1_collision_set_static(ctx, B.t, B.n) != SA1_OK) {
        printf("  context: %s\n", sa1_last_error(ctx));
        sa1_shutdown(ctx);
        return NULL;
    }
    return ctx;
}

static sa1_character_id mk_char(sa1_context *ctx, int32_t kind, float y)
{
    sa1_character_desc d;
    sa1_character_id id = 0;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.kind = kind;
    d.flags = SA1_CREATE_WORLD;
    d.position[0] = 13.7f;
    d.position[1] = y;
    d.position[2] = 7.3f;
    d.yaw = 0.6f;
    if (SA1_FAILED(sa1_character_create_ex(ctx, &d, &id))) {
        printf("  create %s: %s\n", sa1_character_name(kind), sa1_last_error(ctx));
        return 0;
    }
    return id;
}

static int tick(sa1_context *ctx, sa1_character_id id, uint32_t buttons, sa1_mesh *mesh)
{
    sa1_input in;
    memset(&in, 0, sizeof in);
    in.struct_size = sizeof in;
    in.buttons = buttons;
    in.camera_yaw = 0.0f;
    if (sa1_character_tick(ctx, id, &in, 1.0f / 30.0f, NULL, mesh) != SA1_OK) {
        printf("  tick: %s\n", sa1_last_error(ctx));
        return -1;
    }
    return 0;
}

/* 1. standing */
static void test_standing(sa1_context *ctx)
{
    static const int32_t kinds[] = { SA1_CHAR_SONIC, SA1_CHAR_TAILS, SA1_CHAR_KNUCKLES, SA1_CHAR_AMY, SA1_CHAR_GAMMA,
                                     SA1_CHAR_BIG };
    uint32_t k, i;
    printf("1. standing on the floor, 12 pairs:\n");
    for (k = 0; k < sizeof kinds / sizeof kinds[0]; k++) {
        sa1_character_id id = mk_char(ctx, kinds[k], 2.0f);
        const sa1_render *r = NULL;
        blob b;
        char tex[64];
        double ey, dc, side_ok;
        int ok = id != 0;
        if (ok)
            wrap(ctx, id);
        for (i = 0; ok && i < 12; i++)
            ok = tick(ctx, id, 0u, NULL) == 0;
        if (!ok || sa1_character_render(ctx, id, 1.0f, &r) != SA1_OK) {
            CHECK(0, "%s: character / ticks / render", sa1_character_name(kinds[k]));
            continue;
        }
        blob_of(r, &b);
        if (b.n != 1) {
            CHECK(0, "%s: %d SA1_DRAW_SHADOW draws in the render output (1 expected; libsa1 0.2: 0, the blob left out)",
                  sa1_character_name(kinds[k]), b.n);
            sa1_character_destroy(ctx, id);
            continue;
        }
        /* the game's placement on sub step 0: (pos.x, y_bottom + 0.3, pos.z); E-102 (8C09222C): the floor + 0.3 */
        ey = (kinds[k] == SA1_CHAR_GAMMA ? 0.0 : (double)g_ybottom0) + 0.3;
        dc = sqrt((b.c[0] - g_pos0[0]) * (b.c[0] - g_pos0[0]) + (b.c[1] - ey) * (b.c[1] - ey) +
                  (b.c[2] - g_pos0[2]) * (b.c[2] - g_pos0[2]));
        side_ok = b.ext[0] > 1.0 && b.ext[1] > 1.0 &&
                  (fabs(b.ext[0] / b.ext[1] - 1.25) < 0.01 || fabs(b.ext[1] / b.ext[0] - 1.25) < 0.01);
        CHECK(b.d->count == 2 && b.d->source == SA1_SOURCE_MODEL &&
                  (b.d->flags & (SA1_DRAW_BLEND | SA1_DRAW_EFFECT | SA1_DRAW_SHADOW)) ==
                      (SA1_DRAW_BLEND | SA1_DRAW_EFFECT | SA1_DRAW_SHADOW) &&
                  b.d->blend_src == SA1_BLEND_SRC_ALPHA && b.d->blend_dst == SA1_BLEND_INV_SRC_ALPHA && b.alpha204 &&
                  b.after_body && b.nc == 4 && g_have0 && dc < 1e-3 && b.ymax - b.ymin < 1e-3 && side_ok,
              "%s: 1 blob draw of %u triangles (SOURCE_MODEL, flags 0x%04X, blend %u/%u, corner alpha 204 %s, after the "
              "body %s), centre (%.3f %.3f %.3f) = sub step 0's (%.3f, y_bottom %.3f + 0.3, %.3f) within %.5f, flat "
              "(%.5f), %.3f x %.3f units (10s x 8s); texture %s", sa1_character_name(kinds[k]), (unsigned)b.d->count,
              (unsigned)b.d->flags, (unsigned)b.d->blend_src, (unsigned)b.d->blend_dst, b.alpha204 ? "yes" : "no",
              b.after_body ? "yes" : "no", b.c[0], b.c[1], b.c[2], g_pos0[0], ey - 0.3, g_pos0[2], dc, b.ymax - b.ymin,
              b.ext[0], b.ext[1], blob_tex(ctx, kinds[k], b.d, tex, sizeof tex));
        unwrap(ctx, id);
        sa1_character_destroy(ctx, id);
    }
}

/* 2. jumping: the blob is interpolated between the pairs */
static void test_jump(sa1_context *ctx)
{
    sa1_character_id id = mk_char(ctx, SA1_CHAR_SONIC, 2.0f);
    uint32_t i, pairs = 0, between = 0, shrank = 0;
    double smin = 1e30, s0 = 0.0;
    printf("2. Sonic jumping (the blob between the pairs):\n");
    for (i = 0; id && i < 12; i++)
        tick(ctx, id, 0u, NULL);
    for (i = 0; id && i < 24; i++) {
        const sa1_render *r = NULL;
        blob b0, b5, b1;
        if (tick(ctx, id, i < 10 ? SA1_BUTTON_JUMP : 0u, NULL) != 0)
            break;
        if (sa1_character_render(ctx, id, 0.0f, &r) != SA1_OK)
            break;
        blob_of(r, &b0);
        sa1_character_render(ctx, id, 0.5f, &r);
        blob_of(r, &b5);
        sa1_character_render(ctx, id, 1.0f, &r);
        blob_of(r, &b1);
        if (b0.n != 1 || b5.n != 1 || b1.n != 1 || b0.nc != 4 || b5.nc != 4 || b1.nc != 4)
            continue;
        if (i == 0)
            s0 = b1.ext[0];
        smin = fmin(smin, b1.ext[0]);
        shrank += b1.ext[0] < b0.ext[0] - 1e-4;
        pairs++;
        {
            int k, ok = 1;
            for (k = 0; k < 3; k++)
                ok &= b5.c[k] >= fmin(b0.c[k], b1.c[k]) - 1e-4 && b5.c[k] <= fmax(b0.c[k], b1.c[k]) + 1e-4;
            ok &= b5.ext[0] >= fmin(b0.ext[0], b1.ext[0]) - 1e-4 && b5.ext[0] <= fmax(b0.ext[0], b1.ext[0]) + 1e-4;
            between += ok;
            if (g_verbose)
                printf("    pair %2u: size %.4f / %.4f / %.4f, centre y %.4f / %.4f / %.4f%s\n", (unsigned)i, b0.ext[0],
                       b5.ext[0], b1.ext[0], b0.c[1], b5.c[1], b1.c[1], ok ? "" : "  NOT BETWEEN");
        }
    }
    CHECK(pairs >= 20 && between == pairs && shrank > 0 && smin < s0 - 0.05,
          "Sonic's jump: in %u of %u pairs the blob at alpha 0.5 lies between the two pairs' (centre and size); it shrinks "
          "with the height (%.3f -> %.3f units at the top), as the game's 8C09247E does", (unsigned)between,
          (unsigned)pairs, s0, smin);
    if (id)
        sa1_character_destroy(ctx, id);
}

/* 3. a blob of the last pair only is not moved with the root vertically */
static void test_reappear(sa1_context *ctx)
{
    sa1_character_id id = mk_char(ctx, SA1_CHAR_SONIC, 150.0f);
    uint32_t i, had = 0, found = 0;
    printf("3. Sonic dropped from 150 units (the blob comes back into the probe's reach):\n");
    for (i = 0; id && i < 60 && !found; i++) {
        const sa1_render *r = NULL;
        blob b0, b5, b1;
        float root0[3], root1[3];
        if (tick(ctx, id, 0u, NULL) != 0)
            break;
        sa1_character_render(ctx, id, 1.0f, &r);
        blob_of(r, &b1);
        memcpy(root1, r->position, sizeof root1);
        if (b1.n == 0) {
            had = 1;
            continue;
        }
        if (!had)
            continue;           /* the blob was there from the start: no reappearance yet */
        found = 1;
        sa1_character_render(ctx, id, 0.0f, &r);
        blob_of(r, &b0);
        memcpy(root0, r->position, sizeof root0);
        sa1_character_render(ctx, id, 0.5f, &r);
        blob_of(r, &b5);
        CHECK(b0.n == 1 && b5.n == 1 && b0.nc == 4 && b5.nc == 4 && fabs(b0.ymin - b1.ymin) < 1e-4 &&
                  fabs(b0.ymax - b1.ymax) < 1e-4 && fabs(b5.ymin - b1.ymin) < 1e-4 && root0[1] - root1[1] > 1.0f &&
                  fabs(b0.c[0] - b1.c[0] - (root0[0] - root1[0])) < 1e-3 &&
                  fabs(b0.c[2] - b1.c[2] - (root0[2] - root1[2])) < 1e-3,
              "pair %u (root %.2f units above the floor): the blob of the last pair only stays at its height %.4f at "
              "alpha 0 (%.4f) and 0.5 (%.4f) while the root is %.2f units higher at alpha 0 (moved with the root "
              "horizontally only)", (unsigned)i, root1[1], b1.c[1], b0.n ? b0.c[1] : -1.0, b5.n ? b5.c[1] : -1.0,
              root0[1] - root1[1]);
    }
    if (!found)
        CHECK(0, "Sonic dropped from 150 units: no pair where the blob came back (had none: %u)", (unsigned)had);
    if (id)
        sa1_character_destroy(ctx, id);
}

/* 4. hidden pairs (the zero screen of the old references, tests/world_replay.c wr_screen_of) */
static void test_hidden(sa1_context *ctx)
{
    static const uint32_t words[] = { 0x8C88F5E4u, 0x8C88F5E8u, 0x8C88F5ECu, 0x8C88F600u, 0x8C88F604u, 0x8C88F608u,
                                      0x8C88F60Cu, 0x8C88F618u, 0x8C88F61Cu, 0x8C88F620u, 0x8C88F624u, 0x8C88F628u,
                                      0x8C88F644u, 0x8C88F648u };
    sa1_character_id id = mk_char(ctx, SA1_CHAR_SONIC, 2.0f);
    sa1g_world *w;
    uint32_t i, k, hidden = 0, with = 0, last_ok = 0, pose = 0;
    printf("4. hidden pairs (zero screen: the CPU pose is drawn):\n");
    for (i = 0; id && i < 6; i++)
        tick(ctx, id, 0u, NULL);
    w = id ? sa1_character_world(ctx, id) : NULL;
    for (k = 0; w && k < sizeof words / sizeof words[0]; k++)
        sa1g_world_wr32(w, words[k], 0u);
    for (i = 0; w && i < 10; i++) {
        const sa1_render *r = NULL;
        blob b;
        uint32_t d;
        if (tick(ctx, id, 0u, NULL) != 0)
            break;
        hidden += sa1_capture_hidden(sa1_character_capture(ctx, id)) != 0;
        sa1_character_render(ctx, id, -1.0f, &r);
        blob_of(r, &b);
        with += b.n == 1;
        last_ok += b.n == 1 && b.d == &r->draws[r->num_draws - 1u];
        for (d = 0; d < r->num_draws; d++)
            pose += r->draws[d].source == SA1_SOURCE_POSE;
    }
    CHECK(hidden == 10 && with == 10 && last_ok == 10 && pose > 0,
          "Sonic with the zero screen: %u of 10 pairs hidden (the CPU pose: %u pose draws), the blob drawn in %u, after "
          "the pose in %u", (unsigned)hidden, (unsigned)pose, (unsigned)with, (unsigned)last_ok);
    if (id)
        sa1_character_destroy(ctx, id);
}

/* 5. the tick's mesh_out */
static void test_mesh(sa1_context *ctx)
{
    sa1_character_id id = mk_char(ctx, SA1_CHAR_SONIC, 2.0f);
    sa1_mesh v1, full;
    static float pos1[9 * 8192], pos2[9 * 8192];
    static int16_t arc[8192], tex[8192];
    static uint32_t fl[8192];
    uint32_t i, shadow_tris = 0;
    int st1, st2;
    const sa1_render *r = NULL;
    blob b;
    printf("5. sa1_character_tick's mesh_out:\n");
    for (i = 0; id && i < 12; i++)
        tick(ctx, id, 0u, NULL);
    memset(&v1, 0, sizeof v1);
    v1.struct_size = (uint32_t)offsetof(sa1_mesh, archive);
    v1.max_triangles = 8192;
    v1.position = pos1;
    memset(&full, 0, sizeof full);
    full.struct_size = sizeof full;
    full.max_triangles = 8192;
    full.position = pos2;
    full.archive = arc;
    full.texture = tex;
    full.draw_flags = fl;
    st1 = id ? tick(ctx, id, 0u, &v1) : -1;
    sa1_character_render(ctx, id, 1.0f, &r);
    blob_of(r, &b);
    st2 = id ? tick(ctx, id, 0u, &full) : -1;
    for (i = 0; i < full.num_triangles; i++)
        shadow_tris += (fl[i] & SA1_DRAW_SHADOW) != 0;
    CHECK(st1 == 0 && st2 == 0 && b.n == 1 && v1.num_triangles + 2u <= r->num_triangles && shadow_tris == 2,
          "mesh_out without the render streams (v0.1): %u triangles, the blob left out with the effects (the render "
          "output has %u); with them: %u triangles, %u of them SA1_DRAW_SHADOW", (unsigned)v1.num_triangles,
          (unsigned)r->num_triangles, (unsigned)full.num_triangles, (unsigned)shadow_tris);
    if (id)
        sa1_character_destroy(ctx, id);
}

int main(int argc, char **argv)
{
    const char *gdi = getenv("SA1_TEST_GDI");
    sa1_context *ctx;
    int i;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--verbose"))
            g_verbose = 1;
        else
            gdi = argv[i];
    }
    if (!gdi || !*gdi) {
        printf("usage: blob_test [--verbose] GDI (or SA1_TEST_GDI)\n");
        return 2;
    }
    printf("blob_test: the game's blob shadow in the render output (gsa1_060_shadow.md L3-L5), NATIVE GRID floor:\n");
    ctx = mk_ctx(gdi);
    if (!ctx) {
        CHECK(0, "context");
        return 1;
    }
    test_standing(ctx);
    test_jump(ctx);
    test_reappear(ctx);
    test_hidden(ctx);
    test_mesh(ctx);
    sa1_shutdown(ctx);
    printf("blob_test: %s (%d failed)\n", g_fail ? "FAIL" : "ok", g_fail);
    return g_fail ? 1 : 0;
}
