#include "sa1/sa1.h"

/*
 * E-102 laser impact effect (1ST_READ chunk 8C0D62E0-8C0D67C0): an expanding
 * translucent sphere built at run time as an 8 x 8 band triangle-strip model
 * (D_8C4BB05C) whose buffers live in BSS.
 *
 *  func_8C0D62E0  one-time build of the sphere (flag D_8C4BB08C): the 8-entry
 *                 (cos, sin) table B_8C7A3064 (45 degree steps), the 128
 *                 vertices B_8C7A30A4 (8 bands of 8 vertex pairs, each pair
 *                 the band's top and bottom ring), the strip list B_8C7A3CA4
 *                 (8 strips of 18 indices, closed by repeating the first two)
 *                 and the UVs (func_8C0D6454(0)).
 *  func_8C0D6454  strip UVs B_8C7A3DD4: per strip 9 x (u, v), (u, v + 63),
 *                 u and v advancing by 64; v starts at the argument (the
 *                 task's timer.l, so the texture scrolls).
 *  func_8C0D6500  moves each band's top ring towards its bottom ring:
 *                 ring = bottom + f * (top - bottom) (f = twp->scl.z, fades
 *                 from 1.0 so the bands open up).
 *  func_8C0D65BA  exec (started by the laser shot dest func_8C0D3D0C):
 *                 mode 0 init (60 frames, growth speed 5.0, scale 2.0,
 *                 open 1.0, collision D_8C4BB090), mode 1 grows the sphere
 *                 up to D_8C4BB02C, switches the collision attribute 0x10 on
 *                 at frame 30 and closes the bands afterwards, mode 2 frees.
 *  func_8C0D66F4  display: alpha = scl.z * 255, scale scl.x.
 *
 * Matching notes:
 *  - the table values (c, s) of the vertex loops and the alpha source in
 *    func_8C0D66F4 are block-scoped locals: SHC then copies the invariant
 *    factor into FR3 and multiplies by the local (FMOV r,FR3; FMUL c,FR3)
 *    instead of multiplying the dying local in place, and loads both table
 *    values before the first store.
 *  - func_8C0D6454 copies its argument into a local before the loops
 *    (otherwise u and v swap registers).
 *  - func_8C0D65BA writes the clamp as a min() expansion (sum spelled twice);
 *    func_8C0D66F4 uses the "a ? njRotateY(NULL, a) : (void)0" idiom (CSE of
 *    ang.y in a 4-byte stack slot, stored in the BT/S delay slot).
 */

extern Sint32 D_8C4BB08C;           /* sphere buffers built */
extern Float B_8C7A3064[];          /* [8][2] (cos, sin) of 0, 0x2000, ... 0xE000 */
extern NJS_POINT3 B_8C7A30A4[];     /* [128] sphere vertices (D_8C4BB05C.points) */
extern Sint16 B_8C7A3CA4[];         /* [8 * 19] strip list: 18, 18 indices */
extern Sint16 B_8C7A3DD4[];         /* [8 * 18][2] strip UVs (NJS_TEX) */
extern NJS_MODEL D_8C4BB05C;        /* sphere model */
extern NJS_TEXLIST D_8C4BB084;
extern CCL_INFO D_8C4BB090[];       /* one collision shape */
extern Float D_8C4BB02C;            /* maximum scale */

void func_8C0D6454(Sint32 v);
void func_8C0D66F4(task *tp);

void func_8C0D62E0(void)
{
    Float *p;
    Float *q;
    Sint16 *ip;
    Sint32 ang;
    Sint32 i;
    Sint32 j;
    Sint32 n;
    Sint32 k;
    Float s0;
    Float c0;
    Float s1;
    Float c1;

    if (D_8C4BB08C == 0) {
        D_8C4BB08C = 1;
        p = B_8C7A3064;
        for (ang = 0; ang < 0x10000; ang += 0x2000) {
            *p++ = njCos(ang);
            *p++ = njSin(ang);
        }
        q = &B_8C7A30A4[0].x;
        for (ang = 0; ang < 0x8000; ang += 0x1000) {
            p = B_8C7A3064;
            s0 = njSin(ang);
            c0 = njCos(ang);
            s1 = njSin(ang + 0x1000);
            c1 = njCos(ang + 0x1000);
            for (i = 0; i < 0x10000; i += 0x2000) {
                Float c;
                Float s;

                c = *p++;
                s = *p++;
                *q++ = s0 * c;
                *q++ = c0;
                *q++ = s0 * s;
                *q++ = s1 * c;
                *q++ = c1;
                *q++ = s1 * s;
            }
        }
        ip = B_8C7A3CA4;
        n = 0;
        for (i = 0; i < 8; i++) {
            k = n;
            *ip++ = 18;
            for (j = 0; j < 8; j++) {
                *ip++ = n++;
                *ip++ = n++;
            }
            *ip++ = k++;
            *ip++ = k;
        }
        func_8C0D6454(0);
    }
}

void func_8C0D6454(Sint32 v)
{
    Sint16 *p;
    Sint32 i;
    Sint32 j;
    Sint32 u;
    Sint32 w;

    p = B_8C7A3DD4;
    w = v;
    for (i = 0; i < 8; i++) {
        u = 0;
        for (j = 0; j < 9; j++) {
            *p++ = u;
            *p++ = w;
            *p++ = u;
            *p++ = w + 63;
            u += 64;
            w += 64;
        }
    }
}

void func_8C0D6500(Float f)
{
    Float *p;
    Float *q;
    Sint32 ang;
    Sint32 i;
    Float s0;
    Float c0;
    Float s1;
    Float c1;
    Float y;
    Float r;

    q = &B_8C7A30A4[0].x;
    for (ang = 0; ang < 0x8000; ang += 0x1000) {
        p = B_8C7A3064;
        s0 = njSin(ang);
        c0 = njCos(ang);
        s1 = njSin(ang + 0x1000);
        c1 = njCos(ang + 0x1000);
        r = SA1_FMAC(f, s0 - s1, s1);
        y = SA1_FMAC(f, c0 - c1, c1);
        for (i = 0; i < 0x10000; i += 0x2000) {
            Float c;
            Float s;

            c = *p++;
            s = *p++;
            *q++ = r * c;
            *q++ = y;
            *q++ = r * s;
            q += 3;
        }
    }
}

void func_8C0D65BA(task *tp)
{
    taskwk *twp;

    twp = tp->twp;
    switch (twp->mode) {
    case 0:
        twp->wtimer = 60;
        twp->timer.l = 0;
        twp->value.f = 5.0f;
        twp->scl.x = 2.0f;
        twp->scl.z = 1.0f;
        func_8C0D62E0();
        CCL_Init(tp, D_8C4BB090, 1, 1, 1);
        tp->disp = func_8C0D66F4;
        twp->mode = 1;
        break;
    case 1:
        twp->wtimer--;
        if (twp->wtimer == 0) {
            twp->mode = 2;
            break;
        }
        if (twp->wtimer == 30) {
            CWP(twp)->info->attr |= 0x10;
        } else if (twp->wtimer < 30) {
            twp->scl.z -= 0.033f;
        }
        twp->value.f += 0.099999996f;
        twp->value.f *= 0.79999997f;
        twp->scl.x = (twp->scl.x + twp->value.f < D_8C4BB02C)
            ? twp->scl.x + twp->value.f : D_8C4BB02C;
        CWP(twp)->info->a = twp->scl.x * 0.79999997f;
        twp->timer.l += 10;
        break;
    case 2:
        FreeTask(tp);
        return;
    }
    func_8C0D66F4(tp);
    func_8C02F63C(twp);
}

void func_8C0D66F4(task *tp)
{
    taskwk *twp;
    NJS_MODEL *model;
    Float sc;

    twp = tp->twp;
    njSetTexture(&D_8C4BB084);
    func_8C0D6500(twp->scl.z);
    func_8C0D6454(twp->timer.l);
    njPushMatrix(NULL);
    model = &D_8C4BB05C;
    sc = twp->scl.x;
    {
        Float a;

        a = twp->scl.z;
        model->mats->diffuse.argb.a = SA1_FTOI(a * 255.0f);
    }
    njTranslateV(NULL, &twp->pos);
    twp->ang.y ? njRotateY(NULL, twp->ang.y) : (void)0;
    njScale(NULL, sc, sc, sc);
    func_8C03700C(model);
    njPopMatrix(1);
}
