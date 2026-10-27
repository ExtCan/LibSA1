/*
 * 1ST_READ 8C02B0B4 (land collision TU 8C027360-8C02CE80): distance from a
 * point to a line segment.
 *
 * func_8C02B0B4 returns the distance from p to the segment a-b and writes
 * the nearest point of the segment to out (when out is not NULL). Called by
 * the path code (func_8C089AA0) for every path edge.
 *
 * NON_MATCHING with the project compiler SHC 5.1r03 (10 instructions): the
 * two 12-byte struct copies (*out = *a / *out = *b) reuse R3 for all three
 * MOV.L load/store pairs in the original, while 5.1r03 alternates R3/R2 (and
 * the second func_8C057B18 / njDistanceP2P pool loads swap R3/R2). The very
 * same source is a byte-exact MATCH with SHC 5.0r31, 5.0r32 and 5.1r01
 * (shcdiff --ver=5.1r01); this TU belongs to the "differently-built" region
 * 8C027360-8C02CE80 of docs/compiler.md (MOV.W constants, sequential struct
 * copies), which was apparently built with a pre-5.1r03 SHC.
 */
#include "common.h"
#include "ninja.h"
#include "sa1/ninja_types.h"

/* dot(b - a, c - a) (hand-written asm, see code_8C057B18.c) */
extern Float func_8C057B18(NJS_POINT3 *a, NJS_POINT3 *b, NJS_POINT3 *c);
extern Float njDistanceP2P(const NJS_POINT3 *p1, const NJS_POINT3 *p2);
/*
 * Distance from p to the unbounded line l (point l->p, direction l->v);
 * the foot of the perpendicular goes to cp when cp is not NULL. Returns
 * njSqrt(...) (tail call), i.e. a Float in FR0; include/sa1/ninja_funcs.h
 * still declares it as void func_8C63FF84(void *, void *, void *), so it is
 * declared here instead of including that header.
 */
extern Float func_8C63FF84(NJS_POINT3 *p, NJS_LINE *l, NJS_POINT3 *cp);

#ifdef NON_MATCHING
Float func_8C02B0B4(NJS_POINT3 *p, NJS_POINT3 *a, NJS_POINT3 *b, NJS_POINT3 *out)
{
    NJS_LINE line;
    Float x;
    Float y;
    Float z;

    /* p lies before a: nearest point is a */
    if (func_8C057B18(a, b, p) < 0.0f) {
        if (out) {
            *out = *a;
        }
        return njDistanceP2P(p, a);
    }
    /* p lies beyond b: nearest point is b */
    if (func_8C057B18(b, a, p) < 0.0f) {
        if (out) {
            *out = *b;
        }
        return njDistanceP2P(p, b);
    }
    /* otherwise: distance to the line through a and b */
    x = a->x;
    y = a->y;
    z = a->z;
    line.px = x;
    line.py = y;
    line.pz = z;
    line.vx = b->x - x;
    line.vy = b->y - y;
    line.vz = b->z - z;
    return func_8C63FF84(p, &line, out);
}
#else
ASM_STUB(Float func_8C02B0B4(NJS_POINT3 *p, NJS_POINT3 *a, NJS_POINT3 *b, NJS_POINT3 *out))
#endif
