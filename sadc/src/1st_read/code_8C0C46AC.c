#include "common.h"
#include "ninja.h"
#include "sa1/sa1.h"

/*
 * 1ST_READ 8C0C46AC-8C0C4C2E (chunk 8C0C0640-8C0C5220, function 5 of 9):
 * Knuckles' display routine (tp->disp, set by KnucklesMain). Same frame as
 * Sonic's func_8C0AE17A (code_8C0AE0E0.c) and Tails' func_8C0BA820
 * (code_8C0BA820.c):
 *  - the body is translated to the point p.height / 2 above twp->pos along
 *    the player's up axis (in modes 19/20, climbing/gliding, also pushed out
 *    along the wall normal cstsp->normal2 by twp->value.f), which is also
 *    written to collision shape 0's centre,
 *  - action 51: the body is turned towards mwp->ang_aim.y and tilted by
 *    pwp->free.w[3] (AdjustAngle towards the yaw difference, 0x800/frame),
 *  - action 52: the tilt pwp->free.w[3] fades out over the action and the
 *    body is rolled by acos(cstsp->normal2.y),
 *  - spin ball (action 14 on the ground) is squashed like Sonic's; it and
 *    action 13 are drawn twice more as after-images 2 and 4 frames behind
 *    (njSetConstantMaterial alpha -85.3 / -170.7),
 *  - blink while twp->flag & 0x100: pwp->free.w[4] counts frames, action 16
 *    is drawn on odd frames for action 34, else on (count & 0x11) frames.
 */

extern NJS_TEXLIST texlist_KNUCKLES;    /* 8C3537D8 */

/*
 * c += a * b where SHC emitted FMAC: the original loads a and b before the
 * addend c, which the "+=" form gives and SA1_FMAC (a * b + c) does not.
 * Fused (fmaf) on PC like SA1_FMAC.
 */
#if defined(TARGET_PC)
#define FMAC_ADD_TO(c, a, b) ((c) = SA1_FMAC((a), (b), (c)))
#else
#define FMAC_ADD_TO(c, a, b) ((c) += (a) * (b))
#endif

/*
 * Literal pool layout. The original translation unit continues from the
 * previous functions of the chunk (code_8C0C450A.c): their pending pool
 * entries make SHC dump pools inside this function at 8C0C4740 (after the
 * first BRA), 8C0C4928 (after action 51's BRA) and 8C0C4B2A, which turns the
 * "free.w[3] != 0" test of action 51, the mtnmode == 2 test and the two
 * action 13/14 tests of the after-image draw into "BF/BT over BRA" pairs.
 * These two stubs (code in code_8C0C450A.c; never called here, ignored by
 * the splicer, prototypes only on PC) leave a pending __asm_stub_sink entry
 * that reproduces that placement; without them those branches are plain
 * BT/BF (8 instructions fewer).
 */
ASM_STUB(void func_8C0C450A(taskwk *twp, playerwk *pwp))
ASM_STUB(void func_8C0C4690(task *tp))

/*
 * Matching notes:
 *  - the rotations are "?:" expression statements (as in code_8C0AE0E0.c):
 *    SHC then keeps the angle in the stack slot @R15 as the original does.
 *    Writing any of them, or the acos roll of action 52, as an if statement
 *    changes SHC's priority-based allocation of the whole function
 *    (R10/R11, R8/R9 and FR12/FR13 swap); the "!= 0" in the roll's test sets
 *    the scratch registers of that call and of the default case.
 *  - (Sint32)twp->wtimer >= 0 is always true but the original tests it
 *    (EXTU.W + CMP/PZ at 8C0C479A).
 *  - action 52 needs the fade factor in a variable (frame): the inline
 *    product loads free.w[3] before the division.
 */

void func_8C0C46AC(task *tp)
{
    taskwk *twp;
    motionwk *mwp;
    playerwk *pwp;
    Uint32 action;
    Sint32 tilt;
    Sint32 i;
    Float frame;
    Float nbf;

    twp = tp->twp;
    mwp = tp->mwp;
    pwp = mwp->work.pwp;
    if (!func_8C038D00(twp->pos.x, twp->pos.y, twp->pos.z, 20.0f)) {
        return;
    }
    action = pwp->mj.reqaction;
    if (pwp->mj.mtnmode == 2) {
        action = pwp->mj.action;
    }
    if ((twp->flag & 0x100) && !func_8C04FE40()) {
        if (pwp->mj.reqaction == 34) {
            if (++pwp->free.w[4] & 1) {
                action = 16;
            }
        } else {
            if (++pwp->free.w[4] & 0x11) {
                action = 16;
            }
        }
    }
    if ((Sint32)twp->wtimer >= 0 && !(twp->wtimer & 2)) {
        njSetTexture(&texlist_KNUCKLES);
        func_8C038CEC(2);
        njPushMatrix(NULL);
        {
            NJS_VECTOR v;
            NJS_VECTOR w;
            CCL_INFO *info;

            v.x = v.z = 0.0f;
            v.y = pwp->p.height * 0.5f;
            njPushMatrix(D_8C67C640);
            twp->ang.z ? (njRotateZ(NULL, twp->ang.z), 0) : 0;
            twp->ang.x ? (njRotateX(NULL, twp->ang.x), 0) : 0;
            (-twp->ang.y) ? (njRotateY(NULL, -twp->ang.y), 0) : 0;
            njCalcVector(NULL, &v, &w);
            njPopMatrix(1);
            w.x += twp->pos.x;
            w.y += twp->pos.y;
            w.z += twp->pos.z;
            if (twp->mode == 19 || twp->mode == 20) {
                FMAC_ADD_TO(w.x, twp->value.f, pwp->cstsp->normal2.x);
                FMAC_ADD_TO(w.y, twp->value.f, pwp->cstsp->normal2.y);
                FMAC_ADD_TO(w.z, twp->value.f, pwp->cstsp->normal2.z);
            }
            njTranslateV(NULL, &w);
            info = CWP(twp)->info;
            info->center.x = w.x;
            info->center.y = w.y;
            info->center.z = w.z;
            switch (action) {
            case 51:
                pwp->free.w[3] = AdjustAngle(pwp->free.w[3], -SubAngle(twp->ang.y, mwp->ang_aim.y), 0x800);
                (0x8000 - twp->ang.y) ? (njRotateY(NULL, 0x8000 - twp->ang.y), 0) : 0;
                (Angle)pwp->free.w[3] ? (njRotateX(NULL, pwp->free.w[3]), 0) : 0;
                break;
            case 52:
                frame = 1.0f - pwp->mj.nframe / (Float)KnucklesActionList[pwp->mj.reqaction].actptr->motion->nbFrame;
                tilt = SA1_FTOI((Float)pwp->free.w[3] * frame);
                (0x8000 - twp->ang.y) ? (njRotateY(NULL, 0x8000 - twp->ang.y), 0) : 0;
                if (tilt) {
                    njRotateX(NULL, tilt);
                }
                (-SA1_FTOI(func_8C10D008(pwp->cstsp->normal2.y) * 10430.381f)) != 0 ? (njRotateZ(NULL, -SA1_FTOI(func_8C10D008(pwp->cstsp->normal2.y) * 10430.381f)), 0) : 0;
                break;
            default:
                twp->ang.z ? (njRotateZ(NULL, twp->ang.z), 0) : 0;
                twp->ang.x ? (njRotateX(NULL, twp->ang.x), 0) : 0;
                (0x8000 - twp->ang.y) ? (njRotateY(NULL, 0x8000 - twp->ang.y), 0) : 0;
                break;
            }
            if (action == 14 && (twp->flag & 3)) {
                v.x = 0.0f;
                v.y = -1.0f;
                v.z = 0.0f;
                njTranslateV(NULL, &v);
                njRotateZ(NULL, 0x2000);
                v.x = 0.7f;
                v.y = 1.09999993f;
                v.z = 0.79999997f;
                njScaleV(NULL, &v);
            }
        }
        if (EWP(twp)->list40 != NULL) {
            func_8C04B57C(twp);
        } else if (pwp->mj.mtnmode != 2) {
            NJS_ARGB argb;

            func_8C040942(pwp->mj.plactptr[action].actptr, pwp->mj.nframe);
            if (action == 13 || (action == 14 && (twp->flag & 3))) {
                frame = pwp->mj.nframe;
                nbf = (Float)pwp->mj.plactptr[pwp->mj.reqaction].actptr->motion->nbFrame;
                argb.b = argb.g = argb.r = 0.0f;
                for (i = 1; i < 3; i++) {
                    frame -= 2.0f;
                    if (frame < 0.0f) {
                        frame += nbf;
                    }
                    argb.a = -((Float)i * 85.33333f);
                    njSetConstantMaterial(&argb);
                    func_8C040942(pwp->mj.plactptr[action].actptr, frame);
                }
                argb.a = 0.0f;
                njSetConstantMaterial(&argb);
            }
        } else {
            func_8C040942(pwp->mj.actwkptr, pwp->mj.nframe);
        }
        njPopMatrix(1);
        func_8C038CEC(0);
    }
    if (func_8C04FE40()) {
        func_8C09247E(twp, &pwp->shadow);
    }
}
