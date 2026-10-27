#include "common.h"
#include "ninja.h"
#include "task.h"
#include "sa1/sa1.h"

/*
 * 1ST_READ 8C07BAC0-8C07BE68: player death (death zones / kill planes) and
 * the drowning countdown display.
 *
 * func_8C07BAC0: exec of the death-zone task. Runs the children, then (when
 * player 1 exists and no child is running) probes every death-zone object of
 * the current stage/act (DeathZoneTable[level][act], entries enabled per
 * character by bit 1 << GetPlayerNumber()) with the vertical probe
 * func_8C02CA00 at player 1's position: when there is a polygon above the
 * player but none below, within 30 units, the player dies (func_8C07BD48).
 * Stage 0x2100 also kills below y = -432.079.
 * func_8C07BBC2: start the death sequence of player pno (twp->smode = 50).
 * func_8C07BC0E: death camera and scream of player pno.
 * func_8C07BD2A / func_8C07BD48: create the death task (elemental / child).
 * func_8C07BDA0: display of the drowning countdown task: draws 5..0 above
 * the player's head, pulsing with the second fraction.
 */

/* death zone entry (SADX DeathZone): 8-byte stride, list ends with object NULL */
typedef struct DEATHZONE {
    Uint32 flags;               /* 0x00 character bits (1 << GetPlayerNumber()) */
    NJS_OBJECT *object;         /* 0x04 */
} DEATHZONE;

/* one probe result (SADX xssunit), as in code_8C02CA00.c */
typedef struct xssunit {
    Sint32 findflag;            /* 0x00 1 = polygon found */
    Sint32 objatt;              /* 0x04 */
    Angle angx;                 /* 0x08 */
    Angle angz;                 /* 0x0C */
    Float onpos;                /* 0x10 plane height at pos */
    NJS_VECTOR normal;          /* 0x14 */
} xssunit;                      /* 0x20 */

/* probe position + two results (SADX zxsdwstr), as in code_8C02CA00.c */
typedef struct zxsdwstr {
    NJS_POINT3 pos;             /* 0x00 */
    xssunit lower;              /* 0x0C highest polygon below pos.y */
    xssunit upper;              /* 0x2C lowest polygon at/above pos.y */
} zxsdwstr;                     /* 0x4C */

/* number display request (func_8C08EE98), as UNK_NUMDISP in code_8C08ABC0.c */
typedef struct NUMDISP {
    Uint8 flags;                /* 0x00 */
    Uint8 unk1;                 /* 0x01 */
    Uint16 unk2;                /* 0x02 */
    Uint32 max;                 /* 0x04 */
    Uint32 value;               /* 0x08 */
    Float x;                    /* 0x0C */
    Float y;                    /* 0x10 */
    Float unk14;                /* 0x14 */
    Float scale;                /* 0x18 */
} NUMDISP;                      /* 0x1C */

/* any work (task.awp) of the drowning countdown task (exec func_8C07BE6E) */
typedef struct DROWNWK {
    Uint8 pno;                  /* 0x00 */
    Uint8 pad01[3];
    Uint32 timer;               /* 0x04 frames, counts up */
} DROWNWK;

SA1_ASSERT_SIZE(DEATHZONE, 0x08);
SA1_ASSERT_SIZE(xssunit, 0x20);
SA1_ASSERT_SIZE(zxsdwstr, 0x4C);
SA1_ASSERT_OFFSET(zxsdwstr, upper.onpos, 0x3C);
SA1_ASSERT_SIZE(NUMDISP, 0x1C);
SA1_ASSERT_OFFSET(NUMDISP, scale, 0x18);
SA1_ASSERT_OFFSET(DROWNWK, timer, 0x04);

extern DEATHZONE **DeathZoneTable[];    /* 8C1BE404, indexed by level, then act */
extern Sint8 tcrf_CollisionDisplayVar;  /* 8C752BDC, debug display level */
extern Sint16 D_8C19B468;
extern NUMDISP D_8C1BE8D0;

extern void func_8C098E98(task *tp);
extern void func_8C07BA40(task *tp);
extern void func_8C07BA44(task *tp);
extern void func_8C021700(void *param);                                 /* camera function */
extern void func_8C01AD2C(void (*func)(void *), Sint8 adjust, Sint8 direct); /* set event camera function */
extern void func_8C08EE98(NUMDISP *p);

void func_8C07BD48(task *ptp, Uint8 pno);
void SetInputP(Uint8 pno, Sint32 mode);

/*
 * NON_MATCHING (4 of 129 instructions): register choice only. The original
 * loads zss.upper.findflag (8C07BB66) and the loop-end zone->object
 * (8C07BB86) into R2, SHC gives R3 here. Not changed by any combination of
 * early returns / nested ifs / && chains (tested systematically), loop form
 * (for, while, do-while), declaration order, pmask type or a block-scope d.
 * Matching idioms that were needed: act/level as Uint16 locals (EXTU.W of
 * act at its use), the fabsf result in a Float local d (hoists 30.0f into
 * FR15 and gives the FMOV FR2,FR4), the zss.pos struct copy.
 */
#ifdef NON_MATCHING
void func_8C07BAC0(task *tp)
{
    Float d;
    taskwk *ptwp;
    DEATHZONE **list;
    DEATHZONE *zone;
    Uint32 pmask;
    Uint16 stage;
    Uint16 level;
    Uint16 act;
    zxsdwstr zss;

    ptwp = playertwp[0];
    func_8C098E98(tp);
    if (ptwp == NULL || tp->ctp != NULL || (tp->twp->flag & 4)) {
        return;
    }
    stage = GetStageNumber();
    act = (Uint8)stage;
    level = (stage & 0xFF00) >> 8;
    list = DeathZoneTable[level];
    if (list == NULL) {
        return;
    }
    zone = list[act];
    if (zone == NULL || zone->object == NULL) {
        return;
    }
    pmask = 1 << GetPlayerNumber();
    zss.pos = ptwp->pos;
    for (; zone->object != NULL; zone++) {
        if (tcrf_CollisionDisplayVar >= 0) {
            func_8C6215D6(zone->object);
        }
        if ((zone->flags & pmask) && func_8C02CA00(&zss, zone->object)) {
            if (zss.lower.findflag == 0 && zss.upper.findflag != 0) {
                d = _builtin_fabsf(zss.pos.y - zss.upper.onpos);
                if (d <= 30.0f) {
                    func_8C07BD48(tp, 0);
                }
            }
        }
    }
    if ((Uint16)GetStageNumber() == 0x2100 && ptwp->pos.y < -432.07905f) {
        func_8C07BD48(tp, 0);
    }
}
#else
ASM_STUB(void func_8C07BAC0(task *tp))
#endif

/* The two tests must be nested ifs: with && SHC swaps R2/R3 in the body. */
void func_8C07BBC2(Uint8 pno)
{
    taskwk *twp;

    twp = playertwp[pno];
    if ((Uint8)twp->counter.b[0] != 1) {
        if (D_8C19B468 == 0) {
            twp->flag |= 0x1000;
            twp->smode = 50;
            playerpwp[pno]->item |= 0x4000;
            CreateElementalTask(8, 0, func_8C07BA44);
        }
    }
}

void func_8C07BC0E(Uint8 pno)
{
    taskwk *twp;

    twp = playertwp[pno];
    func_8C01AD2C(func_8C021700, 0, 0);
    switch ((Uint8)twp->counter.b[1]) {
    case SA1_CHR_SONIC:
        func_8C0940DA(0x571, 0, 0, 0);
        break;
    case SA1_CHR_TAILS:
        func_8C0940DA(0x54B, 0, 0, 0);
        break;
    case SA1_CHR_KNUCKLES:
        func_8C0940DA(0x53F, 0, 0, 0);
        SetInputP(0, 24);
        break;
    case SA1_CHR_AMY:
        func_8C0940DA(0x506, 0, 0, 0);
        break;
    case SA1_CHR_BIG:
        func_8C0940DA(0x516, 0, 0, 0);
        break;
    case SA1_CHR_E102:
        func_8C0940DA(0x52B, 0, 0, 0);
        break;
    }
}

void func_8C07BD2A(Uint8 pno)
{
    task *tp;

    tp = CreateElementalTask(8, 0, func_8C07BA44);
    tp->dest = func_8C07BA40;
    func_8C07BC0E(pno);
}

void func_8C07BD48(task *ptp, Uint8 pno)
{
    task *tp;

    tp = CreateChildTask(8, func_8C07BA44, ptp);
    tp->dest = func_8C07BA40;
    func_8C07BC0E(pno);
}

/*
 * NON_MATCHING (register allocation only, one extra MOV): the original keeps
 * the divisor 60 in R7, pno in R6 and builds the playertwp index in a copy
 * (MOV R6,R5; SHLL2 R5), so ptwp lands in R5 and 5 in R6. Here 60 takes R5
 * and the index is shifted in place in R6 (ptwp R6, 5 in R5). Instruction
 * order and everything else are the same. Tried: pno as Uint8/Sint32 or no
 * pno local, statement orders, no awp/t/n locals, nested ifs, `register`
 * on t/ptwp (moves t/ptwp to R4/R5), declaration orders.
 */
#ifdef NON_MATCHING
void func_8C07BDA0(task *tp)
{
    taskwk *ptwp;
    Sint32 t;
    DROWNWK *awp;
    Sint32 pno;
    Sint32 n;
    Float f;
    NJS_POINT3 pos;
    NJS_POINT2 scr;

    awp = tp->awp;
    t = awp->timer - 420;
    pno = awp->pno;
    f = (Float)(t % 60) * 0.016666666f;
    ptwp = playertwp[pno];
    n = t / 60;
    if (n < 5 && n >= 0) {
        D_8C1BE8D0.value = 5 - n;
        if (f >= 0.29999999f) {
            f = 0.29999999f;
        }
        D_8C1BE8D0.scale = (f + 1.0f) * 2.0f;
        pos = ptwp->pos;
        njProjectScreen(NULL, &pos, &scr);
        scr.y -= 96.0f;
        D_8C1BE8D0.x = scr.x;
        D_8C1BE8D0.y = scr.y;
        func_8C63FAE8();
        njColorBlendingMode(0, 8);
        njColorBlendingMode(1, 6);
        func_8C08EE98(&D_8C1BE8D0);
        func_8C63FB18();
    }
}
#else
ASM_STUB(void func_8C07BDA0(task *tp))
#endif

ASM_STUB(void SetInputP(Uint8 pno, Sint32 mode))
