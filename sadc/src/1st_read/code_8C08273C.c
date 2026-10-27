#include "common.h"
#include "ninja.h"
#include "task.h"

/* Collision work (SA1 DC layout, see docs/matching_tips.md) */
typedef struct colliwk {
    Uint16 id;                  /* 0x00 */
    Uint8 pad02[0x0E];
    task *mytask;               /* 0x10 */
    Uint8 pad14[0x02];
    Sint16 hit_num;             /* 0x16 */
    struct colliwk *hit_cwp;    /* 0x18 */
} colliwk;

extern Angle func_8C085A50(Float y, Float x);
extern Sint32 GetPlayerNumber(void);
extern Sint32 func_8C049A4E(void);
extern Uint16 GetStageNumber(void);
extern void func_8C0585BA(void);
extern Sint32 func_8C09068E(Sint32 n);
extern Sint32 func_8C049A5A(Sint32 n);

extern Sint32 B_8C78C24C;
extern Uint8 B_8C78B45A;

void func_8C08273C(NJS_POINT3 *v, Float *r, Angle *ang)
{
    if (v->x == 0.0f && v->z == 0.0f) {
        if (r) {
            *r = 0.0f;
        }
        if (ang) {
            *ang = 0;
        }
    } else {
        if (r) {
            *r = 1.0f;
        }
        if (ang) {
            *ang = func_8C085A50(v->z, v->x) + 0x8000;
        }
    }
}

Sint32 func_8C0827C4(task *tp1, task *tp2)
{
    taskwk *twp1 = tp1->twp;
    taskwk *twp2 = tp2->twp;

    if (tp1 && tp2) {
        if ((twp1->flag & 0x408) == 0x408) {
            if (((colliwk *)twp1->cwp)->hit_cwp->mytask == tp2 && (twp2->flag & 0x400)) {
                return 1;
            }
        } else if ((twp2->flag & 0x408) == 0x408) {
            if (((colliwk *)twp2->cwp)->hit_cwp->mytask == tp1 && (twp1->flag & 0x400)) {
                return 1;
            }
        }
    }
    return 0;
}

Sint32 func_8C0829E6(void);

#ifdef NON_MATCHING
/*
 * Byte-identical to the original (all instructions and the 34-entry jump table,
 * checked against the .lis), but tools/shcdiff.py cannot evaluate the
 * ".DATA.W Lcase-Lbase" jump-table entries behind the MOVA and always reports
 * that one instruction as differing, which would fail the splice step.
 */
Sint32 func_8C082820(void)
{
    Sint32 ret;
    Sint32 flag;
    Sint32 stg;
    Sint32 act;
    Sint32 level;

    if (B_8C78C24C || GetPlayerNumber() || func_8C049A4E() == 1) {
        return 0;
    }
    flag = func_8C0829E6();
    stg = GetStageNumber();
    act = stg & 0xFF;
    level = (stg >> 8) & 0xFF;
    switch (level) {
    case 0:
        ret = 0;
        break;
    case 1:
        if (flag) {
            ret = 1;
        } else {
            ret = 0;
        }
        break;
    case 2:
        ret = 1;
        break;
    case 9:
        if (act == 2 || act == 3) {
            ret = 0;
        } else {
            ret = 1;
        }
        break;
    case 8:
        if (act == 0 || act == 1) {
            ret = 1;
        } else {
            ret = 0;
        }
        break;
    case 3:
        if (flag) {
            if (act == 0) {
                ret = 1;
                func_8C0585BA();
            } else if (act == 1) {
                ret = 1;
            } else {
                ret = 0;
            }
        } else {
            ret = 0;
        }
        break;
    case 4:
        if (flag) {
            if (act == 0 || act == 2) {
                ret = 1;
            } else {
                ret = 0;
            }
        } else {
            ret = 0;
        }
        break;
    case 5:
        if (flag) {
            if (act == 0 || act == 1) {
                ret = 1;
            } else {
                ret = 0;
            }
        } else {
            ret = 0;
        }
        break;
    case 6:
        ret = 1;
        break;
    case 7:
        if (flag) {
            if (act == 0 || act == 1) {
                ret = 1;
            } else {
                ret = 0;
            }
        } else {
            ret = 0;
        }
        break;
    case 10:
        if (flag) {
            ret = 1;
        } else {
            ret = 0;
        }
        break;
    case 33:
    default:
        ret = 0;
        break;
    }
    return ret;
}
#endif

Sint32 func_8C0829E6(void)
{
    if (B_8C78B45A == 1) {
        return 1;
    }
    if (func_8C09068E(0x48)) {
        return 1;
    }
    if (func_8C049A5A(0x100) && !func_8C049A5A(0x1A)) {
        return 1;
    }
    if (func_8C09068E(0xA6) && !func_8C049A5A(0xD)) {
        return 1;
    }
    return 0;
}
