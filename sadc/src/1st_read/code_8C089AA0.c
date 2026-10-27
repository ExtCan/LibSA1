#include "common.h"
#include "ninja.h"

/* Path point (20 bytes; pathtbl-like) */
typedef struct {
    Sint16 slangx;
    Sint16 slangz;
    Float len;
    NJS_POINT3 pos;
} PATH_TBL;

/* Path (pathtag-like) */
typedef struct {
    Uint8 flag;
    Uint16 points;
    Float totallen;
    PATH_TBL *tbl;
} PATH_TAG;

/* Spline control point (16 bytes) */
typedef struct {
    Float len;
    NJS_POINT3 pos;
} SPLINE_PT;

typedef struct {
    Uint8 flag;
    Uint16 points;
    Float totallen;
    Float delta;
    Sint32 unkC;
    SPLINE_PT *tbl0;
    SPLINE_PT *tbl1;
} SPLINE_TAG;

typedef struct {
    Angle angx;
    Angle angz;
    Angle angx2;
    Angle angz2;
    Float onpathpos;
    NJS_POINT3 pos;
    NJS_POINT3 pos2;
    NJS_VECTOR normal;
    NJS_VECTOR normal2;
    NJS_VECTOR dir;
} SPLINE_INFO;

extern Float func_8C02B0B4(NJS_POINT3 *p, NJS_POINT3 *a, NJS_POINT3 *b, NJS_POINT3 *out);
extern Float njDistanceP2P(NJS_POINT3 *a, NJS_POINT3 *b);
extern Float func_8C056784(Float a, Float b);
extern Float njUnitVector(NJS_VECTOR *v);
extern Float njOuterProduct(NJS_VECTOR *a, NJS_VECTOR *b, NJS_VECTOR *out);
extern Float func_8C10D058(Float x);
extern Float atan2f(Float y, Float x);

Float func_8C089AA0(PATH_TAG *path, NJS_POINT3 *pos, NJS_POINT3 *outPos, Float *outDist)
{
    NJS_POINT3 hit;
    Sint32 end;
    PATH_TBL *tbl;
    NJS_POINT3 *next;
    NJS_POINT3 *cur;
    Sint32 i;
    Float len;
    Float dist;
    Float min;
    Float d;
    Float rate;

    tbl = path->tbl;
    end = 1 - path->flag;
    dist = 0.0f;
    for (i = path->points; i > end; i--) {
        len = tbl->len;
        cur = &tbl->pos;
        tbl++;
        if (i != 1) {
            next = &tbl->pos;
        } else {
            next = &path->tbl->pos;
        }
        if (dist == 0.0f) {
            min = func_8C02B0B4(pos, cur, next, &hit);
            if (outPos) {
                *outPos = hit;
            }
            if (outDist) {
                rate = njDistanceP2P(cur, &hit) / len;
                *outDist = SA1_FMAC(rate, len, dist);
            }
        } else {
            if (min > (d = func_8C02B0B4(pos, cur, next, &hit))) {
                min = d;
                if (outPos) {
                    *outPos = hit;
                }
                if (outDist) {
                    rate = njDistanceP2P(cur, &hit) / len;
                    *outDist = SA1_FMAC(rate, len, dist);
                }
            }
        }
        dist += len;
    }
    return min;
}

void func_8C089B9A(NJS_POINT3 *out, SPLINE_PT *tbl, Sint32 i, Float t)
{
    Float b0;
    Float b1;
    Float b2;
    SPLINE_PT *p;

    b0 = (1.0f - t) * (1.0f - t) * 0.5f;
    b1 = SA1_FMAC(t, 1.0f - t, 0.5f);
    b2 = t * t * 0.5f;
    p = &tbl[i - 1];
#ifdef TARGET_PC
    /* SHC multiplies the b1 term first and FMACs the b0 and b2 terms onto it */
    out->x = SA1_FMAC(b2, p[2].pos.x, SA1_FMAC(b0, p[0].pos.x, b1 * p[1].pos.x));
    out->y = SA1_FMAC(b2, p[2].pos.y, SA1_FMAC(b0, p[0].pos.y, b1 * p[1].pos.y));
    out->z = SA1_FMAC(b2, p[2].pos.z, SA1_FMAC(b0, p[0].pos.z, b1 * p[1].pos.z));
#else
    out->x = b0 * p[0].pos.x + b1 * p[1].pos.x + b2 * p[2].pos.x;
    out->y = b0 * p[0].pos.y + b1 * p[1].pos.y + b2 * p[2].pos.y;
    out->z = b0 * p[0].pos.z + b1 * p[1].pos.z + b2 * p[2].pos.z;
#endif
}

void func_8C089C12(Float pos, SPLINE_TAG *spl, Sint32 *idx, Float *t)
{
    Sint32 i;
    Float sum;
    Float len;
    SPLINE_PT *p;

    sum = 0.0f;
    p = spl->tbl0;
    for (i = 1, p++; i < spl->points - 3; i++, p++) {
        len = p->len;
        if (sum + len > pos) {
            break;
        }
        sum += len;
    }
    *idx = i;
    *t = (pos - sum) / len;
}

ASM_STUB(Sint32 func_8C089C80(SPLINE_TAG *spl, SPLINE_INFO *info))
