#include "common.h"
#include "ninja.h"
#include "task.h"
#include "sa1/ninja_types.h"    /* NJS_SPRITE (njDrawSprite3D argument) */
#include "sa1/ninja_funcs.h"    /* njPushMatrix, njRotateX/Z, njCalcVector/Point, njPopMatrix, njSin/njCos, njGetMatrix */

/* Collision work (taskwk.cwp) */
typedef struct colliwk {
    Uint16 id;                  /* 0x00 */
    Uint16 flag;                /* 0x02 */
} colliwk;

/* Player work (func_8C07CFDE) */
typedef struct {
    Sint16 unk0;                /* 0x00 */
    Sint16 unk2;                /* 0x02 */
    Sint16 unk4;                /* 0x04 */
    Uint16 equipment;           /* 0x06 */
} UNK_pwk;

/* Dropped ring work (task.awp) */
typedef struct {
    Float ground;               /* 0x00 shadow / ground height */
    Float timer;                /* 0x04 ground check countdown */
} UNK_ringawp;

extern Sint32 D_8C1BF0F8;
extern Uint8 D_8C1BF0BC[];
extern NJS_SPRITE B_8C79897C;
extern NJS_MATRIX D_8C67C640;

extern void CCL_Init(task *tp, void *info, Uint8 id, Sint32 unused, Uint8 nbInfo); /* sa1/ccl.h (CCL_INFO *info) */
extern void func_8C08776C(task *tp);
extern Sint32 rand(void); /* rand() */
extern UNK_pwk *func_8C07CFDE(Uint8 pno); /* sa1/player.h (playerwk *) */
extern void func_8C08A198(Sint16 n);
extern void func_8C0940DA(Sint32 a, Sint32 b, Sint32 c, Sint32 d);
extern Float func_8C091C20(Float x, Float y, Float z, Angle3 *ang); /* sa1/engine.h */
extern Float atan2f(Float y, Float x); /* atan2f */
extern void func_8C02F63C(taskwk *twp);
extern void func_8C0876F4(task *tp);
extern void func_8C086DE0(task *tp);
extern Sint32 func_8C08A71E(void);
extern void func_8C087738(task *tp);
extern void DestroyTask(task *tp);

void func_8C08774C(taskwk *twp, NJS_SPRITE *sp);
void func_8C0877A0(task *tp, Float y, Float sx, Float sz);

ASM_STUB(void func_8C086DE6(taskwk *twp))

#ifdef NON_MATCHING
/*
 * Dropped (scattered) ring. Notes for the libsa1 build (checked against the asm):
 * - the five position updates are FMAC in the original (SA1_FMAC);
 * - every float compare follows the asm FCMP/GT operand order, so NaN takes the
 *   same branch as on the SH-4;
 * - sx/sz are not set on the "value < 0.2, timer < 13" path (the common case:
 *   the ring settles after its bounces). The original then stores whatever FR5/FR4
 *   hold: the constants -2.0/-4.0 loaded at entry (shared with the hit branches),
 *   or, after njRotateZ / njRotateX (current matrix), the new matrix row 1 / row 2
 *   elements 1 and 0 that those Ninja calls leave in FR5/FR4 (they transform the
 *   row with FTRV in FV4 and copy it to XMTRX). The PC build reproduces both;
 *   only the frame on which func_8C091C20 ran with no rotation after it (its own
 *   leftovers stay in FR4/FR5) is not reproduced.
 */
void func_8C0870EA(task *tp)
{
    NJS_VECTOR v;
    NJS_VECTOR w;
    NJS_VECTOR vd;
    NJS_VECTOR vs;
    taskwk *twp;
    UNK_ringawp *awp;
    Float sx;
    Float sz;
    volatile Float pz;
    volatile Float py;
    volatile Float px;
    volatile Float y;
    volatile Angle ang;
#ifdef TARGET_PC
    Sint32 mrow;
    NJS_MATRIX m;
#endif

    PC_ONLY(sx = -2.0f;)  /* FR5 at entry (the original reads it uninitialised) */
    PC_ONLY(sz = -4.0f;)  /* FR4 at entry */
    PC_ONLY(mrow = 0;)
    twp = tp->twp;
    awp = tp->awp;
    switch (twp->mode) {
    case 0:
        awp->ground = twp->pos.y + -100.0f;
        awp->timer = (Float)D_8C1BF0F8;
        if (++D_8C1BF0F8 > 8) {
            D_8C1BF0F8 = 0;
        }
        CCL_Init(tp, D_8C1BF0BC, 7, 1, 1);
        tp->dest = func_8C08776C;
        twp->mode = 1;
        twp->pos.y += 10.0f;
        twp->scl.x = 0.29999999f;
        twp->scl.z = 0.29999999f;
        twp->scl.y = 4.6f;
        twp->value.f = 1.0f;
        twp->counter.f = (Float)SA1_FTOI((Float)rand() * 1.9999999f);
        twp->wtimer = 0;
        twp->smode = 0;
        twp->timer.f = 1.0f;
        twp->id = 11;
        break;
    case 1:
        if (((colliwk *)twp->cwp)->flag & 2) {
            ((colliwk *)twp->cwp)->flag &= 0xFFFD;
            twp->counter.f = 0.0f;
            twp->scl.x = -2.0f;
            twp->scl.z = -4.0f;
            func_8C08774C(twp, &B_8C79897C);
            twp->pos.y += 3.4399999f;
            twp->mode = 4;
            if (!(func_8C07CFDE(0)->equipment & 0x4000)) {
                func_8C08A198(1);
                func_8C0940DA(7, 0, 0, 0);
            }
            break;
        }
        awp->timer -= 1.0f;
        if (0.0f > awp->timer) {
            awp->timer = 8.0f;
            awp->ground = func_8C091C20(twp->pos.x, twp->pos.y + 6.8799998f, twp->pos.z, &twp->ang);
            if (awp->ground == -1000000.0f) {
                awp->ground = twp->pos.y + -100.0f;
            }
        }
        twp->wtimer++;
        twp->scl.x = njSin(twp->ang.y) * 0.29999999f;
        twp->scl.z = njCos(twp->ang.y) * 0.29999999f;
        njPushMatrix(D_8C67C640);
        if ((ang = twp->ang.z) != 0) {
            njRotateZ(NULL, ang);
            PC_ONLY(mrow = 4;)  /* FR4/FR5 = new XF4/XF5 */
        }
        if ((ang = twp->ang.x) != 0) {
            njRotateX(NULL, ang);
            PC_ONLY(mrow = 8;)  /* FR4/FR5 = new XF8/XF9 */
        }
        if (!(0.19999999f > twp->value.f)) {
            twp->pos.x = SA1_FMAC(twp->value.f, twp->scl.x, twp->pos.x);
            twp->pos.z = SA1_FMAC(twp->value.f, twp->scl.z, twp->pos.z);
        } else {
#ifdef TARGET_PC
            if (mrow != 0) {
                njGetMatrix(m);
                sx = m[mrow + 1];
                sz = m[mrow];
            }
#endif
            twp->timer.f += 0.08f;
            if (2.3999999f > twp->timer.f) {
                twp->pos.x = SA1_FMAC(0.29999999f / twp->timer.f, twp->scl.x, twp->pos.x);
                twp->pos.z = SA1_FMAC(0.29999999f / twp->timer.f, twp->scl.z, twp->pos.z);
            } else if (!(13.0f > twp->timer.f)) {
                v.x = 0.0f;
                v.y = 6.8799998f;
                v.z = 0.0f;
                njCalcVector(NULL, &v, &w);
                sx = w.x;
                sz = w.z;
            }
            twp->mode = 2;
            twp->value.f = awp->ground;
            twp->scl.x = sx;
            twp->scl.z = sz;
            njPopMatrix(1);
            break;
        }
        if (!(0.0f > twp->scl.y)) {
            twp->pos.y = SA1_FMAC(0.33333332f, twp->scl.y - (Float)twp->wtimer * 0.33333332f,
                                  twp->pos.y);
        } else {
            twp->pos.y = awp->ground;
        }
        v.x = 0.0f;
        v.y = 3.4399999f;
        v.z = 0.0f;
        njCalcVector(NULL, &v, &w);
        sx = w.x;
        sz = w.z;
        if (!(twp->pos.y > awp->ground)) {
            twp->value.f -= 0.04f;
            twp->scl.y -= 0.27999998f;
            twp->wtimer = 0;
        }
        v.x = 0.0f;
        v.y = 1.0f;
        v.z = 0.0f;
        njCalcVector(NULL, &v, &w);
        if (0.7f > w.y) {
            vs.x = twp->scl.x;
            vs.y = 1.0f;
            vs.z = twp->scl.z;
            njCalcPoint(NULL, &vs, &vd);
            twp->ang.y = SA1_FTOI(atan2f(vd.x, vd.z) * 10430.381f);
        }
        njPopMatrix(1);
        twp->smode++;
        if (twp->smode > 46) {
            twp->smode = 47;
            y = twp->pos.y;
            twp->pos.y += 3.4399999f;
            func_8C02F63C(twp);
            twp->pos.y = y;
        }
        func_8C0877A0(tp, awp->ground, sx, sz);
        tp->disp = func_8C0876F4;
        break;
    case 2:
        if (((colliwk *)twp->cwp)->flag & 2) {
            ((colliwk *)twp->cwp)->flag &= 0xFFFD;
            twp->mode = 4;
            twp->counter.f = 0.0f;
            twp->scl.x = -2.0f;
            twp->scl.z = -4.0f;
            func_8C08774C(twp, &B_8C79897C);
            twp->pos.y += 3.4399999f;
            if (!(func_8C07CFDE(0)->equipment & 0x4000)) {
                func_8C08A198(1);
                func_8C0940DA(7, 0, 0, 0);
            }
            tp->disp = func_8C086DE0;
            break;
        }
        py = twp->pos.y;
        px = twp->pos.x;
        pz = twp->pos.z;
        twp->pos.y += 3.4399999f;
        twp->pos.x += twp->scl.x;
        twp->pos.z += twp->scl.z;
        func_8C02F63C(twp);
        twp->pos.y = py;
        twp->pos.x = px;
        twp->pos.z = pz;
        twp->wtimer++;
        if (func_8C08A71E() & 2) {
            func_8C0877A0(tp, twp->value.f, twp->scl.x, twp->scl.z);
        }
        if (twp->wtimer >= 100) {
            twp->mode = 3;
        }
        tp->disp = func_8C087738;
        break;
    case 4:
        func_8C086DE6(twp);
        break;
    case 3:
        DestroyTask(tp);
        break;
    }
}
#else
ASM_STUB(void func_8C0870EA(task *tp))
#endif

ASM_STUB(void func_8C08774C(taskwk *twp, NJS_SPRITE *sp))
ASM_STUB(void func_8C0877A0(task *tp, Float y, Float sx, Float sz))
