/*
 * Player face (expression) sequence select/clear.
 * Chunk asm/1st_read/text/8C07B2B8.src, functions 8C07B2B8-8C07B2DC.
 */
#include "common.h"
#include "ninja.h"
#include "task.h"
#include "sa1/sa1.h"

/* face sequences: 2-byte steps {time, face}, ended by a step with time 0
 * (game data; also used by the event-work face player 8C04BC70) */
extern const char *D_8C19C32C[];

/*
 * Face work of a player (playerwk.pfp, 0x070). It is the motionwk slot of
 * the face task, stored into playerpwp[pno]->pfp by its exec 8C07B106;
 * layout from 8C07B106. include/sa1/player.h only declares the tag, so the
 * fields known so far are completed here (header change reported).
 */
struct pl_face {
    Sint8 face;                 /* 0x00 current face (8C07B106: = face_next) */
    Sint8 face_next;            /* 0x01 face of the current step (seq[2 * step + 1]) */
    Sint8 b02;                  /* 0x02 (not seen) */
    Uint8 step;                 /* 0x03 step index into seq */
    Float frame;                /* 0x04 0..1 blend of the current step */
    Float frame_spd;            /* 0x08 1 / seq[2 * step] */
    const char *seq;            /* 0x0C face sequence (D_8C19C32C[n]), NULL = none */
};
typedef struct pl_face PL_FACE;

SA1_ASSERT_OFFSET(PL_FACE, step, 0x03);
SA1_ASSERT_OFFSET(PL_FACE, frame, 0x04);
SA1_ASSERT_OFFSET(PL_FACE, seq, 0x0C);

/* start face sequence n on player pno (callers: Sonic/Knuckles mains, events) */
void func_8C07B2B8(Uint8 pno, Sint32 n)
{
    PL_FACE *pfp;

    if (playerpwp[pno] != NULL) {
        pfp = playerpwp[pno]->pfp;
        if (pfp != NULL) {
            pfp->seq = D_8C19C32C[n];
        }
    }
}

/* stop the face sequence of player pno (Sonic/Knuckles mode machines) */
void func_8C07B2DC(Uint8 pno)
{
    PL_FACE *pfp;

    if (playerpwp[pno] != NULL) {
        pfp = playerpwp[pno]->pfp;
        if (pfp != NULL) {
            pfp->seq = NULL;
        }
    }
}
