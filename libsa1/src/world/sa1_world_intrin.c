/*
 * sa1_world_intrin.c - the runtime pieces without a DC address that lowered game code calls
 * (sa1/guest.h "R7: runtime functions and data without a DC address"): the SHC intrinsics
 * _builtin_fabsf / sqrtf / fsrra / fipr / ftrv / fsca renamed by pclower to sa1g_*, on the
 * interpreter's FP models (include/pc/sh4_fpmodel.h, design 4.3 and 8.2), and the SH-4 SR / GBR
 * accessors. SR and GBR are per world (GBR is kept as a guest word, so it survives a world switch).
 *
 * The whole file is game text (sa1g_text.h): a fault inside one of these functions (a trap pointer
 * passed to sa1g_fipr ...) is a world fault like one in the game code that called it.
 */
#define SA1G_TEXT_PRAGMA
#include "sa1g_text.h"

#include "sa1_world.h"
#include "pc/sh4_fpmodel.h"

static uint32_t sa1g_sr_idle;   /* outside a world (startup code run by tests) */
static uint32_t sa1g_gbr_idle;

float sa1g_fabsf(float x)
{
    return sh4_u2f(sh4_f2u(x) & 0x7FFFFFFFu);  /* FABS is a bit operation (NaNs included) */
}

float sa1g_sqrtf(float x)
{
    return sh4_fsqrt(x);
}

float sa1g_fsrra(float x)
{
    return sh4_fsrra(x);
}

float sa1g_fipr(float *a, float *b)
{
    return sh4_fipr(a, b);
}

void sa1g_ftrv(float *v, float *out)
{
    sh4_ftrv(sa1_rt_xmtrx, v, out);             /* XMTRX of the ticking world */
}

void sa1g_fsca(int32_t ang, float *s, float *c)
{
    sh4_fsca((uint32_t)ang, s, c);
}

static uint32_t *sa1g_sr_slot(void)
{
    sa1g_world *w = sa1g_world_current();
    return w ? &w->sr : &sa1g_sr_idle;
}

void sa1g_set_cr(int cr)
{
    *sa1g_sr_slot() = (uint32_t)cr;
}

int sa1g_get_cr(void)
{
    return (int)*sa1g_sr_slot();
}

void sa1g_set_imask(int mask)
{
    uint32_t *sr = sa1g_sr_slot();
    *sr = (*sr & ~0xF0u) | (((uint32_t)mask & 15u) << 4);
}

int sa1g_get_imask(void)
{
    return (int)((*sa1g_sr_slot() >> 4) & 15u);
}

void sa1g_set_gbr(void *p)
{
    sa1g_world *w = sa1g_world_current();
    uint32_t g = w ? sa1_encode(p) : (uint32_t)(uintptr_t)p;
    if (w) {
        w->gbr = g;
    } else {
        sa1g_gbr_idle = g;
    }
}

void *sa1g_get_gbr(void)
{
    sa1g_world *w = sa1g_world_current();
    return w ? sa1_decode(w->gbr) : (void *)(uintptr_t)sa1g_gbr_idle;
}
