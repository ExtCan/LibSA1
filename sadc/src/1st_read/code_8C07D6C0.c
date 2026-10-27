#include "common.h"
#include "ninja.h"
#include "sa1/sa1.h"

/*
 * Land position (island) list, chunk 8C07D6C0 (functions 1-3 of 4).
 * playerwk.island_list is a 15-entry PL_LANDPOSI array allocated by
 * func_8C07D67C; island_num is the number of entries in use.
 */

void func_8C0714B6(taskwk *twp, motionwk *mwp, playerwk *pwp);

/* free the island list (player destructor 8C06C5CA) */
void func_8C07D6C0(playerwk *pwp) {
    if (pwp->island_list != NULL) {
        MFree(pwp->island_list);
        pwp->island_list = NULL;
    }
}

/*
 * Register a landing spot (x, y, z) of radius r for players 0 and 1
 * (O_ITEMBOX): the first player standing at or above y whose distance to the
 * spot minus r is at most 120 gets it appended to its island list.
 */
void func_8C07D6E0(Float x, Float y, Float z, Float r) {
    Sint32 i;
    taskwk *twp;
    playerwk *pwp;
    Float d;
    Sint32 n;

    for (i = 0; i < 2; i++) {
        twp = playertwp[i];
        if (twp == NULL) {
            continue;
        }
        if (y > twp->pos.y) {
            continue;
        }
        pwp = playerpwp[i];
        /* the two accumulations are FMACs; SA1_FMAC(a, a, d) changes the SHC output */
        d = (x - twp->pos.x) * (x - twp->pos.x);
        SA1_FMAC_ACC(d, y - twp->pos.y, y - twp->pos.y);
        SA1_FMAC_ACC(d, z - twp->pos.z, z - twp->pos.z);
        d = njSqrt(d) - r;
        if (d > 120.0f) {
            continue;
        }
        if (pwp->island_num >= 15) {
            continue;
        }
        n = pwp->island_num;
        pwp->island_list[n].x = x;
        pwp->island_list[n].y = y;
        pwp->island_list[n].z = z;
        pwp->island_list[n].r = r;
        pwp->island_list[n].d = d;
        pwp->island_num++;
        return;
    }
}

/*
 * Jump to a registered landing spot (character mains): pick the closest
 * reachable spot ahead of the player, simulate the fall for up to 120 frames
 * and, when the player would land on it, steer the speed toward it.
 * Returns 1 when steering was applied.
 *
 * NON_MATCHING: 511 of 517 instructions identical. Only the register
 * assignment of the two loads in "spd.x * spd.x + spd.z * spd.z" differs
 * (original: spd.x in FR5, spd.z in FR4; SHC gives FR4/FR5). Writing the
 * terms as z*z + x*x swaps the registers but also the FMAC order.
 */
#ifdef NON_MATCHING
Sint32 func_8C07D7B4(taskwk *twp, motionwk *mwp, playerwk *pwp) {
    NJS_POINT3 pos;
    NJS_POINT3 acc;
    NJS_POINT3 spd;
    Float d;
    Angle ang;
    Sint32 i;
    PL_LANDPOSI *lp;
    PL_LANDPOSI *best;
    Angle sub;
    Angle angy;
    Angle aimy;
    Angle angz;

    best = NULL;
    pos = twp->pos;
    for (i = 0; i < pwp->island_num; i++) {
        lp = &pwp->island_list[i];
        if (pos.y < lp->y) {
            continue;
        }
        ang = SA1_FTOI(atan2f(lp->z - pos.z, lp->x - pos.x) * 10430.381f);
        sub = SubAngle(twp->ang.y, ang);
        if (sub > 0x1800 || sub < -0x1800) {
            d = (lp->z - pos.z) * (lp->z - pos.z);
            SA1_FMAC_ACC(d, lp->x - pos.x, lp->x - pos.x); /* FMAC */
            d = njSqrt(d);
            if (d > lp->r) {
                continue;
            }
        }
        if (best != NULL && best->d < lp->d) {
            continue;
        }
        lp->angx = sub;
        lp->angz = ang;
        best = lp;
    }
    if (best == NULL) {
        return 0;
    }
    ang = best->angx;
    if (ang > 0x1800 || ang < -0x1800 || best->d < 0.0f) {
        return 0;
    }
    angy = twp->ang.y;
    aimy = mwp->ang_aim.y;
    func_8C071700(twp, mwp, pwp);
    twp->ang.y = angy;
    ang = mwp->ang_aim.y;
    mwp->ang_aim.y = aimy;
    ang = DiffAngle(ang, best->angz);
    if (ang > 0x1800) {
        return 0;
    }
    angz = best->angz;
    pos.x -= best->x;
    pos.y -= best->y;
    pos.z -= best->z;
    best->h = njSqrt(SA1_FMAC(pos.x, pos.x, pos.z * pos.z));
    spd = mwp->spd;
    acc.x = 0.0f;
    acc.y = -pwp->p.weight;
    acc.z = 0.0f;
    for (i = 0; i < 120; i++) {
        spd.x += SA1_FMAC(pwp->p.air_resist_air, spd.x, acc.x);
        spd.y += SA1_FMAC(pwp->p.air_resist_y, spd.y, acc.y);
        spd.z += SA1_FMAC(pwp->p.air_resist_z, spd.z, acc.z);
        if (spd.y < -pwp->p.lim_v_spd) {
            spd.y = -pwp->p.lim_v_spd;
        }
        pos.x += spd.x;
        pos.y += spd.y;
        pos.z += spd.z;
        if (!(pos.y > best->y)) { /* FCMP/GT: NaN takes this branch */
            pos.x += best->x - twp->pos.x;
            pos.z += best->z - twp->pos.z;
            d = njSqrt(SA1_FMAC(pos.x, pos.x, pos.z * pos.z));
            if (d > best->h) {
                d = (best->h - best->r * 0.75f) - d;
                spd.x = d * njCos(angz);
                spd.y = 0.0f;
                spd.z = d * njSin(angz);
            } else if (d > best->h - best->r * 0.75f) {
                if (SA1_FMAC(spd.x, spd.x, spd.z * spd.z) > pwp->p.run_speed) {
                    d = (best->h - best->r) - d;
                    spd.x = d * njCos(angz);
                    spd.y = 0.0f;
                    spd.z = d * njSin(angz);
                } else {
                    spd.z = 0.0f;
                    spd.y = 0.0f;
                    spd.x = 0.0f;
                }
            } else {
                d = (best->h - best->r * 0.5f) - d;
                spd.x = d * njCos(angz);
                spd.y = 0.0f;
                spd.z = d * njSin(angz);
            }
            spd.x /= (Float)i + 1.0f;
            spd.z /= (Float)i + 1.0f;
            if (spd.x > pwp->p.run_accel) {
                spd.x = pwp->p.run_accel;
            }
            if (spd.x < -pwp->p.run_accel) {
                spd.x = -pwp->p.run_accel;
            }
            if (spd.z > pwp->p.run_accel) {
                spd.z = pwp->p.run_accel;
            }
            if (spd.z < -pwp->p.run_accel) {
                spd.z = -pwp->p.run_accel;
            }
            mwp->acc.x += spd.x;
            mwp->acc.z += spd.z;
            if (best->angx < 0x1000 && best->angx > -0x1000) {
                twp->ang.y = AdjustAngle(twp->ang.y, best->angz, 0x100);
            }
            func_8C0714B6(twp, mwp, pwp);
            return 1;
        }
    }
    return 0;
}
#else
ASM_STUB(Sint32 func_8C07D7B4(taskwk *twp, motionwk *mwp, playerwk *pwp))
#endif
