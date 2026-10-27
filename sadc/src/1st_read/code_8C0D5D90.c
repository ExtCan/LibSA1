#include "sa1/sa1.h"

/*
 * E-102 Gamma lock-on, marker release (1ST_READ chunk 8C0D5D90):
 *
 *  func_8C0D5D90  release the lock-on marker of a target task (tp->mwp is an
 *                 E102_TARGETWK): the marker task gets the release animation
 *                 exec func_8C0D5B54 (twp->timer.l = 0) when its taskwk flag
 *                 has 0x200, else DestroyTask; the target's marker slot and
 *                 the marker's target are cleared. If the marker work flag
 *                 has 0x200 and the target flag 0x1000, the task kept in the
 *                 marker work at +0x14 gets taskwk flag 0x100.
 *  func_8C0D5DF6  sets marker twp->flag 0x100, then func_8C0D5D90.
 *  func_8C0D5E26  sets marker twp->flag 0x300, then func_8C0D5D90.
 */

/* awp of a lock-on marker task (CAlloc(1, 0x34) by func_8C0D38C6); local
 * copy of code_8C0D32A0.c (ewk kept as void * here) */
typedef struct {
    Sint8 mode;                 /* 0x00 */
    Sint8 num;                  /* 0x01 */
    Sint16 unk02;               /* 0x02 */
    Sint16 flag;                /* 0x04 0x200 tested here */
    Sint16 unk06;               /* 0x06 */
    task *target;               /* 0x08 locked-on task, cleared here */
    taskwk *ptwp;               /* 0x0C E-102's taskwk */
    void *ewk;                  /* 0x10 E102WK * */
    task *unk14;                /* 0x14 twp->flag |= 0x100 here */
    Sint32 unk18;               /* 0x18 */
    Float scale;                /* 0x1C */
    Sint32 unk20;               /* 0x20 */
    Angle ang24;                /* 0x24 */
    Angle ang28;                /* 0x28 */
    task *unk2C;                /* 0x2C */
    TaskFuncPtr target_exec;    /* 0x30 */
} E102_MARKERWK;                /* 0x34 */

/* tp->mwp of a task E-102 can lock on (larger than motionwk); local copy of
 * code_8C0D32A0.c */
typedef struct {
    motionwk mw;                /* 0x00 */
    task *marker_tp;            /* 0x40 lock-on marker task, NULL if not locked */
    TaskFuncPtr dest;           /* 0x44 original tp->dest */
    Sint32 unk48;               /* 0x48 */
    Sint16 unk4C;               /* 0x4C */
    Uint16 flag;                /* 0x4E 0x1000 tested here */
} E102_TARGETWK;                /* 0x50 */

SA1_ASSERT_SIZE(E102_MARKERWK, 0x34);
SA1_ASSERT_OFFSET(E102_MARKERWK, flag, 0x04);
SA1_ASSERT_OFFSET(E102_MARKERWK, target, 0x08);
SA1_ASSERT_OFFSET(E102_MARKERWK, unk14, 0x14);
SA1_ASSERT_SIZE(E102_TARGETWK, 0x50);
SA1_ASSERT_OFFSET(E102_TARGETWK, marker_tp, 0x40);
SA1_ASSERT_OFFSET(E102_TARGETWK, flag, 0x4E);

void func_8C0D5B54(task *tp);   /* marker release animation exec (code_8C0D5A00.c) */

void func_8C0D5D90(task *tp)
{
    E102_TARGETWK *mwp;
    task *mtp;
    E102_MARKERWK *awp;
    task *ttp;

    if (tp == NULL) {
        return;
    }
    mwp = (E102_TARGETWK *)tp->mwp;
    if (mwp == NULL) {
        return;
    }
    mtp = mwp->marker_tp;
    if (mtp == NULL) {
        return;
    }
    if (mtp->twp == NULL) {
        return;
    }
    if (mtp->twp->flag & 0x200) {
        mtp->exec = func_8C0D5B54;
        mtp->twp->timer.l = 0;
    } else {
        mtp->exec = DestroyTask;
    }
    mwp->marker_tp = NULL;
    awp = (E102_MARKERWK *)mtp->awp;
    awp->target = NULL;
    if (awp->flag & 0x200) {
        ttp = awp->unk14;
        if (ttp != NULL) {
            if (mwp->flag & 0x1000) {
                ttp->twp->flag |= 0x100;
            }
        }
    }
}

void func_8C0D5DF6(task *tp)
{
    E102_TARGETWK *mwp;

    if (tp == NULL) {
        return;
    }
    mwp = (E102_TARGETWK *)tp->mwp;
    if (mwp == NULL) {
        return;
    }
    if (mwp->marker_tp == NULL) {
        return;
    }
    if (mwp->marker_tp->twp != NULL) {
        mwp->marker_tp->twp->flag |= 0x100;
    }
    func_8C0D5D90(tp);
}

void func_8C0D5E26(task *tp)
{
    E102_TARGETWK *mwp;

    if (tp == NULL) {
        return;
    }
    mwp = (E102_TARGETWK *)tp->mwp;
    if (mwp == NULL) {
        return;
    }
    if (mwp->marker_tp == NULL) {
        return;
    }
    if (mwp->marker_tp->twp != NULL) {
        mwp->marker_tp->twp->flag |= 0x300;
    }
    func_8C0D5D90(tp);
}
