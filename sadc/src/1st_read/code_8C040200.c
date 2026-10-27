#include "common.h"
#include "ninja.h"
#include "sa1/shc_intrinsics.h"

/*
 * 8C040200-8C0403C0: shape motion (vertex / normal key) helpers of the
 * motion playback module 8C03FDC0-8C040A60 (see code_8C03FDC0.c).
 *
 * The module is an assembly source assembled by asmsh, not an SHC object,
 * so none of these functions can match with the game's SHC flags:
 * - its alignment fill is "09 00" (asmsh) instead of SHC's "00 09", and its
 *   literal pools are shared by many functions and dumped between functions
 *   behind a "BRA over the pool; NOP" (8C040020, 8C040380), which SHC
 *   -code=asmcode output never contains;
 * - every "MOV Rm,R0" is followed by a NOP (SHC -extra bit 0x400, which the
 *   game's C code was built without);
 * - no FMAC anywhere (SHC 5.0r26..5.1r13 always fuse a * s + b * t with
 *   -fpu=single), FSRRA (not accepted by the build's asmsh) and FIPR FV4,FV4
 *   on live registers (SHC's _builtin_fipr goes through stack arrays).
 * func_8C0402D0 / func_8C04031E look like compiler output pasted into the
 * module: with "register" on nb SHC 5.1r03 reproduces their registers and
 * stack slots; only the scheduling of the pool load and of the reloads after
 * the first call differs (6 / 9 instructions; SHC 5.0r26 gets the pool load
 * right too). The original assembly stays in the build.
 * 8C040380 is not a function: it is the shared literal pool (BRA L8C0403A4;
 * NOP; 8 .DATA.L entries; NOP fill to 8C0403C0).
 */

/* Ninja shape motion key (SDK layout) */
typedef struct {
    Uint32 keyframe;            /* 0x00 */
    NJS_POINT3 *key;            /* 0x04 vertex or normal array of the key */
} NJS_MKEY_P;                   /* 0x08 */

/* Motion playback context (B_8C88FE40 / B_8C88FE5C), as in code_8C03FDC0.c */
typedef struct {
    void *mdata;                /* 0x00 current NJS_MDATAn entry */
    void **p;                   /* 0x04 key lists of the current entry */
    Uint32 *nb;                 /* 0x08 key counts of the current entry */
    Float frame;                /* 0x0C */
    Uint32 nbFrame;             /* 0x10 */
    Uint32 idx;                 /* 0x14 element index inside the entry */
    Sint32 unk18;               /* 0x18 */
} MOTION_CTX;                   /* 0x1C */

SA1_ASSERT_SIZE(NJS_MKEY_P, 0x08);
SA1_ASSERT_SIZE(MOTION_CTX, 0x1C);
SA1_ASSERT_OFFSET(MOTION_CTX, p, 0x04);
SA1_ASSERT_OFFSET(MOTION_CTX, nb, 0x08);
SA1_ASSERT_OFFSET(MOTION_CTX, frame, 0x0C);
SA1_ASSERT_OFFSET(MOTION_CTX, idx, 0x14);

/* shape motion context */
extern MOTION_CTX B_8C88FE5C;

/* binary search of the key at the shape motion frame (code_8C03FDC0.c) */
ASM_STUB(void func_8C03FEF2(NJS_MKEY_P *key, Uint32 nbkeys, Uint32 *out))

#ifdef NON_MATCHING
/*
 * Keys around the shape motion frame: out[0] = key n, out[1] = the next key
 * (key 0 after the last one). Returns the interpolation rate
 * (frame - keyframe n) / (keyframe n+1 - keyframe n); the division is done
 * as FSRRA(len)^2, and the rate is not scaled when len <= 0.
 */
Float func_8C040200(NJS_MKEY_P *key, Uint32 n, Uint32 nbkeys, NJS_POINT3 **out)
{
    NJS_MKEY_P *k0;
    NJS_MKEY_P *k1;
    Sint32 len;
    Float t;
    Float r;

    k0 = &key[n];
    k1 = key;
    if (n + 1 < nbkeys) {
        k1 = k0 + 1;
    }
    len = (Sint32)(k1->keyframe - k0->keyframe);
    t = B_8C88FE5C.frame - (Float)(Sint32)k0->keyframe;
    if (len > 0) {
        r = _builtin_fsrra((Float)len);
        t *= r * r;
    }
    out += 2;
    *--out = k1->key;
    *--out = k0->key;
    return t;
}
#else
ASM_STUB(Float func_8C040200(NJS_MKEY_P *key, Uint32 n, Uint32 nbkeys, NJS_POINT3 **out))
#endif

#ifdef NON_MATCHING
/*
 * Normal interpolation: out = normalize(p0 * (1 - t) + p1 * t) (FIPR dot
 * product, FSRRA reciprocal square root); (0, 0, 1) when the squared length
 * is not above 0.00001.
 */
void func_8C040240(NJS_POINT3 *p0, NJS_POINT3 *p1, NJS_POINT3 *out, Float t)
{
    Float v[4];
    Float s;
    Float d;
    Float r;
    Float x;
    Float y;
    Float z;

    s = 1.0f - t;
    x = p0->x * s + p1->x * t;
    y = p0->y * s + p1->y * t;
    z = p0->z * s + p1->z * t;
    v[0] = x;
    v[1] = y;
    v[2] = z;
    v[3] = 0.0f;
    d = _builtin_fipr(v, v);
    if (d > 0.00001f) {
        r = _builtin_fsrra(d);
        x *= r;
        y *= r;
        z *= r;
    } else {
        x = 0.0f;
        y = 0.0f;
        z = 1.0f;
    }
    out->z = z;
    out->y = y;
    out->x = x;
}
#else
ASM_STUB(void func_8C040240(NJS_POINT3 *p0, NJS_POINT3 *p1, NJS_POINT3 *out, Float t))
#endif

#ifdef NON_MATCHING
/* point interpolation: out = p0 * (1 - t) + p1 * t (no FMAC in the original) */
void func_8C0402A4(NJS_POINT3 *p0, NJS_POINT3 *p1, NJS_POINT3 *out, Float t)
{
    Float s;
    Float x;
    Float y;
    Float z;

    s = 1.0f - t;
    x = p0->x * s + p1->x * t;
    y = p0->y * s + p1->y * t;
    z = p0->z * s + p1->z * t;
    out->z = z;
    out->y = y;
    out->x = x;
}
#else
ASM_STUB(void func_8C0402A4(NJS_POINT3 *p0, NJS_POINT3 *p1, NJS_POINT3 *out, Float t))
#endif

#ifdef NON_MATCHING
/*
 * Vertex keys of the current shape element: out[0..1] = key pair, *rate =
 * interpolation rate, ++*count. Nothing when the element has no keys.
 */
void func_8C0402D0(Sint32 *count, Float *rate, NJS_POINT3 **out)
{
    NJS_MKEY_P *key;
    register Uint32 nb;
    Uint32 idx;
    Uint32 n;

    idx = B_8C88FE5C.idx;
    key = (NJS_MKEY_P *)B_8C88FE5C.p[idx];
    if (key != NULL) {
        nb = B_8C88FE5C.nb[idx];
        func_8C03FEF2(key, nb, &n);
        *rate = func_8C040200(key, n, nb, out);
        (*count)++;
    }
}
#else
ASM_STUB(void func_8C0402D0(Sint32 *count, Float *rate, NJS_POINT3 **out))
#endif

#ifdef NON_MATCHING
/* normal keys of the current shape element: as func_8C0402D0 but *count += 2 */
void func_8C04031E(Sint32 *count, Float *rate, NJS_POINT3 **out)
{
    NJS_MKEY_P *key;
    register Uint32 nb;
    Uint32 idx;
    Uint32 n;

    idx = B_8C88FE5C.idx;
    key = (NJS_MKEY_P *)B_8C88FE5C.p[idx];
    if (key != NULL) {
        nb = B_8C88FE5C.nb[idx];
        func_8C03FEF2(key, nb, &n);
        *rate = func_8C040200(key, n, nb, out);
        *count += 2;
    }
}
#else
ASM_STUB(void func_8C04031E(Sint32 *count, Float *rate, NJS_POINT3 **out))
#endif
