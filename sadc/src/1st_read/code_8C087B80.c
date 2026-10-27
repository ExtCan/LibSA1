#include "common.h"
#include "ninja.h"
#include "task.h"
#include "sa1/ninja_funcs.h"

extern Angle3 D_8C159434;
extern Angle3 D_8C159478;
extern NJS_POINT3 D_8C159484;
extern NJS_POINT3 D_8C159490;
extern char D_8C159440[];
extern char D_8C159450[];
extern char D_8C15945C[];
extern NJS_MATRIX D_8C67C640;
extern NJS_MODEL Debug_Cube_2;

extern Sint32 func_8C0919CC(task *tp);
extern Sint32 func_8C042DC4(task *tp);
extern void func_8C091C00(task *tp, Sint8 flags);
extern Sint8 func_8C091C10(task *tp);
extern task *CreateChildTask(Uint16 flags, TaskFuncPtr exec, task *parent);
extern void objexec_RING(task *tp);
extern void func_8C098E98(task *tp);
extern void FreeTask(task *tp);
extern void func_8C098E60(task *tp);
extern void DestroyTask(task *tp);
extern Float func_8C10D148(Float x);
extern void func_8C036F12(NJS_MODEL *model);

void func_8C087DCA(NJS_POINT3 *pos, Angle3 *ang);
void func_8C087E80(task *tp);
Uint8 func_8C087E86(task *tp);
Uint8 func_8C087EAC(task *tp);
void func_8C087EEE(NJS_POINT3 *pos, taskwk *twp, Sint32 i);
void func_8C088000(NJS_POINT3 *pos, taskwk *twp, Sint32 i);

#ifdef NON_MATCHING
void objexec_O_GRING(task *tp)
{
    taskwk *twp;
    NJS_POINT3 unk60;
    NJS_POINT3 pos;
    Angle3 ang;
    NJS_POINT3 unk24;
    Sint32 i;
    Sint32 bit;
    task *ctp;
    Sint8 flags;

    twp = tp->twp;
    ang = D_8C159434;
    if (func_8C0919CC(tp)) {
        return;
    }
    switch (twp->mode) {
    case 0:
        tp->dest = func_8C087E80;
        for (i = 0; i < (Sint32)twp->scl.x + 1; i++) {
            switch ((Sint32)twp->scl.z) {
            case 0:
                func_8C087EEE(&pos, twp, i);
                break;
            case 1:
                if (i == 0) {
                    continue;
                }
                func_8C088000(&pos, twp, i);
                break;
            }
            if (func_8C042DC4(tp)) {
                func_8C087DCA(&pos, &ang);
            }
            if (!func_8C042DC4(tp)) {
                flags = func_8C091C10(tp);
                bit = 1 << i;
                if (!(flags & bit)) {
                    if ((ctp = CreateChildTask(2, objexec_RING, tp)) != NULL) {
                        ctp->twp->timer.b[0] = bit;
                        ctp->twp->pos.x = pos.x;
                        ctp->twp->pos.y = pos.y;
                        ctp->twp->pos.z = pos.z;
                        ctp->twp->ang.x = ang.x;
                        ctp->twp->ang.y = ang.y;
                        ctp->twp->ang.z = ang.z;
                    }
                }
            }
        }
        if (!func_8C042DC4(tp)) {
            twp->mode = 1;
        }
        if (func_8C042DC4(tp)) {
            njPrintC(0x00110013, D_8C159440);
            njPrintC(0x00110014, D_8C159450);
            njPrintC(0x00110015, D_8C15945C);
        }
        break;
    case 1:
        if (!func_8C042DC4(tp)) {
            func_8C098E98(tp);
            if (func_8C087EAC(tp)) {
            }
            if (!func_8C087E86(tp)) {
                twp->mode = 3;
            }
        }
        if (func_8C042DC4(tp)) {
            Angle3 ang2;
            NJS_POINT3 pos2;
            Sint32 j;

            ang2 = D_8C159478;
            for (j = 0; j < (Sint32)twp->scl.x + 1; j++) {
                switch ((Sint32)twp->scl.z) {
                case 0:
                    func_8C087EEE(&pos2, twp, j);
                    break;
                case 1:
                    if (j == 0) {
                        continue;
                    }
                    func_8C088000(&pos2, twp, j);
                    break;
                }
                func_8C087DCA(&pos2, &ang2);
            }
        }
        break;
    case 3:
        twp->mode = 4;
        break;
    case 4:
        FreeTask(tp);
        break;
    }
}
#else
ASM_STUB(void objexec_O_GRING(task *tp))
#endif

#ifdef NON_MATCHING
void func_8C087DCA(NJS_POINT3 *pos, Angle3 *ang)
{
    Angle a[1];

    njPushMatrix(NULL);
    njTranslate(NULL, pos->x, pos->y, pos->z);
    if ((a[0] = ang->z) != 0) {
        njRotateZ(NULL, a[0]);
    }
    if ((a[0] = ang->x) != 0) {
        njRotateX(NULL, a[0]);
    }
    if ((a[0] = ang->y) != 0) {
        njRotateY(NULL, a[0]);
    }
    func_8C036F12(&Debug_Cube_2);
    njPopMatrix(1);
}
#else
ASM_STUB(void func_8C087DCA(NJS_POINT3 *pos, Angle3 *ang))
#endif

void func_8C087E80(task *tp)
{
    func_8C098E60(tp);
}

Uint8 func_8C087E86(task *tp)
{
    task *ctp;

    for (ctp = tp->ctp; ctp; ctp = ctp->next) {
        if (ctp->exec == DestroyTask && ctp->twp->timer.b[1] == 1) {
            return 0;
        }
    }
    return 1;
}

#ifdef NON_MATCHING
Uint8 func_8C087EAC(task *tp)
{
    task *ctp;

    for (ctp = tp->ctp; ctp; ctp = ctp->next) {
        if (ctp->twp->mode == 3) {
            func_8C091C00(tp, func_8C091C10(tp) | ctp->twp->timer.b[0]);
        }
    }
    return 1;
}
#else
ASM_STUB(Uint8 func_8C087EAC(task *tp))
#endif

#ifdef NON_MATCHING
void func_8C087EEE(NJS_POINT3 *pos, taskwk *twp, Sint32 i)
{
    NJS_POINT3 v;
    Float f;

    v = D_8C159484;
    f = (Float)i;
    if (i % 2) {
        v.z = twp->scl.y * func_8C10D148(f * 0.5f);
    } else {
        v.z = -(twp->scl.y * f * 0.5f);
    }
    njPushMatrix(D_8C67C640);
    njTranslateV(NULL, &twp->pos);
    if (twp->ang.z) {
        njRotateZ(NULL, twp->ang.z);
    }
    if (twp->ang.x) {
        njRotateX(NULL, twp->ang.x);
    }
    if (twp->ang.y) {
        njRotateY(NULL, twp->ang.y);
    }
    njCalcVector(NULL, &v, pos);
    njAddVector(pos, &twp->pos);
    njPopMatrix(1);
}
#else
ASM_STUB(void func_8C087EEE(NJS_POINT3 *pos, taskwk *twp, Sint32 i))
#endif

#ifdef NON_MATCHING
void func_8C088000(NJS_POINT3 *pos, taskwk *twp, Sint32 i)
{
    NJS_POINT3 v;
    Float f;

    v = D_8C159490;
    f = (Float)i * 360.0f;
    v.z = twp->scl.y * njCos((Sint32)(f / twp->scl.x * 182.04444f));
    v.x = twp->scl.y * njSin((Sint32)(f / twp->scl.x * 182.04444f));
    njPushMatrix(D_8C67C640);
    njTranslateV(NULL, &twp->pos);
    if (twp->ang.z) {
        njRotateZ(NULL, twp->ang.z);
    }
    if (twp->ang.x) {
        njRotateX(NULL, twp->ang.x);
    }
    if (twp->ang.y) {
        njRotateY(NULL, twp->ang.y);
    }
    njCalcVector(NULL, &v, pos);
    njAddVector(pos, &twp->pos);
    njPopMatrix(1);
}
#else
ASM_STUB(void func_8C088000(NJS_POINT3 *pos, taskwk *twp, Sint32 i))
#endif
