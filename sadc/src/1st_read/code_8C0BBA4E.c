/*
 * 1ST_READ 8C0BBA4E-8C0BBDD6 (chunk 8C0BAFC0-8C0BDC80, functions 6-10 of
 * 29): Tails mode-machine helpers, called by TailsModeMachine (8C0B70F4)
 * with (twp, pwp); func_8C0BBD3C is called by 8C0BBDD6 / 8C0BC61C. Same
 * shapes as Sonic's func_8C0AEFA0 (code_8C0AEFA0.c) and func_8C0AFB10 ..
 * func_8C0AFE72 (code_8C0AFB10.c), without the motionwk argument.
 *
 *  func_8C0BBA4E  once Tails runs at least at jog speed, create the
 *                 effect/sound task 8C07BA0A: level 8 by surface attribute
 *                 (4 / 8 / other: 0xE1 / 0xE3 / 0xE2), else 0x2B2
 *  func_8C0BBAE2  damage check: twp->flag & 4 (hurt) or a damaging surface
 *                 (pwp->attr & 0x10000), not for object class 2. Sets the
 *                 invulnerability timer (wtimer = 120), vibrates the pad,
 *                 then modes 48..54 -> mode 54 / action 115; any mode but
 *                 43 -> knock-back (mode 13, action 26, face sequence 8,
 *                 spd = (-run_speed, jmp_y_spd)). SE 0x473 via 8C0BFCA2.
 *  func_8C0BBBAA  jump check (8C06CEBE: 2 = jump on an object, 1 = jump);
 *                 drops a held object (8C0BD440) and starts the jump
 *                 (mode 6, action 14, spd.y = jmp_y_spd, work.f = 2.0f,
 *                 inheriting the force-work speed of the moving land it
 *                 stood on) or the special jumps (mode 40 / action 95,
 *                 mode 29 / action 62). SE 17 via 8C0BFCA2.
 *  func_8C0BBCEA  action button D_8C19E38C pressed: mode 10, action 19,
 *                 speed cleared.
 *  func_8C0BBD3C  rotation helper (= Sonic's func_8C0AFE72): event lock
 *                 1..4 sets twp->flag 0x4000; lock 6 / 5 rotate the player
 *                 towards the event angle (absolute / relative + 0x4000).
 *
 * Translation-unit layout model (SHC only, see the end of the file): the
 * Tails SE wrapper func_8C0BFCA2 belongs to the same original TU (the Tails
 * module 8C0B6120-8C0BFE00: no Tails function ever hoists its address, e.g.
 * 7 pool loads in 8C0BBDD6) but lies far beyond BSR range. SHC calls such
 * in-TU functions with JSR through the pool and never keeps their address in
 * a callee-saved register; declared extern, func_8C0BBBAA's three SE calls
 * get the address hoisted into R10 and cross-jumped (fwp moves to R9, the
 * 0 / -3 constants swap R11/R12). tu_gap_8C0BBDD6 stands in for the code
 * between 8C0BBDD6 and func_8C0BD440 (5.7 KB).
 */
#include "common.h"
#include "ninja.h"
#include "sa1/sa1.h"
#include "sa1/pending.h"

/* create the effect/sound task 8C07B93A with (n, pno, mode) (code_8C07B500.c) */
void func_8C07BA0A(Sint32 n, Sint8 pno, Sint16 mode);
/* Tails SE: func_8C0940DA(se, 0, 0, 0) unless twp->counter.b[0] == 1 (same TU, out of BSR range) */
void func_8C0BFCA2(taskwk *twp, Sint32 se);
/* jump button test (code_8C06CCA0.c reads only twp; the character code passes pwp too) */
Sint32 func_8C06CEBE(taskwk *twp, playerwk *pwp);
/* Tails: release the held object (twp->flag & 0x800; same TU, out of BSR range) */
void func_8C0BD440(taskwk *twp, playerwk *pwp);

void func_8C0BBA4E(taskwk *twp, playerwk *pwp)
{
    Sint32 n;

    if (pwp->spd.x < pwp->p.jog_speed) {
        return;
    }
    if (((Uint16)GetStageNumber() & 0xFF00) == 0x800) {
        if (pwp->attr == 4) {
            n = 0xE1;
        } else if (pwp->attr == 8) {
            n = 0xE3;
        } else {
            n = 0xE2;
        }
    } else {
        n = 0x2B2;
    }
    func_8C07BA0A(n, twp->counter.b[0], twp->mode);
}

Sint32 func_8C0BBAE2(taskwk *twp, playerwk *pwp)
{
    if ((Uint8)twp->id != 2 && ((twp->flag & 4) || (pwp != NULL && (pwp->attr & 0x10000)))) {
        twp->wtimer = 120;
        func_8C109FDE((Uint8)twp->counter.b[0], 7, 59, 6);
        if (twp->mode >= 48 && twp->mode <= 54) {
            func_8C0BFCA2(twp, 0x473);
            twp->mode = 54;
            pwp->mj.reqaction = 115;
            twp->flag &= ~4;
        } else if (twp->mode != 43) {
            func_8C0BFCA2(twp, 0x473);
            twp->mode = 13;
            pwp->mj.reqaction = 26;
            twp->flag &= ~4;
            func_8C07B2B8(twp->counter.b[0], 8);
            pwp->spd.y = pwp->p.jmp_y_spd;
            pwp->spd.x = -pwp->p.run_speed;
            twp->flag &= ~0x500;
            return 1;
        }
    }
    return 0;
}

Sint32 func_8C0BBBAA(taskwk *twp, playerwk *pwp)
{
    forcewk *fwp;
    Sint32 obj;

    if (func_8C06CEBE(twp, pwp) == 2) {
        if (twp->flag & 0x800) {
            func_8C0BD440(twp, pwp);
        }
        func_8C05E788(twp);
        twp->mode = 40;
        pwp->mj.reqaction = 95;
        twp->flag &= ~2;
        pwp->jumptimer = 0;
        func_8C0BFCA2(twp, 17);
        return 1;
    } else if (func_8C06CEBE(twp, pwp)) {
        if (twp->flag & 0x800) {
            obj = func_8C06C880(twp);
            if (obj & 0x200) {
                twp->mode = 29;
                pwp->spd.y = pwp->p.jmp_y_spd;
                pwp->mj.reqaction = 62;
                twp->flag &= ~2;
                pwp->jumptimer = 0;
                func_8C0BFCA2(twp, 17);
                return 1;
            } else {
                func_8C0BD440(twp, pwp);
            }
        }
        twp->mode = 6;
        pwp->spd.y = pwp->p.jmp_y_spd;
        if (pwp->ttp != NULL) {
            if ((fwp = FWP(pwp->ttp)) != NULL) {
                func_8C07CB54(twp->counter.b[0], fwp->pos_spd.x, fwp->pos_spd.y, fwp->pos_spd.z);
            }
        }
        pwp->mj.reqaction = 14;
        pwp->work.f = 2.0f;
        twp->flag &= ~2;
        twp->flag |= 0x500;
        pwp->jumptimer = 0;
        func_8C0BFCA2(twp, 17);
        return 1;
    }
    return 0;
}

Sint32 func_8C0BBCEA(taskwk *twp, playerwk *pwp)
{
    if (B_8C754C08[(Uint8)twp->counter.b[0]].press & D_8C19E38C) {
        twp->mode = 10;
        PClearSpeed(NULL, pwp);
        pwp->mj.reqaction = 19;
        pwp->work.f = 0.0f;
        twp->flag &= ~0x500;
        return 1;
    }
    return 0;
}

Sint32 func_8C0BBD3C(taskwk *twp)
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
        func_8C07C298(twp->counter.b[0], ewp->ang[1] - twp->ang.y + 0x4000);
        break;
    default:
        twp->flag &= ~0x4000;
        return 0;
    }
    return 1;
}

/*
 * Translation-unit layout model (see the top comment). On SHC the gap and
 * the stubs are never spliced (ASM_STUB bodies are ignored, the gap has no
 * address in the image); on the PC only the prototypes remain.
 */
#if !defined(TARGET_PC)
#define TU_GAP4  __asm_stub_sink(); __asm_stub_sink(); __asm_stub_sink(); __asm_stub_sink();
#define TU_GAP16 TU_GAP4 TU_GAP4 TU_GAP4 TU_GAP4
#define TU_GAP64 TU_GAP16 TU_GAP16 TU_GAP16 TU_GAP16
void tu_gap_8C0BBDD6(void)
{
    TU_GAP64 TU_GAP64 TU_GAP64 TU_GAP64 TU_GAP64 TU_GAP64
    TU_GAP64 TU_GAP64 TU_GAP64 TU_GAP64 TU_GAP64 TU_GAP64
    TU_GAP64 TU_GAP64 TU_GAP64 TU_GAP64 TU_GAP64 TU_GAP64
    TU_GAP64 TU_GAP64 TU_GAP64 TU_GAP64
}
#endif

ASM_STUB(void func_8C0BD440(taskwk *twp, playerwk *pwp))
ASM_STUB(void func_8C0BFCA2(taskwk *twp, Sint32 se))
