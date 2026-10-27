#include "sa1/sa1.h"

/*
 * Object tree walks with motion (1ST_READ 8C03FDC0-8C040A60 chunk, functions
 * 8C040840-8C040A40; see code_8C03FDC0.c, code_8C0406A0.c).
 *  func_8C040880  recursive walk of an NJS_OBJECT tree with node and shape
 *                 motion (njDrawShapeMotion-like)
 *  func_8C040942  draw an NJS_ACTION at a frame (node motion only)
 *  func_8C0409C0  draw an object with a node motion at a frame
 *  func_8C0409EE  draw an object with a node motion and a shape motion
 *
 * func_8C040840 and func_8C040980 are not functions: they are literal pools
 * that SHC dumped in the middle of the translation unit (".ALIGN 32;
 * BRA over; NOP; pool; .ALIGN 32"). The splicer keeps them as assembly.
 *
 * COMPILER: this translation unit was NOT built with the project default
 * (SHC 5.1r03 -cpu=sh4). All four functions match byte for byte with
 *     py -3.12 tools/shcdiff.py <func> <this file> --ver=5.0r32
 *         --flags="-cpu=sh3e -define=NON_MATCHING"
 * (also 5.0r26/r28/r31 with -cpu=sh3e; r31/r32 also with -cpu=sh2e).
 * Evidence: the SH-3 scheduler of SHC 5.0 hoists pool loads and constants
 * into the prologue/epilogue and is alignment sensitive (the twin node
 * functions 8C04057A/8C040596 at 2 mod 4 schedule ADD before the pool load,
 * 8C040588/8C0405A4 at 0 mod 4 the other way round, which only -cpu=sh3e
 * reproduces); 0x80 is a MOV.W pool constant (5.0 has no -extra bit 0x1000
 * MOV/SHLL8 constants); Float arguments live across calls go to the stack,
 * not FR15; and FSRRA appears as raw .DATA.W H'F47D/H'F57D words in this
 * chunk (8C03FF58, 8C040282), i.e. inline asm for an SH-4 instruction the
 * compiler target did not know. Until the build supports per-file compiler
 * settings, the functions that differ under 5.1r03/sh4 (scheduling only)
 * are guarded with NON_MATCHING; func_8C0409C0 matches under both.
 */

typedef void (*MOTION_FUNC)(NJS_OBJECT *object);

extern MOTION_FUNC B_8C88FE24;  /* node motion step (per NJS_MDATAn type) */
extern MOTION_FUNC B_8C88FE28;  /* shape motion step (per NJS_MDATAn type) */
extern MOTION_FUNC B_8C890074;  /* optional per-node callback */

/* hand-written model draw (8C037098) */
extern void func_8C037098(NJS_MODEL *model);

ASM_STUB(Sint32 func_8C03FDC0(NJS_MOTION *motion, Float frame))
ASM_STUB(Sint32 func_8C03FE1A(NJS_MOTION *motion, Float frame))
ASM_STUB(void func_8C03FE60(NJS_OBJECT *object))
ASM_STUB(void func_8C040784(NJS_OBJECT *object))

/*
 * Walk the object tree (siblings iteratively, children recursively). Each
 * node is pushed; evalflags 0x40 set: the static pos/ang/scl transform
 * (ang order ZXY when 0x20 is set), else the node motion step B_8C88FE24.
 * Then the optional callback B_8C890074; evalflags 0x80 clear: the shape
 * motion step B_8C88FE28, else the model is drawn unless hidden (0x08).
 * The x/y/z temporaries reproduce the post-increment loads of the original.
 */
#ifdef NON_MATCHING
void func_8C040880(NJS_OBJECT *object)
{
    Uint32 flags;
    void (*rotate)(NJS_MATRIX_PTR m, Angle angx, Angle angy, Angle angz);
    Float *v;
    Float x, y, z;
    Angle *a;
    Angle ax, ay, az;

    do {
        flags = object->evalflags;
        njPushMatrix(NULL);
        if (!(flags & 0x40)) {
            B_8C88FE24(object);
        } else {
            v = object->pos;
            x = *v++;
            y = *v++;
            z = *v;
            njTranslate(NULL, x, y, z);
            rotate = njRotateXYZ;
            if (flags & 0x20) {
                rotate = njRotateZXY;
            }
            a = object->ang;
            ax = a[0];
            ay = a[1];
            az = a[2];
            rotate(NULL, ax, ay, az);
            v = object->scl;
            x = *v++;
            y = *v++;
            z = *v;
            njScale(NULL, x, y, z);
        }
        if (B_8C890074 != NULL) {
            B_8C890074(object);
        }
        if (!(flags & 0x80)) {
            B_8C88FE28(object);
        } else if (object->model != NULL && !(flags & 8)) {
            func_8C037098(object->model);
        }
        if (object->child != NULL) {
            func_8C040880(object->child);
        }
        njPopMatrix(1);
        object = object->sibling;
    } while (object != NULL);
}
#else
ASM_STUB(void func_8C040880(NJS_OBJECT *object))
#endif

/* draw action->object with action->motion at frame (no-op past the end) */
#ifdef NON_MATCHING
void func_8C040942(NJS_ACTION *action, Float frame)
{
    NJS_OBJECT *object;
    NJS_MOTION *motion;

    motion = action->motion;
    object = action->object;
    if (func_8C03FE1A(motion, frame) == 0) {
        func_8C03FE60(object);
        func_8C605DAC();
        func_8C040784(object);
        func_8C605E0A();
    }
}
#else
ASM_STUB(void func_8C040942(NJS_ACTION *action, Float frame))
#endif

/* draw object with the node motion at frame (no-op past the end) */
void func_8C0409C0(NJS_OBJECT *object, NJS_MOTION *motion, Float frame)
{
    if (func_8C03FE1A(motion, frame) == 0) {
        func_8C03FE60(object);
        func_8C605DAC();
        func_8C040784(object);
        func_8C605E0A();
    }
}

/* draw object with a node motion and a shape motion at frame */
#ifdef NON_MATCHING
void func_8C0409EE(NJS_OBJECT *object, NJS_MOTION *motion, NJS_MOTION *shape, Float frame)
{
    if (func_8C03FE1A(motion, frame) == 0 && func_8C03FDC0(shape, frame) == 0) {
        func_8C03FE60(object);
        func_8C605DAC();
        func_8C040880(object);
        func_8C605E0A();
    }
}
#else
ASM_STUB(void func_8C0409EE(NJS_OBJECT *object, NJS_MOTION *motion, NJS_MOTION *shape, Float frame))
#endif
