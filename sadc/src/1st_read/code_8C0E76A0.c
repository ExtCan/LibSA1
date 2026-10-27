/*
 * 1ST_READ 8C0E76A0-8C0E7720 (chunk 8C0E6620; Big the Cat, fishing):
 *
 *  func_8C0E76A0  fishing line length: D_8C565F70 = distance between the
 *                 two points (player 0's position and the lure point,
 *                 see the callers func_8C0E7720 / func_8C0E78C0); the
 *                 reel value D_8C565F74 is clamped to
 *                 [D_8C565F70, D_8C565F70 + 5] and passed to
 *                 func_8C0E746C (line render state).
 */
#include "common.h"
#include "sa1/sa1.h"
#include "sa1/pending.h"

extern Float D_8C565F70;        /* line length (distance rod -> lure) */
extern Float D_8C565F74;        /* reel value, clamped to [len, len + 5] */

ASM_STUB(void func_8C0E746C(Float f))

void func_8C0E76A0(NJS_POINT3 *a, NJS_POINT3 *b)
{
    Float d;

    d = njDistanceP2P(a, b);
    D_8C565F70 = d;
    if (d > D_8C565F74) {
        D_8C565F74 = d;
    }
    if (D_8C565F70 + 5.0f <= D_8C565F74) {
        D_8C565F74 = D_8C565F70 + 5.0f;
    }
    func_8C0E746C(D_8C565F74);
}
