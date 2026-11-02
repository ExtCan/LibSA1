/*
 * ADVERTISE 8C900440: draws the menu sky background (AVA_BACK, 8 tiles of the
 * sprite list D_8C912054) at depth z through the menu 2D library (chunk
 * 8C900440, its only function). Called by the screens' display functions.
 */
#include "common.h"
#include "sa1/sa1.h"

extern NJS_TEXLIST D_8C942548;  /* ava_back */
extern Sint16 D_8C912054[];     /* sprite list: count, then {tex, x, y} per sprite */

/* 1ST_READ menu 2D library */
void func_8C07DF30(Uint32 color);
void func_8C07EB60(Sint16 *list, Float x, Float y, Float z);

void func_8C900440(Float z)
{
    njSetTexture(&D_8C942548);
    func_8C07DF30(0xFFFFFFFF);
    func_8C07EB60(D_8C912054, 0.0f, 0.0f, z);
}
