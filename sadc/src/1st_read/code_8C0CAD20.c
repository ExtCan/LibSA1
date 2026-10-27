/*
 * 1ST_READ 8C0CAD20-8C0CAE0E (chunk 8C0CAD20): Knuckles' debris particle
 * (texlist_KNU_EFF), spawned in batches by func_8C0CAE60 with
 * CreateElementalTask(2, 6, func_8C0CAD20) and disp lbl_8C0CAD98.
 *
 *  func_8C0CAD20  exec: freed after 60 frames; gravity -0.1 per frame on
 *                 scl.y (clamped at -2), pos += scl, ang.x/ang.z spin by
 *                 ang.y per frame; then tail-calls the disp
 *  lbl_8C0CAD98   disp: sprite D_8C353584 at twp->pos rotated by ang.x and
 *                 ang.z (blending modes 8/6)
 *
 * taskwk use: wtimer = age, scl = velocity, ang.y = spin speed,
 * ang.x/ang.z = spin angles.
 *
 * The exec has no RTS: SHC places the tail-called disp right after it and
 * falls through into it (as in code_8C0B2EA0.c), so the original range of
 * func_8C0CAD20 (8C0CAD20-8C0CAE0E, 238 bytes) holds both functions.
 */
#include "common.h"
#include "sa1/sa1.h"

extern NJS_TEXLIST texlist_KNU_EFF;     /* 8C3538DC */
extern NJS_SPRITE D_8C353584;           /* debris sprite (3D) */

void lbl_8C0CAD98(task *tp);

/*
 * NON_MATCHING only for the tools: the code is identical. Compared as one
 * range with lbl_8C0CAD98 (shcdiff with the lbl_8C0CAD98 function label
 * ignored on the compiled side) all 119 of 119 instructions match, and the
 * compiled layout is the original's (lbl_8C0CAD98 at +0x78, literal pool at
 * +0x100). tools/shcdiff.py and tools/splice.py stop the compiled side at the
 * lbl_8C0CAD98 label (60 vs 119 instructions), so the function is reported as
 * DIFF and would fail the splice; it stays guarded until the tools learn
 * about fall-through functions. lbl_8C0CAD98 itself is not a function of the
 * chunk manifest, so it is not spliced and stays outside the guard.
 */
#ifdef NON_MATCHING
void func_8C0CAD20(task *tp)
{
    taskwk *twp;

    twp = tp->twp;
    if (++twp->wtimer >= 60) {
        FreeTask(tp);
    }
    if ((twp->scl.y += -0.099999996f) < -2.0f) {
        twp->scl.y = -2.0f;
    }
    twp->pos.x += twp->scl.x;
    twp->pos.y += twp->scl.y;
    twp->pos.z += twp->scl.z;
    twp->ang.x += twp->ang.y;
    twp->ang.z += twp->ang.y;
    lbl_8C0CAD98(tp);
}
#else
ASM_STUB(void func_8C0CAD20(task *tp))
#endif

void lbl_8C0CAD98(task *tp)
{
    taskwk *twp;

    twp = tp->twp;
    if (B_8C754EC8 != 0) {
        return;
    }
    njColorBlendingMode(0, 8);
    njColorBlendingMode(1, 6);
    njSetTexture(&texlist_KNU_EFF);
    njPushMatrix(NULL);
    njTranslateV(NULL, &twp->pos);
    twp->ang.x ? njRotateX(NULL, twp->ang.x) : (void)0;
    twp->ang.z ? njRotateZ(NULL, twp->ang.z) : (void)0;
    njDrawSprite3D(&D_8C353584, 0, 0x20);
    njPopMatrix(1);
}
