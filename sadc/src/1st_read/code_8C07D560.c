#include "common.h"
#include "ninja.h"
#include "task.h"
#include "sa1/sa1.h"

/* Per-character byte table (8 bytes, indexed by taskwk.counter.b[1]) */
extern Sint8 B_8C798690[8];

void func_8C07D560(task *tp);

/* Power-up flag task: keeps playerwk.item bit 0x8000 set for 120 frames,
 * then clears it and frees itself. counter.b[0] = player number. */
void func_8C07D560(task *tp)
{
    taskwk *twp;
    playerwk *pwp;

    twp = tp->twp;
    pwp = playerpwp[(Uint8)twp->counter.b[0]];
    twp->wtimer++;
    if (twp->wtimer > 120) {
        if (pwp) {
            pwp->item &= ~0x8000;
        }
        FreeTask(tp);
    } else if (pwp) {
        playerpwp[(Uint8)twp->counter.b[0]]->item |= 0x8000;
    }
}

void func_8C07D5AA(Uint8 pno)
{
    task *tp;

    /* sic: the original tests pno > 7 (not pno <= 7) */
    if (pno >= 1 && pno > 7 && !playertwp[pno]) {
        pno = 0;
    }
    tp = CreateElementalTask(2, 0, func_8C07D560);
    if (tp) {
        tp->twp->counter.b[0] = pno;
    }
}

void func_8C07D5E8(Uint8 pno, Sint8 val)
{
    taskwk *twp;

    twp = playertwp[pno];
    B_8C798690[(Uint8)twp->counter.b[1]] = val;
}

Sint8 func_8C07D5FC(Uint8 pno)
{
    taskwk *twp;

    twp = playertwp[pno];
    return B_8C798690[(Uint8)twp->counter.b[1]];
}

/* Make player pno hold task htp (smode 0x25; overlays pass an object task) */
void func_8C07D60E(Uint8 pno, task *htp)
{
    taskwk *twp;

    twp = playertwp[pno];
    twp->flag |= 0x1000;
    twp->smode = 0x25;
    twp->value.tp = htp;
}

/* Release the task held by player pno */
void func_8C07D628(Uint8 pno)
{
    taskwk *twp;
    playerwk *pwp;

    twp = playertwp[pno];
    pwp = playerpwp[pno];
    if (twp && (twp->flag & 0x800) && pwp->htp) {
        twp->flag &= ~0x800;
        if (pwp->htp->twp) {
            pwp->htp->twp->flag &= ~0x1000;
        }
        pwp->htp = NULL;
    }
}
