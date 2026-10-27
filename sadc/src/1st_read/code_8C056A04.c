/*
 * 1ST_READ 8C056A04-8C056EC4 (chunk 8C055D20):
 *   8C056A04       MSetPosition core: an object sphere against the land
 *                  collision list (SADX MSetPositionWIgnoreAttribute analogue;
 *                  wrappers 8C0569E0/8C0569F0/8C056A00)
 *   8C056D5E/D76   set/clear evalflags bits in a NULL-terminated object list
 *   8C056DC0/E6C   build the node matrices of an object tree into B_8C788C44
 *   8C056EB2       load one of those node matrices
 *
 * Idioms the original needs (SHC 5.1r03):
 *  - "a ? njRotateZ(m, a) : (void)0" (not an if) keeps the angle in a stack
 *    temporary, as the original does;
 *  - local matrices are passed as (NJS_MATRIX_PTR)&m (Ninja's NJS_MATRIX *
 *    style); plain m makes SHC keep &m in a callee-saved register.
 */
#include "common.h"
#include "ninja.h"
#include "task.h"
#include "sa1/ninja_funcs.h"
#include "sa1/engine.h"
/* the shared header declares 8C029B00 as (csts *, Uint32, Uint32); every
 * call site (here, PSetPosition 8C07335C/8C073660) passes
 * (csts *, _OBJ_LANDCOLL.pObject, _OBJ_LANDCOLL.slAttribute) */
#define func_8C029B00 func_8C029B00_hdr
#include "sa1/collision.h"
#undef func_8C029B00

/*
 * Force work (task.fwp): CreateElementalTask allocates CAlloc(2, 0x1C) for
 * im & 4. Layout from the SADX analogy (forcewk); not in the shared headers
 * (same local definition as code_8C0988E4.c). func_8C056A04 adds pos_spd to
 * the push-back position and ang_spd to the object angle.
 */
typedef struct forcewk {
    void (*call_back)(task *tp, taskwk *twp, struct forcewk *fwp);   /* 0x00 */
    Angle3 ang_spd;             /* 0x04 */
    NJS_POINT3 pos_spd;         /* 0x10 */
} forcewk;                      /* 0x1C */

SA1_ASSERT_SIZE(forcewk, 0x1C);
SA1_ASSERT_OFFSET(forcewk, ang_spd, 0x04);
SA1_ASSERT_OFFSET(forcewk, pos_spd, 0x10);

extern NJS_MATRIX D_8C67C640;           /* unit matrix */
extern NJS_MATRIX B_8C788C44[];         /* node matrices built by func_8C056E6C */

Sint32 func_8C029B00(csts *cp, NJS_OBJECT *obj, Uint32 attr);

ASM_STUB(Sint32 DiffAngle(Sint32 a, Sint32 b))

NJS_OBJECT *func_8C056DC0(NJS_MATRIX_PTR *mp, NJS_OBJECT *obj, NJS_MATRIX_PTR parent);

/*
 * Moves the sphere (pos, r) with velocity spd against the land collision
 * list gathered for it (B_8C754EF4), skipping entries whose attribute has a
 * bit of attr. Of all hit objects the one whose push-back point is nearest
 * pos wins: pos/spd get its push-back position and speed, and ang (optional)
 * gets its slope angles (under a ceiling, normal2.y < 0, x or z is flipped,
 * whichever is nearer the current angle) plus the rotation speed of a moving
 * collision object (task force work). Returns the OR over all hits of
 * 1 (floor) / 2 (wall or ceiling: normal2 . gravity > -0.5), 0 = no hit.
 *
 * NON_MATCHING only because of the struct copy helper: the code is
 * instruction-identical, but SHC's "best = cs" (0x84 bytes) calls
 * __quick_mvn, which config/symbols.1st_read.txt places at 8C10CD38 (the
 * <= 64-byte routine used by the SDK code), while every game-code struct
 * copy calls 8C10CDDC. It matches once __quick_mvn resolves to 8C10CDDC.
 */
#ifdef NON_MATCHING
Sint32 func_8C056A04(NJS_POINT3 *pos, NJS_POINT3 *spd, Angle3 *ang, Uint32 attr, Float r)
{
    csts cs;
    csts best;
    Angle3 rot;
    csts *cp;
    csts *bp;
    _OBJ_LANDCOLL *lc;
    Float dist;
    Float d;
    Sint32 hit;
    Sint32 diffx;
    Sint32 diffz;
    Sint32 angx;
    Sint32 angz;
    Sint32 angx2;
    Sint32 angz2;
    Uint16 i;
    task *tp;
    forcewk *fwp;

    hit = 0;
    lc = B_8C754EF4;
    cp = &cs;
    rot.x = 0;
    rot.y = 0;
    rot.z = 0;
    PC_ONLY(dist = 0.0f;)       /* always set by the first hit before it is read */
    if (ang == NULL) {
        cp->tnorm.x = 0.0f;
        cp->tnorm.y = 1.0f;
        cp->tnorm.z = 0.0f;
    } else {
        NJS_VECTOR vd;
        NJS_VECTOR vs;

        vs.x = 0.0f;
        vs.y = 1.0f;
        vs.z = 0.0f;
        njPushMatrix(D_8C67C640);
        ang->z ? njRotateZ(NULL, ang->z) : (void)0;
        ang->x ? njRotateX(NULL, ang->x) : (void)0;
        ang->y ? njRotateY(NULL, ang->y) : (void)0;
        njCalcVector(NULL, &vs, &vd);
        njPopMatrix(1);
        cp->tnorm = vd;
    }
    cp->pos = *pos;
    cp->radius = r;
    cp->spd = *spd;
    func_8C028BFE(cp);
    for (i = 0; i < B_8C754EF0; i++) {
        Angle3 frot;
        NJS_VECTOR v;

        /* A hit object whose task has no force work leaves frot as it was:
         * the original then uses a stale value (the previous iteration's, or
         * at first the bits of vd, which shares its stack slot). PC: 0. */
        PC_ONLY(frot.x = 0; frot.y = 0; frot.z = 0;)
        if (lc[i].slAttribute & attr) {
            continue;
        }
        if (func_8C029B00(cp, lc[i].pObject, lc[i].slAttribute) != 0) {
            if (ang != NULL && cp->normal2.y < 0.0f) {
                angx = SA1_FTOI(func_8C10D058(cp->normal2.z) * 10430.381f);
                angz = -SA1_FTOI(func_8C10D058(cp->normal2.x) * 10430.381f);
                angx2 = (Uint16)(0x8000 - angx);
                angz2 = (Uint16)(0x8000 - angz);
                diffx = DiffAngle(ang->x, angx2);
                diffz = DiffAngle(ang->z, angz2);
                if (diffx < diffz) {
                    cp->angx = angx2;
                    cp->angz = angz;
                } else {
                    cp->angx = angx;
                    cp->angz = angz2;
                }
            }
            tp = lc[i].pTask;
            if (tp != NULL) {
                fwp = tp->fwp;
                if (fwp != NULL) {
                    cp->pshbk.x += fwp->pos_spd.x;
                    cp->pshbk.y += fwp->pos_spd.y;
                    cp->pshbk.z += fwp->pos_spd.z;
                    frot = fwp->ang_spd;
                }
            } else {
                frot.x = 0;
                frot.y = 0;
                frot.z = 0;
            }
            v.x = cp->pshbk.x - pos->x;
            v.y = cp->pshbk.y - pos->y;
            v.z = cp->pshbk.z - pos->z;
            if (hit == 0) {
                best = cs;
                dist = njScalor(&v);
                rot = frot;
            } else {
                d = njScalor(&v);
                if (dist > d) {
                    best = cs;
                    dist = d;
                    rot = frot;
                }
            }
            if (njInnerProduct(&cp->normal2, &Gravity) > -0.5f) {
                hit |= 2;
            } else {
                hit |= 1;
            }
        }
    }
    if (hit) {
        bp = &best;
        *pos = bp->pshbk;
        *spd = bp->anaspdh;
        if (ang != NULL) {
            ang->x = bp->angx + rot.x;
            ang->y += rot.y;
            ang->z = bp->angz + rot.z;
        }
    }
    return hit;
}
#else
ASM_STUB(Sint32 func_8C056A04(NJS_POINT3 *pos, NJS_POINT3 *spd, Angle3 *ang, Uint32 attr, Float r))
#endif

/* Sets flag bits in the evalflags of every object of a NULL-terminated list
 * (E-102 uses it with NJD_EVAL_HIDE = 8 on D_8C422C88). */
void func_8C056D5E(NJS_OBJECT **objs, Uint32 flag)
{
    while (*objs != NULL) {
        (*objs++)->evalflags |= flag;
    }
}

/* Clears flag bits in the evalflags of every object of a NULL-terminated list. */
void func_8C056D76(NJS_OBJECT **objs, Uint32 flag)
{
    while (*objs != NULL) {
        (*objs++)->evalflags &= ~flag;
    }
}

/*
 * Stores the matrix of obj (parent, translate pos, rotate z, y, x when
 * non-zero, scale scl) at *mp, advances *mp by one matrix and recurses into
 * the children (depth first). Returns the sibling.
 */
NJS_OBJECT *func_8C056DC0(NJS_MATRIX_PTR *mp, NJS_OBJECT *obj, NJS_MATRIX_PTR parent)
{
    NJS_MATRIX m;
    NJS_OBJECT *child;

    njSetMatrix((NJS_MATRIX_PTR)&m, parent);
    njTranslate((NJS_MATRIX_PTR)&m, obj->pos[0], obj->pos[1], obj->pos[2]);
    obj->ang[2] ? njRotateZ((NJS_MATRIX_PTR)&m, obj->ang[2]) : (void)0;
    obj->ang[1] ? njRotateY((NJS_MATRIX_PTR)&m, obj->ang[1]) : (void)0;
    obj->ang[0] ? njRotateX((NJS_MATRIX_PTR)&m, obj->ang[0]) : (void)0;
    njScaleV((NJS_MATRIX_PTR)&m, (NJS_VECTOR *)obj->scl);
    njSetMatrix(*mp, (NJS_MATRIX_PTR)&m);
    *mp += 16;
    child = obj->child;
    while (child != NULL) {
        child = func_8C056DC0(mp, child, (NJS_MATRIX_PTR)&m);
    }
    return obj->sibling;
}

/*
 * Builds the node matrices of the object tree obj into B_8C788C44 (base =
 * NULL: relative to the current matrix) and returns node idx's matrix.
 */
NJS_MATRIX_PTR func_8C056E6C(NJS_MATRIX_PTR base, NJS_OBJECT *obj, Sint32 idx)
{
    NJS_MATRIX m;
    NJS_MATRIX_PTR mp;

    if (base == NULL) {
        njGetMatrix((NJS_MATRIX_PTR)&m);
    } else {
        njSetMatrix((NJS_MATRIX_PTR)&m, base);
    }
    mp = B_8C788C44[0];
    func_8C056DC0(&mp, obj, (NJS_MATRIX_PTR)&m);
    return B_8C788C44[idx];
}

/* Copies node matrix idx (built by func_8C056E6C) to m. */
void func_8C056EB2(NJS_MATRIX_PTR m, Sint32 idx)
{
    njSetMatrix(m, B_8C788C44[idx]);
}
