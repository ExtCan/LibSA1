#include "common.h"
#include "ninja.h"
#include "sa1/sa1.h"

/*
 * 1ST_READ 8C0B1C56-8C0B2170: Sonic's held-object code (chunk
 * 8C0B11C0-8C0B2338, functions 11-16). pwp->htp is the task of the object
 * Sonic carries (twp->flag & 0x800 while held). All are called with
 * (twp, mwp, pwp).
 *
 *  func_8C0B1C56  SonicModeMachine check: with the D_8C19E3A0 button held,
 *                 pushing against an object (8C06CFCC) starts the push
 *                 (modes 34/15/32); otherwise a 0x200-class object is
 *                 picked up (mode 21, action 33, speed capped at jog_speed)
 *                 or the held object is dropped and Sonic stands. Returns
 *                 0 when the button is held but there is nothing to push.
 *  func_8C0B1D60  drop the held object: clears the flags, throws a
 *                 0x200-class object with the player-space speed (1, 1, 0)
 *                 and clears pwp->htp.
 *  func_8C0B1DE0  per-frame update of the held object (SonicMain): keeps a
 *                 0x200-class object at the hands (rotated by the player's
 *                 angles), drops anything else when the player hits
 *                 another object.
 *  func_8C0B1FD8  puts Sonic at the held object's radius (30 without a
 *                 motionwk) from it, opposite to his facing direction
 *                 (SonicMain modes 51-54).
 *  func_8C0B2022  angle step of SonicMain modes 53/54 (circling the object):
 *                 0x10000 / (rad * 10.47197) (0x100 without a radius),
 *                 ramped in over motion frames 4..14 and out over 18..28,
 *                 0 outside 4..28.
 *  func_8C0B2120  same placement as 8C0B1FD8 at a fixed distance of 4;
 *                 counts pwp->free.w[3] and sets mwp->ang_aim.y to
 *                 0x8000 - twp->ang.y (SonicMain modes 55-60).
 */

/* object class of the carried / touched object (code_8C06C880.c) */
Sint32 func_8C06C880(taskwk *twp);
/* stick vs. push-out direction class of the CCL hit, 1..4 (code_8C06CFCC.c
 * defines it with one parameter; the call site loads R5 = mwp, as in
 * code_8C06EFCE.c) */
Sint32 func_8C06CFCC(taskwk *twp, motionwk *mwp);
/* back to standing (code_8C0A9660.c) */
extern void func_8C0A9660(taskwk *twp, motionwk *mwp, playerwk *pwp);

extern NJS_MATRIX D_8C67C640;   /* unit matrix */

void func_8C0B1D60(taskwk *twp, motionwk *mwp, playerwk *pwp);

#ifdef NON_MATCHING
/*
 * NON_MATCHING (complete, functionally faithful): control flow, branch
 * layout, stack use and every instruction except temporary registers are
 * the original's. In the push block (after the 8C06CFCC call) the original
 * puts the 8C06C880 / 8C0940DA addresses and the 49/15 constants in R3 and
 * the 0x800 mask in R2; this compiles with R2 and R3 exchanged (13 of 108
 * instructions). Found on the way: the D_8C19E3A0 test needs signed
 * operands (Uint32 & Uint32 shifts the first call address to R1, as noted in
 * code_8C06C300.c), the res == 4 test is a switch (an if/else schedules the
 * mode/reqaction stores differently), and the "nothing to push" return 0 is
 * the last statement.
 */
Sint32 func_8C0B1C56(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    Sint32 res;

    if ((Sint32)B_8C754C08[(Uint8)twp->counter.b[0]].on & (Sint32)D_8C19E3A0) {
        res = func_8C06CFCC(twp, mwp);
        if (res != 0) {
            if (func_8C06C880(twp) & 0x800) {
                twp->mode = 34;
                pwp->mj.reqaction = 49;
            } else {
                switch (res) {
                case 4:
                    twp->mode = 15;
                    pwp->mj.reqaction = 22;
                    func_8C0940DA(0x45C, 0, 0, 0);
                    break;
                default:
                    twp->mode = 32;
                    pwp->mj.reqaction = 48;
                    break;
                }
            }
            return 1;
        }
    } else {
        if (func_8C06C880(twp) & 0x200) {
            twp->mode = 21;
            pwp->mj.reqaction = 33;
            if (pwp->spd.x >= pwp->p.jog_speed) {
                pwp->spd.x = pwp->p.jog_speed;
            }
        } else {
            func_8C0B1D60(twp, mwp, pwp);
            func_8C0A9660(twp, mwp, pwp);
        }
        return 1;
    }
    return 0;
}
#else
ASM_STUB(Sint32 func_8C0B1C56(taskwk *twp, motionwk *mwp, playerwk *pwp))
#endif

void func_8C0B1D60(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_POINT3 v;
    taskwk *htwp;

    if (twp->flag & 0x800) {
        htwp = pwp->htp->twp;
        if (htwp != NULL) {
            htwp->flag &= ~0x1000;
        }
        twp->flag &= ~0x800;
        if (func_8C06C880(twp) & 0x200) {
            v.x = 1.0f;
            v.y = 1.0f;
            v.z = 0.0f;
            PConvertVector_P2G(twp, &v);
            pwp->htp->mwp->spd = v;
        }
        pwp->htp = NULL;
    }
}

#ifdef NON_MATCHING
/*
 * NON_MATCHING (complete, functionally faithful): control flow, stack frame
 * (mwp spilled to @0 and shared with the angle, info at @4, out at @8,
 * v at @20) and instruction selection are the original's; only temporary
 * registers differ (21 of 221 instructions): the hit test loads twp->cwp and
 * mytask->twp into R0 in the original (R3 here), and the 2nd/3rd rotation
 * angle and the following call addresses use R3 where this uses R2 (the
 * same unresolved pattern as func_8C028B00 in code_8C028620.c).
 * The angle goes through an unsigned local assigned inside the condition:
 * that is what makes SHC keep it in the stack slot (store in the delay slot
 * and reload before the call); a signed local stays in R5.
 */
void func_8C0B1DE0(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_POINT3 v;
    NJS_POINT3 out;
    CCL_INFO *info;
    Uint32 ang;
    taskwk *htwp;

    htwp = pwp->htp->twp;
    info = CWP(twp)->info;
    if (!(func_8C06C880(twp) & 0x200)) {
        if (CWP(twp)->hit_cwp == NULL) {
            return;
        }
        if (htwp == CWP(twp)->hit_cwp->mytask->twp) {
            return;
        }
        func_8C0B1D60(twp, mwp, pwp);
        if (pwp->htp != NULL) {
            return;
        }
        if (twp->flag & 3) {
            func_8C0A9660(twp, mwp, pwp);
        } else {
            twp->mode = 12;
            pwp->mj.reqaction = 18;
        }
        return;
    }
    htwp->pos = info->center;
    if (pwp->mj.reqaction == 59 || pwp->mj.reqaction == 60
        || pwp->mj.reqaction == 61 || pwp->mj.reqaction == 62) {
        v.x = SA1_FMAC(pwp->righthand_vec.x, 2.5f, pwp->righthand_pos.x);
        v.y = SA1_FMAC(pwp->righthand_vec.y, 2.5f, pwp->righthand_pos.y);
        v.z = SA1_FMAC(pwp->righthand_vec.z, 2.5f, pwp->righthand_pos.z);
    } else {
        v.x = (pwp->righthand_pos.x + pwp->lefthand_pos.x) * 0.5f;
        v.y = (pwp->righthand_pos.y + pwp->lefthand_pos.y) * 0.5f;
        v.z = (pwp->righthand_pos.z + pwp->lefthand_pos.z) * 0.5f;
    }
    v.x -= CWP(htwp)->info->center.x;
    v.y -= CWP(htwp)->info->center.y;
    v.z -= CWP(htwp)->info->center.z;
    njPushMatrix(D_8C67C640);
    if ((ang = (Uint32)twp->ang.z) != 0) {
        njRotateZ(NULL, (Angle)ang);
    }
    if ((ang = (Uint32)twp->ang.x) != 0) {
        njRotateX(NULL, (Angle)ang);
    }
    if ((ang = (Uint32)(0x8000 - twp->ang.y)) != 0) {
        njRotateY(NULL, (Angle)ang);
    }
    njCalcVector(NULL, &v, &out);
    njPopMatrix(1);
    njAddVector(&htwp->pos, &out);
}
#else
ASM_STUB(void func_8C0B1DE0(taskwk *twp, motionwk *mwp, playerwk *pwp))
#endif

void func_8C0B1FD8(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    taskwk *htwp;
    motionwk *hmwp;
    Float r;

    htwp = pwp->htp->twp;
    hmwp = pwp->htp->mwp;
    if (hmwp == NULL) {
        r = 30.0f;
    } else {
        r = hmwp->rad;
    }
    twp->pos.x = htwp->pos.x - njCos(twp->ang.y) * r;
    twp->pos.z = htwp->pos.z - njSin(twp->ang.y) * r;
}

Angle func_8C0B2022(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    motionwk *hmwp;
    Float frame;
    Angle spd;

    hmwp = pwp->htp->mwp;
    frame = pwp->mj.nframe;
    if (hmwp == NULL || hmwp->rad == 0.0f) {
        spd = 0x100;
    } else {
        spd = SA1_FTOI(65536.0f / (hmwp->rad * 10.47197f));
    }
    if (frame >= 28.0f || frame <= 4.0f) {
        spd = 0;
    } else if (frame >= 18.0f) {
        spd = SA1_FTOI((28.0f - frame) * (Float)spd * 0.099999996f);
    } else if (frame <= 14.0f) {
        spd = SA1_FTOI((frame - 4.0f) * (Float)spd * 0.099999996f);
    }
    return spd;
}

void func_8C0B2120(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    taskwk *htwp;

    htwp = pwp->htp->twp;
    pwp->free.w[3]++;
    mwp->ang_aim.y = 0x8000 - twp->ang.y;
    twp->pos.x = htwp->pos.x - njCos(twp->ang.y) * 4.0f;
    twp->pos.z = htwp->pos.z - njSin(twp->ang.y) * 4.0f;
}
