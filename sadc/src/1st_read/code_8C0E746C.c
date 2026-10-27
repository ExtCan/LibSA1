/*
 * 1ST_READ 8C0E746C-8C0E76A0 (chunk 8C0E6620; Big the Cat, HUD numbers):
 *
 * Both draw through the HUD number draw func_8C08EE98 (code_8C08EE98.c)
 * with a request on the stack.
 *
 *  func_8C0E746C  the fishing line length readout: f * 0.1 (2 digits, max
 *                 99, leading zeros) at (555, 418) in grey 0xFFC0C0C0, with
 *                 normal alpha blending; called by func_8C0E76A0 while the
 *                 lure is out. Leaves blending 1 / 1 and material 0.
 *  func_8C0E74FC  Big's HUD numbers (from func_8C0E6FE0 / func_8C0E70E4):
 *                 - the best weight (up to 5 digits, no leading zeros) at
 *                   (96, 32) while BDF <= 0, not in adventure fields;
 *                 - the lives func_8C08A572() (2 digits) at (49, 432)
 *                   while BDE >= 0;
 *                 - the rings func_8C08A2BC() (3 digits) at (46, 57)
 *                   ((46, 32) in adventure fields) while BDF <= 0; at 0
 *                   rings the colour blinks: r 0xC0, g = b = 192 * (1 -
 *                   sin^2), phase B_8C7A4100 += 0x400 per call.
 */
#include "common.h"
#include "sa1/sa1.h"

/* func_8C08EE98's request (number display): sa1_NUMDISP (sa1/engine.h) plus
 * the colour word func_8C08EE98 reads at +0x1C. The header's struct stops at
 * 0x1C; once it has the colour, this file uses NUMDISP from sa1/pending.h. */
typedef struct {
    Uint8 flags;                /* 0x00 1 scale, 8 width from max, 0x20 colon cell, 0x40 leading zeros */
    Uint8 unk1;                 /* 0x01 bits 0-2 priority step, 0x10/0x20 vertical, 0x40/0x80 horizontal anchor */
    Uint16 unk2;                /* 0x02 */
    Uint32 max;                 /* 0x04 */
    Uint32 value;               /* 0x08 */
    Float x;                    /* 0x0C */
    Float y;                    /* 0x10 */
    Float unk14;                /* 0x14 */
    Float scale;                /* 0x18 */
    Uint32 color;               /* 0x1C ARGB8888 */
} HUDNUM;                       /* 0x20 */
SA1_ASSERT_SIZE(HUDNUM, 0x20);
SA1_ASSERT_OFFSET(HUDNUM, value, 0x08);
SA1_ASSERT_OFFSET(HUDNUM, color, 0x1C);

extern Sint8 B_8C752BDE;        /* lives part shown */
extern Sint8 B_8C752BDF;        /* rings part shown */
extern Angle B_8C7A4100;        /* zero-ring blink phase */

extern Sint32 func_8C04C960(void);      /* level type: 1 adventure field, 2 action stage */
extern Sint16 func_8C08A572(void);      /* lives B_8C752BE0 (every caller EXTS.W R0: Sint16; code_8C08A4BA.c) */
extern Sint16 func_8C08A2BC(void);      /* rings B_8C752BE4 (code_8C08A198.c) */
extern void func_8C08EE98(HUDNUM *nd);  /* HUD number draw (code_8C08EE98.c) */

void func_8C0E746C(Float f)
{
    HUDNUM req;

    njColorBlendingMode(0, 8);
    njColorBlendingMode(1, 6);
    func_8C043A20(1.0f, 1.0f, 1.0f, 1.0f);
    req.flags = 0x48;
    req.unk1 = 0x97;
    req.unk2 = 0;
    req.max = 99;
    req.value = SA1_FTOI(f * 0.099999996f);
    req.x = 555.0f;
    req.y = 418.0f;
    req.unk14 = 0.0f;
    req.scale = 1.0f;
    req.color = 0xFFC0C0C0;
    func_8C08EE98(&req);
    njColorBlendingMode(0, 1);
    njColorBlendingMode(1, 1);
    func_8C043A20(0.0f, 0.0f, 0.0f, 0.0f);
}

void func_8C0E74FC(Sint32 weight)
{
    HUDNUM req;
    Sint32 rings;
    Float c;
    Sint32 k;
    Sint32 lives;
    Float s;

    req.flags = 8;
    req.unk1 = 0x97;
    req.unk2 = 0;
    req.max = 99999;
    req.value = weight;
    req.x = 96.0f;
    req.y = 32.0f;
    req.unk14 = 0.0f;
    req.scale = 1.0f;
    req.color = 0xFFC0C0C0;
    if (func_8C04C960() != 1) {
        if (B_8C752BDF <= 0) {
            func_8C08EE98(&req);
        }
    }
    lives = func_8C08A572();
    req.flags = 0x48;
    req.max = 99;
    req.value = lives;
    req.x = 49.0f;
    req.y = 432.0f;
    if (B_8C752BDE >= 0) {
        func_8C08EE98(&req);
    }
    rings = func_8C08A2BC();
    req.flags = 0x48;
    req.max = 999;
    req.value = rings;
    req.x = 46.0f;
    if (func_8C04C960() == 1) {
        req.y = 32.0f;
    } else {
        req.y = 57.0f;
    }
    if (rings == 0) {
        B_8C7A4100 += 0x400;
        s = njSin(B_8C7A4100);
        c = 1.0f - s * s;
    } else {
        c = 1.0f;
    }
    req.color = 0xFFC00000;
    k = SA1_FTOI(c * 192.0f);
    req.color += (k << 8) + k;
    if (B_8C752BDF <= 0) {
        func_8C08EE98(&req);
    }
}
