/*
 * 1ST_READ 8C0DF760-8C0DFCBC (chunk 8C0DF760): two Amy effects
 * (texlist_AMY_EFF).
 *
 * Ring of three sprites around player 0 (exec func_8C0DF90C, created by
 * AmyModeMachine):
 *  func_8C0DF760  disp: three sprites D_8C4BF0C0 at 120 degree steps from
 *                 twp->ang.y, radius 3 around player 0 and 12 above it,
 *                 billboarded to the camera and spun by -4 * angle; each is
 *                 drawn twice (additive, then with alpha |sin(angle)|)
 *  func_8C0DF8C4  exec while fading: shrinks the shared sprite by an
 *                 accelerating step twp->scl.x and frees the task below 0.1
 *  func_8C0DF90C  exec: while func_8C0DDE1C(0) == 9 spins the ring and
 *                 pulses the sprite scale (1 + 0.3 sin); otherwise starts
 *                 the fade (func_8C0DF8C4)
 *
 * Expanding ring of quads (exec func_8C0DFAF8):
 *  func_8C0DF9F8  disp: twelve copies of the polygon D_8C4BF120 rotated
 *                 about Y in 0x1555 steps, scaled by twp->scl
 *  func_8C0DFAF8  exec: life twp->counter.f 1 -> 0 over 30 frames; grows
 *                 scl.x/scl.z by 0.5 per frame, scl.y = 3 sin(life * 0x8000),
 *                 fades the vertex colours B_8C7A409C from D_8C4BF110
 */
#include "common.h"
#include "sa1/sa1.h"

extern NJS_TEXLIST texlist_AMY_EFF;     /* 8C4BF3E4 */
extern NJS_SPRITE D_8C4BF0C0;           /* ring sprite (texlist_AMY_EFF, anim D_8C4BF098) */
extern NJS_POINT3COL D_8C4BF120[];      /* {D_8C4BF0E0, B_8C7A409C, NULL, 4}; must be an array
                                         * (&D_8C4BF120 changes SHC's register choice in func_8C0DF9F8) */
extern NJS_COLOR D_8C4BF110[4];         /* source colours (0x00FFFFFF) */
extern NJS_COLOR B_8C7A409C[4];         /* vertex colours of D_8C4BF120 */

Sint32 func_8C0DDE1C(Sint32 pno);       /* code_8C0DDA00.c */

/* NON_MATCHING: everything matches except the register allocation and
 * order of the first three loads (twp/playertwp[0] spilled to the stack):
 * the original loads &playertwp first and keeps twp in R1, ptwp in R2.
 * Swapping the two assignments fixes that but reshuffles the loop's cached
 * function addresses (R8-R13). The f temporary and the if-block around the
 * body are needed for the rest to match. */
#ifdef NON_MATCHING
void func_8C0DF760(task *tp)
{
    NJS_POINT3 pos;
    taskwk *twp;
    taskwk *ptwp;
    Angle ang;
    Float f;

    twp = tp->twp;
    ptwp = playertwp[0];
    if (B_8C754EC8 == 0) {
        njColorBlendingMode(0, 8);
        njColorBlendingMode(1, 6);
        njSetTexture(&texlist_AMY_EFF);
        for (ang = twp->ang.y; ang < twp->ang.y + 0x10000; ang += 0x5555) {
            njPushMatrix(NULL);
            pos = ptwp->pos;
            pos.x = SA1_FMAC(3.0f, njSin(ang), pos.x);
            pos.z = SA1_FMAC(3.0f, njCos(ang), pos.z);
            pos.y += 12.0f;
            njTranslateV(NULL, &pos);
            njPushMatrix(NULL);
            camera_twp->ang.y ? njRotateY(NULL, camera_twp->ang.y) : (void)0;
            camera_twp->ang.x ? njRotateX(NULL, camera_twp->ang.x) : (void)0;
            /* (-ang) << 2: NEG before SHLL2; -ang * 4 negates afterwards */
            (-ang << 2) != 0 ? njRotateZ(NULL, -ang << 2) : (void)0;
            njDrawSprite3D(&D_8C4BF0C0, 0, 0x28);
            njPopMatrix(1);
            f = _builtin_fabsf(njSin(ang));
            func_8C043A20(f, 1.0f, 1.0f, 1.0f);
            njColorBlendingMode(0, 1);
            njDrawSprite3D(&D_8C4BF0C0, 1, 0x32);
            func_8C043A20(0.0f, 0.0f, 0.0f, 0.0f);
            njPopMatrix(1);
        }
    }
}
#else
ASM_STUB(void func_8C0DF760(task *tp))
#endif

void func_8C0DF8C4(task *tp)
{
    taskwk *twp;

    twp = tp->twp;
    if (playertwp[0] == NULL) {
        FreeTask(tp);
        return;
    }
    twp->scl.x -= 0.024999999f;
    D_8C4BF0C0.sx += twp->scl.x;
    if ((D_8C4BF0C0.sy += twp->scl.x) < 0.099999996f) {
        FreeTask(tp);
        return;
    }
    func_8C0DF760(tp);
}

void func_8C0DF90C(task *tp)
{
    taskwk *twp;
    Float f;

    twp = tp->twp;
    if (playertwp[0] == NULL) {
        FreeTask(tp);
        return;
    }
    tp->disp = func_8C0DF760;
    if (func_8C0DDE1C(0) != 9) {
        twp->scl.x = 0.19999999f;
        func_8C0DF8C4(tp);
        tp->exec = func_8C0DF8C4;
        return;
    }
    twp->ang.y += 0x200;
    f = SA1_FMAC(0.29999999f, njSin(twp->ang.y * 4), 1.0f);
    D_8C4BF0C0.sx = f;
    D_8C4BF0C0.sy = f;
    func_8C0DF760(tp);
}

void func_8C0DF9F8(task *tp)
{
    taskwk *twp;
    Angle ang;

    twp = tp->twp;
    if (B_8C754EC8 != 0) {
        return;
    }
    njColorBlendingMode(0, 10);
    njColorBlendingMode(1, 10);
    njPushMatrix(NULL);
    njTranslateV(NULL, &twp->pos);
    twp->ang.z ? njRotateZ(NULL, twp->ang.z) : (void)0;
    twp->ang.x ? njRotateX(NULL, twp->ang.x) : (void)0;
    /* twelve steps: 12 * 0x1555 = 0xFFFC (SHC unrolls the loop 3x) */
    for (ang = 0; ang < 0xFFFC; ang += 0x1555) {
        njPushMatrix(NULL);
        ang ? njRotateY(NULL, ang) : (void)0;
        njScaleV(NULL, &twp->scl);
        func_8C63D414(D_8C4BF120, 4, 0x60);
        njPopMatrix(1);
    }
    njPopMatrix(1);
}

void func_8C0DFAF8(task *tp)
{
    taskwk *twp;
    Uint16 i;

    twp = tp->twp;
    if (twp->mode == 0) {
        twp->scl.x = 3.0f;
        twp->counter.f = 1.0f;
        twp->mode = 1;
        tp->disp = func_8C0DF9F8;
    }
    if ((twp->counter.f += -0.033333332f) < 0.0f) {
        FreeTask(tp);
        return;
    }
    twp->scl.x += 0.5f;
    twp->scl.y = njSin(SA1_FTOI(twp->counter.f * 32768.0f)) * 3.0f;
    twp->scl.z = twp->scl.x;
    for (i = 0; i < 4; i++) {
        B_8C7A409C[i].argb.r = (Uint8)SA1_FTOI(twp->counter.f * D_8C4BF110[i].argb.r * 0.031f);
        B_8C7A409C[i].argb.g = (Uint8)SA1_FTOI(twp->counter.f * D_8C4BF110[i].argb.g * 0.031f);
        B_8C7A409C[i].argb.b = (Uint8)SA1_FTOI(twp->counter.f * D_8C4BF110[i].argb.b * 0.031f);
    }
    func_8C0DF9F8(tp);
    func_8C0919CC(tp);
}
