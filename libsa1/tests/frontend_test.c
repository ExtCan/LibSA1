/*
 * frontend_test.c - round 0.9.8 (098-lib-frontend): the game's own front end (include/libsa1.h "the game's own front end",
 * SA1_API_FRONTEND; src/sa1_frontend.c, src/world/sa1_world_frontend.c; sadc docs/research/gsa1_098_plan.md 6.5, charsel098.md 5, 7).
 *
 *   frontend_test [--only NAME[,NAME]] [--list] [--verbose] [--guard-alloc] [--sadc DIR] GDI     (GDI: or $SA1_TEST_GDI)
 *
 * Built and run by tests/build_frontend.py (a snapshot of build/<target>[-debug]/libsa1.a and LIB's headers). The oracle: the original
 * code in the harness, build/traces/ref/charsel_*.{d2d,menu}.txt of SADC, read at run time (numbers only, in ignored build/; a missing
 * reference is a FAIL, never a skip).
 *
 * ---- API notes (for 098-gsa1-charsel and the release step) --------------------------------------------------------------------
 *   sa1_frontend_open(ctx, &d, &fe)       the game thread, never inside a character's tick. Reads the disc (1ST_READ, ADVERTISE.PRS; the
 *                                         nine archives once per language). Runs the screen's 131 frames, frame 130 is the first with
 *                                         draws. A second open: SA1_ERR_LIMIT
 *   sa1_frontend_tick(fe, dt, &in, &n)    whole frames of 1/60 s, at most 8 (a backlog beyond that is dropped). A press between frames
 *                                         reaches the next frame, also when already let go. After EV_DONE / EV_CANCEL: n = 0, the last
 *                                         draws stay: close it then
 *   sa1_frontend_poll                     SA1_EVENT_SOUND, VOICE, JINGLE (music: id = the track, the mixer does not play it: review RC2)
 *                                         and SA1_EVENT_FRONTEND (id = slot, -1 for EV_CANCEL; arg = SA1_FRONTEND_EV_*). .step = the frame
 *   sa1_frontend_screen                   triangles in 640 x 480, each quad 2 triangles; order as sa1_character_screen: opaque, then
 *                                         blended, each from the most negative priority. Textures:
 *                                         sa1_texture_get(ctx, SA1_KIND_FRONTEND, archive, texture, ...)
 *   sa1_frontend_render                   the selected slot only, last frame, host space. kind: sa1_texture_get's kind for its draws
 *   sa1_frontend_view                     eye, forward, up, horizontal fov (host space)
 *   Every call on a dead world: SA1_ERR_GAME_FAULT; sa1_last_error names the function; sa1_frontend_state_get still fills faults != 0
 * Trial mode (no public way into it; 8C900F2C and 8C900F86 stay fatal: charsel098 5.3) is not replayed here: charsel_trial's reference
 * is only checked to exist.
 *
 * What a host should know beyond the header:
 *   SA1_EVENT_JINGLE id 0 is the game's music stop (the stubs 8C107AAA, 8C107D80; arg = the stub's DC address), at frames 130 and at the end of the pose,
 *   id 24 at frame 131 is "Choose Your Buddy!" (the only numbered track of this screen). SE: 1 the ring, 3 back, 22 the pick (the skip's, at the press), 21 and 2
 *   the dialog. VOICE: 4 at frame 131, the pick's voice one frame after EV_PICK.
 *   Slot -> kind: 0 Sonic, 1 Tails, 2 Knuckles, 3 Amy, 4 Big, 5 E-102, 6 "?" (kind SA1_KIND_FRONTEND). sa1_frontend_state.kind is the same. Slot 6 of a ring of 6
 *   starts on 0. A locked first slot stands there: the game does not check it, and A picks nothing.
 *   sa1_frontend_render's kinds' archives come from sa1_load_disc: a kind that was not loaded draws untextured (0 unresolved needs the kinds loaded).
 *   sa1_frontend_close frees the world and the image (24 MB); the nine archives stay. sa1_unload_disc and sa1_shutdown close an open front end: the host's
 *   pointer is dead then (gsa1_098_plan.md 5.5, review R98.20).
 *   The 2D draws of this screen all lie at priority about -60000: behind the 3D (SA1_FRONTEND_3D_PRIORITY).
 * Measured (this test, x86_64-windows, 2026-11-02): the first open 40 ms, a frame 0.10 ms, its screen 0.002 ms, its render 0.05 ms; x86 about twice that. 16 MB arena.
 *   adv, super, locked: 1,109 frames, 50,128 draws, 300,768 vertices bit-identical, 294,480 of 294,480 global indices resolved. The research's other 11 input scripts
 *   (--extra, references made with the harness in a scratch folder): 6,320 frames, 238,336 draws identical: the 288,464 non-Trial draws of charsel098.md 3.3's 300,559, on the
 *   landed code: the game's real camera pushes (review R98.19: no difference) and the real 8C014242 / 8C014308 with 8C6016D6 null, new against the probe (no fault, no
 *   difference: the lights are G-SA1's).
 * README lines (owner checklist): the Character Select is the game's own screen and own code; a pick takes the game's pose and voice, without its Game / Instruction /
 *   Cancel box unless the option shows it.
 */
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libsa1.h"
#include "sa1_internal.h"
#include "sa1_world.h"
#include "sa1_api_world.h"
#include "sa1_capture.h"
#include "sa1_frontend.h"
#include "test_util.h"
#include "guard_alloc.c"    /* SA1_GUARD_ALLOC=1 or --guard-alloc: every library heap block against a guard page */

#ifndef SA1_API_FRONTEND
#error "needs the 0.9.8 header"
#endif

static int g_fail, g_ok, g_verbose;
static const char *g_gdi;
static char g_sadc[1024];
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
#define NOTE(...)                                                                                                       \
    do {                                                                                                                \
        printf("  note  ");                                                                                             \
        printf(__VA_ARGS__);                                                                                            \
        printf("\n");                                                                                                   \
    } while (0)

#define FRAME_DT (1.0f / 60.0f)

/* ---- the counting allocator (allocations balanced) ------------------------------------------------------------------------ */
static int64_t g_live;
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

static void log_cb(void *user, int32_t level, const char *msg)
{
    (void)user;
    if (level >= SA1_LOG_WARN || g_verbose) {
        printf("    [libsa1] %s\n", msg);
    }
}

static sa1_context *mkctx(int counting, int load)
{
    sa1_context *ctx = NULL;
    sa1_host h;
    sa1_coords co;
    sa1_load_options lo;
    sa1_status st;
    sa1_host_defaults(&h);
    ga_host(&h);
    if (counting) {
        h.alloc = cnt_alloc;
        h.free = cnt_free;
    }
    h.log = log_cb;
    h.log_level = SA1_LOG_WARN;
    if (sa1_init(&h, &ctx) != SA1_OK) {
        printf("  sa1_init failed\n");
        return NULL;
    }
    memset(&co, 0, sizeof co);
    co.struct_size = sizeof co;
    sa1_coords_preset(SA1_COORDS_PRESET_NATIVE, &co);
    sa1_set_coords(ctx, &co);
    if (!load) {
        return ctx;
    }
    memset(&lo, 0, sizeof lo);
    lo.struct_size = sizeof lo;
    lo.characters = (1u << SA1_CHAR_SONIC) | (1u << SA1_CHAR_TAILS) | (1u << SA1_CHAR_KNUCKLES) | (1u << SA1_CHAR_AMY) |
                    (1u << SA1_CHAR_GAMMA) | (1u << SA1_CHAR_BIG);
    lo.flags = SA1_LOAD_DEFAULT;
    st = sa1_load_disc(ctx, g_gdi, &lo);
    if (SA1_FAILED(st)) {
        printf("  sa1_load_disc: %s: %s\n", sa1_status_string(st), sa1_last_error(ctx));
        sa1_shutdown(ctx);
        return NULL;
    }
    return ctx;
}

/* ---- the references ----------------------------------------------------------------------------------------------------------- */
typedef struct rf_vert {
    uint32_t x, y, z, u, v, col;
} rf_vert;

typedef struct rf_draw {
    int32_t frame;
    uint32_t seq;
    uint32_t tex;               /* 1: T (njDrawTexture), 0: G (njDrawPolygon) */
    uint32_t trans, gbix, blend;
    uint32_t first, n;          /* vertices */
} rf_draw;

typedef struct rf_event {
    int32_t frame;
    uint32_t type, id;          /* SA1_EVENT_* */
} rf_event;

typedef struct rf_state {
    int32_t mode, result, proc, slot, timer, have_sel;
} rf_state;

typedef struct rf_player {
    int32_t frame, slot;
    uint32_t mot;
    float ev_frame;
    int32_t have;
} rf_player;

typedef struct rf_ref {
    char name[32];
    rf_draw *draws;
    uint32_t nd, cap_d;
    rf_vert *verts;
    uint32_t nv, cap_v;
    int32_t *first_draw;        /* per frame: index of its first draw (frames + 1 entries) */
    int32_t frames;
    rf_event *events;
    uint32_t ne, cap_e;
    rf_state *states;           /* per frame */
    rf_player *players;
    uint32_t np, cap_p;
} rf_ref;

typedef struct rf_seg {
    int32_t n, x, y;
    uint32_t b;
} rf_seg;

static char *rf_read_all(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    long n;
    char *b;
    if (f == NULL) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    b = (char *)malloc((size_t)n + 1u);
    if (b == NULL || fread(b, 1, (size_t)n, f) != (size_t)n) {
        free(b);
        fclose(f);
        return NULL;
    }
    b[n] = 0;
    fclose(f);
    if (size != NULL) {
        *size = (size_t)n;
    }
    return b;
}

static void rf_grow(void **p, uint32_t *cap, uint32_t need, size_t el)
{
    if (need > *cap) {
        uint32_t c = *cap ? *cap * 2u : 1024u;
        while (c < need) {
            c *= 2u;
        }
        *p = realloc(*p, (size_t)c * el);
        *cap = c;
    }
}

/* splits a line in place into tokens */
static int rf_tokens(char *line, char **tok, int max)
{
    int n = 0;
    char *s = line;
    while (*s != 0 && n < max) {
        while (*s == ' ' || *s == '\t' || *s == '\r') {
            s++;
        }
        if (*s == 0) {
            break;
        }
        tok[n++] = s;
        while (*s != 0 && *s != ' ' && *s != '\t' && *s != '\r') {
            s++;
        }
        if (*s != 0) {
            *s++ = 0;
        }
    }
    return n;
}

static void rf_free(rf_ref *r)
{
    free(r->draws);
    free(r->verts);
    free(r->first_draw);
    free(r->events);
    free(r->states);
    free(r->players);
    memset(r, 0, sizeof *r);
}

static int rf_load(rf_ref *r, const char *name, int32_t frames)
{
    char path[1200], *b, *line, *next;
    int32_t f;
    uint32_t i;
    memset(r, 0, sizeof *r);
    snprintf(r->name, sizeof r->name, "%s", name);
    r->frames = frames;
    r->states = (rf_state *)calloc((size_t)frames + 1u, sizeof(rf_state));
    /* the draws */
    snprintf(path, sizeof path, "%s/build/traces/ref/charsel_%s.d2d.txt", g_sadc, name);
    b = rf_read_all(path, NULL);
    if (b == NULL) {
        printf("  FAIL  reference %s is missing\n", path);
        g_fail++;
        return 0;
    }
    for (line = b; line != NULL && *line != 0; line = next) {
        char *tok[64];
        int nt, t;
        next = strchr(line, '\n');
        if (next != NULL) {
            *next++ = 0;
        }
        if ((line[0] != 'T' && line[0] != 'G') || line[1] != ' ' || line[2] != 'M') {
            continue;
        }
        nt = rf_tokens(line, tok, 64);
        if (nt < 22) {
            continue;
        }
        rf_grow((void **)&r->draws, &r->cap_d, r->nd + 1u, sizeof *r->draws);
        {
            rf_draw *d = &r->draws[r->nd++];
            int firstv = nt;
            while (firstv > 0 && strchr(tok[firstv - 1], ',') != NULL) {
                firstv--;
            }
            d->frame = (int32_t)strtol(tok[2], NULL, 10);
            d->seq = (uint32_t)strtoul(tok[3], NULL, 10);
            d->tex = line[0] == 'T';
            d->trans = (uint32_t)strtoul(tok[11], NULL, 16);
            d->blend = (uint32_t)strtoul(tok[17], NULL, 16);
            d->gbix = d->tex ? (uint32_t)strtoul(tok[firstv - 2], NULL, 16) : 0u;
            d->first = r->nv;
            d->n = (uint32_t)(nt - firstv);
            for (t = firstv; t < nt; t++) {
                rf_vert *v;
                char *s = tok[t], *e;
                rf_grow((void **)&r->verts, &r->cap_v, r->nv + 1u, sizeof *r->verts);
                v = &r->verts[r->nv++];
                memset(v, 0, sizeof *v);
                v->x = (uint32_t)strtoul(s, &e, 16);
                v->y = (uint32_t)strtoul(e + 1, &e, 16);
                v->z = (uint32_t)strtoul(e + 1, &e, 16);
                if (d->tex) {
                    v->u = (uint32_t)strtoul(e + 1, &e, 16);
                    v->v = (uint32_t)strtoul(e + 1, &e, 16);
                }
                v->col = (uint32_t)strtoul(e + 1, &e, 16);
            }
        }
    }
    free(b);
    r->first_draw = (int32_t *)calloc((size_t)frames + 2u, sizeof(int32_t));
    i = 0;
    for (f = 0; f <= frames; f++) {
        while (i < r->nd && r->draws[i].frame < f) {
            i++;
        }
        r->first_draw[f] = (int32_t)i;
    }
    /* the state and the sounds */
    snprintf(path, sizeof path, "%s/build/traces/ref/charsel_%s.menu.txt", g_sadc, name);
    b = rf_read_all(path, NULL);
    if (b == NULL) {
        printf("  FAIL  reference %s is missing\n", path);
        g_fail++;
        return 0;
    }
    for (line = b; line != NULL && *line != 0; line = next) {
        char *tok[32];
        int nt, t;
        next = strchr(line, '\n');
        if (next != NULL) {
            *next++ = 0;
        }
        if (line[0] == 'N' && line[1] == ' ') {
            uint32_t type = 0;
            nt = rf_tokens(line, tok, 32);
            if (nt < 5) {
                continue;
            }
            type = !strcmp(tok[2], "SE") ? SA1_EVENT_SOUND : !strcmp(tok[2], "VOICE") ? SA1_EVENT_VOICE
                 : !strcmp(tok[2], "BGM") || !strcmp(tok[2], "MUSICSTOP") ? SA1_EVENT_JINGLE : 0u;
            if (type != 0) {
                rf_event *e;
                rf_grow((void **)&r->events, &r->cap_e, r->ne + 1u, sizeof *r->events);
                e = &r->events[r->ne++];
                e->frame = (int32_t)strtol(tok[1], NULL, 10);
                e->type = type;
                /* the harness logs the music stop 8C107AAA, whose stub says JINGLE id 0 */
                e->id = !strcmp(tok[2], "MUSICSTOP") ? 0u : (uint32_t)strtoul(tok[4] + 3, NULL, 16);
            }
        } else if (line[0] == 'M' && line[1] == ' ') {
            int32_t fr;
            nt = rf_tokens(line, tok, 32);
            fr = nt > 1 ? (int32_t)strtol(tok[1], NULL, 10) : -1;
            if (fr >= 0 && fr <= frames) {
                rf_state *s = &r->states[fr];
                for (t = 2; t < nt; t++) {
                    if (!strncmp(tok[t], "master_mode=", 12)) {
                        s->mode = atoi(tok[t] + 12);
                    } else if (!strncmp(tok[t], "result=", 7)) {
                        s->result = atoi(tok[t] + 7);
                    } else if (!strncmp(tok[t], "cs_proc=", 8)) {
                        s->proc = atoi(tok[t] + 8);
                        s->have_sel = 1;
                    } else if (!strncmp(tok[t], "cs_sel=", 7)) {
                        s->slot = atoi(tok[t] + 7);
                    } else if (!strncmp(tok[t], "cs_timer=", 9)) {
                        s->timer = atoi(tok[t] + 9);
                    }
                }
            }
        } else if (line[0] == 'P' && line[1] == ' ') {
            int32_t fr;
            nt = rf_tokens(line, tok, 32);
            fr = nt > 1 ? (int32_t)strtol(tok[1], NULL, 10) : -1;
            if (fr == 250 || fr == 400 || fr == 600) {
                rf_player *p;
                rf_grow((void **)&r->players, &r->cap_p, r->np + 1u, sizeof *r->players);
                p = &r->players[r->np++];
                memset(p, 0, sizeof *p);
                p->frame = fr;
                for (t = 2; t < nt; t++) {
                    if (!strncmp(tok[t], "slot=", 5)) {
                        p->slot = atoi(tok[t] + 5);
                    } else if (!strncmp(tok[t], "mot=", 4)) {
                        p->mot = (uint32_t)strtoul(tok[t] + 4, NULL, 16);
                        p->have = 1;
                    } else if (!strncmp(tok[t], "frame=", 6)) {
                        p->ev_frame = (float)atof(tok[t] + 6);
                    }
                }
            }
        }
    }
    free(b);
    return 1;
}

/* the harness script: "count x y [BUTTON ...]" per line */
static int rf_script(const char *name, rf_seg *seg, int cap)
{
    char path[1200], *b, *line, *next;
    int n = 0;
    snprintf(path, sizeof path, "%s/harness/tests/sim/menu/charsel_%s.txt", g_sadc, name);
    b = rf_read_all(path, NULL);
    if (b == NULL) {
        printf("  FAIL  script %s is missing\n", path);
        g_fail++;
        return 0;
    }
    for (line = b; line != NULL && *line != 0; line = next) {
        char *tok[16];
        int nt, t;
        next = strchr(line, '\n');
        if (next != NULL) {
            *next++ = 0;
        }
        if (line[0] < '0' || line[0] > '9' || n >= cap) {
            continue;
        }
        nt = rf_tokens(line, tok, 16);
        if (nt < 3) {
            continue;
        }
        seg[n].n = atoi(tok[0]);
        seg[n].x = atoi(tok[1]);
        seg[n].y = atoi(tok[2]);
        seg[n].b = 0;
        for (t = 3; t < nt; t++) {
            seg[n].b |= !strcmp(tok[t], "A") ? SA1_FRONTEND_B_A : !strcmp(tok[t], "B") ? SA1_FRONTEND_B_B
                      : !strcmp(tok[t], "START") ? SA1_FRONTEND_B_START : !strcmp(tok[t], "LEFT") ? SA1_FRONTEND_B_LEFT
                      : !strcmp(tok[t], "RIGHT") ? SA1_FRONTEND_B_RIGHT : !strcmp(tok[t], "UP") ? SA1_FRONTEND_B_UP
                      : !strcmp(tok[t], "DOWN") ? SA1_FRONTEND_B_DOWN : 0u;
        }
        n++;
    }
    free(b);
    return n;
}

static void script_at(const rf_seg *seg, int nseg, int32_t f, sa1_frontend_input *in)
{
    int i;
    memset(in, 0, sizeof *in);
    in->struct_size = sizeof *in;
    for (i = 0; i < nseg; i++) {
        if (f < seg[i].n) {
            in->stick_x = (float)seg[i].x / 127.0f;
            in->stick_y = -(float)seg[i].y / 127.0f;
            in->buttons = seg[i].b;
            return;
        }
        f -= seg[i].n;
    }
}

/* ---- comparing a screen with a reference frame -------------------------------------------------------------------------------- */
static uint32_t f2u(float f)
{
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

static float u2f(uint32_t u)
{
    float f;
    memcpy(&f, &u, 4);
    return f;
}

typedef struct cmp_stats {
    uint64_t frames, frames_bad, draws, verts;
    int32_t first_bad;
    char why[256];
    uint64_t gbix_ok, gbix_bad;
} cmp_stats;

typedef struct rf_item {
    const rf_draw *d;
    float pri;
} rf_item;

static int rf_before(const rf_item *a, const rf_item *b)
{
    if (a->d->trans != b->d->trans) {
        return a->d->trans < b->d->trans;
    }
    if (a->pri != b->pri) {
        return a->pri < b->pri;
    }
    return a->d->seq < b->d->seq;
}

/* the frame's draws in the sorted order, as triangles: every vertex, its priority, its texture and its blend */
static int cmp_frame(const rf_ref *r, int32_t ref_frame, const sa1_screen *scr, cmp_stats *cs, char *why, size_t nwhy)
{
    static rf_item items[4096];
    int32_t a = ref_frame < r->frames ? r->first_draw[ref_frame] : (int32_t)r->nd;
    int32_t b = ref_frame < r->frames ? r->first_draw[ref_frame + 1] : (int32_t)r->nd;
    int32_t i, k, nit = 0;
    uint32_t cur = 0, off = 0, total_v = 0;
    for (i = 0; i < (int32_t)scr->num_draws; i++) {
        total_v += scr->draws[i].count;
    }
    for (i = a; i < b && nit < 4096; i++) {
        rf_item it;
        it.d = &r->draws[i];
        it.pri = -1.0f / u2f(r->verts[it.d->first].z);
        for (k = nit; k > 0 && rf_before(&it, &items[k - 1]); k--) {
            items[k] = items[k - 1];
        }
        items[k] = it;
        nit++;
    }
    for (i = 0; i < nit; i++) {
        const rf_draw *d = items[i].d;
        uint32_t tris = d->n - 2u, t, c;
        for (t = 0; t < tris; t++) {
            for (c = 0; c < 3u; c++) {
                static const uint8_t even[3] = { 0, 1, 2 }, odd[3] = { 0, 2, 1 };
                const rf_vert *v = &r->verts[d->first + t + ((t & 1u) ? odd[c] : even[c])];
                const sa1_screen_draw *o;
                const sa1_screen_vertex *ov;
                uint8_t col[4];
                while (cur < scr->num_draws && off >= scr->draws[cur].count) {
                    cur++;
                    off = 0;
                }
                if (cur >= scr->num_draws) {
                    snprintf(why, nwhy, "frame %d: the screen ended at reference draw %d of %d", (int)ref_frame, (int)i, (int)nit);
                    return 0;
                }
                o = &scr->draws[cur];
                ov = &scr->vertices[o->first + off];
                off++;
                cs->verts++;
                col[0] = (uint8_t)(v->col >> 16);
                col[1] = (uint8_t)(v->col >> 8);
                col[2] = (uint8_t)v->col;
                col[3] = (uint8_t)(v->col >> 24);
                if (f2u(ov->x) != v->x || f2u(ov->y) != v->y || memcmp(ov->color, col, 4) != 0 ||
                    (d->tex && (f2u(ov->u) != v->u || f2u(ov->v) != v->v))) {
                    snprintf(why, nwhy, "frame %d draw %d vertex: screen %08X,%08X,%08X,%08X,%02X%02X%02X%02X ref %08X,%08X,%08X,%08X,%08X",
                             (int)ref_frame, (int)i, (unsigned)f2u(ov->x), (unsigned)f2u(ov->y), (unsigned)f2u(ov->u), (unsigned)f2u(ov->v),
                             ov->color[3], ov->color[0], ov->color[1], ov->color[2], (unsigned)v->x, (unsigned)v->y, (unsigned)v->u,
                             (unsigned)v->v, (unsigned)v->col);
                    return 0;
                }
                if (f2u(o->priority) != f2u(items[i].pri)) {
                    snprintf(why, nwhy, "frame %d draw %d priority %f, reference -1 / z = %f", (int)ref_frame, (int)i, (double)o->priority,
                             (double)items[i].pri);
                    return 0;
                }
                if (d->trans ? !(o->flags & SA1_DRAW_BLEND) : (o->flags & SA1_DRAW_BLEND) != 0) {
                    snprintf(why, nwhy, "frame %d draw %d: flags 0x%X, translucent %u", (int)ref_frame, (int)i, (unsigned)o->flags,
                             (unsigned)d->trans);
                    return 0;
                }
                if (d->trans) {
                    uint32_t src = d->blend & 0xFFu, dst = (d->blend >> 8) & 0xFFu;
                    if (src == 0u || src == 1u) {
                        dst = src;
                    }
                    if (o->blend_src != sa1_blend_of_ninja(src, 0) || o->blend_dst != sa1_blend_of_ninja(dst, 1)) {
                        snprintf(why, nwhy, "frame %d draw %d: blend %u, %u, the log's 0x%04X", (int)ref_frame, (int)i, o->blend_src,
                                 o->blend_dst, (unsigned)d->blend);
                        return 0;
                    }
                }
                if (d->tex) {
                    sa1_texture_info ti;
                    memset(&ti, 0, sizeof ti);
                    ti.struct_size = sizeof ti;
                    if (o->archive < 0 || o->texture < 0 ||
                        sa1_texture_get(g_ctx, SA1_KIND_FRONTEND, (uint32_t)o->archive, (uint32_t)o->texture, &ti) != SA1_OK ||
                        ti.global_index != d->gbix) {
                        cs->gbix_bad++;
                        snprintf(why, nwhy, "frame %d draw %d: archive %d texture %d has global index %08X, the log's %08X",
                                 (int)ref_frame, (int)i, (int)o->archive, (int)o->texture, (unsigned)ti.global_index, (unsigned)d->gbix);
                        return 0;
                    }
                    cs->gbix_ok++;
                } else if (o->archive != -1 || o->texture != -1) {
                    snprintf(why, nwhy, "frame %d draw %d: a polygon with a texture", (int)ref_frame, (int)i);
                    return 0;
                }
            }
        }
        cs->draws++;
    }
    if (cs->verts != 0 && (cur + 1u < scr->num_draws || (cur < scr->num_draws && off != scr->draws[cur].count))) {
        snprintf(why, nwhy, "frame %d: the screen has more than the reference's %d draws (%u vertices)", (int)ref_frame, (int)nit,
                 (unsigned)total_v);
        return 0;
    }
    if (nit == 0 && total_v != 0) {
        snprintf(why, nwhy, "frame %d: the reference drew nothing, the screen %u vertices", (int)ref_frame, (unsigned)total_v);
        return 0;
    }
    return 1;
}

static void cmp_note(cmp_stats *cs, const char *why, int32_t frame)
{
    if (cs->first_bad < 0) {
        cs->first_bad = frame;
        snprintf(cs->why, sizeof cs->why, "%s", why);
    }
    cs->frames_bad++;
}

/* ---- opening ---------------------------------------------------------------------------------------------------------------- */
static sa1_frontend *fe_open(uint32_t flags, int32_t language, int32_t slot, uint32_t open_slots)
{
    sa1_frontend_desc d;
    sa1_frontend *fe = NULL;
    sa1_status st;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.flags = flags;
    d.language = language;
    d.slot = slot;
    d.open_slots = open_slots;
    st = sa1_frontend_open(g_ctx, &d, &fe);
    if (st != SA1_OK) {
        printf("  FAIL  sa1_frontend_open: %s: %s\n", sa1_status_string(st), sa1_last_error(g_ctx));
        g_fail++;
        return NULL;
    }
    return fe;
}

/* runs one frame with `in`; 0 on a failure */
static int fe_tick1(sa1_frontend *fe, const sa1_frontend_input *in, int32_t f)
{
    uint32_t n = 0;
    sa1_status st = sa1_frontend_tick(fe, FRAME_DT, in, &n);
    if (st != SA1_OK || n != 1u) {
        printf("  FAIL  frame %d: tick %s, %u frames: %s\n", (int)f, sa1_status_string(st), (unsigned)n, sa1_last_error(g_ctx));
        g_fail++;
        return 0;
    }
    return 1;
}

typedef struct ev_log {
    sa1_event ev[4096];
    uint32_t n;
} ev_log;

static void ev_collect(sa1_frontend *fe, ev_log *l)
{
    uint32_t n = 0;
    while (sa1_frontend_poll(fe, l->ev + l->n, 4096u - l->n, &n) == SA1_OK && n > 0) {
        l->n += n;
        if (l->n == 4096u) {
            break;
        }
    }
}

/* the frame of the first SA1_EVENT_FRONTEND with this arg (-1: none) */
static int32_t ev_frame_of(const ev_log *l, uint32_t arg, uint32_t *id)
{
    uint32_t i;
    for (i = 0; i < l->n; i++) {
        if (l->ev[i].type == SA1_EVENT_FRONTEND && l->ev[i].arg == arg) {
            if (id != NULL) {
                *id = l->ev[i].id;
            }
            return (int32_t)l->ev[i].step;
        }
    }
    return -1;
}

static int ev_has(const ev_log *l, uint32_t type, uint32_t id, int32_t frame)
{
    uint32_t i;
    for (i = 0; i < l->n; i++) {
        if (l->ev[i].type == type && l->ev[i].id == id && (frame < 0 || (int32_t)l->ev[i].step == frame)) {
            return 1;
        }
    }
    return 0;
}

/* events the harness' log does not name: the front end's own, and 8C107D80 (its stub: JINGLE id 0, arg = its address) next to the music stop */
static int ev_not_logged(const sa1_event *e)
{
    return e->type == SA1_EVENT_FRONTEND || (e->type == SA1_EVENT_JINGLE && e->id == 0u && e->arg != 0x8C107AAAu);
}

/* the game's sounds of a log = the reference's (frame >= from, shifted) */
static int ev_same(const ev_log *l, const rf_ref *r, int32_t from, int32_t shift, char *why, size_t nwhy)
{
    uint32_t i, k = 0;
    for (i = 0; i < r->ne; i++) {
        const rf_event *e = &r->events[i];
        if (e->frame < from + shift) {
            continue;
        }
        while (k < l->n && (ev_not_logged(&l->ev[k]) || (int32_t)l->ev[k].step < from)) {
            k++;
        }
        if (k >= l->n) {
            snprintf(why, nwhy, "the reference's event %u at %d (type %u id %u) is missing", (unsigned)i, (int)e->frame, (unsigned)e->type,
                     (unsigned)e->id);
            return 0;
        }
        if ((int32_t)l->ev[k].step + shift != e->frame || l->ev[k].type != e->type || l->ev[k].id != e->id) {
            snprintf(why, nwhy, "event at frame %d type %u id %u, the reference's at %d type %u id %u", (int)l->ev[k].step + shift,
                     (unsigned)l->ev[k].type, (unsigned)l->ev[k].id, (int)e->frame, (unsigned)e->type, (unsigned)e->id);
            return 0;
        }
        k++;
    }
    while (k < l->n) {
        if (!ev_not_logged(&l->ev[k]) && (int32_t)l->ev[k].step >= from) {
            snprintf(why, nwhy, "an extra event at frame %d type %u id %u", (int)l->ev[k].step, (unsigned)l->ev[k].type, (unsigned)l->ev[k].id);
            return 0;
        }
        k++;
    }
    return 1;
}

/* ---- the world, for the P lines ------------------------------------------------------------------------------------------------ */
static uint32_t wrd(const sa1g_world *w, uint32_t g)
{
    uint32_t v = 0;
    sa1g_world_rd32(w, g, &v);
    return v;
}

static int player_check(const sa1g_world *w, const rf_ref *r, int32_t frame, char *why, size_t nwhy)
{
    uint32_t t3 = wrd(w, 0x8C961348u), w3 = t3 ? wrd(w, t3 + 0x2Cu) : 0u, i;
    for (i = 0; i < r->np; i++) {
        const rf_player *p = &r->players[i];
        uint32_t tp, twp, ewp, l, ms;
        float fr;
        if (p->frame != frame || !p->have) {
            continue;
        }
        tp = w3 ? wrd(w, w3 + 4u * (uint32_t)p->slot) : 0u;
        twp = tp ? wrd(w, tp + 0x20u) : 0u;
        ewp = twp ? wrd(w, twp + 0x3Cu) : 0u;
        l = ewp ? wrd(w, ewp + 0x40u) : 0u;
        ms = l ? wrd(w, l + 8u) : 0u;
        fr = ewp ? u2f(wrd(w, ewp + 0x3Cu)) : -1.0f;
        if (ms != p->mot || fabs((double)fr - (double)p->ev_frame) > 0.0051) {
            snprintf(why, nwhy, "frame %d slot %d: mot %08X frame %.3f, the reference's %08X %.2f", (int)frame, (int)p->slot, (unsigned)ms,
                     (double)fr, (unsigned)p->mot, (double)p->ev_frame);
            return 0;
        }
    }
    return 1;
}

/* ---- case 1, 2, 3: the references ----------------------------------------------------------------------------------------------- */
static uint32_t g_det;
static void det_add(uint32_t v)
{
    g_det = (g_det ^ v) * 16777619u;
}

static void det_screen(const sa1_screen *s)
{
    uint32_t i;
    for (i = 0; i < s->num_vertices; i++) {
        det_add(f2u(s->vertices[i].x));
        det_add(f2u(s->vertices[i].y));
        det_add(f2u(s->vertices[i].u));
        det_add(f2u(s->vertices[i].v));
        det_add((uint32_t)s->vertices[i].color[0] | ((uint32_t)s->vertices[i].color[1] << 8) | ((uint32_t)s->vertices[i].color[2] << 16) |
                ((uint32_t)s->vertices[i].color[3] << 24));
    }
    det_add(s->num_draws);
}

static void det_render(const sa1_render *r)
{
    uint32_t i;
    for (i = 0; i < 9u * r->num_triangles; i++) {
        det_add(f2u(r->positions[i]));
    }
}

static void run_reference(const char *name, int32_t frames, uint32_t open_slots, cmp_stats *total)
{
    rf_ref r;
    rf_seg seg[64];
    int nseg;
    sa1_frontend *fe;
    cmp_stats cs;
    static ev_log log;
    const sa1_screen *scr = NULL;
    sa1_frontend_state st;
    sa1fe_test_info ti;
    char why[256];
    int32_t f, state_bad = -1;
    double t0, tsum = 0, tmax = 0;
    memset(&cs, 0, sizeof cs);
    cs.first_bad = -1;
    if (!rf_load(&r, name, frames)) {
        return;
    }
    nseg = rf_script(name, seg, 64);
    fe = fe_open(SA1_FRONTEND_SHOW_DIALOG, 1, 0, open_slots);
    if (fe == NULL || nseg == 0) {
        rf_free(&r);
        return;
    }
    sa1fe_test_run_on(fe, 1);
    log.n = 0;
    /* the open's frames 0..130: the input of the script is neutral there */
    for (f = 130; f < frames; f++) {
        if (f > 130) {
            sa1_frontend_input in;
            script_at(seg, nseg, f, &in);
            t0 = tu_now_ms();
            if (!fe_tick1(fe, &in, f)) {
                break;
            }
            t0 = tu_now_ms() - t0;
            tsum += t0;
            tmax = t0 > tmax ? t0 : tmax;
        }
        ev_collect(fe, &log);
        if (sa1_frontend_screen(fe, &scr) != SA1_OK || scr == NULL) {
            cmp_note(&cs, "sa1_frontend_screen failed", f);
            continue;
        }
        cs.frames++;
        if (!cmp_frame(&r, f, scr, &cs, why, sizeof why)) {
            cmp_note(&cs, why, f);
        }
        det_screen(scr);
        /* the state against the M lines (as the logs read it, after the frame) */
        memset(&st, 0, sizeof st);
        st.struct_size = sizeof st;
        sa1_frontend_state_get(fe, &st);
        if (state_bad < 0 && (st.frame != f + 1 || (r.states[f].have_sel && (st.slot != r.states[f].slot ||
                              (r.states[f].proc == 9 && st.pose_frames != r.states[f].timer))))) {
            state_bad = f;
        }
    }
    ev_collect(fe, &log);
    sa1fe_test_get(fe, &ti);
    CHECK(cs.frames_bad == 0 && cs.frames == (uint64_t)(frames - 130), "%s: %llu frames, %llu draws, %llu vertices: %llu frames differ%s%s",
          name, (unsigned long long)cs.frames, (unsigned long long)cs.draws, (unsigned long long)cs.verts,
          (unsigned long long)cs.frames_bad, cs.frames_bad ? ": " : "", cs.frames_bad ? cs.why : "");
    CHECK(ti.screen_misses == 0 && cs.gbix_bad == 0, "%s: global indices: %llu resolved, %u misses (100 %% required)", name,
          (unsigned long long)cs.gbix_ok, (unsigned)ti.screen_misses);
    CHECK(state_bad < 0, "%s: the state equals the M lines (slot, pose frames, frame count) (first difference at %d)", name, (int)state_bad);
    {
        int ok = ev_same(&log, &r, 0, 0, why, sizeof why);
        CHECK(ok, "%s: events SOUND, VOICE, JINGLE: %u in the reference, on their frames%s%s", name, (unsigned)r.ne, ok ? "" : ": ", ok ? "" : why);
    }
    if (g_verbose) {
        uint32_t q;
        for (q = 0; q < log.n; q++) {
            NOTE("event frame %u type %u id %u arg %u", (unsigned)log.ev[q].step, (unsigned)log.ev[q].type, (unsigned)log.ev[q].id, (unsigned)log.ev[q].arg);
        }
    }
    NOTE("%s: frame cost mean %.3f ms, max %.3f ms (with the capture window, no emit)", name, tsum / (double)(frames - 131), tmax);
    total->frames += cs.frames;
    total->draws += cs.draws;
    total->verts += cs.verts;
    total->frames_bad += cs.frames_bad;
    total->gbix_ok += cs.gbix_ok;
    sa1_frontend_close(fe);
    rf_free(&r);
}

static void case_references(void)
{
    cmp_stats t;
    char path[1200];
    FILE *f;
    memset(&t, 0, sizeof t);
    /* tests 1 and 2: charsel_adv, charsel_super, charsel_locked, and the pick reference below. Their args: --unlock, or none */
    run_reference("adv", 754, 0xFFFFFFFFu, &t);
    run_reference("super", 352, 0xFFFFFFFFu, &t);
    run_reference("locked", 393, 0u, &t);
    NOTE("total: %llu frames, %llu draws, %llu vertices, %llu frames differ, %llu global indices resolved", (unsigned long long)t.frames,
         (unsigned long long)t.draws, (unsigned long long)t.verts, (unsigned long long)t.frames_bad, (unsigned long long)t.gbix_ok);
    /* Trial: 8C900F2C / 8C900F86 stay fatal (charsel098 5.3): no public way into it, its reference only exists */
    snprintf(path, sizeof path, "%s/build/traces/ref/charsel_trial.d2d.txt", g_sadc);
    f = fopen(path, "rb");
    CHECK(f != NULL, "trial: the reference exists (a front end never sets Trial: not replayed, 8C900F2C and 8C900F86 stay fatal)");
    if (f != NULL) {
        fclose(f);
    }
}

/* --extra NAME:FRAMES[,NAME:FRAMES ...]: more scripts with their references under --sadc (charsel_NAME.*, the options of --unlock): the research's other
 * input scripts, made with the harness for one run (a lane's scratch folder). Not in a normal run: the references of the tree are the four above */
static const char *g_extra;

static void case_extra(void)
{
    cmp_stats t;
    const char *p = g_extra;
    memset(&t, 0, sizeof t);
    while (p != NULL && *p != 0) {
        char name[32];
        const char *c = strchr(p, ':'), *e = strchr(p, ',');
        int32_t frames;
        size_t n;
        if (c == NULL) {
            break;
        }
        n = (size_t)(c - p);
        n = n < sizeof name - 1u ? n : sizeof name - 1u;
        memcpy(name, p, n);
        name[n] = 0;
        frames = (int32_t)strtol(c + 1, NULL, 10);
        run_reference(name, frames, 0xFFFFFFFFu, &t);
        p = e != NULL ? e + 1 : NULL;
    }
    NOTE("extra total: %llu frames, %llu draws, %llu vertices, %llu frames differ, %llu global indices resolved", (unsigned long long)t.frames,
         (unsigned long long)t.draws, (unsigned long long)t.verts, (unsigned long long)t.frames_bad, (unsigned long long)t.gbix_ok);
}

/* test 3: one pass of the selected slot, the table of charsel098 3.3 */
static void case_render(void)
{
    static const struct { int32_t kind; uint32_t exec; uint32_t models, tris; int32_t x0, x1, y0, y1; } t[7] = {
        { SA1_CHAR_SONIC, 0x8C0A77BCu, 28, 1001, 253, 385, 93, 309 },
        { SA1_CHAR_TAILS, 0x8C0B6148u, 28, 997, 239, 377, 102, 293 },
        { SA1_CHAR_KNUCKLES, 0x8C0C0640u, 28, 981, 259, 401, 95, 317 },
        { SA1_CHAR_AMY, 0x8C0D6B40u, 31, 934, 246, 389, 92, 313 },
        { SA1_CHAR_BIG, 0x8C0DFFA0u, 36, 1134, 175, 446, 103, 327 },
        { SA1_CHAR_GAMMA, 0x8C0CBE00u, 37, 1036, 230, 434, 71, 329 },
        { SA1_KIND_FRONTEND, 0x8C9027A6u, 1, 692, 304, 364, 139, 319 },
    };
    sa1_frontend *fe = fe_open(SA1_FRONTEND_SHOW_DIALOG, 1, 0, 0xFFFFFFFFu);
    sa1_frontend_input in;
    sa1fe_test_info ti;
    rf_ref r;
    char why[256];
    int32_t f = 131, slot;
    float eye[3], fw[3], up[3], fov = 0;
    if (fe == NULL) {
        return;
    }
    memset(&in, 0, sizeof in);
    in.struct_size = sizeof in;
    sa1_frontend_view(fe, eye, fw, up, &fov);
    CHECK(eye[0] == 0 && eye[1] == 0 && eye[2] == 0 && fw[2] == -1.0f && up[1] == 1.0f && fabs((double)fov - 70.0) < 0.02,
          "the view: eye (%g, %g, %g), forward (%g, %g, %g), up (%g, %g, %g), %.3f degrees (native coordinates)", (double)eye[0], (double)eye[1],
          (double)eye[2], (double)fw[0], (double)fw[1], (double)fw[2], (double)up[0], (double)up[1], (double)up[2], (double)fov);
    for (slot = 0; slot < 7; slot++) {
        const sa1_render *rd = NULL;
        sa1_capture_stats cst;
        sa1_frontend_state st;
        int32_t kind = -1;
        uint32_t i;
        float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
        /* an idle frame on this slot: 69 frames after a move, as the reference's frame 250 is */
        for (i = 0; i < 80u; i++, f++) {
            if (!fe_tick1(fe, &in, f)) {
                sa1_frontend_close(fe);
                return;
            }
        }
        memset(&st, 0, sizeof st);
        st.struct_size = sizeof st;
        sa1_frontend_state_get(fe, &st);
        if (sa1_frontend_render(fe, &rd, &kind) != SA1_OK || rd == NULL) {
            CHECK(0, "slot %d: sa1_frontend_render: %s", (int)slot, sa1_last_error(g_ctx));
            continue;
        }
        sa1fe_test_get(fe, &ti);
        sa1_capture_get_stats(ti.cap, &cst);
        for (i = 0; i < 9u * rd->num_triangles; i += 3u) {
            float z = -rd->positions[i + 2];
            if (z > 1.0f) {
                float sx = 320.0f + 457.06f * rd->positions[i] / z, sy = 240.0f - 457.06f * rd->positions[i + 1] / z;
                x0 = sx < x0 ? sx : x0;
                x1 = sx > x1 ? sx : x1;
                y0 = sy < y0 ? sy : y0;
                y1 = sy > y1 ? sy : y1;
            }
        }
        CHECK(st.slot == slot && kind == t[slot].kind && st.kind == t[slot].kind && cst.emitted == t[slot].models &&
              rd->num_triangles == t[slot].tris && cst.unresolved == 0u,
              "slot %d: kind %d, %u models, %u triangles (table: %u, %u), %u unresolved; on the screen x %.0f..%.0f, y %.0f..%.0f", (int)slot, (int)kind,
              (unsigned)cst.emitted, (unsigned)rd->num_triangles, (unsigned)t[slot].models, (unsigned)t[slot].tris, (unsigned)cst.unresolved, (double)x0,
              (double)x1, (double)y0, (double)y1);
        det_render(rd);
        memset(&in, 0, sizeof in);
        in.struct_size = sizeof in;
        in.buttons = SA1_FRONTEND_B_RIGHT;
        if (!fe_tick1(fe, &in, f++)) {
            sa1_frontend_close(fe);
            return;
        }
        in.buttons = 0;
    }
    sa1_frontend_close(fe);
    /* the event action heads against the P lines of frames 250, 400, 600 */
    if (rf_load(&r, "adv", 754)) {
        rf_seg seg[64];
        int nseg = rf_script("adv", seg, 64), bad = 0;
        fe = fe_open(SA1_FRONTEND_SHOW_DIALOG, 1, 0, 0xFFFFFFFFu);
        if (fe != NULL) {
            sa1fe_test_run_on(fe, 1);
            for (f = 131; f <= 600 && !bad; f++) {
                script_at(seg, nseg, f, &in);
                if (!fe_tick1(fe, &in, f)) {
                    break;
                }
                if (f == 250 || f == 400 || f == 600) {
                    sa1fe_test_get(fe, &ti);
                    if (!player_check((const sa1g_world *)ti.world, &r, f, why, sizeof why)) {
                        CHECK(0, "%s", why);
                        bad = 1;
                    }
                }
            }
            CHECK(!bad && r.np > 0, "the event action heads (motion, frame) equal the reference's %u P lines at frames 250, 400, 600", (unsigned)r.np);
            sa1_frontend_close(fe);
        }
        rf_free(&r);
    }
}

/* test 4: the dialog skip (no SA1_FRONTEND_SHOW_DIALOG) against charsel_pick2 */
static void case_skip(void)
{
    rf_ref r;
    sa1_frontend *fe;
    sa1_frontend_input in;
    const sa1_screen *scr = NULL;
    cmp_stats cs;
    static ev_log log;
    char why[256];
    int32_t f, frames = 860, shift = 42, ref_done = -1;
    uint32_t id = 0;
    if (!rf_load(&r, "pick2", frames)) {
        return;
    }
    for (f = 0; f < frames; f++) {
        if (r.states[f].result != 0) {
            ref_done = f;
            break;
        }
    }
    memset(&cs, 0, sizeof cs);
    cs.first_bad = -1;
    log.n = 0;
    fe = fe_open(0u, 1, 0, 0xFFFFFFFFu);
    if (fe == NULL) {
        rf_free(&r);
        return;
    }
    sa1fe_test_run_on(fe, 1);
    for (f = 131; f + shift < frames; f++) {
        memset(&in, 0, sizeof in);
        in.struct_size = sizeof in;
        in.buttons = (f == 230 || f == 271) ? SA1_FRONTEND_B_RIGHT : f == 312 ? SA1_FRONTEND_B_A : 0u;
        if (!fe_tick1(fe, &in, f)) {
            break;
        }
        ev_collect(fe, &log);
        if (f >= 313) {
            if (sa1_frontend_screen(fe, &scr) != SA1_OK || scr == NULL) {
                cmp_note(&cs, "sa1_frontend_screen failed", f);
                continue;
            }
            cs.frames++;
            if (!cmp_frame(&r, f + shift, scr, &cs, why, sizeof why)) {
                cmp_note(&cs, why, f);
            }
            det_screen(scr);
        }
    }
    CHECK(cs.frames_bad == 0 && cs.frames > 300u, "frames >= 313 equal the reference at +42: %llu frames, %llu draws, %llu differ%s%s",
          (unsigned long long)cs.frames, (unsigned long long)cs.draws, (unsigned long long)cs.frames_bad, cs.frames_bad ? ": " : "",
          cs.frames_bad ? cs.why : "");
    CHECK(ev_has(&log, SA1_EVENT_SOUND, 22u, 312), "SE 22 (the game's confirm tone) at the press, frame 312");
    CHECK(!ev_has(&log, SA1_EVENT_SOUND, 21u, -1) && !ev_has(&log, SA1_EVENT_SOUND, 2u, -1), "no SE 21, no SE 2: they belong to the dialog");
    {
        int ok = ev_same(&log, &r, 313, shift, why, sizeof why);
        CHECK(ok, "the sounds of frames >= 313 equal the reference's at +42%s%s", ok ? "" : ": ", ok ? "" : why);
    }
    CHECK(ev_has(&log, SA1_EVENT_VOICE, 1787u, 314), "the voice 1787 at frame 314 (reference 356)");
    f = ev_frame_of(&log, SA1_FRONTEND_EV_PICK, &id);
    CHECK(f == 313 && id == 2u, "EV_PICK at frame %d (313), slot %u (2)", (int)f, (unsigned)id);
    f = ev_frame_of(&log, SA1_FRONTEND_EV_DONE, &id);
    CHECK(ref_done >= 0 && f + shift == ref_done && id == 2u, "EV_DONE at frame %d (the reference's result 101 at %d - 42), slot %u", (int)f, (int)ref_done,
          (unsigned)id);
    sa1_frontend_close(fe);
    /* A skipping the rest: a key still held stays withheld */
    fe = fe_open(0u, 1, 0, 0xFFFFFFFFu);
    if (fe != NULL) {
        sa1_frontend_state st;
        for (f = 131; f < 600; f++) {
            memset(&in, 0, sizeof in);
            in.struct_size = sizeof in;
            in.buttons = f >= 300 ? SA1_FRONTEND_B_A : 0u;      /* held from the press on */
            if (!fe_tick1(fe, &in, f)) {
                break;
            }
        }
        memset(&st, 0, sizeof st);
        st.struct_size = sizeof st;
        sa1_frontend_state_get(fe, &st);
        CHECK(st.phase == 3 && st.pose_frames > 250, "a held A: phase %d, pose frames %d, the withheld key does not reach the pose", (int)st.phase, (int)st.pose_frames);
        sa1_frontend_close(fe);
    }
    rf_free(&r);
}

/* test 5: first slot, ring of 6 */
static void case_slots(void)
{
    sa1_frontend *fe = fe_open(0u, 1, 2, 0xFFFFFFFFu);
    sa1_frontend_state st;
    sa1_frontend_input in;
    int32_t f;
    const sa1_render *rd = NULL;
    int32_t kind = -1;
    memset(&in, 0, sizeof in);
    in.struct_size = sizeof in;
    if (fe != NULL) {
        for (f = 131; f < 200; f++) {
            fe_tick1(fe, &in, f);
        }
        memset(&st, 0, sizeof st);
        st.struct_size = sizeof st;
        sa1_frontend_state_get(fe, &st);
        sa1_frontend_render(fe, &rd, &kind);
        CHECK(st.slot == 2 && st.kind == SA1_CHAR_KNUCKLES && st.slots == 7 && rd != NULL && rd->num_triangles == 981u && kind == SA1_CHAR_KNUCKLES,
              "slot 2 starts on Knuckles: slot %d, kind %d, %d slots, %u triangles", (int)st.slot, (int)st.kind, (int)st.slots, rd ? (unsigned)rd->num_triangles : 0u);
        sa1_frontend_close(fe);
    }
    fe = fe_open(0u, 1, 0, 0x3Fu);
    if (fe != NULL) {
        memset(&st, 0, sizeof st);
        st.struct_size = sizeof st;
        sa1_frontend_state_get(fe, &st);
        CHECK(st.slots == 6, "open_slots without slot 6: a ring of %d", (int)st.slots);
        sa1_frontend_close(fe);
    }
    /* a first slot the game does not let you pick: it stands there (the game reads B_8C78B45D after its own test), and A picks nothing */
    fe = fe_open(0u, 1, 3, 0x1u);
    if (fe != NULL) {
        static ev_log log;
        log.n = 0;
        for (f = 131; f < 260; f++) {
            in.buttons = f == 200 ? SA1_FRONTEND_B_A : 0u;
            fe_tick1(fe, &in, f);
            ev_collect(fe, &log);
        }
        in.buttons = 0;
        memset(&st, 0, sizeof st);
        st.struct_size = sizeof st;
        sa1_frontend_state_get(fe, &st);
        CHECK(st.slot == 3 && st.phase == 0 && ev_frame_of(&log, SA1_FRONTEND_EV_PICK, NULL) < 0, "a locked first slot: slot %d, phase %d, A picks nothing", (int)st.slot, (int)st.phase);
        sa1_frontend_close(fe);
    }
    fe = fe_open(0u, 1, 6, 0x3Fu);
    if (fe != NULL) {
        memset(&st, 0, sizeof st);
        st.struct_size = sizeof st;
        sa1_frontend_state_get(fe, &st);
        CHECK(st.slot == 0 && st.slots == 6, "slot 6 in a ring of 6 starts on 0: slot %d of %d", (int)st.slot, (int)st.slots);
        sa1_frontend_close(fe);
    }
    /* the frames of a call */
    fe = fe_open(0u, 1, 0, 0xFFFFFFFFu);
    if (fe != NULL) {
        uint32_t n = 99;
        sa1_status s = sa1_frontend_tick(fe, 0.0f, NULL, &n);
        CHECK(s == SA1_OK && n == 0u, "dt 0: no frame");
        s = sa1_frontend_tick(fe, 1.0f / 120.0f, NULL, &n);
        CHECK(s == SA1_OK && n == 0u, "half a frame: none (the rest is kept)");
        s = sa1_frontend_tick(fe, 1.0f / 120.0f, NULL, &n);
        CHECK(s == SA1_OK && n == 1u, "the other half: one");
        s = sa1_frontend_tick(fe, 10.0f, NULL, &n);
        CHECK(s == SA1_OK && n == 8u, "10 seconds: 8 frames, the rest dropped (%u)", (unsigned)n);
        s = sa1_frontend_tick(fe, 0.0f, NULL, &n);
        CHECK(s == SA1_OK && n == 0u, "and no backlog afterwards");
        sa1_frontend_close(fe);
    }
}

/* test 6: B */
static void case_cancel(void)
{
    sa1_frontend *fe = fe_open(0u, 1, 0, 0xFFFFFFFFu);
    sa1_frontend_input in;
    sa1_frontend_state st;
    static ev_log log;
    int32_t f;
    uint32_t id = 0, n = 99;
    if (fe == NULL) {
        return;
    }
    log.n = 0;
    for (f = 131; f < 260; f++) {
        memset(&in, 0, sizeof in);
        in.struct_size = sizeof in;
        in.buttons = f == 230 ? SA1_FRONTEND_B_B : 0u;
        if (sa1_frontend_tick(fe, FRAME_DT, &in, &n) != SA1_OK) {
            break;
        }
        ev_collect(fe, &log);
    }
    f = ev_frame_of(&log, SA1_FRONTEND_EV_CANCEL, &id);
    CHECK(f == 239 && id == (uint32_t)-1, "B at 230: EV_CANCEL at %d (+9), id %d", (int)f, (int)id);
    CHECK(ev_has(&log, SA1_EVENT_SOUND, 3u, 230), "SE 3 at the press");
    memset(&st, 0, sizeof st);
    st.struct_size = sizeof st;
    sa1_frontend_state_get(fe, &st);
    CHECK(st.phase == 4 && n == 0u, "phase %d (4 leaving), a later tick runs %u frames", (int)st.phase, (unsigned)n);
    sa1_frontend_close(fe);
}

/* the host's pad: the stick, Start, a tap between frames, a press in a tick of several frames, the dialog's choices */
static void case_input(void)
{
    static ev_log log;
    sa1_frontend *fe = fe_open(0u, 1, 0, 0xFFFFFFFFu);
    sa1_frontend_input in;
    int32_t f;
    uint32_t id = 0, n = 0;
    memset(&in, 0, sizeof in);
    in.struct_size = sizeof in;
    if (fe != NULL) {
        log.n = 0;
        for (f = 131; f < 300; f++) {
            in.stick_x = (f >= 230 && f < 240) ? 1.0f : 0.0f;
            fe_tick1(fe, &in, f);
            ev_collect(fe, &log);
        }
        in.stick_x = 0.0f;
        f = ev_frame_of(&log, SA1_FRONTEND_EV_SELECT, &id);
        CHECK(f > 230 && id == 1u, "the stick turns the ring: EV_SELECT at %d, slot %u", (int)f, (unsigned)id);
        in.buttons = SA1_FRONTEND_B_START;
        fe_tick1(fe, &in, 300);
        in.buttons = 0;
        for (f = 301; f < 320; f++) {
            fe_tick1(fe, &in, f);
            ev_collect(fe, &log);
        }
        f = ev_frame_of(&log, SA1_FRONTEND_EV_PICK, &id);
        CHECK(f == 301 && id == 1u, "Start picks: EV_PICK at %d, slot %u", (int)f, (unsigned)id);
        sa1_frontend_close(fe);
    }
    /* a tap between two frames */
    fe = fe_open(0u, 1, 0, 0xFFFFFFFFu);
    if (fe != NULL) {
        log.n = 0;
        for (f = 131; f < 230; f++) {
            fe_tick1(fe, &in, f);
        }
        in.buttons = SA1_FRONTEND_B_RIGHT;
        CHECK(sa1_frontend_tick(fe, 0.0f, &in, &n) == SA1_OK && n == 0u, "a press without a frame");
        in.buttons = 0;
        CHECK(sa1_frontend_tick(fe, FRAME_DT, &in, &n) == SA1_OK && n == 1u, "let go, and the next frame");
        ev_collect(fe, &log);
        CHECK(ev_has(&log, SA1_EVENT_SOUND, 1u, 230), "the tap reached the frame: SE 1 at 230");
        /* a press in a tick of 8 frames goes to the first */
        log.n = 0;
        for (f = 231; f < 300; f++) {
            fe_tick1(fe, &in, f);
        }
        ev_collect(fe, &log);
        log.n = 0;
        in.buttons = SA1_FRONTEND_B_RIGHT;
        CHECK(sa1_frontend_tick(fe, 8.0f / 60.0f, &in, &n) == SA1_OK && n == 8u, "8 frames in one call");
        in.buttons = 0;
        ev_collect(fe, &log);
        CHECK(ev_has(&log, SA1_EVENT_SOUND, 1u, 300), "the press went to the first of them: SE 1 at 300");
        sa1_frontend_close(fe);
    }
    /* the dialog's other choices: the same DONE for a host (result 104) */
    fe = fe_open(SA1_FRONTEND_SHOW_DIALOG, 1, 0, 0xFFFFFFFFu);
    if (fe != NULL) {
        sa1_frontend_state st;
        log.n = 0;
        for (f = 131; f < 800; f++) {
            in.buttons = (f == 300 || f == 340) ? SA1_FRONTEND_B_A : f == 322 ? SA1_FRONTEND_B_DOWN : 0u;
            if (sa1_frontend_tick(fe, FRAME_DT, &in, &n) != SA1_OK) {
                CHECK(0, "frame %d: %s", (int)f, sa1_last_error(g_ctx));
                break;
            }
            ev_collect(fe, &log);
        }
        in.buttons = 0;
        memset(&st, 0, sizeof st);
        st.struct_size = sizeof st;
        sa1_frontend_state_get(fe, &st);
        CHECK(ev_frame_of(&log, SA1_FRONTEND_EV_PICK, NULL) > 340 && ev_frame_of(&log, SA1_FRONTEND_EV_DONE, NULL) > 640 && st.phase == 4,
              "the dialog's Instruction: EV_PICK at %d, EV_DONE at %d, phase %d", (int)ev_frame_of(&log, SA1_FRONTEND_EV_PICK, NULL),
              (int)ev_frame_of(&log, SA1_FRONTEND_EV_DONE, NULL), (int)st.phase);
        sa1_frontend_close(fe);
    }
}

/* test 7: isolation; open / close balance */
static uint32_t char_run(sa1_context *ctx, int with_frontend, int steps)
{
    sa1_character_desc d;
    sa1_character_id id = 0;
    sa1_frontend *fe = NULL;
    sa1_collision_tri t[2];
    float h = 2000.0f;
    float q[4][3] = { { -h, 0, -h }, { -h, 0, h }, { h, 0, h }, { h, 0, -h } };
    uint32_t a = 2166136261u;
    int i;
    memset(t, 0, sizeof t);
    memcpy(t[0].v[0], q[0], 12);
    memcpy(t[0].v[1], q[1], 12);
    memcpy(t[0].v[2], q[2], 12);
    memcpy(t[1].v[0], q[0], 12);
    memcpy(t[1].v[1], q[2], 12);
    memcpy(t[1].v[2], q[3], 12);
    t[0].surface = t[1].surface = SA1_SURFACE_SOLID;
    sa1_collision_set_static(ctx, t, 2);
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.kind = SA1_CHAR_SONIC;
    d.flags = SA1_CREATE_WORLD | SA1_CREATE_SA1_SPACE | SA1_CREATE_ANGLES;
    d.position[1] = 10.0f;
    if (SA1_FAILED(sa1_character_create_ex(ctx, &d, &id))) {
        printf("  create: %s\n", sa1_last_error(ctx));
        return 0;
    }
    if (with_frontend) {
        sa1_frontend_desc fd;
        memset(&fd, 0, sizeof fd);
        fd.struct_size = sizeof fd;
        fd.language = 1;
        fd.open_slots = 0xFFFFFFFFu;
        if (sa1_frontend_open(ctx, &fd, &fe) != SA1_OK) {
            printf("  frontend: %s\n", sa1_last_error(ctx));
        }
    }
    for (i = 0; i < steps; i++) {
        sa1_input in;
        sa1_state s;
        const sa1_render *r = NULL;
        uint32_t k, n;
        memset(&in, 0, sizeof in);
        in.struct_size = sizeof in;
        in.stick_y = (i / 40) & 1 ? 0.6f : 1.0f;
        in.stick_x = (i / 90) & 1 ? 0.5f : -0.5f;
        in.buttons = (i % 77) == 5 ? SA1_BUTTON_JUMP : 0u;
        memset(&s, 0, sizeof s);
        s.struct_size = sizeof s;
        if (SA1_FAILED(sa1_character_tick(ctx, id, &in, 1.0f / 30.0f, &s, NULL))) {
            printf("  tick: %s\n", sa1_last_error(ctx));
            break;
        }
        for (k = 0; k < 3u; k++) {
            a = (a ^ f2u(s.position[k])) * 16777619u;
        }
        a = (a ^ (uint32_t)s.action) * 16777619u;
        if (sa1_character_render(ctx, id, 1.0f, &r) == SA1_OK && r != NULL) {
            for (k = 0; k < 9u * r->num_triangles; k++) {
                a = (a ^ f2u(r->positions[k])) * 16777619u;
            }
        }
        if (fe != NULL) {
            sa1_frontend_input fi;
            memset(&fi, 0, sizeof fi);
            fi.struct_size = sizeof fi;
            fi.buttons = (i % 50) == 25 ? SA1_FRONTEND_B_RIGHT : 0u;
            n = 0;
            sa1_frontend_tick(fe, 1.0f / 30.0f, &fi, &n);
            sa1_frontend_poll(fe, NULL, 0u, &n);
        }
    }
    sa1_frontend_close(fe);
    sa1_character_destroy(ctx, id);
    return a;
}

static void case_isolation(void)
{
    sa1_context *c = mkctx(1, 1);
    uint32_t a, b;
    int i;
    int64_t live0 = 0, live1 = 0;
    sa1_frontend *fe;
    if (c == NULL) {
        CHECK(0, "a counting context");
        return;
    }
    a = char_run(c, 0, 400);
    b = char_run(c, 1, 400);
    CHECK(a != 0 && a == b, "a Sonic world beside an open front end: bit-equal state and draws over 400 pairs (%08X, %08X)", (unsigned)a, (unsigned)b);
    /* 20 opens */
    for (i = 0; i < 20; i++) {
        sa1_frontend_desc fd;
        memset(&fd, 0, sizeof fd);
        fd.struct_size = sizeof fd;
        fd.language = 1;
        fd.open_slots = 0xFFFFFFFFu;
        fe = NULL;
        if (sa1_frontend_open(c, &fd, &fe) != SA1_OK) {
            CHECK(0, "open %d: %s", i, sa1_last_error(c));
            break;
        }
        sa1_frontend_close(fe);
        if (i == 0) {
            live0 = g_live;         /* the archives stay until the disc is unloaded */
        }
    }
    live1 = g_live;
    CHECK(live0 == live1, "20 opens and closes: live allocations %lld, then %lld", (long long)live0, (long long)live1);
    /* one open is one open */
    {
        sa1_frontend_desc fd;
        sa1_frontend *fe2 = NULL;
        sa1_status st;
        memset(&fd, 0, sizeof fd);
        fd.struct_size = sizeof fd;
        fd.language = 1;
        fd.open_slots = 0xFFFFFFFFu;
        fe = NULL;
        st = sa1_frontend_open(c, &fd, &fe);
        CHECK(st == SA1_OK && fe != NULL, "open");
        st = sa1_frontend_open(c, &fd, &fe2);
        CHECK(st == SA1_ERR_LIMIT && fe2 == NULL, "a second open: SA1_ERR_LIMIT (%s)", sa1_status_string(st));
        /* the disc going away closes it (the host's pointer is dead then: the module clears it in its own hook) */
        sa1_unload_disc(c);
        st = sa1_frontend_open(c, &fd, &fe2);
        CHECK(st == SA1_ERR_NOT_LOADED, "no disc: SA1_ERR_NOT_LOADED (%s)", sa1_status_string(st));
    }
    sa1_shutdown(c);
    CHECK(g_live == 0, "everything freed at the shutdown (%lld live blocks)", (long long)g_live);
    /* a shutdown with one open */
    c = mkctx(1, 1);
    if (c != NULL) {
        sa1_frontend_desc fd;
        memset(&fd, 0, sizeof fd);
        fd.struct_size = sizeof fd;
        fd.language = 1;
        fd.open_slots = 0xFFFFFFFFu;
        fe = NULL;
        CHECK(sa1_frontend_open(c, &fd, &fe) == SA1_OK, "open");
        sa1_shutdown(c);
        CHECK(g_live == 0, "a shutdown closes an open front end (%lld live blocks)", (long long)g_live);
    }
}

/* arguments, the archives */
static void case_api(void)
{
    sa1_frontend_desc d;
    sa1_frontend *fe = NULL;
    sa1_frontend_state st;
    sa1_frontend_input in;
    const sa1_screen *scr = NULL;
    const sa1_render *rd = NULL;
    sa1_status s;
    uint32_t a, n = 0, total = 0;
    int32_t f;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.language = 1;
    d.open_slots = 0xFFFFFFFFu;
    CHECK(sa1_frontend_open(NULL, &d, &fe) == SA1_ERR_INVALID_ARGUMENT, "open: no context");
    CHECK(sa1_frontend_open(g_ctx, NULL, &fe) == SA1_ERR_INVALID_ARGUMENT, "open: no desc");
    CHECK(sa1_frontend_open(g_ctx, &d, NULL) == SA1_ERR_INVALID_ARGUMENT, "open: no out");
    d.slot = 7;
    CHECK(sa1_frontend_open(g_ctx, &d, &fe) == SA1_ERR_INVALID_ARGUMENT, "open: slot 7");
    d.slot = 0;
    d.language = 5;
    CHECK(sa1_frontend_open(g_ctx, &d, &fe) == SA1_ERR_INVALID_ARGUMENT, "open: language 5");
    d.language = 1;
    d.flags = 0x80u;
    CHECK(sa1_frontend_open(g_ctx, &d, &fe) == SA1_ERR_INVALID_ARGUMENT, "open: an unknown flag");
    d.flags = 0;
    sa1_frontend_close(NULL);
    n = 7;
    CHECK(sa1_frontend_tick(NULL, 1.0f, NULL, &n) == SA1_ERR_INVALID_ARGUMENT && n == 0u, "tick: NULL");
    memset(&st, 0, sizeof st);
    st.struct_size = sizeof st;
    CHECK(sa1_frontend_state_get(NULL, &st) == SA1_ERR_INVALID_ARGUMENT, "state: NULL");
    CHECK(sa1_frontend_poll(NULL, NULL, 0u, &n) == SA1_ERR_INVALID_ARGUMENT, "poll: NULL");
    CHECK(sa1_frontend_screen(NULL, &scr) == SA1_ERR_INVALID_ARGUMENT, "screen: NULL");
    CHECK(sa1_frontend_render(NULL, &rd, &f) == SA1_ERR_INVALID_ARGUMENT, "render: NULL");
    CHECK(sa1_frontend_view(NULL, NULL, NULL, NULL, NULL) == SA1_ERR_INVALID_ARGUMENT, "view: NULL");
    {
        double t0 = tu_now_ms();
        fe = fe_open(0u, 1, 0, 0xFFFFFFFFu);
        NOTE("sa1_frontend_open: %.1f ms (the archives loaded %s)", tu_now_ms() - t0, "by an earlier case, or here");
    }
    if (fe == NULL) {
        return;
    }
    s = sa1_texture_archive(g_ctx, SA1_KIND_FRONTEND, 0u, NULL, &n);
    CHECK(s == SA1_OK, "sa1_texture_archive(SA1_KIND_FRONTEND): %s", sa1_status_string(s));
    for (a = 0; ; a++) {
        const char *name = NULL;
        n = 0;
        if (sa1_texture_archive(g_ctx, SA1_KIND_FRONTEND, a, &name, &n) != SA1_OK) {
            break;
        }
        total += n;
        NOTE("archive %u: %s, %u textures", (unsigned)a, name, (unsigned)n);
    }
    CHECK(a == 9u && total == 67u, "the menu's nine archives: %u, %u textures (the English ones: 67)", (unsigned)a, (unsigned)total);
    /* a fault: the master task's exec to an address without code */
    {
        sa1fe_test_info ti;
        uint32_t tp;
        sa1fe_test_get(fe, &ti);
        tp = wrd((const sa1g_world *)ti.world, 0x8C9610CCu);
        sa1g_world_wr32((sa1g_world *)ti.world, tp + 0x10u, 0x8C000004u);
        memset(&in, 0, sizeof in);
        in.struct_size = sizeof in;
        s = sa1_frontend_tick(fe, FRAME_DT, &in, &n);
        CHECK(s == SA1_ERR_GAME_FAULT && sa1_last_error(g_ctx)[0] != 0, "a dead world: tick says SA1_ERR_GAME_FAULT: %s", sa1_last_error(g_ctx));
        memset(&st, 0, sizeof st);
        st.struct_size = sizeof st;
        s = sa1_frontend_state_get(fe, &st);
        CHECK(s == SA1_ERR_GAME_FAULT && st.faults != 0u, "the state too, with faults %u", (unsigned)st.faults);
        CHECK(sa1_frontend_screen(fe, &scr) == SA1_ERR_GAME_FAULT && scr == NULL, "the screen too");
        CHECK(sa1_frontend_render(fe, &rd, &f) == SA1_ERR_GAME_FAULT && rd == NULL, "the render too");
        sa1_frontend_close(fe);
    }
    /* the context goes on */
    fe = fe_open(0u, 1, 0, 0xFFFFFFFFu);
    CHECK(fe != NULL, "the next open after a fault");
    sa1_frontend_close(fe);
}

/* test 9: Japanese */
static void case_japanese(void)
{
    sa1_frontend *fe = fe_open(0u, 0, 0, 0xFFFFFFFFu);
    sa1_frontend_input in;
    sa1fe_test_info ti;
    const sa1_screen *scr = NULL;
    const char *name = NULL;
    uint32_t a, n, total = 0;
    int32_t f;
    int chsel = 0;
    if (fe == NULL) {
        return;
    }
    memset(&in, 0, sizeof in);
    in.struct_size = sizeof in;
    for (f = 131; f < 300; f++) {
        if (!fe_tick1(fe, &in, f)) {
            break;
        }
    }
    for (a = 0; sa1_texture_archive(g_ctx, SA1_KIND_FRONTEND, a, &name, &n) == SA1_OK; a++) {
        total += n;
        chsel |= !strcmp(name, "AVA_CHSEL");
    }
    sa1_frontend_screen(fe, &scr);
    sa1fe_test_get(fe, &ti);
    CHECK(a == 9u && chsel && scr != NULL && scr->num_draws > 10u && ti.screen_misses == 0u,
          "language 0 (the first run of it): 9 archives without _E (%u textures), %u draws, %u misses", (unsigned)total, scr ? (unsigned)scr->num_draws : 0u,
          (unsigned)ti.screen_misses);
    sa1_frontend_close(fe);
    /* and back to English: the set is loaded again */
    fe = fe_open(0u, 1, 0, 0xFFFFFFFFu);
    if (fe != NULL) {
        total = 0;
        for (a = 0; sa1_texture_archive(g_ctx, SA1_KIND_FRONTEND, a, &name, &n) == SA1_OK; a++) {
            total += n;
        }
        CHECK(total == 67u, "English again: %u textures", (unsigned)total);
        sa1_frontend_close(fe);
    }
}

/* the rt in a character world (review R98.9; E-102's effect display 8C050540 reaches 8C63ED08 there): recording only: outside a window nothing at
 * all, inside one two records in the world's capture block, and no word of the arena either way */
typedef struct rt_job {
    uint32_t vtx;
} rt_job;

static void rt_call_cb(sa1g_world *w, void *user)
{
    uint32_t g = ((rt_job *)user)->vtx;
    (void)w;
    ((void (*)(void *, int32_t, uint32_t, int32_t))sa1_fn(SA1_GFN_C(0x8C63ED08u), 0u))(sa1_decode(g), 4, 0x00012345u, 1);
    ((void (*)(void *, int32_t, int32_t))sa1_fn(SA1_GFN_C(0x8C63EC58u), 0u))(sa1_decode(g), 4, 0);
}

static uint64_t arena_hash(const sa1g_world *w)
{
    const uint8_t *p = (const uint8_t *)sa1g_world_ptr(w, 0x8C000000u);
    uint64_t h = 14695981039346656037ULL;
    uint32_t i;
    for (i = 0; i < 0x01000000u; i += 4u) {
        uint32_t v;
        memcpy(&v, p + i, 4);
        h = (h ^ v) * 1099511628211ULL;
    }
    return h;
}

/* the capture block lies in segment 0x18 */
static uint32_t cap_total(const sa1g_world *w, uint32_t block)
{
    const uint8_t *h = (const uint8_t *)sa1g_world_ptr(w, block);
    return h != NULL ? sa1_rd32(h + offsetof(sa1_cap_hdr, total)) : 0u;
}

static void case_rt(void)
{
    sa1_context *ctx = g_ctx;
    sa1_character_desc d;
    sa1_character_id id = 0;
    sa1_collision_tri t[2];
    float h = 2000.0f;
    float q[4][3] = { { -h, 0, -h }, { -h, 0, h }, { h, 0, h }, { h, 0, -h } };
    sa1_input in;
    sa1g_world *w;
    sa1_capture *c;
    sa1_capture_stats cst;
    const sa1_cap_rec *recs = NULL;
    uint8_t *vtx = NULL;
    rt_job j;
    uint32_t total0, total1, total2, n, k;
    uint64_t h0, h1, h2;
    int i;
    memset(t, 0, sizeof t);
    memcpy(t[0].v[0], q[0], 12);
    memcpy(t[0].v[1], q[1], 12);
    memcpy(t[0].v[2], q[2], 12);
    memcpy(t[1].v[0], q[0], 12);
    memcpy(t[1].v[1], q[2], 12);
    memcpy(t[1].v[2], q[3], 12);
    t[0].surface = t[1].surface = SA1_SURFACE_SOLID;
    sa1_collision_set_static(ctx, t, 2);
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.kind = SA1_CHAR_GAMMA;
    d.flags = SA1_CREATE_WORLD | SA1_CREATE_SA1_SPACE | SA1_CREATE_ANGLES;
    d.position[1] = 10.0f;
    if (SA1_FAILED(sa1_character_create_ex(ctx, &d, &id))) {
        CHECK(0, "E-102: %s", sa1_last_error(ctx));
        return;
    }
    memset(&in, 0, sizeof in);
    in.struct_size = sizeof in;
    for (i = 0; i < 10; i++) {
        sa1_character_tick(ctx, id, &in, 1.0f / 30.0f, NULL, NULL);
    }
    w = sa1_character_world(ctx, id);
    c = sa1_character_capture(ctx, id);
    if (w == NULL || c == NULL) {
        CHECK(0, "E-102's world and capture");
        sa1_character_destroy(ctx, id);
        return;
    }
    j.vtx = sa1g_world_ext_alloc(w, 0x60u, 4u, (void **)&vtx);
    if (j.vtx == 0 || vtx == NULL) {
        CHECK(0, "room for the vertices");
        sa1_character_destroy(ctx, id);
        return;
    }
    for (k = 0; k < 0x60u; k += 4u) {
        float fv = (float)k;
        memcpy(vtx + k, &fv, 4);
    }
    sa1_capture_get_stats(c, &cst);
    total0 = cap_total(w, cst.block);
    h0 = arena_hash(w);
    CHECK(sa1g_world_call(w, rt_call_cb, &j) == SA1_OK, "8C63ED08, 8C63EC58 outside a window");
    total1 = cap_total(w, cst.block);
    h1 = arena_hash(w);
    CHECK(total0 != 0u, "the capture block answers (%u records so far)", (unsigned)total0);
    CHECK(total1 == total0 && h1 == h0, "outside a window: no record (%u, %u), the arena unchanged", (unsigned)total0, (unsigned)total1);
    sa1_capture_open(c);
    CHECK(sa1g_world_call(w, rt_call_cb, &j) == SA1_OK, "inside a window");
    sa1_capture_close(c);
    total2 = cap_total(w, cst.block);
    h2 = arena_hash(w);
    CHECK(total2 == total1 + 2u && h2 == h0, "inside a window: two records (%u -> %u), the arena unchanged", (unsigned)total1, (unsigned)total2);
    n = sa1_capture_records(c, &recs);
    if (n == 2u) {
        const uint8_t *p0 = sa1_capture_payload(c, &recs[0], SA1_CAP_VTX2D_HEAD + 4u * SA1_CAP_TEX2D_VTX);
        const uint8_t *p1 = sa1_capture_payload(c, &recs[1], SA1_CAP_VTX2D_HEAD + 4u * SA1_CAP_POLY2D_V_VTX);
        CHECK(recs[0].kind == SA1_CAP_TEX2D && recs[1].kind == SA1_CAP_POLY2D_V && recs[0].attr == 1u && recs[1].attr == 0u && p0 != NULL && p1 != NULL &&
              sa1_rd32(p0) == 4u && sa1_rd32(p0 + 4) == 0x00012345u && sa1_rd32(p1) == 4u && memcmp(p0 + SA1_CAP_VTX2D_HEAD, vtx, 4u * SA1_CAP_TEX2D_VTX) == 0 &&
              memcmp(p1 + SA1_CAP_VTX2D_HEAD, vtx, 4u * SA1_CAP_POLY2D_V_VTX) == 0,
              "the records: kinds 14, 15, n 4, the global index, trans, the vertices as drawn");
    } else {
        CHECK(0, "%u records", (unsigned)n);
    }
    /* the next tick has its own snapshot, and the world goes on */
    CHECK(sa1_character_tick(ctx, id, &in, 1.0f / 30.0f, NULL, NULL) == SA1_OK, "E-102 ticks on");
    sa1_character_destroy(ctx, id);
}

/* what a host pays per frame: the frame, the screen, the render */
static void case_cost(void)
{
    sa1_frontend *fe = fe_open(0u, 1, 0, 0xFFFFFFFFu);
    sa1_frontend_input in;
    double t0, ttick = 0, tscr = 0, trnd = 0, tmax = 0;
    const sa1_screen *scr = NULL;
    const sa1_render *rd = NULL;
    int32_t f, kind;
    if (fe == NULL) {
        return;
    }
    memset(&in, 0, sizeof in);
    in.struct_size = sizeof in;
    for (f = 131; f < 731; f++) {
        double a, b;
        in.buttons = (f % 60) == 0 ? SA1_FRONTEND_B_RIGHT : 0u;
        t0 = tu_now_ms();
        fe_tick1(fe, &in, f);
        a = tu_now_ms();
        sa1_frontend_screen(fe, &scr);
        b = tu_now_ms();
        sa1_frontend_render(fe, &rd, &kind);
        ttick += a - t0;
        tscr += b - a;
        trnd += tu_now_ms() - b;
        a = tu_now_ms() - t0;
        tmax = a > tmax ? a : tmax;
    }
    NOTE("600 frames with the ring turning: frame %.3f ms, screen %.3f ms, render %.3f ms (mean), one frame with all three at most %.3f ms", ttick / 600.0,
         tscr / 600.0, trnd / 600.0, tmax);
    CHECK((ttick + tscr + trnd) / 600.0 < 2.0, "a frame with its screen and render under 2 ms (mean %.3f ms)", (ttick + tscr + trnd) / 600.0);
    sa1_frontend_close(fe);
}

/* the gate (optimised archives before the final gate) */
/* ---- round 1.0.0-beta1 (sadc docs/research/gsa1_100_select.md 7.1 F1, F2): the slots' skins, the upgrade models, the pulse; Down at idle ------------- */
static const uint16_t k_fk_metal_keep[9] = { 76, 77, 97, 98, 99, 100, 101, 102, 103 };
static const sa1_skin_part k_fk_metal_parts[25] = {
    {1, 10, -1, -1}, {12, 22, -1, -1}, {12, 22, -1, -1},
    {25, 34, -1, -1}, {25, 34, 5, 27}, {27, 29, 6, 30}, {30, 30, -1, -1}, {30, 30, -1, -1}, {30, 30, -1, -1},
    {36, 45, -1, -1}, {36, 45, 11, 38}, {38, 40, 12, 41}, {41, 41, -1, -1}, {41, 41, -1, -1}, {41, 41, -1, -1},
    {47, 55, 16, 49}, {49, 51, 18, 52}, {49, 51, -1, -1}, {52, 52, -1, -1}, {52, 52, -1, -1},
    {57, 65, 21, 59}, {59, 61, 23, 62}, {59, 61, -1, -1}, {62, 62, -1, -1}, {62, 62, -1, -1},
};
static const uint16_t k_fk_mecha_keep[22] = { 7, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96 };
static const sa1_skin_part k_fk_mecha_parts[20] = {
    {1, 10, -1, -1}, {12, 22, -1, -1}, {12, 22, -1, -1}, {1, 10, -1, -1},
    {47, 55, 5, 49}, {49, 51, 6, 52}, {52, 52, -1, -1}, {52, 52, -1, -1}, {52, 52, -1, -1},
    {25, 34, 10, 27}, {27, 29, 11, 30}, {30, 30, -1, -1},
    {57, 65, 13, 59}, {59, 61, 14, 62}, {62, 62, -1, -1}, {62, 62, -1, -1}, {62, 62, -1, -1},
    {36, 45, 18, 38}, {38, 40, 19, 41}, {41, 41, -1, -1},
};

static void fk_skin(sa1_skin_desc *d, int mecha)
{
    memset(d, 0, sizeof *d);
    d->struct_size = sizeof *d;
    d->model_file = "ADV02.PRS";
    d->model_sha1 = "ab5f600e707681f538794520c296c4fefe84659c";
    d->model_load = 0x0C900000u;
    d->tex_file = "ADV_MR03.PRS";
    d->tex_sha1 = "cca1620491391e3d6e44d83be478d0687d5fa14f";
    d->turn = SA1_SKIN_TURN_MINUS_Z;
    d->lift = 1.0f;
    if (mecha) {
        d->model_first = 0x0CA824C0u;
        d->model_end = 0x0CA88AF8u;
        d->model_root = 0x0CA88AC4u;
        d->tex_keep = k_fk_mecha_keep;
        d->num_tex_keep = 22;
        d->scale = 0.52f;
        d->ball_texture = 86;
        d->parts = k_fk_mecha_parts;
        d->num_parts = 20;
        d->id = "mechasonic";
    } else {
        d->model_first = 0x0CA8B9CCu;
        d->model_end = 0x0CA92EB8u;
        d->model_root = 0x0CA92E84u;
        d->tex_keep = k_fk_metal_keep;
        d->num_tex_keep = 9;
        d->scale = 0.70f;
        d->ball_texture = 101;
        d->parts = k_fk_metal_parts;
        d->num_parts = 25;
        d->id = "metalsonic";
    }
}

/* one frame's screen, render and events, hashed */
typedef struct fk_frame {
    uint32_t screen, render, ntris, nev;
    int32_t ring, game_ring, slot, phase;
} fk_frame;

static uint32_t fk_hash_screen(const sa1_screen *s)
{
    uint32_t keep = g_det, h;
    g_det = 2166136261u;
    det_screen(s);
    h = g_det;
    g_det = keep;
    return h;
}

static uint32_t fk_hash_render(const sa1_render *r)
{
    uint32_t keep = g_det, h, i;
    g_det = 2166136261u;
    det_render(r);
    for (i = 0; i < r->num_draws; i++) {
        det_add(r->draws[i].flags);
        det_add((uint32_t)r->draws[i].archive);
    }
    h = g_det;
    g_det = keep;
    return h;
}

/* the game's ADV_SELWK.ring: *D_8C9613A4 -> awp (+0x2C) -> +0x18 */
static int32_t fk_game_ring(const sa1g_world *w)
{
    uint32_t tp = wrd(w, 0x8C9613A4u), awp = tp ? wrd(w, tp + 0x2Cu) : 0u;
    return awp ? (int32_t)wrd(w, awp + 0x18u) : -1;
}

/* mode 0: no change; 1: F1's skins and display sets; 2: Down at frame 230 (idle); 3: Up at frame 230 */
static int fk_run(int mode, int32_t frames, fk_frame *out, ev_log *log)
{
    sa1_frontend *fe = fe_open(0u, 1, 0, 0xFFFFFFFFu);
    sa1_frontend_input in;
    sa1_skin_desc metal, mecha;
    sa1fe_test_info ti;
    int32_t f;
    int ok = 1;
    fk_skin(&metal, 0);
    fk_skin(&mecha, 1);
    if (fe == NULL) {
        return 0;
    }
    memset(&in, 0, sizeof in);
    in.struct_size = sizeof in;
    log->n = 0;
    for (f = 131; f < 131 + frames; f++) {
        const sa1_screen *scr = NULL;
        const sa1_render *r = NULL;
        sa1_frontend_state st;
        int32_t kind = 0;
        uint32_t n0 = log->n;
        in.buttons = 0u;
        if (mode == 1) {
            sa1_status s = SA1_OK;
            if (f == 140) {
                uint32_t k;
                static const int32_t kinds[6] = { SA1_CHAR_SONIC, SA1_CHAR_TAILS, SA1_CHAR_KNUCKLES, SA1_CHAR_AMY, SA1_CHAR_GAMMA, SA1_CHAR_BIG };
                for (k = 0; k < 6u; k++) {
                    s = sa1_frontend_set_upgrade_models(fe, kinds[k], SA1_UPGRADE_MODELS_ON | SA1_UPGRADE_MODELS_ALL);
                    if (s != SA1_OK) {
                        printf("  set_upgrade_models(%d): %s\n", (int)kinds[k], sa1_last_error(g_ctx));
                        ok = 0;
                    }
                }
            }
            if (f == 150) s = sa1_frontend_set_skin(fe, &metal);
            if (f == 170) s = sa1_frontend_set_slot_skin(fe, 0, &mecha);
            if (f == 190) s = sa1_frontend_set_slot_skin(fe, 0, NULL);
            if (f == 200) s = sa1_frontend_set_upgrade_models(fe, SA1_CHAR_SONIC, SA1_UPGRADE_MODELS_ON | SA1_UPGRADE_LIGHT_SPEED_SHOES | SA1_UPGRADE_CRYSTAL_RING);
            if (f == 230) s = sa1_frontend_set_upgrade_models(fe, SA1_CHAR_SONIC, 0u);
            if (s != SA1_OK) {
                printf("  frame %d: %s\n", (int)f, sa1_last_error(g_ctx));
                ok = 0;
            }
        } else if ((mode == 2 && f == 230) || (mode == 3 && f == 230)) {
            in.buttons = mode == 2 ? SA1_FRONTEND_B_DOWN : SA1_FRONTEND_B_UP;
        }
        if (!fe_tick1(fe, &in, f)) {
            ok = 0;
            break;
        }
        ev_collect(fe, log);
        memset(&out[f - 131], 0, sizeof out[0]);
        out[f - 131].nev = log->n - n0;
        if (sa1_frontend_screen(fe, &scr) == SA1_OK && scr != NULL) {
            out[f - 131].screen = fk_hash_screen(scr);
        }
        if (sa1_frontend_render(fe, &r, &kind) == SA1_OK && r != NULL) {
            out[f - 131].render = fk_hash_render(r);
            out[f - 131].ntris = r->num_triangles;
        }
        memset(&st, 0, sizeof st);
        st.struct_size = sizeof st;
        sa1_frontend_state_get(fe, &st);
        out[f - 131].ring = st.ring;
        out[f - 131].slot = st.slot;
        out[f - 131].phase = st.phase;
        sa1fe_test_get(fe, &ti);
        out[f - 131].game_ring = fk_game_ring(ti.world);
    }
    if (mode == 1) {
        CHECK(sa1_frontend_set_slot_skin(fe, 1, &metal) == SA1_ERR_UNSUPPORTED && sa1_frontend_set_slot_skin(fe, 7, NULL) == SA1_ERR_INVALID_ARGUMENT &&
              sa1_frontend_set_upgrade_models(fe, SA1_CHAR_EGGMAN, 0u) == SA1_ERR_INVALID_ARGUMENT &&
              sa1_frontend_set_upgrade_models(fe, SA1_CHAR_SONIC, SA1_UPGRADE_LIGHT_SPEED_SHOES) == SA1_ERR_INVALID_ARGUMENT,
              "F1 a skin on another character's slot: SA1_ERR_UNSUPPORTED; slot 7, Eggman's upgrades, bits without SA1_UPGRADE_MODELS_ON: SA1_ERR_INVALID_ARGUMENT");
    }
    sa1_frontend_close(fe);
    return ok;
}

static int fk_events_same(const ev_log *a, const ev_log *b)
{
    uint32_t i;
    if (a->n != b->n) {
        return 0;
    }
    for (i = 0; i < a->n; i++) {
        if (a->ev[i].type != b->ev[i].type || a->ev[i].id != b->ev[i].id || a->ev[i].arg != b->ev[i].arg || a->ev[i].step != b->ev[i].step) {
            return 0;
        }
    }
    return 1;
}

static void case_skins(void)
{
    enum { N = 140 };
    static fk_frame a[N], b[N];
    static ev_log la, lb;
    int32_t f, scr_diff = 0, ring_bad = 0, ring_moves = 0, shown_diff = 0, back_same = 0, skin_diff = 0, nev = 0;
    if (!fk_run(0, N, a, &la) || !fk_run(1, N, b, &lb)) {
        CHECK(0, "F1 the two runs");
        return;
    }
    for (f = 0; f < N; f++) {
        scr_diff += a[f].screen != b[f].screen || a[f].nev != b[f].nev;
        ring_bad += a[f].ring != a[f].game_ring || b[f].ring != b[f].game_ring;
        ring_moves += f > 0 && a[f].ring != a[f - 1].ring;
        nev += (int32_t)a[f].nev;
    }
    for (f = 150 - 131 + 2; f < 190 - 131; f++) {
        skin_diff += a[f].render != b[f].render;
    }
    for (f = 200 - 131 + 2; f < 230 - 131; f++) {
        shown_diff += a[f].render != b[f].render || a[f].ntris != b[f].ntris;
    }
    for (f = 230 - 131 + 2; f < N; f++) {
        back_same += a[f].render == b[f].render;
    }
    CHECK(scr_diff == 0 && fk_events_same(&la, &lb) && nev > 0, "F1 Sonic > Metal > Mecha > Sonic on slot 0 and every kind's display set: the 2D draws of all %d frames and the %u events the same as without (%d frames differ)",
          N, (unsigned)la.n, (int)scr_diff);
    CHECK(skin_diff >= 36, "F1 the skins drew in the circle: %d of 38 frames' 3D differ from Sonic's", (int)skin_diff);
    CHECK(shown_diff >= 26, "F1 slot 0 with the Light Speed Shoes and the Crystal Ring shown: %d of 28 frames' 3D differ (their models)", (int)shown_diff);
    CHECK(back_same >= N - (230 - 131 + 2) - 1, "F1 the display set back to 0: the game's 3D again (%d of %d frames the same)", (int)back_same, (int)(N - (230 - 131 + 2)));
    CHECK(ring_bad == 0 && ring_moves > 20, "F1 sa1_frontend_state.ring is the game's ADV_SELWK.ring (+0x18) in every frame (%d off), and it moves (%d)", (int)ring_bad, (int)ring_moves);
}

static void case_idle_down(void)
{
    enum { N = 200 };
    static fk_frame a[N], b[N], c[N];
    static ev_log la, lb, lc;
    int32_t f, diff_down = 0, diff_up = 0;
    if (!fk_run(0, N, a, &la) || !fk_run(2, N, b, &lb) || !fk_run(3, N, c, &lc)) {
        CHECK(0, "F2 the three runs");
        return;
    }
    for (f = 0; f < N; f++) {
        diff_down += a[f].screen != b[f].screen || a[f].render != b[f].render || a[f].ring != b[f].ring || a[f].slot != b[f].slot || a[f].phase != b[f].phase;
        diff_up += a[f].screen != c[f].screen || a[f].render != c[f].render || a[f].ring != c[f].ring || a[f].slot != c[f].slot || a[f].phase != c[f].phase;
    }
    CHECK(diff_down == 0 && fk_events_same(&la, &lb), "F2 Down at idle (frame 230, phase %d) does nothing in the game (RM.5's method: what it draws): the 2D, the 3D, the slot, the phase and the ring of %d frames and every event the same as without (%d differ)",
          (int)a[230 - 131].phase, N, (int)diff_down);
    CHECK(diff_up == 0 && fk_events_same(&la, &lc), "F2 Up at idle the same: %d frames differ", (int)diff_up);
}

static int gate_closed(void)
{
    double t0;
    sa1_frontend_desc d;
    sa1_frontend *fe = NULL;
    sa1_status st;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.language = 1;
    d.open_slots = 0xFFFFFFFFu;
    t0 = tu_now_ms();
    st = sa1_frontend_open(g_ctx, &d, &fe);
    t0 = tu_now_ms() - t0;
    if (st == SA1_OK) {
        NOTE("the first sa1_frontend_open (the disc read, the nine archives, the world, 131 frames): %.1f ms", t0);
        sa1_frontend_close(fe);
    }
    return st == SA1_ERR_UNSUPPORTED;
}

static const struct { const char *name; void (*fn)(void); } k_tests[] = {
    { "api", case_api },
    { "references", case_references },
    { "render", case_render },
    { "skip", case_skip },
    { "slots", case_slots },
    { "rt", case_rt },
    { "cancel", case_cancel },
    { "input", case_input },
    { "japanese", case_japanese },
    { "cost", case_cost },
    { "isolation", case_isolation },
    { "skins", case_skins },            /* round 1.0.0-beta1, select F1 */
    { "idle_down", case_idle_down },    /* round 1.0.0-beta1, select F2 */
};

static int selected(const char *only, const char *name)
{
    const char *p = only;
    size_t n = strlen(name);
    if (only == NULL) {
        return 1;
    }
    while (p != NULL && *p != 0) {
        const char *e = strchr(p, ',');
        size_t l = e != NULL ? (size_t)(e - p) : strlen(p);
        if (l == n && strncmp(p, name, n) == 0) {
            return 1;
        }
        p = e != NULL ? e + 1 : NULL;
    }
    return 0;
}

int main(int argc, char **argv)
{
    const char *only = NULL;
    size_t i;
    int a;
    double t0;
    ga_args(&argc, argv);
    snprintf(g_sadc, sizeof g_sadc, "%s", "../sadc");
    for (a = 1; a < argc; a++) {
        if (!strcmp(argv[a], "--verbose")) {
            g_verbose = 1;
        } else if (!strcmp(argv[a], "--only") && a + 1 < argc) {
            only = argv[++a];
        } else if (!strcmp(argv[a], "--extra") && a + 1 < argc) {
            g_extra = argv[++a];
        } else if (!strcmp(argv[a], "--sadc") && a + 1 < argc) {
            snprintf(g_sadc, sizeof g_sadc, "%s", argv[++a]);
        } else if (!strcmp(argv[a], "--list")) {
            for (i = 0; i < sizeof k_tests / sizeof k_tests[0]; i++) {
                printf("%s\n", k_tests[i].name);
            }
            return 0;
        } else {
            g_gdi = argv[a];
        }
    }
    if (g_gdi == NULL) {
        g_gdi = getenv("SA1_TEST_GDI");
    }
    if (g_gdi == NULL) {
        printf("frontend_test: needs the disc (GDI argument or SA1_TEST_GDI)\n");
        return 2;
    }
    g_ctx = mkctx(0, 1);
    if (g_ctx == NULL) {
        return 1;
    }
    printf("libsa1 %s (SA1_API_FRONTEND %d)\n", sa1_version_string(), SA1_API_FRONTEND);
    if (gate_closed()) {
        printf("frontend_test: SKIP behaviour: frontend is not built yet (the optimised archive's gate is closed: run --debug)\n");
        sa1_shutdown(g_ctx);
        return 0;
    }
    g_det = 2166136261u;
    if (g_extra != NULL) {
        case_extra();
        sa1_shutdown(g_ctx);
        printf("frontend_test: %d ok, %d failed", g_ok, g_fail);
        puts("");
        return g_fail ? 1 : 0;
    }
    for (i = 0; i < sizeof k_tests / sizeof k_tests[0]; i++) {
        if (!selected(only, k_tests[i].name)) {
            continue;
        }
        t0 = tu_now_ms();
        k_tests[i].fn();
        printf("  (%s: %.1f s)\n", k_tests[i].name, (tu_now_ms() - t0) / 1000.0);
    }
    printf("DETERMINISM_FRONTEND %08X (every compared screen, the render of each slot)\n", (unsigned)g_det);
    sa1_shutdown(g_ctx);
    printf("frontend_test: %d ok, %d failed\n", g_ok, g_fail);
    return g_fail ? 1 : 0;
}
