#include "sa1/sa1.h"

/*
 * Player common (1ST_READ 8C07BE68-8C07C120): the drowning countdown task
 * (created by func_8C06C6F8 when breathtimer reaches 0x438) and the
 * "put player pno on a path" requests (twp->flag |= 0x1000, twp->smode =
 * mode, twp->value = path), used by the stage overlays.
 */

/* Path tag (SADX pathtag; full layout in code_8C08966E.c). Only passed
 * through here, so it stays incomplete (it holds pointers). */
typedef struct pathtag PATHTAG;

/* any work (task.awp) of the drowning countdown task (CAlloc(1, 0x10)) */
typedef struct {
    Uint8 pno;                  /* 0x00 player number (set by func_8C06C6F8) */
    Uint32 timer;               /* 0x04 frame counter */
} DROWNWK;

extern Sint16 D_8C19B468;

Sint32 func_8C107B26(void);
void func_8C107ADA(void);
void func_8C107C80(Sint32 no);
void func_8C04E386(Sint16 mode);
Sint32 func_8C089A40(PATHTAG *tag, Uint32 n, Float *out);
Float func_8C089AA0(PATHTAG *path, NJS_POINT3 *pos, NJS_POINT3 *outPos, Float *outDist);

/*
 * The three functions before this file's first function belong to the same
 * original translation unit (chunk 8C07BAC0, code_8C07BAC0.c). They are not
 * called from here (func_8C07BDA0 is only referenced as the display
 * function), but their pending literal-pool entries make SHC dump the pool
 * inside func_8C07BE6E after the "case 2" BRA (8C07BF0A), as in the
 * original, which turns three branches of func_8C07BE6E into the original
 * "Bcc +6; BRA far" pairs. They also put the second pool after
 * func_8C07C02E (8C07C048), as in the original.
 */
ASM_STUB(void func_8C07BD2A(Uint8 pno))
ASM_STUB(void func_8C07BD48(task *ptp, Uint8 pno))
ASM_STUB(void func_8C07BDA0(task *tp))

/* destructor of the drowning countdown task: func_8C107B26 (music state:
 * makes the saved track D_8C5FBAB0 current again, presumably undoing the
 * func_8C107C80(96) countdown music) */
void func_8C07BE68(task *tp)
{
    func_8C107B26();
}

/*
 * Exec of the drowning countdown task (awp = DROWNWK, created by
 * func_8C06C6F8 18 s after the player went under water). Counts frames:
 * once the player has air again (breathtimer < 60) the task ends; during
 * the first two frames it installs its display (func_8C07BDA0, the
 * countdown numbers) and destructor and starts the drowning music (96) for
 * player 0. At frame 0x30C the player drowns: player 0 gets mode 39 and the
 * character's drowning voice (none for Eggman/Tikal/E-102: the original
 * passes an uninitialised value), other players get twp->flag |= 4 and the
 * task ends. At frame 0x3FC func_8C04E386(2) is called (stores 2 in the
 * game state D_8C19DE24) and the task ends.
 * With D_8C19B468 == 1 the task ends and resets the breath timer.
 *
 * NON_MATCHING: one delay slot. Everything else is identical (registers,
 * branches, both literal-pool positions), but the original fills the delay
 * slot of the "flag |= 4" branch into the shared FreeTask call
 * (8C07BEC6: BRA 8C07BFA2; MOV.W R0,@(4,R3)) and SHC 5.1r03 here emits
 * MOV.W R0,@(4,R3); BRA; NOP. SHC never fills a BRA slot with the store of
 * a read-modify-write that uses the "MOV #4,R2; ADD R3,R2; MOV.W @R2,R0"
 * form (|= on playertwp[pno]->flag); a local taskwk pointer or an explicit
 * pointer fills the slot but changes the load to @(4,Rn)/@Rn. Tried: if/else
 * chain (this one), single join with 2 or 3 FreeTask calls (FreeTask is
 * then tail-called or no longer kept in R12), goto into the shared call,
 * early returns, inverted pno test, "flag = flag | 4" and temporaries,
 * Uint16/Sint16 casts, inline helper, the DROWNWK pointer in a local (swaps
 * R11/R12). Keep the structure: it gives R12 = FreeTask, R11 = pno * 4.
 */
#ifdef NON_MATCHING
void func_8C07BE6E(task *tp)
{
    Uint32 timer;
    Sint32 pno;
    Sint32 se;

    PC_ONLY(se = 0;)
    pno = ((DROWNWK *)tp->awp)->pno;
    timer = ((DROWNWK *)tp->awp)->timer++;
    if (timer < 0x30C && playerpwp[pno]->breathtimer < 60) {
        FreeTask(tp);
    } else if (timer == 0x30C) {
        if (pno != 0) {
            playertwp[pno]->flag |= 4;
            FreeTask(tp);
        } else {
            SetInputP(pno, 39);
            switch ((Uint8)playertp[pno]->twp->counter.b[1]) {
            case 0:
                se = 0x574;
                break;
            case 2:
                se = 0x54E;
                break;
            case 3:
                se = 0x53E;
                break;
            case 5:
                se = 0x507;
                break;
            case 7:
                se = 0x51B;
                break;
            }
            func_8C0940DA(se, 0, 0, 0);
            func_8C107ADA();
        }
    } else if (timer == 0x3FC) {
        func_8C04E386(2);
        FreeTask(tp);
    } else if (D_8C19B468 == 1) {
        FreeTask(tp);
        playerpwp[pno]->breathtimer = 0;
    } else {
        if (timer <= 1) {
            tp->dest = func_8C07BE68;
            tp->disp = func_8C07BDA0;
            if (pno == 0) {
                func_8C107C80(96);
            }
        }
        tp->disp(tp);
    }
}
#else
ASM_STUB(void func_8C07BE6E(task *tp))
#endif

/*
 * Path requests for player pno: twp->flag |= 0x1000 (request, see SetInputP)
 * with twp->smode = mode and the PATHTAG in twp->value (read back as a path
 * by the character mode machines; the stage overlays pass the PATHTAG of a
 * set object). 8C07BFFA (mode 1) also adds 5.0 to the forward speed.
 */
void func_8C07BFFA(Uint8 pno, PATHTAG *path)
{
    taskwk *twp;

    twp = playertwp[pno];
    twp->flag |= 0x1000;
    twp->smode = 1;
    twp->value.ptr = path;
    playerpwp[pno]->spd.x += 5.0f;
}

void func_8C07C02E(Uint8 pno, PATHTAG *path)
{
    taskwk *twp;

    twp = playertwp[pno];
    twp->flag |= 0x1000;
    twp->smode = 2;
    twp->value.ptr = path;
}

/* mode 26: also projects the player position onto the path into
 * pwp->hpos (func_8C089AA0); ignored when player pno does not exist */
void func_8C07C0A0(Uint8 pno, PATHTAG *path)
{
    taskwk *twp;

    twp = playertwp[pno];
    if (twp) {
        twp->flag |= 0x1000;
        twp->smode = 26;
        twp->value.ptr = path;
        func_8C089AA0(path, &twp->pos, NULL, &playerpwp[pno]->hpos);
    }
}

/*
 * mode 29: start at path point n. pwp->hpos = length of the path up to point
 * n (func_8C089A40). The point index is stored as a 16-bit value at
 * playerwk +0x14 (MOV.W): path_point is a Sint16 there (include/sa1/player.h
 * still declares it Sint32; on every little-endian host the cast writes the
 * same low half).
 */
void func_8C07C0E4(Uint8 pno, PATHTAG *path, Sint32 n)
{
    taskwk *twp;
    playerwk *pwp;

    twp = playertwp[pno];
    pwp = playerpwp[pno];
    twp->flag |= 0x1000;
    twp->smode = 29;
    twp->value.ptr = path;
    *(Sint16 *)&pwp->path_point = n;
    func_8C089A40(path, n, &pwp->hpos);
}

/* called with BSR by func_8C07BE6E (same translation unit) */
ASM_STUB(void SetInputP(Uint8 pno, Sint32 mode))
