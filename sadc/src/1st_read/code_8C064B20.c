#include "sa1/sa1.h"

/*
 * 1ST_READ chunk 8C064B20: the exec / dest of O_SAMPLE_M (set
 * up by objexec_O_SAMPLE_M, code_8C064980.c) and O_SAMPLE_L, the game's heavy
 * sample liftable objects (lift classes 5 and 6: only Big lifts them). Same
 * shape as O_SAMPLE_S (code_8C064980.c), but moved by the heavy step 8C058A1A
 * and drawn at pos + (0, 5, 0) scaled 0.5 (M) or pos + (0, 15, 0) scaled 1.5
 * (L).
 *
 *   8C064B20  O_SAMPLE_M exec
 *   8C064BB4  O_SAMPLE_M dest (empty)
 *   8C064BB8  O_SAMPLE_L init: CCL_Init (D_8C1BA738: 2 shapes, id 4), class 6,
 *             exec 8C064C04, dest 8C064D00
 *   8C064C04  O_SAMPLE_L exec
 *
 * O_SAMPLE_L's dest 8C064D00 is a chunk of its own: code_8C064D00.c (one C
 * file per chunk: the splice step assembles a file's compiled .src once per
 * chunk, into the same .obj, so two chunks from one file race under ninja).
 */

extern NJS_MODEL Debug_Cylinder;    /* 8C199918 debug cylinder model */
extern CCL_INFO D_8C1BA738[];       /* O_SAMPLE_L collision: 2 shapes */


void func_8C064BB4(task *tp);
void func_8C064C04(task *tp);
void func_8C064D00(task *tp);                                  /* O_SAMPLE_L dest (code_8C064D00.c) */

void func_8C064B20(task *tp)
{
    taskwk *twp;

    twp = tp->twp;
    if (func_8C0919CC(tp)) {
        return;
    }
    func_8C058A1A(tp);
    func_8C043B60(twp, 0xFF);
    njPushMatrix(NULL);
    njTranslate(NULL, twp->pos.x, twp->pos.y + 5.0f, twp->pos.z);
    twp->ang.y ? njRotateY(NULL, twp->ang.y) : (void)0;
    njScale(NULL, 0.5f, 0.5f, 0.5f);
    func_8C036F12(twp->counter.ptr);
    njPopMatrix(1);
    func_8C043A20(0.0f, 0.0f, 0.0f, 0.0f);
}

void func_8C064BB4(task *tp)
{
}

void objexec_O_SAMPLE_L(task *tp)
{
    taskwk *twp;
    motionwk *mwp;

    twp = tp->twp;
    mwp = tp->mwp;
    twp->counter.ptr = &Debug_Cylinder;
    if (!func_8C042DC4(tp)) {
        CCL_Init(tp, D_8C1BA738, 4, 1, 2);
    }
    func_8C0586A0(twp, mwp, 6);
    tp->dest = func_8C064D00;
    tp->exec = func_8C064C04;
}

void func_8C064C04(task *tp)
{
    taskwk *twp;

    twp = tp->twp;
    if (func_8C0919CC(tp)) {
        return;
    }
    func_8C058A1A(tp);
    func_8C043B60(twp, 0x80);
    njPushMatrix(NULL);
    njTranslate(NULL, twp->pos.x, twp->pos.y + 15.0f, twp->pos.z);
    twp->ang.y ? njRotateY(NULL, twp->ang.y) : (void)0;
    njScale(NULL, 1.5f, 1.5f, 1.5f);
    func_8C036F12(twp->counter.ptr);
    njPopMatrix(1);
    func_8C043A20(0.0f, 0.0f, 0.0f, 0.0f);
}
