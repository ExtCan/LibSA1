#include "common.h"
#include "ninja.h"
#include "sa1/sa1.h"

/*
 * 1ST_READ 8C0BA820-8C0BAFC0 (chunk 8C0BA820, function 1 of 1):
 * Tails' display routine (tp->disp, set by TailsMain 8C0B6148). Same frame
 * as Sonic's func_8C0AE17A (code_8C0AE0E0.c), plus:
 *  - the body is translated to the point p.height / 2 above twp->pos along
 *    the player's up axis (also written to collision shape 0's centre),
 *  - the spin ball (action 14 on the ground) is drawn twice more as fading
 *    after-images 2 and 4 frames behind (njSetConstantMaterial alpha -1/3,
 *    -2/3, constant-attribute bit 0x100000),
 *  - the two tails (NJS_OBJECTs D_8C258974 / D_8C2582E8; flight tails
 *    D_8C2606A8 / D_8C260028 for actions 37-41) are spun around ang[0] and
 *    drawn twice more as after-images behind the current angle,
 *  - the tails' spin speed and the tail model morph (pwp->free.f[6], 0..1,
 *    raised by actions 11-13) are advanced while the game is not paused.
 * twp->timer.b[3] holds Tails display flags (4: flying tails always on,
 * 0x10: tails stopped); twp->btimer is the spin-ball blink counter.
 */

extern NJS_TEXLIST texlist_MILES;     /* 8C257C54 */
/* objects declared as arrays (like code_8C0BDC80.c): the original reaches
 * their fields as @(disp,Rn) from the object address, not as sym+off */
extern NJS_OBJECT D_8C258974[];       /* tail A (spun around ang[0]) */
extern NJS_OBJECT D_8C2582E8[];       /* tail B */
extern NJS_OBJECT D_8C2606A8[];       /* flight tail A (actions 37-41) */
extern NJS_OBJECT D_8C260028[];       /* flight tail B */
extern NJS_OBJECT D_8C25824C[];       /* tail A morph node (model switched) */
extern NJS_OBJECT D_8C2588D8[];       /* tail B morph node */
extern NJS_MODEL D_8C25FF64[];        /* tail A model at f[6] = 1 */
extern NJS_MODEL D_8C273E3C[];        /* tail A model at f[6] = 0 (bent by func_8C0BDC80) */
extern NJS_MODEL D_8C275060[];        /* tail A morph buffer */
extern NJS_MODEL D_8C2605E4[];        /* tail B model at f[6] = 1 */
extern NJS_MODEL D_8C274448[];        /* tail B model at f[6] = 0 */
extern NJS_MODEL D_8C27566C[];        /* tail B morph buffer */

void func_8C0BDB72(taskwk *twp, playerwk *pwp);  /* flight tails spin (code_8C0BD8B2.c) */
void func_8C0BDC80(void);                         /* bend the tail models (code_8C0BDC80.c) */

#if defined(SA1_GA)
/*
 * SA1_GA (libsa1's lowered game code only; not the SHC build, the harness's SHC / TARGET_PC builds or their traces): a
 * PC-only host option (docs/research/gsa1_rounds_plan.md 14.1h, the owner's answer to docs/research/gsa1_070_tails.md
 * T4: "Add a switch, default like the game"; libsa1 sa1_character_set_game_option SA1_GOPT_TAILS_SNAP_TAILS). The tail
 * morph below blends the bent tails into the propeller blades over 100 pairs (free.f[6] += / -= 0.01) with a vertex
 * blend (func_8C055ECE) that passes through flat ribbons. With snap set the display uses the morph weight snapped to 0 /
 * 1 (1 from 0.5): the morph nodes draw the blend made at weight 0 (the bent tails) or the blades, and the tails' spin,
 * the angle between the two tails and the tail after-images' step take the snapped weight; free.f[6] itself rises and
 * falls as in the game. A pcdata block (one per world) that libsa1 finds by its magic words (LIB
 * src/world/sa1_world_charopt.c) and sets. While snap is 0, the initial value, the function is the original, so worlds
 * that never set it (the reference / exactness worlds) run the game's code.
 */
#define SA1_TAILSNAP_MAGIC0 0x4C494154  /* "TAIL" (sa1_world_charopt.c CO_TAILSNAP_*) */
#define SA1_TAILSNAP_MAGIC1 0x30324142  /* "BA20" */
typedef struct SA1_TAILSNAP {
    Uint32 magic[2];            /* 0x00 SA1_TAILSNAP_MAGIC0, SA1_TAILSNAP_MAGIC1 */
    Sint32 snap;                /* 0x08 0: the game's morph; 1: the morph weight snapped to 0 / 1 */
} SA1_TAILSNAP;
SA1_ASSERT_OFFSET(SA1_TAILSNAP, snap, 0x08);
static SA1_TAILSNAP sa1_tailsnap = { { SA1_TAILSNAP_MAGIC0, SA1_TAILSNAP_MAGIC1 }, 0 };
#endif

/*
 * NON_MATCHING (SHC 5.1r03): same instructions, frame (56 bytes), stack slots
 * and constant pools as the original except for register allocation choices:
 *  - the callee-saved registers R9/R11 are swapped: the original gives
 *    action / the spin-ball loop counter / obj1 R11 and the cached address
 *    of D_8C258974 R9 (here R9 and R11),
 *  - in the spin-ball after-image loop the original keeps 0.33333332f in
 *    FR12 and rebuilds 2.0f (FLDI1 + FADD) every pass; here 2.0f is hoisted
 *    into FR12 and 0.33333332f is reloaded,
 *  - three local scratch choices: R3 vs R2 for the D_8C260028 / 8C0BDB72
 *    pool loads, the j = 0 store scheduled before the argb.a store, and the
 *    tail "+= 0x333" pair (original: D_8C2582E8 in R0, the constant copied
 *    to R3 for the first add, one instruction more).
 * Tried without effect or worse: declaration order and block scopes (SHC
 * allocates per web), a local NJS_OBJECT * for D_8C258974 (fixes FR12 but
 * moves the R8-R11 order and the step/obj2 slots), action/i/j merged,
 * Uint16/Sint16 types, `register`, other forms of the 0x333 and argb/j
 * initialisations, do/for/while forms of the tails loop (the while form
 * gives the original stack slots; a for loop is turned into a count-down).
 * The C is complete and behaves like the original.
 */
#ifdef NON_MATCHING
void func_8C0BA820(task *tp)
{
    taskwk *twp;
    motionwk *mwp;
    playerwk *pwp;
    Uint32 action;
    Sint32 i;
    Float frame;
    Float nbf;
    NJS_OBJECT *obj1;
    NJS_OBJECT *obj2;
    Sint32 step;
    Sint32 saved1;
    Sint32 saved2;
    Sint32 ang1;
    Sint32 ang2;
    Sint32 j;

    twp = tp->twp;
    mwp = tp->mwp;
    pwp = mwp->work.pwp;
    if (!func_8C038D00(twp->pos.x, twp->pos.y, twp->pos.z, 10.0f)) {
        return;
    }
    func_8C038CEC(2);
    if (twp->timer.b[3] & 4) {
        pwp->free.f[7] = 0.0f;
        pwp->free.f[6] = 1.0f;
    }
    action = pwp->mj.reqaction;
    if (pwp->mj.mtnmode == 2) {
        action = pwp->mj.action;
    }
    if (!(twp->wtimer & 2)) {
        njSetTexture(&texlist_MILES);
        njPushMatrix(NULL);
        {
            NJS_VECTOR v;
            NJS_VECTOR w;
            CCL_INFO *info;

            v.x = v.z = 0.0f;
            v.y = pwp->p.height * 0.5f;
            njPushMatrix(D_8C67C640);
            twp->ang.z ? (njRotateZ(NULL, twp->ang.z), 0) : 0;
            twp->ang.x ? (njRotateX(NULL, twp->ang.x), 0) : 0;
            (-twp->ang.y) ? (njRotateY(NULL, -twp->ang.y), 0) : 0;
            njCalcVector(NULL, &v, &w);
            njPopMatrix(1);
            w.x += twp->pos.x;
            w.y += twp->pos.y;
            w.z += twp->pos.z;
            njTranslateV(NULL, &w);
            info = CWP(twp)->info;
            info->center.x = w.x;
            info->center.y = w.y;
            info->center.z = w.z;
            twp->ang.z ? (njRotateZ(NULL, twp->ang.z), 0) : 0;
            twp->ang.x ? (njRotateX(NULL, twp->ang.x), 0) : 0;
            (0x8000 - twp->ang.y) ? (njRotateY(NULL, 0x8000 - twp->ang.y), 0) : 0;
            if (action == 14 && (twp->flag & 3)) {
                v.x = 0.0f;
                v.y = -1.0f;
                v.z = 0.0f;
                njTranslateV(NULL, &v);
                njRotateZ(NULL, 0x2000);
                v.x = 0.7f;
                v.y = 1.09999993f;
                v.z = 0.79999997f;
                njScaleV(NULL, &v);
            }
        }
        if (EWP(twp)->list40 != NULL) {
            func_8C04B57C(twp);
        } else {
            NJS_ARGB argb;

            if (pwp->mj.mtnmode != 2) {
                if (twp->flag & 0x100) {
                    if (!func_8C04FE40()) {
                        twp->btimer++;
                    }
                    if (twp->btimer & 0x11) {
                        action = 16;
                    }
                }
                func_8C040942(pwp->mj.plactptr[action].actptr, pwp->mj.nframe);
                if (action == 14 && (twp->flag & 3)) {
                    njSetConstantAttr(B_8C88F660, B_8C88F664 | 0x100000);
                    frame = pwp->mj.nframe;
                    nbf = (Float)pwp->mj.plactptr[pwp->mj.reqaction].actptr->motion->nbFrame;
                    argb.b = argb.g = argb.r = 0.0f;
                    for (i = 1; i < 3; i++) {
                        frame -= 2.0f;
                        if (frame < 0.0f) {
                            frame += nbf;
                        }
                        argb.a = -((Float)i * 0.33333332f);
                        njSetConstantMaterial(&argb);
                        func_8C040942(pwp->mj.plactptr[pwp->mj.reqaction].actptr, frame);
                    }
                    argb.a = 0.0f;
                    njSetConstantAttr(B_8C88F660, B_8C88F664 & ~0x100000);
                    njSetConstantMaterial(&argb);
                }
            } else {
                func_8C040942(pwp->mj.actwkptr, pwp->mj.nframe);
            }
            if (twp->counter.b[0] == 0 || (twp->timer.b[3] & 4)) {
                if (pwp->mj.reqaction == 37 || pwp->mj.reqaction == 38 || pwp->mj.reqaction == 39
                    || pwp->mj.reqaction == 40 || pwp->mj.reqaction == 41 || (twp->timer.b[3] & 4)) {
                    obj1 = D_8C2606A8;
                    obj2 = D_8C260028;
                    if (pwp->free.f[7] < 0.39999998f) {
                        step = 0x2000;
                    } else {
                        if (pwp->mj.reqaction == 37) {
                            pwp->mj.reqaction = 38;
                            pwp->mj.mtnmode = 2;
                        }
                        step = SA1_FTOI((1.0f - pwp->free.f[7]) * 13653.3322f);
                    }
                } else if (pwp->free.f[6] != 0.0f) {
                    obj1 = D_8C258974;
                    obj2 = D_8C2582E8;
                    step = SA1_FTOI(pwp->free.f[6] * 8192.0f);
                } else {
                    obj1 = D_8C258974;
                    obj2 = D_8C2582E8;
                    step = 0x280;
                }
#if defined(SA1_GA)
                if (sa1_tailsnap.snap && obj1 == D_8C258974) {
                    /* the after-images of the snapped tails: the blades' step (weight 1) or the bent tails' 0x280 */
                    step = pwp->free.f[6] >= 0.5f ? 0x2000 : 0x280;
                }
#endif
                ang1 = saved1 = obj1->ang[0];
                ang2 = saved2 = obj2->ang[0];
                njSetConstantAttr(B_8C88F660, B_8C88F664 | 0x100000);
                argb.r = 0.0f;
                argb.g = 0.0f;
                argb.b = 0.0f;
                argb.a = -0.33333332f;
                j = 0;
                while (j < 2) {
                    if (!func_8C04FE40()) {
                        ang1 -= step;
                        ang2 -= step;
                    }
                    obj1->ang[0] = ang1;
                    obj2->ang[0] = ang2;
                    njSetConstantMaterial(&argb);
                    if (pwp->mj.mtnmode != 2) {
                        func_8C040942(pwp->mj.plactptr[pwp->mj.reqaction].actptr, pwp->mj.nframe);
                    } else {
                        func_8C040942(pwp->mj.actwkptr, pwp->mj.nframe);
                    }
                    j++;
                    argb.a += -0.33333332f;
                }
                argb.a = 0.0f;
                njSetConstantAttr(B_8C88F660, B_8C88F664 & ~0x100000);
                obj1->ang[0] = saved1;
                obj2->ang[0] = saved2;
                obj1->ang[2] = 0xF800;
                obj2->ang[2] = 0xF800;
                njSetConstantMaterial(&argb);
            }
        }
        if (!func_8C04FE40()) {
            if (pwp->mj.reqaction == 37 || pwp->mj.reqaction == 38 || pwp->mj.reqaction == 39
                || pwp->mj.reqaction == 40 || pwp->mj.reqaction == 41 || (twp->timer.b[3] & 4)) {
                func_8C0BDB72(twp, pwp);
            } else if ((pwp->mj.reqaction >= 43 && pwp->mj.reqaction <= 53) || (twp->timer.b[3] & 0x10)) {
                D_8C2606A8->ang[0] = 0;
                D_8C260028->ang[0] = 0;
            } else {
                func_8C0BDC80();
                if (pwp->mj.reqaction == 13 || pwp->mj.reqaction == 12 || pwp->mj.reqaction == 11) {
                    pwp->free.f[6] += 0.01f;
                } else {
                    pwp->free.f[6] -= 0.01f;
                }
                if (pwp->free.f[6] <= 0.0f) {
                    pwp->free.f[6] = 0.0f;
                    if ((Uint16)D_8C258974->ang[0] < 0x1000) {
                        D_8C258974->ang[0] = 0;
                        D_8C2582E8->ang[0] = 0;
                    } else {
                        D_8C258974->ang[0] += 0x333;
                        D_8C2582E8->ang[0] += 0x333;
                    }
                } else if (pwp->free.f[6] >= 1.0f) {
                    pwp->free.f[6] = 1.0f;
                    D_8C25824C->model = D_8C25FF64;
                    D_8C2588D8->model = D_8C2605E4;
                } else {
#if defined(SA1_GA)
                    if (sa1_tailsnap.snap && pwp->free.f[6] >= 0.5f) {
                        /* snapped to 1: the blades, as at f[6] = 1 */
                        D_8C25824C->model = D_8C25FF64;
                        D_8C2588D8->model = D_8C2605E4;
                    } else if (sa1_tailsnap.snap) {
                        /* snapped to 0: the blend at weight 0 (the bent tails of this frame) */
                        func_8C055ECE(D_8C273E3C, D_8C25FF64, D_8C275060, 0.0f);
                        func_8C055ECE(D_8C274448, D_8C2605E4, D_8C27566C, 0.0f);
                        D_8C25824C->model = D_8C275060;
                        D_8C2588D8->model = D_8C27566C;
                    } else {
#endif
                    func_8C055ECE(D_8C273E3C, D_8C25FF64, D_8C275060, pwp->free.f[6]);
                    func_8C055ECE(D_8C274448, D_8C2605E4, D_8C27566C, pwp->free.f[6]);
                    D_8C25824C->model = D_8C275060;
                    D_8C2588D8->model = D_8C27566C;
#if defined(SA1_GA)
                    }
#endif
                }
#if defined(SA1_GA)
                if (sa1_tailsnap.snap) {
                    /* the spin and the angle between the two tails of the snapped weight (as at f[6] = 0 or 1) */
                    Float sw = pwp->free.f[6] >= 0.5f ? 1.0f : 0.0f;

                    D_8C258974->ang[0] += SA1_FTOI(SA1_FMAC(0.80000003f, sw, 0.19999999f) * 4096.0f);
                    D_8C2582E8->ang[0] = D_8C258974->ang[0] + SA1_FTOI(sw * 32768.0f);
                } else {
#endif
                D_8C258974->ang[0] += SA1_FTOI(SA1_FMAC(0.80000003f, pwp->free.f[6], 0.19999999f) * 4096.0f);
                D_8C2582E8->ang[0] = D_8C258974->ang[0] + SA1_FTOI(pwp->free.f[6] * 32768.0f);
#if defined(SA1_GA)
                }
#endif
            }
        }
        njPopMatrix(1);
    }
    func_8C038CEC(0);
    if (func_8C04FE40()) {
        func_8C09247E(twp, &pwp->shadow);
    }
}
#else
ASM_STUB(void func_8C0BA820(task *tp))
#endif
