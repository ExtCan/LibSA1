#include "sa1/sa1.h"

/*
 * 1ST_READ 8C0414CC-8C041666 (8C040A60 chunk: linked motion playback,
 * see code_8C040A60.c).
 *  func_8C0414CC  default rotate handler: the node's own ang
 *  func_8C0414F0  default scale handler: the node's own scl
 *  func_8C041516  run the translate/rotate/scale handlers of the motion
 *                 being evaluated (B_8C88FF38)
 *  func_8C04154E  node motion step for NJS_MDATA1
 *  func_8C041600  node motion step for NJS_MDATA2
 *
 * func_8C0415C0 is not a function: it is SHC's mid-file literal pool
 * (".ALIGN 32; BRA over the pool; NOP; pool; .ALIGN 32") shared by the
 * functions 8C0414A6-8C0415B4; it stays in the assembly chunk.
 *
 * Compiler: the motion translation unit 8C03FDC0-8C042E20 was built for
 * SH-3E, not SH-4, and with bit 0x400 of -extra=a ("NOP after MOV Rm,R0":
 * 8C04156E, 8C041620; this TU is the only game code with that pattern).
 * Evidence: with "-cpu=sh3e -extra=a=1C00" (SHC 5.1r03) four of these five
 * functions MATCH and the fifth differs only by one register, while with
 * the project flags (-cpu=sh4 -extra=a=1800) the SH-4 scheduler moves pool
 * loads into the prologue and fills load-latency slots differently; the
 * FSRRA at 8C040C96 is a raw .DATA.W H'F57D (no SH-4 instruction set). The
 * build has no per-file flags yet, so everything is kept under NON_MATCHING:
 *   py -3.12 tools/shcdiff.py <func> src/1st_read/code_8C0414CC.c
 *       --flags="-define=NON_MATCHING -cpu=sh3e -extra=a=1C00"
 *   func_8C0414CC  MATCH
 *   func_8C0414F0  2 instructions differ (&B_8C88FF7C loaded into R1
 *                  instead of R0; no source form found)
 *   func_8C041516  MATCH
 *   func_8C04154E  MATCH
 *   func_8C041600  MATCH
 *
 * The original copies through inlined helpers (the argument copies
 * MOV R4,R5 / ADD #imm,R5 put the source in R5 and the destination index
 * in R4), and it truncates the context byte offset n * 0x1C to Sint8
 * (EXTS.B after MULS.W), which MOTION_CTX_AT reproduces under SHC.
 */

/* Ninja motion data entries (one per node; SDK layout) */
typedef struct {
    void *p[1];                 /* 0x00 */
    Uint32 nb[1];               /* 0x04 */
} NJS_MDATA1;                   /* 0x08 */

typedef struct {
    void *p[2];                 /* 0x00 */
    Uint32 nb[2];               /* 0x08 */
} NJS_MDATA2;                   /* 0x10 */

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
SA1_ASSERT_SIZE(MOTION_CTX, 0x1C);
SA1_ASSERT_OFFSET(MOTION_CTX, idx, 0x14);

typedef void (*MOTION_FUNC)(NJS_OBJECT *object);

extern MOTION_CTX B_8C88FEB8[2];    /* node motion contexts */
extern Uint16 B_8C88FF38;           /* index of the motion being evaluated */
extern MOTION_FUNC B_8C88FE98[2];   /* translate handlers */
extern MOTION_FUNC B_8C88FEA0[2];   /* rotate handlers */
extern MOTION_FUNC B_8C88FEA8[2];   /* scale handlers */
extern NJS_POINT3 B_8C88FF7C[2];    /* scale of the node, per motion */
extern Angle3 B_8C88FFAC[2];        /* rotation of the node, per motion */

/* context n of tbl; n is 0 or 1, so the byte offset always fits in Sint8 */
#ifdef TARGET_PC
#define MOTION_CTX_AT(tbl, n) (&(tbl)[(Sint8)(n)])
#else
#define MOTION_CTX_AT(tbl, n) \
    ((MOTION_CTX *)((Sint8 *)(tbl) + (Sint8)((Sint8)(n) * (Sint32)sizeof(MOTION_CTX))))
#endif

#ifdef NON_MATCHING

#ifndef TARGET_PC
#pragma inline(CopyAngle3, CopyPoint3)
#endif

static void CopyAngle3(Angle *d, Angle *s)
{
    Angle x;
    Angle y;
    Angle z;

    x = s[0];
    y = s[1];
    z = s[2];
    d[0] = x;
    d[1] = y;
    d[2] = z;
}

static void CopyPoint3(Float *d, Float *s)
{
    Float x;
    Float y;
    Float z;

    x = *s++;
    y = *s++;
    z = *s;
    d += 3;
    *--d = z;
    *--d = y;
    *--d = x;
}

/* default rotate handler: the node's own angles */
void func_8C0414CC(NJS_OBJECT *object)
{
    CopyAngle3(&B_8C88FFAC[B_8C88FF38].x, object->ang);
}

/* default scale handler: the node's own scale */
void func_8C0414F0(NJS_OBJECT *object)
{
    CopyPoint3(&B_8C88FF7C[B_8C88FF38].x, object->scl);
}

void func_8C041516(NJS_OBJECT *object)
{
    B_8C88FE98[B_8C88FF38](object);
    B_8C88FEA0[B_8C88FF38](object);
    B_8C88FEA8[B_8C88FF38](object);
}

/* node motion step for NJS_MDATA1 */
void func_8C04154E(NJS_OBJECT *object)
{
    MOTION_CTX *ctx;
    NJS_MDATA1 *md;

    ctx = MOTION_CTX_AT(B_8C88FEB8, B_8C88FF38);
    md = ctx->mdata;
    ctx->p = md->p;
    ctx->nb = md->nb;
    ctx->idx = 0;
    B_8C88FE98[B_8C88FF38](object);
    B_8C88FEA0[B_8C88FF38](object);
    B_8C88FEA8[B_8C88FF38](object);
    ctx->mdata = md + 1;
}

/* node motion step for NJS_MDATA2 */
void func_8C041600(NJS_OBJECT *object)
{
    MOTION_CTX *ctx;
    NJS_MDATA2 *md;

    ctx = MOTION_CTX_AT(B_8C88FEB8, B_8C88FF38);
    md = ctx->mdata;
    ctx->p = md->p;
    ctx->nb = md->nb;
    ctx->idx = 0;
    B_8C88FE98[B_8C88FF38](object);
    B_8C88FEA0[B_8C88FF38](object);
    B_8C88FEA8[B_8C88FF38](object);
    ctx->mdata = md + 1;
}

#endif /* NON_MATCHING */
