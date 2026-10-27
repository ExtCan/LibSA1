/*
 * 1ST_READ 8C0DE8C0-8C0DEDC0 (chunk 8C0DE8C0): Amy's bird (Birdie), the
 * small flicky that follows Amy (created by func_8C0DEDC0).
 *  func_8C0DE8C0  disp: welds and draws the bird action at the flap frame
 *                 twp->counter.f, then its blob shadow
 *  func_8C0DE964  exec: follows player 0 (perches above the player while
 *                 Amy's state code is 1, sits on the player while
 *                 func_8C0DDDE6 says so, otherwise circles around the
 *                 player), advances the flap frame with the distance moved
 *  func_8C0DECDA  init: starts at the player, sets disp/exec; in Twinkle
 *                 Park (level 3) also calls a function of the STG03 overlay
 */
#include "common.h"
#include "sa1/sa1.h"

extern NJS_TEXLIST texlist_AMY;         /* 8C4BF3AC */
/* bird action (object, flap motion). Declared as an array: with a plain
 * object SHC folds D_8C4BEF90.motion into a "+4" pool constant, the
 * original loads the base address and reads @(4,Rn). */
extern NJS_ACTION D_8C4BEF90[];
extern NODE_WELD D_8C4BEFA0[];          /* bird weld list */

/* func_8C057302 (code_8C0572D8.c) is (object, motion, weld, frame). SHC
 * passes the pointers in R4-R6 and the frame in FR4 whatever the position
 * of the float, so the SH-4 call is the same with either order, but only
 * the order (object, motion, frame, weld) reproduces the original
 * scheduling (with frame last SHC sets up R6 after FR4). PC uses the real
 * prototype. */
#if defined(TARGET_PC)
void func_8C057302(NJS_OBJECT *object, NJS_MOTION *motion, NODE_WELD *weld, Float frame);
#define WELD_ACTION(object, motion, frame, weld) func_8C057302((object), (motion), (weld), (frame))
#else
void func_8C057302(NJS_OBJECT *object, NJS_MOTION *motion, Float frame, NODE_WELD *weld);
#define WELD_ACTION(object, motion, frame, weld) func_8C057302((object), (motion), (frame), (weld))
#endif
Float func_8C09222C(taskwk *twp, Float r);                              /* blob shadow, code_8C092124.c */
Sint32 func_8C0DDDE6(Sint32 pno);                                       /* code_8C0DDA00.c */
Sint32 func_8C0DDE1C(Sint32 pno);                                       /* code_8C0DDA00.c */

/* "acc += a * b" compiled to FMAC: fused on PC (SA1_FMAC); SA1_FMAC itself
 * (a * b + acc) lets SHC load acc once instead of twice (same macro as
 * code_8C0DB230.c) */
#if defined(TARGET_PC)
#define FMAC_ACC(acc, a, b) ((acc) = SA1_FMAC((a), (b), (acc)))
#else
#define FMAC_ACC(acc, a, b) ((acc) += (a) * (b))
#endif

/* Function of the STG03 (Twinkle Park) overlay (overlays load at
 * 0x0C900000), called with the bird task. PC: provided by the overlay
 * loader. */
#if defined(TARGET_PC)
extern void func_0C907998(task *tp);
#define STG03_FUNC_0C907998 func_0C907998
#else
#define STG03_FUNC_0C907998 ((TaskFuncPtr)0x0C907998)
#endif

void func_8C0DE8C0(task *tp)
{
    taskwk *twp;
    NJS_ACTION *action;
    taskwk sw;

    if (B_8C754EC8 != 0) {
        return;
    }
    twp = tp->twp;
    action = D_8C4BEF90;
    WELD_ACTION(action->object, action->motion, twp->counter.f, D_8C4BEFA0);
    njSetTexture(&texlist_AMY);
    njPushMatrix(NULL);
    njTranslateV(NULL, &twp->pos);
    twp->ang.y ? njRotateY(NULL, twp->ang.y) : (void)0;
    func_8C040942(action, twp->counter.f);
    njPopMatrix(1);
    if (func_8C04FE40()) {
        sw.pos = twp->pos;
        sw.ang = twp->ang;
        sw.ang.y *= -1;
        func_8C09222C(&sw, 0.39999998f);
    }
}

#if defined(TARGET_PC)
/* what func_8C0DE964/func_8C0DECDA read through a NULL player taskwk
 * (never written) */
static taskwk null_page_twp;
#endif

void func_8C0DE964(task *tp)
{
    taskwk *ptwp;
    taskwk *twp;
    Float dist;

    ptwp = playertwp[0];
    twp = tp->twp;
    if (ptwp == NULL || GetPlayerTaskPointer(0)->exec == NULL) {
        FreeTask(tp);
        /* the original goes on and reads ptwp->ang/pos through a NULL ptwp:
         * address 0 is the DC boot ROM (zero in the reference simulator),
         * so PC reads a zero taskwk instead */
        PC_ONLY(if (ptwp == NULL) { ptwp = &null_page_twp; })
    }
    if (func_8C049A4E() == 1) {
        return;
    }
    {
        NJS_POINT3 opos;
        NJS_VECTOR vd;
        NJS_VECTOR vs;

        opos = twp->pos;
        if (func_8C0DDE1C(0) == 1) {
            Float dx;
            Float dz;

            vs.x = vs.z = 0.0f;
            vs.y = 7.0f;
            njPushMatrix(D_8C67C640);
            ptwp->ang.z ? njRotateZ(NULL, ptwp->ang.z) : (void)0;
            ptwp->ang.x ? njRotateX(NULL, ptwp->ang.x) : (void)0;
            -ptwp->ang.y ? njRotateY(NULL, -ptwp->ang.y) : (void)0;
            njCalcVector(NULL, &vs, &vd);
            njPopMatrix(1);
            vd.x += ptwp->pos.x;
            vd.y += ptwp->pos.y;
            vd.z += ptwp->pos.z;
            dx = vd.x - twp->pos.x;
            dz = vd.z - twp->pos.z;
            twp->ang.y = AdjustAngle(twp->ang.y, (Angle)SA1_FTOI(atan2f(dz, -dx) * 10430.381f), 0x400);
            FMAC_ACC(twp->pos.x, vd.x - twp->pos.x, 0.099999996f);
            FMAC_ACC(twp->pos.y, vd.y - twp->pos.y, 0.099999996f);
            FMAC_ACC(twp->pos.z, vd.z - twp->pos.z, 0.099999996f);
        } else if (func_8C0DDDE6(0) == 1) {
            twp->pos = ptwp->pos;
            return;
        } else {
            twp->ang.y = AdjustAngle(twp->ang.y, 0x8000 - ptwp->ang.y, 0x400);
            twp->ang.x += 16;
            vd.x = SA1_FMAC(njCos(twp->ang.x), 3.0f, ptwp->pos.x);
            vd.z = SA1_FMAC(njSin(twp->ang.x), 3.0f, ptwp->pos.z);
            vd.y = ptwp->pos.y + njSin(twp->ang.z += 0x400) + 12.0f;
            if (njDistanceP2P(&twp->pos, &vd) < 200.0f) {
#if defined(TARGET_PC)
                /* the same three updates in the order z, y, x (independent
                 * fields, same results): in the original order clang sinks
                 * them and the identical ones of the perch branch into one
                 * shared block (the factor in a phi), so tools/fmac_audit.py
                 * would count 6 fused ops for the 9 FMACs */
                FMAC_ACC(twp->pos.z, vd.z - twp->pos.z, 0.25f);
                FMAC_ACC(twp->pos.y, vd.y - twp->pos.y, 0.25f);
                FMAC_ACC(twp->pos.x, vd.x - twp->pos.x, 0.25f);
#else
                FMAC_ACC(twp->pos.x, vd.x - twp->pos.x, 0.25f);
                FMAC_ACC(twp->pos.y, vd.y - twp->pos.y, 0.25f);
                FMAC_ACC(twp->pos.z, vd.z - twp->pos.z, 0.25f);
#endif
            } else {
                twp->pos = vd;
            }
        }
        dist = njDistanceP2P(&opos, &twp->pos);
    }
#if defined(TARGET_PC)
    /* FCMP/GT + BT (nbFrame > frame keeps the frame): a NaN frame is reset
     * too */
    twp->counter.f += SA1_FMAC(dist, 0.5f, 0.5f);
    if (!((Float)D_8C4BEF90->motion->nbFrame > twp->counter.f)) {
#else
    if ((twp->counter.f += SA1_FMAC(dist, 0.5f, 0.5f)) >= (Float)D_8C4BEF90->motion->nbFrame) {
#endif
        twp->counter.f = 0.0f;
    }
    func_8C0DE8C0(tp);
    {
        taskwk sw;

        sw.pos = twp->pos;
        sw.ang = twp->ang;
        sw.ang.y *= -1;
        func_8C09222C(&sw, 0.39999998f);
    }
    func_8C098E98(tp);
}

void func_8C0DECDA(task *tp)
{
    taskwk *ptwp;
    taskwk *twp;

    ptwp = playertwp[0];
    twp = tp->twp;
    if (ptwp == NULL) {
        FreeTask(tp);
        /* the original goes on and reads ptwp->pos/ang.y through the NULL
         * pointer (DC boot ROM, zero in the reference simulator), so it
         * also sets disp/exec again after FreeTask */
        PC_ONLY(ptwp = &null_page_twp;)
    }
    twp->pos = ptwp->pos;
    twp->pos.y += 12.0f;
    twp->ang.y = 0x8000 - ptwp->ang.y;
    tp->disp = func_8C0DE8C0;
    if ((GetStageNumber() & 0xFF00) == 0x300) {
        STG03_FUNC_0C907998(tp);
    }
    tp->exec = func_8C0DE964;
}
