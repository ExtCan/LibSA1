#include "common.h"
#include "ninja.h"
#include "sa1/ninja_funcs.h"

/*
 * 8C057B40-8C057BB0: hand-written SH-4 assembly (not SHC output). It is a
 * variant of the Ninja library's njPushMatrix (8C639C70), from which it is
 * copied line for line up to the save of the current matrix:
 * - the current matrix lives in XMTRX (the XF bank); it is saved with
 *   FSCHG + "FMOV XD14..XD0,@-Rn" pair stores, which SHC 5.1 never emits
 *   (compiled game code contains no FSCHG at all, see docs/compiler.md);
 * - the identity is built with FLDI1/FLDI0 in FR0-FR5 and copied into the
 *   XF bank with FMOV DRm,XDn pair moves, then stored to the new slot;
 * - the literal pool after the first RTS is only 4-byte aligned with no
 *   00 09 fill, and the object ends at 8C057BB0 followed by the linker's
 *   zero gap (region 8C057AE0-8C057BC0 is listed as assembly in
 *   docs/compiler.md; neighbours 8C057AE0/8C057AFC/8C057B18 are asm too).
 *
 * The body below is a C description of what the assembly does (a Ninja
 * "push unit matrix"), kept under NON_MATCHING only; the original assembly
 * stays in the build. On the PC the Ninja reimplementation keeps the
 * emulated XMTRX as the current matrix, so the routine is expressed with
 * Ninja calls: njPushMatrix(NULL) stores XMTRX into the current slot and
 * advances; the new slot and XMTRX are then set to the identity
 * (njUnitMatrix(m) writes m, njUnitMatrix(NULL) loads XMTRX).
 */

extern NJS_MATRIX_PTR B_8C88F5F8; /* current matrix slot of the Ninja matrix stack */
extern Sint32 B_8C88F698;         /* matrix stack depth limit */
extern Sint32 B_8C88F69C;         /* matrix stack depth */

#ifdef NON_MATCHING
/* Pushes the matrix stack and makes the identity the current matrix.
 * Returns 1, or 0 (and changes nothing) when the stack is full. */
Sint32 func_8C057B40(void) {
    if (B_8C88F69C >= B_8C88F698) {
        return 0;
    }
    njPushMatrix(NULL);
    njUnitMatrix(B_8C88F5F8);
    njUnitMatrix(NULL);
    return 1;
}
#endif
