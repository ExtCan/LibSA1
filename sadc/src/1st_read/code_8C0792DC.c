/*
 * Player common (1ST_READ 8C0792DC): Sonic only (called from SonicMain
 * 8C0A77BC).
 *
 *  func_8C0792DC  copies the world speed (mwp->spd) into player space
 *                 (PConvertVector_G2P) as pwp->spd, flushing components
 *                 within +-0.001 to 0 (pwp->spd.y is cleared too while
 *                 twp->flag & 1, on the ground), then keeps the player
 *                 inside a fixed corridor by height:
 *                   y > -18400:          z in [-160, 160], x in [-20, 300]
 *                   -18845 < y <= -18400: limits widen linearly with depth
 *                                        (z half-width 160 + (-18400 - y) * 10/89,
 *                                        x min -20 + (-18400 - y) * 28/89)
 *                   -19000 < y <= -18845: z in [-110, 110], x in [115, 300]
 *                 (a fixed course section; the bounds are absolute world
 *                 coordinates).
 */
#include "sa1/sa1.h"

ASM_STUB(void func_8C078E40(taskwk *twp, NJS_POINT3 *v))

void func_8C0792DC(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_POINT3 spd;
    Float lim;

    spd = mwp->spd;
    PConvertVector_G2P(twp, &spd);
    if (spd.x <= 0.00099999996f && spd.x >= -0.00099999996f) {
        spd.x = 0.0f;
    }
    if (spd.y <= 0.00099999996f && spd.y >= -0.00099999996f) {
        spd.y = 0.0f;
    }
    if (spd.z <= 0.00099999996f && spd.z >= -0.00099999996f) {
        spd.z = 0.0f;
    }
    pwp->spd = spd;
    if (twp->flag & 1) {
        pwp->spd.y = 0.0f;
    }

    if (twp->pos.y > -18400.0f) {
        if (twp->pos.z < -160.0f) {
            twp->pos.z = -160.0f;
        }
        if (twp->pos.z > 160.0f) {
            twp->pos.z = 160.0f;
        }
        if (twp->pos.x < -20.0f) {
            twp->pos.x = -20.0f;
        }
        if (twp->pos.x > 300.0f) {
            twp->pos.x = 300.0f;
        }
    } else if (twp->pos.y > -18845.0f) {
        lim = SA1_FMAC(0.11235954f, -18400.0f - twp->pos.y, 160.0f);
        if (twp->pos.z < -lim) {
            twp->pos.z = -lim;
        }
        if (twp->pos.z > lim) {
            twp->pos.z = lim;
        }
        lim = SA1_FMAC(0.3146067f, -18400.0f - twp->pos.y, -20.0f);
        if (twp->pos.x < lim) {
            twp->pos.x = lim;
        }
        if (twp->pos.x > 300.0f) {
            twp->pos.x = 300.0f;
        }
    } else if (twp->pos.y > -19000.0f) {
        if (twp->pos.z < -110.0f) {
            twp->pos.z = -110.0f;
        }
        if (twp->pos.z > 110.0f) {
            twp->pos.z = 110.0f;
        }
        if (twp->pos.x < 115.0f) {
            twp->pos.x = 115.0f;
        }
        if (twp->pos.x > 300.0f) {
            twp->pos.x = 300.0f;
        }
    }
}
