#include "sa1/sa1.h"

/*
 * Big (1ST_READ 8C0E3EC0-8C0E43CA, functions 8-13 of chunk 8C0DFFA0):
 * helpers of BigMain (8C0DFFA0) and BigModeMachine (8C0E0E70).
 *  func_8C0E3EC0  (BigMain; Amy: 8C0DAC46) moves twp->pos down by p.height
 *                 along the player's up axis, then steps the 32-frame motion
 *                 (pwp->mj.nframe) by 2.0 in the direction of spd.x:
 *                 moving without drive (mwp->rad == 0) at frame 16 or >= 30
 *                 ends it (mode 29, spd.x 0); forwards 0..15 -> 16, else
 *                 wraps at 32; backwards 16<f<=32 -> 16, else wraps below 0
 *                 to 30; arriving at 16 / 30 without stick input (GetAnalog)
 *                 ends it, otherwise sound 0x4BA plays with probability
 *                 31/128 at 16 forwards and at 30 backwards; spd.x == 0
 *                 clears spd.x.
 *  func_8C0E40CE  damage check (Amy: 8C0DB000): not in modes 51/39, not
 *                 during request 19, not while 8C0E6352 says so; twp->flag
 *                 & 4 or a damaging surface (attr & 0x10000) -> knock-back
 *                 (mode 11, action 19, spd = (-run_speed, jmp_y_spd),
 *                 wtimer 120, voice 0x4BC, 8C01AE60).
 *  func_8C0E416E  jump check (Amy: 8C0DB08C): 8C06CEBE -> voice 0x4B0;
 *                 holding (0x800) a task flagged 0x1000: mode 23 / action
 *                 37; otherwise release it (8C0E55E2) and jump (mode 4,
 *                 action 15, inheriting the force-work speed of the land
 *                 object stood on); SE 17.
 *  func_8C0E428E  1 unless the player is in water (shadow.Attr_top &
 *                 0x400002) with its eye point (p.eyes_height up the
 *                 player's up axis) at or below the water surface
 *                 (shadow.y_top).
 *  func_8C0E42F0  button D_8C19E38C pressed: 8C0EB432 or not in water ->
 *                 mode 31 / action 49 and flag 0x400, else mode 32 /
 *                 action 52.
 *  func_8C0E4380  out of water (8C0E428E) and button D_8C19E38C pressed:
 *                 mode 40 / action 66.
 */

/* jump button test (code_8C06CCA0.c reads only twp; the player code passes pwp too) */
Sint32 func_8C06CEBE(taskwk *twp, playerwk *pwp);
/* Big: 1 in modes 35-37 and 43-45 (8C0E6352, same chunk, JSR) */
Sint32 func_8C0E6352(taskwk *twp);
/* Big: release the held object (8C0E55E2, same chunk, JSR) */
void func_8C0E55E2(taskwk *twp, motionwk *mwp, playerwk *pwp);
/* Big (8C0EB432, other chunk): reads twp->counter.b[0] */
Sint32 func_8C0EB432(taskwk *twp);
void func_8C01AE60(void);

void func_8C0E3EC0(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_POINT3 v;

    v.x = 0.0f;
    v.y = pwp->p.height;
    v.z = 0.0f;
    PConvertVector_P2G(twp, &v);
    njSubVector(&twp->pos, &v);
    if (pwp->spd.x != 0.0f && mwp->rad == 0.0f) {
        if (pwp->mj.nframe == 16.0f || pwp->mj.nframe >= 30.0f) {
            twp->mode = 29;
            pwp->spd.x = 0.0f;
        }
    } else if (pwp->spd.x > 0.0f) {
        if (pwp->mj.nframe >= 0.0f && pwp->mj.nframe < 16.0f) {
            pwp->mj.nframe += 2.0f;
            if (pwp->mj.nframe == 16.0f && GetAnalog(twp, NULL, NULL) == 0) {
                twp->mode = 29;
                pwp->spd.x = 0.0f;
            } else if (pwp->mj.nframe == 16.0f) {
                if (((Uint32)func_8C08A71E() & 0x7F) < 31) {
                    func_8C0940DA(0x4BA, 0, 0, 0);
                }
            }
        } else {
            pwp->mj.nframe += 2.0f;
            if (pwp->mj.nframe >= 32.0f) {
                pwp->mj.nframe = 0.0f;
            }
            if (pwp->mj.nframe == 30.0f && GetAnalog(twp, NULL, NULL) == 0) {
                twp->mode = 29;
                pwp->spd.x = 0.0f;
            }
        }
    } else if (pwp->spd.x < 0.0f) {
        if (pwp->mj.nframe > 16.0f && pwp->mj.nframe <= 32.0f) {
            pwp->mj.nframe -= 2.0f;
            if (pwp->mj.nframe == 16.0f && GetAnalog(twp, NULL, NULL) == 0) {
                twp->mode = 29;
                pwp->spd.x = 0.0f;
            }
        } else {
            pwp->mj.nframe -= 2.0f;
            if (pwp->mj.nframe < 0.0f) {
                pwp->mj.nframe = 30.0f;
            }
            if (pwp->mj.nframe == 30.0f && GetAnalog(twp, NULL, NULL) == 0) {
                twp->mode = 29;
                pwp->spd.x = 0.0f;
            } else if (pwp->mj.nframe == 30.0f) {
                if (((Uint32)func_8C08A71E() & 0x7F) < 31) {
                    func_8C0940DA(0x4BA, 0, 0, 0);
                }
            }
        }
    } else {
        pwp->spd.x = 0.0f;
    }
}

/* Matching notes: separate early returns (one combined condition keeps
 * twp->mode in a stack slot instead of reloading it). */
Sint32 func_8C0E40CE(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    if (twp->mode == 51) {
        return 0;
    }
    if (twp->mode == 39) {
        return 0;
    }
    if ((twp->flag & 0x1000) && twp->smode == 19) {
        return 0;
    }
    if (func_8C0E6352(twp)) {
        return 0;
    }
    if ((twp->flag & 4) || (pwp != NULL && (pwp->attr & 0x10000))) {
        twp->flag &= ~0x400;
        func_8C0940DA(0x4BC, 0, 0, 0);
        twp->mode = 11;
        pwp->mj.reqaction = 19;
        twp->flag &= ~4;
        pwp->spd.y = pwp->p.jmp_y_spd;
        pwp->spd.x = -pwp->p.run_speed;
        twp->wtimer = 120;
        func_8C01AE60();
        return 1;
    }
    return 0;
}

Sint32 func_8C0E416E(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    sa1_forcewk *fwp;

    if (func_8C06CEBE(twp, pwp)) {
        func_8C0940DA(0x4B0, 0, 0, 0);
        if (twp->flag & 0x800) {
            if (pwp->htp != NULL && (pwp->htp->twp->flag & 0x1000)) {
                twp->mode = 23;
                pwp->spd.y = pwp->p.jmp_y_spd;
                pwp->mj.reqaction = 37;
                twp->flag &= ~2;
                pwp->jumptimer = 0;
                func_8C0940DA(17, 0, 0, 0);
                return 1;
            }
            func_8C0E55E2(twp, mwp, pwp);
        }
        twp->mode = 4;
        pwp->spd.y = pwp->p.jmp_y_spd;
        if (pwp->ttp != NULL) {
            fwp = (sa1_forcewk *)pwp->ttp->fwp;
            if (fwp != NULL) {
                func_8C07CB54(twp->counter.b[0], fwp->pos_spd.x, fwp->pos_spd.y, fwp->pos_spd.z);
            }
        }
        pwp->mj.reqaction = 15;
        twp->flag &= ~2;
        pwp->jumptimer = 0;
        func_8C0940DA(17, 0, 0, 0);
        return 1;
    }
    return 0;
}

/* Matching notes: the comparison as !(y_top < v.y) (operand load order; the
 * same NaN behaviour as the original), and v.x = v.z = 0.0f (store order). */
Sint32 func_8C0E428E(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_POINT3 v;

    if (pwp->shadow.Attr_top & 0x400002) {
        v.x = v.z = 0.0f;
        v.y = pwp->p.eyes_height;
        PConvertVector_P2G(twp, &v);
        njAddVector(&v, &twp->pos);
        if (!(pwp->shadow.y_top < v.y)) {
            return 0;
        }
    }
    return 1;
}

/* Matching notes: the (Sint32) casts on both operands of the button test
 * (see sa1/input.h) and a return 1 in each branch (register allocation). */
Sint32 func_8C0E42F0(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    if ((Sint32)B_8C754C08[(Uint8)twp->counter.b[0]].press & (Sint32)D_8C19E38C) {
        if (func_8C0EB432(twp) || !func_8C0E428E(twp, mwp, pwp)) {
            pwp->mj.reqaction = 49;
            twp->mode = 31;
            twp->flag |= 0x400;
            return 1;
        } else {
            pwp->mj.reqaction = 52;
            twp->mode = 32;
            return 1;
        }
    }
    return 0;
}

/* Matching notes: nested ifs and the (Sint32) casts (register allocation). */
Sint32 func_8C0E4380(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    if (func_8C0E428E(twp, mwp, pwp)) {
        if ((Sint32)B_8C754C08[(Uint8)twp->counter.b[0]].press & (Sint32)D_8C19E38C) {
            pwp->mj.reqaction = 66;
            twp->mode = 40;
            return 1;
        }
    }
    return 0;
}
