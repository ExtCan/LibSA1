#include "common.h"
#include "ninja.h"
#include "task.h"
#include "sa1/sa1.h"
#include "sa1/pending.h"

/*
 * 0x6C-byte animal work stored in twp->value (allocated by func_8C015820);
 * 0x2C..0x3C as in code_8C017828.c / code_8C01593C.c.
 */
typedef struct UNK_8C016C60_work {
    Uint8 kind;                 /* 0x00 */
    Uint8 unk1;                 /* 0x01 */
    Uint8 unk2;                 /* 0x02 */
    Sint8 unk3;                 /* 0x03 */
    Float frame;                /* 0x04 */
    NJS_POINT3 spd;             /* 0x08 */
    NJS_POINT3 acc;             /* 0x14 */
    NJS_POINT3 scl;             /* 0x20 squash/stretch scale (func_8C01798A) */
    NJS_POINT3 sclspd;          /* 0x2C */
    Angle unk38;                /* 0x38 */
    Float unk3C;                /* 0x3C */
    Angle angx;                 /* 0x40 */
    Angle angz;                 /* 0x44 */
    NJS_POINT3 unk48;           /* 0x48 */
    NJS_POINT3 oldpos;          /* 0x54 */
    void (*exec)(struct UNK_8C016C60_work *wk, taskwk *twp); /* 0x60 */
} UNK_8C016C60_work;

void func_8C016A70(UNK_8C016C60_work *wk, taskwk *twp);
void func_8C01798A(NJS_POINT3 *p, Sint32 n);

#ifdef NON_MATCHING
/*
 * Land collision of an animal: six axis probes (func_8C029400) against every
 * gathered land object. Walls (+-x, +-z units whose normal is steeper than
 * njCos(0x11C7)) bounce spd and push acc along the wall normal; the +y/-y
 * units put the animal on the ground. With wtimer 0x40 the hits squash
 * wk->scl (func_8C01798A).
 *
 * The sign tests of the wall normals are written !(n < 0) / !(n > 0): SHC
 * compiled them as the negated FCMP/GT, which also takes a NaN normal.
 */
void func_8C016C60(UNK_8C016C60_work *wk, taskwk *twp)
{
    xyyzzxsdwstr info;
    NJS_VECTOR v;
    _OBJ_LANDCOLL *lc;
    Float gny;
    Sint32 old8;
    Sint32 hitw;
    Sint32 hitf;
    Float ny5;
    Float xmin;
    Float xmax;
    Float zmax;
    Float zmin;
    Sint32 i;
    Float nz4;
    Float nz5;
    Float nx1;
    Float nx0;
    Float ny0;
    Float ny1;
    Float ny4;
    Float ymax;

    lc = B_8C754EF4;
    hitw = 0;
    hitf = 0;
    ymax = -10000.0f;
    gny = 1.0f;
    old8 = twp->wtimer & 8;
    twp->wtimer &= 0xFFF7;
    twp->wtimer &= 0xFFEF;
    info.pos.x = twp->pos.x;
    info.pos.y = twp->pos.y;
    info.pos.z = twp->pos.z;
    func_8C0522C0(twp->pos.x, twp->pos.y, twp->pos.z, 10.0f);
    if (twp->wtimer & 0x40) {
        xmin = twp->pos.x - wk->scl.x * 8.0f;
        xmax = twp->pos.x + wk->scl.x * 8.0f;
        zmin = twp->pos.z - wk->scl.z * 8.0f;
        zmax = twp->pos.z + wk->scl.z * 8.0f;
    } else {
        xmin = twp->pos.x - 2.0f;
        xmax = twp->pos.x + 2.0f;
        zmin = twp->pos.z - 2.0f;
        zmax = twp->pos.z + 2.0f;
    }
    for (i = 0; i < B_8C754EF0; i++) {
        if (func_8C029400(&info, lc[i].pObject, lc[i].slAttribute) == 0) {
            continue;
        }
        v.x = 0.0f;
        v.y = 0.0f;
        v.z = 0.0f;
        if (lc[i].slAttribute & 2) {
            continue;
        }
        ny0 = info.hit[0].normal.y;
        nx0 = info.hit[0].normal.x;
        if (info.hit[0].findflag && njCos(0x11C7) > ny0) {
            if ((!(nx0 < 0.0f) && xmin + wk->spd.x < info.hit[0].onpos)
                || (nx0 < 0.0f && xmax + wk->spd.x > info.hit[0].onpos)) {
                if (wk->spd.x < 0.0f) {
                    wk->spd.x = -(wk->spd.x * 0.099999996f);
                }
                v.x += info.hit[0].normal.x;
                v.y += info.hit[0].normal.y;
                v.z += info.hit[0].normal.z;
                hitw = 1;
                wk->unk2++;
            }
        }
        ny1 = info.hit[1].normal.y;
        nx1 = info.hit[1].normal.x;
        if (info.hit[1].findflag && njCos(0x11C7) > ny1) {
            if ((!(nx1 > 0.0f) && xmax + wk->spd.x > info.hit[1].onpos)
                || (nx1 > 0.0f && xmin + wk->spd.x < info.hit[1].onpos)) {
                if (wk->spd.x > 0.0f) {
                    wk->spd.x = -(wk->spd.x * 0.099999996f);
                }
                v.x += info.hit[1].normal.x;
                v.y += info.hit[1].normal.y;
                v.z += info.hit[1].normal.z;
                hitw = 1;
                wk->unk2++;
            }
        }
        if (info.hit[2].findflag && info.hit[2].normal.y > 0.0f) {
            if (info.hit[2].onpos > ymax) {
                ymax = info.hit[2].onpos;
                gny = info.hit[2].normal.y;
            }
            twp->scl.z = info.hit[2].onpos;
            wk->angx = info.hit[2].angx;
            wk->angz = info.hit[2].angz;
            twp->wtimer |= 0x10;
            if (info.hit[2].onpos > twp->pos.y + wk->spd.y - 2.0f) {
                wk->spd.y = 0.0f;
                twp->pos.y = info.hit[2].onpos + 2.0f;
                twp->wtimer |= 8;
                hitf = 1;
            }
        } else if (info.hit[3].findflag && info.hit[3].normal.y > 0.0f
                   && twp->pos.y + 2.0f > info.hit[3].onpos) {
            twp->scl.z = info.hit[3].onpos;
            ymax = info.hit[3].onpos;
            gny = info.hit[3].normal.y;
            wk->angx = info.hit[3].angx;
            wk->angz = info.hit[3].angz;
            twp->wtimer |= 0x10;
            twp->wtimer |= 8;
            wk->spd.y = 0.0f;
            twp->pos.y = info.hit[3].onpos + 2.0f;
            hitf = 1;
        }
        ny4 = info.hit[4].normal.y;
        nz4 = info.hit[4].normal.z;
        if (info.hit[4].findflag && njCos(0x11C7) > ny4) {
            if ((!(nz4 < 0.0f) && zmin + wk->spd.z < info.hit[4].onpos)
                || (nz4 < 0.0f && zmax + wk->spd.z > info.hit[4].onpos)) {
                if (wk->spd.z < 0.0f) {
                    wk->spd.z = -(wk->spd.z * 0.5f);
                }
                v.x += info.hit[4].normal.x;
                v.y += info.hit[4].normal.y;
                v.z += info.hit[4].normal.z;
                wk->unk2++;
            }
        }
        ny5 = info.hit[5].normal.y;
        nz5 = info.hit[5].normal.z;
        if (info.hit[5].findflag && njCos(0x11C7) > ny5) {
            if ((!(nz5 > 0.0f) && zmax + wk->spd.z > info.hit[5].onpos)
                || (nz5 > 0.0f && zmin + wk->spd.z < info.hit[5].onpos)) {
                if (wk->spd.z > 0.0f) {
                    wk->spd.z = -(wk->spd.z * 0.5f);
                }
                v.x += info.hit[5].normal.x;
                v.y += info.hit[5].normal.y;
                v.z += info.hit[5].normal.z;
                wk->unk2++;
            }
        }
        if (twp->wtimer & 0x40) {
            /* asm 8C01720E: R4 = wk + 0x20 */
            func_8C01798A(&wk->scl, ((hitf & 1) << 1) | ((hitf & 1) << 2) | (hitw & 1));
        }
        wk->acc.x += v.x;
        wk->acc.y += v.y;
        wk->acc.z += v.z;
        /* FMAC 8C017254: rand() * (1/6553.6) + 0.0 */
        if ((Float)wk->unk2 > SA1_FMAC((Float)rand(), 1.52587890625e-4f, 0.0f)) {
            wk->unk2 = 0;
            if (twp->wtimer & 0x20) {
                twp->wtimer &= 0xFFDF;
            } else {
                twp->wtimer |= 0x20;
            }
        }
    }
    if (wk->exec != func_8C016A70 && !(twp->wtimer & 0x40) && (old8 || (twp->wtimer & 4))) {
        if ((ymax > -10000.0f && wk->oldpos.y - ymax > 15.0f) || ymax == -10000.0f
            || njCos(0x238E) > gny) {
            wk->unk2++;
            twp->pos.x = wk->oldpos.x;
            twp->pos.z = wk->oldpos.z;
            twp->wtimer |= 0x200;
            twp->wtimer |= 0x800;
        }
    }
}
#else
ASM_STUB(void func_8C016C60(UNK_8C016C60_work *wk, taskwk *twp))
#endif

ASM_STUB(void func_8C01798A(NJS_POINT3 *p, Sint32 n))
