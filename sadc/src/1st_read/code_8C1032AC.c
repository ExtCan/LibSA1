/*
 * 1ST_READ chunk 8C103200: func_8C1032AC, the exec of ZERO (E-100 Zero,
 * the robot that chases Amy; created by func_8C102EAE, exec installed by
 * func_8C1030F0, both in code_8C102EAE.c). Motion work: the global mtnjvwk
 * B_8C7A5140 (action list D_8C5C69C8), played by PSetMotion every frame.
 *
 * twp->mode:
 *   1      wait timer.l frames, then drop (scl.y = -1) -> 5
 *   5      falling; on the ground (flag & 1) face player 0 -> 10 (walk,
 *          action 5) or -> 6 (action 14); a hard landing only thuds
 *   6      action 15: shock task at timer 30 (func_8C106B06), then pick a
 *          heading in ang.x and play action 16 backwards -> 8
 *   8      turn the head node (ang.z, D_8C5CC344->ang[1]) toward ang.x -> 9
 *   9      turn the body by the head angle -> 10
 *   10     walk toward player 0 (scl.x = speed); when timer.l runs out stop,
 *          maybe jump (-> 14, action 17) or hop with dust (-> 5); spawns
 *          the catch probe (func_8C104B60) -> 16
 *   11     scl.x = 3 while timer.l runs, then brake -> 5
 *   12/13  knocked back (Amy's hammer: twp->flag & 4 in the pre-switch)
 *   14     jump; action 18 frame 8..9 spawns the shock wave (func_8C106BCA)
 *   15     destroyed: timer.l frames of dust, then the task dies
 *   16..20 catch / carry / throw sequence (B_8C7A51C8 = 1 while holding
 *          Amy; func_8C105036, func_8C105C08, func_8C10622C, func_8C106580)
 * After the switch: face player 0 (16/17) or B_8C7A51CC (18/19), CCL
 * entry, PSetMotion, fade twp->value.f (draw alpha, 0.15 when Zero hides
 * player 0 from the camera), sparks while damaged (B_8C7A52D0 == 1), run
 * the child tasks with value.f = 1.
 *
 * STATUS: NON_MATCHING (complete; functionally faithful to the original,
 * every case and branch). 1956 vs 1947 instructions, ~330 differing lines
 * in an aligned diff.
 * Remaining differences: (1) the original calls func_8C104D60 with
 * JSR/JSR/BSR (8C103E2A/8C103EB8/8C103F64): SHC picks BSR by the distance to
 * the callee inside the translation unit, so this needs func_8C1045C8..
 * func_8C104D60 in the same file (with only ASM_STUBs all three become
 * BSR); (2) register allocation: R10/R11 (ptwp vs the &B_8C7A52D0/&rand
 * constant) are swapped, and in the pre-switch knock-back block the
 * original keeps 1 - B_8C7A52D0 / 2 in FR4 and spills dz (mine: dz in
 * FR4); this shifts the frame (124 vs 128 bytes) and the order of the
 * per-web spill slots of dx/dz; (3) tail merges: the original shares
 * "reqaction = 5; mode = 10" between modes 5 and 9 and "mode = 5"
 * (mode 14) with modes 17/18 (a goto reproduces the first but perturbs the
 * global allocation).
 */
#include "common.h"
#include "ninja.h"
#include "task.h"
#include "sa1/sa1.h"
#include "sa1/shc_intrinsics.h"

extern Sint16 D_8C19B468;           /* 1: event mode (only draw) */
extern mtnjvwk B_8C7A5140;          /* Zero's motion work */
extern Float B_8C7A516C;            /* motion speed multiplier (B_8C7A5140.workp) */
extern Float B_8C7A5170;            /* frame accumulator of func_8C103252 */
extern Sint32 B_8C7A51C8;           /* grab state (1 = holding Amy) */
extern NJS_POINT3 B_8C7A51CC;       /* steering target (modes 18/19) */
extern Sint32 B_8C7A51D8;
extern Sint32 B_8C7A52CC;
extern Float B_8C7A52D0;            /* damage (0..1) */
extern Float B_8C7A52D4;            /* jump probability (0..1) */
extern Float B_8C88F5E4;            /* screen scale */
extern taskwk *D_8C18B7EC;          /* camera taskwk */
extern PL_ACTION D_8C5C69C8[];      /* Zero's action list (data) */
extern NJS_OBJECT D_8C5CC344[];     /* Zero model node (data, written here: per instance on PC) */

extern Float func_8C09222C(taskwk *twp, Float r);                       /* blob shadow */
extern void func_8C0465E0(NJS_POINT3 *pos, NJS_POINT3 *spd, Float scl);  /* dust effect */
extern void func_8C09E3E8(NJS_POINT3 *pos, NJS_POINT3 *vec, Float scl);  /* spark effect */
extern Sint32 func_8C0941FC(Sint32 se, taskwk *twp, Sint32 a, Sint32 b);
extern void func_8C109EAC(Sint32 pno, Sint32 a, Sint32 b, Sint32 c);    /* pad vibration */
extern Sint32 func_8C0DDDE6(Sint32 pno);                                /* code_8C0DDA00.c */
extern void func_8C102E80(void);
extern Float func_8C104D60(NJS_POINT3 *pos);                            /* squared distance to player 0 (+5 up) */
extern Sint32 func_8C104B60(task *tp);
extern void func_8C104FA4(void);
extern void func_8C105036(task *tp);
extern Sint32 func_8C105C08(task *tp);
extern Sint32 func_8C10622C(task *tp);
extern Sint32 func_8C106580(task *tp);
extern void func_8C10698C(task *tp);
extern void func_8C1069DE(task *tp);
extern void func_8C106A38(task *tp);
extern void func_8C106B06(task *tp);
extern void func_8C106BCA(task *tp);
extern void func_8C10760C(task *tp);
extern void func_8C10762C(task *tp);
extern Sint32 func_8C10776A(void);

ASM_STUB(void func_8C103200(taskwk *twp))
ASM_STUB(Bool func_8C103252(Float spd))

void func_8C104800(task *tp);
void func_8C104DBC(task *tp);

#ifdef NON_MATCHING

/* yaw (Angle) of the vector (dx, dz) as the game computes it */
#define ZERO_ANG(dx, dz) SA1_FTOI(atan2f((dz), -(dx)) * 10430.381f)
/* rand() scaled to 0..1 */
#define ZERO_RAND() ((Float)rand() * (1.0f / 32768.0f))

void func_8C1032AC(task *tp)
{
    NJS_POINT3 p0;
    NJS_POINT3 p1;
    NJS_POINT2 s0;
    NJS_POINT2 s1;
    taskwk *twp;
    taskwk *ptwp;
    task *ctp;
    Float dist;
    Float dx;
    Float dz;
    Float spd;
    Float lim;
    Float alpha;
    Float d0;
    Float d1;
    Float r0;
    Float r1;
    Float sx;
    Float sy;
    Sint32 ang;
    Sint32 i;
    Sint32 flag;
    Uint16 act;
    Float frame;

    twp = tp->twp;
    ptwp = playertwp[0];
    if (D_8C19B468 == 1) {
        twp->value.f = 0.5f;
        func_8C104DBC(tp);
        func_8C09222C(twp, 1.8f);
        B_8C7A51C8 = 0;
        return;
    }
    if (ptwp == NULL) {
        goto die;
    }
    dist = func_8C104D60(&tp->twp->pos);
    if (dist > 160000.0f) {
        goto die;
    }
    if (twp->mode != 15 && (twp->btimer & 1)) {
        twp->timer.l = 60;
        twp->scl.x = 0.0f;
        twp->scl.y = 0.0f;
        twp->scl.z = 0.0f;
        B_8C7A5140.reqaction = 19;
        twp->mode = 15;
    }
    if (func_8C049A4E() == 1) {
        return;
    }
    if (twp->mode != 18 && twp->mode != 19 && twp->mode != 13 && twp->mode != 12
        && twp->mode != 15 && (twp->flag & 4)) {
        func_8C0945D0(twp);
        if (1.0f > B_8C7A52D0) {
            func_8C103200(twp);
            dx = ptwp->pos.x - twp->pos.x;
            dz = ptwp->pos.z - twp->pos.z;
            B_8C7A516C = SA1_FMAC(0.5f, 1.0f - B_8C7A52D0, B_8C7A52D0 * 2.0f);
            spd = (1.0f - B_8C7A52D0 * 0.5f) * 1.0f;
            twp->scl.y = spd;
            ang = ZERO_ANG(dx, dz);
            if (DiffAngle(twp->ang.y, ang) < 0x4000) {
                twp->ang.y = ang;
                twp->scl.x = -spd;
                B_8C7A5140.reqaction = 2;
                twp->mode = 12;
            } else {
                twp->ang.y = ang + 0x8000;
                twp->scl.x = spd;
                B_8C7A5140.reqaction = 0;
                twp->mode = 13;
            }
            B_8C7A52D0 += 0.19999999f;
            if (B_8C7A52D0 > 1.0f) {
                B_8C7A52D0 = 1.0f;
            }
            func_8C09413A(0x2FF, 0, 0, 50, twp->pos.x, twp->pos.y, twp->pos.z);
        }
    }
    switch (twp->mode) {
    case 1:
        if (--twp->timer.l > 0) {
            break;
        }
        twp->scl.z = 0.0f;
        twp->scl.x = 0.0f;
        twp->scl.y = -1.0f;
        twp->mode = 5;
        break;
    case 5:
        func_8C10760C(tp);
        func_8C10762C(tp);
        func_8C104800(tp);
        if (twp->flag & 1) {
            if (0.099999996f > _builtin_fabsf(twp->scl.y)) {
                dx = ptwp->pos.x - twp->pos.x;
                dz = ptwp->pos.z - twp->pos.z;
                if (DiffAngle(twp->ang.y, ZERO_ANG(dx, dz)) < 0x4000) {
                    twp->timer.l = 120;
                    B_8C7A5140.reqaction = 5;
                    twp->mode = 10;
                } else {
                    twp->timer.l = 30;
                    B_8C7A5140.reqaction = 14;
                    twp->mode = 6;
                }
            } else {
                func_8C09413A(0x2FA, 0, 0, 50, twp->pos.x, twp->pos.y, twp->pos.z);
                func_8C109EAC(0, 4, 15, 0);
            }
        }
        break;
    case 6:
        switch (B_8C7A5140.reqaction) {
        case 15:
            if (twp->timer.l == 30) {
                B_8C7A52CC = 0;
                if ((ctp = CreateElementalTask(2, 6, func_8C106B06)) != NULL) {
                    ctp->twp->pos = twp->pos;
                    ctp->twp->pos.y += 15.0f;
                    ctp->disp = func_8C106A38;
                }
                func_8C0940DA(0x2F8, 0, 0, 0);
            }
            if (--twp->timer.l > 0) {
                break;
            }
            if (B_8C7A52D4 > 0.9f && 0.5f > ZERO_RAND()) {
                twp->ang.x = SA1_FTOI((Float)rand() * 2.0f);
            } else {
                dx = ptwp->pos.x - twp->pos.x;
                dz = ptwp->pos.z - twp->pos.z;
                twp->ang.x = SubAngle(twp->ang.y, ZERO_ANG(dx, dz));
                twp->ang.x += SA1_FTOI(SA1_FMAC((Float)rand(), 1.0f / 32768.0f, -0.5f) * 16384.0f);
            }
            twp->ang.z = 0;
            /* play action 16 backwards from its last frame */
            frame = (Float)D_8C5C69C8[16].actptr->motion->nbFrame - 1.0f;
            act = 16;
            B_8C7A5140.reqaction = act;
            B_8C7A5170 = frame;
            B_8C7A5140.start_frame = frame;
            break;
        case 16:
            if (func_8C103252(-1.0f) == 1) {
                twp->mode = 8;
                func_8C09413A(0x304, 0, 0, 50, twp->pos.x, twp->pos.y, twp->pos.z);
            }
            break;
        }
        break;
    case 8:
        twp->ang.z = AdjustAngle(twp->ang.z, twp->ang.x, 0x800);
        D_8C5CC344->ang[1] = twp->ang.z;
        if (DiffAngle(twp->ang.x, twp->ang.z) < 0x800) {
            twp->mode = 9;
            func_8C09413A(0x304, 0, 0, 50, twp->pos.x, twp->pos.y, twp->pos.z);
        }
        break;
    case 9:
        i = AdjustAngle(twp->ang.z, 0, 0x800);
        twp->ang.y += SubAngle(i, twp->ang.z);
        twp->ang.z = i;
        D_8C5CC344->ang[1] = twp->ang.z;
        if (DiffAngle(twp->ang.z, 0) < 0x800) {
            twp->timer.l = SA1_FTOI(SA1_FMAC((Float)rand(), 60.0f / 32768.0f, 120.0f));
            B_8C7A5140.reqaction = 5;
            twp->mode = 10;
        }
        break;
    case 10:
        twp->timer.l--;
        if (ptwp->pos.y - twp->pos.y > 20.0f) {
            twp->timer.l = 0;
        }
        if ((twp->flag & 0x100) && dist < 10000.0f) {
            twp->timer.l = 0;
        }
        if (func_8C0DDDE6(0) != 0 && dist < 400.0f) {
            dx = ptwp->pos.x - twp->pos.x;
            dz = ptwp->pos.z - twp->pos.z;
            if (DiffAngle(twp->ang.y, ZERO_ANG(dx, dz)) < 0x4000) {
                twp->timer.l = 0;
            }
        }
        if (twp->timer.l > 0) {
            twp->scl.x += 0.04f;
            if (1.25f < twp->scl.x) {
                twp->scl.x = 1.25f;
            }
            if (dist > 10000.0f) {
                dx = ptwp->pos.x - twp->pos.x;
                dz = ptwp->pos.z - twp->pos.z;
                twp->ang.y = AdjustAngle(twp->ang.y, ZERO_ANG(dx, dz), 0x400);
            }
        } else {
            twp->scl.x += -0.099999996f;
            if (0.0f > twp->scl.x) {
                twp->scl.x = 0.0f;
                B_8C7A52D4 += 0.125f;
                if (B_8C7A52D4 > 1.0f) {
                    B_8C7A52D4 = 1.0f;
                }
                if (dist < 22500.0f && B_8C7A52D4 > ZERO_RAND()) {
                    B_8C7A52D4 *= 0.5f;
                    B_8C7A5140.reqaction = 17;
                    twp->mode = 14;
                    break;
                }
                if (!(ptwp->pos.y - twp->pos.y > 20.0f)) {
                    twp->mode = 5;
                    break;
                }
                twp->scl.x = 1.0f;
                twp->scl.y = 4.0f;
                twp->mode = 5;
                func_8C09413A(0x303, 0, 0, 50, twp->pos.x, twp->pos.y, twp->pos.z);
                for (i = 5; i != 0; i--) {
                    p0.x = 0.5f - (Float)rand() * (1.0f / 32768.0f);
                    p0.y = -((Float)rand() * (1.0f / 65536.0f));
                    p0.z = 0.5f - (Float)rand() * (1.0f / 32768.0f);
                    func_8C0465E0(&twp->pos, &p0, 1.0f);
                }
            }
        }
        func_8C10760C(tp);
        func_8C10762C(tp);
        func_8C104800(tp);
        if (func_8C104B60(tp) == 1) {
            if ((ctp = CreateElementalTask(2, 6, func_8C1069DE)) != NULL) {
                ctp->twp->pos = twp->pos;
                ctp->twp->value.ptr = twp;
                ctp->disp = func_8C10698C;
            }
            func_8C0940DA(0x2F8, 0, 0, 0);
            B_8C7A52D4 = 0.0f;
            B_8C7A5140.reqaction = 4;
            twp->mode = 16;
        }
        break;
    case 11:
        twp->timer.l--;
        if (twp->timer.l > 0) {
            twp->scl.x = 3.0f;
        } else {
            twp->scl.x += -0.5f;
            if (0.0f > twp->scl.x) {
                twp->scl.x = 0.0f;
                twp->mode = 5;
                func_8C0940DA(0x4A0, 0, 0, 0);
                break;
            }
        }
        func_8C10762C(tp);
        break;
    case 2:
        break;
    case 16:
        if (B_8C7A5140.reqaction == 20) {
            twp->scl.x = 0.0f;
            twp->timer.l = 120;
            B_8C7A5140.reqaction = 12;
            twp->mode = 17;
            break;
        }
        if (njDistanceP2P(&twp->pos, &ptwp->pos) > 40.0f) {
            twp->scl.x += 0.04f;
            if (0.625f < twp->scl.x) {
                twp->scl.x = 1.25f;
            }
        } else {
            twp->scl.x += -0.099999996f;
            if (0.0f > twp->scl.x) {
                twp->scl.x = 0.0f;
            }
        }
        if (B_8C7A5140.nframe < 30.0f) {
            if ((SA1_FTOI(B_8C7A5140.nframe) & 7) == 0) {
                func_8C09413A(0x2F7, 0, 0, 50, twp->pos.x, twp->pos.y, twp->pos.z);
            }
        }
        func_8C10760C(tp);
        func_8C10762C(tp);
        func_8C104800(tp);
        break;
    case 17:
        switch (B_8C7A5140.reqaction) {
        case 12:
            if (B_8C7A5140.nframe <= 1.0f) {
                func_8C104FA4();
            } else if (B_8C7A5140.nframe >= 19.0f) {
                if (--twp->timer.l > 0) {
                    func_8C105036(tp);
                    if (B_8C7A51C8 == 1 && twp->timer.l > 60) {
                        twp->timer.l = 60;
                    }
                } else {
                    twp->timer.l = 14;
                    B_8C7A5140.nframe = (Float)twp->timer.l;
                    B_8C7A5140.reqaction = B_8C7A5140.action = B_8C7A5140.lastaction = B_8C7A5140.nextaction = 13;
                }
                func_8C094330(0x305, twp, 1, 0, 4);
            }
            break;
        case 13:
            if (--twp->timer.l <= 1) {
                B_8C7A5140.reqaction = 20;
            } else {
                B_8C7A5140.nframe = (Float)twp->timer.l;
            }
            break;
        default:
            if (B_8C7A51C8 == 1) {
                twp->timer.l = 180;
                B_8C7A5140.reqaction = 9;
                twp->mode = 18;
                B_8C7A51D8 = 2;
            } else {
                twp->mode = 5;
            }
            break;
        }
        if (njDistanceP2P(&twp->pos, &ptwp->pos) > 40.0f) {
            twp->scl.x += 0.04f;
            if (0.625f < twp->scl.x) {
                twp->scl.x = 1.25f;
            }
        } else {
            twp->scl.x += -0.099999996f;
            if (0.0f > twp->scl.x) {
                twp->scl.x = 0.0f;
            }
        }
        func_8C10760C(tp);
        func_8C10762C(tp);
        func_8C104800(tp);
        if (func_8C104D60(&twp->pos) > 22500.0f || ptwp->pos.y - twp->pos.y > 20.0f
            || func_8C0DDDE6(0) == 1) {
            B_8C7A51C8 = 0;
            twp->mode = 5;
        }
        break;
    case 18:
        if (B_8C7A5140.reqaction == 11 && B_8C7A5140.nframe >= 16.0f) {
            if (B_8C7A51C8 == 1 && func_8C105C08(tp) == 0) {
                func_8C09413A(0x2FB, 0, 0, 50, twp->pos.x, twp->pos.y, twp->pos.z);
                twp->timer.l = 60;
                twp->mode = 19;
            } else {
                twp->mode = 5;
            }
        } else if (B_8C7A51C8 == 1 && func_8C104D60(&twp->pos) > 40000.0f) {
            B_8C7A51C8 = 0;
        }
        break;
    case 19:
        if (--twp->timer.l <= 0) {
            B_8C7A51C8 = 0;
            twp->timer.l = 180;
            twp->mode = 20;
            break;
        }
        if (B_8C7A51C8 == 1 && func_8C104D60(&twp->pos) > 40000.0f) {
            B_8C7A51C8 = 0;
            break;
        }
        switch (func_8C10622C(tp)) {
        case 3:
            break;
        case 1:
        case 4:
        case 5:
        case 6:
            B_8C7A51C8 = 0;
            func_8C0945D0(twp);
            func_8C0941FC(0x2F9, twp, 1, 0);
            twp->timer.l = 180;
            twp->mode = 20;
            break;
        }
        break;
    case 20:
        if (func_8C106580(tp) == 7) {
            func_8C0945D0(twp);
            twp->mode = 5;
        }
        break;
    case 12:
    case 13:
        if (B_8C7A5140.reqaction == 20) {
            twp->mode = 5;
            break;
        }
        func_8C10760C(tp);
        func_8C10762C(tp);
        func_8C104800(tp);
        if (!(twp->flag & 1)) {
            break;
        }
        twp->scl.x *= 0.9f;
        twp->scl.z *= 0.9f;
        if (B_8C7A5140.reqaction == 0 || B_8C7A5140.reqaction == 2) {
            p0 = twp->pos;
            SA1_FMAC_ACC(p0.x, 0.5f - ZERO_RAND(), 16.0f);
            p0.y -= 8.0f;
            SA1_FMAC_ACC(p0.z, 0.5f - ZERO_RAND(), 16.0f);
            p1.x = p1.y = p1.z = 0.0f;
            func_8C0465E0(&p0, &p1, 1.0f);
        }
        if ((B_8C7A5140.reqaction == 1 || B_8C7A5140.reqaction == 3) && 1.0f > B_8C7A5140.nframe) {
            func_8C09413A(0x307, 0, 0, 50, twp->pos.x, twp->pos.y, twp->pos.z);
        }
        if (njScalor(&twp->scl) > 0.5f) {
            func_8C09413A(0x2FC, 0, 0, 50, twp->pos.x, twp->pos.y, twp->pos.z);
            func_8C109EAC(0, 4, 15, 0);
        }
        break;
    case 14:
        if (B_8C7A5140.reqaction == 20) {
            twp->mode = 5;
        } else if (B_8C7A5140.reqaction == 18) {
            if (8.0f > B_8C7A5140.nframe) {
                break;
            }
            if (!(9.0f > B_8C7A5140.nframe)) {
                break;
            }
            if ((ctp = CreateElementalTask(2, 6, func_8C106BCA)) != NULL) {
                ctp->twp->pos = twp->pos;
                ctp->twp->pos.x += -(njCos(twp->ang.y) * 15.0f);
                SA1_FMAC_ACC(ctp->twp->pos.z, njSin(twp->ang.y), 15.0f);
                ctp->twp->pos.y += -12.0f;
            }
            func_8C09413A(0x2FA, 0, 0, 50, twp->pos.x, twp->pos.y, twp->pos.z);
            func_8C09413A(0x2C6, 0, 0, 50, twp->pos.x, twp->pos.y, twp->pos.z);
        }
        break;
    case 15:
        B_8C7A51C8 = 0;
        twp->scl.y += 0.19999999f;
        if (4.0f < twp->scl.y) {
            twp->scl.y = 4.0f;
        }
        func_8C10762C(tp);
        p0.x = 0.5f - (Float)rand() * (1.0f / 32768.0f);
        p0.y = -((Float)rand() * (1.0f / 65536.0f));
        p0.z = 0.5f - (Float)rand() * (1.0f / 32768.0f);
        func_8C0465E0(&twp->pos, &p0, 1.0f);
        if (--twp->timer.l > 0) {
            break;
        }
    die:
        func_8C0945D0(twp);
        func_8C10776A();
        return;
    }
    switch (twp->mode) {
    case 16:
    case 17:
        dx = ptwp->pos.x - twp->pos.x;
        dz = ptwp->pos.z - twp->pos.z;
        twp->ang.y = AdjustAngle(twp->ang.y, ZERO_ANG(dx, dz), 0x400);
        break;
    case 18:
    case 19:
        dx = B_8C7A51CC.x - twp->pos.x;
        dz = B_8C7A51CC.z - twp->pos.z;
        twp->ang.y = AdjustAngle(twp->ang.y, ZERO_ANG(dx, dz), 0x400);
        break;
    }
    CCL_Entry(twp);
    PSetMotion(&B_8C7A5140);
    flag = 0;
    p0 = CWP(ptwp)->info->center;
    p1 = twp->pos;
    p1.y += 2.0f;
    d0 = njDistanceP2P(&D_8C18B7EC->pos, &p0);
    d1 = njDistanceP2P(&D_8C18B7EC->pos, &p1);
    if (10.0f > d1) {
        flag = 1;
    } else if (d0 > d1) {
        njProjectScreen(NULL, &p0, &s0);
        njProjectScreen(NULL, &p1, &s1);
        r0 = B_8C88F5E4 * 5.0f / d0;
        r1 = B_8C88F5E4 * 10.0f / d1;
        sx = s0.x - s1.x;
        sy = s0.y - s1.y;
        if (r0 + r1 > njSqrt(SA1_FMAC(sx, sx, sy * sy))) {
            flag = 1;
        }
    }
    if (flag == 1) {
        lim = 0.149999992f;
    } else {
        lim = 1.0f;
    }
    if (lim > twp->value.f) {
        twp->value.f += 0.049999998f;
        if (twp->value.f > 1.0f) {
            twp->value.f = 1.0f;
        }
    } else if (twp->value.f > lim) {
        twp->value.f -= 0.049999998f;
        if (0.0f > twp->value.f) {
            twp->value.f = 0.0f;
        }
    }
    func_8C104DBC(tp);
    i = twp->mode;              /* the original stores it to the stack (sp0) */
    if (i != 13 && i != 12 && B_8C7A52D0 == 1.0f && 0.049999998f > ZERO_RAND()) {
        p0 = twp->pos;
        SA1_FMAC_ACC(p0.x, 0.5f - ZERO_RAND(), 10.0f);
        p0.y += 15.0f;
        SA1_FMAC_ACC(p0.z, 0.5f - ZERO_RAND(), 10.0f);
        p1.x = 0.5f - ZERO_RAND();
        p1.y = 0.5f;
        p1.z = 0.5f - ZERO_RAND();
        func_8C09E3E8(&p0, &p1, 0.005f);
    }
    alpha = twp->value.f;
    twp->value.f = 1.0f;
    func_8C098E98(tp);
    twp->value.f = alpha;
    func_8C102E80();
}

#else
ASM_STUB(void func_8C1032AC(task *tp))
#endif /* NON_MATCHING */

ASM_STUB(void func_8C104800(task *tp))
ASM_STUB(void func_8C104DBC(task *tp))
