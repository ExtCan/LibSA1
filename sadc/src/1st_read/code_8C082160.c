#include "common.h"
#include "ninja.h"
#include "task.h"
#include "sa1/ninja_funcs.h"

/* Player 2 AI (follows player 1): drives the fake controller of player 2. */

typedef struct {
    Uint8 pad0[0x38];
    Float unk38;
    Uint8 pad3C[0xC0 - 0x3C];
    Float unkC0;
    Float unkC4;
} UNK_8C082160_cs;

typedef struct {
    UNK_8C082160_cs *cs;
    NJS_POINT3 spd;
} UNK_8C082160_pwp;

/* PDS_PERIPHERAL-like */
typedef struct {
    Uint32 id;
    Uint32 support;
    Uint32 on;
    Uint32 off;
    Uint32 press;
} UNK_8C754C3C_t;

typedef struct {
    Angle ang;
    Float stroke;
} UNK_8C754DD4_t;

typedef struct {
    Uint8 pad0[8];
    Uint16 cnt;
} UNK_8C082570_wk;

extern taskwk *B_8C78C608[];
extern UNK_8C082160_pwp *B_8C78C628[];
extern UNK_8C754C3C_t B_8C754C3C;
extern UNK_8C754DD4_t B_8C754DD4;
extern Uint32 D_8C19E38C;
extern Uint32 D_8C19E390;
extern NJS_POINT3 B_8C88F5E4;

extern Float _builtin_sqrtf(Float x);
extern void _builtin_fsca(Sint32 ang, Float *s, Float *c);

extern Float func_8C08593C(NJS_POINT3 *v);
extern Float func_8C085980(NJS_VECTOR *v, NJS_VECTOR *out);  /* canonical: sa1/protos.h */
extern Float func_8C0859E4(NJS_POINT3 *a, NJS_POINT3 *b);
extern Angle func_8C085A50(Float z, Float x);
extern void func_8C085F14(NJS_POINT3 *src, NJS_POINT3 *dst);
void func_8C08273C(NJS_POINT3 *v, Float *f, Angle *ang);

#ifdef NON_MATCHING
void func_8C082160(task *tp) {
    taskwk *twp;
    taskwk *p1;
    taskwk *p2;
    UNK_8C754C3C_t *per;
    UNK_8C082160_pwp *pwp1;
    UNK_8C082160_pwp *pwp2;
    UNK_8C082160_cs *cs1;
    UNK_8C082160_cs *cs2;
    Uint32 press;
    Uint32 on;
    Float dist;
    Float len;
    Float x;
    Float z;
    NJS_POINT3 v;
    Angle ang;
    Float stroke;

    twp = tp->twp;
    p1 = B_8C78C608[0];
    p2 = B_8C78C608[1];
    pwp1 = B_8C78C628[0];
    pwp2 = B_8C78C628[1];
    cs1 = pwp1->cs;
    cs2 = pwp2->cs;
    if (p1 == NULL) {
        return;
    }
    v.x = p2->pos.x - p1->pos.x;
    v.z = p2->pos.z - p1->pos.z;
    v.y = 0.0f;
    njUnitVector(&v);
    v.x *= 5.0f;
    v.z *= 5.0f;
    per = &B_8C754C3C;
    press = per->press;
    on = per->on;
    x = (p1->pos.x + v.x) - p2->pos.x;
    z = (p1->pos.z + v.z) - p2->pos.z;
    dist = _builtin_sqrtf(SA1_FMAC(x, x, z * z));
    ang = func_8C085A50(z, x);
    stroke = 0.0f;
    switch (twp->smode) {
    case 0:
        if (cs1 != NULL && cs2 != NULL) {
            twp->smode = 1;
        }
        break;
    case 1:
        if (dist < 15.0f) {
            if (cs1->unk38 < cs1->unkC0 && cs2->unk38 > cs2->unkC4) {
                func_8C08273C(&pwp2->spd, &stroke, &ang);
            } else if (cs2->unk38 == 0.0f) {
                if (dist < 0.99999996f) {
                    stroke = 0.0f;
                } else {
                    stroke = 0.005f;
                }
            } else {
                stroke = 0.0f;
            }
        } else if (dist < 40.0f) {
            stroke = SA1_FMAC(dist - 20.0f, 0.024999999f, 0.5f);
        } else {
            stroke = 1.0f;
        }
        if (p2->flag & 3) {
            cs1 = pwp1->cs;
            if (p1->pos.y - p2->pos.y > 20.0f && cs1->unk38 < cs1->unkC4) {
                press |= D_8C19E390;
                twp->smode = 2;
            }
        }
        break;
    case 2:
        stroke = dist * 0.049999998f;
        if (stroke > 1.0f) {
            stroke = 1.0f;
        } else if (stroke < 0.099999996f) {
            stroke = 0.0f;
        }
        if (p1->pos.y + 3.0f > p2->pos.y) {
            if (pwp2->spd.y < 0.0f) {
                press |= D_8C19E390;
                twp->smode = 3;
            } else {
                on = on | D_8C19E390;
            }
        }
        if (p2->flag & 3) {
            if (!(p2->flag & 0x100)) {
                twp->smode = 1;
            }
        }
        break;
    case 3:
        stroke = dist * 0.049999998f;
        if (stroke > 1.0f) {
            stroke = 1.0f;
        } else if (stroke < 0.099999996f) {
            stroke = 0.0f;
        }
        if (p2->flag & 3) {
            twp->smode = 1;
        } else if (dist < 20.0f && p1->pos.y < p2->pos.y) {
            press = press | D_8C19E38C;
        } else {
            on |= D_8C19E390;
        }
        break;
    case 4:
        stroke = dist * 0.049999998f;
        if (stroke > 1.0f) {
            stroke = 1.0f;
        } else if (stroke < 0.099999996f) {
            stroke = 0.0f;
        }
        if (p2->flag & 3) {
            twp->smode = 1;
        } else if (p2->pos.y - p1->pos.y < 20.0f) {
            twp->smode = 3;
            press = press | D_8C19E390;
            on = on | D_8C19E390;
        }
        break;
    case 5:
        v.x = p1->pos.x - p2->pos.x;
        v.y = p1->pos.y - p2->pos.y;
        v.z = p1->pos.z - p2->pos.z;
        if (func_8C08593C(&v) < 50.0f || (p2->flag & 4)) {
            twp->smode = 1;
            on = on | D_8C19E38C;
        } else {
            func_8C085980(&v, &v);
            p2->pos.x = p2->pos.x + v.x;
            p2->pos.y = p2->pos.y + v.y;
            p2->pos.z = p2->pos.z + v.z;
        }
        break;
    }
    len = func_8C0859E4(&twp->pos, &p2->pos);
    if (len < 0.99999996f) {
        twp->timer.w[1]++;
        if (twp->timer.w[1] > 60 && dist > 30.0f) {
            twp->timer.w[1] = 0;
            press |= D_8C19E390;
            twp->smode = 2;
        }
    } else {
        twp->timer.w[1] = 0;
    }
    twp->pos = p2->pos;
    twp->ang = p2->ang;
    per = &B_8C754C3C;
    per->on = on;
    per->press = press;
    B_8C754DD4.ang = ang;
    B_8C754DD4.stroke = stroke;
}
#else
ASM_STUB(void func_8C082160(task *tp))
#endif

#ifdef NON_MATCHING
Sint32 func_8C082570(task *tp, taskwk *a, taskwk *b) {
    NJS_POINT3 *p;
    Float d;
    NJS_POINT3 v;
    NJS_POINT3 w;

    p = &B_8C88F5E4;
    d = func_8C0859E4(&a->pos, &b->pos);
    if (d > 1000.0f) {
        ((UNK_8C082570_wk *)tp->twp)->cnt = 0;
        return 1;
    } else if (d < 50.0f) {
        ((UNK_8C082570_wk *)tp->twp)->cnt = 0;
    } else {
        njCalcPoint(NULL, &b->pos, &v);
        func_8C085F14(&v, &w);
        if (w.z < -50.0f || w.x < -(p->y * 0.5f) || w.x > p->y * 0.5f
            || w.y < -(p->z * 0.5f) || w.y > p->z * 0.5f) {
            if (((UNK_8C082570_wk *)tp->twp)->cnt++ > 300) {
                ((UNK_8C082570_wk *)tp->twp)->cnt = 0;
                return 1;
            }
        } else {
            ((UNK_8C082570_wk *)tp->twp)->cnt = 0;
        }
    }
    return 0;
}
#else
ASM_STUB(Sint32 func_8C082570(task *tp, taskwk *a, taskwk *b))
#endif

#ifdef NON_MATCHING
/* Identical to the original by inspection, but contains FSCA, which the
 * assembler used by shcdiff (asmsh 4.0) rejects. R4 = twp, R5 = out,
 * FR4 = r; the C order is that of the matched callers (sa1/protos.h). */
void func_8C0826B0(taskwk *twp, NJS_POINT3 *out, Float r) {
    Float c;
    Float s;

    if (twp != NULL && out != NULL) {
        _builtin_fsca(twp->ang.y, &s, &c);
        out->x = twp->pos.x - r * c;
        out->y = twp->pos.y;
        out->z = twp->pos.z - r * s;
    }
}
#endif

Angle func_8C0826F4(NJS_POINT3 *a, NJS_POINT3 *b, Float *dist, Float *hdist) {
    Float dx;
    Float dy;
    Float dz;

    dx = b->x - a->x;
    dy = b->y - a->y;
    dz = b->z - a->z;
    if (dist != NULL) {
#ifdef TARGET_PC
        /* SHC fuses dy * dy into dx * dx (FMAC); the SA1_FMAC spelling
         * changes the SHC register allocation, so it is PC-only. */
        *dist = _builtin_sqrtf(SA1_FMAC(dy, dy, dx * dx) + dz * dz);
#else
        *dist = _builtin_sqrtf(dx * dx + dy * dy + dz * dz);
#endif
    }
    if (hdist != NULL) {
        *hdist = _builtin_sqrtf(dx * dx + dz * dz);
    }
    return func_8C085A50(dz, dx);
}

ASM_STUB(void func_8C08273C(NJS_POINT3 *v, Float *f, Angle *ang))
