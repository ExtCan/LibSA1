/*
 * ADVERTISE 8C900640-8C9008C6: name / label / digit sprites of the menu
 * (chunk 8C900640, functions 1-6 of 13; the rest is not executed by the
 * character select).
 *
 *  func_8C900640  full character name (B_CHNAM, the bottom bar): sprite
 *                 D_8C90EB08[n]; English (B_8C754BFC != 0) scaled 0.78
 *  func_8C9006B2  ring label (M_CHNAM) D_8C90EB0F[n], scaled
 *  func_8C900702  the same from texlist D_8C94147C / table D_8C90EB16
 *  func_8C900752  "File" sprites (AVA_FSCMN) D_8C90EB1D[n]
 *  func_8C900792  number of decimal digits of n (at least 1)
 *  func_8C900800  a right-aligned number in AVA_SUUJI digits (D_8C90EB21),
 *                 ndigits wide, leading zeros only if pad == 1
 */
#include "common.h"
#include "sa1/sa1.h"

extern NJS_TEXLIST D_8C9413C4;  /* b_chnam(_e) */
extern NJS_TEXLIST D_8C941420;  /* m_chnam */
extern NJS_TEXLIST D_8C94147C;
extern NJS_TEXLIST D_8C941AD4;  /* ava_fscmn(_e) */
extern NJS_TEXLIST D_8C9415A0;  /* ava_suuji */
extern Uint8 D_8C90EB08[];      /* sprite numbers per name */
extern Uint8 D_8C90EB0F[];
extern Uint8 D_8C90EB16[];
extern Uint8 D_8C90EB1D[];
extern Uint8 D_8C90EB21[];      /* digit sprites 0-9 */
extern Sint32 B_8C754BFC;       /* language (0 = Japanese) */

/* 1ST_READ menu 2D library */
void func_8C07DF30(Uint32 color);
void func_8C07E0AE(Uint32 tex, Float x, Float y, Float z);
void func_8C07E642(Uint32 tex, Float x, Float y, Float z, Float sx, Float sy);
void func_8C07EF42(NJS_TEXLIST *tl, Sint32 n, Sint32 *w, Sint32 *h);

void func_8C900640(Sint32 n, Uint32 color, Float x, Float y, Float z)
{
    njSetTexture(&D_8C9413C4);
    func_8C07DF30(color);
    if (B_8C754BFC == 0) {
        func_8C07E0AE(D_8C90EB08[n], x, y, z);
    } else {
        func_8C07E642(D_8C90EB08[n], x, y, z, 0.78f, 0.78f);
    }
}

void func_8C9006B2(Sint32 n, Uint32 color, Float x, Float y, Float z, Float sx, Float sy)
{
    njSetTexture(&D_8C941420);
    func_8C07DF30(color);
    func_8C07E642(D_8C90EB0F[n] | 0x04000000, x, y, z, sx, sy);
}

void func_8C900702(Sint32 n, Uint32 color, Float x, Float y, Float z, Float sx, Float sy)
{
    njSetTexture(&D_8C94147C);
    func_8C07DF30(color);
    func_8C07E642(D_8C90EB16[n] | 0x04000000, x, y, z, sx, sy);
}

void func_8C900752(Uint8 n, Float x, Float y, Float z)
{
    njSetTexture(&D_8C941AD4);
    func_8C07DF30(0xFFFFFFFF);
    func_8C07E0AE(D_8C90EB1D[n] | 0x04000000, x, y, z);
}

Sint32 func_8C900792(Sint32 n)
{
    Sint32 i;

    i = 0;
    do {
        n /= 10;
        i++;
    } while (n != 0);
    return i;
}

void func_8C900800(Sint32 value, Sint32 ndigits, Uint8 pad, Float x, Float y, Float z, Float spacing)
{
    Sint32 h;
    Sint32 w;
    Sint32 len;
    Sint32 d;
    Uint8 c;

    if (ndigits != 0) {
        len = func_8C900792(value);
        njSetTexture(&D_8C9415A0);
        func_8C07DF30(0xFFFFFFFF);
        func_8C07EF42(&D_8C9415A0, 1, &w, &h);
        SA1_FMAC_ACC(x, (Float)((ndigits - 1) * w), spacing);
        for (; ndigits != 0; ndigits--) {
            d = value % 10;
            c = D_8C90EB21[d];
            if (pad == 1 || len > 0) {
                func_8C07E0AE(c | 0x04000000, x, y, z);
                x -= (Float)w * spacing;
            }
            value /= 10;
            len--;
        }
    }
}
