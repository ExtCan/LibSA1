#include "sa1/pending.h"

/*
 * TIKAL_PROG overlay 0CB00780-0CB00F08 (chunk 0CB00000-0CB00F60, functions
 * 3-14 of 14): Tikal's player helpers, counterparts of Sonic's
 * code_8C0AE0E0.c / code_8C0AEFA0.c / code_8C0AFB10.c / code_8C0B2338.c.
 *  func_0CB00780  destructor (tp->dest): free the player slot, stop the
 *                 owned sounds, release resource slot 4
 *  func_0CB007A0  move collision shape 0 to the body centre
 *                 (p.center_height along the player's up axis) in world space
 *  func_0CB008A0  display (tp->disp)
 *  func_0CB00994  run action selector (action 1/2 by speed, work.f)
 *  func_0CB00A40  free movement (debug mode)
 *  func_0CB00B64  jump check (mode 3, action 3)
 *  func_0CB00C20  action button: mode 6, action 5, speed cleared
 *  func_0CB00C64  event lock helper (rotation towards the event angle)
 *  func_0CB00CD2  input/request check (event lock, path queue, twp->smode)
 *  func_0CB00E74  airborne -> mode 6, action 5
 *  func_0CB00E90  no stick input and no speed -> stand (mode 1, action 0)
 *  func_0CB00ED4  face task exec: allocate the PL_FACE and hand the task to
 *                 the face code 8C07B106 / 8C07B0F6
 */

/* jump button pressed (0, 1, 2) */
Sint32 func_8C06CEBE(taskwk *twp, playerwk *pwp);
/* reset the player to the stage start position */
void func_8C04F8FA(taskwk *twp);
/* face task (8C07B080 chunk) */
void func_8C07B0F6(task *tp);
void func_8C07B106(task *tp);

extern NJS_TEXLIST D_0CB01814;                      /* Tikal texlist */
extern NJS_OBJECT D_0CB06D50;                       /* head node whose sibling is switched by the face code */
extern NJS_OBJECT D_0CB06AC0;                       /* static head object */
extern NJS_OBJECT TIKAL_PROG_Tikal_EV_Head;         /* morph head object */
extern NJS_MOTION TIKAL_PROG_Tikal_EV_Head_Shapes;  /* face shape motion */

/* The original loads tp->mwp (into R4, overwritten by the player number):
 * pwp is fetched as in Sonic's destructor but never used; SHC drops the
 * unused pwp load and keeps the mwp one. */
void func_0CB00780(task *tp)
{
    taskwk *twp;
    motionwk *mwp;
    playerwk *pwp;

    twp = tp->twp;
    mwp = tp->mwp;
    pwp = mwp->work.pwp;
    (void)pwp;
    func_8C06C5CA((Uint8)twp->counter.b[0]);
    func_8C0945D0(twp);
    func_8C098FFC(4);
}

/* pwp lives in @R15 (shared with the ?: angle temporary of the rotations) */
void func_0CB007A0(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    CCL_INFO *info;
    NJS_POINT3 v;

    info = CWP(twp)->info;
    njPushMatrix(D_8C67C640);
    v.x = v.z = 0.0f;
    v.y = pwp->p.center_height;
    twp->ang.z ? (njRotateZ(NULL, twp->ang.z), 0) : 0;
    twp->ang.x ? (njRotateX(NULL, twp->ang.x), 0) : 0;
    -twp->ang.y ? (njRotateY(NULL, -twp->ang.y), 0) : 0;
    njCalcVector(NULL, &v, &info->center);
    njPopMatrix(1);
    info->center.x += twp->pos.x;
    info->center.y += twp->pos.y;
    info->center.z += twp->pos.z;
}

/* The rotations are "?:" expressions (angle kept in @R15, as in Sonic's
 * display code_8C0AE0E0.c). */
void func_0CB008A0(task *tp)
{
    taskwk *twp;
    motionwk *mwp;
    playerwk *pwp;
    Uint32 action;
    /* never accessed: they give the 20-byte frame of the original (the ?:
     * angle temporary at @R15, then 4 + 12 bytes), the layout of Sonic's
     * display func_8C0AE17A whose NJS_VECTOR v this display does not use */
    SH_ONLY(NJS_VECTOR v;)
    SH_ONLY(Sint32 stack_pad[1];)

    twp = tp->twp;
    mwp = tp->mwp;
    pwp = mwp->work.pwp;
    func_8C056926();
    func_8C05694A(0, 0x80000);
    func_8C038CEC(2);
    action = pwp->mj.reqaction;
    if (pwp->mj.mtnmode == 2) {
        action = pwp->mj.action;
    }
    if (!(twp->wtimer & 1)) {
        njSetTexture(&D_0CB01814);
        njPushMatrix(NULL);
        njTranslateV(NULL, &CWP(twp)->info->center);
        twp->ang.z ? (njRotateZ(NULL, twp->ang.z), 0) : 0;
        twp->ang.x ? (njRotateX(NULL, twp->ang.x), 0) : 0;
        (0x8000 - twp->ang.y) ? (njRotateY(NULL, 0x8000 - twp->ang.y), 0) : 0;
        if (EWP(twp)->list40 != NULL) {
            func_8C04B57C(twp);
        } else if (pwp->mj.mtnmode != 2) {
            func_8C040942(pwp->mj.plactptr[action].actptr, pwp->mj.nframe);
        } else {
            func_8C040942(pwp->mj.actwkptr, pwp->mj.nframe);
        }
        njPopMatrix(1);
    }
    func_8C038CEC(0);
    func_8C056938();
}

void func_0CB00994(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    Float spd;

    spd = SA1_FMAC(mwp->height, 5.0f, pwp->spd.x);
    if (pwp->mj.reqaction != 8) {
        if (spd > pwp->p.jog_speed) {
            pwp->mj.reqaction = 2;
        } else {
            pwp->mj.reqaction = 1;
        }
        pwp->work.f = SA1_FMAC(mwp->height, 20.0f, pwp->spd.x);
    }
}

/*
 * NON_MATCHING: identical except for one register, the same problem as
 * Sonic's copy func_8C0AF012 (code_8C0AEFA0.c): the original loads the
 * address of atan2f into R2 (0CB00B0A), SHC 5.1r03 picks R3 for this source.
 * Tried: early return after the y branch (Knuckles' func_8C0C5680: the
 * constant 52 is then rematerialised in R5), the 0x400 test as
 * "? TRUE : FALSE" / "&& 1" / "|| 0" (same), split angle expression, a float
 * temporary for the angle, separate variables for the second branch or the
 * sign, the y step as one ?: expression, "+= 5.0f * njCos(ang)".
 * The x/z steps are FMACs (0CB00B40, 0CB00B54): fused on PC.
 */
#ifdef NON_MATCHING
void func_0CB00A40(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    Float fx;
    Float fy;
    Angle ang;

    if (B_8C754C08[(Uint8)twp->counter.b[0]].press & 2) {
        func_8C04F8FA(twp);
    }
    if (B_8C754C08[(Uint8)twp->counter.b[0]].on & 0x400) {
        fx = (Float)(B_8C754C08[(Uint8)twp->counter.b[0]].x1 << 8);
        fy = (Float)(B_8C754C08[(Uint8)twp->counter.b[0]].y1 << 8);
        if (fx > 3072.0f || fx < -3072.0f || fy > 3072.0f || fy < -3072.0f) {
            if (fy != 0.0f) {
                fx = fy / njSqrt(fy * fy);
            } else {
                fx = 0.0f;
            }
            twp->pos.y -= fx * 5.0f;
        }
    } else {
        fx = (Float)(B_8C754C08[(Uint8)twp->counter.b[0]].x1 << 8);
        fy = (Float)(B_8C754C08[(Uint8)twp->counter.b[0]].y1 << 8);
        if (fx > 3072.0f || fx < -3072.0f || fy > 3072.0f || fy < -3072.0f) {
            ang = SA1_FTOI(atan2f(fy, fx) * 10430.381f) - camera_twp->ang.y;
            SA1_FMAC_ACC(twp->pos.x, njCos(ang), 5.0f);
            SA1_FMAC_ACC(twp->pos.z, njSin(ang), 5.0f);
        }
    }
}
#else
ASM_STUB(void func_0CB00A40(taskwk *twp, motionwk *mwp, playerwk *pwp))
#endif

Sint32 func_0CB00B64(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    forcewk *fwp;

    if (func_8C06CEBE(twp, pwp)) {
        twp->mode = 3;
        pwp->spd.y = pwp->p.jmp_y_spd;
        if (pwp->ttp != NULL) {
            fwp = FWP(pwp->ttp);
            if (fwp != NULL) {
                SetAccelerationP(twp->counter.b[0], fwp->pos_spd.x, fwp->pos_spd.y, fwp->pos_spd.z);
            }
        }
        pwp->mj.reqaction = 3;
        twp->flag &= ~2;
        pwp->jumptimer = 0;
        func_8C0940DA(17, 0, 0, 0);
        return 1;
    }
    return 0;
}

Sint32 func_0CB00C20(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    if (B_8C754C08[(Uint8)twp->counter.b[0]].press & D_8C19E38C) {
        twp->mode = 6;
        PClearSpeed(NULL, pwp);
        pwp->mj.reqaction = 5;
        pwp->work.f = 0.0f;
        return 1;
    }
    return 0;
}

Sint32 func_0CB00C64(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    eventwk *ewp;

    ewp = EWP(twp);
    switch ((Uint8)ewp->lock) {
    case 2:
    case 3:
    case 4:
    case 1:
        twp->flag |= TWF_EVENTLOCK;
        break;
    case 6:
        func_8C07C298(twp->counter.b[0], ewp->ang[1]);
        break;
    case 5:
        func_8C07C298(twp->counter.b[0], ewp->ang[1] - twp->ang.y + 0x4000);
        break;
    default:
        twp->flag &= ~TWF_EVENTLOCK;
        return 0;
    }
    return 1;
}

Sint32 func_0CB00CD2(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    if (EWP(twp)->lock) {
        func_0CB00C64(twp, mwp, pwp);
        return 0;
    }
    if (EWP(twp)->list4C != NULL) {
        twp->mode = 9;
        twp->flag &= ~TWF_ONPATH;
        return 1;
    }
    if (!(twp->flag & TWF_REQUEST)) {
        return 0;
    }
    switch (twp->smode) {
    case 3:
    case 8:
    case 6:
    case 7:
        twp->mode = 4;
        pwp->mj.reqaction = 3;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 4:
    case 5:
    case 11:
        twp->mode = 5;
        pwp->mj.reqaction = 3;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 12:
        twp->mode = 8;
        pwp->mj.reqaction = 0;
        twp->ang.x = twp->ang.z = 0;
        PClearSpeed(mwp, pwp);
        twp->flag &= ~TWF_ONPATH;
        break;
    case 17:
        twp->mode = 8;
        pwp->mj.reqaction = 5;
        twp->ang.x = twp->ang.z = 0;
        PClearSpeed(mwp, pwp);
        twp->flag &= ~TWF_ONPATH;
        break;
    case 9:
    case 10:
        twp->mode = 8;
        pwp->mj.reqaction = 0;
        twp->ang.x = twp->ang.z = 0;
        PClearSpeed(mwp, pwp);
        twp->flag &= ~TWF_ONPATH;
        break;
    case 24:
        twp->mode = 1;
        pwp->mj.reqaction = 0;
        twp->flag &= ~TWF_ONPATH;
        break;
    case 26:
        if (twp->mode == 2 || pwp->spd.x > pwp->p.run_speed) {
            twp->flag |= TWF_ONPATH;
        }
        return 0;
    case 50:
        func_8C0940DA(23, 0, 0, 0);
        twp->mode = 10;
        pwp->mj.reqaction = 5;
        twp->flag &= ~TWF_ONPATH;
        break;
    default:
        return 0;
    }
    return 1;
}

Sint32 func_0CB00E74(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    if (!(twp->flag & TWF_GROUND)) {
        twp->mode = 6;
        pwp->mj.reqaction = 5;
        return 1;
    }
    return 0;
}

Sint32 func_0CB00E90(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    if (!GetAnalog(twp, NULL, NULL) && pwp->spd.x == 0.0f) {
        twp->mode = 1;
        pwp->mj.reqaction = 0;
        return 1;
    }
    return 0;
}

void func_0CB00ED4(task *tp)
{
    PL_FACE *fp;

    if ((fp = CAlloc(1, sizeof(PL_FACE))) != NULL) {
        tp->mwp = (motionwk *)fp;
        fp->parent = &D_0CB06D50;
        fp->sibling = &D_0CB06AC0;
        fp->object = &TIKAL_PROG_Tikal_EV_Head;
        fp->motion = &TIKAL_PROG_Tikal_EV_Head_Shapes;
        tp->dest = func_8C07B0F6;
        tp->exec = func_8C07B106;
    }
}
