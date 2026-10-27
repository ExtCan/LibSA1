#include "common.h"
#include "sa1/sa1.h"

/*
 * 1ST_READ 8C0C80D4-8C0C8534: Knuckles helpers (chunk 8C0C80D4-8C0C8A80,
 * functions 1-4 of 9).
 *
 *  func_8C0C80D4  (KnucklesModeMachine, (twp, pwp)): unless action 52/50
 *                 is requested, pick the climbing action from the wall
 *                 speed: 49 (no move / not on the wall), 46 (sideways,
 *                 |spd.x| > |spd.z|), 48 up (work.f = spd.z), 47 down
 *                 (work.f = -spd.z).
 *  func_8C0C813E  (KnucklesMain, (twp, pwp)): per-frame collision shapes
 *                 (outside actions 54-69): shapes 1-3 off (attr 0x10),
 *                 attack levels of shape 0 (3/3 with item 0x8000 or in
 *                 mode 18, 1/1 while twp->flag 0x100, 0/1 in mode 52,
 *                 else 0/0); in action 51 (punch) shape 0 gets 0/3 and
 *                 shapes 2/3 are turned on in front of / behind the
 *                 collision centre along twp->ang.y.
 *  func_8C0C8330  (KnucklesModeMachine, (twp, mwp, pwp)): with
 *                 equipment 0x40 (shovel claw), button D_8C19E38C held and
 *                 D_8C19E390 pressed, on the ground or on a hit object
 *                 with attribute 0x400000: sphere of half the height
 *                 against the land objects; if the nearest hit (by the
 *                 projected centre) has attribute 0x100 without 0x08000000
 *                 start digging (mode 19, action 41, SE 0x47E), else the
 *                 failed dig (mode 22, action 45, SE 0x47F). Returns 1 when
 *                 it changed the mode.
 *  func_8C0C84FC  exec of a sound timer task (func_8C0C8534,
 *                 func_8C0C9C9A): after wtimer frames play SE ang.x and
 *                 free the task.
 *
 * Matching notes (func_8C0C813E):
 *  - SHC gives a stack home (written, never read back) to a local assigned
 *    inside an expression: the original's nine colliwk slots
 *    @(36..4,R15) come from "(cwpN = CWP(twp))->..." (cwp0 first, highest);
 *    the plain "CWP(twp)->info[n].attr &= ~0x10" lines have none.
 *  - the shape loop must index through cwp0 (cwp0->info[i]); through info
 *    SHC turns it into an end-pointer loop with a pre-test, through cwp0
 *    it keeps the counter (unrolled 3x, no pre-test) and info goes to R13.
 *  - the two FMACs load the addend (info->center) before the njCos/njSin
 *    call: written c + a * b (FMAC_ADD below, SA1_FMAC on the PC).
 */

/* c + a * b at an FMAC site, addend evaluated first; fused like SA1_FMAC on PC */
#if defined(TARGET_PC)
#define FMAC_ADD(c, a, b) SA1_FMAC((a), (b), (c))
#else
#define FMAC_ADD(c, a, b) ((c) + (a) * (b))
#endif

void func_8C0C80D4(taskwk *twp, playerwk *pwp)
{
    if (pwp->mj.reqaction != 52 && pwp->mj.reqaction != 50) {
        if (!(twp->flag & 1) || (pwp->spd.x == 0.0f && pwp->spd.z == 0.0f)) {
            pwp->mj.reqaction = 49;
        } else if (_builtin_fabsf(pwp->spd.x) > _builtin_fabsf(pwp->spd.z)) {
            pwp->mj.reqaction = 46;
        } else if (pwp->spd.z > 0.0f) {
            pwp->work.f = pwp->spd.z;
            pwp->mj.reqaction = 48;
        } else {
            pwp->work.f = -pwp->spd.z;
            pwp->mj.reqaction = 47;
        }
    }
}

void func_8C0C813E(taskwk *twp, playerwk *pwp)
{
    colliwk *cwp0;
    colliwk *cwp1;
    colliwk *cwp2;
    colliwk *cwp3;
    colliwk *cwp4;
    colliwk *cwp5;
    colliwk *cwp6;
    colliwk *cwp7;
    colliwk *cwp8;
    CCL_INFO *info;
    Sint32 i;
    Sint32 a;
    Sint32 b;

    if (pwp->mj.reqaction >= 54 && pwp->mj.reqaction <= 69) {
        return;
    }
    if (twp->cwp != NULL) {
        info = (cwp0 = CWP(twp))->info;
        for (i = 1; i < 4; i++) {
            cwp0->info[i].attr |= 0x10;
        }
        if (pwp->item & 0x8000) {
            a = 3;
            b = 3;
        } else if (twp->flag & 0x100) {
            a = 1;
            b = 1;
        } else if (twp->mode == 18) {
            a = 3;
            b = 3;
        } else if (twp->mode == 52) {
            a = 0;
            b = 1;
        } else {
            a = 0;
            b = 0;
        }
        info->flag = (info->flag & ~3) | a;
        info->flag = (info->flag & ~0xC) | (b << 2);
    }
    if (pwp->mj.reqaction == 51) {
        (cwp1 = CWP(twp))->info->flag = (CWP(twp)->info->flag & ~3) | 0;
        (cwp2 = CWP(twp))->info->flag = (CWP(twp)->info->flag & ~0xC) | (3 << 2);
        CWP(twp)->info[2].attr &= ~0x10;
        (cwp3 = CWP(twp))->info[2].center.x = FMAC_ADD(CWP(twp)->info->center.x, njCos(twp->ang.y), 2.0f);
        (cwp4 = CWP(twp))->info[2].center.y = CWP(twp)->info->center.y;
        (cwp5 = CWP(twp))->info[2].center.z = FMAC_ADD(CWP(twp)->info->center.z, njSin(twp->ang.y), 2.0f);
        CWP(twp)->info[3].attr &= ~0x10;
        (cwp6 = CWP(twp))->info[3].center.x = CWP(twp)->info->center.x - njCos(twp->ang.y) * 4.75f;
        (cwp7 = CWP(twp))->info[3].center.y = CWP(twp)->info->center.y + -0.75f;
        (cwp8 = CWP(twp))->info[3].center.z = CWP(twp)->info->center.z - njSin(twp->ang.y) * 4.75f;
    }
}

/*
 * Matching notes (func_8C0C8330):
 *  - the original tests twp->flag & 2 as "MOV #2,R4; TST R4,R0": the mask
 *    goes through a local (a literal gives TST #2,R0);
 *  - the "on" button test needs (Sint32) casts on both operands (see
 *    sa1/input.h), else the D_8C19E390 test uses R2/R3 instead of R1/R2;
 *  - the ground test must be separate statements (one "||" expression CSEs
 *    twp->flag, the original reloads it), the on/press tests one "&&"
 *    expression, and "(d = njDistanceP2P(..)) < min" (compare on FR0 before
 *    the copy);
 *  - the dead "if (0)" block stands in for code compiled out of the
 *    original: SHC counts register priority before removing dead code, and
 *    the extra reference to the constant 1 gives it R12 above pwp (R11),
 *    flag (R10), constant 0 (R9) and i (R8) as in the original (without it
 *    the constant 1 gets R8; the same trick as code_8C032D98.c).
 */
Sint32 func_8C0C8330(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    csts cs;
    Sint32 i;
    _OBJ_LANDCOLL *lp;
    Sint32 flag;
    Float min;
    Float d;
    Sint32 mask;

    if (pwp->equipment & 0x40) {
        if (((Sint32)B_8C754C08[(Uint8)twp->counter.b[0]].on & (Sint32)D_8C19E38C)
                && (B_8C754C08[(Uint8)twp->counter.b[0]].press & D_8C19E390)) {
            mask = 2;
            if (!((twp->flag & mask) && (CWP(twp)->hit_cwp->info->attr & 0x400000))) {
                if (!(twp->flag & 1)) {
                    return 0;
                }
            }
            cs.radius = pwp->p.height * 0.5f;
            cs.angx = twp->ang.x;
            cs.angz = twp->ang.z;
            cs.pos = twp->pos;
            cs.spd = mwp->spd;
            func_8C028BFE(&cs);
            flag = 0;
            min = 1000000.0f;
            for (i = B_8C754EF0, lp = B_8C754EF4; i > 0; i--, lp++) {
                if (func_8C029B00(&cs, lp->pObject, lp->slAttribute) == 1) {
                    if ((d = njDistanceP2P(&cs.onpoly, &cs.pos)) < min) {
                        min = d;
                        if ((lp->slAttribute & 0x100) && !(lp->slAttribute & 0x08000000)) {
                            flag = 1;
                        } else {
                            flag = 0;
                        }
                        if (0) {
                            flag = 1;
                        }
                    }
                }
            }
            if (!flag) {
                twp->mode = 22;
                pwp->mj.reqaction = 45;
                func_8C0940DA(0x47F, 0, 0, 0);
            } else {
                pwp->spd.x = 0.0f;
                pwp->spd.y = 0.0f;
                pwp->spd.z = 0.0f;
                twp->value.f = 0.0f;
                twp->timer.w[0] = 0;
                twp->mode = 19;
                pwp->mj.reqaction = 41;
                func_8C0940DA(0x47E, 0, 0, 0);
            }
            return 1;
        }
    }
    return 0;
}

void func_8C0C84FC(task *tp)
{
    taskwk *twp;

    twp = tp->twp;
    if (--twp->wtimer == 0) {
        func_8C0940DA(twp->ang.x, 0, 0, 0);
        FreeTask(tp);
    }
}
