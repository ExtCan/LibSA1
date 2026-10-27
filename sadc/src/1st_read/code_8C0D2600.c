#include "sa1/sa1.h"

/*
 * E-102 Gamma mode setters (1ST_READ chunk 8C0D2600-8C0D26E0). Each takes
 * the player task, reads twp = tp->twp and pwp = tp->mwp->work.pwp and
 * switches twp->mode:
 *
 *  func_8C0D2600  jump: spd.y = 2 * jmp_y_spd, spd.x -= 1.5625, clears
 *                 E-102 work +0x08, twp->flag bit 1 and jumptimer,
 *                 positional SE 17 when B_8C7A2EA8 is set; mode 41.
 *  func_8C0D266A  mode 7 (the unused mwp/pwp locals reproduce the dead
 *                 MOV.L @(36,R4),R4 of the original).
 *  func_8C0D2674  spd = (0.1, 0, 0); mode 45 (called by the event
 *                 driver 8C084BA0, mode 14).
 *  func_8C0D2692  acc = (-2, 0, 0); mode 46.
 */

/*
 * E-102 work (pwp->free.ptr[0]; E102Main 8C0CC0F2: CAlloc(1, 0x244)).
 * Only the head up to the field used here (see code_8C0D23E4.c,
 * code_8C0D04A0.c, code_8C0CFA6E.c for more fields).
 */
typedef struct {
    Sint8 mode;                 /* 0x00 */
    Sint8 unk01;                /* 0x01 */
    Sint16 unk02;               /* 0x02 */
    Sint8 unk04;                /* 0x04 */
    Sint8 unk05;                /* 0x05 */
    Sint16 unk06;               /* 0x06 */
    Sint16 unk08;               /* 0x08 MOV.W, cleared here; compared with 60 by 8C0D23E4 */
} E102WK;                       /* only the head; the real work is 0x244 bytes (only reached through a pointer here) */

SA1_ASSERT_OFFSET(E102WK, unk08, 0x08);

/* positional-SE enable flag (MOV.B + TST) */
extern Sint8 B_8C7A2EA8;

void func_8C0D2600(task *tp)
{
    taskwk *twp;
    motionwk *mwp;
    playerwk *pwp;
    E102WK *ewk;

    twp = tp->twp;
    mwp = tp->mwp;
    pwp = mwp->work.pwp;
    ewk = (E102WK *)pwp->free.ptr[0];
    pwp->spd.y = pwp->p.jmp_y_spd * 2.0f;
    pwp->spd.x -= 1.5625f;
    ewk->unk08 = 0;
    twp->flag &= ~2;
    pwp->jumptimer = 0;
    if (B_8C7A2EA8) {
        func_8C09413A(17, 0, 0, 0, twp->pos.x, twp->pos.y, twp->pos.z);
    }
    twp->mode = 41;
}

void func_8C0D266A(task *tp)
{
    taskwk *twp;
    motionwk *mwp;
    playerwk *pwp;

    twp = tp->twp;
    mwp = tp->mwp;
    pwp = mwp->work.pwp;
    twp->mode = 7;
}

void func_8C0D2674(task *tp)
{
    taskwk *twp;
    motionwk *mwp;
    playerwk *pwp;

    twp = tp->twp;
    mwp = tp->mwp;
    pwp = mwp->work.pwp;
    pwp->spd.x = 0.099999996f;
    pwp->spd.y = pwp->spd.z = 0.0f;
    twp->mode = 45;
}

void func_8C0D2692(task *tp)
{
    taskwk *twp;
    motionwk *mwp;
    playerwk *pwp;

    twp = tp->twp;
    mwp = tp->mwp;
    pwp = mwp->work.pwp;
    pwp->acc.x = -2.0f;
    pwp->acc.y = pwp->acc.z = 0.0f;
    twp->mode = 46;
}
