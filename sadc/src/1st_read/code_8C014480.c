#include "common.h"
#include "ninja.h"
#include "task.h"

/* Mission card ("mission_s_box") shown at the start of a stage. */

typedef struct {
    Uint8 data[0x4A0];
} UNK_SAVE;

typedef struct {
    NJS_POINT3 p;
    Float sx;
    Float sy;
    Sint32 ang;
    NJS_TEXLIST *tlist;
    void *tanim;
} UNK_SPRITE;

typedef struct {
    Float a;
    Float r;
    Float g;
    Float b;
} UNK_ARGB;

extern Sint32 D_8C161CA4;
extern UNK_SAVE B_8C7989A0[];
extern Sint8 B_8C752BDD;
extern Sint32 B_8C754EC8;
extern NJS_TEXLIST B_8C6B6878;
extern Uint8 D_8C1621B8[];
extern Sint32 B_8C754BFC;
extern NJS_TEXNAME D_8C161CF0[];
extern NJS_TEXNAME D_8C161F54[];
extern char D_8C161CE8[];
extern Sint32 tcrf_LevelSelectVar;
extern Uint8 B_8C78B451[];

extern Sint32 func_8C0A6AB4(UNK_SAVE *save, Sint32 chr, Sint32 level, Sint32 n);
extern void FreeTask(task *tp); /* FreeTask */
extern void njColorBlendingMode(Sint32 a, Sint32 b);
extern Sint32 njSetTexture(NJS_TEXLIST *texlist);
extern void njSetConstantMaterial(UNK_ARGB *argb);
extern void njDrawSprite2D(UNK_SPRITE *sp, Sint32 n, Float pri, Uint32 attr);
extern Sint32 func_8C6088BC(NJS_TEXLIST *texlist); /* texture release */
extern Uint16 GetStageNumber(void);
extern Sint32 GetPlayerNumber(void);
extern Sint32 func_8C608C82(NJS_TEXLIST *texlist); /* texture load */
extern task *CreateElementalTask(Uint16 im, Sint32 level, TaskFuncPtr exec); /* CreateElementalTask */

Sint32 func_8C014480(Sint32 chr, Sint32 level) {
    Sint32 ret;

    if (func_8C0A6AB4(&B_8C7989A0[D_8C161CA4], chr, level, 0)) {
        return -1;
    }
    switch (chr) {
    default:
    case 0:
        if (func_8C0A6AB4(&B_8C7989A0[D_8C161CA4], chr, level, 1)) {
            switch (level) {
            case 1:
                ret = 43;
                break;
            case 9:
            case 6:
                ret = 47;
                break;
            case 8:
                ret = 45;
                break;
            case 4:
                ret = 44;
                break;
            case 7:
            case 10:
                ret = 46;
                break;
            default:
                ret = 2;
                break;
            }
        } else if (func_8C0A6AB4(&B_8C7989A0[D_8C161CA4], chr, level, 2)) {
            ret = 1;
        } else {
            switch (level) {
            case 1:
                ret = 21;
                break;
            case 2:
                ret = 18;
                break;
            case 5:
                ret = 22;
                break;
            case 7:
                ret = 23;
                break;
            case 8:
                ret = 20;
                break;
            case 9:
                ret = 19;
                break;
            case 10:
                ret = 24;
                break;
            default:
                ret = 0;
                break;
            }
        }
        break;
    case 2:
        if (func_8C0A6AB4(&B_8C7989A0[D_8C161CA4], chr, level, 1)) {
            if (level == 4) {
                ret = 30;
            } else {
                ret = 5;
            }
        } else if (func_8C0A6AB4(&B_8C7989A0[D_8C161CA4], chr, level, 2)) {
            if (level == 4) {
                ret = 29;
            } else {
                ret = 4;
            }
        } else {
            switch (level) {
            case 2:
                ret = 25;
                break;
            case 9:
                ret = 26;
                break;
            case 8:
                ret = 27;
                break;
            case 4:
                ret = 28;
                break;
            default:
                ret = 3;
                break;
            }
        }
        break;
    case 3:
        if (func_8C0A6AB4(&B_8C7989A0[D_8C161CA4], chr, level, 1)) {
            if (level == 6) {
                ret = 48;
            } else {
                ret = 8;
            }
        } else if (func_8C0A6AB4(&B_8C7989A0[D_8C161CA4], chr, level, 2)) {
            ret = 7;
        } else {
            ret = 6;
        }
        break;
    case 5:
        if (func_8C0A6AB4(&B_8C7989A0[D_8C161CA4], chr, level, 1)) {
            switch (level) {
            case 12:
                ret = 49;
                break;
            case 10:
                ret = 50;
                break;
            default:
                ret = 11;
                break;
            }
        } else if (func_8C0A6AB4(&B_8C7989A0[D_8C161CA4], chr, level, 2)) {
            ret = 10;
        } else {
            ret = 9;
        }
        break;
    case 6:
        if (func_8C0A6AB4(&B_8C7989A0[D_8C161CA4], chr, level, 1)) {
            switch (level) {
            case 10:
                ret = 33;
                break;
            case 1:
                ret = 36;
                break;
            case 5:
                ret = 39;
                break;
            case 12:
                ret = 42;
                break;
            default:
                ret = 14;
                break;
            }
        } else if (func_8C0A6AB4(&B_8C7989A0[D_8C161CA4], chr, level, 2)) {
            switch (level) {
            case 10:
                ret = 32;
                break;
            case 1:
                ret = 35;
                break;
            case 5:
                ret = 38;
                break;
            case 12:
                ret = 41;
                break;
            default:
                ret = 13;
                break;
            }
        } else {
            switch (level) {
            case 10:
                ret = 31;
                break;
            case 1:
                ret = 34;
                break;
            case 5:
                ret = 37;
                break;
            case 12:
                ret = 40;
                break;
            default:
                ret = 12;
                break;
            }
        }
        break;
    case 7:
        if (func_8C0A6AB4(&B_8C7989A0[D_8C161CA4], chr, level, 1)) {
            ret = 17;
        } else if (func_8C0A6AB4(&B_8C7989A0[D_8C161CA4], chr, level, 2)) {
            ret = 16;
        } else {
            ret = 15;
        }
        break;
    }
    return ret;
}

void func_8C0147D0(task *tp) {
    taskwk *twp;
    UNK_SPRITE sp;
    UNK_ARGB argb;

    twp = tp->twp;
    if (B_8C752BDD < 0) {
        return;
    }
    switch (twp->mode) {
    case 0:
        twp->scl.x = 0.0f;
        twp->scl.y = 0.0f;
        twp->scl.z = 1.0f;
        twp->mode = 1;
        break;
    case 1:
        twp->scl.x += 0.05f;
        if (twp->scl.x >= 1.0f) {
            twp->scl.x = 1.0f;
            twp->mode = 2;
        }
        break;
    case 2:
        twp->scl.y += 0.05f;
        if (twp->scl.y >= 1.0f) {
            twp->scl.y = 1.0f;
            twp->mode = 3;
        }
        break;
    case 3:
        tp->twp->wtimer--;
        if (tp->twp->wtimer <= 0) {
            tp->twp->mode = 4;
        }
        break;
    case 4:
        twp->scl.z -= 0.025f;
        if (twp->scl.z <= 0.0f) {
            FreeTask(tp);
            return;
        }
        break;
    default:
        break;
    }
    if (B_8C754EC8 != 0) {
        return;
    }
    sp.tlist = &B_8C6B6878;
    sp.tanim = D_8C1621B8;
    sp.sx = twp->scl.x;
    sp.sy = twp->scl.y;
    sp.p.x = twp->pos.x;
    sp.p.y = twp->pos.y;
    njColorBlendingMode(0, 8);
    njColorBlendingMode(1, 6);
    njSetTexture(&B_8C6B6878);
    argb.a = twp->scl.z;
    argb.r = 1.0f;
    argb.g = 1.0f;
    argb.b = 1.0f;
    njSetConstantMaterial(&argb);
    njDrawSprite2D(&sp, 0, 0.0f, 34);
    argb.a = 0.0f;
    argb.r = 0.0f;
    argb.g = 0.0f;
    argb.b = 0.0f;
    njSetConstantMaterial(&argb);
}

void func_8C014936(task *tp) {
    func_8C6088BC(&B_8C6B6878);
}

void func_8C01493E(void) {
    Sint32 level;
    Sint32 chr;
    Sint32 n;
    char *s;
    task *tp;

    level = GetStageNumber() >> 8;
    chr = GetPlayerNumber();
    if (level <= 0 || level > 14) {
        return;
    }
    if (level == 8 && tcrf_LevelSelectVar == 9 && B_8C78B451[0] == 1) {
        return;
    }
    if (level == 5 && chr == 2) {
        return;
    }
    if (level == 1 && chr == 2) {
        return;
    }
    n = func_8C014480(chr, level);
    if (n < 0) {
        return;
    }
    if (B_8C754BFC == 0) {
        B_8C6B6878.textures = &D_8C161CF0[n];
    } else {
        B_8C6B6878.textures = &D_8C161F54[n];
        s = B_8C6B6878.textures->texname;
        while (*s) {
            s++;
        }
        *--s = D_8C161CE8[B_8C754BFC];
    }
    B_8C6B6878.nbTexture = 1;
    func_8C608C82(&B_8C6B6878);
    tp = CreateElementalTask(2, 1, func_8C0147D0);
    tp->twp->mode = 0;
    tp->twp->counter.b[1] = chr;
    tp->twp->wtimer = 120;
    tp->twp->pos.x = 320.0f;
    tp->twp->pos.y = 360.0f;
    tp->dest = func_8C014936;
}
