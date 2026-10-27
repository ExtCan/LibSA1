/*
 * Player common (1ST_READ 8C072B44-8C0730D8): PSetPosition.
 *
 * PSetPosition resolves the CCL (object) collision of a player and then runs
 * the land collision core func_8C0730D8. Returns a status for the mains:
 * 1 = pushed by an object (wall slide / turn around a round obstacle),
 * 2 = crash (spd.x >= 15, or >= crash_speed head-on into the wall: speed
 * cleared), 3 = ran into an object of class 12 that faces the player
 * (DiffAngle < 0x3000), else the land collision status. When the player is
 * in the air (twp->flag & 2 set, & 1 clear) floor_normal is rebuilt from
 * twp->ang and the player is pushed 0.5 down along it.
 *
 * NON_MATCHING (630 vs 629 instructions, same control flow, frame, stack
 * slots and callee-saved allocation: R8 = 2, R9 = hinfo/ang/otwp, R10 = 1,
 * R11 = ret, FR15 = 0.0f). Remaining differences:
 *  - the original loads CWP(twp)->info before computing hinfo (pure
 *    scheduling: source order "info = ...; hinfo = ...;");
 *  - dir.x/dir.z for the njSqrt are in FR14/FR12 (here FR12/FR14);
 *  - the original evaluates hinfo->attr & 0xF once for the dead
 *    "== 4 ... == 5" test (the form "(... & 0xF) == 4 && (... & 0xF) == 5"
 *    reproduces it);
 *  - the original keeps twp->flag in an int (no EXTS.W; "Sint32 flag"
 *    reproduces that block exactly).
 * Each of the three source forms that fix the last three local differences
 * changes SHC's priority-based allocation here (the njCalcVector literal gets
 * a callee-saved register and hinfo/ang/otwp or the constant 2 are spilled),
 * so the body keeps the forms that preserve the allocation. Declaration order
 * has no effect (checked); "register" moves the variable to R14.
 *
 * Literal pools: the original translation unit continues into func_8C0730D8
 * (PSetPosition's last pool is at 8C073120, after func_8C0730D8's BRA) and
 * starts PSetPosition with func_8C072946's pending pool entries, which makes
 * SHC dump the first pool at 8C072C46. The two extra stubs below (8C072806,
 * 8C0728A0; never called here, ignored by the splicer like every stub) leave
 * a pending __asm_stub_sink entry far enough back that the three
 * mid-function pools land after the same BRAs as in the original; without
 * them the last pool is dumped inside the "twp->flag & 2" block and the two
 * BT/BF there become BF+BRA pairs.
 */

#include "sa1/sa1.h"

ASM_STUB(void func_8C07256A(motionwk *mwp, playerwk *pwp))
ASM_STUB(void func_8C072806(taskwk *twp, motionwk *mwp, playerwk *pwp))
ASM_STUB(void func_8C0728A0(taskwk *twp, motionwk *mwp, playerwk *pwp))
ASM_STUB(void func_8C072946(taskwk *twp, motionwk *mwp, playerwk *pwp))

#ifdef NON_MATCHING
Sint32 PSetPosition(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_POINT3 dir;
    NJS_POINT3 push;
    CCL_INFO *info;
    taskwk *otwp;
    colliwk *hit;
    Float ip;
    Angle a;
    Sint32 ret;
    Sint32 st;

    ret = 0;
    pwp->wall_normal.z = 0.0f;
    pwp->wall_normal.y = 0.0f;
    pwp->wall_normal.x = 0.0f;
    pwp->last_attr = pwp->attr;
    pwp->attr = 0;
    if ((CWP(twp)->flag & 0x80) && !(twp->flag & 4) && (CWP(twp)->flag & 8)
        && (CWP(twp)->hit_cwp->info[CWP(twp)->hit_num].b05 & 0xF)) {
        CCL_INFO *hinfo;

        hinfo = &CWP(twp)->hit_cwp->info[CWP(twp)->hit_cwp->test_num];
        info = CWP(twp)->info;
        dir = mwp->spd;
        func_8C0344DC(twp, &push);
        pwp->wall_normal = push;
        if (!njScalor(&push)) {
            push.x = 0.0f;
            push.y = 0.0f;
            push.z = 0.0f;
        } else {
            njUnitVector(&push);
        }
        if (!njScalor(&dir)) {
            dir.z = dir.y = dir.x = 0.0f;
        } else {
            njUnitVector(&dir);
        }
        ip = njInnerProduct(&dir, &push);
        if (pwp->spd.x >= 15.0f) {
            ret = 2;
            PClearSpeed(mwp, pwp);
        } else if (pwp->spd.x >= pwp->p.crash_speed && ip < -0.97999997f) {
            ret = 2;
            mwp->spd.z = 0.0f;
            mwp->spd.x = 0.0f;
        } else if (pwp->spd.x >= pwp->p.run_speed && ip < 0.0f) {
            ret = 1;
            if (((hinfo->attr & 0xF) == 0 || (hinfo->attr & 0xF) == 1)
                && hinfo->a > info->a
                && (hinfo->flag & 3) <= ((info->flag & 0xC) >> 2)) {
                Float r;
                Float d;
                Angle ang;

                r = info->a + hinfo->a;
                d = njSqrt(SA1_FMAC(dir.x, dir.x, dir.z * dir.z));
                ang = SA1_FTOI(func_8C10D008(d / r) * 10430.381f);
                a = SA1_FTOI(atan2f(push.z, push.x) * 10430.381f) + 0x8000;
                if (SubAngle(a, SA1_FTOI(atan2f(dir.z, dir.x) * 10430.381f)) < 0) {
                    ang = 0x10000 - ang;
                }
                func_8C057B40();
                -ang ? njRotateY(NULL, -ang) : (void)0;
                dir = mwp->spd;
                njCalcVector(NULL, &dir, &push);
                njPopMatrix(1);
                twp->ang.y += ang;
                mwp->spd.x = push.x;
                mwp->spd.z = push.z;
            } else if ((hinfo->attr & 0xF) == 4) {
                if ((hinfo->attr & 0xF) == 5) {
                    if ((CWP(twp)->flag & 0x80) && !(twp->flag & 4)
                        && !(CWP(twp)->hit_cwp->info[CWP(twp)->hit_num].attr & 0x800)
                        && (CWP(twp)->flag & 8)) {
                        mwp->spd.z = 0.0f;
                        mwp->spd.x = 0.0f;
                    } else {
                        func_8C072946(twp, mwp, pwp);
                    }
                }
            }
        } else {
            ret = 1;
            if (ip < 0.0f) {
                if (func_8C06C880(twp) & 0x400) {
                    mwp->spd.x = -(pwp->spd.x * push.x);
                    mwp->spd.z = -(pwp->spd.x * push.z);
                } else if ((CWP(twp)->flag & 0x80) && !(twp->flag & 4)
                    && !(CWP(twp)->hit_cwp->info[CWP(twp)->hit_num].attr & 0x800)
                    && (CWP(twp)->flag & 8)) {
                    dir.x += push.x;
                    dir.y = 0.0f;
                    dir.z += push.z;
                    if (njScalor(&dir) != 0.0f) {
                        njUnitVector(&dir);
                        mwp->spd.x = pwp->spd.x * dir.x;
                        mwp->spd.z = pwp->spd.x * dir.z;
                    } else {
                        mwp->spd.x = 0.0f;
                        mwp->spd.z = 0.0f;
                    }
                } else {
                    func_8C072946(twp, mwp, pwp);
                }
            }
        }
    } else if (CWP(twp)->flag & 2) {
        hit = CWP(twp)->hit_cwp;
        if (hit->mytask != NULL) {
            otwp = hit->mytask->twp;
            if (otwp != NULL && (Uint8)otwp->id == 12) {
                Sint16 flag;

                flag = twp->flag;
                if ((flag & 1) != 0 && pwp->spd.y <= 0.0f
                    && (pwp->spd.x >= pwp->p.run_speed || (flag & 0x100) != 0)) {
                    if (DiffAngle(0x4000 - twp->ang.y, otwp->ang.y) < 0x3000) {
                        ret = 3;
                        PClearSpeed(mwp, pwp);
                    }
                }
            }
        }
    }
    if (twp->flag & 2) {
        st = func_8C0730D8(twp, mwp, pwp);
        if (!ret) {
            ret = st;
        }
        if (!(twp->flag & 1)) {
            func_8C057B40();
            twp->ang.z ? njRotateZ(NULL, twp->ang.z) : (void)0;
            twp->ang.x ? njRotateX(NULL, twp->ang.x) : (void)0;
            -twp->ang.y ? njRotateY(NULL, -twp->ang.y) : (void)0;
            dir.x = dir.z = 0.0f;
            dir.y = 1.0f;
            njCalcVector(NULL, &dir, &push);
            pwp->floor_normal = push;
            dir.x = dir.z = 0.0f;
            dir.y = -0.5f;
            njCalcVector(NULL, &dir, &push);
            njPopMatrix(1);
            twp->pos.y += push.y;
            mwp->spd.y = 0.0f;
        }
    } else {
        st = func_8C0730D8(twp, mwp, pwp);
        if (!ret) {
            ret = st;
        }
    }
    return ret;
}
#else
ASM_STUB(Sint32 PSetPosition(taskwk *twp, motionwk *mwp, playerwk *pwp))
#endif

ASM_STUB(Sint32 func_8C0730D8(taskwk *twp, motionwk *mwp, playerwk *pwp))
