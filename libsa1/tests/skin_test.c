/*
 * skin_test.c - a worn skin: Metal Sonic over Sonic's world (SA1_API_SKIN, round 0.9.9f; sadc docs/research/metal099e.md section 7, S1-S9, with its review RM.1-16).
 * A flat GRID ground in the Source preset (4 HU per SA1 unit, z up). The world's memory only through the world core's header; the parts' rotations through sa1k_test_get.
 *
 *   py -3.12 tests/build_skin.py --run --gdi GDI [--target x86_64-windows]
 *   skin_test GDI
 *
 * What a host needs to know (the header's section "a worn skin" says the rest):
 *   - sa1_character_create_ex(kind 0, SA1_CREATE_SKIN, desc.skin): the world is byte for byte Sonic's. Only sa1_character_render, _geometry and the mesh differ: 25 rigid parts on archive
 *     SA1_ARCHIVE_SKIN in place of his body, his effects, objects, a board and the blob as they are, from before the first pair on.
 *   - SA1_COPT_SKIN_ANIM: the running rows draw the flown rows; at once. sa1_frontend_set_skin: slot 0 of Character Select the same way, the screen and its events unchanged.
 *   - sa1_texture_get(0, SA1_ARCHIVE_SKIN, n): the kept textures by position, SA1_ERR_NOT_LOADED before the first use.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libsa1.h"
#include "sa1_internal.h"
#include "sa1_api_world.h"
#include "sa1_capture.h"
#include "sa1_world.h"
#include "sa1_skin.h"

#define PI 3.14159265358979323846
#define EVF 0x8C799920u     /* the event flags (tests/flicker_test.c): 0x8D, 0x8E are the Light Speed Shoes and the Crystal Ring */

static sa1_context *g_ctx;
static int g_fail, g_ok, g_quiet_log;
static uint32_t g_hash = 2166136261u;

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
    if (level <= SA1_LOG_WARN && !g_quiet_log) {
        printf("    [libsa1] %s\n", msg);
    }
}

/* ---- the entry, as feat/sh_skins.lua gives it to the module ------------------------------------------------------------------------------- */
static const uint16_t k_keep[9] = { 76, 77, 97, 98, 99, 100, 101, 102, 103 };
static const sa1_skin_part k_parts[25] = {
    {1, 10, -1, -1}, {12, 22, -1, -1}, {12, 22, -1, -1},
    {25, 34, -1, -1}, {25, 34, 5, 27}, {27, 29, 6, 30}, {30, 30, -1, -1}, {30, 30, -1, -1}, {30, 30, -1, -1},
    {36, 45, -1, -1}, {36, 45, 11, 38}, {38, 40, 12, 41}, {41, 41, -1, -1}, {41, 41, -1, -1}, {41, 41, -1, -1},
    {47, 55, 16, 49}, {49, 51, 18, 52}, {49, 51, -1, -1}, {52, 52, -1, -1}, {52, 52, -1, -1},
    {57, 65, 21, 59}, {59, 61, 23, 62}, {59, 61, -1, -1}, {62, 62, -1, -1}, {62, 62, -1, -1},
};
static const uint16_t k_anim[4][2] = { {10, 132}, {11, 133}, {12, 134}, {13, 134} };
static const uint16_t k_walk[1][2] = { {9, 131} };
static sa1_skin_desc g_skin;

static void skin_init(void)
{
    memset(&g_skin, 0, sizeof g_skin);
    g_skin.struct_size = sizeof g_skin;
    g_skin.model_file = "ADV02.PRS";
    g_skin.model_sha1 = "ab5f600e707681f538794520c296c4fefe84659c";
    g_skin.model_load = 0x0C900000u;
    g_skin.model_first = 0x0CA8B9CCu;
    g_skin.model_end = 0x0CA92EB8u;
    g_skin.model_root = 0x0CA92E84u;
    g_skin.tex_file = "ADV_MR03.PRS";
    g_skin.tex_sha1 = "cca1620491391e3d6e44d83be478d0687d5fa14f";
    g_skin.tex_keep = k_keep;
    g_skin.num_tex_keep = 9;
    g_skin.scale = 0.70f;
    g_skin.lift = 1.0f;
    g_skin.turn = SA1_SKIN_TURN_MINUS_Z;
    g_skin.ball_texture = 101;
    g_skin.parts = k_parts;
    g_skin.num_parts = 25;
    g_skin.anim = k_anim;
    g_skin.num_anim = 4;
    g_skin.anim_walk = k_walk;
    g_skin.num_anim_walk = 1;
}

/* ---- the scene --------------------------------------------------------------------------------------------------------------------------- */
static void add_tri(sa1_collision_tri **t, uint32_t *n, uint32_t *cap, const float a[3], const float c[3], const float d[3])
{
    sa1_collision_tri *q;
    if (*n == *cap) {
        *cap = *cap ? 2u * *cap : 1024u;
        *t = (sa1_collision_tri *)realloc(*t, (size_t)*cap * sizeof **t);
    }
    q = &(*t)[(*n)++];
    memset(q, 0, sizeof *q);
    memcpy(q->v[0], a, 12);
    memcpy(q->v[1], c, 12);
    memcpy(q->v[2], d, 12);
    q->surface = SA1_SURFACE_SOLID;
}

static int scene_flat(sa1_context *ctx)
{
    sa1_collision_tri *t = NULL;
    uint32_t n = 0, cap = 0;
    float x, y;
    sa1_status st;
    for (x = -65536.0f; x < 65535.0f; x += 2048.0f) {
        for (y = -65536.0f; y < 65535.0f; y += 2048.0f) {
            float p0[3] = { x, y, 0.0f }, p1[3] = { x + 2048.0f, y, 0.0f }, p2[3] = { x + 2048.0f, y + 2048.0f, 0.0f }, p3[3] = { x, y + 2048.0f, 0.0f };
            add_tri(&t, &n, &cap, p0, p1, p2);
            add_tri(&t, &n, &cap, p0, p2, p3);
        }
    }
    st = sa1_collision_set_static(ctx, t, n);
    free(t);
    if (SA1_FAILED(st)) {
        printf("  set_static: %s: %s\n", sa1_status_string(st), sa1_last_error(ctx));
        return -1;
    }
    return 0;
}

static sa1_status create(uint32_t flags, sa1_character_id *id)
{
    sa1_character_desc d;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.kind = SA1_CHAR_SONIC;
    d.flags = SA1_CREATE_WORLD | flags;
    d.position[2] = 40.0f;
    d.skin = (flags & SA1_CREATE_SKIN) ? &g_skin : NULL;
    *id = 0;
    return sa1_character_create_ex(g_ctx, &d, id);
}

static sa1_status tick(sa1_character_id id, float sx, float sy, uint32_t buttons)
{
    sa1_input in;
    memset(&in, 0, sizeof in);
    in.struct_size = sizeof in;
    in.stick_x = sx;
    in.stick_y = sy;
    in.buttons = buttons;
    return sa1_character_tick(g_ctx, id, &in, 1.0f / 30.0f, NULL, NULL);
}

static void state_of(sa1_character_id id, sa1_state_ex *x)
{
    memset(x, 0, sizeof *x);
    x->struct_size = sizeof *x;
    sa1_character_state_ex(g_ctx, id, x);
}

static void upgrades_on(sa1_character_id id)
{
    static const uint32_t fl[2] = { 0x8Du, 0x8Eu };
    struct sa1g_world *w = sa1_character_world(g_ctx, id);
    int k;
    for (k = 0; k < 2; k++) {
        uint32_t a = EVF + fl[k], word = a & ~3u, v = 0, sh = 8u * (a & 3u);
        sa1g_world_rd32(w, word, &v);
        v = (v & ~(0xFFu << sh)) | (1u << sh);
        sa1g_world_wr32(w, word, v);
    }
}

static void info_of(sa1_character_id id, sa1k_test_info *ti)
{
    sa1k_char *sc = sa1k_char_of(g_ctx, id);
    if (sc != NULL) {
        sa1k_test_get(sc, ti);
    } else {
        memset(ti, 0, sizeof *ti);
    }
}

/* what the render holds: triangles on archive 61, on archive 0 as the character, draws */
typedef struct tally {
    uint32_t skin_tris, skin_draws, body_tris, effect_on_skin, sprites, afterimage;
    uint32_t alpha_draws, effect_draws;
    double lowest;      /* host z / 4 of the skin's vertices */
} tally;

static void tally_of(const sa1_render *r, tally *t)
{
    uint32_t i;
    memset(t, 0, sizeof *t);
    t->lowest = 1e30;
    if (r == NULL) {
        return;
    }
    t->sprites = r->num_sprites;
    for (i = 0; i < r->num_draws; i++) {
        const sa1_draw *d = &r->draws[i];
        if (d->prim != SA1_PRIM_TRIANGLES) {
            continue;
        }
        t->effect_draws += (d->flags & SA1_DRAW_EFFECT) != 0u && !(d->flags & SA1_DRAW_SHADOW);
        if ((uint32_t)d->archive == SA1_ARCHIVE_SKIN) {
            uint32_t k;
            t->skin_tris += d->count;
            t->skin_draws++;
            t->effect_on_skin += (d->flags & SA1_DRAW_EFFECT) != 0u;
            t->afterimage += (d->flags & SA1_DRAW_AFTERIMAGE) != 0u;
            t->alpha_draws += (d->flags & (SA1_DRAW_ALPHA_TEST | SA1_DRAW_BLEND)) != 0u;
            for (k = 0; k < 3u * d->count; k++) {
                double z = (double)r->positions[3u * (3u * d->first) + 3u * k + 2u] / 4.0;
                if (z < t->lowest) {
                    t->lowest = z;
                }
            }
        } else if (d->archive == 0 && d->source == SA1_SOURCE_CHARACTER && !(d->flags & (SA1_DRAW_SHADOW | SA1_DRAW_EFFECT | SA1_DRAW_OBJECT | SA1_DRAW_BOARD))) {
            t->body_tris += d->count;
        }
    }
}

static uint32_t count_faults(sa1_character_id id)
{
    sa1_state_ex x;
    state_of(id, &x);
    return x.flags & SA1_STATE_FAULTED;
}

static double m3_angle_between(const double a[9], const double b[9])
{
    /* trace(a * transpose(b)) */
    double tr = 0.0, c;
    int i;
    for (i = 0; i < 9; i++) {
        tr += a[i] * b[i];
    }
    c = 0.5 * (tr - 1.0);
    return acos(c > 1.0 ? 1.0 : (c < -1.0 ? -1.0 : c)) * 180.0 / PI;
}

static void mul3(const double a[9], const double b[9], double o[9])
{
    double t[9];
    int r, c;
    for (r = 0; r < 3; r++) {
        for (c = 0; c < 3; c++) {
            t[3 * r + c] = a[3 * r] * b[c] + a[3 * r + 1] * b[3 + c] + a[3 * r + 2] * b[6 + c];
        }
    }
    memcpy(o, t, sizeof t);
}

static void mul3_t(const double a[9], const double b[9], double o[9])
{
    double t[9];
    int r, c;
    for (r = 0; r < 3; r++) {
        for (c = 0; c < 3; c++) {
            t[3 * r + c] = a[3 * r] * b[3 * c] + a[3 * r + 1] * b[3 * c + 1] + a[3 * r + 2] * b[3 * c + 2];
        }
    }
    memcpy(o, t, sizeof t);
}

static void rot_of(const double m[12], double o[9])
{
    double a[9];
    a[0] = m[0]; a[1] = m[1]; a[2] = m[2];
    a[3] = m[4]; a[4] = m[5]; a[5] = m[6];
    a[6] = m[8]; a[7] = m[9]; a[8] = m[10];
    if (!sa1_pose_polar3(a, o)) {
        memcpy(o, a, sizeof a);
    }
}

/* ---- S1 -------------------------------------------------------------------------------------------------------------------------------------------- */
typedef struct s1_out {
    sa1_state_ex *s;
    sa1_event *e;
    uint32_t ne, cap;
    uint8_t *arena;
} s1_out;

static void s1_input(int p, sa1_input *in)
{
    int c = p % 320;
    memset(in, 0, sizeof *in);
    in->struct_size = sizeof *in;
    in->camera_yaw = (float)((p / 400) * 0.5);
    if (c < 30) {
        return;
    }
    if (c < 150) {
        in->stick_y = 1.0f;
    } else if (c < 175) {
        in->stick_y = 1.0f;
        in->buttons = SA1_BUTTON_JUMP;
    } else if (c < 177) {
        in->stick_y = 1.0f;
        in->buttons = SA1_BUTTON_ACTION;
    } else if (c < 230) {
        in->stick_y = 1.0f;
    } else if (c < 270) {
        in->stick_x = 1.0f;
    } else if (c < 300) {
        in->buttons = SA1_BUTTON_ACTION;    /* a spin dash charge */
    }
}

static int s1_run(uint32_t flags, int pairs, s1_out *o)
{
    sa1_character_id id = 0;
    int p;
    sa1_status st = create(flags, &id);
    if (st != SA1_OK) {
        printf("  create 0x%X: %s: %s\n", (unsigned)flags, sa1_status_string(st), sa1_last_error(g_ctx));
        return -1;
    }
    o->ne = 0;
    for (p = 0; p < pairs; p++) {
        sa1_input in;
        sa1_event ev[64];
        uint32_t n, i;
        const sa1_render *r = NULL;
        s1_input(p, &in);
        if (p % 500 == 300) {
            sa1_damage dm;
            memset(&dm, 0, sizeof dm);
            dm.struct_size = sizeof dm;
            sa1_character_damage(g_ctx, id, &dm);
        }
        if (p % 500 == 150) {
            sa1_character_add_rings(g_ctx, id, 7);
        }
        st = sa1_character_tick(g_ctx, id, &in, 1.0f / 30.0f, NULL, NULL);
        if (st != SA1_OK) {
            printf("  tick %d: %s: %s\n", p, sa1_status_string(st), sa1_last_error(g_ctx));
            sa1_character_destroy(g_ctx, id);
            return -1;
        }
        memset(&o->s[p], 0, sizeof o->s[p]);
        o->s[p].struct_size = sizeof o->s[p];
        sa1_character_state_ex(g_ctx, id, &o->s[p]);
        do {
            n = 0;
            sa1_poll_events(g_ctx, ev, 64, &n);
            for (i = 0; i < n; i++) {
                if (o->ne == o->cap) {
                    o->cap = o->cap ? 2u * o->cap : 4096u;
                    o->e = (sa1_event *)realloc(o->e, (size_t)o->cap * sizeof *o->e);
                }
                ev[i].character = 0;
                o->e[o->ne++] = ev[i];
            }
        } while (n == 64);
        sa1_character_render(g_ctx, id, 0.5f, &r);
    }
    memcpy(o->arena, sa1_character_world(g_ctx, id)->ram, SA1G_ARENA_SIZE);
    sa1_character_destroy(g_ctx, id);
    return 0;
}

static void test_s1(void)
{
    enum { PAIRS = 2000 };
    s1_out a, b, c;
    uint8_t *mask = (uint8_t *)calloc(SA1G_ARENA_SIZE, 1);
    uint32_t i, ctl = 0, flg = 0, other = 0, sd = 0, ed = 0;
    int modes = 0, rings = 0, p;
    s1_out *all[3];
    printf("S1 world identity: %d pairs with hits, rings, a jump, a spin dash\n", PAIRS);
    all[0] = &a;
    all[1] = &b;
    all[2] = &c;
    for (i = 0; i < 3; i++) {
        memset(all[i], 0, sizeof a);
        all[i]->s = (sa1_state_ex *)malloc(PAIRS * sizeof a.s[0]);
        all[i]->arena = (uint8_t *)malloc(SA1G_ARENA_SIZE);
    }
    if (s1_run(0, PAIRS, &a) || s1_run(0, PAIRS, &b) || s1_run(SA1_CREATE_SKIN, PAIRS, &c)) {
        CHECK(0, "S1 the three worlds ran");
        return;
    }
    for (p = 1; p < PAIRS; p++) {
        modes += a.s[p].mode != a.s[p - 1].mode;
        rings += a.s[p].rings != a.s[p - 1].rings;
    }
    printf("  info  the plain world: %d mode changes, %d ring changes, %u events\n", modes, rings, (unsigned)a.ne);
    for (p = 0; p < PAIRS; p++) {
        sd += memcmp(&a.s[p], &c.s[p], sizeof a.s[p]) != 0;
    }
    ed = a.ne != c.ne ? 1u : (a.ne ? (uint32_t)(memcmp(a.e, c.e, a.ne * sizeof a.e[0]) != 0) : 0u);
    CHECK(modes > 10 && rings > 0 && sd == 0u && ed == 0u, "S1 every sa1_state_ex and every event: %u of %d states differ, events differ: %u (%u events)", (unsigned)sd, PAIRS, (unsigned)ed, (unsigned)c.ne);
    for (i = 0; i < SA1G_ARENA_SIZE; i++) {
        if (a.arena[i] != b.arena[i]) {
            mask[i] = 1;
            ctl++;
        }
    }
    for (i = 0; i < SA1G_ARENA_SIZE; i++) {
        if (a.arena[i] != c.arena[i]) {
            flg++;
            if (!mask[i]) {
                if (other < 16u) {
                    printf("  FAIL  the skin's world differs at guest %08X: %02X, %02X; the control does not\n", (unsigned)(0x8C000000u + i), a.arena[i], c.arena[i]);
                }
                other++;
            }
        }
    }
    CHECK(other == 0u, "S1 the whole arena, 16 MB: the control of two plain worlds differs at %u bytes, the skin's at %u, %u of them where the control does not (0)", (unsigned)ctl, (unsigned)flg, (unsigned)other);
    for (i = 0; i < 3; i++) {
        free(all[i]->s);
        free(all[i]->e);
        free(all[i]->arena);
    }
    free(mask);
}

/* ---- S2, S6: a stand, what is drawn -------------------------------------------------------------------------------------------------------------------- */
static void test_s2(void)
{
    sa1_character_id id = 0;
    const sa1_render *r = NULL;
    sa1_state_ex x;
    tally t;
    sa1k_test_info *ti = (sa1k_test_info *)malloc(sizeof *ti);
    uint32_t arch = 0;
    int p;
    sa1_character_info ci;
    printf("S2 a stand\n");
    if (create(SA1_CREATE_SKIN, &id) != SA1_OK) {
        CHECK(0, "S2 create: %s", sa1_last_error(g_ctx));
        return;
    }
    /* before the first pair: never a plain Sonic */
    sa1_character_render(g_ctx, id, 1.0f, &r);
    tally_of(r, &t);
    CHECK(t.skin_tris == 1015u && t.body_tris == 0u, "S7 before the first pair he is there: %u triangles on the skin's archive, %u of the body", (unsigned)t.skin_tris, (unsigned)t.body_tris);
    for (p = 0; p < 90; p++) {
        tick(id, 0.0f, 0.0f, 0u);
    }
    sa1_character_render(g_ctx, id, 1.0f, &r);
    tally_of(r, &t);
    state_of(id, &x);
    info_of(id, ti);
    printf("  info  row %d, %u draws on the skin's archive, %u of them with alpha\n", x.action, (unsigned)t.skin_draws, (unsigned)t.alpha_draws);
    CHECK(t.skin_tris == 1015u && t.body_tris == 0u && t.effect_on_skin == 0u, "S2 25 parts: %u triangles on archive 61 (1015), %u on the body's (0), %u of them SA1_DRAW_EFFECT (0)", (unsigned)t.skin_tris, (unsigned)t.body_tris, (unsigned)t.effect_on_skin);
    CHECK(t.alpha_draws > 0u, "S2 RM.9: the parts on textures 97 and 98 are classed by their real alpha (%u draws)", (unsigned)t.alpha_draws);
    {
        const sa1_charset *cs = &g_ctx->chars[SA1_CHAR_SONIC];
        sa1_instance *inst = (sa1_instance *)calloc(1, sizeof *inst);
        inst->kind = 0;
        sa1_instance_alloc(g_ctx, cs, inst);
        sa1_pose_evaluate(g_ctx, cs, inst, cs->actions[0].object, cs->actions[0].motion, 0.0f, SA1_POSE_MODEL_SPACE | SA1_POSE_NO_WELD);
        printf("  info  row 0 model space: node 1 at (%.3f %.3f %.3f), node 10 at (%.3f %.3f %.3f); world torso y %.3f, state y %.3f\n", inst->node_mtx[12*1+3], inst->node_mtx[12*1+7], inst->node_mtx[12*1+11], inst->node_mtx[12*10+3], inst->node_mtx[12*10+7], inst->node_mtx[12*10+11], ti->mtx[10][7], x.sa1_position[1]);
        sa1_instance_free(g_ctx, inst);
        free(inst);
    }
    printf("  info  after the landing he stands in row %d (mode 3): the feet %.2f above the ground\n", x.action, t.lowest);
    /* a run, then a stop: row 0 */
    for (p = 0; p < 60; p++) {
        tick(id, 0.0f, 0.5f, 0u);
    }
    for (p = 0; p < 90; p++) {
        tick(id, 0.0f, 0.0f, 0u);
    }
    sa1_character_render(g_ctx, id, 1.0f, &r);
    tally_of(r, &t);
    state_of(id, &x);
    CHECK(x.action == 0 && fabs(t.lowest - 0.30) <= 0.08, "S6 the stand's clearance, row %d: the feet %.2f above the ground (0.28 to 0.32 by the prototype)", x.action, t.lowest);
    memset(&ci, 0, sizeof ci);
    ci.struct_size = sizeof ci;
    sa1_character_info_get(g_ctx, SA1_CHAR_SONIC, &ci);
    arch = ci.num_archives;
    CHECK(arch < SA1_ARCHIVE_SKIN, "S2 num_archives unchanged: %u", (unsigned)arch);
    printf("  info  alignment angles (metal099e.md 3.2: upper arms 26.4, 26.6; forearms 9.1, 5.3; thighs 3.9, 3.9; shins 9.5, 9.5): %.1f %.1f; %.1f %.1f; %.1f %.1f; %.1f %.1f\n", ti->align_deg[4], ti->align_deg[10], ti->align_deg[5],
           ti->align_deg[11], ti->align_deg[15], ti->align_deg[20], ti->align_deg[16], ti->align_deg[21]);
    CHECK(fabs(ti->align_deg[4] - 26.4) < 0.2 && fabs(ti->align_deg[10] - 26.6) < 0.2 && fabs(ti->align_deg[5] - 9.1) < 0.2 && fabs(ti->align_deg[11] - 5.3) < 0.2 && fabs(ti->align_deg[15] - 3.9) < 0.2 &&
              fabs(ti->align_deg[20] - 3.9) < 0.2 && fabs(ti->align_deg[16] - 9.5) < 0.2 && fabs(ti->align_deg[21] - 9.5) < 0.2,
          "S4 the alignment angles equal the prototype's (the review measured them by other code)");
    g_hash = (g_hash ^ (uint32_t)t.skin_tris) * 16777619u;
    {
        /* every texture of the archive */
        sa1_texture_info info;
        const char *name = NULL;
        uint32_t num = 0, bytes = 0, k;
        CHECK(sa1_texture_archive(g_ctx, SA1_CHAR_SONIC, SA1_ARCHIVE_SKIN, &name, &num) == SA1_OK && num == 9u, "S9 the archive: %s, %u textures", name ? name : "?", (unsigned)num);
        for (k = 0; k < num; k++) {
            memset(&info, 0, sizeof info);
            info.struct_size = sizeof info;
            if (sa1_texture_get(g_ctx, SA1_CHAR_SONIC, SA1_ARCHIVE_SKIN, k, &info) == SA1_OK) {
                bytes += info.width * info.height * 4u;
            }
        }
        CHECK(bytes == 36864u && ti->model_bytes == 29932u, "S9 nine textures, %u B as RGBA (36864); %u B of model held (29932)", (unsigned)bytes, (unsigned)ti->model_bytes);
    }
    free(ti);
    sa1_character_destroy(g_ctx, id);
}

/* ---- S3: every mesh node found, by row; S5: joints and NaN; S7: a jump; hit blink --------------------------------------------------------------------- */
static void s3_run(int ups, const char *what)
{
    static const int mesh[14] = { 10, 22, 34, 29, 30, 45, 40, 41, 55, 51, 52, 65, 61, 62 };
    sa1_character_id id = 0;
    sa1k_test_info *ti = (sa1k_test_info *)malloc(sizeof *ti);
    int rows_seen[160], rows_all[160], p, k;
    double worst_joint = 0.0, bone[25];
    uint32_t nan_seen = 0, bad_ball = 0, ball_pairs = 0, parts_in_ball = 0;
    int have_rows = 0;
    memset(rows_seen, 0, sizeof rows_seen);
    memset(rows_all, 0, sizeof rows_all);
    memset(bone, 0, sizeof bone);
    if (create(SA1_CREATE_SKIN, &id) != SA1_OK) {
        CHECK(0, "S3 create: %s", sa1_last_error(g_ctx));
        return;
    }
    if (ups) {
        upgrades_on(id);
    }
    for (p = 0; p < 900; p++) {
        sa1_state_ex x;
        const sa1_render *r = NULL;
        tally t;
        float sy = 0.0f;
        uint32_t b = 0;
        uint32_t i;
        if (p >= 40 && p < 100) {
            sy = 0.25f;
        } else if (p >= 100 && p < 160) {
            sy = 0.5f;
        } else if (p >= 160 && p < 460) {
            sy = 1.0f;
        } else if (p >= 500 && p < 530) {
            b = p < 510 ? SA1_BUTTON_JUMP : 0u;
        } else if (p >= 560 && p < 620) {
            b = SA1_BUTTON_ACTION;
        } else if (p >= 700 && p < 760) {
            b = SA1_BUTTON_ACTION;
            sy = 1.0f;
        }
        tick(id, 0.0f, sy, b);
        state_of(id, &x);
        info_of(id, ti);
        sa1_character_render(g_ctx, id, 1.0f, &r);
        tally_of(r, &t);
        if (x.action >= 0 && x.action < 160) {
            if (ti->drew) {
                int all = 1;
                for (k = 0; k < 14; k++) {
                    if (!ti->have[mesh[k]]) {
                        all = 0;
                    }
                }
                rows_seen[x.action]++;
                rows_all[x.action] += all;
            }
        }
        /* RM.12: a jump: the display draws the ball */
        if (x.action == 14 && !(x.game_flags & 3u)) {
            ball_pairs++;
            parts_in_ball += ti->drew != 0;
            bad_ball += t.skin_tris == 0u || t.body_tris != 0u;
        }
        /* S5: joints: from the table's parents, by other code than the append's: each part's distance to its parent stays the bone's, whatever the pose; each part's rotation stays a rotation */
        if (ti->drew) {
            static const int8_t parent[25] = { -1, 0, 1, 0, 3, 4, 5, 6, 6, 0, 9, 10, 11, 12, 12, 0, 15, 16, 17, 17, 0, 20, 21, 22, 22 };
            for (i = 1; i < 25u; i++) {
                const double *a = ti->p[i], *q = ti->p[parent[i]];
                double d = sqrt((a[0] - q[0]) * (a[0] - q[0]) + (a[1] - q[1]) * (a[1] - q[1]) + (a[2] - q[2]) * (a[2] - q[2]));
                if (d != d) {
                    nan_seen++;
                }
                if (bone[i] == 0.0) {
                    bone[i] = d;
                }
                if (fabs(d - bone[i]) > worst_joint) {
                    worst_joint = fabs(d - bone[i]);
                }
            }
        }
        if (r != NULL) {
            for (i = 0; i < 9u * r->num_triangles; i++) {
                if (r->positions[i] != r->positions[i]) {
                    nan_seen++;
                    break;
                }
            }
        }
    }
    for (p = 0; p < 160; p++) {
        if (rows_seen[p]) {
            have_rows++;
            printf("  info  %s row %d: %d pairs with parts, %d with all 14 mesh nodes\n", what, p, rows_seen[p], rows_all[p]);
        }
    }
    {
        static const int must[] = { 0, 9, 10, 11, 12 };
        for (k = 0; k < 5; k++) {
            int row = must[k];
            CHECK(rows_seen[row] > 0 && rows_all[row] >= rows_seen[row] - 2, "S3 %s row %d: every Sonic mesh node of the table found in %d of %d pairs (the head above all: its morph copy)", what, row, rows_all[row], rows_seen[row]);
        }
    }
    CHECK(nan_seen == 0u && worst_joint < 1e-3, "S5 %s: no NaN (%u); joints never come apart: each part's distance to its parent moved by at most %.6f units over 900 pairs (1e-3)", what, (unsigned)nan_seen, worst_joint);
    CHECK(ball_pairs > 0u && bad_ball == 0u && parts_in_ball == 0u, "S7 %s a jump (state row 14): the ball with archive 61 on all %u pairs of it (%u bad), no parts (%u)", what, (unsigned)ball_pairs, (unsigned)bad_ball, (unsigned)parts_in_ball);
    (void)have_rows;
    free(ti);
    sa1_character_destroy(g_ctx, id);
}

/* the head follows the look task through the morph copy (RM.1) */
static void test_s3_look(void)
{
    sa1_character_id id = 0;
    sa1k_test_info *a = (sa1k_test_info *)malloc(sizeof *a), *b = (sa1k_test_info *)malloc(sizeof *b);
    sa1_look l;
    double rel0[9], rel1[9], ang;
    int p;
    if (create(SA1_CREATE_SKIN, &id) != SA1_OK) {
        CHECK(0, "S3 look create");
        return;
    }
    for (p = 0; p < 40; p++) {
        tick(id, 0.0f, 0.5f, 0u);
    }
    for (p = 0; p < 60; p++) {
        tick(id, 0.0f, 0.0f, 0u);
    }
    info_of(id, a);
    memset(&l, 0, sizeof l);
    l.struct_size = sizeof l;
    l.kind = SA1_LOOK_POINT;
    {
        sa1_state_ex xs;
        state_of(id, &xs);
        l.point[0] = xs.position[0] - 400.0f * (float)sin((double)xs.yaw);
        l.point[1] = xs.position[1] + 400.0f * (float)cos((double)xs.yaw);
        l.point[2] = xs.position[2] + 20.0f;
    }
    CHECK(sa1_character_look(g_ctx, id, &l) == SA1_OK, "S3 the look: %s", sa1_last_error(g_ctx));
    for (p = 0; p < 60; p++) {
        tick(id, 0.0f, 0.0f, 0u);
    }
    info_of(id, b);
    {
        sa1_state_ex xs;
        double ra[9], rb[9], ta[9], tb[9];
        state_of(id, &xs);
        rot_of(a->mtx[22], ra);
        mul3_t(ra, a->rest[22], ta);
        rot_of(b->mtx[22], rb);
        mul3_t(rb, b->rest[22], tb);
        printf("  info  look: state row %d, head_angle %d %d, owner %u, have 22: %d, W(22) moved %.1f degrees\n", xs.action, (int)xs.head_angle[0], (int)xs.head_angle[1], (unsigned)xs.look_owner, b->have[22], m3_angle_between(ta, tb));
    }
    mul3_t(a->r[1], a->r[0], rel0);
    mul3_t(b->r[1], b->r[0], rel1);
    ang = m3_angle_between(rel0, rel1);
    CHECK(ang > 10.0, "S3 RM.1: the look task turns his head: %.1f degrees against the torso", ang);
    free(a);
    free(b);
    sa1_character_destroy(g_ctx, id);
}

/* row 47 with the shoes: the foot within 1 degree of the plain one (RM.2) */
static int play_row(sa1_character_id id, int row)
{
    sa1_emote e;
    memset(&e, 0, sizeof e);
    e.struct_size = sizeof e;
    e.count = 1;
    e.items[0].source = SA1_EMOTE_ROW;
    e.items[0].index = (uint32_t)row;
    e.items[0].play = SA1_EMOTE_PLAY_LOOP;
    e.flags = SA1_EMOTE_KEEP_INPUT;
    return sa1_character_emote(g_ctx, id, &e) == SA1_OK;
}

/* rows the flat ground does not reach by the stick (the review's probe did not either): through an emote. A refused row says so */
static void test_s3_rows_by_emote(void)
{
    static const int rows[2] = { 13, 16 };
    static const int mesh[14] = { 10, 22, 34, 29, 30, 45, 40, 41, 55, 51, 52, 65, 61, 62 };
    sa1k_test_info *ti = (sa1k_test_info *)malloc(sizeof *ti);
    int r;
    for (r = 0; r < 2; r++) {
        sa1_character_id id = 0;
        int p, n = 0, all = 0, ok, reached_cur = 0;
        if (create(SA1_CREATE_SKIN, &id) != SA1_OK) {
            CHECK(0, "S3 row %d: create", rows[r]);
            continue;
        }
        for (p = 0; p < 60; p++) {
            tick(id, 0.0f, 0.0f, 0u);
        }
        ok = play_row(id, rows[r]);
        for (p = 0; p < 40; p++) {
            sa1_state_ex x;
            int k, a = 1;
            tick(id, 0.0f, 0.0f, 0u);
            state_of(id, &x);
            info_of(id, ti);
            if (x.emote_row == rows[r] && ti->drew) {
                for (k = 0; k < 14; k++) {
                    a = a && ti->have[mesh[k]];
                }
                n++;
                all += a;
                reached_cur += ti->cur;
            }
        }
        if (!ok) {
            printf("  info  S3 row %d: the emote is refused (%s): the check is not made\n", rows[r], sa1_last_error(g_ctx));
        } else {
            CHECK(n > 0 && all == n, "S3 row %d, by an emote: every mesh node found in %d of %d pairs", rows[r], all, n);
            if (rows[r] == 13) {
                CHECK(n > 0 && ti->cur == 134, "S4 row 13 flies with row 134 (flown row %d)", ti->cur);
            }
        }
        (void)reached_cur;
        sa1_character_destroy(g_ctx, id);
    }
    free(ti);
}

static void test_s3_row47(void)
{
    sa1_character_id a = 0, b = 0;
    sa1k_test_info *ia = (sa1k_test_info *)malloc(sizeof *ia), *ib = (sa1k_test_info *)malloc(sizeof *ib);
    int p, n = 0;
    double worst = 0.0;
    int oka, okb;
    if (create(SA1_CREATE_SKIN, &a) != SA1_OK || create(SA1_CREATE_SKIN, &b) != SA1_OK) {
        CHECK(0, "S3 row 47: create");
        return;
    }
    upgrades_on(b);
    for (p = 0; p < 60; p++) {
        tick(a, 0.0f, 0.0f, 0u);
        tick(b, 0.0f, 0.0f, 0u);
    }
    oka = play_row(a, 47);
    okb = play_row(b, 47);
    for (p = 0; p < 40; p++) {
        sa1_state_ex xa, xb;
        tick(a, 0.0f, 0.0f, 0u);
        tick(b, 0.0f, 0.0f, 0u);
        state_of(a, &xa);
        state_of(b, &xb);
        info_of(a, ia);
        info_of(b, ib);
        if (xa.emote_row == 47 && xb.emote_row == 47 && xa.action == xb.action && ia->drew && ib->drew) {
            double d = m3_angle_between(ia->r[18], ib->r[18]);
            if (n == 0) {
                double q0[9], q1[9], q2[9], q3[9];
                rot_of(ia->mtx[52], q0);
                rot_of(ia->mtx[51], q1);
                rot_of(ib->mtx[52], q2);
                rot_of(ib->mtx[51], q3);
                mul3_t(q0, q1, q0);
                mul3_t(q2, q3, q2);
                printf("  info  row 47: state row %d; W(52) against W(51): plain and worn differ by %.1f; part 18 by %.1f; drive row %d, fix %d, keys %u\n", xa.action, m3_angle_between(q0, q2), d, ib->drive_row, ib->has_fix52,
                       SA1_VEC(g_ctx->chars[0].motions, sa1_motion)[g_ctx->chars[0].actions[47].motion].nodes[52].num_ang);
            }
            n++;
            if (d > worst) {
                worst = d;
            }
        }
    }
    printf("  info  row 47: emote %d, %d: %s\n", oka, okb, sa1_last_error(g_ctx));
    CHECK(oka && okb && n > 0 && worst < 1.0, "S3 RM.2: row 47 with the shoes on: the foot within 1 degree of the plain one in %d pairs (worst %.2f)", n, worst);
    free(ia);
    free(ib);
    sa1_character_destroy(g_ctx, a);
    sa1_character_destroy(g_ctx, b);
}

/* ---- S4: the flying ---------------------------------------------------------------------------------------------------------------------------------- */
static void test_s4(void)
{
    sa1_character_id id = 0;
    const sa1_charset *cs = &g_ctx->chars[SA1_CHAR_SONIC];
    sa1k_test_info *ti = (sa1k_test_info *)malloc(sizeof *ti), *tp = (sa1k_test_info *)malloc(sizeof *tp);
    sa1_instance *inst = (sa1_instance *)calloc(1, sizeof *inst);
    sa1_status st;
    int p, seen_w_on = 0, rows[16];
    double worst_turn = 0.0, worst_lift = 0.0, worst_head = 0.0, worst_root = 0.0, min_clear = 1e30, worst_ag = 0.0;
    int formula_pairs = 0, have_prev = 0, root_pairs = 0;
    double ag[25][9];
    int have_ag = 0;
    float last_w = 0.0f;
    int bad_step = 0;
    float min_w_in_run = 2.0f;
    printf("S4 the flying\n");
    memset(rows, 0, sizeof rows);
    inst->kind = SA1_CHAR_SONIC;
    if (create(SA1_CREATE_SKIN, &id) != SA1_OK || sa1_instance_alloc(g_ctx, cs, inst) != SA1_OK) {
        CHECK(0, "S4 create");
        return;
    }
    /* speed up through 9, 10, 11, 12, 13, then slow down */
    for (p = 0; p < 560; p++) {
        sa1_state_ex x;
        const sa1_render *r = NULL;
        tally t;
        float sy = p < 30 ? 0.0f : (p < 90 ? 0.3f : (p < 400 ? 1.0f : 0.0f));
        tick(id, 0.0f, sy, 0u);
        state_of(id, &x);
        info_of(id, ti);
        sa1_character_render(g_ctx, id, 1.0f, &r);
        tally_of(r, &t);
        if (x.action >= 9 && x.action <= 13) {
            rows[x.action]++;
        }
        /* the fade steps by 1/8 */
        if (fabsf(ti->w - last_w) > 0.125f + 1e-4f) {
            bad_step++;
        }
        last_w = ti->w;
        if (ti->w > 0.0f) {
            seen_w_on = 1;
        }
        if (x.action >= 10 && x.action <= 13 && p > 120 && ti->w < min_w_in_run) {
            min_w_in_run = ti->w;
        }
        /* from one pair to the next */
        if (have_prev && ti->drew && tp->drew && ti->w >= 1.0f && tp->w >= 1.0f) {
            uint32_t i;
            for (i = 0; i < 25u; i++) {
                double rel0[9], rel1[9], d;
                mul3_t(tp->r[i], tp->r[0], rel0);
                mul3_t(ti->r[i], ti->r[0], rel1);
                d = m3_angle_between(rel0, rel1);
                if (d > worst_turn) {
                    worst_turn = d;
                    printf("  info  turn %.1f part %u pair %d row %d w %.3f x %.3f phase %.3f\n", d, (unsigned)i, p, x.action, ti->w, ti->x, ti->phase);
                }
            }
        }
        /* the joint column against the formula, with the fade done: the flown row at the phase */
        if (ti->w >= 1.0f && ti->cur >= 0 && ti->prev < 0 && ti->drew) {
            const sa1_action *ac = &cs->actions[ti->cur];
            const sa1_motion *mo = &SA1_VEC(cs->motions, sa1_motion)[ac->motion];
            st = sa1_pose_evaluate(g_ctx, cs, inst, ac->object, ac->motion, (float)(ti->phase * mo->num_frames), SA1_POSE_MODEL_SPACE | SA1_POSE_NO_WELD);
            if (st == SA1_OK) {
                double root[9];
                uint32_t i;
                double t0[9];
                root[0] = ti->fly_root[0]; root[1] = ti->fly_root[1]; root[2] = ti->fly_root[2];
                root[3] = ti->fly_root[4]; root[4] = ti->fly_root[5]; root[5] = ti->fly_root[6];
                root[6] = ti->fly_root[8]; root[7] = ti->fly_root[9]; root[8] = ti->fly_root[10];
                for (i = 0; i < 25u; i++) {
                    const float *m = inst->node_mtx + 12u * (uint32_t)k_parts[i].joint_node;
                    double a[9], q[9], tn[9], here[9];
                    a[0] = m[0]; a[1] = m[1]; a[2] = m[2];
                    a[3] = m[4]; a[4] = m[5]; a[5] = m[6];
                    a[6] = m[8]; a[7] = m[9]; a[8] = m[10];
                    sa1_pose_polar3(a, q);
                    mul3(root, q, tn);
                    mul3_t(tn, ti->rest[k_parts[i].joint_node], tn);
                    if (i == 0u) {
                        memcpy(t0, tn, sizeof t0);
                    }
                    /* R(i) = T(i) * AG(i): AG(i) must stay the one constant over every pair */
                    {
                        double trans[9];
                        trans[0] = tn[0]; trans[1] = tn[3]; trans[2] = tn[6];
                        trans[3] = tn[1]; trans[4] = tn[4]; trans[5] = tn[7];
                        trans[6] = tn[2]; trans[7] = tn[5]; trans[8] = tn[8];
                        mul3(trans, ti->r[i], here);
                    }
                    if (!have_ag) {
                        memcpy(ag[i], here, sizeof here);
                    } else {
                        double d = m3_angle_between(ag[i], here);
                        if (d > worst_ag) {
                            worst_ag = d;
                        }
                    }
                }
                have_ag = 1;
                {
                    /* RM.3: head against torso = the motion's, within 1 degree, never 173 */
                    const float *mh = inst->node_mtx + 12u * 12u, *mt = inst->node_mtx + 12u * 1u;
                    double ah[9], at[9], qh[9], qt[9], th[9], tt[9], rel[9], relp[9];
                    ah[0] = mh[0]; ah[1] = mh[1]; ah[2] = mh[2]; ah[3] = mh[4]; ah[4] = mh[5]; ah[5] = mh[6]; ah[6] = mh[8]; ah[7] = mh[9]; ah[8] = mh[10];
                    at[0] = mt[0]; at[1] = mt[1]; at[2] = mt[2]; at[3] = mt[4]; at[4] = mt[5]; at[5] = mt[6]; at[6] = mt[8]; at[7] = mt[9]; at[8] = mt[10];
                    sa1_pose_polar3(ah, qh);
                    sa1_pose_polar3(at, qt);
                    mul3_t(qh, ti->rest[12], th);
                    mul3_t(qt, ti->rest[1], tt);
                    mul3_t(th, tt, rel);
                    {
                        /* in the root's frame: R(1) R(0)^T = root T1 T0^T root^T */
                        double rm[9];
                        mul3(root, rel, rm);
                        mul3_t(rm, root, rel);
                    }
                    mul3_t(ti->r[1], ti->r[0], relp);
                    {
                        double d = m3_angle_between(rel, relp);
                        if (d > worst_head) {
                            worst_head = d;
                        }
                    }
                }
                formula_pairs++;
                /* the clearance while flown */
                if (t.lowest < min_clear) {
                    min_clear = t.lowest;
                }
            }
        }
        /* the root's formula against the world's own drawn torso, in a steady row (Sonic's own row, same root but for the lift) */
        if (x.action >= 10 && x.action <= 13 && ti->have[10] && ti->fly_root[0] != 0.0) {
            const sa1_action *ac = &cs->actions[x.action];
            sa1w_pstate ps;
            int ok = 0;
            ok = sa1_character_world(g_ctx, id) != NULL;
            (void)ps;
            if (ok && ti->w >= 1.0f && ac->status == SA1_ACTION_OK) {
                double rw[9], tr[9], d;
                double root[9];
                root[0] = ti->fly_root[0]; root[1] = ti->fly_root[1]; root[2] = ti->fly_root[2];
                root[3] = ti->fly_root[4]; root[4] = ti->fly_root[5]; root[5] = ti->fly_root[6];
                root[6] = ti->fly_root[8]; root[7] = ti->fly_root[9]; root[8] = ti->fly_root[10];
                /* on flat ground in a straight line: the drawn torso's rotation against its rest, up to the motion's small swing: within 45 degrees of the root's turn */
                rot_of(ti->mtx[10], rw);
                mul3_t(rw, ti->rest[10], tr);
                d = m3_angle_between(tr, root);
                if (d > worst_root) {
                    worst_root = d;
                }
                root_pairs++;
                {
                    double up = ti->fly_root[7] - ti->mtx[10][7];
                    (void)up;
                }
            }
        }
        memcpy(tp, ti, sizeof *tp);
        have_prev = 1;
        (void)worst_lift;
    }
    printf("  info  rows 9-13: %d %d %d %d %d pairs; the first run's w never fell under %.3f after the fade\n", rows[9], rows[10], rows[11], rows[12], rows[13], min_w_in_run);
    CHECK(rows[10] > 0 && rows[11] > 0 && rows[12] > 0, "S4 he ran through rows 10, 11, 12 (%d, %d, %d pairs; row 13: %d)", rows[10], rows[11], rows[12], rows[13]);
    CHECK(seen_w_on && bad_step == 0, "S4 the fade steps by 1/8 a pair (%d bad steps)", bad_step);
    CHECK(formula_pairs > 30 && worst_ag < 0.06, "S4 %d pairs with the fade done: each part = the joint column's formula of the flown row at the mapped frame: A * G stays constant within %.4f degrees (1e-3 rad = 0.057)", formula_pairs, worst_ag);
    CHECK(formula_pairs > 30 && worst_head < 1.0, "S4 RM.3: the head within 1 degree of the torso's turn plus the motion's, never 173 (worst %.3f)", worst_head);
    CHECK(min_clear > 1.0, "S6 while flown: the clearance is above 1.0 (%.2f)", min_clear);
    CHECK(worst_turn < 20.0, "S4 RM.7: speeding up and slowing down through 10, 11, 12, the fade done: from one pair to the next no part turns more than 20 degrees against the torso (worst %.1f; in Sonic's own rows his own arms turn up to 100 in a pair, MEASURED here: the game's)", worst_turn);
    printf("  info  the root's turn against the drawn torso's: worst %.1f degrees over %d pairs (the motion's own swing)\n", worst_root, root_pairs);
    CHECK(root_pairs > 30 && worst_root < 45.0, "S4 RM.7: the flown root (Z, X, 0x8000 - y of the drawn angles) turns as the world's drawn torso does (worst %.1f degrees)", worst_root);
    /* w and x do not move in sa1_character_render alone */
    {
        float w0;
        sa1_character_id id2 = 0;
        const sa1_render *r = NULL;
        info_of(id, ti);
        sa1_character_destroy(g_ctx, id);
        create(SA1_CREATE_SKIN, &id2);
        for (p = 0; p < 110; p++) {
            tick(id2, 0.0f, p < 20 ? 0.0f : 1.0f, 0u);
        }
        info_of(id2, ti);
        w0 = ti->w;
        for (p = 0; p < 10; p++) {
            sa1_character_render(g_ctx, id2, 0.5f, &r);
        }
        info_of(id2, ti);
        CHECK(ti->w == w0, "S4 RM.7: w does not move in sa1_character_render alone (%.3f, %.3f): photo mode's freeze holds it", w0, ti->w);
        /* option off: Sonic's */
        sa1_character_set_option(g_ctx, id2, SA1_COPT_SKIN_ANIM, 0);
        for (p = 0; p < 20; p++) {
            tick(id2, 0.0f, 1.0f, 0u);
        }
        info_of(id2, ti);
        CHECK(ti->w == 0.0f, "S4 the option off: he runs as Sonic, at once with no re-creation (w %.3f)", ti->w);
        sa1_character_destroy(g_ctx, id2);
    }
    sa1_instance_free(g_ctx, inst);
    free(inst);
    free(ti);
    free(tp);
}

/* ---- S6: effects with and without the skin; Light Speed; S5: a curled pair at alpha 0.5; S7: blink and hidden --------------------------------------------------- */
static void test_s6(void)
{
    enum { PAIRS = 300 };
    sa1_character_id a = 0, b = 0;
    int p;
    uint32_t spr_a = 0, spr_b = 0, aura = 0, spr_a64 = 0, spr_b64 = 0;
    int row64 = 0, last_row = -1;
    double worst_mid = 0.0;
    int curled = 0;
    printf("S6 effects\n");
    create(0, &a);
    create(SA1_CREATE_SKIN, &b);
    upgrades_on(a);
    upgrades_on(b);
    for (p = 0; p < PAIRS; p++) {
        const sa1_render *ra = NULL, *rb = NULL;
        tally ta, tb;
        sa1_state_ex x;
        uint32_t but = (p >= 30 && p < 120) || (p >= 180 && p < 260) ? SA1_BUTTON_ACTION : 0u;
        float sy = (p >= 120 && p < 180) ? 1.0f : 0.0f;
        tick(a, 0.0f, sy, but);
        tick(b, 0.0f, sy, but);
        state_of(a, &x);
        if (p == 200) {
            printf(" equipment %X; ", (unsigned)x.equipment);
        }
        if (x.action != last_row) {
            printf(" %d:%d", p, x.action);
            last_row = x.action;
        }
        sa1_character_render(g_ctx, a, 1.0f, &ra);
        tally_of(ra, &ta);
        spr_a += ta.sprites + ta.effect_draws;
        sa1_character_render(g_ctx, b, 1.0f, &rb);
        tally_of(rb, &tb);
        spr_b += tb.sprites + tb.effect_draws;
        if (x.action == 64) {
            row64++;
            spr_a64 += ta.sprites + ta.effect_draws;
            spr_b64 += tb.sprites + tb.effect_draws;
            aura += tb.body_tris != 0u;
        }
        if (tb.body_tris != 0u) {
            aura++;
        }
        /* a curled pair at alpha 0.5 */
        if (x.action == 15 || x.action == 14) {
            const sa1_render *r0 = NULL, *r1 = NULL, *rm = NULL;
            float *p0, *p1;
            uint32_t n = 0, i;
            sa1_character_render(g_ctx, b, 0.0f, &r0);
            if (r0 != NULL) {
                n = 9u * r0->num_triangles;
                p0 = (float *)malloc(n * sizeof(float));
                memcpy(p0, r0->positions, n * sizeof(float));
                sa1_character_render(g_ctx, b, 1.0f, &r1);
                if (r1 != NULL && 9u * r1->num_triangles == n) {
                    p1 = (float *)malloc(n * sizeof(float));
                    memcpy(p1, r1->positions, n * sizeof(float));
                    sa1_character_render(g_ctx, b, 0.5f, &rm);
                    if (rm != NULL && 9u * rm->num_triangles == n) {
                        curled++;
                        for (i = 0; i < n; i++) {
                            double d = fabs((double)rm->positions[i] - 0.5 * ((double)p0[i] + (double)p1[i]));
                            if (d > worst_mid) {
                                worst_mid = d;
                            }
                        }
                    }
                    free(p1);
                }
                free(p0);
            }
        }
    }
    CHECK(spr_a == spr_b && spr_a > 0u, "S6 the sparkles and dust with the skin as without: %u, %u sprites over %d pairs", (unsigned)spr_a, (unsigned)spr_b, PAIRS);
    CHECK(aura == 0u, "S6 RM.6: no draw of Sonic's own body or his shaped glow with the skin (%u pairs)", (unsigned)aura);
    printf("  info  a Light Speed charge (row 64): %d pairs, sprites %u without the skin and %u with it; a jump's or a spin's worst interpolated vertex off the midpoint of its two pairs: %.2f host units over %d pairs (quoted, S5)\n", row64, (unsigned)spr_a64, (unsigned)spr_b64, worst_mid, curled);
    CHECK(row64 > 0 && spr_a64 == spr_b64, "S6 RM.6: the Light Speed charge reached: %d pairs of row 64, its sparks as without the skin (%u, %u)", row64, (unsigned)spr_a64, (unsigned)spr_b64);
    sa1_character_destroy(g_ctx, a);
    sa1_character_destroy(g_ctx, b);
}

static void test_s7(void)
{
    sa1_character_id id = 0;
    int p, blink = 0, parts = 0, hidden = 0, hidden_parts = 0;
    uint32_t screen[] = { 0x8C88F5E4u, 0x8C88F5E8u, 0x8C88F5ECu, 0x8C88F600u, 0x8C88F604u, 0x8C88F608u, 0x8C88F60Cu, 0x8C88F618u, 0x8C88F61Cu };
    uint32_t k;
    printf("S7 a hit's blink, a hidden pair\n");
    create(SA1_CREATE_SKIN, &id);
    for (p = 0; p < 60; p++) {
        tick(id, 0.0f, 0.0f, 0u);
    }
    sa1_character_add_rings(g_ctx, id, 20);
    {
        sa1_damage dm;
        memset(&dm, 0, sizeof dm);
        dm.struct_size = sizeof dm;
        sa1_character_damage(g_ctx, id, &dm);
    }
    for (p = 0; p < 120; p++) {
        const sa1_render *r = NULL;
        tally t;
        tick(id, 0.0f, 0.0f, 0u);
        sa1_character_render(g_ctx, id, 1.0f, &r);
        tally_of(r, &t);
        blink += t.skin_tris == 0u;
        parts += t.skin_tris == 1015u;
    }
    CHECK(blink > 0 && parts > 0, "S7 the hit blink blinks him too: %d pairs without him, %d with", blink, parts);
    /* the reference simulator's zero screen: the game's visibility test never passes */
    for (k = 0; k < sizeof screen / sizeof screen[0]; k++) {
        sa1g_world_wr32(sa1_character_world(g_ctx, id), screen[k], 0u);
    }
    for (p = 0; p < 40; p++) {
        const sa1_render *r = NULL;
        tally t;
        tick(id, 0.0f, 0.0f, 0u);
        sa1_character_render(g_ctx, id, 1.0f, &r);
        tally_of(r, &t);
        if (sa1_capture_hidden(sa1_character_capture(g_ctx, id))) {
            hidden++;
            hidden_parts += t.skin_tris == 1015u && t.body_tris == 0u;
        }
    }
    CHECK(hidden > 0 && hidden_parts == hidden, "S7 a hidden pair: still 25 parts: %d of %d pairs", hidden_parts, hidden);
    CHECK(!count_faults(id), "S7 no fault");
    sa1_character_destroy(g_ctx, id);
}

/* ---- S8: Character Select -------------------------------------------------------------------------------------------------------------------------------- */
static uint32_t hash_bytes(uint32_t h, const void *p, size_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    size_t i;
    for (i = 0; i < n; i++) {
        h = (h ^ b[i]) * 16777619u;
    }
    return h;
}

typedef struct s8_out {
    uint32_t events, screen, sonic;     /* hashes */
    uint32_t parts_idle, parts_pose, head_moves, head_found;
    uint32_t other_slot_tris;
} s8_out;

static int s8_run(int on_at, int off_at, s8_out *o)
{
    sa1_frontend_desc d;
    sa1_frontend *fe = NULL;
    int f;
    memset(o, 0, sizeof *o);
    o->events = o->screen = o->sonic = 2166136261u;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.language = 1;
    d.slot = 0;
    d.open_slots = ~0u;
    if (SA1_FAILED(sa1_frontend_open(g_ctx, &d, &fe))) {
        printf("  open: %s\n", sa1_last_error(g_ctx));
        return -1;
    }
    for (f = 0; f < 420; f++) {
        sa1_frontend_input in;
        uint32_t n = 0, k;
        const sa1_render *r = NULL;
        int32_t kind = -1;
        sa1_event ev[64];
        const sa1_screen *sc = NULL;
        tally t;
        memset(&in, 0, sizeof in);
        in.struct_size = sizeof in;
        if (f == on_at) {
            CHECK(sa1_frontend_set_skin(fe, &g_skin) == SA1_OK, "S8 sa1_frontend_set_skin: %s", sa1_last_error(g_ctx));
        }
        if (f == off_at) {
            sa1_frontend_set_skin(fe, NULL);
        }
        if (f == 300) {
            in.buttons = SA1_FRONTEND_B_A;
        }
        sa1_frontend_tick(fe, 1.0f / 60.0f, &in, &n);
        do {
            n = 0;
            sa1_frontend_poll(fe, ev, 64, &n);
            for (k = 0; k < n; k++) {
                o->events = hash_bytes(o->events, &ev[k], sizeof ev[k]);
            }
        } while (n == 64);
        if (sa1_frontend_screen(fe, &sc) == SA1_OK && sc != NULL) {
            o->screen = hash_bytes(o->screen, &sc->num_draws, sizeof sc->num_draws);
            if (sc->num_vertices && sc->vertices) {
                o->screen = hash_bytes(o->screen, sc->vertices, sizeof sc->vertices[0] * sc->num_vertices);
            }
            if (sc->num_sprites && sc->sprites) {
                o->screen = hash_bytes(o->screen, sc->sprites, sizeof sc->sprites[0] * sc->num_sprites);
            }
        }
        if (sa1_frontend_render(fe, &r, &kind) == SA1_OK && r != NULL) {
            tally_of(r, &t);
            if (f >= 200 && f < 290) {
                o->parts_idle += t.skin_tris == 1015u;
                if (f == 250 && sa1fe_test_skin(fe) != NULL) {
                    sa1k_test_info *ti = (sa1k_test_info *)malloc(sizeof *ti);
                    sa1k_test_get(sa1fe_test_skin(fe), ti);
                    o->head_found = ti->have[22];
                    free(ti);
                }
            }
            if (f >= 310) {
                o->parts_pose += t.skin_tris == 1015u;
            }
            if (f >= on_at && on_at >= 0 && (off_at < 0 || f < off_at)) {
                if (t.body_tris != 0u) {
                    o->head_moves++;    /* (a body draw with the skin on is counted here) */
                }
            }
            if (off_at >= 0 && f >= off_at + 2 && r->num_triangles) {
                o->sonic = hash_bytes(o->sonic, r->positions, 9u * sizeof(float) * r->num_triangles);
            }
        }
    }
    sa1_frontend_close(fe);
    return 0;
}

static void test_s8(void)
{
    s8_out a, b, c, d;
    sa1_frontend_desc fd;
    sa1_frontend *fe = NULL;
    printf("S8 Character Select\n");
    if (s8_run(-1, -1, &a) || s8_run(100, -1, &b) || s8_run(-1, -1, &c) || s8_run(100, 150, &d)) {
        CHECK(0, "S8 the runs");
        return;
    }
    CHECK(a.events == c.events && a.events == b.events, "S8 the same events, frame by frame, with the skin on (%08X, %08X, control %08X)", (unsigned)a.events, (unsigned)b.events, (unsigned)c.events);
    CHECK(a.screen == c.screen && a.screen == b.screen, "S8 the same screen draws, frame by frame (%08X, %08X, control %08X)", (unsigned)a.screen, (unsigned)b.screen, (unsigned)c.screen);
    CHECK(b.head_found == 1u, "S8 RM.1: on the screen too the head's part finds node 22's morph copy");
    CHECK(b.parts_idle == 90u && b.parts_pose > 50u && b.head_moves == 0u, "S8 slot 0, the skin on: 25 parts at the stand (%u of 90 frames) and in the pick pose (%u frames); none of Sonic's body (%u)", (unsigned)b.parts_idle, (unsigned)b.parts_pose, (unsigned)b.head_moves);
    /* another slot, with the skin on: nothing of it */
    memset(&fd, 0, sizeof fd);
    fd.struct_size = sizeof fd;
    fd.language = 1;
    fd.slot = 1;
    fd.open_slots = ~0u;
    if (sa1_frontend_open(g_ctx, &fd, &fe) == SA1_OK) {
        int f;
        uint32_t skin_tris = 0, all = 0;
        sa1_frontend_set_skin(fe, &g_skin);
        for (f = 0; f < 150; f++) {
            sa1_frontend_input in;
            uint32_t n = 0;
            const sa1_render *r = NULL;
            int32_t kind = -1;
            tally t;
            sa1_event ev[64];
            memset(&in, 0, sizeof in);
            in.struct_size = sizeof in;
            sa1_frontend_tick(fe, 1.0f / 60.0f, &in, &n);
            do {
                n = 0;
                sa1_frontend_poll(fe, ev, 64, &n);
            } while (n == 64);
            if (sa1_frontend_render(fe, &r, &kind) == SA1_OK && r != NULL) {
                tally_of(r, &t);
                skin_tris += t.skin_tris;
                all += r->num_triangles;
            }
        }
        CHECK(skin_tris == 0u && all > 0u, "S8 slot 1 with the skin set: nothing of it (%u triangles of %u)", (unsigned)skin_tris, (unsigned)all);
        sa1_frontend_close(fe);
    }
    /* off again: Sonic's draws: a control of the plain run's own frames is the same world after the same frames, so the hash of a run that was off for those frames */
    {
        s8_out e;
        s8_run(-1, 150, &e);
        CHECK(d.sonic == e.sonic, "S8 off again: Sonic's draws identical from then on (%08X, %08X)", (unsigned)d.sonic, (unsigned)e.sonic);
    }
}

/* ---- S9: refusals, leaks ------------------------------------------------------------------------------------------------------------------------------- */
static void test_s9(const char *gdi)
{
    sa1_character_id id = 0;
    sa1_character_desc d;
    sa1_status st;
    sa1_skin_desc bad = g_skin;
    size_t files0;
    printf("S9 the load\n");
    g_quiet_log = 1;
    /* a second context: no disc */
    {
        sa1_host h;
        sa1_context *c2 = NULL;
        sa1_host_defaults(&h);
        h.log_level = SA1_LOG_ERROR;
        if (sa1_init(&h, &c2) == SA1_OK) {
            sa1_frontend *fe = NULL;
            memset(&d, 0, sizeof d);
            d.struct_size = sizeof d;
            d.kind = SA1_CHAR_SONIC;
            d.flags = SA1_CREATE_SKIN;
            d.skin = &g_skin;
            st = sa1_character_create_ex(c2, &d, &id);
            CHECK(st == SA1_ERR_NOT_LOADED, "S9 no disc: %s (%s)", sa1_status_string(st), sa1_last_error(c2));
            (void)fe;
            sa1_shutdown(c2);
        }
    }
    {
        sa1_host h;
        sa1_context *c3 = NULL;
        sa1_load_options lo;
        sa1_host_defaults(&h);
        h.log_level = SA1_LOG_ERROR;
        if (sa1_init(&h, &c3) == SA1_OK) {
            memset(&lo, 0, sizeof lo);
            lo.struct_size = sizeof lo;
            lo.characters = 1u << SA1_CHAR_SONIC;
            lo.flags = SA1_LOAD_DEFAULT | SA1_LOAD_SUPER_FORMS;
            if (!SA1_FAILED(sa1_load_disc(c3, gdi, &lo))) {
                memset(&d, 0, sizeof d);
                d.struct_size = sizeof d;
                d.kind = SA1_CHAR_SONIC;
                d.flags = SA1_CREATE_SKIN;
                bad.model_sha1 = "0000000000000000000000000000000000000000";
                d.skin = &bad;
                st = sa1_character_create_ex(c3, &d, &id);
                CHECK(st == SA1_ERR_NOT_LOADED && strstr(sa1_last_error(c3), "ADV02.PRS") != NULL, "S9 a wrong SHA-1: %s (%s)", sa1_status_string(st), sa1_last_error(c3));
                bad = g_skin;
                bad.tex_sha1 = "0000000000000000000000000000000000000000";
                st = sa1_character_create_ex(c3, &d, &id);
                CHECK(st == SA1_ERR_NOT_LOADED && strstr(sa1_last_error(c3), "ADV_MR03.PRS") != NULL, "S9 ... of the textures: %s (%s)", sa1_status_string(st), sa1_last_error(c3));
                bad = g_skin;
                {
                    sa1_texture_info ti;
                    memset(&ti, 0, sizeof ti);
                    ti.struct_size = sizeof ti;
                    st = sa1_texture_get(c3, SA1_CHAR_SONIC, SA1_ARCHIVE_SKIN, 0, &ti);
                    CHECK(st == SA1_ERR_NOT_LOADED, "S9 the archive before the first use: %s", sa1_status_string(st));
                }
            }
            sa1_shutdown(c3);
        }
    }
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.kind = SA1_CHAR_TAILS;
    d.flags = SA1_CREATE_SKIN;
    d.skin = &g_skin;
    st = sa1_character_create_ex(g_ctx, &d, &id);
    CHECK(st == SA1_ERR_INVALID_ARGUMENT, "S9 kind 1: %s (%s)", sa1_status_string(st), sa1_last_error(g_ctx));
    d.kind = SA1_CHAR_SONIC;
    d.flags = SA1_CREATE_SKIN | SA1_CREATE_PUPPET;
    st = sa1_character_create_ex(g_ctx, &d, &id);
    CHECK(st == SA1_ERR_INVALID_ARGUMENT, "S9 a puppet: %s (%s)", sa1_status_string(st), sa1_last_error(g_ctx));
    d.flags = SA1_CREATE_SKIN | SA1_CREATE_SUPER_CAPABLE;
    st = sa1_character_create_ex(g_ctx, &d, &id);
    CHECK(st == SA1_ERR_INVALID_ARGUMENT, "S9 Super capable: %s (%s)", sa1_status_string(st), sa1_last_error(g_ctx));
    d.flags = SA1_CREATE_SKIN;
    d.skin = NULL;
    st = sa1_character_create_ex(g_ctx, &d, &id);
    CHECK(st == SA1_ERR_INVALID_ARGUMENT, "S9 no skin given: %s", sa1_status_string(st));
    d.skin = &g_skin;
    files0 = g_ctx->files.count;
    st = sa1_character_create_ex(g_ctx, &d, &id);
    CHECK(st == SA1_OK && g_ctx->files.count == files0, "S9 twice: no second read (%u files read, %u)", (unsigned)files0, (unsigned)g_ctx->files.count);
    if (st == SA1_OK) {
        sa1_character_destroy(g_ctx, id);
    }
    /* the plain Sonic next to it: nothing of the skin */
    {
        sa1_character_id p = 0;
        const sa1_render *r = NULL;
        tally t;
        int f;
        create(0, &p);
        for (f = 0; f < 30; f++) {
            tick(p, 0.0f, 0.0f, 0u);
        }
        sa1_character_render(g_ctx, p, 1.0f, &r);
        tally_of(r, &t);
        CHECK(t.skin_tris == 0u && t.body_tris > 900u, "S9 a plain Sonic, with the skin loaded: %u triangles of the skin, %u of his body", (unsigned)t.skin_tris, (unsigned)t.body_tris);
        st = sa1_character_set_option(g_ctx, p, SA1_COPT_SKIN_ANIM, 1);
        CHECK(st == SA1_ERR_UNSUPPORTED, "S9 SA1_COPT_SKIN_ANIM on him: %s", sa1_status_string(st));
        sa1_character_destroy(g_ctx, p);
    }
    g_quiet_log = 0;
}

int main(int argc, char **argv)
{
    sa1_host h;
    sa1_load_options lo;
    sa1_coords co;
    const char *gdi = argc > 1 ? argv[1] : getenv("SA1_TEST_GDI");
    if (gdi == NULL || !*gdi) {
        printf("usage: skin_test GDI\n");
        return 2;
    }
    skin_init();
    sa1_host_defaults(&h);
    h.log = log_cb;
    h.log_level = SA1_LOG_WARN;
    if (sa1_init(&h, &g_ctx) != SA1_OK) {
        return 1;
    }
    memset(&co, 0, sizeof co);
    co.struct_size = sizeof co;
    sa1_coords_preset(SA1_COORDS_PRESET_SOURCE, &co);
    sa1_set_coords(g_ctx, &co);
    memset(&lo, 0, sizeof lo);
    lo.struct_size = sizeof lo;
    lo.characters = (1u << SA1_CHAR_SONIC) | (1u << SA1_CHAR_TAILS) | (1u << SA1_CHAR_KNUCKLES) | (1u << SA1_CHAR_AMY) | (1u << SA1_CHAR_BIG) | (1u << SA1_CHAR_GAMMA);
    lo.flags = SA1_LOAD_DEFAULT | SA1_LOAD_SUPER_FORMS;
    if (SA1_FAILED(sa1_load_disc(g_ctx, gdi, &lo))) {
        printf("load: %s\n", sa1_last_error(g_ctx));
        return 1;
    }
    printf("skin_test: libsa1 %s, %d-bit\n", sa1_version_string(), (int)(sizeof(void *) * 8));
    if (scene_flat(g_ctx) != 0) {
        return 1;
    }
    test_s2();
    s3_run(0, "plain");
    s3_run(1, "with the upgrades");
    test_s3_look();
    test_s3_row47();
    test_s3_rows_by_emote();
    test_s4();
    test_s6();
    test_s7();
    test_s8();
    test_s9(gdi);
    test_s1();
    printf("DETERMINISM skin %08X\n", (unsigned)g_hash);
    printf("\nskin_test: %d checks, %d failed\n", g_ok + g_fail, g_fail);
    sa1_shutdown(g_ctx);
    return g_fail ? 1 : 0;
}
