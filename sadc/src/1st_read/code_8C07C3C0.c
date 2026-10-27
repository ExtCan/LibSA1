/*
 * Player look-at with a time limit (1ST_READ 8C07C3C0-8C07C550).
 * Chunk asm/1st_read/text/8C07BAC0.src.
 *
 *  func_8C07C3C0  destructor of the look timer task: stop looking (8C07C376)
 *  func_8C07C3CA  exec of the look timer task: free it when the player is
 *                 gone or the time is over
 *  func_8C07C3F2  look at a task for a time            (8C07C2E2, mode 3)
 *  func_8C07C44E  look at a position for a time        (8C07C318, mode 2)
 *  func_8C07C49E  look in a direction for a time       (8C07C34C, mode 1)
 *  func_8C07C520  hold task tp (pwp->htp), event request smode 32
 *
 * The look functions themselves are in code_8C07C298.c.
 */
#include "common.h"
#include "ninja.h"
#include "task.h"
#include "sa1/sa1.h"

/* any work (task.awp, CAlloc(1, 0x10)) of the look timer task */
typedef struct {
    Uint8 pno;                  /* 0x00 player number */
    Uint32 timer;               /* 0x04 frame counter */
    Uint32 time;                /* 0x08 frames to look */
} PL_LOOKTIMEWK;

SA1_ASSERT_OFFSET(PL_LOOKTIMEWK, timer, 0x04);
SA1_ASSERT_OFFSET(PL_LOOKTIMEWK, time, 0x08);

ASM_STUB(void func_8C07C2E2(Uint8 pno, task *tp, NJS_POINT3 *pos))
ASM_STUB(void func_8C07C318(Uint8 pno, NJS_POINT3 *pos))
ASM_STUB(void func_8C07C34C(Uint8 pno, Angle3 *ang))
ASM_STUB(void func_8C07C376(Uint8 pno))

/* destructor of the look timer task */
void func_8C07C3C0(task *tp)
{
    func_8C07C376(((PL_LOOKTIMEWK *)tp->awp)->pno);
}

/* exec of the look timer task */
void func_8C07C3CA(task *tp)
{
    PL_LOOKTIMEWK *wk;

    wk = (PL_LOOKTIMEWK *)tp->awp;
    if (playertwp[wk->pno] == NULL || wk->timer++ > wk->time) {
        FreeTask(tp);
    }
}

/* player pno looks at task target for time frames */
void func_8C07C3F2(Uint8 pno, task *target, Uint32 time)
{
    task *tp;
    NJS_POINT3 pos;

    if (playertwp[pno] != NULL && target != NULL) {
        if ((tp = CreateElementalTask(8, 5, func_8C07C3CA)) != NULL) {
            pos.x = pos.y = pos.z = 0.0f;
            tp->dest = func_8C07C3C0;
            ((PL_LOOKTIMEWK *)tp->awp)->pno = pno;
            ((PL_LOOKTIMEWK *)tp->awp)->time = time;
            func_8C07C2E2(pno, target, &pos);
        }
    }
}

/* player pno looks at position pos for time frames */
void func_8C07C44E(Uint8 pno, NJS_POINT3 *pos, Uint32 time)
{
    task *tp;

    if (playertwp[pno] != NULL && pos != NULL) {
        if ((tp = CreateElementalTask(8, 5, func_8C07C3CA)) != NULL) {
            tp->dest = func_8C07C3C0;
            ((PL_LOOKTIMEWK *)tp->awp)->pno = pno;
            ((PL_LOOKTIMEWK *)tp->awp)->time = time;
            func_8C07C318(pno, pos);
        }
    }
}

/* player pno looks in direction ang for time frames */
void func_8C07C49E(Uint8 pno, Angle3 *ang, Uint32 time)
{
    task *tp;

    if (playertwp[pno] != NULL && ang != NULL) {
        if ((tp = CreateElementalTask(8, 5, func_8C07C3CA)) != NULL) {
            tp->dest = func_8C07C3C0;
            ((PL_LOOKTIMEWK *)tp->awp)->pno = pno;
            ((PL_LOOKTIMEWK *)tp->awp)->time = time;
            func_8C07C34C(pno, ang);
        }
    }
}

/* player pno holds task htp (event request smode 32) unless flag 0x800 */
void func_8C07C520(Uint8 pno, task *htp)
{
    taskwk *twp;

    twp = playertwp[pno];
    if (!(twp->flag & 0x800)) {
        twp->flag |= 0x1000;
        twp->smode = 32;
        playerpwp[pno]->htp = htp;
    }
}
