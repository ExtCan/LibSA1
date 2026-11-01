/*
 * sa1_world_board.c - Sonic's and Tails' board inside a character world (round 0.9.5, SA1_API_BOARD; sadc
 * docs/research/gsa1_095_plan.md 4.4, 5.3, 6.2 with its Review R.2, R.4, R.5, R.9; board095_lib.md 4.2 rules 1-14). The
 * API side (arguments, kinds, building the world) is src/sa1_board.c; the two meet through src/sa1_api_world.h (sa1wb_*).
 *
 * THE MECHANISM - what the stage code does (SBOARD 0C9031AE, STG08 0C90EA14): SetInputP(0, 44) (8C07C65E) and
 * CreateElementalTask(3, 2, 8C0B2EE6 Sonic / 8C0BF7CE Tails). The order handler (Sonic 8C0AFF1A, Tails 8C0BBDD6 case 44)
 * puts player 0 on the board at his next exec (Sonic modes 62-68, Tails 48-54); the board task's exec (8C0B2F46 /
 * 8C0BF804) glues the board to him while his action is a board action (Sonic 102-124, Tails 107-127) and runs the game's
 * free-object step 8C05872A otherwise (a board left behind: a KEPT board). Off: SetInputP(0, 24), as Ice Cap's goal 0C90E900
 * does; both board handlers take it in every board mode (code_8C0B0E94.c:17-37, code_8C0BCBCA.c:56-83).
 *
 * A request (sa1wb_request, between ticks) is latched in the lane's block and carried out at the next pair start (pre, sub
 * step 0, after the pad update and before the executor). That is the moment the stage code's own calls take effect: the
 * order is read by the player's next exec and the new task runs in the same executor pass, as when they are made between
 * two pairs (board095_lib.md 2.2). Nothing is written into the game between ticks.
 *
 * Host rules (each named in libsa1.h's board section):
 *   rule 3 / 4.4 a  ON with a KEPT board frees it (FreeTask 8C098786, SA1_BOARD_EV_FREED arg 2) and makes a new one;
 *                   SA1_BOARD_REUSE_KEPT posts 44 alone (Ice Cap's re-entry 0C90DFE0 state 7: the old board snaps back)
 *   rule 4          ON while riding or pending posts nothing (Tails' board handler consumes any order: code_8C0BCBCA.c:81);
 *                   only the terrain and the flags change
 *   rule 5          after the pair that followed the posting, player 0 not in a board mode: SA1_BOARD_EV_REFUSED (arg = his
 *                   mode), the board task this request made and the push task are freed
 *   rule 7          Tails' p.jmp_y_spd (playerwk + 0x9C + 0x18) back to its value before the get-on when the board mode ends,
 *                   unless SA1_BOARD_FAITHFUL_EXIT (the game's x1.2 per ride, code_8C0BBDD6.c:252, compounds)
 *   rule 8          a KEPT board 128 units below the last height where it stood (taskwk.flag & 1) is freed (EV_FREED arg
 *                   1) unless SA1_BOARD_KEEP_FALLEN; SA1_BOARD_REMOVE_ON_EXIT (read at ON and at OFF: review R.5) frees it in
 *                   the step the board mode ends (arg 0); SA1_BOARD_FREE frees it on request (arg 3)
 *   review R.2      the push task (step 3) belongs to the ride: freed in the step the board mode ends and at a refusal
 *   review R.9      FreeTask only sets exec = DestroyTask (code_8C098444.c:349-352) and the struct is reused by later
 *                   tasks: every record of a board task is reset for each task this lane makes and dropped as soon as its
 *                   exec is not a board exec (init 8C0B2EE6 / 8C0BF7CE, exec 8C0B2F46 / 8C0BF804); the state reports
 *                   board_task 0 right after a free
 *   the draws       a board task's draws are left out until it was held once (it starts as the free object at the SA1
 *                   origin: board095_lib.md 2.3 G); presentation only (sa1wb_draw_hidden, read by sa1_character.c's
 *                   render capture)
 *   the terrain     w->board_col (sa1_world.h) for the collision window (sa1_coladapt_world.c): 0 on foot, else
 *                   SA1WB_COL_RIDING | the terrain in force, computed after every step
 *   the pick        Sonic's board (MIW_B001 / SB_BOARD1, the game's pick in 8C0B2EE6 by the frame counter) through the
 *                   LIBSA1_BOARD exec WB_PICK_CODE, which runs the game's init with the counter read as the pair's parity
 *                   (WB_RANDOM_PICK; 095-fix, owner question O12 "keep the random pick"): see "Sonic's board pick" below
 * Rule 13: the hooks are installed by the first request for a world; a world that never gets one runs exactly as before.
 * Rule 14: the block lives in segment 0x18 (magic + self, as sa1_world_emote.c's): a rebuild resets the segment and with it
 * forgets the board; nothing persists (the host re-applies).
 */
#include "sa1_world.h"
#include "sa1_api_world.h"
#include "pc/sh4_fpmodel.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define WB_MAGIC            0x44524F42u     /* "BORD" */
#define WB_FN_SETINPUTP     0x8C07C65Eu     /* SetInputP(pno, order) */
#define WB_FN_FREETASK      0x8C098786u     /* FreeTask(tp): exec = DestroyTask */
#define WB_SONIC_INIT       0x0C0B2EE6u     /* physical code addresses (& 0x1FFFFFFF) */
#define WB_SONIC_EXEC       0x0C0B2F46u
#define WB_TAILS_INIT       0x0C0BF7CEu
#define WB_TAILS_EXEC       0x0C0BF804u
#define WB_SONIC_TEXLIST0   0x8C1C6380u     /* D_8C1C6380: MIW_B001 (variant 0); else S_SBMOT's 0x0CB36F94 SB_BOARD1 */
#define WB_SONIC_DROP       (-5.22f)        /* lbl_8C0B3068 */
#define WB_TAILS_DROP       (-5.190199f)    /* 8C0BF92C */
#define WB_FALL_UNITS       128.0f
#define WB_PWP_JMP_Y_SPD    0xB4u           /* playerwk.p (0x9C) .jmp_y_spd (+0x18) */
#define WB_PWP_ACTION       0x126u          /* playerwk.mj.action */
#define WB_PWP_SPD_X        0x38u

/* ---- Sonic's board pick (095-fix: the 0.9.5 verifier's fail 1; gsa1_095_plan.md 1, 7 and owner question O12, default
 * "keep the random pick") ------------------------------------------------------------------------------------------------
 * 8C0B2EE6 picks Sonic's board by the frame counter: n = func_8C08A71E() % (D_8C1C63A0 + 1) = B_8C752BE8 % 2, MIW_B001
 * (n 0, D_8C1C6380[0]) or SB_BOARD1 (n 1, S_SBMOT's texlist) (sadc code_8C0B2EA0.c:77-95, code_8C08A71E.c:47-49). In libsa1
 * worlds B_8C752BE8 moves by 2 a pair, after both steps (8C08A5BE: += B_8C754EC4 = 2; sa1_world_tick.c sa1g_world_pair), as on
 * the Dreamcast at 30 fps; there its parity changes with the frames the game runs at one step a frame (the events,
 * 8C051760(2, 1): sadc docs/research/v1_machinima.md 2) and is zeroed at a stage start (8C08A578). Host worlds run no such
 * frame, so the counter's parity never changes and the game's pick would always be MIW_B001 (measured: board_test (k) rides
 * 0-3, the census of G-SA1's core_test). Landing the request in the pair's second step would not change it: both steps see
 * the same counter.
 * HOST TIMING RULE (WB_RANDOM_PICK 1): Sonic's board task is made with the exec WB_PICK_CODE (LIBSA1_BOARD), which calls the
 * game's own init 8C0B2EE6 once with B_8C752BE8 read as C + (C / 2) % 2 - a counter that moved by one more step on every
 * second pair - and puts C back right after it; the init then sets the board's exec 8C0B2F46 itself, in the same executor
 * slot. The game's pick thus alternates with the pair the request lands in (as random as the player's timing, which decides
 * it on the Dreamcast too) and stays deterministic (two worlds with the same inputs pick the same board). Presentation only:
 * the texlist (taskwk value.ptr, set with 8C608C82) is all the pick writes; nothing else reads the counter in the init.
 * WB_RANDOM_PICK 0: the world's counter as it is (always MIW_B001 in host worlds). Tails has one board (no pick). */
#define WB_RANDOM_PICK      1
#define SA1WB_PICK_CODE     0x7F010300u
#define WB_PICK_KEY         (SA1WB_PICK_CODE & 0x1FFFFFFFu)
#define WB_SONIC_INIT_DC    0x8C0B2EE6u

enum { WB_REQ_NONE = -1 };

typedef struct wb_state {
    uint32_t magic, self;
    /* the latched request (sa1wb_request), carried out at the next pair start */
    int32_t req;                /* WB_REQ_NONE or SA1_BOARD_ON / OFF / PUSH / FREE */
    uint32_t req_terrain, req_flags;
    /* the ride */
    int32_t kind;               /* the world's character kind at the request (0 / 2) */
    uint32_t terrain;           /* the terrain of the current or the pending ride */
    uint32_t flags;             /* SA1_BOARD_* of the ride (REMOVE_ON_EXIT also from an OFF) */
    int32_t riding;             /* player 0 was in a board mode after the last step */
    int32_t pending;            /* 44 posted (or latched), not answered yet */
    int32_t posted;             /* 44 was posted at the start of the pair pending_pair (the refusal waits for its end) */
    uint32_t pending_pair;      /* w->pairs when it was posted */
    uint32_t pending_task;      /* the board task the pending ON made (0: re-used) */
    int32_t smode_seen;         /* the order player 0 had at the start of the last step (smode with flag 0x1000), 0 none */
    int32_t jmp_saved;
    float jmp_y_spd;            /* Tails' p.jmp_y_spd before the get-on */
    int32_t mode;               /* player 0's mode after the last step */
    /* the board task (review R.9: reset for every task this lane makes) */
    uint32_t task;              /* guest address, 0 none */
    uint32_t task_flags;        /* the flags of the ride that made (or re-used) it: KEEP_FALLEN outlives the ride */
    int32_t task_kind;
    uint32_t variant;
    int32_t held, held_once, draw_ok;
    int32_t pos_valid;
    float last_pos[3];
    int32_t stand_valid;
    float stand_y;
    uint32_t still;
    int32_t pose_valid;
    float mpos[3], mrot[9];     /* SA1 space: the model's origin and rotation as the game draws it */
    /* the start push */
    uint32_t push_task;
    int32_t push_on_take;       /* START_PUSH: the push starts in the step the game takes the order (review R.2 / (d3)) */
    /* bookkeeping */
    uint32_t ons, offs, refusals, frees;
} wb_state;

/* ---- arena access ------------------------------------------------------------------------------------------------- */
static uint32_t wb_rd32(const sa1g_world *w, uint32_t g)
{
    uint32_t v = 0;
    if (g == 0 || sa1g_world_rd32(w, g, &v) != 0) {
        return 0;
    }
    return v;
}

static float wb_rdf(const sa1g_world *w, uint32_t g)
{
    uint32_t v = wb_rd32(w, g);
    float f;
    memcpy(&f, &v, 4);
    return f;
}

static void wb_wrf(sa1g_world *w, uint32_t g, float f)
{
    uint32_t v;
    memcpy(&v, &f, 4);
    sa1g_world_wr32(w, g, v);
}

static int32_t wb_rd8s(const sa1g_world *w, uint32_t g)
{
    uint32_t v = wb_rd32(w, g & ~3u);
    return (int32_t)(int8_t)(uint8_t)(v >> (8u * (g & 3u)));
}

static uint32_t wb_rd16(const sa1g_world *w, uint32_t g)
{
    uint32_t v = wb_rd32(w, g & ~3u);
    return (v >> (8u * (g & 2u))) & 0xFFFFu;
}

static int wb_arena(uint32_t g)
{
    return g != 0 && ((g >> 24) & 31u) == SA1G_SEG_ARENA;
}

/* ---- the block ---------------------------------------------------------------------------------------------------- */
static wb_state *wb_state_of(const sa1g_world *w)
{
    uint32_t g;
    wb_state *S;
    if (w == NULL || w->ext == NULL || w->board.state == 0) {
        return NULL;
    }
    g = w->board.state;
    if (((g >> 24) & 31u) != SA1G_SEG_EXT || (g & 15u) != 0 || (size_t)(g & 0xFFFFFFu) + sizeof(wb_state) > w->ext_used) {
        return NULL;
    }
    S = (wb_state *)(void *)(w->ext + (g & 0xFFFFFFu));
    return S->magic == WB_MAGIC && S->self == g ? S : NULL;
}

static void wb_task_reset(wb_state *S, uint32_t tp, int32_t kind)
{
    S->task = tp;
    S->task_flags = 0;
    S->task_kind = kind;
    S->variant = 0;
    S->held = S->held_once = S->draw_ok = 0;
    S->pos_valid = S->stand_valid = 0;
    S->still = 0;
    S->pose_valid = 0;
}

static wb_state *wb_state_make(sa1g_world *w)
{
    wb_state *S = wb_state_of(w);
    uint32_t g;
    if (S != NULL) {
        return S;
    }
    g = sa1g_world_ext_alloc(w, sizeof(wb_state), 16, (void **)&S);
    if (g == 0 || S == NULL) {
        return NULL;
    }
    memset(S, 0, sizeof *S);
    S->magic = WB_MAGIC;
    S->self = g;
    S->req = WB_REQ_NONE;
    S->kind = -1;
    wb_task_reset(S, 0, -1);
    w->board.state = g;
    return S;
}

/* ---- the game's facts ----------------------------------------------------------------------------------------------- */
static int wb_board_mode(int32_t kind, int32_t mode)
{
    return kind == SA1_CHAR_TAILS ? (mode >= 48 && mode <= 54) : (mode >= 62 && mode <= 68);
}

static int wb_board_action(int32_t kind, int32_t action)
{
    return kind == SA1_CHAR_TAILS ? (action >= 107 && action <= 127) : (action >= 102 && action <= 124);
}

/* riding on the ground: Sonic 62 / 64, Tails 48 / 50 (the modes PUSH accepts) */
static int wb_ground_mode(int32_t kind, int32_t mode)
{
    return kind == SA1_CHAR_TAILS ? (mode == 48 || mode == 50) : (mode == 62 || mode == 64);
}

static int wb_board_exec(uint32_t exec, int32_t kind, int with_init)
{
    exec &= 0x1FFFFFFFu;
    if (kind == SA1_CHAR_TAILS) {
        return exec == WB_TAILS_EXEC || (with_init && exec == WB_TAILS_INIT);
    }
    return exec == WB_SONIC_EXEC || (with_init && (exec == WB_SONIC_INIT || exec == WB_PICK_KEY));
}

/* the record of the board task still names a board task (review R.9) */
static int wb_task_live(const sa1g_world *w, const wb_state *S)
{
    return S->task != 0 && wb_arena(S->task) && wb_board_exec(wb_rd32(w, S->task + 0x10u), S->task_kind, 1);
}

static int32_t wb_player_mode(const sa1g_world *w, sa1g_player_ptrs *pp)
{
    sa1g_world_player(w, pp);
    if (pp->twp == 0) {
        return -1;
    }
    return wb_rd8s(w, pp->twp);
}

/* 1 when the world image holds the kind's board actions (S_SBMOT for Sonic, M_SBMOT for Tails: sa1_world_image.c) */
static int wb_image_has_boards(const sa1g_world *w, int32_t kind)
{
    const char *want = kind == SA1_CHAR_TAILS ? "M_SBMOT.PRS" : "S_SBMOT.PRS";
    uint32_t i;
    if (w->image == NULL) {
        return 0;
    }
    for (i = 0; i < w->image->num_spans; i++) {
        if (strcmp(w->image->spans[i].name, want) == 0) {
            return 1;
        }
    }
    return 0;
}

/* ---- in-world calls (pre / post run inside the world) --------------------------------------------------------------- */
static void wb_free_task(uint32_t tp)
{
    void (*freetask)(void *) = (void (*)(void *))sa1_fn(SA1_GFN_C(WB_FN_FREETASK), 0u);
    freetask(sa1_decode(tp));
}

static void wb_setinputp(int32_t order)
{
    void (*setinp)(uint8_t, int32_t) = (void (*)(uint8_t, int32_t))sa1_fn(SA1_GFN_C(WB_FN_SETINPUTP), 0u);
    setinp(0, order);
}

static uint32_t wb_create(uint16_t im, int32_t level, uint32_t exec)
{
    void *(*create)(uint16_t, int32_t, sa1_gfn) =
        (void *(*)(uint16_t, int32_t, sa1_gfn))sa1_fn(SA1_GFN_C(SA1G_FN_CREATE_TASK), 0u);
    void *tp = create(im, level, SA1_GFN_C(exec));
    return tp != NULL ? sa1_encode(tp) : 0u;
}

static void wb_event(sa1g_world *w, uint32_t id, uint32_t bank, const float pos[3], uint32_t arg)
{
    sa1g_world_event_push(w, SA1_EVENT_BOARD, id, bank, pos, arg);
}

static void wb_player_pos(const sa1g_world *w, const sa1g_player_ptrs *pp, float p[3])
{
    uint32_t k;
    for (k = 0; k < 3; k++) {
        p[k] = pp->twp ? wb_rdf(w, pp->twp + 0x20u + 4u * k) : 0.0f;
    }
}

static void wb_task_pos(const sa1g_world *w, uint32_t tp, float p[3])
{
    uint32_t twp = wb_rd32(w, tp + 0x20u), k;
    for (k = 0; k < 3; k++) {
        p[k] = wb_arena(twp) ? wb_rdf(w, twp + 0x20u + 4u * k) : 0.0f;
    }
}

/* frees the lane's board task (an event with arg `why`) */
static void wb_free_board(sa1g_world *w, wb_state *S, uint32_t why)
{
    float p[3];
    if (!wb_task_live(w, S)) {
        wb_task_reset(S, 0, -1);
        return;
    }
    wb_task_pos(w, S->task, p);
    wb_free_task(S->task);
    wb_event(w, SA1_BOARD_EV_FREED, 0u, p, why);
    S->frees++;
    wb_task_reset(S, 0, -1);
}

/* the push task (step 3: sa1wb_push_* below); review R.2: it ends with the ride */
static uint32_t wb_push_live(const sa1g_world *w, const wb_state *S);
static void wb_push_free(sa1g_world *w, wb_state *S)
{
    if (wb_push_live(w, S)) {
        wb_free_task(S->push_task);
    }
    S->push_task = 0;
}

/* ---- the start push (gsa1_095_plan.md 6.2 task 6; board095_lib.md 4.2 rule 9) -----------------------------------------
 * Ice Cap's own start push, STG08 0C90E810 (decompiled by 095-land: SADC src/overlays/stg08/code_0C90E810.c, MATCH, lowered
 * by pclower as STG08__func_0C90E810 in the code map of image STG08): every step players 0 and 1 get spd.x >= 4.0; the task
 * frees itself after 102 steps (the counter compare before the increment). It touches only 1ST_READ code (GetPlayerWorkPtr
 * 8C07CFDE, FreeTask) and its own awp, so it runs in any world - at a synthetic code address of LIBSA1_BOARD
 * (SA1WB_PUSH_CODE 0x7F010200), never at its overlay address 0x0C90E810 (overlay addresses alias between images: B_CHAOS7's
 * code range is 0C900080-0C91E664). CreateElementalTask(8, 2, ...) as the course start 0C90E900 does. Its 8C07C8E0 (a
 * hop, the course facing, 60 steps without control) is left out. Review R.2: the push task ends with the ride. */
#define SA1WB_PUSH_CODE     0x7F010200u
#define WB_PUSH_KEY         (SA1WB_PUSH_CODE & 0x1FFFFFFFu)
#define WB_PUSH_STEPS       102u

/* the lowered STG08 function through STG08's own code map (no link-time dependency: a build without it has no push) */
static void (*wb_push_fn(void))(void *)
{
    const sa1g_fnimage *im = sa1g_fnimage_find("STG08");
    const sa1g_fnent *e = im != NULL && im->lookup != NULL ? im->lookup(0x0C90E810u) : NULL;
    return e != NULL && e->fn != NULL ? (void (*)(void *))e->fn : NULL;
}

int sa1wb_push_available(void)
{
    return wb_push_fn() != NULL;
}

static void wb_push_exec(void *tpn)
{
    void (*fn)(void *) = wb_push_fn();
    if (fn != NULL && tpn != NULL) {
        fn(tpn);
    }
}

static uint32_t wb_push_live(const sa1g_world *w, const wb_state *S)
{
    return S->push_task != 0 && wb_arena(S->push_task) &&
           (wb_rd32(w, S->push_task + 0x10u) & 0x1FFFFFFFu) == WB_PUSH_KEY;
}

static void wb_push_start(sa1g_world *w, wb_state *S, uint32_t arg, const float pos[3])
{
    wb_push_free(w, S);
    S->push_task = wb_create(8, 2, SA1WB_PUSH_CODE);
    if (S->push_task != 0) {
        wb_event(w, SA1_BOARD_EV_PUSH, 0u, pos, arg);
    }
}

/* ---- the pair start: the latched request ------------------------------------------------------------------------- */
static void wb_do_on(sa1g_world *w, wb_state *S, const sa1g_player_ptrs *pp, int32_t kind)
{
    /* Sonic: the game's init through the pick exec (WB_RANDOM_PICK, "Sonic's board pick" above) */
    uint32_t init = kind == SA1_CHAR_TAILS ? 0x8C0BF7CEu : WB_RANDOM_PICK ? SA1WB_PICK_CODE : WB_SONIC_INIT_DC;
    float p[3];
    wb_player_pos(w, pp, p);
    S->kind = kind;
    S->terrain = S->req_terrain;
    S->flags = S->req_flags;
    /* rule 7: Tails' jump speed before the get-on */
    S->jmp_saved = 0;
    if (kind == SA1_CHAR_TAILS && pp->pwp != 0) {
        S->jmp_y_spd = wb_rdf(w, pp->pwp + WB_PWP_JMP_Y_SPD);
        S->jmp_saved = 1;
    }
    S->pending_task = 0;
    if (wb_task_live(w, S) && !S->held && (S->req_flags & SA1_BOARD_REUSE_KEPT)) {
        /* the game's re-entry (0C90DFE0 state 7): 44 alone, the kept board takes the player again */
        wb_setinputp(44);
        S->task_flags = S->req_flags;
    } else {
        if (wb_task_live(w, S)) {
            wb_free_board(w, S, 2u);    /* 4.4 a: a new board on every get-on */
        }
        wb_setinputp(44);
        S->pending_task = wb_create(3, 2, init);
        wb_task_reset(S, S->pending_task, kind);
        S->task_flags = S->req_flags;
    }
    S->pending = 1;
    S->posted = 1;
    S->pending_pair = (uint32_t)w->pairs;
    /* the start push begins in the step the game takes the order (wb_post, the riding edge), so a refused ON never raised
       the speed (review R.2: the push belongs to the ride; board_test (d3)) */
    S->push_on_take = (S->req_flags & SA1_BOARD_START_PUSH) != 0;
}

static void wb_pre(struct sa1g_world *w, int32_t sub, void *user)
{
    wb_state *S = wb_state_of(w);
    sa1g_player_ptrs pp;
    int32_t mode;
    (void)user;
    if (S == NULL) {
        w->board_col = 0;
        return;
    }
    if (S->task != 0 && !wb_task_live(w, S)) {
        wb_task_reset(S, 0, -1);        /* review R.9: no longer a board task */
    }
    mode = wb_player_mode(w, &pp);
    S->smode_seen = (pp.twp != 0 && (wb_rd16(w, pp.twp + 4u) & 0x1000u)) ? wb_rd8s(w, pp.twp + 1u) : 0;
    if (sub != 0 || S->req == WB_REQ_NONE || pp.twp == 0 || pp.pwp == 0) {
        return;                         /* a request before the world's first player waits for him */
    }
    switch (S->req) {
    case SA1_BOARD_ON:
        if (!wb_board_mode(S->kind, mode)) {
            wb_do_on(w, S, &pp, S->kind);
        }
        break;
    case SA1_BOARD_OFF:
        if (wb_board_mode(S->kind, mode)) {
            if (S->req_flags & SA1_BOARD_REMOVE_ON_EXIT) {
                S->flags |= SA1_BOARD_REMOVE_ON_EXIT;
            }
            wb_setinputp(24);
        } else if (S->pending) {
            return;                     /* the ON is not answered yet: the OFF waits for the edge */
        }
        break;
    case SA1_BOARD_PUSH:
        if (wb_ground_mode(S->kind, mode)) {
            float p[3];
            wb_player_pos(w, &pp, p);
            wb_push_start(w, S, 1u, p);
        }
        break;
    case SA1_BOARD_FREE:
        if (!wb_board_mode(S->kind, mode) && !S->held) {
            wb_free_board(w, S, 3u);
        }
        break;
    default:
        break;
    }
    S->req = WB_REQ_NONE;
}

/* ---- after every step: edges, the kept board, the pose ---------------------------------------------------------- */
static void wb_pose(const sa1g_world *w, wb_state *S)
{
    uint32_t twp = wb_rd32(w, S->task + 0x20u), k, flag;
    int32_t ax, ay, az;
    float sx, cx, sy, cy, sz, cz, pos[3], drop, rz[9], rx[9], ry[9], t[9];
    int i, j;
    if (!wb_arena(twp)) {
        S->pose_valid = 0;
        return;
    }
    for (k = 0; k < 3; k++) {
        pos[k] = wb_rdf(w, twp + 0x20u + 4u * k);
    }
    ax = (int32_t)wb_rd32(w, twp + 0x14u);
    ay = (int32_t)wb_rd32(w, twp + 0x18u);
    az = (int32_t)wb_rd32(w, twp + 0x1Cu);
    flag = wb_rd16(w, twp + 4u);
    /* lbl_8C0B3068 / 8C0BF92C: njTranslateV(pos), njRotateZ / X / Y (when non-zero), the drop when not held (or held with
       flag & 2); with column vectors the rotation is Rz(z) Rx(x) Ry(y) (the SET layer's st_angles reads the same order) */
    sh4_fsca((uint32_t)ax & 0xFFFFu, &sx, &cx);
    sh4_fsca((uint32_t)ay & 0xFFFFu, &sy, &cy);
    sh4_fsca((uint32_t)az & 0xFFFFu, &sz, &cz);
    if (((uint32_t)ax & 0xFFFFu) == 0) { sx = 0.0f; cx = 1.0f; }
    if (((uint32_t)ay & 0xFFFFu) == 0) { sy = 0.0f; cy = 1.0f; }
    if (((uint32_t)az & 0xFFFFu) == 0) { sz = 0.0f; cz = 1.0f; }
    rz[0] = cz;   rz[1] = -sz;  rz[2] = 0.0f;
    rz[3] = sz;   rz[4] = cz;   rz[5] = 0.0f;
    rz[6] = 0.0f; rz[7] = 0.0f; rz[8] = 1.0f;
    rx[0] = 1.0f; rx[1] = 0.0f; rx[2] = 0.0f;
    rx[3] = 0.0f; rx[4] = cx;   rx[5] = -sx;
    rx[6] = 0.0f; rx[7] = sx;   rx[8] = cx;
    ry[0] = cy;   ry[1] = 0.0f; ry[2] = sy;
    ry[3] = 0.0f; ry[4] = 1.0f; ry[5] = 0.0f;
    ry[6] = -sy;  ry[7] = 0.0f; ry[8] = cy;
    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            t[3 * i + j] = rx[3 * i + 0] * ry[0 + j] + rx[3 * i + 1] * ry[3 + j] + rx[3 * i + 2] * ry[6 + j];
        }
    }
    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            S->mrot[3 * i + j] = rz[3 * i + 0] * t[0 + j] + rz[3 * i + 1] * t[3 + j] + rz[3 * i + 2] * t[6 + j];
        }
    }
    drop = (!S->held || (flag & 2u)) ? (S->task_kind == SA1_CHAR_TAILS ? WB_TAILS_DROP : WB_SONIC_DROP) : 0.0f;
    for (k = 0; k < 3; k++) {
        S->mpos[k] = pos[k] + S->mrot[3 * k + 1] * drop;
    }
    S->pose_valid = 1;
}

static void wb_ride_end(sa1g_world *w, wb_state *S, const sa1g_player_ptrs *pp, int32_t mode)
{
    float p[3];
    uint32_t bank = S->smode_seen == 24 ? 0u : S->smode_seen == 19 ? 1u : 2u;
    wb_player_pos(w, pp, p);
    wb_event(w, SA1_BOARD_EV_OFF, bank, p, (uint32_t)mode);
    S->offs++;
    /* rule 7: Tails' jump */
    if (S->kind == SA1_CHAR_TAILS && S->jmp_saved && !(S->flags & SA1_BOARD_FAITHFUL_EXIT) && pp->pwp != 0) {
        wb_wrf(w, pp->pwp + WB_PWP_JMP_Y_SPD, S->jmp_y_spd);
    }
    S->jmp_saved = 0;
    wb_push_free(w, S);                 /* review R.2 */
    if (S->flags & SA1_BOARD_REMOVE_ON_EXIT) {
        wb_free_board(w, S, 0u);
    }
    S->terrain = SA1_BOARD_TERRAIN_MAP;
    S->flags = 0;
}

static void wb_post(struct sa1g_world *w, int32_t sub, void *user)
{
    wb_state *S = wb_state_of(w);
    sa1g_player_ptrs pp;
    int32_t mode, riding;
    (void)user;
    if (S == NULL) {
        w->board_col = 0;
        return;
    }
    mode = wb_player_mode(w, &pp);
    S->mode = mode;
    riding = S->kind >= 0 && wb_board_mode(S->kind, mode);
    if (riding && !S->riding) {
        float p[3];
        wb_player_pos(w, &pp, p);
        wb_event(w, SA1_BOARD_EV_ON, 0u, p, (uint32_t)mode);
        S->ons++;
        S->pending = 0;
        S->posted = 0;
        S->pending_task = 0;
        if (S->push_on_take) {
            wb_push_start(w, S, 0u, p);     /* STG08's start push at the get-on (the step the board mode starts) */
        }
        S->push_on_take = 0;
    } else if (!riding && S->riding) {
        wb_ride_end(w, S, &pp, mode);
    }
    S->riding = riding;
    /* rule 5: the pair after the posting ended without the board mode */
    if (S->posted && !riding && sub == 1 && (uint32_t)w->pairs == S->pending_pair) {
        float p[3];
        wb_player_pos(w, &pp, p);
        wb_event(w, SA1_BOARD_EV_REFUSED, 0u, p, (uint32_t)mode);
        S->refusals++;
        if (S->pending_task != 0 && S->task == S->pending_task) {
            if (wb_task_live(w, S)) {
                wb_free_task(S->task);
            }
            wb_task_reset(S, 0, -1);
        }
        wb_push_free(w, S);             /* review R.2 */
        S->push_on_take = 0;
        S->pending = 0;
        S->posted = 0;
        S->pending_task = 0;
        S->terrain = SA1_BOARD_TERRAIN_MAP;
        S->flags = 0;
        S->jmp_saved = 0;
    }
    /* the board task */
    if (S->task != 0 && !wb_task_live(w, S)) {
        wb_task_reset(S, 0, -1);
    }
    if (S->task != 0) {
        uint32_t exec = wb_rd32(w, S->task + 0x10u), twp = wb_rd32(w, S->task + 0x20u), flag = 0;
        int32_t action = pp.pwp ? (int32_t)(int16_t)wb_rd16(w, pp.pwp + WB_PWP_ACTION) : -1;
        float pos[3];
        int was_held = S->held;
        wb_task_pos(w, S->task, pos);
        if (wb_arena(twp)) {
            flag = wb_rd16(w, twp + 4u);
        }
        S->held = wb_board_exec(exec, S->task_kind, 0) && wb_board_action(S->task_kind, action) && pp.twp != 0;
        if (S->held) {
            S->held_once = 1;
            S->still = 0;
            S->stand_valid = 0;
        } else if (S->held_once) {
            /* a kept board */
            if (was_held || !S->stand_valid) {
                S->stand_y = pos[1];
                S->stand_valid = 1;
            }
            if (flag & 1u) {
                S->stand_y = pos[1];
            }
            if ((flag & 1u) && S->pos_valid && pos[0] == S->last_pos[0] && pos[1] == S->last_pos[1] &&
                pos[2] == S->last_pos[2]) {
                S->still++;
            } else {
                S->still = 0;
            }
        }
        if (wb_arena(twp) && S->task_kind == SA1_CHAR_SONIC) {
            S->variant = wb_rd32(w, twp + 0x10u) == WB_SONIC_TEXLIST0 ? 0u : 1u;
        }
        memcpy(S->last_pos, pos, sizeof pos);
        S->pos_valid = 1;
        wb_pose(w, S);
        /* rule 8: the fallen kept board */
        if (!S->held && S->held_once && S->stand_valid && !(flag & 1u) && pos[1] < S->stand_y - WB_FALL_UNITS &&
            !(S->task_flags & SA1_BOARD_KEEP_FALLEN)) {
            wb_free_board(w, S, 1u);
        }
    }
    if (sub == 0) {
        S->draw_ok = S->task != 0 && S->held_once;
    }
    w->board_col = riding ? (SA1WB_COL_RIDING | (S->terrain & 0xFu)) : 0u;
}

/* ---- the API side's calls (between ticks) --------------------------------------------------------------------------- */
static wb_state *wb_attach(sa1_context *ctx, sa1g_world *w)
{
    wb_state *S = wb_state_make(w);
    if (S == NULL) {
        return NULL;
    }
    w->board.pre = wb_pre;
    w->board.post = wb_post;
    w->board.fill = NULL;
    w->board.ctx = ctx;
    return S;
}

sa1_status sa1wb_request(sa1_context *ctx, struct sa1g_world *w, int32_t kind, const sa1_board_desc *d, char *why,
                         size_t n)
{
    wb_state *S;
    sa1g_player_ptrs pp;
    int32_t mode;
    int riding, kept;
    if (why != NULL && n > 0) {
        why[0] = '\0';
    }
    if (w == NULL || d == NULL || w->ram == NULL) {
        return SA1_ERR_INVALID_ARGUMENT;
    }
    if (w->dead) {
        return w->dead_status;
    }
    if (!wb_image_has_boards(w, kind)) {
        if (why != NULL) {
            snprintf(why, n, "the world image has no %s (SA1_LOAD_BOARDS)", kind == SA1_CHAR_TAILS ? "M_SBMOT" : "S_SBMOT");
        }
        return SA1_ERR_NOT_LOADED;
    }
    if (d->terrain != SA1_BOARD_TERRAIN_MAP && w->land_mode != SA1G_LAND_WINDOW && w->land_mode != SA1G_LAND_EMPTY) {
        if (why != NULL) {
            snprintf(why, n, "terrain %u: only SA1_BOARD_TERRAIN_MAP in a world without a GRID window (DISC / FAITHFUL)",
                     (unsigned)d->terrain);
        }
        return SA1_ERR_INVALID_ARGUMENT;
    }
    if (((d->on == SA1_BOARD_ON && (d->flags & SA1_BOARD_START_PUSH)) || d->on == SA1_BOARD_PUSH) &&
        !sa1wb_push_available()) {
        if (why != NULL) {
            snprintf(why, n, "the start push is not in this build (STG08 0C90E810 has no C)");
        }
        return SA1_ERR_UNSUPPORTED;
    }
    S = wb_attach(ctx, w);
    if (S == NULL) {
        return SA1_ERR_OUT_OF_MEMORY;
    }
    if (S->task != 0 && !wb_task_live(w, S)) {
        wb_task_reset(S, 0, -1);
    }
    mode = wb_player_mode(w, &pp);
    riding = S->riding || (pp.twp != 0 && wb_board_mode(kind, mode));
    kept = wb_task_live(w, S) && !S->held && S->held_once;
    switch (d->on) {
    case SA1_BOARD_ON:
        if (riding || S->pending) {
            /* rule 4: never 44 again while riding (Tails' handler consumes any order); terrain and flags only */
            S->terrain = d->terrain;
            S->flags = d->flags;
            S->req_terrain = d->terrain;
            S->req_flags = d->flags;
            if (S->req == SA1_BOARD_OFF) {
                S->req = WB_REQ_NONE;   /* ON after an OFF that is not carried out yet: stays on */
            }
            return SA1_OK;
        }
        S->kind = kind;
        S->req = SA1_BOARD_ON;
        S->req_terrain = d->terrain;
        S->req_flags = d->flags;
        S->terrain = d->terrain;
        S->flags = d->flags;
        S->pending = 1;
        S->posted = 0;
        return SA1_OK;
    case SA1_BOARD_OFF:
        if (S->req == SA1_BOARD_ON && !riding) {
            /* the ON was not carried out yet: nothing happens */
            S->req = WB_REQ_NONE;
            S->pending = 0;
            S->posted = 0;
            S->terrain = SA1_BOARD_TERRAIN_MAP;
            S->flags = 0;
            return SA1_OK;
        }
        if (!riding && !S->pending) {
            return SA1_OK;              /* rule 6: not riding, nothing posted */
        }
        S->req = SA1_BOARD_OFF;
        S->req_flags = d->flags & SA1_BOARD_REMOVE_ON_EXIT;
        return SA1_OK;
    case SA1_BOARD_PUSH:
        if (!riding || pp.twp == 0 || !wb_ground_mode(kind, mode)) {
            if (why != NULL) {
                snprintf(why, n, "not riding on the ground");
            }
            return SA1_ERR_UNSUPPORTED;
        }
        S->req = SA1_BOARD_PUSH;
        return SA1_OK;
    case SA1_BOARD_FREE:
        if (riding || S->pending) {
            if (why != NULL) {
                snprintf(why, n, "riding");
            }
            return SA1_ERR_UNSUPPORTED;
        }
        if (!kept) {
            if (why != NULL) {
                snprintf(why, n, "no board left behind");
            }
            return SA1_ERR_UNSUPPORTED;
        }
        /* the record goes now (board_task 0 at once: review R.9); FreeTask at the next pair start */
        S->req = SA1_BOARD_FREE;
        return SA1_OK;
    default:
        return SA1_ERR_INVALID_ARGUMENT;
    }
}

void sa1wb_state_get(const struct sa1g_world *w, sa1wb_state *o)
{
    const wb_state *S = wb_state_of(w);
    uint32_t k;
    memset(o, 0, sizeof *o);
    o->kind = -1;
    o->mrot[0] = o->mrot[4] = o->mrot[8] = 1.0f;
    if (S == NULL || w->dead) {
        return;
    }
    o->terrain = S->riding || S->pending ? S->terrain : SA1_BOARD_TERRAIN_MAP;
    if (S->riding) {
        o->flags |= SA1_BSTATE_ON;
    }
    if (S->pending && !S->riding) {
        o->flags |= SA1_BSTATE_PENDING;
    }
    if (S->task != 0 && wb_task_live(w, S) && S->req != SA1_BOARD_FREE) {
        o->board_task = S->task;
        o->kind = S->task_kind;
        o->variant = S->variant;
        if (S->held) {
            o->flags |= SA1_BSTATE_HELD;
        } else if (S->held_once) {
            o->flags |= SA1_BSTATE_KEPT;
            o->still = S->still;
            if (S->still >= SA1_BOARD_SETTLE_STEPS) {
                o->flags |= SA1_BSTATE_SETTLED;
            }
        }
        if (S->pose_valid) {
            o->pose_valid = 1;
            for (k = 0; k < 3; k++) {
                o->mpos[k] = S->mpos[k];
            }
            memcpy(o->mrot, S->mrot, sizeof o->mrot);
        }
    }
    if (wb_push_live(w, S)) {
        uint32_t awp = wb_rd32(w, S->push_task + 0x2Cu), cnt = wb_arena(awp) ? wb_rd32(w, awp) : WB_PUSH_STEPS;
        o->push_steps = cnt < WB_PUSH_STEPS ? WB_PUSH_STEPS - cnt : 0u;
    }
    o->saved_jmp = S->jmp_saved ? S->jmp_y_spd : 0.0f;
    o->mode = S->mode;
    o->ons = S->ons;
    o->offs = S->offs;
    o->refusals = S->refusals;
    o->frees = S->frees;
}

/* ---- the BOARD preview's exec and the LIBSA1_BOARD code image (gsa1_095_plan.md 6.2 task 4, decision 9) ---------------
 * The board task's display needs a player (lbl_8C0B3068 / 8C0BF92C read GetPlayerWorkPtr(counter.b[0]) every pair), which a
 * showcase has not; so a copy of type SA1_PREVIEW_BOARD is a task of libsa1's own whose exec does what the kept board's
 * display does at the end of its matrix chain: njSetTexture(the board's texlist), njPushMatrix, njTranslateV(its pos: the
 * copy's slot), njDrawModel(the board object's model), njPopMatrix - the game's Ninja calls, under identity rotation and
 * without the -5.22 / -5.190199 drop. The copy's draws in object space (the slot subtracted) are then the model's own
 * points: a kept board's draws transformed back by its pose (sa1_board_state.model_position / model_rotation, which include
 * the rotation and the drop). twp: timer.ptr = the object, value.ptr = the texlist (as the board tasks' inits set them) */
#define WB_FN_SETTEXTURE    0x8C608CCCu     /* njSetTexture(NJS_TEXLIST *) */
#define WB_FN_PUSHMATRIX    0x8C639C70u     /* njPushMatrix(NJS_MATRIX *) */
#define WB_FN_TRANSLATEV    0x8C63A804u     /* njTranslateV(NJS_MATRIX *, NJS_POINT3 *) */
#define WB_FN_DRAWMODEL     0x8C611028u     /* njDrawModel(NJS_MODEL *) */
#define WB_FN_POPMATRIX     0x8C639B98u     /* njPopMatrix(Uint32) */
#define WB_FN_VISIBLE       0x8C038D00u     /* the sphere visibility test (x, y, z, r) */
#define WB_SONIC_OBJECT     0x0CB37454u     /* SONIC_SNOWBOARD_OBJECT (S_SBMOT; 8C0B2EE6's timer.ptr) */
#define WB_TAILS_OBJECT     0x0CB029C4u     /* TAILS_SNOWBOARD_OBJECT (M_SBMOT; 8C0BF7CE's timer.ptr) */
#define WB_SONIC_TEXLIST1   0x0CB36F94u     /* S_SBMOT's texlist {"sb_board1"} */
#define WB_TAILS_TEXLIST    0x0CB025A0u     /* M_SBMOT's texlist {"mtx_board0"} */
#define WB_SIG_VP           0xDA9162B5u     /* pclower's signature id of "v:p" (the executor's exec calls) */
#define WB_PREVIEW_KEY      (SA1WB_PREVIEW_CODE & 0x1FFFFFFFu)

uint32_t sa1wb_board_object(int32_t kind)
{
    return kind == SA1_CHAR_SONIC ? WB_SONIC_OBJECT : kind == SA1_CHAR_TAILS ? WB_TAILS_OBJECT : 0u;
}

uint32_t sa1wb_board_texlist(int32_t kind, uint32_t variant)
{
    if (kind == SA1_CHAR_SONIC) {
        return variant == 0u ? WB_SONIC_TEXLIST0 : variant == 1u ? WB_SONIC_TEXLIST1 : 0u;
    }
    return kind == SA1_CHAR_TAILS && variant == 0u ? WB_TAILS_TEXLIST : 0u;
}

static void wb_preview_exec(void *tpn)
{
    sa1g_world *w = sa1g_world_current();
    uint32_t tp, twp, obj, model, texlist;
    int32_t (*settex)(void *) = (int32_t (*)(void *))sa1_fn(SA1_GFN_C(WB_FN_SETTEXTURE), 0u);
    int32_t (*push)(void *) = (int32_t (*)(void *))sa1_fn(SA1_GFN_C(WB_FN_PUSHMATRIX), 0u);
    void (*transv)(void *, void *) = (void (*)(void *, void *))sa1_fn(SA1_GFN_C(WB_FN_TRANSLATEV), 0u);
    void (*drawm)(void *) = (void (*)(void *))sa1_fn(SA1_GFN_C(WB_FN_DRAWMODEL), 0u);
    int32_t (*pop)(uint32_t) = (int32_t (*)(uint32_t))sa1_fn(SA1_GFN_C(WB_FN_POPMATRIX), 0u);
    if (w == NULL || tpn == NULL) {
        return;
    }
    tp = sa1_encode(tpn);
    twp = wb_rd32(w, tp + 0x20u);
    if (!wb_arena(twp)) {
        return;
    }
    obj = wb_rd32(w, twp + 0x0Cu);
    texlist = wb_rd32(w, twp + 0x10u);
    model = wb_arena(obj) ? wb_rd32(w, obj + 0x04u) : 0u;
    if (!wb_arena(model) || texlist == 0) {
        return;
    }
    {
        /* the board displays' own visibility test (lbl_8C0B3068 / 8C0BF92C: 8C038D00 radius 12), which also keeps the draw
           to the pair's drawn step as in a character world */
        int32_t (*vis)(float, float, float, float) =
            (int32_t (*)(float, float, float, float))sa1_fn(SA1_GFN_C(WB_FN_VISIBLE), 0u);
        if (!vis(wb_rdf(w, twp + 0x20u), wb_rdf(w, twp + 0x24u), wb_rdf(w, twp + 0x28u), 12.0f)) {
            return;
        }
    }
    settex(sa1_decode(texlist));
    push(NULL);
    transv(NULL, sa1_decode(twp + 0x20u));
    drawm(sa1_decode(model));
    pop(1u);
}

/* Sonic's board task's first exec (WB_RANDOM_PICK: "Sonic's board pick" at the top): the game's init 8C0B2EE6 with the frame
   counter read as C + (C / 2) % 2, then C again; the init replaces this exec with 8C0B2F46 */
static void wb_pick_exec(void *tpn)
{
    sa1g_world *w = sa1g_world_current();
    void (*init)(void *) = (void (*)(void *))sa1_fn(SA1_GFN_C(WB_SONIC_INIT_DC), 0u);
    uint32_t c = 0;
    int have = w != NULL && sa1g_world_rd32(w, SA1G_ADDR_FRAME_COUNTER, &c) == 0;
    if (have) {
        sa1g_world_wr32(w, SA1G_ADDR_FRAME_COUNTER, c + ((c >> 1) & 1u));
    }
    init(tpn);
    if (have) {
        sa1g_world_wr32(w, SA1G_ADDR_FRAME_COUNTER, c);
    }
}

static const sa1g_fnent wb_fns[3] = {
    { WB_PREVIEW_KEY, SA1WB_PREVIEW_CODE, WB_SIG_VP, (void (*)(void))wb_preview_exec, "board_preview_exec" },
    { WB_PUSH_KEY, SA1WB_PUSH_CODE, WB_SIG_VP, (void (*)(void))wb_push_exec, "board_push_exec (STG08 0C90E810)" },
    { WB_PICK_KEY, SA1WB_PICK_CODE, WB_SIG_VP, (void (*)(void))wb_pick_exec, "board_pick_exec (8C0B2EE6, Sonic's pick)" },
};

static const sa1g_fnent *wb_lookup(uint32_t g)
{
    uint32_t k = g & 0x1FFFFFFFu, i;
    for (i = 0; i < sizeof wb_fns / sizeof wb_fns[0]; i++) {
        if (wb_fns[i].key == k) {
            return &wb_fns[i];
        }
    }
    return NULL;
}

/* SH-4 area 7 like LIBSA1_HOST (0x7F000100, whose image covers 0x1F000000-0x1F010000): never game code */
const sa1g_fnimage sa1g_board_image = { "LIBSA1_BOARD", "", "", 0x1F010000u, 0x1F020000u, wb_lookup, wb_fns,
                                        sizeof wb_fns / sizeof wb_fns[0] };

int sa1wb_draw_hidden(const struct sa1g_world *w)
{
    const wb_state *S = wb_state_of(w);
    return S != NULL && !S->draw_ok;
}

int sa1wb_kept(const struct sa1g_world *w)
{
    const wb_state *S = wb_state_of(w);
    return S != NULL && S->task != 0 && !S->held && S->held_once;
}
