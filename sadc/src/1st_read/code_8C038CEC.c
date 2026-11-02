/*
 * 1ST_READ 8C038CEC-8C038D00 (its own chunk): palette bank select of the
 * hand-written basic-model renderer: GBR + 0x58 = D_8C038F60 + bank * 0x800
 * (GBR = the renderer work B_8C8FFE00, set by 8C036F60). Callers: the
 * character displays (Sonic 8C0AE17A, 8C0BA820, 8C0C46AC, 8C0CFACE,
 * 8C0DA122, 8C0DE598, 8C0E3A40, 8C0EE43C, ...) select bank 2 around their
 * draws and 0 after; the character select's 3D task 8C902FAE selects 2.
 *
 * NON_MATCHING only, and SHC only: the original is hand-written assembly
 * (shift order SHLL8 / SHLL / SHLL2 that SHC does not produce for "<< 11",
 * H'0000 alignment fill, its own .END). The C below is a description; with
 * SHC's GBR intrinsic it differs from the original only in the shift order.
 * libsa1 already defines 8C038CEC in its runtime (LIB src/rt/rt_draw.c,
 * section 5: it keeps the bank for the draw capture), so this C must not be
 * lowered for the PC (hence the !TARGET_PC guard).
 */
#include "sa1/sa1.h"

extern Uint8 D_8C038F60[];              /* the palette banks (0x800 bytes each), as code_8C0141E0.c declares them */
extern void _builtin_gbr_write_long(int offset, Uint32 data);  /* SHC intrinsic: MOV.L R0,@(offset,GBR) */

#if defined(NON_MATCHING) && !defined(TARGET_PC)
void func_8C038CEC(Sint32 bank)
{
    _builtin_gbr_write_long(88, (Uint32)&D_8C038F60[bank << 11]);
}
#endif
