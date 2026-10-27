#ifndef SA1_SHC_INTRINSICS_H
#define SA1_SHC_INTRINSICS_H

/*
 * SHC 5.1 intrinsics (machine.h / umachine.h / mathf.h names) used by the
 * game code, declared once with the prototypes matched code already uses.
 *
 * SHC build: the compiler expands these names inline (FABS, FSQRT, FIPR,
 * FTRV, FSCA, FSRRA). FSCA/FSRRA are emitted as mnemonics that the build's
 * asmsh cannot assemble, so functions using them stay NON_MATCHING until the
 * splice pipeline handles them (see code_8C0826B0, code_8C0859A0).
 *
 * TARGET_PC: ordinary functions provided by the libsa1 runtime (the SHC
 * runtime is reimplemented, never decompiled):
 *   _builtin_fsca   sin/cos of a 16-bit BAMS angle (low 16 bits of ang) with
 *                   the FSCA table (tolerance until the hardware table is
 *                   characterised);
 *   _builtin_fsrra  approximate 1/sqrt(x);
 *   _builtin_fipr   4-element dot product (FIPR precision: tolerance);
 *   _builtin_ftrv   vec2 = XMTRX * vec1 with the runtime's emulated XMTRX,
 *                   which the Ninja matrix reimplementation keeps loaded.
 */

#include "common.h"

extern float _builtin_fabsf(float x);
extern float _builtin_sqrtf(float x);
extern float _builtin_fipr(float vect1[4], float vect2[4]);
extern void _builtin_ftrv(float vec1[4], float vec2[4]);
extern void _builtin_fsca(Sint32 ang, Float *s, Float *c);
extern float _builtin_fsrra(float x);

#endif