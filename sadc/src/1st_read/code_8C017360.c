#include "common.h"
#include "ninja.h"
#include "task.h"

/* per-kind animal parameters (pointed to by work +0x68) */
typedef struct {
    Sint32 unk0;                /* 0x00 */
    Angle ang;                  /* 0x04 model y rotation offset */
    Float scl;                  /* 0x08 model scale */
    Float speed;                /* 0x0C frame step */
    NJS_TEXLIST *texlist;       /* 0x10 */
} UNK_animal_info;

/* 0x6C-byte animal work stored in twp->value */
typedef struct {
    Uint8 kind;                 /* 0x00 */
    Uint8 unk1;                 /* 0x01 */
    Uint8 unk2;                 /* 0x02 */
    Sint8 unk3;                 /* 0x03 */
    Float frame;                /* 0x04 */
    NJS_POINT3 unk8;            /* 0x08 */
    NJS_POINT3 unk14;           /* 0x14 */
    NJS_POINT3 scl;             /* 0x20 bubble scale */
    Sint8 unk2C[0x0C];          /* 0x2C */
    Angle unk38;                /* 0x38 bubble rotation */
    Float unk3C;                /* 0x3C */
    Sint8 unk40[8];             /* 0x40 */
    NJS_POINT3 unk48;           /* 0x48 */
    NJS_POINT3 unk54;           /* 0x54 */
    void (*unk60)();            /* 0x60 */
    void (*unk64)(task *tp);    /* 0x64 */
    UNK_animal_info *info;      /* 0x68 */
} UNK_animal_work;

/* tp->mwp as used by the animal code: speed at +4 */
typedef struct {
    Sint32 unk0;                /* 0x00 */
    NJS_POINT3 spd;             /* 0x04 */
} UNK_animal_mwk;

extern taskwk *B_8C78C608[];    /* playertwp[] */
extern NJS_TEXLIST D_8C162420;
extern NJS_MODEL Animal_Bubble;

extern Float func_8C09222C(taskwk *twp, Float r);
extern void func_8C02F63C(taskwk *twp);
extern Angle AdjustAngle(Angle ang, Angle target, Angle spd);
extern Sint32 func_8C038D00(Float x, Float y, Float z, Float r);
extern Sint32 njSetTexture(NJS_TEXLIST *texlist);
extern Sint32 njPushMatrix(Float *m); /* NJS_MATRIX_PTR (sa1/ninja_funcs.h) */
extern void njTranslate(void *m, Float x, Float y, Float z);
extern void njRotateY(void *m, Angle ang);
extern void njScale(void *m, Float x, Float y, Float z);
extern void njRotateXYZ(void *m, Angle x, Angle y, Angle z);
extern void func_8C040942(NJS_ACTION *act, Float frame);
extern void func_8C036F12(NJS_MODEL *model);
extern Sint32 njPopMatrix(Uint32 n);

#ifdef NON_MATCHING
void func_8C017420(UNK_animal_work *wk, taskwk *twp, task *tp);
void func_8C0174EC(UNK_animal_work *wk, taskwk *twp, task *tp);
void func_8C0175A8(UNK_animal_work *wk, taskwk *twp, task *tp);

/* Canonical prototype (sa1/protos.h): the one caller 8C015AA8 sets R4-R6 and
 * pushes one more word, and SHC passes the last named parameter of a variadic
 * prototype on the stack. tp2 and the variadic part are never read. */
void func_8C017360(void *work, taskwk *twp, task *tp, task *tp2, ...)
{
    UNK_animal_work *wk = (UNK_animal_work *)work;

    twp->timer.w[0]++;
    if (twp->timer.w[0] > 0x3FFF) {
        twp->timer.w[0] = 0x3FFF;
    }
    if (twp->counter.ptr != NULL) {
        wk->frame += wk->info->speed;
        if (wk->frame > (Float)((NJS_ACTION *)twp->counter.ptr)->motion->nbFrame) {
            wk->frame -= (Float)((NJS_ACTION *)twp->counter.ptr)->motion->nbFrame;
        }
    }
    switch (twp->mode) {
    case 0:
        func_8C017420(wk, twp, tp);
        break;
    case 1:
        func_8C0174EC(wk, twp, tp);
        break;
    case 2:
        func_8C0175A8(wk, twp, tp);
        break;
    }
}

void func_8C017420(UNK_animal_work *wk, taskwk *twp, task *tp)
{
    UNK_animal_mwk *mwp;
    Float y;

    mwp = (UNK_animal_mwk *)tp->mwp;
    mwp->spd.y -= 0.05f;
    twp->pos.x += mwp->spd.x;
    twp->pos.y += mwp->spd.y;
    twp->pos.z += mwp->spd.z;
    twp->ang.x = 0;
    twp->ang.z = 0;
    y = func_8C09222C(twp, 0.35f);
    twp->pos.y -= 1.0f;
    if (y > twp->pos.y + -2.0f) {
        twp->pos.y = y + 2.0f;
        if (mwp->spd.y < 0.0f) {
            mwp->spd.y *= -0.6f;
        }
        mwp->spd.x *= 0.7f;
        mwp->spd.z *= 0.7f;
    }
    if (twp->flag & 0x1000) {
        twp->mode = 2;
        twp->wtimer = 0;
    } else {
        func_8C02F63C(twp);
    }
}

void func_8C0174EC(UNK_animal_work *wk, taskwk *twp, task *tp)
{
    UNK_animal_mwk *mwp;
    Float y;

    mwp = (UNK_animal_mwk *)tp->mwp;
    mwp->spd.y -= 0.05f;
    twp->pos.x += mwp->spd.x;
    twp->pos.y += mwp->spd.y;
    twp->pos.z += mwp->spd.z;
    twp->ang.x = 0;
    twp->ang.z = 0;
    y = func_8C09222C(twp, 0.35f);
    if (y > twp->pos.y + -2.0f) {
        twp->pos.y = y + 2.0f;
        if (mwp->spd.y < 0.0f) {
            mwp->spd.y *= -0.6f;
        }
        mwp->spd.x *= 0.7f;
        mwp->spd.z *= 0.7f;
    }
    if (twp->wtimer++ > 200) {
        twp->mode = 0;
        twp->wtimer = 0;
    }
    func_8C02F63C(twp);
}

void func_8C0175A8(UNK_animal_work *wk, taskwk *twp, task *tp)
{
    if (twp->wtimer++ < 30) {
        twp->ang.y = AdjustAngle(twp->ang.y, 0x4000 - B_8C78C608[0]->ang.y, 0x800);
    } else {
        twp->ang.y = 0x4000 - B_8C78C608[0]->ang.y;
    }
    if (!(twp->flag & 0x1000)) {
        twp->mode = 1;
        twp->wtimer = 0;
    }
}

void func_8C017698(UNK_animal_work *wk, taskwk *twp, Angle ang);
void func_8C01771E(UNK_animal_work *wk, taskwk *twp, Angle ang);

void func_8C017648(UNK_animal_work *wk, taskwk *twp)
{
    Angle ang;

    ang = twp->ang.y + wk->info->ang;
    if (func_8C038D00(twp->pos.x, twp->pos.y, twp->pos.z, 20.0f)) {
        func_8C017698(wk, twp, ang);
        func_8C01771E(wk, twp, ang);
    }
}

void func_8C017698(UNK_animal_work *wk, taskwk *twp, Angle ang)
{
    Float s;

    if (twp->counter.ptr == NULL) {
        return;
    }
    njSetTexture(wk->info->texlist);
    njPushMatrix(NULL);
    s = wk->info->scl;
    njTranslate(NULL, twp->pos.x, twp->pos.y + -2.0f, twp->pos.z);
    if (ang) {
        njRotateY(NULL, ang);
    }
    njScale(NULL, s, s, s);
    func_8C040942(twp->counter.ptr, wk->frame);
    njPopMatrix(1);
}

void func_8C01771E(UNK_animal_work *wk, taskwk *twp, Angle ang)
{
    if (twp->wtimer & 0x40) {
        njSetTexture(&D_8C162420);
        njPushMatrix(NULL);
        njTranslate(NULL, twp->pos.x, twp->pos.y + 1.2f, twp->pos.z);
        njScale(NULL, wk->scl.x, wk->scl.y, wk->scl.z);
        njRotateXYZ(NULL, wk->unk38, wk->unk38 + ang, ang);
        func_8C036F12(&Animal_Bubble);
        njPopMatrix(1);
    }
}
#endif

void func_8C0177E0(UNK_animal_work *wk, taskwk *twp)
{
    NJS_POINT3 pos;
    NJS_POINT3 v;

    pos.x = twp->pos.x;
    pos.y = twp->pos.y + -0.2f;
    pos.z = twp->pos.z;
    v.x = -(wk->unk8.z * 0.8f);
    v.z = -(wk->unk8.x * 0.8f);
    v.y = 0.0001f;
}
