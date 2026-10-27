/*
 * 1ST_READ 8C0BCF56-8C0BD4CC (chunk 8C0BAFC0-8C0BDC80, functions 19-22 of
 * 29): Tails' held-object (carry/throw) helpers, called by TailsModeMachine
 * (8C0B70F4).
 *
 *  func_8C0BCF56(twp, mwp, pwp)  action button (D_8C19E3A0) pressed while
 *                 holding: throw. In the air mode 33 / action 86 (target in
 *                 range, 8C06D118) or 87; on the ground below jog speed
 *                 mode 22 / action 88 (speed cleared), else mode 26 /
 *                 action 84 (target) or 85. Not pressed: no-op (0) unless
 *                 twp->flag & 0x800 is clear, then back to standing
 *                 (8C0B70E8) or, in the air, mode 10 / action 19, and the
 *                 held task's request flag 0x1000 and pwp->htp are cleared.
 *  func_8C0BD098(twp, pwp)  release frame of the throw motions: action 84
 *                 frame 20 / action 86 frame 7 throws the held task at the
 *                 nearest target (player-space speed (h, dy, 0) normalised
 *                 * 3); action 85 frame 24 / 87 frame 6 throws it forward
 *                 (2, 0.8, 0); action 88 frame 12 puts it down (8C07DC2A).
 *                 SE 0x46B. Returns 1 when the object was let go.
 *  func_8C0BD336(twp, mwp, pwp)  action button (D_8C19E3A0) held: push /
 *                 pull a held object against an obstacle (8C06CFCC
 *                 direction class, 8C06C880 object class bits 0x800 /
 *                 0x200), modes 34 / 12 / 32 / 21; otherwise drop it
 *                 (8C0BD440) and stand (8C0B70E8).
 *  func_8C0BD440(twp, pwp)  drop the held object (twp->flag & 0x800): clear
 *                 its 0x1000 flag, hand it speed (1, 1, 0) in player space
 *                 when 8C06C880 & 0x200, pwp->htp = NULL.
 */
#include "sa1/pending.h"
#include "sa1/protos.h"

/* Tails SE: func_8C0940DA(se, 0, 0, 0) unless twp->counter.b[0] == 1 (code_8C0BBA4E.c) */
void func_8C0BFCA2(taskwk *twp, Sint32 se);
/* twp->mode = 1, waittimer = 0, then the Tails action code (code_8C0B70E8.c) */
void func_8C0B70E8(taskwk *twp, playerwk *pwp);
/* func_8C06CFCC(twp, mwp): stick direction vs. push-out direction of the CCL
 * hit, 1..4 (code_8C06CFCC.c; sa1/protos.h) */
/* normalise (falls back to (0, 1, 0) when too short); returns the length (code_8C0BAFC0.c) */
Float func_8C067414(NJS_VECTOR *v);

/* same chunk, defined below: BSR */
void func_8C0BD440(taskwk *twp, playerwk *pwp);

Sint32 func_8C0BCF56(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    if (B_8C754C08[(Uint8)twp->counter.b[0]].press & D_8C19E3A0) {
        if (!(twp->flag & 3)) {
            twp->mode = 33;
            if (func_8C06D118(twp->counter.b[0])) {
                pwp->mj.reqaction = 86;
            } else {
                pwp->mj.reqaction = 87;
            }
        } else if (pwp->spd.x < pwp->p.jog_speed) {
            twp->mode = 22;
            pwp->mj.reqaction = 88;
            PClearSpeed(mwp, pwp);
        } else {
            twp->mode = 26;
            if (func_8C06D118(twp->counter.b[0])) {
                pwp->mj.reqaction = 84;
            } else {
                pwp->mj.reqaction = 85;
            }
        }
        return 1;
    } else if (!(twp->flag & 0x800)) {
        if (twp->flag & 3) {
            func_8C0B70E8(twp, pwp);
        } else {
            twp->mode = 10;
            pwp->mj.reqaction = 19;
        }
        if (pwp->htp != NULL) {
            pwp->htp->twp->flag &= ~0x1000;
            pwp->htp = NULL;
        }
        return 1;
    }
    return 0;
}

#ifdef NON_MATCHING
/*
 * NON_MATCHING (complete, functionally faithful): 282 of 282 instructions,
 * control flow, branch forms, literal pools and stack use are the
 * original's; 5 differ only in callee-saved float registers. The original
 * keeps the 0.0f of the two "v.z = 0.0f" stores in FR15 and the copy of
 * v.z (z) in FR13; this compiles with the two exchanged (FR14 = v.x is
 * right). Same source shape as Sonic's 8C0B199A (code_8C0B16BC.c) and Amy's
 * 8C0DC3C0 (code_8C0DC1E2.c), which both have the zero in FR13 like this
 * build: Tails' original differs from them only here. Each branch ends in
 * its own return 1 (SHC merges them into the shared BRA/MOV #1 tail).
 * Tried: z as a temporary (v.z * v.z), z read inside the expression,
 * v.z = z = ..., named x, a named zero local (zero then lands in FR15 but
 * x/z swap), declaration and definition orders of the float locals, the
 * 0.0f store order in the fixed-speed throw, a nested list->twp test,
 * "register". Use counts matter: a third 0.0f store moves the zero to FR14.
 */
Sint32 func_8C0BD098(taskwk *twp, playerwk *pwp)
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
    if (((pwp->mj.reqaction == 84 && pwp->mj.nframe == 20.0f)
            || (pwp->mj.reqaction == 86 && pwp->mj.nframe == 7.0f))
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
            func_8C067414(&v);
            v.x *= 3.0f;
            v.y *= 3.0f;
            PConvertVector_P2G(twp, &v);
            pwp->htp->mwp->spd = v;
        }
        twp->flag &= ~0x800;
        pwp->htp = NULL;
        func_8C0BFCA2(twp, 0x46B);
        return 1;
    }
    if ((pwp->mj.reqaction == 85 && pwp->mj.nframe == 24.0f)
            || (pwp->mj.reqaction == 87 && pwp->mj.nframe == 6.0f)) {
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
        func_8C0BFCA2(twp, 0x46B);
        return 1;
    }
    if (pwp->mj.reqaction == 88 && pwp->mj.nframe == 12.0f) {
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
ASM_STUB(Sint32 func_8C0BD098(taskwk *twp, playerwk *pwp))
#endif

#ifdef NON_MATCHING
/*
 * NON_MATCHING (complete, functionally faithful): control flow, branch
 * layout and scheduling are the original's; 13 of 102 instructions differ
 * only in temporary registers. This is the unresolved "R2-free push block"
 * of Sonic's 8C0B1C56 (code_8C0B1C56.c), Knuckles' 8C0D14D6
 * (code_8C0D10BC.c) and Amy's 8C0DC662 (code_8C0DC1E2.c): after the
 * 8C06CFCC call the original loads the 8C06C880 / 8C0BFCA2 addresses and
 * the 74 / 12 / 32 constants into R3 and the 0x800 mask into R2; this
 * compiles with R2 and R3 exchanged. As in those files: the button test
 * needs Sint32 operands, the res == 4 test is a switch (store order and
 * the unfilled BRA delay slot of the default case) and the not-pushing
 * return 0 is the last statement. The TST/BT to that return 0 is near
 * (BT, as in the original) only while 8C0BD098 above compiles to its
 * original size (282 instructions; the ASM_STUB build also gives BT).
 * Tried without effect on the registers: switch (res) with case 0, an
 * early "if (res == 0) return 0" / goto (inline return or BF+BRA).
 */
#ifdef NON_MATCHING
/*
 * NON_MATCHING (complete, functionally faithful): control flow, branch
 * layout and scheduling are the original's; 13 of 102 instructions differ
 * only in temporary registers. This is the unresolved "R2-free push block"
 * of Sonic's 8C0B1C56 (code_8C0B1C56.c), Knuckles' 8C0D14D6
 * (code_8C0D10BC.c) and Amy's 8C0DC662 (code_8C0DC1E2.c): after the
 * 8C06CFCC call the original loads the 8C06C880 / 8C0BFCA2 addresses and
 * the 74 / 12 / 32 constants into R3 and the 0x800 mask into R2; this
 * compiles with R2 and R3 exchanged. As in those files: the button test
 * needs Sint32 operands, the res == 4 test is a switch (store order and
 * the unfilled BRA delay slot of the default case) and the not-pushing
 * return 0 is the last statement. The TST/BT to that return 0 is near
 * (BT, as in the original) only while 8C0BD098 above compiles to its
 * original size (282 instructions; the ASM_STUB build also gives BT).
 * Tried without effect on the registers: switch (res) with case 0, an
 * early "if (res == 0) return 0" / goto (inline return or BF+BRA).
 */
#ifdef NON_MATCHING
/*
 * NON_MATCHING (complete, functionally faithful): control flow, branch
 * layout and scheduling are the original's; 13 of 102 instructions differ
 * only in temporary registers. This is the unresolved "R2-free push block"
 * of Sonic's 8C0B1C56 (code_8C0B1C56.c), Knuckles' 8C0D14D6
 * (code_8C0D10BC.c) and Amy's 8C0DC662 (code_8C0DC1E2.c): after the
 * 8C06CFCC call the original loads the 8C06C880 / 8C0BFCA2 addresses and
 * the 74 / 12 / 32 constants into R3 and the 0x800 mask into R2; this
 * compiles with R2 and R3 exchanged. As in those files: the button test
 * needs Sint32 operands, the res == 4 test is a switch (store order and
 * the unfilled BRA delay slot of the default case) and the not-pushing
 * return 0 is the last statement. The TST/BT to that return 0 is near
 * (BT, as in the original) only while 8C0BD098 above compiles to its
 * original size (282 instructions; the ASM_STUB build also gives BT).
 * Tried without effect on the registers: switch (res) with case 0, an
 * early "if (res == 0) return 0" / goto (inline return or BF+BRA).
 */
Sint32 func_8C0BD336(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    Sint32 res;

    if ((Sint32)B_8C754C08[(Uint8)twp->counter.b[0]].on & (Sint32)D_8C19E3A0) {
        res = func_8C06CFCC(twp, mwp);
        if (res != 0) {
            if (func_8C06C880(twp) & 0x800) {
                twp->mode = 34;
                pwp->mj.reqaction = 74;
            } else {
                switch (res) {
                case 4:
                    twp->mode = 12;
                    pwp->mj.reqaction = 25;
                    func_8C0BFCA2(twp, 0x46C);
                    break;
                default:
                    twp->mode = 32;
                    pwp->mj.reqaction = 73;
                    break;
                }
            }
            return 1;
        }
    } else {
        if (func_8C06C880(twp) & 0x200) {
            twp->mode = 21;
            pwp->mj.reqaction = 58;
            if (pwp->spd.x >= pwp->p.jog_speed) {
                pwp->spd.x = pwp->p.jog_speed;
            }
        } else {
            func_8C0BD440(twp, pwp);
            func_8C0B70E8(twp, pwp);
        }
        return 1;
    }
    return 0;
}
#else
ASM_STUB(Sint32 func_8C0BD336(taskwk *twp, motionwk *mwp, playerwk *pwp))
#endif
#else
ASM_STUB(Sint32 func_8C0BD336(taskwk *twp, motionwk *mwp, playerwk *pwp))
#endif
#else
ASM_STUB(Sint32 func_8C0BD336(taskwk *twp, motionwk *mwp, playerwk *pwp))
#endif

void func_8C0BD440(taskwk *twp, playerwk *pwp)
{
    NJS_POINT3 v;
    taskwk *htwp;

    if (twp->flag & 0x800) {
        htwp = pwp->htp->twp;
        if (htwp != NULL) {
            htwp->flag &= ~0x1000;
        }
        /* The original clears it a second time without the NULL test (a
         * store to address 4 on the DC when the held task has no taskwk);
         * the PC build keeps the test. */
        PC_ONLY(if (htwp != NULL))
        pwp->htp->twp->flag &= ~0x1000;
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
