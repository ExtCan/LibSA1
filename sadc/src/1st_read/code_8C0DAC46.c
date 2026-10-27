#include "sa1/sa1.h"

/*
 * Amy (1ST_READ 8C0DAC46-8C0DB230, functions 12-15 of chunk 8C0D6B40):
 * helpers of AmyMain (8C0D6B40) and AmyModeMachine (8C0D7960).
 *  func_8C0DAC46  (AmyMain) moves twp->pos down by p.height along the
 *                 player's up axis, then steps the 32-frame motion
 *                 (pwp->mj.nframe 0..31) by the sign of spd.x: moving
 *                 without drive (mwp->rad == 0) at frame 15 or >= 31 ends
 *                 it (mode 40, spd.x 0); forwards 0..14 -> 15, 15..31 wraps
 *                 at 32; backwards 16..31 -> 15, else wraps below 0 to 31;
 *                 arriving at 15 / 31 without stick input (GetAnalog) ends
 *                 it, otherwise sound 0x49F plays with probability 31/128
 *                 ((func_8C08A71E() & 0x7F) < 31); spd.x == 0 clears spd.x.
 *  func_8C0DAE96  free movement (debug mode, likely; Sonic: 8C0AF012):
 *                 press bit 2 of pad 0 returns to the start position
 *                 (8C04F8FA); the stick of the player's pad moves the
 *                 player camera-relative in x/z by 5 units, or along y
 *                 while pad 0 holds 0x400.
 *  func_8C0DB000  damage check (AmyModeMachine; Sonic: 8C0AFB10):
 *                 twp->flag & 4 or a damaging surface (attr & 0x10000),
 *                 not in mode 48 -> knock-back (mode 11, action 24,
 *                 spd = (-run_speed, jmp_y_spd), wtimer 120, vibration,
 *                 face 8); modes 17/18 first call 8C0DEFB6(&twp->pos).
 *  func_8C0DB08C  jump check (AmyModeMachine; Sonic: 8C0AFC98): skipped
 *                 while pwp->free.b[4] == 1; 8C06CEBE 2 = jump on an object
 *                 (mode 42, action 73), 1 = jump (mode 5, action 14,
 *                 work.f 1.2, inheriting the force-work speed of the moving
 *                 land it stood on) or, holding a 0x200 object, mode 31 /
 *                 action 63; voice 0x491 except in the last case.
 *
 * Translation unit: the original object holds the whole chunk
 * 8C0D6B40-8C0DCA6C. SHC dumps a literal pool every few hundred bytes; the
 * pool positions inside func_8C0DAC46 (8C0DACC0, 8C0DAE60) depend on the
 * literals func_8C0DA9A0 leaves pending, and they decide the long-branch
 * forms (BF vs BT/BRA) in func_8C0DAE96 and func_8C0DB08C. Compiled after
 * code_8C0DA6E8.c (func_8C0DA6E8..func_8C0DA9A0) the pools fall where the
 * original has them:
 *   py -3.12 tools/shcdiff.py <func> src/1st_read/code_8C0DAC46.c
 *       --flags=-define=TU_EXPERIMENT,NON_MATCHING
 * (func_8C0DAE96 and func_8C0DB08C MATCH there; func_8C0DAC46 and
 * func_8C0DB000 match either way).
 */
#ifdef TU_EXPERIMENT
#include "1st_read/code_8C0DA6E8.c"
#endif

/* reset the player to the stage start position (Sonic: code_8C0AEFA0.c) */
void func_8C04F8FA(taskwk *twp);
/* Amy (8C0DEFB6, other chunk): called with &twp->pos in modes 17/18 */
Bool func_8C0DEFB6(NJS_POINT3 *pos);
/* jump button test (code_8C06CCA0.c reads only twp; the player code passes pwp too) */
Sint32 func_8C06CEBE(taskwk *twp, playerwk *pwp);
/* Amy: release the held object (Sonic 8C0B1D60; JSR, 8C0DC75E is too far for BSR) */
void func_8C0DC75E(taskwk *twp, motionwk *mwp, playerwk *pwp);

/*
 * Matching notes: the frame-15/31 sound is a nested if (with the test in
 * the else-if condition, SHC loads func_8C0940DA into R3 instead of R1);
 * the random value is compared unsigned (CMP/HS).
 */
void func_8C0DAC46(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_POINT3 v;

    v.x = 0.0f;
    v.y = pwp->p.height;
    v.z = 0.0f;
    PConvertVector_P2G(twp, &v);
    njSubVector(&twp->pos, &v);
    if (pwp->spd.x != 0.0f && mwp->rad == 0.0f) {
        if (pwp->mj.nframe == 15.0f || pwp->mj.nframe >= 31.0f) {
            twp->mode = 40;
            pwp->spd.x = 0.0f;
        }
    } else if (pwp->spd.x > 0.0f) {
        if (pwp->mj.nframe >= 0.0f && pwp->mj.nframe < 15.0f) {
            pwp->mj.nframe += 1.0f;
            if (pwp->mj.nframe == 15.0f && GetAnalog(twp, NULL, NULL) == 0) {
                twp->mode = 40;
                pwp->spd.x = 0.0f;
            } else if (pwp->mj.nframe == 15.0f) {
                if (((Uint32)func_8C08A71E() & 0x7F) < 31) {
                    func_8C0940DA(0x49F, 0, 0, 0);
                }
            }
        } else {
            pwp->mj.nframe += 1.0f;
            if (pwp->mj.nframe >= 32.0f) {
                pwp->mj.nframe = 0.0f;
            }
            if (pwp->mj.nframe == 31.0f && GetAnalog(twp, NULL, NULL) == 0) {
                twp->mode = 40;
                pwp->spd.x = 0.0f;
            }
        }
    } else if (pwp->spd.x < 0.0f) {
        if (pwp->mj.nframe > 15.0f && pwp->mj.nframe <= 31.0f) {
            pwp->mj.nframe -= 1.0f;
            if (pwp->mj.nframe == 15.0f && GetAnalog(twp, NULL, NULL) == 0) {
                twp->mode = 40;
                pwp->spd.x = 0.0f;
            }
        } else {
            pwp->mj.nframe -= 1.0f;
            if (pwp->mj.nframe < 0.0f) {
                pwp->mj.nframe = 31.0f;
            }
            if (pwp->mj.nframe == 31.0f && GetAnalog(twp, NULL, NULL) == 0) {
                twp->mode = 40;
                pwp->spd.x = 0.0f;
            } else if (pwp->mj.nframe == 31.0f) {
                if (((Uint32)func_8C08A71E() & 0x7F) < 31) {
                    func_8C0940DA(0x49F, 0, 0, 0);
                }
            }
        }
    } else {
        pwp->spd.x = 0.0f;
    }
}

/*
 * Matches only in the original translation unit (TU_EXPERIMENT, see the
 * top of the file): standalone, the literals func_8C0DAC46 leaves pending
 * are dumped inside this function and the BF at 8C0DAEF6 becomes BT/BRA.
 * Matching notes: the 0x400 test is a "? TRUE : FALSE" value. A plain
 * "on & 0x400" condition (also with != 0, !!, 0x400U or through a
 * PDS_PERIPHERAL pointer) loads the address of atan2f into R3 instead of
 * R2 at 8C0DAF4E; "&& 1", "|| 0" and "? 1 : 0" match too. The same idea
 * should fix
 * Sonic's func_8C0AF012 (code_8C0AEFA0.c); Knuckles' func_8C0C5680 and
 * Eggman's func_8C0EE6D2 are identical copies.
 */
#ifdef NON_MATCHING
void func_8C0DAE96(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    Float fx;
    Float fy;
    Angle ang;

    if (B_8C754C08[0].press & 2) {
        func_8C04F8FA(twp);
    }
    if ((B_8C754C08[0].on & 0x400) ? TRUE : FALSE) {
        fx = (Float)(B_8C754C08[(Uint8)twp->counter.b[0]].x1 << 8);
        fy = (Float)(B_8C754C08[(Uint8)twp->counter.b[0]].y1 << 8);
        if (fx > 3072.0f || fx < -3072.0f || fy > 3072.0f || fy < -3072.0f) {
            if (fy != 0.0f) {
                fx = fy / njSqrt(fy * fy);
            } else {
                fx = 0.0f;
            }
            twp->pos.y -= fx * 5.0f;
        }
    } else {
        fx = (Float)(B_8C754C08[(Uint8)twp->counter.b[0]].x1 << 8);
        fy = (Float)(B_8C754C08[(Uint8)twp->counter.b[0]].y1 << 8);
        if (fx > 3072.0f || fx < -3072.0f || fy > 3072.0f || fy < -3072.0f) {
            ang = SA1_FTOI(atan2f(fy, fx) * 10430.381f) - camera_twp->ang.y;
            twp->pos.x SH_ONLY(+= 5.0f * njCos(ang)) PC_ONLY(= SA1_FMAC(5.0f, njCos(ang), twp->pos.x));
            twp->pos.z SH_ONLY(+= 5.0f * njSin(ang)) PC_ONLY(= SA1_FMAC(5.0f, njSin(ang), twp->pos.z));
        }
    }
}
#else
ASM_STUB(void func_8C0DAE96(taskwk *twp, motionwk *mwp, playerwk *pwp))
#endif

/* Matching notes: one combined condition and a single return 0 (separate
 * early returns reorder the blocks). */
Sint32 func_8C0DB000(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    if (pwp != NULL && ((twp->flag & 4) || (pwp->attr & 0x10000)) && twp->mode != 48) {
        if (twp->mode == 17 || twp->mode == 18) {
            func_8C0DEFB6(&twp->pos);
        }
        twp->mode = 11;
        pwp->mj.reqaction = 24;
        twp->flag &= ~4;
        pwp->spd.y = pwp->p.jmp_y_spd;
        pwp->spd.x = -pwp->p.run_speed;
        twp->wtimer = 120;
        func_8C109FDE((Uint8)twp->counter.b[0], 7, 59, 6);
        func_8C07B2B8(0, 8);
        return 1;
    }
    return 0;
}

/*
 * Matches only in the original translation unit (TU_EXPERIMENT, see the
 * top of the file): standalone, the pool positions make two branches long
 * (BF/BRA). Matching notes: the 8C06C880 result in a local, the explicit
 * else, pwp->ttp tested and reloaded in a nested if, and fwp assigned in
 * its own statement (with if ((fwp = ...) != NULL) SHC swaps R9/R10
 * between fwp and the ~2 mask).
 */
#ifdef NON_MATCHING
Sint32 func_8C0DB08C(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    sa1_forcewk *fwp;
    Sint32 obj;

    if (pwp->free.b[4] == 1) {
        return 0;
    }
    if (func_8C06CEBE(twp, pwp) == 2) {
        if (twp->flag & 0x800) {
            func_8C0DC75E(twp, mwp, pwp);
        }
        func_8C05E788(twp);
        twp->mode = 42;
        pwp->mj.reqaction = 73;
        twp->flag &= ~2;
        pwp->jumptimer = 0;
        func_8C0940DA(17, 0, 0, 0);
        func_8C0940DA(0x491, 0, 0, 0);
        return 1;
    } else if (func_8C06CEBE(twp, pwp)) {
        if (twp->flag & 0x800) {
            obj = func_8C06C880(twp);
            if (obj & 0x200) {
                twp->mode = 31;
                pwp->spd.y = pwp->p.jmp_y_spd;
                pwp->mj.reqaction = 63;
                twp->flag &= ~2;
                pwp->jumptimer = 0;
                func_8C0940DA(17, 0, 0, 0);
                return 1;
            } else {
                func_8C0DC75E(twp, mwp, pwp);
            }
        }
        twp->mode = 5;
        if (pwp->ttp != NULL) {
            fwp = (sa1_forcewk *)pwp->ttp->fwp;
            if (fwp != NULL) {
                func_8C07CB54(twp->counter.b[0], fwp->pos_spd.x, fwp->pos_spd.y, fwp->pos_spd.z);
            }
        }
        pwp->spd.y = pwp->p.jmp_y_spd;
        pwp->mj.reqaction = 14;
        pwp->work.f = 1.19999996f;     /* 0x3F999999 (1.2f truncated) */
        twp->flag &= ~2;
        twp->flag |= 0x500;
        pwp->jumptimer = 0;
        func_8C0940DA(17, 0, 0, 0);
        func_8C0940DA(0x491, 0, 0, 0);
        return 1;
    }
    return 0;
}
#else
ASM_STUB(Sint32 func_8C0DB08C(taskwk *twp, motionwk *mwp, playerwk *pwp))
#endif
