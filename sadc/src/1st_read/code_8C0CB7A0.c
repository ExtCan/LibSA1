#include "common.h"
#include "ninja.h"
#include "sa1/sa1.h"

/*
 * 1ST_READ 8C0CB7A0-8C0CB960 (chunk 8C0CB7A0): Knuckles' Maximum Heat
 * glow (object Knuckles_Maximum_Heat_Effect, texlist_KNU_EFF).
 *
 *  func_8C0CB7A0  charge glow, called every frame by func_8C0C7600 at full
 *                 charge with the player's first collision shape centre and
 *                 a pulse 0..1: material texture 1, alpha -(0.6 * pulse),
 *                 scale 2 - pulse^2, rotated about Y by pulse * 0x40000
 *  func_8C0CB848  exec of the fade task created by func_8C0CB960 /
 *                 func_8C0CB9A4: timer.f += scl.x; frees the task when
 *                 player 1 is gone or timer.f reaches 1.0, else draws
 *  func_8C0CB870  disp of the fade task: material texture 2 at player 1's
 *                 first collision shape centre, alpha lerp(value.f,
 *                 counter.f, timer.f), scale lerp(scl.y, scl.z, timer.f)
 *
 * Knuckles_Maximum_Heat_Effect is an NJS_OBJECT (the code reads
 * obj->model->mats[0] and func_8C036F12 walks the object tree, see
 * code_8C036BC0.c); the shared prototype of func_8C036F12 still takes an
 * NJS_MODEL *, hence the casts. It is declared as an array so that SHC
 * loads the object address and reads model at +4 (a plain object gives a
 * "sym+4" pool entry).
 *
 * Matching notes: the 1.0 arguments are literals (a Float variable swaps
 * FR14/FR15); the alpha values are locals set before the call (inline
 * arguments put the temporaries in callee-saved registers); the disp alpha
 * is "c + a * b" (FMAC_ADD: with SA1_FMAC's "a * b + c" the result is
 * computed in FR2 and copied to FR4); the B_8C754EC8 test of
 * func_8C0CB870 encloses the body (an early return allocates the material
 * store differently).
 */

/* c + a * b at an FMAC site, addend evaluated first; fused like SA1_FMAC on PC */
#if defined(TARGET_PC)
#define FMAC_ADD(c, a, b) SA1_FMAC((a), (b), (c))
#else
#define FMAC_ADD(c, a, b) ((c) + (a) * (b))
#endif

extern NJS_TEXLIST texlist_KNU_EFF;                 /* 8C3538DC */
extern NJS_OBJECT Knuckles_Maximum_Heat_Effect[];   /* 8C38DBA8 */

void func_8C0CB870(task *tp);

void func_8C0CB7A0(NJS_POINT3 *pos, Float scl)
{
    Float a;
    Float s;
    Sint32 ang;

    if (B_8C754EC8 != 0) {
        return;
    }
    Knuckles_Maximum_Heat_Effect->model->mats->attr_texId = 1;
    a = -(scl * 0.59999998f);   /* 0x3F199999 */
    func_8C043A20(a, 1.0f, 1.0f, 1.0f);
    njPushMatrix(NULL);
    njTranslateV(NULL, pos);
    s = 2.0f - scl * scl * 1.0f;
    njScale(NULL, s, s, s);
    (ang = SA1_FTOI(scl * 262144.0f)) ? njRotateY(NULL, ang) : (void)0;
    njSetTexture(&texlist_KNU_EFF);
    func_8C036F12((NJS_MODEL *)Knuckles_Maximum_Heat_Effect);
    njPopMatrix(1);
    func_8C043A20(0.0f, 0.0f, 0.0f, 0.0f);
}

void func_8C0CB848(task *tp)
{
    taskwk *twp;

    twp = tp->twp;
    if (playertwp[0] == NULL || !((twp->timer.f += twp->scl.x) < 1.0f)) {
        FreeTask(tp);
    } else {
        func_8C0CB870(tp);
    }
}

void func_8C0CB870(task *tp)
{
    taskwk *twp;
    taskwk *ptwp;
    Float s;
    Float a;

    twp = tp->twp;
    if ((ptwp = playertwp[0]) == NULL) {
        return;
    }
    if (B_8C754EC8 == 0) {
        Knuckles_Maximum_Heat_Effect->model->mats->attr_texId = 2;
        s = SA1_FMAC(twp->scl.z - twp->scl.y, twp->timer.f, twp->scl.y);
        a = FMAC_ADD(twp->value.f, twp->counter.f - twp->value.f, twp->timer.f);
        func_8C043A20(a, 1.0f, 1.0f, 1.0f);
        njPushMatrix(NULL);
        njTranslateV(NULL, &CWP(ptwp)->info->center);
        njScale(NULL, s, s, s);
        njSetTexture(&texlist_KNU_EFF);
        func_8C036F12((NJS_MODEL *)Knuckles_Maximum_Heat_Effect);
        njPopMatrix(1);
        func_8C043A20(0.0f, 0.0f, 0.0f, 0.0f);
    }
}
