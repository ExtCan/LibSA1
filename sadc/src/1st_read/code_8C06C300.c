#include "common.h"
#include "ninja.h"
/* include/sa1/player.h declares func_8C06C5CA(task *tp), but the function
 * takes the player number (it indexes the player slots with R4; the
 * destructors pass twp->counter.b[0], e.g. 8C0AE0FA MOV.B @(8,R14) + EXTU.B).
 * Until the header is fixed its prototype is renamed out of the way here. */
#define func_8C06C5CA func_8C06C5CA_header_proto
#include "sa1/sa1.h"
#undef func_8C06C5CA

/*
 * Player common, chunk 8C06C300: player slot reset, a pad button test,
 * PInitialize (playerwk allocation and setup) and the player free routine
 * called by the character destructors.
 */

extern Sint32 D_8C19E3A4;       /* button assignment mask (next to D_8C19E3A0); signed: Uint32 changes SHC's register choice */
extern Sint8 B_8C754BF8;        /* input/game flag tested as a byte */

void func_8C0B3A2E(task *tp);   /* exec of the per-player helper task created by PInitialize */
void func_8C07D67C(playerwk *pwp); /* allocate pwp->island_list (code_8C07D67C.c) */
void func_8C07D6C0(playerwk *pwp); /* free pwp->island_list (code_8C07D6C0.c) */

void func_8C06C5CA(Sint32 pno);

/* clear the 8 player slots (LoadPlayerTask 8C04F3C0) */
void func_8C06C300(void)
{
    Sint32 i;

    for (i = 0; i < 8; i++) {
        playertp[i] = NULL;
        playertwp[i] = NULL;
        playermwp[i] = NULL;
        playerpwp[i] = NULL;
    }
}

/* TRUE when the pad record n has a D_8C19E3A4 button pressed and B_8C754BF8 is set */
Sint32 func_8C06C32C(Sint32 n)
{
    if ((B_8C754C08[n].press & D_8C19E3A4) && B_8C754BF8) {
        return 1;
    }
    return 0;
}

/*
 * PInitialize: allocate and set up the playerwk of player pno (task tp),
 * fill player slot pno, read the upgrade event flags into pwp->equipment and
 * create the per-player helper task (func_8C0B3A2E, twp->scl.z = pno).
 * NON_MATCHING only because of the symbol map: the code is identical, but
 * the struct copy calls SHC's __quick_mvn, which config/symbols.1st_read.txt
 * maps to 8C10CD38 (the SDK library's block move); the game's SHC 5.1r03
 * code calls 8C10CDDC (func_8C10CDDC) for every block move.
 */
#ifdef NON_MATCHING
void PInitialize(Sint32 pno, task *tp)
{
    motionwk *mwp;
    taskwk *twp;
    playerwk *pwp;
    task *ctp;

    twp = tp->twp;
    mwp = tp->mwp;
    pwp = CAlloc(1, sizeof(playerwk));
    pwp->p = Sonic_Physics_Data[(Uint8)twp->counter.b[1]];
    mwp->work.pwp = pwp;
    func_8C04B440(twp);
    pwp->cstsp = CAlloc(1, sizeof(csts));
    playertp[pno] = tp;
    playertwp[pno] = twp;
    playermwp[pno] = mwp;
    playerpwp[pno] = pwp;
    pwp->mj.mtnmode = 0;
    pwp->mj.jvmode = 0;
    pwp->mj.spdp = &pwp->spd.x;
    pwp->mj.workp = &pwp->work.f;
    pwp->shadow.scl = pwp->p.rad * 0.19999999f;
    switch ((Uint8)twp->counter.b[1]) {
    case SA1_CHR_SONIC:
        if (func_8C09068E(0x8D)) {
            pwp->equipment |= 0x2;
        }
        if (func_8C09068E(0x8E)) {
            pwp->equipment |= 0x1;
        }
        if (func_8C09068E(0x9F)) {
            pwp->equipment |= 0x4;
        }
        /* fall through */
    case SA1_CHR_TAILS:
        if (func_8C09068E(0xCD)) {
            pwp->equipment |= 0x8;
        }
        if (func_8C09068E(0xDB)) {
            pwp->equipment |= 0x10;
        }
        break;
    case SA1_CHR_KNUCKLES:
        if (func_8C09068E(0x119)) {
            pwp->equipment |= 0x40;
        }
        if (func_8C09068E(0x11A)) {
            pwp->equipment |= 0x20;
        }
        break;
    case SA1_CHR_AMY:
        if (func_8C09068E(0x152)) {
            pwp->equipment |= 0x100;
        }
        if (func_8C09068E(0x15E)) {
            pwp->equipment |= 0x80;
        }
        break;
    case SA1_CHR_E102:
        if (func_8C09068E(0x189)) {
            pwp->equipment |= 0x400;
        }
        if (func_8C09068E(0x18A)) {
            pwp->equipment |= 0x200;
        }
        /* fall through */
    case SA1_CHR_BIG:
        if (func_8C09068E(0x1D0)) {
            pwp->equipment |= 0x1000;
        }
        if (func_8C09068E(0x1D1)) {
            pwp->equipment |= 0x800;
        }
        break;
    }
    PClearSpeed(NULL, pwp);
    mwp->weight = 1.0f;
    if (twp->id == 0) {
        twp->id = 1;
    }
    func_8C07D67C(pwp);
    twp->scl.z = 1.0f;
    twp->scl.y = 1.0f;
    twp->scl.x = 1.0f;
    ctp = CreateElementalTask(2, 2, func_8C0B3A2E);
    ctp->twp->scl.z = (Float)pno;
}
#else
ASM_STUB(void PInitialize(Sint32 pno, task *tp))
#endif

/* free a player's work (character destructors) and clear its slot */
void func_8C06C5CA(Sint32 pno)
{
    taskwk *twp;
    motionwk *mwp;
    playerwk *pwp;

    twp = playertwp[pno];
    mwp = playermwp[pno];
    pwp = playerpwp[pno];
    playertp[pno] = NULL;
    playertwp[pno] = NULL;
    playermwp[pno] = NULL;
    playerpwp[pno] = NULL;
    if (pwp->cstsp != NULL) {
        MFree(pwp->cstsp);
        pwp->cstsp = NULL;
    }
    pwp->mj.mtnmode = 14;
    if (pwp->mj.actwkptr != NULL) {
        PSetMotion(&pwp->mj);
    }
    pwp->mj.jvmode = 2;
    if (pwp->mj.pljvptr != NULL) {
        PJoinVertexes(twp, mwp, pwp);
    }
    func_8C07D6C0(pwp);
    if (mwp->work.pwp != NULL) {
        MFree(mwp->work.pwp);
        mwp->work.pwp = NULL;
    }
}
