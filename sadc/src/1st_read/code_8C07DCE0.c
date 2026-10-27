#include "common.h"
#include "ninja.h"
#include "task.h"
#include "sa1/sa1.h"

/*
 * Character voice selection (8C07DCE0).
 *
 * D_8C1BE7A0 is indexed by level and points to a list of records
 * { Sint16 character, Sint16 n, Sint16 voice[n] }, terminated by -1.
 * One voice is chosen from the record of the calling character: the
 * only one, a random one (levels 39-41), or the one of the current act
 * (clamped to n - 1). Records are used only half of the time; otherwise
 * the per-character default voice is played.
 */

extern Sint16 *D_8C1BE7A0[];                    /* per-level voice lists (data 8C1BE7A0) */
extern void func_8C107BA0(Sint32 voice);        /* PlayVoice: stores the request if voice is enabled */

#if defined(SA1_GA)
/*
 * SA1_GA (libsa1's lowered game code only; not the SHC build, the harness's SHC / TARGET_PC builds or their traces): a
 * PC-only host option (docs/research/super_general_design.md 4.9 D11, the owner's 0.9.3 notes: "Detransforming from Super
 * Sonic ... doesn't update his idle voice lines" and "Sonic has Super Sonic's voice clips in general"; libsa1
 * sa1_character_set_game_option SA1_GOPT_SUPER_IDLE_BY_FORM). The game picks Sonic's lines by the stage: level 19 (the
 * Perfect Chaos stage, where he is Super Sonic) gives Super Sonic's (396, the level-19 table), every other level Sonic's
 * (390 and the level's table). With `on` set, the stage used for Sonic's choice (chr 0) follows his form instead: level 19
 * act 0 while player 0's playerwk.equipment has 0x8000 (Super), else the world's normal level / act that libsa1 writes
 * into the block (a plain level for a world booted on level 19). CurrentLevel itself is never written. A pcdata block
 * (one per world) that libsa1 finds by its magic words (LIB src/world/sa1_world_charopt.c). While `on` is 0, the initial
 * value, the function is the original, so worlds that never set it (the reference / exactness worlds) run the game's
 * code.
 */
#define SA1_SUPERVOICE_MAGIC0 0x43494F56  /* "VOIC" (sa1_world_charopt.c CO_SUPERVOICE_*) */
#define SA1_SUPERVOICE_MAGIC1 0x30454344  /* "DCE0" */
typedef struct SA1_SUPERVOICE {
    Uint32 magic[2];            /* 0x00 SA1_SUPERVOICE_MAGIC0, SA1_SUPERVOICE_MAGIC1 */
    Sint32 on;                  /* 0x08 0: the game's choice; 1: by Sonic's form */
    Sint32 level;               /* 0x0C the level of normal Sonic's lines (libsa1: the world's, or 1 on level 19) */
    Sint32 act;                 /* 0x10 its act */
} SA1_SUPERVOICE;
SA1_ASSERT_OFFSET(SA1_SUPERVOICE, on, 0x08);
SA1_ASSERT_OFFSET(SA1_SUPERVOICE, level, 0x0C);
SA1_ASSERT_OFFSET(SA1_SUPERVOICE, act, 0x10);
static SA1_SUPERVOICE sa1_supervoice = { { SA1_SUPERVOICE_MAGIC0, SA1_SUPERVOICE_MAGIC1 }, 0, 1, 0 };
#endif

void func_8C07DCE0(taskwk *twp)
{
    Sint32 chr;
    Sint32 level;
    Sint32 act;
    Sint16 voice;
    Sint16 *tbl;
    Sint32 last;

    PC_ONLY(voice = 0;) /* DC: voice is a stale register for characters 1 and 4 */
    chr = (Uint8)twp->counter.b[1];
    level = (Uint16)GetStageNumber() >> 8;
    act = (Uint8)GetStageNumber();
#if defined(SA1_GA)
    if (sa1_supervoice.on != 0 && chr == 0) {
        if (playerpwp[0] != NULL && ((Sint16)playerpwp[0]->equipment & 0x8000)) {
            level = 19;
            act = 0;
        } else {
            level = sa1_supervoice.level;
            act = sa1_supervoice.act;
        }
    }
#endif
    if (twp->counter.b[0] != 0) {
        return;
    }
    if (func_8C049A4E() != 0) {
        return;
    }
    if (chr == 5 && level == 32 && act == 2) {
        level = 0;
    }
    tbl = D_8C1BE7A0[level];
    switch (chr) {
    case 0:
        if (level == 19) {
            voice = 396;
        } else {
            voice = 390;
        }
        break;
    case 2:
        voice = 391;
        break;
    case 3:
        voice = 392;
        break;
    case 5:
        voice = 393;
        break;
    case 6:
        voice = 394;
        break;
    case 7:
        voice = 395;
        break;
    }
    if (tbl != NULL && !((Float)rand() * (1.0f / 32768.0f) < 0.5f)) {
        while (*tbl != -1) {
            if (*tbl++ == chr) {
                voice = *tbl++;
                if (voice == 1) {
                    voice = *tbl;
                } else {
                    if (level == 39 || level == 40 || level == 41) {
                        act = SA1_FTOI((Float)rand() * (Float)voice * (1.0f / 32768.0f));
                        if (act == voice) {
                            act--;
                        }
                    }
                    voice--;
                    last = voice;
                    if (last < act) {
                        act = last;
                    }
                    voice = tbl[act];
                }
                break;
            }
            tbl += *tbl + 1;
        }
    }
    func_8C107BA0(voice);
}
