#include "common.h"

/*
 * 8C049A5A (chunk 8C0491C0.src, 6 B): flag n of the second story flag set
 * (the signed byte B_8C799B88[n]; 512 flags, kept as bits at SAVEDATA +0x2A0,
 * code_8C0886F4.c / code_8C088C74.c; the first set, B_8C799920 at +0x260, is
 * read by func_8C09068E). Read by the follower predicate's flag helper
 * func_8C0829E6 (code_8C08273C.c:190-193: flags 0x100, 0x1A, 0xD) and by
 * 8C04C73C, 8C04C878, 8C04DA20, 8C05F3D2, 8C0904A8.
 */
extern Sint8 B_8C799B88[512];

Sint32 func_8C049A5A(Sint32 n)
{
    return B_8C799B88[n];
}
