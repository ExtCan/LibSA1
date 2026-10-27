#include "sa1/sa1.h"

/*
 * 8C052518-8C0529CA: dynamic land collision list (second half of the
 * translation unit 8C0522C0-8C052A00; func_8C0522C0 is in code_8C0522C0.c).
 *
 * B_8C759708 (count B_8C759704, max 256) is the list of moving land
 * collision objects registered by object tasks: func_8C0528AE adds one
 * (SADX RegisterCollisionEntry) and sets taskwk.flag 0x100 on its task,
 * func_8C052940 removes the entry of a task (SADX WithdrawCollisionEntry)
 * and forgets the object in the collision object cache (func_8C02CE18).
 * Every frame func_8C052518 rebuilds the land collision source list
 * B_8C757EF4 (count D_8C19E964, max 128) from the dynamic entries whose
 * task still has taskwk.flag 0x100 and whose bounding sphere comes within
 * reach of player 1 / player 2 (the camera when there is no player): the
 * first position uses the range D_8C19E96C, the second 40. When
 * D_8C19E978 == 1 (func_8C0528A6 sets it, func_8C05289E clears it) it also
 * copies every landtable entry with an attribute in 0x400003 near a
 * position into B_8C7584F4 (count D_8C19E968, max 128). Each copied entry
 * is also appended to the per-query list B_8C754EF4 (count B_8C754EF0, max
 * 1024). func_8C052E30 is the "gather everything" variant of
 * func_8C052518.
 */

/*
 * Landtable entry (SADX _OBJ_LANDENTRY / COL), 0x24 bytes; same layout as
 * the file-local copies in code_8C0520C8.c, code_8C0522C0.c and
 * code_8C052E30.c. File-local until the shared headers carry it.
 */
typedef struct _OBJ_LANDENTRY {
    Float xCenter;              /* 0x00 */
    Float yCenter;              /* 0x04 */
    Float zCenter;              /* 0x08 */
    Float xWidth;               /* 0x0C bounding radius */
    Float yWidth;               /* 0x10 */
    Float zWidth;               /* 0x14 */
    NJS_OBJECT *pObject;        /* 0x18 */
    Sint32 blockbit;            /* 0x1C */
    Uint32 slAttribute;         /* 0x20 */
} _OBJ_LANDENTRY;               /* 0x24 */

SA1_ASSERT_SIZE(_OBJ_LANDENTRY, 0x24);
SA1_ASSERT_OFFSET(_OBJ_LANDENTRY, xWidth, 0x0C);
SA1_ASSERT_OFFSET(_OBJ_LANDENTRY, pObject, 0x18);
SA1_ASSERT_OFFSET(_OBJ_LANDENTRY, slAttribute, 0x20);

/*
 * Landtable header (SADX _OBJ_LANDTABLE), only the fields read here:
 * ssCount (MOV.W @R4) and pLandEntry (+0x0C).
 */
typedef struct _OBJ_LANDTABLE {
    Sint16 ssCount;             /* 0x00 number of entries in pLandEntry */
    Sint16 ssVisibleCount;      /* 0x02 */
    Sint16 ssMotLandEntryCount; /* 0x04 */
    Sint16 ssAttribute;         /* 0x06 */
    Float fFarClipping;         /* 0x08 */
    _OBJ_LANDENTRY *pLandEntry; /* 0x0C */
} _OBJ_LANDTABLE;

SA1_ASSERT_OFFSET(_OBJ_LANDTABLE, pLandEntry, 0x0C);

extern Sint16 B_8C759704;               /* number of entries in B_8C759708 (MOV.W) */
extern _OBJ_LANDCOLL B_8C759708[];      /* dynamic land collision list, max 256 */
extern _OBJ_LANDENTRY B_8C7584F4[];     /* landtable entry copies, count D_8C19E968, max 128 */
extern Sint32 D_8C19E968;
extern Sint32 D_8C19E978;               /* 1 = also gather landtable entries */
extern _OBJ_LANDTABLE *B_8C7596F4;      /* current landtable */
extern Float D_8C19E96C;                /* gather range of the first position (set by 8C052F34/8C052F3A) */

void func_8C02CE18(NJS_OBJECT *obj);

/*
 * NON_MATCHING. Two separate problems:
 * 1. Translation unit: the original file also held func_8C0522C0, whose
 *    pending njSqrt literal (used at 8C0524B8) makes SHC flush a literal
 *    pool in the middle of this function (8C0525F8, the orphan _njSqrt
 *    entry). Compiled alone this function gets its pool elsewhere, so
 *    eight conditional branches change between the short and the long
 *    (BT/BRA/NOP) forms. It can only match when compiled after
 *    func_8C0522C0 in the same .c file.
 * 2. Compiled in such a combined file, the code has the right size (376
 *    instructions) and differs in 43 register choices only: dst and
 *    &B_8C754EF0 are swapped (R13/R14), dy and dz are in FR6 and FR5 in the
 *    first inner loop (original FR5 and FR6), and several scratch registers
 *    (flag test, loop-2 setup, d - rr after njSqrt) differ.
 * The C is complete and follows the original control flow, FMAC sites
 * (2, first inner loop) and evaluation order.
 */
#ifdef NON_MATCHING
void func_8C052518(void)
{
    NJS_POINT3 pos[2];
    NJS_POINT3 c;
    _OBJ_LANDCOLL *dst;
    _OBJ_LANDCOLL *col;
    _OBJ_LANDCOLL *src;
    _OBJ_LANDENTRY *lent;
    _OBJ_LANDENTRY *ent;
    NJS_OBJECT *obj;
    NJS_POINT3 *p;
    taskwk **tpp;
    Float *a;
    Float *b;
    Sint16 i;
    Sint32 n;
    Sint32 j;
    Float cx;
    Float cy;
    Float cz;
    Float dx;
    Float dy;
    Float dz;
    Float d;
    Float rr;
    Float add;

    dst = B_8C754EF4;
    p = pos;
    tpp = playertwp;
    *p = camera_twp->pos;
    n = 1;
    if (*tpp != NULL) {
        *p = (*tpp)->pos;
        tpp++;
        if (*tpp != NULL) {
            p++;
            *p = (*tpp)->pos;
            n++;
        }
    } else {
        tpp++;
        if (*tpp != NULL) {
            *p = (*tpp)->pos;
        }
    }
    B_8C754EF0 = 0;
    D_8C19E964 = 0;
    col = B_8C757EF4;
    njPushMatrix(NULL);
    for (src = &B_8C759708[B_8C759704 - 1]; src >= B_8C759708; src--) {
        obj = src->pObject;
        if (src->pTask->twp->flag & 0x100) {
            if (!(src->slAttribute & 0x10000000)) {
                a = obj->pos;
                b = &obj->model->center.x;
            } else {
                njUnitMatrix(NULL);
                obj->ang[2] ? njRotateZ(NULL, obj->ang[2]) : (void)0;
                obj->ang[0] ? njRotateX(NULL, obj->ang[0]) : (void)0;
                obj->ang[1] ? njRotateY(NULL, obj->ang[1]) : (void)0;
                njCalcPoint(NULL, &obj->model->center, &c);
                a = obj->pos;
                b = &c.x;
            }
            cx = *a++ + *b++;
            cy = *a++ + *b++;
            cz = *a + *b;
            add = D_8C19E96C;
            for (p = pos, j = n; j > 0; j--, p++) {
                a = &p->x;
                dx = *a++ - cx;
                dy = *a++ - cy;
                dz = *a - cz;
                d = SH_ONLY(dx * dx + dy * dy + dz * dz) PC_ONLY(SA1_FMAC(dz, dz, SA1_FMAC(dx, dx, dy * dy)));
                rr = obj->model->r;
                rr += add;
                rr *= rr;
                if (d < rr) {
                    *col = *src;
                    col++;
                    D_8C19E964++;
                    dst->pObject = obj;
                    dst->pTask = src->pTask;
                    dst->slAttribute = src->slAttribute;
                    B_8C754EF0++;
                    dst++;
                    if (D_8C19E964 >= 128) {
                        goto done;
                    }
                    if (B_8C754EF0 >= 1024) {
                        njPopMatrix(1);
                        return;
                    }
                    break;
                }
                add = 40.0f;
            }
        }
    }
done:
    njPopMatrix(1);
    D_8C19E968 = 0;
    lent = B_8C7584F4;
    if (D_8C19E978 == 1 && B_8C7596F4 != NULL) {
        ent = B_8C7596F4->pLandEntry;
        for (i = B_8C7596F4->ssCount; i > 0; ent++, i--) {
            if (!(ent->slAttribute & 0x400003)) {
                continue;
            }
            rr = D_8C19E96C;
            for (j = n, p = pos; j > 0; j--, p++) {
                a = &ent->xCenter;
                dx = *a++;
                dy = *a++;
                dz = *a;
                a = &p->x;
                dx -= *a++;
                dy -= *a++;
                dz -= *a;
                dx *= dx;
                dy *= dy;
                dz *= dz;
                d = njSqrt(dx + dy + dz);
                d -= rr;
                if (ent->xWidth > d) {
                    *lent = *ent;
                    lent++;
                    D_8C19E968++;
                    dst->slAttribute = ent->slAttribute;
                    dst->pTask = NULL;
                    dst->pObject = ent->pObject;
                    B_8C754EF0++;
                    dst++;
                    if (D_8C19E968 >= 128) {
                        return;
                    }
                    if (B_8C754EF0 >= 1024) {
                        return;
                    }
                    break;
                }
                rr = 40.0f;
            }
        }
    }
}
#else
ASM_STUB(void func_8C052518(void))
#endif /* NON_MATCHING */

void func_8C05289E(void)
{
    D_8C19E978 = 0;
}

void func_8C0528A6(void)
{
    D_8C19E978 = 1;
}

void func_8C0528AE(Uint32 attr, task *tp, NJS_OBJECT *obj)
{
    Float x;
    Float y;
    Float z;
    Float d;

    if (B_8C759704 < 256) {
        x = obj->model->center.x;
        y = obj->model->center.y;
        z = obj->model->center.z;
        d = SH_ONLY(x * x + y * y + z * z) PC_ONLY(SA1_FMAC(z, z, SA1_FMAC(x, x, y * y)));
        if (d >= 225.0f) {
            if ((obj->ang[0] | obj->ang[1] | obj->ang[2]) & 0xFFF0) {
                attr |= 0x10000000;
            }
        }
        B_8C759708[B_8C759704].slAttribute = attr;
        B_8C759708[B_8C759704].pObject = obj;
        B_8C759708[B_8C759704++].pTask = tp;
        tp->twp->flag |= 0x100;
    }
}

void func_8C052940(task *tp, NJS_OBJECT *obj)
{
    Sint16 i;
    Sint16 j;

    for (i = B_8C759704 - 1; i >= 0; i--) {
        if (tp == B_8C759708[i].pTask) {
            break;
        }
    }
    if (i >= 0) {
        B_8C759704--;
        for (j = i; j < B_8C759704; j++) {
            B_8C759708[j].pObject = B_8C759708[j + 1].pObject;
            B_8C759708[j].pTask = B_8C759708[j + 1].pTask;
            B_8C759708[j].slAttribute = B_8C759708[j + 1].slAttribute;
        }
        func_8C02CE18(obj);
    }
}
