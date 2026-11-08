/*
 * sa1/upgdisp.h: the display set of the upgrades, SA1_GA only (docs/research/gsa1_100_select.md 4.3.3, which widens
 * gsa1_091b_upgrades.md 5.1 U1 with its review R.4; staged by R0's p-select, landed by R1's r1-s1).
 *
 * SA1_GA is libsa1's lowered game code only: not the SHC build, the harness's SHC / TARGET_PC builds or their traces.
 * Outside SA1_GA every macro below is the bare game expression, so those builds compile exactly today's code.
 *
 * "Active" is the game's own upgrade: the event flag, PInitialize's playerwk.equipment bit and every ability test of it.
 * "Visible" is this set: only the lines where the game picks the worn part read SA1_UPG_SHOWN(pwp->equipment) instead of
 * the bit, so a part can be drawn without the ability or hidden with it. playerwk.equipment is never written. Amy's hammer
 * (node 36, the spin's hit sphere) and E-102's blaster (node 37 / the arm's node 6, the shot's start) are game logic as well
 * as models: their TUs keep the real bit's part in the tree and put the shown part in only around their draws.
 *
 * One pcdata block per TU that picks a part, nine in all, each with its tag below. libsa1 finds them by their magic words
 * (LIB src/world/sa1_world_charopt.c, next to co_tailsnap; the game option SA1_GOPT_UPGRADE_MODELS) and writes on and shown
 * into all nine between ticks. While on is 0, the initial value, every line is the game's, so the reference / exactness
 * worlds, which never set the option, run the game's code.
 *   on     0: the game; non-zero: the lines below read shown
 *   shown  the playerwk.equipment bits whose part is drawn (0x0001 Crystal Ring, 0x0002 Light Speed Shoes, 0x0008 Jet
 *          Anklet, 0x0010 Rhythm Badge, 0x0020 Fighting Gloves, 0x0040 Shovel Claw, 0x0080 Long Hammer, 0x0100 Warrior
 *          Feather, 0x0200 Laser Blaster, 0x0400 Jet Booster, 0x0800 Power Rod, 0x1000 Life Belt; 0x0004 Ancient Light has
 *          no model), and the lures 0x10000 << k (k = 0..3: event flags 0x1DE..0x1E1). Their count picks the cast lure as
 *          func_8C0E6E80's count of the flags does; func_8C0E6E80 itself (B_8C78C5D8, the item object, the fish) is unchanged.
 */
#ifndef SA1_UPGDISP_H
#define SA1_UPGDISP_H

#if defined(SA1_GA)

#define SA1_UPGDISP_MAGIC0 0x44475055u      /* "UPGD" */
/* magic[1]: the TU's tag, four characters, little endian */
#define SA1_UPGDISP_TAG_A77B 0x42373741u    /* "A77B" code_8C0A77BC.c SonicMain: Light Speed Shoes, Crystal Ring */
#define SA1_UPGDISP_TAG_B614 0x34313642u    /* "B614" code_8C0B6148.c TailsMain: Jet Anklet, Rhythm Badge */
#define SA1_UPGDISP_TAG_C4C2 0x32433443u    /* "C4C2" code_8C0C4C2E.c Knuckles' hands: Shovel Claw, Fighting Gloves */
#define SA1_UPGDISP_TAG_DA10 0x30314144u    /* "DA10" code_8C0DA106.c Amy's display: Warrior Feather, Long Hammer */
#define SA1_UPGDISP_TAG_CFAC 0x43414643u    /* "CFAC" code_8C0CFACE.c E-102's display: Jet Booster, Laser Blaster */
#define SA1_UPGDISP_TAG_DFFA 0x41464644u    /* "DFFA" code_8C0DFFA0.c BigMain: Life Belt, Power Rod */
#define SA1_UPGDISP_TAG_E772 0x32373745u    /* "E772" code_8C0E7720.c the cast lure's draws */
#define SA1_UPGDISP_TAG_E681 0x31383645u    /* "E681" code_8C0E6812.c the lure on the line (func_8C0E6CC0) */
#define SA1_UPGDISP_TAG_EA04 0x34304145u    /* "EA04" code_8C0EA040.c the lure on the ground (func_8C0EA2F0) */

/* 0x10 bytes, four words: no padding on any target */
typedef struct SA1_UPGDISP {
    Uint32 magic[2];            /* 0x00 SA1_UPGDISP_MAGIC0, the TU's tag */
    Uint32 on;                  /* 0x08 0: the game */
    Uint32 shown;               /* 0x0C the parts drawn while on */
} SA1_UPGDISP;

#define SA1_UPGDISP_DEFINE(tag) static SA1_UPGDISP sa1_upgdisp = { { SA1_UPGDISP_MAGIC0, (tag) }, 0, 0 }

/* the bits a part-picking line tests: the real ones, or the shown set */
#define SA1_UPG_SHOWN(eq)   (sa1_upgdisp.on ? (Sint16)sa1_upgdisp.shown : (eq))
/* the cast lure's index: func_8C0E6E80() (the number of lure flags set), or the number of lures shown. func_8C0E6E80 is called
   either way (r1-s1, MEASURED by LIB tests/upgdisp_test.c U5): it also stores its count in B_8C78C5D8, which the fishing reads */
#define SA1_UPG_LURES()     (sa1_upgdisp.on ? ((void)func_8C0E6E80(), \
                                               (Sint32)(((sa1_upgdisp.shown >> 16) & 1u) + ((sa1_upgdisp.shown >> 17) & 1u) \
                                                        + ((sa1_upgdisp.shown >> 18) & 1u) + ((sa1_upgdisp.shown >> 19) & 1u))) \
                                            : func_8C0E6E80())
/* the shown bit differs from the real one: the draw-time swaps of Amy's hammer and E-102's blaster */
#define SA1_UPG_DIFFERS(eq, bit) (sa1_upgdisp.on && ((((Uint32)(Sint32)(eq)) ^ sa1_upgdisp.shown) & (Uint32)(bit)))

#else

#define SA1_UPG_SHOWN(eq)   (eq)
#define SA1_UPG_LURES()     func_8C0E6E80()

#endif /* SA1_GA */

#endif /* SA1_UPGDISP_H */
