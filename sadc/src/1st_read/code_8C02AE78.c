/*
 * 1ST_READ 8C02AE78: small vector geometry helpers (triangle normal,
 * point-to-segment projection). Part of the land-collision module
 * 8C027360-8C02CE80; func_8C02AE78 is the same code as func_8C027360.
 *
 * The original reads x/y/z through post-incremented pointers (FMOV.S @Rn+),
 * which SHC only produces from explicit Float pointer walks, hence the
 * Float * parameters of func_8C02AE78 (NJS_POINT3 is three packed Floats on
 * every target).
 */
#include "common.h"
#include "ninja.h"

/*
 * Unnormalised normal of the triangle (p0, p1, p2), Float[3] each:
 * n = (p1 - p0) x (p0 - p2). Called from func_8C0318C0.
 * The statement order (loads of p1 and the subtractions interleaved) is
 * needed for the FR4/FR6 assignment of the p0 - p2 components.
 */
void func_8C02AE78(Float *p0, Float *p1, Float *p2, Float *n)
{
    Float ax, ay, az;
    Float bx, by, bz;

    bx = *p0++;
    by = *p0++;
    bz = *p0;
    ax = *p1++;
    ax -= bx;
    ay = *p1++;
    bx -= *p2++;
    az = *p1;
    ay -= by;
    az -= bz;
    by -= *p2++;
    bz -= *p2;
    n[0] = az * by - ay * bz;
    n[1] = ax * bz - az * bx;
    n[2] = ay * bx - ax * by;
}

/*
 * Project p onto the segment a-b. Returns 0 when the projection falls
 * outside the segment (before a or beyond b); otherwise stores the foot of
 * the perpendicular in *foot, the squared distance from p to it in *dist2
 * and returns 1. A degenerate segment (every component of (b - a) squared
 * below 0.0001) gives |p - a|^2 and leaves *foot untouched.
 * Every 3-component sum is y * y' (FMUL), then two FMACs: x * x' and z * z'.
 * SA1_FMAC marks the inner one; the outer one is FMAC_ADD (sum + z * z' for
 * SHC: SA1_FMAC(z, z, sum) reorders the addition and changes the SHC output).
 */
/* c + a * b at an FMAC site, addend evaluated first; fused like SA1_FMAC on PC */
#if defined(TARGET_PC)
#define FMAC_ADD(c, a, b) SA1_FMAC((a), (b), (c))
#else
#define FMAC_ADD(c, a, b) ((c) + (a) * (b))
#endif

Sint32 func_8C02AEBA(NJS_POINT3 *a, NJS_POINT3 *b, NJS_POINT3 *p, NJS_POINT3 *foot, Float *dist2)
{
    NJS_VECTOR ab;
    NJS_VECTOR ap;
    NJS_VECTOR pb;
    NJS_VECTOR pf;
    Float t;

    ab.x = b->x - a->x;
    ab.y = b->y - a->y;
    ab.z = b->z - a->z;
    ap.x = p->x - a->x;
    ap.y = p->y - a->y;
    ap.z = p->z - a->z;
    pb.x = b->x - p->x;
    pb.y = b->y - p->y;
    pb.z = b->z - p->z;
    if (FMAC_ADD(SA1_FMAC(ab.x, ap.x, ab.y * ap.y), ab.z, ap.z) < 0.0f
        || FMAC_ADD(SA1_FMAC(ab.x, pb.x, ab.y * pb.y), ab.z, pb.z) < 0.0f) {
        return 0;
    }
    /* 9.9999992e-05f: 0x38D1B716, 0.0001f truncated one ulp lower */
    if (ab.x * ab.x < 9.9999992e-05f && ab.y * ab.y < 9.9999992e-05f
        && ab.z * ab.z < 9.9999992e-05f) {
        *dist2 = FMAC_ADD(SA1_FMAC(ap.x, ap.x, ap.y * ap.y), ap.z, ap.z);
    } else {
        t = FMAC_ADD(SA1_FMAC(ab.x, p->x - a->x, ab.y * (p->y - a->y)), ab.z, p->z - a->z)
            / FMAC_ADD(SA1_FMAC(ab.x, ab.x, ab.y * ab.y), ab.z, ab.z);
        foot->x = SA1_FMAC(ab.x, t, a->x);
        foot->y = SA1_FMAC(ab.y, t, a->y);
        foot->z = SA1_FMAC(ab.z, t, a->z);
        pf.x = foot->x - p->x;
        pf.y = foot->y - p->y;
        pf.z = foot->z - p->z;
        *dist2 = FMAC_ADD(SA1_FMAC(pf.x, pf.x, pf.y * pf.y), pf.z, pf.z);
    }
    return 1;
}
