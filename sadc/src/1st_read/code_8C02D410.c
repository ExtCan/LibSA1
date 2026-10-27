#include "common.h"
#include "ninja.h"
#include "sa1/sa1.h"

/*
 * 8C02D410-8C02D528: hand-written SH-4 assembly (not SHC output).
 *
 * Last routine of chunk 8C02D060-8C02D528, part of the hand-written land
 * collision module 8C02CE80-8C02D980 (see code_8C02CE80.c, code_8C02CF70.c,
 * code_8C02D528.c). It is the per-polygon worker of func_8C02D2E0, the
 * vertical ground/ceiling probe of one FD land object (SADX
 * GetZxShadowOnFDPolygon analogue, zxsdwstr / xssunit layout as in
 * code_8C02AD22.c); func_8C02D170 is the same kind of worker for
 * func_8C02D060.
 *
 * Why it cannot come from C:
 * - No ABI arguments: the caller's loop registers are inherited. R12 points
 *   at the polygon's vertices (colpoly + 0x18), R14 is the zxsdwstr probe,
 *   R13 the object's attribute (colobj.attr, 0x0C). R11 is used as scratch
 *   without being saved (the caller saved it).
 * - FR12 and FR15 (callee-saved in the SHC ABI) are overwritten without
 *   being saved, while FR6-FR11 are pushed in the middle of the routine,
 *   only on the path that needs them.
 * - The BSR helpers have private register conventions: func_8C02D2B2 takes
 *   the edge's vertices in R4/R5 and advances both by 12 (the next call
 *   reuses them; the third edge only does ADD #-36,R5), the probe x/z in
 *   FR4/FR5; func_8C02D286 takes the three vertices in FR0-FR8 and returns
 *   the normal in FR9-FR11; func_8C02D278 normalises FV4 in place with
 *   FIPR and FSRRA (.DATA.W H'F77D).
 * - FIPR FV8,FV0 is used inline for the plane offset; the module pads with
 *   little-endian NOPs (.DATA.W H'0009 before func_8C02D2E0, assembler
 *   .ALIGN 32 fill) and keeps its data words (D_8C02D548/D_8C02D54C, the
 *   float constants D_8C02D960-D_8C02D96C) in the code section.
 * A "#pragma inline_asm" version is not possible either: SHC appends its own
 * RTS/NOP after the body, but the original's literal pool (L8C02D518..)
 * has to follow the routine's own RTS/NOP.
 *
 * The body below is a C description of what the assembly does (PC / libsa1
 * implementation); it is kept under NON_MATCHING only and the original
 * assembly stays in the build. The register inputs become ordinary
 * parameters: func_8C02D410(&poly->v[0], zssp, co->attr).
 *
 * What it does: if the probe position lies strictly inside the triangle
 * projected on the XZ plane (the three edge functions have the same sign)
 * and the triangle is not too steep (n.y^2 > 0.0001), the height y of the
 * triangle's plane at (pos.x, pos.z) is computed. At or below pos.y it
 * replaces zssp->lower when lower is empty or y is higher than lower.onpos;
 * above pos.y it replaces zssp->upper when upper is empty or y is not
 * higher than upper.onpos. A replaced unit gets findflag 1, the object's
 * attribute, onpos = y, the unit normal and the angles
 * angz = -atan2(n.x, n.y) * 10430, angx = asin-like(n.z) * 10430, and the
 * module flag D_8C02D54C (returned by func_8C02D2E0) is set to 1.
 * Differences from the compiled sibling func_8C029400 (code_8C029400.c):
 * strict "> 0" edge tests with only two sign products, the plane offset
 * through FIPR, FSRRA normalisation without a zero-length check, and the
 * angle factor 10430.0f (0x4622F800) negated before the FTRC.
 */

/* one probe result (SADX xssunit); same layout as in code_8C02AD22.c */
typedef struct xssunit {
    Sint32 findflag;            /* 0x00 1 = polygon found */
    Sint32 objatt;              /* 0x04 attribute of the land object */
    Angle angx;                 /* 0x08 */
    Angle angz;                 /* 0x0C */
    Float onpos;                /* 0x10 polygon height at pos */
    NJS_VECTOR normal;          /* 0x14 */
} xssunit;                      /* 0x20 */

/* probe position + two results (SADX zxsdwstr); same layout as in
 * code_8C02AD22.c */
typedef struct zxsdwstr {
    NJS_POINT3 pos;             /* 0x00 */
    xssunit lower;              /* 0x0C highest polygon at or below pos.y */
    xssunit upper;              /* 0x2C lowest polygon above pos.y */
} zxsdwstr;                     /* 0x4C */

SA1_ASSERT_SIZE(xssunit, 0x20);
SA1_ASSERT_OFFSET(xssunit, onpos, 0x10);
SA1_ASSERT_OFFSET(xssunit, normal, 0x14);
SA1_ASSERT_SIZE(zxsdwstr, 0x4C);
SA1_ASSERT_OFFSET(zxsdwstr, lower, 0x0C);
SA1_ASSERT_OFFSET(zxsdwstr, upper, 0x2C);

extern Sint32 D_8C02D54C;       /* 1 = a unit was set by the current probe; data word in the code section */
extern Float D_8C02D964;        /* 10430.0f (0x4622F800), radians -> Angle; module constant */
extern Float D_8C02D968;        /* 0.0001f (0x38D1B717), minimum n.y^2; module constant */

/* atan2-like libm wrapper (FR4 y, FR5 x -> FR0); same body as atan2f
 * 8C10D0F8, both call the core 8C10E6A0 and set errno B_8C7AC108 */
extern Float func_8C10D0A8(Float y, Float x);

#ifdef NON_MATCHING
/* Same prototype as the declaration in code_8C02D060.c (its caller
 * func_8C02D2E0 passes FDOBJ.attr, a Sint32). Because the original takes its
 * inputs in R12/R14/R13, the harness can only check this body through a
 * native caller (route func_8C02D2E0 with code_8C02D060.c native): routing it
 * alone hands it the caller's R4-R6 (zssp, object, garbage) instead. */
void func_8C02D410(NJS_POINT3 *v, zxsdwstr *zssp, Sint32 attr)
{
    Float px;
    Float pz;
    Float c0;
    Float c1;
    Float c2;
    Float e1x;
    Float e1y;
    Float e1z;
    Float e2x;
    Float e2y;
    Float e2z;
    Float nx;
    Float ny;
    Float nz;
    Float d;
    Float y;
    Float r;
    xssunit *hp;
    float a[4];
    float b[4];

    /* func_8C02D2B2: XZ edge functions of the edges v0-v1, v1-v2, v2-v0 */
    px = zssp->pos.x;
    pz = zssp->pos.z;
    c0 = (px - v[0].x) * (v[1].z - v[0].z) - (pz - v[0].z) * (v[1].x - v[0].x);
    c1 = (px - v[1].x) * (v[2].z - v[1].z) - (pz - v[1].z) * (v[2].x - v[1].x);
    if (!(c0 * c1 > 0.0f)) {
        return;
    }
    c2 = (px - v[2].x) * (v[0].z - v[2].z) - (pz - v[2].z) * (v[0].x - v[2].x);
    if (!(c1 * c2 > 0.0f)) {
        return;
    }

    /* func_8C02D286: n = (v1 - v0) x (v2 - v0) */
    e1x = v[1].x - v[0].x;
    e1y = v[1].y - v[0].y;
    e1z = v[1].z - v[0].z;
    e2x = v[2].x - v[0].x;
    e2y = v[2].y - v[0].y;
    e2z = v[2].z - v[0].z;
    nx = e1y * e2z - e1z * e2y;
    ny = e1z * e2x - e1x * e2z;
    nz = e1x * e2y - e1y * e2x;
    if (!(ny * ny > D_8C02D968)) {
        return;
    }

    /* plane n.p + d = 0 through v0 (FIPR FV8,FV0), solved for y */
    a[0] = v[0].x;
    a[1] = v[0].y;
    a[2] = v[0].z;
    a[3] = 0.0f;
    b[0] = nx;
    b[1] = ny;
    b[2] = nz;
    b[3] = 0.0f;
    d = -_builtin_fipr(b, a);
    y = -(zssp->pos.x * nx + zssp->pos.z * nz + d) / ny;

    if (y > zssp->pos.y) {
        hp = &zssp->upper;
        if (hp->findflag != 0 && y > hp->onpos) {
            return;
        }
    } else {
        hp = &zssp->lower;
        if (hp->findflag != 0 && !(y > hp->onpos)) {
            return;
        }
    }

    D_8C02D54C = 1;
    hp->findflag = 1;
    hp->objatt = attr;
    hp->onpos = y;

    /* func_8C02D278: n *= 1 / sqrt(n.n) (FIPR FV4,FV4 + FSRRA) */
    r = _builtin_fsrra(_builtin_fipr(b, b));
    nz *= r;
    ny *= r;
    nx *= r;
    hp->normal.x = nx;
    hp->normal.y = ny;
    hp->normal.z = nz;
    hp->angz = SA1_FTOI(-(func_8C10D0A8(nx, ny) * D_8C02D964));
    hp->angx = SA1_FTOI(func_8C10D058(hp->normal.z) * D_8C02D964);
}
#endif
