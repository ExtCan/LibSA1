#include "common.h"
#include "ninja.h"
#include "task.h"
#include "sa1/sa1.h"
#include "sa1/pending.h"

/*
 * 1ST_READ 8C090FA0-8C0915D2: the SET object manager task and its spawners
 * (staged by dc-set0 for G-SA1 0.9.4; docs/research/gsa1_090_rings.md 5.1).
 *
 * The translation unit runs to 8C091C1E (code_8C091B5E.c, code_8C091B8A.c,
 * code_8C091C00.c and code_8C091C10.c hold its matched tail). This file holds
 * the manager exec 8C090FA0, its mode-1 tail 8C090FFE, the range tests
 * 8C0910C0 / 8C09111A, the two spawners 8C091148 / 8C091380 and the spawner
 * choice 8C0915A2. Still assembly here: 8C091016 (mode 0, the stage-start
 * init) and 8C0915D2 (mode 2, the act change).
 *
 * Split: 8C090FFE and 8C0915D2 are reached only by tail BRAs from 8C090FA0
 * and have no function symbol in config/symbols.1st_read.txt yet
 * (build/staging/set0/LANDING.md).
 */

/* SET entry (SADX OBJ_EDIT_ENTRY), 0x20 bytes, in the stage's SET file */
typedef struct {
    Uint16 usID;                /* 0x00 bits 0-14: index into the object list */
    Sint16 rotx;                /* 0x02 */
    Sint16 roty;                /* 0x04 */
    Sint16 rotz;                /* 0x06 */
    NJS_POINT3 pos;             /* 0x08 */
    NJS_POINT3 scl;             /* 0x14 */
} SET_ENTRY;                    /* 0x20 */

/* Object list entry (SADX OBJ_ITEMENTRY), 0x14 bytes */
typedef struct {
    Uint8 ucInitMode;           /* 0x00 CreateElementalTask im */
    Uint8 ucLevel;              /* 0x01 CreateElementalTask level */
    Sint16 ssAttribute;         /* 0x02 bit 0: fRange, bit 1: always in range, bit 2: everything on the first pass */
    Float fRange;               /* 0x04 squared spawn distance (attribute bit 0) */
    void *ptr;                  /* 0x08 copied to the new task's twp->counter.ptr */
    TaskFuncPtr fnExec;         /* 0x0C */
    Sint8 *strObjName;          /* 0x10 */
} SET_ITEMENTRY;                /* 0x14 */

/* Object list (SADX OBJ_ITEMTABLE) */
typedef struct {
    Sint16 ssAttribute;         /* 0x00 */
    Sint16 ssCount;             /* 0x02 */
    SET_ITEMENTRY *pObjItemEntry; /* 0x04 */
} SET_ITEMTABLE;

/* Saved placement of a live object (condition bit 1): MAlloc(0x24) by the
 * range-out path at 8C091AE8 (pos 12-20, ang 0-8, scl 24-32 of the block) */
typedef struct {
    Angle3 ang;                 /* 0x00 */
    NJS_POINT3 pos;             /* 0x0C */
    NJS_POINT3 scl;             /* 0x18 */
} SET_SAVED;                    /* 0x24 */

SA1_ASSERT_SIZE(SET_ENTRY, 0x20);
SA1_ASSERT_OFFSET(SET_ENTRY, pos, 0x08);
SA1_ASSERT_OFFSET(SET_ENTRY, scl, 0x14);
SA1_ASSERT_SIZE(SET_ITEMENTRY, 0x14);
SA1_ASSERT_OFFSET(SET_ITEMENTRY, ptr, 0x08);
SA1_ASSERT_OFFSET(SET_ITEMENTRY, fnExec, 0x0C);
SA1_ASSERT_SIZE(SET_SAVED, 0x24);
SA1_ASSERT_OFFSET(SET_SAVED, pos, 0x0C);
SA1_ASSERT_OFFSET(SET_SAVED, scl, 0x18);

extern SET_ITEMTABLE *B_8C79A3CC;   /* the stage's object list (8C091016) */
extern void *B_8C79A3D0;            /* the SET file (8C091016) */
extern OBJ_CONDITION B_8C79A3E0[];  /* 1024 conditions, one per SET entry */
extern Sint16 B_8C79A3DC;           /* number of conditions in use */
extern Sint8 B_8C79E3D0;            /* the manager's level */
extern Sint8 B_8C79E3D1;            /* the manager's act */
extern Sint32 D_8C1C0ED0;
extern Sint32 D_8C1C0ED4;           /* act change request: the manager goes to mode 2 */
extern Sint32 D_8C1C0ED8;           /* 0 until the first spawner pass is done */
extern Uint16 B_8C7A40E8;           /* Big's fishing state bits (0x40: lure in the water) */
#if defined(TARGET_PC)
#define B_8C78C60C (&B_8C78C608[1])
#else
extern taskwk *B_8C78C60C[];        /* = &B_8C78C608[1] (separate symbol in the pool) */
#endif

void func_8C090FFE(taskwk *twp);
void func_8C091016(void);
void func_8C0915A2(void);
void func_8C0915D2(taskwk *twp);

/* Manager exec (task list 7, created by the stage start with
 * CreateElementalTask(2, 8, func_8C090FA0)): mode 0 stage init, mode 1 spawn,
 * mode 2 act change. */
void func_8C090FA0(task *tp)
{
    taskwk *twp = tp->twp;

    switch (twp->mode) {
    case 0:
        B_8C79A3D0 = NULL;
        B_8C79E3D0 = 0;
        B_8C79E3D1 = 0;
        D_8C1C0ED0 = 1;
        D_8C1C0ED4 = 0;
        func_8C091016();
        twp->mode = 1;
        D_8C1C0ED8 = 0;
        break;
    case 1:
        func_8C0915A2();
        func_8C090FFE(twp);
        break;
    case 2:
        func_8C0915D2(twp);
        break;
    }
}

/* Mode 1 tail: an act change request switches the manager to mode 2. */
void func_8C090FFE(taskwk *twp)
{
    if (D_8C1C0ED4) {
        twp->mode = 2;
        twp->smode = 0;
        D_8C1C0ED4 = 0;
    }
}

ASM_STUB(void func_8C091016(void))

/* In range: the point pos (x, y, z floats) is within the squared distance r
 * of (x, y, z), tested axis by axis (x^2, then x^2 + y^2, then the sum), or
 * within 40 units of the point p1 (player 1's position, may be NULL). A NaN
 * passes a test, as on the SH-4 (FCMP/GT). */
Sint32 func_8C0910C0(Float *pos, Float *p1, Float x, Float y, Float z, Float r)
{
    Float d;
    Float px, py, pz;

    px = *pos++;
    py = *pos++;
    pz = *pos;
    x -= px;
    d = x * x;
    if (!(d > r)) {
        y -= py;
        SA1_FMAC_ACC(d, y, y);
        if (!(d > r)) {
            z -= pz;
            SA1_FMAC_ACC(d, z, z);
            if (!(d > r)) {
                return 1;
            }
        }
    }
    if (p1) {
        px -= *p1++;
        r = 1600.0f;
        d = px * px;
        if (!(d > r)) {
            py -= *p1++;
            SA1_FMAC_ACC(d, py, py);
            if (!(d > r)) {
                pz -= *p1;
                SA1_FMAC_ACC(d, pz, pz);
                if (!(d > r)) {
                    return 1;
                }
            }
        }
    }
    return 0;
}

/* In range: the point pos is within the squared distance r of (x, y, z),
 * tested axis by axis as in func_8C0910C0. */
Sint32 func_8C09111A(Float *pos, Float x, Float y, Float z, Float r)
{
    Float d;
    Float px, py, pz;

    px = *pos++;
    py = *pos++;
    pz = *pos;
    x -= px;
    d = x * x;
    if (!(d > r)) {
        y -= py;
        SA1_FMAC_ACC(d, y, y);
        if (!(d > r)) {
            z -= pz;
            SA1_FMAC_ACC(d, z, z);
            if (!(d > r)) {
                return 1;
            }
        }
    }
    return 0;
}

/*
 * Spawner used while a player 1 other than Tails exists (follower worlds;
 * 8C0915A2 picks it). Every present (ssCondition bit 15), non-live (bit 0)
 * condition gets its object's task when the SET entry (or the placement saved
 * by a range-out, bit 1) is in range of player 0 (of the camera without one or
 * in event mode), or within 40 units of player 1. Range: the object list
 * entry's fRange (attribute bit 0), always (bit 1, which also writes 0 as the
 * range-out distance), everything on the first pass (bit 2), else 400 units.
 * The new task gets the condition (tp->ocp), the placement, twp->counter =
 * the list entry's ptr, and the condition the squared range-out distance
 * (range + 100) and the task.
 */
#ifdef NON_MATCHING
/* Functionally faithful (asm 8C091148-8C09134A); does not match: SHC
 * allocates the registers differently (the game keeps flags & 2 on the stack
 * and the loop's objects in R8-R14 in another order). */
void func_8C091148(void)
{
    taskwk *ptwp;
    Float *p1;
    OBJ_CONDITION *cond;
    SET_ENTRY *entry;
    SET_ITEMENTRY *ie;
    task *tp;
    taskwk *twp;
    Sint32 n;
    Sint32 in;
    Sint32 flags;
    Sint32 attr;
    Sint32 saved;
    Float x;
    Float y;
    Float z;
    Float range;

    if (playertwp[0] == NULL || D_8C19B468 == 1) {
        if ((ptwp = camera_twp) == NULL) {
            return;
        }
    } else {
        ptwp = playertwp[0];
    }
    x = ptwp->pos.x;
    y = ptwp->pos.y;
    z = ptwp->pos.z;
    if (playertwp[1]) {
        p1 = &playertwp[1]->pos.x;
    } else {
        p1 = NULL;
    }
    n = B_8C79A3DC;
    cond = B_8C79A3E0;
    for (; --n >= 0; cond++) {
        flags = cond->ssCondition;
        if (flags < 0 && !(flags & 1)) {
            entry = (SET_ENTRY *)cond->pObjEditEntry;
            ie = &B_8C79A3CC->pObjItemEntry[entry->usID & 0x7FFF];
            saved = flags & 2;
            attr = ie->ssAttribute;
            if (attr & 1) {
                range = ie->fRange;
            } else if (attr & 2) {
                range = -100.0f;
                goto forced;
            } else {
                range = 160000.0f;
            }
            if ((attr & 4) && D_8C1C0ED8 == 0) {
            forced:
                in = 1;
            } else if (saved == 0) {
                in = func_8C0910C0(&entry->pos.x, p1, x, y, z, range);
            } else {
                in = func_8C0910C0(&((SET_SAVED *)cond->unionStatus.ptr)->pos.x, p1, x, y, z, range);
            }
            if (in) {
                if ((tp = CreateElementalTask(ie->ucInitMode, ie->ucLevel, ie->fnExec)) != NULL) {
                    cond->scCount++;
                    cond->ssCondition |= 1;
                    tp->ocp = cond;
                    if ((twp = tp->twp) != NULL) {
                        if (saved) {
                            twp->pos = ((SET_SAVED *)cond->unionStatus.ptr)->pos;
                            twp->ang = ((SET_SAVED *)cond->unionStatus.ptr)->ang;
                            twp->scl = ((SET_SAVED *)cond->unionStatus.ptr)->scl;
                            MFree(cond->unionStatus.ptr);
                            cond->ssCondition &= ~2;
                        } else {
                            twp->pos = entry->pos;
                            twp->ang.x = entry->rotx;
                            twp->ang.y = entry->roty;
                            twp->ang.z = entry->rotz;
                            twp->scl = entry->scl;
                        }
                        twp->counter.ptr = ie->ptr;
                    }
                    cond->unionStatus.f = range + 100.0f;
                    cond->pTask = tp;
                }
            }
        }
    }
    D_8C1C0ED8 = 1;
}
#else
ASM_STUB(void func_8C091148(void))
#endif

/*
 * Spawner used when there is no player 1, when player 1 is Tails, or in event
 * mode (every libsa1 world today): as func_8C091148 without the player-1
 * test. The centre is the camera (D_8C18B7EC; without one nothing spawns)
 * when there is no player 0, in event mode, in level 42 and while Big's lure
 * is in the water (B_8C7A40E8 & 0x40).
 */
#ifdef NON_MATCHING
/* Functionally faithful (asm 8C091380-8C0915A0); does not match: register
 * allocation (the game holds the constant 1 in R8 and flags & 2 on the
 * stack; FR12 = range, FR15 = z). */
void func_8C091380(void)
{
    taskwk *ptwp;
    OBJ_CONDITION *cond;
    SET_ENTRY *entry;
    SET_ITEMENTRY *ie;
    task *tp;
    taskwk *twp;
    Sint32 n;
    Sint32 in;
    Sint32 flags;
    Sint32 attr;
    Sint32 saved;
    Float x;
    Float y;
    Float z;
    Float range;

    if (playertwp[0] == NULL || D_8C19B468 == 1 || (GetStageNumber() & 0xFF00) == 0x2A00
            || (GetPlayerNumber() == 7 && (B_8C7A40E8 & 0x40))) {
        if ((ptwp = camera_twp) == NULL) {
            return;
        }
    } else {
        ptwp = playertwp[0];
    }
    x = ptwp->pos.x;
    y = ptwp->pos.y;
    z = ptwp->pos.z;
    cond = B_8C79A3E0;
    for (n = B_8C79A3DC - 1; n >= 0; n--, cond++) {
        flags = cond->ssCondition;
        if (flags < 0 && !(flags & 1)) {
            entry = (SET_ENTRY *)cond->pObjEditEntry;
            ie = &B_8C79A3CC->pObjItemEntry[entry->usID & 0x7FFF];
            saved = flags & 2;
            attr = ie->ssAttribute;
            if (attr & 1) {
                range = ie->fRange;
            } else if (attr & 2) {
                range = -100.0f;
                goto forced;
            } else {
                range = 160000.0f;
            }
            if ((attr & 4) && D_8C1C0ED8 == 0) {
            forced:
                in = 1;
            } else if (saved == 0) {
                in = func_8C09111A(&entry->pos.x, x, y, z, range);
            } else {
                in = func_8C09111A(&((SET_SAVED *)cond->unionStatus.ptr)->pos.x, x, y, z, range);
            }
            if (in) {
                if ((tp = CreateElementalTask(ie->ucInitMode, ie->ucLevel, ie->fnExec)) != NULL) {
                    cond->scCount++;
                    cond->ssCondition |= 1;
                    tp->ocp = cond;
                    if ((twp = tp->twp) != NULL) {
                        if (saved) {
                            twp->pos = ((SET_SAVED *)cond->unionStatus.ptr)->pos;
                            twp->ang = ((SET_SAVED *)cond->unionStatus.ptr)->ang;
                            twp->scl = ((SET_SAVED *)cond->unionStatus.ptr)->scl;
                            MFree(cond->unionStatus.ptr);
                            cond->ssCondition &= ~2;
                        } else {
                            twp->pos = entry->pos;
                            twp->ang.x = entry->rotx;
                            twp->ang.y = entry->roty;
                            twp->ang.z = entry->rotz;
                            twp->scl = entry->scl;
                        }
                        twp->counter.ptr = ie->ptr;
                    }
                    cond->unionStatus.f = range + 100.0f;
                    cond->pTask = tp;
                }
            }
        }
    }
}
#else
ASM_STUB(void func_8C091380(void))
#endif
/* Mode 1: picks the spawner. */
void func_8C0915A2(void)
{
    if (B_8C78C60C[0] == NULL || D_8C19B468 == 1 || (Uint8)B_8C78C60C[0]->counter.b[1] == 2) {
        func_8C091380();
    } else {
        func_8C091148();
    }
    D_8C1C0ED8 = 1;
}

ASM_STUB(void func_8C0915D2(taskwk *twp))
