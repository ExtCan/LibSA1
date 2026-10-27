#include "common.h"
#include "ninja.h"
#include "task.h"

/* 0x6C-byte work stored in twp->value (allocated by func_8C015820) */
typedef struct {
    Uint8 kind;                 /* 0x00 */
    Uint8 unk1;                 /* 0x01 */
    Uint8 unk2;                 /* 0x02 */
    Sint8 unk3;                 /* 0x03 */
    Float frame;                /* 0x04 */
    NJS_POINT3 spd;             /* 0x08 */
    NJS_POINT3 acc;             /* 0x14 */
    NJS_POINT3 scl;             /* 0x20 */
    NJS_POINT3 sclspd;          /* 0x2C */
} UNK_8C017828_work;

/* Springs the three scale axes towards the average of the other two,
 * damps the scale velocity and clamps the scale to [0.5, 1.5]. */
void func_8C017828(UNK_8C017828_work *wk, taskwk *twp) {
    NJS_VECTOR unused;
    NJS_VECTOR v;
    NJS_VECTOR avg;

    (void)&v; /* address-taken: keeps SHC from caching &v in a register */
    (void)&avg;
    avg.x = (wk->scl.y + wk->scl.z) * 0.5f;
    avg.y = (wk->scl.z + wk->scl.x) * 0.5f;
    avg.z = (wk->scl.x + wk->scl.y) * 0.5f;
    v.x = avg.x - wk->scl.x;
    v.y = avg.y - wk->scl.y;
    v.z = avg.z - wk->scl.z;
    v.x *= 0.05f;
    v.y *= 0.05f;
    v.z *= 0.05f;
    wk->sclspd.x += v.x;
    wk->sclspd.y += v.y;
    wk->sclspd.z += v.z;
    v.x = wk->sclspd.x * 0.167935f;
    v.y = wk->sclspd.y * 0.167935f;
    v.z = wk->sclspd.z * 0.167935f;
    wk->sclspd.x -= v.x;
    wk->sclspd.y -= v.y;
    wk->sclspd.z -= v.z;
    wk->scl.x += wk->sclspd.x;
    wk->scl.y += wk->sclspd.y;
    wk->scl.z += wk->sclspd.z;
    if (wk->scl.x > 1.5f) {
        wk->scl.x = 1.5f;
    }
    if (wk->scl.y > 1.5f) {
        wk->scl.y = 1.5f;
    }
    if (wk->scl.z > 1.5f) {
        wk->scl.z = 1.5f;
    }
    if (wk->scl.x < 0.5f) {
        wk->scl.x = 0.5f;
    }
    if (wk->scl.y < 0.5f) {
        wk->scl.y = 0.5f;
    }
    if (wk->scl.z < 0.5f) {
        wk->scl.z = 0.5f;
    }
}

void func_8C01798A(NJS_POINT3 *p, Sint32 n) {
    if ((n & 1) && (n & 4)) {
        p->x -= 0.15f;
        p->y += 0.3f;
        p->z -= 0.15f;
    } else if (n & 1) {
        p->x -= 0.3f;
        p->y += 0.15f;
        p->z += 0.15f;
    } else if (n & 4) {
        p->x += 0.15f;
        p->y += 0.15f;
        p->z -= 0.3f;
    }
    if (n & 2) {
        p->x += 0.15f;
        p->y -= 0.3f;
        p->z += 0.15f;
    }
}
