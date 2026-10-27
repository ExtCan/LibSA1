#include "sa1/sa1.h"

/*
 * Bonus counter shown with 2D sprite digits: func_8C0D4E40 (exec, still
 * asm) runs a small state machine, func_8C0D4D08 draws the counter,
 * func_8C0D4C30 is the child task that pays the bonus into the time (one
 * second per 4 frames, SE 4).
 */

/* task.awp of func_8C0D4E40 (CAlloc(1, 8) at 8C0D4E8E) */
typedef struct {
    Sint8 mode;                 /* 0x00 */
    Sint8 count;                /* 0x01 */
    Sint16 value;               /* 0x02 displayed value */
    Sint16 timer;               /* 0x04 also the wobble phase (njSin) */
    Sint16 wait;                /* 0x06 */
} BONUSWK;                      /* 0x08 */
SA1_ASSERT_SIZE(BONUSWK, 0x08);

extern NJS_SPRITE D_8C423024;   /* digit sprite */
extern task *D_8C423044;        /* task created by func_8C0D4B54 */
/* Flag byte (scalar Sint8 in code_8C0CBE00.c, code_8C0D23E4.c, ...). Declared as an array here:
 * only B_8C7A2EAA[0] = 1 gives SHC's register choice at 8C0D4C72 (R2 address,
 * R3 value); the scalar store swaps R2/R3. */
extern Sint8 B_8C7A2EAA[];

void func_8C08AB42(Sint32 s);   /* add s seconds to the time (code_8C08A71E.c) */
void func_8C0D4E40(task *tp);

/* Two digits n / 10, n % 10 at the current sprite position. */
void func_8C0D4B00(Sint32 n)
{
    njDrawSprite2D(&D_8C423024, n / 10, -2.0f, 34);
    D_8C423024.p.x += 24.0f;
    njDrawSprite2D(&D_8C423024, n % 10, -2.0f, 34);
    D_8C423024.p.x += 24.0f;
}

void func_8C0D4B54(task *tp)
{
    D_8C423044 = CreateChildTask(2, func_8C0D4E40, tp);
}

/* Right-aligned decimal number centred on pos. */
void func_8C0D4B70(Sint32 n, NJS_POINT3 *pos, Float scale)
{
    Float w;

    w = scale * 24.0f;
    D_8C423024.p.x = pos->x;
    if (n > 99) {
        D_8C423024.p.x += w;
    } else if (n > 9) {
        D_8C423024.p.x = SA1_FMAC(w, 0.5f, D_8C423024.p.x);
    }
    D_8C423024.p.y = pos->y - w * 0.29999999f;
    D_8C423024.sy = scale;
    D_8C423024.sx = scale;
    njColorBlendingMode(0, 8);
    njColorBlendingMode(1, 6);
    func_8C043A20(1.0f, 1.0f, 1.0f, 0.0f);
    while (n > 0) {
        njDrawSprite2D(&D_8C423024, n % 10, -2.0f, 34);
        D_8C423024.p.x -= w;
        n /= 10;
    }
    D_8C423024.sy = 1.0f;
    D_8C423024.sx = 1.0f;
}

void func_8C0D4C30(task *tp)
{
    taskwk *twp;

    twp = tp->twp;
    if (twp->timer.w[1] > 0) {
        twp->timer.w[1]--;
    } else {
        twp->timer.w[1] = 3;
        if (twp->timer.w[0] > 0) {
            twp->timer.w[0]--;
            func_8C08AB42(1);
            func_8C0940DA(4, 0, 0, 0);
        }
        if (twp->timer.w[0] == 0) {
            B_8C7A2EAA[0] = 1;
            DestroyTask(tp);
        }
    }
}

void func_8C0D4D08(task *tp)
{
    BONUSWK *wk;
    taskwk *twp;
    Float scale;
    Float s;

    twp = tp->twp;
    wk = tp->awp;

    if (B_8C754EC8 == 0 && (twp->flag & 0x100)) {
        if (wk->value >= 25) {
            scale = 1.75f;
        } else if (wk->value >= 15) {
            scale = 1.5f;
        } else if (wk->value >= 6) {
            scale = 1.25f;
        } else {
            scale = 1.0f;
        }
        scale = SA1_FMAC(scale * njSin(wk->timer * 0x8000 / 15), 0.5f, scale);
        scale *= twp->scl.x;
        s = scale;
        if (wk->value >= 100) {
            s *= 1.15f;
        }
        D_8C423024.sy = s;
        D_8C423024.sx = s;
        D_8C423024.p.x = twp->pos.x;
        D_8C423024.p.y = twp->pos.y;
        func_8C043A20(1.0f, 1.0f, 1.0f, 1.0f);
        njDrawSprite2D(&D_8C423024, 11, -3.0f, 34);
        func_8C0D4B70(wk->value, &twp->pos, scale);
    }
}

void func_8C0D4DE6(task *tp)
{
    D_8C423044 = NULL;
}
