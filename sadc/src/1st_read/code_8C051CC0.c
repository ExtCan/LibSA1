/*
 * Landtable manager (1ST_READ 8C0519C0 chunk): 8C051CC0 selects the current
 * landtable. Called by the landtable manager task (8C0519C0, modes 0 and 2).
 * In mode 0 of that task (B_8C7596F8 = its taskwk) the current stage number
 * ((level << 8) | act, GetStageNumber) is latched into D_8C19E970/D_8C19E971;
 * B_8C7596F4 then becomes D_8C19E9FC[level][act] (static [level][8 acts]
 * table of landtable pointers into the stage overlay). With a landtable,
 * landtable gathering is enabled (D_8C19E974/D_8C19E978 = 1) and 8C051A52
 * initialises it; without one both flags are cleared.
 */
#include "common.h"
#include "ninja.h"
#include "task.h"
#include "sa1/engine.h"

struct _OBJ_LANDTABLE;

extern taskwk *B_8C7596F8;                      /* landtable manager task's taskwk (8C0519C0) */
extern Sint8 D_8C19E970;                        /* latched level */
extern Sint8 D_8C19E971;                        /* latched act */
extern Sint32 D_8C19E974;                       /* 1 = landtable present */
extern Sint32 D_8C19E978;                       /* 1 = also gather landtable entries (8C0522C0) */
extern struct _OBJ_LANDTABLE *B_8C7596F4;       /* current landtable */
extern struct _OBJ_LANDTABLE *D_8C19E9FC[][8];  /* [level][act] landtables (data) */

extern Sint32 func_8C094DC4(void);              /* returns 0 (empty stub) */

ASM_STUB(void func_8C051A52(Sint32 level, Sint32 act))

void func_8C051CC0(void)
{
    Uint16 stg;

    if (B_8C7596F8 != NULL && B_8C7596F8->mode == 0) {
        stg = GetStageNumber();
        D_8C19E970 = (stg & 0xFF00) >> 8;
        D_8C19E971 = stg;
    }
    D_8C19E974 = 1;
    D_8C19E978 = 1;
    B_8C7596F4 = D_8C19E9FC[D_8C19E970][D_8C19E971];
    if (B_8C7596F4 == NULL) {
        D_8C19E974 = 0;
        D_8C19E978 = 0;
        return;
    }
    func_8C094DC4();
    func_8C051A52(D_8C19E970, D_8C19E971);
}
