#include "sa1/sa1.h"

/*
 * 1ST_READ chunk 8C091C20 (part): blob shadows. Each one draws the
 * Character_Shadow_Blob object on the ground below an entity, tilted by the
 * ground slope and shrunk with the height above the ground
 * (1 / (|dy| * 0.03 + 1)); nothing is drawn while B_8C754EC8 (logic step
 * index) is not 0.
 */

extern NJS_OBJECT Character_Shadow_Blob[]; /* 8C1ACA78 blob shadow object (model at +4); declared as an
                                            * array: 8C092124 loads its address and then @(4,Rn) */
extern Sint32 func_8C099500(void);         /* shadow draw setup, called before every blob; its
                                            * result (R0) is ignored (canonical: sa1/protos.h) */

/*
 * Translation unit: the original object is the whole chunk 8C091C20-8C092BE0;
 * code_8C091C20.c holds its first part (8C091C20-8C092124). func_8C092124
 * matches only when compiled after it (see below); check with
 *   py -3.12 tools/shcdiff.py func_8C092124 src/1st_read/code_8C092124.c
 *       --flags=-define=TU_EXPERIMENT,NON_MATCHING
 */
#ifdef TU_EXPERIMENT
#include "1st_read/code_8C091C20.c"
#else
ASM_STUB(Float func_8C091C20(Float x, Float y, Float z, Angle3 *ang))
#endif

/*
 * Blob shadow of twp at ground height y (tilted by twp->ang.z/x).
 *
 * NON_MATCHING only because of the translation-unit context; the C below is
 * complete and matches (shcdiff MATCH, 83/83 insns) when compiled after
 * code_8C091C20.c (TU_EXPERIMENT above). In the original object the 9 pool
 * entries left pending by func_8C092000 (B_8C754EF4, 20.0f, func_8C0522C0,
 * njScalor, 1.5f, 3000.0f, 4000.0f, func_8C02D060, B_8C754EF0) make SHC dump
 * the shared pool right after the tail JMP to njPopMatrix (8C0921C0), between
 * the body and the early-return epilogue; that pushes the epilogue out of BT
 * range and gives "BT/S body; BRA end; NOP" instead of "BF/S end". Compiled
 * alone, the stubs leave only one pending entry (__asm_stub_sink) and the
 * pool lands after the RTS; 0 to 24 extra stubs in front do not move it to
 * the JMP (SHC then dumps it between the stubs).
 */
#ifdef NON_MATCHING
void func_8C092124(taskwk *twp, Float y)
{
    Float s;

    if (B_8C754EC8) {
        return;
    }
    s = 1.0f / SA1_FMAC(0.03f, _builtin_fabsf(twp->pos.y - y), 1.0f);
    func_8C099500();
    njPushMatrix(NULL);
    njTranslate(NULL, twp->pos.x, y + 0.19999999f, twp->pos.z);
    twp->ang.z ? njRotateZ(NULL, twp->ang.z) : (void)0;
    twp->ang.x ? njRotateX(NULL, twp->ang.x) : (void)0;
    njScale(NULL, s, s, s * 0.79999997f);
    njDrawModel(Character_Shadow_Blob->model);
    njPopMatrix(1);
}
#else
ASM_STUB(void func_8C092124(taskwk *twp, Float y))
#endif

/* probes the ground below twp (4 above its position), draws a blob shadow of
 * size r there and returns the ground height */
Float func_8C09222C(taskwk *twp, Float r)
{
    Angle3 ang;
    Float y;

    y = func_8C091C20(twp->pos.x, twp->pos.y + 4.0f, twp->pos.z, &ang);
    if (!B_8C754EC8) {
        r = r / SA1_FMAC(0.03f, _builtin_fabsf(twp->pos.y - y), 1.0f);
        func_8C099500();
        njPushMatrix(NULL);
        njTranslate(NULL, twp->pos.x, y + 0.29999999f, twp->pos.z);
        ang.z ? njRotateZ(NULL, ang.z) : (void)0;
        ang.x ? njRotateX(NULL, ang.x) : (void)0;
        -twp->ang.y ? njRotateY(NULL, -twp->ang.y) : (void)0;
        njScale(NULL, r, r, r * 0.79999997f);
        njDrawObject(Character_Shadow_Blob);
        njPopMatrix(1);
    }
    return y;
}

/* blob shadow of size r at pos, tilted by ang->z/x */
void func_8C0922EE(Angle3 *ang, NJS_POINT3 *pos, Float r)
{
    if (B_8C754EC8) {
        return;
    }
    func_8C099500();
    njPushMatrix(NULL);
    njTranslateV(NULL, pos);
    ang->z ? njRotateZ(NULL, ang->z) : (void)0;
    ang->x ? njRotateX(NULL, ang->x) : (void)0;
    njScale(NULL, r, r, r);
    njScale(NULL, r, r, r * 0.79999997f);
    njDrawObject(Character_Shadow_Blob);
    njPopMatrix(1);
}

/* like func_8C09222C, probing from twp->pos itself and returning the slope
 * in *ang; rotated by +twp->ang.y. r is declared before ang: with
 * (twp, ang, r) SHC moves R5 to R13 before FR4 to FR15 (8C0923DA/DC). */
Float func_8C0923C0(taskwk *twp, Float r, Angle3 *ang)
{
    Float y;

    y = func_8C091C20(twp->pos.x, twp->pos.y, twp->pos.z, ang);
    if (!B_8C754EC8) {
        r = r / SA1_FMAC(0.03f, _builtin_fabsf(twp->pos.y - y), 1.0f);
        func_8C099500();
        njPushMatrix(NULL);
        njTranslate(NULL, twp->pos.x, y + 0.29999999f, twp->pos.z);
        ang->z ? njRotateZ(NULL, ang->z) : (void)0;
        ang->x ? njRotateX(NULL, ang->x) : (void)0;
        twp->ang.y ? njRotateY(NULL, twp->ang.y) : (void)0;
        njScale(NULL, r, r, r * 0.79999997f);
        njDrawObject(Character_Shadow_Blob);
        njPopMatrix(1);
    }
    return y;
}

/* player blob shadow from the shadow work (ground shadow->y_bottom, tilt
 * angx/angz, size scl); skipped when the ground is 20000 or more below */
void func_8C09247E(taskwk *twp, shadowwk *shadow)
{
    Float s;

    if (B_8C754EC8) {
        return;
    }
    if (!(shadow->y_bottom > twp->pos.y - 20000.0f)) {
        return;
    }
    s = shadow->scl / SA1_FMAC(0.03f, _builtin_fabsf(twp->pos.y - shadow->y_bottom + 1.0f), 1.0f);
    func_8C099500();
    njPushMatrix(NULL);
    njTranslate(NULL, twp->pos.x, shadow->y_bottom + 0.29999999f, twp->pos.z);
    shadow->angz ? njRotateZ(NULL, shadow->angz) : (void)0;
    shadow->angx ? njRotateX(NULL, shadow->angx) : (void)0;
    -twp->ang.y ? njRotateY(NULL, -twp->ang.y) : (void)0;
    njScale(NULL, s, s, s * 0.79999997f);
    njDrawObject(Character_Shadow_Blob);
    njPopMatrix(1);
}
