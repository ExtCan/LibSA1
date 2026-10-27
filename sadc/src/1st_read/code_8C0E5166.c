#include "sa1/pending.h"

/*
 * Big (1ST_READ 8C0E5166-8C0E5698, functions 25-27 of chunk 8C0DFFA0):
 * carry / throw handlers called by BigModeMachine (8C0E0E70) with
 * (twp, mwp, pwp); they return 1 when they changed the player's mode.
 * Big's versions of E-102's 8C0D11DC/8C0D14D6/8C0D15C0 (code_8C0D10BC.c).
 *  func_8C0E5166  throw timing: at the release frame of the throw actions
 *                 (41/43 at frame 12/7: aimed at the nearest target of the
 *                 player's target list, speed 4; 42/44 at 16/6: fixed
 *                 player-space speed (3, 3, 0)) the held object
 *                 (pwp->htp) gets its speed (rotated by the player's
 *                 angles) and SE 0x4B6; actions 80/45 at frame 8 only let
 *                 it go. When nothing is held it only clears TWF_HOLD.
 *  func_8C0E54E4  action button (D_8C19E3A0) held: pushing against an
 *                 object (class from func_8C06CFCC; 0 = nothing to push,
 *                 returns 0): 0x800 objects mode 28/action 34, straight
 *                 ahead (4) mode 10/action 18 and SE 0x4B7, else mode
 *                 26/action 33. Button not held: 0x200 objects mode
 *                 15/action 26, otherwise drop the held object and stand
 *                 (8C0E65BE).
 *  func_8C0E55E2  drop the held object (TWF_HOLD): clears its TWF_REQUEST,
 *                 throws a 0x200-class object with the player-space speed
 *                 (1, 1, 0) and clears pwp->htp.
 */

/* stick direction vs. CCL push-out class (src/1st_read/code_8C06CFCC.c
 * defines it with (taskwk *) only; the game passes mwp in R5) */
Sint32 func_8C06CFCC(taskwk *twp, motionwk *mwp);

/* Big: back to standing (8C0E65BE, other chunk; also declared in
 * code_8C0E4E60.c) */
void func_8C0E65BE(taskwk *twp, motionwk *mwp, playerwk *pwp);

void func_8C0E55E2(taskwk *twp, motionwk *mwp, playerwk *pwp);

#if !defined(TARGET_PC)
/*
 * Translation-unit layout model (SHC only; never spliced: these names have
 * no address in the image, as in code_8C0D10BC.c). The original object
 * shares one literal pool across its functions: when func_8C0E5166 starts,
 * the func_8C0E65BE entry of func_8C0E501C (first use 8C0E511C) is
 * pending, so SHC dumps the pool at 8C0E51AE (after the first BRA), then
 * at 8C0E5404 and, inside func_8C0E54E4, at 8C0E5584 (with the 8.0f of
 * func_8C0E5166) and 8C0E55B8. Without that state the pools move and the
 * far branch forms differ. The models are copies of func_8C0E4E60,
 * func_8C0E4F28 and func_8C0E501C (all matched in code_8C0E4E60.c):
 * func_8C0E4E60 starts on a 32-byte boundary (8C0E4E60 - 8C0DFFA0 =
 * 0x4EC0) with nothing pending (the pool at 8C0E4E40 holds everything),
 * so the three copies reproduce the original pool state from there.
 */
Sint32 func_8C06CF60(taskwk *twp, motionwk *mwp);

Sint32 tu_model_8C0E4E60(taskwk *twp, motionwk *mwp, playerwk *pwp)
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
                    twp->mode = 18;
                    pwp->mj.reqaction = 35;
                    pwp->htp = CWP(twp)->hit_cwp->mytask;
                    twp->flag |= 0x800;
                } else if (kind & 0x200) {
                    twp->mode = 15;
                    if (CWP(twp)->hit_cwp->mytask->twp->id == 4) {
                        pwp->mj.reqaction = 78;
                    } else {
                        pwp->mj.reqaction = 26;
                    }
                    return 1;
                } else if (kind & 0x800) {
                    twp->mode = 18;
                    pwp->mj.reqaction = 35;
                } else {
                    break;
                }
                return 1;
            }
        }
    }
    return 0;
}

Sint32 tu_model_8C0E4F28(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_POINT3 v;
    Sint32 kind;

    if (func_8C06CB40(twp, mwp) == 0) {
        func_8C0E65BE(twp, mwp, pwp);
        pwp->htp = NULL;
        twp->flag &= ~0x800;
        return 0;
    }
    if (pwp->mj.nframe >= 8.0f) {
        if ((kind = func_8C06C880(twp)) != 0 && func_8C06CF60(twp, mwp) == 4) {
            if (kind & 0x200) {
                twp->mode = 17;
                func_8C0940DA(0x4B4, 0, 0, 0);
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
    func_8C0E65BE(twp, mwp, pwp);
    pwp->htp = NULL;
    twp->flag &= ~0x800;
    return 0;
}

Sint32 tu_model_8C0E501C(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    if (B_8C754C08[(Uint8)twp->counter.b[0]].press & D_8C19E3A0) {
        if (!(twp->flag & 3)) {
            twp->mode = 27;
            if (func_8C06D118(twp->counter.b[0])) {
                pwp->mj.reqaction = 43;
            } else {
                pwp->mj.reqaction = 44;
            }
        } else if (pwp->spd.x < pwp->p.jog_speed) {
            twp->mode = 16;
            if (pwp->htp->twp->id == 4) {
                pwp->mj.reqaction = 80;
            } else {
                pwp->mj.reqaction = 45;
            }
            PClearSpeed(mwp, pwp);
        } else {
            twp->mode = 20;
            if (func_8C06D118(twp->counter.b[0])) {
                pwp->mj.reqaction = 41;
            } else {
                pwp->mj.reqaction = 42;
            }
        }
        return 1;
    } else if (!(twp->flag & 0x800)) {
        if (twp->flag & 3) {
            func_8C0E65BE(twp, mwp, pwp);
        } else {
            twp->mode = 8;
            pwp->mj.reqaction = 14;
        }
        if (pwp->htp) {
            pwp->htp->twp->flag &= ~0x1000;
            pwp->htp = NULL;
        }
        return 1;
    }
    return 0;
}
#endif

/*
 * Same shape as E-102's 8C0D11DC (code_8C0D10BC.c), with the player-space
 * rotation written out (njPushMatrix, njRotateZ/X/Y and njCalcVector
 * straight into the held task's mwp->spd) instead of PConvertVector_P2G.
 * The 0.0f stored to v.z is a local assigned after the htp == NULL test
 * (FR15), z is v.z read back for the squared length (one FMAC), the loop
 * stores min before best, and the rotations use the
 * "ang ? njRotateX(NULL, ang) : (void)0" form (angle kept in the stack
 * slot @0, as in 8C06CCA0 and 8C0D15C0).
 */
Sint32 func_8C0E5166(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_VECTOR v;
    Float zero;
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
        if (!(twp->flag & TWF_HOLD)) {
            return 0;
        }
        twp->flag &= ~TWF_HOLD;
        return 1;
    }
    zero = 0.0f;
    if (((pwp->mj.reqaction == 41 && pwp->mj.nframe == 12.0f)
            || (pwp->mj.reqaction == 43 && pwp->mj.nframe == 7.0f))
        && list->twp != NULL) {
        htwp = pwp->htp->twp;
        if (htwp != NULL) {
            htwp->flag &= ~TWF_REQUEST;
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
            v.z = zero;
            njUnitVector(&v);
            v.x *= 4.0f;
            v.y *= 4.0f;
            njPushMatrix(D_8C67C640);
            twp->ang.z ? njRotateZ(NULL, twp->ang.z) : (void)0;
            twp->ang.x ? njRotateX(NULL, twp->ang.x) : (void)0;
            -twp->ang.y ? njRotateY(NULL, -twp->ang.y) : (void)0;
            njCalcVector(NULL, &v, &pwp->htp->mwp->spd);
            njPopMatrix(1);
        }
        twp->flag &= ~TWF_HOLD;
        pwp->htp = NULL;
        func_8C0940DA(0x4B6, 0, 0, 0);
        return 1;
    }
    if ((pwp->mj.reqaction == 42 && pwp->mj.nframe == 16.0f)
            || (pwp->mj.reqaction == 44 && pwp->mj.nframe == 6.0f)) {
        htwp = pwp->htp->twp;
        if (htwp != NULL) {
            htwp->flag &= ~TWF_REQUEST;
            v.x = 3.0f;
            v.y = 3.0f;
            v.z = zero;
            njPushMatrix(D_8C67C640);
            twp->ang.z ? njRotateZ(NULL, twp->ang.z) : (void)0;
            twp->ang.x ? njRotateX(NULL, twp->ang.x) : (void)0;
            -twp->ang.y ? njRotateY(NULL, -twp->ang.y) : (void)0;
            njCalcVector(NULL, &v, &pwp->htp->mwp->spd);
            njPopMatrix(1);
        }
        twp->flag &= ~TWF_HOLD;
        pwp->htp = NULL;
        func_8C0940DA(0x4B6, 0, 0, 0);
        return 1;
    }
    if (pwp->mj.reqaction == 80 && pwp->mj.nframe == 8.0f) {
        htwp = pwp->htp->twp;
        if (htwp != NULL) {
            htwp->flag &= ~TWF_REQUEST;
        }
        twp->flag &= ~TWF_HOLD;
        pwp->htp = NULL;
        return 1;
    }
    if (pwp->mj.reqaction == 45 && pwp->mj.nframe == 8.0f) {
        htwp = pwp->htp->twp;
        if (htwp != NULL) {
            htwp->flag &= ~TWF_REQUEST;
        }
        twp->flag &= ~TWF_HOLD;
        pwp->htp = NULL;
        return 1;
    }
    return 0;
}

/*
 * The push block (after the 8C06CFCC call) keeps the 8C06C880 / 8C0940DA
 * addresses and the mode constants out of R2 (the 0x800 mask is in R2)
 * only with the "nothing to push" exit written as a goto to a final
 * "return 0" and the not-held path as the fall-through after the held
 * block's "return 1". With "if (res != 0) {...}" and "return 0" last (the
 * form of Sonic's 8C0B1C56, Tails' 8C0BD336, Knuckles' 8C0C6D36, E-102's
 * 8C0D14D6 and Amy's 8C0DC662, all NON_MATCHING for this reason) the push
 * block gets R2 (13 registers differ); the goto with the not-held path in
 * an else block puts its 8C06C880 address in R1 (2 differ). The res == 4
 * test is a switch (as in 8C0B1C56): an if/else merges the reqaction
 * stores of the 0x800 and default blocks.
 */
Sint32 func_8C0E54E4(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    Sint32 res;

    if ((Sint32)B_8C754C08[(Uint8)twp->counter.b[0]].on & (Sint32)D_8C19E3A0) {
        res = func_8C06CFCC(twp, mwp);
        if (res == 0) {
            goto none;
        }
        if (func_8C06C880(twp) & 0x800) {
            twp->mode = 28;
            pwp->mj.reqaction = 34;
        } else {
            switch (res) {
            case 4:
                twp->mode = 10;
                pwp->mj.reqaction = 18;
                func_8C0940DA(0x4B7, 0, 0, 0);
                break;
            default:
                twp->mode = 26;
                pwp->mj.reqaction = 33;
                break;
            }
        }
        return 1;
    }
    if (func_8C06C880(twp) & 0x200) {
        twp->mode = 15;
        pwp->mj.reqaction = 26;
    } else {
        func_8C0E55E2(twp, mwp, pwp);
        func_8C0E65BE(twp, mwp, pwp);
    }
    return 1;
none:
    return 0;
}

void func_8C0E55E2(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_POINT3 v;
    taskwk *htwp;

    if (twp->flag & TWF_HOLD) {
        htwp = pwp->htp->twp;
        if (htwp != NULL) {
            htwp->flag &= ~TWF_REQUEST;
        }
        twp->flag &= ~TWF_HOLD;
        if (func_8C06C880(twp) & 0x200) {
            v.x = 1.0f;
            v.y = 1.0f;
            v.z = 0.0f;
            njPushMatrix(D_8C67C640);
            twp->ang.z ? njRotateZ(NULL, twp->ang.z) : (void)0;
            twp->ang.x ? njRotateX(NULL, twp->ang.x) : (void)0;
            -twp->ang.y ? njRotateY(NULL, -twp->ang.y) : (void)0;
            njCalcVector(NULL, &v, &pwp->htp->mwp->spd);
            njPopMatrix(1);
        }
        pwp->htp = NULL;
    }
}
