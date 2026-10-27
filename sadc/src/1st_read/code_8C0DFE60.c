/*
 * 1ST_READ 8C0DFE60-8C0DFFA0 (chunk 8C0DFE60): Amy target record.
 *
 *  func_8C0DFE60  player 0's taskwk if player 0 is Amy (GetPlayerNumber() == 5),
 *                 else NULL
 *  func_8C0DFE86  stick of pad 0 beyond a dead zone (|v|^2 > 2.25e8 in <<8
 *                 units): *ang = atan2f(x, -y) as an Angle, returns 1
 *  func_8C0DFEDC  fills the record B_8C7A40AC: pos, ang, flag (Amy state
 *                 func_8C0DDE1C(0) == 4), flag2/ang2 (stick); returns it when
 *                 func_8C0DE540 succeeds, else clears the flags and returns NULL
 *  func_8C0DFF4A  returns &B_8C7A40AC (code_8C0DB230.c func_8C0DB28E)
 */
#include "common.h"
#include "sa1/sa1.h"

/*
 * Record at B_8C7A40AC (same layout as AMY_TARGETWK in code_8C0DB230.c;
 * candidate for a shared header).
 */
typedef struct {
    Sint32 flag;                /* 0x00 Amy state 4 */
    Sint32 flag2;               /* 0x04 ang2 valid */
    Angle ang2;                 /* 0x08 stick angle */
    Angle ang;                  /* 0x0C */
    NJS_POINT3 pos;             /* 0x10 */
} AMY_TARGETWK;                 /* 0x1C */

SA1_ASSERT_SIZE(AMY_TARGETWK, 0x1C);

extern AMY_TARGETWK B_8C7A40AC;

Sint32 func_8C0DDE1C(Sint32 pno);                   /* code_8C0DDA00.c: Amy state */
Bool func_8C0DE540(void);                           /* code_8C0DE540.c */

taskwk *func_8C0DFE60(void)
{
    taskwk *twp;

    twp = B_8C78C608[0];
    if (twp == NULL || GetPlayerNumber() != 5) {
        return NULL;
    }
    return twp;
}

Sint32 func_8C0DFE86(Angle *ang)
{
    Float x;
    Float y;

    x = (Float)(B_8C754C08[0].x1 << 8);
    y = -(Float)(B_8C754C08[0].y1 << 8);
    if (SA1_FMAC(x, x, y * y) > 225000000.0f) {
        *ang = SA1_FTOI(atan2f(x, y) * 10430.381f);
        return 1;
    }
    return 0;
}

AMY_TARGETWK *func_8C0DFEDC(NJS_POINT3 *pos, Angle ang)
{
    Angle a;
    AMY_TARGETWK *t;

    if (func_8C0DFE60() == NULL) {
        return NULL;
    }
    t = &B_8C7A40AC;
    if (pos != NULL) {
        t->pos = *pos;
    }
    t->ang = ang;
    t->flag = 0;
    t->flag2 = 0;
    if (func_8C0DDE1C(0) == 4) {
        t->flag = 1;
        if (func_8C0DFE86(&a)) {
            t->flag2 = 1;
            t->ang2 = a;
        }
    }
    if (!func_8C0DE540()) {
        t->flag = 0;
        t->flag2 = 0;
        return NULL;
    }
    return t;
}

AMY_TARGETWK *func_8C0DFF4A(void)
{
    return &B_8C7A40AC;
}
