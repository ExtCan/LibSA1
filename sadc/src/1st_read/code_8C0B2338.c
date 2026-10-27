#include "sa1/pending.h"

/*
 * Sonic's head (1ST_READ 8C0B2338-8C0B2890): face animation, head morph and
 * the event "look at" task. All four are started by SonicMain (8C0A77BC).
 *
 *  func_8C0B2338  exec of a CreateElementalTask(2, 3, ...) task: allocate the
 *                 face work (struct pl_face, 0x20) into tp->mwp, then hand the
 *                 task over to the face code 8C07B106 (exec) / 8C07B0F6
 *                 (dest)
 *  func_8C0B236C  dest of the head morph task: put the original head model
 *                 back into the head object D_8C1CD82C
 *  func_8C0B2376  exec of the head morph task (CreateElementalTask(3, 3, ...),
 *                 im = 3: taskwk + motionwk). Keeps a damped "wobble" in the
 *                 motionwk (spd.y/spd.z amplitudes, ang_spd phases) driven by
 *                 the player's vertical speed and speed, turns it into
 *                 twp->ang, and (when B_8C754EC8 != 0) rebuilds the morph
 *                 copy of the head model: vertices of the original model
 *                 behind x < -2.0 and x < -1.2 are rotated by the full and
 *                 half angle about a pivot at y = -2.0; a third pass (quarter
 *                 angle, x < -0.6) runs on past the points into the normals
 *                 (see the notes at the function).
 *  func_8C0B27EE  create the look-at task 8C0B28EE for player pno with the
 *                 head and eye objects objs[0..2]
 *
 * Works: the face task keeps a PL_FACE and the look-at task a lookwk
 * (sa1/player.h, names from sa1/pending.h) in tp->mwp.
 *
 * Head models: D_8C1C5F14 is one array of 4 NJS_MODEL pointers
 * {D_8C1D9B60, sonic_z, D_8C1D9B60, sonic_z}; [0] is the morphed copy that
 * is drawn, [1] the original (the split labels its second word D_8C1C5F18).
 */

extern NJS_OBJECT D_8C1CDABC;   /* Sonic's head node; its sibling is switched by the face code */
extern NJS_OBJECT D_8C1CD82C[]; /* head object, model = sonic_z (original) or D_8C1D9B60 (morph) */
extern NJS_OBJECT Sonic_EV_Head;
extern NJS_MOTION sonic_face_s_head;
extern NJS_MODEL *D_8C1C5F14[4]; /* {morph copy, original, morph copy, original} */

/* face task (8C07B080 chunk) */
void func_8C07B0F6(task *tp);
void func_8C07B106(task *tp);
/* look-at task exec (8C0B2890 chunk) */
void func_8C0B28EE(task *tp);
/* camera-relative vector from a world position (8C046F48) */
Sint32 func_8C046F48(NJS_POINT3 *pos, NJS_VECTOR *v);

void func_8C0B2338(task *tp)
{
    PL_FACE *fp;

    if ((fp = CAlloc(1, sizeof(PL_FACE))) != NULL) {
        tp->mwp = (motionwk *)fp;
        fp->parent = &D_8C1CDABC;
        fp->sibling = D_8C1CD82C;
        fp->object = &Sonic_EV_Head;
        fp->motion = &sonic_face_s_head;
        tp->dest = func_8C07B0F6;
        tp->exec = func_8C07B106;
    }
}

void func_8C0B236C(task *tp)
{
    D_8C1CD82C->model = D_8C1C5F14[1];
}

/*
 * Matching notes:
 *  - the rotations are written "a ? njRotateY(NULL, a) : (void)0". SHC keeps
 *    the common subexpressions (ang.y >> 1, angz >> 1, ...) in stack slots
 *    (stored in the BT/S delay slot, the call argument forwarded from the
 *    register; they share sp+4 with the &D_8C1C5F14[1] temporary).
 *  - the full-angle Y rotation goes through angy, a memory variable
 *    ((void)&angy: own slot sp+8, reloaded for the call). Written as a
 *    plain CSE like the others, angz instead of n gets R8; written with an
 *    if, R2/R3 swap in the Z test.
 *  - the third pass does not reset dst/src: on the DC the points arrays of
 *    both head models are directly followed by their normals (8C1D8828 +
 *    205 * 12 = 8C1D91C4, 8C1CC4CC + 205 * 12 = 8C1CCE68), so it rotates
 *    the normals (x < -0.6) by the quarter angle, with the same +-2.0 y
 *    pivot as the points. The PC build points dst/src at the normals
 *    explicitly, as its ripped models keep them in separate arrays.
 */
void func_8C0B2376(task *tp)
{
    NJS_VECTOR v;
    NJS_VECTOR va;
    NJS_VECTOR vb;
    taskwk *twp;
    motionwk *mwp;
    taskwk *ptwp;
    motionwk *pmwp;
    playerwk *pwp;
    NJS_MODEL **morph;
    NJS_POINT3 *dst;
    Float *src;
    Uint32 n;
    Uint32 i;
    Float spd;
    Float f;
    Angle angz;
    Angle angy;

    (void)&angy;
    twp = tp->twp;
    mwp = tp->mwp;
    ptwp = playertwp[(Uint8)twp->counter.b[0]];
    pmwp = playermwp[(Uint8)twp->counter.b[0]];
    morph = D_8C1C5F14;
    dst = morph[0]->points;
    src = (Float *)morph[1]->points;
    n = morph[1]->nbPoint;
    switch (twp->mode) {
    case 0:
        twp->mode = 1;
        tp->dest = func_8C0B236C;
        break;
    case 1:
        if (ptwp == NULL) {
            twp->mode = 2;
            return;
        }
        /* vertical wobble: restarted by a vertical speed beyond its amplitude */
        if (pmwp->spd.y > _builtin_fabsf(mwp->spd.y)) {
            mwp->spd.y = pmwp->spd.y;
            mwp->ang_spd.z = 0x8000;
        } else if (pmwp->spd.y < -_builtin_fabsf(mwp->spd.y)) {
            mwp->spd.y = pmwp->spd.y;
            mwp->ang_spd.z = 0;
        }
        /* horizontal wobble: amplitude and direction from the player's motion */
        va.x = va.y = va.z = 0.0f;
        func_8C046F48(&ptwp->pos, &va);
        vb.x = 1.0f;
        vb.y = 0.0f;
        vb.z = 0.0f;
        PConvertVector_P2G(ptwp, &vb);
        vb.y = 0.0f;
        if (njUnitVector(&vb) != 0.0f) {
            pwp = GetPlayerWorkPtr(0);
            mwp->spd.z = njScalor(&va);
            if ((spd = njScalor(&pmwp->spd)) > 0.19999999f) {
                mwp->spd.z = spd * SA1_FMAC((Float)rand(), 0.000030517578125f, 1.0f) * 0.25f;
            }
            njUnitVector(&va);
            mwp->ang_spd.y = VectorAngle(&vb, &va, &pwp->cstsp->tnorm);
            mwp->ang_spd.x = SA1_FTOI(mwp->spd.z * 6553.5997f);
        }
        if (mwp->spd.y >= 0.19999999f || mwp->spd.y <= -0.19999999f) {
            mwp->spd.y *= 0.97999997f;
        } else {
            mwp->spd.y = 0.0f;
        }
        mwp->ang_spd.z = (mwp->ang_spd.z + 0x1000) & 0xFE00;
        f = njCos(mwp->ang_spd.z);
        twp->ang.z = SA1_FTOI(f * mwp->spd.y * 819.19997f);
        if (mwp->spd.z >= 0.19999999f || mwp->spd.z <= -0.19999999f) {
            mwp->spd.z *= 0.97999997f;
        } else {
            mwp->spd.z = 0.0f;
        }
        mwp->ang_spd.y = (mwp->ang_spd.y + 0x1000) & 0xFE00;
        f = njCos(mwp->ang_spd.y);
        twp->ang.y = SA1_FTOI(f * mwp->spd.z * 1638.39993f);
        f = njSin(mwp->ang_spd.x);
        twp->ang.x = SA1_FTOI(f * mwp->spd.z * 409.59998f);
        if (B_8C754EC8 != 0) {
            /* rebuild the morph copy: three nested pivots at y = -2.0 */
            angz = twp->ang.x + twp->ang.z;
            njPushMatrix(D_8C67C640);
            (angy = twp->ang.y) ? njRotateY(NULL, angy) : (void)0;
            angz ? njRotateZ(NULL, angz) : (void)0;
            for (i = n; i > 0; i--) {
                if (*src < -2.0f) {
                    v.x = *src++;
                    v.y = *src++ + 2.0f;
                    v.z = *src++;
                    njCalcVector(NULL, &v, dst);
                    dst->y -= 2.0f;
                } else {
                    src += 3;
                }
                dst++;
            }
            dst = morph[0]->points;
            src = (Float *)morph[1]->points;
            njPushMatrix(D_8C67C640);
            (twp->ang.y >> 1) ? njRotateY(NULL, twp->ang.y >> 1) : (void)0;
            (angz >> 1) ? njRotateZ(NULL, angz >> 1) : (void)0;
            for (i = n; i > 0; i--) {
                if (*src < -1.19999996f) {
                    v.x = *src++;
                    v.y = *src++ + 2.0f;
                    v.z = *src++;
                    njCalcVector(NULL, &v, dst);
                    dst->y -= 2.0f;
                } else {
                    src += 3;
                }
                dst++;
            }
            /* no reset: continues into the normals (see above) */
            PC_ONLY(dst = morph[0]->normals;)
            PC_ONLY(src = (Float *)morph[1]->normals;)
            njPushMatrix(D_8C67C640);
            (twp->ang.y >> 2) ? njRotateY(NULL, twp->ang.y >> 2) : (void)0;
            (angz >> 2) ? njRotateZ(NULL, angz >> 2) : (void)0;
            for (i = n; i > 0; i--) {
                if (*src < -0.59999998f) {
                    v.x = *src++;
                    v.y = *src++ + 2.0f;
                    v.z = *src++;
                    njCalcVector(NULL, &v, dst);
                    dst->y -= 2.0f;
                } else {
                    src += 3;
                }
                dst++;
            }
            njPopMatrix(3);
        }
        break;
    default:
        DestroyTask(tp);
        break;
    }
    D_8C1CD82C->model = morph[0];
}

Bool func_8C0B27EE(Sint8 pno, NJS_OBJECT **objs)
{
    task *tp;
    lookwk *lwp;

    if ((tp = CreateElementalTask(2, 2, func_8C0B28EE)) != NULL) {
        if ((lwp = MAlloc(sizeof(lookwk))) == NULL) {
            FreeTask(tp);
            return FALSE;
        }
        tp->mwp = (motionwk *)lwp;
        tp->twp->counter.b[0] = pno;
        lwp->head = objs[0];
        lwp->eyeL = objs[1];
        lwp->eyeR = objs[2];
        goto ok;  /* the success return is laid out after the failure one */
    }
    return FALSE;
ok:
    return TRUE;
}
