#include "common.h"
#include "sa1/sa1.h"

/*
 * Amy (1ST_READ chunk 8C0DCBA0-8C0DDA00, functions 1-6 of 12; 7-9 are in
 * code_8C0DD166.c).
 *  func_8C0DCBA0  mode-machine check (AmyModeMachine, (twp, pwp)): when
 *                 pwp->free.b[4] != 1, the task B_8C7A4098 exists and the
 *                 button D_8C19E38C is pressed: stop, free that task,
 *                 mode 17 / action 38, SE 0x2F4, return 1.
 *  func_8C0DCC1A  mode-machine check (AmyModeMachine, (twp, pwp)): when
 *                 pwp->free.b[4] == 1 and D_8C19E38C | D_8C19E390 is
 *                 pressed: stop, free.f[5] = length of action 41 - 10,
 *                 mode 18 / action 41, SE 0x2F4, free.b[4] = 0, return 1.
 *  func_8C0DCCA8  per-frame hammer collision (AmyMain, (twp, pwp)): attack
 *                 levels of shape 0 (3/3 with item 0x8000, 0/1 in mode 48,
 *                 else 0/0), shape 1 off; while action 82/86/89/95 plays,
 *                 shape 0 gets 0/3, shape 1 is turned on with radius 9 (7.5
 *                 when equipment | 0x80 is 0, which never happens) and placed
 *                 on node 36 of the action (hammer head) through the player
 *                 matrix (center of shape 0, rotations Z, X, 0x8000 - Y);
 *                 func_8C0DF630 (hammer effect) on logic step 0; then
 *                 func_8C0DE7DC.
 *  func_8C0DCE94  mode-machine check (AmyModeMachine, (twp, pwp)): stick
 *                 rotation detector in pwp->free.b[0..3] (b[0] active,
 *                 b[1] accumulated turn, b[2] last stick angle >> 8, b[3]
 *                 last turn step); returns 1 once b[1] reaches 28 while
 *                 the stick keeps turning in one direction.
 *  func_8C0DD03C  exec of an effect spawner task (CreateElementalTask(2, 6),
 *                 AmyModeMachine): wtimer frames, each spawning a child
 *                 task (func_8C0DFAF8 or func_8C0DFD3E by btimer) at its
 *                 position and angle x/z.
 *  func_8C0DD0DE  (AmyMain, (twp, mwp, pwp)): unless twp->flag 0x4000,
 *                 turn twp->ang.y towards the stick direction (camera
 *                 relative; the current heading without stick input) plus
 *                 pwp->free.w[4] * 0x400, by 0x10 per frame.
 *
 * Matching notes:
 *  - SHC gives a stack home (written, never read back) to locals assigned
 *    inside an expression: tp in 8C0DCBA0, n/ctp in 8C0DD03C, the colliwk
 *    pointers in 8C0DCCA8. In 8C0DCCA8 the "if (0)" after the switch makes
 *    info/cwp live across the calls (info -> R12, cwp -> stack slot), as in
 *    the original; it is dead code.
 *  - 8C0DCBA0: the press test must be an inner if (an && chain swaps
 *    R2/R3 after FreeTask).
 *  - 8C0DCCA8: matrices passed as (NJS_MATRIX_PTR)&B_x (plain B_x is CSE'd
 *    into a callee-saved register; the original reloads it from the pool);
 *    rotations in the "c ? f(m, c) : (void)0" form (CSE temporary in the
 *    stack slot @R15).
 *  - 8C0DCE94: "if (pwp->free.b[1])" (not "!= 0") and "(abs = turn) < 0"
 *    decide the register allocation.
 *  - 8C0DD03C: the local table is an array (the original had an auto
 *    initializer { func_8C0DFAF8, func_8C0DFD3E }, whose template is
 *    D_8C15C7F4); it is filled by a struct copy from that extern.
 *  - 8C0DD0DE: early return; "ofs = ofs << 10" and the ang local give the
 *    original R12/R13 assignment and scratch registers.
 */

/* free.b[4] (0x80) flag; B_8C7A4098 task (Amy chunk 8C0DEEA0) */
extern task *func_8C0DF0CA(void);                   /* returns B_8C7A4098 (code_8C0DEEA0.c) */
extern void func_8C0DF630(NJS_POINT3 *pos);         /* hammer effect at pos (chunk 8C0DF540) */
extern void func_8C0DE7DC(taskwk *twp, playerwk *pwp); /* chunk 8C0DE540 */
extern void func_8C0DFAF8(task *tp);
extern void func_8C0DFD3E(task *tp);

extern NJS_MATRIX B_8C7A4018;           /* work matrix (player base matrix) */
extern NJS_MATRIX B_8C7A4058;           /* node matrix out of func_8C0583A0 */

/*
 * Initializer template of func_8C0DD03C's local table:
 * { func_8C0DFAF8, func_8C0DFD3E } (data 8C15C7F4, code pointers: the PC
 * loader must remap them).
 */
typedef struct {
    TaskFuncPtr exec[2];
} AMY_EFFECT_EXECS;
SA1_ASSERT_SIZE(AMY_EFFECT_EXECS, 0x08);
extern AMY_EFFECT_EXECS D_8C15C7F4;

Sint32 func_8C0DCBA0(taskwk *twp, playerwk *pwp)
{
    task *tp;

    if (pwp->free.b[4] != 1 && (tp = func_8C0DF0CA()) != NULL) {
        if (B_8C754C08[(Uint8)twp->counter.b[0]].press & D_8C19E38C) {
            pwp->spd.x = 0.0f;
            pwp->spd.z = 0.0f;
            FreeTask(tp);
            twp->mode = 17;
            pwp->mj.reqaction = 38;
            func_8C0940DA(0x2F4, 0, 0, 0);
            return 1;
        }
    }
    return 0;
}

Sint32 func_8C0DCC1A(taskwk *twp, playerwk *pwp)
{
    if (pwp->free.b[4] == 1
        && (B_8C754C08[(Uint8)twp->counter.b[0]].press & (D_8C19E38C | D_8C19E390))) {
        pwp->spd.x = 0.0f;
        pwp->spd.z = 0.0f;
        pwp->free.f[5] = (Float)AmyActionList[41].actptr->motion->nbFrame - 10.0f;
        twp->mode = 18;
        pwp->mj.reqaction = 41;
        func_8C0940DA(0x2F4, 0, 0, 0);
        pwp->free.b[4] = 0;
        return 1;
    }
    return 0;
}

void func_8C0DCCA8(taskwk *twp, playerwk *pwp)
{
    NJS_POINT3 v;
    NJS_POINT3 out;
    colliwk *cwp;
    colliwk *cwp1;
    colliwk *cwp2;
    CCL_INFO *info;
    Sint32 lv1;
    Sint32 lv2;

    if (twp->cwp != NULL) {
        (info = (cwp = CWP(twp))->info)[1].attr |= 0x10;
        twp->flag &= ~0x400;
        if (pwp->item & 0x8000) {
            lv1 = 3;
            lv2 = 3;
        } else if (twp->mode == 48) {
            lv1 = 0;
            lv2 = 1;
        } else {
            lv1 = 0;
            lv2 = 0;
        }
        info->flag = (info->flag & 0xFC) | lv1;
        info->flag = (info->flag & 0xF3) | (lv2 << 2);
    }
    switch (pwp->mj.reqaction) {
    case 82:
    case 86:
    case 89:
    case 95:
        (cwp1 = CWP(twp))->info->flag = (CWP(twp)->info->flag & 0xFC) | 0;
        (cwp2 = CWP(twp))->info->flag = (CWP(twp)->info->flag & 0xF3) | (3 << 2);
        CWP(twp)->info[1].attr &= ~0x10;
        /* "equipment | 0x80" in the original (always true; "&" was probably
         * meant): written as 1 on the PC to keep -Wtautological-bitwise-compare quiet */
        if (SH_ONLY(pwp->equipment | 0x80) PC_ONLY(1)) {
            CWP(twp)->info[1].a = 9.0f;
        } else {
            CWP(twp)->info[1].a = 7.5f;
        }
        twp->flag |= 0x400;
        njUnitMatrix((NJS_MATRIX_PTR)&B_8C7A4018);
        njTranslateV((NJS_MATRIX_PTR)&B_8C7A4018, &CWP(twp)->info->center);
        twp->ang.z ? njRotateZ((NJS_MATRIX_PTR)&B_8C7A4018, twp->ang.z) : (void)0;
        twp->ang.x ? njRotateX((NJS_MATRIX_PTR)&B_8C7A4018, twp->ang.x) : (void)0;
        0x8000 - twp->ang.y ? njRotateY((NJS_MATRIX_PTR)&B_8C7A4018, 0x8000 - twp->ang.y) : (void)0;
        if (func_8C0583A0((NJS_MATRIX_PTR)&B_8C7A4018, AmyActionList[pwp->mj.reqaction].actptr,
                pwp->mj.nframe, 36, (NJS_MATRIX_PTR)&B_8C7A4058) == 1) {
            v.x = 0.0f;
            v.y = 0.0f;
            v.z = 0.0f;
            njCalcPoint((NJS_MATRIX_PTR)&B_8C7A4058, &v, &out);
            CWP(twp)->info[1].center = out;
            if (B_8C754EC8) {
                func_8C0DF630(&out);
            }
        }
        func_8C0DE7DC(twp, pwp);
        break;
    }
    if (0) {
        /* dead: only extends the live ranges of info and cwp (see above) */
        info->flag = (Uint8)cwp->flag;
    }
}

Sint32 func_8C0DCE94(taskwk *twp, playerwk *pwp)
{
    Float x;
    Float y;
    Sint32 ang;
    Sint8 a;
    Sint8 turn;
    Sint8 abs;

    x = (Float)(B_8C754C08[(Uint8)twp->counter.b[0]].x1 << 8);
    y = (Float)(B_8C754C08[(Uint8)twp->counter.b[0]].y1 << 8);
    if (x <= 3072.0f && x >= -3072.0f && y <= 3072.0f && y >= -3072.0f) {
        pwp->free.b[1] = 0;
        pwp->free.b[3] = 0;
        pwp->free.b[0] = 0;
    } else {
        ang = SA1_FTOI(atan2f(y, x) * 10430.381f);
        a = ang >> 8;
        if (pwp->free.b[0] != 0) {
            turn = SubAngle(pwp->free.b[2] << 8, ang) >> 8;
            if ((abs = turn) < 0) {
                abs = -turn;
            }
            if (abs < 40) {
                if (pwp->free.b[1]) {
                    if (turn == 0) {
                        if ((pwp->free.b[1] -= 2) < 0) {
                            pwp->free.b[1] = 0;
                        }
                    } else if ((pwp->free.b[3] ^ turn) > 0) {
                        pwp->free.b[1] += abs >> 2;
                        pwp->free.b[2] = a;
                        pwp->free.b[3] = turn;
                        if (pwp->free.b[1] >= 28) {
                            return 1;
                        }
                    } else {
                        pwp->free.b[1] = 0;
                        pwp->free.b[0] = 0;
                    }
                } else {
                    pwp->free.b[1] += (abs >> 2) & 0xF;
                    pwp->free.b[2] = a;
                    pwp->free.b[3] = turn;
                }
            } else {
                pwp->free.b[1] = 0;
                pwp->free.b[0] = 0;
            }
        } else {
            pwp->free.b[0] = 1;
            pwp->free.b[1] = 0;
            pwp->free.b[2] = a;
            pwp->free.b[3] = 0;
        }
    }
    return 0;
}

void func_8C0DD03C(task *tp)
{
    taskwk *twp;
    TaskFuncPtr execs[2];
    Uint8 n;
    task *ctp;

    twp = tp->twp;
    if (--twp->wtimer == 0) {
        FreeTask(tp);
        return;
    }
    *(AMY_EFFECT_EXECS *)execs = D_8C15C7F4;
    if ((n = twp->btimer) < 2 && (ctp = CreateElementalTask(2, 6, execs[n])) != NULL) {
        ctp->twp->pos = twp->pos;
        ctp->twp->ang.x = twp->ang.x;
        ctp->twp->ang.z = twp->ang.z;
    }
}

void func_8C0DD0DE(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    Angle ofs;
    PDS_PERIPHERAL *per;
    Float x;
    Float y;
    Angle ang;

    ofs = pwp->free.w[4];
    ofs = ofs << 10;
    if (twp->flag & 0x4000) {
        return;
    }
    if (GetAnalog(twp, NULL, NULL) == 0) {
        mwp->ang_aim.y = twp->ang.y + ofs;
    } else {
        per = &B_8C754C08[(Uint8)twp->counter.b[0]];
        x = (Float)(per->x1 << 8);
        y = (Float)(per->y1 << 8);
        ang = SA1_FTOI(atan2f(y, x) * 10430.381f);
        mwp->ang_aim.y = ang + ofs - camera_twp->ang.y;
    }
    twp->ang.y = AdjustAngle(twp->ang.y, mwp->ang_aim.y, 0x10);
}
