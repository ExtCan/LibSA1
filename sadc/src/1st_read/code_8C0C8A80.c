#include "sa1/pending.h"

/*
 * Knuckles (1ST_READ chunk 8C0C8A80-8C0C8F20):
 *
 *  func_8C0C8A80  Knuckles' head / eye look task (lookwk in tp->mwp,
 *                 twp->counter.b[0] = player number; destructor
 *                 func_8C0C89CA). Knuckles version of Sonic's
 *                 func_8C0B28EE / Amy's func_8C0DDA00 (same modes, own
 *                 eye limits):
 *                 mode 0: save the node angles, set the angle limits;
 *                 mode 1: head follows the player's turn (ang_aim.y);
 *                 mode 2: look at the camera (while pad bit 0x100 is held);
 *                 mode 3: look as the event work says (look_mode 1: given
 *                 angles, 2: a point, 3: a point local to look_tp);
 *                 mode 4: player gone, free the task.
 *  func_8C0C8E32  Knuckles state code for the objects (player pno):
 *                 modes 19..21 -> 1..3, 26 -> 4, 24 -> 5, 25 -> 6,
 *                 11/12 -> 7; 0 otherwise / not Knuckles.
 *  func_8C0C8E8E  face task (KnucklesMain): allocate the PL_FACE into
 *                 tp->mwp and hand the task to the face code
 *                 8C07B106 / 8C07B0F6.
 */

extern NJS_OBJECT D_8C35AB60[];         /* Knuckles' head node; its sibling is switched by the face code */
extern NJS_OBJECT D_8C35A8D0[];         /* original sibling (static face; array as in code_8C0C71A0.c) */
extern NJS_OBJECT Knuckles_EV_Head;     /* morph face object */
extern NJS_MOTION knuckles_face_k_head; /* face shape motion */

void func_8C056860(NJS_VECTOR *v, NJS_VECTOR *front, Angle *ang);  /* direction -> angles relative to front */
void func_8C0C89CA(task *tp);           /* look task destructor (chunk 8C0C896C) */
void func_8C07B0F6(task *tp);           /* face task dest (code_8C07B080.c) */
void func_8C07B106(task *tp);           /* face task exec (code_8C07B080.c) */

/*
 * Same source shape as Amy's matched func_8C0DDA00 (code_8C0DDA00.c): the
 * three look_mode 2/3/camera func_8C056860 calls stay separate in the
 * source (SHC merges the look 2/3 ones), "v.y = v.z = 0.0f" gives 0.0f FR5
 * / 1.0f FR4, the ?: rotations reload the angle through the spilled ttwp.
 */
void func_8C0C8A80(task *tp)
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
        tp->dest = func_8C0C89CA;
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
        v.y = v.z = 0.0f;
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
            v.y = v.z = 0.0f;
            v.x = 1.0f;
            PConvertVector_P2G(ptwp, &v);
            pos = ewp->look_pos;
            njSubVector(&pos, &info->center);
            func_8C056860(&pos, &v, ang);
            break;
        case 3:
            if (ewp->look_tp == NULL || (ttwp = ewp->look_tp->twp) == NULL) {
                /* the original then uses ang[1]/ang[2] uninitialised */
                ewp->look_mode = 0;
                PC_ONLY(ang[1] = 0;)
                PC_ONLY(ang[2] = 0;)
                break;
            }
            v.y = v.z = 0.0f;
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

Sint32 func_8C0C8E32(Sint32 pno)
{
    taskwk *twp;

    if ((twp = playertwp[pno]) != NULL && (Uint8)twp->counter.b[1] == SA1_CHR_KNUCKLES) {
        if (twp->mode >= 19 && twp->mode <= 21) {
            return twp->mode - 18;
        }
        if (twp->mode == 26) {
            return twp->mode - 22;
        }
        if (twp->mode == 24) {
            return 5;
        }
        if (twp->mode == 25) {
            return 6;
        }
        if (twp->mode == 11 || twp->mode == 12) {
            return 7;
        }
    }
    return 0;
}

void func_8C0C8E8E(task *tp)
{
    PL_FACE *fp;

    if ((fp = CAlloc(1, sizeof(PL_FACE))) != NULL) {
        tp->mwp = (motionwk *)fp;
        fp->parent = D_8C35AB60;
        fp->sibling = D_8C35A8D0;
        fp->object = &Knuckles_EV_Head;
        fp->motion = &knuckles_face_k_head;
        tp->dest = func_8C07B0F6;
        tp->exec = func_8C07B106;
    }
}
