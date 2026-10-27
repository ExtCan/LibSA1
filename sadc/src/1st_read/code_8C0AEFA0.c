#include "sa1/sa1.h"

/*
 * Sonic (1ST_READ 8C0AEFA0-8C0AF5A0): helpers of SonicModeMachine (8C0A966C)
 * and SonicMain (8C0A77BC).
 *  func_8C0AEFA0  once the player runs at least at jog speed, create the
 *                 effect/sound task 8C07BA0A; the id depends on the stage
 *                 (level 8: by surface attribute 4 / 8 / other) and else 0x2B2
 *  func_8C0AF012  free movement (debug mode, likely): press bit 2 returns to
 *                 the start position (8C04F8FA); the stick moves the player
 *                 camera-relative in x/z by 5 units, or along y while the
 *                 0x400 button is held
 *  func_8C0AF18C  start of the target dash (homing attack or light speed
 *                 dash, not established): effect (super / normal), sound
 *                 0x2D3, then a straight dash when nothing can be targeted,
 *                 else the dash step func_8C0AF280
 *  func_8C0AF280  dash step: pick the nearest target in front of the player
 *                 (partner player, then the target list), turn towards it
 *                 and set the speed along the direction to it
 *
 * Sonic's scratch words used here: pwp->free.w[2] (0x80) dash state
 * (-1 searching, 0 locked on, >0 no target, counts frames), pwp->free.w[3]
 * (0x82) dash timer (cleared by func_8C0AF18C; > 180 randomises the speed).
 * equipment bit 0x8000 is Super Sonic (SADX Upgrades_SuperSonic); the field
 * is read as a signed 16-bit value (no EXTU.W before the test).
 */

/* Target list entry: task work + distance (bss 8C73EE38 / 8C742288,
 * terminated by twp == NULL; filled by 8C0334E8 in the CCL code). Same
 * layout as code_8C06CFCC.c. */
typedef struct {
    taskwk *twp;                /* 0x00 */
    Float dist;                 /* 0x04 */
} PL_TARGET;

extern PL_TARGET B_8C73EE38[];  /* player 0 */
extern PL_TARGET B_8C742288[];  /* other players */

extern taskwk *D_8C18B7EC;      /* camera taskwk */

/* create the effect/sound task 8C07B93A with (n, pno, mode) */
void func_8C07BA0A(Sint32 n, Sint8 pno, Sint16 mode);
/* reset the player to the stage start position */
void func_8C04F8FA(taskwk *twp);
/* homing dash start effects (super / normal Sonic) */
void func_8C0B2DD8(taskwk *twp);
void func_8C0B2DB2(taskwk *twp);
/* index of the partner player of pno in playertwp[], -1 if none */
Sint32 func_8C07D18E(Uint8 pno);

void func_8C0AF280(taskwk *twp, motionwk *mwp, playerwk *pwp);

void func_8C0AEFA0(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    Sint32 n;

    if (pwp->spd.x < pwp->p.jog_speed) {
        return;
    }
    if (((Uint16)GetStageNumber() & 0xFF00) == 0x800) {
        if (pwp->attr == 4) {
            n = 0xE1;
        } else if (pwp->attr == 8) {
            n = 0xE3;
        } else {
            n = 0xE2;
        }
    } else {
        n = 0x2B2;
    }
    func_8C07BA0A(n, twp->counter.b[0], twp->mode);
}

/*
 * NON_MATCHING: identical except for one register: the original loads the
 * address of atan2f into R2 (8C0AF132), SHC 5.1r03 picks R3 for this source
 * (tried: temporaries, declaration order/scope, condition forms, operand
 * order, split angle expression; the 0x400 test as "? TRUE : FALSE",
 * "&& 1" or "|| 0" as in Eggman's copy func_8C0DAE96 and Knuckles' early
 * return after the y branch give 149-150 instructions instead of 148).
 * The x/z steps are FMACs (8C0AF168, 8C0AF17C): fused on PC.
 * Only R4 is read; mwp and pwp are passed by SonicMain (8C0A94AE).
 */
#ifdef NON_MATCHING
void func_8C0AF012(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    Float fx;
    Float fy;
    Angle ang;

    if (B_8C754C08[(Uint8)twp->counter.b[0]].press & 2) {
        func_8C04F8FA(twp);
    }
    if (B_8C754C08[(Uint8)twp->counter.b[0]].on & 0x400) {
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
            ang = SA1_FTOI(atan2f(fy, fx) * 10430.381f) - D_8C18B7EC->ang.y;
            SA1_FMAC_ACC(twp->pos.x, njCos(ang), 5.0f);
            SA1_FMAC_ACC(twp->pos.z, njSin(ang), 5.0f);
        }
    }
}
#else
ASM_STUB(void func_8C0AF012(taskwk *twp, motionwk *mwp, playerwk *pwp))
#endif

void func_8C0AF18C(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    PL_TARGET *p;

    if ((Sint16)pwp->equipment & 0x8000) {
        func_8C0B2DD8(twp);
    } else {
        func_8C0B2DB2(twp);
    }
    pwp->free.w[2] = -1;
    pwp->free.w[3] = 0;
    func_8C0940DA(0x2D3, 0, 0, 0);
    if (twp->counter.b[0] == 0) {
        p = B_8C73EE38;
    } else {
        p = B_8C742288;
    }
    if (p->twp == NULL && func_8C07D18E(twp->counter.b[0]) == -1) {
        if ((Sint16)pwp->equipment & 0x8000) {
            pwp->spd.x = 10.0f;
        } else {
            pwp->spd.x = 5.0f;
        }
        pwp->free.w[2] = 1;
        return;
    }
    func_8C0AF280(twp, mwp, pwp);
    if (pwp->free.w[2] != 0) {
        pwp->spd.x = 5.0f;
    }
}

/*
 * NON_MATCHING: same instructions and control flow, different register
 * allocation and tail placement. The original keeps 0x20 in R8 (attr test)
 * and does not keep the address of atan2f in a register; with this source
 * SHC puts atan2f in R8 and tests the attribute with TST #32,R0. The two
 * identical PGetAcceleration/PGetSpeed tails are cross-jumped into the
 * first one in the original (8C0AF2E2) and into the second one here; with a
 * goto into the first tail (one copy) mwp loses R9 instead. Nesting the
 * loop's atan2f inside the DiffAngle argument gives the original register
 * assignment but evaluates the equipment test before atan2f.
 */
#ifdef NON_MATCHING
void func_8C0AF280(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_VECTOR v;
    PL_TARGET *p;
    taskwk *ttwp;
    Float dist;
    Float spd;
    Sint32 n;
    Angle ang;
    CCL_INFO *info;

    if (twp->counter.b[0] == 0) {
        p = B_8C73EE38;
    } else {
        p = B_8C742288;
    }
    if ((pwp->free.w[2] > 0 || p->twp == NULL) && func_8C07D18E(twp->counter.b[0]) == -1) {
        pwp->free.w[2]++;
        pwp->spd.x *= 0.9f;
        PGetAcceleration(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        return;
    }
    if ((n = func_8C07D18E(twp->counter.b[0])) != -1) {
        ttwp = playertwp[n];
        v.x = twp->pos.x - ttwp->pos.x;
        v.y = twp->pos.y - ttwp->pos.y;
        v.z = twp->pos.z - ttwp->pos.z;
        dist = njScalor2(&v);
        if (DiffAngle(twp->ang.y, SA1_FTOI(atan2f(-v.z, -v.x) * 10430.381f)) > 0x5000 || dist >= 10000.0f) {
            ttwp = NULL;
            dist = 10000.0f;
        }
    } else {
        ttwp = NULL;
        dist = 10000.0f;
    }
    for (; p->twp != NULL; p++) {
        info = CWP(p->twp)->info;
        v = info->center;
        if (!(info->attr & 0x20)) {
            njAddVector(&v, &p->twp->pos);
        }
        njSubVector(&v, &twp->pos);
        ang = SA1_FTOI(atan2f(v.z, v.x) * 10430.381f);
        if (((Sint16)pwp->equipment & 0x8000) || DiffAngle(twp->ang.y, ang) <= 0x5000) {
            if (dist > p->dist) {
                dist = p->dist;
                ttwp = p->twp;
            }
        }
    }
    if (ttwp == NULL) {
        pwp->free.w[2] = 1;
        PGetAcceleration(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        return;
    }
    info = CWP(ttwp)->info;
    v = info->center;
    if (!(info->attr & 0x20)) {
        njAddVector(&v, &ttwp->pos);
    }
    njSubVector(&v, &twp->pos);
    ang = SA1_FTOI(atan2f(v.z, v.x) * 10430.381f) & 0xFFFF;
    twp->ang.y = AdjustAngle(twp->ang.y, ang, ((Sint16)pwp->equipment & 0x8000) ? 0x1000 : 0x800);
    if (v.y > 0.0f && pwp->free.w[2] < 0) {
        v.y = 0.0f;
        pwp->free.w[2] = 1;
    }
    njUnitVector(&v);
    v.x = njCos(twp->ang.y) * njSqrt(1.0f - v.y * v.y);
    v.z = njSin(twp->ang.y) * njSqrt(1.0f - v.y * v.y);
    if ((Sint16)pwp->equipment & 0x8000) {
        spd = 10.0f;
    } else {
        spd = 5.0f;
    }
    if (pwp->free.w[3] > 180) {
        spd *= SA1_FMAC((Float)rand(), 3.05175765e-6f, 0.7f);
    }
    v.x *= spd;
    v.y *= spd;
    v.z *= spd;
    mwp->spd = v;
    PConvertVector_G2P(twp, &v);
    pwp->spd = v;
    if (pwp->free.w[2] < 0) {
        pwp->free.w[2] = 0;
    }
}
#else
ASM_STUB(void func_8C0AF280(taskwk *twp, motionwk *mwp, playerwk *pwp))
#endif
