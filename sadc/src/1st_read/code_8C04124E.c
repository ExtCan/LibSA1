#include "sa1/sa1.h"

/*
 * 1ST_READ 8C04124E-8C0414CC: shape (vertex/normal morph) part of the linked
 * motion playback (NJS_MOTION_LINK, see code_8C040A60.c). Each of the two
 * blended motions (index B_8C88FF38) interpolates its shape keys into its own
 * part of the scratch buffer B_8C89002C and records the resulting arrays in
 * B_8C88FFFC / B_8C89000C (vertices / normals); B_8C88FF46 / B_8C88FF48 get
 * bit (index + 1) when the motion has vertex / normal keys.
 *
 * 8C041300-8C041340 (func_8C041300 in the asm) is not a function: it is
 * SHC's mid-file literal pool (BRA over the pool, NOP, pool, .ALIGN 32 NOP
 * fill) between func_8C0412C8 and func_8C041340 (same pattern as
 * code_8C040A40.c). It holds the pool entries of 8C041040-8C04124E and stays
 * in the assembly chunk.
 *
 * None of the functions matches yet (all are NON_MATCHING, see the notes at
 * each function). The remaining differences are instruction scheduling and
 * register choice that no tried source form reproduced:
 *  - after a call, the original keeps the result store (FMOV.S FR0,@R8)
 *    first and loads the pool addresses late; SHC 5.1r03 hoists the pool
 *    loads above the store (func_8C04124E and its siblings 8C0410D0..8C0411D4);
 *  - the loops keep the Float rate in a stack slot (ADD #-4; FMOV.S FR4,@R15,
 *    reloaded every iteration) where SHC 5.1r03 picks FR15; SHC 5.0r26/r28
 *    reproduce the original allocation, but 8C0409C0 of the same unit matches
 *    only with 5.1r03.
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
SA1_ASSERT_OFFSET(MOTION_CTX, frame, 0x0C);
SA1_ASSERT_OFFSET(MOTION_CTX, idx, 0x14);

extern MOTION_CTX B_8C88FEF0[2];     /* shape motion contexts */
extern Uint16 B_8C88FF30[2];         /* shape mode: 1 vertices, 2 normals, 3 both */
extern Uint16 B_8C88FF38;            /* index of the motion being evaluated */
extern Uint16 B_8C88FF48;            /* bit (n + 1): motion n has normal keys */
extern NJS_POINT3 B_8C88FF4C[2];     /* node position of each motion */
extern NJS_POINT3 *B_8C88FFFC[2];    /* vertex array of each motion */
extern NJS_VECTOR *B_8C89000C[2];    /* normal array of each motion */
extern NJS_POINT3 *B_8C89002C;       /* shape scratch buffer */

/*
 * &B_8C88FEF0[B_8C88FF38]. The game computes the byte offset truncated to
 * Sint8 (MOV.B, MULS.W #28, EXTS.B); same value for the indices 0 and 1
 * (also on PC, where the context is larger).
 */
#define SHAPE_CTX \
    ((MOTION_CTX *)((Sint8 *)B_8C88FEF0 + (Sint8)(B_8C88FF38 * sizeof(MOTION_CTX))))

/* binary search of the key at the shape motion frame */
ASM_STUB(void func_8C040AAA(NJS_MKEY_P *key, Uint32 nbkeys, Uint32 *out))
/* the four keys around key n (n - 1 .. n + 2, wrapping); returns the rate */
ASM_STUB(Float func_8C040F7A(NJS_MKEY_P *key, Uint32 n, Uint32 nbkeys, NJS_POINT3 **out))
/* normal interpolation: out = normalize(p0 * (1 - t) + p1 * t) */
ASM_STUB(void func_8C041040(NJS_VECTOR *p0, NJS_VECTOR *p1, NJS_VECTOR *out, Float t))
/* point interpolation: out = p0 * (1 - t) + p1 * t */
ASM_STUB(void func_8C0410A4(NJS_POINT3 *p0, NJS_POINT3 *p1, NJS_POINT3 *out, Float t))
/* vertex keys of the current shape element (mode 1) */
ASM_STUB(void func_8C0410D0(Sint32 *count, Float *rate, NJS_POINT3 **out, NJS_POINT3 *buf))
/* normal keys of the current shape element (mode 2) */
ASM_STUB(void func_8C041152(Sint32 *count, Float *rate, NJS_VECTOR **out, NJS_VECTOR *buf))
/* vertex keys of the current shape element (mode 3) */
ASM_STUB(void func_8C0411D4(Sint32 *count, Float *rate, NJS_POINT3 **out, NJS_POINT3 *buf))

#ifdef NON_MATCHING
/*
 * Normal keys of the current shape element (mode 3): key pair and rate,
 * normals go to buf, *count += 2. Registers and stack slots match; 21
 * scheduling differences (pool loads, see the file comment).
 */
void func_8C04124E(Sint32 *count, Float *rate, NJS_VECTOR **out, NJS_VECTOR *buf)
{
    MOTION_CTX *mp;
    NJS_MKEY_P *key;
    Uint32 i;
    Uint32 nb;
    Uint32 n;

    mp = SHAPE_CTX;
    i = mp->idx;
    key = (NJS_MKEY_P *)mp->p[i];
    if (key != NULL) {
        nb = mp->nb[i];
        func_8C040AAA(key, nb, &n);
        *rate = func_8C040F7A(key, n, nb, out);
        B_8C89000C[B_8C88FF38] = buf;
        B_8C88FF48 |= B_8C88FF38 + 1;
        *count += 2;
    }
}
#else
ASM_STUB(void func_8C04124E(Sint32 *count, Float *rate, NJS_VECTOR **out, NJS_VECTOR *buf))
#endif

#ifdef NON_MATCHING
/*
 * dst[0..n-1] = interpolation of the vertices of keys src[1] and src[2].
 * The original keeps rate in a stack slot (SHC 5.1r03 uses FR15) and
 * schedules the increments before DT.
 */
void func_8C0412C8(NJS_POINT3 **src, NJS_POINT3 *dst, Sint32 n, Float rate)
{
    NJS_POINT3 *p0;
    NJS_POINT3 *p1;
    Sint32 i;

    p0 = src[1];
    p1 = src[2];
    i = n;
    do {
        func_8C0410A4(p0, p1, dst, rate);
        p0++;
        p1++;
        dst++;
    } while (--i != 0);
}
#else
ASM_STUB(void func_8C0412C8(NJS_POINT3 **src, NJS_POINT3 *dst, Sint32 n, Float rate))
#endif

#ifdef NON_MATCHING
/* dst[0..n-1] = interpolation of the normals of keys src[1] and src[2] (as func_8C0412C8) */
void func_8C041340(NJS_VECTOR **src, NJS_VECTOR *dst, Sint32 n, Float rate)
{
    NJS_VECTOR *p0;
    NJS_VECTOR *p1;
    Sint32 i;

    p0 = src[1];
    p1 = src[2];
    i = n;
    do {
        func_8C041040(p0, p1, dst, rate);
        p0++;
        p1++;
        dst++;
    } while (--i != 0);
}
#else
ASM_STUB(void func_8C041340(NJS_VECTOR **src, NJS_VECTOR *dst, Sint32 n, Float rate))
#endif

#ifdef NON_MATCHING
/*
 * Shape motion of one node for linked motion B_8C88FF38 (the per-node
 * handler of the shape motion link): interpolates the vertex and/or normal
 * keys of the current element into this motion's part of B_8C89002C
 * (vertices at [index * nbPoint * 2], normals after them) and records the
 * arrays in B_8C88FFFC / B_8C89000C (the model's own arrays when the motion
 * has no keys for the node).
 * Not matching: the original computes the buffer offsets as
 * 6 * (index * n) << 2 and (3 * n + 6 * (index * n)) << 2, keeps srcv in R9
 * and srcn in R10, and increments the context index through
 * @(R0,Rn) with R0 = &B_8C88FEF0[0].idx (no source form found).
 */
void func_8C041378(NJS_OBJECT *obj)
{
    NJS_POINT3 *srcv[4];
    NJS_VECTOR *srcn[4];
    Float ratev;
    Float raten;
    Sint32 flag;
    NJS_MODEL *model;
    NJS_POINT3 *bufv;
    NJS_VECTOR *bufn;
    Sint32 n;

    model = obj->model;
    if (model == NULL || (obj->evalflags & 8)) {
        return;
    }
    flag = 0;
    /* the rates are only read when the key functions set them (flag) */
    PC_ONLY(ratev = 0.0f; raten = 0.0f;)
    n = obj->model->nbPoint;
    bufv = &B_8C89002C[B_8C88FF38 * n * 2];
    bufn = &B_8C89002C[B_8C88FF38 * n * 2 + n];
    B_8C88FFFC[B_8C88FF38] = model->points;
    B_8C89000C[B_8C88FF38] = model->normals;
    switch (B_8C88FF30[B_8C88FF38]) {
    case 1:
        func_8C0410D0(&flag, &ratev, srcv, bufv);
        break;
    case 2:
        func_8C041152(&flag, &raten, srcn, bufn);
        break;
    case 3:
        func_8C0411D4(&flag, &ratev, srcv, bufv);
        SHAPE_CTX->idx++;
        func_8C04124E(&flag, &raten, srcn, bufn);
        break;
    }
    switch (flag) {
    case 1:
        func_8C0412C8(srcv, bufv, n, ratev);
        break;
    case 2:
        func_8C041340(srcn, bufn, n, raten);
        break;
    case 3:
        func_8C0412C8(srcv, bufv, n, ratev);
        func_8C041340(srcn, bufn, n, raten);
        break;
    }
}
#else
ASM_STUB(void func_8C041378(NJS_OBJECT *obj))
#endif

#ifdef NON_MATCHING
/*
 * Default translate handler of a linked motion: the node's own position.
 * Not matching: the original loads &B_8C88FF4C into R0 (R1 here) and
 * interleaves the loads differently (10 instructions differ).
 */
void func_8C0414A6(NJS_OBJECT *obj)
{
    Float *src;
    Float *dst;
    Float x;
    Float y;
    Float z;

    src = obj->pos;
    dst = &B_8C88FF4C[B_8C88FF38].x + 3;
    x = *src++;
    y = *src++;
    z = *src;
    *--dst = z;
    *--dst = y;
    *--dst = x;
}
#else
ASM_STUB(void func_8C0414A6(NJS_OBJECT *obj))
#endif
