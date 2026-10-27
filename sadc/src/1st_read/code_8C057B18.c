#include "common.h"
#include "ninja.h"
#include "sa1/shc_intrinsics.h"

/*
 * 8C057B18-8C057B40: hand-written SH-4 assembly (last routine of the
 * vector helper module 8C057AE0-8C057B40, see code_8C057AE0.c). Not SHC
 * output:
 * - a is loaded once into FR8-FR10 and both differences are formed directly
 *   in FV0 (b - a) and FV4 (c - a) with @R5+/@R4+/@R6+ post-increment loads
 *   (including the dead final increments) and FLDI0 FR3/FR7 interleaved;
 * - FIPR FV4,FV0 is used on those registers directly. SHC 5.1's fipr
 *   intrinsic always stores both operands to stack arrays and reloads them
 *   (49 instructions with ADD #-32/#32,R15), and the plain C dot product
 *   compiles to FMUL/FMAC (27 instructions);
 * - FMOV FR3,FR0 sits in the RTS delay slot.
 * The body below is a C description (used on PC); the original assembly
 * stays in the build.
 */

#ifdef NON_MATCHING
/* Dot product of (b - a) and (c - a): FIPR of the two edge vectors from a. */
Float func_8C057B18(NJS_POINT3 *a, NJS_POINT3 *b, NJS_POINT3 *c)
{
    float u[4];
    float v[4];

    u[0] = b->x - a->x;
    u[1] = b->y - a->y;
    u[2] = b->z - a->z;
    u[3] = 0.0f;
    v[0] = c->x - a->x;
    v[1] = c->y - a->y;
    v[2] = c->z - a->z;
    v[3] = 0.0f;
    return _builtin_fipr(v, u);
}
#endif
