/*
 * 1ST_READ 8C0A77BC: SonicMain, the exec of Sonic's player task
 * (chunk 8C0A77A0-8C0A9660, function 2/2; created by LoadPlayerTask 8C04F3C0,
 * also called by 8C07F560 and 8C083F7C).
 *
 * Per frame: ride flags from the collision work, the mode machine
 * (SonicModeMachine 8C0A966C), the attribute bits of Sonic's CCL_INFO, then a
 * switch on twp->mode (0-83) running the physics steps of the current mode,
 * then motion, the shoe/crystal-ring weld parts, shadow and the per-frame
 * collision entry.
 *
 * Code generation notes (the C below compiles to the original instructions):
 *  - every local lives on the stack (SHC does that for this very large
 *    function); slots are shared only between sibling block scopes, so the
 *    per-case temporaries are declared in blocks;
 *  - SHC cross-jumps identical tails of "...; break;" sequences, but not of
 *    statements inside if/else arms: cases 2, 6, 46 and 72 use if/else;
 *  - the shoe/ring objects are declared as arrays so the store is
 *    "base + @(48,Rn)" instead of a folded "sym+48" pool word;
 *  - playerwk.equipment/item are tested as signed halfwords (no EXTU.W).
 *
 * NON_MATCHING only because of tools/shcdiff.py (and the splice, which uses
 * it): the instruction stream is identical (3021/3021), but the MOVA that
 * addresses the switch jump table is compared against the table's first
 * ".DATA.W Lx-Ly" item, an expression shcdiff cannot evaluate. Compiling
 * func_8C0A77A0 + SonicMain together (original alignment) reproduces all
 * non-relocated bytes of 8C0A77A0-8C0A9644, jump table and padding included.
 */
#include "common.h"
#include "sa1/sa1.h"
#include "sa1/protos.h"     /* func_8C0B27EE (Bool, Sint8 pno, NJS_OBJECT **), func_8C0AF012 */

/* Super Sonic's object in the B_CHAOS7 overlay (config/symbols.b_chaos7.txt);
 * 1ST_READ refers to it by its absolute address. */
#if defined(TARGET_PC)
extern NJS_OBJECT B_CHAOS7_Super_Sonic;
#define SUPER_SONIC_OBJECT (&B_CHAOS7_Super_Sonic)
#else
#define SUPER_SONIC_OBJECT ((NJS_OBJECT *)0x0C909D2C)
#endif

/* rand() scaled to [0, 1) (inline in the game code: rand() * 1/32768) */
#define SONIC_RAND() ((Float)rand() * 3.0517578125e-05f)

extern Sint16 D_8C19B468;               /* 1: force mode 83 */
extern CCL_INFO D_8C1C5E80[];           /* Sonic's collision info (1 entry) */
extern void *D_8C1C5F08[];              /* head / eyeL / eyeR objects passed to func_8C0B27EE (void * as in sa1/player.h) */
extern NJS_OBJECT D_8C1C7D70[];         /* nodes of Sonic's model whose sibling is the shoe/ring part */
extern NJS_OBJECT D_8C1C8CA4[];
extern NJS_OBJECT D_8C1CAD20[];
extern NJS_OBJECT D_8C1C7938;           /* plain left shoe */
extern NJS_OBJECT D_8C1C8888;           /* plain right shoe */
extern NJS_OBJECT D_8C1CACEC;           /* no crystal ring */
extern NJS_OBJECT Sonic_Left_Light_Speed_Shoe_Worn;
extern NJS_OBJECT Sonic_Right_Light_Speed_Shoe_Worn;
extern NJS_OBJECT Sonic_Crystal_Ring_Worn;

void SonicModeMachine(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C01A7A4(NJS_POINT3 *v);
void func_8C04B9E0(taskwk *twp);
Sint32 func_8C05E7D4(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C06C6F8(taskwk *twp, motionwk *mwp, playerwk *pwp);
Sint32 func_8C06C880(taskwk *twp);
void func_8C06CA4E(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C06D136(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C06F25C(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C06F386(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C06FB8C(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0700C2(taskwk *twp, motionwk *mwp, playerwk *pwp, Float max_spd);
void func_8C07061C(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C070814(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C07095E(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C070EDA(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0711D2(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0714B6(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C071656(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C071B26(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C071D40(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C071DD8(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0725C0(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C07266E(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C078308(taskwk *twp, motionwk *mwp, playerwk *pwp);
Bool func_8C0784F8(taskwk *twp, motionwk *mwp, playerwk *pwp);
Bool func_8C078710(taskwk *twp, motionwk *mwp, playerwk *pwp);
Sint32 func_8C078856(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0792DC(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C07B2B8(Uint8 pno, Sint32 n);
void func_8C07B320(taskwk *twp);
void func_8C07B694(taskwk *twp, motionwk *mwp, playerwk *pwp);
Sint32 func_8C07D7B4(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0925A0(taskwk *twp, shadowwk *shadow);
void func_8C094330(Sint32 n, taskwk *twp, Sint32 a, Sint32 b, Sint32 c);
void func_8C0945A0(Sint32 n);
void func_8C098FAE(Sint32 n);
void func_8C0AE0E0(task *tp);
void func_8C0AE110(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0AE17A(task *tp);
void func_8C0AE3D6(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0AE588(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0AE6D8(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0AE760(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0AE97E(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0AEBC0(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0AF012(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0AF280(taskwk *twp, motionwk *mwp, playerwk *pwp);
Sint32 func_8C0AF6C4(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0AF90A(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0B1DE0(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0B1FD8(taskwk *twp, motionwk *mwp, playerwk *pwp);
Angle func_8C0B2022(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0B2120(taskwk *twp, motionwk *mwp, playerwk *pwp);
void func_8C0B2338(task *tp);
void func_8C0B2376(task *tp);
void func_8C0B2C8C(taskwk *twp);
void func_8C0B2CB2(taskwk *twp);
void func_8C0B2D40(taskwk *twp);
void func_8C0B2D66(taskwk *twp);
void func_8C0B2D8C(taskwk *twp);
void func_8C0B5B80(taskwk *twp, Sint32 n);

#ifdef NON_MATCHING
void SonicMain(task *tp)
{
    taskwk *twp;
    motionwk *mwp;
    playerwk *pwp;

    twp = tp->twp;
    mwp = tp->mwp;
    pwp = PWP(mwp);
    if ((twp->flag & 0x20) && mwp->spd.y <= 0.0f) {
        twp->flag |= 2;
        twp->ang.x = CWP(twp)->mytask->twp->ang.x;
        twp->ang.z = CWP(twp)->mytask->twp->ang.z;
    } else {
        twp->flag &= ~2;
    }
    if (D_8C19B468 == 1) {
        twp->mode = 83;
    } else if (twp->mode != 82) {
        SonicModeMachine(twp, mwp, pwp);
    }
    if (twp->cwp != NULL) {
        Sint32 a;
        Sint32 b;
        CCL_INFO *info;

        info = CWP(twp)->info;
        if ((Sint16)pwp->item & 0x8000) {
            a = 3;
            b = 3;
        } else if ((Sint16)pwp->equipment & 0x8000) {
            a = 3;
            b = 3;
        } else if (twp->mode == 44) {
            a = 2;
            b = 3;
        } else if (twp->mode == 6) {
            a = 1;
            b = 3;
        } else if (twp->flag & 0x100) {
            a = 1;
            b = 1;
        } else if (twp->mode == 45) {
            a = 0;
            b = 1;
        } else {
            a = 0;
            b = 0;
        }
        info->flag = (info->flag & ~3) | a;
        info->flag = (info->flag & ~0xC) | (b << 2);
    }
    if (twp->flag & 0x100) {
        pwp->free.w[1]++;
    }
    switch (twp->mode) {
    case 0:
        twp->counter.b[1] = SA1_CHR_SONIC;
        PInitialize((Uint8)twp->counter.b[0], tp);
        pwp = PWP(mwp);
        pwp->mj.plactptr = SonicActionList;
        pwp->mj.reqaction = 0;
        CCL_Init(tp, D_8C1C5E80, 0, 1, 1);
        CWP(twp)->info->a = pwp->p.rad;
        twp->mode = 1;
        pwp->waittimer = 0;
        func_8C098FAE(0);
        pwp->free.f[6] = 0.0f;
        pwp->free.f[7] = 0.0f;
        func_8C0B27EE(twp->counter.b[0], (NJS_OBJECT **)D_8C1C5F08);
        PSetMotion(&pwp->mj);
        {
            PL_JOIN_VERTEX *jv;

            jv = SonicWeldList;
        if (((Uint16)GetStageNumber() & 0xFF00) == 0x1300) {
            for (; jv->objptr != NULL; jv++) {
                if (jv->objptr == SUPER_SONIC_OBJECT) {
                    jv->inpmode = 2;
                }
            }
        } else {
            for (; jv->objptr != NULL; jv++) {
                if (jv->objptr == SUPER_SONIC_OBJECT) {
                    jv->inpmode = 0;
                }
            }
        }
        }
        pwp->mj.pljvptr = SonicWeldList;
        PJoinVertexes(twp, mwp, pwp);
        {
            task *ctp;

            ctp = CreateElementalTask(2, 3, func_8C0B2338);
        if (ctp != NULL) {
            ctp->twp->counter.b[0] = twp->counter.b[0];
        }
        ctp = CreateElementalTask(3, 3, func_8C0B2376);
        if (ctp != NULL) {
            ctp->twp->counter.b[0] = twp->counter.b[0];
        }
        }
        tp->dest = func_8C0AE0E0;
        tp->disp = func_8C0AE17A;
        break;
    case 1:
        func_8C071700(twp, mwp, pwp);
        PGetAcceleration(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        func_8C0AE3D6(twp, mwp, pwp);
        break;
    case 2:
        if (pwp->mj.lastaction == 30 && pwp->mj.action != 30) {
            pwp->spd.x += pwp->p.jog_speed;
        }
        PGetAcceleration(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        if (PSetPosition(twp, mwp, pwp) == 2) {
            if (twp->counter.b[0] == 0) {
                func_8C0940DA(0x458, 0, 0, 0);
            }
            twp->mode = 18;
            pwp->mj.reqaction = 27;
            func_8C07B320(twp);
            pwp->spd.x = -(pwp->spd.x * 0.29999999f);
            pwp->spd.z = -(pwp->spd.z * 0.29999999f);
        } else {
            func_8C079096(twp, mwp, pwp);
            func_8C0AE588(twp, mwp, pwp);
        }
        break;
    case 3:
        func_8C06F25C(twp, mwp, pwp);
        func_8C0725C0(twp, mwp, pwp);
        func_8C078308(twp, mwp, pwp);
        func_8C0AE588(twp, mwp, pwp);
        break;
    case 4:
        func_8C071700(twp, mwp, pwp);
        func_8C070EDA(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        func_8C0B2D40(twp);
        if ((Sint16)pwp->equipment & 2) {
            Sint32 i;

            if (SONIC_RAND() < 0.099999996f) {
                for (i = 0; i < 3; i++) {
                    func_8C0B5B80(twp, 31);
                }
            }
        }
        break;
    case 5:
        func_8C071700(twp, mwp, pwp);
        func_8C0714B6(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        if (pwp->spd.x >= pwp->p.run_speed) {
            func_8C0B2C8C(twp);
            pwp->free.b[0] |= 1;
            func_8C0B2D66(twp);
        }
        break;
    case 6:
        func_8C0AF90A(twp, mwp, pwp);
        if (PSetPosition(twp, mwp, pwp) == 2) {
            func_8C0940DA(0x458, 0, 0, 0);
            twp->mode = 18;
            pwp->mj.reqaction = 27;
            twp->flag &= ~0x400;
            pwp->free.w[2] = 0;
            func_8C0945A0(725);
            func_8C07B320(twp);
        } else {
            func_8C079096(twp, mwp, pwp);
            func_8C0B2C8C(twp);
            pwp->free.b[0] |= 1;
        }
        break;
    case 7:
        func_8C0714B6(twp, mwp, pwp);
        func_8C0725C0(twp, mwp, pwp);
        func_8C078308(twp, mwp, pwp);
        func_8C0B2D66(twp);
        break;
    case 8:
        func_8C071D40(twp, mwp, pwp);
        if (!func_8C07D7B4(twp, mwp, pwp)) {
            func_8C06E904(twp, mwp, pwp);
        }
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 9:
        func_8C06E904(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 10:
        if (pwp->spd.y >= pwp->p.nocon_speed) {
            func_8C0711D2(twp, mwp, pwp);
        } else {
            func_8C06E904(twp, mwp, pwp);
        }
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 11:
        func_8C071656(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 12:
        func_8C071D40(twp, mwp, pwp);
        if (!func_8C07D7B4(twp, mwp, pwp)) {
            func_8C06E904(twp, mwp, pwp);
        }
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 13:
        func_8C070EDA(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 14:
        func_8C071D40(twp, mwp, pwp);
        func_8C0AF280(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        func_8C0B2C8C(twp);
        pwp->free.b[0] |= 1;
        func_8C0B2D66(twp);
        break;
    case 15:
        if (pwp->mj.reqaction == 21) {
            func_8C070EDA(twp, mwp, pwp);
        } else {
            func_8C06F386(twp, mwp, pwp);
            if (func_8C06C880(twp) & 0x400) {
                func_8C094330(15, twp, 1, 0, 2);
            }
            if (pwp->mj.lastaction == 21) {
                func_8C0940DA(0x45C, 0, 0, 0);
            }
        }
        func_8C07266E(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 16:
        if (twp->flag & 3) {
            func_8C071D40(twp, mwp, pwp);
        }
        func_8C070EDA(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 17:
        func_8C070EDA(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 18:
        func_8C070EDA(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 19:
        func_8C071700(twp, mwp, pwp);
        func_8C0711D2(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 36:
    case 37:
        func_8C0784F8(twp, mwp, pwp);
        func_8C0AE760(twp, mwp, pwp);
        break;
    case 38:
    case 39:
        func_8C078710(twp, mwp, pwp);
        func_8C0AE97E(twp, mwp, pwp);
        break;
    case 40:
        if (func_8C05E7D4(twp, mwp, pwp) <= 31) {
            pwp->mj.reqaction = 72;
        }
        break;
    case 41:
        break;
    case 42:
        func_8C0711D2(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 43: {
        Float mag;
        Angle ang;

        func_8C07B2B8(0, 30);
        func_8C0711D2(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        if (GetAnalog(twp, &ang, &mag)) {
            mwp->spd.x = mag * (0.5f - SONIC_RAND());
            if ((twp->flag & 3) && SONIC_RAND() > 0.79999997f) {
                /* SHC folds the two constants (1/32768 * 0.2 = 0x36CCCCCC, exact):
                 * FMAC (Float)rand(), K, 0.3 */
                mwp->spd.y = SA1_FMAC((Float)rand(),
                                      3.0517578125e-05f * 0.19999999f,
                                      0.29999999f);
            }
            mwp->spd.z = mag * (0.5f - SONIC_RAND());
        }
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    }
    case 44:
        func_8C071D40(twp, mwp, pwp);
        if (!func_8C0AF6C4(twp, mwp, pwp)) {
            twp->mode = 12;
            pwp->mj.reqaction = 18;
            twp->flag &= ~0x500;
        }
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        func_8C0B2C8C(twp);
        pwp->free.b[0] |= 1;
        break;
    case 20:
        break;
    case 21:
        func_8C0711D2(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 22:
        func_8C0711D2(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 23:
        func_8C071700(twp, mwp, pwp);
        PGetAcceleration(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 24:
        func_8C06F386(twp, mwp, pwp);
        func_8C07266E(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 26:
        func_8C06D136(twp, mwp, pwp);
        func_8C0711D2(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 25:
        if (pwp->mj.lastaction == 36 && pwp->mj.action != 36) {
            pwp->spd.x += pwp->p.jog_speed;
        }
        PGetAcceleration(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        func_8C0AE588(twp, mwp, pwp);
        break;
    case 27:
        func_8C070EDA(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 28:
        func_8C071D40(twp, mwp, pwp);
        if (!func_8C07D7B4(twp, mwp, pwp)) {
            func_8C06E904(twp, mwp, pwp);
        }
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 29:
        func_8C071D40(twp, mwp, pwp);
        if (!func_8C07D7B4(twp, mwp, pwp)) {
            func_8C06E904(twp, mwp, pwp);
        }
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 30:
        func_8C06E904(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 31:
        if (pwp->spd.y >= pwp->p.nocon_speed) {
            func_8C0711D2(twp, mwp, pwp);
        } else {
            func_8C06E904(twp, mwp, pwp);
        }
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 32:
        func_8C06F386(twp, mwp, pwp);
        func_8C07266E(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        if (func_8C06C880(twp) & 0x400) {
            func_8C094330(15, twp, 1, 0, 2);
        }
        break;
    case 33:
        func_8C071D40(twp, mwp, pwp);
        func_8C06D136(twp, mwp, pwp);
        func_8C0711D2(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 34:
        func_8C06CA4E(twp, mwp, pwp);
        break;
    case 35:
        pwp->htp->mwp->acc.x = pwp->htp->mwp->acc.y = pwp->htp->mwp->acc.z
            = pwp->mj.nframe * 0.01f;
        break;
    case 45:
        break;
    case 46:
        func_8C06FB8C(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        if (func_8C078856(twp, mwp, pwp) == 2) {
            func_8C0940DA(0x458, 0, 0, 0);
            twp->mode = 50;
            pwp->mj.reqaction = 46;
            func_8C07B320(twp);
            SA1_FMAC_ACC(twp->pos.x, pwp->cstsp->tnorm.x, 5.0f);
            SA1_FMAC_ACC(twp->pos.y, pwp->cstsp->tnorm.y, 5.0f);
            SA1_FMAC_ACC(twp->pos.z, pwp->cstsp->tnorm.z, 5.0f);
            pwp->spd.x *= 0.19999999f;
            pwp->spd.y = pwp->p.jmp_y_spd;
            twp->ang.x = 0;
            twp->ang.z = 0xC000;
        } else {
            func_8C0792DC(twp, mwp, pwp);
            func_8C0AE588(twp, mwp, pwp);
        }
        break;
    case 47:
        func_8C06FB8C(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        if (func_8C078856(twp, mwp, pwp) == 2) {
            func_8C0940DA(0x458, 0, 0, 0);
            twp->flag &= ~0x500;
            twp->mode = 50;
            pwp->mj.reqaction = 46;
            func_8C07B320(twp);
            SA1_FMAC_ACC(twp->pos.x, pwp->cstsp->tnorm.x, 5.0f);
            SA1_FMAC_ACC(twp->pos.y, pwp->cstsp->tnorm.y, 5.0f);
            SA1_FMAC_ACC(twp->pos.z, pwp->cstsp->tnorm.z, 5.0f);
            pwp->spd.x *= 0.19999999f;
            pwp->spd.y = pwp->p.jmp_y_spd;
            twp->ang.x = 0;
            twp->ang.z = 0xC000;
        } else {
            func_8C0792DC(twp, mwp, pwp);
        }
        if (pwp->spd.x >= pwp->p.run_speed) {
            func_8C0B2C8C(twp);
            pwp->free.b[0] |= 1;
            func_8C0B2D66(twp);
        }
        break;
    case 49:
        func_8C071DD8(twp, mwp, pwp);
        func_8C06FB8C(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        if (func_8C078856(twp, mwp, pwp) == 2) {
            func_8C0940DA(0x458, 0, 0, 0);
            twp->mode = 50;
            pwp->mj.reqaction = 46;
            func_8C07B320(twp);
            SA1_FMAC_ACC(twp->pos.x, pwp->cstsp->tnorm.x, 5.0f);
            SA1_FMAC_ACC(twp->pos.y, pwp->cstsp->tnorm.y, 5.0f);
            SA1_FMAC_ACC(twp->pos.z, pwp->cstsp->tnorm.z, 5.0f);
            pwp->spd.x *= 0.19999999f;
            pwp->spd.y = pwp->p.jmp_y_spd;
            twp->ang.x = 0;
            twp->ang.z = 0xC000;
            twp->flag &= ~0x500;
        } else {
            func_8C0792DC(twp, mwp, pwp);
        }
        func_8C0792DC(twp, mwp, pwp);
        break;
    case 48:
        func_8C071DD8(twp, mwp, pwp);
        func_8C06FB8C(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        if (func_8C078856(twp, mwp, pwp) == 2) {
            func_8C0940DA(0x458, 0, 0, 0);
            twp->mode = 50;
            pwp->mj.reqaction = 46;
            func_8C07B320(twp);
            SA1_FMAC_ACC(twp->pos.x, pwp->cstsp->tnorm.x, 5.0f);
            SA1_FMAC_ACC(twp->pos.y, pwp->cstsp->tnorm.y, 5.0f);
            SA1_FMAC_ACC(twp->pos.z, pwp->cstsp->tnorm.z, 5.0f);
            pwp->spd.x *= 0.19999999f;
            pwp->spd.y = pwp->p.jmp_y_spd;
            twp->ang.x = 0;
            twp->ang.z = 0xC000;
        } else {
            func_8C0792DC(twp, mwp, pwp);
        }
        func_8C0792DC(twp, mwp, pwp);
        break;
    case 50:
        func_8C06FB8C(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        twp->pos.y += mwp->spd.y;
        func_8C0792DC(twp, mwp, pwp);
        break;
    case 51:
        func_8C0B1FD8(twp, mwp, pwp);
        break;
    case 52: {
        taskwk *htwp;
        Float y;

        htwp = pwp->htp->twp;
        twp->pos.y -= 0.5f;
        y = htwp->pos.y + CWP(htwp)->info->center.y - CWP(htwp)->info->b;
        if (y >= twp->pos.y) {
            twp->pos.y = y;
        }
        func_8C0B1FD8(twp, mwp, pwp);
        break;
    }
    case 53:
        twp->ang.y += func_8C0B2022(twp, mwp, pwp);
        func_8C0B1FD8(twp, mwp, pwp);
        break;
    case 54:
        twp->ang.y -= func_8C0B2022(twp, mwp, pwp);
        func_8C0B1FD8(twp, mwp, pwp);
        break;
    case 55:
        func_8C0B2120(twp, mwp, pwp);
        break;
    case 56:
        func_8C0B2120(twp, mwp, pwp);
        break;
    case 57:
        twp->ang.y += 0x300;
        twp->ang.y &= 0xFE00;
        func_8C0B2120(twp, mwp, pwp);
        break;
    case 58:
        twp->ang.y -= 0x300;
        twp->ang.y &= 0xFE00;
        func_8C0B2120(twp, mwp, pwp);
        break;
    case 59:
        func_8C0B2120(twp, mwp, pwp);
        break;
    case 60:
        twp->pos.y += 0.29999999f;
        func_8C0B2120(twp, mwp, pwp);
        break;
    case 61:
        func_8C071D40(twp, mwp, pwp);
        func_8C0714B6(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 62:
        func_8C0700C2(twp, mwp, pwp, 5.9799992f);
        PGetSpeed(twp, mwp, pwp);
        if (PSetPosition(twp, mwp, pwp) == 2) {
            if (twp->counter.b[0] == 0) {
                func_8C0940DA(0x458, 0, 0, 0);
            }
            twp->mode = 68;
            if (VectorAngle(&mwp->spd, &pwp->wall_normal, &pwp->cstsp->tnorm) > 0x8000) {
                pwp->mj.reqaction = 110;
            } else {
                pwp->mj.reqaction = 109;
            }
            func_8C07B320(twp);
            if ((Uint16)GetStageNumber() == 0x802) {
                func_8C0940DA(235, 0, 0, 0);
            }
        } else {
            func_8C079096(twp, mwp, pwp);
            func_8C0AEBC0(twp, mwp, pwp);
        }
        func_8C07B694(twp, mwp, pwp);
        break;
    case 64:
        func_8C07095E(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        func_8C07B694(twp, mwp, pwp);
        break;
    case 65:
        func_8C071D40(twp, mwp, pwp);
        func_8C07061C(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 66:
        func_8C071D40(twp, mwp, pwp);
        func_8C07061C(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 67:
        func_8C071D40(twp, mwp, pwp);
        func_8C070814(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        if (PSetPosition(twp, mwp, pwp)) {
            pwp->mj.reqaction = 112;
        }
        func_8C079096(twp, mwp, pwp);
        break;
    case 68:
        func_8C07095E(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        func_8C07B694(twp, mwp, pwp);
        break;
    case 69: {
        Float mag;
        NJS_POINT3 v;
        Angle rang;

        if (GetAnalog(twp, NULL, &mag) && mag != 0.0f) {
            rang = SA1_FTOI(SONIC_RAND() * 65536.0f);
            v.x = njSin(rang) * mag;
            v.z = njCos(rang) * mag;
            v.y = 0.0f;
            func_8C01A7A4(&v);
            if (SONIC_RAND() > 0.79999997f) {
                func_8C07CB54(twp->counter.b[0], v.x * 0.5f, v.y, v.z * 0.099999996f);
            }
        }
        func_8C071B26(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        if (twp->flag & 1) {
            mwp->spd.y = 0.0f;
        }
        func_8C079096(twp, mwp, pwp);
        if (pwp->spd.x >= pwp->p.run_speed) {
            func_8C0B2C8C(twp);
            pwp->free.b[0] |= 1;
            func_8C0B2D66(twp);
        }
        break;
    }
    case 70: {
        Float mag;
        NJS_POINT3 v;
        Angle rang;

        if (GetAnalog(twp, NULL, &mag) && mag != 0.0f) {
            rang = SA1_FTOI(SONIC_RAND() * 65536.0f);
            v.x = njSin(rang) * mag;
            v.z = njCos(rang) * mag;
            v.y = 0.0f;
            func_8C01A7A4(&v);
        }
        func_8C071D40(twp, mwp, pwp);
        func_8C0714B6(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        if (pwp->spd.x >= pwp->p.run_speed) {
            func_8C0B2C8C(twp);
            pwp->free.b[0] |= 1;
            func_8C0B2D66(twp);
        }
        break;
    }
    case 71:
        func_8C071700(twp, mwp, pwp);
        PGetAcceleration(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 72:
        PGetAcceleration(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        if (PSetPosition(twp, mwp, pwp) == 2) {
            twp->mode = 71;
            pwp->mj.reqaction = 130;
            pwp->spd.x = -(pwp->spd.x * 0.29999999f);
            pwp->spd.z = -(pwp->spd.z * 0.29999999f);
        } else {
            func_8C079096(twp, mwp, pwp);
            func_8C0AE6D8(twp, mwp, pwp);
        }
        break;
    case 73:
        func_8C06F25C(twp, mwp, pwp);
        func_8C0725C0(twp, mwp, pwp);
        func_8C078308(twp, mwp, pwp);
        func_8C0AE6D8(twp, mwp, pwp);
        func_8C0B2CB2(twp);
        pwp->free.b[0] |= 1;
        break;
    case 78:
        func_8C071D40(twp, mwp, pwp);
        func_8C06E904(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 76:
        func_8C06E904(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 75:
        if (pwp->spd.y >= pwp->p.nocon_speed) {
            func_8C0711D2(twp, mwp, pwp);
        } else {
            func_8C06E904(twp, mwp, pwp);
        }
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 74:
        func_8C071D40(twp, mwp, pwp);
        func_8C06E904(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 77:
        func_8C071D40(twp, mwp, pwp);
        func_8C0AF280(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        func_8C0B2CB2(twp);
        pwp->free.b[0] |= 1;
        func_8C0B2D8C(twp);
        break;
    case 79:
        func_8C070EDA(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 80:
        func_8C04B9E0(twp);
        break;
    case 81:
        if ((pwp->mj.flag & 1) && pwp->shadow.y_top > CWP(twp)->info->center.y
                && (pwp->shadow.Attr_top & 2)) {
            pwp->spd.y = pwp->shadow.y_top - CWP(twp)->info->center.y;
            if (pwp->spd.y > 0.39999998f) {
                pwp->spd.y = 0.39999998f;
            }
        } else {
            pwp->spd.y = 0.0f;
        }
        func_8C071D40(twp, mwp, pwp);
        func_8C070EDA(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 82:
        if (twp->flag & 3) {
            pwp->mj.reqaction = 87;
        }
        func_8C071D40(twp, mwp, pwp);
        func_8C070EDA(twp, mwp, pwp);
        PGetSpeed(twp, mwp, pwp);
        PSetPosition(twp, mwp, pwp);
        func_8C079096(twp, mwp, pwp);
        break;
    case 83:
        func_8C0AF012(twp, mwp, pwp);
        break;
    }
    PSetMotion(&pwp->mj);
    if (!(twp->flag & 0x100)) {
        if ((Sint16)pwp->equipment & 2) {
            D_8C1C7D70->sibling = &Sonic_Left_Light_Speed_Shoe_Worn;
            D_8C1C8CA4->sibling = &Sonic_Right_Light_Speed_Shoe_Worn;
        } else {
            D_8C1C7D70->sibling = &D_8C1C7938;
            D_8C1C8CA4->sibling = &D_8C1C8888;
        }
        if ((Sint16)pwp->equipment & 1) {
            D_8C1CAD20->sibling = &Sonic_Crystal_Ring_Worn;
        } else {
            D_8C1CAD20->sibling = &D_8C1CACEC;
        }
        PJoinVertexes(twp, mwp, pwp);
    }
    func_8C0AE110(twp, mwp, pwp);
    func_8C0AE17A(tp);
    if (pwp->htp != NULL) {
        func_8C0B1DE0(twp, mwp, pwp);
    }
    pwp->free.b[0] = pwp->free.b[0] << 1;
    func_8C0925A0(twp, &pwp->shadow);
    if (pwp->mj.action != 75 && pwp->mj.action != 139
            && (twp->mode < 55 || twp->mode > 60)) {
        CCL_Entry(twp);
        CWP(twp)->range = pwp->p.height + njScalor(&pwp->spd);
    } else {
        CWP(twp)->hit_num = -1;
        CWP(twp)->hit_cwp = NULL;
        CWP(twp)->flag &= 0xFF61;
    }
    func_8C06C6F8(twp, mwp, pwp);
}
#else
ASM_STUB(void SonicMain(task *tp))
#endif
