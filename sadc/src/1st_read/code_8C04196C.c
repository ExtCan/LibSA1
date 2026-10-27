#include "sa1/sa1.h"

/*
 * 1ST_READ 8C04196C-8C041E3E (part of the 8C040A60 chunk): drawing with a
 * linked (blended) motion, the NJS_MOTION_LINK counterpart of
 * code_8C040840.c (see code_8C040A60.c, code_8C040CE0.c). Every node is
 * evaluated for both motions of the link (B_8C88FF38 = 0, 1) and the two
 * results are blended with the rate B_8C88FF3C (func_8C0417C8 for the node
 * transform, func_8C04196C for the shape keys):
 *  func_8C04196C  shape step: blend the vertices/normals of both shape
 *                 motions into the scratch buffer, draw the model, restore
 *                 its arrays
 *  func_8C041A2E  recursive njDrawMotionLink-style walk of an NJS_OBJECT tree
 *  func_8C041B0E  the same with shape motion (njDrawShapeMotionLink-style)
 *  func_8C041C92  draw an NJS_ACTION_LINK at a blend rate (rate <= 1)
 *  func_8C041D68  draw an object with a motion link at a blend rate
 *  func_8C041DC6  draw an object with a motion link and a shape motion link
 *
 * COMPILER: this code was built with SHC 5.0 Release 10, not 5.1r03 (very
 * likely the whole motion module 8C03FDC0-8C041E40: it is the only game code
 * where body instructions are scheduled above the stack-frame ADD, and it
 * loads 16-bit constants with MOV.W). With 5.0r10 -cpu=sh4 every function
 * below is byte-identical to the original; with 5.1r03 (any -extra, -speed
 * or -cpu setting) they differ in instruction scheduling and literal-pool
 * placement only. 5.0r10 has no -fpu/-round options, so the check is (all
 * six: MATCH, also with the ASM_STUBs moved to the end of the file):
 *   py -3.12 -c "import sys,os;sys.path.insert(0,'tools');import shcdiff as s;
 *     c=s.compiler_cfg();c.update(version='5.0r10',cflags='-cpu=sh4
 *     -endian=little -division=cpu -optimize=1 -speed -macsave=0
 *     -define=NON_MATCHING');o=s.Original('1ST_READ');p='build/diff/r10.src';
 *     s.compile_c(os.path.abspath('src/1st_read/code_8C04196C.c'),p,c);
 *     l=s.Listing(p,c['version']);[print(f,s.compare(o,l,f)[0]) for f in
 *     sys.argv[1:]]" func_8C04196C func_8C041A2E func_8C041B0E func_8C041C92
 *     func_8C041D68 func_8C041DC6
 * Everything is kept under NON_MATCHING until the build supports per-file
 * compiler versions.
 *
 * Source forms the match depends on: the original reads pos/ang/scl through
 * explicit pointer walks (FMOV.S @Rn+), and func_8C04196C indexes the scratch
 * buffer as Float (n * 12 and n * 15 Floats = 4n and 5n NJS_POINT3s) with a
 * separate loop counter. NJS_POINT3 is three packed Floats on every target.
 */

typedef void (*MOTION_FUNC)(NJS_OBJECT *object);

extern Uint16 B_8C88FF38;           /* motion of the link being evaluated (0, 1) */
extern Float B_8C88FF3C;            /* blend rate */
extern Uint16 B_8C88FF40;           /* translation key flags (bit n + 1: motion n) */
extern Uint16 B_8C88FF42;           /* rotation key flags */
extern Uint16 B_8C88FF44;           /* scale key flags */
extern Uint16 B_8C88FF46;           /* vertex shape key flags */
extern Uint16 B_8C88FF48;           /* normal shape key flags */
extern MOTION_FUNC B_8C88FE80[2];   /* node motion step of each motion */
extern MOTION_FUNC B_8C88FE88[2];   /* shape motion step of each motion */
extern NJS_MATRIX_PTR B_8C88FE58;   /* matrix for the static (NJD_EVAL_SKIP) transform */
extern NJS_POINT3 *B_8C88FFFC[2];   /* interpolated vertices of each shape motion */
extern NJS_VECTOR *B_8C89000C[2];   /* interpolated normals of each shape motion */
extern NJS_POINT3 *B_8C89002C;      /* shape scratch buffer */
extern MOTION_FUNC B_8C890074;      /* optional per-node callback */

/* hand-written basic model draw (8C03700C) */
extern void func_8C03700C(NJS_MODEL *model);

/* code_8C040A60.c (same module, but out of BSR range: called with JSR) */
Sint32 func_8C040AF4(NJS_MOTION_LINK *link, Sint32 n);  /* shape motion n set-up, 1 = past the end */
Sint32 func_8C040B90(NJS_MOTION_LINK *link, Sint32 n);  /* node motion n set-up, 1 = past the end */
void func_8C040C02(NJS_OBJECT *object, Sint32 n);       /* node motion n handlers */

/* same-file callees (BSR), still assembly */
/* out = normalize(a * (1 - t) + b * t), (0, 0, 1) when too short */
ASM_STUB(void func_8C041040(NJS_VECTOR *a, NJS_VECTOR *b, NJS_VECTOR *out, Float t))
/* out = a * (1 - t) + b * t */
ASM_STUB(void func_8C0410A4(NJS_POINT3 *a, NJS_POINT3 *b, NJS_POINT3 *out, Float t))
/* blend the translation/rotation/scale results of both motions (rate
 * B_8C88FF3C) and apply them with njTranslate / D_8C19AD44 / njScale */
ASM_STUB(void func_8C0417C8(void))

#ifdef NON_MATCHING
void func_8C04196C(NJS_OBJECT *object)
{
    NJS_MODEL *model;
    NJS_POINT3 *points;
    NJS_VECTOR *normals;
    NJS_POINT3 *p0;
    NJS_POINT3 *p1;
    NJS_POINT3 *pdst;
    NJS_VECTOR *n0;
    NJS_VECTOR *n1;
    NJS_VECTOR *ndst;
    Sint32 i;
    Sint32 n;

    model = object->model;
    if (model == NULL || (object->evalflags & 8)) {
        return;
    }
    points = model->points;
    normals = model->normals;
    if (B_8C88FF46 > 0 && B_8C88FF48 > 0) {
        p0 = B_8C88FFFC[0];
        p1 = B_8C88FFFC[1];
        n = model->nbPoint;
        pdst = (NJS_POINT3 *)&((Float *)B_8C89002C)[n * 12];      /* buf[4n] */
        i = n;
        do {
            func_8C0410A4(p0, p1, pdst, B_8C88FF3C);
            p0++;
            p1++;
            pdst++;
        } while (--i);
        n0 = B_8C89000C[0];
        n1 = B_8C89000C[1];
        n = model->nbPoint;
        ndst = (NJS_VECTOR *)&((Float *)B_8C89002C)[n * 15];      /* buf[5n] */
        i = n;
        do {
            func_8C041040(n0, n1, ndst, B_8C88FF3C);
            n0++;
            n1++;
            ndst++;
        } while (--i);
        /* the original stores the END of each blended array (pdst/ndst after
         * the loops), so the model is drawn from buf[5n] (the blended
         * normals) and buf[6n]; kept as is */
        model->points = pdst;
        model->normals = ndst;
    }
    func_8C03700C(model);
    model->points = points;
    model->normals = normals;
}
#else
ASM_STUB(void func_8C04196C(NJS_OBJECT *object))
#endif

#ifdef NON_MATCHING
void func_8C041A2E(NJS_OBJECT *object)
{
    Uint32 flags;
    NJS_MATRIX_PTR m;
    void (*rotate)(NJS_MATRIX_PTR m, Angle angx, Angle angy, Angle angz);
    Float *v;
    Float x, y, z;
    Angle *a;
    Angle ax, ay, az;

    do {
        flags = object->evalflags;
        njPushMatrix(NULL);
        if (!(flags & 0x40)) {
            B_8C88FF40 = 0;
            B_8C88FF42 = 0;
            B_8C88FF44 = 0;
            B_8C88FF38 = 0;
            B_8C88FE80[0](object);
            B_8C88FF38 = 1;
            B_8C88FE80[1](object);
            func_8C0417C8();
        } else {
            m = B_8C88FE58;
            v = object->pos;
            x = *v++;
            y = *v++;
            z = *v;
            njTranslate(m, x, y, z);
            rotate = njRotateXYZ;
            if (flags & 0x20) {
                rotate = njRotateZXY;
            }
            a = object->ang;
            ax = a[0];
            ay = a[1];
            az = a[2];
            rotate(m, ax, ay, az);
            v = object->scl;
            x = *v++;
            y = *v++;
            z = *v;
            njScale(m, x, y, z);
        }
        if (B_8C890074 != NULL) {
            B_8C890074(object);
        }
        if (object->model != NULL && !(flags & 8)) {
            func_8C03700C(object->model);
        }
        if (object->child != NULL) {
            func_8C041A2E(object->child);
        }
        njPopMatrix(1);
        object = object->sibling;
    } while (object != NULL);
}
#else
ASM_STUB(void func_8C041A2E(NJS_OBJECT *object))
#endif

#ifdef NON_MATCHING
void func_8C041B0E(NJS_OBJECT *object)
{
    Uint32 flags;
    NJS_MATRIX_PTR m;
    void (*rotate)(NJS_MATRIX_PTR m, Angle angx, Angle angy, Angle angz);
    Float *v;
    Float x, y, z;
    Angle *a;
    Angle ax, ay, az;

    do {
        flags = object->evalflags;
        njPushMatrix(NULL);
        if (!(flags & 0x40)) {
            B_8C88FF40 = 0;
            B_8C88FF42 = 0;
            B_8C88FF44 = 0;
            B_8C88FF38 = 0;
            B_8C88FE80[0](object);
            B_8C88FF38 = 1;
            B_8C88FE80[1](object);
            func_8C0417C8();
        } else {
            m = B_8C88FE58;
            v = object->pos;
            x = *v++;
            y = *v++;
            z = *v;
            njTranslate(m, x, y, z);
            rotate = njRotateXYZ;
            if (flags & 0x20) {
                rotate = njRotateZXY;
            }
            a = object->ang;
            ax = a[0];
            ay = a[1];
            az = a[2];
            rotate(m, ax, ay, az);
            v = object->scl;
            x = *v++;
            y = *v++;
            z = *v;
            njScale(m, x, y, z);
        }
        if (B_8C890074 != NULL) {
            B_8C890074(object);
        }
        if (!(flags & 0x80)) {
            B_8C88FF46 = 0;
            B_8C88FF48 = 0;
            B_8C88FF38 = 0;
            B_8C88FE88[0](object);
            B_8C88FF38 = 1;
            B_8C88FE88[1](object);
            func_8C04196C(object);
        } else if (object->model != NULL && !(flags & 8)) {
            func_8C03700C(object->model);
        }
        if (object->child != NULL) {
            func_8C041B0E(object->child);
        }
        njPopMatrix(1);
        object = object->sibling;
    } while (object != NULL);
}
#else
ASM_STUB(void func_8C041B0E(NJS_OBJECT *object))
#endif

#ifdef NON_MATCHING
void func_8C041C92(NJS_ACTION_LINK *action, Float rate)
{
    NJS_OBJECT *object;
    NJS_MOTION_LINK *link;

    if (rate > 1.0f) {
        return;
    }
    link = action->motionlink;
    object = action->object;
    B_8C88FF38 = 0;
    B_8C88FF3C = rate;
    if (func_8C040B90(link, 0) == 0 && func_8C040B90(link, 1) == 0) {
        func_8C040C02(object, 0);
        func_8C040C02(object, 1);
        func_8C605DAC();
        func_8C041A2E(object);
        func_8C605E0A();
    }
}
#else
ASM_STUB(void func_8C041C92(NJS_ACTION_LINK *action, Float rate))
#endif

#ifdef NON_MATCHING
void func_8C041D68(NJS_OBJECT *object, NJS_MOTION_LINK *link, Float rate)
{
    if (rate > 1.0f) {
        return;
    }
    B_8C88FF38 = 0;
    B_8C88FF3C = rate;
    if (func_8C040B90(link, 0) == 0 && func_8C040B90(link, 1) == 0) {
        func_8C040C02(object, 0);
        func_8C040C02(object, 1);
        func_8C605DAC();
        func_8C041A2E(object);
        func_8C605E0A();
    }
}
#else
ASM_STUB(void func_8C041D68(NJS_OBJECT *object, NJS_MOTION_LINK *link, Float rate))
#endif

#ifdef NON_MATCHING
void func_8C041DC6(NJS_OBJECT *object, NJS_MOTION_LINK *link, NJS_MOTION_LINK *shape, Float rate)
{
    if (rate > 1.0f) {
        return;
    }
    B_8C88FF38 = 0;
    B_8C88FF3C = rate;
    if (func_8C040B90(link, 0) == 0 && func_8C040B90(link, 1) == 0
        && func_8C040AF4(shape, 0) == 0 && func_8C040AF4(shape, 1) == 0) {
        func_8C040C02(object, 0);
        func_8C040C02(object, 1);
        func_8C605DAC();
        func_8C041B0E(object);
        func_8C605E0A();
    }
}
#else
ASM_STUB(void func_8C041DC6(NJS_OBJECT *object, NJS_MOTION_LINK *link, NJS_MOTION_LINK *shape, Float rate))
#endif
