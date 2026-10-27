/*
 * Input module: analog stick conversion (1ST_READ 8C05117E-8C051760).
 * For every player slot, turns the stick of the peripheral B_8C754DA8[i]
 * (slots 0 and 1 only) into a camera-relative angle and a stroke: a dead
 * zone of 3072 (stick << 8) is removed, the angle is atan2f(y, x) in BAMS
 * minus the camera yaw (kept to 16 bits), the stroke is r^1.5 / 29696^3
 * with r = x*x + y*y (not clamped). B_8C754E0C[] always gets the result;
 * B_8C754DCC[] (read by GetAnalog) only when pad reading (D_8C19E3A8) and
 * the port (D_8C19E3A9[i]) are enabled, otherwise it is zeroed.
 * SHC unrolls the loop 4x; x/y/r of each copy live in their own stack slot.
 */
#include "common.h"
#include "sa1/input.h"
#include "sa1/engine.h"
#include "sa1/ninja_funcs.h"

extern Uint8 D_8C19E3A8;                /* pad reading enabled (code_8C0509A0.c) */
extern Uint8 D_8C19E3A9[];              /* per-port enable (2 used; see below) */

void func_8C05117E(void) {
    Sint32 i;
    Float x;
    Float y;
    Float r;
    Float stroke;
    Angle ang;

    for (i = 0; i < 8; i++) {
        if (i < 2) {
            x = (Float)(B_8C754DA8[i]->x1 << 8);
            y = (Float)(B_8C754DA8[i]->y1 << 8);
        } else {
            x = y = 0.0f;
        }
        if (x > 3072.0f || x < -3072.0f || y > 3072.0f || y < -3072.0f) {
            if (x >= 0.0f) {
                x -= 3072.0f;
            } else {
                x += 3072.0f;
            }
            if (y >= 0.0f) {
                y -= 3072.0f;
            } else {
                y += 3072.0f;
            }
            if (camera_twp) {
                ang = SA1_FTOI(atan2f(y, x) * 10430.381f) - camera_twp->ang.y;
            } else {
                ang = SA1_FTOI(atan2f(y, x) * 10430.381f);
            }
            r = SA1_FMAC(x, x, y * y);
            stroke = r * njSqrt(r) * 3.8186171e-14f;    /* 0x292BF9A8 = 1 / 29696^3 */
        } else {
            ang = 0;
            stroke = 0.0f;
        }
        ang = (Uint16)ang;
        B_8C754E0C[i].ang = ang;
        B_8C754E0C[i].stroke = stroke;
        /*
         * The original reads D_8C19E3A9[i] for i = 2..7 too (past the 2-byte
         * array, into D_8C19E3AC); both branches store 0/0.0f there, so the
         * PC build skips that out-of-bounds read without changing the result.
         */
        if (D_8C19E3A8 == 1 && PC_ONLY(i < 2 &&) D_8C19E3A9[i] == 1) {
            B_8C754DCC[i].ang = ang;
            B_8C754DCC[i].stroke = stroke;
        } else {
            B_8C754DCC[i].ang = 0;
            B_8C754DCC[i].stroke = 0.0f;
        }
    }
}
