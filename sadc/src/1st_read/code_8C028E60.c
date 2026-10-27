#include "common.h"
#include "ninja.h"
#include "sa1/sa1.h"

/*
 * Land collision object cache ("colbuf"): the objects of the gathered land
 * collision list (B_8C754EF4, built by 8C052518 / 8C052E30) are kept, with
 * the polygons that 8C028B00 builds for them, in a 192-entry object buffer
 * (B_8C6BB27C) and an 8192-entry polygon buffer (B_8C6BE27C). The list and
 * free-list helpers 8C02CE80..8C02D00E are hand-written assembly; their list
 * heads live in the code section: 8C02D540 (free polygons), 8C02D544 (free
 * objects), 8C02D548 (objects in use).
 *
 * COMPILER FLAGS: this region (8C027360-8C02D980) was built without bit
 * 0x1000 of -extra=a=1800: 16-bit constants are MOV.W pool loads, not
 * MOV #imm/SHLL8. func_8C028E60 is byte-identical with
 *   shcdiff.py ... --flags="-define=NON_MATCHING -extra=a=800"
 * and is kept under NON_MATCHING until the build supports per-file flags
 * (same situation as code_8C0100A0.c). func_8C028EBA has no 16-bit constant
 * and matches with the project flags.
 */

/* polygon buffer entry ("RANGE-poly", B_8C6BE27C, 0x2000 entries) */
typedef struct colpoly {
    struct colpoly *next;       /* 0x00 free list / per-object list (8C02CE80, 8C02D00E) */
    struct colpoly *prev;       /* 0x04 linked by 8C02D00E */
    Sint32 unk08;               /* 0x08 */
    Sint32 unk0C;               /* 0x0C */
    Sint32 unk10;               /* 0x10 */
    Float unk14;                /* 0x14 cleared by 8C028E60 */
    Sint32 unk18[10];           /* 0x18 */
} colpoly;                      /* 0x40 */

/* object buffer entry ("OBJECT buf", B_8C6BB27C, 192 entries) */
typedef struct colobj {
    struct colobj *next;        /* 0x00 free list D_8C02D544 / in-use list D_8C02D548 */
    void *prev;                 /* 0x04 previous entry, or the negated address of the list head (asm 8C02CEF4, 8C02CF20) */
    NJS_OBJECT *object;         /* 0x08 NULL: entry free */
    Uint32 attr;                /* 0x0C _OBJ_LANDCOLL.slAttribute */
    colpoly *polys;             /* 0x10 first polygon (8C02D00E) */
    colpoly *last;              /* 0x14 last polygon (8C02D00E) */
    Sint32 unk18;               /* 0x18 */
    void *unk1C;                /* 0x1C pointer: cleared with a pointer zero like polys */
    Sint32 unk20;               /* 0x20 */
    Sint32 unk24;               /* 0x24 */
    Float unk28;                /* 0x28 B_8C73EDF0 after 8C028B00 */
    Float unk2C;                /* 0x2C B_8C73EDF4 after 8C028B00 */
    NJS_POINT3 center;          /* 0x30 model centre in view space */
    Sint32 unk3C;               /* 0x3C */
} colobj;                       /* 0x40 */

SA1_ASSERT_SIZE(colpoly, 0x40);
SA1_ASSERT_OFFSET(colpoly, unk14, 0x14);
SA1_ASSERT_SIZE(colobj, 0x40);
SA1_ASSERT_OFFSET(colobj, polys, 0x10);
SA1_ASSERT_OFFSET(colobj, unk1C, 0x1C);
SA1_ASSERT_OFFSET(colobj, unk28, 0x28);
SA1_ASSERT_OFFSET(colobj, center, 0x30);

#define COLOBJ_MAX  192
#define COLPOLY_MAX 0x2000

extern colobj B_8C6BB27C[COLOBJ_MAX];
extern colpoly B_8C6BE27C[COLPOLY_MAX];
extern Sint32 B_8C73E27C;
extern Sint32 B_8C73E400;
extern colobj *D_8C02D548;          /* objects in use (list head in the code section) */
extern NJS_OBJECT *B_8C73EDC4;      /* object to rebuild on the next update */
extern Float B_8C73EDEC;            /* view-space z of the current object's centre */
extern Float B_8C73EDF0;
extern Float B_8C73EDF4;
extern Sint32 D_8C196044;           /* 1: next update gathers with 8C052E30 instead of 8C052518 */
extern NJS_MATRIX D_8C67C640;
extern Sint8 tcrf_CollisionDisplayVar;
extern char D_8C1124A8[];           /* "colbuf:" */
extern char D_8C1124B0[];           /* "obj:" */
extern char D_8C1124B8[];           /* "%" */
extern char D_8C1124BC[];           /* "WARNING: OBJECT buf is FULL" */
extern char D_8C1124D8[];           /* "polMAX:" */
extern char D_8C1124E0[];           /* "r-p:" */
extern char D_8C1124E8[];           /* "WARNING: RANGE-poly buf is FULL" */

/* hand-written list helpers */
extern void func_8C02CE80(colpoly *buf, Sint32 n);      /* builds the polygon free list */
extern void func_8C02CECA(colobj *buf, Sint32 n);       /* builds the object free list, clears the in-use list */
extern colobj *func_8C02CEF4(void);                     /* takes a free object, links it in use (NULL: none) */
extern void func_8C02CF48(colobj *co);                  /* frees an object and its polygons */
extern void func_8C02CFA4(void);                        /* clears the 128-entry table D_8C02D560 */
extern void func_8C02D00E(colobj *co);                  /* chains the D_8C02D560 entries into co->polys/last */
/* builds the polygons of an object tree; returns obj->sibling */
extern NJS_OBJECT *func_8C028B00(NJS_OBJECT *obj, Uint32 attr, colobj *co);
extern void func_8C052518(void);
extern void func_8C052E30(void);

#ifdef NON_MATCHING
/* byte-identical with -extra=a=800 (see the note at the top of the file) */
Sint32 func_8C028E60(void)
{
    Sint32 i;
    colobj *co;
    colpoly *poly;

    B_8C73E27C = 0;
    B_8C73E400 = 0;
    D_8C02D548 = NULL;
    B_8C73EDC4 = NULL;
    i = COLOBJ_MAX;
    co = B_8C6BB27C;
    while (i > 0) {
        co->object = NULL;
        co++;
        i--;
    }
    i = COLPOLY_MAX;
    poly = B_8C6BE27C;
    while (i > 0) {
        poly->unk14 = 0.0f;
        poly++;
        i--;
    }
    func_8C02CE80(B_8C6BE27C, COLPOLY_MAX);
    func_8C02CECA(B_8C6BB27C, COLOBJ_MAX);
    return 1;
}
#endif

void func_8C028EBA(void)
{
    D_8C196044 = 1;
}

#ifdef NON_MATCHING
/*
 * Updates the object cache from the gathered list and draws the debug
 * statistics. Not matching (also needs -extra=a=800): instruction selection
 * and control flow are the original's (95% of the normalised instruction
 * stream is identical), but the register allocation differs: the original
 * keeps 192 in R12 and 1 in R11 (both loaded before the clamp of num), num
 * in R9 and pa in R6 in the first loop, does not hoist the address of
 * func_8C02CF48 out of the second loop, and in the build loop keeps pa in R9
 * and spills the counter to @(4,R15) (here pa is spilled). Findings that
 * reproduce the original code shapes:
 *  - loop 1 is a for loop with continue (BRA to the test at the bottom);
 *  - the rotation angle and the polygon total share one memory-resident
 *    variable at @R15 (the same store/reload idiom is in 8C028B00), here a
 *    volatile local;
 *  - the per-object matrix is passed through a local (spilled to @(8,R15));
 *  - polys and unk1C are pointers (pointer zero from MOV #0 + dead MOV copy);
 *  - the statistics loop is a do-while.
 */
Sint32 func_8C028EC2(void)
{
    NJS_OBJECT *objs[COLOBJ_MAX];
    Uint32 attrs[COLOBJ_MAX];
    NJS_POINT3 pos;
    NJS_OBJECT **po;
    Uint32 *pa;
    _OBJ_LANDCOLL *lc;
    colobj *co;
    colobj *next;
    colpoly *poly;
    NJS_OBJECT *obj;
    NJS_MATRIX_PTR m;           /* matrix pushed for each object */
    Sint32 num;
    Sint32 n;
    Sint32 found;
    Sint32 nfree;
    Sint32 cnt;
    Sint32 max;
    Sint32 pct;
    Sint32 i;
    volatile Sint32 total;     /* also the rotation angle (see above) */

    if (D_8C196044 == 0) {
        func_8C052518();
    } else {
        func_8C052E30();
        D_8C196044 = 0;
    }
    n = 0;
    num = B_8C754EF0;
    po = objs;
    pa = attrs;
    if (num > COLOBJ_MAX) {
        num = COLOBJ_MAX;
    }
    lc = B_8C754EF4;
    for (; num > 0; num--, lc++) {
        if (lc->slAttribute & 0x08000000) {
            n++;
            *po++ = lc->pObject;
            *pa++ = lc->slAttribute;
            continue;
        }
        if (B_8C73EDC4 == lc->pObject) {
            n++;
            B_8C73EDC4 = NULL;
            *po++ = lc->pObject;
            *pa++ = lc->slAttribute;
            continue;
        }
        found = FALSE;
        co = D_8C02D548;
        while (co != NULL) {
            if (co->object == lc->pObject) {
                found = TRUE;
                break;
            }
            co = co->next;
        }
        if (!found) {
            n++;
            *po++ = lc->pObject;
            *pa++ = lc->slAttribute;
            if (n == COLOBJ_MAX) {
                goto build;
            }
        }
    }
    if (n != COLOBJ_MAX) {
        co = D_8C02D548;
        while (co != NULL) {
            num = B_8C754EF0;
            lc = B_8C754EF4;
            found = FALSE;
            while (num > 0) {
                if (co->object == lc->pObject) {
                    if ((lc->slAttribute & 0x08000000) || B_8C73EDC4 == lc->pObject) {
                        found = FALSE;
                    } else {
                        found = TRUE;
                    }
                    break;
                }
                num--;
                lc++;
            }
            next = co->next;
            if (!found) {
                func_8C02CF48(co);
            }
            co = next;
        }
    }
build:
    njPushMatrix(D_8C67C640);
    pa = attrs;
    po = objs;
    i = n;
    m = D_8C67C640;
    while (i > 0) {
        co = func_8C02CEF4();
        func_8C02CFA4();
        njPushMatrix(m);
        obj = *po;
        njTranslateV(NULL, (NJS_VECTOR *)obj->pos);
        if ((total = obj->ang[2]) != 0) {
            njRotateZ(NULL, total);
        }
        if ((total = obj->ang[1]) != 0) {
            njRotateY(NULL, total);
        }
        if ((total = obj->ang[0]) != 0) {
            njRotateX(NULL, total);
        }
        njScaleV(NULL, (NJS_VECTOR *)obj->scl);
        pos.x = obj->model->center.x;
        pos.y = obj->model->center.y;
        pos.z = obj->model->center.z;
        njCalcPoint(NULL, &pos, &pos);
        B_8C73EDEC = pos.z;
        co->center.x = pos.x;
        co->center.y = pos.y;
        co->center.z = pos.z;
        njPopMatrix(1);
        B_8C73EDF0 = 0.0f;
        B_8C73EDF4 = 0.0f;
        co->object = *po;
        co->attr = *pa;
        co->polys = NULL;
        co->unk18 = 0;
        co->unk1C = NULL;
        co->unk24 = 0;
        func_8C028B00(*po++, *pa++, co);
        func_8C02D00E(co);
        co->unk28 = B_8C73EDF0;
        co->unk2C = B_8C73EDF4;
        i--;
    }
    njPopMatrix(1);
    if (tcrf_CollisionDisplayVar >= 0) {
        njPrintC(0x0002001B, D_8C1124A8);
        co = B_8C6BB27C;
        nfree = 0;
        i = COLOBJ_MAX;
        max = 0;
        total = 0;
        do {
            if (co->object == NULL) {
                nfree++;
            } else {
                cnt = 0;
                poly = co->polys;
                while (poly != NULL) {
                    poly = poly->next;
                    cnt++;
                }
                if (max < cnt) {
                    max = cnt;
                }
                total += cnt;
            }
            i--;
            co++;
        } while (i > 0);
        pct = 100 - nfree * 100 / COLOBJ_MAX;
        njPrintC(0x000A001A, D_8C1124B0);
        if (pct >= 95) {
            njPrintColor(0xFFFF8080);
            func_8C640A80(0x000F001A, pct, 3);
            njPrintC(0x0012001A, D_8C1124B8);
            njPrintColor(0xFFFFFFFF);
        } else {
            func_8C640A80(0x000F001A, pct, 3);
            njPrintC(0x0012001A, D_8C1124B8);
        }
        func_8C640A80(0x0014001A, COLOBJ_MAX - nfree, 5);
        if (nfree == 0 && tcrf_CollisionDisplayVar >= 0) {
            njPrintC(0x00020013, D_8C1124BC);
        }
        njPrintC(0x001B001A, D_8C1124D8);
        func_8C640A80(0x0023001A, max, 4);
        pct = total * 100 / COLPOLY_MAX;
        njPrintC(0x000A001B, D_8C1124E0);
        if (pct >= 95) {
            njPrintColor(0xFFFF8080);
            func_8C640A80(0x000F001B, pct, 3);
            njPrintC(0x0012001B, D_8C1124B8);
            njPrintColor(0xFFFFFFFF);
        } else {
            func_8C640A80(0x000F001B, pct, 3);
            njPrintC(0x0012001B, D_8C1124B8);
        }
        func_8C640A80(0x0014001B, total, 5);
        /* original checks the object count again, not the polygon count */
        if (nfree == 0 && tcrf_CollisionDisplayVar >= 0) {
            njPrintC(0x00020014, D_8C1124E8);
        }
    }
    return 1;
}
#endif
