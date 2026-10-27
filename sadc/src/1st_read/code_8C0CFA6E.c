#include "sa1/sa1.h"

/*
 * E-102 Gamma (1ST_READ chunk 8C0CBE00):
 *   func_8C0CFA6E  E-102 player task destructor (tp->dest; caller E102Main):
 *                  frees the two line buffers and the E-102 work kept in
 *                  pwp->free.ptr[0], destroys its child task, clears
 *                  pwp->mj.reqaction, frees the player slot and tail-calls
 *                  8C0D6A84 (FreeTask of the task in D_8C4BB1CC).
 */

/*
 * Line draw list of the E-102 work (0x20 bytes; init 8C0D5552 copies a
 * 0x20-byte template from D_8C42311C/D_8C42313C, then 8C0D54A0 MAllocs
 * p3c.p (num * 2 * 12) and p3c.col (num * 2 * 4) and fills them from
 * src_p/src_col through idx; 8C0D55A6 MFrees them; drawn with njDrawLine3D).
 */
typedef struct {
    NJS_POINT3 *src_p;          /* 0x00 source points (12-byte stride, 8C0D54A0) */
    NJS_COLOR *src_col;         /* 0x04 source colours (4-byte stride) */
    Sint16 *idx;                /* 0x08 index list read with MOV.W @R11+ */
    Sint16 num;                 /* 0x0C index count (MOV.W @(12,R4)) */
    Sint16 pad0E;               /* 0x0E */
    NJS_POINT3COL p3c;          /* 0x10 p/col MAlloc'd, tex = NULL, num = 2 * num */
} E102_LINE;                    /* 0x20 */

/*
 * E-102 work (pwp->free.ptr[0]; E102Main 8C0CC0F2: CAlloc(1, 0x244)).
 * Only the fields used here are named; the rest is unresolved padding
 * (shared type still to be defined, see type notes).
 */
typedef struct {
    Uint8 unk00[0x6C];          /* 0x00 */
    task *tp;                   /* 0x6C child task, DestroyTask'd here */
    Uint8 unk70[0xB0 - 0x70];   /* 0x70 */
    E102_LINE line0;            /* 0xB0 init 8C0D5552, freed 8C0D55A6 */
    E102_LINE line1;            /* 0xD0 */
    Uint8 unkF0[0x244 - 0xF0];  /* 0xF0 */
} E102WK;                       /* 0x244 */

SA1_ASSERT_SIZE(E102_LINE, 0x20);
SA1_ASSERT_OFFSET(E102_LINE, p3c, 0x10);
SA1_ASSERT_SIZE(E102WK, 0x244);
SA1_ASSERT_OFFSET(E102WK, tp, 0x6C);
SA1_ASSERT_OFFSET(E102WK, line0, 0xB0);
SA1_ASSERT_OFFSET(E102WK, line1, 0xD0);

void func_8C0D55A6(E102_LINE *a, E102_LINE *b);     /* MFree a/b->p3c.p and .col */
void func_8C0D6A84(void);                           /* FreeTask(D_8C4BB1CC), clear it */

void func_8C0CFA6E(task *tp)
{
    taskwk *twp;
    motionwk *mwp;
    playerwk *pwp;
    E102WK *ewk;

    twp = tp->twp;
    mwp = tp->mwp;
    pwp = mwp->work.pwp;
    ewk = (E102WK *)pwp->free.ptr[0];
    func_8C0D55A6(&ewk->line0, &ewk->line1);
    if (ewk->tp != NULL) {
        DestroyTask(ewk->tp);
    }
    MFree(ewk);
    pwp->mj.reqaction = 0;
    func_8C06C5CA((Uint8)twp->counter.b[0]);
    func_8C098FFC(6);
    func_8C0945D0(twp);
    func_8C0D6A84();
}
