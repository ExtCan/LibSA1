/*
 * 1ST_READ 8C0E727A-8C0E746C (chunk 8C0E6620; Big the Cat, fishing HUD):
 *
 *  func_8C0E727A  while the lure task B_8C7A40C8 exists: draws the reel
 *                 sprite (D_8C566044[1]) at (592, 394), turned by the reel
 *                 phase D_8C565F80 while the lure task is in modes 4..6.
 *  func_8C0E72DA  draws the tension gauge frame (D_8C566044[0]) at
 *                 (576, 328), then the tension bar: a gouraud quad
 *                 x 574..606, from y 368 up by 135 * tension, coloured
 *                 r = c, b = 255 - c with c = tension * 255 (green 255 at
 *                 the base y 368, 0 at the top). In between it re-scales
 *                 the frame sprite (sx 1, sy 4.3 * clamp(tension, 0, 1))
 *                 and sets D_8C56606C[0].v1 = (1 - tension) * 256, but
 *                 never draws the sprite again (dead stores, kept).
 */
#include "common.h"
#include "sa1/sa1.h"

extern task *B_8C7A40C8;        /* lure task (func_8C0E6DA0's result) */
extern NJS_TEXLIST texlist_FISHING;     /* 8C54CB4C */
extern NJS_TEXANIM D_8C566044[];        /* gauge frame [0], reel [1] */
extern NJS_TEXANIM D_8C56606C[];        /* fishing HUD cells (7) */
extern Float D_8C565F78;        /* line tension 0..1 */
extern Sint32 D_8C565F80;       /* reel SE phase */

/* SDK 2D polygon draw (njDrawPolygon2D-like) */
extern void func_8C63EBC8(NJS_POINT2COL *p, Sint32 n, Float pri, Uint32 attr);

void func_8C0E727A(void)
{
    NJS_SPRITE sp;

    if (B_8C7A40C8 != NULL) {
        sp.tlist = &texlist_FISHING;
        sp.tanim = D_8C566044;
        if (B_8C7A40C8->twp->mode < 4 || B_8C7A40C8->twp->mode > 6) {
            sp.ang = 0;
        } else {
            sp.ang = D_8C565F80;
        }
        sp.sx = 1.0f;
        sp.sy = 1.0f;
        sp.p.x = 592.0f;
        sp.p.y = 394.0f;
        PC_ONLY(sp.p.z = 0.0f;) /* never set by the original (unused in 2D) */
        njDrawSprite2D(&sp, 1, -1.81799986f, 0x21);
    }
}

void func_8C0E72DA(void)
{
    NJS_SPRITE sp;
    NJS_POINT2COL p2c;
    NJS_POINT2 pts[4];
    NJS_COLOR cols[4];
    NJS_POINT2COL *pc;
    Sint32 c;

    sp.tlist = &texlist_FISHING;
    sp.tanim = D_8C566044;
    sp.ang = 0;
    sp.sx = 1.0f;
    sp.sy = 1.0f;
    sp.p.x = 576.0f;
    sp.p.y = 328.0f;
    PC_ONLY(sp.p.z = 0.0f;) /* never set by the original (unused in 2D) */
    njDrawSprite2D(&sp, 0, -1.81899992f, 0x20);
    sp.sx = 1.0f;
    if (D_8C565F78 < 0.0f) {
        sp.sy = 0.0f;
    } else if (D_8C565F78 >= 1.0f) {
        sp.sy = 4.29999983f;
    } else {
        sp.sy = D_8C565F78 * 4.29999983f;
        D_8C56606C[0].v1 = (Sint16)SA1_FTOI((1.0f - D_8C565F78) * 256.0f);
    }
    pc = &p2c;
    pc->p = pts;
    pc->col = cols;
    pc->num = 4;
    pc->p[0].x = pc->p[3].x = 574.0f;
    pc->p[0].y = pc->p[1].y = 368.0f;
    pc->p[1].x = pc->p[2].x = 606.0f;
    pc->p[2].y = pc->p[3].y = 368.0f - D_8C565F78 * 135.0f;
    c = SA1_FTOI(D_8C565F78 * 255.0f) & 0xFF;
    pc->col[0].color = pc->col[1].color = 0xFF00FFFF + ((c << 16) - c);
    pc->col[2].color = pc->col[3].color = 0xFF0000FF + ((c << 16) - c);
    pc->tex = NULL;
    func_8C63EBC8(pc, 4, -1.81999996f, 0x20);
}
