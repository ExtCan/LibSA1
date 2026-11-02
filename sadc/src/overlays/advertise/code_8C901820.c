/*
 * ADVERTISE 8C901820-8C901A4C: menu sound effects, background music and
 * voices (chunk 8C901820, all 16 functions).
 *
 *  func_8C901820 .. func_8C901850  sound effects 21, 22, 1, 2, 3 (8C0940DA)
 *  func_8C90185C  loads the menu sound bank mobile1.mlt (8C0939B0) and
 *                 clears the music / voice task pointers
 *  func_8C901874  stops: destroys both tasks, music stop (8C107AAA)
 *  func_8C9018A4, func_8C9018AA  wrappers of 8C107ADA / 8C107AF6
 *  func_8C9018B0  music task: from its second frame, once D_8C9610D8 is set,
 *                 plays background music w->id (8C107AC6) and ends
 *  func_8C901940  starts music id (id < 0: only stops)
 *  func_8C90198E  starts the menu music D_8C90EE54[n] (102 TITLE2, 101
 *                 TITLE, 72 OPTION, 24 CHARACTR, 103 TRIAL)
 *  func_8C90199A  voice task: the same for voice w->id (8C107BA0)
 *  func_8C9019D2  starts voice id (0 if one is pending)
 *  func_8C901A1C  screen voice n of the voice set B_8C78B450[2]
 *                 (D_8C90EE84[set][n])
 *  func_8C901A3E  the pick voice of select slot n (D_8C90EE68[n])
 */
#include "common.h"
#include "sa1/sa1.h"

/* music / voice task work (task.awp, CAlloc(1, 8)) */
typedef struct ADV_SOUNDWK {
    Sint32 id;                  /* 0x00 music or voice number */
    Sint32 timer;               /* 0x04 */
} ADV_SOUNDWK;                  /* 0x08 */

extern task *D_8C9610D0;        /* pending music task */
extern task *D_8C9610D4;        /* pending voice task */
extern Sint32 D_8C9610D8;       /* sound start allowed */
extern Sint32 D_8C90EE54[];     /* menu music per screen */
extern Sint32 D_8C90EE68[];     /* pick voice per select slot */
extern Sint32 D_8C90EE84[][9];  /* screen voices per voice set */
/* the menu's 18-byte block B_8C78B450..B_8C78B461 (array in the original):
 * [2] voice set (VMU voice files, 8C902518) */
extern Uint8 B_8C78B450[18];

/* 1ST_READ */
void func_8C0940DA(Sint32 se, Sint32 a, Sint32 b, Sint32 c);    /* play SE */
void func_8C0939B0(const char *name);                           /* load an MLT sound bank */
void func_8C107AAA(void);                                       /* music stop */
void func_8C107ADA(void);
void func_8C107AF6(void);
void func_8C107AC6(Sint32 n);                                   /* background music */
void func_8C107BA0(Sint32 n);                                   /* voice */
void func_8C107D80(void);

void func_8C901820(void)
{
    func_8C0940DA(21, 0, 0, 0);
}

void func_8C90182C(void)
{
    func_8C0940DA(22, 0, 0, 0);
}

void func_8C901838(void)
{
    func_8C0940DA(1, 0, 0, 0);
}

void func_8C901844(void)
{
    func_8C0940DA(2, 0, 0, 0);
}

void func_8C901850(void)
{
    func_8C0940DA(3, 0, 0, 0);
}

void func_8C90185C(void)
{
    func_8C0939B0("mobile1.mlt");
    D_8C9610D0 = D_8C9610D4 = NULL;
}

void func_8C901874(void)
{
    if (D_8C9610D4 != NULL) {
        DestroyTask(D_8C9610D4);
        D_8C9610D4 = NULL;
    }
    if (D_8C9610D0 != NULL) {
        DestroyTask(D_8C9610D0);
        D_8C9610D0 = NULL;
    }
    func_8C107AAA();
}

void func_8C9018A4(void)
{
    func_8C107ADA();
}

void func_8C9018AA(void)
{
    func_8C107AF6();
}

void func_8C9018B0(task *tp)
{
    ADV_SOUNDWK *w;

    w = tp->awp;
    if (w->timer++ > 0 && D_8C9610D8 != 0) {
        func_8C107AC6(w->id);
        DestroyTask(tp);
        D_8C9610D0 = NULL;
    }
}

/*
 * func_8C901940, func_8C9019D2 NON_MATCHING: instruction-identical except
 * the two stack slots (the original keeps id at @(4,R15) and the task at
 * @R15, SHC the other way round; the same as func_8C9000F4). Tried the
 * declaration order, block scope, register, an early return.
 */
#ifdef NON_MATCHING
void func_8C901940(Sint32 id)
{
    task *tp;
    ADV_SOUNDWK *w;

    func_8C901874();
    func_8C107D80();
    if (id >= 0 && D_8C9610D0 == NULL) {
        if ((D_8C9610D0 = tp = CreateElementalTask(0, 4, func_8C9018B0)) != NULL) {
            w = CAlloc(1, sizeof(ADV_SOUNDWK));
            tp->awp = w;
            w->id = id;
            w->timer = 0;
        }
    }
}
#else
ASM_STUB(void func_8C901940(Sint32 id))
#endif

void func_8C90198E(Sint32 n)
{
    func_8C901940(D_8C90EE54[n]);
}

void func_8C90199A(task *tp)
{
    ADV_SOUNDWK *w;

    w = tp->awp;
    if (w->timer++ > 0 && D_8C9610D8 != 0) {
        func_8C107BA0(w->id);
        DestroyTask(tp);
        D_8C9610D4 = NULL;
    }
}

#ifdef NON_MATCHING
Sint32 func_8C9019D2(Sint32 id)
{
    task *tp;
    ADV_SOUNDWK *w;

    if (D_8C9610D4 == NULL) {
        if ((D_8C9610D4 = tp = CreateElementalTask(0, 4, func_8C90199A)) != NULL) {
            w = CAlloc(1, sizeof(ADV_SOUNDWK));
            tp->awp = w;
            w->id = id;
            w->timer = 0;
            return 1;
        }
    }
    return 0;
}
#else
ASM_STUB(Sint32 func_8C9019D2(Sint32 id))
#endif

Sint32 func_8C901A1C(Sint32 n)
{
    return func_8C9019D2(D_8C90EE84[B_8C78B450[2]][n]);
}

Sint32 func_8C901A3E(Sint32 slot)
{
    return func_8C9019D2(D_8C90EE68[slot]);
}
