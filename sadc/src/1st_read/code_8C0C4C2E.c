#include "common.h"
#include "ninja.h"
#include "sa1/sa1.h"

/*
 * 1ST_READ 8C0C4C2E-8C0C5220 (chunk 8C0C0640-8C0C5220, functions 6-9 of 9):
 * Knuckles.
 *  func_8C0C4C2E  hand models for the upgrades (pwp->equipment & 0x60:
 *                 0x20 Fighting Gloves, 0x40 Shovel Claw): switches the
 *                 model of the hand nodes of both model trees and the vertex
 *                 index lists (pnum) of welds 10, 11, 22 and 23
 *  func_8C0C4F40  twp->mode = 1, waittimer = 0, then the stand selector
 *                 func_8C0C4F48 (tail call placed right after: no BRA)
 *  func_8C0C4F48  stand action selector: on flat ground actions 0-6 (idle
 *                 action 4 after 1800 frames, with a voice), on a slope
 *                 (no attr 0x4000) action 7 or 8 depending on how the
 *                 player faces the slope
 *  func_8C0C513A  run action from the speed (pwp->spd.x + 5 * mwp->height):
 *                 9-13 (jog_speed..dash_speed), 71/75 while holding an
 *                 object; work.f = spd.x + 20 * mwp->height
 */

extern void func_8C07DCE0(taskwk *twp);         /* character voice (code_8C07DCE0.c) */

/* Knuckles' hand node objects (NJS_OBJECT.model is switched) and models
 * (each model sits right before its object: object - 0x28). Declared as
 * arrays: SHC then stores through the object's address with @(4,Rn) and
 * hoists the same constants as the original; a plain NJS_OBJECT stores
 * through a "symbol+4" pool constant. */
extern NJS_OBJECT D_8C356DD8[];
extern NJS_OBJECT D_8C3569DC[];
extern NJS_OBJECT D_8C356810[];
extern NJS_OBJECT D_8C3563BC[];
extern NJS_OBJECT D_8C357ED8[];
extern NJS_OBJECT D_8C357AA8[];
extern NJS_OBJECT D_8C3578DC[];
extern NJS_OBJECT D_8C35746C[];
extern NJS_OBJECT D_8C35E5BC[];
extern NJS_OBJECT D_8C35E1C0[];
extern NJS_OBJECT D_8C35DFF4[];
extern NJS_OBJECT D_8C35DBA0[];
extern NJS_OBJECT D_8C35F6BC[];
extern NJS_OBJECT D_8C35F28C[];
extern NJS_OBJECT D_8C35F0C0[];
extern NJS_OBJECT D_8C35EC50[];
extern NJS_MODEL D_8C356DB0[];
extern NJS_MODEL D_8C3569B4[];
extern NJS_MODEL D_8C3567E8[];
extern NJS_MODEL D_8C356394[];
extern NJS_MODEL D_8C357EB0[];
extern NJS_MODEL D_8C357A80[];
extern NJS_MODEL D_8C3578B4[];
extern NJS_MODEL D_8C357444[];
extern NJS_MODEL D_8C38E160[];            /* Knuckles_Left_Shovel_Claw_Normal 8C38E188 model */
extern NJS_MODEL D_8C38ED04[];            /* Knuckles_Right_Shovel_Claw_Normal 8C38ED2C model */
extern NJS_MODEL D_8C38E740[];            /* Knuckles_Left_Shovel_Claw_Fighting_Gloves 8C38E768 model */
extern NJS_MODEL D_8C38F2C8[];            /* Knuckles_Right_Shovel_Claw_Fighting_Gloves 8C38F2F0 model */
extern NJS_MODEL D_8C38F760[];            /* Knuckles_Left_Fighting_Glove_Wrist 8C38F788 model */
extern NJS_MODEL D_8C38FFE8[];            /* knu_i_claw_l 8C390010 model */
extern NJS_MODEL D_8C38FE1C[];
extern NJS_MODEL D_8C38F9C8[];
extern NJS_MODEL D_8C390418[];            /* Knuckles_Right_Fighting_Glove_Wrist 8C390440 model */
extern NJS_MODEL D_8C390CBC[];            /* knu_i_claw_r 8C390CE4 model */
extern NJS_MODEL D_8C390AF0[];
extern NJS_MODEL D_8C390680[];
/* weld vertex index lists (PL_JOIN_VERTEX.pnum), right before KnucklesWeldList */
extern Uint16 D_8C3530EC[];
extern Uint16 D_8C353100[];

/*
 * Same translation unit as the Knuckles code before it: func_8C0C46AC's
 * literal pool is flushed inside func_8C0C4C2E (8C0C4D00, after case 0),
 * which puts the case bodies out of BT range (BF + BRA). These stubs supply
 * the pending pool references that make SHC flush at the same place
 * (code_8C0C46AC.c uses the same device).
 */
ASM_STUB(void func_8C0C450A(taskwk *twp, playerwk *pwp))
ASM_STUB(void func_8C0C4690(task *tp))
ASM_STUB(void func_8C0C46AC(task *tp))

void func_8C0C4C2E(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    PL_JOIN_VERTEX *jvl;
    PL_JOIN_VERTEX *jvr;

    /* welds 10/11 and 22/23 (the original computes &[11] as &[10] + 1) */
    jvl = &KnucklesWeldList[10];
    jvr = &KnucklesWeldList[22];
    switch (pwp->equipment & 0x60) {
    case 0:
        D_8C356DD8->model = D_8C356DB0;
        D_8C3569DC->model = D_8C3569B4;
        D_8C356810->model = D_8C3567E8;
        D_8C3563BC->model = D_8C356394;
        D_8C357ED8->model = D_8C357EB0;
        D_8C357AA8->model = D_8C357A80;
        D_8C3578DC->model = D_8C3578B4;
        D_8C35746C->model = D_8C357444;
        D_8C35E5BC->model = D_8C356DB0;
        D_8C35E1C0->model = D_8C3569B4;
        D_8C35DFF4->model = D_8C3567E8;
        D_8C35DBA0->model = D_8C356394;
        D_8C35F6BC->model = D_8C357EB0;
        D_8C35F28C->model = D_8C357A80;
        D_8C35F0C0->model = D_8C3578B4;
        D_8C35EC50->model = D_8C357444;
        jvl[0].pnum = jvl[1].pnum = jvr[0].pnum = jvr[1].pnum = D_8C3530EC;
        break;
    case 0x40:
        D_8C356DD8->model = D_8C356DB0;
        D_8C3569DC->model = D_8C3569B4;
        D_8C356810->model = D_8C38E160;
        D_8C3563BC->model = D_8C356394;
        D_8C357ED8->model = D_8C357EB0;
        D_8C357AA8->model = D_8C357A80;
        D_8C3578DC->model = D_8C38ED04;
        D_8C35746C->model = D_8C357444;
        D_8C35E5BC->model = D_8C356DB0;
        D_8C35E1C0->model = D_8C3569B4;
        D_8C35DFF4->model = D_8C38E160;
        D_8C35DBA0->model = D_8C356394;
        D_8C35F6BC->model = D_8C357EB0;
        D_8C35F28C->model = D_8C357A80;
        D_8C35F0C0->model = D_8C38ED04;
        D_8C35EC50->model = D_8C357444;
        jvl[0].pnum = jvl[1].pnum = jvr[0].pnum = jvr[1].pnum = D_8C353100;
        break;
    case 0x20:
        D_8C356DD8->model = D_8C38F760;
        D_8C3569DC->model = D_8C38FFE8;
        D_8C356810->model = D_8C38FE1C;
        D_8C3563BC->model = D_8C38F9C8;
        D_8C357ED8->model = D_8C390418;
        D_8C357AA8->model = D_8C390CBC;
        D_8C3578DC->model = D_8C390AF0;
        D_8C35746C->model = D_8C390680;
        D_8C35E5BC->model = D_8C38F760;
        D_8C35E1C0->model = D_8C38FFE8;
        D_8C35DFF4->model = D_8C38FE1C;
        D_8C35DBA0->model = D_8C38F9C8;
        D_8C35F6BC->model = D_8C390418;
        D_8C35F28C->model = D_8C390CBC;
        D_8C35F0C0->model = D_8C390AF0;
        D_8C35EC50->model = D_8C390680;
        jvl[0].pnum = jvl[1].pnum = jvr[0].pnum = jvr[1].pnum = D_8C3530EC;
        break;
    case 0x60:
        D_8C356DD8->model = D_8C38F760;
        D_8C3569DC->model = D_8C38FFE8;
        D_8C356810->model = D_8C38E740;
        D_8C3563BC->model = D_8C38F9C8;
        D_8C357ED8->model = D_8C390418;
        D_8C357AA8->model = D_8C390CBC;
        D_8C3578DC->model = D_8C38F2C8;
        D_8C35746C->model = D_8C390680;
        D_8C35E5BC->model = D_8C38F760;
        D_8C35E1C0->model = D_8C38FFE8;
        D_8C35DFF4->model = D_8C38E740;
        D_8C35DBA0->model = D_8C38F9C8;
        D_8C35F6BC->model = D_8C390418;
        D_8C35F28C->model = D_8C390CBC;
        D_8C35F0C0->model = D_8C38F2C8;
        D_8C35EC50->model = D_8C390680;
        jvl[0].pnum = jvl[1].pnum = jvr[0].pnum = jvr[1].pnum = D_8C353100;
        break;
    }
}

void func_8C0C4F48(taskwk *twp, motionwk *mwp, playerwk *pwp);

void func_8C0C4F40(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    twp->mode = 1;
    pwp->waittimer = 0;
    func_8C0C4F48(twp, mwp, pwp);
}

void func_8C0C4F48(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_LINE line;
    NJS_POINT3 v;
    NJS_POINT3 b;
    NJS_POINT3 a;
    Float c;
    Angle ang;

    if (pwp->mj.reqaction == 33) {
        return;
    }
    c = -njInnerProduct(&Gravity, &pwp->floor_normal);
    if (c > njCos(0x11C7)) {
        if (pwp->mj.reqaction != 0 && pwp->mj.reqaction != 1 && pwp->mj.reqaction != 2 &&
            pwp->mj.reqaction != 3 && pwp->mj.reqaction != 4 && pwp->mj.reqaction != 5 &&
            pwp->mj.reqaction != 6) {
            pwp->mj.reqaction = 0;
        } else if (pwp->waittimer > 1800) {
            pwp->waittimer = 0;
            pwp->mj.reqaction = 4;
            func_8C07DCE0(twp);
        }
    } else if (!(pwp->attr & 0x4000) && c > njCos(0x2000)) {
        line.vx = pwp->floor_normal.x;
        line.vy = pwp->floor_normal.y;
        line.vz = pwp->floor_normal.z;
        line.px = line.py = line.pz = 0.0f;
        v.x = 1.0f;
        v.y = v.z = 0.0f;
        PConvertVector_P2G(twp, &v);
        njDistanceP2L(&v, &line, &a);
        if (njScalor(&a) == 0.0f) {
            a.x = 1.0f;
            a.y = a.z = 0.0f;
        } else {
            njUnitVector(&a);
        }
        njDistanceP2L(&Gravity, &line, &b);
        if (njScalor(&b) == 0.0f) {
            b.x = 1.0f;
            b.y = b.z = 0.0f;
        } else {
            njUnitVector(&b);
        }
        ang = VectorAngle(&a, &b, &pwp->floor_normal);
        if (ang > 0xC000 || ang < 0x4000) {
            pwp->mj.reqaction = 8;
        } else {
            pwp->mj.reqaction = 7;
        }
    }
    pwp->waittimer++;
}

void func_8C0C513A(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    Float spd;

    spd = SA1_FMAC(mwp->height, 5.0f, pwp->spd.x);
    if (pwp->mj.reqaction == 33 || pwp->mj.reqaction == 22 || pwp->mj.reqaction == 73) {
        return;
    }
    if (twp->flag & TWF_HOLD) {
        if (spd > pwp->p.run_speed) {
            pwp->mj.reqaction = 71;
        } else {
            pwp->mj.reqaction = 75;
        }
    } else if (spd > pwp->p.dash_speed) {
        pwp->mj.reqaction = 13;
    } else if (spd > pwp->p.rush_speed) {
        pwp->mj.reqaction = 12;
    } else if (spd > pwp->p.run_speed) {
        pwp->mj.reqaction = 11;
    } else if (spd > pwp->p.jog_speed) {
        pwp->mj.reqaction = 10;
    } else {
        pwp->mj.reqaction = 9;
    }
    pwp->work.f = SA1_FMAC(mwp->height, 20.0f, pwp->spd.x);
}
