/*
 * harness_pre.h - force-included (-include) before every decompiled C file
 * that the harness compiles natively (32-bit x86).
 *
 * ASM_STUB(proto) defines a dummy body for SHC (the build keeps the original
 * assembly); natively it becomes a plain declaration, so calls to it go to a
 * generated thunk that runs the original code in the interpreter.
 *
 * The SHC intrinsics (_builtin_*) are known to SHC without a declaration;
 * they are declared here and implemented in sh4_intrin.c with the SH-4
 * instruction semantics of the interpreter (include/pc/sh4_fpmodel.h).
 *
 * TARGET_PC_HARNESS also selects the harness profile of the libsa1 runtime
 * (libsa1/src/rt/rt.h, build.py --rt): SA1_XMTRX = the interpreter's XF bank.
 */
#ifndef HARNESS_PRE_H
#define HARNESS_PRE_H
#define TARGET_PC_HARNESS 1
#include "common.h"
#undef ASM_STUB
#define ASM_STUB(proto) proto;

float _builtin_fabsf(float x);
float _builtin_sqrtf(float x);
float _builtin_fsrra(float x);
float _builtin_fipr(float vect1[4], float vect2[4]);
void _builtin_ftrv(float vec1[4], float vec2[4]);
void _builtin_fsca(Sint32 ang, Float *s, Float *c);
void _builtin_set_cr(int cr);
int _builtin_get_cr(void);
void _builtin_set_imask(int mask);
int _builtin_get_imask(void);
#endif