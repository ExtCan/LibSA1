#include "sa1/sa1.h"

/*
 * E-102 Gamma laser shots (1ST_READ chunk 8C0D3C00-8C0D4B00, functions 1-5
 * of 13):
 *
 *  func_8C0D3C00  nearest valid lock-on marker (func_8C0D46CE, args twp,
 *                 ewk, flag): walks ewk->lock_tp[0..nb_lock-1], counts the
 *                 markers that are free (work unk14 == 0) and still have a
 *                 target task with a taskwk (with flag also mode != 0),
 *                 stores the count in ewk->nb_target and returns the marker
 *                 whose target is nearest to twp->pos (999999.0f start).
 *  func_8C0D3C9A  display of the laser shot task: additive sprite
 *                 D_8C422DA8 (cell 1 with the laser upgrade, wtimer 0x200)
 *                 at twp->pos.
 *  func_8C0D3D0C  dest of the laser shot task: frees its lock-on marker
 *                 (unk14 = 0) and, with flag 0x100, starts func_8C0D65BA at
 *                 the shot position.
 *  func_8C0D3DA0  exec of the laser shot task (twp->value.tp = lock-on
 *                 marker task): mode 0 sets up collision, the SE and the
 *                 homing speed, mode 1 homes on the marker (speed 5.0f,
 *                 snaps when nearer than 16.0f) and registers itself in
 *                 ewk->shot_tp[], mode 2 flies straight until the SET
 *                 manager drops it; every frame it leaves a beam segment
 *                 task (func_8C0D4250) oriented along the speed. Dies after
 *                 600 frames.
 *  func_8C0D4160  display of the beam segment task (func_8C0D4250 sets it
 *                 as disp): scaled laser/targeting beam model.
 */

/* E-102 line draw list (0x20 bytes; kept so E102WK has its full native layout) */
typedef struct {
    NJS_POINT3 *src_p;          /* 0x00 */
    NJS_COLOR *src_col;         /* 0x04 */
    Sint16 *idx;                /* 0x08 */
    Sint16 num;                 /* 0x0C */
    Sint16 pad0E;               /* 0x0E */
    NJS_POINT3COL p3c;          /* 0x10 */
} E102_LINE;                    /* 0x20 */

/*
 * E-102 work (pwp->free.ptr[0]; E102Main 8C0CC0F2: CAlloc(1, 0x244)).
 * Layout merged from code_8C0D2EC0.c / code_8C0CFACE.c / code_8C0D37F4.c
 * plus the two task lists at 0xF4 and 0x174 (this chunk). Local copy until a
 * shared E102WK exists (see type notes).
 */
typedef struct {
    Sint8 mode;                 /* 0x00 */
    Sint8 smode;                /* 0x01 */
    Sint8 nb_target;            /* 0x02 valid markers (func_8C0D3C00, MOV.B store) */
    Sint8 unk03;                /* 0x03 */
    Sint8 nb_lock;              /* 0x04 entries of lock_tp[] (MOV.B) */
    Sint8 nb_shot;              /* 0x05 entries of shot_tp[] (func_8C0D3DA0, MOV.B ++) */
    Sint16 unk06;               /* 0x06 */
    Sint16 timer08;             /* 0x08 */
    Sint16 timer0A;             /* 0x0A */
    Sint16 timer0C;             /* 0x0C */
    Sint16 unk0E;               /* 0x0E */
    Sint16 unk10;               /* 0x10 */
    Sint16 lockon_timer;        /* 0x12 */
    Sint16 unk14;               /* 0x14 */
    Sint16 flag;                /* 0x16 */
    Float hover_y;              /* 0x18 */
    Sint32 unk1C;               /* 0x1C */
    Angle3 jet_ang;             /* 0x20 */
    Angle jet_phase;            /* 0x2C */
    task *target_tp;            /* 0x30 */
    NJS_POINT3 laser_pos;       /* 0x34 */
    NJS_POINT3 laser_vec;       /* 0x40 */
    Float range;                /* 0x4C */
    Float range2;               /* 0x50 */
    NJS_POINT3 arm_pos;         /* 0x54 */
    NJS_POINT3 arm_vec;         /* 0x60 */
    task *tp;                   /* 0x6C */
    Float arm_frame;            /* 0x70 */
    Sint32 unk74;               /* 0x74 */
    Sint32 unk78;               /* 0x78 */
    Sint32 unk7C;               /* 0x7C */
    NJS_ACTION *arm_action;     /* 0x80 */
    Float scope_frame;          /* 0x84 */
    Angle blink_ang;            /* 0x88 */
    Angle scope_ang;            /* 0x8C */
    Float body_y;               /* 0x90 */
    NJS_POINT3 muzzle[2];       /* 0x94 */
    Sint32 unkAC;               /* 0xAC */
    E102_LINE line0;            /* 0xB0 */
    E102_LINE line1;            /* 0xD0 */
    Sint32 unkF0;               /* 0xF0 */
    task *lock_tp[32];          /* 0xF4 lock-on marker tasks (func_8C0D577C) */
    task *shot_tp[32];          /* 0x174 laser shot tasks of this frame */
    PL_ANALOG analog[8];        /* 0x1F4 stick history */
    Float f234;                 /* 0x234 */
    Sint32 unk238[2];           /* 0x238 */
    Float step_dist;            /* 0x240 */
} E102WK;                       /* 0x244 */

SA1_ASSERT_SIZE(E102_LINE, 0x20);
SA1_ASSERT_SIZE(E102WK, 0x244);
SA1_ASSERT_OFFSET(E102WK, nb_target, 0x02);
SA1_ASSERT_OFFSET(E102WK, nb_lock, 0x04);
SA1_ASSERT_OFFSET(E102WK, nb_shot, 0x05);
SA1_ASSERT_OFFSET(E102WK, line0, 0xB0);
SA1_ASSERT_OFFSET(E102WK, lock_tp, 0xF4);
SA1_ASSERT_OFFSET(E102WK, shot_tp, 0x174);
SA1_ASSERT_OFFSET(E102WK, analog, 0x1F4);

/* awp of the lock-on marker task func_8C0D577C (CAlloc(1, 0x34) by 8C0D38C6;
 * same layout as code_8C0D37F4.c) */
typedef struct {
    Sint8 mode;                 /* 0x00 */
    Sint8 count;                /* 0x01 */
    Sint16 unk02;               /* 0x02 */
    Sint16 equipment;           /* 0x04 player equipment (0x200: laser blaster) */
    Sint16 unk06;               /* 0x06 */
    task *target_tp;            /* 0x08 */
    taskwk *ptwp;               /* 0x0C player taskwk */
    E102WK *ewk;                /* 0x10 */
    Sint32 unk14;               /* 0x14 0: free; cleared by the shot's dest */
    Sint32 unk18[6];            /* 0x18 */
    TaskFuncPtr target_exec;    /* 0x30 */
} E102_LOCKWK;                  /* 0x34 */

SA1_ASSERT_SIZE(E102_LOCKWK, 0x34);
SA1_ASSERT_OFFSET(E102_LOCKWK, equipment, 0x04);
SA1_ASSERT_OFFSET(E102_LOCKWK, target_tp, 0x08);
SA1_ASSERT_OFFSET(E102_LOCKWK, ptwp, 0x0C);
SA1_ASSERT_OFFSET(E102_LOCKWK, unk14, 0x14);

extern Sint8 B_8C7A2EA8;                /* positional SE enable */
extern E102WK *B_8C7A2EBC;              /* E-102 work of this frame */

/* E-102 data (extern only) */
extern NJS_SPRITE D_8C422DA8;           /* laser shot sprite */
extern NJS_ARGB D_8C422DE8;             /* laser shot material */
extern CCL_INFO D_8C422DF8[];           /* laser shot collision (1 shape) */
extern NJS_ARGB D_8C422E2C;             /* beam material */
#if defined(TARGET_PC)
#define D_8C422E30 (D_8C422E2C.r)
#define D_8C422E34 (D_8C422E2C.g)
#define D_8C422E38 (D_8C422E2C.b)
#else
extern Float D_8C422E30;                /* = D_8C422E2C.r (separate symbols in the pool) */
extern Float D_8C422E34;                /* = D_8C422E2C.g */
extern Float D_8C422E38;                /* = D_8C422E2C.b */
#endif
extern NJS_TEXLIST texlist_E102BEAM;
extern NJS_MODEL E_102_Gamma_Laser_Blast;
extern NJS_MODEL E_102_Gamma_Targeting_Beam;

void func_8C056972(void);               /* save draw flags (code_8C056860.c) */
void func_8C05697C(void);               /* restore draw flags (code_8C05697C.c) */
void func_8C0D4250(task *tp);           /* beam segment exec */
void func_8C0D65BA(task *tp);

task *func_8C0D3C00(taskwk *twp, E102WK *ewk, Sint32 flag)
{
    task **list;
    task *found;
    Sint8 count;
    Sint32 n;
    task *tp;
    E102_LOCKWK *lwp;
    Float min;
    Float d;

    count = 0;
    min = 999999.0f;
    list = ewk->lock_tp;
    found = NULL;
    n = ewk->nb_lock;
    for (; n > 0; n--) {
        tp = *list++;
        lwp = (E102_LOCKWK *)tp->awp;
        if (lwp->unk14 != 0) {
            continue;
        }
        if (lwp->target_tp == NULL) {
            continue;
        }
        if (lwp->target_tp->twp == NULL) {
            continue;
        }
        if (flag && lwp->mode == 0) {
            continue;
        }
        count++;
        d = njDistanceP2P(&twp->pos, &lwp->target_tp->twp->pos);
        if (d < min) {
            min = d;
            found = tp;
        }
    }
    ewk->nb_target = count;
    return found;
}

void func_8C0D3C9A(task *tp)
{
    taskwk *twp;
    Sint32 n;

    twp = tp->twp;
    if (!B_8C754EC8) {
        njSetConstantMaterial(&D_8C422DE8);
        njColorBlendingMode(0, 10);
        njColorBlendingMode(1, 10);
        njPushMatrix(NULL);
        n = (twp->wtimer & 0x200) ? 1 : 0;
        njTranslateV(NULL, &twp->pos);
        njSetTexture(&texlist_E102BEAM);
        njDrawSprite3D(&D_8C422DA8, n, 0x30);
        njPopMatrix(1);
    }
}

void func_8C0D3D0C(task *tp)
{
    taskwk *twp;
    task *ltp;
    E102_LOCKWK *lwp;
    task *ntp;

    twp = tp->twp;
    ltp = twp->value.tp;
    if (ltp != NULL) {
        lwp = (E102_LOCKWK *)ltp->awp;
        lwp->unk14 = 0;
    }
    if (twp->flag & 0x100) {
        ntp = CreateElementalTask(2, 2, func_8C0D65BA);
        if (ntp != NULL) {
            ntp->twp->pos = twp->pos;
        }
    }
}

void func_8C0D3DA0(task *tp)
{
    taskwk *twp;
    motionwk *mwp;
    task *target;
    E102_LOCKWK *lwp;
    taskwk *ptwp;
    task *ntp;
    taskwk *ntwp;
    Sint32 se;
    Float d;
    Float f;
    NJS_POINT3 v;

    twp = tp->twp;
    mwp = tp->mwp;
    target = twp->value.tp;
    twp->ang.x++;
    if (twp->ang.x > 600) {
        FreeTask(tp);
        return;
    }
    switch (twp->mode) {
    case 0:
        if (target == NULL) {
            FreeTask(tp);
            return;
        }
        twp->scl.x = 1.0f;
        mwp->spd.x *= 5.0f;
        mwp->spd.y *= 5.0f;
        mwp->spd.z *= 5.0f;
        twp->timer.f = 0.0f;
        CCL_Init(tp, D_8C422DF8, 1, 1, 1);
        lwp = (E102_LOCKWK *)target->awp;
        /* the original reads lwp->ptwp before the NULL check (harmless on
         * the DC, a NULL dereference on PC) */
        SH_ONLY(ptwp = lwp->ptwp;)
        if (lwp == NULL) {
            FreeTask(tp);
            return;
        }
        PC_ONLY(ptwp = lwp->ptwp;)
        if (ptwp == NULL) {
            ptwp = playertwp[0];
        }
        twp->counter.ptr = ptwp;
        twp->wtimer = lwp->equipment;
        se = (twp->wtimer & 0x200) ? 0x318 : 0x317;
        if (B_8C7A2EA8) {
            func_8C09413A(se, 0, 0, 0, twp->pos.x, twp->pos.y, twp->pos.z);
        }
        tp->disp = func_8C0D3C9A;
        tp->dest = func_8C0D3D0C;
        v = target->twp->pos;
        njSubVector(&v, &twp->pos);
        d = njScalor(&v);
        f = 5.0f / d;
        if (d < 100.0f) {
            if (d >= 5.0f) {
                v.x *= f;
                v.y *= f;
                v.z *= f;
            }
            mwp->spd = v;
        } else {
            njUnitVector(&v);
            mwp->spd.x = v.x * f;
            mwp->spd.y = v.y * f;
            mwp->spd.z = v.z * f;
        }
        twp->mode = 1;
        break;
    case 1:
        if (target == NULL) {
            FreeTask(tp);
            return;
        }
        v = target->twp->pos;
        njSubVector(&v, &twp->pos);
        d = njScalor(&v);
        if (d >= 16.0f) {
            if (d >= 100.0f) {
                f = twp->timer.f / d;
                v.x *= f;
                v.y *= f;
                v.z *= f;
                njAddVector(&mwp->spd, &v);
            } else {
                mwp->spd = v;
            }
            njUnitVector(&mwp->spd);
            mwp->spd.x *= 5.0f;
            mwp->spd.y *= 5.0f;
            mwp->spd.z *= 5.0f;
            njAddVector(&twp->pos, &mwp->spd);
        } else {
            twp->pos = target->twp->pos;
        }
        B_8C7A2EBC->shot_tp[B_8C7A2EBC->nb_shot++] = tp;
        break;
    case 2:
        if (func_8C0919CC(tp)) {
            FreeTask(tp);
            return;
        }
        njAddVector(&twp->pos, &mwp->spd);
        break;
    }
    ntp = CreateElementalTask(2, 2, func_8C0D4250);
    if (ntp != NULL) {
        ntwp = ntp->twp;
        ntwp->pos = twp->pos;
        ntwp->ang.y = 0x8000 - SA1_FTOI(atan2f(mwp->spd.z, mwp->spd.x) * 10430.381f);
        ntwp->ang.z = -SA1_FTOI(atan2f(mwp->spd.y, njSqrt(SA1_FMAC(mwp->spd.x, mwp->spd.x, mwp->spd.z * mwp->spd.z))) * 10430.381f);
        ntwp->smode = (twp->timer.f == 0.0f);
        ptwp = (taskwk *)twp->counter.ptr;
        if (ptwp == NULL) {
            ptwp = playertwp[0];
        }
        ntwp->timer.b[0] = ptwp->counter.b[0];
        ntwp->wtimer = twp->wtimer;
    }
    /* max(timer + 0.5, 5.0), spelled as a MAX()-style macro expansion */
    twp->timer.f = (twp->timer.f + 0.5f > 5.0f) ? twp->timer.f + 0.5f : 5.0f;
    func_8C0D3C9A(tp);
    func_8C02F63C(twp);
}

void func_8C0D4160(task *tp)
{
    taskwk *twp;
    Uint32 ang;
    Float sy;
    Float one;

    twp = tp->twp;
    if (!B_8C754EC8) {
        func_8C056972();
        func_8C60EDE0(16);
        njPushMatrix(NULL);
        njTranslateV(NULL, &twp->pos);
        (ang = twp->ang.y) ? njRotateY(NULL, ang) : (void)0;
        (ang = twp->ang.z) ? njRotateZ(NULL, ang) : (void)0;
        one = 1.0f;     /* sy/one locals: FR13-FR15 allocation of 2.5/1.25/1.0 */
        if (twp->wtimer & 0x200) {
            sy = 1.25f;
            njScale(NULL, 2.5f, sy, sy);
            D_8C422E30 = twp->counter.f;
            D_8C422E34 = twp->counter.f;
            D_8C422E38 = one;
        } else {
            njScale(NULL, 2.5f, one, one);
            D_8C422E30 = twp->counter.f;
            D_8C422E34 = twp->counter.f;
            D_8C422E38 = 0.0f;
        }
        njSetConstantMaterial(&D_8C422E2C);
        if (twp->smode) {
            njSetTexture(&texlist_E102BEAM);
            func_8C6215D6(&E_102_Gamma_Laser_Blast);
        } else {
            func_8C6215D6(&E_102_Gamma_Targeting_Beam);
        }
        njPopMatrix(1);
        func_8C05697C();
    }
}
