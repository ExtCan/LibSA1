#include "sa1/sa1.h"

/*
 * Knuckles (1ST_READ 8C0C5680-8C0C5B3A, chunk 8C0C5680-8C0C71A0, functions
 * 1-6 of 18): helpers of KnucklesMain (8C0C0640) and KnucklesModeMachine
 * (8C0C193A), Knuckles' counterparts of Sonic's code_8C0AEFA0.c and
 * code_8C0AFB10.c.
 *  func_8C0C5680  free movement (debug mode, likely; same as Sonic's
 *                 func_8C0AF012 except that the buttons are read from pad
 *                 0): press bit 2 returns to the start position
 *                 (8C04F8FA); the stick of the player's pad moves the player
 *                 camera-relative in x/z by 5 units, or along y while the
 *                 0x400 button is held. KnucklesMain passes twp, mwp, pwp;
 *                 only twp is read (prototype: sa1/protos.h)
 *  func_8C0C5792  spd.x raised to at least run_speed, spd.y and
 *                 free.w[3] cleared, SE 0x2EC (KnucklesModeMachine,
 *                 8C0C5BCA, 8C0C61F4)
 *  func_8C0C57C8  damage check: twp->flag & 4 (hurt) or a damaging surface
 *                 (pwp->attr & 0x10000), not in mode 52: knock-back (mode
 *                 14, action 25, spd = (-run_speed, jmp_y_spd)),
 *                 invulnerability timer wtimer = 120, pad vibration, face 8
 *  func_8C0C5888  jump check (8C06CEBE: 2 = jump on an object -> mode 46,
 *                 action 105; 1 -> mode 35 / action 92 when the held object
 *                 answers 0x200 (8C06C880), else the jump: mode 6, action
 *                 14, spd.y = jmp_y_spd, work.f = 2.0f, inheriting the
 *                 force-work speed of the moving land it stood on). A held
 *                 object (flag 0x800) is let go first (8C0C6DE0, like
 *                 Sonic's 8C0B1D60).
 *  func_8C0C59B6  action button D_8C19E38C pressed: mode 9, action 19,
 *                 speed cleared
 *  func_8C0C5A20  jump off a wall while climbing (mode 6, action 14,
 *                 spd.x = spd.y = jmp_y_spd * 0.5325): moves 5 units along
 *                 the old up vector, stands upright facing that direction
 *                 (ang.x = ang.z = 0, ang.y from the vector), then moves 5
 *                 units back along the new up vector
 *
 * Matching notes: func_8C0C5680 needs the early return after the y branch
 * (with if/else SHC loads atan2f into R3 instead of R2: the same problem as
 * Sonic's NON_MATCHING func_8C0AF012). func_8C0C5888 is Sonic's
 * func_8C0AFC98 pattern: three separate SE calls (hoisted into R10 and
 * cross-jumped), the 8C06C880 result in a local, else-if and the explicit
 * else, fwp assigned in a nested if (pwp->ttp reloaded, fwp in R9).
 * func_8C0C59B6 indexes the pads with (Uint16)(Uint8) (MULU.W).
 */

/* jump button pressed: 0, 1, or 2 when 8C05E6E8 reports > 0 (defined in
 * code_8C06CCA0.c with (taskwk *); the mode machines pass pwp too) */
Sint32 func_8C06CEBE(taskwk *twp, playerwk *pwp);
/* reset the player to the stage start position */
void func_8C04F8FA(taskwk *twp);
/* let go of the held object (same chunk, 8C0C6DE0, out of BSR range: JSR) */
void func_8C0C6DE0(taskwk *twp, motionwk *mwp, playerwk *pwp);

/* "acc += a * b" compiled to FMAC: SHC needs the compound assignment (the
 * address of acc is formed before the product; acc = a * b + acc allocates
 * the registers the other way round); fused on PC (SA1_FMAC) */
#if defined(TARGET_PC)
#define FMAC_ACC(acc, a, b) ((acc) = SA1_FMAC((a), (b), (acc)))
#else
#define FMAC_ACC(acc, a, b) ((acc) += (a) * (b))
#endif

void func_8C0C5680(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    Float fx;
    Float fy;
    Angle ang;

    if (B_8C754C08[0].press & 2) {
        func_8C04F8FA(twp);
    }
    if (B_8C754C08[0].on & 0x400) {
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
        return;
    }
    fx = (Float)(B_8C754C08[(Uint8)twp->counter.b[0]].x1 << 8);
    fy = (Float)(B_8C754C08[(Uint8)twp->counter.b[0]].y1 << 8);
    if (fx > 3072.0f || fx < -3072.0f || fy > 3072.0f || fy < -3072.0f) {
        ang = SA1_FTOI(atan2f(fy, fx) * 10430.381f) - D_8C18B7EC->ang.y;
        FMAC_ACC(twp->pos.x, njCos(ang), 5.0f);
        FMAC_ACC(twp->pos.z, njSin(ang), 5.0f);
    }
}

void func_8C0C5792(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    if (pwp->spd.x <= pwp->p.run_speed) {
        pwp->spd.x = pwp->p.run_speed;
    }
    pwp->spd.y = 0.0f;
    pwp->free.w[3] = 0;
    func_8C0940DA(0x2EC, 0, 0, 0);
}

Sint32 func_8C0C57C8(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    if (pwp != NULL && ((twp->flag & 4) || (pwp->attr & 0x10000)) && twp->mode != 52) {
        twp->mode = 14;
        pwp->mj.reqaction = 25;
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

Sint32 func_8C0C5888(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    sa1_forcewk *fwp;
    Sint32 cls;

    if (func_8C06CEBE(twp, pwp) == 2) {
        if (twp->flag & 0x800) {
            func_8C0C6DE0(twp, mwp, pwp);
        }
        func_8C05E788(twp);
        twp->mode = 46;
        pwp->mj.reqaction = 105;
        twp->flag &= ~2;
        pwp->jumptimer = 0;
        func_8C0940DA(17, 0, 0, 0);
        return 1;
    } else if (func_8C06CEBE(twp, pwp)) {
        if (twp->flag & 0x800) {
            cls = func_8C06C880(twp);
            if (cls & 0x200) {
                twp->mode = 35;
                pwp->spd.y = pwp->p.jmp_y_spd;
                pwp->mj.reqaction = 92;
                twp->flag &= ~2;
                pwp->jumptimer = 0;
                func_8C0940DA(17, 0, 0, 0);
                return 1;
            } else {
                func_8C0C6DE0(twp, mwp, pwp);
            }
        }
        twp->mode = 6;
        if (pwp->ttp != NULL) {
            fwp = (sa1_forcewk *)pwp->ttp->fwp;
            if (fwp != NULL) {
                SetAccelerationP(twp->counter.b[0], fwp->pos_spd.x, fwp->pos_spd.y, fwp->pos_spd.z);
            }
        }
        pwp->spd.y = pwp->p.jmp_y_spd;
        pwp->mj.reqaction = 14;
        pwp->work.f = 2.0f;
        twp->flag &= ~2;
        twp->flag |= 0x500;
        pwp->jumptimer = 0;
        func_8C0940DA(17, 0, 0, 0);
        return 1;
    }
    return 0;
}

Sint32 func_8C0C59B6(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    if (B_8C754C08[(Uint16)(Uint8)twp->counter.b[0]].press & D_8C19E38C) {
        twp->mode = 9;
        pwp->spd.x = pwp->spd.y = pwp->spd.z = 0.0f;
        pwp->mj.reqaction = 19;
        pwp->work.f = 0.0f;
        twp->flag &= ~0x500;
        return 1;
    }
    return 0;
}

Sint32 func_8C0C5A20(taskwk *twp, playerwk *pwp)
{
    NJS_POINT3 v;

    if (func_8C06CEBE(twp, pwp)) {
        twp->mode = 6;
        pwp->spd.x = pwp->p.jmp_y_spd * 0.53249998f;
        pwp->spd.y = pwp->p.jmp_y_spd * 0.53249998f;
        pwp->spd.z = 0.0f;
        v.z = 0.0f;
        v.x = 0.0f;
        v.y = 1.0f;
        PConvertVector_P2G(twp, &v);
        FMAC_ACC(twp->pos.x, v.x, 5.0f);
        FMAC_ACC(twp->pos.y, v.y, 5.0f);
        FMAC_ACC(twp->pos.z, v.z, 5.0f);
        twp->ang.x = 0;
        twp->ang.y = SA1_FTOI(atan2f(v.z, v.x) * 10430.381f);
        twp->ang.z = 0;
        v.z = 0.0f;
        v.x = 0.0f;
        v.y = 1.0f;
        PConvertVector_P2G(twp, &v);
        twp->pos.x -= v.x * 5.0f;
        twp->pos.y -= v.y * 5.0f;
        twp->pos.z -= v.z * 5.0f;
        pwp->mj.reqaction = 14;
        pwp->work.f = 2.0f;
        twp->flag |= 0x500;
        pwp->jumptimer = 0;
        func_8C0940DA(17, 0, 0, 0);
        return 1;
    }
    return 0;
}
