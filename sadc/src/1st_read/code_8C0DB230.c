/*
 * 1ST_READ 8C0DB230-8C0DB67E (chunk 8C0D6B40-8C0DCA6C): Amy mode helpers.
 *
 *  func_8C0DB230  button D_8C19E38C pressed: mode 22, action 95, SE 0x492,
 *                 pad vibration (AmyModeMachine)
 *  func_8C0DB28E  button D_8C19E38C pressed while free.b[6] bit 0: snap to
 *                 the target record func_8C0DFF4A (&B_8C7A40AC), mode 15,
 *                 action 33 (AmyModeMachine)
 *  func_8C0DB380  free.b[6] bit 0 clear or button D_8C19E38C/D_8C19E390
 *                 pressed: func_8C0DA6E0 (back to mode 1) (AmyModeMachine)
 *  func_8C0DB3BA  free.b[6] bit 1: level the body, random tilt speed
 *                 (free.w[6]/w[7]), mode 16, action 35 (AmyModeMachine)
 *  func_8C0DB430  tilt from the stick: free.w[6]/w[7] integrate ang.x/z,
 *                 clamped to +-0x400; action 36 sets work.f (AmyMain)
 *  func_8C0DB5E4  tilt beyond +-0x3800: fall off (mode 8, action 17)
 *                 (AmyModeMachine)
 */
#include "common.h"
#include "sa1/sa1.h"

/*
 * Record at B_8C7A40AC returned by func_8C0DFF4A (chunk 8C0DFE60).
 * func_8C0DFEDC(pos, ang) fills it: flag = 0/1 (func_8C0DDE1C(0) == 4),
 * flag2 = 0/1 and ang2 from the stick (func_8C0DFE86), ang, pos.
 */
typedef struct {
    Sint32 flag;                /* 0x00 */
    Sint32 flag2;               /* 0x04 */
    Angle ang2;                 /* 0x08 */
    Angle ang;                  /* 0x0C */
    NJS_POINT3 pos;             /* 0x10 */
} AMY_TARGETWK;                 /* 0x1C */

SA1_ASSERT_SIZE(AMY_TARGETWK, 0x1C);

extern AMY_TARGETWK *func_8C0DFF4A(void);                    /* returns &B_8C7A40AC */
extern void func_8C109EAC(Sint32 pno, Sint32 a, Sint32 b, Sint32 c); /* pad vibration */

/* "acc += a * b" compiled to FMAC (the address of acc is taken before the
 * call in a): fused on PC (SA1_FMAC); SA1_FMAC itself (a * b + acc) changes
 * the SHC output of func_8C0DB5E4 */
#if defined(TARGET_PC)
#define FMAC_ACC(acc, a, b) ((acc) = SA1_FMAC((a), (b), (acc)))
#else
#define FMAC_ACC(acc, a, b) ((acc) += (a) * (b))
#endif

ASM_STUB(void func_8C0DA6E0(taskwk *twp, motionwk *mwp, playerwk *pwp))

Sint32 func_8C0DB230(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    if (B_8C754C08[(Uint8)twp->counter.b[0]].press & D_8C19E38C) {
        pwp->free.b[7] = 0;
        twp->mode = 22;
        pwp->mj.reqaction = 95;
        func_8C0940DA(0x492, 0, 0, 0);
        func_8C109EAC((Uint8)twp->counter.b[0], 3, 15, 0);
        return 1;
    }
    return 0;
}

Sint32 func_8C0DB28E(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    AMY_TARGETWK *tgt;

    if ((B_8C754C08[(Uint8)twp->counter.b[0]].press & D_8C19E38C) && (pwp->free.b[6] & 1)) {
        if ((tgt = func_8C0DFF4A()) != NULL) {
            twp->ang.y = (Uint16)(0xC000 - tgt->ang);
            twp->pos.x = SA1_FMAC(6.0f, njSin(tgt->ang), tgt->pos.x);
            twp->pos.z = SA1_FMAC(6.0f, njCos(tgt->ang), tgt->pos.z);
        }
        twp->mode = 15;
        pwp->mj.reqaction = 33;
        return 1;
    }
    return 0;
}

Sint32 func_8C0DB380(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    if (!(pwp->free.b[6] & 1)
        || (B_8C754C08[(Uint8)twp->counter.b[0]].press & (D_8C19E390 | D_8C19E38C))) {
        func_8C0DA6E0(twp, mwp, pwp);
        return 1;
    }
    return 0;
}

Sint32 func_8C0DB3BA(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    Angle ang;

    if (pwp->free.b[6] & 2) {
        twp->ang.x = 0;
        twp->ang.z = 0;
        ang = SA1_FTOI((Float)rand() * 2.0f);
        pwp->free.w[6] = (Sint16)SA1_FTOI(njSin(ang) * 64.0f);
        pwp->free.w[7] = (Sint16)SA1_FTOI(njCos(ang) * 64.0f);
        twp->mode = 16;
        pwp->mj.reqaction = 35;
        return 1;
    }
    return 0;
}

void func_8C0DB430(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    Float x;
    Float z;
    Float mag;
    Float sx;
    Float sz;
    Angle ang;

    pwp->free.w[6] += (Sint16)twp->ang.x >> 11;
    pwp->free.w[7] += (Sint16)twp->ang.z >> 11;
    x = (Float)(B_8C754C08[(Uint8)twp->counter.b[0]].x1 << 8) * 3.0517578125e-05f;
    z = (Float)(B_8C754C08[(Uint8)twp->counter.b[0]].y1 << 8) * 3.0517578125e-05f;
    mag = njSqrt(SA1_FMAC(x, x, z * z));
    if (mag > 0.099999996f) {
        ang = SA1_FTOI(atan2f(x, z) * 10430.381f) + camera_twp->ang.y + 0x4000;
        pwp->free.w[6] += (Sint16)SA1_FTOI(mag * njSin(ang) * 12.0f);
        pwp->free.w[7] += (Sint16)SA1_FTOI(mag * njCos(ang) * 12.0f);
    }
    if (pwp->free.w[6] < -0x400) {
        pwp->free.w[6] = -0x400;
    } else if (pwp->free.w[6] > 0x400) {
        pwp->free.w[6] = 0x400;
    }
    if (pwp->free.w[7] < -0x400) {
        pwp->free.w[7] = -0x400;
    } else if (pwp->free.w[7] > 0x400) {
        pwp->free.w[7] = 0x400;
    }
    twp->ang.x += pwp->free.w[6];
    twp->ang.z += pwp->free.w[7];
    if (pwp->mj.reqaction == 36) {
        sx = njSin(twp->ang.x);
        sz = njSin(twp->ang.z);
        pwp->work.f = SA1_FMAC(1.5f, njSqrt(SA1_FMAC(sx, sx, sz * sz)), 0.5f);
    }
}

Sint32 func_8C0DB5E4(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    if (twp->ang.x < -0x3800 || twp->ang.x > 0x3800
        || twp->ang.z < -0x3800 || twp->ang.z > 0x3800) {
        twp->pos.x += -(njSin(twp->ang.z) * njCos(twp->ang.x) * 10.0f);
        twp->pos.y += -2.0f;
        FMAC_ACC(twp->pos.z, njSin(twp->ang.x), 10.0f);
        twp->mode = 8;
        pwp->mj.reqaction = 17;
        return 1;
    }
    return 0;
}
