#include "common.h"
#include "sa1/sa1.h"

/*
 * 1ST_READ 8C0B2EA0-8C0B3240 (chunk 8C0B2EA0): Sonic's speed-limit task and
 * Sonic's snowboard task.
 *
 *  func_8C0B2EA0  create the speed-limit task (exec func_8C0B2DFE, dest
 *                 func_8C0B2E30, both in code_8C0B2D40.c / code_8C0B2E30.c):
 *                 saves pwp->p.max_x_spd in the task's any-work and lowers
 *                 it to 0.5f (called from Sonic's code func_8C0B14E0)
 *  func_8C0B2EE6  set up the snowboard task (called from 8C07FA54):
 *                 twp->timer.ptr = board NJS_OBJECT, twp->value.ptr =
 *                 texlist chosen by the frame counter (loaded with
 *                 func_8C608C82), mwp->weight = 0.02f, exec/disp/dest
 *  func_8C0B2F46  snowboard exec: while Sonic plays a board action
 *                 (102..124) the board follows his collision centre, angle
 *                 and speed; otherwise it runs the object physics
 *                 func_8C05872A on its own. Tail-calls the display, which
 *                 SHC places right after it (fall-through, no BRA).
 *  lbl_8C0B3068   snowboard display: board action motion (D_8C1C6318) or
 *                 the static model
 *  func_8C0B31C6  snowboard destructor: release the texlist
 */

/* task.awp of the speed-limit task (CAlloc(1, 0x10), 0x0C used); same layout
 * as in code_8C0B2D40.c and code_8C0B2E30.c */
typedef struct {
    taskwk *twp;                /* 0x00 player taskwk */
    playerwk *pwp;              /* 0x04 */
    Float max_x_spd;            /* 0x08 saved pwp->p.max_x_spd */
} SPDLIMITWK;

extern void func_8C0B2DFE(task *tp);                    /* speed-limit task exec (code_8C0B2D40.c) */
extern void func_8C0B2E30(task *tp);                    /* speed-limit task dest (code_8C0B2E30.c) */
extern Sint32 func_8C08A71E(void);                      /* frame counter (code_8C08A71E.c) */
extern Sint32 func_8C038D00(Float x, Float y, Float z, Float r); /* sphere visibility test */

/* Sonic's snowboard texlists: frame counter % (D_8C1C63A0 + 1) picks
 * D_8C1C6380[n] for n < D_8C1C63A0 (= 1 in the data), else the S_SBMOT.PRS
 * texlist below */
extern NJS_TEXLIST D_8C1C6380[];
extern Sint32 D_8C1C63A0;
/* snowboard actions for Sonic's board actions 102..124 (NULL: static model) */
extern NJS_ACTION *D_8C1C6318[];

/* S_SBMOT.PRS data at absolute addresses (snowboard object 0x0CB37454 and a
 * texlist at 0x0CB36F94). PC: provided by the overlay loader. */
#if defined(TARGET_PC)
extern NJS_OBJECT S_SBMOT_Sonic_Snowboard;
extern NJS_TEXLIST S_SBMOT_texlist_0CB36F94;
#define SONIC_SNOWBOARD_OBJECT  (&S_SBMOT_Sonic_Snowboard)
#define SONIC_SNOWBOARD_TEXLIST (&S_SBMOT_texlist_0CB36F94)
#else
#define SONIC_SNOWBOARD_OBJECT  ((NJS_OBJECT *)0x0CB37454)
#define SONIC_SNOWBOARD_TEXLIST ((NJS_TEXLIST *)0x0CB36F94)
#endif

void func_8C0B2F46(task *tp);
void lbl_8C0B3068(task *tp);
void func_8C0B31C6(task *tp);

void func_8C0B2EA0(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    task *tp;

    tp = CreateElementalTask(8, 0, func_8C0B2DFE);
    if (tp != NULL) {
        ((SPDLIMITWK *)tp->awp)->twp = twp;
        ((SPDLIMITWK *)tp->awp)->pwp = pwp;
        ((SPDLIMITWK *)tp->awp)->max_x_spd = pwp->p.max_x_spd;
        pwp->p.max_x_spd = 0.5f;
        tp->dest = func_8C0B2E30;
    }
}

void func_8C0B2EE6(task *tp)
{
    taskwk *twp;
    Sint32 n;

    twp = tp->twp;
    twp->timer.ptr = SONIC_SNOWBOARD_OBJECT;
    n = SA1_UMOD((Uint32)func_8C08A71E(), D_8C1C63A0 + 1);
    if (n >= D_8C1C63A0) {
        twp->value.ptr = SONIC_SNOWBOARD_TEXLIST;
    } else {
        twp->value.ptr = &D_8C1C6380[n];
    }
    tp->mwp->weight = 0.02f;
    func_8C608C82(twp->value.ptr);
    tp->dest = func_8C0B31C6;
    tp->exec = func_8C0B2F46;
    tp->disp = lbl_8C0B3068;
}

/*
 * NON_MATCHING (only with tools/shcdiff.py patched to treat lbl_8C0B3068 as
 * part of this function, see below): 261 of 261 instructions, 4 differ. The
 * original spills pmwp to @R15 and mwp to @(4,R15); SHC gives the opposite
 * slots. Tried without effect: declaration order and scope, assignment
 * order, initialisers, variable names, the struct-copy spelling.
 *
 * The original has no RTS here: the tail call to lbl_8C0B3068 falls through
 * into it (SHC places a tail-called same-file function right after the
 * caller), so 8C0B2F46-8C0B31C6 holds both functions. shcdiff/splice stop the
 * compiled side at the lbl_8C0B3068 label, so this function cannot be
 * reported as MATCH (or spliced) even when the code is identical; that is
 * why lbl_8C0B3068 (identical to the original when compared as one range)
 * stays outside the guard.
 */
#ifdef NON_MATCHING
void func_8C0B2F46(task *tp)
{
    taskwk *twp;
    taskwk *ptwp;
    Uint8 pno;
    motionwk *mwp;
    motionwk *pmwp;
    mtnjvwk *mjp;
    Sint16 action;

    twp = tp->twp;
    pno = twp->counter.b[0];
    ptwp = playertwp[pno];
    pmwp = playermwp[pno];
    mwp = tp->mwp;
    mjp = &GetPlayerWorkPtr(pno)->mj;
    action = mjp->action;
    if (action < 102 || action > 124) {
        if (!(twp->flag & 8)) {
            twp->pos.y += -5.22f;
            func_8C05872A(tp);
            twp->pos.y -= -5.22f;
        }
    } else {
        twp->pos = CWP(ptwp)->info->center;
        twp->ang.x = ptwp->ang.x;
        twp->ang.y = 0x8000 - ptwp->ang.y;
        twp->ang.z = ptwp->ang.z;
        mwp->spd = pmwp->spd;
        twp->flag &= ~8;
    }
    lbl_8C0B3068(tp);
}
#else
ASM_STUB(void func_8C0B2F46(task *tp))
#endif

void lbl_8C0B3068(task *tp)
{
    taskwk *twp;
    mtnjvwk *mjp;
    Sint16 action;
    NJS_ACTION *actp;
    NJS_MOTION *motion;

    twp = tp->twp;
    mjp = &GetPlayerWorkPtr(twp->counter.b[0])->mj;
    action = mjp->action;
    if (func_8C038D00(twp->pos.x, twp->pos.y, twp->pos.z, 12.0f)) {
        njSetTexture(twp->value.ptr);
        njPushMatrix(NULL);
        if (action < 102 || action > 124) {
            njTranslateV(NULL, &twp->pos);
            twp->ang.z ? (njRotateZ(NULL, twp->ang.z), 0) : 0;
            twp->ang.x ? (njRotateX(NULL, twp->ang.x), 0) : 0;
            twp->ang.y ? (njRotateY(NULL, twp->ang.y), 0) : 0;
            njTranslate(NULL, 0.0f, -5.22f, 0.0f);
            njDrawModel(((NJS_OBJECT *)twp->timer.ptr)->model);
        } else {
            action -= 102;
            actp = D_8C1C6318[action];
            njTranslateV(NULL, &twp->pos);
            twp->ang.z ? (njRotateZ(NULL, twp->ang.z), 0) : 0;
            twp->ang.x ? (njRotateX(NULL, twp->ang.x), 0) : 0;
            twp->ang.y ? (njRotateY(NULL, twp->ang.y), 0) : 0;
            if (actp != NULL) {
                motion = actp->motion;
                if (twp->flag & 2) {
                    njTranslate(NULL, 0.0f, -5.22f, 0.0f);
                }
                func_8C60E080(twp->timer.ptr, motion, mjp->nframe);
            } else {
                if (twp->flag & 2) {
                    njTranslate(NULL, 0.0f, -5.22f, 0.0f);
                }
                njDrawObject(twp->timer.ptr);
            }
        }
        njPopMatrix(1);
    }
}

void func_8C0B31C6(task *tp)
{
    taskwk *twp;

    twp = tp->twp;
    func_8C6088BC(twp->value.ptr);
}
