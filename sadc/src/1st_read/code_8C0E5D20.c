#include "sa1/pending.h"

/*
 * Big the Cat (1ST_READ chunk 8C0E5D20-8C0E63C0, first three functions):
 *
 *  func_8C0E5D20  create Big's head / eye look task (exec func_8C0E5DF0,
 *                 look work in tp->mwp, twp->counter.b[0] = player number;
 *                 objs = head, eyeL, eyeR, D_8C525C1C from BigMain).
 *                 Copy of Sonic's func_8C0B27EE / Tails' func_8C0BE070.
 *  func_8C0E5D82  destructor of the look task: puts the saved head angles
 *                 and eye positions back into the head and eye nodes.
 *  func_8C0E5DF0  Big's head / eye look task. Big version of Tails'
 *                 func_8C0BE1CE (same modes, own limits); Big's eyes are
 *                 moved by translation (func_8C0E61A0), so mode 0 saves
 *                 the eye node positions instead of their angles:
 *                 mode 0: save the node state, set the angle limits;
 *                 mode 1: head follows the player's turn (ang_aim.y);
 *                 mode 2: look at the camera (while pad bit 0x100 is held);
 *                 mode 3: look as the event work says (look_mode 1: given
 *                 angles, 2: a point, 3: a point relative to look_tp's
 *                 position, unrotated);
 *                 mode 4: player gone, free the task.
 */

/*
 * Look work of Big's look task: the shared lookwk layout, except that
 * 0x70..0x87 hold the saved eye node positions (Float, FMOV.S at
 * 8C0E5EF0..8C0E5F1E and 8C0E5DAA..8C0E5DEE) instead of the saved eye
 * angles (same as Tails' TAILS_LOOKWK in code_8C0BE160.c). The head/eye
 * angle part (0x00..0x6F) is the one func_8C07AEF8 / func_8C07AF8C work on.
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
} BIG_LOOKWK;

SA1_ASSERT_SIZE(BIG_LOOKWK, 0x88);
SA1_ASSERT_OFFSET(BIG_LOOKWK, head, 0x58);
SA1_ASSERT_OFFSET(BIG_LOOKWK, head_ang, 0x64);
SA1_ASSERT_OFFSET(BIG_LOOKWK, eyeL_pos, 0x70);
SA1_ASSERT_OFFSET(BIG_LOOKWK, eyeR_pos, 0x7C);

/* held buttons of player n (the literal pool holds B_8C754C08 + 8, which
 * the symbol map calls B_8C754C10) */
#define PAD_ON(n) (B_8C754C08[n].on)

void func_8C056860(NJS_VECTOR *v, NJS_VECTOR *front, Angle *ang);  /* direction -> angles relative to front */
void func_8C0E5D82(task *tp);
void func_8C0E5DF0(task *tp);
void func_8C0E61A0(BIG_LOOKWK *lwp);    /* place the eye nodes (same chunk) */

Bool func_8C0E5D20(Sint8 pno, NJS_OBJECT **objs)
{
    task *tp;
    BIG_LOOKWK *lwp;

    if ((tp = CreateElementalTask(2, 2, func_8C0E5DF0)) != NULL) {
        if ((lwp = MAlloc(sizeof(BIG_LOOKWK))) == NULL) {
            FreeTask(tp);
            return FALSE;
        }
        tp->mwp = (motionwk *)lwp;
        tp->twp->counter.b[0] = pno;
        lwp->head = objs[0];
        lwp->eyeL = objs[1];
        lwp->eyeR = objs[2];
        goto ok;  /* the success return is laid out after the failure one */
    }
    return FALSE;
ok:
    return TRUE;
}

void func_8C0E5D82(task *tp)
{
    BIG_LOOKWK *lwp;

    lwp = (BIG_LOOKWK *)tp->mwp;
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

void func_8C0E5DF0(task *tp)
{
    Angle ang[3];
    NJS_POINT3 pos;
    NJS_VECTOR v;
    BIG_LOOKWK *lwp;
    taskwk *ptwp;
    eventwk *ewp;
    CCL_INFO *info;
    taskwk *cam;
    taskwk *ttwp;
    motionwk *pmwp;
    taskwk *twp;

    twp = tp->twp;
    lwp = (BIG_LOOKWK *)tp->mwp;
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
        lwp->eyeL_y_max = 0x400;
        lwp->eyeL_y_min = -0x800;
        lwp->eyeR_y_max = 0x800;
        lwp->eyeR_y_min = -0x400;
        lwp->eyeL_z_max = 0x800;
        lwp->eyeL_z_min = -0x800;
        lwp->eye_z_max = 0x800;
        lwp->eye_z_min = -0x800;
        tp->dest = func_8C0E5D82;
        break;
    case 1:
        lwp->head_tgt_y = -SubAngle(ptwp->ang.y, pmwp->ang_aim.y);
        lwp->head_tgt_z = 0;
        func_8C07AEF8((lookwk *)lwp);
        lwp->eye_tgt_y = 0;
        lwp->eye_tgt_z = 0;
        func_8C07AF8C((lookwk *)lwp);
        func_8C0E61A0(lwp);
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
        func_8C0E61A0(lwp);
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
            if (ewp->look_tp->twp != NULL) {
                ttwp = ewp->look_tp->twp;
                v.y = v.z = 0.0f;
                v.x = 1.0f;
                PConvertVector_P2G(ptwp, &v);
                pos.x = ttwp->pos.x + ewp->look_pos.x - info->center.x;
                pos.y = ttwp->pos.y + ewp->look_pos.y - info->center.y;
                pos.z = ttwp->pos.z + ewp->look_pos.z - info->center.z;
            } else {
                /* the original copies v as it is (not set on this path) */
                PC_ONLY(v.x = 1.0f;)
                PC_ONLY(v.y = 0.0f;)
                PC_ONLY(v.z = 0.0f;)
                pos = v;
            }
            func_8C056860(&pos, &v, ang);
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
        func_8C0E61A0(lwp);
        break;
    default:
        FreeTask(tp);
        break;
    }
}

ASM_STUB(void func_8C0E61A0(BIG_LOOKWK *lwp))
