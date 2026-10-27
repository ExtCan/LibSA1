#include "common.h"
#include "ninja.h"
#include "sa1/sa1.h"

/*
 * 1ST_READ 8C0B16BC-8C0B1C56: Sonic's object lift / carry / throw handlers
 * (chunk 8C0B11C0-8C0B2338, functions 7-10). All four are called by
 * SonicModeMachine (8C0A966C) with (twp, mwp, pwp) and return 1 when they
 * changed the player's mode, else 0. The object class comes from
 * func_8C06C880 (0x200, 0x400, 0x800), the angle class (1..4, 4 = straight
 * ahead) from func_8C06CF60.
 *
 *  func_8C0B16BC  action button (D_8C19E3A0) pressed while touching an
 *                 object in front: 0x400 objects are taken at once (mode 24,
 *                 action 53, the hit task becomes pwp->htp, twp->flag 0x800);
 *                 0x200 objects start the lift (mode 21, action 33, speed
 *                 capped at jog speed); 0x800 objects mode 24, action 53.
 *  func_8C0B17AE  mode 21: from frame 12 of the lift motion a 0x200 object
 *                 straight ahead is taken (mode 23, SE 0x45A, its taskwk
 *                 flag 0x1000, twp->flag 0x800); otherwise back to standing.
 *  func_8C0B184C  carrying: the action button throws (in the air: mode 33;
 *                 on the ground: mode 26, or a put-down, mode 22, when slower
 *                 than jog speed); the throw action depends on whether the
 *                 player's target list is empty (func_8C06D118). When the
 *                 object is gone (flag 0x800 clear): standing or falling
 *                 (mode 12, action 18), and the held task is released.
 *  func_8C0B199A  throw / put-down timing: at the release frame of the
 *                 throw actions (59/61 at frame 20/7: aimed at the nearest
 *                 target; 60/62 at 24/6: fixed speed (2, 0.8, 0)) the held
 *                 object gets its speed (player space -> global) and SE 0x45B;
 *                 action 63 frame 12 puts it down (func_8C07DC2A).
 *
 * Translation unit: the original object starts before 8C0B16BC (chunk
 * 8C0B11C0, code_8C0B11C0.c); SHC shares one literal pool across the
 * functions and dumps it mid-function (8C0B1774 inside 8C0B16BC holds
 * func_8C094330 from 8C0B14E0; 8C0B19E2 inside 8C0B199A). Those pool
 * positions decide far/near branch forms (BF/S+BRA at 8C0B16DE; the
 * htwp != NULL test at 8C0B1A68), so 8C0B16BC and 8C0B199A match only when
 * compiled after code_8C0B11C0.c, as in the original object:
 *   py -3.12 tools/shcdiff.py <func> src/1st_read/code_8C0B16BC.c
 *       --flags=-define=TU_EXPERIMENT,NON_MATCHING
 * (all four functions MATCH that way). 8C0B17AE and 8C0B184C match alone.
 *
 * Matching notes:
 *  - 8C0B16BC: the button test needs Sint32 operands (as 8C06CEBE); the
 *    angle class test is a switch (CMP/EQ chain on R0); kind is assigned
 *    inside the condition (TST R0,R0 with the copy in the delay slot).
 *  - 8C0B17AE: kind assigned inside the condition goes to a stack slot;
 *    the outer if needs the explicit "else return 0" for SHC's register
 *    choice of the two calls.
 *  - 8C0B184C: the not-holding path must be an "else if" of the button
 *    test (this gives the original's address-register store of action 18).
 *  - 8C0B199A: each branch ends in its own return (no tail merging of the
 *    flag/htp/SE epilogues); z is a separate local read back from v.z,
 *    which gives the FR13/FR14/FR15 allocation.
 *  - "a >= b" is what gives SHC's !(b > a) (FCMP/GT, operand load order);
 *    the two differ only for NaN (8C0B16BC jog-speed cap, 8C0B17AE frame).
 */

/* Target list entry (8 bytes; lists B_8C73EE38 for player 0 and B_8C742288
 * for the others, terminated by a NULL twp; nearest = smallest dist).
 * Read by 8C06D118 and 8C0B199A. The shared type is sa1_PL_TARGET
 * (sa1/player.h; canonical name PL_TARGET in sa1/pending.h). */
typedef sa1_PL_TARGET SONIC_TARGET;

SA1_ASSERT_SIZE(SONIC_TARGET, 0x08);

extern SONIC_TARGET B_8C73EE38[];
extern SONIC_TARGET B_8C742288[];

/* object classification of the carried/touched object (code_8C06C880.c) */
extern Sint32 func_8C06C880(taskwk *twp);
/* turn towards the hit object and push out of it (code_8C06CA30.c) */
extern Sint32 func_8C06CB40(taskwk *twp, motionwk *mwp);
/* angle class 1..4 towards the hit object (code_8C06CCA0.c); the callers
 * here pass mwp as a second argument */
extern Sint32 func_8C06CF60(taskwk *twp, motionwk *mwp);
/* non-zero when the player's target list is not empty */
extern Sint32 func_8C06D118(Uint8 pno);
/* held-object push-out (code_8C07DC2A.c) */
extern void func_8C07DC2A(taskwk *twp, taskwk *htwp, playerwk *pwp);
/* back to standing (code_8C0A9660.c) */
extern void func_8C0A9660(taskwk *twp, motionwk *mwp, playerwk *pwp);

#ifdef TU_EXPERIMENT
#include "1st_read/code_8C0B11C0.c"
#endif

#ifdef NON_MATCHING
/* Matches only in the original translation unit (TU_EXPERIMENT, see above). */
Sint32 func_8C0B16BC(taskwk *twp, motionwk *mwp, playerwk *pwp)
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
                    twp->mode = 24;
                    pwp->mj.reqaction = 53;
                    pwp->htp = CWP(twp)->hit_cwp->mytask;
                    twp->flag |= 0x800;
                } else if (kind & 0x200) {
                    twp->mode = 21;
                    pwp->mj.reqaction = 33;
                    if (pwp->spd.x >= pwp->p.jog_speed) {
                        pwp->spd.x = pwp->p.jog_speed;
                    }
                } else if (kind & 0x800) {
                    twp->mode = 24;
                    pwp->mj.reqaction = 53;
                } else {
                    break;
                }
                return 1;
            }
        }
    }
    return 0;
}
#else
ASM_STUB(Sint32 func_8C0B16BC(taskwk *twp, motionwk *mwp, playerwk *pwp))
#endif

Sint32 func_8C0B17AE(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    Sint32 kind;

    func_8C06CB40(twp, mwp);
    if (pwp->mj.nframe >= 12.0f) {
        if ((kind = func_8C06C880(twp)) != 0 && func_8C06CF60(twp, mwp) == 4) {
            if (kind & 0x200) {
                twp->mode = 23;
                func_8C0940DA(0x45A, 0, 0, 0);
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
    func_8C0A9660(twp, mwp, pwp);
    return 0;
}

Sint32 func_8C0B184C(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    if (B_8C754C08[(Uint8)twp->counter.b[0]].press & D_8C19E3A0) {
        if (!(twp->flag & 3)) {
            twp->mode = 33;
            if (func_8C06D118(twp->counter.b[0])) {
                pwp->mj.reqaction = 61;
            } else {
                pwp->mj.reqaction = 62;
            }
        } else if (pwp->spd.x < pwp->p.jog_speed) {
            twp->mode = 22;
            pwp->mj.reqaction = 63;
            PClearSpeed(mwp, pwp);
        } else {
            twp->mode = 26;
            if (func_8C06D118(twp->counter.b[0])) {
                pwp->mj.reqaction = 59;
            } else {
                pwp->mj.reqaction = 60;
            }
        }
        return 1;
    } else if (!(twp->flag & 0x800)) {
        if (twp->flag & 3) {
            func_8C0A9660(twp, mwp, pwp);
        } else {
            twp->mode = 12;
            pwp->mj.reqaction = 18;
        }
        if (pwp->htp) {
            pwp->htp->twp->flag &= ~0x1000;
            pwp->htp = NULL;
        }
        return 1;
    }
    return 0;
}

#ifdef NON_MATCHING
/* Matches only in the original translation unit (TU_EXPERIMENT, see above). */
Sint32 func_8C0B199A(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_VECTOR v;
    SONIC_TARGET *list;
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
    if (((pwp->mj.reqaction == 59 && pwp->mj.nframe == 20.0f)
            || (pwp->mj.reqaction == 61 && pwp->mj.nframe == 7.0f))
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
            v.x = njSqrt(SA1_FMAC(v.x, v.x, z * z)); /* FMUL z*z, FMAC x*x */
            v.z = 0.0f;
            njUnitVector(&v);
            v.x *= 3.0f;
            v.y *= 3.0f;
            PConvertVector_P2G(twp, &v);
            pwp->htp->mwp->spd = v;
        }
        twp->flag &= ~0x800;
        pwp->htp = NULL;
        func_8C0940DA(0x45B, 0, 0, 0);
        return 1;
    }
    if ((pwp->mj.reqaction == 60 && pwp->mj.nframe == 24.0f)
            || (pwp->mj.reqaction == 62 && pwp->mj.nframe == 6.0f)) {
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
        func_8C0940DA(0x45B, 0, 0, 0);
        return 1;
    }
    if (pwp->mj.reqaction == 63 && pwp->mj.nframe == 12.0f) {
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
#else
ASM_STUB(Sint32 func_8C0B199A(taskwk *twp, motionwk *mwp, playerwk *pwp))
#endif
