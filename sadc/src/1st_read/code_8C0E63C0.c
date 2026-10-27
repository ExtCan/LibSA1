/*
 * 1ST_READ 8C0E63C0-8C0E6500 (chunk 8C0E63C0): Big, fishing.
 *  func_8C0E63C0  sound/voice when the line state changes: without the
 *                 B_8C7A40E8 bit 0x400 plays SE 0x517; otherwise, outside
 *                 levels 3 and 8, calls func_8C04EF22 with 0x50F in level
 *                 0x12 and 0x50C elsewhere.
 *  func_8C0E6446  Big action helper (called by BigModeMachine 8C0E0E70 with
 *                 twp, mwp, pwp): if the lure task B_8C7A40CC exists, plays
 *                 the sound above, func_8C107C80(44) while the lure's
 *                 twp->value.w[1] is set, spawns func_8C0EBD22 with the lure's
 *                 mwp->weight (as integer) and value.w[1], and picks the
 *                 requested action 60/61/62 from the lure's scl.y (<= 0,
 *                 <= 1, > 1); without a lure requests 62. Keeps only bits
 *                 0x401 of B_8C7A40E8.
 */
#include "common.h"
#include "sa1/sa1.h"

extern Uint16 B_8C7A40E8;           /* Big fishing state bits (code_8C0DFFA0.c declares Sint16) */
extern task *B_8C7A40CC;            /* Big's lure task */

void func_8C04EF22(Sint32 n);
void func_8C0EBD22(Sint32 a, Sint32 b);  /* creates a task; twp->value.l = a, twp->counter.l = b */

void func_8C0E63C0(void)
{
    if (B_8C7A40E8 & 0x400) {
        if ((GetStageNumber() & 0xFF00) != 0x300 && (GetStageNumber() & 0xFF00) != 0x800) {
            if ((GetStageNumber() & 0xFF00) == 0x1200) {
                func_8C04EF22(0x50F);
            } else {
                func_8C04EF22(0x50C);
            }
        }
    } else {
        func_8C0940DA(0x517, 0, 0, 0);
    }
}

void func_8C0E6446(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    if (B_8C7A40CC != NULL) {
        func_8C0E63C0();
        if (B_8C7A40CC->twp->value.w[1]) {
            func_8C107C80(44);
        }
        func_8C0EBD22(SA1_FTOI(B_8C7A40CC->mwp->weight), B_8C7A40CC->twp->value.w[1]);
        if (B_8C7A40CC->twp->scl.y <= 0.0f) {
            pwp->mj.reqaction = 60;
        } else if (B_8C7A40CC->twp->scl.y <= 1.0f) {
            pwp->mj.reqaction = 61;
        } else {
            pwp->mj.reqaction = 62;
        }
    } else {
        pwp->mj.reqaction = 62;
    }
    B_8C7A40E8 &= 0x401;
}
