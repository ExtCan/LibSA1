#include "common.h"
#include "ninja.h"
#include "sa1/shc_intrinsics.h"

/*
 * 8C03FDC0-8C040A60: the game's own motion player (njDrawMotion /
 * njDrawShapeMotion style). B_8C88FE40 is the node-motion context,
 * B_8C88FE5C the shape-motion context; func_8C03FE60 picks the per-node
 * position/angle/scale handlers from the motion type and the object's
 * evaluation flags.
 *
 * BUILD NOTE: this module was NOT compiled like the rest of 1ST_READ.
 *   - func_8C03FDC0, func_8C03FE1A, func_8C03FE60, func_8C03FEB8 and
 *     func_8C03FEF2 below are byte-identical (tools/shcdiff.compare) when this
 *     file is compiled with SHC 5.0 Release 10 and
 *         -cpu=sh3e -endian=little -division=cpu -optimize=1 -extra=a=400
 *     (no -speed). Bit 0x400 of -extra is what puts a NOP after every
 *     "MOV Rm,R0". With the project's 5.1r03 -cpu=sh4 flags they cannot
 *     match (SH-4 scheduling, global constant CSE, no NOP after MOV Rm,R0).
 *   - The output was then edited by hand and assembled with asmsh: alignment
 *     fill is "09 00" (asmsh) instead of the SHC object writer's "00 09", the
 *     literal pool sits behind a BRA at 8C040020, FSRRA (.DATA.W H'Fn7D, not
 *     an SH-3E instruction) is used in func_8C03FF2C/8C03FF90/8C040200/
 *     8C040240, FIPR FV4,FV4 works on registers in func_8C040240, and
 *     func_8C040784 reads its own register save area (@R15, @(4,R15),
 *     @(8,R15)) as njRotate angles. func_8C03FF2C is therefore not
 *     reproducible from C.
 * Until the build can select a compiler/flags per file, every function here
 * stays NON_MATCHING (the original assembly is kept).
 */

/* Ninja motion keys (SDK layout) */
typedef struct {
    Uint32 keyframe;            /* 0x00 */
    Float key[3];               /* 0x04 */
} NJS_MKEY_F;                   /* 0x10 */

typedef struct {
    Uint32 keyframe;            /* 0x00 */
    NJS_POINT3 *key;            /* 0x04 vertex/normal array of this key */
} NJS_MKEY_P;                   /* 0x08 */

/* Motion playback context (B_8C88FE40 node motion, B_8C88FE5C shape motion) */
typedef struct {
    void *mdata;                /* 0x00 current NJS_MDATAn entry */
    void **p;                   /* 0x04 key lists of the current entry */
    Uint32 *nb;                 /* 0x08 key counts of the current entry */
    Float frame;                /* 0x0C */
    Uint32 nbFrame;             /* 0x10 */
    Uint32 idx;                 /* 0x14 element (pos/ang/scl) index in the entry */
    Sint32 unk18;               /* 0x18 cleared on setup, use unknown */
} MOTION_CTX;                   /* 0x1C */

SA1_ASSERT_SIZE(NJS_MKEY_F, 0x10);
SA1_ASSERT_SIZE(NJS_MKEY_P, 0x08);
SA1_ASSERT_SIZE(MOTION_CTX, 0x1C);
SA1_ASSERT_OFFSET(MOTION_CTX, frame, 0x0C);
SA1_ASSERT_OFFSET(MOTION_CTX, nbFrame, 0x10);
SA1_ASSERT_OFFSET(MOTION_CTX, idx, 0x14);

/* NJS_MOTION.type bits (Ninja NJD_MTYPE_*) */
#ifndef NJD_MTYPE_POS_0
#define NJD_MTYPE_POS_0     0x0001
#define NJD_MTYPE_ANG_1     0x0002
#define NJD_MTYPE_SCL_2     0x0004
#define NJD_MTYPE_VERT_4    0x0010
#define NJD_MTYPE_NORM_5    0x0020
#endif
/* NJS_OBJECT.evalflags: rotation order Z-X-Y */
#ifndef NJD_EVAL_ZXY_ANG
#define NJD_EVAL_ZXY_ANG    0x0020
#endif

typedef void (*MOTION_FUNC)(NJS_OBJECT *object);

extern MOTION_CTX B_8C88FE40;
extern MOTION_CTX B_8C88FE5C;
extern MOTION_FUNC B_8C88FE24;      /* per-node dispatcher (by interpolation type) */
extern MOTION_FUNC B_8C88FE28;      /* shape (vertex/normal) handler */
extern MOTION_FUNC B_8C88FE2C;      /* position handler */
extern MOTION_FUNC B_8C88FE30;      /* rotation handler */
extern MOTION_FUNC B_8C88FE34;      /* scale handler */
extern Uint16 B_8C749100;           /* node motion inp_fn & 0xFF3F */
extern Uint16 B_8C749102;           /* node motion type */
extern Uint16 B_8C749104;           /* shape handler index */
extern Uint16 B_8C749106;           /* shape motion type */
extern MOTION_FUNC D_8C19ACE0[];    /* 5 node dispatchers, indexed by inp_fn */
extern MOTION_FUNC D_8C19ACF4[];    /* 4 shape handlers: none/vert/norm/both */

void func_8C0400A0(NJS_OBJECT *object);
void func_8C0400F8(NJS_OBJECT *object);
void func_8C040150(NJS_OBJECT *object);
void func_8C0401A8(NJS_OBJECT *object);
void func_8C04057A(NJS_OBJECT *object);
void func_8C040588(NJS_OBJECT *object);
void func_8C040596(NJS_OBJECT *object);
void func_8C0405A4(NJS_OBJECT *object);

#ifdef NON_MATCHING
/*
 * Set up the shape motion context for frame. Returns 1 (and changes nothing)
 * when frame is not below motion->nbFrame, else 0. motion may be NULL
 * (no shape motion: only the frame is set and the "none" handler selected).
 */
Sint32 func_8C03FDC0(NJS_MOTION *motion, Float frame)
{
    MOTION_CTX *ctx;
    Uint16 idx;
    Uint16 type;

    ctx = &B_8C88FE5C;
    idx = 0;
    type = idx;
    if (motion != NULL) {
        if (frame >= (Float)(Sint32)motion->nbFrame) {
            return 1;
        }
        ctx->mdata = motion->mdata;
        ctx->nbFrame = motion->nbFrame;
        type = motion->type;
    }
    ctx->frame = frame;
    ctx->idx = 0;
    ctx->unk18 = 0;
    if (type & NJD_MTYPE_VERT_4) {
        idx = 1;
    }
    if (type & NJD_MTYPE_NORM_5) {
        idx += 2;
    }
    B_8C88FE28 = D_8C19ACF4[idx];
    B_8C749106 = type;
    B_8C749104 = idx;
    return 0;
}
#else
ASM_STUB(Sint32 func_8C03FDC0(NJS_MOTION *motion, Float frame))
#endif

#ifdef NON_MATCHING
/*
 * Set up the node motion context for frame. Returns 1 when frame is not
 * below motion->nbFrame, else 0. motion may be NULL.
 */
Sint32 func_8C03FE1A(NJS_MOTION *motion, Float frame)
{
    MOTION_CTX *ctx;
    Uint16 type;
    Uint16 inp;

    ctx = &B_8C88FE40;
    type = 0;
    inp = type;
    if (motion != NULL) {
        if (frame >= (Float)(Sint32)motion->nbFrame) {
            return 1;
        }
        ctx->mdata = motion->mdata;
        ctx->nbFrame = motion->nbFrame;
        inp = motion->inp_fn & 0xFF3F;
        type = motion->type;
    }
    ctx->frame = frame;
    ctx->idx = 0;
    ctx->unk18 = 0;
    B_8C749100 = inp;
    B_8C749102 = type;
    return 0;
}
#else
ASM_STUB(Sint32 func_8C03FE1A(NJS_MOTION *motion, Float frame))
#endif

#ifdef NON_MATCHING
/* Select the node dispatcher and the position/rotation/scale handlers. */
void func_8C03FE60(NJS_OBJECT *object)
{
    Uint16 type;
    Uint32 zxy;
    MOTION_FUNC f;

    type = B_8C749102;
    B_8C88FE24 = D_8C19ACE0[B_8C749100];
    f = func_8C04057A;
    if (type & NJD_MTYPE_POS_0) {
        f = func_8C0400A0;
    }
    B_8C88FE2C = f;
    zxy = object->evalflags & NJD_EVAL_ZXY_ANG;
    if (type & NJD_MTYPE_ANG_1) {
        f = func_8C040150;
        if (zxy) {
            f = func_8C0401A8;
        }
    } else {
        f = func_8C040588;
        if (zxy) {
            f = func_8C040596;
        }
    }
    B_8C88FE30 = f;
    f = func_8C0405A4;
    if (type & NJD_MTYPE_SCL_2) {
        f = func_8C0400F8;
    }
    B_8C88FE34 = f;
}
#else
ASM_STUB(void func_8C03FE60(NJS_OBJECT *object))
#endif

#ifdef NON_MATCHING
/*
 * Binary search in a Float key list for the last key whose keyframe is
 * <= the node motion frame (truncated); the key index is stored to *out.
 * The byte-offset form (t = mid * sizeof, then reload t) is what makes SHC
 * reuse R0 for the loaded keyframe.
 */
void func_8C03FEB8(NJS_MKEY_F *key, Uint32 nbkeys, Uint32 *out)
{
    Uint32 lo;
    Uint32 mid;
    Uint32 t;
    Sint32 frame;

    lo = 0;
    frame = SA1_FTOI(B_8C88FE40.frame);
    while (nbkeys - lo - 1 > 0) {
        mid = (nbkeys + lo) >> 1;
        t = mid * (Uint32)sizeof(NJS_MKEY_F);
        t = ((NJS_MKEY_F *)((Uint8 *)key + t))->keyframe;
        if ((Uint32)frame >= t) {
            lo = mid;
        } else {
            nbkeys = mid;
        }
    }
    *out = lo;
}
#else
ASM_STUB(void func_8C03FEB8(NJS_MKEY_F *key, Uint32 nbkeys, Uint32 *out))
#endif

#ifdef NON_MATCHING
/* Same search in a shape (NJS_MKEY_P) key list at the shape motion frame. */
void func_8C03FEF2(NJS_MKEY_P *key, Uint32 nbkeys, Uint32 *out)
{
    Uint32 lo;
    Uint32 mid;
    Uint32 t;
    Sint32 frame;

    lo = 0;
    frame = SA1_FTOI(B_8C88FE5C.frame);
    while (nbkeys - lo - 1 > 0) {
        mid = (nbkeys + lo) >> 1;
        t = mid * (Uint32)sizeof(NJS_MKEY_P);
        t = ((NJS_MKEY_P *)((Uint8 *)key + t))->keyframe;
        if ((Uint32)frame >= t) {
            lo = mid;
        } else {
            nbkeys = mid;
        }
    }
    *out = lo;
}
#else
ASM_STUB(void func_8C03FEF2(NJS_MKEY_P *key, Uint32 nbkeys, Uint32 *out))
#endif

#ifdef NON_MATCHING
/*
 * Linear interpolation of key n and the next key (key[0] after the last one)
 * at the node motion frame; out[0..2] = k0 * (1 - t) + k1 * t with
 * t = (frame - k0.keyframe) / (k1.keyframe - k0.keyframe) when that span is
 * positive. Hand-edited in the original: 1/span is FSRRA(span) squared
 * (approximate), and the stores are pre-decrement (out += 3; *--out).
 */
void func_8C03FF2C(NJS_MKEY_F *key, Uint32 n, Uint32 nbkeys, Float *out)
{
    NJS_MKEY_F *k0;
    Float *p0;
    Float *p1;
    Sint32 len;
    Float t;
    Float s;
    Float r;
    Float x;
    Float y;
    Float z;

    k0 = &key[n];
    if (n + 1 < nbkeys) {
        key = k0 + 1;
    }
    t = B_8C88FE40.frame - (Float)(Sint32)k0->keyframe;
    len = key->keyframe - k0->keyframe;
    if (len > 0) {
        r = _builtin_fsrra((Float)len);
        t *= r * r;
    }
    s = 1.0f - t;
    p0 = k0->key;
    p1 = key->key;
    x = *p0++ * s;
    x += *p1++ * t;
    y = *p0++ * s;
    y += *p1++ * t;
    z = *p0 * s;
    z += *p1 * t;
    out += 3;
    *--out = z;
    *--out = y;
    *--out = x;
}
#else
ASM_STUB(void func_8C03FF2C(NJS_MKEY_F *key, Uint32 n, Uint32 nbkeys, Float *out))
#endif
