#include "sa1/sa1.h"

/*
 * Amy stage objects (1ST_READ 8C0DDFB4-8C0DE252, chunk 8C0DDA00, functions
 * 6-9). AmyMain (8C0D6B40) calls func_8C0DE022 and func_8C0DE22E.
 *  func_8C0DDFB4  exec of a type-2 entry: when player 1 comes closer than
 *                 twp->value.f to twp->pos, calls func_8C107744 with
 *                 {2, twp->scl, player pos} and frees itself (also freed
 *                 when there is no player 1).
 *  func_8C0DE022  walks the table D_8C15C774 (terminated by stage 0xFFF)
 *                 and for each entry of the current stage: type 0 creates a
 *                 func_8C0DDF40 task (pos, value.f = distance), type 1 calls
 *                 func_8C107744({1, pos2}), type 2 creates a func_8C0DDFB4
 *                 task (wtimer = 2, pos, value.f, scl = pos2).
 *  func_8C0DE158  exec created in stage 0x0C00 (level 12, act 0): while
 *                 wtimer is 0 and player 1 is within 2 units in height and 6
 *                 units of D_8C15C7FC, sets bit 1 of pwp->free.b[6] (0x82);
 *                 otherwise counts wtimer down. When func_8C0DDE1C(0)
 *                 returns 6 the player is put back at D_8C15C7FC with zero
 *                 speed and wtimer = 30. Freed outside stage 0x0C00.
 *  func_8C0DE22E  creates the func_8C0DE158 task in stage 0x0C00.
 */

/* Table entry of D_8C15C774 (0x20 bytes, ADD #32 at 8C0DE13C) */
typedef struct {
    Uint16 stage;               /* 0x00 GetStageNumber() value; 0xFFF ends the table */
    Sint16 type;                /* 0x02 0, 1, 2 (copied to twp->wtimer by type 2) */
    Float dist;                 /* 0x04 trigger distance (twp->value.f) */
    NJS_POINT3 pos;             /* 0x08 */
    NJS_POINT3 pos2;            /* 0x14 */
} AMY_OBJENTRY;                 /* 0x20 */

SA1_ASSERT_SIZE(AMY_OBJENTRY, 0x20);

/* Argument of func_8C107744 (0x1C bytes on the stack of both callers) */
typedef struct {
    Sint32 type;                /* 0x00 1 or 2 */
    NJS_POINT3 pos;             /* 0x04 */
    NJS_POINT3 pos2;            /* 0x10 type 2: player position */
} UNK_8C107744_arg;             /* 0x1C */

SA1_ASSERT_SIZE(UNK_8C107744_arg, 0x1C);

extern AMY_OBJENTRY D_8C15C774[];
extern NJS_POINT3 D_8C15C7FC;

/* Returns 1 when it stored a new task in D_8C5FB8A4 (via func_8C102EAE) */
extern Sint32 func_8C107744(UNK_8C107744_arg *arg);

/* earlier in the chunk (code_8C0DDA00.c). func_8C0DDF40 is only taken as an
 * address, but SHC keeps same-file function addresses out of registers in
 * the loop of func_8C0DE022, so it is defined here as a stub too. */
ASM_STUB(Sint32 func_8C0DDE1C(Sint32 pno))
ASM_STUB(void func_8C0DDF40(task *tp))

void func_8C0DDFB4(task *tp)
{
    UNK_8C107744_arg arg;
    taskwk *twp;
    taskwk *ptwp;

    ptwp = playertwp[0];
    if (ptwp == NULL) {
        FreeTask(tp);   /* SHC merges the two FreeTask(tp) tails */
        return;
    }
    twp = tp->twp;
    if (!(njDistanceP2P(&ptwp->pos, &twp->pos) < twp->value.f)) {
        return;
    }
    /* a one-case switch: an if schedules the copy differently */
    switch (twp->wtimer) {
    case 2:
        arg.type = 2;
        arg.pos = twp->scl;
        arg.pos2 = ptwp->pos;
        break;
    }
    func_8C107744(&arg);
    FreeTask(tp);
}

void func_8C0DE022(void)
{
    UNK_8C107744_arg arg;
    task *tp;
    AMY_OBJENTRY *e;
    Uint16 stage;

    stage = GetStageNumber();
    for (e = D_8C15C774; e->stage != 0xFFF; e++) {
        if (e->stage != stage) {
            continue;
        }
        switch (e->type) {
        case 0:
            if ((tp = CreateElementalTask(2, 3, func_8C0DDF40)) != NULL) {
                tp->twp->pos = e->pos;
                tp->twp->value.f = e->dist;
            }
            break;
        case 1:
            arg.type = 1;
            arg.pos = e->pos2;
            func_8C107744(&arg);
            break;
        case 2:
            if ((tp = CreateElementalTask(2, 3, func_8C0DDFB4)) != NULL) {
                tp->twp->wtimer = e->type;
                tp->twp->pos = e->pos;
                tp->twp->value.f = e->dist;
                tp->twp->scl = e->pos2;
            }
            break;
        }
    }
}

void func_8C0DE158(task *tp)
{
    NJS_POINT3 pos;
    taskwk *twp;
    taskwk *ptwp;
    playerwk *pwp;

    twp = tp->twp;
    ptwp = playertwp[0];
    pos = D_8C15C7FC;
    pwp = GetPlayerWorkPtr(0);
    if (ptwp == NULL || (pwp = GetPlayerWorkPtr(0)) == NULL
        || GetStageNumber() != 0x0C00) {
        FreeTask(tp);
        return;
    }
    if (twp->wtimer == 0) {
        if (_builtin_fabsf(ptwp->pos.y - pos.y) < 2.0f) {
            if (njDistanceP2P(&ptwp->pos, &pos) < 6.0f) {
                pwp->free.b[6] |= 2;
            }
        }
    } else {
        twp->wtimer--;
    }
    if (func_8C0DDE1C(0) == 6) {
        twp->wtimer = 30;
        ptwp->pos = pos;
        pwp->spd.x = 0.0f;
        pwp->spd.y = 0.0f;
        pwp->spd.z = 0.0f;
    }
}

void func_8C0DE22E(void)
{
    if (GetStageNumber() == 0x0C00) {
        CreateElementalTask(2, 3, func_8C0DE158);
    }
}
