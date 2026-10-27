#include "common.h"
#include "sa1/sa1.h"

/*
 * Amy (1ST_READ 8C0DE2A0-8C0DE540, chunk 8C0DE2A0, functions 1-3).
 *  func_8C0DE2A0  hammer effect draw, called by Amy's display 8C0DA122 with
 *                 (twp, mwp, pwp). Picks an A_MOT.PRS action and frame:
 *                 mode 17 (actions 38/39/41) and mode 18 (frame in
 *                 pwp->free.f[5], set by 8C0DCBA0) draw action 0CC93B9C;
 *                 otherwise, while pwp->free.b[4] == 1, action 0CC93698 at
 *                 mj.nframe modulo 32 (returns 1). The action is drawn at
 *                 Amy's position raised by 4.5 along her rotated up axis,
 *                 rotated by twp->ang (y as 0x8000 - ang.y). Returns 0 when
 *                 nothing is drawn or for modes 17/18.
 *  func_8C0DE46C  player pno: free.b[5] = (f > 0); when set, free.f[6] =
 *                 free.f[7] = f. 0 when the player does not exist, else 1.
 *  func_8C0DE4AC  same, setting only free.f[6].
 */

/* A_MOT.PRS actions (Amy's extra motions, overlay linked at 0x0CC00000).
 * PC: provided by the overlay loader. */
#if defined(TARGET_PC)
extern NJS_ACTION A_MOT_action_0CC93B9C;
extern NJS_ACTION A_MOT_action_0CC93698;
#define AMY_ACTION_0CC93B9C (&A_MOT_action_0CC93B9C)
#define AMY_ACTION_0CC93698 (&A_MOT_action_0CC93698)
#else
#define AMY_ACTION_0CC93B9C ((NJS_ACTION *)0x0CC93B9C)
#define AMY_ACTION_0CC93698 ((NJS_ACTION *)0x0CC93698)
#endif

Sint32 func_8C0DE2A0(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_ACTION *action;
    Float frame;
    Sint32 ret;
    NJS_POINT3 out;
    NJS_POINT3 v;

    ret = 0;
    if (twp->mode == 17) {
        action = AMY_ACTION_0CC93B9C;
        if (pwp->mj.reqaction == 39 || pwp->mj.reqaction == 41) {
            frame = pwp->mj.nframe;
        } else if (pwp->mj.reqaction == 38) {
            frame = 0.0f;
        } else {
            frame = (Float)AmyActionList[39].actptr->motion->nbFrame - 1.0f;
        }
    } else if (twp->mode == 18) {
        action = AMY_ACTION_0CC93B9C;
        frame = pwp->free.f[5];
    } else if (pwp->free.b[4] == 1) {
        frame = pwp->mj.nframe;
        while (frame >= 32.0f) {
            frame -= 32.0f;
        }
        action = AMY_ACTION_0CC93698;
        ret = 1;
    } else {
        return 0;
    }
    njPushMatrix(NULL);
    v.z = 0.0f;
    v.x = 0.0f;
    v.y = 4.5f;
    njPushMatrix(D_8C67C640);
    twp->ang.z ? njRotateZ(NULL, twp->ang.z) : (void)0;
    twp->ang.x ? njRotateX(NULL, twp->ang.x) : (void)0;
    -twp->ang.y ? njRotateY(NULL, -twp->ang.y) : (void)0;
    njCalcVector(NULL, &v, &out);
    njPopMatrix(1);
    out.x += twp->pos.x;
    out.y += twp->pos.y;
    out.z += twp->pos.z;
    njTranslateV(NULL, &out);
    twp->ang.z ? njRotateZ(NULL, twp->ang.z) : (void)0;
    twp->ang.x ? njRotateX(NULL, twp->ang.x) : (void)0;
    0x8000 - twp->ang.y ? njRotateY(NULL, 0x8000 - twp->ang.y) : (void)0;
    func_8C040942(action, frame);
    njPopMatrix(1);
    return ret;
}

Sint32 func_8C0DE46C(Uint8 pno, Float f)
{
    playerwk *pwp;

    if ((pwp = func_8C07CFDE(pno)) == NULL) {
        return 0;
    }
    if (!(f > 0.0f)) {
        pwp->free.b[5] = 0;
    } else {
        pwp->free.b[5] = 1;
        pwp->free.f[6] = f;
        pwp->free.f[7] = f;
    }
    return 1;
}

Sint32 func_8C0DE4AC(Uint8 pno, Float f)
{
    playerwk *pwp;

    if ((pwp = func_8C07CFDE(pno)) == NULL) {
        return 0;
    }
    if (!(f > 0.0f)) {
        pwp->free.b[5] = 0;
    } else {
        pwp->free.b[5] = 1;
        pwp->free.f[6] = f;
    }
    return 1;
}
