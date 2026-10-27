#include "common.h"
#include "ninja.h"
#include "task.h"

/* Ninja ARGB float color (njSetConstantMaterial argument) */
typedef struct {
    Float a;
    Float r;
    Float g;
    Float b;
} NJS_ARGB;

/* Ninja sprite (njDrawSprite2D argument) */
typedef struct {
    NJS_POINT3 p;
    Float sx;
    Float sy;
    Angle ang;
    NJS_TEXLIST *tlist;
    void *tanim;
} NJS_SPRITE;

typedef struct {
    Uint8 data[0x4A0];
} UNK_SAVE;

extern Sint32 D_8C161CA4;
extern UNK_SAVE B_8C7989A0[];
extern NJS_SPRITE D_8C1BF520[];
extern Sint32 B_8C754BFC;

extern Sint32 GetPlayerNumber(void);
extern Uint16 GetStageNumber(void);
extern Sint32 func_8C0A6AB4(UNK_SAVE *save, Sint32 chr, Sint32 level, Sint32 n);
extern void func_8C0A6AD2(UNK_SAVE *save, Sint32 chr, Sint32 level, Sint32 n);
extern void func_8C014A78(void);
extern void func_8C0940DA(Sint32 id, Sint32 a, Sint32 b, Sint32 c);
extern void njSetConstantMaterial(NJS_ARGB *argb);
extern void njColorBlendingMode(Sint32 a, Sint32 b);
extern void njDrawSprite2D(NJS_SPRITE *sp, Sint32 n, Float pri, Uint32 attr);
extern void func_8C107AAA(void);
extern void func_8C0A7500(void);
extern void func_8C107D80(void);

ASM_STUB(void func_8C08AE00(Float a, Float b, Float c, Float d))
ASM_STUB(Sint32 func_8C08B0A0(Sint32 chr, Sint32 level, Sint32 n))
ASM_STUB(Sint32 func_8C08B1BA(Sint32 chr, Sint32 level, Sint32 n))

#ifdef NON_MATCHING
void func_8C08B284(task *tp) {
    taskwk *twp;
    Sint32 chr;
    Sint32 level;
    Sint32 n;
    Sint32 ret;
    Sint32 flag;
    Sint32 cnt;
    NJS_ARGB argb;

    twp = tp->twp;
    switch ((Uint8)twp->btimer) {
    case 0:
        chr = GetPlayerNumber();
        level = GetStageNumber() >> 8;
        flag = 0;
        if (twp->pos.x > 0.0f) {
            flag = 1;
        }
        switch (level) {
        case 0:
        case 35:
        case 36:
        case 37:
        case 38:
            n = 1;
            break;
        default:
            n = 2;
            break;
        }
        switch (n) {
        case 2:
        default:
            if (!func_8C0A6AB4(&B_8C7989A0[D_8C161CA4], chr, level, n)) {
                break;
            }
            n--;
        case 1:
            if (!func_8C0A6AB4(&B_8C7989A0[D_8C161CA4], chr, level, n)) {
                break;
            }
            n--;
        case 0:
            if (func_8C0A6AB4(&B_8C7989A0[D_8C161CA4], chr, level, n)) {
                n--;
            }
            break;
        }
        twp->counter.l = n;
        if (n < 0) {
            twp->btimer = 4;
            n = 0;
        } else {
            twp->btimer = 1;
            twp->wtimer = 0;
            func_8C014A78();
            flag = 0;
        }
        switch (level) {
        case 0:
        case 36:
        case 37:
        case 35:
        case 38:
            ret = func_8C08B1BA(chr, level, n);
            break;
        default:
            ret = func_8C08B0A0(chr, level, n);
            break;
        }
        if (level != 36 && level != 35 && level != 0) {
            if (flag == 1) {
                switch (chr) {
                case 0:
                    func_8C0940DA(0x56B, 0, 0, 0);
                    break;
                case 2:
                    func_8C0940DA(0x546, 0, 0, 0);
                    break;
                case 3:
                    func_8C0940DA(0x539, 0, 0, 0);
                    break;
                case 5:
                    func_8C0940DA(0x502, 0, 0, 0);
                    break;
                case 7:
                    func_8C0940DA(0x50E, 0, 0, 0);
                    break;
                case 6:
                    break;
                }
            } else if (ret == 1) {
                switch (chr) {
                case 0:
                    func_8C0940DA(0x56D, 0, 0, 0);
                    break;
                case 2:
                    func_8C0940DA(0x548, 0, 0, 0);
                    break;
                case 3:
                    func_8C0940DA(0x53B, 0, 0, 0);
                    break;
                case 5:
                    func_8C0940DA(0x501, 0, 0, 0);
                    break;
                case 7:
                    func_8C0940DA(0x510, 0, 0, 0);
                    break;
                case 6:
                    break;
                }
            } else {
                switch (chr) {
                case 0:
                    func_8C0940DA(0x56A, 0, 0, 0);
                    break;
                case 2:
                    func_8C0940DA(0x545, 0, 0, 0);
                    break;
                case 3:
                    func_8C0940DA(0x538, 0, 0, 0);
                    break;
                case 5:
                    func_8C0940DA(0x4FF, 0, 0, 0);
                    break;
                case 6:
                    break;
                case 7:
                    func_8C0940DA(0x50D, 0, 0, 0);
                    break;
                }
            }
        }
        if (ret == 1 && n >= 0) {
            func_8C0A6AD2(&B_8C7989A0[D_8C161CA4], chr, level, n);
        }
        twp->timer.l = ret;
        twp->wtimer = 0;
        break;
    case 1:
        ret = twp->timer.l;
        level = GetStageNumber() >> 8;
        switch (level) {
        case 35:
        case 36:
        case 37:
        case 38:
        case 0:
            twp->wtimer = 150;
            break;
        default:
            cnt = twp->counter.l;
            argb.a = -0.5f;
            argb.r = argb.g = argb.b = 1.0f;
            njSetConstantMaterial(&argb);
            njColorBlendingMode(0, 8);
            njColorBlendingMode(1, 6);
            njDrawSprite2D(&D_8C1BF520[0], 0, -1.15f, 33);
            njDrawSprite2D(&D_8C1BF520[1], cnt + 1, -1.15f, 33);
            if (B_8C754BFC == 0) {
                njDrawSprite2D(&D_8C1BF520[2], ret + 4, -1.15f, 33);
            } else {
                njDrawSprite2D(&D_8C1BF520[3], ret + 4, -1.15f, 33);
            }
            argb.r = argb.g = argb.b = argb.a = 0.0f;
            njSetConstantMaterial(&argb);
            func_8C08AE00(320.0f, 240.0f, 10.0f, 7.0f);
            break;
        }
        if (++twp->wtimer > 150.0f) {
            switch (ret) {
            case 0:
            default:
                twp->btimer = 4;
                break;
            case 1:
                twp->btimer = 2;
                twp->wtimer = 0;
                func_8C107AAA();
                func_8C0A7500();
                func_8C107D80();
                break;
            }
        }
        break;
    case 2:
        if (++twp->wtimer > 30.0f) {
            twp->btimer = 3;
            twp->wtimer = 0;
        }
        break;
    case 3:
        if (++twp->wtimer > 60) {
            twp->btimer = 4;
            twp->wtimer = 0;
        }
        break;
    case 4:
        twp->mode = 19;
        twp->counter.l = 2;
        break;
    default:
        twp->mode = 20;
        break;
    }
}
#else
ASM_STUB(void func_8C08B284(task *tp))
#endif
