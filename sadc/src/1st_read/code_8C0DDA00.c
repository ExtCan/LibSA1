#include "sa1/pending.h"

/*
 * Amy (1ST_READ chunk 8C0DDA00-8C0DE2A0, first five functions):
 *
 *  func_8C0DDA00  Amy's head / eye look task (lookwk in tp->mwp,
 *                 twp->counter.b[0] = player number; created by
 *                 8C0DD8DC, destructor func_8C0DD944). Amy version of
 *                 Sonic's func_8C0B28EE (same modes, own eye limits):
 *                 mode 0: save the node angles, set the angle limits;
 *                 mode 1: head follows the player's turn (ang_aim.y);
 *                 mode 2: look at the camera (while pad bit 0x100 is held);
 *                 mode 3: look as the event work says (look_mode 1: given
 *                 angles, 2: a point, 3: a point local to look_tp);
 *                 mode 4: player gone, free the task.
 *  func_8C0DDDB2  face task (AmyMain): allocate the PL_FACE into tp->mwp
 *                 and hand the task to the face code 8C07B106 / 8C07B0F6.
 *  func_8C0DDDE6  Amy's pwp->free.b[4] for player pno (0 if the player is
 *                 not Amy).
 *  func_8C0DDE1C  Amy state code for the objects: 9 while player 0's
 *                 reqaction is 87/88, else a code (1..8) per Amy mode
 *                 (11, 15-18, 20-22); 0 otherwise / not Amy.
 *  func_8C0DDF40  task exec: free the task (after func_8C10778E) once
 *                 player 0 is closer than twp->value.f.
 */

/* held buttons of player n (the literal pool holds B_8C754C08 + 8, which
 * the symbol map calls B_8C754C10) */
#define PAD_ON(n) (B_8C754C08[n].on)

extern NJS_OBJECT D_8C4C75C8;       /* Amy's head node; its sibling is switched by the face code */
extern NJS_OBJECT D_8C4C72FC;       /* original sibling (static face) */
extern NJS_OBJECT Amy_EV_Head;      /* morph face object */
extern NJS_MOTION amy_face_a_head;  /* face shape motion */

void func_8C056860(NJS_VECTOR *v, NJS_VECTOR *front, Angle *ang);  /* direction -> angles relative to front */
void func_8C0DD944(task *tp);       /* look task destructor (chunk 8C0DD166) */
void func_8C07B0F6(task *tp);       /* face task dest (code_8C07B080.c) */
void func_8C07B106(task *tp);       /* face task exec (code_8C07B080.c) */
Sint32 func_8C10778E(void);         /* if task D_8C5FB8A4 exists: twp->btimer |= 1, returns 1 (else 0) */

/*
 * Matching notes (differences from Sonic's / E-102's non-matching versions):
 * the three look_mode 2/3/camera func_8C056860 calls stay separate in the
 * source (SHC merges the look 2/3 ones after scheduling); "v.y = v.z = 0.0f"
 * gives 0.0f FR5 / 1.0f FR4; the joined look_tp / twp NULL test and the ?:
 * rotations (angle reloaded through the spilled ttwp) give ttwp a stack slot,
 * which leaves R8/R9 to func_8C07AEF8/func_8C07AF8C.
 */
void func_8C0DDA00(task *tp)
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
        lwp->eyeL_y_min = -0x800;
        lwp->eyeR_y_max = 0x800;
        lwp->eyeR_y_min = -0x200;
        lwp->eyeL_z_max = 0x800;
        lwp->eyeL_z_min = -0xD00;
        lwp->eye_z_max = 0x800;
        lwp->eye_z_min = -0xD00;
        tp->dest = func_8C0DD944;
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

void func_8C0DDDB2(task *tp)
{
    PL_FACE *fp;

    if ((fp = CAlloc(1, sizeof(PL_FACE))) != NULL) {
        tp->mwp = (motionwk *)fp;
        fp->parent = &D_8C4C75C8;
        fp->sibling = &D_8C4C72FC;
        fp->object = &Amy_EV_Head;
        fp->motion = &amy_face_a_head;
        tp->dest = func_8C07B0F6;
        tp->exec = func_8C07B106;
    }
}

Sint32 func_8C0DDDE6(Sint32 pno)
{
    taskwk *twp;
    playerwk *pwp;

    twp = playertwp[pno];
    if (twp == NULL || (Uint8)twp->counter.b[1] != SA1_CHR_AMY) {
        return 0;
    }
    pwp = GetPlayerWorkPtr(pno);
    return pwp->free.b[4];
}

/*
 * The original keeps pno (a dead store) and pwp in an 8-byte frame and
 * reloads pwp from it: reproduced with the two volatile locals (n is never
 * read). reqaction is read once (switch).
 */
Sint32 func_8C0DDE1C(Sint32 pno)
{
    taskwk *twp;
    playerwk * volatile pwp;
    volatile Sint32 n;

    twp = playertwp[n = pno];
    if (twp == NULL) {
        return 0;
    }
    if ((Uint8)twp->counter.b[1] != SA1_CHR_AMY) {
        return 0;
    }
    if ((pwp = GetPlayerWorkPtr(0)) == NULL) {
        return 0;
    }
    switch (pwp->mj.reqaction) {
    case 87:
    case 88:
        return 9;
    }
    switch (twp->mode) {
    case 11:
        return 3;
    case 17:
        return 1;
    case 18:
        return 2;
    case 15:
        return 4;
    case 20:
        return 5;
    case 16:
        return 6;
    case 21:
        return 7;
    case 22:
        return 8;
    }
    return 0;
}

void func_8C0DDF40(task *tp)
{
    taskwk *ptwp;
    taskwk *twp;

    ptwp = playertwp[0];
    if (ptwp == NULL) {
        FreeTask(tp);
        return;
    }
    twp = tp->twp;
    if (njDistanceP2P(&ptwp->pos, &twp->pos) < twp->value.f) {
        func_8C10778E();
        FreeTask(tp);
    }
}
