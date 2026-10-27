#include "sa1/sa1.h"

/*
 * E-102 Gamma effects (1ST_READ chunk 8C0D2C00-8C0D2EC0):
 *
 *  func_8C0D2C00  foot smoke: while on the ground (twp->flag & 3), takes
 *                 both feet (pwp->leftfoot_pos / rightfoot_pos) through the
 *                 body rotation (Z, X, 0x8000 - Y) and a (-4, -3, 0)
 *                 offset, adds the collision centre and starts one
 *                 func_8C0D28D8 task per foot with the position, the
 *                 player's speed and angles (called by E102Main).
 *  func_8C0D2D16  Jet Booster exhaust: while the jet height y is 0..30
 *                 below the body, spawns one particle (func_8C0466E8) at a
 *                 random point of a 7-unit circle 10 units behind E-102,
 *                 sized and spread by the height (called by the hover
 *                 functions 8C0D1C78/8C0D1D76).
 *
 * Matching notes:
 *  - the rotations are written "a ? njRotateZ(NULL, a) : (void)0" (no
 *    local): SHC keeps the angle in a stack temporary like the original.
 *  - func_8C0D2C00: the foot loop sits in a one-pass DT loop (MOV #1/DT);
 *    "i = 1; do ... while (--i)" keeps it (an "i < 1" for loop is deleted
 *    by SHC). Still NON_MATCHING: the DT counter and the &v[0] temporary
 *    trade R9/R10 (6 instructions); "for (i = 1; i != 0; i--)" swaps the
 *    counter with mwp (R10/R11) instead. Everything else is identical.
 *    Tried without success (~150 variants): every outer-loop spelling
 *    falls in one of these two modes (decrement fused into the test, e.g.
 *    "while (--i)", "(i -= 1) != 0": counter below the &v[0] copy; separate
 *    decrement and test, e.g. for/"i--; } while (i)": counter above mwp);
 *    counter types, declaration order/scope, init placement, register,
 *    v vs &v[0] spellings, pointer inner loops, mwp/spd locals and no-code
 *    references do not move it (bounded loops "i < 1", "i != 1", "i < n"
 *    are deleted or restructured).
 *    Second session (~70 more variants): the allocation flips to the
 *    original (counter R10, copy R9) when the counter's definition shares
 *    the statement of the last njAddVector ("njAddVector(&v[1], ...), i = 1;")
 *    or comes earlier, but then njPopMatrix(1) reuses R10 for its argument
 *    (MOV R10,R4); "--i;" at the top of the outer body also allocates
 *    correctly but tests with TST instead of DT. Separate "i = 1;"
 *    statements anywhere after that call, dead stores, no-code references
 *    (x = x, (void)x), counter types, pointer variables for v, inner loop
 *    spellings and trailing statements do not change it.
 *  - func_8C0D2D16: the position FMACs compute the addend (pos + d) before
 *    the njCos/njSin call (pushed on the stack), i.e. "c + a * b" order,
 *    which SA1_FMAC (a * b + c) cannot express: FMAC_ADD below. The random
 *    scale must be exactly 2^-15 (0x38000000); the truncated 3.0517578e-05f
 *    (0x37FFFFFF) changes the register allocation.
 */

/*
 * Particle parameters read by func_8C0466E8 (8C0465E0 chunk): it creates a
 * sprite task, copies pos/spd/argb into it and keeps the pointer (task
 * +0x44), whose exec func_8C0464EE reads 0x08..0x10 every frame. Also used
 * by the b_e101/adv01xx/ev004c overlays. D_8C422CC0 is initialised data
 * (0x3C bytes, floats only): 1.0, 0.03, 0.19, 0.05, 0.025, pos 0, spd 0,
 * argb 0.3/0.8/0.8/1.0. Local view until a shared type exists.
 */
typedef struct {
    Float scl;                  /* 0x00 sprite scale (* constant into task +0x14) */
    Float unk04;                /* 0x04 */
    Float anim_spd;             /* 0x08 frame step (func_8C0464EE) */
    Float drag;                 /* 0x0C spd -= spd * drag (func_8C0464EE) */
    Float gravity;              /* 0x10 added to spd.y (func_8C0464EE) */
    NJS_POINT3 pos;             /* 0x14 */
    NJS_POINT3 spd;             /* 0x20 */
    Float a;                    /* 0x2C */
    Float r;                    /* 0x30 */
    Float g;                    /* 0x34 */
    Float b;                    /* 0x38 */
} PTCL_PARAM;                   /* 0x3C */

SA1_ASSERT_SIZE(PTCL_PARAM, 0x3C);
SA1_ASSERT_OFFSET(PTCL_PARAM, pos, 0x14);
SA1_ASSERT_OFFSET(PTCL_PARAM, spd, 0x20);
SA1_ASSERT_OFFSET(PTCL_PARAM, a, 0x2C);

extern PTCL_PARAM D_8C422CC0;   /* jet exhaust particle */

void func_8C0D28D8(task *tp);               /* foot smoke task exec (8C0D26E0 chunk) */
void func_8C0466E8(PTCL_PARAM *prm);        /* particle spawn (8C0465E0 chunk) */

#ifdef NON_MATCHING
void func_8C0D2C00(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_POINT3 v[2];
    Sint32 i;
    Sint32 j;
    task *tp;
    CCL_INFO *info;

    if (twp->flag & 3) {
        njPushMatrix(D_8C67C640);
        info = CWP(twp)->info;
        twp->ang.z ? njRotateZ(NULL, twp->ang.z) : (void)0;
        twp->ang.x ? njRotateX(NULL, twp->ang.x) : (void)0;
        (0x8000 - twp->ang.y) ? njRotateY(NULL, 0x8000 - twp->ang.y) : (void)0;
        njTranslate(NULL, -4.0f, -3.0f, 0.0f);
        njCalcPoint(NULL, &pwp->leftfoot_pos, &v[0]);
        njAddVector(&v[0], &info->center);
        njCalcPoint(NULL, &pwp->rightfoot_pos, &v[1]);
        njAddVector(&v[1], &info->center);
        njPopMatrix(1);
        i = 1;
        do {
            for (j = 0; j < 2; j++) {
                tp = CreateElementalTask(3, 5, func_8C0D28D8);
                if (tp != NULL) {
                    tp->twp->pos = v[j];
                    tp->mwp->spd = mwp->spd;
                    tp->twp->ang = twp->ang;
                }
            }
        } while (--i);
    }
}
#else
ASM_STUB(void func_8C0D2C00(taskwk *twp, motionwk *mwp, playerwk *pwp))
#endif

/* c + a * b with the addend c evaluated first (the original computes it
 * before the call in the product); fused like SA1_FMAC on PC */
#if defined(TARGET_PC)
#define FMAC_ADD(c, a, b) SA1_FMAC((a), (b), (c))
#else
#define FMAC_ADD(c, a, b) ((c) + (a) * (b))
#endif

void func_8C0D2D16(taskwk *twp, Float y)
{
    Float h;
    Float dx;
    Float dz;
    Angle ang;
    Angle a;

    h = twp->pos.y - y;
    ang = twp->ang.y;
    dx = -(njCos(ang) * 10.0f);
    dz = -(njSin(ang) * 10.0f);
    if (h > 0.0f && h < 30.0f) {
        a = SA1_FTOI((Float)rand() * 1.99999990f);           /* 0x3FFFFFFF: 0..0xFFFF */
        h = (21.0f - h) * 0.25f;
        D_8C422CC0.scl = SA1_FMAC(0.099999996f, h, 0.5f);
        D_8C422CC0.pos.x = FMAC_ADD(twp->pos.x + dx, njCos(a), 7.0f);
        D_8C422CC0.pos.y = y + 1.5f;
        D_8C422CC0.pos.z = FMAC_ADD(twp->pos.z + dz, njSin(a), 7.0f);
        D_8C422CC0.spd.x = h * SA1_FMAC(3.0517578125e-05f, (Float)rand(), -0.5f);  /* rand / 32768 - 0.5 */
        D_8C422CC0.spd.y = 0.0f;
        D_8C422CC0.spd.z = h * SA1_FMAC(3.0517578125e-05f, (Float)rand(), -0.5f);
        func_8C0466E8(&D_8C422CC0);
    }
}
