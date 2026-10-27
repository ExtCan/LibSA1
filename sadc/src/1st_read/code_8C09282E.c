#include "sa1/pending.h"

/*
 * 1ST_READ 8C09282E: six axis probes (+x, -x, +y, -y, +z, -z) around
 * xyzp->pos against the land objects gathered within 1 of the point. For
 * each axis the hit nearest to the point is kept in xyzp->hit[]; returns 1
 * if any land object was probed successfully. Caller: 8C047260.
 * hit[0]/[2]/[4] start at -1000000, hit[1]/[3]/[5] at +1000000 (onpos).
 * Matching: the |old|/|new| distances live in Float locals (FABS + FMOV);
 * hit[0] and hit[3] compare as "d1 < d0", the others as "d0 > d1" (same
 * test, it only swaps the FR4/FR5 allocation).
 */
Sint32 func_8C09282E(xyyzzxsdwstr *xyzp)
{
    NJS_VECTOR v;
    xyyzzxsdwstr xyz;
    Sint32 ret;
    _OBJ_LANDCOLL *lc;
    Uint16 i;
    NJS_POINT3 *pos;
    Sint32 j;
    NJS_OBJECT *obj;
    Float dist;
    Float scl;
    Float d0;
    Float d1;

    ret = 0;
    pos = &xyzp->pos;
    xyz.pos = xyzp->pos;
    for (j = 0; j < 6; j++) {
        xyzp->hit[j].findflag = 0;
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
        if (lc[i].slAttribute & 0x400002) {
            continue;
        }
        if (func_8C029400(&xyz, lc[i].pObject, lc[i].slAttribute)) {
            ret = 1;
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
