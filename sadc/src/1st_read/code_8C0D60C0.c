#include "sa1/sa1.h"

/*
 * E-102 HUD helper (called from E102Main 8C0CBE00): a pulsing sprite shown
 * while the remaining time (func_8C08AB2E: minutes/seconds/frames) is at
 * most 5 seconds; twp->counter.f fades its width in and out.
 *
 *  func_8C0D60C0  draw: sprite D_8C423024 at (320, 415.2) with index
 *                 min * 60 + sec, scaled by 2.2 * (0.9 + 0.4 * |sin|) where
 *                 the angle follows the frame counter; then sprite 13 at
 *                 (320, 380).
 *  func_8C0D6198  state machine on twp->mode (0 init, 1 wait for <= 5 s and
 *                 play SE 0x4AB, 2 fade in, 3 wait for > 5 s, 4 fade out).
 */

extern NJS_SPRITE D_8C423024;
void func_8C08AB2E(Sint8 *min, Sint8 *sec, Sint8 *fr);   /* code_8C08A71E.c */

void func_8C0D60C0(task *tp) {
    Sint32 t;
    Sint8 fr;
    Sint8 sec;
    Sint8 min;
    taskwk *twp;
    Float s;
    Angle ang;

    twp = tp->twp;
    if (B_8C754EC8 == 0) {
        func_8C08AB2E(&min, &sec, &fr);
        t = (Uint8)min * 60 + (Uint8)sec;
        njColorBlendingMode(0, 8);
        njColorBlendingMode(1, 6);
        D_8C423024.p.x = 320.0f;
        D_8C423024.p.y = 415.19999f;
        ang = (Uint8)fr * 0x8000 / 30;
        s = SA1_FMAC(0.39999998f, _builtin_fabsf(njSin(ang)), 0.9f) * 2.1999999f;
        D_8C423024.sx = s * twp->counter.f;
        D_8C423024.sy = s;
        njDrawSprite2D(&D_8C423024, t, -2.0f, 32);
        D_8C423024.p.x = 320.0f;
        D_8C423024.p.y = 380.0f;
        D_8C423024.sx = twp->counter.f * 2.1999999f;
        D_8C423024.sy = 2.1999999f;
        njDrawSprite2D(&D_8C423024, 13, -3.0f, 32);
    }
}

void func_8C0D6198(task *tp) {
    Sint8 fr;
    Sint8 sec;
    Sint8 min;
    taskwk *twp;
    Sint32 t;

    twp = tp->twp;
    func_8C08AB2E(&min, &sec, &fr);
    t = (Uint8)min * 60 + (Uint8)sec;
    switch (twp->mode) {
    case 0:
        twp->counter.f = 0.0f;
        twp->mode = 1;
        /* fallthrough */
    case 1:
        if (t > 5) {
            return;
        }
        twp->mode = 2;
        func_8C0940DA(0x4AB, 0, 0, 0);
        return;
        break; /* unreachable; needed for the original register allocation */
    case 2:
        twp->counter.f += 0.125f;
        if (twp->counter.f >= 1.0f) {
            twp->counter.f = 1.0f;
            twp->mode = 3;
        }
        break;
    case 3:
        if (t > 5) {
            twp->mode = 4;
        }
        break;
    case 4:
        twp->counter.f -= 0.125f;
        if (twp->counter.f <= 0.0f) {
            twp->counter.f = 0.0f;
            twp->mode = 1;
            return;
        }
        break;
    }
    func_8C0D60C0(tp);
}
