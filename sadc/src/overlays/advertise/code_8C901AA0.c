/*
 * ADVERTISE 8C901AA0-8C901BA8: settings bits of the current save record
 * (chunk 8C901AA0, all 5 functions). The record is B_8C7989A0[D_8C161CA4]
 * (0x4A0 bytes, the save data); the byte at +0x251 holds menu settings.
 * SHC inlines func_8C901AA0 into the other four (same file, -speed).
 *
 *  func_8C901AA0  the current save record
 *  func_8C901AB4  bit 1 of +0x251 (B_8C788C10 = !this, func_8C901280)
 *  func_8C901ADC  sets / clears bit 1
 *  func_8C901B18  bits 2-3 of +0x251: 0 -> B_8C754BFC != 0, 1 -> 0, else 1
 *  func_8C901B68  bits 2-3 = 1 (off) or 2 (on)
 */
#include "common.h"
#include "sa1/sa1.h"

typedef struct ADV_SAVE {
    Uint8 pad000[0x251];
    Sint8 flags;                /* 0x251 menu settings bits */
    Uint8 pad252[0x4A0 - 0x252];
} ADV_SAVE;                     /* 0x4A0 */

extern ADV_SAVE B_8C7989A0[];   /* save records */
extern Sint32 D_8C161CA4;       /* current save record */
extern Sint32 B_8C754BFC;       /* language (0 = Japanese) */

ADV_SAVE *func_8C901AA0(void)
{
    return &B_8C7989A0[D_8C161CA4];
}

Sint8 func_8C901AB4(void)
{
    ADV_SAVE *p;

    p = func_8C901AA0();
    return (p->flags & 2) >> 1;
}

void func_8C901ADC(Sint8 on)
{
    ADV_SAVE *p;

    p = func_8C901AA0();
    if (on) {
        p->flags |= 2;
    } else {
        p->flags &= 0xFD;
    }
}

Sint32 func_8C901B18(void)
{
    ADV_SAVE *p;
    Sint8 v;

    p = func_8C901AA0();
    v = (p->flags >> 2) & 3;
    switch (v) {
    case 0:
        return B_8C754BFC == 0 ? 0 : 1;
    case 1:
        return 0;
    default:
        return 1;
    }
}

void func_8C901B68(Sint8 on)
{
    ADV_SAVE *p;

    p = func_8C901AA0();
    p->flags &= ~0x0C;
    if (!on) {
        p->flags |= 4;
    } else {
        p->flags |= 8;
    }
}
