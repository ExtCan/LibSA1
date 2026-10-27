/*
 * 8C0B4040-8C0B4600 (part of chunk asm/1st_read/text/8C0B33A0.src).
 *
 * Small spark/ball effects placed around a player (pno in
 * twp->counter.b[0]); the texture list of the effect (twp->value.ptr) is
 * one of two picked at random. The creators are func_8C0B3FA6,
 * func_8C0B3FC0 and func_8C0B3FDA (each calls one of the setup
 * functions below with tp->twp, then installs exec/disp).
 *   func_8C0B4040  setup: 5 units along the player's up axis, plus a
 *                  random offset (length 0.2..1.2) in a random direction.
 *   func_8C0B41E0  setup: as above (length 1..3, random z/x rotation,
 *                  player's y rotation); twp->scl = player position.
 *   func_8C0B4332  setup: 5 units along the player's up axis.
 *   func_8C0B4436  display: draws Sonic_Jumpball_Effect at
 *                  player pos + twp->pos, fading with twp->wtimer.
 *   func_8C0B4550  exec: 5 frames of display, then FreeTask.
 * func_8C0B4580 is not a function: it is the unreachable epilogue that
 * SHC leaves after the tail call (BRA) of func_8C0B4550, followed by the
 * literal pool.
 */

#include "sa1/sa1.h"

extern NJS_MATRIX D_8C67C640;           /* unit matrix */
extern NJS_TEXLIST D_8C1C694C;          /* effect texture list A */
extern NJS_TEXLIST D_8C1C6954;          /* effect texture list B */
extern NJS_MODEL Sonic_Jumpball_Effect; /* 8C25654C */

extern void func_8C056926(void);                /* save draw flags (code_8C056860.c) */
extern void func_8C056938(void);                /* restore draw flags */
extern void func_8C05694A(Uint32 a, Uint32 b);  /* set draw flags */

#if defined(SA1_GA)
/*
 * SA1_GA (libsa1's lowered game code only; not the SHC build, the harness's SHC / TARGET_PC builds or their traces): a
 * PC-only host option (docs/research/super2_plan.md 6.1 task 5, super2_lsd.md 4.2 M1, the owner's 0.9.3a note N2;
 * libsa1 SA1_SUPOPT_SUPER_EFFECTS bit SA1_SUPER_EFFECTS_DUST). The game's spin dash dust (8C0B3FA6, set up here) has no
 * Super version: its texlist is Sonic's s_ball_a / s_ball_b (D_8C1C694C / D_8C1C6954) whatever his form. With `on` set
 * and the dust's player (the creator 8C0B2D40 copies the player number into counter.b[0]) Super (playerwk.equipment &
 * 0x8000), the game's random pick is kept and mapped onto the texlists the game's own Super after-images use (B_CHAOS7's
 * ss_ball_a / ss_ball_b, the pick of the Super clone 0C91DB60): tex_a where the game picked D_8C1C694C, tex_b where it
 * picked D_8C1C6954. The addresses are written by libsa1 (LIB src/world/sa1_world_charopt.c, General worlds, whose image
 * holds B_CHAOS7), so this TU names no overlay symbol. A pcdata block (one per world) that libsa1 finds by its magic
 * words. While `on` is 0, the initial value, the function is the original, so worlds that never set it (the reference /
 * exactness worlds) run the game's code; the rand() calls and their order are never changed.
 */
#define SA1_SUPERDUST_MAGIC0 0x54535544  /* "DUST" (sa1_world_charopt.c CO_SUPERDUST_*) */
#define SA1_SUPERDUST_MAGIC1 0x30343034  /* "4040" */
typedef struct SA1_SUPERDUST {
    Uint32 magic[2];            /* 0x00 SA1_SUPERDUST_MAGIC0, SA1_SUPERDUST_MAGIC1 */
    Sint32 on;                  /* 0x08 0: the game's texlists; 1: the Super after-images' while the player is Super */
    Uint32 tex_a;               /* 0x0C the texlist in place of D_8C1C694C (libsa1: 0x0CA6B2D0, ss_ball_a) */
    Uint32 tex_b;               /* 0x10 the texlist in place of D_8C1C6954 (libsa1: 0x0CA6B2D8, ss_ball_b) */
} SA1_SUPERDUST;
SA1_ASSERT_OFFSET(SA1_SUPERDUST, on, 0x08);
SA1_ASSERT_OFFSET(SA1_SUPERDUST, tex_a, 0x0C);
SA1_ASSERT_OFFSET(SA1_SUPERDUST, tex_b, 0x10);
static SA1_SUPERDUST sa1_superdust = { { SA1_SUPERDUST_MAGIC0, SA1_SUPERDUST_MAGIC1 }, 0, 0, 0 };
#endif

/* rotation skipped for a zero angle; written as "?:" so that SHC keeps the
 * angle in a stack slot, as the original does (see code_8C028100.c).
 * With a rand() expression the test and the rotation use two different
 * random numbers (macro argument evaluated twice, as in the original). */
#define ROTATEX(a) ((a) ? njRotateX(NULL, (a)) : (void)0)
#define ROTATEY(a) ((a) ? njRotateY(NULL, (a)) : (void)0)
#define ROTATEZ(a) ((a) ? njRotateZ(NULL, (a)) : (void)0)

void func_8C0B4040(taskwk *twp)
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
        v.y = SA1_FMAC((Float)rand(), 3.0517578125e-05f, 0.19999999f);
        njPushMatrix(D_8C67C640);
        ROTATEZ(SA1_FTOI((Float)rand() * 2.0f));
        ROTATEX(SA1_FTOI((Float)rand() * 2.0f));
        ROTATEY(SA1_FTOI((Float)rand() * 2.0f));
        njCalcVector(NULL, &v, &vo);
        njPopMatrix(2);
        njAddVector(&twp->pos, &vo);
    }
    if ((Float)rand() * 3.0517578125e-05f > 0.5f) {
        twp->value.ptr = &D_8C1C694C;
    } else {
        twp->value.ptr = &D_8C1C6954;
    }
#if defined(SA1_GA)
    if (sa1_superdust.on != 0 && playerpwp[(Uint8)twp->counter.b[0]] != NULL &&
        ((Sint16)playerpwp[(Uint8)twp->counter.b[0]]->equipment & 0x8000)) {
        if (twp->value.ptr == &D_8C1C694C) {
            twp->value.ptr = (void *)sa1_superdust.tex_a;
        } else {
            twp->value.ptr = (void *)sa1_superdust.tex_b;
        }
    }
#endif
}

void func_8C0B41E0(taskwk *twp)
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
        twp->value.ptr = &D_8C1C694C;
    } else {
        twp->value.ptr = &D_8C1C6954;
    }
}

void func_8C0B4332(taskwk *twp)
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
        twp->value.ptr = &D_8C1C694C;
    } else {
        twp->value.ptr = &D_8C1C6954;
    }
    twp->scl.x = 3.0f;
}

/* NOTE: playertwp[pno] is not checked for NULL here (original behaviour) */
void func_8C0B4436(task *tp)
{
    NJS_ARGB argb;
    NJS_VECTOR v;
    taskwk *twp;
    taskwk *ptwp;

    twp = tp->twp;
    ptwp = playertwp[(Uint8)twp->counter.b[0]];
    func_8C056926();
    func_8C05694A(0, 0x2100000);
    argb.r = argb.g = argb.b = 0.0f;
    argb.a = -(twp->wtimer * 0.19999999f);
    njSetConstantMaterial(&argb);
    njSetTexture((NJS_TEXLIST *)twp->value.ptr);
    njPushMatrix(NULL);
    njTranslate(NULL, twp->pos.x + ptwp->pos.x, twp->pos.y + ptwp->pos.y,
                twp->pos.z + ptwp->pos.z);
    ROTATEZ(ptwp->ang.z);
    ROTATEX(ptwp->ang.x);
    ROTATEY(-ptwp->ang.y);
    v.x = 0.0f;
    v.y = -1.0f;
    v.z = 0.0f;
    njTranslateV(NULL, &v);
    njRotateZ(NULL, -0x2000);
    v.x = 0.7f;
    v.y = 1.09999993f;      /* 1.1f */
    v.z = 0.79999997f;      /* 0.8f */
    njScaleV(NULL, &v);
    func_8C036F12(&Sonic_Jumpball_Effect);
    njPopMatrix(1);
    argb.a = 0.0f;
    njSetConstantMaterial(&argb);
    func_8C056938();
}

#ifdef NON_MATCHING
void func_8C0B4550(task *tp)
{
    taskwk *twp;

    twp = tp->twp;
    if (twp->wtimer++ >= 5) {
        FreeTask(tp);
        tp->disp = NULL;
        return;
    }
    func_8C0B4436(tp);
}
#else
ASM_STUB(void func_8C0B4550(task *tp))
#endif
