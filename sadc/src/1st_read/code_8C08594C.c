#include "common.h"
#include "ninja.h"

/*
 * 8C08594C-8C08595E: part of a hand-written SH-4 assembly module of small
 * vector helpers (starts at 8C0858C0 after a zero-filled object gap). Not SHC
 * output: functions are 4-byte aligned with "09 00" NOP fill (asmsh style;
 * SHC 5.1r03 -code=machinecode packs functions at 2 bytes and pads with
 * "00 09"), the vector is loaded straight into FV0 with FLDI0 for the unused
 * lanes and FIPR FV0,FV0, and FMOV FR3,FR0 is scheduled before FSQRT FR0 in
 * the RTS delay slot. SHC's fipr() intrinsic always goes through two memory
 * arrays (FMOV.S @R3+/@R2+ into FV0/FV4), so the code cannot be produced from
 * C. The original assembly stays in the build.
 */

/* SHC intrinsics (machine.h fipr / sqrtf) */
extern float _builtin_fipr(float vect1[4], float vect2[4]);
extern float _builtin_sqrtf(float x);

#ifdef NON_MATCHING
/* XZ-plane length of a vector: sqrtf(x*x + z*z) (y ignored). */
Float func_8C08594C(NJS_VECTOR *v) {
    float t[4];

    t[0] = v->x;
    t[1] = 0.0f;
    t[2] = v->z;
    t[3] = 0.0f;
    return _builtin_sqrtf(_builtin_fipr(t, t));
}
#endif
