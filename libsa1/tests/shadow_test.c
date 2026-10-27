/*
 * shadow_test.c - the blob shadow's texture and size in libsa1 worlds (0.6.0 fixes: sadc docs/research/gsa1_060_shadow.md
 * L1 / L2 and its Review R.3.1 / R.3.5 / R.3.6 / R.3.10; owner report on G-SA1 0.6.0: "characters have no shadows").
 *
 * L1: the shadow texture setup 8C099500 is the game's (sadc src/1st_read/code_8C099500.c, routed; its boundary stub row
 * is gone from gen/policy/stubs.tsv) and the world boot stores the stage-start texlist B_8C79F0F0 the way the stage
 * start 8C04E490 -> 8C0990DE(mode) does (sa1_world_boot.c sa1g_boot_stage_texlist: OBJ_REGULAR 0x8C19F530; EFF_REGULAR
 * for levels 0x24 / 0x25). L2: OBJ_REGULAR is loaded as a common archive, before EFF_REGULAR (the stage start's order).
 * The placement, size, tilt and reach are the game's own code (8C0925A0, 8C09247E, 8C09222C); this test checks that the
 * draw it captures now carries the game's texture, and the size and reach rules the host then shows.
 *
 * Scene (the shadow research's, sadc build/research/shadow/shadow_probe.c): NATIVE coordinates (host = SA1 units), a
 * GRID floor of 100-unit quads at y = 0; characters start at (13.7, y, 7.3), off the quads' vertices and diagonals.
 *   1. the archives of Sonic, Tails, Knuckles, Amy, E-102 and Big: OBJ_REGULAR (96 textures, texlist 0x8C19F530) right
 *      after the character's own archives and EFF_REGULAR right after it, the last; OBJ_REGULAR's texture 58 is
 *      "shadow", 16 x 16 ARGB4444, black in every texel, its shape in the alpha (255 in the middle, 0 in the corners,
 *      4-bit steps). The boot's texlist: OBJ_REGULAR's for an ordinary level, EFF_REGULAR's for levels 0x24 / 0x25.
 *   2. every character standing (12 pairs): the last pair's capture holds exactly one blob record (Character_Shadow_Blob's
 *      model, sub step 0) with OBJ_REGULAR's texlist; its material texture 58 resolves into OBJ_REGULAR texture 58
 *      "shadow"; the render output's SA1_DRAW_SHADOW draw is textured with it (not SA1_DRAW_NO_TEXTURE). Its size is the
 *      game's: the record's matrix scales x by s and z by 0.8 s (an oval 10 s x 8 s), s = scl / (0.03 |y - y_bottom + 1|
 *      + 1) from sub step 0's position and shadow work (8C09247E; E-102: 8C09222C, 1.5 / (0.03 |y - ground| + 1)),
 *      centred at (x, ground + 0.3, z). (Review R.3.5: the state after a pair is sub step 1's; a wrapper around the
 *      world's step reads sub step 0's, the step that draws.)
 *   3. the other draws keep the texlists they had before L1 (Review R.3.10): each character runs standing, running and
 *      jumping inputs once as it boots and once with B_8C79F0F0 cleared after its boot (libsa1 0.6: the setup set no
 *      texlist); every record of every pair is the same in both runs but the blob's texlist (OBJ_REGULAR, then not).
 *   4. Sonic jumping: s follows the height on every pair (the formula of 2.), shrinking while he rises, growing while he
 *      falls, back to the rest size on landing.
 *   5. Sonic dropped from 150 units: a pair has a blob exactly when sub step 0's shadow work found ground (8C09247E's
 *      own test); none while he is more than 105 units above the floor (beyond the probe's reach on these 100-unit
 *      triangles, Review R.2.6: about 73), a blob again within 65 units.
 *
 *   shadow_test [--verbose] GDI          (build / run: tests/build_shadow.py --run)
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

#define PWP_SHADOW          0x1DCu      /* playerwk.shadow (include/sa1/player.h shadowwk): y_bottom +0x14, scl +0x20 */
#define ADDR_STAGE_TEXLIST  0x8C79F0F0u /* B_8C79F0F0, read by the shadow texture setup 8C099500 */
#define OBJ_REGULAR_TEXLIST 0x8C19F530u /* entry 0 of the stage start's list SA1_ADDR_PVM_COMMON (8C0990DE mode 0) */
#define SHADOW_TEX          58u         /* Character_Shadow_Blob's material texture (attribute 0x94318400) */
#define PHYS(a)             ((a) & 0x1FFFFFFFu)

static const int32_t k_kinds[6] = { SA1_CHAR_SONIC, SA1_CHAR_TAILS, SA1_CHAR_KNUCKLES, SA1_CHAR_AMY, SA1_CHAR_GAMMA,
                                    SA1_CHAR_BIG };
/* the character's own archives before the common ones (the game's PVM list 0x8C1C2584: SONIC SON_EFF / MILES /
 * KNUCKLES KNU_EFF / AMY AMY_EFF / E102 E102BEAM E102EFFECT E102TIME E102HIT / BIG FISHING BIG_KAERU) */
static const int32_t k_own[6] = { 2, 1, 2, 2, 5, 3 };
/* the blob's size factor: playerwk.shadow.scl = p.rad * 0.2 (PInitialize 8C06C300; rad of the physics table 0x8C1BDFE4:
 * 4.0, 3.5, 4.0, 3.5, -, 9.5); E-102: the fixed r 1.5 of 8C09222C */
static const double k_scl[6] = { 0.8, 0.7, 0.8, 0.7, 1.5, 1.9 };

static void log_cb(void *user, int32_t level, const char *msg)
{
    (void)user;
    if (level >= SA1_LOG_WARN)
        printf("    [libsa1 %s] %s\n", level >= SA1_LOG_ERROR ? "error" : "warn", msg);
}

/* ---- the scene: a GRID floor of 100-unit quads at y = 0 ---- */
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
static float g_pos0[3], g_ybottom0, g_scl0;
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
        const uint8_t *s = (const uint8_t *)sa1g_world_ptr(w, pp.pwp + PWP_SHADOW);
        if (t && s) {
            memcpy(g_pos0, t, sizeof g_pos0);
            memcpy(&g_ybottom0, s + 0x14, sizeof g_ybottom0);
            memcpy(&g_scl0, s + 0x20, sizeof g_scl0);
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

/* ---- context, characters, ticks ---- */
static sa1_context *mk_ctx(const char *gdi)
{
    static tbuf B;
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
    for (k = 0; k < 6u; k++)
        lo.characters |= 1u << k_kinds[k];
    lo.flags = SA1_LOAD_DEFAULT;
    scene(&B);
    if (SA1_FAILED(sa1_load_disc(ctx, gdi, &lo)) || sa1_collision_set_static(ctx, B.t, B.n) != SA1_OK) {
        printf("  context: %s\n", sa1_last_error(ctx));
        sa1_shutdown(ctx);
        return NULL;
    }
    return ctx;
}

static sa1_character_id mk_char_at(sa1_context *ctx, int32_t kind, float y, int32_t level)
{
    sa1_character_desc d;
    sa1_character_id id = 0;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.kind = kind;
    d.flags = SA1_CREATE_WORLD | (level >= 0 ? SA1_CREATE_STAGE : 0u);
    d.level = level >= 0 ? level : 0;
    d.act = 0;
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

static sa1_character_id mk_char(sa1_context *ctx, int32_t kind, float y)
{
    return mk_char_at(ctx, kind, y, -1);
}

static int tick(sa1_context *ctx, sa1_character_id id, uint32_t buttons, float stick_y)
{
    sa1_input in;
    memset(&in, 0, sizeof in);
    in.struct_size = sizeof in;
    in.buttons = buttons;
    in.stick_y = stick_y;
    in.camera_yaw = 0.0f;
    if (sa1_character_tick(ctx, id, &in, 1.0f / 30.0f, NULL, NULL) != SA1_OK) {
        printf("  tick: %s\n", sa1_last_error(ctx));
        return -1;
    }
    return 0;
}

static uint32_t world_word(sa1_context *ctx, sa1_character_id id, uint32_t g)
{
    sa1g_world *w = sa1_character_world(ctx, id);
    uint32_t v = 0xFFFFFFFFu;
    if (!w || sa1g_world_rd32(w, g, &v) != 0)
        return 0xFFFFFFFFu;
    return v;
}

/* ---- the archives ---- */
typedef struct arcs {
    int32_t obj, eff, n;
    int32_t con;                /* round 0.9.1 (the HUD, sadc docs/research/gsa1_070_hud.md F5): CON_REGULAR after EFF_REGULAR */
    uint32_t obj_textures, obj_texlist, eff_texlist;
} arcs;
static arcs g_arc[SA1_CHAR_COUNT];

static void find_archives(sa1_context *ctx, int32_t kind, arcs *a)
{
    const sa1_charset *cs = &ctx->chars[kind];
    uint32_t i, nt;
    const char *nm;
    memset(a, 0, sizeof *a);
    a->obj = a->eff = a->con = -1;
    for (i = 0; i < 16u; i++) {
        nm = NULL;
        nt = 0;
        if (sa1_texture_archive(ctx, kind, i, &nm, &nt) != SA1_OK)
            break;
        a->n++;
        if (nm && strcmp(nm, "OBJ_REGULAR") == 0) {
            a->obj = (int32_t)i;
            a->obj_textures = nt;
            a->obj_texlist = i < cs->num_archives ? cs->archives[i].texlist_addr : 0u;
        }
        if (nm && strcmp(nm, "EFF_REGULAR") == 0) {
            a->eff = (int32_t)i;
            a->eff_texlist = i < cs->num_archives ? cs->archives[i].texlist_addr : 0u;
        }
        if (nm && strcmp(nm, "CON_REGULAR") == 0) {
            a->con = (int32_t)i;
        }
    }
}

/* OBJ_REGULAR texture 58: "shadow", 16 x 16 ARGB4444, black, the shape in the alpha */
static int shadow_texture(sa1_context *ctx, int32_t kind, int32_t arc, char *why, size_t n)
{
    sa1_texture_info ti;
    uint32_t i, rgb = 0, odd = 0, distinct = 0, w, h;
    uint8_t seen[256], centre, corners;
    memset(&ti, 0, sizeof ti);
    ti.struct_size = sizeof ti;
    memset(seen, 0, sizeof seen);
    if (arc < 0 || sa1_texture_get(ctx, kind, (uint32_t)arc, SHADOW_TEX, &ti) != SA1_OK || !ti.rgba || !ti.name) {
        snprintf(why, n, "texture %u of archive %d not available", SHADOW_TEX, (int)arc);
        return 0;
    }
    w = ti.width;
    h = ti.height;
    for (i = 0; i < w * h; i++) {
        const uint8_t *p = ti.rgba + 4u * i;
        rgb |= (uint32_t)p[0] | p[1] | p[2];
        odd += p[3] % 17u != 0;
        if (!seen[p[3]]) {
            seen[p[3]] = 1;
            distinct++;
        }
    }
    centre = w >= 2 && h >= 2 ? ti.rgba[4u * ((h / 2u) * w + w / 2u) + 3u] : 0;
    corners = (uint8_t)(ti.rgba[3] | ti.rgba[4u * (w - 1u) + 3u] | ti.rgba[4u * ((h - 1u) * w) + 3u] |
                        ti.rgba[4u * (h * w - 1u) + 3u]);
    snprintf(why, n, "\"%s\" %ux%u pixel format %u, RGB %s, %u alpha values (%s 4-bit steps), centre %u, corners %u",
             ti.name, (unsigned)w, (unsigned)h, (unsigned)ti.pixel_format, rgb ? "not black" : "black in every texel",
             (unsigned)distinct, odd ? "not all" : "all", (unsigned)centre, (unsigned)corners);
    return strcmp(ti.name, "shadow") == 0 && w == 16u && h == 16u && ti.pixel_format == 2u && rgb == 0 && odd == 0 &&
           distinct >= 3u && centre == 255u && corners == 0u && ti.alpha == SA1_TEXTURE_ALPHA_BLEND;
}

/* 1. the archives and the boot's texlist */
static void test_archives(sa1_context *ctx)
{
    static const int32_t levels[3] = { 1, 0x24, 0x25 };
    uint32_t k;
    printf("1. the common archives and the stage-start texlist:\n");
    for (k = 0; k < 6u; k++) {
        int32_t kind = k_kinds[k];
        arcs *a = &g_arc[kind];
        char why[160];
        int tex_ok;
        find_archives(ctx, kind, a);
        tex_ok = shadow_texture(ctx, kind, a->obj, why, sizeof why);
        CHECK(a->obj == k_own[k] && a->eff == a->obj + 1 && a->con == a->eff + 1 && a->n == a->con + 1 &&
                  a->n <= (int32_t)SA1_MAX_ARCHIVES && a->obj_textures == 96u && a->obj_texlist == OBJ_REGULAR_TEXLIST &&
                  tex_ok,
              "%s: %d archives, OBJ_REGULAR %d (after its %d own; %u textures, texlist 0x%08X), EFF_REGULAR %d, "
              "CON_REGULAR %d (the last, round 0.9.1); texture %u: %s", sa1_character_name(kind), (int)a->n, (int)a->obj,
              (int)k_own[k], (unsigned)a->obj_textures, (unsigned)a->obj_texlist, (int)a->eff, (int)a->con, SHADOW_TEX,
              why);
    }
    for (k = 0; k < 3u; k++) {
        sa1_character_id id = mk_char_at(ctx, SA1_CHAR_SONIC, 2.0f, levels[k]);
        uint32_t want = levels[k] == 0x24 || levels[k] == 0x25 ? g_arc[SA1_CHAR_SONIC].eff_texlist : OBJ_REGULAR_TEXLIST;
        uint32_t got = id ? world_word(ctx, id, ADDR_STAGE_TEXLIST) : 0xFFFFFFFFu;
        CHECK(id && got == want && want != 0u,
              "level 0x%02X: the boot stores B_8C79F0F0 = 0x%08X (8C0990DE(%d): %s's texlist 0x%08X)", (unsigned)levels[k],
              (unsigned)got, levels[k] == 0x24 || levels[k] == 0x25 ? 2 : 0,
              levels[k] == 0x24 || levels[k] == 0x25 ? "EFF_REGULAR" : "OBJ_REGULAR", (unsigned)want);
        if (id)
            sa1_character_destroy(ctx, id);
    }
}

/* ---- the blob in a capture snapshot ---- */
typedef struct blobrec {
    int n;                          /* blob records of the snapshot */
    const sa1_cap_rec *r;
    double s[3];                    /* the matrix's column lengths: x, y, z scale */
    double t[3];                    /* translation */
} blobrec;

static void blob_rec(sa1_capture *cap, blobrec *b)
{
    const sa1_cap_rec *recs = NULL;
    uint32_t n, i, model = sa1_capture_shadow_model(cap);
    memset(b, 0, sizeof *b);
    n = cap ? sa1_capture_records(cap, &recs) : 0u;
    for (i = 0; model && i < n; i++) {
        if (SA1_CAP_IS_MODEL(recs[i].kind) && PHYS(recs[i].obj) == PHYS(model)) {
            b->n++;
            b->r = &recs[i];
        }
    }
    if (b->n == 1) {
        double m[12];
        int c;
        sa1_capture_world_matrix(b->r, m);
        for (c = 0; c < 3; c++) {
            b->s[c] = sqrt(m[c] * m[c] + m[4 + c] * m[4 + c] + m[8 + c] * m[8 + c]);
            b->t[c] = m[4 * c + 3];
        }
    }
}

/* the blob model's material texture id, read from the world as the draw read it */
static uint32_t blob_texid(sa1_capture *cap)
{
    uint32_t model = sa1_capture_shadow_model(cap), mats, v;
    const uint8_t *p = model ? sa1_capture_read(cap, model + 0x10u, 4) : NULL;
    if (!p)
        return 0xFFFFFFFFu;
    memcpy(&mats, p, 4);
    p = sa1_capture_read(cap, mats + 0x0Cu, 4);
    if (!p)
        return 0xFFFFFFFFu;
    memcpy(&v, p, 4);
    return v & 0x3FFFFFFFu;
}

/* the game's size: 8C09247E s = scl / (0.03 |y - y_bottom + 1| + 1); E-102 (8C09222C) r / (0.03 |y - ground| + 1) */
static double expected_s(int gamma, double scl, double y, double ground)
{
    return gamma ? scl / (0.03 * fabs(y - ground) + 1.0) : scl / (0.03 * fabs(y - ground + 1.0) + 1.0);
}

/* 2. standing: texture and size */
static void test_standing(sa1_context *ctx)
{
    uint32_t k, i;
    printf("2. standing on the floor, 12 pairs (the last pair's blob):\n");
    for (k = 0; k < 6u; k++) {
        int32_t kind = k_kinds[k];
        int gamma = kind == SA1_CHAR_GAMMA;
        sa1_character_id id = mk_char(ctx, kind, 2.0f);
        uint32_t boot_tl = id ? world_word(ctx, id, ADDR_STAGE_TEXLIST) : 0u, texid;
        sa1_capture *cap;
        blobrec b;
        sa1_texture_info ti;
        int32_t arc = -1, tex = -1;
        double ground, s_exp, dc, scl;
        int ok = id != 0;
        if (ok)
            wrap(ctx, id);
        for (i = 0; ok && i < 12; i++)
            ok = tick(ctx, id, 0u, 0.0f) == 0;
        cap = ok ? sa1_character_capture(ctx, id) : NULL;
        if (!cap) {
            CHECK(0, "%s: character / ticks / capture", sa1_character_name(kind));
            if (id)
                sa1_character_destroy(ctx, id);
            continue;
        }
        blob_rec(cap, &b);
        texid = blob_texid(cap);
        memset(&ti, 0, sizeof ti);
        ti.struct_size = sizeof ti;
        if (b.n == 1)
            tex = sa1_capture_resolve(cap, b.r->texlist, texid, &arc);
        if (tex >= 0 && arc >= 0)
            sa1_texture_get(ctx, kind, (uint32_t)arc, (uint32_t)tex, &ti);
        CHECK(boot_tl == OBJ_REGULAR_TEXLIST && b.n == 1 && b.r->sub == 0 && b.r->texlist == OBJ_REGULAR_TEXLIST &&
                  texid == SHADOW_TEX && arc == g_arc[kind].obj && tex == (int32_t)SHADOW_TEX && ti.name &&
                  strcmp(ti.name, "shadow") == 0,
              "%s: B_8C79F0F0 0x%08X after the boot; %d blob record(s), sub step %d, texlist 0x%08X (OBJ_REGULAR "
              "0x%08X), material texture %u -> archive %d (OBJ_REGULAR %d) texture %d \"%s\"", sa1_character_name(kind),
              (unsigned)boot_tl, b.n, b.n == 1 ? (int)b.r->sub : -1, b.n == 1 ? (unsigned)b.r->texlist : 0u,
              (unsigned)OBJ_REGULAR_TEXLIST, (unsigned)texid, (int)arc, (int)g_arc[kind].obj, (int)tex,
              ti.name ? ti.name : "-");
        if (b.n == 1 && g_have0) {
            /* E-102's ground is the one 8C09222C returns (not in the shadow work): the draw's own, here the floor */
            ground = gamma ? b.t[1] - 0.3 : (double)g_ybottom0;
            scl = gamma ? k_scl[k] : (double)g_scl0;
            s_exp = expected_s(gamma, scl, g_pos0[1], ground);
            dc = sqrt((b.t[0] - g_pos0[0]) * (b.t[0] - g_pos0[0]) + (b.t[1] - (ground + 0.3)) * (b.t[1] - (ground + 0.3)) +
                      (b.t[2] - g_pos0[2]) * (b.t[2] - g_pos0[2]));
            CHECK(fabs(scl - k_scl[k]) < 1e-5 && fabs(b.s[0] - s_exp) < 1e-4 && fabs(b.s[1] - s_exp) < 1e-4 &&
                      fabs(b.s[2] - 0.8 * s_exp) < 1e-4 && dc < 1e-3 && fabs(ground) < 1e-3,
                  "%s: scale %.5f / %.5f / %.5f = s, s, 0.8 s with s = %.5f (scl %.3f, %.3f above the ground at %.4f): an "
                  "oval %.3f x %.3f units; centre (%.3f %.3f %.3f), sub step 0's (x, ground + 0.3, z) within %.5f",
                  sa1_character_name(kind), b.s[0], b.s[1], b.s[2], s_exp, scl, (double)g_pos0[1] - ground, ground,
                  10.0 * b.s[0], 10.0 * b.s[2], b.t[0], b.t[1], b.t[2], dc);
        } else {
            CHECK(0, "%s: no blob record or no sub step 0 state (%d records, have %d)", sa1_character_name(kind), b.n,
                  g_have0);
        }
#if defined(SA1_DRAW_SHADOW)
        {
            const sa1_render *r = NULL;
            const sa1_draw *sd = NULL;
            uint32_t d, nshadow = 0;
            if (sa1_character_render(ctx, id, 1.0f, &r) == SA1_OK) {
                for (d = 0; d < r->num_draws; d++) {
                    if (r->draws[d].flags & SA1_DRAW_SHADOW) {
                        nshadow++;
                        sd = &r->draws[d];
                    }
                }
            }
            CHECK(nshadow == 1 && sd->archive == g_arc[kind].obj && sd->texture == (int32_t)SHADOW_TEX &&
                      !(sd->flags & SA1_DRAW_NO_TEXTURE),
                  "%s: the render output's SA1_DRAW_SHADOW draw (%u) is textured with OBJ_REGULAR's \"shadow\": archive %d "
                  "texture %d, flags 0x%04X", sa1_character_name(kind), (unsigned)nshadow, sd ? (int)sd->archive : -1,
                  sd ? (int)sd->texture : -1, sd ? (unsigned)sd->flags : 0u);
        }
#endif
        unwrap(ctx, id);
        sa1_character_destroy(ctx, id);
    }
}

/* 3. the other draws keep their texlists (Review R.3.10) */
#define TWIN_PAIRS 36u
typedef struct run {
    sa1_cap_rec *rec;               /* TWIN_PAIRS x SA1_CAP_MAX */
    uint32_t n[TWIN_PAIRS];
    uint32_t model;
} run;

static void twin_input(uint32_t p, uint32_t *buttons, float *stick)
{
    *buttons = 0u;
    *stick = 0.0f;
    if (p >= 6u && p < 22u)
        *stick = 1.0f;                                  /* run */
    if (p >= 22u && p < 28u) {
        *stick = 1.0f;
        *buttons = SA1_BUTTON_JUMP;                     /* jump forward */
    }
}

static int twin_run(sa1_context *ctx, int32_t kind, int clear, run *out)
{
    sa1_character_id id = mk_char(ctx, kind, 2.0f);
    uint32_t p;
    if (!id)
        return -1;
    if (clear) {
        sa1g_world *w = sa1_character_world(ctx, id);
        if (!w || sa1g_world_wr32(w, ADDR_STAGE_TEXLIST, 0u) != 0) {
            sa1_character_destroy(ctx, id);
            return -1;
        }
    }
    for (p = 0; p < TWIN_PAIRS; p++) {
        const sa1_cap_rec *recs = NULL;
        sa1_capture *cap;
        uint32_t buttons, n;
        float stick;
        twin_input(p, &buttons, &stick);
        if (tick(ctx, id, buttons, stick) != 0) {
            sa1_character_destroy(ctx, id);
            return -1;
        }
        cap = sa1_character_capture(ctx, id);
        n = cap ? sa1_capture_records(cap, &recs) : 0u;
        if (n > SA1_CAP_MAX)
            n = SA1_CAP_MAX;
        out->n[p] = n;
        if (n)
            memcpy(out->rec + (size_t)p * SA1_CAP_MAX, recs, n * sizeof *recs);
        out->model = cap ? sa1_capture_shadow_model(cap) : 0u;
    }
    sa1_character_destroy(ctx, id);
    return 0;
}

static void test_twin(sa1_context *ctx)
{
    static run A, B;
    uint32_t k, p, i;
    printf("3. the other draws keep their texlists (each character with and without B_8C79F0F0, %u pairs of standing, "
           "running and jumping):\n", (unsigned)TWIN_PAIRS);
    A.rec = (sa1_cap_rec *)calloc((size_t)TWIN_PAIRS * SA1_CAP_MAX, sizeof(sa1_cap_rec));
    B.rec = (sa1_cap_rec *)calloc((size_t)TWIN_PAIRS * SA1_CAP_MAX, sizeof(sa1_cap_rec));
    if (!A.rec || !B.rec) {
        CHECK(0, "memory");
        free(A.rec);
        free(B.rec);
        return;
    }
    for (k = 0; k < 6u; k++) {
        int32_t kind = k_kinds[k];
        uint32_t count_diff = 0, field_diff = 0, tl_diff = 0, blobs = 0, blob_ok = 0, records = 0, pairs_blob = 0;
        uint32_t first_bad = 0xFFFFFFFFu;
        if (twin_run(ctx, kind, 0, &A) != 0 || twin_run(ctx, kind, 1, &B) != 0) {
            CHECK(0, "%s: the two runs", sa1_character_name(kind));
            continue;
        }
        for (p = 0; p < TWIN_PAIRS; p++) {
            uint32_t pb = 0;
            if (A.n[p] != B.n[p]) {
                count_diff++;
                if (first_bad == 0xFFFFFFFFu)
                    first_bad = p;
                continue;
            }
            for (i = 0; i < A.n[p]; i++) {
                sa1_cap_rec a = A.rec[(size_t)p * SA1_CAP_MAX + i], b = B.rec[(size_t)p * SA1_CAP_MAX + i];
                int blob = SA1_CAP_IS_MODEL(a.kind) && A.model && PHYS(a.obj) == PHYS(A.model);
                uint32_t ta = a.texlist, tb = b.texlist;
                records++;
                a.texlist = b.texlist = 0;
                if (memcmp(&a, &b, sizeof a) != 0) {
                    field_diff++;
                    if (first_bad == 0xFFFFFFFFu)
                        first_bad = p;
                }
                if (blob) {
                    blobs++;
                    pb++;
                    blob_ok += ta == OBJ_REGULAR_TEXLIST && tb != OBJ_REGULAR_TEXLIST;
                } else if (ta != tb) {
                    tl_diff++;
                    if (g_verbose)
                        printf("    pair %u record %u kind %u obj 0x%08X: texlist 0x%08X, 0x%08X before\n", (unsigned)p,
                               (unsigned)i, (unsigned)a.kind, (unsigned)a.obj, (unsigned)ta, (unsigned)tb);
                    if (first_bad == 0xFFFFFFFFu)
                        first_bad = p;
                }
            }
            pairs_blob += pb >= 1u;
        }
        CHECK(count_diff == 0 && field_diff == 0 && tl_diff == 0 && blobs >= TWIN_PAIRS - 8u && blob_ok == blobs,
              "%s: %u records in %u pairs: %u pairs with another record count, %u records with other fields, %u non-blob "
              "records with another texlist (first at pair %d); %u blob records in %u pairs, %u with OBJ_REGULAR's "
              "texlist where the run without B_8C79F0F0 has another", sa1_character_name(kind), (unsigned)records,
              (unsigned)TWIN_PAIRS, (unsigned)count_diff, (unsigned)field_diff, (unsigned)tl_diff,
              first_bad == 0xFFFFFFFFu ? -1 : (int)first_bad, (unsigned)blobs, (unsigned)pairs_blob, (unsigned)blob_ok);
    }
    free(A.rec);
    free(B.rec);
    A.rec = B.rec = NULL;
}

/* 4. Sonic jumping: the size follows the height */
static void test_jump(sa1_context *ctx)
{
    sa1_character_id id = mk_char(ctx, SA1_CHAR_SONIC, 2.0f);
    uint32_t i, pairs = 0, formula = 0, against = 0, airborne = 0, landed_ok = 0;
    double rest = 0.8 / 1.03, smin = 1e30, hmax = 0.0, prev_s = -1.0, prev_h = 0.0, last_s = 0.0;
    printf("4. Sonic jumping (the blob's size by height):\n");
    if (id)
        wrap(ctx, id);
    for (i = 0; id && i < 12; i++)
        tick(ctx, id, 0u, 0.0f);
    for (i = 0; id && i < 50; i++) {
        blobrec b;
        double h, s_exp;
        if (tick(ctx, id, i < 10 ? SA1_BUTTON_JUMP : 0u, 0.0f) != 0)
            break;
        blob_rec(sa1_character_capture(ctx, id), &b);
        if (b.n != 1 || !g_have0)
            continue;
        pairs++;
        h = (double)g_pos0[1] - (double)g_ybottom0;
        s_exp = expected_s(0, g_scl0, g_pos0[1], g_ybottom0);
        formula += fabs(b.s[0] - s_exp) < 1e-4 && fabs(b.s[2] - 0.8 * s_exp) < 1e-4 && fabs(b.t[1] - g_ybottom0 - 0.3) < 1e-3;
        if (prev_s >= 0.0 && fabs(h - prev_h) > 1e-3)
            against += (h > prev_h) == (b.s[0] < prev_s);       /* higher -> smaller, lower -> larger */
        else if (prev_s >= 0.0)
            against++;
        airborne += h > 1.0;
        if (h > hmax)
            hmax = h;
        if (b.s[0] < smin)
            smin = b.s[0];
        if (g_verbose)
            printf("    pair %2u: height %7.3f, s %.5f (game %.5f)\n", (unsigned)i, h, b.s[0], s_exp);
        prev_s = b.s[0];
        prev_h = h;
        last_s = b.s[0];
    }
    landed_ok = fabs(last_s - rest) < 1e-4;
    CHECK(pairs >= 45 && formula == pairs && against + 1u == pairs && airborne >= 10 && hmax > 10.0 &&
              smin < rest - 0.05 && landed_ok,
          "Sonic's jump: a blob in %u of 50 pairs, the game's size in %u, smaller when higher / larger when lower in %u "
          "of %u steps; %u pairs in the air, %.2f units high at most, s %.4f at rest -> %.4f at the top -> %.4f after "
          "landing", (unsigned)pairs, (unsigned)formula, (unsigned)against, pairs ? (unsigned)pairs - 1u : 0u,
          (unsigned)airborne, hmax, rest, smin, last_s);
    if (id) {
        unwrap(ctx, id);
        sa1_character_destroy(ctx, id);
    }
}

/* 5. Sonic dropped from 150 units: the probe's reach */
static void test_reach(sa1_context *ctx)
{
    sa1_character_id id = mk_char(ctx, SA1_CHAR_SONIC, 150.0f);
    uint32_t i, pairs = 0, agree = 0, high = 0, high_with = 0, low = 0, low_without = 0;
    double first_h = -1.0, last_none_h = -1.0;
    printf("5. Sonic dropped from 150 units (the probe's reach):\n");
    if (id)
        wrap(ctx, id);
    for (i = 0; id && i < 60; i++) {
        blobrec b;
        double h;
        int found;
        if (tick(ctx, id, 0u, 0.0f) != 0 || !g_have0)
            break;
        blob_rec(sa1_character_capture(ctx, id), &b);
        h = (double)g_pos0[1];                          /* the floor is at y = 0 */
        found = g_ybottom0 > g_pos0[1] - 20000.0f;      /* 8C09247E's test */
        pairs++;
        agree += (b.n == 1) == found && b.n <= 1;
        if (h > 105.0) {
            high++;
            high_with += b.n != 0;
        }
        if (h <= 65.0) {
            low++;
            low_without += b.n == 0;
        }
        if (b.n == 0)
            last_none_h = h;
        else if (first_h < 0.0 && last_none_h >= 0.0)
            first_h = h;
        if (g_verbose)
            printf("    pair %2u: %8.3f above the floor, ground %s, %d blob record(s)\n", (unsigned)i, h,
                   found ? "found" : "not found", b.n);
    }
    CHECK(pairs == 60 && agree == pairs && high > 0 && high_with == 0 && low > 0 && low_without == 0 && first_h > 0.0,
          "%u pairs: a blob exactly when sub step 0 found ground in %u; above 105 units %u pairs, %u with a blob; within "
          "65 units %u pairs, %u without; the blob came back at %.2f units (the last pair without one at %.2f)",
          (unsigned)pairs, (unsigned)agree, (unsigned)high, (unsigned)high_with, (unsigned)low, (unsigned)low_without,
          first_h, last_none_h);
    if (id) {
        unwrap(ctx, id);
        sa1_character_destroy(ctx, id);
    }
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
        printf("usage: shadow_test [--verbose] GDI (or SA1_TEST_GDI)\n");
        return 2;
    }
    printf("shadow_test: the blob shadow's texture and size (gsa1_060_shadow.md L1 / L2), NATIVE GRID floor:\n");
    ctx = mk_ctx(gdi);
    if (!ctx) {
        CHECK(0, "context");
        return 1;
    }
    test_archives(ctx);
    test_standing(ctx);
    test_twin(ctx);
    test_jump(ctx);
    test_reach(ctx);
    sa1_shutdown(ctx);
    printf("shadow_test: %s (%d failed)\n", g_fail ? "FAIL" : "ok", g_fail);
    return g_fail ? 1 : 0;
}
