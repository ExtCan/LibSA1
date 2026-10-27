/*
 * CCL (object-vs-object collision) 8C03339E-8C0337FE: the target lists of
 * the players (homing attack / light speed dash candidates) and the
 * shape-pair dispatch of two colliwk.
 */
#include "common.h"
#include "task.h"
#include "sa1/pending.h"

/*
 * same chunk (BSR). func_8C033122 is not called here, but its stub keeps the
 * code before func_8C03339E at the size that puts SHC's literal pool of
 * func_8C03339E where the original has it.
 */
ASM_STUB(void func_8C033122(colliwk *cwp1, colliwk *cwp2))
ASM_STUB(Sint32 func_8C0332A0(colliwk *cwp1, colliwk *cwp2))

/* shape-pair tests (JSR); they return the squared distance of the shapes */
extern Float func_8C02FF7E(colliwk *cwp1, colliwk *cwp2);  /* sphere - sphere */
extern Float func_8C030540(colliwk *cwp1, colliwk *cwp2);
extern Float func_8C030634(colliwk *cwp1, colliwk *cwp2);
extern Float func_8C030F20(colliwk *cwp1, colliwk *cwp2);
extern Float func_8C0315C0(colliwk *cwp1, colliwk *cwp2);
extern Float func_8C031726(colliwk *cwp1, colliwk *cwp2);
extern Float func_8C031B88(colliwk *cwp1, colliwk *cwp2);  /* sphere - box */

/* target lists (PL_TARGET, ended by a NULL twp) and their entry counts */
extern PL_TARGET B_8C741280[];  /* light speed dash targets, player 0 (512 + 1) */
extern PL_TARGET B_8C7446D0[];  /* light speed dash targets, player 1 (512 + 1) */
extern Uint16 B_8C747708;       /* entries in B_8C73EE38 */
extern Uint16 B_8C74770A;       /* entries in B_8C741280 */
extern Uint16 B_8C74770C;       /* entries in B_8C742288 */
extern Uint16 B_8C74770E;       /* entries in B_8C7446D0 */

/*
 * Called by the range test 8C03357A for every tested pair, with their
 * squared distance: when cwp1 is a player (colliwk id 0) and cwp2 can be
 * targeted (flag 0x800), append cwp2's task to the player's target list
 * (objects of id 11: light speed dash list). Player 0 also becomes a target
 * of player 1.
 * Matching notes: 10000.0f must be a literal (SHC hoists it, with
 * B_8C78C608, &B_8C74770C and NULL, to the top: pool order B_8C78C608,
 * 10000.0f, &B_8C74770C) and the first test must be split into the nested
 * ifs below; a Float local for the limit or a flat && chain swap R6/R7.
 */
void func_8C03339E(colliwk *cwp1, colliwk *cwp2, Float dist)
{
    PL_TARGET *p;

    if (cwp1->id != 0) {
        return;
    }
    if (cwp1->mytask->twp == B_8C78C608[0] && cwp2->mytask->twp == B_8C78C608[1]) {
        if (cwp1->flag & 0x800) {
            if (dist < 10000.0f && B_8C74770C < 1160) {
                p = &B_8C742288[B_8C74770C];
                p->twp = cwp1->mytask->twp;
                p->dist = dist;
                p++;
                p->twp = NULL;
                B_8C74770C++;
            }
        }
    }
    if (cwp2->flag & 0x800) {
        if (cwp1->mytask->twp == B_8C78C608[0]) {
            if (cwp2->mytask->twp->id == 11) {
                if (dist < 10000.0f && B_8C74770A < 512) {
                    p = &B_8C741280[B_8C74770A];
                    p->twp = cwp2->mytask->twp;
                    p->dist = dist;
                    p++;
                    p->twp = NULL;
                    B_8C74770A++;
                }
            } else if (dist < 10000.0f && B_8C747708 < 1160) {
                p = &B_8C73EE38[B_8C747708];
                p->twp = cwp2->mytask->twp;
                p->dist = dist;
                p++;
                p->twp = NULL;
                B_8C747708++;
            }
        } else if (cwp1->mytask->twp == B_8C78C608[1]) {
            if (cwp2->mytask->twp->id == 11) {
                if (dist < 10000.0f && B_8C74770E < 512) {
                    p = &B_8C7446D0[B_8C74770E];
                    p->twp = cwp2->mytask->twp;
                    p->dist = dist;
                    p++;
                    p->twp = NULL;
                    B_8C74770E++;
                }
            } else if (dist < 10000.0f && B_8C74770C < 1160) {
                p = &B_8C742288[B_8C74770C];
                p->twp = cwp2->mytask->twp;
                p->dist = dist;
                p++;
                p->twp = NULL;
                B_8C74770C++;
            }
        }
    }
}

/*
 * Bounding-sphere test of two colliwk: 1 when the squared distance of the
 * two tasks is below (range1 + range2)^2. Feeds the target lists.
 * NON_MATCHING: complete and faithful; 5 instructions differ, all in the
 * prologue: the two parameter spill slots are swapped (cwp1 @R15 / cwp2
 * @(4,R15) here, @(4,R15) / @R15 in the original) and the two "ADD #32"
 * are scheduled in the other order. The float part (load order, FMUL +
 * 2 FMAC, FR14/FR15) matches for any load order with bx/by first.
 * Variants that get the spill slots and ADD order right but break the float
 * schedule: setting a/b after the range sum (statement or nested block),
 * a #pragma inline dist2(Float *, Float *) helper (192 load orders tried),
 * "cwp2->range + cwp1->range" (then R2/R3 and FR3/FR4 swap). All of these
 * also put MOV R7,R2 before MOV #8,R0. Tried without effect: declaration
 * orders, rr position, local copies of cwp1/cwp2, register/const, return
 * forms, NJS_POINT3 field access (gives @(R0,Rn) loads, not @Rn+).
 */
#ifdef NON_MATCHING
Sint32 func_8C03357A(taskwk *twp1, colliwk *cwp1, taskwk *twp2, colliwk *cwp2)
{
    Float *a = &twp1->pos.x;
    Float *b = &twp2->pos.x;
    Float ax;
    Float ay;
    Float az;
    Float bx;
    Float by;
    Float bz;
    Float dx;
    Float dy;
    Float dz;
    Float d;
    Float r;
    Float rr;

    r = cwp1->range + cwp2->range;
    rr = r * r;
    bx = *b++;
    by = *b++;
    bz = *b;
    ax = *a++;
    ay = *a++;
    dx = ax - bx;
    az = *a;
    dy = ay - by;
    dz = az - bz;
    /* FMUL dy*dy, FMAC dx*dx, FMAC dz*dz (the nested SA1_FMAC spelling makes SHC reorder the terms) */
    d = SH_ONLY(dx * dx + dy * dy + dz * dz) PC_ONLY(SA1_FMAC(dz, dz, SA1_FMAC(dx, dx, dy * dy)));
    func_8C03339E(cwp1, cwp2, d);
    return d < rr;
}
#else
ASM_STUB(Sint32 func_8C03357A(taskwk *twp1, colliwk *cwp1, taskwk *twp2, colliwk *cwp2))
#endif

/*
 * Tests every shape pair of cwp1 x cwp2 (forms 0 sphere, 1, 3, 4 box) with
 * the matching pair function; returns the result of the last pair test that
 * set it (1000000.0f when none did).
 * Matching note: the "default: break" of the form-4 switch is what makes SHC
 * use R2 (not R3) for EXTU.W j in the inner loop test; the other defaults
 * are neutral and kept for a uniform style.
 */
Float func_8C0335D4(colliwk *cwp1, colliwk *cwp2)
{
    taskwk *twp1;
    taskwk *twp2;
    Float ret;
    Uint16 i;
    Uint16 j;

    ret = 1000000.0f;
    twp1 = cwp1->mytask->twp;
    twp2 = cwp2->mytask->twp;
    if (func_8C03357A(twp1, cwp1, twp2, cwp2)) {
        for (i = 0; i < cwp1->nbInfo; i++) {
            for (j = 0; j < cwp2->nbInfo; j++) {
                cwp1->test_num = i;
                cwp2->test_num = j;
                if (func_8C0332A0(cwp1, cwp2)) {
                    continue;
                }
                if (!(cwp1->info[i].attr & 0x10) && !(cwp2->info[j].attr & 0x10)) {
                    if ((cwp1->info[i].attr & 0xF) == 0) {
                        switch (cwp2->info[j].attr & 0xF) {
                        case 0:
                            func_8C02FF7E(cwp1, cwp2);
                            break;
                        case 1:
                            func_8C030540(cwp1, cwp2);
                            break;
                        case 3:
                            func_8C030F20(cwp1, cwp2);
                            break;
                        case 4:
                            func_8C031B88(cwp1, cwp2);
                            break;
                        default:
                            break;
                        }
                    } else if ((cwp1->info[i].attr & 0xF) == 1) {
                        switch (cwp2->info[j].attr & 0xF) {
                        case 0:
                            ret = func_8C030540(cwp2, cwp1);
                            break;
                        case 1:
                            ret = func_8C030634(cwp1, cwp2);
                            break;
                        case 3:
                            ret = func_8C0315C0(cwp1, cwp2);
                            break;
                        case 4:
                            break;
                        default:
                            break;
                        }
                    } else if ((cwp1->info[i].attr & 0xF) == 3) {
                        switch (cwp2->info[j].attr & 0xF) {
                        case 0:
                            ret = func_8C030F20(cwp2, cwp1);
                            break;
                        case 1:
                            ret = func_8C0315C0(cwp2, cwp1);
                            break;
                        case 3:
                            ret = func_8C031726(cwp1, cwp2);
                            break;
                        case 4:
                            break;
                        default:
                            break;
                        }
                    } else if ((cwp1->info[i].attr & 0xF) == 4) {
                        switch (cwp2->info[j].attr & 0xF) {
                        case 0:
                            ret = func_8C031B88(cwp2, cwp1);
                            break;
                        case 1:
                            break;
                        case 4:
                            break;
                        default:
                            break;
                        }
                    }
                }
            }
        }
    }
    return ret;
}
