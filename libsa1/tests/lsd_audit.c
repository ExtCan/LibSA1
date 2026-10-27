/*
 * lsd_audit.c - round 4c regression (sadc docs/research/lightspeed_visuals.md 5.3 / 6.3, plan gsa1_rounds_plan.md 6.1):
 * all six characters with all their upgrades on Emerald Coast act 1's own collision (DISC, no stage tasks) run 27 move
 * scripts; after every pair the render output (sa1_character_render, alpha 1: the pair as drawn) is dumped:
 *
 *   D|char|script|pair|m<k>|src S n N flags 0xF blend S/D mat 0xM arc A tex T col0 r,g,b,a
 *        every triangle draw that is not a 3D polygon (the model draws: character renderer, Ninja model / motion
 *        draws, CPU pose), k = its index among them in the pair;
 *   P|char|script|pair|r<k>|attr 0xA fl 0xF tl 0xT|w W h H   (or "|-" when the record emitted no sprite)
 *        every 3D sprite record of the pair's capture (k = its index among them) with the size of the sprite it
 *        emitted, in SA1 units;
 *   Q|char|script|pair|polygons N tris N lines N segs N      the effect primitives (information);
 *   R|char|script|...                                        a summary per script (information).
 *
 * Round 0.9.2a (FX3, sadc docs/research/gsa1_091b_itembox.md 6): Sonic's scripts "barrier", "invincible" and "magnetic"
 * add the item box power-up models (Ninja's node draw 8C621574, rt_draw.c 5b): a script group "<item>i" makes an air box
 * with that item at the character through the game's own spawner 8C05D9C0 (no pair runs for it); the box breaks on him
 * about 12 pairs later and the item's display task draws from then on.
 *
 * build_lsd_audit.py compares two dumps: one made with the library before a change (the reference) and one after.
 * Round 4c's rule: among the D lines only the Light Speed aura's (Sonic, "charge", material 0x26182400) may differ,
 * and a P line may change only for an unscaled sprite (no NJD_SPRITE_SCALE) or a scaled one of zero size that is no
 * longer emitted.
 *
 * Which sprite a record emitted: the emitter emits one sprite per 3D sprite record with a payload, in record order,
 * except (since round 4c) quads of zero extent. The dump tells the two libraries apart by the count: if the render
 * holds as many sprites as there are records with a payload, every record emitted one; if it holds as many as the
 * records of non-zero extent, the zero ones emitted none; otherwise the pair's P lines say "unmapped" (none expected:
 * a sprite parked beyond the clip radius would do it).
 *
 *   lsd_audit GDI [--out FILE]
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libsa1.h"
#include "sa1_internal.h"
#include "sa1_api_world.h"
#include "sa1_world.h"
#include "sa1_capture.h"

static FILE *g_out;

static void log_cb(void *user, int32_t level, const char *msg)
{
    (void)user;
    if (level >= SA1_LOG_ERROR)
        fprintf(stderr, "    [libsa1] %s\n", msg);
}

typedef struct cs_def {
    int32_t kind;
    uint32_t flags[6];           /* event flags set before the first pair (G-SA1's set_event_flags): the upgrades */
    uint32_t nflags;
    const char *scripts[8];      /* "name:<pairs><cmd>...": n nothing, f forward, a action, b action + forward, j jump,
                                    J jump + forward, x action + jump; "<item>i": an air box with that item (round 0.9.2a) */
} cs_def;

static const cs_def k_chars[] = {
    { SA1_CHAR_SONIC, { 0x8D, 0x8E, 0x9F }, 3,
      { "charge:15n70a20n", "run:15n40f", "jumpdash:15n10f1J3n1J20n", "spindash:15n20a20n", "air:15n1j2n1x15n",
        "barrier:15n5i45n", "invincible:15n1i45n", "magnetic:15n8i45n" } },
    { SA1_CHAR_TAILS, { 0xCD, 0xDB }, 2,
      { "fly:15n1j2n60j20n", "tail:15n1a15n", "runfly:15n20f1J2n40J", "tailchain:15n1a2n1a2n1a2n1a20n", NULL } },
    { SA1_CHAR_KNUCKLES, { 0x119, 0x11A }, 2,
      { "punch:15n1a4n1a4n1a20n", "heat:15n60a20n", "glide:15n1j2n60j20n", "dig:15n1x40n", "runpunch:15n20f1b20n",
        NULL } },
    { SA1_CHAR_AMY, { 0x152, 0x15E }, 2,
      { "hammer:15n1a20n", "jumpatk:15n1j2n1a20n", "runhammer:15n20f1b20n", "spin:15n60a20n", NULL } },
    { SA1_CHAR_GAMMA, { 0x189, 0x18A }, 2,
      { "lockon:15n60a20n", "hover:15n1j2n60j20n", "shoot:15n20a1n20n", NULL } },
    { SA1_CHAR_BIG, { 0x1D0, 0x1D1, 0x1DE, 0x1DF, 0x1E0, 0x1E1 }, 6,
      { "cast:15n1a60n", "aim:15n30a40n", "reel:15n1a30n30a30n", NULL } },
};

static sa1_context *make_ctx(const char *gdi, int32_t kind)
{
    sa1_context *ctx = NULL;
    sa1_host h;
    sa1_load_options lo;
    sa1_collision_static col;
    sa1_host_defaults(&h);
    h.log = log_cb;
    h.log_level = SA1_LOG_ERROR;
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
        fprintf(stderr, "context: %s\n", sa1_last_error(ctx));
        sa1_shutdown(ctx);
        return NULL;
    }
    return ctx;
}

static double col_norm(const double m[12], int c)
{
    return sqrt(m[c] * m[c] + m[4 + c] * m[4 + c] + m[8 + c] * m[8 + c]);
}

/* the sprite records of the pair and the sprites they emitted */
#define MAXREC 1024u
static void dump_sprites(sa1_context *ctx, sa1_character_id id, const char *who, const char *script, uint32_t tick,
                         const sa1_render *r, uint32_t *unmapped)
{
    sa1_capture *c = sa1_character_capture(ctx, id);
    const sa1_cap_rec *recs = NULL;
    uint32_t n = c ? sa1_capture_records(c, &recs) : 0u, i, nspr = 0, npay = 0, nnz = 0, j = 0;
    static uint8_t has[MAXREC], zero[MAXREC];
    int mode;
    float k;
    sa1_coords co;
    memset(&co, 0, sizeof co);
    co.struct_size = sizeof co;
    sa1_get_coords(ctx, &co);
    k = co.scale > 0.0f ? co.scale : 1.0f;
    for (i = 0; i < n && i < MAXREC; i++) {
        const sa1_cap_rec *rc = &recs[i];
        const uint8_t *p;
        double m[12], sx, sy, w, h;
        has[i] = zero[i] = 0;
        if (rc->kind != SA1_CAP_SPRITE3D)
            continue;
        nspr++;
        p = sa1_capture_payload(c, rc, 0x34u);
        if (!p)
            continue;
        has[i] = 1;
        npay++;
        sa1_capture_world_matrix(rc, m);
        sx = sa1_rdf32(p + 0x0C);
        sy = sa1_rdf32(p + 0x10);
        if (rc->attr & 0x10u) {
            if (rc->flags & SA1_CAPF_GAME_SPRITE)
                sy = sx;
        } else {
            sx *= col_norm(m, 0);
            sy *= col_norm(m, 1);
        }
        w = (double)(int16_t)sa1_rd16(p + 0x20) * sx;
        h = (double)(int16_t)sa1_rd16(p + 0x22) * sy;
        zero[i] = !(fabs(w) > 0.0) || !(fabs(h) > 0.0);
        nnz += !zero[i];
    }
    if (nspr == 0)
        return;
    mode = r && r->num_sprites == npay ? 1 : r && r->num_sprites == nnz ? 2 : 0;
    if (!mode)
        (*unmapped)++;
    for (i = 0, nspr = 0; i < n && i < MAXREC; i++) {
        const sa1_cap_rec *rc = &recs[i];
        if (rc->kind != SA1_CAP_SPRITE3D)
            continue;
        fprintf(g_out, "P|%s|%s|%u|r%u|attr 0x%02X fl 0x%X tl 0x%08X|", who, script, (unsigned)tick, (unsigned)nspr++,
                (unsigned)rc->attr, (unsigned)rc->flags, (unsigned)rc->texlist);
        if (!mode) {
            fprintf(g_out, "unmapped\n");
        } else if (!has[i] || (mode == 2 && zero[i])) {
            fprintf(g_out, "-\n");
        } else {
            const sa1_sprite *s = &r->sprites[j++];
            fprintf(g_out, "w %.4f h %.4f\n", (double)(s->rect[2] - s->rect[0]) / k, (double)(s->rect[3] - s->rect[1]) / k);
        }
    }
}

/* round 0.9.2a: an air box with item *user at the player, made by the game's spawner 8C05D9C0 (void (NJS_POINT3 *pos,
 * Sint32 item): what Knuckles' dig 8C0C8534 calls) inside the world */
static void spawn_box_cb(struct sa1g_world *w, void *user)
{
    sa1g_player_ptrs pp;
    uint8_t *twp;
    sa1g_world_player(w, &pp);
    twp = pp.twp ? (uint8_t *)sa1g_world_ptr(w, pp.twp) : NULL;
    if (twp)
        ((void (*)(void *, int32_t))sa1_fn(SA1_GFN_C(0x8C05D9C0u), 0u))(twp + 0x20, *(const int32_t *)user);
}

static void run_script(sa1_context *ctx, const cs_def *cd, const char *script, uint32_t *unmapped)
{
    char name[24];
    const char *p = strchr(script, ':') + 1, *who = sa1_character_name(cd->kind);
    sa1_character_desc d;
    sa1_character_id id = 0;
    sa1_input in;
    struct sa1g_world *w;
    uint32_t i, tick = 0, ndraw = 0, ntri = 0, nsprite = 0, nmodel = 0;
    double sprw = 0.0;
    size_t nl = (size_t)(strchr(script, ':') - script);
    if (nl >= sizeof name)
        nl = sizeof name - 1;
    memcpy(name, script, nl);
    name[nl] = 0;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.kind = cd->kind;
    d.flags = SA1_CREATE_WORLD | SA1_CREATE_START_TABLE;
    if (SA1_FAILED(sa1_character_create_ex(ctx, &d, &id))) {
        fprintf(g_out, "X|%s|%s|create: %s\n", who, name, sa1_last_error(ctx));
        return;
    }
    /* the upgrades: event flags, as G-SA1's set_event_flags writes them (one byte each from 0x8C799920) */
    w = sa1_character_world(ctx, id);
    for (i = 0; i < cd->nflags; i++) {
        uint32_t a = 0x8C799920u + cd->flags[i], word = a & ~3u, v = 0, sh = 8u * (a & 3u);
        if (w && !sa1g_world_rd32(w, word, &v))
            sa1g_world_wr32(w, word, (v & ~(0xFFu << sh)) | (1u << sh));
    }
    memset(&in, 0, sizeof in);
    in.struct_size = sizeof in;
    while (*p) {
        char *e;
        long cnt = strtol(p, &e, 10), k2;
        char cmd;
        if (e == p || !*e)
            break;
        cmd = *e;
        p = e + 1;
        if (cmd == 'i') {
            int32_t item = (int32_t)cnt;
            if (!w || sa1g_world_call(w, spawn_box_cb, &item) != SA1_OK) {
                fprintf(g_out, "X|%s|%s|%u|item box %d: %s\n", who, name, (unsigned)tick, (int)item, sa1_last_error(ctx));
                goto done;
            }
            continue;
        }
        in.stick_y = (cmd == 'f' || cmd == 'b' || cmd == 'J') ? 1.0f : 0.0f;
        in.buttons = ((cmd == 'a' || cmd == 'b' || cmd == 'x') ? SA1_BUTTON_ACTION : 0u) |
                     ((cmd == 'j' || cmd == 'J' || cmd == 'x') ? SA1_BUTTON_JUMP : 0u);
        for (k2 = 0; k2 < cnt; k2++, tick++) {
            const sa1_render *r = NULL;
            uint32_t q, m = 0, polys = 0, ptris = 0, lines = 0;
            if (sa1_character_tick(ctx, id, &in, 1.0f / 30.0f, NULL, NULL) != SA1_OK) {
                fprintf(g_out, "X|%s|%s|%u|tick: %s\n", who, name, (unsigned)tick, sa1_last_error(ctx));
                goto done;
            }
            if (sa1_character_render(ctx, id, 1.0f, &r) != SA1_OK)
                r = NULL;
            dump_sprites(ctx, id, who, name, tick, r, unmapped);
            if (!r)
                continue;
            for (q = 0; q < r->num_draws; q++) {
                const sa1_draw *dr = &r->draws[q];
                if (dr->prim == SA1_PRIM_LINES) {
                    lines++;
                    continue;
                }
                if (dr->prim != SA1_PRIM_TRIANGLES)
                    continue;
                if (dr->source == SA1_SOURCE_POLYGON) {
                    polys++;
                    ptris += dr->count;
                    continue;
                }
                fprintf(g_out, "D|%s|%s|%u|m%u|src %u n %u flags 0x%04X blend %u/%u mat 0x%08X arc %d tex %d col0 %u,%u,%u,%u\n",
                        who, name, (unsigned)tick, (unsigned)m++, (unsigned)dr->source, (unsigned)dr->count,
                        (unsigned)dr->flags, (unsigned)dr->blend_src, (unsigned)dr->blend_dst, (unsigned)dr->material,
                        (int)dr->archive, (int)dr->texture, r->colors[12u * dr->first], r->colors[12u * dr->first + 1u],
                        r->colors[12u * dr->first + 2u], r->colors[12u * dr->first + 3u]);
            }
            nmodel += m;
            if (polys || lines)
                fprintf(g_out, "Q|%s|%s|%u|polygons %u tris %u lines %u segs %u\n", who, name, (unsigned)tick,
                        (unsigned)polys, (unsigned)ptris, (unsigned)lines, (unsigned)(r->num_line_vertices / 2u));
            for (q = 0; q < r->num_sprites; q++)
                sprw += (double)(r->sprites[q].rect[2] - r->sprites[q].rect[0]);
            nsprite += r->num_sprites;
            ndraw += r->num_draws;
            ntri += r->num_triangles;
        }
    }
    {
        sa1_state_ex st;
        memset(&st, 0, sizeof st);
        st.struct_size = sizeof st;
        sa1_character_state_ex(ctx, id, &st);
        fprintf(g_out, "R|%s|%s|pairs %u equipment 0x%04X mode %d action %d|draws %u model draws %u tris %u sprites %u "
                "sprite-width-sum %.1f\n", who, name, (unsigned)tick, (unsigned)st.equipment, (int)st.mode,
                (int)st.action, (unsigned)ndraw, (unsigned)nmodel, (unsigned)ntri, (unsigned)nsprite, sprw);
    }
done:
    sa1_character_destroy(ctx, id);
}

int main(int argc, char **argv)
{
    const char *gdi = NULL, *out = NULL;
    uint32_t ci, si, nscripts = 0, unmapped = 0;
    int i;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--out") && i + 1 < argc)
            out = argv[++i];
        else if (!gdi)
            gdi = argv[i];
    }
    if (!gdi)
        gdi = getenv("SA1_TEST_GDI");
    if (!gdi) {
        fprintf(stderr, "usage: lsd_audit GDI [--out FILE]   (GDI or $SA1_TEST_GDI)\n");
        return 2;
    }
    g_out = out ? fopen(out, "w") : stdout;
    if (!g_out) {
        fprintf(stderr, "cannot write %s\n", out);
        return 2;
    }
    for (ci = 0; ci < sizeof k_chars / sizeof k_chars[0]; ci++) {
        const cs_def *cd = &k_chars[ci];
        sa1_context *ctx = make_ctx(gdi, cd->kind);
        if (!ctx) {
            fprintf(g_out, "X|%s|context\n", sa1_character_name(cd->kind));
            continue;
        }
        for (si = 0; si < 8 && cd->scripts[si]; si++, nscripts++)
            run_script(ctx, cd, cd->scripts[si], &unmapped);
        sa1_shutdown(ctx);
    }
    fprintf(g_out, "E|scripts %u|unmapped sprite pairs %u\n", (unsigned)nscripts, (unsigned)unmapped);
    if (out)
        fclose(g_out);
    fprintf(stderr, "lsd_audit: %u scripts, %u pairs with unmapped sprites\n", (unsigned)nscripts, (unsigned)unmapped);
    return 0;
}
