/*
 * follower_replay.c - F2 and F3 of the follower rounds: the reference traces of the game's own "1.5P" Tails (sadc gsa1_rounds_plan.md
 * 12.1, 12.2; gsa1_096_plan.md 6.1 tasks 2, 10 with its review R.8; gsa1_098_plan.md 2.4, 6.2), through the internal boots, on the
 * optimised archives (the public follower calls are gated until the lane's final gate).
 *
 *   follower_replay [--out DIR] [--trace NAME]... SADC [GDI]        (GDI: or $SA1_TEST_GDI)
 *
 * FOLLOW (F2, the A configuration): one world, the references' own stage (level 2 act 0, level 6 act 1), the DISC landtable, the
 *   world's boot with SA1G_BOOT_FOLLOWER_SAME: LoadPlayerTask's slot-1 iteration for character 0, func_8C081AC0. Players 0 and 1 are
 *   compared strictly with NAME.bin and NAME.p1.bin (SA1XP101): every decoded field and raw blob, the AI task's taskwk. Injections:
 *   the record's pad (port A), at the AI exec's first entry of a step XMTRX and the camera taskwk (it runs in list 0, before the
 *   players: Review R.2.5), at the first player entry rand (player 1's: Tails runs first, R.2.4), at player 1's entry its px_*,
 *   and player 0's as every reference has them. Outputs: OUT/NAME.bin (player 0: build_replay.py diffs it with sa1trace.py),
 *   OUT/NAME.p1rec.bin (player 1's records). A difference is printed with its step and field (Review G7).
 *   One thing the world does not have: Windy Valley's list-2 task 0C908AE0 (STG02, no C; the stage start makes it whatever
 *   --no-level-tasks says: a walk of the reference's task lists at step 6, harness trace --dump-ram-at 6) draws 10 rand() every 6
 *   steps after the players. So on level 2 rand_state is not compared bit for bit: the reference must be exactly m draws ahead of the
 *   world (the LCG), m printed; each player's own stream is the record's at its entry.
 *
 * FOLLOWC (F3, the C configuration; the production boot): a world of the follower's kind whose slot 0 is the proxy fed per step
 *   from player 0's records (before the step the leader after record k - 1, right after the executor after record k: the two
 *   refreshes of production), SA1_FOLLOW_GAME_VIEW | SA1_FOLLOW_GAME_HISTORY, the same injections; player 1 and the AI's
 *   taskwk compared strictly. Output: OUT/NAME.followc.p1.bin. What the C configuration cannot have is masked, exactly these
 *   fields and no other, none silently (Review G7, each counted and printed by the gate's output):
 *     rand_state        the global after the step: Sonic's exec, 0C908AE0 and the others draw after Tails' entry; the injection at
 *                       Tails' entry gives him the recorded stream
 *     taskwk.flag 0x04, 0x08, playerwk.flag 0x08   set by CCL when Tails touches Sonic (0x04 his attack): no second player in CCL
 *     the hit itself    in wv_follow_run Sonic's jump (step 236, 1.3 units away) knocks Tails back: mode 13, wtimer 120, in step 237.
 *                       The world has no Sonic to do that: when a record differs and the reference's record before it had the
 *                       contact bit 0x04, that step is counted as "a hit by the leader", player 1 and the AI are reseeded from
 *                       the record (pointer words stay) and the comparison goes on free-running. A difference anywhere else fails.
 *
 * x86 = x64: tests/build_replay.py's V7 compares every output file of the two Windows targets byte for byte.
 */
#include "world_replay.c"

#include <math.h>

#define INJ_FULL (SA1G_INJ_PAD | SA1G_INJ_ANALOG | SA1G_INJ_RAND | SA1G_INJ_XMTRX | SA1G_INJ_CAMERA)
#define INJ_PAD_ONLY (SA1G_INJ_PAD | SA1G_INJ_ANALOG)

static int g_fail;

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

/* ---- the sidecar ---------------------------------------------------------------------------------------------------------- */
typedef struct p1_trace {
    sa1_trace_ext_header h;
    sa1_trace_p1_rec *r;
    uint32_t n;
} p1_trace;

static int p1_load(const char *path, p1_trace *t)
{
    size_t n = 0;
    unsigned char *b = wr_read_file(path, &n);
    memset(t, 0, sizeof *t);
    if (b == NULL || n < sizeof t->h) {
        printf("    cannot read %s\n", path);
        free(b);
        return -1;
    }
    memcpy(&t->h, b, sizeof t->h);
    if (memcmp(t->h.magic, SA1_TRACE_EXT_MAGIC_P1, 8) != 0 || t->h.record_size != sizeof(sa1_trace_p1_rec)
        || n < sizeof t->h + (size_t)t->h.count * sizeof(sa1_trace_p1_rec)) {
        printf("    %s: not a version-1 player-1 sidecar\n", path);
        free(b);
        return -1;
    }
    t->n = t->h.count;
    t->r = (sa1_trace_p1_rec *)malloc((size_t)t->n * sizeof(sa1_trace_p1_rec) + 1u);
    if (t->r != NULL) {
        memcpy(t->r, b + t->h.header_size, (size_t)t->n * sizeof(sa1_trace_p1_rec));
    }
    free(b);
    return t->r != NULL ? 0 : -1;
}

/* ---- the step's injections ----------------------------------------------------------------------------------------------- */
static void fill_inj(sa1g_follow_inj *fi, const sa1_trace_p1_rec *q)
{
    memset(fi, 0, sizeof *fi);
    fi->ai_on = (q->flags & SA1_P1F_AI_ENTERED) != 0;
    memcpy(fi->ai_xmtrx, q->ai_xmtrx, sizeof fi->ai_xmtrx);
    memcpy(fi->ai_cam_raw, q->ai_cam_raw, sizeof fi->ai_cam_raw);
    fi->first_on = q->first_pno != 0xFFFFFFFFu;
    fi->first_rand = q->first_rand;
    fi->p1_on = q->p1.px_calls != 0;
    fi->p1_rand = q->p1.px_rand;
    fi->p1_frame_counter = q->p1.px_frame_counter;
    fi->p1_rings = q->p1.px_rings;
    memcpy(fi->p1_xmtrx, q->p1.px_xmtrx, sizeof fi->p1_xmtrx);
    memcpy(fi->p1_cam_raw, q->p1.px_cam_raw, sizeof fi->p1_cam_raw);
}

/* ---- player 1's record of the world (the harness's fill for pno 1): wr_record on the slot, port B's pad, the px_* of its entry ----- */
static void p1_record(sa1g_world *w, const sa1_trace_rec *in, const sa1g_follow_inj *fi, sa1_trace_rec *r)
{
    int32_t slot = w->slot;
    uint32_t s;
    w->slot = 1;
    wr_record(w, in, r);
    w->slot = slot;
    r->pad_on = wr_g32(w, SA1G_ADDR_PAD_REC + 0x34u + 0x08u);
    r->pad_press = wr_g32(w, SA1G_ADDR_PAD_REC + 0x34u + 0x10u);
    r->analog_ang = wr_g32(w, SA1G_ADDR_ANALOG + 8u);
    s = wr_g32(w, SA1G_ADDR_ANALOG + 12u);
    memcpy(&r->analog_stroke, &s, 4);
    r->px_exec = fi->px_exec;
    r->px_calls = fi->px_calls;
    r->px_rand = fi->px_rand;
    r->px_frame_counter = fi->px_frame_counter;
    r->px_rings = fi->px_rings;
    memcpy(r->px_gravity, fi->px_gravity, sizeof r->px_gravity);
    memcpy(r->px_xmtrx, fi->px_xmtrx, sizeof r->px_xmtrx);
    r->px_cam_twp = fi->px_cam_twp;
    memcpy(r->px_cam_raw, fi->px_cam_raw, sizeof r->px_cam_raw);
}

/* the AI task's taskwk against the record's (by the AI's own pointer words: +0x38, +0x3C by structure) */
static void ai_compare(const sa1g_world *w, const sa1_trace_p1_rec *q, uint32_t *bad, int *first, uint32_t step)
{
    uint32_t tp = wr_g32(w, 0x8C78C248u), twp;
    uint8_t raw[0x40];
    uint32_t off;
    int diff = 0;
    twp = wr_arena(tp) ? wr_g32(w, tp + 0x20u) : 0u;
    if (!(q->flags & SA1_P1F_AI_TASK)) {
        return;
    }
    wr_copy(w, raw, twp, sizeof raw);
    for (off = 0; off + 4 <= sizeof raw; off += 4) {
        if (off == 0x38 || off == 0x3C) {
            continue;
        }
        if (wr_u32(raw + off) != wr_u32(q->ai_twp_raw + off)) {
            if (*bad == 0) {
                printf("      step %u: AI taskwk +0x%02X: reference %08X, world %08X\n", (unsigned)step, (unsigned)off,
                       (unsigned)wr_u32(q->ai_twp_raw + off), (unsigned)wr_u32(raw + off));
            }
            diff = 1;
        }
    }
    if (diff) {
        (*bad)++;
        if (*first < 0) {
            *first = (int)step;
        }
    }
}

/* what the AI wrote for player 1 and the respawns, printed so that the gate's output says the reference really ran them */
static void summary(const char *what, const sa1_trace_p1_rec *r, uint32_t n)
{
    uint32_t i, modes[10] = { 0 }, hi = 0;
    for (i = 0; i < n; i++) {
        int m = r[i].ai_mode;
        if (m >= 0 && m < 10) {
            modes[m]++;
        }
        if (r[i].ai_counter < 512u && r[i].ai_counter > hi) {
            hi = r[i].ai_counter;
        }
    }
    printf("    %s: AI modes 0 x%u, 2 x%u, 4 x%u; highest off-screen counter %u\n", what, (unsigned)modes[0], (unsigned)modes[2],
           (unsigned)modes[4], (unsigned)hi);
}

/* ---- contexts ------------------------------------------------------------------------------------------------------------- */
typedef struct env {
    const char *sadc, *gdi, *out;
} env;

static sa1_context *make_ctx(const env *E, int16_t level, int16_t act, uint32_t chars)
{
    sa1_context *ctx = NULL;
    sa1_host h;
    sa1_load_options lo;
    sa1_collision_static d;
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
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.mode = SA1_COLLISION_MODE_DISC;
    d.disc_level = level;
    d.disc_act = act;
    st = sa1_collision_set_static_ex(ctx, &d);
    if (st != SA1_OK) {
        printf("    DISC collision of stage %d act %d: %s: %s\n", (int)level, (int)act, sa1_status_string(st), sa1_last_error(ctx));
        sa1_shutdown(ctx);
        return NULL;
    }
    return ctx;
}

static sa1_status create_desc(const sa1_trace_header *h, int32_t kind, sa1_character_desc *d)
{
    int k;
    memset(d, 0, sizeof *d);
    d->struct_size = sizeof *d;
    d->kind = kind;
    d->flags = SA1_CREATE_WORLD | SA1_CREATE_SA1_SPACE | SA1_CREATE_ANGLES | SA1_CREATE_STAGE;
    memcpy(d->position, h->start_pos, sizeof d->position);
    for (k = 0; k < 3; k++) {
        d->angle[k] = (int32_t)h->start_ang[k];
    }
    d->level = h->level;
    d->act = h->act;
    return SA1_OK;
}

static void write_records(const env *E, const char *name, const char *suffix, const sa1_trace_header *h, const sa1_trace_rec *r,
                          uint32_t n)
{
    char path[1100];
    if (E->out == NULL || n == 0) {
        return;
    }
    snprintf(path, sizeof path, "%s/%s%s.bin", E->out, name, suffix);
    if (wr_trace_write(path, h, r, n) != 0) {
        CHECK(0, "cannot write %s", path);
    }
}

/* ---- FOLLOW ---------------------------------------------------------------------------------------------------------------- */
static void run_same(const env *E, const char *name, const wr_trace *t, const p1_trace *q)
{
    sa1_context *ctx = make_ctx(E, t->h.level, t->h.act, (1u << SA1_CHAR_SONIC) | (1u << SA1_CHAR_TAILS));
    sa1_character_desc d;
    sa1_character_id id = 0;
    sa1g_world *w;
    sa1g_follow_inj fi;
    sa1_status st;
    sa1_trace_rec *out0, *out1;
    wr_diff d0, d1;
    uint32_t i, steps = 0, aibad = 0, first_mismatch = 0;
    int aifirst = -1, first_first = -1;
    /* see the header: Windy Valley's list-2 task draws after the players */
    const uint32_t how = WR_CMP_RAW | WR_CMP_HEAP | (t->h.level == 2 ? WR_CMP_NO_RAND : 0u);
    uint32_t rand_diff = 0, rand_lost = 0, m_seen[8] = { 0 }, m_count[8] = { 0 };
    printf("FOLLOW %s (A configuration: one world, %u records, stage %d act %d):\n", name, (unsigned)t->n, (int)t->h.level, (int)t->h.act);
    if (ctx == NULL) {
        CHECK(0, "FOLLOW %s: context", name);
        return;
    }
    create_desc(&t->h, SA1_CHAR_SONIC, &d);
    sa1w_test_boot_flags(SA1G_BOOT_FOLLOWER_SAME);
    st = sa1_character_create_ex(ctx, &d, &id);
    sa1w_test_boot_flags(0);
    w = st == SA1_OK ? sa1_character_world(ctx, id) : NULL;
    CHECK(w != NULL, "FOLLOW %s: the world with SA1G_BOOT_FOLLOWER_SAME: %s %s", name, sa1_status_string(st),
          st == SA1_OK ? "" : sa1_last_error(ctx));
    if (w == NULL) {
        sa1_shutdown(ctx);
        return;
    }
    wr_screen_of(w, &t->h);
    out0 = (sa1_trace_rec *)calloc(t->n + 1u, sizeof *out0);
    out1 = (sa1_trace_rec *)calloc(t->n + 1u, sizeof *out1);
    wr_diff_init(&d0);
    wr_diff_init(&d1);
    w->replay = 1;
    w->fol_inj = &fi;
    for (i = 0; i < t->n; i++) {
        const sa1_trace_rec *r = &t->r[i];
        const sa1_trace_p1_rec *pr = &q->r[i];
        wr_inject(w, r, INJ_FULL, 0);
        fill_inj(&fi, pr);
        st = sa1g_world_step(w, (int32_t)r->sub);
        if (st != SA1_OK) {
            printf("    step %u: %s: %s\n", (unsigned)i, sa1g_status_string(st), w->fault.msg);
            break;
        }
        steps++;
        wr_record(w, r, &out0[i]);
        wr_compare(r, &out0[i], how, &d0, (int)i);
        if ((how & WR_CMP_NO_RAND) && r->rand_state != out0[i].rand_state) {
            uint32_t m = wr_rand_ahead(out0[i].rand_state, r->rand_state), k;
            rand_diff++;
            rand_lost += m == 0;
            for (k = 0; m != 0 && k < 8u && m_seen[k] != m && m_seen[k] != 0; k++) {
            }
            if (m != 0 && k < 8u) {
                m_seen[k] = m;
                m_count[k]++;
            }
        }
        p1_record(w, &pr->p1, &fi, &out1[i]);
        wr_compare(&pr->p1, &out1[i], how, &d1, (int)i);
        ai_compare(w, pr, &aibad, &aifirst, i);
        if (fi.first_calls != 0 && pr->first_pno != 0xFFFFFFFFu && fi.first_pno != pr->first_pno) {
            if (first_mismatch++ == 0) {
                first_first = (int)i;
            }
        }
    }
    w->fol_inj = NULL;
    summary(name, q->r, steps);
    wr_diff_print(&d0, 12);
    wr_diff_print(&d1, 12);
    CHECK(steps == t->n && d0.nsteps_bad == 0, "FOLLOW %s player 0 strict: %u of %u records differ (first: step %d)", name,
          (unsigned)d0.nsteps_bad, (unsigned)t->n, d0.first_bad_step);
    CHECK(steps == t->n && d1.nsteps_bad == 0, "FOLLOW %s player 1 strict: %u of %u records differ (first: step %d)", name,
          (unsigned)d1.nsteps_bad, (unsigned)t->n, d1.first_bad_step);
    CHECK(aibad == 0, "FOLLOW %s the AI task's taskwk: %u of %u steps differ (first: step %d)", name, (unsigned)aibad, (unsigned)t->n,
          aifirst);
    if (how & WR_CMP_NO_RAND) {
        uint32_t k;
        printf("    rand_state on level 2: %u steps the reference is ahead of the world by:", (unsigned)rand_diff);
        for (k = 0; k < 8u && m_seen[k] != 0; k++) {
            printf(" %u draws x%u", (unsigned)m_seen[k], (unsigned)m_count[k]);
        }
        printf("\n");
        CHECK(rand_lost == 0, "FOLLOW %s every rand_state difference is the reference some draws ahead (the task 0C908AE0): %u are not",
              name, (unsigned)rand_lost);
    }
    CHECK(first_mismatch == 0, "FOLLOW %s the first player of each step is the recorded one: %u differ (first: step %d)", name,
          (unsigned)first_mismatch, first_first);
    write_records(E, name, "", &t->h, out0, steps);
    write_records(E, name, ".p1rec", &t->h, out1, steps);
    free(out0);
    free(out1);
    sa1_character_destroy(ctx, id);
    sa1_shutdown(ctx);
}

/* ---- FOLLOWC --------------------------------------------------------------------------------------------------------------- */
/*
 * The fields the C configuration cannot have, masked in the comparison (Review G7; exactly these, printed by the gate):
 *   rand_state     the global after the step: on the DC Sonic's exec and the stage's other code draw after Tails' entry (the
 *                  injection at Tails' entry gives him the recorded stream)
 *   rings, px_rings, frame_counter fields stay compared: they are injected or equal
 */
#define FOLLOWC_HOW (WR_CMP_RAW | WR_CMP_HEAP | WR_CMP_NO_RAND)

/* the contact bits of the reference over the world's record: only those bits, the rest of the words stay compared */
static uint32_t mask_contact(const sa1_trace_rec *ref, sa1_trace_rec *mine)
{
    const uint32_t tb = 0x0Cu, pb = 0x08u;
    uint32_t n = 0, v, rv;
    if ((((uint16_t)ref->twp_flag ^ (uint16_t)mine->twp_flag) & tb) != 0) {
        mine->twp_flag = (int16_t)(((uint16_t)mine->twp_flag & ~tb) | ((uint16_t)ref->twp_flag & tb));
        n++;
    }
    v = wr_u32(mine->twp_raw + 4);
    rv = wr_u32(ref->twp_raw + 4);
    v = (v & ~tb) | (rv & tb);
    memcpy(mine->twp_raw + 4, &v, 4);
    if (((uint16_t)ref->pwp_flag ^ (uint16_t)mine->pwp_flag) & pb) {
        mine->pwp_flag = (int16_t)(((uint16_t)mine->pwp_flag & ~pb) | ((uint16_t)ref->pwp_flag & pb));
        n++;
    }
    v = wr_u32(mine->pwp_raw + 0x10);
    rv = wr_u32(ref->pwp_raw + 0x10);
    v = (v & ~pb) | (rv & pb);
    memcpy(mine->pwp_raw + 0x10, &v, 4);
    return n;
}

static void import_blob(sa1g_world *w, int blob, uint32_t g, const uint8_t *src)
{
    uint8_t *dst;
    uint32_t off;
    if (!wr_arena(g) || (g & 0xFFFFFFu) > SA1G_ARENA_SIZE - k_wr_blobs[blob].size) {
        return;
    }
    dst = w->ram + (g & 0xFFFFFFu);
    for (off = 0; off + 4 <= k_wr_blobs[blob].size; off += 4) {
        uint32_t v = wr_u32(src + off), mine = wr_u32(dst + off);
        if (wr_is_ptr_word(blob, off) || (wr_is_union_word(blob, off) && (wr_guest_ptr(v) || wr_guest_ptr(mine)))) {
            continue;
        }
        memcpy(dst + off, src + off, 4);
    }
}

/* player 1 and the AI's taskwk from the record: the state the DC left after a hit no world without a second player can make */
static void reseed_p1(sa1g_world *w, const sa1_trace_p1_rec *pr)
{
    sa1g_player_ptrs pp;
    int32_t slot = w->slot;
    uint32_t tp = wr_g32(w, 0x8C78C248u), twp = wr_arena(tp) ? wr_g32(w, tp + 0x20u) : 0u;
    w->slot = 1;
    sa1g_world_player(w, &pp);
    w->slot = slot;
    import_blob(w, WR_TWP, pp.twp, pr->p1.twp_raw);
    import_blob(w, WR_MWP, pp.mwp, pr->p1.mwp_raw);
    import_blob(w, WR_PWP, pp.pwp, pr->p1.pwp_raw);
    import_blob(w, WR_CSTS, pp.csts, pr->p1.csts_raw);
    import_blob(w, WR_TWP, twp, pr->ai_twp_raw);
}

static void snap_of_record(const sa1_trace_rec *r, sa1g_snap *s)
{
    uint32_t k;
    memset(s, 0, sizeof *s);
    memcpy(s->pos, r->pos, sizeof s->pos);
    memcpy(s->ang, r->ang, sizeof s->ang);
    s->flag = (uint16_t)r->twp_flag & 3u;
    memcpy(s->mspd, r->mspd, sizeof s->mspd);
    memcpy(s->pspd, r->pspd, sizeof s->pspd);
    s->jog_speed = wr_f32(r->pwp_raw + 0xC0);
    s->run_speed = wr_f32(r->pwp_raw + 0xC4);
    for (k = 0; k < 3; k++) {
        s->tnorm[k] = wr_f32(r->csts_raw + 0x1C + 4 * k);
    }
    s->equipment = (uint32_t)r->equipment;
    s->game_kind = 0;
    s->action = r->action;
    s->valid = 1;
}

static void run_proxy(const env *E, const char *name, const wr_trace *t, const p1_trace *q)
{
    sa1_context *ctx = make_ctx(E, t->h.level, t->h.act, 1u << SA1_CHAR_TAILS);
    sa1_character_desc d;
    struct sa1w_follow_create fc;
    sa1g_snap first;
    sa1_character_id id = 0;
    sa1g_world *w;
    sa1g_follow_inj fi;
    sa1_status st;
    sa1_trace_rec *out1;
    wr_diff d1;
    uint32_t i, steps = 0, aibad = 0, nmask = 0, hits = 0, hit_steps[8] = { 0 };
    int aifirst = -1;
    printf("FOLLOWC %s (C configuration: the proxy fed from player 0's records, %u records):\n", name, (unsigned)t->n);
    if (ctx == NULL) {
        CHECK(0, "FOLLOWC %s: context", name);
        return;
    }
    create_desc(&t->h, SA1_CHAR_TAILS, &d);
    memset(&fc, 0, sizeof fc);
    fc.flags = SA1_FOLLOW_GAME_VIEW | SA1_FOLLOW_GAME_HISTORY;
    fc.leader_kind = SA1_CHAR_SONIC;
    /* the first snapshot: the leader as the stage start made him (the header's position and angles) */
    memset(&first, 0, sizeof first);
    memcpy(first.pos, t->h.start_pos, sizeof first.pos);
    memcpy(first.ang, t->h.start_ang, sizeof first.ang);
    first.tnorm[1] = 0.0f;
    first.valid = 1;
    fc.test_first = &first;
    st = sa1w_character_create_follower(ctx, &d, &fc, &id);
    w = st == SA1_OK ? sa1_character_world(ctx, id) : NULL;
    CHECK(w != NULL, "FOLLOWC %s: the follower world: %s %s", name, sa1_status_string(st), st == SA1_OK ? "" : sa1_last_error(ctx));
    if (w == NULL) {
        sa1_shutdown(ctx);
        return;
    }
    wr_screen_of(w, &t->h);
    out1 = (sa1_trace_rec *)calloc(t->n + 1u, sizeof *out1);
    wr_diff_init(&d1);
    w->replay = 1;
    w->fol_inj = &fi;
    for (i = 0; i < t->n; i++) {
        const sa1_trace_rec *r = &t->r[i];
        const sa1_trace_p1_rec *pr = &q->r[i];
        wr_inject(w, r, INJ_PAD_ONLY, 0);
        fill_inj(&fi, pr);
        if (i > 0) {
            snap_of_record(&t->r[i - 1], &w->fol.host_pre);
        } else {
            w->fol.host_pre = first;
        }
        snap_of_record(r, &w->fol.host_mid);
        st = sa1g_world_step(w, (int32_t)r->sub);
        if (st != SA1_OK) {
            printf("    step %u: %s: %s\n", (unsigned)i, sa1g_status_string(st), w->fault.msg);
            break;
        }
        steps++;
        p1_record(w, &pr->p1, &fi, &out1[i]);
        {
            sa1_trace_rec cmp = out1[i];
            wr_diff one;
            nmask += mask_contact(&pr->p1, &cmp);
            wr_diff_init(&one);
            if (wr_compare(&pr->p1, &cmp, FOLLOWC_HOW, &one, (int)i) != 0) {
                if (i > 0 && ((uint16_t)q->r[i - 1].p1.twp_flag & 0x04u) != 0 && hits < 8u) {
                    /* the leader's attack hit him one step ago on the DC: no second player here */
                    hit_steps[hits++] = i;
                    reseed_p1(w, pr);
                } else {
                    wr_compare(&pr->p1, &cmp, FOLLOWC_HOW, &d1, (int)i);
                }
            }
        }
        ai_compare(w, pr, &aibad, &aifirst, i);
    }
    w->fol_inj = NULL;
    wr_diff_print(&d1, 16);
    printf("    masked (CCL contact with the leader, Review G7): the bits 0x0C of taskwk.flag and 0x08 of playerwk.flag differ %u times; "
           "hits by the leader: %u", (unsigned)nmask, (unsigned)hits);
    for (i = 0; i < hits; i++) {
        printf(" (step %u)", (unsigned)hit_steps[i]);
    }
    printf("\n");
    CHECK(steps == t->n && d1.nsteps_bad == 0,
          "FOLLOWC %s player 1 strict (masked: rand_state, the contact bits; see above): %u of %u records differ (first: step %d)",
          name, (unsigned)d1.nsteps_bad, (unsigned)t->n, d1.first_bad_step);
    CHECK(aibad == 0, "FOLLOWC %s the AI task's taskwk: %u of %u steps differ (first: step %d)", name, (unsigned)aibad, (unsigned)t->n,
          aifirst);
    write_records(E, name, ".followc.p1", &t->h, out1, steps);
    free(out1);
    sa1_character_destroy(ctx, id);
    sa1_shutdown(ctx);
}

int main(int argc, char **argv)
{
    static env E;
    const char *names[16];
    uint32_t n = 0, j;
    int i, do_same = 1, do_proxy = 1;
    (void)wr_result_print;
    (void)wr_replay;
    (void)wr_rand_ahead;
    ga_args(&argc, argv);
    E.gdi = getenv("SA1_TEST_GDI");
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--out") && i + 1 < argc) {
            E.out = argv[++i];
        } else if (!strcmp(argv[i], "--trace") && i + 1 < argc && n < 16) {
            names[n++] = argv[++i];
        } else if (!strcmp(argv[i], "--same")) {
            do_proxy = 0;
        } else if (!strcmp(argv[i], "--proxy")) {
            do_same = 0;
        } else if (E.sadc == NULL) {
            E.sadc = argv[i];
        } else {
            E.gdi = argv[i];
        }
    }
    if (E.sadc == NULL || E.gdi == NULL || !*E.gdi || n == 0) {
        printf("usage: follower_replay [--out DIR] --trace NAME... [--same | --proxy] SADC [GDI]\n");
        return 2;
    }
    printf("libsa1 %s follower replay: %d-bit, game code: %s\n", sa1_version_string(), (int)(sizeof(void *) * 8), sa1g_game_info());
    if (!sa1g_game_ready()) {
        printf("FAIL: the step's game code is not linked\n");
        return 1;
    }
    for (j = 0; j < n; j++) {
        char path[1100];
        wr_trace t;
        p1_trace q;
        snprintf(path, sizeof path, "%s/build/traces/ref/%s.bin", E.sadc, names[j]);
        if (wr_trace_load(path, &t) != 0) {
            CHECK(0, "the reference %s", path);
            continue;
        }
        snprintf(path, sizeof path, "%s/build/traces/ref/%s.p1.bin", E.sadc, names[j]);
        if (p1_load(path, &q) != 0 || q.n != t.n) {
            CHECK(0, "the player-1 sidecar %s (%u records against %u)", path, (unsigned)q.n, (unsigned)t.n);
            wr_trace_free(&t);
            continue;
        }
        if (do_same) {
            run_same(&E, names[j], &t, &q);
        }
        if (do_proxy) {
            run_proxy(&E, names[j], &t, &q);
        }
        free(q.r);
        wr_trace_free(&t);
    }
    printf("follower_replay: %d failures\n", g_fail);
    return g_fail ? 1 : 0;
}
