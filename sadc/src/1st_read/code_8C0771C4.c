/*
 * Player common (1ST_READ 8C0771C4-8C0777E0): PSetPosition-like position
 * update with land collision along the player's own up vector (sibling of
 * func_8C078856, same structure). Returns 0, or 1/2 (soft/hard wall hit) like
 * PSetPosition.
 *
 * func_8C0771C4 first reacts to a CCL hit on a shape whose flag byte +5 has a
 * low nibble set: the horizontal speed is cleared when moving into the
 * push-out vector of 8C0344DC; status 1 below p.run_speed, else 2. With
 * twp->flag & 2 the speed is just added to x/z. Otherwise the collision sphere
 * (cstsp, radius height/2) is put at height/2 - pos_error above the feet along
 * the up vector (rotation ZXY of twp->ang), cp->tnorm/angx/angz take the
 * current up vector, and every gathered land object (B_8C754EF4) is tested
 * with 8C029B00:
 *  - floor-like hit (normal2 within 0x2C00 of tnorm, or more upward than it)
 *    unless rising while it was grounded (twp->flag & 3 before the call):
 *    when moving, twp->ang.x/z turn (AdjustAngle 0x1000) towards the polygon
 *    slope (asin of normal2.z / -normal2.x; on ceilings the flipped variant
 *    closer to the current angles), the sphere goes to pshbk + anaspdh,
 *    moving land objects carry the player (8C07506E, pwp->ttp), the position
 *    follows the sphere, twp->flag |= 1;
 *  - other hits push the player back to pshbk and take the analog speed;
 *    a head-on hit (VectorAngle(-spd, normal2) <= 0x2C00) gives status 1
 *    below p.crash_speed, else 2.
 * Without a floor hit the speed is added to the position.
 */
/* the shared header declares 8C029B00 as (csts *, Uint32, Uint32); R5 is the
 * land object (_OBJ_LANDCOLL.pObject) and R6 its attribute (slAttribute),
 * as in code_8C0742D4.c */
#define func_8C029B00 func_8C029B00_hdr
#include "sa1/sa1.h"
#undef func_8C029B00

Sint32 func_8C029B00(csts *cp, NJS_OBJECT *object, Uint32 attr);
/* CCL hit query: push-out vector from the hit shape (code_8C06CA30.c) */
Sint8 func_8C0344DC(taskwk *twp, NJS_POINT3 *v);
/* moving land object: carry the player (code_8C0730D8.c prototype) */
void func_8C07506E(taskwk *twp, playerwk *pwp, csts *cp, NJS_POINT3 *v);

#ifdef NON_MATCHING
/*
 * Instruction-identical except for the stack slots of six spilled values
 * (15 instructions): original ax@8 az@4 ax2@20 az2@16 d1@28 !(flag&3)@32,
 * this source gives ax@16 az@20 ax2@4 az2@8 d1@32 !(flag&3)@28 (pure
 * allocator ordering; every other instruction, register and slot matches).
 * Notes for a future attempt:
 *  - the loop body must be a nested if (not "continue") to get the
 *    original pre-test (CMP/PL) loop shape; the else after "flag & 2"
 *    (instead of an early return) gives the original R9 = ret / R10 = 1;
 *  - the original has three induction pointers for lcp[i] (@(24,R15) for
 *    pTask, a dead one at @R15, R8 for the 8C029B00 arguments) plus lcp at
 *    @(4,R15); the dead one is reproduced only by an unused read of lcp[i]
 *    in the block before AdjustAngle (obj below), so the original source
 *    probably had some equivalent dead expression there;
 *  - ip, ang, d1/d2 and tp must be variables (FMOV FR0,FR4 / MOV R0,R4).
 * Tried without effect on the slots: declaration order/scopes/types,
 * for/while, compare spellings, float operand order, explicit
 * wasair = !(flag & 3), field of the dead read.
 */
Sint32 func_8C0771C4(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_POINT3 ofs;
    NJS_POINT3 pos;
    NJS_POINT3 v;
    NJS_POINT3 dir;
    csts *cp;
    task *tp;
    NJS_OBJECT *obj;
    _OBJ_LANDCOLL *lcp;
    Angle ax;
    Angle az;
    Angle ax2;
    Angle az2;
    Angle ang;
    Sint32 d1;
    Sint32 d2;
    Float ip;
    Sint16 flag;
    Uint16 i;
    Uint8 hit;
    Sint32 ret;

    ret = 0;
    if ((CWP(twp)->flag & 0x80) && !(twp->flag & 4) && (CWP(twp)->flag & 8)
        && (CWP(twp)->hit_cwp->info[CWP(twp)->hit_num].b05 & 0xF)) {
        func_8C0344DC(twp, &pos);
        dir = mwp->spd;
        if (njScalor(&dir) != 0.0f) {
            njUnitVector(&dir);
        }
        ip = njInnerProduct(&pos, &dir);
        if (ip < 0.0f) {
            mwp->spd.x = 0.0f;
            mwp->spd.z = 0.0f;
        }
        if (pwp->spd.x >= pwp->p.run_speed) {
            ret = 2;
        } else {
            ret = 1;
        }
    }
    if (twp->flag & 2) {
        twp->flag &= ~3;
        twp->pos.x += mwp->spd.x;
        twp->pos.z += mwp->spd.z;
    } else {
        flag = twp->flag;
        twp->flag &= ~3;
        hit = 0;
        lcp = B_8C754EF4;
        cp = pwp->cstsp;
        func_8C057B40();
        twp->ang.z ? njRotateZ(NULL, twp->ang.z) : (void)0;
        twp->ang.x ? njRotateX(NULL, twp->ang.x) : (void)0;
        -twp->ang.y ? njRotateY(NULL, -twp->ang.y) : (void)0;
        v.x = v.z = 0.0f;
        v.y = 1.0f;
        cp->angx = twp->ang.x;
        cp->angz = twp->ang.z;
        njCalcVector(NULL, &v, &cp->tnorm);
        v.x = v.z = 0.0f;
        v.y = pwp->p.height * 0.5f;
        njCalcVector(NULL, &v, &ofs);
        v.x = v.z = 0.0f;
        v.y = pwp->p.height * 0.5f - pwp->p.pos_error;
        njCalcVector(NULL, &v, &pos);
        njPopMatrix(1);
        cp->radius = pwp->p.height * 0.5f;
        cp->pos.x = twp->pos.x + pos.x;
        cp->pos.y = twp->pos.y + pos.y;
        cp->pos.z = twp->pos.z + pos.z;
        cp->spd = mwp->spd;
        func_8C028BFE(cp);
        for (i = 0; i < B_8C754EF0; i++) {
            if (func_8C029B00(cp, lcp[i].pObject, lcp[i].slAttribute)) {
                ang = VectorAngle(&cp->tnorm, &cp->normal2, NULL);
                if (ang <= 0x2C00 || cp->tnorm.y < cp->normal2.y) {
                    if (pwp->spd.y <= 0.0f || (pwp->spd.y > 0.0f && !(flag & 3))) {
                        hit = 1;
                        if (mwp->spd.x != 0.0f || mwp->spd.z != 0.0f) {
                            ax = SA1_FTOI(func_8C10D058(cp->normal2.z) * 10430.381f);
                            az = -SA1_FTOI(func_8C10D058(cp->normal2.x) * 10430.381f);
                            if (cp->normal2.y < 0.0f) {
                                ax2 = (Uint16)(0x8000 - ax);
                                az2 = (Uint16)(0x8000 - az);
                                d1 = DiffAngle(twp->ang.x, ax2);
                                d2 = DiffAngle(twp->ang.z, az2);
                                if (d1 < d2) {
                                    cp->angx = ax2;
                                    cp->angz = az;
                                } else {
                                    cp->angx = ax;
                                    cp->angz = az2;
                                }
                            }
                            /* unused read (see the note above) */
                            obj = lcp[i].pObject;
                            PC_ONLY((void)obj;)
                            twp->ang.x = AdjustAngle(twp->ang.x, cp->angx, 0x1000);
                            twp->ang.z = AdjustAngle(twp->ang.z, cp->angz, 0x1000);
                        }
                        mwp->ang_aim.x = cp->angx;
                        mwp->ang_aim.z = cp->angz;
                        cp->pos.x = cp->pshbk.x + cp->anaspdh.x;
                        cp->pos.y = cp->pshbk.y + cp->anaspdh.y;
                        cp->pos.z = cp->pshbk.z + cp->anaspdh.z;
                        tp = lcp[i].pTask;
                        if (tp != NULL) {
                            pwp->ttp = tp;
                            func_8C07506E(twp, pwp, cp, &ofs);
                        }
                        v.x = cp->pos.x - ofs.x;
                        v.y = cp->pos.y - ofs.y;
                        v.z = cp->pos.z - ofs.z;
                        dir.x = twp->pos.x - v.x;
                        dir.y = twp->pos.y - v.y;
                        dir.z = twp->pos.z - v.z;
                        if (njScalor2(&dir) > 0.0001f) {
                            twp->pos = v;
                        }
                        cp->spd.x = cp->spd.y = cp->spd.z = 0.0f;
                        twp->flag |= 1;
                    }
                } else {
                    dir.x = -mwp->spd.x;
                    dir.y = -mwp->spd.y;
                    dir.z = -mwp->spd.z;
                    if (njScalor(&dir) != 0.0f) {
                        njUnitVector(&dir);
                        ang = VectorAngle(&dir, &cp->normal2, NULL);
                        if (ang <= 0x2C00) {
                            if (pwp->spd.x >= pwp->p.crash_speed) {
                                ret = 2;
                            } else {
                                ret = 1;
                            }
                        }
                    }
                    cp->pos = cp->pshbk;
                    twp->pos.x = cp->pshbk.x - ofs.x;
                    twp->pos.y = cp->pshbk.y - ofs.y;
                    twp->pos.z = cp->pshbk.z - ofs.z;
                    cp->spd = cp->anaspdh;
                    mwp->spd = cp->anaspdh;
                }
            }
        }
        if (hit != 1) {
            twp->pos.x += mwp->spd.x;
            twp->pos.y += mwp->spd.y;
            twp->pos.z += mwp->spd.z;
        }
    }
    return ret;
}
#else
ASM_STUB(Sint32 func_8C0771C4(taskwk *twp, motionwk *mwp, playerwk *pwp))
#endif
