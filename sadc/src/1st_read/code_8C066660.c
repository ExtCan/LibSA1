#include "common.h"
#include "ninja.h"
#include "sa1/sa1.h"

/* Cross product: out = a x b. */
void func_8C066660(NJS_VECTOR *a, NJS_VECTOR *b, NJS_VECTOR *out)
{
    out->x = a->y * b->z - a->z * b->y;
    out->y = a->z * b->x - a->x * b->z;
    out->z = a->x * b->y - a->y * b->x;
}
