/*
 * Player common (1ST_READ 8C0742D4-8C0747D2): one land-collision probe of
 * the PSetPosition core 8C0730D8 (called for an entry of the gathered land
 * collision list, B_8C754EF4[i]).
 *
 * func_8C0742D4 saves the csts, recomputes the collision sphere centre from
 * the push-back result (cp->pshbk) plus a height offset (4.0 in player
 * space, D_8C1BE850 for slot 5 in the D_8C1BE84C mode) converted to global
 * space, sets the sphere radius from the forward speed (4.1 at jog speed up
 * to 5.1 at dash speed, 5.1 otherwise; +0.3 for slot 7), optionally draws
 * the sphere (tcrf_CollisionDisplayVar >= 0), and runs the sphere/polygon
 * test 8C029B00 against the land object. On no hit, or when the new
 * surface is too steep (VectorAngle > 0x2C00) and not accepted, the csts is
 * restored and 0 is returned; otherwise radius and speed are restored and
 * 1 is returned.
 *
 * NON_MATCHING only because of the symbol map (same situation as
 * PInitialize in code_8C06C300.c): the three csts copies (save = *cp,
 * *cp = save) call SHC's __quick_mvn, which config/symbols.1st_read.txt maps
 * to 8C10CD38, while the game code calls 8C10CDDC. With that resolved the
 * body below compiles to the original instruction for instruction
 * (shcdiff: 3 differing words, all the __quick_mvn pool entries).
 */
/* the shared header declares 8C029B00 as (csts *, Uint32, Uint32); R5 is the
 * land object (_OBJ_LANDCOLL.pObject) and R6 its attribute (slAttribute):
 * see the call below and the caller 8C0730D8 */
#define func_8C029B00 func_8C029B00_hdr
#include "sa1/sa1.h"
#undef func_8C029B00

Sint32 func_8C029B00(csts *cp, NJS_OBJECT *object, Uint32 attr);
void func_8C079000(NJS_POINT3 *nrm, NJS_POINT3 *v, Angle angy); /* player->global along a floor normal */
void func_8C043A20(Float a, Float r, Float g, Float b);           /* njSetConstantMaterial wrapper (code_8C043A20.c) */

extern Sint32 D_8C1BE84C;               /* collision probe mode (0 in the data) */
extern Float D_8C1BE850;                /* probe height for slot 5 (5.2) */
extern Float D_8C1BE854;                /* probe radius for slot 5 (4.0) */
extern Sint8 tcrf_CollisionDisplayVar;  /* 8C752BDC, debug display level */
extern NJS_MODEL Debug_Sphere;          /* 8C199518 */

void func_8C074CF0(motionwk *mwp, playerwk *pwp, csts *cp, NJS_POINT3 *v);

#ifdef NON_MATCHING
Sint32 func_8C0742D4(taskwk *twp, csts *cp, _OBJ_LANDCOLL *lcp)
{
    csts save;
    NJS_POINT3 rad;
    NJS_POINT3 ofs;
    NJS_POINT3 v;
    Angle ang;
    motionwk *mwp;
    playerwk *pwp;
    Float s;

    mwp = playermwp[(Uint8)twp->counter.b[0]];
    pwp = playerpwp[(Uint8)twp->counter.b[0]];
    save = *cp;
    /* no code: one more reference to pwp, needed for SHC to give pwp R13
     * and twp R12 as the original does (register-allocation weight) */
    (void)pwp->flag;
    ofs.x = ofs.z = 0.0f;
    ofs.y = 4.0f;
    if (D_8C1BE84C && (Uint8)twp->counter.b[0] == 5) {
        ofs.y = D_8C1BE850;
    }
    func_8C074CF0(mwp, pwp, cp, &ofs);
    if (!D_8C1BE84C || (twp->flag & 0x2000)) {
        PConvertVector_P2G(twp, &ofs);
    } else {
        func_8C079000(&cp->normal2, &ofs, twp->ang.y);
    }
    rad.x = rad.z = 0.0f;
    rad.y = cp->radius;
    if (D_8C1BE84C && (Uint8)twp->counter.b[0] == 5) {
        rad.y = D_8C1BE854;
    }
    if (!D_8C1BE84C || (twp->flag & 0x2000)) {
        PConvertVector_P2G(twp, &rad);
    } else {
        func_8C079000(&cp->normal2, &rad, twp->ang.y);
    }
    if (!D_8C1BE84C || (twp->flag & 0x2000)) {
        if ((Uint8)twp->counter.b[0] == 7) {
            cp->pos.x = cp->onpoly.x + ofs.x;
            cp->pos.y = cp->onpoly.y + ofs.y;
            cp->pos.z = cp->onpoly.z + ofs.z;
        } else {
            cp->pos.x = cp->pshbk.x - cp->pos.x + twp->pos.x + ofs.x;
            cp->pos.y = cp->pshbk.y - cp->pos.y + twp->pos.y + ofs.y;
            cp->pos.z = cp->pshbk.z - cp->pos.z + twp->pos.z + ofs.z;
        }
    } else if (lcp->slAttribute & 0x4000) {
        cp->pos.x = cp->anaspdh.x + cp->pshbk.x - rad.x + ofs.x;
        cp->pos.y = cp->anaspdh.y + cp->pshbk.y - rad.y + ofs.y;
        cp->pos.z = cp->anaspdh.z + cp->pshbk.z - rad.z + ofs.z;
        cp->spd.x = cp->spd.y = cp->spd.z = 0.0f;
    } else {
        cp->pos.x = cp->pshbk.x - rad.x + ofs.x;
        cp->pos.y = cp->pshbk.y - rad.y + ofs.y;
        cp->pos.z = cp->pshbk.z - rad.z + ofs.z;
        ang = VectorAngle(&cp->tnorm, &cp->normal2, NULL);
        if (ang >= 0x2C00) {
            v = cp->anaspdh;
            if (njScalor(&v) >= 1.0f) {
                njUnitVector(&v);
                s = njScalor(&cp->spd);
                cp->spd.x = v.x * s;
                cp->spd.y = v.y * s;
                cp->spd.z = v.z * s;
            }
        }
    }
    if (pwp->spd.x >= pwp->p.dash_speed || (pwp->last_attr & 0x4000)
        || (pwp->spd.y <= 0.0f && (lcp->slAttribute & 0x4000))) {
        cp->radius = 5.1f;
    } else if (pwp->spd.x >= pwp->p.jog_speed && pwp->spd.x <= pwp->p.dash_speed) {
        cp->radius = (pwp->spd.x - pwp->p.jog_speed) / (pwp->p.dash_speed - pwp->p.jog_speed) + 4.1f;
    } else if (pwp->spd.x < pwp->p.jog_speed) {
        cp->radius = 4.1f;
    } else {
        cp->radius = 4.1f;
    }
    if ((Uint8)twp->counter.b[0] == 7) {
        cp->radius += 0.29999999f;
    }
    if (tcrf_CollisionDisplayVar >= 0) {
        njPushMatrix(NULL);
        njTranslate(NULL, cp->pos.x, cp->pos.y, cp->pos.z);
        func_8C043A20(0.099999996f, 1.0f, 0.39999998f, 0.79999996f);
        njScale(NULL, 0.41f, 0.41f, 0.41f);
        func_8C036F12(&Debug_Sphere);
        func_8C043A20(0.0f, 0.0f, 0.0f, 0.0f);
        njPopMatrix(1);
    }
    if (!func_8C029B00(cp, lcp->pObject, lcp->slAttribute)) {
        *cp = save;
        return 0;
    }
    ang = VectorAngle(&cp->tnorm, &cp->normal2, NULL);
    if ((ang <= 0x2C00
         || njInnerProduct(&Gravity, &cp->tnorm) > njInnerProduct(&Gravity, &cp->normal2)
         || (lcp->slAttribute & 0x4000))
        && (!(njInnerProduct(&Gravity, &cp->normal2) > 0.0f) || (pwp->flag & 3))) {
        cp->radius = save.radius;
        cp->spd = save.spd;
    } else {
        *cp = save;
        return 0;
    }
    return 1;
}
#else
ASM_STUB(Sint32 func_8C0742D4(taskwk *twp, csts *cp, _OBJ_LANDCOLL *lcp))
#endif

ASM_STUB(void func_8C074CF0(motionwk *mwp, playerwk *pwp, csts *cp, NJS_POINT3 *v))
