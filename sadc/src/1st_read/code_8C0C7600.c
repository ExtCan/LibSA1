#include "common.h"
#include "ninja.h"
#include "sa1/sa1.h"

/*
 * 1ST_READ 8C0C7600-8C0C7786 (chunk 8C0C7600-8C0C80D4, functions 1-3 of 7):
 * Knuckles' charge effect task and the action-chain check of actions 54/55.
 *
 *  func_8C0C7600  exec of the effect task created by KnucklesMain case 0
 *                 (CreateElementalTask(2, 3, func_8C0C7694), counter.b[0] =
 *                 player number, value.ptr = &pwp->free.w[5]). Destroys
 *                 itself when the player is gone; while the counter is
 *                 1..119 calls func_8C0CB580(counter / 120) every 4th
 *                 count; at 120 calls func_8C0CB7A0 with a pulsing scale
 *                 (sin(btimer * 1024) + 1) / 2 at the player's first
 *                 collision shape centre. btimer counts frames.
 *  func_8C0C7694  task init: exec = func_8C0C7600, dest = func_8C0C75AC.
 *  func_8C0C769E  KnucklesModeMachine check for actions 54/55: before frame
 *                 20 a press of D_8C19E38C chains the next action
 *                 (KnucklesActionList[reqaction].next = reqaction + 1);
 *                 from frame 20 a press, or a chain set earlier, switches
 *                 to reqaction + 1 at frame 0. Both clear free.w[5].
 *
 * KnucklesActionList[].next is written and read with halfword accesses
 * (MOV.W at 8C0C76EC/8C0C7764): the header has PL_ACTION.next as Uint8 +
 * pad07 (PSetMotion reads it with MOV.B), so the accesses go through a
 * Sint16 pointer, as in code_8C0DCA6C.c (little endian,
 * -fno-strict-aliasing on PC: next = value, pad07 = 0).
 */

/* the task's dest (previous chunk 8C0C71A0) */
extern void func_8C0C75AC(task *tp);
/* effect helpers (chunk 8C0CB580/8C0CB7A0) */
extern void func_8C0CB580(Float rate);
extern void func_8C0CB7A0(NJS_POINT3 *pos, Float scl);

/*
 * The player pointer is assigned inside the NULL test: SHC then keeps it
 * in a stack slot across the njSin call, as the original does (as a
 * separate statement it takes R12). func_8C0CB7A0 keeps R4 (pos) and FR4
 * (scl); the order is inferred from the argument evaluation order.
 */
void func_8C0C7600(task *tp)
{
    taskwk *twp;
    taskwk *ptwp;
    Sint32 cnt;

    twp = tp->twp;
    if (!(ptwp = playertwp[(Uint8)twp->counter.b[0]])) {
        DestroyTask(tp);
        return;
    }
    twp->btimer++;
    cnt = *(Sint16 *)twp->value.ptr;
    if (cnt > 0 && cnt < 120 && !(cnt & 3)) {
        func_8C0CB580((Float)cnt * 0.0083333330f);
        return;
    }
    if (cnt == 120) {
        func_8C0CB7A0(&CWP(ptwp)->info->center, (njSin(twp->btimer << 10) + 1.0f) * 0.5f);
    }
}

void func_8C0C7694(task *tp)
{
    tp->dest = func_8C0C75AC;
    tp->exec = func_8C0C7600;
}

/*
 * The (Sint32) casts on both operands of the button tests are needed: with
 * the Uint32 types SHC hoists the D_8C19E38C load above the nframe test
 * into a callee-saved register (see the NOTE in sa1/input.h).
 */
void func_8C0C769E(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    switch (pwp->mj.reqaction) {
    case 54:
    case 55:
        if (pwp->mj.nframe < 20.0f) {
            if ((Sint32)B_8C754C08[(Uint8)twp->counter.b[0]].press & (Sint32)D_8C19E38C) {
                *(Sint16 *)&KnucklesActionList[pwp->mj.reqaction].next = pwp->mj.reqaction + 1;
                pwp->free.w[5] = 0;
            }
        } else if (((Sint32)B_8C754C08[(Uint8)twp->counter.b[0]].press & (Sint32)D_8C19E38C)
                   || *(Sint16 *)&KnucklesActionList[pwp->mj.reqaction].next != 0) {
            pwp->mj.reqaction++;
            pwp->mj.nframe = 0.0f;
            pwp->free.w[5] = 0;
        }
        break;
    }
}
