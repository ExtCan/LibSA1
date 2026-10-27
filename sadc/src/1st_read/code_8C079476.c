#include "sa1/sa1.h"

/*
 * Player motion (1ST_READ 8C079476-8C079C20): PSetMotion, the per-frame
 * motion state machine of mtnjvwk (switch on mtnmode), called by every
 * character main.
 *
 *  mtnmode  0  first call: allocate the link action actwkptr (NJS_ACTION +
 *              2-frame NJS_MOTION + 111 mdata entries), then as 1
 *           1  start reqaction: if the link speed (PL_ACTION.frame) is not 1
 *              and both actions use the same model, build a link motion
 *              (func_8C079C20) and blend from frame 0, then as 2
 *           2  linking: advance by PL_ACTION.frame; at 1.0 switch to the
 *              action's own mtnmode (func_8C07A49C, PInitMotionFrame)
 *           3  loop, speed racio            9  loop, racio * *spdp
 *          10  loop, racio * *workp        12  loop, frame set by the caller
 *           4  once, then PL_ACTION.next   11  once (speed *workp), then next
 *           5  once, hold the last frame (flag |= 1)
 *           6  backwards once, then next    7  backwards once, hold frame 0
 *           8  once, then link into nextaction
 *     13, 14, other: free the link action.
 *  A new reqaction restarts at mode 1 unless the mode is 0, 2 or 14.
 *
 * The two helpers after PSetMotion have no symbol (only reached by BRA tail
 * calls), so the tools count them as part of PSetMotion; names are ours:
 *  PNextMotion       8C079B56  go to PL_ACTION.next of an action; falls
 *                              through (tail call) into PInitMotionFrame
 *  PInitMotionFrame  8C079B92  start frame for the new mtnmode
 *
 * Modes 9 and 10 compile to FMAC: SA1_FMAC_ACC (SA1_FMAC(a, b, nframe)
 * evaluates the operands in a different order and changes the SH-4 code).
 *
 * NON_MATCHING: the SH-4 code is identical to the original (all 858
 * instructions of 8C079476-8C079BF8, the pool constants and the 15-entry
 * jump table), but tools/shcdiff.py cannot verify it: it cannot evaluate the
 * ".DATA.W Lcase-Lbase" jump-table entries behind the MOVA, and it ends
 * PSetMotion at the first static function of the compiled output while the
 * original range runs on through 8C079BF8. The constants in the pool at
 * 8C0794C0 belong to func_8C0792DC, so the original translation unit also
 * held func_8C0792DC.
 */

#ifdef NON_MATCHING

/* mdata entry used only for the size of the link motion's mdata array
 * (CAlloc(111, 0x18) on the DC; the Ninja NJS_MDATA3 layout, native on PC) */
typedef struct {
    void *p[3];
    Uint32 nb[3];
} MTN_MDATA3;

SA1_ASSERT_SIZE(MTN_MDATA3, 0x18);

static void PNextMotion(mtnjvwk *mjp, Uint16 action);
static void PInitMotionFrame(mtnjvwk *mjp);

void PSetMotion(mtnjvwk *mjp)
{
    NJS_ACTION *actp;
    Uint16 reqaction;

    reqaction = mjp->reqaction;
    mjp->lastaction = mjp->action;
    mjp->flag = 0;
    if (mjp->action != mjp->reqaction && mjp->mtnmode != 2 && mjp->mtnmode != 0
        && mjp->mtnmode != 14) {
        mjp->mtnmode = 1;
        mjp->acttimer = 0;
    } else {
        mjp->acttimer++;
    }
    switch (mjp->mtnmode) {
    case 0:
        mjp->nframe = 0.0f;
        mjp->action = reqaction;
        mjp->mtnmode = 1;
        mjp->nframe = mjp->start_frame = 0.0f;
        mjp->actwkptr = (NJS_ACTION *)MAlloc(sizeof(NJS_ACTION));
        mjp->actwkptr->object = mjp->plactptr[reqaction].actptr->object;
        mjp->actwkptr->motion = (NJS_MOTION *)MAlloc(sizeof(NJS_MOTION));
        mjp->actwkptr->motion->nbFrame = 2;
        mjp->actwkptr->motion->type = 3;
        mjp->actwkptr->motion->inp_fn = 2;
        mjp->actwkptr->motion->mdata = CAlloc(111, sizeof(MTN_MDATA3));
        break;
    case 1:
        if (mjp->plactptr[reqaction].frame != 1.0f
            && mjp->plactptr[reqaction].actptr->object
               == mjp->plactptr[mjp->action].actptr->object) {
            func_8C079C20(mjp);
            mjp->nframe = 0.0f;
        } else {
            mjp->nframe = 1.0f;
        }
        mjp->action = reqaction;
        mjp->mtnmode = 2;
        mjp->nextaction = mjp->plactptr[reqaction].next;
        /* fall through */
    case 2:
        mjp->nframe += mjp->plactptr[reqaction].frame;
        if (mjp->nframe >= 1.0f) {
            mjp->mtnmode = mjp->plactptr[reqaction].mtnmode;
            func_8C07A49C(mjp);
            PInitMotionFrame(mjp);
        }
        break;
    case 3:
        actp = mjp->plactptr[reqaction].actptr;
        mjp->nframe += mjp->plactptr[reqaction].racio;
        while (mjp->nframe >= (Float)actp->motion->nbFrame) {
            mjp->nframe -= (Float)actp->motion->nbFrame;
        }
        while (0.0f > mjp->nframe) {
            mjp->nframe += (Float)actp->motion->nbFrame;
        }
        break;
    case 9:
        actp = mjp->plactptr[reqaction].actptr;
        SA1_FMAC_ACC(mjp->nframe, *mjp->spdp, mjp->plactptr[reqaction].racio);
        while (mjp->nframe >= (Float)actp->motion->nbFrame) {
            mjp->nframe -= (Float)actp->motion->nbFrame;
        }
        while (0.0f > mjp->nframe) {
            mjp->nframe += (Float)actp->motion->nbFrame;
        }
        break;
    case 10:
        actp = mjp->plactptr[reqaction].actptr;
        SA1_FMAC_ACC(mjp->nframe, *mjp->workp, mjp->plactptr[reqaction].racio);
        while (mjp->nframe >= (Float)actp->motion->nbFrame) {
            mjp->nframe -= (Float)actp->motion->nbFrame;
        }
        while (0.0f > mjp->nframe) {
            mjp->nframe += (Float)actp->motion->nbFrame;
        }
        break;
    case 11:
        actp = mjp->plactptr[reqaction].actptr;
        mjp->nframe += *mjp->workp;
        if (mjp->nframe >= (Float)actp->motion->nbFrame - 1.0f) {
            PNextMotion(mjp, reqaction);
        }
        break;
    case 4:
        actp = mjp->plactptr[reqaction].actptr;
        mjp->nframe += mjp->plactptr[reqaction].racio;
        if (mjp->nframe >= (Float)actp->motion->nbFrame - 1.0f) {
            PNextMotion(mjp, reqaction);
        }
        break;
    case 6:
        actp = mjp->plactptr[reqaction].actptr;   /* unused (dead load in the original) */
        mjp->nframe -= mjp->plactptr[reqaction].racio;
        if (0.0f > mjp->nframe) {
            PNextMotion(mjp, reqaction);
        }
        break;
    case 8:
        actp = mjp->plactptr[reqaction].actptr;
        mjp->nframe += mjp->plactptr[reqaction].racio;
        if (mjp->nframe >= (Float)actp->motion->nbFrame - 1.0f) {
            mjp->acttimer = 0;
            if (mjp->plactptr[reqaction].frame != 1.0f
                && mjp->plactptr[reqaction].actptr->object
                   == mjp->plactptr[mjp->nextaction].actptr->object) {
                reqaction = mjp->nextaction;
                mjp->reqaction = (Uint8)reqaction;
                func_8C079C20(mjp);
                mjp->nframe = 0.0f;
            } else {
                mjp->nframe = 1.0f;
            }
            mjp->action = reqaction;
            mjp->nextaction = mjp->plactptr[reqaction].next;
            mjp->mtnmode = 2;
            mjp->nframe += mjp->plactptr[reqaction].frame;
            if (mjp->nframe >= 1.0f) {
                mjp->mtnmode = mjp->plactptr[reqaction].mtnmode;
                func_8C07A49C(mjp);
                PInitMotionFrame(mjp);
            }
        }
        break;
    case 5:
        actp = mjp->plactptr[reqaction].actptr;
        mjp->nframe += mjp->plactptr[reqaction].racio;
        if (mjp->nframe >= (Float)actp->motion->nbFrame - 1.0f) {
            mjp->nframe = (Float)actp->motion->nbFrame - 1.0f;
            mjp->flag |= 1;
        }
        break;
    case 7:
        actp = mjp->plactptr[reqaction].actptr;   /* unused (dead load in the original) */
        mjp->nframe -= mjp->plactptr[reqaction].racio;
        if (0.0f > mjp->nframe) {
            mjp->nframe = 0.0f;
            mjp->flag |= 1;
        }
        break;
    case 12:
        actp = mjp->plactptr[reqaction].actptr;
        while (mjp->nframe >= (Float)actp->motion->nbFrame) {
            mjp->nframe -= (Float)actp->motion->nbFrame;
        }
        while (0.0f > mjp->nframe) {
            mjp->nframe += (Float)actp->motion->nbFrame;
        }
        break;
    case 13:
    case 14:
    default:
        func_8C07A49C(mjp);
        if (mjp->actwkptr) {
            if (mjp->actwkptr->motion->mdata) {
                MFree(mjp->actwkptr->motion->mdata);
                mjp->actwkptr->motion->mdata = NULL;
            }
            if (mjp->actwkptr->motion) {
                MFree(mjp->actwkptr->motion);
                mjp->actwkptr->motion = NULL;
            }
            MFree(mjp->actwkptr);
            mjp->actwkptr = NULL;
        }
        break;
    }
}

/* 8C079B56 (no symbol): continue with PL_ACTION.next of `action` */
static void PNextMotion(mtnjvwk *mjp, Uint16 action)
{
    mjp->reqaction = mjp->plactptr[action].next;
    mjp->action = mjp->reqaction;
    mjp->mtnmode = mjp->plactptr[mjp->action].mtnmode;
    mjp->nextaction = mjp->plactptr[mjp->reqaction].next;
    mjp->flag |= 1;
    PInitMotionFrame(mjp);
}

/* 8C079B92 (no symbol): first frame for the new mtnmode (last frame for
 * the backwards modes, start_frame for the loops, else 0) */
static void PInitMotionFrame(mtnjvwk *mjp)
{
    Sint16 mode;

    if ((mode = mjp->mtnmode) == 6 || mode == 7) {
        mjp->nframe = (Float)mjp->plactptr[mjp->action].actptr->motion->nbFrame - 1.0f;
    } else if ((mode = mjp->mtnmode) == 3 || mode == 10 || mode == 9 || mode == 12) {
        mjp->nframe = mjp->start_frame;
        mjp->start_frame = 0.0f;
    } else {
        mjp->nframe = 0.0f;
    }
}

ASM_STUB(void func_8C079C20(mtnjvwk *mjp))
ASM_STUB(void func_8C07A49C(mtnjvwk *mjp))

#endif /* NON_MATCHING */
