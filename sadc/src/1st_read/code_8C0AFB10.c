#include "common.h"
#include "ninja.h"
#include "sa1/sa1.h"

/*
 * 1ST_READ 8C0AFB10-8C0AFF1A: Sonic mode-machine checks (called by
 * SonicModeMachine 8C0A966C with (twp, mwp, pwp)) and Sonic's rotation
 * helper (called by the forced-action handlers 8C0AFF1A / 8C0B07E4).
 *
 *  func_8C0AFB10  damage check: twp->flag & 4 (hurt) or a damaging surface
 *                 (pwp->attr & 0x10000). Sets the invulnerability timer
 *                 (wtimer = 120), vibrates the pad, then by mode: modes
 *                 46..50 -> mode 50 / action 46 (crash effect), modes
 *                 62..68 -> mode 68 / action 110, Super Sonic
 *                 (equipment & 0x8000) only halves spd.x, otherwise the
 *                 knock-back (mode 16, action 23, spd = (-run_speed,
 *                 jmp_y_spd)).
 *  func_8C0AFC98  jump check (8C06CEBE: 2 = jump on an object, 1 = jump);
 *                 drops a held object (8C0B1D60) and starts the jump
 *                 (mode 8, action 14, spd.y = jmp_y_spd, inheriting the
 *                 force-work speed of the moving land it stood on) or the
 *                 special jumps (mode 40 / action 73, mode 29 / action 55).
 *  func_8C0AFE20  action button D_8C19E38C pressed: mode 12, action 18,
 *                 speed cleared (spin dash charge).
 *  func_8C0AFE72  rotation helper: event lock 1..4 sets twp->flag 0x4000;
 *                 lock 6 / 5 rotate the player towards the event angle
 *                 (absolute / relative + 0x4000) via 8C07C298.
 */

/*
 * Translation unit: the original object holds the whole chunk
 * 8C0AEFA0-8C0B11C0 (no pool gap before 8C0AFB10). The literal pool SHC
 * dumps at 8C0AFB8A, inside func_8C0AFB10, still carries 12 entries of
 * func_8C0AF90A (func_8C078EB0 .. func_8C078E40); its position decides the
 * long-branch forms (BF/BRA) of func_8C0AFB10 and the position of the next
 * pool (8C0AFD4A, inside func_8C0AFC98). Compiled alone, this file dumps
 * both pools elsewhere, so func_8C0AFB10 and func_8C0AFC98 match only when
 * compiled after code_8C0AF5A0.c, as in the original object:
 *   py -3.12 tools/shcdiff.py <func> src/1st_read/code_8C0AFB10.c
 *       --flags=-define=TU_EXPERIMENT,NON_MATCHING
 * (both MATCH there; func_8C0AFE20 and func_8C0AFE72 match either way).
 */
#ifdef TU_EXPERIMENT
#include "1st_read/code_8C0AF5A0.c"
#endif

/*
 * Force work (task.fwp): CreateElementalTask allocates CAlloc(2, 0x1C) for
 * im & 4. Layout from the SADX analogy (forcewk); not in the shared headers
 * (same local definition as code_8C0988E4.c and code_8C056A04.c).
 */
typedef struct forcewk {
    void (*call_back)(task *tp, taskwk *twp, struct forcewk *fwp);   /* 0x00 */
    Angle3 ang_spd;             /* 0x04 */
    NJS_POINT3 pos_spd;         /* 0x10 */
} forcewk;                      /* 0x1C */

SA1_ASSERT_SIZE(forcewk, 0x1C);
SA1_ASSERT_OFFSET(forcewk, ang_spd, 0x04);
SA1_ASSERT_OFFSET(forcewk, pos_spd, 0x10);

/* pad vibration (8C109FDE -> pdVib); the caller zero-extends the player number */
void func_8C109FDE(Sint32 pno, Sint32 a, Sint32 b, Sint32 c);
/* spawns the crash/impact effect task 8C044680 for the player (8C07B320) */
void func_8C07B320(taskwk *twp);
/* camera (8C01AE60, 8 callers from the characters) */
void func_8C01AE60(void);
/* face/expression of player pno (8C07B2B8: playerpwp[pno]->pfp->+0x0C = D_8C19C32C[n]) */
void func_8C07B2B8(Uint8 pno, Sint32 n);
/* jump button test (code_8C06CCA0.c reads only twp; the Sonic code passes pwp too) */
Sint32 func_8C06CEBE(taskwk *twp, playerwk *pwp);
/* release the held object (Sonic, 8C0B1D60) */
void func_8C0B1D60(taskwk *twp, motionwk *mwp, playerwk *pwp);
/* object query next to the jump panel code (8C05E788) */
void func_8C05E788(taskwk *twp);
/* object classification of the carried/touched object (code_8C06C880.c) */
Sint32 func_8C06C880(taskwk *twp);
/* rotation wrapper of the player API (8C07C298) */
void func_8C07C298(Uint8 pno, Angle ang);

#define EWP(twp) ((eventwk *)(twp)->ewp)

#ifdef NON_MATCHING
/*
 * Matches only in the original translation unit (TU_EXPERIMENT, see above).
 * Matching notes: the mode ranges form one else-if chain (a separate if
 * for 62..68 swaps R2/R3); equipment is tested signed (no EXTU.W).
 */
Sint32 func_8C0AFB10(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    if ((Uint8)twp->id != 2 && ((twp->flag & 4) || (pwp != NULL && (pwp->attr & 0x10000)))) {
        twp->wtimer = 120;
        func_8C109FDE((Uint8)twp->counter.b[0], 7, 59, 6);
        if (twp->mode >= 46 && twp->mode <= 50) {
            func_8C0940DA(0x458, 0, 0, 0);
            twp->mode = 50;
            pwp->mj.reqaction = 46;
            func_8C07B320(twp);
            return 1;
        } else if (twp->mode >= 62 && twp->mode <= 68) {
            func_8C0940DA(0x463, 0, 0, 0);
            twp->mode = 68;
            pwp->mj.reqaction = 110;
            twp->flag &= ~4;
        } else if ((Sint16)pwp->equipment & 0x8000) {
            pwp->spd.x *= 0.5f;
        } else if (twp->mode != 45) {
            if (twp->mode == 44) {
                func_8C01AE60();
            }
            func_8C0940DA(0x463, 0, 0, 0);
            twp->mode = 16;
            pwp->mj.reqaction = 23;
            twp->flag &= ~4;
            func_8C07B2B8(0, 8);
            pwp->spd.y = pwp->p.jmp_y_spd;
            pwp->spd.x = -pwp->p.run_speed;
            twp->flag &= ~0x500;
            return 1;
        }
    }
    return 0;
}
#else
ASM_STUB(Sint32 func_8C0AFB10(taskwk *twp, motionwk *mwp, playerwk *pwp))
#endif

#ifdef NON_MATCHING
/*
 * Matches only in the original translation unit (TU_EXPERIMENT, see above).
 * Matching notes: three separate SE calls (SHC hoists func_8C0940DA into
 * R10 and cross-jumps them into one), the 8C06C880 result in a local,
 * the explicit else, and fwp assigned in a nested if (pwp->ttp is
 * reloaded, fwp lives in R9).
 */
Sint32 func_8C0AFC98(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    forcewk *fwp;
    Sint32 obj;

    if (func_8C06CEBE(twp, pwp) == 2) {
        if (twp->flag & 0x800) {
            func_8C0B1D60(twp, mwp, pwp);
        }
        func_8C05E788(twp);
        twp->mode = 40;
        pwp->mj.reqaction = 73;
        twp->flag &= ~2;
        pwp->jumptimer = 0;
        func_8C0940DA(17, 0, 0, 0);
        return 1;
    } else if (func_8C06CEBE(twp, pwp)) {
        if (twp->flag & 0x800) {
            obj = func_8C06C880(twp);
            if (obj & 0x200) {
                twp->mode = 29;
                pwp->spd.y = pwp->p.jmp_y_spd;
                pwp->mj.reqaction = 55;
                twp->flag &= ~2;
                pwp->jumptimer = 0;
                func_8C0940DA(17, 0, 0, 0);
                return 1;
            } else {
                func_8C0B1D60(twp, mwp, pwp);
            }
        }
        twp->mode = 8;
        pwp->spd.y = pwp->p.jmp_y_spd;
        if (pwp->ttp != NULL) {
            if ((fwp = pwp->ttp->fwp) != NULL) {
                func_8C07CB54(twp->counter.b[0], fwp->pos_spd.x, fwp->pos_spd.y, fwp->pos_spd.z);
            }
        }
        pwp->mj.reqaction = 14;
        pwp->work.f = 5.0f;
        twp->flag &= ~2;
        twp->flag |= 0x500;
        pwp->jumptimer = 0;
        func_8C0940DA(17, 0, 0, 0);
        return 1;
    }
    return 0;
}
#else
ASM_STUB(Sint32 func_8C0AFC98(taskwk *twp, motionwk *mwp, playerwk *pwp))
#endif

Sint32 func_8C0AFE20(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    if (B_8C754C08[(Uint8)twp->counter.b[0]].press & D_8C19E38C) {
        twp->mode = 12;
        PClearSpeed(NULL, pwp);
        pwp->mj.reqaction = 18;
        pwp->work.f = 0.0f;
        twp->flag &= ~0x500;
        return 1;
    }
    return 0;
}

Sint32 func_8C0AFE72(taskwk *twp, motionwk *mwp, playerwk *pwp)
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
