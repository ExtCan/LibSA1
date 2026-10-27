/* @tu 8C0673E0 */
/*
 * Compiler probe t04 -- BYTE-EXACT match of a translation-unit tail.
 * Original: 1ST_READ.BIN 0x8C0673E0-0x8C067480 (160 bytes):
 *   8C0673E0 IsStage1   ((u8)(GetStageAct() >> 8) range check)
 *   8C067414 GetDir     (njScalor / njUnitVector)
 *   8C067456 Dummy      (empty function: RTS/NOP)
 *   8C06745A-8C06745F   .ALIGN 32 padding = bytes 00 09 00 09 00 09
 *   8C067460-8C067473   literal pool (5 entries)
 *   8C067474-8C06747F   linker gap (00 bytes) up to the next object (TU starts 8C067480)
 * 0x8C0673E0 follows a mid-TU pool that was itself followed by .ALIGN 32, so
 * compiling just these functions reproduces the tail of the TU exactly.
 *
 * Result (shcprobe.py --exact, SA1 flags):
 *   5.1r03                 MATCH (0 differing halfwords)
 *   5.1r04, 5.1r08         3 halfwords differ: the .ALIGN padding is 09 00 (LE NOP)
 *                          instead of 00 09 -- the machine-code object writer
 *                          endianness bug is fixed from 5.1r04 on.
 *   5.1r11, 5.1r13, <=5.1r01: code differs (IsStage1: these versions drop the
 *                          CMP/PZ + NOT/SHLR8/NOT signed-shift sequence).
 *   no -speed or no -extra=a=1800: differs.
 * Flags: -cpu=sh4 -division=cpu -fpu=single -endian=little -optimize=1 -speed
 *        -macsave=0 -extra=a=1800 -round=zero -code=machinecode   (SHC 5.1r03)
 */
typedef struct { float x, y, z; } NJS_POINT3;
typedef NJS_POINT3 NJS_VECTOR;

extern unsigned short GetStageAct_8C04F7E0(void);
extern void sub_8C066320(NJS_VECTOR *, NJS_VECTOR *);
extern float njScalor_8C63A75C(NJS_VECTOR *);
extern void njUnitVector_8C63A94C(NJS_VECTOR *);

/* @match IsStage1 8C0673E0-8C067414 */
int IsStage1(void)
{
    unsigned short stg;
    stg = (unsigned char)(GetStageAct_8C04F7E0() >> 8);
    if (stg >= 0 && stg < 15) {
        return 1;
    }
    return 0;
}

/* @match GetDir 8C067414-8C067456 */
float GetDir(NJS_VECTOR *dir)
{
    NJS_VECTOR v;
    float len;

    sub_8C066320(dir, &v);
    len = njScalor_8C63A75C(&v);
    if (len < 0.000001f) {
        dir->x = 0.0f;
        dir->y = 1.0f;
        dir->z = 0.0f;
    }
    njUnitVector_8C63A94C(dir);
    return len;
}

/* @match Dummy 8C067456-8C06745A */
void Dummy(void)
{
}
