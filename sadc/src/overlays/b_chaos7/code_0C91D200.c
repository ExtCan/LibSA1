/*
 * B_CHAOS7 0C91D200-0C91DB60 (chunk asm/overlays/b_chaos7/text/0C91D200.src,
 * functions 1-3 of 3): the Super Sonic aura (super_sonic.md 1.9).
 *
 *  func_0C91D200  water splash: copies pos, spd and the scale into the
 *                 particle parameter block D_0CA6B418 and tail-calls the
 *                 1ST_READ spawner func_8C046A9C with it.
 *  func_0C91D222  [SADX Sonic_SuperAura_Display] disp: nothing on sub
 *                 step 1 and later (B_8C754EC8 != 0). Texlist
 *                 D_0CA6B210[btimer & 7], matrix at the centre of the
 *                 player's first collision shape, rotations Z, X, -Y and a
 *                 mirroring njScale(-1, 1, 1); the model by speed (spd.x >
 *                 13: Aura_Dash_2 with D_0CA6B250[(btimer >> 1) & 7]; > 7:
 *                 Aura_Dash_1 with D_0CA6B210[(btimer >> 1) & 7]; otherwise
 *                 Aura). On the ground on B_CHAOS7's water plane (the land
 *                 object's model is D_0C966D08) it also draws the two water
 *                 splash models at the player's position with texlist
 *                 D_0CA6B290[(btimer >> 1) & 7]: scaled by scl.x (-s, s, s)
 *                 and by scl.y (-0.8 s, 0.8 s, 1.3 s), each when non-zero.
 *  func_0C91D486  [SADX Sonic_SuperAura_Main] exec: frees itself without
 *                 the player work or without equipment bit 0x8000;
 *                 btimer++; scl.x = sqrt(|spd.x|) * 0.4. On the water
 *                 plane: spd.x > 2 spawns two splashes every step (scale
 *                 0.28), spd.x > 6 with probability 0.06 two more (0.4),
 *                 spd.x > 9 with probability 0.2 two more (0.6); scl.y
 *                 (spd.x > 6) and scl.z (spd.x > 9) fade in and out by
 *                 0.05 per step; off the water scl is cleared. Then the
 *                 whoosh state machine B_0CA6C310 (tones 1035 / 1036) and
 *                 the display.
 *
 * All three functions match (shcdiff --image B_CHAOS7). Code generation
 * notes:
 *  - both tasks read the player number once into a Uint8 local (as in
 *    code_8C0B2EA0.c); indexing playertwp[] with the byte expression twice
 *    loads counter.b[0] twice.
 *  - disp: the 16-byte frame holds the ?: rotation temporary at @R15 and an
 *    unused 12-byte local (NJS_VECTOR); without it the frame is 4 bytes.
 *    The splash scales are written as plain expressions (scl.x * 1.0f,
 *    scl.y * 0.8f twice, scl.y * 1.3f): SHC CSEs them into FR14/FR15 and
 *    keeps the constant 1.0f of njScale(-1, 1, 1) in FR15. Float locals for
 *    them move the scale into FR15 and also change the integer allocation
 *    (the mask 7 then loses R8 to njSetTexture's address).
 *  - exec: the dead copy "p = ptwp->pos" before the second spd > 2 splash
 *    is in the original (MOV.L copies at 0C91D61C-0C91D628). The spd > 6
 *    and spd > 9 splashes compute p from v before PConvertVector_P2G
 *    rotates it (player-space v), the spd > 2 ones after: original order.
 *    SA1_FMAC: 21 FMACs in the asm, 21 SA1_FMAC.
 *
 * B_0CA6C310 lies past the end of the B_CHAOS7 image (0x0CA6B454), in the
 * overlay's BSS, which the split does not label: the pool holds the plain
 * number H'0CA6C310. As in code_0C911DC0.c (STG05), under SHC it is spelled
 * as that address so that the pool entry compares equal; on PC it is an
 * ordinary variable (same spelling as the wrappers file code_0C900480.c,
 * whose func_0C900480 resets it to 0).
 */
#include "common.h"
#include "sa1/sa1.h"

/* Particle parameter block of func_8C046A9C (same layout as
 * SA1_EFFECT_PARAM in src/1st_read/code_8C046A08.c, 15 floats; the spawner
 * reads 0x00, 0x14-0x28 and the argb at 0x2C-0x38). */
typedef struct {
    Float scl;                  /* 0x00 */
    Float dscl;                 /* 0x04 scale step per frame */
    Float dframe;               /* 0x08 animation step per frame */
    Float drag;                 /* 0x0C velocity damping */
    Float rise;                 /* 0x10 added to spd.y per frame */
    NJS_POINT3 pos;             /* 0x14 */
    NJS_POINT3 spd;             /* 0x20 */
    NJS_ARGB argb;              /* 0x2C */
} SA1_EFFECT_PARAM;             /* 0x3C */

#if SA1_PTR32
SA1_ASSERT_SIZE(SA1_EFFECT_PARAM, 0x3C);
SA1_ASSERT_OFFSET(SA1_EFFECT_PARAM, pos, 0x14);
SA1_ASSERT_OFFSET(SA1_EFFECT_PARAM, spd, 0x20);
#endif

/* B_CHAOS7 BSS: whoosh sound state of the aura (see above) */
#if defined(TARGET_PC)
extern Sint8 B_0CA6C310;
#else
#define B_0CA6C310 (*(Sint8 *)0x0CA6C310)
#endif

/* B_CHAOS7 data (extern only). The Super_Sonic_Aura* and Water_Effect_*
 * labels are NJS_OBJECTs (evalflags 0x17 / 0x14, model at +4), not basic
 * models as config/symbols.b_chaos7.txt says. */
extern NJS_OBJECT B_CHAOS7_Super_Sonic_Aura;                /* 0C90C620 */
extern NJS_OBJECT B_CHAOS7_Super_Sonic_Aura_Dash_1;         /* 0C90CFA4 */
extern NJS_OBJECT B_CHAOS7_Super_Sonic_Aura_Dash_2;         /* 0C90D928 */
extern NJS_OBJECT B_CHAOS7_Super_Sonic_Water_Effect_Left;   /* 0C90DC14 */
extern NJS_OBJECT B_CHAOS7_Super_Sonic_Water_Effect_Right;  /* 0C90DF00 */
extern NJS_MODEL D_0C966D08;            /* model of the water plane B_CHAOS7_Collision_Plane (80 x 80 quad) */
extern NJS_TEXLIST D_0CA6B210[];        /* 8 one-texture slices of the SUPERSONIC names */
extern NJS_TEXLIST D_0CA6B250[];        /* 8 one-texture slices */
extern NJS_TEXLIST D_0CA6B290[];        /* 8 two-texture slices (ss_waterl*) */
extern SA1_EFFECT_PARAM D_0CA6B418;     /* splash parameters (written here) */

/* 1ST_READ functions not declared in the sa1 headers */
void func_8C046A9C(SA1_EFFECT_PARAM *param);    /* spawn one particle from a parameter block (no C yet) */

#if defined(SA1_GA)
/*
 * SA1_GA (libsa1's lowered game code only; not the SHC build, the harness's SHC / TARGET_PC builds or their traces): a
 * PC-only host option (docs/research/super_general_design.md 4.8 D8, the owner's 0.9.3 note "Super Sonic's aura's
 * textures switch much faster than they should"; libsa1 sa1_character_set_game_option SA1_GOPT_SUPER_AURA_ANIM). The
 * display below runs on sub step 0 only, after the exec's btimer++ of each 60 Hz step (btimer advances 2 per pair): the
 * standing aura reads btimer & 7 (4 of its 8 textures, a new one every pair, the even or the odd ones), the dash auras
 * (btimer >> 1) & 7 (all 8 in order). `anim` 1 (calm): the standing aura reads (btimer >> 1) & 7 as the dash auras do;
 * 2 (slow): all three read (btimer >> 2) & 7 (each texture for two pairs). The water splash models keep the game's
 * index. A pcdata block (one per world) that libsa1 finds by its magic words (LIB src/world/sa1_world_charopt.c). While
 * `anim` is 0, the initial value, the function is the original, so worlds that never set it (the reference / exactness
 * worlds) run the game's code.
 */
#define SA1_AURAANIM_MAGIC0 0x41525541  /* "AURA" (sa1_world_charopt.c CO_AURAANIM_*) */
#define SA1_AURAANIM_MAGIC1 0x32323244  /* "D222" */
typedef struct SA1_AURAANIM {
    Uint32 magic[2];            /* 0x00 SA1_AURAANIM_MAGIC0, SA1_AURAANIM_MAGIC1 */
    Sint32 anim;                /* 0x08 0: the game's (Dreamcast); 1: calm; 2: slow */
} SA1_AURAANIM;
SA1_ASSERT_OFFSET(SA1_AURAANIM, anim, 0x08);
static SA1_AURAANIM sa1_auraanim = { { SA1_AURAANIM_MAGIC0, SA1_AURAANIM_MAGIC1 }, 0 };
/* the texture index of the standing aura (stand) and of the dash auras (!stand) */
#define SA1_AURA_INDEX(bt, stand)                                                                                   \
    (sa1_auraanim.anim == 2 ? (((bt) >> 2) & 7)                                                                  \
                            : (stand) && sa1_auraanim.anim != 1 ? ((bt) & 7) : (((bt) >> 1) & 7))
#endif

/* rotation skipped for a zero angle (as in code_8C0B4040.c) */
#define ROTATEX(a) ((a) ? njRotateX(NULL, (a)) : (void)0)
#define ROTATEY(a) ((a) ? njRotateY(NULL, (a)) : (void)0)
#define ROTATEZ(a) ((a) ? njRotateZ(NULL, (a)) : (void)0)

void func_0C91D200(NJS_POINT3 *pos, NJS_POINT3 *spd, Float scl)
{
    D_0CA6B418.pos = *pos;
    D_0CA6B418.spd = *spd;
    D_0CA6B418.scl = scl;
    func_8C046A9C(&D_0CA6B418);
}

/* [SADX Sonic_SuperAura_Display] */
void func_0C91D222(task *tp)
{
    taskwk *twp;
    taskwk *ptwp;
    playerwk *pwp;
    NJS_VECTOR unused;          /* never used; sizes the original 16-byte frame */
    Uint8 pno;

    twp = tp->twp;
    pno = twp->counter.b[0];
    ptwp = playertwp[pno];
    pwp = GetPlayerWorkPtr(pno);
    if (B_8C754EC8 != 0) {
        return;
    }
#if defined(SA1_GA)
    njSetTexture(&D_0CA6B210[SA1_AURA_INDEX(twp->btimer, 1)]);
#else
    njSetTexture(&D_0CA6B210[twp->btimer & 7]);
#endif
    njPushMatrix(NULL);
    njTranslateV(NULL, &CWP(ptwp)->info->center);
    ROTATEZ(ptwp->ang.z);
    ROTATEX(ptwp->ang.x);
    ROTATEY(-ptwp->ang.y);
    njScale(NULL, -1.0f, 1.0f, 1.0f);
    if (pwp->spd.x > 13.0f) {
#if defined(SA1_GA)
        njSetTexture(&D_0CA6B250[SA1_AURA_INDEX(twp->btimer, 0)]);
#else
        njSetTexture(&D_0CA6B250[(twp->btimer >> 1) & 7]);
#endif
        func_8C036F12((NJS_MODEL *)&B_CHAOS7_Super_Sonic_Aura_Dash_2);
    } else if (pwp->spd.x > 7.0f) {
#if defined(SA1_GA)
        njSetTexture(&D_0CA6B210[SA1_AURA_INDEX(twp->btimer, 0)]);
#else
        njSetTexture(&D_0CA6B210[(twp->btimer >> 1) & 7]);
#endif
        func_8C036F12((NJS_MODEL *)&B_CHAOS7_Super_Sonic_Aura_Dash_1);
    } else {
        func_8C036F12((NJS_MODEL *)&B_CHAOS7_Super_Sonic_Aura);
    }
    njPopMatrix(1);
    if ((ptwp->flag & 1) && pwp->lclop != NULL && pwp->lclop->model == &D_0C966D08) {
        njSetTexture(&D_0CA6B290[(twp->btimer >> 1) & 7]);
        njPushMatrix(NULL);
        njTranslateV(NULL, &ptwp->pos);
        ROTATEZ(ptwp->ang.z);
        ROTATEX(ptwp->ang.x);
        ROTATEY(-ptwp->ang.y);
        if (twp->scl.x != 0.0f) {
            njPushMatrix(NULL);
            njScale(NULL, -(twp->scl.x * 1.0f), twp->scl.x * 1.0f, twp->scl.x * 1.0f);
            func_8C036F12((NJS_MODEL *)&B_CHAOS7_Super_Sonic_Water_Effect_Left);
            func_8C036F12((NJS_MODEL *)&B_CHAOS7_Super_Sonic_Water_Effect_Right);
            njPopMatrix(1);
        }
        if (twp->scl.y != 0.0f) {
            njPushMatrix(NULL);
            njScale(NULL, -(twp->scl.y * 0.799999967f), twp->scl.y * 0.799999967f, twp->scl.y * 1.29999998f);
            func_8C036F12((NJS_MODEL *)&B_CHAOS7_Super_Sonic_Water_Effect_Left);
            func_8C036F12((NJS_MODEL *)&B_CHAOS7_Super_Sonic_Water_Effect_Right);
            njPopMatrix(1);
        }
        njPopMatrix(1);
    }
}

/* [SADX Sonic_SuperAura_Main] */
void func_0C91D486(task *tp)
{
    NJS_POINT3 v;
    NJS_POINT3 p;
    taskwk *twp;
    taskwk *ptwp;
    motionwk *pmwp;
    playerwk *pwp;
    Uint8 pno;

    twp = tp->twp;
    pno = twp->counter.b[0];
    ptwp = playertwp[pno];
    pmwp = playermwp[pno];
    pwp = GetPlayerWorkPtr(pno);
    if (pwp == NULL || !(pwp->equipment & 0x8000)) {
        FreeTask(tp);
        return;
    }
    twp->btimer++;
    twp->scl.x = njSqrt(_builtin_fabsf(pwp->spd.x)) * 0.399999984f;
    if (pwp->spd.x > 2.0f && (ptwp->flag & 1) && pwp->lclop != NULL
        && pwp->lclop->model == &D_0C966D08) {
        v.x = pwp->spd.x * 0.599999979f;
        v.y = 0.7f;
        v.z = -((Float)rand() * 9.15527312e-06f) + -0.75f;
        PConvertVector_P2G(ptwp, &v);
        p.x = SA1_FMAC(2.0f, v.x, ptwp->pos.x) - pmwp->spd.x * 3.0f;
        p.y = SA1_FMAC(2.0f, v.y, ptwp->pos.y) - pmwp->spd.y * 3.0f;
        p.z = SA1_FMAC(2.0f, v.z, ptwp->pos.z) - pmwp->spd.z * 3.0f;
        func_0C91D200(&p, &v, 0.279999979f);
        p = ptwp->pos;          /* dead store, in the original */
        v.x = pwp->spd.x * 0.599999979f;
        v.y = 0.7f;
        v.z = SA1_FMAC((Float)rand(), 9.15527312e-06f, 0.75f);
        PConvertVector_P2G(ptwp, &v);
        p.x = SA1_FMAC(2.0f, v.x, ptwp->pos.x) - pmwp->spd.x * 3.0f;
        p.y = SA1_FMAC(2.0f, v.y, ptwp->pos.y) - pmwp->spd.y * 3.0f;
        p.z = SA1_FMAC(2.0f, v.z, ptwp->pos.z) - pmwp->spd.z * 3.0f;
        func_0C91D200(&p, &v, 0.279999979f);
    }
    if (pwp->spd.x > 6.0f) {
        if (twp->scl.y < 1.0f) {
            twp->scl.y += 0.049999998f;
        }
        if ((ptwp->flag & 1) && pwp->lclop != NULL && pwp->lclop->model == &D_0C966D08) {
            if ((Float)rand() * 3.0517578125e-05f > 0.940000013f) {
                v.x = pwp->spd.x * 0.399999984f;
                v.y = 0.29999999f;
                v.z = -((Float)rand() * 9.15527312e-06f) + -2.0f;
                p.x = SA1_FMAC(2.0f, v.x, ptwp->pos.x) - pmwp->spd.x * 3.0f;
                p.y = SA1_FMAC(2.0f, v.y, ptwp->pos.y) - pmwp->spd.y * 3.0f;
                p.z = SA1_FMAC(2.0f, v.z, ptwp->pos.z) - pmwp->spd.z * 3.0f;
                PConvertVector_P2G(ptwp, &v);
                func_0C91D200(&p, &v, 0.399999984f);
                v.x = pwp->spd.x * 0.399999984f;
                v.y = 0.29999999f;
                v.z = SA1_FMAC((Float)rand(), 9.15527312e-06f, 2.0f);
                p.x = SA1_FMAC(2.0f, v.x, ptwp->pos.x) - pmwp->spd.x * 3.0f;
                p.y = SA1_FMAC(2.0f, v.y, ptwp->pos.y) - pmwp->spd.y * 3.0f;
                p.z = SA1_FMAC(2.0f, v.z, ptwp->pos.z) - pmwp->spd.z * 3.0f;
                PConvertVector_P2G(ptwp, &v);
                func_0C91D200(&p, &v, 0.399999984f);
            }
        }
    } else if (twp->scl.y > 0.0f) {
        twp->scl.y -= 0.049999998f;
    } else {
        twp->scl.y = 0.0f;
    }
    if (pwp->spd.x > 9.0f) {
        if (twp->scl.z < 1.0f) {
            twp->scl.z += 0.049999998f;
        }
        if ((ptwp->flag & 1) && pwp->lclop != NULL && pwp->lclop->model == &D_0C966D08) {
            if ((Float)rand() * 3.0517578125e-05f < 0.199999992f) {
                v.x = pwp->spd.x * 0.199999992f;
                v.y = 0.599999979f;
                v.z = -((Float)rand() * 1.52587890625e-05f) + -1.25f;
                p.x = SA1_FMAC(2.0f, v.x, ptwp->pos.x) - pmwp->spd.x * 3.0f;
                p.y = SA1_FMAC(2.0f, v.y, ptwp->pos.y) - pmwp->spd.y * 3.0f;
                p.z = SA1_FMAC(2.0f, v.z, ptwp->pos.z) - pmwp->spd.z * 3.0f;
                PConvertVector_P2G(ptwp, &v);
                func_0C91D200(&p, &v, 0.599999979f);
                v.x = pwp->spd.x * 0.199999992f;
                v.y = 0.599999979f;
                v.z = SA1_FMAC((Float)rand(), 1.52587890625e-05f, 1.25f);
                p.x = SA1_FMAC(2.0f, v.x, ptwp->pos.x) - pmwp->spd.x * 3.0f;
                p.y = SA1_FMAC(2.0f, v.y, ptwp->pos.y) - pmwp->spd.y * 3.0f;
                p.z = SA1_FMAC(2.0f, v.z, ptwp->pos.z) - pmwp->spd.z * 3.0f;
                PConvertVector_P2G(ptwp, &v);
                func_0C91D200(&p, &v, 0.599999979f);
            }
        }
    } else if (twp->scl.z > 0.0f) {
        twp->scl.z -= 0.049999998f;
    } else {
        twp->scl.z = 0.0f;
    }
    if (pwp->lclop == NULL || pwp->lclop->model != &D_0C966D08) {
        twp->scl.x = 0.0f;
        twp->scl.y = 0.0f;
        twp->scl.z = 0.0f;
    }
    switch (B_0CA6C310) {
    case 0:
        if (pwp->spd.x > 7.0f) {
            func_8C0940DA(0x40B, 0, 0, 127);
            B_0CA6C310 = 1;
        }
        break;
    case 1:
        if (pwp->spd.x > 13.0f) {
            func_8C0945A0(0x40B);
            func_8C0940DA(0x40C, 0, 0, 127);
            B_0CA6C310 = 2;
        } else if (pwp->spd.x < 6.0f) {
            func_8C0945A0(0x40B);
            B_0CA6C310 = 0;
        }
        break;
    case 2:
        if (pwp->spd.x < 6.0f) {
            func_8C0945A0(0x40C);
            B_0CA6C310 = 0;
        }
        break;
    }
    func_0C91D222(tp);
}
