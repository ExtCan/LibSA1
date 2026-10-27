#include "common.h"
#include "ninja.h"

/*
 * 8C02CE80-8C02CEA0: hand-written SH-4 assembly (not SHC output).
 *
 * First routine of the land collision list module 8C02CE80-8C02D54C
 * (free-list and list helpers 8C02CE80, 8C02CEA0, 8C02CECA, 8C02CEF4,
 * 8C02CF20, 8C02CF48 ..., see src/1st_read/code_8C028E60.c):
 * - zero-displacement stores are encoded MOV.L Rm,@(0,Rn) (0x1nm0); SHC
 *   always emits MOV.L Rm,@Rn (0x2nm2). In 1ST_READ the @(0,Rn) form only
 *   occurs in this module and in other assembly (8C036FFC, SDK libraries).
 * - the list heads it writes (D_8C02D540, D_8C02D544, D_8C02D548) are data
 *   words inside the code section, after func_8C02D528 (FTRV routine).
 * - unscheduled code: NOP in the RTS delay slot although MOV.L R0,@(0,R4)
 *   could fill it; R0/R1 used as scratch where SHC picks R6/R3.
 * SHC output for the C below (same algorithm, DT loop) differs in all 14
 * instructions; the original assembly stays in the build.
 *
 * Builds the free list of the RANGE-poly buffer (B_8C6BE27C, 0x2000
 * 64-byte nodes; called from 8C0285D2 and 8C028E60): node i links to node
 * i+1, the last node to NULL, and the head goes to D_8C02D540.
 * The DT loop runs n-1 times; n <= 1 is not guarded (the counter wraps),
 * the callers always pass 0x2000.
 */

/* polygon buffer entry ("RANGE-poly", B_8C6BE27C); same layout as in
 * code_8C028E60.c (should move to sa1/collision.h) */
typedef struct colpoly {
    struct colpoly *next;       /* 0x00 free list / per-object list (8C02CE80, 8C02D00E) */
    struct colpoly *prev;       /* 0x04 linked by 8C02D00E */
    Sint32 unk08;               /* 0x08 */
    Sint32 unk0C;               /* 0x0C */
    Sint32 unk10;               /* 0x10 */
    Float unk14;                /* 0x14 cleared by 8C028E60 */
    Sint32 unk18[10];           /* 0x18 */
} colpoly;                      /* 0x40 */

SA1_ASSERT_SIZE(colpoly, 0x40);

extern colpoly *D_8C02D540;     /* free polygons (head); data word in the code section */

#ifdef NON_MATCHING
void func_8C02CE80(colpoly *buf, Sint32 n)
{
    colpoly *next;

    D_8C02D540 = buf;
    next = buf + 1;
    n--;
    do {
        buf->next = next;
        buf++;
        next++;
    } while (--n != 0);
    buf->next = NULL;
}
#endif
