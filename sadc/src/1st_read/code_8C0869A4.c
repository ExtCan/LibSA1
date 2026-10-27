#include "common.h"

/* memset-like fill: writes n copies of c backwards from dst + n */
void func_8C0869A4(Uint8 *dst, Uint8 c, Sint32 n) {
    dst += n;
    do {
        *--dst = c;
    } while (--n != 0);
}
