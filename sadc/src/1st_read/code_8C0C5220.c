#include "common.h"
#include "ninja.h"
#include "sa1/sa1.h"

/*
 * 1ST_READ 8C0C5220-8C0C5654 (chunk 8C0C5220-8C0C5680): two Knuckles
 * frame-stepping actions, both called by KnucklesMain (8C0C0640) with
 * (twp, mwp, pwp). They are Knuckles' copies of Sonic's func_8C0AE760 /
 * func_8C0AE97E (code_8C0AE760.c) and of Tails' func_8C0BB476
 * (code_8C0BB476.c) with Knuckles' mode/action numbers and SE.
 *  func_8C0C5220  lowers the position by the character height (rotated into
 *                 global space). Moving without drive (mwp->rad == 0) it
 *                 switches to twp->mode 42 at once, choosing action 103
 *                 (frame 0 or outside 16..48) or 102; otherwise it steps
 *                 pwp->mj.nframe by 2 in the direction of pwp->spd.x
 *                 through the 0..63 cycle and at the rest frames 32 / 0
 *                 without stick input (GetAnalog) switches to mode 42 with
 *                 action 102 / 103.
 *  func_8C0C543E  same with the 0..32 cycle and rest frames 16 / 30
 *                 (mode 44, no action change); plays SE 0x489 at frame 16
 *                 (going forward) or 30 (going back) with probability
 *                 31/128 (frame counter func_8C08A71E() & 127).
 */

void func_8C0C5220(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_POINT3 v;

    v.x = 0.0f;
    v.y = pwp->p.height;
    v.z = 0.0f;
    PConvertVector_P2G(twp, &v);
    njSubVector(&twp->pos, &v);
    if (pwp->spd.x != 0.0f && mwp->rad == 0.0f) {
        if (pwp->mj.nframe == 0.0f || pwp->mj.nframe >= 32.0f) {
            twp->mode = 42;
            if (pwp->mj.nframe == 0.0f) {
                pwp->mj.reqaction = 103;
            } else {
                pwp->mj.reqaction = 102;
            }
            pwp->spd.x = 0.0f;
        } else {
            twp->mode = 42;
            pwp->spd.x = 0.0f;
            if (pwp->mj.nframe >= 16.0f && pwp->mj.nframe <= 48.0f) {
                pwp->mj.reqaction = 102;
            } else {
                pwp->mj.reqaction = 103;
            }
        }
    } else if (pwp->spd.x > 0.0f) {
        if (pwp->mj.nframe >= 0.0f && pwp->mj.nframe < 32.0f) {
            pwp->mj.nframe += 2.0f;
            if (pwp->mj.nframe == 32.0f && !GetAnalog(twp, NULL, NULL)) {
                twp->mode = 42;
                pwp->mj.reqaction = 102;
                pwp->spd.x = 0.0f;
            }
        } else {
            pwp->mj.nframe += 2.0f;
            if (pwp->mj.nframe >= 63.0f) {
                pwp->mj.nframe = 0.0f;
            }
            if (pwp->mj.nframe == 0.0f && !GetAnalog(twp, NULL, NULL)) {
                twp->mode = 42;
                pwp->mj.reqaction = 103;
                pwp->spd.x = 0.0f;
            }
        }
    } else if (pwp->spd.x < 0.0f) {
        if (pwp->mj.nframe > 0.0f && pwp->mj.nframe <= 32.0f) {
            pwp->mj.nframe -= 2.0f;
            if (pwp->mj.nframe == 0.0f && !GetAnalog(twp, NULL, NULL)) {
                twp->mode = 42;
                pwp->mj.reqaction = 103;
                pwp->spd.x = 0.0f;
            }
        } else {
            pwp->mj.nframe -= 2.0f;
            if (pwp->mj.nframe < 0.0f) {
                pwp->mj.nframe = 62.0f;
            }
            if (pwp->mj.nframe == 32.0f && !GetAnalog(twp, NULL, NULL)) {
                twp->mode = 42;
                pwp->mj.reqaction = 102;
                pwp->spd.x = 0.0f;
            }
        }
    } else {
        pwp->spd.x = 0.0f;
    }
}

void func_8C0C543E(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_POINT3 v;

    v.x = 0.0f;
    v.y = pwp->p.height;
    v.z = 0.0f;
    PConvertVector_P2G(twp, &v);
    njSubVector(&twp->pos, &v);
    if (pwp->spd.x != 0.0f && mwp->rad == 0.0f) {
        if (pwp->mj.nframe == 16.0f || pwp->mj.nframe == 30.0f) {
            twp->mode = 44;
            pwp->spd.x = 0.0f;
        }
    } else if (pwp->spd.x > 0.0f) {
        if (pwp->mj.nframe >= 0.0f && pwp->mj.nframe < 16.0f) {
            pwp->mj.nframe += 2.0f;
            if (pwp->mj.nframe == 16.0f && !GetAnalog(twp, NULL, NULL)) {
                twp->mode = 44;
                pwp->spd.x = 0.0f;
            } else if (pwp->mj.nframe == 16.0f) {
                if ((Uint32)(func_8C08A71E() & 127) < 31) {
                    func_8C0940DA(0x489, 0, 0, 0);
                }
            }
        } else {
            pwp->mj.nframe += 2.0f;
            if (pwp->mj.nframe >= 32.0f) {
                pwp->mj.nframe = 0.0f;
            }
            if (pwp->mj.nframe == 30.0f && !GetAnalog(twp, NULL, NULL)) {
                twp->mode = 44;
                pwp->spd.x = 0.0f;
            }
        }
    } else if (pwp->spd.x < 0.0f) {
        if (pwp->mj.nframe > 16.0f && pwp->mj.nframe <= 32.0f) {
            pwp->mj.nframe -= 2.0f;
            if (pwp->mj.nframe == 16.0f && !GetAnalog(twp, NULL, NULL)) {
                twp->mode = 44;
                pwp->spd.x = 0.0f;
            }
        } else {
            pwp->mj.nframe -= 2.0f;
            if (pwp->mj.nframe < 0.0f) {
                pwp->mj.nframe = 30.0f;
            }
            if (pwp->mj.nframe == 30.0f && !GetAnalog(twp, NULL, NULL)) {
                twp->mode = 44;
                pwp->spd.x = 0.0f;
            } else if (pwp->mj.nframe == 30.0f) {
                if ((Uint32)(func_8C08A71E() & 127) < 31) {
                    func_8C0940DA(0x489, 0, 0, 0);
                }
            }
        }
    } else {
        pwp->spd.x = 0.0f;
    }
}
