/*
 * ADVERTISE 8C901460-8C9017C2: the menu's master task and the screen
 * sequence (chunk 8C901460, functions 1-10 and 12 of 12; func_8C901792 is
 * not executed by the character select and stays assembly).
 *
 * A sequence (D_8C9412F4[submode], entries of 16 bytes ending with type 0)
 * lists the screen objects to create: {type, back, next, next2}. The master
 * task creates them through a helper task 130 frames after the menu init
 * (func_8C90148E / func_8C901460 -> func_8C9014C8, create table
 * D_8C941300[type]) and deletes them when it is destroyed (func_8C901502,
 * delete table D_8C941338[type]).
 *
 *  func_8C901460  helper exec: count down, create the sequence, destroy itself
 *  func_8C90148E  creates the helper (130 frames)
 *  func_8C9014C8  calls every entry's create function with {back, next, next2}
 *  func_8C901502  calls every entry's delete function
 *  func_8C90152C  master init: work, sequence of the submode, first screen
 *  func_8C901588  master exec: 10-frame fade before a requested screen
 *                 (func_8C901776); screen 14 waits for "ready", screen 15
 *                 hands the result to the main loop (8C9001DA) and sets up
 *                 the game (func_8C901280)
 *  func_8C9016F4  master dest
 *  func_8C90170E  creates the master task (D_8C9610CC; hangs if one exists)
 *  func_8C90175A  destroys it
 *  func_8C901776  switches the master to screen n (remembers the previous)
 *  func_8C90179E  requests screen n after a fade (8C901874)
 */
#include "common.h"
#include "sa1/sa1.h"

/* screen sequence entry (D_8C9412F4[submode] -> array ending with type 0) */
typedef struct ADV_SCRARGS {
    Sint32 back;                /* 0x00 screen for B */
    Sint32 next;                /* 0x04 screen after a pick */
    Sint32 next2;               /* 0x08 */
} ADV_SCRARGS;                  /* 0x0C */

typedef struct ADV_SEQ {
    Sint32 type;                /* 0x00 screen object type 1-13, 0 ends */
    ADV_SCRARGS args;           /* 0x04 */
} ADV_SEQ;                      /* 0x10 */

/* master task work (task.awp, CAlloc(1, 0x24)) */
typedef struct ADV_MASTERWK {
    ADV_SEQ *seq;               /* 0x00 */
    Sint32 mode;                /* 0x04 current screen */
    Sint32 prev;                /* 0x08 previous screen */
    Sint32 screen;              /* 0x0C first screen of the sequence (6 = title) */
    Sint32 next;                /* 0x10 result chosen by the screens */
    Sint32 result;              /* 0x14 exec result for the main loop */
    Sint32 req;                 /* 0x18 screen requested after the fade */
    Sint16 fade;                /* 0x1C */
    Sint8 fadereq;              /* 0x1E */
    Uint8 ready;                /* 0x1F */
    Sint8 b20;                  /* 0x20 */
    Sint8 b21;                  /* 0x21 */
} ADV_MASTERWK;                 /* 0x24 */

/* helper task work (task.awp, CAlloc(1, 8)) */
typedef struct ADV_DELAYWK {
    ADV_SEQ *seq;               /* 0x00 */
    Sint32 timer;               /* 0x04 */
} ADV_DELAYWK;                  /* 0x08 */

#define MASTERWK ((ADV_MASTERWK *)D_8C9610CC->awp)

extern task *D_8C9610CC;                        /* the master task */
extern ADV_SEQ *D_8C9412F4[];                   /* sequence per submode */
extern task *(*D_8C941300[])(ADV_SCRARGS *args); /* screen object create, by type */
extern void (*D_8C941338[])(void);              /* screen object delete, by type */

/* 1ST_READ */
void func_8C04F8EC(Sint32 a);
task *func_8C0A6660(void);
void func_8C0A6696(void);

/* ADVERTISE, other chunks */
void func_8C901280(void);
void func_8C900F20(void);
void func_8C901874(void);
void func_8C902326(void);
void func_8C90233C(void);

void func_8C9014C8(ADV_SEQ *seq);

void func_8C901460(task *tp)
{
    ADV_DELAYWK *w;

    w = tp->awp;
    if (--w->timer <= 0) {
        func_8C9014C8(w->seq);
        DestroyTask(tp);
    }
}

void func_8C90148E(ADV_SEQ *seq)
{
    task *tp;
    ADV_DELAYWK *w;

    if ((tp = CreateElementalTask(0, 3, func_8C901460)) != NULL) {
        w = CAlloc(1, sizeof(ADV_DELAYWK));
        tp->awp = w;
        w->seq = seq;
        w->timer = 130;
        tp->exec = func_8C901460;
    }
}

void func_8C9014C8(ADV_SEQ *seq)
{
    ADV_SCRARGS args;

    while (seq->type != 0) {
        args.back = seq->args.back;
        args.next = seq->args.next;
        args.next2 = seq->args.next2;
        D_8C941300[seq->type](&args);
        seq++;
    }
}

void func_8C901502(ADV_SEQ *seq)
{
    while (seq->type != 0) {
        D_8C941338[seq->type]();
        seq++;
    }
}

void func_8C90152C(task *tp, Sint32 *submode)
{
    ADV_MASTERWK *w;

    w = tp->awp;
    func_8C902326();
    func_8C900F20();
    w->prev = 0;
    w->next = -1;
    w->result = 0;
    w->ready = 0;
    w->b20 = 1;
    w->b21 = 1;
    w->seq = D_8C9412F4[*submode];
    w->mode = w->seq->type;
    w->screen = w->seq->type;
    func_8C90148E(w->seq);
    w->fadereq = 0;
    w->fade = 0;
    func_8C0A6660();
}

void func_8C901776(Sint32 mode);

/*
 * NON_MATCHING: instruction-identical except 8 register numbers after the
 * inner switch (the original loads 8C90233C / 8C901280 into R2 / R1 and uses
 * R2 / R1 for the two stores, SHC R1 / R2: scratch-register rotation). Tried
 * default-first switches, an inverted case 14 test.
 */
#ifdef NON_MATCHING
void func_8C901588(task *tp)
{
    ADV_MASTERWK *w;

    w = tp->awp;
    if (w->fadereq != 0) {
        w->fade = 10;
        w->fadereq = 0;
    }
    if (w->fade != 0) {
        w->fade--;
        if (w->fade <= 0) {
            if (w->req == 0) {
                func_8C901776(14);
            } else {
                func_8C901776(w->req);
            }
        }
    }
    switch (w->mode) {
    case 14:
        if (w->ready == 1) {
            w->mode = 15;
        }
        break;
    case 15:
        switch (w->next) {
        case 1:
            MASTERWK->result = 1;
            break;
        case 100:
            MASTERWK->result = 100;
            break;
        case 101:
            MASTERWK->result = 101;
            break;
        case 102:
            MASTERWK->result = 102;
            break;
        case 103:
            MASTERWK->result = 103;
            break;
        case 104:
            MASTERWK->result = 104;
            break;
        case 106:
            MASTERWK->result = 106;
            break;
        case 107:
            MASTERWK->result = 107;
            func_8C04F8EC(0);
            break;
        default:
            MASTERWK->result = -1;
            break;
        }
        func_8C90233C();
        func_8C901280();
        w->mode = 16;
        break;
    default:
        w->ready = 0;
        break;
    }
}
#else
ASM_STUB(void func_8C901588(task *tp))
#endif

void func_8C9016F4(task *tp)
{
    ADV_MASTERWK *w;

    w = tp->awp;
    func_8C90233C();
    func_8C901502(w->seq);
    func_8C0A6696();
}

task *func_8C90170E(Sint32 *submode)
{
    task *tp;
    ADV_MASTERWK *w;

    if (D_8C9610CC != NULL) {
        for (;;) {
        }
    }
    if ((D_8C9610CC = tp = CreateElementalTask(0, 3, func_8C901588)) != NULL) {
        w = CAlloc(1, sizeof(ADV_MASTERWK));
        tp->awp = w;
        func_8C90152C(tp, submode);
        tp->exec = func_8C901588;
        tp->dest = func_8C9016F4;
    }
    return tp;
}

void func_8C90175A(void)
{
    if (D_8C9610CC != NULL) {
        DestroyTask(D_8C9610CC);
        D_8C9610CC = NULL;
    }
}

void func_8C901776(Sint32 mode)
{
    if (MASTERWK->mode != mode) {
        MASTERWK->prev = MASTERWK->mode;
        MASTERWK->mode = mode;
    }
}

void func_8C90179E(Sint32 req)
{
    ADV_MASTERWK *w;

    w = MASTERWK;
    func_8C901874();
    w->fadereq = 1;
    w->req = req;
}
