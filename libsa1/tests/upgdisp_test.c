/*
 * upgdisp_test.c - round 1.0.0-beta1: the game option SA1_GOPT_UPGRADE_MODELS, the display set of the upgrades (sadc docs/research/gsa1_100_select.md 3, 4.3.3
 * and 7.1 U1-U6, which widen gsa1_091b_upgrades.md 5.1 with its review R.4; the SA1_GA blocks of sadc include/sa1/upgdisp.h in nine TUs).
 *
 *   py -3.12 tests/build_upgdisp.py --run [--target windows] [--gdi GDI]
 *   upgdisp_test [--verbose] GDI
 *
 * "Active" is the game's own upgrade (its event flags, written into the world before its first step as G-SA1 does); "visible" is the display set, given
 * before the first step. Worlds of each kind on one flat floor (SA1 space), the same input:
 *   U1 keeps      held + none shown against held, and none held + all shown against none held, 400 pairs of running, jumping and the action button:
 *                 sa1_state_ex, taskwk, motionwk, the playerwk (the feet points excluded: they follow the drawn shoe) and the collision shapes the same
 *                 in every pair, against a control world of the same make (whatever differs there is not counted)
 *   U2 looks      at a stand: held + none shown draws as none held, none held + all shown draws as held (positions, UVs, colours, draws, bit for bit;
 *                 the first two pairs are not counted: a part that appears for the first time is unwelded for one pair)
 *   U3 one by one each upgrade shown alone on a world without it draws as the world with only it; hidden alone on a world with all, as the world without it
 *   U4 the logic  Amy's hammer and E-102's blaster: U1's script covers their attacks; the hit shapes and the hand points of the real bit's world in every pair
 *   U5 lures      Big with the four lure flags and one lure shown: the three lure blocks say so; B_8C78C5D8 and the state the game's in every pair
 *   U6 run time   toggled every 30 pairs, no fault; bad values refused; a missing block named (SA1_ERR_NOT_IMPLEMENTED); 0 is always SA1_OK
 * Every check fails on libsa1 0.5.4, which refuses the option.
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

static int g_fail, g_ok, g_verbose;
static uint32_t g_hash = 2166136261u;
static sa1_context *g_ctx;
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

#define EVF       0x8C799920u   /* Uint8 B_8C799920[]: the event / upgrade flags */
#define LURE_N    0x8C78C5D8u   /* Sint32 B_8C78C5D8: func_8C0E6E80's count of the lure flags */
#define ON        SA1_UPGRADE_MODELS_ON

typedef struct kspec {
    int32_t kind;
    const char *name;
    int n;
    uint16_t flag[6];           /* the game's event flags */
    uint32_t bit[6];            /* their SA1_UPGRADE_* bits */
} kspec;

static const kspec k_kinds[6] = {
    { SA1_CHAR_SONIC, "Sonic", 2, { 0x8D, 0x8E }, { SA1_UPGRADE_LIGHT_SPEED_SHOES, SA1_UPGRADE_CRYSTAL_RING } },
    { SA1_CHAR_TAILS, "Tails", 2, { 0xCD, 0xDB }, { SA1_UPGRADE_JET_ANKLET, SA1_UPGRADE_RHYTHM_BADGE } },
    { SA1_CHAR_KNUCKLES, "Knuckles", 2, { 0x119, 0x11A }, { SA1_UPGRADE_SHOVEL_CLAW, SA1_UPGRADE_FIGHTING_GLOVES } },
    { SA1_CHAR_AMY, "Amy", 2, { 0x152, 0x15E }, { SA1_UPGRADE_WARRIOR_FEATHER, SA1_UPGRADE_LONG_HAMMER } },
    { SA1_CHAR_GAMMA, "E-102", 2, { 0x189, 0x18A }, { SA1_UPGRADE_JET_BOOSTER, SA1_UPGRADE_LASER_BLASTER } },
    { SA1_CHAR_BIG, "Big", 6, { 0x1D0, 0x1D1, 0x1DE, 0x1DF, 0x1E0, 0x1E1 },
      { SA1_UPGRADE_LIFE_BELT, SA1_UPGRADE_POWER_ROD, SA1_UPGRADE_LURE_1, SA1_UPGRADE_LURE_2, SA1_UPGRADE_LURE_3, SA1_UPGRADE_LURE_4 } },
};

static void log_cb(void *user, int32_t level, const char *msg)
{
    (void)user;
    if (level >= SA1_LOG_WARN || g_verbose) {
        printf("    [libsa1] %s\n", msg);
    }
}

static void hadd(const void *p, size_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    size_t i;
    for (i = 0; i < n; i++) {
        g_hash = (g_hash ^ b[i]) * 16777619u;
    }
}

static uint32_t wrd(const sa1g_world *w, uint32_t g)
{
    uint32_t v = 0;
    if (w == NULL || sa1g_world_rd32(w, g, &v) != 0) {
        return 0;
    }
    return v;
}

static uint32_t all_bits(const kspec *k)
{
    uint32_t b = 0;
    int i;
    for (i = 0; i < k->n; i++) {
        b |= k->bit[i];
    }
    return b;
}

/* a world of k holding the flags of `held` (a mask over k's list), the display set `gopt` before its first step (-1: none) */
static sa1_character_id mk(const kspec *k, uint32_t held, int64_t gopt)
{
    sa1_character_desc d;
    sa1_character_id id = 0;
    struct sa1g_world *w;
    sa1_status st;
    int i;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.kind = k->kind;
    d.flags = SA1_CREATE_WORLD | SA1_CREATE_SA1_SPACE | SA1_CREATE_ANGLES | SA1_CREATE_SEED;
    d.seed = 0x00100B01u;
    d.position[0] = -2000.0f;
    d.position[1] = 2.0f;
    if (SA1_FAILED(sa1_character_create_ex(g_ctx, &d, &id))) {
        printf("  create %s: %s\n", k->name, sa1_last_error(g_ctx));
        return 0;
    }
    w = sa1_character_world(g_ctx, id);
    for (i = 0; i < k->n; i++) {
        if (held & (1u << i)) {
            uint32_t a = EVF + k->flag[i], word = a & ~3u, v = wrd(w, word), sh = 8u * (a & 3u);
            v = (v & ~(0xFFu << sh)) | (1u << sh);
            sa1g_world_wr32(w, word, v);
        }
    }
    if (gopt >= 0) {
        st = sa1_character_set_game_option(g_ctx, id, SA1_GOPT_UPGRADE_MODELS, (int32_t)(uint32_t)gopt);
        if (st != SA1_OK) {
            printf("  set_game_option(UPGRADE_MODELS, 0x%08X) on %s: %s: %s\n", (unsigned)gopt, k->name, sa1_status_string(st), sa1_last_error(g_ctx));
            sa1_character_destroy(g_ctx, id);
            return 0;
        }
    }
    return id;
}

/* KEEPS: a cycle of 200 pairs: stand, run, a jump held (Tails flies, Knuckles glides, E-102 hovers), run, the action button (an attack, a charge, a
   shot, a cast), stand. keeps 2 (Amy, gsa1_091b_upgrades.md 3.3): a swing, then the action held and the stick turned by 22 and 38 degrees in turn: her
   spin, action 86, where node 36 of the hammer is the hit sphere. STAND: nothing */
static void script(int keeps, int p, sa1_input *in)
{
    int c = p % 200;
    memset(in, 0, sizeof *in);
    in->struct_size = sizeof *in;
    if (!keeps || c < 20) {
        return;
    }
    if (keeps == 2) {
        /* sadc build/research/upgrades/upg_probe.c amy(): "20n1a60n60h120r40n" (r: 22 / 38 degrees per pair in turn: 8C0DCE94 resets on two equal turns) */
        if (p == 20) {
            in->buttons = SA1_BUTTON_ACTION;
        } else if (p >= 81 && p < 141) {
            in->buttons = SA1_BUTTON_ACTION;
        } else if (p >= 141 && p < 261) {
            uint32_t n = (uint32_t)(p - 140);
            double a = (30.0 * (double)(n & ~1u) + ((n & 1u) ? 22.0 : 0.0)) * 3.14159265358979323846 / 180.0;
            in->buttons = SA1_BUTTON_ACTION;
            in->stick_x = (float)cos(a);
            in->stick_y = (float)sin(a);
        }
        return;
    }
    if (c < 100) {
        in->stick_y = 1.0f;
    } else if (c < 130) {
        in->stick_y = 1.0f;
        in->buttons = SA1_BUTTON_JUMP;
    } else if (c < 140) {
        in->stick_y = 1.0f;
        in->stick_x = c < 120 ? 0.5f : -0.5f;
    } else if (c < 146) {
        in->buttons = SA1_BUTTON_ACTION;
    } else if (c < 160) {
        return;
    } else if (c < 190) {
        in->buttons = SA1_BUTTON_ACTION;
        in->stick_y = c < 175 ? 0.0f : 1.0f;
    }
}

static int g_keeps = 1;     /* the KEEPS script of the kind running (2: Amy's spin) */
static int tick(sa1_character_id id, int keeps, int p)
{
    sa1_input in;
    sa1_event ev[64];
    uint32_t n;
    script(keeps ? g_keeps : 0, p, &in);
    if (sa1_character_tick(g_ctx, id, &in, 1.0f / 30.0f, NULL, NULL) != SA1_OK) {
        printf("  tick %d: %s\n", p, sa1_last_error(g_ctx));
        return -1;
    }
    do {
        n = 0;
        sa1_poll_events(g_ctx, ev, 64, &n);
    } while (n == 64);
    return 0;
}

/* ---- the state of a pair ---------------------------------------------------------------------------------------------------------------------- */
#define R_STATE 0x01u
#define R_TWP   0x02u
#define R_MWP   0x04u
#define R_PWP   0x08u   /* playerwk but the hand, foot and user points */
#define R_HANDS 0x10u   /* righthand / lefthand _pos, _vec (0x14C, 0x194): E-102's shot starts at his hand */
#define R_FEET  0x20u   /* rightfoot / leftfoot _pos, _vec (0x164, 0x1AC): they follow the drawn shoe (select 3.2) */
#define R_USER  0x40u   /* user0 / user1 _pos, _vec (Big's fishing) */
#define R_CCL   0x80u   /* the collision shapes (colliwk.info[]) */
#define R_EWK   0x100u  /* E-102's work block (playerwk.free.ptr[0], 0x244 bytes: arm_pos, the lock-ons) */
typedef struct snap {
    sa1_state_ex x;
    uint8_t twp[0x40], mwp[0x40], pwp[0x200];
    uint8_t ccl[16 * 0x34];
    uint32_t nccl;
    int32_t lure_n;
    uint8_t ewk[0x244];
} snap;

static void rd_bytes(const sa1g_world *w, uint32_t g, uint8_t *out, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i += 4u) {
        uint32_t v = wrd(w, g + i);
        memcpy(out + i, &v, 4);
    }
}

static void snap_of(sa1_character_id id, snap *s)
{
    const sa1g_world *w = sa1_character_world(g_ctx, id);
    sa1g_player_ptrs pp;
    uint32_t cwp;
    memset(s, 0, sizeof *s);
    s->x.struct_size = sizeof s->x;
    sa1_character_state_ex(g_ctx, id, &s->x);
    sa1g_world_slot_player(w, &pp);
    rd_bytes(w, pp.twp, s->twp, 0x40u);
    rd_bytes(w, pp.mwp, s->mwp, 0x40u);
    rd_bytes(w, pp.pwp, s->pwp, 0x200u);
    cwp = wrd(w, pp.twp + 0x38u);
    if (cwp != 0u) {
        uint32_t nb = wrd(w, cwp + 4u) & 0xFFFFu, info = wrd(w, cwp + 0x0Cu);
        s->nccl = nb > 16u ? 16u : nb;
        if (info != 0u) {
            rd_bytes(w, info, s->ccl, 0x34u * s->nccl);
        }
    }
    s->lure_n = (int32_t)wrd(w, LURE_N);
    if (s->x.struct_size && wrd(w, pp.twp + 0x08u) != 0u && ((wrd(w, pp.twp + 0x08u) >> 8) & 0xFFu) == 6u) {
        uint32_t ewk = wrd(w, pp.pwp + 0x7Cu);
        if (ewk != 0u) {
            rd_bytes(w, ewk, s->ewk, 0x244u);
        }
    }
}

static int rng_ne(const uint8_t *a, const uint8_t *b, uint32_t lo, uint32_t hi)
{
    return memcmp(a + lo, b + lo, hi - lo) != 0;
}

static uint32_t snap_diff(const snap *a, const snap *b)
{
    uint32_t d = 0, k;
    sa1_state_ex xa = a->x, xb = b->x;
    /* the feet points of the weld evaluation follow the drawn shoe (select 3.2): R_FEET, not the state */
    for (k = SA1_ATTACH_RIGHT_FOOT; k <= SA1_ATTACH_LEFT_FOOT; k++) {
        if (memcmp(xa.attach[k], xb.attach[k], 12) != 0 || memcmp(xa.attach_dir[k], xb.attach_dir[k], 12) != 0) {
            d |= R_FEET;
        }
        memset(xa.attach[k], 0, 12);
        memset(xb.attach[k], 0, 12);
        memset(xa.attach_dir[k], 0, 12);
        memset(xb.attach_dir[k], 0, 12);
    }
    if ((xa.attach_valid ^ xb.attach_valid) & ((1u << SA1_ATTACH_RIGHT_FOOT) | (1u << SA1_ATTACH_LEFT_FOOT))) {
        d |= R_FEET;
    }
    xa.attach_valid &= ~((1u << SA1_ATTACH_RIGHT_FOOT) | (1u << SA1_ATTACH_LEFT_FOOT));
    xb.attach_valid &= ~((1u << SA1_ATTACH_RIGHT_FOOT) | (1u << SA1_ATTACH_LEFT_FOOT));
    if (memcmp(&xa, &xb, sizeof xa) != 0 && g_verbose) {
        const uint8_t *pa = (const uint8_t *)&xa, *pb = (const uint8_t *)&xb;
        size_t o;
        for (o = 0; o < sizeof xa && pa[o] == pb[o]; o++)
            ;
        printf("    sa1_state_ex differs first at offset %u (attach at %u, attach_valid at %u)\n", (unsigned)o, (unsigned)offsetof(sa1_state_ex, attach),
               (unsigned)offsetof(sa1_state_ex, attach_valid));
    }
    d |= memcmp(&xa, &xb, sizeof xa) != 0 ? R_STATE : 0u;
    d |= memcmp(a->twp, b->twp, 0x40u) != 0 ? R_TWP : 0u;
    d |= memcmp(a->mwp, b->mwp, 0x40u) != 0 ? R_MWP : 0u;
    d |= (rng_ne(a->pwp, b->pwp, 0x000u, 0x14Cu) || rng_ne(a->pwp, b->pwp, 0x1DCu, 0x200u)) ? R_PWP : 0u;
    d |= (rng_ne(a->pwp, b->pwp, 0x14Cu, 0x164u) || rng_ne(a->pwp, b->pwp, 0x194u, 0x1ACu)) ? R_HANDS : 0u;
    d |= (rng_ne(a->pwp, b->pwp, 0x164u, 0x17Cu) || rng_ne(a->pwp, b->pwp, 0x1ACu, 0x1C4u)) ? R_FEET : 0u;
    d |= (rng_ne(a->pwp, b->pwp, 0x17Cu, 0x194u) || rng_ne(a->pwp, b->pwp, 0x1C4u, 0x1DCu)) ? R_USER : 0u;
    d |= (a->nccl != b->nccl || memcmp(a->ccl, b->ccl, 0x34u * a->nccl) != 0) ? R_CCL : 0u;
    d |= a->lure_n != b->lure_n ? R_STATE : 0u;
    d |= memcmp(a->ewk, b->ewk, sizeof a->ewk) != 0 ? R_EWK : 0u;
    return d;
}

static const char *regions(uint32_t d, char *buf, size_t cap)
{
    static const char *const nm[9] = { "state", "taskwk", "motionwk", "playerwk", "hands", "feet", "user points", "collision", "E-102's work" };
    int i;
    size_t n = 0;
    buf[0] = 0;
    for (i = 0; i < 9; i++) {
        if (d & (1u << i)) {
            n += (size_t)snprintf(buf + n, n < cap ? cap - n : 0, "%s%s", n ? ", " : "", nm[i]);
        }
    }
    return n ? buf : "none";
}

/* U1 / U4 / U5: world t against world r over `pairs` of KEEPS, a control c of r's make; returns the regions that differed where the control did not */
static uint32_t keeps_run(const kspec *k, uint32_t held_r, int64_t g_r, uint32_t held_t, int64_t g_t, int pairs, uint32_t *bad_pairs, int *actions86)
{
    sa1_character_id r = mk(k, held_r, g_r), t = mk(k, held_t, g_t), c = mk(k, held_r, g_r);
    uint32_t all = 0;
    int p;
    static snap sr, st, sc;
    *bad_pairs = 0;
    if (actions86) {
        *actions86 = 0;
    }
    if (!r || !t || !c) {
        sa1_character_destroy(g_ctx, r);
        sa1_character_destroy(g_ctx, t);
        sa1_character_destroy(g_ctx, c);
        return 0xFFFFFFFFu;
    }
    for (p = 0; p < pairs; p++) {
        uint32_t d, dc;
        if (tick(r, 1, p) || tick(t, 1, p) || tick(c, 1, p)) {
            all = 0xFFFFFFFFu;
            break;
        }
        snap_of(r, &sr);
        snap_of(t, &st);
        snap_of(c, &sc);
        hadd(&st.x.sa1_position, sizeof st.x.sa1_position);
        if (actions86 && (sr.x.action == 86 || (int16_t)(sr.pwp[0x124] | (sr.pwp[0x125] << 8)) == 86)) {
            (*actions86)++;
        }
        d = snap_diff(&sr, &st);
        dc = snap_diff(&sr, &sc);
        d &= ~dc;
        if (d & ~R_FEET) {
            if (*bad_pairs < 3u && g_verbose) {
                char b[128];
                printf("    pair %d: %s differ (action %d / %d)\n", p, regions(d, b, sizeof b), (int)sr.x.action, (int)st.x.action);
            }
            (*bad_pairs)++;
        }
        all |= d;
    }
    sa1_character_destroy(g_ctx, r);
    sa1_character_destroy(g_ctx, t);
    sa1_character_destroy(g_ctx, c);
    return all;
}

static void test_u1(void)
{
    int i;
    printf("U1 keeps: the ability is the game's, the display set is not seen by it\n");
    for (i = 0; i < 6; i++) {
        const kspec *k = &k_kinds[i];
        uint32_t allh = (1u << k->n) - 1u, bad = 0, bad2 = 0, d, d2;
        int a86 = 0, a86b = 0;
        char b1[160], b2[160];
        g_keeps = k->kind == SA1_CHAR_AMY ? 2 : 1;
        d = keeps_run(k, allh, -1, allh, (int64_t)ON, 400, &bad, &a86);
        d2 = keeps_run(k, 0u, -1, 0u, (int64_t)(ON | all_bits(k)), 400, &bad2, &a86b);
        g_keeps = 1;
        CHECK(d != 0xFFFFFFFFu && d2 != 0xFFFFFFFFu && bad == 0u && bad2 == 0u,
              "U1 %s: held + none shown against held: %u of 400 pairs differ (%s); none held + all shown against none held: %u (%s); the feet are not counted",
              k->name, (unsigned)bad, regions(d & ~R_FEET, b1, sizeof b1), (unsigned)bad2, regions(d2 & ~R_FEET, b2, sizeof b2));
        if (k->kind == SA1_CHAR_AMY || k->kind == SA1_CHAR_GAMMA) {
            CHECK(d != 0xFFFFFFFFu && !(d & (R_CCL | R_HANDS | R_EWK)) && !(d2 & (R_CCL | R_HANDS | R_EWK)) && (k->kind != SA1_CHAR_AMY || a86 > 0),
                  "U4 %s: the hit shapes, the hand points%s are the real bit's in every pair, both ways (action 86, the spin, in %d pairs held; %d without the upgrades: the spin needs the Warrior Feather)", k->name,
                  k->kind == SA1_CHAR_GAMMA ? " and his work block (arm_pos)" : "", a86, a86b);
        }
    }
}

/* ---- the render output ------------------------------------------------------------------------------------------------------------------------ */
/* body_only: the draws that are not SA1_DRAW_EFFECT (Big: the fishing line and the lure on it start where the real rod bit puts them, code_8C0E79D4.c:66-77,
   and the lure model is the one of the lures shown: select 3.2) */
static uint32_t render_hash(sa1_character_id id, uint32_t *tris, int body_only)
{
    const sa1_render *r = NULL;
    uint32_t h = 2166136261u, i;
    const uint8_t *b;
    size_t n;
    *tris = 0;
    if (sa1_character_render(g_ctx, id, 1.0f, &r) != SA1_OK || r == NULL) {
        return 0;
    }
    *tris = r->num_triangles;
    if (body_only) {
        for (i = 0; i < r->num_draws; i++) {
            const sa1_draw *d = &r->draws[i];
            uint32_t k, w[4] = { d->prim, d->count, (uint32_t)d->archive, (uint32_t)d->texture };
            if (d->flags & SA1_DRAW_EFFECT) {
                continue;
            }
            for (k = 0; k < 4u; k++) {
                h = (h ^ w[k]) * 16777619u;
            }
            if (d->prim == SA1_PRIM_TRIANGLES) {
                b = (const uint8_t *)(r->positions + 9u * d->first);
                for (k = 0; k < 36u * d->count; k++) {
                    h = (h ^ b[k]) * 16777619u;
                }
                b = (const uint8_t *)(r->uvs + 6u * d->first);
                for (k = 0; k < 24u * d->count; k++) {
                    h = (h ^ b[k]) * 16777619u;
                }
                b = r->colors + 12u * d->first;
                for (k = 0; k < 12u * d->count; k++) {
                    h = (h ^ b[k]) * 16777619u;
                }
            }
        }
        return h;
    }
#define RH(p, len)                                                                                                     \
    do {                                                                                                               \
        b = (const uint8_t *)(p);                                                                                      \
        n = (len);                                                                                                     \
        for (i = 0; i < n; i++) {                                                                                      \
            h = (h ^ b[i]) * 16777619u;                                                                                \
        }                                                                                                              \
    } while (0)
    RH(r->positions, 36u * (size_t)r->num_triangles);
    RH(r->uvs, 24u * (size_t)r->num_triangles);
    RH(r->colors, 12u * (size_t)r->num_triangles);
    for (i = 0; i < r->num_draws; i++) {
        uint32_t k, w[5] = { r->draws[i].prim, r->draws[i].first, r->draws[i].count, (uint32_t)r->draws[i].archive, (uint32_t)r->draws[i].texture };
        for (k = 0; k < 5u; k++) {
            h = (h ^ w[k]) * 16777619u;
        }
    }
    RH(r->sprites, sizeof r->sprites[0] * (size_t)r->num_sprites);
#undef RH
    return h;
}

/* how many pairs of STAND (from the third) a and b draw differently */
static int looks_run(const kspec *k, uint32_t held_a, int64_t g_a, uint32_t held_b, int64_t g_b, uint32_t *tri_a, uint32_t *tri_b)
{
    int body_only = k->kind == SA1_CHAR_BIG;
    sa1_character_id a = mk(k, held_a, g_a), b = mk(k, held_b, g_b);
    int p, differ = 0;
    if (!a || !b) {
        sa1_character_destroy(g_ctx, a);
        sa1_character_destroy(g_ctx, b);
        return -1;
    }
    for (p = 0; p < 90; p++) {
        uint32_t ha, hb;
        if (tick(a, 0, p) || tick(b, 0, p)) {
            differ = -1;
            break;
        }
        ha = render_hash(a, tri_a, body_only);
        hb = render_hash(b, tri_b, body_only);
        if (p >= 2 && (ha != hb || (!body_only && *tri_a != *tri_b))) {
            if (differ == 0 && g_verbose) {
                /* which draws differ, the first time (--verbose) */
                const sa1_render *ra = NULL, *rb = NULL;
                uint32_t i, shown = 0;
                sa1_character_render(g_ctx, a, 1.0f, &ra);
                sa1_character_render(g_ctx, b, 1.0f, &rb);
                for (i = 0; ra && rb && i < ra->num_draws && i < rb->num_draws && shown < 8u; i++) {
                    const sa1_draw *da = &ra->draws[i], *db = &rb->draws[i];
                    int pd = da->count == db->count && da->first == db->first &&
                             memcmp(ra->positions + 9u * da->first, rb->positions + 9u * db->first, 36u * da->count) != 0;
                    if (da->prim != db->prim || da->count != db->count || da->archive != db->archive || da->texture != db->texture || pd) {
                        printf("    pair %d draw %u: prim %u/%u count %u/%u archive %d/%d texture %d/%d flags %04X/%04X source %u/%u%s\n", p, (unsigned)i,
                               (unsigned)da->prim, (unsigned)db->prim, (unsigned)da->count, (unsigned)db->count, (int)da->archive, (int)db->archive,
                               (int)da->texture, (int)db->texture, (unsigned)da->flags, (unsigned)db->flags, (unsigned)da->source, (unsigned)db->source,
                               pd ? " positions differ" : "");
                        shown++;
                    }
                }
                if (ra && rb) {
                    printf("    pair %d: %u / %u draws, %u / %u sprites, %u / %u line vertices\n", p, (unsigned)ra->num_draws, (unsigned)rb->num_draws,
                           (unsigned)ra->num_sprites, (unsigned)rb->num_sprites, (unsigned)ra->num_line_vertices, (unsigned)rb->num_line_vertices);
                }
            }
            differ++;
        }
    }
    sa1_character_destroy(g_ctx, a);
    sa1_character_destroy(g_ctx, b);
    return differ;
}

static void test_u2_u3(void)
{
    int i, j;
    printf("U2, U3 looks: the parts the game ships, picked by the display set\n");
    for (i = 0; i < 6; i++) {
        const kspec *k = &k_kinds[i];
        uint32_t allh = (1u << k->n) - 1u, ta = 0, tb = 0, tc = 0, td = 0, tp = 0, th = 0;
        int d1 = looks_run(k, allh, (int64_t)ON, 0u, -1, &ta, &tb);
        int d2 = looks_run(k, 0u, (int64_t)(ON | all_bits(k)), allh, -1, &tc, &td);
        int d0 = looks_run(k, 0u, -1, allh, -1, &tp, &th);
        printf("  info  %s at a stand: %u triangles without the upgrades, %u with them (%d of 88 pairs differ)%s\n", k->name, (unsigned)tp, (unsigned)th, d0,
               k->kind == SA1_CHAR_BIG ? "; Big's looks compare the draws that are not effects: the line and the lure on it follow the real rod bit and the lures shown" : "");
        CHECK(d1 == 0 && d2 == 0, "U2 %s: held + none shown draws as none held (%d of 88 pairs differ, %u / %u triangles); none held + all shown as held (%d, %u / %u)",
              k->name, d1, (unsigned)ta, (unsigned)tb, d2, (unsigned)tc, (unsigned)td);
        for (j = 0; j < k->n; j++) {
            int e1, e2;
            if (k->bit[j] >= SA1_UPGRADE_LURE_1) {
                continue;   /* the lures: U5 */
            }
            e1 = looks_run(k, 0u, (int64_t)(ON | k->bit[j]), 1u << j, -1, &ta, &tb);
            e2 = looks_run(k, allh, (int64_t)(ON | (all_bits(k) & ~k->bit[j])), allh & ~(1u << j), -1, &tc, &td);
            CHECK(e1 == 0 && e2 == 0, "U3 %s 0x%05X shown alone without it draws as the world holding only it (%d pairs differ); hidden alone with all, as the world without it (%d)",
                  k->name, (unsigned)k->bit[j], e1, e2);
        }
    }
}

/* ---- U5: the lures ------------------------------------------------------------------------------------------------------------------------------ */
static const uint8_t *block_of(const sa1g_world *w, uint32_t tag)
{
    size_t off;
    if (w == NULL || w->ext == NULL || w->pcdata_size > w->ext_used) {
        return NULL;
    }
    for (off = 0; off + 16u <= w->pcdata_size; off += 4u) {
        uint32_t a, b;
        memcpy(&a, w->ext + off, 4);
        memcpy(&b, w->ext + off + 4, 4);
        if (a == 0x44475055u && b == tag) {
            return w->ext + off;
        }
    }
    return NULL;
}

static void test_u5(void)
{
    static const uint32_t lure_tags[3] = { 0x32373745u, 0x31383645u, 0x34304145u };
    const kspec *k = &k_kinds[5];
    uint32_t held = 0x3Cu, bad = 0, d, i, ok_blocks = 0;
    sa1_character_id id;
    char b[128];
    printf("U5 the lures\n");
    id = mk(k, held, (int64_t)(ON | SA1_UPGRADE_LURE_1));
    if (id) {
        const sa1g_world *w = sa1_character_world(g_ctx, id);
        for (i = 0; i < 3u; i++) {
            const uint8_t *p = block_of(w, lure_tags[i]);
            uint32_t on = 0, shown = 0;
            if (p != NULL) {
                memcpy(&on, p + 8, 4);
                memcpy(&shown, p + 12, 4);
                ok_blocks += on == 1u && shown == SA1_UPGRADE_LURE_1;
            }
        }
        sa1_character_destroy(g_ctx, id);
    }
    CHECK(ok_blocks == 3u, "U5 the four lure flags and one lure shown: the blocks E772, E681, EA04 say on 1, shown 0x10000 (%u of 3): the cast lure is lure 1 (D_8C565FC4[1])", (unsigned)ok_blocks);
    d = keeps_run(k, held, -1, held, (int64_t)(ON | SA1_UPGRADE_LURE_1), 400, &bad, NULL);
    CHECK(d != 0xFFFFFFFFu && bad == 0u, "U5 B_8C78C5D8 (func_8C0E6E80's count), the state and the shapes the game's in every pair with one lure shown: %u of 400 pairs differ (%s)",
          (unsigned)bad, regions(d & ~R_FEET, b, sizeof b));
}

/* ---- U6: run time -------------------------------------------------------------------------------------------------------------------------------- */
static void test_u6(void)
{
    static const int64_t vals[4] = { (int64_t)(ON | 0x3u), (int64_t)ON, 0, (int64_t)(ON | 0x2u) };
    const kspec *k = &k_kinds[0];
    sa1_character_id id = mk(k, 0x3u, -1);
    int p, faults = 0, set_bad = 0;
    sa1_status st;
    const char *err;
    printf("U6 run time\n");
    if (!id) {
        CHECK(0, "U6 the world");
        return;
    }
    for (p = 0; p < 300; p++) {
        if (p % 30 == 0) {
            set_bad += sa1_character_set_game_option(g_ctx, id, SA1_GOPT_UPGRADE_MODELS, (int32_t)(uint32_t)vals[(p / 30) % 4]) != SA1_OK;
        }
        faults += tick(id, 1, p) != 0;
    }
    CHECK(faults == 0 && set_bad == 0, "U6 toggled every 30 pairs for 300 pairs: %d faults, %d refusals", faults, set_bad);
    CHECK(sa1_character_set_game_option(g_ctx, id, SA1_GOPT_UPGRADE_MODELS, 0x3) == SA1_ERR_INVALID_ARGUMENT &&
          sa1_character_set_game_option(g_ctx, id, SA1_GOPT_UPGRADE_MODELS, (int32_t)(ON | 0x2000u)) == SA1_ERR_INVALID_ARGUMENT &&
          sa1_character_set_game_option(g_ctx, id, 6u, 1) == SA1_ERR_INVALID_ARGUMENT && sa1_character_set_game_option(g_ctx, id, SA1_GOPT_UPGRADE_MODELS, 0) == SA1_OK,
          "U6 bits without SA1_UPGRADE_MODELS_ON or outside SA1_UPGRADE_MODELS_ALL, option 6: SA1_ERR_INVALID_ARGUMENT; 0: SA1_OK");
    {
        /* a build without the A77B block: its magic word put out of the way in this world's pcdata, then back */
        struct sa1g_world *w = sa1_character_world(g_ctx, id);
        uint8_t *p8 = (uint8_t *)(uintptr_t)block_of(w, 0x42373741u);
        uint32_t keep = 0, junk = 0x58585858u;
        if (p8 != NULL) {
            memcpy(&keep, p8 + 4, 4);
            memcpy(p8 + 4, &junk, 4);
        }
        st = sa1_character_set_game_option(g_ctx, id, SA1_GOPT_UPGRADE_MODELS, (int32_t)(ON | 0x2u));
        err = sa1_last_error(g_ctx);
        CHECK(p8 != NULL && st == SA1_ERR_NOT_IMPLEMENTED && err != NULL && strstr(err, "A77B") != NULL, "U6 a missing block: %s, \"%s\"", sa1_status_string(st), err ? err : "");
        CHECK(sa1_character_set_game_option(g_ctx, id, SA1_GOPT_UPGRADE_MODELS, 0) == SA1_OK, "U6 0 with the block missing: SA1_OK");
        if (p8 != NULL) {
            memcpy(p8 + 4, &keep, 4);
        }
        CHECK(sa1_character_set_game_option(g_ctx, id, SA1_GOPT_UPGRADE_MODELS, (int32_t)(ON | 0x2u)) == SA1_OK, "U6 the block back: SA1_OK");
    }
    sa1_character_destroy(g_ctx, id);
}

/* U-SHC's part here: the nine blocks are in every character world of this build, all 0 (the game) until a host sets the option */
static void test_blocks(void)
{
    static const uint32_t tags[9] = { 0x42373741u, 0x34313642u, 0x32433443u, 0x30314144u, 0x43414643u, 0x41464644u, 0x32373745u, 0x31383645u, 0x34304145u };
    sa1_character_id id = mk(&k_kinds[0], 0u, -1);
    uint32_t i, found = 0, zero = 0, set = 0;
    if (!id) {
        CHECK(0, "the blocks: the world");
        return;
    }
    for (i = 0; i < 9u; i++) {
        const uint8_t *p = block_of(sa1_character_world(g_ctx, id), tags[i]);
        uint32_t on = 1, shown = 1;
        if (p != NULL) {
            found++;
            memcpy(&on, p + 8, 4);
            memcpy(&shown, p + 12, 4);
            zero += on == 0u && shown == 0u;
        }
    }
    sa1_character_set_game_option(g_ctx, id, SA1_GOPT_UPGRADE_MODELS, (int32_t)(ON | SA1_UPGRADE_MODELS_ALL));
    for (i = 0; i < 9u; i++) {
        const uint8_t *p = block_of(sa1_character_world(g_ctx, id), tags[i]);
        uint32_t on = 0, shown = 0;
        if (p != NULL) {
            memcpy(&on, p + 8, 4);
            memcpy(&shown, p + 12, 4);
            set += on == 1u && shown == SA1_UPGRADE_MODELS_ALL;
        }
    }
    CHECK(found == 9u && zero == 9u && set == 9u, "the nine SA1_UPGDISP blocks in a character world: %u found, %u at 0 (the game) before the option, %u written by it", (unsigned)found,
          (unsigned)zero, (unsigned)set);
    sa1_character_destroy(g_ctx, id);
}

int main(int argc, char **argv)
{
    const char *gdi = NULL;
    sa1_host h;
    sa1_coords co;
    sa1_load_options lo;
    sa1_collision_tri t[2];
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
        printf("usage: upgdisp_test [--verbose] GDI\n");
        return 2;
    }
    sa1_host_defaults(&h);
    h.log = log_cb;
    h.log_level = SA1_LOG_WARN;
    if (sa1_init(&h, &g_ctx) != SA1_OK) {
        return 1;
    }
    memset(&co, 0, sizeof co);
    co.struct_size = sizeof co;
    sa1_coords_preset(SA1_COORDS_PRESET_NATIVE, &co);
    sa1_set_coords(g_ctx, &co);
    memset(&lo, 0, sizeof lo);
    lo.struct_size = sizeof lo;
    lo.characters = (1u << SA1_CHAR_SONIC) | (1u << SA1_CHAR_TAILS) | (1u << SA1_CHAR_KNUCKLES) | (1u << SA1_CHAR_AMY) | (1u << SA1_CHAR_GAMMA) | (1u << SA1_CHAR_BIG);
    lo.flags = SA1_LOAD_DEFAULT;
    memset(t, 0, sizeof t);
    {
        float p[4][3] = { { -6000, 0, -6000 }, { -6000, 0, 6000 }, { 6000, 0, 6000 }, { 6000, 0, -6000 } };
        memcpy(t[0].v[0], p[0], 12);
        memcpy(t[0].v[1], p[1], 12);
        memcpy(t[0].v[2], p[2], 12);
        memcpy(t[1].v[0], p[0], 12);
        memcpy(t[1].v[1], p[2], 12);
        memcpy(t[1].v[2], p[3], 12);
        t[0].surface = t[1].surface = SA1_SURFACE_SOLID;
    }
    if (SA1_FAILED(sa1_load_disc(g_ctx, gdi, &lo)) || sa1_collision_set_static(g_ctx, t, 2) != SA1_OK) {
        printf("context: %s\n", sa1_last_error(g_ctx));
        sa1_shutdown(g_ctx);
        return 1;
    }
    printf("upgdisp_test: libsa1 %s, %d-bit\n", sa1_version_string(), (int)(sizeof(void *) * 8));
    test_blocks();
    test_u6();
    test_u5();
    test_u1();
    test_u2_u3();
    printf("DETERMINISM upgdisp %08X\n", (unsigned)g_hash);
    printf("upgdisp_test: %d ok, %d failed\n", g_ok, g_fail);
    sa1_shutdown(g_ctx);
    return g_fail ? 1 : 0;
}