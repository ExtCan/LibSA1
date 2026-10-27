/*
 * STG05 0C911DC0 (Red Mountain): chunk 0C911DC0-0C912860, functions 1-4
 * of 10 (func_0C9122EE.. are still assembly).
 *
 *  func_0C911DC0  countdown: when no event runs, decrements twp->wtimer and
 *                 when it goes negative calls func_8C107D80, then (unless
 *                 the player is Tails) func_8C107AC6(twp->mode), and
 *                 destroys the task (sound/music calls, roles unverified).
 *  func_0C911E0A  exec of a stage controller: mode 0 runs func_0C9122EE
 *                 (and, for Tails, func_8C04E386(1)); mode 1 runs the
 *                 children and, for Sonic in act 0, calls func_0C91253C
 *                 when player 0 is within 20 units of (-3667, 0, -2319);
 *                 mode 5 -> 1; mode 6 -> func_0C912534.
 *  func_0C911F20  in act 2 (unless D_8C19A8E0 is set) raises the flag
 *                 B_0CADD4C0 and creates the child func_0C911F5C.
 *  func_0C911F5C  exec of that child: keeps player 0 within 60 units of
 *                 (-3147, -1398) in x/z and slides two 2D panels
 *                 (twp->pos.x 576 -> 0, twp->pos.z -576 -> 0, accelerating
 *                 by 2 per frame; smode 3 slides them back by 4 per frame
 *                 and smode 4 clears the flags). While the panels are open
 *                 (smode <= 2), 61 frames with player 0 in mode 11 play SE
 *                 12 and start closing them. Tail-calls func_0C9121E4.
 *  func_0C9121E4  draws the two panels (sprites of D_0CADA298 with texlist
 *                 D_0CADA1B4, alpha 0.6, priority -11).
 *
 * FUNCTION BOUNDARIES (manifest): func_0C9121E4 is a separate function
 * (own prologue, reached only by the BRA tail call at 0C9121D0) but the
 * split has no label for it, so the manifest range of func_0C911F5C
 * (0C911F5C-0C9122EE) holds both functions. Likewise 0C912534 (one store:
 * D_0CADA294 = 0) and 0C91253C (a call sequence ending in a BRA to
 * func_0C9122EE) are separate functions inside the manifest range of
 * func_0C9122EE; func_0C911E0A tail-calls both with BRA. Their names
 * below are the addresses. func_0C911E0A and func_0C911F5C compile to the
 * original instructions (verified with shcdiff after adding the three
 * entry points to the function list and symbol table), but shcdiff and
 * the splicer cannot verify them until the manifest/symbols know these
 * entry points, so they are kept under NON_MATCHING for the build.
 *
 * Code generation: func_0C911F20 needs the nested ifs and the unused
 * result variable (a plain call statement becomes a JMP tail call, "&&"
 * changes the register allocation). func_0C911E0A keeps the squared
 * distance in a local (FMOV FR0,FR4 before the compare). func_0C911F5C:
 * twp is loaded after ptwp, and cos * dist / sin * dist go to locals
 * before the stores (pos.x is stored after the njSin call). SA1_FMAC: one
 * FMAC in the asm (0C911FB0), one SA1_FMAC.
 *
 * B_0CADD4C0 lies past the end of the STG05 image (0x0CADD2E6), in the
 * overlay's BSS, which the split does not label: the pools hold the plain
 * number H'0CADD4C0. Under SHC it is spelled as that address so that the
 * pool entries compare equal; on PC it is an ordinary variable.
 */
#include "common.h"
#include "sa1/sa1.h"

/* STG05 BSS flag word (see above) */
#if defined(TARGET_PC)
extern Sint32 B_0CADD4C0;
#else
#define B_0CADD4C0 (*(Sint32 *)0x0CADD4C0)
#endif

/* STG05 data (extern only) */
extern NJS_TEXLIST D_0CADA1B4;          /* panel texlist (3 textures) */
extern NJS_TEXANIM D_0CADA298[];        /* 3 cells 512x128, centre (256, 64), texid 0/2/1 */

/* 1ST_READ functions and data not declared in the sa1 headers */
extern void func_8C107D80(void);        /* as in code_8C084BA0.c */
extern void func_8C107AC6(Sint32 a);    /* as in code_8C084BA0.c */
void func_8C04E386(Sint16 mode);        /* code_8C07BE68.c */
extern Sint32 D_8C19A8E0;
extern Sint8 B_8C752BDD;

void func_0C911F5C(task *tp);
void func_0C9121E4(task *tp);
void func_0C9122EE(task *tp);
void func_0C912534(task *tp);
void func_0C91253C(task *tp);

void func_0C911DC0(task *tp)
{
    taskwk *twp;

    twp = tp->twp;
    if (func_8C049A4E() == 0) {
        twp->wtimer--;
        if ((Sint16)twp->wtimer < 0) {
            func_8C107D80();
            if (GetPlayerNumber() != 2) {
                func_8C107AC6(twp->mode);
            }
            DestroyTask(tp);
        }
    }
}

#ifdef NON_MATCHING
void func_0C911E0A(task *tp)
{
    taskwk *twp;
    taskwk *ptwp;
    Float x;
    Float y;
    Float z;
    Float d;

    twp = tp->twp;
    switch (twp->mode) {
    case 0:
        func_0C9122EE(tp);
        if (GetPlayerNumber() == 2) {
            func_8C04E386(1);
            return;
        }
        break;
    case 1:
        func_8C098E98(tp);
        if (GetPlayerNumber() == 0 && (Uint8)GetStageNumber() == 0) {
            ptwp = playertwp[0];
            x = ptwp->pos.x;
            y = ptwp->pos.y;
            z = ptwp->pos.z;
            d = func_8C02CF68(-3667.0f - x, 0.0f - y, -2319.0f - z);
            if (d < 400.0f) {
                func_0C91253C(tp);
                return;
            }
        }
        break;
    case 5:
        twp->mode = 1;
        break;
    case 6:
        func_0C912534(tp);
        return;
    }
}
#else
ASM_STUB(void func_0C911E0A(task *tp))
#endif

void func_0C911F20(task *tp)
{
    task *ctp;

    B_0CADD4C0 = 0;
    if ((Uint8)GetStageNumber() == 2) {
        if (D_8C19A8E0 == 0) {
            B_0CADD4C0 = 1;
            B_8C752BDD = -1;
            /* the unused result keeps the JSR (no tail call) */
            ctp = CreateChildTask(2, func_0C911F5C, tp);
            PC_ONLY((void)ctp;)
        }
    }
}

#ifdef NON_MATCHING
void func_0C911F5C(task *tp)
{
    taskwk *twp;
    taskwk *ptwp;
    Angle ang;
    Float x;
    Float z;
    Float dist;

    ptwp = playertwp[0];
    twp = tp->twp;
    if (B_0CADD4C0 == 0) {
        return;
    }
    x = ptwp->pos.x + 3147.0f;
    z = ptwp->pos.z + 1398.0f;
    ang = SA1_FTOI(atan2f(z, x) * 10430.381f);
    dist = njSqrt(SA1_FMAC(x, x, z * z));
    if (dist > 60.0f) {
        dist = 60.0f;
    }
    x = njCos(ang) * dist;
    z = njSin(ang) * dist;
    ptwp->pos.x = x + -3147.0f;
    ptwp->pos.z = z + -1398.0f;

    switch (twp->smode) {
    case 0:
        twp->pos.x = 576.0f;
        twp->pos.z = -576.0f;
        twp->scl.x = 0.0f;
        twp->scl.z = 0.0f;
        twp->btimer = 0;
        twp->wtimer = 0;
        twp->smode = 1;
        break;
    case 1:
        twp->scl.x -= 2.0f;
        twp->pos.x += twp->scl.x;
        if (twp->pos.x < 0.0f) {
            twp->pos.x = 0.0f;
        }
        twp->scl.z += 2.0f;
        twp->pos.z += twp->scl.z;
        if (twp->pos.z > 0.0f) {
            twp->pos.z = 0.0f;
        }
        if (twp->pos.x == 0.0f && twp->pos.z == 0.0f) {
            twp->smode = 2;
        }
        break;
    case 2:
        break;
    case 3:
        if (twp->scl.x < 0.0f) {
            twp->scl.x = 0.0f;
        }
        if (twp->scl.z > 0.0f) {
            twp->scl.z = 0.0f;
        }
        twp->scl.x += 4.0f;
        twp->pos.x += twp->scl.x;
        if (twp->pos.x > 576.0f) {
            twp->pos.x = 576.0f;
        }
        twp->scl.z -= 4.0f;
        twp->pos.z += twp->scl.z;
        if (twp->pos.z < -576.0f) {
            twp->pos.z = -576.0f;
        }
        if (twp->pos.x == 576.0f && twp->pos.z == -576.0f) {
            twp->smode = 4;
        }
        break;
    case 4:
        B_0CADD4C0 = 0;
        B_8C752BDD = 0;
        break;
    }

    if (twp->smode <= 2) {
        if (twp->wtimer == 0) {
            if (ptwp->mode == 11) {
                twp->wtimer = 1;
            }
        } else if (ptwp->mode != 11) {
            twp->wtimer = 0;
            twp->btimer = 60;
        } else {
            twp->wtimer++;
            if (twp->wtimer > 60) {
                func_8C0940DA(12, 0, 0, 0);
                twp->smode = 3;
            }
        }
    }
    func_0C9121E4(tp);
}
#else
ASM_STUB(void func_0C911F5C(task *tp))
#endif

void func_0C9121E4(task *tp)
{
    taskwk *twp;
    NJS_SPRITE sp;
    NJS_ARGB argb;
    Sint32 n;

    twp = tp->twp;
    if (B_8C754EC8 != 0) {
        return;
    }
    njSetTexture(&D_0CADA1B4);
    PC_ONLY(sp.p.z = 0.0f; sp.ang = 0;)     /* never set by the original */
    sp.tlist = &D_0CADA1B4;
    sp.tanim = D_0CADA298;
    sp.sx = 1.0f;
    sp.sy = 1.0f;
    sp.p.x = twp->pos.x + 320.0f;
    sp.p.y = 96.0f;
    argb.a = 0.59999998f;               /* 0x3F199999 */
    argb.r = 1.0f;
    argb.g = 1.0f;
    argb.b = 1.0f;
    njSetConstantMaterial(&argb);
    njDrawSprite2D(&sp, 0, -11.0f, 32);     /* 32: NJD_SPRITE_ALPHA */
    sp.p.x = twp->pos.z + 320.0f;
    sp.p.y = 384.0f;
    if (twp->btimer != 0) {
        twp->btimer--;
        n = 2;
    } else {
        n = 1;
    }
    njDrawSprite2D(&sp, n, -11.0f, 32);
    argb.a = 0.0f;
    argb.r = 0.0f;
    argb.g = 0.0f;
    argb.b = 0.0f;
    njSetConstantMaterial(&argb);
}

ASM_STUB(void func_0C9122EE(task *tp))
ASM_STUB(void func_0C912534(task *tp))
ASM_STUB(void func_0C91253C(task *tp))
