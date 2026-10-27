#include "common.h"
#include "ninja.h"
#include "sa1/sa1.h"

/*
 * 1ST_READ 8C0B4600-8C0B48E0 (chunk asm/1st_read/text/8C0B33A0.src,
 * functions 19-24 of 28): Sonic's jump-ball after-image effects and the
 * set-up of the effect task 8C0B4B20/8C0B4A48/8C0B4E62.
 *
 *  func_8C0B4600  disp (set by 8C0B3FC0): fading ball at pos + scl,
 *                 rotated by ang (z, x, -y)
 *  func_8C0B46CC  exec (set by 8C0B3FC0): draws for 5 frames, following
 *                 the player (scl = player pos, ang = player ang), then
 *                 frees itself and clears tp->disp
 *  func_8C0B4724  disp (8C0B3FDA): fading ball at pos, uniform scale scl.x
 *  func_8C0B4820  exec (8C0B3FDA): draws for 10 frames, shrinking scl.x by
 *                 0.4 per frame, then frees itself
 *  func_8C0B4860  direction vector -> angles (y = yaw, z = pitch, x = 0);
 *                 called by 8C0B4B20 with twp->ang
 *  func_8C0B48B4  initial exec of the task created by 8C0B2C8C (SonicMain):
 *                 exec/disp/dest = 8C0B4B20/8C0B4A48/8C0B4E62 when
 *                 func_8C0B48E0 returns 1, otherwise FreeTask
 *
 * The effect tasks keep the texlist in twp->value.ptr and the frame count
 * in twp->wtimer.
 */

extern void func_8C056926(void);                                        /* save draw flags (code_8C056860.c) */
extern void func_8C056938(void);                                        /* restore draw flags */
extern void func_8C05694A(Uint32 a, Uint32 b);                          /* set draw flags */

extern NJS_MODEL Sonic_Jumpball_Effect;                                 /* 8C25654C */

extern void func_8C0B4A48(task *tp);
extern void func_8C0B4B20(task *tp);
extern void func_8C0B4E62(task *tp);

Sint32 func_8C0B48E0(taskwk *twp);

void func_8C0B4600(task *tp)
{
    taskwk *twp;
    NJS_ARGB argb;

    twp = tp->twp;
    func_8C056926();
    func_8C05694A(0, 0x2100000);
    argb.r = argb.g = argb.b = 0.0f;
    argb.a = -(twp->wtimer * 0.19999999f);
    njSetConstantMaterial(&argb);
    njSetTexture(twp->value.ptr);
    njPushMatrix(NULL);
    njTranslate(NULL, twp->pos.x + twp->scl.x, twp->pos.y + twp->scl.y, twp->pos.z + twp->scl.z);
    twp->ang.z ? (njRotateZ(NULL, twp->ang.z), 0) : 0;
    twp->ang.x ? (njRotateX(NULL, twp->ang.x), 0) : 0;
    -twp->ang.y ? (njRotateY(NULL, -twp->ang.y), 0) : 0;
    func_8C036F12(&Sonic_Jumpball_Effect);
    njPopMatrix(1);
    argb.a = 0.0f;
    njSetConstantMaterial(&argb);
    func_8C056938();
}

void func_8C0B46CC(task *tp)
{
    taskwk *twp;
    taskwk *ptwp;

    twp = tp->twp;
    ptwp = playertwp[(Uint8)twp->counter.b[0]];
    if (twp->wtimer++ >= 5) {
        FreeTask(tp);
        tp->disp = NULL;
    } else {
        func_8C0B4600(tp);
        twp->scl = ptwp->pos;
        twp->ang = ptwp->ang;
    }
}

void func_8C0B4724(task *tp)
{
    taskwk *twp;
    Float s;
    NJS_ARGB argb;

    twp = tp->twp;
    func_8C056926();
    func_8C05694A(0, 0x2100000);
    argb.r = argb.g = argb.b = 0.0f;
    argb.a = -(twp->wtimer * 0.099999996f);
    njSetConstantMaterial(&argb);
    njSetTexture(twp->value.ptr);
    njPushMatrix(NULL);
    njTranslate(NULL, twp->pos.x, twp->pos.y, twp->pos.z);
    s = twp->scl.x;
    njScale(NULL, s, s, s);
    func_8C036F12(&Sonic_Jumpball_Effect);
    njPopMatrix(1);
    argb.a = 0.0f;
    njSetConstantMaterial(&argb);
    func_8C056938();
}

void func_8C0B4820(task *tp)
{
    taskwk *twp;

    twp = tp->twp;
    if (twp->wtimer++ >= 10) {
        FreeTask(tp);
        tp->disp = NULL;
    } else {
        func_8C0B4724(tp);
        twp->scl.x -= 0.39999998f;
    }
}

void func_8C0B4860(NJS_VECTOR *v, Angle3 *ang)
{
    Float r;

    r = njScalor(v);
    if (r != 0.0f) {
        ang->y = SA1_FTOI(atan2f(-v->z, v->x) * 10430.381f);
        ang->z = SA1_FTOI(func_8C10D058(v->y / r) * 10430.381f);
        ang->x = 0;
    }
}

void func_8C0B48B4(task *tp)
{
    if (func_8C0B48E0(tp->twp) == 1) {
        tp->exec = func_8C0B4B20;
        tp->disp = func_8C0B4A48;
        tp->dest = func_8C0B4E62;
    } else {
        FreeTask(tp);
    }
}

ASM_STUB(Sint32 func_8C0B48E0(taskwk *twp))
