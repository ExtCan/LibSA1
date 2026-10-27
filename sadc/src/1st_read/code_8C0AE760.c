#include "common.h"
#include "ninja.h"
#include "sa1/sa1.h"

/*
 * 1ST_READ 8C0AE760-8C0AEB94: two Sonic frame-stepping actions (chunk
 * 8C0AE760-8C0AEBC0). Both are called by SonicMain (8C0A77BC) with
 * (twp, mwp, pwp), right after the path functions func_8C0784F8 and
 * func_8C078710. They lower the position by the character height (rotated
 * into global space). With pwp->spd.x != 0 and mwp->rad (motionwk +0x34)
 * == 0 they switch to twp->mode 36 / 38 at once; otherwise they step the
 * motion frame pwp->mj.nframe by 2 in the direction of pwp->spd.x and switch
 * mode when the frame reaches a rest point and there is no stick input
 * (GetAnalog). The second one plays SE 0x461 at its rest frames with
 * probability 31/128 (frame counter func_8C08A71E() & 127).
 */

Sint32 func_8C08A71E(void);     /* frame counter (code_8C08A71E.c) */

void func_8C0AE760(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_POINT3 v;

    v.x = 0.0f;
    v.y = pwp->p.height;
    v.z = 0.0f;
    PConvertVector_P2G(twp, &v);
    njSubVector(&twp->pos, &v);
    if (pwp->spd.x != 0.0f && mwp->rad == 0.0f) {
        if (pwp->mj.nframe == 0.0f || pwp->mj.nframe >= 32.0f) {
            twp->mode = 36;
            if (pwp->mj.nframe == 0.0f) {
                pwp->mj.reqaction = 68;
            } else {
                pwp->mj.reqaction = 67;
            }
            pwp->spd.x = 0.0f;
        } else {
            twp->mode = 36;
            pwp->spd.x = 0.0f;
            if (pwp->mj.nframe >= 16.0f && pwp->mj.nframe <= 48.0f) {
                pwp->mj.reqaction = 67;
            } else {
                pwp->mj.reqaction = 68;
            }
        }
    } else if (pwp->spd.x > 0.0f) {
        if (pwp->mj.nframe >= 0.0f && pwp->mj.nframe < 32.0f) {
            pwp->mj.nframe += 2.0f;
            if (pwp->mj.nframe == 32.0f && !GetAnalog(twp, NULL, NULL)) {
                twp->mode = 36;
                pwp->mj.reqaction = 67;
                pwp->spd.x = 0.0f;
            }
        } else {
            pwp->mj.nframe += 2.0f;
            if (pwp->mj.nframe >= 63.0f) {
                pwp->mj.nframe = 0.0f;
            }
            if (pwp->mj.nframe == 0.0f && !GetAnalog(twp, NULL, NULL)) {
                twp->mode = 36;
                pwp->mj.reqaction = 68;
                pwp->spd.x = 0.0f;
            }
        }
    } else if (pwp->spd.x < 0.0f) {
        if (pwp->mj.nframe > 0.0f && pwp->mj.nframe <= 32.0f) {
            pwp->mj.nframe -= 2.0f;
            if (pwp->mj.nframe == 0.0f && !GetAnalog(twp, NULL, NULL)) {
                twp->mode = 36;
                pwp->mj.reqaction = 68;
                pwp->spd.x = 0.0f;
            }
        } else {
            pwp->mj.nframe -= 2.0f;
            if (pwp->mj.nframe < 0.0f) {
                pwp->mj.nframe = 62.0f;
            }
            if (pwp->mj.nframe == 32.0f && !GetAnalog(twp, NULL, NULL)) {
                twp->mode = 36;
                pwp->mj.reqaction = 67;
                pwp->spd.x = 0.0f;
            }
        }
    } else {
        pwp->spd.x = 0.0f;
    }
}

void func_8C0AE97E(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    NJS_POINT3 v;

    v.x = 0.0f;
    v.y = pwp->p.height;
    v.z = 0.0f;
    PConvertVector_P2G(twp, &v);
    njSubVector(&twp->pos, &v);
    if (pwp->spd.x != 0.0f && mwp->rad == 0.0f) {
        if (pwp->mj.nframe == 16.0f || pwp->mj.nframe == 30.0f) {
            twp->mode = 38;
            pwp->spd.x = 0.0f;
        }
    } else if (pwp->spd.x > 0.0f) {
        if (pwp->mj.nframe >= 0.0f && pwp->mj.nframe < 16.0f) {
            pwp->mj.nframe += 2.0f;
            if (pwp->mj.nframe == 16.0f && !GetAnalog(twp, NULL, NULL)) {
                twp->mode = 38;
                pwp->spd.x = 0.0f;
            } else if (pwp->mj.nframe == 16.0f) {
                if ((Uint32)(func_8C08A71E() & 127) < 31) {
                    func_8C0940DA(0x461, 0, 0, 0);
                }
            }
        } else {
            pwp->mj.nframe += 2.0f;
            if (pwp->mj.nframe >= 32.0f) {
                pwp->mj.nframe = 0.0f;
            }
            if (pwp->mj.nframe == 30.0f && !GetAnalog(twp, NULL, NULL)) {
                twp->mode = 38;
                pwp->spd.x = 0.0f;
            }
        }
    } else if (pwp->spd.x < 0.0f) {
        if (pwp->mj.nframe > 16.0f && pwp->mj.nframe <= 32.0f) {
            pwp->mj.nframe -= 2.0f;
            if (pwp->mj.nframe == 16.0f && !GetAnalog(twp, NULL, NULL)) {
                twp->mode = 38;
                pwp->spd.x = 0.0f;
            }
        } else {
            pwp->mj.nframe -= 2.0f;
            if (pwp->mj.nframe < 0.0f) {
                pwp->mj.nframe = 30.0f;
            }
            if (pwp->mj.nframe == 30.0f && !GetAnalog(twp, NULL, NULL)) {
                twp->mode = 38;
                pwp->spd.x = 0.0f;
            } else if (pwp->mj.nframe == 30.0f) {
                if ((Uint32)(func_8C08A71E() & 127) < 31) {
                    func_8C0940DA(0x461, 0, 0, 0);
                }
            }
        }
    } else {
        pwp->spd.x = 0.0f;
    }
}
