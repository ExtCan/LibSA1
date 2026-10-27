#include "sa1/sa1.h"

/*
 * E-102 Gamma beam effects (1ST_READ chunk 8C0D3C00-8C0D4B00, part):
 *
 *  func_8C0D4250  exec of a fading effect: counter.f runs 1.0 -> 0 in steps
 *                 of 0.1, display func_8C0D4160; destroyed at <= 0.
 *  func_8C0D4320  display: additive sprite D_8C422DC8 (texlist_E102BEAM,
 *                 cell 0/1 from wtimer bit 0x200) at twp->pos, scaled by
 *                 counter.f.
 *  func_8C0D439E  exec of that sprite: counter.f 2.0 -> 0 in steps of 0.5.
 *  func_8C0D43DA  exec+display of the blaster ring: counter.f 0 -> 8 in
 *                 steps of 0.5; fades the material (1.5 - c/8), scales the
 *                 model by c/4 and animates its texture id (= c).
 *  func_8C0D454A  classifies where ptwp is relative to twp (yaw quadrant
 *                 and pitch): 0..5, 4 when twp->mode is 31 or 33.
 */

ASM_STUB(void func_8C0D4160(task *tp))

void func_8C056972(void);                   /* save draw flags (code_8C056860.c) */
void func_8C05697C(void);                   /* restore draw flags (code_8C05697C.c) */

extern Uint32 B_8C88F62C;                   /* draw flag word (see sa1/engine.h) */
extern NJS_SPRITE D_8C422DC8;
extern NJS_ARGB D_8C422E3C;
extern NJS_ARGB D_8C422E4C;
#if defined(TARGET_PC)
#define D_8C422E50 (D_8C422E4C.r)
#define D_8C422E54 (D_8C422E4C.g)
#define D_8C422E58 (D_8C422E4C.b)
#else
extern Float D_8C422E50;                    /* = D_8C422E4C.r (separate symbols in the pool) */
extern Float D_8C422E54;                    /* = D_8C422E4C.g */
extern Float D_8C422E58;                    /* = D_8C422E4C.b */
#endif
extern NJS_TEXLIST texlist_E102BEAM;
/*
 * E_102_Gamma_Blaster_Effect (8C431D88) is an NJS_OBJECT (evalflags 0x17,
 * model 8C431D60) whose model->mats is 8C4314D0, the material whose texture
 * id func_8C0D43DA animates; the PC build reaches it through the object so
 * the ripped model and the written material are the same.
 */
extern NJS_OBJECT E_102_Gamma_Blaster_Effect;
#if defined(TARGET_PC)
#define D_8C4314D0 (E_102_Gamma_Blaster_Effect.model->mats)
#else
extern NJS_MATERIAL D_8C4314D0[];
#endif

void func_8C0D4250(task *tp)
{
    taskwk *twp;

    twp = tp->twp;
    switch (twp->mode) {
    case 0:
        twp->counter.f = 1.0f;
        tp->disp = func_8C0D4160;
        twp->mode = 1;
    case 1:
        twp->counter.f -= 0.099999996f;
        if (twp->counter.f <= 0.0f) {
            DestroyTask(tp);
            return;
        }
    }
    func_8C0D4160(tp);
}

void func_8C0D4320(task *tp)
{
    taskwk *twp;
    Sint32 n;

    twp = tp->twp;
    if (B_8C754EC8 != 0) {
        return;
    }
    njColorBlendingMode(0, 10);
    njColorBlendingMode(1, 10);
    njSetConstantMaterial(&D_8C422E3C);
    njSetTexture(&texlist_E102BEAM);
    njPushMatrix(NULL);
    n = (twp->wtimer & 0x200) ? 1 : 0;
    njTranslateV(NULL, &twp->pos);
    D_8C422DC8.sx = D_8C422DC8.sy = twp->counter.f;
    njDrawSprite3D(&D_8C422DC8, n, 48);
    njPopMatrix(1);
}

void func_8C0D439E(task *tp)
{
    taskwk *twp;

    twp = tp->twp;
    switch (twp->mode) {
    case 0:
        twp->counter.f = 2.0f;
        twp->mode = 1;
    case 1:
        twp->counter.f -= 0.5f;
        if (twp->counter.f <= 0.0f) {
            DestroyTask(tp);
            return;
        }
    }
    func_8C0D4320(tp);
}

void func_8C0D43DA(task *tp)
{
    taskwk *twp;
    Float s;
    Float c;

    twp = tp->twp;
    switch (twp->mode) {
    case 0:
        twp->counter.f = 0.0f;
        twp->mode = 1;
    case 1:
        twp->counter.f += 0.5f;
        if (twp->counter.f >= 8.0f) {
            DestroyTask(tp);
            return;
        }
        break;
    }
    func_8C056972();
    func_8C60EDE0((B_8C88F62C & ~0x20) | 0x10);
    njColorBlendingMode(0, 10);
    njColorBlendingMode(1, 10);
    s = twp->counter.f * 0.125f;
    c = 1.5f - s;
    D_8C422E50 = c;
    D_8C422E54 = c;
    D_8C422E58 = c;
    njSetConstantMaterial(&D_8C422E4C);
    njSetTexture(&texlist_E102BEAM);
    njPushMatrix(NULL);
    s *= 2.0f;
    njTranslateV(NULL, &twp->pos);
    twp->ang.y ? njRotateY(NULL, twp->ang.y) : (void)0;
    twp->ang.z ? njRotateZ(NULL, twp->ang.z) : (void)0;
    njScale(NULL, s, s, s);
    /*
     * SHC's float -> Uint32 conversion, written out: values above 2^31 are
     * biased by -2^32 before FTRC (counter.f is 0.5..7.5 here).
     */
    D_8C4314D0[0].attr_texId = (Uint32)SA1_FTOI(twp->counter.f > 2147483648.0f
                                                 ? twp->counter.f + -4294967296.0f
                                                 : twp->counter.f);
    func_8C036F12((NJS_MODEL *)&E_102_Gamma_Blaster_Effect); /* takes an NJS_OBJECT (8C036BC0 walks evalflags) */
    njPopMatrix(1);
    func_8C05697C();
}

Sint32 func_8C0D454A(taskwk *twp, taskwk *ptwp)
{
    Float dx;
    Float dy;
    Float dz;
    Sint32 angy;
    Sint32 angx;
    Sint32 ret;
    Sint32 back;
    Sint32 mode;

    mode = twp->mode;
    if (mode == 31 || mode == 33) {
        return 4;
    }
    dx = ptwp->pos.x - twp->pos.x;
    dy = ptwp->pos.y - twp->pos.y;
    dz = ptwp->pos.z - twp->pos.z;
    angy = (SA1_FTOI(atan2f(dz, dx) * 10430.381f) - twp->ang.y + 0x2000) & 0xFFFF;
    angx = -SA1_FTOI(atan2f(dy, njSqrt(SA1_FMAC(dx, dx, dz * dz))) * 10430.381f);
    switch (angy & 0xC000) {
    case 0x4000:
        ret = 3;
        break;
    case 0x8000:
        ret = 1;
        break;
    case 0xC000:
        ret = 2;
        break;
    default:
        ret = 0;
        break;
    }
    back = (angy >= 0x6000 && angy <= 0xE000);
    if (angx < -0x2000) {
        ret = back ? 4 : 5;
    }
    if (angx > 0x2000) {
        ret = back ? 5 : 4;
    }
    return ret;
}
