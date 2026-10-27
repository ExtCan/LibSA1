#include "sa1/sa1.h"

/*
 * 1ST_READ 8C040CE0-8C041000 (middle of the 8C040A60 chunk): linked
 * (blended) motion playback, continued from code_8C040A60.c
 * (njDrawMotionLink-like; see that file for the context layout).
 * B_8C88FF38 is the index (0 or 1) of the motion of the link that is being
 * evaluated. Each motion has its own evaluation context (B_8C88FEB8[2] node
 * motion, B_8C88FEF0[2] shape motion) and its own result slots, filled by
 * the per-node callbacks installed by func_8C040C02:
 *   B_8C88FF4C[2]  translation  (B_8C88FF40 |= n + 1 when keyed)
 *   B_8C88FFAC[2]  rotation     (B_8C88FF42)
 *   B_8C88FF7C[2]  scale        (B_8C88FF44)
 *
 *  func_8C040CE0  not a function: SHC's mid-file literal pool of
 *                 func_8C040A60..func_8C040C5C (BRA over the pool + NOP,
 *                 pool, .ALIGN 32 NOP fill); stays in the assembly chunk.
 *  func_8C040D60  linear interpolation of an Angle key (NJS_MKEY_A)
 *  func_8C040DF0  translate callback (pos keys or object->pos)
 *  func_8C040E74  scale callback (scl keys or object->scl)
 *  func_8C040EF8  rotate callback (ang keys or object->ang)
 *  func_8C040F7A  the 4 shape keys around the frame (spline) and the rate
 *
 * NOT MATCHING (all five, bodies under NON_MATCHING):
 *  - func_8C040D60/func_8C040F7A use FSRRA, which the build's asmsh cannot
 *    assemble (as func_8C03FF2C/func_8C040C5C). Besides that, SHC 5.1r03
 *    fuses k0 * (1 - t) + k1 * t into FMAC; the original has FMUL, FMUL,
 *    FADD (as everywhere in the 8C03FDC0-8C042DC4 motion module; no source
 *    form or flag tried avoids the FMAC).
 *  - func_8C040DF0/E74/EF8: registers and instructions are identical, only
 *    the schedule differs: the original moves the first body instructions
 *    (and the "ADD #-4,R15" frame allocation) into the register pushes, sets
 *    R7 before the "MOV.L @R15,R5" argument load and "out += 3" before the
 *    loads. SHC 5.1r03 never schedules into the prologue of a function with
 *    a stack frame; only ~50 functions of the game have this, most of them
 *    in this motion module (8C0403C0..8C041DC6), which suggests different
 *    compiler settings for the module.
 */

/* Ninja motion keys (SDK layout) */
typedef struct {
    Uint32 keyframe;            /* 0x00 */
    Float key[3];               /* 0x04 */
} NJS_MKEY_F;                   /* 0x10 */

typedef struct {
    Uint32 keyframe;            /* 0x00 */
    Angle key[3];               /* 0x04 */
} NJS_MKEY_A;                   /* 0x10 */

typedef struct {
    Uint32 keyframe;            /* 0x00 */
    NJS_POINT3 *key;            /* 0x04 */
} NJS_MKEY_P;                   /* 0x08 */

/* Motion evaluation context (same layout as B_8C88FE40 in code_8C03FDC0.c) */
typedef struct {
    void *mdata;                /* 0x00 current NJS_MDATAn entry */
    void **p;                   /* 0x04 key lists of the current entry */
    Uint32 *nb;                 /* 0x08 key counts of the current entry */
    Float frame;                /* 0x0C */
    Uint32 nbFrame;             /* 0x10 */
    Uint32 idx;                 /* 0x14 element index inside the entry */
    Sint32 unk18;               /* 0x18 */
} MOTION_CTX;                   /* 0x1C */

SA1_ASSERT_SIZE(NJS_MKEY_F, 0x10);
SA1_ASSERT_SIZE(NJS_MKEY_A, 0x10);
SA1_ASSERT_SIZE(NJS_MKEY_P, 0x08);
SA1_ASSERT_SIZE(MOTION_CTX, 0x1C);
SA1_ASSERT_OFFSET(MOTION_CTX, p, 0x04);
SA1_ASSERT_OFFSET(MOTION_CTX, nb, 0x08);
SA1_ASSERT_OFFSET(MOTION_CTX, frame, 0x0C);
SA1_ASSERT_OFFSET(MOTION_CTX, idx, 0x14);

extern MOTION_CTX B_8C88FEB8[2];        /* node motion contexts */
extern MOTION_CTX B_8C88FEF0[2];        /* shape motion contexts */
extern Uint16 B_8C88FF38;               /* motion of the link being evaluated */
extern Uint16 B_8C88FF40;               /* bit n: translation of motion n keyed */
extern Uint16 B_8C88FF42;               /* bit n: rotation keyed */
extern Uint16 B_8C88FF44;               /* bit n: scale keyed */
extern NJS_POINT3 B_8C88FF4C[2];        /* translation of each motion */
extern NJS_POINT3 B_8C88FF7C[2];        /* scale of each motion */
extern Angle3 B_8C88FFAC[2];            /* rotation of each motion */

/*
 * Context (and its frame) of motion B_8C88FF38 in base[]. The original
 * computes the byte offset as a signed char, (Sint8)(n * 0x1C) (MULS.W +
 * EXTS.B after the product), the same as &base[n] for the two motions of a
 * link. The original reads the frame through the folded pool constant
 * &base[0].frame (B_8C88FEC4 / B_8C88FEFC) + offset, FMOV.S @(R0,Rn); the
 * byte-offset form below makes SHC add the 12 separately (array indexing
 * folds it but sign-extends n before the multiply instead).
 */
#ifdef TARGET_PC
#define LINK_CTX(base) (&(base)[B_8C88FF38])
#define LINK_FRAME(base) ((base)[B_8C88FF38].frame)
#else
#define LINK_OFS ((Sint8)(B_8C88FF38 * sizeof(MOTION_CTX)))
#define LINK_CTX(base) ((MOTION_CTX *)((Sint8 *)(base) + LINK_OFS))
#define LINK_FRAME(base) (*(Float *)((Sint8 *)&(base)[0].frame + LINK_OFS))
#endif

ASM_STUB(void func_8C040A60(NJS_MKEY_F *key, Uint32 nbkeys, Uint32 *out))
ASM_STUB(void func_8C040C5C(NJS_MKEY_F *key, Uint32 n, Uint32 nbkeys, Float *out))

#ifdef NON_MATCHING
/* linear interpolation of Angle key n at the frame of node motion B_8C88FF38 */
void func_8C040D60(NJS_MKEY_A *key, Uint32 n, Uint32 nbkeys, Angle *out)
{
    NJS_MKEY_A *k0;
    NJS_MKEY_A *k1;
    Sint32 len;
    Float t;
    Float r;
    Float s;

    k0 = &key[n];
    k1 = key;
    if (n + 1 < nbkeys) {
        k1 = k0 + 1;
    }
    len = k1->keyframe - k0->keyframe;
    t = LINK_FRAME(B_8C88FEB8) - (Float)(Sint32)k0->keyframe;
    if (len > 0) {
        r = _builtin_fsrra((Float)len);
        r *= r;
        t *= r;
    }
    s = 1.0f - t;
    out[0] = SA1_FTOI((Float)k0->key[0] * s + (Float)k1->key[0] * t);
    out[1] = SA1_FTOI((Float)k0->key[1] * s + (Float)k1->key[1] * t);
    out[2] = SA1_FTOI((Float)k0->key[2] * s + (Float)k1->key[2] * t);
}
#else
ASM_STUB(void func_8C040D60(NJS_MKEY_A *key, Uint32 n, Uint32 nbkeys, Angle *out))
#endif

#ifdef NON_MATCHING
/* translate callback: position of node motion B_8C88FF38 into B_8C88FF4C */
void func_8C040DF0(NJS_OBJECT *object)
{
    Float *out;
    MOTION_CTX *mp;
    Uint32 i;
    NJS_MKEY_F *key;
    Uint32 nbkeys;
    Uint32 n;
    Float *p;
    Float x;
    Float y;
    Float z;

    mp = LINK_CTX(B_8C88FEB8);
    i = mp->idx;
    out = &B_8C88FF4C[B_8C88FF38].x;
    key = (NJS_MKEY_F *)mp->p[i];
    if (key != NULL) {
        nbkeys = mp->nb[i];
        func_8C040A60(key, nbkeys, &n);
        func_8C040C5C(key, n, nbkeys, out);
        B_8C88FF40 |= B_8C88FF38 + 1;
    } else {
        p = object->pos;
        out += 3;
        x = *p++;
        y = *p++;
        z = *p;
        *--out = z;
        *--out = y;
        *--out = x;
    }
    mp->idx = i + 1;
}

/* scale callback: scale of node motion B_8C88FF38 into B_8C88FF7C */
void func_8C040E74(NJS_OBJECT *object)
{
    Float *out;
    MOTION_CTX *mp;
    Uint32 i;
    NJS_MKEY_F *key;
    Uint32 nbkeys;
    Uint32 n;
    Float *p;
    Float x;
    Float y;
    Float z;

    mp = LINK_CTX(B_8C88FEB8);
    i = mp->idx;
    out = &B_8C88FF7C[B_8C88FF38].x;
    key = (NJS_MKEY_F *)mp->p[i];
    if (key != NULL) {
        nbkeys = mp->nb[i];
        func_8C040A60(key, nbkeys, &n);
        func_8C040C5C(key, n, nbkeys, out);
        B_8C88FF44 |= B_8C88FF38 + 1;
    } else {
        p = object->scl;
        out += 3;
        x = *p++;
        y = *p++;
        z = *p;
        *--out = z;
        *--out = y;
        *--out = x;
    }
    mp->idx = i + 1;
}

/* rotate callback: angles of node motion B_8C88FF38 into B_8C88FFAC */
void func_8C040EF8(NJS_OBJECT *object)
{
    MOTION_CTX *mp;
    Angle *out;
    Uint32 i;
    NJS_MKEY_A *key;
    Uint32 nbkeys;
    Uint32 n;
    Angle *p;

    mp = LINK_CTX(B_8C88FEB8);
    i = mp->idx;
    out = &B_8C88FFAC[B_8C88FF38].x;
    key = (NJS_MKEY_A *)mp->p[i];
    if (key != NULL) {
        nbkeys = mp->nb[i];
        func_8C040A60((NJS_MKEY_F *)key, nbkeys, &n);
        func_8C040D60(key, n, nbkeys, out);
        B_8C88FF42 |= B_8C88FF38 + 1;
    } else {
        p = object->ang;
        out[0] = p[0];
        out[1] = p[1];
        out[2] = p[2];
    }
    mp->idx = i + 1;
}
#else
ASM_STUB(void func_8C040DF0(NJS_OBJECT *object))
ASM_STUB(void func_8C040E74(NJS_OBJECT *object))
ASM_STUB(void func_8C040EF8(NJS_OBJECT *object))
#endif

#ifdef NON_MATCHING
/*
 * Shape key n at the frame of shape motion B_8C88FF38 and its neighbours
 * for a 4-point (spline) blend: out[] = vertex arrays of keys n-1 (wraps to
 * the last key), n, n+1 and n+2 (both wrap to key 0); returns the rate in
 * [n, n+1].
 */
Float func_8C040F7A(NJS_MKEY_P *key, Uint32 n, Uint32 nbkeys, NJS_POINT3 **out)
{
    NJS_MKEY_P *k0;
    NJS_MKEY_P *km;
    NJS_MKEY_P *k1;
    Sint32 len;
    Float t;
    Float r;

    k0 = &key[n];
    km = &key[nbkeys - 1];
    if (n > 0) {
        km = k0 - 1;
    }
    k1 = key;
    if (n + 1 < nbkeys) {
        k1 = k0 + 1;
    }
    if (n + 2 < nbkeys) {
        key = k1 + 1;
    }
    len = k1->keyframe - k0->keyframe;
    t = LINK_FRAME(B_8C88FEF0) - (Float)(Sint32)k0->keyframe;
    if (len > 0) {
        r = _builtin_fsrra((Float)len);
        r *= r;
        t *= r;
    }
    out[0] = km->key;
    out[1] = k0->key;
    out[2] = k1->key;
    out[3] = key->key;
    return t;
}
#else
ASM_STUB(Float func_8C040F7A(NJS_MKEY_P *key, Uint32 n, Uint32 nbkeys, NJS_POINT3 **out))
#endif
