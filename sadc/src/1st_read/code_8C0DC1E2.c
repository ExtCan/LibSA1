#include "common.h"
#include "ninja.h"
#include "sa1/sa1.h"
#include "sa1/pending.h"

/*
 * Amy Rose (1ST_READ chunk 8C0D6B40-8C0DCA6C, functions 26-30 of 32):
 * lifting, carrying, throwing and pushing objects. Amy's versions of
 * Sonic's func_8C0B17AE / 8C0B184C / 8C0B199A (code_8C0B16BC.c) and
 * 8C0B1C56 / 8C0B1D60 (code_8C0B1C56.c) with her own mode and action
 * numbers and SE 0x495 / 0x497. All take (twp, mwp, pwp); the Sint32 ones
 * are AmyModeMachine (8C0D7960) checks returning 1 when they changed the
 * player's mode.
 *
 *  func_8C0DC1E2  lift (mode 23, started by func_8C0DC120): from frame 12 a
 *                 0x200 object straight ahead (func_8C06CF60 == 4) is
 *                 taken: mode 25, SE 0x495, the hit task becomes pwp->htp
 *                 (its taskwk flag 0x1000), twp->flag 0x800 and ~8;
 *                 otherwise back to standing (func_8C0DA6E0).
 *  func_8C0DC2A0  carrying: the action button (press & D_8C19E3A0) throws
 *                 (in the air: mode 35, action 69/70; on the ground mode 28,
 *                 action 67/68, or a put-down, mode 24 action 71, when
 *                 slower than jog speed); the action depends on whether the
 *                 target list is empty (func_8C06D118). When the object is
 *                 gone (flag 0x800 clear): standing or falling (mode 8,
 *                 action 18) and the held task is released.
 *  func_8C0DC3C0  throw / put-down timing: action 67 frame 20 / 69 frame 7
 *                 throw the held object at the nearest target (speed 3,
 *                 player space -> global); 68 frame 24 / 70 frame 6 throw it
 *                 with the fixed speed (2, 0.8, 0); 71 frame 12 puts it down
 *                 (func_8C07DC2A). SE 0x497 for the throws.
 *  func_8C0DC662  mode 26 (holding a pushable object): with the button held
 *                 (on & D_8C19E3A0) the stick class func_8C06CFCC picks
 *                 0x800 objects: mode 36 action 57; class 4: mode 10 action
 *                 23; else mode 34 action 56 (class 0: return 0). Without
 *                 the button: 0x200 objects mode 23 action 43, else drop
 *                 (func_8C0DC75E) and back to standing.
 *  func_8C0DC75E  drop the held object (flag 0x800): clears the object's
 *                 0x1000 and twp->flag 0x800; a 0x200 object gets the speed
 *                 (1, 1, 0) (player space -> global); pwp->htp = NULL.
 *                 Also called by AmyModeMachine, 8C0DB08C, 8C0DBC16,
 *                 8C0DC7DE and 8C0DC9BE.
 *
 * Translation unit: the original object starts with 8C0DBC16/8C0DC120
 * (code_8C0DBC16.c). SHC shares one literal pool across the functions and
 * dumps it where the MOV.L range demands; in the original the pool of
 * 8C0DC120 is dumped after 8C0DC1E2, so 8C0DC2A0 dumps its own at its end
 * and 8C0DC3C0 dumps one in its middle (8C0DC526-8C0DC580), which turns
 * three of its conditional branches into the far BT/BRA form. Compiled
 * alone this file dumps its first pool inside 8C0DC2A0 instead, and
 * 8C0DC3C0 comes out without the far branches. With the original TU
 *   py -3.12 tools/shcdiff.py func_8C0DC3C0 src/1st_read/code_8C0DC1E2.c
 *       --flags=-define=TU_EXPERIMENT,NON_MATCHING
 * reports MATCH (as do 8C0DC1E2, 8C0DC2A0 and 8C0DC75E, also alone).
 */

/* object classification of the carried/touched object (code_8C06C880.c) */
extern Sint32 func_8C06C880(taskwk *twp);
/* angle class 1..4 towards the hit object (code_8C06CCA0.c); the callers
 * pass mwp as a second argument */
extern Sint32 func_8C06CF60(taskwk *twp, motionwk *mwp);
/* stick direction class vs. the CCL push-out (code_8C06CFCC.c) */
extern Sint32 func_8C06CFCC(taskwk *twp, motionwk *mwp);
/* Amy: back to standing (8C0DA6E0) */
extern void func_8C0DA6E0(taskwk *twp, motionwk *mwp, playerwk *pwp);

void func_8C0DC75E(taskwk *twp, motionwk *mwp, playerwk *pwp);

#ifdef TU_EXPERIMENT
/* The original object: 8C0DBC16 and 8C0DC120 (code_8C0DBC16.c), then this
 * file. Its ASM_STUBs become prototypes while it is included (8C0DC75E is
 * defined below; a stub body would also shift the literal pool). */
#undef ASM_STUB
#define ASM_STUB(proto) proto;
#include "1st_read/code_8C0DBC16.c"
#undef ASM_STUB
#define ASM_STUB(proto) proto { __asm_stub_sink(); __asm_stub_sink(); \
    __asm_stub_sink(); __asm_stub_sink(); __asm_stub_sink(); \
    __asm_stub_sink(); __asm_stub_sink(); __asm_stub_sink(); }
#endif

Sint32 func_8C0DC1E2(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    Sint32 kind;

    func_8C06CB40(twp, mwp);
    if (pwp->mj.nframe >= 12.0f) {
        if ((kind = func_8C06C880(twp)) != 0 && func_8C06CF60(twp, mwp) == 4) {
            if (kind & 0x200) {
                twp->mode = 25;
                func_8C0940DA(0x495, 0, 0, 0);
                pwp->htp = CWP(twp)->hit_cwp->mytask;
                pwp->htp->twp->flag |= 0x1000;
                twp->flag |= 0x800;
                twp->flag &= ~8;
                return 1;
            }
        }
    } else {
        return 0;
    }
    func_8C0DA6E0(twp, mwp, pwp);
    return 0;
}

Sint32 func_8C0DC2A0(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    if (B_8C754C08[(Uint8)twp->counter.b[0]].press & D_8C19E3A0) {
        if (!(twp->flag & 3)) {
            twp->mode = 35;
            if (func_8C06D118(twp->counter.b[0])) {
                pwp->mj.reqaction = 69;
            } else {
                pwp->mj.reqaction = 70;
            }
        } else if (pwp->spd.x < pwp->p.jog_speed) {
            twp->mode = 24;
            pwp->mj.reqaction = 71;
            PClearSpeed(mwp, pwp);
        } else {
            twp->mode = 28;
            if (func_8C06D118(twp->counter.b[0])) {
                pwp->mj.reqaction = 67;
            } else {
                pwp->mj.reqaction = 68;
            }
        }
        return 1;
    } else if (!(twp->flag & 0x800)) {
        if (twp->flag & 3) {
            func_8C0DA6E0(twp, mwp, pwp);
        } else {
            twp->mode = 8;
            pwp->mj.reqaction = 18;
        }
        if (pwp->htp) {
            pwp->htp->twp->flag &= ~0x1000;
            pwp->htp = NULL;
        }
        return 1;
    }
    return 0;
}

#ifdef NON_MATCHING
/*
 * NON_MATCHING (complete, functionally faithful): MATCH in the original
 * translation unit (TU_EXPERIMENT, see the file comment). Compiled alone the
 * instructions are the same except that the mid-function literal pool is
 * missing, so the three branches to 8C0DC580 are near BF instead of
 * BT/BRA/NOP (and the pool offsets differ).
 * Same source shape as Sonic's 8C0B199A: z is a separate local read back
 * from v.z (FR13/FR14/FR15 allocation), the x*x + z*z is the original's
 * single FMAC.
 */
Sint32 func_8C0DC3C0(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_VECTOR v;
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
        if (!(twp->flag & 0x800)) {
            return 0;
        }
        twp->flag &= ~0x800;
        return 1;
    }
    if (((pwp->mj.reqaction == 67 && pwp->mj.nframe == 20.0f)
            || (pwp->mj.reqaction == 69 && pwp->mj.nframe == 7.0f))
        && list->twp != NULL) {
        htwp = pwp->htp->twp;
        if (htwp != NULL) {
            htwp->flag &= ~0x1000;
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
            v.z = 0.0f;
            njUnitVector(&v);
            v.x *= 3.0f;
            v.y *= 3.0f;
            PConvertVector_P2G(twp, &v);
            pwp->htp->mwp->spd = v;
        }
        twp->flag &= ~0x800;
        pwp->htp = NULL;
        func_8C0940DA(0x497, 0, 0, 0);
        return 1;
    }
    if ((pwp->mj.reqaction == 68 && pwp->mj.nframe == 24.0f)
            || (pwp->mj.reqaction == 70 && pwp->mj.nframe == 6.0f)) {
        htwp = pwp->htp->twp;
        if (htwp != NULL) {
            htwp->flag &= ~0x1000;
            v.x = 2.0f;
            v.y = 0.799999957f;
            v.z = 0.0f;
            PConvertVector_P2G(twp, &v);
            pwp->htp->mwp->spd = v;
        }
        twp->flag &= ~0x800;
        pwp->htp = NULL;
        func_8C0940DA(0x497, 0, 0, 0);
        return 1;
    }
    if (pwp->mj.reqaction == 71 && pwp->mj.nframe == 12.0f) {
        htwp = pwp->htp->twp;
        if (htwp != NULL) {
            htwp->flag &= ~0x1000;
            func_8C07DC2A(twp, htwp, pwp);
        }
        twp->flag &= ~0x800;
        pwp->htp = NULL;
        return 1;
    }
    return 0;
}
#else
ASM_STUB(Sint32 func_8C0DC3C0(taskwk *twp, motionwk *mwp, playerwk *pwp))
#endif

#ifdef NON_MATCHING
/*
 * NON_MATCHING (complete, functionally faithful): control flow, branch
 * layout (including the shared "reqaction = act" tail of the 0x800 and the
 * default case, which needs the act local) and every instruction except
 * temporary registers in the push block are the original's (12 of 85
 * differ; the not-held path matches). After the 8C06CFCC call the original
 * loads the 8C06C880 address into R3, the 0x800 mask into R2 and keeps act
 * in R3 with the mode constants in R1/R3 (the whole push block avoids R2);
 * this compiles with the address in R2, the mask in R3, act in R4 and the
 * mode constants in R1/R2. Same unresolved R2-free push block as Sonic's
 * 8C0B1C56, Tails' 8C0BD336 and E-102's 8C0D14D6; Knuckles' 8C0C6D36 has
 * exactly this shape. The button test needs the signed operands (as
 * 8C0B1C56, 8C0DC120). Tried without effect on the push block: a kind
 * local (also "kind = ... & 0x800"), casts and operand order on the mask
 * test ((Uint32), 0x800U, 0x800L, != 0), int/Sint32/Uint32 res and act,
 * Sint16 act, block-scoped or register res/act, a switch on res (break or
 * return), act set before the mode, a nested if instead of else-if, a goto
 * to the shared store, the reversed button test, a pno local, a peripheral
 * pointer or other casts in the button test (they move only the first
 * call's address register), and plain reqaction stores instead of act
 * (SHC then merges a different pair of stores).
 */
Sint32 func_8C0DC662(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    Sint32 res;
    Uint16 act;

    if ((Sint32)B_8C754C08[(Uint8)twp->counter.b[0]].on & (Sint32)D_8C19E3A0) {
        res = func_8C06CFCC(twp, mwp);
        if (res != 0) {
            if (func_8C06C880(twp) & 0x800) {
                twp->mode = 36;
                act = 57;
            } else if (res == 4) {
                twp->mode = 10;
                pwp->mj.reqaction = 23;
                return 1;
            } else {
                twp->mode = 34;
                act = 56;
            }
            pwp->mj.reqaction = act;
            return 1;
        }
    } else {
        if (func_8C06C880(twp) & 0x200) {
            twp->mode = 23;
            pwp->mj.reqaction = 43;
        } else {
            func_8C0DC75E(twp, mwp, pwp);
            func_8C0DA6E0(twp, mwp, pwp);
        }
        return 1;
    }
    return 0;
}
#else
ASM_STUB(Sint32 func_8C0DC662(taskwk *twp, motionwk *mwp, playerwk *pwp))
#endif

void func_8C0DC75E(taskwk *twp, motionwk *mwp, playerwk *pwp)
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
