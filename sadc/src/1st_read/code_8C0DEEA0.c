/*
 * 1ST_READ 8C0DEEA0-8C0DF100 (chunk 8C0DEEA0): Amy, hammer-target objects.
 * Small collision objects placed from a per-stage table when the player is
 * Amy (character 5). One of them is remembered in B_8C7A4098 when the object
 * collides with a shape of id 0 (the player) facing it within 45 degrees and
 * the two are at nearly the same height (read by func_8C0DCBA0 through
 * func_8C0DF0CA).
 *  func_8C0DEEA0  disp: draws model D_8C4C850C at twp->pos
 *  func_8C0DEEE2  exec: freed without player 0; inactive farther than 400;
 *                 sets B_8C7A4098 on a facing player hit; draws, CCL entry
 *  func_8C0DEF90  init (exec of the new task): CCL_Init, exec/disp
 *  func_8C0DEFB6  creates one object at *pos (TRUE on success)
 *  func_8C0DF060  creates the objects of the current stage (Amy only)
 *  func_8C0DF0CA  returns B_8C7A4098
 * (func_8C0DF0D0, B_8C7A4098 = NULL, is the last function of the chunk.)
 */
#include "common.h"
#include "sa1/sa1.h"

/*
 * Per-stage object list D_8C4BF044 (game data, 8 bytes per entry on DC):
 * stage = GetStageNumber() value ((level << 8) | act), 0x7FFF ends the list;
 * num positions at pos (MOV.W @(2), MOV.L @(4); positions walked with ADD #12).
 * Holds a pointer: native layout on 64-bit hosts (the ripper converts it).
 */
typedef struct {
    Uint16 stage;               /* 0x00 */
    Uint16 num;                 /* 0x02 */
    NJS_POINT3 *pos;            /* DC 0x04 (native on 64-bit PC) */
} AMYOBJ_STAGE;                 /* DC 0x08 (pointer: native layout on 64-bit PC) */

SA1_ASSERT_SIZE(AMYOBJ_STAGE, 0x08);
SA1_ASSERT_OFFSET(AMYOBJ_STAGE, num, 0x02);
SA1_ASSERT_OFFSET(AMYOBJ_STAGE, pos, 0x04);

extern NJS_TEXLIST texlist_AMY;         /* 8C4BF3AC */
extern NJS_MODEL D_8C4C850C;            /* object model */
extern CCL_INFO D_8C4BEFB8[];           /* 2 shapes, CCL_Init id 4 */
extern AMYOBJ_STAGE D_8C4BF044[];       /* per-stage positions, ends at stage 0x7FFF */
extern task *B_8C7A4098;                /* object the player hit while facing it */

void func_8C0DEEA0(task *tp)
{
    if (B_8C754EC8 == 0) {
        njSetTexture(&texlist_AMY);
        njPushMatrix(NULL);
        njTranslateV(NULL, &tp->twp->pos);
        func_8C03700C(&D_8C4C850C);
        njPopMatrix(1);
    }
}

void func_8C0DEEE2(task *tp)
{
    taskwk *twp;
    taskwk *ptwp;
    colliwk *hcwp;
    taskwk *htwp;
    NJS_POINT3 v;
    Sint32 ang;

    twp = tp->twp;
    ptwp = playertwp[0];
    if (ptwp == NULL) {
        FreeTask(tp);
        return;
    }
    if (njDistanceP2P(&twp->pos, &ptwp->pos) > 400.0f) {
        return;
    }
    if (twp->flag & 8) {
        hcwp = CWP(twp)->hit_cwp;
        if (hcwp != NULL && hcwp->id == 0) {
            htwp = hcwp->mytask->twp;
            v = twp->pos;
            njSubVector(&v, &htwp->pos);
            ang = SA1_FTOI(atan2f(v.z, v.x) * 10430.381f);
            if (_builtin_fabsf(v.y) < 2.0f && DiffAngle(htwp->ang.y, ang) < 0x2000) {
                B_8C7A4098 = tp;
            }
        }
    }
    func_8C0DEEA0(tp);
    func_8C02F63C(twp);
}

void func_8C0DEF90(task *tp)
{
    CCL_Init(tp, D_8C4BEFB8, 4, 1, 2);
    tp->exec = func_8C0DEEE2;
    tp->disp = func_8C0DEEA0;
}

Bool func_8C0DEFB6(NJS_POINT3 *pos)
{
    task *tp;

    if ((tp = CreateElementalTask(2, 6, func_8C0DEF90)) != NULL) {
        tp->twp->pos = *pos;
        return TRUE;
    }
    return FALSE;
}

Bool func_8C0DF060(void)
{
    AMYOBJ_STAGE *sp;
    Uint16 stage;
    Sint32 n;
    NJS_POINT3 *pos;

    if (GetPlayerNumber() != 5) {
        return FALSE;
    }
    stage = GetStageNumber();
    for (sp = D_8C4BF044; sp->stage != stage; sp++) {
        if (sp->stage == 0x7FFF) {
            return FALSE;
        }
    }
    pos = sp->pos;
    for (n = sp->num; n > 0; n--) {
        if (!func_8C0DEFB6(pos)) {
            return FALSE;
        }
        pos++;
    }
    B_8C7A4098 = NULL;
    return TRUE;
}

task *func_8C0DF0CA(void)
{
    return B_8C7A4098;
}
