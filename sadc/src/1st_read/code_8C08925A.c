#include "common.h"
#include "ninja.h"
#include "sa1/sa1.h"

/*
 * Natural cubic spline through a list of knots (8C08925A-8C089528):
 * second derivatives by the tridiagonal solve, then sampling of the curve
 * in unit steps of the accumulated length. func_8C089184 (code_8C089184.c)
 * builds the knot parameters. Only caller: the test function func_8C089528.
 */

/* Cubic spline knot (48 bytes); code_8C089184.c has the same layout with Float m[3], p[3] */
typedef struct {
    Float t;            /* 0x00 parameter (accumulated length) */
    Float h;            /* 0x04 t[i] - t[i-1] */
    Float mu;           /* 0x08 */
    Float lambda;       /* 0x0C */
    Float l;            /* 0x10 */
    Float d;            /* 0x14 */
    NJS_POINT3 m;       /* 0x18 second derivatives */
    NJS_POINT3 p;       /* 0x24 point */
} SPLINE_KNOT;

/*
 * Neither function below calls anything; these stubs only stand in for the
 * code that preceded func_8C08925A in the original translation unit
 * (func_8C089184, 214 bytes, with a literal at +0x18). SHC flushes its
 * literal pool at a function end once the oldest pending literal is far
 * enough back, and a pool still pending inside func_8C0893E0 (here the 6.0f
 * of func_8C08925A) is counted into its branch-range estimates, which turns
 * the inner-loop guard and the outer back branch into BT/S+BRA pairs.
 * With 2 to 4 stubs here the pool is flushed after func_8C08925A as in the
 * original (pool at 8C0893C0) and func_8C0893E0 matches; 3 is the middle.
 */
ASM_STUB(void func_8C089184(SPLINE_KNOT *k, Sint32 n))
ASM_STUB(void func_8C089528(void))
ASM_STUB(Sint32 func_8C089560(void))

void func_8C08925A(SPLINE_KNOT *k, Sint32 n)
{
    Sint32 i;
    Sint32 j;
    SPLINE_KNOT *p;
    Float *pa;
    Float *pb;
    Float b[32];
    Float a[32];

    for (j = 0; j < 3; j++) {
        for (i = 1, p = &k[1], pb = &b[1]; i < n - 1; i++, p++, pb++) {
            *pb = (((&p[1].p.x)[j] - (&p->p.x)[j]) / p[1].h - ((&p->p.x)[j] - (&p[-1].p.x)[j]) / p->h) * 6.0f / (p->h + p[1].h);
        }
        a[1] = b[1];
        for (i = 2, p = &k[2], pb = &b[2], pa = &a[2]; i < n - 1; i++, p++, pa++, pb++) {
            *pa = *pb - p->l * pa[-1];
        }
        (&p->m.x)[j] = 0.0f;
        (&p[n - 1].m.x)[j] = 0.0f;
        for (i = n - 2, p = &k[i], pa = &a[i]; i >= 0; i--, p--, pa--) {
            (&p->m.x)[j] = (*pa - p->mu * (&(p - 1)->m.x)[j]) / p->d;
        }
    }
}

/*
 * Samples the spline in unit steps of t (at most 256 samples) and returns the
 * number of samples taken. As in the original, out only advances once per
 * segment: out[0] = k[0].p and out[i] (i >= 1) ends up with the last sample
 * of segment k[i-1]..k[i]. The first pass (i == 1) pairs k[0] with k[-1] but
 * takes no sample, since t starts at k[0].t.
 */
Sint32 func_8C0893E0(SPLINE_KNOT *k, NJS_POINT3 *out, Sint32 n)
{
    Sint32 i;
    Sint32 cont;
    Sint32 cnt;
    Float t;
    Float a;
    Float b;
    Float ca;
    Float cb;
    Float ih;

    cnt = 0;
    cont = 1;
    t = k->t;
    out->x = k->p.x;
    out->y = k->p.y;
    out->z = k->p.z;
    for (i = 1; i < n; i++) {
        ih = 1.0f / k->h;
        while (k->t > t) {
            t += 1.0f;
            if (t > k->t) {
                t = k->t;
            }
            a = k->t - t;
            b = t - (k - 1)->t;
            ca = a * a * a * ih;
            cb = b * b * b * ih;
            out->x = SA1_FMAC(0.16666666f, SA1_FMAC((k - 1)->m.x, ca - a * k->h, k->m.x * (cb - b * k->h)), SA1_FMAC(a, (k - 1)->p.x, b * k->p.x) * ih);
            out->y = SA1_FMAC(0.16666666f, SA1_FMAC((k - 1)->m.y, ca - a * k->h, k->m.y * (cb - b * k->h)), SA1_FMAC(a, (k - 1)->p.y, b * k->p.y) * ih);
            out->z = SA1_FMAC(0.16666666f, SA1_FMAC((k - 1)->m.z, ca - a * k->h, k->m.z * (cb - b * k->h)), SA1_FMAC(a, (k - 1)->p.z, b * k->p.z) * ih);
            if (++cnt == 256) {
                cont = 0;
                break;
            }
        }
        if (!cont) {
            break;
        }
        k++;
        out++;
    }
    return cnt;
}
