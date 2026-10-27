#include "sa1/sa1.h"

/*
 * Knuckles (1ST_READ chunk 8C0C5680-8C0C71A0, functions 14-18 of 18):
 * held-object code, Knuckles' copies of Sonic's 8C0B1C56-8C0B1DE0
 * (code_8C0B1C56.c). pwp->htp is the task of the object Knuckles carries
 * (twp->flag & 0x800 while held). All are called with (twp, mwp, pwp).
 *
 *  func_8C0C6D36  KnucklesModeMachine check (Sonic: 8C0B1C56): with the
 *                 D_8C19E3A0 button held, pushing against an object
 *                 (8C06CFCC) starts the push (modes 40/13/38); otherwise a
 *                 0x200-class object is picked up (mode 27, action 70) or
 *                 the held object is dropped and Knuckles stands. Returns
 *                 0 when the button is held but there is nothing to push.
 *  func_8C0C6DE0  drop the held object (Sonic: 8C0B1D60): clears the
 *                 flags, throws a 0x200-class object with the player-space
 *                 speed (1, 1, 0) and clears pwp->htp.
 *  func_8C0C6E80  per-frame update of the held object (KnucklesMain;
 *                 Sonic: 8C0B1DE0): keeps a 0x200-class object at the hands
 *                 (rotated by the player's angles), drops anything else
 *                 when the player hits another object.
 *  func_8C0C7080  KnucklesModeMachine check: in the air, the attack state
 *                 (flag 0x100) goes to mode 6; otherwise a held 0x200-class
 *                 object gives mode 34 (action 94), anything else is
 *                 dropped and Knuckles falls (mode 9, action 19).
 *                 Returns 0 on the ground.
 *  func_8C0C70EE  KnucklesModeMachine check: no stick input and no speed:
 *                 with a held object mode 30 (action 91) for a 0xC00-class
 *                 object, else mode 29 (action 74); without one Knuckles
 *                 stands (8C0C4F40). Returns 0 otherwise.
 */

/* stick vs. push-out direction class of the CCL hit, 1..4 (code_8C06CFCC.c
 * defines it with one parameter; the call site loads R5 = mwp, as in
 * code_8C06EFCE.c and code_8C0B1C56.c) */
Sint32 func_8C06CFCC(taskwk *twp, motionwk *mwp);
/* Knuckles: back to standing (code_8C0C4C2E.c; Sonic func_8C0A9660) */
extern void func_8C0C4F40(taskwk *twp, motionwk *mwp, playerwk *pwp);

void func_8C0C6DE0(taskwk *twp, motionwk *mwp, playerwk *pwp);

#ifdef NON_MATCHING
/*
 * NON_MATCHING (complete, functionally faithful): 85 of 85 instructions,
 * only 2 differ: the mode constant 38 of the default block goes to R2 here,
 * R1 in the original (MOV #38,R1; MOV.B R1,@R14). Everything else matches:
 * control flow, branch layout, the "BRA; NOP" of the straight-ahead (4)
 * block, the reqaction store shared by the held and default blocks (86/85
 * in R3, cross-jumped) and the "R2-free push block" of the siblings (the
 * 8C06C880 address in R3, the 0x800 mask in R2; see Sonic 8C0B1C56 in
 * code_8C0B1C56.c, Tails 8C0BD336 in code_8C0BCF56.c, E-102 8C0D14D6 in
 * code_8C0D10BC.c, Amy 8C0DC662 in code_8C0DC1E2.c).
 * What fixed the push block (12 -> 2 differing), all needed together:
 *  - "if (res == 0) goto none;" with "none: return 0;" as the last
 *    statement (Big's matched 8C0E54E4, code_8C0E5166.c) instead of
 *    "if (res != 0) { ... }" (that form gives the push block R2/R3 swapped);
 *  - the not-held path in the else of the button test (without the else it
 *    swaps R2/R3 in the not-held path instead);
 *  - "switch (res) { case 4: ...; return 1; default: break; }" followed by
 *    the default stores: without "default: break;" (or with the stores in
 *    the default label, or "if (res == 4)") the default block gets 38/85 in
 *    R3/R2, so its store no longer merges with the held block's.
 * An act local for the shared store also gives the right layout (3 differ),
 * but SHC puts a user variable that crosses no call in R4 (R3 in the
 * original), so the original merged plain stores (cross-jumping).
 * Tried without effect on the 38: store orders in the three blocks (every
 * combination), comma expressions, casts on the constants, a goto to the
 * default stores, an extra block or null statement, int/Uint32 res, "!= 0"
 * on the mask test, the shared return 1 after the if/else, the reversed
 * button test with a goto; else-if / nested-if / switch-with-default shapes
 * lose the store merge.
 */
Sint32 func_8C0C6D36(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    Sint32 res;

    if ((Sint32)B_8C754C08[(Uint8)twp->counter.b[0]].on & (Sint32)D_8C19E3A0) {
        res = func_8C06CFCC(twp, mwp);
        if (res == 0) {
            goto none;
        }
        if (func_8C06C880(twp) & 0x800) {
            twp->mode = 40;
            pwp->mj.reqaction = 86;
        } else {
            switch (res) {
            case 4:
                twp->mode = 13;
                pwp->mj.reqaction = 24;
                return 1;
            default:
                break;
            }
            twp->mode = 38;
            pwp->mj.reqaction = 85;
        }
        return 1;
    } else {
        if (func_8C06C880(twp) & 0x200) {
            twp->mode = 27;
            pwp->mj.reqaction = 70;
        } else {
            func_8C0C6DE0(twp, mwp, pwp);
            func_8C0C4F40(twp, mwp, pwp);
        }
        return 1;
    }
none:
    return 0;
}
#else
ASM_STUB(Sint32 func_8C0C6D36(taskwk *twp, motionwk *mwp, playerwk *pwp))
#endif

void func_8C0C6DE0(taskwk *twp, motionwk *mwp, playerwk *pwp)
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
 * NON_MATCHING (complete, functionally faithful): 221 of 221 instructions,
 * only 2 differ: the reload of twp->cwp for the hit test goes to R4 here,
 * R0 in the original (MOV.L @(56,R13),R0; MOV.L @(24,R0),R3). Same
 * unresolved R0 chain as Sonic 8C0B1DE0 (code_8C0B1C56.c), Tails 8C0BD4CC
 * (code_8C0BD4CC.c), Amy 8C0DC7DE (code_8C0DC7DE.c) and Big 8C0E5698,
 * whose notes list the many forms already tried; this is Amy's best form
 * (colliwk local for the compare; written inline the chain rotates
 * R3, R1, R0, R3: 5 differ). Control flow, the stack frame (mwp spilled to
 * @0 and shared with the parked rotation angle, info @4, out @8, v @20),
 * the 3 FMACs and the rotations match. The rotations must be conditional
 * expressions (E-102's matched func_8C0D1672, code_8C0D1672.c): ifs with
 * an angle local give other registers for the 2nd/3rd angle (21 differ).
 */
void func_8C0C6E80(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_POINT3 v;
    NJS_POINT3 out;
    CCL_INFO *info;
    taskwk *htwp;
    colliwk *cwp;

    htwp = pwp->htp->twp;
    info = CWP(twp)->info;
    if (!(func_8C06C880(twp) & 0x200)) {
        if (CWP(twp)->hit_cwp != NULL) {
            cwp = CWP(twp);
            if (htwp != cwp->hit_cwp->mytask->twp) {
                func_8C0C6DE0(twp, mwp, pwp);
                if (pwp->htp == NULL) {
                    if (twp->flag & TWF_GROUND) {
                        func_8C0C4F40(twp, mwp, pwp);
                    } else {
                        twp->mode = 9;
                        pwp->mj.reqaction = 19;
                    }
                }
            }
        }
        return;
    }
    htwp->pos = info->center;
    if (pwp->mj.reqaction == 96 || pwp->mj.reqaction == 97
        || pwp->mj.reqaction == 98 || pwp->mj.reqaction == 99) {
        v.x = SA1_FMAC(2.5f, pwp->righthand_vec.x, pwp->righthand_pos.x);
        v.y = SA1_FMAC(2.5f, pwp->righthand_vec.y, pwp->righthand_pos.y);
        v.z = SA1_FMAC(2.5f, pwp->righthand_vec.z, pwp->righthand_pos.z);
    } else {
        v.x = (pwp->righthand_pos.x + pwp->lefthand_pos.x) * 0.5f;
        v.y = (pwp->righthand_pos.y + pwp->lefthand_pos.y) * 0.5f;
        v.z = (pwp->righthand_pos.z + pwp->lefthand_pos.z) * 0.5f;
    }
    v.x -= CWP(htwp)->info->center.x;
    v.y -= CWP(htwp)->info->center.y;
    v.z -= CWP(htwp)->info->center.z;
    njPushMatrix(D_8C67C640);
    twp->ang.z ? njRotateZ(NULL, twp->ang.z) : (void)0;
    twp->ang.x ? njRotateX(NULL, twp->ang.x) : (void)0;
    0x8000 - twp->ang.y ? njRotateY(NULL, 0x8000 - twp->ang.y) : (void)0;
    njCalcVector(NULL, &v, &out);
    njPopMatrix(1);
    njAddVector(&htwp->pos, &out);
}
#else
ASM_STUB(void func_8C0C6E80(taskwk *twp, motionwk *mwp, playerwk *pwp))
#endif

/*
 * Matching notes: the attack-state (0x100) block ends in its own return 1
 * inside an if/else (its BRA to the shared return gets the empty delay slot
 * of the original); without that return the delay slot is filled, and
 * without the else the 0x800 mask moves from R2 to R3. The ground test
 * wraps the body so that return 0 is the last statement.
 */
Sint32 func_8C0C7080(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    if (!(twp->flag & 3)) {
        if (twp->flag & 0x100) {
            twp->mode = 6;
            return 1;
        } else {
            if (twp->flag & 0x800) {
                if (func_8C06C880(twp) & 0x200) {
                    twp->mode = 34;
                    pwp->mj.reqaction = 94;
                    return 1;
                }
                func_8C0C6DE0(twp, mwp, pwp);
            }
            twp->mode = 9;
            pwp->mj.reqaction = 19;
        }
        return 1;
    }
    return 0;
}

Sint32 func_8C0C70EE(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    if (GetAnalog(twp, NULL, NULL) == 0 && pwp->spd.x == 0.0f) {
        if (twp->flag & 0x800) {
            if (func_8C06C880(twp) & 0xC00) {
                twp->mode = 30;
                pwp->mj.reqaction = 91;
            } else {
                twp->mode = 29;
                pwp->mj.reqaction = 74;
            }
        } else {
            func_8C0C4F40(twp, mwp, pwp);
        }
        return 1;
    }
    return 0;
}
