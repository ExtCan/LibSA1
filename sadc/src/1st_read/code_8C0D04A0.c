#include "sa1/sa1.h"

/*
 * E-102 Gamma (1ST_READ chunk 8C0CBE00-8C0D2080), mode-machine checks
 * called by E102ModeMachine with (twp, mwp, pwp) (E-102 counterparts of
 * Sonic's func_8C0AFB10 / func_8C0AFC98 / func_8C0AFE20 / func_8C0AFE72):
 *
 *  func_8C0D04A0  damage check: twp->flag & 4 (hurt) or a damaging surface
 *                 (pwp->attr & 0x10000), not in mode 53: knock-back
 *                 (mode 10, action 13, spd = (-run_speed, jmp_y_spd)),
 *                 invulnerability timer wtimer = 120, SE 0x529, pad
 *                 vibration, clears the E-102 work timers at +0x0A/+0x0C.
 *  func_8C0D0528  jump check (8C06CEBE): holding an object of class 0x200
 *                 -> mode 36 (from 34) or mode 23 / action 41; otherwise
 *                 drops the held object (8C0D15C0) and jumps: mode 35
 *                 (from 33) or mode 4 / action 6, spd.y = jmp_y_spd,
 *                 inheriting the force-work speed of the moving land it
 *                 stood on; positional SE 0x30F when B_8C7A2EA8 is set.
 *  func_8C0D06C6  button D_8C19E390 pressed: clears pwp->spd, returns 1.
 *  func_8C0D06F4  rotation helper: event lock 1..4 sets twp->flag 0x4000;
 *                 lock 6 / 5 rotate the player towards the event angle
 *                 (absolute / relative to ang.y) via 8C07C298.
 */

/*
 * E-102 work (pwp->free.ptr[0], allocated by E102Main 8C0CBE00). Head only
 * (the full 0x244-byte layout is in code_8C0CBE00.c; field names follow it);
 * the work is only reached through a pointer here and the head holds no
 * pointers, so the head is valid on 64-bit hosts too.
 */
typedef struct {
    Sint8 mode;                 /* 0x00 laser/targeting state (E102Main: MOV.B, == 3) */
    Sint8 smode;                /* 0x01 */
    Sint8 nb_target;            /* 0x02 */
    Sint8 unk03;                /* 0x03 */
    Sint8 nb_lock;              /* 0x04 */
    Sint8 nb_shot;              /* 0x05 */
    Sint8 arm_idx;              /* 0x06 */
    Sint8 unk07;                /* 0x07 */
    Sint16 timer08;             /* 0x08 */
    Sint16 timer0A;             /* 0x0A MOV.W, cleared by func_8C0D04A0 */
    Sint16 timer0C;             /* 0x0C MOV.W, cleared by func_8C0D04A0 */
    Sint16 unk0E;               /* 0x0E */
    Sint16 laser_timer;         /* 0x10 */
    Sint16 lockon_timer;        /* 0x12 */
    Sint16 unk14;               /* 0x14 */
    Sint16 flag;                /* 0x16 bit 0: hover SE playing (8C0D257E) */
} E102WK;                       /* head only; the real work is 0x244 bytes */

SA1_ASSERT_OFFSET(E102WK, timer0A, 0x0A);
SA1_ASSERT_OFFSET(E102WK, timer0C, 0x0C);
SA1_ASSERT_OFFSET(E102WK, flag, 0x16);

/* positional-SE enable flag (MOV.B + TST; also read by 8C0D257E) */
extern Sint8 B_8C7A2EA8;

/* jump button test (code_8C06CCA0.c reads only twp; the character code passes pwp too) */
Sint32 func_8C06CEBE(taskwk *twp, playerwk *pwp);
/* E-102: clears E102WK.flag bit 0 with SE 0x312/0x310 (8C0D24E0 chunk) */
void func_8C0D257E(taskwk *twp, E102WK *ewk);
/* E-102: release the held object (same chunk, reached through JSR) */
void func_8C0D15C0(taskwk *twp, motionwk *mwp, playerwk *pwp);

Sint32 func_8C0D04A0(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    E102WK *ewk;

    if (pwp != NULL && ((twp->flag & 4) || (pwp->attr & 0x10000)) && twp->mode != 53) {
        ewk = (E102WK *)pwp->free.ptr[0];
        twp->mode = 10;
        pwp->mj.reqaction = 13;
        twp->flag &= ~4;
        pwp->spd.y = pwp->p.jmp_y_spd;
        pwp->spd.x = -pwp->p.run_speed;
        twp->wtimer = 120;
        func_8C0940DA(0x529, 0, 0, 0);
        func_8C109FDE((Uint8)twp->counter.b[0], 7, 59, 6);
        ewk->timer0A = 0;
        ewk->timer0C = 0;
        return 1;
    }
    return 0;
}

Sint32 func_8C0D0528(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    E102WK *ewk;
    sa1_forcewk *fwp;
    Sint32 obj;

    ewk = (E102WK *)pwp->free.ptr[0];
    if (func_8C06CEBE(twp, pwp)) {
        if (twp->flag & 0x800) {
            obj = func_8C06C880(twp);
            if (obj & 0x200) {
                if (twp->mode == 34) {
                    twp->mode = 36;
                } else {
                    twp->mode = 23;
                    pwp->mj.reqaction = 41;
                    func_8C0D257E(twp, ewk);
                }
                pwp->spd.y = pwp->p.jmp_y_spd;
                twp->flag &= ~2;
                pwp->jumptimer = 0;
                if (B_8C7A2EA8) {
                    func_8C09413A(0x30F, 0, 0, 0, twp->pos.x, twp->pos.y, twp->pos.z);
                }
                return 1;
            } else {
                func_8C0D15C0(twp, mwp, pwp);
            }
        }
        if (twp->mode == 33) {
            twp->mode = 35;
        } else {
            twp->mode = 4;
            pwp->mj.reqaction = 6;
        }
        pwp->spd.y = pwp->p.jmp_y_spd;
        if (pwp->ttp != NULL) {
            if ((fwp = (sa1_forcewk *)pwp->ttp->fwp) != NULL) {
                func_8C07CB54(twp->counter.b[0], fwp->pos_spd.x, fwp->pos_spd.y, fwp->pos_spd.z);
            }
        }
        twp->flag &= ~2;
        pwp->jumptimer = 0;
        func_8C0D257E(twp, ewk);
        if (B_8C7A2EA8) {
            func_8C09413A(0x30F, 0, 0, 0, twp->pos.x, twp->pos.y, twp->pos.z);
        }
        return 1;
    }
    return 0;
}

Sint32 func_8C0D06C6(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    if (B_8C754C08[(Uint8)twp->counter.b[0]].press & D_8C19E390) {
        pwp->spd.x = pwp->spd.y = pwp->spd.z = 0.0f;
        return 1;
    }
    return 0;
}

/* reads only twp; mwp and pwp are passed by both callers (8C0D07C0, 8C0D0B3A) */
Sint32 func_8C0D06F4(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    eventwk *ewp;

    ewp = EWP(twp);
    switch ((Uint8)ewp->lock) {
    case 2:
    case 3:
    case 4:
    case 1:
        twp->flag |= 0x4000;
        break;
    case 6:
        func_8C07C298(twp->counter.b[0], ewp->ang[1]);
        break;
    case 5:
        func_8C07C298(twp->counter.b[0], ewp->ang[1] + twp->ang.y);
        break;
    default:
        twp->flag &= ~0x4000;
        return 0;
    }
    return 1;
}
