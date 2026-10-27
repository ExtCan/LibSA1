/*
 * STG01 0C909F20: Emerald Coast ocean task (chunk 0C909F20-0C90A820,
 * functions 1-4 of 8).
 *
 *  func_0C909F20  init: flattens the morph-effect ocean mesh, allocates a
 *                 pooled NJS_OBJECT for the water collision and registers it
 *                 (func_8C0528AE), installs exec/disp/dest.
 *  func_0C909FDA  exec: frees itself outside act 0; follows player 0 in
 *                 steps of 25 units (x/z), clears and re-runs the wave list
 *                 D_0CAB1B04 (func_0C90A3EE per wave), derives the morph
 *                 normals from the vertex heights, drops dead waves and
 *                 animates the ocean texture every 3 frames.
 *  func_0C90A20C  disp: draws the morph effect (when waves exist) and the
 *                 primary ocean mesh at twp->pos.
 *  func_0C90A260  dest: unregisters and frees the collision object.
 *
 * All four functions match. Code generation notes:
 *  - the 676-vertex loops are "for (i...) { p->y = ...; p++; }": SHC turns
 *    them into a count-down loop unrolled 2x (ADD #-2 / TST); p[i] gives an
 *    end-pointer compare instead.
 *  - exec: "0.04f * (px - x)" (constant first) makes SHC keep 0.04f in FR12
 *    and spill pz to the stack slot at @R15 (16-byte frame); "(px - x) *
 *    0.04f" or a named constant swaps FR12/FR13 and FR4/FR5. The wave list
 *    is walked with a pointer (w++) in the call loop (R12) and copied with
 *    six explicit "*d++ = *s++" float moves (a struct assignment gives
 *    MOV.L pairs, a k-loop is not unrolled); "last--" must come before
 *    "i--" or SHC hoists i-- into a BT/S delay slot.
 *  - disp: the draw function lives in R14 only when it is held in a local
 *    (assigned after njTranslateV).
 *  - SA1_FMAC count: asm 4 FMACs = 2 SA1_FMAC_ACC + the one SA1_FMAC of the
 *    normals loop, which SHC unrolls 2x.
 */
#include "common.h"
#include "sa1/sa1.h"

/* Wave entry of the list D_0CAB1B04 (stride 0x18: func_0C909FDA ADD #24;
 * written by func_0C90A282, read by func_0C90A3EE). */
typedef struct {
    Float x;                    /* 0x00 */
    Float y;                    /* 0x04 */
    Float z;                    /* 0x08 */
    Float spdx;                 /* 0x0C func_0C90A282: FR6 * 436.9 */
    Float spdz;                 /* 0x10 func_0C90A282: FR7 * 436.9 */
    Float life;                 /* 0x14 200.0f when added; dropped when <= 0 */
} OCEAN_WAVE;                   /* 0x18 */

/* STG01 data (extern only). The STG01_Ocean_* and STG01_Water_Collision
 * labels are NJS_OBJECTs (evalflags 0x17, model at +4), not basic models. */
extern NJS_OBJECT STG01_Ocean_Morph_Effect;
extern NJS_OBJECT STG01_Ocean_Primary;
extern NJS_OBJECT STG01_Water_Collision;
extern NJS_TEXLIST D_0CAA14DC;              /* ocean texlist, 10 textures */
extern Float D_0CAB1AF4;                    /* ocean x */
extern Float D_0CAB1AF8;                    /* ocean z */
extern Sint32 D_0CAB1AFC;
extern Sint32 D_0CAB1B00;
extern OCEAN_WAVE D_0CAB1B04[];

/* 1ST_READ functions not declared in the sa1 headers */
NJS_OBJECT *func_8C052A1E(void);                        /* allocate a pooled NJS_OBJECT (code_8C052A00.c) */
void func_8C052A5E(NJS_OBJECT *obj);                    /* free it */
void func_8C0528AE(Uint32 attr, task *tp, NJS_OBJECT *obj); /* register a collision object (code_8C052518.c) */
void func_8C052940(task *tp, NJS_OBJECT *obj);          /* unregister it */
Float func_8C63A994(Float x);                           /* SDK: x < 0 ? -floorf(-x) : floorf(x) (truncate) */

void func_0C909FDA(task *tp);
void func_0C90A20C(task *tp);
void func_0C90A260(task *tp);
void func_0C90A3EE(OCEAN_WAVE *wave);

void func_0C909F20(task *tp)
{
    taskwk *twp;
    NJS_OBJECT *obj;
    NJS_POINT3 *p;
    Sint32 i;

    twp = tp->twp;
    twp->pos.y = -1.5f;
    twp->ang.x = 0;
    twp->ang.y = 0;
    twp->ang.z = 0;
    twp->wtimer = 0;
    twp->btimer = 0;
    twp->counter.l = 0;
    twp->scl.x = 0.0f;
    twp->value.f = 0.0f;
    p = STG01_Ocean_Morph_Effect.model->points;
    for (i = 0; i < 676; i++) {
        p->y = 0.0f;
        p++;
    }
    D_0CAB1AFC = 0;
    obj = func_8C052A1E();
    twp->timer.ptr = obj;
    obj->evalflags = NJD_EVAL_BREAK;
    obj->scl[0] = 1.0f;
    obj->scl[1] = 1.0f;
    obj->scl[2] = 1.0f;
    obj->ang[0] = 0;
    obj->ang[1] = twp->ang.y;
    obj->ang[2] = 0;
    obj->pos[0] = twp->pos.x;
    obj->pos[1] = twp->pos.y;
    obj->pos[2] = twp->pos.z;
    obj->model = STG01_Water_Collision.model;
    obj->child = NULL;
    obj->sibling = NULL;
    func_8C0528AE(0x88000002, tp, obj);
    twp->flag |= 0x100;
    tp->exec = func_0C909FDA;
    tp->disp = func_0C90A20C;
    tp->dest = func_0C90A260;
}

void func_0C909FDA(task *tp)
{
    taskwk *twp;
    NJS_OBJECT *obj;
    NJS_POINT3 *p;
    NJS_VECTOR *v;
    OCEAN_WAVE *w;
    OCEAN_WAVE *last;
    Float x;
    Float z;
    Float px;
    Float pz;
    Sint32 i;
    Sint32 n;
    Float *s;
    Float *d;
    NJS_POINT3 pos;

    twp = tp->twp;
    if ((Uint8)GetStageNumber()) {
        FreeTask(tp);
    }
    GetPlayerPosition(0, 0, &pos, NULL);
    x = twp->pos.x;
    z = twp->pos.z;
    px = pos.x;
    pz = pos.z;
    px = func_8C63A994(0.04f * (px - x));
    pz = func_8C63A994(0.04f * (pz - z));
    SA1_FMAC_ACC(x, 25.0f, px);
    SA1_FMAC_ACC(z, 25.0f, pz);
    twp->pos.x = x;
    twp->pos.z = z;
    D_0CAB1AF4 = x;
    D_0CAB1AF8 = z;
    obj = twp->timer.ptr;
    obj->pos[0] = x;
    obj->pos[2] = z;
    p = STG01_Ocean_Morph_Effect.model->points;
    for (i = 0; i < 676; i++) {
        p->y = 0.0f;
        p++;
    }
    D_0CAB1B00 = 0;
    n = twp->counter.l;
    w = D_0CAB1B04;
    for (i = 0; i < n; i++) {
        func_0C90A3EE(w);
        w++;
    }
    p = STG01_Ocean_Morph_Effect.model->points;
    v = STG01_Ocean_Morph_Effect.model->normals;
    for (i = 0; i < 676; i++) {
        v->y = SA1_FMAC(0.19999999f, p->y, 1.0f);
        p++;
        v++;
    }
    n = twp->counter.l;
    w = D_0CAB1B04;
    last = &D_0CAB1B04[n - 1];
    for (i = 0; i < n; i++) {
        if (w->life <= 0.0f) {
            if (i != n) {
                s = &last->x;
                d = &w->x;
                *d++ = *s++;
                *d++ = *s++;
                *d++ = *s++;
                *d++ = *s++;
                *d++ = *s++;
                *d = *s;
            }
            last--;
            i--;
            n--;
        } else {
            w++;
        }
    }
    twp->counter.l = n;
    twp->wtimer++;
    if (twp->wtimer > 2) {
        twp->wtimer = 0;
        STG01_Ocean_Morph_Effect.model->mats->attr_texId = twp->btimer;
        STG01_Ocean_Primary.model->mats->attr_texId = twp->btimer;
        twp->btimer++;
        twp->btimer %= 10;
    }
    func_0C90A20C(tp);
}

void func_0C90A20C(task *tp)
{
    taskwk *twp;
    void (*draw)(NJS_MODEL *model);

    twp = tp->twp;
    njSetTexture(&D_0CAA14DC);
    njPushMatrix(NULL);
    njTranslateV(NULL, &twp->pos);
    draw = func_8C036F12;
    if (twp->counter.l) {
        /* func_8C036F12 takes an NJS_OBJECT (header says NJS_MODEL) */
        draw((NJS_MODEL *)&STG01_Ocean_Morph_Effect);
        njTranslate(NULL, 2476.0f, 0.0f, 0.0f);
    }
    draw((NJS_MODEL *)&STG01_Ocean_Primary);
    njPopMatrix(1);
}

void func_0C90A260(task *tp)
{
    taskwk *twp;
    NJS_OBJECT *obj;

    twp = tp->twp;
    obj = twp->timer.ptr;
    if (obj) {
        func_8C052940(tp, obj);
        func_8C052A5E(obj);
    }
}

ASM_STUB(void func_0C90A3EE(OCEAN_WAVE *wave))
