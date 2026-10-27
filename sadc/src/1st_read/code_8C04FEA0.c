/*
 * Particle effect pool, part 1 (1ST_READ chunk 8C04FEA0-8C0502A0,
 * functions 1-5):
 *   func_8C04FEA0  initialise a particle motion (pos, speed, scale)
 *   func_8C04FEF6  step a particle motion: pos += spd, drag + gravity
 *   func_8C04FF58  emit one particle from a pool task
 *   func_8C04FFE4  free the i-th active particle of a pool task
 *   func_8C050018  pool task display: draw every active particle
 *
 * The pool task is created by func_8C050190(num, accel, anim, move): it
 * MAllocs the 0x20-byte EFF_PARTICLEWK below into task.awp, two Sint16
 * index lists of num entries and num 0x30-byte particles; exec is
 * func_8C0500CC (default motion func_8C0500A0) or, with a move callback,
 * func_8C05011E, disp func_8C050018, dest func_8C050172.
 */
#include "sa1/sa1.h"

/* Sprite list entry (0x2C, defined in code_8C0D6960.c); only passed on here. */
typedef struct EFF_SPRANIM EFF_SPRANIM;

/*
 * Sprite animation state (0x10): reset by func_8C0503B6, frame set by
 * func_8C0502A0, stepped by func_8C050470.
 */
typedef struct {
    Float frame;                /* 0x00 */
    Float speed;                /* 0x04 func_8C0503B6: 1.0f */
    Sint32 no;                  /* 0x08 current EFF_SPRANIM entry */
    Uint32 loop;                /* 0x0C loop counter (8C050470 compares it unsigned) */
} EFF_ANIMWK;

/* Particle motion (0x20); also embedded at +0x08 of the 8C050400 sprite work. */
typedef struct {
    NJS_POINT3 pos;             /* 0x00 */
    NJS_VECTOR spd;             /* 0x0C */
    Float unk18;                /* 0x18 1.0f by default (EFF_SPRPARAM.unk10) */
    Float scl;                  /* 0x1C */
} EFF_MOVE;

typedef struct {
    EFF_MOVE mv;                /* 0x00 */
    EFF_ANIMWK anim;            /* 0x20 */
} EFF_PARTICLE;                 /* 0x30 */

typedef Sint32 (*EFF_MOVEFUNC)(EFF_PARTICLE *p, NJS_POINT2 *accel, EFF_SPRANIM *anim);

/* Pool work in task.awp (func_8C050190: MAlloc(0x20)). */
typedef struct {
    Sint16 num;                 /* 0x00 active particles */
    Sint16 max;                 /* 0x02 */
    Sint16 *freeid;             /* 0x04 free particle numbers (stack, top at num) */
    Sint16 *active;             /* 0x08 active particle numbers */
    EFF_PARTICLE *buf;          /* 0x0C max particles */
    EFF_MOVEFUNC move;          /* 0x10 optional motion callback (exec func_8C05011E) */
    NJS_POINT2 accel;           /* 0x14 x: drag factor, y: gravity */
    EFF_SPRANIM *anim;          /* 0x1C */
} EFF_PARTICLEWK;               /* 0x20 */

SA1_ASSERT_SIZE(EFF_ANIMWK, 0x10);
SA1_ASSERT_SIZE(EFF_MOVE, 0x20);
SA1_ASSERT_SIZE(EFF_PARTICLE, 0x30);
SA1_ASSERT_SIZE(EFF_PARTICLEWK, 0x20);
SA1_ASSERT_OFFSET(EFF_PARTICLE, anim, 0x20);
SA1_ASSERT_OFFSET(EFF_PARTICLEWK, move, 0x10);
SA1_ASSERT_OFFSET(EFF_PARTICLEWK, accel, 0x14);
SA1_ASSERT_OFFSET(EFF_PARTICLEWK, anim, 0x1C);

/* same chunk, called through the literal pool */
void func_8C0503B6(EFF_ANIMWK *aw);
void func_8C0502A0(EFF_ANIMWK *aw, EFF_SPRANIM *anim, Float frame);
void func_8C050540(NJS_POINT3 *pos, EFF_MOVE *mv, EFF_ANIMWK *aw, EFF_SPRANIM *anim);

void func_8C04FEA0(EFF_MOVE *mv, NJS_POINT3 *pos, NJS_VECTOR *spd)
{
    if (pos != NULL) {
        mv->pos.x = pos->x;
        mv->pos.y = pos->y;
        mv->pos.z = pos->z;
    } else {
        mv->pos.x = mv->pos.y = mv->pos.z = 0.0f;
    }
    if (spd != NULL) {
        mv->spd.x = spd->x;
        mv->spd.y = spd->y;
        mv->spd.z = spd->z;
    } else {
        mv->spd.x = mv->spd.y = mv->spd.z = 0.0f;
    }
    mv->unk18 = 1.0f;
    mv->scl = 0.0f;
}

void func_8C04FEF6(EFF_MOVE *mv, NJS_POINT2 *accel)
{
    mv->pos.x = mv->pos.x + mv->spd.x;
    mv->pos.y = mv->pos.y + mv->spd.y;
    mv->pos.z = mv->pos.z + mv->spd.z;
    mv->spd.x -= mv->spd.x * accel->x;
    mv->spd.y -= mv->spd.y * accel->x - accel->y;
    mv->spd.z -= mv->spd.z * accel->x;
}

void func_8C04FF58(task *tp, NJS_POINT3 *pos, NJS_VECTOR *spd, Float frame, Float scl)
{
    EFF_PARTICLEWK *wk;
    EFF_PARTICLE *p;

    wk = (EFF_PARTICLEWK *)tp->awp;
    if (wk->num < wk->max) {
        wk->active[wk->num] = wk->freeid[wk->num];
        p = &wk->buf[wk->active[wk->num]];
        p->mv.pos.x = pos->x;
        p->mv.pos.y = pos->y;
        p->mv.pos.z = pos->z;
        p->mv.spd.x = spd->x;
        p->mv.spd.y = spd->y;
        p->mv.spd.z = spd->z;
        p->mv.unk18 = 1.0f;
        p->mv.scl = scl;
        func_8C0503B6(&p->anim);
        func_8C0502A0(&p->anim, wk->anim, frame);
        wk->num++;
    }
}

void func_8C04FFE4(task *tp, Sint32 i)
{
    EFF_PARTICLEWK *wk;

    wk = (EFF_PARTICLEWK *)tp->awp;
    if (wk->num > 0) {
        wk->num--;
        wk->freeid[wk->num] = wk->active[i];
        wk->active[i] = wk->active[wk->num];
    }
}

void func_8C050018(task *tp)
{
    EFF_PARTICLEWK *wk;
    EFF_PARTICLE *p;
    Sint32 i;

    wk = (EFF_PARTICLEWK *)tp->awp;
    for (i = 0; i < wk->num; i++) {
        p = &wk->buf[wk->active[i]];
        func_8C050540(&p->mv.pos, &p->mv, &p->anim, wk->anim);
    }
}
