/*
 * 1ST_READ 8C0E80A6-8C0E9072 (chunk 8C0E6620, Big the Cat): the fishing
 * lure task (tp = lure; exec 8C0E943C -> func_8C0E8A4A).
 *
 *  func_8C0E80A6  reel in (D_8C19E390 held): D_8C565F74 -= 0.2f and reel
 *                 SE 800 whenever bit 15 of the phase D_8C565F80 (+0x11C7
 *                 per frame) flips; while D_8C565F74 >= D_8C565F70 the lure
 *                 only slows down (func_8C0E7E00), else it is pulled towards
 *                 *pos at 0.2f in x/z, scaled by 5000 / (5000 + fish weight)
 *                 when a fish is on; spd.y 0.18f in stage 8-3, else 0.06f.
 *  func_8C0E82A0  fish on and Big pulling (B_8C7A40E8 & 0x20): the lure is
 *                 pulled towards *pos at 0.5f; Big's pull actions 57/59/71/73
 *                 and 56/58/70/72 steer it sideways while the angle between
 *                 the lure's aim and Big is in range, else it slows down.
 *  func_8C0E8562  line tension: target D_8C565F7C from the fish weight, the
 *                 fish's pull against the line (njCos of the angle) and the
 *                 reel buttons; D_8C565F78 follows it in steps of
 *                 0.02f/0.002f (once per displayed frame); at 1.0f the line
 *                 breaks (flag 0x800, func_8C107C80(46), func_8C0EBD22);
 *                 SE 810 every 120 frames while it is >= 0.8f.
 *  func_8C0E89A0  lure out and either flag 8, or a fish on (flag 1, not
 *                 0x20) with both reel buttons released.
 *  func_8C0E89EC  stage 8-3 (0x803): Big (B_8C7A40F0) within 70 of one of
 *                 the three spots D_8C565FD8.
 *  func_8C0E8A4A  lure motion: probe (func_8C0EA040; water level cached in
 *                 D_8C565F6C), landing, following the hooked fish, sphere
 *                 sweeps against the land (func_8C0569E0 / func_8C0EB70E),
 *                 splash (func_8C0EB25E, SE 809).
 *
 * Status: func_8C0E80A6, func_8C0E82A0 and func_8C0E89EC match; the other
 * three are NON_MATCHING (register allocation only, see each function).
 *
 * Translation-unit layout model (SHC only; never spliced: ASM_STUB bodies
 * are ignored and the tu_gap / tu_size functions have no address). The
 * original TU is the whole chunk 8C0E6620-8C0EAA80:
 *  - func_8C0E7E00 is reached with BRA/BSR from func_8C0E80A6 and
 *    func_8C0E82A0 but with JSR from func_8C0E8A4A (0x100E bytes away);
 *  - func_8C0E91E4 is reached with BSR from func_8C0E8A4A but with JSR from
 *    func_8C0E80A6 (0xFFA bytes away, which SHC already treats as out of
 *    range);
 *  - func_8C0E6AEE is called 7 times by func_8C0E8A4A with JSR and its
 *    address is never hoisted into a register, which SHC does only for an
 *    in-TU function out of range (declared extern it gets hoisted).
 * The stubs and gaps reproduce these distances. In the default build the
 * NON_MATCHING functions are ASM_STUBs followed by tu_size stand-ins of
 * about their size, so that func_8C0E80A6 still sees func_8C0E91E4 out of
 * range. func_8C0E79D4 and func_8C0EA040 (called once each, JSR) are
 * plain prototypes.
 */
#include "common.h"
#include "sa1/sa1.h"

/*
 * Probe result of func_8C0EA040 (48 bytes on the lure's stack): three
 * records {attribute, two angles, height}. func_8C0EA040 initialises
 * above.y and water.y to 1000000.0f and below.y to -1000000.0f ("nothing
 * found"); water is the water surface over the lure, below the ground under
 * it, above a surface over it (roles from the use here, unverified).
 * Accessed with constant offsets only (named members, not an array). No
 * pointers.
 */
typedef struct {
    Sint32 attr;                /* 0x00 */
    Angle angx;                 /* 0x04 */
    Angle angz;                 /* 0x08 */
    Float y;                    /* 0x0C */
} LUREHIT;                      /* 0x10 */

typedef struct {
    LUREHIT above;              /* 0x00 */
    LUREHIT below;              /* 0x10 */
    LUREHIT water;              /* 0x20 */
} LURECOLL;                     /* 0x30 */

SA1_ASSERT_SIZE(LUREHIT, 0x10);
SA1_ASSERT_SIZE(LURECOLL, 0x30);

extern task *B_8C7A40C8;        /* lure task (created by BigMain 8C0E0286, cleared by 8C0E6EF6) */
extern task *B_8C7A40CC;        /* hooked fish task (mwp->weight = fish weight) */
extern Uint16 B_8C7A40E8;       /* fishing flags (roles inferred): 1 fish on, 4 line too long, 8 reeling, 0x10 lure on the ground, 0x20 Big pulling, 0x800 line broken, 0x1000 pull input, 0x2000 splash done */
extern Float B_8C7A40EC;        /* tension offset from the reel buttons */
extern NJS_POINT3 B_8C7A40F0;   /* Big's position */
extern Sint32 B_8C7A40FC;       /* frames the fish has been on */
extern Float D_8C565F6C;        /* water surface height */
extern Float D_8C565F70;        /* line length (> 300 / 400 with the upgrade: flag 4) */
extern Float D_8C565F74;        /* reel value (-0.2f per reeling frame) */
extern Float D_8C565F78;        /* line tension 0..1 */
extern Float D_8C565F7C;        /* target tension */
extern Sint32 D_8C565F80;       /* reel SE phase */
extern Sint32 D_8C565F84;       /* tension SE timer */
extern NJS_POINT3 D_8C565FD8[3]; /* stage 8-3 spots */

/* other chunks */
Bool func_8C0569E0(NJS_POINT3 *pos, NJS_VECTOR *v, Angle3 *ang, Float r);  /* sphere sweep (code_8C0EAEC0.c) */
Sint32 func_8C0EB70E(NJS_POINT3 *pos, NJS_VECTOR *v, Angle3 *ang, Float r);
void func_8C0EB25E(task *tp);
void func_8C0EBD22(Sint32 weight, Sint32 n);

/* same TU, out of BSR range (JSR, called once each) */
void func_8C0E79D4(task *tp, NJS_POINT3 *pos);
Float func_8C0EA040(taskwk *twp, Sint32 n, LURECOLL *col);

#if !defined(TARGET_PC)
#define TU_GAP4  __asm_stub_sink(); __asm_stub_sink(); __asm_stub_sink(); __asm_stub_sink();
#define TU_GAP16 TU_GAP4 TU_GAP4 TU_GAP4 TU_GAP4
#define TU_GAP64 TU_GAP16 TU_GAP16 TU_GAP16 TU_GAP16
#endif
/* same TU, out of BSR range (JSR); called 3 + 4 times by func_8C0E8A4A,
 * which must not hoist its address: modelled as an in-TU function */
ASM_STUB(Sint32 func_8C0E6AEE(task *tp, Angle3 *ang))
#if !defined(TARGET_PC)
void tu_gap_8C0E6AEE(void)
{
    TU_GAP64 TU_GAP64 TU_GAP64 TU_GAP64 TU_GAP64
    TU_GAP64 TU_GAP64 TU_GAP64 TU_GAP64 TU_GAP64
    TU_GAP64 TU_GAP64 TU_GAP64 TU_GAP64 TU_GAP64
}
#endif

/* same TU, in BSR range */
ASM_STUB(Sint32 func_8C0E7D5A(NJS_POINT3 *p, NJS_POINT3 *pos))
ASM_STUB(void func_8C0E7E00(task *tp))
#if !defined(TARGET_PC)
void tu_gap_8C0E7E00(void)
{
    TU_GAP64 TU_GAP64 TU_GAP16 TU_GAP16 TU_GAP4
}
#endif
ASM_STUB(void func_8C0E7F60(task *tp, NJS_POINT3 *pos))
void func_8C0E91E4(task *tp);

void func_8C0E80A6(task *tp, NJS_POINT3 *pos)
{
    taskwk *twp;
    motionwk *mwp;
    Float rate;
    Float spd;
    Sint32 n;

    twp = tp->twp;
    mwp = tp->mwp;
    if (B_8C7A40C8 == NULL) {
        return;
    }
    if (B_8C7A40CC != NULL && (B_8C7A40E8 & 1)) {
        rate = 5000.0f / (B_8C7A40CC->mwp->weight + 5000.0f);
    } else {
        rate = 1.0f;
    }
    D_8C565F74 -= 0.19999999f;
    n = D_8C565F80;
    D_8C565F80 += 0x11C7;
    if ((n ^ D_8C565F80) & 0x8000) {
        func_8C0945A0(800);
        func_8C0940DA(800, 0, 0, 0);
    }
    if (D_8C565F70 <= D_8C565F74) {
        func_8C0E7E00(tp);
        return;
    }
    mwp->spd.x = pos->x - twp->pos.x;
    mwp->spd.z = pos->z - twp->pos.z;
    if (func_8C0E7D5A(pos, &twp->pos)) {
        func_8C0E91E4(tp);
    }
    njUnitVector(&mwp->spd);
    spd = rate * 0.19999999f;
    mwp->spd.x *= spd;
    mwp->spd.z *= spd;
    if (GetStageNumber() == 0x803) {
        mwp->spd.y = 0.179999994f;
    } else {
        mwp->spd.y = 0.06f;
    }
    twp->ang.z = tp->mwp->ang_aim.z + 0x2000;
    if ((Sint32)B_8C754C08[0].press & (Sint32)D_8C19E390) {
        B_8C7A40E8 |= 0x1000;
    }
}

void func_8C0E82A0(task *tp, NJS_POINT3 *pos)
{
    NJS_VECTOR v;
    NJS_MATRIX m;
    taskwk *ptwp;
    taskwk *twp;
    motionwk *mwp;
    playerwk *pwp;
    Sint32 ang;

    twp = tp->twp;
    mwp = tp->mwp;
    pwp = playermwp[0]->work.pwp;
    ptwp = playertwp[0];
    if (D_8C565F74 <= D_8C565F70 + 5.0f) {
    mwp->spd.x = pos->x - twp->pos.x;
    mwp->spd.y = pos->y - twp->pos.y;
    mwp->spd.z = pos->z - twp->pos.z;
    njUnitVector(&mwp->spd);
    mwp->spd.x *= 0.5f;
    mwp->spd.y *= 0.5f;
    mwp->spd.z *= 0.5f;
    ang = (Uint16)(tp->mwp->ang_aim.y - ptwp->ang.y);
    if (pwp->mj.reqaction == 57 || pwp->mj.reqaction == 59
            || pwp->mj.reqaction == 71 || pwp->mj.reqaction == 73) {
        if (ang < 0x1555 || ang > 0x8000) {
            v.x = 0.0f;
            v.y = 0.0f;
            v.z = 1.0f;
            njUnitMatrix((NJS_MATRIX_PTR)&m);
            -tp->twp->ang.y ? njRotateY((NJS_MATRIX_PTR)&m, -tp->twp->ang.y) : (void)0;
            njCalcVector((NJS_MATRIX_PTR)&m, &v, &v);
            njAddVector(&mwp->spd, &v);
            njUnitVector(&mwp->spd);
        } else {
            mwp->spd.x = 0.0f;
            mwp->spd.z = 0.0f;
            if (mwp->spd.y >= 0.02f) {
                mwp->spd.y = 0.02f;
            }
            func_8C0E7E00(tp);
            return;
        }
        if (twp->ang.y - mwp->ang_aim.y > -0x2000) {
            twp->ang.y -= 0x100;
        }
    } else if (pwp->mj.reqaction == 56 || pwp->mj.reqaction == 58
            || pwp->mj.reqaction == 70 || pwp->mj.reqaction == 72) {
        if (ang > 0xEAAA || ang < 0x8000) {
            v.x = 0.0f;
            v.y = 0.0f;
            v.z = -1.0f;
            njUnitMatrix((NJS_MATRIX_PTR)&m);
            -tp->twp->ang.y ? njRotateY((NJS_MATRIX_PTR)&m, -tp->twp->ang.y) : (void)0;
            njCalcVector((NJS_MATRIX_PTR)&m, &v, &v);
            njAddVector(&mwp->spd, &v);
            njUnitVector(&mwp->spd);
        } else {
            mwp->spd.x = 0.0f;
            mwp->spd.z = 0.0f;
            if (mwp->spd.y >= 0.02f) {
                mwp->spd.y = 0.02f;
            }
            func_8C0E7E00(tp);
            return;
        }
        if (twp->ang.y - mwp->ang_aim.y < 0x2000) {
            twp->ang.y += 0x100;
        }
    } else {
        mwp->spd.x = 0.0f;
        mwp->spd.z = 0.0f;
        if (mwp->spd.y >= 0.02f) {
            mwp->spd.y = 0.02f;
        }
        func_8C0E7E00(tp);
        return;
    }
    } else {
        if (mwp->spd.y >= 0.02f) {
            mwp->spd.y = 0.02f;
        }
        func_8C0E7E00(tp);
        return;
    }
    mwp->spd.x *= 0.5f;
    mwp->spd.y = 0.04f;
    mwp->spd.z *= 0.5f;
    twp->ang.z = tp->mwp->ang_aim.z + 0x1000;
    if (pwp->mj.nframe == 15.0f) {
        B_8C7A40E8 |= 0x1000;
    }
}

#ifdef NON_MATCHING
/*
 * NON_MATCHING: 9 instructions differ, all FR12 <-> FR13 (the 0.0f held in
 * "zero" and the 0.1f constant / njScalor result swap callee-saved
 * registers). "zero" must be a variable: as literals, the hoisted
 * B_8C7A40EC / 0.001f pair and the D_8C565F7C / 0.02f pair of the step
 * code get FR4/FR5 and FR5/FR6 the other way round (19 differences).
 */
void func_8C0E8562(task *tp, NJS_VECTOR *v)
{
    motionwk *mwp;
    Float w;
    Float d;
    Float f;
    Float diff;
    Float zero;

    mwp = tp->mwp;
    zero = 0.0f;
    if (B_8C7A40C8 != NULL && B_8C7A40CC != NULL) {
        if (B_8C7A40CC->mwp != NULL && B_8C7A40CC->mwp->weight) {
            w = B_8C7A40CC->mwp->weight;
        } else {
            w = 1000.0f;
        }
        w *= 0.0001f;
        if (B_8C7A40E8 & 1) {
            if ((D_8C19E38C | D_8C19E390) & B_8C754C08[0].on) {
                B_8C7A40EC += 0.00049999997f;
            } else if (B_8C7A40EC > 0.0f) {
                B_8C7A40EC -= 0.00099999994f;
            }
            if (B_8C7A40E8 & 0x20) {
                if (B_8C7A40EC > 0.0f) {
                    B_8C7A40EC -= 0.00099999994f;
                }
            }
            if (B_8C7A40E8 & 8) {
                njScalor(v);
                f = njScalor(&B_8C7A40CC->mwp->spd);
                d = f * njCos(B_8C7A40CC->twp->ang.y - mwp->ang_aim.y);
                if (d < 0.0f) {
                    D_8C565F7C = w * 1.09999992f - d * 2.1f;
                    D_8C565F7C = SA1_FMAC((0.24f - w) * (Float)rand(), 0.000036621092f, D_8C565F7C);
                } else {
                    D_8C565F7C = SA1_FMAC(2.0f, w, zero);
                }
                if (D_8C565F7C < 0.5f) {
                    if (B_8C754C08[0].on & D_8C19E390) {
                        D_8C565F7C += 0.5f;
                    } else if (B_8C754C08[0].on & D_8C19E38C) {
                        D_8C565F7C += 0.29999999f;
                    }
                }
                D_8C565F7C += B_8C7A40EC;
                if (B_8C7A40FC > 1800 && D_8C565F7C > 0.79999996f) {
                    D_8C565F7C -= 0.099999996f;
                }
                njAddVector(v, &B_8C7A40CC->mwp->spd);
            } else {
                njScalor(v);
                f = njScalor(&B_8C7A40CC->mwp->spd);
                d = f * njCos(B_8C7A40CC->twp->ang.y - mwp->ang_aim.y);
                if (d < 0.0f) {
                    w -= d * 2.0f;
                } else {
                    w += zero;
                }
                D_8C565F7C = w;
                if (D_8C565F7C < 0.5f) {
                    if (B_8C754C08[0].on & D_8C19E390) {
                        D_8C565F7C += 0.5f;
                    } else if (B_8C754C08[0].on & D_8C19E38C) {
                        D_8C565F7C += 0.29999999f;
                    }
                }
                D_8C565F7C += B_8C7A40EC;
            }
        } else {
            if (B_8C7A40EC > 0.0f) {
                B_8C7A40EC -= 0.00099999994f;
            }
            if (B_8C7A40E8 & 0x20) {
                D_8C565F7C = 0.099999996f;
            } else {
                D_8C565F7C = 0.19999999f;
            }
        }
    } else {
        D_8C565F7C = 0.099999996f;
    }
    SA1_FMAC_ACC(D_8C565F7C, (Float)rand(), 0.0000030517577f);
    if (!B_8C754EC8) {
        if (D_8C565F7C != D_8C565F78) {
            diff = D_8C565F7C - D_8C565F78;
            if (diff > 0.02f) {
                D_8C565F78 += 0.02f;
            } else if (diff < -0.02f) {
                D_8C565F78 -= 0.02f;
            } else if (diff > 0.0019999999f) {
                D_8C565F78 += 0.0019999999f;
            } else if (diff < -0.0019999999f) {
                D_8C565F78 -= 0.0019999999f;
            } else {
                D_8C565F78 = D_8C565F7C;
            }
        }
        if (D_8C565F78 < 0.0f) {
            D_8C565F78 = zero;
        } else if (D_8C565F78 >= 1.0f) {
            D_8C565F78 = 1.0f;
            if (B_8C7A40C8 != NULL) {
                B_8C7A40E8 |= 0x800;
                func_8C107C80(46);
                if (B_8C7A40CC != NULL) {
                    func_8C0EBD22(SA1_FTOI(B_8C7A40CC->mwp->weight), 999);
                }
            }
        }
    }
    if (D_8C565F84) {
        D_8C565F84--;
    }
    if (D_8C565F78 >= 0.79999996f) {
        if (D_8C565F84 == 0) {
            func_8C0940DA(810, 0, 0, 0);
            D_8C565F84 = 120;
        }
    } else {
        func_8C0945A0(810);
    }
}
#else
ASM_STUB(void func_8C0E8562(task *tp, NJS_VECTOR *v))
#if !defined(TARGET_PC)
/* size stand-in for the TU layout model (see the top comment) */
void tu_size_8C0E8562(void)
{
    TU_GAP64 TU_GAP64 TU_GAP64 TU_GAP64 TU_GAP4
}
#endif
#endif

#ifdef NON_MATCHING
/*
 * NON_MATCHING (37 instructions, original 38): the original keeps
 * &B_8C754C08 in R1 across both "(off & mask) == mask" tests and reloads
 * the mask (address and value) for the compare in both; this build reuses
 * the first mask, reloads the base for the second test and puts the flag
 * test constants in R1 (original R2/R3). No C form tried (casts, operand
 * order, volatile, pointer locals, sequential ifs) reproduces it. Control
 * flow and the flag word in R4 (a local: as a CSE temp it goes to R14) are
 * the original's.
 */
Sint32 func_8C0E89A0(task *tp)
{
    Sint32 flag;

    if (B_8C7A40C8 != NULL) {
        flag = B_8C7A40E8;
        if (flag & 8) {
            return 1;
        }
        if (!(flag & 0x20) && (flag & 1)
                && (B_8C754C08[0].off & D_8C19E38C) == D_8C19E38C
                && (B_8C754C08[0].off & D_8C19E390) == D_8C19E390) {
            return 1;
        }
    }
    return 0;
}
#else
ASM_STUB(Sint32 func_8C0E89A0(task *tp))
#if !defined(TARGET_PC)
/* size stand-in for the TU layout model (see the top comment) */
void tu_size_8C0E89A0(void)
{
    TU_GAP4 TU_GAP4
}
#endif
#endif

Sint32 func_8C0E89EC(void)
{
    Sint32 i;

    if (GetStageNumber() != 0x803) {
        return 0;
    }
    for (i = 0; i < 3; i++) {
        if (njDistanceP2P(&D_8C565FD8[i], &B_8C7A40F0) < 70.0f) {
            break;
        }
    }
    if (i == 3) {
        return 0;
    }
    return 1;
}

#ifdef NON_MATCHING
/*
 * NON_MATCHING: the original keeps the combined hit flag of the fish branch
 * in R8 (mine: shares R11 with the hoisted 0) and stores the
 * func_8C0EB70E results to the frame (sp+4, sp+0 in the second branch:
 * 120-byte frame, mine 116), so every frame offset is shifted by 4;
 * otherwise the instruction stream is the original's.
 */
void func_8C0E8A4A(task *tp)
{
    NJS_POINT3 target;
    NJS_POINT3 p;
    NJS_VECTOR v;
    LURECOLL col;
    Angle3 ang1;
    Angle3 ang2;
    Sint32 hit1;
    Sint32 hit2;
    playerwk *pwp;
    taskwk *twp;
    motionwk *mwp;
    Float r;
    Float d;
    Float s;
    Sint32 hit;
    Sint32 hitb;
    Sint32 hit2b;
    Sint32 n;

    twp = tp->twp;
    mwp = tp->mwp;
    pwp = playermwp[0]->work.pwp;
    if (B_8C7A40E8 & 1) {
        r = 5.5f;
    } else {
        r = 0.5f;
    }
    func_8C0EA040(twp, 3, &col);
    if (col.water.y == 1000000.0f) {
        col.water.y = D_8C565F6C;
    } else {
        D_8C565F6C = col.water.y;
    }
    d = col.above.y - col.water.y;
    if (d < 2.0f && d > -2.0f && !func_8C0E89EC()) {
        twp->pos.y = col.above.y;
        func_8C0E91E4(tp);
        return;
    }
    if (func_8C0E89A0(tp)) {
        if (B_8C7A40CC == NULL) {
            return;
        }
        v = B_8C7A40CC->mwp->spd;
        ang1.x = ang1.y = ang1.z = 0;
        p = twp->pos;
        hit1 = func_8C0569E0(&p, &v, &ang1, r);
        ang2.x = ang2.y = ang2.z = 0;
        p = twp->pos;
        hit2 = func_8C0EB70E(&p, &v, &ang2, r);
        hit = hit2 & hit1;
        hit &= func_8C0E6AEE(NULL, &ang2);
        if (hit & func_8C0E6AEE(NULL, &ang1)) {
            v.x = v.z = 0.0f;
            v.y *= 3.0f;
        }
        if (twp->pos.y + v.y > col.water.y - r - 0.099999996f) {
            if (!func_8C0E6AEE(tp, &ang1)) {
                v.y = 0.0f;
                twp->pos.y = col.water.y - r - 0.099999996f;
            }
        }
        if (twp->pos.y + v.y <= col.below.y + r) {
            s = njScalor(&v);
            if (twp->pos.y != col.water.y - r - 0.099999996f) {
                twp->pos.y = col.below.y + r;
                if (s <= 0.03f) {
                    v.x = v.y = v.z = 0.0f;
                }
            } else {
                v.x = v.y = v.z = 0.0f;
            }
        }
        mwp->spd = v;
        twp->pos.x += v.x;
        twp->pos.y += v.y;
        twp->pos.z += v.z;
        func_8C0E8562(tp, &v);
        if (pwp->equipment & 0x800) {
            if (D_8C565F70 > 400.0f) {
                B_8C7A40E8 |= 4;
            }
        } else if (D_8C565F70 > 300.0f) {
            B_8C7A40E8 |= 4;
        }
        return;
    }
    func_8C0E79D4(tp, &target);
    if (B_8C7A40C8 != NULL && (B_8C7A40E8 & 0x20)) {
        func_8C0E82A0(tp, &target);
    } else {
        n = 0;
        if (B_8C754C08[0].on & D_8C19E38C) {
            n = 1;
        }
        if (B_8C754C08[0].on & D_8C19E390) {
            n |= 2;
        }
        switch (n) {
        case 1:
            func_8C0E7F60(tp, &target);
            break;
        case 2:
        case 3:
            func_8C0E80A6(tp, &target);
            break;
        default:
            func_8C0945A0(800);
            func_8C0E7E00(tp);
            break;
        }
    }
    v = mwp->spd;
    ang1.x = ang1.y = ang1.z = 0;
    p = twp->pos;
    hitb = func_8C0569E0(&p, &v, &ang1, r);
    if (GetStageNumber() == 0x803 && ang1.x == 0 && ang1.z == 0x8000) {
        hitb = 0;
        v.y = mwp->spd.y;
    }
    ang2.x = ang2.y = ang2.z = 0;
    p = twp->pos;
    hit2b = func_8C0EB70E(&p, &v, &ang2, r);
    hitb &= hit2b;
    hitb &= func_8C0E6AEE(NULL, &ang2);
    if (hitb & func_8C0E6AEE(NULL, &ang1)) {
        v.x = v.z = 0.0f;
        v.y *= 3.0f;
    }
    if (twp->pos.y + v.y > col.water.y - r - 0.099999996f) {
        if (!(func_8C0E6AEE(tp, &ang1) || func_8C0E6AEE(tp, &ang2))
                || (GetStageNumber() & 0xFF00) == 0x100 || (B_8C7A40E8 & 0x20)) {
            v.y = 0.0f;
            twp->pos.y = col.water.y - r - 0.099999996f;
            mwp->spd.y = 0.0f;
        } else {
            func_8C0E91E4(tp);
            return;
        }
    }
    if (twp->pos.y + v.y <= col.below.y + r) {
        s = njScalor(&v);
        twp->pos.y = col.below.y + r;
        if (B_8C7A40C8 != NULL) {
            B_8C7A40E8 |= 0x10;
            if (!(B_8C7A40E8 & 0x2000) && s >= 0.149999992f) {
                func_8C0EB25E(tp);
                func_8C0940DA(809, 0, 0, 0);
                B_8C7A40E8 |= 0x2000;
            }
            if (s <= 0.03f) {
                v.x = v.y = v.z = 0.0f;
            }
        }
    }
    twp->pos.x += v.x;
    twp->pos.y += v.y;
    twp->pos.z += v.z;
    func_8C0E8562(tp, &v);
    D_8C565F7C = SA1_FMAC(0.0000030517577f, (Float)rand(), D_8C565F7C);
    if (twp->pos.y > col.water.y - r) {
        func_8C0E91E4(tp);
    }
}
#else
ASM_STUB(void func_8C0E8A4A(task *tp))
#if !defined(TARGET_PC)
/* size stand-in for the TU layout model (see the top comment) */
void tu_size_8C0E8A4A(void)
{
    TU_GAP64 TU_GAP64 TU_GAP64 TU_GAP64 TU_GAP64 TU_GAP16 TU_GAP16 TU_GAP16 TU_GAP4 TU_GAP4
}
#endif
#endif

#if !defined(TARGET_PC)
void tu_gap_8C0E9072(void)
{
    TU_GAP16 TU_GAP16 TU_GAP16 TU_GAP4
}
#endif
ASM_STUB(void func_8C0E91E4(task *tp))
