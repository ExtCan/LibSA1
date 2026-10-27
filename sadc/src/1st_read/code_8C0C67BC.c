#include "common.h"
#include "ninja.h"
#include "sa1/sa1.h"
#include "sa1/pending.h"

/*
 * 1ST_READ 8C0C67BC-8C0C6D36: Knuckles' object lift / carry / throw
 * handlers (chunk 8C0C5680-8C0C71A0, functions 10-13 of 18), the Knuckles
 * copies of Sonic's 8C0B16BC/8C0B17AE/8C0B184C/8C0B199A (code_8C0B16BC.c).
 * All four are called by KnucklesModeMachine (8C0C193A) with
 * (twp, mwp, pwp) and return 1 when they changed the player's mode, else 0.
 *
 *  func_8C0C67BC  action button (D_8C19E3A0) pressed while touching an
 *                 object in front (angle class 1..4): 0x400 objects are
 *                 taken at once (mode 30, action 90, htp, flag 0x800);
 *                 0x200 objects start the lift (mode 27, action 70, speed
 *                 capped at jog speed); 0x800 objects mode 30, action 90.
 *  func_8C0C68A0  lift: from frame 12 a 0x200 object straight ahead is
 *                 taken (mode 29, SE 0x47B, its flag 0x1000, flag 0x800);
 *                 otherwise back to standing (func_8C0C4F40).
 *  func_8C0C693E  carrying: the action button throws (in the air: mode 39,
 *                 action 98/99; on the ground mode 32, action 96/97, or a
 *                 put-down, mode 28 action 100, when slower than jog
 *                 speed); the throw action depends on whether the target
 *                 list is empty (func_8C06D118). When the object is gone:
 *                 standing or falling (mode 9, action 19), htp released.
 *  func_8C0C6A7A  release frames: actions 96/98 at frame 20/7 aim at the
 *                 nearest target, 97/99 at 24/6 use the fixed speed
 *                 (2, 0.8, 0) (both SE 0x47C); action 100 frame 12 puts the
 *                 object down (func_8C07DC2A).
 *
 * Literal pool layout. The original translation unit is the whole chunk
 * 8C0C5680-8C0C71A0; func_8C0C67BC follows func_8C0C61F4 directly (no pool
 * in between). There SHC dumps the pool right after func_8C0C67BC and in
 * the middle of func_8C0C693E (8C0C69D4), so func_8C0C693E's call of
 * func_8C0C4F40 is still pending in func_8C0C6A7A and forces its mid-function
 * pool at 8C0C6AC2, which keeps the three branches to 8C0C6C12 near (BF).
 * Compiled with nothing in front, the first pool goes after func_8C0C68A0
 * and func_8C0C6A7A gets BT/BRA/NOP far branches instead. The ASM_STUBs of
 * the two preceding functions (8C0C5BCA, 8C0C61F4) recreate the original
 * layout: one to three stubs work, four do not; two is the middle. The
 * splicer ignores the stub bodies and keeps the original pools.
 *
 * Matching notes: the button test of 8C0C67BC needs Sint32 operands (as
 * Sonic's 8C0B16BC); 8C0C693E's matches with or without them. The source
 * shapes are those of the Sonic/Amy copies (code_8C0B16BC.c,
 * code_8C0DC1E2.c); in 8C0C6A7A z is a separate local read back from v.z
 * (FR13/FR14/FR15 allocation) and x*x + z*z is the original's single FMAC.
 */

/* angle class 1..4 towards the hit object (code_8C06CCA0.c); the callers
 * pass mwp as a second argument */
extern Sint32 func_8C06CF60(taskwk *twp, motionwk *mwp);
/* Knuckles: mode 1 and the stand selector (code_8C0C4C2E.c) */
extern void func_8C0C4F40(taskwk *twp, motionwk *mwp, playerwk *pwp);

/* pool-layout stubs (see the file comment); the splicer ignores their bodies */
ASM_STUB(Sint32 func_8C0C5BCA(taskwk *twp, motionwk *mwp, playerwk *pwp))
ASM_STUB(Sint32 func_8C0C61F4(taskwk *twp, motionwk *mwp, playerwk *pwp))

Sint32 func_8C0C67BC(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    Sint32 kind;

    if ((Sint32)B_8C754C08[(Uint8)twp->counter.b[0]].press & (Sint32)D_8C19E3A0) {
        if ((kind = func_8C06C880(twp)) != 0) {
            switch (func_8C06CF60(twp, mwp)) {
            case 4:
            case 3:
            case 2:
            case 1:
                if (kind & 0x400) {
                    twp->mode = 30;
                    pwp->mj.reqaction = 90;
                    pwp->htp = CWP(twp)->hit_cwp->mytask;
                    twp->flag |= 0x800;
                } else if (kind & 0x200) {
                    twp->mode = 27;
                    pwp->mj.reqaction = 70;
                    if (pwp->spd.x >= pwp->p.jog_speed) {
                        pwp->spd.x = pwp->p.jog_speed;
                    }
                } else if (kind & 0x800) {
                    twp->mode = 30;
                    pwp->mj.reqaction = 90;
                } else {
                    break;
                }
                return 1;
            }
        }
    }
    return 0;
}

Sint32 func_8C0C68A0(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    Sint32 kind;

    func_8C06CB40(twp, mwp);
    if (pwp->mj.nframe >= 12.0f) {
        if ((kind = func_8C06C880(twp)) != 0 && func_8C06CF60(twp, mwp) == 4) {
            if (kind & 0x200) {
                twp->mode = 29;
                func_8C0940DA(0x47B, 0, 0, 0);
                pwp->htp = CWP(twp)->hit_cwp->mytask;
                pwp->htp->twp->flag |= 0x1000;
                twp->flag |= 0x800;
                twp->flag &= ~8;
                return 1;
            }
        }
    } else {
        return 0;
    }
    func_8C0C4F40(twp, mwp, pwp);
    return 0;
}

Sint32 func_8C0C693E(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    if (B_8C754C08[(Uint8)twp->counter.b[0]].press & D_8C19E3A0) {
        if (!(twp->flag & 3)) {
            twp->mode = 39;
            if (func_8C06D118(twp->counter.b[0])) {
                pwp->mj.reqaction = 98;
            } else {
                pwp->mj.reqaction = 99;
            }
        } else if (pwp->spd.x < pwp->p.jog_speed) {
            twp->mode = 28;
            pwp->mj.reqaction = 100;
            PClearSpeed(mwp, pwp);
        } else {
            twp->mode = 32;
            if (func_8C06D118(twp->counter.b[0])) {
                pwp->mj.reqaction = 96;
            } else {
                pwp->mj.reqaction = 97;
            }
        }
        return 1;
    } else if (!(twp->flag & 0x800)) {
        if (twp->flag & 3) {
            func_8C0C4F40(twp, mwp, pwp);
        } else {
            twp->mode = 9;
            pwp->mj.reqaction = 19;
        }
        if (pwp->htp) {
            pwp->htp->twp->flag &= ~0x1000;
            pwp->htp = NULL;
        }
        return 1;
    }
    return 0;
}

Sint32 func_8C0C6A7A(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_VECTOR v;
    PL_TARGET *list;
    taskwk *best;
    taskwk *htwp;
    CCL_INFO *info;
    Float min;
    Float z;

    if (twp->counter.b[0] == 0) {
        list = B_8C73EE38;
    } else {
        list = B_8C742288;
    }
    if (pwp->htp == NULL) {
        if (!(twp->flag & 0x800)) {
            return 0;
        }
        twp->flag &= ~0x800;
        return 1;
    }
    if (((pwp->mj.reqaction == 96 && pwp->mj.nframe == 20.0f)
            || (pwp->mj.reqaction == 98 && pwp->mj.nframe == 7.0f))
        && list->twp != NULL) {
        htwp = pwp->htp->twp;
        if (htwp != NULL) {
            htwp->flag &= ~0x1000;
            best = list->twp;
            min = list->dist;
            while (list->twp != NULL) {
                if (min > list->dist) {
                    min = list->dist;
                    best = list->twp;
                }
                list++;
            }
            info = CWP(best)->info;
            v.x = best->pos.x + info->center.x - twp->pos.x;
            v.y = best->pos.y + info->center.y - twp->pos.y;
            v.z = best->pos.z + info->center.z - twp->pos.z;
            z = v.z;
            v.x = njSqrt(SA1_FMAC(v.x, v.x, z * z));
            v.z = 0.0f;
            njUnitVector(&v);
            v.x *= 3.0f;
            v.y *= 3.0f;
            PConvertVector_P2G(twp, &v);
            pwp->htp->mwp->spd = v;
        }
        twp->flag &= ~0x800;
        pwp->htp = NULL;
        func_8C0940DA(0x47C, 0, 0, 0);
        return 1;
    }
    if ((pwp->mj.reqaction == 97 && pwp->mj.nframe == 24.0f)
            || (pwp->mj.reqaction == 99 && pwp->mj.nframe == 6.0f)) {
        htwp = pwp->htp->twp;
        if (htwp != NULL) {
            htwp->flag &= ~0x1000;
            v.x = 2.0f;
            v.y = 0.799999957f;
            v.z = 0.0f;
            PConvertVector_P2G(twp, &v);
            pwp->htp->mwp->spd = v;
        }
        twp->flag &= ~0x800;
        pwp->htp = NULL;
        func_8C0940DA(0x47C, 0, 0, 0);
        return 1;
    }
    if (pwp->mj.reqaction == 100 && pwp->mj.nframe == 12.0f) {
        htwp = pwp->htp->twp;
        if (htwp != NULL) {
            htwp->flag &= ~0x1000;
            func_8C07DC2A(twp, htwp, pwp);
        }
        twp->flag &= ~0x800;
        pwp->htp = NULL;
        return 1;
    }
    return 0;
}
