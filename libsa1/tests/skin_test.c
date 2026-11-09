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
 * Round 1.0.0-beta1 (SA1_API_SKIN 3; sadc docs/research/gsa1_100_select.md 7.1 S1-S5, the "100-S" lines): up to 16 skins at once on archives 64-79 (Metal is the first loaded here:
 * 64; Mecha Sonic the second: 65), Mecha Sonic's table (LANDING.md B of sadc build/staging/upgdisp), the trails in the skin's hue, the display set of the upgrades under a skin.
 * 100-S6 (select 4.2.1, 7.1 S6; the owner's answer 17, rounds plan 14.1bl): Mecha's entry is feat/sh_skins.lua's (rows 12, 13 -> 11, lift 0, no walk or fast rows): rows 12 and 13 drawn as the live
 * row 11 at k (1.6, 1.65) its rate, the plant (his lowest point on Sonic's own), r1-verify-s's feet phases against plain Sonic and Metal in lockstep; Metal's flown rows never in Sonic's tree.
 * Round 1.0.0-beta1a (lane b1a-mecha; the "1a-M" lines and the 100-S6 lines changed with it): on his feet his lowest point is on the floor (or as deep as Sonic's own toes dip), frame by frame as
 * Garry's Mod draws him (alpha 0 and 0.5 after each pair); the jog never faster than Sonic's own fastest (SA1_SKIN_ANIM_PACE), its phase read off the render frame by frame; the options; Metal's
 * draws byte for byte as 1.0.0-beta1's. Frames between two pairs need sa1_character.c's call of sa1k_frame (render_build): without it 1a-M1, M2, M4 and M7 FAIL (the lane's fix run: they had
 * only said "PENDING", and the gate was green with half of the frames broken). The fix run adds 1a-M8 (no change of pace at the switch), 1a-M9 (no stall on a stop) and 1a-M10 (slopes).
 */
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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

/* round 1.0.0-beta1a, the lane's fix run: the libsa1 self-check lines of a skin of Sonic's own tree, as G-SA1's log shows them (INFO; 1a-M11) */
static int g_said_feet, g_said_frames;

static void log_cb(void *user, int32_t level, const char *msg)
{
    (void)user;
    if (level == SA1_LOG_WARN && !g_quiet_log) {       /* (the warnings, as before the host's level went down to INFO) */
        printf("    [libsa1] %s\n", msg);
    }
    if (level == SA1_LOG_INFO && strncmp(msg, "skin id:mechasonic: ", 20) == 0) {
        if (strstr(msg, ": on his feet (D9) on, the jog's top (D10) 1.00 x Sonic's own fastest jog") != NULL) {
            if (!g_said_feet) {
                printf("  info  libsa1 said: %s\n", msg);
            }
            g_said_feet++;
        }
        if (strstr(msg, ": the 60 Hz frames between two pairs are drawn by sa1k_frame") != NULL) {
            if (!g_said_frames) {
                printf("  info  libsa1 said: %s\n", msg);
            }
            g_said_frames++;
        }
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
static const uint16_t k_fast[1][2] = { {9, 132} };
static sa1_skin_desc g_skin;
/* round 1.0.0-beta1: the skin create() wears, the archive tally_of counts as the skin's, the parts' parents s3_run checks */
static const sa1_skin_desc *g_cur = &g_skin;
static uint32_t g_tally_arch = SA1_ARCHIVE_SKIN_FIRST;
static const int8_t k_parent_metal[25] = { -1, 0, 1, 0, 3, 4, 5, 6, 6, 0, 9, 10, 11, 12, 12, 0, 15, 16, 17, 17, 0, 20, 21, 22, 22 };
static const int8_t *g_parent = k_parent_metal;
static uint32_t g_nparts = 25u;

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
    g_skin.anim_fast = k_fast;
    g_skin.num_anim_fast = 1;
    g_skin.fast_on = 0.25f;
    g_skin.fast_off = 0.215f;
    g_skin.glow_rgb[0] = 0.55f;
    g_skin.glow_rgb[1] = 0.10f;
    g_skin.glow_rgb[2] = 1.00f;
    g_skin.glow_inflate = 0.30f;
    /* round 1.0.0-beta1: the entry's id and trail (feat/sh_skins.lua: Metal's purple, his glow's colour) */
    g_skin.id = "metalsonic";
    g_skin.base_kind = SA1_CHAR_SONIC;
    g_skin.trail_rgb[0] = 0.55f;
    g_skin.trail_rgb[1] = 0.10f;
    g_skin.trail_rgb[2] = 1.00f;
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
    d.skin = (flags & SA1_CREATE_SKIN) ? g_cur : NULL;
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
        if ((uint32_t)d->archive == g_tally_arch) {
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
    double worst_joint = 0.0, bone[SA1K_PARTS_MAX];
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
            const int8_t *parent = g_parent;     /* round 1.0.0-beta1: Metal's 25 or Mecha's 20 */
            for (i = 1; i < g_nparts; i++) {
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
    CHECK(ball_pairs > 0u && bad_ball == 0u && parts_in_ball == 0u, "S7 %s a jump (state row 14): the ball with archive %u on all %u pairs of it (%u bad), no parts (%u)", what, (unsigned)g_tally_arch, (unsigned)ball_pairs, (unsigned)bad_ball, (unsigned)parts_in_ball);
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

/* ---- S10: the way between a walk and a run (round 0.9.9g; the owner: "Metal Sonic's transition from run>walk and walk>run uses Sonic's animation instead of Super Sonic's") ------------------------- */
typedef struct t_out {
    int walk_pairs, rise_pairs, rise_live, link_pairs, link_live, fast_pairs, fast_live;
    int src_moved, formula_pairs;
    double worst_src, worst_formula, worst_turn, worst_step;
} t_out;

static void t_run(const char *what, int n, float (*stick)(int), t_out *o)
{
    sa1_character_id id = 0;
    sa1k_test_info *ti = (sa1k_test_info *)malloc(sizeof *ti), *tp = (sa1k_test_info *)malloc(sizeof *tp);
    int p, have_prev = 0;
    memset(o, 0, sizeof *o);
    if (create(SA1_CREATE_SKIN, &id) != SA1_OK) {
        CHECK(0, "S10 create");
        return;
    }
    for (p = 0; p < n; p++) {
        sa1_state_ex x;
        const sa1_render *r = NULL;
        uint32_t i;
        tick(id, 0.0f, stick(p), 0u);
        state_of(id, &x);
        sa1_character_render(g_ctx, id, 1.0f, &r);
        info_of(id, ti);
        /* The rows of the way up and down: while the game's row is one of Sonic's running rows, or linking next to one, none of Sonic's own pose is in the parts:
           he is the flown row, or the walk's held pose fading into it */
        if (ti->drew && x.action >= 10 && x.action <= 13 && ti->w > 0.0f && ti->w < 1.0f) {
            o->rise_pairs++;
            o->rise_live += ti->live_mix > 0.0f;
        }
        if (ti->drew && x.action != x.req_action && x.action >= 9 && x.action <= 13 && ti->drive_row >= 9 && ti->drive_row <= 13) {
            o->link_pairs++;
            o->link_live += ti->live_mix > 0.0f && ti->w > 0.0f;
        }
        if (ti->drew && x.action == 9 && x.player_speed[0] >= 0.25f && ti->w > 0.0f) {
            o->fast_pairs++;
        }
        if (ti->drew && x.action == 9 && x.player_speed[0] >= 0.25f && ti->live_mix > 0.0f) {
            o->fast_live++;
        }
        if (ti->drew && x.action == 9 && x.action == x.req_action && x.player_speed[0] < 0.21f && ti->w <= 0.0f) {
            o->walk_pairs++;
        }
        if (ti->w > 0.0f && ti->w < 1.0f && ti->drew && tp->drew && tp->w > 0.0f && have_prev) {
            /* no pop: from one pair to the next, against the torso */
            for (i = 0; i < 25u; i++) {
                double a[9], b[9], d;
                mul3_t(tp->r[i], tp->r[0], a);
                mul3_t(ti->r[i], ti->r[0], b);
                d = m3_angle_between(a, b);
                if (d > o->worst_turn) {
                    o->worst_turn = d;
                }
            }
            /* ... and the torso's own place against the character's */
            {
                double dx = (ti->p[0][0] - ti->fly_root[3]) - (tp->p[0][0] - tp->fly_root[3]);
                double dy = (ti->p[0][1] - ti->fly_root[7]) - (tp->p[0][1] - tp->fly_root[7]);
                double dz = (ti->p[0][2] - ti->fly_root[11]) - (tp->p[0][2] - tp->fly_root[11]);
                double d = sqrt(dx * dx + dy * dy + dz * dz);
                if (d > o->worst_step) {
                    o->worst_step = d;
                }
            }
        }
        /* a fade in: the source stays the held pose against the root (no motion of Sonic's in it); each part lies on the way from it to the flown pose, at w, by other code than the library's: the angles add up */
        if (ti->w > 0.0f && ti->w < 1.0f && ti->live_mix <= 0.0f && ti->drew && ti->have_flown) {
            for (i = 0; i < 25u; i++) {
                double whole, part, here, a[9], rt[9];
                rt[0] = ti->fly_root[0]; rt[1] = ti->fly_root[1]; rt[2] = ti->fly_root[2];
                rt[3] = ti->fly_root[4]; rt[4] = ti->fly_root[5]; rt[5] = ti->fly_root[6];
                rt[6] = ti->fly_root[8]; rt[7] = ti->fly_root[9]; rt[8] = ti->fly_root[10];
                mul3(rt, ti->src_r[i], a);
                whole = m3_angle_between(a, ti->flown_r[i]);
                part = m3_angle_between(a, ti->r[i]);
                here = m3_angle_between(ti->r[i], ti->flown_r[i]);
                if (fabs((part + here) - whole) > o->worst_formula) {
                    o->worst_formula = fabs((part + here) - whole);
                }
                if (whole > 1.0 && fabs(part / whole - (double)ti->w) > o->worst_formula) {
                    o->worst_formula = fabs(part / whole - (double)ti->w);
                }
            }
            o->formula_pairs++;
            if (have_prev && tp->live_mix <= 0.0f && tp->w > 0.0f && tp->w < 1.0f && tp->have_flown) {
                for (i = 0; i < 25u; i++) {
                    double d = m3_angle_between(ti->src_r[i], tp->src_r[i]);
                    if (d > o->worst_src) {
                        o->worst_src = d;
                    }
                }
            }
        }
        memcpy(tp, ti, sizeof *tp);
        have_prev = 1;
    }
    printf("  info  %s: %d pairs of a running row fading, %d with Sonic's own pose in them; %d link pairs, %d with it; %d fast pairs of row 9 flying; %d pairs of a plain slow walk; worst turn %.1f degrees, worst torso step %.3f units\n",
           what, o->rise_pairs, o->rise_live, o->link_pairs, o->link_live, o->fast_pairs, o->walk_pairs, o->worst_turn, o->worst_step);
    sa1_character_destroy(g_ctx, id);
    free(ti);
    free(tp);
}

static float t_step(int p) { return p < 30 ? 0.0f : (p < 200 ? 1.0f : 0.0f); }
static float t_ramp(int p)
{
    if (p < 30) {
        return 0.0f;
    }
    if (p < 180) {
        return (float)(p - 30) / 150.0f;
    }
    if (p < 300) {
        return 1.0f;
    }
    return p < 450 ? 1.0f - (float)(p - 300) / 150.0f : 0.0f;
}
/* a ramp up over g_ramp pairs, 60 at the top, down over as many */
static int g_ramp;
static float t_rate(int p)
{
    if (p < 30) {
        return 0.0f;
    }
    if (p < 30 + g_ramp) {
        return (float)(p - 30) / (float)g_ramp;
    }
    if (p < 90 + g_ramp) {
        return 1.0f;
    }
    return p < 90 + 2 * g_ramp ? 1.0f - (float)(p - 90 - g_ramp) / (float)g_ramp : 0.0f;
}
static float t_walk(int p) { return p < 30 ? 0.0f : 0.75f; }
static float t_walk_run(int p) { return p < 30 ? 0.0f : (p < 150 ? 0.75f : (p < 300 ? 1.0f : (p < 340 ? 0.0f : 0.75f))); }

static void test_s10(void)
{
    t_out a, b, c, d;
    printf("S10 the way between a walk and a run\n");
    t_run("the stick pushed, then let go", 300, t_step, &a);
    t_run("a stick ramp up and down", 520, t_ramp, &b);
    t_run("a slow walk", 200, t_walk, &c);
    t_run("walk, run, stop, walk", 460, t_walk_run, &d);
    {
        static const int rates[5] = { 5, 20, 45, 90, 240 };
        int k, live = 0, fast = 0, seen = 0;
        double turn = 0.0;
        for (k = 0; k < 5; k++) {
            t_out o;
            char what[64];
            g_ramp = rates[k];
            snprintf(what, sizeof what, "a ramp over %d pairs each way", rates[k]);
            t_run(what, 90 + 2 * rates[k] + 40, t_rate, &o);
            live += o.rise_live + o.link_live + o.fast_live;
            fast += o.fast_pairs;
            seen += o.rise_pairs + o.link_pairs;
            if (o.worst_turn > turn) {
                turn = o.worst_turn;
            }
        }
        CHECK(seen > 0 && fast > 0 && live == 0, "S10 ramps at five rates, both ways: %d transition pairs and %d of the walk's top, Sonic's own pose in none (%d)", seen, fast, live);
        CHECK(turn < 45.0, "S10 ramps at five rates: no pop, at most %.1f degrees a pair against the torso", turn);
    }
    CHECK(a.rise_pairs + b.rise_pairs + d.rise_pairs > 0 && a.rise_live + b.rise_live + d.rise_live == 0, "S10 walk > run: in %d pairs of a running row still fading in, Sonic's own pose is in none (%d)", a.rise_pairs + b.rise_pairs + d.rise_pairs, a.rise_live + b.rise_live + d.rise_live);
    CHECK(a.link_pairs + b.link_pairs + d.link_pairs > 0 && a.link_live + b.link_live + d.link_live == 0, "S10 the game's blends (mtnmode 2) between rows 9 to 13: %d pairs, Sonic's own blend is in none (%d)", a.link_pairs + b.link_pairs + d.link_pairs, a.link_live + b.link_live + d.link_live);
    CHECK(a.fast_live + b.fast_live + c.fast_live + d.fast_live == 0, "S10 the top of row 9: Sonic's own pose in no pair of it (%d)", a.fast_live + b.fast_live + c.fast_live + d.fast_live);
    CHECK(a.fast_pairs + b.fast_pairs + d.fast_pairs > 0, "S10 the top of row 9, faster than any held walk: he flies (%d pairs)", a.fast_pairs + b.fast_pairs + d.fast_pairs);
    CHECK(c.walk_pairs > 100 && c.fast_pairs == 0, "S10 a slow walk stays Sonic's walk: %d pairs of it, %d flying", c.walk_pairs, c.fast_pairs);
    CHECK(a.formula_pairs + b.formula_pairs + d.formula_pairs > 0 && a.worst_src < 1e-5 && b.worst_src < 1e-5 && d.worst_src < 1e-5, "S10 a fade in holds the walk's last pose against his root: it moves %.2e degrees (%d pairs)", a.worst_src, a.formula_pairs + b.formula_pairs + d.formula_pairs);
    CHECK(a.worst_formula < 0.03 && b.worst_formula < 0.03 && d.worst_formula < 0.03, "S10 each part lies on the way from the held pose to the flown one, at w (worst %.4f, %.4f, %.4f)", a.worst_formula, b.worst_formula, d.worst_formula);
    CHECK(a.worst_turn < 45.0 && b.worst_turn < 45.0 && d.worst_turn < 45.0, "S10 no pop: while the fade runs no part turns more than 45 degrees a pair against the torso (%.1f, %.1f, %.1f; Sonic's own arms turn up to 100, MEASURED, S4)", a.worst_turn, b.worst_turn, d.worst_turn);
    CHECK(a.worst_step < 1.0 && b.worst_step < 1.0 && d.worst_step < 1.0, "S10 no pop: the torso's place against the root moves under 1 unit a pair while the fade runs (%.3f, %.3f, %.3f)", a.worst_step, b.worst_step, d.worst_step);
    /* a stop: from the stick let go to standing as Sonic stands: he does not hang in the air for the game's long link from a walk to the stand */
    {
        sa1_character_id id = 0;
        sa1k_test_info *ti = (sa1k_test_info *)malloc(sizeof *ti);
        int p, stopped = -1, landed = -1;
        create(SA1_CREATE_SKIN, &id);
        for (p = 0; p < 300; p++) {
            sa1_state_ex x;
            tick(id, 0.0f, t_step(p), 0u);
            state_of(id, &x);
            info_of(id, ti);
            if (p >= 200 && stopped < 0 && x.player_speed[0] <= 0.0f) {
                stopped = p;
            }
            if (stopped >= 0 && landed < 0 && ti->w <= 0.0f) {
                landed = p;
            }
        }
        CHECK(stopped > 0 && landed > 0 && landed - stopped <= 13, "S10 a stop: stopped at pair %d, on his feet at %d: at most 13 pairs (4 or 5 of flight through the link, 8 of fade)", stopped, landed);
        sa1_character_destroy(g_ctx, id);
        free(ti);
    }
    /* gsa1_metal_walk_fly: the walk flies, and the option off leaves him Sonic's all the way */
    {
        sa1_character_id id = 0;
        sa1k_test_info *ti = (sa1k_test_info *)malloc(sizeof *ti);
        int p, flown = 0, off_w = 0;
        create(SA1_CREATE_SKIN, &id);
        sa1_character_set_option(g_ctx, id, SA1_COPT_SKIN_ANIM, SA1_SKIN_ANIM_FLY | SA1_SKIN_ANIM_WALK);
        for (p = 0; p < 120; p++) {
            tick(id, 0.0f, p < 20 ? 0.0f : 0.75f, 0u);
            info_of(id, ti);
            flown += ti->cur == 131 && ti->w >= 1.0f;
        }
        CHECK(flown > 50, "S10 gsa1_metal_walk_fly: the slow walk flies, row 131 (%d pairs)", flown);
        sa1_character_destroy(g_ctx, id);
        create(SA1_CREATE_SKIN, &id);
        sa1_character_set_option(g_ctx, id, SA1_COPT_SKIN_ANIM, 0);
        for (p = 0; p < 300; p++) {
            tick(id, 0.0f, t_step(p), 0u);
            info_of(id, ti);
            off_w += ti->w > 0.0f;
        }
        CHECK(off_w == 0, "S10 gsa1_metal_fly off: never a flown pair (%d)", off_w);
        sa1_character_destroy(g_ctx, id);
        free(ti);
    }
}

/* ---- S11: the Light Speed glow (round 0.9.9g; the owner: "copy the character model and inflate it a bit and add it with a purple colour") ------------------------------------------------------------- */
static uint32_t glow_tally(const sa1_render *r, uint8_t col[3][3], uint32_t per)
{
    uint32_t i, tris = 0, seen = 0;
    if (col) {
        memset(col, 0, 9);
    }
    if (r == NULL) {
        return 0;
    }
    for (i = 0; i < r->num_draws; i++) {
        const sa1_draw *d = &r->draws[i];
        if (d->prim != SA1_PRIM_TRIANGLES || d->source != SA1_SOURCE_MODEL || !(d->flags & SA1_DRAW_ADDITIVE) || (d->flags & SA1_DRAW_EFFECT) || d->blend_src != SA1_BLEND_ONE || d->blend_dst != SA1_BLEND_ONE || d->archive != -1) {
            continue;
        }
        tris += d->count;
        if (col && per) {
            uint32_t t;
            for (t = 0; t < d->count; t++, seen++) {
                if (seen % per == 0u && seen / per < 3u) {
                    memcpy(col[seen / per], r->colors + 12u * (d->first + t), 3);
                }
            }
        }
    }
    return tris;
}

static void test_s11(void)
{
    sa1_character_id a = 0, b = 0;
    int p, charged = 0, both = 0, differ = 0, bad_flags = 0, bad_col = 0, mids = 0, nans = 0, pulse_up = 0, pulse_down = 0, first_off = 0;
    uint8_t last_g = 0;
    int have_g = 0;
    double worst_infl = 0.0, worst_mid = 0.0;
    uint32_t tris_max = 0, plain_parts = 0;
    printf("S11 the Light Speed glow\n");
    create(0, &a);
    create(SA1_CREATE_SKIN, &b);
    upgrades_on(a);
    upgrades_on(b);
    for (p = 0; p < 300; p++) {
        const sa1_render *ra = NULL, *rb = NULL;
        uint8_t ca[3][3], cb[3][3];
        uint32_t ga, gb, i;
        uint32_t but = (p >= 30 && p < 120) || (p >= 180 && p < 260) ? SA1_BUTTON_ACTION : 0u;
        float sy = (p >= 120 && p < 180) ? 1.0f : 0.0f;
        tally t;
        tick(a, 0.0f, sy, but);
        tick(b, 0.0f, sy, but);
        sa1_character_render(g_ctx, a, 1.0f, &ra);
        sa1_character_render(g_ctx, b, 1.0f, &rb);
        ga = glow_tally(ra, ca, 290u);
        gb = glow_tally(rb, cb, 1015u);
        tally_of(rb, &t);
        charged += ga > 0u;
        both += ga > 0u && gb > 0u;
        differ += (ga > 0u) != (gb > 0u) && t.skin_tris > 0u;
        if (gb > tris_max) {
            tris_max = gb;
        }
        plain_parts += gb > 0u && t.skin_tris != 1015u;
        if (gb == 0u) {
            continue;
        }
        /* the same state as Sonic's: blended, unlit, additive */
        for (i = 0; i < rb->num_draws; i++) {
            const sa1_draw *d = &rb->draws[i];
            if (d->prim == SA1_PRIM_TRIANGLES && d->source == SA1_SOURCE_MODEL && d->archive == -1 && (d->flags & SA1_DRAW_ADDITIVE) && (d->flags & (SA1_DRAW_BLEND | SA1_DRAW_UNLIT)) != (SA1_DRAW_BLEND | SA1_DRAW_UNLIT)) {
                bad_flags++;
            }
        }
        /* the colour from the game's own colour of the same pair: skin * blue + (1 - skin) * green, shared out so that the three add up to the colour once */
        for (i = 0; i < 3u; i++) {
            static const double s[3] = { 0.55, 0.10, 1.00 };
            double gg = (double)ca[i][1] / 255.0, bb = (double)ca[i][2] / 255.0;
            int k;
            for (k = 0; k < 3; k++) {
                double want = 255.0 * (s[k] * bb + (1.0 - s[k]) * gg) / 2.3;
                if (fabs(want - (double)cb[i][k]) > 3.0) {
                    bad_col++;
                }
            }
        }
        /* the pulse goes up and down with the game's */
        if (have_g) {
            pulse_up += ca[0][1] > last_g;
            pulse_down += ca[0][1] < last_g;
        }
        last_g = ca[0][1];
        have_g = 1;
        /* the mid-pair interpolation stays whole */
        {
            const sa1_render *rm = NULL;
            sa1_character_render(g_ctx, b, 0.5f, &rm);
            if (rm != NULL) {
                uint32_t v, sf = 0xFFFFFFFFu, gf = 0xFFFFFFFFu;
                mids++;
                for (v = 0; v < 9u * rm->num_triangles; v++) {
                    if (!sa1_finite(rm->positions[v])) {
                        nans++;
                    }
                }
                /* the shells stay on their parts at mid pair: the outermost within the inflation (4 x 0.30 host units) and a little of each corner */
                for (i = 0; i < rm->num_draws; i++) {
                    const sa1_draw *d = &rm->draws[i];
                    if (d->prim != SA1_PRIM_TRIANGLES) {
                        continue;
                    }
                    if ((uint32_t)d->archive == SA1_ARCHIVE_SKIN_FIRST && sf == 0xFFFFFFFFu) {
                        sf = d->first;
                    }
                    if (d->archive == -1 && (d->flags & SA1_DRAW_ADDITIVE) && d->source == SA1_SOURCE_MODEL && gf == 0xFFFFFFFFu) {
                        gf = d->first;
                    }
                }
                for (v = 0; v < 3u * 1015u && sf != 0xFFFFFFFFu && gf != 0xFFFFFFFFu; v++) {
                    const float *a0 = rm->positions + 3u * sf * 3u + 3u * v;
                    const float *b0 = rm->positions + 3u * (gf + 2u * 1015u) * 3u + 3u * v;
                    double dx = (double)b0[0] - (double)a0[0], dy = (double)b0[1] - (double)a0[1], dz = (double)b0[2] - (double)a0[2], e = sqrt(dx * dx + dy * dy + dz * dz);
                    if (e > worst_mid) {
                        worst_mid = e;
                    }
                }
            }
        }
        if (!first_off) {
            /* inflation: the three shells stand over the part's own vertices by a third, two thirds and all of glow_inflate, along each vertex's normal (host units: 4 per SA1 unit) */
            const sa1_render *r1 = NULL;
            first_off = 1;
            sa1_character_render(g_ctx, b, 1.0f, &r1);
            if (r1 != NULL) {
                uint32_t skin_first = 0xFFFFFFFFu, glow_first = 0xFFFFFFFFu, c;
                for (i = 0; i < r1->num_draws; i++) {
                    const sa1_draw *d = &r1->draws[i];
                    if (d->prim != SA1_PRIM_TRIANGLES) {
                        continue;
                    }
                    if ((uint32_t)d->archive == SA1_ARCHIVE_SKIN_FIRST && skin_first == 0xFFFFFFFFu) {
                        skin_first = d->first;
                    }
                    if (d->archive == -1 && (d->flags & SA1_DRAW_ADDITIVE) && d->source == SA1_SOURCE_MODEL && glow_first == 0xFFFFFFFFu) {
                        glow_first = d->first;
                    }
                }
                for (c = 0; c < 3u * 1015u && skin_first != 0xFFFFFFFFu && glow_first != 0xFFFFFFFFu; c++) {
                    int shell;
                    for (shell = 0; shell < 3; shell++) {
                        const float *a0 = r1->positions + 3u * skin_first * 3u + 3u * c;
                        const float *b0 = r1->positions + 3u * (glow_first + (uint32_t)shell * 1015u) * 3u + 3u * c;
                        const float *n0 = r1->normals + 3u * skin_first * 3u + 3u * c;
                        double dx = (double)b0[0] - (double)a0[0], dy = (double)b0[1] - (double)a0[1], dz = (double)b0[2] - (double)a0[2];
                        double want = 4.0 * 0.30 * (double)(shell + 1) / 3.0;
                        double e = sqrt((dx - want * n0[0]) * (dx - want * n0[0]) + (dy - want * n0[1]) * (dy - want * n0[1]) + (dz - want * n0[2]) * (dz - want * n0[2]));
                        if (e > worst_infl) {
                            worst_infl = e;
                        }
                    }
                }
            }
        }
    }
    printf("  info  a charge: %d pairs of Sonic's own glow (870 triangles), %d of Metal Sonic's, %d differ with parts drawn; %u triangles a pair (3 x 1015)\n", charged, both, differ, (unsigned)tris_max);
    CHECK(charged > 0 && both > 0 && differ == 0, "S11 the glow draws in exactly the pairs Sonic's would, %d pairs, %d differ", charged, differ);
    CHECK(tris_max == 3045u && plain_parts == 0u, "S11 three shells of the 25 parts: %u triangles, 3 x 1015; the parts themselves stay 1015 triangles on archive 61 (%u pairs off)", (unsigned)tris_max, (unsigned)plain_parts);
    CHECK(bad_flags == 0, "S11 blended, unlit, additive, ONE / ONE, untextured, source MODEL: Sonic's aura's own state (%d off)", bad_flags);
    CHECK(bad_col == 0, "S11 the colour follows the game's own colour of the pair, purple (%d channels off)", bad_col);
    CHECK(pulse_up > 0 && pulse_down > 0, "S11 Sonic's pulse: the glow's colour rises in %d pairs and falls in %d", pulse_up, pulse_down);
    CHECK(worst_infl < 0.02, "S11 the inflation: each shell's vertex stands off the part's by k/3 of glow_inflate along its normal (worst %.4f host units)", worst_infl);
    CHECK(mids > 0 && nans == 0, "S11 the mid-pair output stays finite (%d pairs, %d bad)", mids, nans);
    CHECK(worst_mid < 1.3, "S11 at mid pair the outermost shell stays on its part: at most %.3f host units off (1.2 is the inflation)", worst_mid);
    sa1_character_destroy(g_ctx, a);
    sa1_character_destroy(g_ctx, b);
    /* the option off: none, at once */
    create(SA1_CREATE_SKIN, &b);
    upgrades_on(b);
    sa1_character_set_option(g_ctx, b, SA1_COPT_SKIN_ANIM, SA1_SKIN_ANIM_FLY);
    {
        uint32_t off = 0, on = 0;
        for (p = 0; p < 118; p++) {
            const sa1_render *r = NULL;
            tick(b, 0.0f, 0.0f, p >= 30 ? SA1_BUTTON_ACTION : 0u);
            sa1_character_render(g_ctx, b, 1.0f, &r);
            off += glow_tally(r, NULL, 0u);
        }
        CHECK(off == 0u, "S11 gsa1_metal_glow off: no glow (%u triangles)", (unsigned)off);
        sa1_character_set_option(g_ctx, b, SA1_COPT_SKIN_ANIM, SA1_SKIN_ANIM_FLY | SA1_SKIN_ANIM_GLOW);
        for (p = 0; p < 2; p++) {
            const sa1_render *r = NULL;
            tick(b, 0.0f, 0.0f, SA1_BUTTON_ACTION);
            sa1_character_render(g_ctx, b, 1.0f, &r);
            on = glow_tally(r, NULL, 0u);
        }
        CHECK(on == 3045u, "S11 ... and on again at once, no re-creation: %u triangles", (unsigned)on);
        /* the cost: one rebuild of a pair's output, with the glow and without it. What the draw itself costs in Garry's Mod only the game can show */
        {
            sa1k_char *sc = sa1k_char_of(g_ctx, b);
            sa1_meshbuf *mb = (sa1_meshbuf *)calloc(1, sizeof *mb);
            const sa1_charset *cs = &g_ctx->chars[SA1_CHAR_SONIC];
            clock_t t0;
            enum { N = 3000 };
            double on_ms, off_ms;
            int k;
            t0 = clock();
            for (k = 0; k < N; k++) {
                sa1_meshbuf_reset(mb);
                sa1k_append(g_ctx, sc, cs, mb);
            }
            on_ms = 1000.0 * (double)(clock() - t0) / CLOCKS_PER_SEC / N;
            sa1k_set_anim(sc, SA1_SKIN_ANIM_FLY);
            t0 = clock();
            for (k = 0; k < N; k++) {
                sa1_meshbuf_reset(mb);
                sa1k_append(g_ctx, sc, cs, mb);
            }
            off_ms = 1000.0 * (double)(clock() - t0) / CLOCKS_PER_SEC / N;
            sa1k_set_anim(sc, SA1_SKIN_ANIM_FLY | SA1_SKIN_ANIM_GLOW);
            printf("  info  the cost: building a pair's parts takes %.3f ms with the glow, %.3f ms without (once per 30 Hz pair, in this test's build); %u bytes held for the three shells\n", on_ms, off_ms, (unsigned)sa1k_test_glow_bytes(sc));
            sa1_meshbuf_free(g_ctx, mb);
            free(mb);
        }
    }
    sa1_character_destroy(g_ctx, b);
}

/* ---- S12: a follower wears the skin (round 0.9.9h; the owner: "can you add a Metal Sonic follower?") -------------------------------------------------------------
 * sa1_follower_create with SA1_FOLLOW_SKIN: the follower is a Sonic follower made with SA1_CREATE_SKIN. Its world is a Sonic follower's, byte for byte (states, events, the whole
 * arena against a control of two plain ones), only its render output is the skin's: no draw of Sonic's body, the parts on archive 61, the flying while it runs. A skin follower
 * leads a skin follower (a chain). The cost of the render step per pair: an info line, not a check. */
static sa1_status s12_follower(sa1_character_id leader, int skin, int32_t kind, uint32_t size, sa1_character_id *out)
{
    sa1_follower_desc d;
    memset(&d, 0, sizeof d);
    d.struct_size = size ? size : (uint32_t)sizeof d;
    d.kind = kind;
    d.leader = leader;
    d.flags = SA1_FOLLOW_SEED | SA1_FOLLOW_NO_SET | (skin ? SA1_FOLLOW_SKIN : 0u);
    d.seed = 0x5EED0099u;
    d.skin = skin == 1 ? &g_skin : NULL;
    *out = 0;
    return sa1_follower_create(g_ctx, &d, out);
}

typedef struct s12_ev {
    sa1_event *e;
    uint32_t n, cap;
} s12_ev;

static void s12_keep(s12_ev *k, const sa1_event *e)
{
    if (k->n == k->cap) {
        k->cap = k->cap ? 2u * k->cap : 1024u;
        k->e = (sa1_event *)realloc(k->e, (size_t)k->cap * sizeof *k->e);
    }
    k->e[k->n] = *e;
    k->e[k->n].character = 0;
    k->n++;
}

static double s12_ms(clock_t a, clock_t b)
{
    return 1000.0 * (double)(b - a) / (double)CLOCKS_PER_SEC;
}

static void test_s12(void)
{
    enum { PAIRS = 900 };
    sa1_character_id lead = 0, fa = 0, fb = 0, fc = 0, fd = 0, bad = 0;
    sa1_status s1, s2, s3;
    sa1_state_ex xa, xb;
    s12_ev ea, eb;
    uint8_t *ra, *rb, *rc;
    uint32_t i, sd = 0, ctl = 0, flg = 0, other = 0;
    int p, a_body = 0, a_skin = 0, b_body = 0, b_skin = 0, d_skin = 0, d_body = 0, flew = 0;
    float wmax = 0.0f;
    double ms_a = 0.0, ms_b = 0.0;
    printf("S12 a follower wears the skin: a Sonic follower's world, byte for byte, the skin drawn over it\n");
    memset(&ea, 0, sizeof ea);
    memset(&eb, 0, sizeof eb);
    if (create(0, &lead) != SA1_OK) {
        CHECK(0, "S12 the leader: %s", sa1_last_error(g_ctx));
        return;
    }
    s1 = s12_follower(lead, 1, SA1_CHAR_TAILS, 0, &bad);
    s2 = s12_follower(lead, 1, SA1_CHAR_SONIC, (uint32_t)offsetof(sa1_follower_desc, skin), &bad);
    s3 = s12_follower(lead, 2, SA1_CHAR_SONIC, 0, &bad);
    CHECK(s1 == SA1_ERR_INVALID_ARGUMENT && s2 == SA1_ERR_INVALID_ARGUMENT && s3 == SA1_ERR_INVALID_ARGUMENT && bad == 0,
          "S12 refused: SA1_FOLLOW_SKIN on Tails (%s), a desc that does not reach .skin (%s), .skin NULL (%s)", sa1_status_string(s1), sa1_status_string(s2), sa1_status_string(s3));
    s1 = s12_follower(lead, 0, SA1_CHAR_SONIC, 0, &fa);
    s2 = s12_follower(lead, 0, SA1_CHAR_SONIC, 0, &fc);
    s3 = s12_follower(lead, 1, SA1_CHAR_SONIC, 0, &fb);
    CHECK(s1 == SA1_OK && s2 == SA1_OK && s3 == SA1_OK, "S12 a plain Sonic follower, its control and a skin follower, one seed (%s)", s3 == SA1_OK ? "ok" : sa1_last_error(g_ctx));
    if (s1 != SA1_OK || s2 != SA1_OK || s3 != SA1_OK) {
        sa1_character_destroy(g_ctx, lead);
        return;
    }
    s1 = s12_follower(fb, 1, SA1_CHAR_SONIC, 0, &fd);
    CHECK(s1 == SA1_OK, "S12 a chain: a skin follower follows the skin follower (%s)", s1 == SA1_OK ? "ok" : sa1_last_error(g_ctx));
    CHECK(sa1_character_set_option(g_ctx, fa, SA1_COPT_SKIN_ANIM, 1) == SA1_ERR_UNSUPPORTED && sa1_character_set_option(g_ctx, fb, SA1_COPT_SKIN_ANIM, SA1_SKIN_ANIM_FLY | SA1_SKIN_ANIM_GLOW) == SA1_OK,
          "S12 SA1_COPT_SKIN_ANIM on a follower id: the skin's (ok), a plain one's (unsupported)");
    for (p = 0; p < PAIRS; p++) {
        sa1_input in, none;
        sa1_event ev[64];
        uint32_t ne;
        const sa1_render *r = NULL;
        tally t;
        sa1k_test_info ti;
        clock_t c0, c1, c2;
        s1_input(p, &in);
        memset(&none, 0, sizeof none);
        none.struct_size = sizeof none;
        none.camera_yaw = in.camera_yaw;
        sa1_character_tick(g_ctx, lead, &in, 1.0f / 30.0f, NULL, NULL);
        c0 = clock();
        sa1_character_tick(g_ctx, fa, &none, 1.0f / 30.0f, NULL, NULL);
        sa1_character_render(g_ctx, fa, 0.5f, &r);
        tally_of(r, &t);
        c1 = clock();
        a_body += t.body_tris > 0u;
        a_skin += t.skin_tris > 0u;
        sa1_character_tick(g_ctx, fb, &none, 1.0f / 30.0f, NULL, NULL);
        sa1_character_render(g_ctx, fb, 0.5f, &r);
        tally_of(r, &t);
        c2 = clock();
        b_body += t.body_tris > 0u;
        b_skin += t.skin_tris > 0u;
        ms_a += s12_ms(c0, c1);
        ms_b += s12_ms(c1, c2);
        sa1_character_tick(g_ctx, fc, &none, 1.0f / 30.0f, NULL, NULL);
        sa1_character_tick(g_ctx, fd, &none, 1.0f / 30.0f, NULL, NULL);
        sa1_character_render(g_ctx, fd, 0.5f, &r);
        tally_of(r, &t);
        d_body += t.body_tris > 0u;
        d_skin += t.skin_tris > 0u;
        info_of(fb, &ti);
        if (ti.w > wmax) {
            wmax = ti.w;
        }
        flew += ti.w > 0.99f;
        state_of(fa, &xa);
        state_of(fb, &xb);
        sd += memcmp(&xa, &xb, sizeof xa) != 0;
        do {
            ne = 0;
            sa1_poll_events(g_ctx, ev, 64, &ne);
            for (i = 0; i < ne; i++) {
                if (ev[i].character == fa) {
                    s12_keep(&ea, &ev[i]);
                } else if (ev[i].character == fb) {
                    s12_keep(&eb, &ev[i]);
                }
            }
        } while (ne == 64);
    }
    CHECK(sd == 0u, "S12 every sa1_state_ex of the skin follower is the plain one's: %u of %d pairs differ", (unsigned)sd, PAIRS);
    CHECK(ea.n > 0u && ea.n == eb.n && memcmp(ea.e, eb.e, ea.n * sizeof ea.e[0]) == 0, "S12 every event the same: %u and %u events", (unsigned)ea.n, (unsigned)eb.n);
    ra = sa1_character_world(g_ctx, fa)->ram;
    rb = sa1_character_world(g_ctx, fb)->ram;
    rc = sa1_character_world(g_ctx, fc)->ram;
    for (i = 0; i < SA1G_ARENA_SIZE; i++) {
        int c_ = ra[i] != rc[i];
        ctl += (uint32_t)c_;
        if (ra[i] != rb[i]) {
            flg++;
            if (!c_) {
                if (other < 16u) {
                    printf("  FAIL  the skin follower's world differs at guest %08X: %02X, %02X; the control does not\n", (unsigned)(0x8C000000u + i), ra[i], rb[i]);
                }
                other++;
            }
        }
    }
    CHECK(other == 0u, "S12 the whole arena: the control of two plain followers differs at %u bytes, the skin follower's at %u, %u of them where the control does not (0)", (unsigned)ctl, (unsigned)flg, (unsigned)other);
    CHECK(a_body > PAIRS * 9 / 10 && a_skin == 0, "S12 the plain follower: Sonic's body on %d of %d pairs, the skin's parts on %d (0)", a_body, PAIRS, a_skin);
    CHECK(b_body == 0 && b_skin > PAIRS * 9 / 10, "S12 the skin follower: the skin on %d of %d pairs, Sonic's body on %d (0)", b_skin, PAIRS, b_body);
    CHECK(d_body == 0 && d_skin > PAIRS * 9 / 10, "S12 the chain's skin follower: the skin on %d of %d pairs, Sonic's body on %d (0)", d_skin, PAIRS, d_body);
    CHECK(wmax > 0.99f && flew > 0, "S12 running behind the leader, the skin follower flies: the fade reached %.3f, flown on %d pairs", (double)wmax, flew);
    printf("  info  the cost of the skin, this machine: a plain Sonic follower %.3f ms a pair (tick and render), the skin follower %.3f ms (clock(), %d pairs)\n", ms_a / PAIRS, ms_b / PAIRS, PAIRS);
    CHECK(!count_faults(fa) && !count_faults(fb) && !count_faults(fc) && !count_faults(fd), "S12 no fault");
    sa1_character_destroy(g_ctx, fd);
    sa1_character_destroy(g_ctx, fb);
    sa1_character_destroy(g_ctx, fc);
    sa1_character_destroy(g_ctx, fa);
    sa1_character_destroy(g_ctx, lead);
    free(ea.e);
    free(eb.e);
}

/* ---- S8: Character Select -------------------------------------------------------------------------------------------------------------------------------- */static uint32_t hash_bytes(uint32_t h, const void *p, size_t n)
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

/* ---- round 1.0.0-beta1 (gsa1_100_select.md 7.1 S1-S5): many skins, Mecha Sonic, the trails, the display set under a skin ----------------------------- */
/* Mecha Sonic, as sadc build/staging/upgdisp/LANDING.md B gives him (numbers and names only; the bytes are read from the disc at run time) */
static const uint16_t k_mkeep[22] = { 7, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96 };
static const sa1_skin_part k_mparts[20] = {
    {1, 10, -1, -1}, {12, 22, -1, -1}, {12, 22, -1, -1}, {1, 10, -1, -1},
    {47, 55, 5, 49}, {49, 51, 6, 52}, {52, 52, -1, -1}, {52, 52, -1, -1}, {52, 52, -1, -1},
    {25, 34, 10, 27}, {27, 29, 11, 30}, {30, 30, -1, -1},
    {57, 65, 13, 59}, {59, 61, 14, 62}, {62, 62, -1, -1}, {62, 62, -1, -1}, {62, 62, -1, -1},
    {36, 45, 18, 38}, {38, 40, 19, 41}, {41, 41, -1, -1},
};
static const int8_t k_parent_mecha[20] = { -1, 0, 1, 0, 0, 4, 5, 6, 6, 0, 9, 10, 0, 12, 13, 14, 14, 0, 17, 18 };
/* the owner's answer 17 (rounds plan 14.1bl; select 4.2.1): "he runs instead of flies ... he uses the Jog": Sonic's own rows, the jog (row 11) where Sonic runs with his arms behind him (12, 13) */
static const uint16_t k_mjog[2][2] = { {12, 11}, {13, 11} };
static sa1_skin_desc g_mecha;

static void mecha_init(void)
{
    g_mecha = g_skin;                           /* Metal's files, then his own (feat/sh_skins.lua's entry) */
    g_mecha.anim = k_mjog;
    g_mecha.num_anim = 2;
    g_mecha.anim_walk = NULL;
    g_mecha.num_anim_walk = 0;
    g_mecha.anim_fast = NULL;
    g_mecha.num_anim_fast = 0;
    g_mecha.fast_on = 0.0f;
    g_mecha.fast_off = 0.0f;
    g_mecha.model_first = 0x0CA824C0u;
    g_mecha.model_end = 0x0CA88AF8u;
    g_mecha.model_root = 0x0CA88AC4u;
    g_mecha.tex_keep = k_mkeep;
    g_mecha.num_tex_keep = 22;
    g_mecha.scale = 0.52f;                      /* LANDING.md D2: his feet on the ground at a stand */
    g_mecha.lift = 0.0f;                        /* Sonic's own rows stand on the game's own root (select 4.2.1) */
    g_mecha.ball_texture = 86;                  /* mrd_064s_msa */
    g_mecha.parts = k_mparts;
    g_mecha.num_parts = 20;
    g_mecha.glow_rgb[0] = 1.00f;
    g_mecha.glow_rgb[1] = 0.45f;
    g_mecha.glow_rgb[2] = 0.05f;
    g_mecha.glow_inflate = 0.35f;
    g_mecha.id = "mechasonic";
    g_mecha.trail_rgb[0] = 1.00f;
    g_mecha.trail_rgb[1] = 0.45f;
    g_mecha.trail_rgb[2] = 0.05f;
}

/* ---- 100-S6's helpers (the owner's answer 17): a speed above dash_speed, the lowest vertex of a render ------------------------------------------------- */
#define A_PLAYER_PWP 0x8C78C668u        /* playerpwp[8] (tests/super_test.c); playerwk.spd.x at +0x38 */

/* playerwk.spd.x set before a pair (tests/super_test.c pair_at_speed): faster than dash_speed the game's own row choice is 13, the sprint (row 13 is never reached on the flat floor) */
static void s6_speed(sa1_character_id id, float v)
{
    struct sa1g_world *w = sa1_character_world(g_ctx, id);
    uint32_t p = 0, u;
    if (w == NULL) {
        return;
    }
    sa1g_world_rd32(w, A_PLAYER_PWP, &p);
    if (p == 0u || ((p >> 24) & 31u) != SA1G_SEG_ARENA) {
        return;
    }
    memcpy(&u, &v, 4);
    sa1g_world_wr32(w, p + 0x38u, u);
}

/* the lowest vertex of a render in SA1 units (the host's z / 4, the floor at 0): the skin's parts (archives 64-79), or the body (r1-verify-s's r1vs_probe.c: no effect, shadow or object) */
static double s6_low(const sa1_render *r, int skin)
{
    uint32_t i, k;
    double lo = 1e30;
    if (r == NULL) {
        return lo;
    }
    for (i = 0; i < r->num_draws; i++) {
        const sa1_draw *d = &r->draws[i];
        int is_skin = d->archive >= (int32_t)SA1_ARCHIVE_SKIN_FIRST && d->archive <= (int32_t)SA1_ARCHIVE_SKIN_LAST;
        if (d->prim != SA1_PRIM_TRIANGLES || (skin ? !is_skin : (d->flags & (SA1_DRAW_EFFECT | SA1_DRAW_SHADOW | SA1_DRAW_OBJECT)) != 0u)) {
            continue;
        }
        for (k = 0; k < 3u * d->count; k++) {
            double z = (double)r->positions[3u * (3u * d->first) + 3u * k + 2u] / 4.0;
            if (z < lo) {
                lo = z;
            }
        }
    }
    return lo;
}

static void flags_on(sa1_character_id id, const uint32_t *fl, int n)
{
    struct sa1g_world *w = sa1_character_world(g_ctx, id);
    int k;
    for (k = 0; k < n; k++) {
        uint32_t a = EVF + fl[k], word = a & ~3u, v = 0, sh = 8u * (a & 3u);
        sa1g_world_rd32(w, word, &v);
        v = (v & ~(0xFFu << sh)) | (1u << sh);
        sa1g_world_wr32(w, word, v);
    }
}

static int name_ci(const char *a, const char *b)
{
    if (a == NULL || b == NULL) {
        return 0;
    }
    for (; *a && *b; a++, b++) {
        char x = (*a >= 'A' && *a <= 'Z') ? (char)(*a + 32) : *a, y = (*b >= 'A' && *b <= 'Z') ? (char)(*b + 32) : *b;
        if (x != y) {
            return 0;
        }
    }
    return *a == *b;
}

/* Sonic's SON_EFF archive and its lcharge01-04 */
static int32_t g_eff_arc = -1, g_lch[4] = { -1, -1, -1, -1 };
static void find_lcharge(void)
{
    uint32_t a, n = 0, k;
    const char *name = NULL;
    for (a = 0; a < 64u && sa1_texture_archive(g_ctx, SA1_CHAR_SONIC, a, &name, &n) == SA1_OK; a++) {
        if (!name_ci(name, "SON_EFF")) {
            continue;
        }
        g_eff_arc = (int32_t)a;
        for (k = 0; k < n; k++) {
            sa1_texture_info ti;
            int j;
            memset(&ti, 0, sizeof ti);
            ti.struct_size = sizeof ti;
            if (sa1_texture_get(g_ctx, SA1_CHAR_SONIC, a, k, &ti) != SA1_OK) {
                continue;
            }
            for (j = 0; j < 4; j++) {
                static const char *const nm[4] = { "lcharge01", "lcharge02", "lcharge03", "lcharge04" };
                if (name_ci(ti.name, nm[j])) {
                    g_lch[j] = (int32_t)k;
                }
            }
        }
    }
}

/* S4: a charged Light Speed Dash (the shoes, the ring if `ring`), action held: the draws of lcharge01-04 and their SA1_DRAW_SKIN_TRAIL */
typedef struct s4_out {
    uint32_t lch, lch_flag, other_flag, glow;
    uint8_t glow_col[3];
} s4_out;

static void s4_run(const sa1_skin_desc *skin, int ring, int32_t anim, s4_out *o)
{
    static const uint32_t fl[2] = { 0x8Du, 0x8Eu };
    sa1_character_id id = 0;
    int p;
    memset(o, 0, sizeof *o);
    g_cur = skin != NULL ? skin : &g_skin;
    if (create(skin != NULL ? SA1_CREATE_SKIN : 0u, &id) != SA1_OK) {
        g_cur = &g_skin;
        return;
    }
    g_cur = &g_skin;
    flags_on(id, fl, ring ? 2 : 1);
    if (anim >= 0) {
        sa1_character_set_option(g_ctx, id, SA1_COPT_SKIN_ANIM, anim);
    }
    for (p = 0; p < 200; p++) {
        const sa1_render *r = NULL;
        uint32_t i, k;
        uint8_t col[3][3];
        tick(id, 0.0f, 0.0f, (p >= 30 && p < 170) ? SA1_BUTTON_ACTION : 0u);
        sa1_character_render(g_ctx, id, 1.0f, &r);
        if (r == NULL) {
            continue;
        }
        for (i = 0; i < r->num_draws; i++) {
            const sa1_draw *d = &r->draws[i];
            int is = 0;
            for (k = 0; k < 4u; k++) {
                is |= g_eff_arc >= 0 && d->archive == g_eff_arc && g_lch[k] >= 0 && d->texture == g_lch[k];
            }
            if (is) {
                o->lch++;
                o->lch_flag += (d->flags & SA1_DRAW_SKIN_TRAIL) != 0u;
            } else {
                o->other_flag += (d->flags & SA1_DRAW_SKIN_TRAIL) != 0u;
            }
        }
        if (skin != NULL && glow_tally(r, col, skin->num_parts == 20u ? 819u : 1015u) > 0u && o->glow++ == 0u) {
            memcpy(o->glow_col, col[0], 3);
        }
    }
    sa1_character_destroy(g_ctx, id);
}

static double hue_of(double r, double g, double b)
{
    double mx = r > g ? (r > b ? r : b) : (g > b ? g : b), mn = r < g ? (r < b ? r : b) : (g < b ? g : b), h;
    if (!(mx > mn)) {
        return 0.0;
    }
    if (mx == r) {
        h = 60.0 * (g - b) / (mx - mn);
    } else if (mx == g) {
        h = 60.0 * (b - r) / (mx - mn) + 120.0;
    } else {
        h = 60.0 * (r - g) / (mx - mn) + 240.0;
    }
    return h < 0.0 ? h + 360.0 : h;
}

/* the variant 16 + slot of the four textures: the trail's hue (within 2 degrees where the texel has colour), value and saturation within 1, alpha kept */
static void s4_variant(uint32_t slot, const float rgb[3], const char *what)
{
    double want = hue_of(rgb[0], rgb[1], rgb[2]), worst_h = 0.0;
    uint32_t texels = 0, bad_vs = 0, bad_a = 0, k, have = 0;
    for (k = 0; k < 4u; k++) {
        sa1_texture_info a, v;
        uint32_t i;
        if (g_lch[k] < 0) {
            continue;
        }
        memset(&a, 0, sizeof a);
        memset(&v, 0, sizeof v);
        a.struct_size = v.struct_size = sizeof a;
        if (sa1_texture_get(g_ctx, SA1_CHAR_SONIC, (uint32_t)g_eff_arc, (uint32_t)g_lch[k], &a) != SA1_OK ||
            sa1_texture_variant_get(g_ctx, SA1_CHAR_SONIC, (uint32_t)g_eff_arc, (uint32_t)g_lch[k], SA1_TEXTURE_VARIANT_SKIN_FIRST + slot, &v) != SA1_OK ||
            v.width != a.width || v.height != a.height || a.rgba == NULL || v.rgba == NULL) {
            continue;
        }
        have++;
        for (i = 0; i < a.width * a.height; i++) {
            const uint8_t *s0 = a.rgba + 4u * i, *s1 = v.rgba + 4u * i;
            int mx0 = s0[0] > s0[1] ? (s0[0] > s0[2] ? s0[0] : s0[2]) : (s0[1] > s0[2] ? s0[1] : s0[2]);
            int mn0 = s0[0] < s0[1] ? (s0[0] < s0[2] ? s0[0] : s0[2]) : (s0[1] < s0[2] ? s0[1] : s0[2]);
            int mx1 = s1[0] > s1[1] ? (s1[0] > s1[2] ? s1[0] : s1[2]) : (s1[1] > s1[2] ? s1[1] : s1[2]);
            int mn1 = s1[0] < s1[1] ? (s1[0] < s1[2] ? s1[0] : s1[2]) : (s1[1] < s1[2] ? s1[1] : s1[2]);
            bad_vs += abs(mx0 - mx1) > 1 || abs(mn0 - mn1) > 1;
            bad_a += s0[3] != s1[3];
            if (mx1 - mn1 >= 64) {
                double h = hue_of(s1[0], s1[1], s1[2]), e = fabs(h - want);
                e = e > 180.0 ? 360.0 - e : e;
                worst_h = e > worst_h ? e : worst_h;
                texels++;
            }
        }
    }
    CHECK(have == 4u && texels > 0u && worst_h <= 2.0 && bad_vs == 0u && bad_a == 0u,
          "100-S4 %s: variant %u of lcharge01-04 (%u of 4): the trail's hue %.1f within %.2f degrees on %u coloured texels, value and saturation within 1/255 (%u off), alpha kept (%u off)",
          what, (unsigned)(SA1_TEXTURE_VARIANT_SKIN_FIRST + slot), (unsigned)have, want, worst_h, (unsigned)texels, (unsigned)bad_vs, (unsigned)bad_a);
}

static void test_100_s4(void)
{
    s4_out o;
    sa1_texture_info v;
    printf("100-S4 the Light Speed trails in the skin's hue\n");
    find_lcharge();
    CHECK(g_eff_arc >= 0 && g_lch[0] >= 0 && g_lch[1] >= 0 && g_lch[2] >= 0 && g_lch[3] >= 0, "100-S4 Sonic's SON_EFF is archive %d, lcharge01-04 are %d %d %d %d",
          (int)g_eff_arc, (int)g_lch[0], (int)g_lch[1], (int)g_lch[2], (int)g_lch[3]);
    s4_run(&g_skin, 0, -1, &o);
    CHECK(o.lch > 0u && o.lch_flag == o.lch && o.other_flag == 0u, "100-S4 Metal, plain charge: %u draws of lcharge01-04, %u with SA1_DRAW_SKIN_TRAIL; %u other draws with it (0)",
          (unsigned)o.lch, (unsigned)o.lch_flag, (unsigned)o.other_flag);
    s4_run(&g_skin, 1, -1, &o);
    CHECK(o.lch > 0u && o.lch_flag == o.lch && o.other_flag == 0u, "100-S4 Metal with the Crystal Ring: %u draws of lcharge01-04, %u with the flag; %u other draws with it (0)",
          (unsigned)o.lch, (unsigned)o.lch_flag, (unsigned)o.other_flag);
    s4_run(&g_skin, 0, (int32_t)(SA1_SKIN_ANIM_FLY | SA1_SKIN_ANIM_GLOW), &o);
    CHECK(o.lch > 0u && o.lch_flag == 0u && o.other_flag == 0u, "100-S4 Metal with SA1_SKIN_ANIM_TRAIL off: %u draws of lcharge01-04, none flagged (%u, %u)",
          (unsigned)o.lch, (unsigned)o.lch_flag, (unsigned)o.other_flag);
    s4_run(NULL, 1, -1, &o);
    CHECK(o.lch > 0u && o.lch_flag == 0u && o.other_flag == 0u, "100-S4 Sonic without a skin: %u draws of lcharge01-04, none flagged (%u, %u)",
          (unsigned)o.lch, (unsigned)o.lch_flag, (unsigned)o.other_flag);
    s4_variant(0u, g_skin.trail_rgb, "Metal (slot 0, purple)");
    memset(&v, 0, sizeof v);
    v.struct_size = sizeof v;
    CHECK(sa1_texture_variant_get(g_ctx, SA1_CHAR_SONIC, (uint32_t)g_eff_arc, 0u, SA1_TEXTURE_VARIANT_SKIN_FIRST, &v) == SA1_ERR_NOT_FOUND || g_lch[0] == 0 || g_lch[1] == 0 ||
          g_lch[2] == 0 || g_lch[3] == 0, "100-S4 another texture of SON_EFF has no trail variant (SA1_ERR_NOT_FOUND)");
    CHECK(sa1_texture_variant_get(g_ctx, SA1_CHAR_SONIC, (uint32_t)g_eff_arc, (uint32_t)g_lch[0], SA1_TEXTURE_VARIANT_SKIN_FIRST + 9u, &v) == SA1_ERR_NOT_FOUND,
          "100-S4 the variant of a slot no skin holds: SA1_ERR_NOT_FOUND");
}

/* S5: the skin's parts do not change with the display set of the upgrades (select 3.3) */
static uint32_t skin_hash(const sa1_render *r, uint32_t arch, uint32_t *tris)
{
    uint32_t i, h = 2166136261u, k;
    *tris = 0;
    if (r == NULL) {
        return 0;
    }
    for (i = 0; i < r->num_draws; i++) {
        const sa1_draw *d = &r->draws[i];
        const uint8_t *b;
        if (d->prim != SA1_PRIM_TRIANGLES || (uint32_t)d->archive != arch) {
            continue;
        }
        *tris += d->count;
        b = (const uint8_t *)(r->positions + 9u * d->first);
        for (k = 0; k < 36u * d->count; k++) {
            h = (h ^ b[k]) * 16777619u;
        }
    }
    return h;
}

static void test_100_s5(void)
{
    static const uint32_t fl[2] = { 0x8Du, 0x8Eu };
    sa1_character_id a = 0, b = 0, c = 0;
    int p, differ = 0, drew = 0;
    printf("100-S5 a skin's look under the display set\n");
    create(SA1_CREATE_SKIN, &a);
    create(SA1_CREATE_SKIN, &b);
    create(SA1_CREATE_SKIN, &c);
    flags_on(a, fl, 2);
    flags_on(b, fl, 2);
    CHECK(sa1_character_set_game_option(g_ctx, b, SA1_GOPT_UPGRADE_MODELS, (int32_t)SA1_UPGRADE_MODELS_ON) == SA1_OK &&
          sa1_character_set_game_option(g_ctx, c, SA1_GOPT_UPGRADE_MODELS, (int32_t)(SA1_UPGRADE_MODELS_ON | SA1_UPGRADE_MODELS_ALL)) == SA1_OK,
          "100-S5 the display set on two Metal worlds: none shown on one holding the shoes and the ring, all shown on one holding none");
    for (p = 0; p < 240; p++) {
        const sa1_render *ra = NULL, *rb = NULL, *rc = NULL;
        uint32_t ta, tb, tc, ha, hb, hc;
        float sy = (p >= 60 && p < 180) ? 1.0f : 0.0f;
        tick(a, 0.0f, sy, 0u);
        tick(b, 0.0f, sy, 0u);
        tick(c, 0.0f, sy, 0u);
        sa1_character_render(g_ctx, a, 1.0f, &ra);
        sa1_character_render(g_ctx, b, 1.0f, &rb);
        sa1_character_render(g_ctx, c, 1.0f, &rc);
        ha = skin_hash(ra, SA1_ARCHIVE_SKIN_FIRST, &ta);
        hb = skin_hash(rb, SA1_ARCHIVE_SKIN_FIRST, &tb);
        hc = skin_hash(rc, SA1_ARCHIVE_SKIN_FIRST, &tc);
        drew += ta > 0u;
        differ += ha != hb || ta != tb || ha != hc || ta != tc;
    }
    CHECK(drew > 200 && differ == 0, "100-S5 Metal's parts bit for bit the same with no display set, with none shown and with all shown: %d of 240 pairs differ (%d drew)", differ, drew);
    sa1_character_destroy(g_ctx, a);
    sa1_character_destroy(g_ctx, b);
    sa1_character_destroy(g_ctx, c);
}

/* S2: Mecha Sonic's parts, his archive, his textures, his glow */
static void test_100_s2(void)
{
    static const uint32_t fl[1] = { 0x8Du };
    sa1_character_id id = 0;
    sa1k_test_info *ti = (sa1k_test_info *)malloc(sizeof *ti);
    uint32_t arch = 0, num = 0, i;
    const char *name = NULL;
    const sa1_render *r = NULL;
    tally t;
    sa1_status st;
    int p, nan = 0;
    s4_out o;
    printf("100-S2 Mecha Sonic\n");
    g_cur = &g_mecha;
    st = create(SA1_CREATE_SKIN, &id);
    g_cur = &g_skin;
    CHECK(st == SA1_OK, "100-S2 a world wears Mecha Sonic beside Metal's: %s %s", sa1_status_string(st), st == SA1_OK ? "" : sa1_last_error(g_ctx));
    if (st != SA1_OK) {
        free(ti);
        return;
    }
    CHECK(sa1_character_skin_archive(g_ctx, id, &arch) == SA1_OK && arch == SA1_ARCHIVE_SKIN_FIRST + 1u, "100-S2 his parts draw from archive %u (65: the second skin loaded)", (unsigned)arch);
    CHECK(sa1_texture_archive(g_ctx, SA1_CHAR_SONIC, arch, &name, &num) == SA1_OK && num == 22u, "100-S2 archive %u: %s, %u textures (22: 7 and 76-96)", (unsigned)arch, name ? name : "?", (unsigned)num);
    g_tally_arch = arch;
    for (p = 0; p < 60; p++) {
        tick(id, 0.0f, 0.0f, 0u);
    }
    sa1_character_render(g_ctx, id, 1.0f, &r);
    tally_of(r, &t);
    info_of(id, ti);
    for (i = 0; r != NULL && i < 9u * r->num_triangles; i++) {
        nan += !sa1_finite(r->positions[i]);
    }
    printf("  info  after the landing: %u parts, %u triangles on archive %u, lowest vertex %.3f units\n", (unsigned)ti->num_parts, (unsigned)t.skin_tris, (unsigned)arch, t.lowest);
    CHECK(ti->num_parts == 20u && ti->drew && t.skin_tris == 819u && t.body_tris == 0u && nan == 0, "100-S2 20 parts, 819 triangles (%u), no Sonic body (%u), no NaN (%d)",
          (unsigned)t.skin_tris, (unsigned)t.body_tris, nan);
    {
        /* a run, then a stop: row 0, the stand (S6's method for Metal). Round 1.0.0-beta1 (the plant, the owner's answer 17): against plain Sonic's own lowest body vertex through the same pairs */
        sa1_state_ex x;
        sa1_character_id s = 0;
        double sl = 1e30;
        create(0, &s);
        for (p = 0; p < 60; p++) {
            tick(s, 0.0f, 0.0f, 0u);
        }
        for (p = 0; p < 60; p++) {
            tick(id, 0.0f, 0.5f, 0u);
            tick(s, 0.0f, 0.5f, 0u);
        }
        for (p = 0; p < 90; p++) {
            tick(id, 0.0f, 0.0f, 0u);
            tick(s, 0.0f, 0.0f, 0u);
        }
        sa1_character_render(g_ctx, id, 1.0f, &r);
        tally_of(r, &t);
        state_of(id, &x);
        if (s != 0) {
            const sa1_render *rs = NULL;
            sa1_character_render(g_ctx, s, 1.0f, &rs);
            sl = s6_low(rs, 0);
            sa1_character_destroy(g_ctx, s);
        }
        /* round 1.0.0-beta1a (lane b1a-mecha; the owner: "Mecha Sonic is floating"): on the floor, not at Sonic's own lowest point, which stands off it (+0.36 .. +0.42) */
        CHECK(x.action == 0 && fabs(t.lowest) <= 0.05 && t.lowest <= sl, "100-S2 at the scale 0.52 he stands on the floor in row %d: lowest vertex %+.3f (within 0.05), Sonic's own %+.3f (r1-verify-s: +0.36)",
              (int)x.action, t.lowest, sl);
    }
    sa1_character_destroy(g_ctx, id);
    g_tally_arch = SA1_ARCHIVE_SKIN_FIRST;
    s4_run(&g_mecha, 0, -1, &o);
    CHECK(o.glow > 0u && o.glow_col[0] > o.glow_col[2] && o.glow_col[0] >= o.glow_col[1] && o.glow_col[1] >= o.glow_col[2],
          "100-S2 his charged glow: %u pairs of shells, orange (%u, %u, %u)", (unsigned)o.glow, o.glow_col[0], o.glow_col[1], o.glow_col[2]);
    CHECK(o.lch > 0u && o.lch_flag == o.lch && o.other_flag == 0u, "100-S4 Mecha, plain charge: %u draws of lcharge01-04, %u with SA1_DRAW_SKIN_TRAIL; %u other draws with it (0)",
          (unsigned)o.lch, (unsigned)o.lch_flag, (unsigned)o.other_flag);
    s4_variant(1u, g_mecha.trail_rgb, "Mecha (slot 1, orange)");
    (void)fl;
    free(ti);
}

/* S3: the rows, the joints and the ball as Metal's S3, S5 and S7 check them, then the world identity of S1, on Mecha Sonic */
static void test_100_s3(void)
{
    printf("100-S3 Mecha Sonic's rows and world\n");
    g_cur = &g_mecha;
    g_tally_arch = SA1_ARCHIVE_SKIN_FIRST + 1u;
    g_parent = k_parent_mecha;
    g_nparts = 20u;
    s3_run(0, "Mecha plain");
    s3_run(1, "Mecha with the upgrades");
    test_s1();
    g_cur = &g_skin;
    g_tally_arch = SA1_ARCHIVE_SKIN_FIRST;
    g_parent = k_parent_metal;
    g_nparts = 25u;
}

/* S1: many skins: Metal and Mecha on two worlds and a follower, their archives, their own draws; 16 at most; all freed with the disc */
static void test_100_s1(void)
{
    sa1_character_id a = 0, b = 0, f = 0, plain = 0;
    sa1_follower_desc fd;
    uint32_t aa = 0, ab = 0, af = 0, k, wrong = 0, mine_a = 0, mine_b = 0, dummy = 0;
    sa1_texture_info t0, t1;
    sa1_status st;
    const char *err;
    int p;
    static char ids[16][16];
    static sa1_skin_desc more[16];
    printf("100-S1 many skins\n");
    create(SA1_CREATE_SKIN, &a);
    g_cur = &g_mecha;
    create(SA1_CREATE_SKIN, &b);
    g_cur = &g_skin;
    create(0, &plain);
    memset(&fd, 0, sizeof fd);
    fd.struct_size = sizeof fd;
    fd.kind = SA1_CHAR_SONIC;
    fd.leader = a;
    fd.flags = SA1_FOLLOW_SEED | SA1_FOLLOW_NO_SET | SA1_FOLLOW_SKIN;
    fd.seed = 0x5EED0100u;
    fd.skin = &g_mecha;
    st = sa1_follower_create(g_ctx, &fd, &f);
    CHECK(a != 0 && b != 0 && st == SA1_OK, "100-S1 Metal on a world, Mecha on a second world and on a follower of the first: %s", st == SA1_OK ? "every load answers" : sa1_last_error(g_ctx));
    sa1_character_skin_archive(g_ctx, a, &aa);
    sa1_character_skin_archive(g_ctx, b, &ab);
    sa1_character_skin_archive(g_ctx, f, &af);
    CHECK(aa == SA1_ARCHIVE_SKIN_FIRST && ab == SA1_ARCHIVE_SKIN_FIRST + 1u && af == ab, "100-S1 the archives: Metal %u, Mecha %u, the Mecha follower %u (64, 65, 65: one load each)",
          (unsigned)aa, (unsigned)ab, (unsigned)af);
    CHECK(sa1_character_skin_archive(g_ctx, plain, &dummy) == SA1_ERR_UNSUPPORTED && sa1_character_skin_archive(g_ctx, (sa1_character_id)0x7FFF00FF, &dummy) == SA1_ERR_NOT_FOUND,
          "100-S1 a plain Sonic: SA1_ERR_UNSUPPORTED; no such character: SA1_ERR_NOT_FOUND");
    for (p = 0; p < 30; p++) {
        const sa1_render *ra = NULL, *rb = NULL;
        uint32_t i;
        tick(a, 0.0f, 0.0f, 0u);
        tick(b, 0.0f, 0.0f, 0u);
        sa1_character_render(g_ctx, a, 1.0f, &ra);
        sa1_character_render(g_ctx, b, 1.0f, &rb);
        for (i = 0; ra != NULL && i < ra->num_draws; i++) {
            wrong += (uint32_t)ra->draws[i].archive == ab || (uint32_t)ra->draws[i].archive == SA1_ARCHIVE_SKIN;
            mine_a += (uint32_t)ra->draws[i].archive == aa;
        }
        for (i = 0; rb != NULL && i < rb->num_draws; i++) {
            wrong += (uint32_t)rb->draws[i].archive == aa || (uint32_t)rb->draws[i].archive == SA1_ARCHIVE_SKIN;
            mine_b += (uint32_t)rb->draws[i].archive == ab;
        }
    }
    CHECK(mine_a > 0u && mine_b > 0u && wrong == 0u, "100-S1 each world's draws use only its own skin's archive: %u and %u draws, %u on another's (0)", (unsigned)mine_a, (unsigned)mine_b, (unsigned)wrong);
    memset(&t0, 0, sizeof t0);
    memset(&t1, 0, sizeof t1);
    t0.struct_size = t1.struct_size = sizeof t0;
    CHECK(sa1_texture_get(g_ctx, SA1_CHAR_SONIC, aa, 0, &t0) == SA1_OK && sa1_texture_get(g_ctx, SA1_CHAR_SONIC, ab, 0, &t1) == SA1_OK &&
          (t0.width != t1.width || t0.height != t1.height || (t0.name && t1.name && strcmp(t0.name, t1.name) != 0)),
          "100-S1 the textures differ: archive %u texture 0 %s %ux%u, archive %u texture 0 %s %ux%u", (unsigned)aa, t0.name ? t0.name : "?", (unsigned)t0.width, (unsigned)t0.height,
          (unsigned)ab, t1.name ? t1.name : "?", (unsigned)t1.width, (unsigned)t1.height);
    CHECK(sa1_texture_archive(g_ctx, SA1_CHAR_SONIC, SA1_ARCHIVE_SKIN_FIRST + 2u, NULL, NULL) == SA1_ERR_NOT_LOADED && sa1_texture_archive(g_ctx, SA1_CHAR_SONIC, SA1_ARCHIVE_SKIN, NULL, &k) == SA1_OK,
          "100-S1 archive 66 holds nothing yet; 61 still answers the first skin");
    sa1_character_destroy(g_ctx, f);
    sa1_character_destroy(g_ctx, a);
    sa1_character_destroy(g_ctx, b);
    sa1_character_destroy(g_ctx, plain);
    /* fourteen more by their id (Metal's files: one load each), then the seventeenth */
    for (k = 2; k < 17u; k++) {
        struct sa1k_state *q = NULL;
        more[k - 1u] = g_skin;
        snprintf(ids[k - 1u], sizeof ids[0], "test-%u", (unsigned)k);
        more[k - 1u].id = ids[k - 1u];
        st = sa1k_load(g_ctx, &more[k - 1u], &q);
        if (k < 16u && st != SA1_OK) {
            CHECK(0, "100-S1 skin %u loads: %s", (unsigned)(k + 1u), sa1_last_error(g_ctx));
            break;
        }
        if (k == 16u) {
            err = sa1_last_error(g_ctx);
            CHECK(st == SA1_ERR_LIMIT && err != NULL && strstr(err, "test-16") != NULL, "100-S1 the seventeenth skin: %s, \"%s\"", sa1_status_string(st), err ? err : "");
        }
    }
    CHECK(sa1_texture_archive(g_ctx, SA1_CHAR_SONIC, SA1_ARCHIVE_SKIN_LAST, NULL, &k) == SA1_OK && sa1_texture_archive(g_ctx, SA1_CHAR_SONIC, SA1_ARCHIVE_SKIN_LAST + 1u, NULL, NULL) != SA1_OK,
          "100-S1 sixteen skins: archive 79 answers, 80 does not");
    g_cur = &g_mecha;
    st = create(SA1_CREATE_SKIN, &b);
    g_cur = &g_skin;
    CHECK(st == SA1_OK, "100-S1 a loaded skin is worn again with sixteen loaded: %s", st == SA1_OK ? "ok" : sa1_last_error(g_ctx));
    sa1_character_destroy(g_ctx, b);
    sa1_unload_disc(g_ctx);
    CHECK(sa1_texture_archive(g_ctx, SA1_CHAR_SONIC, SA1_ARCHIVE_SKIN_FIRST, NULL, NULL) != SA1_OK && sa1_texture_archive(g_ctx, SA1_CHAR_SONIC, SA1_ARCHIVE_SKIN_LAST, NULL, NULL) != SA1_OK,
          "100-S1 freeing the disc frees all sixteen");
}

/* ---- 100-S6 (gsa1_100_select.md 4.2.1, 7.1 S6; the owner's answer 17, rounds plan 14.1bl): Mecha Sonic jogs where Sonic runs with his arms behind him, and stands where Sonic stands ------- */
static void mul3_tn(const double a[9], const double b[9], double o[9])     /* transpose(a) * b */
{
    double t[9];
    int r, c;
    for (r = 0; r < 3; r++) {
        for (c = 0; c < 3; c++) {
            t[3 * r + c] = a[r] * b[c] + a[3 + r] * b[3 + c] + a[6 + r] * b[6 + c];
        }
    }
    memcpy(o, t, sizeof t);
}

static uint32_t s6_frames(const sa1_charset *cs, int row)
{
    if (row < 0 || (uint32_t)row >= cs->num_actions || cs->actions[row].motion < 0) {
        return 0u;
    }
    return SA1_VEC(cs->motions, sa1_motion)[cs->actions[row].motion].num_frames;
}

/* the stick of pair p (and a speed above dash_speed): a stand, a ramp up, the run, the sprint, the run again, a stop */
static float s6_stick(int p, int *sprint)
{
    *sprint = p >= 240 && p < 330;
    if (p < 30) {
        return 0.0f;
    }
    if (p < 150) {
        return (float)(p - 30) / 120.0f;
    }
    return p < 450 ? 1.0f : 0.0f;
}

/* Sonic's own lowest point along up under root * the model-space pose in inst (his plain models, no weld; the drawn nodes of his body): other code than the library's */
static double s6_pose_low(const sa1_charset *cs, const sa1_instance *inst, const double root[12], const double up[3])
{
    const sa1_object *o = &SA1_VEC(cs->objects, sa1_object)[cs->main_object];
    double low = 1e30;
    uint32_t n, v;
    for (n = 0; n < o->num_nodes; n++) {
        const sa1_model *md;
        const float *m = inst->node_mtx + 12u * n;
        double w[12];
        int k;
        if (o->nodes[n].model < 0 || !o->nodes[n].drawn) {
            continue;
        }
        md = &SA1_VEC(cs->models, sa1_model)[o->nodes[n].model];
        for (k = 0; k < 3; k++) {
            int c;
            for (c = 0; c < 4; c++) {
                w[4 * k + c] = root[4 * k] * m[c] + root[4 * k + 1] * m[4 + c] + root[4 * k + 2] * m[8 + c] + (c == 3 ? root[4 * k + 3] : 0.0);
            }
        }
        for (v = 0; md->points != NULL && v < md->num_points; v++) {
            const float *pt = md->points + 3u * v;
            double hx = w[0] * pt[0] + w[1] * pt[1] + w[2] * pt[2] + w[3];
            double hy = w[4] * pt[0] + w[5] * pt[1] + w[6] * pt[2] + w[7];
            double hz = w[8] * pt[0] + w[9] * pt[1] + w[10] * pt[2] + w[11];
            double h = up[0] * hx + up[1] * hy + up[2] * hz;
            if (h < low) {
                low = h;
            }
        }
    }
    return low;
}

static void test_100_s6(void)
{
    enum { PAIRS = 600 };
    const sa1_charset *cs = &g_ctx->chars[SA1_CHAR_SONIC];
    sa1k_test_info *ti = (sa1k_test_info *)malloc(sizeof *ti), *tp = (sa1k_test_info *)malloc(sizeof *tp);
    sa1k_test_info *tm = (sa1k_test_info *)malloc(sizeof *tm), *tq = (sa1k_test_info *)malloc(sizeof *tq);
    sa1_instance *inst = (sa1_instance *)calloc(1, sizeof *inst);
    static double ag[SA1K_PARTS_MAX][9], off[SA1K_PARTS_MAX][3];
    sa1_character_id m = 0, s = 0, t = 0;
    sa1_state_ex x, xp, xs;
    double k12, k13, worst_ang = 0.0, worst_perp = 0.0, worst_shape = 0.0, worst_plant = 0.0, sum_d[2] = { 0.0, 0.0 }, sum_w[2] = { 0.0, 0.0 };
    double stand_worst = 0.0, sink_m = -1e30, sink_t = -1e30, live_worst = -1e30, live_under = -1e30, max_step = 0.0, top = 4.2853 / (double)s6_frames(cs, 11);
    int top_n = 0, last_full = -1000;
    int live_n = 0, have_live = 0, p, row, have_prev = 0, prev_jog = 0, jog[2] = { 0, 0 }, rate_n[2] = { 0, 0 };
    int super_tree = 0, flown_other = 0, live_bad = 0, mismatch = 0, metal_own = 0, metal_rate_bad = 0, metal_flown = 0, stand_n = 0;
    uint32_t mhash = 2166136261u, mtris = 0;
    int own = 1;
    printf("100-S6 Mecha Sonic's jog (the owner: \"he uses the Jog\") and his feet\n");
    for (row = 9; row <= 13; row++) {
        const sa1_action *a = &cs->actions[row];
        printf("  info  row %d: object %d (Sonic's body: %d), mtnmode %d, racio %.3f, %u frames\n", row, (int)a->object, (int)cs->main_object, (int)a->mode, (double)a->speed, (unsigned)s6_frames(cs, row));
        own = own && (row < 11 || a->object == cs->main_object);
    }
    k12 = ((double)cs->actions[11].speed / (double)cs->actions[12].speed) * ((double)s6_frames(cs, 12) / (double)s6_frames(cs, 11));
    k13 = ((double)cs->actions[11].speed / (double)cs->actions[13].speed) * ((double)s6_frames(cs, 13) / (double)s6_frames(cs, 11));
    CHECK(own && cs->actions[11].mode == 10 && cs->actions[12].mode == 10 && cs->actions[13].mode == 9 && fabs(k12 - 1.6) < 1e-3 && fabs(k13 - 1.65) < 1e-3,
          "100-S6 rows 11-13 are Sonic's own tree and speed loops (mtnmode 10, 10, 9): the jog's k %.4f from row 12, %.4f from row 13 (select 4.2.1: 1.6, 1.65)", k12, k13);
    inst->kind = 0;
    if (sa1_instance_alloc(g_ctx, cs, inst) != SA1_OK) {
        CHECK(0, "100-S6 an instance");
        free(ti); free(tp); free(tm); free(tq); free(inst);
        return;
    }
    g_cur = &g_mecha;
    create(SA1_CREATE_SKIN, &m);
    g_cur = &g_skin;
    create(SA1_CREATE_SKIN, &t);
    create(0, &s);
    if (m == 0 || s == 0 || t == 0) {
        CHECK(0, "100-S6 three worlds: Mecha, plain Sonic, Metal: %s", sa1_last_error(g_ctx));
        sa1_instance_free(g_ctx, inst);
        free(ti); free(tp); free(tm); free(tq); free(inst);
        return;
    }
    memset(&xp, 0, sizeof xp);
    for (p = 0; p < PAIRS; p++) {
        int sprint, is_jog, ri = 0;
        float sy = s6_stick(p, &sprint);
        const sa1_render *rm = NULL, *rs = NULL, *rt = NULL;
        double lm, ls, lt;
        uint32_t tris = 0;
        if (sprint) {
            s6_speed(m, 5.6f);
            s6_speed(s, 5.6f);
            s6_speed(t, 5.6f);
        }
        tick(m, 0.0f, sy, 0u);
        tick(s, 0.0f, sy, 0u);
        tick(t, 0.0f, sy, 0u);
        state_of(m, &x);
        state_of(s, &xs);
        mismatch += x.action != xs.action || x.frame != xs.frame;
        info_of(m, ti);
        info_of(t, tm);
        sa1_character_render(g_ctx, m, 1.0f, &rm);
        lm = s6_low(rm, 1);
        sa1_character_render(g_ctx, s, 1.0f, &rs);
        ls = s6_low(rs, 0);
        sa1_character_render(g_ctx, t, 1.0f, &rt);
        lt = s6_low(rt, 1);
        mhash = (mhash ^ skin_hash(rt, SA1_ARCHIVE_SKIN_FIRST, &tris)) * 16777619u;
        mtris += tris;
        if (lm < 1e29 && ls < 1e29 && ls - lm > sink_m) {
            sink_m = ls - lm;
        }
        if (ti->w <= 0.0f && ti->live_mix >= 1.0f && lm < 1e29 && ls < 1e29 && x.action >= 0 && x.action <= 13 && (x.flags & SA1_STATE_ON_GROUND) && rm != NULL && rm->position[2] / 4.0f < 0.03f) {
            /* round 1.0.0-beta1a: never above the floor, never deeper than Sonic's own toes where they dip under it */
            double sd = ls < 0.0 ? ls : 0.0;
            live_n++;
            live_worst = lm > live_worst ? lm : live_worst;
            live_under = sd - lm > live_under ? sd - lm : live_under;
        }
        if (getenv("S6_DEBUG") != NULL) {
            printf("    s6 %3d act %3d req %3d spd %.2f drive %3d cur %3d prev %3d w %.3f x %.3f live %.3f | Mecha %+.3f Sonic %+.3f Metal %+.3f\n", p, (int)x.action, (int)x.req_action, (double)x.player_speed[0],
                   (int)ti->drive_row, (int)ti->cur, (int)ti->prev, (double)ti->w, (double)ti->x, (double)ti->live_mix, lm, ls, lt);
        }
        if (lt < 1e29 && ls < 1e29 && ls - lt > sink_t) {
            sink_t = ls - lt;
        }
        if (p >= 480 && x.action == 0 && lm < 1e29 && ls < 1e29) {
            stand_n++;
            stand_worst = fabs(lm) > stand_worst ? fabs(lm) : stand_worst;      /* round 1.0.0-beta1a: against the floor */
        }
        /* the drawn rows: only Sonic's own tree, only the jog */
        if (ti->cur >= 0) {
            super_tree += cs->actions[ti->cur].object != cs->main_object;
            flown_other += ti->cur != 11;
        }
        if (tm->cur >= 0) {
            metal_own += cs->actions[tm->cur].object == cs->main_object;
            if (tm->w >= 1.0f && have_prev && tq->cur == tm->cur && tq->w >= 1.0f) {
                double dd = tm->phase - tq->phase;
                if (dd < 0.0) {
                    dd += 1.0;
                }
                metal_flown++;
                metal_rate_bad += fabs(dd - tm->rate) > 1e-9;
            }
        }
        /* a live pair at a stand: each part's A(i) G and offset, read back from the library's own output (r = turn * AG; offset = R(parent)^T (p - p(parent))) */
        if (!have_live && ti->drew && ti->w <= 0.0f && ti->live_mix >= 1.0f && x.action == 0) {
            int i, ok = 1;
            for (i = 0; i < 20 && ok; i++) {
                int n = k_mparts[i].mesh_node;
                double R[9], T[9];
                if (!ti->have[n]) {
                    ok = 0;
                    break;
                }
                rot_of(ti->mtx[n], R);
                mul3_t(R, ti->rest[n], T);
                mul3_tn(T, ti->r[i], ag[i]);
                if (i > 0) {
                    const double *pp = ti->p[k_parent_mecha[i]], *pc = ti->p[i], *pr = ti->r[k_parent_mecha[i]];
                    double d[3];
                    d[0] = pc[0] - pp[0];
                    d[1] = pc[1] - pp[1];
                    d[2] = pc[2] - pp[2];
                    off[i][0] = pr[0] * d[0] + pr[3] * d[1] + pr[6] * d[2];
                    off[i][1] = pr[1] * d[0] + pr[4] * d[1] + pr[7] * d[2];
                    off[i][2] = pr[2] * d[0] + pr[5] * d[1] + pr[8] * d[2];
                }
            }
            have_live = ok;
        }
        is_jog = ti->drew && (ti->drive_row == 12 || ti->drive_row == 13) && ti->cur == 11 && ti->w >= 1.0f && ti->prev < 0 && ti->live_mix <= 0.0f;
        if (is_jog) {
            ri = ti->drive_row - 12;
            jog[ri]++;
        }
        if (is_jog && have_live) {
            /* row 11 at the drawn frame by the library's evaluator, posed as the live path poses a drawn row (Sonic's mesh nodes), under the jog's root */
            const double *fr = ti->fly_root;
            double f = ti->phase * (double)s6_frames(cs, 11), Rr[9], up[3], e[SA1K_PARTS_MAX][3], er[SA1K_PARTS_MAX][9], d[3], al, perp, low;
            const float *nm;
            int i, k;
            sa1_pose_evaluate(g_ctx, cs, inst, cs->actions[11].object, cs->actions[11].motion, (float)f, SA1_POSE_MODEL_SPACE | SA1_POSE_NO_WELD);
            Rr[0] = fr[0]; Rr[1] = fr[1]; Rr[2] = fr[2];
            Rr[3] = fr[4]; Rr[4] = fr[5]; Rr[5] = fr[6];
            Rr[6] = fr[8]; Rr[7] = fr[9]; Rr[8] = fr[10];
            up[0] = fr[1];
            up[1] = fr[5];
            up[2] = fr[9];
            for (i = 0; i < 20; i++) {
                int nn = k_mparts[i].mesh_node;
                const float *q = inst->node_mtx + 12u * (uint32_t)nn;
                double nmx[12], Rn[9], A[9], B[9], a;
                for (k = 0; k < 12; k++) {
                    nmx[k] = (double)q[k];
                }
                rot_of(nmx, Rn);
                mul3(Rr, Rn, A);
                mul3_t(A, ti->rest[nn], B);
                mul3(B, ag[i], er[i]);
                a = m3_angle_between(er[i], ti->r[i]);
                worst_ang = a > worst_ang ? a : worst_ang;
            }
            nm = inst->node_mtx + 12u * (uint32_t)k_mparts[0].mesh_node;
            for (k = 0; k < 3; k++) {
                e[0][k] = fr[4 * k] * nm[3] + fr[4 * k + 1] * nm[7] + fr[4 * k + 2] * nm[11] + fr[4 * k + 3];
            }
            for (i = 1; i < 20; i++) {
                const double *pr = er[k_parent_mecha[i]], *pe = e[k_parent_mecha[i]];
                e[i][0] = pe[0] + pr[0] * off[i][0] + pr[1] * off[i][1] + pr[2] * off[i][2];
                e[i][1] = pe[1] + pr[3] * off[i][0] + pr[4] * off[i][1] + pr[5] * off[i][2];
                e[i][2] = pe[2] + pr[6] * off[i][0] + pr[7] * off[i][1] + pr[8] * off[i][2];
            }
            for (k = 0; k < 3; k++) {
                d[k] = ti->p[0][k] - e[0][k];
            }
            al = d[0] * up[0] + d[1] * up[1] + d[2] * up[2];
            perp = sqrt((d[0] - al * up[0]) * (d[0] - al * up[0]) + (d[1] - al * up[1]) * (d[1] - al * up[1]) + (d[2] - al * up[2]) * (d[2] - al * up[2]));
            worst_perp = perp > worst_perp ? perp : worst_perp;
            for (i = 1; i < 20; i++) {
                double dx = (ti->p[i][0] - ti->p[0][0]) - (e[i][0] - e[0][0]);
                double dy = (ti->p[i][1] - ti->p[0][1]) - (e[i][1] - e[0][1]);
                double dz = (ti->p[i][2] - ti->p[0][2]) - (e[i][2] - e[0][2]);
                double dl = sqrt(dx * dx + dy * dy + dz * dz);
                worst_shape = dl > worst_shape ? dl : worst_shape;
            }
            /* the plant: his drawn lowest point (the render) is Sonic's own lowest in that pose, and never above the floor through the drawn root (round 1.0.0-beta1a, D9 on his feet; the floor is flat:
               up is the SA1 y the render's z / 4 is) */
            low = s6_pose_low(cs, inst, fr, up);
            if (ti->floor_h < low) {
                low = ti->floor_h;
            }
            if (lm < 1e29 && fabs(lm - low) > worst_plant) {
                worst_plant = fabs(lm - low);
            }
            /* the jog's step against the world row's, from the world's own frame (sa1_state_ex.frame) */
            if (have_prev && prev_jog && tp->drive_row == ti->drive_row && x.action == x.req_action && xp.action == x.action) {
                double dw = ((double)x.frame - (double)xp.frame) / (double)s6_frames(cs, ti->drive_row), dd = ti->phase - tp->phase;
                if (dw < 0.0) {
                    dw += 1.0;
                }
                if (dd < 0.0) {
                    dd += 1.0;
                }
                if (dw > 0.0 && dw < 0.5) {
                    sum_w[ri] += dw;
                    sum_d[ri] += dd;
                    rate_n[ri]++;
                }
                /* round 1.0.0-beta1a (D10): the drawn step never above the top (Sonic's own fastest jog), at the top through the sprint */
                if (ti->step > max_step) {
                    max_step = ti->step;
                }
                top_n += ri == 1 && fabs(ti->step - top) < 1e-9;
            }
        }
        if (ti->w >= 1.0f) {
            last_full = p;
        }
        if (x.action >= 0 && x.action <= 10 && p - last_full > 4 && ti->w > 0.0f) {
            live_bad++;     /* round 1.0.0-beta1a: back in row 11 he may jog on his own until his phase meets the world's; a fade out takes 4 pairs */
        }
        memcpy(tp, ti, sizeof *tp);
        memcpy(tq, tm, sizeof *tq);
        xp = x;
        have_prev = 1;
        prev_jog = is_jog;
    }
    CHECK(mismatch == 0, "100-S6 Mecha's world is plain Sonic's: the same action and frame in all %d pairs (%d differ)", PAIRS, mismatch);
    CHECK(jog[0] > 50 && jog[1] > 30 && super_tree == 0 && flown_other == 0,
          "100-S6 in rows 12 and 13 he jogs: %d and %d pairs drawn from row 11; no part from the Super tree (%d pairs), no other drawn row (%d)", jog[0], jog[1], super_tree, flown_other);
    CHECK(have_live && worst_ang <= 0.5 && worst_perp <= 1e-3 && worst_shape <= 1e-3,
          "100-S6 rows 12 and 13 drawn as the live row-11 pose at the drawn frame (Sonic's mesh nodes): every part's rotation within %.4f degrees (0.5), the torso's place %.2e units across the up axis, each part's place against it %.2e (1e-3)",
          worst_ang, worst_perp, worst_shape);
    CHECK(worst_plant <= 1e-3, "100-S6 planted in the jog: his lowest drawn point is Sonic's own lowest point in that pose or the floor, the lower, within %.2e units (1e-3)", worst_plant);
    CHECK(rate_n[0] > 20 && rate_n[1] > 20 && max_step <= top + 1e-9 && top_n >= rate_n[1] - 3,
          "1a-M3 the jog's step never above Sonic's own fastest jog (%.5f a pair): at most %.5f; through the sprint (row 13) at it in %d of %d pairs; against the world row's %.4f x from row 12, %.4f x from row 13 (beta 1: 1.6, 1.65)",
          top, max_step, top_n, rate_n[1], rate_n[0] ? sum_d[0] / sum_w[0] : 0.0, rate_n[1] ? sum_d[1] / sum_w[1] : 0.0);
    CHECK(live_bad == 0, "100-S6 rows 0-10 more than 4 pairs after he last jogged on his own: Sonic's own pose, no flown pair (%d)", live_bad);
    CHECK(stand_n > 20 && stand_worst <= 0.05, "100-S6 at a stand after the run: his lowest point within %.3f units of the floor (%d pairs; 0.05)", stand_worst, stand_n);
    printf("  info  the whole run: his lowest point at most %.3f units under plain Sonic's (whose run, rows 12-13, is not the jog he shows there); Metal's at most %.3f\n", sink_m, sink_t);
    CHECK(live_n > 200 && live_worst <= 0.05 && live_under <= 1e-3, "100-S6 every pair he shows Sonic's own pose (rows 0-13): his lowest point at most %+.3f over the floor (0.05), at most %+.3f under Sonic's own dip (%d pairs)",
          live_worst, live_under, live_n);
    CHECK(mtris == 609000u && mhash == 0x0AC269FBu, "1a-M6 Metal through the same pairs: %u skin triangles, his draws' hash %08X (1.0.0-beta1: 609000, 0AC269FB, MEASURED before this round)", (unsigned)mtris, (unsigned)mhash);
    CHECK(metal_own == 0 && metal_flown > 50 && metal_rate_bad == 0,
          "100-S6 Metal Sonic: none of his drawn rows is in Sonic's own tree (%d pairs), his phase steps exactly at the world row's rate (k 1) in %d flown pairs (%d off)", metal_own, metal_flown, metal_rate_bad);
    sa1_character_destroy(g_ctx, m);
    sa1_character_destroy(g_ctx, s);
    sa1_character_destroy(g_ctx, t);
    sa1_instance_free(g_ctx, inst);
    free(ti);
    free(tp);
    free(tm);
    free(tq);
    free(inst);
}

/* 100-S6's feet, as r1-verify-s measured them (r1vs_probe.c test F: the phases, the lowest vertex every pair in lockstep with plain Sonic on the flat floor), Metal Sonic the same way */
static void test_100_s6_feet(int ups)
{
    static const struct { const char *name; int n; float sy; } ph[4] = { { "standing after a spawn", 90, 0.0f }, { "walking (stick 0.3)", 90, 0.3f }, { "running (stick 1)", 220, 1.0f }, { "stopping", 90, 0.0f } };
    sa1_character_id m = 0, s = 0, t = 0;
    int i, p, stand_n = 0, deeper = 0, live_n = 0;
    double stand_worst = 0.0, live_worst = -1e30, live_above = -1e30;
    sa1k_test_info *ti = (sa1k_test_info *)malloc(sizeof *ti);
    printf("100-S6 his feet against Sonic's and Metal's, by phase (r1-verify-s's test F)%s\n", ups ? ", the three holding the Light Speed Shoes and the Crystal Ring" : "");
    g_cur = &g_mecha;
    create(SA1_CREATE_SKIN, &m);
    g_cur = &g_skin;
    create(SA1_CREATE_SKIN, &t);
    create(0, &s);
    if (m == 0 || s == 0 || t == 0) {
        CHECK(0, "100-S6 feet: three worlds");
        free(ti);
        return;
    }
    if (ups) {
        upgrades_on(m);
        upgrades_on(s);
        upgrades_on(t);
    }
    for (i = 0; i < 4; i++) {
        double mlo = 1e30, slo_at = 0.0, slo = 1e30, tlo = 1e30, sm = -1e30, st = -1e30;
        int mact = -1, mpair = -1;
        for (p = 0; p < ph[i].n; p++) {
            const sa1_render *rm = NULL, *rs = NULL, *rt = NULL;
            sa1_state_ex x;
            double a, b, c;
            tick(m, 0.0f, ph[i].sy, 0u);
            tick(s, 0.0f, ph[i].sy, 0u);
            tick(t, 0.0f, ph[i].sy, 0u);
            sa1_character_render(g_ctx, m, 1.0f, &rm);
            a = s6_low(rm, 1);
            sa1_character_render(g_ctx, s, 1.0f, &rs);
            b = s6_low(rs, 0);
            sa1_character_render(g_ctx, t, 1.0f, &rt);
            c = s6_low(rt, 1);
            state_of(m, &x);
            info_of(m, ti);
            /* Sonic's own rows: round 1.0.0-beta1a, against the floor: how far over it, and how far under Sonic's own lowest point where that dips under it (with the upgrades the plant reads the plain
               shoes' points: the worn ones reach a little lower) */
            if (ti->w <= 0.0f && ti->live_mix >= 1.0f && a < 1e29 && b < 1e29 && (x.flags & SA1_STATE_ON_GROUND) && rm != NULL && rm->position[2] / 4.0f < 0.03f && x.action <= 13) {
                double sd = b < 0.0 ? b : 0.0;
                live_n++;
                live_worst = sd - a > live_worst ? sd - a : live_worst;
                live_above = a > live_above ? a : live_above;
            }
            if (a < 1e29 && a < mlo) {
                mlo = a;
                mact = (int)x.action;
                mpair = p;
                slo_at = b;
            }
            slo = b < slo ? b : slo;
            tlo = c < tlo ? c : tlo;
            if (a < 1e29 && b < 1e29) {
                sm = b - a > sm ? b - a : sm;
            }
            if (c < 1e29 && b < 1e29) {
                st = b - c > st ? b - c : st;
            }
            /* a stand: rows 0-2 on the ground after the landing (spawned 10 units up), and the end of the stop */
            if (((i == 0 && p >= 45) || (i == 3 && p >= 50)) && x.action >= 0 && x.action <= 2 && a < 1e29 && b < 1e29) {
                stand_n++;
                stand_worst = fabs(a) > stand_worst ? fabs(a) : stand_worst;      /* round 1.0.0-beta1a: on the floor */
            }
        }
        printf("  info  %-23s Mecha lowest %+.3f (pair %d, action %d; Sonic there %+.3f); Sonic's lowest %+.3f; Metal's lowest %+.3f; under Sonic by up to: Mecha %.3f, Metal %.3f\n", ph[i].name, mlo, mpair,
               mact, slo_at, slo, tlo, sm, st);
        /* the goal (r1-delta's task (7)): no deeper than Metal Sonic's own sink; where Sonic himself dips deeper than Metal did (the stop's links) the plant keeps him on Sonic's own point */
        if (i > 0 && mlo < tlo - 1e-3 && mlo < slo - 1e-3) {
            deeper++;
        }
        if (i > 0) {
            printf("  info  %-23s deepest: Mecha %+.3f, Metal %+.3f, Sonic %+.3f: %s\n", ph[i].name, mlo, tlo, slo, mlo >= tlo - 1e-3 ? "no deeper than Metal" : "deeper than Metal, as deep as Sonic himself");
        }
    }
    CHECK(stand_n > 40 && stand_worst <= 0.05, "100-S6%s standing after a spawn and after a stop: his lowest point within %.3f units of the floor (%d pairs; 0.05)", ups ? " (upgrades)" : "", stand_worst, stand_n);
    CHECK(live_n > 200 && live_above <= 0.05 && live_worst <= (ups ? 0.15 : 1e-3), "100-S6%s walking, running and stopping in Sonic's own rows: his lowest point at most %+.3f over the floor (%d pairs; 0.05), at most %+.3f under Sonic's own dip (%s)",
          ups ? " (upgrades)" : "", live_above, live_n, live_worst, ups ? "0.15: the worn shoes reach lower than the plain ones the plant reads" : "0.001");
    CHECK(deeper == 0, "100-S6%s walking, running and stopping: his deepest point no deeper than Metal Sonic's, or than Sonic's own (%d phases deeper)", ups ? " (upgrades)" : "", deeper);
    free(ti);
    sa1_character_destroy(g_ctx, m);
    sa1_character_destroy(g_ctx, s);
    sa1_character_destroy(g_ctx, t);
}

/* ---- round 1.0.0-beta1a (lane b1a-mecha; the owner on beta 1: "Mecha Sonic is floating ... his run animation has stuttering and it animates far too fast") ------------------------------
 * Frame by frame as Garry's Mod draws him (G-SA1's gsa1.Render(id, -1) at 60 fps: after each pair alpha 0, the pair before, then alpha 0.5), on the flat floor, Mecha and plain Sonic in lockstep
 * (sadc build/research/b1a/b1a-mecha: probe2.c and its outputs, the measurements these stand on):
 *   1a-M0 Sonic's own jog at its fastest: the last steady row-11 pair before the game switches to row 12 is sa1_skin.c's SK_OWN_TOP_FRAMES (4.2853 frames a pair)
 *   1a-M1 on the ground (both pairs shown on his feet, the drawn root on the floor, not the ball): his lowest point never more than 0.05 over the floor; in each drawn row no deeper than plain
 *         Sonic's own deepest point there (his toes' dips are Sonic's own: 1.0.0-beta1's numbers)
 *   1a-M2 the drawn jog per frame, read off the render: each skin draw's centroid against the first one's, fitted to the poses of a run at a tenth of the top pace (dense phases): no step
 *         backward but the loop's wrap, none over 1.5 times the median of the 15 frames around it, and between two pairs a pose of the jog (the fit's error)
 *   1a-M4 his lowest point between two frames that both stand on the floor: no jump over 0.05; in the whole run no jump over plain Sonic's own (of min(lowest, floor)) + 0.05
 *   1a-M5 the options: SA1_SKIN_ANIM_NO_PLANT (no step along up, no 60 Hz frames), SA1_SKIN_ANIM_PACE_FREE (1.0.0-beta1's 1.6 / 1.65 x the world row's rate), SA1_SKIN_ANIM_PACE(50) (half the top)
 *   1a-M7 sa1_character.c's render_build calls sa1k_frame: frames between two pairs posed again and planted (without the call they are render_interp's own chords, 1.0.0-beta1's: M1, M2, M4 FAIL)
 *   1a-M8 the pace through the switch to rows 12 and 13: while he does not slow down the drawn step never goes down (v-mecha MEASURED a 15 % dip for a pair just after the switch)
 *   1a-M9 a stop: through the fade out of the jog its phase steps on every pair (v-mecha MEASURED it frozen for the fade's 4 pairs)
 *   1a-M10 slopes of 12 degrees (test_1a_slope), up, down and across them, at 60 Hz the module's way (tick the frame's dt, render alpha -1): his lowest point to the slope (along its normal)
 *         never more than 0.05 over it on the ground in rows 0-13, at pairs and between them, and in no row deeper under it than plain Sonic's own deepest point, or the slope, by more than 0.05 */
#define M1A_REF_MAX  1400
#define M1A_VEC_MAX  (3 * 96)
#define M1A_SEQ_MAX  1400
typedef struct m1a_ref {
    double ph;
    int n;
    float v[M1A_VEC_MAX];
} m1a_ref;
typedef struct m1a_frame {
    int p, sub;
    double ph, err;
} m1a_frame;

/* the skin draws' centroids against the first one's, turned by -yaw about the host's up (the Source preset: z), SA1 units; the number of floats, -1: too many */
static int m1a_vec(const sa1_render *r, float *v)
{
    uint32_t i, k;
    int n = 0, have = 0;
    double t0[3] = { 0.0, 0.0, 0.0 }, cy, sy;
    if (r == NULL) {
        return -1;
    }
    cy = cos(-(double)r->yaw);
    sy = sin(-(double)r->yaw);
    for (i = 0; i < r->num_draws; i++) {
        const sa1_draw *d = &r->draws[i];
        int is_skin = d->archive >= (int32_t)SA1_ARCHIVE_SKIN_FIRST && d->archive <= (int32_t)SA1_ARCHIVE_SKIN_LAST;
        double c[3] = { 0.0, 0.0, 0.0 };
        if (d->prim != SA1_PRIM_TRIANGLES || !is_skin || (d->flags & (SA1_DRAW_EFFECT | SA1_DRAW_SHADOW)) != 0u || d->count == 0u) {
            continue;
        }
        for (k = 0; k < 3u * d->count; k++) {
            const float *q = r->positions + 3u * (3u * d->first) + 3u * k;
            c[0] += q[0];
            c[1] += q[1];
            c[2] += q[2];
        }
        c[0] /= 3.0 * d->count;
        c[1] /= 3.0 * d->count;
        c[2] /= 3.0 * d->count;
        if (!have) {
            memcpy(t0, c, sizeof t0);
            have = 1;
        }
        if (n + 3 > M1A_VEC_MAX) {
            return -1;
        }
        c[0] = (c[0] - t0[0]) / 4.0;
        c[1] = (c[1] - t0[1]) / 4.0;
        c[2] = (c[2] - t0[2]) / 4.0;
        v[n++] = (float)(cy * c[0] - sy * c[1]);
        v[n++] = (float)(sy * c[0] + cy * c[1]);
        v[n++] = (float)c[2];
    }
    return n;
}

static double m1a_fit(const m1a_ref *ref, int nref, const float *v, int n, double *err)
{
    int i, k, best = -1;
    double be = 1e30;
    for (i = 0; i < nref; i++) {
        double e = 0.0;
        if (ref[i].n != n) {
            continue;
        }
        for (k = 0; k < n; k++) {
            double dd = (double)ref[i].v[k] - (double)v[k];
            e += dd * dd;
        }
        if (e < be) {
            be = e;
            best = i;
        }
    }
    *err = best >= 0 ? sqrt(be / (double)(n > 0 ? n : 1)) : 1e30;
    return best >= 0 ? ref[best].ph : -1.0;
}

static double m1a_wrap(double x)
{
    x = fmod(x, 1.0);
    if (x < 0.0) {
        x += 1.0;
    }
    return x > 0.5 ? x - 1.0 : x;
}

static int m1a_foot(int32_t row)
{
    return (row >= 0 && row <= 13) || row == 20 || row == 30;
}

/* the pair's pose is row 11 at a known phase: drawn on its own, or the world's own row 11 steady this pair and the one before (not the game's link into it) */
static int m1a_jog(const sa1k_test_info *ti, const sa1w_pstate *ps, const sa1w_pstate *pp)
{
    if (ti->cur == 11 && ti->w >= 1.0f && ti->prev < 0 && ti->live_mix <= 0.0f) {
        return 1;
    }
    return ti->w <= 0.0f && ti->live_mix >= 1.0f && ti->drive_row == 11 && ps->action == 11u && ps->reqaction == 11u && ps->mtnmode != 2 && pp->action == 11u && pp->reqaction == 11u && pp->mtnmode != 2;
}

static void m1a_pstate(sa1_character_id id, sa1w_pstate *ps)
{
    memset(ps, 0, sizeof *ps);
    sa1w_world_pstate(sa1_character_world(g_ctx, id), ps);
}

static uint32_t m1a_anim(uint32_t extra)
{
    return SA1_SKIN_ANIM_FLY | SA1_SKIN_ANIM_GLOW | SA1_SKIN_ANIM_TRAIL | extra;
}

static void test_1a_mecha(void)
{
    const sa1_charset *cs = &g_ctx->chars[SA1_CHAR_SONIC];
    m1a_ref *ref = (m1a_ref *)calloc(M1A_REF_MAX, sizeof *ref);
    m1a_frame *seq = (m1a_frame *)calloc(M1A_SEQ_MAX, sizeof *seq);
    sa1k_test_info *ti = (sa1k_test_info *)malloc(sizeof *ti), *tp = (sa1k_test_info *)malloc(sizeof *tp);
    float *vec = (float *)malloc(M1A_VEC_MAX * sizeof(float));
    double top = 4.2853 / (double)s6_frames(cs, 11), last11 = -1.0;
    int nref = 0, p, prof, hooked = 0, fade_all = 0, stall_all = 0, posed_all = 0, planted_all = 0;
    sa1_character_id m = 0, s = 0;
    printf("1a-M Mecha Sonic frame by frame as Garry's Mod draws him: the floor, the jog's pace and its steps (round 1.0.0-beta1a, lane b1a-mecha)\n");
    /* M0: plain Sonic, the stick full from a stand: his row 11's frames a pair, the last steady pair before the game's switch to row 12 */
    create(0, &s);
    {
        sa1w_pstate a, b;
        double df = -1.0;
        memset(&a, 0, sizeof a);
        for (p = 0; p < 200 && s != 0; p++) {
            tick(s, 0.0f, p < 30 ? 0.0f : 1.0f, 0u);
            m1a_pstate(s, &b);
            if (a.action == 11u && a.reqaction == 11u && a.mtnmode != 2 && b.action == 11u && b.reqaction == 11u && b.mtnmode != 2) {
                df = (double)b.nframe - (double)a.nframe;
                if (df < 0.0) {
                    df += (double)s6_frames(cs, 11);
                }
            }
            if (a.action == 11u && b.action == 12u && last11 < 0.0) {
                last11 = df;
            }
            a = b;
        }
        sa1_character_destroy(g_ctx, s);
        s = 0;
    }
    CHECK(fabs(last11 - 4.2853) < 1e-3, "1a-M0 Sonic's own jog at its fastest (the stick full from a stand, the last steady row-11 pair before row 12): %.4f frames a pair (%.2f loops a second), "
          "sa1_skin.c's SK_OWN_TOP_FRAMES 4.2853", last11, last11 / (double)s6_frames(cs, 11) * 30.0);
    /* the poses of the jog, densely: a run at a tenth of the top pace, each pair as it is (alpha 1) */
    g_cur = &g_mecha;
    create(SA1_CREATE_SKIN, &m);
    g_cur = &g_skin;
    if (m == 0) {
        CHECK(0, "1a-M a Mecha world: %s", sa1_last_error(g_ctx));
        free(ref); free(seq); free(ti); free(tp); free(vec);
        return;
    }
    sa1_character_set_option(g_ctx, m, SA1_COPT_SKIN_ANIM, (int32_t)m1a_anim(SA1_SKIN_ANIM_PACE(10)));
    for (p = 0; p < 1200 && nref < M1A_REF_MAX; p++) {
        const sa1_render *rm = NULL;
        tick(m, 0.0f, p < 30 ? 0.0f : 1.0f, 0u);
        info_of(m, ti);
        if (ti->cur == 11 && ti->w >= 1.0f && ti->prev < 0 && ti->live_mix <= 0.0f && sa1_character_render(g_ctx, m, 1.0f, &rm) == SA1_OK) {
            int n = m1a_vec(rm, ref[nref].v);
            if (n > 0) {
                ref[nref].n = n;
                ref[nref].ph = ti->phase;
                nref++;
            }
        }
    }
    sa1_character_destroy(g_ctx, m);
    m = 0;
    printf("  info  %d poses of the jog at a tenth of the top pace for the fit\n", nref);
    for (prof = 0; prof < 2; prof++) {
        sa1w_pstate pm, pmp;
        double over = -1e30, deep_m[32], deep_s[32], jump_floor = 0.0, jump_m = 0.0, jump_s = 0.0, prev_lm = 0.0, prev_sd = 0.0, worst_err = 0.0, err_mid = 0.0;
        int nseq = 0, ground_n = 0, mid_n = 0, prev_ok = 0, back = 0, big = 0, deeper = 0, i, row, runs = 0;
        int gnd_prev = 0, row_prev = -1, jog_last = 0;
        /* M8, M9 (the fix run) */
        int fast_n = 0, drops = 0, fade_n = 0, stalls = 0;
        double worst_drop = 1.0, spd_prev = 0.0;
        for (i = 0; i < 32; i++) {
            deep_m[i] = deep_s[i] = 1e30;
        }
        g_cur = &g_mecha;
        create(SA1_CREATE_SKIN, &m);
        g_cur = &g_skin;
        create(0, &s);
        memset(&pm, 0, sizeof pm);
        memset(&pmp, 0, sizeof pmp);
        memset(tp, 0, sizeof *tp);
        for (p = 0; p < 600 && m != 0 && s != 0; p++) {
            int sprint = 0, sub, jog_now, jog_prev, gnd;
            float sy = prof == 0 ? s6_stick(p, &sprint) : (p < 30 ? 0.0f : (p < 330 ? (float)(p - 30) / 300.0f : (p < 480 ? 1.0f : 0.0f)));
            sa1_state_ex x;
            sa1w_pstate pq = pm;
            if (sprint) {
                s6_speed(m, 5.6f);
                s6_speed(s, 5.6f);
            }
            tick(m, 0.0f, sy, 0u);
            tick(s, 0.0f, sy, 0u);
            state_of(m, &x);
            memcpy(tp, ti, sizeof *tp);
            info_of(m, ti);
            pmp = pq;
            m1a_pstate(m, &pm);
            gnd = (x.flags & SA1_STATE_ON_GROUND) != 0u;
            jog_now = m1a_jog(ti, &pm, &pmp);
            jog_prev = p > 0 && jog_last;
            /* M8: his own jog in rows 12-13 two pairs running, while |spd.x| does not fall (sa1_skin.c (5): by less than 0.005): the step never goes down */
            if (p > 0 && tp->cur == 11 && tp->w >= 1.0f && tp->step > 0.0 && ti->cur == 11 && ti->w >= 1.0f && ti->prev < 0 && ti->step > 0.0 && (ti->drive_row == 12 || ti->drive_row == 13)) {
                double rr = ti->step / tp->step;
                fast_n++;
                if (fabs((double)pm.pspd[0]) >= spd_prev - 0.005) {
                    drops += rr < 1.0 - 1e-9;
                    worst_drop = rr < worst_drop ? rr : worst_drop;
                }
            }
            /* M9: a fade out of his own jog (w going down, still his row): the phase steps every pair */
            if (p > 0 && tp->cur == 11 && ti->cur == 11 && tp->w > 0.0f && ti->w < tp->w) {
                fade_n++;
                stalls += fabs(m1a_wrap(ti->phase - tp->phase)) < 1e-9;
            }
            spd_prev = fabs((double)pm.pspd[0]);
            for (sub = 0; sub < 2; sub++) {
                const sa1_render *rm = NULL, *rs = NULL;
                double lm, ls, rz;
                int shown = sub == 0 ? (p > 0 && gnd_prev) : (p > 0 && gnd_prev && gnd), foot, ball, n;
                int planted0 = ti->frames_planted, jog = sub == 0 ? jog_prev : (jog_prev && jog_now);
                row = sub == 0 ? row_prev : ti->drive_row;
                sa1_character_render(g_ctx, m, sub == 0 ? 0.0f : 0.5f, &rm);
                sa1_character_render(g_ctx, s, sub == 0 ? 0.0f : 0.5f, &rs);
                if (sub == 1) {
                    info_of(m, ti);
                    hooked |= ti->frames_planted > planted0;
                }
                lm = s6_low(rm, 1);
                ls = s6_low(rs, 0);
                rz = rm != NULL ? (double)rm->position[2] / 4.0 : 9.0;
                foot = m1a_foot(row_prev) && (sub == 0 || m1a_foot(ti->drive_row));
                ball = row_prev == 14 || row_prev == 15 || ti->drive_row == 14 || ti->drive_row == 15;
                if (shown && foot && !ball && rz < 0.03 && lm < 1e29 && ls < 1e29) {
                    /* every frame counts, between two pairs too (the fix run: without sa1k_frame these FAIL) */
                    double sd = ls < 0.0 ? ls : 0.0;
                    ground_n++;
                    mid_n += sub;
                    over = lm > over ? lm : over;
                    if (row >= 0 && row < 32) {
                        deep_m[row] = lm < deep_m[row] ? lm : deep_m[row];
                        deep_s[row] = sd < deep_s[row] ? sd : deep_s[row];
                    }
                    if (prev_ok) {
                        double jm = fabs(lm - prev_lm), js = fabs(sd - prev_sd);
                        if (fabs(lm) <= 0.05 && fabs(prev_lm) <= 0.05) {
                            jump_floor = jm > jump_floor ? jm : jump_floor;
                        }
                        jump_m = jm > jump_m ? jm : jump_m;
                        jump_s = js > jump_s ? js : jump_s;
                    }
                    prev_ok = 1;
                    prev_lm = lm;
                    prev_sd = sd;
                } else {
                    prev_ok = 0;
                }
                /* the drawn jog */
                n = jog ? m1a_vec(rm, vec) : -1;
                if (n > 0 && nseq < M1A_SEQ_MAX) {
                    double e, ph = m1a_fit(ref, nref, vec, n, &e);
                    if (ph >= 0.0) {
                        seq[nseq].p = p;
                        seq[nseq].sub = sub;
                        seq[nseq].ph = ph;
                        seq[nseq].err = e;
                        nseq++;
                    }
                } else if (nseq > 0 && seq[nseq - 1].p >= 0) {
                    if (nseq < M1A_SEQ_MAX) {
                        seq[nseq].p = -1;           /* a run ends */
                        nseq++;
                    }
                }
            }
            gnd_prev = gnd;
            row_prev = ti->drive_row;
            jog_last = jog_now;
        }
        /* M2: the runs of jog frames */
        for (i = 0; i < nseq;) {
            int j = i, k;
            while (j < nseq && seq[j].p >= 0) {
                j++;
            }
            if (j - i >= 8) {
                runs++;
                for (k = i + 1; k < j; k++) {
                    double st = m1a_wrap(seq[k].ph - seq[k - 1].ph), win[15], med;
                    int a = k - 7 < i + 1 ? i + 1 : k - 7, b = k + 7 >= j ? j - 1 : k + 7, c, w = 0, u, v;
                    for (c = a; c <= b && w < 15; c++) {
                        win[w++] = m1a_wrap(seq[c].ph - seq[c - 1].ph);
                    }
                    for (u = 1; u < w; u++) {
                        for (v = u; v > 0 && win[v - 1] > win[v]; v--) {
                            double tt = win[v];
                            win[v] = win[v - 1];
                            win[v - 1] = tt;
                        }
                    }
                    med = w > 0 ? win[w / 2] : st;
                    back += st < -0.005;
                    big += st > 1.5 * med + 0.004;
                    if (seq[k].sub == 1) {
                        err_mid = seq[k].err > err_mid ? seq[k].err : err_mid;
                    }
                    worst_err = seq[k].err > worst_err ? seq[k].err : worst_err;
                }
            }
            i = j + 1;
        }
        for (row = 0; row < 32; row++) {
            if (deep_m[row] < 1e29 && deep_m[row] < deep_s[row] - 0.05) {
                deeper++;
                printf("  info  run %d row %d: Mecha's deepest %+.3f, plain Sonic's own dip (or the floor) %+.3f\n", prof, row, deep_m[row], deep_s[row]);
            }
        }
        printf("  info  run %d (%s): %d frames on the ground, %d of them between two pairs; %d runs of jog frames, %d fitted frames; the 60 Hz frames %s\n", prof, prof == 0 ? "a stand, a ramp, the run, the sprint, a stop" : "a slow ramp, the run, a stop",
               ground_n, mid_n, runs, nseq, hooked ? "posed by sa1k_frame" : "NOT posed: sa1_character.c's render_build does not call sa1k_frame (frames between pairs are render_interp's chords)");
        CHECK(ground_n > 600 && mid_n > 300 && over <= 0.05 && deeper == 0, "1a-M1 run %d on the ground: his lowest point at most %+.3f over the floor (0.05) in %d frames (%d between two pairs); in no drawn row deeper than "
              "the floor or plain Sonic's own deepest dip there (%d rows over 0.05)", prof, over, ground_n, mid_n, deeper);
        CHECK(runs >= 1 && back == 0 && big == 0 && worst_err <= 0.05, "1a-M2 run %d the drawn jog frame by frame (%d runs): %d steps backward (but the wrap), %d over 1.5 x the median around them; a pose of the jog in every "
              "frame (fit error at most %.4f, between pairs %.4f; 0.05)", prof, runs, back, big, worst_err, err_mid);
        CHECK(jump_floor <= 0.05 && jump_m <= jump_s + 0.05, "1a-M4 run %d his lowest point between two frames on the floor: at most %.3f (0.05); in the run at most %.3f, plain Sonic's own (to the floor) %.3f",
              prof, jump_floor, jump_m, jump_s);
        CHECK(fast_n > 50 && drops == 0, "1a-M8 run %d the pace through the switch to rows 12-13: in %d pairs of his jog there, while he did not slow down, %d steps down (the least step against the pair "
              "before %.4f; 1.0.0-beta1a before the fix: 0.85 for a pair just after the switch)", prof, fast_n, drops, worst_drop);
        printf("  info  run %d a fade out of his jog: %d pairs, %d with its phase standing still\n", prof, fade_n, stalls);
        fade_all += fade_n;
        stall_all += stalls;
        info_of(m, ti);
        posed_all += ti->frames_posed;
        planted_all += ti->frames_planted;
        sa1_character_destroy(g_ctx, m);
        sa1_character_destroy(g_ctx, s);
        m = s = 0;
    }
    CHECK(hooked && posed_all > 300 && planted_all > 500, "1a-M7 sa1_character.c's render_build calls sa1k_frame: %d frames between two pairs posed again (his jog), %d planted, in the two runs",
          posed_all, planted_all);
    CHECK(fade_all > 0 && stall_all == 0, "1a-M9 a stop: his jog's phase steps on in every pair of its fade out (%d pairs, %d standing still; 1.0.0-beta1a before the fix: the fade's 4 pairs)", fade_all, stall_all);
    CHECK(g_said_feet > 0 && g_said_frames > 0, "1a-M11 the self-check lines G-SA1's log shows (libsa1 INFO): \"on his feet (D9) on, the jog's top (D10) 1.00 x ...\" %d times, \"the 60 Hz frames between two "
          "pairs are drawn by sa1k_frame\" %d times (once a character)", g_said_feet, g_said_frames);
    /* M6: Metal Sonic and plain Sonic frame by frame through 100-S6's run, every triangle's corners hashed: 1.0.0-beta1's (MEASURED with its code, b1a-mecha probe_metal.c) */
    {
        sa1_character_id t = 0;
        uint32_t ht = 2166136261u, hs = 2166136261u, mt = 0, k;
        int sub;
        g_cur = &g_skin;
        create(SA1_CREATE_SKIN, &t);
        create(0, &s);
        for (p = 0; p < 600 && t != 0 && s != 0; p++) {
            int sprint = 0;
            float sy = s6_stick(p, &sprint);
            if (sprint) {
                s6_speed(t, 5.6f);
                s6_speed(s, 5.6f);
            }
            tick(t, 0.0f, sy, 0u);
            tick(s, 0.0f, sy, 0u);
            for (sub = 0; sub < 2; sub++) {
                const sa1_render *rt = NULL, *rs = NULL;
                sa1_character_render(g_ctx, t, sub ? 0.5f : 0.0f, &rt);
                for (k = 0; rt != NULL && rt->positions != NULL && k < 36u * rt->num_triangles; k++) {
                    ht = (ht ^ ((const uint8_t *)rt->positions)[k]) * 16777619u;
                }
                mt += rt != NULL ? rt->num_triangles : 0u;
                sa1_character_render(g_ctx, s, sub ? 0.5f : 0.0f, &rs);
                for (k = 0; rs != NULL && rs->positions != NULL && k < 36u * rs->num_triangles; k++) {
                    hs = (hs ^ ((const uint8_t *)rs->positions)[k]) * 16777619u;
                }
            }
        }
        CHECK(mt == 1220200u && ht == 0x03578E38u && hs == 0x36CCFCACu, "1a-M6 frame by frame (alpha 0 and 0.5) through the same run: Metal Sonic's %u triangles hash %08X, plain Sonic's %08X (1.0.0-beta1: 1220200, 03578E38, 36CCFCAC)",
              (unsigned)mt, (unsigned)ht, (unsigned)hs);
        sa1_character_destroy(g_ctx, t);
        sa1_character_destroy(g_ctx, s);
        s = 0;
    }
    /* M5: the options */
    {
        static const struct { uint32_t extra; const char *what; } opt[3] = { { SA1_SKIN_ANIM_NO_PLANT, "NO_PLANT" }, { SA1_SKIN_ANIM_PACE(SA1_SKIN_ANIM_PACE_FREE), "PACE_FREE" },
                                                                             { SA1_SKIN_ANIM_PACE(50), "PACE(50)" } };
        int o;
        for (o = 0; o < 3; o++) {
            int shifted = 0, planted = 0, n = 0, steps = 0, over_top = 0, half_over = 0, half_at = 0;
            double ratio = 0.0, max_step = 0.0, off_hi = -1e30, off_lo = 1e30;     /* (the fix run) NO_PLANT: his lowest point against the floor, on the ground */
            g_cur = &g_mecha;
            create(SA1_CREATE_SKIN, &m);
            g_cur = &g_skin;
            if (m == 0) {
                CHECK(0, "1a-M5 a Mecha world");
                continue;
            }
            sa1_character_set_option(g_ctx, m, SA1_COPT_SKIN_ANIM, (int32_t)m1a_anim(opt[o].extra));
            for (p = 0; p < 360; p++) {
                int sprint = 0;
                float sy = s6_stick(p, &sprint);
                const sa1_render *rm = NULL;
                if (sprint) {
                    s6_speed(m, 5.6f);
                }
                tick(m, 0.0f, sy, 0u);
                if (o == 0) {
                    sa1_state_ex x;
                    state_of(m, &x);
                    sa1_character_render(g_ctx, m, 0.0f, &rm);
                    info_of(m, ti);
                    if ((x.flags & SA1_STATE_ON_GROUND) && ti->drive_row >= 0 && ti->drive_row <= 13 && p > 30 && rm != NULL && rm->position[2] / 4.0f < 0.03f) {
                        double l = s6_low(rm, 1);
                        off_hi = l > off_hi ? l : off_hi;
                        off_lo = l < off_lo ? l : off_lo;
                    }
                }
                sa1_character_render(g_ctx, m, 0.5f, &rm);
                info_of(m, ti);
                shifted += ti->shift != 0.0;
                planted = ti->frames_planted;
                if (ti->cur == 11 && ti->w >= 1.0f && ti->drive_row >= 12 && ti->step > 0.0) {
                    max_step = ti->step > max_step ? ti->step : max_step;
                    if (p >= 200 && ti->rate > 0.0) {
                        double k = ti->drive_row == 12 ? 1.6 : 1.65;
                        ratio += ti->step / (k * ti->rate);
                        steps++;
                    }
                    over_top += ti->step > top + 1e-9;
                    if (p >= 200) {
                        half_over += ti->step > 0.5 * top + 1e-9;
                        half_at += fabs(ti->step - 0.5 * top) < 1e-9;
                    }
                }
                n++;
            }
            if (o == 0) {
                CHECK(shifted == 0 && planted == 0, "1a-M5 SA1_SKIN_ANIM_NO_PLANT: no step along up in %d pairs (%d), no 60 Hz frame of his (%d): his parts where Sonic's would be", n, shifted, planted);
                printf("  info  SA1_SKIN_ANIM_NO_PLANT on the ground in rows 0-13 (pairs): his lowest point from %+.3f to %+.3f against the floor (the Q-menu box's off text)\n", off_lo, off_hi);
            } else if (o == 1) {
                CHECK(steps > 50 && fabs(ratio / steps - 1.0) <= 0.01 && over_top > 50, "1a-M5 SA1_SKIN_ANIM_PACE_FREE: 1.0.0-beta1's jog, k x the world row's rate (1.6, 1.65): %.4f of it on average in %d pairs, %d pairs above the top",
                      steps ? ratio / steps : 0.0, steps, over_top);
            } else {
                CHECK(half_over == 0 && half_at > 100, "1a-M5 SA1_SKIN_ANIM_PACE(50): half Sonic's own fastest jog (%.5f a pair) from 30 pairs after the switch: %d pairs above it, %d at it; at the switch it goes on at Sonic's own pace (%.5f) and eases down",
                      0.5 * top, half_over, half_at, max_step);
            }
            sa1_character_destroy(g_ctx, m);
            m = 0;
        }
    }
    free(ref);
    free(seq);
    free(ti);
    free(tp);
    free(vec);
}

/* ---- round 1.0.0-beta1a, the lane's fix run: 1a-M10, slopes (v-mecha's vm_slope.c, a host plane through the origin; MEASURED +0.73 over it between pairs when sa1k_frame went along the host's
 * up). He runs along +x (the stick forward); the plane z = (ax x + ay y) tan(12 degrees) rises along his way, falls along it, or rises across it ---------------------------------------------- */
static double g_sl_tan, g_sl_cos, g_sl_ax, g_sl_ay;

static int scene_slope(sa1_context *ctx, double deg, double ax, double ay)
{
    sa1_collision_tri *t = NULL;
    uint32_t n = 0, cap = 0;
    float x, y;
    sa1_status st;
    g_sl_tan = tan(deg * PI / 180.0);
    g_sl_cos = cos(deg * PI / 180.0);
    g_sl_ax = ax;
    g_sl_ay = ay;
    for (x = -16384.0f; x < 16383.0f; x += 1024.0f) {
        for (y = -16384.0f; y < 16383.0f; y += 1024.0f) {
            float x1 = x + 1024.0f, y1 = y + 1024.0f;
            float p0[3] = { x, y, (float)((ax * x + ay * y) * g_sl_tan) }, p1[3] = { x1, y, (float)((ax * x1 + ay * y) * g_sl_tan) };
            float p2[3] = { x1, y1, (float)((ax * x1 + ay * y1) * g_sl_tan) }, p3[3] = { x, y1, (float)((ax * x + ay * y1) * g_sl_tan) };
            add_tri(&t, &n, &cap, p0, p1, p2);
            add_tri(&t, &n, &cap, p0, p2, p3);
        }
    }
    st = sa1_collision_set_static(ctx, t, n);
    free(t);
    return SA1_FAILED(st) ? -1 : 0;
}

/* the least signed distance of a render's skin parts (skin 1) or body (0) to the slope, along its normal, SA1 units */
static double m1a_low_slope(const sa1_render *r, int skin)
{
    uint32_t i, k;
    double lo = 1e30;
    if (r == NULL) {
        return lo;
    }
    for (i = 0; i < r->num_draws; i++) {
        const sa1_draw *d = &r->draws[i];
        int is_skin = d->archive >= (int32_t)SA1_ARCHIVE_SKIN_FIRST && d->archive <= (int32_t)SA1_ARCHIVE_SKIN_LAST;
        if (d->prim != SA1_PRIM_TRIANGLES || (skin ? !is_skin : (d->flags & (SA1_DRAW_EFFECT | SA1_DRAW_SHADOW | SA1_DRAW_OBJECT)) != 0u)) {
            continue;
        }
        for (k = 0; k < 3u * d->count; k++) {
            const float *v = r->positions + 9u * d->first + 3u * k;
            double h = ((double)v[2] - (g_sl_ax * (double)v[0] + g_sl_ay * (double)v[1]) * g_sl_tan) * g_sl_cos / 4.0;
            lo = h < lo ? h : lo;
        }
    }
    return lo;
}

static void test_1a_slope(void)
{
    static const char *what[4] = { "up a slope", "down a slope", "across a slope", "up a slope at an uneven frame rate (50-75 fps)" };
    static const double sax[4] = { 1.0, -1.0, 0.0, 1.0 }, say[4] = { 0.0, 0.0, 1.0, 0.0 };
    int dir;
    uint32_t rnd = 12345u;
    printf("1a-M10 Mecha Sonic on 12 degree slopes, frame by frame the module's way (tick the frame's dt, render alpha -1)\n");
    for (dir = 0; dir < 4; dir++) {
        sa1_character_id m = 0, s = 0;
        double over[2] = { -1e30, -1e30 }, deep_m[32], deep_s[32], top_spd = 0.0, tilt = 0.0, x0 = 0.0, x1 = 0.0;
        int n[2] = { 0, 0 }, f, row, deeper = 0;
        uint32_t rows_seen = 0u;
        for (row = 0; row < 32; row++) {
            deep_m[row] = deep_s[row] = 1e30;
        }
        if (scene_slope(g_ctx, 12.0, sax[dir], say[dir]) != 0) {
            CHECK(0, "1a-M10 the slope: %s", sa1_last_error(g_ctx));
            break;
        }
        g_cur = &g_mecha;
        create(SA1_CREATE_SKIN, &m);
        g_cur = &g_skin;
        create(0, &s);
        for (f = 0; f < 600 && m != 0 && s != 0; f++) {
            sa1_input in;
            const sa1_render *rm = NULL, *rs = NULL;
            sa1_state_ex x;
            sa1w_pstate pm;
            float dt = 1.0f / 60.0f;
            if (dir == 3) {
                rnd = rnd * 1103515245u + 12345u;
                dt = 1.0f / (50.0f + 25.0f * (float)((rnd >> 16) & 0x7FFFu) / 32767.0f);
            }
            memset(&in, 0, sizeof in);
            in.struct_size = sizeof in;
            if (f >= 60 && f < 520) {
                in.stick_y = 1.0f;
            }
            sa1_character_tick(g_ctx, m, &in, dt, NULL, NULL);
            sa1_character_tick(g_ctx, s, &in, dt, NULL, NULL);
            state_of(m, &x);
            m1a_pstate(m, &pm);
            sa1_character_render(g_ctx, m, -1.0f, &rm);
            sa1_character_render(g_ctx, s, -1.0f, &rs);
            row = (int)pm.drawn_action;
            if (f == 60) {
                x0 = (double)x.position[0] / 4.0;
            }
            x1 = (double)x.position[0] / 4.0;
            if (rm != NULL && rs != NULL && (x.flags & SA1_STATE_ON_GROUND) && row >= 0 && row <= 13 && f > 90) {
                double lm = m1a_low_slope(rm, 1), ls = m1a_low_slope(rs, 0), sd = ls < 0.0 ? ls : 0.0;
                int b = rm->alpha > 1e-4f, ax = (int16_t)(x.angle[0] & 0xFFFF), az = (int16_t)(x.angle[2] & 0xFFFF);
                double tl = sqrt((double)ax * ax + (double)az * az) * 360.0 / 65536.0;
                if (lm > 1e29 || ls > 1e29) {
                    continue;
                }
                rows_seen |= 1u << row;
                top_spd = fabs((double)x.player_speed[0]) > top_spd ? fabs((double)x.player_speed[0]) : top_spd;
                tilt = tl > tilt ? tl : tilt;
                over[b] = lm > over[b] ? lm : over[b];
                n[b]++;
                deep_m[row] = lm < deep_m[row] ? lm : deep_m[row];
                deep_s[row] = sd < deep_s[row] ? sd : deep_s[row];
            }
        }
        for (row = 0; row < 32; row++) {
            if (deep_m[row] < 1e29 && deep_m[row] < deep_s[row] - 0.05) {
                deeper++;
                printf("  info  %s row %d: Mecha's deepest %+.3f, plain Sonic's own (or the slope) %+.3f\n", what[dir], row, deep_m[row], deep_s[row]);
            }
        }
        printf("  info  %s: x from %.1f to %.1f, at most spd.x %.3f, tilted up to %.1f degrees, rows seen %08X\n", what[dir], x0, x1, top_spd, tilt, (unsigned)rows_seen);
        CHECK(n[1] > 200 && (dir == 3 || n[0] > 200) && over[0] <= 0.05 && over[1] <= 0.05 && deeper == 0, "1a-M10 %s: on the ground in rows 0-13 his lowest point at most %+.3f over the slope at pairs "
              "(%d frames), %+.3f between them (%d frames; 0.05); in no row deeper than plain Sonic's own or the slope (%d rows over 0.05)", what[dir], over[0] > -1e29 ? over[0] : 0.0, n[0], over[1], n[1], deeper);
        sa1_character_destroy(g_ctx, m);
        sa1_character_destroy(g_ctx, s);
    }
    if (scene_flat(g_ctx) != 0) {
        CHECK(0, "1a-M10 the flat floor back");
    }
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
    mecha_init();
    sa1_host_defaults(&h);
    h.log = log_cb;
    h.log_level = SA1_LOG_INFO;     /* (the fix run: 1a-M11 reads the self-check lines; log_cb still prints only warnings and errors) */
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
    test_s10();
    test_s11();
    test_s6();
    test_s7();
    test_s8();
    test_s9(gdi);
    test_s12();
    test_s1();
    /* round 1.0.0-beta1 (select 7.1): Metal is slot 0 (archive 64) from the tests above; Mecha takes slot 1 (65); S1 fills all 16 last */
    test_100_s5();
    test_100_s4();
    test_100_s2();
    test_100_s3();
    test_100_s6();          /* the owner's answer 17: the jog, the plant (Metal is still slot 0, Mecha slot 1) */
    test_100_s6_feet(0);
    test_100_s6_feet(1);
    test_1a_mecha();        /* round 1.0.0-beta1a: Mecha frame by frame (Mecha is still slot 1) */
    test_1a_slope();        /* round 1.0.0-beta1a, the lane's fix run: slopes (the flat floor back after it) */
    test_100_s1();
    printf("DETERMINISM skin %08X\n", (unsigned)g_hash);
    printf("\nskin_test: %d checks, %d failed\n", g_ok + g_fail, g_fail);
    sa1_shutdown(g_ctx);
    return g_fail ? 1 : 0;
}
