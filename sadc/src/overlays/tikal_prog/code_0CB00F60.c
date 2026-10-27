#include "sa1/pending.h"

/*
 * TIKAL_PROG overlay 0CB00F60-0CB013F4: Tikal's head / eye "look at" task
 * (Tikal copy of Sonic's func_8C0B27EE / func_8C0B2890 / func_8C0B28EE and
 * Knuckles' func_8C0C896C / func_8C0C89CA / func_8C0C8A80).
 *
 *  func_0CB00F60  create the look task func_0CB0101C for player pno with the
 *                 head and eye objects objs[0..2] (lookwk in tp->mwp,
 *                 twp->counter.b[0] = player number); called by Tikal's
 *                 main code (0CB000DC), which ignores the result.
 *  func_0CB00FBE  dest of the look task: put the saved head and eye angles
 *                 back into the nodes.
 *  func_0CB0101C  look task:
 *                 mode 0: save the node angles, set the angle limits;
 *                 mode 1: head follows the player's turn (ang_aim.y);
 *                 mode 2: look at the camera (while pad bit 0x100 is held);
 *                 mode 3: look as the event work says (look_mode 1: given
 *                 angles, 2: a point, 3: a point local to look_tp);
 *                 mode 4: player gone, free the task.
 */

void func_8C056860(NJS_VECTOR *v, NJS_VECTOR *front, Angle *ang);  /* direction -> angles relative to front */

void func_0CB00FBE(task *tp);
void func_0CB0101C(task *tp);

/* The success path has no return value in the original (R0 still holds the
 * store offset 0x60); the caller ignores the result. The PC build returns
 * TRUE. */
Bool func_0CB00F60(Sint8 pno, NJS_OBJECT **objs)
{
    task *tp;
    lookwk *lwp;

    if ((tp = CreateElementalTask(2, 2, func_0CB0101C)) != NULL) {
        if ((lwp = MAlloc(sizeof(lookwk))) == NULL) {
            FreeTask(tp);
            return FALSE;
        }
        tp->mwp = (motionwk *)lwp;
        tp->twp->counter.b[0] = pno;
        lwp->head = objs[0];
        lwp->eyeL = objs[1];
        lwp->eyeR = objs[2];
        goto ok;
    }
    return FALSE;
ok:
    PC_ONLY(return TRUE;)
    ;
}

void func_0CB00FBE(task *tp)
{
    lookwk *lwp;

    lwp = (lookwk *)tp->mwp;
    lwp->head->ang[0] = lwp->head_ang[0];
    lwp->head->ang[1] = lwp->head_ang[1];
    lwp->head->ang[2] = lwp->head_ang[2];
    lwp->eyeR->ang[0] = lwp->eyeR_ang[0];
    lwp->eyeR->ang[1] = lwp->eyeR_ang[1];
    lwp->eyeR->ang[2] = lwp->eyeR_ang[2];
    lwp->eyeL->ang[0] = lwp->eyeL_ang[0];
    lwp->eyeL->ang[1] = lwp->eyeL_ang[1];
    lwp->eyeL->ang[2] = lwp->eyeL_ang[2];
}

/*
 * NON_MATCHING: every instruction and register matches (407/407); only the
 * two spill slots of the 44-byte frame (@R15 / @(4,R15)) are assigned the
 * other way round in mode 2: cam/info (0CB011B4-0CB011E2, original cam @R15,
 * info @(4,R15)) and the &ang[1] / &ang[2] address temporaries of the mode 2
 * tail (0CB011FE-0CB0121C, original &ang[1] @R15). The mode 3 tail already
 * gets the original order (&ang[1] @(4,R15)). Same problem as Sonic's
 * func_8C0B28EE (code_8C0B2890.c), which is this function plus the aura
 * head copy; Tikal's mode 3 tail differs from Sonic's original there.
 * SHC orders spill slots by allocation priority (reference count); the two
 * pairs tie and the tie goes the other way. Tried without effect: cam/info
 * declaration order and block-scope copies, statement order of the cam/info
 * loads (moves instructions only), dead references/assignments, ?:/!=0
 * spellings of the tests, &ang[0], chained zero stores (moves FR4/FR5),
 * eye_tgt_z before eye_tgt_y (fixes the tail slots but swaps the two eye
 * computations), explicit &ang[1]/&ang[2] pointer variables (other code),
 * case 3 before case 2 (other layout). An extra real use of info gives it
 * R11 (reference-count priority). Needed for the rest of the match (as in
 * Sonic's version): the split look_tp/twp NULL tests (reload of
 * ewp->look_tp; the joined || form changes the code), "v.z = 0; v.y = 0;
 * v.x = 1" (FR5 = 1.0f, FR4 = 0.0f) and the ?: rotations.
 */
#ifdef NON_MATCHING
void func_0CB0101C(task *tp)
{
    Angle ang[3];
    NJS_POINT3 pos;
    NJS_VECTOR v;
    motionwk *pmwp;
    taskwk *cam;
    taskwk *twp;
    lookwk *lwp;
    taskwk *ptwp;
    eventwk *ewp;
    taskwk *ttwp;
    CCL_INFO *info;

    twp = tp->twp;
    lwp = (lookwk *)tp->mwp;
    ptwp = playertwp[(Uint8)twp->counter.b[0]];
    pmwp = playermwp[(Uint8)twp->counter.b[0]];
    if (ptwp == NULL) {
        twp->mode = 4;
    } else if (twp->mode == 0) {
        /* first frame: initialised by case 0 below */
    } else if (EWP(ptwp)->look_mode != 0) {
        twp->mode = 3;
    } else if (B_8C754C08[(Uint8)twp->counter.b[0]].on & 0x100) {
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
        lwp->eyeL_ang[0] = lwp->eyeL->ang[0];
        lwp->eyeL_ang[1] = lwp->eyeL->ang[1];
        lwp->eyeL_ang[2] = lwp->eyeL->ang[2];
        lwp->eyeR_ang[0] = lwp->eyeR->ang[0];
        lwp->eyeR_ang[1] = lwp->eyeR->ang[1];
        lwp->eyeR_ang[2] = lwp->eyeR->ang[2];
        lwp->head_y_max = 0x2000;
        lwp->head_y_min = -0x2000;
        lwp->head_z_max = 0x400;
        lwp->head_z_min = -0x1800;
        lwp->eyeL_y_max = 0x200;
        lwp->eyeL_y_min = -0x1B00;
        lwp->eyeR_y_max = 0x1B00;
        lwp->eyeR_y_min = -0x200;
        lwp->eyeL_z_max = 0x800;
        lwp->eyeL_z_min = -0xD00;
        lwp->eye_z_max = 0x800;
        lwp->eye_z_min = -0xD00;
        tp->dest = func_0CB00FBE;
        break;
    case 1:
        lwp->head_tgt_y = -SubAngle(ptwp->ang.y, pmwp->ang_aim.y);
        lwp->head_tgt_z = 0;
        func_8C07AEF8(lwp);
        lwp->eye_tgt_y = 0;
        lwp->eye_tgt_z = 0;
        func_8C07AF8C(lwp);
        break;
    case 2:
        cam = D_8C18B7EC;
        info = CWP(ptwp)->info;
        v.z = 0.0f;
        v.y = 0.0f;
        v.x = 1.0f;
        PConvertVector_P2G(ptwp, &v);
        pos = cam->pos;
        njSubVector(&pos, &info->center);
        func_8C056860(&pos, &v, ang);
        lwp->head_tgt_y = -ang[1];
        lwp->head_tgt_z = ang[2];
        func_8C07AEF8(lwp);
        lwp->eye_tgt_y = ang[1] + lwp->head_y;
        lwp->eye_tgt_z = ang[2] - lwp->head_z;
        func_8C07AF8C(lwp);
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
            v.z = 0.0f;
            v.y = 0.0f;
            v.x = 1.0f;
            PConvertVector_P2G(ptwp, &v);
            pos = ewp->look_pos;
            njSubVector(&pos, &info->center);
            func_8C056860(&pos, &v, ang);
            break;
        case 3:
            if (ewp->look_tp == NULL) {
                ewp->look_mode = 0;
                PC_ONLY(ang[1] = 0;)
                PC_ONLY(ang[2] = 0;)
                break;
            }
            ttwp = ewp->look_tp->twp;
            if (ttwp == NULL) {
                ewp->look_mode = 0;
                PC_ONLY(ang[1] = 0;)
                PC_ONLY(ang[2] = 0;)
                break;
            }
            v.z = 0.0f;
            v.y = 0.0f;
            v.x = 1.0f;
            PConvertVector_P2G(ptwp, &v);
            njPushMatrix(D_8C67C640);
            ttwp->ang.z ? njRotateZ(NULL, ttwp->ang.z) : (void)0;
            ttwp->ang.x ? njRotateX(NULL, ttwp->ang.x) : (void)0;
            ttwp->ang.y ? njRotateY(NULL, ttwp->ang.y) : (void)0;
            njCalcVector(NULL, &ewp->look_pos, &pos);
            njPopMatrix(1);
            njAddVector(&pos, &ttwp->pos);
            njSubVector(&pos, &info->center);
            func_8C056860(&pos, &v, ang);
            break;
        default:
            ang[1] = 0;
            ang[2] = 0;
            break;
        }
        lwp->head_tgt_y = -ang[1];
        lwp->head_tgt_z = ang[2];
        func_8C07AEF8(lwp);
        lwp->eye_tgt_y = ang[1] + lwp->head_y;
        lwp->eye_tgt_z = ang[2] - lwp->head_z;
        func_8C07AF8C(lwp);
        break;
    default:
        FreeTask(tp);
        break;
    }
}
#else
ASM_STUB(void func_0CB0101C(task *tp))
#endif
