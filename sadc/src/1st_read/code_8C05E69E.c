#include "common.h"
#include "ninja.h"
#include "sa1/sa1.h"

/*
 * 8C05E69E-8C05EB48: jump panel (O_JPanel) player helpers
 * (chunk asm/1st_read/text/8C05E680.src, functions 2-6).
 *
 * B_8C78C4E0[10] holds the jump panel tasks of the act in sequence order.
 * A panel's twp->mode is 0 (idle), 1 (touched) or 2 (the next target);
 * twp->wtimer is its index in the list (0 = start panel), twp->timer.l a
 * frame counter of the panel task (8C05E6E8 tests it against 30).
 *
 * 8C05E69E  reset all panels to mode 0
 * 8C05E6C4  the first player (counter.b[0] == 0) touches a jump panel
 *           (hit object id 8)
 * 8C05E6E8  -1/0/1 jump panel state for the mode machines
 * 8C05E788  touch the hit panel and make the next one the target
 * 8C05E7D4  fly the player towards the target panel (mains)
 *
 * Matching notes: the reset loop needs `continue` (BRA-to-test loop shape);
 * 8C05E6C4/8C05E6E8 need early returns and the hit colliwk in its own local
 * (mytask then goes through R3); 8C05E788 needs the two `n = 0` branches
 * (0 hoisted into R7, tails merged) and the list in a local.
 */

extern task *B_8C78C4E0[10];            /* jump panel tasks */
void func_8C05EFBA(task *tp);           /* panel landing effect task */

void func_8C05E69E(void)
{
    Sint32 i;

    for (i = 0; i < 10; i++) {
        if (B_8C78C4E0[i] == NULL) {
            continue;
        }
        B_8C78C4E0[i]->twp->mode = 0;
    }
}

Sint32 func_8C05E6C4(taskwk *twp)
{
    colliwk *hcwp;
    taskwk *htwp;

    hcwp = CWP(twp)->hit_cwp;
    if (hcwp == NULL || twp->counter.b[0] != 0) {
        return 0;
    }
    htwp = hcwp->mytask->twp;
    if (htwp->id != 8) {
        return 0;
    }
    return 1;
}

Sint32 func_8C05E6E8(taskwk *twp)
{
    taskwk *htwp;
    colliwk *hcwp;

    if (!func_8C05E6C4(twp)) {
        if (twp->counter.b[0] == 0) {
            func_8C05E69E();
        }
        return 0;
    }
    hcwp = CWP(twp)->hit_cwp;
    htwp = hcwp->mytask->twp;
    if (htwp->wtimer == 0) {
        func_8C05E69E();
        return -1;
    }
    if (CWP(twp)->hit_num != 1) {
        func_8C05E69E();
        return 0;
    }
    if ((Float)htwp->timer.l >= 30.0f) {
        func_8C05E69E();
        return 0;
    }
    return 1;
}

void func_8C05E788(taskwk *twp)
{
    taskwk *htwp;
    colliwk *hcwp;
    task **list;
    Sint16 n;

    func_8C05E69E();
    hcwp = CWP(twp)->hit_cwp;
    htwp = hcwp->mytask->twp;
    n = htwp->wtimer;
    htwp->mode = 1;
    list = B_8C78C4E0;
    if (n == 9) {
        n = 0;
    } else if (list[n + 1] == NULL) {
        n = 0;
    } else {
        n++;
    }
    list[n]->twp->mode = 2;
}

/*
 * Fly the player towards the target panel (mode 2): the target point is
 * the panel position plus (0, center_height + 3.8, 0) rotated by the panel
 * angle, relative to the player's first collision shape. While it is
 * farther than one frame of movement (speed at least 9, +0.5 per frame up
 * to 15) the player moves straight at it and turns towards the direction;
 * then it is put 3.3 above the panel with the panel's angles (y negated),
 * the speed is cleared, all panels are reset and the landing effect task
 * starts. Returns the remaining distance / 9 (0 on arrival or without a
 * target panel).
 *
 * NON_MATCHING: complete and functionally faithful (no FMAC in the original).
 * Same instructions as the original except the register choice for the two
 * pointer parameters (14 words): the original keeps mwp in R12 and pwp in
 * R11 (so AdjustAngle is later loaded into R12), this compiles to the
 * opposite. Both have 3 references; SHC breaks the tie in favour of the
 * pointer referenced first (pwp->p.center_height comes before
 * njScalor(&mwp->spd)). Any extra reference to mwp (e.g. a dead load
 * "mwp->acc.x;" anywhere) gives the original registers but then swaps the
 * stack slots of the two saved panel angles (angx sp+0 / angz sp+8 instead
 * of sp+8 / sp+0, 4 words) in every variant tried (dead loads of other
 * pointers in any combination, save order, types, volatile, named rotate
 * temps, register). Shapes needed for the rest: do-while loop over the
 * panel list through a local pointer (a for loop is strength-reduced),
 * "p.x = p.z = 0.0f" (FR13 = 0, FR14 = dist), the ?: rotate idiom (stack
 * temps), "pos - center + v" operand order, scalar far-path angles (they
 * share the slots of the saved angles; an Angle3 gets its own 12 bytes),
 * "if ((tp = ...) != NULL)", and ptwp = NULL before list = B_8C78C4E0.
 */
#ifdef NON_MATCHING
Sint32 func_8C05E7D4(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_VECTOR v;
    NJS_POINT3 p;
    NJS_VECTOR ofs;
    Angle ax;
    Angle ay;
    Angle az;
    Angle angx;
    Angle angz;
    taskwk *ptwp;
    task *tp;
    task **list;
    Float dist;
    Float spd;
    Sint16 i;

    ptwp = NULL;
    list = B_8C78C4E0;
    i = 0;
    do {
        if (list[i] == NULL) {
            return 0;
        }
        if (list[i]->twp->mode == 2) {
            ptwp = list[i]->twp;
            break;
        }
    } while (++i < 10);
    if (ptwp == NULL) {
        return 0;
    }
    p.x = p.z = 0.0f;
    p.y = pwp->p.center_height + 3.8f;
    njPushMatrix(D_8C67C640);
    ptwp->ang.z ? njRotateZ(NULL, ptwp->ang.z) : (void)0;
    ptwp->ang.x ? njRotateX(NULL, ptwp->ang.x) : (void)0;
    ptwp->ang.y ? njRotateY(NULL, ptwp->ang.y) : (void)0;
    njCalcVector(NULL, &p, &v);
    njPopMatrix(1);
    v.x = ptwp->pos.x - CWP(twp)->info->center.x + v.x;
    v.y = ptwp->pos.y - CWP(twp)->info->center.y + v.y;
    v.z = ptwp->pos.z - CWP(twp)->info->center.z + v.z;
    dist = njScalor(&v);
    spd = njScalor(&mwp->spd);
    njUnitVector(&v);
    angx = ptwp->ang.x;
    angz = ptwp->ang.z;
    if (dist <= spd) {
        p.x = p.z = 0.0f;
        p.y = 3.3f;
        njPushMatrix(D_8C67C640);
        ptwp->ang.z ? njRotateZ(NULL, ptwp->ang.z) : (void)0;
        ptwp->ang.x ? njRotateX(NULL, ptwp->ang.x) : (void)0;
        njCalcVector(NULL, &p, &ofs);
        njPopMatrix(1);
        twp->pos.x = ptwp->pos.x + ofs.x;
        twp->pos.y = ptwp->pos.y + ofs.y;
        twp->pos.z = ptwp->pos.z + ofs.z;
        twp->ang.x = angx;
        twp->ang.y = -ptwp->ang.y;
        twp->ang.z = angz;
        PClearSpeed(mwp, pwp);
        twp->flag |= 2;
        func_8C05E69E();
        dist = 0.0f;
        if ((tp = CreateElementalTask(2, 6, func_8C05EFBA)) != NULL) {
            tp->twp->pos = twp->pos;
            tp->twp->ang = ptwp->ang;
        }
    } else {
        if (spd < 9.0f) {
            spd = 9.0f;
        } else if (spd < 15.0f) {
            spd += 0.5f;
        }
        v.x *= spd;
        v.y *= spd;
        v.z *= spd;
        mwp->spd = v;
        twp->pos.x += v.x;
        twp->pos.y += v.y;
        twp->pos.z += v.z;
        az = -SA1_FTOI(atan2f(v.x, v.y) * 10430.381f);
        ax = SA1_FTOI(func_8C10D058(v.z) * 10430.381f);
        ay = SA1_FTOI(atan2f(v.x, -v.z) * 10430.381f);
        twp->ang.x = AdjustAngle(twp->ang.x, ax, 0x1000);
        twp->ang.y = AdjustAngle(twp->ang.y, ay, 0x1000);
        twp->ang.z = AdjustAngle(twp->ang.z, az, 0x1000);
        PConvertVector_G2P(twp, &v);
        pwp->spd = v;
    }
    return SA1_FTOI(dist * 0.111111106f);
}
#else
ASM_STUB(Sint32 func_8C05E7D4(taskwk *twp, motionwk *mwp, playerwk *pwp))
#endif
