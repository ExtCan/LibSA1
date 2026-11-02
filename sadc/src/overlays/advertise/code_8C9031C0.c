/*
 * ADVERTISE 8C9031C0-8C903F1C: the Character Select screen object (screen
 * type 1: create 8C903EC0, exec 8C903D40, disp 8C903E40; chunk 8C9031C0,
 * functions 1, 2, 4-17 of 17; "func_8C90320A" is the dead epilogue of
 * func_8C9031CE, see below).
 *
 *  func_8C9031C0  Trial mode? (B_8C78B450[10])
 *  func_8C9031CE  can slot n be picked (Trial: the per-character Trial
 *                 lists, else the unlock flags of func_8C90117C)
 *  func_8C903210  back to idle (procedure 0, no slide)
 *  func_8C90321E  screen enter: PVMs, the 3D task, music (24, Trial 103),
 *                 voice 4; a slot that cannot be picked falls back to 0
 *  func_8C90327E  the selected slot -> B_8C78B450[11] / [13]
 *  func_8C9032E0  the procedure: 0 idle (LEFT / RIGHT / A / START / B),
 *                 1-2 ring slide, 3 Trial pick wait, 4-7 dialogs 6-9, 8 the
 *                 dialog's choice, 9 pick wait (300 frames), 10 leaving
 *  func_8C9036E0  title, title bar, circle, tab and bottom bar
 *  func_8C903808  full name of the selected character
 *  func_8C9038E0  the sphere ring and the labels
 *  func_8C903AE0  the bottom bar contents (File, Clear %)
 *  func_8C903C3C  init (sequence arguments, depth 60000)
 *  func_8C903D40  exec: fade in when the master shows screen 1, fade out
 *                 when it leaves it
 *  func_8C903E40  disp
 *  func_8C903EC0  create (D_8C9613A4)
 *  func_8C903F00  destroy, func_8C903F12  set "next"
 *
 * SPLIT NOTE: "func_8C90320A" (LDS.L / RTS / NOP, no callers) is SHC's dead
 * epilogue after func_8C9031CE's tail call (as func_8C901212 after
 * func_8C901200): func_8C9031CE compiles to 8C9031CE-8C903210.
 */
#include "common.h"
#include "sa1/sa1.h"

/* screen sequence arguments (func_8C9014C8) */
typedef struct ADV_SCRARGS {
    Sint32 back;                /* 0x00 */
    Sint32 next;                /* 0x04 */
    Sint32 next2;               /* 0x08 */
} ADV_SCRARGS;                  /* 0x0C */

/* master task work (task.awp; func_8C90170E) */
typedef struct ADV_MASTERWK {
    void *seq;                  /* 0x00 */
    Sint32 mode;                /* 0x04 current screen */
    Sint32 prev;                /* 0x08 previous screen */
    Sint32 screen;              /* 0x0C */
    Sint32 next;                /* 0x10 result chosen by the screens */
    Sint32 result;              /* 0x14 */
    Sint32 req;                 /* 0x18 */
    Sint16 fade;                /* 0x1C */
    Sint8 fadereq;              /* 0x1E */
    Uint8 ready;                /* 0x1F */
    Sint8 b20;                  /* 0x20 */
    Sint8 b21;                  /* 0x21 */
} ADV_MASTERWK;                 /* 0x24 */

#define MASTERWK ((ADV_MASTERWK *)D_8C9610CC->awp)

/* character select work (task.awp, CAlloc(1, 0x38) by func_8C903EC0) */
typedef struct ADV_SELWK {
    Sint32 state;               /* 0x00 0 hidden, 1 fading in, 2 shown, 3 fading out */
    Sint32 back;                /* 0x04 screen for B */
    Sint32 next;                /* 0x08 */
    Float z;                    /* 0x0C 2D depth base (60000) */
    Float fade;                 /* 0x10 */
    Uint32 color;               /* 0x14 fade colour, 0 = not drawn */
    Sint32 ring;                /* 0x18 highlight phase */
    Sint32 proc;                /* 0x1C procedure */
    Sint32 slot;                /* 0x20 selected slot */
    Sint8 b24;                  /* 0x24 */
    Uint8 pad25[3];
    Float slide;                /* 0x28 ring slide -1..1 */
    Sint16 timer;               /* 0x2C */
    Uint8 pad2E[2];
    Sint32 choice;              /* 0x30 dialog choice: 0 Game, 1 Instruction, 2 Ending, 3 Cancel */
    Sint8 b34;                  /* 0x34 */
    Uint8 pad35[3];
} ADV_SELWK;                    /* 0x38 */

extern task *D_8C9610CC;        /* the master task */
extern Sint32 D_8C9610D8;       /* sound start allowed */
extern task *D_8C9613A4;        /* this screen object */
extern Uint32 B_8C754E5C;       /* buttons pressed this frame */
extern Float D_8C9102E8[];      /* label width per slot */
extern NJS_TEXLIST D_8C941508;  /* ava_chsel */
extern NJS_TEXLIST D_8C9415A0;  /* ava_suuji */
extern NJS_TEXLIST D_8C941AD4;  /* ava_fscmn */
/* the menu's 18-byte block B_8C78B450..B_8C78B461 (cleared by 8C058646);
 * this file reaches it through a struct pointer (base + displacement code) */
typedef struct ADV_MENUFLAGS {
    Uint8 all_open;             /* 0x00 every character open */
    Uint8 trial_sub;            /* 0x01 B_8C78B451 */
    Uint8 voice_set;            /* 0x02 */
    Uint8 b03;                  /* 0x03 */
    Uint8 pad04[5];
    Uint8 file;                 /* 0x09 file number */
    Uint8 trial;                /* 0x0A Trial mode */
    Uint8 prev_slot;            /* 0x0B previous slot */
    Uint8 b0C;                  /* 0x0C */
    Uint8 slot;                 /* 0x0D selected slot */
    Uint8 pad0E[4];
} ADV_MENUFLAGS;                /* 0x12 */

extern ADV_MENUFLAGS B_8C78B450;

/* 1ST_READ menu 2D library and dialog / sphere helpers */
void func_8C07DF30(Uint32 color);
void func_8C07DF6E(Uint32 c0, Uint32 c1, Uint32 c2, Uint32 c3);
void func_8C07E040(Uint32 color);
void func_8C07E0AE(Uint32 tex, Float x, Float y, Float z);
void func_8C07E642(Uint32 tex, Float x, Float y, Float z, Float sx, Float sy);
void func_8C07EC90(void);
void func_8C07F42A(Float x, Float y, Float z, Float w, Float h);
Uint32 func_8C0A58FC(Uint32 c0, Uint32 c1, Float t);
void func_8C0A5B60(Uint32 color, Float x, Float y, Float z, Float sx, Float sy);
Sint32 func_8C0A6830(void);                      /* dialog result, < 0 while open */

/* ADVERTISE, other chunks */
Uint8 func_8C900F2C(Sint32 n);
Uint8 func_8C900F86(Sint32 n);
Sint32 func_8C90117C(Sint32 n);
Sint32 func_8C901216(Sint32 n);
Sint32 func_8C901400(void);
void func_8C90179E(Sint32 req);
void func_8C901838(void);
void func_8C901850(void);
void func_8C90198E(Sint32 n);
Sint32 func_8C901A1C(Sint32 n);
Sint32 func_8C901A3E(Sint32 slot);
void func_8C90236E(Sint32 screen);
void func_8C9023E0(Sint32 a);
task *func_8C9030F0(void);
void func_8C903132(void);
void func_8C90314C(Sint8 pick);
void func_8C903F40(Sint32 n);
Uint32 func_8C900D60(void);
Uint32 func_8C900D70(void);
Uint32 func_8C9004A0(Float t);
Uint32 func_8C9004A8(Float t);
void func_8C900440(Float z);
void func_8C900640(Sint32 n, Uint32 color, Float x, Float y, Float z);
void func_8C9006B2(Sint32 n, Uint32 color, Float x, Float y, Float z, Float sx, Float sy);
void func_8C900752(Uint8 n, Float x, Float y, Float z);
void func_8C900800(Sint32 value, Sint32 ndigits, Uint8 pad, Float x, Float y, Float z, Float spacing);
Sint32 func_8C907AE0(Sint32 slot);

Sint32 func_8C9031C0(void)
{
    return B_8C78B450.trial != 0;
}

/*
 * NON_MATCHING: (1) the split gives this function 8C9031CE-8C90320A, but
 * SHC's code includes the dead epilogue at 8C90320A (see the split note
 * above); (2) the Trial test's value and the menu base swap R5 / R6, and
 * the second call uses R2 for R3. Everything else is identical.
 */
#ifdef NON_MATCHING
Sint32 func_8C9031CE(Sint32 n)
{
    ADV_MENUFLAGS *f;
    Sint32 t;

    f = &B_8C78B450;
    t = f->trial != 0;
    if (t) {
        if (f->trial_sub != 0) {
            return func_8C900F86(n) != 0;
        }
        return func_8C900F2C(n) != 0;
    }
    return func_8C90117C(n);
}
#else
ASM_STUB(Sint32 func_8C9031CE(Sint32 n))
#endif

/* the explicit return keeps SHC -speed from inlining it (the original
 * calls it everywhere) */
void func_8C903210(ADV_SELWK *w)
{
    w->ring = 0;
    w->slide = 0.0f;
    w->proc = 0;
    return;
}

/*
 * NON_MATCHING: the original computes the Trial test as a value in R4
 * (MOVT / ADD #-1 / NEG) without a frame; every C form tried (a local,
 * !!, != 0 != 0, the inlined func_8C9031C0) gives MOVT / XOR or keeps the
 * value in a stack slot, which shifts the rest of the function.
 */
#ifdef NON_MATCHING
void func_8C90321E(ADV_SELWK *w)
{
    Sint32 t;

    func_8C90236E(1);
    func_8C9030F0();
    t = B_8C78B450.trial != 0;
    func_8C90198E(t ? 4 : 3);
    func_8C901A1C(2);
    func_8C903210(w);
    if ((w->slot == 6 && func_8C9031C0()) || !func_8C9031CE(w->slot)) {
        B_8C78B450.slot = w->slot = 0;
    }
}
#else
ASM_STUB(void func_8C90321E(ADV_SELWK *w))
#endif

void func_8C90327E(ADV_SELWK *w)
{
    ADV_MENUFLAGS *f;

    f = &B_8C78B450;
    f->prev_slot = w->slot = f->slot;
    f->slot = w->slot;
}

/*
 * NON_MATCHING: complete; tools/shcdiff.py cannot compare its jump table
 * (MOVA of ".DATA.W Lcase-Lbase" items), and SHC hoists the constants 1 / 2
 * into R8 / R9 where the original keeps func_8C90179E, &B_8C78B450 and
 * func_8C0A6830 in R8 / R9 / R11 (with 0, 3, 8, 300 and 0.05 in R13 / R12 /
 * R10 / R4 / FR4 as in the original), so registers differ throughout.
 */
#ifdef NON_MATCHING
void func_8C9032E0(ADV_SELWK *w)
{
    Sint32 t;
    ADV_MENUFLAGS *f;

    f = &B_8C78B450;
    switch (w->proc) {
    case 0:
        w->ring += 0x5B0;
        if (B_8C754E5C & 0x40) {
            w->proc = 1;
            func_8C901838();
            return;
        }
        if (B_8C754E5C & 0x80) {
            w->proc = 2;
            func_8C901838();
            return;
        }
        if (func_8C900D60() && D_8C9610D8 == 1) {
            if (!func_8C9031CE(w->slot)) {
                break;
            }
            w->timer = 0;
            t = f->trial != 0;
            if (t) {
                func_8C901A3E(w->slot);
                func_8C90314C(1);
                w->proc = 3;
                break;
            }
            if (w->slot == 6) {
                if (func_8C901216(w->slot)) {
                    func_8C903F40(9);
                    w->proc = 7;
                } else {
                    func_8C903F40(8);
                    w->proc = 6;
                }
            } else {
                if (func_8C901216(w->slot)) {
                    func_8C903F40(7);
                    w->proc = 5;
                } else {
                    func_8C903F40(6);
                    w->proc = 4;
                }
            }
            break;
        }
        if (!func_8C900D70()) {
            break;
        }
        if (D_8C9610D8 != 1) {
            break;
        }
        func_8C90179E(w->back);
        func_8C901850();
        w->proc = 10;
        break;
    case 1:
        w->slide += 0.049999998f;
        if (w->slide < 1.0f) {
            break;
        }
        f->slot = w->slot = SA1_MOD(w->slot + func_8C901400() - 1, func_8C901400());
        func_8C903210(w);
        return;
    case 2:
        w->slide -= 0.049999998f;
        if (w->slide > -1.0f) {
            break;
        }
        f->slot = w->slot = SA1_MOD(w->slot + 1, func_8C901400());
        func_8C903210(w);
        return;
    case 3:
        if (w->timer++ < 300) {
            break;
        }
        func_8C90179E(f->trial_sub != 0 ? 10 : 3);
        w->proc = 10;
        break;
    case 4:
        if ((t = func_8C0A6830()) < 0) {
            break;
        }
        switch ((Sint8)t) {
        case 0:
            w->choice = 0;
            break;
        case 1:
            w->choice = 1;
            break;
        case 2:
            w->choice = 3;
            break;
        }
        w->proc = 8;
        break;
    case 5:
        if ((t = func_8C0A6830()) < 0) {
            break;
        }
        switch ((Sint8)t) {
        case 0:
            w->choice = 0;
            break;
        case 1:
            w->choice = 1;
            break;
        case 2:
            w->choice = 2;
            break;
        case 3:
            w->choice = 3;
            break;
        }
        w->proc = 8;
        break;
    case 6:
        if ((t = func_8C0A6830()) < 0) {
            break;
        }
        switch ((Sint8)t) {
        case 0:
            w->choice = 0;
            break;
        case 1:
            w->choice = 3;
            break;
        }
        w->proc = 8;
        break;
    case 7:
        if ((t = func_8C0A6830()) < 0) {
            break;
        }
        switch ((Sint8)t) {
        case 0:
            w->choice = 0;
            break;
        case 1:
            w->choice = 2;
            break;
        case 2:
            w->choice = 3;
            break;
        }
        w->proc = 8;
        break;
    case 8:
        if (w->choice == 3) {
            func_8C903210(w);
            return;
        }
        w->timer = 0;
        w->proc = 9;
        func_8C901A3E(w->slot);
        func_8C90314C(1);
        return;
    case 9:
        if (++w->timer < 300) {
            break;
        }
        switch (w->choice) {
        case 0:
            MASTERWK->next = 101;
            break;
        case 1:
            MASTERWK->next = 104;
            break;
        case 2:
            MASTERWK->next = 103;
            break;
        }
        func_8C90179E(0);
        w->proc = 10;
        break;
    case 10:
        break;
    }
}
#else
ASM_STUB(void func_8C9032E0(ADV_SELWK *w))
#endif

void func_8C9036E0(ADV_SELWK *w)
{
    njSetTexture(&D_8C941508);
    func_8C07DF30(0xFFFFFFFF);
    func_8C07E0AE(9, 26.0f, 41.0f, w->z + -14.0f);
    func_8C07F42A(0.0f, 32.0f, w->z + -8.0f, 557.0f, 41.0f);
    func_8C07E0AE(0x04000007, 316.0f, 244.0f, w->z + -12.0f);
    func_8C07DF6E(0x79FF7A00, 0x79FFFF00, 0x79FF7A00, 0x79FFFF00);
    func_8C07E642(0x04000004, 316.0f, 244.0f, w->z + -8.0f, 1.17199994f, 1.17199994f);
    func_8C07DF6E(0xFFE37500, 0xFFFEC800, 0xFFE37500, 0xFFFEC800);
    func_8C07E642(0x07000006, 316.0f, 244.0f, w->z + -10.0f, 1.109f, 1.109f);
    func_8C07DF6E(0xFFFEC800, 0xFFE47900, 0xFFFEC800, 0xFFE47900);
    func_8C07E642(0x01000005, 316.0f, 244.0f, w->z + -10.0f, 1.109f, 1.109f);
    func_8C07DF6E(0xFF0016FF, 0xFF002EFF, 0xFF0016FF, 0xFF002EFF);
    func_8C07E0AE(0x01000003, 316.0f, 312.0f, w->z + -8.0f);
    func_8C07DF6E(0xFF002EFF, 0xFF00CBFF, 0xFF002EFF, 0xFF00CBFF);
    func_8C07E0AE(0x01000000, 316.0f, 386.0f, w->z + -8.0f);
}

void func_8C903808(ADV_SELWK *w)
{
    Uint32 color;

    color = func_8C9031CE(w->slot) ? 0xFFFFFFFF : 0x80C0C0C0;
    func_8C900640(w->slot, color, 164.0f, 390.0f, w->z + -12.0f);
}

void func_8C9038E0(ADV_SELWK *w)
{
    Uint32 color;
    Float y;
    Sint32 step;
    Sint32 i;
    Sint32 slot;
    Sint32 ang;
    Float t;
    Float x;
    Float scale;

    step = SA1_FTOI(360.0f / (Float)func_8C901400() * 182.04444f);
    for (i = 0; i < func_8C901400(); i++) {
        t = w->slide;
        if (-1.0f > t) {
            t = -1.0f;
        }
        if (t > 1.0f) {
            t = 1.0f;
        }
        slot = SA1_MOD(w->slot + i, func_8C901400());
        ang = SA1_FTOI(SA1_FMAC(t, (Float)step, (Float)(step * i)));
        x = SA1_FMAC(126.0f, njSin(ang), 316.0f);
        y = SA1_FMAC(126.0f, njCos(ang), 244.0f);
        scale = 0.61499996f;
        if (slot != w->slot) {
            color = 0xFF0000FF;
        } else {
            color = func_8C0A58FC(0xFFFF0000, 0xFF0000FF, _builtin_fabsf(w->slide));
            if (w->slide == 0.0f) {
                scale = SA1_FMAC(0.18449997f, njSin(w->ring) + 1.0f, 0.61499996f);
            }
            if (w->proc >= 3) {
                scale = 1.1069999f;
            }
        }
        func_8C0A5B60(color, x, y, w->z + -12.0f, 0.75f, 0.75f);
        if (w->proc < 3 || slot == w->slot) {
            SA1_FMAC_ACC(x, njSin(ang) * D_8C9102E8[slot], 0.5f);
            if (func_8C9031CE(slot)) {
                color = 0xFFFFFFFF;
            } else {
                color = 0x80C0C0C0;
                scale = 0.61499996f;
            }
            func_8C9006B2(slot, color, x + 0.0f, y + -28.0f, w->z + -14.0f, scale, scale);
        }
    }
}

void func_8C903AE0(ADV_SELWK *w)
{
    Sint32 t;

    njSetTexture(&D_8C941508);
    func_8C07DF6E(0xFF002EFF, 0xFF00CBFF, 0xFF002EFF, 0xFF00CBFF);
    t = B_8C78B450.trial != 0;
    if (t) {
        func_8C07E0AE(0x02000002, 188.0f, 386.0f, w->z + -8.0f);
        func_8C07E0AE(0xC0000002, 444.0f, 386.0f, w->z + -8.0f);
        return;
    }
    func_8C07E0AE(0x02000001, 188.0f, 386.0f, w->z + -8.0f);
    func_8C07E0AE(0xC0000001, 444.0f, 386.0f, w->z + -8.0f);
    func_8C900800(func_8C907AE0(w->slot), 3, 0, 487.0f, 418.8f, w->z + -14.0f, 0.7f);
    func_8C900752(B_8C78B450.file, 125.899995f, 414.9f, w->z + -14.0f);
    func_8C0A5B60(0xFFFF0000, 504.0f, 418.0f, w->z + -12.0f, 0.78099997f, 0.78099997f);
    func_8C0A5B60(0xFFFF0000, 128.0f, 418.0f, w->z + -12.0f, 0.78099997f, 0.78099997f);
    njSetTexture(&D_8C9415A0);
    func_8C07DF30(0xFFFFFFFF);
    func_8C07E0AE(0x0400000B, 520.0f, 418.8f, w->z + -14.0f);
    njSetTexture(&D_8C941508);
    func_8C07E0AE(8, 538.0f, 394.0f, w->z + -12.0f);
    njSetTexture(&D_8C941AD4);
    func_8C07E0AE(0, 36.0f, 423.0f, w->z + -12.0f);
}

void func_8C903C3C(task *tp, ADV_SCRARGS *args)
{
    ADV_SELWK *w;

    w = tp->awp;
    w->back = args->back;
    w->next = args->next;
    w->z = 60000.0f;
    w->state = 0;
    w->slot = B_8C78B450.prev_slot;
    w->b24 = 0;
    w->b34 = 0;
    func_8C903210(w);
}

void func_8C903D40(task *tp)
{
    ADV_SELWK *w;

    w = tp->awp;
    if (MASTERWK->mode == 1 && w->state == 0) {
        func_8C90321E(w);
        w->fade = 0.0f;
        w->state = 1;
    }
    if (MASTERWK->prev == 1 && w->state == 2) {
        func_8C903132();
        w->fade = 0.0f;
        w->state = 3;
    }
    func_8C90327E(w);
    switch (w->state) {
    case 0:
        w->color = 0;
        break;
    case 1:
        w->color = func_8C9004A0(w->fade);
        w->fade += 0.099999995f;
        if (!(1.0f > w->fade)) {
            w->state = 2;
        }
        break;
    case 2:
        w->b34 = 0;
        MASTERWK->b21 = 0;
        w->color = 0xFFFFFFFF;
        MASTERWK->screen = 1;
        func_8C9032E0(w);
        return;
    case 3:
        w->color = func_8C9004A8(w->fade);
        w->fade += 0.099999995f;
        if (!(1.0f > w->fade)) {
            MASTERWK->b21 = 1;
        func_8C9023E0(1);
        func_8C903132();
            MASTERWK->ready = 1;
            w->state = 0;
        }
        break;
    }
}

void func_8C903E40(task *tp)
{
    ADV_SELWK *w;

    w = tp->awp;
    if (w->color != 0) {
        func_8C07EC90();
        func_8C07E040(w->color);
        func_8C9036E0(w);
        func_8C903808(w);
        func_8C9038E0(w);
        func_8C903AE0(w);
        func_8C900440(w->z + -4.0f);
    }
}

task *func_8C903EC0(ADV_SCRARGS *args)
{
    task *tp;
    ADV_SELWK *w;

    if ((D_8C9613A4 = tp = CreateElementalTask(0, 3, func_8C903D40)) != NULL) {
        w = CAlloc(1, sizeof(ADV_SELWK));
        tp->awp = w;
        func_8C903C3C(tp, args);
        tp->exec = func_8C903D40;
        tp->disp = func_8C903E40;
    }
    return tp;
}

void func_8C903F00(void)
{
    if (D_8C9613A4 != NULL) {
        DestroyTask(D_8C9613A4);
    }
}

void func_8C903F12(Sint32 next)
{
    ((ADV_SELWK *)D_8C9613A4->awp)->next = next;
}
