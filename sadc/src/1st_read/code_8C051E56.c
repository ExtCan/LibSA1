/*
 * Landtable drawing (1ST_READ 8C0519C0 chunk), called by the landtable
 * manager task (8C0519C0) and its display (8C051E00):
 *   8C051E56  draw the visible landtable entries (list B_8C75A30C, count
 *             B_8C75A308, filled by 8C0520C8)
 *   8C051F64  draw the animated landtable entries (_OBJ_LANDTABLE.pMotLandEntry)
 *   8C052048  advance the frames of the animated landtable entries
 */
#include "common.h"
#include "ninja.h"
#include "task.h"
#include "sa1/ninja_funcs.h"
#include "sa1/engine.h"

/*
 * Landtable entry (SADX _OBJ_LANDENTRY / COL), 0x24 bytes; same layout as the
 * file-local copy in code_8C0522C0.c. File-local until the shared headers
 * carry it.
 */
typedef struct _OBJ_LANDENTRY {
    Float xCenter;              /* 0x00 */
    Float yCenter;              /* 0x04 */
    Float zCenter;              /* 0x08 */
    Float xWidth;               /* 0x0C bounding radius */
    Float yWidth;               /* 0x10 */
    Float zWidth;               /* 0x14 */
    NJS_OBJECT *pObject;        /* 0x18 */
    Sint32 blockbit;            /* 0x1C */
    Uint32 slAttribute;         /* 0x20 */
} _OBJ_LANDENTRY;               /* 0x24 */

/*
 * Animated landtable entry (SADX _OBJ_MOTLANDENTRY / GeoAnimData), 0x18 bytes
 * (array walked with ADD #24 by 8C051F64/8C052048). +0x10 is an NJS_ACTION *
 * unless _OBJ_LANDTABLE.ssAttr has 0x10, then an NJS_MOTION * for pObject.
 */
typedef struct _OBJ_MOTLANDENTRY {
    Float fFrame;               /* 0x00 */
    Float fStep;                /* 0x04 */
    Float fMaxFrame;            /* 0x08 */
    NJS_OBJECT *pObject;        /* 0x0C */
    union {
        NJS_ACTION *pAction;    /* ssAttr & 0x10 clear: func_8C040942 */
        NJS_MOTION *pMotion;    /* ssAttr & 0x10 set: func_8C0409C0 with pObject */
    } u;                        /* 0x10 */
    NJS_TEXLIST *pTexList;      /* 0x14 */
} _OBJ_MOTLANDENTRY;            /* 0x18 */

/* Landtable (SADX _OBJ_LANDTABLE, SA1 layout: 16-bit attribute). Size not
 * known from this code. */
typedef struct _OBJ_LANDTABLE {
    Sint16 ssCount;             /* 0x00 */
    Sint16 ssMotCount;          /* 0x02 */
    Sint16 ssAttr;              /* 0x04 1: has pMotLandEntry; 0x10: pMotLandEntry entries are object + motion; 0x0A (either bit): set pTexList before drawing the entries */
    Sint16 ssLoadFlag;          /* 0x06 */
    Float fFarClipping;         /* 0x08 */
    _OBJ_LANDENTRY *pLandEntry; /* 0x0C */
    _OBJ_MOTLANDENTRY *pMotLandEntry; /* 0x10 */
    Sint8 *pPvmFileName;        /* 0x14 */
    NJS_TEXLIST *pTexList;      /* 0x18 */
} _OBJ_LANDTABLE;

SA1_ASSERT_SIZE(_OBJ_LANDENTRY, 0x24);
SA1_ASSERT_OFFSET(_OBJ_LANDENTRY, pObject, 0x18);
SA1_ASSERT_SIZE(_OBJ_MOTLANDENTRY, 0x18);
SA1_ASSERT_OFFSET(_OBJ_MOTLANDENTRY, pObject, 0x0C);
SA1_ASSERT_OFFSET(_OBJ_MOTLANDENTRY, u, 0x10);
SA1_ASSERT_OFFSET(_OBJ_MOTLANDENTRY, pTexList, 0x14);
SA1_ASSERT_OFFSET(_OBJ_LANDTABLE, ssAttr, 0x04);
SA1_ASSERT_OFFSET(_OBJ_LANDTABLE, pMotLandEntry, 0x10);
SA1_ASSERT_OFFSET(_OBJ_LANDTABLE, pTexList, 0x18);

extern _OBJ_LANDTABLE *B_8C7596F4;      /* current landtable */
extern Sint16 B_8C75A308;               /* number of entries in B_8C75A30C */
extern _OBJ_LANDENTRY *B_8C75A30C[];    /* visible landtable entries (max 1024, 8C0520C8) */

extern void func_8C038DA0(void);        /* landtable draw setup */
extern void func_8C038DFC(void);        /* landtable draw end (8C036FFC, 8C605E0A) */
/* hand-written (8C02E7E4): FSCHG loads of the matrix at B_8C88F5F8 - 0x40
 * (the one below the top of the Ninja stack) into XMTRX; returns 1, ignored */
extern void func_8C02E7E4(void);
extern void func_8C037108(NJS_MODEL *model); /* hand-written basic model draw (like 8C037098) */

/* NJS_OBJECT.model value that marks a landtable object without a drawable
 * model (built as 0x200 - 0x6B at 8C051E76). PC: the data ripper must keep
 * this sentinel as is. */
#define LAND_NO_MODEL ((NJS_MODEL *)0x195)

/* MATCHING: the model test must assign inside the condition (a separate
 * "model = obj->model;" statement makes SHC hoist func_8C037108 instead of
 * func_8C02E7E4 into a callee-saved register). */
void func_8C051E56(void)
{
    _OBJ_LANDENTRY *ent;
    NJS_OBJECT *obj;
    NJS_MODEL *model;
    Sint16 i;

    func_8C038DA0();
    njPushMatrix(NULL);
    if (B_8C7596F4->ssAttr & 10) {
        njSetTexture(B_8C7596F4->pTexList);
        for (i = B_8C75A308 - 1; i >= 0; i--) {
            ent = B_8C75A30C[i];
            obj = ent->pObject;
            njTranslate(NULL, obj->pos[0], obj->pos[1], obj->pos[2]);
            njRotateXYZ(NULL, obj->ang[0], obj->ang[1], obj->ang[2]);
            if ((model = obj->model) != LAND_NO_MODEL) {
                func_8C037108(model);
            }
            func_8C02E7E4();
        }
    } else {
        for (i = B_8C75A308 - 1; i >= 0; i--) {
            ent = B_8C75A30C[i];
            obj = ent->pObject;
            njTranslate(NULL, obj->pos[0], obj->pos[1], obj->pos[2]);
            njRotateXYZ(NULL, obj->ang[0], obj->ang[1], obj->ang[2]);
            if ((model = obj->model) != LAND_NO_MODEL) {
                func_8C037108(model);
            }
            func_8C02E7E4();
        }
    }
    njPopMatrix(1);
    func_8C038DFC();
}

void func_8C051F64(void)
{
    _OBJ_MOTLANDENTRY *ent;
    Sint32 n;
    Sint32 attr;
    Float frame;

    attr = B_8C7596F4->ssAttr;
    if (!(attr & 1)) {
        return;
    }
    ent = B_8C7596F4->pMotLandEntry;
    n = B_8C7596F4->ssMotCount;
    if (attr & 0x10) {
        for (; n > 0; n--, ent++) {
            frame = ent->fFrame;
            njSetTexture(ent->pTexList);
            func_8C0409C0(ent->pObject, ent->u.pMotion, frame);
        }
    } else {
        for (; n > 0; n--, ent++) {
            frame = ent->fFrame;
            njSetTexture(ent->pTexList);
            func_8C040942(ent->u.pAction, frame);
        }
    }
}

/* MATCHING: the step local and the early returns are needed (register
 * choice of the frame value, RTS at the end of the first loop). */
void func_8C052048(void)
{
    _OBJ_MOTLANDENTRY *ent;
    Sint32 n;
    Sint32 attr;
    Float frame;
    Float step;

    attr = B_8C7596F4->ssAttr;
    if (!(attr & 1)) {
        return;
    }
    ent = B_8C7596F4->pMotLandEntry;
    n = B_8C7596F4->ssMotCount;
    if (attr & 0x10) {
        for (; n > 0; n--, ent++) {
            frame = ent->fFrame;
            step = ent->fStep;
            frame += step;
            if (frame >= ent->fMaxFrame - step - 1.0f) {
                frame = 0.0f;
            }
            ent->fFrame = frame;
        }
        return;
    }
    for (; n > 0; n--, ent++) {
        frame = ent->fFrame;
        step = ent->fStep;
        frame += step;
        if (frame >= ent->fMaxFrame - step - 1.0f) {
            frame = 0.0f;
        }
        ent->fFrame = frame;
    }
}
