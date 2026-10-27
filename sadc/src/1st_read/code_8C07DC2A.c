#include "common.h"
#include "ninja.h"
#include "sa1/sa1.h"

/*
 * Held-object push-out (chunk 8C07D6C0, function 4/4 8C07DC2A).
 * Called by the character mains (8C0B199A, 8C0BD098, 8C0C6A7A, 8C0DC3C0)
 * as (player twp, pwp->htp->twp, pwp): when the held object is closer to
 * the player than the player radius plus the object's second collision
 * shape size, it is moved out horizontally (y unchanged) to that distance.
 */

void func_8C07DC2A(taskwk *twp, taskwk *htwp, playerwk *pwp) {
    NJS_VECTOR v;
    Float r;
    colliwk *cwp;

    cwp = CWP(htwp);
    if (cwp == NULL) {
        return;
    }
    r = pwp->p.rad + cwp->info[1].a;
    if (r > njDistanceP2P(&twp->pos, &htwp->pos)) {
        v = htwp->pos;
        njSubVector(&v, &twp->pos);
        v.y = 0.0f;
        njUnitVector(&v);
        htwp->pos.x = SA1_FMAC(r, v.x, twp->pos.x);
        htwp->pos.z = SA1_FMAC(r, v.z, twp->pos.z);
    }
}
