#include "common.h"
#include "task.h"

/* Object-specific work passed next to the taskwk (offsets verified by matched code). */
typedef struct {
    Sint32 pad00[2];
    Float f8;                   /* 0x008 */
    Sint8 pad0C[0x42];
    Uint16 flag4E;              /* 0x04E */
    Sint8 pad50[0x150];
    Float f1A0;                 /* 0x1A0 spring velocity of scl.y */
} UNK_pwk;

/* Collision shape entry (CCL_INFO-like), 0x34 bytes in SA1 DC. */
typedef struct {
    Uint32 attr;                /* 0x00 */
    Sint8 pad04[0x30];
} UNK_ccl;                      /* 0x34 */

/* Collision work (colliwk-like) as laid out in SA1 DC (differs from SADX). */
typedef struct UNK_cwk {
    Uint16 id;                  /* 0x00 */
    Sint16 pad02;
    Sint32 pad04;
    Sint32 pad08;
    UNK_ccl *info;              /* 0x0C */
    task *mytask;               /* 0x10 */
    Sint16 pad14;
    Sint16 hit_num;             /* 0x16 index into the hit object's info[] */
    struct UNK_cwk *hit_cwp;    /* 0x18 */
} UNK_cwk;

extern task *CreateElementalTask(Uint16 im, Sint32 level, TaskFuncPtr exec); /* CreateElementalTask */
extern Sint32 rand(void);                                     /* rand() */
extern void func_8C048528(task *tp);
extern void func_8C048624(task *tp);
extern taskwk *B_8C78C608[];                                           /* playertwp[] */
extern void func_8C07C676(Uint8 pno, Float x, Float y, Float z);    /* SetVelocityP */
extern void func_8C109EAC(Sint32 pno, Sint32 a, Sint32 b, Sint32 c);
extern void func_8C08A2A2(Sint32 a);
extern Sint32 func_8C05E5C2(NJS_POINT3 *pos);

#ifdef NON_MATCHING
/*
 * Instruction-identical except:
 *  - twp and the hoisted constant 10 (for i % 10) get R8/R9 swapped
 *    (orig: twp=R9, 10=R8). Changing the divisor to 6/7/9/100 gives the
 *    original allocation, so it is an allocator tie-break.
 *  - the % helper is emitted as __modls, which shcdiff cannot map to
 *    func_8C10CBB8 (same routine), so the pool entry is reported as a diff.
 */
void func_8C0486A0(taskwk *twp, Sint32 *list, Sint32 val)
{
    task *tp;
    taskwk *cwp;
    Sint32 i;

    for (i = 0; list[i] != 0; i++) {
        tp = CreateElementalTask(2, 2, func_8C048528);
        if (tp) {
            cwp = tp->twp;
            cwp->pos = twp->pos;
            cwp->scl.x = SA1_FMAC((Float)rand(), 3.0517579e-05f, -0.5f) * 2.0f;
            cwp->scl.y = SA1_FMAC((Float)rand(), 6.1035157e-05f, 0.9f);
            cwp->scl.z = SA1_FMAC((Float)rand(), 3.0517579e-05f, -0.5f) * 2.0f;
            cwp->ang = twp->ang;
            cwp->btimer = i % 10;
            cwp->wtimer = 0;
            cwp->counter.l = list[i];
            cwp->timer.l = SA1_FTOI((0.5f - (Float)rand() * 3.0517579e-05f) * 3640.8887f);
            cwp->flag = SA1_FTOI((0.5f - (Float)rand() * 3.0517579e-05f) * 3640.8887f);
            cwp->value.l = val;
        }
    }
}
#endif

void func_8C0487B6(taskwk *twp, Uint8 n)
{
    task *tp;
    taskwk *cwp;
    Sint32 i;

    for (i = 0; i < n; i++) {
        tp = CreateElementalTask(2, 3, func_8C048624);
        if (tp) {
            cwp = tp->twp;
            cwp->pos = twp->pos;
            cwp->scl.x = SA1_FMAC((Float)rand(), 3.0517579e-05f, -0.5f) * 3.0f;
            cwp->scl.y = SA1_FMAC((Float)rand(), 9.1552735e-05f, 0.099999996f);
            cwp->scl.z = SA1_FMAC((Float)rand(), 3.0517579e-05f, -0.5f) * 3.0f;
            cwp->ang = twp->ang;
            cwp->wtimer = 0;
            cwp->timer.l = SA1_FTOI(SA1_FMAC((Float)rand(), 3.0517579e-05f, 1.0f) * 9102.222f);
            cwp->flag = SA1_FTOI(SA1_FMAC((Float)rand(), 3.0517579e-05f, 1.0f) * 9102.222f);
        }
    }
}

void func_8C0488A2(taskwk *twp, UNK_pwk *pwp)
{
    Float a;

    a = twp->scl.y - 1.0f;
    pwp->f1A0 += -(pwp->f1A0 * 0.19999999f) - a * 0.32f;
    twp->scl.y += pwp->f1A0;
    if (twp->scl.y > 1.69999996f) {
        twp->scl.y = 1.69999996f;
    } else if (twp->scl.y < 0.29999999f) {
        twp->scl.y = 0.29999999f;
    }
    twp->scl.x = 2.0f - twp->scl.y;
}

void func_8C048980(Sint8 pno)
{
    taskwk *ptwp;
    Sint32 c;
    Sint32 flag;

    ptwp = B_8C78C608[pno];
    if (ptwp != NULL) {
        flag = ptwp->flag;
        if ((flag & 0x100) && !(flag & 3)) {
            c = (Uint8)ptwp->counter.b[1];
            if (c == 0 || c == 2 || c == 3) {
                func_8C07C676(pno, 0.0f, 2.0f, 0.0f);
                func_8C109EAC(pno, 6, 30, 0);
            }
        }
    }
}

Sint32 func_8C0489E4(taskwk *twp, UNK_pwk *pwp)
{
    UNK_cwk *hcwp;
    Sint32 id;
    Sint8 pno;

    hcwp = ((UNK_cwk *)twp->cwp)->hit_cwp;
    if (twp->flag & 4) {
        pwp->f1A0 = 0.35f;
        id = hcwp->id;
        if (id == 0 || id == 1) {
            pwp->flag4E |= 0x1000;
            func_8C08A2A2(10);
            if (hcwp->info[((UNK_cwk *)twp->cwp)->hit_num].attr & 0x200) {
                pwp->f8 = 0.7f;
                pwp->flag4E |= 0x800;
            } else {
                if (B_8C78C608[0] == hcwp->mytask->twp) {
                    pno = 0;
                } else {
                    pno = 1;
                }
                func_8C048980(pno);
                pwp->flag4E &= 0xFBFF;
            }
        }
        return 1;
    }
    if (func_8C05E5C2(&twp->pos)) {
        return 1;
    }
    return 0;
}
