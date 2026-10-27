/*
 * replay_test.c - M1c / M1 / M1.1 / Phase 7: the public character API on dump geometry against the reference
 * traces (sadc docs/libsa1_integration.md 9.1 V5-V7, 11 "First milestone", Phase 7 "all 1ST_READ characters").
 *
 *   replay_test [--out DIR] [--land PREFIX] [--trace NAME]... [--tolerant] [--no-stage-tasks] [--pure] [--no-disc]
 *               [--dump-ram STEP] SADC [GDI]
 *   (GDI: or $SA1_TEST_GDI; NAME default: ec1_stand ec1_run_jump (M1), ec1_turns ec1_jump_hold ec1_spindash
 *   (M1.1), tails_sh1 knux_rm3 amy_tp2 gamma_fe1 big_ec3 (Phase 7); PREFIX default per trace:
 *   SADC/build/traces/land/L<level>A<act> of the trace header's stage, e.g. L01A0, L04A0, L05A2)
 *
 * The public path, per landtable dump (one context each): sa1_init (a counting allocator), sa1_load_disc (the
 * characters of the traces on that land), the --dump-land v2 landtable of the trace's stage (original
 * transforms, raw COL flags, SA1 space) through sa1_collision_set_static_ex in FAITHFUL mode,
 * sa1_character_create_ex(<the header's character>, SA1_CREATE_WORLD) at the trace header's start and stage
 * (bit for bit: SA1_CREATE_SA1_SPACE | ANGLES | STAGE) with the stage's own tasks (SA1_CREATE_STAGE_TASKS: the world
 * image holds the stage overlay STGnn from the disc in FAITHFUL mode too, and the stage start's level tasks run as
 * far as the stage's code is decompiled: src/world/sa1_world_stage.c; --no-stage-tasks: without them, for
 * information). The character's world then runs single 60 Hz steps with
 * each record's sub step in replay mode: trace injection is a test facility, so the test drives the character's
 * world (the internal sa1_character_world) with the world tests' helpers (world_replay.c): per record the pad
 * (raw stick / buttons through the real pad update, then the record's on / press / analog), and at the first
 * entry of player 0's exec its rand, XMTRX and camera blob (trace v2 px_*).
 *
 *   V6 (free run)  from creation; every record compared like sa1trace.py diff --ulps 0 (decoded fields) and
 *                  --raw --heap-by-structure (raw blobs; pointers to the synthesized landtable objects by COL
 *                  index); the public sa1_character_state_ex must equal the record at every step.
 *                  Written to OUT/<name>.bin for `sa1trace.py diff REF OUT --ulps 0`.
 *   V5 (--reseed)  as V6, but before record i the player blobs of record i-1 (taskwk, motionwk, playerwk, csts;
 *                  pointer words stay the world's) are imported: every step starts from the reference's state.
 *                  OUT/<name>.reseed.bin; the first failing step and field are reported.
 *   pure (--pure)  information: only the pad and analog injected (the world's own rand, XMTRX, camera).
 *   --dump-ram N   the V6 world's arena at the start of step N: OUT/<name>.stepN.world.ram (debug ladder 9.3: a
 *                  region diff against the harness's trace --dump-ram-at N, e.g. with a task-list walk).
 *   DISC           the same V6 replay with sa1_collision_set_static_ex(DISC, the trace's stage) in a second
 *                  context: its records must equal FAITHFUL's (M1 exit 4). OUT/<name>.disc.bin. Stages whose
 *                  overlay the collision adapter does not know (sa1_coladapt.c k_stage_files / the manifest) are
 *                  reported as skipped.
 *   V7             two V6 runs in one process write byte-identical records; the host MXCSR is unchanged after
 *                  every step (a custom value is set around the replay); the context allocator is not called
 *                  inside steps or ticks. (tests/build_replay.py compares the outputs of x86_64-windows and
 *                  x86-windows byte for byte and builds x86_64-linux.)
 *   live           sa1_character_tick through the public API (Sonic on L01A0): whole pairs from the accumulator,
 *                  the stick forward with a camera yaw moves the character along that yaw in host space, the
 *                  state's yaw follows the motion, the mesh output, sa1_poll_events.
 *
 * Strict (the default, the gate): every record of V6, V5 and DISC identical in every decoded field and raw blob,
 * DISC = FAITHFUL, and no KNOWN ISSUE (a known issue is a failure). k_stage_notes says what each stage's own tasks
 * do to the player (the evidence: harness trace --log-sdk / --dump-ram-at of the references). --tolerant (for
 * information, e.g. with --no-stage-tasks) passes a record that differs ONLY in the ways stage tasks show (a
 * constant land_count offset: DynamicCOL entries of stage tasks that every gather counts; rand_state: the
 * reference exactly m draws ahead, rand() draws after the player; on RM3 pwp->lclop kept by the reference where
 * the world's sphere query is empty) and prints world state the records do not hold (Big's lure task) as a KNOWN
 * ISSUE. Character-private heap pointers that sa1trace.py does not list (Big's pwp->free.ptr[1]
 * / [2]) are compared by structure (k_char_ptr_words). When the
 * reference's player reaches the STG01 water plane itself (its water probe, playerwk.shadow Attr_top /
 * _Attr_top at pwp+0x1E4 / +0x1F4, holds the plane's attribute 0x88000002; PGetSpeed 8C072174 then applies water
 * physics: ec1_spindash from step 144, rolling below y = -1.5) and the world has no stage tasks, the later records
 * cannot match: --tolerant verifies the records before that step and reports the rest as a KNOWN ISSUE.
 */
#include "world_replay.c"

#include <math.h>
#include <xmmintrin.h>

#include "sa1_api_world.h"
#include "sa1_coladapt.h"

static int g_fail, g_skip, g_known;
static uint32_t g_create_flags = SA1_CREATE_STAGE_TASKS;   /* --no-stage-tasks: 0 */

/* the attribute of the STG01 ocean task's water plane (0C909FDA: DynamicCOL_Add 8C0528AE(0x88000002, ...), model
 * D_0CAA1838, moved in 25-unit steps under the player): no libsa1 world has it */
#define WATER_TASK_ATTR 0x88000002u
static long g_allocs, g_frees;

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

static void *alloc_cb(void *user, size_t n)
{
    (void)user;
    g_allocs++;
    return ga_malloc(n);        /* tests/guard_alloc.h: malloc unless SA1_GUARD_ALLOC / --guard-alloc */
}

static void free_cb(void *user, void *p)
{
    (void)user;
    if (p != NULL) {
        g_frees++;
    }
    ga_free(p);
}

/* ---- the reference traces ------------------------------------------------------------------------------ */
typedef struct trace_def {
    const char *name;
    const char *tag;            /* milestone */
} trace_def;

static const trace_def k_traces[] = {
    { "ec1_stand", "M1" }, { "ec1_run_jump", "M1" },
    { "ec1_turns", "M1.1" }, { "ec1_jump_hold", "M1.1" }, { "ec1_spindash", "M1.1" },
    { "tails_sh1", "P7" }, { "knux_rm3", "P7" }, { "amy_tp2", "P7" }, { "gamma_fe1", "P7" }, { "big_ec3", "P7" },
};

/* What the stage overlay's own tasks do to the player in the reference of each stage, as measured with a pure
 * harness build (trace --log-sdk with --sdk-list hooks on rand 8C10D334 and on the tasks' execs; --dump-ram-at 0,1,2
 * and a walk of the task lists B_8C79F04C[] and DynamicCOL B_8C759708[]), and what the world has of them with
 * SA1_CREATE_STAGE_TASKS (src/world/sa1_world_stage.c). Stages without a line (TP2 0301, FE1 0A00): no stage task
 * acts on the player. */
#define SN_LCLOP_KEPT 0x1u      /* a stage task's DynamicCOL keeps pwp->lclop (pwp+0x74) where the world clears it */

typedef struct stage_note {
    int16_t level, act;
    uint32_t flags;             /* SN_* */
    const char *text;
    const char *stub;           /* a cause on libsa1's side (a boundary stub): reported as a KNOWN ISSUE when the
                                   replay shows rand_state residuals */
} stage_note;

static const stage_note k_stage_notes[] = {
    { 1, 0, 0, "the level task 0C900000 (list 0; asm only: the world's stand-in) creates in step 0 the ocean 0C909F20 "
               "-> 0C909FDA (list 3: DynamicCOL 0x88000002, a water plane at y -1.5 that follows the player in 25-unit "
               "steps: land_count + 1, the water probe of ec1_spindash from step 144) and the footprints O_ASIATO "
               "-> 0C9061DA (list 3, after the player: one rand() per print on sand, attr 0x100000)", NULL },
    { 1, 2, 0, "the level task 0C900000 (stand-in) creates the footprints O_ASIATO -> 0C9061DA (Big's print set: "
               "rand() once in 18 steps, after the player); no DynamicCOL. BigMain's particle spawner 8C046A08 "
               "(decompiled 2026-10-20) draws 2 rand() for each of the 20 particles of steps 179 and 193", NULL },
    { 4, 0, 0, "Tails' Speed Highway race task 8C07FC70 (list 0, created by LoadPlayerTask's slot-1 call 8C07F560 "
               "through 8C07FC4C; code without C, not in the world: sa1_world_boot.c) draws rand() 3 times per step before "
               "the player in 413 of 414 steps, 0C90B4B6 (STG04) once and 8C046E26 twice in one step: all absorbed by "
               "the px_rand injection; nothing after the player", NULL },
    { 5, 2, SN_LCLOP_KEPT,
      "the level task 0C911E0A (list 0; lowered STG05 C) runs the act setup 0C9122EE in step 0; its child 0C913028 "
      "(Knuckles only) registers DynamicCOL attr 0x1001 on the pool object 8C7606DC (model 0CA399DC, 44 points, "
      "centre (-2649, 622, -2181), r 3585) in step 1: land_count + 1 from step 1; from step 141 (Knuckles gliding, no "
      "static object in the sphere query) 8C073160 collects that object and keeps pwp->lclop (COL 57's object "
      "0CA0EEA0)", NULL },
};

static const stage_note *stage_note_of(int16_t level, int16_t act)
{
    size_t i;
    for (i = 0; i < sizeof k_stage_notes / sizeof k_stage_notes[0]; i++) {
        if (k_stage_notes[i].level == level && k_stage_notes[i].act == act) {
            return &k_stage_notes[i];
        }
    }
    return NULL;
}

/* Character-private pointer words of the player blobs that sa1trace.py's PTR_WORDS / UNION_WORDS do not list: heap
 * pointers, compared by structure (both in the heap; with an exec, the world's word must point to a task running
 * it) and never imported by V5. */
typedef struct char_ptr_word {
    int32_t chr;
    uint16_t off;               /* in pwp_raw */
    uint32_t exec;              /* 0: any */
    const char *what;
} char_ptr_word;

static const char_ptr_word k_char_ptr_words[] = {
    { SA1_CHAR_BIG, 0x80, 0, "pwp->free.ptr[1]: the cast task of 8C0E6620 (BigMain free.l[0] bit 0)" },
    { SA1_CHAR_BIG, 0x84, 0x8C0E943Cu, "pwp->free.ptr[2] = B_8C7A40C8: Big's lure task (8C0E6DA0, exec 8C0E943C)" },
};

static const char_ptr_word *char_ptr_word_of(int32_t chr, int blob, uint32_t off)
{
    size_t i;
    for (i = 0; blob == WR_PWP && i < sizeof k_char_ptr_words / sizeof k_char_ptr_words[0]; i++) {
        if (k_char_ptr_words[i].chr == chr && k_char_ptr_words[i].off == off) {
            return &k_char_ptr_words[i];
        }
    }
    return NULL;
}

static int heap_ptr(uint32_t v)
{
    uint32_t q = v & 0x1FFFFFFFu;
    return wr_guest_ptr(v) && q >= (SA1G_ADDR_HEAP & 0x1FFFFFFFu) && q < (SA1G_ADDR_HEAP & 0x1FFFFFFFu) + SA1G_HEAP_SIZE;
}

/* the character's pointer words of `mine` (a world record) that mean what the reference's mean get the reference's
 * value; w: the world (NULL: no exec check); returns the number of words canonicalized */
static uint32_t canon_char_words(const sa1g_world *w, int32_t chr, const sa1_trace_rec *ref, sa1_trace_rec *mine)
{
    size_t i;
    uint32_t n = 0;
    for (i = 0; i < sizeof k_char_ptr_words / sizeof k_char_ptr_words[0]; i++) {
        const char_ptr_word *c = &k_char_ptr_words[i];
        uint32_t a, b;
        if (c->chr != chr) {
            continue;
        }
        a = wr_u32(ref->pwp_raw + c->off);
        b = wr_u32(mine->pwp_raw + c->off);
        if (a == b || !heap_ptr(a) || !heap_ptr(b)) {
            continue;
        }
        if (w != NULL && c->exec != 0 && (wr_g32(w, b + 0x10u) & 0x1FFFFFFFu) != (c->exec & 0x1FFFFFFFu)) {
            continue;
        }
        memcpy(mine->pwp_raw + c->off, &a, 4);
        n++;
    }
    return n;
}

/* ---- environment ------------------------------------------------------------------------------------ */
#define MAX_JOBS 16
#define MAX_LANDS 8

/* one landtable dump = one FAITHFUL context (and a DISC context of the same stage) */
typedef struct land {
    char prefix[1024];
    int16_t level, act;
    uint32_t chars;             /* SA1_CHAR_* bits of the traces on this land */
    sa1_context *ctx;           /* FAITHFUL */
    sa1_context *dctx;          /* DISC */
    sa1ca_dump dump;
    int dump_ok;
    uint32_t *ref_obj;          /* pObject of each COL of the reference landtable (PREFIX.col) */
    uint32_t ncol;
    uint32_t *faith_obj;        /* the synthesized landtable's objects, same COL order */
    uint32_t faith_lt;
} land;

typedef struct env {
    const char *sadc, *gdi, *out;
    const char *land_override;
    int strict, pure, disc;
    land lands[MAX_LANDS];
    uint32_t nlands;
} env;

static sa1_context *make_ctx(const env *E, uint32_t chars)
{
    sa1_context *ctx = NULL;
    sa1_host h;
    sa1_load_options lo;
    sa1_status st;
    sa1_host_defaults(&h);
    h.log = log_cb;
    h.log_level = SA1_LOG_WARN;
    h.alloc = alloc_cb;
    h.free = free_cb;
    if (sa1_init(&h, &ctx) != SA1_OK) {
        return NULL;
    }
    memset(&lo, 0, sizeof lo);
    lo.struct_size = sizeof lo;
    lo.characters = chars ? chars : 1u << SA1_CHAR_SONIC;
    lo.flags = SA1_LOAD_DEFAULT;
    st = sa1_load_disc(ctx, E->gdi, &lo);
    if (SA1_FAILED(st)) {
        printf("    sa1_load_disc: %s: %s\n", sa1_status_string(st), sa1_last_error(ctx));
        sa1_shutdown(ctx);
        return NULL;
    }
    return ctx;
}

/* pObject of every COL of the dumped landtable (sa1_land_col.object) */
static uint32_t *load_col_objects(const char *prefix, uint32_t *ncol)
{
    char path[1100];
    size_t n = 0;
    unsigned char *b;
    sa1_land_col_header h;
    uint32_t *o, i;
    *ncol = 0;
    snprintf(path, sizeof path, "%s.col", prefix);
    b = wr_read_file(path, &n);
    if (b == NULL || n < sizeof h) {
        free(b);
        return NULL;
    }
    memcpy(&h, b, sizeof h);
    if (h.magic != SA1_LANDCOL_MAGIC || h.col_size != sizeof(sa1_land_col) || n < sizeof h + (size_t)h.ncol * sizeof(sa1_land_col)) {
        free(b);
        return NULL;
    }
    o = (uint32_t *)calloc(h.ncol + 1u, sizeof *o);
    for (i = 0; o != NULL && i < h.ncol; i++) {
        sa1_land_col c;
        memcpy(&c, b + sizeof h + (size_t)i * sizeof c, sizeof c);
        o[i] = c.object;
    }
    free(b);
    *ncol = h.ncol;
    return o;
}

/* ---- comparison helpers ------------------------------------------------------------------------------ */
/* a copy of a world record whose pointers to synthesized landtable objects (pwp->lclop ...) name the
 * reference's object of the same COL index: "pointers to synthetic objects compared by structure" */
static void canon_objects(const land *L, sa1_trace_rec *r)
{
    int k;
    if (L == NULL || L->faith_obj == NULL || L->ref_obj == NULL) {
        return;
    }
    for (k = 0; k < WR_CAM; k++) {
        uint8_t *b = (uint8_t *)r + k_wr_blobs[k].off;
        uint32_t off, i;
        for (off = 0; off + 4 <= k_wr_blobs[k].size; off += 4) {
            uint32_t v = wr_u32(b + off);
            if (v == 0 || !(wr_is_ptr_word(k, off) || wr_is_union_word(k, off))) {
                continue;
            }
            for (i = 0; i < L->ncol; i++) {
                if ((v & 0x1FFFFFFFu) == (L->faith_obj[i] & 0x1FFFFFFFu)) {
                    memcpy(b + off, &L->ref_obj[i], 4);
                    break;
                }
            }
        }
    }
}

/* record i-1's player blobs into the world's (V5): every word but the pointer words, which stay the world's */
static void import_blob(sa1g_world *w, int32_t chr, int blob, uint32_t g, const uint8_t *src)
{
    uint8_t *dst;
    uint32_t off;
    if (!wr_arena(g) || (g & 0xFFFFFFu) > SA1G_ARENA_SIZE - k_wr_blobs[blob].size) {
        return;
    }
    dst = w->ram + (g & 0xFFFFFFu);
    for (off = 0; off + 4 <= k_wr_blobs[blob].size; off += 4) {
        uint32_t v = wr_u32(src + off), mine = wr_u32(dst + off);
        if (wr_is_ptr_word(blob, off) || (wr_is_union_word(blob, off) && (wr_guest_ptr(v) || wr_guest_ptr(mine)))
            || char_ptr_word_of(chr, blob, off) != NULL) {
            continue;
        }
        memcpy(dst + off, src + off, 4);
    }
}

static void reseed(sa1g_world *w, int32_t chr, const sa1_trace_rec *prev)
{
    sa1g_player_ptrs pp;
    sa1g_world_player(w, &pp);
    import_blob(w, chr, WR_TWP, pp.twp, prev->twp_raw);
    import_blob(w, chr, WR_MWP, pp.mwp, prev->mwp_raw);
    import_blob(w, chr, WR_PWP, pp.pwp, prev->pwp_raw);
    import_blob(w, chr, WR_CSTS, pp.csts, prev->csts_raw);
}

/* the public state (sa1_character_state_ex) against the world's record: every decoded field bit for bit,
 * and the host-space position / yaw of the Source preset (host = 4 (x, -z, y), yaw = -ang.y) */
static int state_mismatch(sa1_context *ctx, sa1_character_id id, const sa1_trace_rec *m, char *msg, size_t n)
{
    sa1_state_ex x;
    float hp[3];
    float yaw;
    int k;
    memset(&x, 0, sizeof x);
    x.struct_size = sizeof x;
    if (sa1_character_state_ex(ctx, id, &x) != SA1_OK) {
        snprintf(msg, n, "sa1_character_state_ex failed");
        return 1;
    }
    hp[0] = m->pos[0] * 4.0f;
    hp[1] = -m->pos[2] * 4.0f;
    hp[2] = m->pos[1] * 4.0f;
    yaw = (float)(-(double)(int16_t)(uint16_t)(m->ang[1] & 0xFFFFu) * (6.28318530717958647692 / 65536.0));
#define SM(cond, what)                                                         \
    if (!(cond)) {                                                             \
        snprintf(msg, n, "%s", what);                                          \
        return 1;                                                              \
    }
    SM(x.flags & SA1_STATE_TICKING, "flags: not ticking");
    SM(((x.flags & SA1_STATE_ON_GROUND) != 0) == ((m->twp_flag & 3) != 0), "flags: on ground");
    SM(x.mode == m->mode && x.smode == m->smode, "mode / smode");
    SM(x.game_flags == (uint16_t)m->twp_flag && x.player_flags == (uint16_t)m->pwp_flag, "game / player flags");
    SM(memcmp(x.sa1_position, m->pos, 12) == 0, "sa1_position");
    SM(x.position[0] == hp[0] && x.position[1] == hp[1] && x.position[2] == hp[2], "host position");
    SM(memcmp(&x.yaw, &yaw, 4) == 0, "yaw");
    SM(memcmp(x.speed, m->mspd, 12) == 0 && memcmp(x.accel, m->macc, 12) == 0, "speed / accel");
    SM(memcmp(x.player_speed, m->pspd, 12) == 0 && memcmp(x.player_accel, m->pacc, 12) == 0, "player speed / accel");
    SM(memcmp(x.floor_normal, m->floor_normal, 12) == 0 && memcmp(&x.dotp, &m->dotp, 4) == 0, "floor normal / dotp");
    for (k = 0; k < 3; k++) {
        SM((uint32_t)x.angle[k] == m->ang[k] && (uint32_t)x.angle_aim[k] == m->ang_aim[k], "angle / angle_aim");
    }
    SM(x.attr == m->attr && x.last_attr == m->last_attr, "attr / last_attr");
    SM(x.jump_timer == m->jumptimer && x.nocon_timer == m->nocontimer && x.wait_timer == m->waittimer, "timers");
    SM(x.action == m->action && x.req_action == m->reqaction && x.last_action == m->lastaction
       && x.next_action == m->nextaction, "actions");
    SM(memcmp(&x.frame, &m->nframe, 4) == 0, "frame");
    SM(x.equipment == m->equipment && x.item == m->item, "equipment / item");
    SM(x.land_count == m->land_count && x.rand_state == m->rand_state, "land_count / rand_state");
#undef SM
    return 0;
}

static int obj_col(const uint32_t *objs, uint32_t n, uint32_t v);

/* ---- one replay ---------------------------------------------------------------------------------------- */
typedef struct rep_result {
    wr_result r;                /* every field / blob (wr_compare) */
    wr_diff others;             /* everything but land_count / rand_state */
    uint32_t land_steps, rand_steps, rand_ahead;
    uint32_t rand_m[8], rand_mn[8], rand_nm;    /* "reference m draws ahead": distinct m, steps each */
    uint32_t lclop_steps;       /* SN_LCLOP_KEPT: steps with the stage task's lclop residual */
    uint32_t char_words;        /* character pointer words compared by structure (k_char_ptr_words) */
    int32_t land_min, land_max;
    int water_step;             /* first record whose reference player probes the STG01 water plane (-1: none) */
    uint32_t state_bad;         /* steps where sa1_character_state_ex differed from the record */
    int first_state_bad;
    char state_msg[128];
    uint32_t mxcsr_bad;         /* steps after which the host MXCSR was not the custom value */
    long allocs;                /* context allocations during the steps */
    double ms;                  /* time in the steps */
} rep_result;

#define INJ_FULL (SA1G_INJ_PAD | SA1G_INJ_ANALOG | SA1G_INJ_RAND | SA1G_INJ_XMTRX | SA1G_INJ_CAMERA)
#define INJ_PURE (SA1G_INJ_PAD | SA1G_INJ_ANALOG)

/* the world's arena at the start of one V6 step (--dump-ram STEP): OUT/<name>.step<N>.world.ram, for a region
 * diff against the harness's --dump-ram-at N (debug ladder 9.3, step 2) */
static int g_dump_step = -1;
static char g_dump_path[1100];

static void replay(const land *L, int canon, sa1_context *ctx, sa1_character_id id, const wr_trace *t, uint32_t inject,
                   int reseed_on, sa1_trace_rec *out, rep_result *res)
{
    sa1g_world *w = sa1_character_world(ctx, id);
    const stage_note *note = stage_note_of(t->h.level, t->h.act);
    const uint32_t how = WR_CMP_RAW | WR_CMP_HEAP;
    unsigned host_csr = _mm_getcsr(), custom = (host_csr & ~0x6000u) | 0x2000u;  /* round down: never the world's */
    long a0 = g_allocs;
    uint32_t i;
    memset(res, 0, sizeof *res);
    wr_diff_init(&res->r.d);
    wr_diff_init(&res->others);
    res->r.first_pad_mismatch = -1;
    res->r.first_rand_pre = -1;
    res->first_state_bad = -1;
    res->land_min = 0x7FFFFFFF;
    res->land_max = -0x7FFFFFFF;
    res->water_step = -1;
    if (w == NULL) {
        res->r.fault = 1;
        return;
    }
    w->replay = 1;
    for (i = 0; i < t->n; i++) {
        const sa1_trace_rec *r = &t->r[i];
        sa1_trace_rec mine, cmp;
        sa1_status st;
        double t0;
        if (reseed_on && i > 0) {
            reseed(w, t->h.chr, &t->r[i - 1]);
        }
        if (out != NULL && !reseed_on && g_dump_step == (int)i && g_dump_path[0]) {
            if (wr_write_file(g_dump_path, w->ram, SA1G_ARENA_SIZE) == 0) {
                printf("    wrote %s (the world's arena at the start of step %u)\n", g_dump_path, (unsigned)i);
            }
        }
        wr_inject(w, r, inject, 0);
        _mm_setcsr(custom);
        t0 = sa1_now_ms();
        st = sa1g_world_step(w, (int32_t)r->sub);
        res->ms += sa1_now_ms() - t0;
        if (_mm_getcsr() != custom) {
            res->mxcsr_bad++;
        }
        _mm_setcsr(host_csr);
        if (st != SA1_OK) {
            printf("    step %u: %s: %s\n", (unsigned)i, sa1g_status_string(st), w->fault.msg);
            res->r.fault = 1;
            break;
        }
        res->r.steps++;
        if (w->inject.pad_computed_on != r->pad_on || w->inject.pad_computed_press != r->pad_press
            || ((w->inject.analog_computed_ang ^ r->analog_ang) & 0xFFFFu) != 0
            || memcmp(&w->inject.analog_computed_stroke, &r->analog_stroke, 4) != 0) {
            if (res->r.pad_mismatch++ == 0) {
                res->r.first_pad_mismatch = (int)i;
            }
        }
        if (w->inject.player_entries == 0) {
            res->r.no_entry++;
        } else {
            if (w->inject.pre_rand != r->px_rand && res->r.rand_pre_mismatch++ == 0) {
                res->r.first_rand_pre = (int)i;
            }
            if (memcmp(w->inject.pre_xmtrx, r->px_xmtrx, sizeof r->px_xmtrx) != 0) {
                res->r.xmtrx_pre_mismatch++;
            }
        }
        wr_record(w, r, &mine);
        if (out != NULL) {
            out[i] = mine;
        }
        cmp = mine;
        if (canon) {
            canon_objects(L, &cmp);
        }
        res->char_words += canon_char_words(w, t->h.chr, r, &cmp);
        wr_compare(r, &cmp, how, &res->r.d, (int)i);
        if (res->water_step < 0) {
            uint32_t off;
            for (off = 0x1E4; off < 0x1F8; off += 4) {
                if (wr_u32(r->pwp_raw + off) == WATER_TASK_ATTR && wr_u32(mine.pwp_raw + off) != WATER_TASK_ATTR) {
                    res->water_step = (int)i;
                }
            }
        }
        /* from the reference's water contact on, the residual classification stops (its player is in the water
         * plane libsa1 does not have) */
        if (res->water_step < 0) {
            sa1_trace_rec cmp2 = cmp;
            uint32_t rl = wr_u32(r->pwp_raw + 0x74), ml = wr_u32(cmp.pwp_raw + 0x74);
            if (note != NULL && (note->flags & SN_LCLOP_KEPT) && ml == 0 && rl != 0 && L != NULL
                && obj_col(L->ref_obj, L->ncol, rl) >= 0) {
                memcpy(cmp2.pwp_raw + 0x74, &rl, 4);
                res->lclop_steps++;
            }
            wr_compare(r, &cmp2, how | WR_CMP_NO_LANDCNT | WR_CMP_NO_RAND, &res->others, (int)i);
            if (r->land_count != mine.land_count) {
                int32_t dl = (int32_t)(r->land_count - mine.land_count);
                res->land_steps++;
                res->land_min = dl < res->land_min ? dl : res->land_min;
                res->land_max = dl > res->land_max ? dl : res->land_max;
            }
            if (r->rand_state != mine.rand_state) {
                uint32_t m = wr_rand_ahead(mine.rand_state, r->rand_state), k;
                res->rand_steps++;
                res->rand_ahead += m != 0;
                for (k = 0; m != 0 && k < res->rand_nm && res->rand_m[k] != m; k++) {
                }
                if (m != 0 && k < 8u) {
                    if (k == res->rand_nm) {
                        res->rand_m[res->rand_nm++] = m;
                    }
                    res->rand_mn[k]++;
                }
            }
        }
        if (state_mismatch(ctx, id, &mine, res->state_msg, sizeof res->state_msg) && res->state_bad++ == 0) {
            res->first_state_bad = (int)i;
        }
    }
    res->allocs = g_allocs - a0;
}

/* only the stage tasks' residuals (or none) before the reference reaches the STG01 water plane */
static int rep_tolerant_ok(const rep_result *res, uint32_t n)
{
    return !res->r.fault && res->r.steps == n && res->others.nsteps_bad == 0
           && (res->land_steps == 0 || res->land_min == res->land_max) && res->rand_ahead == res->rand_steps;
}

static void rep_known(const char *what, const char *name, const rep_result *res, const wr_trace *t)
{
    const sa1_trace_rec *r;
    float yt;
    if (res->water_step < 0) {
        return;
    }
    r = &t->r[res->water_step];
    memcpy(&yt, r->pwp_raw + 0x1EC, 4);
    g_known++;
    printf("KNOWN ISSUE %s %s: from step %d the reference's water probe (shadow Attr_top %08X, water surface y %g) hits "
           "the STG01 ocean task's water plane (DynamicCOL 0x88000002 of 0C909FDA, which a world has with "
           "SA1_CREATE_STAGE_TASKS only); the player at y %g gets water physics there, so records %d-%u are not "
           "comparable; records "
           "0-%d are verified\n", what, name, res->water_step, (unsigned)wr_u32(r->pwp_raw + 0x1E4), yt, r->pos[1],
           res->water_step, (unsigned)(t->n - 1), res->water_step - 1);
}

static void rep_print(const char *what, const rep_result *res, uint32_t n)
{
    printf("    %s: %u of %u steps, %u records differ in some field (%.4f ms per step)%s\n", what,
           (unsigned)res->r.steps, (unsigned)n, (unsigned)res->r.d.nsteps_bad,
           res->r.steps ? res->ms / (double)res->r.steps : 0.0, res->r.fault ? " (stopped by a fault)" : "");
    printf("      input: the pad update computed the record's on/press/analog in %u steps (first other %d); player "
           "entries missing in %u steps; rand before injection equal in %u\n",
           (unsigned)(res->r.steps - res->r.pad_mismatch), res->r.first_pad_mismatch, (unsigned)res->r.no_entry,
           (unsigned)(res->r.steps - res->r.no_entry - res->r.rand_pre_mismatch));
    printf("      land_count differs in %u steps (reference - world %d..%d); rand_state in %u steps (%u with the "
           "reference ahead); all other fields and raw blobs: %u records differ%s\n", (unsigned)res->land_steps,
           res->land_steps ? (int)res->land_min : 0, res->land_steps ? (int)res->land_max : 0,
           (unsigned)res->rand_steps, (unsigned)res->rand_ahead, (unsigned)res->others.nsteps_bad,
           res->water_step >= 0 ? " (before the reference's water contact)" : "");
    if (res->rand_nm != 0) {
        uint32_t k;
        printf("      rand_state: the reference");
        for (k = 0; k < res->rand_nm; k++) {
            printf("%s %u draw(s) ahead in %u step(s)", k ? "," : "", (unsigned)res->rand_m[k], (unsigned)res->rand_mn[k]);
        }
        printf("\n");
    }
    if (res->lclop_steps != 0) {
        printf("      pwp->lclop: the reference keeps a landtable object where the world cleared it in %u steps (the stage "
               "task's DynamicCOL, see the stage note)\n", (unsigned)res->lclop_steps);
    }
    if (res->char_words != 0) {
        printf("      %u character pointer words compared by structure (heap pointers to the same task)\n",
               (unsigned)res->char_words);
    }
    if (res->water_step >= 0) {
        printf("      the reference's player probes the STG01 water plane from step %d (not in libsa1 worlds); all "
               "differences from there on:\n", res->water_step);
        wr_diff_print(&res->r.d, 6);
    }
    wr_diff_print(&res->others, 12);
}

/* ---- a character at a trace's start, through the public API ------------------------------------------ */
static int g_noscreen_told;

static sa1_status create_at(sa1_context *ctx, const sa1_trace_header *h, sa1_character_id *id)
{
    sa1_character_desc d;
    sa1_status st;
    int k;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.kind = h->chr;
    d.flags = SA1_CREATE_WORLD | SA1_CREATE_SA1_SPACE | SA1_CREATE_ANGLES | SA1_CREATE_STAGE | g_create_flags;
    memcpy(d.position, h->start_pos, sizeof d.position);
    for (k = 0; k < 3; k++) {
        d.angle[k] = (int32_t)h->start_ang[k];
    }
    d.level = h->level;
    d.act = h->act;
    st = sa1_character_create_ex(ctx, &d, id);
    if (!SA1_FAILED(st) && wr_screen_of(sa1_character_world(ctx, *id), h) && !g_noscreen_told) {
        printf("    the reference has the zero screen (no header flag 8: recorded before 2026-10-20 or with "
               "--no-screen): the world's screen words are zeroed\n");
        g_noscreen_told = 1;
    }
    return st;
}

static void write_out(const env *E, const char *name, const char *suffix, const wr_trace *t, const sa1_trace_rec *out,
                      uint32_t n)
{
    char path[1100];
    if (E->out == NULL || n == 0) {
        return;
    }
    snprintf(path, sizeof path, "%s/%s%s.bin", E->out, name, suffix);
    if (wr_trace_write(path, &t->h, out, n) == 0) {
        printf("    wrote %s\n", path);
    } else {
        CHECK(0, "cannot write %s", path);
    }
}

/* the object pointer of every COL of the synthesized landtable (for canon_objects) */
static void map_faithful_objects(land *L, sa1_character_id id)
{
    sa1g_world *w = sa1_character_world(L->ctx, id);
    sa1_collision_info info;
    const uint8_t *lt, *cols;
    uint32_t i, n, ca;
    int16_t cnt;
    memset(&info, 0, sizeof info);
    info.struct_size = sizeof info;
    if (L->faith_obj != NULL || w == NULL || sa1_collision_info_get(L->ctx, &info) != SA1_OK || info.landtable == 0) {
        return;
    }
    lt = (const uint8_t *)sa1g_world_ptr(w, info.landtable);
    memcpy(&cnt, lt, 2);
    memcpy(&ca, lt + 0x0C, 4);
    n = cnt > 0 ? (uint32_t)cnt : 0u;
    cols = (const uint8_t *)sa1g_world_ptr(w, ca);
    L->faith_obj = (uint32_t *)calloc(n + 1u, sizeof *L->faith_obj);
    for (i = 0; L->faith_obj != NULL && i < n; i++) {
        memcpy(&L->faith_obj[i], cols + 0x24u * i + 0x18u, 4);
    }
    L->faith_lt = info.landtable;
    CHECK(n == L->ncol && L->faith_obj != NULL && (info.landtable >> 24) == 0x88u,
          "FAITHFUL landtable %08X in the collision segment: %u COLs (the dump's landtable %08X has %u)",
          (unsigned)info.landtable, (unsigned)n, (unsigned)L->dump.landtable, (unsigned)L->ncol);
}

/* Big's lure task (B_8C7A40C8, created by BigMain through 8C0E6DA0 at step 0): its exec 8C0E943C runs case 0 once,
 * whose func_8C0E91A0 sets twp->mode = 1 (lure held) and resets its state; the reference is in mode 1 from step 1
 * on (trace --dump-ram-at 179: lure task 8C7EED70, taskwk mode 1). A world whose lure is still in mode 0 after the
 * replay runs 8C0E91A0 as the boundary stub of gen/policy/stubs.tsv ("event SOUND_STOP", which drops the state
 * changes): reported as a KNOWN ISSUE (no player field of big_ec3 depends on it). */
static void check_big_lure(const sa1g_world *w, const char *what, const char *name)
{
    uint32_t tp = 0, twp = 0;
    if (w == NULL || sa1g_world_rd32(w, 0x8C7A40C8u, &tp) != 0 || !wr_arena(tp)
        || sa1g_world_rd32(w, tp + 0x20u, &twp) != 0 || !wr_arena(twp)) {
        return;
    }
    if (*(const uint8_t *)sa1g_world_ptr(w, twp) == 0) {
        g_known++;
        printf("KNOWN ISSUE %s %s: Big's lure task (B_8C7A40C8 %08X, exec %08X) is still in mode 0 after the replay "
               "(the reference: mode 1 from step 1): func_8C0E91A0 (lure reset: twp->mode = 1, awp->work[1].w[0..1] = 0, "
               "D_8C565F70..D_8C565F80 = 0, 8C107B26 when B_8C7A40E8 & 1, SE stop 0x320) is the boundary stub "
               "\"event SOUND_STOP\" (gen/policy/stubs.tsv; its C in sadc code_8C0E91E4.c is SHC-only, an ASM_STUB "
               "under TARGET_PC): the lure never reaches mode 1, so a cast (B_8C7A40E8 & 0x8000) never starts\n",
               what, name, (unsigned)tp, (unsigned)wr_g32(w, tp + 0x10u));
    } else {
        printf("  ok    %s %s: Big's lure task (B_8C7A40C8) left mode 0 as in the reference\n", what, name);
    }
}

typedef struct trace_job {
    const char *name;
    const char *tag;
    uint32_t land;              /* index into env.lands */
    wr_trace t;
    int loaded;
    sa1_trace_rec *v6;          /* FAITHFUL V6 records (for DISC) */
    uint32_t v6n;
} trace_job;

static const char *char_name(int32_t chr)
{
    static const char *const n[] = { "SONIC", "EGGMAN", "TAILS", "KNUCKLES", "TIKAL", "AMY", "GAMMA", "BIG",
                                     "SUPER_SONIC" };
    return chr >= 0 && chr < (int32_t)(sizeof n / sizeof n[0]) ? n[chr] : "?";
}

static void run_trace(env *E, trace_job *J)
{
    const wr_trace *t = &J->t;
    land *L = &E->lands[J->land];
    const char *tag = J->tag;
    const stage_note *note = stage_note_of(t->h.level, t->h.act);
    sa1_character_id id = 0;
    rep_result v6, v5, again;
    sa1_trace_rec *out = (sa1_trace_rec *)calloc(t->n + 1u, sizeof *out);
    sa1_trace_rec *out2 = (sa1_trace_rec *)calloc(t->n + 1u, sizeof *out2);
    sa1_status st;
    int strict_ok, tol_ok;
    printf("%s %s (%s, %u records, start (%g, %g, %g) ang (%X, %X, %X), stage %02X%02X):\n", tag, J->name,
           char_name(t->h.chr), (unsigned)t->n, t->h.start_pos[0], t->h.start_pos[1], t->h.start_pos[2],
           (unsigned)t->h.start_ang[0], (unsigned)t->h.start_ang[1], (unsigned)t->h.start_ang[2], (unsigned)t->h.level,
           (unsigned)t->h.act);
    if (note != NULL) {
        printf("    the stage's own tasks (%s): %s\n", g_create_flags & SA1_CREATE_STAGE_TASKS ? "in the world"
               : "NOT in the world: --no-stage-tasks", note->text);
    }
    if (out == NULL || out2 == NULL) {
        free(out);
        free(out2);
        CHECK(0, "memory");
        return;
    }
    /* V6 */
    st = create_at(L->ctx, &t->h, &id);
    CHECK(st == SA1_OK && sa1_character_world(L->ctx, id) != NULL,
          "sa1_character_create_ex(%s, SA1_CREATE_WORLD%s) at the header's start and stage: %s %s", char_name(t->h.chr),
          g_create_flags & SA1_CREATE_STAGE_TASKS ? " | SA1_CREATE_STAGE_TASKS" : "", sa1_status_string(st),
          st == SA1_OK ? "" : sa1_last_error(L->ctx));
    if (st == SA1_OK && sa1_character_world(L->ctx, id) != NULL && (g_create_flags & SA1_CREATE_STAGE_TASKS)) {
        const sa1g_world *sw = sa1_character_world(L->ctx, id);
        uint32_t k;
        printf("    world image overlays with a code map:");
        for (k = 0; k < sw->num_fn_images; k++) {
            printf(" %s (%u functions)", sw->fn_images[k]->name, (unsigned)sw->fn_images[k]->count);
        }
        printf("%s; stage tasks: %s\n", sw->num_fn_images ? "" : " none", sw->stage_note);
    }
    if (st != SA1_OK) {
        free(out);
        free(out2);
        return;
    }
    map_faithful_objects(L, id);
    g_dump_path[0] = 0;
    if (g_dump_step >= 0 && E->out != NULL) {
        snprintf(g_dump_path, sizeof g_dump_path, "%s/%s.step%d.world.ram", E->out, J->name, g_dump_step);
    }
    replay(L, 1, L->ctx, id, t, INJ_FULL, 0, out, &v6);
    g_dump_path[0] = 0;
    rep_print("V6 (pad, sub; rand / XMTRX / camera at player entry; FAITHFUL dump geometry)", &v6, t->n);
    write_out(E, J->name, "", t, out, v6.r.steps);
    strict_ok = !v6.r.fault && v6.r.steps == t->n && v6.r.d.nsteps_bad == 0;
    tol_ok = rep_tolerant_ok(&v6, t->n);
    if (E->strict) {
        CHECK(strict_ok, "%s V6 %s strict: %u of %u records differ", tag, J->name, (unsigned)v6.r.d.nsteps_bad,
              (unsigned)t->n);
    } else {
        CHECK(tol_ok, "%s V6 %s: %s (records 0-%d; strict: %u of %u differ)", tag, J->name,
              strict_ok ? "every decoded field and raw blob identical"
                        : "identical but for the stage tasks' land_count / rand_state residuals",
              v6.water_step >= 0 ? v6.water_step - 1 : (int)v6.r.steps - 1, (unsigned)v6.r.d.nsteps_bad,
              (unsigned)v6.r.steps);
        rep_known("V6", J->name, &v6, t);
    }
    if (note != NULL && note->stub != NULL && v6.rand_steps != 0) {
        g_known++;
        printf("KNOWN ISSUE V6 %s: rand_state residuals on libsa1's side: %s\n", J->name, note->stub);
    }
    if (t->h.chr == SA1_CHAR_BIG) {
        check_big_lure(sa1_character_world(L->ctx, id), "V6", J->name);
    }
    CHECK(v6.state_bad == 0, "public sa1_character_state_ex == the record at every step (%u differ; first %d: %s)",
          (unsigned)v6.state_bad, v6.first_state_bad, v6.state_bad ? v6.state_msg : "-");
    CHECK(v6.mxcsr_bad == 0 && v6.allocs == 0, "host MXCSR restored after every step (%u not), %ld context allocations "
          "inside the steps", (unsigned)v6.mxcsr_bad, v6.allocs);
    sa1_character_destroy(L->ctx, id);

    /* V7: the same replay again in this process */
    st = create_at(L->ctx, &t->h, &id);
    if (st == SA1_OK) {
        replay(L, 1, L->ctx, id, t, INJ_FULL, 0, out2, &again);
        sa1_character_destroy(L->ctx, id);
    }
    CHECK(st == SA1_OK && again.r.steps == v6.r.steps && memcmp(out, out2, (size_t)v6.r.steps * sizeof *out) == 0,
          "a second create + V6 replay in this process writes byte-identical records (%u)", (unsigned)again.r.steps);

    /* V5 */
    st = create_at(L->ctx, &t->h, &id);
    if (st == SA1_OK) {
        replay(L, 1, L->ctx, id, t, INJ_FULL, 1, out2, &v5);
        sa1_character_destroy(L->ctx, id);
        rep_print("V5 reseeded (record i-1's player blobs imported before step i)", &v5, t->n);
        write_out(E, J->name, ".reseed", t, out2, v5.r.steps);
        strict_ok = !v5.r.fault && v5.r.steps == t->n && v5.r.d.nsteps_bad == 0;
        tol_ok = rep_tolerant_ok(&v5, t->n);
        if (E->strict) {
            CHECK(strict_ok, "%s V5 %s strict: %u of %u records differ (first: step %d)", tag, J->name,
                  (unsigned)v5.r.d.nsteps_bad, (unsigned)t->n, v5.r.d.first_bad_step);
        } else {
            CHECK(tol_ok, "%s V5 %s: %s (records 0-%d; first other difference: step %d)", tag, J->name,
                  strict_ok ? "every record identical" : "identical but for the stage tasks' residuals",
                  v5.water_step >= 0 ? v5.water_step - 1 : (int)v5.r.steps - 1, v5.others.first_bad_step);
            rep_known("V5", J->name, &v5, t);
        }
    } else {
        CHECK(0, "create for V5: %s", sa1_last_error(L->ctx));
    }

    /* information: only the input injected */
    if (E->pure && create_at(L->ctx, &t->h, &id) == SA1_OK) {
        replay(L, 1, L->ctx, id, t, INJ_PURE, 0, out2, &again);
        sa1_character_destroy(L->ctx, id);
        rep_print("pure run (information: pad + analog only; the world's own rand, XMTRX and camera adapter)", &again,
                  t->n);
    }
    free(out2);
    J->v6 = out;
    J->v6n = v6.r.steps;
}

/* M1 exit 4: the DISC world (the stage's own landtable in place) writes FAITHFUL's records */
static void run_disc(env *E, trace_job *J)
{
    const wr_trace *t = &J->t;
    land *L = &E->lands[J->land];
    sa1_character_id id = 0;
    rep_result res;
    wr_diff d;
    sa1_trace_rec *out;
    sa1_status st;
    uint32_t i, n;
    if (J->v6 == NULL || L->dctx == NULL) {
        return;
    }
    out = (sa1_trace_rec *)calloc(t->n + 1u, sizeof *out);
    st = out != NULL ? create_at(L->dctx, &t->h, &id) : SA1_ERR_OUT_OF_MEMORY;
    if (st == SA1_ERR_BAD_HASH) {
        /* the collision adapter has no SHA-1 for this stage overlay (sa1_coladapt.c k_stage_files) */
        printf("  skip  DISC %s: %s\n", J->name, sa1_last_error(L->dctx));
        g_skip++;
        free(out);
        return;
    }
    if (st != SA1_OK) {
        CHECK(0, "DISC %s: create: %s %s", J->name, sa1_status_string(st), sa1_last_error(L->dctx));
        free(out);
        return;
    }
    replay(L, 0, L->dctx, id, t, INJ_FULL, 0, out, &res);
    sa1_character_destroy(L->dctx, id);
    rep_print("DISC V6 (the stage's own landtable)", &res, t->n);
    write_out(E, J->name, ".disc", t, out, res.r.steps);
    wr_diff_init(&d);
    n = res.r.steps < J->v6n ? res.r.steps : J->v6n;
    for (i = 0; i < n; i++) {
        sa1_trace_rec f = J->v6[i];
        canon_objects(L, &f);
        canon_char_words(NULL, t->h.chr, &out[i], &f);
        wr_compare(&out[i], &f, WR_CMP_RAW | WR_CMP_HEAP, &d, (int)i);
    }
    wr_diff_print(&d, 8);
    CHECK(!res.r.fault && n == t->n && d.nsteps_bad == 0,
          "DISC and FAITHFUL write identical records for %s (decoded fields, raw blobs, landtable objects by COL index): "
          "%u of %u differ", J->name, (unsigned)d.nsteps_bad, (unsigned)n);
    free(out);
}

/* ---- M1 exit 3: the land collision state against the reference RAM ----------------------------------- */
#define ADDR_COLOBJ_USED  0x8C02D548u   /* D_8C02D548: in-use colobj list (head word in the code section) */
#define ADDR_STATIC_LIST  0x8C7584F4u   /* B_8C7584F4: landtable entry copies of the step (0x24 each) */
#define ADDR_STATIC_COUNT 0x8C19E968u   /* D_8C19E968 */
#define ADDR_DYN_COUNT    0x8C19E964u   /* D_8C19E964: dynamic entries of the step */

static uint32_t ram32(const uint8_t *ram, uint32_t g)
{
    uint32_t v = 0;
    if (wr_arena(g) && (g & 0xFFFFFFu) <= SA1G_ARENA_SIZE - 4u) {
        memcpy(&v, ram + (g & 0xFFFFFFu), 4);
    }
    return v;
}

static int obj_col(const uint32_t *objs, uint32_t n, uint32_t v)
{
    uint32_t i;
    for (i = 0; v != 0 && objs != NULL && i < n; i++) {
        if ((objs[i] & 0x1FFFFFFFu) == (v & 0x1FFFFFFFu)) {
            return (int)i;
        }
    }
    return -1;
}

/* the in-use colobjs of landtable objects, in list order (COL index and guest address); others counted */
static uint32_t colobj_list(const uint8_t *ram, const uint32_t *objs, uint32_t n, int *idx, uint32_t *addr, uint32_t cap,
                            uint32_t *others)
{
    uint32_t co = ram32(ram, ADDR_COLOBJ_USED), k = 0, guard = 0;
    *others = 0;
    while (co != 0 && guard++ < 192u) {
        int c = obj_col(objs, n, ram32(ram, co + 8u));
        if (c < 0) {
            (*others)++;
        } else if (k < cap) {
            idx[k] = c;
            addr[k++] = co;
        }
        co = ram32(ram, co);
    }
    return k;
}

static void test_cache(const env *E, const trace_job *J, uint32_t step)
{
    char path[1100];
    size_t n = 0;
    unsigned char *ref = NULL;
    const wr_trace *t = &J->t;
    const land *L = &E->lands[J->land];
    sa1_character_id id = 0;
    sa1g_world *w;
    int ri[192], wi[192];
    uint32_t ra[192], wa[192], rn, wn, rother = 0, wother = 0, i, k, bad = 0, polys = 0, sbad = 0, rs, ws, rdyn, wdyn;
    snprintf(path, sizeof path, "%s/build/traces/m1_%s.step%u.ram", E->sadc, J->name, (unsigned)step);
    ref = wr_read_file(path, &n);
    if (ref == NULL || n != SA1G_ARENA_SIZE || L->faith_obj == NULL || step >= t->n) {
        printf("  skip  exit 3 at step %u: no reference RAM %s (harness trace --dump-ram-at %u, local data)\n",
               (unsigned)step, path, (unsigned)step);
        g_skip++;
        free(ref);
        return;
    }
    if (create_at(L->ctx, &t->h, &id) != SA1_OK || (w = sa1_character_world(L->ctx, id)) == NULL) {
        CHECK(0, "exit 3: create");
        free(ref);
        return;
    }
    w->replay = 1;
    for (i = 0; i < step; i++) {
        wr_inject(w, &t->r[i], INJ_FULL, 0);
        if (sa1g_world_step(w, (int32_t)t->r[i].sub) != SA1_OK) {
            break;
        }
    }
    /* the step's static gather list: entry copies, pObject by COL index */
    rs = ram32(ref, ADDR_STATIC_COUNT);
    ws = ram32(w->ram, ADDR_STATIC_COUNT);
    for (k = 0; k < rs && k < ws && k < 128u; k++) {
        uint32_t off, e = ADDR_STATIC_LIST + 0x24u * k;
        for (off = 0; off < 0x24u; off += 4) {
            uint32_t a = ram32(ref, e + off), b = ram32(w->ram, e + off);
            if (off == 0x18u ? obj_col(L->ref_obj, L->ncol, a) != obj_col(L->faith_obj, L->ncol, b) || obj_col(L->ref_obj, L->ncol, a) < 0
                             : a != b) {
                sbad++;
            }
        }
    }
    /* the colobj cache: landtable objects in list order, every field, every polygon */
    rn = colobj_list(ref, L->ref_obj, L->ncol, ri, ra, 192, &rother);
    wn = colobj_list(w->ram, L->faith_obj, L->ncol, wi, wa, 192, &wother);
    for (k = 0; k < rn && k < wn; k++) {
        static const uint32_t words[] = { 0x0C, 0x18, 0x20, 0x24, 0x28, 0x2C, 0x30, 0x34, 0x38, 0x3C };
        uint32_t q, pr, pw, lr, lw, guard = 0;
        if (ri[k] != wi[k]) {
            bad++;
            continue;
        }
        for (q = 0; q < sizeof words / sizeof words[0]; q++) {
            bad += ram32(ref, ra[k] + words[q]) != ram32(w->ram, wa[k] + words[q]);
        }
        bad += (ram32(ref, ra[k] + 0x1Cu) != 0) != (ram32(w->ram, wa[k] + 0x1Cu) != 0);
        pr = ram32(ref, ra[k] + 0x10u);
        pw = ram32(w->ram, wa[k] + 0x10u);
        lr = ram32(ref, ra[k] + 0x14u);
        lw = ram32(w->ram, wa[k] + 0x14u);
        while (pr != 0 && pw != 0 && guard++ < 8192u) {
            uint32_t off;
            for (off = 0x08; off < 0x40; off += 4) {
                bad += ram32(ref, pr + off) != ram32(w->ram, pw + off);
            }
            polys++;
            if (pr == lr || pw == lw) {
                bad += !(pr == lr && pw == lw);
                break;
            }
            pr = ram32(ref, pr);
            pw = ram32(w->ram, pw);
        }
    }
    rdyn = ram32(ref, ADDR_DYN_COUNT);
    wdyn = ram32(w->ram, ADDR_DYN_COUNT);
    sa1_character_destroy(L->ctx, id);
    free(ref);
    printf("    step %u: reference cache %u landtable objects + %u other (dynamic entries %u), world %u + %u (dynamic %u); "
           "static gather lists %u / %u entries\n", (unsigned)step, (unsigned)rn, (unsigned)rother, (unsigned)rdyn,
           (unsigned)wn, (unsigned)wother, (unsigned)wdyn, (unsigned)rs, (unsigned)ws);
    CHECK(rn == wn && rs == ws && bad == 0 && sbad == 0 && rn > 0
              && (g_create_flags & SA1_CREATE_STAGE_TASKS ? wother == rother && wdyn == rdyn : wother == 0),
          "exit 3, %s step %u: the land collision cache equals the reference RAM by structure: %u landtable objects, "
          "%u polygons, %u static gather entries compared, %u + %u words differ; other objects (the STG01 water "
          "plane) reference %u, world %u", J->name, (unsigned)step, (unsigned)rn, (unsigned)polys, (unsigned)rs,
          (unsigned)bad, (unsigned)sbad, (unsigned)rother, (unsigned)wother);
}

/* ---- live mode through sa1_character_tick ------------------------------------------------------------- */
static double host_angle_deg(double a)
{
    while (a > 180.0) {
        a -= 360.0;
    }
    while (a <= -180.0) {
        a += 360.0;
    }
    return a;
}

static void test_live(sa1_context *ctx, const wr_trace *t)
{
    sa1_character_id id = 0, id2 = 0, pid = 0;
    sa1_character_info ci;
    sa1_state s;
    sa1_state_ex x, x2;
    sa1_input in;
    sa1_mesh mesh;
    sa1_event ev[16];
    float pos[3], yaw, p0[3], *mpos;
    double run_yaw, dir, cx = 0, cy = 0, cz = 0;
    unsigned host_csr = _mm_getcsr();
    uint32_t i, k, nev = 0, same = 1;
    long a0;
    sa1_status st, st2;
    int grounded_at = -1;
    printf("live mode (sa1_character_tick, FAITHFUL dump geometry, Source coordinates):\n");
    pos[0] = t->h.start_pos[0] * 4.0f;
    pos[1] = -t->h.start_pos[2] * 4.0f;
    pos[2] = t->h.start_pos[1] * 4.0f;
    yaw = (float)(-(double)(int16_t)(uint16_t)t->h.start_ang[1] * (6.28318530717958647692 / 65536.0));
    st = sa1_character_create(ctx, SA1_CHAR_SONIC, pos, yaw, &id);
    st2 = sa1_character_create(ctx, SA1_CHAR_SONIC, pos, yaw, &id2);
    CHECK(st == SA1_OK && st2 == SA1_OK && sa1_character_world(ctx, id) == NULL,
          "sa1_character_create (the 0.1 signature): a character without a world until its first tick");
    if (st != SA1_OK || st2 != SA1_OK) {
        return;
    }
    memset(&in, 0, sizeof in);
    in.struct_size = sizeof in;
    memset(&s, 0, sizeof s);
    s.struct_size = sizeof s;
    memset(&x, 0, sizeof x);
    x.struct_size = sizeof x;
    memset(&x2, 0, sizeof x2);
    x2.struct_size = sizeof x2;
    st = sa1_character_tick(ctx, id, &in, 1.0f / 60.0f, &s, NULL);
    sa1_character_state_ex(ctx, id, &x);
    CHECK(st == SA1_OK && sa1_character_world(ctx, id) != NULL && x.step == 0 && (s.flags & SA1_STATE_TICKING)
          && s.position[0] == pos[0] && s.position[1] == pos[1] && s.position[2] == pos[2],
          "first tick builds the world at the create position (%g, %g, %g); 1/60 s runs no pair yet (step %u): %s",
          s.position[0], s.position[1], s.position[2], (unsigned)x.step, sa1_status_string(st));
    st = sa1_character_tick(ctx, id, &in, 1.0f / 60.0f, &s, NULL);
    sa1_character_state_ex(ctx, id, &x);
    CHECK(st == SA1_OK && x.step == 2, "another 1/60 s completes a pair: 2 steps (%u)", (unsigned)x.step);
    st = sa1_character_tick(ctx, id, &in, 0.1f, &s, NULL);
    sa1_character_state_ex(ctx, id, &x);
    CHECK(st == SA1_OK && x.step == 8, "0.1 s runs 3 more pairs: 8 steps (%u)", (unsigned)x.step);
    /* the second character catches up with the same input: two worlds, interleaved, identical */
    sa1_character_tick(ctx, id2, &in, 1.0f / 60.0f, NULL, NULL);
    sa1_character_tick(ctx, id2, &in, 1.0f / 60.0f, NULL, NULL);
    sa1_character_tick(ctx, id2, &in, 0.1f, NULL, NULL);
    /* fall to the deck */
    a0 = g_allocs;
    _mm_setcsr((host_csr & ~0x6000u) | 0x2000u);
    for (i = 0; i < 30; i++) {
        unsigned csr = _mm_getcsr();
        st = sa1_character_tick(ctx, id, &in, 1.0f / 30.0f, &s, NULL);
        st2 = sa1_character_tick(ctx, id2, &in, 1.0f / 30.0f, NULL, NULL);
        sa1_character_state_ex(ctx, id, &x);
        sa1_character_state_ex(ctx, id2, &x2);
        same &= memcmp(&x, &x2, sizeof x) == 0;
        if (_mm_getcsr() != csr) {
            same = 0;
        }
        if (grounded_at < 0 && (s.flags & SA1_STATE_ON_GROUND)) {
            grounded_at = (int)x.step;
        }
        if (st != SA1_OK || st2 != SA1_OK) {
            break;
        }
    }
    _mm_setcsr(host_csr);
    CHECK(st == SA1_OK && grounded_at > 0 && (s.flags & SA1_STATE_ON_GROUND) && (s.surface & SA1_SURFACE_SOLID)
          && x.mode == 1, "standing: on the deck from step %d at host z %g (SA1 y %g), surface %X (attr %08X), mode %d",
          grounded_at, s.position[2], x.sa1_position[1], (unsigned)s.surface, (unsigned)x.attr, (int)x.mode);
    CHECK(same && g_allocs == a0, "two worlds ticked interleaved stay identical, the host MXCSR is kept, %ld allocations "
          "in 60 ticks", g_allocs - a0);
    /* run: the stick forward along a camera yaw = the reference run's direction (ang.y 0x6C21) */
    run_yaw = -(double)0x6C21 * (360.0 / 65536.0);
    in.stick_y = 1.0f;
    in.camera_yaw = (float)(run_yaw * (3.14159265358979323846 / 180.0));
    memcpy(p0, s.position, sizeof p0);
    for (i = 0; i < 45 && st == SA1_OK; i++) {
        st = sa1_character_tick(ctx, id, &in, 1.0f / 30.0f, &s, NULL);
    }
    sa1_character_state_ex(ctx, id, &x);
    dir = atan2((double)(s.position[1] - p0[1]), (double)(s.position[0] - p0[0])) * (180.0 / 3.14159265358979323846);
    CHECK(st == SA1_OK && fabs(host_angle_deg(dir - run_yaw)) < 15.0
              && fabs(host_angle_deg((double)s.yaw * (180.0 / 3.14159265358979323846) - run_yaw)) < 15.0
              && hypot((double)(s.position[0] - p0[0]), (double)(s.position[1] - p0[1])) > 100.0,
          "stick forward with camera yaw %.1f deg: moved %.1f host units at %.1f deg, yaw %.1f deg, speed %.1f/s, action %d",
          run_yaw, hypot((double)(s.position[0] - p0[0]), (double)(s.position[1] - p0[1])), dir,
          (double)s.yaw * (180.0 / 3.14159265358979323846),
          hypot((double)s.velocity[0], (double)s.velocity[1]), (int)s.action);
    /* the mesh of the current action and frame */
    memset(&ci, 0, sizeof ci);
    ci.struct_size = sizeof ci;
    sa1_character_info_get(ctx, SA1_CHAR_SONIC, &ci);
    memset(&mesh, 0, sizeof mesh);
    mesh.struct_size = sizeof mesh;
    mesh.max_triangles = ci.max_triangles;
    mpos = (float *)malloc((size_t)ci.max_triangles * 9u * sizeof(float) + 4u);
    mesh.position = mpos;
    st = sa1_character_tick(ctx, id, &in, 1.0f / 30.0f, &s, &mesh);
    for (k = 0; mpos != NULL && k < mesh.num_triangles * 3u; k++) {
        cx += mpos[3 * k];
        cy += mpos[3 * k + 1];
        cz += mpos[3 * k + 2];
    }
    if (mesh.num_triangles) {
        cx /= mesh.num_triangles * 3.0;
        cy /= mesh.num_triangles * 3.0;
        cz /= mesh.num_triangles * 3.0;
    }
    CHECK(st == SA1_OK && mesh.num_triangles > 0 && fabs(cx - s.position[0]) < 40.0 && fabs(cy - s.position[1]) < 40.0
              && fabs(cz - s.position[2]) < 60.0,
          "mesh_out: %u triangles of action %d frame %g centred at (%.1f, %.1f, %.1f), the character at (%.1f, %.1f, %.1f)",
          (unsigned)mesh.num_triangles, (int)s.action, s.frame, cx, cy, cz, s.position[0], s.position[1], s.position[2]);
    free(mpos);
    st = sa1_poll_events(ctx, ev, 16, &nev);
    CHECK(st == SA1_OK, "sa1_poll_events: %u event(s)", (unsigned)nev);
    /* puppets and characters that cannot tick */
    {
        sa1_character_desc d;
        memset(&d, 0, sizeof d);
        d.struct_size = sizeof d;
        d.kind = SA1_CHAR_SONIC;
        d.flags = SA1_CREATE_PUPPET;
        st = sa1_character_create_ex(ctx, &d, &pid);
        st2 = st == SA1_OK ? sa1_character_tick(ctx, pid, &in, 0.1f, NULL, NULL) : st;
        CHECK(st == SA1_OK && st2 == SA1_ERR_UNSUPPORTED && sa1_character_world(ctx, pid) == NULL,
              "a puppet (SA1_CREATE_PUPPET) does not tick: %s", sa1_status_string(st2));
        sa1_character_destroy(ctx, pid);
        d.flags = SA1_CREATE_WORLD | SA1_CREATE_PUPPET;
        CHECK(sa1_character_create_ex(ctx, &d, &pid) == SA1_ERR_INVALID_ARGUMENT, "PUPPET with WORLD is refused");
    }
    sa1_character_destroy(ctx, id);
    sa1_character_destroy(ctx, id2);
}
/* ---- the world tests' character sketch (src/world/sa1_world_api.c) uses the public API's conventions ------- */
static void test_sketch(sa1_context *ctx, const char *gdi)
{
    static sa1g_wctx c;
    sa1g_world *w = NULL;
    sa1_input in;
    sa1_state s, s0;
    sa1_status st;
    const double two_pi = 6.28318530717958647692;
    const float yaw = 2.0f;                              /* about 114.6 degrees, counter-clockwise */
    float pos[3] = { -36.0f, -16.0f, 64.0f };            /* host space (Source preset): SA1 (-9, 16, 4) */
    double t, run_yaw = -2.5, dir;
    uint32_t want, i;
    printf("character sketch over worlds (sa1g_character_world_*, the world tests' entry points):\n");
    st = sa1g_wctx_init(&c, ctx, gdi);
    if (st == SA1_OK) {
        st = sa1g_character_world_create(&c, SA1_CHAR_SONIC, pos, yaw, &w);
    }
    t = floor(-(double)yaw / two_pi * 65536.0 + 0.5);
    t = fmod(t, 65536.0);
    want = (uint32_t)(t < 0.0 ? t + 65536.0 : t) & 0xFFFFu;
    memset(&s0, 0, sizeof s0);
    s0.struct_size = sizeof s0;
    if (w != NULL) {
        sa1g_world_state(w, &s0);
    }
    CHECK(st == SA1_OK && w != NULL && w->start_ang_y == want
              && fabs((double)s0.yaw - (double)yaw) < two_pi / 65536.0,
          "create with host yaw %g: ang.y %04X = round(-yaw / 2 pi * 65536) (%04X); state yaw %g = -(Sint16)ang.y",
          (double)yaw, w ? (unsigned)w->start_ang_y : 0u, (unsigned)want, (double)s0.yaw);
    if (w != NULL) {
        memset(&in, 0, sizeof in);
        in.struct_size = sizeof in;
        in.stick_y = 1.0f;
        in.camera_yaw = (float)run_yaw;
        for (i = 0; i < 40 && st == SA1_OK; i++) {
            memset(&s, 0, sizeof s);
            s.struct_size = sizeof s;
            st = sa1g_character_world_tick(w, &in, 1.0f / 30.0f, &s);
        }
        t = floor(run_yaw / two_pi * 65536.0 + 0.5);
        want = (uint32_t)((int64_t)fmod(t, 65536.0) - 0x4000) & 0xFFFFu;
        dir = atan2((double)(s.position[1] - s0.position[1]), (double)(s.position[0] - s0.position[0]));
        CHECK(st == SA1_OK && w->camera.ang[1] == want && fabs(host_angle_deg((dir - run_yaw) * 180.0 / 3.14159265358979))
              < 15.0, "stick forward, camera yaw %g: camera ang.y %04X = round(camera_yaw / 2 pi * 65536) - 0x4000 (%04X); "
              "moved at %.1f deg (camera %.1f deg), state yaw %.1f deg", run_yaw, (unsigned)w->camera.ang[1],
              (unsigned)want, dir * 180.0 / 3.14159265358979, run_yaw * 180.0 / 3.14159265358979,
              (double)s.yaw * 180.0 / 3.14159265358979);
    }
    sa1g_character_world_destroy(&c, w);
    sa1g_wctx_free(&c);
}

/* ---- per landtable dump: FAITHFUL context, its traces, the DISC context ------------------------------- */
static int land_open(env *E, land *L)
{
    sa1_collision_info info;
    sa1_status st;
    printf("landtable dump %s (stage %d act %d; characters %X):\n", L->prefix, (int)L->level, (int)L->act,
           (unsigned)L->chars);
    L->ctx = make_ctx(E, L->chars);
    if (L->ctx == NULL) {
        CHECK(0, "context / disc for %s", L->prefix);
        return -1;
    }
    st = sa1ca_dump_load(L->ctx, L->prefix, &L->dump);
    L->dump_ok = st == SA1_OK;
    L->ref_obj = load_col_objects(L->prefix, &L->ncol);
    CHECK(st == SA1_OK && L->ref_obj != NULL && L->ncol == L->dump.ncol,
          "landtable dump %s: %s (%u COLs, %u triangles; strips %u: flip deduced %u, assumed %u, conflicts %u)",
          L->prefix, st == SA1_OK ? "ok" : sa1_last_error(L->ctx), (unsigned)L->dump.ncol, (unsigned)L->dump.ntri,
          (unsigned)L->dump.strips, (unsigned)L->dump.parity_deduced, (unsigned)L->dump.parity_assumed,
          (unsigned)L->dump.parity_conflicts);
    if (st != SA1_OK) {
        printf("    (the dump is made by the harness: trace <script args> --dump-land %s)\n", L->prefix);
        return -1;
    }
    st = sa1_collision_set_static_ex(L->ctx, &L->dump.desc);
    memset(&info, 0, sizeof info);
    info.struct_size = sizeof info;
    sa1_collision_info_get(L->ctx, &info);
    if (L->level == 1 && L->act == 0) {
        CHECK(st == SA1_OK && info.mode == SA1_COLLISION_MODE_FAITHFUL && info.groups == 445
                  && info.collidable_groups == 151 && info.collidable_triangles == 13701,
              "sa1_collision_set_static_ex(FAITHFUL): %s, %u groups (%u collidable, %u triangles)", sa1_status_string(st),
              (unsigned)info.groups, (unsigned)info.collidable_groups, (unsigned)info.collidable_triangles);
    } else {
        CHECK(st == SA1_OK && info.mode == SA1_COLLISION_MODE_FAITHFUL && info.groups == L->dump.ncol
                  && info.collidable_groups > 0 && info.collidable_triangles > 0,
              "sa1_collision_set_static_ex(FAITHFUL): %s, %u groups (%u collidable, %u triangles)", sa1_status_string(st),
              (unsigned)info.groups, (unsigned)info.collidable_groups, (unsigned)info.collidable_triangles);
    }
    return st == SA1_OK ? 0 : -1;
}

static void land_disc_open(env *E, land *L, int first)
{
    sa1_collision_static d;
    printf("DISC mode for %s (a second context: sa1_collision_set_static_ex(DISC, stage %d act %d)):\n", L->prefix,
           (int)L->level, (int)L->act);
    L->dctx = make_ctx(E, L->chars);
    if (L->dctx == NULL) {
        CHECK(0, "DISC context");
        return;
    }
    if (first) {
        sa1_character_desc cd;
        sa1_character_id id = 0;
        sa1_status st;
        int32_t kind = SA1_CHAR_SONIC;
        while (kind < SA1_CHAR_COUNT && !(L->chars & (1u << kind))) {
            kind++;
        }
        memset(&cd, 0, sizeof cd);
        cd.struct_size = sizeof cd;
        cd.kind = kind < SA1_CHAR_COUNT ? kind : SA1_CHAR_SONIC;
        cd.flags = SA1_CREATE_WORLD | SA1_CREATE_START_TABLE;
        st = sa1_character_create_ex(L->dctx, &cd, &id);
        CHECK(st == SA1_PARTIAL && sa1_character_world(L->dctx, id) != NULL, "a world without static collision: %s (%s)",
              sa1_status_string(st), sa1_last_error(L->dctx));
        sa1_character_destroy(L->dctx, id);
    }
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.mode = SA1_COLLISION_MODE_DISC;
    d.disc_level = L->level;
    d.disc_act = L->act;
    CHECK(sa1_collision_set_static_ex(L->dctx, &d) == SA1_OK, "DISC description");
}

static void land_close(land *L)
{
    if (L->ctx != NULL) {
        sa1ca_dump_free(L->ctx, &L->dump);
    }
    free(L->ref_obj);
    free(L->faith_obj);
    L->ref_obj = L->faith_obj = NULL;
    if (L->ctx != NULL) {
        sa1_shutdown(L->ctx);
    }
    if (L->dctx != NULL) {
        sa1_shutdown(L->dctx);
    }
    L->ctx = L->dctx = NULL;
}

static uint32_t land_of(env *E, const char *prefix, int16_t level, int16_t act)
{
    uint32_t i;
    for (i = 0; i < E->nlands; i++) {
        if (!strcmp(E->lands[i].prefix, prefix)) {
            return i;
        }
    }
    if (E->nlands >= MAX_LANDS) {
        return MAX_LANDS;
    }
    memset(&E->lands[i], 0, sizeof E->lands[i]);
    snprintf(E->lands[i].prefix, sizeof E->lands[i].prefix, "%s", prefix);
    E->lands[i].level = level;
    E->lands[i].act = act;
    E->nlands++;
    return i;
}

static const char *tag_of(const char *name)
{
    size_t i;
    for (i = 0; i < sizeof k_traces / sizeof k_traces[0]; i++) {
        if (!strcmp(k_traces[i].name, name)) {
            return k_traces[i].tag;
        }
    }
    return "trace";
}

int main(int argc, char **argv)
{
    static env E;
    static trace_job jobs[MAX_JOBS];
    uint32_t njobs = 0, j, li;
    int i, disc_first = 1;
    (void)wr_replay;            /* world_replay.c helpers this test does not use */
    (void)wr_result_print;
    ga_args(&argc, argv);
    E.gdi = getenv("SA1_TEST_GDI");
    E.disc = 1;
    E.strict = 1;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--out") && i + 1 < argc) {
            E.out = argv[++i];
        } else if (!strcmp(argv[i], "--land") && i + 1 < argc) {
            E.land_override = argv[++i];
        } else if (!strcmp(argv[i], "--trace") && i + 1 < argc) {
            if (njobs < MAX_JOBS) {
                jobs[njobs++].name = argv[++i];
            } else {
                ++i;
            }
        } else if (!strcmp(argv[i], "--strict")) {
            E.strict = 1;               /* the default */
        } else if (!strcmp(argv[i], "--tolerant")) {
            E.strict = 0;
        } else if (!strcmp(argv[i], "--no-stage-tasks")) {
            g_create_flags &= ~SA1_CREATE_STAGE_TASKS;
        } else if (!strcmp(argv[i], "--pure")) {
            E.pure = 1;
        } else if (!strcmp(argv[i], "--no-disc")) {
            E.disc = 0;
        } else if (!strcmp(argv[i], "--dump-ram") && i + 1 < argc) {
            g_dump_step = atoi(argv[++i]);
        } else if (E.sadc == NULL) {
            E.sadc = argv[i];
        } else {
            E.gdi = argv[i];
        }
    }
    if (E.sadc == NULL || E.gdi == NULL || !*E.gdi) {
        printf("usage: replay_test [--out DIR] [--land PREFIX] [--trace NAME]... [--tolerant] [--no-stage-tasks] "
               "[--pure] [--no-disc] [--dump-ram STEP] SADC [GDI]\n");
        return 2;
    }
    if (njobs == 0) {
        for (j = 0; j < sizeof k_traces / sizeof k_traces[0] && njobs < MAX_JOBS; j++) {
            jobs[njobs++].name = k_traces[j].name;
        }
    }
    printf("libsa1 %s replay test (M1c / M1 / M1.1 / Phase 7 through the public API): %d-bit, game code: %s\n",
           sa1_version_string(), (int)(sizeof(void *) * 8), sa1g_game_info());
    if (!sa1g_game_ready()) {
        printf("FAIL: the step's game code is not linked\n");
        return 1;
    }
    /* the traces, and the landtable dump of each trace's stage */
    for (j = 0; j < njobs; j++) {
        char path[1100], prefix[1024];
        trace_job *J = &jobs[j];
        J->tag = tag_of(J->name);
        snprintf(path, sizeof path, "%s/build/traces/ref/%s.bin", E.sadc, J->name);
        if (wr_trace_load(path, &J->t) != 0) {
            CHECK(0, "reference trace %s", path);
            continue;
        }
        if (E.land_override != NULL) {
            snprintf(prefix, sizeof prefix, "%s", E.land_override);
        } else {
            snprintf(prefix, sizeof prefix, "%s/build/traces/land/L%02dA%d", E.sadc, (int)J->t.h.level, (int)J->t.h.act);
        }
        J->land = land_of(&E, prefix, J->t.h.level, J->t.h.act);
        if (J->land >= MAX_LANDS) {
            CHECK(0, "too many landtable dumps");
            wr_trace_free(&J->t);
            continue;
        }
        if (J->t.h.chr >= 0 && J->t.h.chr < SA1_CHAR_COUNT) {
            E.lands[J->land].chars |= 1u << J->t.h.chr;
        }
        J->loaded = 1;
    }
    for (li = 0; li < E.nlands; li++) {
        land *L = &E.lands[li];
        trace_job *ec1_run_jump = NULL, *first = NULL;
        if (land_open(&E, L) == 0) {
            for (j = 0; j < njobs; j++) {
                if (jobs[j].loaded && jobs[j].land == li) {
                    run_trace(&E, &jobs[j]);
                    if (first == NULL) {
                        first = &jobs[j];
                    }
                    if (!strcmp(jobs[j].name, "ec1_run_jump") && jobs[j].t.n > 150) {
                        ec1_run_jump = &jobs[j];
                    }
                }
            }
            if (ec1_run_jump != NULL) {
                printf("M1 exit 3 (ec1_run_jump: the land collision cache against harness --dump-ram-at):\n");
                test_cache(&E, ec1_run_jump, 30);
                test_cache(&E, ec1_run_jump, 150);
            }
            {
                sa1_collision_info info;
                memset(&info, 0, sizeof info);
                info.struct_size = sizeof info;
                sa1_collision_info_get(L->ctx, &info);
                CHECK(info.builds == 1 && info.landtable == L->faith_lt, "%s: one synthesis served every world of the "
                      "geometry generation (%u builds, %u bytes, %u meshsets, %u materials)", L->prefix,
                      (unsigned)info.builds, (unsigned)info.bytes, (unsigned)info.meshsets, (unsigned)info.materials);
            }
            if (E.disc) {
                land_disc_open(&E, L, disc_first);
                disc_first = 0;
                for (j = 0; j < njobs; j++) {
                    if (jobs[j].loaded && jobs[j].land == li) {
                        run_disc(&E, &jobs[j]);
                    }
                }
            }
            /* the live test: Sonic on the first EC1 trace's land */
            if (first != NULL && first->t.h.chr == SA1_CHAR_SONIC && L->level == 1 && L->act == 0) {
                test_live(L->ctx, &first->t);
                test_sketch(L->ctx, E.gdi);
            }
        }
        land_close(L);
    }
    for (j = 0; j < njobs; j++) {
        free(jobs[j].v6);
        wr_trace_free(&jobs[j].t);
    }
    CHECK(g_allocs == g_frees, "every context allocation was freed (%ld allocations, %ld frees)", g_allocs, g_frees);
    if (E.strict && g_known != 0) {
        CHECK(0, "strict: %d known issue(s) (each one is a failure)", g_known);
    }
    printf("%s: %d failure(s), %d skipped, %d known issue(s)\n", g_fail ? "FAIL" : "PASS", g_fail, g_skip, g_known);
    return g_fail ? 1 : 0;
}
