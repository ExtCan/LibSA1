/*
 * 1ST_READ chunk 8C0A5840-8C0A6830: the menu dialog (the small window with a
 * row of spheres the menus open: the character select's Game / Instruction /
 * Ending / Cancel, gsa1_090_charselect.md 2.5) and its sphere helpers
 * (staged for the character select front end, agent dc-charsel-1b).
 *
 * One dialog task exists at a time (B_8C7A1E40, created by 8C0A6660 with the
 * exec 8C0A6182 and the display 8C0A65B2); its any-work points to a 0x40-byte
 * DLGWK. A menu opens it with a DLGDEF (8C0A66A8 / 8C0A66EA at once,
 * 8C0A6730 / 8C0A6792 / 8C0A682A with the opening animation), polls
 * 8C0A6830 for the picked item and closes it by re-opening or freeing it.
 *
 *   func_8C0A5840  cursor := first item at or after n that is not disabled
 *   func_8C0A589A  highlight pulse: phase += 0x2D8, scale back to 1 by 0.05,
 *                  alpha = (1 - sqrt(2 (scale - 1))) * 255
 *   func_8C0A58FC  colour lerp a -> b by t (per channel)
 *   func_8C0A5A64  the cursor sphere (yellow gradient, AVA_CSR-like texlist
 *                  D_8C1C4A40, texture 3, centred)
 *   func_8C0A5AC4  the same for type-1 dialogs (texlist D_8C1C4A6C, texture 2)
 *   func_8C0A5B60  an item sphere (textures 2 and 1 of D_8C1C4A40)
 *   func_8C0A5BCA  an item sphere of a type-1 dialog (textures 0 and 1 of D_8C1C4A6C)
 *   func_8C0A5C3A  a window frame centred on (x, y) (8C07F2A0)
 *   func_8C0A5C6C  reset the dialog work (scale 0, nothing drawn)
 *   func_8C0A5C9A  copy the disabled-item list (-1 terminated) or clear it
 *   func_8C0A5D40  is item n in the list (and not the exempt item)?
 *   func_8C0A5D72  next enabled item after n (or n)
 *   func_8C0A5DAE  previous enabled item before n (or n)
 *   func_8C0A5DE8  the window frame of the dialog
 *   func_8C0A5EA0  the items and the cursor
 *   func_8C0A6178  dialog work init (no definition, closed)
 *   func_8C0A6182  dialog exec: opening (16 frames of D_8C15B6F0 scales),
 *                  open (input: left / right by type, or free movement for
 *                  type 2; A / Start picks, B cancels), picked (4 frames),
 *                  closing (16 frames of D_8C15B770), closed (mode 5)
 *   func_8C0A65B2  dialog display (frame, items, the definition's callback)
 *   func_8C0A6660  create the dialog task
 *   func_8C0A6696  free it
 *   func_8C0A66A8  open at once (cursor, disabled list)
 *   func_8C0A66EA  open at once with an exempt item
 *   func_8C0A6730  open with the animation (and the open SE)
 *   func_8C0A6792  the same with an exempt item
 *   func_8C0A682A  open with the animation, cursor 0, nothing disabled
 *
 * Input: B_8C754E5C is the pad's "pressed this frame" word (bits 2 B, 4 A,
 * 8 Start, 0x10 up, 0x20 down, 0x40 left, 0x80 right, 0x400 the second
 * cancel button, UNVERIFIED name of 0x400).
 */
#include "sa1/sa1.h"

typedef struct {
    Sint32 open;                /* 0x00 played by 8C0A6730 / 8C0A6792 */
    Sint32 close;               /* 0x04 picked -> closing */
    Sint32 move;                /* 0x08 the cursor moved */
    Sint32 pick;                /* 0x0C A / Start on an item */
    Sint32 cancel;              /* 0x10 B, or A on the cancel item */
} DLGSE;                        /* 0x14 */

typedef struct {
    Float x;                    /* 0x00 relative to the window centre, scaled by DLGWK.sx */
    Float y;                    /* 0x04 */
    Uint8 tex;                  /* 0x08 the item's label in DLGDEF.texlist */
    Uint8 pad09[3];
} DLGITEM;                      /* 0x0C */

/* What the definition's callback gets every frame (8C0A65B2) */
typedef struct {
    Float x;                    /* 0x00 DLGDEF.x */
    Float y;                    /* 0x04 DLGDEF.y */
    Float pri;                  /* 0x08 DLGDEF.pri */
    Float sx;                   /* 0x0C DLGWK.sx */
    Float sy;                   /* 0x10 DLGWK.sy */
    Sint8 cursor;               /* 0x14 */
    Uint8 pad15[3];
} DLGDISP;                      /* 0x18 */

typedef struct {
    Sint32 type;                /* 0x00 0, 2: blue spheres (2: free cursor movement), 1: yellow */
    void (*disp)(DLGDISP *d);   /* 0x04 */
    NJS_TEXLIST *texlist;       /* 0x08 the item labels */
    DLGITEM *item;              /* 0x0C */
    DLGSE *se;                  /* 0x10 */
    NJS_COLOR col0;             /* 0x14 window gradient (top) */
    NJS_COLOR col1;             /* 0x18 (bottom) */
    Float x;                    /* 0x1C window centre */
    Float y;                    /* 0x20 */
    Float pri;                  /* 0x24 */
    Float w;                    /* 0x28 */
    Float h;                    /* 0x2C */
    Float iw;                   /* 0x30 sphere scale */
    Float ih;                   /* 0x34 */
    Sint8 count;                /* 0x38 */
    Sint8 cancel;               /* 0x39 the item B selects */
} DLGDEF;

typedef struct {
    DLGDEF *def;                /* 0x00 */
    Sint32 mode;                /* 0x04 0 reset, 1 opening, 2 open, 3 picked, 4 closing, 5 closed */
    Uint32 col;                 /* 0x08 global colour; 0: not drawn */
    Float sx;                   /* 0x0C */
    Float sy;                   /* 0x10 */
    Float pulse;                /* 0x14 cursor scale: 1.5 after a move, back to 1 */
    Uint32 col_on;              /* 0x18 the selected sphere */
    Uint32 col_off;             /* 0x1C the others */
    Sint32 phase;               /* 0x20 highlight phase (BAMS) */
    Sint16 held;                /* 0x24 frames on the current item (up to 0x7FFF) */
    Sint16 timer;               /* 0x26 */
    Sint8 cursor;               /* 0x28 */
    Uint8 alpha;                /* 0x29 cursor alpha */
    Sint8 off[16];              /* 0x2A disabled items, -1 terminated */
    Uint8 pad3A[2];
    Sint32 keep;                /* 0x3C exempt item (not disabled, but cannot be picked) */
} DLGWK;                        /* 0x40 */

SA1_ASSERT_SIZE(DLGSE, 0x14);
SA1_ASSERT_SIZE(DLGITEM, 0x0C);
SA1_ASSERT_SIZE(DLGDISP, 0x18);
SA1_ASSERT_OFFSET(DLGDEF, count, 0x38);
SA1_ASSERT_SIZE(DLGWK, 0x40);
SA1_ASSERT_OFFSET(DLGWK, off, 0x2A);
SA1_ASSERT_OFFSET(DLGWK, keep, 0x3C);

typedef struct {
    Float sx;                   /* 0x00 */
    Float sy;                   /* 0x04 */
} DLGSCALE;                     /* 0x08 */

typedef struct {
    Uint32 prev;                /* 0x00 buttons that move the cursor back */
    Uint32 next;                /* 0x04 and forward */
} DLGKEYS;                      /* 0x08 */

extern task *B_8C7A1E40;                /* the dialog task */
extern Uint32 B_8C754E5C;               /* pad 0: buttons pressed this frame */
extern NJS_TEXLIST D_8C1C4A40;          /* spheres (blue dialogs) */
extern NJS_TEXLIST D_8C1C4A6C;          /* spheres (yellow dialogs) */
extern DLGSCALE D_8C15B6F0[16];         /* opening scales */
extern DLGSCALE D_8C15B770[16];         /* closing scales */
extern DLGKEYS D_8C15B7F0[3];           /* cursor keys per dialog type */

extern void func_8C07DF30(Uint32 col);
extern void func_8C07DF6E(Uint32 c0, Uint32 c1, Uint32 c2, Uint32 c3);
extern void func_8C07E040(Uint32 col);
extern void func_8C07E642(Uint32 attr, Float x, Float y, Float pri, Float sx, Float sy);
extern void func_8C07EC90(void);
extern void func_8C07F2A0(Float x, Float y, Float pri, Float w, Float h);

Sint32 func_8C0A5D40(Sint8 *list, Sint8 n);
void func_8C0A5C9A(Sint8 *list);

void func_8C0A5840(Sint8 n)
{
    DLGWK *dw;
    Sint8 i;

    dw = (DLGWK *)B_8C7A1E40->awp;
    for (i = 0; i < dw->def->count; i++) {
        if (func_8C0A5D40(dw->off, n)) {
            n++;
            if (n >= dw->def->count) {
                n = 0;
            }
        }
    }
    dw->cursor = n;
}

void func_8C0A589A(Uint8 *alpha, Sint32 *phase, Float *pulse)
{
    *phase += 0x2D8;
    *pulse = (*pulse > 1.0f) ? *pulse + -0.049999998f : 1.0f;
    *alpha = (1.0f - njSqrt((*pulse - 1.0f) * 2.0f)) * 255.0f;
}

Uint32 func_8C0A58FC(Uint32 a, Uint32 b, Float t)
{
    Uint32 ca;
    Uint32 cr;
    Uint32 cg;
    Uint32 cb;

    ca = SA1_FMAC(1.0f - t, (Float)(a >> 24), (Float)(b >> 24) * t);
    cr = SA1_FMAC(1.0f - t, (Float)((a >> 16) & 0xFF), (Float)((b >> 16) & 0xFF) * t);
    cg = SA1_FMAC(1.0f - t, (Float)((a >> 8) & 0xFF), (Float)((b >> 8) & 0xFF) * t);
    cb = SA1_FMAC(1.0f - t, (Float)(a & 0xFF), (Float)(b & 0xFF) * t);
    return (ca << 24) | (cr << 16) | (cg << 8) | cb;
}

void func_8C0A5A64(Uint8 a, Float x, Float y, Float pri, Float sx, Float sy)
{
    Uint32 c0;
    Uint32 c1;

    c0 = ((Uint32)a << 24) | 0x00FFEB00;
    c1 = ((Sint32)a << 24) | 0x00EC9800;
    njSetTexture(&D_8C1C4A40);
    func_8C07DF6E(c0, c1, c0, c1);
    func_8C07E642(0x04000003, x, y, pri, sx, sy);
}

void func_8C0A5AC4(Uint8 a, Float x, Float y, Float pri, Float sx, Float sy)
{
    njSetTexture(&D_8C1C4A6C);
    func_8C07DF30((a << 24) | 0x00EC9800);
    func_8C07E642(0x04000002, x, y, pri, sx, sy);
}

void func_8C0A5B60(Uint32 col, Float x, Float y, Float pri, Float sx, Float sy)
{
    njSetTexture(&D_8C1C4A40);
    func_8C07DF30(col);
    func_8C07E642(0x04000002, x, y, pri + -0.29999999f, sx, sy);
    func_8C07DF30((col & 0xFF000000) | 0x00FFFFFF);
    func_8C07E642(0x04000001, x, y, pri + -0.59999998f, sx, sy);
}

void func_8C0A5BCA(Uint32 col, Float x, Float y, Float pri, Float sx, Float sy)
{
    njSetTexture(&D_8C1C4A6C);
    func_8C07DF30(col);
    func_8C07E642(0x04000000, x, y, pri + -0.29999999f, sx, sy);
    func_8C07DF30((col & 0xFF000000) | 0x00FFFFFF);
    func_8C07E642(0x04000001, x, y, pri, sx * 1.0460432f, sy * 1.1770245f);
}

void func_8C0A5C3A(Float x, Float y, Float pri, Float w, Float h)
{
    func_8C07F2A0(x - w * 0.5f, y - h * 0.5f, pri, w, h);
}

void func_8C0A5C6C(DLGWK *dw)
{
    dw->col = 0;
    dw->sx = 0.0f;
    dw->sy = 0.0f;
    dw->pulse = 1.0f;
    dw->held = 0;
    dw->timer = 0;
    func_8C0A5C9A(NULL);
    dw->keep = -1;
}

void func_8C0A5C9A(Sint8 *list)
{
    DLGWK *dw;
    Sint32 i;

    dw = (DLGWK *)B_8C7A1E40->awp;
    i = 0;
    if (list != NULL) {
        do {
            dw->off[i++] = *list;
        } while (*list++ >= 0);
    } else {
        for (i = 0; i < 16; i++) {
            dw->off[i] = -1;
        }
    }
}

Sint32 func_8C0A5D40(Sint8 *list, Sint8 n)
{
    Sint32 r;

    r = 0;
    while (*list >= 0) {
        if (n == *list) {
            r = 1;
        }
        list++;
    }
    if (n == ((DLGWK *)B_8C7A1E40->awp)->keep) {
        r = 0;
    }
    return r;
}

/* NON_MATCHING: complete and faithful; block order (the original falls through into the return after the 8C0A5D40 call and merges the first return into it) */
#ifdef NON_MATCHING
Sint8 func_8C0A5D72(Sint8 *list, Sint8 n, Sint8 count)
{
    Sint8 cur;
    Sint32 last;

    last = count - 1;
    if (n != last) {
        cur = n;
        do {
            n++;
            if (func_8C0A5D40(list, n) != 0) {
                continue;
            }
            return n;
        } while (n < last);
        return cur;
    }
    return n;
}
#else
ASM_STUB(Sint8 func_8C0A5D72(Sint8 *list, Sint8 n, Sint8 count))
#endif

/* NON_MATCHING: complete and faithful; block order, as 8C0A5D72 */
#ifdef NON_MATCHING
Sint8 func_8C0A5DAE(Sint8 *list, Sint8 n)
{
    Sint8 cur;

    if (n > 0) {
        cur = n;
        do {
            n--;
            if (func_8C0A5D40(list, n) != 0) {
                continue;
            }
            return n;
        } while (n > 0);
        return cur;
    }
    return n;
}
#else
ASM_STUB(Sint8 func_8C0A5DAE(Sint8 *list, Sint8 n))
#endif

void func_8C0A5DE8(DLGWK *dw)
{
    DLGDEF *def;

    def = dw->def;
    func_8C07EC90();
    func_8C07DF6E(def->col0.color, def->col1.color, def->col0.color, def->col1.color);
    func_8C0A5C3A(def->x, def->y, def->pri + -4.0f, def->w * dw->sx, def->h * dw->sy);
}

/* NON_MATCHING: complete and faithful; the original keeps -6.0 in FR12 and multiplies by a fresh FLDI1 1.0, SHC here keeps 1.0 in FR12; constants R8-R10 hoisted differently */
#ifdef NON_MATCHING
void func_8C0A5EA0(DLGWK *dw)
{
    DLGDEF *def;
    Sint8 i;
    Float x;
    Float y;
    Float sx;
    Float sy;
    Uint32 col;
    Uint32 lab;
    DLGITEM *it;
    Uint8 a;

    def = dw->def;
    for (i = 0; i < def->count; i++) {
        x = SA1_FMAC(def->item[i].x, dw->sx, def->x);
        y = SA1_FMAC(def->item[i].y, dw->sy, def->y);
        if (dw->cursor == i) {
            sx = dw->sx * 1.19999996f;
            sy = dw->sy * 1.19999996f;
        } else {
            sx = dw->sx * 1.0f;
            sy = dw->sy * 1.0f;
        }
        if (dw->keep == i || func_8C0A5D40(dw->off, i)) {
            col = (dw->col_off & 0x00FFFFFF) | 0x30000000;
            lab = 0xB0FFFFFF;
        } else {
            col = (dw->cursor == i) ? dw->col_on : dw->col_off;
            lab = 0xFFFFFFFF;
        }
        switch (def->type) {
        case 0:
        case 2:
            func_8C0A5B60(col, x, y, def->pri - 6.0f, def->iw * sx, def->ih * sy);
            break;
        case 1:
            func_8C0A5BCA(col, x, y, def->pri - 6.0f, def->iw * sx, def->ih * sy);
            break;
        }
        njSetTexture(def->texlist);
        func_8C07DF30(lab);
        func_8C07E642(def->item[i].tex | 0x04000000, x, y, def->pri - 8.0f, sx, sy);
    }
    if (dw->cursor < def->count) {
        if (func_8C0A5D40(dw->off, dw->cursor)) {
            a = dw->alpha / 2;
        } else {
            a = dw->alpha;
        }
        sx = def->iw * dw->sx * dw->pulse * 1.19999996f;
        sy = def->ih * dw->sy * dw->pulse * 1.19999996f;
        it = &def->item[dw->cursor];
        switch (def->type) {
        case 0:
        case 2:
            func_8C0A5A64(a, SA1_FMAC(it->x, dw->sx, def->x), SA1_FMAC(it->y, dw->sy, def->y), def->pri - 10.0f, sx, sy);
            break;
        case 1:
            func_8C0A5AC4(a, SA1_FMAC(it->x, dw->sx, def->x), SA1_FMAC(it->y, dw->sy, def->y), def->pri - 10.0f, sx, sy);
            break;
        }
    }
}
#else
ASM_STUB(void func_8C0A5EA0(DLGWK *dw))
#endif

void func_8C0A6178(task *tp)
{
    DLGWK *dw;

    dw = (DLGWK *)tp->awp;
    dw->def = NULL;
    dw->mode = 0;
    func_8C0A5C6C(dw);
}

/* NON_MATCHING: complete and faithful; register allocation of the hoisted colour constants (0xFF12B4FF in R12, 0xFFFF0000 in R8 in the original) and of the mode switch constants differs */
#ifdef NON_MATCHING
void func_8C0A6182(task *tp)
{
    DLGWK *dw;
    DLGDEF *def;
    Sint8 prev;
    Sint8 moved;
    Sint32 i;
    DLGITEM *cur;
    DLGITEM *it;
    Float dx;
    Float dy;
    Float d;
    Float best;

    dw = (DLGWK *)tp->awp;
    def = dw->def;
    func_8C0A589A(&dw->alpha, &dw->phase, &dw->pulse);
    dw->col_on = 0xFFFF0000;
    if (def != NULL) {
        switch (def->type) {
        case 0:
        case 2:
            dw->col_off = 0xFF0000FF;
            break;
        case 1:
            dw->col_off = 0xFF12B4FF;
            break;
        }
    } else {
        dw->col_off = 0xFF12B4FF;
    }
    switch (dw->mode) {
    case 0:
        func_8C0A5C6C(dw);
        return;
    case 1:
        dw->col = 0xFFFFFFFF;
        dw->sx = D_8C15B6F0[dw->timer].sx;
        dw->sy = D_8C15B6F0[dw->timer].sy;
        dw->timer++;
        if (dw->timer >= 16) {
            dw->phase = 0;
            dw->mode = 2;
        }
        break;
    case 2:
        dw->col = 0xFFFFFFFF;
        dw->sy = 1.0f;
        dw->sx = 1.0f;
        moved = 0;
        switch (def->type) {
        case 0:
        case 2:
            dw->col_on = func_8C0A58FC(0xFF0000FF, 0xFFFF0000, (njCos(dw->phase) + 1.0f) * 0.5f);
            break;
        case 1:
            dw->col_on = func_8C0A58FC(0xFF12B4FF, 0xFFFF0000, (njCos(dw->phase) + 1.0f) * 0.5f);
            break;
        }
        prev = dw->cursor;
        if (def->type == 2) {
            if (B_8C754E5C & 0xF0) {
                cur = &def->item[dw->cursor];
                PC_ONLY(best = 0.0f;)
                for (i = 0; i < def->count; i++) {
                    it = &def->item[i];
                    dy = it->y - cur->y;
                    dx = it->x - cur->x;
                    d = SA1_FMAC(dx, dx, (dy * 2.0f) * (dy * 2.0f));
                    if (i == dw->cursor) {
                        continue;
                    }
                    if ((B_8C754E5C & 0x40) && !(-5.0f > dx)) {
                        continue;
                    }
                    if ((B_8C754E5C & 0x80) && !(dx > 5.0f)) {
                        continue;
                    }
                    if ((B_8C754E5C & 0x10) && !(-5.0f > dy)) {
                        continue;
                    }
                    if ((B_8C754E5C & 0x20) && !(dy > 5.0f)) {
                        continue;
                    }
                    if (moved && !(best > d)) {
                        continue;
                    }
                    best = d;
                    moved = 1;
                    dw->cursor = i;
                }
            }
        } else if ((B_8C754E5C & D_8C15B7F0[def->type].prev) && dw->cursor > 0) {
            dw->cursor = func_8C0A5DAE(dw->off, dw->cursor);
            moved = 1;
        } else if ((B_8C754E5C & D_8C15B7F0[def->type].next) && dw->cursor < def->count - 1) {
            dw->cursor = func_8C0A5D72(dw->off, dw->cursor, def->count);
            moved = 1;
        }
        if (!moved) {
            if (B_8C754E5C & (8 | 4)) {
                if (!func_8C0A5D40(dw->off, dw->cursor) && dw->cursor != dw->keep) {
                    dw->timer = 0;
                    dw->mode = 3;
                    if (def->se != NULL) {
                        func_8C0940DA(dw->cursor == def->cancel ? def->se->cancel : def->se->pick, 0, 0, 0);
                    }
                }
            } else if (B_8C754E5C & (0x400 | 2)) {
                dw->cursor = def->cancel;
                dw->timer = 0;
                dw->mode = 3;
                if (def->se != NULL) {
                    func_8C0940DA(def->se->cancel, 0, 0, 0);
                }
            }
        }
        if (prev != dw->cursor) {
            dw->held = 0;
            dw->pulse = 1.5f;
            if (def->se != NULL) {
                func_8C0940DA(def->se->move, 0, 0, 0);
            }
        } else if (dw->held < 0x7FFF) {
            dw->held++;
        }
        break;
    case 3:
        dw->timer++;
        if (dw->timer >= 4) {
            dw->timer = 0;
            dw->mode = 4;
            if (def->se != NULL) {
                func_8C0940DA(def->se->close, 0, 0, 0);
            }
        }
        break;
    case 4:
        dw->col = 0xFFFFFFFF;
        dw->sx = D_8C15B770[dw->timer].sx;
        dw->sy = D_8C15B770[dw->timer].sy;
        dw->timer++;
        if (dw->timer >= 16) {
            func_8C0A5C6C(dw);
            dw->mode = 5;
        }
        break;
    case 5:
        break;
    }
}
#else
ASM_STUB(void func_8C0A6182(task *tp))
#endif

void func_8C0A65B2(task *tp)
{
    DLGWK *dw;
    DLGDISP d;

    dw = (DLGWK *)tp->awp;
    if (dw->col == 0) {
        return;
    }
    if (dw->sx == 0.0f && dw->sy == 0.0f) {
        return;
    }
    func_8C07E040(dw->col);
    func_8C0A5DE8(dw);
    func_8C0A5EA0(dw);
    if (dw->def->disp != NULL) {
        d.x = dw->def->x;
        d.y = dw->def->y;
        d.pri = dw->def->pri;
        d.sx = dw->sx;
        d.sy = dw->sy;
        d.cursor = dw->cursor;
        dw->def->disp(&d);
    }
}

task *func_8C0A6660(void)
{
    task *tp;
    DLGWK *dw;

    if ((B_8C7A1E40 = tp = CreateElementalTask(8, 3, func_8C0A6182)) != NULL) {
        dw = (DLGWK *)MAlloc(sizeof(DLGWK));
        tp->awp = dw;
        func_8C0A6178(tp);
        tp->exec = func_8C0A6182;
        tp->disp = func_8C0A65B2;
    }
    return tp;
}

void func_8C0A6696(void)
{
    if (B_8C7A1E40 != NULL) {
        FreeTask(B_8C7A1E40);
    }
}

void func_8C0A66A8(DLGDEF *def, Sint8 n, Sint8 *off)
{
    DLGWK *dw;
    Sint16 z;

    ((DLGWK *)B_8C7A1E40->awp)->def = def;
    func_8C0A5C9A(off);
    ((DLGWK *)B_8C7A1E40->awp)->keep = -1;
    func_8C0A5840(n);
    z = 0;
    dw = (DLGWK *)B_8C7A1E40->awp;
    dw->held = z;
    dw = (DLGWK *)B_8C7A1E40->awp;
    dw->phase = z;
    dw->mode = 2;
}

void func_8C0A66EA(DLGDEF *def, Sint8 n, Sint8 *off, Sint8 keep)
{
    DLGWK *dw;
    Sint32 z;

    ((DLGWK *)B_8C7A1E40->awp)->def = def;
    func_8C0A5C9A(off);
    ((DLGWK *)B_8C7A1E40->awp)->keep = keep;
    func_8C0A5840(n);
    z = 0;
    dw = (DLGWK *)B_8C7A1E40->awp;
    dw->held = z;
    dw = (DLGWK *)B_8C7A1E40->awp;
    dw->phase = z;
    dw->mode = 2;
}

void func_8C0A6730(DLGDEF *def, Sint8 n, Sint8 *off)
{
    DLGWK *dw;
    Sint16 z;

    ((DLGWK *)B_8C7A1E40->awp)->def = def;
    ((DLGWK *)B_8C7A1E40->awp)->mode = 1;
    func_8C0A5C9A(off);
    ((DLGWK *)B_8C7A1E40->awp)->keep = -1;
    func_8C0A5840(n);
    z = 0;
    dw = (DLGWK *)B_8C7A1E40->awp;
    dw->held = z;
    if (def->se != NULL) {
        func_8C0940DA(def->se->open, 0, 0, 0);
    }
}

void func_8C0A6792(DLGDEF *def, Sint8 n, Sint8 *off, Sint8 keep)
{
    DLGWK *dw;
    Sint16 z;

    ((DLGWK *)B_8C7A1E40->awp)->def = def;
    ((DLGWK *)B_8C7A1E40->awp)->mode = 1;
    func_8C0A5C9A(off);
    ((DLGWK *)B_8C7A1E40->awp)->keep = keep;
    func_8C0A5840(n);
    z = 0;
    dw = (DLGWK *)B_8C7A1E40->awp;
    dw->held = z;
    if (def->se != NULL) {
        func_8C0940DA(def->se->open, 0, 0, 0);
    }
}

void func_8C0A682A(DLGDEF *def)
{
    func_8C0A6730(def, 0, NULL);
}

/* 1ST_READ chunk 8C0A6830 (the next chunk; same translation unit): the
 * dialog's result: the picked item once it has closed (mode 5), else -1. */
Sint32 func_8C0A6830(void)
{
    DLGWK *dw;

    dw = (DLGWK *)B_8C7A1E40->awp;
    if (dw->mode >= 5) {
        return dw->cursor;
    }
    return -1;
}
