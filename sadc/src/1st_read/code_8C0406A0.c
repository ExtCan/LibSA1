#include "sa1/sa1.h"

/*
 * Motion playback: per-node motion steps and the object tree walk
 * (1ST_READ 8C03FDC0-8C040A60 chunk; see code_8C03FDC0.c, code_8C03FF90.c,
 * code_8C040596.c, code_8C040840.c).
 *  func_8C0406A0  node motion step for NJS_MDATA3 (pos/ang/scl callbacks)
 *  func_8C0406E0  node motion step for NJS_MDATA4
 *  func_8C040720  draw the model of a node unless hidden (evalflags bit 3)
 *  func_8C04073C  shape motion step for NJS_MDATA1
 *  func_8C040760  shape motion step for NJS_MDATA2
 *  func_8C040784  recursive njDrawMotion-like walk of an NJS_OBJECT tree
 *
 * NON_MATCHING (all functions) with the project compiler (SHC 5.1r03
 * -cpu=sh4): only the instruction scheduling differs. All six functions
 * MATCH exactly when this file is compiled with SHC 5.0r10 and
 *   -cpu=sh3e -endian=little -division=cpu -optimize=1 -speed -macsave=0
 * (5.0r10 has no -cpu=sh4/-fpu). This module (about 8C03FDC0-8C041D60) was
 * built with that older SH-3E compiler (see also code_8C041000.c). Its
 * scheduler depends on the instruction address modulo 4, so the functions
 * must sit at the original address parity in the object: func_8C0406A0 is
 * the first function of this file (offset 0, 8C0406A0 = 0 mod 4) and the
 * ASM_STUB for func_8C040498 is placed after the decompiled functions.
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

/* Motion playback context (B_8C88FE40 node motion, B_8C88FE5C shape motion) */
typedef struct {
    void *mdata;                /* 0x00 NJS_MDATAn entry of the current node */
    void **p;                   /* 0x04 key lists of the current entry */
    Uint32 *nb;                 /* 0x08 key counts of the current entry */
    Float frame;                /* 0x0C */
    Uint32 nbFrame;             /* 0x10 */
    Uint32 idx;                 /* 0x14 next key channel of the entry */
    Sint32 unk18;               /* 0x18 */
} MOTION_CTX;                   /* 0x1C */

SA1_ASSERT_SIZE(NJS_MDATA1, 0x08);
SA1_ASSERT_SIZE(NJS_MDATA2, 0x10);
SA1_ASSERT_SIZE(NJS_MDATA3, 0x18);
SA1_ASSERT_SIZE(NJS_MDATA4, 0x20);
SA1_ASSERT_OFFSET(NJS_MDATA3, nb, 0x0C);
SA1_ASSERT_SIZE(MOTION_CTX, 0x1C);
SA1_ASSERT_OFFSET(MOTION_CTX, nb, 0x08);
SA1_ASSERT_OFFSET(MOTION_CTX, idx, 0x14);

typedef void (*MOTION_FUNC)(NJS_OBJECT *object);

extern MOTION_CTX B_8C88FE40;   /* node motion context */
extern MOTION_CTX B_8C88FE5C;   /* shape motion context (= B_8C88FE40 + 0x1C) */
extern MOTION_FUNC B_8C88FE24;  /* node motion step (per NJS_MDATAn type) */
extern MOTION_FUNC B_8C88FE2C;  /* translate callback */
extern MOTION_FUNC B_8C88FE30;  /* rotate callback */
extern MOTION_FUNC B_8C88FE34;  /* scale callback */
extern MOTION_FUNC B_8C890074;  /* optional per-node callback */

/* hand-written model draw (8C037098) */
extern void func_8C037098(NJS_MODEL *model);

/* shape motion evaluation of one node (asm, stub at the end of the file) */
void func_8C040498(NJS_OBJECT *object);

#ifdef NON_MATCHING
void func_8C0406A0(NJS_OBJECT *object)
{
    NJS_MDATA3 *md;

    md = B_8C88FE40.mdata;
    B_8C88FE40.p = md->p;
    B_8C88FE40.nb = md->nb;
    B_8C88FE40.idx = 0;
    B_8C88FE2C(object);
    B_8C88FE30(object);
    B_8C88FE34(object);
    B_8C88FE40.mdata = md + 1;
}
#else
ASM_STUB(void func_8C0406A0(NJS_OBJECT *object))
#endif

#ifdef NON_MATCHING
void func_8C0406E0(NJS_OBJECT *object)
{
    NJS_MDATA4 *md;

    md = B_8C88FE40.mdata;
    B_8C88FE40.p = md->p;
    B_8C88FE40.nb = md->nb;
    B_8C88FE40.idx = 0;
    B_8C88FE2C(object);
    B_8C88FE30(object);
    B_8C88FE34(object);
    B_8C88FE40.mdata = md + 1;
}
#else
ASM_STUB(void func_8C0406E0(NJS_OBJECT *object))
#endif

#ifdef NON_MATCHING
void func_8C040720(NJS_OBJECT *object)
{
    if (object->model != NULL && !(object->evalflags & 8)) {
        func_8C037098(object->model);
    }
}
#else
ASM_STUB(void func_8C040720(NJS_OBJECT *object))
#endif

#ifdef NON_MATCHING
void func_8C04073C(NJS_OBJECT *object)
{
    NJS_MDATA1 *md;

    md = B_8C88FE5C.mdata;
    B_8C88FE5C.p = md->p;
    B_8C88FE5C.nb = md->nb;
    B_8C88FE5C.idx = 0;
    func_8C040498(object);
    B_8C88FE5C.mdata = md + 1;
}
#else
ASM_STUB(void func_8C04073C(NJS_OBJECT *object))
#endif

#ifdef NON_MATCHING
void func_8C040760(NJS_OBJECT *object)
{
    NJS_MDATA2 *md;

    md = B_8C88FE5C.mdata;
    B_8C88FE5C.p = md->p;
    B_8C88FE5C.nb = md->nb;
    B_8C88FE5C.idx = 0;
    func_8C040498(object);
    B_8C88FE5C.mdata = md + 1;
}
#else
ASM_STUB(void func_8C040760(NJS_OBJECT *object))
#endif

#ifdef NON_MATCHING
/*
 * Original bug: the angle pointer of the static (evalflags 0x40) branch is
 * never set. SHC allocated the uninitialised variable to R15, so the game
 * rotates by the three words at the stack pointer (the saved PR, R8 and R9)
 * instead of object->ang. The shape-motion twin func_8C040880 uses
 * object->ang; the PC build does the same (PC_ONLY).
 */
void func_8C040784(NJS_OBJECT *object)
{
    Uint32 flags;
    Float *v;
    Angle *a;
    Float x;
    Float y;
    Float z;
    Angle ax;
    Angle ay;
    Angle az;
    void (*rotate)(NJS_MATRIX_PTR m, Angle angx, Angle angy, Angle angz);

    do {
        flags = object->evalflags;
        njPushMatrix(NULL);
        if (!(flags & 0x40)) {
            B_8C88FE24(object);
        } else {
            v = object->pos;
            x = *v++;
            y = *v++;
            z = *v;
            njTranslate(NULL, x, y, z);
            rotate = njRotateXYZ;
            if (flags & 0x20) {
                rotate = njRotateZXY;
            }
            PC_ONLY(a = object->ang;)
            ax = a[0];
            ay = a[1];
            az = a[2];
            rotate(NULL, ax, ay, az);
            v = object->scl;
            x = *v++;
            y = *v++;
            z = *v;
            njScale(NULL, x, y, z);
        }
        if (B_8C890074 != NULL) {
            B_8C890074(object);
        }
        if (object->model != NULL && !(flags & 8)) {
            func_8C037098(object->model);
        }
        if (object->child != NULL) {
            func_8C040784(object->child);
        }
        njPopMatrix(1);
        object = object->sibling;
    } while (object != NULL);
}
#else
ASM_STUB(void func_8C040784(NJS_OBJECT *object))
#endif

ASM_STUB(void func_8C040498(NJS_OBJECT *object))
