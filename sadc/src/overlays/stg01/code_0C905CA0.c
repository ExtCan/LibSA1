/*
 * STG01 0C905CA0: O_ASIATO, footprints in the sand (chunk 0C905CA0-0C906820,
 * functions 1-4 of 8; objexec_O_ASIATO and the rest are not decompiled here).
 *
 *  func_0C905CA0  disp: draws the footprint ring buffer from the newest
 *                 print back to the oldest, each one fainter (constant
 *                 material alpha offset -0.05, then -0.0095 per print).
 *  func_0C905D9C  world positions of player pno's feet: playerwk
 *                 rightfoot_pos/leftfoot_pos with y replaced by -(distance
 *                 collision centre - twp->pos), through the player's
 *                 translation/rotation and an optional offset; also returns
 *                 the print angles (twp->ang with y = 0x8000 - y).
 *  func_0C905F40  appends a footprint to the ring buffer (overwrites the
 *                 oldest one when all 100 are in use); y gets a random
 *                 0.05..0.06 lift against z-fighting.
 *  func_0C905FE6  probes the ground under a foot (func_8C09282E, six axis
 *                 land probes from 1.5 above) and adds a print on the ground
 *                 found below (hit[2]) when that polygon has attribute
 *                 0x100000 (sand), tilted to the ground slope.
 *
 * All four functions match. Code generation notes:
 *  - the rotations use the ?: idiom ("ang ? njRotateZ(NULL, ang) : (void)0"):
 *    SHC then keeps each angle in a spilled temp at @R15 like the original
 *    (an if with an Angle local gives R5).
 *  - 0CA0: func_8C043A20 is called directly (R9 = its address and R8 = 99
 *    are SHC's own loop-invariant choices); a separate twp local gives the
 *    "MOV.L @(32,R4),R4" load.
 *  - 0D9C: "rpos.y = -d; lpos.y = rpos.y;" gives the FMOV FR0,FR3 copy (a
 *    chained assignment stores FR0 twice); the unused 12-byte local fills
 *    the original's 48-byte frame (sp+12..23 is never accessed).
 *  - 0FE6: the original keeps the hit pointer in R14, i.e. SHC saw it live
 *    across the func_0C905F40 call; the dead load "(void)hit->findflag;"
 *    after the call reproduces that (it emits no code).
 *  - SA1_FMAC count: 1 (func_0C905F40), as in the asm.
 */
#include "common.h"
#include "sa1/pending.h"

/* One footprint (stride 0x1C: func_0C905CA0 ADD #-28, MUL.L 28). */
typedef struct {
    NJS_POINT3 pos;             /* 0x00 */
    Angle3 ang;                 /* 0x0C applied as Z, X, then Y + 0x8000 */
    NJS_MODEL *model;           /* 0x18 NJS_OBJECT.model of the print object */
} ASIATO_PRINT;                 /* 0x1C */

/* Footprint work (taskwk.value.ptr; objexec_O_ASIATO: func_8C098A80(0xB1C)). */
typedef struct {
    ASIATO_PRINT print[100];    /* 0x000 ring buffer */
    Sint32 start;               /* 0xAF0 index of the oldest print */
    Sint32 num;                 /* 0xAF4 prints in use (max 100) */
    NJS_POINT3 unkAF8;          /* 0xAF8 player position at creation (objexec then memsets the whole block) */
    NJS_POINT3 unkB04;          /* 0xB04 */
    NJS_POINT3 unkB10;          /* 0xB10 */
} ASIATO_WORK;                  /* 0xB1C */

SA1_ASSERT_SIZE(ASIATO_PRINT, 0x1C);
SA1_ASSERT_OFFSET(ASIATO_PRINT, ang, 0x0C);
SA1_ASSERT_OFFSET(ASIATO_PRINT, model, 0x18);
SA1_ASSERT_SIZE(ASIATO_WORK, 0xB1C);
SA1_ASSERT_OFFSET(ASIATO_WORK, start, 0xAF0);
SA1_ASSERT_OFFSET(ASIATO_WORK, num, 0xAF4);
SA1_ASSERT_OFFSET(ASIATO_WORK, unkB10, 0xB10);

/* 1ST_READ functions not declared in the sa1 headers */
Sint32 func_8C099538(void);                     /* njSetTexture(B_8C79F0F4) when it is set; returns 0/1 */
extern void func_8C02E7E4(void);                /* hand-written: loads the matrix below the top of the Ninja stack into XMTRX (code_8C051E56.c) */
Sint32 func_8C09282E(xyyzzxsdwstr *xyzp);       /* six axis land probes (code_8C09282E.c) */

void func_0C905CA0(task *tp)
{
    ASIATO_WORK *wk;
    Sint32 num;
    Sint32 idx;
    ASIATO_PRINT *pr;
    Float alpha;
    Sint32 i;
    taskwk *twp;

    twp = tp->twp;
    wk = twp->value.ptr;
    func_8C099538();
    num = wk->num;
    if (num == 0) {
        return;
    }
    idx = wk->start + num - 1;
    if (idx >= 100) {
        idx -= 100;
    }
    pr = &wk->print[idx];
    alpha = -0.050000013f;
    njPushMatrix(NULL);
    for (i = 0; i < num; i++) {
        func_8C02E7E4();
        func_8C043A20(alpha, 0.0f, 0.0f, 0.0f);
        njTranslateV(NULL, &pr->pos);
        pr->ang.z ? njRotateZ(NULL, pr->ang.z) : (void)0;
        pr->ang.x ? njRotateX(NULL, pr->ang.x) : (void)0;
        pr->ang.y + 0x8000 ? njRotateY(NULL, pr->ang.y + 0x8000) : (void)0;
        alpha -= 0.0095f;
        func_8C03700C(pr->model);
        idx--;
        pr--;
        if (idx < 0) {
            pr = &wk->print[99];
            idx = 99;
        }
    }
    njPopMatrix(1);
    func_8C043A20(0.0f, 0.0f, 0.0f, 0.0f);
}

void func_0C905D9C(Sint32 pno, Angle3 *ang, NJS_POINT3 *right, NJS_POINT3 *left, NJS_VECTOR *ofs)
{
    NJS_POINT3 lpos;
    NJS_POINT3 rpos;
    NJS_POINT3 unused;          /* frame filler (sp+12), never accessed */
    taskwk *twp;
    CCL_INFO *info;
    playerwk *pwp;

    twp = B_8C78C608[pno];
    info = CWP(twp)->info;
    pwp = func_8C07CFDE(pno);
    rpos = pwp->rightfoot_pos;
    lpos = pwp->leftfoot_pos;
    rpos.y = -njDistanceP2P(&info->center, &twp->pos);
    lpos.y = rpos.y;
    njPushMatrix(D_8C67C640);
    njTranslateV(NULL, &info->center);
    twp->ang.z ? njRotateZ(NULL, twp->ang.z) : (void)0;
    twp->ang.x ? njRotateX(NULL, twp->ang.x) : (void)0;
    0x8000 - twp->ang.y ? njRotateY(NULL, 0x8000 - twp->ang.y) : (void)0;
    if (ofs != NULL) {
        njTranslateV(NULL, ofs);
    }
    njCalcPoint(NULL, &lpos, &lpos);
    njCalcPoint(NULL, &rpos, &rpos);
    njPopMatrix(1);
    *ang = twp->ang;
    ang->y = 0x8000 - ang->y;
    *left = lpos;
    *right = rpos;
}

void func_0C905F40(NJS_POINT3 *pos, Angle3 *ang, NJS_OBJECT *obj, taskwk *twp)
{
    ASIATO_WORK *wk;
    Sint32 idx;
    ASIATO_PRINT *pr;

    wk = twp->value.ptr;
    idx = wk->start + wk->num;
    if (idx >= 100) {
        idx -= 100;
    }
    pr = &wk->print[idx];
    pr->pos = *pos;
    /* 0.05 + rand() * (0.01 / 32768) */
    pr->pos.y += SA1_FMAC(3.0517578e-7f, (Float)rand(), 0.049999998f);
    pr->ang = *ang;
    pr->model = obj->model;
    if (wk->num < 100) {
        wk->num++;
    } else {
        wk->start++;
        if (wk->start >= 100) {
            wk->start = 0;
        }
    }
}

void func_0C905FE6(NJS_POINT3 *pos, Angle3 *ang, NJS_OBJECT *obj, taskwk *twp)
{
    xyyzzxsdwstr xyz;
    NJS_POINT3 p;
    Angle3 a;
    xssunit *hit;

    xyz.pos = *pos;
    xyz.pos.y += 1.5f;
    func_8C09282E(&xyz);
    hit = &xyz.hit[2];
    if (hit->findflag != 0 && (hit->objatt & 0x100000)) {
        p = *pos;
        p.y = hit->onpos;
        a = *ang;
        a.x = hit->angx;
        a.z = hit->angz;
        func_0C905F40(&p, &a, obj, twp);
        (void)hit->findflag;    /* MATCHING: keeps hit in R14 (no code) */
    }
}
