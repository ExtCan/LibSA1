#include "common.h"
#include "sa1/sa1.h"

/*
 * Tails (1ST_READ chunk 8C0BE160-8C0BEBE0, third function):
 *
 *  func_8C0BE558  second head / eye look task of Tails (created by
 *                 func_8C0BE0CE; look work in tp->mwp, twp->counter.b[0] =
 *                 player number). The head only tilts (ang[2], kept in
 *                 twp->ang.z) with the player's vertical speed; the eyes are
 *                 moved by translation (func_8C0BEA0C):
 *                 mode 0: save the node state (as func_8C0BE1CE), set the
 *                 destructor func_8C0BE160;
 *                 mode 1: head tilt by spd.y (> 0: -0x1000, < 0: 0x400),
 *                 eyes to the front; pad bit 0x100 held -> mode 2;
 *                 mode 2: head tilt by spd.y (> 1: -0x1000, < -1: 0x1000),
 *                 eyes look at the camera: yaw/pitch of the vector from the
 *                 eyes (player pos + (0, eyes_height, 0) rotated by the
 *                 player's angles) to the camera, minus yaw/pitch of the
 *                 rotated player front (1, 0, 0), pitch plus the head tilt;
 *                 pad bit released -> mode 1;
 *                 other modes: free the task.
 *                 Player gone (checked first): free the task.
 */

/*
 * Look work of Tails' look tasks (same as in code_8C0BE160.c): the shared
 * lookwk layout, except that 0x70..0x87 hold the saved eye node positions
 * (Float, FMOV.S here at 8C0BE5DC..8C0BE620) instead of the saved eye angles.
 * The memory is MAlloc(sizeof(lookwk)) (func_8C0BE0CE); the field sequence
 * is the same as sa1_lookwk's, so the overlay also holds on 64-bit. DC
 * offsets are checked by the SA1_ASSERT_OFFSETs below (no per-field offset
 * comments: pointers move them on x86_64).
 */
typedef struct {
    Angle head_tgt_y;
    Angle head_tgt_z;
    Angle head_y;
    Angle head_z;
    Angle eye_tgt_y;
    Angle eye_tgt_z;
    Angle eyeL_y;
    Angle eyeL_z;
    Angle eyeR_y;
    Angle eyeR_z;
    Angle head_y_max;
    Angle head_y_min;
    Angle head_z_max;
    Angle head_z_min;
    Angle eyeL_y_max;
    Angle eyeL_y_min;
    Angle eyeL_z_max;
    Angle eyeL_z_min;
    Angle eyeR_y_max;
    Angle eyeR_y_min;
    Angle eye_z_max;
    Angle eye_z_min;
    NJS_OBJECT *head;
    NJS_OBJECT *eyeL;
    NJS_OBJECT *eyeR;
    Angle head_ang[3];          /* saved head->ang */
    Float eyeL_pos[3];          /* saved eyeL->pos */
    Float eyeR_pos[3];          /* saved eyeR->pos */
} TAILS_LOOKWK;

SA1_ASSERT_SIZE(TAILS_LOOKWK, 0x88);
SA1_ASSERT_OFFSET(TAILS_LOOKWK, eye_tgt_y, 0x10);
SA1_ASSERT_OFFSET(TAILS_LOOKWK, head, 0x58);
SA1_ASSERT_OFFSET(TAILS_LOOKWK, head_ang, 0x64);
SA1_ASSERT_OFFSET(TAILS_LOOKWK, eyeL_pos, 0x70);
SA1_ASSERT_OFFSET(TAILS_LOOKWK, eyeR_pos, 0x7C);

/* held buttons of player n (the literal pool holds B_8C754C08 + 8, which
 * the symbol map calls B_8C754C10) */
#define PAD_ON(n) (B_8C754C08[n].on)

void func_8C0BE160(task *tp);           /* look task destructor (code_8C0BE160.c) */
void func_8C0BEA0C(TAILS_LOOKWK *lwp);  /* place the eye nodes (same chunk, BSR) */

/*
 * Matching notes (all needed for the original register allocation):
 *  - the head target is computed into tgt by the if/else chain and copied
 *    to utgt = (Uint16)tgt before the AdjustAngle call: a variable that is
 *    converted inside the call statement (or a ?: inside the call) is kept
 *    live across the call by SHC (callee-saved register or stack slot);
 *  - "twp->mode = 2; return;" in the player-NULL paths (break gives the
 *    wrong cross-jump in mode 1 and fills the BRA delay slot in mode 2);
 *  - the chained "lwp->eyeR_z = lwp->eyeL_z = ang" in mode 1 only, the
 *    chained zero stores "v.x = v.z" / "ofs.y = ofs.z" and the spelled-out
 *    "a = a - x" / "a = a + y" updates of eye_tgt_y / eye_tgt_z in mode 2
 *    fix SHC's tie-breaking between the hoisted constants (R8 -0x1000,
 *    R9 0x800, R10 0x400), the mode 1 pointer registers and FR12-FR15;
 *    found by an exhaustive search over these equivalent spellings.
 */
void func_8C0BE558(task *tp)
{
    NJS_VECTOR v;
    NJS_VECTOR ofs;
    taskwk *twp;
    TAILS_LOOKWK *lwp;
    taskwk *ptwp;
    motionwk *pmwp;
    playerwk *pwp;
    Angle ang;
    Angle tgt;
    Angle utgt;
    Float r;

    twp = tp->twp;
    lwp = (TAILS_LOOKWK *)tp->mwp;
    if (playertwp[(Uint8)twp->counter.b[0]] == NULL) {
        FreeTask(tp);
        return;
    }
    switch (twp->mode) {
    case 0:
        lwp->head_ang[0] = lwp->head->ang[0];
        lwp->head_ang[1] = lwp->head->ang[1];
        lwp->head_ang[2] = lwp->head->ang[2];
        lwp->eyeL_pos[0] = lwp->eyeL->pos[0];
        lwp->eyeL_pos[1] = lwp->eyeL->pos[1];
        lwp->eyeL_pos[2] = lwp->eyeL->pos[2];
        lwp->eyeR_pos[0] = lwp->eyeR->pos[0];
        lwp->eyeR_pos[1] = lwp->eyeR->pos[1];
        lwp->eyeR_pos[2] = lwp->eyeR->pos[2];
        tp->dest = func_8C0BE160;
        twp->mode = 1;
        break;
    case 1:
        ptwp = playertwp[(Uint8)twp->counter.b[0]];
        pmwp = playermwp[(Uint8)twp->counter.b[0]];
        if (ptwp == NULL) {
            twp->mode = 2;
            return;
        }
        if (pmwp->spd.y > 0.0f) {
            tgt = -0x1000;
        } else if (pmwp->spd.y < 0.0f) {
            tgt = 0x400;
        } else {
            tgt = 0;
        }
        utgt = (Uint16)tgt;
        twp->ang.z = AdjustAngle(twp->ang.z, utgt, 0x400);
        lwp->head->ang[2] = twp->ang.z;
        lwp->eye_tgt_y = 0;
        lwp->eye_tgt_z = 0;
        ang = AdjustAngle(lwp->eyeL_y, lwp->eye_tgt_y, 0x800);
        if (ang & 0x8000)
            ang |= ~0xFFFF;
        if (ang > 0x200)
            ang = 0x200;
        else if (ang < -0x1000)
            ang = -0x1000;
        lwp->eyeL_y = ang;
        ang = AdjustAngle(lwp->eyeR_y, lwp->eye_tgt_y, 0x800);
        if (ang & 0x8000)
            ang |= ~0xFFFF;
        if (ang < -0x200)
            ang = -0x200;
        else if (ang > 0x1000)
            ang = 0x1000;
        lwp->eyeR_y = ang;
        ang = AdjustAngle(lwp->eyeL_z, lwp->eye_tgt_z, 0x800);
        if (ang & 0x8000)
            ang |= ~0xFFFF;
        ang >>= 1;
        if (ang < -0x800)
            ang = -0x800;
        else if (ang > 0x400)
            ang = 0x400;
        lwp->eyeR_z = lwp->eyeL_z = ang;
        func_8C0BEA0C(lwp);
        if (PAD_ON((Uint8)twp->counter.b[0]) & 0x100) {
            twp->mode = 2;
        }
        break;
    case 2:
        ptwp = playertwp[(Uint8)twp->counter.b[0]];
        pmwp = playermwp[(Uint8)twp->counter.b[0]];
        pwp = pmwp->work.pwp;
        if (ptwp == NULL) {
            twp->mode = 2;
            return;
        }
        if (pmwp->spd.y > 1.0f) {
            tgt = -0x1000;
        } else if (pmwp->spd.y < -1.0f) {
            tgt = 0x1000;
        } else {
            tgt = 0;
        }
        utgt = (Uint16)tgt;
        twp->ang.z = AdjustAngle(twp->ang.z, utgt, 0x400);
        lwp->head->ang[2] = twp->ang.z;
        v.x = v.z = 0.0f;
        v.y = pwp->p.eyes_height;
        njPushMatrix(D_8C67C640);
        ptwp->ang.z ? njRotateZ(NULL, ptwp->ang.z) : (void)0;
        ptwp->ang.x ? njRotateX(NULL, ptwp->ang.x) : (void)0;
        -ptwp->ang.y ? njRotateY(NULL, -ptwp->ang.y) : (void)0;
        njCalcVector(NULL, &v, &ofs);
        v.x = camera_twp->pos.x - ptwp->pos.x - ofs.x;
        v.y = camera_twp->pos.y - ptwp->pos.y - ofs.y;
        v.z = camera_twp->pos.z - ptwp->pos.z - ofs.z;
        r = njSqrt(SA1_FMAC(v.x, v.x, v.z * v.z));
        lwp->eye_tgt_y = SA1_FTOI(atan2f(v.x, -v.z) * 10430.381f);
        lwp->eye_tgt_z = SA1_FTOI(atan2f(-v.y, r) * 10430.381f);
        ofs.y = ofs.z = 0.0f;
        ofs.x = 1.0f;
        njCalcVector(NULL, &ofs, &v);
        njPopMatrix(1);
        r = njSqrt(SA1_FMAC(v.x, v.x, v.z * v.z));
        lwp->eye_tgt_y = lwp->eye_tgt_y - SA1_FTOI(atan2f(v.x, -v.z) * 10430.381f);
        lwp->eye_tgt_z -= SA1_FTOI(atan2f(-v.y, r) * 10430.381f);
        lwp->eye_tgt_z = lwp->eye_tgt_z + twp->ang.z;
        ang = AdjustAngle(lwp->eyeL_y, lwp->eye_tgt_y, 0x800);
        if (ang & 0x8000)
            ang |= ~0xFFFF;
        if (ang > 0x200)
            ang = 0x200;
        else if (ang < -0x1000)
            ang = -0x1000;
        lwp->eyeL_y = ang;
        ang = AdjustAngle(lwp->eyeR_y, lwp->eye_tgt_y, 0x800);
        if (ang & 0x8000)
            ang |= ~0xFFFF;
        if (ang < -0x200)
            ang = -0x200;
        else if (ang > 0x1000)
            ang = 0x1000;
        lwp->eyeR_y = ang;
        ang = AdjustAngle(lwp->eyeL_z, lwp->eye_tgt_z, 0x800);
        if (ang & 0x8000)
            ang |= ~0xFFFF;
        ang >>= 1;
        if (ang < -0x800)
            ang = -0x800;
        else if (ang > 0x400)
            ang = 0x400;
        lwp->eyeL_z = ang;
        lwp->eyeR_z = ang;
        func_8C0BEA0C(lwp);
        if (!(PAD_ON((Uint8)twp->counter.b[0]) & 0x100)) {
            twp->mode = 1;
        }
        break;
    default:
        FreeTask(tp);
        break;
    }
}

ASM_STUB(void func_8C0BEA0C(TAILS_LOOKWK *lwp))
