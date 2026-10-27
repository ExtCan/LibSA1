/*
 * func_8C0C9B88 (8C0C9B88): proximity test used by func_8C0C8534.
 * Returns 1 when player 1 is Knuckles (character id 3), B_8C7A26E0 is set,
 * B_8C7A26E4 != 3, the B_8C7A2710 record is in state (flag & 0x70) == 0x20
 * with unk04 == 0 and unk08 == 1, and pos lies within 30 units of the
 * record's position; 0 otherwise.
 *
 * Matching note: the B_8C7A26E4 test has to be its own early-return
 * statement; folding it into the && chain swaps R2/R3 for the unk04 load.
 */
#include "common.h"
#include "ninja.h"
#include "task.h"
#include "sa1/sa1.h"

/* File-local view of B_8C7A2710 (only this function uses it). */
typedef struct {
    Sint32 flag;                /* 0x00 bits 0x70 = state */
    Sint32 unk04;               /* 0x04 */
    Sint32 unk08;               /* 0x08 */
    Sint32 unk0C;               /* 0x0C */
    NJS_POINT3 pos;             /* 0x10 */
} struct_8C7A2710;

/* B_8C78C608 (playertwp[]) is declared in sa1/player.h. */
extern Sint32 B_8C7A26E0;
extern Sint32 B_8C7A26E4;
extern struct_8C7A2710 B_8C7A2710;

Sint32 func_8C0C9B88(NJS_POINT3 *pos)
{
    taskwk *twp;
    struct_8C7A2710 *wk;

    twp = B_8C78C608[0];
    if (twp != NULL && (Uint8)twp->counter.b[1] == 3 && B_8C7A26E0 != 0) {
        if (B_8C7A26E4 == 3) {
            return 0;
        }
        wk = &B_8C7A2710;
        if ((wk->flag & 0x70) == 0x20 && wk->unk04 == 0 && wk->unk08 == 1
            && njDistanceP2P(pos, &wk->pos) < 30.0f) {
            return 1;
        }
    }
    return 0;
}
