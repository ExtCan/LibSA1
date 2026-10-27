/*
 * image_guard.h - the overlay image guard of native routing (harness/README.md "Overlay image guard").
 *
 * Overlays that share a load slot (STGxx, B_CHAOS7, MINICART, ADV02, ... at 0x0C900000) reuse the same
 * DC code addresses: 0C914D60 is Super Sonic's trigger in B_CHAOS7 and a 6-byte wrapper in MINICART.
 * A native function compiled from src/overlays/<dir>/ may only replace the original while the image
 * loaded at its slot is <DIR> itself.  harness/tools/gen_native.py and gen_ga.py record the image of
 * every native function and, per image, the code ranges of config/overlays/<dir>.yaml and the CRC-32
 * of those bytes in baserom/overlays/<DIR>.BIN; the trace command (t_sim.c) replaces the routing hook
 * of each overlay function by a guard hook that runs the native code only while the CRC-32 of the
 * same ranges in guest RAM matches (checked once per guard epoch: every logic step, every file the
 * GD-ROM HLE reads, every set-up phase), and the original otherwise.
 */
#ifndef IMAGE_GUARD_H
#define IMAGE_GUARD_H

#include <stdint.h>

typedef struct sa1h_fn_image {
    const char *name;           /* overlay image (the overlay file's name): "B_CHAOS7" */
    uint32_t crc32;             /* CRC-32 of the code ranges, in order, in baserom/overlays/<name>.BIN */
    uint32_t code_bytes;        /* their total size (0: the BIN or the config was missing: never live) */
    int first_range, nranges;   /* into sa1h_fn_image_ranges[] */
} sa1h_fn_image;

/* generated (native_registry.c) */
extern const sa1h_fn_image sa1h_fn_images[];
extern const int sa1h_fn_image_count;
extern const uint32_t sa1h_fn_image_ranges[][2];    /* [lo, hi) DC addresses */
extern const signed char sa1h_native_fn_image[];    /* per sh4_native_fns[] entry: index into
                                                       sa1h_fn_images, -1 = the base image (1ST_READ) */

#endif