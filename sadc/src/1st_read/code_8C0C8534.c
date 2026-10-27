#include "common.h"
#include "sa1/sa1.h"
#include "sa1/pending.h"

/*
 * 1ST_READ 8C0C8534-8C0C8A80: Knuckles helpers (chunk 8C0C80D4-8C0C8A80,
 * functions 5-9 of 9).
 *
 *  func_8C0C8534  (KnucklesModeMachine, (twp, mwp, pwp)): result of a dig.
 *                 Unless func_8C0C9B88 takes over: with B_8C7A25F0 == 1
 *                 only a sound (SE 0x480 / 0x482 for B_8C7A25F4 1 / 2);
 *                 otherwise pick an outcome i in 0..6 with rand() / 32768
 *                 against the cumulative weights B_8C7A25F8[6], [5], ...:
 *                 0 nothing, 1/2/3 scatter 1/5/10 rings 8 below twp->pos,
 *                 4 func_8C0CB30E, 5 an item box (random item of
 *                 D_8C15C0DC), 6 func_8C015798 (only on the levels listed
 *                 in D_8C15C0F4, else nothing). A timer task func_8C0C84FC
 *                 then plays SE 0x480 (nothing) / 0x481 (rings) / 0x482
 *                 (object) after 60 frames.
 *  func_8C0C876C  (KnucklesMain, (twp)): dig weights B_8C7A25F8[7]: within
 *                 15 of the last dig point B_8C7A2614 D_8C15BF98 scaled by
 *                 0 (< 8) .. 7/8; else by the distance to the centre of the
 *                 100 x 100 grid cell (x and z; < 4: D_8C15BFD0, < 20:
 *                 D_8C15BFB4); else D_8C15BF98.
 *  func_8C0C88E6  (KnucklesMain, KnucklesModeMachine, (twp)): 0 when
 *                 twp->pos is in one of the stage death zones enabled for
 *                 this character (vertical probe func_8C02CA00), else 1.
 *  func_8C0C896C  create the look-at task func_8C0C8A80 for player pno
 *                 with the head and eye objects objs[0..2] (Knuckles copy
 *                 of Sonic func_8C0B27EE / Amy func_8C0DD8DC).
 *  func_8C0C89CA  dest of the look task: put the saved head and eye angles
 *                 back (same as Sonic func_8C0B2890 / Amy func_8C0DD944).
 */

/* D_8C15C0DC = { 0, 1, 5, 6, 7, 8 }: item box contents (func_8C05D9C0). It
 * is the initialiser constant of a block-scope local array of the original
 * (game data, extern only; copied to the stack like the original). */
typedef struct {
    Sint32 item[6];
} ITEM_LIST6;

/* D_8C15C0F4 = { 2, 5, 7, 8, 10, 0x7F }: levels, 0x7F-terminated; also the
 * initialiser constant of a block-scope local array. */
typedef union {
    Uint16 lvl[6];
    Uint32 align[3];
} LEVEL_LIST6;

extern Sint32 B_8C7A25F0;           /* dig mode flag (1: only a sound) */
extern Sint32 B_8C7A25F4;           /* sound selector in that mode (1: SE 0x480, 2: SE 0x482) */
extern Float B_8C7A25F8[7];         /* dig outcome weights (func_8C0C876C) */
extern NJS_POINT3 B_8C7A2614;       /* position where Knuckles came out of the ground (code_8C0C193A.c) */
extern Float D_8C15BF98[7];         /* weights: default */
extern Float D_8C15BFB4[7];         /* weights: grid cell, centre distance 4..20 */
extern Float D_8C15BFD0[7];         /* weights: grid cell, centre distance < 4 */
extern ITEM_LIST6 D_8C15C0DC;
extern LEVEL_LIST6 D_8C15C0F4;

Sint32 func_8C0C9B88(NJS_POINT3 *pos);
void func_8C0CB30E(NJS_POINT3 *pos);
void func_8C05D9C0(NJS_POINT3 *pos, Sint32 item);                 /* air item box (objexec_O_ItemBoxAir) */
void func_8C015798(Float x, Float y, Float z);                    /* code_8C015720.c */
void func_8C0C84FC(task *tp);                                     /* SE timer task (code_8C0C80D4.c) */
void func_8C0C8A80(task *tp);                                     /* look task exec (code_8C0C8A80.c) */
void func_8C0C89CA(task *tp);

/*
 * Ring scatter 8C087A9E, defined in code_8C0876F4.c as
 * (Sint32 n, Float x, Float y, Float z). SHC evaluates call arguments right
 * to left; the three ring calls of func_8C0C8534 only load the count first
 * (so that SHC can tail-merge them as in the original) when the count is
 * the LAST parameter, so the original prototype was most likely
 * (Float x, Float y, Float z, Sint32 n). Both give the same SH-4 registers
 * (R4, FR4-FR6); the PC build keeps the order of the definition.
 */
#if defined(TARGET_PC)
void func_8C087A9E(Sint32 n, Float x, Float y, Float z);
#define SCATTER_RINGS(x, y, z, n) func_8C087A9E((n), (x), (y), (z))
#else
void func_8C087A9E(Float x, Float y, Float z, Sint32 n);
#define SCATTER_RINGS(x, y, z, n) func_8C087A9E((x), (y), (z), (n))
#endif

/*
 * NON_MATCHING: 10 of 221 instructions differ (same control flow, stack
 * frame and literal pool). The original keeps the random value r in FR15
 * (shared with the hoisted -8.0f of the ring cases) and loads rand's
 * address into R3; here r is in FR5 and rand's address in R2, and the
 * compound "r *= k" lets SHC reuse FR3 for the 2^-15 constant, so that
 * load is scheduled after the FR3 -> r copy (FR2, before FLOAT, there).
 * Findings (about 2000 compiled variants):
 *  - the callee-saved integers of the original (R9 GetStageNumber, R10
 *    0x480, R11 &func_8C087A9E then 0xFF00, R12 0x482, R13 0x481 then p)
 *    only come out with BOTH "r = (Float)rand(); r *= k;" and
 *    "register task *tp" (16 and 17 differences with one of them alone);
 *  - "r = (Float)rand() * k" gives the original FP schedule (constant in
 *    FR2) but permutes R10/R11/R13 (17 differences);
 *  - the for loop "i > 0 && r > sum" beats the while/break form (21);
 *  - no effect from declaration order, block scope, register on other
 *    subsets of the locals, if-else for the sound path or the B_8C7A25F4
 *    switch, initialised local arrays instead of the struct copies,
 *    double literals, dead uses of r, index instead of pointer loops.
 * SHC puts a float in FR12-FR15 when its live range crosses a call in its
 * model (the hoisted -8.0f does, through the unmerged ring calls); no
 * tried source gives r such a range, which is probably the missing piece.
 * Idioms needed for the rest: the ring calls with the count last (see
 * SCATTER_RINGS), CMP/HI with 0 for the unsigned "i > 0", the struct copy
 * for the 24-byte list and "lp = &lvls; lvls = ...; p = lp->lvl" for the
 * 12-byte one (own destination register like the original initialised
 * local array).
 */
#ifdef NON_MATCHING
void func_8C0C8534(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    Uint16 *p;
    Float r;
    Float sum;
    Float *wp;
    Uint32 i;
    Sint32 se;
    register task *tp;

    if (func_8C0C9B88(&twp->pos)) {
        return;
    }
    if (B_8C7A25F0 == 1) {
        switch (B_8C7A25F4) {
        case 1:
            func_8C0940DA(0x480, 0, 0, 0);
            break;
        case 2:
            func_8C0940DA(0x482, 0, 0, 0);
            break;
        }
        return;
    }
    r = (Float)rand();
    r *= 3.0517578125e-05f;
    wp = &B_8C7A25F8[6];
    sum = *wp;
    for (i = 6; i > 0 && r > sum; i--) {
        sum += *--wp;
    }
    PC_ONLY(se = 0x480;)            /* i is always 0..6: the original leaves se unset only on the unreachable default path */
    switch (i) {
    case 0:
        se = 0x480;
        break;
    case 1:
        SCATTER_RINGS(twp->pos.x, twp->pos.y - 8.0f, twp->pos.z, 1);
        se = 0x481;
        break;
    case 2:
        SCATTER_RINGS(twp->pos.x, twp->pos.y - 8.0f, twp->pos.z, 5);
        se = 0x481;
        break;
    case 3:
        SCATTER_RINGS(twp->pos.x, twp->pos.y - 8.0f, twp->pos.z, 10);
        se = 0x481;
        break;
    case 4:
        func_8C0CB30E(&twp->pos);
        se = 0x482;
        break;
    case 5:
        {
            ITEM_LIST6 items;

            items = D_8C15C0DC;
            func_8C05D9C0(&twp->pos, items.item[SA1_FTOI((Float)rand() * 0.000181274405f)]);
        }
        se = 0x482;
        break;
    case 6:
        {
            LEVEL_LIST6 lvls;
            LEVEL_LIST6 *lp;

            lp = &lvls;
            lvls = D_8C15C0F4;
            p = lp->lvl;
            while (*p != 0x7F) {
                if ((GetStageNumber() & 0xFF00) == (*p << 8)) {
                    break;
                }
                p++;
            }
            if (*p != 0x7F) {
                func_8C015798(twp->pos.x, twp->pos.y, twp->pos.z);
                se = 0x482;
            } else {
                se = 0x480;
            }
        }
        break;
    }
    if ((tp = CreateElementalTask(2, 6, func_8C0C84FC)) != NULL) {
        tp->twp->wtimer = 60;
        tp->twp->ang.x = se;
    }
}
#else
ASM_STUB(void func_8C0C8534(taskwk *twp, motionwk *mwp, playerwk *pwp))
#endif

/*
 * Matching notes: v.x = v.x - f(v.x, ...) keeps v.x in a register across
 * the call; the loops need a signed counter against the unsigned sizeof
 * bound (an unsigned counter is turned into a down-counter) and the
 * pointer increments in the for header (with *dst++ = *src++ the hoisted
 * B_8C7A25F8 / 0 registers swap).
 */
#ifdef NON_MATCHING
void func_8C0C876C(taskwk *twp)
{
    NJS_POINT3 v;
    Float d;
    Float f;
    Float *src;
    Float *dst;
    Uint32 i;

    v.x = twp->pos.x + 50.0f;
    v.x = v.x - func_8C056784(v.x, 100.0f);
    v.y = twp->pos.y;
    v.z = twp->pos.z + 50.0f;
    v.z = v.z - func_8C056784(v.z, 100.0f);
    d = njDistanceP2P(&twp->pos, &B_8C7A2614);
    if (d < 15.0f) {
        if (d < 8.0f) {
            f = 0.0f;
        } else {
            f = (d + -8.0f) * 0.125f;
        }
        src = D_8C15BF98;
        dst = B_8C7A25F8;
        for (i = 0; i < 7; i++) {
            *dst++ = *src++ * f;
        }
    } else {
        d = njDistanceP2P(&twp->pos, &v);
        if (d < 20.0f) {
            if (d < 4.0f) {
                src = D_8C15BFD0;
            } else {
                src = D_8C15BFB4;
            }
            dst = B_8C7A25F8;
            for (i = 0; i < 7; i++) {
                *dst++ = *src++;
            }
        } else {
            src = D_8C15BF98;
            dst = B_8C7A25F8;
            for (i = 0; i < 7; i++) {
                *dst++ = *src++;
            }
        }
    }
}
#else
ASM_STUB(void func_8C0C876C(taskwk *twp))
#endif

Sint32 func_8C0C88E6(taskwk *twp)
{
    DEATHZONE **list;
    DEATHZONE *zone;
    Uint32 pmask;
    Uint16 stage;
    Uint16 level;
    Uint16 act;
    zxsdwstr zss;

    stage = GetStageNumber();
    act = (Uint8)stage;
    level = (stage & 0xFF00) >> 8;
    list = DeathZoneTable[level];
    if (list == NULL) {
        return 0;
    }
    zone = list[act];
    if (zone == NULL || zone->object == NULL) {
        return 0;
    }
    pmask = 1 << GetPlayerNumber();
    zss.pos = twp->pos;
    for (; zone->object != NULL; zone++) {
        if ((zone->flags & pmask) && func_8C02CA00(&zss, zone->object)) {
            return 0;
        }
    }
    return 1;
}

/* Same source as Amy func_8C0DD8DC (code_8C0DD490.c): the success path has
 * no return value in the original (R0 still holds the store offset 0x60);
 * KnucklesMain ignores the result. The PC build returns TRUE. */
Bool func_8C0C896C(Sint8 pno, NJS_OBJECT **objs)
{
    task *tp;
    lookwk *lwp;

    if ((tp = CreateElementalTask(2, 2, func_8C0C8A80)) != NULL) {
        if ((lwp = MAlloc(sizeof(lookwk))) == NULL) {
            FreeTask(tp);
            return FALSE;
        }
        tp->mwp = (motionwk *)lwp;
        tp->twp->counter.b[0] = pno;
        lwp->head = objs[0];
        lwp->eyeL = objs[1];
        lwp->eyeR = objs[2];
        goto ok;
    }
    return FALSE;
ok:
    PC_ONLY(return TRUE;)
    ;
}

void func_8C0C89CA(task *tp)
{
    lookwk *lwp;

    lwp = (lookwk *)tp->mwp;
    lwp->head->ang[0] = lwp->head_ang[0];
    lwp->head->ang[1] = lwp->head_ang[1];
    lwp->head->ang[2] = lwp->head_ang[2];
    lwp->eyeR->ang[0] = lwp->eyeR_ang[0];
    lwp->eyeR->ang[1] = lwp->eyeR_ang[1];
    lwp->eyeR->ang[2] = lwp->eyeR_ang[2];
    lwp->eyeL->ang[0] = lwp->eyeL_ang[0];
    lwp->eyeL->ang[1] = lwp->eyeL_ang[1];
    lwp->eyeL->ang[2] = lwp->eyeL_ang[2];
}
