#include "sa1/pending.h"

/*
 * 1ST_READ 8C0D10BC-8C0D1672: E-102 Gamma's carry / throw handlers (chunk
 * 8C0CBE00-8C0D2080, functions 16-19). All are called with (twp, mwp, pwp)
 * by E102ModeMachine (8C0CD0A8) and return 1 when they changed the
 * player's mode, else 0. They are the E-102 versions of Sonic's
 * 8C0B184C/8C0B199A/8C0B1C56/8C0B1D60 (code_8C0B16BC.c, code_8C0B1C56.c).
 *
 *  func_8C0D10BC  carrying: the action button (D_8C19E3A0 pressed) throws.
 *                 In the air mode 27 (action 37 when the player's target
 *                 list is not empty, func_8C06D118, else 38); on the ground
 *                 slower than jog speed: put-down (SE 1190, mode 16,
 *                 action 39, speeds cleared); else mode 20 (action 35/36).
 *  func_8C0D11DC  throw / put-down timing: at the release frame of the
 *                 throw actions (35/37 at frame 20/7: aimed at the nearest
 *                 target, speed 3; 36/38 at 24/6: fixed player-space speed
 *                 (2, 0.8, 0)) the held object (pwp->htp) gets its speed
 *                 (player space -> global, 8C078EB0) and SE 1191; action 39
 *                 frame 12 puts it down. When nothing is held it only
 *                 clears TWF_HOLD.
 *  func_8C0D14D6  action button held: pushing against an object (class
 *                 from func_8C06CFCC; 0 = nothing to push, returns 0):
 *                 0x800 objects mode 28/action 28, straight ahead (4)
 *                 mode 9/action 12, else mode 26/action 27. Button not
 *                 held: 0x200 objects mode 15/action 21, otherwise drop
 *                 the held object and stand (8C0CD09C).
 *  func_8C0D15C0  drop the held object (TWF_HOLD): clears its TWF_REQUEST,
 *                 throws a 0x200-class object with the player-space speed
 *                 (1, 1, 0) (rotated by the player's angles) and clears
 *                 pwp->htp.
 *
 * Literal pools / translation unit: the original object starts at 8C0CBE00
 * and SHC shares one literal pool across its functions. When func_8C0D10BC
 * starts, func_8C0D100A's seven pool entries (first use 8C0D1014) are
 * pending; the pool is dumped inside func_8C0D10BC (8C0D1108, after the BRA
 * of the "action 38" store), which turns its button test into the far form
 * "BF/S; BRA; NOP", and func_8C0D10BC's later entries are dumped at
 * 8C0D1224 inside func_8C0D11DC, which gives its far forms. Stubs cannot
 * recreate that state (an ASM_STUB adds a single pool entry), so the file
 * starts with a layout model of the predecessor (tu_pad_8C0D100A +
 * tu_model_8C0D100A, SHC only, see below): with it all four functions get
 * the original pool positions and func_8C0D10BC, func_8C0D11DC and
 * func_8C0D15C0 match in the same build.
 */

/* func_8C0D15C0 is reached with BSR from func_8C0D14D6 */
void func_8C0D15C0(taskwk *twp, motionwk *mwp, playerwk *pwp);

/* E-102 return to standing (src/1st_read/code_8C0CD09C.c) */
void func_8C0CD09C(taskwk *twp, motionwk *mwp, playerwk *pwp);

/* stick direction vs. CCL push-out class (src/1st_read/code_8C06CFCC.c
 * defines it with (taskwk *) only; the game passes mwp in R5 here, as in
 * code_8C06EFCE.c and code_8C0A966C.c) */
Sint32 func_8C06CFCC(taskwk *twp, motionwk *mwp);

/* byte flag, only used by the E-102 chunk: positional SE enable */
extern Sint8 B_8C7A2EA8;

#if !defined(TARGET_PC)
/*
 * Translation-unit layout model (SHC only; never spliced: these names have
 * no address in the image, as tu_gap_8C0D0B16 in code_8C0D0B16.c).
 * tu_model_8C0D100A is a copy of func_8C0D100A (matched in
 * code_8C0D0B16.c): its pool entries (func_8C06CB40, 12.0f,
 * func_8C06C880, func_8C06CF60, B_8C7A2EA8, func_8C09413A,
 * func_8C0CD09C) are what the original has pending when func_8C0D10BC
 * starts (nothing was pending before func_8C0D100A: func_8C0D0F0C's pool
 * is dumped at 8C0D0F88). tu_pad_8C0D100A (10 bytes) puts the model at
 * the original offset 0x0A modulo the 32-byte pool alignment
 * (8C0D100A - 8C0CBE00 = 0x520A); without it the pool is dumped later in
 * func_8C0D10BC (8C0D11C0) and func_8C0D11DC does not match.
 */
Sint32 func_8C06CF60(taskwk *twp, motionwk *mwp);

void tu_pad_8C0D100A(Sint32 *p)
{
    p[0] = 0;
    p[1] = 0;
    p[2] = 0;
}

Sint32 tu_model_8C0D100A(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    Sint32 kind;

    func_8C06CB40(twp, mwp);
    if (pwp->mj.nframe >= 12.0f) {
        if ((kind = func_8C06C880(twp)) != 0) {
            switch (func_8C06CF60(twp, mwp)) {
            case 4:
                if (kind & 0x200) {
                    twp->mode = 17;
                    if (B_8C7A2EA8) {
                        func_8C09413A(0x4A5, 0, 0, 0, twp->pos.x, twp->pos.y, twp->pos.z);
                    }
                    pwp->htp = CWP(twp)->hit_cwp->mytask;
                    pwp->htp->twp->flag |= TWF_REQUEST;
                    twp->flag |= TWF_HOLD;
                    twp->flag &= ~8;
                    return 1;
                }
                break;
            }
            func_8C0CD09C(twp, mwp, pwp);
        }
    }
    return 0;
}
#endif

/*
 * Same shape as Sonic's 8C0B184C. Needs the pending pool entries of the
 * layout model above: without them the button test compiles to BT/S
 * (98 instead of 100 instructions).
 */
Sint32 func_8C0D10BC(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    if (B_8C754C08[(Uint8)twp->counter.b[0]].press & D_8C19E3A0) {
        if (!(twp->flag & TWF_GROUND)) {
            twp->mode = 27;
            if (func_8C06D118(twp->counter.b[0])) {
                pwp->mj.reqaction = 37;
            } else {
                pwp->mj.reqaction = 38;
            }
        } else if (pwp->spd.x < pwp->p.jog_speed) {
            if (B_8C7A2EA8) {
                func_8C09413A(1190, 0, 0, 0, twp->pos.x, twp->pos.y, twp->pos.z);
            }
            twp->mode = 16;
            pwp->mj.reqaction = 39;
            PClearSpeed(mwp, pwp);
        } else {
            twp->mode = 20;
            if (func_8C06D118(twp->counter.b[0])) {
                pwp->mj.reqaction = 35;
            } else {
                pwp->mj.reqaction = 36;
            }
        }
        return 1;
    }
    return 0;
}

/*
 * The pool entries left pending by func_8C0D10BC make SHC dump the pool
 * at 8C0D1224, which gives the far forms "BF; BRA; NOP" of the
 * htp == NULL test and of the first SE test.
 * Matching notes: each branch ends in its own return (as Sonic's 8C0B199A);
 * the 0.0f stored to v.z is a local assigned after the htp == NULL test
 * (SHC then keeps it in FR14 and v.x in FR13; a literal swaps the two);
 * z is v.z read back for the squared length.
 */
Sint32 func_8C0D11DC(taskwk *twp, motionwk *mwp, playerwk *pwp)
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
    if (((pwp->mj.reqaction == 35 && pwp->mj.nframe == 20.0f)
            || (pwp->mj.reqaction == 37 && pwp->mj.nframe == 7.0f))
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
            v.x *= 3.0f;
            v.y *= 3.0f;
            PConvertVector_P2G(twp, &v);
            pwp->htp->mwp->spd = v;
        }
        twp->flag &= ~TWF_HOLD;
        pwp->htp = NULL;
        if (B_8C7A2EA8) {
            func_8C09413A(1191, 0, 0, 0, twp->pos.x, twp->pos.y, twp->pos.z);
        }
        return 1;
    }
    if ((pwp->mj.reqaction == 36 && pwp->mj.nframe == 24.0f)
            || (pwp->mj.reqaction == 38 && pwp->mj.nframe == 6.0f)) {
        htwp = pwp->htp->twp;
        if (htwp != NULL) {
            htwp->flag &= ~TWF_REQUEST;
            v.x = 2.0f;
            v.y = 0.799999957f;
            v.z = zero;
            PConvertVector_P2G(twp, &v);
            pwp->htp->mwp->spd = v;
        }
        twp->flag &= ~TWF_HOLD;
        pwp->htp = NULL;
        if (B_8C7A2EA8) {
            func_8C09413A(1191, 0, 0, 0, twp->pos.x, twp->pos.y, twp->pos.z);
        }
        return 1;
    }
    if (pwp->mj.reqaction == 39 && pwp->mj.nframe == 12.0f) {
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

#ifdef NON_MATCHING
/*
 * NON_MATCHING (complete, functionally faithful): 87 of 87 instructions,
 * control flow, branch layout and scheduling are the original's; 11 differ
 * only in temporary registers. After the 0x800 mask (R2) the original's
 * whole push block avoids R2: the func_8C06C880 address is in R3 (R2 here;
 * the second call, in the not-held path, uses R2 in both), the mask in R2
 * (R3 here) and the mode/action constants in R1/R3 (R1/R2 and R3/R2 here).
 * Sonic's 8C0B1C56 (code_8C0B1C56.c) and the Tails/Knuckles/Amy/Big
 * versions 8C0BD336, 8C0C6D36, 8C0DC662, 8C0E54E4 show the same R2-free
 * push block, so it is one source idiom not found yet.
 * Found: the "straight ahead" (4) block ends in its own return 1 (it gets
 * the empty BRA delay slot and the action constant first, the default
 * block the filled slot, as in the original; a switch gives the opposite).
 * Tried without effect on the registers: "(kind = ...) & 0x800", Uint32 /
 * int res, TWF_HOLD, "== 0x800", (Uint32) casts, store order in the three
 * blocks, unsigned/reordered operands of the button test (they move the
 * func_8C06CFCC address to R1), early "res == 0" return, while/do loops,
 * a #pragma inline helper for the push block, the negated layout.
 * The D_8C19E3A0 test needs Sint32 operands (func_8C06CFCC address in R2,
 * as Sonic's 8C0B16BC); the not-pushing return 0 is the last statement.
 */
Sint32 func_8C0D14D6(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    Sint32 res;

    if ((Sint32)B_8C754C08[(Uint8)twp->counter.b[0]].on & (Sint32)D_8C19E3A0) {
        res = func_8C06CFCC(twp, mwp);
        if (res != 0) {
            if (func_8C06C880(twp) & 0x800) {
                twp->mode = 28;
                pwp->mj.reqaction = 28;
            } else {
                if (res == 4) {
                    twp->mode = 9;
                    pwp->mj.reqaction = 12;
                    return 1;
                }
                twp->mode = 26;
                pwp->mj.reqaction = 27;
            }
            return 1;
        }
    } else {
        if (func_8C06C880(twp) & 0x200) {
            twp->mode = 15;
            pwp->mj.reqaction = 21;
        } else {
            func_8C0D15C0(twp, mwp, pwp);
            func_8C0CD09C(twp, mwp, pwp);
        }
        return 1;
    }
    return 0;
}
#else
ASM_STUB(Sint32 func_8C0D14D6(taskwk *twp, motionwk *mwp, playerwk *pwp))
#endif

/*
 * The rotations are written as "ang ? njRotateZ(NULL, ang) : (void)0" (the
 * matched form of func_8C06CCA0, code_8C06CCA0.c): SHC keeps the conditional
 * angle in the stack slot @0 (store in the delay slot, reload before the
 * call) and gives the 2nd and 3rd angle R3; an if with an unsigned local
 * gives R2.
 */
void func_8C0D15C0(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_POINT3 v;

    if (twp->flag & TWF_HOLD) {
        pwp->htp->twp->flag &= ~TWF_REQUEST;
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
