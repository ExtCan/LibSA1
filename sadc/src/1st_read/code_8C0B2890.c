#include "common.h"
#include "ninja.h"
#include "sa1/sa1.h"

/*
 * 1ST_READ 8C0B2890-8C0B2D40: Sonic's head / eye "look at" task and the
 * creators of two per-player tasks (called from the Sonic mode machine
 * 8C0A77BC).
 *
 * func_8C0B2890  destructor of the look task: puts the saved angles back
 *                into the head and eye nodes.
 * func_8C0B28EE  look task (created by func_8C0B27EE with a MAlloc(0x88)
 *                lookwk in tp->mwp; twp->counter.b[0] = player number).
 *                mode 0: save the node angles, set the angle limits;
 *                mode 1: head follows the player's turn (ang_aim.y);
 *                mode 2: look at the camera (while pad bit 0x100 is held);
 *                mode 3: look as the event work says (ewp->look 1: given
 *                angles, 2: a point, 3: a point on another task);
 *                mode 4: player gone, free the task.
 *                Player 1 also copies the head angles into the head node of
 *                the Light Speed aura skeleton (S_MOT overlay).
 * func_8C0B2C8C  create task func_8C0B48B4 for the player (twp->counter.b[0]).
 * func_8C0B2CB2  same with the Super Sonic overlay task 0C9004F2.
 */

/*
 * Head / eye look work (same layout as lookwk in code_8C07AEF8.c, plus the
 * saved node angles; MAlloc(0x88) by func_8C0B27EE).
 */
typedef struct {
    Angle head_tgt_y;           /* 0x00 */
    Angle head_tgt_z;           /* 0x04 */
    Angle head_y;               /* 0x08 */
    Angle head_z;               /* 0x0C */
    Angle eye_tgt_y;            /* 0x10 */
    Angle eye_tgt_z;            /* 0x14 */
    Angle eyeL_y;               /* 0x18 */
    Angle eyeL_z;               /* 0x1C */
    Angle eyeR_y;               /* 0x20 */
    Angle eyeR_z;               /* 0x24 */
    Angle head_y_max;           /* 0x28 */
    Angle head_y_min;           /* 0x2C */
    Angle head_z_max;           /* 0x30 */
    Angle head_z_min;           /* 0x34 */
    Angle eyeL_y_max;           /* 0x38 */
    Angle eyeL_y_min;           /* 0x3C */
    Angle eyeL_z_max;           /* 0x40 */
    Angle eyeL_z_min;           /* 0x44 */
    Angle eyeR_y_max;           /* 0x48 */
    Angle eyeR_y_min;           /* 0x4C */
    Angle eye_z_max;            /* 0x50 */
    Angle eye_z_min;            /* 0x54 */
    NJS_OBJECT *head;           /* 0x58 */
    NJS_OBJECT *eyeL;           /* 0x5C */
    NJS_OBJECT *eyeR;           /* 0x60 */
    Angle head_ang[3];          /* 0x64 saved head->ang */
    Angle eyeL_ang[3];          /* 0x70 saved eyeL->ang */
    Angle eyeR_ang[3];          /* 0x7C saved eyeR->ang */
} lookwk;                       /* 0x88 */

SA1_ASSERT_SIZE(lookwk, 0x88);
SA1_ASSERT_OFFSET(lookwk, head, 0x58);
SA1_ASSERT_OFFSET(lookwk, head_ang, 0x64);
SA1_ASSERT_OFFSET(lookwk, eyeR_ang, 0x7C);

#define LWP(tp) ((lookwk *)(tp)->mwp)

/*
 * Look-at part of the event work (taskwk.ewp, eventwk 0x00-0x23 in
 * sa1/engine.h is still padding there).
 */
typedef struct {
    Sint8 mode;                 /* 0x00 */
    Uint8 pad01[3];
    Uint8 look;                 /* 0x04 0 none, 1 angles, 2 point, 3 point on a task */
    Uint8 pad05[7];
    Angle look_y;               /* 0x0C look 1 */
    Angle look_z;               /* 0x10 look 1 */
    NJS_POINT3 look_pos;        /* 0x14 look 2 (global), look 3 (local to look_tp) */
    task *look_tp;              /* 0x20 look 3 */
} evlookwk;

SA1_ASSERT_OFFSET(evlookwk, look, 0x04);
SA1_ASSERT_OFFSET(evlookwk, look_y, 0x0C);
SA1_ASSERT_OFFSET(evlookwk, look_pos, 0x14);
SA1_ASSERT_OFFSET(evlookwk, look_tp, 0x20);

#define EVLWP(twp) ((evlookwk *)(twp)->ewp)

/* held buttons of player n (the literal pool holds B_8C754C08 + 8, which
 * the symbol map calls B_8C754C10) */
#define PAD_ON(n) (B_8C754C08[n].on)

/*
 * Head node of the Light Speed aura skeleton in S_MOT.PRS (overlay linked
 * at 0x0CC00000, aura object 0x0CC02AD4). PC: provided by the overlay loader.
 */
#if defined(TARGET_PC)
extern NJS_OBJECT S_MOT_object_0CC0252C;
#define AURA_HEAD (&S_MOT_object_0CC0252C)
#else
#define AURA_HEAD ((NJS_OBJECT *)0x0CC0252C)
#endif

/*
 * Task in the Super Sonic overlay B_CHAOS7.BIN (load address 0x0C900000).
 * PC: provided by the overlay loader.
 */
#if defined(TARGET_PC)
extern void func_0C9004F2(task *tp);
#define SUPERSONIC_TASK func_0C9004F2
#else
#define SUPERSONIC_TASK ((TaskFuncPtr)0x0C9004F2)
#endif

extern taskwk *D_8C18B7EC;         /* camera taskwk */
extern NJS_MATRIX D_8C67C640;      /* base matrix */

extern void func_8C07AEF8(lookwk *lwp);    /* turn the head (code_8C07AEF8.c) */
extern void func_8C07AF8C(lookwk *lwp);    /* turn the eyes (code_8C07AEF8.c) */
extern void func_8C056860(NJS_VECTOR *v, NJS_VECTOR *front, Angle *ang);  /* direction -> angles relative to front */
extern void func_8C0B48B4(task *tp);

void func_8C0B2890(task *tp)
{
    lookwk *lwp;

    lwp = LWP(tp);
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
 * NON_MATCHING: every instruction matches except the two spill slots of the
 * 44-byte frame (@R15 / @(4,R15)), which SHC assigns the other way round in
 * two places: cam/info in mode 2 (8C0B2A74-8C0B2AA0) and the &ang[1] /
 * &ang[2] address temporaries of the mode 3 tail (8C0B2C2A-8C0B2C48).
 * Code and register allocation are otherwise identical (418/418, 8 words).
 * Tried: declaration orders, statement orders in mode 2 and the tails,
 * goto-shared func_8C07AF8C call, chained assignments, ternary/if
 * rotations, Angle3 struct or pointer for ang (worse). Needed for the rest
 * of the match: ang as an array (address temporaries, &ang hoisted to R12),
 * the split look_tp/twp NULL tests (reload of ewp->look_tp), the ?: rotations
 * (angle kept in a stack slot) and the flat else-if chain with the empty
 * mode == 0 branch (R3 for ptwp->ewp).
 */
#ifdef NON_MATCHING
void func_8C0B28EE(task *tp)
{
    Angle ang[3];
    NJS_POINT3 pos;
    NJS_VECTOR v;
    motionwk *pmwp;
    taskwk *cam;
    taskwk *twp;
    lookwk *lwp;
    taskwk *ptwp;
    evlookwk *ewp;
    taskwk *ttwp;
    CCL_INFO *info;

    twp = tp->twp;
    lwp = LWP(tp);
    ptwp = playertwp[(Uint8)twp->counter.b[0]];
    pmwp = playermwp[(Uint8)twp->counter.b[0]];
    if (ptwp == NULL) {
        twp->mode = 4;
    } else if (twp->mode == 0) {
        /* first frame: initialised by case 0 below */
    } else if (EVLWP(ptwp)->look != 0) {
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
        lwp->eyeL_y_min = -0x1B00;
        lwp->eyeR_y_max = 0x1B00;
        lwp->eyeR_y_min = -0x200;
        lwp->eyeL_z_max = 0x800;
        lwp->eyeL_z_min = -0xD00;
        lwp->eye_z_max = 0x800;
        lwp->eye_z_min = -0xD00;
        tp->dest = func_8C0B2890;
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
        ewp = EVLWP(ptwp);
        info = CWP(ptwp)->info;
        switch (ewp->look) {
        case 1:
            ang[1] = ewp->look_y;
            ang[2] = ewp->look_z;
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
                ewp->look = 0;
                PC_ONLY(ang[1] = 0;)
                PC_ONLY(ang[2] = 0;)
                break;
            }
            ttwp = ewp->look_tp->twp;
            if (ttwp == NULL) {
                ewp->look = 0;
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
    if (twp->counter.b[0] == 0) {
        AURA_HEAD->ang[1] = lwp->head->ang[1];
        AURA_HEAD->ang[2] = lwp->head->ang[2];
    }
}
#else
ASM_STUB(void func_8C0B28EE(task *tp))
#endif

void func_8C0B2C8C(taskwk *twp)
{
    task *tp;

    if ((tp = CreateElementalTask(2, 6, func_8C0B48B4)) != NULL) {
        tp->twp->counter.b[0] = twp->counter.b[0];
    }
}

void func_8C0B2CB2(taskwk *twp)
{
    task *tp;

    if ((tp = CreateElementalTask(2, 6, SUPERSONIC_TASK)) != NULL) {
        tp->twp->counter.b[0] = twp->counter.b[0];
    }
}
