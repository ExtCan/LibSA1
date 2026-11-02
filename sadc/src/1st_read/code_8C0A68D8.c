/*
 * 1ST_READ chunk 8C0A68D8 (part): the emblem helpers the character select
 * runs in Trial mode (gsa1_090_charselect.md 2.7: 8C0A68D8 / 8C0A69A8 from
 * E3; the Trial clear count 8C900F2C / 8C900F86, UNVERIFIED role).
 *
 *   func_8C0A68D8  is emblem n set in the save data (bit n of the flags at
 *                  +0x240)?
 *   func_8C0A69A8  emblem number of (character, level, act): action stages
 *                  1-14 count the stages of the per-character mask table
 *                  D_8C1C53DC up to this one (32 per act), level 0 gives
 *                  100 + 5 * act, levels 35-38 level + 61 + 5 * act, level 42
 *                  106 + act, others 0
 */
#include "sa1/sa1.h"

/* The save data as these helpers see it (only the emblem flags) */
typedef struct {
    Uint8 pad000[0x240];
    Sint8 emblem[1];            /* 0x240 one bit per emblem */
} SAVE_EMBLEMS;

extern Sint16 D_8C1C53DC[];

/* 1 << s with SH-4 SHAD semantics: a negative count shifts right (n % 8 is
 * negative for a negative emblem number, e.g. -1 from 8C0A69A8 for a level
 * without emblems: the original then tests bit 0 >> 1 = nothing and returns
 * 0). C leaves a negative shift count undefined (x86 masks it to 5 bits), so
 * the PC build spells the SHAD rule out; SHC gets the plain shift (same
 * code). Proposed for include/common.h as SA1_SHAD (LANDING.md). */
#if defined(TARGET_PC)
#define SHAD1(s) ((s) >= 0 ? (1 << (s)) : (1 >> -(s)))
#else
#define SHAD1(s) (1 << (s))
#endif     /* per character: bit (level - 1) set when the level has emblems */

Sint32 func_8C0A68D8(SAVE_EMBLEMS *save, Sint32 n)
{
    Sint32 i;
    Sint32 b;

    i = n / 8;
    b = SHAD1(n % 8);
    if (save->emblem[i] & b) {
        return 1;
    }
    return 0;
}

/* NON_MATCHING: complete and faithful; register allocation (the mask table pointer in R13 instead of R1) and the add order of the last steps differ */
#ifdef NON_MATCHING
Sint32 func_8C0A69A8(Sint32 chr, Sint32 level, Sint32 act)
{
    Sint32 n;
    Sint32 i;
    Uint16 bits;
    Sint32 mask;
    Sint32 act5;

    n = 0;
    act5 = act * 5;
    if (level == 0) {
        n = 100;
        n += act5;
    } else if (level <= 14) {
        mask = 0xFFFF >> (15 - level);
        for (i = 0; i < chr + 1; i++) {
            if (chr == i) {
                bits = mask & D_8C1C53DC[i];
            } else {
                bits = D_8C1C53DC[i];
            }
            while (bits != 0) {
                if (bits & 1) {
                    n++;
                }
                bits >>= 1;
            }
        }
        n--;
        n += act * 32;
    } else if (level > 34 && level <= 38) {
        n = level + 61;
        n += act5;
    } else if (level == 42) {
        n = 106;
        n += act;
    }
    return n;
}
#else
ASM_STUB(Sint32 func_8C0A69A8(Sint32 chr, Sint32 level, Sint32 act))
#endif
