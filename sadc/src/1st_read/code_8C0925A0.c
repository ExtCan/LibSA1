#include "sa1/pending.h"

/*
 * 1ST_READ 8C0925A0: character shadow / water probe of the player mains
 * (SonicMain .. EggmanMain). Gathers the land objects within 200 of the
 * player, probes every entry and records the lowest surface above
 * (water: slAttribute & 0x400002, also probed with 8C02CA00) in
 * shadow->y_top/Attr_top and the highest ground at or below the player in
 * shadow->y_bottom/Attr_bottom/angx/angz; then draws the blob shadow.
 * Matching: the forcewk test must be written "(fwp = FWP(..)) != NULL";
 * a separate "fwp = ..; if (fwp)" gives the same code but changes the
 * register priorities (0x400002 is then not kept in R8 and the loop's
 * induction pointers are allocated differently).
 */

/* lower + upper probe against one FD land object (hand-written; the
 * callers pass the entry's slAttribute in R6 as well) */
Sint32 func_8C02D2E0(zxsdwstr *zssp, NJS_OBJECT *object, Uint32 attr);
/* code_8C07B320.c: water splash effects. 8C07B346 is passed a third
 * pointer (surface angles) in R6 that it never reads. */
void func_8C07B346(Uint8 pno, NJS_POINT3 *pos, Angle *ang);
void func_8C07B3DA(taskwk *twp);

ASM_STUB(void func_8C09247E(taskwk *twp, shadowwk *shadow))

void func_8C0925A0(taskwk *twp, shadowwk *shadow)
{
    zxsdwstr zss;
    NJS_VECTOR v;
    Angle ang[3];
    _OBJ_LANDCOLL *lc;
    Float scl;
    Float dist;
    forcewk *fwp;
    NJS_OBJECT *obj;
    Uint16 i;

    if (shadow->Attr_top & 0x400002) {
        if (!(shadow->_Attr_top & 0x400002)) {
            func_8C07B3DA(twp);
        }
        v.x = twp->pos.x;
        v.y = shadow->y_top;
        v.z = twp->pos.z;
        ang[0] = shadow->angx;
        ang[1] = twp->ang.y;
        ang[2] = shadow->angz;
        func_8C07B346(twp->counter.b[0], &v, ang);
    }
    shadow->_Attr_top = shadow->Attr_top;
    shadow->_Attr_bottom = shadow->Attr_bottom;
    shadow->Attr_top = 0;
    shadow->Attr_bottom = 0;
    zss.pos = twp->pos;
    zss.pos.y += 1.0f;
    lc = B_8C754EF4;
    func_8C0522C0(twp->pos.x, twp->pos.y, twp->pos.z, 200.0f);
    shadow->y_top = 1000000.0f;
    shadow->y_bottom = -1000000.0f;
    for (i = 0; i < B_8C754EF0; i++) {
        obj = lc[i].pObject;
        if (lc[i].slAttribute & 0x400002) {
            if (func_8C02CA00(&zss, lc[i].pObject) && zss.upper.findflag == 1
                && zss.pos.y < zss.upper.onpos && zss.upper.onpos < shadow->y_top) {
                shadow->y_top = zss.upper.onpos;
                shadow->Attr_top = lc[i].slAttribute;
            }
        } else if (lc[i].pTask) {
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
            if (dist > scl * obj->model->r) {
                continue;
            }
        }
        if (func_8C02D2E0(&zss, lc[i].pObject, lc[i].slAttribute)) {
            if (lc[i].pTask) {
                if ((fwp = FWP(lc[i].pTask)) != NULL) {
                    zss.upper.onpos += fwp->pos_spd.y;
                    zss.lower.onpos += fwp->pos_spd.y;
                }
            }
            if (zss.upper.findflag == 1 && zss.pos.y < zss.upper.onpos
                && zss.upper.onpos < shadow->y_top) {
                shadow->y_top = zss.upper.onpos;
                shadow->Attr_top = lc[i].slAttribute;
            }
            if (zss.lower.findflag == 1 && zss.pos.y >= zss.lower.onpos
                && zss.lower.onpos > shadow->y_bottom) {
                shadow->y_bottom = zss.lower.onpos;
                shadow->Attr_bottom = lc[i].slAttribute;
                shadow->angx = zss.lower.angx;
                shadow->angz = zss.lower.angz;
            }
        }
    }
    func_8C09247E(twp, shadow);
}
