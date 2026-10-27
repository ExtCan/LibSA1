#include "sa1/sa1.h"
#include "sa1/protos.h"

/*
 * Player common (1ST_READ 8C06CCA0-8C06CFCC): input and orientation queries.
 *  func_8C06CCA0  +Y rotated by the player's angles (z, x, -y; unit matrix
 *                 pushed by 8C057B40), then by the matrix below it, has y > 0
 *  func_8C06CD22  GetAnalog: stick angle and stroke of the player (event lock,
 *                 no-control timer; Sonic in stages 0x902/0x903 with flag
 *                 0x100 gets his facing angle and stroke 0)
 *  func_8C06CE40  event lock released (lock == 1: clear speed, unlock) or the
 *                 stick points more than 0x6000 away from the facing angle
 *  func_8C06CEBE  button D_8C19E390 pressed: 2 when 8C05E6E8 reports > 0, else 1
 *  func_8C06CEFE  button D_8C19E390 held while jumptimer++ < p.jump2_timer
 *  func_8C06CF60  angle class (1..4) between the facing angle and the
 *                 reversed push-out vector of the hit object (8C0344DC)
 *
 * Matching notes: 8C06CEBE needs both operands of the button test as Sint32
 * (8C06CEFE matches with the Uint32 fields); 8C06CE40 compares the event
 * lock as Uint8 (EXTU.B); GetStageNumber is compared as Uint16 (EXTU.W).
 * 8C06CE40, 8C06CEBE and 8C06CF60 read only R4; their callers also pass
 * mwp / pwp in R5, so the second parameter belongs to the prototype
 * (sa1/protos.h) and is unused here (same SHC output).
 */

/* CCL hit query: push-out vector from the hit shape (8C0344DC; returns a
 * byte, non-zero when there is a hit; same prototype as code_8C06CA30.c) */
Sint8 func_8C0344DC(taskwk *twp, NJS_POINT3 *v);
/* query on the object the player touches (CWP(twp)->hit_cwp); returns -1, 0
 * or 1 (8C05E6E8, next to the jump panel code; also called by the mode
 * machines) */
Sint32 func_8C05E6E8(taskwk *twp);

#define EWP(twp) ((eventwk *)(twp)->ewp)

Bool func_8C06CCA0(taskwk *twp)
{
    NJS_VECTOR vs;
    NJS_VECTOR vd;

    vs.x = vs.z = 0.0f;
    vs.y = 1.0f;
    func_8C057B40();
    twp->ang.z ? njRotateZ(NULL, twp->ang.z) : (void)0;
    twp->ang.x ? njRotateX(NULL, twp->ang.x) : (void)0;
    -twp->ang.y ? njRotateY(NULL, -twp->ang.y) : (void)0;
    njCalcVector(NULL, &vs, &vd);
    njPopMatrix(1);
    njCalcVector(NULL, &vd, &vs);
    return vs.y > 0.0f;
}

Sint32 func_8C06CD22(taskwk *twp, Angle *ang, Float *mag)
{
    if (twp->flag & 0x4000) {
        if (EWP(twp)->lock) {
            return 1;
        }
        return 0;
    }
    if (B_8C754DCC[(Uint8)twp->counter.b[0]].stroke != 0.0f) {
        if (!(twp->flag & 0x2000) && playerpwp[(Uint8)twp->counter.b[0]]->nocontimer) {
            return 0;
        }
        if (ang) {
            *ang = B_8C754DCC[(Uint8)twp->counter.b[0]].ang;
        }
        if (mag) {
            *mag = B_8C754DCC[(Uint8)twp->counter.b[0]].stroke;
        }
        return 1;
    }
    if (twp->counter.b[1] == 0 && (twp->flag & 0x100)) {
        if ((Uint16)GetStageNumber() == 0x902 || (Uint16)GetStageNumber() == 0x903) {
            if (ang) {
                *ang = twp->ang.y;
            }
            if (mag) {
                *mag = 0.0f;
            }
            return 1;
        }
    }
    return 0;
}

Bool func_8C06CE40(taskwk *twp, motionwk *mwp)
{
    Angle ang;
    Sint32 diff;

    if (twp->flag & 0x4000) {
        if ((Uint8)EWP(twp)->lock == 1) {
            PClearSpeed(NULL, playerpwp[(Uint8)twp->counter.b[0]]);
            EWP(twp)->lock = 0;
            twp->flag &= ~0x4000;
            return TRUE;
        }
    } else if (!(twp->flag & 0x2000) && GetAnalog(twp, &ang, NULL)) {
        diff = DiffAngle(twp->ang.y, ang);
        if (diff > 0x6000) {
            return TRUE;
        }
    }
    return FALSE;
}

Sint32 func_8C06CEBE(taskwk *twp, playerwk *pwp)
{
    if (twp->flag & 0x4000) {
        return 0;
    }
    if ((Sint32)B_8C754C08[(Uint16)(Uint8)twp->counter.b[0]].press & (Sint32)D_8C19E390) {
        if (func_8C05E6E8(twp) > 0) {
            return 2;
        }
        return 1;
    }
    return 0;
}

Bool func_8C06CEFE(taskwk *twp, playerwk *pwp)
{
    if (twp->flag & 0x4000) {
        return FALSE;
    }
    if ((B_8C754C08[(Uint16)(Uint8)twp->counter.b[0]].on & D_8C19E390) && pwp->jumptimer++ < pwp->p.jump2_timer) {
        return TRUE;
    }
    return FALSE;
}

Sint32 func_8C06CF60(taskwk *twp, motionwk *mwp)
{
    NJS_POINT3 v;
    Angle ang;
    Sint32 diff;

    ang = twp->ang.y;
    func_8C0344DC(twp, &v);
    diff = DiffAngle(ang, SA1_FTOI(atan2f(-v.z, -v.x) * 10430.381f));
    if (diff > 0x6000) {
        return 1;
    }
    if (diff > 0x3400) {
        return 2;
    }
    if (diff > 0x1C00) {
        return 3;
    }
    return 4;
}
