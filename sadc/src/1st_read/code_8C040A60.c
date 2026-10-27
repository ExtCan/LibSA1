#include "sa1/sa1.h"

/*
 * 8C040A60-8C040CD0: linked (blended) motion playback setup, the
 * NJS_MOTION_LINK counterpart of the motion code in code_8C03FDC0.c
 * (njDrawMotionLink / njDrawShapeMotionLink style; callers 8C041C92,
 * 8C041D68, 8C041DC6). Two motions are evaluated per node and blended; each
 * has its own evaluation context:
 *   B_8C88FEB8[2]  node motion contexts (0x1C each, same layout as B_8C88FE40)
 *   B_8C88FEF0[2]  shape motion contexts
 *   B_8C88FF38     index of the motion being evaluated (0 or 1)
 *   B_8C88FF28[2]  inp_fn & 0xFF3F, B_8C88FF2C[2] type (node motions)
 *   B_8C88FF30[2]  shape handler index, B_8C88FF34[2] type (shape motions)
 *   B_8C88FE80/88/98/A0/A8[2]  per-motion handlers (interpolation, shape,
 *                  translate, rotate, scale)
 *   D_8C19AD44     rotation function (njRotateXYZ or njRotateZXY)
 *
 * NON_MATCHING (all six functions; the C is complete and functionally
 * faithful, the differences are register allocation and scheduling).
 * This object does not seem to be built like the rest of the game:
 *  - every "MOV Rm,R0" in 8C03FDC0-8C0417AC is followed by a NOP (SHC
 *    -extra bit 0x400, which docs/compiler.md rules out for the game); from
 *    8C041EB4 on the NOPs stop (and FMAC appears), so 8C040A60.src probably
 *    holds two objects;
 *  - the binary searches have the non-rotated loop shape (BRA to the test)
 *    that SHC 5.1r03 only emits without -speed;
 *  - 0 is rematerialised where 5.1r03 keeps it in a callee-saved register.
 * Best results found (shcdiff --flags="-nospeed -extra=a=1C00"):
 *  func_8C040A60 17/37 differing with hi = the nbkeys parameter, the frame
 *    read as *(Float *)((Sint8 *)&B_8C88FEC4 + (Sint8)(B_8C88FF38 * 28))
 *    (a separate symbol for &B_8C88FEB8[0].frame folds the base into one
 *    pool load) and the key read as ((Uint32 *)key)[mid * 4]; the typed form
 *    below gives 36;
 *  func_8C040C02 21/45 (scheduling only, same with every flag set tried);
 *  func_8C040B90 50/57, func_8C040AF4 79/78 (5.1r03 keeps the constant 0 in
 *    R11/R14, the original rematerialises it);
 *  func_8C040C5C 50/58 (FSRRA: asmsh cannot assemble it, checked with the
 *    opcode patched to .DATA.W).
 * Each context is addressed with a signed-char byte offset: MULS.W n,#0x1C
 * then EXTS.B of the product, i.e. (Sint8)(n * 0x1C); MLINK_CTX reproduces
 * that on SHC (n is 0 or 1, so plain indexing is equivalent on the PC).
 */

/* Ninja motion keys (SDK layout) */
typedef struct {
    Uint32 keyframe;            /* 0x00 */
    Float key[3];               /* 0x04 */
} NJS_MKEY_F;                   /* 0x10 */

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
SA1_ASSERT_SIZE(NJS_MKEY_P, 0x08);
SA1_ASSERT_SIZE(MOTION_CTX, 0x1C);
SA1_ASSERT_OFFSET(MOTION_CTX, frame, 0x0C);
SA1_ASSERT_OFFSET(MOTION_CTX, idx, 0x14);

typedef void (*MOTION_FUNC)(NJS_OBJECT *object);
typedef void (*ROTATE_FUNC)(NJS_MATRIX_PTR m, Angle angx, Angle angy, Angle angz);

/* context n of a context pair */
#if defined(TARGET_PC)
#define MLINK_CTX(tbl, n) (&(tbl)[n])
#else
#define MLINK_CTX(tbl, n) ((MOTION_CTX *)((Sint8 *)(tbl) + (Sint8)((n) * 0x1C)))
#endif

extern MOTION_CTX B_8C88FEB8[2];
extern MOTION_CTX B_8C88FEF0[2];
extern Uint16 B_8C88FF28[2];
extern Uint16 B_8C88FF2C[2];
extern Uint16 B_8C88FF30[2];
extern Uint16 B_8C88FF34[2];
extern Uint16 B_8C88FF38;
extern Uint16 B_8C89002A;
extern MOTION_FUNC B_8C88FE80[2];
extern MOTION_FUNC B_8C88FE88[2];
extern MOTION_FUNC B_8C88FE98[2];
extern MOTION_FUNC B_8C88FEA0[2];
extern MOTION_FUNC B_8C88FEA8[2];
extern MOTION_FUNC D_8C19AD20[];
extern MOTION_FUNC D_8C19AD34[];
extern ROTATE_FUNC D_8C19AD44;

void func_8C040DF0(NJS_OBJECT *object);
void func_8C040E74(NJS_OBJECT *object);
void func_8C040EF8(NJS_OBJECT *object);
void func_8C0414A6(NJS_OBJECT *object);
void func_8C0414CC(NJS_OBJECT *object);
void func_8C0414F0(NJS_OBJECT *object);

#ifdef NON_MATCHING
/* binary search of the key at the current node motion frame (16-byte keys) */
void func_8C040A60(NJS_MKEY_F *key, Uint32 hi, Uint32 *out)
{
    Uint32 lo;
    Uint32 mid;
    Sint32 frame;

    lo = 0;
    frame = SA1_FTOI(MLINK_CTX(B_8C88FEB8, B_8C88FF38)->frame);
    while (hi - lo - 1 > 0) {
        mid = (hi + lo) >> 1;
        if (frame >= key[mid].keyframe) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    *out = lo;
}
#endif

#ifdef NON_MATCHING
/* binary search of the key at the current shape motion frame (8-byte keys) */
void func_8C040AAA(NJS_MKEY_P *key, Uint32 hi, Uint32 *out)
{
    Uint32 lo;
    Uint32 mid;
    Sint32 frame;

    lo = 0;
    frame = SA1_FTOI(MLINK_CTX(B_8C88FEF0, B_8C88FF38)->frame);
    while (hi - lo - 1 > 0) {
        mid = (hi + lo) >> 1;
        if (frame >= key[mid].keyframe) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    *out = lo;
}
#endif

#ifdef NON_MATCHING
/* set up shape motion context n of a motion link; 1 when past the end */
Sint32 func_8C040AF4(NJS_MOTION_LINK *link, Sint32 n)
{
    MOTION_CTX *mp;
    NJS_MOTION *motion;
    Uint16 idx;
    Uint16 type;

    mp = MLINK_CTX(B_8C88FEF0, n);
    idx = 0;
    type = idx;
    motion = link->motion[n];
    if (motion != NULL) {
        if (link->frame[n] >= (Float)(Sint32)motion->nbFrame) {
            return 1;
        }
        mp->frame = link->frame[n];
        mp->mdata = motion->mdata;
        mp->nbFrame = motion->nbFrame;
        type = motion->type;
        B_8C89002A = (motion->inp_fn & 0xC0) > 0;
    }
    mp->idx = 0;
    mp->unk18 = 0;
    if (type & 0x10) {
        idx = 1;
    }
    if (type & 0x20) {
        idx += 2;
    }
    B_8C88FE88[n] = D_8C19AD34[idx];
    B_8C88FF34[n] = 0;
    B_8C88FF30[n] = idx;
    B_8C88FF34[n] = type;
    return 0;
}
#endif

#ifdef NON_MATCHING
/* set up node motion context n of a motion link; 1 when past the end */
Sint32 func_8C040B90(NJS_MOTION_LINK *link, Sint32 n)
{
    MOTION_CTX *mp;
    NJS_MOTION *motion;
    Uint16 type;
    Uint16 inp;

    mp = MLINK_CTX(B_8C88FEB8, n);
    type = 0;
    inp = type;
    motion = link->motion[n];
    if (motion != NULL) {
        if (link->frame[n] >= (Float)(Sint32)motion->nbFrame) {
            return 1;
        }
        mp->frame = link->frame[n];
        mp->mdata = motion->mdata;
        mp->nbFrame = motion->nbFrame;
        inp = motion->inp_fn & 0xFF3F;
        type = motion->type;
    }
    mp->idx = 0;
    mp->unk18 = 0;
    B_8C88FF28[n] = inp;
    B_8C88FF2C[n] = type;
    return 0;
}
#endif

#ifdef NON_MATCHING
/* select the per-node handlers of node motion n */
void func_8C040C02(NJS_OBJECT *object, Sint32 n)
{
    Uint32 type;
    MOTION_FUNC f;
    ROTATE_FUNC rot;

    B_8C88FE80[n] = D_8C19AD20[B_8C88FF28[n]];
    rot = njRotateXYZ;
    if (object->evalflags & 0x20) {
        rot = njRotateZXY;
    }
    D_8C19AD44 = rot;
    type = B_8C88FF2C[n];
    f = func_8C0414A6;
    if (type & 1) {
        f = func_8C040DF0;
    }
    B_8C88FE98[n] = f;
    f = func_8C0414CC;
    if (type & 2) {
        f = func_8C040EF8;
    }
    B_8C88FEA0[n] = f;
    f = func_8C0414F0;
    if (type & 4) {
        f = func_8C040E74;
    }
    B_8C88FEA8[n] = f;
}
#endif

#ifdef NON_MATCHING
/* linear interpolation of a Float key at the current node motion frame
 * (1/len as fsrra(len)^2). The original reuses the key parameter as the
 * second key and evaluates the products without FMAC (separate temporaries);
 * best result 50/58 differing (checked with FSRRA patched to .DATA.W). */
void func_8C040C5C(NJS_MKEY_F *key, Uint32 n, Uint32 nbkeys, Float *out)
{
    NJS_MKEY_F *k0;
    Float *p0;
    Float *p1;
    Sint32 len;
    Float t;
    Float r;
    Float s;
    Float a0, b0, a1, b1, a2, b2;

    k0 = &key[n];
    if (n + 1 < nbkeys) {
        key = k0 + 1;
    }
    len = key->keyframe - k0->keyframe;
    t = MLINK_CTX(B_8C88FEB8, B_8C88FF38)->frame - (Float)(Sint32)k0->keyframe;
    if (len > 0) {
        r = _builtin_fsrra((Float)len);
        r *= r;
        t *= r;
    }
    s = 1.0f - t;
    p0 = k0->key;
    p1 = key->key;
    a0 = *p0++ * s;
    b0 = *p1++ * t;
    a1 = *p0++ * s;
    b1 = *p1++ * t;
    a2 = *p0 * s;
    b2 = *p1 * t;
    out += 3;
    *--out = a2 + b2;
    *--out = a1 + b1;
    *--out = a0 + b0;
}
#endif
