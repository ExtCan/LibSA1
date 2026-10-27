#include "sa1/sa1.h"

/*
 * E-102 Gamma (1ST_READ chunk 8C0D2080-8C0D24E0; same translation unit as
 * the E-102 chunk 8C0CBE00), called by E102Main 8C0CBE00 / E102ModeMachine
 * 8C0CD0A8 and the E-102 display 8C0CFACE:
 *
 *  func_8C0D2080  tail call of the air acceleration 8C06E904.
 *  func_8C0D2086  E-102 skid/slide speed: rotation from input (8C0D2156),
 *                 spd.x += stroke * run_accel / 2, without input also
 *                 + slow_down (x -1.5 while moving backwards); while
 *                 nocontimer runs slow_down is subtracted, otherwise spd.x
 *                 is limited to rush_speed; then limited to -rush_speed;
 *                 spd.y = -3, spd.z = 0, acc = 0.
 *  func_8C0D2150  tail call of 8C070EDA (braking acceleration).
 *  func_8C0D2156  tail call of 8C071700 (rotation from input).
 *  func_8C0D215C  motion callback of the E-102 model (8C0CFACE passes it
 *                 to func_8C60D1F8): node D_8C427688 is rotated about Y by
 *                 the E-102 scope_ang (when non-zero), node D_8C42A3E8 is
 *                 moved up by twice body_y.
 *  func_8C0D2210  E-102 stop step: lowers twp->pos by the player height
 *                 (converted to global space), then steps mj.nframe by +-1
 *                 in the direction of spd.x (wrapping in 0..31); when a
 *                 half cycle ends (frame 15 or 31) without stick input it
 *                 switches to mode 47 with spd.x = 0. With speed but no
 *                 drive (mwp->rad == 0) it switches at frame 15 / >= 31.
 *
 * Matching notes:
 *  - SHC -speed inlines small same-file functions automatically. The
 *    original keeps the BSR from func_8C0D2086 to func_8C0D2156, so the
 *    thunk carries two dead "if (0) return;" blocks: they only raise SHC's
 *    inline size estimate (it is taken before dead code is removed) and
 *    compile to nothing, so the thunk itself still matches.
 *  - func_8C0D215C: the rotation is the "a ? njRotateY(NULL, a) : (void)0"
 *    idiom (also used by 8C0CFACE): SHC keeps the common subexpression
 *    B_8C7A2EBC->scope_ang in a 4-byte stack slot (stored in the BT/S
 *    delay slot, reloaded for the call), which also keeps that call from
 *    becoming a tail call. With the first arm ending in "return;" and the
 *    second test a separate if, SHC loads B_8C7A2EBC in each arm (two
 *    literal-pool entries) instead of hoisting one load into R14, and the
 *    njTranslate call stays a tail call (JMP after the epilogue).
 *  - func_8C0D2210's far-branch choice at 8C0D22F6 depends on the size of
 *    the literal pool dumped inside it. func_8C0D2086 therefore stays
 *    compiled in the SHC build (an 8-call ASM_STUB body moves the pools and
 *    func_8C0D2210 stops matching); its NON_MATCHING code has the same size
 *    and pool entries as the original, and the splicer keeps the original
 *    assembly for it because of the ASM_STUB marker in its comment.
 */

/*
 * E-102 line draw list and E-102 work (pwp->free.ptr[0]; E102Main 8C0CC0F2:
 * CAlloc(1, 0x244); E102Main also stores it in B_8C7A2EBC, 8C0CCF38).
 * Same definition as code_8C0CBE00.c (no shared header yet); only
 * scope_ang (0x8C) and body_y (0x90) are used here.
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
    Sint8 nb_target;            /* 0x02 valid markers (func_8C0D3C00) */
    Sint8 unk03;                /* 0x03 */
    Sint8 nb_lock;              /* 0x04 entries of lock_tp[] */
    Sint8 nb_shot;              /* 0x05 entries of shot_tp[] */
    Sint8 arm_idx;              /* 0x06 index into D_8C422D90 */
    Sint8 unk07;                /* 0x07 */
    Sint16 timer08;             /* 0x08 frame counter */
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
    Float range;                /* 0x4C laser length */
    Float range2;               /* 0x50 range squared */
    NJS_POINT3 arm_pos;         /* 0x54 */
    NJS_POINT3 arm_vec;         /* 0x60 */
    task *tp;                   /* 0x6C child task */
    Float arm_frame;            /* 0x70 */
    Sint32 unk74;               /* 0x74 */
    Sint32 unk78;               /* 0x78 */
    Sint32 unk7C;               /* 0x7C */
    NJS_ACTION *arm_action;     /* 0x80 */
    Float scope_frame;          /* 0x84 */
    Angle blink_ang;            /* 0x88 */
    Angle scope_ang;            /* 0x8C scope/arm yaw; node D_8C427688 Y rotation (8C0D215C, 0 = none) */
    Float body_y;               /* 0x90 node D_8C42A3E8 Y offset / 2 (8C0D215C; 8C0CFACE uses -2x) */
    NJS_POINT3 muzzle[2];       /* 0x94 */
    Float spd_y;                /* 0xAC */
    E102_LINE line0;            /* 0xB0 */
    E102_LINE line1;            /* 0xD0 */
    Sint32 unkF0;               /* 0xF0 */
    task *lock_tp[32];          /* 0xF4 lock-on marker tasks */
    task *shot_tp[32];          /* 0x174 laser shot tasks */
    PL_ANALOG analog[8];        /* 0x1F4 stick history */
    Float ground_y;             /* 0x234 */
    Float altitude;             /* 0x238 */
    Sint32 unk23C;              /* 0x23C */
    Float step_dist;            /* 0x240 */
} E102WK;                       /* 0x244 */

SA1_ASSERT_SIZE(E102_LINE, 0x20);
SA1_ASSERT_SIZE(E102WK, 0x244);
SA1_ASSERT_OFFSET(E102WK, scope_ang, 0x8C);
SA1_ASSERT_OFFSET(E102WK, body_y, 0x90);

extern E102WK *B_8C7A2EBC;          /* E-102 work of the E-102 being drawn (set by E102Main) */
extern NJS_OBJECT D_8C427688;       /* E-102 model nodes (1ST_READ data) */
extern NJS_OBJECT D_8C42A3E8;

void func_8C070EDA(taskwk *twp, motionwk *mwp, playerwk *pwp);  /* braking acceleration (code_8C070EDA.c) */

void func_8C0D2156(taskwk *twp, motionwk *mwp, playerwk *pwp);

void func_8C0D2080(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    func_8C06E904(twp, mwp, pwp);
}

/*
 * NON_MATCHING: 2 of 101 instructions differ. Only the delay slot of the
 * "if (!ret)" branch: the original keeps the store of mag before a plain BF
 * at 8C0D20B6 (BF, empty slot), SHC moves it into a BF/S slot. Register use
 * (FR3 for the 0.0f compare, FR1 for the 1.5f product), frame and stack
 * homes are identical. The original gives mwp a stack home
 * (MOV.L R5,@(8,R15), never read back); the dead block at the start of the
 * !ret block reproduces it and also the FR allocation of that block (the
 * same block at the end of the function gives FR1/FR0 instead, 7 diffs).
 * Tried without effect on the slot: comma/assignment forms of the multiply
 * inside the condition, if/else, goto, switch, do/while(0), while/break,
 * labels, ?: forms, inline helpers (#pragma inline), volatile mag, a temp
 * for run_accel * 0.5f, literal forms, declaration orders/types, dead
 * blocks of other contents and positions (~600 compiled variants).
 * Constants used in two dead blocks get hoisted into the slot (MOV #0,Rn /
 * FLDI0), which suggests the original slot held an instruction that was
 * removed after delay-slot filling. C complete and faithful.
 * The SHC build keeps this body for the literal-pool layout; the splicer
 * keeps the original asm because of this marker:
 *   ASM_STUB(void func_8C0D2086(taskwk *twp, motionwk *mwp, playerwk *pwp))
 */
void func_8C0D2086(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    Float mag;
    Sint32 ret;
    Float f;

    func_8C0D2156(twp, mwp, pwp);
    ret = GetAnalog(twp, NULL, &mag);
    mag *= pwp->p.run_accel * 0.5f;
    if (!ret) {
        if (0) {
            mwp->rad = mag;
        }
        f = pwp->p.slow_down;
        if (pwp->spd.x < 0.0f) {
            f = -(f * 1.5f);
        }
        mag += f;
    }
    pwp->spd.x += mag;
    if (pwp->nocontimer) {
        pwp->spd.x -= pwp->p.slow_down;
    } else {
        pwp->spd.x = (pwp->spd.x < pwp->p.rush_speed) ? pwp->spd.x : pwp->p.rush_speed;
    }
    pwp->spd.x = (pwp->spd.x > -pwp->p.rush_speed) ? pwp->spd.x : -pwp->p.rush_speed;
    pwp->spd.y = -3.0f;
    pwp->spd.z = 0.0f;
    pwp->acc.x = 0.0f;
    pwp->acc.y = 0.0f;
    pwp->acc.z = 0.0f;
}

void func_8C0D2150(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    func_8C070EDA(twp, mwp, pwp);
}

void func_8C0D2156(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    /* dead code: keeps SHC from inlining this thunk (matching notes) */
    if (0) {
        return;
    }
    if (0) {
        return;
    }
    func_8C071700(twp, mwp, pwp);
}

/* motion callback of the E-102 body (see the matching notes above) */
void func_8C0D215C(NJS_OBJECT *obj)
{
    if (obj == &D_8C427688) {
        B_8C7A2EBC->scope_ang ? njRotateY(NULL, B_8C7A2EBC->scope_ang) : (void)0;
        return;
    }
    if (obj == &D_8C42A3E8) {
        njTranslate(NULL, 0.0f, B_8C7A2EBC->body_y * 2.0f, 0.0f);
    }
}

void func_8C0D2210(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_POINT3 v;

    v.x = 0.0f;
    v.y = pwp->p.height;
    v.z = 0.0f;
    PConvertVector_P2G(twp, &v);
    njSubVector(&twp->pos, &v);
    if (pwp->spd.x != 0.0f && mwp->rad == 0.0f) {
        if (pwp->mj.nframe == 15.0f || pwp->mj.nframe >= 31.0f) {
            twp->mode = 47;
            pwp->spd.x = 0.0f;
        }
    } else if (pwp->spd.x > 0.0f) {
        if (pwp->mj.nframe >= 0.0f && pwp->mj.nframe < 15.0f) {
            pwp->mj.nframe += 1.0f;
            if (pwp->mj.nframe == 15.0f && !GetAnalog(twp, NULL, NULL)) {
                twp->mode = 47;
                pwp->spd.x = 0.0f;
            }
        } else {
            pwp->mj.nframe += 1.0f;
            if (pwp->mj.nframe >= 32.0f) {
                pwp->mj.nframe = 0.0f;
            }
            if (pwp->mj.nframe == 31.0f && !GetAnalog(twp, NULL, NULL)) {
                twp->mode = 47;
                pwp->spd.x = 0.0f;
            }
        }
    } else if (pwp->spd.x < 0.0f) {
        if (pwp->mj.nframe > 15.0f && pwp->mj.nframe <= 31.0f) {
            pwp->mj.nframe -= 1.0f;
            if (pwp->mj.nframe == 15.0f && !GetAnalog(twp, NULL, NULL)) {
                twp->mode = 47;
                pwp->spd.x = 0.0f;
            }
        } else {
            pwp->mj.nframe -= 1.0f;
            if (pwp->mj.nframe < 0.0f) {
                pwp->mj.nframe = 31.0f;
            }
            if (pwp->mj.nframe == 31.0f && !GetAnalog(twp, NULL, NULL)) {
                twp->mode = 47;
                pwp->spd.x = 0.0f;
            }
        }
    } else {
        pwp->spd.x = 0.0f;
    }
}