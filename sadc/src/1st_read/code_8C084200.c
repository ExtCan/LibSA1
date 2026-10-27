#include "common.h"
#include "ninja.h"

extern void func_8C07DEC0(void);
extern Sint32 njSetTexture(NJS_TEXLIST *texlist);
extern void func_8C605480(Sint32 a);
extern void func_8C07E0AE(Uint32 a, Float x, Float y, Float z);

extern NJS_TEXLIST D_8C1BF1A4;

void func_8C084200(Sint32 a, NJS_POINT3 *p)
{
    func_8C07DEC0();
    njSetTexture(&D_8C1BF1A4);
    func_8C605480(1);
    func_8C07E0AE(a, p->x, p->y, p->z);
}
