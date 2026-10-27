/*
 * 1ST_READ 8C05E0F8 (chunk 8C05D9C0-8C05E680, item box code).
 * func_8C05E0F8: queue an item icon in the item-get icon list B_8C78C394
 * (B_8C78C394 is 332 bytes in bss = 12-byte header + 20 entries of 16 bytes).
 * Called with the item kind by the item boxes (func_8C05D386, func_8C05DF18)
 * and by func_8C0E943C. The list is animated by func_8C05E14E's state
 * machine (func_8C05E1E0/28E/2EE): entry y slides towards a slot position,
 * scale is clamped to 2.0f.
 */
#include "common.h"

typedef struct {
    Sint32 kind;     /* +0x0 item kind (argument)              */
    Float  y;        /* +0x4 vertical position, starts at -60  */
    Float  scale;    /* +0x8 starts at 2.0                     */
    Sint32 value;    /* +0xC copy of B_8C78C37C                */
} ITEMICON_ENTRY;

typedef struct {
    Sint32 mode;                /* +0x0 0 idle, 1..3 animation phases */
    Sint32 num;                 /* +0x4 number of queued entries      */
    Sint32 timer;               /* +0x8 cleared by func_8C05E1E0      */
    ITEMICON_ENTRY entry[20];   /* +0xC                               */
} ITEMICON_LIST;

SA1_ASSERT_SIZE(ITEMICON_ENTRY, 0x10);
SA1_ASSERT_SIZE(ITEMICON_LIST, 0x14C);

extern ITEMICON_LIST B_8C78C394;
extern Sint32 B_8C78C37C;

#if defined(SA1_GA)
/*
 * SA1_GA (libsa1's lowered game code only; not the SHC build, the harness's SHC / TARGET_PC builds or their traces): a
 * PC-only host option (docs/research/gsa1_094_items.md 6.6 / F13, gsa1_094_plan.md 6.3 task 5; libsa1
 * sa1_character_set_game_option SA1_GOPT_ITEM_ICON_LIMIT). The list B_8C78C394 holds 20 entries and the append below
 * has no bound: the list task (func_8C05E14E) empties it only 134 steps after the last append, so 21 or more items in
 * quick succession (a row of item boxes, Big's lure func_8C0E943C) write past it into B_8C78C4E0 (the jump panel list)
 * and on up to the player task pointers B_8C78C608 (40: a fault in func_8C078EB0). The game never places boxes that
 * densely; a host that lets the player place them can. With `on` set, an append to a full list is skipped: the item is
 * still given (the callers give it before or after, unchanged), only its icon is not queued. A pcdata block (one per
 * world) that libsa1 finds by its magic words (LIB src/world/sa1_world_charopt.c) and sets. While `on` is 0, the
 * initial value, the function is the original, so worlds that never set it (the reference / exactness worlds) run the
 * game's code.
 */
#define SA1_ICONLIMIT_MAGIC0 0x4E4F4349  /* "ICON" (sa1_world_charopt.c CO_ICONLIMIT_*) */
#define SA1_ICONLIMIT_MAGIC1 0x38463045  /* "E0F8" */
typedef struct SA1_ICONLIMIT {
    Uint32 magic[2];            /* 0x00 SA1_ICONLIMIT_MAGIC0, SA1_ICONLIMIT_MAGIC1 */
    Sint32 on;                  /* 0x08 0: the game's unbounded append; 1: the 20 entries are a bound */
} SA1_ICONLIMIT;
SA1_ASSERT_OFFSET(SA1_ICONLIMIT, on, 0x08);
static SA1_ICONLIMIT sa1_iconlimit = { { SA1_ICONLIMIT_MAGIC0, SA1_ICONLIMIT_MAGIC1 }, 0 };
#endif

void func_8C05E0F8(Sint32 kind)
{
#if defined(SA1_GA)
    if (sa1_iconlimit.on && B_8C78C394.num >= 20) {
        return;
    }
#endif
    B_8C78C394.entry[B_8C78C394.num].kind = kind;
    B_8C78C394.entry[B_8C78C394.num].y = -60.0f;
    B_8C78C394.entry[B_8C78C394.num].scale = 2.0f;
    B_8C78C394.entry[B_8C78C394.num].value = B_8C78C37C;
    B_8C78C394.num++;
    B_8C78C394.mode = 1;
}
