/*
 * ADVERTISE 8C900D60-8C900D82: menu button tests on the pad's "pressed"
 * mask B_8C754E5C (chunk 8C900D60, both functions).
 *
 *  func_8C900D60  "back" pressed: bits 4 | 8
 *  func_8C900D70  "confirm" pressed: bits 2 | 0x400
 */
#include "common.h"

extern Uint32 B_8C754E5C;       /* buttons pressed this frame (all ports) */

/*
 * NON_MATCHING (both): register choice only. The original loads the address
 * into R4 and the value over it (MOV.L @R4,R4); SHC gives R3 / R2 here.
 * Tried: a value local (R4 value, R3 address), a pointer local (R4 address,
 * R5 value), register, a parameter, Sint32 / volatile / array declarations,
 * split temporaries.
 */
#ifdef NON_MATCHING
Uint32 func_8C900D60(void)
{
    return (B_8C754E5C & 4) | (B_8C754E5C & 8);
}

Uint32 func_8C900D70(void)
{
    return (B_8C754E5C & 2) | (B_8C754E5C & 0x400);
}
#else
ASM_STUB(Uint32 func_8C900D60(void))
ASM_STUB(Uint32 func_8C900D70(void))
#endif
