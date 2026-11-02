/*
 * ADVERTISE 8C9004A0-8C9004EE: small dialog / sphere helpers (chunk 8C9004A0,
 * functions 1-3 of 4; 8C9004EE is not executed by the character select).
 *
 *  func_8C9004A0  fade-in colour for t (1ST_READ 8C0A58FC, from transparent
 *                 white to white)
 *  func_8C9004A8  fade colour for a title quad: 0 from t = 0.75 on, else the
 *                 alpha (0.75 - t) * 255 over white
 *  func_8C9004E0  a sphere (8C0A5B60) at unit scale
 */
#include "common.h"
#include "sa1/sa1.h"

/* 1ST_READ dialog / sphere helpers */
Uint32 func_8C0A58FC(Uint32 c0, Uint32 c1, Float t);
void func_8C0A5B60(Uint32 color, Float x, Float y, Float z, Float sx, Float sy);

Uint32 func_8C9004A0(Float t)
{
    return func_8C0A58FC(0x00FFFFFF, 0xFFFFFFFF, t);
}

Uint32 func_8C9004A8(Float t)
{
    if (t < 0.75f) {
        return ((Uint32)(t * 0.0f + (0.75f - t) * 255.0f) << 24) | 0x00FFFFFF;
    }
    return 0;
}

void func_8C9004E0(Uint32 color, Float x, Float y, Float z)
{
    func_8C0A5B60(color, x, y, z, 1.0f, 1.0f);
}
