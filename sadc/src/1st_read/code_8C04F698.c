/*
 * GetPlayerNumber (1ST_READ 8C04F698): current character id
 * (CurrentCharacter, B_8C7493C8), returned as an unsigned 16-bit value
 * (MOV.W + EXTU.W). 0 Sonic, 1 Eggman, 2 Tails, 3 Knuckles, 4 Tikal, 5 Amy,
 * 6 Gamma, 7 Big. 75+ callers.
 *
 * NON_MATCHING only because of translation-unit context: the body is exact,
 * but the original object also holds the preceding setter func_8C04F692
 * (void (Sint16 n) { CurrentCharacter = n; }, address in R3), and SHC
 * continues its scratch-register rotation from the previous function of the
 * same file. Compiled alone this body loads the address into R3; placed right
 * after that setter it uses R2 exactly as the original does (verified MATCH
 * with shcdiff). An ASM_STUB neighbour does not reproduce it, and source-only
 * variants (Uint16/Sint32/Uint32 locals and return types, pointer local,
 * & 0xFFFF, casts) all stay on R3. Merge into the 8C04D920 chunk's file after
 * func_8C04F692 to match.
 */
#include "sa1/sa1.h"

#ifdef NON_MATCHING
Sint32 GetPlayerNumber(void)
{
    return (Uint16)CurrentCharacter;
}
#else
ASM_STUB(Sint32 GetPlayerNumber(void))
#endif
