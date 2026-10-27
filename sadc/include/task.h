#ifndef TASK_H
#define TASK_H

#include "ninja.h"

/*
 * Task (object) system. Layouts follow the game's own field names as known
 * from Sonic Team's later releases; offsets are verified by matched code and
 * by the binary (CreateElementalTask 8C09852E allocates the taskwk and the
 * motionwk with CAlloc(1, 0x40) each).
 *
 * PORTABILITY (docs/portability.md): these are the DC layouts only when
 * SA1_PTR32. On 64-bit hosts pointers widen and every field moves, so code
 * must use the typed fields, sizeof() and never raw offsets.
 *
 * Types named sa1_<name> (here: sa1_forcewk, sa1_anywk, sa1_OBJ_CONDITION)
 * are shared definitions whose canonical name <name> is still defined
 * locally by unmigrated src files; sa1/pending.h maps <name> to them
 * (docs/header_migration.md).
 */

struct task;
struct playerwk;                /* include/sa1/player.h */
typedef void (*TaskFuncPtr)(struct task *tp);

/*
 * 4-byte scratch union (taskwk counter/timer/value, motionwk work).
 * A pointer kept here must be written and read through a pointer member
 * (.ptr, .tp, .pwp), never through .l: on 64-bit hosts the union is 8 bytes
 * and .l holds only 32 bits of it.
 */
typedef union {
    Sint8 b[4];
    Sint16 w[2];
    Sint32 l;
    Float f;
    void *ptr;
    struct task *tp;
    struct playerwk *pwp;       /* motionwk.work of player tasks (PInitialize 8C06C350) */
} TaskWkUnion;

/*
 * taskwk.flag bits seen in the player code (SADX Status_* names where they
 * agree): 0x0001/0x0002 on ground (flag & 3, 8C0B16BC), 0x0008 cleared on
 * pickup, 0x0020 hit by a collision object (SonicMain 8C0A77F4 copies the
 * hit task's ang.x/z), 0x0040 required on a hit object (8C06C880), 0x0100
 * attack state (8C0A77BC), 0x0800 holding an object (8C0B16BC; SADX
 * Status_HoldObject), 0x1000 input/position request pending (SetInputP;
 * also set on the held object), 0x2000 following a path (taskwk.value =
 * PATHTAG *, 8C06F530/8C07C120; SADX Status_OnPath), 0x4000 event lock
 * (eventwk.lock drives the player, 8C06EAC0/8C06EFCE).
 */
#define TWF_GROUND      0x0003
#define TWF_HOLD        0x0800
#define TWF_REQUEST     0x1000
#define TWF_ONPATH      0x2000
#define TWF_EVENTLOCK   0x4000

typedef struct taskwk {
    Sint8 mode;                 /* 0x00 */
    Sint8 smode;                /* 0x01 player request/state number (SetInputP stores it) */
    Uint8 id;                   /* 0x02 object class; every read is MOV.B + EXTU.B (8C06C880 compares 1/3/4-7; 8C06CA30, 8C07D060, 8C04B7BA, 8C0AFB10) */
    Uint8 btimer;               /* 0x03 effect tasks: sprite/parameter index (read EXTU.B by 8C0B3B1A, 8C0B58E0) */
    Sint16 flag;                /* 0x04 TWF_* bits above */
    Uint16 wtimer;              /* 0x06 */
    TaskWkUnion counter;        /* 0x08 players: b[0] = player number, b[1] = character id; effects: counter.ptr = parameter table (8C0B33A0) */
    TaskWkUnion timer;          /* 0x0C snowboard: timer.ptr = NJS_OBJECT * (8C0B2EA0); effects: timer.l = life */
    TaskWkUnion value;          /* 0x10 players: value.ptr = PATHTAG * while TWF_ONPATH (8C06F530), value.tp = handed task (smode 0x25, 8C07D60E); effects: value.ptr = NJS_TEXLIST * or list head */
    Angle3 ang;                 /* 0x14 */
    NJS_POINT3 pos;             /* 0x20 */
    NJS_POINT3 scl;             /* 0x2C bubble emitter: scl.z = player number as a float (8C0B3B1A) */
    void *cwp;                  /* 0x38 colliwk * (include/sa1/ccl.h, CCL_Init 8C02F4B0); CWP(twp) */
    void *ewp;                  /* 0x3C eventwk * (include/sa1/engine.h, 8C04B440); EWP(twp) */
} taskwk;                       /* 0x40 */

/*
 * Motion work (task->mwp). SA1 DC uses the layout SADX calls motionwk2:
 * a 4-byte work union first, speed at +4 (verified: player physics
 * 8C06D254/8C072174 and the animal code read spd at +4; PInitialize 8C06C350
 * stores the playerwk pointer at +0 and 1.0f at +0x3C).
 * In the player code rad/height/weight are per-frame scratch, not a radius
 * or a height (SADX names kept): see the field comments.
 */
typedef struct motionwk {
    TaskWkUnion work;           /* 0x00 player tasks: work.pwp */
    NJS_POINT3 spd;             /* 0x04 */
    NJS_POINT3 acc;             /* 0x10 read by PGetAcceleration 8C06D254; written by 8C07CB54 */
    Angle3 ang_aim;             /* 0x1C ang_aim.y read/written by ~40 player functions; masked to 16 bits by 8C079096 */
    Angle3 ang_spd;             /* 0x28 */
    Float rad;                  /* 0x34 players: forward drive of this frame (stroke * run_accel, 0 without input: 8C06D254, 8C06E4E0, 8C06FB8C); snowboard lean (8C0700C2, 8C07095E); on-path flag 0/1 (8C078308..8C078710); Sonic turn amount, signed (8C0AEBC0, 8C0AE760) */
    Float height;               /* 0x38 players: friction excess s - lim (8C06D254, 8C06DCA2, 8C06E4E0, 8C06EAC0); speed bias read by 8C0AE588; cleared each frame by 8C06C6F8 */
    Float weight;               /* 0x3C PInitialize: 1.0f; players: friction / surface acceleration multiplier (8C06D254, 8C070EDA, 8C0751C0) */
} motionwk;                     /* 0x40 */
typedef motionwk motionwk2;     /* SADX name of the same layout */

/*
 * Task. NOTE (unresolved, docs/header_migration.md): SA1 DC tasks are 0x30
 * bytes: the pool init 8C09844A MAllocs 0x30000 for 0x1000 tasks and links
 * them with ADD #48, and DestroyTask/8C0985EC touch only 0x00-0x2C
 * (matched code_8C098444.c). thp is the SADX field and does not exist in
 * SA1 DC; it is kept because existing field offsets/sizes are frozen. Code
 * must not use thp or sizeof(task) for DC memory (code_8C098444.c uses its
 * own TASK_SIZE).
 * Under SA1_GA (pclower / libsa1 guest arena, design decision D12) thp is
 * dropped: there the task is the DC 0x30 bytes, asserted below. SHC and
 * TARGET_PC without SA1_GA keep thp (0x34).
 */
typedef struct task {
    struct task *next;          /* 0x00 */
    struct task *last;          /* 0x04 */
    struct task *ptp;           /* 0x08 parent */
    struct task *ctp;           /* 0x0C first child */
    TaskFuncPtr exec;           /* 0x10 */
    TaskFuncPtr disp;           /* 0x14 */
    TaskFuncPtr dest;           /* 0x18 */
    void *ocp;                  /* 0x1C OBJ_CONDITION * (SET data; DestroyTask 8C098752 clears ssCondition bit 0) */
    taskwk *twp;                /* 0x20 CreateElementalTask im & 2: CAlloc(1, 0x40) */
    motionwk *mwp;              /* 0x24 im & 1: CAlloc(1, 0x40); the player face task keeps a PL_FACE here (8C0B2338, 8C07B106) */
    void *fwp;                  /* 0x28 forcewk *, im & 4: CAlloc(2, 0x1C) (8C07506E uses fwp[1] for players other than 0) */
    void *awp;                  /* 0x2C anywk *, im & 8: CAlloc(1, 0x10); some tasks CAlloc their own (PL_AIWK, 0x9C) */
#if !defined(SA1_GA)
    void *thp;                  /* 0x30 not in SA1 DC (see above) */
#endif
} task;                         /* 0x34 (DC and SA1_GA: 0x30) */

/*
 * Force work (task.fwp; SADX forcewk). CreateElementalTask allocates
 * CAlloc(2, 0x1C). Used by moving land objects to carry a player standing on
 * them: 8C07506E calls call_back(playertp[pno], twp, fwp), adds pos_spd to
 * csts.pos and ang_spd to twp->ang (16-bit wrap); 8C056A04, 8C076782 and
 * 8C0AFC98 read pos_spd at +0x10..+0x18 and ang_spd at +0x04..+0x0C.
 * Canonical name forcewk: sa1/pending.h.
 */
typedef struct sa1_forcewk {
    void (*call_back)(struct task *tp, taskwk *twp, struct sa1_forcewk *fwp); /* 0x00 */
    Angle3 ang_spd;             /* 0x04 */
    NJS_POINT3 pos_spd;         /* 0x10 */
} sa1_forcewk;                  /* 0x1C */

/* Any work (task.awp, CAlloc(1, 0x10); SADX anywk). Canonical name anywk:
 * sa1/pending.h. Task-specific views of the same 0x10 bytes are in
 * sa1/player.h (sa1_PL_SPDUPWK, sa1_DROWNWK, ...). */
typedef struct sa1_anywk {
    TaskWkUnion work[4];        /* 0x00 */
} sa1_anywk;                    /* 0x10 */

/* SET object condition (task.ocp; SADX OBJ_CONDITION). DestroyTask
 * 8C098752-8C09875A does ocp->ssCondition &= ~1 (MOV.W @(2) / AND #-2).
 * Canonical name OBJ_CONDITION: sa1/pending.h. */
typedef struct sa1_OBJ_CONDITION {
    Sint8 scCount;              /* 0x00 */
    Sint8 scUserFlag;           /* 0x01 */
    Sint16 ssCondition;         /* 0x02 */
    struct task *pTask;         /* 0x04 created task (SET loader 8C091148: MOV.L R13,@(4,R11) at 8C09131C) */
    void *pObjEditEntry;        /* 0x08 SET entry */
    union { Float f; void *ptr; } unionStatus; /* 0x0C */
} sa1_OBJ_CONDITION;            /* 0x10 (array B_8C79A3E0 walked with ADD #16 at 8C09131E) */

SA1_ASSERT_SIZE(TaskWkUnion, 0x04);
SA1_ASSERT_SIZE(taskwk, 0x40);
SA1_ASSERT_OFFSET(taskwk, id, 0x02);
SA1_ASSERT_OFFSET(taskwk, btimer, 0x03);
SA1_ASSERT_OFFSET(taskwk, flag, 0x04);
SA1_ASSERT_OFFSET(taskwk, wtimer, 0x06);
SA1_ASSERT_OFFSET(taskwk, counter, 0x08);
SA1_ASSERT_OFFSET(taskwk, timer, 0x0C);
SA1_ASSERT_OFFSET(taskwk, value, 0x10);
SA1_ASSERT_OFFSET(taskwk, ang, 0x14);
SA1_ASSERT_OFFSET(taskwk, pos, 0x20);
SA1_ASSERT_OFFSET(taskwk, scl, 0x2C);
SA1_ASSERT_OFFSET(taskwk, cwp, 0x38);
SA1_ASSERT_OFFSET(taskwk, ewp, 0x3C);
SA1_ASSERT_SIZE(motionwk, 0x40);
SA1_ASSERT_OFFSET(motionwk, spd, 0x04);
SA1_ASSERT_OFFSET(motionwk, acc, 0x10);
SA1_ASSERT_OFFSET(motionwk, ang_aim, 0x1C);
SA1_ASSERT_OFFSET(motionwk, ang_spd, 0x28);
SA1_ASSERT_OFFSET(motionwk, rad, 0x34);
SA1_ASSERT_OFFSET(motionwk, height, 0x38);
SA1_ASSERT_OFFSET(motionwk, weight, 0x3C);
#if defined(SA1_GA)
SA1_ASSERT_SIZE(task, 0x30);            /* D12: the SA1 DC task */
#else
SA1_ASSERT_SIZE(task, 0x34);
#endif
SA1_ASSERT_OFFSET(task, exec, 0x10);
SA1_ASSERT_OFFSET(task, ocp, 0x1C);
SA1_ASSERT_OFFSET(task, twp, 0x20);
SA1_ASSERT_OFFSET(task, mwp, 0x24);
SA1_ASSERT_OFFSET(task, fwp, 0x28);
SA1_ASSERT_OFFSET(task, awp, 0x2C);
#if !defined(SA1_GA)
SA1_ASSERT_OFFSET(task, thp, 0x30);
#endif
SA1_ASSERT_SIZE(sa1_forcewk, 0x1C);
SA1_ASSERT_OFFSET(sa1_forcewk, ang_spd, 0x04);
SA1_ASSERT_OFFSET(sa1_forcewk, pos_spd, 0x10);
SA1_ASSERT_SIZE(sa1_anywk, 0x10);
SA1_ASSERT_SIZE(sa1_OBJ_CONDITION, 0x10);
SA1_ASSERT_OFFSET(sa1_OBJ_CONDITION, ssCondition, 0x02);

#endif
