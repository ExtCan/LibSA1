#include "sa1/sa1.h"

/*
 * E-102 Gamma (1ST_READ chunk 8C0D24E0), sound helpers of the player
 * (E-102 counterparts of Sonic's footstep / hover sounds):
 *
 *  func_8C0D24E0  footstep sound (E102Main 8C0CCA18, args twp, pwp, ewk):
 *                 accumulates |pwp->spd.x| in ewk->step_dist; every 50
 *                 units plays the positional SE 0x30E at twp->pos when
 *                 B_8C7A2EA8 is set (tail call).
 *  func_8C0D2538  hover start (E102Main 8C0CCE52 / E102ModeMachine, args
 *                 twp, ewk): if ewk->flag bit 0 is clear, plays the
 *                 positional SE 0x312 (when B_8C7A2EA8) and sets the bit.
 *  func_8C0D257E  hover stop (E102ModeMachine, func_8C0D0528): if bit 0 is
 *                 set, stops SE 0x312 (8C0945A0), plays the positional
 *                 SE 0x310 (when B_8C7A2EA8) and clears the bit.
 */

/*
 * E-102 line draw list (0x20 bytes, see code_8C0CFA6E.c / code_8C0CFACE.c).
 * Not used here; kept so E102WK has its full native layout.
 */
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
 * Layout merged from code_8C0CFACE.c (0x00-0xEF), code_8C0D1916.c (0x234)
 * and this chunk (0x16 flag, 0x240 step_dist). Local copy until a shared
 * E102WK exists (see type notes).
 */
typedef struct {
    Sint8 mode;                 /* 0x00 */
    Sint8 smode;                /* 0x01 */
    Sint16 unk02;               /* 0x02 */
    Sint32 unk04;               /* 0x04 */
    Sint16 timer08;             /* 0x08 */
    Sint16 timer0A;             /* 0x0A */
    Sint16 timer0C;             /* 0x0C */
    Sint16 unk0E;               /* 0x0E */
    Sint16 unk10;               /* 0x10 */
    Sint16 lockon_timer;        /* 0x12 */
    Sint16 unk14;               /* 0x14 */
    Sint16 flag;                /* 0x16 bit 0: hover SE 0x312 playing (MOV.W, OR #1 / AND #-2) */
    Float hover_y;              /* 0x18 */
    Sint32 unk1C;               /* 0x1C */
    Angle3 jet_ang;             /* 0x20 */
    Angle jet_phase;            /* 0x2C */
    task *target_tp;            /* 0x30 */
    NJS_POINT3 laser_pos;       /* 0x34 */
    NJS_POINT3 laser_vec;       /* 0x40 */
    Sint32 unk4C;               /* 0x4C */
    Sint32 unk50;               /* 0x50 */
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
    E102_LINE line0;            /* 0xB0 */
    E102_LINE line1;            /* 0xD0 */
    Sint32 unkF0[81];           /* 0xF0 */
    Float f234;                 /* 0x234 (code_8C0D1916.c) */
    Sint32 unk238[2];           /* 0x238 */
    Float step_dist;            /* 0x240 footstep distance accumulator (func_8C0D24E0) */
} E102WK;                       /* 0x244 */

SA1_ASSERT_SIZE(E102_LINE, 0x20);
SA1_ASSERT_SIZE(E102WK, 0x244);
SA1_ASSERT_OFFSET(E102WK, flag, 0x16);
SA1_ASSERT_OFFSET(E102WK, tp, 0x6C);
SA1_ASSERT_OFFSET(E102WK, line0, 0xB0);
SA1_ASSERT_OFFSET(E102WK, f234, 0x234);
SA1_ASSERT_OFFSET(E102WK, step_dist, 0x240);

/* positional-SE enable flag (MOV.B + TST) */
extern Sint8 B_8C7A2EA8;

void func_8C0D24E0(taskwk *twp, playerwk *pwp, E102WK *ewk)
{
    ewk->step_dist += _builtin_fabsf(pwp->spd.x);
    if (ewk->step_dist > 50.0f) {
        ewk->step_dist -= 50.0f;
        if (B_8C7A2EA8) {
            func_8C09413A(0x30E, 0, 0, 0, twp->pos.x, twp->pos.y, twp->pos.z);
        }
    }
}

void func_8C0D2538(taskwk *twp, E102WK *ewk)
{
    if (!(ewk->flag & 1)) {
        if (B_8C7A2EA8) {
            func_8C09413A(0x312, 0, 0, 0, twp->pos.x, twp->pos.y, twp->pos.z);
        }
        ewk->flag |= 1;
    }
}

void func_8C0D257E(taskwk *twp, E102WK *ewk)
{
    if (ewk->flag & 1) {
        func_8C0945A0(0x312);
        if (B_8C7A2EA8) {
            func_8C09413A(0x310, 0, 0, 0, twp->pos.x, twp->pos.y, twp->pos.z);
        }
        ewk->flag &= ~1;
    }
}
