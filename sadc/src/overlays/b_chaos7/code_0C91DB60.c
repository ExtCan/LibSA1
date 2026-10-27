/*
 * B_CHAOS7 0C91DB60-0C91E664 (chunk asm/overlays/b_chaos7/text/0C91DB60.src,
 * all 11 functions): Super Sonic's effect tasks. They are copies of Sonic's
 * 1ST_READ effect tasks, built with the Super Sonic texlists (slices of the
 * SUPERSONIC NJS_TEXNAME array 0x0C901B1C) instead of the SON_EFF ones.
 * The creators are the B_CHAOS7 wrappers 0C9004B6 / 0C9004D4 / 0C9004F2
 * (super variants of 8C0B3FC0 / 8C0B3FDA / 8C0B48B4).
 *
 *  func_0C91DB60  <- 8C0B41E0  jump-ball after-image set-up (0C9004B6)
 *  func_0C91DCB2  <- 8C0B4332  homing start ball set-up (0C9004D4)
 *  func_0C91DDB6  <- 8C0B4600  after-image disp (follows the player)
 *  func_0C91DE82  <- 8C0B46CC  after-image exec: 5 frames
 *  func_0C91DED2  <- 8C0B4724  start ball disp (uniform scale scl.x)
 *  func_0C91DFE0  <- 8C0B4820  start ball exec: 10 frames, scl.x -= 0.4
 *  func_0C91E016  <- 8C0B4860  direction vector -> angles
 *  func_0C91E06A  <- 8C0B48E0  trail init (copies Sonic_Effect_Trail)
 *  func_0C91E1D2  <- 8C0B4A48  trail disp
 *  func_0C91E26A  <- 8C0B4B20  trail exec
 *  func_0C91E5E2  <- 8C0B4E62  trail dest
 *
 * Differences from the 1ST_READ originals (everything else is instruction
 * for instruction the same, apart from the pools):
 *  - the exec functions 0C91DE82, 0C91DFE0 and 0C91E26A end a finished task
 *    with FreeTask(tp) only; the originals also clear tp->disp;
 *  - 0C91E26A has no "scl.x >= 5: FreeTask" test on frame 1, and it sets
 *    flag 8 (short trail, second texlist) only when free.b[0] has 0x04 set
 *    and 0x02 clear. The original sets it unless both bits are set.
 *  - the pool words: D_0CA6B2D0 / D_0CA6B2D8 (ss_ball_a / ss_ball_b) for the
 *    two random ball texlists, D_0CA6B2E0[] (ss_dash01..12, 20 entries) and
 *    D_0CA6B380[] (ss_dash01, 13..22, 18 entries) for the trail texlists,
 *    and the overlay BSS matrix 0x0CA6C2D0 for the scratch matrix
 *    B_8C7A1E48. The models are still the 1ST_READ Sonic_Jumpball_Effect
 *    and Sonic_Effect_Trail.
 */

#include "common.h"
#include "ninja.h"
#include "task.h"
#include "sa1/sa1.h"

/* B_CHAOS7 data (extern only): one-entry texlists, slices of the
 * SUPERSONIC NJS_TEXNAME array 0x0C901B1C (73 names). */
extern NJS_TEXLIST D_0CA6B2D0;          /* ss_ball_a (texname 48) */
extern NJS_TEXLIST D_0CA6B2D8;          /* ss_ball_b (texname 49) */
extern NJS_TEXLIST D_0CA6B2E0[];        /* trail texlists, indexed by wtimer - 1 (20) */
extern NJS_TEXLIST D_0CA6B380[];        /* the same while twp->flag & 8 (18) */

/*
 * B_CHAOS7 BSS scratch matrix at 0x0CA6C2D0 (replaces 1ST_READ B_8C7A1E48).
 * It lies past the end of the B_CHAOS7 image (0x0CA6B454), which the split
 * does not label, so the pools hold the plain number H'0CA6C2D0.
 * It must be an extern symbol, not the constant-address define of
 * B_0CADD4C0 in src/overlays/stg05/code_0C911DC0.c: SHC keeps an integer
 * constant (and a symbol + offset) in R12 across the five uses in
 * func_0C91E26A and spills `ap` to the stack (frame 44 instead of 40), while
 * a plain symbol is reloaded from the pool at each use, as in the original.
 * LANDING: add "B_0CA6C2D0 = 0x0CA6C2D0;" to config/symbols.b_chaos7.txt and
 * re-split. Then build/B_CHAOS7.symbols.txt resolves the name for shcdiff and
 * splice, the chunk's pool word becomes an import of _B_0CA6C2D0, and
 * tools/gensym.py defines it from its B_XXXXXXXX name. Without that line
 * func_0C91E26A reports 5 differing pool words (value "sym:_B_0CA6C2D0").
 * On PC pclower maps the B_ name to its DC address like B_0CADD4C0.
 */
extern NJS_MATRIX B_0CA6C2D0;

/* 1ST_READ data */
extern NJS_MATRIX D_8C67C640;           /* unit matrix */
extern NJS_MODEL Sonic_Jumpball_Effect; /* 8C25654C */
extern NJS_OBJECT Sonic_Effect_Trail;   /* 8C255B10 (figure/sonic/models/sonic_dash.nja) */

/* 1ST_READ functions not declared in the sa1 headers */
extern void func_8C056926(void);                /* save draw flags (code_8C056860.c) */
extern void func_8C056938(void);                /* restore draw flags */
extern void func_8C05694A(Uint32 a, Uint32 b);  /* set draw flags */

/* rotation skipped for a zero angle (as code_8C0B4040.c) */
#define ROTATEX(a) ((a) ? njRotateX(NULL, (a)) : (void)0)
#define ROTATEY(a) ((a) ? njRotateY(NULL, (a)) : (void)0)
#define ROTATEZ(a) ((a) ? njRotateZ(NULL, (a)) : (void)0)

void func_0C91DB60(taskwk *twp);
void func_0C91DCB2(taskwk *twp);
void func_0C91DDB6(task *tp);
void func_0C91DE82(task *tp);
void func_0C91DED2(task *tp);
void func_0C91DFE0(task *tp);
void func_0C91E016(NJS_VECTOR *v, Angle3 *ang);
Sint32 func_0C91E06A(taskwk *twp);
void func_0C91E1D2(task *tp);
void func_0C91E26A(task *tp);
void func_0C91E5E2(task *tp);

/* 8C0B41E0: 5 units along the player's up axis plus a random offset of
 * length 1..3; twp->scl = player position; texlist ss_ball_a or _b. */
void func_0C91DB60(taskwk *twp)
{
    NJS_VECTOR v;
    NJS_VECTOR vo;
    taskwk *ptwp;

    ptwp = playertwp[(Uint8)twp->counter.b[0]];
    if (ptwp) {
        v.x = v.z = 0.0f;
        v.y = 5.0f;
        njPushMatrix(D_8C67C640);
        ROTATEZ(ptwp->ang.z);
        ROTATEX(ptwp->ang.x);
        ROTATEY(-ptwp->ang.y);
        njCalcVector(NULL, &v, &twp->pos);
        v.x = v.z = 0.0f;
        v.y = SA1_FMAC((Float)rand(), 6.103515625e-05f, 1.0f);
        njPushMatrix(D_8C67C640);
        ROTATEZ(SA1_FTOI((Float)rand() * 2.0f));
        ROTATEX(SA1_FTOI((Float)rand() * 2.0f));
        ROTATEY(ptwp->ang.y);
        njCalcVector(NULL, &v, &vo);
        njPopMatrix(2);
        njAddVector(&twp->pos, &vo);
        twp->scl = ptwp->pos;
    }
    if ((Float)rand() * 3.0517578125e-05f > 0.5f) {
        twp->value.ptr = &D_0CA6B2D0;
    } else {
        twp->value.ptr = &D_0CA6B2D8;
    }
}

/* 8C0B4332: 5 units along the player's up axis; scl.x = 3. */
void func_0C91DCB2(taskwk *twp)
{
    NJS_VECTOR v;
    taskwk *ptwp;

    ptwp = playertwp[(Uint8)twp->counter.b[0]];
    if (ptwp) {
        v.x = v.z = 0.0f;
        v.y = 5.0f;
        njPushMatrix(D_8C67C640);
        ROTATEZ(ptwp->ang.z);
        ROTATEX(ptwp->ang.x);
        ROTATEY(-ptwp->ang.y);
        njCalcVector(NULL, &v, &twp->pos);
        njAddVector(&twp->pos, &ptwp->pos);
        njPopMatrix(1);
    }
    if ((Float)rand() * 3.0517578125e-05f > 0.5f) {
        twp->value.ptr = &D_0CA6B2D0;
    } else {
        twp->value.ptr = &D_0CA6B2D8;
    }
    twp->scl.x = 3.0f;
}

/* 8C0B4600: fading ball at pos + scl, rotated by ang (z, x, -y). */
void func_0C91DDB6(task *tp)
{
    taskwk *twp;
    NJS_ARGB argb;

    twp = tp->twp;
    func_8C056926();
    func_8C05694A(0, 0x2100000);
    argb.r = argb.g = argb.b = 0.0f;
    argb.a = -(twp->wtimer * 0.19999999f);
    njSetConstantMaterial(&argb);
    njSetTexture(twp->value.ptr);
    njPushMatrix(NULL);
    njTranslate(NULL, twp->pos.x + twp->scl.x, twp->pos.y + twp->scl.y, twp->pos.z + twp->scl.z);
    twp->ang.z ? (njRotateZ(NULL, twp->ang.z), 0) : 0;
    twp->ang.x ? (njRotateX(NULL, twp->ang.x), 0) : 0;
    -twp->ang.y ? (njRotateY(NULL, -twp->ang.y), 0) : 0;
    func_8C036F12(&Sonic_Jumpball_Effect);
    njPopMatrix(1);
    argb.a = 0.0f;
    njSetConstantMaterial(&argb);
    func_8C056938();
}

/* 8C0B46CC: draws for 5 frames, following the player (scl = player pos,
 * ang = player ang), then frees itself. Unlike the original it leaves
 * tp->disp alone (FreeTask is a tail call). */
void func_0C91DE82(task *tp)
{
    taskwk *twp;
    taskwk *ptwp;

    twp = tp->twp;
    ptwp = playertwp[(Uint8)twp->counter.b[0]];
    if (twp->wtimer++ >= 5) {
        FreeTask(tp);
        return;
    }
    func_0C91DDB6(tp);
    twp->scl = ptwp->pos;
    twp->ang = ptwp->ang;
}

/* 8C0B4724: fading ball at pos, uniform scale scl.x. */
void func_0C91DED2(task *tp)
{
    taskwk *twp;
    Float s;
    NJS_ARGB argb;

    twp = tp->twp;
    func_8C056926();
    func_8C05694A(0, 0x2100000);
    argb.r = argb.g = argb.b = 0.0f;
    argb.a = -(twp->wtimer * 0.099999996f);
    njSetConstantMaterial(&argb);
    njSetTexture(twp->value.ptr);
    njPushMatrix(NULL);
    njTranslate(NULL, twp->pos.x, twp->pos.y, twp->pos.z);
    s = twp->scl.x;
    njScale(NULL, s, s, s);
    func_8C036F12(&Sonic_Jumpball_Effect);
    njPopMatrix(1);
    argb.a = 0.0f;
    njSetConstantMaterial(&argb);
    func_8C056938();
}

/* 8C0B4820: draws for 10 frames, shrinking scl.x by 0.4 per frame, then
 * frees itself (tp->disp is left alone, unlike the original). */
void func_0C91DFE0(task *tp)
{
    taskwk *twp;

    twp = tp->twp;
    if (twp->wtimer++ >= 10) {
        FreeTask(tp);
    } else {
        func_0C91DED2(tp);
        twp->scl.x -= 0.39999998f;
    }
}

/* 8C0B4860: direction of v as angles (x = 0, y = atan2(-z, x),
 * z = asin(y / |v|)). */
void func_0C91E016(NJS_VECTOR *v, Angle3 *ang)
{
    Float r;

    r = njScalor(v);
    if (r != 0.0f) {
        ang->y = SA1_FTOI(atan2f(-v->z, v->x) * 10430.381f);
        ang->z = SA1_FTOI(func_8C10D058(v->y / r) * 10430.381f);
        ang->x = 0;
    }
}

#ifdef NON_MATCHING
/*
 * 8C0B48E0: copy the object, model and points of Sonic_Effect_Trail
 * (MAlloc); start at the centre of the player's first collision shape.
 * Returns 1, or 0 when an allocation fails.
 *
 * Complete and functionally equivalent; the C of 8C0B48E0
 * (src/1st_read/code_8C0B48E0.c). In this translation unit the literal pool
 * lands after the "return 0" branch, as in the clone, so the instruction
 * count and branches match (142). The only difference left is the one the
 * original already has: register allocation of the inline NJS_MODEL copy
 * (0C91E0D0-0C91E0FC, 22 instructions) has the source (org->model) in R3
 * and the destination (obj->model) in R2; this compile swaps them. All 22
 * differing instructions are exactly that R2 <-> R3 rename, and both
 * registers are dead after the block (L0C91E106 writes R3 before any read,
 * then calls MAlloc). Tried here without effect on the tie: early-return
 * forms of the second and third allocation tests (different layout), "!"
 * and "== NULL" tests with the copy in the else branch (layout), a block
 * local for org->model (35 differing). See code_8C0B48E0.c for the forms
 * tried on the original.
 */
Sint32 func_0C91E06A(taskwk *twp)
{
    NJS_OBJECT *obj;
    NJS_POINT3 *dst;
    NJS_POINT3 *src;
    Uint32 i;
    taskwk *ptwp;
    CCL_INFO *info;
    Uint32 n;
    NJS_OBJECT *org;

    org = &Sonic_Effect_Trail;
    n = org->model->nbPoint;
    obj = (NJS_OBJECT *)MAlloc(sizeof(NJS_OBJECT));
    if (obj == NULL) {
        return 0;
    }
    *obj = *org;
    twp->value.ptr = obj;
    if ((obj->model = (NJS_MODEL *)MAlloc(sizeof(NJS_MODEL))) != NULL) {
        *obj->model = *org->model;
    } else {
        MFree(obj);
        return 0;
    }
    if ((obj->model->points = (NJS_POINT3 *)MAlloc(n * sizeof(NJS_POINT3))) != NULL) {
        dst = obj->model->points;
        src = org->model->points;
        for (i = 0; i < n; i++) {
            dst->x = src->x;
            dst->y = src->y;
            dst->z = src->z;
            dst++;
            src++;
        }
    } else {
        MFree(obj->model);
        MFree(obj);
        return 0;
    }
    ptwp = playertwp[(Uint8)twp->counter.b[0]];
    if (ptwp) {
        info = CWP(ptwp)->info;
        twp->pos = info->center;
    }
    return 1;
}
#else
ASM_STUB(Sint32 func_0C91E06A(taskwk *twp))
#endif

/* 8C0B4A48: texlist D_0CA6B2E0[wtimer - 1] (D_0CA6B380[] when flag & 8),
 * translate pos, rotate ang.y / ang.z, scale scl, draw the copy (the
 * original object on frame 1). func_8C036F12 draws an NJS_OBJECT;
 * sa1/engine.h declares it with an NJS_MODEL * parameter, hence the casts. */
void func_0C91E1D2(task *tp)
{
    taskwk *twp;
    NJS_OBJECT *obj;

    twp = tp->twp;
    obj = (NJS_OBJECT *)twp->value.ptr;
    if (twp->flag & 8) {
        njSetTexture(&D_0CA6B380[twp->wtimer - 1]);
    } else {
        njSetTexture(&D_0CA6B2E0[twp->wtimer - 1]);
    }
    njPushMatrix(NULL);
    njTranslateV(NULL, &twp->pos);
    twp->ang.y ? njRotateY(NULL, twp->ang.y) : (void)0;
    twp->ang.z ? njRotateZ(NULL, twp->ang.z) : (void)0;
    njScaleV(NULL, &twp->scl);
    if (twp->wtimer == 1) {
        func_8C036F12((NJS_MODEL *)&Sonic_Effect_Trail);
    } else {
        func_8C036F12((NJS_MODEL *)obj);
    }
    njPopMatrix(1);
}

/*
 * 8C0B4B20: frees the task after 20 frames (at wtimer 19 while flag & 8),
 * else draws via 0C91E1D2. Frame 1: pos = player centre (pos + rotated
 * (0, center_height, 0)), ang / scl.x point the trail from the start
 * position to it; flag |= 8 when free.b[0] has 0x04 set and 0x02 clear.
 * Frame 2: the odd points of the copy move so that the trail reaches the
 * player's new centre. Differences from 8C0B4B20: no tp->disp = NULL, no
 * scl.x >= 5 test, and the flag-8 condition (see the file header).
 */
void func_0C91E26A(task *tp)
{
    taskwk *twp;
    NJS_OBJECT *obj;
    taskwk *ptwp;
    Uint32 n;
    Uint32 i;
    NJS_POINT3 *pts;
    playerwk *pwp;
    NJS_VECTOR v1;
    NJS_VECTOR v2;
    Angle3 ang;
    Angle3 *ap;
    NJS_OBJECT *org;

    twp = tp->twp;
    obj = (NJS_OBJECT *)twp->value.ptr;
    if (twp->wtimer++ >= 20) {
        FreeTask(tp);
        return;
    } else if ((twp->flag & 8) && twp->wtimer >= 19) {
        FreeTask(tp);
        return;
    }
    if (twp->wtimer == 1) {
        ptwp = playertwp[(Uint8)twp->counter.b[0]];
        if (ptwp) {
            v1.x = v1.z = 0.0f;
            v1.y = GetPlayerWorkPtr(twp->counter.b[0])->p.center_height;
            njPushMatrix((NJS_MATRIX_PTR)&D_8C67C640);
            ptwp->ang.z ? njRotateZ(NULL, ptwp->ang.z) : (void)0;
            ptwp->ang.x ? njRotateX(NULL, ptwp->ang.x) : (void)0;
            -ptwp->ang.y ? njRotateY(NULL, -ptwp->ang.y) : (void)0;
            njCalcVector(NULL, &v1, &v2);
            njPopMatrix(1);
            njAddVector(&v2, &ptwp->pos);
            v1 = twp->pos;
            twp->pos = v2;
            njSubVector(&v1, &v2);
            func_0C91E016(&v1, &twp->ang);
            twp->scl.x = njScalor(&v1) * 0.125f;
            twp->scl.y = 1.0f;
            twp->scl.z = 1.0f;
            pwp = PWP(playermwp[(Uint8)twp->counter.b[0]]);
            if ((pwp->free.b[0] & 4) != 0 && (pwp->free.b[0] & 2) == 0) {
                twp->flag |= 8;
            }
        }
    } else if (twp->wtimer == 2) {
        ptwp = playertwp[(Uint8)twp->counter.b[0]];
        if (ptwp) {
            org = &Sonic_Effect_Trail;
            pts = obj->model->points;
            n = org->model->nbPoint;
            v1.x = v1.z = 0.0f;
            v1.y = GetPlayerWorkPtr(twp->counter.b[0])->p.center_height;
            njPushMatrix((NJS_MATRIX_PTR)&D_8C67C640);
            ptwp->ang.z ? njRotateZ(NULL, ptwp->ang.z) : (void)0;
            ptwp->ang.x ? njRotateX(NULL, ptwp->ang.x) : (void)0;
            -ptwp->ang.y ? njRotateY(NULL, -ptwp->ang.y) : (void)0;
            njCalcVector(NULL, &v1, &v2);
            njPopMatrix(1);
            njAddVector(&v2, &ptwp->pos);
            v1 = twp->pos;
            njSubVector(&v1, &v2);
            if (njScalor(&v1) != 0.0f) {
                ap = &ang;
                func_0C91E016(&v1, ap);
                njUnitMatrix((NJS_MATRIX_PTR)&B_0CA6C2D0);
                ap->y ? njRotateY((NJS_MATRIX_PTR)&B_0CA6C2D0, ap->y) : (void)0;
                ap->z ? njRotateZ((NJS_MATRIX_PTR)&B_0CA6C2D0, ap->z) : (void)0;
                njScaleV((NJS_MATRIX_PTR)&B_0CA6C2D0, &twp->scl);
                njPushMatrix((NJS_MATRIX_PTR)&D_8C67C640);
                twp->ang.y ? njRotateY(NULL, twp->ang.y) : (void)0;
                twp->ang.z ? njRotateZ(NULL, twp->ang.z) : (void)0;
                njScaleV(NULL, &twp->scl);
                njInvertMatrix(NULL);
                njMultiMatrix(NULL, (NJS_MATRIX_PTR)&B_0CA6C2D0);
                pts++;
                for (i = 0; i < n; i += 2) {
                    v1 = *pts;
                    njCalcPoint(NULL, &v1, pts);
                    pts += 2;
                }
                njPopMatrix(1);
            }
        }
    }
    func_0C91E1D2(tp);
}

/* 8C0B4E62: frees the copies. */
void func_0C91E5E2(task *tp)
{
    taskwk *twp;
    NJS_OBJECT *obj;

    twp = tp->twp;
    obj = (NJS_OBJECT *)twp->value.ptr;
    if (obj->model->points) {
        MFree(obj->model->points);
    }
    if (obj->model) {
        MFree(obj->model);
    }
    if (obj) {
        MFree(obj);
    }
}
