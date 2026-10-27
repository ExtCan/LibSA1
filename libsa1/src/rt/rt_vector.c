/*
 * rt_vector.c - Ninja vector maths, distances, line/plane intersections and
 * the sphere test (docs/libsa1_integration.md 4.2, "FMAC sites exact").
 *
 * Clean-room: written from the documented behaviour and the operation order
 * of the SDK functions (one C operation per SH-4 FPU instruction, in the
 * original order), on top of the shared FPU models of rt.h; verified
 * bit-exact against the original SDK code in the harness (README.md):
 *     sa1h_<tag>.exe diff njUnitVector 2000 1 --warm --ulps 0   (and the others)
 *
 * Conventions used throughout:
 *   - every FMAC of the original is one RT_FMAC (exact product, one rounding;
 *     the FMAC count of each function is given in its comment), every FIPR
 *     one RT_FIPR with the w components the original loads (FLDI0 = +0);
 *     FMUL / FSUB / FADD / FDIV are plain C operators on Float (single
 *     precision, the game's RZ/FTZ/DAZ mode), except where the result is
 *     stored or returned and can be a NaN: there RT_FMUL ... give the SH-4
 *     default NaN 0x7FBFFFFF (the FMAC/FIPR/FSRRA/FSQRT models canonicalise
 *     too, so a NaN in an intermediate never reaches memory with x86 bits);
 *   - memory is read and written in the original order relative to the
 *     stores, so aliased arguments (njAddVector(v, v), an output vector that
 *     is also an input, NJS_LINE == NJS_PLANE ...) behave like the original:
 *     the vector functions load everything before the first store, the
 *     intersection helpers re-read their inputs after each output store;
 *   - nothing here reads or writes XMTRX or the caller's stack. The
 *     originals leave other values in caller-saved registers (njUnitVector:
 *     the scaled components in FR1/FR2/FR4 and FSRRA in FR3; njScalor /
 *     njScalor2: v in FR4-FR6 and v . v in FR7; njOuterProduct: the product
 *     in FR8-FR10; njDistanceP2P: the squared distance in FR4; the Sint32
 *     functions a float in FR0; T, R0 ...). Only the C return value is part
 *     of the interface (the harness adapters set FR0 or R0, nothing else).
 *     All 1,932 call sites (891 in 1ST_READ, 1,041 in the overlays; all
 *     SHC-compiled, no hand-written asm caller) were scanned for a read of a
 *     caller-saved register after the call before any write. Two exist, both
 *     an uninitialised local of the original on a zero-length path:
 *       func_8C055E40 (code_8C055D20.c) reads FR4 after njDistanceP2P when
 *         the distance is 0: the original gets the squared distance, +0;
 *         the decompiled C sets r = 0 (PC_ONLY), which is the same value;
 *       func_8C0ED0BA (not decompiled yet) reads FR6 after njScalor when the
 *         length is 0: the original gets v->z (+-0 or a denormal, DAZ), so
 *         its "FR6 > 1" and "FR6 > 0" tests are both false; its C has to
 *         take that branch instead of reading an uninitialised local.
 *     An interpreted caller of a routed native function reads its own stale
 *     register there instead (harness only; libsa1 has no such caller).
 *     8C63FEAC's FR0 (njDistanceP2L's result or 0) is read by none of its 6
 *     callers (the game declares it void).
 *
 * Edge semantics kept (design 4.5): njUnitVector of a zero (or all-denormal,
 * DAZ) vector stores NaN for zero components, +-inf for the others and
 * returns NaN (0 * inf); njDistanceP2L / 8C63FEAC / 8C63FF84 / 8C64016C
 * divide without a zero check (inf / NaN results as on the DC).
 *
 * 32-bit x86 note: under the i386 C ABI a Float return value travels in x87
 * ST(0), and loading the SH-4 NaN 0x7FBFFFFF (a signalling NaN in x86 terms)
 * into ST(0) quiets it to 0x7FFFFFFF. Everything stored to memory is exact;
 * only a NaN *return value* changes bits on 32-bit x86 ("diff" counts it as
 * approx: njUnitVector, njDistanceP2L, 8C63FF84 and 8C64016C, whose random
 * inputs often hold zero vectors; "call --route njUnitVector <zero vector>"
 * gives FR0 7FFFFFFF where the original gives 7FBFFFFF). x86_64 returns in
 * XMM0 and is exact.
 *
 * njCollisionCheckSS (8C63FE54) runs in none of the sample traces: it is
 * verified by "diff" alone.
 */
#include "rt.h"

#if defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#endif

/* internal helpers of the SDK (never called by game code); external under
 * their addresses so the harness can diff and route them alone */
Float func_8C63A9DC(Float x);
Sint32 func_8C6505F0(const NJS_LINE *l, const NJS_PLANE *pl);
Sint32 func_8C650614(const NJS_PLANE *pl1, const NJS_PLANE *pl2);

/* ------------------------------------------------------------------ */
/* vectors                                                             */

/* 8C638EAC: v1 += v2 (3 FADD; all loads before the stores z, y, x) */
void njAddVector(NJS_VECTOR *v1, const NJS_VECTOR *v2)
{
    Float x1 = v1->x, x2 = v2->x;
    Float y1 = v1->y, y2 = v2->y;
    Float z1 = v1->z, z2 = v2->z;
    Float x = RT_FADD(x2, x1);
    Float y = RT_FADD(y2, y1);
    Float z = RT_FADD(z2, z1);

    v1->z = z;
    v1->y = y;
    v1->x = x;
}

/* 8C63A7E4: v1 -= v2 (3 FSUB; all loads before the stores z, y, x) */
void njSubVector(NJS_VECTOR *v1, const NJS_VECTOR *v2)
{
    Float x2 = v2->x, x1 = v1->x;
    Float y2 = v2->y, y1 = v1->y;
    Float z2 = v2->z, z1 = v1->z;
    Float x = RT_FSUB(x1, x2);
    Float y = RT_FSUB(y1, y2);
    Float z = RT_FSUB(z1, z2);

    v1->z = z;
    v1->y = y;
    v1->x = x;
}

/* 8C639090: v1 . v2, one FIPR FV4,FV0 with w = +0 in both */
Float njInnerProduct(const NJS_VECTOR *v1, const NJS_VECTOR *v2)
{
    Float a[4], b[4];

    a[0] = v1->x;
    a[1] = v1->y;
    a[2] = v1->z;
    a[3] = 0.0f;
    b[0] = v2->x;
    b[1] = v2->y;
    b[2] = v2->z;
    b[3] = 0.0f;
    return RT_FIPR(b, a);
}

/* 8C639B50: ov = v1 x v2, returns |ov| (3 FMUL, 3 FMAC, FIPR, FSQRT).
 * Each component is FMAC(-a, b, c * d) and is stored as soon as it is
 * computed; all inputs are loaded before the first store, so ov may alias
 * v1 or v2. The length comes from the register values, not from memory. */
Float njOuterProduct(const NJS_VECTOR *v1, const NJS_VECTOR *v2, NJS_VECTOR *ov)
{
    Float x1 = v1->x, y1 = v1->y, z1 = v1->z;
    Float x2 = v2->x, y2 = v2->y, z2 = v2->z;
    Float x, y, z;
    Float a[4], b[4];

    x = RT_FMAC(RT_FNEG(z1), y2, y1 * z2);
    ov->x = x;
    y = RT_FMAC(x1, RT_FNEG(z2), z1 * x2);
    ov->y = y;
    z = RT_FMAC(y1, RT_FNEG(x2), x1 * y2);
    ov->z = z;

    a[0] = x;
    a[1] = y;
    a[2] = z;
    a[3] = 0.0f;
    b[0] = x;
    b[1] = y;
    b[2] = z;
    b[3] = 0.0f;
    return RT_FSQRT(RT_FIPR(a, b));
}

/* 8C63A748: v . v (FIPR, w = +0) */
Float njScalor2(const NJS_VECTOR *v)
{
    Float a[4];

    a[0] = v->x;
    a[1] = v->y;
    a[2] = v->z;
    a[3] = 0.0f;
    return RT_FIPR(a, a);
}

/* 8C63A75C: |v| = FSQRT(FIPR(v, v)) */
Float njScalor(const NJS_VECTOR *v)
{
    Float a[4];

    a[0] = v->x;
    a[1] = v->y;
    a[2] = v->z;
    a[3] = 0.0f;
    return RT_FSQRT(RT_FIPR(a, a));
}

/* 8C63A94C: v *= FSRRA(v . v), returns (v . v) * FSRRA(v . v), i.e. the old
 * length. A zero vector (dot product +0, also after DAZ/FTZ) gives
 * FSRRA = +inf: components 0 * inf = NaN or x * inf = +-inf, result NaN. */
Float njUnitVector(NJS_VECTOR *v)
{
    Float a[4];
    Float d, r, x, y, z;

    a[0] = v->x;
    a[1] = v->y;
    a[2] = v->z;
    a[3] = 0.0f;
    d = RT_FIPR(a, a);
    r = RT_FSRRA(d);
    z = RT_FMUL(a[2], r);
    y = RT_FMUL(a[1], r);
    x = RT_FMUL(a[0], r);
    v->z = z;
    v->y = y;
    v->x = x;
    return RT_FMUL(d, r);
}
/* ------------------------------------------------------------------ */
/* distances                                                           */

/* 8C640128: |p1 - p2| (3 FSUB, FMUL + 2 FMAC, tail call of njSqrt) */
Float njDistanceP2P(const NJS_POINT3 *p1, const NJS_POINT3 *p2)
{
    Float dy = p1->y - p2->y;
    Float dx = p1->x - p2->x;
    Float dz = p1->z - p2->z;
    Float d = dy * dy;

    d = RT_FMAC(dx, dx, d);
    d = RT_FMAC(dz, dz, d);
    return njSqrt(d);
}

/* signed FSRRA (8C63A9DC): x < 0 gives -FSRRA(|x|), anything else FSRRA(x)
 * (so -0 -> -inf, NaN -> NaN) */
SA1_INLINE Float rt_fsrra_signed(Float x)
{
    if (0.0f > x) {
        return RT_FNEG(RT_FSRRA(RT_FABS(x)));
    }
    return RT_FSRRA(x);
}

/* 8C63A9DC: FR4 -> FR0, called only by njDistanceP2L */
Float func_8C63A9DC(Float x)
{
    return rt_fsrra_signed(x);
}

/* 8C640080: distance of p from the plane through l->p with normal l->v,
 * |v . (p - l->p)| / |v| computed as |v . p - v . l->p| * FSRRA(v . v)
 * (signed helper 8C63A9DC); with cp != NULL also the foot point
 * cp = p - v * (v . p - v . l->p) / (v . v). FMUL + 2 FMAC (v . p),
 * FMUL + 2 FMAC (v . v), 3 FMAC (cp), FDIV. cp is written component by
 * component, each from p and l re-read after the previous store (as the
 * original), so cp may alias p. */
Float njDistanceP2L(const NJS_POINT3 *p, const NJS_LINE *l, NJS_POINT3 *cp)
{
    Float vx = l->vx, vy = l->vy, vz = l->vz;
    Float d, num, len2, t;

    d = RT_FNEG(vx * l->px);
    d = d - vy * l->py;
    d = d - vz * l->pz;                 /* -(v . l->p) */
    num = vy * p->y;
    num = RT_FMAC(vx, p->x, num);
    num = RT_FMAC(vz, p->z, num);
    num = num + d;                      /* v . p - v . l->p */
    len2 = vy * vy;
    len2 = RT_FMAC(vx, vx, len2);
    len2 = RT_FMAC(vz, vz, len2);       /* v . v */
    if (cp != NULL) {
        t = RT_FNEG(num) / len2;
        cp->x = RT_FMAC(t, l->vx, p->x);
        cp->y = RT_FMAC(t, l->vy, p->y);
        cp->z = RT_FMAC(t, l->vz, p->z);
    }
    return RT_FMUL(RT_FABS(num), rt_fsrra_signed(len2));
}

/* 8C63FF84: distance of p from the line l (point l->p, direction l->v):
 * t = v . (p - l->p) / (v . v); with cp != NULL the foot point
 * cp = l->p + t * v is stored and the distance is |p - cp| with cp read back
 * from memory, else |(p - v * t) - l->p|. Tail call of njSqrt.
 * FMUL + 2 FMAC (num), FMUL + 2 FMAC (v . v), FDIV, then 3 FMAC (cp) and
 * FMUL + 2 FMAC (distance), or 3 FMUL + 6 FSUB and FMUL + 2 FMAC. */
Float func_8C63FF84(NJS_POINT3 *p, NJS_LINE *l, NJS_POINT3 *cp)
{
    Float vx = l->vx, vy = l->vy, vz = l->vz;
    Float dx, dy, dz, num, len2, t, d, ex, ey, ez;

    dy = p->y - l->py;
    dx = p->x - l->px;
    num = vy * dy;
    num = RT_FMAC(vx, dx, num);
    dz = p->z - l->pz;
    len2 = vy * vy;
    num = RT_FMAC(vz, dz, num);
    len2 = RT_FMAC(vx, vx, len2);
    len2 = RT_FMAC(vz, vz, len2);
    t = num / len2;
    if (cp != NULL) {
        cp->x = RT_FMAC(t, l->vx, l->px);
        cp->y = RT_FMAC(t, l->vy, l->py);
        cp->z = RT_FMAC(t, l->vz, l->pz);
        ey = p->y - cp->y;
        ex = p->x - cp->x;
        d = ey * ey;
        d = RT_FMAC(ex, ex, d);
        ez = p->z - cp->z;
        d = RT_FMAC(ez, ez, d);
    } else {
        ex = (p->x - l->vx * t) - l->px;
        ey = (p->y - l->vy * t) - l->py;
        ez = (p->z - l->vz * t) - l->pz;
        d = ey * ey;
        d = RT_FMAC(ex, ex, d);
        d = RT_FMAC(ez, ez, d);
    }
    return njSqrt(d);
}

/* ------------------------------------------------------------------ */
/* line / plane intersections                                          */

/* 8C6505F0: l->v . pl->v == 0 (FMUL + 2 FMAC, FCMP/EQ: -0 counts as 0, NaN
 * does not), i.e. the line is parallel to the plane */
Sint32 func_8C6505F0(const NJS_LINE *l, const NJS_PLANE *pl)
{
    Float s = l->vy * pl->vy;

    s = RT_FMAC(pl->vx, l->vx, s);
    s = RT_FMAC(pl->vz, l->vz, s);
    return s == 0.0f;
}

/* 8C650614: |pl1->v x pl2->v|^2 == 0 (6 FMUL, 3 FSUB, FMUL + 2 FMAC,
 * FCMP/EQ), i.e. the planes are parallel */
Sint32 func_8C650614(const NJS_PLANE *pl1, const NJS_PLANE *pl2)
{
    Float ax = pl1->vx, ay = pl1->vy, az = pl1->vz;
    Float bx = pl2->vx, by = pl2->vy, bz = pl2->vz;
    Float cz = ax * by - ay * bx;
    Float p_yz = ay * bz;
    Float p_zy = az * by;
    Float p_xz = ax * bz;
    Float cx = p_yz - p_zy;
    Float p_zx = az * bx;
    Float s = cx * cx;
    Float cy;

    s = RT_FMAC(cz, cz, s);
    cy = p_zx - p_xz;
    s = RT_FMAC(cy, cy, s);
    return s == 0.0f;
}

/* 8C63FEAC: intersection of the line a (NJS_LINE: point, direction) with the
 * plane b (NJS_PLANE: point, normal) into c (NJS_POINT3, may be NULL).
 *   parallel (8C6505F0): njDistanceP2L(copy of a->p, b, c), i.e. c = the
 *     projection of the line's point onto the plane;
 *   else (c != NULL): t = -(b->v . a->p - b->v . b->p) / (b->v . a->v),
 *     c = a->p + t * a->v, each component re-reading a after the previous
 *     store. FMUL + 2 FMAC, FMUL + 2 FMAC, FDIV, 3 FMAC.
 * The original also leaves njDistanceP2L's result or 0 in FR0; the game
 * declares it void and none of its 6 call sites reads FR0. */
void func_8C63FEAC(void *a, void *b, void *c)
{
    const NJS_LINE *ln = (const NJS_LINE *)a;
    const NJS_PLANE *pl = (const NJS_PLANE *)b;
    NJS_POINT3 *cp = (NJS_POINT3 *)c;
    Float vx, vy, vz, d, s, den, t, ax, ay, az;

    if (func_8C6505F0(ln, pl) != 0) {
        NJS_POINT3 pos;

        pos.x = ln->px;
        pos.y = ln->py;
        pos.z = ln->pz;
        njDistanceP2L(&pos, pl, cp);
        return;
    }
    if (cp == NULL) {
        return;
    }
    vx = pl->vx;
    vy = pl->vy;
    vz = pl->vz;
    d = RT_FNEG(vx * pl->px);
    d = d - vy * pl->py;
    d = d - vz * pl->pz;                /* -(b->v . b->p) */
    s = vy * ln->py;
    s = RT_FMAC(vx, ln->px, s);
    s = RT_FMAC(vz, ln->pz, s);         /* b->v . a->p */
    s = s + d;
    ax = ln->vx;
    ay = ln->vy;
    az = ln->vz;
    den = vy * ay;
    den = RT_FMAC(vx, ax, den);
    den = RT_FMAC(vz, az, den);         /* b->v . a->v */
    t = RT_FNEG(s) / den;
    cp->x = RT_FMAC(t, ax, ln->px);
    cp->y = RT_FMAC(t, ln->vy, ln->py);
    cp->z = RT_FMAC(t, ln->vz, ln->pz);
}

/* 8C64016C: intersection line of the planes pl1 and pl2 into l (may be NULL).
 *   parallel normals (8C650614): l = six +0 (when l != NULL) and the result
 *     is njDistanceP2L(copy of pl1->p, pl2, NULL), the distance between the
 *     planes;
 *   else (l != NULL): l->v = pl1->v x pl2->v, stored component by component,
 *     then a point of the line from the plane equations with
 *     d1 = -(pl1->v . pl1->p), d2 = -(pl2->v . pl2->p), in the original's
 *     case order (no check for a zero denominator):
 *       l->vx == 0 && l->vy == 0: p = (x, y, 0),
 *       l->vx == 0 && l->vz == 0: p = (x, 0, z),
 *       otherwise:                p = (0, y, z);
 *     result 0.
 * No FMAC: FMUL, FNEG, FSUB and FDIV in the original order; the planes are
 * re-read after each store into l (l may alias them). */
Float func_8C64016C(NJS_PLANE *pl1, NJS_PLANE *pl2, NJS_LINE *l)
{
    Float d1, d2;

    if (func_8C650614(pl1, pl2) != 0) {
        NJS_POINT3 pos;

        pos.x = pl1->px;
        pos.y = pl1->py;
        pos.z = pl1->pz;
        if (l != NULL) {
            l->px = 0.0f;
            l->py = 0.0f;
            l->pz = 0.0f;
            l->vx = 0.0f;
            l->vy = 0.0f;
            l->vz = 0.0f;
        }
        return njDistanceP2L(&pos, pl2, NULL);
    }
    if (l == NULL) {
        return 0.0f;
    }
    l->vx = RT_FSUB(pl1->vy * pl2->vz, pl2->vy * pl1->vz);
    l->vy = RT_FSUB(pl1->vz * pl2->vx, pl2->vz * pl1->vx);
    l->vz = RT_FSUB(pl1->vx * pl2->vy, pl2->vx * pl1->vy);
    d1 = RT_FNEG(pl1->vx * pl1->px);
    d1 = d1 - pl1->vy * pl1->py;
    d1 = d1 - pl1->vz * pl1->pz;
    d2 = RT_FNEG(pl2->vx * pl2->px);
    d2 = d2 - pl2->vy * pl2->py;
    d2 = d2 - pl2->vz * pl2->pz;
    if (l->vx == 0.0f && l->vy == 0.0f) {
        l->px = RT_FDIV(pl1->vy * d2 - pl2->vy * d1, pl1->vx * pl2->vy - pl2->vx * pl1->vy);
        l->py = RT_FDIV(pl1->vx * d2 - pl2->vx * d1, pl2->vx * pl1->vy - pl1->vx * pl2->vy);
        l->pz = 0.0f;
    } else if (l->vx == 0.0f && l->vz == 0.0f) {
        l->px = RT_FDIV(pl1->vz * d2 - pl2->vz * d1, pl1->vx * pl2->vz - pl2->vx * pl1->vz);
        l->py = 0.0f;
        l->pz = RT_FDIV(pl1->vx * d2 - pl2->vx * d1, pl2->vx * pl1->vz - pl1->vx * pl2->vz);
    } else {
        l->px = 0.0f;
        l->py = RT_FDIV(pl2->vz * d1 - pl1->vz * d2, pl2->vy * pl1->vz - pl1->vy * pl2->vz);
        l->pz = RT_FDIV(pl2->vy * d1 - pl1->vy * d2, pl1->vy * pl2->vz - pl2->vy * pl1->vz);
    }
    return 0.0f;
}

/* 8C63FE54 njCollisionCheckSS (ninja_funcs.h maps the name to the address):
 * 1 when s1->r + s2->r > |s1->c - s2->c| (FCMP/GT + MOVT; NaN gives 0).
 * 3 FSUB, FMUL + 2 FMAC, njSqrt, FADD. */
Sint32 func_8C63FE54(NJS_SPHERE *s1, NJS_SPHERE *s2)
{
    Float dy = s1->c.y - s2->c.y;
    Float dx = s1->c.x - s2->c.x;
    Float dz = s1->c.z - s2->c.z;
    Float d = dy * dy;
    Float dist, r;

    d = RT_FMAC(dx, dx, d);
    d = RT_FMAC(dz, dz, d);
    dist = njSqrt(d);
    r = s1->r + s2->r;
    return r > dist;
}
