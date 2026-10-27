/*
 * 1ST_READ chunk 8C103200 (first three functions).
 *
 *   func_8C103200  resets the object's state: twp->btimer = 0,
 *                  twp->value.f = 1.0, sets attr bit 0x10 of its second
 *                  collision shape, clears the flag B_8C7A51C8, resets two
 *                  node angles of its model and unhides four model nodes
 *                  (NJD_EVAL_HIDE). Callers: func_8C10324A, func_8C1032AC.
 *   func_8C10324A  task wrapper of func_8C103200 (caller func_8C1030F0).
 *   func_8C103252  advances the frame accumulator B_8C7A5170 by spd, copies
 *                  it to the motion work's nframe and returns 1 when the
 *                  next step would leave the motion (forward: beyond nbFrame
 *                  of action D_8C5C69C8[reqaction]; backward: below 0).
 *                  Caller: func_8C1032AC.
 *
 * B_8C7A5140 is the object's motion work (mtnjvwk, 0x2C bytes up to the
 * next symbol B_8C7A516C): func_8C1032AC stores reqaction = 2 with
 * MOV.W R0,@(4,R12) (8C103486). The split symbols B_8C7A5144 and
 * B_8C7A5150 are its reqaction and nframe. Accessing nframe as a struct
 * member is required to match (the member address is not CSE'd across
 * basic blocks; a plain Float global is).
 *
 * D_8C5CC310/D_8C5CC344 and D_8C5C9290/8D2C/8A20/8FC0 are NJS_OBJECT nodes
 * of a model in 1ST_READ data (D_8C5CC344.child = D_8C5CC310). They are
 * declared as arrays like the original object_xxx[] model data: only then
 * does SHC keep the base address and use @(disp,Rn). The code writes them
 * (per-instance copies on PC).
 */
#include "common.h"
#include "ninja.h"
#include "task.h"
#include "sa1/sa1.h"

extern Sint32 B_8C7A51C8;           /* flag, compared with 1 by func_8C1032AC */
extern mtnjvwk B_8C7A5140;          /* motion work of this object */
extern Float B_8C7A5170;            /* frame accumulator */

extern NJS_OBJECT D_8C5CC310[];
extern NJS_OBJECT D_8C5CC344[];
extern NJS_OBJECT D_8C5C9290[];
extern NJS_OBJECT D_8C5C8D2C[];
extern NJS_OBJECT D_8C5C8A20[];
extern NJS_OBJECT D_8C5C8FC0[];

extern PL_ACTION D_8C5C69C8[];      /* game data: action table of this object */

void func_8C103200(taskwk *twp)
{
    twp->btimer = 0;
    twp->value.f = 1.0f;
    CWP(twp)->info[1].attr |= 0x10;
    B_8C7A51C8 = 0;
    D_8C5CC344->ang[1] = 0;
    D_8C5CC310->ang[2] = 0x8000;
    D_8C5C9290->evalflags &= ~NJD_EVAL_HIDE;
    D_8C5C8D2C->evalflags &= ~NJD_EVAL_HIDE;
    D_8C5C8A20->evalflags &= ~NJD_EVAL_HIDE;
    D_8C5C8FC0->evalflags &= ~NJD_EVAL_HIDE;
}

void func_8C10324A(task *tp)
{
    func_8C103200(tp->twp);
}

Bool func_8C103252(Float spd)
{
    B_8C7A5170 += spd;
    B_8C7A5140.nframe = B_8C7A5170;
    if (spd > 0.0f && (Float)D_8C5C69C8[B_8C7A5140.reqaction].actptr->motion->nbFrame - spd < B_8C7A5140.nframe) {
        return 1;
    }
    if (spd < 0.0f && B_8C7A5140.nframe < -spd) {
        return 1;
    }
    return 0;
}
