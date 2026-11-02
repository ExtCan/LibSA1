/*
 * 1ST_READ 8C019F4A (chunk 8C019400): the camera core of the game's camera
 * task (round 0.9.8, fish098.md 5; the exec 8C0197BA around it stays assembly).
 * It runs the current camera mode, the mode's conversion kind and the current
 * adjust on the camera taskwk.
 *
 *  level 5 (debug):     8C01DC36 fills the parameters.
 *  flags bit 1:         a mode set by code: the parameters B_8C6B7010, +0x20 =
 *                       the steps since the change.
 *  else, a CAM entry:   8C01B308 fills them from that entry.
 *  adjust:              only after the camera has run 60 steps (B_8C6B7078),
 *                       and not while the adjust's flag holds at timer 0.
 *
 * NON_MATCHING: complete and faithful; register allocation only. In the TU of
 * the conversion kinds (the original reaches them by BSR) 37 of 172 words differ:
 * the original holds &D_8C11204C in R14 and twp in R13 (here swapped) and the
 * control work of the level / flags / entry tests in R4 (here R12).
 *
 * Not native in the harness's plain --pc build: a native indirect call of a
 * guest mode goes through its thunk, and the modes' thunks say void (void) (the
 * canonical prototype), so R4 would not reach the assembly modes that read it
 * (8C01FB98: MOV.L R4,@R15). There the original runs; shcdiff (with the words
 * above) and libsa1's strict replays are this C's checks. A --ga harness has the
 * lowered C and the same problem: regress.py --route leaves this function to the
 * original there (route_args).
 */
#include "sa1/sa1.h"

#if defined(NON_MATCHING) && (!defined(TARGET_PC) || defined(SA1_GA))
/* the camera parameters the core hands the mode */
typedef struct { Uint32 w[9]; } CAMPARAM_CC;

/* A mode function is stored by the canonical prototype of func_8C01AD2C's
 * argument, void (void). The core calls it with the parameters in R4; the
 * modes libsa1 runs (8C0239C2, 8C0EAF28, 8C0EAA80) take none, and libsa1's code
 * map checks the signature of an indirect call, so the lowered call has no
 * argument. */
typedef void (*CAMMODEFUNC_CC)(void);
#if defined(SA1_GA)
#define CAM_CALL_MODE(f) ((f)())
#else
#define CAM_CALL_MODE(f) (((void (*)(CAMPARAM_CC *))(f))(&B_8C6B6FEC))
#endif

struct CAMADJUSTWK_CC;
typedef void (*CAMADJUSTFUNC_CC)(taskwk *twp, taskwk *otwp, struct CAMADJUSTWK_CC *awp);

/* camera control work *D_8C11204C (code_8C01ABDC.c has the whole record) */
typedef struct {
    Sint32 dirty;               /* 0x00 */
    Sint16 entry;               /* 0x04 current CAM entry, -1 = none */
    Sint8 mode;                 /* 0x06 */
    Sint8 kind;                 /* 0x07 conversion kind 1..3 */
    Sint8 level;                /* 0x08 5 = DEBUG */
    Sint8 adjust;               /* 0x09 */
    Sint8 pad0A;                /* 0x0A */
    Sint8 flags;                /* 0x0B bit 1 = set by code, bit 0 = the adjust's flag */
    CAMMODEFUNC_CC modefunc;    /* 0x0C */
    CAMADJUSTFUNC_CC adjfunc;   /* 0x10 */
    Sint32 timer;               /* 0x14 steps since the last change */
} CAMCONTROLWK_CC;

/* adjust work *D_8C112050 */
typedef struct CAMADJUSTWK_CC {
    Sint16 flag;
    Sint16 pad02;
    Sint32 angspdx;
    Sint32 angspdy;
    Sint32 angspdz;
    Float speed;
    Sint32 counter;             /* 0x14 */
} CAMADJUSTWK_CC;

/* camera work *D_8C112048 (code_8C01A60C.c) */
typedef struct {
    Sint32 w00;
    Sint32 w04;
    Float dist;
    NJS_POINT3 pos;             /* 0x0C */
} CAMWK_CC;

/* one CAM file entry (code_8C01ABDC.c) */
typedef struct { Uint8 b[0x40]; } CAMENTRY_CC;

extern CAMCONTROLWK_CC *D_8C11204C;
extern CAMADJUSTWK_CC *D_8C112050;
extern CAMWK_CC *D_8C112048;
extern taskwk *D_8C112044;
extern taskwk B_8C6B6EAC;       /* the previous camera taskwk: *D_8C112044 */
extern CAMPARAM_CC B_8C6B6FEC;
extern CAMPARAM_CC B_8C6B7010;
extern CAMENTRY_CC *B_8C6B7064;
extern Uint32 B_8C6B7078;

void func_8C01A60C(taskwk *twp);
void func_8C01A680(taskwk *twp);
void func_8C01A718(taskwk *twp);
void func_8C01B308(CAMENTRY_CC *entry);
void func_8C01DC36(CAMPARAM_CC *param);

void func_8C019F4A(taskwk *twp)
{
    if (D_8C11204C->modefunc == NULL) {
        return;
    }
    B_8C6B6EAC = *twp;
    D_8C112048->pos.x = twp->pos.x;
    D_8C112048->pos.y = twp->pos.y;
    D_8C112048->pos.z = twp->pos.z;
    if (D_8C11204C->level == 5) {
        func_8C01DC36(&B_8C6B6FEC);
        CAM_CALL_MODE(D_8C11204C->modefunc);
    } else if (D_8C11204C->flags & 2) {
        B_8C6B6FEC = B_8C6B7010;
        B_8C6B6FEC.w[8] = D_8C11204C->timer;
        CAM_CALL_MODE(D_8C11204C->modefunc);
    } else {
        func_8C01B308((CAMENTRY_CC *)((Uint8 *)B_8C6B7064 + (D_8C11204C->entry << 6)));
        CAM_CALL_MODE(D_8C11204C->modefunc);
    }
    switch (D_8C11204C->kind) {
    case 3:
        func_8C01A60C(twp);
        break;
    case 2:
        func_8C01A680(twp);
        break;
    case 1:
        func_8C01A718(twp);
        break;
    }
    if (D_8C11204C->timer != 0 || !(D_8C11204C->flags & 1)) {
        if (B_8C6B7078 > 60u) {
            D_8C11204C->adjfunc(twp, D_8C112044, D_8C112050);
            D_8C112050->counter++;
        }
    }
}
#else
ASM_STUB(void func_8C019F4A(taskwk *twp))
#endif
