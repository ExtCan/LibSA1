#include "sa1/sa1.h"

/*
 * 1ST_READ chunk 8C098EE0 (part): the forced-texlist setters.
 *
 * B_8C79F0F0 and B_8C79F0F4 hold the stage-wide texlists that the stage start
 * fills (8C0990DE, called by the stage start 8C04E490 with mode 0, or mode 2
 * for levels 0x24 / 0x25: entry 0 of PTR_8C1C24A8[mode], i.e. OBJ_REGULAR
 * 0x8C19F530 in every ordinary stage; 8C09955E releases them again). They are
 * separate symbols in the original pools, so they are declared separately
 * here (docs/portability.md 2: no sym+off aliases).
 *
 *   8C099500  blob-shadow texture setup: every blob shadow (8C092124,
 *             8C09222C, 8C0922EE, 8C0923C0, 8C09247E, the ring shadows
 *             8C086D00 / 8C0877A0) and ~30 object displays call it before
 *             drawing Character_Shadow_Blob; the callers ignore the result
 *             (sa1/protos.h). gsa1_060_shadow.md fix S.
 *   8C099538  its twin for B_8C79F0F4 (footprints 0C905CA0, the Red Mountain
 *             pillars 0C9122EE and the 8C058F62.. / 8C09FB88.. displays); the
 *             overlay callers skip their own njSetTexture when it returns 1.
 */

extern NJS_TEXLIST *B_8C79F0F0;     /* stage texlist (OBJ_REGULAR in ordinary stages), set by 8C0990DE */
extern NJS_TEXLIST *B_8C79F0F4;     /* second forced texlist, tested by 8C099526 */

Sint32 func_8C099500(void)
{
    NJS_TEXLIST **tl;

    tl = &B_8C79F0F0;
    if (*tl) {
        njSetTexture(*tl);
    } else {
        return 0;
    }
    return 1;
}

Sint32 func_8C099538(void)
{
    NJS_TEXLIST **tl;

    tl = &B_8C79F0F4;
    if (*tl) {
        njSetTexture(*tl);
    } else {
        return 0;
    }
    return 1;
}
