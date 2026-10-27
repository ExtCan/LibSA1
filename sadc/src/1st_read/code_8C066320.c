#include "common.h"
#include "ninja.h"

/* 8C066320: copy a 3-float vector (src -> dst). */
void func_8C066320(NJS_POINT3 *src, NJS_POINT3 *dst) {
    Float *s;
    Float *d;

    s = &src->x;
    d = &dst->x;
    *d++ = *s++;
    *d++ = *s++;
    *d = *s;
}
