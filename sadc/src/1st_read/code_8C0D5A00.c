#include "sa1/sa1.h"

/*
 * E-102 Gamma lock-on marker, release animation (1ST_READ chunk 8C0D5A00):
 *
 *  func_8C0D5A00  marker disp while it is released: scales the marker by
 *                 twp->value.f * awp->scale at twp->pos, fades both E-102
 *                 line lists (ewk->line0/line1 colours, 16 each) from red/
 *                 yellow to blue over 60 frames (twp->timer.l) and draws
 *                 them rotated by (ang28, ang24) and (-ang24, -ang28).
 *  func_8C0D5B54  marker exec installed by func_8C0D5D90 (twp->timer.l = 0)
 *                 when the target is released: 60 frames of spin
 *                 (ang24 += 0x200, ang28 += 0x300), then FreeTask.
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
 * Local copy of the merged layout of code_8C0D32A0.c; only line0/line1 are
 * used here.
 */
typedef struct {
    Sint8 mode;                 /* 0x00 */
    Sint8 smode;                /* 0x01 */
    Sint16 unk02;               /* 0x02 */
    Sint8 nb_lockon;            /* 0x04 */
    Sint8 pad05[3];             /* 0x05 */
    Sint16 timer08;             /* 0x08 */
    Sint16 timer0A;             /* 0x0A */
    Sint16 timer0C;             /* 0x0C */
    Sint16 unk0E;               /* 0x0E */
    Sint16 laser_timer;         /* 0x10 */
    Sint16 lockon_timer;        /* 0x12 */
    Sint16 unk14;               /* 0x14 */
    Sint16 flag;                /* 0x16 */
    Float hover_y;              /* 0x18 */
    Sint32 unk1C;               /* 0x1C */
    Angle3 jet_ang;             /* 0x20 */
    Angle jet_phase;            /* 0x2C */
    task *target_tp;            /* 0x30 */
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
    E102_LINE line0;            /* 0xB0 colours rewritten here (p3c.col, 16 entries) */
    E102_LINE line1;            /* 0xD0 */
    Sint32 unkF0;               /* 0xF0 */
    task *lockon_tp[32];        /* 0xF4 */
    Sint32 unk174[46];          /* 0x174 */
    Sint32 unk22C;              /* 0x22C */
    Float unk230;               /* 0x230 */
    Float ground_y;             /* 0x234 */
    Float altitude;             /* 0x238 */
    Sint32 unk23C;              /* 0x23C */
    Float step_dist;            /* 0x240 */
} E102WK;                       /* 0x244 */

/* awp of a lock-on marker task (CAlloc(1, 0x34) by func_8C0D38C6; local
 * copy of code_8C0D32A0.c) */
typedef struct {
    Sint8 mode;                 /* 0x00 */
    Sint8 num;                  /* 0x01 */
    Sint16 unk02;               /* 0x02 */
    Sint16 flag;                /* 0x04 */
    Sint16 unk06;               /* 0x06 */
    task *target;               /* 0x08 */
    taskwk *ptwp;               /* 0x0C */
    E102WK *ewk;                /* 0x10 */
    task *unk14;                /* 0x14 */
    Sint32 unk18;               /* 0x18 */
    Float scale;                /* 0x1C size factor (func_8C0D5A00: njScale by twp->value.f * scale) */
    Sint32 unk20;               /* 0x20 */
    Angle ang24;                /* 0x24 spin, += 0x200 per frame while released (njRotateY) */
    Angle ang28;                /* 0x28 spin, += 0x300 per frame while released (njRotateZ) */
    task *unk2C;                /* 0x2C */
    TaskFuncPtr target_exec;    /* 0x30 */
} E102_MARKERWK;                /* 0x34 */

SA1_ASSERT_SIZE(E102_LINE, 0x20);
SA1_ASSERT_OFFSET(E102_LINE, p3c, 0x10);
SA1_ASSERT_SIZE(E102WK, 0x244);
SA1_ASSERT_OFFSET(E102WK, line0, 0xB0);
SA1_ASSERT_OFFSET(E102WK, line1, 0xD0);
SA1_ASSERT_SIZE(E102_MARKERWK, 0x34);
SA1_ASSERT_OFFSET(E102_MARKERWK, scale, 0x1C);
SA1_ASSERT_OFFSET(E102_MARKERWK, ang24, 0x24);
SA1_ASSERT_OFFSET(E102_MARKERWK, ang28, 0x28);

extern E102WK *B_8C7A2EBC;      /* E-102 work of this frame */

void func_8C0D553A(E102_LINE *l, Uint32 attr);  /* njDrawLine3D of a line list (code_8C0D54A0.c) */

/* colour channel (0..255) of an intensity 0..1; the CSE of the repeated
 * E102_COL8(a) uses is what gives the original register allocation */
#define E102_COL8(f) ((Uint8)SA1_FTOI((f) * 255.0f) & 0xFF)

void func_8C0D5A00(task *tp)
{
    taskwk *twp;
    E102_MARKERWK *awp;
    E102WK *ewk;
    NJS_COLOR *col0;
    NJS_COLOR *col1;
    Float a;
    Float s;
    Sint32 i;
    NJS_COLOR c0;
    NJS_COLOR c1;

    twp = tp->twp;
    awp = (E102_MARKERWK *)tp->awp;
    a = (Float)(60 - twp->timer.l) * 0.016666666f;
    njPushMatrix(NULL);
    ewk = B_8C7A2EBC;
    s = twp->value.f * awp->scale;
    njTranslateV(NULL, &twp->pos);
    njScale(NULL, s, s, s);
    njColorBlendingMode(0, 8);
    njColorBlendingMode(1, 6);
    c0.color = (E102_COL8(a) << 16) | E102_COL8(1.0f - a) | 0xFF000000;
    c1.color = (E102_COL8(a) << 8) | E102_COL8(1.0f - a) | (E102_COL8(a) << 16) | 0xFF000000;
    col0 = ewk->line0.p3c.col;
    col1 = ewk->line1.p3c.col;
    for (i = 0; i < 16; i++) {
        *col0++ = c0;
        *col1++ = c1;
    }
    njPushMatrix(NULL);
    awp->ang28 ? njRotateZ(NULL, awp->ang28) : (void)0;
    awp->ang24 ? njRotateY(NULL, awp->ang24) : (void)0;
    func_8C0D553A(&ewk->line0, 0x40);
    njPopMatrix(1);
    njPushMatrix(NULL);
    -awp->ang24 ? njRotateZ(NULL, -awp->ang24) : (void)0;
    -awp->ang28 ? njRotateY(NULL, -awp->ang28) : (void)0;
    func_8C0D553A(&ewk->line1, 0x40);
    njPopMatrix(1);
    njPopMatrix(1);
}

void func_8C0D5B54(task *tp)
{
    taskwk *twp;
    E102_MARKERWK *awp;

    twp = tp->twp;
    awp = (E102_MARKERWK *)tp->awp;
    if (twp->timer.l >= 60) {
        FreeTask(tp);
        return;
    }
    twp->timer.l++;
    awp->ang24 += 0x200;
    awp->ang28 += 0x300;
    tp->disp = func_8C0D5A00;
    func_8C0D5A00(tp);
}
