/* 1ST_READ 8C0DEDC0-8C0DEEA0: Amy's stage task spawner (called from AmyMain). */
#include "common.h"
#include "sa1/sa1.h"

/* D_8C15C808: 0x7FFF-terminated list of stage numbers ((level << 8) | act):
 * { 0x0301, 0x0302, 0x0A00, 0x0C00, 0x0C01, 0x7FFF }. It is the anonymous
 * initialiser constant of the original's block-scope local array (see the
 * note on func_8C0DEDC0). */
typedef union {
    Uint16 stg[6];
    Uint32 align[3];
} STAGE_LIST6;

extern STAGE_LIST6 D_8C15C808;

void func_8C0DECDA(task *tp);

/* Spawns func_8C0DECDA when the player is Amy (5) in level-select modes
 * 4/5/9, on one of the stages listed in D_8C15C808 or on a stage in
 * 0x1A00-0x22FF while no event is running.
 *
 * NON_MATCHING: the original was
 *     Uint16 list[6] = { 0x301, 0x302, 0xA00, 0xC00, 0xC01, 0x7FFF };
 *     p = list;
 * in the "stg < 0xF00" block. SHC lowers that initialiser copy with its own
 * destination register (MOV R15,R2) apart from p (MOV R15,R5; MOV R5,R4);
 * every extern struct-copy spelling (struct/union assign, cast, volatile,
 * pointer temp, nested struct, p before/after the copy) gets the address
 * CSE'd, one MOV short (86 vs 87 insns). The initialiser form itself (not
 * allowed: it defines the data) reaches 87 insns with only an R2/R3
 * allocation swap left (23 lines; control flow below is the best of ~120
 * variants: early returns, nested else for the 0x1A00-0x22FF range). */
#ifdef NON_MATCHING
task *func_8C0DEDC0(void)
{
    Uint16 *p;
    Uint16 stg;
    Sint32 n;
    task *tp;

    if (GetPlayerNumber() != 5) {
        return 0;
    }
    n = tcrf_LevelSelectVar;
    if (n != 4 && n != 5 && n != 9) {
        return 0;
    }
    stg = GetStageNumber();
    if (stg < 0xF00) {
        STAGE_LIST6 list;

        list = D_8C15C808;
        p = list.stg;
        while (*p != stg) {
            if (*p == 0x7FFF) {
                return 0;
            }
            p++;
        }
    } else if (stg >= 0x1A00 && stg < 0x2300) {
        if (func_8C049A4E()) {
            return 0;
        }
    } else {
        return 0;
    }
    if (!(tp = CreateElementalTask(2, 3, func_8C0DECDA))) {
        return 0;
    }
    return tp;
}
#else
ASM_STUB(task *func_8C0DEDC0(void))
#endif
