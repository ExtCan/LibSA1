/*
 * Player power-up spawners (8C07D3A0-8C07D560), called by the item box
 * handlers (8C05D764, 8C05D884, 8C05D900, 8C05D964) with pno 0:
 *   8C07D3A0 speed up (task 8C07D2DA, any work below)
 *   8C07D3DC magnetic barrier (item bit 2, task 8C043C9E)
 *   8C07D43E barrier (item bit 1, task 8C044060)
 *   8C07D4A0 invincibility (item bit 0x8000, task 8C044284, then
 *            func_8C107C80(55), music/jingle request)
 *
 * playerwk.item is Uint16 in sa1/player.h, but the code reads it with a
 * plain MOV.W (no EXTU.W) and masks it with MOV #-4: the original field is
 * Sint16. The (Sint16) casts and the Sint16 lvalue below reproduce that
 * (signed/unsigned variants of a type may alias, so this is portable).
 */

#include "common.h"
#include "task.h"
#include "sa1/sa1.h"

/*
 * Any work (task.awp, CAlloc(1, 0x10)) of the task run by func_8C07D2DA:
 * pno at 0x00; 8C07D2DA saves playerpwp[pno]->p.run_accel/max_x_spd/
 * lim_frict at 0x04/0x08/0x0C.
 */
typedef struct {
    Uint8 pno;                  /* 0x00 */
    Float run_accel;            /* 0x04 */
    Float max_x_spd;            /* 0x08 */
    Float lim_frict;            /* 0x0C */
} PL_SPDUPWK;

SA1_ASSERT_SIZE(PL_SPDUPWK, 0x10);

void func_8C07D2DA(task *tp);
void func_8C043C9E(task *tp);
void func_8C044060(task *tp);
void func_8C044284(task *tp);
void func_8C107C80(Sint32 no);

void func_8C07D3A0(Uint8 pno)
{
    task *tp;

    if (pno >= 1 && (pno > 7 || playertwp[pno] == NULL)) {
        pno = 0;
    }
    tp = CreateElementalTask(8, 2, func_8C07D2DA);
    if (tp != NULL) {
        ((PL_SPDUPWK *)tp->awp)->pno = pno;
    }
}

/*
 * NOTE: unlike func_8C07D3A0 (pno > 7 || slot empty), the next three only
 * fall back to 0 when pno >= 1 AND pno > 7 AND playertwp[pno] == NULL, so a
 * valid pno is never replaced and an out-of-range one would index past the
 * 8-entry slot arrays (on DC as well). Kept as in the binary; every caller
 * passes 0. 8C07D3DC/8C07D43E only match with the nested ifs, 8C07D4A0 only
 * with the && chain.
 */
void func_8C07D3DC(Uint8 pno)
{
    playerwk *pwp;
    task *tp;

    if (pno >= 1) {
        if (pno > 7) {
            if (playertwp[pno] == NULL) {
                pno = 0;
            }
        }
    }
    pwp = playerpwp[pno];
    if (pwp != NULL) {
        if (!((Sint16)pwp->item & 2)) {
            tp = CreateElementalTask(2, 2, func_8C043C9E);
            if (tp != NULL) {
                tp->twp->counter.b[0] = pno;
                *(Sint16 *)&pwp->item &= ~3;
                pwp->item |= 2;
            }
        }
    }
}

void func_8C07D43E(Uint8 pno)
{
    playerwk *pwp;
    task *tp;

    if (pno >= 1) {
        if (pno > 7) {
            if (playertwp[pno] == NULL) {
                pno = 0;
            }
        }
    }
    pwp = playerpwp[pno];
    if (pwp != NULL) {
        if (!((Sint16)pwp->item & 1)) {
            tp = CreateElementalTask(2, 2, func_8C044060);
            if (tp != NULL) {
                tp->twp->counter.b[0] = pno;
                *(Sint16 *)&pwp->item &= ~3;
                pwp->item |= 1;
            }
        }
    }
}

void func_8C07D4A0(Uint8 pno)
{
    playerwk *pwp;
    task *tp;

    if (pno >= 1 && pno > 7 && playertwp[pno] == NULL) {
        pno = 0;
    }
    pwp = playerpwp[pno];
    if (pwp != NULL && !((Sint16)pwp->item & 0x8000)) {
        tp = CreateElementalTask(2, 2, func_8C044284);
        if (tp != NULL) {
            tp->twp->counter.b[0] = pno;
            pwp->item |= 0x8000;
            func_8C107C80(55);
        }
    }
}
