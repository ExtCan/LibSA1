#include "sa1/sa1.h"

/*
 * E-102 lock-on marker count (1ST_READ chunk 8C0D5BE0):
 *   func_8C0D5BE0  disp: billboard at *twp->timer.ptr (the marker task's
 *                  twp->pos), facing the camera; draws the marker cell 12
 *                  and the lock count twp->value.b[0] as one or two digits
 *   func_8C0D5CE0  exec of the count child task created by the lock-on
 *                  marker exec func_8C0D577C (CreateChildTask(2, ...);
 *                  value.b[0] = lock work count, timer.ptr = &marker twp->pos)
 *   func_8C0D5D08  shot task of a lock-on marker task (NULL-safe); called
 *                  by the E-101 / stage overlays
 */

/*
 * awp of the lock-on marker task func_8C0D577C (CAlloc(1, 0x34) by
 * code_8C0D37F4.c). Local copy of E102_LOCKWK (code_8C0D37F4.c,
 * code_8C0D46CE.c, code_8C0D3C00.c); shared type still to be defined.
 */
typedef struct {
    Sint8 mode;                 /* 0x00 */
    Sint8 count;                /* 0x01 */
    Sint16 unk02;               /* 0x02 */
    Sint16 equipment;           /* 0x04 */
    Sint16 unk06;               /* 0x06 */
    task *target_tp;            /* 0x08 */
    taskwk *ptwp;               /* 0x0C */
    void *ewk;                  /* 0x10 E102WK * */
    task *shot_tp;              /* 0x14 shot task fired at this target */
    Sint32 unk18[5];            /* 0x18 */
    task *group_tp;             /* 0x2C */
    TaskFuncPtr target_exec;    /* 0x30 */
} E102_LOCKWK;                  /* 0x34 */

SA1_ASSERT_SIZE(E102_LOCKWK, 0x34);
SA1_ASSERT_OFFSET(E102_LOCKWK, target_tp, 0x08);
SA1_ASSERT_OFFSET(E102_LOCKWK, shot_tp, 0x14);
SA1_ASSERT_OFFSET(E102_LOCKWK, group_tp, 0x2C);

extern NJS_SPRITE D_8C423260;   /* lock-on marker sprite: cells 0-9 digits, 12 marker */

/*
 * The camera rotations are conditional expressions (not if statements), as
 * in func_8C058100: SHC then keeps the angle in a stack temporary like the
 * original.
 */
void func_8C0D5BE0(task *tp)
{
    taskwk *twp;
    Sint32 n;

    twp = tp->twp;
    if (B_8C754EC8 != 0) {
        return;
    }
    njPushMatrix(NULL);
    njTranslateV(NULL, (NJS_VECTOR *)twp->timer.ptr);
    camera_twp->ang.y ? njRotateY(NULL, camera_twp->ang.y) : (void)0;
    camera_twp->ang.x + 0x8000 ? njRotateX(NULL, camera_twp->ang.x + 0x8000) : (void)0;
    njTranslate(NULL, 0.0f, 0.0f, -10.0f);
    njColorBlendingMode(0, 8);
    njColorBlendingMode(1, 6);
    func_8C043A20(1.0f, 1.0f, 1.0f, 1.0f);
    njDrawSprite3D(&D_8C423260, 12, 0x22); /* NJD_SPRITE_ALPHA | NJD_SPRITE_COLOR */
    njTranslate(NULL, 8.0f, 0.0f, 0.0f);
    if (twp->value.b[0] >= 10) {
        n = twp->value.b[0] / 10;
        njDrawSprite3D(&D_8C423260, n, 0x22);
        njTranslate(NULL, 8.0f, 0.0f, 0.0f);
    }
    n = twp->value.b[0] % 10;
    njDrawSprite3D(&D_8C423260, n, 0x22);
    njPopMatrix(1);
}

void func_8C0D5CE0(task *tp)
{
    taskwk *twp;

    twp = tp->twp;
    if (B_8C754EC8 != 0) {
        return;
    }
    switch (twp->mode) {
    case 0:
        tp->disp = func_8C0D5BE0;
        twp->mode = 1;
    case 1:
        break;
    }
    func_8C0D5BE0(tp);
}

task *func_8C0D5D08(task *tp)
{
    E102_LOCKWK *awp;

    if (tp == NULL) {
        return NULL;
    }
    awp = (E102_LOCKWK *)tp->awp;
    return awp->shot_tp;
}
