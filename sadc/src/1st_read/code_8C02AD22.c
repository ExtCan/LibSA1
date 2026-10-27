/*
 * 1ST_READ 8C02AD22: ground/ceiling probe over the gathered land-collision
 * list, for shadows (SADX zxsdwstr / xssunit layout). Second function of
 * the object 8C02A680-8C02AE78, part of the land collision module
 * 8C027360-8C02CE80.
 *
 * The caller (8C0BD8B2) copies twp->pos into zss.pos, calls
 * func_8C02AD22(&zss, 20.0f) and reads lower.findflag (== 1) and
 * lower.onpos (and upper.onpos). func_8C02BC60(zssp, obj, attr) probes one
 * land object: it clears both findflags of *zssp and fills the units it hits.
 *
 * Matching notes (NON_MATCHING):
 *  - Like the rest of the collision module (see code_8C028480.c), this
 *    object was built with SHC 5.0r31/5.0r32, not 5.1r03: the two 32-byte
 *    unit copies at the end are sequential single-register MOV.L load/store
 *    pairs, which 5.0r3x/5.1r01 produce and 5.1r03 does not (5.1r03
 *    interleaves struct copies in register pairs).
 *  - With --ver=5.0r32 the body below differs from the original only in
 *    loop-invariant motion: SHC hoists the address of func_8C02BC60 into R8
 *    (one extra push/pop, JSR @R8) while the original reloads it from the
 *    pool in every iteration (JSR @R2) and keeps R9-R14 only. Everything else
 *    (init sequence, loop rotation, the constant 1 in R12, register roles
 *    R9 n, R10 lc, R11 ret, R13 zssp, R14 &zss, the copies) is identical.
 *    Not reproduced: loop forms, declaration orders, register keywords,
 *    alias pointers and call temporaries all keep the hoist; a goto loop
 *    drops it but loses the loop weighting (registers swap).
 *  - Only findflag and onpos of the local results are written; the other
 *    fields (objatt, angx, angz, normal) are copied back to *zssp from the
 *    uninitialised local in the original. The PC build seeds the local
 *    from *zssp so those fields are defined.
 */
#include "common.h"
#include "ninja.h"
#include "sa1/collision.h"

/* one probe result (SADX xssunit); same layout as UNK_8C016C60_unit */
typedef struct xssunit {
    Sint32 findflag;            /* 0x00 1 = polygon found */
    Sint32 objatt;              /* 0x04 */
    Angle angx;                 /* 0x08 */
    Angle angz;                 /* 0x0C */
    Float onpos;                /* 0x10 polygon height at pos */
    NJS_VECTOR normal;          /* 0x14 */
} xssunit;                      /* 0x20 */

/* probe position + two results (SADX zxsdwstr) */
typedef struct zxsdwstr {
    NJS_POINT3 pos;             /* 0x00 */
    xssunit lower;              /* 0x0C */
    xssunit upper;              /* 0x2C */
} zxsdwstr;                     /* 0x4C */

SA1_ASSERT_SIZE(xssunit, 0x20);
SA1_ASSERT_OFFSET(xssunit, onpos, 0x10);
SA1_ASSERT_SIZE(zxsdwstr, 0x4C);
SA1_ASSERT_OFFSET(zxsdwstr, lower, 0x0C);
SA1_ASSERT_OFFSET(zxsdwstr, upper, 0x2C);

/* probe one land object (R4 zssp, R5 obj, R6 attr); returns 1 on a hit */
extern Sint32 func_8C02BC60(zxsdwstr *zssp, NJS_OBJECT *obj, Uint32 attr);

#ifdef NON_MATCHING
/*
 * Gather the land objects within r of zssp->pos and probe each one, keeping
 * the smallest lower.onpos and the largest upper.onpos found. Returns 1 when
 * any unit was found.
 */
Sint32 func_8C02AD22(zxsdwstr *zssp, Float r)
{
    zxsdwstr zss;
    _OBJ_LANDCOLL *lc;
    Sint32 n;
    Sint32 ret;

    PC_ONLY(zss = *zssp;)
    ret = 0;
    zss.lower.findflag = 0;
    zss.upper.findflag = 0;
    func_8C0522C0(zssp->pos.x, zssp->pos.y, zssp->pos.z, r);
    for (n = B_8C754EF0, lc = B_8C754EF4; n > 0; n--, lc++) {
        if (func_8C02BC60(zssp, lc->pObject, lc->slAttribute) == 1) {
            if (zssp->lower.findflag == 1) {
                if (zss.lower.findflag == 0) {
                    ret = 1;
                    zss.lower.findflag = 1;
                    zss.lower.onpos = zssp->lower.onpos;
                } else if (zss.lower.onpos > zssp->lower.onpos) {
                    zss.lower.onpos = zssp->lower.onpos;
                }
            }
            if (zssp->upper.findflag == 1) {
                if (zss.upper.findflag == 0) {
                    ret = 1;
                    zss.upper.findflag = 1;
                    zss.upper.onpos = zssp->upper.onpos;
                } else if (zss.upper.onpos < zssp->upper.onpos) {
                    zss.upper.onpos = zssp->upper.onpos;
                }
            }
        }
    }
    zssp->lower = zss.lower;
    zssp->upper = zss.upper;
    return ret;
}
#endif /* NON_MATCHING */
