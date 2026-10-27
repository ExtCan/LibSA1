#include "common.h"
#include "sa1/sa1.h"

/*
 * 8C06C880: classifies the object a player is carrying or touching.
 * Called by the character mains, PSetPosition and many player mode
 * functions with the player's taskwk.
 *
 * - twp->flag & 0x800 (holding an object): looks at the held task
 *   (playerpwp[pno]->htp), which must have a motionwk, a taskwk and a
 *   colliwk; otherwise 0.
 * - otherwise: the player's colliwk must have flag bits 2 and 8 set, the
 *   player must not have twp->flag & 4, and the task hit (cwp->hit_cwp)
 *   must have twp->flag & 0x40 and not 0x1000; otherwise 0.
 *
 * The result depends on the character (counter.b[1]) and on the object's
 * taskwk.id (4..7): 0x200, 0x400, 0x600, 0x800 or 0. Eggman and Tikal
 * always get 0.
 *
 * Matching notes: the playerwk pointer is assigned inside the expression
 * (SHC then keeps it and pwp->htp in stack slots, as in the original);
 * the null checks are separate statements; each branch has its own
 * Uint8 id (a stack slot each); the 0x1000 test goes through a Sint16
 * local (EXTS.W into R6).
 */
Sint32 func_8C06C880(taskwk *twp)
{
    Sint32 ret;
    Sint16 flag;
    playerwk *pwp;
    motionwk *mwp;
    taskwk *otwp;
    colliwk *cwp;

    ret = 0;
    if (twp->flag & 0x800) {
        Uint8 id;

        otwp = (pwp = playerpwp[(Uint8)twp->counter.b[0]])->htp->twp;
        mwp = pwp->htp->mwp;
        if (mwp == NULL) {
            return 0;
        }
        if (otwp == NULL) {
            return 0;
        }
        if (otwp->cwp == NULL) {
            return 0;
        }
        id = otwp->id;
        switch ((Uint8)twp->counter.b[1]) {
        case SA1_CHR_SONIC:
        case SA1_CHR_TAILS:
        case SA1_CHR_KNUCKLES:
        case SA1_CHR_AMY:
        case SA1_CHR_E102:
            if (id == 4) {
                ret = 0x200;
            } else if (id == 5) {
                ret = 0x400;
            } else if (id == 6) {
                ret = 0;
            } else if (id == 7) {
                ret = 0x800;
            }
            break;
        case SA1_CHR_BIG:
            if (id == 4) {
                ret = 0x200;
            } else if (id == 5) {
                ret = 0x600;
            } else if (id == 6) {
                ret = 0x600;
            } else if (id == 7) {
                ret = 0x800;
            }
            break;
        }
    } else {
        Uint8 id;

        if ((CWP(twp)->flag & 2) && !(twp->flag & 4) && (CWP(twp)->flag & 8)) {
            cwp = CWP(twp)->hit_cwp;
            if (cwp != NULL && (cwp->mytask->twp->flag & 0x40)) {
                otwp = cwp->mytask->twp;
                id = otwp->id;
                flag = otwp->flag;
                if (flag & 0x1000) {
                    return 0;
                }
                switch ((Uint8)twp->counter.b[1]) {
                case SA1_CHR_SONIC:
                case SA1_CHR_TAILS:
                case SA1_CHR_KNUCKLES:
                case SA1_CHR_AMY:
                case SA1_CHR_E102:
                    if (id == 4) {
                        ret = 0x200;
                    } else if (id == 5) {
                        ret = 0x400;
                    } else if (id == 6) {
                        ret = 0;
                    } else if (id == 7) {
                        ret = 0x800;
                    }
                    break;
                case SA1_CHR_BIG:
                    if (id == 4) {
                        ret = 0x200;
                    } else if (id == 5) {
                        ret = 0x600;
                    } else if (id == 6) {
                        ret = 0x600;
                    } else if (id == 7) {
                        ret = 0x800;
                    }
                    break;
                }
            }
        }
    }
    return ret;
}
