/*
 * 1ST_READ 8C0EA040-8C0EA78C (chunk 8C0E6620): Big the Cat's fishing lure
 * (callers func_8C0E8A4A, func_8C0E943C).
 *
 *  func_8C0EA040  ground / ceiling / water surface around a position: gathers
 *                 the land objects within 25 and probes each one (moving
 *                 objects only when the point is within their XZ radius);
 *                 keeps the lowest ceiling above and the highest ground at or
 *                 below pos.y, and in out->water the water polygon
 *                 (slAttribute & 2) nearest in height. flags bit 0 / bit 1
 *                 keep water out of the ground / ceiling result. Returns the
 *                 ground height (-1000000 if none).
 *  func_8C0EA2F0  orients the child node of the current lure model
 *                 (D_8C565FC4[func_8C0E6E80()]) from the fishing state
 *                 (B_8C7A40E8) and the lure task's any work.
 *  func_8C0EA48A  turns the lure task toward player 0 (ang_aim.y) and swings
 *                 or settles its ang.x.
 *  func_8C0EA64E  turns twp->ang.y toward mwp->ang_aim.y (step 0x40).
 *  func_8C0EA6C0  pitch toward the point of func_8C0E79D4 (stage 8-3 only)
 *                 kept in mwp->ang_aim.z; turns twp->ang.z toward it.
 */
#include "common.h"
#include "sa1/sa1.h"
#include "sa1/pending.h"

/* one probe result kept by func_8C0EA040 */
typedef struct {
    Uint32 attr;                /* 0x00 _OBJ_LANDCOLL.slAttribute */
    Angle angx;                 /* 0x04 */
    Angle angz;                 /* 0x08 */
    Float onpos;                /* 0x0C */
} LURE_HIT;                     /* 0x10 */

typedef struct {
    LURE_HIT upper;             /* 0x00 lowest ceiling above pos.y */
    LURE_HIT lower;             /* 0x10 highest ground at or below pos.y */
    LURE_HIT water;             /* 0x20 water surface nearest to pos.y */
} LURE_GROUND;                  /* 0x30 */

SA1_ASSERT_SIZE(LURE_HIT, 0x10);
SA1_ASSERT_SIZE(LURE_GROUND, 0x30);
SA1_ASSERT_OFFSET(LURE_GROUND, lower, 0x10);
SA1_ASSERT_OFFSET(LURE_GROUND, water, 0x20);

/*
 * Any work of the lure task (task.awp, shared anywk through AWP(tp)):
 *  work[0].w[1]  ang[2] of the lure node while B_8C7A40E8 & 0x10
 *  work[1].w[0]  wobble phase (+0x800 per frame, njSin * 4096 -> ang[0])
 *  work[1].w[1]  ang[2] of the lure node, eased toward -twp->ang.z in
 *                steps of 0x38E (0x38E3 while the lure moves or a button
 *                is pressed); used as ang[0] while B_8C7A40E8 & 0x10
 * The array access (work[1].w[0]) is needed for the ADD #4 / MOV.W @Rn
 * addressing of the phase update.
 */

extern task *B_8C7A40C8;                /* lure task (NULL: no lure) */
extern task *B_8C7A40CC;                /* hooked fish task */
extern Uint16 B_8C7A40E8;               /* fishing state bits */
extern NJS_OBJECT *D_8C565FC4[];        /* lure models (_Big_Normal_Lure, ...) */

/* lower + upper probe against one FD land object (hand-written) */
Sint32 func_8C02D2E0(zxsdwstr *zssp, NJS_OBJECT *object, Uint32 attr);
Sint32 func_8C0E6E80(void);                /* lure model index (number of lure upgrades) */
void func_8C0E79D4(task *tp, NJS_POINT3 *pos);

/*
 * NON_MATCHING: stack slot assignment only (20 of 328 instructions, all
 * @(n,R15) offsets). The original keeps the six hit locals in declaration
 * order (uangx sp+40, uangz 36, uattr 32, langx 28, langz 24, lattr 20);
 * this compile puts them at uangz 20, uattr 24, langx 28, langz 32,
 * uangx 36, lattr 40. Everything else matches, including the three
 * induction pointers of the lower block and the spill of the third one.
 * The slot order does not follow the declaration order or the variable
 * types here; it changes with the order of the final copies to *out and
 * with the grouping of the zero initialisation (more than two init
 * statements also spill i instead of the third induction pointer, which is
 * why the zeroing is written as two chained assignments). Tried: all 32
 * groupings of the zero chain in the original evaluation order, chain and
 * statement orders, declaration orders, Sint32/Uint32 types, initialisers
 * in the declarations, local arrays (worse).
 */
#ifdef NON_MATCHING
Float func_8C0EA040(taskwk *twp, Sint32 flags, LURE_GROUND *out)
{
    zxsdwstr zss;
    NJS_VECTOR v;
    Angle uangx;
    Angle uangz;
    Uint32 uattr;
    Angle langx;
    Angle langz;
    Uint32 lattr;
    _OBJ_LANDCOLL *lc;
    NJS_OBJECT *obj;
    Float x;
    Float y;
    Float z;
    Float dist;
    Float scl;
    Float upper;
    Float lower;
    Uint16 i;

    x = twp->pos.x;
    y = twp->pos.y;
    z = twp->pos.z;
    zss.pos.x = x;
    zss.pos.y = y;
    zss.pos.z = z;
    lower = -1000000.0f;
    langx = langz = lattr = 0;
    uangx = uangz = uattr = 0;
    out->water.onpos = upper = 1000000.0f;
    out->water.angx = 0;
    out->water.angz = 0;
    out->water.attr = 0;
    lc = B_8C754EF4;
    func_8C0522C0(x, y, z, 25.0f);
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
        if (!func_8C02D2E0(&zss, lc[i].pObject, lc[i].slAttribute)) {
            continue;
        }
        if (lc[i].pTask && FWP(lc[i].pTask)) {
            zss.upper.onpos += FWP(lc[i].pTask)->pos_spd.y;
            zss.lower.onpos += FWP(lc[i].pTask)->pos_spd.y;
        }
        if (zss.upper.findflag == 1 && zss.pos.y < zss.upper.onpos && upper > zss.upper.onpos) {
            if (lc[i].slAttribute & 2) {
                if (_builtin_fabsf(zss.upper.onpos - zss.pos.y) < _builtin_fabsf(out->water.onpos - zss.pos.y)) {
                    out->water.onpos = zss.upper.onpos;
                    out->water.angx = zss.upper.angx;
                    out->water.angz = zss.upper.angz;
                    out->water.attr = lc[i].slAttribute;
                }
                if (!(flags & 2)) {
                    upper = zss.upper.onpos;
                    uangx = zss.upper.angx;
                    uangz = zss.upper.angz;
                    uattr = lc[i].slAttribute;
                }
            } else {
                upper = zss.upper.onpos;
                uangx = zss.upper.angx;
                uangz = zss.upper.angz;
                uattr = lc[i].slAttribute;
            }
        }
        if (zss.lower.findflag == 1 && zss.pos.y >= zss.lower.onpos && zss.lower.onpos > lower) {
            if (lc[i].slAttribute & 2) {
                if (_builtin_fabsf(zss.lower.onpos - zss.pos.y) < _builtin_fabsf(out->water.onpos - zss.pos.y)) {
                    out->water.onpos = zss.lower.onpos;
                    out->water.angx = zss.lower.angx;
                    out->water.angz = zss.lower.angz;
                    out->water.attr = lc[i].slAttribute;
                }
                if (!(flags & 1)) {
                    lower = zss.lower.onpos;
                    langx = zss.lower.angx;
                    langz = zss.lower.angz;
                    lattr = lc[i].slAttribute;
                }
            } else {
                lower = zss.lower.onpos;
                langx = zss.lower.angx;
                langz = zss.lower.angz;
                lattr = lc[i].slAttribute;
            }
        }
    }
    out->upper.onpos = upper;
    out->upper.angx = uangx;
    out->upper.angz = uangz;
    out->upper.attr = uattr;
    out->lower.onpos = lower;
    out->lower.angx = langx;
    out->lower.angz = langz;
    out->lower.attr = lattr;
    return lower;
}
#else
ASM_STUB(Float func_8C0EA040(taskwk *twp, Sint32 flags, LURE_GROUND *out))
#endif

void func_8C0EA2F0(task *tp)
{
    NJS_VECTOR v;
    NJS_OBJECT *obj;
    Sint32 ang;
    Sint32 d;
    Float spd;

    obj = D_8C565FC4[func_8C0E6E80()]->child;
    obj->evalflags &= ~NJD_EVAL_UNIT_ANG;
    if (B_8C7A40C8 == NULL) {
        return;
    }
    if (B_8C7A40E8 & 1) {
        obj->ang[0] = 0x38E3;
        obj->ang[2] = 0;
        if (B_8C7A40CC) {
            obj->ang[1] = B_8C7A40CC->twp->ang.y + B_8C7A40C8->twp->ang.y + 0xC000;
        } else {
            obj->ang[1] = -0x4000;
        }
    } else if (B_8C7A40E8 & 0x10) {
        obj->ang[0] = AWP(tp)->work[1].w[1];
        obj->ang[1] = 0;
        obj->ang[2] = AWP(tp)->work[0].w[1];
    } else {
        ang = AWP(tp)->work[1].w[0];
        ang = (Uint16)(ang + 0x800);
        AWP(tp)->work[1].w[0] = ang;
        obj->ang[0] = SA1_FTOI(njSin(ang) * 4096.0f);
        ang = AWP(tp)->work[1].w[1];
        v.x = tp->mwp->spd.x;
        v.y = 0.0f;
        v.z = tp->mwp->spd.z;
        spd = njScalor(&v);
        if (spd > 0.099999996f || (B_8C7A40E8 & 0x10)
            || (B_8C754C08[0].press & (D_8C19E38C | D_8C19E390)) || tp->twp->mode < 3) {
            ang = 0x38E3;
        } else {
            d = (Uint16)(tp->twp->ang.z + ang);
            if (d < 0x8000) {
                ang -= 0x38E;
            } else if (d > 0x8000) {
                ang += 0x38E;
            }
            if (d < 0x38E || d > 0xFC71) {
                ang = -tp->twp->ang.z;
            }
        }
        AWP(tp)->work[1].w[1] = ang;
        obj->ang[2] = ang;
    }
}

void func_8C0EA48A(task *tp)
{
    taskwk *ptwp;
    Angle a;
    Angle aim;
    Sint32 lo;
    Sint32 step;
    Sint32 n;

    ptwp = B_8C78C608[0];
    if (B_8C7A40C8 == NULL) {
        return;
    }
    tp->mwp->ang_aim.y = 0x4000 - SA1_FTOI(atan2f(tp->twp->pos.x - ptwp->pos.x, tp->twp->pos.z - ptwp->pos.z) * 10430.381f);
    if (B_8C7A40E8 & 1) {
        a = tp->twp->ang.x;
        if (a > 0x8000) {
            a += 0x80;
            if (a >= 0x10000) {
                a = 0;
            }
        } else {
            a -= 0x80;
            if (a <= 0) {
                a = 0;
            }
        }
        tp->twp->ang.x = a;
    } else if (B_8C7A40E8 & 0x10) {
        a = tp->twp->ang.x;
        if (a >= 0) {
            a += 0x80;
            if (a > 0x1800) {
                a = 0x1800;
            }
        } else {
            a -= 0x80;
            a = (Uint16)a;
            if (a < 0xE800) {
                a = 0xE800;
            }
        }
        tp->twp->ang.x = a;
    } else {
        a = tp->twp->ang.x;
        if (tp->mwp->spd.x == 0.0f && tp->mwp->spd.z == 0.0f) {
            if (a > 0x8000) {
                a += 0x80;
                if (a >= 0x10000) {
                    a = 0;
                }
            } else {
                a -= 0x80;
                if (a <= 0) {
                    a = 0;
                }
            }
        } else {
            n = 0;
            if ((Sint32)B_8C754C08[0].on & (Sint32)D_8C19E38C) {
                n = 1;
            }
            if ((Sint32)B_8C754C08[0].on & (Sint32)D_8C19E390) {
                n |= 2;
            }
            switch (n) {
            case 1:
            default:
                step = 0x200;
                lo = 0x800;
                break;
            case 2:
            case 3:
                step = 0xC00;
                lo = 0x1400;
                break;
            }
            aim = tp->mwp->ang_aim.x;
            if (a > lo && a < lo + 0x3000) {
                aim = 0x10000 - lo;
            }
            if (a > 0xD000 - lo && a < 0x10000 - lo) {
                aim = lo;
            }
            if (aim < 0x8000) {
                a = (Uint16)(a + step);
            } else {
                a = (Uint16)(a - step);
            }
            tp->mwp->ang_aim.x = aim;
        }
        tp->twp->ang.x = a;
    }
}

void func_8C0EA64E(task *tp)
{
    Angle a;
    Angle aim;
    Sint32 d;

    a = tp->twp->ang.y;
    aim = tp->mwp->ang_aim.y;
    d = (Uint16)(a - aim);
    if (d < 0x8000) {
        a -= 0x40;
    } else if (d > 0x8000) {
        a += 0x40;
    }
    if (d < 0x80 || d > 0xFF80) {
        a = aim;
    }
    tp->twp->ang.y = a;
}

void func_8C0EA6C0(task *tp)
{
    NJS_VECTOR v;
    taskwk *twp;
    Angle a;
    Angle aim;
    Sint32 d;

    twp = tp->twp;
    a = twp->ang.z;
    if (B_8C7A40C8) {
        if (!(B_8C7A40E8 & 0x10) && GetStageNumber() == 0x803) {
            func_8C0E79D4(tp, &v);
            v.x -= twp->pos.x;
            v.y -= twp->pos.y;
            v.z -= twp->pos.z;
            njUnitVector(&v);
            aim = tp->mwp->ang_aim.z = -SA1_FTOI(func_8C10D058(v.y) * 10430.381f) & 0xFFFF;
        } else {
            aim = tp->mwp->ang_aim.z = 0;
        }
        d = (Uint16)(a - aim);
        if (d < 0x8000) {
            a -= 0x80;
        } else if (d > 0x8000) {
            a += 0x80;
        }
        if (d < 0x80 || d > 0xFF80) {
            a = aim;
        }
        tp->twp->ang.z = (Uint16)a;
    }
}
