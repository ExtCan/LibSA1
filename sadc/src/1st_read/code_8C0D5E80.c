#include "sa1/sa1.h"

/*
 * E-102 Gamma time bonus number (1ST_READ chunk 8C0D5E80):
 *
 *  func_8C0D5E80  exec+display of the floating "+N" time bonus created by
 *                 func_8C0D6054 (CreateElementalTask(2, ...); pos copied
 *                 from the caller's position, value.w[0] = N). Mode 0
 *                 clears counter.f; mode 1 rises 0.3 per frame and runs
 *                 counter.f 0 -> 1 in 120 frames, then the task is
 *                 destroyed. Drawn (skipped while B_8C754EC8 != 0) as
 *                 camera-facing sprites of texlist_E102TIME: cell 11 ("+")
 *                 10 units towards the camera, then the decimal digits of
 *                 N (1 to 3 of them) from the last one, each 8 units to
 *                 the left.
 */

extern NJS_ARGB D_8C4232A0;             /* constant material of the number */
extern NJS_SPRITE D_8C423260;           /* E102TIME sprite (cells 0-9 digits, 11 sign) */
extern NJS_TEXLIST texlist_E102TIME;

void func_8C0D5E80(task *tp)
{
    taskwk *twp;
    Sint32 n;
    Sint32 digits;

    twp = tp->twp;
    switch (twp->mode) {
    case 0:
        twp->counter.f = 0.0f;
        twp->mode = 1;
        break;
    case 1:
        twp->pos.y += 0.29999999f;
        twp->counter.f += 0.00833333307f;
        if (twp->counter.f >= 1.0f) {
            DestroyTask(tp);
            return;
        }
        break;
    }
    if (B_8C754EC8 != 0) {
        return;
    }
    n = twp->value.w[0];
    if (n > 99) {
        digits = 3;
    } else if (n > 9) {
        digits = 2;
    } else {
        digits = 1;
    }
    njSetConstantMaterial(&D_8C4232A0);
    njColorBlendingMode(0, 8);
    njColorBlendingMode(1, 6);
    njSetTexture(&texlist_E102TIME);
    njPushMatrix(NULL);
    njTranslateV(NULL, &twp->pos);
    camera_twp->ang.y ? njRotateY(NULL, camera_twp->ang.y) : (void)0;
    camera_twp->ang.x + 0x8000 ? njRotateX(NULL, camera_twp->ang.x + 0x8000) : (void)0;
    njTranslate(NULL, 0.0f, 0.0f, -10.0f);
    njDrawSprite3D(&D_8C423260, 11, 34);
    while (digits > 0) {
        njTranslate(NULL, -8.0f, 0.0f, 0.0f);
        njDrawSprite3D(&D_8C423260, n % 10, 34);
        digits--;
        n /= 10;
    }
    njPopMatrix(1);
}
