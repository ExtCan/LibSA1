#include "sa1/sa1.h"

/*
 * Motion playback helpers (1ST_READ 8C03FDC0-8C040A60 chunk: Ninja-style
 * keyframe motion evaluation).
 *  func_8C03FF90  interpolates an NJS_MKEY_A (Angle) key list at the current
 *                 frame B_8C88FE40.frame (linear, wraps to key 0 after the
 *                 last key; 1/d is computed as fsrra(d)^2).
 *  func_8C0400A0  node callback: njTranslate by the interpolated position key
 *                 of the current channel, or by obj->pos without one.
 *  func_8C0400F8  node callback: njScale (scale key or obj->scl).
 *  func_8C040150  node callback: njRotateXYZ (angle key or obj->ang).
 *  func_8C0401A8  node callback: njRotateZXY (angle key or obj->ang).
 * The callbacks are stored in the function pointers B_8C88FE24..B_8C88FE34
 * by func_8C03FE60 and called per node by 8C0406A0/8C0406E0, which set
 * B_8C88FE40.p/.nb to the node's NJS_MDATA3 (p[3], nb[3]) and reset .idx;
 * each callback consumes one key channel (idx++).
 * 8C040020 ("func_8C040020") is not a function: it is SHC's mid-file
 * literal pool (BRA over the pool, .ALIGN 32 fill).
 *
 * NON_MATCHING (all five): this object was built with different code
 * generation settings than the rest of the game (NOP after every
 * MOV Rm,R0, i.e. -extra bit 0x400; no FMAC anywhere in the object; less
 * aggressive scheduling), which the project's single flag set cannot
 * reproduce:
 *  - func_8C0400A0/func_8C0400F8: identical except two swapped independent
 *    instruction pairs (MOV R15,R6 before the nb load; MOV R15,R7 before the
 *    load of n);
 *  - func_8C040150/func_8C0401A8: the same two pairs plus the order of the
 *    three angle loads before the njRotate call;
 *  - func_8C03FF90: uses FSRRA (asmsh rejects the mnemonic SHC emits for
 *    _builtin_fsrra), and the original has no FMAC and a different FP
 *    register assignment / schedule.
 */

/* Ninja keyframe lists (16 bytes per key) */
typedef struct {
    Uint32 keyframe;            /* 0x00 */
    Float key[3];               /* 0x04 */
} NJS_MKEY_F;                   /* 0x10 */

typedef struct {
    Uint32 keyframe;            /* 0x00 */
    Angle key[3];               /* 0x04 */
} NJS_MKEY_A;                   /* 0x10 */

/*
 * Motion evaluation state: B_8C88FE40 (this file) and B_8C88FE5C, two 0x1C
 * instances (func_8C03FDC0 / func_8C03FE1A initialise them). Run-time state
 * written by the code itself, not ripped data.
 */
typedef struct {
    void *mdata;                /* current node's NJS_MDATAn (advanced per node) */
    void **p;                   /* key lists of the current node */
    Uint32 *nb;                 /* key counts of the current node */
    Float frame;
    Uint32 nbFrame;
    Sint32 idx;                 /* next key channel (p[idx], nb[idx]) */
    Sint32 unk18;               /* cleared by func_8C03FDC0 / func_8C03FE1A */
} MOTION_EVAL;

SA1_ASSERT_SIZE(NJS_MKEY_F, 0x10);
SA1_ASSERT_SIZE(NJS_MKEY_A, 0x10);
SA1_ASSERT_SIZE(MOTION_EVAL, 0x1C);
SA1_ASSERT_OFFSET(MOTION_EVAL, p, 0x04);
SA1_ASSERT_OFFSET(MOTION_EVAL, nb, 0x08);
SA1_ASSERT_OFFSET(MOTION_EVAL, frame, 0x0C);
SA1_ASSERT_OFFSET(MOTION_EVAL, idx, 0x14);

extern MOTION_EVAL B_8C88FE40;  /* .frame is the B_8C88FE4C pool symbol */

/* key search (binary search on keyframe, returns the key index in *n) and
 * NJS_MKEY_F interpolation; both still assembly */
ASM_STUB(void func_8C03FEB8(NJS_MKEY_F *key, Uint32 nbkeys, Uint32 *n))
ASM_STUB(void func_8C03FF2C(NJS_MKEY_F *key, Uint32 n, Uint32 nbkeys, Float *out))

#ifdef NON_MATCHING
void func_8C03FF90(NJS_MKEY_A *key, Uint32 n, Uint32 nbkeys, Angle *out)
{
    NJS_MKEY_A *k0;
    NJS_MKEY_A *k1;
    Sint32 d;
    Float t;
    Float r;

    k0 = &key[n];
    k1 = key;
    if (n + 1 < nbkeys) {
        k1 = k0 + 1;
    }
    t = B_8C88FE40.frame - (Float)(Sint32)k0->keyframe;
    d = (Sint32)(k1->keyframe - k0->keyframe);
    if (d > 0) {
        r = _builtin_fsrra((Float)d);
        t *= r * r;
    }
    out[0] = SA1_FTOI((Float)k0->key[0] * (1.0f - t) + (Float)k1->key[0] * t);
    out[1] = SA1_FTOI((Float)k0->key[1] * (1.0f - t) + (Float)k1->key[1] * t);
    out[2] = SA1_FTOI((Float)k0->key[2] * (1.0f - t) + (Float)k1->key[2] * t);
}
#else
ASM_STUB(void func_8C03FF90(NJS_MKEY_A *key, Uint32 n, Uint32 nbkeys, Angle *out))
#endif

#ifdef NON_MATCHING
void func_8C0400A0(NJS_OBJECT *obj)
{
    MOTION_EVAL *mp;
    Sint32 i;
    void *key;
    Uint32 nb;
    Float *v;
    Float buf[3];
    Uint32 n;
    Float x, y, z;

    mp = &B_8C88FE40;
    i = mp->idx;
    key = mp->p[i];
    v = obj->pos;
    if (key != NULL) {
        nb = mp->nb[i];
        func_8C03FEB8(key, nb, &n);
        func_8C03FF2C(key, n, nb, buf);
        v = buf;
    }
    /* separate loads (FMOV.S @R4+): v must not be live in the call
     * statement, or SHC keeps it in a callee-saved register */
    x = *v++;
    y = *v++;
    z = *v;
    njTranslate(NULL, x, y, z);
    mp->idx = i + 1;
}
#else
ASM_STUB(void func_8C0400A0(NJS_OBJECT *obj))
#endif

#ifdef NON_MATCHING
void func_8C0400F8(NJS_OBJECT *obj)
{
    MOTION_EVAL *mp;
    Sint32 i;
    void *key;
    Uint32 nb;
    Float *v;
    Float buf[3];
    Uint32 n;
    Float x, y, z;

    mp = &B_8C88FE40;
    i = mp->idx;
    key = mp->p[i];
    v = obj->scl;
    if (key != NULL) {
        nb = mp->nb[i];
        func_8C03FEB8(key, nb, &n);
        func_8C03FF2C(key, n, nb, buf);
        v = buf;
    }
    x = *v++;
    y = *v++;
    z = *v;
    njScale(NULL, x, y, z);
    mp->idx = i + 1;
}
#else
ASM_STUB(void func_8C0400F8(NJS_OBJECT *obj))
#endif

#ifdef NON_MATCHING
void func_8C040150(NJS_OBJECT *obj)
{
    MOTION_EVAL *mp;
    Sint32 i;
    void *key;
    Uint32 nb;
    Angle *v;
    Angle buf[3];
    Uint32 n;
    Angle x, y, z;

    mp = &B_8C88FE40;
    i = mp->idx;
    key = mp->p[i];
    v = obj->ang;
    if (key != NULL) {
        nb = mp->nb[i];
        func_8C03FEB8(key, nb, &n);
        func_8C03FF90(key, n, nb, buf);
        v = buf;
    }
    x = v[0];
    y = v[1];
    z = v[2];
    njRotateXYZ(NULL, x, y, z);
    mp->idx = i + 1;
}
#else
ASM_STUB(void func_8C040150(NJS_OBJECT *obj))
#endif

#ifdef NON_MATCHING
void func_8C0401A8(NJS_OBJECT *obj)
{
    MOTION_EVAL *mp;
    Sint32 i;
    void *key;
    Uint32 nb;
    Angle *v;
    Angle buf[3];
    Uint32 n;
    Angle x, y, z;

    mp = &B_8C88FE40;
    i = mp->idx;
    key = mp->p[i];
    v = obj->ang;
    if (key != NULL) {
        nb = mp->nb[i];
        func_8C03FEB8(key, nb, &n);
        func_8C03FF90(key, n, nb, buf);
        v = buf;
    }
    x = v[0];
    y = v[1];
    z = v[2];
    njRotateZXY(NULL, x, y, z);
    mp->idx = i + 1;
}
#else
ASM_STUB(void func_8C0401A8(NJS_OBJECT *obj))
#endif
