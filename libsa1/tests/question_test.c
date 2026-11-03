/*
 * question_test.c - the question mark of Character Select as a character (SA1_API_QUESTION, round 0.9.9; sadc docs/research/gsa1_099_plan.md 5.10, 6.3 task B, playable099.md 5, 7.3).
 * A flat GRID ground in the Source preset (4 HU per SA1 unit, z up). The world's memory is read through the world core's header (Q1), everything else through the public API.
 *
 *   py -3.12 tests/build_question.py --run --gdi GDI [--debug] [--target x86_64-windows]
 *   question_test GDI
 *
 * What a host needs to know (the header's section says the rest):
 *   - sa1_character_create_ex(kind 8, SA1_CREATE_QUESTION | SA1_CREATE_WORLD or lazily): the world is byte for byte kind 8's: same state, same events, same sounds, the same arena. Only
 *     sa1_character_render, _geometry and the mesh differ: the game's objects, a board, the blob shadow, and the "?" (692 triangles, one draw, archive SA1_ARCHIVE_QUESTION, texture 7,
 *     material 9461A400, flags PALETTE_LIT + ENV_MAP) from before the first pair on. No body, no aura, no effects, never the CPU pose.
 *   - Its foot stands on the ground (13.1 units tall, 52.4 HU; the hitbox stays Super Sonic's). It spins 0x2D8 BAMS a step as on the menu, the same way round as sa1_frontend_render's "?".
 *   - SA1_COPT_QUESTION_SPIN 0: the model's front (+z) along the way the character faces; a chase camera sees its back. Takes effect at the next pair. On another character: SA1_ERR_UNSUPPORTED.
 *   - sa1_texture_get(8, SA1_ARCHIVE_QUESTION, n): AVA_CHSEL_E's 11 textures by their names, loaded by the first create with the flag, SA1_ERR_NOT_LOADED before it, not counted in num_archives. Another kind: NOT_FOUND.
 *   - The flag on another kind or with SA1_CREATE_PUPPET: SA1_ERR_INVALID_ARGUMENT. The optimised archive answers "the question mark is not built yet" until SA1Q_BUILT is 1.
 *   - A follower can follow a "?".
 *
 * Cases: Q1 (a control first: two plain kind 8 worlds; review R99.12) state, events, the whole arena; Q2 the render, before the first pair too; Q3 a ring stays; Q4 the spin, its sign against the menu,
 * spin off; Q5 the archive; Q6 refusals, DETERMINISM (build_board.py's main compares the lines); Q7 every allocation freed; Q8 a Sonic follower behind a "?".
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libsa1.h"
#include "sa1_api_world.h"
#include "sa1_world.h"

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

static uint32_t fnv(uint32_t h, const void *p, size_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    size_t i;
    for (i = 0; i < n; i++) {
        h = (h ^ b[i]) * 16777619u;
    }
    return h;
}

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
    for (x = -16384.0f; x < 16383.0f; x += 2048.0f) {
        for (y = -16384.0f; y < 16383.0f; y += 2048.0f) {
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

static sa1_status create_ex(sa1_context *ctx, uint32_t flags, float x, float y, float z, uint32_t super_flags, int32_t rings, sa1_character_id *id)
{
    sa1_character_desc d;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.kind = SA1_CHAR_SUPER_SONIC;
    d.flags = flags;
    d.position[0] = x;
    d.position[1] = y;
    d.position[2] = z;
    d.super_flags = super_flags;
    d.start_rings = rings;
    *id = 0;
    return sa1_character_create_ex(ctx, &d, id);
}

static sa1_status tick(sa1_character_id id, float sx, float sy, uint32_t buttons, float yaw)
{
    sa1_input in;
    memset(&in, 0, sizeof in);
    in.struct_size = sizeof in;
    in.stick_x = sx;
    in.stick_y = sy;
    in.buttons = buttons;
    in.camera_yaw = yaw;
    return sa1_character_tick(g_ctx, id, &in, 1.0f / 30.0f, NULL, NULL);
}

static void state_of(sa1_character_id id, sa1_state_ex *x)
{
    memset(x, 0, sizeof *x);
    x->struct_size = sizeof *x;
    sa1_character_state_ex(g_ctx, id, x);
}

static uint32_t count_faults(void)
{
    sa1_event ev[64];
    uint32_t n, i, bad = 0;
    do {
        n = 0;
        if (sa1_poll_events(g_ctx, ev, 64, &n) != SA1_OK) {
            break;
        }
        for (i = 0; i < n; i++) {
            bad += ev[i].type == SA1_EVENT_FAULT || ev[i].type == SA1_EVENT_UNIMPLEMENTED;
        }
    } while (n == 64);
    return bad;
}

/* the "?"'s draw of a render, or NULL */
static const sa1_draw *qdraw_of(const sa1_render *r)
{
    uint32_t i;
    for (i = 0; i < r->num_draws; i++) {
        if (r->draws[i].archive == (int32_t)SA1_ARCHIVE_QUESTION) {
            return &r->draws[i];
        }
    }
    return NULL;
}

/* the horizontal angle (BAMS, Source axes) of the vector between two fixed corners of a run of triangles: independent of where the model stands */
static int vec_angle(const float *pos, uint32_t first, uint32_t count, double *out)
{
    const float *a, *b;
    uint32_t k;
    double best = 0.0;
    if (count < 2u) {
        return 0;
    }
    a = pos + 9u * first;
    b = a;
    /* a fixed choice: the first corner of the run and the first corner of triangle count / 2, count / 3 ... that lies 20 HU or more away: the same index for the same model */
    for (k = 2; k < 8u; k++) {
        const float *c = pos + 9u * (first + count / k);
        double dx = (double)c[0] - a[0], dy = (double)c[1] - a[1], l = dx * dx + dy * dy;
        if (l > best) {
            best = l;
            b = c;
        }
    }
    if (best < 1.0) {
        return 0;
    }
    *out = atan2((double)b[1] - a[1], (double)b[0] - a[0]) * (32768.0 / 3.14159265358979323846);
    return 1;
}

static double bams_diff(double a, double b)
{
    double d = fmod(a - b, 65536.0);
    if (d > 32768.0) {
        d -= 65536.0;
    }
    if (d < -32768.0) {
        d += 65536.0;
    }
    return d;
}

/* ---- Q1 --------------------------------------------------------------------------------------------------------------------------------- */
#define Q1_PAIRS 2000
typedef struct q1_out {
    sa1_state_ex *s;
    sa1_event *e;
    uint32_t ne, cap;
    uint8_t *arena;
} q1_out;

static uint32_t g_ring_rec[4];

static void q1_input(int p, sa1_input *in)
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
    } else if (c < 152) {
        in->stick_y = 1.0f;
        in->buttons = SA1_BUTTON_JUMP;
    } else if (c < 175) {
        in->stick_y = 1.0f;
        in->buttons = SA1_BUTTON_JUMP;
    } else if (c < 177) {
        in->stick_y = 1.0f;
        in->buttons = SA1_BUTTON_ACTION;
    } else if (c < 230) {
        in->stick_y = 1.0f;
    } else if (c < 300) {
        in->stick_x = 1.0f;
    }
}

static int q1_run(uint32_t flags, uint32_t super_flags, int32_t rings, int pairs, q1_out *o)
{
    sa1_character_id id = 0;
    sa1_status st;
    int p;
    uint32_t k;
    for (k = 0; k < 4u; k++) {
        sa1_set_state(g_ctx, g_ring_rec[k], SA1_SET_STATE_PRESENT, 0u);
    }
    st = create_ex(g_ctx, SA1_CREATE_WORLD | flags, 0.0f, 0.0f, 40.0f, super_flags, rings, &id);
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
        q1_input(p, &in);
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
        /* the mesh window open, as in play */
        sa1_character_render(g_ctx, id, 0.5f, &r);
    }
    memcpy(o->arena, sa1_character_world(g_ctx, id)->ram, SA1G_ARENA_SIZE);
    sa1_character_destroy(g_ctx, id);
    return 0;
}

static void q1_out_init(q1_out *o, int pairs)
{
    memset(o, 0, sizeof *o);
    o->s = (sa1_state_ex *)calloc((size_t)pairs, sizeof *o->s);
    o->arena = (uint8_t *)malloc(SA1G_ARENA_SIZE);
}

static void q1_out_free(q1_out *o)
{
    free(o->s);
    free(o->e);
    free(o->arena);
}

static uint32_t cmp_states(const q1_out *a, const q1_out *b, int pairs, int *first)
{
    uint32_t n = 0;
    int p;
    *first = -1;
    for (p = 0; p < pairs; p++) {
        if (memcmp(&a->s[p], &b->s[p], sizeof a->s[p]) != 0) {
            if (*first < 0) {
                *first = p;
            }
            n++;
        }
    }
    return n;
}

static uint32_t cmp_events(const q1_out *a, const q1_out *b)
{
    uint32_t i, n = a->ne != b->ne;
    for (i = 0; i < a->ne && i < b->ne; i++) {
        n += memcmp(&a->e[i], &b->e[i], sizeof a->e[i]) != 0;
    }
    return n;
}

static void test_q1_config(const char *what, uint32_t flags, uint32_t super_flags, int32_t rings, int pairs)
{
    q1_out a, b, c;
    uint8_t *mask = (uint8_t *)calloc(SA1G_ARENA_SIZE, 1);
    uint32_t i, ctl = 0, flg = 0, other = 0, ev_max = 0;
    int first, ok;
    int mode_seen = 0, rings_seen = 0;
    uint32_t sd;
    printf("Q1 %s: %d pairs with hits and rings: state, events, the whole arena\n", what, pairs);
    q1_out_init(&a, pairs);
    q1_out_init(&b, pairs);
    q1_out_init(&c, pairs);
    ok = q1_run(flags, super_flags, rings, pairs, &a) == 0 && q1_run(flags, super_flags, rings, pairs, &b) == 0 && q1_run(flags | SA1_CREATE_QUESTION, super_flags, rings, pairs, &c) == 0;
    CHECK(ok, "Q1 the three worlds ran");
    if (!ok) {
        goto done;
    }
    for (i = 1; i < (uint32_t)pairs; i++) {
        mode_seen += a.s[i].mode != a.s[i - 1].mode;
        rings_seen += a.s[i].rings != a.s[i - 1].rings;
    }
    ev_max = a.ne;
    printf("  info  the plain world: %d mode changes, %d ring changes, %u events\n", mode_seen, rings_seen, (unsigned)ev_max);
    sd = cmp_states(&a, &b, pairs, &first);
    CHECK(sd == 0u && cmp_events(&a, &b) == 0u, "Q1 control, two plain worlds: %u state differences (first at pair %d), %u event differences", (unsigned)sd, first, (unsigned)cmp_events(&a, &b));
    sd = cmp_states(&a, &c, pairs, &first);
    CHECK(sd == 0u && cmp_events(&a, &c) == 0u, "Q1 the flag: %u state differences (first at pair %d), %u event differences", (unsigned)sd, first, (unsigned)cmp_events(&a, &c));
    for (i = 0; i < SA1G_ARENA_SIZE; i++) {
        if (a.arena[i] != b.arena[i]) {
            mask[i] = 1;
            if (ctl < 16u) {
                printf("  info  control difference at guest %08X: %02X, %02X\n", (unsigned)(0x8C000000u + i), a.arena[i], b.arena[i]);
            }
            ctl++;
        }
    }
    for (i = 0; i < SA1G_ARENA_SIZE; i++) {
        if (a.arena[i] != c.arena[i]) {
            flg++;
            if (!mask[i]) {
                if (other < 16u) {
                    printf("  FAIL  the flag's world differs at guest %08X: %02X, %02X; the control does not\n", (unsigned)(0x8C000000u + i), a.arena[i], c.arena[i]);
                }
                other++;
            }
        }
    }
    CHECK(other == 0u, "Q1 the arena, 16 MB: the control differs at %u bytes, the flag's world at %u, %u of them where the control does not (0)", (unsigned)ctl, (unsigned)flg, (unsigned)other);
done:
    q1_out_free(&a);
    q1_out_free(&b);
    q1_out_free(&c);
    free(mask);
}

/* ---- Q2, Q3 ----------------------------------------------------------------------------------------------------------------------------- */
typedef struct q2_tally {
    uint32_t frames, bad_tris, bad_archive, additive, sprites, blob_frames, tex7;
} q2_tally;

static void q2_check(const sa1_render *r, q2_tally *t)
{
    uint32_t i, tris = 0;
    t->frames++;
    for (i = 0; i < r->num_draws; i++) {
        const sa1_draw *d = &r->draws[i];
        if (d->archive == (int32_t)SA1_ARCHIVE_QUESTION) {
            tris += d->count;
            t->tex7 += d->texture == 7;
            t->bad_archive += d->material != 0x9461A400u;
        } else if (d->flags & SA1_DRAW_SHADOW) {
            t->blob_frames++;
        } else if (!(d->flags & (SA1_DRAW_OBJECT | SA1_DRAW_BOARD))) {
            if (t->bad_archive < 8u) {
                printf("    a draw that is none of ours: archive %d texture %d flags %X count %u\n", (int)d->archive, (int)d->texture, (unsigned)d->flags, (unsigned)d->count);
            }
            t->bad_archive++;
        }
        t->additive += (d->flags & SA1_DRAW_ADDITIVE) != 0u;
    }
    t->bad_tris += tris != 692u;
    t->sprites += r->num_sprites != 0u;
}

static void test_q2(void)
{
    sa1_character_id id = 0, pid = 0;
    const sa1_render *r = NULL;
    const sa1_geometry *g = NULL;
    sa1_character_info info;
    q2_tally t;
    sa1_status st;
    int p;
    uint32_t plain_body = 0, plain_add = 0;
    printf("Q2 the render, 500 pairs\n");
    memset(&t, 0, sizeof t);
    st = create_ex(g_ctx, SA1_CREATE_WORLD | SA1_CREATE_QUESTION, 0.0f, 0.0f, 40.0f, 0u, -1, &id);
    CHECK(st == SA1_OK, "Q2 create: %s", sa1_status_string(st));
    if (st != SA1_OK) {
        return;
    }
    st = sa1_character_render(g_ctx, id, 1.0f, &r);
    {
        const sa1_draw *qd = st == SA1_OK && r != NULL ? qdraw_of(r) : NULL;
        CHECK(st == SA1_OK && r != NULL && r->num_triangles == 692u && r->num_draws == 1u && qd != NULL && qd->texture == 7, "Q2 before the first pair: %s, %u triangles, %u draws, archive %d, texture %d: never the CPU pose",
              sa1_status_string(st), r ? (unsigned)r->num_triangles : 0u, r ? (unsigned)r->num_draws : 0u, qd ? (int)qd->archive : -1, qd ? (int)qd->texture : -1);
    }
    st = sa1_character_geometry(g_ctx, id, &g);
    CHECK(st == SA1_OK && g != NULL && g->num_triangles == 692u, "Q2 sa1_character_geometry before the first pair: %u triangles", g ? (unsigned)g->num_triangles : 0u);
    for (p = 0; p < 500; p++) {
        sa1_input in;
        q1_input(p, &in);
        st = sa1_character_tick(g_ctx, id, &in, 1.0f / 30.0f, NULL, NULL);
        if (st != SA1_OK) {
            CHECK(0, "Q2 tick %d: %s", p, sa1_status_string(st));
            break;
        }
        count_faults();
        if (sa1_character_render(g_ctx, id, 1.0f, &r) == SA1_OK && r != NULL) {
            const sa1_draw *qd = qdraw_of(r);
            q2_check(r, &t);
            if (qd != NULL) {
                g_hash = fnv(g_hash, r->positions + 9u * qd->first, 9u * sizeof(float) * qd->count);
            }
            g_hash = fnv(g_hash, r->position, sizeof r->position);
        }
        if (sa1_character_render(g_ctx, id, 0.5f, &r) == SA1_OK && r != NULL) {
            q2_check(r, &t);
        }
    }
    CHECK(t.frames == 1000u && t.bad_tris == 0u && t.bad_archive == 0u, "Q2 %u renders: %u without exactly 692 triangles of the \"?\", %u draws that are not the \"?\", the blob, objects or a board", (unsigned)t.frames, (unsigned)t.bad_tris, (unsigned)t.bad_archive);
    CHECK(t.additive == 0u && t.sprites == 0u && t.tex7 == t.frames, "Q2 no SA1_DRAW_ADDITIVE (%u), no sprites (%u renders with any), texture 7 in %u of %u", (unsigned)t.additive, (unsigned)t.sprites, (unsigned)t.tex7, (unsigned)t.frames);
    CHECK(t.blob_frames > 0u, "Q2 the blob stays: in %u renders", (unsigned)t.blob_frames);
    st = sa1_character_geometry(g_ctx, id, &g);
    CHECK(st == SA1_OK && g != NULL && (g->num_triangles == 692u || g->num_triangles == 694u), "Q2 sa1_character_geometry after a pair: %u triangles", g ? (unsigned)g->num_triangles : 0u);
    {
        sa1_mesh m;
        sa1_input in;
        memset(&m, 0, sizeof m);
        m.struct_size = sizeof m;
        memset(&in, 0, sizeof in);
        in.struct_size = sizeof in;
        st = sa1_character_tick(g_ctx, id, &in, 1.0f / 30.0f, NULL, &m);
        CHECK(st == SA1_OK || st == SA1_ERR_BUFFER_TOO_SMALL, "Q2 the mesh: %s, needed %u triangles (692 or 694)", sa1_status_string(st), (unsigned)m.triangles_needed);
        CHECK(m.triangles_needed == 692u || m.triangles_needed == 694u, "Q2 the mesh's size: %u", (unsigned)m.triangles_needed);
    }
    sa1_character_destroy(g_ctx, id);
    count_faults();
    memset(&info, 0, sizeof info);
    info.struct_size = sizeof info;
    sa1_character_info_get(g_ctx, SA1_CHAR_SUPER_SONIC, &info);
    CHECK(info.num_archives == 6u, "Q2 sa1_character_info(8).num_archives %u (6)", (unsigned)info.num_archives);
    /* the control: the same filter does not touch a plain world */
    if (create_ex(g_ctx, SA1_CREATE_WORLD, 0.0f, 0.0f, 40.0f, 0u, -1, &pid) == SA1_OK) {
        for (p = 0; p < 200; p++) {
            sa1_input in;
            q1_input(p, &in);
            sa1_character_tick(g_ctx, pid, &in, 1.0f / 30.0f, NULL, NULL);
            count_faults();
            if (sa1_character_render(g_ctx, pid, 1.0f, &r) == SA1_OK && r != NULL) {
                uint32_t i;
                for (i = 0; i < r->num_draws; i++) {
                    plain_body += r->draws[i].archive == 0;
                    plain_add += (r->draws[i].flags & SA1_DRAW_ADDITIVE) != 0u;
                }
            }
        }
        CHECK(plain_body > 0u, "Q2 the control: a plain kind 8 draws his body in those pairs (%u draws of archive 0, %u additive)", (unsigned)plain_body, (unsigned)plain_add);
        sa1_character_destroy(g_ctx, pid);
    }
    count_faults();
}

static void test_q3(void)
{
    sa1_character_id id = 0;
    const sa1_render *r = NULL;
    uint32_t i, objects = 0, others = 0;
    int p;
    uint32_t k;
    printf("Q3 a game object stays drawn\n");
    for (k = 0; k < 4u; k++) {
        sa1_set_state(g_ctx, g_ring_rec[k], SA1_SET_STATE_PRESENT, 0u);
    }
    if (create_ex(g_ctx, SA1_CREATE_WORLD | SA1_CREATE_QUESTION, 0.0f, 0.0f, 40.0f, 0u, -1, &id) != SA1_OK) {
        CHECK(0, "Q3 create: %s", sa1_last_error(g_ctx));
        return;
    }
    for (p = 0; p < 20; p++) {
        tick(id, 0, 0, 0u, 0.0f);
        count_faults();
    }
    if (sa1_character_render(g_ctx, id, 1.0f, &r) == SA1_OK && r != NULL) {
        for (i = 0; i < r->num_draws; i++) {
            if (r->draws[i].flags & SA1_DRAW_OBJECT) {
                objects++;
            } else if (r->draws[i].archive != (int32_t)SA1_ARCHIVE_QUESTION && !(r->draws[i].flags & SA1_DRAW_SHADOW)) {
                others++;
            }
        }
    }
    CHECK(objects > 0u && others == 0u, "Q3 rings near it: %u draws with SA1_DRAW_OBJECT, %u draws of anything else but the \"?\" and the blob; %u triangles in all", (unsigned)objects, (unsigned)others, r ? (unsigned)r->num_triangles : 0u);
    sa1_character_destroy(g_ctx, id);
    count_faults();
}

/* ---- Q4 --------------------------------------------------------------------------------------------------------------------------------- */
/* how many BAMS the "?" of the open menu turns a frame (host axes), or 0 */
static double menu_turn(double *frames_seen)
{
    sa1_frontend_desc d;
    sa1_frontend *fe = NULL;
    sa1_frontend_input in;
    sa1_status st;
    double prev = 0.0, sum = 0.0;
    int have = 0, f, n = 0;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.flags = SA1_FRONTEND_SHOW_DIALOG;
    d.language = 1;
    d.slot = 6;
    d.open_slots = 0xFFFFFFFFu;
    st = sa1_frontend_open(g_ctx, &d, &fe);
    if (st != SA1_OK) {
        printf("    sa1_frontend_open: %s: %s\n", sa1_status_string(st), sa1_last_error(g_ctx));
        return 0.0;
    }
    memset(&in, 0, sizeof in);
    in.struct_size = sizeof in;
    for (f = 0; f < 300; f++) {
        const sa1_render *r = NULL;
        int32_t kind = -1;
        uint32_t nf = 0;
        if (sa1_frontend_tick(fe, 1.0f / 60.0f, &in, &nf) != SA1_OK) {
            break;
        }
        if (sa1_frontend_render(fe, &r, &kind) == SA1_OK && r != NULL && r->num_triangles == 692u && r->num_draws == 1u) {
            double a;
            if (vec_angle(r->positions, r->draws[0].first, r->draws[0].count, &a)) {
                if (have) {
                    sum += bams_diff(a, prev);
                    n++;
                }
                prev = a;
                have = 1;
            }
        }
    }
    sa1_frontend_close(fe);
    *frames_seen = (double)n;
    return n ? sum / n : 0.0;
}

static void test_q4(void)
{
    sa1_character_id id = 0;
    const sa1_render *r = NULL;
    const sa1_draw *qd;
    sa1_state_ex x;
    double a0 = 0.0, a1 = 0.0, sum = 0.0, frames = 0.0, turn, menu;
    float zmin = 1e9f, zmax = -1e9f;
    uint32_t i;
    int p, n = 0;
    printf("Q4 the spin\n");
    if (create_ex(g_ctx, SA1_CREATE_WORLD | SA1_CREATE_QUESTION, 0.0f, 0.0f, 40.0f, 0u, -1, &id) != SA1_OK) {
        CHECK(0, "Q4 create: %s", sa1_last_error(g_ctx));
        return;
    }
    for (p = 0; p < 30; p++) {
        tick(id, 0, 0, 0u, 0.0f);
        count_faults();
    }
    for (p = 0; p < 60; p++) {
        tick(id, 0, 0, 0u, 0.0f);
        count_faults();
        if (sa1_character_render(g_ctx, id, 1.0f, &r) != SA1_OK || r == NULL || (qd = qdraw_of(r)) == NULL) {
            continue;
        }
        if (vec_angle(r->positions, qd->first, qd->count, &a1)) {
            if (p > 0) {
                sum += bams_diff(a1, a0);
                n++;
            }
            a0 = a1;
        }
    }
    turn = n ? sum / n : 0.0;
    CHECK(n == 59 && fabs(fabs(turn) - 2.0 * 0x2D8) < 1.0, "Q4 a pair turns it %.2f BAMS (2 x 0x2D8 = %d, the sign is host axes), %d pairs", turn, 2 * 0x2D8, n);
    menu = menu_turn(&frames);
    CHECK(frames > 10.0 && fabs(fabs(menu) - 0x2D8) < 1.0, "Q4 the menu's own \"?\" (sa1_frontend_render, slot 6): %.2f BAMS a frame (0x2D8 = %d) over %.0f frames", menu, 0x2D8, frames);
    CHECK(turn * menu > 0.0, "Q4 the same way round as the menu: %.2f a pair, %.2f a frame", turn, menu);
    /* its foot on the ground: the lowest and the highest corner, against the drawn root */
    state_of(id, &x);
    sa1_character_render(g_ctx, id, 1.0f, &r);
    qd = r != NULL ? qdraw_of(r) : NULL;
    if (qd != NULL) {
        for (i = 0; i < 9u * qd->count; i += 3u) {
            float z = r->positions[9u * qd->first + i + 2u];
            zmin = z < zmin ? z : zmin;
            zmax = z > zmax ? z : zmax;
        }
        CHECK(fabsf(zmin - r->position[2]) < 0.1f && fabsf(zmax - zmin - 52.45f) < 0.2f, "Q4 the foot: lowest corner %.2f HU over the root (0), %.2f HU tall (52.4), the ground at 0: root z %.2f", (double)(zmin - r->position[2]),
              (double)(zmax - zmin), (double)r->position[2]);
    } else {
        CHECK(0, "Q4 no draw of the \"?\"");
    }
    /* spin off: its front along the way it runs */
    {
        /* the camera's yaw picks the way; the way itself is measured: the game's stick mapping lies 5.9 degrees off the host's camera_yaw */
        static const struct { float yaw; const char *name; } dirs[2] = { { 0.0f, "camera yaw 0" }, { 1.5707963f, "camera yaw 90 degrees" } };
        int di;
        sa1_status st = sa1_character_set_option(g_ctx, id, SA1_COPT_QUESTION_SPIN, 0);
        CHECK(st == SA1_OK, "Q4 SA1_COPT_QUESTION_SPIN 0: %s", sa1_status_string(st));
        for (di = 0; di < 2; di++) {
            float fmin = 1e9f, fmax = -1e9f, smin = 1e9f, smax = -1e9f, fx, fy, l;
            sa1_state_ex x0, x1;
            for (p = 0; p < 40; p++) {
                tick(id, 0, 1.0f, 0u, dirs[di].yaw);
                count_faults();
            }
            state_of(id, &x0);
            tick(id, 0, 1.0f, 0u, dirs[di].yaw);
            count_faults();
            state_of(id, &x1);
            fx = x1.position[0] - x0.position[0];
            fy = x1.position[1] - x0.position[1];
            l = (float)sqrt((double)(fx * fx + fy * fy));
            fx = l > 0.0f ? fx / l : 1.0f;
            fy = l > 0.0f ? fy / l : 0.0f;
            sa1_character_render(g_ctx, id, 1.0f, &r);
            qd = r != NULL ? qdraw_of(r) : NULL;
            if (qd == NULL) {
                CHECK(0, "Q4 no draw of the \"?\" (spin off)");
                continue;
            }
            for (i = 0; i < 3u * qd->count; i++) {
                const float *c = r->positions + 9u * qd->first + 3u * i;
                float dx = c[0] - r->position[0], dy = c[1] - r->position[1];
                float along = dx * fx + dy * fy, side = -dx * fy + dy * fx;
                fmin = along < fmin ? along : fmin;
                fmax = along > fmax ? along : fmax;
                smin = side < smin ? side : smin;
                smax = side > smax ? side : smax;
            }
            CHECK(fabsf(fmin) < 0.5f && fabsf(fmax - 7.4f) < 0.5f && smax - smin > 32.0f, "Q4 spin off, running at %s, way (%.3f %.3f): along the run %.2f .. %.2f HU (0 .. 7.4: its +z front leads), across it %.2f .. %.2f HU (the glyph's 32.9)", dirs[di].name, (double)fx, (double)fy,
                  (double)fmin, (double)fmax, (double)smin, (double)smax);
        }
        /* it does not turn while the yaw stands */
        {
            double b0 = 0.0, b1 = 0.0;
            for (p = 0; p < 20; p++) {
                tick(id, 0, 0, 0u, dirs[1].yaw);
                count_faults();
            }
            sa1_character_render(g_ctx, id, 1.0f, &r);
            qd = r != NULL ? qdraw_of(r) : NULL;
            if (qd != NULL) {
                vec_angle(r->positions, qd->first, qd->count, &b0);
            }
            for (p = 0; p < 10; p++) {
                tick(id, 0, 0, 0u, dirs[1].yaw);
                count_faults();
            }
            sa1_character_render(g_ctx, id, 1.0f, &r);
            qd = r != NULL ? qdraw_of(r) : NULL;
            if (qd != NULL) {
                vec_angle(r->positions, qd->first, qd->count, &b1);
            }
            CHECK(fabs(bams_diff(b1, b0)) < 1.0, "Q4 spin off, standing: it turned %.3f BAMS in 10 pairs", bams_diff(b1, b0));
        }
        st = sa1_character_set_option(g_ctx, id, SA1_COPT_QUESTION_SPIN, 1);
        tick(id, 0, 0, 0u, dirs[1].yaw);
        count_faults();
        CHECK(st == SA1_OK, "Q4 SA1_COPT_QUESTION_SPIN 1: %s", sa1_status_string(st));
    }
    sa1_character_destroy(g_ctx, id);
    /* the option on anything else */
    {
        sa1_character_id p0 = 0;
        if (create_ex(g_ctx, SA1_CREATE_WORLD, 0.0f, 0.0f, 40.0f, 0u, -1, &p0) == SA1_OK) {
            sa1_status st = sa1_character_set_option(g_ctx, p0, SA1_COPT_QUESTION_SPIN, 0);
            CHECK(st == SA1_ERR_UNSUPPORTED, "Q4 the option on a plain kind 8: %s: %s", sa1_status_string(st), sa1_last_error(g_ctx));
            sa1_character_destroy(g_ctx, p0);
        }
    }
    count_faults();
}

/* ---- Q5 --------------------------------------------------------------------------------------------------------------------------------- */
static int name_is(const char *a, const char *b)
{
    while (*a && *b) {
        char x = *a++, y = *b++;
        x = (x >= 'a' && x <= 'z') ? (char)(x - 32) : x;
        y = (y >= 'a' && y <= 'z') ? (char)(y - 32) : y;
        if (x != y) {
            return 0;
        }
    }
    return *a == *b;
}

static void test_q5_loaded(void)
{
    sa1_texture_info ti;
    const char *name = NULL;
    uint32_t n = 0;
    sa1_status st;
    printf("Q5 the archive\n");
    st = sa1_texture_archive(g_ctx, SA1_CHAR_SUPER_SONIC, SA1_ARCHIVE_QUESTION, &name, &n);
    CHECK(st == SA1_OK && n == 11u, "Q5 sa1_texture_archive(8, 62): %s, %s, %u textures (11)", sa1_status_string(st), name ? name : "-", (unsigned)n);
    memset(&ti, 0, sizeof ti);
    ti.struct_size = sizeof ti;
    st = sa1_texture_get(g_ctx, SA1_CHAR_SUPER_SONIC, SA1_ARCHIVE_QUESTION, 7, &ti);
    CHECK(st == SA1_OK && ti.name != NULL && name_is(ti.name, "CS_CE_TX") && ti.rgba != NULL, "Q5 (8, 62, 7): %s, %s, %ux%u", sa1_status_string(st), ti.name ? ti.name : "-", (unsigned)ti.width, (unsigned)ti.height);
    memset(&ti, 0, sizeof ti);
    ti.struct_size = sizeof ti;
    st = sa1_texture_get(g_ctx, SA1_CHAR_SUPER_SONIC, SA1_ARCHIVE_QUESTION, 10, &ti);
    CHECK(st == SA1_OK && ti.name != NULL && name_is(ti.name, "sstx_body") && ti.rgba != NULL, "Q5 (8, 62, 10): %s, %s, %ux%u", sa1_status_string(st), ti.name ? ti.name : "-", (unsigned)ti.width, (unsigned)ti.height);
    memset(&ti, 0, sizeof ti);
    ti.struct_size = sizeof ti;
    st = sa1_texture_get(g_ctx, SA1_CHAR_SONIC, SA1_ARCHIVE_QUESTION, 7, &ti);
    CHECK(st == SA1_ERR_NOT_FOUND, "Q5 (0, 62, 7): %s (not found)", sa1_status_string(st));
    st = sa1_texture_get(g_ctx, SA1_CHAR_SUPER_SONIC, SA1_ARCHIVE_QUESTION, 11, &ti);
    CHECK(st == SA1_ERR_NOT_FOUND, "Q5 (8, 62, 11): %s (not found)", sa1_status_string(st));
    st = sa1_texture_archive(g_ctx, SA1_CHAR_SUPER_SONIC, 6u, &name, &n);
    CHECK(st == SA1_ERR_NOT_FOUND, "Q5 enumerating kind 8's archives ends at 6: %s", sa1_status_string(st));
}

/* ---- Q6 --------------------------------------------------------------------------------------------------------------------------------- */
static void test_q6(void)
{
    sa1_character_desc d;
    sa1_character_id id = 0;
    sa1_status st;
    printf("Q6 refusals\n");
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.kind = SA1_CHAR_SONIC;
    d.flags = SA1_CREATE_WORLD | SA1_CREATE_QUESTION;
    st = sa1_character_create_ex(g_ctx, &d, &id);
    CHECK(st == SA1_ERR_INVALID_ARGUMENT && id == 0 && strstr(sa1_last_error(g_ctx), "SA1_CREATE_QUESTION: only kind 8") != NULL, "Q6 the flag on kind 0: %s: %s", sa1_status_string(st), sa1_last_error(g_ctx));
    d.kind = SA1_CHAR_SUPER_SONIC;
    d.flags = SA1_CREATE_PUPPET | SA1_CREATE_QUESTION;
    st = sa1_character_create_ex(g_ctx, &d, &id);
    CHECK(st == SA1_ERR_INVALID_ARGUMENT && id == 0 && strstr(sa1_last_error(g_ctx), "a puppet has no world") != NULL, "Q6 with SA1_CREATE_PUPPET: %s: %s", sa1_status_string(st), sa1_last_error(g_ctx));
    /* lazily: no world yet, the "?" already */
    d.flags = SA1_CREATE_QUESTION;
    d.position[2] = 40.0f;
    st = sa1_character_create_ex(g_ctx, &d, &id);
    if (st == SA1_OK) {
        const sa1_render *r = NULL;
        sa1_status s2 = sa1_character_render(g_ctx, id, 1.0f, &r);
        CHECK(s2 == SA1_OK && r != NULL && r->num_triangles == 692u, "Q6 no SA1_CREATE_WORLD, before the world: %u triangles", r ? (unsigned)r->num_triangles : 0u);
        s2 = tick(id, 0, 0, 0u, 0.0f);
        CHECK(s2 == SA1_OK, "Q6 its first tick builds the world: %s", sa1_status_string(s2));
        sa1_character_destroy(g_ctx, id);
    } else {
        CHECK(0, "Q6 lazily: %s: %s", sa1_status_string(st), sa1_last_error(g_ctx));
    }
    count_faults();
}

/* ---- Q7 --------------------------------------------------------------------------------------------------------------------------------- */
typedef struct q7_mem {
    int64_t blocks, bytes;
} q7_mem;

static void *q7_alloc(void *user, size_t size)
{
    q7_mem *m = (q7_mem *)user;
    uint8_t *p = (uint8_t *)malloc(size + 16u);
    if (p == NULL) {
        return NULL;
    }
    memcpy(p, &size, sizeof size);
    m->blocks++;
    m->bytes += (int64_t)size;
    return p + 16u;
}

static void q7_free(void *user, void *ptr)
{
    q7_mem *m = (q7_mem *)user;
    if (ptr != NULL) {
        uint8_t *p = (uint8_t *)ptr - 16u;
        size_t size;
        memcpy(&size, p, sizeof size);
        m->blocks--;
        m->bytes -= (int64_t)size;
        free(p);
    }
}

static int q7_cycle(sa1_context *c, const char *gdi, uint32_t flags)
{
    sa1_load_options lo;
    sa1_character_id id = 0;
    int p;
    memset(&lo, 0, sizeof lo);
    lo.struct_size = sizeof lo;
    lo.characters = (1u << SA1_CHAR_SUPER_SONIC) | (1u << SA1_CHAR_SONIC);
    lo.flags = SA1_LOAD_DEFAULT;
    if (SA1_FAILED(sa1_load_disc(c, gdi, &lo))) {
        printf("    load: %s\n", sa1_last_error(c));
        return -1;
    }
    if (scene_flat(c) != 0) {
        return -1;
    }
    if (create_ex(c, SA1_CREATE_WORLD | flags, 0.0f, 0.0f, 40.0f, 0u, -1, &id) != SA1_OK) {
        printf("    create: %s\n", sa1_last_error(c));
        return -1;
    }
    for (p = 0; p < 10; p++) {
        sa1_input in;
        sa1_event ev[64];
        uint32_t n = 0;
        const sa1_render *r = NULL;
        memset(&in, 0, sizeof in);
        in.struct_size = sizeof in;
        sa1_character_tick(c, id, &in, 1.0f / 30.0f, NULL, NULL);
        sa1_character_render(c, id, 0.5f, &r);
        do {
            n = 0;
            sa1_poll_events(c, ev, 64, &n);
        } while (n == 64);
    }
    sa1_character_destroy(c, id);
    sa1_unload_disc(c);
    return 0;
}

static void test_q7(const char *gdi)
{
    sa1_host h;
    sa1_context *c = NULL;
    q7_mem m;
    int64_t b1, by1, b2, by2, b3, by3, b4, by4;
    printf("Q7 every allocation freed (sa1_character_destroy, sa1_unload_disc)\n");
    memset(&m, 0, sizeof m);
    sa1_host_defaults(&h);
    h.user = &m;
    h.alloc = q7_alloc;
    h.free = q7_free;
    h.log = log_cb;
    h.log_level = SA1_LOG_WARN;
    if (sa1_init(&h, &c) != SA1_OK) {
        CHECK(0, "Q7 sa1_init");
        return;
    }
    if (q7_cycle(c, gdi, 0u) != 0) {
        CHECK(0, "Q7 the plain cycle");
        sa1_shutdown(c);
        return;
    }
    b1 = m.blocks;
    by1 = m.bytes;
    if (q7_cycle(c, gdi, SA1_CREATE_QUESTION) != 0) {
        CHECK(0, "Q7 the cycle with the flag");
        sa1_shutdown(c);
        return;
    }
    b2 = m.blocks;
    by2 = m.bytes;
    q7_cycle(c, gdi, SA1_CREATE_QUESTION);
    b3 = m.blocks;
    by3 = m.bytes;
    q7_cycle(c, gdi, 0u);
    b4 = m.blocks;
    by4 = m.bytes;
    printf("  info  live after each cycle's unload: plain %lld blocks %lld B; flag %lld blocks %lld B; flag %lld blocks %lld B; plain %lld blocks %lld B\n", (long long)b1, (long long)by1, (long long)b2, (long long)by2, (long long)b3,
           (long long)by3, (long long)b4, (long long)by4);
    CHECK(b2 == b1 && by2 == by1 && b3 == b1 && by3 == by1 && b4 == b1 && by4 == by1, "Q7 the flag's cycles leave nothing behind: %lld, %lld, %lld blocks against the plain %lld", (long long)b2, (long long)b3, (long long)b4, (long long)b1);
    sa1_shutdown(c);
    CHECK(m.blocks == 0 && m.bytes == 0, "Q7 sa1_shutdown: %lld blocks, %lld B left", (long long)m.blocks, (long long)m.bytes);
}

/* ---- Q8 --------------------------------------------------------------------------------------------------------------------------------- */
static void test_q8(void)
{
    sa1_character_id lid = 0, fid = 0;
    sa1_follower_desc fd;
    sa1_status st;
    uint32_t faults = 0;
    sa1_state_ex lx, fx;
    int p;
    float d = 0.0f;
    printf("Q8 a Sonic follower behind a \"?\": 300 pairs\n");
    if (create_ex(g_ctx, SA1_CREATE_WORLD | SA1_CREATE_QUESTION, -3000.0f, 0.0f, 40.0f, 0u, -1, &lid) != SA1_OK) {
        CHECK(0, "Q8 create: %s", sa1_last_error(g_ctx));
        return;
    }
    memset(&fd, 0, sizeof fd);
    fd.struct_size = sizeof fd;
    fd.kind = SA1_CHAR_SONIC;
    fd.leader = lid;
    st = sa1_follower_create(g_ctx, &fd, &fid);
    CHECK(st == SA1_OK && fid != 0, "Q8 sa1_follower_create: %s", st == SA1_OK ? "ok" : sa1_last_error(g_ctx));
    if (st != SA1_OK) {
        sa1_character_destroy(g_ctx, lid);
        return;
    }
    for (p = 0; p < 300; p++) {
        if (tick(lid, 0, (p > 30 && p < 80) ? 0.5f : 0.0f, 0u, 0.0f) != SA1_OK || tick(fid, 0, 0, 0u, 0.0f) != SA1_OK) {
            printf("    tick %d: %s\n", p, sa1_last_error(g_ctx));
            break;
        }
        faults += count_faults();
        state_of(lid, &lx);
        state_of(fid, &fx);
        g_hash = fnv(g_hash, fx.position, sizeof fx.position);
    }
    d = (float)sqrt((double)((lx.position[0] - fx.position[0]) * (lx.position[0] - fx.position[0]) + (lx.position[1] - fx.position[1]) * (lx.position[1] - fx.position[1])));
    CHECK(p == 300 && faults == 0u, "Q8 %d pairs, %u faults", p, (unsigned)faults);
    CHECK(d < 500.0f, "Q8 it follows: leader (%.0f %.0f %.0f), follower (%.0f %.0f %.0f), %.1f HU apart at the end", (double)lx.position[0], (double)lx.position[1], (double)lx.position[2], (double)fx.position[0], (double)fx.position[1],
          (double)fx.position[2], (double)d);
    sa1_character_destroy(g_ctx, fid);
    sa1_character_destroy(g_ctx, lid);
    count_faults();
}

/* ---- main --------------------------------------------------------------------------------------------------------------------------------- */
int main(int argc, char **argv)
{
    sa1_host h;
    sa1_load_options lo;
    sa1_coords co;
    const char *gdi = argc > 1 ? argv[1] : getenv("SA1_TEST_GDI");
    sa1_character_desc d;
    sa1_character_id id = 0;
    sa1_status st;
    uint32_t k;
    if (gdi == NULL || !*gdi) {
        printf("usage: question_test GDI\n");
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
    sa1_coords_preset(SA1_COORDS_PRESET_SOURCE, &co);
    sa1_set_coords(g_ctx, &co);
    memset(&lo, 0, sizeof lo);
    lo.struct_size = sizeof lo;
    lo.characters = (1u << SA1_CHAR_SONIC) | (1u << SA1_CHAR_SUPER_SONIC);
    lo.flags = SA1_LOAD_DEFAULT;
    if (SA1_FAILED(sa1_load_disc(g_ctx, gdi, &lo))) {
        printf("load: %s\n", sa1_last_error(g_ctx));
        return 1;
    }
    printf("question_test: libsa1 %s, %d-bit\n", sa1_version_string(), (int)(sizeof(void *) * 8));
    if (scene_flat(g_ctx) != 0) {
        return 1;
    }
    /* Q5, before the load */
    {
        sa1_texture_info ti;
        memset(&ti, 0, sizeof ti);
        ti.struct_size = sizeof ti;
        st = sa1_texture_get(g_ctx, SA1_CHAR_SUPER_SONIC, SA1_ARCHIVE_QUESTION, 7, &ti);
        CHECK(st == SA1_ERR_NOT_LOADED, "Q5 before the first create with the flag: %s (not loaded)", sa1_status_string(st));
    }
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.kind = SA1_CHAR_SUPER_SONIC;
    d.flags = SA1_CREATE_WORLD | SA1_CREATE_QUESTION;
    st = sa1_character_create_ex(g_ctx, &d, &id);
    if (st == SA1_ERR_UNSUPPORTED) {
        printf("SKIP behaviour: question is not built yet (%s)\n", sa1_last_error(g_ctx));
        sa1_shutdown(g_ctx);
        return 0;
    }
    CHECK(st == SA1_OK, "the first create with the flag: %s: %s", sa1_status_string(st), st == SA1_OK ? "ok" : sa1_last_error(g_ctx));
    if (st != SA1_OK) {
        return 1;
    }
    sa1_character_destroy(g_ctx, id);
    /* rings on the run's line and around the start: hits and rings for Q1, objects for Q3 */
    for (k = 0; k < 4u; k++) {
        sa1_set_record r;
        memset(&r, 0, sizeof r);
        r.struct_size = sizeof r;
        r.type = SA1_OBJTYPE_RING;
        r.position[0] = k == 0u ? 80.0f : 400.0f + 300.0f * (float)k;
        r.position[1] = k == 0u ? 60.0f : 0.0f;
        r.position[2] = 16.0f;
        st = sa1_set_add(g_ctx, &r, &g_ring_rec[k]);
        if (st != SA1_OK) {
            CHECK(0, "sa1_set_add: %s: %s", sa1_status_string(st), sa1_last_error(g_ctx));
        }
    }
    test_q5_loaded();
    test_q6();
    test_q2();
    test_q3();
    test_q4();
    test_q8();
    test_q1_config("kind 8 as the API makes it", 0u, 0u, -1, Q1_PAIRS);
    test_q1_config("kind 8 as G-SA1 makes the \"?\" (no drain, keeps at zero, water floor, 50 rings)", SA1_CREATE_SUPER | SA1_CREATE_WATER_FLOOR | SA1_CREATE_START_RINGS, SA1_SUPER_NO_DRAIN | SA1_SUPER_KEEP_AT_ZERO, 50, 600);
    printf("DETERMINISM question %08X\n", (unsigned)g_hash);
    sa1_shutdown(g_ctx);
    test_q7(gdi);
    printf("question_test: %d ok, %d failed\n", g_ok, g_fail);
    return g_fail ? 1 : 0;
}
