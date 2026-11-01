/*
 * capture_test.c - the character mesh from the game's own draws (sadc docs/libsa1_integration.md 7.2, D10) and the
 * event queue (7.4), against the reference traces and a private harness run.
 *
 *   capture_test [--harness DIR] [--frames N] [--obj DIR] [--part LIST] SADC [GDI]   (LIST: 1-9, lsd, alpha, powerup,
 *   exec, turn, object, boxrec; e.g. 7,lsd)
 *   (GDI: or $SA1_TEST_GDI. DIR: the output of a private pure interpreter build of SADC's harness, made with
 *    harness/build/sa1h_<tag>.exe trace --level 1 --act 0 --no-objects --input @harness/tests/sim/<NAME>.txt
 *        --out DIR/<NAME> --log-sdk DIR/<NAME>.sdk --sdk-list DIR/events.lst --check build/traces/ref/<NAME>.bin
 *        [--dump-ram-at 30,50,...,250 for ec1_run_jump]
 *    for NAME = ec1_run_jump, ec1_jump_hold, ec1_spindash; events.lst = the "event" stubs of gen/policy/stubs.tsv
 *    as "ADDR NAME SIG" lines. Without --harness, parts 3 and 4 are skipped.)
 *
 * Build from LIB after `py -3.12 build.py --target x86_64-windows` (the world tests' flags):
 *   py -3.12 -m ziglang cc -target x86_64-windows-gnu -std=gnu11 -Iinclude -Isrc -Isrc/world -I<SADC>/include
 *       -I<SADC>/harness/src -Wall -Wextra -O2 -g -ffp-contract=off -frounding-math -fno-builtin -DSA1_STATIC
 *       tests/capture_test.c build/x86_64-windows/libsa1.a -o build/x86_64-windows/capture_test.exe
 *
 * 1. Capture adds recording only: every reference trace whose landtable dump exists is replayed twice (V6, the
 *    record's pad / rand / XMTRX / camera injected, like tests/replay_test.c), without and with the capture window
 *    open around every step. The sphere visibility test 8C038D00 is the original's (rt_draw.c section 6: game logic,
 *    the same answer with and without a window; until 2026-10-20 it answered "visible" inside a window only, and
 *    Tails' and Knuckles' records then differed): all six characters' records (decoded fields, raw blobs, v2 scalars)
 *    and events must be identical, no world may fault, and the displays that ask the test (Sonic, Tails, Knuckles,
 *    Big) must be visible on sub step 0 as on the Dreamcast (the references since 2026-10-20 have the screen).
 * 2. 60 frames of ec1_run_jump (sub step 0, steps 30, 34, ... 266): each capture must be exactly the visible nodes of
 *    the drawn action's object as the arena holds it, in the motion walk 8C040784's order (the display's own choice
 *    of action: reqaction, or 32, the jump ball, while spinning on the frames pwp->free.w[1] & 0x11 picks), the blob
 *    shadow aside; then, node for node, against the CPU pose evaluator (sa1_character_pose of a puppet at the same
 *    action and frame): UVs / textures / materials / colours, and the corners relative to the root (the first
 *    unchanged node's draw maps the evaluator's world onto the capture's: the display draws at the collision centre
 *    and tilts by ang.x / ang.z) with and without the evaluator's welds; nodes the game changes at run time (the
 *    head morph copy, the look-at task's head and eyes, models swapped in) are counted apart.
 * 3. The harness's RAM at 12 steps of ec1_run_jump (--dump-ram-at): Sonic's display 8C0AE17A run on the reference
 *    RAM and on libsa1's arena at the same step (both as scratch worlds, same XMTRX, sub step 0) records the same
 *    draws bit for bit (models, XMTRX, matrix stack slot 0, texlist, draw state) and the same mesh hash.
 * 4. Events: the sounds of ec1_run_jump, ec1_jump_hold and ec1_spindash (sa1_poll_events, host space) against the
 *    harness's SDK call log of the event stubs, step by step; positions are the owning task's position at the call
 *    (or, for sounds without one, the character's) in host space.
 * 5. Live (sa1_character_tick): Sonic stands, runs, jumps and spin-dashes; mesh_out every tick comes from the
 *    capture (the body's root at the character, the triangle count the emitter produced), events arrive with host
 *    positions; then every other character that ticks (Tails, Knuckles, Amy, E-102, Big) runs 60 pairs with
 *    mesh_out on the same land without a fault.
 * 6. Textures (no reference data needed): every texture of the characters' archives decodes; every mipmapped one
 *    agrees with its own half-size level (box filter), which a misplaced full-size level (the old decoder's padding
 *    rule) does not. The texel-exact check against an independent decoder is tests/texdump_ref.py (+ tests/texdump.c).
 * 7. Render output (API 0.2 render draft) on Emerald Coast act 1's own collision (DISC): Sonic's materials (palette-lit,
 *    white vertex RGB, USE_ALPHA drawn opaque, the graded-alpha buckle blended) and eyes (the irises lie in front of
 *    the eye whites and are opaque, not black); spin dash effects with SON_EFF; Tails' afterimages (blended, the body
 *    copies left out) and tails as drawn (capture v3: the tail models' snapshot holds the points of the draw, not the
 *    ones the display bent after it); Knuckles' punch sprite; E-102's targeting lines; interpolation between the last two pairs
 *    (exact ends, the parts' transforms interpolated - arcs, not chords -, continuity, sa1_character_mesh_lerp, the
 *    accumulator), the spin ball (its size kept at alpha 0.25 / 0.5 / 0.75 of every pair of a jump and a spin dash; turns
 *    beyond half a turn the motion's way round), welds (corners that coincide in both pairs coincide in between: Sonic,
 *    Tails' morphing tails) and determinism (rendering every tick
 *    leaves the world's state as it is). Big after the action button and Amy's hammer are reported.
 * 9. Hidden displays (sa1_capture_hidden): a live world whose screen is zeroed again (the reference simulator's screen
 *    before 2026-10-20: the visibility test never passes) still gives the character's mesh every tick - the CPU pose
 *    of the game's action and frame, since its display draws nothing - while the world's own camera view (the camera
 *    adapter's, src/world/sa1_world_tick.c) keeps Sonic, Tails, Knuckles and Big visible on every sub step 0.
 * 8. Effects (each character on its stage's own collision with the stage tasks): the particle manager 8C09DF64 runs in
 *    the step: 100,000 steps of Sonic's spin dash / jump loop at Emerald Coast's shore spawn thousands of water splash
 *    particles into the 512-node pool, which never runs out, and they keep being drawn: sprites with EFF_REGULAR
 *    textures near Sonic in the render output. Big's fishing displays (decompiled 2026-10-20) are captured: the lure in
 *    every pair, the target group (cast circle + cursor) while he aims, the fishing line while the lure is out.
 * lsd. Special-move visuals (round 4c, sadc docs/research/lightspeed_visuals.md 4-5 and Review R.4; no reference data
 *    needed): Sonic with the Light Speed Shoes and the Crystal Ring (event flags 0x8D + 0x8E) holds the action button
 *    for 75 pairs on Emerald Coast act 1 (DISC): the charge sparkles have the game's size (64 px x 0.2 x the matrix
 *    scale; zero-size ones left out), the ribbons are textured strips of n triangles, the charged aura is an additive,
 *    unlit, untextured ONE/ONE draw of 870 triangles in the game's pulse colour, and no draw of his is untextured and
 *    opaque; Big's fishing line is 11 connected segments; Tails' tail quads split along the diagonal 1-3; synthetic
 *    records cover the rest of the SDK's polygon / line / sprite rules and the snowboards' unchanged path (F2(a)).
 * alpha. The character renderer's vertex alpha (round 0.9.1, sadc docs/research/gsa1_070_translucency.md T4 and its
 *    Review R2.7 / R3.7): synthetic one-triangle records through both branches of 8C037872 (lit, IGNORE_LIGHT) give the
 *    game's single-precision, round-toward-zero arithmetic and FTRC (Amy's first ghost 76, not the old rounding's 77);
 *    Amy's hammer ghosts are {76, 51}, Tails' tail copies {170, 85}, E-102's draws keep their diffuse alpha (etx_right3
 *    216).
 * powerup. The item box power-up models (round 0.9.2a FX3, sadc docs/research/gsa1_091b_itembox.md 6 with its Review
 *    R.4.4): the barrier's spark model, the invincibility model and its spark model, and the magnetic barrier's spark model
 *    are drawn by the game with Ninja's node draw 8C621574 (a "void" stub before: the items worked, nothing was drawn).
 *    (1) The harness references airbox_5_ec1 / airbox_1_ec1 / airbox_8_ec1 (sadc build/traces/ref, 092-harness: the box
 *    made by the sidecar's task; skipped without them) replayed without and with the capture window: every record and
 *    event identical; the models recorded (RT_CAP_NJMOTION + SA1_CAPF_NJ_ACTION, sub step 0, the display's own texlist,
 *    a v3 snapshot) and emitted with the character's archives: textured (EFF_REGULAR), SA1_DRAW_EFFECT, source MODEL, not
 *    palette-lit, additive as their materials say (SRC_ALPHA / ONE, double-sided), 8C6209A8's colours record by record
 *    (RGB 1.0 under control-3D 0x200 plus the constant material: the magnetic barrier's sparks fade), and the magnetic
 *    barrier's spark quads. (2) Live on Emerald Coast act 1 (DISC), Sonic and Knuckles: an air box from the game's
 *    spawner 8C05D9C0 at the character, items 5 / 1 / 8: the item bit, the same records, and sa1_character_render holds
 *    exactly the models' triangles as its MODEL draws (the blob shadow aside; none before the box).
 * Round 0.9.4 (agent 094-lib-render; sadc docs/research/gsa1_094_plan.md 6.2 with its Review R.1 / R.2), on a flat GRID
 * floor in SOURCE coordinates (no reference data needed):
 * exec. Capture version 5: every record carries the running exec D_8C1C1D40 (6): Sonic's body records SonicMain
 *    8C0A77BC, the HUD pass's 2D records 0, a RING's model and sparkle objexec_RING 8C0869E8, dropped rings' models
 *    8C0870EA, an O_GRING line's children's models and sparkle the group's exec 8C087B80 (its children run through
 *    8C098E98, which does not set D_8C1C1D40: Review R.2).
 * turn. (11) the static-ring turn (sadc docs/research/gsa1_090_rings.md Review R.4.9): 12 RINGs 150 units around, 40 up,
 *    Sonic in tight circles: every ring model at alpha 0.5 within 0.25 units of its place (0.9.0: 10.58).
 * object. (10) a RING, an O_GRING line, a ground box (with an OBJ_CONDITION) and an air box in a Sonic world: every drawn
 *    model and sprite of the emitter carries SA1_DRAW_OBJECT / SA1_NODE_OBJECT exactly when its exec is a game object's
 *    (the models, sparkle, panel and the rings' blob shadows do; Sonic's body and effects do not); Sonic's own blob shadow
 *    and the rings' are never one draw of the render (Review R.1); the HUD's screen draws carry no OBJECT flag; Sonic
 *    falls on the ground box: it breaks (0x8011 -> 0x8015), its debris particles (another task's sprites) are not OBJECT
 *    draws, the shell's models are.
 * boxrec. (12) measure, no pass / fail: capture records per item box (ground intact, ground shell, air) with 1 / 10 / 32
 *    boxes in view (for 094-lib-set's box cap; RT_CAP_MAX 512 records per pair).
 */
#include "world_replay.c"

#include <math.h>

#include "sa1_api_world.h"
#include "sa1_capture.h"
#include "sa1_coladapt.h"

static int g_fail, g_skip;

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

#define INJ_FULL (SA1G_INJ_PAD | SA1G_INJ_ANALOG | SA1G_INJ_RAND | SA1G_INJ_XMTRX | SA1G_INJ_CAMERA)
#define SONIC_DISPLAY 0x8C0AE17Au       /* SonicMain's display (also called by SonicMain at the end of each step) */

static void log_cb(void *user, int32_t level, const char *msg)
{
    (void)user;
    if (level >= SA1_LOG_WARN) {
        printf("    [libsa1 %s] %s\n", level >= SA1_LOG_ERROR ? "error" : "warn", msg);
    }
}

typedef struct env {
    const char *sadc, *gdi, *harness, *obj_dir;
    uint32_t frames;
} env;

/* a context with the characters of `chars` loaded and the landtable dump PREFIX as FAITHFUL static collision */
static sa1_context *make_ctx(const env *E, uint32_t chars, const char *prefix, sa1ca_dump *dump)
{
    sa1_context *ctx = NULL;
    sa1_host h;
    sa1_load_options lo;
    sa1_status st;
    sa1_host_defaults(&h);
    h.log = log_cb;
    h.log_level = SA1_LOG_WARN;
    if (sa1_init(&h, &ctx) != SA1_OK) {
        return NULL;
    }
    memset(&lo, 0, sizeof lo);
    lo.struct_size = sizeof lo;
    lo.characters = chars;
    lo.flags = SA1_LOAD_DEFAULT;
    st = sa1_load_disc(ctx, E->gdi, &lo);
    if (SA1_FAILED(st)) {
        printf("    sa1_load_disc: %s: %s\n", sa1_status_string(st), sa1_last_error(ctx));
        sa1_shutdown(ctx);
        return NULL;
    }
    memset(dump, 0, sizeof *dump);
    st = sa1ca_dump_load(ctx, prefix, dump);
    if (st == SA1_OK) {
        st = sa1_collision_set_static_ex(ctx, &dump->desc);
    }
    if (st != SA1_OK) {
        printf("    landtable dump %s: %s: %s\n", prefix, sa1_status_string(st), sa1_last_error(ctx));
        sa1ca_dump_free(ctx, dump);
        sa1_shutdown(ctx);
        return NULL;
    }
    return ctx;
}

static void free_ctx(sa1_context *ctx, sa1ca_dump *dump)
{
    sa1ca_dump_free(ctx, dump);
    sa1_shutdown(ctx);
}

/* a ticking character at a trace's start (bit for bit, as tests/replay_test.c) */
static sa1_status create_at(sa1_context *ctx, const sa1_trace_header *h, sa1_character_id *id)
{
    sa1_character_desc d;
    sa1_status st;
    int k;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.kind = h->chr;
    d.flags = SA1_CREATE_WORLD | SA1_CREATE_SA1_SPACE | SA1_CREATE_ANGLES | SA1_CREATE_STAGE;
    memcpy(d.position, h->start_pos, sizeof d.position);
    for (k = 0; k < 3; k++) {
        d.angle[k] = (int32_t)h->start_ang[k];
    }
    d.level = h->level;
    d.act = h->act;
    st = sa1_character_create_ex(ctx, &d, id);
    if (!SA1_FAILED(st)) {
        wr_screen_of(sa1_character_world(ctx, *id), h);     /* a zero-screen reference: the zero screen again */
    }
    return st;
}

/* ---- replay with an optional capture window around every step ----------------------------------------------- */
typedef void (*step_fn)(void *user, uint32_t i, sa1g_world *w);

static int replay_run(sa1g_world *w, const wr_trace *t, uint32_t first, uint32_t end, sa1_capture *cap,
                      sa1_trace_rec *out, step_fn cb, void *user)
{
    uint32_t i;
    w->replay = 1;
    for (i = first; i < end && i < t->n; i++) {
        const sa1_trace_rec *r = &t->r[i];
        sa1_status st;
        wr_inject(w, r, INJ_FULL, 0);
        sa1_capture_open(cap);
        st = sa1g_world_step(w, (int32_t)r->sub);
        sa1_capture_close(cap);
        if (st != SA1_OK) {
            printf("    step %u: %s: %s\n", (unsigned)i, sa1g_status_string(st), w->fault.msg);
            return -1;
        }
        if (out != NULL) {
            wr_record(w, r, &out[i]);
        }
        if (cb != NULL) {
            cb(user, i, w);
        }
    }
    return 0;
}

/* ---- events of a step, through the public API (host space) -------------------------------------------------- */
#define MAX_EV 4096
typedef struct ev_log {
    sa1_event ev[MAX_EV];
    uint32_t n, lost;
} ev_log;

static void ev_drain(sa1_context *ctx, ev_log *L)
{
    sa1_event tmp[64];
    uint32_t got = 0, k;
    do {
        if (sa1_poll_events(ctx, tmp, 64, &got) != SA1_OK) {
            return;
        }
        for (k = 0; k < got; k++) {
            if (L->n < MAX_EV) {
                L->ev[L->n++] = tmp[k];
            } else {
                L->lost++;
            }
        }
    } while (got == 64);
}

/* FNV-1a */
static uint64_t fnv(uint64_t h, const void *p, size_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    size_t i;
    for (i = 0; i < n; i++) {
        h ^= b[i];
        h *= 1099511628211ull;
    }
    return h;
}

static uint64_t geom_hash(const sa1_geometry *g)
{
    uint64_t h = 1469598103934665603ull;
    h = fnv(h, &g->num_vertices, 4);
    h = fnv(h, &g->num_triangles, 4);
    if (g->num_vertices) {
        h = fnv(h, g->positions, 12u * g->num_vertices);
        h = fnv(h, g->normals, 12u * g->num_vertices);
    }
    if (g->num_triangles) {
        h = fnv(h, g->indices, 12u * g->num_triangles);
        h = fnv(h, g->uvs, 24u * g->num_triangles);
        h = fnv(h, g->colors, 12u * g->num_triangles);
        h = fnv(h, g->textures, 2u * g->num_triangles);
        h = fnv(h, g->materials, 4u * g->num_triangles);
    }
    return h;
}

/* an OBJ of a geometry (inspection) */
static void write_obj(const char *path, const sa1_geometry *g)
{
    FILE *f = fopen(path, "w");
    uint32_t i;
    if (f == NULL) {
        return;
    }
    for (i = 0; i < g->num_vertices; i++) {
        fprintf(f, "v %.5f %.5f %.5f\n", g->positions[3 * i], g->positions[3 * i + 1], g->positions[3 * i + 2]);
    }
    for (i = 0; i < g->num_triangles; i++) {
        fprintf(f, "f %u %u %u\n", (unsigned)g->indices[3 * i] + 1u, (unsigned)g->indices[3 * i + 1] + 1u,
                (unsigned)g->indices[3 * i + 2] + 1u);
    }
    fclose(f);
}

/* ---- 1. capture adds recording only ----------------------------------------------------------------------- */
static const char *const k_traces[] = { "ec1_stand", "ec1_run_jump", "ec1_turns", "ec1_jump_hold", "ec1_spindash",
                                        "tails_sh1", "knux_rm3", "amy_tp2", "gamma_fe1", "big_ec3" };

static int load_trace(const env *E, const char *name, wr_trace *t)
{
    char path[1100];
    snprintf(path, sizeof path, "%s/build/traces/ref/%s.bin", E->sadc, name);
    return wr_trace_load(path, t);
}

static int land_prefix(const env *E, const sa1_trace_header *h, char *prefix, size_t n)
{
    char tri[1200];
    FILE *f;
    snprintf(prefix, n, "%s/build/traces/land/L%02dA%d", E->sadc, (int)h->level, (int)h->act);
    snprintf(tri, sizeof tri, "%s.tri", prefix);
    f = fopen(tri, "rb");
    if (f == NULL) {
        return -1;
    }
    fclose(f);
    return 0;
}

static int events_equal(const ev_log *a, const ev_log *b)
{
    uint32_t i;
    if (a->n != b->n || a->lost || b->lost) {
        return 0;
    }
    for (i = 0; i < a->n; i++) {
        const sa1_event *x = &a->ev[i], *y = &b->ev[i];
        if (x->type != y->type || x->id != y->id || x->bank != y->bank || x->arg != y->arg || x->step != y->step ||
            memcmp(x->position, y->position, sizeof x->position) != 0) {
            return 0;
        }
    }
    return 1;
}

static void print_kinds(const sa1_capture_stats *s)
{
    static const char *const names[SA1_CAP_KINDS] = { "-", "sprite2d", "sprite3d", "line3d", "tri3d", "poly3d", "print",
                                                      "8C037098", "8C03700C", "njDrawModel", "njDrawObject node",
                                                      "Ninja motion node", "8C6215D6", "-", "-", "-" };
    uint32_t k;
    printf("      draws inside capture windows:");
    for (k = 1; k < SA1_CAP_KINDS; k++) {
        if (s->calls[k]) {
            printf(" %s %u", names[k], (unsigned)s->calls[k]);
        }
    }
    printf("; visibility tests %u\n", (unsigned)s->vis_calls);
}

/* per step of a replay: the visibility answers of sub step 0 (rt_draw.c section 6, counted inside capture windows) */
typedef struct vis_tally {
    sa1_capture *cap;
    uint32_t steps0, asked, shown, hidden, first_hidden;
} vis_tally;

static void vis_step(void *user, uint32_t i, sa1g_world *w)
{
    vis_tally *v = (vis_tally *)user;
    sa1_capture_stats s;
    uint32_t sub = 1;
    sa1g_world_rd32(w, 0x8C754EC8u, &sub);  /* B_8C754EC8: the step's sub step */
    if (sub != 0) {
        return;
    }
    sa1_capture_get_stats(v->cap, &s);
    v->steps0++;
    if (s.vis_shown + s.vis_hidden != 0) {
        v->asked++;
    }
    v->shown += s.vis_shown != 0;
    if (s.vis_hidden != 0 && s.vis_shown == 0) {
        if (v->hidden == 0) {
            v->first_hidden = i;
        }
        v->hidden++;
    }
}

static void test_invariance(const env *E)
{
    size_t k;
    printf("1. capture adds recording only (replays without and with the capture window, V6 injection):\n");
    for (k = 0; k < sizeof k_traces / sizeof k_traces[0]; k++) {
        wr_trace t;
        char prefix[1024];
        sa1ca_dump dump;
        sa1_context *ctx;
        sa1_character_id id0 = 0, id1 = 0;
        sa1g_world *w0, *w1;
        sa1_capture *cap = NULL;
        sa1_capture_stats cs;
        sa1_trace_rec *r0, *r1;
        ev_log *e0, *e1;
        vis_tally vt;
        wr_diff d;
        int rc0, rc1;
        uint32_t i;
        if (load_trace(E, k_traces[k], &t) != 0) {
            printf("  skip  %s: no reference trace\n", k_traces[k]);
            g_skip++;
            continue;
        }
        if (land_prefix(E, &t.h, prefix, sizeof prefix) != 0) {
            printf("  skip  %s: no landtable dump %s\n", k_traces[k], prefix);
            g_skip++;
            wr_trace_free(&t);
            continue;
        }
        ctx = make_ctx(E, 1u << t.h.chr, prefix, &dump);
        if (ctx == NULL || create_at(ctx, &t.h, &id0) != SA1_OK || create_at(ctx, &t.h, &id1) != SA1_OK) {
            CHECK(0, "%s: context / characters", k_traces[k]);
            if (ctx) {
                free_ctx(ctx, &dump);
            }
            wr_trace_free(&t);
            continue;
        }
        w0 = sa1_character_world(ctx, id0);
        w1 = sa1_character_world(ctx, id1);
        r0 = (sa1_trace_rec *)calloc(t.n + 1u, sizeof *r0);
        r1 = (sa1_trace_rec *)calloc(t.n + 1u, sizeof *r1);
        e0 = (ev_log *)calloc(1, sizeof *e0);
        e1 = (ev_log *)calloc(1, sizeof *e1);
        CHECK(sa1_capture_attach(ctx, w1, &cap) == SA1_OK, "%s: the world's capture block is found", k_traces[k]);
        memset(&vt, 0, sizeof vt);
        vt.cap = cap;
        rc0 = replay_run(w0, &t, 0, t.n, NULL, r0, NULL, NULL);
        ev_drain(ctx, e0);
        rc1 = replay_run(w1, &t, 0, t.n, cap, r1, vis_step, &vt);
        ev_drain(ctx, e1);
        wr_diff_init(&d);
        for (i = 0; i < t.n; i++) {
            wr_compare(&r0[i], &r1[i], WR_CMP_RAW, &d, (int)i);
        }
        sa1_capture_get_stats(cap, &cs);
        /* Tails' display 8C0BA820 and Knuckles' 8C0C46AC carry logic (the CCL centre of the player's shape, Tails'
         * free.f[6] / f[7] and reqaction 37 -> 38 with mtnmode 2, twp->btimer, Knuckles' free.w[4] counter); the
         * window does not change whether it runs: the visibility test is the original's against the world's screen */
        CHECK(rc0 == 0 && rc1 == 0 && d.nsteps_bad == 0 && events_equal(e0, e1) && cs.total > 0,
              "%s (%u steps, character %d): with the capture window open every record is identical (decoded "
              "fields, raw blobs, v2 scalars: %u records differ), %u events identical; %u draws recorded",
              k_traces[k], (unsigned)t.n, (int)t.h.chr, (unsigned)d.nsteps_bad, (unsigned)e1->n,
              (unsigned)cs.total);
        if (d.nsteps_bad) {
            wr_diff_print(&d, 12);
        }
        if (t.h.chr == SA1_CHAR_AMY || t.h.chr == SA1_CHAR_GAMMA) {
            CHECK(vt.asked == 0, "%s: the display does not ask the visibility test (asked on %u sub-0 steps)",
                  k_traces[k], (unsigned)vt.asked);
        } else {
            /* step 0 runs before the camera's first view (XMTRX = njInitMatrix's diag(1, -1, -1, 1)): Sonic at
             * (-9, 16, 4) is visible, Tails, Knuckles and Big are not, as in the references; a replay step that draws
             * nothing leaves the window's snapshot as it was (it looks like a window without a pair), so a hidden
             * step 0 is not counted at all */
            CHECK(vt.asked + 1u >= vt.steps0 && vt.hidden == 0,
                  "%s: the display asks the visibility test on every sub-0 step from step 2 on (%u of %u) and is "
                  "visible on all of them (hidden on %u), as on the Dreamcast",
                  k_traces[k], (unsigned)vt.asked, (unsigned)vt.steps0, (unsigned)vt.hidden);
        }
        print_kinds(&cs);
        sa1_capture_free(ctx, cap);
        free(r0);
        free(r1);
        free(e0);
        free(e1);
        sa1_character_destroy(ctx, id0);
        sa1_character_destroy(ctx, id1);
        free_ctx(ctx, &dump);
        wr_trace_free(&t);
    }
}

/* ---- 2. captured mesh against the CPU pose evaluator -------------------------------------------------------- */
#define MAXD 256

typedef struct mesh_cmp {
    sa1_context *ctx;
    sa1_capture *cap;
    sa1_character_id pid;           /* the puppet posed by the evaluator */
    const sa1_charset *cs;
    uint32_t texlist0;
    const env *E;
    uint32_t first, stride, count;  /* frames: steps first + stride * k (sub step 0) */
    sa1_meshbuf buf;                /* the capture, draw by draw */
    /* results */
    uint32_t frames, link_frames, compared, empty, tri_min, tri_max, shadow_draws;
    uint32_t walk_same;             /* frames whose draws are the arena tree's visible nodes, in the motion walk's order */
    uint32_t tree_other;            /* frames whose arena tree is not the loader's node for node (other topology) */
    uint32_t draws, static_draws, runtime_draws, swap_draws, hide_draws, ang_draws;
    uint32_t tri_count_bad, attr_bad_tris, tris;
    double max_static, max_runtime, sum_static;  /* SA1 units, against the evaluator with welds */
    double max_static_noweld;
    uint64_t static_corners, weld_corners;
    double weld_res_w, weld_res_nw;
    uint32_t untilted;
    double max_root_err, max_root_rot;
    uint32_t worst_model, worst_step;
} mesh_cmp;

static uint32_t g_u32(const sa1g_world *w, uint32_t g)
{
    uint32_t v = 0;
    sa1g_world_rd32(w, g, &v);
    return v;
}

static float g_f32(const sa1g_world *w, uint32_t g)
{
    uint32_t v = g_u32(w, g);
    float f;
    memcpy(&f, &v, 4);
    return f;
}

/* r = a * b^-1 for 3x4 affine matrices (column vectors, row-major); 0 if b is singular */
static int affine_div(const float *a, const float *b, double r[12])
{
    double c00 = (double)b[5] * b[10] - (double)b[6] * b[9], c01 = (double)b[2] * b[9] - (double)b[1] * b[10];
    double c02 = (double)b[1] * b[6] - (double)b[2] * b[5], c10 = (double)b[6] * b[8] - (double)b[4] * b[10];
    double c11 = (double)b[0] * b[10] - (double)b[2] * b[8], c12 = (double)b[2] * b[4] - (double)b[0] * b[6];
    double c20 = (double)b[4] * b[9] - (double)b[5] * b[8], c21 = (double)b[1] * b[8] - (double)b[0] * b[9];
    double c22 = (double)b[0] * b[5] - (double)b[1] * b[4];
    double det = (double)b[0] * c00 + (double)b[1] * c10 + (double)b[2] * c20, inv[12];
    int i, j;
    if (fabs(det) < 1e-30) {
        return 0;
    }
    inv[0] = c00 / det; inv[1] = c01 / det; inv[2] = c02 / det;
    inv[4] = c10 / det; inv[5] = c11 / det; inv[6] = c12 / det;
    inv[8] = c20 / det; inv[9] = c21 / det; inv[10] = c22 / det;
    for (i = 0; i < 3; i++) {
        inv[4 * i + 3] = -(inv[4 * i] * b[3] + inv[4 * i + 1] * b[7] + inv[4 * i + 2] * b[11]);
    }
    for (i = 0; i < 3; i++) {
        for (j = 0; j < 4; j++) {
            r[4 * i + j] = (double)a[4 * i] * inv[j] + (double)a[4 * i + 1] * inv[4 + j] + (double)a[4 * i + 2] * inv[8 + j] +
                           (j == 3 ? (double)a[4 * i + 3] : 0.0);
        }
    }
    return 1;
}

static double dist_r(const float *c, const double R[12], const float *e)
{
    double x = R[0] * e[0] + R[1] * e[1] + R[2] * e[2] + R[3] - c[0];
    double y = R[4] * e[0] + R[5] * e[1] + R[6] * e[2] + R[7] - c[1];
    double z = R[8] * e[0] + R[9] * e[1] + R[10] * e[2] + R[11] - c[2];
    return sqrt(x * x + y * y + z * z);
}

/* a node of the object tree as it is in the arena now (the game swaps models, toggles SA1_EVAL_HIDE, the head look
 * work writes the head's and eyes' angles) */
typedef struct arena_node {
    uint32_t addr, model, flags, ang[3];
    int32_t parent;
} arena_node;

/* pre-order walk of the arena's tree from obj (node, children, siblings: the motion walk 8C040784's order) */
static uint32_t arena_walk(const sa1g_world *w, uint32_t obj, arena_node *out, uint32_t cap)
{
    uint32_t stack[512], n = 0, sp = 0;
    int32_t parent[512];
    if (obj == 0) {
        return 0;
    }
    stack[sp] = obj;
    parent[sp++] = -1;
    while (sp > 0 && n < cap) {
        uint32_t a = stack[--sp], k, sib, child;
        int32_t par = parent[sp];
        arena_node *nd = &out[n];
        nd->addr = a & 0x1FFFFFFFu;
        nd->flags = g_u32(w, a);
        nd->model = g_u32(w, a + 4) & 0x1FFFFFFFu;
        for (k = 0; k < 3; k++) {
            nd->ang[k] = g_u32(w, a + 0x14 + 4 * k);
        }
        nd->parent = par;
        child = g_u32(w, a + 0x2C);
        sib = g_u32(w, a + 0x30);
        if (sib != 0 && sp < 512) {
            stack[sp] = sib;
            parent[sp++] = par;
        }
        if (child != 0 && sp < 512) {
            stack[sp] = child;
            parent[sp++] = (int32_t)n;
        }
        n++;
    }
    return n;
}

static void mesh_frame(void *user, uint32_t i, sa1g_world *w)
{
    mesh_cmp *M = (mesh_cmp *)user;
    sa1g_player_ptrs pp;
    sa1_geometry gp, gq;
    const sa1_geometry *gpp = NULL;
    const sa1_cap_rec *recs;
    const sa1_object *o;
    sa1_pose pose;
    static arena_node an[1024];
    uint32_t nrec, k, d, ncap = 0, nan_, shadow, flag, cwp, info, ang[3], obj, nexp = 0;
    uint32_t capm[MAXD], capt0[MAXD], capt1[MAXD], capnode[MAXD];
    uint32_t evt0[1024], et = 0;
    uint8_t changed[1024];
    uint32_t look[3];
    int32_t act, mtn;
    float pos[3], center[3], nframe, hpos[3], hcen[3];
    double R[12], s = 1.0 / (double)M->ctx->coords.scale;
    float *qpos;
    uint32_t *qidx;
    int have_r = 0, tree_same, walk_ok;
    if (i < M->first || (i - M->first) % M->stride != 0 || (i - M->first) / M->stride >= M->count) {
        return;
    }
    sa1g_world_player(w, &pp);
    mtn = (int16_t)g_u32(w, pp.pwp + 0x120);
    act = (int32_t)(g_u32(w, pp.pwp + 0x124) & 0xFFFFu);       /* reqaction: what the display draws */
    if (mtn == 2) {
        act = (int32_t)(g_u32(w, pp.pwp + 0x126) & 0xFFFFu);
    }
    nframe = g_f32(w, pp.pwp + 0x130);
    for (k = 0; k < 3; k++) {
        pos[k] = g_f32(w, pp.twp + 0x20 + 4 * k);
        ang[k] = g_u32(w, pp.twp + 0x14 + 4 * k);
    }
    flag = g_u32(w, pp.twp + 4) & 0xFFFFu;
    /* SonicDisplay 8C0AE17A draws action 32 (Sonic_Jumpball) instead while spinning (twp->flag 0x100) on the frames
     * pwp->free.w[1] & 0x11 picks: the jump alternates the ball and the curled model */
    if (mtn != 2 && (flag & 0x100) && act != 141 && (g_u32(w, pp.pwp + 0x7C) >> 16 & 0x11u)) {
        act = 32;
    }
    cwp = g_u32(w, pp.twp + 0x38);
    info = g_u32(w, cwp + 0x0C);
    for (k = 0; k < 3; k++) {
        center[k] = g_f32(w, info + 0x08 + 4 * k);
    }
    M->frames++;
    /* the capture, draw by draw (as sa1_capture_emit, keeping each draw's triangle range); the blob shadow aside */
    nrec = sa1_capture_records(M->cap, &recs);
    shadow = sa1_capture_shadow_model(M->cap);
    sa1_meshbuf_reset(&M->buf);
    for (k = 0; k < nrec && ncap < MAXD; k++) {
        const sa1_cap_rec *r = &recs[k];
        double m[12];
        uint32_t t0 = M->buf.nt;
        if (!SA1_CAP_IS_MODEL(r->kind)) {
            continue;
        }
        if ((r->obj & 0x1FFFFFFFu) == (shadow & 0x1FFFFFFFu)) {
            M->shadow_draws++;
            continue;
        }
        sa1_capture_world_matrix(r, m);
        /* the stored materials (no attribute masks or constant material: the evaluator has none), with the character
           renderer's colours: white light and the material alpha, as the evaluator emits them */
        {
            sa1_draw_state hand;
            memset(&hand, 0, sizeof hand);
            hand.hand = 1;
            if (sa1_pose_emit_model(M->ctx, &M->buf, sa1_capture_read, M->cap, r->obj, m,
                                    (r->texlist & 0x1FFFFFFFu) == (M->texlist0 & 0x1FFFFFFFu), &hand) != SA1_OK) {
                continue;
            }
        }
        capm[ncap] = r->obj & 0x1FFFFFFFu;
        capt0[ncap] = t0;
        capt1[ncap] = M->buf.nt;
        ncap++;
    }
    if (M->buf.nt == 0) {
        M->empty++;
    }
    M->tri_min = M->buf.nt < M->tri_min ? M->buf.nt : M->tri_min;
    M->tri_max = M->buf.nt > M->tri_max ? M->buf.nt : M->tri_max;
    if (M->E->obj_dir != NULL && (i == M->first || (i - M->first) / M->stride == M->count / 2)) {
        char path[1200];
        sa1_geometry g;
        memset(&g, 0, sizeof g);
        sa1_meshbuf_geometry(&M->buf, &g);
        snprintf(path, sizeof path, "%s/capture_step%u.obj", M->E->obj_dir, (unsigned)i);
        write_obj(path, &g);
    }
    if (mtn == 2) {
        /* a link action (PSetMotion's blend, MAlloc'd by 8C079C76): the evaluator has no counterpart */
        M->link_frames++;
        return;
    }
    if (act < 0 || (uint32_t)act >= M->cs->num_actions || M->cs->actions[act].status != SA1_ACTION_OK) {
        return;
    }
    /* the drawn object as it is in the arena: the capture must be its visible nodes in the motion walk's order */
    obj = g_u32(w, g_u32(w, g_u32(w, pp.pwp + 0x140) + 0x10u * (uint32_t)act));
    nan_ = arena_walk(w, obj, an, 1024);
    walk_ok = 1;
    for (k = 0; k < nan_; k++) {
        if (an[k].model != 0 && !(an[k].flags & SA1_EVAL_HIDE)) {
            if (nexp >= ncap || capm[nexp] != an[k].model) {
                walk_ok = 0;
            } else {
                capnode[nexp] = k;
            }
            nexp++;
        }
    }
    walk_ok = walk_ok && nexp == ncap;
    M->walk_same += walk_ok;
    if (!walk_ok) {
        return;
    }
    /* the loader's tree (the disc's initial pointers, flags and angles) against the arena's, node for node */
    o = &SA1_VEC(M->cs->objects, sa1_object)[M->cs->actions[act].object];
    tree_same = o->num_nodes == nan_;
    /* the head / eyes of the look-at task 8C0B28EE (D_8C1C5F08, handed over by SonicMain through 8C0B27EE): its
     * lookwk turns their angles toward the look target around the draw and puts the saved ones back */
    for (d = 0; d < 3; d++) {
        look[d] = g_u32(w, 0x8C1C5F08u + 4u * d) & 0x1FFFFFFFu;
    }
    for (k = 0; tree_same && k < nan_; k++) {
        const sa1_node *ln = &o->nodes[k];
        uint32_t lm = ln->model >= 0 ? SA1_VEC(M->cs->models, sa1_model)[ln->model].addr & 0x1FFFFFFFu : 0u;
        tree_same = ln->addr == an[k].addr;
        changed[k] = (uint8_t)((lm != an[k].model ? 1u : 0u) | (((ln->flags ^ an[k].flags) & SA1_EVAL_HIDE) ? 2u : 0u) |
                               ((uint32_t)ln->ang[0] != an[k].ang[0] || (uint32_t)ln->ang[1] != an[k].ang[1] ||
                                (uint32_t)ln->ang[2] != an[k].ang[2] ? 4u : 0u));
        for (d = 0; d < 3; d++) {
            if (an[k].addr == look[d]) {
                changed[k] |= 16u;  /* a node of the look-at work: its angles turn while the game draws */
            }
        }
        if (an[k].parent >= 0) {
            changed[k] |= (uint8_t)(changed[an[k].parent] & 8u);
            if (changed[an[k].parent] & 23u) {
                changed[k] |= 8u;   /* below a changed node */
            }
        }
        evt0[k] = et;
        if (ln->drawn) {
            et += SA1_VEC(M->cs->models, sa1_model)[ln->model].num_tris;
        }
    }
    if (!tree_same) {
        M->tree_other++;
        return;
    }
    /* the evaluator at the display's action and frame, at twp->pos with yaw = -ang.y: without welds, then with */
    memset(&pose, 0, sizeof pose);
    pose.struct_size = sizeof pose;
    pose.action = act;
    pose.frame = nframe;
    sa1_to_host_point(M->ctx, pos, pose.position);
    pose.yaw = (float)(-(double)(int16_t)(uint16_t)(ang[1] & 0xFFFFu) * (6.28318530717958647692 / 65536.0));
    pose.flags = SA1_POSE_NO_WELD;
    if (sa1_character_pose(M->ctx, M->pid, &pose, NULL) != SA1_OK || sa1_character_geometry(M->ctx, M->pid, &gpp) != SA1_OK) {
        return;
    }
    gq = *gpp;
    qpos = (float *)malloc(12u * (gq.num_vertices + 1u));
    qidx = (uint32_t *)malloc(12u * (gq.num_triangles + 1u));
    memcpy(qpos, gq.positions, 12u * gq.num_vertices);
    memcpy(qidx, gq.indices, 12u * gq.num_triangles);
    pose.flags = 0;
    sa1_character_pose(M->ctx, M->pid, &pose, NULL);
    sa1_character_geometry(M->ctx, M->pid, &gpp);
    gp = *gpp;
    M->compared++;
    /* the root: the first unchanged node's draw maps the evaluator's world onto the capture's */
    for (d = 0; d < ncap && !have_r; d++) {
        if (!changed[capnode[d]] && o->nodes[capnode[d]].drawn) {
            have_r = affine_div(M->buf.node_mtx + 12u * d, gp.node_matrices + 12u * capnode[d], R);
        }
    }
    sa1_to_host_point(M->ctx, pos, hpos);
    sa1_to_host_point(M->ctx, center, hcen);
    if (have_r && ang[0] == 0 && ang[2] == 0 && !(act == 14 && (flag & 3))) {
        /* without tilt the display's root is the evaluator's moved by center - pos (the root node's own motion
         * included: the first draw is a child of the root) */
        double e3 = 0.0, rot = 0.0;
        for (k = 0; k < 3; k++) {
            double dt = R[4 * k + 3] - ((double)hcen[k] - hpos[k]);
            uint32_t j;
            e3 += dt * dt;
            for (j = 0; j < 3; j++) {
                double dr = R[4 * k + j] - (j == k ? 1.0 : 0.0);
                rot = fabs(dr) > rot ? fabs(dr) : rot;
            }
        }
        M->untilted++;
        M->max_root_err = sqrt(e3) * s > M->max_root_err ? sqrt(e3) * s : M->max_root_err;
        M->max_root_rot = rot > M->max_root_rot ? rot : M->max_root_rot;
    }
    for (d = 0; d < ncap && have_r; d++) {
        uint32_t nd = capnode[d], t, n;
        const sa1_node *ln = &o->nodes[nd];
        int runtime = changed[nd] != 0;
        double dmax = 0.0;
        M->draws++;
        if (changed[nd] & 1u) {
            M->swap_draws++;
        }
        if (changed[nd] & 2u) {
            M->hide_draws++;
        }
        if (changed[nd] & 28u) {
            M->ang_draws++;
        }
        if ((changed[nd] & 3u) || !ln->drawn) {
            M->runtime_draws++;
            continue;           /* another model or a node the evaluator hides: nothing to compare */
        }
        n = capt1[d] - capt0[d];
        if (n != SA1_VEC(M->cs->models, sa1_model)[ln->model].num_tris) {
            M->tri_count_bad++;
            continue;
        }
        if (runtime) {
            M->runtime_draws++;
        } else {
            M->static_draws++;
        }
        for (t = 0; t < n; t++) {
            uint32_t tc = capt0[d] + t, te = evt0[nd] + t, c;
            int bad = 0;
            M->tris++;
            if (M->buf.tex[tc] != gp.textures[te] || M->buf.mat[tc] != gp.materials[te] ||
                memcmp(M->buf.col + 12u * tc, gp.colors + 12u * te, 12) != 0 ||
                memcmp(M->buf.uv + 6u * tc, gp.uvs + 6u * te, 6u * sizeof(float)) != 0) {
                bad = 1;
            }
            for (c = 0; c < 3; c++) {
                const float *pc = M->buf.pos + 3u * M->buf.idx[3 * tc + c];
                const float *pw = gp.positions + 3u * gp.indices[3 * te + c];
                const float *pn = qpos + 3u * qidx[3 * te + c];
                double dw = dist_r(pc, R, pw) * s, dn = dist_r(pc, R, pn) * s;
                dmax = dw > dmax ? dw : dmax;
                if (runtime) {
                    continue;
                }
                M->sum_static += dw;
                M->static_corners++;
                M->max_static_noweld = dn > M->max_static_noweld ? dn : M->max_static_noweld;
                if (pw[0] != pn[0] || pw[1] != pn[1] || pw[2] != pn[2]) {
                    M->weld_corners++;
                    M->weld_res_w += dw;
                    M->weld_res_nw += dn;
                }
            }
            M->attr_bad_tris += bad;
        }
        if (runtime) {
            M->max_runtime = dmax > M->max_runtime ? dmax : M->max_runtime;
        } else if (dmax > M->max_static) {
            M->max_static = dmax;
            M->worst_model = capm[d];
            M->worst_step = i;
        }
    }
    free(qpos);
    free(qidx);
}

static void test_mesh(const env *E)
{
    wr_trace t;
    char prefix[1024];
    sa1ca_dump dump;
    sa1_context *ctx;
    sa1_character_id id = 0, pid = 0;
    sa1_character_desc d;
    sa1_physics ph;
    mesh_cmp *M;
    int rc;
    printf("2. the captured mesh of ec1_run_jump against the CPU pose evaluator (%u frames):\n", (unsigned)E->frames);
    if (load_trace(E, "ec1_run_jump", &t) != 0 || land_prefix(E, &t.h, prefix, sizeof prefix) != 0) {
        printf("  skip  no ec1_run_jump trace / landtable dump\n");
        g_skip++;
        return;
    }
    ctx = make_ctx(E, 1u << SA1_CHAR_SONIC, prefix, &dump);
    M = (mesh_cmp *)calloc(1, sizeof *M);
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.kind = SA1_CHAR_SONIC;
    d.flags = SA1_CREATE_PUPPET;
    if (ctx == NULL || M == NULL || create_at(ctx, &t.h, &id) != SA1_OK || sa1_character_create_ex(ctx, &d, &pid) != SA1_OK) {
        CHECK(0, "context / characters");
        free(M);
        if (ctx) {
            free_ctx(ctx, &dump);
        }
        wr_trace_free(&t);
        return;
    }
    M->ctx = ctx;
    M->E = E;
    M->pid = pid;
    M->cs = &ctx->chars[SA1_CHAR_SONIC];
    M->texlist0 = M->cs->num_archives ? M->cs->archives[0].texlist_addr : 0u;
    M->first = 30;
    M->stride = 4;
    M->count = E->frames;
    M->tri_min = 0xFFFFFFFFu;
    if (sa1_capture_attach(ctx, sa1_character_world(ctx, id), &M->cap) != SA1_OK) {
        CHECK(0, "capture block");
    } else {
        rc = replay_run(sa1_character_world(ctx, id), &t, 0, t.n, M->cap, NULL, mesh_frame, M);
        memset(&ph, 0, sizeof ph);
        sa1_physics_get(ctx, SA1_CHAR_SONIC, &ph);
        CHECK(rc == 0 && M->frames == E->frames && M->empty == 0,
              "%u frames (steps %u, %u, ... %u: sub step 0) captured, %u-%u triangles each without the blob shadow "
              "(%u shadow draws: Character_Shadow_Blob through njDrawObject, left out of the character's mesh)",
              (unsigned)M->frames, (unsigned)M->first, (unsigned)(M->first + M->stride),
              (unsigned)(M->first + M->stride * (M->count - 1u)), (unsigned)M->tri_min, (unsigned)M->tri_max,
              (unsigned)M->shadow_draws);
        printf("      frames drawing a link action (PSetMotion's blend between two actions, mtnmode 2, MAlloc'd by "
               "8C079C76; the evaluator has no counterpart): %u\n", (unsigned)M->link_frames);
        CHECK(M->walk_same + M->link_frames == M->frames,
              "in every other frame (%u) the capture is exactly the visible nodes of the action's object as the arena "
              "holds it, in the motion walk's order (8C040784: model set, SA1_EVAL_HIDE clear)", (unsigned)M->walk_same);
        CHECK(M->compared > 0 && M->tree_other == 0 && M->tri_count_bad == 0,
              "%u frames compared with the evaluator node for node (%u draws): %u nodes as on the disc, %u changed by "
              "the game at run time (%u another model: 8C0B2170's head part / the jump ball on the curled model's root; "
              "%u NJD_EVAL_HIDE toggled; %u the look-at task's head / eyes, nodes with angles the game wrote, or "
              "nodes below them: 8C0B28EE, 8C07AEF8)", (unsigned)M->compared, (unsigned)M->draws, (unsigned)M->static_draws,
              (unsigned)M->runtime_draws, (unsigned)M->swap_draws, (unsigned)M->hide_draws, (unsigned)M->ang_draws);
        CHECK(M->tris > 0 && M->attr_bad_tris == 0,
              "%u triangles of the same models: UVs, texture ids, materials and colours identical to the evaluator's",
              (unsigned)M->tris);
        CHECK(M->untilted > 0 && M->max_root_err < 0.05 && M->max_root_rot < 1e-3,
              "root: in the %u frames without tilt the display's root is the evaluator's moved by center - pos "
              "(njTranslateV(CWP(twp)->info->center) in 8C0AE17A, center = pos + %g up, 8C0AE110 / p.center_height): "
              "within %.4f SA1 units / %.1e of rotation (the root part's own interpolation); the evaluator puts the "
              "model's root at twp->pos and does not tilt it by ang.x / ang.z", (unsigned)M->untilted,
              (double)ph.center_height, M->max_root_err, M->max_root_rot);
        CHECK(M->static_corners > 0 && M->max_static < 0.005,
              "unchanged nodes, %llu corners relative to the root: max %.5f, mean %.6f SA1 units from the evaluator "
              "with welds (FSCA / FTRV / the motion player's float interpolation against double trig; worst model "
              "%08X, step %u), max %.5f without its welds; at the %llu corners the welds move: mean %.5f with / %.5f "
              "without the evaluator's welds (the game's PJoinVertexes = the evaluator's weld rules); nodes the game "
              "changed: up to %.4f", (unsigned long long)M->static_corners, M->max_static,
              M->sum_static / (double)M->static_corners, (unsigned)M->worst_model, (unsigned)M->worst_step,
              M->max_static_noweld, (unsigned long long)M->weld_corners,
              M->weld_corners ? M->weld_res_w / (double)M->weld_corners : 0.0,
              M->weld_corners ? M->weld_res_nw / (double)M->weld_corners : 0.0, M->max_runtime);
    }
    sa1_capture_free(ctx, M->cap);
    sa1_meshbuf_free(ctx, &M->buf);
    free(M);
    sa1_character_destroy(ctx, id);
    sa1_character_destroy(ctx, pid);
    free_ctx(ctx, &dump);
    wr_trace_free(&t);
}
/* ---- 3. Sonic's display on the harness's RAM and on libsa1's arena ----------------------------------------- */
typedef struct disp_arg {
    uint32_t tp;
} disp_arg;

static void call_display(sa1g_world *w, void *user)
{
    const disp_arg *a = (const disp_arg *)user;
    void (*f)(void *) = (void (*)(void *))sa1_fn(SA1_GFN_C(SONIC_DISPLAY), 0u);
    sa1g_world_wr32(w, SA1G_ADDR_SUB_STEP, 0u);
    f(sa1g_world_ptr(w, a->tp));
}

/* a scratch world holding `ram` (16 MB), Sonic's display run on it with XMTRX = xm, sub step 0, capture on */
static int display_on_ram(sa1_context *ctx, const void *ram, const uint32_t xm[16], sa1g_world **out_w,
                          sa1_capture **out_c)
{
    sa1g_world *w = (sa1g_world *)calloc(1, sizeof *w);
    sa1_capture *c = NULL;
    disp_arg a;
    sa1_status st;
    *out_w = NULL;
    *out_c = NULL;
    if (w == NULL || sa1g_world_create(w, NULL) != 0) {
        free(w);
        return -1;
    }
    if (sa1g_world_apply_ram(w, ram, SA1G_ARENA_SIZE) != SA1_OK || sa1_capture_attach(ctx, w, &c) != SA1_OK) {
        sa1g_world_destroy(w);
        free(w);
        return -1;
    }
    memcpy(w->xmtrx, xm, sizeof w->xmtrx);
    a.tp = 0;
    sa1g_world_rd32(w, SA1G_ADDR_PLAYER_TP, &a.tp);
    sa1_capture_open(c);
    st = sa1g_world_call(w, call_display, &a);
    sa1_capture_close(c);
    if (st != SA1_OK) {
        printf("    display: %s: %s\n", sa1g_status_string(st), w->fault.msg);
    }
    *out_w = w;
    *out_c = c;
    return st == SA1_OK ? 0 : -1;
}

/* records equal but for the frame counter (the replay injects the reference's counter at player entry, the
 * reference's RAM at a step's start has the pair's advance) and the constant material B_8C88F668 (the reference's
 * stage tasks set it; no libsa1 world has them); *what names the first other field that differs */
static int rec_equal(const sa1_cap_rec *a, const sa1_cap_rec *b, const char **what)
{
#define RQ(f)                                                                  \
    if (memcmp(&a->f, &b->f, sizeof a->f) != 0) {                              \
        *what = #f;                                                            \
        return 0;                                                              \
    }
    RQ(kind) RQ(sub) RQ(depth) RQ(obj) RQ(texlist) RQ(ctl3d) RQ(attr_and) RQ(attr_or) RQ(n) RQ(attr) RQ(bank)
    RQ(m) RQ(base)
#undef RQ
    return 1;
}

static void test_harness_ram(const env *E)
{
    static const uint32_t steps[] = { 30, 50, 70, 90, 110, 130, 150, 170, 190, 210, 230, 250 };
    wr_trace t;
    char prefix[1024];
    sa1ca_dump dump;
    sa1_context *ctx;
    sa1_character_id id = 0;
    sa1g_world *w;
    uint32_t cur = 0, k, nok = 0, nrun = 0, nrec_tot = 0, cmat_diff = 0, cmat_r[4] = { 0, 0, 0, 0 };
    printf("3. Sonic's display (8C0AE17A) on the harness's RAM and on libsa1's arena at the same step (ec1_run_jump):\n");
    if (E->harness == NULL) {
        printf("  skip  no --harness DIR\n");
        g_skip++;
        return;
    }
    if (load_trace(E, "ec1_run_jump", &t) != 0 || land_prefix(E, &t.h, prefix, sizeof prefix) != 0) {
        printf("  skip  no ec1_run_jump trace / landtable dump\n");
        g_skip++;
        return;
    }
    ctx = make_ctx(E, 1u << SA1_CHAR_SONIC, prefix, &dump);
    if (ctx == NULL || create_at(ctx, &t.h, &id) != SA1_OK) {
        CHECK(0, "context / character");
        if (ctx) {
            free_ctx(ctx, &dump);
        }
        wr_trace_free(&t);
        return;
    }
    w = sa1_character_world(ctx, id);
    for (k = 0; k < sizeof steps / sizeof steps[0]; k++) {
        char path[1200];
        size_t n = 0;
        unsigned char *ram;
        sa1g_world *wr = NULL, *wl = NULL;
        sa1_capture *cr = NULL, *cl = NULL;
        const sa1_cap_rec *rr, *rl;
        sa1_geometry gr, gl;
        uint32_t nr, nl, i, same = 0;
        const char *what = "-";
        sa1_capture_emit_opts eo;
        uint64_t hr, hl;
        snprintf(path, sizeof path, "%s/ec1_run_jump.step%u.ram", E->harness, (unsigned)steps[k]);
        ram = wr_read_file(path, &n);
        if (ram == NULL || n != SA1G_ARENA_SIZE) {
            printf("  skip  %s: missing\n", path);
            g_skip++;
            free(ram);
            continue;
        }
        if (replay_run(w, &t, cur, steps[k], NULL, NULL, NULL, NULL) != 0) {
            free(ram);
            break;
        }
        cur = steps[k];
        nrun++;
        if (display_on_ram(ctx, ram, t.r[steps[k]].px_xmtrx, &wr, &cr) != 0 ||
            display_on_ram(ctx, w->ram, t.r[steps[k]].px_xmtrx, &wl, &cl) != 0) {
            CHECK(0, "step %u: display on the scratch worlds", (unsigned)steps[k]);
        } else {
            nr = sa1_capture_records(cr, &rr);
            nl = sa1_capture_records(cl, &rl);
            for (i = 0; i < nr && i < nl; i++) {
                const char *w1 = NULL;
                if (rec_equal(&rr[i], &rl[i], &w1)) {
                    same++;
                } else if (what[0] == '-') {
                    what = w1;
                }
            }
            memset(&gr, 0, sizeof gr);
            memset(&gl, 0, sizeof gl);
            memset(&eo, 0, sizeof eo);
            eo.texlist0 = ctx->chars[SA1_CHAR_SONIC].archives[0].texlist_addr;
            sa1_capture_emit(ctx, cr, &eo, &gr);
            hr = geom_hash(&gr);
            sa1_capture_emit(ctx, cl, &eo, &gl);
            hl = geom_hash(&gl);
            if (nr > 0 && nl > 0 && memcmp(rr[0].cmat, rl[0].cmat, sizeof rr[0].cmat) != 0) {
                cmat_diff++;
                memcpy(cmat_r, rr[0].cmat, sizeof cmat_r);
            }
            nrec_tot += nr;
            if (nr == nl && same == nr && nr > 0 && hr == hl && gr.num_triangles > 0) {
                nok++;
            } else {
                CHECK(0, "step %u: reference RAM %u draws, libsa1 %u, %u identical (first other field %s); mesh hash "
                      "%016llX / %016llX (%u / %u triangles)", (unsigned)steps[k], (unsigned)nr, (unsigned)nl,
                      (unsigned)same, what,
                      (unsigned long long)hr, (unsigned long long)hl, (unsigned)gr.num_triangles, (unsigned)gl.num_triangles);
            }
            if (k == 0) {
                printf("      step %u: %u draws, mesh %u triangles, hash %016llX\n", (unsigned)steps[k], (unsigned)nr,
                       (unsigned)gr.num_triangles, (unsigned long long)hr);
            }
        }
        sa1_capture_free(ctx, cr);
        sa1_capture_free(ctx, cl);
        if (wr) {
            sa1g_world_destroy(wr);
            free(wr);
        }
        if (wl) {
            sa1g_world_destroy(wl);
            free(wl);
        }
        free(ram);
    }
    CHECK(nrun > 0 && nok == nrun, "%u of %u steps: the reference's RAM and libsa1's arena give the same draws bit for bit "
          "(model, XMTRX, matrix stack slot 0, texlist, control-3D flags, constant attribute masks, palette bank; %u "
          "draws) and the same mesh hash", (unsigned)nok, (unsigned)nrun, (unsigned)nrec_tot);
    if (cmat_diff) {
        printf("      the constant material B_8C88F668 differs in %u steps (reference %08X %08X %08X %08X, libsa1 0): the "
               "reference's stage tasks set it (no libsa1 world has them); the hand-written renderer only offsets the "
               "alpha with it, already 1 for Sonic's materials (the mesh hashes agree)\n",
               (unsigned)cmat_diff, (unsigned)cmat_r[0], (unsigned)cmat_r[1], (unsigned)cmat_r[2], (unsigned)cmat_r[3]);
    }
    sa1_character_destroy(ctx, id);
    free_ctx(ctx, &dump);
    wr_trace_free(&t);
}

/* ---- 4. events against the harness's SDK call log --------------------------------------------------------- */
typedef struct sdk_call {
    int step;
    uint32_t addr;
    uint32_t a[8];
    int na;
    int matched;
} sdk_call;

/* "S <step> <seq> <ADDR> <name>(<args>) -> <ret>" lines of the event stubs */
static uint32_t parse_sdk(const char *path, sdk_call **out)
{
    size_t n = 0;
    unsigned char *b = wr_read_file(path, &n);
    char *s, *line;
    uint32_t cap = 1024, cnt = 0;
    sdk_call *v = (sdk_call *)calloc(cap, sizeof *v);
    *out = NULL;
    if (b == NULL || v == NULL) {
        free(b);
        free(v);
        return 0;
    }
    b = (unsigned char *)realloc(b, n + 1u);
    b[n] = 0;
    for (line = (char *)b; line != NULL && *line; line = s) {
        sdk_call c;
        char *paren, *close_p, *arg;
        int step = 0, seq = 0;
        unsigned addr = 0;
        s = strchr(line, '\n');
        if (s != NULL) {
            *s++ = 0;
        }
        if (line[0] != 'S' || sscanf(line, "S %d %d %8X", &step, &seq, &addr) != 3) {
            continue;
        }
        if (addr == 0x8C03700Cu || addr == 0x8C037098u || addr == 0x8C038D00u) {
            continue;
        }
        memset(&c, 0, sizeof c);
        c.step = step;
        c.addr = addr;
        paren = strchr(line, '(');
        close_p = paren ? strchr(paren, ')') : NULL;
        if (paren != NULL && close_p != NULL) {
            *close_p = 0;
            for (arg = paren + 1; *arg && c.na < 8;) {
                char *e;
                if (*arg == 'f') {
                    arg++;
                }
                c.a[c.na++] = (uint32_t)strtoul(arg, &e, 16);
                arg = (*e == ',') ? e + 1 : e;
                if (e == arg && *e != 0) {
                    break;
                }
            }
        }
        if (cnt == cap) {
            cap *= 2u;
            v = (sdk_call *)realloc(v, cap * sizeof *v);
        }
        v[cnt++] = c;
    }
    free(b);
    *out = v;
    return cnt;
}

/* the event a stub call queues (gen/policy/stubs.tsv "event" lines) */
static int expect_of(const sdk_call *c, uint32_t *type, uint32_t *id)
{
    switch (c->addr) {
    case 0x8C0940DAu: case 0x8C09413Au: case 0x8C0941FCu: case 0x8C094330u: case 0x8C0943E0u:
        *type = SA1_EVENT_SOUND;
        *id = c->a[0];
        return 1;
    case 0x8C0945A0u:
        *type = SA1_EVENT_SOUND_STOP;
        *id = c->a[0];
        return 1;
    case 0x8C0945D0u: case 0x8C0E91A0u:
        *type = SA1_EVENT_SOUND_STOP;
        *id = 0;
        return 1;
    case 0x8C107BA0u: case 0x8C04EF22u:
        *type = SA1_EVENT_VOICE;
        *id = c->a[0];
        return 1;
    case 0x8C107AC6u: case 0x8C107C80u: case 0x8C04EF52u:
        *type = SA1_EVENT_JINGLE;
        *id = c->a[0];
        return 1;
    case 0x8C107AAAu: case 0x8C107ADAu: case 0x8C107B26u: case 0x8C107D80u:
        *type = SA1_EVENT_JINGLE;
        *id = 0;
        return 1;
    case 0x8C109EACu: case 0x8C109FDEu:
        *type = SA1_EVENT_RUMBLE;
        *id = c->a[1];
        return 1;
    case 0x8C04E386u:
        *type = SA1_EVENT_RESTART;
        *id = c->a[0] & 0xFFFFu;
        return 1;
    case 0x8C04F8FAu:
        *type = SA1_EVENT_RESPAWN;
        *id = 0;
        return 1;
    default:
        return 0;
    }
}

typedef struct ev_steps {
    sa1_context *ctx;
    ev_log *log;
    uint32_t *first;            /* per step: index of its first event */
    float (*pos)[3];            /* per step: player position after the step (SA1) */
    uint32_t steps;
} ev_steps;

static void ev_step(void *user, uint32_t i, sa1g_world *w)
{
    ev_steps *S = (ev_steps *)user;
    sa1g_player_ptrs pp;
    uint32_t k;
    S->first[i] = S->log->n;
    ev_drain(S->ctx, S->log);
    sa1g_world_player(w, &pp);
    for (k = 0; k < 3; k++) {
        S->pos[i][k] = g_f32(w, pp.twp + 0x20 + 4 * k);
    }
    S->steps = i + 1u;
}

static void test_events(const env *E)
{
    static const char *const names[] = { "ec1_run_jump", "ec1_jump_hold", "ec1_spindash" };
    size_t k;
    char prefix[1024];
    sa1ca_dump dump;
    sa1_context *ctx = NULL;
    printf("4. events (sa1_poll_events, host space) against the harness's SDK call log of the event stubs:\n");
    if (E->harness == NULL) {
        printf("  skip  no --harness DIR\n");
        g_skip++;
        return;
    }
    for (k = 0; k < sizeof names / sizeof names[0]; k++) {
        wr_trace t;
        char path[1200];
        sdk_call *calls = NULL;
        uint32_t ncalls, i, j, matched = 0, stage_only = 0, water_only = 0, other_ref = 0, extra = 0, pos_bad = 0;
        uint32_t pos_task = 0, pos_chr = 0;
        ev_steps S;
        sa1_character_id id = 0;
        char ids[256] = "";
        size_t idl = 0;
        if (load_trace(E, names[k], &t) != 0) {
            continue;
        }
        if (ctx == NULL) {
            if (land_prefix(E, &t.h, prefix, sizeof prefix) != 0 || (ctx = make_ctx(E, 1u << SA1_CHAR_SONIC, prefix, &dump)) == NULL) {
                CHECK(0, "context");
                wr_trace_free(&t);
                return;
            }
        }
        snprintf(path, sizeof path, "%s/%s.sdk", E->harness, names[k]);
        ncalls = parse_sdk(path, &calls);
        if (calls == NULL) {
            printf("  skip  %s: missing\n", path);
            g_skip++;
            wr_trace_free(&t);
            continue;
        }
        memset(&S, 0, sizeof S);
        S.ctx = ctx;
        S.log = (ev_log *)calloc(1, sizeof *S.log);
        S.first = (uint32_t *)calloc(t.n + 2u, sizeof *S.first);
        S.pos = (float (*)[3])calloc(t.n + 2u, sizeof *S.pos);
        if (create_at(ctx, &t.h, &id) != SA1_OK ||
            replay_run(sa1_character_world(ctx, id), &t, 0, t.n, NULL, NULL, ev_step, &S) != 0) {
            CHECK(0, "%s: replay", names[k]);
        }
        S.first[S.steps] = S.log->n;
        /* every libsa1 event against the reference's calls of the same step */
        for (i = 0; i < S.steps; i++) {
            for (j = S.first[i]; j < S.first[i + 1]; j++) {
                const sa1_event *e = &S.log->ev[j];
                uint32_t c, type, cid;
                int found = 0;
                for (c = 0; c < ncalls && !found; c++) {
                    if (calls[c].step != (int)i || calls[c].matched || !expect_of(&calls[c], &type, &cid)) {
                        continue;
                    }
                    if (type == e->type && cid == e->id && (e->type != SA1_EVENT_SOUND || e->arg == calls[c].addr)) {
                        float hp[3], hq[3];
                        calls[c].matched = 1;
                        found = 1;
                        matched++;
                        if (idl + 16 < sizeof ids && matched <= 6) {
                            idl += (size_t)snprintf(ids + idl, sizeof ids - idl, "%s%X@%u", idl ? " " : "", (unsigned)e->id,
                                                    (unsigned)i);
                        }
                        /* the position: the task's at the call (8C094330 / 8C0941FC: twp->pos, before or after this
                         * step's movement) or, for the sounds without one, the character's after the step */
                        sa1_to_host_point(ctx, S.pos[i], hp);
                        sa1_to_host_point(ctx, i > 0 ? S.pos[i - 1] : S.pos[i], hq);
                        if (calls[c].addr == 0x8C094330u || calls[c].addr == 0x8C0941FCu) {
                            pos_task++;
                            pos_bad += memcmp(e->position, hp, 12) != 0 && memcmp(e->position, hq, 12) != 0;
                        } else if (calls[c].addr == 0x8C0940DAu) {
                            pos_chr++;
                            pos_bad += memcmp(e->position, hp, 12) != 0;
                        }
                    }
                }
                if (!found) {
                    extra++;
                    if (extra <= 4) {
                        printf("      libsa1 only: step %u type %u id %X arg %08X\n", (unsigned)i, (unsigned)e->type,
                               (unsigned)e->id, (unsigned)e->arg);
                    }
                }
            }
        }
        for (i = 0; i < ncalls; i++) {
            uint32_t type, cid;
            if (calls[i].matched || calls[i].step >= (int)S.steps || !expect_of(&calls[i], &type, &cid)) {
                continue;
            }
            /* the stage tasks are not in a libsa1 world: STG01's task 0C900000 starts two looping SEs with no owner
             * (twp = -1) at step 0 and the stage music (8C107D80 / 8C107AC6) at step 11; from step 144 of
             * ec1_spindash the reference's player rolls into the STG01 ocean task's water plane (replay_test's known
             * issue): its splashes (2CD) have no libsa1 counterpart */
            if ((calls[i].addr == 0x8C0941FCu && calls[i].a[1] == 0xFFFFFFFFu) || type == SA1_EVENT_JINGLE) {
                stage_only++;
            } else if (!strcmp(names[k], "ec1_spindash") && calls[i].step >= 144) {
                water_only++;
            } else {
                other_ref++;
                if (other_ref <= 4) {
                    printf("      reference only: step %d %08X id %X\n", calls[i].step, (unsigned)calls[i].addr, (unsigned)cid);
                }
            }
        }
        CHECK(matched > 0 && extra == 0 && other_ref == 0 && pos_bad == 0 && S.log->lost == 0,
              "%s: %u events, each the reference's call of the same step (%s ...); the reference's %u calls of the stage "
              "tasks (no libsa1 world has them) and %u after its water contact aside, none missing; positions in host "
              "space: %u of the owning task, %u at the character, %u wrong", names[k], (unsigned)matched, ids,
              (unsigned)stage_only, (unsigned)water_only, (unsigned)pos_task, (unsigned)pos_chr, (unsigned)pos_bad);
        sa1_character_destroy(ctx, id);
        free(calls);
        free(S.log);
        free(S.first);
        free(S.pos);
        wr_trace_free(&t);
    }
    if (ctx != NULL) {
        free_ctx(ctx, &dump);
    }
}

/* ---- 5. live ----------------------------------------------------------------------------------------------- */
typedef struct live_mesh {
    sa1_mesh m;
    float *pos;
} live_mesh;

static int live_mesh_init(live_mesh *L, uint32_t cap)
{
    memset(L, 0, sizeof *L);
    L->m.struct_size = sizeof L->m;
    L->m.max_triangles = cap;
    L->pos = (float *)malloc((size_t)cap * 9u * sizeof(float) + 4u);
    L->m.position = L->pos;
    L->m.normal = (float *)malloc((size_t)cap * 9u * sizeof(float) + 4u);
    L->m.uv = (float *)malloc((size_t)cap * 6u * sizeof(float) + 4u);
    L->m.color = (uint8_t *)malloc((size_t)cap * 12u + 4u);
    L->m.texture = (int16_t *)malloc((size_t)cap * 2u + 4u);
    L->m.material = (uint32_t *)malloc((size_t)cap * 4u + 4u);
    return L->pos && L->m.normal && L->m.uv && L->m.color && L->m.texture && L->m.material ? 0 : -1;
}

static void live_mesh_free(live_mesh *L)
{
    free(L->pos);
    free(L->m.normal);
    free(L->m.uv);
    free(L->m.color);
    free(L->m.texture);
    free(L->m.material);
}

/* the centroid of the mesh's corners */
static void centroid(const live_mesh *L, double c[3])
{
    uint32_t k;
    c[0] = c[1] = c[2] = 0.0;
    for (k = 0; k < 3u * L->m.num_triangles; k++) {
        c[0] += L->pos[3 * k];
        c[1] += L->pos[3 * k + 1];
        c[2] += L->pos[3 * k + 2];
    }
    if (L->m.num_triangles) {
        c[0] /= 3.0 * L->m.num_triangles;
        c[1] /= 3.0 * L->m.num_triangles;
        c[2] /= 3.0 * L->m.num_triangles;
    }
}

typedef struct live_run {
    uint32_t ticks, empty, bad_status, too_big, far_off, geom_mismatch, tex_ok;
    uint32_t tri_min, tri_max;
    double max_off;
    uint32_t sounds[64], sound_n[64], nsound;
    uint32_t jump_tick, jump_sound_tick, charge_ticks;
    uint32_t ev_far;
    uint32_t foreign, emitted, clipped, excluded;
    uint32_t tris, ignspec, faded;  /* triangles; with SA1_MAT_IGNORE_SPECULAR; with a corner alpha below 255 */
} live_run;

static void live_tick(sa1_context *ctx, sa1_character_id id, const sa1_input *in, live_mesh *L, live_run *R)
{
    sa1_state s;
    sa1_event ev[64];
    uint32_t n = 0, k, t;
    const sa1_geometry *g = NULL;
    double c[3], d;
    sa1_status st;
    memset(&s, 0, sizeof s);
    s.struct_size = sizeof s;
    st = sa1_character_tick(ctx, id, in, 1.0f / 30.0f, &s, &L->m);
    R->ticks++;
    if (st != SA1_OK) {
        R->bad_status++;
        if (R->bad_status == 1) {
            printf("      tick %u: %s: %s\n", (unsigned)R->ticks, sa1_status_string(st), sa1_last_error(ctx));
        }
        return;
    }
    if (L->m.num_triangles == 0) {
        R->empty++;
    }
    if (L->m.num_triangles != L->m.triangles_needed) {
        R->too_big++;
    }
    R->tri_min = L->m.num_triangles < R->tri_min ? L->m.num_triangles : R->tri_min;
    R->tri_max = L->m.num_triangles > R->tri_max ? L->m.num_triangles : R->tri_max;
    /* the origin of the first draw (the body's root part): the mesh is the pair's sub step 0 (the hand-written
     * renderers draw then), the state is after sub step 1, one 60 Hz step of the velocity further; effects (the
     * spin dash trail ...) may be anywhere */
    if (sa1_character_geometry(ctx, id, &g) == SA1_OK && g->num_nodes > 0) {
        c[0] = g->node_matrices[3];
        c[1] = g->node_matrices[7];
        c[2] = g->node_matrices[11];
    } else {
        centroid(L, c);
    }
    for (k = 0; k < 3; k++) {
        c[k] -= (double)s.position[k] - (double)s.velocity[k] / 60.0;
    }
    d = sqrt(c[0] * c[0] + c[1] * c[1] + c[2] * c[2]);
    R->max_off = d > R->max_off ? d : R->max_off;
    if (d > 60.0) {
        if (R->far_off++ == 0) {
            const sa1_geometry *gg = NULL;
            printf("      tick %u: the first draw %.1f host units from the character (action %d); draws far from it:",
                   (unsigned)R->ticks, d, (int)s.action);
            if (sa1_character_geometry(ctx, id, &gg) == SA1_OK) {
                uint32_t q, shown = 0;
                for (q = 0; q < gg->num_nodes && shown < 6; q++) {
                    const float *m = gg->node_matrices + 12u * q;
                    double dx = m[3] - s.position[0], dy = m[7] - s.position[1], dz = m[11] - s.position[2];
                    if (sqrt(dx * dx + dy * dy + dz * dz) > 200.0) {
                        printf(" #%u at (%.0f, %.0f, %.0f)", (unsigned)q, (double)m[3], (double)m[7], (double)m[11]);
                        shown++;
                    }
                }
            }
            printf("\n");
        }
    }
    for (t = 0; t < L->m.num_triangles; t++) {
        R->tex_ok += L->m.texture[t] >= 0;
        R->ignspec += (L->m.material[t] & SA1_MAT_IGNORE_SPECULAR) != 0;
        R->faded += L->m.color[12u * t + 3u] < 255u;
        R->tris += 1u;
    }
    {
        /* mesh_out (no render streams) = the non-effect triangles of the render output; the indexed geometry holds them
           all (effects too) */
        const sa1_render *rr = NULL;
        uint32_t d, own = 0;
        if (sa1_character_render(ctx, id, 1.0f, &rr) == SA1_OK) {
            for (d = 0; d < rr->num_draws; d++) {
                if (rr->draws[d].prim == SA1_PRIM_TRIANGLES && !(rr->draws[d].flags & SA1_DRAW_EFFECT)) {
                    own += rr->draws[d].count;
                }
            }
        }
        if (sa1_character_geometry(ctx, id, &g) != SA1_OK || rr == NULL || own != L->m.num_triangles ||
            g->num_triangles != rr->num_triangles) {
            R->geom_mismatch++;
        }
    }
    {
        sa1_capture_stats cs;
        sa1_capture_get_stats(sa1_character_capture(ctx, id), &cs);
        R->foreign += cs.foreign;
        R->emitted += cs.emitted;
        R->clipped += cs.clipped;
        R->excluded += cs.excluded;
    }
    if (sa1_poll_events(ctx, ev, 64, &n) == SA1_OK) {
        for (k = 0; k < n; k++) {
            uint32_t j;
            double e = sqrt((ev[k].position[0] - s.position[0]) * (ev[k].position[0] - s.position[0]) +
                            (ev[k].position[1] - s.position[1]) * (ev[k].position[1] - s.position[1]) +
                            (ev[k].position[2] - s.position[2]) * (ev[k].position[2] - s.position[2]));
            if (ev[k].type != SA1_EVENT_SOUND) {
                continue;
            }
            if (e > 100.0) {
                R->ev_far++;
            }
            for (j = 0; j < R->nsound && R->sounds[j] != ev[k].id; j++) {
            }
            if (j == R->nsound && R->nsound < 64) {
                R->sounds[R->nsound++] = ev[k].id;
                R->sound_n[j] = 0;
            }
            if (j < 64) {
                R->sound_n[j]++;
            }
            if (ev[k].id == 0x11 && R->jump_sound_tick == 0) {
                R->jump_sound_tick = R->ticks;
            }
            if (ev[k].id == 0x2D8) {
                R->charge_ticks++;
            }
        }
    }
}

static void test_live(const env *E)
{
    static const int32_t others[] = { SA1_CHAR_TAILS, SA1_CHAR_KNUCKLES, SA1_CHAR_AMY, SA1_CHAR_GAMMA, SA1_CHAR_BIG };
    wr_trace t;
    char prefix[1024];
    sa1ca_dump dump;
    sa1_context *ctx;
    sa1_character_id id = 0;
    sa1_character_info ci;
    sa1_input in;
    live_mesh L;
    live_run R;
    float pos[3], yaw;
    uint32_t i, k, maxt = 0;
    double run_yaw;
    printf("5. live (sa1_character_tick, FAITHFUL L01A0, Source coordinates):\n");
    if (load_trace(E, "ec1_run_jump", &t) != 0 || land_prefix(E, &t.h, prefix, sizeof prefix) != 0) {
        printf("  skip  no ec1_run_jump trace / landtable dump\n");
        g_skip++;
        return;
    }
    ctx = make_ctx(E, (1u << SA1_CHAR_SONIC) | (1u << SA1_CHAR_TAILS) | (1u << SA1_CHAR_KNUCKLES) | (1u << SA1_CHAR_AMY) |
                          (1u << SA1_CHAR_GAMMA) | (1u << SA1_CHAR_BIG), prefix, &dump);
    if (ctx == NULL) {
        CHECK(0, "context");
        wr_trace_free(&t);
        return;
    }
    for (k = 0; k < SA1_CHAR_COUNT; k++) {
        memset(&ci, 0, sizeof ci);
        ci.struct_size = sizeof ci;
        if (sa1_character_info_get(ctx, (int32_t)k, &ci) == SA1_OK && ci.max_triangles > maxt) {
            maxt = ci.max_triangles;
        }
    }
    if (live_mesh_init(&L, 4u * maxt) != 0) {
        CHECK(0, "mesh buffers");
        free_ctx(ctx, &dump);
        wr_trace_free(&t);
        return;
    }
    pos[0] = t.h.start_pos[0] * 4.0f;
    pos[1] = -t.h.start_pos[2] * 4.0f;
    pos[2] = t.h.start_pos[1] * 4.0f;
    yaw = (float)(-(double)(int16_t)(uint16_t)t.h.start_ang[1] * (6.28318530717958647692 / 65536.0));
    run_yaw = -(double)0x6C21 * (3.14159265358979323846 / 32768.0);
    memset(&in, 0, sizeof in);
    in.struct_size = sizeof in;
    in.camera_yaw = (float)run_yaw;
    memset(&R, 0, sizeof R);
    R.tri_min = 0xFFFFFFFFu;
    if (sa1_character_create(ctx, SA1_CHAR_SONIC, pos, yaw, &id) != SA1_OK) {
        CHECK(0, "create Sonic");
    } else {
        for (i = 0; i < 30; i++) {
            live_tick(ctx, id, &in, &L, &R);                    /* fall to the deck, stand */
        }
        in.stick_y = 1.0f;
        for (i = 0; i < 40; i++) {
            live_tick(ctx, id, &in, &L, &R);                    /* run */
        }
        in.buttons = SA1_BUTTON_JUMP;
        R.jump_tick = R.ticks + 1u;
        live_tick(ctx, id, &in, &L, &R);                        /* jump */
        in.buttons = 0;
        for (i = 0; i < 40; i++) {
            live_tick(ctx, id, &in, &L, &R);                    /* fly, land, run */
        }
        in.stick_y = 0.0f;
        for (i = 0; i < 40; i++) {
            live_tick(ctx, id, &in, &L, &R);                    /* stop */
        }
        in.buttons = SA1_BUTTON_ACTION;
        for (i = 0; i < 20; i++) {
            live_tick(ctx, id, &in, &L, &R);                    /* spin dash: charge */
        }
        in.buttons = 0;
        for (i = 0; i < 30; i++) {
            live_tick(ctx, id, &in, &L, &R);                    /* release */
        }
        CHECK(R.bad_status == 0 && R.empty == 0 && R.too_big == 0 && R.geom_mismatch == 0 && R.far_off == 0 && R.tex_ok > 0,
              "Sonic, %u ticks with mesh_out: every mesh from the capture (%u-%u triangles, all of them written, "
              "sa1_character_geometry the same), the body's root within %.1f host units of the state's position",
              (unsigned)R.ticks, (unsigned)R.tri_min, (unsigned)R.tri_max, R.max_off);
        printf("      draws over the ticks: %u emitted, %u with another texlist than the character's (effects), %u blob "
               "shadows left out, %u parked beyond the clip radius\n", (unsigned)R.emitted, (unsigned)R.foreign,
               (unsigned)R.excluded, (unsigned)R.clipped);
        CHECK(R.tris > 0 && R.ignspec == R.tris,
              "the draw state applied: all %u triangles carry SA1_MAT_IGNORE_SPECULAR, which SonicDisplay ORs into the "
              "constant attribute mask (8C05694A(0, 0x80000)) under NJD_CONTROL_3D_CONSTANT_ATTR", (unsigned)R.tris);
        printf("      sound ids:");
        for (k = 0; k < R.nsound; k++) {
            printf(" %X x%u", (unsigned)R.sounds[k], (unsigned)R.sound_n[k]);
        }
        printf("\n");
        CHECK(R.jump_sound_tick >= R.jump_tick && R.jump_sound_tick <= R.jump_tick + 1u && R.charge_ticks > 0 &&
              R.ev_far == 0, "events: the jump SE 11 in the tick of the jump (%u, pressed in %u), the spin dash charge "
              "SE 2D8 in %u events, every sound within 100 host units of the character", (unsigned)R.jump_sound_tick,
              (unsigned)R.jump_tick, (unsigned)R.charge_ticks);
        sa1_character_destroy(ctx, id);
    }
    /* the other characters that tick: their displays draw too while the window is open */
    for (k = 0; k < sizeof others / sizeof others[0]; k++) {
        sa1_character_id oid = 0;
        memset(&R, 0, sizeof R);
        R.tri_min = 0xFFFFFFFFu;
        memset(&in, 0, sizeof in);
        in.struct_size = sizeof in;
        in.camera_yaw = (float)run_yaw;
        if (sa1_character_create(ctx, others[k], pos, yaw, &oid) != SA1_OK) {
            CHECK(0, "create %s", sa1_character_name(others[k]));
            continue;
        }
        for (i = 0; i < 60; i++) {
            in.stick_y = i >= 20 ? 1.0f : 0.0f;
            in.buttons = (i == 40) ? SA1_BUTTON_JUMP : 0u;
            live_tick(ctx, oid, &in, &L, &R);
        }
        CHECK(R.bad_status == 0 && R.too_big == 0 && R.geom_mismatch == 0 && R.empty < R.ticks,
              "%s: 60 ticks with mesh_out, no fault; %u-%u triangles (%u empty meshes), the first draw within %.1f "
              "host units",
              sa1_character_name(others[k]), (unsigned)(R.tri_min == 0xFFFFFFFFu ? 0u : R.tri_min), (unsigned)R.tri_max,
              (unsigned)R.empty, R.max_off);
        printf("      draws over the ticks: %u emitted, %u with another texlist than the character's (effects), %u blob "
               "shadows left out, %u parked beyond the clip radius; %u of %u triangles fade (alpha below 255: the "
               "constant material's alpha offset)\n", (unsigned)R.emitted, (unsigned)R.foreign, (unsigned)R.excluded,
               (unsigned)R.clipped, (unsigned)R.faded, (unsigned)R.tris);
        if (others[k] == SA1_CHAR_TAILS) {
            CHECK(R.faded > 0, "Tails' fading tail copies (alpha offsets -1/3, -2/3 under NJD_CONTROL_3D_OFFSET_MATERIAL) "
                  "come out with alpha below 255 (%u triangles)", (unsigned)R.faded);
        }
        sa1_character_destroy(ctx, oid);
    }
    live_mesh_free(&L);
    free_ctx(ctx, &dump);
    wr_trace_free(&t);
}

/* ---- 6. textures: every texture decodes; mipmapped ones agree with their own next mip level ---------------------- */
/* The decoder (src/sa1_pvr.c) was checked texel for texel against an independent reference (tests/texdump_ref.py,
 * which matches SA Tools' ArchiveTool on all 782 character / effect / common textures). This part is a self-contained
 * guard that needs no reference data: the full-size level of a mipmapped texture, box-filtered to half size, must look
 * like the half-size level stored in the same chunk (both read with the decoder's layout rules). A misplaced full-size
 * level (the 6-byte padding of the old decoder: a 2-texel shift in twiddled order) scrambles it and fails. */
static uint32_t mip16_offset(uint32_t w, uint32_t pad)
{
    uint32_t off = pad, s;
    for (s = 1; s < w; s *= 2u) {
        off += 2u * s * s;
    }
    return off;
}

static uint32_t mipvq_offset(uint32_t w)
{
    uint32_t off = 1, s;
    for (s = 2; s < w; s *= 2u) {
        off += (s / 2u) * (s / 2u);
    }
    return off;
}

/* mean absolute difference (0..255) between the full level box-filtered to half size and the half level of a chunk;
 * shift: bytes added to the stored layout's offsets (0 = the decoder's; 4 = the old decoder's mistake); < 0: n/a */
static double mip_error(const uint8_t *chunk, uint32_t len, uint32_t shift)
{
    uint32_t w = sa1_rd16(chunk + 12), h = sa1_rd16(chunk + 14), pf = chunk[8], df = chunk[9], hw = w / 2u, x, y, k;
    uint8_t *tmp, *full, *half;
    uint32_t body = 16, lvl_full, lvl_half, n;
    double err = 0.0;
    size_t cl;
    if (w != h || w < 4 || (df != 2 && df != 0x12 && df != 4))
        return -1.0;
    if (df == 4) {
        lvl_full = 2048u + mipvq_offset(w);
        lvl_half = 2048u + mipvq_offset(hw);
        n = (hw / 2u) * (hw / 2u);
    } else {
        lvl_full = mip16_offset(w, df == 2 ? 2u : 6u);
        lvl_half = mip16_offset(hw, df == 2 ? 2u : 6u);
        n = hw * hw * 2u;
    }
    lvl_full += shift;
    lvl_half += shift;
    cl = 16u + (df == 4 ? 2048u : 0u) + n;
    if (body + lvl_half + n > len || body + lvl_full + (df == 4 ? (w / 2u) * (w / 2u) : w * w * 2u) > len) {
        return -1.0;
    }
    tmp = (uint8_t *)calloc(1, cl + (size_t)(df == 4 ? (w / 2u) * (w / 2u) : w * w * 2u) + 2048u + 16u);
    full = (uint8_t *)malloc((size_t)w * w * 4u);
    half = (uint8_t *)malloc((size_t)hw * hw * 4u);
    if (tmp == NULL || full == NULL || half == NULL) {
        free(tmp);
        free(full);
        free(half);
        return -1.0;
    }
    /* a chunk of each level alone, without mipmaps (data format 1 / 3) */
    memcpy(tmp, chunk, 16);
    tmp[9] = (uint8_t)(df == 4 ? 3 : 1);
    if (df == 4) {
        memcpy(tmp + 16, chunk + body, 2048);
        memcpy(tmp + 16 + 2048, chunk + body + lvl_full, (size_t)(w / 2u) * (w / 2u));
    } else {
        memcpy(tmp + 16, chunk + body + lvl_full, (size_t)w * w * 2u);
    }
    tmp[4] = tmp[5] = tmp[6] = tmp[7] = 0;
    if (sa1_pvr_decode(tmp, (uint32_t)(16u + (df == 4 ? 2048u + (w / 2u) * (w / 2u) : w * w * 2u)), full,
                       (size_t)w * w * 4u) != SA1_OK) {
        err = -1.0;
    }
    tmp[12] = (uint8_t)hw;
    tmp[13] = (uint8_t)(hw >> 8);
    tmp[14] = (uint8_t)hw;
    tmp[15] = (uint8_t)(hw >> 8);
    if (df == 4) {
        memcpy(tmp + 16 + 2048, chunk + body + lvl_half, n);
    } else {
        memcpy(tmp + 16, chunk + body + lvl_half, n);
    }
    if (err == 0.0 && sa1_pvr_decode(tmp, (uint32_t)(16u + (df == 4 ? 2048u : 0u) + n), half, (size_t)hw * hw * 4u) != SA1_OK) {
        err = -1.0;
    }
    if (err == 0.0) {
        for (y = 0; y < hw; y++) {
            for (x = 0; x < hw; x++) {
                for (k = 0; k < 3; k++) {
                    double b = ((double)full[4u * ((2u * y) * w + 2u * x) + k] + full[4u * ((2u * y) * w + 2u * x + 1u) + k] +
                                full[4u * ((2u * y + 1u) * w + 2u * x) + k] + full[4u * ((2u * y + 1u) * w + 2u * x + 1u) + k]) / 4.0;
                    err += fabs(b - (double)half[4u * (y * hw + x) + k]);
                }
            }
        }
        err /= 3.0 * hw * hw;
    }
    free(tmp);
    free(full);
    free(half);
    (void)pf;
    return err;
}

static void test_textures(const env *E)
{
    sa1_context *ctx = NULL;
    sa1_host h;
    sa1_disc disc;
    uint32_t k, a, total = 0, decoded = 0, mips = 0, mips_ok = 0, shifted_bad = 0, better = 0, alpha_kind[3] = { 0, 0, 0 };
    double worst = 0.0, best_shift = 1e9;
    char worst_name[64] = "-";
    printf("6. textures: every archive texture decodes; mipmapped ones match their own half-size level:\n");
    sa1_host_defaults(&h);
    h.log = log_cb;
    h.log_level = SA1_LOG_WARN;
    if (sa1_init(&h, &ctx) != SA1_OK || SA1_FAILED(sa1_load_disc(ctx, E->gdi, NULL))) {
        CHECK(0, "context / disc");
        sa1_shutdown(ctx);
        return;
    }
    memset(&disc, 0, sizeof disc);
    if (sa1_disc_open(ctx, E->gdi, &disc) != SA1_OK) {
        CHECK(0, "disc open");
        sa1_shutdown(ctx);
        return;
    }
    for (k = 0; k < SA1_CHAR_COUNT; k++) {
        const sa1_charset *cs = &ctx->chars[k];
        for (a = 0; cs->loaded && a < cs->num_archives; a++) {
            const sa1_archive *ar = &cs->archives[a];
            char path[64];
            uint8_t *raw = NULL, *pvm = NULL;
            uint32_t rsize = 0, psize = 0, n = 0, i;
            sa1_pvm_entry *ents;
            for (i = 0; i < ar->num_textures; i++) {
                sa1_texture_info ti;
                memset(&ti, 0, sizeof ti);
                ti.struct_size = sizeof ti;
                total++;
                if (sa1_texture_get(ctx, (int32_t)k, a, i, &ti) == SA1_OK && ti.rgba != NULL) {
                    decoded++;
                    if (ti.alpha < 3) {
                        alpha_kind[ti.alpha]++;
                    }
                }
            }
            snprintf(path, sizeof path, "SONICADV/%s.PVR", ar->name);
            if (ar->num_textures == 1 && sa1_disc_read_file(&disc, path, &raw, &rsize) == SA1_OK) {
                /* round 0.9.5 (095-lib-board): the board's archives are standalone PVRs (sa1_loader.c load_pvr_archive): the
                   PVRT after an optional GBIX chunk; mip-checked as the PVM entries are */
                uint32_t pos = 0;
                if (rsize >= 8u && memcmp(raw, "GBIX", 4) == 0) {
                    pos = 8u + sa1_rd32(raw + 4);
                }
                if (pos + 16u <= rsize && memcmp(raw + pos, "PVRT", 4) == 0) {
                    double e0 = mip_error(raw + pos, rsize - pos, 0), e4 = mip_error(raw + pos, rsize - pos, 4);
                    if (e0 >= 0.0) {
                        mips++;
                        mips_ok += e0 < 24.0;
                        better += e4 < 0.0 || e0 <= e4 + 0.5;
                        if (e0 > worst) {
                            worst = e0;
                            snprintf(worst_name, sizeof worst_name, "%s", ar->name);
                        }
                        if (e4 >= 0.0) {
                            shifted_bad += e4 > e0 + 2.0;
                            best_shift = e4 < best_shift ? e4 : best_shift;
                        }
                    }
                } else {
                    CHECK(0, "%s: not a PVR texture", path);
                }
                sa1_mem_free(ctx, raw);
                continue;
            }
            raw = NULL;
            snprintf(path, sizeof path, "SONICADV/%s.PRS", ar->name);
            if (sa1_disc_read_file(&disc, path, &raw, &rsize) != SA1_OK ||
                sa1_prs_decompress(ctx, raw, rsize, 16u << 20, &pvm, &psize) != SA1_OK) {
                CHECK(0, "%s: read / unpack", path);
                sa1_mem_free(ctx, raw);
                continue;
            }
            ents = (sa1_pvm_entry *)calloc(4096, sizeof *ents);
            if (ents != NULL && sa1_pvm_parse(pvm, psize, ents, 4096, &n) == SA1_OK) {
                for (i = 0; i < n; i++) {
                    double e0 = mip_error(pvm + ents[i].offset, ents[i].length, 0);
                    double e4 = mip_error(pvm + ents[i].offset, ents[i].length, 4);
                    if (e0 < 0.0) {
                        continue;
                    }
                    mips++;
                    mips_ok += e0 < 24.0;
                    better += e4 < 0.0 || e0 <= e4 + 0.5;
                    if (e0 > worst) {
                        worst = e0;
                        snprintf(worst_name, sizeof worst_name, "%s/%s", ar->name, ents[i].name);
                    }
                    if (e4 >= 0.0) {
                        shifted_bad += e4 > e0 + 2.0;
                        best_shift = e4 < best_shift ? e4 : best_shift;
                    }
                }
            }
            free(ents);
            sa1_mem_free(ctx, raw);
            sa1_mem_free(ctx, pvm);
        }
    }
    sa1_disc_close(&disc);
    CHECK(total > 0 && decoded == total, "%u of %u textures of the characters' archives decode (alpha: %u opaque, %u 1-bit, "
          "%u graded)", (unsigned)decoded, (unsigned)total, (unsigned)alpha_kind[0], (unsigned)alpha_kind[1],
          (unsigned)alpha_kind[2]);
    CHECK(mips > 0 && mips_ok == mips && better == mips && shifted_bad * 10u >= mips * 8u,
          "%u mipmapped textures: the full-size level box-filtered agrees with the stored half-size level within 24/255 "
          "on average (worst %.2f: %s; the game's mips are not all plain box filters), %u at least as well as when read "
          "4 bytes further on (the padding error of the old decoder), which %u of them clearly fail (the rest are nearly "
          "uniform; best %.1f)", (unsigned)mips, worst, worst_name, (unsigned)better, (unsigned)shifted_bad, best_shift);
    sa1_shutdown(ctx);
}
/* ---- 7. render output: materials, eyes, afterimages, effects, interpolation ----------------------------------- */
/* a context with one character kind loaded and Emerald Coast act 1's own collision (DISC) */
static sa1_context *disc_ctx(const env *E, int32_t kind)
{
    sa1_context *ctx = NULL;
    sa1_host h;
    sa1_load_options lo;
    sa1_collision_static col;
    sa1_host_defaults(&h);
    h.log = log_cb;
    h.log_level = SA1_LOG_WARN;
    if (sa1_init(&h, &ctx) != SA1_OK) {
        return NULL;
    }
    memset(&lo, 0, sizeof lo);
    lo.struct_size = sizeof lo;
    lo.characters = 1u << kind;
    lo.flags = SA1_LOAD_DEFAULT;
    memset(&col, 0, sizeof col);
    col.struct_size = sizeof col;
    col.mode = SA1_COLLISION_MODE_DISC;
    col.disc_level = 1;
    col.disc_act = 0;
    if (SA1_FAILED(sa1_load_disc(ctx, E->gdi, &lo)) || sa1_collision_set_static_ex(ctx, &col) != SA1_OK) {
        printf("    context: %s\n", sa1_last_error(ctx));
        sa1_shutdown(ctx);
        return NULL;
    }
    return ctx;
}

static sa1_character_id disc_char(sa1_context *ctx, int32_t kind)
{
    sa1_character_desc d;
    sa1_character_id id = 0;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.kind = kind;
    d.flags = SA1_CREATE_WORLD | SA1_CREATE_START_TABLE;
    if (SA1_FAILED(sa1_character_create_ex(ctx, &d, &id))) {
        printf("    create %s: %s\n", sa1_character_name(kind), sa1_last_error(ctx));
        return 0;
    }
    return id;
}

/* "<pairs><cmd>" groups: n nothing, f forward, a action, j jump, b action + forward; one pair per tick */
static int run_ticks(sa1_context *ctx, sa1_character_id id, const char *script)
{
    sa1_input in;
    const char *p = script;
    memset(&in, 0, sizeof in);
    in.struct_size = sizeof in;
    while (*p) {
        char *e;
        long n = strtol(p, &e, 10), i;
        char cmd;
        if (e == p || !*e) {
            return -1;
        }
        cmd = *e;
        p = e + 1;
        in.stick_y = (cmd == 'f' || cmd == 'b') ? 1.0f : 0.0f;
        in.buttons = (cmd == 'a' || cmd == 'b') ? SA1_BUTTON_ACTION : cmd == 'j' ? SA1_BUTTON_JUMP : 0u;
        for (i = 0; i < n; i++) {
            if (sa1_character_tick(ctx, id, &in, 1.0f / 30.0f, NULL, NULL) != SA1_OK) {
                printf("    tick: %s\n", sa1_last_error(ctx));
                return -1;
            }
        }
    }
    return 0;
}

static int32_t tex_named(const sa1_context *ctx, int32_t kind, uint32_t archive, const char *name)
{
    uint32_t n = 0, i;
    if (sa1_texture_archive(ctx, kind, archive, NULL, &n) != SA1_OK) {
        return -1;
    }
    for (i = 0; i < n; i++) {
        sa1_texture_info ti;
        memset(&ti, 0, sizeof ti);
        ti.struct_size = sizeof ti;
        if (sa1_texture_get(ctx, kind, archive, i, &ti) == SA1_OK && ti.name && !strcmp(ti.name, name)) {
            return (int32_t)i;
        }
    }
    return -1;
}

typedef struct draw_census {
    uint32_t draws, tris, blend, test, use_alpha, palette, effect, afterimage, additive, notex, sprites, lines;
    uint32_t effect_tris, by_archive[SA1_MAX_ARCHIVES], white_bad, ignspec_tris;
    uint32_t shadow, shadow_tris;   /* the blob shadow's draws (SA1_DRAW_SHADOW, 0.6.0 fixes): left out of the rest */
} draw_census;

static void census(const sa1_render *r, draw_census *c)
{
    uint32_t d, t, k;
    memset(c, 0, sizeof *c);
    c->draws = r->num_draws;
    c->tris = r->num_triangles;
    c->sprites = r->num_sprites;
    c->lines = r->num_line_vertices / 2u;
    for (d = 0; d < r->num_draws; d++) {
        const sa1_draw *dr = &r->draws[d];
        if (dr->flags & SA1_DRAW_SHADOW) {
            /* the game's blob shadow (sadc docs/research/gsa1_060_shadow.md L3 / Review R.4.3): counted on its own */
            c->shadow++;
            c->draws--;
            if (dr->prim == SA1_PRIM_TRIANGLES) {
                c->shadow_tris += dr->count;
                c->tris -= dr->count;
            }
            continue;
        }
        c->blend += (dr->flags & SA1_DRAW_BLEND) != 0;
        c->test += (dr->flags & SA1_DRAW_ALPHA_TEST) != 0;
        c->use_alpha += (dr->flags & SA1_DRAW_USE_ALPHA) != 0;
        c->palette += (dr->flags & SA1_DRAW_PALETTE_LIT) != 0;
        c->effect += (dr->flags & SA1_DRAW_EFFECT) != 0;
        c->afterimage += (dr->flags & SA1_DRAW_AFTERIMAGE) != 0;
        c->additive += (dr->flags & SA1_DRAW_ADDITIVE) != 0;
        c->notex += (dr->flags & SA1_DRAW_NO_TEXTURE) != 0;
        if (dr->archive >= 0 && dr->archive < (int32_t)SA1_MAX_ARCHIVES) {
            c->by_archive[dr->archive]++;
        }
        if (dr->prim != SA1_PRIM_TRIANGLES) {
            continue;
        }
        if (dr->flags & SA1_DRAW_EFFECT) {
            c->effect_tris += dr->count;
        }
        for (t = dr->first; t < dr->first + dr->count; t++) {
            c->ignspec_tris += (dr->material & SA1_MAT_IGNORE_SPECULAR) != 0;
            if (dr->flags & SA1_DRAW_PALETTE_LIT) {
                for (k = 0; k < 3; k++) {
                    const uint8_t *col = r->colors + 12u * t + 4u * k;
                    c->white_bad += col[0] != 255 || col[1] != 255 || col[2] != 255;
                }
            }
        }
    }
}

/* corners of the triangles of draws with texture (archive 0, tex) */
static uint32_t corners_of(const sa1_render *r, int32_t tex, float *out, uint32_t cap, int *blended)
{
    uint32_t d, t, n = 0;
    *blended = 0;
    for (d = 0; d < r->num_draws; d++) {
        const sa1_draw *dr = &r->draws[d];
        if (dr->prim != SA1_PRIM_TRIANGLES || dr->archive != 0 || dr->texture != tex) {
            continue;
        }
        *blended |= (dr->flags & SA1_DRAW_BLEND) != 0;
        for (t = dr->first; t < dr->first + dr->count && n + 3u <= cap; t++) {
            memcpy(out + 3u * n, r->positions + 9u * t, 9 * sizeof(float));
            n += 3u;
        }
    }
    return n;
}

/* Sonic's eyes: the irises (stx_eye2) lie in front of the eye whites (stx_eye00) seen from his front, so a depth-tested
 * render shows them; both are opaque draws. Returns the corners covered by an eye white that are in front of it. */
static void eye_check(const sa1_context *ctx, const sa1_render *r)
{
    int32_t iris = tex_named(ctx, SA1_CHAR_SONIC, 0, "stx_eye2"), white = tex_named(ctx, SA1_CHAR_SONIC, 0, "stx_eye00");
    static float ic[3 * 600], wc[3 * 3000];
    uint32_t ni, nw, i, j, covered = 0, front = 0, black = 0, d, t, k;
    int ib = 0, wb = 0;
    float f[3], rt[3], ctr[3] = { 0.0f, 0.0f, 0.0f };
    ni = corners_of(r, iris, ic, 600, &ib);
    nw = corners_of(r, white, wc, 3000, &wb);
    f[0] = cosf(r->yaw);
    f[1] = sinf(r->yaw);
    f[2] = 0.0f;
    rt[0] = -f[1];
    rt[1] = f[0];
    rt[2] = 0.0f;
    for (i = 0; i < ni; i++) {
        for (k = 0; k < 3; k++) {
            ctr[k] += ic[3 * i + k] / (float)(ni ? ni : 1u);
        }
    }
    for (i = 0; i < ni; i++) {
        const float *v = ic + 3 * i;
        float vu = v[0] * rt[0] + v[1] * rt[1], vw = v[2], vd = v[0] * f[0] + v[1] * f[1];
        for (j = 0; j + 3 <= nw; j += 3) {
            const float *a = wc + 3 * j, *b = a + 3, *c = a + 6;
            float au = a[0] * rt[0] + a[1] * rt[1], bu = b[0] * rt[0] + b[1] * rt[1], cu = c[0] * rt[0] + c[1] * rt[1];
            float area = (bu - au) * (c[2] - a[2]) - (b[2] - a[2]) * (cu - au), l0, l1, l2, dd;
            float dx = a[0] - ctr[0], dy = a[1] - ctr[1], dz = a[2] - ctr[2];
            if (fabsf(area) < 1e-9f || dx * dx + dy * dy + dz * dz > 400.0f) {
                continue;   /* the gloves use the same white texture: only eye whites near the irises */
            }
            l0 = ((bu - vu) * (c[2] - vw) - (b[2] - vw) * (cu - vu)) / area;
            l1 = ((cu - vu) * (a[2] - vw) - (c[2] - vw) * (au - vu)) / area;
            l2 = 1.0f - l0 - l1;
            if (l0 < 0.0f || l1 < 0.0f || l2 < 0.0f) {
                continue;
            }
            dd = l0 * (a[0] * f[0] + a[1] * f[1]) + l1 * (b[0] * f[0] + b[1] * f[1]) + l2 * (c[0] * f[0] + c[1] * f[1]);
            covered++;
            front += vd >= dd - 0.01f;
            break;
        }
    }
    for (d = 0; d < r->num_draws; d++) {
        const sa1_draw *dr = &r->draws[d];
        if (dr->prim != SA1_PRIM_TRIANGLES || dr->archive != 0 || dr->texture != iris) {
            continue;
        }
        for (t = dr->first; t < dr->first + dr->count; t++) {
            for (k = 0; k < 3; k++) {
                const uint8_t *col = r->colors + 12u * t + 4u * k;
                black += col[0] < 128 && col[1] < 128 && col[2] < 128;
            }
        }
    }
    CHECK(iris >= 0 && white >= 0 && ni >= 12 && nw > 0 && !ib && !wb && black == 0 && covered > 0 &&
          front * 10u >= covered * 9u,
          "Sonic's eyes: %u iris corners (stx_eye2, its material's diffuse colour is black: vertex colours are the renderer's "
          "white light, %u dark), %u eye-white corners (stx_eye00), both opaque (USE_ALPHA without anything translucent); "
          "seen from his front %u of the %u iris corners over an eye white lie in front of it (a depth test shows them; "
          "drawn as translucent in the game's order - irises first - they would be covered)", (unsigned)ni,
          (unsigned)black, (unsigned)nw, (unsigned)front, (unsigned)covered);
}
/* Capture v3 (rt_draw.c rt_cap_model): the tails as the game drew them. Tails' display draws the two tail models
 * (8C273E3C / 8C274448, the morph nodes' models while standing) and then bends them for the next draw (8C0BDC80), on
 * both sub steps of a pair, while only sub step 0 draws: the points the next pair draws are the ones the arena holds
 * now. After one more pair, every draw of the tail models must carry exactly those points (the snapshot), the arena must
 * hold other ones (bent twice since; version 2 emitted these: the tails 0.5-0.65 SA1 units off at the tips), and the
 * emitted vertices must follow the snapshot (an emit with live_models, version 2's reads, differs by about as much). */
static void tails_drawn_tails(sa1_context *ctx, sa1_character_id id)
{
    static const uint32_t mdl[2] = { 0x8C273E3Cu, 0x8C274448u };
    const sa1g_world *w = sa1_character_world(ctx, id);
    sa1_capture *cap = sa1_character_capture(ctx, id);
    float before[2][96];
    const sa1_cap_rec *recs;
    sa1_capture_emit_opts eo;
    sa1_meshbuf b1, b2;
    sa1_geometry g1, g2;
    uint32_t k, i, n, draws = 0, equal = 0, models = 0, snaps = 0, lost = 0;
    double moved = 0.0, emit_diff = 0.0;
    if (w == NULL || cap == NULL) {
        CHECK(0, "Tails' drawn tails: no world / capture");
        return;
    }
    for (k = 0; k < 2; k++) {
        uint32_t pts = g_u32(w, mdl[k]);
        for (i = 0; i < 96; i++) {
            before[k][i] = g_f32(w, pts + 4u * i);
        }
    }
    if (run_ticks(ctx, id, "1n") != 0) {
        CHECK(0, "Tails' drawn tails: tick");
        return;
    }
    n = sa1_capture_records(cap, &recs);
    for (i = 0; i < n; i++) {
        sa1_emit_info ei;
        if (!SA1_CAP_IS_MODEL(recs[i].kind)) {
            continue;
        }
        models++;
        lost += (recs[i].flags & SA1_CAPF_PAY_LOST) != 0;
        memset(&ei, 0, sizeof ei);
        if (!sa1_capture_model_snapshot(cap, &recs[i], &ei)) {
            continue;
        }
        snaps++;
        for (k = 0; k < 2; k++) {
            uint32_t pts = g_u32(w, mdl[k]), j;
            if ((recs[i].obj & 0x1FFFFFFFu) != (mdl[k] & 0x1FFFFFFFu)) {
                continue;
            }
            draws++;
            equal += ei.snap_np == 32u && memcmp(ei.snap_pts, before[k], sizeof before[k]) == 0;
            for (j = 0; ei.snap_np == 32u && j < 32u; j++) {
                float s[3];
                double dx, dy, dz;
                memcpy(s, ei.snap_pts + 12u * j, sizeof s);
                dx = (double)g_f32(w, pts + 12u * j) - s[0];
                dy = (double)g_f32(w, pts + 12u * j + 4u) - s[1];
                dz = (double)g_f32(w, pts + 12u * j + 8u) - s[2];
                moved = fmax(moved, sqrt(dx * dx + dy * dy + dz * dz));
            }
        }
    }
    memset(&eo, 0, sizeof eo);
    eo.dedupe = 1;
    memset(&b1, 0, sizeof b1);
    memset(&b2, 0, sizeof b2);
    if (sa1_capture_emit_into(ctx, cap, &eo, &b1, &g1) == SA1_OK && (eo.live_models = 1,
        sa1_capture_emit_into(ctx, cap, &eo, &b2, &g2)) == SA1_OK && g1.num_vertices == g2.num_vertices) {
        for (i = 0; i < 3u * g1.num_vertices; i += 3u) {
            double dx = (double)g1.positions[i] - g2.positions[i], dy = (double)g1.positions[i + 1] - g2.positions[i + 1];
            double dz = (double)g1.positions[i + 2] - g2.positions[i + 2];
            emit_diff = fmax(emit_diff, sqrt(dx * dx + dy * dy + dz * dz));
        }
    } else {
        emit_diff = -1.0;
    }
    sa1_meshbuf_free(ctx, &b1);
    sa1_meshbuf_free(ctx, &b2);
    CHECK(snaps == models && lost == 0 && draws >= 6 && equal == draws && moved > 0.1 &&
              emit_diff > 0.1 * ctx->coords.scale && emit_diff < 2.0 * moved * ctx->coords.scale,
          "Tails' drawn tails (capture v3): %u of %u model draws carry their snapshot (%u lost); %u of %u draws of the two "
          "tail models hold exactly the points the arena held when the pair began (what sub step 0 draws), while the "
          "arena now holds them bent twice more (up to %.3f SA1 units away; version 2 emitted those); the emitted "
          "vertices follow the snapshot (an emit reading the models live differs by up to %.3f host units)",
          (unsigned)snaps, (unsigned)models, (unsigned)lost, (unsigned)equal, (unsigned)draws, moved, emit_diff);
}

static void print_census(const char *what, const draw_census *c)
{
    printf("      %s: %u triangles in %u draws (%u blended, %u alpha-tested, %u USE_ALPHA opaque, %u palette-lit, %u "
           "afterimage, %u additive, %u effects: %u triangles, %u untextured-unknown), %u sprites, %u line segments, "
           "blob shadow %u draws / %u triangles; draws per archive:", what, (unsigned)c->tris, (unsigned)c->draws,
           (unsigned)c->blend, (unsigned)c->test, (unsigned)c->use_alpha, (unsigned)c->palette, (unsigned)c->afterimage,
           (unsigned)c->additive, (unsigned)c->effect, (unsigned)c->effect_tris, (unsigned)c->notex,
           (unsigned)c->sprites, (unsigned)c->lines, (unsigned)c->shadow, (unsigned)c->shadow_tris);
    {
        uint32_t a;
        for (a = 0; a < SA1_MAX_ARCHIVES; a++) {
            if (c->by_archive[a]) {
                printf(" %u: %u", (unsigned)a, (unsigned)c->by_archive[a]);
            }
        }
    }
    printf("\n");
}

static float *copy_positions(const sa1_render *r)
{
    float *p = (float *)malloc(9u * sizeof(float) * (r->num_triangles + 1u));
    if (p != NULL && r->num_triangles) {
        memcpy(p, r->positions, 9u * sizeof(float) * r->num_triangles);
    }
    return p;
}

static void test_lerp(sa1_context *ctx, sa1_character_id id)
{
    const sa1_render *r = NULL;
    float *prev = NULL, *cur = NULL, *step = NULL, ppos[3], cpos[3];
    uint32_t n = 0, i, k, exact0 = 1, exact1 = 1, interp = 0, mismatch_mid = 0, ok_mesh = 1;
    double maxd = 0.0, maxstep = 0.0, mid_err = 0.0, acc_alpha = -1.0;
    float a;
    sa1_mesh m;
    sa1_input in;
    memset(&in, 0, sizeof in);
    in.struct_size = sizeof in;
    in.stick_y = 1.0f;
    /* two pairs of running: the last two pairs differ by a step of the run */
    run_ticks(ctx, id, "12f");
    if (sa1_character_render(ctx, id, 1.0f, &r) != SA1_OK) {
        CHECK(0, "render");
        return;
    }
    prev = copy_positions(r);
    memcpy(ppos, r->position, sizeof ppos);
    n = r->num_triangles;
    sa1_character_tick(ctx, id, &in, 1.0f / 30.0f, NULL, NULL);
    sa1_character_render(ctx, id, 1.0f, &r);
    cur = copy_positions(r);
    memcpy(cpos, r->position, sizeof cpos);
    exact1 = r->interpolated == 0 && r->num_triangles == n;
    for (i = 0; exact1 && i < 9u * n; i++) {
        double d = fabs((double)cur[i] - prev[i]);
        maxd = d > maxd ? d : maxd;
    }
    sa1_character_render(ctx, id, 0.0f, &r);
    interp = r->interpolated;
    for (i = 0; interp && i < 9u * n; i++) {
        exact0 &= r->positions[i] == prev[i];
    }
    exact0 &= fabsf(r->position[0] - ppos[0]) < 1e-3f && fabsf(r->position[1] - ppos[1]) < 1e-3f;
    /* the corners move by their draw's interpolated transform (an arc for a turning part, not the chord): at alpha 0.5
       a corner lies within half its pair's motion of the chord's midpoint (tan(turn / 4) / 2 of it for a pure turn) */
    sa1_character_render(ctx, id, 0.5f, &r);
    for (i = 0; interp && i < 3u * n; i++) {
        double e = 0.0, ch = 0.0;
        for (k = 0; k < 3; k++) {
            double mid = 0.5 * ((double)prev[3u * i + k] + cur[3u * i + k]), dd = (double)cur[3u * i + k] - prev[3u * i + k];
            e += ((double)r->positions[3u * i + k] - mid) * ((double)r->positions[3u * i + k] - mid);
            ch += dd * dd;
        }
        e = sqrt(e);
        mid_err = e > mid_err ? e : mid_err;
        mismatch_mid += e > 0.5 * sqrt(ch) + 1e-3;
    }
    /* continuity: ten steps of 0.1 never move a corner by more than a tenth of the arc of the pair's largest motion
       (pi / 2 times the chord at most for turns up to half a turn), plus rounding */
    step = (float *)malloc(9u * sizeof(float) * (n + 1u));
    if (step != NULL) {
        memcpy(step, prev, 9u * sizeof(float) * n);
        for (a = 0.1f; a < 1.05f; a += 0.1f) {
            sa1_character_render(ctx, id, a > 1.0f ? 1.0f : a, &r);
            for (i = 0; i < 9u * n && r->num_triangles == n; i++) {
                double d = fabs((double)r->positions[i] - step[i]);
                maxstep = d > maxstep ? d : maxstep;
                step[i] = r->positions[i];
            }
        }
    }
    /* sa1_character_mesh_lerp: the same triangles (non-effect draws, legacy stream set) */
    memset(&m, 0, sizeof m);
    m.struct_size = sizeof m;
    m.max_triangles = n + 16u;
    m.position = (float *)malloc(9u * sizeof(float) * (n + 16u));
    m.archive = (int16_t *)malloc(2u * (n + 16u));
    m.draw_flags = (uint32_t *)malloc(4u * (n + 16u));
    m.order = (uint32_t *)malloc(4u * (n + 16u));
    m.blend = (uint8_t *)malloc(2u * (n + 16u));
    if (m.position && m.archive && m.draw_flags && m.order && m.blend &&
        sa1_character_mesh_lerp(ctx, id, 0.5f, &m) == SA1_OK && sa1_character_render(ctx, id, 0.5f, &r) == SA1_OK) {
        ok_mesh = m.num_triangles == r->num_triangles;
        for (i = 0; ok_mesh && i < 9u * m.num_triangles; i++) {
            ok_mesh = m.position[i] == r->positions[i];
        }
        for (i = 0, k = 0; ok_mesh && i < r->num_draws; i++) {
            ok_mesh = r->draws[i].prim != SA1_PRIM_TRIANGLES || (m.order[r->draws[i].first] == r->draws[i].order &&
                                                                   m.archive[r->draws[i].first] == r->draws[i].archive);
        }
    } else {
        ok_mesh = 0;
    }
    /* the library's accumulator: half a pair of dt runs no pair, alpha -1 = 0.5 */
    in.stick_y = 0.0f;
    sa1_character_tick(ctx, id, &in, 1.0f / 60.0f, NULL, NULL);
    if (sa1_character_render(ctx, id, -1.0f, &r) == SA1_OK) {
        acc_alpha = r->alpha;
    }
    CHECK(n > 0 && maxd > 0.0 && exact1 && interp && exact0 && mismatch_mid == 0 &&
          maxstep <= maxd * 3.14159265 / 20.0 + 1e-3 && ok_mesh && fabs(acc_alpha - 0.5) < 0.02,
          "interpolation (Sonic running, %u triangles, corners moved up to %.2f host units in the last pair): alpha 1 = the "
          "last pair as drawn, alpha 0 = the pair before exactly (position too), alpha 0.5 within half a corner's motion "
          "of its midpoint (largest offset %.3f: parts turn on arcs), steps of 0.1 move a corner by at most %.3f; "
          "sa1_character_mesh_lerp = sa1_character_render; after 1/60 s more alpha -1 is %.3f", (unsigned)n, maxd,
          mid_err, maxstep, acc_alpha);
    (void)cpos;
    free(prev);
    free(cur);
    free(step);
    free(m.position);
    free(m.archive);
    free(m.draw_flags);
    free(m.order);
    free(m.blend);
}

/* bounding radius (from the centroid) of the corners of the character's own triangles (archive 0, not effects or
 * afterimages); *rms gets their root mean square distance from it (a ball flattened by a vertex lerp keeps its poles,
 * so its bounding radius, but not its mean size), *ntri their count */
static double own_radius(const float *pos, const sa1_render *r, double *rms, uint32_t *ntri)
{
    double c[3] = { 0.0, 0.0, 0.0 }, mx = 0.0, ss = 0.0;
    uint32_t d, t, k, n = 0, pass;
    for (pass = 0; pass < 2; pass++) {
        for (d = 0; d < r->num_draws; d++) {
            const sa1_draw *dr = &r->draws[d];
            if (dr->prim != SA1_PRIM_TRIANGLES || dr->archive != 0 ||
                (dr->flags & (SA1_DRAW_EFFECT | SA1_DRAW_AFTERIMAGE))) {
                continue;
            }
            for (t = dr->first; t < dr->first + dr->count; t++) {
                for (k = 0; k < 3; k++) {
                    const float *p = pos + 9u * t + 3u * k;
                    if (pass == 0) {
                        c[0] += p[0];
                        c[1] += p[1];
                        c[2] += p[2];
                        n++;
                    } else {
                        double dx = p[0] - c[0], dy = p[1] - c[1], dz = p[2] - c[2], dd = sqrt(dx * dx + dy * dy + dz * dz);
                        mx = dd > mx ? dd : mx;
                        ss += dd * dd;
                    }
                }
            }
        }
        if (pass == 0) {
            if (n == 0) {
                *ntri = 0;
                *rms = 0.0;
                return 0.0;
            }
            c[0] /= n;
            c[1] /= n;
            c[2] /= n;
        }
    }
    *ntri = n / 3u;
    *rms = sqrt(ss / n);
    return mx;
}

/* The spin ball (owner report on G-SA1 0.3: "the ball animations are weird, they flatten and widen randomly"): Sonic's
 * ball turns 70-150 degrees per pair in a jump and up to 216 while a spin dash revs up, and a vertex lerp of a turning
 * part shrinks it towards the chord (to cos(turn / 2) of its size: half at 120 degrees). Interpolated as the draw's
 * transform, the ball keeps its size: at alpha 0.25 / 0.5 / 0.75 of every pair of a jump and a spin dash its bounding
 * radius stays within 2% of the two pairs' (the spin dash ball is squashed by the display, in its parent's frame); the
 * turns beyond half a turn take the motion's way round (the hint). */
static void test_ball_lerp(sa1_context *ctx, sa1_character_id id)
{
    static const char *const script = "1j11j26n14a6n14n";
    const sa1_render *r = NULL;
    const char *p = script;
    sa1_input in;
    float *p0 = NULL, *p1 = NULL, *vl = NULL;
    uint32_t pairs = 0, jump_pairs = 0, dash_pairs = 0, ball_tris = 0, bad = 0, not_rigid = 0, over_half = 0, hinted = 0;
    int32_t prev_action = -1;
    uint32_t prev_tris = 0;
    double worst_lo = 1.0, worst_hi = 1.0, vl_min = 1.0, s0, s1, sa, rms_lo = 1.0, rms_hi = 1.0;
    uint32_t rms_pairs = 0;
    memset(&in, 0, sizeof in);
    in.struct_size = sizeof in;
    while (*p) {
        char *e;
        long n = strtol(p, &e, 10), i;
        char cmd = *e;
        p = e + 1;
        in.buttons = cmd == 'a' ? SA1_BUTTON_ACTION : cmd == 'j' ? SA1_BUTTON_JUMP : 0u;
        for (i = 0; i < n; i++) {
            sa1_state_ex x;
            sa1_lerp_stats ls;
            double r0, r1, ra;
            uint32_t n0, n1, na, q, c;
            if (sa1_character_tick(ctx, id, &in, 1.0f / 30.0f, NULL, NULL) != SA1_OK) {
                CHECK(0, "spin ball: tick: %s", sa1_last_error(ctx));
                free(p0);
                free(p1);
                free(vl);
                return;
            }
            memset(&x, 0, sizeof x);
            x.struct_size = sizeof x;
            sa1_character_state_ex(ctx, id, &x);
            sa1_character_render(ctx, id, 1.0f, &r);
            r1 = own_radius(r->positions, r, &s1, &n1);
            free(p1);
            p1 = copy_positions(r);
            sa1_character_render(ctx, id, 0.0f, &r);
            r0 = own_radius(r->positions, r, &s0, &n0);
            free(p0);
            p0 = copy_positions(r);
            if (ball_tris == 0 && (x.action == 14 || x.action == 15) && n1 > 0) {
                ball_tris = n1;     /* the first pair of the jump draws the ball alone */
            }
            if (!(x.action == 14 || x.action == 15) || !(prev_action == 14 || prev_action == 15) || n0 != ball_tris ||
                n1 != ball_tris || prev_tris != ball_tris || !r->interpolated || p0 == NULL || p1 == NULL) {
                prev_action = x.action;
                prev_tris = n1;
                continue;
            }
            prev_action = x.action;
            prev_tris = n1;
            pairs++;
            if (cmd == 'a' || cmd == 'n') {
                dash_pairs += x.mode == 4 || x.mode == 5;
            }
            jump_pairs += x.mode == 8;
            free(vl);
            vl = (float *)malloc(9u * sizeof(float) * (r->num_triangles + 1u));
            for (q = 1; q <= 3; q++) {
                float al = 0.25f * (float)q;
                sa1_character_render(ctx, id, al, &r);
                ra = own_radius(r->positions, r, &sa, &na);
                if (na != ball_tris || r->interpolated == 0) {
                    bad++;
                    continue;
                }
                worst_lo = fmin(worst_lo, ra / fmin(r0, r1));
                worst_hi = fmax(worst_hi, ra / fmax(r0, r1));
                bad += ra < 0.98 * fmin(r0, r1) || ra > 1.02 * fmax(r0, r1);
                /* the mean size of the round ball (every vertex as far from the centre: mean size = bounding radius;
                   the display's squash while the spin dash revs up makes it depend on how the ball's vertices lie in the
                   squash) */
                if (fabs(s0 - r0) <= 0.005 * r0 && fabs(s1 - r1) <= 0.005 * r1) {
                    rms_pairs += q == 1;
                    rms_lo = fmin(rms_lo, sa / fmin(s0, s1));
                    rms_hi = fmax(rms_hi, sa / fmax(s0, s1));
                    bad += sa < 0.98 * fmin(s0, s1) || sa > 1.02 * fmax(s0, s1);
                }
                if (vl != NULL && fabs(s0 - r0) <= 0.005 * r0 && fabs(s1 - r1) <= 0.005 * r1) {
                    /* what the vertex lerp of 0.3 drew: its mean size */
                    double vs;
                    for (c = 0; c < 9u * r->num_triangles; c++) {
                        vl[c] = p0[c] + (p1[c] - p0[c]) * al;
                    }
                    own_radius(vl, r, &vs, &na);
                    vl_min = fmin(vl_min, vs / fmin(s0, s1));
                }
            }
            sa1_character_lerp_stats(ctx, id, &ls);
            not_rigid += ls.vertex_lerp != 0 || ls.local != 0 || ls.rigid == 0;   /* effect models come and go */
            hinted += ls.hinted;
            over_half += ls.over_half_turn;
        }
    }
    CHECK(pairs >= 20 && jump_pairs >= 8 && dash_pairs >= 5 && rms_pairs >= 8 && bad == 0 && not_rigid == 0 &&
              hinted > 0 && over_half > 0 && vl_min < 0.9,
          "spin ball: %u pairs of the ball alone (%u in the air, %u revving / rolling): at alpha 0.25 / 0.5 / 0.75 its "
          "bounding radius stays within 2%% of the pairs' (%.4f .. %.4f of them), and in the %u pairs of the round ball "
          "its mean size too (%.4f .. %.4f; the vertex lerp of 0.3 flattened it to %.2f of it); every draw interpolated "
          "as a rigid transform; %u turns from the motion's hint, %u of them beyond half a turn (the spin dash revs up to "
          "about 216 degrees per pair)", (unsigned)pairs, (unsigned)jump_pairs, (unsigned)dash_pairs, worst_lo, worst_hi,
          (unsigned)rms_pairs, rms_lo, rms_hi, vl_min, (unsigned)hinted, (unsigned)over_half);
    free(p0);
    free(p1);
    free(vl);
}

typedef struct seam_pair {
    uint32_t a, b;
} seam_pair;

static const float *g_sort_pos;

static int cmp_corner_x(const void *a, const void *b)
{
    float xa = g_sort_pos[3u * *(const uint32_t *)a], xb = g_sort_pos[3u * *(const uint32_t *)b];
    return xa < xb ? -1 : xa > xb ? 1 : 0;
}

/* Welds between pairs: corners that coincide in both pairs (welded copies, duplicated seam vertices) must keep
 * coinciding in between: the vertex lerp of 0.3 kept them together by construction; with the parts' transforms
 * interpolated, the welded copies follow the vertex they copy. Ticks `script` (run_ticks) and checks every pair. */
static void seam_check(sa1_context *ctx, sa1_character_id id, const char *label, const char *script)
{
    const sa1_render *r = NULL;
    const char *p = script;
    sa1_input in;
    double eps = 1e-3 * ctx->coords.scale, worst = 0.0;
    uint32_t pairs = 0, seams = 0, followers = 0, local = 0, rigid = 0, gaps = 0;
    memset(&in, 0, sizeof in);
    in.struct_size = sizeof in;
    while (*p) {
        char *e;
        long n = strtol(p, &e, 10), i;
        char cmd = *e;
        p = e + 1;
        in.stick_y = (cmd == 'f' || cmd == 'b') ? 1.0f : 0.0f;
        in.buttons = (cmd == 'a' || cmd == 'b') ? SA1_BUTTON_ACTION : cmd == 'j' ? SA1_BUTTON_JUMP : 0u;
        for (i = 0; i < n; i++) {
            float *q0, *q1;
            uint32_t nc, *ord, c, d, q, np = 0, cap;
            seam_pair *sp;
            sa1_lerp_stats ls;
            sa1_character_tick(ctx, id, &in, 1.0f / 30.0f, NULL, NULL);
            sa1_character_render(ctx, id, 1.0f, &r);
            q1 = copy_positions(r);
            nc = 3u * r->num_triangles;
            sa1_character_render(ctx, id, 0.0f, &r);
            if (!r->interpolated || 3u * r->num_triangles != nc || q1 == NULL) {
                free(q1);
                continue;
            }
            q0 = copy_positions(r);
            ord = (uint32_t *)malloc(sizeof(uint32_t) * (nc + 1u));
            cap = 8u * nc + 16u;
            sp = (seam_pair *)malloc(sizeof(seam_pair) * cap);
            if (q0 == NULL || ord == NULL || sp == NULL) {
                free(q0);
                free(q1);
                free(ord);
                free(sp);
                continue;
            }
            for (c = 0; c < nc; c++) {
                ord[c] = c;
            }
            g_sort_pos = q0;
            qsort(ord, nc, sizeof(uint32_t), cmp_corner_x);
            for (c = 0; c < nc; c++) {
                for (d = c + 1u; d < nc && q0[3u * ord[d]] - q0[3u * ord[c]] <= eps; d++) {
                    const float *a0 = q0 + 3u * ord[c], *b0 = q0 + 3u * ord[d], *a1 = q1 + 3u * ord[c], *b1 = q1 + 3u * ord[d];
                    double g0 = sqrt((a0[0] - b0[0]) * (a0[0] - b0[0]) + (a0[1] - b0[1]) * (a0[1] - b0[1]) +
                                     (a0[2] - b0[2]) * (a0[2] - b0[2]));
                    double g1 = sqrt((a1[0] - b1[0]) * (a1[0] - b1[0]) + (a1[1] - b1[1]) * (a1[1] - b1[1]) +
                                     (a1[2] - b1[2]) * (a1[2] - b1[2]));
                    if (g0 <= eps && g1 <= eps && np < cap) {
                        sp[np].a = ord[c];
                        sp[np].b = ord[d];
                        np++;
                    }
                }
            }
            pairs++;
            seams += np;
            for (q = 1; q <= 3; q++) {
                sa1_character_render(ctx, id, 0.25f * (float)q, &r);
                for (c = 0; c < np && 3u * r->num_triangles == nc; c++) {
                    const float *a = r->positions + 3u * sp[c].a, *b = r->positions + 3u * sp[c].b;
                    double g = sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) +
                                    (a[2] - b[2]) * (a[2] - b[2]));
                    worst = g > worst ? g : worst;
                    gaps += g > 4.0 * eps;
                }
            }
            sa1_character_lerp_stats(ctx, id, &ls);
            followers += ls.followers;
            local += ls.local;
            rigid += ls.rigid;
            free(q0);
            free(q1);
            free(ord);
            free(sp);
        }
    }
    CHECK(pairs > 10 && seams > 0 && gaps == 0,
          "welds (%s): %u pairs, %u corner pairs coinciding in both pairs of a pair stay together at alpha 0.25 / 0.5 / "
          "0.75 (largest gap %.5f host units); per pair on average %.1f drawn models interpolated as rigid transforms, "
          "%.1f with changed local points (welded, morphed), %.1f welded vertices following the vertex they copy", label,
          (unsigned)pairs, (unsigned)seams, worst, pairs ? (double)rigid / pairs : 0.0, pairs ? (double)local / pairs : 0.0,
          pairs ? (double)followers / pairs : 0.0);
}

/* The blob shadow in the render output (0.6.0 fixes: sadc docs/research/gsa1_060_shadow.md L3-L5 and Review R.4.1): one
 * draw of 2 triangles marked SA1_DRAW_SHADOW, kept an effect (SA1_DRAW_EFFECT: the v0.1 / v0.2 mesh_out leaves it out),
 * from njDrawObject (SOURCE_MODEL), blended SA / ISA with its material's alpha 204, after the body, lying about 0.3
 * units above the ground under the drawn root (Tails and Amy stand up to 0.9 above the floor). Its texture is
 * OBJ_REGULAR's "shadow" once the world sets the stage-start texlist and loads OBJ_REGULAR (L1 / L2), untextured
 * (SA1_DRAW_NO_TEXTURE) before. */
static void shadow_check(const sa1_context *ctx, int32_t kind, const sa1_render *r)
{
    uint32_t d, n = 0, t, k, alpha_ok = 1, body_order = 0, after = 0;
    const sa1_draw *s = NULL;
    float up0[3] = { 0.0f, 1.0f, 0.0f }, up[3];
    double hmin = 1e30, hmax = -1e30, horiz = 0.0, sc = ctx->coords.scale, mean[3] = { 0.0, 0.0, 0.0 }, hc;
    char tex[64] = "untextured";
    sa1_to_host_dir(ctx, up0, up);
    for (d = 0; d < r->num_draws; d++) {
        const sa1_draw *dr = &r->draws[d];
        if (dr->flags & SA1_DRAW_SHADOW) {
            n++;
            s = dr;
        } else if (dr->source == SA1_SOURCE_CHARACTER && dr->archive == 0 && dr->order >= body_order) {
            /* the body (Big's fishing gear, drawn after the blob with its own texlist, is not) */
            body_order = dr->order;
        }
    }
    if (n != 1 || s == NULL || s->prim != SA1_PRIM_TRIANGLES) {
        CHECK(0, "%s's blob shadow: %u SA1_DRAW_SHADOW draws in the render output (1 expected)",
              sa1_character_name(kind), (unsigned)n);
        return;
    }
    after = s->order > body_order;
    for (t = s->first; t < s->first + s->count; t++) {
        for (k = 0; k < 3; k++) {
            const float *p = r->positions + 9u * t + 3u * k;
            double q[3], h, hv;
            int m;
            alpha_ok &= r->colors[12u * t + 4u * k + 3u] == 204u;
            for (m = 0; m < 3; m++) {
                q[m] = (double)p[m] - r->position[m];
            }
            h = (q[0] * up[0] + q[1] * up[1] + q[2] * up[2]) / sc;
            hv = sqrt(fmax(0.0, (q[0] * q[0] + q[1] * q[1] + q[2] * q[2]) / (sc * sc) - h * h));
            hmin = fmin(hmin, h);
            hmax = fmax(hmax, h);
            (void)hv;
            for (m = 0; m < 3; m++) {
                mean[m] += q[m] / (3.0 * s->count);   /* corners 0 1 3 / 0 3 2: their mean is the quad's centre */
            }
        }
    }
    hc = (mean[0] * up[0] + mean[1] * up[1] + mean[2] * up[2]) / sc;
    horiz = sqrt(fmax(0.0, (mean[0] * mean[0] + mean[1] * mean[1] + mean[2] * mean[2]) / (sc * sc) - hc * hc));
    if (s->archive >= 0) {
        sa1_texture_info ti;
        const char *an = NULL;
        uint32_t na = 0;
        memset(&ti, 0, sizeof ti);
        ti.struct_size = sizeof ti;
        sa1_texture_archive(ctx, kind, (uint32_t)s->archive, &an, &na);
        if (sa1_texture_get(ctx, kind, (uint32_t)s->archive, (uint32_t)s->texture, &ti) == SA1_OK) {
            snprintf(tex, sizeof tex, "%s:%s", an ? an : "?", ti.name ? ti.name : "?");
        }
    }
    CHECK(s->count == 2 && s->source == SA1_SOURCE_MODEL &&
              (s->flags & (SA1_DRAW_BLEND | SA1_DRAW_EFFECT | SA1_DRAW_SHADOW)) ==
                  (SA1_DRAW_BLEND | SA1_DRAW_EFFECT | SA1_DRAW_SHADOW) &&
              s->blend_src == SA1_BLEND_SRC_ALPHA && s->blend_dst == SA1_BLEND_INV_SRC_ALPHA && alpha_ok && after &&
              hmax - hmin < 5.0 && hc > -1.5 && hc < 1.0 && horiz < 0.5 &&
              (s->archive < 0 ? (s->flags & SA1_DRAW_NO_TEXTURE) != 0 : strcmp(tex, "OBJ_REGULAR:shadow") == 0),
          "%s's blob shadow (the game's, 0.6.0 fixes): 1 draw of %u triangles, SOURCE_MODEL, flags 0x%04X (BLEND | EFFECT | "
          "SHADOW), blend %u/%u (SA / ISA), corner alpha 204: %s, after the body: %s; its centre %.3f units above the "
          "drawn root and %.3f units from it horizontally (corners %.3f..%.3f: the ground's tilt); texture %s",
          sa1_character_name(kind), (unsigned)s->count, (unsigned)s->flags, (unsigned)s->blend_src,
          (unsigned)s->blend_dst, alpha_ok ? "yes" : "no", after ? "yes" : "no", hc, horiz, hmin, hmax, tex);
}

static void test_render(const env *E)
{
    sa1_context *ctx;
    sa1_character_id id, id2;
    const sa1_render *r = NULL;
    draw_census c;
    sa1_capture_stats cs;
    uint32_t i, draws_char = 0, same = 1;
    int32_t kan;
    printf("7. render output (sa1_character_render; Emerald Coast act 1's own collision, DISC):\n");
    /* Sonic standing: materials and eyes */
    ctx = disc_ctx(E, SA1_CHAR_SONIC);
    id = ctx ? disc_char(ctx, SA1_CHAR_SONIC) : 0;
    if (!id || run_ticks(ctx, id, "20n") != 0 || sa1_character_render(ctx, id, 1.0f, &r) != SA1_OK) {
        CHECK(0, "Sonic: context / character / render");
        sa1_shutdown(ctx);
        return;
    }
    census(r, &c);
    print_census("Sonic standing", &c);
    for (i = 0; i < r->num_draws; i++) {
        draws_char += r->draws[i].source == SA1_SOURCE_CHARACTER;
    }
    kan = tex_named(ctx, SA1_CHAR_SONIC, 0, "stx_kanagu");
    {
        int kan_blend = 1, found = 0;
        for (i = 0; i < r->num_draws; i++) {
            if (r->draws[i].archive == 0 && r->draws[i].texture == kan) {
                found = 1;
                kan_blend &= (r->draws[i].flags & SA1_DRAW_BLEND) != 0;
            }
        }
        CHECK(c.tris > 900 && draws_char == c.draws && c.palette == c.draws && c.white_bad == 0 && c.use_alpha > 0 &&
              c.ignspec_tris == c.tris && c.effect == 0 && found && kan_blend,
              "Sonic's materials: every draw from the character renderer is palette-lit with white vertex RGB, carries "
              "IGNORE_SPECULAR (SonicDisplay's constant attribute), %u USE_ALPHA draws are opaque, the shoe buckle "
              "(stx_kanagu, ARGB4444 graded alpha) is blended (the blob shadow aside: %u draw)", (unsigned)c.use_alpha,
              (unsigned)c.shadow);
    }
    shadow_check(ctx, SA1_CHAR_SONIC, r);
    eye_check(ctx, r);
    /* a puppet: its pose as one draw per texture / material run (SA1_SOURCE_POSE), the same colours and classes */
    {
        sa1_character_desc pd;
        sa1_character_id pid = 0;
        sa1_pose ps;
        sa1_mesh pm;
        uint32_t pose_ok = 0, posed = 0, pd_ok = 1;
        memset(&pd, 0, sizeof pd);
        pd.struct_size = sizeof pd;
        pd.kind = SA1_CHAR_SONIC;
        pd.flags = SA1_CREATE_PUPPET;
        memset(&ps, 0, sizeof ps);
        ps.struct_size = sizeof ps;
        ps.action = 12;
        ps.frame = 8.0f;
        memset(&pm, 0, sizeof pm);
        pm.struct_size = sizeof pm;
        pm.max_triangles = 4096;
        pm.position = (float *)malloc(9u * sizeof(float) * 4096u);
        if (sa1_character_create_ex(ctx, &pd, &pid) == SA1_OK && sa1_character_pose(ctx, pid, &ps, &pm) == SA1_OK &&
            sa1_character_render(ctx, pid, -1.0f, &r) == SA1_OK) {
            draw_census pc;
            census(r, &pc);
            posed = pm.num_triangles;
            pose_ok = r->num_triangles == posed && pc.white_bad == 0 && pc.use_alpha > 0 && r->interpolated == 0;
            for (i = 0; i < r->num_draws; i++) {
                pd_ok &= r->draws[i].source == SA1_SOURCE_POSE && (r->draws[i].flags & SA1_DRAW_PALETTE_LIT) != 0;
            }
            for (i = 0; pose_ok && i < 9u * posed; i++) {
                pose_ok = r->positions[i] == pm.position[i];
            }
        }
        CHECK(pose_ok && pd_ok, "a puppet (sa1_character_pose, action 12 frame 8): sa1_character_render gives its %u "
              "triangles as SA1_SOURCE_POSE draws, palette-lit with white vertex RGB, USE_ALPHA drawn opaque, not "
              "interpolated", (unsigned)posed);
        free(pm.position);
        sa1_character_destroy(ctx, pid);
        sa1_character_render(ctx, id, 1.0f, &r);
    }
    /* spin dash: the ball's trail and the dash effect models with SON_EFF (archive 1), additive */
    run_ticks(ctx, id, "14a");
    sa1_character_render(ctx, id, 1.0f, &r);
    census(r, &c);
    print_census("Sonic charging a spin dash", &c);
    {
        draw_census c2;
        run_ticks(ctx, id, "6n");
        sa1_character_render(ctx, id, 1.0f, &r);
        census(r, &c2);
        print_census("Sonic released", &c2);
        CHECK(c.by_archive[1] > 0 && c.additive > 0 && c2.by_archive[1] > 0,
              "spin dash effects are draws with SON_EFF textures (archive 1, additive): %u while charging, %u after the "
              "release", (unsigned)c.by_archive[1], (unsigned)c2.by_archive[1]);
    }
    sa1_character_destroy(ctx, id);
    /* interpolation, then determinism: rendering never touches the world */
    id = disc_char(ctx, SA1_CHAR_SONIC);
    if (id && run_ticks(ctx, id, "20n") == 0) {
        test_lerp(ctx, id);
    }
    sa1_character_destroy(ctx, id);
    id = disc_char(ctx, SA1_CHAR_SONIC);
    if (id && run_ticks(ctx, id, "20n") == 0) {
        test_ball_lerp(ctx, id);
    }
    sa1_character_destroy(ctx, id);
    id = disc_char(ctx, SA1_CHAR_SONIC);
    if (id) {
        seam_check(ctx, id, "Sonic standing up, running, jumping", "20n40f1j10j10n");
    }
    sa1_character_destroy(ctx, id);
    id = disc_char(ctx, SA1_CHAR_SONIC);
    id2 = disc_char(ctx, SA1_CHAR_SONIC);
    if (id && id2) {
        static const char *const seq = "nnnnnnnnnnffffffffffjnnnffffaaaaaannnnbbbbnn";
        sa1_input in;
        sa1_mesh m;
        float *buf = (float *)malloc(9u * sizeof(float) * 8192u);
        memset(&in, 0, sizeof in);
        in.struct_size = sizeof in;
        memset(&m, 0, sizeof m);
        m.struct_size = sizeof m;
        m.max_triangles = 8192u;
        m.position = buf;
        for (i = 0; seq[i]; i++) {
            sa1_state_ex xa, xb;
            in.stick_y = (seq[i] == 'f' || seq[i] == 'b') ? 1.0f : 0.0f;
            in.buttons = seq[i] == 'j' ? SA1_BUTTON_JUMP : (seq[i] == 'a' || seq[i] == 'b') ? SA1_BUTTON_ACTION : 0u;
            sa1_character_tick(ctx, id, &in, 1.0f / 30.0f, NULL, &m);
            sa1_character_render(ctx, id, -1.0f, &r);
            sa1_character_render(ctx, id, 0.3f, &r);
            sa1_character_mesh_lerp(ctx, id, 0.7f, &m);
            sa1_character_tick(ctx, id2, &in, 1.0f / 30.0f, NULL, NULL);
            memset(&xa, 0, sizeof xa);
            xa.struct_size = sizeof xa;
            xb = xa;
            sa1_character_state_ex(ctx, id, &xa);
            sa1_character_state_ex(ctx, id2, &xb);
            same &= memcmp(&xa, &xb, sizeof xa) == 0;
        }
        free(buf);
        CHECK(same, "determinism: of two Sonics given the same input for %u ticks, the one whose mesh is emitted, rendered "
              "and interpolated every tick keeps exactly the other's state (sa1_state_ex)", (unsigned)i);
    }
    sa1_shutdown(ctx);
    /* Tails: the tails' fading copies blend in order; the copies of the unmoved body are left out */
    ctx = disc_ctx(E, SA1_CHAR_TAILS);
    id = ctx ? disc_char(ctx, SA1_CHAR_TAILS) : 0;
    if (id && run_ticks(ctx, id, "20n") == 0 && sa1_character_render(ctx, id, 1.0f, &r) == SA1_OK) {
        uint32_t fading = 0, d, t;
        census(r, &c);
        print_census("Tails standing", &c);
        shadow_check(ctx, SA1_CHAR_TAILS, r);
        sa1_capture_get_stats(sa1_character_capture(ctx, id), &cs);
        for (d = 0; d < r->num_draws; d++) {
            const sa1_draw *dr = &r->draws[d];
            if (!(dr->flags & SA1_DRAW_AFTERIMAGE) || dr->prim != SA1_PRIM_TRIANGLES) {
                continue;
            }
            for (t = dr->first; t < dr->first + dr->count; t++) {
                fading += r->colors[12u * t + 3u] < 255u && (dr->flags & SA1_DRAW_BLEND) != 0;
            }
        }
        CHECK(c.afterimage > 0 && fading > 0 && cs.duplicates > 0 && c.tris < 2000u,
              "Tails: %u afterimage draws (%u blended triangles with alpha below 255, after the opaque draws in the game's "
              "order); %u repeated draws of the unmoved body under the fading material left out (they z-fight in a "
              "host; on the Dreamcast they blend over themselves): %u triangles instead of about 3000",
              (unsigned)c.afterimage, (unsigned)fading, (unsigned)cs.duplicates, (unsigned)c.tris);
        tails_drawn_tails(ctx, id);
        seam_check(ctx, id, "Tails running and jumping: the tails morph", "30f1j8j10n");
    } else {
        CHECK(0, "Tails");
    }
    sa1_shutdown(ctx);
    /* Knuckles: the punch sprite (KNU_EFF) */
    ctx = disc_ctx(E, SA1_CHAR_KNUCKLES);
    id = ctx ? disc_char(ctx, SA1_CHAR_KNUCKLES) : 0;
    if (id && run_ticks(ctx, id, "20n1a5n") == 0 && sa1_character_render(ctx, id, 1.0f, &r) == SA1_OK) {
        census(r, &c);
        print_census("Knuckles punching", &c);
        shadow_check(ctx, SA1_CHAR_KNUCKLES, r);
        CHECK(c.sprites > 0 && c.by_archive[1] > 0, "Knuckles' punch: %u sprites with KNU_EFF textures (archive 1)",
              (unsigned)c.sprites);
    } else {
        CHECK(0, "Knuckles");
    }
    sa1_shutdown(ctx);
    /* E-102: the targeting lines (njDrawLine3D) while locking on */
    ctx = disc_ctx(E, SA1_CHAR_GAMMA);
    id = ctx ? disc_char(ctx, SA1_CHAR_GAMMA) : 0;
    if (id && run_ticks(ctx, id, "20n20a") == 0 && sa1_character_render(ctx, id, 1.0f, &r) == SA1_OK) {
        census(r, &c);
        print_census("E-102 locking on", &c);
        shadow_check(ctx, SA1_CHAR_GAMMA, r);
        CHECK(c.lines > 0, "E-102's targeting: %u line segments (njDrawLine3D, the points as the game drew them)",
              (unsigned)c.lines);
    } else {
        CHECK(0, "E-102");
    }
    sa1_shutdown(ctx);
    /* Big and Amy: reported (Big's fishing displays: part 8) */
    ctx = disc_ctx(E, SA1_CHAR_BIG);
    id = ctx ? disc_char(ctx, SA1_CHAR_BIG) : 0;
    if (id && run_ticks(ctx, id, "20n10a20n") == 0 && sa1_character_render(ctx, id, 1.0f, &r) == SA1_OK) {
        census(r, &c);
        print_census("Big after the action button (information; his fishing displays: part 8)", &c);
        shadow_check(ctx, SA1_CHAR_BIG, r);
    }
    sa1_shutdown(ctx);
    ctx = disc_ctx(E, SA1_CHAR_AMY);
    id = ctx ? disc_char(ctx, SA1_CHAR_AMY) : 0;
    if (id && run_ticks(ctx, id, "20n1a4n") == 0 && sa1_character_render(ctx, id, 1.0f, &r) == SA1_OK) {
        census(r, &c);
        print_census("Amy swinging (information)", &c);
        shadow_check(ctx, SA1_CHAR_AMY, r);
    }
    sa1_shutdown(ctx);
}
/* ---- 8. effects: the particle manager in the step, particles as sprites, Big's fishing displays --------------- */
#define FX_POOL         0x8C7493D4u     /* B_8C7493D4: the particle nodes (0x4C bytes each; +4 exec, NULL = free) */
#define FX_NODES        512u
#define FX_NODE_SIZE    0x4Cu
#define FX_FREE_N       0x8C7A0D1Cu     /* B_8C7A0D1C: free nodes */
#define FX_DRAWN        0x8C7A0D28u     /* B_8C7A0D28: particles the manager's draw 8C09E0B8 drew on the last sub step 0 */
#define BIG_LURE_TASK   0x8C7A40C8u     /* B_8C7A40C8: Big's lure task (twp +0x20, twp->mode at +0) */
#define BIG_LURE_OBJS   0x8C565FC4u     /* D_8C565FC4[5]: the lure objects by number of fishing upgrades */
#define BIG_CAST_CIRCLE 0x8C555B08u     /* Big_Cast_Circle (NJS_OBJECT): the target circle of the cast cursor 8C0E66C4 */
#define BIG_CAST_CURSOR 0x8C555C3Cu     /* Big_Cast_Cursor (NJS_OBJECT) */

static uint32_t fx_rd(const sa1g_world *w, uint32_t a)
{
    uint32_t v = 0;
    sa1g_world_rd32(w, a, &v);
    return v;
}

/* a context with one character on its stage's own collision (DISC) */
static sa1_context *stage_ctx(const env *E, int32_t kind, int32_t level, int32_t act)
{
    sa1_context *ctx = NULL;
    sa1_host h;
    sa1_load_options lo;
    sa1_collision_static col;
    sa1_host_defaults(&h);
    h.log = log_cb;
    h.log_level = SA1_LOG_WARN;
    if (sa1_init(&h, &ctx) != SA1_OK) {
        return NULL;
    }
    memset(&lo, 0, sizeof lo);
    lo.struct_size = sizeof lo;
    lo.characters = 1u << kind;
    lo.flags = SA1_LOAD_DEFAULT;
    memset(&col, 0, sizeof col);
    col.struct_size = sizeof col;
    col.mode = SA1_COLLISION_MODE_DISC;
    col.disc_level = level;
    col.disc_act = act;
    if (SA1_FAILED(sa1_load_disc(ctx, E->gdi, &lo)) || sa1_collision_set_static_ex(ctx, &col) != SA1_OK) {
        printf("    context: %s\n", sa1_last_error(ctx));
        sa1_shutdown(ctx);
        return NULL;
    }
    return ctx;
}

/* the character at the stage's start with the stage's own tasks (Emerald Coast's ocean: the water plane) */
static sa1_character_id stage_char(sa1_context *ctx, int32_t kind, int32_t level, int32_t act)
{
    sa1_character_desc d;
    sa1_character_id id = 0;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.kind = kind;
    d.flags = SA1_CREATE_WORLD | SA1_CREATE_START_TABLE | SA1_CREATE_STAGE | SA1_CREATE_STAGE_TASKS;
    d.level = level;
    d.act = act;
    if (SA1_FAILED(sa1_character_create_ex(ctx, &d, &id))) {
        printf("    create %s: %s\n", sa1_character_name(kind), sa1_last_error(ctx));
        return 0;
    }
    return id;
}

static int32_t archive_named(const sa1_context *ctx, int32_t kind, const char *name)
{
    uint32_t a, n;
    for (a = 0; a < SA1_MAX_ARCHIVES; a++) {
        const char *nm = NULL;
        if (sa1_texture_archive(ctx, kind, a, &nm, &n) != SA1_OK) {
            break;
        }
        if (nm != NULL && strcmp(nm, name) == 0) {
            return (int32_t)a;
        }
    }
    return -1;
}

/* the physical addresses of the models of an object tree (guest), at most cap */
static uint32_t tree_models(const sa1g_world *w, uint32_t obj, uint32_t *out, uint32_t n, uint32_t cap, int depth)
{
    uint32_t k;
    for (k = 0; obj != 0 && k < 256u && n < cap && depth < 32; k++) {
        uint32_t model = fx_rd(w, obj + 4u);
        if (model != 0) {
            out[n++] = model & 0x1FFFFFFFu;
        }
        n = tree_models(w, fx_rd(w, obj + 0x2Cu), out, n, cap, depth + 1);
        obj = fx_rd(w, obj + 0x30u);
    }
    return n;
}

static float fx_rdf(const sa1g_world *w, uint32_t a)
{
    uint32_t v = fx_rd(w, a);
    float f;
    memcpy(&f, &v, 4);
    return f;
}

/* SA1 space of a host point (sa1_coords: host = axis * sa1 * scale, axis orthonormal) */
static void host_to_sa1(const sa1_coords *c, const float h[3], double out[3])
{
    int i;
    for (i = 0; i < 3; i++) {
        out[i] = ((double)c->axis[i] * h[0] + (double)c->axis[3 + i] * h[1] + (double)c->axis[6 + i] * h[2]) / c->scale;
    }
}

static int in_set(const uint32_t *set, uint32_t n, uint32_t a)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        if (set[i] == (a & 0x1FFFFFFFu)) {
            return 1;
        }
    }
    return 0;
}

/* 8a. Sonic on Emerald Coast act 1 (its own collision and stage tasks: the ocean) runs 100,000 steps of a spin dash /
 * jump loop (back and forth: he stays at the shore and keeps entering the water); every entry bursts 20 splash particles
 * (the shadow code 8C0925A0 -> 8C07B3DA -> 8C046A08, particle exec 8C046960). The particle manager 8C09DF64 runs in
 * every step (src/world/sa1_world_tick.c): the pool of 512 nodes never runs out, the nodes are reused, and the
 * particles keep being drawn by the game's sprite draw 8C09E778 into the capture: sprites with EFF_REGULAR textures
 * (the particle texlists are slices of its texname array) at the particles' positions. */
static void test_particles(const env *E)
{
    static const char *const loop = "14b6n2j20n14k6n2j20n";
    const uint32_t pairs = 50000u;
    sa1_context *ctx;
    sa1_character_id id;
    sa1g_world *w;
    sa1_input in;
    const sa1_render *r = NULL;
    uint8_t live[FX_NODES];
    uint64_t spawns = 0, drawn_sum = 0;
    uint32_t pair, i, minfree = FX_NODES, drawn_pairs = 0, late_drawn = 0, late_spawns = 0, renders = 0, sprites = 0;
    uint32_t spr_eff = 0, spr_other = 0, faults = 0, matched = 0, drawn_rendered = 0, bad_renders = 0;
    int32_t effreg;
    sa1_coords co;
    const char *p = loop;
    long left = 0;
    char cur = 'n';
    ctx = stage_ctx(E, SA1_CHAR_SONIC, 1, 0);
    id = ctx ? stage_char(ctx, SA1_CHAR_SONIC, 1, 0) : 0;
    w = id ? sa1_character_world(ctx, id) : NULL;
    effreg = ctx ? archive_named(ctx, SA1_CHAR_SONIC, "EFF_REGULAR") : -1;
    if (w == NULL || effreg < 0) {
        CHECK(0, "particles: Sonic on Emerald Coast act 1 with its stage tasks (and EFF_REGULAR among his archives: %d)",
              (int)effreg);
        sa1_shutdown(ctx);
        return;
    }
    memset(live, 0, sizeof live);
    memset(&in, 0, sizeof in);
    in.struct_size = sizeof in;
    memset(&co, 0, sizeof co);
    co.struct_size = sizeof co;
    sa1_get_coords(ctx, &co);
    for (pair = 0; pair < pairs; pair++) {
        uint32_t freec, drawn;
        if (left == 0) {
            char *e;
            if (!*p) {
                p = loop;
            }
            left = strtol(p, &e, 10);
            cur = *e;
            p = e + 1;
        }
        left--;
        in.stick_y = cur == 'b' ? 1.0f : cur == 'k' ? -1.0f : 0.0f;
        in.buttons = (cur == 'b' || cur == 'k') ? SA1_BUTTON_ACTION : cur == 'j' ? SA1_BUTTON_JUMP : 0u;
        if (sa1_character_tick(ctx, id, &in, 1.0f / 30.0f, NULL, NULL) != SA1_OK) {
            printf("    pair %u: %s\n", (unsigned)pair, sa1_last_error(ctx));
            faults++;
            break;
        }
        for (i = 0; i < FX_NODES; i++) {
            uint8_t on = fx_rd(w, FX_POOL + FX_NODE_SIZE * i + 4u) != 0;
            if (on && !live[i]) {
                spawns++;
                late_spawns += pair >= pairs - pairs / 10u;
            }
            live[i] = on;
        }
        freec = fx_rd(w, FX_FREE_N);
        if (freec < minfree) {
            minfree = freec;
        }
        drawn = fx_rd(w, FX_DRAWN);
        if (drawn != 0) {
            drawn_pairs++;
            drawn_sum += drawn;
            late_drawn += pair >= pairs - pairs / 10u;
        }
        /* every 16th pair that drew particles: the render output holds them as sprites, one at the position of each
         * particle the manager drew on sub step 0 (the node's pos, less the spd sub step 1 then added; a particle that
         * ended in sub step 1 did not move); other sprites come from effect tasks that call 8C09E778 themselves */
        if (drawn != 0 && (drawn_pairs & 15u) == 1u && sa1_character_render(ctx, id, 1.0f, &r) == SA1_OK) {
            uint32_t d, s, here = 0;
            renders++;
            sprites += r->num_sprites;
            for (d = 0; d < r->num_draws; d++) {
                const sa1_draw *dr = &r->draws[d];
                if (dr->prim != SA1_PRIM_SPRITES) {
                    continue;
                }
                if (dr->archive == effreg && dr->texture >= 0) {
                    spr_eff += dr->count;
                } else {
                    spr_other += dr->count;
                }
                for (s = dr->first; s < dr->first + dr->count && s < r->num_sprites; s++) {
                    double sp[3], best = 1e30;
                    uint32_t nd;
                    host_to_sa1(&co, r->sprites[s].position, sp);
                    for (nd = 0; nd < FX_NODES; nd++) {
                        uint32_t a = FX_POOL + FX_NODE_SIZE * nd, c;
                        double d0 = 0.0, d1 = 0.0;
                        for (c = 0; c < 3u; c++) {
                            double ps = fx_rdf(w, a + 0x18u + 4u * c), v = fx_rdf(w, a + 0x24u + 4u * c);
                            d0 += (sp[c] - ps) * (sp[c] - ps);
                            d1 += (sp[c] - (ps - v)) * (sp[c] - (ps - v));
                        }
                        if (d0 < best) {
                            best = d0;
                        }
                        if (d1 < best) {
                            best = d1;
                        }
                    }
                    here += best < 0.01 * 0.01;
                }
            }
            matched += here;
            drawn_rendered += drawn;
            bad_renders += here != drawn;
        }
    }
    printf("      %u pairs (%u steps) of \"%s\": %llu particles spawned (%u in the last tenth), at least %u of the %u nodes "
           "free at any time, particles drawn in %u pairs (%llu sprite draws of the manager; %u in the last tenth)\n",
           (unsigned)pair, (unsigned)(2u * pair), loop, (unsigned long long)spawns, (unsigned)late_spawns,
           (unsigned)minfree, (unsigned)FX_NODES, (unsigned)drawn_pairs, (unsigned long long)drawn_sum,
           (unsigned)late_drawn);
    CHECK(faults == 0 && pair == pairs && minfree > 0 && spawns > 4u * FX_NODES && late_spawns > 0 && late_drawn > 0,
          "particle manager 8C09DF64 in the step: %u steps of a spin dash / jump loop at the shore spawn %llu particles "
          "(water splashes) into the 512-node pool, which never runs out (at least %u nodes free): ended particles give "
          "their nodes back and they are reused; particles are still spawned and drawn in the last tenth of the run",
          (unsigned)(2u * pair), (unsigned long long)spawns, (unsigned)minfree);
    CHECK(renders > 0 && spr_eff > 0 && spr_other == 0 && bad_renders == 0 && matched == drawn_rendered,
          "particles in the render output: %u renders of pairs that drew particles hold %u sprites (the game's sprite "
          "draw 8C09E778, recorded on sub step 0), all with EFF_REGULAR textures (archive %d: the particle texlists are "
          "slices of its texname array); in every render one sprite at each drawn particle's position (%u of the %u the "
          "manager drew; %u more sprites from effect tasks that draw with 8C09E778 themselves)", (unsigned)renders,
          (unsigned)sprites, (int)effreg, (unsigned)matched, (unsigned)drawn_rendered, (unsigned)(sprites - matched));
    sa1_shutdown(ctx);
}

/* 8b. Big on Emerald Coast act 3 (its own collision and stage tasks) holds the action button (the cast stance, then the
 * cast cursor 8C0E6CC0 with its display 8C0E66C4: the target circle and the cursor), releases it (the throw: the lure
 * task 8C0E943C goes to modes 2, 3 ...): the capture holds the lure (the lure display 8C0E7720 / 8C0E78C0: one of
 * the lure objects D_8C565FC4 with texlist_FISHING) in every pair, the target group while he aims, and the fishing
 * line (8C0EC180: njDrawLine3D, 12 points) while the lure is out (lure mode 3 and later) */
static void test_big_fishing(const env *E)
{
    static const char *const seq = "20n30a40n";
    sa1_context *ctx;
    sa1_character_id id;
    sa1g_world *w;
    sa1_capture *cap;
    sa1_input in;
    uint32_t lure[64], circle[16], cursor[16], nl = 0, nci, ncu, k;
    uint32_t pair = 0, p_lure = 0, p_target = 0, p_line = 0, p_out = 0, line_pts = 0, line_zero = 0, maxmode = 0;
    uint32_t r_fish = 0, r_lines = 0, r_target = 0;
    int32_t fishing;
    const char *p = seq;
    long left = 0;
    char cur = 'n';
    ctx = stage_ctx(E, SA1_CHAR_BIG, 1, 2);
    id = ctx ? stage_char(ctx, SA1_CHAR_BIG, 1, 2) : 0;
    w = id ? sa1_character_world(ctx, id) : NULL;
    fishing = ctx ? archive_named(ctx, SA1_CHAR_BIG, "FISHING") : -1;
    if (w == NULL || fishing < 0) {
        CHECK(0, "Big on Emerald Coast act 3 with its stage tasks (FISHING archive %d)", (int)fishing);
        sa1_shutdown(ctx);
        return;
    }
    for (k = 0; k < 5u; k++) {
        nl = tree_models(w, fx_rd(w, BIG_LURE_OBJS + 4u * k), lure, nl, 64u, 0);
    }
    nci = tree_models(w, BIG_CAST_CIRCLE, circle, 0, 16u, 0);
    ncu = tree_models(w, BIG_CAST_CURSOR, cursor, 0, 16u, 0);
    memset(&in, 0, sizeof in);
    in.struct_size = sizeof in;
    for (;;) {
        const sa1_cap_rec *recs = NULL;
        const sa1_render *r = NULL;
        uint32_t nrec, i, lt, lmode = 0, has_lure = 0, has_ci = 0, has_cu = 0, has_line = 0;
        if (left == 0) {
            char *e;
            if (!*p) {
                break;
            }
            left = strtol(p, &e, 10);
            cur = *e;
            p = e + 1;
        }
        left--;
        in.buttons = cur == 'a' ? SA1_BUTTON_ACTION : 0u;
        if (sa1_character_tick(ctx, id, &in, 1.0f / 30.0f, NULL, NULL) != SA1_OK) {
            printf("    pair %u: %s\n", (unsigned)pair, sa1_last_error(ctx));
            break;
        }
        cap = sa1_character_capture(ctx, id);
        nrec = cap ? sa1_capture_records(cap, &recs) : 0u;
        for (i = 0; i < nrec; i++) {
            const sa1_cap_rec *rc = &recs[i];
            if (SA1_CAP_IS_MODEL(rc->kind)) {
                has_lure |= in_set(lure, nl, rc->obj);
                has_ci |= in_set(circle, nci, rc->obj);
                has_cu |= in_set(cursor, ncu, rc->obj);
            } else if (rc->kind == SA1_CAP_LINE3D && rc->n == 11u && rc->attr == 0x42u) {
                const uint8_t *pl = sa1_capture_payload(cap, rc, 16u);
                has_line = 1;
                if (pl != NULL && sa1_rd32(pl) == 12u && rc->pay_len >= 16u + 12u * 12u) {
                    uint32_t q;
                    for (q = 0; q < 12u * 3u; q++) {
                        line_pts++;
                        line_zero += sa1_rd32(pl + 16u + 4u * q) == 0u;
                    }
                }
            }
        }
        lt = fx_rd(w, BIG_LURE_TASK);
        if (lt != 0) {
            lmode = fx_rd(w, fx_rd(w, lt + 0x20u)) & 0xFFu;
        }
        if (lmode > maxmode) {
            maxmode = lmode;
        }
        p_lure += has_lure;
        p_target += has_ci && has_cu;
        p_line += has_line;
        p_out += lmode >= 3u;
        if (sa1_character_render(ctx, id, 1.0f, &r) == SA1_OK) {
            uint32_t d, f = 0;
            for (d = 0; d < r->num_draws; d++) {
                f += r->draws[d].prim == SA1_PRIM_TRIANGLES && r->draws[d].archive == fishing;
            }
            r_fish += f != 0;
            r_lines += r->num_line_vertices != 0;
            r_target += (has_ci && has_cu) && f != 0;
        }
        pair++;
    }
    printf("      Big, \"%s\": lure models %u, target circle %u / cursor %u models; pairs: %u drawing the lure, %u the "
           "target group, %u the fishing line (lure out in %u, highest lure mode %u); render output: FISHING draws in %u "
           "pairs, line segments in %u\n", seq, (unsigned)nl, (unsigned)nci, (unsigned)ncu, (unsigned)p_lure,
           (unsigned)p_target, (unsigned)p_line, (unsigned)p_out, (unsigned)maxmode, (unsigned)r_fish,
           (unsigned)r_lines);
    CHECK(nl > 0 && nci > 0 && ncu > 0 && p_lure + 4u >= pair && p_target > 0 && r_target > 0 && p_line > 0 &&
          maxmode >= 3u && r_fish + 4u >= pair && r_lines > 0,
          "Big's fishing displays in the capture: the lure (8C0E7720 / 8C0E78C0, D_8C565FC4 with texlist_FISHING) in %u "
          "of %u pairs, the target group of the cast cursor (8C0E66C4: Big_Cast_Circle + Big_Cast_Cursor) in %u pairs "
          "while he aims, the fishing line (8C0EC180, njDrawLine3D 11 segments) in %u pairs while the lure is out; in the "
          "render output as FISHING (archive %d) draws and line segments", (unsigned)p_lure, (unsigned)pair,
          (unsigned)p_target, (unsigned)p_line, (int)fishing);
    printf("      (information) the line's 12 points: %u of %u coordinates are 0.0: the rope simulation 8C0EBFA8 / "
           "8C0EC0A8 that moves them has no C (void boundary stubs), so the line lies at the origin of the line buffer "
           "until it is decompiled\n", (unsigned)line_zero, (unsigned)line_pts);
    sa1_shutdown(ctx);
}

/* ---- 9. hidden displays ----------------------------------------------------------------------------------------- */
static void test_hidden(const env *E)
{
    static const int32_t chars[] = { SA1_CHAR_SONIC, SA1_CHAR_TAILS, SA1_CHAR_KNUCKLES, SA1_CHAR_BIG, SA1_CHAR_AMY };
    wr_trace t;
    char prefix[1024];
    sa1ca_dump dump;
    sa1_context *ctx;
    sa1_character_info ci;
    sa1_input in;
    live_mesh L;
    float pos[3], yaw;
    uint32_t k, i, maxt = 0;
    printf("9. hidden displays (the game's camera does not see the character: sa1_capture_hidden), FAITHFUL L01A0:\n");
    if (load_trace(E, "ec1_run_jump", &t) != 0 || land_prefix(E, &t.h, prefix, sizeof prefix) != 0) {
        printf("  skip  no ec1_run_jump trace / landtable dump\n");
        g_skip++;
        return;
    }
    ctx = make_ctx(E, (1u << SA1_CHAR_SONIC) | (1u << SA1_CHAR_TAILS) | (1u << SA1_CHAR_KNUCKLES) | (1u << SA1_CHAR_BIG) |
                          (1u << SA1_CHAR_AMY), prefix, &dump);
    if (ctx == NULL) {
        CHECK(0, "context");
        wr_trace_free(&t);
        return;
    }
    for (k = 0; k < SA1_CHAR_COUNT; k++) {
        memset(&ci, 0, sizeof ci);
        ci.struct_size = sizeof ci;
        if (sa1_character_info_get(ctx, (int32_t)k, &ci) == SA1_OK && ci.max_triangles > maxt) {
            maxt = ci.max_triangles;
        }
    }
    if (live_mesh_init(&L, 4u * maxt) != 0) {
        CHECK(0, "mesh buffers");
        free_ctx(ctx, &dump);
        wr_trace_free(&t);
        return;
    }
    pos[0] = t.h.start_pos[0] * 4.0f;
    pos[1] = -t.h.start_pos[2] * 4.0f;
    pos[2] = t.h.start_pos[1] * 4.0f;
    yaw = (float)(-(double)(int16_t)(uint16_t)t.h.start_ang[1] * (6.28318530717958647692 / 65536.0));
    for (k = 0; k < sizeof chars / sizeof chars[0]; k++) {
        sa1_character_id id = 0;
        sa1_trace_header zero;
        live_run R0, R1;
        sa1_capture_stats s;
        const sa1_render *rr = NULL;
        uint32_t shown = 0, asked = 0, hidden0 = 0, hidden1 = 0, rendered = 0, rtri_min = 0xFFFFFFFFu;
        uint32_t blob_pairs = 0, blob_drawn = 0, blob_mismatch = 0;
        memset(&in, 0, sizeof in);
        in.struct_size = sizeof in;
        in.camera_yaw = (float)(-(double)0x6C21 * (3.14159265358979323846 / 32768.0));
        if (sa1_character_create(ctx, chars[k], pos, yaw, &id) != SA1_OK) {
            CHECK(0, "create %s", sa1_character_name(chars[k]));
            continue;
        }
        /* the world's own view: the camera adapter's (src/world/sa1_world_tick.c sa1g_camera_view) */
        memset(&R0, 0, sizeof R0);
        R0.tri_min = 0xFFFFFFFFu;
        for (i = 0; i < 30; i++) {
            in.stick_y = i >= 10 ? 1.0f : 0.0f;
            live_tick(ctx, id, &in, &L, &R0);
            sa1_capture_get_stats(sa1_character_capture(ctx, id), &s);
            if (i >= 1) {
                asked += s.vis_shown + s.vis_hidden != 0;
                shown += s.vis_shown != 0 && s.vis_hidden == 0;
                hidden0 += s.hidden;
            }
        }
        if (chars[k] == SA1_CHAR_AMY) {
            CHECK(R0.bad_status == 0 && asked == 0 && hidden0 == 0 && R0.empty == 0,
                  "%s: the display does not ask the visibility test (%u pairs asked), never hidden, %u-%u triangles",
                  sa1_character_name(chars[k]), (unsigned)asked, (unsigned)R0.tri_min, (unsigned)R0.tri_max);
        } else {
            CHECK(R0.bad_status == 0 && shown == 29 && hidden0 == 0 && R0.empty == 0,
                  "%s: with the world's own view (the camera adapter's) the display is visible on sub step 0 of every "
                  "pair (%u of 29 ticks), never hidden (%u), %u-%u triangles from the capture",
                  sa1_character_name(chars[k]), (unsigned)shown, (unsigned)hidden0, (unsigned)R0.tri_min,
                  (unsigned)R0.tri_max);
        }
        /* the zero screen of the old references: the visibility test never passes */
        memset(&zero, 0, sizeof zero);
        wr_screen_of(sa1_character_world(ctx, id), &zero);
        memset(&R1, 0, sizeof R1);
        R1.tri_min = 0xFFFFFFFFu;
        for (i = 0; i < 30; i++) {
            in.stick_y = (i & 8u) ? 1.0f : 0.0f;
            in.buttons = (i == 20) ? SA1_BUTTON_JUMP : 0u;
            live_tick(ctx, id, &in, &L, &R1);
            sa1_capture_get_stats(sa1_character_capture(ctx, id), &s);
            hidden1 += s.hidden;
            if (sa1_character_render(ctx, id, 1.0f, &rr) == SA1_OK && rr->num_triangles > 0) {
                /* the blob shadow does not ask the visibility test: a pair whose capture holds it renders it, also
                 * when the pair is hidden (0.6.0 fixes, sadc docs/research/gsa1_060_shadow.md L4) */
                sa1_capture *cp = sa1_character_capture(ctx, id);
                const sa1_cap_rec *recs = NULL;
                uint32_t nrec = sa1_capture_records(cp, &recs), q, blob = 0, drawn = 0;
                uint32_t sm = sa1_capture_shadow_model(cp);
                for (q = 0; q < nrec; q++) {
                    blob |= SA1_CAP_IS_MODEL(recs[q].kind) && (recs[q].obj & 0x1FFFFFFFu) == (sm & 0x1FFFFFFFu);
                }
                for (q = 0; q < rr->num_draws; q++) {
                    drawn += (rr->draws[q].flags & SA1_DRAW_SHADOW) != 0;
                }
                blob_pairs += blob;
                blob_drawn += drawn != 0;
                blob_mismatch += (blob != 0) != (drawn == 1) || drawn > 1;
                rendered++;
                rtri_min = rr->num_triangles < rtri_min ? rr->num_triangles : rtri_min;
            }
        }
        CHECK(blob_mismatch == 0 && blob_pairs > 0,
              "%s with the zero screen: the blob shadow is in the render output in exactly the %u of 30 pairs whose capture "
              "holds it (%u rendered; hidden pairs append it to the CPU pose)", sa1_character_name(chars[k]),
              (unsigned)blob_pairs, (unsigned)blob_drawn);
        if (chars[k] == SA1_CHAR_AMY) {
            CHECK(R1.bad_status == 0 && hidden1 == 0 && R1.empty == 0 && rendered == 30,
                  "%s: the zero screen changes nothing (no visibility test): never hidden, mesh every tick",
                  sa1_character_name(chars[k]));
        } else {
            CHECK(R1.bad_status == 0 && hidden1 == 30 && R1.empty == 0 && R1.far_off == 0 && rendered == 30,
                  "%s: with the zero screen every pair is hidden (%u of 30 ticks); mesh_out and sa1_character_render "
                  "still hold the character every tick (%u-%u / at least %u triangles: the CPU pose of the game's "
                  "action and frame), within %.1f host units of it",
                  sa1_character_name(chars[k]), (unsigned)hidden1, (unsigned)R1.tri_min, (unsigned)R1.tri_max,
                  (unsigned)(rtri_min == 0xFFFFFFFFu ? 0u : rtri_min), R1.max_off);
        }
        sa1_character_destroy(ctx, id);
    }
    live_mesh_free(&L);
    free_ctx(ctx, &dump);
    wr_trace_free(&t);
}

static void test_effects(const env *E)
{
    printf("8. effects (the particle manager in the step; particles and Big's fishing displays in the capture):\n");
    test_particles(E);
    test_big_fishing(E);
}

/* ---- lsd. special-move visuals (round 4c: sadc docs/research/lightspeed_visuals.md 4-5 and its Review R.4; plan
 * docs/research/gsa1_rounds_plan.md 6.1) ------------------------------------------------------------------------- */
#define LSD_SPARKLE_TL0  0x8C1C6AA4u    /* D_8C1C6AD0[0].tlist { lcharge02 }: the charge sparkle */
#define LSD_SPARKLE_TL1  0x8C1C6AACu    /* D_8C1C6AD0[1].tlist { lcharge04 }: with the Crystal Ring */
#define LSD_AURA_MAT     0x26182400u    /* the aura's material (S_MOT 0x0CC00000; its 20 node models have nbMat 0) */
#define LSD_GAME_CLOCK   0x8C752BECu    /* B_8C752BEC: the game clock 8C08A5CC's counter, + 1 at the end of every step */
#define LSD_FRAME        0x8C752BE8u    /* B_8C752BE8: the frame counter the capture keys its pairs on */
#define LSD_EVENT_FLAGS  0x8C799920u    /* the event flag bytes (G-SA1's set_event_flags writes one byte per flag) */

static void lsd_event_flag(sa1g_world *w, uint32_t flag)
{
    uint32_t a = LSD_EVENT_FLAGS + flag, word = a & ~3u, v = 0, sh = 8u * (a & 3u);
    if (w != NULL && sa1g_world_rd32(w, word, &v) == 0) {
        sa1g_world_wr32(w, word, (v & ~(0xFFu << sh)) | (1u << sh));
    }
}

/* archive and index of the texture called name among a character's archives (-1: none) */
static int32_t lsd_tex_any(const sa1_context *ctx, int32_t kind, const char *name, int32_t *arc)
{
    uint32_t a;
    for (a = 0; a < SA1_MAX_ARCHIVES; a++) {
        int32_t t = tex_named(ctx, kind, a, name);
        if (t >= 0) {
            *arc = (int32_t)a;
            return t;
        }
    }
    *arc = -1;
    return -1;
}

static double lsd_axis(const double m[12], int c)
{
    return sqrt(m[c] * m[c] + m[4 + c] * m[4 + c] + m[8 + c] * m[8 + c]);
}

/* the emitter's host point of a record's model-space point (sa1_capture.c xform + sa1_to_host_point) */
static void lsd_host_point(const sa1_coords *co, const double m[12], const float in[3], float out[3])
{
    double x = in[0], y = in[1], z = in[2];
    float w[3];
    int i;
    for (i = 0; i < 3; i++) {
        w[i] = (float)(m[4 * i] * x + m[4 * i + 1] * y + m[4 * i + 2] * z + m[4 * i + 3]);
    }
    for (i = 0; i < 3; i++) {
        out[i] = (co->axis[3 * i] * w[0] + co->axis[3 * i + 1] * w[1] + co->axis[3 * i + 2] * w[2]) * co->scale;
    }
}

static int lsd_near(const float *a, const float *b, float tol)
{
    return fabsf(a[0] - b[0]) <= tol && fabsf(a[1] - b[1]) <= tol && fabsf(a[2] - b[2]) <= tol;
}

/* the sprite size the emitter gives a 3D sprite record (SA1 units; F1), 0 extent: nothing emitted */
static int lsd_sprite_size(const sa1_capture *cap, const sa1_cap_rec *rc, double *w, double *h)
{
    const uint8_t *p = sa1_capture_payload(cap, rc, 0x34u);
    double m[12], sx, sy;
    if (p == NULL) {
        return 0;
    }
    sa1_capture_world_matrix(rc, m);
    sx = sa1_rdf32(p + 0x0C);
    sy = sa1_rdf32(p + 0x10);
    if (rc->attr & 0x10u) {
        if (rc->flags & SA1_CAPF_GAME_SPRITE) {
            sy = sx;
        }
    } else {
        sx *= lsd_axis(m, 0);
        sy *= lsd_axis(m, 1);
    }
    *w = (double)(int16_t)sa1_rd16(p + 0x20) * sx;
    *h = (double)(int16_t)sa1_rd16(p + 0x22) * sy;
    return fabs(*w) > 0.0 && fabs(*h) > 0.0;
}

/* the f of the aura's pulse (code_8C0B4F00.c:89-93) from a clock value */
static double lsd_pulse(uint32_t clock)
{
    double c = (double)(clock & 0x7Fu);
    return (c >= 64.0 ? 128.0 - c : c) * 0.015625;
}

/* ---- synthetic records: written into a world's capture block, snapshotted by sa1_capture_close, emitted into a
 * private builder (the emitter's rules on inputs no character script draws) ---- */
typedef struct lsd_syn {
    sa1g_world *w;
    sa1_capture *c;
    uint32_t block, recs, pay, pay_cap, n, used;
} lsd_syn;

static uint8_t *lsd_gptr(sa1_capture *c, uint32_t g, uint32_t n)
{
    return (uint8_t *)(uintptr_t)sa1_capture_read(c, g, n);
}

static int lsd_syn_begin(lsd_syn *s, sa1_context *ctx, sa1_character_id id)
{
    sa1_capture_stats st;
    const uint8_t *h;
    sa1_cap_hdr hd;
    memset(s, 0, sizeof *s);
    s->w = sa1_character_world(ctx, id);
    s->c = sa1_character_capture(ctx, id);
    if (s->w == NULL || s->c == NULL) {
        return -1;
    }
    sa1_capture_get_stats(s->c, &st);
    s->block = st.block;
    h = sa1_capture_read(s->c, s->block, (uint32_t)sizeof(sa1_cap_hdr));
    if (h == NULL) {
        return -1;
    }
    memcpy(&hd, h, sizeof hd);
    s->recs = hd.rec;
    s->pay = hd.pay;
    s->pay_cap = hd.pay_cap;
    sa1_capture_open(s->c);
    return 0;
}

/* a record at XMTRX m (row-vector convention; NULL: identity), matrix stack slot 0 the identity */
static sa1_cap_rec *lsd_syn_rec(lsd_syn *s, uint32_t kind, uint32_t attr, uint32_t n, const float m[16])
{
    sa1_cap_rec *r = (sa1_cap_rec *)lsd_gptr(s->c, s->recs + s->n * (uint32_t)sizeof(sa1_cap_rec),
                                            (uint32_t)sizeof(sa1_cap_rec));
    int i;
    if (r == NULL) {
        return NULL;
    }
    memset(r, 0, sizeof *r);
    r->kind = kind;
    r->attr = attr;
    r->n = n;
    for (i = 0; i < 16; i++) {
        r->m[i] = m ? m[i] : (i % 5 == 0 ? 1.0f : 0.0f);
        r->base[i] = i % 5 == 0 ? 1.0f : 0.0f;
    }
    r->blend = 8u | (10u << 8);     /* SRCALPHA / ONE, as the charge effects */
    r->pay = SA1_CAP_NOPAY;
    s->n++;
    return r;
}

static uint8_t *lsd_syn_pay(lsd_syn *s, sa1_cap_rec *r, uint32_t len)
{
    uint32_t off = (s->used + 3u) & ~3u;
    if (r == NULL || off + len > s->pay_cap) {
        return NULL;
    }
    s->used = off + len;
    r->pay = off;
    r->pay_len = len;
    return lsd_gptr(s->c, s->pay + off, len);
}

/* a point list: n points pts (3 floats each), white, with UVs uv (2 s16 each) when uv != NULL */
static void lsd_syn_points(lsd_syn *s, sa1_cap_rec *r, uint32_t num, const float *pts, const int16_t *uv)
{
    uint8_t *q = lsd_syn_pay(s, r, 16u + num * (12u + 4u + (uv ? 4u : 0u)));
    uint32_t i, one = 1, zero = 0;
    if (q == NULL) {
        return;
    }
    memcpy(q, &num, 4);
    memcpy(q + 4, &one, 4);
    memcpy(q + 8, uv ? &one : &zero, 4);
    memcpy(q + 12, &num, 4);
    memcpy(q + 16, pts, 12u * num);
    for (i = 0; i < num; i++) {
        uint32_t white = 0xFFFFFFFFu;
        memcpy(q + 16 + 12u * num + 4u * i, &white, 4);
        if (uv) {
            memcpy(q + 16 + 16u * num + 4u * i, uv + 2u * i, 4);
        }
    }
}

/* a sprite: NJS_SPRITE (p 0, sx, sy, ang, tlist) and tanim cell (w x h, centre cx / cy, uv 0..255) */
static void lsd_syn_sprite(lsd_syn *s, sa1_cap_rec *r, float sx, float sy, int32_t ang, int16_t w, int16_t h,
                           int16_t cx, int16_t cy)
{
    uint8_t *q = lsd_syn_pay(s, r, 0x34u);
    int16_t cell[10];
    if (q == NULL) {
        return;
    }
    memset(q, 0, 0x34u);
    memcpy(q + 0x0C, &sx, 4);
    memcpy(q + 0x10, &sy, 4);
    memcpy(q + 0x14, &ang, 4);
    cell[0] = w;
    cell[1] = h;
    cell[2] = cx;
    cell[3] = cy;
    cell[4] = 0;
    cell[5] = 0;
    cell[6] = 255;
    cell[7] = 255;
    cell[8] = 0;
    cell[9] = 0;
    memcpy(q + 0x20, cell, sizeof cell);
}

/* the records into the snapshot (sa1_capture_close), then emitted into b */
static sa1_status lsd_syn_emit(lsd_syn *s, sa1_context *ctx, sa1_meshbuf *b)
{
    uint8_t *h = lsd_gptr(s->c, s->block, (uint32_t)sizeof(sa1_cap_hdr));
    sa1_capture_emit_opts o;
    sa1_geometry g;
    sa1_cap_hdr hd;
    uint32_t frame = 0;
    if (h == NULL) {
        return SA1_ERR_BAD_FORMAT;
    }
    sa1g_world_rd32(s->w, LSD_FRAME, &frame);
    memcpy(&hd, h, sizeof hd);
    hd.count = s->n;
    hd.total += s->n;
    hd.frame = frame;
    hd.pay_used = s->used;
    memcpy(h, &hd, sizeof hd);
    sa1_capture_close(s->c);
    memset(&o, 0, sizeof o);
    o.effects = 1;
    memset(&g, 0, sizeof g);
    return sa1_capture_emit_into(ctx, s->c, &o, b, &g);
}

/* the point index (0..n-1) of a host position, -1 if none */
static int lsd_point_of(const sa1_coords *co, const float *pts, uint32_t n, const float *pos)
{
    static const double ident[12] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };
    uint32_t i;
    for (i = 0; i < n; i++) {
        float hp[3];
        lsd_host_point(co, ident, pts + 3u * i, hp);
        if (lsd_near(hp, pos, 1e-3f)) {
            return (int)i;
        }
    }
    return -1;
}

/* triangle t of b as point indices, sorted */
static void lsd_tri_points(const sa1_coords *co, const sa1_meshbuf *b, uint32_t t, const float *pts, uint32_t n,
                           int out[3])
{
    int k, x;
    for (k = 0; k < 3; k++) {
        out[k] = lsd_point_of(co, pts, n, b->pos + 3u * b->idx[3u * t + k]);
    }
    for (k = 0; k < 2; k++) {
        if (out[k] > out[k + 1]) { x = out[k]; out[k] = out[k + 1]; out[k + 1] = x; }
    }
    if (out[0] > out[1]) { x = out[0]; out[0] = out[1]; out[1] = x; }
}

/* the triangles of draw d of b against an expected list of sorted point triples */
static int lsd_tris_are(const sa1_coords *co, const sa1_meshbuf *b, const sa1_draw *d, const float *pts, uint32_t n,
                        const int *expect, uint32_t ntri)
{
    uint32_t t;
    if (d == NULL || d->prim != SA1_PRIM_TRIANGLES || d->count != ntri) {
        return 0;
    }
    for (t = 0; t < ntri; t++) {
        int got[3];
        lsd_tri_points(co, b, d->first + t, pts, n, got);
        if (got[0] != expect[3 * t] || got[1] != expect[3 * t + 1] || got[2] != expect[3 * t + 2]) {
            return 0;
        }
    }
    return 1;
}

/* the segments of line draw d of b against an expected list of (start, end) point pairs, in order */
static int lsd_segs_are(const sa1_coords *co, const sa1_meshbuf *b, const sa1_draw *d, const float *pts, uint32_t n,
                        const int *expect, uint32_t nseg)
{
    uint32_t s;
    if (d == NULL || d->prim != SA1_PRIM_LINES || d->count != nseg) {
        return 0;
    }
    for (s = 0; s < nseg; s++) {
        const sa1_line_vertex *v = &b->lv[2u * (d->first + s)];
        if (lsd_point_of(co, pts, n, v[0].position) != expect[2 * s] ||
            lsd_point_of(co, pts, n, v[1].position) != expect[2 * s + 1]) {
            return 0;
        }
    }
    return 1;
}

/* a one-triangle model in host memory at guest 0x0C100000 (the emitter reads through a reader callback) */
#define LSD_FAKE 0x0C100000u
static uint8_t g_lsd_fake[256];

static const uint8_t *lsd_fake_read(void *user, uint32_t g, uint32_t n)
{
    (void)user;
    if (g < LSD_FAKE || g - LSD_FAKE > sizeof g_lsd_fake || n > sizeof g_lsd_fake - (g - LSD_FAKE)) {
        return NULL;
    }
    return g_lsd_fake + (g - LSD_FAKE);
}

static void lsd_put32(uint32_t off, uint32_t v)
{
    memcpy(g_lsd_fake + off, &v, 4);
}

/* F2(a), 8C6209A8's colour of a material drawn by 8C60E49A: diffuse ARGB 0x80402010, constant material (a, r, g, b)
 * = (-0.25, 0, 0, -0.5) added (control-3D 0x20); a textured material under control-3D 0x200 has RGB 1.0 first.
 * Returns the RGBA of the emitted corner for (textured, ctl3d) */
static void lsd_ninja_colour(sa1_context *ctx, int textured, uint32_t ctl3d, uint8_t out[4])
{
    static const float pts[9] = { 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f };
    static const uint16_t idx[3] = { 0, 1, 2 };
    static const double ident[12] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };
    static const float cmat[4] = { -0.25f, 0.0f, 0.0f, -0.5f };
    sa1_cap_rec rc;
    sa1_draw_state ds;
    sa1_emit_info ei;
    sa1_meshbuf mb;
    memset(g_lsd_fake, 0, sizeof g_lsd_fake);
    lsd_put32(0x00, LSD_FAKE + 0x30u);  /* points */
    lsd_put32(0x08, 3u);                /* nbPoint */
    lsd_put32(0x0C, LSD_FAKE + 0x60u);  /* meshsets */
    lsd_put32(0x10, LSD_FAKE + 0x90u);  /* mats */
    lsd_put32(0x14, 1u | (1u << 16));   /* nbMeshset 1, nbMat 1 */
    memcpy(g_lsd_fake + 0x30, pts, sizeof pts);
    lsd_put32(0x60, 0u | (1u << 16));   /* triangles, material 0; one mesh */
    lsd_put32(0x64, LSD_FAKE + 0x80u);
    memcpy(g_lsd_fake + 0x80, idx, sizeof idx);
    lsd_put32(0x90, 0x80402010u);       /* diffuse */
    lsd_put32(0xA0, SA1_MAT_IGNORE_LIGHT | (textured ? SA1_MAT_USE_TEXTURE : 0u));
    memset(&rc, 0, sizeof rc);
    rc.kind = SA1_CAP_NJMOTION;
    rc.flags = SA1_CAPF_NJ_ACTION;
    rc.ctl3d = ctl3d;
    rc.attr_and = 0xFFFFFFFFu;
    memcpy(rc.cmat, cmat, sizeof rc.cmat);
    sa1_capture_draw_state(&rc, &ds);
    memset(&ei, 0, sizeof ei);
    ei.reader = lsd_fake_read;
    ei.ds = &ds;
    ei.source = (uint8_t)SA1_SOURCE_MODEL;
    memset(&mb, 0, sizeof mb);
    memset(out, 0, 4);
    if (sa1_pose_emit_model_ex(ctx, &mb, &ei, LSD_FAKE, ident) == SA1_OK && mb.nt == 1u) {
        memcpy(out, mb.col, 4);
    }
    sa1_meshbuf_free(ctx, &mb);
}

static void lsd_synthetic(sa1_context *ctx, sa1_character_id id, const sa1_cap_rec *aura, int32_t aura_g)
{
    static const float pts[3 * 6] = { 1.0f, 0.5f, 0.0f, 2.0f, 2.5f, 0.25f, 3.0f, 1.5f, 1.0f,
                                      4.0f, 4.0f, 0.5f, 5.0f, 0.0f, 2.0f, 6.0f, 3.0f, 1.5f };
    static const float diag231[16] = { 2, 0, 0, 0, 0, 3, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    static const float mirror_y[16] = { 1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    static const float zero3[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1 };
    /* expected topology (sorted point triples; ordered segments) */
    static const int zig5[9] = { 0, 1, 4, 1, 3, 4, 1, 2, 3 };           /* njDrawPolygon3D fill: 0,4,1,3,2 */
    static const int strip5[9] = { 0, 1, 2, 1, 2, 3, 2, 3, 4 };         /* 0x22 */
    static const int fan5[9] = { 0, 1, 2, 0, 2, 3, 0, 3, 4 };           /* 0x23 */
    static const int fan2_5[6] = { 0, 1, 2, 0, 3, 4 };                  /* 0x21: (0, 1 + 2i, 2 + 2i) */
    static const int sep6[6] = { 0, 1, 2, 3, 4, 5 };                    /* 0x20 */
    static const int loop5[10] = { 0, 4, 0, 1, 1, 2, 2, 3, 3, 4 };      /* outline: (0, n-1) first, then (i-1, i) */
    static const int tri_edges[12] = { 0, 2, 0, 1, 1, 2, 3, 5, 3, 4, 4, 5 };
    static const int conn4[6] = { 0, 1, 1, 2, 2, 3 };                   /* njDrawLine3D 0x42 */
    static const int pairs4[4] = { 0, 1, 2, 3 };                        /* 0x40 */
    static const int fanl4[6] = { 0, 1, 0, 2, 0, 3 };                   /* 0x41 */
    lsd_syn s;
    sa1_meshbuf mb;
    sa1_coords co;
    sa1_capture_stats cs;
    sa1_status st;
    float k;
    uint32_t i;
    int ok[24];
    memset(ok, 0, sizeof ok);
    memset(&co, 0, sizeof co);
    co.struct_size = sizeof co;
    sa1_get_coords(ctx, &co);
    k = co.scale;
    if (lsd_syn_begin(&s, ctx, id) != 0) {
        CHECK(0, "synthetic records: the character's capture block");
        return;
    }
    /* polygons: fill / outline, both entry points (records 0-7) */
    lsd_syn_points(&s, lsd_syn_rec(&s, SA1_CAP_TRI3D, 0x60u, 5, NULL), 5, pts, NULL);      /* d0: zig-zag */
    lsd_syn_points(&s, lsd_syn_rec(&s, SA1_CAP_POLY3D, 0x62u, 3, NULL), 5, pts, NULL);     /* d1: strip */
    lsd_syn_points(&s, lsd_syn_rec(&s, SA1_CAP_POLY3D, 0x63u, 3, NULL), 5, pts, NULL);     /* d2: fan */
    lsd_syn_points(&s, lsd_syn_rec(&s, SA1_CAP_POLY3D, 0x61u, 2, NULL), 5, pts, NULL);     /* d3: stride-2 fan */
    lsd_syn_points(&s, lsd_syn_rec(&s, SA1_CAP_POLY3D, 0x60u, 2, NULL), 6, pts, NULL);     /* d4: separate */
    lsd_syn_points(&s, lsd_syn_rec(&s, SA1_CAP_TRI3D, 0x40u, 5, NULL), 5, pts, NULL);      /* d5: outline loop */
    lsd_syn_points(&s, lsd_syn_rec(&s, SA1_CAP_POLY3D, 0x40u, 2, NULL), 6, pts, NULL);     /* d6: triangle edges */
    {
        /* d7: a textured strip whose texlist has more than one texture: untextured, counted */
        sa1_cap_rec *r = lsd_syn_rec(&s, SA1_CAP_POLY3D, 0x80000062u, 3, NULL);
        static const int16_t uv[10] = { 0, 0, 0, 255, 255, 0, 255, 255, 0, 0 };
        if (r != NULL) {
            r->texlist = 0x8C1ACA78u;   /* not a texlist with one texture (the shadow object's address) */
        }
        lsd_syn_points(&s, r, 5, pts, uv);
    }
    /* lines (records 8-10) */
    lsd_syn_points(&s, lsd_syn_rec(&s, SA1_CAP_LINE3D, 0x42u, 3, NULL), 4, pts, NULL);     /* d8: connected */
    lsd_syn_points(&s, lsd_syn_rec(&s, SA1_CAP_LINE3D, 0x40u, 2, NULL), 4, pts, NULL);     /* d9: pairs */
    {
        sa1_cap_rec *r = lsd_syn_rec(&s, SA1_CAP_LINE3D, 0x41u, 3, NULL);                  /* d10: fan from 0 */
        if (r != NULL) {
            r->blend = 1u | (1u << 8);  /* another blend state: a draw of its own */
        }
        lsd_syn_points(&s, r, 4, pts, NULL);
    }
    /* sprites (records 11-15): unscaled under a non-uniform matrix; unscaled under a mirroring one with an angle;
     * unscaled of zero extent; scaled by the game's own draw (sx on both axes); scaled by njDrawSprite3D */
    lsd_syn_sprite(&s, lsd_syn_rec(&s, SA1_CAP_SPRITE3D, 0x20u, 0, diag231), 0.5f, 0.25f, 0, 64, 32, 32, 16);
    lsd_syn_sprite(&s, lsd_syn_rec(&s, SA1_CAP_SPRITE3D, 0x21u, 0, mirror_y), 1.0f, 1.0f, 0x4000, 64, 32, 32, 8);
    lsd_syn_sprite(&s, lsd_syn_rec(&s, SA1_CAP_SPRITE3D, 0x20u, 0, zero3), 1.0f, 1.0f, 0, 64, 64, 32, 32);
    {
        sa1_cap_rec *r = lsd_syn_rec(&s, SA1_CAP_SPRITE3D, 0x30u, 0, diag231);
        if (r != NULL) {
            r->flags = SA1_CAPF_GAME_SPRITE;
        }
        lsd_syn_sprite(&s, r, 0.5f, 0.25f, 0, 64, 32, 32, 16);
    }
    lsd_syn_sprite(&s, lsd_syn_rec(&s, SA1_CAP_SPRITE3D, 0x30u, 0, diag231), 0.5f, 0.25f, 0, 64, 32, 32, 16);
    /* the aura's node model drawn by 8C60E49A (the flag) and by 8C60E080 (no flag: the snowboards' path) */
    if (aura != NULL) {
        sa1_cap_rec *r;
        for (i = 0; i < 2; i++) {
            r = lsd_syn_rec(&s, SA1_CAP_NJMOTION, 0, 0, aura->m);
            if (r != NULL) {
                memcpy(r->base, aura->base, sizeof r->base);
                r->obj = aura->obj;
                r->texlist = aura->texlist;
                r->ctl3d = aura->ctl3d;
                r->attr_and = aura->attr_and;
                r->attr_or = aura->attr_or;
                memcpy(r->cmat, aura->cmat, sizeof r->cmat);
                r->flags = i == 0 ? SA1_CAPF_NJ_ACTION : 0u;
            }
        }
    }
    memset(&mb, 0, sizeof mb);
    st = lsd_syn_emit(&s, ctx, &mb);
    sa1_capture_get_stats(s.c, &cs);
    if (st != SA1_OK || mb.ndraw < 10u) {
        CHECK(0, "synthetic records: emit (%s, %u draws)", sa1_status_string(st), (unsigned)mb.ndraw);
        sa1_meshbuf_free(ctx, &mb);
        return;
    }
    ok[0] = lsd_tris_are(&co, &mb, &mb.draws[0], pts, 5, zig5, 3);
    ok[1] = lsd_tris_are(&co, &mb, &mb.draws[1], pts, 5, strip5, 3);
    ok[2] = lsd_tris_are(&co, &mb, &mb.draws[2], pts, 5, fan5, 3);
    ok[3] = lsd_tris_are(&co, &mb, &mb.draws[3], pts, 5, fan2_5, 2);
    ok[4] = lsd_tris_are(&co, &mb, &mb.draws[4], pts, 6, sep6, 2);
    /* d5 and d6 are both outline line draws of source POLYGON with one state: one merged draw of 5 + 6 segments */
    {
        sa1_draw d5 = mb.draws[5], d6;
        d5.count = 5;
        d6 = mb.draws[5];
        d6.first += 5;
        d6.count = 6;
        ok[5] = mb.draws[5].prim == SA1_PRIM_LINES && mb.draws[5].source == SA1_SOURCE_POLYGON &&
                mb.draws[5].count == 11u && lsd_segs_are(&co, &mb, &d5, pts, 5, loop5, 5) &&
                lsd_segs_are(&co, &mb, &d6, pts, 6, tri_edges, 6) && cs.outlines == 2u;
    }
    ok[6] = mb.draws[6].prim == SA1_PRIM_TRIANGLES && mb.draws[6].count == 3u && mb.draws[6].archive < 0 &&
            (mb.draws[6].flags & SA1_DRAW_NO_TEXTURE) != 0 && cs.poly_untextured == 1u;
    /* d8 and d9 merge (same state, source LINE); d10 has its own blend */
    {
        sa1_draw a = mb.draws[7], b2 = mb.draws[7];
        a.count = 3;
        b2.first += 3;
        b2.count = 2;
        ok[7] = mb.draws[7].source == SA1_SOURCE_LINE && mb.draws[7].count == 5u &&
                lsd_segs_are(&co, &mb, &a, pts, 4, conn4, 3) && lsd_segs_are(&co, &mb, &b2, pts, 4, pairs4, 2) &&
                lsd_segs_are(&co, &mb, &mb.draws[8], pts, 4, fanl4, 3);
    }
    /* sprites: draw 9 holds the first two (same state), the zero one is left out */
    {
        const sa1_sprite *sp = mb.spr;
        ok[8] = mb.nspr == 4u && cs.zero_sprites == 1u;
        ok[9] = ok[8] && fabsf(sp[0].rect[0] + 32.0f * k) < 1e-3f && fabsf(sp[0].rect[2] - 32.0f * k) < 1e-3f &&
                fabsf(sp[0].rect[1] + 12.0f * k) < 1e-3f && fabsf(sp[0].rect[3] - 12.0f * k) < 1e-3f &&
                sp[0].angle == 0.0f && sp[0].uv[0] == 0.0f && sp[0].uv[2] == 255.0f / 256.0f &&
                sp[0].uv[1] == 0.0f && sp[0].uv[3] == 255.0f / 256.0f;
        ok[10] = ok[8] && fabsf(sp[1].rect[0] + 32.0f * k) < 1e-3f && fabsf(sp[1].rect[2] - 32.0f * k) < 1e-3f &&
                 fabsf(sp[1].rect[1] + 24.0f * k) < 1e-3f && fabsf(sp[1].rect[3] - 8.0f * k) < 1e-3f &&
                 fabsf(sp[1].angle + 1.5707963f) < 1e-5f && sp[1].uv[1] == 255.0f / 256.0f && sp[1].uv[3] == 0.0f;
        ok[11] = ok[8] && fabsf(sp[2].rect[0] + 16.0f * k) < 1e-3f && fabsf(sp[2].rect[2] - 16.0f * k) < 1e-3f &&
                 fabsf(sp[2].rect[1] + 8.0f * k) < 1e-3f && fabsf(sp[2].rect[3] - 8.0f * k) < 1e-3f;
        ok[12] = ok[8] && fabsf(sp[3].rect[0] + 16.0f * k) < 1e-3f && fabsf(sp[3].rect[2] - 16.0f * k) < 1e-3f &&
                 fabsf(sp[3].rect[1] + 4.0f * k) < 1e-3f && fabsf(sp[3].rect[3] - 4.0f * k) < 1e-3f;
    }
    if (aura != NULL) {
        const sa1_draw *da = NULL, *dh = NULL;
        for (i = 0; i < mb.ndraw; i++) {
            if (mb.draws[i].prim == SA1_PRIM_TRIANGLES && mb.draws[i].material == LSD_AURA_MAT) {
                if (mb.draws[i].source == SA1_SOURCE_MODEL && da == NULL) {
                    da = &mb.draws[i];
                } else if (mb.draws[i].source == SA1_SOURCE_CHARACTER && dh == NULL) {
                    dh = &mb.draws[i];
                }
            }
        }
        if (da != NULL) {
            const uint8_t *c0 = mb.col + 12u * da->first;
            ok[13] = (da->flags & (SA1_DRAW_BLEND | SA1_DRAW_ADDITIVE | SA1_DRAW_UNLIT)) ==
                         (SA1_DRAW_BLEND | SA1_DRAW_ADDITIVE | SA1_DRAW_UNLIT) &&
                     !(da->flags & SA1_DRAW_PALETTE_LIT) && c0[0] == 0 && (int32_t)c0[1] == aura_g && c0[2] == 255 &&
                     c0[3] == 255;
        }
        if (dh != NULL) {
            const uint8_t *c0 = mb.col + 12u * dh->first;
            ok[14] = (dh->flags & SA1_DRAW_PALETTE_LIT) != 0 && c0[0] == 255 && c0[1] == 255 && c0[2] == 255;
        }
    }
    CHECK(ok[0] && ok[1] && ok[2] && ok[3] && ok[4],
          "synthetic polygons (F4): njDrawPolygon3D fill of 5 points = the SDK's zig-zag strip 0,4,1,3,2 (triangles "
          "{0,1,4} {1,3,4} {1,2,3}: a quad's diagonal 1-3); njDrawTriangle3D 0x22 strip, 0x23 fan, 0x21 fan of every "
          "other point (0,1,2) (0,3,4), 0x20 separate triangles: %d %d %d %d %d", ok[0], ok[1], ok[2], ok[3], ok[4]);
    CHECK(ok[5], "synthetic outlines (no fill 0x20): njDrawPolygon3D's closed edge loop (0,4) (0,1) (1,2) (2,3) (3,4) "
          "and njDrawTriangle3D's three edges per triangle as line segments of source POLYGON (%u outline draws)",
          (unsigned)cs.outlines);
    CHECK(ok[6], "synthetic textured strip with a texlist that is not a one-texture one: emitted untextured, "
          "SA1_DRAW_NO_TEXTURE, counted (%u; phase 2 of F4)", (unsigned)cs.poly_untextured);
    CHECK(ok[7], "synthetic lines (F5): njDrawLine3D 0x42 connected (0,1) (1,2) (2,3), 0x40 pairs (0,1) (2,3), 0x41 "
          "a fan (0,1) (0,2) (0,3)");
    CHECK(ok[9] && ok[10] && ok[11] && ok[12],
          "synthetic sprites (F1): unscaled 64x32 px, sp 0.5 x 0.25 under XMTRX diag(2, 3, 1): 64 x 24 units (sx |e_x| "
          "by sy |e_y|); unscaled under a mirroring matrix (local y = view up, the billboard case) with angle 0x4000: rect "
          "and v flipped, angle -90 degrees; zero extent left out (%u); scaled game sprite: the matrix ignored, sx on "
          "both axes (32 x 16); scaled njDrawSprite3D: sx by sy (32 x 8): %d %d %d %d", (unsigned)cs.zero_sprites,
          ok[9], ok[10], ok[11], ok[12]);
    if (aura != NULL) {
        CHECK(ok[13] && ok[14],
              "the aura's node model recorded by 8C60E49A (SA1_CAPF_NJ_ACTION): Ninja's colours (source MODEL, "
              "BLEND|ADDITIVE|UNLIT, not palette-lit, (0, %d, 255, 255)); the same record without the flag (8C60E080: "
              "the snowboards' path) keeps the hand-written renderer's rules (source CHARACTER, palette-lit, white RGB; "
              "F2(a): unchanged): %d %d", (int)aura_g, ok[13], ok[14]);
    }
    sa1_meshbuf_free(ctx, &mb);
    {
        uint8_t a[4], b2[4], c2[4];
        lsd_ninja_colour(ctx, 1, 0x36Cu, a);
        lsd_ninja_colour(ctx, 1, 0x16Cu, b2);
        lsd_ninja_colour(ctx, 0, 0x36Cu, c2);
        CHECK(a[0] == 255 && a[1] == 255 && a[2] == 128 && a[3] == 64 && b2[0] == 64 && b2[1] == 32 && b2[2] == 0 &&
              b2[3] == 64 && c2[0] == 64 && c2[1] == 32 && c2[2] == 0 && c2[3] == 64,
              "8C6209A8's colours for 8C60E49A's records (F2(a)): diffuse 0x80402010 with the constant material (-0.25, "
              "0, 0, -0.5) added to all four channels; a textured material under control-3D 0x200 starts from RGB 1.0: "
              "(%u,%u,%u,%u) (expected 255,255,128,64), without 0x200 (%u,%u,%u,%u) and untextured (%u,%u,%u,%u) "
              "(expected 64,32,0,64)", a[0], a[1], a[2], a[3], b2[0], b2[1], b2[2], b2[3], c2[0], c2[1], c2[2], c2[3]);
    }
}

/* lsd-1: Sonic with the Light Speed Shoes and the Crystal Ring (event flags 0x8D + 0x8E) holds the action button on
 * Emerald Coast act 1's own collision (DISC): the charge sparkles, their ribbons and the charged aura */
static void lsd_sonic(const env *E)
{
    sa1_context *ctx = disc_ctx(E, SA1_CHAR_SONIC);
    sa1_character_id id = ctx ? disc_char(ctx, SA1_CHAR_SONIC) : 0;
    sa1g_world *w = id ? sa1_character_world(ctx, id) : NULL;
    sa1_coords co;
    sa1_input in;
    sa1_cap_rec aura_rec;
    int have_aura_rec = 0;
    int32_t arc03 = -1, tex03, aura_g = -1, clock_k = -1;
    uint32_t pair, prev_flags = 0, equipment = 0;
    uint32_t spark = 0, spark_zero = 0, spark_bad = 0, wpair = 0, map_bad = 0, spark_flip = 0;
    double spark_turn = 0.0;
    uint32_t aura_pairs = 0, aura_bad = 0, aura_col_bad = 0, aura_rec_bad = 0, nonblend_bad = 0;
    uint32_t rib = 0, rib_bad = 0, rib_uv_bad = 0, rib_uv70 = 0, poly_tex = 0;
    double first_w[2] = { 0.0, 0.0 };
    uint32_t nfirst = 0;
    float k;
    printf("  lsd-1: Sonic with the Light Speed Shoes and the Crystal Ring charging on Emerald Coast act 1 (DISC):\n");
    if (w == NULL) {
        CHECK(0, "Sonic: context / character");
        sa1_shutdown(ctx);
        return;
    }
    memset(&co, 0, sizeof co);
    co.struct_size = sizeof co;
    sa1_get_coords(ctx, &co);
    k = co.scale;
    lsd_event_flag(w, 0x8D);
    lsd_event_flag(w, 0x8E);
    tex03 = lsd_tex_any(ctx, SA1_CHAR_SONIC, "lcharge03", &arc03);
    memset(&in, 0, sizeof in);
    in.struct_size = sizeof in;
    for (pair = 0; pair < 15u + 75u; pair++) {
        const sa1_render *r = NULL;
        const sa1_cap_rec *recs = NULL;
        sa1_capture *cap;
        sa1_state_ex x;
        sa1_capture_stats cs;
        uint32_t nrec, i, j = 0, aura_tris = 0, nj_action = 0, clock = 0, pd = 0;
        const sa1_draw *first_aura = NULL;
        const sa1_cap_rec *first_action = NULL;
        in.buttons = pair >= 15u ? SA1_BUTTON_ACTION : 0u;
        if (sa1_character_tick(ctx, id, &in, 1.0f / 30.0f, NULL, NULL) != SA1_OK ||
            sa1_character_render(ctx, id, 1.0f, &r) != SA1_OK) {
            CHECK(0, "pair %u: %s", (unsigned)pair, sa1_last_error(ctx));
            break;
        }
        memset(&x, 0, sizeof x);
        x.struct_size = sizeof x;
        sa1_character_state_ex(ctx, id, &x);
        equipment = x.equipment;
        sa1g_world_rd32(w, LSD_GAME_CLOCK, &clock);
        cap = sa1_character_capture(ctx, id);
        nrec = sa1_capture_records(cap, &recs);
        sa1_capture_get_stats(cap, &cs);
        poly_tex += cs.poly_textured;
        /* sprites: one per record with a payload and a non-zero extent, in record order */
        for (i = 0; i < nrec; i++) {
            const sa1_cap_rec *rc = &recs[i];
            const uint8_t *p;
            double ww, hh, m[12];
            int sparkle;
            if (rc->kind != SA1_CAP_SPRITE3D) {
                continue;
            }
            p = sa1_capture_payload(cap, rc, 0x34u);
            sparkle = (rc->texlist == LSD_SPARKLE_TL0 || rc->texlist == LSD_SPARKLE_TL1) && rc->attr == 0x20u;
            if (!lsd_sprite_size(cap, rc, &ww, &hh)) {
                spark_zero += sparkle && p != NULL;
                continue;
            }
            if (j >= r->num_sprites) {
                map_bad++;
                break;
            }
            if (sparkle) {
                const sa1_sprite *sp = &r->sprites[j];
                double gw, gh, ew = (sp->rect[2] - sp->rect[0]) / k, eh = (sp->rect[3] - sp->rect[1]) / k;
                sa1_capture_world_matrix(rc, m);
                gw = 64.0 * 0.2 * lsd_axis(m, 0);
                gh = 64.0 * 0.2 * lsd_axis(m, 1);
                spark++;
                if (!(sa1_rd16(p + 0x20) == 64u && fabs(sa1_rdf32(p + 0x0C) - 0.2) < 1e-6 && fabs(ew - gw) <= 1e-4 &&
                      fabs(eh - gh) <= 1e-4 &&
                      sp->uv[0] == (float)(int16_t)sa1_rd16(p + 0x28) / 256.0f &&
                      sp->uv[2] == (float)(int16_t)sa1_rd16(p + 0x2C) / 256.0f)) {
                    if (spark_bad++ == 0) {
                        printf("      first wrong sparkle (pair %u): tanim %u px, sp.sx %.6f, emitted %.6f x %.6f, game "
                               "%.6f x %.6f, u %.6f..%.6f (tanim %d..%d)\n", (unsigned)pair,
                               (unsigned)sa1_rd16(p + 0x20), (double)sa1_rdf32(p + 0x0C), ew, eh, gw, gh,
                               (double)sp->uv[0], (double)sp->uv[2], (int)(int16_t)sa1_rd16(p + 0x28),
                               (int)(int16_t)sa1_rd16(p + 0x2C));
                    }
                }
                spark_flip += sp->uv[1] > sp->uv[3];       /* v flipped: the matrix mirrors (a world-up billboard) */
                if (fabs(sp->angle) > spark_turn) {
                    spark_turn = fabs(sp->angle);
                }
                if (nfirst < 2u && (nfirst == 0u || pair != wpair)) {
                    first_w[nfirst++] = ew;     /* the first sparkle of the first two pairs that draw sparkles */
                    wpair = pair;
                }
            }
            j++;
        }
        map_bad += j != r->num_sprites;
        /* the ribbons: polygon records map in order onto the filled polygon draws */
        for (i = 0; i < nrec; i++) {
            const sa1_cap_rec *rc = &recs[i];
            const uint8_t *p;
            const sa1_draw *d = NULL;
            uint32_t num, hasc, t;
            if (rc->kind != SA1_CAP_TRI3D && rc->kind != SA1_CAP_POLY3D) {
                continue;
            }
            p = sa1_capture_payload(cap, rc, 16u);
            if (p == NULL || sa1_rd32(p) < 3u || !(rc->attr & 0x20u)) {
                continue;
            }
            while (pd < r->num_draws && !(r->draws[pd].source == SA1_SOURCE_POLYGON &&
                                          r->draws[pd].prim == SA1_PRIM_TRIANGLES)) {
                pd++;
            }
            if (pd >= r->num_draws) {
                rib_bad++;
                break;
            }
            d = &r->draws[pd++];
            if (rc->kind != SA1_CAP_POLY3D || rc->attr != 0x80000062u) {
                continue;
            }
            rib++;
            num = sa1_rd32(p);
            hasc = sa1_rd32(p + 4);
            if (d->count != rc->n || d->archive != arc03 || d->texture != tex03 || (d->flags & SA1_DRAW_NO_TEXTURE) ||
                (d->flags & (SA1_DRAW_BLEND | SA1_DRAW_ADDITIVE)) != (SA1_DRAW_BLEND | SA1_DRAW_ADDITIVE) ||
                !sa1_rd32(p + 8) || rc->n + 2u > num) {
                rib_bad++;
                continue;
            }
            for (t = 0; t < d->count; t++) {
                const float *uv = r->uvs + 6u * (d->first + t);
                const uint8_t *tx = p + 16u + 12u * num + (hasc ? 4u * num : 0u);
                uint32_t c2, v, hit = 0;
                for (c2 = 0; c2 < 3; c2++) {
                    for (v = t; v < t + 3u; v++) {
                        float eu = (float)(int16_t)sa1_rd16(tx + 4u * v) / 256.0f;
                        float ev = (float)(int16_t)sa1_rd16(tx + 4u * v + 2u) / 256.0f;
                        if (uv[2 * c2] == eu && uv[2 * c2 + 1] == ev) {
                            hit++;
                            break;
                        }
                    }
                    rib_uv70 += uv[2 * c2] == 112.0f / 256.0f && uv[2 * c2 + 1] == 0.0f;
                }
                rib_uv_bad += hit != 3u;
            }
        }
        /* the aura: its records (8C60E49A: SA1_CAPF_NJ_ACTION) and its draw */
        for (i = 0; i < nrec; i++) {
            if (recs[i].kind == SA1_CAP_NJMOTION && (recs[i].flags & SA1_CAPF_NJ_ACTION)) {
                if (first_action == NULL) {
                    first_action = &recs[i];
                }
                nj_action++;
            }
        }
        for (i = 0; i < r->num_draws; i++) {
            const sa1_draw *d = &r->draws[i];
            if (d->archive < 0 && !(d->flags & SA1_DRAW_BLEND) && d->source != SA1_SOURCE_POLYGON) {
                nonblend_bad++;
            }
            if (d->prim != SA1_PRIM_TRIANGLES || d->material != LSD_AURA_MAT) {
                continue;
            }
            if (first_aura == NULL) {
                first_aura = d;
            }
            aura_tris += d->count;
            aura_bad += d->source != SA1_SOURCE_MODEL ||
                        (d->flags & (SA1_DRAW_BLEND | SA1_DRAW_ADDITIVE | SA1_DRAW_UNLIT)) !=
                            (SA1_DRAW_BLEND | SA1_DRAW_ADDITIVE | SA1_DRAW_UNLIT) ||
                        (d->flags & SA1_DRAW_PALETTE_LIT) || d->blend_src != SA1_BLEND_ONE ||
                        d->blend_dst != SA1_BLEND_ONE;
        }
        if (prev_flags & 0x200u) {
            /* charged since the pair before: the display drew the aura in this pair (sub step 0) */
            aura_pairs++;
            aura_bad += aura_tris != 870u || nj_action != 60u || first_aura == NULL || first_action == NULL;
            if (first_aura != NULL && first_action != NULL) {
                const uint8_t *c0 = r->colors + 12u * first_aura->first;
                float g;
                int kk, found = -1;
                memcpy(&g, &first_action->cmat[2], 4);
                /* which clock value the display saw: B_8C752BEC advances at the end of every step, so it is the value
                   after the pair less 2 (sub step 0 drew); the record's constant material must agree */
                for (kk = 0; kk < 4 && found < 0; kk++) {
                    if (fabs((double)g - (0.28 - 0.18 * lsd_pulse(clock - (uint32_t)kk))) < 1e-5) {
                        found = kk;
                    }
                }
                if (clock_k < 0) {
                    clock_k = found;
                }
                aura_rec_bad += found != 2;
                {
                    double gg = 255.0 * (0.28 - 0.18 * lsd_pulse(clock - 2u));
                    aura_col_bad += !(c0[0] == 0 && fabs((double)c0[1] - gg) <= 0.5 + 1e-3 && c0[2] == 255 &&
                                      c0[3] == 255);
                    if (aura_g < 0) {
                        aura_g = c0[1];
                    }
                }
                if (!have_aura_rec) {
                    aura_rec = *first_action;
                    have_aura_rec = 1;
                }
            }
        }
        prev_flags = x.game_flags;
    }
    printf("      equipment 0x%04X; %u sparkle sprites (first widths %.2f, %.2f units; %u drawn v-flipped by a mirroring "
           "matrix, in-plane turn up to %.4f rad), %u of zero size left out; %u ribbons; aura in %u pairs (clock offset "
           "%d)\n", (unsigned)equipment, (unsigned)spark, first_w[0], first_w[1], (unsigned)spark_flip, spark_turn,
           (unsigned)spark_zero, (unsigned)rib, (unsigned)aura_pairs, (int)clock_k);
    CHECK((equipment & 3u) == 3u && spark > 20u && spark_bad == 0 && spark_zero > 0 && map_bad == 0,
          "F1: every charge sparkle (texlist 0x8C1C6AA4 / 0x8C1C6AAC, attr 0x20: unscaled, 64 px x 0.2) is 64 x 0.2 x "
          "|m e_x| by 64 x 0.2 x |m e_y| SA1 units within 1e-4 (%u sprites, %u wrong), UV / 256; the %u of zero size "
          "(njScale(0) on their last draw) emit nothing (sprites map onto records: %u mismatches)", (unsigned)spark,
          (unsigned)spark_bad, (unsigned)spark_zero, (unsigned)map_bad);
    CHECK(aura_pairs > 5u && aura_bad == 0 && aura_rec_bad == 0 && aura_col_bad == 0,
          "F2(a) + F3: once charged (taskwk.flag 0x200) the aura (60 node draws of 8C60E49A with SA1_CAPF_NJ_ACTION per "
          "pair) is 870 triangles of material 0x26182400, source MODEL, BLEND|ADDITIVE|UNLIT, ONE/ONE, not palette-lit; "
          "pass 1's corner colour is (0, round(255 x (0.28 - 0.18 f)), 255, 255) with f from B_8C752BEC (%u pairs, %u "
          "wrong draws, %u wrong records, %u wrong colours)", (unsigned)aura_pairs, (unsigned)aura_bad,
          (unsigned)aura_rec_bad, (unsigned)aura_col_bad);
    CHECK(nonblend_bad == 0, "no Sonic draw is untextured, opaque and from a model, sprite or line (the 0.5.0 aura was "
          "three opaque white shells): %u", (unsigned)nonblend_bad);
    CHECK(tex03 >= 0 && rib > 20u && rib_bad == 0 && rib_uv_bad == 0 && rib_uv70 > 0 && poly_tex > 0,
          "F4: every ribbon (njDrawTriangle3D 0x80000062, n triangles of a strip) is n triangles with texture lcharge03 "
          "(archive %d index %d, the Crystal Ring's), additive, its UVs the points' NJS_TEX / 256 ((0x70 / 256, 0) at %u "
          "corners): %u ribbons, %u wrong, %u triangles with wrong UVs", (int)arc03, (int)tex03, (unsigned)rib_uv70,
          (unsigned)rib, (unsigned)rib_bad, (unsigned)rib_uv_bad);
    lsd_synthetic(ctx, id, have_aura_rec ? &aura_rec : NULL, aura_g);
    sa1_shutdown(ctx);
}

/* lsd-2: Big's fishing line (njDrawLine3D(11, 0x42), 12 points) after a cast, on Emerald Coast act 3 with its stage
 * tasks (part 8's setting) */
static void lsd_big(const env *E)
{
    static const char *const seq = "20n30a40n";
    sa1_context *ctx = stage_ctx(E, SA1_CHAR_BIG, 1, 2);
    sa1_character_id id = ctx ? stage_char(ctx, SA1_CHAR_BIG, 1, 2) : 0;
    sa1_input in;
    const char *p = seq;
    long left = 0;
    char cur = 'n';
    uint32_t pairs = 0, bad = 0, moving = 0, other = 0;
    printf("  lsd-2: Big's fishing line on Emerald Coast act 3 (\"%s\"):\n", seq);
    if (!id) {
        CHECK(0, "Big: context / character");
        sa1_shutdown(ctx);
        return;
    }
    memset(&in, 0, sizeof in);
    in.struct_size = sizeof in;
    for (;;) {
        const sa1_render *r = NULL;
        const sa1_cap_rec *recs = NULL;
        sa1_capture *cap;
        uint32_t nrec, i, off = 0;
        if (left == 0) {
            char *e;
            if (!*p) {
                break;
            }
            left = strtol(p, &e, 10);
            cur = *e;
            p = e + 1;
        }
        left--;
        in.buttons = cur == 'a' ? SA1_BUTTON_ACTION : 0u;
        if (sa1_character_tick(ctx, id, &in, 1.0f / 30.0f, NULL, NULL) != SA1_OK ||
            sa1_character_render(ctx, id, 1.0f, &r) != SA1_OK) {
            CHECK(0, "Big: %s", sa1_last_error(ctx));
            break;
        }
        cap = sa1_character_capture(ctx, id);
        nrec = sa1_capture_records(cap, &recs);
        for (i = 0; i < nrec; i++) {
            const sa1_cap_rec *rc = &recs[i];
            const uint8_t *pl;
            uint32_t num, nseg, s;
            if ((rc->kind == SA1_CAP_TRI3D || rc->kind == SA1_CAP_POLY3D) && !(rc->attr & 0x20u)) {
                other++;        /* an outline would add segments in between: not expected for Big */
            }
            if (rc->kind != SA1_CAP_LINE3D) {
                continue;
            }
            pl = sa1_capture_payload(cap, rc, 16u);
            num = pl ? sa1_rd32(pl) : 0u;
            if (num < 2u) {
                continue;
            }
            nseg = (num - 2u) / ((rc->attr & 0x0Fu) ? 1u : 2u) + 1u;
            if (rc->n < nseg) {
                nseg = rc->n;
            }
            if (rc->attr == 0x42u && rc->n == 11u && num == 12u) {
                pairs++;
                if (nseg != 11u || 2u * (off + nseg) > r->num_line_vertices) {
                    bad++;
                } else {
                    const sa1_line_vertex *v = r->line_vertices + 2u * off;
                    for (s = 0; s + 1u < 11u; s++) {
                        bad += memcmp(v[2u * s + 1u].position, v[2u * s + 2u].position, sizeof v->position) != 0;
                        moving += memcmp(v[2u * s].position, v[2u * s + 1u].position, sizeof v->position) != 0;
                    }
                }
            }
            off += nseg;
        }
    }
    CHECK(pairs > 0 && bad == 0 && other == 0,
          "F5: Big's fishing line (njDrawLine3D 0x42, n 11, 12 points) is 11 connected segments (the end of segment i "
          "is the start of segment i + 1) in all %u pairs that draw it (%u breaks; %u segments of non-zero length)",
          (unsigned)pairs, (unsigned)bad, (unsigned)moving);
    sa1_shutdown(ctx);
}

/* lsd-3: Tails' tail attack quads (njDrawPolygon3D(4 points, 0x60)) on Emerald Coast act 1 (DISC) */
static void lsd_tails(const env *E)
{
    sa1_context *ctx = disc_ctx(E, SA1_CHAR_TAILS);
    sa1_character_id id = ctx ? disc_char(ctx, SA1_CHAR_TAILS) : 0;
    sa1_coords co;
    sa1_input in;
    uint32_t pair, quads = 0, bad = 0;
    printf("  lsd-3: Tails' tail attack on Emerald Coast act 1 (DISC):\n");
    if (!id) {
        CHECK(0, "Tails: context / character");
        sa1_shutdown(ctx);
        return;
    }
    memset(&co, 0, sizeof co);
    co.struct_size = sizeof co;
    sa1_get_coords(ctx, &co);
    memset(&in, 0, sizeof in);
    in.struct_size = sizeof in;
    for (pair = 0; pair < 31u; pair++) {
        const sa1_render *r = NULL;
        const sa1_cap_rec *recs = NULL;
        sa1_capture *cap;
        uint32_t nrec, i, pd = 0;
        in.buttons = pair == 15u ? SA1_BUTTON_ACTION : 0u;
        if (sa1_character_tick(ctx, id, &in, 1.0f / 30.0f, NULL, NULL) != SA1_OK ||
            sa1_character_render(ctx, id, 1.0f, &r) != SA1_OK) {
            CHECK(0, "Tails: %s", sa1_last_error(ctx));
            break;
        }
        cap = sa1_character_capture(ctx, id);
        nrec = sa1_capture_records(cap, &recs);
        for (i = 0; i < nrec; i++) {
            const sa1_cap_rec *rc = &recs[i];
            const uint8_t *p;
            const sa1_draw *d;
            double m[12];
            float hp[4][3];
            uint32_t q, t, k2;
            if (rc->kind != SA1_CAP_TRI3D && rc->kind != SA1_CAP_POLY3D) {
                continue;
            }
            p = sa1_capture_payload(cap, rc, 16u);
            if (p == NULL || sa1_rd32(p) < 3u || !(rc->attr & 0x20u)) {
                continue;
            }
            while (pd < r->num_draws && !(r->draws[pd].source == SA1_SOURCE_POLYGON &&
                                          r->draws[pd].prim == SA1_PRIM_TRIANGLES)) {
                pd++;
            }
            if (pd >= r->num_draws) {
                bad++;
                break;
            }
            d = &r->draws[pd++];
            if (rc->kind != SA1_CAP_TRI3D || rc->attr != 0x60u || rc->n != 4u || sa1_rd32(p) < 4u) {
                continue;
            }
            quads++;
            sa1_capture_world_matrix(rc, m);
            for (q = 0; q < 4u; q++) {
                float in3[3];
                in3[0] = sa1_rdf32(p + 16u + 12u * q);
                in3[1] = sa1_rdf32(p + 16u + 12u * q + 4u);
                in3[2] = sa1_rdf32(p + 16u + 12u * q + 8u);
                lsd_host_point(&co, m, in3, hp[q]);
            }
            if (d->count != 2u) {
                bad++;
                continue;
            }
            /* both triangles hold points 1 and 3 (the diagonal); the first point 0, the second point 2 */
            for (t = 0; t < 2u; t++) {
                int has[4] = { 0, 0, 0, 0 };
                for (k2 = 0; k2 < 3u; k2++) {
                    const float *c = r->positions + 9u * (d->first + t) + 3u * k2;
                    for (q = 0; q < 4u; q++) {
                        has[q] |= lsd_near(c, hp[q], 1e-3f);
                    }
                }
                bad += !(has[1] && has[3] && has[t == 0 ? 0 : 2]);
            }
        }
    }
    CHECK(quads > 0 && bad == 0,
          "F4: Tails' tail attack quads (njDrawPolygon3D 0x60, 4 points) are 2 triangles split along the diagonal 1-3 "
          "(the SDK's zig-zag 0, 3, 1, 2; the old fan split along 0-2): %u quads, %u wrong", (unsigned)quads,
          (unsigned)bad);
    sa1_shutdown(ctx);
}

static void test_lsd(const env *E)
{
    printf("lsd. special-move visuals (round 4c: the capture emitter's sprites, Ninja action draws, materials beyond "
           "nbMat, 3D polygons and lines):\n");
    lsd_sonic(E);
    lsd_big(E);
    lsd_tails(E);
}

/* ---- alpha. the character renderer's vertex alpha (round 0.9.1, sadc docs/research/gsa1_070_translucency.md T4 and its
 * Review R2.7 / R3.7) ---------------------------------------------------------------------------------------------- */
/* the vertex alpha of a one-triangle model drawn by the hand-written renderer (a SA1_CAP_RENDER record): diffuse alpha
 * `byte`, control-3D `ctl3d` (0x20 offset material, 0x10 constant material), constant material alpha c, attribute attr */
static uint8_t alpha_hand(sa1_context *ctx, uint32_t byte, uint32_t ctl3d, float c, uint32_t attr)
{
    static const float pts[9] = { 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f };
    static const uint16_t idx[3] = { 0, 1, 2 };
    static const double ident[12] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };
    sa1_cap_rec rc;
    sa1_draw_state ds;
    sa1_emit_info ei;
    sa1_meshbuf mb;
    uint8_t a = 0xEE;
    memset(g_lsd_fake, 0, sizeof g_lsd_fake);
    lsd_put32(0x00, LSD_FAKE + 0x30u);  /* points */
    lsd_put32(0x08, 3u);                /* nbPoint */
    lsd_put32(0x0C, LSD_FAKE + 0x60u);  /* meshsets */
    lsd_put32(0x10, LSD_FAKE + 0x90u);  /* mats */
    lsd_put32(0x14, 1u | (1u << 16));   /* nbMeshset 1, nbMat 1 */
    memcpy(g_lsd_fake + 0x30, pts, sizeof pts);
    lsd_put32(0x60, 0u | (1u << 16));   /* triangles, material 0; one mesh */
    lsd_put32(0x64, LSD_FAKE + 0x80u);
    memcpy(g_lsd_fake + 0x80, idx, sizeof idx);
    lsd_put32(0x90, (byte << 24) | 0x00808080u);  /* diffuse: its RGB is not used by this renderer */
    lsd_put32(0xA0, attr);
    memset(&rc, 0, sizeof rc);
    rc.kind = SA1_CAP_RENDER;
    rc.ctl3d = ctl3d;
    rc.attr_and = 0xFFFFFFFFu;
    memcpy(&rc.cmat[0], &c, 4);
    sa1_capture_draw_state(&rc, &ds);
    memset(&ei, 0, sizeof ei);
    ei.reader = lsd_fake_read;
    ei.ds = &ds;
    ei.source = (uint8_t)SA1_SOURCE_CHARACTER;
    memset(&mb, 0, sizeof mb);
    if (sa1_pose_emit_model_ex(ctx, &mb, &ei, LSD_FAKE, ident) == SA1_OK && mb.nt == 1u && ds.hand) {
        a = mb.col[3];
    }
    sa1_meshbuf_free(ctx, &mb);
    return a;
}

/* the distinct vertex alphas (a 256-bit set) of the render's triangle draws with (flags & mask) == want; *n draws */
static void alpha_set(const sa1_render *r, uint32_t mask, uint32_t want, uint32_t set[8], uint32_t *n)
{
    uint32_t d, t, k;
    for (d = 0; d < r->num_draws; d++) {
        const sa1_draw *dr = &r->draws[d];
        if (dr->prim != SA1_PRIM_TRIANGLES || (dr->flags & mask) != want) {
            continue;
        }
        (*n)++;
        for (t = dr->first; t < dr->first + dr->count && t < r->num_triangles; t++) {
            for (k = 0; k < 3; k++) {
                uint8_t a = r->colors[12u * t + 4u * k + 3u];
                set[a >> 5] |= 1u << (a & 31u);
            }
        }
    }
}

static int set_is(const uint32_t set[8], const uint8_t *vals, uint32_t nv, char *text, size_t cap)
{
    uint32_t want[8], i;
    size_t len = 0;
    memset(want, 0, sizeof want);
    for (i = 0; i < nv; i++) {
        want[vals[i] >> 5] |= 1u << (vals[i] & 31u);
    }
    text[0] = 0;
    for (i = 0; i < 256u && len + 8 < cap; i++) {
        if (set[i >> 5] & (1u << (i & 31u))) {
            len += (size_t)snprintf(text + len, cap - len, "%s%u", len ? "," : "", (unsigned)i);
        }
    }
    return memcmp(set, want, sizeof want) == 0;
}

/* the AFTERIMAGE alphas of a character running `script` ("<n><cmd>" as run_ticks), rendered at alpha 1 after every tick */
static void afterimage_alphas(const env *E, int32_t kind, const char *script, uint32_t set[8], uint32_t *draws)
{
    sa1_context *ctx = disc_ctx(E, kind);
    sa1_character_id id = ctx ? disc_char(ctx, kind) : 0;
    const char *p = script;
    memset(set, 0, 8u * sizeof(uint32_t));
    *draws = 0;
    while (id && *p) {
        char *e, one[4];
        long n = strtol(p, &e, 10), i;
        if (e == p || !*e) {
            break;
        }
        one[0] = '1';
        one[1] = *e;
        one[2] = 0;
        p = e + 1;
        for (i = 0; i < n; i++) {
            const sa1_render *r = NULL;
            if (run_ticks(ctx, id, one) != 0 || sa1_character_render(ctx, id, 1.0f, &r) != SA1_OK) {
                break;
            }
            alpha_set(r, SA1_DRAW_AFTERIMAGE, SA1_DRAW_AFTERIMAGE, set, draws);
        }
    }
    sa1_shutdown(ctx);
}

static void test_alpha(const env *E)
{
    static const struct {
        uint32_t byte, ctl3d;
        float c;
        uint8_t want, old;
        const char *what;
    } syn[] = {
        { 255, 0x20u, -0.7f, 76, 77, "Amy's first hammer ghost (-0.5 - 0.1 x 2): 0.30000001 x 255 = 76.5 truncated" },
        { 255, 0x20u, -0.8f, 51, 51, "Amy's second ghost" },
        { 255, 0x20u, -1.0f / 3.0f, 170, 170, "Tails' first tail copy" },
        { 255, 0x20u, -2.0f / 3.0f, 85, 85, "Tails' second tail copy" },
        { 216, 0x00u, 0.0f, 216, 216, "no constant material (E-102's etx_right3): the byte" },
        { 255, 0x20u, -0.19999999f, 204, 204, "Sonic's landing shell 8C0B4436 (-0.19999999 x wtimer), wtimer 1" },
        { 255, 0x20u, -0.39999998f, 153, 153, "the shell, wtimer 2" },
        { 255, 0x20u, -0.59999996f, 102, 102, "the shell, wtimer 3" },
        { 255, 0x20u, -0.79999995f, 51, 51, "the shell, wtimer 4" },
        { 255, 0x20u, -0.99999994f, 0, 0, "the shell, wtimer 5: 0.00007, truncated" },
        { 255, 0x20u, -1.0f, 0, 0, "an offset of -1: clamped to 0" },
        { 128, 0x20u, 0.5f, 255, 255, "an offset beyond 1: clamped to 1" },
        { 100, 0x20u, -0.2f, 49, 49, "a byte below 255 with an offset" },
        { 255, 0x10u, 0.5f, 127, 128, "a constant material of 0.5: 127.5 truncated" },
        { 255, 0x10u, 1.25f, 62, 255, "a constant material of 1.25, not clamped: FTRC 318, its low byte" },
        { 255, 0x10u, -0.5f, 129, 0, "a constant material of -0.5, not clamped: FTRC -127, its low byte" },
    };
    sa1_context *ctx;
    sa1_character_id id;
    uint32_t i, bad = 0, changed = 0, set[8], draws = 0;
    char text[256];
    printf("alpha. the character renderer's vertex alpha (8C037872: x 1/255, constant material, x 255, FTRC; "
           "gsa1_070_translucency.md T4):\n");
    ctx = disc_ctx(E, SA1_CHAR_SONIC);
    if (ctx == NULL) {
        CHECK(0, "alpha: context");
        return;
    }
    for (i = 0; i < sizeof syn / sizeof syn[0]; i++) {
        uint8_t lit = alpha_hand(ctx, syn[i].byte, syn[i].ctl3d, syn[i].c, SA1_MAT_USE_ALPHA);
        uint8_t unlit = alpha_hand(ctx, syn[i].byte, syn[i].ctl3d, syn[i].c, SA1_MAT_USE_ALPHA | SA1_MAT_IGNORE_LIGHT);
        if (lit != syn[i].want || unlit != syn[i].want) {
            bad++;
            printf("    byte %u, control-3D %02X, constant alpha %.8f (%s): %u lit / %u IGNORE_LIGHT, expected %u\n",
                   (unsigned)syn[i].byte, (unsigned)syn[i].ctl3d, (double)syn[i].c, syn[i].what, (unsigned)lit,
                   (unsigned)unlit, (unsigned)syn[i].want);
        }
        changed += syn[i].want != syn[i].old;
    }
    CHECK(bad == 0, "synthetic records of the hand-written renderer, both its lit and its IGNORE_LIGHT branch: %u cases "
          "as the game's single-precision arithmetic (rounding toward zero) and FTRC give them, %u of them different from "
          "libsa1's rounding before round 0.9.1 (Amy's first ghost 76, not 77; a constant material of 0.5 127, not 128; "
          "constant materials outside 0..1 wrap like the game's byte store)", (unsigned)(sizeof syn / sizeof syn[0]),
          (unsigned)changed);
    sa1_shutdown(ctx);
    {
        static const uint8_t amy[2] = { 76, 51 };
        afterimage_alphas(E, SA1_CHAR_AMY, "10n1a20n", set, &draws);
        CHECK(draws > 0 && set_is(set, amy, 2, text, sizeof text),
              "Amy's hammer ghosts (10n1a20n): %u AFTERIMAGE draws, vertex alphas {%s} (the Dreamcast: 76, 51)",
              (unsigned)draws, text);
    }
    {
        static const uint8_t tails[2] = { 170, 85 };
        afterimage_alphas(E, SA1_CHAR_TAILS, "20n", set, &draws);
        CHECK(draws > 0 && set_is(set, tails, 2, text, sizeof text),
              "Tails' tail-blur copies (standing): %u AFTERIMAGE draws, vertex alphas {%s} (170, 85)", (unsigned)draws,
              text);
    }
    {
        /* every character-renderer draw of E-102 without a fading material has its material's diffuse alpha */
        const sa1_render *r = NULL;
        uint32_t d, t, k, n = 0, off = 0, right3 = 0;
        int32_t tex = -1;
        ctx = disc_ctx(E, SA1_CHAR_GAMMA);
        id = ctx ? disc_char(ctx, SA1_CHAR_GAMMA) : 0;
        if (id && run_ticks(ctx, id, "20n") == 0 && sa1_character_render(ctx, id, 1.0f, &r) == SA1_OK) {
            tex = tex_named(ctx, SA1_CHAR_GAMMA, 0, "etx_right3");
            for (d = 0; d < r->num_draws; d++) {
                const sa1_draw *dr = &r->draws[d];
                if (dr->prim != SA1_PRIM_TRIANGLES || dr->source != SA1_SOURCE_CHARACTER ||
                    (dr->flags & (SA1_DRAW_AFTERIMAGE | SA1_DRAW_SHADOW))) {
                    continue;
                }
                n++;
                for (t = dr->first; t < dr->first + dr->count && t < r->num_triangles; t++) {
                    for (k = 0; k < 3; k++) {
                        uint8_t a = r->colors[12u * t + 4u * k + 3u];
                        off += a != dr->diffuse[3];
                        right3 += dr->archive == 0 && dr->texture == tex && a == 216;
                    }
                }
            }
        }
        CHECK(n > 0 && off == 0 && tex >= 0 && right3 > 0,
              "E-102 standing: %u character-renderer draws without a fading material, every corner with its material's "
              "diffuse alpha (%u differ); etx_right3 %u corners at 216 (unchanged)", (unsigned)n, (unsigned)off,
              (unsigned)right3);
        sa1_shutdown(ctx);
    }
}

/* ---- powerup. the item box power-up models (round 0.9.2a FX3: sadc docs/research/gsa1_091b_itembox.md 6 "FX3" with its
 * Review R.4.4; rt_draw.c section 5b: 8C621574, a "void" boundary stub before) ------------------------------------- */
#define PU_FN_SPAWN_AIRBOX 0x8C05D9C0u  /* void (NJS_POINT3 *pos, Sint32 item): the air box spawner (Knuckles' dig 8C0C8534) */
#define PU_TL_MAG_QUAD     0x8C19B5E8u  /* D_8C19B5E8[n]: the magnetic barrier spark quad's texlists (8C043E70) */
#define PU_NMODELS         4u

typedef struct pu_model {
    uint32_t model;                     /* the NJS_MODEL the power-up display draws with 8C621574 */
    uint32_t tl, ntl;                   /* its texlists: tl + 8 * n, n < ntl (NJS_TEXLIST is 8 bytes) */
    const char *name;
} pu_model;

/* sadc code_8C043C00.c / code_8C044480.c */
static const pu_model k_pu_models[PU_NMODELS] = {
    { 0x8C1A1264u, 0x8C19B708u, 1u, "invincibility model D_8C1A1264 (8C044290)" },
    { 0x8C1A0910u, 0x8C19B6C8u, 8u, "invincibility spark D_8C1A0910 (8C0444C4, texlist (btimer / 2) & 7)" },
    { 0x8C1A07F8u, 0x8C19B650u, 15u, "barrier spark D_8C1A07F8 (8C044166, texlist btimer / 2)" },
    { 0x8C1A0388u, 0x8C19B630u, 3u, "magnetic barrier spark D_8C1A0388 (8C043E70, texlist rand() * 3 / 32768)" },
};

typedef struct pu_item {
    int32_t item;                       /* the air box's item (D_8C1B9BA0[item]) */
    const char *name, *trace;           /* the harness reference of that box (092-harness, `--create-task .../SX`) */
    uint32_t models;                    /* bit k: k_pu_models[k] is drawn */
    uint32_t bit;                       /* the playerwk.item bit it sets */
} pu_item;

static const pu_item k_pu_items[3] = {
    { 5, "barrier", "airbox_5_ec1", 0x4u, 0x0001u },
    { 1, "invincibility", "airbox_1_ec1", 0x3u, 0x8000u },
    { 8, "magnetic barrier", "airbox_8_ec1", 0x8u, 0x0002u },
};

/* what the capture snapshots held of the power-up models */
typedef struct pu_tally {
    uint32_t recs[PU_NMODELS];          /* records of the model */
    uint32_t bad_rec[PU_NMODELS];       /* ... not as 8C621574 makes them (NJMOTION + NJ_ACTION, sub step 0, its texlist) */
    uint32_t snaps[PU_NMODELS];         /* ... with a v3 model snapshot (materials, points, normals) */
    uint32_t pairs[PU_NMODELS];         /* snapshots (live pairs / replay steps) that hold the model */
    uint32_t draws[PU_NMODELS], tris[PU_NMODELS];   /* its draws emitted alone (only_model), with the character's archives */
    uint32_t bad_draw[PU_NMODELS];      /* ... not a textured, unlit-by-palette MODEL effect with the material's blending */
    uint32_t blended[PU_NMODELS], additive[PU_NMODELS];
    uint32_t mat[PU_NMODELS];           /* the material word of the first draw */
    uint8_t col[PU_NMODELS][4];         /* its first corner's colour */
    int32_t arc[PU_NMODELS], tex[PU_NMODELS];
    uint32_t col_bad[PU_NMODELS];       /* records whose triangles do not have 8C6209A8's colour (below) */
    uint32_t fading;                    /* magnetic barrier spark records drawn fading (constant material RGB < 0) */
    uint32_t quads, quads_bad;          /* the magnetic barrier's spark quads (njDrawTriangle3D 0x80000062, D_8C19B5E8[n]) */
    uint32_t pair_tris;                 /* the power-up models' emitted triangles of the last snapshot scanned */
} pu_tally;

/* 8C6209A8's colour channel of a textured material under control-3D 0x200 (RGB 1.0; sadc docs/research/
 * lightspeed_visuals.md Review R.2.1 / R.2.2): the constant material replaces it under 0x10 and is added under 0x20 (the
 * magnetic barrier's fade: 8C043A20(0, f - 1, f - 1, f - 1) before its spark model), scaled to 0..255 and clamped */
static int pu_channel(uint32_t ctl3d, uint32_t cbits)
{
    float c;
    double x;
    memcpy(&c, &cbits, 4);
    x = (ctl3d & 0x10u) ? (double)c * 255.0 : (ctl3d & 0x20u) ? 255.0 + (double)c * 255.0 : 255.0;
    if (!(x > 0.0)) {
        return 0;
    }
    return x >= 255.0 ? 255 : (int)(x + 0.5);
}

static void pu_tally_init(pu_tally *t)
{
    uint32_t k;
    memset(t, 0, sizeof *t);
    for (k = 0; k < PU_NMODELS; k++) {
        t->arc[k] = -1;
        t->tex[k] = -1;
    }
}

/* the power-up models of the capture's snapshot: their records, and their draws emitted alone with the character's
 * archives (the render path's options: textures resolved by sa1_capture_resolve, the classification of the emitter) */
static void pu_scan(sa1_context *ctx, int32_t kind, sa1_capture *cap, uint32_t models, pu_tally *t)
{
    const sa1_cap_rec *recs = NULL;
    const sa1_charset *cs = &ctx->chars[kind];
    uint32_t n = sa1_capture_records(cap, &recs), i, k;
    t->pair_tris = 0;
    for (i = 0; i < n; i++) {
        const sa1_cap_rec *r = &recs[i];
        uint32_t off = (r->texlist & 0x1FFFFFFFu) - (PU_TL_MAG_QUAD & 0x1FFFFFFFu);
        if ((models & 0x8u) && r->kind == SA1_CAP_POLY3D && r->attr == 0x80000062u && off % 8u == 0 && off / 8u < 4u) {
            t->quads++;
            t->quads_bad += r->n != 2u || sa1_capture_payload(cap, r, 16u) == NULL;
        }
    }
    for (k = 0; k < PU_NMODELS; k++) {
        const pu_model *pm = &k_pu_models[k];
        uint32_t nrec = 0, d;
        sa1_capture_emit_opts o;
        sa1_meshbuf mb;
        sa1_geometry g;
        if (!(models & (1u << k))) {
            continue;
        }
        for (i = 0; i < n; i++) {
            const sa1_cap_rec *r = &recs[i];
            sa1_emit_info ei;
            uint32_t off;
            if (!SA1_CAP_IS_MODEL(r->kind) || (r->obj & 0x1FFFFFFFu) != (pm->model & 0x1FFFFFFFu)) {
                continue;
            }
            nrec++;
            off = (r->texlist & 0x1FFFFFFFu) - (pm->tl & 0x1FFFFFFFu);
            t->bad_rec[k] += r->kind != SA1_CAP_NJMOTION || !(r->flags & SA1_CAPF_NJ_ACTION) || r->sub != 0 ||
                             off % 8u != 0 || off / 8u >= pm->ntl;
            memset(&ei, 0, sizeof ei);
            t->snaps[k] += (uint32_t)sa1_capture_model_snapshot(cap, r, &ei);
        }
        t->recs[k] += nrec;
        if (nrec == 0) {
            continue;
        }
        t->pairs[k]++;
        memset(&o, 0, sizeof o);
        o.texlist0 = cs->num_archives ? cs->archives[0].texlist_addr : 0u;
        o.cs = cs;
        o.only_model = pm->model;
        memset(&mb, 0, sizeof mb);
        memset(&g, 0, sizeof g);
        if (sa1_capture_emit_into(ctx, cap, &o, &mb, &g) != SA1_OK || mb.ndraw == 0) {
            t->bad_draw[k]++;
            sa1_meshbuf_free(ctx, &mb);
            continue;
        }
        for (d = 0; d < mb.ndraw; d++) {
            const sa1_draw *dr = &mb.draws[d];
            uint32_t a = dr->material, bs = (a >> 29) & 7u, bd = (a >> 26) & 7u;
            int ok = dr->prim == SA1_PRIM_TRIANGLES && dr->source == SA1_SOURCE_MODEL && (dr->flags & SA1_DRAW_EFFECT) &&
                     !(dr->flags & SA1_DRAW_PALETTE_LIT) && dr->archive >= 0 && dr->texture >= 0 &&
                     !(dr->flags & SA1_DRAW_NO_TEXTURE) && dr->count > 0;
            /* the material's own blending (Ninja's model draw 8C6209A8: NJD_SA / NJD_DA of a USE_ALPHA material; the
               opaque list without it) */
            if (a & SA1_MAT_USE_ALPHA) {
                ok &= dr->blend_src == bs && dr->blend_dst == bd;
            } else {
                ok &= dr->blend_src == SA1_BLEND_ONE && dr->blend_dst == SA1_BLEND_ZERO && !(dr->flags & SA1_DRAW_BLEND);
            }
            if (t->draws[k] == 0 && d == 0) {
                t->mat[k] = a;
                t->arc[k] = dr->archive;
                t->tex[k] = dr->texture;
                memcpy(t->col[k], mb.col + 12u * dr->first, 4);
            }
            if (a & SA1_MAT_DOUBLE_SIDE) {
                ok &= (dr->flags & SA1_DRAW_DOUBLE_SIDED) != 0;
            }
            t->bad_draw[k] += !ok;
            t->blended[k] += (dr->flags & SA1_DRAW_BLEND) != 0;
            t->additive[k] += (dr->flags & SA1_DRAW_ADDITIVE) != 0;
            t->tris[k] += dr->count;
            t->pair_tris += dr->count;
        }
        t->draws[k] += mb.ndraw;
        /* the colours, record by record: the emitter emits the records in order, each the same model (the same triangle
           count); corner 0 of a record's first triangle against the record's control-3D flags and constant material */
        if (mb.nt % nrec != 0 || mb.nt == 0) {
            t->col_bad[k] += nrec;
        } else {
            uint32_t tpr = mb.nt / nrec, j = 0, ch;
            for (i = 0; i < n; i++) {
                const sa1_cap_rec *r = &recs[i];
                const uint8_t *c0;
                int bad = 0;
                float cr;
                if (!SA1_CAP_IS_MODEL(r->kind) || (r->obj & 0x1FFFFFFFu) != (pm->model & 0x1FFFFFFFu)) {
                    continue;
                }
                c0 = mb.col + 12u * (j++ * tpr);
                bad = !(r->ctl3d & 0x200u);
                for (ch = 0; ch < 3; ch++) {
                    bad |= c0[ch] != pu_channel(r->ctl3d, r->cmat[1 + ch]);
                }
                t->col_bad[k] += bad != 0;
                memcpy(&cr, &r->cmat[1], 4);
                t->fading += k == 3u && (r->ctl3d & 0x20u) && cr < 0.0f && c0[0] < 255;
            }
        }
        sa1_meshbuf_free(ctx, &mb);
    }
}

static const char *pu_tex_name(const sa1_context *ctx, int32_t kind, int32_t arc, int32_t tex)
{
    sa1_texture_info ti;
    memset(&ti, 0, sizeof ti);
    ti.struct_size = sizeof ti;
    if (arc < 0 || tex < 0 || sa1_texture_get(ctx, kind, (uint32_t)arc, (uint32_t)tex, &ti) != SA1_OK || ti.name == NULL) {
        return "-";
    }
    return ti.name;
}

static void pu_report(const sa1_context *ctx, int32_t kind, const char *who, const pu_item *it, const pu_tally *t)
{
    uint32_t k;
    for (k = 0; k < PU_NMODELS; k++) {
        const char *an = "-";
        if (!(it->models & (1u << k))) {
            continue;
        }
        if (t->arc[k] >= 0) {
            sa1_texture_archive(ctx, kind, (uint32_t)t->arc[k], &an, NULL);
        }
        printf("      %s, %s: %s: %u records in %u snapshots (%u not as 8C621574's, %u with a model snapshot), %u draws / "
               "%u triangles (%u wrong, %u blended, %u additive; %u records with another colour than 8C6209A8's); "
               "material 0x%08X, texture %s:%s, first corner (%u, %u, %u, %u)\n", who, it->name, k_pu_models[k].name,
               (unsigned)t->recs[k], (unsigned)t->pairs[k], (unsigned)t->bad_rec[k], (unsigned)t->snaps[k],
               (unsigned)t->draws[k], (unsigned)t->tris[k], (unsigned)t->bad_draw[k], (unsigned)t->blended[k],
               (unsigned)t->additive[k], (unsigned)t->col_bad[k], (unsigned)t->mat[k],
               an ? an : "-", pu_tex_name(ctx, kind, t->arc[k], t->tex[k]), t->col[k][0], t->col[k][1], t->col[k][2],
               t->col[k][3]);
    }
    if (it->models & 0x8u) {
        printf("      %s, %s: %u spark quads (%u wrong); %u spark models drawn fading\n", who, it->name,
               (unsigned)t->quads, (unsigned)t->quads_bad, (unsigned)t->fading);
    }
}

/* every model of the item recorded and emitted as the game draws it */
static int pu_ok(const pu_item *it, const pu_tally *t)
{
    uint32_t k;
    int ok = 1;
    for (k = 0; k < PU_NMODELS; k++) {
        if (!(it->models & (1u << k))) {
            continue;
        }
        /* the materials (0x8671A400, 0x86B1A400): NJD_SA_SRC_ALPHA / NJD_DA_ONE, USE_ALPHA, double-sided: additive */
        ok &= t->recs[k] > 0 && t->bad_rec[k] == 0 && t->snaps[k] == t->recs[k] && t->draws[k] > 0 && t->tris[k] > 0 &&
              t->bad_draw[k] == 0 && t->col_bad[k] == 0 && t->blended[k] == t->draws[k] && t->additive[k] == t->draws[k];
    }
    if (it->models & 0x8u) {
        ok &= t->quads > 0 && t->quads_bad == 0 && t->fading > 0;
    }
    return ok;
}

/* powerup-1: the harness references of the three boxes (recorded on the original game code, whose 8C621574 draws):
 * replayed without and with the capture window, the window must change nothing and record the models */
typedef struct pu_step {
    sa1_context *ctx;
    int32_t kind;
    sa1_capture *cap;
    const pu_item *it;
    pu_tally t;
    uint32_t first;                     /* the first step whose snapshot holds a power-up model (+1; 0: none) */
} pu_step;

static void pu_step_cb(void *user, uint32_t i, sa1g_world *w)
{
    pu_step *s = (pu_step *)user;
    uint32_t before = 0, k;
    (void)w;
    for (k = 0; k < PU_NMODELS; k++) {
        before += s->t.recs[k];
    }
    pu_scan(s->ctx, s->kind, s->cap, s->it->models, &s->t);
    for (k = 0; k < PU_NMODELS; k++) {
        before -= s->t.recs[k];
    }
    if (before != 0 && s->first == 0) {
        s->first = i + 1u;
    }
}

static sa1_status pu_create_at(sa1_context *ctx, const sa1_trace_header *h, sa1_character_id *id)
{
    sa1_character_desc d;
    sa1_status st;
    int k;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.kind = h->chr;
    /* as tests/replay_test.c replays the references: the stage's own tasks in the world */
    d.flags = SA1_CREATE_WORLD | SA1_CREATE_SA1_SPACE | SA1_CREATE_ANGLES | SA1_CREATE_STAGE | SA1_CREATE_STAGE_TASKS;
    memcpy(d.position, h->start_pos, sizeof d.position);
    for (k = 0; k < 3; k++) {
        d.angle[k] = (int32_t)h->start_ang[k];
    }
    d.level = h->level;
    d.act = h->act;
    st = sa1_character_create_ex(ctx, &d, id);
    if (!SA1_FAILED(st)) {
        wr_screen_of(sa1_character_world(ctx, *id), h);     /* the screen, and the sidecar's tasks: the box */
    }
    return st;
}

static void pu_replay(const env *E, const pu_item *it)
{
    wr_trace t;
    char prefix[1024];
    sa1ca_dump dump;
    sa1_context *ctx;
    sa1_character_id id0 = 0, id1 = 0;
    sa1g_world *w0, *w1;
    sa1_trace_rec *r0 = NULL, *r1 = NULL;
    ev_log *e0 = NULL, *e1 = NULL;
    pu_step s;
    wr_diff d;
    int rc0 = -1, rc1 = -1;
    uint32_t i, bad0 = 0, bad1 = 0;
    if (load_trace(E, it->trace, &t) != 0) {
        printf("  skip  %s: no reference trace (092-harness)\n", it->trace);
        g_skip++;
        return;
    }
    if (land_prefix(E, &t.h, prefix, sizeof prefix) != 0) {
        printf("  skip  %s: no landtable dump %s\n", it->trace, prefix);
        g_skip++;
        wr_trace_free(&t);
        return;
    }
    ctx = make_ctx(E, 1u << t.h.chr, prefix, &dump);
    memset(&s, 0, sizeof s);
    pu_tally_init(&s.t);
    r0 = (sa1_trace_rec *)calloc(t.n + 1u, sizeof *r0);
    r1 = (sa1_trace_rec *)calloc(t.n + 1u, sizeof *r1);
    e0 = (ev_log *)calloc(1, sizeof *e0);
    e1 = (ev_log *)calloc(1, sizeof *e1);
    if (ctx == NULL || r0 == NULL || r1 == NULL || e0 == NULL || e1 == NULL) {
        CHECK(0, "%s: context", it->trace);
        goto done;
    }
    /* one world after the other: each creation starts the sidecar's run (the box task) on its world */
    if (pu_create_at(ctx, &t.h, &id0) != SA1_OK || (w0 = sa1_character_world(ctx, id0)) == NULL) {
        CHECK(0, "%s: character: %s", it->trace, sa1_last_error(ctx));
        goto done;
    }
    rc0 = replay_run(w0, &t, 0, t.n, NULL, r0, NULL, NULL);
    bad0 = wr_run.bad;
    ev_drain(ctx, e0);
    if (pu_create_at(ctx, &t.h, &id1) != SA1_OK || (w1 = sa1_character_world(ctx, id1)) == NULL) {
        CHECK(0, "%s: character: %s", it->trace, sa1_last_error(ctx));
        goto done;
    }
    s.ctx = ctx;
    s.kind = t.h.chr;
    s.it = it;
    if (sa1_capture_attach(ctx, w1, &s.cap) != SA1_OK) {
        CHECK(0, "%s: the world's capture block", it->trace);
        goto done;
    }
    sa1_capture_set_archives(s.cap, &ctx->chars[t.h.chr]);
    rc1 = replay_run(w1, &t, 0, t.n, s.cap, r1, pu_step_cb, &s);
    bad1 = wr_run.bad;
    ev_drain(ctx, e1);
    wr_diff_init(&d);
    for (i = 0; i < t.n; i++) {
        wr_compare(&r0[i], &r1[i], WR_CMP_RAW, &d, (int)i);
    }
    CHECK(rc0 == 0 && rc1 == 0 && d.nsteps_bad == 0 && events_equal(e0, e1) && bad0 == 0 && bad1 == 0,
          "%s (%u steps, %s): with the capture window open every record is identical (%u records differ), %u events "
          "identical; the host-object sidecar (the box task) agrees with the reference's in both runs (%u / %u records "
          "differ)", it->trace, (unsigned)t.n, it->name, (unsigned)d.nsteps_bad, (unsigned)e1->n, (unsigned)bad0,
          (unsigned)bad1);
    if (d.nsteps_bad) {
        wr_diff_print(&d, 12);
    }
    pu_report(ctx, t.h.chr, sa1_character_name(t.h.chr), it, &s.t);
    CHECK(pu_ok(it, &s.t),
          "%s: the %s's models (first at step %d) are recorded by 8C621574 (RT_CAP_NJMOTION with SA1_CAPF_NJ_ACTION, sub "
          "step 0, the display's texlist, a model snapshot) and emitted as the game draws them: textured (the "
          "character's archives: EFF_REGULAR), SA1_DRAW_EFFECT, source MODEL, not palette-lit, the material's blending "
          "(additive SRC_ALPHA / ONE, double-sided), 8C6209A8's colours (RGB 1.0 with the constant material)%s",
          it->trace, it->name, s.first ? (int)(s.first - 1u) : -1,
          (it->models & 0x8u) ? "; the sparks fade (8C043A20's offset) and their quads are drawn too (njDrawTriangle3D "
                                "0x80000062)" : "");
done:
    if (s.cap != NULL) {
        sa1_capture_free(ctx, s.cap);
    }
    free(r0);
    free(r1);
    free(e0);
    free(e1);
    if (ctx != NULL) {
        if (id0) {
            sa1_character_destroy(ctx, id0);
        }
        if (id1) {
            sa1_character_destroy(ctx, id1);
        }
        free_ctx(ctx, &dump);
    }
    wr_trace_free(&t);
}

/* powerup-2: live (sa1_character_tick / sa1_character_render, what G-SA1 draws): an air box made by the game's spawner at
 * the character on Emerald Coast act 1 (DISC), as Knuckles' dig makes it */
static void pu_spawn_cb(sa1g_world *w, void *user)
{
    int32_t item = *(const int32_t *)user;
    sa1g_player_ptrs pp;
    uint8_t *twp;
    sa1g_world_player(w, &pp);
    twp = pp.twp ? (uint8_t *)sa1g_world_ptr(w, pp.twp) : NULL;
    if (twp != NULL) {
        ((void (*)(void *, int32_t))sa1_fn(SA1_GFN_C(PU_FN_SPAWN_AIRBOX), 0u))(twp + 0x20, item);
    }
}

/* the render's model effects: triangles of MODEL-source effect draws (the blob shadow aside: SA1_DRAW_SHADOW), and those
 * not textured or palette-lit */
static uint32_t pu_render_tris(const sa1_render *r, uint32_t *bad)
{
    uint32_t i, n = 0;
    *bad = 0;
    for (i = 0; i < r->num_draws; i++) {
        const sa1_draw *d = &r->draws[i];
        if (d->prim != SA1_PRIM_TRIANGLES || d->source != SA1_SOURCE_MODEL || (d->flags & SA1_DRAW_SHADOW)) {
            continue;
        }
        n += d->count;
        *bad += !(d->flags & SA1_DRAW_EFFECT) || (d->flags & (SA1_DRAW_PALETTE_LIT | SA1_DRAW_NO_TEXTURE)) ||
                d->archive < 0 || d->texture < 0;
    }
    return n;
}

static void pu_live(const env *E, int32_t kind)
{
    sa1_context *ctx = disc_ctx(E, kind);
    uint32_t c;
    if (ctx == NULL) {
        CHECK(0, "%s: context", sa1_character_name(kind));
        return;
    }
    for (c = 0; c < 3; c++) {
        const pu_item *it = &k_pu_items[c];
        sa1_character_id id = disc_char(ctx, kind);
        sa1g_world *w = id ? sa1_character_world(ctx, id) : NULL;
        sa1_input in;
        pu_tally t;
        sa1_status st = SA1_ERR_INVALID_ARGUMENT;
        uint32_t pair, base_tris = 0, base_bad = 0, item_pair = 0, drawn_pairs = 0, match_bad = 0, render_bad = 0;
        uint32_t item_bits = 0, half_bad = 0;
        pu_tally_init(&t);
        if (w == NULL) {
            CHECK(0, "%s: character", sa1_character_name(kind));
            continue;
        }
        memset(&in, 0, sizeof in);
        in.struct_size = sizeof in;
        for (pair = 0; pair < 20u; pair++) {
            const sa1_render *r = NULL;
            st = sa1_character_tick(ctx, id, &in, 1.0f / 30.0f, NULL, NULL);
            if (st == SA1_OK && sa1_character_render(ctx, id, 1.0f, &r) == SA1_OK && r != NULL) {
                uint32_t b = 0;
                base_tris += pu_render_tris(r, &b);
                base_bad += b;
            }
        }
        if (st == SA1_OK) {
            int32_t item = it->item;
            st = sa1g_world_call(w, pu_spawn_cb, &item);
        }
        for (pair = 1; pair <= 90u && st == SA1_OK; pair++) {
            const sa1_render *r = NULL;
            sa1_state_ex x;
            uint32_t k, have = 0, rt, rb = 0;
            st = sa1_character_tick(ctx, id, &in, 1.0f / 30.0f, NULL, NULL);
            if (st != SA1_OK || sa1_character_render(ctx, id, 1.0f, &r) != SA1_OK || r == NULL) {
                printf("    pair %u: %s: %s\n", (unsigned)pair, sa1_status_string(st), sa1_last_error(ctx));
                st = SA1_ERR_INVALID_ARGUMENT;
                break;
            }
            memset(&x, 0, sizeof x);
            x.struct_size = sizeof x;
            sa1_character_state_ex(ctx, id, &x);
            if ((x.item & it->bit) && item_pair == 0) {
                item_pair = pair;
                item_bits = x.item;
            }
            for (k = 0; k < PU_NMODELS; k++) {
                have += t.recs[k];
            }
            pu_scan(ctx, kind, sa1_character_capture(ctx, id), it->models, &t);
            for (k = 0; k < PU_NMODELS; k++) {
                have -= t.recs[k];
            }
            rt = pu_render_tris(r, &rb);
            render_bad += rb;
            if (have != 0) {
                /* the render output holds exactly the power-up models' triangles as their only draws of a model */
                drawn_pairs++;
                match_bad += rt != t.pair_tris;
            }
            /* between the last two pairs (what G-SA1 draws every frame; sparks come and go from pair to pair): the same
               model draws, interpolated or moved with the root */
            {
                const sa1_render *h = NULL;
                uint32_t hb = 0;
                if (sa1_character_render(ctx, id, 0.5f, &h) != SA1_OK || h == NULL || pu_render_tris(h, &hb) != rt || hb) {
                    half_bad++;
                }
            }
        }
        pu_report(ctx, kind, sa1_character_name(kind), it, &t);
        CHECK(st == SA1_OK && item_pair != 0 && pu_ok(it, &t) && drawn_pairs > 10u && match_bad == 0 && render_bad == 0 &&
              base_tris == 0 && base_bad == 0 && half_bad == 0,
              "%s, live, %s (box item %d): item 0x%04X from pair %u; the models drawn in %u pairs, each one in "
              "sa1_character_render exactly as emitted alone (%u pairs differ; %u untextured / palette-lit / non-effect "
              "MODEL draws), and at alpha 0.5 the same model triangles (%u pairs differ); before the box no MODEL draw "
              "(%u triangles)", sa1_character_name(kind), it->name, (int)it->item, (unsigned)item_bits,
              (unsigned)item_pair, (unsigned)drawn_pairs, (unsigned)match_bad, (unsigned)render_bad, (unsigned)half_bad,
              (unsigned)base_tris);
        sa1_character_destroy(ctx, id);
    }
    sa1_shutdown(ctx);
}

static void test_powerup(const env *E)
{
    uint32_t c;
    printf("powerup. the item box power-up models (round 0.9.2a FX3: the Ninja node draw 8C621574, rt_draw.c 5b):\n");
    for (c = 0; c < 3; c++) {
        pu_replay(E, &k_pu_items[c]);
    }
    pu_live(E, SA1_CHAR_SONIC);
    pu_live(E, SA1_CHAR_KNUCKLES);
}

/* ==== round 0.9.4 (094-lib-render): who drew (capture version 5) and the game OBJECT draws ==============================
 * sadc docs/research/gsa1_094_plan.md 6.2 tests (6), (10), (11), (12) with its Review R.1 / R.2; gsa1_090_rings.md Review
 * R.4.9. A flat GRID floor in host SOURCE coordinates (4 HU per SA1 unit, z up); objects are made outside the steps the way
 * the SET spawner 8C091380 makes them (CreateElementalTask with the list entry's exec), the ground box with an
 * OBJ_CONDITION (its mode 0 reads tp->ocp unconditionally: gsa1_094_plan.md 2.3 fact 12). */
#define OB_EXEC_SONIC   0x8C0A77BCu     /* SonicMain */
#define OB_EXEC_RING    0x8C0869E8u     /* objexec_RING */
#define OB_EXEC_GRING   0x8C087B80u     /* objexec_O_GRING */
#define OB_EXEC_DROP    0x8C0870EAu     /* the dropped / dug ring */
#define OB_EXEC_GBOX    0x8C05D10Cu     /* objexec_O_ITEMBOX */
#define OB_EXEC_ABOX    0x8C05D9F6u     /* objexec_O_ItemBoxAir */
#define OB_FN_CREATE    0x8C09852Eu     /* CreateElementalTask(im, level, exec) */
#define OB_FN_DROPSPAWN 0x8C087A9Eu     /* func_8C087A9E(n, x, y, z) */
#define OB_COND         0x8C79A3E0u     /* OBJ_CONDITION[]: 16 bytes each */
#define OB_MODEL_RING   0x8C1A21DCu     /* the ring model */
#define OB_TL_SPARKLE   0x8C1BF014u     /* the sparkle's texlist */
#define OB_PHYS(a)      ((a) & 0x1FFFFFFFu)
static const uint32_t ob_gbox_models[4] = { 0x8C1AC944u, 0x8C1AC1A8u, 0x8C1ABD30u, 0x8C1AB994u };

static sa1_context *ob_ctx(const env *E)
{
    sa1_context *ctx = NULL;
    sa1_host h;
    sa1_load_options lo;
    sa1_coords co;
    sa1_collision_tri *t;
    uint32_t n = 0;
    float x, y;
    sa1_host_defaults(&h);
    h.log = log_cb;
    h.log_level = SA1_LOG_WARN;
    if (sa1_init(&h, &ctx) != SA1_OK) {
        return NULL;
    }
    memset(&co, 0, sizeof co);
    co.struct_size = sizeof co;
    sa1_coords_preset(SA1_COORDS_PRESET_SOURCE, &co);
    sa1_set_coords(ctx, &co);
    memset(&lo, 0, sizeof lo);
    lo.struct_size = sizeof lo;
    lo.characters = 1u << SA1_CHAR_SONIC;
    lo.flags = SA1_LOAD_DEFAULT | SA1_LOAD_WORLD_IMAGES;
    t = (sa1_collision_tri *)calloc(2u * 32u * 32u, sizeof *t);
    if (t == NULL || SA1_FAILED(sa1_load_disc(ctx, E->gdi, &lo))) {
        printf("    context: %s\n", sa1_last_error(ctx));
        free(t);
        sa1_shutdown(ctx);
        return NULL;
    }
    for (x = -16384.0f; x < 16384.0f; x += 1024.0f) {
        for (y = -16384.0f; y < 16384.0f; y += 1024.0f) {
            float a[3] = { x, y, 0 }, b[3] = { x + 1024.0f, y, 0 }, c[3] = { x + 1024.0f, y + 1024.0f, 0 },
                  d[3] = { x, y + 1024.0f, 0 };
            memcpy(t[n].v[0], a, 12);
            memcpy(t[n].v[1], b, 12);
            memcpy(t[n].v[2], c, 12);
            t[n++].surface = SA1_SURFACE_SOLID;
            memcpy(t[n].v[0], a, 12);
            memcpy(t[n].v[1], c, 12);
            memcpy(t[n].v[2], d, 12);
            t[n++].surface = SA1_SURFACE_SOLID;
        }
    }
    if (sa1_collision_set_static(ctx, t, n) != SA1_OK) {
        printf("    collision: %s\n", sa1_last_error(ctx));
        free(t);
        sa1_shutdown(ctx);
        return NULL;
    }
    free(t);
    return ctx;
}

static sa1_character_id ob_char(sa1_context *ctx)
{
    sa1_character_desc d;
    sa1_character_id id = 0;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.kind = SA1_CHAR_SONIC;
    d.flags = SA1_CREATE_WORLD | SA1_CREATE_START_RINGS;
    d.position[2] = 8.0f;
    d.start_rings = 0;
    if (SA1_FAILED(sa1_character_create_ex(ctx, &d, &id))) {
        printf("    create: %s\n", sa1_last_error(ctx));
        return 0;
    }
    return id;
}

static int ob_tick(sa1_context *ctx, sa1_character_id id, float sx, float sy, uint32_t buttons)
{
    sa1_input in;
    sa1_event ev[256];
    uint32_t got;
    memset(&in, 0, sizeof in);
    in.struct_size = sizeof in;
    in.stick_x = sx;
    in.stick_y = sy;
    in.buttons = buttons;
    if (sa1_character_tick(ctx, id, &in, 1.0f / 30.0f, NULL, NULL) != SA1_OK) {
        printf("    tick: %s\n", sa1_last_error(ctx));
        return -1;
    }
    do {
        got = 0;
        sa1_poll_events(ctx, ev, 256, &got);
    } while (got == 256);
    return 0;
}

static float ob_rdf(const sa1g_world *w, uint32_t g)
{
    uint32_t v = 0;
    float f;
    sa1g_world_rd32(w, g, &v);
    memcpy(&f, &v, 4);
    return f;
}

static void ob_wrf(sa1g_world *w, uint32_t g, float f)
{
    uint32_t v;
    memcpy(&v, &f, 4);
    sa1g_world_wr32(w, g, v);
}

/* the player's position (SA1 units) */
static void ob_player(const sa1g_world *w, float p[3])
{
    sa1g_player_ptrs pp;
    int k;
    sa1g_world_player(w, &pp);
    for (k = 0; k < 3; k++) p[k] = pp.twp ? ob_rdf(w, pp.twp + 0x20u + 4u * (uint32_t)k) : 0.0f;
}

typedef struct ob_mk {
    uint32_t im, exec, out;
} ob_mk;

static void ob_mk_cb(sa1g_world *w, void *user)
{
    ob_mk *m = (ob_mk *)user;
    typedef void *(*create_fn)(uint16_t, int32_t, sa1_gfn);
    void *tp = ((create_fn)sa1_fn(SA1_GFN_C(OB_FN_CREATE), 0u))((uint16_t)m->im, 3, SA1_GFN_C(m->exec));
    (void)w;
    m->out = tp != NULL ? sa1_encode(tp) : 0u;
}

/* CreateElementalTask(im, 3, exec) at pos (SA1), angle y, scale scl (NULL: 0); cond: an OBJ_CONDITION slot with the word
 * (ssCondition << 16 | 1) as the SET spawner leaves it (0: no ocp). Returns the task */
static uint32_t ob_task(sa1g_world *w, uint32_t im, uint32_t exec, const float pos[3], uint32_t ang_y, const float scl[3],
                        uint32_t slot, uint32_t cond)
{
    ob_mk m;
    uint32_t twp, k;
    m.im = im;
    m.exec = exec;
    m.out = 0;
    if (sa1g_world_call(w, ob_mk_cb, &m) != SA1_OK || m.out == 0) {
        return 0;
    }
    twp = 0;
    sa1g_world_rd32(w, m.out + 0x20u, &twp);
    for (k = 0; k < 3; k++) {
        sa1g_world_wr32(w, twp + 0x14u + 4u * k, k == 1 ? ang_y : 0u);
        ob_wrf(w, twp + 0x20u + 4u * k, pos[k]);
        ob_wrf(w, twp + 0x2Cu + 4u * k, scl ? scl[k] : 0.0f);
    }
    if (cond) {
        uint32_t c = OB_COND + 16u * slot;
        sa1g_world_wr32(w, c, 1u | (cond << 16));
        sa1g_world_wr32(w, c + 4u, m.out);
        sa1g_world_wr32(w, c + 8u, 0u);
        ob_wrf(w, c + 12u, 160100.0f);
        sa1g_world_wr32(w, m.out + 0x1Cu, c);
    }
    return m.out;
}

typedef struct ob_drop {
    int32_t n;
    float x, y, z;
} ob_drop;

static void ob_drop_cb(sa1g_world *w, void *user)
{
    ob_drop *d = (ob_drop *)user;
    typedef void (*fn)(int32_t, float, float, float);
    (void)w;
    ((fn)sa1_fn(SA1_GFN_C(OB_FN_DROPSPAWN), 0u))(d->n, d->x, d->y, d->z);
}

/* the records of the last pair made by exec (any kind / model kinds of model `model`, 0: any) */
static uint32_t ob_recs(sa1_capture *cap, uint32_t exec, int models_only, uint32_t model)
{
    const sa1_cap_rec *r = NULL;
    uint32_t n = sa1_capture_records(cap, &r), i, c = 0;
    for (i = 0; i < n; i++) {
        if (OB_PHYS(r[i].exec) != OB_PHYS(exec) || (models_only && !SA1_CAP_IS_MODEL(r[i].kind)) ||
            (model && OB_PHYS(r[i].obj) != OB_PHYS(model)))
            continue;
        c++;
    }
    return c;
}

/* (6): the records carry who drew them */
static void ob_exec_part(const env *E)
{
    sa1_context *ctx = ob_ctx(E);
    sa1_character_id id;
    sa1g_world *w;
    sa1_capture *cap;
    const sa1_cap_rec *r = NULL;
    float p[3], q[3];
    uint32_t n, i, body = 0, body_other = 0, hud = 0, hud_exec = 0, ring_m = 0, ring_other = 0, spark = 0, spark_other = 0;
    uint32_t drop_m = 0, drop_other = 0, grp_m = 0, grp_other = 0, grp_spark = 0, k;
    int32_t got = 0;
    printf("exec (round 0.9.4, capture v5): the records carry the running exec D_8C1C1D40\n");
    if (ctx == NULL || (id = ob_char(ctx)) == 0) {
        CHECK(0, "context / Sonic");
        if (ctx) sa1_shutdown(ctx);
        return;
    }
    w = sa1_character_world(ctx, id);
    sa1_character_set_option(ctx, id, SA1_COPT_HUD_PARTS, (int32_t)SA1_HUD_ALL);
    for (i = 0; i < 12; i++) ob_tick(ctx, id, 0.0f, i < 8 ? 0.6f : 0.0f, 0);
    cap = sa1_character_capture(ctx, id);
    n = sa1_capture_records(cap, &r);
    for (i = 0; i < n; i++) {
        if (r[i].kind == SA1_CAP_RENDER) {
            body += OB_PHYS(r[i].exec) == OB_PHYS(OB_EXEC_SONIC);
            body_other += OB_PHYS(r[i].exec) != OB_PHYS(OB_EXEC_SONIC);
        }
        if (r[i].kind == SA1_CAP_SPRITE2D || r[i].kind == SA1_CAP_POLY2D) {
            hud++;
            hud_exec += r[i].exec != 0;
        }
    }
    printf("  info  %u records: %u character-renderer records with exec 8C0A77BC, %u with another; %u HUD records (%u with an "
           "exec)\n", (unsigned)n, (unsigned)body, (unsigned)body_other, (unsigned)hud, (unsigned)hud_exec);
    CHECK(body > 0 && body_other == 0, "(6) Sonic's body: %u records of the character renderer, every one with exec SonicMain "
          "8C0A77BC (%u not)", (unsigned)body, (unsigned)body_other);
    CHECK(hud > 0 && hud_exec == 0, "(6) the HUD pass: %u 2D records, exec 0 (outside the executor; %u not)", (unsigned)hud,
          (unsigned)hud_exec);
    /* a RING ahead, then Sonic runs into it (the sparkle) */
    ob_player(w, p);
    q[0] = p[0] + 20.0f;
    q[1] = p[1] + 4.0f;
    q[2] = p[2];
    ob_task(w, 2, OB_EXEC_RING, q, 0, NULL, 0, 0);
    ob_tick(ctx, id, 0.0f, 0.0f, 0);
    ob_tick(ctx, id, 0.0f, 0.0f, 0);
    n = sa1_capture_records(cap, &r);
    for (i = 0; i < n; i++) {
        if (OB_PHYS(r[i].obj) == OB_PHYS(OB_MODEL_RING) && SA1_CAP_IS_MODEL(r[i].kind)) {
            ring_m += OB_PHYS(r[i].exec) == OB_PHYS(OB_EXEC_RING);
            ring_other += OB_PHYS(r[i].exec) != OB_PHYS(OB_EXEC_RING);
        }
    }
    for (i = 0; i < 40 && spark == 0; i++) {
        sa1_state_ex s;
        ob_tick(ctx, id, 0.0f, 0.7f, 0);
        memset(&s, 0, sizeof s);
        s.struct_size = sizeof s;
        sa1_character_state_ex(ctx, id, &s);
        got = s.rings;
        n = sa1_capture_records(cap, &r);
        for (k = 0; k < n; k++) {
            if (r[k].kind == SA1_CAP_SPRITE3D && OB_PHYS(r[k].texlist) == OB_PHYS(OB_TL_SPARKLE)) {
                spark += OB_PHYS(r[k].exec) == OB_PHYS(OB_EXEC_RING);
                spark_other += OB_PHYS(r[k].exec) != OB_PHYS(OB_EXEC_RING);
            }
        }
    }
    CHECK(ring_m > 0 && ring_other == 0 && spark > 0 && spark_other == 0 && got == 1, "(6) a RING: its model %u record(s) "
          "with exec objexec_RING 8C0869E8 (%u not), collected (rings %d): its sparkle %u record(s) with it (%u not)",
          (unsigned)ring_m, (unsigned)ring_other, (int)got, (unsigned)spark, (unsigned)spark_other);
    /* dropped rings (8C087A9E) 30 units ahead */
    {
        ob_drop dq;
        ob_player(w, p);
        dq.n = 5;
        dq.x = p[0] + 30.0f;
        dq.y = p[1];
        dq.z = p[2];
        sa1g_world_call(w, ob_drop_cb, &dq);
        for (i = 0; i < 6; i++) ob_tick(ctx, id, 0.0f, 0.0f, 0);
        n = sa1_capture_records(cap, &r);
        for (i = 0; i < n; i++) {
            if (OB_PHYS(r[i].obj) == OB_PHYS(OB_MODEL_RING) && SA1_CAP_IS_MODEL(r[i].kind)) {
                drop_m += OB_PHYS(r[i].exec) == OB_PHYS(OB_EXEC_DROP);
                drop_other += OB_PHYS(r[i].exec) != OB_PHYS(OB_EXEC_DROP);
            }
        }
        CHECK(drop_m > 0 && drop_other == 0, "(6) dropped rings: %u ring model records with exec 8C0870EA (%u not)",
              (unsigned)drop_m, (unsigned)drop_other);
    }
    sa1_character_destroy(ctx, id);
    /* an O_GRING line: its children draw with the group's exec (gsa1_094_plan.md Review R.2) */
    if ((id = ob_char(ctx)) != 0) {
        static const float line[3] = { 7.0f, 10.0f, 0.0f };
        w = sa1_character_world(ctx, id);
        for (i = 0; i < 6; i++) ob_tick(ctx, id, 0.0f, 0.0f, 0);
        cap = sa1_character_capture(ctx, id);
        ob_player(w, p);
        q[0] = p[0] + 55.0f;
        q[1] = p[1] + 4.0f;
        q[2] = p[2];
        ob_task(w, 2, OB_EXEC_GRING, q, 0x4000u, line, 0, 0);
        ob_tick(ctx, id, 0.0f, 0.0f, 0);
        ob_tick(ctx, id, 0.0f, 0.0f, 0);
        n = sa1_capture_records(cap, &r);
        for (i = 0; i < n; i++) {
            if (OB_PHYS(r[i].obj) == OB_PHYS(OB_MODEL_RING) && SA1_CAP_IS_MODEL(r[i].kind)) {
                grp_m += OB_PHYS(r[i].exec) == OB_PHYS(OB_EXEC_GRING);
                grp_other += OB_PHYS(r[i].exec) != OB_PHYS(OB_EXEC_GRING);
            }
        }
        for (i = 0; i < 60 && grp_spark == 0; i++) {
            ob_tick(ctx, id, 0.0f, 0.7f, 0);
            n = sa1_capture_records(cap, &r);
            for (k = 0; k < n; k++)
                if (r[k].kind == SA1_CAP_SPRITE3D && OB_PHYS(r[k].texlist) == OB_PHYS(OB_TL_SPARKLE)) {
                    grp_spark += OB_PHYS(r[k].exec) == OB_PHYS(OB_EXEC_GRING);
                    grp_other += OB_PHYS(r[k].exec) != OB_PHYS(OB_EXEC_GRING);
                }
        }
        CHECK(grp_m >= 4 && grp_spark > 0 && grp_other == 0, "(6) an O_GRING line's children: %u ring model records and %u "
              "sparkle records with the group's exec 8C087B80 (%u not)", (unsigned)grp_m, (unsigned)grp_spark,
              (unsigned)grp_other);
        sa1_character_destroy(ctx, id);
    }
    sa1_shutdown(ctx);
}

/* (11): the static-ring turn case (gsa1_090_rings.md Review R.4.9): 12 rings 150 units around, 40 up, Sonic runs in
 * tight circles: every ring model at alpha 0.5 within 0.25 units of its place (0.9.0: 10.58; 0.9.1+: 0.001) */
static void ob_turn_part(const env *E)
{
    sa1_context *ctx = ob_ctx(E);
    sa1_character_id id;
    sa1g_world *w;
    float p[3], R[64][3];
    uint32_t i, k, j, nr = 0, measured = 0;
    double worst = 0.0;
    printf("turn: 12 RINGs 150 units around (40 up), Sonic in tight circles (rings review R.4.9)\n");
    if (ctx == NULL || (id = ob_char(ctx)) == 0) {
        CHECK(0, "context / Sonic");
        if (ctx) sa1_shutdown(ctx);
        return;
    }
    w = sa1_character_world(ctx, id);
    for (i = 0; i < 6; i++) ob_tick(ctx, id, 0.0f, 0.0f, 0);
    ob_player(w, p);
    for (k = 0; k < 12; k++) {
        float q[3];
        double a = k * (6.283185307179586 / 12.0);
        q[0] = p[0] + (float)(150.0 * cos(a));
        q[1] = p[1] + 40.0f;
        q[2] = p[2] + (float)(150.0 * sin(a));
        ob_task(w, 2, OB_EXEC_RING, q, 0, NULL, 0, 0);
    }
    for (i = 0; i < 150; i++) {
        const sa1_render *r = NULL;
        double a = i * 0.35;
        ob_tick(ctx, id, (float)sin(a), (float)cos(a), 0);
        if (sa1_character_render(ctx, id, 1.0f, &r) != SA1_OK || r == NULL) continue;
        for (k = 0; k < r->num_draws; k++) {
            const sa1_draw *dr = &r->draws[k];
            uint32_t q;
            if (dr->prim != SA1_PRIM_TRIANGLES || !(dr->flags & SA1_DRAW_ENV_MAP) || (dr->flags & SA1_DRAW_SHADOW) ||
                dr->count % 96u)
                continue;
            for (q = 0; q < dr->count / 96u; q++) {
                float c[3] = { 0, 0, 0 };
                uint32_t t, m, have = 0;
                for (t = dr->first + 96u * q; t < dr->first + 96u * (q + 1u); t++)
                    for (m = 0; m < 9; m++) c[m % 3] += r->positions[9u * t + m];
                for (m = 0; m < 3; m++) c[m] /= 288.0f;
                for (j = 0; j < nr && !have; j++)
                    have = fabsf(c[0] - R[j][0]) + fabsf(c[1] - R[j][1]) + fabsf(c[2] - R[j][2]) < 2.0f;
                if (!have && nr < 64) memcpy(R[nr++], c, 12);
            }
        }
        if (sa1_character_render(ctx, id, 0.5f, &r) != SA1_OK || r == NULL) continue;
        for (k = 0; k < r->num_draws; k++) {
            const sa1_draw *dr = &r->draws[k];
            uint32_t q;
            if (dr->prim != SA1_PRIM_TRIANGLES || !(dr->flags & SA1_DRAW_ENV_MAP) || (dr->flags & SA1_DRAW_SHADOW) ||
                dr->count % 96u)
                continue;
            for (q = 0; q < dr->count / 96u; q++) {
                float c[3] = { 0, 0, 0 };
                uint32_t t, m;
                double best = 1e30;
                for (t = dr->first + 96u * q; t < dr->first + 96u * (q + 1u); t++)
                    for (m = 0; m < 9; m++) c[m % 3] += r->positions[9u * t + m];
                for (m = 0; m < 3; m++) c[m] /= 288.0f;
                for (j = 0; j < nr; j++) {
                    double dx = c[0] - R[j][0], dy = c[1] - R[j][1], dz = c[2] - R[j][2], d = sqrt(dx * dx + dy * dy + dz * dz);
                    if (d < best) best = d;
                }
                measured++;
                if (best / 4.0 > worst) worst = best / 4.0;     /* SA1 units (SOURCE: 4 HU per unit) */
            }
        }
    }
    printf("  info  %u ring places, %u ring models measured at alpha 0.5\n", (unsigned)nr, (unsigned)measured);
    CHECK(nr >= 2 && measured > 100 && worst < 0.25, "(11) the static-ring turn: every ring model at alpha 0.5 within 0.25 "
          "units of its place (%.4f units; 0.9.0: 10.58)", worst);
    sa1_shutdown(ctx);
}

/* emit the capture's last pair into b as sa1_character_tick does, with the exec arrays */
static sa1_status ob_emit(sa1_context *ctx, sa1_capture *cap, sa1_meshbuf *b)
{
    sa1_capture_emit_opts o;
    sa1_geometry g;
    memset(&o, 0, sizeof o);
    o.shadow_model = sa1_capture_shadow_model(cap);
    o.effects = 1;
    o.dedupe = 1;
    o.exec_arrays = 1;
    memset(&g, 0, sizeof g);
    return sa1_capture_emit_into(ctx, cap, &o, b, &g);
}

/* the emitted pair against the exec table: every drawn model and sprite carries SA1_DRAW_OBJECT / SA1_NODE_OBJECT exactly
 * when its exec is a game object's; counts per class */
typedef struct ob_tally {
    uint32_t nodes_obj, nodes_char, sprites_obj, sprites_other, wrong, ring_nodes, gbox_nodes, shadow_obj, shadow_char;
} ob_tally;

static void ob_tally_of(const sa1_meshbuf *b, ob_tally *t)
{
    uint32_t k, d, s;
    memset(t, 0, sizeof *t);
    for (k = 0; k < b->nn; k++) {
        int obj = sa1_capture_object_exec(b->node_exec ? b->node_exec[k] : 0u);
        int fl = (b->node_fl[k] & SA1_NODE_OBJECT) != 0;
        t->wrong += obj != fl;
        if (obj) t->nodes_obj++;
        if (OB_PHYS(b->node_exec ? b->node_exec[k] : 0u) == OB_PHYS(OB_EXEC_SONIC)) t->nodes_char++;
        if (OB_PHYS(b->node_model[k]) == OB_PHYS(OB_MODEL_RING)) t->ring_nodes += obj;
        for (d = 0; d < 4; d++)
            if (OB_PHYS(b->node_model[k]) == OB_PHYS(ob_gbox_models[d])) t->gbox_nodes += obj;
        if (b->node_fl[k] & SA1_NODE_SHADOW) {
            if (obj) t->shadow_obj++;
            else t->shadow_char++;
        }
    }
    for (d = 0; d < b->ndraw; d++) {
        const sa1_draw *dr = &b->draws[d];
        if (dr->prim != SA1_PRIM_SPRITES) continue;
        for (s = dr->first; s < dr->first + dr->count && s < b->nspr; s++) {
            int obj = sa1_capture_object_exec(b->spr_exec ? b->spr_exec[s] : 0u);
            t->wrong += obj != ((dr->flags & SA1_DRAW_OBJECT) != 0);
            if (obj) t->sprites_obj++;
            else t->sprites_other++;
        }
    }
}

/* the render's blob shadow draws: pieces (2 triangles) under the character (horizontally within 6 HU of the root) and
 * elsewhere, per class of draw; mixed: a draw with pieces of both */
static void ob_shadows(const sa1_render *r, uint32_t *char_in_char, uint32_t *char_elsewhere, uint32_t *obj_under_char,
                       uint32_t *obj_pieces)
{
    uint32_t d, piece, t, k;
    *char_in_char = *char_elsewhere = *obj_under_char = *obj_pieces = 0;
    for (d = 0; d < r->num_draws; d++) {
        const sa1_draw *dr = &r->draws[d];
        int obj = (dr->flags & SA1_DRAW_OBJECT) != 0;
        if (dr->prim != SA1_PRIM_TRIANGLES || !(dr->flags & SA1_DRAW_SHADOW)) continue;
        for (piece = 0; piece + 2u <= dr->count; piece += 2u) {
            float c[3] = { 0, 0, 0 };
            double dc;
            for (t = dr->first + piece; t < dr->first + piece + 2u; t++)
                for (k = 0; k < 9; k++) c[k % 3] += r->positions[9u * t + k];
            for (k = 0; k < 3; k++) c[k] /= 6.0f;
            dc = sqrt((double)(c[0] - r->position[0]) * (c[0] - r->position[0]) +
                      (double)(c[1] - r->position[1]) * (c[1] - r->position[1]));
            if (obj) {
                (*obj_pieces)++;
                *obj_under_char += dc < 6.0;
            } else if (dc < 6.0) {
                (*char_in_char)++;
            } else {
                (*char_elsewhere)++;
            }
        }
    }
}

/* (10) and (12): a RING, an O_GRING line, a ground box (with an OBJ_CONDITION) and an air box in a Sonic world */
static void ob_object_part(const env *E)
{
    static const float line[3] = { 7.0f, 10.0f, 0.0f }, gscl[3] = { 3.0f, 0.0f, 0.0f }, ascl[3] = { 6.0f, 0.0f, 0.0f };
    sa1_context *ctx = ob_ctx(E);
    sa1_character_id id;
    sa1g_world *w;
    sa1_capture *cap;
    sa1_meshbuf mb;
    ob_tally T;
    const sa1_render *r = NULL;
    const sa1_screen *scr = NULL;
    float p[3], q[3];
    uint32_t i, k, gbox = 0, cond = 0, c_in = 0, c_out = 0, o_under = 0, o_pieces = 0, scr_obj = 0, rend_obj = 0;
    uint32_t debris = 0, debris_obj = 0, shell_nodes = 0;
    printf("object (round 0.9.4): game OBJECT draws (SA1_DRAW_OBJECT) and who drew them\n");
    if (ctx == NULL || (id = ob_char(ctx)) == 0) {
        CHECK(0, "context / Sonic");
        if (ctx) sa1_shutdown(ctx);
        return;
    }
    memset(&mb, 0, sizeof mb);
    w = sa1_character_world(ctx, id);
    sa1_character_set_option(ctx, id, SA1_COPT_HUD_PARTS, (int32_t)SA1_HUD_ALL);
    /* Sonic walks a little (his blob shadow is drawn from his first move on), then stands */
    for (i = 0; i < 16; i++) ob_tick(ctx, id, 0.0f, i < 6 ? 0.5f : 0.0f, 0);
    cap = sa1_character_capture(ctx, id);
    ob_player(w, p);
    q[0] = p[0] + 22.0f; q[1] = p[1] + 4.0f; q[2] = p[2] + 6.0f;
    ob_task(w, 2, OB_EXEC_RING, q, 0, NULL, 0, 0);
    q[0] = p[0] + 70.0f; q[1] = p[1] + 4.0f; q[2] = p[2] + 22.0f;
    ob_task(w, 2, OB_EXEC_GRING, q, 0x4000u, line, 0, 0);
    q[0] = p[0] + 30.0f; q[1] = p[1]; q[2] = p[2] - 14.0f;
    gbox = ob_task(w, 2, OB_EXEC_GBOX, q, 0, gscl, 200, 0x8001u);
    q[0] = p[0] + 45.0f; q[1] = p[1] + 12.0f; q[2] = p[2] - 4.0f;
    ob_task(w, 15, OB_EXEC_ABOX, q, 0, ascl, 0, 0);
    for (i = 0; i < 40; i++) ob_tick(ctx, id, 0.0f, 0.0f, 0);
    /* the records */
    printf("  info  records of the pair: Sonic %u, RING %u, O_GRING %u, ground box %u, air box %u (model records: %u / %u / %u "
           "/ %u / %u)\n", (unsigned)ob_recs(cap, OB_EXEC_SONIC, 0, 0), (unsigned)ob_recs(cap, OB_EXEC_RING, 0, 0),
           (unsigned)ob_recs(cap, OB_EXEC_GRING, 0, 0), (unsigned)ob_recs(cap, OB_EXEC_GBOX, 0, 0),
           (unsigned)ob_recs(cap, OB_EXEC_ABOX, 0, 0), (unsigned)ob_recs(cap, OB_EXEC_SONIC, 1, 0),
           (unsigned)ob_recs(cap, OB_EXEC_RING, 1, 0), (unsigned)ob_recs(cap, OB_EXEC_GRING, 1, 0),
           (unsigned)ob_recs(cap, OB_EXEC_GBOX, 1, 0), (unsigned)ob_recs(cap, OB_EXEC_ABOX, 1, 0));
    CHECK(ob_recs(cap, OB_EXEC_RING, 1, OB_MODEL_RING) == 1 && ob_recs(cap, OB_EXEC_GRING, 1, OB_MODEL_RING) >= 4 &&
              ob_recs(cap, OB_EXEC_GBOX, 1, 0) >= 4 && ob_recs(cap, OB_EXEC_ABOX, 1, 0) >= 1,
          "(10) the objects drew with their own execs: the RING's model, >= 4 group children's models (%u), the ground box's "
          "models (%u), the air box's (%u)", (unsigned)ob_recs(cap, OB_EXEC_GRING, 1, OB_MODEL_RING),
          (unsigned)ob_recs(cap, OB_EXEC_GBOX, 1, 0), (unsigned)ob_recs(cap, OB_EXEC_ABOX, 1, 0));
    /* the emitter: OBJECT exactly for the objects' draws */
    if (ob_emit(ctx, cap, &mb) == SA1_OK) {
        ob_tally_of(&mb, &T);
        printf("  info  emitted: %u drawn models (%u OBJECT, %u Sonic's), %u sprites (%u OBJECT); ring models %u, ground box "
               "models %u; blob shadows: %u OBJECT, %u not\n", (unsigned)mb.nn, (unsigned)T.nodes_obj,
               (unsigned)T.nodes_char, (unsigned)mb.nspr, (unsigned)T.sprites_obj, (unsigned)T.ring_nodes,
               (unsigned)T.gbox_nodes, (unsigned)T.shadow_obj, (unsigned)T.shadow_char);
        CHECK(T.wrong == 0 && T.ring_nodes >= 5 && T.gbox_nodes >= 4 && T.nodes_char > 0 && T.shadow_obj > 0,
              "(10) every drawn model and sprite carries SA1_DRAW_OBJECT exactly when its exec is a game object's (%u "
              "wrong): the ring models (%u), the ground box (%u), the rings' blob shadows (%u); Sonic's %u models not",
              (unsigned)T.wrong, (unsigned)T.ring_nodes, (unsigned)T.gbox_nodes, (unsigned)T.shadow_obj,
              (unsigned)T.nodes_char);
    } else {
        CHECK(0, "(10) emit");
    }
    /* the render (what a host draws) */
    if (sa1_character_render(ctx, id, 1.0f, &r) == SA1_OK && r != NULL) {
        for (k = 0; k < r->num_draws; k++) rend_obj += (r->draws[k].flags & SA1_DRAW_OBJECT) != 0;
        ob_shadows(r, &c_in, &c_out, &o_under, &o_pieces);
        printf("  info  render: %u draws, %u OBJECT; blob shadow pieces: %u of Sonic's draw under him, %u elsewhere; %u in "
               "OBJECT draws (%u under Sonic)\n", (unsigned)r->num_draws, (unsigned)rend_obj, (unsigned)c_in,
               (unsigned)c_out, (unsigned)o_pieces, (unsigned)o_under);
        CHECK(rend_obj > 0 && c_in == 1 && c_out == 0 && o_pieces > 0 && o_under == 0, "(10) Sonic's own blob shadow and the "
              "rings' blob shadows are never one draw (review R.1): his draw holds his blob only (%u + %u), the OBJECT "
              "draws %u ring blobs and not his", (unsigned)c_in, (unsigned)c_out, (unsigned)o_pieces);
    } else {
        CHECK(0, "(10) render");
    }
    if (sa1_character_screen(ctx, id, &scr) == SA1_OK && scr != NULL) {
        for (k = 0; k < scr->num_draws; k++) scr_obj += (scr->draws[k].flags & SA1_DRAW_OBJECT) != 0;
        CHECK(scr->num_draws > 0 && scr_obj == 0, "(10) the HUD's %u screen draws carry no SA1_DRAW_OBJECT (%u do)",
              (unsigned)scr->num_draws, (unsigned)scr_obj);
    }
    /* Sonic falls onto the ground box's lid: the box breaks (0x8011 -> 0x8015), its debris particle is not an OBJECT draw,
       the shell is */
    if (gbox) {
        uint32_t btwp = 0;
        sa1g_player_ptrs pp;
        sa1g_world_rd32(w, gbox + 0x20u, &btwp);
        sa1g_world_player(w, &pp);
        ob_wrf(w, pp.twp + 0x20u, ob_rdf(w, btwp + 0x20u));
        ob_wrf(w, pp.twp + 0x24u, ob_rdf(w, btwp + 0x24u) + 25.0f);
        ob_wrf(w, pp.twp + 0x28u, ob_rdf(w, btwp + 0x28u));
        for (i = 0; i < 40; i++) {
            ob_tick(ctx, id, 0.0f, 0.0f, 0);
            if (ob_emit(ctx, cap, &mb) != SA1_OK) break;
            for (k = 0; k < mb.ndraw; k++) {
                const sa1_draw *dr = &mb.draws[k];
                uint32_t s;
                if (dr->prim != SA1_PRIM_SPRITES) continue;
                for (s = dr->first; s < dr->first + dr->count && s < mb.nspr; s++) {
                    uint32_t ex = mb.spr_exec ? mb.spr_exec[s] : 0u;
                    /* the particles: the manager 8C09DF64 runs outside the task executor (exec 0) */
                    if (OB_PHYS(ex) == OB_PHYS(OB_EXEC_SONIC) || sa1_capture_object_exec(ex)) continue;
                    debris++;
                    debris_obj += (dr->flags & SA1_DRAW_OBJECT) != 0;
                }
            }
            for (k = 0; k < mb.nn && mb.node_exec != NULL; k++)
                if (OB_PHYS(mb.node_exec[k]) == OB_PHYS(OB_EXEC_GBOX))
                    shell_nodes += (mb.node_fl[k] & SA1_NODE_OBJECT) != 0;
        }
        sa1g_world_rd32(w, OB_COND + 16u * 200u, &cond);
        CHECK((cond >> 16) == 0x8015u && debris > 0 && debris_obj == 0 && shell_nodes > 0, "(10) the ground box broke "
              "(ssCondition %04X): %u debris particle sprites (drawn by the particle manager 8C09DF64 outside the task "
              "executor: exec 0), none OBJECT; the shell's models OBJECT (%u)", (unsigned)(cond >> 16), (unsigned)debris,
              (unsigned)shell_nodes);
    }
    sa1_meshbuf_free_exec(ctx, &mb);
    sa1_meshbuf_free(ctx, &mb);
    sa1_character_destroy(ctx, id);
    sa1_shutdown(ctx);
}

/* (12) measure (no pass / fail): capture records per box (ground intact, ground shell, air) with 1 / 10 / 32 in view */
static void ob_boxrec_part(const env *E)
{
    static const uint32_t counts[3] = { 1, 10, 32 };
    static const char *const kinds[3] = { "ground box, intact", "ground box, shell", "air box" };
    sa1_context *ctx = ob_ctx(E);
    uint32_t kk, ci;
    printf("boxrec (measure): capture records per item box in view (for 094-lib-set's box cap)\n");
    if (ctx == NULL) {
        CHECK(0, "context");
        return;
    }
    for (kk = 0; kk < 3; kk++) {
        for (ci = 0; ci < 3; ci++) {
            sa1_character_id id = ob_char(ctx);
            sa1g_world *w;
            sa1_capture *cap;
            sa1_capture_stats st;
            float p[3];
            uint32_t i, b, recs, exec = kk == 2 ? OB_EXEC_ABOX : OB_EXEC_GBOX, capmax = 0;
            if (!id) break;
            w = sa1_character_world(ctx, id);
            for (i = 0; i < 4; i++) ob_tick(ctx, id, 0.0f, 0.0f, 0);
            cap = sa1_character_capture(ctx, id);
            ob_player(w, p);
            for (b = 0; b < counts[ci]; b++) {
                static const float gs[3] = { 3.0f, 0.0f, 0.0f }, as[3] = { 3.0f, 0.0f, 0.0f };
                float q[3];
                q[0] = p[0] + 30.0f + 12.0f * (float)(b / 4u);
                q[1] = p[1] + (kk == 2 ? 10.0f : 0.0f);
                q[2] = p[2] - 18.0f + 12.0f * (float)(b % 4u);
                if (kk == 2)
                    ob_task(w, 15, OB_EXEC_ABOX, q, 0, as, 0, 0);
                else
                    ob_task(w, 2, OB_EXEC_GBOX, q, 0, gs, 300u + b, kk == 1 ? 0x8005u : 0x8001u);
            }
            for (i = 0; i < 40; i++) ob_tick(ctx, id, 0.0f, 0.0f, 0);
            recs = ob_recs(cap, exec, 0, 0);
            sa1_capture_get_stats(cap, &st);
            /* the capture block's own capacity (rt_draw.c RT_CAP_HDR.capacity at +0x1C = RT_CAP_MAX: 2,048 since round
               0.9.4b) instead of a literal; the block lies in segment 0x18 (pcdata), outside sa1g_world_rd32's arena */
            if (st.block != 0 && w != NULL) {
                memcpy(&capmax, sa1g_world_ptr(w, st.block + 0x1Cu), 4);
            }
            printf("  info  %-20s x %2u: %4u records by the boxes (%.1f per box; %u model records), %u records in the pair "
                   "(of %u), %u dropped\n", kinds[kk], (unsigned)counts[ci], (unsigned)recs,
                   (double)recs / (double)counts[ci], (unsigned)ob_recs(cap, exec, 1, 0), (unsigned)st.records,
                   (unsigned)capmax, (unsigned)st.dropped);
            sa1_character_destroy(ctx, id);
        }
    }
    sa1_shutdown(ctx);
}

/* --part LIST: only these parts: a comma-separated list of part numbers 1-9 and names ("lsd"); a run of digits is one
 * part per digit (e.g. "19" = parts 1 and 9, as before 2026-10-21); default all */
static const char *g_parts;

static int part_on_name(const char *name)
{
    const char *p = g_parts;
    size_t n = strlen(name);
    if (g_parts == NULL) {
        return 1;
    }
    while (*p) {
        const char *e = strchr(p, ',');
        size_t len = e ? (size_t)(e - p) : strlen(p), i;
        int digits = len > 0;
        for (i = 0; i < len; i++) {
            digits &= p[i] >= '0' && p[i] <= '9';
        }
        if (digits ? (n == 1 && memchr(p, name[0], len) != NULL) : (len == n && strncmp(p, name, n) == 0)) {
            return 1;
        }
        if (e == NULL) {
            break;
        }
        p = e + 1;
    }
    return 0;
}

static int part_on(int k)
{
    char s[2];
    s[0] = (char)('0' + k);
    s[1] = 0;
    return part_on_name(s);
}

int main(int argc, char **argv)
{
    env E;
    int i;
    memset(&E, 0, sizeof E);
    E.frames = 60;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--harness") && i + 1 < argc) {
            E.harness = argv[++i];
        } else if (!strcmp(argv[i], "--frames") && i + 1 < argc) {
            E.frames = (uint32_t)atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--obj") && i + 1 < argc) {
            E.obj_dir = argv[++i];
        } else if (!strcmp(argv[i], "--part") && i + 1 < argc) {
            g_parts = argv[++i];
        } else if (E.sadc == NULL) {
            E.sadc = argv[i];
        } else if (E.gdi == NULL) {
            E.gdi = argv[i];
        }
    }
    if (E.gdi == NULL) {
        E.gdi = getenv("SA1_TEST_GDI");
    }
    if (E.sadc == NULL || E.gdi == NULL) {
        printf("usage: capture_test [--harness DIR] [--frames N] [--obj DIR] [--part LIST] SADC [GDI]  (LIST: parts 1-9, "
               "\"lsd\", \"alpha\", \"powerup\", \"exec\", \"turn\", \"object\" and \"boxrec\", comma-separated; GDI "
               "or $SA1_TEST_GDI)\n");
        return 2;
    }
    printf("libsa1 %s capture test (display capture 7.2, events 7.4): %s\n", sa1_version_string(), sa1g_game_info());
    if (part_on(1))
        test_invariance(&E);
    if (part_on(2))
        test_mesh(&E);
    if (part_on(3))
        test_harness_ram(&E);
    if (part_on(4))
        test_events(&E);
    if (part_on(5))
        test_live(&E);
    if (part_on(6))
        test_textures(&E);
    if (part_on(7))
        test_render(&E);
    if (part_on(8))
        test_effects(&E);
    if (part_on(9))
        test_hidden(&E);
    if (part_on_name("lsd"))
        test_lsd(&E);
    if (part_on_name("alpha"))
        test_alpha(&E);
    if (part_on_name("powerup"))
        test_powerup(&E);
    if (part_on_name("exec"))
        ob_exec_part(&E);
    if (part_on_name("turn"))
        ob_turn_part(&E);
    if (part_on_name("object"))
        ob_object_part(&E);
    if (part_on_name("boxrec"))
        ob_boxrec_part(&E);
    printf("%s: %d failure(s), %d skipped\n", g_fail ? "FAIL" : "PASS", g_fail, g_skip);
    return g_fail ? 1 : 0;
}
