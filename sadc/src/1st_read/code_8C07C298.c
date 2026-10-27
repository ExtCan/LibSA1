/*
 * Player look-at / rotation API (event work, taskwk.ewp).
 * Chunk asm/1st_read/text/8C07BAC0.src, functions 8C07C298-8C07C376.
 *
 *  func_8C07C298  set player rotation y (through 8C07C9D2), clear ewp->lock
 *  func_8C07C2D0  clear ewp->lock
 *  func_8C07C2E2  look at a task (+ offset)       look mode 3
 *  func_8C07C318  look at a position              look mode 2
 *  func_8C07C34C  look at an angle                look mode 1
 *  func_8C07C376  stop looking                    look mode 0
 */
#include "common.h"
#include "ninja.h"
#include "task.h"
#include "sa1/sa1.h"

/*
 * Look-at part of the event work (eventwk 0x04-0x23). include/sa1/engine.h
 * still has these bytes in eventwk.pad01, so they are reached through this
 * file-local view of the start of eventwk (header change reported).
 * Writers: 8C07C2E2/8C07C318/8C07C34C/8C07C376.
 */
typedef struct {
    Sint8 mode;                 /* 0x00 eventwk.mode */
    Uint8 pad01[3];
    Sint8 look_mode;            /* 0x04 0 off, 1 angle, 2 position, 3 task (+ offset) */
    Uint8 pad05[3];
    Angle3 look_ang;            /* 0x08 mode 1 */
    NJS_POINT3 look_pos;        /* 0x14 mode 2: position, mode 3: offset from the task */
    task *look_tp;              /* 0x20 mode 3 */
} EVWK_LOOK;

SA1_ASSERT_OFFSET(EVWK_LOOK, look_mode, 0x04);
SA1_ASSERT_OFFSET(EVWK_LOOK, look_ang, 0x08);
SA1_ASSERT_OFFSET(EVWK_LOOK, look_pos, 0x14);
SA1_ASSERT_OFFSET(EVWK_LOOK, look_tp, 0x20);

#define EWP(twp)      ((eventwk *)(twp)->ewp)
#define EWP_LOOK(twp) ((EVWK_LOOK *)(twp)->ewp)

/* set rotation (x, 0x4000 - y, z) of player pno (twp->ang, mwp->ang_aim.y) */
void func_8C07C9D2(Uint8 pno, Angle ax, Angle ay, Angle az);

/* turn player pno to angle ang (y), keeping x and z (8C0AFE72, 8C0BBD3C, ...) */
void func_8C07C298(Uint8 pno, Angle ang)
{
    func_8C07C9D2(pno, playertwp[pno]->ang.x, ang, playertwp[pno]->ang.z);
    EWP(playertwp[pno])->lock = 0;
}

void func_8C07C2D0(Uint8 pno)
{
    EWP(playertwp[pno])->lock = 0;
}

/* look at task tp, offset by pos (may be NULL) */
void func_8C07C2E2(Uint8 pno, task *tp, NJS_POINT3 *pos)
{
    EVWK_LOOK *look;

    if (playertwp[pno] != NULL) {
        look = EWP_LOOK(playertwp[pno]);
        look->look_mode = 3;
        look->look_tp = tp;
        if (pos != NULL) {
            look->look_pos.x = pos->x;
            look->look_pos.y = pos->y;
            look->look_pos.z = pos->z;
        }
    }
}

/* look at position pos (may be NULL: keep the old one) */
void func_8C07C318(Uint8 pno, NJS_POINT3 *pos)
{
    EVWK_LOOK *look;

    if (playertwp[pno] != NULL) {
        look = EWP_LOOK(playertwp[pno]);
        look->look_mode = 2;
        if (pos != NULL) {
            look->look_pos.x = pos->x;
            look->look_pos.y = pos->y;
            look->look_pos.z = pos->z;
        }
    }
}

/* look in direction ang (may be NULL: keep the old one) */
void func_8C07C34C(Uint8 pno, Angle3 *ang)
{
    EVWK_LOOK *look;

    if (playertwp[pno] == NULL) {
        return;
    }
    look = EWP_LOOK(playertwp[pno]);
    look->look_mode = 1;
    if (ang == NULL) {
        return;
    }
    look->look_ang.x = ang->x;
    look->look_ang.y = ang->y;
    look->look_ang.z = ang->z;
}

/* stop looking */
void func_8C07C376(Uint8 pno)
{
    EVWK_LOOK *look;

    if (playertwp[pno] != NULL) {
        look = EWP_LOOK(playertwp[pno]);
        look->look_mode = 0;
    }
}

ASM_STUB(void func_8C07C9D2(Uint8 pno, Angle ax, Angle ay, Angle az))
