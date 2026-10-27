#include "sa1/pending.h"

/*
 * Big's head (1ST_READ 8C0E59C0-8C0E5D20): face animation and head morph,
 * both started by BigMain (8C0DFFA0). Big version of Sonic's code_8C0B2338.c.
 *
 *  func_8C0E59C0  face task: allocate the face work (PL_FACE, 0x20) into
 *                 tp->mwp and hand the task over to the face code 8C07B106
 *                 (exec) / 8C07B0F6 (dest)
 *  func_8C0E59F4  dest of the head morph task: put the original head model
 *                 back into the head object D_8C5291DC
 *  func_8C0E59FE  exec of the head morph task (taskwk + motionwk): keeps a
 *                 damped vertical "wobble" in the motionwk (spd.y amplitude,
 *                 ang_spd.z phase) driven by the player's vertical speed,
 *                 turns it into twp->ang.z and rebuilds the morph copy of
 *                 the head model: vertices with z beyond +-4 / +-8 are
 *                 rotated about X by a half / full angle (opposite signs on
 *                 both sides) with a pivot at y = -4.
 *
 * Head models: D_8C525C28 is an array of NJS_MODEL pointers {morph copy,
 * original}; [0] is drawn, [1] the original (the split labels its second
 * word D_8C525C2C).
 */

extern NJS_OBJECT D_8C5294EC;       /* Big's head node; its sibling is switched by the face code */
extern NJS_OBJECT D_8C5291DC[];     /* head object, model = original or morph copy */
extern NJS_OBJECT Big_EV_Head;      /* morph face object */
extern NJS_MOTION big_face_b_head;  /* face shape motion */
extern NJS_MODEL *D_8C525C28[2];    /* {morph copy, original} */

/* face task (8C07B080 chunk) */
void func_8C07B0F6(task *tp);
void func_8C07B106(task *tp);

void func_8C0E59C0(task *tp)
{
    PL_FACE *fp;

    if ((fp = CAlloc(1, sizeof(PL_FACE))) != NULL) {
        tp->mwp = (motionwk *)fp;
        fp->parent = &D_8C5294EC;
        fp->sibling = D_8C5291DC;
        fp->object = &Big_EV_Head;
        fp->motion = &big_face_b_head;
        tp->dest = func_8C07B0F6;
        tp->exec = func_8C07B106;
    }
}

void func_8C0E59F4(task *tp)
{
    D_8C5291DC->model = D_8C525C28[1];
}

/*
 * Matching notes:
 *  - the loop passes the matrices as "(NJS_MATRIX_PTR)&mN": SHC then hoists
 *    the four addresses in front of the loop (R8 and three spill slots) and
 *    recomputes them for the njUnitMatrix/njRotateX calls. Written as plain
 *    "mN", it CSEs some of them with the setup calls and keeps the rest in
 *    the loop; with pointer variables the spill slots come out in another
 *    order.
 *  - D_8C525C28[k] is read directly (a local "morph = D_8C525C28" copy, as in
 *    Sonic's version, swaps R6/R7 in the prologue and the FP registers of the
 *    hoisted -4/8/-8 constants).
 *  - "0.97999997f * spd.y" (constant first; the other order loads it through
 *    FPUL) and "dst->y -= 4.0f" ("dst->y = dst->y - 4.0f" becomes an FADD of
 *    the -4.0f constant).
 *  - the rotations are "a ? njRotateX(m, a) : (void)0" as in code_8C0B2338.c
 *    (the angle temporaries share the stack slot sp+0).
 */
void func_8C0E59FE(task *tp)
{
    NJS_MATRIX m0;
    NJS_MATRIX m1;
    NJS_MATRIX m2;
    NJS_MATRIX m3;
    NJS_VECTOR v;
    taskwk *twp;
    motionwk *mwp;
    taskwk *ptwp;
    motionwk *pmwp;
    NJS_POINT3 *dst;
    NJS_POINT3 *src;
    Uint32 n;
    Uint32 i;
    Float f;

    twp = tp->twp;
    mwp = tp->mwp;
    ptwp = playertwp[(Uint8)twp->counter.b[0]];
    pmwp = playermwp[(Uint8)twp->counter.b[0]];
    dst = D_8C525C28[0]->points;
    src = D_8C525C28[1]->points;
    n = D_8C525C28[1]->nbPoint;
    switch (twp->mode) {
    case 0:
        twp->mode = 1;
        tp->dest = func_8C0E59F4;
        break;
    case 1:
        if (ptwp == NULL) {
            twp->mode = 2;
            return;
        }
        if (pmwp->spd.y > _builtin_fabsf(mwp->spd.y)) {
            mwp->spd.y = pmwp->spd.y;
            mwp->ang_spd.z = 0x8000;
        } else if (pmwp->spd.y < -_builtin_fabsf(mwp->spd.y)) {
            mwp->spd.y = pmwp->spd.y;
            mwp->ang_spd.z = 0;
        }
        if (mwp->spd.y >= 0.19999999f || mwp->spd.y <= -0.19999999f) {
            mwp->spd.y = 0.97999997f * mwp->spd.y;
        } else {
            mwp->spd.y = 0.0f;
        }
        mwp->ang_spd.z += 0x1000;
        mwp->ang_spd.z &= 0xFE00;
        f = njCos(mwp->ang_spd.z);
        twp->ang.z = SA1_FTOI(f * mwp->spd.y * 819.19997f);
        njUnitMatrix(m0);
        twp->ang.z ? njRotateX(m0, twp->ang.z) : (void)0;
        njUnitMatrix(m1);
        (twp->ang.z >> 1) ? njRotateX(m1, twp->ang.z >> 1) : (void)0;
        njUnitMatrix(m2);
        -twp->ang.z ? njRotateX(m2, -twp->ang.z) : (void)0;
        njUnitMatrix(m3);
        (-twp->ang.z >> 1) ? njRotateX(m3, -twp->ang.z >> 1) : (void)0;
        for (i = 0; i < n; i++, src++, dst++) {
            if (src->z < -8.0f) {
                v.x = src->x;
                v.y = src->y + 4.0f;
                v.z = src->z;
                njCalcVector((NJS_MATRIX_PTR)&m0, &v, dst);
                dst->y -= 4.0f;
            } else if (src->z > 8.0f) {
                v.x = src->x;
                v.y = src->y + 4.0f;
                v.z = src->z;
                njCalcVector((NJS_MATRIX_PTR)&m2, &v, dst);
                dst->y -= 4.0f;
            } else if (src->z < -4.0f) {
                v.x = src->x;
                v.y = src->y + 4.0f;
                v.z = src->z;
                njCalcVector((NJS_MATRIX_PTR)&m1, &v, dst);
                dst->y -= 4.0f;
            } else if (src->z > 4.0f) {
                v.x = src->x;
                v.y = src->y + 4.0f;
                v.z = src->z;
                njCalcVector((NJS_MATRIX_PTR)&m3, &v, dst);
                dst->y -= 4.0f;
            }
        }
        break;
    default:
        DestroyTask(tp);
        break;
    }
    D_8C5291DC->model = D_8C525C28[0];
}
