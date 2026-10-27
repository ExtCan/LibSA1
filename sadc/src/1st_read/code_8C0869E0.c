#include "common.h"
#include "ninja.h"
#include "task.h"

/* Ninja sprite (njDrawSprite3D argument) */
typedef struct {
    NJS_POINT3 p;               /* 0x00 */
    Float sx;                   /* 0x0C */
    Float sy;                   /* 0x10 */
    Angle ang;                  /* 0x14 */
    NJS_TEXLIST *tlist;         /* 0x18 */
    void *tanim;                /* 0x1C */
} NJS_SPRITE;

/* Collision work (taskwk.cwp) */
typedef struct colliwk {
    Uint16 id;                  /* 0x00 */
    Uint16 flag;                /* 0x02 */
    Uint8 pad04[0x0C];
    task *mytask;               /* 0x10 */
    Uint8 pad14[0x04];
    struct colliwk *hit_cwp;    /* 0x18 */
} colliwk;

/* Player work (func_8C07CFDE) */
typedef struct {
    Sint16 unk0;                /* 0x00 */
    Sint16 unk2;                /* 0x02 */
    Sint16 unk4;                /* 0x04 */
    Sint16 equipment;           /* 0x06 */
} UNK_pwk;

extern Sint32 D_8C1BF0F8;
extern Uint8 D_8C1BF0BC[];
extern NJS_SPRITE B_8C79897C;
extern Sint16 D_8C19B468;
extern taskwk *D_8C18B7EC;
extern Uint8 D_8C1A21DC[];
extern Sint32 B_8C754EC8;
extern NJS_TEXLIST D_8C1BF014;

extern Sint32 func_8C0919CC(task *tp);
extern void CCL_Init(task *tp, void *info, Uint8 id, Sint32 unused, Uint8 nbInfo);
extern Float func_8C091C20(Float x, Float y, Float z, Angle3 *ang);
extern void func_8C08776C(task *tp);
extern UNK_pwk *func_8C07CFDE(Uint8 pno);
extern task *func_8C07CFA0(Uint8 pno);
extern Sint32 GetPlayerNumber(void);
extern void func_8C08A198(Sint16 n);
extern void func_8C0940DA(Sint32 a, Sint32 b, Sint32 c, Sint32 d);
extern void func_8C02F63C(taskwk *twp);
extern Float func_8C02CF68(Float x, Float y, Float z);
extern task *CreateElementalTask(Uint16 im, Sint32 level, TaskFuncPtr exec); /* canonical; sa1/engine.h follows later */
extern void func_8C0870EA(task *tp);
extern Sint32 rand(void); /* rand() */
extern void func_8C091BA4(task *tp);
extern void FreeTask(task *tp); /* FreeTask */
extern Sint32 func_8C038D00(Float x, Float y, Float z, Float r);
extern void func_8C092124(taskwk *twp, Float y);
extern void func_8C056926(void);
extern void func_8C05695C(Uint32 a, Uint32 b);
extern Sint32 func_8C099500(void);
extern Sint32 njPushMatrix(Float *m); /* NJS_MATRIX_PTR m */
extern void njTranslate(Float *m, Float x, Float y, Float z);
extern void njRotateY(Float *m, Angle ang);
extern void njRotateX(Float *m, Angle ang);
extern void func_8C03700C(void *model);
extern Sint32 njPopMatrix(Uint32 n);
extern void func_8C056938(void);
extern Sint32 func_8C04FE40(void);
extern void func_8C043A20(Float a, Float r, Float g, Float b);
extern void njColorBlendingMode(Sint32 a, Sint32 b);
extern Sint32 njSetTexture(NJS_TEXLIST *texlist);
extern void njScale(Float *m, Float sx, Float sy, Float sz);
extern void njDrawSprite3D(NJS_SPRITE *sp, Sint32 n, Uint32 attr);

void func_8C086D00(task *tp);
void func_8C086DE0(task *tp);
void func_8C086DE6(taskwk *twp);
void func_8C08774C(taskwk *twp, NJS_SPRITE *sp);
Sint32 func_8C08789A(taskwk *twp);

/* Resets the counter that staggers the rings' sparkle timers. */
void func_8C0869E0(void)
{
    D_8C1BF0F8 = 0;
}

/* Ring object (exec). Mode 0 init, 1 idle/collect check, 2 collected, 3 free. */
void objexec_RING(task *tp)
{
    taskwk *twp;
    taskwk *ctwp;
    task *ctp;
    task *ptp;
    Float y;
    Float dx;
    Float dy;
    Float dz;

    twp = tp->twp;
    if (func_8C0919CC(tp)) {
        twp->timer.b[1] = 1;
        return;
    }
    switch (twp->mode) {
    case 0:
        CCL_Init(tp, D_8C1BF0BC, 7, 1, 1);
        if (twp->scl.x == -1.0f) {
            twp->smode = D_8C1BF0F8;
            D_8C1BF0F8++;
            if (D_8C1BF0F8 > 8) {
                D_8C1BF0F8 = 0;
            }
        } else {
            twp->smode = -1;
        }
        twp->scl.y = func_8C091C20(twp->pos.x, twp->pos.y, twp->pos.z, &twp->ang);
        twp->mode = 1;
        twp->counter.f = 0.0f;
        twp->scl.x = -2.0f;
        twp->scl.z = -4.0f;
        twp->id = 11;
        tp->dest = func_8C08776C;
        tp->disp = func_8C086D00;
        func_8C08774C(twp, &B_8C79897C);
        break;
    case 1:
        if (D_8C19B468 == 0) {
            if (((colliwk *)twp->cwp)->flag & 2) {
                twp->mode = 2;
                if (!(func_8C07CFDE(0)->equipment & 0x4000)) {
                    ptp = func_8C07CFA0(0);
                    if (GetPlayerNumber() != 2) {
                        func_8C08A198(1);
                        func_8C0940DA(7, 0, 0, 0);
                    } else if (((colliwk *)twp->cwp)->hit_cwp->mytask == ptp) {
                        func_8C08A198(1);
                        func_8C0940DA(7, 0, 0, 0);
                    }
                }
                tp->disp = func_8C086DE0;
            }
        }
        if (D_8C19B468 == 0 && !func_8C08789A(twp)) {
            func_8C02F63C(twp);
            twp->ang.y += 0x16C;
        }
        if (twp->smode >= 0) {
            twp->smode--;
            if (twp->smode == -1) {
                twp->smode = 8;
                dx = D_8C18B7EC->pos.x - twp->pos.x;
                dy = D_8C18B7EC->pos.y - twp->pos.y;
                dz = D_8C18B7EC->pos.z - twp->pos.z;
                if (func_8C02CF68(dx, dy, dz) < 160000.0f) {
                    y = func_8C091C20(twp->pos.x, twp->pos.y, twp->pos.z, &twp->ang);
                    dy = y - twp->scl.y;
                    /* SHC intrinsic: emits FABS */
                    if (_builtin_fabsf(dy) < 8.0f) {
                        twp->scl.y = y;
                        twp->pos.y += dy;
                    } else {
                        twp->mode = 3;
                        ctp = CreateElementalTask(10, 2, func_8C0870EA);
                        if (ctp) {
                            ctwp = ctp->twp;
                            ctwp->pos = twp->pos;
                            ctwp->pos.y -= 10.0f;
                            ctwp->ang.y = rand() * 1.9999999f;
                        }
                    }
                }
            }
        }
        func_8C086D00(tp);
        break;
    case 2:
        func_8C086DE6(twp);
        break;
    case 3:
        if (tp->ocp) {
            func_8C091BA4(tp);
        } else {
            FreeTask(tp);
        }
        break;
    }
}

/* Ring display. */
void func_8C086D00(task *tp)
{
    taskwk *twp;

    twp = tp->twp;
    if (!func_8C038D00(twp->pos.x, twp->pos.y, twp->pos.z, 20.0f)) {
        return;
    }
    if ((Uint8)twp->btimer < 50) {
        if (twp->scl.y != -1000000.0f) {
            func_8C092124(twp, twp->scl.y);
        }
    }
    func_8C056926();
    func_8C05695C(0, 0x80000);
    func_8C099500();
    njPushMatrix(0);
    njTranslate(0, twp->pos.x, twp->pos.y, twp->pos.z);
    /* conditional expression (not an if): keeps the angle in a temporary
     * that SHC places on the stack, as in the original */
    twp->ang.y ? njRotateY(0, twp->ang.y) : (void)0;
    func_8C03700C(D_8C1A21DC);
    njPopMatrix(1);
    func_8C056938();
}

/* Display after collection; SHC drops the tail-call BRA because
 * func_8C086DE6 follows immediately. */
void func_8C086DE0(task *tp)
{
    func_8C086DE6(tp->twp);
}

/* Collected ring: draws three expanding sparkle sprites, then frees the ring. */
void func_8C086DE6(taskwk *twp)
{
    Sint32 sc[3];
    Sint32 i;
    Float s;
    Float t;

    if (!func_8C04FE40()) {
        twp->counter.f += 0.8f;
        twp->scl.x += 0.8f;
        twp->scl.z += 0.8f;
    }
    if (B_8C754EC8) {
        return;
    }
    func_8C043A20(0.0f, 0.0f, 0.0f, 0.0f);
    njColorBlendingMode(0, 8);
    njColorBlendingMode(1, 6);
    njSetTexture(&D_8C1BF014);
    sc[0] = twp->counter.f;
    sc[1] = twp->scl.x;
    sc[2] = twp->scl.z;
    for (i = 0; i < 3; i++) {
        if (sc[i] < 7.0f && sc[i] >= 0) {
            t = sc[i] * 0.05f;
            s = _builtin_fabsf(sc[i] * 0.05); /* double constant: not merged with 0.05f */
            njPushMatrix(0);
            njTranslate(0, twp->pos.x - i * 2.0f, twp->pos.y - t * 3.0f, twp->pos.z - i * 2.0f);
            D_8C18B7EC->ang.y ? njRotateY(0, D_8C18B7EC->ang.y) : (void)0;
            D_8C18B7EC->ang.x ? njRotateX(0, D_8C18B7EC->ang.x) : (void)0;
            njScale(0, s, s, s);
            njDrawSprite3D(&B_8C79897C, sc[i], 41);
            njPopMatrix(1);
        }
    }
    if (sc[2] > 7.0f) {
        twp->mode = 3;
    }
}

ASM_STUB(void func_8C08774C(taskwk *twp, NJS_SPRITE *sp))
ASM_STUB(Sint32 func_8C08789A(taskwk *twp))
