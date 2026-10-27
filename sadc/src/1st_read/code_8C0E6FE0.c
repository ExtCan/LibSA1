/*
 * 1ST_READ 8C0E6FE0-8C0E727A (chunk 8C0E6620; Big the Cat, HUD):
 *
 * Big has no common HUD (8C08A4BA / 8C08A724 skip player 7); his fishing HUD
 * task (exec func_8C0EB160, code_8C0EB160.c) draws it instead, with the
 * common HUD's visibility flags B_8C752BDD (all), BDE (lives), BDF (rings).
 *
 *  func_8C0E6FE0  the in-play HUD (the exec's mode 1, every frame; drawn on
 *                 step 0 only): FISHING cell 4 "bighyouji" (WEIGHT label and
 *                 ring icon) at (48, 64) and cell 5 "hyouji_g" at (184, 47)
 *                 while BDF <= 0 (sic: the game's test is "BDF > 0 skips",
 *                 not the common HUD's "BDF >= 0 draws"), the life icon
 *                 (D_8C5660F8 = CON_REGULAR's first 24 names, cell 19
 *                 "hyoji_zankib") at (16, 416) while BDE >= 0, then the
 *                 numbers func_8C0E74FC(best weight) (code_8C0E746C.c).
 *                 Not in adventure fields (8C04C960() == 1) and not in
 *                 Twinkle Circuit (8C0A1218). Afterwards it copies BDD into
 *                 BDE and BDF (-1 in adventure fields), as 8C08A4BA does
 *                 for the other characters.
 *  func_8C0E70E4  the same as the task's display (run only by the pause
 *                 display pass 8C0988C4): no level-type gate; in adventure
 *                 fields it draws cell 8 at (48, 54) instead of cells 4 / 5.
 */
#include "common.h"
#include "sa1/sa1.h"

extern Sint8 B_8C752BDD;        /* HUD shown (-1 while an event runs) */
extern Sint8 B_8C752BDE;        /* lives part shown */
extern Sint8 B_8C752BDF;        /* rings part shown */
extern Sint32 B_8C7A40D8;       /* Big's best fish weight */
extern NJS_TEXLIST texlist_FISHING;     /* 8C54CB4C */
extern NJS_TEXANIM D_8C566044[];        /* fishing HUD cells (4: bighyouji, 5: hyouji_g, 8) */
extern NJS_TEXLIST D_8C5660F8;          /* { CON_REGULAR names, 24 } */
extern NJS_TEXANIM D_8C566100[];        /* [0]: texid 19 hyoji_zankib */

extern Sint32 func_8C0A1218(void);      /* Twinkle Circuit (level 35) */
extern Sint32 func_8C04C960(void);      /* level type: 1 adventure field, 2 action stage */
extern void func_8C605480(Sint32 a);    /* Kamui draw state (0 before, 1 after 2D draws) */

void func_8C0E6FE0(task *tp);
void func_8C0E70E4(task *tp);
ASM_STUB(void func_8C0E74FC(Sint32 weight))

void func_8C0E6FE0(task *tp)
{
    NJS_SPRITE sp;

    sp.tlist = &texlist_FISHING;
    sp.tanim = D_8C566044;
    sp.sx = 1.0f;
    sp.sy = 1.0f;
    PC_ONLY(sp.p.z = 0.0f;) /* never set by the original (unused in 2D) */
    PC_ONLY(sp.ang = 0;)    /* never set by the original (attr 0x20: no NJD_SPRITE_ANGLE) */
    if (func_8C0A1218() != 0) {
        return;
    }
    if (B_8C754EC8 != 0) {
        return;
    }
    if (func_8C04C960() == 1) {
        return;
    }
    if (B_8C752BDD < 0) {
        return;
    }
    njColorBlendingMode(0, 1);
    func_8C605480(0);
    func_8C043A20(1.0f, 1.0f, 1.0f, 1.0f);
    if (B_8C752BDF <= 0) {
        sp.p.x = 48.0f;
        sp.p.y = 64.0f;
        njDrawSprite2D(&sp, 4, -1.81999996f, 0x20);
        sp.p.x = 184.0f;
        sp.p.y = 47.0f;
        njDrawSprite2D(&sp, 5, -1.81999996f, 0x20);
    }
    if (B_8C752BDE >= 0) {
        sp.tlist = &D_8C5660F8;
        sp.tanim = D_8C566100;
        sp.p.x = 16.0f;
        sp.p.y = 416.0f;
        njDrawSprite2D(&sp, 0, -2.0f, 0x20);
    }
    func_8C0E74FC(B_8C7A40D8);
    func_8C605480(1);
    func_8C043A20(0.0f, 0.0f, 0.0f, 0.0f);
    if (func_8C04C960() == 1) {
        B_8C752BDF = -1;
        B_8C752BDE = -1;
    } else {
        B_8C752BDF = B_8C752BDE = B_8C752BDD;
    }
}

void func_8C0E70E4(task *tp)
{
    NJS_SPRITE sp;

    sp.tlist = &texlist_FISHING;
    sp.tanim = D_8C566044;
    sp.sx = 1.0f;
    sp.sy = 1.0f;
    PC_ONLY(sp.p.z = 0.0f;) /* never set by the original (unused in 2D) */
    PC_ONLY(sp.ang = 0;)    /* never set by the original (attr 0x20: no NJD_SPRITE_ANGLE) */
    if (func_8C0A1218() != 0) {
        return;
    }
    if (B_8C754EC8 != 0) {
        return;
    }
    if (B_8C752BDD < 0) {
        return;
    }
    njColorBlendingMode(0, 1);
    func_8C605480(0);
    func_8C043A20(1.0f, 1.0f, 1.0f, 1.0f);
    if (B_8C752BDF <= 0) {
        if (func_8C04C960() == 1) {
            sp.p.x = 48.0f;
            sp.p.y = 54.0f;
            njDrawSprite2D(&sp, 8, -1.81999996f, 0x20);
        } else {
            sp.p.x = 48.0f;
            sp.p.y = 64.0f;
            njDrawSprite2D(&sp, 4, -1.81999996f, 0x20);
            sp.p.x = 184.0f;
            sp.p.y = 47.0f;
            njDrawSprite2D(&sp, 5, -1.81999996f, 0x20);
        }
    }
    if (B_8C752BDE >= 0) {
        sp.tlist = &D_8C5660F8;
        sp.tanim = D_8C566100;
        sp.p.x = 16.0f;
        sp.p.y = 416.0f;
        njDrawSprite2D(&sp, 0, -2.0f, 0x20);
    }
    func_8C0E74FC(B_8C7A40D8);
    func_8C605480(1);
    func_8C043A20(0.0f, 0.0f, 0.0f, 0.0f);
    if (func_8C04C960() == 1) {
        B_8C752BDF = -1;
        B_8C752BDE = -1;
    } else {
        B_8C752BDF = B_8C752BDE = B_8C752BDD;
    }
}
