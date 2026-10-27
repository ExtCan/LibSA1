#include "sa1/pending.h"

/*
 * E-102 Gamma (1ST_READ chunk 8C0D26E0-8C0D2C00).
 *
 *  func_8C0D26E0  called by E102Main 8C0CBE00 with (twp, pwp, E-102 work):
 *                 advances the hover bob phase by 0x800 + 12.5 degrees *
 *                 clamp(|spd.x + spd.z| - 1, 0, 1), hover_y = sin(phase),
 *                 jet tilt jet_ang.z = clamp(spd.x * 10 degrees, +-25 deg).
 *  func_8C0D278A  called by E102Main with twp: ground probe under twp->pos
 *                 against every entry of the gathered land collision list
 *                 (B_8C754EF4) with the FD probe 8C02D060; dynamic entries
 *                 (pTask != NULL) are first culled by the XZ distance to the
 *                 object against its largest scale * model radius. Keeps the
 *                 hit closest in y to twp->pos.y (plus the owner's
 *                 forcewk.pos_spd.y for moving objects) and returns its
 *                 slAttribute (0: nothing); E102Main stores it at E-102
 *                 work +0x23C.
 *  func_8C0D28D8  exec of a spark task created by 8C0D2C00: a short random
 *                 line (D_8C422D1C, 2 points) thrown backwards (+-20
 *                 degrees around 0x8000 - ang.y, 30-60 degrees up), falling
 *                 for 60 frames with random yellow/white colours.
 */

/*
 * E-102 work (pwp->free.ptr[0]; E102Main 8C0CC0F2: CAlloc(1, 0x244)).
 * Only the head up to the fields used here (names as in code_8C0CFACE.c;
 * the real work is 0x244 bytes and is only reached through a pointer).
 */
typedef struct {
    Sint8 mode;                 /* 0x00 */
    Sint8 smode;                /* 0x01 */
    Sint16 unk02;               /* 0x02 */
    Sint32 unk04;               /* 0x04 */
    Sint16 timer08;             /* 0x08 */
    Sint16 timer0A;             /* 0x0A */
    Sint16 timer0C;             /* 0x0C */
    Sint16 unk0E;               /* 0x0E */
    Sint16 unk10;               /* 0x10 */
    Sint16 lockon_timer;        /* 0x12 */
    Sint16 unk14;               /* 0x14 */
    Sint16 flag;                /* 0x16 */
    Float hover_y;              /* 0x18 sin(hover_phase) (8C0D26E0) */
    Angle hover_phase;          /* 0x1C advanced by 8C0D26E0 */
    Angle3 jet_ang;             /* 0x20 jet_ang.z = tilt from spd.x (8C0D26E0) */
} E102WK;                       /* head only (0x244 bytes in all) */

SA1_ASSERT_OFFSET(E102WK, hover_y, 0x18);
SA1_ASSERT_OFFSET(E102WK, hover_phase, 0x1C);
SA1_ASSERT_OFFSET(E102WK, jet_ang, 0x20);

/*
 * Ground probe of 8C0D278A: the head of a zxsdwstr (pos + lower, 0x2C
 * bytes on the stack at sp+20 of a 64-byte frame); 8C02D060 only uses
 * these two members.
 */
typedef struct {
    NJS_POINT3 pos;             /* 0x00 */
    xssunit lower;              /* 0x0C */
} E102_GNDPROBE;                /* 0x2C */

SA1_ASSERT_SIZE(E102_GNDPROBE, 0x2C);
SA1_ASSERT_OFFSET(E102_GNDPROBE, lower, 0x0C);

/*
 * Lower probe against one FD land object (code_8C02D060.c). This caller,
 * like all the others, passes the entry's slAttribute in R6, which the
 * callee never reads (it takes the attribute from its FD object); the
 * canonical prototype (sa1/protos.h) keeps the argument.
 */
Sint32 func_8C02D060(zxsdwstr *zssp, NJS_OBJECT *object, Uint32 attr);

extern NJS_COLOR D_8C422D14[2];         /* line colours (0x80FFFF80 x 2) */
extern NJS_POINT3COL D_8C422D1C;        /* {D_8C422CFC (2 points, z = -0.4/+0.4), D_8C422D14, NULL, 2} */
extern NJS_POINT3 D_8C422D2C;           /* spark launch vector (x set here, y/z 0) */

#define CLAMP(x, lo, hi) ((x) < (lo) ? (lo) : ((x) > (hi) ? (hi) : (x)))

/*
 * Random launch angles of func_8C0D28D8. The original rotation macro
 * "a ? njRotate(NULL, a) : (void)0" evaluates its argument twice (three
 * times when nested), and every evaluation calls rand() again.
 */
#define SPARK_YAW(twp)   (0x8000 - (twp)->ang.y + SA1_FTOI((20.0f - (Float)rand() * 0.001220703125f) * 182.044437f))  /* 0x8000 - ang.y +-20 deg */
#define SPARK_PITCH(twp) (SA1_FTOI(SA1_FMAC((Float)rand(), 0.00091552734375f, 30.0f) * 182.044437f) - (twp)->ang.z)   /* 30..60 deg - ang.z */

void func_8C0D26E0(taskwk *twp, playerwk *pwp, E102WK *ewk)
{
    Float rate;
    Angle ang;

    rate = CLAMP(_builtin_fabsf(pwp->spd.x + pwp->spd.z) - 1.0f, 0.0f, 1.0f);
    ewk->hover_phase += SA1_FTOI(rate * 2275.5555f) + 0x800;
    ewk->hover_y = njSin(ewk->hover_phase) * 1.0f;
    ang = SA1_FTOI(pwp->spd.x * 1820.44437f);
    ewk->jet_ang.z = CLAMP(ang, -0x11C7, 0x11C7);
}


/*
 * NON_MATCHING: only the register allocation differs (13 instructions): the
 * per-iteration &lc[i] temporary and the strength-reduced &lc[i].pTask
 * pointer of the second pTask test trade R8/R9. Everything else, including
 * the lc/attr spill slots, is identical. Tried without success (~500
 * variants): statement order and compound-assignment spellings, != NULL
 * tests, comparison orientation, continue/nested control flow, ++i, block
 * scopes, initialisers, declaration order, Sint32/Uint32 attr/return,
 * an explicit &lc[i] pointer, no obj local, ternary max. Second session
 * (~700 more, incl. a random search over combinations of equivalent
 * spellings): obj loaded inside the pTask block, best = best + x, an
 * if/else for best, B_8C754EF4[i] without lc, &lc[i] in a user pointer for
 * the first block only, "register" (any variable: +10 insns). The
 * allocation of the four loop pointers reorders with almost every change
 * (best = best + x puts &lc[i] in R11; the if/else form gets &lc[i] in R8
 * as in the original but rotates the three induction pointers); none gave
 * the original attrSR R11 / fwpSR R10 / testSR R9 / &lc[i] R8.
 */
#ifdef NON_MATCHING
Uint32 func_8C0D278A(taskwk *twp)
{
    E102_GNDPROBE probe;
    NJS_VECTOR v;
    Uint32 attr;
    _OBJ_LANDCOLL *lc;
    NJS_OBJECT *obj;
    Float dist;
    Float scl;
    Float best;
    Float d0;
    Float d1;
    Uint16 i;

    attr = 0;
    probe.pos = twp->pos;
    lc = B_8C754EF4;
    best = -100000.0f;
    for (i = 0; i < B_8C754EF0; i++) {
        obj = lc[i].pObject;
        if (lc[i].pTask) {
            v.x = obj->pos[0] - twp->pos.x;
            v.y = 0.0f;
            v.z = obj->pos[2] - twp->pos.z;
            dist = njScalor(&v);
            scl = obj->scl[0];
            if (scl < obj->scl[1]) {
                scl = obj->scl[1];
            }
            if (obj->scl[2] > scl) {
                scl = obj->scl[2];
            }
            if (dist > scl * obj->model->r) {
                continue;
            }
        }
        if (func_8C02D060((zxsdwstr *)&probe, lc[i].pObject, lc[i].slAttribute)) {
            d0 = _builtin_fabsf(twp->pos.y - best);
            d1 = _builtin_fabsf(twp->pos.y - probe.lower.onpos);
            if (d0 > d1) {
                best = probe.lower.onpos;
                if (lc[i].pTask) {
                    best += FWP(lc[i].pTask)->pos_spd.y;
                }
                attr = lc[i].slAttribute;
            }
        }
    }
    return attr;
}
#else
ASM_STUB(Uint32 func_8C0D278A(taskwk *twp))
#endif

/*
 * Matching notes: the rotations use the original "a ? njRotate(NULL, a) :
 * (void)0" form (if statements or a volatile local trade FR13/FR14 and
 * the call-address registers); the timer is decremented and tested in two
 * statements ("--twp->timer.l <= 0" drops a register copy and swaps
 * twp/mwp between R12 and R13).
 */
void func_8C0D28D8(task *tp)
{
    taskwk *twp;
    motionwk *mwp;
    NJS_VECTOR v;
    Sint32 i;

    twp = tp->twp;
    mwp = tp->mwp;
    switch (twp->mode) {
    case 0:
        njPushMatrix(D_8C67C640);
        D_8C422D2C.x = SA1_FMAC((Float)rand(), 0.00006103515625f, 1.0f);   /* 1..3 */
        SPARK_YAW(twp) ? (SPARK_YAW(twp) ? njRotateY(NULL, SPARK_YAW(twp)) : (void)0) : (void)0;
        SPARK_PITCH(twp) ? njRotateZ(NULL, SPARK_PITCH(twp)) : (void)0;
        njCalcVector(NULL, &D_8C422D2C, &v);
        njAddVector(&mwp->spd, &v);
        njPopMatrix(1);
        twp->ang.x = SA1_FTOI((Float)rand() * 1.99999991f);        /* 0x3FFFFFFF: 0..0xFFFF */
        twp->ang.y = SA1_FTOI((Float)rand() * 1.99999991f);
        twp->timer.l = 60;
        twp->mode = 1;
        break;
    case 1:
        twp->timer.l--;
        if (twp->timer.l <= 0) {
            DestroyTask(tp);
            return;
        }
        twp->ang.x += 0x2000;
        twp->ang.y += 0x2000;
        mwp->spd.y -= 0.163333315f;
        njAddVector(&twp->pos, &mwp->spd);
        break;
    }
    njPushMatrix(NULL);
    for (i = 0; i < 2; i++) {
        D_8C422D14[i].argb.r = SA1_FTOI(SA1_FMAC((Float)rand(), 0.0048828125f, 96.0f));  /* 96..255 */
        D_8C422D14[i].argb.g = SA1_FTOI(SA1_FMAC((Float)rand(), 0.0048828125f, 96.0f));
        D_8C422D14[i].argb.b = SA1_FTOI((Float)rand() * 0.00390625f);                  /* 0..127 */
    }
    njTranslate(NULL, twp->pos.x, twp->pos.y, twp->pos.z);
    twp->ang.x ? njRotateX(NULL, twp->ang.x) : (void)0;
    twp->ang.y ? njRotateY(NULL, twp->ang.y) : (void)0;
    njColorBlendingMode(0, 10);
    njColorBlendingMode(1, 10);
    njDrawLine3D(&D_8C422D1C, 1, 64);
    njPopMatrix(1);
}
