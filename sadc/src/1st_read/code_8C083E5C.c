#include "common.h"
#include "ninja.h"
#include "task.h"

/* Work referenced through task->awp (also passed directly to func_8C084118) */
typedef struct {
    Uint8 pad0[0x44];
    task *tp44;                 /* 0x44 */
    taskwk *twp48;              /* 0x48 */
    Uint8 pad4C[0x58 - 0x4C];
    taskwk *twp58;              /* 0x58 */
    Uint8 pad5C[0x90 - 0x5C];
    NJS_POINT3 pos;             /* 0x90 */
} UNK_8C083EF8_t;

/* Creation request (kind at +0, created task stored at +8) */
typedef struct {
    Sint32 kind;                /* 0x00 */
    Sint32 unk4;                /* 0x04 */
    task *tp;                   /* 0x08 */
} UNK_8C083F7C_t;

extern taskwk *B_8C78C608[];    /* playertwp[] */
extern NJS_POINT3 D_8C1BEFB0[3];

extern void func_8C098E98(task *tp);
extern Uint16 GetStageNumber(void);
extern task *CreateElementalTask(Uint16 im, Sint32 level, TaskFuncPtr exec);
extern void SonicMain(task *tp);
extern void KnucklesMain(task *tp);
extern void E102Main(task *tp);
extern void FreeTask(task *tp);
extern task *func_8C07CFA0(Uint8 pno);
extern Float func_8C0859A0(NJS_VECTOR *v, NJS_VECTOR *out);
extern void func_8C07C580(Uint8 pno, Float x, Float y, Float z);
extern Sint32 func_8C07CEF2(Uint8 pno, Sint32 frame, NJS_POINT3 *pos, Angle3 *ang);
extern void func_8C07C676(Uint8 pno, Float x, Float y, Float z);
extern void func_8C07CB54(Uint8 pno, Float x, Float y, Float z);

void func_8C083E5C(task *tp)
{
    taskwk *twp;
    taskwk *p1;
    taskwk *p2;

    twp = tp->twp;
    switch (twp->mode) {
    case 0:
        twp->btimer++;
        if ((Uint8)twp->btimer > 5) {
            twp->btimer = 0;
            twp->mode = 1;
        }
        break;
    case 1:
        p1 = B_8C78C608[0];
        p2 = B_8C78C608[1];
        if (p1 != NULL && p2 != NULL) {
            func_8C098E98(tp);
        }
        break;
    }
}

void func_8C083EF8(task *tp)
{
    UNK_8C083EF8_t *awp;
    Sint32 stg;
    NJS_POINT3 *tbl;

    awp = tp->awp;
    stg = GetStageNumber();
    tbl = D_8C1BEFB0;
    switch (stg) {
    case 0:
        awp->tp44->twp->pos = tbl[0];
        break;
    case 0x2100:
        awp->tp44->twp->pos = tbl[1];
        break;
    case 0x1D02:
        awp->tp44->twp->pos = tbl[2];
        break;
    }
    awp->pos = awp->tp44->twp->pos;
}

task *func_8C083F7C(UNK_8C083F7C_t *p)
{
    task *tp;

    tp = NULL;
    if (p == NULL) {
        return NULL;
    }
    switch (p->kind) {
    case 0:
    case 1:
        tp = CreateElementalTask(7, 1, SonicMain);
        tp->twp->counter.b[1] = 0;
        tp->twp->counter.b[0] = 1;
        break;
    case 2:
    case 3:
        tp = CreateElementalTask(7, 1, KnucklesMain);
        tp->twp->counter.b[1] = 3;
        tp->twp->counter.b[0] = 1;
        break;
    case 4:
    case 5:
        tp = CreateElementalTask(7, 1, E102Main);
        tp->twp->counter.b[1] = 6;
        tp->twp->counter.b[0] = 1;
        break;
    }
    p->tp = tp;
    return tp;
}

void func_8C08403E(UNK_8C083F7C_t *p)
{
    task *tp;

    if (p != NULL) {
        if (p->tp != NULL) {
            p->tp->exec = FreeTask;
            p->tp->disp = NULL;
            p->tp = NULL;
        }
    } else {
        tp = func_8C07CFA0(1);
        if (tp != NULL) {
            tp->exec = FreeTask;
            tp->disp = NULL;
        }
    }
}

void func_8C084074(Uint8 pno1, Uint8 pno2, Float f)
{
    taskwk *twp1;
    taskwk *twp2;
    NJS_POINT3 v;

    twp1 = B_8C78C608[pno1];
    twp2 = B_8C78C608[pno2];
    if (twp1 != NULL && twp2 != NULL) {
        v.x = twp1->pos.x - twp2->pos.x;
        v.y = twp1->pos.y - twp2->pos.y;
        v.z = twp1->pos.z - twp2->pos.z;
        func_8C0859A0(&v, &v);
        v.x *= f;
        v.y = 1.9999999f;
        v.z *= f;
        func_8C07C580(pno1, v.x, v.y, v.z);
        func_8C07C580(pno2, -v.x, v.y, -v.z);
    }
}

Sint32 func_8C084118(UNK_8C083EF8_t *awp)
{
    if (awp->pos.y - awp->twp48->pos.y > 5.0f) {
        awp->twp48->pos.y = awp->pos.y;
        func_8C07CEF2(1, 0, &awp->pos, NULL);
        func_8C07C676(1, 0.0f, 0.0f, 0.0f);
        func_8C07CB54(1, 0.0f, 0.0f, 0.0f);
    }
    if (awp->pos.y - awp->twp58->pos.y > 5.0f) {
        awp->twp58->pos.y = awp->pos.y;
        func_8C07CEF2(0, 0, &awp->pos, NULL);
        func_8C07C676(0, 0.0f, 0.0f, 0.0f);
        func_8C07CB54(0, 0.0f, 0.0f, 0.0f);
    }
    return 0;
}
