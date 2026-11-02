/*
 * 1ST_READ 8C01AAE0-8C01B1FA, 8C01B358, 8C01B3E0-8C01B40E, 8C01C28E (chunk
 * 8C019400): the camera request stack and its helpers (staged by dc-fishcam
 * for 0.9.6; docs/research/gsa1_090_bigfish.md 2.1, F1 and its review R2.6;
 * v1_triggers.md 1.2-1.5). The conversion kinds are in code_8C01A60C.c; the
 * rest of the chunk stays assembly.
 *
 *  func_8C01AAE0  set the NORMAL (level 0) camera: mode index + adjust (SADX
 *                 CameraSetNormalCamera analogue).
 *  func_8C01ABDC  push an EVENT (level 2) camera by mode index + adjust
 *                 (SADX CameraSetEventCamera analogue).
 *  func_8C01AD2C  push an EVENT camera by mode function + adjust +
 *                 conversion kind (SADX CameraSetEventCameraFunc).
 *  func_8C01AE60  pop the EVENT camera (SADX CameraReleaseEventCamera).
 *  func_8C01AF06  push a COLI (level 4) camera by function + adjust + kind.
 *  func_8C01B040  push a COLI camera by mode index + adjust (the free
 *                 camera SURVEY, 8C019ADC).
 *  func_8C01B158  pop the COLI camera.
 *  func_8C01B358  exec of the task every camera change creates: frees it.
 *  func_8C01B3E0  set the current adjust (and its function), counter 0.
 *  func_8C01B406  the current adjust.
 *  func_8C01B436  end the current adjust when the camera has settled: moved
 *                 under 0.1 units and every angle under 91 BAMS (the exec
 *                 8C0197BA calls it after the core 8C019F4A; matching C from
 *                 tools/tests/compiler_probe/t02_gamestat.c; here because
 *                 the original reaches 8C01B3E0 by BRA).
 *  func_8C01C28E  the camera's sweep: func_8C0569F0 with attribute mask
 *                 0x400002.
 *
 * Every push does nothing while a higher level is active (EVENT: level > 2,
 * COLI: level > 4). When it raises the level it saves the state it covers in
 * the new level's slot (level, CAM entry, flags, mode, mode function, and the
 * adjust to return with: the CAM entry's own adjust, or 0x15 when no entry
 * is active). A pop acts only at its own level and restores that slot, so
 * the camera returns to the covered one with the saved adjust (0x15 =
 * RELATIVE timed blend, func_8C02535E). When a pop lands on level 0 and a
 * NORMAL camera change is pending (B_8C6B7084 != -1), it is applied through
 * func_8C01AAE0.
 *
 * CreateElementalTask(8, 6, func_8C01B358): every camera change (also
 * 8C019BE6, 8C019C50, 8C019DC4, 8C01A7D6, 8C01A9B8, 8C01AAE0, 8C01B240)
 * creates a level-6 task with a 0x10-byte any work whose exec frees it on
 * its first run. Nothing reads the task or its work (no pointer to
 * func_8C01B358 outside these call sites; the task pointer is dropped), so
 * its only effects are on the task pool and the heap between the change and
 * the next level-6 pass. Purpose UNVERIFIED (a leftover of a debug/notify
 * task is the likeliest reading).
 */
#include "sa1/sa1.h"

/* mode function: the camera task calls it with the camera parameters
 * B_8C6B6FEC (8C019F4A); written as the canonical prototype of
 * func_8C01AD2C's argument (include/sa1/protos.h) */
typedef void (*CAMMODEFUNC_FC)(void);

/* adjust (transition) function: (camera taskwk, previous camera taskwk
 * *D_8C112044, adjust work *D_8C112050), 8C019F4A */
struct CAMADJUSTWK_FC;
typedef void (*CAMADJUSTFUNC_FC)(taskwk *twp, taskwk *otwp, struct CAMADJUSTWK_FC *awp);

/* camera control work *D_8C11204C (v1_triggers.md 1.2; SADX camcont_wp
 * analogue). The save slots are indexed by level: 2 = EVENT, 4 = COLI. */
typedef struct {
    Sint32 dirty;               /* 0x00 set to 1 by every camera change */
    Sint16 entry;               /* 0x04 current CAM entry, -1 = none */
    Sint8 mode;                 /* 0x06 index into the mode table, -1 = by function */
    Sint8 kind;                 /* 0x07 conversion kind 1..3 (8C01A718 / 8C01A680 / 8C01A60C) */
    Sint8 level;                /* 0x08 0 NORMAL, 1 AREA, 2 EVENT, 3 COMP, 4 COLI, 5 DEBUG */
    Sint8 adjust;               /* 0x09 index into the adjust table */
    Sint8 pad0A;                /* 0x0A */
    Sint8 flags;                /* 0x0B bit 1 = set by code, bit 0 = the adjust's flag */
    CAMMODEFUNC_FC modefunc;    /* 0x0C */
    CAMADJUSTFUNC_FC adjfunc;   /* 0x10 */
    Sint32 timer;               /* 0x14 steps since the last change */
    Sint32 slevel[6];           /* 0x18 level covered by the push of level i */
    Sint16 sentry[6];           /* 0x30 */
    Sint8 sflags[6];            /* 0x3C */
    Sint8 pad42[2];             /* 0x42 */
    CAMMODEFUNC_FC smodefunc[6];   /* 0x44 */
    CAMADJUSTFUNC_FC sadjfunc[6];  /* 0x5C */
    Sint8 smode[6];             /* 0x74 */
    Sint8 sadjust[6];           /* 0x7A */
} CAMCONTROLWK_FC;              /* 0x80 */

/* adjust work *D_8C112050 (SADX _OBJ_ADJUSTPARAM analogue); only the
 * counter is used here */
typedef struct CAMADJUSTWK_FC {
    Sint16 flag;                /* 0x00 */
    Sint16 pad02;               /* 0x02 */
    Sint32 angspdx;             /* 0x04 */
    Sint32 angspdy;             /* 0x08 */
    Sint32 angspdz;             /* 0x0C */
    Float speed;                /* 0x10 */
    Sint32 counter;             /* 0x14 steps of the running adjust (8C019F4A) */
} CAMADJUSTWK_FC;               /* 0x18 */

/* mode table entry (8C195310, 76 entries; pointer B_8C6B7054) */
typedef struct {
    CAMMODEFUNC_FC func;        /* 0x00 */
    Sint8 level;                /* 0x04 */
    Sint8 kind;                 /* 0x05 */
    Sint8 pad06[2];             /* 0x06 */
    Sint32 normal;              /* 0x08 selectable as NORMAL by the debug cycle */
    CAMMODEFUNC_FC func2;       /* 0x0C */
} CAMMODE_FC;                   /* 0x10 */

/* adjust table entry (8C1957D0, 27 entries; pointer B_8C6B705C) */
typedef struct {
    Sint32 flag;                /* 0x00 nonzero: the "S" variants */
    CAMADJUSTFUNC_FC func;      /* 0x04 */
} CAMADJUST_FC;                 /* 0x08 */

/* CAM file entry (0x0CB50000.. + 0x40; pointer B_8C6B7064; v1_triggers.md
 * 1.6); only the adjust is read here. No pointers. */
typedef struct {
    Sint8 mode;                 /* 0x00 */
    Sint8 prio;                 /* 0x01 */
    Uint8 adjust;               /* 0x02 adjust used when the volume is entered */
    Sint8 shape;                /* 0x03 */
    Uint8 rest[0x3C];           /* 0x04 angles, centre, size, camera angles, points A / B, distance */
} CAMENTRY_FC;                  /* 0x40 */

extern CAMCONTROLWK_FC *D_8C11204C;
extern CAMADJUSTWK_FC *D_8C112050;
extern CAMMODE_FC *B_8C6B7054;      /* mode table (8C195310) */
extern CAMADJUST_FC *B_8C6B705C;    /* adjust table (8C1957D0) */
extern CAMENTRY_FC *B_8C6B7064;     /* the act's CAM entries (file + 0x40) */
extern Sint32 B_8C6B707C;           /* cleared by the EVENT pop (reader UNVERIFIED) */
extern Sint32 B_8C6B7084;           /* pending NORMAL mode, -1 = none */
extern Sint32 B_8C6B7088;           /* pending NORMAL adjust */

void func_8C01B358(task *tp);

/*
 * Prototypes: func_8C01AD2C has the canonical one of include/sa1/protos.h.
 * func_8C01ABDC / func_8C01B040 take (Sint32 mode, Sint32 adjust) and narrow
 * at the table index (EXTS.W / EXTU.B where the original extends), and
 * func_8C01AF06 takes Sint32 bytes, as their C callers already declare them
 * (code_8C0C450A.c, code_8C0C193A.c, code_8C0E6500.c, code_8C0E91E4.c), so
 * pclower sees no new prototype conflict (R8); the narrow forms (Sint16,
 * Uint8) / (gfn, Sint8, Sint8) compile to the same callee code. func_8C01AAE0
 * keeps (Sint16, Uint8): its only C callers are the pops here, and with
 * Sint32 parameters SHC allocates the pop's last store differently.
 */
Bool func_8C0569F0(NJS_POINT3 *pos, NJS_POINT3 *v, Angle3 *ang, Uint32 mask, Float r); /* sphere sweep (code_8C05697C.c) */

void func_8C01AAE0(Sint16 mode, Uint8 adjust)
{
    if (D_8C11204C->level != 0) {
        return;
    }
    D_8C11204C->slevel[0] = D_8C11204C->level;
    D_8C11204C->sentry[0] = D_8C11204C->entry;
    D_8C11204C->sflags[0] = D_8C11204C->flags;
    D_8C11204C->smode[0] = D_8C11204C->mode;
    D_8C11204C->smodefunc[0] = D_8C11204C->modefunc;
    if (D_8C11204C->entry != -1) {
        D_8C11204C->sadjust[0] = B_8C6B7064[D_8C11204C->entry].adjust;
    } else {
        D_8C11204C->sadjust[0] = 21;
    }
    D_8C11204C->sadjfunc[0] = B_8C6B705C[D_8C11204C->sadjust[0]].func;
    D_8C11204C->entry = -1;
    D_8C11204C->level = 0;
    D_8C11204C->mode = mode;
    D_8C11204C->modefunc = B_8C6B7054[mode].func;
    D_8C11204C->adjust = adjust;
    D_8C11204C->adjfunc = B_8C6B705C[adjust].func;
    D_8C112050->counter = 0;
    if (B_8C6B705C[D_8C11204C->adjust].flag != 0) {
        D_8C11204C->flags |= 1;
    }
    D_8C11204C->kind = B_8C6B7054[D_8C11204C->mode].kind;
    D_8C11204C->dirty = 1;
    CreateElementalTask(8, 6, func_8C01B358);
}

void func_8C01ABDC(Sint32 mode, Sint32 adjust)
{
    if (D_8C11204C->level > 2) {
        return;
    }
    if (D_8C11204C->level != 2) {
        D_8C11204C->slevel[2] = D_8C11204C->level;
        D_8C11204C->sentry[2] = D_8C11204C->entry;
        D_8C11204C->sflags[2] = D_8C11204C->flags;
        D_8C11204C->smode[2] = D_8C11204C->mode;
        D_8C11204C->smodefunc[2] = D_8C11204C->modefunc;
        if (D_8C11204C->entry != -1) {
            D_8C11204C->sadjust[2] = B_8C6B7064[D_8C11204C->entry].adjust;
        } else {
            D_8C11204C->sadjust[2] = 21;
        }
        D_8C11204C->sadjfunc[2] = B_8C6B705C[D_8C11204C->sadjust[2]].func;
        D_8C11204C->level = 2;
    }
    D_8C11204C->entry = -1;
    D_8C11204C->mode = mode;
    D_8C11204C->modefunc = B_8C6B7054[(Sint16)mode].func;
    D_8C11204C->adjust = adjust;
    D_8C11204C->adjfunc = B_8C6B705C[(Uint8)adjust].func;
    D_8C112050->counter = 0;
    D_8C11204C->flags = 2;
    if (B_8C6B705C[D_8C11204C->adjust].flag != 0) {
        D_8C11204C->flags |= 1;
    }
    D_8C11204C->kind = B_8C6B7054[D_8C11204C->mode].kind;
    D_8C11204C->dirty = 1;
    CreateElementalTask(8, 6, func_8C01B358);
}

void func_8C01AD2C(void (*func)(void), Sint8 adjust, Sint8 direct)
{
    if (D_8C11204C->level > 2) {
        return;
    }
    if (D_8C11204C->level != 2) {
        D_8C11204C->slevel[2] = D_8C11204C->level;
        D_8C11204C->sentry[2] = D_8C11204C->entry;
        D_8C11204C->sflags[2] = D_8C11204C->flags;
        D_8C11204C->smode[2] = D_8C11204C->mode;
        D_8C11204C->smodefunc[2] = D_8C11204C->modefunc;
        if (D_8C11204C->entry != -1) {
            D_8C11204C->sadjust[2] = B_8C6B7064[D_8C11204C->entry].adjust;
        } else {
            D_8C11204C->sadjust[2] = 21;
        }
        D_8C11204C->sadjfunc[2] = B_8C6B705C[D_8C11204C->sadjust[2]].func;
        D_8C11204C->level = 2;
    }
    D_8C11204C->entry = -1;
    D_8C11204C->mode = -1;
    D_8C11204C->modefunc = func;
    D_8C11204C->adjust = adjust;
    D_8C11204C->adjfunc = B_8C6B705C[D_8C11204C->adjust].func;
    D_8C112050->counter = 0;
    D_8C11204C->flags = 2;
    if (B_8C6B705C[D_8C11204C->adjust].flag != 0) {
        D_8C11204C->flags |= 1;
    }
    D_8C11204C->kind = direct;
    D_8C11204C->dirty = 1;
    CreateElementalTask(8, 6, func_8C01B358);
}

void func_8C01AE60(void)
{
    if (D_8C11204C->level == 2) {
        D_8C11204C->entry = D_8C11204C->sentry[2];
        D_8C11204C->flags = D_8C11204C->sflags[2];
        D_8C11204C->mode = D_8C11204C->smode[2];
        D_8C11204C->modefunc = D_8C11204C->smodefunc[2];
        D_8C11204C->adjust = D_8C11204C->sadjust[2];
        D_8C11204C->adjfunc = D_8C11204C->sadjfunc[2];
        D_8C11204C->level = D_8C11204C->slevel[2];
        D_8C11204C->kind = B_8C6B7054[D_8C11204C->mode].kind;
        D_8C11204C->dirty = 1;
        D_8C11204C->timer = 0;
        D_8C112050->counter = 0;
        CreateElementalTask(8, 6, func_8C01B358);
        if (D_8C11204C->level == 0 && B_8C6B7084 != -1) {
            func_8C01AAE0(B_8C6B7084, B_8C6B7088);
        }
    }
    B_8C6B707C = 0;
}

void func_8C01AF06(void (*func)(void), Sint32 adjust, Sint32 direct)
{
    if (D_8C11204C->level <= 4) {
        if (D_8C11204C->level != 4) {
            D_8C11204C->slevel[4] = D_8C11204C->level;
            D_8C11204C->sentry[4] = D_8C11204C->entry;
            D_8C11204C->sflags[4] = D_8C11204C->flags;
            D_8C11204C->smode[4] = D_8C11204C->mode;
            D_8C11204C->smodefunc[4] = D_8C11204C->modefunc;
            if (D_8C11204C->entry != -1) {
                D_8C11204C->sadjust[4] = B_8C6B7064[D_8C11204C->entry].adjust;
            } else {
                D_8C11204C->sadjust[4] = 21;
            }
            D_8C11204C->sadjfunc[4] = B_8C6B705C[D_8C11204C->sadjust[4]].func;
            D_8C11204C->level = 4;
        }
        D_8C11204C->entry = -1;
        D_8C11204C->mode = -1;
        D_8C11204C->modefunc = func;
        D_8C11204C->adjust = adjust;
        D_8C11204C->adjfunc = B_8C6B705C[D_8C11204C->adjust].func;
        D_8C112050->counter = 0;
        D_8C11204C->flags = 2;
        if (B_8C6B705C[D_8C11204C->adjust].flag != 0) {
            D_8C11204C->flags |= 1;
        }
        D_8C11204C->kind = direct;
        D_8C11204C->dirty = 1;
        CreateElementalTask(8, 6, func_8C01B358);
    }
}

void func_8C01B040(Sint32 mode, Sint32 adjust)
{
    if (D_8C11204C->level <= 4) {
        if (D_8C11204C->level != 4) {
            D_8C11204C->slevel[4] = D_8C11204C->level;
            D_8C11204C->sentry[4] = D_8C11204C->entry;
            D_8C11204C->sflags[4] = D_8C11204C->flags;
            D_8C11204C->smode[4] = D_8C11204C->mode;
            D_8C11204C->smodefunc[4] = D_8C11204C->modefunc;
            if (D_8C11204C->entry != -1) {
                D_8C11204C->sadjust[4] = B_8C6B7064[D_8C11204C->entry].adjust;
            } else {
                D_8C11204C->sadjust[4] = 21;
            }
            D_8C11204C->sadjfunc[4] = B_8C6B705C[D_8C11204C->sadjust[4]].func;
            D_8C11204C->level = 4;
        }
        D_8C11204C->entry = -1;
        D_8C11204C->mode = mode;
        D_8C11204C->modefunc = B_8C6B7054[(Sint16)mode].func;
        D_8C11204C->adjust = adjust;
        D_8C11204C->adjfunc = B_8C6B705C[(Uint8)adjust].func;
        D_8C112050->counter = 0;
        D_8C11204C->flags = 2;
        if (B_8C6B705C[D_8C11204C->adjust].flag != 0) {
            D_8C11204C->flags |= 1;
        }
        D_8C11204C->kind = B_8C6B7054[D_8C11204C->mode].kind;
        D_8C11204C->dirty = 1;
        CreateElementalTask(8, 6, func_8C01B358);
    }
}

void func_8C01B158(void)
{
    if (D_8C11204C->level == 4) {
        D_8C11204C->entry = D_8C11204C->sentry[4];
        D_8C11204C->flags = D_8C11204C->sflags[4];
        D_8C11204C->mode = D_8C11204C->smode[4];
        D_8C11204C->modefunc = D_8C11204C->smodefunc[4];
        D_8C11204C->adjust = D_8C11204C->sadjust[4];
        D_8C11204C->adjfunc = D_8C11204C->sadjfunc[4];
        D_8C11204C->level = D_8C11204C->slevel[4];
        D_8C11204C->kind = B_8C6B7054[D_8C11204C->mode].kind;
        D_8C11204C->dirty = 1;
        D_8C11204C->timer = 0;
        D_8C112050->counter = 0;
        CreateElementalTask(8, 6, func_8C01B358);
        if (D_8C11204C->level == 0 && B_8C6B7084 != -1) {
            func_8C01AAE0(B_8C6B7084, B_8C6B7088);
        }
    }
}

void func_8C01B358(task *tp)
{
    FreeTask(tp);
}

void func_8C01B3E0(Sint8 adjust)
{
    D_8C11204C->adjust = adjust;
    D_8C11204C->adjfunc = B_8C6B705C[D_8C11204C->adjust].func;
    D_8C112050->counter = 0;
}

Sint32 func_8C01B406(void)
{
    return D_8C11204C->adjust;
}

void func_8C01B436(taskwk *a, taskwk *b)
{
    Float dx, dy, dz, d;

    dx = a->pos.x - b->pos.x;
    dy = a->pos.y - b->pos.y;
    dz = a->pos.z - b->pos.z;
    d = dx * dx + dy * dy + dz * dz;
    if (d < 0.0099999986f
        && DiffAngle(a->ang.x, b->ang.x) < 91
        && DiffAngle(a->ang.y, b->ang.y) < 91
        && DiffAngle(a->ang.z, b->ang.z) < 91) {
        switch (D_8C11204C->adjust) {
        case 1:
            func_8C01B3E0(3);
            break;
        case 0:
        case 2:
        case 3:
        case 4:
            func_8C01B3E0(0);
            break;
        }
    }
}

Bool func_8C01C28E(NJS_POINT3 *pos, NJS_POINT3 *v, Angle3 *ang, Float r)
{
    return func_8C0569F0(pos, v, ang, 0x400002, r);
}
