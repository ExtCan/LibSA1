#include "common.h"
#include "sa1/sa1.h"
#include "sa1/pending.h"

/*
 * 1ST_READ 8C0BDC80-8C0BE160 (chunk 8C0BDC80, functions 1-5 of 5): Tails.
 *  func_8C0BDC80  bend Tails' two tail models: advances the bend phase
 *                 B_8C7A2188 by 0x500-0x800 (by phase quarter), sets the
 *                 bend amount B_8C7A218C to 0.4, rebuilds the points of
 *                 D_8C273E3C and D_8C274448 from the straight tail
 *                 D_8C274A54 (second tail 0x5000 ahead in phase),
 *                 recomputes their normals and puts them in the tail
 *                 morph nodes D_8C25824C / D_8C2588D8 (called by Tails'
 *                 display func_8C0BA820).
 *  func_8C0BDD30  reset: tail spin angles to 0 and the original models
 *                 back into the morph nodes (called by Tails' dest).
 *  func_8C0BDD48  bend n points: x in [0, len1) is wound on an arc one
 *                 way, x >= len1 the other way (sign by phase half),
 *                 then x is scaled by 1.15.
 *                 NON_MATCHING: complete and functionally faithful (6 FMAC =
 *                 6 SA1_FMAC/FMAC_ACC); 12 of 334 instructions differ, all
 *                 register-allocation ties: the njSin/njCos addresses get
 *                 R12/R13 swapped (pool order follows) and the hoisted
 *                 len1 + start is FADD FR3,FR1 instead of FADD FR1,FR3.
 *                 Moving "start = 0.0f" before "r = f" fixed a third tie
 *                 (R4/R5 of the induction-variable inits); declaration
 *                 order, prologue statement order, operand order, loop form,
 *                 call/temporary forms and parameter copies were searched
 *                 without effect on the remaining two.
 *  func_8C0BE070  create Tails' head/eye look task (exec 8C0BE1CE;
 *  func_8C0BE0CE  and 8C0BE558), lookwk in tp->mwp, objs = head, eyeL,
 *                 eyeR (copies of Sonic's func_8C0B27EE).
 */

extern Sint32 B_8C7A2188;           /* tail bend phase (bits 8-15 angle, 16-17 quarter) */
extern Float B_8C7A218C;            /* tail bend amount */
extern NJS_MODEL D_8C274A54[];        /* straight tail (source points) */
extern NJS_MODEL D_8C273E3C[];        /* tail A model (bent points written here) */
extern NJS_MODEL D_8C274448[];        /* tail B model */
extern NJS_MODEL D_8C258224[];        /* tail A original model (object D_8C25824C) */
extern NJS_MODEL D_8C2588B0[];        /* tail B original model (object D_8C2588D8) */
extern NJS_OBJECT D_8C258974[];       /* tail A (spun around ang[0]) */
extern NJS_OBJECT D_8C2582E8[];       /* tail B */
extern NJS_OBJECT D_8C25824C[];       /* tail A morph node */
extern NJS_OBJECT D_8C2588D8[];       /* tail B morph node */

void func_8C056000(NJS_MODEL *model);   /* polygon normals (code_8C055ECE.c) */
void func_8C0BE1CE(task *tp);           /* look task exec (chunk 8C0BE160) */
void func_8C0BE558(task *tp);           /* look task exec (chunk 8C0BE160) */

void func_8C0BDD48(NJS_POINT3 *src, NJS_POINT3 *dst, Float f, Sint32 ang, Uint32 n);

/* "acc += a * b" compiled to FMAC: fused on PC (SA1_FMAC); spelled as
 * acc = a * b + acc it changes the SHC output (as in code_8C0DB230.c) */
#if defined(TARGET_PC)
#define FMAC_ACC(acc, a, b) ((acc) = SA1_FMAC((a), (b), (acc)))
#else
#define FMAC_ACC(acc, a, b) ((acc) += (a) * (b))
#endif

void func_8C0BDC80(void)
{
    NJS_POINT3 *src;
    NJS_POINT3 *dst;
    Uint32 n;
    NJS_MODEL *org;
    NJS_MODEL *mdl;

    org = D_8C274A54;
    mdl = D_8C273E3C;
    n = org->nbPoint;
    src = org->points;
    dst = mdl->points;
    switch (B_8C7A2188 & 0x30000) {
    case 0:
        B_8C7A2188 += 0x700;
        B_8C7A218C = 0.39999998f;
        break;
    case 0x10000:
        B_8C7A2188 += 0x500;
        B_8C7A218C = 0.39999998f;
        break;
    case 0x20000:
        B_8C7A2188 += 0x800;
        B_8C7A218C = 0.39999998f;
        break;
    case 0x30000:
        B_8C7A2188 += 0x600;
        B_8C7A218C = 0.39999998f;
        break;
    }
    func_8C0BDD48(src, dst, B_8C7A218C, B_8C7A2188 & 0xFF00, n);
    func_8C056000(mdl);
    func_8C05646C(mdl);
    org = D_8C274A54;
    mdl = D_8C274448;
    n = org->nbPoint;
    src = org->points;
    dst = mdl->points;
    func_8C0BDD48(src, dst, B_8C7A218C, (B_8C7A2188 + 0x5000) & 0xFF00, n);
    func_8C056000(mdl);
    func_8C05646C(mdl);
    D_8C25824C->model = D_8C273E3C;
    D_8C2588D8->model = D_8C274448;
}

void func_8C0BDD30(void)
{
    D_8C258974->ang[0] = 0;
    D_8C2582E8->ang[0] = 0;
    D_8C25824C->model = D_8C258224;
    D_8C2588D8->model = D_8C2588B0;
}

#ifdef NON_MATCHING
void func_8C0BDD48(NJS_POINT3 *src, NJS_POINT3 *dst, Float f, Sint32 ang, Uint32 n)
{
    Float len1;
    Sint32 t;
    Uint32 i;
    Float ox;
    Float start;
    Float a;
    Float oy;
    Float bf;
    Float b;
    Float af;
    Float s;
    Float r;
    Float c;
    Float len2;

    start = 0.0f;
    r = f;
    if (ang < 0x8000) {
        a = (Float)ang * 0.000244140625f;
    } else {
        a = (Float)(ang - 0x8000) * 0.000244140625f;
    }
    b = 8.0f - a;
    len1 = SA1_FMAC(a * r, 1.57079592f, a * (1.0f - r));
    len2 = SA1_FMAC(b * r, 1.57079592f, b * (1.0f - r));
    af = a * r;
    bf = b * r;
    for (i = 0; i < n; i++) {
        if (src[i].x < 0.0f) {
            ox = src[i].x;
            oy = 0.0f;
        } else if (src[i].x < len1 + start) {
            if (len1 != 0.0f) {
                t = SA1_FTOI((src[i].x - start) / len1 * 32768.0f);
                s = njSin(t);
                c = njCos(t);
                oy = af * s * 0.5f;
                ox = SA1_FMAC(0.5f, (1.0f - c) * a, start);
                t = (t + 0x4000) >> 1;
                s = njSin(t);
                c = njCos(t);
                if (ang < 0x8000) {
                    ox -= src[i].y * c * s * 2.0f;
                } else {
                    oy = -oy;
                    FMAC_ACC(ox, src[i].y * c * s, 2.0f);
                }
                oy -= src[i].y * c * c * 2.0f;
            } else {
                ox = src[i].x;
                oy = 0.0f;
            }
        } else {
            if (len2 != 0.0f) {
                t = SA1_FTOI((src[i].x - len1 - start) / len2 * 32768.0f);
                t = 0x10000 - t;
                s = njSin(t);
                c = njCos(t);
                oy = bf * s * 0.5f;
                ox = SA1_FMAC(0.5f, (1.0f - c) * b, start) + a;
                t = (0x4000 - t) >> 1;
                s = njSin(t);
                c = njCos(t);
                if (ang < 0x8000) {
                    FMAC_ACC(ox, src[i].y * c * s, 2.0f);
                } else {
                    oy = -oy;
                    ox -= src[i].y * c * s * 2.0f;
                }
                oy -= src[i].y * c * c * 2.0f;
            } else {
                ox = src[i].x - len1 + a;
                oy = 0.0f;
            }
        }
        dst[i].x = ox * 1.15f;
        dst[i].y = src[i].y + oy;
        dst[i].z = src[i].z;
    }
}
#else
ASM_STUB(void func_8C0BDD48(NJS_POINT3 *src, NJS_POINT3 *dst, Float f, Sint32 ang, Uint32 n))
#endif

Bool func_8C0BE070(Sint8 pno, NJS_OBJECT **objs)
{
    task *tp;
    lookwk *lwp;

    if ((tp = CreateElementalTask(2, 2, func_8C0BE1CE)) != NULL) {
        if ((lwp = MAlloc(sizeof(lookwk))) == NULL) {
            FreeTask(tp);
            return FALSE;
        }
        tp->mwp = (motionwk *)lwp;
        tp->twp->counter.b[0] = pno;
        lwp->head = objs[0];
        lwp->eyeL = objs[1];
        lwp->eyeR = objs[2];
        goto ok;
    }
    return FALSE;
ok:
    PC_ONLY(return TRUE;)
    ;
}

Bool func_8C0BE0CE(Sint8 pno, NJS_OBJECT **objs)
{
    task *tp;
    lookwk *lwp;

    if ((tp = CreateElementalTask(2, 2, func_8C0BE558)) != NULL) {
        if ((lwp = MAlloc(sizeof(lookwk))) == NULL) {
            FreeTask(tp);
            return FALSE;
        }
        tp->mwp = (motionwk *)lwp;
        tp->twp->counter.b[0] = pno;
        lwp->head = objs[0];
        lwp->eyeL = objs[1];
        lwp->eyeR = objs[2];
        goto ok;
    }
    return FALSE;
ok:
    PC_ONLY(return TRUE;)
    ;
}
