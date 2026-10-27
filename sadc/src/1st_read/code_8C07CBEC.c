/*
 * Player requests and player-vs-point proximity tests
 * (1ST_READ 8C07CBEC-8C07CE10). Chunk asm/1st_read/text/8C07BAC0.src.
 *
 *  func_8C07CBEC  event request smode 28 with speed (x, y, z)
 *  func_8C07CC04  push player 0 out of the collision sphere of twp
 *  func_8C07CD16  first player (1-based) whose collision centre is
 *                 closer than r to pos, else 0 (SADX IsPlayerInsideSphere)
 *  func_8C07CD96  first player (1-based) inside the vertical cylinder
 *                 (radius r, half height h) around pos, else 0
 *                 (SADX CheckCollisionCylinderP)
 */
#include "common.h"
#include "ninja.h"
#include "task.h"
#include "sa1/sa1.h"

ASM_STUB(void func_8C07C676(Uint8 pno, Float x, Float y, Float z))

/* event request smode 28 with speed (x, y, z) */
void func_8C07CBEC(Uint8 pno, Float x, Float y, Float z)
{
    taskwk *twp;

    twp = playertwp[pno];
    twp->flag |= 0x1000;
    twp->smode = 28;
    func_8C07C676(pno, x, y, z);
}

/*
 * Push player 0 out of the collision sphere of twp (with 0.2 to spare).
 *
 * Complete and faithful; matches only in the original translation unit.
 * In the original object the literal pool still holds the entries of
 * func_8C07CB40..func_8C07CBBC (B_8C78C668, B_8C78C628, B_8C78C688,
 * B_8C78C690) and is dumped in the middle of this function (8C07CC9A,
 * after the BRA of the if/else); that turns the loop-entry test into the
 * far form BF/S + BRA instead of BT/S. Verified: placing before
 * func_8C07CBEC a filler function of 70 stores B_FILL<i % 4>[i] = i (four
 * pending pool entries; ASM_STUB moved to the end of the file, prototype
 * first) makes this body MATCH byte for byte (118 insns). Standalone the
 * only difference is that loop entry (2 insns).
 * The three "+=" are FMACs (pos + d * v); the SA1_FMAC spelling
 * (d * v + pos) loads pos later and changes the SHC output, hence the
 * SH_ONLY/PC_ONLY idiom (fused fmaf on PC).
 */
#ifdef NON_MATCHING
void func_8C07CC04(taskwk *twp)
{
    Sint32 i;
    taskwk *ptwp;
    CCL_INFO *info;
    Float d;
    NJS_VECTOR v;

    for (i = 0; i < 1; i++) {
        ptwp = playertwp[i];
        info = CWP(ptwp)->info;
        v.x = info->center.x - twp->pos.x;
        v.y = info->center.y - twp->pos.y;
        v.z = info->center.z - twp->pos.z;
        d = njScalor(&v);
        d -= info->a;
        d -= CWP(twp)->info->a;
        if (d < 0.0f) {
            if (njScalor(&v) == 0.0f) {
                v.x = 1.0f;
                v.y = v.z = 0.0f;
            } else {
                njUnitVector(&v);
            }
            d = -0.19999999f - d;
            ptwp->pos.x SH_ONLY(+= d * v.x) PC_ONLY(= SA1_FMAC(d, v.x, ptwp->pos.x));
            ptwp->pos.y SH_ONLY(+= d * v.y) PC_ONLY(= SA1_FMAC(d, v.y, ptwp->pos.y));
            ptwp->pos.z SH_ONLY(+= d * v.z) PC_ONLY(= SA1_FMAC(d, v.z, ptwp->pos.z));
        }
    }
}
#else
ASM_STUB(void func_8C07CC04(taskwk *twp))
#endif

/*
 * First player (1-based) whose collision centre is closer than r to pos,
 * else 0. Matching notes: pos and the centre are read through a Float
 * pointer (the original uses FMOV.S @R4+ post-increment loads; NJS_POINT3
 * is three packed Floats on every target); the continues give the
 * BRA-to-test loop shape; "d = njScalor(); d - r" (not one expression)
 * gives the njScalor/end-pointer register order R11/R12.
 */
Sint32 func_8C07CD16(NJS_POINT3 *pos, Float r)
{
    Sint32 i;
    taskwk *twp;
    colliwk *cwp;
    Float *c;
    Float x;
    Float y;
    Float z;
    Float d;
    NJS_VECTOR v;

    c = &pos->x;
    x = *c++;
    y = *c++;
    z = *c;
    for (i = 0; i < 2; i++) {
        twp = playertwp[i];
        if (twp == NULL) {
            continue;
        }
        cwp = CWP(twp);
        if (cwp == NULL) {
            continue;
        }
        c = &cwp->info->center.x;
        v.x = *c++ - x;
        v.y = *c++ - y;
        v.z = *c - z;
        d = njScalor(&v);
        if (d - r < 0.0f) {
            return i + 1;
        }
    }
    return 0;
}

/*
 * First player (1-based) within horizontal distance r and height h of pos
 * (only player 0 is tested), else 0. Matching note: r * r is written in
 * the loop (SHC hoists it); a local r2 set before the loop swaps two
 * preheader instructions.
 */
Sint32 func_8C07CD96(NJS_POINT3 *pos, Float r, Float h)
{
    Sint32 i;
    taskwk *twp;
    Float d;
    Float dy;
    NJS_VECTOR v;

    for (i = 0; i < 1; i++) {
        twp = playertwp[i];
        v.x = twp->pos.x - pos->x;
        v.y = twp->pos.y - pos->y;
        v.z = twp->pos.z - pos->z;
        d = SA1_FMAC(v.x, v.x, v.z * v.z);
        if (d - r * r > 0.0f) {
            continue;
        }
        dy = v.y;
        if (dy < 0.0f) {
            dy = -dy;
        }
        if (dy > h) {
            continue;
        }
        return i + 1;
    }
    return 0;
}
