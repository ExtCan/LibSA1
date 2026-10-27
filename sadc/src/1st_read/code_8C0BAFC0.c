/*
 * 1ST_READ 8C0BAFC0: Tails action code (chunk 8C0BAFC0-8C0BDC80, functions
 * 1-3 of 29; the others are separate assignments).
 *
 * func_8C0BAFC0(twp, pwp): standing/idle action selector. On a steep floor
 * (floor normal against gravity steeper than njCos(0x11C7)) it picks a
 * slope stance (7 or 8) depending on which side the slope faces; on flat
 * ground it starts the idle action (4) and a voice after 1800 frames.
 * Called by TailsMain, TailsModeMachine and func_8C0B70E8.
 *
 * func_8C0BB11C(twp, mwp, pwp): run action (9-13 by speed, 59/63 with
 * twp->flag & 0x800) from pwp->spd.x + mwp->height * 5; stores
 * spd.x + height * 20 in pwp->work.f.
 *
 * func_8C0BB22A(twp, mwp, pwp): moves the player down by p.height (player
 * space) and steps the hanging/swinging motion frame (actions 91/92,
 * twp->mode 36).
 */
#include "common.h"
#include "ninja.h"
#include "task.h"
#include "sa1/sa1.h"

extern void func_8C07DCE0(taskwk *twp);         /* character voice (code_8C07DCE0.c) */
extern Float func_8C067414(NJS_VECTOR *v);      /* normalise (falls back to (0, 1, 0) when too short); returns the length */

void func_8C0BAFC0(taskwk *twp, playerwk *pwp)
{
    NJS_LINE line;
    NJS_POINT3 v;
    NJS_VECTOR b;
    NJS_VECTOR a;
    Float d;
    Angle ang;
    Sint32 act;

    if (pwp->mj.reqaction == 36) {
        return;
    }
    d = -njInnerProduct(&Gravity, &pwp->floor_normal);
    if (d > njCos(0x11C7)) {
        act = pwp->mj.reqaction;
        if (act != 0 && act != 1 && act != 2 && act != 3 && act != 4 && act != 5 && act != 6) {
            pwp->mj.reqaction = 0;
        } else if (pwp->waittimer > 1800) {
            pwp->waittimer = 0;
            pwp->mj.reqaction = 4;
            func_8C07DCE0(twp);
        }
    } else if (!(pwp->attr & 0x4000) && d > njCos(0x2000)) {
        line.vx = pwp->floor_normal.x;
        line.vy = pwp->floor_normal.y;
        line.vz = pwp->floor_normal.z;
        line.px = line.py = line.pz = 0.0f;
        v.x = 1.0f;
        v.y = v.z = 0.0f;
        PConvertVector_P2G(twp, &v);
        njDistanceP2L(&v, &line, &a);
        func_8C067414(&a);
        njDistanceP2L(&Gravity, &line, &b);
        func_8C067414(&b);
        ang = VectorAngle(&a, &b, &pwp->floor_normal);
        if (ang > 0xC000 || ang < 0x4000) {
            pwp->mj.reqaction = 8;
        } else {
            pwp->mj.reqaction = 7;
        }
    }
    pwp->waittimer++;
}

void func_8C0BB11C(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    Float spd;
    Sint32 act;

    spd = SA1_FMAC(5.0f, mwp->height, pwp->spd.x);
    act = pwp->mj.reqaction;
    if (act == 36 || act == 22 || act == 61) {
        return;
    }
    if (twp->flag & 0x800) {
        if (spd > pwp->p.run_speed) {
            pwp->mj.reqaction = 59;
        } else {
            pwp->mj.reqaction = 63;
        }
    } else if (spd > pwp->p.dash_speed) {
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

void func_8C0BB22A(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_POINT3 v;

    v.x = 0.0f;
    v.y = pwp->p.height;
    v.z = 0.0f;
    PConvertVector_P2G(twp, &v);
    njSubVector(&twp->pos, &v);
    if (pwp->spd.x != 0.0f && mwp->rad == 0.0f) {
        if (pwp->mj.nframe == 0.0f || pwp->mj.nframe >= 32.0f) {
            twp->mode = 36;
            if (pwp->mj.nframe == 0.0f) {
                pwp->mj.reqaction = 92;
            } else {
                pwp->mj.reqaction = 91;
            }
            pwp->spd.x = 0.0f;
        } else {
            twp->mode = 36;
            pwp->spd.x = 0.0f;
            if (pwp->mj.nframe >= 16.0f && pwp->mj.nframe <= 48.0f) {
                pwp->mj.reqaction = 91;
            } else {
                pwp->mj.reqaction = 92;
            }
        }
    } else if (pwp->spd.x > 0.0f) {
        if (pwp->mj.nframe >= 0.0f && pwp->mj.nframe < 32.0f) {
            pwp->mj.nframe += 2.0f;
            if (pwp->mj.nframe == 32.0f && !GetAnalog(twp, NULL, NULL)) {
                twp->mode = 36;
                pwp->mj.reqaction = 91;
                pwp->spd.x = 0.0f;
            }
        } else {
            pwp->mj.nframe += 2.0f;
            if (pwp->mj.nframe >= 63.0f) {
                pwp->mj.nframe = 0.0f;
            }
            if (pwp->mj.nframe == 0.0f && !GetAnalog(twp, NULL, NULL)) {
                twp->mode = 36;
                pwp->mj.reqaction = 92;
                pwp->spd.x = 0.0f;
            }
        }
    } else if (pwp->spd.x < 0.0f) {
        if (pwp->mj.nframe > 0.0f && pwp->mj.nframe <= 32.0f) {
            pwp->mj.nframe -= 2.0f;
            if (pwp->mj.nframe == 0.0f && !GetAnalog(twp, NULL, NULL)) {
                twp->mode = 36;
                pwp->mj.reqaction = 92;
                pwp->spd.x = 0.0f;
            }
        } else {
            pwp->mj.nframe -= 2.0f;
            if (pwp->mj.nframe < 0.0f) {
                pwp->mj.nframe = 62.0f;
            }
            if (pwp->mj.nframe == 32.0f && !GetAnalog(twp, NULL, NULL)) {
                twp->mode = 36;
                pwp->mj.reqaction = 91;
                pwp->spd.x = 0.0f;
            }
        }
    } else {
        pwp->spd.x = 0.0f;
    }
}
