/*
 * transplant_test.c - RAM transplant, M1a (sadc docs/libsa1_integration.md 9.1 V4, 11 "First milestone").
 *
 * A 16 MB RAM image of the reference simulator at the start of step N (a private pure harness build:
 * `sa1h_<tag>.exe trace --level 1 --act 0 --no-objects --input @harness/tests/sim/<script>.txt
 * --dump-ram-at 0,1 --out build/traces/m1_<script>`; generated locally, never committed) becomes the
 * arena of a 64-bit world (sa1g_world_transplant): the original landtable 0x0C981554 in the stage
 * overlay, the lowered land gathers 8C052518 / 8C0522C0, every task of the reference RAM. Tasks whose
 * exec has no native code are frozen: the camera 8C0197BA, the sky 8C052FA0, the stage tasks of the
 * STG01 overlay (0C900000 and the tasks it creates in step 0: water 0C909FDA, ...), sound and effect
 * tasks; the landtable manager 8C0519C0 runs its mode-0 set-up but not its per-frame draw step
 * 8C051D50. Every step gets the record's pad (raw input through the real pad update, then on / press /
 * analog), sub step, and at player-exec entry its rand, XMTRX and camera taskwk (trace v2).
 *
 * Exit (strict, the default): decoded fields and raw twp / mwp / pwp / csts blobs, pointers included
 * (physically), identical to the reference in every replayed record.
 *
 * Residuals the frozen STG01 tasks explain (reported; accepted with --stage-tolerant):
 *   land_count  the reference gathers one more entry: the water plane (DynamicCOL attr 0x88000002) that
 *               the water task 0C909FDA registers in step 0 and moves with the player (on a 25-unit
 *               grid); a transplant from step 0 has no such task, one from step 1 has it frozen;
 *   rand_state  the reference is m >= 1 rand() draws ahead after some steps: draws of the water task
 *               after the player's exec (the next step's injected px_rand resynchronises);
 *   pointers    from step 0 the frozen camera / stage tasks do not allocate their step-0 heap blocks,
 *               so heap addresses differ (identical by structure, sa1trace.py --heap-by-structure).
 *
 *   transplant_test [--out DIR] [--from N] [--steps N] [--stage-tolerant] TRACE.bin RAM [TRACE.bin RAM ...]
 *   transplant_test [--out DIR] [--from N] [--stage-tolerant] --sadc SADC
 *          ec1_stand and ec1_run_jump: SADC/build/traces/ref/<n>.bin with SADC/build/traces/m1_<n>.step<N>.ram
 * --from N: the RAM image is the start of step N; records N.. are replayed. With --out the world's records
 * are written as SA1 traces (DIR/<trace name>, DIR/from<N>_<trace name> for N > 0; records N..) for
 * `sa1trace.py diff REF OUT --raw`.
 */
#include "world_replay.c"

static void log_cb(void *user, int32_t level, const char *msg)
{
    (void)user;
    if (level >= SA1_LOG_WARN) {
        printf("    [libsa1 %s] %s\n", level >= SA1_LOG_ERROR ? "error" : "warn", msg);
    }
}

static const char *base_name(const char *p)
{
    const char *b = p, *s;
    for (s = p; *s; s++) {
        if (*s == '/' || *s == '\\') {
            b = s + 1;
        }
    }
    return b;
}

typedef struct opts {
    const char *out_dir;
    uint32_t from, max_steps;
    int stage_tolerant;
} opts;

/* one transplant replay; 0 = passed */
static int run_one(sa1g_world *w, const char *trace_path, const char *ram_path, const opts *o)
{
    wr_trace t;
    wr_result res, rest;
    unsigned char *ram;
    size_t n = 0;
    sa1_trace_rec *out = NULL;
    sa1_status st;
    uint32_t end, i, land_steps = 0, rand_steps = 0, rand_explained = 0, rand_max_ahead = 0;
    int32_t land_min = 0x7FFFFFFF, land_max = -0x7FFFFFFF;
    wr_diff others;
    int strict_ok, tolerant_ok;
    printf("%s from %s (records %u..):\n", base_name(trace_path), base_name(ram_path), (unsigned)o->from);
    if (wr_trace_load(trace_path, &t) != 0) {
        return 1;
    }
    if (o->from >= t.n) {
        printf("  FAIL  --from %u is past the trace's %u records\n", (unsigned)o->from, (unsigned)t.n);
        wr_trace_free(&t);
        return 1;
    }
    ram = wr_read_file(ram_path, &n);
    if (ram == NULL || n != SA1G_ARENA_SIZE) {
        printf("  FAIL  cannot read the 16 MB RAM image %s\n", ram_path);
        free(ram);
        wr_trace_free(&t);
        return 1;
    }
    st = sa1g_world_transplant(w, ram, n);
    free(ram);
    if (st != SA1_OK) {
        printf("  FAIL  transplant: %s\n", sa1g_status_string(st));
        wr_trace_free(&t);
        return 1;
    }
    printf("  arena from the RAM image: character %d, stage %02X%02X, landtable %08X (trace header %08X)\n",
           (int)w->chr, (unsigned)w->level, (unsigned)w->act, (unsigned)w->landtable, (unsigned)t.h.landtable);
    end = o->max_steps && o->from + o->max_steps < t.n ? o->from + o->max_steps : t.n;
    out = (sa1_trace_rec *)calloc(end - o->from + 1u, sizeof *out);
    if (out == NULL) {
        wr_trace_free(&t);
        return 1;
    }
    wr_replay(w, &t, o->from, end, SA1G_INJ_PAD | SA1G_INJ_ANALOG | SA1G_INJ_RAND | SA1G_INJ_XMTRX | SA1G_INJ_CAMERA, 1,
              WR_CMP_RAW, out, &res);
    wr_result_print("strict (decoded fields + raw blobs, pointers physical)", &res, end - o->from);
    printf("    frozen execs skipped: %u (last %08X)", (unsigned)w->frozen_skips, (unsigned)w->frozen_last);
    for (i = 0; i < w->num_xhooks; i++) {
        printf("; override %08X: %u calls", (unsigned)w->xhooks[i].exec, (unsigned)w->xhooks[i].calls);
    }
    printf("\n");
    /* the residuals the frozen stage tasks explain */
    wr_diff_init(&others);
    for (i = 0; i < res.steps; i++) {
        const sa1_trace_rec *r = &t.r[o->from + i], *m = &out[i];
        if (r->land_count != m->land_count) {
            int32_t dl = (int32_t)(r->land_count - m->land_count);
            land_steps++;
            land_min = dl < land_min ? dl : land_min;
            land_max = dl > land_max ? dl : land_max;
        }
        if (r->rand_state != m->rand_state) {
            uint32_t a = wr_rand_ahead(m->rand_state, r->rand_state);
            rand_steps++;
            if (a != 0) {
                rand_explained++;
                rand_max_ahead = a > rand_max_ahead ? a : rand_max_ahead;
            }
        }
        wr_compare(r, m, WR_CMP_RAW | WR_CMP_HEAP | WR_CMP_NO_LANDCNT | WR_CMP_NO_RAND, &others, (int)(o->from + i));
    }
    memset(&rest, 0, sizeof rest);
    printf("    land_count differs in %u steps (reference - world: %d..%d)\n", (unsigned)land_steps,
           land_steps ? (int)land_min : 0, land_steps ? (int)land_max : 0);
    printf("    rand_state differs in %u steps, %u of them with the reference 1..%u draws ahead of the world\n",
           (unsigned)rand_steps, (unsigned)rand_explained, (unsigned)rand_max_ahead);
    printf("    every other decoded field and raw blob (pointers by structure): %u steps differ\n",
           (unsigned)others.nsteps_bad);
    wr_diff_print(&others, 12);
    if (o->out_dir != NULL && res.steps > 0) {
        char path[1024];
        if (o->from != 0) {
            snprintf(path, sizeof path, "%s/from%u_%s", o->out_dir, (unsigned)o->from, base_name(trace_path));
        } else {
            snprintf(path, sizeof path, "%s/%s", o->out_dir, base_name(trace_path));
        }
        if (wr_trace_write(path, &t.h, out, res.steps) == 0) {
            printf("    wrote %s (records %u..%u)\n", path, (unsigned)o->from, (unsigned)(o->from + res.steps - 1));
        }
    }
    strict_ok = !res.fault && res.steps == end - o->from && res.d.nsteps_bad == 0;
    tolerant_ok = !res.fault && res.steps == end - o->from && others.nsteps_bad == 0
                  && (land_steps == 0 || land_min == land_max) && rand_explained == rand_steps;
    printf("  %s  M1a %s strict: decoded fields and raw twp/mwp/pwp/csts blobs (pointers physical): %u of %u "
           "records differ\n", strict_ok ? "ok  " : (o->stage_tolerant ? "note" : "FAIL"), base_name(trace_path),
           (unsigned)res.d.nsteps_bad, (unsigned)res.steps);
    printf("  %s  M1a %s with the frozen-STG01 residuals (land_count constant offset, rand draws ahead, pointers by "
           "structure): %s\n", tolerant_ok ? "ok  " : "FAIL", base_name(trace_path), tolerant_ok ? "identical" : "differ");
    free(out);
    wr_trace_free(&t);
    return (o->stage_tolerant ? tolerant_ok : strict_ok) ? 0 : 1;
}

int main(int argc, char **argv)
{
    static sa1g_world w;
    sa1_context *ctx = NULL;
    sa1_host host;
    const char *sadc = NULL;
    const char *pairs[32];
    char paths[4][1024];
    opts o;
    int np = 0, i, fails = 0;
    memset(&o, 0, sizeof o);
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--out") && i + 1 < argc) {
            o.out_dir = argv[++i];
        } else if (!strcmp(argv[i], "--steps") && i + 1 < argc) {
            o.max_steps = (uint32_t)strtoul(argv[++i], NULL, 0);
        } else if (!strcmp(argv[i], "--from") && i + 1 < argc) {
            o.from = (uint32_t)strtoul(argv[++i], NULL, 0);
        } else if (!strcmp(argv[i], "--stage-tolerant")) {
            o.stage_tolerant = 1;
        } else if (!strcmp(argv[i], "--sadc") && i + 1 < argc) {
            sadc = argv[++i];
        } else if (np < 32) {
            pairs[np++] = argv[i];
        }
    }
    if (sadc != NULL) {
        snprintf(paths[0], sizeof paths[0], "%s/build/traces/ref/ec1_stand.bin", sadc);
        snprintf(paths[1], sizeof paths[1], "%s/build/traces/m1_ec1_stand.step%u.ram", sadc, (unsigned)o.from);
        snprintf(paths[2], sizeof paths[2], "%s/build/traces/ref/ec1_run_jump.bin", sadc);
        snprintf(paths[3], sizeof paths[3], "%s/build/traces/m1_ec1_run_jump.step%u.ram", sadc, (unsigned)o.from);
        np = 0;
        for (i = 0; i < 4; i++) {
            pairs[np++] = paths[i];
        }
    }
    if (np == 0 || (np & 1)) {
        printf("usage: transplant_test [--out DIR] [--from N] [--steps N] [--stage-tolerant] TRACE.bin RAM [...]\n"
               "       transplant_test [--out DIR] [--from N] [--stage-tolerant] --sadc SADC\n");
        return 2;
    }
    sa1_host_defaults(&host);
    host.log = log_cb;
    host.log_level = SA1_LOG_WARN;
    if (sa1_init(&host, &ctx) != SA1_OK) {
        printf("FAIL: sa1_init\n");
        return 1;
    }
    printf("libsa1 transplant test (M1a): %d-bit, game code: %s\n", (int)(sizeof(void *) * 8), sa1g_game_info());
    if (!sa1g_game_ready()) {
        printf("FAIL: the step's game code is not linked\n");
        return 1;
    }
    if (sa1g_world_create(&w, NULL) != 0) {
        printf("FAIL: world creation\n");
        return 1;
    }
    for (i = 0; i + 1 < np; i += 2) {
        fails += run_one(&w, pairs[i], pairs[i + 1], &o);
    }
    sa1g_world_destroy(&w);
    sa1_shutdown(ctx);
    printf("%s: %d of %d transplant replays fail (%s)\n", fails ? "FAIL" : "PASS", fails, np / 2,
           o.stage_tolerant ? "frozen-STG01 residuals accepted" : "strict");
    return fails ? 1 : 0;
}
