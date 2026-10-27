#include "sa1/sa1.h"

/*
 * E-102 Gamma (1ST_READ chunk 8C0D2EC0-8C0D32A0):
 *
 *  func_8C0D2EC0  ring of 12 smoke puffs around E-102 (E102ModeMachine,
 *                 args twp, mwp): puff size and spread scale with
 *                 |mwp->spd.y| (landing dust); spawned by 8C0466E8 from
 *                 the template D_8C422CC0.
 *  func_8C0D317E  per-frame stick history (E102Main, args ewk, pno, flag):
 *                 shifts ewk->analog[] down by one, appends this frame's
 *                 B_8C754DCC[pno]; with flag (ewk->mode == 3) the player's
 *                 analog is replaced by the oldest entry (input delay).
 *  func_8C0D3220  restore B_8C754DCC[pno] from the newest history entry
 *                 (E102ModeMachine).
 *  func_8C0D323E  1 if the E-102 work mode of tp is 2 or 3 (func_8C084BA0).
 *  func_8C0D325E  playertwp[pno]->mode = 45 (objexec_O_AXPNL).
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
 * Layout from code_8C0D24E0.c plus the stick history at 0x1F4 (this chunk).
 * Local copy until a shared E102WK exists (see type notes).
 */
typedef struct {
    Sint8 mode;                 /* 0x00 (8C0D323E: MOV.B, == 2 / == 3) */
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
    Sint16 flag;                /* 0x16 */
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
    Sint32 unkF0[65];           /* 0xF0 */
    PL_ANALOG analog[8];        /* 0x1F4 stick history, [7] newest (8C0D317E, 8C0D3220) */
    Float f234;                 /* 0x234 */
    Sint32 unk238[2];           /* 0x238 */
    Float step_dist;            /* 0x240 */
} E102WK;                       /* 0x244 */

SA1_ASSERT_SIZE(E102_LINE, 0x20);
SA1_ASSERT_SIZE(E102WK, 0x244);
SA1_ASSERT_OFFSET(E102WK, line0, 0xB0);
SA1_ASSERT_OFFSET(E102WK, analog, 0x1F4);
SA1_ASSERT_OFFSET(E102WK, f234, 0x234);

/*
 * Smoke puff template read by 8C0466E8 (0x3C bytes): it creates an effect
 * task (exec 8C0464EE) and copies pos, spd and argb into it; size scales
 * the task's size, the template pointer is kept at task work +0x44.
 */
typedef struct {
    Float size;                 /* 0x00 template 1.0 */
    Float unk04[4];             /* 0x04 template 0.03, 0.19, 0.05, 0.025 */
    NJS_POINT3 pos;             /* 0x14 */
    NJS_POINT3 spd;             /* 0x20 */
    Float argb[4];              /* 0x2C template 0.3, 0.8, 0.8, 1.0 */
} SMOKE_PARAM;                  /* 0x3C */

SA1_ASSERT_SIZE(SMOKE_PARAM, 0x3C);
SA1_ASSERT_OFFSET(SMOKE_PARAM, pos, 0x14);
SA1_ASSERT_OFFSET(SMOKE_PARAM, spd, 0x20);

extern SMOKE_PARAM D_8C422CC0;          /* E-102 landing smoke template */
void func_8C0466E8(SMOKE_PARAM *param); /* create a smoke puff task */

/*
 * c + a * b at an FMAC site, written with the addend first. Same fused
 * operation as SA1_FMAC(a, b, c) (fmaf on PC), but SHC allocates the
 * loop-invariant constants of func_8C0D2EC0 differently for "a * b + c"
 * (7.0f and 1/32768 swap FR13/FR14), so SA1_FMAC cannot be used for the
 * two position terms.
 */
#if defined(TARGET_PC)
#define FMAC_C(c, a, b) SA1_FMAC((a), (b), (c))
#else
#define FMAC_C(c, a, b) ((c) + (a) * (b))
#endif

/*
 * FMAC audit: SHC unrolls the 12-iteration loop of func_8C0D2EC0 4x, clang
 * does not. The 5 fused operations of the loop body (size, pos.x, pos.z,
 * spd.x, spd.z) are the 5 FMACs of each unrolled copy, 20 in the asm.
 */
void func_8C0D2EC0(taskwk *twp, motionwk *mwp)
{
    Sint32 i;
    Angle ang;
    Float f;

    for (i = 0; i < 12; i++) {
        ang = SA1_FTOI((Float)i * 5461.3326f);  /* i * 65536 / 12, in range */
        f = _builtin_fabsf(mwp->spd.y) * 1.0f;
        D_8C422CC0.size = SA1_FMAC(f, 0.099999996f, 0.5f);
        D_8C422CC0.pos.x = FMAC_C(twp->pos.x, njCos(ang), 7.0f);
        D_8C422CC0.pos.y = twp->pos.y + 1.5f;
        D_8C422CC0.pos.z = FMAC_C(twp->pos.z, njSin(ang), 7.0f);
        D_8C422CC0.spd.x = f * SA1_FMAC((Float)rand(), 3.0517578125e-05f, -0.5f);
        D_8C422CC0.spd.y = 0.0f;
        D_8C422CC0.spd.z = f * SA1_FMAC((Float)rand(), 3.0517578125e-05f, -0.5f);
        func_8C0466E8(&D_8C422CC0);
    }
}

void func_8C0D317E(E102WK *ewk, Sint32 pno, Sint32 flag)
{
    PL_ANALOG *p;
    Sint32 i;

    p = ewk->analog;
    for (i = 0; i < 7; i++) {
        p[0] = p[1];
        p++;
    }
    *p = B_8C754DCC[pno];
    if (flag) {
        B_8C754DCC[pno].ang = ewk->analog[0].ang;
        B_8C754DCC[pno].stroke = ewk->analog[0].stroke;
    }
}

void func_8C0D3220(E102WK *ewk, Sint32 pno)
{
    B_8C754DCC[pno].ang = ewk->analog[7].ang;
    B_8C754DCC[pno].stroke = ewk->analog[7].stroke;
}

Sint32 func_8C0D323E(task *tp)
{
    E102WK *ewk;
    motionwk *mwp;
    playerwk *pwp;

    mwp = tp->mwp;
    pwp = mwp->work.pwp;
    ewk = (E102WK *)pwp->free.ptr[0];
    return ewk->mode == 2 || ewk->mode == 3;
}

void func_8C0D325E(Sint32 pno)
{
    taskwk *twp;

    twp = playertwp[pno];
    twp->mode = 45;
}
