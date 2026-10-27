/*
 * 1ST_READ 8C102EAE-8C10318C (chunk 8C102E80): creation of ZERO (E-100
 * Zero, the robot that chases Amy; its model D_8C5CE6B0 precedes the
 * E_100_Zero data).
 *  func_8C102EAE  create the Zero task (Amy only, player 0 present) from a
 *                 placement request: mode 0 / 2 start walking toward the
 *                 target point (twp->mode 11 / 5), mode 1 faces player 0
 *                 (twp->mode 1); returns the task (NULL when not created)
 *  func_8C1030F0  first exec: collision, dest/disp/exec, Twinkle Park act 2
 *                 overlay hook, motion work reset
 *  func_8C10313C  reset Zero's motion work and state
 * func_8C102E80 (same chunk, not decompiled here) clears the
 * B_8C7A52D8 block.
 */
#include "common.h"
#include "sa1/sa1.h"

/* Placement request passed by func_8C107744 (no pointers) */
typedef struct {
    Sint32 mode;                /* 0x00 0, 1 or 2 */
    NJS_POINT3 pos;             /* 0x04 start position */
    NJS_POINT3 tgt;             /* 0x10 point to walk toward (modes 0 and 2) */
} ZERO_SETINFO;                 /* 0x1C */

SA1_ASSERT_SIZE(ZERO_SETINFO, 0x1C);
SA1_ASSERT_OFFSET(ZERO_SETINFO, tgt, 0x10);

extern Sint32 B_8C7A52E8;               /* 1 in level 3 (Twinkle Park) acts 1 and 2 (0x301/0x302) */
extern Sint32 B_8C7A52C8;
extern Sint32 B_8C7A52CC;
extern Float B_8C7A52D0;
extern Float B_8C7A52D4;
extern mtnjvwk B_8C7A5140;              /* Zero's motion work */
extern Float B_8C7A516C;                /* motion speed multiplier (B_8C7A5140.workp) */
extern PL_ACTION D_8C5C69C8[];          /* Zero's action list (data) */
extern CCL_INFO D_8C5C6B18[];           /* Zero's collision shapes (2, data) */
/* Zero model nodes (data, written here: per instance on PC). Declared as
 * arrays: with plain objects SHC folds the member offset into the pool
 * constant (D_8C5CC344+24); the original loads the base and stores
 * @(24,Rn). */
extern NJS_OBJECT D_8C5CC310[];
extern NJS_OBJECT D_8C5CC344[];

extern void func_8C109EAC(Sint32 pno, Sint32 a, Sint32 b, Sint32 c); /* pad vibration */
extern void func_8C10324A(task *tp);    /* dest */
extern void func_8C104DBC(task *tp);    /* disp */
extern void func_8C1032AC(task *tp);    /* exec */
extern void func_8C104FA4(void);

/* Function of the STG03 (Twinkle Park) overlay (overlays load at
 * 0x0C900000), called with the Zero task. PC: provided by the overlay
 * loader (same function as in code_8C0DE8C0.c). */
#if defined(TARGET_PC)
extern void func_0C907998(task *tp);
#define STG03_FUNC_0C907998 func_0C907998
#else
#define STG03_FUNC_0C907998 ((TaskFuncPtr)0x0C907998)
#endif

ASM_STUB(void func_8C102E80(void))

void func_8C1030F0(task *tp);
void func_8C10313C(task *tp);

/*
 * Matching notes: tp must not be assigned inside the || chain (SHC then
 * keeps it in a stack slot); in case 1 both differences must be
 * statements of their own (a named player pointer goes to R11, a
 * playertwp[0] CSE across a single dx statement is spilled).
 */
task *func_8C102EAE(ZERO_SETINFO *p)
{
    task *tp;
    Float dx;
    Float dz;

    if (GetPlayerNumber() != 5 || playertwp[0] == NULL) {
        return NULL;
    }
    if ((tp = CreateElementalTask(2, 3, func_8C1030F0)) == NULL) {
        return NULL;
    }
    B_8C7A52E8 = 0;
    switch (GetStageNumber()) {
    case 0x301:
    case 0x302:
        B_8C7A52E8 = 1;
        break;
    }
    switch (p->mode) {
    case 0:
        tp->twp->pos = p->pos;
        tp->twp->pos.y += 5.0f;
        dx = p->tgt.x - p->pos.x;
        dz = p->tgt.z - p->pos.z;
        tp->twp->ang.y = SA1_FTOI(atan2f(dz, -dx) * 10430.381f);
        tp->twp->timer.l = SA1_FTOI(njSqrt(SA1_FMAC(dx, dx, dz * dz)) * 0.33333332f);
        tp->twp->scl.y = 0.0f;
        tp->twp->scl.z = 0.0f;
        tp->twp->mode = 11;
        break;
    case 1:
        tp->twp->pos = p->pos;
        if (playertwp[0] == NULL) {
            tp->twp->ang.y = 0;
        } else {
            dx = playertwp[0]->pos.x - p->pos.x;
            dz = playertwp[0]->pos.z - p->pos.z;
            tp->twp->ang.y = SA1_FTOI(atan2f(dz, -dx) * 10430.381f);
        }
        tp->twp->mode = 1;
        break;
    case 2:
        tp->twp->pos = p->pos;
        tp->twp->pos.y += 5.0f;
        dx = p->tgt.x - p->pos.x;
        dz = p->tgt.z - p->pos.z;
        tp->twp->ang.y = SA1_FTOI(atan2f(dz, -dx) * 10430.381f);
        tp->twp->timer.l = SA1_FTOI(njSqrt(SA1_FMAC(dx, dx, dz * dz)) * 0.33333332f);
        tp->twp->scl.x = 1.0f;
        tp->twp->scl.y = 4.0f;
        tp->twp->scl.z = 0.0f;
        tp->twp->mode = 5;
        break;
    }
    func_8C109EAC(0, 6, 15, 0);
    return tp;
}

void func_8C1030F0(task *tp)
{
    CCL_Init(tp, D_8C5C6B18, 3, 1, 2);
    tp->dest = func_8C10324A;
    tp->disp = func_8C104DBC;
    if (GetStageNumber() == 0x302) {     /* Twinkle Park act 2 */
        STG03_FUNC_0C907998(tp);
    }
    tp->exec = func_8C1032AC;
    B_8C7A52D0 = 0.0f;
    func_8C10313C(tp);
    func_8C104FA4();
}

void func_8C10313C(task *tp)
{
    taskwk *twp;

    twp = tp->twp;
    if (playertwp[0] == NULL) {
        return;
    }
    B_8C7A5140.mtnmode = 0;
    B_8C7A5140.plactptr = D_8C5C69C8;
    B_8C7A5140.reqaction = B_8C7A5140.action = 20;
    B_8C7A5140.workp = &B_8C7A516C;
    twp->btimer = 0;
    twp->ang.x = 0;
    twp->ang.z = 0;
    D_8C5CC344[0].ang[1] = 0;
    D_8C5CC310[0].ang[2] = 0x8000;
    B_8C7A52C8 = 0;
    B_8C7A52CC = 0;
    CWP(twp)->info[1].attr |= 0x10;
    B_8C7A52D4 = 0.0f;
    func_8C102E80();
}
