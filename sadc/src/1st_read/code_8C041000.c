#include "sa1/sa1.h"

/*
 * 1ST_READ 8C041000-8C04124E: shape (vertex / normal key) helpers of the
 * linked motion playback module (njDrawShapeMotionLink style; see
 * code_8C040A60.c for the context globals). Link counterparts of
 * func_8C040240 / func_8C0402A4 / func_8C0402D0 / func_8C04031E
 * (code_8C040200.c), which compile to identical instruction sequences.
 *
 * func_8C041000 is not a function: it is the translation unit's mid-file
 * literal pool (".ALIGN 32; BRA over the pool; NOP; pool; .ALIGN 32") that
 * holds the pool entries of func_8C040F7A and earlier functions. It is
 * produced by the compiler and stays in the assembly chunk.
 *
 * NON_MATCHING (all functions): this module (about 8C03FDC0-8C041D60) was
 * not built like the rest of the game code:
 *  - its .ALIGN fill is "09 00" (asmsh style, i.e. -code=asmcode + asmsh)
 *    where every SHC 5.1r03 -code=machinecode object has "00 09"
 *    (8C040FFA, 8C04102C, 8C041324; the 00 09 fill resumes at 8C041EE4);
 *  - &B_8C88FEF0[(Sint8)B_8C88FF38] is compiled as MULS.W + EXTS.B (the
 *    byte offset is truncated to 8 bits): SHC 5.0r10 .. 5.1r01 do this,
 *    5.1r03 and later do not;
 *  - a * s + b * t is never contracted to FMAC (FMUL, FMUL, FADD), which no
 *    available SHC version produces for -cpu=sh4 (only 5.0r10 -cpu=sh3e);
 *  - FIPR FV4,FV4 on register operands (no stack arrays) and FSRRA as a
 *    .DATA.W word (func_8C041040), unlike SHC 5.1's _builtin_fipr.
 * With SHC 5.0r26..5.1r01 the integer functions below differ only in
 * instruction scheduling (22 of 65 instructions); the original compiler
 * version is not among tools/shc.
 */

/* Ninja shape motion key (SDK layout) */
typedef struct {
    Uint32 keyframe;            /* 0x00 */
    NJS_POINT3 *key;            /* 0x04 */
} NJS_MKEY_P;                   /* 0x08 */

/* Motion evaluation context (same layout as in code_8C040A60.c) */
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
SA1_ASSERT_OFFSET(MOTION_CTX, idx, 0x14);

extern MOTION_CTX B_8C88FEF0[2];        /* shape motion contexts */
extern Uint16 B_8C88FF30[2];            /* shape handler index per motion */
extern Uint16 B_8C88FF38;               /* motion being evaluated (0 or 1) */
extern Uint16 B_8C88FF46;               /* bit n: motion n has vertex keys */
extern Uint16 B_8C88FF48;               /* bit n: motion n has normal keys */
extern NJS_POINT3 *B_8C88FFFC[2];       /* vertex destination per motion */
extern NJS_VECTOR *B_8C89000C[2];       /* normal destination per motion */

ASM_STUB(void func_8C040AAA(NJS_MKEY_P *key, Uint32 nbkeys, Uint32 *out))
ASM_STUB(Float func_8C040F7A(NJS_MKEY_P *key, Uint32 n, Uint32 nbkeys, NJS_POINT3 **out))

#ifdef NON_MATCHING
/* normal interpolation: out = normalize(p0 * (1 - t) + p1 * t), (0, 0, 1) if too short */
void func_8C041040(NJS_VECTOR *p0, NJS_VECTOR *p1, NJS_VECTOR *out, Float t)
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
ASM_STUB(void func_8C041040(NJS_VECTOR *p0, NJS_VECTOR *p1, NJS_VECTOR *out, Float t))
#endif

#ifdef NON_MATCHING
/* point interpolation: out = p0 * (1 - t) + p1 * t */
void func_8C0410A4(NJS_POINT3 *p0, NJS_POINT3 *p1, NJS_POINT3 *out, Float t)
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
ASM_STUB(void func_8C0410A4(NJS_POINT3 *p0, NJS_POINT3 *p1, NJS_POINT3 *out, Float t))
#endif

#ifdef NON_MATCHING
/*
 * Vertex keys of the current shape element of motion B_8C88FF38: keys[0..3]
 * = the 4 key point arrays around the frame, *rate = interpolation rate;
 * records dst and the motion's vertex flag; *type = its shape handler index.
 */
void func_8C0410D0(Sint32 *type, Float *rate, NJS_POINT3 **keys, NJS_POINT3 *dst)
{
    MOTION_CTX *mp;
    NJS_MKEY_P *key;
    Uint32 nb;
    Uint32 n;
    Uint32 i;

    mp = &B_8C88FEF0[(Sint8)B_8C88FF38];
    i = mp->idx;
    key = (NJS_MKEY_P *)mp->p[i];
    if (key != NULL) {
        nb = mp->nb[i];
        func_8C040AAA(key, nb, &n);
        *rate = func_8C040F7A(key, n, nb, keys);
        B_8C88FFFC[B_8C88FF38] = dst;
        B_8C88FF46 |= B_8C88FF38 + 1;
        *type = B_8C88FF30[B_8C88FF38];
    }
}
#else
ASM_STUB(void func_8C0410D0(Sint32 *type, Float *rate, NJS_POINT3 **keys, NJS_POINT3 *dst))
#endif

#ifdef NON_MATCHING
/* normal keys of the current shape element (as func_8C0410D0) */
void func_8C041152(Sint32 *type, Float *rate, NJS_VECTOR **keys, NJS_VECTOR *dst)
{
    MOTION_CTX *mp;
    NJS_MKEY_P *key;
    Uint32 nb;
    Uint32 n;
    Uint32 i;

    mp = &B_8C88FEF0[(Sint8)B_8C88FF38];
    i = mp->idx;
    key = (NJS_MKEY_P *)mp->p[i];
    if (key != NULL) {
        nb = mp->nb[i];
        func_8C040AAA(key, nb, &n);
        *rate = func_8C040F7A(key, n, nb, keys);
        B_8C89000C[B_8C88FF38] = dst;
        B_8C88FF48 |= B_8C88FF38 + 1;
        *type = B_8C88FF30[B_8C88FF38];
    }
}
#else
ASM_STUB(void func_8C041152(Sint32 *type, Float *rate, NJS_VECTOR **keys, NJS_VECTOR *dst))
#endif

#ifdef NON_MATCHING
/* vertex keys of the current shape element; counts them in *type (+1) */
void func_8C0411D4(Sint32 *type, Float *rate, NJS_POINT3 **keys, NJS_POINT3 *dst)
{
    MOTION_CTX *mp;
    NJS_MKEY_P *key;
    Uint32 nb;
    Uint32 n;
    Uint32 i;

    mp = &B_8C88FEF0[(Sint8)B_8C88FF38];
    i = mp->idx;
    key = (NJS_MKEY_P *)mp->p[i];
    if (key != NULL) {
        nb = mp->nb[i];
        func_8C040AAA(key, nb, &n);
        *rate = func_8C040F7A(key, n, nb, keys);
        B_8C88FFFC[B_8C88FF38] = dst;
        B_8C88FF46 |= B_8C88FF38 + 1;
        (*type)++;
    }
}
#else
ASM_STUB(void func_8C0411D4(Sint32 *type, Float *rate, NJS_POINT3 **keys, NJS_POINT3 *dst))
#endif
