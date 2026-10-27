/*
 * Keyframe lookup for a looping key list (1ST_READ 8C0502A0).
 * Called by func_8C04FF58 (spawn of a 48-byte particle element) as
 * func_8C0502A0(&elem->anim, ctrl->keys, frame) after func_8C0503B6 has
 * reset the 16-byte state {frame = 0, speed = 1, index = 0, unk = 0}.
 * There ctrl = tp->awp (task +0x2C), keys = ctrl +0x1C, and the state
 * lives at +0x20 of the 0x30-byte element (ctrl +0x0C array).
 *
 * Keys are 44-byte records; a negative id ends the list. The key time at
 * +24 is cumulative: the search stops at the first key whose time is
 * greater than the frame. At the end of the list the frame is wrapped by
 * subtracting the last key's time and the search restarts at key 1.
 * Note: the frame stored in the state is the unwrapped input value.
 */
#include "common.h"

/* 44-byte key record (only the fields read here are known) */
typedef struct ANIMKEY {
    Sint16 id;          /* 0x00: < 0 terminates the list */
    Sint16 unk02;       /* 0x02 */
    Sint32 unk04[5];    /* 0x04 */
    Float time;         /* 0x18: cumulative end time */
    Sint32 unk1C[4];    /* 0x1C */
} ANIMKEY;
SA1_ASSERT_SIZE(ANIMKEY, 0x2C);
SA1_ASSERT_OFFSET(ANIMKEY, time, 0x18);

/* 16-byte animation state (initialised by func_8C0503B6) */
typedef struct ANIMSTATE {
    Float frame;        /* 0x00 */
    Float speed;        /* 0x04: 1.0 after func_8C0503B6 */
    Sint32 index;       /* 0x08: current key */
    Sint32 unk0C;       /* 0x0C */
} ANIMSTATE;
SA1_ASSERT_SIZE(ANIMSTATE, 0x10);
SA1_ASSERT_OFFSET(ANIMSTATE, index, 0x08);

void func_8C0502A0(ANIMSTATE *out, ANIMKEY *keys, Float frame)
{
    ANIMKEY *cur;
    ANIMKEY *prev;
    Sint32 i;

    prev = keys;
    cur = keys + 1;
    i = 0;
    out->frame = frame;
    for (;;) {
        if (cur->id < 0) {
            i = 0;
            frame -= prev->time;
            prev = keys;
            cur = keys;
        } else {
            if (cur->time > frame) {
                break;
            }
            i++;
            prev = cur;
        }
        cur++;
    }
    out->index = i;
}
