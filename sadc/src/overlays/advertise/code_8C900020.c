/*
 * ADVERTISE 8C900020-8C9001F4: the menu overlay's entry functions (chunk
 * 8C900020, functions 1-5). The overlay's entry table at 8C900000 holds
 * {8C9000F4 init(&submode), 8C9001C0 cleanup, 8C9001DA exec}; the main loop
 * 8C053B60 (cases 10 / 11) calls them.
 *
 *  func_8C900020  menu system init: music stop, mobile1.mlt (8C90185C), frame
 *                 interval 1 (60 Hz, 8C051760), heap and task init, 2D colour
 *                 state, clear colour 0, 2D library init (8C07DEC0)
 *  func_8C900088  one frame: pad update, task exec / display, soft reset
 *                 (A+B+X+Y+Start) -> 108 from the title screen (screen 6),
 *                 else -2; 0 = keep running
 *  func_8C9000F4  init(&submode): submode 1 / 2 enter the master task in
 *                 that mode, anything else mode 0 (and clears B_8C78B45B)
 *  func_8C9001C0  cleanup: master task, all tasks, memory-card end
 *  func_8C9001DA  exec: the frame result, or the master's result
 */
#include "common.h"
#include "sa1/sa1.h"

/* master task work (task.awp, CAlloc(1, 0x24) by func_8C90170E; full
 * description in code_8C901460.c) */
typedef struct ADV_MASTERWK {
    void *seq;                  /* 0x00 screen sequence */
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

extern task *D_8C9610CC;        /* the master task */
extern Sint32 D_8C1BDFDC;
extern Uint8 B_8C78B450[18];    /* the menu block; [0xB] previous select slot */

/* 1ST_READ */
void func_8C107AAA(void);                       /* music stop */
void func_8C051760(Sint32 a, Sint32 b);         /* frame interval */
void func_8C09844A(void);                       /* task system init */
void func_8C60526A(Uint32 a, Uint32 b, Uint32 c);
void func_8C043998(NJS_COLOR *col);             /* clear colour */
void func_8C099080(void);
void func_8C6087F4(void);
void func_8C07DEC0(void);                       /* menu 2D library init */
void func_8C099A00(void);
void func_8C0509A0(void);
void func_8C0988A4(void);                       /* run all task levels */
void func_8C605480(Sint32 a);
void func_8C0988C4(void);                       /* display all task levels */
Sint32 func_8C050A06(Sint32 a);                 /* soft-reset flag */
void func_8C098900(void);                       /* destroy all tasks */
void func_8C099A50(void);

/* ADVERTISE, other chunks */
void func_8C90185C(void);
void func_8C902480(void);
task *func_8C90170E(Sint32 *mode);
void func_8C90175A(void);
void func_8C9024C6(void);

void func_8C900020(void)
{
    NJS_COLOR col;

    func_8C107AAA();
    func_8C90185C();
    func_8C051760(1, 1);
    func_8C098A10();
    func_8C09844A();
    func_8C60526A(0xFF000000, 0xFF000000, 0xFF000000);
    col.argb.a = 0;
    col.argb.r = 0;
    col.argb.g = 0;
    col.argb.b = 0;
    func_8C043998(&col);
    func_8C099080();
    func_8C6087F4();
    func_8C07DEC0();
    func_8C099A00();
}

Sint32 func_8C900088(void)
{
    Sint32 r;

    func_8C050BE4();
    func_8C0509A0();
    func_8C0988A4();
    func_8C605480(1);
    func_8C63FAE8();
    func_8C0988C4();
    D_8C1BDFDC = 0;
    if (func_8C050A06(0)) {
        r = (((ADV_MASTERWK *)D_8C9610CC->awp)->screen == 6) ? 108 : -2;
        func_8C098900();
        return r;
    }
    func_8C099A50();
    return 0;
}

#ifdef NON_MATCHING
/* NON_MATCHING: instruction-identical except the frame layout (the original
 * keeps the spilled submode pointer at @R15 and mode at @(4,R15); SHC puts
 * them the other way round: tried local copies, block scope, arrays, a
 * struct, a second parameter) */
Sint32 func_8C9000F4(Sint32 *submode)
{
    Sint32 mode;

    func_8C900020();
    func_8C902480();
    D_8C9610CC = NULL;
    switch (*submode) {
    case 1:
        mode = 1;
        break;
    case 2:
        mode = 2;
        break;
    default:
        B_8C78B450[11] = 0;
        mode = 0;
        break;
    }
    func_8C90170E(&mode);
    return 0;
}
#else
ASM_STUB(Sint32 func_8C9000F4(Sint32 *submode))
#endif

Sint32 func_8C9001C0(void)
{
    func_8C90175A();
    func_8C098900();
    func_8C9024C6();
    return 0;
}

Sint32 func_8C9001DA(void)
{
    Sint32 r;

    if ((r = func_8C900088()) == 0) {
        r = ((ADV_MASTERWK *)D_8C9610CC->awp)->result;
    }
    return r;
}
