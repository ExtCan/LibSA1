#include "sa1/sa1.h"

/*
 * Linked (blended) motion playback: per-node steps and the blend itself
 * (1ST_READ 8C040A60-8C042DC4 chunk; see code_8C040A60.c for the contexts).
 *  func_8C041666  node motion step for NJS_MDATA3 (motion B_8C88FF38)
 *  func_8C0416CC  node motion step for NJS_MDATA4
 *  func_8C041732  remember the points/normals of the node model
 *  func_8C04175C  shape motion step for NJS_MDATA1
 *  func_8C041792  shape motion step for NJS_MDATA2
 *  func_8C0417C8  blend pos/ang/scl of the two motions and apply them
 * The per-motion handlers B_8C88FE98/A0/A8 leave their result in
 * B_8C88FF4C (pos), B_8C88FFAC (ang) and B_8C88FF7C (scl) and set the flags
 * B_8C88FF40/42/44 (see func_8C041A2E, which runs both motions and then
 * func_8C0417C8).
 *
 * NON_MATCHING (all six): this translation unit (like 8C03FDC0) was not built
 * with the project compiler setup. It has a NOP after "MOV Rm,R0" (SHC
 * -extra bit 0x400, only seen in these two motion chunks) and computes
 * &ctx[(Sint8)i] as MULS.W + EXTS.B of the product, which only SHC
 * 5.0r31..5.1r01 emit (5.1r03 extends before the multiply). With
 * --ver 5.1r01 --flags=-extra=a=1C00 the four step functions differ only in
 * one or two scheduling ties. The blend uses FMUL+FMUL+FADD where SHC
 * fuses "a * s + b * r" into FMAC.
 */

/* Ninja motion data entries (one per node; SDK layout) */
typedef struct {
    void *p[1];                 /* 0x00 key lists */
    Uint32 nb[1];               /* 0x04 key counts */
} NJS_MDATA1;                   /* 0x08 */

typedef struct {
    void *p[2];                 /* 0x00 */
    Uint32 nb[2];               /* 0x08 */
} NJS_MDATA2;                   /* 0x10 */

typedef struct {
    void *p[3];                 /* 0x00 */
    Uint32 nb[3];               /* 0x0C */
} NJS_MDATA3;                   /* 0x18 */

typedef struct {
    void *p[4];                 /* 0x00 */
    Uint32 nb[4];               /* 0x10 */
} NJS_MDATA4;                   /* 0x20 */

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

SA1_ASSERT_SIZE(NJS_MDATA1, 0x08);
SA1_ASSERT_SIZE(NJS_MDATA2, 0x10);
SA1_ASSERT_SIZE(NJS_MDATA3, 0x18);
SA1_ASSERT_SIZE(NJS_MDATA4, 0x20);
SA1_ASSERT_OFFSET(NJS_MDATA3, nb, 0x0C);
SA1_ASSERT_OFFSET(NJS_MDATA4, nb, 0x10);
SA1_ASSERT_SIZE(MOTION_CTX, 0x1C);
SA1_ASSERT_OFFSET(MOTION_CTX, nb, 0x08);
SA1_ASSERT_OFFSET(MOTION_CTX, idx, 0x14);

typedef void (*MOTION_FUNC)(NJS_OBJECT *object);
typedef void (*ROTATE_FUNC)(NJS_MATRIX_PTR m, Angle angx, Angle angy, Angle angz);

extern MOTION_CTX B_8C88FEB8[2];        /* node motion contexts */
extern MOTION_CTX B_8C88FEF0[2];        /* shape motion contexts */
extern Uint16 B_8C88FF38;               /* motion being evaluated (0/1) */
extern MOTION_FUNC B_8C88FE98[2];       /* translate handlers */
extern MOTION_FUNC B_8C88FEA0[2];       /* rotate handlers */
extern MOTION_FUNC B_8C88FEA8[2];       /* scale handlers */
extern Float B_8C88FF3C;                /* blend ratio of motion 1 */
extern Uint16 B_8C88FF40;               /* pos written by a handler */
extern Uint16 B_8C88FF42;               /* ang written by a handler */
extern Uint16 B_8C88FF44;               /* scl written by a handler */
extern NJS_POINT3 B_8C88FF4C[2];        /* evaluated pos per motion */
extern NJS_POINT3 B_8C88FF7C[2];        /* evaluated scl per motion */
extern Angle3 B_8C88FFAC[2];            /* evaluated ang per motion */
extern NJS_POINT3 *B_8C88FFFC[2];       /* model points per motion */
extern NJS_VECTOR *B_8C89000C[2];       /* model normals per motion */
extern ROTATE_FUNC D_8C19AD44;          /* njRotateXYZ or njRotateZXY */

ASM_STUB(void func_8C041378(NJS_OBJECT *object))

#ifdef NON_MATCHING
/* node motion step for NJS_MDATA3 (pos, ang, scl) */
void func_8C041666(NJS_OBJECT *object)
{
    MOTION_CTX *mp;
    NJS_MDATA3 *md;

    mp = &B_8C88FEB8[(Sint8)B_8C88FF38];
    md = mp->mdata;
    mp->p = md->p;
    mp->nb = md->nb;
    mp->idx = 0;
    B_8C88FE98[B_8C88FF38](object);
    B_8C88FEA0[B_8C88FF38](object);
    B_8C88FEA8[B_8C88FF38](object);
    mp->mdata = md + 1;
}
#else
ASM_STUB(void func_8C041666(NJS_OBJECT *object))
#endif

#ifdef NON_MATCHING
/* node motion step for NJS_MDATA4 */
void func_8C0416CC(NJS_OBJECT *object)
{
    MOTION_CTX *mp;
    NJS_MDATA4 *md;

    mp = &B_8C88FEB8[(Sint8)B_8C88FF38];
    md = mp->mdata;
    mp->p = md->p;
    mp->nb = md->nb;
    mp->idx = 0;
    B_8C88FE98[B_8C88FF38](object);
    B_8C88FEA0[B_8C88FF38](object);
    B_8C88FEA8[B_8C88FF38](object);
    mp->mdata = md + 1;
}
#else
ASM_STUB(void func_8C0416CC(NJS_OBJECT *object))
#endif

#ifdef NON_MATCHING
/* remember the vertex/normal arrays of a visible node model */
void func_8C041732(NJS_OBJECT *object)
{
    NJS_MODEL *model;

    model = object->model;
    if (model != NULL && !(object->evalflags & 8)) {
        B_8C88FFFC[B_8C88FF38] = model->points;
        B_8C89000C[B_8C88FF38] = model->normals;
    }
}
#else
ASM_STUB(void func_8C041732(NJS_OBJECT *object))
#endif

#ifdef NON_MATCHING
/* shape motion step for NJS_MDATA1 */
void func_8C04175C(NJS_OBJECT *object)
{
    MOTION_CTX *mp;
    NJS_MDATA1 *md;

    mp = &B_8C88FEF0[(Sint8)B_8C88FF38];
    md = mp->mdata;
    mp->p = md->p;
    mp->nb = md->nb;
    mp->idx = 0;
    func_8C041378(object);
    mp->mdata = md + 1;
}
#else
ASM_STUB(void func_8C04175C(NJS_OBJECT *object))
#endif

#ifdef NON_MATCHING
/* shape motion step for NJS_MDATA2 */
void func_8C041792(NJS_OBJECT *object)
{
    MOTION_CTX *mp;
    NJS_MDATA2 *md;

    mp = &B_8C88FEF0[(Sint8)B_8C88FF38];
    md = mp->mdata;
    mp->p = md->p;
    mp->nb = md->nb;
    mp->idx = 0;
    func_8C041378(object);
    mp->mdata = md + 1;
}
#else
ASM_STUB(void func_8C041792(NJS_OBJECT *object))
#endif

#ifdef NON_MATCHING
/*
 * Blend the transforms of the two motions with ratio B_8C88FF3C (motion 0
 * alone when a component was not written) and apply them to the current
 * matrix. Original bug kept: the scale blend reads the pos buffers.
 */
void func_8C0417C8(void)
{
    NJS_POINT3 pos;
    NJS_POINT3 scl;
    Angle3 ang;
    Float r;
    Float s;

    if (B_8C88FF40 > 0) {
        r = B_8C88FF3C;
        s = 1.0f - r;
        pos.x = B_8C88FF4C[0].x * s + B_8C88FF4C[1].x * r;
        pos.y = B_8C88FF4C[0].y * s + B_8C88FF4C[1].y * r;
        pos.z = B_8C88FF4C[0].z * s + B_8C88FF4C[1].z * r;
    } else {
        pos.x = B_8C88FF4C[0].x;
        pos.y = B_8C88FF4C[0].y;
        pos.z = B_8C88FF4C[0].z;
    }
    if (B_8C88FF42 > 0) {
        r = B_8C88FF3C;
        s = 1.0f - r;
        ang.x = SA1_FTOI((Float)B_8C88FFAC[0].x * s + (Float)B_8C88FFAC[1].x * r);
        ang.y = SA1_FTOI((Float)B_8C88FFAC[0].y * s + (Float)B_8C88FFAC[1].y * r);
        ang.z = SA1_FTOI((Float)B_8C88FFAC[0].z * s + (Float)B_8C88FFAC[1].z * r);
    } else {
        ang.x = B_8C88FFAC[0].x;
        ang.y = B_8C88FFAC[0].y;
        ang.z = B_8C88FFAC[0].z;
    }
    if (B_8C88FF44 > 0) {
        r = B_8C88FF3C;
        s = 1.0f - r;
        scl.x = B_8C88FF4C[0].x * s + B_8C88FF4C[1].x * r;
        scl.y = B_8C88FF4C[0].y * s + B_8C88FF4C[1].y * r;
        scl.z = B_8C88FF4C[0].z * s + B_8C88FF4C[1].z * r;
    } else {
        scl.x = B_8C88FF7C[0].x;
        scl.y = B_8C88FF7C[0].y;
        scl.z = B_8C88FF7C[0].z;
    }
    njTranslate(NULL, pos.x, pos.y, pos.z);
    D_8C19AD44(NULL, ang.x, ang.y, ang.z);
    njScale(NULL, scl.x, scl.y, scl.z);
}
#else
ASM_STUB(void func_8C0417C8(void))
#endif
