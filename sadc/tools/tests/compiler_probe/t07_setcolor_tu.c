/* @tu 8C02E820 */
/*
 * Compiler probe t07: start of the TU at 0x8C02E820 (preceded by a linker zero gap
 * 0x8C02E810-0x8C02E81F, i.e. a real object boundary).
 *   8C02E820-8C02E886 SetColor_8C02E820  switch on (u8 & 3) with 4 float-constant calls
 *                                         merged into one JSR (cross-jumping); 1.0/0.0/
 *                                         0.5/0.03 hoisted into FR12-FR15
 *   8C02E886-8C02E8DA Disp1              NEAR MISS: original copies wk to R3 for the
 *                                         first load (MOV R4,R3) -- source not found yet
 *   8C02E8DA-8C02E936 Disp2              njPushMatrix/njTranslate/njScale/draw/njPopMatrix
 *   8C02E936-8C02E93F .ALIGN 32 padding (00 09 x5), 8C02E940-8C02E96F pool (12 entries)
 * Function mode: SetColor = SHC >= 5.1r01, Disp2 = SHC >= 5.1r01, Disp1 = 1-instruction diff.
 * Byte mode (--exact): with 5.1r03 every byte up to 0x8C02E970 except Disp1 matches
 *   (pool order + 00 09 padding identical; Disp1 is 2 bytes short so the rest is shifted);
 *   5.1r04+ additionally differ in the five 09 00 padding halfwords.
 * Flags: -cpu=sh4 -division=cpu -fpu=single -endian=little -optimize=1 -speed
 *        -macsave=0 -extra=a=1800 -round=zero -code=machinecode   (SHC 5.1r03)
 */
typedef struct { float x, y, z; } NJS_POINT3;
typedef struct {
    char pad0[4];
    unsigned char kind;   /* +4  */
    char pad5[15];
    float sx;             /* +20 */
    float sy;             /* +24 */
} WK;
typedef struct { int dummy; } NJS_MODEL;

extern void SetMaterial_8C043A20(float, float, float, float);
extern void njPushMatrix_8C639C70(void *);
extern void njTranslate_8C63A878(void *, float, float, float);
extern void njScale_8C63A69C(void *, float, float, float);
extern void njPopMatrix_8C639B98(int);
extern void DrawModel_8C6215D6(NJS_MODEL *);
extern NJS_MODEL model_8C199518;
extern NJS_MODEL model_8C199918;

/* @match SetColor_8C02E820 8C02E820-8C02E886 */
void SetColor_8C02E820(WK *wk)
{
    switch (wk->kind & 3) {
    case 0:
        SetMaterial_8C043A20(0.03f, 0.0f, 1.0f, 0.0f);
        break;
    case 1:
        SetMaterial_8C043A20(0.03f, 0.5f, 0.5f, 0.0f);
        break;
    case 2:
        SetMaterial_8C043A20(0.03f, 0.5f, 0.25f, 0.0f);
        break;
    case 3:
        SetMaterial_8C043A20(0.03f, 1.0f, 0.0f, 0.0f);
        break;
    }
}

/* @match Disp1 8C02E886-8C02E8DA */
void Disp1(WK *wk, NJS_POINT3 *pos)
{
    float s;
    WK *w = wk;

    s = wk->sx * 0.1f;
    njPushMatrix_8C639C70(0);
    njTranslate_8C63A878(0, pos->x, pos->y, pos->z);
    njScale_8C63A69C(0, s, s, s);
    SetColor_8C02E820(w);
    DrawModel_8C6215D6(&model_8C199518);
    njPopMatrix_8C639B98(1);
}

/* @match Disp2 8C02E8DA-8C02E936 */
void Disp2(WK *wk, NJS_POINT3 *pos)
{
    float sx, sy;

    sx = wk->sx * 0.1f;
    sy = wk->sy * 0.1f;
    njPushMatrix_8C639C70(0);
    njTranslate_8C63A878(0, pos->x, pos->y, pos->z);
    njScale_8C63A69C(0, sx, sy, sx);
    SetColor_8C02E820(wk);
    DrawModel_8C6215D6(&model_8C199918);
    njPopMatrix_8C639B98(1);
}
