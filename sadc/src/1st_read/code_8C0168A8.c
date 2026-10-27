#include "common.h"
#include "task.h"

/* 0x6C-byte object work (see code_8C015820.c, stored in twp->value) */
typedef struct {
    Uint8 kind;                 /* 0x00 */
    Uint8 flags;                /* 0x01 */
    Sint8 pad2[2];
    Float unk4;                 /* 0x04 */
    NJS_POINT3 spd;             /* 0x08 */
    NJS_POINT3 pos;             /* 0x14 */
    Sint8 pad20[0x18];
    Angle ang;                  /* 0x38 */
    Float unk3C;                /* 0x3C */
    Sint8 pad40[8];
    NJS_POINT3 unk48;           /* 0x48 */
    Sint8 pad54[0x14];
    Uint16 *unk68;              /* 0x68 */
} UNK_8C0168A8_work;

extern Float atan2f(Float y, Float x);
extern Float njCos(Angle ang);
extern Float njSin(Angle ang);
extern Float njUnitVector(NJS_VECTOR *v);

ASM_STUB(Sint32 func_8C01669E(UNK_8C0168A8_work *wk, taskwk *twp, NJS_POINT3 *out))
void func_8C0177E0(UNK_8C0168A8_work *wk, taskwk *twp);

void func_8C0168A8(UNK_8C0168A8_work *wk, taskwk *twp) {
    struct { Float lo; Float hi; } lim;
    NJS_POINT3 v;
    Angle ang;
    Float r;
    Float h;

    if (func_8C01669E(wk, twp, &v) != 0) {
        return;
    }
    wk->pos.y += -0.28094999f;
    /* only kinds 7 and 8 install this handler (func_8C01593E); for any other kind
     * the original would read FR14/FR15 and the stack slots of lim uninitialised */
    PC_ONLY(h = 0.0f; r = 0.0f; lim.lo = 0.0f; lim.hi = 0.0f;)
    switch (wk->kind) {
    case 7:
        h = 2.25f;
        r = 2.8f;
        lim.lo = 2.0f;
        lim.hi = 4.0f;
        break;
    case 8:
        h = 3.6f;
        r = 3.6f;
        lim.lo = 3.0f;
        lim.hi = 4.0f;
        break;
    }
    if (twp->wtimer & 8) {
        twp->wtimer &= ~4;
        twp->wtimer |= 0x200;
        if (wk->unk4 <= lim.hi && wk->unk4 > lim.lo) {
            twp->wtimer &= ~0x200;
            twp->wtimer |= 4;
            twp->pos.y += 0.01f;
            wk->spd.y = 0.0f;
            v.x -= twp->pos.x;
            v.y -= twp->pos.y;
            v.z -= twp->pos.z;
            ang = SA1_FTOI(atan2f(v.z, v.x) * 10430.381f);
            v.x = r * njCos(ang);
            v.z = r * njSin(ang);
            v.y = h;
            wk->pos.x += v.x;
            wk->pos.y += v.y;
            wk->pos.z += v.z;
            if (SA1_FMAC(wk->spd.x, wk->spd.x, wk->spd.z * wk->spd.z) >= 0.80999996f) {
                njUnitVector(&wk->spd);
                wk->spd.x = wk->spd.x * 0.9f;
                wk->spd.y = wk->spd.y * 0.9f;
                wk->spd.z = wk->spd.z * 0.9f;
                func_8C0177E0(wk, twp);
            }
        }
    }
}

void func_8C016A70(UNK_8C0168A8_work *wk, taskwk *twp) {
    NJS_POINT3 p;
    NJS_POINT3 v;
    Sint32 d;
    Float s;

    if (twp->wtimer & 0x40) {
        d = SA1_FTOI(((wk->flags & 3) + 1) * 182.04444f);
    } else {
        d = SA1_FTOI(((wk->flags & 3) + wk->unk68[1]) * 182.04444f);
    }
    if (wk->flags & 8) {
        d = -d;
    }
    if (twp->wtimer & 0x20) {
        d = -d;
    }
    wk->ang += d;
    /* (&p)/(&v) mark the vectors address-taken: SHC then addresses them off
     * R15 instead of keeping a base pointer in a register (needed to match) */
    (&p)->x = wk->unk48.x;
    (&p)->y = wk->unk48.y;
    (&p)->z = wk->unk48.z;
    wk->unk3C = SA1_FMAC(njCos(wk->ang >> 2), 5.0f, 22.0f);
    SA1_FMAC_ACC(p.x, wk->unk3C, njCos(wk->ang));
    SA1_FMAC_ACC(p.z, wk->unk3C, njSin(wk->ang));
    p.y += SA1_FMAC(njSin(wk->ang << 1), 4.0f, 8.0f);
    if (!(twp->wtimer & 0x40)) {
        if (twp->wtimer & 0x100) {
            wk->unk48.y += 0.08f;
        }
    }
    if (wk->kind == 4 && !(twp->wtimer & 0x40)) {
        (&v)->x = p.x - twp->pos.x;
        v.y = p.y - twp->pos.y;
        v.z = p.z - twp->pos.z;
        s = 0.049999998f;
    } else {
        v.x = p.x - twp->pos.x;
        v.y = p.y - twp->pos.y;
        v.z = p.z - twp->pos.z;
        s = 0.01f;
    }
    v.x *= s;
    v.y *= s;
    v.z *= s;
    wk->pos.x += v.x;
    wk->pos.y += v.y;
    wk->pos.z += v.z;
}

ASM_STUB(void func_8C0177E0(UNK_8C0168A8_work *wk, taskwk *twp))
