#include "sa1/pending.h"

/*
 * Tails (1ST_READ chunk 8C0BE160-8C0BEBE0, first two functions):
 *
 *  func_8C0BE160  destructor of Tails' head / eye look task: puts the
 *                 saved head angles and eye positions back into the
 *                 head and eye nodes.
 *  func_8C0BE1CE  Tails' head / eye look task (look work in tp->mwp,
 *                 twp->counter.b[0] = player number; created by
 *                 8C0BE070 from TailsMain). Tails version of Sonic's
 *                 func_8C0B28EE / Amy's func_8C0DDA00 (same modes, own
 *                 limits); Tails' eyes are moved by translation
 *                 (func_8C0BEA0C), so mode 0 saves the eye node
 *                 positions instead of their angles:
 *                 mode 0: save the node state, set the angle limits;
 *                 mode 1: head follows the player's turn (ang_aim.y);
 *                 mode 2: look at the camera (while pad bit 0x100 is held);
 *                 mode 3: look as the event work says (look_mode 1: given
 *                 angles, 2: a point, 3: a point relative to look_tp's
 *                 position, unrotated);
 *                 mode 4: player gone, free the task.
 */

/*
 * Look work of Tails' look task: the shared lookwk layout, except that
 * 0x70..0x87 hold the saved eye node positions (Float, FMOV.S at
 * 8C0BE28A..8C0BE2D6, 8C0BE188..8C0BE1CC; read by func_8C0BEA0C) instead
 * of the saved eye angles. The head/eye angle part (0x00..0x6F) is the
 * one func_8C07AEF8 / func_8C07AF8C work on.
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
SA1_ASSERT_OFFSET(TAILS_LOOKWK, head, 0x58);
SA1_ASSERT_OFFSET(TAILS_LOOKWK, head_ang, 0x64);
SA1_ASSERT_OFFSET(TAILS_LOOKWK, eyeL_pos, 0x70);
SA1_ASSERT_OFFSET(TAILS_LOOKWK, eyeR_pos, 0x7C);

/* held buttons of player n (the literal pool holds B_8C754C08 + 8, which
 * the symbol map calls B_8C754C10) */
#define PAD_ON(n) (B_8C754C08[n].on)

void func_8C056860(NJS_VECTOR *v, NJS_VECTOR *front, Angle *ang);  /* direction -> angles relative to front */
void func_8C0BEA0C(TAILS_LOOKWK *lwp);  /* place the eye nodes (same chunk) */

void func_8C0BE160(task *tp)
{
    TAILS_LOOKWK *lwp;

    lwp = (TAILS_LOOKWK *)tp->mwp;
    lwp->head->ang[0] = lwp->head_ang[0];
    lwp->head->ang[1] = lwp->head_ang[1];
    lwp->head->ang[2] = lwp->head_ang[2];
    lwp->eyeR->pos[0] = lwp->eyeR_pos[0];
    lwp->eyeR->pos[1] = lwp->eyeR_pos[1];
    lwp->eyeR->pos[2] = lwp->eyeR_pos[2];
    lwp->eyeL->pos[0] = lwp->eyeL_pos[0];
    lwp->eyeL->pos[1] = lwp->eyeL_pos[1];
    lwp->eyeL->pos[2] = lwp->eyeL_pos[2];
}

/*
 * Matching notes: as E-102's func_8C0D1916, "v.y = v.z = 0.0f; v.x = 1.0f;"
 * and this declaration order give the original allocation (ptwp R11, &ang
 * R13, ewp / info R12, cam / info R10, 0 / ttwp R9, func_8C07AF8C R8). The
 * look_mode 3 body inside "if (ttwp != NULL) { ... }" (instead of an early
 * break) keeps SHC from cross-jumping the look_mode 2 func_8C056860 call
 * into the look_mode 3 one, so both get their own scheduled call setup as
 * in the original.
 */
void func_8C0BE1CE(task *tp)
{
    Angle ang[3];
    NJS_POINT3 pos;
    NJS_VECTOR v;
    TAILS_LOOKWK *lwp;
    taskwk *ptwp;
    eventwk *ewp;
    CCL_INFO *info;
    taskwk *cam;
    taskwk *ttwp;
    motionwk *pmwp;
    taskwk *twp;

    twp = tp->twp;
    lwp = (TAILS_LOOKWK *)tp->mwp;
    ptwp = playertwp[(Uint8)twp->counter.b[0]];
    pmwp = playermwp[(Uint8)twp->counter.b[0]];
    if (ptwp == NULL) {
        twp->mode = 4;
    } else if (twp->mode == 0) {
        /* first frame: initialised by case 0 below */
    } else if (EWP(ptwp)->look_mode != 0) {
        twp->mode = 3;
    } else if (PAD_ON((Uint8)twp->counter.b[0]) & 0x100) {
        twp->mode = 2;
    } else {
        twp->mode = 1;
    }
    switch (twp->mode) {
    case 0:
        twp->mode = 1;
        lwp->head_ang[0] = lwp->head->ang[0];
        lwp->head_ang[1] = lwp->head->ang[1];
        lwp->head_ang[2] = lwp->head->ang[2];
        lwp->eyeL_pos[0] = lwp->eyeL->pos[0];
        lwp->eyeL_pos[1] = lwp->eyeL->pos[1];
        lwp->eyeL_pos[2] = lwp->eyeL->pos[2];
        lwp->eyeR_pos[0] = lwp->eyeR->pos[0];
        lwp->eyeR_pos[1] = lwp->eyeR->pos[1];
        lwp->eyeR_pos[2] = lwp->eyeR->pos[2];
        lwp->head_y_max = 0x1800;
        lwp->head_y_min = -0x1800;
        lwp->head_z_max = 0x400;
        lwp->head_z_min = -0x1800;
        lwp->eyeL_y_max = 0x200;
        lwp->eyeL_y_min = -0x1000;
        lwp->eyeR_y_max = 0x1000;
        lwp->eyeR_y_min = -0x200;
        lwp->eyeL_z_max = 0x400;
        lwp->eyeL_z_min = -0x800;
        lwp->eye_z_max = 0x400;
        lwp->eye_z_min = -0x800;
        tp->dest = func_8C0BE160;
        break;
    case 1:
        lwp->head_tgt_y = -SubAngle(ptwp->ang.y, pmwp->ang_aim.y);
        lwp->head_tgt_z = 0;
        func_8C07AEF8((lookwk *)lwp);
        lwp->eye_tgt_y = 0;
        lwp->eye_tgt_z = 0;
        func_8C07AF8C((lookwk *)lwp);
        func_8C0BEA0C(lwp);
        break;
    case 2:
        cam = D_8C18B7EC;
        info = CWP(ptwp)->info;
        v.y = v.z = 0.0f;
        v.x = 1.0f;
        PConvertVector_P2G(ptwp, &v);
        pos.x = cam->pos.x - info->center.x;
        pos.y = cam->pos.y - info->center.y;
        pos.z = cam->pos.z - info->center.z;
        func_8C056860(&pos, &v, ang);
        lwp->head_tgt_y = -ang[1];
        lwp->head_tgt_z = ang[2];
        func_8C07AEF8((lookwk *)lwp);
        lwp->eye_tgt_y = ang[1] + lwp->head_y;
        lwp->eye_tgt_z = ang[2] - lwp->head_z;
        func_8C07AF8C((lookwk *)lwp);
        func_8C0BEA0C(lwp);
        break;
    case 3:
        ewp = EWP(ptwp);
        info = CWP(ptwp)->info;
        switch (ewp->look_mode) {
        case 1:
            ang[1] = ewp->look_ang.y;
            ang[2] = ewp->look_ang.z;
            break;
        case 2:
            v.y = v.z = 0.0f;
            v.x = 1.0f;
            PConvertVector_P2G(ptwp, &v);
            pos.x = ewp->look_pos.x - info->center.x;
            pos.y = ewp->look_pos.y - info->center.y;
            pos.z = ewp->look_pos.z - info->center.z;
            func_8C056860(&pos, &v, ang);
            break;
        case 3:
            ttwp = ewp->look_tp->twp;
            if (ttwp != NULL) {
                v.y = v.z = 0.0f;
                v.x = 1.0f;
                PConvertVector_P2G(ptwp, &v);
                pos.x = ttwp->pos.x + ewp->look_pos.x - info->center.x;
                pos.y = ttwp->pos.y + ewp->look_pos.y - info->center.y;
                pos.z = ttwp->pos.z + ewp->look_pos.z - info->center.z;
                func_8C056860(&pos, &v, ang);
            }
#ifdef TARGET_PC
            else {
                /* the original goes on with ang[1]/ang[2] as they are */
                ang[1] = 0;
                ang[2] = 0;
            }
#endif
            break;
        default:
            ang[1] = 0;
            ang[2] = 0;
            break;
        }
        lwp->head_tgt_y = -ang[1];
        lwp->head_tgt_z = ang[2];
        func_8C07AEF8((lookwk *)lwp);
        lwp->eye_tgt_y = ang[1] + lwp->head_y;
        lwp->eye_tgt_z = ang[2] - lwp->head_z;
        func_8C07AF8C((lookwk *)lwp);
        func_8C0BEA0C(lwp);
        break;
    default:
        FreeTask(tp);
        break;
    }
}

ASM_STUB(void func_8C0BEA0C(TAILS_LOOKWK *lwp))
