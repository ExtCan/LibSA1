#include "common.h"
#include "ninja.h"
#include "sa1/sa1.h"

/*
 * 1ST_READ 8C0B2170-8C0B2338 (chunk 8C0B11C0-8C0B2338, function 17 of 17):
 * Sonic's model morph for action 13. Called from the destructor
 * (func_8C0AE0E0, with reqaction cleared) and the display routine
 * (func_8C0AE17A) before the body is drawn.
 *
 * While pwp->mj.reqaction == 13 two parts of Sonic's model are morphed
 * between a rest shape and a target shape as functions of the motion frame
 * (pwp->mj.nframe), with the blend factors kept in pwp->free.f[6] / f[7]:
 *   part A: frames 0-16 blend 1 -> 0, 16-24 rest, 24-32 blend 0 -> 1
 *   part B: frames 8-16 blend 0 -> 1, 16-32 blend 1 -> 0, rest otherwise
 * Each part has a model set {morphed model, model a, model b, original
 * model}; the morph writes into [0] (func_8C055ECE) and the part's
 * NJS_OBJECT then points at [0], or at [1] when no blending is needed. For any
 * other action both objects get their original model [3] back.
 */

extern void func_8C055ECE(NJS_MODEL *a, NJS_MODEL *b, NJS_MODEL *dst, Float t);   /* morph (code_8C055ECE.c) */

extern NJS_MODEL *D_8C1C5EE8[4];   /* part A models {dst, a, b, original} (8C1D66A8, 8C1D6E40, 8C1D6A6C, 8C1C74DC) */
extern NJS_MODEL *D_8C1C5EF8[4];   /* part B models {dst, a, b, original} (8C1D7204, 8C1D79A8, 8C1D75C8, 8C1C842C) */
extern NJS_OBJECT D_8C1C7504;      /* part A object of Sonic's tree (model 8C1C74DC) */
extern NJS_OBJECT D_8C1C8454;      /* part B object of Sonic's tree (model 8C1C842C) */

void func_8C0B2170(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    if (pwp->mj.reqaction == 13) {
        if (pwp->mj.nframe < 16.0f) {
            pwp->free.f[6] = (16.0f - pwp->mj.nframe) * 0.0625f;
            func_8C055ECE(D_8C1C5EE8[1], D_8C1C5EE8[2], D_8C1C5EE8[0], pwp->free.f[6]);
            D_8C1C7504.model = D_8C1C5EE8[0];
        } else if (pwp->mj.nframe > 24.0f) {
            pwp->free.f[6] = (pwp->mj.nframe - 24.0f) * 0.125f;
            func_8C055ECE(D_8C1C5EE8[1], D_8C1C5EE8[2], D_8C1C5EE8[0], pwp->free.f[6]);
            D_8C1C7504.model = D_8C1C5EE8[0];
        } else {
            pwp->free.f[6] = 0.0f;
            D_8C1C7504.model = D_8C1C5EE8[1];
        }
        if (pwp->mj.nframe >= 8.0f && pwp->mj.nframe <= 16.0f) {
            pwp->free.f[7] = (pwp->mj.nframe - 8.0f) * 0.125f;
            func_8C055ECE(D_8C1C5EF8[1], D_8C1C5EF8[2], D_8C1C5EF8[0], pwp->free.f[7]);
            D_8C1C8454.model = D_8C1C5EF8[0];
        } else if (pwp->mj.nframe > 16.0f && pwp->mj.nframe <= 32.0f) {
            pwp->free.f[7] = (32.0f - pwp->mj.nframe) * 0.0625f;
            func_8C055ECE(D_8C1C5EF8[1], D_8C1C5EF8[2], D_8C1C5EF8[0], pwp->free.f[7]);
            D_8C1C8454.model = D_8C1C5EF8[0];
        } else {
            pwp->free.f[7] = 0.0f;
            D_8C1C8454.model = D_8C1C5EF8[1];
        }
    } else {
        pwp->free.f[6] = 0.0f;
        pwp->free.f[7] = 0.0f;
        D_8C1C7504.model = D_8C1C5EE8[3];
        D_8C1C8454.model = D_8C1C5EF8[3];
    }
}
