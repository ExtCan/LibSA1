#include "sa1/sa1.h"
#include "sa1/pending.h"    /* PATHINFO */

/*
 * Spline path evaluation (1ST_READ 8C089C80-8C089EE8).
 *
 * The spline tag/point layouts are not in the shared headers yet; they are
 * the same as the local SPLINE_TAG/SPLINE_PT of code_8C089AA0.c (which holds
 * the spline helpers func_8C089B9A and func_8C089C12 reached here with BSR).
 * The result block is the shared PATHINFO (onpathpos at 0x10, position at
 * 0x14 and 0x20, normal at 0x2C/0x38, direction at 0x44).
 */

/* Spline control point (16 bytes) */
typedef struct {
    Float len;                  /* 0x00 length of the segment */
    NJS_POINT3 pos;             /* 0x04 */
} SPLINE_PT;                    /* 0x10 */

typedef struct {
    Uint8 flag;                 /* 0x00 bits 2-3: end mode (0 clamp, 1 loop) */
    Uint16 points;              /* 0x02 */
    Float totallen;             /* 0x04 */
    Float delta;                /* 0x08 parameter step for the direction samples */
    Sint32 unkC;                /* 0x0C */
    SPLINE_PT *tbl0;            /* DC 0x10 centre line */
    SPLINE_PT *tbl1;            /* DC 0x14 side line (normal) */
} SPLINE_TAG;                   /* DC 0x18 (pointers: native layout on 64-bit PC) */

SA1_ASSERT_SIZE(SPLINE_PT, 0x10);
SA1_ASSERT_SIZE(SPLINE_TAG, 0x18);
SA1_ASSERT_OFFSET(SPLINE_TAG, delta, 0x08);
SA1_ASSERT_OFFSET(SPLINE_TAG, tbl0, 0x10);
SA1_ASSERT_OFFSET(SPLINE_TAG, tbl1, 0x14);

/*
 * Prefix view of PATHINFO: this function copies the position as one
 * NJS_POINT3 (12-byte struct copy addressed @(20,info)), which the shared
 * header spells as the three floats xpos/ypos/zpos. All members are 4-byte
 * scalars, so the view has the PATHINFO layout on every target.
 */
typedef struct {
    Angle slangx;               /* 0x00 */
    Angle slangz;               /* 0x04 */
    Angle angx;                 /* 0x08 */
    Angle angz;                 /* 0x0C */
    Float onpathpos;            /* 0x10 */
    NJS_POINT3 pos;             /* 0x14 = PATHINFO.xpos/ypos/zpos */
} PATHINFO_POS;

SA1_ASSERT_OFFSET(PATHINFO_POS, pos, 0x14);

ASM_STUB(void func_8C089B9A(NJS_POINT3 *out, SPLINE_PT *tbl, Sint32 i, Float t))
ASM_STUB(void func_8C089C12(Float pos, SPLINE_TAG *spl, Sint32 *idx, Float *t))

/*
 * Position, direction and normal at info->onpathpos on a spline.
 * Returns 0 when a clamped spline is evaluated out of range. The original
 * has no return statement on the success path (R0 holds whatever atan2f
 * left there; no caller is known); the PC build returns 1.
 *
 * The decrement of the first wrap-around test must be spelled i -= 1:
 * with i-- SHC keeps the constant 1 of that block in R4 instead of R2.
 */
Bool func_8C089C80(SPLINE_TAG *spl, PATHINFO *info)
{
    NJS_POINT3 v80;
    NJS_POINT3 v68;
    NJS_POINT3 v56;
    NJS_POINT3 v44;
    NJS_POINT3 v32;
    NJS_VECTOR d20;
    NJS_VECTOR d8;
    Float t;
    Sint32 idx;
    Uint16 mode;
    Sint32 i;
    Float tt;

    mode = (Uint16)((spl->flag >> 2) & 3);
    switch (mode) {
    case 0:
        if (0.0f > info->onpathpos || info->onpathpos > spl->totallen) {
            return 0;
        }
        break;
    case 1:
        info->onpathpos = func_8C056784(info->onpathpos, spl->totallen);
        break;
    }
    func_8C089C12(info->onpathpos, spl, &idx, &t);
    func_8C089B9A(&v80, spl->tbl0, idx, t);
    info->xpos2 = v80.x;
    info->ypos2 = v80.y;
    info->zpos2 = v80.z;
    ((PATHINFO_POS *)info)->pos = v80;

    tt = t - spl->delta;
    i = idx;
    if (tt < 0.0f) {
        i -= 1;
        tt += 1.0f;
        if (i < 1) {
            switch (mode) {
            case 0:
                i = 1;
                tt = 0.0f;
                break;
            case 1:
                i = spl->points - 2;
                break;
            }
        }
    }
    func_8C089B9A(&v68, spl->tbl0, i, tt);
    func_8C089B9A(&v56, spl->tbl1, i, tt);

    tt = t + spl->delta;
    i = idx;
    if (tt > 1.0f) {
        i++;
        tt -= 1.0f;
        if (i > spl->points - 2) {
            switch (mode) {
            case 0:
                i = spl->points - 2;
                tt = 1.0f;
                break;
            case 1:
                i = 1;
                break;
            }
        }
    }
    func_8C089B9A(&v44, spl->tbl0, i, tt);
    func_8C089B9A(&v32, spl->tbl1, i, tt);

    info->front.x = v44.x - v68.x;
    info->front.y = v44.y - v68.y;
    info->front.z = v44.z - v68.z;
    njUnitVector(&info->front);
    d20.x = v32.x - v68.x;
    d20.y = v32.y - v68.y;
    d20.z = v32.z - v68.z;
    d8.x = v44.x - v56.x;
    d8.y = v44.y - v56.y;
    d8.z = v44.z - v56.z;
    njOuterProduct(&d20, &d8, &info->normal);
    njUnitVector(&info->normal);
    info->normala = info->normal;
    info->slangx = info->angx = SA1_FTOI(func_8C10D058(info->normal.z) * 10430.381f);
    info->slangz = info->angz = -SA1_FTOI(atan2f(info->normal.x, info->normal.y) * 10430.381f);
    PC_ONLY(return 1;)
}
