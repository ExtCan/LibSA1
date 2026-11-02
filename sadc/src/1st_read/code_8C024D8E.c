/*
 * 1ST_READ 8C024D8E-8C0255AC (chunk 8C023C20): camera adjusts (transitions)
 * of the adjust table 8C1957D0 (staged by dc-fishcam for 0.9.6;
 * gsa1_090_bigfish.md review R3.3; v1_triggers.md 1.5).
 *
 * The camera task (8C019F4A) calls the current adjust once per step, after
 * the mode function and the conversion into the camera taskwk, with
 * (camera taskwk, the previous step's copy of it *D_8C112044 = B_8C6B6EAC,
 * adjust work *D_8C112050), and then counts the adjust work's counter up.
 * An adjust rewrites the camera work *D_8C112048 between the previous and
 * the new camera, and converts it into the taskwk again.
 *
 *  func_8C024D8E  adjust 0 NONE (a cut): computes the distance between the
 *                 new and the previous camera and drops it.
 *  func_8C025010  adjusts 3 / 4 SLOW: per step the yaw moves by at most a
 *                 quarter of the difference, its speed changing by 0x180
 *                 BAMS per step (snap when closer than 0x180); the pitch
 *                 likewise with steps of 0x20 BAMS, at most 0x200 (snap
 *                 within 0x20); the position moves towards the new one by
 *                 a speed that changes by 0.7 per step, at most a quarter
 *                 of the distance and 80 units (snap within 0.7). Bit 0 of
 *                 the camera work's flag skips the yaw part once. Then
 *                 func_8C01A60C (conversion kind 3).
 *  func_8C02535E  adjusts 6..26 THREE* / RELATIVE* (timed blends; 0x15 is
 *                 the adjust a pop returns with): first step sets up the
 *                 blend (func_8C025482), every step measures the angles from
 *                 the previous camera to player 0 (func_8C025524), t =
 *                 counter / duration (adjust NONE through func_8C01B3E0 at
 *                 t = 1), blends two angles towards 0x1C71 / 0xE38 (40 /
 *                 20 degrees), moves the camera (func_8C025620 before
 *                 adjust 16, func_8C025790 from 16; then func_8C025880,
 *                 func_8C0258F2) and, when the blend's table entry asks for
 *                 it, tests the line previous -> new camera against the land
 *                 (func_8C01E508).
 *  func_8C025482  blend setup: the per-adjust table (21 x {land test,
 *                 duration, step count}, D_8C1122AC, copied to the stack
 *                 like the original's initialised local array), the angle
 *                 differences between the previous camera and the line to
 *                 player 0, and B_8C6B6EA0 kept.
 *  func_8C025524  yaw and pitch from the previous camera to player 0
 *                 (BAMS).
 *
 * Shared BSS of this translation unit: the original addresses B_8C6B74F8
 * as B_8C6B74F4 + 4 and B_8C6B7510 as B_8C6B750C + 4 (one base register),
 * so they are two-element arrays here.
 */
#include "sa1/sa1.h"

/* camera work *D_8C112048 (v1_triggers.md 1.2). No pointers. */
typedef struct {
    Sint32 w00;                 /* 0x00 */
    Sint16 w04;                 /* 0x04 */
    Sint16 flag;                /* 0x06 bit 0: SLOW skips the yaw once */
    Sint32 w08;                 /* 0x08 */
    NJS_POINT3 pos;             /* 0x0C camera position */
    NJS_POINT3 tgt;             /* 0x18 camera target */
    Angle angx;                 /* 0x24 */
    Angle angy;                 /* 0x28 */
    Angle angz;                 /* 0x2C */
} CAMWK_4D8E;

/* adjust work *D_8C112050 (SADX _OBJ_ADJUSTPARAM analogue). No pointers. */
typedef struct {
    Sint16 flag;                /* 0x00 0x20 / 0x40: pitch / yaw moving (cleared on the snap) */
    Sint16 pad02;               /* 0x02 */
    Sint32 angspdx;             /* 0x04 pitch step */
    Sint32 angspdy;             /* 0x08 yaw step */
    Sint32 angspdz;             /* 0x0C */
    Float speed;                /* 0x10 position step */
    Sint32 counter;             /* 0x14 steps of the running adjust */
} CAMADJUSTWK_4D8E;             /* 0x18 */

/* one row of the timed-blend table D_8C1122AC (adjusts 6..26). No
 * pointers. */
typedef struct {
    Sint32 coli;                /* 0x00 nonzero: test the move against the land (8C01E508) */
    Float time;                 /* 0x04 duration in steps */
    Sint32 steps;               /* 0x08 step threshold of 8C025620 */
} CAMTIME_4D8E;                 /* 0x0C */

typedef struct {
    CAMTIME_4D8E e[21];
} CAMTIMETBL_4D8E;              /* 0xFC */

/* an (x, y) angle pair in this TU's BSS: the code reaches .y through the
 * base of .x (MOV.L @(4,Rn)), yet stores a call result into either member
 * through its own address, as SHC does for struct members. No pointers. */
typedef struct {
    Sint32 x;                   /* 0x00 pitch */
    Sint32 y;                   /* 0x04 yaw */
} ANG2_4D8E;

extern CAMWK_4D8E *D_8C112048;
extern const CAMTIMETBL_4D8E D_8C1122AC;
extern NJS_POINT3 B_8C6B6EA0;
extern ANG2_4D8E B_8C6B74F4;    /* angle differences at the blend start (.y = B_8C6B74F8) */
extern ANG2_4D8E B_8C6B7500;    /* pitch / yaw from the previous camera to player 0 (.y = B_8C6B7504) */
extern ANG2_4D8E B_8C6B750C;    /* blended limits (.y = B_8C6B7510) */
extern Float B_8C6B7518;        /* blend ratio t */
extern NJS_POINT3 B_8C6B751C;   /* B_8C6B6EA0 at the blend start */
extern Float B_8C6B7528;        /* blend duration in steps */
extern Sint32 B_8C6B752C;       /* step threshold */
extern Sint32 B_8C6B7530;       /* land test on */
extern Sint32 B_8C6B7534;       /* cleared at the blend start */

Sint32 func_8C01B406(void);                     /* the current adjust */
void func_8C01B3E0(Sint8 adjust);               /* set the adjust, counter 0 */
void func_8C01A60C(taskwk *twp);                /* conversion kind 3 */
void func_8C01E508(NJS_POINT3 *from, NJS_POINT3 *to);  /* line against the land */

void func_8C025482(taskwk *twp, taskwk *otwp, CAMADJUSTWK_4D8E *awp);
void func_8C025524(taskwk *twp, taskwk *otwp, CAMADJUSTWK_4D8E *awp);
void func_8C025620(taskwk *twp, taskwk *otwp, CAMADJUSTWK_4D8E *awp);
void func_8C025790(taskwk *twp, taskwk *otwp, CAMADJUSTWK_4D8E *awp);
void func_8C025880(taskwk *twp, taskwk *otwp, CAMADJUSTWK_4D8E *awp);
void func_8C0258F2(taskwk *twp, taskwk *otwp, CAMADJUSTWK_4D8E *awp);

void func_8C024D8E(taskwk *twp, taskwk *otwp, CAMADJUSTWK_4D8E *awp)
{
    Float dx;
    Float dy;
    Float dz;
    Float dist;

    dx = twp->pos.x - otwp->pos.x;
    dy = twp->pos.y - otwp->pos.y;
    dz = twp->pos.z - otwp->pos.z;
    dist = njSqrt(SH_ONLY(dx * dx + dy * dy + dz * dz) PC_ONLY(SA1_FMAC(dz, dz, SA1_FMAC(dx, dx, dy * dy))));
}

#ifdef NON_MATCHING
/* NON_MATCHING: complete and faithful; register allocation only (53 of 286 instructions):
 * the original keeps the constant 0 in R9 (here R10), 0x8000 / -0x180 in R6 / R7 (swapped),
 * d >> 2 in R0 and the limit in R5 (here R5 / R6), and dx / dy in FR13 / FR14 (swapped). */
void func_8C025010(taskwk *twp, taskwk *otwp, CAMADJUSTWK_4D8E *awp)
{
    Sint32 d;
    Sint32 s;
    Sint32 lim;
    Float dx;
    Float dy;
    Float dz;
    Float dist;
    Float dd;
    Float q;
    Float spd;

    if (D_8C112048->flag & 1) {
        D_8C112048->flag &= ~1;
    } else {
        d = twp->ang.y - otwp->ang.y;
        if (d & 0x8000) {
            d |= 0xFFFF0000;
        } else {
            d &= 0xFFFF;
        }
        if (d > -0x180 && d < 0x180) {
            D_8C112048->angy = twp->ang.y;
            awp->angspdy = 0;
            awp->flag &= ~0x40;
        } else {
            s = d >> 2;
            if (d < 0) {
                lim = awp->angspdy + -0x180;
                if (s > lim) {
                    d = s;
                } else {
                    d = lim;
                }
                awp->angspdy = d;
                D_8C112048->angy = d + otwp->ang.y;
            } else {
                lim = awp->angspdy + 0x180;
                if (s < lim) {
                    d = s;
                } else {
                    d = lim;
                }
                awp->angspdy = d;
                D_8C112048->angy = d + otwp->ang.y;
            }
        }
    }
    d = twp->ang.x - otwp->ang.x;
    if (d & 0x8000) {
        d |= 0xFFFF0000;
    } else {
        d &= 0xFFFF;
    }
    if (d > -0x20 && d < 0x20) {
        D_8C112048->angx = twp->ang.x;
        awp->angspdx = 0;
        awp->flag &= ~0x20;
    } else {
        s = d >> 2;
        if (d < 0) {
            lim = awp->angspdx + -0x20;
            if (s > lim) {
                d = s;
            } else {
                d = lim;
            }
            if (d <= -0x200) {
                d = -0x200;
            }
            awp->angspdx = d;
            D_8C112048->angx = d + otwp->ang.x;
        } else {
            lim = awp->angspdx + 0x20;
            if (s < lim) {
                d = s;
            } else {
                d = lim;
            }
            if (d >= 0x200) {
                d = 0x200;
            }
            awp->angspdx = d;
            D_8C112048->angx = d + otwp->ang.x;
        }
    }
    dx = twp->pos.x - otwp->pos.x;
    dy = twp->pos.y - otwp->pos.y;
    dz = twp->pos.z - otwp->pos.z;
    dist = njSqrt(SH_ONLY(dx * dx + dy * dy + dz * dz) PC_ONLY(SA1_FMAC(dz, dz, SA1_FMAC(dx, dx, dy * dy))));
    spd = awp->speed;
    dd = dist - spd;
    if (dd < 0.69999999f && dd > -0.69999999f) {
        D_8C112048->pos.x = twp->pos.x;
        D_8C112048->pos.y = twp->pos.y;
        D_8C112048->pos.z = twp->pos.z;
        awp->speed = 0.0f;
    } else {
        q = dd * 0.25f;
        if (dd < 0.0f) {
            spd += -0.69999999f;
            if (q > spd) {
                spd = q;
            }
            if (spd < -80.0f) {
                spd = -80.0f;
            }
        } else {
            spd += 0.69999999f;
            if (spd > q) {
                spd = q;
            }
            if (spd > 80.0f) {
                spd = 80.0f;
            }
        }
        awp->speed = spd;
        if (0.0f == dist) {
            D_8C112048->pos.x = otwp->pos.x;
            D_8C112048->pos.y = otwp->pos.y;
            D_8C112048->pos.z = otwp->pos.z;
        } else {
            spd /= dist;
            D_8C112048->pos.x = SA1_FMAC(spd, dx, otwp->pos.x);
            D_8C112048->pos.y = SA1_FMAC(spd, dy, otwp->pos.y);
            D_8C112048->pos.z = SA1_FMAC(spd, dz, otwp->pos.z);
        }
    }
    D_8C112048->angz = 0;
    func_8C01A60C(twp);
}
#else
ASM_STUB(void func_8C025010(taskwk *twp, taskwk *otwp, CAMADJUSTWK_4D8E *awp))
#endif

void func_8C02535E(taskwk *twp, taskwk *otwp, CAMADJUSTWK_4D8E *awp)
{
    func_8C025524(twp, otwp, awp);
    if (awp->counter == 0) {
        func_8C025482(twp, otwp, awp);
    }
    B_8C6B7518 = (Float)awp->counter / B_8C6B7528;
    if (!(B_8C6B7518 < 1.0f)) {     /* FCMP/GT: a NaN ratio also ends the blend, as on the SH-4 */
        B_8C6B7518 = 1.0f;
        func_8C01B3E0(0);
    }
    B_8C6B750C.x = SA1_FTOI(SA1_FMAC((Float)(0x1C71 - B_8C6B74F4.x), B_8C6B7518, (Float)B_8C6B74F4.x));
    B_8C6B750C.y = SA1_FTOI(SA1_FMAC((Float)(0xE38 - B_8C6B74F4.y), B_8C6B7518, (Float)B_8C6B74F4.y));
    if (func_8C01B406() <= 15) {
        func_8C025620(twp, otwp, awp);
    } else {
        func_8C025790(twp, otwp, awp);
    }
    func_8C025880(twp, otwp, awp);
    func_8C0258F2(twp, otwp, awp);
    if (B_8C6B7530 != 0) {
        func_8C01E508(&otwp->pos, &twp->pos);
    }
}

#ifdef NON_MATCHING
/* NON_MATCHING: complete and faithful; two differences, both in the table copy. (1) The symbol
 * map: SHC's __quick_mvn resolves to 8C10CD38 (config/symbols.1st_read.txt) while the game code
 * calls 8C10CDDC for every block move (as PInitialize, code_8C06C300.c). (2) The original's
 * table is an initialised local array (its image D_8C1122AC in this TU's C section), whose copy
 * computes the destination R15+4 a second time (MOV R15,R1 / ADD #4,R1); data stays extern
 * here, so the copy reuses R14 and the rest is the same code one instruction earlier. */
void func_8C025482(taskwk *twp, taskwk *otwp, CAMADJUSTWK_4D8E *awp)
{
    CAMTIME_4D8E tbl[21];

    *(CAMTIMETBL_4D8E *)tbl = D_8C1122AC;
    B_8C6B74F4.x = DiffAngle(otwp->ang.x, B_8C6B7500.x);
    B_8C6B74F4.y = DiffAngle(otwp->ang.y, B_8C6B7500.y);
    B_8C6B751C = B_8C6B6EA0;
    B_8C6B7530 = tbl[func_8C01B406() - 6].coli;
    B_8C6B7528 = tbl[func_8C01B406() - 6].time;
    B_8C6B752C = tbl[func_8C01B406() - 6].steps;
    B_8C6B7534 = 0;
}
#else
ASM_STUB(void func_8C025482(taskwk *twp, taskwk *otwp, CAMADJUSTWK_4D8E *awp))
#endif

void func_8C025524(taskwk *twp, taskwk *otwp, CAMADJUSTWK_4D8E *awp)
{
    NJS_VECTOR v;

    v.x = otwp->pos.x - playertwp[0]->pos.x;
    v.z = otwp->pos.z - playertwp[0]->pos.z;
    B_8C6B7500.y = SA1_FTOI(atan2f(v.x, v.z) * 10430.381f);
    B_8C6B7500.x = SA1_FTOI(atan2f(playertwp[0]->pos.y - otwp->pos.y, njSqrt(SA1_FMAC(v.x, v.x, v.z * v.z))) * 10430.381f);
}

#ifdef NON_MATCHING
/* NON_MATCHING: complete and faithful; register allocation and frame layout only. The original
 * runs out of scratch registers in the counter < steps path: it keeps the counter in R3 and
 * spills it to a 4-byte stack slot under w (frame 28 bytes, w at R15+4), and holds 0.2 / 1.0
 * in FR4 / FR5; SHC here keeps the counter in R14 (frame 24 bytes) and swaps FR4 / FR5. */
void func_8C025620(taskwk *twp, taskwk *otwp, CAMADJUSTWK_4D8E *awp)
{
    NJS_VECTOR v;
    NJS_VECTOR w;
    Sint32 n;

    v.x = (otwp->pos.x - playertwp[0]->pos.x) * (1.0f - B_8C6B7518) * 0.19999999f;
    v.y = (otwp->pos.y - playertwp[0]->pos.y) * (1.0f - B_8C6B7518) * 0.19999999f;
    v.z = (otwp->pos.z - playertwp[0]->pos.z) * (1.0f - B_8C6B7518) * 0.19999999f;
    if (awp->counter < B_8C6B752C) {
        w.x = (twp->pos.x - otwp->pos.x + v.x) * B_8C6B7518;
        w.y = (twp->pos.y - otwp->pos.y + v.y) * B_8C6B7518;
        w.z = (twp->pos.z - otwp->pos.z + v.z) * B_8C6B7518;
        n = awp->counter;
        twp->pos.x = otwp->pos.x + SA1_FMAC((Float)n, w.x, B_8C6B751C.x * (Float)(B_8C6B752C - n)) / (Float)B_8C6B752C;
        n = awp->counter;
        twp->pos.y = otwp->pos.y + SA1_FMAC((Float)n, w.y, B_8C6B751C.y * (Float)(B_8C6B752C - n)) / (Float)B_8C6B752C;
        n = awp->counter;
        twp->pos.z = otwp->pos.z + SA1_FMAC((Float)n, w.z, B_8C6B751C.z * (Float)(B_8C6B752C - n)) / (Float)B_8C6B752C;
    } else {
        twp->pos.x = SA1_FMAC(B_8C6B7518, twp->pos.x - otwp->pos.x + v.x, otwp->pos.x);
        twp->pos.y = SA1_FMAC(B_8C6B7518, twp->pos.y - otwp->pos.y + v.y, otwp->pos.y);
        twp->pos.z = SA1_FMAC(B_8C6B7518, twp->pos.z - otwp->pos.z + v.z, otwp->pos.z);
    }
}
#else
ASM_STUB(void func_8C025620(taskwk *twp, taskwk *otwp, CAMADJUSTWK_4D8E *awp))
#endif

void func_8C025790(taskwk *twp, taskwk *otwp, CAMADJUSTWK_4D8E *awp)
{
    NJS_POINT3 p;
    NJS_VECTOR a;
    NJS_VECTOR b;

    GetPlayerPosition(0, 1, &p, NULL);
    a.x = otwp->pos.x - p.x;
    a.y = otwp->pos.y - p.y;
    a.z = otwp->pos.z - p.z;
    b.x = twp->pos.x - playertwp[0]->pos.x;
    b.y = twp->pos.y - playertwp[0]->pos.y;
    b.z = twp->pos.z - playertwp[0]->pos.z;
    twp->pos.x = SA1_FMAC(b.x - a.x, B_8C6B7518, a.x) + playertwp[0]->pos.x;
    twp->pos.y = SA1_FMAC(b.y - a.y, B_8C6B7518, a.y) + playertwp[0]->pos.y;
    twp->pos.z = SA1_FMAC(b.z - a.z, B_8C6B7518, a.z) + playertwp[0]->pos.z;
}

void func_8C025880(taskwk *twp, taskwk *otwp, CAMADJUSTWK_4D8E *awp)
{
    twp->ang.x = AdjustAngle(otwp->ang.x, twp->ang.x, SA1_FTOI((Float)DiffAngle(twp->ang.x, otwp->ang.x) * B_8C6B7518));
    twp->ang.y = AdjustAngle(otwp->ang.y, twp->ang.y, SA1_FTOI((Float)DiffAngle(twp->ang.y, otwp->ang.y) * B_8C6B7518));
    twp->ang.z = AdjustAngle(otwp->ang.z, twp->ang.z, SA1_FTOI((Float)DiffAngle(twp->ang.z, otwp->ang.z) * B_8C6B7518));
}

void func_8C0258F2(taskwk *twp, taskwk *otwp, CAMADJUSTWK_4D8E *awp)
{
    if (DiffAngle(twp->ang.x, B_8C6B7500.x) > B_8C6B750C.x) {
        twp->ang.x = AdjustAngle(B_8C6B7500.x, twp->ang.x, B_8C6B750C.x);
    }
    if (DiffAngle(twp->ang.y, B_8C6B7500.y) > B_8C6B750C.y) {
        twp->ang.y = AdjustAngle(B_8C6B7500.y, twp->ang.y, B_8C6B750C.y);
    }
}
