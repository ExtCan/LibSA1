#include "sa1/sa1.h"

/*
 * E-102 Gamma lock-on marker (1ST_READ chunk 8C0D5600):
 *
 *  func_8C0D5600  marker disp: at twp->pos, colours both E-102 line lists
 *                 (ewk->line0/line1, 24 colours each) from red/yellow to
 *                 blue as the laser timer (ewk +0x10) runs down from 60,
 *                 and draws them scaled by twp->value.f * awp->scale,
 *                 rotated by (ang28, ang24) and (-ang24, -ang28).
 *  func_8C0D577C  marker exec (created by func_8C0D38C6 with a 0x34-byte
 *                 awp): mode 0 installs the dest, takes the size of the
 *                 target's first CCL shape (10.0f without a colliwk), gives
 *                 the target a 0x23C-byte mwp if it has none and links the
 *                 marker into it, creates the lock count child task
 *                 func_8C0D5CE0 (not for shapes with attr 0x100000), plays
 *                 SE 0x31A and installs the disp; mode 1 grows the marker
 *                 (value.f += 0.015 up to 0.15) and follows the centre of
 *                 the target's first shape (func_8C02FC12). Every frame it
 *                 spins (ang24 += 0x200, ang28 += 0x300), registers itself
 *                 in ewk->lockon_tp[] and runs its children. The marker is
 *                 freed as soon as the target or its taskwk is gone.
 *  func_8C0D5944  marker dest: clears the E-102 work's last-marker link,
 *                 releases the shot fired at the target (func_8C0D4996
 *                 when twp->flag 0x100 was set by the release 8C0D5D90,
 *                 else clears the shot's target) and unlinks the marker
 *                 from the target's work.
 *
 * Matching notes: SHC reuses a tested field only inside one expression, so
 * the original's reloads of ttwp->cwp / shot->twp / target->mwp come from
 * separate if statements (no &&, no ?:, no assignment in a condition, which
 * also costs a stack temporary). The colour clamp must be written inline
 * ((Float)(60 - x > 0 ? 60 - x : 0)); with a variable SHC picks R3, not R2.
 */

/* E-102 line draw list (0x20 bytes, code_8C0D54A0.c); local copy until a
 * shared E102 header exists */
typedef struct {
    NJS_POINT3 *src_p;          /* 0x00 */
    NJS_COLOR *src_col;         /* 0x04 */
    Sint16 *idx;                /* 0x08 */
    Sint16 num;                 /* 0x0C */
    Sint16 pad0E;               /* 0x0E */
    NJS_POINT3COL p3c;          /* 0x10 */
} E102_LINE;                    /* 0x20 */

/*
 * E-102 work (pwp->free.ptr[0]; E102Main 8C0CC0F2: CAlloc(1, 0x244)).
 * Local copy of the merged layout of code_8C0D32A0.c / code_8C0D5A00.c.
 */
typedef struct {
    Sint8 mode;                 /* 0x00 */
    Sint8 smode;                /* 0x01 */
    Sint16 unk02;               /* 0x02 */
    Sint8 nb_lockon;            /* 0x04 entries in lockon_tp (MOV.B, ++ by every marker exec) */
    Sint8 pad05[3];             /* 0x05 */
    Sint16 timer08;             /* 0x08 */
    Sint16 timer0A;             /* 0x0A */
    Sint16 timer0C;             /* 0x0C */
    Sint16 unk0E;               /* 0x0E */
    Sint16 laser_timer;         /* 0x10 marker colour fades with 60 - laser_timer */
    Sint16 lockon_timer;        /* 0x12 cleared by the marker dest */
    Sint16 unk14;               /* 0x14 */
    Sint16 flag;                /* 0x16 */
    Float hover_y;              /* 0x18 */
    Sint32 unk1C;               /* 0x1C */
    Angle3 jet_ang;             /* 0x20 */
    Angle jet_phase;            /* 0x2C */
    task *target_tp;            /* 0x30 last lock-on marker task (func_8C0D38C6) */
    NJS_POINT3 laser_pos;       /* 0x34 */
    NJS_POINT3 laser_vec;       /* 0x40 */
    Float laser_len;            /* 0x4C */
    Float laser_len2;           /* 0x50 */
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
    Sint32 unkAC;               /* 0xAC */
    E102_LINE line0;            /* 0xB0 colours rewritten by the marker disp (p3c.col, 24 entries) */
    E102_LINE line1;            /* 0xD0 */
    Sint32 unkF0;               /* 0xF0 */
    task *lockon_tp[32];        /* 0xF4 lock-on marker tasks, nb_lockon entries */
    Sint32 unk174[46];          /* 0x174 */
    Sint32 unk22C;              /* 0x22C */
    Float unk230;               /* 0x230 */
    Float ground_y;             /* 0x234 */
    Float altitude;             /* 0x238 */
    Sint32 unk23C;              /* 0x23C */
    Float step_dist;            /* 0x240 */
} E102WK;                       /* 0x244 */

/* awp of a lock-on marker task (CAlloc(1, 0x34) by func_8C0D38C6; local
 * copy of E102_LOCKWK, code_8C0D37F4.c / code_8C0D46CE.c) */
typedef struct {
    Sint8 mode;                 /* 0x00 */
    Sint8 count;                /* 0x01 lock-on number (copied to the count child task) */
    Sint16 unk02;               /* 0x02 */
    Sint16 equipment;           /* 0x04 */
    Sint16 unk06;               /* 0x06 */
    task *target_tp;            /* 0x08 locked-on task */
    taskwk *ptwp;               /* 0x0C E-102's taskwk */
    E102WK *ewk;                /* 0x10 */
    task *shot_tp;              /* 0x14 shot task fired at the target */
    Sint32 unk18;               /* 0x18 */
    Float scale;                /* 0x1C size of the target's first CCL shape (10.0f without a colliwk) */
    Sint32 unk20;               /* 0x20 */
    Angle ang24;                /* 0x24 spin, += 0x200 per frame (njRotateY) */
    Angle ang28;                /* 0x28 spin, += 0x300 per frame (njRotateZ) */
    task *group_tp;             /* 0x2C */
    TaskFuncPtr target_exec;    /* 0x30 */
} E102_LOCKWK;                  /* 0x34 */

/*
 * tp->mwp of a task E-102 locks on (an enemy work, larger than motionwk).
 * Only the prefix used by the E-102 code is known (same layout as the local
 * copies in code_8C0D32A0.c and code_8C0D5D90.c).
 */
typedef struct {
    motionwk mw;                /* 0x00 */
    task *marker_tp;            /* 0x40 lock-on marker task, NULL if not locked */
    TaskFuncPtr dest;           /* 0x44 original tp->dest (hooked by func_8C0D332C) */
    Sint32 unk48;               /* 0x48 */
    Sint16 unk4C;               /* 0x4C */
    Uint16 flag;                /* 0x4E */
} E102_TARGETWK;                /* 0x50 known prefix */

/*
 * Storage the marker exec CAllocs for a target without a mwp: the whole
 * enemy work is 0x23C bytes on the DC (CAlloc(1, 0x23C)), of which only the
 * E102_TARGETWK prefix is known. On the PC the union is at least the native
 * size of the prefix.
 */
typedef union {
    E102_TARGETWK wk;
    Uint8 bytes[0x23C];         /* DC size of the enemy work */
} E102_TARGETBUF;

SA1_ASSERT_SIZE(E102_LINE, 0x20);
SA1_ASSERT_OFFSET(E102_LINE, p3c, 0x10);
SA1_ASSERT_SIZE(E102WK, 0x244);
SA1_ASSERT_OFFSET(E102WK, nb_lockon, 0x04);
SA1_ASSERT_OFFSET(E102WK, laser_timer, 0x10);
SA1_ASSERT_OFFSET(E102WK, lockon_timer, 0x12);
SA1_ASSERT_OFFSET(E102WK, target_tp, 0x30);
SA1_ASSERT_OFFSET(E102WK, line0, 0xB0);
SA1_ASSERT_OFFSET(E102WK, line1, 0xD0);
SA1_ASSERT_OFFSET(E102WK, lockon_tp, 0xF4);
SA1_ASSERT_SIZE(E102_LOCKWK, 0x34);
SA1_ASSERT_OFFSET(E102_LOCKWK, count, 0x01);
SA1_ASSERT_OFFSET(E102_LOCKWK, target_tp, 0x08);
SA1_ASSERT_OFFSET(E102_LOCKWK, ewk, 0x10);
SA1_ASSERT_OFFSET(E102_LOCKWK, shot_tp, 0x14);
SA1_ASSERT_OFFSET(E102_LOCKWK, scale, 0x1C);
SA1_ASSERT_OFFSET(E102_LOCKWK, ang24, 0x24);
SA1_ASSERT_OFFSET(E102_LOCKWK, ang28, 0x28);
SA1_ASSERT_SIZE(E102_TARGETWK, 0x50);
SA1_ASSERT_SIZE(E102_TARGETBUF, 0x23C);
SA1_ASSERT_OFFSET(E102_TARGETWK, marker_tp, 0x40);

extern E102WK *B_8C7A2EBC;      /* E-102 work of this frame */
extern Sint8 B_8C7A2EA8;        /* positional-SE enable flag (E102Main) */

void func_8C0D553A(E102_LINE *l, Uint32 attr);  /* njDrawLine3D of a line list (code_8C0D54A0.c) */
void func_8C02FC12(taskwk *twp, CCL_INFO *ci, NJS_POINT3 *out);   /* shape centre */
void func_8C0D5CE0(task *tp);   /* lock count child task exec (code_8C0D5BE0.c) */
void func_8C0D4996(task *tp, NJS_POINT3 *v);    /* release a shot task (code_8C0D46CE.c) */
void func_8C0D5944(task *tp);

void func_8C0D5600(task *tp)
{
    taskwk *twp;
    E102_LOCKWK *awp;
    E102WK *ewk;
    Float s;
    Float a;
    Uint8 r;
    Uint8 b;
    NJS_COLOR *col0;
    NJS_COLOR *col1;
    Sint32 i;
    NJS_COLOR c0;
    NJS_COLOR c1;

    twp = tp->twp;
    awp = (E102_LOCKWK *)tp->awp;
    if (B_8C754EC8 != 0) {
        return;
    }
    njPushMatrix(NULL);
    ewk = awp->ewk;
    s = twp->value.f * awp->scale;
    njTranslateV(NULL, &twp->pos);
    njColorBlendingMode(0, 8);
    njColorBlendingMode(1, 6);
    a = (Float)(60 - ewk->laser_timer > 0 ? 60 - ewk->laser_timer : 0) * 0.01666666614f;
    r = SA1_FTOI((1.0f - a) * 255.0f);
    b = SA1_FTOI(a * 255.0f);
    col0 = ewk->line0.p3c.col;
    col1 = ewk->line1.p3c.col;
    c0.color = (r << 16) | b | 0xFF000000;
    c1.color = (r << 8) | b | (r << 16) | 0xFF000000;
    for (i = 0; i < 24; i++) {
        *col0++ = c0;
        *col1++ = c1;
    }
    njPushMatrix(NULL);
    awp->ang28 ? njRotateZ(NULL, awp->ang28) : (void)0;
    awp->ang24 ? njRotateY(NULL, awp->ang24) : (void)0;
    njScale(NULL, s, s, s);
    func_8C0D553A(&ewk->line0, 0x40);
    njPopMatrix(1);
    njPushMatrix(NULL);
    -awp->ang24 ? njRotateZ(NULL, -awp->ang24) : (void)0;
    -awp->ang28 ? njRotateY(NULL, -awp->ang28) : (void)0;
    njScale(NULL, s, s, s);
    func_8C0D553A(&ewk->line1, 0x40);
    njPopMatrix(1);
    njPopMatrix(1);
}

void func_8C0D577C(task *tp)
{
    taskwk *twp;
    E102_LOCKWK *awp;
    taskwk *ttwp;
    task *target;
    E102_TARGETWK *mwp;
    task *ctp;

    twp = tp->twp;
    awp = (E102_LOCKWK *)tp->awp;
    switch (twp->mode) {
    case 0:
        tp->dest = func_8C0D5944;
        if (awp->target_tp == NULL) {
            FreeTask(tp);
            return;
        }
        ttwp = awp->target_tp->twp;
        if (ttwp == NULL) {
            FreeTask(tp);
            return;
        }
        twp->value.f = 0.0f;
        if (ttwp->cwp != NULL) {
            awp->scale = CWP(ttwp)->info->a;
        } else {
            awp->scale = 10.0f;
        }
        target = awp->target_tp;
        mwp = (E102_TARGETWK *)target->mwp;
        if (mwp == NULL) {
            mwp = (E102_TARGETWK *)CAlloc(1, sizeof(E102_TARGETBUF));
            target->mwp = &mwp->mw;
        }
        mwp->marker_tp = tp;
        if (ttwp->cwp != NULL) {
            if (!(CWP(ttwp)->info->attr & 0x100000)) {
                ctp = CreateChildTask(2, func_8C0D5CE0, tp);
                if (ctp != NULL) {
                    ctp->twp->value.b[0] = awp->count;
                    ctp->twp->timer.ptr = &twp->pos;
                }
            }
        }
        if (B_8C7A2EA8) {
            func_8C09413A(0x31A, 0, 0, 0, twp->pos.x, twp->pos.y, twp->pos.z);
        }
        tp->disp = func_8C0D5600;
        twp->mode = 1;
    case 1:
        if (twp->value.f < 0.1499999948f) {
            twp->value.f += 0.01499999897f;
        }
        if (awp->target_tp == NULL) {
            FreeTask(tp);
            return;
        }
        ttwp = awp->target_tp->twp;
        if (ttwp == NULL) {
            FreeTask(tp);
            return;
        }
        if (ttwp->cwp != NULL) {
            func_8C02FC12(ttwp, CWP(ttwp)->info, &twp->pos);
        }
        break;
    }
    awp->ang24 += 0x200;
    awp->ang28 += 0x300;
    B_8C7A2EBC->lockon_tp[B_8C7A2EBC->nb_lockon++] = tp;
    func_8C0D5600(tp);
    func_8C098E98(tp);
}

void func_8C0D5944(task *tp)
{
    E102_LOCKWK *awp;
    task *target;
    task *shot;
    E102_TARGETWK *mwp;

    awp = (E102_LOCKWK *)tp->awp;
    shot = awp->shot_tp;
    target = awp->target_tp;
    if (B_8C7A2EBC->target_tp == tp) {
        B_8C7A2EBC->target_tp = NULL;
        B_8C7A2EBC->lockon_timer = 0;
    }
    if (shot != NULL) {
        if (tp->twp->flag & 0x100) {
            func_8C0D4996(shot, NULL);
        } else if (shot->twp != NULL) {
            shot->twp->value.tp = NULL;
        }
    }
    if (target != NULL) {
        mwp = (E102_TARGETWK *)target->mwp;
        if (mwp != NULL) {
            mwp->marker_tp = NULL;
        }
    }
}
