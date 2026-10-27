/*
 * 1ST_READ 8C036BC0-8C036F26 (chunk asm/1st_read/text/8C036BC0.src):
 * basic NJS_OBJECT tree draw.
 *
 *  func_8C036BC0  draws an object, its children and its siblings: per node,
 *                 unless all three NJD_EVAL_UNIT_* flags are set, push a
 *                 matrix, apply the non-unit translate / rotate (ZXY when
 *                 NJD_EVAL_ZXY_ANG) / scale, draw the model with 8C037098
 *                 unless NJD_EVAL_HIDE, recurse into the child unless
 *                 NJD_EVAL_BREAK, pop the matrix; then the next sibling.
 *  func_8C036F12  wrapper: save draw state (8C605DAC), draw the tree,
 *                 restore (8C605E0A).
 *
 * func_8C036F12 takes an NJS_OBJECT * (sa1/engine.h declares it with
 * NJS_MODEL *, so that header is not included here).
 */

#include "common.h"
#include "ninja.h"
#include "sa1/ninja_funcs.h"

void func_8C037098(NJS_MODEL *model);   /* hand-written basic model draw */

void func_8C036BC0(NJS_OBJECT *obj)
{
    do {
        switch (obj->evalflags & 7) {
        case 7:
            if (!(obj->evalflags & NJD_EVAL_HIDE)) {
                func_8C037098(obj->model);
            }
            if (!(obj->evalflags & NJD_EVAL_BREAK)) {
                func_8C036BC0(obj->child);
            }
            break;
        case 0:
            njPushMatrix(NULL);
            njTranslate(NULL, obj->pos[0], obj->pos[1], obj->pos[2]);
            if (obj->evalflags & NJD_EVAL_ZXY_ANG) {
                njRotateZXY(NULL, obj->ang[0], obj->ang[1], obj->ang[2]);
            } else {
                njRotateXYZ(NULL, obj->ang[0], obj->ang[1], obj->ang[2]);
            }
            njScale(NULL, obj->scl[0], obj->scl[1], obj->scl[2]);
            if (!(obj->evalflags & NJD_EVAL_HIDE)) {
                func_8C037098(obj->model);
            }
            if (!(obj->evalflags & NJD_EVAL_BREAK)) {
                func_8C036BC0(obj->child);
            }
            njPopMatrix(1);
            break;
        case 1:
            njPushMatrix(NULL);
            if (obj->evalflags & NJD_EVAL_ZXY_ANG) {
                njRotateZXY(NULL, obj->ang[0], obj->ang[1], obj->ang[2]);
            } else {
                njRotateXYZ(NULL, obj->ang[0], obj->ang[1], obj->ang[2]);
            }
            njScale(NULL, obj->scl[0], obj->scl[1], obj->scl[2]);
            if (!(obj->evalflags & NJD_EVAL_HIDE)) {
                func_8C037098(obj->model);
            }
            if (!(obj->evalflags & NJD_EVAL_BREAK)) {
                func_8C036BC0(obj->child);
            }
            njPopMatrix(1);
            break;
        case 2:
            njPushMatrix(NULL);
            njTranslate(NULL, obj->pos[0], obj->pos[1], obj->pos[2]);
            njScale(NULL, obj->scl[0], obj->scl[1], obj->scl[2]);
            if (!(obj->evalflags & NJD_EVAL_HIDE)) {
                func_8C037098(obj->model);
            }
            if (!(obj->evalflags & NJD_EVAL_BREAK)) {
                func_8C036BC0(obj->child);
            }
            njPopMatrix(1);
            break;
        case 3:
            njPushMatrix(NULL);
            njScale(NULL, obj->scl[0], obj->scl[1], obj->scl[2]);
            if (!(obj->evalflags & NJD_EVAL_HIDE)) {
                func_8C037098(obj->model);
            }
            if (!(obj->evalflags & NJD_EVAL_BREAK)) {
                func_8C036BC0(obj->child);
            }
            njPopMatrix(1);
            break;
        case 4:
            njPushMatrix(NULL);
            njTranslate(NULL, obj->pos[0], obj->pos[1], obj->pos[2]);
            if (obj->evalflags & NJD_EVAL_ZXY_ANG) {
                njRotateZXY(NULL, obj->ang[0], obj->ang[1], obj->ang[2]);
            } else {
                njRotateXYZ(NULL, obj->ang[0], obj->ang[1], obj->ang[2]);
            }
            if (!(obj->evalflags & NJD_EVAL_HIDE)) {
                func_8C037098(obj->model);
            }
            if (!(obj->evalflags & NJD_EVAL_BREAK)) {
                func_8C036BC0(obj->child);
            }
            njPopMatrix(1);
            break;
        case 5:
            njPushMatrix(NULL);
            if (obj->evalflags & NJD_EVAL_ZXY_ANG) {
                njRotateZXY(NULL, obj->ang[0], obj->ang[1], obj->ang[2]);
            } else {
                njRotateXYZ(NULL, obj->ang[0], obj->ang[1], obj->ang[2]);
            }
            if (!(obj->evalflags & NJD_EVAL_HIDE)) {
                func_8C037098(obj->model);
            }
            if (!(obj->evalflags & NJD_EVAL_BREAK)) {
                func_8C036BC0(obj->child);
            }
            njPopMatrix(1);
            break;
        case 6:
            njPushMatrix(NULL);
            njTranslate(NULL, obj->pos[0], obj->pos[1], obj->pos[2]);
            if (!(obj->evalflags & NJD_EVAL_HIDE)) {
                func_8C037098(obj->model);
            }
            if (!(obj->evalflags & NJD_EVAL_BREAK)) {
                func_8C036BC0(obj->child);
            }
            njPopMatrix(1);
            break;
        }
        obj = obj->sibling;
    } while (obj != NULL);
}

void func_8C036F12(NJS_OBJECT *obj)
{
    func_8C605DAC();
    func_8C036BC0(obj);
    func_8C605E0A();
}
