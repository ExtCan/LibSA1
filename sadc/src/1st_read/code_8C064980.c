#include "sa1/sa1.h"

/*
 * 1ST_READ chunk 8C064980: O_SAMPLE_S and O_SAMPLE_M, the game's sample
 * liftable objects (object list entries; SA Tools DC_SA1 objlist). Each is a
 * Debug_Cylinder model with two collision shapes, set up as a movable object
 * of lift class 4 (S) or 5 (M, heavy: only Big lifts it) by 8C0586A0, and
 * moved every step by the movable-object step 8C05872A. They are the game's
 * reference for libsa1's HOLDABLE host object (libsa1_interaction.md 6.1 and
 * 6.7, reference hold_ec1).
 *
 *   8C064980  O_SAMPLE_S init: model, CCL_Init (D_8C1BA668: 2 shapes, id 4),
 *             class 4, exec 8C0649CC, dest 8C064A60
 *   8C0649CC  O_SAMPLE_S exec: SET range check, movable step, debug colour,
 *             draws the model at pos + (0, 2.5, 0) turned by ang.y, scale 0.25
 *   8C064A60  O_SAMPLE_S dest (empty)
 *   8C064A64  O_SAMPLE_M init: as S with D_8C1BA6D0, class 5, exec 8C064B20,
 *             dest 8C064BB4 (both in code_8C064B20.c, with O_SAMPLE_L)
 */

extern NJS_MODEL Debug_Cylinder;    /* 8C199918 debug cylinder model */
extern CCL_INFO D_8C1BA668[];       /* O_SAMPLE_S collision: 2 shapes */
extern CCL_INFO D_8C1BA6D0[];       /* O_SAMPLE_M collision: 2 shapes */

void func_8C064B20(task *tp);                                  /* O_SAMPLE_M exec (code_8C064B20.c) */
void func_8C064BB4(task *tp);                                  /* O_SAMPLE_M dest (code_8C064B20.c) */

void func_8C0649CC(task *tp);
void func_8C064A60(task *tp);

void objexec_O_SAMPLE_S(task *tp)
{
    taskwk *twp;
    motionwk *mwp;

    twp = tp->twp;
    mwp = tp->mwp;
    twp->counter.ptr = &Debug_Cylinder;
    if (!func_8C042DC4(tp)) {
        CCL_Init(tp, D_8C1BA668, 4, 1, 2);
    }
    func_8C0586A0(twp, mwp, 4);
    tp->dest = func_8C064A60;
    tp->exec = func_8C0649CC;
}

void func_8C0649CC(task *tp)
{
    taskwk *twp;

    twp = tp->twp;
    if (func_8C0919CC(tp)) {
        return;
    }
    func_8C05872A(tp);
    func_8C043B60(twp, 0xFF);
    njPushMatrix(NULL);
    njTranslate(NULL, twp->pos.x, twp->pos.y + 2.5f, twp->pos.z);
    twp->ang.y ? njRotateY(NULL, twp->ang.y) : (void)0;
    njScale(NULL, 0.25f, 0.25f, 0.25f);
    func_8C036F12(twp->counter.ptr);
    njPopMatrix(1);
    func_8C043A20(0.0f, 0.0f, 0.0f, 0.0f);
}

void func_8C064A60(task *tp)
{
}

void objexec_O_SAMPLE_M(task *tp)
{
    taskwk *twp;
    motionwk *mwp;

    twp = tp->twp;
    mwp = tp->mwp;
    twp->counter.ptr = &Debug_Cylinder;
    if (!func_8C042DC4(tp)) {
        CCL_Init(tp, D_8C1BA6D0, 4, 1, 2);
    }
    func_8C0586A0(twp, mwp, 5);
    tp->dest = func_8C064BB4;
    tp->exec = func_8C064B20;
}
