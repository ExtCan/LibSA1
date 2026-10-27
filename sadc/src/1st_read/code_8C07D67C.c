#include "common.h"
#include "ninja.h"
#include "sa1/sa1.h"

/*
 * Land position list setup (chunk 8C07D560, function 8C07D67C).
 * Called by PInitialize 8C06C350 with the new playerwk.
 */

/* allocate the 15-entry island (land position) list, reset the index */
void func_8C07D67C(playerwk *pwp) {
    pwp->island_list = CAlloc(15, sizeof(PL_LANDPOSI));
    pwp->island_num = 0;
}
