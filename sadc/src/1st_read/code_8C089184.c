#include "common.h"
#include "ninja.h"
#include "task.h"

/* Cubic spline knot (48 bytes) */
typedef struct {
    Float t;            /* 0x00 parameter (accumulated length) */
    Float h;            /* 0x04 t[i] - t[i-1] */
    Float mu;           /* 0x08 */
    Float lambda;       /* 0x0C */
    Float l;            /* 0x10 */
    Float d;            /* 0x14 */
    Float m[3];         /* 0x18 second derivatives */
    Float p[3];         /* 0x24 point */
} SPLINE_KNOT;

/* Per-stage task list entry */
typedef struct {
    Sint32 stage;
    void **list;
} UNK_8C1BDF24_t;

typedef struct {
    Sint32 unk0;
    Sint32 unk4;
    Sint32 unk8;
    TaskFuncPtr exec;
} UNK_TASKDEF_t;

extern UNK_8C1BDF24_t D_8C1BDF24[];

extern Float njDistanceP2P(NJS_POINT3 *a, NJS_POINT3 *b);
extern task *CreateElementalTask(Uint16 im, Sint32 level, TaskFuncPtr exec); /* CreateElementalTask */
extern Uint16 GetStageNumber(void);

void func_8C089598(task *tp);
void func_8C08960A(void);

void func_8C089184(SPLINE_KNOT *k, Sint32 n)
{
    Sint32 i;
    SPLINE_KNOT *p;

    k[0].t = 0.0f;
    for (i = 1, p = &k[1]; i < n; i++, p++) {
        p->t = p[-1].t + njDistanceP2P((NJS_POINT3 *)p[-1].p, (NJS_POINT3 *)p->p);
        p->h = p->t - p[-1].t;
    }
    for (i = 1, p = &k[1]; i < n - 1; i++, p++) {
        p->mu = p[1].h / (p->h + p[1].h);
        p->lambda = 1.0f - p->mu;
    }
    k[1].d = 2.0f;
    for (i = 2, p = &k[2]; i < n - 1; i++, p++) {
        p->l = p->lambda / p[-1].d;
        p->d = 2.0f - p->l * p[-1].mu;
    }
}

ASM_STUB(void func_8C08925A(SPLINE_KNOT *k, Sint32 n))

ASM_STUB(Sint32 func_8C0893E0(SPLINE_KNOT *k, NJS_POINT3 *out, Sint32 n))

void func_8C089528(void)
{
    SPLINE_KNOT k[32];
    NJS_POINT3 out[256];
    volatile Sint32 n;

    func_8C089184(k, n);
    func_8C08925A(k, n);
    func_8C0893E0(k, out, n);
}

Sint32 func_8C089560(void)
{
    task *tp;
    taskwk *twp;

    if ((tp = CreateElementalTask(2, 2, func_8C089598)) != NULL) {
        twp = tp->twp;
        twp->timer.w[0] = GetStageNumber();
        return 1;
    }
    return 0;
}

void func_8C089598(task *tp)
{
    taskwk *twp;
    Uint16 stg;

    twp = tp->twp;
    switch (twp->mode) {
    case 0:
        func_8C08960A();
        twp->mode = 1;
        break;
    case 1:
        stg = GetStageNumber();
        if (twp->timer.w[0] != stg) {
            func_8C08960A();
            twp->timer.w[0] = stg;
        }
        break;
    }
}

void func_8C08960A(void)
{
    UNK_8C1BDF24_t *e;
    UNK_TASKDEF_t **pp;
    UNK_TASKDEF_t *d;
    task *tp;
    taskwk *twp;
    Uint16 stg;

    e = D_8C1BDF24;
    stg = GetStageNumber();
    while (1) {
        if (e->stage == stg) {
            break;
        }
        if (e->stage == 0xFFFF) {
            return;
        }
        e++;
    }
    pp = (UNK_TASKDEF_t **)e->list;
    while (*pp) {
        d = *pp;
        if (d->exec) {
            if ((tp = CreateElementalTask(2, 2, d->exec)) != NULL) {
                twp = tp->twp;
                twp->value.ptr = d;
                twp->timer.w[0] = stg;
            }
        }
        pp++;
    }
}
