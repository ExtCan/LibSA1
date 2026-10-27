#include "common.h"
#include "ninja.h"
#include "sa1/sa1.h"

/*
 * 1ST_READ 8C0BB476-8C0BBA4E (chunk 8C0BAFC0-8C0BDC80, functions 4-5 of 29):
 * two Tails standing actions, both called by TailsMain (8C0B6148) with
 * (twp, mwp, pwp). They are Tails' copies of Sonic's func_8C0AE97E and
 * func_8C0AEBC0 (code_8C0AE760.c, code_8C0AEBC0.c) with Tails' action
 * numbers and SE.
 *  func_8C0BB476  lowers the position by the character height (rotated into
 *                 global space), then steps pwp->mj.nframe by 2 in the
 *                 direction of pwp->spd.x through the 0..32 cycle; at the
 *                 rest frames 16 / 30 without stick input (GetAnalog) it
 *                 switches to twp->mode 38. Plays SE 0x472 at frame 16
 *                 (going forward) or 30 (going back) with probability
 *                 31/128 (frame counter func_8C08A71E() & 127).
 *  func_8C0BB68A  standing/turning action selector: acts only while
 *                 pwp->mj.reqaction is one of 107, 127, 113, 112, 119, 120.
 *                 With stick input it picks the turn action from the sign of
 *                 mwp->rad (113 for < 0, 112 for > 0) and moves nframe
 *                 towards 4 + |rad| * 12; without input it lets a turn run
 *                 out and returns to 107 (or 127 while one of the
 *                 D_8C19E38C/D_8C19E390 buttons is held), and steps
 *                 107 -> 119 -> 120 while pwp->spd.x < 1.38.
 */

/*
 * Literal pool layout. The original translation unit is the whole chunk
 * 8C0BAFC0-8C0BDC80. There func_8C0BB476's first pool is dumped after the
 * spd.x > 0 branch (8C0BB5C0), before the spd.x < 0 test, which gives
 * "BT/S body; BRA else" at 8C0BB4FC. Compiled alone the pool lands one block
 * later. One stub before it leaves a pending __asm_stub_sink pool entry that
 * reproduces the original dump (one or two stubs work, none or three do
 * not). The splicer ignores the stub body and keeps the original assembly.
 */
ASM_STUB(void func_8C0BB22A(taskwk *twp, motionwk *mwp, playerwk *pwp))

void func_8C0BB476(taskwk *twp, motionwk *mwp, playerwk *pwp)
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
                    func_8C0940DA(0x472, 0, 0, 0);
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
                    func_8C0940DA(0x472, 0, 0, 0);
                }
            }
        }
    } else {
        pwp->spd.x = 0.0f;
    }
}

void func_8C0BB68A(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    Float target;

    if (pwp->mj.reqaction != 107 && pwp->mj.reqaction != 127 && pwp->mj.reqaction != 113
        && pwp->mj.reqaction != 112 && pwp->mj.reqaction != 119 && pwp->mj.reqaction != 120) {
        return;
    }
    if (GetAnalog(twp, NULL, NULL)) {
        if (mwp->rad < 0.0f) {
            if (pwp->mj.reqaction == 107 || pwp->mj.reqaction == 127 || pwp->mj.reqaction == 119
                || pwp->mj.reqaction == 120) {
                pwp->mj.reqaction = 113;
                pwp->mj.nframe = 0.0f;
                target = -(mwp->rad * 12.0f) + 4.0f;
            } else if (pwp->mj.reqaction == 112) {
                if (pwp->mj.nframe == 0.0f) {
                    pwp->mj.reqaction = 113;
                    target = -(mwp->rad * 12.0f) + 4.0f;
                } else {
                    target = 0.0f;
                }
            } else {
                target = -(mwp->rad * 12.0f) + 4.0f;
            }
        } else if (mwp->rad > 0.0f) {
            if (pwp->mj.reqaction == 107 || pwp->mj.reqaction == 127 || pwp->mj.reqaction == 119
                || pwp->mj.reqaction == 120) {
                pwp->mj.reqaction = 112;
                pwp->mj.nframe = 0.0f;
                target = SA1_FMAC(mwp->rad, 12.0f, 4.0f);
            } else if (pwp->mj.reqaction == 113) {
                if (pwp->mj.nframe == 0.0f) {
                    pwp->mj.reqaction = 112;
                    target = SA1_FMAC(mwp->rad, 12.0f, 4.0f);
                } else {
                    target = 0.0f;
                }
            } else {
                target = SA1_FMAC(mwp->rad, 12.0f, 4.0f);
            }
        } else {
            if (pwp->mj.reqaction == 107 || pwp->mj.reqaction == 127 || pwp->mj.reqaction == 119
                || pwp->mj.reqaction == 120) {
                if (B_8C754C08[(Uint8)twp->counter.b[0]].on & (D_8C19E390 | D_8C19E38C)) {
                    pwp->mj.reqaction = 127;
                }
            } else {
                pwp->mj.nframe -= 1.0f;
                if (pwp->mj.nframe <= 0.0f) {
                    pwp->mj.nframe = 0.0f;
                    if (B_8C754C08[(Uint8)twp->counter.b[0]].on & (D_8C19E390 | D_8C19E38C)) {
                        pwp->mj.reqaction = 127;
                    } else {
                        pwp->mj.reqaction = 107;
                    }
                }
            }
            target = pwp->mj.nframe;
        }
        if (pwp->mj.nframe > target) {
            if (target == 0.0f) {
                pwp->mj.nframe -= 2.0f;
                if (pwp->mj.nframe < 0.0f) {
                    pwp->mj.nframe = 0.0f;
                }
            } else if (pwp->mj.nframe - target > 1.0f) {
                if (_builtin_fabsf(mwp->rad) > 0.5f) {
                    pwp->mj.nframe -= _builtin_fabsf(mwp->rad);
                } else {
                    pwp->mj.nframe -= 0.5f;
                }
            } else {
                pwp->mj.nframe = target;
            }
        } else if (target > pwp->mj.nframe) {
            if (target - pwp->mj.nframe > 1.0f) {
                if (_builtin_fabsf(mwp->rad) > 0.5f) {
                    pwp->mj.nframe += _builtin_fabsf(mwp->rad);
                } else {
                    pwp->mj.nframe += 0.5f;
                }
            } else {
                pwp->mj.nframe = target;
            }
        }
    } else if (pwp->mj.reqaction == 113 || pwp->mj.reqaction == 112) {
        pwp->mj.nframe -= 1.0f;
        if (pwp->mj.nframe <= 0.0f) {
            if (B_8C754C08[(Uint8)twp->counter.b[0]].on & (D_8C19E390 | D_8C19E38C)) {
                pwp->mj.reqaction = 127;
            } else {
                pwp->mj.reqaction = 107;
            }
            pwp->mj.nframe = 0.0f;
        }
    } else if (B_8C754C08[(Uint8)twp->counter.b[0]].on & (D_8C19E390 | D_8C19E38C)) {
        pwp->mj.reqaction = 127;
    } else if (pwp->mj.reqaction == 107 && pwp->spd.x < 1.3799999f) {
        pwp->mj.reqaction = 119;
    } else if (pwp->mj.reqaction == 119 && pwp->spd.x < 1.3799999f) {
        pwp->mj.reqaction = 120;
    }
}
