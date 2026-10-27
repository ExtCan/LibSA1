#include "common.h"

/* 512 one-byte story/event flags (see func_8C090686, which clears one). */
extern Uint8 B_8C799920[];

Bool func_8C09068E(Sint32 i) {
    return B_8C799920[i] != 0;
}
