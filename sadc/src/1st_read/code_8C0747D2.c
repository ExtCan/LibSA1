#include "sa1/sa1.h"

/*
 * Player common (1ST_READ 8C0747D2-8C074CF0): wall contact responses of the
 * PSetPosition collision cores. Both take (twp, mwp, pwp, cp = pwp->cstsp,
 * pos = position before the move, ofs = offset from the collision sphere
 * centre to twp->pos, ret = status so far, attr = collision attribute mask)
 * and return the new status (1 = sliding along / holding the wall,
 * 2 = stopped by the wall).
 *  func_8C0747D2  wall hit in PSetPosition core 8C0730D8 (BSR, two sites).
 *                 Copies the wall normal (cp->normal2) into pwp->wall_normal
 *                 and takes the move direction: on the ground (twp->flag & 1)
 *                 pos - twp->pos, in the air -mwp->spd (player-space -X
 *                 after a CCL push-back, D_8C1BDFE0). With attr & 0x200000
 *                 the player always slides (func_8C0763A0) unless the wall
 *                 faces away (angle > 0x4000). Otherwise: at spd.x >= 15 the
 *                 speed is cleared (PClearSpeed, cp->anaspdh = 0, status 2);
 *                 on the ground a steep hit (angle <= 0x1000) slides below
 *                 crash_speed and stops above it, a glancing one slides; in
 *                 the air Knuckles grabs the wall (angle <= 0x3400, no
 *                 attr 0x201000), the others slide (the inner-product test
 *                 against 4.0 can never fail for unit vectors, so the
 *                 air-borne clear/stop branches are dead in practice).
 *                 Always ends by moving the player to the push-back
 *                 position: cp->pos = cp->pshbk, twp->pos = pshbk - ofs,
 *                 cp->spd = cp->anaspdh (the early "return ret" paths skip it).
 *  func_8C074AFC  the same for the alternate core 8C0777E0 (called through a
 *                 pointer table): no speed checks, slide helper
 *                 func_8C076640, falls back to -mwp->spd when the ground
 *                 move is zero; the inner product is computed and unused.
 *
 * NON_MATCHING (both). The C is complete and follows the original control
 * flow; what differs is register allocation and block merging:
 *  1. func_8C0763A0 / func_8C076640 are called 5 / 3 times. SHC keeps the
 *     address of an extern callee used that often in a callee-saved
 *     register; the original loads it from the pool at every call, which is
 *     what SHC does for a callee in the same translation unit but out of
 *     BSR range (both are ~7 KB further on, so the original TU spans at
 *     least 8C0747D2-8C076640). Same situation as PGetAcceleration
 *     (code_8C06D254.c). The NON_MATCHING SHC compile below emulates it with
 *     stubs in another section (test aid; shcdiff then reports those pool
 *     loads as "data differs" because the label is local).
 *  2. With that aid, func_8C0747D2 gets the original callee-saved set but
 *     with the constants 2 and 0x4000 swapped (R8/R9), and SHC cross-jumps
 *     two of the five "ret = 1; func_8C0763A0(...)" blocks that the
 *     original keeps separate (12 vs 14 calls), which shifts the layout.
 *  3. func_8C074AFC shares its first literal pool with func_8C0747D2's tail
 *     (the original's L8C074C60 entry) and the original's then-arm of the
 *     ground case ends in a "BRA" trampoline into the else-arm's call
 *     (late cross-jump); neither was reproduced.
 * Verify: py -3.12 tools/shcdiff.py func_8C0747D2 src/1st_read/code_8C0747D2.c
 *         --flags=-define=NON_MATCHING   (same for func_8C074AFC)
 */

extern Sint32 D_8C1BDFE0;               /* set by the CCL push-back (code_8C06CA30.c), cleared every frame */

/* wall slide helpers (8C0763A0 / 8C076640, JSR; same original TU) */
void func_8C0763A0(taskwk *twp, playerwk *pwp, csts *cp, NJS_POINT3 *v);
void func_8C076640(taskwk *twp, playerwk *pwp, csts *cp, NJS_POINT3 *v);

#ifdef NON_MATCHING
Sint32 func_8C0747D2(taskwk *twp, motionwk *mwp, playerwk *pwp, csts *cp,
                     NJS_POINT3 *pos, NJS_POINT3 *ofs, Sint32 ret, Uint32 attr)
{
    NJS_POINT3 v;
    Sint32 ang;
    Float ip;
    Float lim;

    pwp->wall_normal = cp->normal2;
    if (twp->flag & 1) {
        v.x = pos->x - twp->pos.x;
        v.y = pos->y - twp->pos.y;
        v.z = pos->z - twp->pos.z;
        if (njScalor(&v) == 0.0f) {
            goto end;
        }
        njUnitVector(&v);
        ang = VectorAngle(&v, &cp->normal2, NULL);
        if (attr & 0x200000) {
            if (ang > 0x4000) {
                return ret;
            }
            ret = 1;
            func_8C0763A0(twp, pwp, cp, &v);
            goto end;
        }
        if (pwp->spd.x >= 15.0f) {
            cp->anaspdh.x = cp->anaspdh.y = cp->anaspdh.z = 0.0f;
            ret = 2;
            PClearSpeed(NULL, pwp);
            goto end;
        }
        if (ang <= 0x1000) {
            if (pwp->spd.x >= pwp->p.crash_speed) {
                ret = 2;
                goto end;
            }
            ret = 1;
            func_8C0763A0(twp, pwp, cp, &v);
            goto end;
        }
        if (ang > 0x4000) {
            goto end;
        }
        ret = 1;
        func_8C0763A0(twp, pwp, cp, &v);
        goto end;
    } else {
        lim = 4.0f;             /* held in FR14 across the calls (original register use) */
        if (D_8C1BDFE0) {
            v.x = -1.0f;
            v.y = 0.0f;
            v.z = 0.0f;
            PConvertVector_P2G(twp, &v);
        } else {
            v.x = -mwp->spd.x;
            v.y = -mwp->spd.y;
            v.z = -mwp->spd.z;
        }
        if (njScalor(&v) == 0.0f) {
            goto end;
        }
        njUnitVector(&v);
        ang = VectorAngle(&v, &cp->normal2, NULL);
        ip = njInnerProduct(&v, &cp->normal2);
        if (attr & 0x200000) {
            if (ang > 0x4000) {
                return ret;
            }
            ret = 1;
            func_8C0763A0(twp, pwp, cp, &v);
            goto end;
        }
        if (pwp->spd.x >= 15.0f) {
            if (ip < lim) {
                goto grab;
            }
            cp->anaspdh.x = cp->anaspdh.y = cp->anaspdh.z = 0.0f;
            ret = 2;
            PClearSpeed(NULL, pwp);
            goto end;
        }
        if (ip < lim) {
            goto grab;
        }
        if (pwp->spd.x >= pwp->p.crash_speed) {
            ret = 2;
            goto end;
        }
        ret = 1;
        goto end;
grab:
        if ((Uint8)twp->counter.b[1] == SA1_CHR_KNUCKLES && ang <= 0x3400 && !(attr & 0x201000)) {
            ret = 1;
            goto end;
        }
        if (ang >= 0x4000) {
            return ret;
        }
        func_8C0763A0(twp, pwp, cp, &v);
    }
end:
    cp->pos = cp->pshbk;
    twp->pos.x = cp->pshbk.x - ofs->x;
    twp->pos.y = cp->pshbk.y - ofs->y;
    twp->pos.z = cp->pshbk.z - ofs->z;
    cp->spd = cp->anaspdh;
    return ret;
}
#else
ASM_STUB(Sint32 func_8C0747D2(taskwk *twp, motionwk *mwp, playerwk *pwp, csts *cp,
                     NJS_POINT3 *pos, NJS_POINT3 *ofs, Sint32 ret, Uint32 attr))
#endif

#ifdef NON_MATCHING
Sint32 func_8C074AFC(taskwk *twp, motionwk *mwp, playerwk *pwp, csts *cp,
                     NJS_POINT3 *pos, NJS_POINT3 *ofs, Sint32 ret, Uint32 attr)
{
    NJS_POINT3 v;
    Sint32 ang;

    pwp->wall_normal = cp->normal2;
    if (twp->flag & 1) {
        v.x = pos->x - twp->pos.x;
        v.y = pos->y - twp->pos.y;
        v.z = pos->z - twp->pos.z;
        if (njScalor(&v) != 0.0f) {
            njUnitVector(&v);
            ang = VectorAngle(&v, &cp->normal2, NULL);
            if (ang >= 0x4000) {
                return ret;
            }
            ret = 1;
            func_8C076640(twp, pwp, cp, &v);
        } else {
            v.x = -mwp->spd.x;
            v.y = -mwp->spd.y;
            v.z = -mwp->spd.z;
            if (njScalor(&v) == 0.0f) {
                return ret;
            }
            njUnitVector(&v);
            ang = VectorAngle(&v, &cp->normal2, NULL);
            if (ang >= 0x4000) {
                return ret;
            }
            ret = 1;
            func_8C076640(twp, pwp, cp, &v);
        }
    } else {
        v.x = -mwp->spd.x;
        v.y = -mwp->spd.y;
        v.z = -mwp->spd.z;
        if (njScalor(&v) != 0.0f) {
            njUnitVector(&v);
            ang = VectorAngle(&v, &cp->normal2, NULL);
            njInnerProduct(&v, &cp->normal2);
            if (attr & 0x200000) {
                if (ang >= 0x4000) {
                    return ret;
                }
                ret = 1;
                func_8C076640(twp, pwp, cp, &v);
                goto end;
            }
            if (ang >= 0x4000) {
                return ret;
            }
            func_8C076640(twp, pwp, cp, &v);
        }
    }
end:
    cp->pos = cp->pshbk;
    twp->pos.x = cp->pshbk.x - ofs->x;
    twp->pos.y = cp->pshbk.y - ofs->y;
    twp->pos.z = cp->pshbk.z - ofs->z;
    cp->spd = cp->anaspdh;
    return ret;
}
#else
ASM_STUB(Sint32 func_8C074AFC(taskwk *twp, motionwk *mwp, playerwk *pwp, csts *cp,
                     NJS_POINT3 *pos, NJS_POINT3 *ofs, Sint32 ret, Uint32 attr))
#endif

#if defined(NON_MATCHING) && !defined(TARGET_PC)
/* Test aid only (see item 1 above): same-TU callees out of BSR range, so SHC
 * loads their addresses at each call like the original. */
#pragma section STUB
ASM_STUB(void func_8C0763A0(taskwk *twp, playerwk *pwp, csts *cp, NJS_POINT3 *v))
ASM_STUB(void func_8C076640(taskwk *twp, playerwk *pwp, csts *cp, NJS_POINT3 *v))
#pragma section
#endif
