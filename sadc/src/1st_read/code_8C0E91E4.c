/*
 * 1ST_READ 8C0E91E4-8C0EA040 (chunk 8C0E6620): Big the Cat, fishing.
 *
 *  func_8C0E91E4  starts reeling the lure in (callers func_8C0E7F60,
 *                 func_8C0E80A6, func_8C0E8A4A): unless an event is running
 *                 (func_8C04C960), plays SE 0x320 and, with a lure, clears
 *                 bit 0x40 and releases (lure mode <= 4) or resets the
 *                 camera; with a fish hooked (B_8C7A40E8 bit 0) the lure
 *                 goes to mode 8 when within 50 of player 0, otherwise the
 *                 fish task is freed, bits 0/2/3 are cleared and the lure
 *                 goes to mode 7. Then selects the camera: func_8C0EAA80 +
 *                 func_8C0EAD6A (mode 8 or stage 12), else func_8C0239C2 +
 *                 func_8C0EAE0A.
 *  func_8C0E93AE  1 when the ground or the ceiling found by the land probe
 *                 is water (slAttribute & 2) and the lure is not hit by a
 *                 collision (cwp->flag & 2).
 *  func_8C0E93D2  stage 12 only: 1 (and B_8C7A40E8 |= 0x80) when the hooked
 *                 fish task stands exactly at (750, 75.5, -650).
 *  func_8C0E943C  lure task exec (mode machine: 0 init, 1 held, 2 cast,
 *                 3 flying, 4 entering the water, 5 in the water, 6 fish
 *                 hooked, 7/8 reeled in).
 *  func_8C0E9CC6  end of the reel-in: tells player 0 (free.l[0] |= 4),
 *                 switches the camera back when a fish is hooked, then
 *                 func_8C0E91A0.
 *  func_8C0E9D24  ground / ceiling / water probe at (x, y, z) within 25
 *                 (same algorithm as func_8C0EA040 for a task position):
 *                 fills *out and returns the ground height.
 *
 * func_8C0E943C and func_8C0E9D24 are NON_MATCHING (notes at each). The
 * file also carries a layout replica of func_8C0E91A0 for the SHC build
 * (see there): the literal pool placement in func_8C0E91E4, and through it
 * the branch forms, depends on the code in front of it.
 */
#include "common.h"
#include "sa1/sa1.h"
#include "sa1/pending.h"

/* one probe result (func_8C0E9D24, func_8C0EA040) */
typedef struct {
    Uint32 attr;                /* 0x00 _OBJ_LANDCOLL.slAttribute */
    Angle angx;                 /* 0x04 */
    Angle angz;                 /* 0x08 */
    Float onpos;                /* 0x0C */
} LURE_HIT;                     /* 0x10 */

typedef struct {
    LURE_HIT upper;             /* 0x00 lowest ceiling above pos.y */
    LURE_HIT lower;             /* 0x10 highest ground at or below pos.y */
    LURE_HIT water;             /* 0x20 water surface nearest to pos.y */
} LURE_GROUND;                  /* 0x30 */

SA1_ASSERT_SIZE(LURE_HIT, 0x10);
SA1_ASSERT_SIZE(LURE_GROUND, 0x30);
SA1_ASSERT_OFFSET(LURE_GROUND, lower, 0x10);
SA1_ASSERT_OFFSET(LURE_GROUND, water, 0x20);

extern task *B_8C7A40C8;                /* lure task (NULL: no lure) */
extern task *B_8C7A40CC;                /* hooked fish task */
extern Uint16 B_8C7A40E8;               /* fishing state bits */
extern NJS_POINT3 B_8C7A40F0;           /* last position of the lure in the water */
extern Float D_8C565F70;                /* line tension limit */
extern Float D_8C565F74;                /* line tension */
extern CCL_INFO D_8C566010[];           /* lure collision */

extern Sint32 func_8C04C960(void);
extern void func_8C01AD2C(void (*func)(void), Sint8 adjust, Sint8 direct);
extern void func_8C01AF06(void (*fn)(void), Sint32 a, Sint32 b);
extern void func_8C01AE60(void);
extern void func_8C01B158(void);
extern void func_8C0239C2(void);
extern Sint32 func_8C107B26(void);
extern Sint32 func_8C02D2E0(zxsdwstr *zssp, NJS_OBJECT *object, Uint32 attr);
extern void func_8C0E6ED4(task *tp);
extern void func_8C0E7720(task *tp);
extern void func_8C0E78C0(task *tp);
extern void func_8C0E79D4(task *tp, NJS_POINT3 *pos);
extern Sint32 func_8C0E7BA8(task *tp);
extern Sint32 func_8C0E7C34(task *tp);
extern Sint32 func_8C0E7D5A(NJS_POINT3 *a, NJS_POINT3 *b);
extern Sint32 func_8C0E632A(task *tp);
extern void func_8C0EAA80(void);
extern void func_8C0EABEE(task *tp);
extern void func_8C0EAD6A(task *tp);
extern void func_8C0EAE0A(task *tp);
extern void func_8C0EAF28(void);
extern void func_8C0EA2F0(task *tp);
extern Sint32 func_8C0EB4E0(void);
extern void func_8C0EBFA8(void *line, NJS_POINT3 *a, NJS_POINT3 *b);
extern void func_8C0EC0A8(void *line, NJS_POINT3 *a, NJS_POINT3 *b, Sint32 mode);
void func_8C05E0F8(Sint32 kind);

extern Float D_8C565F78;
extern Float D_8C565F7C;
extern Sint32 D_8C565F80;

void func_8C0E8A4A(task *tp);
void func_8C0E9072(task *tp, NJS_POINT3 *pos);
void func_8C0E9CC6(task *tp);
Float func_8C0EA040(taskwk *twp, Sint32 flags, LURE_GROUND *out);
void func_8C0EA48A(task *tp);
void func_8C0EA64E(task *tp);
void func_8C0EA6C0(task *tp);
void func_8C0EA942(NJS_POINT3 *pos);

/*
 * func_8C0E91A0: lure reset (callers func_8C0E943C modes 0, 4, 5, 6 and
 * func_8C0E9CC6): twp->mode = 1 (lure held), awp->work[1].w[0..1] = 0, the
 * line state D_8C565F78 / 7C / 80 / 74 / 70 = 0, func_8C107B26 when a fish
 * is hooked (B_8C7A40E8 bit 0), then stops SE 0x320 (func_8C0945A0, tail
 * call). The body does what the asm (8C0E91A0-8C0E91E2) does, store for
 * store and in its order.
 *
 * TARGET_PC compiles it as the function. The SHC build keeps the original
 * assembly for it (the ASM_STUB line under "#if 0" is the marker
 * tools/splice.py reads; this compile differs from the original in 9 of 34
 * instructions, register choice and scheduling only) and compiles the same
 * body as a layout replica: it shares its literal pool with func_8C0E91E4,
 * and SHC only dumps that pool after the third tail call of func_8C0E91E4
 * (as in the original) when the pending pool starts with this function's 7
 * entries and 68 bytes of code; the 44-byte ASM_STUB body moves the dump
 * and changes func_8C0E91E4.
 */
#if 0
ASM_STUB(void func_8C0E91A0(task *tp))
#endif
void func_8C0E91A0(task *tp)
{
    anywk *awp;
    taskwk *twp;

    awp = AWP(tp);
    twp = tp->twp;
    twp->mode = 1;
    awp->work[1].w[0] = 0;
    awp->work[1].w[1] = 0;
    D_8C565F78 = 0.0f;
    D_8C565F7C = 0.0f;
    D_8C565F80 = 0;
    D_8C565F74 = 0.0f;
    D_8C565F70 = 0.0f;
    if (B_8C7A40E8 & 1) {
        func_8C107B26();
    }
    func_8C0945A0(0x320);
}

/*
 * Matching notes: tp and twp have to be register variables (without the
 * keyword SHC gives twp R14 and tp R12). The camera selection keeps its own
 * copy of the func_8C0EAA80 tail in the mode 8 path (SHC cross-jumps it into
 * the shared copy after scheduling, which leaves the original's unfilled
 * BRA), reaches the shared copy with a goto from the stage-12 test of the
 * hooked path, and keeps two separate func_8C0239C2 tails; with three
 * copies SHC hoists both function addresses into R9/R10, with if/else
 * chains it merges the two func_8C0239C2 tails.
 */
void func_8C0E91E4(register task *tp)
{
    register taskwk *twp;
    taskwk *ptwp;

    twp = tp->twp;
    ptwp = playertwp[0];
    if (func_8C04C960() != 1) {
        func_8C0945A0(0x320);
        if (B_8C7A40C8 != NULL) {
            B_8C7A40E8 &= 0xFFBF;
            if (twp->mode <= 4) {
                func_8C01AE60();
            } else {
                func_8C01B158();
            }
            if (B_8C7A40E8 & 1) {
                func_8C107B26();
                if (njDistanceP2P(&twp->pos, &ptwp->pos) < 50.0f) {
                    twp->mode = 8;
                    func_8C01AD2C(func_8C0EAA80, 0, 2);
                    func_8C0EAD6A(tp);
                } else {
                    twp->mode = 7;
                    if (B_8C7A40CC) {
                        FreeTask(B_8C7A40CC);
                    }
                    B_8C7A40CC = NULL;
                    B_8C7A40E8 &= 0xFFF2;
                    if ((GetStageNumber() & 0xFF00) == 0xC00) {
                        goto camera;
                    }
                    func_8C01AD2C(func_8C0239C2, 0, 2);
                    func_8C0EAE0A(tp);
                }
            } else {
                twp->mode = 7;
                if ((GetStageNumber() & 0xFF00) == 0xC00) {
camera:
                    func_8C01AD2C(func_8C0EAA80, 0, 2);
                    func_8C0EAD6A(tp);
                } else {
                    func_8C01AD2C(func_8C0239C2, 0, 2);
                    func_8C0EAE0A(tp);
                }
            }
        }
    }
}

Sint32 func_8C0E93AE(task *tp, LURE_GROUND *gnd)
{
    if (!(gnd->lower.attr & 2) && !(gnd->upper.attr & 2)) {
        return 0;
    }
    if (CWP(tp->twp)->flag & 2) {
        return 0;
    }
    return 1;
}

Sint32 func_8C0E93D2(void)
{
    if (B_8C7A40CC != NULL && (GetStageNumber() & 0xFF00) == 0xC00
        && B_8C7A40CC->twp->pos.x == 750.0f
        && B_8C7A40CC->twp->pos.y == 75.5f
        && B_8C7A40CC->twp->pos.z == -650.0f) {
        B_8C7A40E8 |= 0x80;
        return 1;
    }
    return 0;
}

/*
 * NON_MATCHING: complete and functionally faithful (checked against the
 * asm case by case), not matched. Blocking issues:
 *  - BSR vs JSR to same-file callees depends on SHC's distance estimate:
 *    the original reaches func_8C0E8A4A with BSR from mode 4 (3718 bytes
 *    back) but with JSR from mode 5 (4126 bytes back), and func_8C0EA942
 *    with BSR only from mode 6; reproducing that needs the real distances
 *    to func_8C0E8A4A (0x79A bytes before func_8C0E91E4) and to
 *    func_8C0EA040..func_8C0EA942 (the ASM_STUB bodies are 44 bytes).
 *  - callee-saved register choice: the original keeps tp R14, twp R13,
 *    &B_8C7A40E8 R12, mwp R11, &B_8C7A40C8 R10, awp R9 and the constant 7
 *    (twp->mode = 7) in R8 and spills n to sp+4 (frame 92); small source
 *    changes swap 7 with 0xFFBF or func_8C0E7720 (the goto in mode 3 and the
 *    && in mode 1 bring this compile closest).
 *  - the "& 1" / "& 0x8000" tests reload &B_8C7A40E8 from the pool; they do
 *    so here only when written as the second operand of && (mode 1).
 *  - mode 3: the original has two copies of "if (B_8C7A40C8) ... &= 0xFFBF"
 *    in front of one shared "mode = 7; SE 0x31F" tail.
 *  - mode 2 passes the njScalor result in FR4 to CCL_Init as well (an extra
 *    float argument in the original call; CCL_Init ignores it).
 */
#ifdef NON_MATCHING
ASM_STUB(void func_8C0E8A4A(task *tp))
ASM_STUB(void func_8C0E9072(task *tp, NJS_POINT3 *pos))

void func_8C0E943C(task *tp)
{
    NJS_VECTOR v;
    LURE_GROUND gnd;
    NJS_POINT3 pos;
    NJS_POINT3 lpos;
    Sint32 n;
    playerwk *pwp;
    taskwk *twp;
    motionwk *mwp;
    anywk *awp;
    Float t;

    awp = AWP(tp);
    twp = tp->twp;
    mwp = tp->mwp;
    pwp = PWP(playermwp[0]);
    func_8C0E79D4(tp, &pos);
    if (twp->mode) {
        func_8C0EA2F0(tp);
    }
    if (B_8C7A40C8) {
        B_8C7A40E8 &= 0xEFEF;
    }
    switch (twp->mode) {
    case 0:
        func_8C0E91A0(tp);
        tp->dest = func_8C0E6ED4;
        tp->disp = func_8C0E78C0;
        break;
    case 1:
        func_8C0E9072(tp, &pos);
        func_8C0E7720(tp);
        if (B_8C7A40C8 && (B_8C7A40E8 & 0x8000)) {
            B_8C7A40E8 &= ~0x8000;
            twp->mode = 2;
            ((NJS_POINT3 *)AWP(tp)->work[3].ptr)->x = ((task *)pwp->free.ptr[1])->twp->pos.x;
            ((NJS_POINT3 *)AWP(tp)->work[3].ptr)->y = AWP((task *)pwp->free.ptr[1])->work[1].f;
            ((NJS_POINT3 *)AWP(tp)->work[3].ptr)->z = ((task *)pwp->free.ptr[1])->twp->pos.z;
            FreeTask((task *)pwp->free.ptr[1]);
            twp->timer.l = 10;
            B_8C7A40E8 = 0;
        }
        break;
    case 2:
        func_8C0E9072(tp, &pos);
        func_8C0E7720(tp);
        if (--twp->timer.l != 0) {
            break;
        }
        v.x = ((NJS_POINT3 *)awp->work[3].ptr)->x - twp->pos.x;
        v.y = 0.0f;
        v.z = ((NJS_POINT3 *)awp->work[3].ptr)->z - twp->pos.z;
        t = njScalor(&v) * 0.33333332f;
        CCL_Init(tp, D_8C566010, 1, 1, 1);
        twp->mode = 3;
        mwp->spd.x = v.x / t;
        mwp->spd.z = v.z / t;
        mwp->spd.y = SA1_FMAC(0.0099999998f, t, (((NJS_POINT3 *)awp->work[3].ptr)->y - twp->pos.y) / t);
        mwp->ang_aim.x = 0;
        twp->ang.z = 0;
        func_8C0EA942(&lpos);
        if (AWP(tp)->work[2].ptr) {
            func_8C0EBFA8(AWP(tp)->work[2].ptr, &pos, &lpos);
        }
        func_8C0940DA(0x31D, 0, 0, 0);
        func_8C0940DA(0x4B8, 0, 0, 0);
        break;
    case 3:
        twp->timer.l++;
        if (func_8C0E7BA8(tp)) {
            func_8C0EA040(twp, 0, &gnd);
            if (!func_8C0E93AE(tp, &gnd)) {
                goto miss;
            }
            twp->mode = 4;
            twp->pos.x = ((NJS_POINT3 *)awp->work[3].ptr)->x;
            twp->pos.y = gnd.water.onpos + -0.59999998f;
            twp->pos.z = ((NJS_POINT3 *)awp->work[3].ptr)->z;
        } else if (njDistanceP2P((NJS_POINT3 *)awp->work[3].ptr, &twp->pos) < 3.0f) {
            twp->mode = 4;
            twp->pos.x = ((NJS_POINT3 *)awp->work[3].ptr)->x;
            twp->pos.y = ((NJS_POINT3 *)awp->work[3].ptr)->y;
            twp->pos.z = ((NJS_POINT3 *)awp->work[3].ptr)->z;
            func_8C0EA040(twp, 0, &gnd);
            if (!func_8C0E93AE(tp, &gnd)) {
miss:
                if (B_8C7A40C8) {
                    B_8C7A40E8 &= 0xFFBF;
                }
                twp->mode = 7;
                func_8C0940DA(0x31F, 0, 0, 0);
            } else {
                twp->pos.y = gnd.water.onpos + -0.59999998f;
            }
        }
        func_8C0EA942(&lpos);
        if (AWP(tp)->work[2].ptr) {
            func_8C0EC0A8(AWP(tp)->work[2].ptr, &pos, &lpos, 0);
        }
        func_8C0E7720(tp);
        func_8C02F63C(twp);
        break;
    case 7:
    case 8:
        if (func_8C0E7C34(tp)) {
            func_8C0E9CC6(tp);
            break;
        }
        func_8C0EA942(&lpos);
        if (AWP(tp)->work[2].ptr) {
            func_8C0EC0A8(AWP(tp)->work[2].ptr, &pos, &lpos, 1);
        }
        func_8C0E7720(tp);
        break;
    case 4:
        if (func_8C04C960() != 1) {
            B_8C7A40F0 = twp->pos;
        }
        twp->pos.y -= 0.50009997f;
        twp->mode = 5;
        func_8C0EABEE(tp);
        func_8C0E8A4A(tp);
        func_8C0EA942(&lpos);
        if (AWP(tp)->work[2].ptr) {
            func_8C0EC0A8(AWP(tp)->work[2].ptr, &pos, &lpos, 0);
        }
        func_8C0E7720(tp);
        if (func_8C04C960() == 1) {
            if (njDistanceP2P(&playertwp[0]->pos, &B_8C7A40F0) > 20.0f) {
                n = func_8C0EB4E0();
                if (n > 0) {
                    func_8C05E0F8(n);
                    B_8C7A40F0 = playertwp[0]->pos;
                }
            }
            B_8C7A40E8 &= 0xFFBF;
            twp->mode = 7;
            func_8C01AE60();
            break;
        }
        if (func_8C0E632A(tp)) {
            func_8C0E91A0(tp);
            break;
        }
        if (func_8C0E7D5A(&pos, &twp->pos)) {
            B_8C7A40E8 &= 0xFFBF;
            twp->mode = 7;
            break;
        }
        if (twp->mode != 5) {
            break;
        }
        if (B_8C7A40C8) {
            B_8C7A40E8 |= 0x40;
        }
        B_8C7A40F0 = twp->pos;
        D_8C565F74 = D_8C565F70;
        mwp->spd.x *= 0.25f;
        mwp->spd.y *= 0.25f;
        mwp->spd.z *= 0.25f;
        tp->twp->ang.x = 0;
        func_8C0940DA(0x31E, 0, 0, 0);
        func_8C01AE60();
        func_8C01AF06(func_8C0EAF28, 0, 2);
        break;
    case 5:
        func_8C0EABEE(tp);
        func_8C0E8A4A(tp);
        func_8C0EA48A(tp);
        func_8C0EA64E(tp);
        func_8C0EA6C0(tp);
        func_8C0EA942(&lpos);
        if (AWP(tp)->work[2].ptr) {
            func_8C0EC0A8(AWP(tp)->work[2].ptr, &pos, &lpos, 1);
        }
        func_8C0E7720(tp);
        if ((B_8C754C08[0].l << 8) - 0x8000 > -0x7400 && (B_8C754C08[0].r << 8) - 0x8000 > -0x7400
            && !(B_8C7A40E8 & 1)) {
            B_8C7A40E8 |= 0x4000;
            func_8C01B158();
            if ((GetStageNumber() & 0xFF00) == 0xC00) {
                func_8C01AD2C(func_8C0EAA80, 0, 2);
                func_8C0EAD6A(tp);
            } else {
                func_8C01AD2C(func_8C0239C2, 0, 2);
                func_8C0EAE0A(tp);
            }
        }
        if (B_8C7A40E8 & 0x80) {
            if ((GetStageNumber() & 0xFF00) == 0xC00 && !(B_8C7A40E8 & 1)) {
                B_8C7A40E8 |= 0x4000;
                func_8C01B158();
                if ((GetStageNumber() & 0xFF00) == 0xC00) {
                    func_8C01AD2C(func_8C0EAA80, 0, 2);
                    func_8C0EAD6A(tp);
                } else {
                    func_8C01AD2C(func_8C0239C2, 0, 2);
                    func_8C0EAE0A(tp);
                }
                B_8C7A40E8 &= 0xFF7F;
            }
        }
        if (B_8C7A40E8 & 0x4000) {
            if (B_8C7A40C8) {
                B_8C7A40E8 &= 0xFFBF;
                twp->mode = 7;
                func_8C0945A0(0x320);
            }
            break;
        }
        if (func_8C0E632A(tp)) {
            func_8C0E91A0(tp);
            break;
        }
        if (B_8C7A40C8 && (B_8C7A40E8 & 1)) {
            twp->mode = 6;
            func_8C0E93D2();
        }
        break;
    case 6:
        func_8C0EABEE(tp);
        func_8C0E8A4A(tp);
        func_8C0EA48A(tp);
        func_8C0EA64E(tp);
        func_8C0EA6C0(tp);
        func_8C0EA942(&lpos);
        if (AWP(tp)->work[2].ptr) {
            func_8C0EC0A8(AWP(tp)->work[2].ptr, &pos, &lpos, 1);
        }
        func_8C0E7720(tp);
        if (B_8C7A40C8 && !(B_8C7A40E8 & 1)) {
            if (twp->mode == 6) {
                twp->mode = 5;
            }
            func_8C107B26();
            break;
        }
        if (func_8C0E632A(tp)) {
            func_8C0E91A0(tp);
        }
        break;
    }
}
#else
ASM_STUB(void func_8C0E943C(task *tp))
#endif

void func_8C0E9CC6(task *tp)
{
    playerwk *pwp;

    pwp = PWP(playermwp[0]);
    pwp->free.l[0] |= 4;
    if (B_8C7A40C8) {
        B_8C7A40E8 &= 0xFFBF;
        if (B_8C7A40CC && (B_8C7A40E8 & 1)) {
            func_8C01AE60();
            func_8C01AD2C(func_8C0EAA80, 0, 2);
            B_8C7A40E8 |= 2;
            func_8C0EAD6A(tp);
        }
    }
    func_8C0E91A0(tp);
}

/*
 * NON_MATCHING (25 of 315 instructions; code, scheduling and the literal
 * pools match): register/stack-slot allocation only. The original puts the
 * loop counter i in R9 and the lower block's lattr induction pointer in
 * R10 (this compile swaps them), keeps the hit locals at uattr sp+36,
 * langx 20, langz 28, lattr 24 (this compile: 20, 24, 36, 28; uangx 32 and
 * uangz 16 agree), and schedules MOV #52,R0 before FMOV FR4,FR13.
 * Findings: the slot order follows neither declarations nor the zeroing
 * order; it follows the order of the final copies to *out (copying in the
 * order upper.angx, upper.angz, lower.angx, lower.attr, upper.attr,
 * lower.angz gives the original slots, but the stores are then emitted in
 * that order). Chaining the zeroing (as below) moves i out of R8 as in the
 * original. x and z must be locals (as parameters z is spilled);
 * "out->water.onpos = upper = ..." gives upper/lower the original FR14/FR15;
 * h reproduces the CSE'd onpos load in the BT/S delay slot; the comparison
 * operand order fixes the load order. Same allocation problem as the twin
 * func_8C0EA040 (code_8C0EA040.c).
 */
#ifdef NON_MATCHING
Float func_8C0E9D24(Float px, Float y, Float pz, Sint32 flags, LURE_GROUND *out)
{
    zxsdwstr zss;
    NJS_VECTOR v;
    Angle uangx;
    Angle uangz;
    Uint32 uattr;
    Angle langx;
    Angle langz;
    Uint32 lattr;
    _OBJ_LANDCOLL *lc;
    NJS_OBJECT *obj;
    Float dist;
    Float scl;
    Float lower;
    Float upper;
    Float h;
    Uint16 i;
    Float x;
    Float z;

    x = px;
    z = pz;
    zss.pos.x = x;
    zss.pos.y = y;
    zss.pos.z = z;
    lower = -1000000.0f;
    langx = langz = lattr = 0;
    uattr = 0;
    uangx = uangz = 0;
    out->water.onpos = upper = 1000000.0f;
    out->water.angx = 0;
    out->water.angz = 0;
    out->water.attr = 0;
    lc = B_8C754EF4;
    func_8C0522C0(x, y, z, 25.0f);
    for (i = 0; i < B_8C754EF0; i++) {
        obj = lc[i].pObject;
        if (lc[i].pTask) {
            v.x = obj->pos[0] - x;
            v.y = 0.0f;
            v.z = obj->pos[2] - z;
            dist = njScalor(&v);
            scl = obj->scl[0];
            if (obj->scl[1] > scl) {
                scl = obj->scl[1];
            }
            if (obj->scl[2] > scl) {
                scl = obj->scl[2];
            }
            if (dist > scl * obj->model->r) {
                continue;
            }
        }
        if (!func_8C02D2E0(&zss, lc[i].pObject, lc[i].slAttribute)) {
            continue;
        }
        if (lc[i].pTask && FWP(lc[i].pTask)) {
            zss.upper.onpos += FWP(lc[i].pTask)->pos_spd.y;
            zss.lower.onpos += FWP(lc[i].pTask)->pos_spd.y;
        }
        if (zss.upper.findflag == 1 && zss.pos.y < zss.upper.onpos && upper > zss.upper.onpos) {
            h = zss.upper.onpos;
            if (lc[i].slAttribute & 2) {
                if (_builtin_fabsf(out->water.onpos - zss.pos.y) > _builtin_fabsf(h - zss.pos.y)) {
                    out->water.onpos = zss.upper.onpos;
                    out->water.angx = zss.upper.angx;
                    out->water.angz = zss.upper.angz;
                    out->water.attr = lc[i].slAttribute;
                }
                if (!(flags & 2)) {
                    upper = zss.upper.onpos;
                    uangx = zss.upper.angx;
                    uangz = zss.upper.angz;
                    uattr = lc[i].slAttribute;
                }
            } else {
                upper = h;
                uangx = zss.upper.angx;
                uangz = zss.upper.angz;
                uattr = lc[i].slAttribute;
            }
        }
        if (zss.lower.findflag == 1 && zss.pos.y >= zss.lower.onpos && zss.lower.onpos > lower) {
            h = zss.lower.onpos;
            if (lc[i].slAttribute & 2) {
                if (_builtin_fabsf(out->water.onpos - zss.pos.y) > _builtin_fabsf(h - zss.pos.y)) {
                    out->water.onpos = zss.lower.onpos;
                    out->water.angx = zss.lower.angx;
                    out->water.angz = zss.lower.angz;
                    out->water.attr = lc[i].slAttribute;
                }
                if (!(flags & 1)) {
                    lower = zss.lower.onpos;
                    langx = zss.lower.angx;
                    langz = zss.lower.angz;
                    lattr = lc[i].slAttribute;
                }
            } else {
                lower = h;
                langx = zss.lower.angx;
                langz = zss.lower.angz;
                lattr = lc[i].slAttribute;
            }
        }
    }
    out->upper.onpos = upper;
    out->upper.angx = uangx;
    out->upper.angz = uangz;
    out->upper.attr = uattr;
    out->lower.onpos = lower;
    out->lower.angx = langx;
    out->lower.angz = langz;
    out->lower.attr = lattr;
    return lower;
}
#else
ASM_STUB(Float func_8C0E9D24(Float px, Float y, Float pz, Sint32 flags, LURE_GROUND *out))
#endif

#ifdef NON_MATCHING
/* same-file BSR callees of func_8C0E943C */
ASM_STUB(Float func_8C0EA040(taskwk *twp, Sint32 flags, LURE_GROUND *out))
ASM_STUB(void func_8C0EA48A(task *tp))
ASM_STUB(void func_8C0EA64E(task *tp))
ASM_STUB(void func_8C0EA6C0(task *tp))
ASM_STUB(void func_8C0EA942(NJS_POINT3 *pos))
#endif
