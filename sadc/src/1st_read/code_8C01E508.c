/*
 * 1ST_READ 8C01E508 (chunk 8C01DFA0): the line test of the timed camera
 * blends (func_8C02535E, and the TIME blend 8C0252A2) against the land.
 * Staged by dc-fishcam for 0.9.6 (gsa1_090_bigfish.md review R3.3).
 *
 *  func_8C01E508  moves the camera from 'from' (the previous camera) towards
 *                 'to' (the blended camera) with the camera's sphere sweep
 *                 func_8C01C28E (func_8C0569F0, mask 0x400002, which pushes
 *                 the move vector back at a hit): radius 8 when 'to' is
 *                 farther from player 0 than 'from', else 4. On a hit the
 *                 camera ends at from + the pushed-back move, unless pad 0
 *                 holds L or R (0x10000 / 0x20000, the triggers' digital
 *                 bits: harness/src/t_sim.c btn_names), which moves it 10 %
 *                 of the way instead. Without a hit 'to' is kept.
 */
#include "sa1/sa1.h"

Bool func_8C01C28E(NJS_POINT3 *pos, NJS_POINT3 *v, Angle3 *ang, Float r); /* code_8C01ABDC.c */

#ifdef NON_MATCHING
/* NON_MATCHING: complete and faithful; register allocation only. The original keeps &v1 / &v2
 * in R12 / R10, 'hit' in R11 and d1 in FR12 (it saves R10-R14 and FR12); SHC here needs fewer
 * callee-saved registers (d1 in FR15), so the whole body is renamed. */
void func_8C01E508(NJS_POINT3 *from, NJS_POINT3 *to)
{
    NJS_VECTOR mv;
    NJS_VECTOR v2;
    NJS_VECTOR v1;
    Float d1;
    Sint32 hit;

    hit = 0;
    mv.x = to->x - from->x;
    mv.y = to->y - from->y;
    mv.z = to->z - from->z;
    v1.x = playertwp[0]->pos.x - from->x;
    v1.y = playertwp[0]->pos.y - from->y;
    v1.z = playertwp[0]->pos.z - from->z;
    v2.x = playertwp[0]->pos.x - to->x;
    v2.y = playertwp[0]->pos.y - to->y;
    v2.z = playertwp[0]->pos.z - to->z;
    d1 = njScalor(&v1);
    if (njScalor(&v2) > d1) {
        if (func_8C01C28E(from, &mv, NULL, 8.0f)) {
            if (B_8C754C08[0].on & 0x30000) {
                hit = 1;
            } else {
                to->x = from->x + mv.x;
                to->y = from->y + mv.y;
                to->z = from->z + mv.z;
            }
        }
    } else {
        if (func_8C01C28E(from, &mv, NULL, 4.0f)) {
            if (B_8C754C08[0].on & 0x30000) {
                hit = 1;
            } else {
                to->x = from->x + mv.x;
                to->y = from->y + mv.y;
                to->z = from->z + mv.z;
            }
        }
    }
    if (hit) {
        to->x = SA1_FMAC(0.099999996f, to->x - from->x, from->x);
        to->y = SA1_FMAC(0.099999996f, to->y - from->y, from->y);
        to->z = SA1_FMAC(0.099999996f, to->z - from->z, from->z);
    }
}
#else
ASM_STUB(void func_8C01E508(NJS_POINT3 *from, NJS_POINT3 *to))
#endif
