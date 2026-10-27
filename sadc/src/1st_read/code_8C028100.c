#include "common.h"
#include "ninja.h"
#include "sa1/sa1.h"

/*
 * Land collision 8C028100-8C028428 (module 8C027360-8C02CE80): sphere (csts)
 * against the polygons of an NJS_OBJECT tree. func_8C02833A walks the tree
 * like njDrawObject; func_8C02770E (still asm) tests the polygons of each
 * node's model and records up to 16 hits in B_8C6BACF0; func_8C028100 then
 * copies the selected hit into the csts and averages onpoly/pshbk over all
 * hits. func_8C02833A has no callers in 1ST_READ (debug/unused path).
 *
 * Compiler: this module was not built like the rest of the game. 8C028280
 * and 8C02833A match with the project flags (5.1r03), but 8C028100 needs
 * SHC 5.0r31/5.0r32 (or 5.1r01 with -extra=a=800): 5.1r03 assigns the
 * registers of its 12-byte struct copies differently (docs/compiler.md t03)
 * and builds the 16-bit frame constant with MOV/SHLL8 instead of MOV.W.
 * It is therefore kept under NON_MATCHING until the build supports per-file
 * compiler versions:
 *   py -3.12 tools/shcdiff.py func_8C028100 src/1st_read/code_8C028100.c
 *       --ver 5.0r32 --flags=-define=NON_MATCHING          -> MATCH
 */

/* One polygon hit recorded by func_8C02770E (csts field names) */
typedef struct {
    Angle angx;                         /* 0x00 */
    Angle angz;                         /* 0x04 */
    NJS_POINT3 normal;                  /* 0x08 -> csts.normal */
    NJS_POINT3 normal2;                 /* 0x14 -> csts.normal2 */
    NJS_POINT3 onpoly;                  /* 0x20 averaged into csts.onpoly */
    NJS_POINT3 pshbk;                   /* 0x2C averaged into csts.pshbk */
    NJS_POINT3 anaspdh;                 /* 0x38 -> csts.anaspdh */
    NJS_POINT3 anaspdv;                 /* 0x44 -> csts.anaspdv */
    Float unk50;                        /* 0x50 */
    Float unk54;                        /* 0x54 */
} COLHIT;                               /* 0x58 */

/* Hit test state on func_8C02833A's stack (R6 of 8C028280/8C02770E) */
typedef struct {
    Sint32 hit;                         /* 0x00 set to 1 by 8C02770E on a hit; result */
    Sint32 nbHit;                       /* 0x04 entries used in list (max 16) */
    Sint32 nbCheck;                     /* 0x08 incremented by 8C02770E */
    Float range2;                       /* 0x0C D_8C112460[...] */
    Float spd;                          /* 0x10 njScalor(&cp->spd) */
    Float halfr;                        /* 0x14 cp->radius * 0.5f */
    COLHIT *list;                       /* 0x18 B_8C6BACF0 */
} COLHITCTX;                            /* 0x1C */

SA1_ASSERT_SIZE(COLHIT, 0x58);
SA1_ASSERT_OFFSET(COLHIT, onpoly, 0x20);
SA1_ASSERT_OFFSET(COLHIT, unk54, 0x54);
SA1_ASSERT_SIZE(COLHITCTX, 0x1C);
SA1_ASSERT_OFFSET(COLHITCTX, list, 0x18);

extern COLHIT B_8C6BACF0[];             /* 16 hits */
extern Float D_8C112460[];              /* 4 squared ranges: 220^2, 202^2, 101^2, 30.3^2 */

ASM_STUB(void func_8C02770E(csts *cp, NJS_OBJECT *obj, COLHITCTX *ctx))

#ifdef NON_MATCHING
/*
 * Takes the hit with the smallest |unk54| (the start value is |list[0].unk50|,
 * as in the original) and averages onpoly and pshbk over all nbHit hits.
 * Only called with ctx->nbHit > 0.
 */
void func_8C028100(csts *cp, COLHITCTX *ctx)
{
    Sint32 n;
    csts tmp;
    Sint32 i;
    Sint32 best;
    Float min;
    Float inv;
    COLHIT *hit;

    best = 0;
    min = _builtin_fabsf(ctx->list[0].unk50);
    for (i = 1; i < ctx->nbHit; i++) {
        if (min > _builtin_fabsf(ctx->list[i].unk54)) {
            best = i;
            min = _builtin_fabsf(ctx->list[i].unk54);
        }
    }
    cp->selected_nmb = best;
    hit = &ctx->list[best];
    cp->angx = hit->angx;
    cp->angz = hit->angz;
    cp->normal = hit->normal;
    cp->normal2 = hit->normal2;
    cp->anaspdh = hit->anaspdh;
    cp->anaspdv = hit->anaspdv;
    tmp.onpoly.x = tmp.onpoly.y = tmp.onpoly.z = 0.0f;
    tmp.pshbk.x = tmp.pshbk.y = tmp.pshbk.z = 0.0f;
    inv = 1.0f / (Float)ctx->nbHit;
    /* the "+=" are FMACs (SA1_FMAC(inv, x, acc) would change the SHC output) */
    for (n = ctx->nbHit, hit = ctx->list; n > 0; n--, hit++) {
        SA1_FMAC_ACC(tmp.onpoly.x, inv, hit->onpoly.x);
        SA1_FMAC_ACC(tmp.onpoly.y, inv, hit->onpoly.y);
        SA1_FMAC_ACC(tmp.onpoly.z, inv, hit->onpoly.z);
        SA1_FMAC_ACC(tmp.pshbk.x, inv, hit->pshbk.x);
        SA1_FMAC_ACC(tmp.pshbk.y, inv, hit->pshbk.y);
        SA1_FMAC_ACC(tmp.pshbk.z, inv, hit->pshbk.z);
    }
    cp->onpoly = tmp.onpoly;
    cp->pshbk = tmp.pshbk;
}
#else
ASM_STUB(void func_8C028100(csts *cp, COLHITCTX *ctx))
#endif

/*
 * One node of the tree walk: evaluates the node matrix (unless all of
 * NJD_EVAL_UNIT_POS/ANG/SCL are set), recurses into the children, tests the
 * node's polygons and returns the next sibling.
 * The rotations are written as "?:" expressions: SHC then keeps the angle in
 * a stack slot, as the original does.
 */
NJS_OBJECT *func_8C028280(csts *cp, NJS_OBJECT *obj, COLHITCTX *ctx)
{
    NJS_OBJECT *child;

    if ((obj->evalflags & 7) ^ 7) {
        njPushMatrix(NULL);
        if (!(obj->evalflags & 1)) {
            njTranslateV(NULL, (NJS_VECTOR *)obj->pos);
        }
        if (!(obj->evalflags & 2)) {
            obj->ang[2] ? njRotateZ(NULL, obj->ang[2]) : (void)0;
            obj->ang[1] ? njRotateY(NULL, obj->ang[1]) : (void)0;
            obj->ang[0] ? njRotateX(NULL, obj->ang[0]) : (void)0;
        }
        if (!(obj->evalflags & 4)) {
            njScaleV(NULL, (NJS_VECTOR *)obj->scl);
        }
    }
    for (child = obj->child; child != NULL; ) {
        child = func_8C028280(cp, child, ctx);
    }
    func_8C02770E(cp, obj, ctx);
    if ((obj->evalflags & 7) ^ 7) {
        njPopMatrix(1);
    }
    return obj->sibling;
}

/*
 * Sphere cp against the object tree obj (root placed by its own pos/ang/scl
 * from the unit matrix). Returns 1 on a hit; then cp->find_count is the
 * number of hits and the selected one is copied into cp (func_8C028100).
 * NOTE: the range index is (attr & 0x60000000) >> 13, i.e. 0, 0x10000,
 * 0x20000 or 0x30000, but D_8C112460 has 4 entries: only attrs without
 * bits 29-30 read inside the table (original behaviour, kept; a >> 29 was
 * probably intended).
 */
Sint32 func_8C02833A(csts *cp, NJS_OBJECT *obj, Uint32 attr)
{
    COLHITCTX ctx;
    NJS_OBJECT *child;

    ctx.hit = 0;
    ctx.nbHit = 0;
    ctx.nbCheck = 0;
    ctx.list = B_8C6BACF0;
    ctx.range2 = D_8C112460[(attr & 0x60000000) >> 13];
    ctx.spd = njScalor(&cp->spd);
    ctx.halfr = cp->radius * 0.5f;
    njPushMatrix(NULL);
    njUnitMatrix(NULL);
    if (!(obj->evalflags & 1)) {
        njTranslateV(NULL, (NJS_VECTOR *)obj->pos);
    }
    if (!(obj->evalflags & 2)) {
        obj->ang[2] ? njRotateZ(NULL, obj->ang[2]) : (void)0;
        obj->ang[1] ? njRotateY(NULL, obj->ang[1]) : (void)0;
        obj->ang[0] ? njRotateX(NULL, obj->ang[0]) : (void)0;
    }
    if (!(obj->evalflags & 4)) {
        njScaleV(NULL, (NJS_VECTOR *)obj->scl);
    }
    for (child = obj->child; child != NULL; ) {
        child = func_8C028280(cp, child, &ctx);
    }
    func_8C02770E(cp, obj, &ctx);
    if (ctx.nbHit > 0) {
        func_8C028100(cp, &ctx);
        cp->find_count = ctx.nbHit;
    }
    njPopMatrix(1);
    return ctx.hit;
}
