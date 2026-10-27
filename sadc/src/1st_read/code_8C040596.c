#include "sa1/sa1.h"

/*
 * Motion playback node callbacks (1ST_READ 8C03FDC0-8C040A60 chunk; see
 * code_8C03FDC0.c, code_8C03FF90.c, code_8C0406A0.c).
 *  func_8C040596  per-node rotate callback without motion keys (njRotateZXY)
 *  func_8C0405A4  per-node scale callback without motion keys (njScale)
 *  func_8C0405B2  run the translate/rotate/scale callbacks of one node
 *  func_8C0405D2  same for NJS_MDATA1 motion data, then step to the next node
 *  func_8C040612  same for NJS_MDATA2 motion data, then step to the next node
 * The callbacks B_8C88FE2C/30/34 are installed by func_8C03FE60.
 * 8C040660 (func_8C040660) is not a function: it is SHC's mid-file literal
 * pool (BRA over the pool + .ALIGN 32 fill) shared by 8C0403C0-8C040612.
 */

/* Ninja motion data per node (SDK layout) */
typedef struct {
    void *p[1];                 /* 0x00 key arrays */
    Uint32 nb[1];               /* 0x04 key counts */
} NJS_MDATA1;                   /* 0x08 */

typedef struct {
    void *p[2];                 /* 0x00 key arrays */
    Uint32 nb[2];               /* 0x08 key counts */
} NJS_MDATA2;                   /* 0x10 */

/* Motion evaluation state (B_8C88FE40, B_8C88FE5C: two 0x1C instances) */
typedef struct {
    void *mdata;                /* 0x00 NJS_MDATAn of the current node */
    void **p;                   /* 0x04 key arrays of the current node */
    Uint32 *nb;                 /* 0x08 key counts of the current node */
    Float frame;                /* 0x0C */
    Uint32 nbFrame;             /* 0x10 */
    Sint32 idx;                 /* 0x14 next key channel */
    Sint32 unk18;               /* 0x18 */
} MOTION_EVAL;                  /* 0x1C */

SA1_ASSERT_SIZE(NJS_MDATA1, 0x08);
SA1_ASSERT_SIZE(NJS_MDATA2, 0x10);
SA1_ASSERT_OFFSET(NJS_MDATA2, nb, 0x08);
SA1_ASSERT_SIZE(MOTION_EVAL, 0x1C);
SA1_ASSERT_OFFSET(MOTION_EVAL, p, 0x04);
SA1_ASSERT_OFFSET(MOTION_EVAL, nb, 0x08);
SA1_ASSERT_OFFSET(MOTION_EVAL, idx, 0x14);

typedef void (*MOTION_FUNC)(NJS_OBJECT *obj);

extern MOTION_EVAL B_8C88FE40;
extern MOTION_FUNC B_8C88FE2C;  /* translate callback */
extern MOTION_FUNC B_8C88FE30;  /* rotate callback */
extern MOTION_FUNC B_8C88FE34;  /* scale callback */

/*
 * The original keeps the angle pointer in R4 and loads x, y, z in order;
 * that needs the temporaries, read in exactly this order (y, z, x).
 */
void func_8C040596(NJS_OBJECT *obj)
{
    Angle *a;
    Angle x;
    Angle y;
    Angle z;

    a = obj->ang;
    y = a[1];
    z = a[2];
    x = a[0];
    njRotateZXY(NULL, x, y, z);
}

#ifdef NON_MATCHING
/*
 * Instruction-identical except that the original loads the njScale address
 * before ADD #32,R4 (MOV.L pool,R3 / ADD #32,R4); this source gives the reverse
 * order. The same form matches the translate twin func_8C04057A. Indexed
 * reads (s[0], s[1], s[2]) give the pool-first order but @(R0,R4) loads, and
 * passing obj->scl[i] directly as arguments keeps obj in R14 with a dummy
 * stack slot.
 */
void func_8C0405A4(NJS_OBJECT *obj)
{
    Float *s;
    Float x;
    Float y;
    Float z;

    s = obj->scl;
    x = *s++;
    y = *s++;
    z = *s;
    njScale(NULL, x, y, z);
}

/*
 * Instruction-identical except scheduling of the literal-pool loads: the
 * original pushes R14/PR before loading &B_8C88FE2C and, at the end, does
 * MOV R14,R4 / LDS.L @R15+,PR before loading &B_8C88FE34; SHC 5.1r03 hoists
 * both pool loads. Not changed by the SHC version, -extra bits, volatile or
 * const globals, array/struct callbacks or a #pragma inline helper. The same
 * pool-load scheduling difference shows in every function of this TU
 * (func_8C03FE60, func_8C0406A0, func_8C04073C, ...).
 */
void func_8C0405B2(NJS_OBJECT *obj)
{
    B_8C88FE2C(obj);
    B_8C88FE30(obj);
    B_8C88FE34(obj);
}

/*
 * Instruction-identical except the prologue scheduling: the original runs
 * MOV #0,R3 / MOV.L &B_8C88FE2C,R1 before all pushes and loads
 * &B_8C88FE40 into R14 only after MOV R4,R12; ours interleaves the R14 and
 * R1 loads with the pushes (same issue as func_8C0405B2).
 */
void func_8C0405D2(NJS_OBJECT *obj)
{
    MOTION_EVAL *mp;
    NJS_MDATA1 *md;

    mp = &B_8C88FE40;
    md = mp->mdata;
    mp->p = md->p;
    mp->nb = md->nb;
    mp->idx = 0;
    B_8C88FE2C(obj);
    B_8C88FE30(obj);
    B_8C88FE34(obj);
    mp->mdata = md + 1;
}

/* As func_8C0405D2 for NJS_MDATA2 (same prologue scheduling difference). */
void func_8C040612(NJS_OBJECT *obj)
{
    MOTION_EVAL *mp;
    NJS_MDATA2 *md;

    mp = &B_8C88FE40;
    md = mp->mdata;
    mp->p = md->p;
    mp->nb = md->nb;
    mp->idx = 0;
    B_8C88FE2C(obj);
    B_8C88FE30(obj);
    B_8C88FE34(obj);
    mp->mdata = md + 1;
}
#endif
