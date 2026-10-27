/*
 * 1ST_READ 8C07B500 (chunk 8C07B500-8C07BAC0): player surface effects and
 * the timed sound tasks.
 *
 *  func_8C07B500  dust puffs behind a running player (two particle chances,
 *                 one above 12.0 and one above 2.0 of |spd|^2), skipped on
 *                 the surfaces 0x40800E
 *  func_8C07B694  board sliding (Sonic, Tails): sound + snow overlay effect
 *                 on snow (attr 4/8), sound on sand (attr 0x10), and the
 *                 spray/sand particles behind the board
 *  func_8C07B93A  exec of the sound task created by func_8C07BA0A: keeps a
 *                 positional sound on the player while twp->mode is the
 *                 mode it was started in, then frees itself
 *  func_8C07BA0A  create that task (any work: sound id, player, mode)
 *  func_8C07BA40  empty task function
 *  func_8C07BA44  exec of a timer task (created in chunk 8C07BAC0): after
 *                 121 frames sets bit 4 of the parent's twp->flag, calls
 *                 func_8C04E386(2) and frees itself
 */
#include "common.h"
#include "sa1/sa1.h"

/* Particle node of the effect lists (func_8C09DDFC takes one from the free
 * list B_8C7A0D18, links it into the list and sets exec; func_8C046480
 * animates it). Partial layout. */
typedef struct sa1_effect {
    struct sa1_effect *next;    /* DC 0x00 */
    void *exec;                 /* DC 0x04 exec(node, list) */
    Sint16 w08;                 /* DC 0x08 */
    Sint16 cell;                /* DC 0x0A animation cell (func_8C046480) */
    Angle ang;                  /* DC 0x0C */
    Float frame;                /* DC 0x10 */
    Float scl;                  /* DC 0x14 */
    NJS_POINT3 pos;             /* DC 0x18 */
} SA1_EFFECT;

/* Effect spawn parameters (D_8C1BE858 / D_8C1BE894, 15 floats; read by
 * func_8C09E474 and the SBOARD overlay function 0C900080) */
typedef struct {
    Float scl;                  /* 0x00 */
    Float f04;                  /* 0x04 */
    Float f08;                  /* 0x08 */
    Float f0C;                  /* 0x0C */
    Float f10;                  /* 0x10 */
    NJS_POINT3 pos;             /* 0x14 */
    NJS_POINT3 spd;             /* 0x20 */
    NJS_ARGB argb;              /* 0x2C */
} SA1_EFFECT_PARAM;             /* 0x3C */

/* any work (task.awp) of the sound task func_8C07B93A */
typedef struct {
    Sint32 se;                  /* 0x00 */
    Uint8 pno;                  /* 0x04 */
    Uint8 pad05;
    Sint16 mode;                /* 0x06 twp->mode of the player at creation */
} SA1_SE_AWP;

#define SE_AWP(tp) ((SA1_SE_AWP *)(tp)->awp)

/* any work (task.awp) of the timer task func_8C07BA44 */
typedef struct {
    Uint32 timer;               /* 0x00 */
} SA1_TIMER_AWP;

typedef void (*SA1_EFFECT_FUNC)(SA1_EFFECT_PARAM *param);

extern void *D_8C19BF48;                        /* effect list (dust) */
extern SA1_EFFECT_PARAM D_8C1BE858;             /* spray (attr 8) */
extern SA1_EFFECT_PARAM D_8C1BE894;             /* sand (attr 0x10) */
extern Sint16 D_8C19B468;

SA1_EFFECT *func_8C09DDFC(void *list, void (*exec)(SA1_EFFECT *ep, void *list));
void func_8C046480(SA1_EFFECT *ep, void *list);
void func_8C09E474(SA1_EFFECT_PARAM *param);
void func_8C094330(Sint32 se, taskwk *twp, Sint32 pri, Sint32 volofs, Sint32 timer);
void func_8C0943E0(Sint32 se, task *tp, Sint32 pri, Sint32 volofs, Sint32 timer,
                   Float x, Float y, Float z);
void func_8C04E386(Sint16 n);

/* Overlay functions reached by absolute address (overlays load at
 * 0x0C900000): 0C900AE0 = STG08 (Ice Cap) func_0C900AE0(pno),
 * 0C900080 = SBOARD func_0C900080(param). PC: provided by the overlay loader. */
#if defined(TARGET_PC)
extern void func_0C900AE0(Uint8 pno);
extern void func_0C900080(SA1_EFFECT_PARAM *param);
#define STG08_FUNC_0C900AE0 func_0C900AE0
#define SBOARD_FUNC_0C900080 func_0C900080
#else
#define STG08_FUNC_0C900AE0 ((void (*)(Uint8))0x0C900AE0)
#define SBOARD_FUNC_0C900080 ((SA1_EFFECT_FUNC)0x0C900080)
#endif

void func_8C07B93A(task *tp);

void func_8C07B500(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    SA1_EFFECT *ep;
    Float spd2;
    Float angscl;

    /* The original keeps ep in its stack slot (stored after the call and
     * reloaded before every store through it): ep is address-taken. */
    (void)&ep;
    if (pwp->attr & 0x40800E) {
        return;
    }
    spd2 = njScalor2(&mwp->spd);
    angscl = 1.9999999f;
    if (spd2 > 12.0f && (Float)rand() * 3.0517578125e-05f > 0.29999999f) {
        ep = func_8C09DDFC(D_8C19BF48, func_8C046480);
        if (ep != NULL) {
            ep->pos.x = twp->pos.x - mwp->spd.x * 0.5f;
            ep->pos.y = twp->pos.y + spd2 * 0.04f - mwp->spd.y * 0.5f;
            ep->pos.z = twp->pos.z - mwp->spd.z * 0.5f;
            ep->scl = SA1_FMAC((Float)rand(), 6.1035156e-07f, spd2 * 0.010f) * 0.149999992f;
            ep->frame = (Float)rand() * 1.52587890625e-04f;
            ep->ang = SA1_FTOI((Float)rand() * angscl);
        }
    }
    if (spd2 > 2.0f && (Float)rand() * 3.0517578125e-05f > 0.29999999f) {
        ep = func_8C09DDFC(D_8C19BF48, func_8C046480);
        if (ep == NULL) {
            return;
        }
        ep->pos.x = twp->pos.x;
        ep->pos.y = twp->pos.y + spd2 * 0.04f;
        ep->pos.z = twp->pos.z;
        ep->scl = SA1_FMAC((Float)rand(), 3.0517578e-07f, spd2 * 0.010f) * 0.149999992f;
        ep->frame = (Float)rand() * 2.13623046875e-04f;
        ep->ang = SA1_FTOI((Float)rand() * angscl);
    }
}

/*
 * NON_MATCHING: register allocation only (10 of 286 instructions). SHC gives
 * the hoisted constant 0.2f FR15 and spd2 FR14; the original has spd2 in
 * FR15, 0.2f in FR14 (0.0f FR13 and the CSE'd spd2 * 0.002f FR12 agree).
 * Everything else (integer registers, scheduling, literal pool) is identical.
 * The priority is decided by reference counts after CSE/copy propagation:
 * one more live use of spd2 anywhere, or one fewer use of 0.2f in either
 * block (e.g. the last FMAC with another constant), gives exactly the
 * original FP allocation; one extra use each also favours spd2, so a spd2
 * use outweighs a constant use. No code-neutral spelling found that does
 * this. Tried without effect: dead init / dead copies / (void) uses of spd2,
 * copies of spd2 into other locals, all 24 declaration orders, unused or
 * block-scoped locals, early return vs if (param != NULL) vs goto, nested
 * vs && conditions where the code stays identical, <= vs !(>), reversed
 * comparison operands, double literals (same as float with -fpu=single),
 * different literal spellings of 0.2f, operand/addition orders of the
 * FMACs, scl as a local or as njRandom()*0.01f (SHC folds rand*2^-15*0.01
 * into one constant; it also folds spd2*0.2f*0.01f, but to 0x3B03126D), a
 * preceding function using the same literals. Changing control-flow syntax
 * that alters code (nested block 2 condition, assignment inside the first
 * condition, a local multiplier variable) moves other things.
 */
#ifdef NON_MATCHING
void func_8C07B694(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_POINT3 v;
    SA1_EFFECT_FUNC func;
    SA1_EFFECT_PARAM *param;
    Float spd2;

    spd2 = 0.0f;
    param = NULL;
    func = NULL;
    spd2 = njScalor2(&mwp->spd);
    if (spd2 > 3.0f && pwp->mj.mtnmode == 12) {
        if (pwp->attr & 0xC) {
            if ((Float)rand() * 3.0517578125e-05f > 0.95f) {
                STG08_FUNC_0C900AE0(twp->counter.b[0]);
            }
            func_8C094330(237, twp, 1, 0, 2);
        } else if (pwp->attr & 0x10) {
            func_8C094330(691, twp, 1, 0, 2);
        }
    }
    if (pwp->attr & 8) {
        param = &D_8C1BE858;
        func = func_8C09E474;
    } else if (pwp->attr & 0x10) {
        param = &D_8C1BE894;
        func = SBOARD_FUNC_0C900080;
    }
    if (param == NULL) {
        return;
    }
    if (spd2 > 3.0f && !((Float)rand() * 3.0517578125e-05f > 0.099999995f)) {
        v.x = -(pwp->p.height * 0.39999998f);
        v.y = 0.0f;
        v.z = 0.0f;
        PConvertVector_P2G(twp, &v);
        param->pos.x = twp->pos.x + v.x;
        param->pos.y = twp->pos.y + v.y + 1.0f;
        param->pos.z = twp->pos.z + v.z;
        param->scl = SA1_FMAC((Float)rand(), 3.0517578e-07f, spd2 * 0.0019999999f);
        param->spd.x = mwp->spd.x * 0.19999999f;
        param->spd.y = mwp->spd.y * 0.19999999f;
        param->spd.z = mwp->spd.z * 0.19999999f;
        func(param);
    }
    if (spd2 >= 4.0f || pwp->mj.mtnmode == 12) {
        if (!((Float)rand() * 3.0517578125e-05f > 0.25f)) {
            v.x = 0.0f;
            v.y = 0.0f;
            v.z = -mwp->rad;
            PConvertVector_P2G(twp, &v);
            param->pos.x = twp->pos.x + v.x + mwp->spd.x;
            param->pos.y = twp->pos.y + v.y + mwp->spd.y + 1.0f;
            param->pos.z = twp->pos.z + v.z + mwp->spd.z;
            param->scl = SA1_FMAC((Float)rand(), 3.0517578e-07f, spd2 * 0.0019999999f);
            param->spd.x = SA1_FMAC(mwp->spd.x, 0.19999999f, v.x);
            param->spd.y = SA1_FMAC(mwp->spd.y, 0.19999999f, v.y);
            param->spd.z = SA1_FMAC(mwp->spd.z, 0.19999999f, v.z);
            func(param);
        }
    }
}
#else
ASM_STUB(void func_8C07B694(taskwk *twp, motionwk *mwp, playerwk *pwp))
#endif

void func_8C07B93A(task *tp)
{
    SA1_SE_AWP *awp;
    taskwk *twp;
    Sint32 pno;

    /* reading pno before taking awp gives tp R13 and awp R12 */
    pno = SE_AWP(tp)->pno;
    awp = SE_AWP(tp);
    twp = playertwp[pno];
    if (twp != NULL && twp->mode == awp->mode) {
        func_8C0943E0(awp->se, tp, 1, 0, 2, twp->pos.x, twp->pos.y, twp->pos.z);
    } else {
        FreeTask(tp);
    }
}

void func_8C07BA0A(Sint32 se, Sint8 pno, Sint16 mode)
{
    task *tp;

    tp = CreateElementalTask(8, 6, func_8C07B93A);
    if (tp != NULL) {
        SE_AWP(tp)->se = se;
        SE_AWP(tp)->pno = pno;
        SE_AWP(tp)->mode = mode;
    }
}

void func_8C07BA40(task *tp)
{
}

void func_8C07BA44(task *tp)
{
    if (((SA1_TIMER_AWP *)tp->awp)->timer++ > 120) {
        func_8C04E386(2);
        if (tp->ptp != NULL) {
            tp->ptp->twp->flag |= 4;
        }
        FreeTask(tp);
    } else if (D_8C19B468 == 1) {
        FreeTask(tp);
    }
}
