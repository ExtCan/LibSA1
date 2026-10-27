#include "common.h"
#include "ninja.h"
#include "sa1/sa1.h"

/*
 * Amy Rose (1ST_READ chunk 8C0D6B40-8C0DCA6C, functions 9-11 of 32):
 *
 *  func_8C0DA6E8  stand selector (AmyMain, AmyModeMachine): on level ground
 *                 (cos(0x11C7), about 25 degrees) requests 0-6 become the
 *                 idle action 4 (with a voice, 8C07DCE0) after 1800 frames,
 *                 any other request is reset to 0; on steeper ground (up to
 *                 45 degrees) without attr 0x4000 the slope stand action 7
 *                 or 8 is picked by the angle between Amy's forward axis and
 *                 gravity, both projected onto the ground plane. Same code as
 *                 Sonic's func_8C0AE3D6 (code_8C0AE0E0.c), except that a
 *                 projection of length 0 is replaced by (1, 0, 0) instead of
 *                 being normalised.
 *  func_8C0DA8DA  run action selector (AmyMain, AmyModeMachine): leaves 21
 *                 and 46 alone; speed spd.x + 5 * mwp->height picks 44/48
 *                 while holding an object (twp->flag & 0x800), else
 *                 13/12/11/10/9 by 1.2 / rush / run / jog speed; then
 *                 pwp->work.f = spd.x + 20 * mwp->height.
 *  func_8C0DA9A0  (AmyMain) moves twp->pos down by p.height along the
 *                 player's up axis, then works on the motion frame
 *                 (mj.nframe): moving (spd.x != 0) without drive
 *                 (mwp->rad == 0) inside frames 14-18 or 46-50 sets
 *                 twp->mode 38 and spd.x 0; otherwise the frame steps
 *                 by one in the direction of spd.x (14 -> 46 forwards,
 *                 50 -> 18 backwards, wrapping at 63 / 0) and stops the
 *                 same way at the end of a window when there is no
 *                 stick input (GetAnalog); with spd.x == 0 it rocks the
 *                 frame inside 14-18 or 46-50, spd.z being the
 *                 direction flag (0 = down, -1 = up).
 */

extern void func_8C07DCE0(taskwk *twp);                                 /* character voice (code_8C07DCE0.c) */

void func_8C0DA6E8(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_LINE line;
    NJS_POINT3 v;
    NJS_POINT3 b;
    NJS_POINT3 a;
    Float dot;
    Angle ang;

    dot = -njInnerProduct(&Gravity, &pwp->floor_normal);
    if (dot > njCos(0x11C7)) {
        if (pwp->mj.reqaction != 0 && pwp->mj.reqaction != 1 && pwp->mj.reqaction != 2
            && pwp->mj.reqaction != 3 && pwp->mj.reqaction != 4 && pwp->mj.reqaction != 5
            && pwp->mj.reqaction != 6) {
            pwp->mj.reqaction = 0;
        } else if (pwp->waittimer > 1800) {
            pwp->waittimer = 0;
            pwp->mj.reqaction = 4;
            func_8C07DCE0(twp);
        }
    } else if (!(pwp->attr & 0x4000)) {
        if (dot > njCos(0x2000)) {
            line.vx = pwp->floor_normal.x;
            line.vy = pwp->floor_normal.y;
            line.vz = pwp->floor_normal.z;
            line.px = line.py = line.pz = 0.0f;
            v.x = 1.0f;
            v.y = v.z = 0.0f;
            PConvertVector_P2G(twp, &v);
            njDistanceP2L(&v, &line, &a);
            if (njScalor(&a) == 0.0f) {
                a.x = 1.0f;
                a.y = a.z = 0.0f;
            } else {
                njUnitVector(&a);
            }
            njDistanceP2L(&Gravity, &line, &b);
            if (njScalor(&b) == 0.0f) {
                b.x = 1.0f;
                b.y = b.z = 0.0f;
            } else {
                njUnitVector(&b);
            }
            ang = VectorAngle(&a, &b, &pwp->floor_normal);
            if (ang > 0xC000 || ang < 0x4000) {
                pwp->mj.reqaction = 8;
            } else {
                pwp->mj.reqaction = 7;
            }
        }
    }
    pwp->waittimer++;
}

void func_8C0DA8DA(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    Float spd;

    spd = SA1_FMAC(5.0f, mwp->height, pwp->spd.x);
    if (pwp->mj.reqaction == 21 || pwp->mj.reqaction == 46) {
        return;
    }
    if (twp->flag & 0x800) {
        if (spd > pwp->p.run_speed) {
            pwp->mj.reqaction = 44;
        } else {
            pwp->mj.reqaction = 48;
        }
    } else if (spd > 1.19999995f) {
        pwp->mj.reqaction = 13;
    } else if (spd > pwp->p.rush_speed) {
        pwp->mj.reqaction = 12;
    } else if (spd > pwp->p.run_speed) {
        pwp->mj.reqaction = 11;
    } else if (spd > pwp->p.jog_speed) {
        pwp->mj.reqaction = 10;
    } else {
        pwp->mj.reqaction = 9;
    }
    pwp->work.f = SA1_FMAC(20.0f, mwp->height, pwp->spd.x);
}

void func_8C0DA9A0(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_POINT3 v;

    v.x = 0.0f;
    v.y = pwp->p.height;
    v.z = 0.0f;
    PConvertVector_P2G(twp, &v);
    njSubVector(&twp->pos, &v);
    if (pwp->spd.x != 0.0f && mwp->rad == 0.0f) {
        if ((pwp->mj.nframe >= 14.0f && pwp->mj.nframe <= 18.0f)
            || (pwp->mj.nframe >= 46.0f && pwp->mj.nframe <= 50.0f)) {
            twp->mode = 38;
            pwp->spd.x = 0.0f;
        }
    } else if (pwp->spd.x > 0.0f) {
        if (pwp->mj.nframe >= 14.0f && pwp->mj.nframe < 46.0f) {
            pwp->mj.nframe += 1.0f;
            if (pwp->mj.nframe == 46.0f && !GetAnalog(twp, NULL, NULL)) {
                twp->mode = 38;
                pwp->spd.x = 0.0f;
            }
        } else {
            pwp->mj.nframe += 1.0f;
            if (pwp->mj.nframe >= 63.0f) {
                pwp->mj.nframe = 0.0f;
            }
            if (pwp->mj.nframe == 14.0f && !GetAnalog(twp, NULL, NULL)) {
                twp->mode = 38;
                pwp->spd.x = 0.0f;
            }
        }
    } else if (pwp->spd.x < 0.0f) {
        if (pwp->mj.nframe > 18.0f && pwp->mj.nframe <= 50.0f) {
            pwp->mj.nframe -= 1.0f;
            if (pwp->mj.nframe == 18.0f && !GetAnalog(twp, NULL, NULL)) {
                twp->mode = 38;
                pwp->spd.x = 0.0f;
            }
        } else {
            pwp->mj.nframe -= 1.0f;
            if (pwp->mj.nframe < 0.0f) {
                pwp->mj.nframe = 62.0f;
            }
            if (pwp->mj.nframe == 50.0f && !GetAnalog(twp, NULL, NULL)) {
                twp->mode = 38;
                pwp->spd.x = 0.0f;
            }
        }
    } else {
        pwp->spd.x = 0.0f;
        if (pwp->mj.nframe >= 14.0f && pwp->mj.nframe <= 18.0f) {
            if (pwp->spd.z != 0.0f) {
                pwp->mj.nframe += 1.0f;
                if (pwp->mj.nframe > 18.0f) {
                    pwp->mj.nframe = 17.0f;
                    pwp->spd.z = -1.0f;
                }
            } else {
                pwp->mj.nframe -= 1.0f;
                if (pwp->mj.nframe < 14.0f) {
                    pwp->mj.nframe = 15.0f;
                    pwp->spd.z = 0.0f;
                }
            }
        } else if (pwp->spd.z != 0.0f) {
            pwp->mj.nframe += 1.0f;
            if (pwp->mj.nframe > 50.0f) {
                pwp->mj.nframe = 49.0f;
                pwp->spd.z = -1.0f;
            }
        } else {
            pwp->mj.nframe -= 1.0f;
            if (pwp->mj.nframe < 46.0f) {
                pwp->mj.nframe = 47.0f;
                pwp->spd.z = 0.0f;
            }
        }
    }
}
