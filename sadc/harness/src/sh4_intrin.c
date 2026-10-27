/*
 * sh4_intrin.c - SHC intrinsics (machine.h _builtin_*) for natively compiled
 * decompiled code, with the same semantics as the interpreter's instructions
 * (the shared models of include/pc/sh4_fpmodel.h, here as static inlines).
 * _builtin_ftrv uses the interpreter's XMTRX (SH4.xf), i.e. the matrix the
 * original Ninja code loaded.
 */
#include "pc/sh4_fpmodel.h"     /* before sh4.h: its declarations then refer to these inlines */
#include "sh4.h"

#include <string.h>

/* XMTRX for the libsa1 runtime compiled into the harness (--rt builds,
 * libsa1/src/rt/rt.h: SA1_XMTRX): the interpreter's XF bank, so native
 * runtime code and the original SDK code share one matrix register */
float *const sa1h_xmtrx = SH4.xf.f;

float _builtin_fabsf(float x)
{
    uint32_t u;
    memcpy(&u, &x, 4);
    u &= 0x7FFFFFFFu;
    memcpy(&x, &u, 4);
    return x;
}

float _builtin_sqrtf(float x) { return sh4_fsqrt(x); }
float _builtin_fipr(float *a, float *b) { return sh4_fipr(a, b); }
float _builtin_fsrra(float x) { return sh4_fsrra(x); }

void _builtin_ftrv(float *v, float *out)
{
    sh4_ftrv(SH4.xf.f, v, out);
}

void _builtin_fsca(long ang, float *s, float *c)
{
    sh4_fsca((uint32_t)ang, s, c);
}

void _builtin_set_cr(int cr) { sh4_set_sr(&SH4, (uint32_t)cr); }
int _builtin_get_cr(void) { return (int)SH4.sr; }
void _builtin_set_imask(int mask) { SH4.sr = (SH4.sr & ~SR_IMASK) | (((uint32_t)mask & 15u) << 4); }
int _builtin_get_imask(void) { return (int)((SH4.sr >> 4) & 15u); }
void _builtin_set_gbr(void *p) { SH4.gbr = (uint32_t)(uintptr_t)p; }
void *_builtin_get_gbr(void) { return (void *)(uintptr_t)SH4.gbr; }

void sh4_unsupported_call(const char *name, uint32_t guest)
{
    sh4_fault(&SH4, SH4_FAULT_HOOK, guest, "native call to %s: signature not supported by the thunk generator", name);
}

/* body of ASM_STUB() in include/common.h (and of the TU_GAP pool-layout fillers some files use):
   a call SHC cannot inline; natively it does nothing */
void __asm_stub_sink(void) {}
