#include "sa1/pending.h"

/*
 * 1ST_READ chunk 8C091C20: ground/shadow probes against the gathered land
 * collision list (B_8C754EF4, count B_8C754EF0) with the hand-written FD
 * probes of code_8C02D060.c.
 */

/* lower + upper probe against one FD land object (hand-written; the
 * callers pass the entry's slAttribute in R6 as well) */
Sint32 func_8C02D2E0(zxsdwstr *zssp, NJS_OBJECT *object, Uint32 attr);
/* lower probe only (hand-written, code_8C02D060.c); touches only pos and
 * lower of the probe */
Sint32 func_8C02D060(zxsdwstr *zssp, NJS_OBJECT *object, Uint32 attr);

/* head of a zxsdwstr (pos + lower, 0x2C bytes): the probe the lower-only
 * callers of 8C02D060 keep on the stack */
typedef struct {
    NJS_POINT3 pos;             /* 0x00 */
    xssunit lower;              /* 0x0C */
} ZXSDW_LOWER;                  /* 0x2C */

SA1_ASSERT_SIZE(ZXSDW_LOWER, 0x2C);
SA1_ASSERT_OFFSET(ZXSDW_LOWER, lower, 0x0C);

/*
 * Ground under (x, y, z): gathers the land objects within 10 around the
 * point, probes every entry (dynamic ones only when the point is inside the
 * XZ radius of the object) and returns the highest ground at or below y
 * (-1000000 if none); ang->x/ang->z get its slope. angx/angz are not set
 * when nothing is hit (the original stores whatever the stack held; PC: 0).
 *
 * NON_MATCHING: register allocation only (7/170 insns). The loop counter i
 * and the induction pointer of the post-probe lc[i].pTask test swap
 * R12/R11; everything else (incl. the FWP induction pointer in R10, which
 * needed the tp temporary) matches. Tried: declaration orders, &&/nested
 * forms of the attr/pTask/fwp tests, global vs local list base, an
 * _OBJ_LANDCOLL pointer, temporaries for r/dist/scl/obj/pos_spd.y/return.
 * A copy temporary between FWP(tp) and fwp (fw2 = FWP(tp); fwp = fw2;)
 * fixes i/R12 but moves fwp from R4 to R14 (4 insns differ).
 */
#ifdef NON_MATCHING
Float func_8C091C20(Float x, Float y, Float z, Angle3 *ang)
{
    zxsdwstr zss;
    NJS_VECTOR v;
    Angle angz;
    Angle angx;
    _OBJ_LANDCOLL *lc;
    NJS_OBJECT *obj;
    forcewk *fwp;
    Float dist;
    Float scl;
    Float onpos;
    Uint16 i;
    task *tp;

    PC_ONLY(angx = 0;)
    PC_ONLY(angz = 0;)
    lc = B_8C754EF4;
    zss.pos.x = x;
    zss.pos.y = y;
    zss.pos.z = z;
    func_8C0522C0(x, y, z, 10.0f);
    onpos = -1000000.0f;
    for (i = 0; i < B_8C754EF0; i++) {
        obj = lc[i].pObject;
        if (lc[i].pTask) {
            v.x = obj->pos[0] - x;
            v.y = 0.0f;
            v.z = obj->pos[2] - z;
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
        if (!(lc[i].slAttribute & 0x400002)) if (func_8C02D2E0(&zss, lc[i].pObject, lc[i].slAttribute)) {
            if (lc[i].pTask) {
                tp = lc[i].pTask;
                fwp = FWP(tp);
                if (fwp) {
                    zss.upper.onpos += fwp->pos_spd.y;
                    zss.lower.onpos += fwp->pos_spd.y;
                }
            }
            if (zss.lower.findflag == 1 && y >= zss.lower.onpos && zss.lower.onpos > onpos) {
                onpos = zss.lower.onpos;
                angx = zss.lower.angx;
                angz = zss.lower.angz;
            }
        }
    }
    ang->x = angx;
    ang->z = angz;
    return onpos;
}
#else
ASM_STUB(Float func_8C091C20(Float x, Float y, Float z, Angle3 *ang))
#endif

/*
 * Ground nearest in height to (x, y, z): gathers the land objects within 5
 * around the point and keeps the lower-probe hit whose height is closest to
 * y (plus the owner's forcewk.pos_spd.y for moving objects); ang->x/ang->z
 * get its slope. Returns -1000000 if nothing was hit.
 */
Float func_8C091D74(Float x, Float y, Float z, Angle3 *ang)
{
    ZXSDW_LOWER zss;
    NJS_VECTOR v;
    Angle angz;
    Angle angx;
    _OBJ_LANDCOLL *lc;
    NJS_OBJECT *obj;
    Float dist;
    Float scl;
    Float onpos;
    Float d0;
    Float d1;
    Uint16 i;
    task *tp;

    PC_ONLY(angx = 0;)
    PC_ONLY(angz = 0;)
    lc = B_8C754EF4;
    zss.pos.x = x;
    zss.pos.y = y;
    zss.pos.z = z;
    func_8C0522C0(x, y, z, 5.0f);
    onpos = -1000000.0f;
    for (i = 0; i < B_8C754EF0; i++) {
        obj = lc[i].pObject;
        if (lc[i].pTask) {
            v.x = obj->pos[0] - x;
            v.y = 0.0f;
            v.z = obj->pos[2] - z;
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
        if (func_8C02D060((zxsdwstr *)&zss, lc[i].pObject, lc[i].slAttribute)) {
            d0 = _builtin_fabsf(y - onpos);
            d1 = _builtin_fabsf(y - zss.lower.onpos);
            if (d0 > d1) {
                onpos = zss.lower.onpos;
                tp = lc[i].pTask;
                if (tp && FWP(tp)) {
                    onpos += FWP(lc[i].pTask)->pos_spd.y;
                }
                angx = zss.lower.angx;
                angz = zss.lower.angz;
            }
        }
    }
    ang->x = angx;
    ang->z = angz;
    return onpos;
}

/*
 * Like func_8C091D74 for static land objects only: entries owned by a task
 * and entries with slAttribute & 0x400002 are skipped.
 */
Float func_8C091F02(Float x, Float y, Float z, Angle3 *ang)
{
    ZXSDW_LOWER zss;
    Angle angz;
    Angle angx;
    _OBJ_LANDCOLL *lc;
    NJS_OBJECT *obj;
    Float onpos;
    Float d0;
    Float d1;
    Uint16 i;

    PC_ONLY(angx = 0;)
    PC_ONLY(angz = 0;)
    lc = B_8C754EF4;
    zss.pos.x = x;
    zss.pos.y = y;
    zss.pos.z = z;
    func_8C0522C0(x, y, z, 5.0f);
    onpos = -1000000.0f;
    for (i = 0; i < B_8C754EF0; i++) {
        obj = lc[i].pObject;
        if (lc[i].pTask) {
            continue;
        }
        if (!(lc[i].slAttribute & 0x400002)) if (func_8C02D060((zxsdwstr *)&zss, lc[i].pObject, lc[i].slAttribute)) {
            d0 = _builtin_fabsf(y - onpos);
            d1 = _builtin_fabsf(y - zss.lower.onpos);
            if (d0 > d1) {
                onpos = zss.lower.onpos;
                if (lc[i].pTask && FWP(lc[i].pTask)) {
                    onpos += FWP(lc[i].pTask)->pos_spd.y;
                }
                angx = zss.lower.angx;
                angz = zss.lower.angz;
            }
        }
    }
    ang->x = angx;
    ang->z = angz;
    return onpos;
}

/*
 * Ground probe under twp->pos against the land objects within 20: stops at
 * the first lower-probe hit. Static objects (no owner task) are culled by
 * their XZ radius first. Returns 4000 when nothing was hit, otherwise the hit
 * height, or 3000 when pos[1] is not above hit + 1.5; ang->x/ang->z get the
 * slope of the probe result (stale stack contents when the list is empty;
 * PC: 4000 and 0).
 *
 * Original bug kept for matching: pos is never written, so the test reads an
 * uninitialised stack slot (R15+0x44). The intent was almost certainly
 * twp->pos.y; the PC build uses that instead of the garbage value.
 */
Float func_8C092000(taskwk *twp, Angle3 *ang)
{
    Float pos[3];
    ZXSDW_LOWER zss;
    NJS_VECTOR v;
    _OBJ_LANDCOLL *lc;
    NJS_OBJECT *obj;
    Float dist;
    Float scl;
    Uint16 i;
    Float ret;
    Float r;

    PC_ONLY(pos[0] = twp->pos.x;)
    PC_ONLY(pos[1] = twp->pos.y;)
    PC_ONLY(pos[2] = twp->pos.z;)
    PC_ONLY(zss.lower.onpos = 4000.0f;)
    PC_ONLY(zss.lower.angx = 0;)
    PC_ONLY(zss.lower.angz = 0;)
    zss.pos = twp->pos;
    lc = B_8C754EF4;
    func_8C0522C0(twp->pos.x, twp->pos.y, twp->pos.z, 20.0f);
    for (i = 0; i < B_8C754EF0; i++) {
        obj = lc[i].pObject;
        if (lc[i].pTask == NULL) {
            v.x = obj->pos[0] - twp->pos.x;
            v.y = 0.0f;
            v.z = obj->pos[2] - twp->pos.z;
            dist = njScalor(&v);
            scl = obj->scl[0];
            if (obj->scl[1] > scl) {
                scl = obj->scl[1];
            }
            if (obj->scl[2] > scl) {
                scl = obj->scl[2];
            }
            r = scl * obj->model->r;
            if (dist > r) {
                zss.lower.onpos = 4000.0f;
                continue;
            }
        }
        if (func_8C02D060((zxsdwstr *)&zss, lc[i].pObject, lc[i].slAttribute)) {
            if (!(pos[1] > zss.lower.onpos + 1.5f)) {
                zss.lower.onpos = 3000.0f;
            }
            break;
        }
        else {
            zss.lower.onpos = 4000.0f;
        }
    }
    ang->x = zss.lower.angx;
    ang->z = zss.lower.angz;
    ret = zss.lower.onpos;
    return ret;
}
