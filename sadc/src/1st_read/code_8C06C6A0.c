#include "sa1/sa1.h"

/*
 * Player common (1ST_READ 8C06C6A0-8C06C880): the player position/angle
 * history, the per-frame player reset with the breath (underwater) timer
 * that every character main calls first, and the "action button while
 * touching an object" query.
 */

/* Player position/angle history: 256 frames x 8 players, ring index per
 * player. Written by func_8C06C6A0, read by GetPlayerPosition 8C07CE82. */
extern Uint8 B_8C78C688[8];             /* per-player ring index (wraps at 256) */
extern NJS_POINT3 B_8C78C690[256][8];   /* positions (0x60 per frame) */
extern Angle3 B_8C792690[256][8];       /* angles (B_8C78C690 + 0x6000) */

extern Sint32 D_8C1BDFE0;               /* cleared every frame by func_8C06C6F8 */

/* exec of the drowning countdown task */
void func_8C07BE6E(task *tp);

/* any work (task.awp) of the drowning countdown task 8C07BE6E */
typedef struct {
    Sint8 pno;                  /* 0x00 player number (byte read by 8C07BE6E) */
    Uint32 timer;               /* 0x04 frame counter (8C07BE6E, compared with 0x30C) */
} DROWNWK;

/*
 * Record the current position and angle of player pno in the history.
 * NON_MATCHING: same instructions, but SHC keeps idx * 0x60 in a register
 * and spills pno * 12 (the original spills idx * 0x60) and loads the
 * B_8C78C690 base later; about 30 source variants tried (pointer locals,
 * row pointers, struct rows, index types).
 */
#ifdef NON_MATCHING
void func_8C06C6A0(Uint8 pno)
{
    taskwk *twp;
    Sint32 idx;

    twp = playertwp[pno];
    idx = ++B_8C78C688[pno];
    B_8C78C690[idx][pno] = twp->pos;
    B_8C792690[idx][pno] = twp->ang;
}
#else
ASM_STUB(void func_8C06C6A0(Uint8 pno))
#endif

/*
 * Inlined into func_8C06C6F8: the original copies pwp into R4 before the
 * store (MOV R14,R4; MOV.W R0,@(18,R4)), the trace of an inline expansion
 * of a helper taking the playerwk pointer. Name is ours.
 */
#ifndef TARGET_PC
#pragma inline(PClearIsland)
#endif
static void PClearIsland(playerwk *pwp)
{
    pwp->island_num = 0;
}

/*
 * Per-frame player reset (first call of every character main): clears
 * twp->flag bits 0x1038, the accelerations and mwp->height, counts the
 * no-control timer down, records the position history and runs the breath
 * timer. While the water attribute (0x400002) is set below the player and
 * the eyes (p.eyes_height above the position, in global space) are not
 * above the water surface (shadow.y_top), breathtimer counts up; at 0x438
 * frames (18 s) the drowning countdown task is created.
 */
void func_8C06C6F8(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_POINT3 v;
    task *tp;

    twp->flag &= ~0x1038;
    mwp->acc.x = mwp->acc.y = mwp->acc.z = 0.0f;
    pwp->eff.x = pwp->eff.y = pwp->eff.z = pwp->acc.x = pwp->acc.y = pwp->acc.z = 0.0f;
    mwp->height = 0.0f;
    if (pwp->nocontimer > 0) {
        pwp->nocontimer = pwp->nocontimer - 1;
    } else {
        pwp->nocontimer = 0;
    }
    func_8C06C6A0(twp->counter.b[0]);
    PClearIsland(pwp);
    if (pwp->shadow._Attr_top & 0x400002) {
        v.x = v.z = 0.0f;
        v.y = pwp->p.eyes_height;
        PConvertVector_P2G(twp, &v);
        njAddVector(&v, &twp->pos);
        if (pwp->shadow.y_top >= v.y) {
            pwp->breathtimer++;
            if (pwp->breathtimer == 0x438) {
                tp = CreateElementalTask(8, 0, func_8C07BE6E);
                if (tp) {
                    ((DROWNWK *)tp->awp)->pno = twp->counter.b[0];
                }
            }
        } else {
            pwp->breathtimer = 0;
        }
    } else {
        pwp->breathtimer = 0;
    }
    D_8C1BDFE0 = 0;
}

/*
 * If the action button (D_8C19E38C) of player twp is pressed this frame and
 * its collision is touching another object (cwp->flag bits 2 and 8,
 * twp->flag bit 4 clear), return the touched object's taskwk.id (as an
 * unsigned byte), else 0.
 */
Sint32 func_8C06C7DE(taskwk *twp)
{
    colliwk *hit;
    Sint32 flag;

    if (B_8C754C08[(Uint8)twp->counter.b[0]].press & D_8C19E38C) {
        flag = CWP(twp)->flag;
        if ((flag & 2) && !(twp->flag & 4) && (flag & 8)) {
            hit = CWP(twp)->hit_cwp;
            if (hit) {
                return (Uint8)hit->mytask->twp->id;
            }
        }
    }
    return 0;
}
