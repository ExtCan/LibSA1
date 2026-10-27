#include "common.h"
#include "ninja.h"

/* Squared horizontal (XZ) distance between two points. */
Float func_8C02B074(NJS_POINT3 *a, NJS_POINT3 *b)
{
    Float dx;
    Float dz;

    dx = b->x - a->x;
    dz = b->z - a->z;
    return SA1_FMAC(dx, dx, dz * dz);
}

/* Squared 3D distance between two points. */
Float func_8C02B08E(NJS_POINT3 *a, NJS_POINT3 *b)
{
    Float dx, dy, dz;

    dx = b->x - a->x;
    dy = b->y - a->y;
    dz = b->z - a->z;
#if defined(TARGET_PC)
    /* Original: FR3 = dy*dy; FMAC dx*dx into FR3; FMAC dz*dz into FR3. */
    return SA1_FMAC(dz, dz, SA1_FMAC(dx, dx, dy * dy));
#else
    return SA1_FMAC(dx, dx, dy * dy) + dz * dz;
#endif
}
