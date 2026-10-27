/*
 * STG01 0C90A282: Emerald Coast ocean ripples (chunk 0C909F20-0C90A820,
 * functions 5-8 of 8).
 *
 * The ocean task (func_0C909F20 init, func_0C909FDA exec) keeps up to 30
 * expanding ring waves in D_0CAB1B04 (count in twp->counter.l). Every frame
 * the exec zeroes the height (y) of the 26x26 vertex grid of the ocean morph
 * model (STG01_Ocean_Morph_Effect.model->points) and calls func_0C90A3EE for
 * each wave, which grows the ring and adds its height profile to the grid
 * points it covers (func_0C90A56C).
 *
 *  func_0C90A282  add a wave at (x, z): radius and width * 65536/150, given
 *                 amplitude, 200 frames of life (caller func_0C90878C)
 *  func_0C90A2CA  water height at (x, z): -1.5 + the ring profiles
 *                 2 * amp * sin((dist - rad) * 150) (caller func_0C908794)
 *  func_0C90A3EE  per-frame update of one wave: radius + 8, amplitude
 *                 * 0.985, life - 1; deforms the ring's bounding square, or
 *                 when the inner circle is not empty the four bands around
 *                 the square inscribed in it (half side (rad - width) *
 *                 0.707106, side bands shortened by 40 = one grid cell)
 *  func_0C90A56C  deform the grid points of the band [x0,x1] x [z0,z1]
 *                 relative to the ocean origin (40 units per cell, cell 13
 *                 at the origin; floor/ceil through func_8C10D198/D148)
 *
 * Status: func_0C90A282 and func_0C90A56C match.
 *  - func_0C90A2CA (NON_MATCHING): one extra instruction. The original
 *    computes "(dist - rad) * 150" in place in dist's register (FSUB FR14,FR15;
 *    FMUL FR3,FR15; FTRC FR15); SHC 5.1 here copies to FR2 for the FMUL.
 *    "dist -= rad; dist *= 150.0f;" (the form that matches in func_0C90A56C)
 *    gives the in-place FMUL but then sum gets FR12 instead of the hoisted
 *    2.0f constant (and is no longer kept at @R15); "volatile Float sum"
 *    restores that allocation but swaps the scratch registers of dx/dz
 *    (FR4/FR5). Kept: the dx/dz two-step loads (they put dx/dz in FR4/FR5 and
 *    let SHC hoist 2.0f into FR12, as in the original), "sum += ..." as
 *    SA1_FMAC_ACC. Also tried: inline distance expression, #pragma inline
 *    helper, a constant variable for 2.0f, Angle/Float temporaries, in-call
 *    assignments, operand orders.
 *  - func_0C90A3EE (NON_MATCHING): same instruction count (191), 33 differing:
 *    the box values computed before the branch come out in the order x1, x0,
 *    z0, z1 (original: x0, z0, x1, z1, with x1 still at @0 and x0 at @4), so
 *    the register that holds the value re-stored at the start of the 4-band
 *    branch is x0 (original: z0), and the inner square values/CSE temporaries
 *    get permuted stack slots (original: iz1 @24, iz0 @28, iz1-40 @32,
 *    iz0+40 @36, ix1 @40, ix0 @44). Kept: the Float pointer walk over the
 *    wave (FMOV.S @R4+ loads, x/z/amp/rad in FR13/FR14/FR12/FR15), the box
 *    assigned in both branches (SHC hoists it: the "z0 stored again in each
 *    branch" pattern and all reloads of the original), "iz0 + 40.0f" /
 *    "iz1 - 40.0f" as repeated expressions (CSE temporaries). Swept: all
 *    orders of the box assignments in the first branch and of the inner
 *    square, declaration orders, inline/variable mixes, rin > 0 first,
 *    temporaries assigned after the first call.
 */
#include "common.h"
#include "sa1/sa1.h"

/*
 * One ring wave (D_0CAB1B04[30], 0x18 bytes each). Six Floats, no padding on
 * any target: func_0C90A3EE reads them with a walking Float pointer, as the
 * original does (FMOV.S @R4+).
 */
typedef struct {
    Float x;        /* 0x00 centre (world) */
    Float z;        /* 0x04 */
    Float amp;      /* 0x08 amplitude, * 0.985 per frame */
    Float rad;      /* 0x0C ring radius, + 8 per frame */
    Float width;    /* 0x10 ring width */
    Float life;     /* 0x14 frames left (200 at creation); func_0C909FDA drops waves at <= 0 */
} OCEANWAVE;
SA1_ASSERT_SIZE(OCEANWAVE, 0x18);
SA1_ASSERT_OFFSET(OCEANWAVE, z, 0x04);
SA1_ASSERT_OFFSET(OCEANWAVE, amp, 0x08);
SA1_ASSERT_OFFSET(OCEANWAVE, rad, 0x0C);
SA1_ASSERT_OFFSET(OCEANWAVE, width, 0x10);
SA1_ASSERT_OFFSET(OCEANWAVE, life, 0x14);

extern OCEANWAVE D_0CAB1B04[];      /* 30 waves; count in the ocean task's twp->counter.l */
extern Float D_0CAB1AF4;            /* ocean origin x (written by func_0C909FDA) */
extern Float D_0CAB1AF8;            /* ocean origin z */
extern Sint32 D_0CAB1B00;           /* grid points visited this frame (cleared by func_0C909FDA) */

/*
 * D_0CAA1330 is the model field of STG01_Ocean_Morph_Effect (0CAA132C, an
 * NJS_OBJECT); the matching build loads it through its own symbol.
 */
extern NJS_OBJECT STG01_Ocean_Morph_Effect;
#if defined(TARGET_PC)
#define D_0CAA1330 (STG01_Ocean_Morph_Effect.model)
#else
extern NJS_MODEL *D_0CAA1330;
#endif

void func_0C90A56C(Float rad, Float width, Float amp, Float cx, Float cz,
                   Float x0, Float z0, Float x1, Float z1);

void func_0C90A282(task *tp, Float x, Float z, Float rad, Float width, Float amp)
{
    taskwk *twp;
    OCEANWAVE *wp;
    Float k;
    Sint32 n;

    twp = tp->twp;
    k = 436.90665f;                 /* 65536 / 150 */
    if ((Uint32)twp->counter.l < 30) {
        n = twp->counter.l++;
        wp = &D_0CAB1B04[n];
        wp->x = x;
        wp->z = z;
        wp->amp = amp;
        wp->rad = rad * k;
        wp->width = width * k;
        wp->life = 200.0f;
    }
}

#ifdef NON_MATCHING
Float func_0C90A2CA(task *tp, Float x, Float z)
{
    OCEANWAVE *wp;
    Float sum;
    Float dist;
    Float rad, width, lo;
    Float dx, dz;
    Sint32 i, n;
    taskwk *twp;

    twp = tp->twp;
    sum = -1.5f;
    n = twp->counter.l;
    for (i = 0; i < n; i++) {
        wp = &D_0CAB1B04[i];
        dx = wp->x;
        dz = wp->z;
        rad = wp->rad;
        width = wp->width;
        dx = x - dx;
        dz = z - dz;
        dist = SA1_FMAC(dx, dx, dz * dz);
        dist = njSqrt(dist);
        if (width > rad) {
            lo = 0.0f;
        } else {
            lo = rad - width;
        }
        if (dist > lo && rad > dist) {
            dist -= rad;
            SA1_FMAC_ACC(sum, njSin(SA1_FTOI(dist * 150.0f)) * wp->amp, 2.0f);
        }
    }
    return sum;
}
#else
ASM_STUB(Float func_0C90A2CA(task *tp, Float x, Float z))
#endif

#ifdef NON_MATCHING
void func_0C90A3EE(OCEANWAVE *wp)
{
    Float x, z, amp, rad, width, life;
    Float x0, z0, x1, z1;
    Float rin, d;
    Float ix0, iz0, ix1, iz1;
    Float *p;

    p = &wp->x;
    x = *p++;
    z = *p++;
    amp = *p++;
    rad = *p++;
    width = *p++;
    life = *p;
    x -= D_0CAB1AF4;
    z -= D_0CAB1AF8;
    rad += 8.0f;
    rin = rad - width;
    if (rin <= 0.0f) {
        x1 = x + rad;
        x0 = x - rad;
        z0 = z - rad;
        z1 = z + rad;
        func_0C90A56C(rad, width, amp, x, z, x0, z0, x1, z1);
    } else {
        x0 = x - rad;
        z0 = z - rad;
        x1 = x + rad;
        z1 = z + rad;
        d = rin * 0.707106f;
        ix0 = x - d;
        iz0 = z - d;
        ix1 = x + d;
        iz1 = z + d;
        func_0C90A56C(rad, width, amp, x, z, x0, z0, x1, iz0);
        func_0C90A56C(rad, width, amp, x, z, x0, iz0 + 40.0f, ix0, iz1 - 40.0f);
        func_0C90A56C(rad, width, amp, x, z, ix1, iz0 + 40.0f, x1, iz1 - 40.0f);
        func_0C90A56C(rad, width, amp, x, z, x0, iz1, x1, z1);
    }
    life -= 1.0f;
    amp *= 0.98499997f;
    wp->amp = amp;
    wp->rad = rad;
    wp->life = life;
}
#else
ASM_STUB(void func_0C90A3EE(OCEANWAVE *wp))
#endif

/*
 * The bounds are turned into grid cells in place (x0..z1 are reused; the
 * original keeps each new value in its own stack slot, z1 in its argument
 * slot): x0/z0 = floor(v / 40) + 13 (0 below -500), x1/z1 = ceil(v / 40) + 13
 * (26 above 500); a band entirely outside +-500 does nothing.
 */
void func_0C90A56C(Float rad, Float width, Float amp, Float cx, Float cz,
                   Float x0, Float z0, Float x1, Float z1)
{
    NJS_POINT3 *points;
    NJS_POINT3 *p;
    Float i, j, row, rin, dist;
    Float dx, dz;
    Sint32 idx;

    if (x0 > 500.0f) {
        return;
    } else if (x0 < -500.0f) {
        x0 = 0.0f;
    } else {
        x0 *= 0.024999999f;
        x0 = func_8C10D198(x0) + 13.0f;
    }
    if (x1 < -500.0f) {
        return;
    } else if (x1 > 500.0f) {
        x1 = 26.0f;
    } else {
        x1 *= 0.024999999f;
        x1 = func_8C10D148(x1) + 13.0f;
    }
    if (z0 > 500.0f) {
        return;
    } else if (z0 < -500.0f) {
        z0 = 0.0f;
    } else {
        z0 *= 0.024999999f;
        z0 = func_8C10D198(z0) + 13.0f;
    }
    if (z1 < -500.0f) {
        return;
    } else if (z1 > 500.0f) {
        z1 = 26.0f;
    } else {
        z1 *= 0.024999999f;
        z1 = func_8C10D148(z1) + 13.0f;
    }
    points = D_0CAA1330->points;
    row = x0 * 26.0f;
    rin = (width > rad) ? 0.0f : rad - width;
    for (i = x0; i < x1; i += 1.0f) {
        for (j = z0; j < z1; j += 1.0f) {
            idx = SA1_FTOI(j + row);
            if (idx >= 0 && idx < 676) {
                p = &points[idx];
                dx = p->x - cx;
                dz = p->z - cz;
                dist = SA1_FMAC(dx, dx, dz * dz);
                dist = njSqrt(dist);
                if ((dist > rin) & (dist <= rad)) {
                    dist -= rad;
                    dist *= 150.0f;
                    SA1_FMAC_ACC(p->y, njSin(SA1_FTOI(dist)) * amp, 2.0f);
                }
                D_0CAB1B00++;
            }
        }
        row += 26.0f;
    }
}
