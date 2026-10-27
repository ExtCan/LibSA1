/*
 * Player face morphing (1ST_READ 8C07B080-8C07B2B8, chunk
 * asm/1st_read/text/8C07B080.src).
 *
 * The event characters (creators 8C0B2338, 8C0BF76A, 8C0C8E8E, 8C0DDDB2,
 * 8C0E59C0) CAlloc a 0x20-byte face work, keep it in tp->mwp and install
 * func_8C07B106 as exec and func_8C07B0F6 as dest. The face work names a
 * "parent" object of the character model whose sibling link is switched
 * between the static face (the original sibling) and a morph face object
 * whose vertices are blended between two keys of a shape motion.
 *
 * func_8C07B080  blend the vertex keys of every node of an object tree
 *                (one NJS_MDATA2 per node, in tree order)
 * func_8C07B0F6  dest: restore the original sibling (static face)
 * func_8C07B106  exec: play the face work's own sequence (seq, set by
 *                8C07B2B8) or follow the player's event-work face
 */
#include "common.h"
#include "ninja.h"
#include "task.h"
#include "sa1/engine.h"
#include "sa1/player.h"

/* Ninja SDK shape-motion types (not yet in include/sa1/ninja_types.h) */
typedef struct {
    Uint32 keyframe;            /* 0x00 */
    NJS_POINT3 *key;            /* 0x04 */
} NJS_MKEY_P;                   /* 0x08 */

typedef struct {
    NJS_MKEY_P *p[2];           /* 0x00 SDK: void *; p[0] = vertex keys, p[1] = normal keys */
    Uint32 nb[2];               /* 0x08 */
} NJS_MDATA2;                   /* 0x10 */

/* one step of a face sequence (game data D_8C19C32C[n]; time 0 ends it) */
typedef struct {
    Sint8 time;                 /* 0x00 frames to blend to face */
    Sint8 face;                 /* 0x01 key index */
} PL_FACE_STEP;                 /* 0x02 */

/* face work (playerwk.pfp; CAlloc(1, 0x20) by the creators, kept in tp->mwp) */
struct pl_face {
    Sint8 face;                 /* 0x00 key index blended from */
    Sint8 face_next;            /* 0x01 key index blended to */
    Sint8 unk02;                /* 0x02 not seen */
    Sint8 step;                 /* 0x03 current step of seq */
    Float rate;                 /* 0x04 blend ratio 0..1 */
    Float rate_spd;             /* 0x08 ratio step per frame */
    const PL_FACE_STEP *seq;    /* 0x0C face sequence (8C07B2B8: D_8C19C32C[n]) */
    NJS_OBJECT *parent;         /* 0x10 node whose sibling is switched */
    NJS_OBJECT *sibling;        /* 0x14 original sibling (static face) */
    NJS_OBJECT *object;         /* 0x18 morph face object */
    NJS_MOTION *motion;         /* 0x1C shape motion: mdata = NJS_MDATA2[] */
};                              /* 0x20 */
typedef struct pl_face PL_FACE;

SA1_ASSERT_SIZE(NJS_MDATA2, 0x10);
SA1_ASSERT_SIZE(NJS_MKEY_P, 0x08);
SA1_ASSERT_SIZE(PL_FACE, 0x20);
SA1_ASSERT_OFFSET(PL_FACE, rate, 0x04);
SA1_ASSERT_OFFSET(PL_FACE, seq, 0x0C);
SA1_ASSERT_OFFSET(PL_FACE, motion, 0x1C);

/*
 * Face block of eventwk (0x5C-0x6B), reached through a pointer to
 * ewp + 0x5C ("ADD #92"), as in code_8C04BC70.c. The PC build uses the
 * flat eventwk fields of the same names.
 */
#if defined(TARGET_PC)
typedef eventwk facewk;
#define FACEWK(ewp) (ewp)
#else
typedef struct facewk {
    Uint8 face;                 /* 0x00 = eventwk 0x5C */
    Uint8 face_next;            /* 0x01 */
    Sint16 face_time;           /* 0x02 */
    Sint16 face_count;          /* 0x04 */
    Uint8 pad06[2];
    const char *face_script;    /* 0x08 = eventwk 0x64 */
    const char *face_seq;       /* 0x0C = eventwk 0x68 */
} facewk;
#define FACEWK(ewp) ((facewk *)&(ewp)->face)
#endif

/* dst[i] = a[i] * (1 - t) + b[i] * t over n points (code_8C055ECE.c) */
extern void func_8C055EFE(Float t, NJS_POINT3 *a, NJS_POINT3 *b, NJS_POINT3 *dst, Sint32 n);
/* advance the event-work face sequence (code_8C04BC70.c) */
extern void func_8C04BCC8(taskwk *twp);

/*
 * Blend every node of the morph object tree between the vertex keys
 * fp->face and fp->face_next of the shape motion (one NJS_MDATA2 per node,
 * *idx = node number in tree order). The sibling call is tail recursion
 * (SHC turns it into a branch).
 */
void func_8C07B080(NJS_OBJECT *obj, PL_FACE *fp, Sint16 *idx)
{
    NJS_MDATA2 *md;
    NJS_POINT3 *pnt;
    NJS_POINT3 *a;
    NJS_POINT3 *b;
    NJS_OBJECT *child;
    NJS_OBJECT *sibling;

    pnt = obj->model->points;
    md = (NJS_MDATA2 *)fp->motion->mdata + *idx;
    if (md->p[0] != NULL) {
        a = md->p[0][fp->face].key;
        b = md->p[0][fp->face_next].key;
        func_8C055EFE(fp->rate, a, b, pnt, obj->model->nbPoint);
    }
    child = obj->child;
    if (child != NULL) {
        (*idx)++;
        func_8C07B080(child, fp, idx);
    }
    sibling = obj->sibling;
    if (sibling != NULL) {
        (*idx)++;
        func_8C07B080(sibling, fp, idx);
    }
}

/* face work dest: put the static face back into the model */
void func_8C07B0F6(task *tp)
{
    PL_FACE *fp;

    fp = (PL_FACE *)tp->mwp;
    if (fp != NULL) {
        fp->parent->sibling = fp->sibling;
    }
}

/*
 * Face work exec. twp->counter.b[0] is the player number. Modes:
 * 0 init (publish fp as playerpwp[pno]->pfp), 1 idle (static face, wait
 * for an event-work face or a sequence), 2 next step of seq, 3 blend
 * towards the step's face over step.time frames, 4 follow the player's
 * event-work face (facewk face/face_next/face_count/face_time).
 * obj is loaded into a local before "idx = 0" (and, in mode 4, before the
 * rate store): this ordering is what makes SHC give the hoisted constant
 * 0 R5 and 1 R4 as in the original.
 */
void func_8C07B106(task *tp)
{
    taskwk *twp;
    PL_FACE *fp;
    taskwk *ptwp;
    facewk *fwp;
    NJS_OBJECT *obj;
    Sint16 idx;

    twp = tp->twp;
    ptwp = playertwp[(Uint8)twp->counter.b[0]];
    fp = (PL_FACE *)tp->mwp;
    if (ptwp == NULL) {
        FreeTask(tp);
        return;
    }
    fwp = FACEWK((eventwk *)ptwp->ewp);
    func_8C04BCC8(ptwp);
    switch (twp->mode) {
    case 0:
        fp->step = 0;
        playerpwp[(Uint8)twp->counter.b[0]]->pfp = fp;
        if (fp->seq == NULL) {
            fp->parent->sibling = fp->sibling;
            fp->rate = 0.0f;
            fp->rate_spd = 0.0f;
            twp->mode = 1;
            break;
        }
        twp->mode = 2;
        /* fall through */
    case 2:
        fp->face = fp->face_next;
        if (fp->seq == NULL || fp->seq[fp->step].time == 0) {
            fp->parent->sibling = fp->sibling;
            fp->rate = 0.0f;
            fp->rate_spd = 0.0f;
            fp->face_next = 0;
            fp->step = 0;
            fp->seq = NULL;
            twp->mode = 1;
            break;
        }
        fp->face_next = fp->seq[fp->step].face;
        fp->rate = 0.0f;
        fp->rate_spd = 1.0f / (Float)fp->seq[fp->step].time;
        twp->mode = 3;
        /* fall through */
    case 3:
        if (fp->seq == NULL) {
            twp->mode = 2;
            break;
        }
        fp->rate += fp->rate_spd;
        if (fp->rate >= 1.0f) {
            fp->rate = 1.0f;
        }
        obj = fp->object;
        idx = 0;
        func_8C07B080(obj, fp, &idx);
        if (fp->rate >= 1.0f) {
            twp->mode = 2;
            fp->step++;
        }
        fp->parent->sibling = fp->object;
        break;
    case 1:
        if (fwp->face_time != 0) {
            twp->mode = 4;
        } else {
            if (fp->seq != NULL) {
                twp->mode = 2;
            } else {
                fp->rate = 0.0f;
                fp->rate_spd = 0.0f;
                fp->parent->sibling = fp->sibling;
            }
            break;
        }
        /* fall through */
    case 4:
        if (fwp->face_time == 0) {
            twp->mode = 1;
            break;
        }
        fp->face = fwp->face;
        fp->face_next = fwp->face_next;
        obj = fp->object;
        fp->rate = (Float)fwp->face_count / (Float)fwp->face_time;
        idx = 0;
        func_8C07B080(obj, fp, &idx);
        fp->parent->sibling = fp->object;
        break;
    }
}
