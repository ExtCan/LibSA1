/*
 * 1ST_READ chunk 8C105920: func_8C106140, func_8C10622C, func_8C106580.
 *
 *   func_8C106140  pushes a point out of up to six axis-aligned walls found
 *                  by func_8C1045C8 (entries 0/1: -x/+x, 2/3: -y/+y,
 *                  4/5: -z/+z), keeping it at least r away from each.
 *                  Callers: func_8C10622C, func_8C106580.
 *   func_8C10622C  one step of a flying follower in mode 3 (position
 *                  B_8C7A51F0, velocity B_8C7A51FC): steers toward
 *                  B_8C7A51CC, moves 4 units, keeps the 16-point trail
 *                  B_8C7A5208 (eased toward B_8C7A51E4), updates the yaw
 *                  B_8C7A51DC / pitch B_8C7A51E0 from the velocity and
 *                  returns the new mode, also stored in B_8C7A51D8:
 *                  6 = too far from B_8C7A51E4 (squared distance > 40000),
 *                  5 = farther from it than B_8C7A51CC is, 4 = func_8C104D60
 *                  below 25, else 3. Callers: 8C1032AC, 8C106D96 (switches
 *                  on the result).
 *   func_8C106580  the same in mode 6 with the target B_8C7A51E4 (speed 4
 *                  while the squared distance is above 900, else 1); when
 *                  twp->timer.l runs out or the target is reached (squared
 *                  distance < 2.25) it unhides four model nodes, sets attr
 *                  bit 0x10 of the second collision shape, calls
 *                  func_8C0945D0 and returns mode 7; otherwise it turns the
 *                  yaw toward twp->ang.y + 0x8000 and the pitch toward 0
 *                  when close and returns 6.
 *
 * D_8C5C9290/8D2C/8A20/8FC0 are NJS_OBJECT nodes of a model in 1ST_READ data
 * (see code_8C103200.c); this code mutates them (per-instance copies on PC).
 * func_8C105F48 (same chunk) builds the same local 4-node array.
 */
#include "common.h"
#include "ninja.h"
#include "task.h"
#include "sa1/sa1.h"

/*
 * Wall query result of func_8C1045C8 (0xCC bytes on the stack of
 * func_8C106140). Six 0x20-byte entries copied as whole records; entry i
 * tests axis i >> 1 of the point (FMOV.S @(R0,R8) with R0 = (i >> 1) * 4).
 */
typedef struct {
    Sint32 flag;                /* 0x00 1: wall found */
    Float unk04[3];             /* 0x04 */
    Float d;                    /* 0x10 wall coordinate on the entry's axis */
    Float unk14[3];             /* 0x14 */
} WALLENT;                      /* 0x20 */

typedef struct {
    Float unk00[3];             /* 0x00 */
    WALLENT ent[6];             /* 0x0C */
} WALLINFO;                     /* 0xCC */

SA1_ASSERT_SIZE(WALLENT, 0x20);
SA1_ASSERT_SIZE(WALLINFO, 0xCC);
SA1_ASSERT_OFFSET(WALLENT, d, 0x10);
SA1_ASSERT_OFFSET(WALLINFO, ent, 0x0C);

extern NJS_POINT3 B_8C7A51CC;       /* steering target (mode 3) */
extern Sint32 B_8C7A51D8;           /* mode */
extern Angle B_8C7A51DC;            /* yaw */
extern Angle B_8C7A51E0;            /* pitch */
extern NJS_POINT3 B_8C7A51E4;       /* home / target (mode 6) */
extern NJS_POINT3 B_8C7A51F0;       /* position */
extern NJS_VECTOR B_8C7A51FC;       /* velocity */
extern NJS_POINT3 B_8C7A5208[16];   /* trail, [0] = position */

extern NJS_OBJECT D_8C5C9290;
extern NJS_OBJECT D_8C5C8D2C;
extern NJS_OBJECT D_8C5C8A20;
extern NJS_OBJECT D_8C5C8FC0;

Sint32 func_8C1045C8(WALLINFO *info, NJS_POINT3 *pos, Float r);
Float func_8C104CCA(NJS_POINT3 *a, NJS_POINT3 *b);
Float func_8C104D60(NJS_POINT3 *p);

ASM_STUB(void func_8C105B7C(task *tp))
ASM_STUB(void func_8C105F00(void))
ASM_STUB(void func_8C105F48(void))

void func_8C106140(NJS_POINT3 *pos, Float r)
{
    WALLINFO info;

    if (func_8C1045C8(&info, pos, r)) {
        if (info.ent[0].flag == 1 && r > pos->x - info.ent[0].d) {
            pos->x = info.ent[0].d + r;
        }
        if (info.ent[1].flag == 1 && r > info.ent[1].d - pos->x) {
            pos->x = info.ent[1].d - r;
        }
        if (info.ent[4].flag == 1 && r > pos->z - info.ent[4].d) {
            pos->z = info.ent[4].d + r;
        }
        if (info.ent[5].flag == 1 && r > info.ent[5].d - pos->z) {
            pos->z = info.ent[5].d - r;
        }
        if (info.ent[2].flag == 1 && r > pos->y - info.ent[2].d) {
            pos->y = info.ent[2].d + r;
        }
        if (info.ent[3].flag == 1 && r > info.ent[3].d - pos->y) {
            pos->y = info.ent[3].d - r;
        }
    }
}

/*
 * NON_MATCHING: complete and functionally faithful (7 SA1_FMAC = the 13 asm
 * FMACs, the loop body being unrolled 3x by SHC).
 * 11 instructions differ, all in the setup of the trail loop: SHC schedules
 * the loop-invariant loads (0.85f, 0.15f, &B_8C7A51E4) above the zero-trip
 * guard (CMP/HI) instead of after it. The identical loop in func_8C106580
 * matches; there, dropping the four objs[]->evalflags updates gives this same
 * schedule, so it depends on surrounding register allocation. Tried: index /
 * pointer / while / reversed loops, if-else chains, comparison operand
 * order, local copies of the globals, declaration order.
 */
#ifdef NON_MATCHING
Sint32 func_8C10622C(task *tp)
{
    NJS_VECTOR dir;
    NJS_VECTOR to;
    taskwk *twp;
    Float dist;
    Sint32 i;

    twp = tp->twp;
    func_8C105B7C(tp);
    dir = B_8C7A51FC;
    if (njScalor(&dir) == 0.0f) {
        dir.x = 1.0f;
        dir.y = dir.z = 0.0f;
    } else {
        njUnitVector(&dir);
    }
    to.x = B_8C7A51CC.x - B_8C7A51F0.x;
    to.y = B_8C7A51CC.y - B_8C7A51F0.y;
    to.z = B_8C7A51CC.z - B_8C7A51F0.z;
    if (njScalor(&to) == 0.0f) {
        to = dir;
    } else {
        njUnitVector(&to);
    }
    B_8C7A51FC.x = SA1_FMAC(dir.x, 0.825f, to.x * 0.175f);
    B_8C7A51FC.y = SA1_FMAC(dir.y, 0.825f, to.y * 0.175f);
    B_8C7A51FC.z = SA1_FMAC(dir.z, 0.825f, to.z * 0.175f);
    if (njScalor(&B_8C7A51FC) == 0.0f) {
        B_8C7A51FC = to;
    } else {
        njUnitVector(&B_8C7A51FC);
    }
    B_8C7A51FC.x *= 4.0f;
    B_8C7A51FC.y *= 4.0f;
    B_8C7A51FC.z *= 4.0f;
    njAddVector(&B_8C7A51F0, &B_8C7A51FC);
    func_8C106140(&B_8C7A51F0, 8.0f);
    CWP(twp)->info[1].center = B_8C7A51F0;
    if (func_8C104CCA(&B_8C7A51E4, &B_8C7A51F0) > 40000.0f) {
        return B_8C7A51D8 = 6;
    }
    if ((dist = njDistanceP2P(&B_8C7A51E4, &B_8C7A51F0)) > 20.0f
            && dist > njDistanceP2P(&B_8C7A51E4, &B_8C7A51CC)) {
        return B_8C7A51D8 = 5;
    }
    if (func_8C104D60(&B_8C7A51F0) < 25.0f) {
        return B_8C7A51D8 = 4;
    }
    for (i = 15; i > 0; i--) {
        B_8C7A5208[i].x = SA1_FMAC(B_8C7A5208[i - 1].x, 0.84999998f, B_8C7A51E4.x * 0.149999995f);
        B_8C7A5208[i].y = SA1_FMAC(B_8C7A5208[i - 1].y, 0.84999998f, B_8C7A51E4.y * 0.149999995f);
        B_8C7A5208[i].z = SA1_FMAC(B_8C7A5208[i - 1].z, 0.84999998f, B_8C7A51E4.z * 0.149999995f);
    }
    B_8C7A5208[0] = B_8C7A51F0;
    func_8C105F00();
    B_8C7A51DC = -SA1_FTOI(atan2f(B_8C7A51FC.z, B_8C7A51FC.x) * 10430.381f);
    dist = njSqrt(SA1_FMAC(B_8C7A51FC.x, B_8C7A51FC.x, B_8C7A51FC.z * B_8C7A51FC.z));
    B_8C7A51E0 = SA1_FTOI(atan2f(-B_8C7A51FC.y, dist) * 10430.381f);
    func_8C105F48();
    return B_8C7A51D8 = 3;
}
#else
ASM_STUB(Sint32 func_8C10622C(task *tp))
#endif /* NON_MATCHING */

Sint32 func_8C106580(task *tp)
{
    NJS_VECTOR dir;
    NJS_VECTOR to;
    NJS_OBJECT *objs[4];
    taskwk *twp;
    Float d2;
    Sint32 i;

    twp = tp->twp;
    objs[0] = &D_8C5C9290;
    objs[1] = &D_8C5C8D2C;
    objs[2] = &D_8C5C8A20;
    objs[3] = &D_8C5C8FC0;
    func_8C105B7C(tp);
    d2 = func_8C104CCA(&B_8C7A51E4, &B_8C7A51F0);
    dir = B_8C7A51FC;
    if (njScalor(&dir) == 0.0f) {
        dir.x = 1.0f;
        dir.y = dir.z = 0.0f;
    } else {
        njUnitVector(&dir);
    }
    to.x = B_8C7A51E4.x - B_8C7A51F0.x;
    to.y = B_8C7A51E4.y - B_8C7A51F0.y;
    to.z = B_8C7A51E4.z - B_8C7A51F0.z;
    if (njScalor(&to) == 0.0f) {
        to = dir;
    } else {
        njUnitVector(&to);
    }
    B_8C7A51FC.x = SA1_FMAC(dir.x, 0.825f, to.x * 0.175f);
    B_8C7A51FC.y = SA1_FMAC(dir.y, 0.825f, to.y * 0.175f);
    B_8C7A51FC.z = SA1_FMAC(dir.z, 0.825f, to.z * 0.175f);
    if (njScalor(&B_8C7A51FC) == 0.0f) {
        B_8C7A51FC = to;
    } else {
        njUnitVector(&B_8C7A51FC);
    }
    if (d2 > 900.0f) {
        B_8C7A51FC.x *= 4.0f;
        B_8C7A51FC.y *= 4.0f;
        B_8C7A51FC.z *= 4.0f;
    } else {
        B_8C7A51FC.x *= 1.0f;
        B_8C7A51FC.y *= 1.0f;
        B_8C7A51FC.z *= 1.0f;
    }
    njAddVector(&B_8C7A51F0, &B_8C7A51FC);
    func_8C106140(&B_8C7A51F0, 8.0f);
    CWP(twp)->info[1].center = B_8C7A51F0;
    if (--twp->timer.l <= 0 || d2 < 2.25f) {
        objs[0]->evalflags &= ~NJD_EVAL_HIDE;
        objs[1]->evalflags &= ~NJD_EVAL_HIDE;
        objs[2]->evalflags &= ~NJD_EVAL_HIDE;
        objs[3]->evalflags &= ~NJD_EVAL_HIDE;
        CWP(twp)->info[1].attr |= 0x10;
        func_8C0945D0(twp);
        return B_8C7A51D8 = 7;
    }
    for (i = 15; i > 0; i--) {
        B_8C7A5208[i].x = SA1_FMAC(B_8C7A5208[i - 1].x, 0.84999998f, B_8C7A51E4.x * 0.149999995f);
        B_8C7A5208[i].y = SA1_FMAC(B_8C7A5208[i - 1].y, 0.84999998f, B_8C7A51E4.y * 0.149999995f);
        B_8C7A5208[i].z = SA1_FMAC(B_8C7A5208[i - 1].z, 0.84999998f, B_8C7A51E4.z * 0.149999995f);
    }
    B_8C7A5208[0] = B_8C7A51F0;
    func_8C105F00();
    if (d2 < 900.0f) {
        B_8C7A51DC = AdjustAngle(B_8C7A51DC, twp->ang.y + 0x8000, 0x400);
        B_8C7A51E0 = AdjustAngle(B_8C7A51E0, 0, 0x400);
    }
    func_8C105F48();
    return B_8C7A51D8 = 6;
}
