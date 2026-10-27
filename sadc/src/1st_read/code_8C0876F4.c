#include "common.h"
#include "ninja.h"
#include "task.h"

typedef struct {
    Float unk0;
    Float unk4;
    Float unk8;
    Float unkC;
    Float unk10;
    Sint32 unk14;
    void *unk18;
    void *unk1C;
} UNK_8C08774C_t;

typedef struct {
    Sint16 unk0;
    Sint16 unk2;
    Sint16 unk4;
    Sint16 unk6;
    Sint32 pad8[12];
    NJS_POINT3 spd;
} UNK_8C07CFDE_t;

extern Sint32 D_8C1BF014[];
extern Sint32 D_8C1BF01C[];
extern NJS_MODEL Character_Shadow_Blob;
extern Sint32 D_8C1A21DC[];
extern NJS_MATRIX D_8C67C640;

extern void njCalcVector(void *m, NJS_POINT3 *src, NJS_POINT3 *dst);
extern void njCalcPoint(void *m, NJS_POINT3 *src, NJS_POINT3 *dst);
extern Sint32 func_8C099500(void);
extern Sint32 njPushMatrix(Float *m);
extern void njTranslate(void *m, Float x, Float y, Float z);
extern void njRotateZ(void *m, Angle a);
extern void njRotateX(void *m, Angle a);
extern void njRotateY(void *m, Angle a);
extern void njScale(void *m, Float x, Float y, Float z);
extern void func_8C036F12(void *model);
extern void func_8C03700C(void *model);
extern Sint32 njPopMatrix(Uint32 n);
extern void func_8C056926(void);
extern void func_8C05695C(Uint32 a, Uint32 b);
extern Sint32 func_8C04FE40(void);
extern void func_8C056938(void);
extern task *func_8C07CFA0(Uint8 pno);
extern UNK_8C07CFDE_t *func_8C07CFDE(Uint8 pno);
extern Float njDistanceP2P(NJS_POINT3 *a, NJS_POINT3 *b);
extern Float njScalor(NJS_POINT3 *v);
extern void func_8C057586(NJS_POINT3 *from, NJS_POINT3 *to, Float dist, NJS_POINT3 *out);
extern void func_8C02F63C(taskwk *twp);
extern Sint32 rand(void);
extern float _builtin_fabsf(float x);
#define fabsf _builtin_fabsf
extern task *CreateElementalTask(Uint16 a, Sint32 b, TaskFuncPtr exec);
extern void func_8C0870EA(task *tp);

void func_8C0877A0(task *tp, Float y, Float x, Float z);

/* the rotation is skipped for a zero angle; the angle goes through an
 * unsigned local, which SHC keeps in a stack slot */
#define ROTATEX(m, a) if ((ang = (a)) != 0) njRotateX((m), ang)
#define ROTATEY(m, a) if ((ang = (a)) != 0) njRotateY((m), ang)
#define ROTATEZ(m, a) if ((ang = (a)) != 0) njRotateZ((m), ang)

void func_8C0876F4(task *tp) {
    Float *p;
    NJS_POINT3 v;
    NJS_POINT3 w;

    p = (Float *)tp->awp;
    v.x = 0.0f;
    v.y = 3.4399999f;
    v.z = 0.0f;
    njCalcVector(0, &v, &v);
    func_8C0877A0(tp, *p, v.x, v.z);
}

void func_8C087738(task *tp) {
    taskwk *twp;

    twp = tp->twp;
    func_8C0877A0(tp, twp->value.f, twp->scl.x, twp->scl.z);
}

void func_8C08774C(task *tp, UNK_8C08774C_t *p) {
    p->unk18 = D_8C1BF014;
    p->unk1C = D_8C1BF01C;
    p->unkC = 1.0f;
    p->unk10 = 1.0f;
    p->unk0 = p->unk4 = p->unk8 = 0.0f;
}

void func_8C08776C(task *tp) {
}

#ifdef NON_MATCHING
void func_8C0877A0(task *tp, Float y, Float x, Float z) {
    taskwk *twp;
    Float s;
    Uint32 ang;

    twp = tp->twp;
    s = 1.0f / SA1_FMAC(fabsf(twp->pos.y - y), 0.03f, 1.0f);
    func_8C099500();
    njPushMatrix(0);
    njTranslate(0, twp->pos.x, y + 0.39999998f, twp->pos.z);
    ROTATEZ(0, twp->ang.z);
    ROTATEX(0, twp->ang.x);
    njScale(0, s, s, s);
    func_8C036F12(&Character_Shadow_Blob);
    njPopMatrix(1);
    func_8C056926();
    func_8C05695C(0, 0x80000);
    if (func_8C04FE40() == 0) {
        twp->counter.f += 5.0f;
    }
    njPushMatrix(0);
    njTranslate(0, twp->pos.x, twp->pos.y + 3.4399999f, twp->pos.z);
    ROTATEY(0, SA1_FTOI(twp->counter.f * 182.04444f));
    func_8C03700C(D_8C1A21DC);
    njPopMatrix(1);
    func_8C056938();
}
#else
ASM_STUB(void func_8C0877A0(task *tp, Float y, Float x, Float z))
#endif

#ifdef NON_MATCHING
Sint32 func_8C08789A(taskwk *twp) {
    task *ptp;
    taskwk *ptwp;
    UNK_8C07CFDE_t *pwp;
    Float d;
    NJS_POINT3 v;
    NJS_POINT3 spd;

    ptp = func_8C07CFA0(0);
    if (ptp == NULL) {
        return 0;
    }
    ptwp = ptp->twp;
    pwp = func_8C07CFDE(0);
    if (pwp == NULL || !(pwp->unk6 & 2)) {
        return 0;
    }
    d = njDistanceP2P(&twp->pos, &ptwp->pos);
    if (d < 50.0f || twp->wtimer > 0) {
        v.x = 0.0f;
        v.y = 7.0f;
        v.z = 0.0f;
        njPushMatrix(D_8C67C640);
        if (ptwp->ang.z) {
            njRotateZ(0, ptwp->ang.z);
        }
        if (ptwp->ang.x) {
            njRotateX(0, ptwp->ang.x);
        }
        if (ptwp->ang.y) {
            njRotateY(0, ptwp->ang.y);
        }
        njCalcPoint(0, &v, &v);
        njPopMatrix(1);
        v.x += ptwp->pos.x;
        v.y += ptwp->pos.y;
        v.z += ptwp->pos.z;
        if (d > 50.0f) {
            d = 50.0f;
        }
        d *= 0.025999997f;
        if (d > 5.0f) {
            d = 5.0f;
        } else if (d < 0.84999998f) {
            d = 0.84999998f;
        }
        pwp = func_8C07CFDE(0);
        if (pwp) {
            spd.x = pwp->spd.x;
            spd.y = pwp->spd.y;
            spd.z = pwp->spd.z;
            d *= SA1_FMAC(njScalor(&spd), 0.5f, 1.0f);
        }
        func_8C057586(&twp->pos, &v, d, &twp->pos);
        twp->wtimer++;
    }
    twp->counter.f += 3.0f;
    func_8C02F63C(twp);
    return 1;
}
#else
ASM_STUB(Sint32 func_8C08789A(taskwk *twp))
#endif

#ifdef NON_MATCHING
void func_8C087A9E(Sint32 n, Float x, Float y, Float z) {
    Float phase;
    Sint32 i;
    task *tp;
    taskwk *twp;

    phase = (Float)rand() * 0.010986328125f;
    for (i = 0; i < n; i++) {
        tp = CreateElementalTask(10, 2, func_8C0870EA);
        if (tp) {
            twp = tp->twp;
            twp->pos.x = x;
            twp->pos.y = y;
            twp->pos.z = z;
            twp->ang.y = SA1_FTOI(((Float)SA1_DIV(i * 360, n) + phase) * 182.04444f);
        }
    }
}
#else
ASM_STUB(void func_8C087A9E(Sint32 n, Float x, Float y, Float z))
#endif


