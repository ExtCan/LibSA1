/*
 * 1ST_READ 8C0C7786-8C0C7D4C (chunk 8C0C7600-8C0C80D4, functions 4-5 of 7):
 * Knuckles.
 *
 *  func_8C0C7786  per-frame attack collision of Knuckles' punches (KnucklesMain,
 *                 (twp, mwp, pwp)). Action 56 (the dash punch): from frame 15
 *                 speed ramps up to 4.0 at frame 20, kicks up dust and calls
 *                 func_8C0CB628 / pad vibration until frame 30, then decays.
 *                 Then shape 0 attack levels 0/0 and shape 1 off; for actions
 *                 54-63 the entry D_8C15BFEC[action - 54] gives the hand (or
 *                 foot) whose weld position, rotated by twp->ang and added to
 *                 shape 0's center, is the punch point: during
 *                 [hit_start, hit_end] shape 1 is on at that point (levels 0/3,
 *                 twp->flag 0x400); at eff_frame an effect task
 *                 (func_8C0CA11C) is started there with a direction
 *                 (PConvertVector_P2G of +-2 on x or y); at hit_start the punch
 *                 sound (54/55: SE 0x2E2 + vibration, 56: SE 0x483 + 0x2E4);
 *                 during [trail_start, trail_end] a trail task (func_8C0CA040).
 *  func_8C0C7C26  (func_8C0C7D4C, (twp, mwp, pwp, v)): with a = yaw of v
 *                 (atan2f(v->z, v->x) in BAMS): when the reversed heading
 *                 (twp->ang.y + 0x8000) is more than 0x2000 away from a,
 *                 twp->ang.y = a +- 0x4000 (sign of SubAngle), else
 *                 a + SubAngle(twp->ang.y + 0x8000, a); copy it into
 *                 mwp->ang_aim.y, clear pwp->free.w[3] and move twp->pos on
 *                 x/z by the last frame's movement (history frame 1) plus v.
 */
#include "common.h"
#include "sa1/sa1.h"

/*
 * Knuckles' punch table D_8C15BFEC (game data, extern only): one 0x18-byte
 * entry per action 54-63 (index action - 54). Read with MOV.W + EXTU.W
 * (node) and MOV.W (dir) at 8C0C7948/8C0C7A96.
 */
typedef struct {
    Uint16 node;                /* 0x00 index into pwp->righthand_pos.. (0 right hand, 1 left hand, 3 left foot) */
    Sint16 dir;                 /* 0x02 direction of the func_8C0CA11C effect: 1 +x, 2 -x, 3 +y, 4 -y, 0 none */
    Float trail_start;          /* 0x04 func_8C0CA040 window (< 0: none) */
    Float trail_end;            /* 0x08 */
    Float hit_start;            /* 0x0C shape 1 on; sound at this frame */
    Float hit_end;              /* 0x10 */
    Float eff_frame;            /* 0x14 frame of the func_8C0CA11C effect */
} KNUCKLES_PUNCH;               /* 0x18 */

SA1_ASSERT_SIZE(KNUCKLES_PUNCH, 0x18);

extern KNUCKLES_PUNCH D_8C15BFEC[10];

void func_8C0465E0(NJS_POINT3 *pos, NJS_POINT3 *spd, Float scl);   /* dust effect */
void func_8C0CB628(NJS_POINT3 *pos, Angle3 *ang, Float frame);
void func_8C109EAC(Sint32 pno, Sint32 a, Sint32 b, Sint32 c);      /* pad vibration */
void func_8C0CA040(task *tp);                                       /* punch trail exec */
void func_8C0CA11C(task *tp);                                       /* punch effect exec */

void func_8C0C7786(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_POINT3 pos;
    NJS_POINT3 vec;
    KNUCKLES_PUNCH *pp;
    task *tp;
    task *ctp;

    if (pwp->mj.reqaction == 56) {
        if (pwp->mj.nframe < 15.0f) {
            /* nothing before frame 15 (an empty first branch: a plain
             * "nframe >= 15.0f" condition allocates other FP registers) */
        } else if (pwp->mj.nframe < 20.0f) {
            pwp->spd.x = (pwp->mj.nframe - 15.0f) * 0.79999997f;
        } else if (pwp->mj.nframe < 30.0f) {
            pwp->spd.x = 4.0f;
            pos = twp->pos;
            pos.y += 2.0f;
            vec.x = -(mwp->spd.x * 0.19999999f);
            vec.y = 0.0f;
            vec.z = -(mwp->spd.z * 0.19999999f);
            func_8C0465E0(&pos, &vec, 1.0f);
            vec.x = vec.z = 0.0f;
            func_8C0465E0(&pos, &vec, 1.0f);
            func_8C0CB628(&CWP(twp)->info->center, &twp->ang, pwp->mj.nframe);
            func_8C109EAC((Uint8)twp->counter.b[0], 3, 15, 0);
        } else {
            pwp->spd.x *= 0.25f;
        }
    }
    CWP(twp)->info->flag = (CWP(twp)->info->flag & ~3) | 0;
    CWP(twp)->info->flag = (CWP(twp)->info->flag & ~0xC) | (0 << 2);
    CWP(twp)->info[1].attr |= 0x10;
    if (pwp->mj.reqaction >= 54 && pwp->mj.reqaction <= 63) {
        pp = &D_8C15BFEC[pwp->mj.reqaction - 54];
        /* righthand_pos..user1_pos are consecutive NJS_POINT3 members (no
         * pointers between them: same layout on every target) */
        vec = (&pwp->righthand_pos)[pp->node];
        njPushMatrix(D_8C67C640);
        twp->ang.z ? njRotateZ(NULL, twp->ang.z) : (void)0;
        twp->ang.x ? njRotateX(NULL, twp->ang.x) : (void)0;
        0x8000 - twp->ang.y ? njRotateY(NULL, 0x8000 - twp->ang.y) : (void)0;
        njCalcVector(NULL, &vec, &pos);
        njPopMatrix(1);
        njAddVector(&pos, &CWP(twp)->info->center);
        if (pwp->mj.reqaction == 58) {
            pos.y += 2.5f;
        }
        twp->flag &= ~0x400;
        if (pwp->mj.nframe >= pp->hit_start && pwp->mj.nframe <= pp->hit_end) {
            CWP(twp)->info->flag = (CWP(twp)->info->flag & ~3) | 0;
            CWP(twp)->info->flag = (CWP(twp)->info->flag & ~0xC) | (3 << 2);
            CWP(twp)->info[1].attr &= ~0x10;
            CWP(twp)->info[1].center = pos;
            twp->flag |= 0x400;
        }
        if (pp->dir != 0 && pwp->mj.nframe >= pp->eff_frame && pwp->mj.nframe < pp->eff_frame + 1.0f) {
            if ((tp = CreateElementalTask(2, 6, func_8C0CA11C)) != NULL) {
                switch (pp->dir) {
                case 1:
                    vec.x = 2.0f;
                    vec.y = vec.z = 0.0f;
                    break;
                case 2:
                    vec.x = -2.0f;
                    vec.y = vec.z = 0.0f;
                    break;
                case 3:
                    vec.y = 2.0f;
                    vec.x = vec.z = 0.0f;
                    break;
                case 4:
                    vec.y = -2.0f;
                    vec.x = vec.z = 0.0f;
                    break;
                }
                PConvertVector_P2G(twp, &vec);
                tp->twp->pos = pos;
                tp->twp->scl = vec;
            }
        }
        if (pwp->mj.nframe >= pp->hit_start && pwp->mj.nframe < pp->hit_start + 1.0f) {
            switch (pwp->mj.reqaction) {
            case 54:
            case 55:
                func_8C0940DA(0x2E2, 0, 0, 0);
                func_8C109EAC((Uint8)twp->counter.b[0], 3, 15, 0);
                break;
            case 56:
                func_8C0940DA(0x483, 0, 0, 0);
                func_8C0940DA(0x2E4, 0, 0, 0);
                break;
            }
        }
        if (pp->trail_start >= 0.0f && pwp->mj.nframe >= pp->trail_start && pwp->mj.nframe <= pp->trail_end) {
            if ((ctp = CreateElementalTask(2, 6, func_8C0CA040)) != NULL) {
                ctp->twp->pos = pos;
                ctp->twp->scl.x = 1.0f;
            }
        }
    }
}

void func_8C0C7C26(taskwk *twp, motionwk *mwp, playerwk *pwp, NJS_POINT3 *v)
{
    NJS_POINT3 pos;
    Float dx;
    Float dz;
    Sint32 ang;

    ang = SA1_FTOI(atan2f(v->z, v->x) * 10430.381f);
    if (DiffAngle(twp->ang.y + 0x8000, ang) > 0x2000) {
        if (SubAngle(twp->ang.y + 0x8000, ang) > 0) {
            twp->ang.y = ang + 0x4000;
        } else {
            twp->ang.y = ang - 0x4000;
        }
    } else {
        twp->ang.y = SubAngle(twp->ang.y + 0x8000, ang) + ang;
    }
    mwp->ang_aim.y = twp->ang.y;
    pwp->free.w[3] = 0;
    GetPlayerPosition(twp->counter.b[0], 1, &pos, NULL);
    dx = twp->pos.x - pos.x;
    dz = twp->pos.z - pos.z;
    twp->pos.x += dx + v->x;
    twp->pos.z += dz + v->z;
}
