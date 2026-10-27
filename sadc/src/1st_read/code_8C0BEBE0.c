/*
 * 1ST_READ 8C0BEBE0-8C0BEDA0: Tails action chaining helper (called by
 * TailsModeMachine 8C0B70F4 with (twp, pwp)).
 *
 *  func_8C0BEBE0  for the requested actions 43..53: near the end of the
 *                 current motion (nframe > nbFrame - 10) and while button
 *                 D_8C19E38C is held with equipment bit 0x10, rewrites the
 *                 chained action (TailsActionList[reqaction].next):
 *                 stick held -> one of the 8 directional actions 46..53
 *                 (stick angle in 45 degree sectors), SE 0x2DE when it
 *                 changes; stick released -> 44 <-> 45 toggle (SE 0x2DE)
 *                 or 44/45 by frame parity. Before the end: next = 0.
 *                 NOTE: writes into the shared action table (per-instance
 *                 data on PC, portability.md 4).
 */
#include "common.h"
#include "sa1/sa1.h"

/* Tails SE: func_8C0940DA(se, 0, 0, 0) unless twp->counter.b[0] == 1 */
extern void func_8C0BFCA2(taskwk *twp, Sint32 se);

/*
 * The original stores a halfword at PL_ACTION +6 (MOV.W, next and the
 * following pad byte; the header has next as Uint8 because PSetMotion reads
 * it with MOV.B). On PC (little endian, -fno-strict-aliasing) the same store
 * sets next and clears pad07 (same spelling as code_8C0DCA6C.c).
 * Matching notes: nbFrame goes through a Float local (SHC then converts the
 * unsigned with FLDI0/FCMP/GT; an inline (Float) cast gives CMP/PZ), and the
 * two early returns are one || condition (else the JSR target is R2, not R3).
 */
void func_8C0BEBE0(taskwk *twp, playerwk *pwp)
{
    PL_ACTION *act;
    Float nbf;
    Uint8 n;

    if (pwp->mj.reqaction < 43 || pwp->mj.reqaction > 53) {
        return;
    }
    act = &TailsActionList[pwp->mj.reqaction];
    nbf = (Float)act->actptr->motion->nbFrame;
    if (pwp->mj.nframe > nbf - 10.0f) {
        if (!(B_8C754C08[(Uint8)twp->counter.b[0]].on & D_8C19E38C) || !(pwp->equipment & 0x10)) {
            return;
        }
        if (GetAnalog(twp, NULL, NULL) != 0) {
            n = ((SA1_FTOI(atan2f((Float)(B_8C754C08[(Uint8)twp->counter.b[0]].y1 << 8),
                                  (Float)(B_8C754C08[(Uint8)twp->counter.b[0]].x1 << 8)) * 10430.381f) + 0x1000) >> 13 & 7) + 46;
            if (n == pwp->mj.reqaction) {
                *(Sint16 *)&act->next = ((Uint8)SA1_FTOI(pwp->mj.nframe) & 1) + 44;
            } else {
                *(Sint16 *)&act->next = n;
                func_8C0BFCA2(twp, 0x2DE);
            }
        } else if (pwp->mj.reqaction == 44 || pwp->mj.reqaction == 45) {
            *(Sint16 *)&act->next = pwp->mj.reqaction ^ 1;
            func_8C0BFCA2(twp, 0x2DE);
        } else {
            *(Sint16 *)&act->next = ((Uint8)SA1_FTOI(pwp->mj.nframe) & 1) + 44;
        }
    } else {
        *(Sint16 *)&act->next = 0;
    }
}
