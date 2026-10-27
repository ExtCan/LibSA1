#include "common.h"
#include "ninja.h"
#include "sa1/sa1.h"

/*
 * 1ST_READ 8C0AEBC0-8C0AEFA0: Sonic standing/turning action selector
 * (one function; called by SonicMain 8C0A77BC with (twp, mwp, pwp)).
 *
 * Only acts while pwp->mj.reqaction is one of the standing actions
 * 102, 124, 107, 108, 114, 115. With stick input (GetAnalog) it picks the
 * turn action from the sign of mwp->rad (motionwk +0x34: 107 for > 0,
 * 108 for < 0) and moves pwp->mj.nframe towards a target frame
 * 4 + |rad| * 12. Without input it lets a turn action run out and returns
 * to 102 (or 124 when one of the D_8C19E38C/D_8C19E390 buttons is held),
 * and steps 102 -> 114 -> 115 while pwp->spd.x < 1.38.
 *
 * libsa1 notes: the frame run-out tests are written !(nframe > 0.0f) like
 * the FCMP/GT + BT of the original, so a NaN frame also takes them. The
 * three FMAC sites (rad * 12 + 4 for rad > 0) are SA1_FMAC; the rad < 0
 * sites are FMUL + FNEG + FADD (not fused). clang tail-merges the three
 * identical SA1_FMAC expressions into one vfmadd, so tools/fmac_audit.py
 * counts 1 fused op against 3 FMACs although every path is fused.
 */

void func_8C0AEBC0(taskwk *twp, motionwk *mwp, playerwk *pwp)
{
    Float target;

    if (pwp->mj.reqaction != 102 && pwp->mj.reqaction != 124 && pwp->mj.reqaction != 108
        && pwp->mj.reqaction != 107 && pwp->mj.reqaction != 114 && pwp->mj.reqaction != 115) {
        return;
    }
    if (GetAnalog(twp, NULL, NULL)) {
        if (mwp->rad < 0.0f) {
            if (pwp->mj.reqaction == 102 || pwp->mj.reqaction == 124 || pwp->mj.reqaction == 114
                || pwp->mj.reqaction == 115) {
                pwp->mj.reqaction = 108;
                pwp->mj.nframe = 0.0f;
                target = -(mwp->rad * 12.0f) + 4.0f;
            } else if (pwp->mj.reqaction == 107) {
                if (pwp->mj.nframe == 0.0f) {
                    pwp->mj.reqaction = 108;
                    target = -(mwp->rad * 12.0f) + 4.0f;
                } else {
                    target = 0.0f;
                }
            } else {
                target = -(mwp->rad * 12.0f) + 4.0f;
            }
        } else if (mwp->rad > 0.0f) {
            if (pwp->mj.reqaction == 102 || pwp->mj.reqaction == 124 || pwp->mj.reqaction == 114
                || pwp->mj.reqaction == 115) {
                pwp->mj.reqaction = 107;
                pwp->mj.nframe = 0.0f;
                target = SA1_FMAC(mwp->rad, 12.0f, 4.0f);
            } else if (pwp->mj.reqaction == 108) {
                if (pwp->mj.nframe == 0.0f) {
                    pwp->mj.reqaction = 107;
                    target = SA1_FMAC(mwp->rad, 12.0f, 4.0f);
                } else {
                    target = 0.0f;
                }
            } else {
                target = SA1_FMAC(mwp->rad, 12.0f, 4.0f);
            }
        } else {
            if (pwp->mj.reqaction == 102 || pwp->mj.reqaction == 124 || pwp->mj.reqaction == 114
                || pwp->mj.reqaction == 115) {
                if (B_8C754C08[(Uint8)twp->counter.b[0]].on & (D_8C19E390 | D_8C19E38C)) {
                    pwp->mj.reqaction = 124;
                }
            } else {
                pwp->mj.nframe -= 1.0f;
                if (!(pwp->mj.nframe > 0.0f)) {
                    pwp->mj.nframe = 0.0f;
                    if (B_8C754C08[(Uint8)twp->counter.b[0]].on & (D_8C19E390 | D_8C19E38C)) {
                        pwp->mj.reqaction = 124;
                    } else {
                        pwp->mj.reqaction = 102;
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
    } else if (pwp->mj.reqaction == 108 || pwp->mj.reqaction == 107) {
        pwp->mj.nframe -= 1.0f;
        if (!(pwp->mj.nframe > 0.0f)) {
            if (B_8C754C08[(Uint8)twp->counter.b[0]].on & (D_8C19E390 | D_8C19E38C)) {
                pwp->mj.reqaction = 124;
            } else {
                pwp->mj.reqaction = 102;
            }
            pwp->mj.nframe = 0.0f;
        }
    } else if (B_8C754C08[(Uint8)twp->counter.b[0]].on & (D_8C19E390 | D_8C19E38C)) {
        pwp->mj.reqaction = 124;
    } else if (pwp->mj.reqaction == 102 && pwp->spd.x < 1.3799999f) {
        pwp->mj.reqaction = 114;
    } else if (pwp->mj.reqaction == 114 && pwp->spd.x < 1.3799999f) {
        pwp->mj.reqaction = 115;
    }
}
