#include "common.h"
#include "ninja.h"
#include "sa1/sa1.h"

/*
 * 1ST_READ 8C0B11C0-8C0B16BC: Sonic mode checks (chunk 8C0B11C0-8C0B2338,
 * functions 1-6). All are called by SonicModeMachine (8C0A966C) with
 * (twp, mwp, pwp); each returns 1 when it changed twp->mode (and requested
 * an action in pwp->mj.reqaction), 0 otherwise.
 *
 * Sonic's scratch words: pwp->free.w[1] (0x7E) spin dash charge timer,
 * pwp->work.f spin dash charge speed.
 *
 *  func_8C0B11C0  (twp->flag & 0x100) -> mode 8; holding an object of class
 *                 0x200 -> mode 28; otherwise (after 8C0B1D60 when holding)
 *                 mode 12 / action 18.
 *  func_8C0B122E  no stick input and spd.x == 0: back to standing or the
 *                 holding stand modes 23/24.
 *  func_8C0B12E0  jump button pressed: start spin dash charge (mode 4).
 *  func_8C0B1352  jump button released while charging (flag 0x200): light
 *                 speed dash (equipment & 4 and a target) or spin dash run.
 *  func_8C0B14E0  spin dash charge: release when fully charged (needs
 *                 equipment bits), timeout at 300, button released, or keep
 *                 charging (work.f += 0.4 up to 10).
 *  func_8C0B1686  spd.x below run_speed: back to stand/walk (mode 1/2).
 *
 * Matching notes:
 *  - pwp->equipment is tested through (Sint16) (no EXTU.W), as in the other
 *    Sonic files.
 *  - 8C0B11C0: the 0x800 test must be an else-if of the 0x100 test.
 *  - 8C0B1352: the target pointer and the two colliwk pointers have stack
 *    homes in the original (12-byte frame, written but never read back);
 *    SHC does that for variables assigned inside an expression. The
 *    collision flag updates are written so the assignment is not read in
 *    the same expression (well-defined C).
 *  - 8C0B14E0: if / else if chain; the button test is signed
 *    ((Sint32) casts, like code_8C06CCA0.c), which decides the register
 *    allocation of the release block.
 *  - 8C0B1686: reqaction = 29 written in both arms (SHC hoists the
 *    constant into the branch delay slot); 8C0B14E0 has the same code with
 *    a single store after the if.
 *  - !(x > 0.0f) keeps the FCMP/GT semantics for NaN on the PC.
 */

Sint32 func_8C06C880(taskwk *twp);                                      /* held/touched object class (code_8C06C880.c) */
taskwk *func_8C0AF5A0(taskwk *twp, playerwk *pwp);                      /* light speed dash target (code_8C0AF5A0.c) */
void func_8C0A9660(taskwk *twp, motionwk *mwp, playerwk *pwp);          /* back to standing (code_8C0A9660.c) */
void func_8C0B2EA0(taskwk *twp, motionwk *mwp, playerwk *pwp);          /* spin dash release (charged) */
void func_8C094330(Sint32 se, taskwk *twp, Sint32 a, Sint32 b, Sint32 c); /* SE owned by a task */
void func_8C0945A0(Sint32 se);                                          /* stop SE */
void func_8C021700(void *param);                                        /* camera function */
void func_8C01AD2C(void (*func)(void *), Sint8 adjust, Sint8 direct);   /* set event camera function */

void func_8C0B1D60(taskwk *twp, motionwk *mwp, playerwk *pwp);

/* 8C0B11C0 */
Sint32 func_8C0B11C0(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    if (!(twp->flag & 3)) {
        if (twp->flag & 0x100) {
            twp->mode = 8;
            return 1;
        } else if (twp->flag & 0x800) {
            if (func_8C06C880(twp) & 0x200) {
                twp->mode = 28;
                pwp->mj.reqaction = 57;
                return 1;
            }
            func_8C0B1D60(twp, mwp, pwp);
        }
        twp->mode = 12;
        pwp->mj.reqaction = 18;
        return 1;
    }
    return 0;
}

/* 8C0B122E */
Sint32 func_8C0B122E(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    if (GetAnalog(twp, NULL, NULL) == 0 && pwp->spd.x == 0.0f) {
        if (twp->flag & 0x800) {
            if (func_8C06C880(twp) & 0xC00) {
                twp->mode = 24;
                pwp->mj.reqaction = 54;
            } else {
                twp->mode = 23;
                pwp->mj.reqaction = 37;
            }
        } else {
            func_8C0A9660(twp, mwp, pwp);
        }
        return 1;
    }
    return 0;
}

/* 8C0B12E0 */
Sint32 func_8C0B12E0(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    if (B_8C754C08[(Uint8)twp->counter.b[0]].press & D_8C19E38C) {
        twp->mode = 4;
        pwp->mj.reqaction = 14;
        twp->flag |= 0x500;
        pwp->free.w[1] = 0;
        if (pwp->spd.x > 2.0f) {
            pwp->work.f = pwp->spd.x;
        } else {
            pwp->work.f = 2.0f;
        }
        func_8C094330(0x2D8, twp, 1, 0, 2);
        return 1;
    }
    return 0;
}

/* 8C0B1352 */
Sint32 func_8C0B1352(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    taskwk *ttwp;
    colliwk *cwp1;
    colliwk *cwp2;

    if (!(B_8C754C08[(Uint8)twp->counter.b[0]].on & D_8C19E38C) && (twp->flag & 0x200)) {
        if (((Sint16)pwp->equipment & 4) && (ttwp = func_8C0AF5A0(twp, pwp)) != NULL) {
            twp->flag &= ~0x200;
            twp->mode = 44;
            pwp->mj.reqaction = 15;
            pwp->spd.y = 16.0f;
            twp->flag |= 0x500;
            func_8C0940DA(0x2D5, 0, 0, 0);
            pwp->free.w[1] = 0;
            func_8C01AD2C(func_8C021700, 0, 0);
            (cwp1 = CWP(twp))->info->flag = (CWP(twp)->info->flag & ~3) | 3;
            (cwp2 = CWP(twp))->info->flag = (CWP(twp)->info->flag & ~0xC) | 0xC;
        } else {
            twp->flag &= ~0x200;
            twp->mode = 6;
            pwp->mj.reqaction = 64;
            pwp->free.w[2] = 10;
            pwp->free.w[3] = 0;
            pwp->spd.x = 16.0f;
            twp->flag &= ~0x500;
            twp->flag |= 0x400;
            func_8C0940DA(0x2D5, 0, 0, 0);
            func_8C0945A0(0x45D);
            func_8C0940DA(0x45E, 0, 0, 0);
        }
        return 1;
    }
    return 0;
}

/* 8C0B14E0 */
Sint32 func_8C0B14E0(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    if ((pwp->free.w[1] >= 120 && ((Sint16)pwp->equipment & 3) == 3)
        || (pwp->free.w[1] >= 210 && ((Sint16)pwp->equipment & 2))) {
        twp->flag |= 0x200;
        func_8C0B2EA0(twp, mwp, pwp);
        pwp->free.w[1] = 0;
        if (!(pwp->spd.x > 0.0f)) {
            twp->mode = 1;
            pwp->waittimer = 0;
        } else {
            twp->mode = 2;
        }
        pwp->mj.reqaction = 29;
        twp->flag &= ~0x500;
        func_8C0945A0(0x2D8);
        func_8C0945A0(0x2D4);
        func_8C0940DA(0x45D, 0, 0, 0);
        return 1;
    } else if (pwp->free.w[1] >= 300) {
        pwp->free.w[1] = 0;
        if (!(pwp->spd.x > 0.0f)) {
            twp->mode = 1;
            pwp->waittimer = 0;
        } else {
            twp->mode = 2;
        }
        pwp->mj.reqaction = 29;
        twp->flag &= ~0x500;
        func_8C0945A0(0x2D8);
        func_8C0945A0(0x2D4);
        return 1;
    } else if (!((Sint32)B_8C754C08[(Uint8)twp->counter.b[0]].on & (Sint32)D_8C19E38C)) {
        twp->mode = 5;
        pwp->mj.reqaction = 15;
        pwp->spd.x = pwp->work.f;
        func_8C0940DA(0x2D9, 0, 0, 0);
        func_8C0945A0(0x2D8);
        func_8C0945A0(0x2D4);
        pwp->free.w[1] = 0;
        pwp->work.f = 0.0f;
        return 1;
    }
    if (10.0f > pwp->work.f) {
        pwp->work.f += 0.39999998f;
    }
    if ((Sint16)pwp->equipment & 2) {
        func_8C094330(0x2D4, twp, 1, 0, 2);
    } else {
        func_8C094330(0x2D8, twp, 1, 0, 2);
    }
    return 0;
}

/* 8C0B1686 */
Sint32 func_8C0B1686(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    if (pwp->spd.x < pwp->p.run_speed) {
        if (!(pwp->spd.x > 0.0f)) {
            twp->mode = 1;
            pwp->waittimer = 0;
            pwp->mj.reqaction = 29;
        } else {
            twp->mode = 2;
            pwp->mj.reqaction = 29;
        }
        return 1;
    }
    return 0;
}

ASM_STUB(void func_8C0B1D60(taskwk *twp, motionwk *mwp, playerwk *pwp))
