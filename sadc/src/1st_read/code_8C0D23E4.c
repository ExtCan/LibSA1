#include "sa1/sa1.h"

/*
 * E-102 Gamma (1ST_READ chunk 8C0D2080-8C0D24E0).
 *
 *  func_8C0D23E4  called by E102Main 8C0CBE00 with (twp, pwp) right after
 *                 E102ModeMachine (return value unused there). Gives up
 *                 (returns 0) unless B_8C7A2EAA and B_8C7A2EC1 are set, the
 *                 mode is not 49/51, E-102 work +0x05 <= 0 and +0x08 >= 60
 *                 (looks like an expired counter/timer), no event runs
 *                 (8C049A4E != 1) and twp->id != 3. Otherwise: action 70
 *                 (mode 31) / 71 (mode 33) / 69 (other modes), mode 49,
 *                 death sequence 8C07BBC2(pno), stops twp's sounds
 *                 (8C0945D0), positional SE 0x533 when B_8C7A2EA8 is set,
 *                 E-102 work mode 3 -> 4; returns 1.
 */

/*
 * E-102 work (pwp->free.ptr[0]; E102Main 8C0CC0F2: CAlloc(1, 0x244)).
 * Only the head up to the fields used here (types of the unk fields are
 * placeholders; see code_8C0D04A0.c / code_8C0CFA6E.c for more fields).
 */
typedef struct {
    Sint8 mode;                 /* 0x00 MOV.B, 3 -> 4 here (E102Main compares it with 3) */
    Sint8 unk01;                /* 0x01 */
    Sint16 unk02;               /* 0x02 */
    Sint8 unk04;                /* 0x04 */
    Sint8 unk05;                /* 0x05 MOV.B + CMP/PL: > 0 blocks the trigger */
    Sint16 unk06;               /* 0x06 */
    Sint16 unk08;               /* 0x08 MOV.W, compared with 60 (>= 60 triggers) */
} E102WK;                       /* only the head; the real work is 0x244 bytes (only reached through a pointer here) */

SA1_ASSERT_OFFSET(E102WK, unk05, 0x05);
SA1_ASSERT_OFFSET(E102WK, unk08, 0x08);

/* flags tested with MOV.B + TST (B_8C7A2EAA is cleared by 8C082D60,
 * 8C084AE0, 8C084BA0; B_8C7A2EC1 is also read by E102Main) */
extern Sint8 B_8C7A2EAA;
extern Sint8 B_8C7A2EC1;
/* positional-SE enable flag (MOV.B + TST) */
extern Sint8 B_8C7A2EA8;

/* start the death sequence of player pno (defined in code_8C07BAC0.c) */
void func_8C07BBC2(Uint8 pno);

Sint32 func_8C0D23E4(taskwk *twp, playerwk *pwp)
{
    E102WK *ewk;
    Sint32 mode;

    ewk = (E102WK *)pwp->free.ptr[0];
    if (B_8C7A2EAA == 0) {
        return 0;
    }
    mode = twp->mode;
    if (mode == 49 || mode == 51) {
        return 0;
    }
    if (B_8C7A2EC1 == 0 || ewk->unk05 > 0 || ewk->unk08 < 60) {
        return 0;
    }
    if (func_8C049A4E() == 1 || twp->id == 3) {
        return 0;
    }
    switch (twp->mode) {
    case 31:
        pwp->mj.reqaction = 70;
        break;
    case 33:
        pwp->mj.reqaction = 71;
        break;
    default:
        pwp->mj.reqaction = 69;
        break;
    }
    twp->mode = 49;
    func_8C07BBC2(twp->counter.b[0]);
    func_8C0945D0(twp);
    if (B_8C7A2EA8) {
        func_8C09413A(0x533, 0, 0, 0, twp->pos.x, twp->pos.y, twp->pos.z);
    }
    if (ewk->mode == 3) {
        ewk->mode = 4;
    }
    return 1;
}
