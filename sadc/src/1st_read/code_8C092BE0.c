/*
 * 1ST_READ chunk 8C092BE0: six axis probes (+x, -x, +y, -y, +z, -z) of a
 * point against the gathered land collision list (B_8C754EF4, count
 * B_8C754EF0) with func_8C029400, keeping for every axis the hit nearest
 * to the point. Caller: 8C047260.
 */
#include "common.h"
#include "ninja.h"
#include "sa1/sa1.h"
#include "sa1/shc_intrinsics.h"
#include "sa1/pending.h"

/*
 * Probes the land objects gathered within 1.0 of xyzp->pos along the six
 * axes (func_8C029400 fills a local copy). Dynamic entries (pTask != NULL)
 * are skipped when the point is outside the bounding sphere of their model
 * (model->r * largest scale). For every axis the found hit whose onpos is
 * nearest to the point's coordinate replaces xyzp->hit[n]; the units start
 * as not found with onpos -1000000 (even n) / +1000000 (odd n). Returns 1
 * when a hit[2] (the y unit that starts at -1000000) was taken from an
 * entry whose slAttribute has a bit of 0x400002 set.
 *
 * Matching notes: the three uses of lc[i] (obj/pTask, the call, the
 * attribute test) become three induction pointers (R9, R8, sp+4); a single
 * lp = &lc[i] alias does not. The compares of units 0 and 3 are written
 * d1 < d0 (FR5/FR4 allocation), the others d0 > d1.
 */
Sint32 func_8C092BE0(xyyzzxsdwstr *xyzp)
{
    NJS_VECTOR v;
    xyyzzxsdwstr xyz;
    Sint32 ret;
    _OBJ_LANDCOLL *lc;
    Uint16 i;
    NJS_POINT3 *pos;
    NJS_OBJECT *obj;
    Float dist;
    Float scl;
    Float d0;
    Float d1;
    Sint32 n;

    pos = &xyzp->pos;
    ret = 0;
    xyz.pos = xyzp->pos;
    for (n = 0; n < 6; n++) {
        xyzp->hit[n].findflag = 0;
    }
    xyzp->hit[0].onpos = -1000000.0f;
    xyzp->hit[1].onpos = 1000000.0f;
    xyzp->hit[2].onpos = -1000000.0f;
    xyzp->hit[3].onpos = 1000000.0f;
    xyzp->hit[4].onpos = -1000000.0f;
    xyzp->hit[5].onpos = 1000000.0f;
    lc = B_8C754EF4;
    func_8C0522C0(pos->x, pos->y, pos->z, 1.0f);
    for (i = 0; i < B_8C754EF0; i++) {
        obj = lc[i].pObject;
        if (lc[i].pTask) {
            v.x = obj->pos[0] - pos->x;
            v.y = obj->pos[1] - pos->y;
            v.z = obj->pos[2] - pos->z;
            dist = njScalor(&v);
            scl = obj->scl[0];
            if (obj->scl[1] > scl) {
                scl = obj->scl[1];
            }
            if (obj->scl[2] > scl) {
                scl = obj->scl[2];
            }
            if (dist > scl * obj->model->r) {
                continue;
            }
        }
        if (func_8C029400(&xyz, lc[i].pObject, lc[i].slAttribute)) {
            if (xyz.hit[0].findflag) {
                d0 = _builtin_fabsf(pos->x - xyzp->hit[0].onpos);
                d1 = _builtin_fabsf(pos->x - xyz.hit[0].onpos);
                if (d1 < d0) {
                    xyzp->hit[0] = xyz.hit[0];
                }
            }
            if (xyz.hit[1].findflag) {
                d0 = _builtin_fabsf(xyzp->hit[1].onpos - pos->x);
                d1 = _builtin_fabsf(xyz.hit[1].onpos - pos->x);
                if (d0 > d1) {
                    xyzp->hit[1] = xyz.hit[1];
                }
            }
            if (xyz.hit[2].findflag) {
                d0 = _builtin_fabsf(pos->y - xyzp->hit[2].onpos);
                d1 = _builtin_fabsf(pos->y - xyz.hit[2].onpos);
                if (d0 > d1) {
                    xyzp->hit[2] = xyz.hit[2];
                    if (lc[i].slAttribute & 0x400002) {
                        ret = 1;
                    }
                }
            }
            if (xyz.hit[3].findflag) {
                d0 = _builtin_fabsf(xyzp->hit[3].onpos - pos->y);
                d1 = _builtin_fabsf(xyz.hit[3].onpos - pos->y);
                if (d1 < d0) {
                    xyzp->hit[3] = xyz.hit[3];
                }
            }
            if (xyz.hit[4].findflag) {
                d0 = _builtin_fabsf(pos->z - xyzp->hit[4].onpos);
                d1 = _builtin_fabsf(pos->z - xyz.hit[4].onpos);
                if (d0 > d1) {
                    xyzp->hit[4] = xyz.hit[4];
                }
            }
            if (xyz.hit[5].findflag) {
                d0 = _builtin_fabsf(xyzp->hit[5].onpos - pos->z);
                d1 = _builtin_fabsf(xyz.hit[5].onpos - pos->z);
                if (d0 > d1) {
                    xyzp->hit[5] = xyz.hit[5];
                }
            }
        }
    }
    return ret;
}
