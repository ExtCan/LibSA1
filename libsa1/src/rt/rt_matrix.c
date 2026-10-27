/*
 * rt_matrix.c - Ninja matrix state and matrix functions
 * (docs/libsa1_integration.md 4.1/4.2/4.5, 2.9).
 *
 * Clean room: written from the documented behaviour of the SDK functions and
 * from the operation order, rounding and edge cases observed in the original
 * code, on top of the shared FPU models (rt.h).  Verified bit-exact against
 * the original SDK code in the harness, XMTRX included (README.md, section
 * "rt_matrix.c").
 *
 * State
 *   XMTRX           the current matrix: SA1_XMTRX[16] (rt.h).  Most functions
 *                   take a matrix argument m; m == NULL means XMTRX.
 *   B_8C88F5F8      matrix stack pointer: the next free 64-byte slot (the
 *                   current matrix itself lives in XMTRX, not on the stack)
 *   B_8C88F5FC      stack base (written by njInitMatrix only)
 *   B_8C88F698      stack depth limit, B_8C88F69C depth (1 after init)
 *   B_8C890070      njInitMatrix's flag: njCalcVector normalises its result
 *                   when it is not 0 (the game passes 0)
 *   D_8C67C640      the unit matrix (library data, read from the image)
 *   B_8C88F5E4 ...  screen parameters read by njProjectScreen
 * All of it lives at the SDK's own addresses, so game code that reads the
 * stack pointer or depth directly sees the real state (design 2.9).
 *
 * Conventions reproduced
 *   - "pre-multiply": every transform T applied to a matrix M gives T * M in
 *     Ninja's row-vector convention, computed row by row as FTRV (rt.h) of
 *     T's rows against M (XMTRX), or as FIPR / plain FMUL+FADD on a matrix
 *     in memory; each path keeps the original's own arithmetic, so the
 *     XMTRX and the memory variants of one function may differ in the last
 *     bit, exactly as on the Dreamcast.
 *   - njRotateX/Y/Z on a matrix in memory: the SDK loads the two affected
 *     rows of m into the XF bank over XMTRX's rows, transforms them with
 *     FTRV, stores them back to m and restores XMTRX.  The products of the
 *     two other rows (weight 0) therefore come from XMTRX: invisible except
 *     for inf/NaN and the sign of zero, which is why rt_rot_rows() builds
 *     exactly that mixed matrix.  XMTRX is unchanged afterwards.
 *   - Edge semantics (design 4.5): push at the limit does nothing and
 *     returns 0; push(NULL) stores XMTRX in two slots and advances once; pop
 *     refuses only depth - n == 0 (an unsigned compare: n > depth does pop,
 *     below the base); zero angles are skipped by njRotateXYZ / njRotateZXY
 *     (not by njRotateX/Y/Z or njRotate); a zero determinant fills the result
 *     with 0x7F7FC99E and returns 0.
 *
 * Interface notes (register results the C prototypes do not carry)
 *   - XMTRX after the call is part of every function's interface and is
 *     reproduced exactly (in the harness SA1_XMTRX is the interpreter's XF
 *     bank, so routed callers see it).
 *   - njPushMatrix and njPopMatrix return 1 / 0 (done / refused) in R0, as
 *     the originals do (Ninja's own headers declare them void).  Only
 *     njPushMatrix's result is ever read (adv00 0C900724 tests it); a scan of
 *     the JSR sites in 1ST_READ and the overlays found no read of R0 after
 *     njPopMatrix.
 *   - Values left in FR0-FR11 (caller-saved) are not reproduced; FR12-FR15
 *     are preserved by the originals as by this code.  The SDK callers of
 *     these functions are SHC-compiled C and rely on the ABI only.
 *
 * Limits (outside what the game does; see README.md, "rt_matrix.c")
 *   - njInitMatrix with m overlapping the unit matrix from below
 *     (m < D_8C67C640 < m + 16) reads D_8C67C640 after writing m[8..15] on
 *     the Dreamcast; in C that overlap needs pointer arithmetic outside an
 *     object, so the compiler may load the unit matrix first.  The game
 *     passes the stack buffer B_8C7EC8E0.
 *   - NaN encodings: results are the SH-4 default NaN 0x7FBFFFFF.  The
 *     32-bit harness interpreter stores 0x7FFFFFFF for a NaN that FIPR
 *     produces (its out-of-line sh4_fipr returns through x87 ST(0), which
 *     sets the quiet bit), and a float return value of this file passes
 *     through ST(0) as well on 32-bit x86 (func_8C64F3EC gives 0x7FFFFFFF
 *     to a routed caller there).
 */
#include "rt.h"

/* SDK state not declared in sa1/ninja_funcs.h (B_8C88F5F8, B_8C88F698,
 * B_8C88F69C and D_8C67C640 are) */
extern NJS_MATRIX_PTR B_8C88F5FC;       /* matrix stack base */
extern Sint32 B_8C890070;               /* njInitMatrix flag: njCalcVector normalises when != 0 */
extern Float B_8C88F5E4;                /* screen distance */
extern Float B_8C88F5F0;                /* screen centre x */
extern Float B_8C88F5F4;                /* screen centre y */
extern Float B_8C88F618;                /* screen scale x */
extern Float B_8C88F61C;                /* screen scale y */

/* functions of this file that sa1/ninja_funcs.h does not declare */
void func_8C638FE4(NJS_MATRIX_PTR m, Sint32 n, Sint32 flag);   /* njInitMatrix */
Float func_8C64F3EC(NJS_MATRIX_PTR m);                         /* determinant */

#define RT_FILL_SINGULAR 0x7F7FC99Eu    /* what njInvertMatrix leaves for a singular matrix */

/* ------------------------------------------------------------------ */
/* helpers                                                             */

/* the matrix a function works on: m, or XMTRX for NULL */
static Float *rt_target(NJS_MATRIX_PTR m)
{
    return m != NULL ? m : SA1_XMTRX;
}

/* copy 8 floats through a temporary (the SDK copies matrices in two halves
 * of 8 registers, which defines the result for overlapping buffers) */
static void rt_copy8(Float *dst, const Float *src)
{
    Float t[8];
    int i;

    for (i = 0; i < 8; i++)
        t[i] = src[i];
    for (i = 0; i < 8; i++)
        dst[i] = t[i];
}

/* T * M for a plane rotation: rows ra and rb of the target become
 * va * M and vb * M (FTRV), everything else stays.  For m != NULL the
 * matrix transformed is XMTRX with rows ra and rb taken from m (see the
 * header); the result goes to m and XMTRX is unchanged. */
static void rt_rot_rows(NJS_MATRIX_PTR m, int ra, int rb, const Float *va, const Float *vb)
{
    Float t[16], oa[4], ob[4];
    Float *dst = rt_target(m);
    int i;

    rt_xmtrx_store(t);
    if (m != NULL) {
        for (i = 0; i < 4; i++) {
            t[4 * ra + i] = m[4 * ra + i];
            t[4 * rb + i] = m[4 * rb + i];
        }
    }
    RT_FTRV_M(t, va, oa);
    RT_FTRV_M(t, vb, ob);
    for (i = 0; i < 4; i++) {
        dst[4 * ra + i] = oa[i];
        dst[4 * rb + i] = ob[i];
    }
}

/* rotations about one axis by the BAMS angle ang (FSCA) */
static void rt_rot_x(NJS_MATRIX_PTR m, Angle ang)
{
    Float s, c, va[4], vb[4];

    RT_FSCA(ang, &s, &c);
    va[0] = 0.0f; va[1] = c;          va[2] = s; va[3] = 0.0f;
    vb[0] = 0.0f; vb[1] = RT_FNEG(s); vb[2] = c; vb[3] = 0.0f;
    rt_rot_rows(m, 1, 2, va, vb);
}

static void rt_rot_y(NJS_MATRIX_PTR m, Angle ang)
{
    Float s, c, va[4], vb[4];

    RT_FSCA(ang, &s, &c);
    va[0] = c; va[1] = 0.0f; va[2] = RT_FNEG(s); va[3] = 0.0f;
    vb[0] = s; vb[1] = 0.0f; vb[2] = c;          vb[3] = 0.0f;
    rt_rot_rows(m, 0, 2, va, vb);
}

static void rt_rot_z(NJS_MATRIX_PTR m, Angle ang)
{
    Float s, c, va[4], vb[4];

    RT_FSCA(ang, &s, &c);
    va[0] = c;          va[1] = s; va[2] = 0.0f; va[3] = 0.0f;
    vb[0] = RT_FNEG(s); vb[1] = c; vb[2] = 0.0f; vb[3] = 0.0f;
    rt_rot_rows(m, 0, 1, va, vb);
}

/* njRotate on a matrix in memory: each step rotates two rows p and q of m
 * with plain FMUL / FADD / FSUB, one rounding per operation:
 *   rt_mrot_add:  p' = p*c + q*s,  q' = q*c - p*s
 *   rt_mrot_sub:  p' = p*c - q*s,  q' = q*c + p*s */
static void rt_mrot_add(Float *p, Float *q, Float c, Float s)
{
    int j;

    for (j = 0; j < 4; j++) {
        Float a = p[j], b = q[j];
        p[j] = RT_FADD(RT_FMUL(a, c), RT_FMUL(b, s));
        q[j] = RT_FSUB(RT_FMUL(b, c), RT_FMUL(a, s));
    }
}

static void rt_mrot_sub(Float *p, Float *q, Float c, Float s)
{
    int j;

    for (j = 0; j < 4; j++) {
        Float a = p[j], b = q[j];
        p[j] = RT_FSUB(RT_FMUL(a, c), RT_FMUL(b, s));
        q[j] = RT_FADD(RT_FMUL(b, c), RT_FMUL(a, s));
    }
}

/* ------------------------------------------------------------------ */
/* stack                                                               */

/* 8C638FE4: njInitMatrix(stack, number of slots, flag).  Slot 0 becomes the
 * unit matrix with elements 0 and 5 negated (X and Y mirrored); XMTRX is
 * loaded from it; depth 1. */
void func_8C638FE4(NJS_MATRIX_PTR m, Sint32 n, Sint32 flag)
{
    Float lo[8];
    int i;

    B_8C88F69C = 1;
    B_8C890070 = flag;
    B_8C88F5F8 = m;
    B_8C88F698 = n;
    B_8C88F5FC = m;
    rt_copy8(m + 8, D_8C67C640 + 8);
    for (i = 0; i < 8; i++)
        lo[i] = D_8C67C640[i];
    for (i = 0; i < 8; i++)
        m[i] = lo[i];
    m[0] = RT_FNEG(lo[0]);
    m[5] = RT_FNEG(lo[5]);
    rt_xmtrx_load(m);
}

/* 8C639C70: save XMTRX in the next slot.  m != NULL then loads XMTRX from
 * m; m == NULL stores XMTRX into the following slot as well (without
 * advancing the pointer again).  0 when the depth is at the limit. */
Sint32 njPushMatrix(NJS_MATRIX_PTR m)
{
    Sint32 depth = B_8C88F69C;
    NJS_MATRIX_PTR p;

    if (depth >= B_8C88F698)
        return 0;
    B_8C88F69C = depth + 1;
    p = B_8C88F5F8;
    rt_xmtrx_store(p);
    B_8C88F5F8 = p + 16;
    if (m == NULL)
        rt_xmtrx_store(p + 16);
    else
        rt_xmtrx_load(m);
    return 1;
}

/* 8C639B98: drop n slots and reload XMTRX from the slot reached.  Refused
 * only when depth - n is exactly 0 (unsigned compare with 1), so n > depth
 * does pop, below the base.  The slot address is pointer - (n << 6) bytes
 * in 32 bits (n = 0xFFFFFFFF moves up one slot); a signed 32-bit byte
 * offset gives the same address on 64-bit hosts.  1 popped, 0 refused. */
Sint32 njPopMatrix(Uint32 n)
{
    Uint32 depth = (Uint32)B_8C88F69C - n;
    NJS_MATRIX_PTR p;

    if (depth < 1u)
        return 0;
    B_8C88F69C = (Sint32)depth;
    p = (NJS_MATRIX_PTR)((Uint8 *)B_8C88F5F8 - (Sint32)(n << 6));
    B_8C88F5F8 = p;
    rt_xmtrx_load(p);
    return 1;
}

/* ------------------------------------------------------------------ */
/* whole matrices                                                      */

/* 8C63A8E0: m (or XMTRX) = unit matrix D_8C67C640 */
void njUnitMatrix(NJS_MATRIX_PTR m)
{
    if (m == NULL) {
        rt_xmtrx_load(D_8C67C640);
        return;
    }
    rt_copy8(m, D_8C67C640);
    rt_copy8(m + 8, D_8C67C640 + 8);
}

/* 8C63A770: md (or XMTRX) = ms */
void njSetMatrix(NJS_MATRIX_PTR md, NJS_MATRIX_PTR ms)
{
    if (md == NULL) {
        rt_xmtrx_load(ms);
        return;
    }
    rt_copy8(md, ms);
    rt_copy8(md + 8, ms + 8);
}

/* 8C638F94: m = XMTRX */
void njGetMatrix(NJS_MATRIX_PTR m)
{
    rt_xmtrx_store(m);
}

/* 8C63997C: md = ms * md.  XMTRX: one FTRV per row of ms; memory: one FIPR
 * per element (row of ms . column of md).  All of ms and md is read before
 * anything is written. */
void njMultiMatrix(NJS_MATRIX_PTR md, NJS_MATRIX_PTR ms)
{
    Float r[16];
    int i, j, k;

    if (md == NULL) {
        for (i = 0; i < 4; i++)
            RT_FTRV(ms + 4 * i, r + 4 * i);
        rt_xmtrx_load(r);
        return;
    }
    for (j = 0; j < 4; j++) {
        Float col[4];

        for (k = 0; k < 4; k++)
            col[k] = md[j + 4 * k];
        for (i = 0; i < 4; i++)
            r[4 * i + j] = RT_FIPR(col, ms + 4 * i);
    }
    for (i = 0; i < 16; i++)
        md[i] = r[i];
}

/* ------------------------------------------------------------------ */
/* translation and scale                                               */

/* row 3 = (x, y, z, 1) * M: FTRV on XMTRX, four FIPR on memory (column by
 * column, each stored before the next column is read) */
static void rt_translate(NJS_MATRIX_PTR m, Float x, Float y, Float z)
{
    Float v[4];
    int i;

    v[0] = x; v[1] = y; v[2] = z; v[3] = 1.0f;
    if (m == NULL) {
        Float r[4];

        RT_FTRV(v, r);
        for (i = 0; i < 4; i++)
            SA1_XMTRX[12 + i] = r[i];
        return;
    }
    for (i = 0; i < 4; i++) {
        Float col[4];

        col[0] = m[i]; col[1] = m[4 + i]; col[2] = m[8 + i]; col[3] = m[12 + i];
        m[12 + i] = RT_FIPR(v, col);
    }
}

/* 8C63A878 */
void njTranslate(NJS_MATRIX_PTR m, Float x, Float y, Float z)
{
    rt_translate(m, x, y, z);
}

/* 8C63A804 (v is read before m is written) */
void njTranslateV(NJS_MATRIX_PTR m, const NJS_VECTOR *v)
{
    rt_translate(m, v->x, v->y, v->z);
}

/* rows 0, 1, 2 *= sx, sy, sz (one FMUL each) */
static void rt_scale(NJS_MATRIX_PTR m, Float sx, Float sy, Float sz)
{
    Float *d = rt_target(m);
    int i;

    for (i = 0; i < 4; i++)
        d[i] = RT_FMUL(d[i], sx);
    for (i = 4; i < 8; i++)
        d[i] = RT_FMUL(d[i], sy);
    for (i = 8; i < 12; i++)
        d[i] = RT_FMUL(d[i], sz);
}

/* 8C63A69C */
void njScale(NJS_MATRIX_PTR m, Float sx, Float sy, Float sz)
{
    rt_scale(m, sx, sy, sz);
}

/* 8C63A5EC (v is read before m is written) */
void njScaleV(NJS_MATRIX_PTR m, const NJS_VECTOR *v)
{
    rt_scale(m, v->x, v->y, v->z);
}

/* ------------------------------------------------------------------ */
/* rotation                                                            */

/* 8C639EC8 */
void njRotateX(NJS_MATRIX_PTR m, Angle ang)
{
    rt_rot_x(m, ang);
}

/* 8C639F5C */
void njRotateY(NJS_MATRIX_PTR m, Angle ang)
{
    rt_rot_y(m, ang);
}

/* 8C63A1CC */
void njRotateZ(NJS_MATRIX_PTR m, Angle ang)
{
    rt_rot_z(m, ang);
}

/* 8C639CF4: Z, then Y, then X (M' = X * Y * Z * M); a zero angle (all 32
 * bits) is skipped */
void njRotateXYZ(NJS_MATRIX_PTR m, Angle angx, Angle angy, Angle angz)
{
    if (angz != 0)
        rt_rot_z(m, angz);
    if (angy != 0)
        rt_rot_y(m, angy);
    if (angx != 0)
        rt_rot_x(m, angx);
}

/* 8C639FF8: Y, then X, then Z; a zero angle is skipped */
void njRotateZXY(NJS_MATRIX_PTR m, Angle angx, Angle angy, Angle angz)
{
    if (angy != 0)
        rt_rot_y(m, angy);
    if (angx != 0)
        rt_rot_x(m, angx);
    if (angz != 0)
        rt_rot_z(m, angz);
}

/* 8C63A268: rotation by ang about the axis v (expected to be a unit vector),
 * done as five plane rotations X(a) Y(b) Z(ang) Y(-b) X(-a): the X rotation
 * that brings the axis into the XZ plane uses (sin, cos) = (-y, z) / |(y, z)|
 * (FMAC, FSRRA), the Y rotation that aligns it with Z uses (sin, cos) =
 * (x, |(y, z)|) (FSQRT; not normalised, the axis is assumed unit length).
 * A zero component selects +-1 / 0 instead (see below); the angle is not
 * checked for 0. */
void njRotate(NJS_MATRIX_PTR m, const NJS_VECTOR *v, Angle ang)
{
    Float vx = v->x, vy = v->y, vz = v->z;
    Float q, r, sa, ca, sb, cb;

    q = RT_FMAC(vz, vz, RT_FMUL(vy, vy));        /* y*y + z*z */
    if (vy == 0.0f) {
        sa = 0.0f;
        ca = vz > 0.0f ? 1.0f : RT_FNEG(1.0f);
    } else if (vz == 0.0f) {
        ca = 0.0f;
        sa = vy > 0.0f ? 1.0f : RT_FNEG(1.0f);
    } else {
        Float k = RT_FSRRA(q);

        sa = RT_FMUL(vy, k);
        ca = RT_FMUL(vz, k);
    }
    r = RT_FSQRT(q);
    if (vx == 0.0f) {
        sb = 0.0f;
        cb = r > 0.0f ? 1.0f : RT_FNEG(1.0f);
    } else if (r == 0.0f) {
        cb = 0.0f;
        sb = vx > 0.0f ? 1.0f : RT_FNEG(1.0f);
    } else {
        sb = vx;
        cb = r;
    }

    if (m == NULL) {
        Float s, c, va[4], vb[4];

        /* X with (sin, cos) = (-sa, ca) */
        va[0] = 0.0f; va[1] = ca; va[2] = RT_FNEG(sa); va[3] = 0.0f;
        vb[0] = 0.0f; vb[1] = sa; vb[2] = ca;          vb[3] = 0.0f;
        rt_rot_rows(NULL, 1, 2, va, vb);
        /* Y with (sb, cb) */
        va[0] = cb; va[1] = 0.0f; va[2] = RT_FNEG(sb); va[3] = 0.0f;
        vb[0] = sb; vb[1] = 0.0f; vb[2] = cb;          vb[3] = 0.0f;
        rt_rot_rows(NULL, 0, 2, va, vb);
        /* Z by ang */
        RT_FSCA(ang, &s, &c);
        va[0] = c;          va[1] = s; va[2] = 0.0f; va[3] = 0.0f;
        vb[0] = RT_FNEG(s); vb[1] = c; vb[2] = 0.0f; vb[3] = 0.0f;
        rt_rot_rows(NULL, 0, 1, va, vb);
        /* Y with (-sb, cb) */
        va[0] = cb;          va[1] = 0.0f; va[2] = sb; va[3] = 0.0f;
        vb[0] = RT_FNEG(sb); vb[1] = 0.0f; vb[2] = cb; vb[3] = 0.0f;
        rt_rot_rows(NULL, 0, 2, va, vb);
        /* X with (sa, ca) */
        va[0] = 0.0f; va[1] = ca;          va[2] = sa; va[3] = 0.0f;
        vb[0] = 0.0f; vb[1] = RT_FNEG(sa); vb[2] = ca; vb[3] = 0.0f;
        rt_rot_rows(NULL, 1, 2, va, vb);
    } else {
        Float s, c;

        rt_mrot_add(m + 4, m + 8, ca, RT_FNEG(sa));
        rt_mrot_sub(m, m + 8, cb, sb);
        RT_FSCA(ang, &s, &c);
        rt_mrot_add(m, m + 4, c, s);
        rt_mrot_sub(m, m + 8, cb, RT_FNEG(sb));
        rt_mrot_add(m + 4, m + 8, ca, sa);
    }
}

/* ------------------------------------------------------------------ */
/* determinant and inverse                                             */

/* 8C64F3EC: determinant of m (or XMTRX), cofactor expansion along row 0
 * from the 2x2 minors of rows 2 and 3, in the SDK's association order.
 * Intermediate NaNs stay NaNs whatever their payload, so only the result
 * is canonicalised. */
Float func_8C64F3EC(NJS_MATRIX_PTR m)
{
    const Float *a = rt_target(m);
    /* minors of rows 2/3: mij = a[8+i]*a[12+j] - a[8+j]*a[12+i]; nij is the
     * same difference taken the other way round */
    Float m01 = a[8] * a[13] - a[9] * a[12];
    Float m12 = a[9] * a[14] - a[10] * a[13];
    Float m23 = a[10] * a[15] - a[11] * a[14];
    Float m30 = a[11] * a[12] - a[8] * a[15];
    Float m13 = a[13] * a[11] - a[9] * a[15];
    Float n13 = a[9] * a[15] - a[13] * a[11];
    Float m02 = a[8] * a[14] - a[12] * a[10];
    Float n02 = a[12] * a[10] - a[8] * a[14];
    Float c2 = a[2] * (((a[4] * n13) + (a[5] * m30)) + (a[7] * m01));
    Float c3 = a[3] * (((a[5] * n02) + (a[4] * m12)) + (a[6] * m01));
    Float c0 = a[0] * (((a[6] * m13) + (a[5] * m23)) + (a[7] * m12));
    Float c1 = a[1] * (((a[4] * m23) + (a[6] * m30)) + (a[7] * m02));

    return RT_FADD(c2 - c3, c0 - c1);
}

/* one product of the adjugate: a[k] * (a[i] * a[j]) */
#define RT_T(k, i, j) (a[k] * (a[i] * a[j]))
/* one cofactor: ((t1 + t2) + t3) - t4 - t5 - t6 */
#define RT_COF(t1, t2, t3, t4, t5, t6) ((((((t1) + (t2)) + (t3)) - (t4)) - (t5)) - (t6))

/* 8C6390B0: m (or XMTRX) = inverse; returns 1.  A determinant equal to 0
 * (+-0, not NaN) fills all 16 floats with 0x7F7FC99E and returns 0.  The
 * adjugate is computed in the SDK's order (each cofactor negated where
 * needed, then multiplied by 1/det); all inputs are read before anything
 * is written. */
Sint32 njInvertMatrix(NJS_MATRIX_PTR m)
{
    Float *d;
    const Float *a;
    Float det, inv, c[16];
    int i;

    det = func_8C64F3EC(m);
    d = rt_target(m);
    if (det == 0.0f) {
        for (i = 0; i < 16; i++)
            d[i] = RT_U2F(RT_FILL_SINGULAR);
        return 0;
    }
    inv = RT_FDIV(1.0f, det);
    a = d;
    c[0] = RT_COF(RT_T(5, 10, 15), RT_T(6, 11, 13), RT_T(7, 9, 14), RT_T(7, 10, 13), RT_T(6, 9, 15), RT_T(5, 11, 14));
    c[1] = RT_FNEG(RT_COF(RT_T(2, 11, 13), RT_T(1, 10, 15), RT_T(3, 9, 14), RT_T(3, 10, 13), RT_T(2, 9, 15),
                          RT_T(1, 11, 14)));
    c[2] = RT_COF(RT_T(15, 1, 6), RT_T(13, 2, 7), RT_T(14, 3, 5), RT_T(13, 3, 6), RT_T(15, 2, 5), RT_T(14, 1, 7));
    c[3] = RT_FNEG(RT_COF(RT_T(11, 1, 6), RT_T(9, 2, 7), RT_T(10, 3, 5), RT_T(9, 3, 6), RT_T(11, 2, 5),
                          RT_T(10, 1, 7)));
    c[4] = RT_FNEG(RT_COF(RT_T(4, 10, 15), RT_T(6, 11, 12), RT_T(7, 8, 14), RT_T(7, 10, 12), RT_T(6, 8, 15),
                          RT_T(4, 11, 14)));
    /* element 5: the last product is negated and added first */
    c[5] = RT_FNEG(RT_T(0, 11, 14)) +
           ((((RT_T(0, 10, 15) + RT_T(2, 11, 12)) + RT_T(3, 8, 14)) - RT_T(3, 10, 12)) - RT_T(2, 8, 15));
    c[6] = RT_FNEG(RT_COF(RT_T(12, 2, 7), RT_T(15, 0, 6), RT_T(14, 3, 4), RT_T(12, 3, 6), RT_T(15, 2, 4),
                          RT_T(14, 0, 7)));
    c[7] = RT_COF(RT_T(8, 2, 7), RT_T(11, 0, 6), RT_T(10, 3, 4), RT_T(8, 3, 6), RT_T(11, 2, 4), RT_T(10, 0, 7));
    c[8] = RT_COF(RT_T(4, 9, 15), RT_T(5, 11, 12), RT_T(7, 8, 13), RT_T(7, 9, 12), RT_T(5, 8, 15), RT_T(4, 11, 13));
    c[9] = RT_FNEG(RT_COF(RT_T(0, 9, 15), RT_T(1, 11, 12), RT_T(3, 8, 13), RT_T(3, 9, 12), RT_T(1, 8, 15),
                          RT_T(0, 11, 13)));
    c[10] = RT_COF(RT_T(12, 1, 7), RT_T(15, 0, 5), RT_T(13, 3, 4), RT_T(12, 3, 5), RT_T(15, 1, 4),
                   RT_T(13, 0, 7));
    c[11] = RT_FNEG(RT_COF(RT_T(8, 1, 7), RT_T(11, 0, 5), RT_T(9, 3, 4), RT_T(8, 3, 5), RT_T(11, 1, 4),
                           RT_T(9, 0, 7)));
    c[12] = RT_FNEG(RT_COF(RT_T(5, 10, 12), RT_T(4, 9, 14), RT_T(6, 8, 13), RT_T(6, 9, 12), RT_T(5, 8, 14),
                           RT_T(4, 10, 13)));
    c[13] = RT_COF(RT_T(0, 9, 14), RT_T(1, 10, 12), RT_T(2, 8, 13), RT_T(2, 9, 12), RT_T(1, 8, 14),
                   RT_T(0, 10, 13));
    c[14] = RT_FNEG(RT_COF(RT_T(12, 1, 6), RT_T(14, 0, 5), RT_T(13, 2, 4), RT_T(12, 2, 5), RT_T(14, 1, 4),
                           RT_T(13, 0, 6)));
    c[15] = RT_COF(RT_T(10, 0, 5), RT_T(8, 1, 6), RT_T(9, 2, 4), RT_T(8, 2, 5), RT_T(10, 1, 4), RT_T(9, 0, 6));
    for (i = 0; i < 16; i++)
        d[i] = RT_FMUL(c[i], inv);
    return 1;
}

#undef RT_T
#undef RT_COF

/* ------------------------------------------------------------------ */
/* points and vectors                                                  */

/* 8C638ECC: pd = (ps, 1) * M.  XMTRX: FTRV.  Memory: one FIPR per column,
 * interleaved with the stores exactly as the SDK does (pd.x is written
 * before column 2 is read, pd.y before the last FIPR). */
void njCalcPoint(NJS_MATRIX_PTR m, const NJS_POINT3 *ps, NJS_POINT3 *pd)
{
    Float v[4], col[4], x, y, z;

    v[0] = ps->x; v[1] = ps->y; v[2] = ps->z; v[3] = 1.0f;
    if (m == NULL) {
        Float r[4];

        RT_FTRV(v, r);
        pd->x = r[0];
        pd->y = r[1];
        pd->z = r[2];
        return;
    }
    col[0] = m[0]; col[1] = m[4]; col[2] = m[8]; col[3] = m[12];
    x = RT_FIPR(v, col);
    col[0] = m[1]; col[1] = m[5]; col[2] = m[9]; col[3] = m[13];
    pd->x = x;
    y = RT_FIPR(v, col);
    col[0] = m[2]; col[1] = m[6]; col[2] = m[10]; col[3] = m[14];
    pd->y = y;
    z = RT_FIPR(v, col);
    pd->z = z;
}

/* 8C638F28: vd = (vs, 0) * M (no translation); normalised with FIPR +
 * FSRRA when the njInitMatrix flag B_8C890070 is set */
void njCalcVector(NJS_MATRIX_PTR m, const NJS_VECTOR *vs, NJS_VECTOR *vd)
{
    Float v[4], r[4];

    v[0] = vs->x; v[1] = vs->y; v[2] = vs->z; v[3] = 0.0f;
    if (m == NULL) {
        RT_FTRV(v, r);
    } else {
        Float col[4];
        int j;

        for (j = 0; j < 3; j++) {
            col[0] = m[j]; col[1] = m[4 + j]; col[2] = m[8 + j]; col[3] = 0.0f;
            r[j] = RT_FIPR(v, col);
        }
    }
    r[3] = 0.0f;
    if (B_8C890070 != 0) {
        Float k = RT_FSRRA(RT_FIPR(r, r));

        r[2] = RT_FMUL(r[2], k);
        r[1] = RT_FMUL(r[1], k);
        r[0] = RT_FMUL(r[0], k);
    }
    vd->x = r[0];
    vd->y = r[1];
    vd->z = r[2];
}

/* 8C639BDC: p2 = screen position of (p3, 1) * M:
 *   x = ((dist * X) * (1/Z)) * scale_x + centre_x, y likewise */
void njProjectScreen(NJS_MATRIX_PTR m, const NJS_POINT3 *p3, NJS_POINT2 *p2)
{
    Float v[4], r[4], iz, sx, sy;

    v[0] = p3->x; v[1] = p3->y; v[2] = p3->z; v[3] = 1.0f;
    if (m == NULL) {
        RT_FTRV(v, r);
    } else {
        Float col[4];
        int j;

        for (j = 0; j < 3; j++) {
            col[0] = m[j]; col[1] = m[4 + j]; col[2] = m[8 + j]; col[3] = m[12 + j];
            r[j] = RT_FIPR(v, col);
        }
    }
    iz = RT_FDIV(1.0f, r[2]);
    sx = RT_FADD(RT_FMUL(RT_FMUL(RT_FMUL(B_8C88F5E4, r[0]), iz), B_8C88F618), B_8C88F5F0);
    sy = RT_FADD(RT_FMUL(RT_FMUL(RT_FMUL(B_8C88F5E4, r[1]), iz), B_8C88F61C), B_8C88F5F4);
    p2->x = sx;
    p2->y = sy;
}
