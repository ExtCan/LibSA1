#include "sa1/pending.h"

/*
 * E-102 Gamma (1ST_READ chunk 8C0CBE00-8C0D2080):
 *
 *  func_8C0D1916  E-102 head / eye look task (lookwk in tp->mwp,
 *                 twp->counter.b[0] = player number; destructor
 *                 func_8C0D18B8). E-102 version of Sonic's func_8C0B28EE:
 *                 mode 0: save the node angles, set the angle limits;
 *                 mode 1: head follows the player's turn (ang_aim.y);
 *                 mode 2: look at the camera (while pad bit 0x100 is held);
 *                 mode 3: look as the event work says (look_mode 1: given
 *                 angles, 2: a point, 3: look_tp's position + look_pos;
 *                 unlike Sonic's there is no rotation by the target's
 *                 angles and no NULL check of look_tp / its twp);
 *                 mode 4: player gone, free the task.
 *  func_8C0D1C78  Jet Booster hover (pwp->equipment 0x400) while the jump
 *                 buttons (D_8C19E390) are held: clears pwp->acc.y, limits
 *                 the fall speed (spd.y + 0.05, at least -0.4), runs the
 *                 jet effect 8C0D2D16 with the ground height and the
 *                 positional SE 0x315; otherwise stops SE 0x315. Returns 1
 *                 while hovering (E102Main then sets a hover action).
 *  func_8C0D1D76  same, but only once the fall speed has reached -0.4;
 *                 also clears mwp->acc.y.
 *
 * All three are called with the (twp, mwp, pwp) / (tp) of E102Main
 * (8C0CBE00); the original translation unit is the whole chunk, so the
 * literal pools are shared with the functions before them (see the stubs
 * below).
 */

/*
 * E-102 line draw list and E-102 work (pwp->free.ptr[0]; E102Main 8C0CC0F2:
 * CAlloc(1, 0x244)). Copy of the full layout in code_8C0CBE00.c (no shared
 * header type yet); only ground_y is used here.
 */
typedef struct {
    NJS_POINT3 *src_p;          /* 0x00 */
    NJS_COLOR *src_col;         /* 0x04 */
    Sint16 *idx;                /* 0x08 */
    Sint16 num;                 /* 0x0C */
    Sint16 pad0E;               /* 0x0E */
    NJS_POINT3COL p3c;          /* 0x10 */
} E102_LINE;                    /* 0x20 */

typedef struct {
    Sint8 mode;                 /* 0x00 laser/targeting state */
    Sint8 smode;                /* 0x01 targeting arm state 0..5 */
    Sint8 nb_target;            /* 0x02 valid markers */
    Sint8 unk03;                /* 0x03 */
    Sint8 nb_lock;              /* 0x04 entries of lock_tp[] */
    Sint8 nb_shot;              /* 0x05 entries of shot_tp[] */
    Sint8 arm_idx;              /* 0x06 */
    Sint8 unk07;                /* 0x07 */
    Sint16 timer08;             /* 0x08 */
    Sint16 timer0A;             /* 0x0A */
    Sint16 timer0C;             /* 0x0C */
    Sint16 unk0E;               /* 0x0E */
    Sint16 laser_timer;         /* 0x10 */
    Sint16 lockon_timer;        /* 0x12 */
    Sint16 unk14;               /* 0x14 */
    Sint16 flag;                /* 0x16 */
    Float hover_y;              /* 0x18 */
    Angle hover_phase;          /* 0x1C */
    Angle3 jet_ang;             /* 0x20 */
    Angle jet_phase;            /* 0x2C */
    task *target_tp;            /* 0x30 */
    NJS_POINT3 laser_pos;       /* 0x34 */
    NJS_POINT3 laser_vec;       /* 0x40 */
    Float range;                /* 0x4C */
    Float range2;               /* 0x50 */
    NJS_POINT3 arm_pos;         /* 0x54 */
    NJS_POINT3 arm_vec;         /* 0x60 */
    task *tp;                   /* 0x6C */
    Float arm_frame;            /* 0x70 */
    Sint32 unk74;               /* 0x74 */
    Sint32 unk78;               /* 0x78 */
    Sint32 unk7C;               /* 0x7C */
    NJS_ACTION *arm_action;     /* 0x80 */
    Float scope_frame;          /* 0x84 */
    Angle blink_ang;            /* 0x88 */
    Angle scope_ang;            /* 0x8C */
    Float body_y;               /* 0x90 */
    NJS_POINT3 muzzle[2];       /* 0x94 */
    Float spd_y;                /* 0xAC */
    E102_LINE line0;            /* 0xB0 */
    E102_LINE line1;            /* 0xD0 */
    Sint32 unkF0;               /* 0xF0 */
    task *lock_tp[32];          /* 0xF4 */
    task *shot_tp[32];          /* 0x174 */
    PL_ANALOG analog[8];        /* 0x1F4 */
    Float ground_y;             /* 0x234 ground height below (8C09222C); jet effect height here */
    Float altitude;             /* 0x238 */
    Sint32 unk23C;              /* 0x23C */
    Float step_dist;            /* 0x240 */
} E102WK;                       /* 0x244 */

SA1_ASSERT_SIZE(E102_LINE, 0x20);
SA1_ASSERT_SIZE(E102WK, 0x244);
SA1_ASSERT_OFFSET(E102WK, line0, 0xB0);
SA1_ASSERT_OFFSET(E102WK, analog, 0x1F4);
SA1_ASSERT_OFFSET(E102WK, ground_y, 0x234);

/* held buttons of player n (the literal pool holds B_8C754C08 + 8, which
 * the symbol map calls B_8C754C10) */
#define PAD_ON(n) (B_8C754C08[n].on)

/* positional-SE enable flag (MOV.B + TST) */
extern Sint8 B_8C7A2EA8;
/* set to 1 while E-102 hovers (MOV.B store) */
extern Sint8 B_8C7A2EA9;
/* MOV.B + CMP/EQ #1: no positional hover SE while it is 1 */
extern Sint8 B_8C7A2EC0;

void func_8C056860(NJS_VECTOR *v, NJS_VECTOR *front, Angle *ang);  /* direction -> angles relative to front */
void func_8C0D2D16(taskwk *twp, Float y);   /* E-102 jet effect (chunk 8C0D2C00) */

/*
 * Literal pool layout. SHC keeps one pool for the whole chunk and dumps it
 * at an unconditional branch once the oldest pending entry is far enough
 * back; where that happens changes branch forms. In the original the pool
 * pending from func_8C0D1672/func_8C0D18B8 (first entry used at 8C0D17E0)
 * is dumped inside func_8C0D1916 (8C0D1978), and the one pending from
 * func_8C0D1916 (first entry used at 8C0D1B8C) after the first RTS of
 * func_8C0D1C78 (8C0D1D18), which gives its far "BF/S; BRA; NOP" for the
 * equipment test. These stubs of the preceding chunk functions only
 * reproduce that pending pool (their __asm_stub_sink entry); the splicer
 * ignores stub bodies. These five stubs plus the C body of func_8C0D1916
 * put func_8C0D1916's three pools and the dump in func_8C0D1C78 where the
 * original has them.
 */
ASM_STUB(Sint32 func_8C0D11DC(taskwk *twp, motionwk *mwp, playerwk *pwp))
ASM_STUB(Sint32 func_8C0D14D6(taskwk *twp, motionwk *mwp, playerwk *pwp))
ASM_STUB(void func_8C0D15C0(taskwk *twp, motionwk *mwp, playerwk *pwp))
ASM_STUB(void func_8C0D1672(taskwk *twp, motionwk *mwp, playerwk *pwp))
ASM_STUB(void func_8C0D18B8(task *tp))   /* look task destructor (code_8C0D1672.c) */

/*
 * Matching notes: "v.y = v.z = 0.0f; v.x = 1.0f;" (as in Amy's
 * func_8C0DDA00) is what gives the original register allocation (ptwp R13,
 * &ang R12, info / ewp R11, cam / info R10, 0 / ttwp R9, func_8C07AF8C R8,
 * so the three func_8C07AF8C calls cross-jump into one JSR @R8) and the
 * spill slots of &ang[1] / &ang[2]; with "v.z = 0.0f; v.y = 0.0f;" SHC
 * picks another allocation (383 insns, 230 words differ).
 * The vector arithmetic is written inline (no njSubVector / njAddVector
 * calls, unlike Sonic's and Amy's versions).
 */
void func_8C0D1916(task *tp)
{
    Angle ang[3];
    NJS_POINT3 pos;
    NJS_VECTOR v;
    lookwk *lwp;
    taskwk *ptwp;
    eventwk *ewp;
    CCL_INFO *info;
    taskwk *cam;
    taskwk *ttwp;
    motionwk *pmwp;
    taskwk *twp;

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
        lwp->eyeL_y_min = -0x1B00;
        lwp->eyeR_y_max = 0x1B00;
        lwp->eyeR_y_min = -0x200;
        lwp->eyeL_z_max = 0x800;
        lwp->eyeL_z_min = -0xD00;
        lwp->eye_z_max = 0x800;
        lwp->eye_z_min = -0xD00;
        tp->dest = func_8C0D18B8;
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
        pos.x = cam->pos.x - info->center.x;
        pos.y = cam->pos.y - info->center.y;
        pos.z = cam->pos.z - info->center.z;
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
            pos.x = ewp->look_pos.x - info->center.x;
            pos.y = ewp->look_pos.y - info->center.y;
            pos.z = ewp->look_pos.z - info->center.z;
            func_8C056860(&pos, &v, ang);
            break;
        case 3:
            ttwp = ewp->look_tp->twp;
            v.y = v.z = 0.0f;
            v.x = 1.0f;
            PConvertVector_P2G(ptwp, &v);
            pos.x = ttwp->pos.x + ewp->look_pos.x - info->center.x;
            pos.y = ttwp->pos.y + ewp->look_pos.y - info->center.y;
            pos.z = ttwp->pos.z + ewp->look_pos.z - info->center.z;
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

Sint32 func_8C0D1C78(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    E102WK *ewk;

    if (!(pwp->equipment & 0x400)) {
        return 0;
    }
    if (PAD_ON((Uint8)twp->counter.b[0]) & D_8C19E390) {
        ewk = (E102WK *)pwp->free.ptr[0];
        pwp->acc.y = 0.0f;
        mwp->spd.y = (-0.39999998f < mwp->spd.y + 0.049999998f) ? mwp->spd.y + 0.049999998f : -0.39999998f;
        func_8C0D2D16(twp, ewk->ground_y);
        B_8C7A2EA9 = 1;
        if (B_8C7A2EC0 != 1 && B_8C7A2EA8) {
            func_8C09413A(0x315, 0, 0, 0, twp->pos.x, twp->pos.y, twp->pos.z);
        }
        return 1;
    } else {
        func_8C0945A0(0x315);
    }
    return 0;
}

Sint32 func_8C0D1D76(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    E102WK *ewk;

    if (!(pwp->equipment & 0x400)) {
        return 0;
    }
    if ((PAD_ON((Uint8)twp->counter.b[0]) & D_8C19E390) && mwp->spd.y <= -0.39999998f) {
        ewk = (E102WK *)pwp->free.ptr[0];
        pwp->acc.y = mwp->acc.y = 0.0f;
        mwp->spd.y = (-0.39999998f < mwp->spd.y + 0.049999998f) ? mwp->spd.y + 0.049999998f : -0.39999998f;
        func_8C0D2D16(twp, ewk->ground_y);
        B_8C7A2EA9 = 1;
        if (B_8C7A2EC0 != 1 && B_8C7A2EA8) {
            func_8C09413A(0x315, 0, 0, 0, twp->pos.x, twp->pos.y, twp->pos.z);
        }
        return 1;
    } else {
        func_8C0945A0(0x315);
    }
    return 0;
}
