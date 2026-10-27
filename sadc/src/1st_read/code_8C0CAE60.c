/*
 * 1ST_READ 8C0CAE60-8C0CB380 (chunk 8C0CAE60): Knuckles dig effects
 * (knu_eff.c TU 8C0CA040-8C0CBE00).
 *  func_8C0CAE60  spawns 4 debris pieces (exec func_8C0CAD20, disp
 *                 lbl_8C0CAD98) at pos, each with velocity
 *                 v + (0.5 - rand) on every axis (twp->scl), random
 *                 ang.x / ang.z and a random spin ang.y in
 *                 [-2048, 2048); called by KnucklesModeMachine when
 *                 digging and by the dig-up spawner below
 *  func_8C0CB1A0  exec of the dig-up ring spawner: mode 0 sets 32 rings
 *                 (ang.x) and a 17-frame delay (ang.y); mode 1 releases
 *                 up to rand * 5.1 / 32768 rings every 17 frames through
 *                 func_8C087A9E (8 units below pos) and frees itself when
 *                 none are left; every 8 frames (ang.z) it throws a ring
 *                 of dirt clods (func_8C0CA9B0) every 0x800 BAMS from a
 *                 random start angle, plus 4 debris pieces (v = up)
 *  func_8C0CB30E  creates the spawner task at pos
 *
 * Spawner taskwk use: ang.x = rings left, ang.y = delay, ang.z = frame
 * counter. Particle taskwk use: scl = velocity.
 *
 * Matching notes:
 *  - func_8C0CAD20 / lbl_8C0CAD98 / func_8C0CA9B0 were defined in the same
 *    translation unit in the original (chunks 8C0CAD20, 8C0CA780). SHC keeps
 *    the address of a same-file function in the literal pool instead of a
 *    callee-saved register: func_8C0CAE60 (loop unrolled 4x by SHC) only gets
 *    the original allocation (pos in R9, &twp->scl.x in R8 across rand())
 *    with the ASM_STUBs below (same finding as code_8C0CA780.c). The build
 *    splicer skips ASM_STUB bodies and functions of other chunks.
 *  - FMAC audit: the 3 SA1_FMAC of the loop body are the 12 FMACs of the
 *    asm (SHC unrolls the 4-iteration loop 4x, clang does not). The FMAC
 *    multiplies by 1.0 (FR13), so fused or not gives the same result.
 *  - func_8C0CB1A0: the ring scatter call matches only with the count as
 *    the LAST parameter (SCATTER_RINGS, as in code_8C0C8534.c): SHC
 *    evaluates arguments right to left, so the original prototype was most
 *    likely (Float x, Float y, Float z, Sint32 n); the registers (R4,
 *    FR4-FR6) are the same either way, and the PC build keeps the order of
 *    the definition in code_8C0876F4.c. With the count first, the callee
 *    address load and the clamp's BT were scheduled differently.
 *    "v.x = v.z = 0.0f" gives the store order (z, x, y) and with it the
 *    njCos (not njSin) hoist into R8.
 */
#include "common.h"
#include "sa1/sa1.h"

ASM_STUB(void func_8C0CAD20(task *tp))  /* debris exec (8C0CAD20.src) */
ASM_STUB(void lbl_8C0CAD98(task *tp))   /* debris disp (8C0CAD20.src) */
ASM_STUB(void func_8C0CA9B0(task *tp))  /* dirt clod exec (8C0CA780.src) */
#if defined(TARGET_PC)
extern void func_8C087A9E(Sint32 n, Float x, Float y, Float z); /* ring burst (code_8C0876F4.c) */
#define SCATTER_RINGS(x, y, z, n) func_8C087A9E((n), (x), (y), (z))
#else
extern void func_8C087A9E(Float x, Float y, Float z, Sint32 n);
#define SCATTER_RINGS(x, y, z, n) func_8C087A9E((x), (y), (z), (n))
#endif

void func_8C0CAE60(NJS_POINT3 *pos, NJS_POINT3 *v)
{
    task *tp;
    Sint32 i;

    for (i = 0; i < 4; ++i) {
        if ((tp = CreateElementalTask(2, 6, func_8C0CAD20)) != NULL) {
            tp->twp->pos = *pos;
            tp->twp->scl.x = SA1_FMAC(1.0f, 0.5f - (Float)rand() * 3.0517578125e-05f, v->x);
            tp->twp->scl.y = SA1_FMAC(1.0f, 0.5f - (Float)rand() * 3.0517578125e-05f, v->y);
            tp->twp->scl.z = SA1_FMAC(1.0f, 0.5f - (Float)rand() * 3.0517578125e-05f, v->z);
            tp->twp->ang.x = SA1_FTOI((Float)rand() * 2.0f);
            tp->twp->ang.y = SA1_FTOI((0.5f - (Float)rand() * 3.0517578125e-05f) * 4096.0f);
            tp->twp->ang.z = SA1_FTOI((Float)rand() * 2.0f);
            tp->disp = lbl_8C0CAD98;
        }
    }
}

void func_8C0CB1A0(task *tp)
{
    taskwk *twp;
    task *ctp;
    Sint32 n;
    Angle ang;
    NJS_POINT3 v;

    twp = tp->twp;
    switch (twp->mode) {
    case 0:
        twp->ang.x = 32;
        twp->ang.y = 17;
        twp->mode = 1;
        break;
    case 1:
        if (--twp->ang.y > 0) {
            break;
        }
        twp->ang.y = 17;
        n = SA1_FTOI((Float)rand() * 1.5563965e-04f);
        if (twp->ang.x < n) {
            n = twp->ang.x;
        }
        SCATTER_RINGS(twp->pos.x, twp->pos.y + -8.0f, twp->pos.z, n);
        if ((twp->ang.x -= n) <= 0) {
            FreeTask(tp);
            return;
        }
        break;
    }
    if ((++twp->ang.z & 7) == 0) {
        for (ang = SA1_FTOI((Float)rand() * 0.0625f); ang < 0x10000; ang += 0x800) {
            if ((ctp = CreateElementalTask(2, 6, func_8C0CA9B0)) != NULL) {
                ctp->twp->pos = twp->pos;
                ctp->twp->scl.x = (Float)rand() * njSin(ang) * 3.0517578125e-05f;
                ctp->twp->scl.y = SA1_FMAC((Float)rand(), 5.340576171875e-05f, 0.25f);
                ctp->twp->scl.z = (Float)rand() * njCos(ang) * 3.0517578125e-05f;
            }
        }
        v.x = v.z = 0.0f;
        v.y = 1.0f;
        func_8C0CAE60(&twp->pos, &v);
    }
}

void func_8C0CB30E(NJS_POINT3 *pos)
{
    task *tp;

    if ((tp = CreateElementalTask(2, 6, func_8C0CB1A0)) != NULL) {
        tp->twp->pos = *pos;
    }
}
