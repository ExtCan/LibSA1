/*
 * STG01 0C906082: footprints, object O_ASIATO (chunk 0C905CA0-0C906820,
 * functions 5-8 of 8). Functions 1-4 of the chunk are the print helpers
 * (decompiled in code_0C905CA0.c), ASM_STUBs at the end of this file:
 *   func_0C905CA0  disp: draws the print ring (also called by the exec)
 *   func_0C905D9C  foot positions/angle of player pno (pwp->rightfoot_pos /
 *                  leftfoot_pos with y = -(height of the CCL centre), the
 *                  player angle with y = 0x8000 - ang.y)
 *   func_0C905FE6  drops one print of obj at pos/ang when the ground probe
 *                  under it finds attribute 0x100000 (via func_0C905F40)
 *
 *  objexec_O_ASIATO  init: player number = scl.x; MAllocs the print ring
 *                    (ASIATO_WORK, 100 prints), remembers the stage number
 *                    in counter.w[1], picks the print set by character
 *                    (mode: Big 1, E-102 2, others 0) and installs exec
 *                    (from the set), disp func_0C905CA0, dest func_0C9061A0.
 *  func_0C9061A0     dest: frees the print ring.
 *  func_0C9061B0     debug exec (installed when func_8C042DC4 returns
 *                    nonzero, which it never does): "PlayerID:" + number.
 *  func_0C9061DA     exec: frees itself when the stage changed; draws the
 *                    prints; on the ground (ptwp->flag & 3):
 *                    - player mode 31 with an alternate print set (E-102's
 *                      tire trail): a print of obj[2]/obj[3] whenever a foot
 *                      is below info->height on attribute 0x100000 and both
 *                      the foot and the player moved more than info->dist
 *                      since the last one; when the gap is still short
 *                      (< 2 * dist and < 16) an interpolated print halfway
 *                      to the newest print first;
 *                    - otherwise a print of obj[0]/obj[1] when a foot that
 *                      was raised (timer.w[0] bit 1 left / 2 right) comes
 *                      down again, at least info->dist from the newest one;
 *                    timer.w[0] = 4 in the air. The feet of every frame are
 *                    kept in work->lfoot/rfoot.
 *
 * Status: func_0C9061A0, func_0C9061B0 and func_0C9061DA match.
 *  - objexec_O_ASIATO (NON_MATCHING): one register choice, the address of
 *    GetStageNumber is loaded into R2 instead of R3 (2 instructions). Tried:
 *    else/early-return/combined-condition structures, declaration orders,
 *    an int-returning (cast and unprototyped) declaration, casts of the
 *    stored values, extra/merged zero stores, a result temporary,
 *    ablations of the later code (the choice only depends on the stores
 *    around the call: without the wtimer store it becomes R1), and a C
 *    model of the preceding func_0C905F40/func_0C905FE6 in front of it
 *    (reproduces the original pool dump after its first BRA, same R2).
 *    The ASM_STUBs are at the end of the file so that nothing but this
 *    function's code precedes func_0C9061A0.
 * Code generation notes for func_0C9061DA:
 *  - GetStageNumber() is compared without EXTU.W: the original saw an int
 *    result (STAGE_NUMBER below; same value, the stage number fits in 16
 *    bits).
 *  - dist lives on the stack (stored after each call, reloaded once) like
 *    an address-taken variable; "(void)&dist;" reproduces that, a plain or
 *    volatile local gets FR14 or is reloaded at every use.
 *  - the midpoint vectors are walked with Float pointers (FMOV.S @Rn+
 *    loads); the halved angle difference is read and written as the low
 *    Sint16 of each Angle through a pointer with a Float temporary (MOV.W,
 *    FR4). The right-hand scale starts with "*d *= 0.5f; d++;" (with
 *    "*d++ *= 0.5f" the following add swaps R4/R5), the dist tests are one
 *    condition (nested ifs load njDistanceP2P into R3), "i = i - 100"
 *    gives ADD #-100 ("i -= 100" reuses the 100 in R3), and work is
 *    assigned before info.
 */
#include "common.h"
#include "sa1/sa1.h"
#include "sa1/protos.h"

/* One footprint (ring of 100 in ASIATO_WORK, stride 0x1C: func_0C905CA0
 * MUL.L #28; written by func_0C905F40, drawn by func_0C905CA0 with
 * njTranslateV(pos), njRotateZ(ang.z), njRotateX(ang.x),
 * njRotateY(0x8000 + ang.y)). */
typedef struct {
    NJS_POINT3 pos;             /* 0x00 */
    Angle3 ang;                 /* 0x0C */
    NJS_MODEL *model;           /* 0x18 obj->model of the print set */
} ASIATO_PRINT;                 /* 0x1C */

/* Print ring (twp->value.ptr, MAlloc'd by objexec_O_ASIATO). */
typedef struct {
    ASIATO_PRINT print[100];    /* 0x000 */
    Sint32 start;               /* 0xAF0 index of the oldest print */
    Sint32 num;                 /* 0xAF4 number of prints (<= 100) */
    NJS_POINT3 lfoot;           /* 0xAF8 left foot of the last frame */
    NJS_POINT3 rfoot;           /* 0xB04 right foot of the last frame */
    NJS_POINT3 pos;             /* 0xB10 player position at the last print */
} ASIATO_WORK;                  /* 0xB1C */

/* Print set per character (D_0CAB0530[3], stride 0x28: Sonic and the
 * others, Big, E-102). */
typedef struct {
    NJS_OBJECT *obj[4];         /* 0x00 left, right print; 2/3: player mode 31 prints (E-102 tire trail), NULL for the others */
    TaskFuncPtr exec;           /* 0x10 func_0C9061DA */
    Float dist;                 /* 0x14 minimum distance between prints */
    Float height;               /* 0x18 a foot below this height is down */
    NJS_POINT3 ofs;             /* 0x1C passed to func_0C905D9C */
} ASIATO_INFO;                  /* 0x28 */

SA1_ASSERT_SIZE(ASIATO_PRINT, 0x1C);
SA1_ASSERT_OFFSET(ASIATO_PRINT, ang, 0x0C);
SA1_ASSERT_OFFSET(ASIATO_PRINT, model, 0x18);
SA1_ASSERT_SIZE(ASIATO_WORK, 0xB1C);
SA1_ASSERT_OFFSET(ASIATO_WORK, start, 0xAF0);
SA1_ASSERT_OFFSET(ASIATO_WORK, num, 0xAF4);
SA1_ASSERT_OFFSET(ASIATO_WORK, lfoot, 0xAF8);
SA1_ASSERT_OFFSET(ASIATO_WORK, rfoot, 0xB04);
SA1_ASSERT_OFFSET(ASIATO_WORK, pos, 0xB10);
SA1_ASSERT_SIZE(ASIATO_INFO, 0x28);
SA1_ASSERT_OFFSET(ASIATO_INFO, exec, 0x10);
SA1_ASSERT_OFFSET(ASIATO_INFO, dist, 0x14);
SA1_ASSERT_OFFSET(ASIATO_INFO, height, 0x18);
SA1_ASSERT_OFFSET(ASIATO_INFO, ofs, 0x1C);

extern ASIATO_INFO D_0CAB0530[];
extern const char D_0C910BBC[];         /* "PlayerID:" */

/* GetStageNumber() as an int (see above); PC: the canonical call. */
#if defined(TARGET_PC)
#define STAGE_NUMBER() GetStageNumber()
#else
#define STAGE_NUMBER() ((Sint32 (*)(void))GetStageNumber)()
#endif

#define ASIATO_WK(twp) ((ASIATO_WORK *)(twp)->value.ptr)

void func_0C9061A0(task *tp);
void func_0C9061B0(task *tp);
void func_0C9061DA(task *tp);

void objexec_O_ASIATO(task *tp);
void func_0C905CA0(task *tp);
void func_0C905D9C(Sint32 pno, Angle3 *ang, NJS_POINT3 *rfoot, NJS_POINT3 *lfoot, NJS_POINT3 *ofs);
void func_0C905FE6(NJS_POINT3 *pos, Angle3 *ang, NJS_OBJECT *obj, taskwk *twp);

/*
 * NON_MATCHING (see the status above). The matching build keeps the original
 * assembly (ASM_STUB at the end of the file) and compiles this body as
 * tu_objexec_O_ASIATO, a translation-unit context model that is never
 * spliced: SHC continues its scratch-register rotation from the previous
 * function, and func_0C9061A0 only matches after this code.
 */
#if !defined(NON_MATCHING)
#define objexec_O_ASIATO tu_objexec_O_ASIATO
void objexec_O_ASIATO(task *tp);
#endif
void objexec_O_ASIATO(task *tp)
{
    taskwk *twp;
    taskwk *ptwp;

    twp = tp->twp;
    if (func_8C042DC4(tp)) {
        tp->exec = func_0C9061B0;
    } else {
        twp->smode = SA1_FTOI(twp->scl.x);
        if (twp->smode < 8) {
            ptwp = playertwp[twp->smode];
            if (ptwp) {
                twp->wtimer = 0;
                twp->counter.w[1] = GetStageNumber();
                twp->timer.w[0] = 0;
                twp->value.ptr = MAlloc(sizeof(ASIATO_WORK));
                ASIATO_WK(twp)->start = 0;
                ASIATO_WK(twp)->num = 0;
                ASIATO_WK(twp)->rfoot = ptwp->pos;
                ASIATO_WK(twp)->lfoot = ptwp->pos;
                ASIATO_WK(twp)->pos = ptwp->pos;
                /* clears the fields set just above (as the original does) */
                memset(twp->value.ptr, 0, sizeof(ASIATO_WORK));
                switch (func_8C07CE40(twp->smode)) {
                default:
                    twp->mode = 0;
                    break;
                case 7:                 /* Big */
                    twp->mode = 1;
                    break;
                case 6:                 /* E-102 */
                    twp->mode = 2;
                    break;
                }
                tp->disp = func_0C905CA0;
                tp->exec = D_0CAB0530[twp->mode].exec;
                tp->dest = func_0C9061A0;
            }
        }
    }
}
#if !defined(NON_MATCHING)
#undef objexec_O_ASIATO
#endif

void func_0C9061A0(task *tp)
{
    taskwk *twp;

    twp = tp->twp;
    MFree(twp->value.ptr);
}

void func_0C9061B0(task *tp)
{
    taskwk *twp;

    twp = tp->twp;
    njPrintC(0x00120013, D_0C910BBC);
    func_8C640A80(0x001D0013, (Sint8)SA1_FTOI(twp->scl.x), 3);
}

void func_0C9061DA(task *tp)
{
    NJS_POINT3 rfoot;
    NJS_POINT3 lfoot;
    Angle3 ang;
    NJS_POINT3 mid;
    Angle3 angm;
    Sint32 pno;
    Float dist;
    ASIATO_WORK *work;
    taskwk *twp;
    ASIATO_INFO *info;
    playerwk *pwp;
    taskwk *ptwp;
    ASIATO_PRINT *prev;
    Angle3 *pang;
    Sint32 i;
    Sint32 placed;
    Float *d;
    Float *a;
    Float *b;
    Sint16 *s;
    Float f;

    twp = tp->twp;
    pno = twp->smode;
    pwp = GetPlayerWorkPtr(pno);
    work = twp->value.ptr;
    info = &D_0CAB0530[twp->mode];
    if ((Uint16)twp->counter.w[1] != STAGE_NUMBER()) {
        FreeTask(tp);
        return;
    }
    ptwp = playertwp[pno];
    if (ptwp == NULL) {
        return;
    }
    if (pwp == NULL) {
        return;
    }
    func_0C905CA0(tp);
    /* newest print */
    prev = NULL;
    pang = NULL;
    if (work->num > 0) {
        i = work->start + work->num - 1;
        if (i >= 100) {
            i = i - 100;
        }
        if (i < 0) {
            i = 99;
        }
        prev = &work->print[i];
        pang = &work->print[i].ang;
    }
    if (ptwp->flag & 3) {
        func_0C905D9C(pno, &ang, &rfoot, &lfoot, &info->ofs);
        if (ptwp->mode == 31 && info->obj[2] && info->obj[3]) {
            dist = 0.0f;
            (void)&dist;
            placed = 0;
            if (pwp->leftfoot_pos.y < info->height && (pwp->attr & 0x100000)
             && (dist = njDistanceP2P(&work->lfoot, &lfoot)) > info->dist
             && njDistanceP2P(&work->pos, &ptwp->pos) > info->dist) {
                if (prev && info->dist * 2.0f > dist && 16.0f > dist) {
                    /* mid = (last foot - foot) / 2 + foot */
                    d = &mid.x;
                    a = &work->lfoot.x;
                    b = &lfoot.x;
                    *d++ = *a++ - *b++;
                    *d++ = *a++ - *b++;
                    *d = *a - *b;
                    d = &mid.x;
                    *d++ *= 0.5f;
                    *d++ *= 0.5f;
                    *d *= 0.5f;
                    d = &mid.x;
                    b = &lfoot.x;
                    *d++ += *b++;
                    *d++ += *b++;
                    *d += *b;
                    /* angm = (Sint16)(ang - newest) / 2 + newest */
                    angm.x = ang.x - pang->x;
                    angm.y = ang.y - pang->y;
                    angm.z = ang.z - pang->z;
                    s = (Sint16 *)&angm.x;
                    f = *s;
                    *s = SA1_FTOI(f * 0.5f);
                    s = (Sint16 *)&angm.y;
                    f = *s;
                    *s = SA1_FTOI(f * 0.5f);
                    s = (Sint16 *)&angm.z;
                    f = *s;
                    *s = SA1_FTOI(f * 0.5f);
                    angm.x += pang->x;
                    angm.y += pang->y;
                    angm.z += pang->z;
                    func_0C905FE6(&mid, &angm, info->obj[2], twp);
                }
                func_0C905FE6(&lfoot, &ang, info->obj[2], twp);
                placed = 1;
            }
            if (pwp->rightfoot_pos.y < info->height && (pwp->attr & 0x100000)
             && (dist = njDistanceP2P(&work->rfoot, &rfoot)) > info->dist
             && njDistanceP2P(&work->pos, &ptwp->pos) > info->dist) {
                if (prev && info->dist * 2.0f > dist && 16.0f > dist) {
                    d = &mid.x;
                    a = &work->rfoot.x;
                    b = &rfoot.x;
                    *d++ = *a++ - *b++;
                    *d++ = *a++ - *b++;
                    *d = *a - *b;
                    d = &mid.x;
                    *d *= 0.5f;
                    d++;
                    *d++ *= 0.5f;
                    *d *= 0.5f;
                    d = &mid.x;
                    b = &rfoot.x;
                    *d++ += *b++;
                    *d++ += *b++;
                    *d += *b;
                    angm.x = ang.x - pang->x;
                    angm.y = ang.y - pang->y;
                    angm.z = ang.z - pang->z;
                    s = (Sint16 *)&angm.x;
                    f = *s;
                    *s = SA1_FTOI(f * 0.5f);
                    s = (Sint16 *)&angm.y;
                    f = *s;
                    *s = SA1_FTOI(f * 0.5f);
                    s = (Sint16 *)&angm.z;
                    f = *s;
                    *s = SA1_FTOI(f * 0.5f);
                    angm.x += pang->x;
                    angm.y += pang->y;
                    angm.z += pang->z;
                    func_0C905FE6(&mid, &angm, info->obj[3], twp);
                }
                func_0C905FE6(&rfoot, &ang, info->obj[3], twp);
                placed = 1;
            }
            if (placed) {
                work->pos = ptwp->pos;
            }
            twp->timer.w[0] &= ~3;
        } else {
            if (info->height > pwp->leftfoot_pos.y && work->lfoot.y > pwp->leftfoot_pos.y
             && pwp->rightfoot_pos.y >= pwp->leftfoot_pos.y) {
                /* left foot raised */
                twp->timer.w[0] |= 1;
            } else if (pwp->leftfoot_pos.y > work->lfoot.y) {
                if ((pwp->attr & 0x100000) && (twp->timer.w[0] & 1) && !(twp->timer.w[0] & 2)) {
                    if (prev == NULL || njDistanceP2P(&prev->pos, &lfoot) > info->dist) {
                        func_0C905FE6(&lfoot, &ang, info->obj[0], twp);
                        work->pos = ptwp->pos;
                    }
                }
                twp->timer.w[0] &= ~1;
            }
            if (info->height > pwp->rightfoot_pos.y && work->rfoot.y > pwp->rightfoot_pos.y
             && pwp->leftfoot_pos.y >= pwp->rightfoot_pos.y) {
                /* right foot raised */
                twp->timer.w[0] |= 2;
            } else if (pwp->rightfoot_pos.y > work->rfoot.y) {
                if ((pwp->attr & 0x100000) && (twp->timer.w[0] & 2) && !(twp->timer.w[0] & 1)) {
                    if (prev == NULL || njDistanceP2P(&prev->pos, &rfoot) > info->dist) {
                        func_0C905FE6(&rfoot, &ang, info->obj[1], twp);
                        work->pos = ptwp->pos;
                    }
                }
                twp->timer.w[0] &= ~2;
            }
        }
        twp->timer.w[0] &= ~4;
    } else {
        twp->timer.w[0] = 4;
    }
    work->rfoot = pwp->rightfoot_pos;
    work->lfoot = pwp->leftfoot_pos;
}

/* Functions 1-4 of the chunk (code_0C905CA0.c) and objexec_O_ASIATO stay
 * assembly in this unit. */
#ifndef NON_MATCHING
ASM_STUB(void objexec_O_ASIATO(task *tp))
#endif
ASM_STUB(void func_0C905CA0(task *tp))
ASM_STUB(void func_0C905D9C(Sint32 pno, Angle3 *ang, NJS_POINT3 *rfoot, NJS_POINT3 *lfoot, NJS_POINT3 *ofs))
ASM_STUB(void func_0C905FE6(NJS_POINT3 *pos, Angle3 *ang, NJS_OBJECT *obj, taskwk *twp))
