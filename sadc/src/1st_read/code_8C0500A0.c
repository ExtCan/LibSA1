#include "sa1/sa1.h"

/*
 * Particle system of the effect code (1ST_READ chunk 8C04FEA0-8C0502A0,
 * functions 6-10; 8C04FEA0-8C050018 are other work and stubbed here):
 *   func_8C0500A0  default particle step: move (8C04FEF6), advance the
 *                  sprite animation (8C050470); 0 when it has ended
 *   func_8C0500CC  exec: step every live particle, release the dead ones
 *                  (8C04FFE4), then draw (8C050018)
 *   func_8C05011E  exec with a user step function (PTCLSYS.func)
 *   func_8C050172  dest: free the three particle arrays
 *   func_8C050190  create the particle task (caller: E-102 sparks 8C0D6960)
 *
 * The system state is MAlloc'ed by 8C050190 and kept in task.awp. Particles
 * live in a pool ptcl[max]; act[0..num-1] are the indices of the live ones
 * and freeidx[num..max-1] (a stack, see 8C04FF58/8C04FFE4) the free ones.
 */

/* Sprite animation key list (EFF_SPRANIM in code_8C0D6960.c, ANIMKEY in
 * code_8C0502A0.c); only passed through here. */
typedef struct EFF_SPRANIM EFF_SPRANIM;

/* 16-byte sprite animation state (reset by 8C0503B6, advanced by 8C050470;
 * ANIMSTATE in code_8C0502A0.c) */
typedef struct {
    Float frame;                /* 0x00 */
    Float speed;                /* 0x04 1.0f after 8C0503B6 */
    Sint32 index;               /* 0x08 current key */
    Sint32 loop;                /* 0x0C loop counter (8C050470, key id -2) */
} PTCL_ANIM;                    /* 0x10 */

/* One particle (pool stride 0x30: MOV/SHLL/ADD/SHLL2/SHLL2 = idx * 48) */
typedef struct {
    NJS_POINT3 pos;             /* 0x00 += spd each frame (8C04FEF6) */
    NJS_VECTOR spd;             /* 0x0C damped by accel.x, accel.y subtracted from spd.y (8C04FEF6) */
    Float scl;                  /* 0x18 1.0f at spawn; sprite size factor in 8C050540 */
    Float unk1C;                /* 0x1C spawn argument (8C04FF58 FR5) */
    PTCL_ANIM anim;             /* 0x20 */
} PTCL;                         /* 0x30 */

/* Particle step: R4 particle, R5 &sys->accel, R6 sys->anim; 0 = particle died */
typedef Sint32 (*PTCL_FUNC)(PTCL *p, NJS_POINT2 *accel, EFF_SPRANIM *anim);

/* Particle system (task.awp, MAlloc(0x20) by 8C050190) */
typedef struct {
    Sint16 num;                 /* 0x00 live particles */
    Sint16 max;                 /* 0x02 pool size */
    Sint16 *freeidx;            /* 0x04 free index stack, freeidx[num..max-1] */
    Sint16 *act;                /* 0x08 live particle indices act[0..num-1] */
    PTCL *ptcl;                 /* 0x0C pool */
    PTCL_FUNC func;             /* 0x10 user step (exec 8C05011E) or NULL (exec 8C0500CC) */
    NJS_POINT2 accel;           /* 0x14 x: speed damping, y: gravity */
    EFF_SPRANIM *anim;          /* 0x1C */
} PTCLSYS;                      /* 0x20 */

SA1_ASSERT_SIZE(PTCL_ANIM, 0x10);
SA1_ASSERT_SIZE(PTCL, 0x30);
SA1_ASSERT_OFFSET(PTCL, anim, 0x20);
SA1_ASSERT_SIZE(PTCLSYS, 0x20);
SA1_ASSERT_OFFSET(PTCLSYS, func, 0x10);
SA1_ASSERT_OFFSET(PTCLSYS, accel, 0x14);
SA1_ASSERT_OFFSET(PTCLSYS, anim, 0x1C);

Sint32 func_8C050470(PTCL_ANIM *st, EFF_SPRANIM *anim);  /* advance the animation; -1 = ended */

ASM_STUB(void func_8C04FEF6(PTCL *p, NJS_POINT2 *accel))
ASM_STUB(void func_8C04FFE4(task *tp, Sint32 i))
ASM_STUB(void func_8C050018(task *tp))

Sint32 func_8C0500A0(PTCL *p, NJS_POINT2 *accel, EFF_SPRANIM *anim)
{
    func_8C04FEF6(p, accel);
    if (func_8C050470(&p->anim, anim) == -1) {
        return 0;
    }
    return 1;
}

void func_8C0500CC(task *tp)
{
    PTCLSYS *sys;
    PTCL *p;
    Sint32 i;

    sys = tp->awp;
    for (i = 0; i < sys->num; i++) {
        p = &sys->ptcl[sys->act[i]];
        if (func_8C0500A0(p, &sys->accel, sys->anim) == 0) {
            func_8C04FFE4(tp, i--);
        }
    }
    func_8C050018(tp);
}

void func_8C05011E(task *tp)
{
    PTCLSYS *sys;
    PTCL *p;
    Sint32 i;

    sys = tp->awp;
    for (i = 0; i < sys->num; i++) {
        p = &sys->ptcl[sys->act[i]];
        if (sys->func(p, &sys->accel, sys->anim) == 0) {
            func_8C04FFE4(tp, i--);
        }
    }
    func_8C050018(tp);
}

void func_8C050172(task *tp)
{
    PTCLSYS *sys;

    sys = tp->awp;
    MFree(sys->freeidx);
    MFree(sys->act);
    MFree(sys->ptcl);
}

/*
 * The signed (Sint32)sizeof(Sint16) products are needed for matching: SHC then
 * shares num * 2 between the two MAllocs (spilled to @(12,R15)); with the
 * unsigned sizeof product it recomputes it.
 */
task *func_8C050190(Sint32 num, NJS_POINT2 *accel, EFF_SPRANIM *anim, PTCL_FUNC func)
{
    task *tp;
    PTCLSYS *sys;
    Sint32 i;

    if ((tp = CreateElementalTask(2, 4, func_8C0500CC)) != NULL) {
        sys = MAlloc(sizeof(PTCLSYS));
        if (sys != NULL) {
            tp->awp = sys;
            sys->num = 0;
            sys->max = num;
            sys->freeidx = MAlloc(num * (Sint32)sizeof(Sint16));
            sys->act = MAlloc(num * (Sint32)sizeof(Sint16));
            sys->ptcl = MAlloc(num * sizeof(PTCL));
            sys->accel = *accel;
            sys->anim = anim;
            sys->func = func;
            if (func != NULL) {
                tp->exec = func_8C05011E;
            }
            for (i = 0; i < num; i++) {
                sys->freeidx[i] = i;
            }
            tp->disp = func_8C050018;
            tp->dest = func_8C050172;
        } else {
            DestroyTask(tp);
            tp = NULL;
        }
    }
    return tp;
}
