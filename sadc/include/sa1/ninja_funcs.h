#ifndef SA1_NINJA_FUNCS_H
#define SA1_NINJA_FUNCS_H

/*
 * The 68 SDK (Ninja / Shinobi) functions called by the engine, player and
 * character code (docs/research/sa1_code_map.md 5.1), with prototypes
 * inferred from usage: argument registers set at the call sites (R4-R7
 * integers/pointers, FR4-FR7 floats), result register read after the call
 * (R0 / FR0), and the callee body. "n=" is the number of call sites in
 * 1ST_READ. SDK code is never decompiled into libsa1: these are reimplemented
 * from behaviour for the PC build (docs/libsa1_plan.md 7).
 *
 * Names:
 *  - nj... / pd... names are the SDK names (identified by sa1_code_map.md; a few
 *    marked "verify"). They are the real symbol names in config/symbols.1st_read.txt,
 *    so both the SHC build and TARGET_PC link these names directly; on PC they are
 *    functions of the libsa1 runtime (reimplemented, never decompiled).
 *    njSin is 8C63A9B8 and njCos is 8C63A970: FSCA leaves the sine in FR[n] and the
 *    cosine in FR[n+1] (verified in the interpreter harness).
 *  - func_XXXXXXXX: role known, SDK name not established; the PC runtime
 *    implements them under the address name.
 *
 * Fidelity notes for the reimplementation: njSin/njCos/njTan use FSCA
 * (16-bit BAMS angle, hardware table); njUnitVector uses FSRRA; njInnerProduct
 * and the matrix code use FIPR/FTRV; njSqrt is signed (-sqrt(-x) for x < 0).
 */

#include "sa1/ninja_types.h"


/* ---- vectors and matrices (Ninja math, 8C638EAC-8C63A9F4) ---- */
void njAddVector(NJS_VECTOR *v1, const NJS_VECTOR *v2);                 /* 8C638EAC R4,R5 n=74 */
void njCalcPoint(NJS_MATRIX_PTR m, const NJS_POINT3 *ps, NJS_POINT3 *pd); /* 8C638ECC R4-R6 n=140 */
void njCalcVector(NJS_MATRIX_PTR m, const NJS_VECTOR *vs, NJS_VECTOR *vd); /* 8C638F28 R4-R6 n=171 */
void njGetMatrix(NJS_MATRIX_PTR m);                                     /* 8C638F94 R4 n=9 */
Float njInnerProduct(const NJS_VECTOR *v1, const NJS_VECTOR *v2);       /* 8C639090 R4,R5 -> FR0 n=88; FIPR */
Sint32 njInvertMatrix(NJS_MATRIX_PTR m);                                /* 8C6390B0 R4 n=20; result unused */
void njMultiMatrix(NJS_MATRIX_PTR md, NJS_MATRIX_PTR ms);               /* 8C63997C R4,R5 n=22 */
Float njOuterProduct(const NJS_VECTOR *v1, const NJS_VECTOR *v2, NJS_VECTOR *ov); /* 8C639B50 R4-R6 -> FR0 n=12 */
Sint32 njPopMatrix(Uint32 n);                                           /* 8C639B98 R4 -> R0 n=458; 0 when depth - n < 1 (CMP/HS: unsigned), else 1; callers ignore it */
void njProjectScreen(NJS_MATRIX_PTR m, const NJS_POINT3 *p3, NJS_POINT2 *p2); /* 8C639BDC R4-R6 n=7 */
Sint32 njPushMatrix(NJS_MATRIX_PTR m);                                  /* 8C639C70 R4 -> R0 n=464; 0 when the stack is full (depth B_8C88F69C >= limit B_8C88F698), else 1; callers ignore it */
void njRotateXYZ(NJS_MATRIX_PTR m, Angle angx, Angle angy, Angle angz); /* 8C639CF4 R4-R7 n=34 */
void njRotateX(NJS_MATRIX_PTR m, Angle ang);                            /* 8C639EC8 R4,R5 n=341 */
void njRotateY(NJS_MATRIX_PTR m, Angle ang);                            /* 8C639F5C R4,R5 n=515 */
void njRotateZXY(NJS_MATRIX_PTR m, Angle angx, Angle angy, Angle angz); /* 8C639FF8 R4-R7 n=27; verify order vs 8C639CF4 */
void njRotateZ(NJS_MATRIX_PTR m, Angle ang);                            /* 8C63A1CC R4,R5 n=315 */
void njRotate(NJS_MATRIX_PTR m, const NJS_VECTOR *v, Angle ang);        /* 8C63A268 R4-R6 n=3 (axis rotation) */
void njScaleV(NJS_MATRIX_PTR m, const NJS_VECTOR *v);                   /* 8C63A5EC R4,R5 n=31 */
void njScale(NJS_MATRIX_PTR m, Float sx, Float sy, Float sz);           /* 8C63A69C R4,FR4-FR6 n=165 */
Float njScalor2(const NJS_VECTOR *v);                                   /* 8C63A748 R4 -> FR0 n=15 */
Float njScalor(const NJS_VECTOR *v);                                    /* 8C63A75C R4 -> FR0 n=184 */
void njSetMatrix(NJS_MATRIX_PTR md, NJS_MATRIX_PTR ms);                 /* 8C63A770 R4,R5 n=24 */
void njSubVector(NJS_VECTOR *v1, const NJS_VECTOR *v2);                 /* 8C63A7E4 R4,R5 n=61 */
void njTranslateV(NJS_MATRIX_PTR m, const NJS_VECTOR *v);               /* 8C63A804 R4,R5 n=231 */
void njTranslate(NJS_MATRIX_PTR m, Float x, Float y, Float z);          /* 8C63A878 R4,FR4-FR6 n=159 */
void njUnitMatrix(NJS_MATRIX_PTR m);                                    /* 8C63A8E0 R4 n=67 */
Float njUnitVector(NJS_VECTOR *v);                                      /* 8C63A94C R4 -> FR0 n=186; FSRRA */
Float njSin(Angle n);                                                   /* 8C63A9B8 R4 -> FR0 n=161; FSCA */
Float njRandom(Float n);                                                /* 8C63A980 FR4 -> FR0 n=5: misnamed, it is a fraction (modff) helper: tail-calls 8C10EB50(n, &B_8C89033C) with FR4 untouched (code_8C04B7BA.c) */
Float njCos(Angle n);                                                   /* 8C63A970 R4 -> FR0 n=188; FSCA */
Float njSqrt(Float n);                                                  /* 8C63A9C4 FR4 -> FR0 n=89; n < 0: -sqrt(-n) */
Float njTan(Angle n);                                                   /* 8C63A9F4 R4 -> FR0 n=4 */

/* ---- Ninja matrix stack state (Ninja library bss/data) ---- */
extern NJS_MATRIX_PTR B_8C88F5F8;   /* current matrix of the Ninja stack (njPushMatrix 8C639C70; code_8C057B40.c, code_8C0572D8.c) */
extern Sint32 B_8C88F698;           /* matrix stack depth limit (signed CMP/GE) */
extern Sint32 B_8C88F69C;           /* matrix stack depth */
extern NJS_MATRIX D_8C67C640;       /* unit matrix (the base passed to njPushMatrix; probably _nj_unit_matrix_; declared locally in ~20 files) */
#define njCollisionCheckSS func_8C63FE54

/* ---- geometry helpers (8C63FE54-8C640128) ---- */
Sint32 func_8C63FE54(NJS_SPHERE *s1, NJS_SPHERE *s2);                   /* njCollisionCheckSS: r1 + r2 > distance (MOVT), R4,R5 only (code_8C057460.c) */
void func_8C63FEAC(void *a, void *b, void *c);                          /* R4-R6 n=6 */
Float func_8C63FF84(NJS_POINT3 *p, NJS_LINE *l, NJS_POINT3 *cp);        /* R4-R6 -> FR0 n=2: point-line distance (tail-calls njSqrt; code_8C02B0B4.c) */
Float func_8C64016C(NJS_PLANE *pl1, NJS_PLANE *pl2, NJS_LINE *l);       /* R4-R6 -> FR0 n=6: plane-plane intersection line into *l (njDistancePL2PL-like; 8C07266E, 8C075A40, 8C075F8C, 8C0763A0); FR0 unused */
Float njDistanceP2L(const NJS_POINT3 *p, const NJS_LINE *l, NJS_POINT3 *cp); /* 8C640080 R4-R6 n=20 */
Float njDistanceP2P(const NJS_POINT3 *p1, const NJS_POINT3 *p2);        /* 8C640128 R4,R5 -> FR0 n=62 */

/* ---- drawing (replaced by the libsa1 mesh emitter; kept for linking) ---- */
void njDrawLine3D(NJS_POINT3COL *p, Int n, Uint32 attr);                /* 8C63CE24 R4-R6 n=17 */
void func_8C63D414(NJS_POINT3COL *p, Int n, Uint32 attr);               /* R4-R6 n=9; 3D polygon draw (njDrawTriangle3D-like?) */
void func_8C63D66C(NJS_POINT3COL *p, Int n, Uint32 attr);               /* R4-R6 n=5; 3D polygon draw (njDrawPolygon3D-like?) */
void njDrawSprite2D(NJS_SPRITE *sp, Int n, Float pri, Uint32 attr);     /* 8C63EEB4 R4-R6,FR4 n=42 */
void njDrawSprite3D(NJS_SPRITE *sp, Int n, Uint32 attr);                /* 8C63EF74 R4-R6 n=48 */
void func_8C63FAE8(void);                                               /* render-state switch, n=8 */
void func_8C63FB18(void);                                               /* render-state switch, n=8 */
void njColorBlendingMode(Int target, Int mode);                         /* 8C605628 R4,R5 n=238 */
void func_8C605DAC(void);                                               /* save draw state (paired with 8C605E0A) n=49 */
void func_8C605E0A(void);                                               /* restore draw state n=48 */
void func_8C60E080(void *a, void *b, Float f);                          /* model draw variant R4,R5,FR4 n=2 */
void func_8C60E49A(void *a, Float f);                                   /* model draw variant R4,FR4 n=2 */
void func_8C60EDE0(Uint32 v);                                           /* stores one render global, n=6 */
void njSetConstantAttr(Uint32 and_attr, Uint32 or_attr);                /* 8C60EDE6 R4,R5 n=8 (stores two globals) */
void njSetConstantMaterial(NJS_ARGB *mat);                              /* 8C60EDF0 R4 n=85 */
void njDrawModel(NJS_MODEL *model);                                     /* 8C611028 R4 n=8 (8C605DAC; draw; 8C605E0A) */
void njDrawObject(NJS_OBJECT *object);                                  /* 8C61103C R4 n=8 */
void func_8C6215D6(void *model);                                        /* alternative model draw R4 n=21 */
void njPrintColor(Uint32 color);                                        /* 8C640766 R4 n=32 */
void njPrintC(Int loc, const char *str);                                /* 8C640922 R4,R5 n=173 (CCL_Init: "CANT GET COLLI_WORK") */
void func_8C640A80(Int loc, Int value, Int digits);                     /* R4-R6 n=15; njPrintD/H-like */

/* ---- textures ---- */
Sint32 njSetTexture(NJS_TEXLIST *texlist);                              /* 8C608CCC R4 n=175 */
Sint32 njSetTextureNum(Uint32 n);                                       /* 8C608D14 R4 n=5 */
Sint32 func_8C608C82(NJS_TEXLIST *texlist);                             /* texture load R4 -> R0 n=19 */
Sint32 func_8C6088BC(NJS_TEXLIST *texlist);                             /* texture release R4 -> R0 n=46 (reads textures[i].texaddr) */

/* ---- system ---- */
const void *pdGetPeripheral(Uint32 port);                               /* 8C601874 R4 -> R0 n=9; ports 0/6/12/18 = A0..D0 (8C050BE4); PDS_PERIPHERAL * (sa1/input.h) */
Sint32 func_8C603998(Uint32 icon_num, Uint32 visual_type, Uint32 save_size); /* backup (VMU) file size in blocks, R4-R6 -> R0 n=2 (buCalcBackupFileSize-like): R4/R5 checked CMP/HI #3 as 32-bit unsigned, -256 for a bad argument */
void func_8C604500(void *dst, const void *src, Uint32 n);               /* byte copy R4-R6 n=31 (CCL_Init copies CCL_INFO with it) */
void func_8C604546(void);                                               /* n=1 */
void func_8C60D1F8(Uint32 v);                                           /* stores one global, n=2 */
Sint32 func_8C64133E(void *handler, Uint32 arg);                        /* R4,R5 -> R0 n=3: registers handler (a void (*)(void), e.g. func_8C06C1D8 from 8C06C0C0) + arg in the SDK table D_8C67C004 slot 0 (+0x230/+0x234) via 8C6412C2; NULL removes it; returns 0 */

#endif