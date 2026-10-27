#include "common.h"
#include "ninja.h"
#include "task.h"

typedef struct {
    Uint16 unk0;
    Uint16 unk2;
} UNK_8C01622A_param;

/* Work of a small object that circles/follows a target point. */
typedef struct UNK_8C01622A_work {
    Uint8 mode;                 /* 0x00 */
    Uint8 flag;                 /* 0x01 */
    Sint8 pad2[2];              /* 0x02 */
    Float unk4;                 /* 0x04 */
    NJS_POINT3 spd;             /* 0x08 */
    NJS_POINT3 acc;             /* 0x14 */
    Sint8 pad20[0x18];          /* 0x20 */
    Angle ang;                  /* 0x38 */
    Float rad;                  /* 0x3C */
    Sint8 pad40[8];             /* 0x40 */
    NJS_POINT3 target;          /* 0x48 */
    NJS_POINT3 prev;            /* 0x54 */
    void (*exec)(struct UNK_8C01622A_work *wk, taskwk *twp); /* 0x60 */
    Sint32 unk64;               /* 0x64 */
    UNK_8C01622A_param *param;  /* 0x68 */
} UNK_8C01622A_work;

extern taskwk *B_8C78C608[]; /* playertwp[] */

extern Sint32 rand(void); /* rand() */
extern Float atan2f(Float y, Float x); /* atan2f */
extern Float njCos(Angle ang); /* 8C63A970 (FSCA cosine) */
extern Float njSin(Angle ang); /* 8C63A9B8 (FSCA sine) */
extern Float njUnitVector(NJS_VECTOR *v); /* njUnitVector (returns the length in FR0) */
extern void njUnitMatrix(NJS_MATRIX *m); /* njUnitMatrix */
extern void njRotateY(NJS_MATRIX *m, Angle ang); /* njRotateY */
extern void njCalcVector(NJS_MATRIX *m, NJS_POINT3 *src, NJS_POINT3 *dst); /* njCalcVector */
extern void func_8C0177E0(UNK_8C01622A_work *wk, taskwk *twp);

void func_8C016536(UNK_8C01622A_work *wk, taskwk *twp);
void func_8C016A70(UNK_8C01622A_work *wk, taskwk *twp);

#ifdef NON_MATCHING
void func_8C01622A(UNK_8C01622A_work *wk, taskwk *twp) {
    NJS_POINT3 v;
    NJS_MATRIX m;
    Angle ang;
    Float t;

    if (!(twp->wtimer & 0x1000) || !(twp->wtimer & 0x40)) {
        switch (twp->mode) {
        case 0:
            wk->target.x = B_8C78C608[0]->pos.x;
            wk->target.y = B_8C78C608[0]->pos.y;
            wk->target.z = B_8C78C608[0]->pos.z;
            break;
        case 1:
            if (!(twp->wtimer & 0x400)) {
                if (wk->exec == func_8C016536) {
                    wk->target.x = twp->pos.x;
                    wk->target.y = twp->pos.y;
                    wk->target.z = twp->pos.z;
                } else {
                    wk->target.x = B_8C78C608[0]->pos.x;
                    wk->target.y = B_8C78C608[0]->pos.y;
                    wk->target.z = B_8C78C608[0]->pos.z;
                }
                wk->rad = 10.0f;
                twp->wtimer |= 0x400;
            }
            if (twp->wtimer & 0x800) {
                /* SHC merged the two FMACs of the x branches (addend -4 or 1) */
                if (wk->spd.x > 0.0f) {
                    t = -4.0f;
                } else {
                    t = 1.0f;
                }
                wk->target.x += SA1_FMAC((Float)rand(), 9.1552734375e-05f, t);
                if (wk->spd.z > 0.0f) {
                    wk->target.z += SA1_FMAC((Float)rand(), 9.1552734375e-05f, -4.0f);
                } else {
                    wk->target.z += SA1_FMAC((Float)rand(), 9.1552734375e-05f, 1.0f);
                }
                twp->wtimer &= ~0x800;
            }
            break;
        case 2:
        case 3:
        case 4:
            switch (twp->mode) {
            case 2:
                ang = 0;
                break;
            case 3:
                ang = 0x4000;
                break;
            case 4:
            default:
                ang = 0xC000;
                break;
            }
            if (twp->wtimer & 0x800) {
                twp->mode = SA1_FTOI(SA1_FMAC((Float)rand(), 9.1552734375e-05f, 0.0f));
                twp->mode += 2;
                twp->wtimer &= ~0x800;
            }
            v.x = twp->pos.x - B_8C78C608[0]->pos.x;
            v.y = twp->pos.y - B_8C78C608[0]->pos.y;
            v.z = twp->pos.z - B_8C78C608[0]->pos.z;
            if (SA1_FMAC(v.z, v.z, SA1_FMAC(v.x, v.x, v.y * v.y)) > 25.0f) {
                njUnitVector(&v);
                njUnitMatrix(&m);
                if (ang != 0) {
                    njRotateY(&m, ang);
                }
                njCalcVector(&m, &v, &v);
                v.x = v.x * 8.0f;
                v.y = v.y * 8.0f;
                v.z = v.z * 8.0f;
            } else {
                v.y = njSin(0) * 8.0f;
                t = njCos(0) * 8.0f;
                v.x = t * njCos(ang);
                v.z = -(t * njSin(ang));
            }
            wk->target.x = v.x + twp->pos.x;
            wk->target.y = v.y + twp->pos.y;
            wk->target.z = v.z + twp->pos.z;
            break;
        }
    }
}
#else
ASM_STUB(void func_8C01622A(UNK_8C01622A_work *wk, taskwk *twp))
#endif

#ifdef NON_MATCHING
void func_8C016536(UNK_8C01622A_work *wk, taskwk *twp) {
    NJS_POINT3 v;
    Angle ang;
    Float f;

    if (twp->wtimer & 0x40) {
        func_8C016A70(wk, twp);
        return;
    }
    if (twp->wtimer & 0x80) {
        wk->rad = 0.0f;
    }
    v.x = wk->target.x - twp->pos.x;
    v.y = wk->target.y - twp->pos.y;
    v.z = wk->target.z - twp->pos.z;
    v.x = SA1_FMAC(wk->rad, njCos(wk->ang), v.x);
    v.z = SA1_FMAC(wk->rad, njSin(wk->ang), v.z);
    wk->ang += SA1_FTOI((Float)((wk->flag & 5) + wk->param->unk2) * 182.04444f);
    if (SA1_FMAC(v.z, v.z, SA1_FMAC(v.x, v.x, v.y * v.y)) > 25.0f) {
        f = (Float)SA1_FTOI(atan2f(v.z, v.x) * 10430.381f);
        ang = SA1_FTOI(SA1_FMAC(njSin(wk->ang), 8192.0f, f));
        wk->spd.x = njCos(ang) * 0.25f;
        wk->spd.z = njSin(ang) * 0.25f;
    }
    wk->acc.y += -0.28094999f;
}
#else
ASM_STUB(void func_8C016536(UNK_8C01622A_work *wk, taskwk *twp))
#endif

Sint32 func_8C01669E(UNK_8C01622A_work *wk, taskwk *twp, NJS_POINT3 *pos) {
    Sint32 flag;
    Sint32 spd;

    if (twp->wtimer & 0x40) {
        func_8C016A70(wk, twp);
        return 1;
    }
    flag = wk->flag;
    spd = SA1_FTOI((Float)((flag & 3) + wk->param->unk2) * 182.04444f);
    if (flag & 4) {
        spd = -spd;
    }
    if (twp->wtimer & 0x20) {
        spd = -spd;
    }
    wk->ang += spd;
    pos->x = wk->target.x;
    pos->y = wk->target.y;
    pos->z = wk->target.z;
    wk->rad = SA1_FMAC(njCos(wk->ang >> 2), 5.0f, 18.0f);
    SA1_FMAC_ACC(pos->x, wk->rad, njCos(wk->ang));
    SA1_FMAC_ACC(pos->z, wk->rad, njSin(wk->ang));
    return 0;
}

void func_8C016754(UNK_8C01622A_work *wk, taskwk *twp) {
    NJS_POINT3 pos;
    Float d;

    if (func_8C01669E(wk, twp, &pos) == 0) {
        d = pos.x - twp->pos.x;
        d *= 0.01f;
        wk->acc.x += d;
        d = pos.z - twp->pos.z;
        d *= 0.01f;
        wk->acc.z += d;
        wk->acc.y += -0.28094999f;
    }
}

void func_8C016800(UNK_8C01622A_work *wk, taskwk *twp) {
    struct { Float lo; Float hi; } lim;

    func_8C016754(wk, twp);
    switch (wk->mode) {
    case 6:
        lim.lo = 13.0f;
        lim.hi = 15.0f;
        break;
    case 10:
    default:
        lim.lo = 13.0f;
        lim.hi = 15.0f;
        break;
    }
    if (twp->wtimer & 8) {
        twp->wtimer &= ~4;
        if (wk->unk4 <= lim.hi && wk->unk4 > lim.lo) {
            twp->wtimer |= 0x200;
        } else {
            if (SA1_FMAC(wk->spd.x, wk->spd.x, wk->spd.z * wk->spd.z) >= 0.80999996f) {
                njUnitVector(&wk->spd);
                wk->spd.x = wk->spd.x * 0.9f;
                wk->spd.y = wk->spd.y * 0.9f;
                wk->spd.z = wk->spd.z * 0.9f;
                func_8C0177E0(wk, twp);
            }
            twp->wtimer |= 4;
        }
    }
}

ASM_STUB(void func_8C016A70(UNK_8C01622A_work *wk, taskwk *twp))
