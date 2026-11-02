/*
 * sa1_world_camreq.c - the game's camera state in every booted world (round 0.9.8; sadc docs/research/fish098.md 3.2,
 * build/staging/fishcam/LANDING.md 5).
 *
 * The camera requests of the game's code (the pushes and pops 8C01ABDC, 8C01AD2C, 8C01AE60, 8C01AF06, 8C01B040, 8C01B158) are
 * real C since round 0.9.8; they run in live AND replay worlds, read this state and fault on a zero one. libsa1 has no camera
 * task, so nothing runs the game's init: sa1g_camreq_boot writes what 8C01957E and 8C01951E write, minus the camera task and
 * the CAM file (a GMod world has none: a host decision; entry -1 keeps B_8C6B7060 and B_8C6B7064 unread). Level 0 is the host's
 * camera: its mode function stays NULL, so the core 8C019F4A returns at once at that level.
 *
 * sa1g_world_reset_run and the image apply zero the BSS and every rebuild boots again, so a rebuilt world has no request.
 * Replays: nothing but the pushes reads this (fish098.md 2.3, all 67 strict references byte-identical with it).
 *
 * The step (SA1_API_CAMERA: sa1g_camreq_pre, sa1wc_state_get) is at the end of the file; the API is src/sa1_camreq.c.
 */
#include "sa1_world.h"
#include "sa1_api_world.h"
#include "sa1_world_look.h"
#include "pc/sh4_fpmodel.h"

#include <string.h>

#define CAM_TABLES      0x8C1958A8u     /* D_8C1958A8: Sint16 mode count, Sint16 adjust count, mode table, adjust table */
#define CAM_MODE_COUNT  0x8C6B7050u     /* B_8C6B7050 (Sint16) */
#define CAM_MODE_TABLE  0x8C6B7054u     /* B_8C6B7054 */
#define CAM_ADJ_COUNT   0x8C6B7058u     /* B_8C6B7058 (Sint16) */
#define CAM_ADJ_TABLE   0x8C6B705Cu     /* B_8C6B705C */
#define CAM_FILE        0x8C6B7060u     /* B_8C6B7060: the act's CAM file */
#define CAM_ENTRIES     0x8C6B7064u     /* B_8C6B7064: its entries */
#define CAM_FOV         0x8C6B7068u     /* B_8C6B7068: the perspective, BAMS */
#define CAM_B706C       0x8C6B706Cu     /* B_8C6B706C */
#define CAM_RUN_STEPS   0x8C6B7078u     /* B_8C6B7078: the core runs adjusts only above 60 */
#define CAM_PEND_B7080  0x8C6B7080u     /* B_8C6B7080 */
#define CAM_PEND_MODE   0x8C6B7084u     /* B_8C6B7084: pending NORMAL mode, -1 = none */
#define CAM_CONTROL     0x8C6B6F4Cu     /* *D_8C11204C, 0x80 bytes (sadc src/1st_read/code_8C01ABDC.c CAMCONTROLWK_FC) */

void sa1g_camreq_boot(sa1g_world *w);
void sa1g_camreq_pre(sa1g_world *w, int32_t sub, void *user);

static uint32_t rd32(sa1g_world *w, uint32_t g)
{
    uint32_t v = 0;
    if (sa1g_world_rd32(w, g, &v) != 0) {
        sa1g_fatal(0, "camera boot: guest word outside the arena");
    }
    return v;
}

static void wr32(sa1g_world *w, uint32_t g, uint32_t v)
{
    if (sa1g_world_wr32(w, g, v) != 0) {
        sa1g_fatal(0, "camera boot: guest word outside the arena");
    }
}

void sa1g_camreq_boot(sa1g_world *w)
{
    uint32_t counts = rd32(w, CAM_TABLES);
    uint32_t modes = rd32(w, CAM_TABLES + 4u);
    uint32_t adjusts = rd32(w, CAM_TABLES + 8u);
    uint16_t mode_count = (uint16_t)counts;
    uint16_t adjust_count = (uint16_t)(counts >> 16);
    uint8_t *c = (uint8_t *)sa1g_world_ptr(w, CAM_CONTROL);
    int32_t dirty = 1;
    int16_t no_entry = -1;
    uint32_t adjust0 = rd32(w, adjusts + 4u);       /* adjust table entry 0's function */

    memcpy((uint8_t *)sa1g_world_ptr(w, CAM_MODE_COUNT), &mode_count, 2);
    wr32(w, CAM_MODE_TABLE, modes);
    memcpy((uint8_t *)sa1g_world_ptr(w, CAM_ADJ_COUNT), &adjust_count, 2);
    wr32(w, CAM_ADJ_TABLE, adjusts);
    wr32(w, CAM_FILE, 0u);
    wr32(w, CAM_ENTRIES, 0u);
    wr32(w, CAM_B706C, 0u);
    wr32(w, CAM_PEND_B7080, 0xFFFFFFFFu);
    wr32(w, CAM_PEND_MODE, 0xFFFFFFFFu);
    wr32(w, CAM_FOV, 0x31C7u);
    wr32(w, CAM_RUN_STEPS, 61u);                    /* the camera has been running for a second */

    memset(c, 0, 0x80u);
    memcpy(c + 0x00, &dirty, 4);
    memcpy(c + 0x04, &no_entry, 2);                 /* no CAM entry */
    c[0x06] = 0;                                    /* mode 0 */
    c[0x07] = 1;                                    /* kind 1 */
    c[0x08] = 0;                                    /* level 0: the host's camera */
    c[0x09] = 0;                                    /* adjust 0 */
    c[0x0B] = 2;                                    /* flags: set by code */
    /* +0x0C the mode function stays NULL: the core returns at once */
    wr32(w, CAM_CONTROL + 0x10u, adjust0);
    /* a rebuilt world has no request: the host-side records go with it (T11); the installed step and the tick's bit stay */
    w->camreq.running = 0;
    memset(w->camreq.drawn, 0, sizeof w->camreq.drawn);
    w->camreq.last_fn = 0u;
    w->camreq.last_level = 0;
}

/* ---- the step (098-lib-lookcam; fish098.md 3.3, 3.4, gsa1_098_plan.md 6.3 C1-C3) ---------------------------------------
 * sa1g_camreq_pre is the part of the camera task's exec 8C0197BA (state 2) a host world needs, one per step, right before the walk
 * (S3: the camera task runs before the players), in the world's FP mode: the timer lines, and for a request LIB knows, with the bit,
 * the game's camera core 8C019F4A and the adjust's end test 8C01B436 (fish098.md 2.2: bit for bit the Dreamcast's). While it runs
 * the camera taskwk is the game camera: sa1g_step_game skips sa1g_camera_refresh (S3), so camera_yaw and host_cam_pos do not reach the
 * taskwk; 8C05117E reads the game camera's ang.y. XMTRX stays the virtual view. No other mode function ever runs (8C021700, 8C0213E0,
 * 8C023360, 8C01FB98, 8C01FD6C: SA1_CAMKIND_OTHER, reported only). Not run, decided: 8C01B500, 8C01B554, 8C01995E, 8C01A100, 8C01BD9E,
 * 8C01C2D8, 8C01A5AE and the B_8C6B708C rumble line (fish098.md 3.3).
 *
 * Installed by sa1g_camreq_install (S9, src/sa1_camreq.c) at the first tick with the bit, live worlds only; never in a replay but through
 * camreq_test T1's hook, which calls this function itself. All game calls through the code map: a --no-game build links (review R98.15).
 *
 * drawn[1] / drawn[0]: the camera at the pair's first step, the step the game draws (libsa1.h:712-714), and the pair before; the first run of a
 * request records at once (it can be a pair's second step), so RUNNING always has a valid record. sa1g_camreq_boot clears them (T11). */
#define CQ_FN_CORE          0x8C019F4Au     /* func_8C019F4A(taskwk *) */
#define CQ_FN_ENDTEST       0x8C01B436u     /* func_8C01B436(taskwk *, taskwk *) */
#define CQ_ADDR_PREV_PTR    0x8C112044u     /* D_8C112044: the taskwk copy of the step before */
#define CQ_ADDR_WORK        0x8C6B6EECu     /* *D_8C112048: the camera work */
#define CQ_WORK_TARGET      0x18u
#define CQ_CTRL_DIRTY       0x00u
#define CQ_CTRL_LEVEL       0x08u
#define CQ_CTRL_ADJUST      0x09u
#define CQ_CTRL_MODEFUNC    0x0Cu
#define CQ_CTRL_TIMER       0x14u
#define CQ_CUT_UNITS2       1681.0f         /* more than the lure camera's 40 units a pair (20.00 a step: fish098.md 2.1), with 1 for the rounding */

static uint32_t cq_rd32(const sa1g_world *w, uint32_t g)
{
    uint32_t v = 0;
    if (sa1g_world_rd32(w, g, &v) != 0) {
        return 0;
    }
    return v;
}

static int32_t cq_rd8s(const sa1g_world *w, uint32_t g)
{
    return (int32_t)(int8_t)(uint8_t)(cq_rd32(w, g & ~3u) >> (8u * (g & 3u)));
}

static uint32_t cq_kind(uint32_t fn)
{
    switch (fn & 0x1FFFFFFFu) {
    case 0x0C0239C2u: return SA1_CAMKIND_AIM;
    case 0x0C0EAF28u: return SA1_CAMKIND_LURE;
    case 0x0C0EAA80u: return SA1_CAMKIND_CATCH;
    default:          return SA1_CAMKIND_OTHER;
    }
}

static void cq_record(sa1g_world *w, uint32_t fn, int32_t level, uint32_t adjust)
{
    sa1g_camreq *q = &w->camreq;
    sa1g_camreq_drawn d;
    const uint8_t *twp = (const uint8_t *)sa1g_world_ptr(w, w->camera_twp);
    const uint8_t *work = (const uint8_t *)sa1g_world_ptr(w, CQ_ADDR_WORK);
    memcpy(d.pos, twp + 0x20u, sizeof d.pos);
    memcpy(d.ang, twp + 0x14u, sizeof d.ang);
    memcpy(d.target, work + CQ_WORK_TARGET, sizeof d.target);
    d.fn = fn;
    d.level = level;
    d.adjust = adjust;
    q->drawn[0] = q->drawn[1].fn != 0u ? q->drawn[1] : d;
    q->drawn[1] = d;
}

void sa1g_camreq_pre(sa1g_world *w, int32_t sub, void *user)
{
    sa1g_camreq *q = &w->camreq;
    uint32_t fn, adjust, timer;
    int32_t level;
    (void)user;
    if (cq_rd32(w, CAM_CONTROL + CQ_CTRL_DIRTY) == 0u) {       /* 8C0198DA-8C0198EE */
        timer = cq_rd32(w, CAM_CONTROL + CQ_CTRL_TIMER) + 1u;
    } else {
        timer = 0u;
        sa1g_world_wr32(w, CAM_CONTROL + CQ_CTRL_DIRTY, 0u);
    }
    sa1g_world_wr32(w, CAM_CONTROL + CQ_CTRL_TIMER, timer);
    fn = cq_rd32(w, CAM_CONTROL + CQ_CTRL_MODEFUNC);
    level = cq_rd8s(w, CAM_CONTROL + CQ_CTRL_LEVEL);
    if (q->want && !w->replay && (fn != q->last_fn || level != q->last_level)) {
        /* SA1_EVENT_CAMERA: a request changed, only in a tick with the bit, never in replays */
        float p[3] = { 0.0f, 0.0f, 0.0f };
        adjust = (uint32_t)cq_rd8s(w, CAM_CONTROL + CQ_CTRL_ADJUST);
        if (w->camera_twp != 0u) {
            memcpy(p, (const uint8_t *)sa1g_world_ptr(w, w->camera_twp) + 0x20u, sizeof p);
        }
        sa1g_world_event_push(w, SA1_EVENT_CAMERA, fn, 0u, p, ((uint32_t)level & 0xFFu) | ((adjust & 0xFFu) << 8));
        q->last_fn = fn;
        q->last_level = level;
    }
    if (q->want && fn != 0u && cq_kind(fn) != SA1_CAMKIND_OTHER && w->camera_twp != 0u) {
        void *twp = sa1_decode(w->camera_twp);
        ((void (*)(void *))sa1_fn(SA1_GFN_C(CQ_FN_CORE), 0u))(twp);
        ((void (*)(void *, void *))sa1_fn(SA1_GFN_C(CQ_FN_ENDTEST), 0u))(twp, sa1_decode(cq_rd32(w, CQ_ADDR_PREV_PTR)));
        q->running = 1;
        if (sub == 0 || q->drawn[1].fn == 0u) {
            /* the core may have ended SLOW or the request: read after it */
            cq_record(w, fn, level, (uint32_t)cq_rd8s(w, CAM_CONTROL + CQ_CTRL_ADJUST));
        }
    } else {
        q->running = 0;             /* the next step's refresh gives the taskwk back */
        q->drawn[0].fn = 0u;
        q->drawn[1].fn = 0u;
    }
    sa1g_world_wr32(w, CAM_RUN_STEPS, cq_rd32(w, CAM_RUN_STEPS) + 1u);
}

/* S9 through src/sa1_camreq.c's gate: the first tick with the bit, live worlds only */
void sa1g_camreq_arm(struct sa1g_world *w)
{
    if (w != NULL && !w->replay && w->camreq.pre == NULL) {
        w->camreq.pre = sa1g_camreq_pre;
    }
}

/* ---- sa1_character_camera's world side: outside the world, possibly on a worker (R.17): only the world's own accessors ------- */
static float cq_lerpf(float a, float b, float t)
{
    return a + (b - a) * t;
}

static uint32_t cq_lerp_ang(int32_t a, int32_t b, float t)
{
    /* the shorter way, as the render's yaw */
    int32_t d = (int32_t)(int16_t)(uint16_t)((uint32_t)b - (uint32_t)a);
    float f = (float)d * t;
    return (uint32_t)(a + (int32_t)(f >= 0.0f ? f + 0.5f : f - 0.5f)) & 0xFFFFu;
}

void sa1wc_state_get(const struct sa1g_world *w, float alpha, sa1wc_state *o)
{
    const sa1g_camreq *q = &w->camreq;
    uint32_t fn = cq_rd32(w, CAM_CONTROL + CQ_CTRL_MODEFUNC);
    int32_t level = cq_rd8s(w, CAM_CONTROL + CQ_CTRL_LEVEL);
    memset(o, 0, sizeof *o);
    o->level = level;
    o->adjust = (uint32_t)cq_rd8s(w, CAM_CONTROL + CQ_CTRL_ADJUST);
    o->function = fn;
    if (level > 0 && fn != 0u) {
        o->flags |= SA1_CAMSTATE_ACTIVE;
        o->kind = cq_kind(fn);
        if (o->kind != SA1_CAMKIND_OTHER) {
            o->flags |= SA1_CAMSTATE_KNOWN;
        }
    }
    o->fov = (float)((double)(int32_t)cq_rd32(w, CAM_FOV) * (6.283185307179586 / 65536.0));
    if (q->running && q->drawn[1].fn != 0u) {
        const sa1g_camreq_drawn *d0 = &q->drawn[0], *d1 = &q->drawn[1];
        float t = alpha, dx = d1->pos[0] - d0->pos[0], dy = d1->pos[1] - d0->pos[1], dz = d1->pos[2] - d0->pos[2], sx, cx, sy, cy;
        uint32_t k;
        o->flags |= SA1_CAMSTATE_RUNNING;
        if ((d0->fn != d1->fn || d0->level != d1->level) && d1->adjust == 0u && dx * dx + dy * dy + dz * dz > CQ_CUT_UNITS2) {
            o->flags |= SA1_CAMSTATE_CUT;
            t = 1.0f;
        } else if (!(t == t)) {
            t = 1.0f;
        } else if (t < 0.0f) {
            t = (float)(w->acc * 30.0);
        }
        t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
        for (k = 0; k < 3u; k++) {
            o->pos[k] = cq_lerpf(d0->pos[k], d1->pos[k], t);
            o->tgt[k] = cq_lerpf(d0->target[k], d1->target[k], t);
        }
        sh4_fsca(cq_lerp_ang(d0->ang[0], d1->ang[0], t), &sx, &cx);
        sh4_fsca(cq_lerp_ang(d0->ang[1], d1->ang[1], t), &sy, &cy);
        o->fwd[0] = -sy * cx;
        o->fwd[1] = sx;
        o->fwd[2] = -cy * cx;
    }
}
