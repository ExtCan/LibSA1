#include "common.h"
#include "ninja.h"

/*
 * 8C02CFC0-8C02D050: hand-written SH-4 assembly (not SHC output).
 *
 * Last two routines of the land collision list module 8C02CE80-8C02D54C
 * (free-list and list helpers 8C02CE80, 8C02CEA0, 8C02CECA, 8C02CEF4,
 * 8C02CF20, 8C02CF48, the bucket clear 8C02CFA4; see code_8C02CF20.c,
 * code_8C02CFA4.c and code_8C028E60.c):
 * - zero-displacement accesses are encoded MOV.L @(0,R0),R3 (0x5300),
 *   MOV.L @(0,R4),R2 (0x5240), MOV.L R2,@(0,R0) (0x1020),
 *   MOV.L R0,@(0,R4) (0x1400) and MOV.L R0,@(0,R5) (0x1500); SHC always
 *   emits MOV.L @Rm,Rn (0x6nm2) and MOV.L Rm,@Rn (0x2nm2).
 * - func_8C02D00E starts at a 2-byte-aligned address (8C02D00E); SHC
 *   aligns every function to at least 4.
 * - func_8C02CFC0 scales the bucket index with SHLL2+SHLL and returns NULL
 *   with MOV #0,R0 in the RTS delay slot after a BRA over its literal pool;
 *   func_8C02D00E splits its loop (DT, then ADD #-1/CMP/PL + DT) by hand.
 * The C below is the same algorithm; the original assembly stays in the
 * build (bodies only under NON_MATCHING, used by libsa1).
 *
 * func_8C02CFC0: takes a polygon from the free list D_8C02D540 and pushes
 *   it on the z bucket ((Sint32)z >> 4) + 64, clamped to 0..127, of the
 *   polygon sorter D_8C02D560 (the bucket's tail is set when the bucket
 *   was empty; the polygon's prev is cleared). Returns NULL, without
 *   touching anything, when the free head has no successor: the last free
 *   polygon is never handed out (same rule as func_8C02CEF4).
 *   Caller: func_8C0287A0 (code_8C028620.c), z = view z - B_8C73EDEC.
 * func_8C02D00E: concatenates the 128 buckets in ascending order into one
 *   doubly linked chain and stores it in co->polys .. co->last. With all
 *   buckets empty co->polys is left as it is (the caller cleared it) and
 *   co->last becomes NULL. The buckets themselves are not cleared
 *   (func_8C02CFA4 does that).
 *   Caller: func_8C028EC2 (code_8C028E60.c), after func_8C028B00.
 */

/* polygon buffer entry ("RANGE-poly", B_8C6BE27C, 0x2000 entries); same
 * layout as COLPOLY in code_8C028620.c (should move to sa1/collision.h) */
typedef struct colpoly {
    struct colpoly *next;       /* 0x00 free list / bucket list / per-object chain */
    struct colpoly *prev;       /* 0x04 previous polygon (NULL: first) */
    NJS_POINT3 center;          /* 0x08 centroid */
    Float r;                    /* 0x14 bounding radius^2 (0 = free slot) */
    NJS_POINT3 v[3];            /* 0x18 vertices relative to center */
    Uint32 attr;                /* 0x3C material attrflags */
} colpoly;                      /* 0x40 */

/* one z bucket of polygons (8 bytes, D_8C02D560[128]); same layout as in
 * code_8C02CFA4.c (should move to sa1/collision.h) */
typedef struct colbucket {
    colpoly *head;              /* 0x00 first polygon (func_8C02CFC0 pushes here) */
    colpoly *tail;              /* 0x04 last polygon (set when the bucket was empty) */
} colbucket;

/* object buffer entry ("OBJECT buf", B_8C6BB27C, 192 entries); same layout
 * as in code_8C028E60.c (should move to sa1/collision.h) */
typedef struct colobj {
    struct colobj *next;        /* 0x00 free list D_8C02D544 / in-use list D_8C02D548 */
    void *prev;                 /* 0x04 previous in-use entry, or the list-head marker */
    NJS_OBJECT *object;         /* 0x08 NULL: entry free */
    Uint32 attr;                /* 0x0C _OBJ_LANDCOLL.slAttribute */
    colpoly *polys;             /* 0x10 first polygon (func_8C02D00E) */
    colpoly *last;              /* 0x14 last polygon (func_8C02D00E) */
    Sint32 unk18;               /* 0x18 */
    void *unk1C;                /* 0x1C */
    Sint32 unk20;               /* 0x20 */
    Sint32 unk24;               /* 0x24 */
    Float unk28;                /* 0x28 */
    Float unk2C;                /* 0x2C */
    NJS_POINT3 center;          /* 0x30 model centre in view space */
    Sint32 unk3C;               /* 0x3C */
} colobj;                       /* 0x40 */

SA1_ASSERT_SIZE(colpoly, 0x40);
SA1_ASSERT_OFFSET(colpoly, prev, 0x04);
SA1_ASSERT_OFFSET(colpoly, r, 0x14);
SA1_ASSERT_OFFSET(colpoly, attr, 0x3C);
SA1_ASSERT_SIZE(colbucket, 0x08);
SA1_ASSERT_OFFSET(colbucket, tail, 0x04);
SA1_ASSERT_SIZE(colobj, 0x40);
SA1_ASSERT_OFFSET(colobj, polys, 0x10);
SA1_ASSERT_OFFSET(colobj, last, 0x14);

#define COLBUCKET_MAX 128

extern colpoly *D_8C02D540;                     /* free polygons (head); data word in the code section */
extern colbucket D_8C02D560[COLBUCKET_MAX];     /* z buckets; data block in the code section */

#ifdef NON_MATCHING
colpoly *func_8C02CFC0(Float z)
{
    colbucket *b;
    colpoly *p;
    colpoly *next;
    colpoly *head;
    Sint32 i;

    i = (SA1_FTOI(z) >> 4) + 64;
    if (i < 0) {
        i = 0;
    }
    if (i > COLBUCKET_MAX - 1) {
        i = COLBUCKET_MAX - 1;
    }
    b = &D_8C02D560[i];
    p = D_8C02D540;
    next = p->next;
    if (next == NULL) {
        return NULL;
    }
    D_8C02D540 = next;
    head = b->head;
    p->next = head;
    if (head != NULL) {
        head->prev = p;
    } else {
        b->tail = p;
    }
    b->head = p;
    p->prev = NULL;
    return p;
}
#endif

#ifdef NON_MATCHING
void func_8C02D00E(colobj *co)
{
    colbucket *b;
    colpoly *p;
    colpoly *last;
    Sint32 i;

    b = D_8C02D560;
    i = COLBUCKET_MAX;
    last = NULL;
    do {
        p = b->head;
        if (p != NULL) {
            co->polys = p;
            last = b->tail;
            b++;
            while (--i > 0) {
                p = b->head;
                if (p != NULL) {
                    last->next = p;
                    p->prev = last;
                    last = b->tail;
                }
                b++;
            }
            break;
        }
        b++;
    } while (--i != 0);
    co->last = last;
}
#endif
