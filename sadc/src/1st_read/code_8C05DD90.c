/*
 * 1ST_READ 8C05DD90 (chunk 8C05D9C0-8C05E680, air item box O_ItemBoxAir).
 */
#include "sa1/sa1.h"

/*
 * Allocates and clears the 32-byte air item box work (ITEMBOXAIR_WK, see
 * code_8C05DF18.c: Sint32/Float fields only, so 32 bytes on every target),
 * which objexec_O_ItemBoxAir stores in tp->awp->work[0] on its first frame.
 * The empty NULL check is in the original: it leaves a dead TST R0,R0
 * before the clear loop (same allocate-and-clear pattern in 8C06523C,
 * 56 bytes, and 8C069C1C, 220 bytes). The byte loop is unrolled twice by
 * SHC.
 */
void *func_8C05DD90(void)
{
    Uint8 *buf;
    Uint8 *p;
    Sint32 i;

    if ((buf = (Uint8 *)MAlloc(32)) != NULL) {
    }
    p = buf;
    for (i = 0; i < 32; i++) {
        p[i] = 0;
    }
    return buf;
}
