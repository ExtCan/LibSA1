#include "common.h"

/*
 * 1ST_READ 8C04C5E0-8C04C678 (8 zero bytes of padding up to 8C04C680):
 * PRS decompressor (SEGA's LZ77 variant). Decompresses the PRS stream at
 * src into dst and returns the number of bytes written. Called by the file
 * loaders 8C0356AC, 8C0997BC ("%s.PRS"), 8C099872 and 8C09DA44.
 *
 * Hand-written SH-4 assembly, not SHC output:
 * - the control-bit reader is "DT R7; BF/S skip; SHLR R6" followed at the
 *   join by "BT"/"BF" on the T bit left by SHLR (the delayed SHLR runs on
 *   both paths and its result is discarded on the reload path, which
 *   repeats the SHLR); SHC never branches on a T bit produced by a shift
 *   across a join;
 * - the 2-bit length of the short copy is assembled with ROTCL R0 straight
 *   from those T bits;
 * - the two mask constants (H'FF00, H'E000) sit in a literal pool directly
 *   behind the entry BRA and are kept in R8/R9 for the whole routine; the
 *   long-copy path jumps into the middle of the short-copy tail (L8C04C638)
 *   and of the copy loop (L8C04C63C) with the OR of the mask in a BF/S
 *   delay slot.
 * A "#pragma inline_asm" version cannot match either: SHC appends
 * ".ALIGN 4 / RTS / NOP" after the body, which would land on the zero
 * padding at 8C04C678 (the original returns with "RTS; SUB R3,R0").
 *
 * The body below is a C description of the routine (PC / libsa1
 * implementation); it is kept under NON_MATCHING only and the original
 * assembly stays in the build.
 *
 * Format: control bits are read LSB first, 8 per control byte; the first
 * control byte is the first byte of the stream, later ones are fetched
 * from the stream when the previous one is used up.
 *   1                  literal: copy one byte from src.
 *   0 0 b1 b0, o       short copy: length (b1 b0) + 2 (2..5) from
 *                      dst + o - 256 (offset -256..-1).
 *   0 1, lo hi         word w = lo | hi << 8; w == 0 ends the stream.
 *                      Offset (w >> 3) - 8192 (-8192..-1); length
 *                      (w & 7) + 2 (3..9), or, when (w & 7) == 0, the
 *                      next byte + 1 (1..256).
 * Copies run byte by byte, so overlapping copies repeat the pattern.
 */

#ifdef NON_MATCHING
/* next control bit into bit (R7 counts down the bits left, R6 shifts) */
#define PRS_GETBIT(bit)                     \
    do {                                    \
        if (--nbits == 0) {                 \
            flags = *s++;                   \
            nbits = 8;                      \
        }                                   \
        (bit) = (Sint32)(flags & 1u);       \
        flags >>= 1;                        \
    } while (0)

Sint32 func_8C04C5E0(void *src, void *dst)
{
    Uint8 *s;
    Uint8 *d;
    Uint8 *start;
    Uint8 *from;
    Uint32 flags;
    Uint32 w;
    Sint32 nbits;
    Sint32 bit;
    Sint32 len;

    s = (Uint8 *)src;
    d = (Uint8 *)dst;
    start = d;
    flags = *s++;
    nbits = 9;
    for (;;) {
        PRS_GETBIT(bit);
        if (bit != 0) {
            /* literal */
            *d++ = *s++;
            continue;
        }
        PRS_GETBIT(bit);
        if (bit == 0) {
            /* short copy: 2-bit length, 8-bit offset */
            PRS_GETBIT(bit);
            len = bit << 1;
            PRS_GETBIT(bit);
            len |= bit;
            from = d + ((Sint32)*s++ - 0x100);
            len += 2;
        } else {
            /* long copy: 13-bit offset, 3-bit length or length byte */
            w = (Uint32)s[0] | ((Uint32)s[1] << 8);
            s += 2;
            if (w == 0) {
                break;
            }
            from = d + ((Sint32)(w >> 3) - 0x2000);
            len = (Sint32)(w & 7u);
            if (len != 0) {
                len += 2;
            } else {
                len = (Sint32)*s++ + 1;
            }
        }
        do {
            *d++ = *from++;
        } while (--len != 0);
    }
    return (Sint32)(d - start);
}
#endif
