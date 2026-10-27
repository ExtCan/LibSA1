/*
 * The header declares SetPlayerPosition (func_8C07CEF2) with the call-site
 * prototype guessed in code_8C081D00 (void, Sint32 args); its real signature
 * is the one defined below. Rename the header declaration out of the way.
 */
#define func_8C07CEF2 func_8C07CEF2_header_decl
#include "sa1/sa1.h"
#undef func_8C07CEF2

/*
 * Player queries (1ST_READ 8C07CE40-8C07CFA0): character id, speed, and the
 * position/angle history written every frame by func_8C06C6A0.
 */

/* Player position/angle history: 256 frames x 8 players, ring index per
 * player (see code_8C06C6A0.c). */
extern Uint8 B_8C78C688[8];             /* per-player ring index (wraps at 256) */
extern NJS_POINT3 B_8C78C690[256][8];   /* positions (0x60 per frame) */
extern Angle3 B_8C792690[256][8];       /* angles */

/*
 * Character id of player pno (taskwk.counter.b[1]); without a player task,
 * the current character (GetPlayerNumber). SADX: GetPlayerCharacterName.
 */
Sint32 func_8C07CE40(Uint8 pno)
{
    taskwk *twp;

    twp = playertwp[pno];
    if (twp == NULL) {
        return GetPlayerNumber();
    }
    return (Uint8)twp->counter.b[1];
}

/*
 * Player-space forward speed (playerwk.spd.x) of player pno when it is on
 * the ground (twp->flag & 3). Returns 1 if stored, 0 otherwise.
 */
Sint32 func_8C07CE58(Uint8 pno, Float *spd)
{
    taskwk *twp;
    playerwk *pwp;

    twp = playertwp[pno];
    pwp = playerpwp[pno];
    if (twp != NULL && (twp->flag & 3)) {
        *spd = pwp->spd.x;
    } else {
        return 0;
    }
    return 1;
}

/*
 * GetPlayerPosition: position and angle of player pno `frame` frames ago
 * (either output may be NULL). Returns 0 if the player does not exist.
 */
Sint32 func_8C07CE82(Uint8 pno, Sint32 frame, NJS_POINT3 *pos, Angle3 *ang)
{
    Uint8 idx;

    idx = B_8C78C688[pno] - frame;
    if (playertwp[pno] == NULL) {
        return 0;
    }
    if (pos != NULL) {
        *pos = B_8C78C690[idx][pno];
    }
    if (ang != NULL) {
        ang->x = B_8C792690[idx][pno].x;
        ang->y = B_8C792690[idx][pno].y;
        ang->z = B_8C792690[idx][pno].z;
    }
    return 1;
}

/*
 * SetPlayerPosition: overwrite the history entry `frame` frames ago of
 * player pno (either input may be NULL). Returns 0 if the player does not
 * exist.
 */
Sint32 func_8C07CEF2(Uint8 pno, Sint32 frame, NJS_POINT3 *pos, Angle3 *ang)
{
    Uint8 idx;

    idx = B_8C78C688[pno] - frame;
    if (playertwp[pno] == NULL) {
        return 0;
    }
    if (pos != NULL) {
        B_8C78C690[idx][pno] = *pos;
    }
    if (ang != NULL) {
        B_8C792690[idx][pno].x = ang->x;
        B_8C792690[idx][pno].y = ang->y;
        B_8C792690[idx][pno].z = ang->z;
    }
    return 1;
}
