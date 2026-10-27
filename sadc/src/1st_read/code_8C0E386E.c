#include "sa1/sa1.h"

/*
 * Big the Cat (1ST_READ chunk 8C0DFFA0-8C0E59C0, functions 3-7 of 29):
 *
 *  func_8C0E386E  destructor (tp->dest, set by BigMain): player free,
 *                 stop Big's sounds, then release texlist_BIG when the
 *                 stage is 0x13 (level 19), else resource slot 7.
 *  func_8C0E38AA  collision centres (BigMain): shape 0 at center_height
 *                 above Big's position, shape 1 at the user0 weld point
 *                 moved back by 10 * user0_vec (the fishing rod), both
 *                 rotated by twp->ang (y as 0x8000 - ang.y).
 *  func_8C0E3A40  display (tp->disp): sphere visibility test, palette
 *                 bank, draw flags, model at the collision centre with
 *                 twp->ang and twp->scl, event action queue / debug model
 *                 (button 0x400 in mode 55) / action at mj.nframe, shadow.
 *  func_8C0E3C20  stand selector (BigMain, BigModeMachine, 8C0E4432,
 *                 8C0E488C, 8C0E4DA4): holding an object gives action 74
 *                 for a held object of class 4 (fishing rod), else 30; on
 *                 level ground (cos(0x11C7)) requests 0-5 become the idle
 *                 action 1 (with a voice, 8C07DCE0) after 1800 frames, any
 *                 other request is reset to 0; on steeper ground (up to 45
 *                 degrees) without attr 0x4000 the slope stand action 4 or
 *                 5 is picked as in Sonic's func_8C0AE3D6.
 *  func_8C0E3DE0  walk action selector (BigMain, BigModeMachine): leaves 24
 *                 and 29 alone; speed spd.x + 5 * mwp->height against the
 *                 jog/run/rush/dash speeds picks 7-11, or 75/76 (class-4
 *                 object) and 31/27 while holding.
 */

extern NJS_TEXLIST texlist_BIG;                 /* 8C525FB0 */

/* character voice (code_8C07DCE0.c) */
extern void func_8C07DCE0(taskwk *twp);

void func_8C0E386E(task *tp)
{
    taskwk *twp;

    twp = tp->twp;
    func_8C06C5CA((Uint8)twp->counter.b[0]);
    func_8C0945D0(twp);
    if ((GetStageNumber() & 0xFF00) == 0x1300) {
        func_8C6088BC(&texlist_BIG);
    } else {
        func_8C098FFC(7);
    }
}

void func_8C0E38AA(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_POINT3 v;
    CCL_INFO *info;
    CCL_INFO *info2;

    info = CWP(twp)->info;
    njPushMatrix(D_8C67C640);
    v.x = v.z = 0.0f;
    v.y = pwp->p.center_height;
    twp->ang.z ? njRotateZ(NULL, twp->ang.z) : (void)0;
    twp->ang.x ? njRotateX(NULL, twp->ang.x) : (void)0;
    0x8000 - twp->ang.y ? njRotateY(NULL, 0x8000 - twp->ang.y) : (void)0;
    njCalcVector(NULL, &v, &info->center);
    info->center.x += twp->pos.x;
    info->center.y += twp->pos.y;
    info->center.z += twp->pos.z;
    v.x = pwp->user0_pos.x - pwp->user0_vec.x * 10.0f;
    v.y = pwp->user0_pos.y - pwp->user0_vec.y * 10.0f;
    v.z = pwp->user0_pos.z - pwp->user0_vec.z * 10.0f;
    info2 = info + 1;
    njCalcVector(NULL, &v, &info2->center);
    info2->center.x += info->center.x;
    info2->center.y += info->center.y;
    info2->center.z += info->center.z;
    njPopMatrix(1);
}

void func_8C0E3A40(task *tp)
{
    taskwk *twp;
    motionwk *mwp;
    playerwk *pwp;
    Uint32 action;

    twp = tp->twp;
    mwp = tp->mwp;
    pwp = mwp->work.pwp;
    if (!func_8C038D00(twp->pos.x, twp->pos.y, twp->pos.z, 25.0f)) {
        return;
    }
    func_8C038CEC(2);
    func_8C056926();
    func_8C05694A(0, 0x80000);
    action = pwp->mj.reqaction;
    if (pwp->mj.mtnmode == 2) {
        action = pwp->mj.action;
    }
    if (!(twp->wtimer & 2)) {
        njSetTexture(&texlist_BIG);
        njPushMatrix(NULL);
        njTranslateV(NULL, &CWP(twp)->info->center);
        twp->ang.z ? njRotateZ(NULL, twp->ang.z) : (void)0;
        twp->ang.x ? njRotateX(NULL, twp->ang.x) : (void)0;
        0x8000 - twp->ang.y ? njRotateY(NULL, 0x8000 - twp->ang.y) : (void)0;
        njScale(NULL, twp->scl.x, twp->scl.y, twp->scl.z);
        if (EWP(twp)->list40 != NULL) {
            func_8C04B57C(twp);
        } else if ((B_8C754C08[0].on & 0x400) && twp->mode == 55) {
            func_8C036F12((NJS_MODEL *)pwp->mj.plactptr[action].actptr->object);
        } else if (pwp->mj.mtnmode != 2) {
            func_8C040942(pwp->mj.plactptr[action].actptr, pwp->mj.nframe);
        } else {
            func_8C040942(pwp->mj.actwkptr, pwp->mj.nframe);
        }
        njPopMatrix(1);
    }
    func_8C056938();
    func_8C038CEC(0);
    if (func_8C04FE40()) {
        func_8C09247E(twp, &pwp->shadow);
    }
}

void func_8C0E3C20(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_LINE line;
    NJS_POINT3 v;
    NJS_POINT3 b;
    NJS_POINT3 a;
    Float dot;
    Angle ang;

    dot = -njInnerProduct(&Gravity, &pwp->floor_normal);
    if (twp->flag & TWF_HOLD) {
        if (pwp->htp != NULL && pwp->htp->twp->id == 4) {
            pwp->mj.reqaction = 74;
        } else {
            pwp->mj.reqaction = 30;
        }
    } else if (dot > njCos(0x11C7)) {
        if (pwp->mj.reqaction != 0 && pwp->mj.reqaction != 1 && pwp->mj.reqaction != 2
            && pwp->mj.reqaction != 3 && pwp->mj.reqaction != 4 && pwp->mj.reqaction != 5) {
            pwp->mj.reqaction = 0;
        } else if (pwp->waittimer > 1800) {
            pwp->waittimer = 0;
            pwp->mj.reqaction = 1;
            func_8C07DCE0(twp);
        }
    } else if (!(pwp->attr & 0x4000)) {
        if (dot > njCos(0x2000)) {
            line.vx = pwp->floor_normal.x;
            line.vy = pwp->floor_normal.y;
            line.vz = pwp->floor_normal.z;
            line.px = line.py = line.pz = 0.0f;
            v.x = 1.0f;
            v.y = v.z = 0.0f;
            PConvertVector_P2G(twp, &v);
            njDistanceP2L(&v, &line, &a);
            njUnitVector(&a);
            njDistanceP2L(&Gravity, &line, &b);
            njUnitVector(&b);
            ang = VectorAngle(&a, &b, &pwp->floor_normal);
            if (ang > 0xC000 || ang < 0x4000) {
                pwp->mj.reqaction = 5;
            } else {
                pwp->mj.reqaction = 4;
            }
        }
    }
    pwp->waittimer++;
}

void func_8C0E3DE0(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    Float spd;

    spd = SA1_FMAC(5.0f, mwp->height, pwp->spd.x);
    if (pwp->mj.reqaction == 24 || pwp->mj.reqaction == 29) {
        return;
    }
    if (twp->flag & TWF_HOLD) {
        if (pwp->htp->twp->id != 4) {
            if (spd > pwp->p.run_speed) {
                pwp->mj.reqaction = 27;
            } else {
                pwp->mj.reqaction = 31;
            }
        } else if (spd > pwp->p.run_speed) {
            pwp->mj.reqaction = 76;
        } else {
            pwp->mj.reqaction = 75;
        }
    } else if (spd > pwp->p.dash_speed) {
        pwp->mj.reqaction = 11;
    } else if (spd > pwp->p.rush_speed) {
        pwp->mj.reqaction = 10;
    } else if (spd > pwp->p.run_speed) {
        pwp->mj.reqaction = 9;
    } else if (spd > pwp->p.jog_speed) {
        pwp->mj.reqaction = 8;
    } else {
        pwp->mj.reqaction = 7;
    }
}
