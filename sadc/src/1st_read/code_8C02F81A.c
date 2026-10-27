#include "common.h"
#include "ninja.h"
#include "sa1/sa1.h"

/*
 * 8C02F81A-8C02FD32: CCL (object collision) helpers
 * (chunk asm/1st_read/text/8C02E820.src, functions 16-21).
 *
 * 8C02F81A  reset of the CCL counters/flags (8C0342E0, 8C04E660)
 * 8C02F8A0  record a hit between two shapes in both colliwks
 * 8C02FA20  attack/defence level test between the two tested shapes
 * 8C02FB3A  hit filter by colliwk id and CCL_INFO.b05 masks
 * 8C02FBE4  number (0/1) of the player the collided object hit, else -1
 *           (SADX: CCL_IsHitPlayer)
 * 8C02FC12  world position of a shape centre
 *
 * SHC notes (found here):
 *  - SHC does common-subexpression elimination only inside one expression
 *    (one full condition, one ?: statement); the same load in two
 *    statements is reloaded.
 *  - A CSE temporary or a plain local with few uses often gets a 4-byte
 *    stack slot (stored, then reloaded): `c ? f(x) : (void)0` with x a
 *    load gives "load; TST; BT/S; MOV.L Rn,@R15 ... MOV.L @R15,R5; JSR"
 *    without `volatile` (func_8C02FC12), and keeps tail calls.
 */

extern Sint16 B_8C7456D8;
extern Sint16 B_8C7456DA;
extern Sint16 B_8C7456DC;
extern Sint16 B_8C7456DE;
extern Sint16 B_8C7456E0;
extern Sint16 B_8C7456E2;
extern Sint16 B_8C7456E4;
extern Sint16 B_8C752BD8;
extern Sint16 B_8C752BDA;
extern Sint8 D_8C19A738;
extern Sint8 D_8C19A739;

void func_8C02F81A(void)
{
    B_8C7456D8 = 0;
    B_8C7456DA = 0;
    B_8C752BD8 = 0;
    B_8C752BDA = 0;
    B_8C7456DC = 0;
    B_8C7456DE = 0;
    B_8C7456E0 = 0;
    B_8C7456E2 = 0;
    B_8C7456E4 = 0;
    D_8C19A738 = 0;
    D_8C19A739 = 0;
}

/*
 * A hit is recorded in a slot unless the slot already holds a player
 * collision (id 0 or 1) and the new one is a non-player.
 */
#ifdef NON_MATCHING
/*
 * Same instructions as the original except register numbers. The original
 * computes "cwp2 != NULL" at the start of each slot test into a register
 * (R8 in the first three tests, R1 in the last one, where the hit id takes
 * the free R11; R9 is never used). A plain local `f` gets a 4-byte stack
 * slot instead (MOVT/XOR #1/MOV.L R0,@R15, 179 instructions: one def and
 * one use per web is below SHC's register threshold; webs with two uses get
 * R9/R1); `register` gives the exact instruction sequence (175) but puts f
 * in R14 in all four tests and shifts the other callee-saved registers.
 * Tried without success: block-scoped f, Uint8/Sint16/Uint32/pointer f,
 * ?:/if-else definitions, one f per block (no recomputation), switch on the
 * hit id, single-expression conditions, nested ifs, #pragma inline helpers,
 * every declaration order. The id of the old hit is a local (loaded once,
 * EXTU.W, TST/CMP), the hit pointer is reloaded, so the test is in its own
 * statement (no CSE).
 */
void func_8C02F8A0(taskwk *twp1, colliwk *cwp1, taskwk *twp2, colliwk *cwp2)
{
    Uint16 n1;
    Uint16 n2;
    CCL_INFO *ci1;
    CCL_INFO *ci2;
    register Sint32 f;
    Sint32 id;

    n1 = cwp1->test_num;
    n2 = cwp2->test_num;
    ci1 = &cwp1->info[n1];
    ci2 = &cwp2->info[n2];
    if (!(cwp1->flag & 0x4000)) {
        f = cwp2 != NULL;
        if (cwp1->hit_cwp != NULL) {
            id = cwp1->hit_cwp->id;
            if ((id == 0 || id == 1) && f && cwp2->id != 0 && cwp2->id != 1) {
                goto keep1;
            }
        }
        cwp1->hit_cwp = cwp2;
keep1:
        cwp1->hit_num = n2;
        cwp1->my_num = n1;
        f = cwp2 != NULL;
        if (ci1->hit_cwp != NULL) {
            id = ci1->hit_cwp->id;
            if ((id == 0 || id == 1) && f && cwp2->id != 0 && cwp2->id != 1) {
                goto keep2;
            }
        }
        ci1->hit_cwp = cwp2;
keep2:
        ci1->hit_info = n2;
        if (ci2->attr & 0x200000) {
            cwp1->flag |= 0x4000;
        }
    }
    cwp1->flag |= 2;
    if (!(cwp2->flag & 0x4000)) {
        f = cwp1 != NULL;
        if (cwp2->hit_cwp != NULL) {
            id = cwp2->hit_cwp->id;
            if ((id == 0 || id == 1) && f && cwp1->id != 0 && cwp1->id != 1) {
                goto keep3;
            }
        }
        cwp2->hit_cwp = cwp1;
keep3:
        cwp2->hit_num = n1;
        cwp2->my_num = n2;
        f = cwp1 != NULL;
        if (ci2->hit_cwp != NULL) {
            id = ci2->hit_cwp->id;
            if ((id == 0 || id == 1) && f && cwp1->id != 0 && cwp1->id != 1) {
                goto keep4;
            }
        }
        ci2->hit_cwp = cwp1;
keep4:
        ci2->hit_info = n1;
        if (ci1->attr & 0x200000) {
            cwp2->flag |= 0x4000;
        }
    }
    cwp2->flag |= 2;
    twp1->flag |= 8;
    twp2->flag |= 8;
}
#else
ASM_STUB(void func_8C02F8A0(taskwk *twp1, colliwk *cwp1, taskwk *twp2, colliwk *cwp2))
#endif

/*
 * CCL_INFO.flag bits 0-1: attack level, bits 2-3: defence level; bits
 * 0x20/0x40/0x80 select the colliwk ids (0, 2/3, 4/5) the attack applies
 * to. A shape with attr 0x80 that is out-levelled gets flag 0x10 and its
 * task gets flag 4.
 *
 * SHC: the original reloads cwp->id for every comparison (no common
 * subexpression, also not for id == 2 || id == 3): the id is read through
 * a volatile view. The attr test and the level test are two nested ifs
 * (one && condition allocates the first id load to R3 instead of R1).
 */
#define CCL_ID(cwp) (((volatile colliwk *)(cwp))->id)

void func_8C02FA20(colliwk *cwp1, colliwk *cwp2)
{
    Uint8 atk1;
    taskwk *twp1;
    Uint8 atk2;
    taskwk *twp2;
    Uint8 def2;
    Uint8 def1;
    Uint16 n1;
    Uint16 n2;
    CCL_INFO *ci1;
    CCL_INFO *ci2;

    n1 = cwp1->test_num;
    n2 = cwp2->test_num;
    twp1 = cwp1->mytask->twp;
    twp2 = cwp2->mytask->twp;
    ci1 = &cwp1->info[n1];
    ci2 = &cwp2->info[n2];
    ci1->flag &= ~0x10;
    ci2->flag &= ~0x10;
    atk1 = (def1 = ci1->flag) & 3;
    atk2 = (def2 = ci2->flag) & 3;
    def1 = (def1 & 0xC) >> 2;
    def2 = (def2 & 0xC) >> 2;
    if (ci2->attr & 0x80) {
        if (atk1 > def2) {
            if ((CCL_ID(cwp2) == 0 && (ci1->flag & 0x20))
                || ((CCL_ID(cwp2) == 2 || CCL_ID(cwp2) == 3) && (ci1->flag & 0x40))
                || ((CCL_ID(cwp2) == 4 || CCL_ID(cwp2) == 5) && (ci1->flag & 0x80))) {
                ci2->flag |= 0x10;
                twp2->flag |= 4;
            }
        }
    }
    if (ci1->attr & 0x80) {
        if (atk2 > def1) {
            if ((CCL_ID(cwp1) == 0 && (ci2->flag & 0x20))
                || ((CCL_ID(cwp1) == 2 || CCL_ID(cwp1) == 3) && (ci2->flag & 0x40))
                || ((CCL_ID(cwp1) == 4 || CCL_ID(cwp1) == 5) && (ci2->flag & 0x80))) {
                ci1->flag |= 0x10;
                twp1->flag |= 4;
            }
        }
    }
}

/*
 * b05 bits 0-3: colliwk ids (0, 2/3, 4/5, 6/7) this shape can hit;
 * bits 4-7: ids it ignores. Sets cwp1->flag 0x80 and returns 1 on a hit.
 * Returns a signed char: every caller sign-extends R0 (EXTS.B).
 */
Sint8 func_8C02FB3A(colliwk *cwp1, CCL_INFO *ci1, colliwk *cwp2, CCL_INFO *ci2)
{
    Sint32 b1;
    Sint32 id1;
    Sint32 id2;
    Sint32 b2;

    id1 = cwp1->id;
    id2 = cwp2->id;
    b1 = ci1->b05;
    b2 = ci2->b05;
    if (id1 == 0 && id2 == 0) {
        return 0;
    }
    if ((id1 == 0 && (b2 & 1))
        || ((id1 == 2 || id1 == 3) && (b2 & 2))
        || ((id1 == 4 || id1 == 5) && (b2 & 4))
        || ((id1 == 6 || id1 == 7) && (b2 & 8))) {
        if ((id2 == 0 && (b1 & 0x10))
            || ((id2 == 2 || id2 == 3) && (b1 & 0x20))
            || ((id2 == 4 || id2 == 5) && (b1 & 0x40))
            || ((id2 == 6 || id2 == 7) && (b1 & 0x80))) {
            return 0;
        }
        cwp1->flag |= 0x80;
        return 1;
    }
    return 0;
}

Sint32 func_8C02FBE4(taskwk *twp)
{
    colliwk *cwp;
    taskwk *htwp;

    cwp = CWP(twp);
    if (cwp != NULL && cwp->hit_cwp != NULL) {
        htwp = cwp->hit_cwp->mytask->twp;
        if (htwp == playertwp[0]) {
            return 0;
        } else if (htwp == playertwp[1]) {
            return 1;
        }
    }
    return -1;
}

/*
 * Rotate by a non-zero angle only. The ?: form (the angle load is one
 * common subexpression) is what makes SHC keep the angle in a stack
 * temporary like the original.
 */
#define ROTATE_NZ(rot, a) ((a) != 0 ? rot(NULL, (a)) : (void)0)

/*
 * attr 0x20: center is absolute; 0x2000: center is relative to twp->pos
 * without rotation; else rotated by twp->ang (order Y, Z, X with attr
 * 0x40, else Z, X, Y) around twp->pos.
 * The 0x2000 path walks the vectors with a Float pointer (post-increment
 * loads, pre-decrement stores), as the original code does.
 */
void func_8C02FC12(taskwk *twp, CCL_INFO *ci, NJS_POINT3 *out)
{
    Float *p;
    Float x;
    Float y;
    Float z;
    Float cx;
    Float cy;
    Float cz;

    if (ci->attr & 0x20) {
        out->x = ci->center.x;
        out->y = ci->center.y;
        out->z = ci->center.z;
    } else if (!(ci->attr & 0x2000)) {
        njPushMatrix(D_8C67C640);
        njTranslateV(NULL, &twp->pos);
        if (ci->attr & 0x40) {
            ROTATE_NZ(njRotateY, twp->ang.y);
            ROTATE_NZ(njRotateZ, twp->ang.z);
            ROTATE_NZ(njRotateX, twp->ang.x);
        } else {
            ROTATE_NZ(njRotateZ, twp->ang.z);
            ROTATE_NZ(njRotateX, twp->ang.x);
            ROTATE_NZ(njRotateY, twp->ang.y);
        }
        njCalcPoint(NULL, &ci->center, out);
        njPopMatrix(1);
    } else {
        p = &twp->pos.x;
        x = *p++;
        y = *p++;
        z = *p;
        p = &ci->center.x;
        cx = *p++;
        cy = *p++;
        cz = *p;
        x += cx;
        y += cy;
        z += cz;
        p = &out->x;
        p += 3;
        *--p = z;
        *--p = y;
        *--p = x;
    }
}
