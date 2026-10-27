#include "common.h"
#include "ninja.h"
#include "sa1/ninja_funcs.h"

/* Debug: draws a white 3D line from (x1,y1,z1) to (x2,y2,z2) (code_8C043A20.c) */
extern void func_8C043A40(Float x1, Float y1, Float z1, Float x2, Float y2, Float z2);

/*
 * func_8C057A20: debug draw of a direction ray. Rotates the vector (0, 0, len)
 * by ang (X, Y, Z order) and draws a line from pos to pos + rotated vector.
 */
void func_8C057A20(Angle3 *ang, NJS_POINT3 *pos, Float len)
{
    NJS_VECTOR v;
    NJS_VECTOR d;

    v.x = 0.0f;
    v.y = 0.0f;
    v.z = len;
    njPushMatrix(NULL);
    njUnitMatrix(NULL);
    njRotateXYZ(NULL, ang->x, ang->y, ang->z);
    njCalcVector(NULL, &v, &d);
    d.x += pos->x;
    d.y += pos->y;
    d.z += pos->z;
    njPopMatrix(1);
    func_8C043A40(pos->x, pos->y, pos->z, d.x, d.y, d.z);
}
