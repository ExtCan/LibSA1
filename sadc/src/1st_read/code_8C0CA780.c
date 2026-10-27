/*
 * 1ST_READ 8C0CA780-8C0CAD20 (chunk 8C0CA780): Knuckles dig/dirt effects
 * (texlist_KNU_EFF).
 *  func_8C0CA780  spawn 10 drifting particles (exec func_8C0CA51E, disp
 *                 func_8C0CA680) at pos with random velocities in (-2, 2];
 *                 n is not read (its caller 8C0C8F20 passes R5 = 0)
 *  func_8C0CA9B0  dirt clod exec (tasks created by KnucklesModeMachine and
 *                 func_8C0CB1A0): freed after 48 frames; mode 0 picks a
 *                 floor height, size and flip, mode 1 falls under gravity
 *                 until it passes the floor height, mode 2 sinks
 *  func_8C0CAAF8  dirt clod disp: sprite D_8C353564, camera facing,
 *                 stretched by the vertical speed
 *  func_8C0CAC28  2D sprite D_8C35366C (texlist_KNU_EFF) at a screen point
 *
 * Dirt clod taskwk use: wtimer = age, value.f = floor height,
 * counter.f = size (1..2), timer.f = +-1 (horizontal flip), scl = velocity.
 *
 * Matching notes:
 *  - The original translation unit also contained func_8C0CA51E and
 *    func_8C0CA680 (chunk 8C0CA040, now code_8C0CA51E.c). SHC only keeps
 *    their addresses in the literal pool (instead of callee-saved registers)
 *    in func_8C0CA780 when they are defined in the same file, hence the
 *    ASM_STUBs. They sit at the END of the file so that no pool entries are
 *    pending when func_8C0CA780 starts: the pool then lands where the
 *    original's did (after the JMP in func_8C0CA9B0 and after the BRA in
 *    func_8C0CAAF8, which makes func_8C0CAAF8's early return a BT/S + BRA).
 *  - func_8C0CAAF8: Uint32 ang gives the stack slot (MOV.L Rn,@R15 /
 *    MOV.L @R15,R5) and the ternary form the R2/R3 allocation (same idiom
 *    as code_8C02FD32.c).
 */
#include "common.h"
#include "sa1/sa1.h"
#include "sa1/protos.h"

extern NJS_TEXLIST texlist_KNU_EFF;     /* 8C3538DC */
extern NJS_SPRITE D_8C353564;           /* dirt clod sprite (3D) */
extern NJS_SPRITE D_8C35366C;           /* 2D sprite */

void func_8C0CA51E(task *tp);           /* particle exec (code_8C0CA51E.c; stub at the end) */
void func_8C0CA680(task *tp);           /* particle disp (code_8C0CA51E.c; stub at the end) */

void func_8C0CAAF8(task *tp);

void func_8C0CA780(NJS_POINT3 *pos, Sint32 n)
{
    task *tp;
    Uint8 i;

    for (i = 0; i < 10; i++) {
        if ((tp = CreateElementalTask(2, 6, func_8C0CA51E)) != NULL) {
            tp->twp->pos = *pos;
            tp->twp->scl.x = (1.0f - (Float)rand() * 6.103515625e-05f) * 2.0f;
            tp->twp->scl.y = (1.0f - (Float)rand() * 6.103515625e-05f) * 2.0f;
            tp->twp->scl.z = (1.0f - (Float)rand() * 6.103515625e-05f) * 2.0f;
            tp->disp = func_8C0CA680;
        }
    }
}

void func_8C0CA9B0(task *tp)
{
    taskwk *twp;

    twp = tp->twp;
    if ((Float)++twp->wtimer > 47.9999965f) {
        FreeTask(tp);
        return;
    }
    switch (twp->mode) {
    case 0:
        twp->value.f = twp->pos.y - (Float)rand() * 3.0517578125e-05f;
        twp->counter.f = SA1_FMAC((Float)rand(), 3.0517578125e-05f, 1.0f);
        if ((Float)rand() * 3.0517578125e-05f < 0.5f) {
            twp->timer.f = 1.0f;
        } else {
            twp->timer.f = -1.0f;
        }
        twp->mode = 1;
        tp->disp = func_8C0CAAF8;
        return;
    case 1:
        twp->scl.y += -0.19999999f;
        twp->pos.x += twp->scl.x;
        twp->pos.y += twp->scl.y;
        twp->pos.z += twp->scl.z;
        if (twp->pos.y < twp->value.f) {
            twp->mode = 2;
        }
        break;
    case 2:
        twp->pos.y += -0.19999999f;
        break;
    }
    func_8C0CAAF8(tp);
    func_8C0919CC(tp);
}

void func_8C0CAAF8(task *tp)
{
    taskwk *twp;
    Float s;
    Float sx;
    Float sy;
    Uint32 ang;

    twp = tp->twp;
    if (B_8C754EC8 != 0) {
        return;
    }
    njColorBlendingMode(0, 8);
    njColorBlendingMode(1, 6);
    njSetTexture(&texlist_KNU_EFF);
    njPushMatrix(NULL);
    s = twp->scl.y * 0.25f;
    if (s < -0.25f) {
        s = -0.25f;
    } else if (s > 0.25f) {
        s = 0.25f;
    }
    sx = (1.0f - s) * twp->counter.f * twp->timer.f;
    sy = (s + 1.0f) * twp->counter.f;
    njTranslateV(NULL, &twp->pos);
    (ang = camera_twp->ang.y) ? njRotateY(NULL, ang) : (void)0;
    (ang = camera_twp->ang.x + 0x8000) ? njRotateX(NULL, ang) : (void)0;
    njScale(NULL, sx, sy, sx);
    njDrawSprite3D(&D_8C353564, 0, 32);
    njPopMatrix(1);
}

void func_8C0CAC28(NJS_POINT2 *p, Int n, Float scl, Float a)
{
    if (B_8C754EC8 != 0) {
        return;
    }
    func_8C043A20(a, 1.0f, 1.0f, 1.0f);
    njColorBlendingMode(0, 1);
    D_8C35366C.p.x = p->x;
    D_8C35366C.p.y = p->y;
    D_8C35366C.sx = scl;
    D_8C35366C.sy = scl;
    njSetTexture(&texlist_KNU_EFF);
    njDrawSprite2D(&D_8C35366C, n, SA1_FMAC(0.01f, scl, -2.0f), 34);
    func_8C043A20(0.0f, 0.0f, 0.0f, 0.0f);
}

ASM_STUB(void func_8C0CA51E(task *tp))
ASM_STUB(void func_8C0CA680(task *tp))
