#include "common.h"
#include "ninja.h"

/*
 * 8C057AFC-8C057B18: hand-written SH-4 assembly (vector helper module
 * 8C057AE0-8C057B40, a region without the 00 09 pool padding of SHC objects;
 * see docs/compiler.md). Twin of func_8C057AE0 (add) with FSUB instead of
 * FADD. Not SHC output:
 * - the final loads of both operands are still FMOV.S @R4+,FR2 / @R5+,FR5
 *   although R4/R5 are dead afterwards; SHC drops a dead final
 *   post-increment (FMOV.S @R4,FRn) in every form tried (struct fields,
 *   x = *a++ - *b++, six separate loads, x = *a++; x -= *b++;, a loop);
 * - operands live in FR0-FR2 / FR3-FR5 in strict sequence; SHC allocates
 *   these leaf temporaries from FR3-FR9 (FR3 reused for every b load).
 * The result is stored with ADD #12,R6 / FMOV.S FRn,@-R6 (z, y, x); all
 * three components are read before any is written, so out may alias a or b.
 * The original assembly stays in the build.
 */

#ifdef NON_MATCHING
/* njSubVector-like, three-operand form: out = a - b. */
void func_8C057AFC(NJS_VECTOR *a, NJS_VECTOR *b, NJS_VECTOR *out) {
    Float x;
    Float y;
    Float z;

    x = a->x - b->x;
    y = a->y - b->y;
    z = a->z - b->z;
    out->z = z;
    out->y = y;
    out->x = x;
}
#endif
