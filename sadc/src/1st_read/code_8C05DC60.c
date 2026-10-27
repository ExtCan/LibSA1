/*
 * 1ST_READ chunk 8C05D9C0 (air item box O_ItemBoxAir): the box's display
 * and the two node draw functions of its model.
 *
 *  func_8C05DC60  disp (tp->disp, also called by objexec_O_ItemBoxAir):
 *                 visibility test (func_8C038D00, radius 20); B_8C78C380 =
 *                 the box work; translate / rotate Z, X, Y to the box; in
 *                 modes 0 / 1 the model Item_Box_Air at scale work.scl2 with
 *                 the node list D_8C1B9C3C, else (broken: modes 2-4) at
 *                 scale work.scl, translucent (blend 8 / 6, ctl3d bit
 *                 0x100000, constant material alpha (1 - scl) / 4) with the
 *                 node list D_8C1B9C4C
 *  func_8C05DDBE  node draw (D_8C1B9C4C) of the box's second node
 *                 (object D_8C1B7FC0): constant material colour
 *                 (1 - scl) / 4 with ctl3d 0x20, drawn with func_8C6215D6
 *  func_8C05DEA0  node draw (both lists) of the item panel node (object
 *                 D_8C1B80C4): sets the 1-up item's icon (D_8C1B9BA0[6].tex;
 *                 the split's pool symbol is D_8C1B9BD0, shcdiff resolves the
 *                 two to the same address) to the current character's
 *                 (D_8C1B9BE8[GetPlayerNumber()]) and the node model's first
 *                 material texture to the box item's icon
 *                 (D_8C1B9BA0[work.item].tex), then draws the node turned by
 *                 the work's spin angle (work.ang) with func_8C03700C
 *
 * The two node functions are reached only through the data tables
 * D_8C1B9C3C / D_8C1B9C4C (func_8C057460 calls list[i].func(obj)).
 *
 * PORTABILITY: func_8C05DEA0 writes game data (D_8C1B9BA0[6].tex and the
 * texture id of the panel model's material): per instance on PC
 * (docs/portability.md 4). func_8C05DDBE never sets the constant
 * material's alpha (mat.a, stack garbage on the DC): PC_ONLY(mat.a = 0) (see
 * the function).
 *
 * Matching notes (all three MATCH): func_8C05DDBE's colour is one chained
 * assignment (a local float copies it into FR4 first); func_8C05DEA0 writes
 * the 1-up icon as D_8C1B9BA0[6].tex (the store address is loaded into R8
 * before the call, as in the original; the pool word is the split's
 * D_8C1B9BD0, the same address), so no alias symbol is needed on PC.
 */
#include "common.h"
#include "sa1/sa1.h"
#include "sa1/pending.h"
#include "sa1/protos.h"     /* func_8C099500 */

/*
 * Air item box work block (same layout as in code_8C05D9C0.c and
 * code_8C05DF18.c): MAlloc(32) and zeroed by func_8C05DD90, kept in
 * AWP(tp)->work[0].ptr and mirrored in B_8C78C380.
 */
typedef struct {
    Sint32 unk00;               /* 0x00 */
    Sint32 item;                /* 0x04 item kind 0..8 (index of D_8C1B9BA0) */
    Sint32 unk08[3];            /* 0x08 */
    Sint32 ang;                 /* 0x14 spin angle, += 0x222 per frame in mode 1 */
    Float scl;                  /* 0x18 box scale: 1.0 at init, += 0.2 per frame in mode 2 */
    Float scl2;                 /* 0x1C += 0.05 up to 1.0 (func_8C05DF18); drawn scale in modes 0/1 */
} ITEMBOXAIR_WK;                /* 0x20 */

SA1_ASSERT_SIZE(ITEMBOXAIR_WK, 0x20);
SA1_ASSERT_OFFSET(ITEMBOXAIR_WK, item, 0x04);
SA1_ASSERT_OFFSET(ITEMBOXAIR_WK, ang, 0x14);
SA1_ASSERT_OFFSET(ITEMBOXAIR_WK, scl, 0x18);
SA1_ASSERT_OFFSET(ITEMBOXAIR_WK, scl2, 0x1C);

/* Item kind table (same as code_8C05DF18.c): icon texture number and the
 * pickup handler (code pointer: remapped by the PC loader) */
typedef struct {
    Sint32 tex;                 /* 0x00 */
    void (*func)(taskwk *twp);  /* 0x04 */
} ITEMBOX_ITEM;                 /* 0x08 */

SA1_ASSERT_SIZE(ITEMBOX_ITEM, 0x08);

extern ITEMBOXAIR_WK *B_8C78C380;       /* current air item box work */
extern ITEMBOX_ITEM D_8C1B9BA0[];       /* item kinds 0..8 */
extern Sint32 D_8C1B9BE8[];             /* 1-up icon per character (GetPlayerNumber 0..7) */
extern NJS_OBJECT Item_Box_Air;         /* 8C1B87BC */
extern OBJECT_DRAW_FUNC D_8C1B9C3C[];   /* {D_8C1B80C4, func_8C05DEA0}, end */
extern OBJECT_DRAW_FUNC D_8C1B9C4C[];   /* {D_8C1B80C4, func_8C05DEA0}, {D_8C1B7FC0, func_8C05DDBE}, end */

void func_8C056972(void);               /* save ctl3d B_8C88F62C (code_8C056860.c) */
void func_8C05697C(void);               /* restore it (code_8C05697C.c) */

void func_8C05DC60(task *tp)
{
    taskwk *twp;

    twp = tp->twp;
    if (func_8C038D00(twp->pos.x, twp->pos.y, twp->pos.z, 20.0f)) {
        B_8C78C380 = AWP(tp)->work[0].ptr;
        njPushMatrix(NULL);
        njTranslateV(NULL, &twp->pos);
        twp->ang.z ? njRotateZ(NULL, twp->ang.z) : (void)0;
        twp->ang.x ? njRotateX(NULL, twp->ang.x) : (void)0;
        twp->ang.y ? njRotateY(NULL, twp->ang.y) : (void)0;
        func_8C099500();
        if (twp->mode == 1 || twp->mode == 0) {
            njScale(NULL, B_8C78C380->scl2, B_8C78C380->scl2, B_8C78C380->scl2);
            func_8C057460(&Item_Box_Air, D_8C1B9C3C);
        } else {
            njScale(NULL, B_8C78C380->scl, B_8C78C380->scl, B_8C78C380->scl);
            func_8C056926();
            func_8C05694A(0, 0x100000);
            njColorBlendingMode(0, 8);
            njColorBlendingMode(1, 6);
            func_8C043A20((1.0f - B_8C78C380->scl) * 0.25f, 0.0f, 0.0f, 0.0f);
            func_8C057460(&Item_Box_Air, D_8C1B9C4C);
            func_8C043A20(0.0f, 0.0f, 0.0f, 0.0f);
            func_8C056938();
        }
        njPopMatrix(1);
    }
}

void func_8C05DDBE(NJS_OBJECT *obj)
{
    NJS_ARGB mat;

    func_8C056972();
    func_8C60EDE0(32);
    /* mat.a is never written: the DC passes whatever the stack slot holds
     * (UNVERIFIED value, plausibly ~0); PC uses 0 */
    PC_ONLY(mat.a = 0.0f;)
    mat.r = mat.g = mat.b = (1.0f - B_8C78C380->scl) * 0.25f;
    njSetConstantMaterial(&mat);
    func_8C6215D6(obj);
    func_8C05697C();
}

void func_8C05DEA0(NJS_OBJECT *obj)
{
    D_8C1B9BA0[6].tex = D_8C1B9BE8[GetPlayerNumber()];
    obj->model->mats->attr_texId = D_8C1B9BA0[B_8C78C380->item].tex;
    njTranslateV(NULL, (NJS_VECTOR *)obj->pos);
    obj->ang[2] ? njRotateZ(NULL, obj->ang[2]) : (void)0;
    obj->ang[0] ? njRotateX(NULL, obj->ang[0]) : (void)0;
    B_8C78C380->ang ? njRotateY(NULL, B_8C78C380->ang) : (void)0;
    func_8C03700C(obj->model);
}
