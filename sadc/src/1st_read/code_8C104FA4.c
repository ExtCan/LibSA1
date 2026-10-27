#include "sa1/pending.h"

/*
 * 1ST_READ 8C104FA4-8C105650 (chunk 8C103200): the capture beam of ZERO
 * (E-100 Zero, the robot that chases Amy; exec func_8C1032AC in
 * code_8C1032AC.c calls both functions in its catch sequence, mode 17).
 * Zero's motion work is the global mtnjvwk B_8C7A5140 (action list
 * D_8C5C69C8).
 *
 * func_8C104FA4 resets the beam: target point B_8C7A5180 = player 0's
 * position plus a random XZ offset of +-5, beam length 0, re-aim timer 17.
 *
 * func_8C105036 (called with Zero's task): the beam starts at node 10 of
 * Zero's current action (offset 4.5, -5, 0), grows by 1 per frame up to
 * 150 and points at the target point, which steers towards player 0
 * (re-aimed every 17..33 frames, else accelerated by 0.01 per axis up to
 * 1). The beam end is the land hit of a ray cast (func_8C02A680); a hit
 * within 5 of the previous frame's hit spawns func_8C105818, every frame
 * spawns a func_8C105650 trail segment, and when a point of the beam
 * (sampled every 4 units) comes within 4 of player 0 (+5 up)
 * func_8C105920 and SE 0x306 are started once (B_8C7A51C8 = 1: Amy is
 * caught).
 */

/* Struct view of the beam line B_8C7A52EC (NJS_POINT3[2], same layout on
 * every target); see the notes of func_8C105036. */
typedef struct {
    NJS_POINT3 start;           /* 0x00 */
    NJS_POINT3 end;             /* 0x0C */
} BEAM_LINE;                    /* 0x18 */

SA1_ASSERT_SIZE(BEAM_LINE, 0x18);

extern mtnjvwk B_8C7A5140;              /* Zero's motion work */
extern PL_ACTION D_8C5C69C8[];          /* Zero's action list */
extern Sint32 B_8C7A5174;               /* frames until the next re-aim */
extern Sint32 B_8C7A5178;               /* 1: the ray hit last frame */
extern Float B_8C7A517C;                /* beam length (0..150) */
extern NJS_POINT3 B_8C7A5180;           /* target point */
extern NJS_VECTOR B_8C7A518C;           /* beam direction (unit) */
extern NJS_POINT3 B_8C7A5198;           /* beam start of the previous frame */
extern NJS_POINT3 B_8C7A51A4;           /* beam end */
extern NJS_POINT3 B_8C7A51B0;           /* beam end of the previous frame */
extern NJS_VECTOR B_8C7A51BC;           /* target point velocity */
extern Sint32 B_8C7A51C8;               /* 1: the beam reached player 0 */
extern Sint32 B_8C7A52E8;               /* non-zero: no ray cast (beam end = full length) */
extern NJS_POINT3 B_8C7A52EC[2];        /* beam line: start, end (D_8C5C6BBC.p) */
extern NJS_MATRIX B_8C7A5308;           /* matrix of node 10 */
extern NJS_COLOR D_8C5C6BB4[2];         /* beam line colours (D_8C5C6BBC.col); [1].a fades with the length */
extern NJS_POINT3COL D_8C5C6BBC;        /* {B_8C7A52EC, D_8C5C6BB4, NULL, 2} */

Sint32 func_8C02A680(LINECOLLI *lc);    /* ray vs land (code_8C02A680.c) */
void func_8C105650(task *tp);           /* beam trail segment (8 frames) */
void func_8C105818(task *tp);           /* beam hit spark */
void func_8C105920(task *tp);           /* the beam caught player 0 */

ASM_STUB(Float func_8C104CCA(NJS_POINT3 *a, NJS_POINT3 *b))
ASM_STUB(Float func_8C104D60(NJS_POINT3 *p))

void func_8C104FA4(void)
{
    B_8C7A5178 = 0;
    B_8C7A5174 = 17;
    B_8C7A517C = 0.0f;
    B_8C7A5180 = playertwp[0]->pos;
    SA1_FMAC_ACC(B_8C7A5180.x, SA1_FMAC((Float)rand(), 0.000030517578125f, -0.5f), 10.0f);
    SA1_FMAC_ACC(B_8C7A5180.z, SA1_FMAC((Float)rand(), 0.000030517578125f, -0.5f), 10.0f);
    B_8C7A51BC.x = B_8C7A51BC.y = B_8C7A51BC.z = 0.0f;
    B_8C7A51C8 = 0;
}

/*
 * NON_MATCHING (complete; 586 of 626 instructions identical): everything
 * but one register-allocation choice of SHC's global CSE. The original
 * keeps the constant 1 in R8 (MOV #1,R8 in the delay slot of the
 * B_8C7A52E8 test, stored to B_8C7A5178 and B_8C7A51C8), loads
 * playertwp/CreateElementalTask from the pool at every use and therefore
 * branches over the "B_8C7A5178 = 0" else-case (BT/S + BRA + NOP). This
 * compile gives R8 to &playertwp (timer/velocity blocks and the ppos copy)
 * and materialises 1 at each store; every other register, the stack
 * layout and all instructions are otherwise the same. Probes: one more use
 * of the constant 1 anywhere between the hit test and the line draw (e.g.
 * an extra call argument 1) makes SHC CSE it, but into R9 (51B0 then gets
 * R8); an extra use in the loop or after it does not. The missing source
 * construct was not found (tried: loop forms, dead locals, constant
 * spellings, inline helpers for the target/ppos code, nested ifs).
 * Findings that do matter and are kept below:
 *  - twp->ang.y as a ?: expression statement (the CSE temporary of the
 *    load goes to the @R15 stack slot, as in the original);
 *  - "if ((B_8C7A517C += 1.0f) > 150.0f)" and the same form for the
 *    velocity clamps (store in the delay slot, no FMOV copy);
 *  - "if ((ctp = CreateElementalTask(...)) != NULL)" (TST R0,R0; BT/S);
 *  - the alpha goes through a #pragma inline helper taking the rate
 *    (FMOV FR0,FR4 / FMOV FR4,FR1 copies, MOV.B R0,@(3,Rn));
 *  - the copy of the beam end is a struct member store (displacement
 *    @(12,Rn)) while the rest of the function indexes B_8C7A52EC as an
 *    array (&B_8C7A52EC[0] is CSE'd into the @R15 slot across the second
 *    njDistanceP2P): BEAM_LINE view for that one store;
 *  - "if (dist > 0) do ... while (dist > 0)" (0x306 and 16.0f are loaded
 *    after the entry test);
 *  - the ppos copy is dead in the original too (func_8C104D60 adds the 5).
 */
#ifdef NON_MATCHING

/* alpha of the beam's end colour from a 0..1 rate (inlined: the original
 * keeps the rate in FR4 like a parameter) */
#ifndef TARGET_PC
#pragma inline(SetBeamAlpha)
#endif
static void SetBeamAlpha(Float rate)
{
    D_8C5C6BB4[1].argb.a = (Uint8)SA1_FTOI(rate * 255.0f);
}

void func_8C105036(task *tp)
{
    taskwk *twp;
    NJS_ACTION *action;
    task *ctp;
    Float dist;

    twp = tp->twp;
    {
        NJS_POINT3 ofs;
        NJS_POINT3 pos;

        njPushMatrix(D_8C67C640);
        ofs.x = 4.5f;
        ofs.y = -5.0f;
        ofs.z = 0.0f;
        njTranslateV(NULL, &twp->pos);
        twp->ang.y ? njRotateY(NULL, twp->ang.y) : (void)0;
        if (B_8C7A5140.mtnmode == 2) {
            action = B_8C7A5140.actwkptr;
        } else {
            action = D_8C5C69C8[B_8C7A5140.reqaction].actptr;
        }
        func_8C0583A0(NULL, action, B_8C7A5140.nframe, 10, B_8C7A5308);
        njCalcPoint(B_8C7A5308, &ofs, &pos);
        B_8C7A52EC[0] = pos;
        njPopMatrix(1);
    }
    if ((B_8C7A517C += 1.0f) > 150.0f) {
        B_8C7A517C = 150.0f;
    }
    if (--B_8C7A5174 <= 0) {
        NJS_VECTOR v;

        B_8C7A5174 = SA1_FTOI((Float)rand() * 0.000518798828125f) + 17;
        v.x = playertwp[0]->pos.x - B_8C7A5180.x;
        v.y = playertwp[0]->pos.y - B_8C7A5180.y + 5.0f;
        v.z = playertwp[0]->pos.z - B_8C7A5180.z;
        if (njScalor(&v) == 0.0f) {
            B_8C7A51BC.x = B_8C7A51BC.y = B_8C7A51BC.z = 0.0f;
        } else {
            njUnitVector(&v);
            B_8C7A51BC = v;
        }
    } else {
        if (B_8C7A5180.x < playertwp[0]->pos.x) {
            if ((B_8C7A51BC.x += 0.01f) > 1.0f) {
                B_8C7A51BC.x = 1.0f;
            }
        } else {
            if ((B_8C7A51BC.x -= 0.01f) < -1.0f) {
                B_8C7A51BC.x = -1.0f;
            }
        }
        if (B_8C7A5180.y < playertwp[0]->pos.y) {
            if ((B_8C7A51BC.y += 0.01f) > 1.0f) {
                B_8C7A51BC.y = 1.0f;
            }
        } else {
            if ((B_8C7A51BC.y -= 0.01f) < -1.0f) {
                B_8C7A51BC.y = -1.0f;
            }
        }
        if (B_8C7A5180.z < playertwp[0]->pos.z) {
            if ((B_8C7A51BC.z += 0.01f) > 1.0f) {
                B_8C7A51BC.z = 1.0f;
            }
        } else {
            if ((B_8C7A51BC.z -= 0.01f) < -1.0f) {
                B_8C7A51BC.z = -1.0f;
            }
        }
    }
    B_8C7A5180.x += B_8C7A51BC.x;
    B_8C7A5180.y += B_8C7A51BC.y;
    B_8C7A5180.z += B_8C7A51BC.z;
    B_8C7A518C.x = B_8C7A5180.x - B_8C7A52EC[0].x;
    B_8C7A518C.y = B_8C7A5180.y - B_8C7A52EC[0].y;
    B_8C7A518C.z = B_8C7A5180.z - B_8C7A52EC[0].z;
    if (njScalor(&B_8C7A518C) == 0.0f) {
        B_8C7A518C.x = 1.0f;
        B_8C7A518C.y = B_8C7A518C.z = 0.0f;
    } else {
        njUnitVector(&B_8C7A518C);
    }
    B_8C7A51A4.x = SA1_FMAC(B_8C7A518C.x, B_8C7A517C, B_8C7A52EC[0].x);
    B_8C7A51A4.y = SA1_FMAC(B_8C7A518C.y, B_8C7A517C, B_8C7A52EC[0].y);
    B_8C7A51A4.z = SA1_FMAC(B_8C7A518C.z, B_8C7A517C, B_8C7A52EC[0].z);
    if (B_8C7A52E8 == 0) {
        LINECOLLI lc;

        lc.len = B_8C7A517C;
        lc.pos = B_8C7A52EC[0];
        lc.vec = B_8C7A518C;
        if (func_8C02A680(&lc) == 1) {
            B_8C7A51A4 = lc.hitpos;
            if (B_8C7A5178 == 1 && func_8C104CCA(&B_8C7A51A4, &B_8C7A51B0) < 25.0f) {
                if ((ctp = CreateElementalTask(2, 6, func_8C105818)) != NULL) {
                    ctp->twp->pos = B_8C7A51A4;
                    ctp->twp->scl = B_8C7A51B0;
                }
            }
            B_8C7A5178 = 1;
        } else {
            B_8C7A5178 = 0;
        }
    } else {
        B_8C7A5178 = 0;
    }
    ((BEAM_LINE *)B_8C7A52EC)->end = B_8C7A51A4;
    SetBeamAlpha(1.0f - njDistanceP2P(&B_8C7A52EC[0], &B_8C7A52EC[1]) * 0.0066666665f);
    if (B_8C754EC8 == 0) {
        njColorBlendingMode(0, 8);
        njColorBlendingMode(1, 10);
        njDrawLine3D(&D_8C5C6BBC, 1, 64);
    }
    if (B_8C7A517C > 1.0f) {
        if ((ctp = CreateElementalTask(2, 6, func_8C105650)) != NULL) {
            ctp->twp->pos = B_8C7A52EC[0];
            ctp->twp->scl = B_8C7A52EC[1];
            ctp->twp->counter.f = B_8C7A51B0.x;
            ctp->twp->timer.f = B_8C7A51B0.y;
            ctp->twp->value.f = B_8C7A51B0.z;
            ctp->twp->ang.x = SA1_FTOI((B_8C7A5198.x - B_8C7A52EC[0].x) * 65536.0f);
            ctp->twp->ang.y = SA1_FTOI((B_8C7A5198.y - B_8C7A52EC[0].y) * 65536.0f);
            ctp->twp->ang.z = SA1_FTOI((B_8C7A5198.z - B_8C7A52EC[0].z) * 65536.0f);
        }
    }
    if (B_8C7A51C8 == 0) {
        NJS_POINT3 p;
        NJS_VECTOR step;
        NJS_POINT3 ppos;

        dist = njDistanceP2P(&B_8C7A52EC[0], &B_8C7A52EC[1]);
        p = B_8C7A52EC[0];
        step = B_8C7A518C;
        step.x *= 4.0f;
        step.y *= 4.0f;
        step.z *= 4.0f;
        ppos = playertwp[0]->pos;
        ppos.y += 5.0f;
        if (dist > 0.0f) {
            do {
                if (func_8C104D60(&p) < 16.0f) {
                    B_8C7A51C8 = 1;
                    CreateElementalTask(2, 6, func_8C105920);
                    func_8C0940DA(0x306, 0, 0, 0);
                    break;
                }
                dist -= 4.0f;
                p.x += step.x;
                p.y += step.y;
                p.z += step.z;
            } while (dist > 0.0f);
        }
    }
    B_8C7A5198 = B_8C7A52EC[0];
    B_8C7A51B0 = B_8C7A51A4;
}
#else
ASM_STUB(void func_8C105036(task *tp))
#endif
