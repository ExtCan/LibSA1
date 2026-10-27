#include "common.h"
#include "ninja.h"
#include "sa1/ninja_funcs.h"
#include "sa1/engine.h"

/*
 * Node matrices of a motion (8C057BC0-8C0584A0).
 * func_8C057BC0 walks an object tree like njDrawMotion, but instead of
 * drawing it evaluates the motion at one frame (linear interpolation between
 * the two surrounding keys, wrapping to key 0 after the last frame) and
 * stores each node's matrix (njGetMatrix) into a caller buffer, for the weld
 * code (func_8C0583A0, func_8C058434). Returns the sibling so the caller can
 * walk a level; children are walked here.
 */

/* Ninja motion keys and motion data (Ninja SDK layout) */
typedef struct {
    Uint32 keyframe;            /* 0x00 */
    Float key[3];               /* 0x04 */
} NJS_MKEY_F;                   /* 0x10 */

typedef struct {
    Uint32 keyframe;            /* 0x00 */
    Angle key[3];               /* 0x04 */
} NJS_MKEY_A;                   /* 0x10 */

typedef struct {
    void *p[2];                 /* 0x00 pos keys (NJS_MKEY_F), ang keys (NJS_MKEY_A) */
    Uint32 nb[2];               /* 0x08 */
} NJS_MDATA2;                   /* 0x10 */

typedef struct {
    void *p[3];                 /* 0x00 pos, ang, scl keys */
    Uint32 nb[3];               /* 0x0C */
} NJS_MDATA3;                   /* 0x18 */

/* Evaluation context built on the stack by func_8C0583A0 (8C0583DE) */
typedef struct {
    void *mdata;                /* 0x00 NJS_MDATA2/3 of the current node (NJS_MOTION.mdata) */
    NJS_MATRIX *mtx;            /* 0x04 output matrix of the current node */
    Uint32 type;                /* 0x08 NJS_MOTION.type */
    Uint32 nbFrame;             /* 0x0C NJS_MOTION.nbFrame */
    Float frame;                /* 0x10 */
} MOTION_MTX_CTX;               /* 0x14 */

#define NJD_EVAL_UNIT_POS 0x01
#define NJD_EVAL_UNIT_ANG 0x02
#define NJD_EVAL_UNIT_SCL 0x04

extern Uint32 B_8C78B448;       /* nodes evaluated so far (func_8C05842E returns it) */
extern Uint32 B_8C78B44C;       /* node limit (func_8C0583A0: n + 1, at most 0x80) */

#ifdef NON_MATCHING
/*
 * NON_MATCHING (SHC 5.1r03: 582 instructions, same count and control flow
 * except 2 instructions; 87 lines differ, all register/stack-slot choices):
 *  - case 2, keyed rotation: the x and z key angles get stack slots @(12,R15)
 *    and @(8,R15) instead of @(8,R15) and @(12,R15);
 *  - case 3, keyed scale: the key pointers are coalesced with kf/nf (R4/R5)
 *    instead of being copied to R6/R4 as in the two translation cases,
 *    which saves the two MOVs and shifts every later branch target.
 * Both are global allocator tie-breaks: compiling case 2 or case 3 alone
 * gives the original shape. Things that were needed to get this close: a
 * separate loop index per case (else the i << 2 CSE is dropped), `continue`
 * in the NULL-key path (loop entry jumps to the test), obj->ang read
 * directly in the NULL path (spilled CSE temp at @R15), n++ first in the
 * bit-count loop and kp before ap in the rotation case (SHC schedules the
 * later of two ready instructions first).
 */
NJS_OBJECT *func_8C057BC0(NJS_OBJECT *obj, MOTION_MTX_CTX *ctx)
{
    Angle *ap;
    Angle *kp;
    Angle a1;
    Angle a0;
    Angle a2;
    Sint32 d0;
    Sint32 d1;
    Sint32 d2;
    NJS_MDATA2 *m2;
    NJS_MDATA3 *m3;
    NJS_MKEY_F *kf;
    NJS_MKEY_F *nf;
    NJS_OBJECT *child;
    Float *fp;
    Float *fq;
    Uint32 type;
    Uint32 bit;
    Uint32 frame;
    Float rate;
    Float x;
    Float y;
    Float z;
    Sint32 n;
    Sint32 i;
    Sint32 j;

    if (++B_8C78B448 > B_8C78B44C) {
        B_8C78B448 = B_8C78B44C;
        return NULL;
    }
    type = ctx->type;
    for (n = 0, bit = 1; n < 5 && type != 0;) {
        n++;
        type &= ~bit;
        bit <<= 1;
    }
    njPushMatrix(NULL);
    switch (n) {
    case 2:
        m2 = (NJS_MDATA2 *)ctx->mdata;
        for (i = 0; i < 2; i++) {
            if (m2->p[i] == NULL) {
                switch (i) {
                case 0:
                    njTranslateV(NULL, (NJS_VECTOR *)obj->pos);
                    break;
                case 1:
                    if (!(obj->evalflags & NJD_EVAL_UNIT_ANG)) {
                        njRotateXYZ(NULL, obj->ang[0], obj->ang[1], obj->ang[2]);
                    }
                    break;
                }
                continue;
            }
            frame = (Uint32)ctx->frame;
            kf = (NJS_MKEY_F *)m2->p[i];
            nf = kf;
            nf++;
            while (frame > kf->keyframe && frame >= nf->keyframe) {
                kf++;
                nf++;
            }
            if (kf->keyframe == ctx->nbFrame - 1) {
                nf = (NJS_MKEY_F *)m2->p[i];
                rate = ctx->frame - (Float)kf->keyframe;
            } else {
                rate = (ctx->frame - (Float)kf->keyframe) / (Float)(nf->keyframe - kf->keyframe);
            }
            switch (i) {
            case 0:
                fp = kf->key;
                fq = nf->key;
                x = SA1_FMAC(*fq - *fp, rate, *fp);
                fp++;
                fq++;
                y = SA1_FMAC(*fq - *fp, rate, *fp);
                fp++;
                fq++;
                z = SA1_FMAC(*fq - *fp, rate, *fp);
                njTranslate(NULL, x, y, z);
                break;
            case 1:
                kp = ((NJS_MKEY_A *)kf)->key;
                ap = ((NJS_MKEY_A *)nf)->key;
                a0 = *kp++;
                a1 = *kp++;
                a2 = *kp;
                d0 = SubAngle(a0, *ap++);
                d1 = SubAngle(a1, *ap++);
                d2 = SubAngle(a2, *ap);
                d0 = a0 + SA1_FTOI((Float)d0 * rate);
                d1 = a1 + SA1_FTOI((Float)d1 * rate);
                d2 = a2 + SA1_FTOI((Float)d2 * rate);
                njRotateXYZ(NULL, d0, d1, d2);
                break;
            }
        }
        if (!(obj->evalflags & NJD_EVAL_UNIT_SCL)) {
            njScaleV(NULL, (NJS_VECTOR *)obj->scl);
        }
        ctx->mdata = m2 + 1;
        break;
    case 3:
        m3 = (NJS_MDATA3 *)ctx->mdata;
        for (j = 0; j < 3; j++) {
            if (m3->p[j] == NULL) {
                switch (j) {
                case 0:
                    if (!(obj->evalflags & NJD_EVAL_UNIT_POS)) {
                        njTranslateV(NULL, (NJS_VECTOR *)obj->pos);
                    }
                    break;
                case 1:
                    if (!(obj->evalflags & NJD_EVAL_UNIT_ANG)) {
                        njRotateXYZ(NULL, obj->ang[0], obj->ang[1], obj->ang[2]);
                    }
                    break;
                case 2:
                    if (!(obj->evalflags & NJD_EVAL_UNIT_SCL)) {
                        njScaleV(NULL, (NJS_VECTOR *)obj->scl);
                    }
                    break;
                }
                continue;
            }
            frame = (Uint32)ctx->frame;
            kf = (NJS_MKEY_F *)m3->p[j];
            nf = kf;
            nf++;
            while (frame > kf->keyframe && frame >= nf->keyframe) {
                kf++;
                nf++;
            }
            if (kf->keyframe == ctx->nbFrame - 1) {
                nf = (NJS_MKEY_F *)m3->p[j];
                rate = ctx->frame - (Float)kf->keyframe;
            } else {
                rate = (ctx->frame - (Float)kf->keyframe) / (Float)(nf->keyframe - kf->keyframe);
            }
            switch (j) {
            case 0:
                fp = kf->key;
                fq = nf->key;
                x = SA1_FMAC(*fq - *fp, rate, *fp);
                fp++;
                fq++;
                y = SA1_FMAC(*fq - *fp, rate, *fp);
                fp++;
                fq++;
                z = SA1_FMAC(*fq - *fp, rate, *fp);
                njTranslate(NULL, x, y, z);
                break;
            case 1:
                kp = ((NJS_MKEY_A *)kf)->key;
                ap = ((NJS_MKEY_A *)nf)->key;
                a0 = *kp++;
                a1 = *kp++;
                a2 = *kp;
                d0 = SubAngle(a0, *ap++);
                d1 = SubAngle(a1, *ap++);
                d2 = SubAngle(a2, *ap);
                d0 = a0 + SA1_FTOI((Float)d0 * rate);
                d1 = a1 + SA1_FTOI((Float)d1 * rate);
                d2 = a2 + SA1_FTOI((Float)d2 * rate);
                njRotateXYZ(NULL, d0, d1, d2);
                break;
            case 2:
                fp = kf->key;
                fq = nf->key;
                x = SA1_FMAC(*fq - *fp, rate, *fp);
                fp++;
                fq++;
                y = SA1_FMAC(*fq - *fp, rate, *fp);
                fp++;
                fq++;
                z = SA1_FMAC(*fq - *fp, rate, *fp);
                njScale(NULL, x, y, z);
                break;
            }
        }
        ctx->mdata = m3 + 1;
        break;
    default:
        for (;;) {
        }
    }
    njGetMatrix(*ctx->mtx);
    ctx->mtx++;
    child = obj->child;
    while (child != NULL) {
        child = func_8C057BC0(child, ctx);
    }
    njPopMatrix(1);
    return obj->sibling;
}
#endif /* NON_MATCHING */
