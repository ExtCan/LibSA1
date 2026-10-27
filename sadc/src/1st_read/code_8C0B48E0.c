/*
 * 1ST_READ 8C0B48E0-8C0B4E96 (end of chunk asm/1st_read/text/8C0B33A0.src,
 * functions 25-28 of 28): Sonic's trail ("dash") effect task. func_8C0B48B4
 * (code_8C0B4600.c) installs exec/disp/dest = 8C0B4B20/8C0B4A48/8C0B4E62
 * when func_8C0B48E0 succeeds. The task keeps a private copy of the
 * Sonic_Effect_Trail object (object, model and point array) in
 * twp->value.ptr and the frame count in twp->wtimer.
 *
 *  func_8C0B48E0  init: copy the object, model and points (MAlloc); start at
 *                 the centre of the player's first collision shape.
 *                 Returns 1, or 0 when an allocation fails.
 *  func_8C0B4A48  disp: texlist D_8C1C695C[wtimer - 1] (D_8C1C69FC[] when
 *                 flag & 8), translate pos, rotate ang.y/ang.z, scale scl,
 *                 draw the copy (the original object on frame 1).
 *  func_8C0B4B20  exec: frees the task after 20 frames (at wtimer 19 while
 *                 flag & 8), else draws via 8C0B4A48. Frame 1: pos = player
 *                 centre (pos + rotated (0, center_height, 0)), ang/scl.x
 *                 point the trail from the start position to it (FreeTask
 *                 when scl.x >= 5); flag |= 8 unless free.b[0] has both
 *                 0x04 and 0x02 set. Frame 2: the odd points of the copy move
 *                 so that the trail reaches the player's new centre.
 *  func_8C0B4E62  dest: frees the copies.
 */

#include "common.h"
#include "ninja.h"
#include "task.h"
#include "sa1/sa1.h"

extern NJS_OBJECT Sonic_Effect_Trail;   /* 8C255B10 (figure/sonic/models/sonic_dash.nja) */
extern NJS_TEXLIST D_8C1C695C[];        /* trail texlists, indexed by wtimer - 1 */
extern NJS_TEXLIST D_8C1C69FC[];        /* the same while twp->flag & 8 */
extern NJS_MATRIX B_8C7A1E48;           /* scratch matrix */

/*
 * The original translation unit starts at func_8C0B4600 (code_8C0B4600.c):
 * SHC dumps the literal pool it shares with func_8C0B4724..func_8C0B48B4
 * inside func_8C0B48E0 (8C0B4978-8C0B49BF), which turns the first
 * "return 0" into a far branch (BF +6; BRA at 8C0B4902). These stubs (code
 * defined in code_8C0B4600.c) reproduce that pool placement; the splicer
 * skips stub bodies and they are plain prototypes on PC.
 */
ASM_STUB(void func_8C0B4724(task *tp))
ASM_STUB(void func_8C0B4820(task *tp))
/* direction of v as angles (x = 0, y = atan2(-z, x), z = asin(y / |v|)) */
ASM_STUB(void func_8C0B4860(NJS_VECTOR *v, Angle3 *ang))
ASM_STUB(void func_8C0B48B4(task *tp))

#ifdef NON_MATCHING
/*
 * Complete and functionally equivalent. With the stubs above the
 * instruction count and the far branch match (144 instructions); the only
 * difference left is register allocation in the inline NJS_MODEL copy
 * (8C0B494A-8C0B4976): the original has the source (org->model) in R3 and
 * the destination (obj->model) in R2, this compile swaps them (22
 * instructions).
 *
 * Tried without effect: *a = *b, a[0] = b[0], (void *) and same-type casts,
 * copy through a 40-byte block type, Sonic_Effect_Trail.model instead of
 * org->model (and all-global forms without `org`), src/dst temporaries and
 * `*(s = org->model)`, a static helper (SHC inlines it with parameters in
 * R4/R5), memcpy (a call), every declaration order (random permutations: no
 * change at all), Uint32/Sint32 for n and i, separate assignment and test of
 * obj->model, == NULL with early return, nested ifs and gotos (different
 * layout), several forms of the player tail (pno local, combined
 * assignment/test, no info local), and compiling it after the real
 * code_8C0B4600.c functions (same result). Second session, also no effect
 * (22): initialisers in the declarations, a `ret` local, `twp` as a copy
 * of the parameter, `*(*org).model`, Sint32 n; worse: `register` on
 * obj/org, block-scoped model temporaries in either order (addresses move
 * to R4/R5 and twp/MAlloc swap R10/R11), the NULL test first, an indexed
 * points loop, storing twp->value.ptr before the object copy.
 * Observation: the allocation flips to the original one whenever twp is not
 * kept in a callee-saved register (e.g. with the tail reading playertwp[0],
 * twp is spilled to the stack and the copy gets src R3 / dst R2), so the
 * tie between the two copy temporaries depends on the global allocation;
 * with the original's R8-R14 assignment no source form found reproduces it.
 * The `org` local reproduces R13 = &Sonic_Effect_Trail / R12 = n, and the
 * `info` local the register use of the final position copy.
 */
Sint32 func_8C0B48E0(taskwk *twp)
{
    NJS_OBJECT *obj;
    NJS_POINT3 *dst;
    NJS_POINT3 *src;
    Uint32 i;
    taskwk *ptwp;
    CCL_INFO *info;
    Uint32 n;
    NJS_OBJECT *org;

    org = &Sonic_Effect_Trail;
    n = org->model->nbPoint;
    obj = (NJS_OBJECT *)MAlloc(sizeof(NJS_OBJECT));
    if (obj == NULL) {
        return 0;
    }
    *obj = *org;
    twp->value.ptr = obj;
    if ((obj->model = (NJS_MODEL *)MAlloc(sizeof(NJS_MODEL))) != NULL) {
        *obj->model = *org->model;
    } else {
        MFree(obj);
        return 0;
    }
    if ((obj->model->points = (NJS_POINT3 *)MAlloc(n * sizeof(NJS_POINT3))) != NULL) {
        dst = obj->model->points;
        src = org->model->points;
        for (i = 0; i < n; i++) {
            dst->x = src->x;
            dst->y = src->y;
            dst->z = src->z;
            dst++;
            src++;
        }
    } else {
        MFree(obj->model);
        MFree(obj);
        return 0;
    }
    ptwp = playertwp[(Uint8)twp->counter.b[0]];
    if (ptwp) {
        info = CWP(ptwp)->info;
        twp->pos = info->center;
    }
    return 1;
}
#else
ASM_STUB(Sint32 func_8C0B48E0(taskwk *twp))
#endif

/*
 * func_8C036F12 draws an NJS_OBJECT (8C036BC0 dispatches on evalflags & 7);
 * sa1/engine.h still declares it with an NJS_MODEL * parameter, hence the
 * casts.
 */
void func_8C0B4A48(task *tp)
{
    taskwk *twp;
    NJS_OBJECT *obj;

    twp = tp->twp;
    obj = (NJS_OBJECT *)twp->value.ptr;
    if (twp->flag & 8) {
        njSetTexture(&D_8C1C69FC[twp->wtimer - 1]);
    } else {
        njSetTexture(&D_8C1C695C[twp->wtimer - 1]);
    }
    njPushMatrix(NULL);
    njTranslateV(NULL, &twp->pos);
    twp->ang.y ? njRotateY(NULL, twp->ang.y) : (void)0;
    twp->ang.z ? njRotateZ(NULL, twp->ang.z) : (void)0;
    njScaleV(NULL, &twp->scl);
    if (twp->wtimer == 1) {
        func_8C036F12((NJS_MODEL *)&Sonic_Effect_Trail);
    } else {
        func_8C036F12((NJS_MODEL *)obj);
    }
    njPopMatrix(1);
}

/*
 * Matching notes: the unit/scratch matrices are passed as
 * (NJS_MATRIX_PTR)&array (with the plain array names SHC keeps D_8C67C640 or
 * B_8C7A1E48 in a callee-saved register instead of njPushMatrix / &ang), the
 * second-frame angles go through the pointer `ap` (held in R11), and the
 * flag test re-reads pwp->free.b[0] for each term.
 */
void func_8C0B4B20(task *tp)
{
    taskwk *twp;
    NJS_OBJECT *obj;
    taskwk *ptwp;
    Uint32 n;
    Uint32 i;
    NJS_POINT3 *pts;
    playerwk *pwp;
    NJS_VECTOR v1;
    NJS_VECTOR v2;
    Angle3 ang;
    Angle3 *ap;
    NJS_OBJECT *org;

    twp = tp->twp;
    obj = (NJS_OBJECT *)twp->value.ptr;
    if (twp->wtimer++ >= 20) {
        FreeTask(tp);
        tp->disp = NULL;
        return;
    } else if ((twp->flag & 8) && twp->wtimer >= 19) {
        FreeTask(tp);
        return;
    }
    if (twp->wtimer == 1) {
        ptwp = playertwp[(Uint8)twp->counter.b[0]];
        if (ptwp) {
            v1.x = v1.z = 0.0f;
            v1.y = GetPlayerWorkPtr(twp->counter.b[0])->p.center_height;
            njPushMatrix((NJS_MATRIX_PTR)&D_8C67C640);
            ptwp->ang.z ? njRotateZ(NULL, ptwp->ang.z) : (void)0;
            ptwp->ang.x ? njRotateX(NULL, ptwp->ang.x) : (void)0;
            -ptwp->ang.y ? njRotateY(NULL, -ptwp->ang.y) : (void)0;
            njCalcVector(NULL, &v1, &v2);
            njPopMatrix(1);
            njAddVector(&v2, &ptwp->pos);
            v1 = twp->pos;
            twp->pos = v2;
            njSubVector(&v1, &v2);
            func_8C0B4860(&v1, &twp->ang);
            twp->scl.x = njScalor(&v1) * 0.125f;
            if (!(twp->scl.x < 5.0f)) {
                FreeTask(tp);
                return;
            }
            twp->scl.y = 1.0f;
            twp->scl.z = 1.0f;
            pwp = PWP(playermwp[(Uint8)twp->counter.b[0]]);
            if (((pwp->free.b[0] & 4) != 0 && (pwp->free.b[0] & 2) == 0) || (pwp->free.b[0] & 4) == 0) {
                twp->flag |= 8;
            }
        }
    } else if (twp->wtimer == 2) {
        ptwp = playertwp[(Uint8)twp->counter.b[0]];
        if (ptwp) {
            org = &Sonic_Effect_Trail;
            pts = obj->model->points;
            n = org->model->nbPoint;
            v1.x = v1.z = 0.0f;
            v1.y = GetPlayerWorkPtr(twp->counter.b[0])->p.center_height;
            njPushMatrix((NJS_MATRIX_PTR)&D_8C67C640);
            ptwp->ang.z ? njRotateZ(NULL, ptwp->ang.z) : (void)0;
            ptwp->ang.x ? njRotateX(NULL, ptwp->ang.x) : (void)0;
            -ptwp->ang.y ? njRotateY(NULL, -ptwp->ang.y) : (void)0;
            njCalcVector(NULL, &v1, &v2);
            njPopMatrix(1);
            njAddVector(&v2, &ptwp->pos);
            v1 = twp->pos;
            njSubVector(&v1, &v2);
            if (njScalor(&v1) != 0.0f) {
                ap = &ang;
                func_8C0B4860(&v1, ap);
                njUnitMatrix((NJS_MATRIX_PTR)&B_8C7A1E48);
                ap->y ? njRotateY((NJS_MATRIX_PTR)&B_8C7A1E48, ap->y) : (void)0;
                ap->z ? njRotateZ((NJS_MATRIX_PTR)&B_8C7A1E48, ap->z) : (void)0;
                njScaleV((NJS_MATRIX_PTR)&B_8C7A1E48, &twp->scl);
                njPushMatrix((NJS_MATRIX_PTR)&D_8C67C640);
                twp->ang.y ? njRotateY(NULL, twp->ang.y) : (void)0;
                twp->ang.z ? njRotateZ(NULL, twp->ang.z) : (void)0;
                njScaleV(NULL, &twp->scl);
                njInvertMatrix(NULL);
                njMultiMatrix(NULL, (NJS_MATRIX_PTR)&B_8C7A1E48);
                pts++;
                for (i = 0; i < n; i += 2) {
                    v1 = *pts;
                    njCalcPoint(NULL, &v1, pts);
                    pts += 2;
                }
                njPopMatrix(1);
            }
        }
    }
    func_8C0B4A48(tp);
}

void func_8C0B4E62(task *tp)
{
    taskwk *twp;
    NJS_OBJECT *obj;

    twp = tp->twp;
    obj = (NJS_OBJECT *)twp->value.ptr;
    if (obj->model->points) {
        MFree(obj->model->points);
    }
    if (obj->model) {
        MFree(obj->model);
    }
    if (obj) {
        MFree(obj);
    }
}
