#include "common.h"
#include "ninja.h"

/*
 * 8C057AE0-8C057AFC: hand-written SH-4 assembly (vector helper module
 * 8C057AE0-8C057B40 with func_8C057AFC (subtract) and func_8C057B18, which
 * loads straight into FV0/FV4 with FLDI0 lanes and uses FIPR FV4,FV0 with
 * FMOV FR3,FR0 in the RTS delay slot). Not SHC output:
 * - the last load of the first operand is still FMOV.S @R4+,FR2 although R4
 *   is dead afterwards; SHC always drops a dead final post-increment
 *   (FMOV.S @R4,FRn) in every form tried (*a++ + *b++, separate loads,
 *   register locals);
 * - operands live in FR0-FR2 / FR3-FR5 in strict sequence; SHC allocates
 *   leaf temporaries from FR4-FR9 and never picked FR0-FR2 here.
 * The result is stored with ADD #12,R6 / FMOV.S FRn,@-R6 (z, y, x).
 * The original assembly stays in the build.
 */

#ifdef NON_MATCHING
/* njAddVector-like, three-operand form: out = a + b. */
void func_8C057AE0(NJS_VECTOR *a, NJS_VECTOR *b, NJS_VECTOR *out) {
    Float x;
    Float y;
    Float z;

    x = a->x + b->x;
    y = a->y + b->y;
    z = a->z + b->z;
    out->z = z;
    out->y = y;
    out->x = x;
}
#endif
