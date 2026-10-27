/*
 * gold_test.c - round 0.9.4a: the golden Light Speed charge in libsa1 (include/libsa1.h SA1_SUPOPT_LSD_GOLD,
 * SA1_DRAW_SUPER_GOLD, sa1_texture_variant_get / SA1_TEXTURE_VARIANT_SUPER_GOLD; sadc docs/research/super2_plan.md 6.2 with
 * its Review R.4 / R.7 / R.10 / R.11 and the owner answers O3 / O4 of 10.1; super2_lsd.md 2.2, 4.3, 4.4).
 *
 *   gold_test [--only NAME] [--verbose] GDI
 *
 * Built and run by tests/build_gold.py (a snapshot of build/<target>/libsa1.a and its headers taken under libsa1's build
 * lock). The context is in SA1 space (SA1_COORDS_PRESET_NATIVE), GRID floor (sa1_collision_set_static), the disc loaded with
 * Sonic, Super Sonic and Tails and SA1_LOAD_SUPER_FORMS (as super_test). SA1_GUARD_ALLOC=1 puts libsa1's heap blocks on the
 * page-guard allocator (tests/guard_alloc.h).
 *
 *   variant    (1) sa1_texture_variant_get of SON_EFF lcharge01-04 (found by name): the size and formats of sa1_texture_get,
 *              alpha texel for texel, HSV value and saturation within 1/255, the hue 45 degrees (within the 8-bit
 *              quantisation, and +-1 where max - min >= 30), every texel equal (+-1) to an independent HSV computation,
 *              the alpha x value weighted means within +-3 of super2_lsd.md 4.3; NOT_FOUND for s_ball_a, a body texture,
 *              variants 0 and 2, Tails, Super Sonic (kind 8) and an archive out of range; two calls, one pixel pointer
 *   lsd_gold   (2) GRID General worlds (SA1_SUPER_START | NO_DRAIN, SA1_SUPOPT_SUPER_EFFECTS 1) driven in lockstep with
 *              SA1_SUPOPT_LSD_GOLD 0 and 1: 8 pairs standing, the action button held 130 pairs, released and pushed 40
 *              pairs (no target: mode 6). (A) the owner's three upgrades (shoes, Crystal Ring, Ancient Light: equipment
 *              0x8007; review R.10): the Crystal Ring's green sparkles / streaks (lcharge04 / 03) keep the game's texture
 *              and carry no flag (owner answer O3); the glow's vertices are the complement of option 0's (pass 1 (0,
 *              25..71, 255) -> (255, 184..230, 0)) and the glow carries the flag; (B) the shoes alone: the plain sparkles /
 *              streaks (lcharge02 / 01) carry the flag with archive and texture unchanged, the glow as in (A);
 *              SA1_ORDER_NORMAL at hold pair 118 with the option still 1: no flag from the second pair after it. Every
 *              other draw, vertex, sprite and line identical; option 0 never flags. One list hash per run
 *   identity   (3) both scenarios: the player state (sa1_state_ex, every byte) per pair, the rand state and the arena hash
 *              after the script identical with 0 and 1 (M2 / M3 touch no world memory)
 *   merges     (4) the shoes-alone run with the option 1, every pair: the character's capture re-emitted into a builder of
 *              the test (render_capture's options); per draw, every triangle's own texture / material (written per record
 *              before any merge) says gold exactly when the draw is flagged, every sprite draw is flagged exactly when it
 *              draws lcharge02, and the capture's counters agree (flagged sprites, polygon draws, glow models); the run
 *              must hold a flagged sprite, polygon and glow draw in pairs that also draw unflagged additive effects (the
 *              dust, the Super aura), else the case fails as vacuous (review R.11)
 *   unchanged  (5) in (2): the dash's trail (the Super clones, SUPERSONIC ss_dash), the Super aura (s1sonic / s2sonic) and
 *              the body draws present, unflagged and identical; a kind-0 Sonic that is not General and kind 8: the bridge
 *              refuses SA1_SUPOPT_LSD_GOLD (SA1_ERR_UNSUPPORTED) and a plain Sonic's charge draws nothing flagged
 *   memory     (6) a context of its own on a counting host (the guard heap under SA1_GUARD_ALLOC=1): Sonic loaded without
 *              the forms has no variant (SA1_ERR_NOT_FOUND); loaded with the forms and unloaded three times: the variants
 *              there each time, the live blocks after each unload the same, 0 after sa1_shutdown
 *
 * API NOTES (round 0.9.4a, 094a-lib-gold; for 094a-gsa1 and the README):
 *   sa1_texture_variant_get(ctx, kind, archive, index, variant, &info): variant SA1_TEXTURE_VARIANT_SUPER_GOLD (1) of
 *     Sonic's (kind 0) SON_EFF lcharge01-04, made when the disc is loaded with SA1_LOAD_SUPER_FORMS (sa1_texture.c; in the
 *     character set's arena, freed by sa1_unload_disc / sa1_shutdown; never written to disk). info as sa1_texture_get fills
 *     it (same width / height / formats / name / alpha kind; rgba = the variant's pixels, a stable pointer). Anything else
 *     (another kind, archive, texture or variant, Sonic loaded without the forms): SA1_ERR_NOT_FOUND; NULL ctx / out or a
 *     kind out of range: SA1_ERR_INVALID_ARGUMENT; Sonic not loaded: SA1_ERR_NOT_LOADED.
 *   SA1_DRAW_SUPER_GOLD (0x2000): while SA1_SUPOPT_LSD_GOLD is 1 and the General look drew the pair, the PLAIN charge's
 *     sparkles (SPRITES, SON_EFF lcharge02) and streaks (TRIANGLES from a polygon draw, lcharge01) carry it: draw the
 *     texture's variant in place of the texture (archive / texture stay the game's). The Crystal Ring's lcharge04 / 03 are
 *     never flagged (owner answer O3: they stay green). The charged glow (TRIANGLES, untextured, material 0x26182400,
 *     source MODEL) carries it too and its vertex colours are already gold (max + min - c of the game's per channel,
 *     alpha kept): nothing to do for it. A flagged draw never merges with an unflagged one. Measured means (alpha x value
 *     weighted RGB of the variants) are printed by the variant case; the plan's: lcharge01 (142, 107, 2), lcharge02 (192,
 *     164, 79), lcharge03 (135, 103, 5), lcharge04 (177, 148, 61).
 *   README line: the game has no golden Light Speed effects: G-SA1 colours them from the game's own Super gold (the plain
 *     charge's sparkles and streaks get a golden texture made by libsa1, the charged glow the complementary colours of
 *     the game's blue); with the Crystal Ring the sparkles and streaks stay the game's green (owner's choice).
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

/* the page-guard allocator (tests/guard_alloc.h): SA1_GUARD_ALLOC=1 puts libsa1's heap blocks against no-access pages */
#include "guard_alloc.c"
#define sa1_host_defaults(h) (sa1_host_defaults(h), ga_host(h))

static int g_verbose, g_fail, g_ok, g_skip;
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

#define A_EVENT_FLAGS   0x8C799920u     /* Uint8 per flag: 0x8D the Light Speed Shoes, 0x8E the Crystal Ring, 0x9F the
                                           Ancient Light (as super_test's give_upgrades) */
#define GLOW_MATERIAL   0x26182400u     /* the S_MOT aura material's attribute (lightspeed_visuals.md 3.2) */

static uint32_t rd32(const sa1g_world *w, uint32_t g)
{
    uint32_t v = 0;
    sa1g_world_rd32(w, g, &v);
    return v;
}

static void wr8w(sa1g_world *w, uint32_t g, uint32_t v)
{
    uint32_t a = g & ~3u, sh = 8u * (g & 3u), o = rd32(w, a);
    sa1g_world_wr32(w, a, (o & ~(0xFFu << sh)) | ((v & 0xFFu) << sh));
}

/* ---- the scene and the characters (as super_test) --------------------------------------------------------------------- */
static int scene_grid(sa1_context *ctx)
{
    sa1_collision_tri t[2];
    static const float p[4][3] = { { -3000.0f, 0.0f, -3000.0f }, { 3000.0f, 0.0f, -3000.0f }, { 3000.0f, 0.0f, 3000.0f },
                                   { -3000.0f, 0.0f, 3000.0f } };
    sa1_status st;
    memset(t, 0, sizeof t);
    /* a flat floor at y = 0, facing up (the winding super_test's tb_quad picks for the normal (0, 1, 0)) */
    memcpy(t[0].v[0], p[0], 12);
    memcpy(t[0].v[1], p[2], 12);
    memcpy(t[0].v[2], p[1], 12);
    memcpy(t[1].v[0], p[0], 12);
    memcpy(t[1].v[1], p[3], 12);
    memcpy(t[1].v[2], p[2], 12);
    t[0].surface = t[1].surface = SA1_SURFACE_SOLID;
    st = sa1_collision_set_static(ctx, t, 2);
    if (SA1_FAILED(st)) {
        printf("  set_static: %s: %s\n", sa1_status_string(st), sa1_last_error(ctx));
        return -1;
    }
    return 0;
}

static sa1_character_id mk(sa1_context *ctx, int32_t kind, const float pos[3], uint32_t flags, int32_t rings,
                           int super_given, uint32_t super_flags)
{
    sa1_character_desc d;
    sa1_character_id id = 0;
    sa1_status st;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.kind = kind;
    d.flags = flags | SA1_CREATE_WORLD | SA1_CREATE_SA1_SPACE | SA1_CREATE_ANGLES;
    memcpy(d.position, pos, sizeof d.position);
    if (rings >= 0) {
        d.flags |= SA1_CREATE_START_RINGS;
        d.start_rings = rings;
    }
    if (super_given) {
        d.flags |= SA1_CREATE_SUPER;
        d.super_flags = super_flags;
    }
    st = sa1_character_create_ex(ctx, &d, &id);
    if (SA1_FAILED(st)) {
        printf("  create: %s: %s\n", sa1_status_string(st), sa1_last_error(ctx));
        return 0;
    }
    return id;
}

/* a General Sonic (kind 0, SA1_CREATE_SUPER_CAPABLE, SA1_SUPER_GENERAL | extra) */
static sa1_character_id mk_gen(sa1_context *ctx, const float pos[3], uint32_t extra)
{
    return mk(ctx, SA1_CHAR_SONIC, pos, SA1_CREATE_SUPER_CAPABLE, 50, 1, SA1_SUPER_GENERAL | extra);
}

/* upgrades as G-SA1 sets them (event flags, one byte each) into a world built and not yet stepped: 1 shoes, 2 ring, 4 light */
static void give_upgrades(sa1_context *ctx, sa1_character_id id, int upg)
{
    sa1g_world *w = sa1_character_world(ctx, id);
    if (w == NULL) {
        return;
    }
    if (upg & 1) {
        wr8w(w, A_EVENT_FLAGS + 0x8Du, 1);
    }
    if (upg & 2) {
        wr8w(w, A_EVENT_FLAGS + 0x8Eu, 1);
    }
    if (upg & 4) {
        wr8w(w, A_EVENT_FLAGS + 0x9Fu, 1);
    }
}

static int tick(sa1_context *ctx, sa1_character_id id, float sx, float sy, uint32_t buttons, sa1_state_ex *x)
{
    sa1_input in;
    sa1_event ev[64];
    uint32_t n = 0;
    sa1_status st;
    memset(&in, 0, sizeof in);
    in.struct_size = sizeof in;
    in.stick_x = sx;
    in.stick_y = sy;
    in.buttons = buttons;
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
    } while (n == 64);
    return 0;
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

static int32_t texture_named(const sa1_context *ctx, int32_t kind, int32_t arc, const char *name)
{
    uint32_t i, n = 0;
    const char *nm = NULL;
    if (arc < 0 || sa1_texture_archive(ctx, kind, (uint32_t)arc, &nm, &n) != SA1_OK) {
        return -1;
    }
    for (i = 0; i < n; i++) {
        sa1_texture_info ti;
        memset(&ti, 0, sizeof ti);
        ti.struct_size = sizeof ti;
        if (sa1_texture_get(ctx, kind, (uint32_t)arc, i, &ti) == SA1_OK && ti.name != NULL && strcmp(ti.name, name) == 0) {
            return (int32_t)i;
        }
    }
    return -1;
}

/* the name of a draw's texture ("" untextured) */
static const char *tex_name(const sa1_context *ctx, int32_t kind, const sa1_draw *d)
{
    sa1_texture_info ti;
    if (d->archive < 0 || d->texture < 0) {
        return "";
    }
    memset(&ti, 0, sizeof ti);
    ti.struct_size = sizeof ti;
    if (sa1_texture_get(ctx, kind, (uint32_t)d->archive, (uint32_t)d->texture, &ti) != SA1_OK || ti.name == NULL) {
        return "";
    }
    return ti.name;
}

/* ---- (1) the texture variants ------------------------------------------------------------------------------------------ */
static void hsv_of(const uint8_t p[4], double *h, double *s, double *v)
{
    double r = p[0] / 255.0, g = p[1] / 255.0, b = p[2] / 255.0, mx = r, mn = r, d;
    if (g > mx) mx = g;
    if (b > mx) mx = b;
    if (g < mn) mn = g;
    if (b < mn) mn = b;
    d = mx - mn;
    *v = mx;
    *s = mx > 0.0 ? d / mx : 0.0;
    if (d <= 0.0) {
        *h = 0.0;
        return;
    }
    if (mx == r) {
        *h = (g - b) / d;
    } else if (mx == g) {
        *h = (b - r) / d + 2.0;
    } else {
        *h = (r - g) / d + 4.0;
    }
    *h *= 60.0;
    if (*h < 0.0) {
        *h += 360.0;
    }
}

/* an independent "colourise" (Python colorsys's hsv_to_rgb of (45 / 360, s, v), round half to even, as gold_preview.py) */
static double round_even(double x)
{
    double f = floor(x), r = x - f;
    if (r > 0.5) {
        return f + 1.0;
    }
    if (r < 0.5) {
        return f;
    }
    return fmod(f, 2.0) == 0.0 ? f : f + 1.0;
}

static void ref_gold(const uint8_t p[4], double hue_deg, uint8_t out[3])
{
    double h, s, v, i, f, pp, q, t, rgb[3];
    hsv_of(p, &h, &s, &v);
    h = hue_deg / 360.0 * 6.0;
    i = floor(h);
    f = h - i;
    pp = v * (1.0 - s);
    q = v * (1.0 - s * f);
    t = v * (1.0 - s * (1.0 - f));
    switch ((int)i % 6) {
    case 0: rgb[0] = v; rgb[1] = t; rgb[2] = pp; break;
    case 1: rgb[0] = q; rgb[1] = v; rgb[2] = pp; break;
    case 2: rgb[0] = pp; rgb[1] = v; rgb[2] = t; break;
    case 3: rgb[0] = pp; rgb[1] = q; rgb[2] = v; break;
    case 4: rgb[0] = t; rgb[1] = pp; rgb[2] = v; break;
    default: rgb[0] = v; rgb[1] = pp; rgb[2] = q; break;
    }
    out[0] = (uint8_t)round_even(rgb[0] * 255.0);
    out[1] = (uint8_t)round_even(rgb[1] * 255.0);
    out[2] = (uint8_t)round_even(rgb[2] * 255.0);
}

static void wmean(const uint8_t *rgba, uint32_t n, double m[3])
{
    double sw = 0.0, s[3] = { 0.0, 0.0, 0.0 };
    uint32_t i;
    for (i = 0; i < n; i++) {
        const uint8_t *p = rgba + 4u * i;
        double mx = p[0], w;
        if (p[1] > mx) mx = p[1];
        if (p[2] > mx) mx = p[2];
        w = (p[3] / 255.0) * (mx / 255.0);
        sw += w;
        s[0] += w * p[0];
        s[1] += w * p[1];
        s[2] += w * p[2];
    }
    m[0] = sw > 0.0 ? s[0] / sw : 0.0;
    m[1] = sw > 0.0 ? s[1] / sw : 0.0;
    m[2] = sw > 0.0 ? s[2] / sw : 0.0;
}

static void t_variant(sa1_context *ctx)
{
    static const char *const names[4] = { "lcharge01", "lcharge02", "lcharge03", "lcharge04" };
    static const double plan[4][3] = { { 142, 107, 2 }, { 192, 164, 79 }, { 135, 103, 5 }, { 177, 148, 61 } };
    int32_t a_eff = archive_named(ctx, SA1_CHAR_SONIC, "SON_EFF"), k;
    sa1_texture_info ti, tv, tv2;
    sa1_status st;
    printf("variant: SA1_TEXTURE_VARIANT_SUPER_GOLD of SON_EFF lcharge01-04 (T1, super2_lsd.md 4.3)\n");
    CHECK(a_eff >= 0, "Sonic's SON_EFF archive: %d", (int)a_eff);
    if (a_eff < 0) {
        return;
    }
    for (k = 0; k < 4; k++) {
        int32_t ix = texture_named(ctx, SA1_CHAR_SONIC, a_eff, names[k]);
        uint32_t i, n, alpha_bad = 0, vs_bad = 0, hue_n = 0, hue_bad = 0, hue_strict_n = 0, hue_strict_bad = 0;
        uint32_t ref_bad = 0, ref_exact = 0;
        double m[3], max_dev = 0.0;
        memset(&ti, 0, sizeof ti);
        ti.struct_size = sizeof ti;
        memset(&tv, 0, sizeof tv);
        tv.struct_size = sizeof tv;
        memset(&tv2, 0, sizeof tv2);
        tv2.struct_size = sizeof tv2;
        st = ix >= 0 ? sa1_texture_get(ctx, SA1_CHAR_SONIC, (uint32_t)a_eff, (uint32_t)ix, &ti) : SA1_ERR_NOT_FOUND;
        if (st == SA1_OK) {
            st = sa1_texture_variant_get(ctx, SA1_CHAR_SONIC, (uint32_t)a_eff, (uint32_t)ix, SA1_TEXTURE_VARIANT_SUPER_GOLD,
                                         &tv);
        }
        CHECK(st == SA1_OK && ti.rgba != NULL && tv.rgba != NULL && tv.rgba != ti.rgba && tv.width == ti.width &&
                  tv.height == ti.height && tv.pixel_format == ti.pixel_format && tv.data_format == ti.data_format &&
                  tv.global_index == ti.global_index && tv.alpha == ti.alpha && tv.name != NULL &&
                  strcmp(tv.name, names[k]) == 0,
              "%s (texture %d): the variant %s, %ux%u, formats %u / %u, alpha kind %u (the texture: %ux%u, %u / %u, %u)",
              names[k], (int)ix, sa1_status_string(st), (unsigned)tv.width, (unsigned)tv.height,
              (unsigned)tv.pixel_format, (unsigned)tv.data_format, (unsigned)tv.alpha, (unsigned)ti.width,
              (unsigned)ti.height, (unsigned)ti.pixel_format, (unsigned)ti.data_format, (unsigned)ti.alpha);
        if (st != SA1_OK || tv.rgba == NULL || ti.rgba == NULL || tv.width != ti.width || tv.height != ti.height) {
            continue;
        }
        n = ti.width * ti.height;
        for (i = 0; i < n; i++) {
            const uint8_t *p = ti.rgba + 4u * i, *q = tv.rgba + 4u * i;
            double h0, s0, v0, h1, s1, v1, dev, tol;
            uint8_t r[3];
            int mx = p[0], mn = p[0], c;
            if (p[1] > mx) mx = p[1];
            if (p[2] > mx) mx = p[2];
            if (p[1] < mn) mn = p[1];
            if (p[2] < mn) mn = p[2];
            alpha_bad += p[3] != q[3];
            hsv_of(p, &h0, &s0, &v0);
            hsv_of(q, &h1, &s1, &v1);
            vs_bad += fabs(v1 - v0) > 1.0 / 255.0 + 1e-9 || fabs(s1 - s0) > 1.0 / 255.0 + 1e-9;
            if (s1 > 0.1) {
                dev = fabs(h1 - (double)SA1_SUPER_GOLD_HUE);
                if (dev > 180.0) {
                    dev = 360.0 - dev;
                }
                /* the 8-bit result moves the hue by at most 60 x 0.5 / (max - min) degrees */
                tol = 30.0 / (double)(mx - mn) + 1e-6;
                hue_n++;
                hue_bad += dev > (tol > 1.0 ? tol : 1.0);
                if (mx - mn >= 30) {
                    hue_strict_n++;
                    hue_strict_bad += dev > 1.0;
                    if (dev > max_dev) {
                        max_dev = dev;
                    }
                }
            }
            ref_gold(p, (double)SA1_SUPER_GOLD_HUE, r);
            for (c = 0; c < 3; c++) {
                ref_bad += abs((int)r[c] - (int)q[c]) > 1;
            }
            ref_exact += r[0] == q[0] && r[1] == q[1] && r[2] == q[2];
        }
        CHECK(alpha_bad == 0 && vs_bad == 0, "%s: alpha identical texel for texel (%u differ), HSV value and saturation within "
              "1/255 (%u differ) in %u texels", names[k], (unsigned)alpha_bad, (unsigned)vs_bad, (unsigned)n);
        CHECK(hue_bad == 0 && hue_strict_bad == 0 && hue_strict_n > 0,
              "%s: the hue %d degrees where the saturation is above 0.1: %u texels within the 8-bit quantisation (%u not), "
              "%u of them with max - min >= 30 within +-1 degree (%u not; largest deviation %.3f)", names[k],
              (int)SA1_SUPER_GOLD_HUE, (unsigned)hue_n, (unsigned)hue_bad, (unsigned)hue_strict_n,
              (unsigned)hue_strict_bad, max_dev);
        CHECK(ref_bad == 0, "%s: every texel equal (+-1) to an independent HSV colourise (colorsys, round half to even): "
              "%u channels off, %u of %u texels exact", names[k], (unsigned)ref_bad, (unsigned)ref_exact, (unsigned)n);
        wmean(tv.rgba, n, m);
        CHECK(fabs(m[0] - plan[k][0]) <= 3.0 && fabs(m[1] - plan[k][1]) <= 3.0 && fabs(m[2] - plan[k][2]) <= 3.0,
              "%s: the alpha x value weighted mean (%.1f, %.1f, %.1f), the plan's (%.0f, %.0f, %.0f) +-3", names[k], m[0],
              m[1], m[2], plan[k][0], plan[k][1], plan[k][2]);
        st = sa1_texture_variant_get(ctx, SA1_CHAR_SONIC, (uint32_t)a_eff, (uint32_t)ix, SA1_TEXTURE_VARIANT_SUPER_GOLD,
                                     &tv2);
        CHECK(st == SA1_OK && tv2.rgba == tv.rgba, "%s: a second call gives the same pixel pointer", names[k]);
    }
    /* the refusals */
    {
        int32_t ball = texture_named(ctx, SA1_CHAR_SONIC, a_eff, "s_ball_a"), l1 = texture_named(ctx, SA1_CHAR_SONIC, a_eff,
                                                                                                  "lcharge01");
        int32_t a8 = archive_named(ctx, SA1_CHAR_SUPER_SONIC, "SON_EFF");
        int32_t l8 = texture_named(ctx, SA1_CHAR_SUPER_SONIC, a8, "lcharge01");
        sa1_status s_ball, s_body, s_v0, s_v2, s_tails, s_k8, s_arc, s_null;
        memset(&tv, 0, sizeof tv);
        tv.struct_size = sizeof tv;
        s_ball = sa1_texture_variant_get(ctx, SA1_CHAR_SONIC, (uint32_t)a_eff, (uint32_t)ball,
                                         SA1_TEXTURE_VARIANT_SUPER_GOLD, &tv);
        s_body = sa1_texture_variant_get(ctx, SA1_CHAR_SONIC, 0, 0, SA1_TEXTURE_VARIANT_SUPER_GOLD, &tv);
        s_v0 = sa1_texture_variant_get(ctx, SA1_CHAR_SONIC, (uint32_t)a_eff, (uint32_t)l1, 0, &tv);
        s_v2 = sa1_texture_variant_get(ctx, SA1_CHAR_SONIC, (uint32_t)a_eff, (uint32_t)l1, 2, &tv);
        s_tails = sa1_texture_variant_get(ctx, SA1_CHAR_TAILS, 0, 0, SA1_TEXTURE_VARIANT_SUPER_GOLD, &tv);
        s_k8 = sa1_texture_variant_get(ctx, SA1_CHAR_SUPER_SONIC, (uint32_t)a8, (uint32_t)l8,
                                       SA1_TEXTURE_VARIANT_SUPER_GOLD, &tv);
        s_arc = sa1_texture_variant_get(ctx, SA1_CHAR_SONIC, 99, 0, SA1_TEXTURE_VARIANT_SUPER_GOLD, &tv);
        s_null = sa1_texture_variant_get(ctx, SA1_CHAR_SONIC, (uint32_t)a_eff, (uint32_t)l1,
                                         SA1_TEXTURE_VARIANT_SUPER_GOLD, NULL);
        CHECK(ball >= 0 && s_ball == SA1_ERR_NOT_FOUND && s_body == SA1_ERR_NOT_FOUND && s_v0 == SA1_ERR_NOT_FOUND &&
                  s_v2 == SA1_ERR_NOT_FOUND && s_tails == SA1_ERR_NOT_FOUND && l8 >= 0 && s_k8 == SA1_ERR_NOT_FOUND &&
                  s_arc == SA1_ERR_NOT_FOUND && s_null == SA1_ERR_INVALID_ARGUMENT,
              "refused: s_ball_a %s, a body texture %s, variant 0 %s, variant 2 %s, Tails %s, Super Sonic's lcharge01 %s, "
              "archive 99 %s; out NULL %s", sa1_status_string(s_ball), sa1_status_string(s_body),
              sa1_status_string(s_v0), sa1_status_string(s_v2), sa1_status_string(s_tails), sa1_status_string(s_k8),
              sa1_status_string(s_arc), sa1_status_string(s_null));
    }
}

/* ---- (2)-(5): two General worlds in lockstep, SA1_SUPOPT_LSD_GOLD 0 and 1 ---------------------------------------------- */
typedef struct texids {
    int32_t eff, ss;                    /* Sonic's SON_EFF and SUPERSONIC archives */
    int32_t l[4];                       /* lcharge01-04 in SON_EFF */
} texids;

typedef struct gsum {
    uint32_t pairs;
    uint32_t state_diff;                /* pairs whose sa1_state_ex differs (any byte) */
    uint32_t shape_diff;                /* pairs whose render differs in counts, draw fields, vertices, sprites or lines */
    uint32_t color_diff;                /* draws (not the glow) whose colours differ */
    uint32_t glow_not_complement;       /* glow draws whose vertices are not the complement of option 0's */
    uint32_t flag_run0;                 /* SA1_DRAW_SUPER_GOLD draws in the option-0 run */
    uint32_t flag_wrong;                /* option-1 draws flagged where not expected, or not flagged where expected */
    uint32_t plain_flagged[2];          /* option-1 sparkle (lcharge02) / streak (lcharge01) draws flagged */
    uint32_t ring_draws, ring_flagged;  /* option-1 lcharge03 / 04 draws, of them flagged (O3: none) */
    uint32_t glow_draws, glow_flagged;  /* option-1 glow draws, of them flagged */
    uint32_t glow_pass1;                /* pairs with the glow whose pass-1 colours were checked */
    int g0min, g0max, g1min, g1max;     /* pass 1's green over the pulse, option 0 / 1 */
    uint32_t glow_pass1_bad;            /* pass-1 corners outside (0, g, 255) / (255, g, 0) */
    uint32_t after_revert_flagged;      /* flagged draws from the second pair after SA1_ORDER_NORMAL */
    uint32_t trail, aura, body;         /* option-1 draws of the Super trail (ss_dash), the Super aura, the body */
    uint32_t trail_flagged, aura_flagged, body_flagged;
    uint32_t mixed_sprite, mixed_poly, mixed_glow;  /* (4) pairs with a flagged sprite / polygon / glow draw and an
                                                       unflagged additive draw */
    uint32_t merge_bad;                 /* (4) draws whose records mix gold and other ones, or counter mismatches */
    uint32_t merge_pairs;               /* (4) pairs re-emitted */
    uint32_t hash0, hash1;              /* the per-pair lists (archive, texture, flags, first colour) */
    uint32_t dash_pairs;                /* pairs in mode 6 after the release */
    uint32_t fl_spr, fl_poly, fl_glow;  /* the flags of the flagged sprite / streak / glow draws (OR) */
    char first[512];                    /* the first difference */
} gsum;

static void note(gsum *s, const char *fmt, int a, int b)
{
    if (!s->first[0]) {
        snprintf(s->first, sizeof s->first, fmt, a, b);
    }
}

static uint32_t fnv(uint32_t h, const void *p, size_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    size_t i;
    for (i = 0; i < n; i++) {
        h = (h ^ b[i]) * 16777619u;
    }
    return h;
}

static uint32_t list_hash(uint32_t h, const sa1_render *r)
{
    uint32_t i;
    for (i = 0; i < r->num_draws; i++) {
        const sa1_draw *d = &r->draws[i];
        uint8_t c[4] = { 0, 0, 0, 0 };
        if (d->count && d->prim == SA1_PRIM_TRIANGLES) {
            memcpy(c, r->colors + 12u * d->first, 4);
        } else if (d->count && d->prim == SA1_PRIM_SPRITES) {
            memcpy(c, r->sprites[d->first].color, 4);
        }
        h = fnv(h, &d->archive, 4);
        h = fnv(h, &d->texture, 4);
        h = fnv(h, &d->flags, 4);
        h = fnv(h, c, 4);
    }
    return fnv(h, &r->num_draws, 4);
}

static int is_glow(const sa1_draw *d)
{
    return d->prim == SA1_PRIM_TRIANGLES && d->archive < 0 && d->material == GLOW_MATERIAL &&
           d->source == SA1_SOURCE_MODEL;
}

/* 0 none, 1 plain sparkle / streak (lcharge01 / 02), 2 Crystal Ring (lcharge03 / 04) */
static int lsd_kind(const texids *t, const sa1_draw *d)
{
    if (d->archive != t->eff || d->archive < 0) {
        return 0;
    }
    if (d->texture == t->l[0] || d->texture == t->l[1]) {
        return 1;
    }
    if (d->texture == t->l[2] || d->texture == t->l[3]) {
        return 2;
    }
    return 0;
}

/* what the flag of a run-1 draw may be in a pair */
#define EXPECT_NONE   0     /* the game's colours: never flagged */
#define EXPECT_GOLD   1     /* Super with the look: flagged exactly on the plain sparkles / streaks and the glow */
#define EXPECT_EITHER 2     /* a pair of a form change (the look's one-pair lag) */
#define EXPECT_AFTER  3     /* from the second pair after SA1_ORDER_NORMAL: counted (must be 0) */

/* compares the two renders of a pair (expect: EXPECT_*) */
static void cmp_pair(const sa1_context *ctx, const texids *t, const sa1_render *r0, const sa1_render *r1, int expect_mode,
                     gsum *s)
{
    uint32_t i;
    int add_unflagged = 0, f_spr = 0, f_poly = 0, f_glow = 0;
    s->hash0 = list_hash(s->hash0, r0);
    s->hash1 = list_hash(s->hash1, r1);
    if (r0->num_draws != r1->num_draws || r0->num_triangles != r1->num_triangles || r0->num_sprites != r1->num_sprites ||
        r0->num_line_vertices != r1->num_line_vertices ||
        memcmp(r0->positions, r1->positions, 36u * r0->num_triangles) != 0 ||
        memcmp(r0->normals, r1->normals, 36u * r0->num_triangles) != 0 ||
        memcmp(r0->uvs, r1->uvs, 24u * r0->num_triangles) != 0 ||
        (r0->num_sprites && memcmp(r0->sprites, r1->sprites, sizeof(sa1_sprite) * r0->num_sprites) != 0) ||
        (r0->num_line_vertices &&
         memcmp(r0->line_vertices, r1->line_vertices, sizeof(sa1_line_vertex) * r0->num_line_vertices) != 0) ||
        memcmp(r0->position, r1->position, sizeof r0->position) != 0) {
        s->shape_diff++;
        note(s, "pair %d: counts, vertices, sprites or lines differ (draws %d)", (int)s->pairs, (int)r0->num_draws);
        return;
    }
    for (i = 0; i < r1->num_draws; i++) {
        const sa1_draw *a = &r0->draws[i], *b = &r1->draws[i];
        int glow = is_glow(b), lk = lsd_kind(t, b), flagged = (b->flags & SA1_DRAW_SUPER_GOLD) != 0, expect;
        const char *tn;
        if (a->prim != b->prim || a->first != b->first || a->count != b->count || a->archive != b->archive ||
            a->texture != b->texture || a->material != b->material || a->blend_src != b->blend_src ||
            a->blend_dst != b->blend_dst || a->source != b->source || memcmp(a->diffuse, b->diffuse, 4) != 0 ||
            (a->flags | SA1_DRAW_SUPER_GOLD) != (b->flags | SA1_DRAW_SUPER_GOLD)) {
            s->shape_diff++;
            note(s, "pair %d draw %d: a field other than SA1_DRAW_SUPER_GOLD differs", (int)s->pairs, (int)i);
            continue;
        }
        s->flag_run0 += (a->flags & SA1_DRAW_SUPER_GOLD) != 0;
        expect = expect_mode == EXPECT_GOLD && (glow || lk == 1);
        if (expect_mode == EXPECT_AFTER) {
            s->after_revert_flagged += flagged;
        } else if (expect_mode != EXPECT_EITHER && flagged != expect) {
            s->flag_wrong++;
            note(s, "pair %d draw %d: the flag is not where it is expected", (int)s->pairs, (int)i);
        }
        if (lk == 1 && flagged) {
            s->plain_flagged[b->prim == SA1_PRIM_SPRITES ? 0 : 1]++;
        }
        if (lk == 2) {
            s->ring_draws++;
            s->ring_flagged += flagged;
        }
        if (glow) {
            s->glow_draws++;
            s->glow_flagged += flagged;
        }
        /* colours */
        if (b->prim == SA1_PRIM_TRIANGLES) {
            const uint8_t *c0 = r0->colors + 12u * b->first, *c1 = r1->colors + 12u * b->first;
            if (glow && flagged) {
                uint32_t k, bad = 0, n = 3u * b->count, p1 = 0;
                for (k = 0; k < n; k++) {
                    const uint8_t *p = c0 + 4u * k, *q = c1 + 4u * k;
                    int mx = p[0], mn = p[0], j;
                    if (p[1] > mx) mx = p[1];
                    if (p[2] > mx) mx = p[2];
                    if (p[1] < mn) mn = p[1];
                    if (p[2] < mn) mn = p[2];
                    for (j = 0; j < 3; j++) {
                        bad += q[j] != (uint8_t)(mx + mn - p[j]);
                    }
                    bad += q[3] != p[3];
                    if (p[0] == 0) {
                        /* pass 1: the game's red 0 (passes 2 and 3 have 0.05: 13) */
                        p1++;
                        s->glow_pass1_bad += !(p[2] == 255 && q[0] == 255 && q[2] == 0);
                        if (p[1] < s->g0min) s->g0min = p[1];
                        if (p[1] > s->g0max) s->g0max = p[1];
                        if (q[1] < s->g1min) s->g1min = q[1];
                        if (q[1] > s->g1max) s->g1max = q[1];
                    }
                }
                s->glow_pass1 += p1 > 0;
                if (bad) {
                    s->glow_not_complement++;
                    note(s, "pair %d draw %d: the glow is not the complement of option 0's", (int)s->pairs, (int)i);
                }
            } else if (memcmp(c0, c1, 12u * b->count) != 0) {
                s->color_diff++;
                note(s, "pair %d draw %d: colours differ", (int)s->pairs, (int)i);
            }
        }
        /* the classes of (5) and the pairs of (4) */
        tn = tex_name(ctx, SA1_CHAR_SONIC, b);
        if (b->archive == t->ss && !strncmp(tn, "ss_dash", 7)) {
            s->trail++;
            s->trail_flagged += flagged;
        }
        if (b->archive == t->ss && (!strncmp(tn, "s1sonic", 7) || !strncmp(tn, "s2sonic", 7))) {
            s->aura++;
            s->aura_flagged += flagged;
        }
        if (b->source == SA1_SOURCE_CHARACTER && !(b->flags & (SA1_DRAW_SHADOW | SA1_DRAW_ADDITIVE | SA1_DRAW_EFFECT))) {
            s->body++;
            s->body_flagged += flagged;
        }
        if ((b->flags & SA1_DRAW_ADDITIVE) && !flagged) {
            add_unflagged = 1;
        }
        f_spr |= flagged && b->prim == SA1_PRIM_SPRITES;
        f_poly |= flagged && b->prim == SA1_PRIM_TRIANGLES && b->source == SA1_SOURCE_POLYGON;
        f_glow |= flagged && glow;
        if (flagged) {
            if (b->prim == SA1_PRIM_SPRITES) {
                s->fl_spr |= b->flags;
            } else if (glow) {
                s->fl_glow |= b->flags;
            } else {
                s->fl_poly |= b->flags;
            }
        }
    }
    s->mixed_sprite += add_unflagged && f_spr;
    s->mixed_poly += add_unflagged && f_poly;
    s->mixed_glow += add_unflagged && f_glow;
}

/* (4): the character's capture re-emitted into a builder of the test with render_capture's options; per draw, every
 * primitive's own origin against the draw's flag */
static void merges_pair(sa1_context *ctx, sa1_character_id id, const texids *t, sa1_meshbuf *mb, gsum *s)
{
    sa1_capture *cap = sa1_character_capture(ctx, id);
    const sa1_charset *cs = &ctx->chars[SA1_CHAR_SONIC];
    sa1_capture_emit_opts o;
    sa1_capture_stats st;
    sa1_geometry g;
    sa1_state_ex x;
    uint32_t i, k, spr_f = 0, poly_f = 0;
    sa1_status rs;
    if (cap == NULL || !sa1_capture_valid(cap) || sa1_capture_hidden(cap)) {
        return;
    }
    memset(&x, 0, sizeof x);
    x.struct_size = sizeof x;
    sa1_character_state_ex(ctx, id, &x);
    memset(&o, 0, sizeof o);
    o.texlist0 = cs->archives[0].texlist_addr;
    o.shadow_model = sa1_capture_shadow_model(cap);
    o.clip = 1;
    memcpy(o.clip_center, x.sa1_position, sizeof o.clip_center);
    o.clip_radius = 16384.0f;
    o.cs = cs;
    o.effects = 1;
    o.dedupe = 1;
    memset(&g, 0, sizeof g);
    rs = sa1_capture_emit_into(ctx, cap, &o, mb, &g);
    if (rs != SA1_OK) {
        s->merge_bad++;
        note(s, "pair %d: the re-emit failed (%d)", (int)s->pairs, (int)rs);
        return;
    }
    s->merge_pairs++;
    sa1_capture_get_stats(cap, &st);
    for (i = 0; i < mb->ndraw; i++) {
        const sa1_draw *d = &mb->draws[i];
        int flagged = (d->flags & SA1_DRAW_SUPER_GOLD) != 0;
        if (d->prim == SA1_PRIM_TRIANGLES) {
            for (k = d->first; k < d->first + d->count; k++) {
                /* the triangle's own texture / material, written per record before any merge (sa1_meshbuf tarc / tidx /
                   mat): a plain streak's lcharge01, or the glow's untextured S_MOT material; gold while the snapshot is */
                int gold_rec = st.super_gold &&
                               ((mb->tarc[k] == t->eff && mb->tarc[k] >= 0 && mb->tidx[k] == t->l[0] &&
                                 d->source == SA1_SOURCE_POLYGON) ||
                                (mb->tarc[k] < 0 && mb->mat[k] == GLOW_MATERIAL && d->source == SA1_SOURCE_MODEL));
                if (gold_rec != flagged) {
                    s->merge_bad++;
                    note(s, "pair %d builder draw %d: a triangle whose record is not as the draw's flag", (int)s->pairs,
                         (int)i);
                    break;
                }
            }
            poly_f += flagged && d->source == SA1_SOURCE_POLYGON;
        } else if (d->prim == SA1_PRIM_SPRITES) {
            /* sprites merge only with the same texture (emit_sprite's key): lcharge02 is drawn only from 8C1C6AA4 */
            int gold_rec = st.super_gold && d->archive == t->eff && d->archive >= 0 && d->texture == t->l[1];
            if (gold_rec != flagged) {
                s->merge_bad++;
                note(s, "pair %d builder draw %d: a sprite draw whose records are not as its flag", (int)s->pairs, (int)i);
            }
            spr_f += flagged ? d->count : 0u;
        } else if (flagged) {
            s->merge_bad++;
            note(s, "pair %d builder draw %d: a flagged line draw", (int)s->pairs, (int)i);
        }
    }
    if (spr_f != st.gold_sprites || poly_f != st.gold_polygons) {
        s->merge_bad++;
        note(s, "pair %d: the capture counted other gold sprites / polygon draws (%d sprites in flagged draws)",
             (int)s->pairs, (int)spr_f);
    }
}

/* one scenario: upg (1 shoes, 2 ring, 4 light), revert_at (hold pair of SA1_ORDER_NORMAL, -1 none), merges ((4)) */
static void scenario(sa1_context *ctx, const texids *t, const char *title, int upg, int revert_at, int merges, gsum *s,
                     uint32_t *arena0, uint32_t *arena1, uint32_t *rand0, uint32_t *rand1)
{
    static const float at0[3] = { 0.0f, 0.5f, 0.0f };
    sa1_character_id id0, id1;
    sa1_state_ex x0, x1;
    sa1_meshbuf mb;
    int i, rev = -1, charged = -1, dash = 0;
    sa1_status o0, o1;
    memset(s, 0, sizeof *s);
    memset(&mb, 0, sizeof mb);
    s->g0min = s->g1min = 256;
    s->g0max = s->g1max = -1;
    s->hash0 = s->hash1 = 2166136261u;
    printf("  %s\n", title);
    id0 = mk_gen(ctx, at0, SA1_SUPER_START | SA1_SUPER_NO_DRAIN);
    id1 = mk_gen(ctx, at0, SA1_SUPER_START | SA1_SUPER_NO_DRAIN);
    if (id0 == 0 || id1 == 0) {
        g_fail++;
        return;
    }
    give_upgrades(ctx, id0, upg);
    give_upgrades(ctx, id1, upg);
    sa1_character_set_super_option(ctx, id0, SA1_SUPOPT_SUPER_EFFECTS, SA1_SUPER_EFFECTS_CLONES);
    sa1_character_set_super_option(ctx, id1, SA1_SUPOPT_SUPER_EFFECTS, SA1_SUPER_EFFECTS_CLONES);
    o0 = sa1_character_set_super_option(ctx, id0, SA1_SUPOPT_LSD_GOLD, 0);
    o1 = sa1_character_set_super_option(ctx, id1, SA1_SUPOPT_LSD_GOLD, 1);
    CHECK(o0 == SA1_OK && o1 == SA1_OK, "SA1_SUPOPT_LSD_GOLD 0 / 1 taken by the General worlds: %s / %s",
          sa1_status_string(o0), sa1_status_string(o1));
    for (i = 0; i < 8 + 130 + 40; i++) {
        const sa1_render *r0 = NULL, *r1 = NULL;
        uint32_t b = (i >= 8 && i < 138) ? SA1_BUTTON_ACTION : 0u;
        float sy = i >= 138 ? 1.0f : 0.0f;
        int expect;
        if (revert_at >= 0 && i == 8 + revert_at) {
            sa1_character_order(ctx, id0, SA1_ORDER_NORMAL);
            sa1_character_order(ctx, id1, SA1_ORDER_NORMAL);
            rev = i;
        }
        if (tick(ctx, id0, 0.0f, sy, b, &x0) != 0 || tick(ctx, id1, 0.0f, sy, b, &x1) != 0) {
            g_fail++;
            break;
        }
        if (memcmp(&x0, &x1, sizeof x0) != 0) {
            s->state_diff++;
            note(s, "pair %d: the player state differs (mode %d)", i, (int)x1.mode);
        }
        if (charged < 0 && (x1.game_flags & 0x200u)) {
            charged = i;
        }
        dash += x1.mode == 6;
        if (sa1_character_render(ctx, id0, 1.0f, &r0) != SA1_OK || sa1_character_render(ctx, id1, 1.0f, &r1) != SA1_OK ||
            r0 == NULL || r1 == NULL) {
            g_fail++;
            break;
        }
        /* the gold decision follows the look of the drawn pair: Super from the start (the first pairs: either); the pair
           of SA1_ORDER_NORMAL may still be drawn gold (the look's one-pair lag), from the second pair after it never */
        if (rev >= 0) {
            expect = i == rev ? EXPECT_EITHER : EXPECT_AFTER;
        } else {
            expect = i < 2 ? EXPECT_EITHER : EXPECT_GOLD;
        }
        cmp_pair(ctx, t, r0, r1, expect, s);
        s->pairs++;
        if (merges) {
            merges_pair(ctx, id1, t, &mb, s);
        }
        if (g_verbose && (i == 50 || i == 120 || i == 140)) {
            printf("    pair %d: mode %d action %d flag %08X equipment %04X draws %u\n", i, (int)x1.mode, (int)x1.action,
                   (unsigned)x1.game_flags, (unsigned)x1.equipment, (unsigned)r1->num_draws);
        }
    }
    printf("    charged at pair %d (hold pair %d); %s; released: %d pairs in mode 6 (the Light Speed Dash), mode %d at the "
           "end\n", charged, charged - 8, rev >= 0 ? "SA1_ORDER_NORMAL given" : "Super throughout", dash, (int)x1.mode);
    s->dash_pairs = (uint32_t)dash;
    {
        sa1g_world *w0 = sa1_character_world(ctx, id0), *w1 = sa1_character_world(ctx, id1);
        *arena0 = w0 != NULL ? fnv(2166136261u, w0->ram, SA1G_ARENA_SIZE) : 0u;
        *arena1 = w1 != NULL ? fnv(2166136261u, w1->ram, SA1G_ARENA_SIZE) : 1u;
        *rand0 = x0.rand_state;
        *rand1 = x1.rand_state;
    }
    sa1_meshbuf_free_exec(ctx, &mb);
    sa1_meshbuf_free(ctx, &mb);
    sa1_character_destroy(ctx, id0);
    sa1_character_destroy(ctx, id1);
}

static int g_have_scen;
static gsum g_three, g_shoes;
static uint32_t g_arena[2][2], g_rand[2][2];

static int texids_of(sa1_context *ctx, texids *t)
{
    static const char *const names[4] = { "lcharge01", "lcharge02", "lcharge03", "lcharge04" };
    int k;
    t->eff = archive_named(ctx, SA1_CHAR_SONIC, "SON_EFF");
    t->ss = archive_named(ctx, SA1_CHAR_SONIC, "SUPERSONIC");
    for (k = 0; k < 4; k++) {
        t->l[k] = texture_named(ctx, SA1_CHAR_SONIC, t->eff, names[k]);
    }
    return t->eff >= 0 && t->ss >= 0 && t->l[0] >= 0 && t->l[1] >= 0 && t->l[2] >= 0 && t->l[3] >= 0;
}

static void run_scenarios(sa1_context *ctx)
{
    texids t;
    if (g_have_scen) {
        return;
    }
    g_have_scen = 1;
    if (!texids_of(ctx, &t) || scene_grid(ctx) != 0) {
        printf("  the archives / textures or the scene are missing\n");
        g_fail++;
        return;
    }
    scenario(ctx, &t, "(A) the owner's three upgrades (shoes, Crystal Ring, Ancient Light), no target", 7, -1, 0, &g_three,
             &g_arena[0][0], &g_arena[0][1], &g_rand[0][0], &g_rand[0][1]);
    scenario(ctx, &t, "(B) the shoes alone; SA1_ORDER_NORMAL at hold pair 118", 1, 118, 1, &g_shoes, &g_arena[1][0],
             &g_arena[1][1], &g_rand[1][0], &g_rand[1][1]);
}

static void t_lsd_gold(sa1_context *ctx)
{
    const gsum *a = &g_three, *b = &g_shoes;
    printf("lsd_gold (GRID): SA1_SUPOPT_LSD_GOLD 0 and 1 in lockstep (super2_plan.md 6.2 (2), O3 / O4)\n");
    run_scenarios(ctx);
    printf("    (A) list hashes %08X / %08X; (B) %08X / %08X (the per-pair (archive, texture, flags, first colour) lists of "
           "option 0 / 1)\n", (unsigned)a->hash0, (unsigned)a->hash1, (unsigned)b->hash0, (unsigned)b->hash1);
    printf("GOLD_LISTS %08X %08X %08X %08X\n", (unsigned)a->hash0, (unsigned)a->hash1, (unsigned)b->hash0,
           (unsigned)b->hash1);
    printf("    the flagged draws' flags: sparkles 0x%04X, streaks 0x%04X, glow 0x%04X\n", (unsigned)b->fl_spr,
           (unsigned)b->fl_poly, (unsigned)(a->fl_glow | b->fl_glow));
    CHECK(a->pairs == 178 && b->pairs == 178 && a->flag_run0 == 0 && b->flag_run0 == 0 && a->dash_pairs > 0,
          "%u + %u pairs; option 0 flags no draw (%u, %u); (A) released into the Light Speed Dash (mode 6, no target: "
          "review R.10) for %u pairs", (unsigned)a->pairs, (unsigned)b->pairs, (unsigned)a->flag_run0,
          (unsigned)b->flag_run0, (unsigned)a->dash_pairs);
    CHECK(a->ring_draws > 0 && a->ring_flagged == 0 && a->plain_flagged[0] == 0 && a->plain_flagged[1] == 0,
          "(A) with the Crystal Ring: %u lcharge03 / 04 draws, none flagged, their textures the game's (owner answer O3)",
          (unsigned)a->ring_draws);
    /* pass 1's green is (0.28 - 0.18 f) x 255 over the pulse f = 0..1: 25.5..71.4, which ds_channel rounds to 25 (the
       single-precision 0.28 - 0.18 lies just below 0.1) .. 71 - the plan's "26..71" (lightspeed_visuals.md 3.5) is one off
       at the bottom; its complement 184..230 */
    CHECK(a->glow_draws > 0 && a->glow_flagged == a->glow_draws && a->glow_not_complement == 0 && a->glow_pass1 > 0 &&
              a->glow_pass1_bad == 0 && a->g0min >= 25 && a->g0max <= 71 && a->g1min >= 184 && a->g1max <= 230 &&
              a->g0min + a->g1max == 255 && a->g0max + a->g1min == 255,
          "(A) the glow: %u draws, %u flagged, every vertex the complement of option 0's (%u not); pass 1 option 0 (0, "
          "%d..%d, 255) -> option 1 (255, %d..%d, 0) in %u pairs (%u corners outside)", (unsigned)a->glow_draws,
          (unsigned)a->glow_flagged, (unsigned)a->glow_not_complement, a->g0min, a->g0max, a->g1min, a->g1max,
          (unsigned)a->glow_pass1, (unsigned)a->glow_pass1_bad);
    CHECK(b->plain_flagged[0] > 0 && b->plain_flagged[1] > 0 && b->ring_draws == 0 && b->glow_draws > 0 &&
              b->glow_flagged > 0 && b->glow_not_complement == 0 && b->glow_pass1_bad == 0,
          "(B) the shoes alone: %u lcharge02 sparkle draws and %u lcharge01 streak draws flagged (archive and texture the "
          "game's), the glow gold in %u draws", (unsigned)b->plain_flagged[0], (unsigned)b->plain_flagged[1],
          (unsigned)b->glow_flagged);
    CHECK(a->flag_wrong == 0 && b->flag_wrong == 0,
          "the flag exactly on the plain sparkles / streaks and the glow while Super ((A) %u, (B) %u wrong)",
          (unsigned)a->flag_wrong, (unsigned)b->flag_wrong);
    CHECK(a->shape_diff == 0 && b->shape_diff == 0 && a->color_diff == 0 && b->color_diff == 0,
          "no other draw differs: draw fields, vertices, normals, UVs, sprites, lines and colours identical ((A) %u / %u, "
          "(B) %u / %u)%s%s", (unsigned)a->shape_diff, (unsigned)a->color_diff, (unsigned)b->shape_diff,
          (unsigned)b->color_diff, a->first[0] ? "; (A): " : "", a->first[0] ? a->first : "");
    if (b->first[0]) {
        printf("    (B) first difference: %s\n", b->first);
    }
    CHECK(b->after_revert_flagged == 0, "(B) after SA1_ORDER_NORMAL (option still 1): no flag from the second pair on, the "
          "game's colours (%u flagged)", (unsigned)b->after_revert_flagged);
}

static void t_identity(sa1_context *ctx)
{
    printf("identity: the world with SA1_SUPOPT_LSD_GOLD 1 runs exactly as with 0 (presentation only)\n");
    run_scenarios(ctx);
    CHECK(g_three.state_diff == 0 && g_shoes.state_diff == 0,
          "the player state (sa1_state_ex, every byte) identical in every pair ((A) %u, (B) %u differ)",
          (unsigned)g_three.state_diff, (unsigned)g_shoes.state_diff);
    CHECK(g_rand[0][0] == g_rand[0][1] && g_rand[1][0] == g_rand[1][1] && g_arena[0][0] == g_arena[0][1] &&
              g_arena[1][0] == g_arena[1][1],
          "after the scripts: rand %08X / %08X, %08X / %08X; arena hash %08X / %08X, %08X / %08X",
          (unsigned)g_rand[0][0], (unsigned)g_rand[0][1], (unsigned)g_rand[1][0], (unsigned)g_rand[1][1],
          (unsigned)g_arena[0][0], (unsigned)g_arena[0][1], (unsigned)g_arena[1][0], (unsigned)g_arena[1][1]);
}

static void t_merges(sa1_context *ctx)
{
    const gsum *b = &g_shoes;
    printf("merges: no draw holds both gold and other records (the shoes alone, option 1; review R.11)\n");
    run_scenarios(ctx);
    CHECK(b->merge_pairs > 100 && b->merge_bad == 0,
          "%u pairs re-emitted: every triangle / sprite of every draw from records as its flag says, the capture's "
          "counters agree (%u bad)%s%s", (unsigned)b->merge_pairs, (unsigned)b->merge_bad, b->merge_bad ? ": " : "",
          b->merge_bad ? b->first : "");
    CHECK(b->mixed_sprite > 0 && b->mixed_poly > 0 && b->mixed_glow > 0,
          "not vacuous: pairs with a flagged sprite draw %u, polygon draw %u, glow draw %u next to unflagged additive "
          "draws (the dust, the Super aura)", (unsigned)b->mixed_sprite, (unsigned)b->mixed_poly, (unsigned)b->mixed_glow);
}

static void t_unchanged(sa1_context *ctx)
{
    static const float at[3] = { 300.0f, 0.5f, 0.0f };
    const gsum *a = &g_three, *b = &g_shoes;
    sa1_character_id id, k8;
    sa1_status sp, s8, sp0;
    uint32_t flagged = 0, sparkles = 0;
    int i;
    printf("unchanged: the trail, the Super aura and the body; the worlds that are not General\n");
    run_scenarios(ctx);
    CHECK(a->trail > 0 && a->aura > 0 && a->body > 0 && a->trail_flagged + a->aura_flagged + a->body_flagged == 0 &&
              b->aura > 0 && b->body > 0 && b->aura_flagged + b->body_flagged == 0,
          "(A) the Super trail (ss_dash) %u draws, the Super aura %u, the body %u; (B) aura %u, body %u: none flagged, "
          "identical with 0 and 1 (lsd_gold)", (unsigned)a->trail, (unsigned)a->aura, (unsigned)a->body,
          (unsigned)b->aura, (unsigned)b->body);
    id = mk(ctx, SA1_CHAR_SONIC, at, 0, 50, 0, 0);
    k8 = mk(ctx, SA1_CHAR_SUPER_SONIC, at, 0, -1, 0, 0);
    if (id == 0 || k8 == 0) {
        g_fail++;
        return;
    }
    give_upgrades(ctx, id, 1);
    sp = sa1_character_set_super_option(ctx, id, SA1_SUPOPT_LSD_GOLD, 1);
    sp0 = sa1_character_set_super_option(ctx, id, SA1_SUPOPT_LSD_GOLD, 0);
    s8 = sa1_character_set_super_option(ctx, k8, SA1_SUPOPT_LSD_GOLD, 1);
    for (i = 0; i < 8 + 130; i++) {
        const sa1_render *r = NULL;
        uint32_t k;
        if (tick(ctx, id, 0.0f, 0.0f, i >= 8 ? SA1_BUTTON_ACTION : 0u, NULL) != 0 ||
            sa1_character_render(ctx, id, 1.0f, &r) != SA1_OK || r == NULL) {
            g_fail++;
            break;
        }
        for (k = 0; k < r->num_draws; k++) {
            flagged += (r->draws[k].flags & SA1_DRAW_SUPER_GOLD) != 0;
            sparkles += r->draws[k].prim == SA1_PRIM_SPRITES;
        }
    }
    CHECK(sp == SA1_ERR_UNSUPPORTED && sp0 == SA1_ERR_UNSUPPORTED && s8 == SA1_ERR_UNSUPPORTED && flagged == 0 &&
              sparkles > 0,
          "a plain Sonic: SA1_SUPOPT_LSD_GOLD 1 / 0 %s / %s; kind 8 %s; his Light Speed charge: %u sprite draws, %u flagged",
          sa1_status_string(sp), sa1_status_string(sp0), sa1_status_string(s8), (unsigned)sparkles, (unsigned)flagged);
    sa1_character_destroy(ctx, id);
    sa1_character_destroy(ctx, k8);
}

/* ---- (6) memory: the variants live and die with the character set --------------------------------------------------- */
static long g_live;

static void *cnt_alloc(void *u, size_t n)
{
    void *p = ga_malloc(n);
    (void)u;
    if (p != NULL) {
        g_live++;
    }
    return p;
}

static void cnt_free(void *u, void *p)
{
    (void)u;
    if (p != NULL) {
        g_live--;
        ga_free(p);
    }
}

static sa1_status load_sonic(sa1_context *c, uint32_t flags)
{
    sa1_load_options lo;
    memset(&lo, 0, sizeof lo);
    lo.struct_size = sizeof lo;
    lo.characters = 1u << SA1_CHAR_SONIC;
    lo.flags = flags;
    return sa1_load_disc(c, g_gdi, &lo);
}

static void t_memory(sa1_context *unused)
{
    sa1_context *c = NULL;
    sa1_host h;
    sa1_texture_info tv;
    sa1_status st, sv;
    long after[3] = { -1, -1, -1 };
    int i, ok = 1;
    int32_t eff, l2;
    (void)unused;
    printf("memory: the variants are freed with the disc (%s)\n", ga_enabled() ? "the guard heap" : "malloc; "
           "SA1_GUARD_ALLOC=1 for the guard heap");
    memset(&h, 0, sizeof h);
    h.struct_size = sizeof h;
    h.api_version = SA1_API_VERSION;
    h.log = log_cb;
    h.log_level = SA1_LOG_WARN;
    h.alloc = cnt_alloc;
    h.free = cnt_free;
    g_live = 0;
    if (sa1_init(&h, &c) != SA1_OK) {
        g_fail++;
        return;
    }
    st = load_sonic(c, SA1_LOAD_DEFAULT);
    eff = archive_named(c, SA1_CHAR_SONIC, "SON_EFF");
    l2 = texture_named(c, SA1_CHAR_SONIC, eff, "lcharge02");
    memset(&tv, 0, sizeof tv);
    tv.struct_size = sizeof tv;
    sv = sa1_texture_variant_get(c, SA1_CHAR_SONIC, (uint32_t)eff, (uint32_t)l2, SA1_TEXTURE_VARIANT_SUPER_GOLD, &tv);
    CHECK(!SA1_FAILED(st) && l2 >= 0 && sv == SA1_ERR_NOT_FOUND, "Sonic loaded without SA1_LOAD_SUPER_FORMS: no variant (%s)",
          sa1_status_string(sv));
    sa1_unload_disc(c);
    for (i = 0; i < 3; i++) {
        st = load_sonic(c, SA1_LOAD_DEFAULT | SA1_LOAD_SUPER_FORMS);
        eff = archive_named(c, SA1_CHAR_SONIC, "SON_EFF");
        l2 = texture_named(c, SA1_CHAR_SONIC, eff, "lcharge02");
        memset(&tv, 0, sizeof tv);
        tv.struct_size = sizeof tv;
        sv = sa1_texture_variant_get(c, SA1_CHAR_SONIC, (uint32_t)eff, (uint32_t)l2, SA1_TEXTURE_VARIANT_SUPER_GOLD, &tv);
        ok &= !SA1_FAILED(st) && sv == SA1_OK && tv.rgba != NULL;
        if (sv == SA1_OK && tv.rgba != NULL) {
            /* the pixels are the rule's (sa1_texture_gold_texel) of this load's texture, every texel read */
            sa1_texture_info ti;
            uint32_t p, bad = 0;
            memset(&ti, 0, sizeof ti);
            ti.struct_size = sizeof ti;
            if (sa1_texture_get(c, SA1_CHAR_SONIC, (uint32_t)eff, (uint32_t)l2, &ti) != SA1_OK || ti.rgba == NULL) {
                bad = 1;
            }
            for (p = 0; !bad && p < tv.width * tv.height; p++) {
                uint8_t g[4];
                sa1_texture_gold_texel(ti.rgba + 4u * p, g);
                bad += memcmp(g, tv.rgba + 4u * p, 4) != 0;
            }
            ok &= bad == 0;
        }
        sa1_unload_disc(c);
        after[i] = g_live;
    }
    sa1_shutdown(c);
    CHECK(ok && after[0] == after[1] && after[1] == after[2] && g_live == 0,
          "three loads with the forms: the variant each time; live blocks after each unload %ld / %ld / %ld, after "
          "sa1_shutdown %ld", after[0], after[1], after[2], g_live);
    if (ga_enabled()) {
        ga_stats gs;
        ga_stats_get(&gs);
        CHECK(gs.damaged == 0 && ga_check() == 0, "the guard heap: no damaged block (%llu allocations, %llu live)",
              (unsigned long long)gs.allocs, (unsigned long long)gs.live);
    }
}

int main(int argc, char **argv)
{
    static const struct { const char *name; void (*fn)(sa1_context *); } tests[] = {
        { "variant", t_variant }, { "lsd_gold", t_lsd_gold }, { "identity", t_identity }, { "merges", t_merges },
        { "unchanged", t_unchanged }, { "memory", t_memory },
    };
    const char *only = NULL;
    sa1_context *ctx = NULL;
    sa1_host h;
    sa1_coords co;
    sa1_load_options lo;
    sa1_status st;
    size_t i;
    int a;
    ga_args(&argc, argv);
    for (a = 1; a < argc; a++) {
        if (!strcmp(argv[a], "--verbose")) {
            g_verbose = 1;
        } else if (!strcmp(argv[a], "--only") && a + 1 < argc) {
            only = argv[++a];
        } else {
            g_gdi = argv[a];
        }
    }
    if (g_gdi == NULL) {
        g_gdi = getenv("SA1_TEST_GDI");
    }
    if (g_gdi == NULL) {
        printf("usage: gold_test [--only NAME] [--verbose] GDI\n");
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
    printf("libsa1 %s (SA1_API_SUPER_POLISH %d, SA1_DRAW_SUPER_GOLD 0x%X, SA1_TEXTURE_VARIANT_SUPER_GOLD %u, "
           "SA1_SUPER_GOLD_HUE %d, SA1_SUPOPT_LSD_GOLD %u)\n", sa1_version_string(), SA1_API_SUPER_POLISH,
           (unsigned)SA1_DRAW_SUPER_GOLD, (unsigned)SA1_TEXTURE_VARIANT_SUPER_GOLD, (int)SA1_SUPER_GOLD_HUE,
           (unsigned)SA1_SUPOPT_LSD_GOLD);
    for (i = 0; i < sizeof tests / sizeof tests[0]; i++) {
        if (only != NULL && strcmp(only, tests[i].name) != 0) {
            continue;
        }
        tests[i].fn(ctx);
    }
    sa1_shutdown(ctx);
    printf("gold_test: %d ok, %d failed, %d skipped\n", g_ok, g_fail, g_skip);
    return g_fail ? 1 : 0;
}
