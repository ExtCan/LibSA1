/*
 * world_boot_test.c - world boot and player creation against the reference traces (sadc
 * docs/libsa1_integration.md 6.2-6.7, 9.1 V3, 11 M0 / M1b), plus the live tick and the character API.
 *
 *   world_boot_test [--out DIR] [--dump DIR] [--stage-tolerant] [--free] SADC [GDI]   (GDI: or $SA1_TEST_GDI)
 *
 * M0   Sonic's world image from the user's disc (1ST_READ + S_MOT + S_SBMOT + B_CHAOS7), libsa1's own boot
 *      with an EMPTY landtable, player 0 at ec1_stand's header start (-9, 16, 4), ang.y 0x6000; the free fall
 *      replayed: every decoded field equal except land_count / attr / last_attr (the shadow fields are inside
 *      the raw blobs, not compared) while the reference is airborne (twp->flag & 3 == 0: steps 0-16; it
 *      touches the deck at step 17, two steps before the mode change of step 19), steps 17-18 reported.
 * M1b  The DISC image (Sonic with STG01 at 0x0C900000 instead of B_CHAOS7), the stage's own landtable
 *      0x0C981554 (DISC collision), the death zone task 8C07BAC0, no land update at boot (the stage start
 *      builds the cache at the end of step 0); ec1_stand (180 records) and ec1_run_jump (271) replayed:
 *      decoded fields and raw blobs with heap pointers compared by structure (sa1trace.py
 *      --heap-by-structure). This boot has no stage tasks (SA1G_BOOT_STAGE_TASKS; tests/replay_test.c replays
 *      with them, strictly): the residuals of the STG01 level task's ocean 0C909FDA and footprints 0C9061DA
 *      remain here: land_count one lower (the water plane DynamicCOL), rand_state some draws behind;
 *      --stage-tolerant accepts exactly those (constant land_count offset, reference rand ahead).
 * V3   With --dump DIR the arena right after boot is written to DIR/boot_ec1.ram for a region diff against
 *      SADC/build/traces/m1_ec1_stand.step0.ram.
 * Free (--free, information only) the M1b replays with only the pad and analog injected (V6 style).
 * Live 60 pairs of the default step in live mode on the M1b world (pad held forward), and the character
 *      API (sa1g_character_world_create / _tick / sa1g_world_state) on a Sonic world.
 */
#include "world_replay.c"
#include "sa1_api_world.h"

#include <math.h>

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

static void log_cb(void *user, int32_t level, const char *msg)
{
    (void)user;
    if (level >= SA1_LOG_WARN) {
        printf("    [libsa1 %s] %s\n", level >= SA1_LOG_ERROR ? "error" : "warn", msg);
    }
}

/* a booted Sonic world at a trace header's start */
static sa1_status boot_at(sa1g_world *w, const sa1g_image *img, const sa1_trace_header *h, int32_t land_mode,
                          uint32_t flags)
{
    sa1g_boot_cfg cfg;
    sa1_status st = sa1g_world_apply_image(w, img);
    if (st != SA1_OK) {
        return st;
    }
    sa1g_boot_defaults(&cfg);
    cfg.chr = h->chr;
    cfg.level = h->level;
    cfg.act = h->act;
    cfg.flags = SA1G_BOOT_POS | flags;
    memcpy(cfg.pos, h->start_pos, sizeof cfg.pos);
    memcpy(cfg.ang, h->start_ang, sizeof cfg.ang);
    cfg.land_mode = land_mode;
    cfg.landtable = land_mode == SA1G_LAND_DISC ? h->landtable : 0u;
    st = sa1g_world_boot(w, &cfg);
    if (st != SA1_OK) {
        printf("    boot: %s: %s\n", sa1g_status_string(st), w->fault.msg);
    }
    return st;
}

/* the game's start table (8C04F8FA) gives the trace header's start for Sonic on stage 0x0100 */
static void test_start_table(sa1g_world *w, const sa1g_image *img, const sa1_trace_header *h)
{
    sa1g_boot_cfg cfg;
    sa1g_player_ptrs pp;
    float pos[3] = { 0.0f, 0.0f, 0.0f };
    uint32_t ang[3] = { 0, 0, 0 };
    sa1_status st = sa1g_world_apply_image(w, img);
    sa1g_boot_defaults(&cfg);
    cfg.chr = h->chr;
    cfg.level = h->level;
    cfg.act = h->act;
    if (st == SA1_OK) {
        st = sa1g_world_boot(w, &cfg);
    }
    sa1g_world_player(w, &pp);
    if (st == SA1_OK && pp.twp != 0) {
        memcpy(ang, (const uint8_t *)sa1g_world_ptr(w, pp.twp) + 0x14, sizeof ang);
        memcpy(pos, (const uint8_t *)sa1g_world_ptr(w, pp.twp) + 0x20, sizeof pos);
    }
    CHECK(st == SA1_OK && memcmp(pos, h->start_pos, sizeof pos) == 0 && memcmp(ang, h->start_ang, sizeof ang) == 0,
          "start table 8C19C4F8 for stage %02X%02X: (%g, %g, %g) ang (%X, %X, %X), the trace header's start", (unsigned)h->level,
          (unsigned)h->act, pos[0], pos[1], pos[2], (unsigned)ang[0], (unsigned)ang[1], (unsigned)ang[2]);
}

/* ---- M0 ----------------------------------------------------------------------------------------------- */
/* The free fall of ec1_stand: the records before the reference reports ground contact (twp->flag & 3,
 * design 7.1), at most steps 0-18. The reference sets it at step 17, two steps before the landing at 19
 * that the design's M0 window assumed: from there on the deck's collision (absent from an EMPTY
 * landtable) changes the state, which M1b covers with the DISC landtable. */
static void test_m0(sa1g_world *w, const sa1g_image *img, const wr_trace *t, const char *out_dir)
{
    wr_result res, rest;
    sa1_trace_rec out[19];
    sa1_status st;
    uint32_t air = 0;
    const uint32_t inj = SA1G_INJ_PAD | SA1G_INJ_ANALOG | SA1G_INJ_RAND | SA1G_INJ_XMTRX | SA1G_INJ_CAMERA;
    printf("M0 (ec1_stand free fall, EMPTY landtable, own boot):\n");
    while (air < 19 && air < t->n && (t->r[air].twp_flag & 3) == 0) {
        air++;
    }
    st = boot_at(w, img, &t->h, SA1G_LAND_EMPTY, 0);
    CHECK(st == SA1_OK && w->booted && w->landtable != 0 && w->camera_twp != 0 && w->player_tp != 0,
          "boot: player task %08X, camera adapter taskwk %08X, empty landtable %08X (COLs %08X, room for %u)",
          (unsigned)w->player_tp, (unsigned)w->camera_twp, (unsigned)w->landtable, (unsigned)w->window_cols,
          (unsigned)w->window_cap);
    if (st != SA1_OK) {
        return;
    }
    wr_replay(w, t, 0, air, inj, 0, WR_CMP_NO_LAND, out, &res);
    wr_result_print("M0 replay of the airborne records (decoded fields, land_count / attr / last_attr excluded)", &res,
                    air);
    CHECK(!res.fault && air >= 17 && res.steps == air && res.d.nsteps_bad == 0,
          "M0: ec1_stand steps 0-%u (reference airborne): decoded fields equal (land_count / attr / last_attr "
          "excluded): %u of %u differ", (unsigned)(air - 1), (unsigned)res.d.nsteps_bad, (unsigned)air);
    if (!res.fault && air < 19) {
        wr_replay(w, t, air, 19, inj, 0, WR_CMP_NO_LAND, out + air, &rest);
        printf("  note  steps %u-18 (the reference touches the deck, twp->flag %X): %u of %u differ without "
               "landtable geometry, as expected\n", (unsigned)air, (unsigned)(uint16_t)t->r[air].twp_flag,
               (unsigned)rest.d.nsteps_bad, (unsigned)(19 - air));
        wr_diff_print(&rest.d, 4);
        res.steps += rest.steps;
    }
    if (out_dir != NULL && res.steps > 0) {
        char path[1024];
        snprintf(path, sizeof path, "%s/m0_ec1_stand.bin", out_dir);
        if (wr_trace_write(path, &t->h, out, res.steps) == 0) {
            printf("    wrote %s\n", path);
        }
    }
}

/* ---- M1b ---------------------------------------------------------------------------------------------- */
/* V6-style information: the same boot, only the pad and analog injected (the world's own rand, XMTRX and
 * camera adapter); not a pass / fail criterion of M1b */
static void test_free_one(sa1g_world *w, const sa1g_image *img, const wr_trace *t, const char *name)
{
    wr_result res;
    if (boot_at(w, img, &t->h, SA1G_LAND_DISC, SA1G_BOOT_NO_WORLD_UPDATE | SA1G_BOOT_DEATHZONE) != SA1_OK) {
        CHECK(0, "%s: boot for the free run", name);
        return;
    }
    wr_replay(w, t, 0, t->n, SA1G_INJ_PAD | SA1G_INJ_ANALOG, 0, WR_CMP_HEAP | WR_CMP_NO_LANDCNT | WR_CMP_NO_RAND, NULL,
              &res);
    printf("  note  free run of %s (pad + analog only; land_count / rand_state not compared): %u of %u records differ%s\n",
           name, (unsigned)res.d.nsteps_bad, (unsigned)res.steps, res.fault ? " (fault)" : "");
    wr_diff_print(&res.d, 6);
}

static int test_m1b_one(sa1g_world *w, const sa1g_image *img, const wr_trace *t, const char *name, const char *out_dir,
                        const char *dump_dir, int tolerant, sa1_trace_rec **keep)
{
    wr_result res;
    wr_diff others;
    sa1_trace_rec *out;
    sa1_status st;
    uint32_t i, land_steps = 0, rand_steps = 0, rand_ok = 0;
    int32_t land_min = 0x7FFFFFFF, land_max = -0x7FFFFFFF;
    int strict_ok, tol_ok;
    st = boot_at(w, img, &t->h, SA1G_LAND_DISC, SA1G_BOOT_NO_WORLD_UPDATE | SA1G_BOOT_DEATHZONE);
    CHECK(st == SA1_OK && w->landtable == t->h.landtable,
          "%s: boot with the DISC landtable %08X, player task %08X, camera adapter taskwk %08X", name,
          (unsigned)w->landtable, (unsigned)w->player_tp, (unsigned)w->camera_twp);
    if (st != SA1_OK) {
        return 1;
    }
    if (dump_dir != NULL) {
        char path[1024];
        snprintf(path, sizeof path, "%s/boot_%s.ram", dump_dir, name);
        if (wr_write_file(path, w->ram, SA1G_ARENA_SIZE) == 0) {
            printf("    V3: arena after boot written to %s\n", path);
        }
    }
    out = (sa1_trace_rec *)calloc(t->n + 1u, sizeof *out);
    if (out == NULL) {
        return 1;
    }
    wr_replay(w, t, 0, t->n, SA1G_INJ_PAD | SA1G_INJ_ANALOG | SA1G_INJ_RAND | SA1G_INJ_XMTRX | SA1G_INJ_CAMERA, 0,
              WR_CMP_RAW | WR_CMP_HEAP, out, &res);
    wr_result_print("M1b replay (decoded fields + raw blobs, heap pointers by structure)", &res, t->n);
    wr_diff_init(&others);
    for (i = 0; i < res.steps; i++) {
        const sa1_trace_rec *r = &t->r[i], *m = &out[i];
        if (r->land_count != m->land_count) {
            int32_t dl = (int32_t)(r->land_count - m->land_count);
            land_steps++;
            land_min = dl < land_min ? dl : land_min;
            land_max = dl > land_max ? dl : land_max;
        }
        if (r->rand_state != m->rand_state) {
            rand_steps++;
            rand_ok += wr_rand_ahead(m->rand_state, r->rand_state) != 0;
        }
        wr_compare(r, m, WR_CMP_RAW | WR_CMP_HEAP | WR_CMP_NO_LANDCNT | WR_CMP_NO_RAND, &others, (int)i);
    }
    printf("    land_count differs in %u steps (reference - world %d..%d); rand_state in %u steps (%u with the "
           "reference ahead)\n", (unsigned)land_steps, land_steps ? (int)land_min : 0, land_steps ? (int)land_max : 0,
           (unsigned)rand_steps, (unsigned)rand_ok);
    printf("    every other decoded field and raw blob (heap by structure): %u steps differ\n",
           (unsigned)others.nsteps_bad);
    wr_diff_print(&others, 12);
    if (out_dir != NULL && res.steps > 0) {
        char path[1024];
        snprintf(path, sizeof path, "%s/m1b_%s.bin", out_dir, name);
        if (wr_trace_write(path, &t->h, out, res.steps) == 0) {
            printf("    wrote %s\n", path);
        }
    }
    strict_ok = !res.fault && res.steps == t->n && res.d.nsteps_bad == 0;
    tol_ok = !res.fault && res.steps == t->n && others.nsteps_bad == 0 && (land_steps == 0 || land_min == land_max)
             && rand_ok == rand_steps;
    if (tolerant) {
        CHECK(tol_ok, "M1b %s: identical but for the STG01 water task's land_count / rand residuals (%u records)",
              name, (unsigned)res.steps);
        printf("  %s  M1b %s strict: %u of %u records differ\n", strict_ok ? "ok  " : "note", name,
               (unsigned)res.d.nsteps_bad, (unsigned)res.steps);
    } else {
        CHECK(strict_ok, "M1b %s strict: decoded fields and raw blobs (heap by structure): %u of %u records differ",
              name, (unsigned)res.d.nsteps_bad, (unsigned)res.steps);
        printf("  %s  M1b %s without the STG01 water task's residuals: %s\n", tol_ok ? "ok  " : "note", name,
               tol_ok ? "identical" : "differ");
    }
    if (keep != NULL) {
        *keep = out;
    } else {
        free(out);
    }
    return 0;
}

/* two runs of the same replay in one process write the same records (M1 exit 5) */
static void test_determinism(sa1g_world *w, const sa1g_image *img, const wr_trace *t, const sa1_trace_rec *first)
{
    wr_result res;
    sa1_trace_rec *again = (sa1_trace_rec *)calloc(t->n + 1u, sizeof *again);
    if (again == NULL || first == NULL) {
        free(again);
        return;
    }
    if (boot_at(w, img, &t->h, SA1G_LAND_DISC, SA1G_BOOT_NO_WORLD_UPDATE | SA1G_BOOT_DEATHZONE) == SA1_OK) {
        wr_replay(w, t, 0, t->n, SA1G_INJ_PAD | SA1G_INJ_ANALOG | SA1G_INJ_RAND | SA1G_INJ_XMTRX | SA1G_INJ_CAMERA, 0,
                  WR_CMP_RAW | WR_CMP_HEAP, again, &res);
        CHECK(!res.fault && res.steps == t->n && memcmp(first, again, (size_t)t->n * sizeof *again) == 0,
              "a second boot and replay of the same trace in this process writes byte-identical records (%u)",
              (unsigned)res.steps);
    } else {
        CHECK(0, "second boot for the determinism check");
    }
    free(again);
}

/* ---- live mode: pairs with the host pad, MXCSR, state readout --------------------------------------------- */
static void test_live(sa1g_world *w, const sa1g_image *img, const wr_trace *t)
{
    sa1g_pad pad;
    sa1_state s;
    sa1_status st = SA1_OK;
    uint32_t i, ec8 = 7, frame = 0;
    float p0[3];
    printf("live mode (DISC world, 60 pairs, stick forward from pair 10):\n");
    if (boot_at(w, img, &t->h, SA1G_LAND_DISC, SA1G_BOOT_DEATHZONE) != SA1_OK) {
        CHECK(0, "boot for live mode");
        return;
    }
    w->replay = 0;
    memset(&s, 0, sizeof s);
    s.struct_size = sizeof s;
    sa1g_world_state(w, &s);
    memcpy(p0, s.position, sizeof p0);
    memset(&pad, 0, sizeof pad);
    pad.connected = 1;
    for (i = 0; i < 60 && st == SA1_OK; i++) {
        pad.y1 = i >= 10 ? -127 : 0;
        sa1g_world_set_pad(w, 0, &pad);
        st = sa1g_world_pair(w);
    }
    sa1g_world_rd32(w, SA1G_ADDR_SUB_STEP, &ec8);
    sa1g_world_rd32(w, SA1G_ADDR_FRAME_COUNTER, &frame);
    sa1g_world_state(w, &s);
    CHECK(st == SA1_OK && w->pairs == 60 && w->steps == 120 && ec8 == 1 && frame == 120,
          "60 live pairs: %s, %u steps, B_8C754EC8 %u after the pair, frame counter %u (+2 per pair)",
          sa1g_status_string(st), (unsigned)w->steps, (unsigned)ec8, (unsigned)frame);
    CHECK(st == SA1_OK && (s.position[0] != p0[0] || s.position[2] != p0[2]) && s.action != 0,
          "the stick moved Sonic: (%g, %g, %g) -> (%g, %g, %g), action %d, frame %g, velocity (%g, %g, %g)/s",
          p0[0], p0[1], p0[2], s.position[0], s.position[1], s.position[2], (int)s.action, s.frame, s.velocity[0],
          s.velocity[1], s.velocity[2]);
    if (st != SA1_OK) {
        printf("    fault: %s\n", w->fault.msg);
    }
}

/* ---- sa1w_world_run (sa1_api_world.h): both paths apply sa1g_world_tick's checks before the accumulator ---------- */
/* keep_backlog 1 (the intermediate pairs of a multi-pair sa1_character_tick, sadc docs/research/gsa1_060_pops.md F1)
 * used to add dt to the accumulator of a world that cannot run and, below a whole pair, return SA1_OK */
static void test_run_checks(sa1g_world *w, int booted)
{
    static const float dts[2] = { 0.01f, 0.05f };   /* below one pair, and above */
    double acc0 = w->acc;
    uint64_t pairs0 = w->pairs;
    uint32_t p, i;
    sa1_status st[2], want = booted ? SA1_OK : SA1_ERR_NOT_LOADED;
    int k, same = 1;
    if (!booted) {
        for (i = 0; i < 2; i++) {
            for (k = 0; k < 2; k++) {
                p = 77;
                st[k] = sa1w_world_run(w, dts[i], 1, k, &p);
                same = same && st[k] == SA1_ERR_NOT_LOADED && p == 0 && w->acc == acc0 && w->pairs == pairs0;
            }
        }
        CHECK(same, "sa1w_world_run on a world before its boot, dt 0.01 / 0.05, keep_backlog 0 / 1: %s / %s, no pair, "
              "the accumulator unchanged (%g)", sa1g_status_string(st[0]), sa1g_status_string(st[1]), w->acc);
        {
            sa1g_step_fn step = w->step;
            w->step = NULL;
            st[0] = sa1w_world_run(w, 0.05f, 1, 0, &p);
            st[1] = sa1w_world_run(w, 0.05f, 1, 1, &p);
            w->step = step;
            CHECK(st[0] == SA1_ERR_INVALID_ARGUMENT && st[1] == SA1_ERR_INVALID_ARGUMENT && w->acc == acc0,
                  "no step: %s / %s, the accumulator unchanged", sa1g_status_string(st[0]), sa1g_status_string(st[1]));
        }
        st[1] = sa1w_world_run(w, -1.0f, 1, 1, &p);
        CHECK(st[1] == SA1_ERR_INVALID_ARGUMENT && w->acc == acc0, "a negative dt with keep_backlog: %s",
              sa1g_status_string(st[1]));
        return;
    }
    /* a booted world: a dead one answers with its status before the accumulator; an alive one runs */
    {
        int32_t dead = w->dead;
        sa1_status ds = w->dead_status;
        w->dead = 1;
        w->dead_status = SA1_ERR_GAME_FAULT;
        st[0] = sa1w_world_run(w, 0.01f, 1, 1, &p);
        st[1] = sa1w_world_run(w, 0.05f, 1, 1, &p);
        w->dead = dead;
        w->dead_status = ds;
        CHECK(st[0] == SA1_ERR_GAME_FAULT && st[1] == SA1_ERR_GAME_FAULT && w->acc == acc0 && w->pairs == pairs0,
              "a dead world, keep_backlog 1, dt 0.01 / 0.05: %s / %s, the accumulator unchanged",
              sa1g_status_string(st[0]), sa1g_status_string(st[1]));
    }
    st[0] = sa1w_world_run(w, 0.05f, 1, 1, &p);
    CHECK(st[0] == want && p == 1 && w->pairs == pairs0 + 1,
          "the booted world, keep_backlog 1, dt 0.05: %s, %u pair (the rest stays in the accumulator: %g s)",
          sa1g_status_string(st[0]), (unsigned)p, w->acc);
}

static void test_api(sa1_context *ctx, const char *gdi)
{
    static sa1g_wctx c;
    sa1g_world *w = NULL;
    sa1_input in;
    sa1_state s;
    sa1_status st;
    uint32_t i;
    float pos[3] = { -36.0f, -16.0f, 16.0f };    /* host space (Source preset): SA1 (-9, 4, 4) */
    printf("character API over worlds (Sonic, WINDOW landtable without geometry):\n");
    st = sa1g_wctx_init(&c, ctx, gdi);
    if (st == SA1_OK) {
        st = sa1g_character_world_create(&c, SA1_CHAR_SONIC, pos, 0.0f, &w);
    }
    CHECK(st == SA1_OK && w != NULL && w->booted && w->land_mode == SA1G_LAND_WINDOW && w->window_cap == 191,
          "create: %s, booted, window landtable %08X (room for %u COLs), start SA1 (%g, %g, %g)", sa1g_status_string(st),
          w ? (unsigned)w->landtable : 0u, w ? (unsigned)w->window_cap : 0u, w ? w->start_pos[0] : 0.0f,
          w ? w->start_pos[1] : 0.0f, w ? w->start_pos[2] : 0.0f);
    if (w != NULL) {
        memset(&in, 0, sizeof in);
        in.struct_size = sizeof in;
        for (i = 0; i < 30 && st == SA1_OK; i++) {
            memset(&s, 0, sizeof s);
            s.struct_size = sizeof s;
            st = sa1g_character_world_tick(w, &in, 1.0f / 30.0f, &s);
        }
        CHECK(st == SA1_OK && w->pairs == 30 && s.velocity[2] != 0.0f,
              "30 ticks of 1/30 s with no ground: %s, %u pairs, falling (host velocity z %g/s), action %d",
              sa1g_status_string(st), (unsigned)w->pairs, s.velocity[2], (int)s.action);
        if (st != SA1_OK) {
            printf("    fault: %s\n", w->fault.msg);
        }
    }
    {
        sa1g_world *wt = NULL;
        st = sa1g_character_world_create(&c, SA1_CHAR_TIKAL, NULL, 0.0f, &wt);
        CHECK(st == SA1_ERR_UNSUPPORTED && wt == NULL, "Tikal: %s (own create path, 6.3a)", sa1g_status_string(st));
    }
    sa1g_character_world_destroy(&c, w);
    sa1g_wctx_free(&c);
}

int main(int argc, char **argv)
{
    static sa1g_world w;
    static sa1g_image img_sonic, img_disc;
    sa1_context *ctx = NULL;
    sa1_host host;
    const char *out_dir = NULL, *dump_dir = NULL, *sadc = NULL, *gdi = getenv("SA1_TEST_GDI");
    char path[1024];
    wr_trace stand, run;
    sa1_trace_rec *first = NULL;
    sa1_status st;
    int i, tolerant = 0, free_run = 0;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--out") && i + 1 < argc) {
            out_dir = argv[++i];
        } else if (!strcmp(argv[i], "--dump") && i + 1 < argc) {
            dump_dir = argv[++i];
        } else if (!strcmp(argv[i], "--stage-tolerant")) {
            tolerant = 1;
        } else if (!strcmp(argv[i], "--free")) {
            free_run = 1;
        } else if (sadc == NULL) {
            sadc = argv[i];
        } else {
            gdi = argv[i];
        }
    }
    if (sadc == NULL || gdi == NULL || !*gdi) {
        printf("usage: world_boot_test [--out DIR] [--dump DIR] [--stage-tolerant] [--free] SADC [GDI] (or SA1_TEST_GDI)\n");
        return 2;
    }
    sa1_host_defaults(&host);
    host.log = log_cb;
    host.log_level = SA1_LOG_WARN;
    if (sa1_init(&host, &ctx) != SA1_OK) {
        printf("FAIL: sa1_init\n");
        return 1;
    }
    printf("libsa1 world boot test (M0, M1b): %d-bit, game code: %s\n", (int)(sizeof(void *) * 8), sa1g_game_info());
    if (!sa1g_game_ready()) {
        printf("FAIL: the step's game code is not linked\n");
        return 1;
    }
    snprintf(path, sizeof path, "%s/build/traces/ref/ec1_stand.bin", sadc);
    if (wr_trace_load(path, &stand) != 0) {
        return 1;
    }
    snprintf(path, sizeof path, "%s/build/traces/ref/ec1_run_jump.bin", sadc);
    if (wr_trace_load(path, &run) != 0) {
        return 1;
    }
    st = sa1g_image_compose(ctx, gdi, SA1_CHAR_SONIC, 0, &img_sonic);
    CHECK(st == SA1_OK, "Sonic image from the disc: %s (%u spans, %u patches)", sa1g_status_string(st),
          (unsigned)img_sonic.num_spans, (unsigned)img_sonic.num_patches);
    if (st == SA1_OK) {
        st = sa1g_image_compose(ctx, gdi, SA1_CHAR_SONIC, SA1G_IMAGE_NO_OPTIONAL, &img_disc);
        if (st == SA1_OK) {
            st = sa1g_image_add_stage(&img_disc, gdi, stand.h.level, 0);
        }
    }
    CHECK(st == SA1_OK, "DISC image (Sonic + STG01 at 0x0C900000): %s (%u spans, %u patches)", sa1g_status_string(st),
          (unsigned)img_disc.num_spans, (unsigned)img_disc.num_patches);
    if (st != SA1_OK || sa1g_world_create(&w, NULL) != 0) {
        printf("FAIL: images / world\n");
        return 1;
    }
    printf("sa1w_world_run checks (keep_backlog path):\n");
    test_run_checks(&w, 0);
    test_start_table(&w, &img_sonic, &stand.h);
    test_m0(&w, &img_sonic, &stand, out_dir);
    printf("M1b (own boot, DISC collision):\n");
    test_m1b_one(&w, &img_disc, &stand, "ec1_stand", out_dir, dump_dir, tolerant, &first);
    test_m1b_one(&w, &img_disc, &run, "ec1_run_jump", out_dir, NULL, tolerant, NULL);
    test_determinism(&w, &img_disc, &stand, first);
    free(first);
    if (free_run) {
        test_free_one(&w, &img_disc, &stand, "ec1_stand");
        test_free_one(&w, &img_disc, &run, "ec1_run_jump");
    }
    test_live(&w, &img_disc, &stand);
    test_run_checks(&w, w.booted && !w.dead);
    sa1g_world_destroy(&w);
    test_api(ctx, gdi);
    sa1g_image_free(&img_sonic);
    sa1g_image_free(&img_disc);
    wr_trace_free(&stand);
    wr_trace_free(&run);
    sa1_shutdown(ctx);
    printf("%s: %d failure(s), %d skipped\n", g_fail ? "FAIL" : "PASS", g_fail, g_skip);
    return g_fail ? 1 : 0;
}
