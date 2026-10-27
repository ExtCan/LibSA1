/*
 * 1ST_READ 8C05E0CE-8C05E5C0 without 8C05E0F8 (chunk 8C05D9C0-8C05E680,
 * item box code; dc-itembox-a, staged for G-SA1 0.9.2; gsa1_090_faults.md
 * F2b and F2d): the item icon list task and the bomb sphere.
 *
 * Item icon list B_8C78C394 (12-byte header + 20 entries; func_8C05E0F8 in
 * code_8C05E0F8.c appends the kind of every item picked up, x = -60,
 * scale 2.0, value = B_8C78C37C, and sets mode 1). The stage start
 * (8C058E48) creates the list task with func_8C05E0CE:
 *
 *  func_8C05E0CE  create the task (exec 8C05E14E, disp 8C05E33C, dest
 *                 8C05E57E); empty list, bomb sphere off (radius -1)
 *  func_8C05E14E  exec: mode 1 slide the icons in (8C05E1E0), mode 2 hold
 *                 them 60 frames (8C05E28E), mode 3 shrink them away in 60
 *                 frames, then empty the list (8C05E2EE); every frame also
 *                 the bomb sphere (8C05E59A)
 *  func_8C05E33C  disp: draw every queued icon (8C05E3A0)
 *  func_8C05E3A0  draw one icon (2D sprite at x, 370) and, for the random
 *                 rings item, the amount (a second sprite, one quarter of
 *                 the digit texture per amount)
 *  func_8C05E57E  dest: nothing
 *
 * Bomb sphere (item kind 7, handler func_8C05D930): centre B_8C78C384,
 * radius B_8C78C390 (< 0 = off):
 *
 *  func_8C05E582  start it at pos with radius 10
 *  func_8C05E59A  (no symbol in the split: BRA tail call at 8C05E188 from
 *                 func_8C05E14E) grow it by 10 per frame; past 500 off
 *  func_8C05E5C2  1 when pos is inside the sphere: in code_8C05E5C2.c (its
 *                 own file, so that it can be landed separately: LANDING.md)
 *
 * Not here: 8C05E612 (no symbol: debug print of the item name, the BRA tail
 * call of objexec_O_ItemBoxAir in debug mode; a fatal stub in libsa1).
 *
 * SPLIT: the manifest has no function at 8C05E59A, so tools/shcdiff.py
 * counts 8C05E59A-8C05E5C0 as part of func_8C05E582 and cannot resolve the
 * BRA of func_8C05E14E. With the symbol func_8C05E59A = 0x8C05E59A added to
 * config/symbols.1st_read.txt (LANDING.md) func_8C05E14E, func_8C05E582 and
 * func_8C05E59A match. func_8C05E3A0 is NON_MATCHING (see there); its cell
 * D_8C1B9C64 has no symbol in the split (the split names D_8C1B9C74 =
 * D_8C1B9C64.texid and the cell D_8C1B9C78 with its fields): LANDING.md.
 */
#include "common.h"
#include "ninja.h"
#include "task.h"
#include "sa1/sa1.h"

typedef struct {
    Sint32 kind;     /* +0x0 item kind (index of D_8C1B9BA0)                        */
    Float  x;        /* +0x4 screen x of the icon: -60, += 30 per frame up to its slot */
    Float  scale;    /* +0x8 2.0 (+0.2 per frame, at most 2.0), shrinks in mode 3   */
    Sint32 value;    /* +0xC copy of B_8C78C37C (amount of a random-rings item)     */
} ITEMICON_ENTRY;

typedef struct {
    Sint32 mode;                /* +0x0 0 idle, 1 slide in, 2 hold, 3 shrink         */
    Sint32 num;                 /* +0x4 number of queued entries (no bound, as in the game) */
    Uint32 timer;               /* +0x8 frames of mode 2 / mode 3 (unsigned: CMP/HI, unsigned FLOAT) */
    ITEMICON_ENTRY entry[20];   /* +0xC                                              */
} ITEMICON_LIST;

SA1_ASSERT_SIZE(ITEMICON_ENTRY, 0x10);
SA1_ASSERT_SIZE(ITEMICON_LIST, 0x14C);

/* Item kind table (game data, see code_8C05DF18.c): icon texture and handler */
typedef struct {
    Sint32 tex;                 /* 0x00 texture number of the icon in D_8C19F530 */
    void (*func)(taskwk *twp);  /* 0x04 pickup handler */
} ITEMBOX_ITEM;                 /* 0x08 */

extern ITEMICON_LIST B_8C78C394;
extern NJS_POINT3 B_8C78C384;           /* bomb sphere centre */
extern Float B_8C78C390;                /* bomb sphere radius, < 0 = off */
extern ITEMBOX_ITEM D_8C1B9BA0[9];
extern Sint32 D_8C1B9BE8[8];            /* 1-up icon texture per character */
extern NJS_TEXLIST D_8C19F530;          /* item icon textures */
extern NJS_TEXANIM D_8C1B9C64;          /* the icon cell (texid = the item's icon) */
extern NJS_TEXANIM D_8C1B9C78;          /* the random-rings amount cell (one quarter of the digits) */

void func_8C05E14E(task *tp);
void func_8C05E1E0(void);
void func_8C05E28E(void);
void func_8C05E2EE(void);
void func_8C05E33C(task *tp);
void func_8C05E3A0(Sint32 kind, Float x, Sint32 value, Float scl);
void func_8C05E57E(task *tp);
void func_8C05E582(NJS_POINT3 *pos);
void func_8C05E59A(task *tp);

/* create the item icon list task (stage start 8C058E48) */
void func_8C05E0CE(void)
{
    task *tp;

    tp = CreateElementalTask(2, 3, func_8C05E14E);
    tp->disp = func_8C05E33C;
    tp->dest = func_8C05E57E;
    B_8C78C394.num = 0;
    B_8C78C394.mode = 0;
    B_8C78C390 = -1.0f;
}

/* exec of the item icon list task */
void func_8C05E14E(task *tp)
{
    switch (B_8C78C394.mode) {
    case 0:
        break;
    case 1:
        func_8C05E1E0();
        func_8C05E33C(tp);
        break;
    case 2:
        func_8C05E28E();
        func_8C05E33C(tp);
        break;
    case 3:
        func_8C05E2EE();
        func_8C05E33C(tp);
        break;
    }
    func_8C05E59A(tp);
}

/* mode 1: the icons slide in from the left to their slots around x = 320 */
void func_8C05E1E0(void)
{
    ITEMICON_LIST *list;
    Sint32 i;
    Sint32 n;
    Float slot;

    list = &B_8C78C394;
    for (i = 0, n = 0; i < list->num; i++) {
        list->entry[i].x += 30.0f;
        slot = (Float)(list->num - 1) * 40.0f - (Float)i * 80.0f + 320.0f;
        if (!(slot > list->entry[i].x)) {
            list->entry[i].x = slot;
            n++;
        }
        list->entry[i].scale += 0.19999999f;
        if (list->entry[i].scale > 2.0f) {
            list->entry[i].scale = 2.0f;
        }
    }
    if (n == list->num) {
        list->mode = 2;
        list->timer = 0;
    }
}

/* mode 2: hold for 60 frames */
void func_8C05E28E(void)
{
    ITEMICON_LIST *list;
    Sint32 i;

    list = &B_8C78C394;
    for (i = 0; i < list->num; i++) {
        list->entry[i].scale += 0.19999999f;
        if (list->entry[i].scale > 2.0f) {
            list->entry[i].scale = 2.0f;
        }
    }
    list->timer++;
    if (list->timer > 60) {
        list->timer = 60;
        list->mode = 3;
    }
}

/* mode 3: shrink away in 60 frames, then empty the list */
void func_8C05E2EE(void)
{
    ITEMICON_LIST *list;
    Sint32 i;

    list = &B_8C78C394;
    for (i = 0; i < list->num; i++) {
        list->entry[i].scale = (Float)list->timer * 0.033333332f;
    }
    list->timer--;
    if (list->timer == 0) {
        list->num = 0;
        list->mode = 0;
    }
}

/* disp of the item icon list task */
void func_8C05E33C(task *tp)
{
    ITEMICON_LIST *list;
    Sint32 i;
    ITEMICON_ENTRY *e;

    list = &B_8C78C394;
    for (i = 0; i < list->num; i++) {
        e = &list->entry[i];
        func_8C05E3A0(e->kind, e->x, e->value, e->scale);
    }
}

/*
 * Draw one item icon at (x, 370) with scale scl; for the random-rings item
 * (kind 4) the amounts 5 and 10 use the 5- and 10-rings icons, the others
 * draw the amount on top (a second sprite at (x - 1, 372): one quarter of
 * the digits texture per amount: 40, 20, 15, else 1). The 1-up icon is
 * the character's own (D_8C1B9BE8[character]).
 *
 * NON_MATCHING (C complete and functionally faithful): the first 70
 * instructions match except that kind and the constant 0 swap R13 and R14;
 * in the amount switch the original loads the addresses of
 * D_8C1B9C78.u1 / v1 / u2 from the pool in every case (v2 = u1 + 6, MOV.W
 * Rm,@Rn stores), SHC here keeps &D_8C1B9C78 in a register and stores with
 * MOV.W R0,@(disp,Rn). Tried without success: one NJS_TEXANIM[2] array,
 * two cells, a Sint16[4] or scalar symbols for the UV fields, stores
 * through num.tanim, defined (initialised) cells, constant addresses, a
 * #pragma inline helper, if/else chains, a local zero. The draw call in
 * every case (cross-jumped into one tail) is what puts njDrawSprite2D in
 * R12 and -9.0 in FR15 as in the original; the order of the first
 * sprite's stores gives the original's ADD #-16 from &texid to the cell.
 */
#ifdef NON_MATCHING
void func_8C05E3A0(Sint32 kind, Float x, Sint32 value, Float scl)
{
    NJS_SPRITE sp;
    NJS_SPRITE num;

    if (kind == 4) {
        switch (value) {
        case 5:
            kind = 2;
            break;
        case 10:
            kind = 3;
            break;
        }
    }
    D_8C1B9BA0[6].tex = D_8C1B9BE8[GetPlayerNumber()];
    D_8C1B9C64.texid = D_8C1B9BA0[kind].tex;
    sp.tlist = &D_8C19F530;
    sp.tanim = &D_8C1B9C64;
    sp.sx = scl;
    sp.sy = scl;
    sp.ang = 0;
    sp.p.x = x;
    sp.p.y = 370.0f;
    njColorBlendingMode(0, 8);
    njColorBlendingMode(1, 6);
    func_8C043A20(1.0f, 1.0f, 1.0f, 1.0f);
    njDrawSprite2D(&sp, 0, -10.0f, 0x22);
    if (kind == 4) {
        num.p.x = x + -1.0f;
        num.p.y = 372.0f;
        num.sx = scl;
        num.sy = scl;
        num.ang = 0;
        num.tlist = &D_8C19F530;
        num.tanim = &D_8C1B9C78;
        switch (value) {
        case 40:
            D_8C1B9C78.u1 = 0;
            D_8C1B9C78.v1 = 0;
            D_8C1B9C78.u2 = 127;
            D_8C1B9C78.v2 = 127;
            njDrawSprite2D(&num, 0, -9.0f, 0x22);
            break;
        case 20:
            D_8C1B9C78.u1 = 0;
            D_8C1B9C78.v1 = 128;
            D_8C1B9C78.u2 = 127;
            D_8C1B9C78.v2 = 255;
            njDrawSprite2D(&num, 0, -9.0f, 0x22);
            break;
        case 15:
            D_8C1B9C78.u1 = 128;
            D_8C1B9C78.v1 = 0;
            D_8C1B9C78.u2 = 255;
            D_8C1B9C78.v2 = 127;
            njDrawSprite2D(&num, 0, -9.0f, 0x22);
            break;
        default:
            D_8C1B9C78.u1 = 128;
            D_8C1B9C78.v1 = 128;
            D_8C1B9C78.u2 = 255;
            D_8C1B9C78.v2 = 255;
            njDrawSprite2D(&num, 0, -9.0f, 0x22);
            break;
        }
    }
    func_8C043A20(0.0f, 0.0f, 0.0f, 0.0f);
}
#else
ASM_STUB(void func_8C05E3A0(Sint32 kind, Float x, Sint32 value, Float scl))
#endif

/* dest of the item icon list task */
void func_8C05E57E(task *tp)
{
}

/* bomb: the sphere starts at pos with radius 10 */
void func_8C05E582(NJS_POINT3 *pos)
{
    B_8C78C384 = *pos;
    B_8C78C390 = 10.0f;
}

/* bomb: the sphere grows by 10 per frame, off past 500 (8C05E59A, no symbol) */
void func_8C05E59A(task *tp)
{
    if (B_8C78C390 >= 0.0f) {
        B_8C78C390 += 10.0f;
        if (B_8C78C390 > 500.0f) {
            B_8C78C390 = -1.0f;
        }
    }
}
